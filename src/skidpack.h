/* DSI resource container - codec for the compressed files Stunts ships.
 *
 * Container: byte 0 has the multi-pass flag in bit 7. If set, the low bits are
 * the pass count and the next three bytes are the final decompressed size;
 * otherwise byte 0 is the compression type of the single pass. Each pass then
 * carries its own 4-byte header of the same shape.
 *
 * Type 1 is run-length coding, type 2 is Huffman. Every compressed file the
 * game ships is Huffman on the outside; the two-pass ones are Huffman over RLE,
 * so on the way in the RLE stage runs first.
 *
 * Strict C89, no dependencies, and no assumption that a pointer can address
 * more than 64 KB - see below. It compiles as cleanly on a 1990 16-bit DOS
 * compiler as on a modern one.
 */
#ifndef SKIDPACK_H
#define SKIDPACK_H

#include <stddef.h>

/* --- 16-bit hosts ---------------------------------------------------------
 * EGA.CMN is 143 KB and SDOSEL.PVS decompresses past 109 KB, so buffers exceed
 * a 16-bit segment and every offset must be 32-bit. On a small-model 16-bit
 * compiler that means huge pointers, whose arithmetic normalises across
 * segment boundaries; everywhere else these vanish.
 *
 * rs_size is unsigned long rather than size_t for the same reason: size_t is
 * 16 bits on those hosts and would silently wrap at 64 KB.
 *
 * The test is for a 16-bit target, not for DOS. DOS also had 32-bit compilers,
 * where pointers are flat, there is nothing to normalise, and `huge` is not a
 * keyword. Microsoft C 5.10 predefines M_I86; Watcom 16-bit predefines both
 * spellings; Turbo C is named outright.
 */
#if defined(M_I86) || defined(_M_I86) || defined(__TURBOC__)
#    if defined(__WATCOMC__)
/* Watcom answers to both spellings, but only the reserved one survives its
 * strict ANSI mode, and everything here is built strict. */
#        define RS_HUGE __huge
#    else
#        define RS_HUGE huge
#    endif
#else
#    define RS_HUGE
#endif

typedef unsigned long                rs_size;
typedef unsigned char RS_HUGE       *rs_bytep;
typedef const unsigned char RS_HUGE *rs_cbytep;

/* Every length in this format is stored in 24 bits. Anything larger cannot be
 * written down, so it is refused before a header is emitted rather than
 * silently truncated into a file that will not read back. */
#define RS_SIZE24_MAX 0xFFFFFFul

/* Ceiling for one buffer, so growth arithmetic cannot wrap. Far above the
 * largest resource, which decompresses to about 144 KB. */
#define RS_SIZE_MAX 0x7FFFFFFFul

#define RS_TYPE_RLE 1
#define RS_TYPE_VLE 2

#define RS_VLE_MAX_WIDTH 16 /* escape/level table length  */

/* Longest Huffman code the GAME can read, which is 15 and not 16.
 *
 * Sixteen is the number you arrive at by counting table entries, and it is what
 * the Stunts Wiki states: "the maximum Huffman tree height is 16", because the
 * level offset table holds 16. The table is indeed 16 long, and index 15 is
 * code width 16, so nothing overruns. The limit is arithmetic, not indexing.
 *
 * file_decomp_vle builds that table by accumulating and doubling:
 *
 *     j = 0;  per level:  j += count[level];  esc2[level] = j;  j *= 2;
 *
 * esc2[level] is the exclusive upper bound, the first code value past the last
 * code of that width, and the decode ends when the code word falls under it.
 * For a COMPLETE tree of depth d that bound telescopes to exactly 2^d. It is
 * held in an unsigned short: at depth 15 it is 32768 and fits, at depth 16 it
 * is 65536 and wraps to zero. The comparison then never comes true, the decode
 * walks past where it should have stopped, and the output drifts by a byte.
 * The usual off-by-one, needing d+1 bits for the exclusive bound of a d-bit
 * range.
 *
 * Huffman trees are complete by construction, so 16 is unreachable for any real
 * encoder. An incomplete 16-level tree would stay under the bound and probably
 * decode, but nothing produces one, and this is not the place to rely on it.
 *
 * The shipped data agrees. Of the 314 Huffman passes in the four releases none
 * is deeper than 15 and 85 sit exactly at 15, which is what an enforced ceiling
 * looks like.
 *
 * Found by repacking a release and watching Stunts fail to draw the opponent
 * selection screen. SDOSEL.PVS is the one resource whose data pushes an
 * unconstrained tree to 16.
 */
