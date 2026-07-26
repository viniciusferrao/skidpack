/* Huffman stage of the DSI resource container.
 *
 * The format is self-describing: the per-length symbol counts and the alphabet
 * are both stored, and the alphabet is canonical - sorted by (code length,
 * symbol value). So the code lengths determine the tree completely, and the
 * only unknown was how code lengths get assigned. Plain Huffman matches every
 * shipped file, which is evidence that it is equivalent to whatever produced
 * them, not proof that it is the same code.
 *
 * Layout, after the 4-byte pass header:
 *   1 byte   tree depth; bit 7 is the delta flag (unused in every shipped file)
 *   depth    number of symbols at each code length, 1..depth
 *   alphlen  the alphabet, in canonical order
 *   ...      the code stream
 *
 * Canonical codes, matching the esc1/esc2 tables the game builds on decode
 * (its decoder survives, in restunts fileio.c; only the packer does not):
 *   first[w] = (first[w-1] + count[w-1]) * 2
 *   index    = code - first[w] + (number of symbols shorter than w)
 */
#include <stdlib.h>
#include "skidpack.h"

/* Working depth for the unconstrained tree, before it is brought down to
 * RS_VLE_MAX_DEPTH. Only a bound on the intermediate, never written to a file:
 * 256 symbols cannot exceed it unless frequencies span a Fibonacci run far
 * longer than any resource here. */
#define RS_HUFF_WORK_DEPTH 64

/* Per-byte bit reversal, built once. The 8086 has no instruction for this,
 * which is the likely reason the format changed instead. */
static unsigned char rs_revtab[256];
static int           rs_revtab_ready = 0;

static void rs_build_revtab(void)
{
    int i, b;
    if (rs_revtab_ready) return;
    for (i = 0; i < 256; ++i) {
        unsigned char v = 0;
        for (b = 0; b < 8; ++b)
            if (i & (1 << b)) v |= (unsigned char)(0x80 >> b);
        rs_revtab[i] = v;
    }
    rs_revtab_ready = 1;
}

/* --------------------------------------------------------- tree header -- */

int rs_vle_read_tree(rs_cbytep src, rs_size srclen, unsigned char *len,
                     rs_size *codepos)
{
    rs_size  p;
    unsigned esclen, alphlen, w, i;

    if (srclen < 6) return -1;
    esclen = src[4] & 0x7F;
    if (esclen == 0 || esclen > RS_VLE_MAX_DEPTH) return -1;
    if ((rs_size)(5 + esclen) > srclen) return -1;

    alphlen = 0;
    for (i = 0; i < esclen; ++i) alphlen += src[5 + i];
    if (alphlen == 0 || alphlen > RS_ALPH_LEN) return -1;
    if ((rs_size)(5 + esclen + alphlen) > srclen) return -1;

    /* A canonical code of depth w has 2^w slots, and every symbol placed at a
     * shorter length removes the subtree beneath it. Declaring more symbols at
     * some length than the remaining prefix space allows describes a tree that
     * cannot exist, and the decoder would otherwise build one whose codes
     * overlap. Fewer is fine: the tree is merely incomplete, which is what the
     * flat-histogram repair in the encoder deliberately produces. */
    {
        unsigned long slots = 1;
        for (w = 1; w <= esclen; ++w) {
            slots *= 2;
            if ((unsigned long)src[4 + w] > slots) return -1;
            slots -= src[4 + w];
        }
    }

    for (i = 0; i < RS_ALPH_LEN; ++i) len[i] = 0;

    p = (rs_size)(5 + esclen);
    for (w = 1; w <= esclen; ++w) {
        for (i = 0; i < src[4 + w]; ++i) {
            unsigned char sym = src[p++];

            /* Each symbol appears once. A repeat would overwrite its earlier
             * code length, leaving the stored alphabet and the rebuilt one
             * describing different trees while still decoding to the declared
             * length. */
            if (len[sym]) return -1;
            len[sym] = (unsigned char)w;
        }
    }

    if (codepos) *codepos = p;
    return 0;
}

