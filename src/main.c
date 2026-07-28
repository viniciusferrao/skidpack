/* skidpack - decompress, compress and verify DSI resource files. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "skidpack.h"
#include "cli.h"
#include "version.h"
#include "modpack.h"
#include "sdtitl.h"
#include "glob.h"

#if defined(__TURBOC__)
/* Turbo C sizes the stack from this variable rather than from a linker switch,
 * so this is the counterpart of the /STACK:8192 in MSCBUILD.BAT. Its 4 KB
 * default did run the corpus, but there is no reason for the two DOS builds to
 * differ on something this easy to get wrong. */
unsigned _stklen = 8192;
#endif

static int read_file(const char *path, rs_buf *b)
{
    /* Static because a 16-bit DOS build gets a 2 KB default stack, and 4 KB of
     * automatic buffer overflows it before main() does anything. The failure
     * mode is an abnormal exit with no diagnostic, which is miserable to
     * debug. */
    static unsigned char chunk[4096];
    FILE                *f = fopen(path, "rb");
    long                 sz;
    int                  ioerr;
    size_t               n;
    if (!f) return -1;

    /* Pre-size from the file length so the buffer is allocated once instead of
     * doubling its way there. Falls back to growth if the stream
     * will not seek. */
    if (fseek(f, 0L, SEEK_END) == 0) {
        sz = ftell(f);
        if (sz > 0) rs_buf_reserve(b, (rs_size)sz);
        rewind(f);
    }
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        rs_buf_append(b, chunk, (rs_size)n);

    /* fread returning 0 means end of file OR a read error, and only ferror
     * tells them apart. Treating a short read as the whole file would let this
     * tool compress or vet a fragment of a resource and report success, which
     * for something pointed at ageing media is the worst failure it has. */
    ioerr = ferror(f);
    if (fclose(f) != 0) ioerr = 1;

    return (b->err || ioerr) ? -1 : 0;
}

static int write_file(const char *path, rs_cbytep p, rs_size n)
{
    FILE   *f;
    rs_size done = 0;
    if (!path) {
        fputs("skidpack: this mode needs an output file\n", stderr);
        return -1;
    }
    f = fopen(path, "wb");
    if (!f) return -1;

    /* one byte at a time is portable across the far/huge pointer models and
     * fast enough via the stdio buffer */
    while (done < n) {
        if (fputc(p[done], f) == EOF) {
            fclose(f);
            return -1;
        }
        ++done;
    }
    return fclose(f) ? -1 : 0;
}

/* A consistency check, not a proof of integrity.
 *
 * A Huffman tree is built from a histogram of the data it encodes, so in a
 * sound file a symbol occurring more often never carries a longer code. An
 * inversion therefore proves the stored tree and the payload disagree, which
 * is strong evidence of damage after writing.
 *
 * The converse does not hold, and this must not imply it does. Zero inversions
 * cannot prove a file intact: a two-symbol tree gives both symbols one-bit
 * codes, so any payload bit can flip, changing the output, while every code
 * length and the physical length stay exactly as they were. The same blindness
 * covers damage that swaps symbols within one length class, or shifts
 * frequencies without reordering them. Catching that needs a checksum, and the
 * format carries none.
 *
 * Both dialects are scored rather than stopping at the first that reaches
 * zero, so a file that reads equally well either way is reported as ambiguous
 * instead of being assigned a dialect by loop order.
 */
