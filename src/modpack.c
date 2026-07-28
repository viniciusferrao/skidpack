/* Packing a mod's resources into the form the game prefers.
 *
 * Stunts picks a loader from the file's extension, never from its contents.
 * file_load_shape2d walks a fixed list - .PVS, .XVS, .VSH, .PES, .ESH - and
 * takes the first that exists; the 3D loader tries .P3S and falls back to .3SH.
 * The packed spelling is always tried first, so writing one beside a plain file
 * is enough to make the game read it instead. That is also why a compressor was
 * never needed to publish a mod: the format has always had an uncompressed lane
 * with its own name, and the community has used it since.
 *
 * The pairs differ in what the loader does after reading:
 *
 *   .3SH -> .P3S  file_load_resource type 1 against type 7, which are
 *                 file_load_binary and file_decomp. Nothing else differs
 *                 between them, so the swap is always safe.
 *
 *   .VSH -> .PVS  the packed side also runs file_unflip_shape2d
 *   .ESH -> .PES  the packed side also runs file_unflip_shape2d_pes
 *
 * Both unflip passes transpose a shape's bitmap, and both are driven by a flag
 * in the shape header rather than by the extension: they act only where
 * (s2d_unk6 & 0xF0) is clear and the high nibble of s2d_unk5 is not. A resource
 * whose shapes all leave that nibble clear passes through untouched, so its
 * bytes mean the same thing under either loader and it can be packed as it
 * stands. One that sets it cannot. The game would transpose data that was never
 * transposed, and the result draws as garbage rather than failing, which is the
 * expensive kind of wrong.
 *
 * DSI used the flip: Stunts 1.1 sets it on 1 shape of 13 in STDAANSX.PVS and on
 * 4 of 8 in STDBANSX.PVS. Measured mod shapes leave it clear, which is what
 * reduces packing them to a compress and a rename. sk_modpack_flip_safe keeps
 * that a fact checked per file rather than an assumption held about all mods.
 */
#include <string.h>
#include "modpack.h"

/* clang-format off */
static const struct {
    const char *plain;
    const char *packed;
    int         unflips;
} pairs[] = {
    { "3SH", "P3S", SK_UNFLIP_NONE },
    { "VSH", "PVS", SK_UNFLIP_PVS  },
    { "ESH", "PES", SK_UNFLIP_PES  }
};
/* clang-format on */
#define NPAIRS (int)(sizeof(pairs) / sizeof(pairs[0]))

/* The last dot of the final component, or NULL. Directory separators reset the
 * search so that a dot earlier in the path cannot be mistaken for one. Both
 * slashes count everywhere: a DOS path uses backslashes, but this also runs on
 * hosts where a mod directory was unpacked with forward ones. */
static const char *ext_of(const char *path)
{
    const char *p, *dot = 0;

    for (p = path; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':')
            dot = 0;
        else if (*p == '.')
            dot = p;
    }
    return dot;
}

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* Extensions are matched without regard to case because the releases and the
 * mods disagree about it, and DOS never cared: one archive here ships
 * STDAFUNO.vsh next to STDBFUNO.VSH. */
static int ext_is(const char *dot, const char *want)
{
    int i;

    for (i = 0; i < 3; ++i)
        if (upper(dot[1 + i]) != want[i]) return 0;

    return dot[4] == '\0';
}

/* The new extension is always written in upper case, which is how DOS spells
 * one and how the releases themselves are named. Following the old extension's
 * case instead was tidier on a host that cares and wrong where the file is
 * meant to be read: a mod built on Windows may ship Stdai260.vsh, and its twin
 * belongs in the game directory as a .PVS. Only the extension is decided here.
 * The stem is copied as it stands, since it names a file already on disk and is
 * not this tool's to rename. */
/* Both directions are the same walk over the same table, differing only in
 * which column is matched and which is written, so they share one body. */
static int swap_ext(const char *path, char *out, int *unflips, int to_packed)
{
    const char *dot = ext_of(path);
    size_t      n;
    int         i, k;

    *unflips = 0;
    if (!dot) return 0;

    for (i = 0; i < NPAIRS; ++i) {
        const char *from = to_packed ? pairs[i].plain : pairs[i].packed;
        const char *to = to_packed ? pairs[i].packed : pairs[i].plain;

        if (!ext_is(dot, from)) continue;

        n = (size_t)(dot - path);
        if (n + 5 > SK_MODPACK_PATHMAX - 1) return 0;

        memcpy(out, path, n);
        out[n] = '.';
        for (k = 0; k < 3; ++k) out[n + 1 + k] = to[k];
        out[n + 4] = '\0';
        *unflips = pairs[i].unflips;
        return 1;
    }
    return 0;
}