#define RS_VLE_MAX_DEPTH 15
#define RS_ALPH_LEN 256
#define RS_VLE_ESC_WIDTH 0x40
#define RS_VLE_NUM_SYMB 0x80

#define RS_RLE_NESC 10      /* every shipped file uses exactly ten */
#define RS_RLE_ESCSEQ_POS 1 /* slot 1 is the byte-sequence delimiter */
#define RS_RLE_MAXFREQ 10   /* an escape byte may occur at most this often */

/* Huffman payload bit order.
 *
 * Two dialects exist, differing ONLY in where each logical bit sits inside a
 * payload byte. Everything else - header, lengths, tree depth, per-depth symbol
 * counts, alphabet, RLE, the multipass wrapper - is identical, and so is the
 * compressed length, because a per-byte bit reversal is a bijection.
 *
 *   RS_VLE_MSB  first logical bit in bit 7. Stunts 1.1 throughout, and also
 *               Stunts 1.0's LOAD.EXE modules (COD/HDR/CMN/DIF).
 *   RS_VLE_LSB  first logical bit in bit 0, i.e. every payload byte reversed.
 *               Stunts 1.0 game resources only (PVS/P3S/PRE/...).
 *
 * So the split is not by release: Stunts 1.0 ships both. The later decoder
 * indexes a 256-entry prefix table with the top eight bits of its input window,
 * which needs the next bits already in the high end of the byte - and the 8086
 * has no bit-reversal instruction. Changing the encoder was cheaper than
 * reversing every byte at load time, which is the likely reason for the change.
 */
#define RS_VLE_MSB 0
#define RS_VLE_LSB 1

/* --- growable byte buffer -------------------------------------------------
 * err is sticky: once a reallocation fails every further append is a no-op and
 * the flag stays set, so callers test it once at the end instead of at every
 * write. Checking each write is what invites the opposite bug - a silently
 * truncated result that still reports success.
 */
typedef struct {
    rs_bytep data;
    rs_size  len;
    rs_size  cap;
    int      err;
} rs_buf;

void rs_buf_init(rs_buf *b);
void rs_buf_free(rs_buf *b);
void rs_buf_reserve(rs_buf *b, rs_size want);
int  rs_buf_append_self(rs_buf *b, rs_size off, rs_size n);
void rs_buf_push(rs_buf *b, unsigned char v);
void rs_buf_append(rs_buf *b, rs_cbytep p, rs_size n);

/* Read a 24-bit little-endian size, and write one. */
rs_size rs_size24(rs_cbytep p);
int     rs_put24(rs_buf *b, rs_size v);

/* --- decoding -------------------------------------------------------------
 * Each returns 0 on success. stop_after > 0 halts rs_decomp once that many
 * passes have run, which yields the intermediate between stages.
 *
 * Nothing in the file records which dialect it uses, so rs_decomp takes a
 * starting guess and retries with the other order when a pass yields something
 * that cannot be right.
 *
 * rs_decomp_as does not retry: the dialect passed is the dialect used. A
 * caller that is measuring the dialects rather than merely reading the file
 * needs that, since a silent retry means the result it holds can have come
 * from the order it did not ask for.
 */