static int do_verify(const options *o, const rs_buf *in)
{
    rs_size sp = (in->data[0] & 0x80) ? 4 : 0;
    rs_size expected = 0, bestExpected = 0, actual;
    long    invs, best = -1;
    int     ord, bestOrder = RS_VLE_MSB, decoded = 0, zeros = 0;

    for (ord = 0; ord < 2; ++ord) {
        rs_buf stage;
        int    order = ord ? RS_VLE_LSB : RS_VLE_MSB;
        rs_buf_init(&stage);
        if (!rs_decomp(in->data, in->len, 1, order, &stage)) {
            decoded = 1;
            if (!rs_vle_inversions(in->data + sp, in->len - sp, stage.data,
                                   stage.len, &invs, &expected)) {
                if (invs == 0) ++zeros;
                if (best < 0 || invs < best) {
                    best = invs;
                    bestOrder = order;
                    bestExpected = expected;
                }
            }
        }
        rs_buf_free(&stage);
    }

    if (!decoded) {
        printf("%s: FAIL - does not decompress\n", o->in);
        return 1;
    }

    /* Not a pass this check understands. Saying so and exiting 0 would read as
     * "checked and sound" to anything driving this in a loop. */
    if (best < 0) {
        printf("%s: NOT CHECKED - the outer pass is not Huffman\n", o->in);
        return 2;
    }

    if (zeros == 2)
        printf(
            "%s: note - consistent under both bit orders, dialect ambiguous\n",
            o->in);

    if (best > 0) {
        printf("%s: CORRUPT - %ld frequency/length inversions "
               "(a sound file has 0)\n",
               o->in, best);
        return 1;
    }

    /* Three cases, not two. A file shorter than its stream is truncated and is
     * a fault; longer is trailing debris and is not. Reporting only the long
     * case left the short one indistinguishable from a clean file. */
    actual = in->len - sp;
    if (actual < bestExpected) {
        printf("%s: TRUNCATED - %lu bytes short of the %lu-byte stream (%s)\n",
               o->in, (unsigned long)(bestExpected - actual),
               (unsigned long)bestExpected,
               bestOrder == RS_VLE_LSB ? "LSB-first" : "MSB-first");
        return 1;
    }
    if (actual > bestExpected)
        printf("%s: OK (%s) - %lu trailing bytes past the stream, unread\n",
               o->in, bestOrder == RS_VLE_LSB ? "LSB-first" : "MSB-first",
               (unsigned long)(actual - bestExpected));
    else
        printf("%s: OK - tree consistent with its payload (%s)\n", o->in,
               bestOrder == RS_VLE_LSB ? "LSB-first" : "MSB-first");
    return 0;
}

/* `quiet` is for the bulk caller. Failing to derive an escape table is fatal
 * when one file was asked for and routine when many were: pack falls back to
 * Huffman alone, and a line of explanation per file would bury the report. */
static int do_compress_container(const options *o, const rs_buf *in,
                                 rs_buf *out, int quiet)
{
    rs_buf        stage;
    unsigned char esc[RS_RLE_NESC];
    int           noseq = o->noseq, rc;

    if (o->have_esc)
        memcpy(esc, o->esc, RS_RLE_NESC);
    else {
        /* The skip bit falls out of the same computation: if no candidate byte
         * is free of the single-byte pass's output there is nothing that can
         * safely delimit a sequence, so the pass cannot run. */
        int derived_skip = 0;
        if (rs_rle_pick_escapes(in->data, in->len, esc, &derived_skip)) {
            if (!quiet)
                fputs("skidpack: cannot derive an escape table for this data "
                      "(too few candidate byte values) - pass -esc\n",
                      stderr);
            return -1;
        }
        if (derived_skip) noseq = 1;
    }

    rs_buf_init(&stage);
    rc = rs_rle_encode(in->data, in->len, esc, noseq, &stage);
    if (!rc) {
        rs_buf_push(out, (unsigned char)(0x80 | 2)); /* multipass, 2 passes */
        rs_put24(out, in->len);
        rc = rs_vle_encode(stage.data, stage.len, o->bitorder, out);
    }
    rs_buf_free(&stage);
    return rc;
}

/* Compress one buffer the best of the two ways the game can read, and say
 * which won. Mode c is what every shipped .PVS and .P3S uses, but it does not
 * always win on a mod's data, and a resource that will not yield an escape
 * table has no container form at all. */
static int pack_best(const options *o, const rs_buf *in, rs_buf *out)
{
    rs_buf vle;
    int    have_c;

    have_c = (do_compress_container(o, in, out, 1) == 0 && !out->err);

    rs_buf_init(&vle);
    if (rs_vle_encode(in->data, in->len, o->bitorder, &vle) == 0 && !vle.err) {
        if (!have_c || vle.len < out->len) {
            rs_buf_free(out);
            *out = vle;
            return 0;
        }
    }
    rs_buf_free(&vle);
    return have_c ? 0 : -1;
}