/* ------------------------------------------------------------ bit reader -- */

typedef struct {
    rs_cbytep p;
    rs_size   len, pos;
    unsigned  buf, cnt;
    int       lsb;
    int       overrun; /* a bit was taken from past the end of the data */
} bitreader;

static void br_init(bitreader *br, rs_cbytep p, rs_size len, rs_size start,
                    int lsb)
{
    br->p = p;
    br->len = len;
    br->pos = start;
    br->buf = 0;
    br->cnt = 0;
    br->lsb = lsb;
    br->overrun = 0;
    if (lsb) rs_build_revtab();
}

/* Next logical bit. The two dialects differ only in where the first logical bit
 * sits inside a byte, so reversing the byte on the way in and then always
 * reading top-down covers both. Reads past the end yield zeroes: the encoder
 * emits a flush byte, but a code can still finish inside it. */
static unsigned br_bit(bitreader *br)
{
    if (br->cnt == 0) {
        unsigned char b = 0;
        if (br->pos < br->len)
            b = br->p[br->pos];
        else
            br->overrun = 1;
        ++br->pos;
        br->buf = br->lsb ? rs_revtab[b] : b;
        br->cnt = 8;
    }
    --br->cnt;
    return (br->buf >> br->cnt) & 1u;
}

/* ---------------------------------------------------------------- decode --
 *
 * Canonical Huffman, decoded straight from the stored tree:
 *
 *   first[w] = (first[w-1] + count[w-1]) * 2     lowest code of length w
 *   base[w]  = symbols shorter than w            its index in the alphabet
 *
 * Accumulate bits into `code`; at each length w the code belongs to that length
 * exactly when code - first[w] < count[w], and the symbol is
 * alph[base[w] + code - first[w]].
 *
 * The game's own decoder adds a 256-entry prefix table so codes of eight bits
 * or fewer resolve in one lookup - worth it on an 8086, irrelevant here, and it
 * is only a speed optimisation over this.
 */
int rs_vle_decode(rs_cbytep src, rs_size srclen, int bitorder, rs_buf *out)
{
    static unsigned char len[RS_ALPH_LEN], alph[RS_ALPH_LEN];
    static unsigned      count[RS_VLE_MAX_DEPTH + 2];
    static unsigned first[RS_VLE_MAX_DEPTH + 2], base[RS_VLE_MAX_DEPTH + 2];
    bitreader       br;
    rs_size         outlen, codepos, produced;
    unsigned        depth, w, s, idx, code;
    unsigned char   cur = 0;
    int             additive;

    if (srclen < 6 || src[0] != RS_TYPE_VLE) return -1;
    outlen = rs_size24(src + 1);
    rs_buf_reserve(out, outlen); /* the header states the size; use it */
    depth = src[4] & 0x7F;
    additive = (src[4] & 0x80) ? 1 : 0;
    if (depth == 0 || depth > RS_VLE_MAX_DEPTH) return -1;
    if (rs_vle_read_tree(src, srclen, len, &codepos)) return -1;

    /* counts per length, and the alphabet in canonical order */
    for (w = 0; w <= RS_VLE_MAX_DEPTH + 1; ++w) count[w] = 0;
    for (s = 0; s < RS_ALPH_LEN; ++s)
        if (len[s]) count[len[s]]++;
    idx = 0;
    for (w = 1; w <= depth; ++w)
        for (s = 0; s < RS_ALPH_LEN; ++s)
            if ((unsigned)len[s] == w) alph[idx++] = (unsigned char)s;

    first[1] = 0;
    base[1] = 0;
    for (w = 2; w <= depth + 1; ++w) {
        first[w] = (first[w - 1] + count[w - 1]) * 2;
        base[w] = base[w - 1] + count[w - 1];
    }

    br_init(&br, src, srclen, codepos, bitorder == RS_VLE_LSB);

    code = 0;
    for (produced = 0; produced < outlen; ++produced) {
        code = 0;
        for (w = 1; w <= depth; ++w) {
            code = (code << 1) | br_bit(&br);
            if (count[w] && code >= first[w] && code - first[w] < count[w])
                break;
        }
        if (w > depth) return -1; /* no code of any length */
        s = alph[base[w] + (code - first[w])];

        /* the delta flag is defined by the format but unused in every shipped
         * file; supported so a file that sets it still decodes */
        cur = additive ? (unsigned char)(cur + s) : (unsigned char)s;
        rs_buf_push(out, cur);
    }

    /* Reaching the declared length on bits that were never in the file is not
     * a successful decode. */
    if (br.overrun) return -1;
    return out->err ? -1 : 0;
}

