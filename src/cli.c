/* Argument parsing and help text. See cli.h for what the platform split is for.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "version.h"

/* Option table. Short letters exist only on the Unix side; DOS spells switches
 * out, as DOS tools do. `sdtitl` is long-only: it names one file, so a letter
 * would be a poor mnemonic and there is nothing to abbreviate.
 *
 * Each row carries an explicit id and the code switches on that. Dispatching on
 * the short letter instead only worked while exactly one option lacked one. */
enum sk_opt_id { OPT_TARGET, OPT_ESC, OPT_NOSEQ, OPT_STOP, OPT_SDTITL };

/* clang-format off */
static const struct {
    int         id;
    char        sh; /* short letter, 0 for long-only */
    const char *lng;
    int         takes_value;
} opts[] = {
    { OPT_TARGET, 't', "target",     1 },
    { OPT_ESC,    'e', "esc",        1 },
    { OPT_NOSEQ,  'n', "noseq",      0 },
    { OPT_STOP,   's', "stop",       1 },
    { OPT_SDTITL, 0,   "sdtitl",     0 }
};
/* clang-format on */
#define NOPTS (int)(sizeof(opts) / sizeof(opts[0]))

/* Name comparison, exact on Unix and case-insensitive on DOS.
 *
 * C89 has no case-insensitive comparison; strcmp is exact and that is all
 * the standard offers. Folding is only ever wanted on DOS,
 * where switch names are matched the way every other DOS tool matches them,
 * and every DOS compiler of the period ships stricmp in <string.h>. So each
 * platform already has the right function and there is nothing to hand-roll.
 *
 * This used to be a hand-rolled loop with a `fold` argument that every caller
 * set to SK_DOS_SWITCHES, so it was already decided at compile time.
 *
 * Watcom is the exception to "ships stricmp": it has the function under both
 * names but hides the unreserved one in strict ANSI mode, which is how this is
 * built. Same idea as RS_HUGE in skidpack.h. */
#if SK_DOS_SWITCHES
#    if defined(__WATCOMC__) || defined(_WIN32)
#        define sk_fold_cmp _stricmp
#    else
#        define sk_fold_cmp stricmp
#    endif
#    define sk_name_eq(a, b) (sk_fold_cmp((a), (b)) == 0)
#    define sk_char_eq(c, l) (sk_lower(c) == (l))
#else
#    define sk_name_eq(a, b) (strcmp((a), (b)) == 0)
#    define sk_char_eq(c, l) ((c) == (l))
#endif

#if SK_DOS_SWITCHES

/* One character, so stricmp would need a two-byte buffer to say the same
 * thing. ASCII only, deliberately: option letters are ASCII by construction
 * and tolower() answers to the locale. */
static char sk_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

#endif

/* Classify one argument.
 *
 * Unix accepts  -t VALUE   --target VALUE   --target=VALUE   -n   --noseq
 * DOS accepts   /T VALUE   /TARGET VALUE    /TARGET=VALUE    and the same with
 *               a leading dash, case-insensitively.
 *
 * Returns the index into opts[], -1 if the argument is not an option at all,
 * or -2 if it looks like one but names nothing. *inline_val points at a value
 * supplied with '=' , else NULL.
 */
static int opt_lookup(const char *a, const char **inline_val)
{
    static char name[32];
    const char *p = a, *eq;
    int         i, longform = 0;
    size_t      n;

    *inline_val = 0;

    if (p[0] == '-' && p[1] == '-' && p[2]) {
        p += 2;
        longform = 1;
    } else if (p[0] == '-' && p[1]) {
        p += 1;
    }
#if SK_DOS_SWITCHES
    else if (p[0] == '/' && p[1]) {
        p += 1;
    }
#endif
    else {
        return -1;
    }

    eq = strchr(p, '=');
    n = eq ? (size_t)(eq - p) : strlen(p);
    if (n == 0 || n >= sizeof(name)) return -2;
    memcpy(name, p, n);
    name[n] = '\0';
    if (eq) *inline_val = eq + 1;

    /* a single character on the Unix short side, or anywhere on DOS */
    if (n == 1 && !longform)
        for (i = 0; i < NOPTS; ++i)
            if (opts[i].sh && sk_char_eq(name[0], opts[i].sh)) return i;

    for (i = 0; i < NOPTS; ++i)
        if (sk_name_eq(name, opts[i].lng)) return i;

    return -2;
}

