/* The SDTITL.PVS bug.
 *
 * One file in the four commercial releases carries bytes past the end of its
 * compressed stream. Reproducing it is not something any rule derives, so it
 * lives here on its own rather than as a special case inside the packer.
 *
 * sdtitl.c has the measurements and what they do and do not support.
 */
#ifndef SKIDPACK_SDTITL_H
#define SKIDPACK_SDTITL_H

#include "skidpack.h"

/* Append the trailing bytes to a finished compressed stream, in place.
 * Returns 0 on success, non-zero if `out` is too short to have come from the
 * file this reproduces, after printing why. */
int sk_sdtitl_apply(rs_buf *out);

#endif
