/* Run-length stage of the DSI resource container.
 *
 * The escape table is POSITIONAL, which is the thing that makes or breaks a
 * reimplementation:
 *   slot 0        run count in the next byte
 *   slot 1        a run of one - and the byte-sequence delimiter
 *   slot 2        run count in the next two bytes, little endian
 *   slot i >= 3   a run of exactly i
 *
 * Two sub-passes. On decode the sequence pass runs first, unless bit 7 of the
 * escape count says to skip it; so on encode the single-byte pass runs first
 * and the sequence pass runs over its output.
 *
 * Three encoding rules were recovered from the shipped files:
 *
 *  - A literal escape byte is emitted through slot 0 with an explicit count of
 *    one (three bytes), NOT through slot 1, which also means "a run of one" and
 *    would cost two. Slot 1 is reserved as the sequence delimiter, and routing
 *    literals around it is what guarantees the delimiter never appears bare in
 *    the stream - which is what makes the sequence pass decodable at all.
 *
 *  - A sequence is taken whenever it saves anything. Saving a single byte is
 *    the most common case shipped (2232 of 4570 sequences), so this is not a
 *    threshold rule.
 *
 *  - A sequence may never consume the final byte of the stream.
 */
#include <stdio.h>
#include "skidpack.h"

#define SEQ_MAX_LEN 64
#define SEQ_MAX_REP 255

/* memchr over a huge pointer, which the 16-bit library's memchr cannot take. */
static int contains_byte(rs_cbytep p, rs_size n, unsigned char v)
{
    rs_size i;
    for (i = 0; i < n; ++i)
        if (p[i] == v) return 1;

    return 0;
}

/* Does this look like the start of another pass?
 *
 * Used to choose between the two payload dialects: a wrong guess produces
 * arbitrary bytes, and one byte in 128 of arbitrary data begins with a valid
 * compression type. Checking the whole header instead of just the type byte
 * makes a false accept far less likely. It is a filter, not a proof; the pass
 * that follows still has to decode. */
static int next_pass_plausible(rs_cbytep p, rs_size n)
{
    unsigned esclen;

    if (n < 10) return 0;

    switch (p[0]) {
    case RS_TYPE_VLE:
        esclen = p[4] & 0x7F;
        return esclen >= 1 && esclen <= RS_VLE_MAX_WIDTH &&
               rs_size24(p + 1) > 0 && (rs_size)(5 + esclen) <= n;

    case RS_TYPE_RLE:
        esclen = p[8] & 0x7F;
        return esclen >= 1 && esclen <= RS_VLE_MAX_WIDTH &&
               rs_size24(p + 1) > 0 && (rs_size)(9 + esclen) <= n &&
               rs_size24(p + 4) <= n;

    default:
        return 0;
    }
}

/* ---------------------------------------------------------------- decode -- */

