/* Wildcard expansion for the hosts whose C runtime leaves it to the program.
 * See glob.c for why this is here rather than done at link time.
 */
#ifndef SKIDPACK_GLOB_H
#define SKIDPACK_GLOB_H

#include "cli.h"

/* Where the runtime does not expand argv, so this has to. A Unix shell has
 * already done the work before the program starts, and MinGW's runtime globs
 * by default, so both are left alone; expanding twice would be harmless but
 * pointless. Open Watcom does not expand on any of its targets, which is what
 * makes the Win32 case belong on this side. */
#if SK_DOS_SWITCHES
#    define SK_WANT_GLOB 1
#else
#    define SK_WANT_GLOB 0
#endif

/* Expands any pattern in *list, replacing it with the matches. Returns 1 when
 * it replaced the list, 0 when there was nothing to expand, -1 on failure.
 *
 * On 1 the new list is heap allocated and its entries are a mix of heap
 * strings and pointers into argv, so it is never freed; the process is about
 * to use it and then exit. A pattern matching nothing stays in the list as
 * typed, so the caller reports the name the user gave rather than silently
 * packing fewer files than were asked for. */
int sk_glob(char ***list, int *count);

#endif /* SKIDPACK_GLOB_H */
