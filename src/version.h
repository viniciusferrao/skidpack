/* Who this program is: its name, its version, and who to blame for it.
 *
 * Apart from the codec in skidpack.h and the option handling in cli.h, because
 * it is neither. The version governs the whole tool rather than the way it
 * parses a command line, and a release is the one line below and nothing else.
 */
#ifndef SKIDPACK_VERSION_H
#define SKIDPACK_VERSION_H

#define SK_VERSION "1.0"

#define SK_NAME "SKIDPACK"
#define SK_DESCRIPTION "(un)packer for the Stunts DOS game"
#define SK_YEAR "2026"
#define SK_DEV "Vinicius Ferrao <vinicius@ferrao.net.br>"

/* The banner, printed above the usage the way a DOS tool of the period
 * announced itself before saying anything else.
 *
 * ASCII, and the author's name loses its accents in it. Not an oversight: the
 * screen this is written for renders bytes through code page 437, where the
 * source file's UTF-8 would come out as box-drawing characters, and where an
 * a-tilde does not exist to be shown correctly at any encoding. 437 carries
 * a-acute, i-acute, o-acute, u-acute and n-tilde and stops there. Spelling it
 * in plain letters is the version that reads the same on a 1991 screen and a
 * modern terminal; LICENSE carries the accented spelling, being read where
 * UTF-8 works. */
#define SK_TAGLINE SK_NAME " " SK_VERSION ": " SK_DESCRIPTION "\n"
#define SK_COPYRIGHT "Copyleft (c) " SK_YEAR " " SK_DEV "\n"

#define SK_BANNER SK_TAGLINE SK_COPYRIGHT

#endif /* SKIDPACK_VERSION_H */
