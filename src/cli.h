/* Command line handling for skidpack: option syntax, help, and the parsed
 * result the modes in main.c read. Kept apart from main.c because the platform
 * differences below are fiddly enough to be worth reading on their own.
 */
#ifndef SKIDPACK_CLI_H
#define SKIDPACK_CLI_H

#include <stdio.h>
#include "skidpack.h"

/* --- option syntax --------------------------------------------------------
 * DOS and Windows take /switch, Unix takes -switch, and this is one of the few
 * places where differing by platform is right rather than lazy: on Unix "/x" is
 * an absolute path, so accepting it as an option would make
 *
 *     skidpack d /tmp/ALPINE.PVS out.raw
 *
 * ambiguous. DOS has no such clash - its paths use backslashes - so a DOS build
 * accepts both spellings, and matches switch names case-insensitively the way
 * every other DOS tool does.
 *
 * Windows inherited the convention along with the path separator, and its own
 * tools still answer to /?, so a Win32 build belongs on this side rather than
 * with Unix. The dash spellings keep working everywhere, so nothing that drove
 * this tool with -target has to change.
 */
#if defined(__MSDOS__) || defined(MSDOS) || defined(_MSDOS) || \
    defined(__TURBOC__) || defined(_WIN32)
#    define SK_DOS_SWITCHES 1
#    define SK_OPT_LEAD "/"   /* how a long option is spelled in messages */
#    define SK_HELP_FLAG "/?" /* DOS has meant this since 2.0 */
#    define SK_HELP_ADV_FLAG "/ADV"
#    define SK_VERSION_FLAG "/V"
#else
#    define SK_DOS_SWITCHES 0
#    define SK_OPT_LEAD "--"
#    define SK_HELP_FLAG "--help"
#    define SK_HELP_ADV_FLAG "--help-advanced"
#    define SK_VERSION_FLAG "-v|--version"
#endif

/* What `bb10` leaves undecided until a file name is in hand. */
#define SK_ORDER_BY_EXT (-1)

/* What the tool was asked to do. Resolved from argv[1] once, because C cannot
 * switch on a string and a chain of strcmp is not a dispatch. */
enum sk_mode_id {
    SK_MODE_NONE = 0,
    SK_MODE_D,      /* decompress                        */
    SK_MODE_CV,     /* compress, Huffman only            */
    SK_MODE_CR,     /* compress, RLE only                */
    SK_MODE_C,      /* compress, RLE then Huffman        */
    SK_MODE_VERIFY, /* integrity check, no output file   */
    SK_MODE_PACK,   /* bulk: plain mod resources -> packed */
    SK_MODE_UNPACK  /* bulk: packed mod resources -> plain */
};

/* Everything the command line can carry, so the modes read as one step.
 *
 * `list` and `nlist` serve the modes that take a set of files rather than an
 * in/out pair. They point into argv, which outlives every use of them, so
 * nothing here owns memory. */
typedef struct {
    int           mode; /* enum sk_mode_id */
    const char   *in, *out;
    char        **list;
    int           nlist;
    unsigned char esc[RS_RLE_NESC];
    int           have_esc, noseq, stop, bitorder;
    int           sdtitl_bug;
    /* The dialect by name, and whether it was asked for or fallen back on.
     * bitorder above is what the codec reads; these two are for saying which
     * one a run used, which matters most when nobody chose it. */
    const char *target;
    int         have_target;
} options;

/* Fills *o from argv. Returns 0 on success, non-zero after printing what was
 * wrong; the caller prints the usage text. */
int sk_parse_args(int argc, char **argv, options *o);

/* Help to `out`. Callers use stdout when it was asked for and stderr when it
 * follows a mistake. `prog` is what to call the program in the usage lines.
 *
 * Two screens. The first carries what somebody with a mod needs: pack, unpack,
 * verify, and which game to target. The second carries the single-file codec,
 * where a caller picks a compression shape and the bytes that drive it, and is
 * only reached by asking. Splitting them is what keeps either inside an 80x25
 * screen now that a banner sits above both. */
void sk_usage(FILE *out, const char *prog, int advanced);

/* The file name the program was invoked as, with any directory stripped.
 * Messages then name the file in front of the reader rather than the name this
 * was written under, which are not the same thing once a copy is renamed. */
const char *sk_progname(const char *argv0);

/* Resolve a bit order that a target left open, from the extension of the file
 * it applies to. Anything already decided is returned untouched, so callers can
 * pass every order through this without asking which kind they hold. */
int sk_order_for(const char *path, int order);

/* Which help the arguments ask for: 0 none, 1 the first screen, 2 the advanced
 * one. Tested before parsing so that it works with no mode, with a mode, or
 * after a typo. */
int sk_wants_help(int argc, char **argv);

/* Whether the arguments ask only for the version. Tested alongside the help,
 * and for the same reason: it has to answer before anything can be wrong with
 * the rest of the command line. */
int sk_wants_version(int argc, char **argv);

#endif