/* Stunts 1.0 ships both Huffman dialects: its game resources are LSB-first
 * while its own LOAD.EXE modules are already MSB-first like everything in 1.1.
 * So a target could not simply name the release, and for a long time the two
 * halves were spelled out and the caller had to know which it held.
 *
 * It does not have to. Which half a file belongs to follows from its extension,
 * and does so exactly: across the 77 compressed files Stunts 1.0 ships, CMN,
 * COD, DIF and HDR are loader modules without exception and P3S, PES, PRE and
 * PVS are resources without exception. No extension appears on both sides. So
 * bb10 asks the file rather than the user, and the two explicit spellings stay
 * for the manifest, which records what each shipped file actually needs rather
 * than what could be worked out about it.
 *
 * ms11 is the same dialect under the publisher who shipped it here: Broderbund
 * put out Stunts, Mindscape put out 4D Sports Driving, and both 4D releases
 * carry bb11 exactly. There is deliberately no ms10 to match. Nothing Mindscape
 * shipped uses the 1.0 dialect, so the name would describe no file that exists,
 * and anyone reaching for it on 4D Sports Driving 1990 would get the wrong bit
 * order on every resource in it.
 *
 * The list is not printed. Two targets fit on the /TARGET line that introduces
 * them, and a block repeating them cost four rows on a screen that has to hold
 * a banner as well. */
/* clang-format off */
static const struct {
    const char *name;
    int         order;
} targets[] = {
    { "bb11",     RS_VLE_MSB      },
    { "ms11",     RS_VLE_MSB      },
    { "bb10",     SK_ORDER_BY_EXT },
    { "bb10-ldr", RS_VLE_MSB      },
    { "bb10-res", RS_VLE_LSB      }
};
/* clang-format on */
#define NTARGETS (int)(sizeof(targets) / sizeof(targets[0]))

/* The four extensions LOAD.EXE reads. Everything else the format carries is a
 * game resource, and so is anything unrecognised: a caller who asked for bb10
 * is working on 1.0 data, and 65 of its 77 compressed files are resources. */
int sk_order_for(const char *path, int order)
{
    static const char *ldr[] = {"CMN", "COD", "DIF", "HDR"};
    const char        *p, *dot = 0;
    int                i, k;

    if (order != SK_ORDER_BY_EXT) return order;

    for (p = path; p && *p; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':')
            dot = 0;
        else if (*p == '.')
            dot = p;
    }
    if (!dot) return RS_VLE_LSB;

    for (i = 0; i < 4; ++i) {
        for (k = 0; k < 3; ++k) {
            char c = dot[1 + k];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (c != ldr[i][k]) break;
        }
        if (k == 3 && dot[4] == '\0') return RS_VLE_MSB;
    }
    return RS_VLE_LSB;
}

/* Help goes to stdout so it can be piped or paged; a diagnostic goes to stderr.
 * Hence the stream argument rather than a hardcoded one. */
const char *sk_progname(const char *argv0)
{
    const char *p, *base;

    /* DOS has handed over a full path since 3.0, so this is usually
     * C:\SOMEWHERE\SKIDPACK.EXE and only the tail is worth printing. */
    if (!argv0 || !*argv0) return "skidpack";

    base = argv0;
    for (p = argv0; *p; ++p)
        if (*p == '/' || *p == '\\' || *p == ':') base = p + 1;

    /* Written out rather than as a conditional. Microsoft C 5.10 takes the
     * literal as char * and base as const char *, and warns that the two arms
     * indirect to different types. */
    if (!*base) return "skidpack";

    return base;
}