/* Does the output read back as the input, byte for byte?
 *
 * Cheap next to the compression that produced it, and the only thing here that
 * asks the question the mod author cares about. It is not the whole of what
 * d78890e taught, though: skidpack decoded its own 16-bit codes perfectly while
 * the game's decoder drifted, so this proves the container is well formed, not
 * that Stunts reads it. The depth cap in vle.c is what covers that. */
static int packs_back(const options *o, const rs_buf *in, const rs_buf *packed)
{
    rs_buf  back;
    rs_size i;
    int     same = 0;

    rs_buf_init(&back);
    if (!rs_decomp(packed->data, packed->len, 0, o->bitorder, &back) &&
        back.len == in->len) {
        /* Compared by index rather than with memcmp. These are huge pointers
         * on a 16-bit host and memcmp takes a far one, so the conversion
         * silently stops the comparison at the end of a segment, and its
         * length argument is a size_t that wraps at 64 KB besides. A mod's
         * dashboard resource is past 100 KB, which is exactly where a check
         * that quietly compares the first segment would start passing
         * everything. Turbo C 2.01 is the compiler that says so. */
        same = 1;
        for (i = 0; i < in->len; ++i) {
            if (back.data[i] != in->data[i]) {
                same = 0;
                break;
            }
        }
    }

    rs_buf_free(&back);
    return same;
}

/* Percentage saved, rounded to nearest rather than truncated. Plain integer
 * division reports 76 for a file that actually shrank by 75.3, so every
 * inexact line overstated the saving by up to a point and the totals column
 * disagreed with anything that measured the files afterwards. */
/* Both guards earn their place. plain is zero for a container that decodes to
 * nothing, which divided by zero here. And packed can exceed plain when a file
 * decodes smaller than its own packed form, where the subtraction wraps because
 * rs_size is unsigned and the percentage came out astronomical. */
