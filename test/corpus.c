/* skidpack's test driver. Two modes:
 *
 *     corpus check <skidpack> <releases-dir> [manifest]
 *     corpus sweep <skidpack> <file>...
 *
 * check is the manifest-driven one. <releases-dir> holds one directory per
 * release, named as corpus.txt's rel column: st10, st11, 4d90, 4d91. You
 * can also point it straight at one unpacked release, in which case rows for
 * the others count as absent. A release directory you leave out is skipped
 * and the run still succeeds; only a run that matched nothing fails.
 * Each row is checked four ways:
 *
 *   the shipped file still checksums to what the manifest recorded
 *   it decompresses to the recorded size and checksum
 *   repacking that output with the recorded parameters reproduces it exactly
 *   nothing is silently skipped
 *
 * sweep needs no manifest: it round-trips whatever files it is given, trying
 * both payload dialects and both compression modes, and reports what did not
 * come back byte for byte. Use it on a release nobody has recorded yet.
 *
 * Wildcards work in both places. A Unix shell expands them; on DOS the C
 * runtime does, if SETARGV.OBJ is linked in, which MSCBUILD.BAT does.
 *
 * Both of these were shell scripts, which is fine everywhere the tool runs
 * except the one place it goes to the most trouble to support. DOS has no sh,
 * so the 16-bit build could only be verified by driving it from a modern host.
 * This is C89 with its own CRC-32 and builds with the same compiler as the
 * rest.
 *
 * No game data lives in this repository. You need your own copies.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Paths are built by hand rather than with snprintf, which is C99. */
#define PATHMAX 260
#define LINEMAX 512

/* ---------------------------------------------------------------- CRC-32 --
 *
 * The manifest identifies files by CRC-32, the same one PKZIP shipped in 1989
 * and the one in PNG and Ethernet: reflected, polynomial 0xEDB88320, seeded
 * with all ones and inverted at the end.
 *
 * SHA-256 was the first thing here and it was the wrong choice for this
 * project. It costs 64 rounds of 32-bit arithmetic per 64-byte block, which on
 * an 8086 with no barrel shifter is punishing, and it made every manifest line
 * 192 characters wide for two 64-character fields. CRC-32 is a table lookup
 * and an exclusive-or per byte, and its four bytes print in eight.
 *
 * What that costs: CRC-32 detects damage, it does not resist an adversary.
 * Anyone who wants a colliding file can construct one. That is the right trade
 * here, because the manifest answers "is this the same file, and did the
 * packer reproduce it", not "did somebody tamper with this". The container
 * format itself carries no checksum at all, so this is strictly more than the
 * game ever had.
 *
 * The table is built on first use rather than stored, which keeps a kilobyte
 * out of the image and costs 256 iterations once.
 */
static unsigned long crc_tab[256];
static int           crc_ready = 0;

#define M32(x) ((x) & 0xFFFFFFFFul)

static void crc_build(void)
{
    unsigned long c;
    int           i, k;

    if (crc_ready) return;
    for (i = 0; i < 256; ++i) {
        c = (unsigned long)i;
        for (k = 0; k < 8; ++k)
            c = (c & 1ul) ? M32(0xEDB88320ul ^ (c >> 1)) : M32(c >> 1);

        crc_tab[i] = c;
    }
    crc_ready = 1;
}

/* CRC-32 of a file, formatted as eight lowercase hex digits. Returns 0 on
 * success, and reports the size, which the manifest records separately. */
static int hash_file(const char *path, char *hex, unsigned long *size)
{
    static const char    digits[] = "0123456789abcdef";
    static unsigned char chunk[4096]; /* static: DOS gives 2 KB of stack */
    FILE                *f;
    size_t               n;
    unsigned             i;
    unsigned long        crc = 0xFFFFFFFFul, total = 0;
    int                  k;

    crc_build();
    f = fopen(path, "rb");
    if (!f) return -1;

    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        for (i = 0; i < (unsigned)n; ++i)
            crc = M32(crc_tab[(crc ^ chunk[i]) & 0xFFul] ^ (crc >> 8));

        total += (unsigned long)n;
    }
    if (ferror(f)) {
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) return -1;

    crc = M32(crc ^ 0xFFFFFFFFul);
    for (k = 7; k >= 0; --k) hex[7 - k] = digits[(crc >> (k * 4)) & 0xFul];

    hex[8] = '\0';
    if (size) *size = total;
    return 0;
}

/* ------------------------------------------------------------------ util -- */

/* Size of a file, or 0 if it cannot be opened. Only used to tell an empty
 * result from a real one, so the distinction between the two does not matter.
 */