void sk_usage(FILE *out, const char *prog, int advanced)
{
    /* Both screens fit an 80x25 DOS screen with the banner above them, the
     * command that asked, and the prompt that follows. One screen carrying
     * everything did not, which is what the split is for rather than any
     * judgement about who deserves to see what.
     *
     * Split into several calls deliberately: C90 only guarantees 509 characters
     * in a string literal and a compiler of the era may hold you to it. */
    fputs(SK_BANNER "\n", out);

    if (!advanced) {
        /* Two usage lines because the modes below do not agree on their
         * arguments: p and u take a list and expand wildcards themselves, and
         * v takes one container. One line reading MODE FILE... promised the
         * list to all three. */
        fprintf(out,
                "usage:\n"
                "  %s p|u FILE... [options]\n"
                "  %s v FILE [options]\n\n",
                prog, prog);

        fputs("modes:\n"
              "  p   pack, write the packed twin of each file given\n"
              "  u   unpack, write the plain twin of each file given\n"
              "  v   verify one container, and say what it costs to load\n\n",
              out);

#if SK_DOS_SWITCHES
        fputs("options:\n"
              "  /TARGET T  bb11 for Stunts 1.1 and 4D (default), bb10 "
              "for 1.0\n"
              "  /V         print the version and stop\n\n",
              out);
#else
        fputs("options:\n"
              "  -t, --target T  bb11 for Stunts 1.1 and 4D (default), bb10 "
              "for 1.0\n"
              "  -v, --version   print the version and stop\n\n",
              out);
#endif
        fprintf(out,
                "advanced options:\n"
                "  %s %s\n",
                prog, SK_HELP_ADV_FLAG);
        return;
    }

    fprintf(out,
            "usage:\n"
            "  %s MODE <in> <out> [options]\n\n",
            prog);

    /* The three compress modes name the three shapes the format takes, and the
     * one to reach for is c: every release stores 62 of its resources that way.
     * cv is the other shipped shape, carrying the loader modules and a handful
     * of resources. cr is in no shipped file at all and is the RLE stage on its
     * own, which is why it is last and why it is the one that has to be told
     * its escape bytes. */
    fputs("modes:\n"
          "  d   decompress\n"
          "  c   compress, RLE then Huffman in a container, as most files are\n"
          "  cv  compress, Huffman alone, as the loader modules are\n"
          "  cr  compress, RLE alone, in no shipped file; needs /ESC\n\n",
          out);

    /* /ESC said what it took and never what it was for. The RLE pass marks a
     * run with a byte the data does not otherwise use, and picks those ten
     * bytes by counting which values are rare. Passing them is for reproducing
     * a file whose packer chose differently, and for cr, which has no pass to
     * derive them from. */
#if SK_DOS_SWITCHES
    fputs("options:\n"
          "  /TARGET T    bb11 for Stunts 1.1 and 4D (default), bb10 for 1.0\n"
          "  /ESC HH,...  the ten RLE run markers, counted from the data\n"
          "  /NOSEQ       suppress the byte-sequence sub-pass\n"
          "  /STOP N      halt decompression after N passes\n"
          "  /SDTITL      reproduce SDTITL.PVS's trailing bytes\n",
          out);
#else
    fputs("options:\n"
          "  -t, --target T     bb11 for Stunts 1.1 and 4D (default), bb10 "
          "for 1.0\n"
          "  -e, --esc HH,...   the ten RLE run markers, counted from the "
          "data\n"
          "  -n, --noseq        suppress the byte-sequence sub-pass\n"
          "  -s, --stop N       halt decompression after N passes\n"
          "      --sdtitl       reproduce SDTITL.PVS's trailing bytes\n",
          out);
#endif
}

/* Asking for help is not an error, so it is answered before the arguments are
 * parsed: it works with no mode, with a mode, and after a typo. Unix spells it
 * -h or --help, DOS /? as it has since 2.0. */