int rs_rle_decode(rs_cbytep src, rs_size srclen, rs_buf *out)
{
    static unsigned char lookup[256];
    unsigned char        esc[RS_VLE_MAX_WIDTH];
    rs_size              outlen, seqlen, bodylen, p, q, end;
    unsigned             esclenb, esclen, skipseq;
    rs_cbytep            body;
    rs_buf               tmp;
    int                  i, rc = -1;

    if (srclen < 10) return -1;
    outlen = rs_size24(src + 1);
    seqlen = rs_size24(src + 4);
    esclenb = src[8];
    skipseq = (esclenb & 0x80) ? 1u : 0u;
    esclen = esclenb & 0x7F;
    if (esclen == 0 || esclen > RS_VLE_MAX_WIDTH) return -1;

    /* The sequence pass reads esc[1] as its delimiter, so a one-entry table
     * cannot support it. Reading it anyway is an uninitialised read. */
    if (!skipseq && esclen < 2) return -1;
    if ((rs_size)(9 + esclen) > srclen) return -1;

    /* Every field is checked before this point, deliberately. outlen is a
     * 24-bit number the header supplies and nothing else has vouched for, so
     * reserving on it first means a header that fails validation one line
     * later can still ask for 16 MB - which a 16-bit host answers by
     * exhausting its far heap. */
    rs_buf_reserve(out, outlen); /* the header states the size; use it */
    if (out->err) return -1;     /* no point decoding into a dead buffer */

    for (i = 0; i < (int)esclen; ++i) esc[i] = src[9 + i];

    /* Later slot wins, deliberately.
     *
     * Shipped files DO repeat a byte across slots. SDGAME2.PVS carries the
     * table 10 16 16 17 1A 1B 1C 1D 1E 1F, where slots 1 and 2 are both 0x16.
     * It decodes because the two meanings live in different passes: slot 1 is
     * read directly as the sequence delimiter, while lookup[] is consulted
     * only by the single-byte pass, where 0x16 means slot 2. It is safe in
     * this file because no run needs a 16-bit count, so 0x16 is never emitted
     * as a slot-2 marker either.
     *
     * Rejecting duplicates therefore rejects real files: seven resources in
     * each of the four releases, all sound data, stop decoding. */
    for (i = 0; i < 256; ++i) lookup[i] = 0;
    for (i = 0; i < (int)esclen; ++i) lookup[esc[i]] = (unsigned char)(i + 1);

    rs_buf_init(&tmp);
    if (!skipseq) {
        unsigned char d = esc[RS_RLE_ESCSEQ_POS];
        q = (rs_size)(9 + esclen);

        /* Refuse a body that runs past the end rather than quietly shortening
         * it to fit: a header disagreeing with the file is damage, not an
         * instruction. Subtraction so q + seqlen cannot wrap. */
        if (seqlen > srclen - q) goto done;

        end = q + seqlen;
        while (q < end) {
            unsigned char c = src[q++];
            if (c == d) {
                rs_size  s0 = q, s1;
                unsigned repbyte, extra;
                while (q < end && (c = src[q++]) != d) rs_buf_push(&tmp, c);

                /* Ran out before the closing delimiter, or before the count
                 * that must follow it: the token is incomplete and the stream
                 * is malformed. This used to break out and carry on with
                 * whatever had been produced so far. */
                if (q >= end) goto done;

                repbyte = src[q++];

                /* The original decoder holds this in an unsigned char and
                 * subtracts one, so a stored 0 wraps to 255 and copies 256
                 * times in all. Taken from file_decomp_rle_seq in restunts'
                 * fileio.c rather than inferred. */
                extra = (repbyte ? repbyte : 256u) - 1u;
                s1 = q;

                /* Bound the expansion before performing it. A token is at most
                 * 64 bytes and may repeat 256 times, so a handful of them can
                 * ask for far more than the format can describe. Nothing may
                 * exceed the 24-bit ceiling, and checking after the fact means
                 * the allocation has already happened. */
                if (s1 >= s0 + 2) {
                    rs_size seqlen_tok = (s1 - s0) - 2;
                    if (extra > 0 && seqlen_tok > 0 &&
                        seqlen_tok > (RS_SIZE24_MAX - tmp.len) / extra)
                        goto done;
                }

                while (extra--) {
                    rs_size r = s0;
                    while (r + 2 < s1) rs_buf_push(&tmp, src[r++]);
                }
                q = s1;
            } else
                rs_buf_push(&tmp, c);
        }
        if (tmp.err) goto done;
        body = tmp.data;
        bodylen = tmp.len;
        p = 0;
    } else {
        body = src;
        bodylen = srclen;
        p = (rs_size)(9 + esclen);
    }

    while (out->len < outlen && p < bodylen) {
        unsigned char b = body[p++];
        unsigned      n = lookup[b];
        rs_size       cnt;
        unsigned char val;
        if (n == 0) {
            rs_buf_push(out, b);
            continue;
        }

        /* n is the escape slot, one-based: lookup[] holds i+1 for esc[i]. The
         * table is positional, so the slot alone says how the count is
         * carried. */
        switch (n) {
        case 1: /* esc[0]: 8-bit count */
            if (p + 1 >= bodylen) goto done;
            cnt = body[p++];
            val = body[p++];
            break;

        case 3: /* esc[2]: 16-bit count */
            if (p + 2 >= bodylen) goto done;
            cnt = (rs_size)body[p] | ((rs_size)body[p + 1] << 8);
            p += 2;
            val = body[p++];
            break;

        default: /* esc[i]: a run of exactly i */
            if (p >= bodylen) goto done;
            cnt = n - 1;
            val = body[p++];
            break;
        }

        /* A run may not write past the length the header declared. Without
         * this a malformed count expands well beyond the output before the
         * final size comparison notices. */
        if (cnt > outlen - out->len) goto done;

        while (cnt--) rs_buf_push(out, val);
        if (out->err) goto done;
    }
    rc = (!out->err && out->len == outlen) ? 0 : -1;

done:
    rs_buf_free(&tmp);
    return rc;
}