static unsigned long file_size(const char *path)
{
    FILE         *f = fopen(path, "rb");
    unsigned long n = 0;

    if (!f) return 0;
    if (fseek(f, 0L, SEEK_END) == 0) {
        long t = ftell(f);
        if (t > 0) n = (unsigned long)t;
    }
    fclose(f);
    return n;
}

static int files_equal(const char *a, const char *b)
{
    static unsigned char ba[2048], bb[2048];
    FILE                *fa, *fb;
    int                  same = 1;

    fa = fopen(a, "rb");
    if (!fa) return 0;
    fb = fopen(b, "rb");
    if (!fb) {
        fclose(fa);
        return 0;
    }

    for (;;) {
        size_t na = fread(ba, 1, sizeof(ba), fa);
        size_t nb = fread(bb, 1, sizeof(bb), fb);
        if (na != nb) {
            same = 0;
            break;
        }
        if (na == 0) break;
        if (memcmp(ba, bb, na) != 0) {
            same = 0;
            break;
        }
    }
    fclose(fa);
    fclose(fb);
    return same;
}

/* DOS wants a backslash, everything else a forward slash. Both accept '/' in
 * practice, but a DOS user reading an error message should see a DOS path. */
#if defined(__MSDOS__) || defined(MSDOS) || defined(_MSDOS) || \
    defined(__TURBOC__)
#    define SEP '\\'
#else
#    define SEP '/'
#endif

/* One character of the program's path as the command interpreter needs it
 * spelled, which is not always how the caller typed it.
 *
 * fopen takes either separator on DOS and on Windows, but COMMAND.COM and
 * cmd.exe both end the command name at a forward slash, because that is where
 * a switch starts. `../skidpack` arrives as the command `..` with the argument
 * `/skidpack`, and every row fails before skidpack runs at all. Only that one
 * token is affected; arguments carrying '/' pass through untouched, which is
 * why a releases directory spelled the Unix way still works.
 *
 * A Unix shell is the other way round - '/' separates and '\' escapes what
 * follows - so there the macro is the identity and the command is byte for
 * byte what it was. Cygwin and MSYS route system() through sh and define
 * neither _WIN32 nor a DOS macro, which puts them on that side too. */
#if defined(__MSDOS__) || defined(MSDOS) || defined(_MSDOS) || \
    defined(__TURBOC__) || defined(_WIN32)
#    define CMD_CH(c) ((char)((c) == '/' ? '\\' : (c)))
#else
#    define CMD_CH(c) ((char)(c))
#endif

static void join2(char *out, const char *a, const char *b)
{
    size_t n = 0;
    while (*a && n < PATHMAX - 1) out[n++] = *a++;
    if (n < PATHMAX - 1) out[n++] = SEP;
    while (*b && n < PATHMAX - 1) out[n++] = *b++;
    out[n] = '\0';
}

static void join3(char *out, const char *a, const char *b, const char *c)
{
    size_t n = 0;
    while (*a && n < PATHMAX - 1) out[n++] = *a++;
    if (n < PATHMAX - 1) out[n++] = SEP;
    while (*b && n < PATHMAX - 1) out[n++] = *b++;
    if (n < PATHMAX - 1) out[n++] = SEP;
    while (*c && n < PATHMAX - 1) out[n++] = *c++;
    out[n] = '\0';
}

/* Split a line on runs of whitespace, in place. Returns the field count.
 *
 * The manifest is space-aligned rather than tab-separated so that it lines up
 * in whatever happens to be displaying it. That costs nothing here: no field
 * can contain a space, because the releases use 8.3 DOS names and every other
 * column is a number, a checksum, or a word from a fixed set. */
static int split_fields(char *line, char **field, int maxfields)
{
    int n = 0;

    while (*line && n < maxfields) {
        while (*line == ' ' || *line == '\t') ++line;
        if (*line == '\0' || *line == '\n' || *line == '\r') break;

        field[n++] = line;
        while (*line && *line != ' ' && *line != '\t' && *line != '\n' &&
               *line != '\r')
            ++line;
        if (*line) *line++ = '\0';
    }
    return n;
}

/* Build and run one skidpack invocation. system() is the only way to start a
 * program from C89, and it is what a shell script did anyway.
 *
 * Its return value is not worth much. C89 says only that it is
 * implementation-defined, and on DOS system() shells out through COMMAND.COM
 * and reports whether the interpreter ran, not what the child returned.
 *
 * DOS itself does carry an exit status: INT 21h function 4Dh returns it, and
 * spawnl(P_WAIT, ...) surfaces it, declared in process.h on both Microsoft C
 * 5.10 and Turbo C 2.01. That road is not taken because process.h is not
 * standard C, and this file compiles unchanged on five compilers across three
 * decades. Callers here decide by looking at the output file, which needs no
 * ifdef and asks the better question anyway: whether the bytes are right,
 * rather than what a process claimed on its way out. */