int sk_modpack_target(const char *path, char *packed, int *unflips)
{
    return swap_ext(path, packed, unflips, 1);
}

int sk_modpack_source(const char *path, char *plain, int *unflips)
{
    return swap_ext(path, plain, unflips, 0);
}

/* The layout file_get_shape2d walks: a 4-byte size, the shape count at offset
 * 4, then a table of 4-byte ids and a table of 4-byte offsets, with shape data
 * starting after both. An offset is relative to the end of those tables.
 *
 * Everything here is bounds-checked against the buffer rather than trusted,
 * because this runs on files nobody vetted. A resource that does not parse is
 * reported as such and left alone, which is the safe answer: refusing to pack a
 * sound file costs a few kilobytes, packing an unsound one costs a mod that
 * looks broken in a way nobody will connect to this tool.
 */
int sk_modpack_flip_safe(rs_cbytep data, rs_size len)
{
    rs_size  dataofs;
    unsigned shapecount, i;

    if (len < 6) return -1;

    shapecount = (unsigned)data[4] | ((unsigned)data[5] << 8);
    if (shapecount == 0) return -1;

    dataofs = ((rs_size)shapecount << 3) + 6;
    if (dataofs > len) return -1;

    for (i = 0; i < shapecount; ++i) {
        rs_size entry = ((rs_size)i << 2) + ((rs_size)shapecount << 2) + 6;
        rs_size chunkofs, shape;

        if (entry + 4 > len) return -1;
        chunkofs = (rs_size)data[entry] | ((rs_size)data[entry + 1] << 8) |
                   ((rs_size)data[entry + 2] << 16) |
                   ((rs_size)data[entry + 3] << 24);

        /* Tested before the addition so a wild offset cannot wrap into a
         * plausible one. */
        if (chunkofs > len) return -1;
        shape = dataofs + chunkofs;
        if (shape + 16 > len) return -1;

        /* s2d_unk6 at 15, s2d_unk5 at 14, matching struct SHAPE2D. The first
         * test is the loader's own early out, so a shape it never examines
         * cannot make the file unsafe. */
        if ((data[shape + 15] & 0xF0) != 0) continue;
        if ((data[shape + 14] >> 4) != 0) return 0;
    }
    return 1;
}

/* Same walk as above, reading the two size fields instead of the two flags.
 *
 * Computed in rs_size rather than the game's own arithmetic, which is done in
 * an int and therefore wraps above 65535 pixels. No shape in the releases or in
 * any published car measured comes near that, so the two agree today; this
 * reports the size the buffer ought to be, and a resource that ever crossed the
 * line would be a hazard in the game rather than a disagreement here. */
rs_size sk_modpack_scratch(rs_cbytep data, rs_size len, int kind)
{
    rs_size  dataofs, most = 0;
    unsigned shapecount, i;

    /* .PES does not measure anything. The loader asks for 1000 paragraphs
     * whatever the file turns out to hold, so the widest shape in it changes
     * nothing and the container is never walked. */
    if (kind == SK_UNFLIP_PES) return SK_UNFLIP_PES_BYTES;
    if (kind != SK_UNFLIP_PVS) return 0;

    if (len < 6) return 0;

    shapecount = (unsigned)data[4] | ((unsigned)data[5] << 8);
    if (shapecount == 0) return 0;

    dataofs = ((rs_size)shapecount << 3) + 6;
    if (dataofs > len) return 0;

    for (i = 0; i < shapecount; ++i) {
        rs_size entry = ((rs_size)i << 2) + ((rs_size)shapecount << 2) + 6;
        rs_size chunkofs, shape, w, h, para;

        if (entry + 4 > len) return 0;
        chunkofs = (rs_size)data[entry] | ((rs_size)data[entry + 1] << 8) |
                   ((rs_size)data[entry + 2] << 16) |
                   ((rs_size)data[entry + 3] << 24);

        if (chunkofs > len) return 0;
        shape = dataofs + chunkofs;
        if (shape + 16 > len) return 0;

        w = (rs_size)data[shape] | ((rs_size)data[shape + 1] << 8);
        h = (rs_size)data[shape + 2] | ((rs_size)data[shape + 3] << 8);
        para = (w * h + 0x20) >> 4;
        if (para > most) most = para;
    }
    return most * 16;
}