/* ---------------------------------------------------------------- encode -- */

/* used[] records which slots this actually emitted, so the caller can tell
 * whether a repeated escape byte matters for this particular payload. */
static void single_compress(rs_cbytep p, rs_size plen,
                            const unsigned char esc[RS_RLE_NESC],
                            unsigned char used[RS_RLE_NESC], rs_buf *o)
{
    static unsigned char isesc[256];
    rs_size              i = 0;
    int                  k;

    for (k = 0; k < 256; ++k) isesc[k] = 0;
    for (k = 0; k < RS_RLE_NESC; ++k) isesc[esc[k]] = 1;
    for (k = 0; k < RS_RLE_NESC; ++k) used[k] = 0;

    while (i < plen) {
        unsigned char v = p[i];
        rs_size       n = 1, left;
        while (i + n < plen && p[i + n] == v) ++n;
        left = n;
        while (left > 0) {
            if (left >= 3 && left <= 9) {
                used[left] = 1;
                rs_buf_push(o, esc[left]);
                rs_buf_push(o, v);
                left = 0;
            } else if (left >= 10 && left <= 255) {
                used[0] = 1;
                rs_buf_push(o, esc[0]);
                rs_buf_push(o, (unsigned char)left);
                rs_buf_push(o, v);
                left = 0;
            } else if (left > 255) {
                rs_size take = left > 65535ul ? 65535ul : left;
                used[2] = 1;
                rs_buf_push(o, esc[2]);
                rs_buf_push(o, (unsigned char)(take & 0xFF));
                rs_buf_push(o, (unsigned char)(take >> 8));
                rs_buf_push(o, v);
                left -= take;
            } else {
                if (isesc[v]) {
                    used[0] = 1;
                    rs_buf_push(o, esc[0]);
                    rs_buf_push(o, (unsigned char)left);
                    rs_buf_push(o, v);
                } else {
                    while (left--) rs_buf_push(o, v);
                }
                left = 0;
            }
        }
        i += n;
    }
}

/* Would the decoder read back every marker this encoding emitted?
 *
 * Repeated escape bytes are legal and shipped files use them, so a blanket
 * uniqueness rule is wrong. What matters is narrower: the decoder builds its
 * lookup table in slot order, so a later slot holding the same byte shadows an
 * earlier one. That only breaks the file if the shadowed slot was actually
 * emitted.
 *
 * SDGAME2.PVS is the case to keep working. Slots 1 and 2 are both 0x16, and
 * slot 2 is never emitted because no run there needs a 16-bit count, so
 * nothing is shadowed in practice. A table supplied with -esc can be less
 * lucky: with slots 0 and 2 sharing a byte, a run of ten emits a slot-0 marker
 * that reads back as slot 2.
 *
 * Slot 1 is excluded: single_compress never emits it, and its safety as the
 * sequence delimiter is checked separately. */
static int markers_survive_lookup(const unsigned char esc[RS_RLE_NESC],
                                  const unsigned char used[RS_RLE_NESC])
{
    unsigned char lookup[256];
    int           i;

    for (i = 0; i < 256; ++i) lookup[i] = 0;
    for (i = 0; i < RS_RLE_NESC; ++i) lookup[esc[i]] = (unsigned char)(i + 1);

    for (i = 0; i < RS_RLE_NESC; ++i) {
        if (i == RS_RLE_ESCSEQ_POS) continue;
        if (used[i] && lookup[esc[i]] != (unsigned char)(i + 1)) return -1;
    }
    return 0;
}