static unsigned long saved_pct(rs_size plain, rs_size packed)
{
    if (plain == 0 || packed >= plain) return 0;
    return (unsigned long)(((plain - packed) * 100 + plain / 2) / plain);
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Bulk mode, both directions. Each file is judged on its own and a reason is
 * printed for every one left alone, because a silent skip in a run over a whole
 * mod directory reads as "nothing to do here" when it may mean "this one would
 * have broken".
 *
 * Nothing is deleted and nothing is overwritten. The file given stays where it
 * is, which costs disk and keeps the mod editable, and an existing twin is left
 * untouched rather than replaced, since it may be the author's own and this
 * tool has no way to tell.
 *
 * The flip check guards both directions, and for the same reason read twice.
 * What a container holds is the flipped form and what the plain extension holds
 * is not, so a shape marked for transposing means the two are different data
 * and neither name can simply be swapped for the other. Packing one would have
 * the game transpose what was never transposed; unpacking one would hand the
 * game a file it does not transpose at all. Every mod measured leaves the
 * nibble clear, which is what makes the swap a rename; the check is what keeps
 * that a fact per file rather than an assumption about mods in general.
 *
 * The report is laid out for an 80-column DOS screen, which is where a run over
 * a mod directory is most likely to be read, and in the upper case a DOS screen
 * shows anyway. The columns spend 41 characters before the last one:
 *
 *     name 12, plain 9, packed 9, saved 6, and four spaces between them
 *
 * leaving 38. Twelve is the whole of an 8.3 name and every resource these
 * releases and their mods carry is one, so a wider field only pushed the reason
 * away from what it explains. No reason may carry a file name: the twin differs
 * from the one already in the first column by its extension alone, and spelling
 * it out again cost 29 of those columns and wrapped the row.
 *
 * The text is upper case rather than the file names being folded to it. On DOS
 * they arrive that way already, since that is what FAT stores and what the
 * runtime hands to argv, so the report reads all of a piece there. Folding a
 * name would print one that exists nowhere else: a mod built on Windows may
 * ship Stdai260.vsh, and that stem is not this tool's to rename.
 *
 * Only the extensions with a twin get a row. `p *` over a mod directory
 * otherwise spent most of its report saying nothing about the .RES, the readme
 * and the zip that were never candidates, which buries the files that are. They
 * are still counted, so the totals account for everything given. What does earn
 * a row is a file that could have been converted and was not: no gain, a shape
 * claiming a flip, a container that will not read.
 *
 * A name past 12 characters still pushes the row wide. No layout prevents that,
 * truncating a path in a report is worse, and 8.3 does not reach it.
 */
static int do_bulk(const options *o, int unpacking)
{
    static char   twin[SK_MODPACK_PATHMAX];
    unsigned long tot_plain = 0, tot_packed = 0, tot_scratch = 0;
    int           wrote = 0, nopair = 0, stored = 0, present = 0;
    int           flipped = 0, invalid = 0, failed = 0, f;
    char        **list = o->list;
    int           nlist = o->nlist;

    /* Only here, and only for the modes that take a list. On a Unix shell this
     * does nothing because the shell expanded the pattern before the program
     * started; on DOS and under Open Watcom on any target it is what makes
     * `p *.VSH` mean what README.md says it means. */
    if (sk_glob(&list, &nlist) < 0) {
        fprintf(stderr, "%s: too many files matched\n",
                sk_progname("skidpack"));
        return 1;
    }

    printf("%-12s %9s %9s %6s  %s\n", "FILE", "PLAIN", "PACKED", "SAVED",
           "RESULT");

    for (f = 0; f < nlist; ++f) {
        const char   *path = list[f];
        rs_buf        in, out;
        options       fo;
        unsigned long plain, packed;
        int           unflips, safe, paired;

        paired = unpacking ? sk_modpack_source(path, twin, &unflips)
                           : sk_modpack_target(path, twin, &unflips);
        if (!paired) {
            ++nopair;
            continue;
        }
        if (file_exists(twin)) {
            printf("%-12s %9s %9s %6s  EXISTS\n", path, "-", "-", "-");
            ++present;
            continue;
        }

        /* Per file, because bb10 reads the dialect off a name. Packing writes
         * the container so the twin names it; unpacking reads one, so the file
         * given does. */
        fo = *o;
        fo.bitorder =
            sk_order_for(unpacking ? path : (const char *)twin, o->bitorder);

        rs_buf_init(&in);
        rs_buf_init(&out);

        if (read_file(path, &in) || in.len == 0) {
            printf("%-12s %9s %9s %6s  READ ERROR\n", path, "-", "-", "-");
            ++failed;
            goto next;
        }

        if (unpacking) {
            if (rs_decomp(in.data, in.len, 0, fo.bitorder, &out) || out.err) {
                printf("%-12s %9s %9lu %6s  INVALID FORMAT\n", path, "-",
                       (unsigned long)in.len, "-");
                ++invalid;
                goto next;
            }
        } else {
            if (unflips) {
                safe = sk_modpack_flip_safe(in.data, in.len);
                if (safe == 0) {
                    printf("%-12s %9lu %9s %6s  FLIPFLAG SET\n", path,
                           (unsigned long)in.len, "-", "-");
                    ++flipped;
                    goto next;
                }
                if (safe < 0) {
                    printf("%-12s %9lu %9s %6s  INVALID FORMAT\n", path,
                           (unsigned long)in.len, "-", "-");
                    ++invalid;
                    goto next;
                }
            }

            if (pack_best(&fo, &in, &out)) {
                printf("%-12s %9lu %9s %6s  PACK FAILED\n", path,
                       (unsigned long)in.len, "-", "-");
                ++failed;
                goto next;
            }
            if (out.len >= in.len) {
                printf("%-12s %9lu %9lu %6s  STORED\n", path,
                       (unsigned long)in.len, (unsigned long)out.len, "-");
                ++stored;
                goto next;
            }
            if (!packs_back(&fo, &in, &out)) {
                printf("%-12s %9lu %9lu %6s  VERIFY FAILED\n", path,
                       (unsigned long)in.len, (unsigned long)out.len, "-");
                ++failed;
                goto next;
            }
        }

        /* A container that decodes to nothing is not a resource. The flip check
         * below would catch it for the 2D pairs, which parse the result, but
         * .P3S parses nothing and reached the report with a zero-length plain
         * side. Refused here so every pair is covered by one rule. */
        if (out.len == 0) {
            printf("%-12s %9s %9lu %6s  INVALID FORMAT\n", path, "-",
                   (unsigned long)in.len, "-");
            ++invalid;
            goto next;
        }

        /* Checked after decompression when unpacking, because the flags live in
         * the data the container carries rather than in the container. */
        if (unpacking && unflips) {
            safe = sk_modpack_flip_safe(out.data, out.len);
            if (safe == 0) {
                printf("%-12s %9lu %9lu %6s  FLIPFLAG SET\n", path,
                       (unsigned long)out.len, (unsigned long)in.len, "-");
                ++flipped;
                goto next;
            }
            if (safe < 0) {
                printf("%-12s %9lu %9lu %6s  INVALID FORMAT\n", path,
                       (unsigned long)out.len, (unsigned long)in.len, "-");
                ++invalid;
                goto next;
            }
        }

        if (write_file(twin, out.data, out.len)) {
            printf("%-12s %9s %9s %6s  WRITE ERROR\n", path, "-", "-", "-");
            ++failed;
            goto next;
        }

        /* Same two numbers whichever way the run went, so a column means one
         * thing: what the resource weighs plain, and what it weighs packed. */
        plain = (unsigned long)(unpacking ? out.len : in.len);
        packed = (unsigned long)(unpacking ? in.len : out.len);
        printf("%-12s %9lu %9lu %5lu%%  %s\n", path, plain, packed,
               saved_pct(plain, packed), twin);
        tot_plain += plain;
        tot_packed += packed;
        /* Off the plain form either way: that is the data the loader unflips,
         * and it is what `in` holds when packing and `out` when unpacking. */
        if (unflips)
            tot_scratch += (unsigned long)sk_modpack_scratch(
                unpacking ? out.data : in.data, unpacking ? out.len : in.len);
        ++wrote;

    next:
        rs_buf_free(&in);
        rs_buf_free(&out);
    }

    printf("\nTARGET   : %s%s\n", o->target,
           o->have_target ? "" : " (default)");
    printf("FILES    : %d\n", nlist);
    printf("%-8s : %d\n", unpacking ? "UNPACKED" : "PACKED", wrote);
    /* What the packed form costs the game that the plain one does not: the
     * buffer it unflips each of these in. Reported rather than judged, because
     * how much a given installation has left is its own question. */
    if (tot_scratch) printf("SCRATCH  : %lu BYTES\n", tot_scratch);
    printf("SKIPPED  : %d\n", nopair);
    if (!unpacking) printf("STORED   : %d\n", stored);
    printf("EXISTS   : %d\n", present);
    printf("FLIPFLAG : %d\n", flipped);
    printf("INVALID  : %d\n", invalid);
    printf("ERRORS   : %d\n", failed);

    /* The one number a reader came for, kept out of the counter block so it is
     * not read as another counter. Written as a sentence for the same reason.
     * `tot_plain` is always the plain side and `tot_packed` the packed one
     * whichever direction the run went, so only the order of the two changes.
     * A ratio is a fact about compressing, so unpacking does not claim one. */
    if (tot_plain) {
        if (unpacking)
            printf("\nUnpacked: %lu bytes into %lu bytes.\n", tot_packed,
                   tot_plain);
        else
            printf("\nCompressed: %lu bytes into %lu bytes. Ratio: %lu%%\n",
                   tot_plain, tot_packed, saved_pct(tot_plain, tot_packed));
    }

    /* A legend only for what actually happened. These carry a decision the
     * reader may want to argue with; the rest say what they mean. A run where
     * everything converted prints nothing here, which is the common one. */
    if (stored || flipped || invalid) putchar('\n');
    if (stored)
        puts("STORED: packing would enlarge the file, so the plain one was "
             "kept.");
    if (flipped)
        puts("FLIPFLAG: a shape is marked for transposing; the two forms are "
             "not the same data.");
    if (invalid)
        puts("INVALID: the file did not read as a shape resource, so it was "
             "left alone.");

    return failed ? 1 : 0;
}

int main(int argc, char **argv)
{
    options     o;
    rs_buf      in, out;
    int         rc = 1, derived, help;
    const char *prog = sk_progname(argv[0]);

    help = sk_wants_help(argc, argv);
    if (help) {
        sk_usage(stdout, prog, help == 2);
        return 0;
    }

    /* Before the usage, so that asking for the version gets the version rather
     * than a screen of modes with the version at the top of it. */
    if (sk_wants_version(argc, argv)) {
        fputs(SK_BANNER, stdout);
        return 0;
    }

    /* What it takes and where the detail is. Anyone who runs a tool bare wants
     * those two things, not the detail unasked.
     *
     * The usage block is the same two lines the help opens with, rather than a
     * shortened one. Someone who sees both sees one invocation described one
     * way, and the second line is where pack lives, which is the mode a reader
     * arriving now is most likely to be after. */
    if (argc < 2) {
        fputs(SK_BANNER "\n", stderr);
        fprintf(stderr,
                "usage:\n"
                "  %s MODE FILE... [options]\n\n"
                "help:\n"
                "  %s %s\n",
                prog, prog, SK_HELP_FLAG);
        return 2;
    }
    if (sk_parse_args(argc, argv, &o)) {
        sk_usage(stderr, prog, 0);
        return 2;
    }

    /* Every run announces itself, not only the ones that end in a usage screen.
     * It goes to stdout because the work does: the data leaves through the
     * output file, so nothing here is in the way of a pipe. */
    fputs(SK_BANNER "\n", stdout);

    /* Handled before the single-file plumbing below, which has no input file
     * to read: pack is given a list and does its own reading per entry. */
    if (o.mode == SK_MODE_PACK) return do_bulk(&o, 0);
    if (o.mode == SK_MODE_UNPACK) return do_bulk(&o, 1);

    /* bb10 does not settle its dialect until a file name is in hand, and the
     * name that carries it is the container's: the input when reading one, the
     * output when about to write one. Every other target answers unchanged.
     *
     * Which means the answer follows the name the caller chose. Compress to
     * WORK.TMP rather than to CGA.COD and the extension says nothing, the
     * resource dialect is assumed, and the file comes out wrong in a way that
     * only shows up in the game. So when the dialect was worked out rather than
     * stated, the result line says which one it landed on. */
    derived = (o.bitorder == SK_ORDER_BY_EXT);
    o.bitorder = sk_order_for(
        (o.mode == SK_MODE_D || o.mode == SK_MODE_VERIFY) ? o.in : o.out,
        o.bitorder);

    rs_buf_init(&in);
    rs_buf_init(&out);
    if (read_file(o.in, &in) || in.len == 0) {
        fprintf(stderr, "skidpack: cannot read %s\n", o.in);
        goto done;
    }

    switch (o.mode) {
    case SK_MODE_VERIFY:
        rc = do_verify(&o, &in);
        goto done;

    case SK_MODE_D:
        if (rs_decomp(in.data, in.len, o.stop, o.bitorder, &out)) {
            fprintf(stderr, "skidpack: decompress failed on %s\n", o.in);
            goto done;
        }
        break;

    case SK_MODE_CV:
        if (rs_vle_encode(in.data, in.len, o.bitorder, &out)) goto done;
        break;

    case SK_MODE_CR:
        if (!o.have_esc) {
            fputs("skidpack: cr needs -esc\n", stderr);
            goto done;
        }
        if (rs_rle_encode(in.data, in.len, o.esc, o.noseq, &out)) goto done;
        break;

    case SK_MODE_C:
        if (do_compress_container(&o, &in, &out, 0)) goto done;
        break;

    default:
        /* sk_parse_args rejects an unknown mode, so reaching this means the
         * mode table and this switch have drifted apart. */
        sk_usage(stderr, prog, 0);
        rc = 2;
        goto done;
    }

    /* Reproduce one shipped file's trailing debris, after the stream is
     * complete so it cannot influence the coding. See sdtitl.c. */
    if (o.sdtitl_bug && sk_sdtitl_apply(&out)) goto done;

    if (out.err) {
        fputs("skidpack: out of memory\n", stderr);
        goto done;
    }

    if (write_file(o.out, out.data, out.len)) {
        fprintf(stderr, "skidpack: cannot write %s\n", o.out);
        goto done;
    }
    printf("%s: %lu -> %lu bytes%s\n", o.in, (unsigned long)in.len,
           (unsigned long)out.len,
           !derived                   ? ""
           : o.bitorder == RS_VLE_LSB ? " (bb10 resource dialect)"
                                      : " (bb10 loader dialect)");
    rc = 0;

done:
    rs_buf_free(&in);
    rs_buf_free(&out);
    return rc;
}