int sk_wants_help(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        /* The second screen is /ADV rather than the /?? it reads like, because
         * a question mark is a wildcard and both builds that would meet one
         * expand it before main() is entered. The DOS build links SETARGV.OBJ
         * so that `p *.VSH` works, and MinGW's runtime globs argv by default:
         * /?? arrived here as /VM, the two-letter directory it matched at the
         * root of the drive. /? survives only by matching nothing. */
        if (a[0] == '-' && a[1] == '-' && sk_name_eq(a + 2, "help-advanced"))
            return 2;
        if (a[0] == '-' && a[1] == '-' && sk_name_eq(a + 2, "help")) return 1;
        if (a[0] == '-' && a[1] == 'h' && !a[2]) return 1;
#if SK_DOS_SWITCHES
        if ((a[0] == '/' || a[0] == '-') && sk_name_eq(a + 1, "adv")) return 2;
        if ((a[0] == '/' || a[0] == '-') && sk_name_eq(a + 1, "advanced"))
            return 2;
        if (a[0] == '/' && sk_name_eq(a + 1, "help-advanced")) return 2;
        if ((a[0] == '/' || a[0] == '-') && a[1] == '?' && !a[2]) return 1;
        if (a[0] == '/' && sk_name_eq(a + 1, "h")) return 1;
        if (a[0] == '/' && sk_name_eq(a + 1, "help")) return 1;
#endif
    }
    return 0;
}

/* -v is the short spelling everywhere it is not already taken, and here it is
 * not: the codec's verify is a mode word rather than a switch, so `skidpack
 * verify` and `skidpack -v` never meet. On DOS /V is the spelling a tool of the
 * period would answer to. */
int sk_wants_version(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1] == '-' && sk_name_eq(a + 2, "version"))
            return 1;
        if (a[0] == '-' && a[1] == 'v' && !a[2]) return 1;
#if SK_DOS_SWITCHES
        if (a[0] == '/' && sk_name_eq(a + 1, "v")) return 1;
        if (a[0] == '/' && sk_name_eq(a + 1, "version")) return 1;
#endif
    }
    return 0;
}

static int parse_esc(const char *s, unsigned char esc[RS_RLE_NESC])
{
    int n = 0;
    for (; n < RS_RLE_NESC && *s; ++n) {
        char         *end;
        unsigned long v = strtoul(s, &end, 16);
        if (end == s || v > 255) return -1;
        esc[n] = (unsigned char)v;
        s = end;
        while (*s == ',' || *s == ' ') ++s;
    }
    return (n == RS_RLE_NESC && *s == '\0') ? 0 : -1;
}

/* Modes. C cannot switch on a string, so the name is resolved to an id once
 * here and everything downstream switches on that. The table also owns whether
 * a mode consumes argv[3], which used to be a second place that knew "verify"
 * was the odd one out, and whether it takes a file list instead of an in/out
 * pair. No mode does both, but the two columns stay separate because they
 * answer different questions and a third mode may yet split them. */
/* clang-format off */
static const struct {
    const char *name;
    int         id;
    int         wants_output;
    int         takes_list;
} modes[] = {
    { "p",      SK_MODE_PACK,   0, 1 },
    { "u",      SK_MODE_UNPACK, 0, 1 },
    { "d",      SK_MODE_D,      1, 0 },
    { "c",      SK_MODE_C,      1, 0 },
    { "cv",     SK_MODE_CV,     1, 0 },
    { "cr",     SK_MODE_CR,     1, 0 },
    { "v",      SK_MODE_VERIFY, 0, 0 },
    /* The words these letters stand for, still accepted and not listed. They
     * were the names until the set was made consistent, and refusing them now
     * would only cost somebody a run to discover it. */
    { "verify", SK_MODE_VERIFY, 0, 0 },
    { "pack",   SK_MODE_PACK,   0, 1 },
    { "unpack", SK_MODE_UNPACK, 0, 1 }
};
/* clang-format on */
#define NMODES (int)(sizeof(modes) / sizeof(modes[0]))