/* --------------------------------------------------------------- verify -- */

int rs_vle_inversions(rs_cbytep src, rs_size srclen, rs_cbytep payload,
                      rs_size paylen, long *inv, rs_size *expected)
{
    static unsigned long freq[RS_ALPH_LEN];
    static unsigned char len[RS_ALPH_LEN];
    unsigned long        bits = 0;
    rs_size              codepos, p;
    long                 n = 0;
    int                  j, k;

    if (srclen < 6 || src[0] != RS_TYPE_VLE) return -1;
    if (rs_vle_read_tree(src, srclen, len, &codepos)) return -1;

    for (j = 0; j < RS_ALPH_LEN; ++j) freq[j] = 0;
    for (p = 0; p < paylen; ++p) freq[payload[p]]++;

    for (j = 0; j < RS_ALPH_LEN; ++j) {
        if (!len[j]) continue;
        bits += freq[j] * len[j];
        for (k = 0; k < RS_ALPH_LEN; ++k) {
            if (!len[k]) continue;
            if (freq[j] > freq[k] && len[j] > len[k]) ++n;
        }
    }
    *inv = n;

    /* The encoder writes floor(bits/8) whole bytes and then exactly one more:
     * the padded remainder, or an explicit zero when the last code landed on a
     * boundary. So it is floor and not ceil - rounding up here counted the
     * final byte twice on any stream whose bit count is not a multiple of 8,
     * which understated trailing data by one byte and hid truncation. */
    if (expected) *expected = codepos + (rs_size)(bits / 8) + 1;
    return 0;
}

/* ---------------------------------------------------------------- encode -- */

typedef struct hnode {
    unsigned long freq;
    int           sym; /* -1 for an internal node */
    struct hnode *l, *r;
} hnode;

/* Plain Huffman. Ties take the lowest symbol first, and a new internal node is
 * inserted after every node of equal weight - the conventional ordering, and
 * the one whose code lengths match every shipped file. */
static int huff_lengths(const unsigned long *freq, unsigned char *len)
{
    static hnode  pool[RS_ALPH_LEN * 2];
    static hnode *live[RS_ALPH_LEN * 2];
    static hnode *stack[RS_ALPH_LEN * 2];
    static int    depth[RS_ALPH_LEN * 2];
    int           nlive = 0, npool = 0, i, k, sp;

    for (i = 0; i < RS_ALPH_LEN; ++i) len[i] = 0;

    for (i = 0; i < RS_ALPH_LEN; ++i) {
        if (!freq[i]) continue;
        pool[npool].freq = freq[i];
        pool[npool].sym = i;
        pool[npool].l = pool[npool].r = 0;
        live[nlive++] = &pool[npool++];
    }
    if (nlive == 0) return -1;
    if (nlive == 1) {
        len[live[0]->sym] = 1;
        return 0;
    }

    /* insertion sort by (freq, symbol) - stable and small */
    for (i = 1; i < nlive; ++i) {
        hnode *t = live[i];
        k = i - 1;
        while (k >= 0 &&
               (live[k]->freq > t->freq ||
                (live[k]->freq == t->freq && live[k]->sym > t->sym))) {
            live[k + 1] = live[k];
            --k;
        }
        live[k + 1] = t;
    }

    while (nlive > 1) {
        hnode *a = live[0], *b = live[1], *n;
        for (i = 2; i < nlive; ++i) live[i - 2] = live[i];
        nlive -= 2;

        n = &pool[npool++];
        n->freq = a->freq + b->freq;
        n->sym = -1;
        n->l = a;
        n->r = b;

        i = 0;
        while (i < nlive && live[i]->freq <= n->freq) ++i;
        for (k = nlive; k > i; --k) live[k] = live[k - 1];
        live[i] = n;
        ++nlive;
    }

    sp = 0;
    stack[sp] = live[0];
    depth[sp] = 0;
    ++sp;
    while (sp > 0) {
        hnode *cur;
        int    d;
        --sp;
        cur = stack[sp];
        d = depth[sp];
        if (!cur->l) {
            if (d > RS_HUFF_WORK_DEPTH) return -1;
            len[cur->sym] = (unsigned char)d;
        } else {
            stack[sp] = cur->l;
            depth[sp] = d + 1;
            ++sp;
            stack[sp] = cur->r;
            depth[sp] = d + 1;
            ++sp;
        }
    }
    return 0;
}

