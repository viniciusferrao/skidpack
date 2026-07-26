/* The SDTITL.PVS bug: what is measured, what is not, and how to reproduce it.
 *
 * ---------------------------------------------------------------------------
 * The measurement
 * ---------------------------------------------------------------------------
 *
 * Stunts 1.1 ships SDTITL.PVS with 96 bytes after the end of its compressed
 * stream. Those bytes are a verbatim copy of offsets 4768..4863 of the same
 * file.
 *
 * Every compressed file in Stunts 1.0, Stunts 1.1, 4D Sports Driving 1990 and
 * 4D Sports Driving 1991 was checked, 314 in all. It is the only one with
 * anything after its stream. The same file in the other three releases has
 * none.
 *
 * The bytes are inert. Decompression stops at the length the header declares,
 * so the file is correct in use and repeated decodes agree. They are not a
 * hidden message either: entropy 6.132 against the payload's 6.585, and all
 * 256 single-byte XOR keys give noise.
 *
 * ---------------------------------------------------------------------------
 * What caused it is not known
 * ---------------------------------------------------------------------------
 *
 * A write buffer flushed without being cleared would look exactly like this,
 * and the numbers are suggestive: a 5120-byte buffer whose final short block is
 * padded up to a 256-byte boundary predicts both the offset and the length
 * exactly.
 *
 *     stream length                     9888
 *     full blocks                       9888 / 5120 = 1, covering 5120 bytes
 *     bytes left in the last block      4768
 *     rounded up to 256                 4864
 *     padding                           4864 - 4768 = 96      <- the length
 *     buffer positions 4768..4863 still hold the previous
 *     block's bytes, which were file offsets 4768..4863       <- the offset
 *     total                             5120 + 4864 = 9984    <- the file size
 *
 * That is two parameters fitted to one observation, and it is wrong. The same
 * model pads whenever the last block is not already 256-aligned, which is about
 * one file in 256. The measured rate is one in 314, and three other copies of
 * this very file, whose lengths differ, are clean. A rule that fits one point
 * and mispredicts the rest is not a rule.
 *
 * So the constants below record one file. They are not a mechanism, they are
 * not derived, and nothing else should be built on them.
 */
#include <stdio.h>
#include "sdtitl.h"

/* Measured from Stunts 1.1's SDTITL.PVS. See above for why these are a record
 * rather than a formula. */
#define SK_SDTITL_OFF 4768ul
#define SK_SDTITL_LEN 96ul

int sk_sdtitl_apply(rs_buf *out)
{
    if (SK_SDTITL_OFF + SK_SDTITL_LEN > out->len) {
        fprintf(stderr,
                "skidpack: sdtitl copies bytes %lu..%lu of the output, "
                "but this output is only %lu bytes\n",
                SK_SDTITL_OFF, SK_SDTITL_OFF + SK_SDTITL_LEN - 1,
                (unsigned long)out->len);
        return -1;
    }

    /* Appended after the stream is complete, so it cannot influence the coding
     * that precedes it. rs_buf_append_self rather than rs_buf_append: the
     * source is inside the buffer being grown, and growth moves it. */
    return rs_buf_append_self(out, SK_SDTITL_OFF, SK_SDTITL_LEN);
}
