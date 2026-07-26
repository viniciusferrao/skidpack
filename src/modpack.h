/* Which plain resource extensions have a packed twin, and when swapping one for
 * the other is safe. See modpack.c for what the game does with each pair.
 */
#ifndef SKIDPACK_MODPACK_H
#define SKIDPACK_MODPACK_H

#include "skidpack.h"

/* Long enough for a DOS path with room for the rewritten extension. */
#define SK_MODPACK_PATHMAX 260

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

/* The scratch buffer the packed loader needs to unflip this resource, in bytes:
 * its largest shape rounded up to a paragraph, which is what
 * file_get_unflip_size computes. 0 when the data is not a shape container, and
 * 0 for the pairs that unflip nothing.
 *
 * This is the memory a packed resource costs that a plain one does not, so it
 * is the number worth totalling before packing a mod into a game that has to
 * hold everything else as well. */
rs_size sk_modpack_scratch(rs_cbytep data, rs_size len);

#endif
