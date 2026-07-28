/* Which plain resource extensions have a packed twin, and when swapping one for
 * the other is safe. See modpack.c for what the game does with each pair.
 */
#ifndef SKIDPACK_MODPACK_H
#define SKIDPACK_MODPACK_H

#include "skidpack.h"

/* Long enough for a DOS path with room for the rewritten extension. */
#define SK_MODPACK_PATHMAX 260

/* Which unflip path the game takes for a packed name, because the two 2D forms
 * do not share one. `.PVS` sizes a scratch buffer from the widest shape in the
 * file; `.PES` asks for a flat 1000 paragraphs whatever the file holds. Both
 * release the buffer as soon as the shape is unflipped. `.P3S` unflips nothing.
 */
enum sk_unflip_kind {
    SK_UNFLIP_NONE = 0, /* .P3S: file_load_binary, no scratch          */
    SK_UNFLIP_PVS,      /* .PVS: file_get_unflip_size sizes the buffer */
    SK_UNFLIP_PES       /* .PES: mmgr_alloc_pages("UNFLIP", 1000)      */
};

/* What `.PES` asks for, in bytes: 1000 paragraphs of 16. Fixed, so a small
 * dashboard costs exactly what a large one does. */
#define SK_UNFLIP_PES_BYTES 16000UL

/* Map a plain resource path to the packed name the game looks for first.
 *
 * Fills `packed` and sets *unflips to whether that name's load path runs an
 * unflip pass, in which case the caller owes sk_modpack_flip_safe a look at the
 * data. Returns 1 when the extension has a twin and 0 when it has none, which
 * covers .RES and everything else a mod ships alongside its shapes. */
int sk_modpack_target(const char *path, char *packed, int *unflips);

/* The same pairing read the other way: a packed resource to the plain name the
 * game reads when no packed one is there. Returns 1 when the extension has a
 * twin, 0 when it has none. *unflips carries the same warning as above, and
 * matters more here: what comes out of a container is the flipped form, and
 * writing it under the plain extension hands the game data it will not unflip.
 */
int sk_modpack_source(const char *path, char *plain, int *unflips);

/* Whether the unflip pass would leave this shape resource alone.
 *
 * 1 when no shape claims a flip and the bytes therefore mean the same thing
 * under both loaders, 0 when one does, and -1 when the data is not a shape
 * container at all. */
int sk_modpack_flip_safe(rs_cbytep data, rs_size len);

/* The scratch buffer the packed loader needs to unflip this resource, in bytes,
 * for the `kind` its extension selects.
 *
 * SK_UNFLIP_PVS sizes it from the widest shape in the container, which is what
 * file_get_unflip_size computes, and returns 0 when the data does not parse as
 * one. SK_UNFLIP_PES ignores the data entirely and answers 16000, because the
 * loader asks for a flat 1000 paragraphs there. SK_UNFLIP_NONE is 0.
 *
 * This is memory a packed resource costs that a plain one does not, but it is
 * held only while the shape is unflipped and released immediately after. The
 * figure worth reporting across a run is therefore the largest single one, not
 * the sum: the game never holds two of these at once. */
rs_size sk_modpack_scratch(rs_cbytep data, rs_size len, int kind);

#endif