static int run(const char *sk, const char *mode, const char *in,
               const char *out, const char *target, int sdtitl)
{
    char        cmd[LINEMAX];
    size_t      n = 0;
    const char *parts[9];
    int         i, np = 0;

    /* The program's path goes down first and on its own, since it is the one
     * part of the line the interpreter reads for separators. */
    while (*sk && n < sizeof(cmd) - 1) {
        char c = *sk++;
        cmd[n++] = CMD_CH(c);
    }

    parts[np++] = " ";
    parts[np++] = mode;
    parts[np++] = " ";
    parts[np++] = in;
    parts[np++] = " ";
    parts[np++] = out;
    parts[np++] = " -target ";
    parts[np++] = target;
    parts[np++] = sdtitl ? " -sdtitl" : "";

    for (i = 0; i < np; ++i) {
        const char *s = parts[i];
        while (*s && n < sizeof(cmd) - 1) cmd[n++] = *s++;
    }
    cmd[n] = '\0';
    return system(cmd);
}

/* Round-trip one file without a manifest. Returns 1 if it came back exactly,
 * 0 if it decompressed but would not reproduce, -1 if it is not a container.
 *
 * Telling the last two apart matters. The shell version this replaces asked
 * erify when a round trip failed and filed anything verify rejected under
 * "not a container", so a compressed file that decoded but repacked wrongly
 * disappeared into the skip count. Whether decompression ever succeeded is the
 * honest test, and it is known here without asking anything else.
 */
static int sweep_one(const char *sk, const char *path, const char *raw,
                     const char *rep)
{
    static const char *targets[2] = {"bb11", "bb10-res"};
    static const char *modes[2] = {"c", "cv"};
    int                t, m, decoded = 0;

    /* What run() returned is deliberately ignored. system() does not carry a
     * child's exit status back on DOS, so asking it whether skidpack succeeded
     * gets a confident yes either way. Judging by the output file works
     * everywhere: an absent or empty result is a failure however the exit
     * status was reported.
     *
     * Trusting it counted AD15.DRV and BERNIES.TRK, which skidpack refuses
     * outright, as files that decoded and then failed to reproduce. */
    for (t = 0; t < 2; ++t) {
        remove(raw);
        run(sk, "d", path, raw, targets[t], 0);
        if (file_size(raw) == 0) continue;
        decoded = 1;

        for (m = 0; m < 2; ++m) {
            remove(rep);
            run(sk, modes[m], raw, rep, targets[t], 0);
            if (file_size(rep) == 0) continue;
            if (files_equal(rep, path)) return 1;
        }
    }
    return decoded ? 0 : -1;
}

static int do_sweep(int argc, char **argv)
{
    const char *sk = argv[2];
    const char *raw = "corpraw.tmp", *rep = "corprep.tmp";
    long        ok = 0, bad = 0, skip = 0;
    int         i;

    for (i = 3; i < argc; ++i) {
        switch (sweep_one(sk, argv[i], raw, rep)) {
        case 1:
            ++ok;
            break;
        case 0:
            printf("  MISMATCH: %s\n", argv[i]);
            ++bad;
            break;
        default:
            ++skip;
            break;
        }
    }
    remove(raw);
    remove(rep);

    printf("reproduced: %ld    mismatched: %ld    not a container: %ld\n", ok,
           bad, skip);
    return bad == 0 ? 0 : 1;
}