int rs_vle_encode(rs_cbytep src, rs_size srclen, int bitorder, rs_buf *out)
{
    static unsigned long freq[RS_ALPH_LEN];
    static unsigned char len[RS_ALPH_LEN], alph[RS_ALPH_LEN];
    static unsigned int  code[RS_ALPH_LEN], counts[RS_HUFF_WORK_DEPTH + 2];
    static int           order[RS_ALPH_LEN];
    unsigned int         maxw = 0, first, idx, acc, nbits, q;
    rs_size              i, payload_start;
    int                  s, w, k;

    if (srclen == 0) return -1;
    if (srclen > RS_SIZE24_MAX) return -1; /* the header cannot state it */

    /* Only a hint. On the game's data Huffman shrinks the input, so one
     * allocation replaces a dozen doublings. Incompressible input expands past
     * this and simply grows as before. */
    rs_buf_reserve(out, srclen);

    for (s = 0; s < RS_ALPH_LEN; ++s) freq[s] = 0;
    for (i = 0; i < srclen; ++i) freq[src[i]]++;
    if (huff_lengths(freq, len)) return -1;

    for (s = 0; s < RS_ALPH_LEN; ++s)
        if ((unsigned)len[s] > maxw) maxw = len[s];
    if (maxw == 0) return -1;

    for (w = 0; w <= RS_HUFF_WORK_DEPTH + 1; ++w) counts[w] = 0;
    for (s = 0; s < RS_ALPH_LEN; ++s)
        if (len[s]) counts[len[s]]++;

    /* Bring the tree within the depth the game can read.
     *
     * A plain Huffman tree is optimal but unbounded, and RS_VLE_MAX_DEPTH says
     * why that is not good enough here. SDOSEL.PVS is the resource that reaches
     * 16, so without this the tool either emits a file Stunts renders as
     * garbage or refuses the input outright.
     *
     * The adjustment is the one JPEG uses. Take two symbols from the level that
     * is too deep and promote one; to keep the code prefix-free, find the
     * deepest level j that still has room, and push one of its symbols down,
     * which frees exactly the two slots just consumed. Repeat until nothing
     * sits below the limit. The result is a valid prefix code, no longer
     * optimal, but the loss is a fraction of a percent on data that reaches
     * this at all.
     *
     * Lengths are then handed back out by frequency, most frequent first, which
     * is what makes the reassignment cost so little. Untouched when the tree
     * already fits, so the files this tool exists to reproduce never see it. */
    if (maxw > RS_VLE_MAX_DEPTH) {
        int j, n = 0;

        for (w = (int)maxw; w > RS_VLE_MAX_DEPTH; --w) {
            while (counts[w] > 0) {
                j = w - 2;
                while (counts[j] == 0) --j;
                counts[w] -= 2;
                counts[w - 1] += 1;
                counts[j + 1] += 2;
                counts[j] -= 1;
            }
        }

        /* Symbols by descending frequency, ties by value so the result does not
         * depend on anything but the input. */
        for (s = 0; s < RS_ALPH_LEN; ++s)
            if (len[s]) order[n++] = s;
        for (k = 1; k < n; ++k) {
            int v = order[k];
            j = k - 1;
            while (j >= 0 && freq[order[j]] < freq[v]) {
                order[j + 1] = order[j];
                --j;
            }
            order[j + 1] = v;
        }

        k = 0;
        for (w = 1; w <= RS_VLE_MAX_DEPTH; ++w)
            for (q = 0; q < counts[w]; ++q) len[order[k++]] = (unsigned char)w;

        maxw = 0;
        for (s = 0; s < RS_ALPH_LEN; ++s)
            if ((unsigned)len[s] > maxw) maxw = len[s];
    }
    if (maxw > RS_VLE_MAX_DEPTH) return -1;

    /* The tree header stores one byte per code length, so a length holding all
     * 256 symbols cannot be written: the count truncates to zero and the file
     * will not decode. Only a perfectly flat distribution gets there, which no
     * shipped resource has and which in practice means near-random input.
     *
     * Moving one symbol down a level costs a single bit and makes the tree
     * representable. The code stays prefix-free: Kraft falls from 256/256 to
     * 255/256 + 1/512. Nothing the game ships reaches this, so it cannot
     * disturb the files this tool exists to reproduce. */
    for (w = 1; w <= RS_VLE_MAX_DEPTH; ++w) {
        if (counts[w] <= 255) continue;
        if (w + 1 > RS_VLE_MAX_DEPTH) return -1;
        for (s = RS_ALPH_LEN - 1; s >= 0; --s)
            if ((int)len[s] == w) {
                len[s] = (unsigned char)(w + 1);
                break;
            }
        counts[w]--;
        counts[w + 1]++;
        if ((unsigned)(w + 1) > maxw) maxw = (unsigned)(w + 1);
    }

    /* alphabet in canonical order, and the codes that follow from it */
    idx = 0;
    first = 0;
    q = 0;
    for (w = 1; w <= (int)maxw; ++w) {
        for (s = 0; s < RS_ALPH_LEN; ++s)
            if ((int)len[s] == w) alph[idx++] = (unsigned char)s;
        for (k = 0; k < (int)counts[w]; ++k) code[alph[q + k]] = first + k;
        q += counts[w];
        first = (first + counts[w]) * 2;
    }

    rs_buf_push(out, RS_TYPE_VLE);
    rs_put24(out, srclen);
    rs_buf_push(out, (unsigned char)maxw);
    for (w = 1; w <= (int)maxw; ++w) rs_buf_push(out, (unsigned char)counts[w]);
    for (i = 0; i < idx; ++i) rs_buf_push(out, alph[i]);
    payload_start = out->len;

    acc = 0;
    nbits = 0;
    for (i = 0; i < srclen; ++i) {
        int          wl = len[src[i]];
        unsigned int c = code[src[i]];
        for (k = wl - 1; k >= 0; --k) {
            acc = (acc << 1) | ((c >> k) & 1u);
            if (++nbits == 8) {
                rs_buf_push(out, (unsigned char)(acc & 0xFF));
                acc = 0;
                nbits = 0;
            }
        }
    }

    /* Always flush a final byte, even when the last code lands on a byte
     * boundary: the decoder pre-reads two bytes and reads past the last code,
     * so the stream needs a byte of slack. MCGA.COD shows it. */
    if (nbits)
        rs_buf_push(out, (unsigned char)((acc << (8 - nbits)) & 0xFF));
    else
        rs_buf_push(out, 0);

    /* Payload only, including the padded final byte. Header, tree and alphabet
     * are untouched - reversing those would corrupt the file. */
    if (bitorder == RS_VLE_LSB && !out->err) {
        rs_build_revtab();
        for (i = payload_start; i < out->len; ++i)
            out->data[i] = rs_revtab[out->data[i]];
    }

    return out->err ? -1 : 0;
}