static void seq_compress(rs_cbytep y, rs_size ylen, unsigned char d, rs_buf *o)
{
    rs_size i = 0;

    while (i < ylen) {
        int     bestl = 0, bestr = 0, bestsave = 0, L;
        rs_size lim = (ylen - i) / 2;
        if (lim > SEQ_MAX_LEN) lim = SEQ_MAX_LEN;
        for (L = 2; L <= (int)lim; ++L) {
            int R = 1, save;
            while (R < SEQ_MAX_REP && i + (rs_size)(R + 1) * L <= ylen) {
                int eq = 1, k;
                for (k = 0; k < L; ++k)
                    if (y[i + (rs_size)R * L + k] != y[i + k]) {
                        eq = 0;
                        break;
                    }
                if (!eq) break;
                ++R;
            }
            if (R < 2) continue;
            save = L * R - (L + 3);
            if (save > bestsave) {
                bestsave = save;
                bestl = L;
                bestr = R;
            }
        }
        /* never let a sequence run to the last byte of the stream */
        if (bestsave > 0 && i + (rs_size)bestl * bestr < ylen) {
            int k;
            rs_buf_push(o, d);
            for (k = 0; k < bestl; ++k) rs_buf_push(o, y[i + k]);
            rs_buf_push(o, d);
            rs_buf_push(o, (unsigned char)bestr);
            i += (rs_size)bestl * bestr;
        } else {
            rs_buf_push(o, y[i]);
            ++i;
        }
        if (o->err) return;
    }
}

int rs_rle_pick_escapes(rs_cbytep src, rs_size srclen,
                        unsigned char esc[RS_RLE_NESC], int *skipseq)
{
    static unsigned long freq[256];
    static unsigned char unused[256], inY[256];
    unsigned char        used[RS_RLE_NESC];
    rs_buf               y;
    rs_size              i;
    int                  v, nun = 0, k, rc = -1;

    for (v = 0; v < 256; ++v) freq[v] = 0;
    for (i = 0; i < srclen; ++i) freq[src[i]]++;

    /* Candidate pool: values that never occur, ascending, then values occurring
     * at most RS_RLE_MAXFREQ times, ascending. Data with ten or more unused
     * values never reaches the second group, which is why the sparse case looks
     * like a plain scan of the absent values. */
    for (v = 0; v < 256; ++v)
        if (!freq[v]) unused[nun++] = (unsigned char)v;
    for (v = 0; v < 256; ++v)
        if (freq[v] >= 1 && freq[v] <= RS_RLE_MAXFREQ)
            unused[nun++] = (unsigned char)v;

    if (nun < RS_RLE_NESC) return -1;

    for (k = 0; k < RS_RLE_NESC; ++k) esc[k] = unused[k];

    rs_buf_init(&y);
    single_compress(src, srclen, esc, used, &y);
    if (y.err) {
        rs_buf_free(&y);
        return -1;
    }

    for (v = 0; v < 256; ++v) inY[v] = 0;
    for (i = 0; i < y.len; ++i) inY[y.data[i]] = 1;
    rs_buf_free(&y);

    /* Dense data can leave no pool entry free of the output at all - every
     * candidate turns up as a run-count byte. There is then no byte that can
     * safely delimit a sequence, so the sequence pass cannot run: that, and not
     * any size comparison, is what the skip bit records. Slot 1 keeps the
     * pool's second entry, unused by anything in that case. */
    rc = 0;
    if (skipseq) *skipseq = 1;
    for (k = 1; k < nun; ++k) {
        if (!inY[unused[k]]) {
            esc[1] = unused[k];
            if (skipseq) *skipseq = 0;
            break;
        }
    }
    return rc;
}