static int do_check(int argc, char **argv)
{
    static char   line[LINEMAX];
    char         *field[12];
    char          src[PATHMAX], raw[PATHMAX], rep[PATHMAX];
    char          hex[9];
    const char   *sk, *root, *manifest;
    FILE         *mf;
    unsigned long size;
    long          total = 0, ok = 0, absent = 0, bad = 0, lineno = 0;

    sk = argv[2];
    root = argv[3];
    manifest = (argc > 4) ? argv[4] : "corpus.txt";

    mf = fopen(manifest, "r");
    if (!mf) {
        fprintf(stderr, "corpus: cannot open %s\n", manifest);
        return 2;
    }

    /* Temporary files land beside the manifest rather than in the release
     * directory, which may be a read-only mount or a CD image. */
    strcpy(raw, "corpraw.tmp");
    strcpy(rep, "corprep.tmp");

    while (fgets(line, sizeof(line), mf)) {
        const char   *rel, *name, *mode, *target, *bug, *pcrc, *rcrc;
        unsigned long psize, rsize;
        char         *end;
        int           n;
        ++lineno;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        /* A row that does not parse is a fault in the manifest, not a row to
         * pass over. Skipping it quietly loses a file from the count while the
         * run still reports success. */
        n = split_fields(line, field, 12);
        if (n != 9) {
            fprintf(stderr, "%s:%ld: expected 9 fields, found %d\n", manifest,
                    lineno, n);
            ++bad;
            continue;
        }
        ++total;

        /* rel file mode target bug packed raw pkd_crc32 raw_crc32 */
        rel = field[0];
        name = field[1];
        mode = field[2];
        target = field[3];
        bug = field[4];
        pcrc = field[7];
        rcrc = field[8];

        psize = strtoul(field[5], &end, 10);
        if (*end) {
            fprintf(stderr, "%s:%ld: packed size is not a number: %s\n",
                    manifest, lineno, field[5]);
            ++bad;
            continue;
        }
        rsize = strtoul(field[6], &end, 10);
        if (*end) {
            fprintf(stderr, "%s:%ld: raw size is not a number: %s\n", manifest,
                    lineno, field[6]);
            ++bad;
            continue;
        }

        /* The manifest describes <root>/<release>/<file>, which is how you
         * check several releases at once. Failing that, try <root>/<file>, so
         * pointing straight at one unpacked release works without having to
         * rename it to the release code.
         *
         * The two cases fail differently on purpose. If the file is where the
         * manifest says it is and the hash is wrong, that is a fault worth
         * reporting. If it was found only by the flat fallback and the hash is
         * wrong, it is simply a row belonging to a different release, so it is
         * absent rather than broken. Releases share filenames throughout. */
        join3(src, root, rel, name);
        if (hash_file(src, hex, &size) == 0) {
            if (strcmp(hex, pcrc) != 0) {
                printf("%s/%s: shipped file does not match the recorded hash\n",
                       rel, name);
                ++bad;
                continue;
            }
            if (size != psize) {
                printf("%s/%s: shipped file is %lu bytes, manifest says %lu\n",
                       rel, name, size, psize);
                ++bad;
                continue;
            }
        } else {
            join2(src, root, name);
            if (hash_file(src, hex, &size) != 0 || strcmp(hex, pcrc) != 0) {
                ++absent;
                continue;
            }
            if (size != psize) {
                ++absent;
                continue;
            }
        }

        /* Removed before the child runs, not after. system() does not reliably
         * carry the child's exit status on DOS, which sweep_one already works
         * around; here a failed child could leave the previous row's output in
         * place and the hash below would be taken from it. Releases share many
         * byte-identical resources, so a stale file can match. */
        remove(raw);
        if (run(sk, "d", src, raw, target, 0) != 0) {
            printf("%s/%s: decompression failed\n", rel, name);
            ++bad;
            continue;
        }
        if (hash_file(raw, hex, &size) != 0 || strcmp(hex, rcrc) != 0) {
            printf("%s/%s: decompressed output does not match the recorded "
                   "hash\n",
                   rel, name);
            ++bad;
            continue;
        }
        if (size != rsize) {
            printf("%s/%s: decompressed to %lu bytes, manifest says %lu\n", rel,
                   name, size, rsize);
            ++bad;
            continue;
        }

        remove(rep);
        if (run(sk, mode, raw, rep, target, strcmp(bug, "-") != 0) != 0) {
            printf("%s/%s: repack failed\n", rel, name);
            ++bad;
            continue;
        }
        if (!files_equal(src, rep)) {
            printf("%s/%s: repacked bytes differ from the shipped file\n", rel,
                   name);
            ++bad;
            continue;
        }
        ++ok;
    }
    /* A read error stops fgets exactly like end of file does, so without this
     * a truncated read would report however many rows it managed as a pass. */
    if (ferror(mf)) {
        fprintf(stderr, "corpus: error reading %s\n", manifest);
        ++bad;
    }
    if (fclose(mf) != 0) {
        fprintf(stderr, "corpus: error closing %s\n", manifest);
        ++bad;
    }
    remove(raw);
    remove(rep);

    printf("manifest rows      : %ld\n", total);
    printf("reproduced exactly : %ld\n", ok);
    printf("not present here   : %ld\n", absent);
    printf("failed             : %ld\n", bad);

    if (bad > 0) return 1;
    if (ok == 0) {
        fputs("corpus: no release directory matched; nothing was checked\n",
              stderr);
        return 2;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 4 && strcmp(argv[1], "check") == 0) return do_check(argc, argv);
    if (argc >= 4 && strcmp(argv[1], "sweep") == 0) return do_sweep(argc, argv);

    fputs("usage: corpus check <skidpack> <releases-dir> [manifest]\n"
          "       corpus sweep <skidpack> <file>...\n",
          stderr);
    return 2;
}
