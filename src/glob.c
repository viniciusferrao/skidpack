/* Wildcard expansion for the hosts whose C runtime does not do it.
 *
 * `skidpack p *.VSH` is how README.md tells a modder to pack a car, and on a
 * Unix shell that line never reaches the program with a star in it. DOS and
 * Windows hand the argument over untouched and expect the program to cope.
 * Microsoft C ships SETARGV.OBJ for exactly this and MSCBUILD links it, but the
 * released binaries are built with Open Watcom, whose equivalent module is not
 * in the 1.9 distribution and whose source needs a header that is not either.
 * Turbo C never had one. So all three released binaries read `*.VSH` as a file
 * name, fail to open it, and report one error where the user asked for a batch.
 *
 * Doing it here rather than at link time fixes every DOS compiler at once and
 * keeps the behaviour identical across them, which a per-compiler runtime
 * module would not.
 *
 * Only the modes that take a list are expanded. The single-file modes name one
 * input and one output, and a star in either is a mistake worth reporting
 * rather than quietly turning into whatever matched first.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "glob.h"

#if SK_WANT_GLOB

#    if defined(__TURBOC__)
#        include <dir.h>
#    elif defined(_WIN32)
#        include <io.h>
#    else
#        include <dos.h>
#    endif

#    define GLOB_MAX 512 /* files in one expansion; a car has three */

/* A match carries a bare file name, so a pattern with a directory in it has to
 * have that directory put back: `SUBDIR\*.VSH` matches `A.VSH`, and the caller
 * needs `SUBDIR\A.VSH` to be able to open it. */
static char *join_dir(const char *pattern, const char *name)
{
    const char *cut = NULL, *p;
    size_t      dlen, nlen;
    char       *out;

    for (p = pattern; *p; ++p)
        if (*p == '\\' || *p == '/' || *p == ':') cut = p;

    dlen = cut ? (size_t)(cut - pattern) + 1 : 0;
    nlen = strlen(name);
    out = (char *)malloc(dlen + nlen + 1);
    if (!out) return NULL;
    if (dlen) memcpy(out, pattern, dlen);
    memcpy(out + dlen, name, nlen + 1);
    return out;
}

static int has_wildcard(const char *s)
{
    for (; *s; ++s)
        if (*s == '*' || *s == '?') return 1;
    return 0;
}

/* Appends every match for one pattern. Returns the number added, or -1 on a
 * failure the caller should report. A pattern that matches nothing adds
 * nothing and is not an error here: it is passed through unchanged by the
 * caller so the open fails with the name the user typed, which is a better
 * message than "no matches". */
static int expand_one(const char *pattern, char **out, int cap, int n)
{
    /* The assignments below are deliberately not folded into their `if`, which
     * reads more tightly but makes Turbo C 2.01 warn about a possibly
     * incorrect assignment on every one of them. */
#    if defined(__TURBOC__)
    struct ffblk f;
    int          more = findfirst(pattern, &f, 0);
    while (more == 0) {
        if (n >= cap) return -1;
        out[n] = join_dir(pattern, f.ff_name);
        if (!out[n]) return -1;
        ++n;
        more = findnext(&f);
    }
#    elif defined(_WIN32)
    struct _finddata_t f;
    long               h = _findfirst(pattern, &f);
    if (h != -1L) {
        do {
            if (n >= cap) {
                _findclose(h);
                return -1;
            }
            out[n] = join_dir(pattern, f.name);
            if (!out[n]) {
                _findclose(h);
                return -1;
            }
            ++n;
        } while (_findnext(h, &f) == 0);
        _findclose(h);
    }
#    else
    struct find_t f;
    /* Microsoft C 5.10 predates const in its prototypes and declares the path
     * as `char *`, so the cast is what keeps that build quiet. Watcom declares
     * it const and is indifferent to it. */
    unsigned more = _dos_findfirst((char *)pattern, _A_NORMAL, &f);
    while (more == 0) {
        if (n >= cap) return -1;
        out[n] = join_dir(pattern, f.name);
        if (!out[n]) return -1;
        ++n;
        more = _dos_findnext(&f);
    }
#    endif
    return n;
}

int sk_glob(char ***list, int *count)
{
    char **in = *list, **out;
    int    i, n = 0, any = 0;

    for (i = 0; i < *count; ++i)
        if (has_wildcard(in[i])) any = 1;
    if (!any) return 0;

    out = (char **)malloc(sizeof(char *) * GLOB_MAX);
    if (!out) return -1;

    for (i = 0; i < *count; ++i) {
        if (!has_wildcard(in[i])) {
            if (n >= GLOB_MAX) {
                free(out);
                return -1;
            }
            out[n++] = in[i]; /* points into argv, which outlives this */
            continue;
        }
        {
            int before = n;
            n = expand_one(in[i], out, GLOB_MAX, n);
            if (n < 0) {
                free(out);
                return -1;
            }
            /* Nothing matched: keep the pattern so the caller reports it
             * against the name that was actually typed. */
            if (n == before) {
                if (n >= GLOB_MAX) {
                    free(out);
                    return -1;
                }
                out[n++] = in[i];
            }
        }
    }

    *list = out;
    *count = n;
    return 1;
}

#else

int sk_glob(char ***list, int *count)
{
    (void)list;
    (void)count;
    return 0;
}

#endif