int rs_rle_encode(rs_cbytep src, rs_size srclen,
                  const unsigned char esc[RS_RLE_NESC], int skipseq,
                  rs_buf *out)
{
    rs_buf        y, body;
    unsigned char used[RS_RLE_NESC];
    int           i, rc = -1;

    rs_buf_init(&y);
    rs_buf_init(&body);

    if (srclen > RS_SIZE24_MAX) return -1; /* the header cannot state it */

    /* RLE can expand slightly on hostile input, so ask for the input length
     * plus headroom rather than exactly the input length. */
    rs_buf_reserve(&y, srclen + srclen / 8 + 64);
    rs_buf_reserve(&body, srclen + srclen / 8 + 64);

    single_compress(src, srclen, esc, used, &y);
    if (y.err) goto done;

    if (markers_survive_lookup(esc, used)) {
        fputs("skidpack: an escape byte is repeated in a later slot that "
              "shadows a marker this data needs; choose another table\n",
              stderr);
        goto done;
    }

    /* The delimiter must not occur in what the sequence pass is about to read.
     * A bare occurrence is indistinguishable from the start of a token, so the
     * result would not decode. Derivation guarantees this; a table supplied
     * with -esc does not, and used to reach seq_compress unchecked. */
    if (!skipseq && contains_byte(y.data, y.len, esc[RS_RLE_ESCSEQ_POS])) {
        fprintf(stderr,
                "skidpack: escape slot 1 (%02X) occurs in the "
                "single-byte stream, so it cannot delimit sequences; "
                "use -noseq or choose another byte\n",
                esc[RS_RLE_ESCSEQ_POS]);
        goto done;
    }

    if (skipseq)
        rs_buf_append(&body, y.data, y.len);
    else
        seq_compress(y.data, y.len, esc[RS_RLE_ESCSEQ_POS], &body);
    if (body.err) goto done;
    if (body.len > RS_SIZE24_MAX) goto done;

    /* y is dead once body exists, and out can be sized from body rather than
     * guessed from the input. Three buffers of input size plus the caller's
     * copy is more than a 640 KB machine has for the largest resources; two is
     * not. Freeing here is what lets SDTITL.PVS pack on DOS at all. */
    rs_buf_free(&y);
    rs_buf_reserve(out, body.len + RS_RLE_NESC + 16);

    rs_buf_push(out, RS_TYPE_RLE);
    rs_put24(out, srclen);
    rs_put24(out, body.len);
    rs_buf_push(out, 0); /* unk, always 0 */
    rs_buf_push(out,
                (unsigned char)(skipseq ? (RS_RLE_NESC | 0x80) : RS_RLE_NESC));
    for (i = 0; i < RS_RLE_NESC; ++i) rs_buf_push(out, esc[i]);
    rs_buf_append(out, body.data, body.len);
    rc = out->err ? -1 : 0;

done:
    rs_buf_free(&y);
    rs_buf_free(&body);
    return rc;
}

/* ------------------------------------------------------------- container -- */

/* One attempt at the file with the dialect already decided.
 *
 * `total` is how many passes the file declares and `run` how many to actually
 * perform; they differ only when the caller asked to stop early. Every VLE pass
 * uses `order`: the dialect belongs to whoever wrote the file, so a reading
 * that switches dialect halfway through is not one worth accepting.
 *
 * want_len, when non-zero, is the size the wrapper promises the last pass
 * produces. Returns 0 and hands the result to *out, or -1 having touched
 * nothing.
 */