int rs_vle_decode(rs_cbytep src, rs_size srclen, int bitorder, rs_buf *out);
int rs_rle_decode(rs_cbytep src, rs_size srclen, rs_buf *out);
int rs_decomp(rs_cbytep src, rs_size srclen, int stop_after, int bitorder,
              rs_buf *out);
int rs_decomp_as(rs_cbytep src, rs_size srclen, int stop_after, int bitorder,
                 rs_buf *out);

/* Read a Huffman pass header: per-symbol code lengths from the stored
 * per-depth counts and alphabet, plus where the code stream starts. Shared by
 * the decoder and the integrity check so the format's fiddliest invariant is
 * expressed once.
 *
 * len must hold RS_ALPH_LEN entries; a symbol absent from the alphabet gets 0.
 */
int rs_vle_read_tree(rs_cbytep src, rs_size srclen, unsigned char *len,
                     rs_size *codepos);

/* Integrity check for a Huffman pass.
 *
 * Counts frequency/length inversions: pairs where a symbol occurring MORE often
 * in the payload carries a LONGER code. A tree is built from a histogram of the
 * data it encodes, so a correctly produced file has exactly zero. Any inversion
 * means the tree and the payload disagree - the file was damaged after it was
 * written, or the encoder counted different bytes than it compressed.
 *
 * This catches what "does it decompress" cannot. A file with two corrupt
 * sectors in its code stream still decodes without error, still yields exactly
 * its declared length, and still looks entirely plausible; only the tree
 * disagrees with the result.
 *
 * Also reports, via *expected, the size this pass ought to occupy: header plus
 * the coded bits rounded up plus the mandatory flush byte. Comparing that with
 * the actual length finds trailing data the decoder never reads - a different
 * fault from corruption, and one inversions cannot see because those bytes take
 * no part in the coding.
 */
int rs_vle_inversions(rs_cbytep src, rs_size srclen, rs_cbytep payload,
                      rs_size paylen, long *inv, rs_size *expected);

/* --- encoding ------------------------------------------------------------ */

/* Huffman. Canonical; the alphabet is emitted sorted by (code length, symbol
 * value), which is how every shipped file stores it. bitorder picks the payload
 * dialect and affects the payload only - never the header, tree or alphabet. */
int rs_vle_encode(rs_cbytep src, rs_size srclen, int bitorder, rs_buf *out);

/* RLE. esc[] is the ten-byte escape table; skipseq suppresses the byte-sequence
 * sub-pass. */
int rs_rle_encode(rs_cbytep src, rs_size srclen,
                  const unsigned char esc[RS_RLE_NESC], int skipseq,
                  rs_buf *out);

/* Derive an escape table that reproduces the shipped files. Whether the
 * original packer chose the same way is unknown; this is inferred from output.
 *
 * Candidate pool: byte values that never occur, ascending, then values
 * occurring at most RS_RLE_MAXFREQ times, ascending. Slots 0..9 take the first
 * ten of that pool in order. Data with ten or more unused values never reaches
 * the second group, which is why most files look like a plain scan of the
 * absent values and only dense ones spill into used bytes.
 *
 * Slot 1 is then overridden: it is the sequence delimiter, and the sequence
 * pass runs over the single-byte pass's OUTPUT rather than over the data. That
 * output carries run-count bytes, which can hold any value at all, including
 * ones the data never uses - so a delimiter that is free in the data can still
 * collide downstream. Slot 1 is therefore the first pool entry from index 1 up
 * that is also absent from the output. The scan needs no exclusion list: the
 * other slots' bytes appear in the output as escape codes, so they rule
 * themselves out.
 *
 * *skipseq is set when no candidate is free of that output: there is then no
 * byte that can safely delimit a sequence, so the pass cannot run. That, and
 * not any size comparison, is what the container's skip bit records.
 *
 * Returns 0 on success, -1 if the pool cannot fill ten slots.
 */
int rs_rle_pick_escapes(rs_cbytep src, rs_size srclen,
                        unsigned char esc[RS_RLE_NESC], int *skipseq);

#endif