int sk_parse_args(int argc, char **argv, options *o)
{
    int i, m, found = 0, nlist = 0;

    o->in = o->out = 0;
    o->list = 0;
    o->nlist = 0;
    o->mode = SK_MODE_NONE;
    o->have_esc = o->noseq = o->stop = o->sdtitl_bug = 0;
    /* targets[0] is the default and the two must not drift apart, so the
     * bit order is taken from the same row that names it. */
    o->bitorder = targets[0].order;
    o->target = targets[0].name;
    o->have_target = 0;

    if (argc < 3) return -1;

    for (m = 0; m < NMODES; ++m) {
        if (sk_name_eq(argv[1], modes[m].name)) {
            o->mode = modes[m].id;
            found = m;
            break;
        }
    }
    if (o->mode == SK_MODE_NONE) {
        fprintf(stderr, "skidpack: unknown mode %s\n", argv[1]);
        return -1;
    }

    if (modes[found].takes_list) {
        i = 2;
    } else {
        o->in = argv[2];
        i = 3;
        if (modes[found].wants_output) {
            if (argc < 4) return -1;
            o->out = argv[3];
            i = 4;
        }
    }

    for (; i < argc; ++i) {
        const char *val;
        int         k = opt_lookup(argv[i], &val);

        if (k == -1) {
            /* Compacted in place. The write index never passes the read one,
             * since a file can only ever move towards the front of argv. */
            if (modes[found].takes_list) {
                argv[2 + nlist] = argv[i];
                ++nlist;
                continue;
            }
            fprintf(stderr, "skidpack: unexpected argument %s\n", argv[i]);
            return -1;
        }
        if (k == -2) {
            fprintf(stderr, "skidpack: unknown option %s\n", argv[i]);
            return -1;
        }

        if (!opts[k].takes_value) {
            if (val) {
                fprintf(stderr, "skidpack: %s%s takes no value\n", SK_OPT_LEAD,
                        opts[k].lng);
                return -1;
            }
            if (opts[k].id == OPT_NOSEQ)
                o->noseq = 1;
            else
                o->sdtitl_bug = 1;
            continue;
        }

        /* value is either glued on with '=' or the next argument */
        if (!val) {
            if (i + 1 >= argc) {
                fprintf(stderr, "skidpack: %s%s needs a value\n", SK_OPT_LEAD,
                        opts[k].lng);
                return -1;
            }
            val = argv[++i];
        }

        switch (opts[k].id) {
        case OPT_TARGET: {
            int t, known = 0;
            for (t = 0; t < NTARGETS; ++t)
                if (sk_name_eq(val, targets[t].name)) {
                    o->bitorder = targets[t].order;
                    o->target = targets[t].name;
                    o->have_target = 1;
                    known = 1;
                    break;
                }
            if (!known) {
                fprintf(stderr, "skidpack: unknown target %s\n", val);
                return -1;
            }
            break;
        }
        case OPT_ESC:
            if (parse_esc(val, o->esc)) {
                fprintf(stderr,
                        "skidpack: %sesc wants ten hex bytes, "
                        "e.g. 07,37,1D,...\n",
                        SK_OPT_LEAD);
                return -1;
            }
            o->have_esc = 1;
            break;
        default: { /* OPT_STOP */
            /* atoi has no way to report failure, so -stop banana and
             * -stop -3 both quietly became 0, which means "do not
             * stop" - the opposite of what was asked for. */
            char *end;
            long  v = strtol(val, &end, 10);
            if (end == val || *end != '\0' || v < 1 || v > 4) {
                fprintf(stderr,
                        "skidpack: %sstop wants a pass count "
                        "from 1 to 4\n",
                        SK_OPT_LEAD);
                return -1;
            }
            o->stop = (int)v;
            break;
        }
        }
    }

    if (modes[found].takes_list) {
        if (nlist == 0) {
            fprintf(stderr, "skidpack: %s needs at least one file\n",
                    modes[found].name);
            return -1;
        }
        o->list = argv + 2;
        o->nlist = nlist;
    }
    return 0;
}