static int decomp_chain(rs_cbytep src, rs_size srclen, int sp, int total,
                        int run, int order, rs_size want_len, rs_buf *out)
{
    rs_buf    cur, next;
    rs_cbytep cp;
    rs_size   cl;
    int       p, rc = -1;

    rs_buf_init(&cur);
    rs_buf_init(&next);

    /* The first pass reads the caller's buffer where it lies. Copying it in
     * order to step over a four-byte header cost a second allocation the size
     * of the whole input, which on a 16-bit host costs more memory than the
     * largest resource leaves free. */
    cp = src + (rs_size)sp;
    cl = srclen - (rs_size)sp;

    for (p = 0; p < run; ++p) {
        if (cl == 0) goto done;

        /* Freed before the pass, not after it. A failed decode leaves bytes
         * behind - it writes symbols until it notices it has been reading past
         * the end - so a buffer with something in it proves nothing. */
        rs_buf_free(&next);
        rs_buf_init(&next);

        switch (cp[0]) {
        case RS_TYPE_RLE:
            if (rs_rle_decode(cp, cl, &next)) goto done;
            break;

        case RS_TYPE_VLE:
            if (rs_vle_decode(cp, cl, order, &next)) goto done;
            break;

        default: /* not a compression type we know */
            goto done;
        }

        /* A cheap reject before the next pass reserves from a header nobody
         * has vouched for yet. What proves the dialect is the run reaching the
         * end; this only stops a wrong guess from asking for 16 MB on its way
         * to failing anyway. */
        if (p + 1 < total && !next_pass_plausible(next.data, next.len))
            goto done;

        rs_buf_free(&cur);
        cur = next;
        rs_buf_init(&next);
        cp = cur.data;
        cl = cur.len;
    }

    if (want_len && cur.len != want_len) goto done;

    /* Hand the last stage to the caller rather than copying it. Both callers
     * pass an empty buffer, and copying would hold two full-size results at
     * once to no purpose. */
    rs_buf_free(out);
    *out = cur;
    rs_buf_init(&cur);
    rc = 0;

done:
    rs_buf_free(&cur);
    rs_buf_free(&next);
    return rc;
}

int rs_decomp(rs_cbytep src, rs_size srclen, int stop_after, int bitorder,
              rs_buf *out)
{
    rs_size final_len = 0, want_len;
    int     total_passes, passes_to_run, sp, order, tries, wrapped = 0;

    if (srclen < 5) return -1;

    total_passes = src[0];
    sp = 0;
    if (total_passes & 0x80) {
        wrapped = 1;
        total_passes &= 0x7F;
        final_len = rs_size24(src + 1); /* what the wrapper promises */
        sp = 4;
    } else {
        total_passes = 1;
    }

    if (total_passes < 1 || total_passes > 4) return -1;

    /* How many passes the file has and how many the caller wants are different
     * questions. Collapsing them meant --stop 1 on a two-pass file made the
     * first stage look like the last one, which switched off the check that
     * picks the payload dialect - so the guess went untested exactly when the
     * caller asked to inspect it. */
    passes_to_run = total_passes;
    if (stop_after > 0 && stop_after < total_passes) passes_to_run = stop_after;

    /* The wrapper states the final size. Checking it costs nothing and catches
     * a damaged outer header that would otherwise return a plausible buffer of
     * the wrong length. Not checked when the caller stopped early on purpose,
     * since then the result is an intermediate stage, not the final output. */
    want_len = (wrapped && passes_to_run == total_passes) ? final_len : 0;

    /* Dialect selection.
     *
     * Nothing in the file records the payload bit order, so it is settled by
     * trying one and seeing whether the file survives it.
     *
     * Surviving used to mean the next pass header looked sane. That is a
     * filter, not a proof: a wrong guess yields arbitrary bytes, and arbitrary
     * bytes sometimes begin with a well formed header. Worse, the choice was
     * remade independently at every pass, so a file could be accepted under a
     * mixed reading no encoder would ever have produced, and a later pass
     * failing to decode condemned the whole file rather than casting doubt on
     * the guess that led there. Now one order is fixed for the file and has to
     * carry it to the end, every pass actually decoding.
     *
     * What this does NOT buy is a check on the last pass. A wrong dialect
     * produces exactly the length that pass's own header states - only the
     * contents are nonsense - so neither the wrapper's final size nor anything
     * else in the container disagrees with it. On a single-pass file there is
     * therefore still nothing to test and the caller's choice stands, which is
     * what -target is for. want_len catches a damaged wrapper, not a dialect.
     *
     * The cost is a second attempt when the first guess is wrong, and it is
     * small: a wrong guess almost always dies on the very next pass header,
     * before any real work. Peak memory is unchanged, since a failed attempt is
     * released in full before the next one starts.
     */
    order = bitorder;
    for (tries = 0; tries < 2; ++tries) {
        if (!decomp_chain(src, srclen, sp, total_passes, passes_to_run, order,
                          want_len, out))
            return 0;
        order = (order == RS_VLE_MSB) ? RS_VLE_LSB : RS_VLE_MSB;
    }
    return -1;
}
