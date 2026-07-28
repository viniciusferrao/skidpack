# Developing skidpack

What you need to work on the code. [README.md](README.md) covers using it.

## The compilers

`skidpack` is compiled and tested with all of these:

| compiler | why it is here |
|---|---|
| Microsoft C 5.10 | what the shipped game executables fingerprint to |
| Turbo C 2.01 | a second opinion on the 16-bit allocator |
| Open Watcom 1.9, 16-bit | strict ANSI, which hides two spellings |
| Open Watcom 1.9, 32-bit DOS | a DOS target with a flat address space |
| GCC | builds in CI, and runs cppcheck and `-fanalyzer` |
| Clang | builds in CI, and runs the address and UB sanitizers |

The four DOS compilers are not redundant. Turbo C reaches the allocator
through `farmalloc` where Microsoft C uses `halloc`, and Watcom's strict
ANSI mode hides the unreserved spellings of `huge` and `stricmp`, so only
the reserved ones survive it. The 32-bit target has no huge pointers at
all, which is what keeps the `M_I86` guards in `buf.c` honest. `EGA.CMN`
below explains why any of that exists.

The goal is period-correct software. `skidpack` is a recreation, built
from what could be worked out about a tool that never left Distinctive
Software, and keeping these compilers building is what lets it run on a
machine of the era.

## Traps

**Turbo C miscompiles a source file with Unix line endings.** It emits a
nearly empty object, reports nothing, and the failure surfaces much later
as a pile of undefined externals at link time.
[`.gitattributes`](.gitattributes) stores the sources LF, so a fresh
checkout needs converting before `TCBUILD` will work:

    unix2dos src/*.c src/*.h test/corpus.c

Without `unix2dos`, either of these does the same job:

    sed -i 's/$/\r/' src/*.c src/*.h test/corpus.c    # on a Unix host
    perl -pi -e 's/\n/\r\n/' src/*.c src/*.h test/corpus.c

Only Turbo C cares. The other compilers read LF perfectly well, so
convert a copy rather than the tree if you build with more than one.

**Run the formatter before pushing; CI treats it as blocking.** The
version is pinned because clang-format's defaults move between majors,
and a mismatch reformats the tree the day the runner image changes. Point
`CLANG_FORMAT` at a matching build, or call it directly:

    clang-format --dry-run --Werror --style=file src/*.c src/*.h

## Testing

    make check DIR=/path/to/releases    # verify against the manifest
    make sweep DIR=/path/to/releases    # round-trip anything, no manifest

`DIR` holds one directory per release, named `st10`, `st11`, `4d90` and
`4d91`, or a single unpacked release. Releases you leave out are counted
as absent and the run still succeeds.

`check` verifies every row of [`test/corpus.txt`](test/corpus.txt) end to
end: the shipped file matches its recorded CRC-32, unpacks to the
recorded CRC-32, and repacks to the shipped bytes. The manifest also
records the mode, dialect and flags each file needs.

`sweep` needs no manifest and tries both dialects and both modes. Use it
on a release the manifest does not cover.

[`test/corpus.c`](test/corpus.c) is C89, so the tests run on DOS too
rather than having to be driven from a modern host. The batch files do
not build it unless asked, because someone who wants the tool should not
wait for the checker:

    MSCBUILD CORPUS
    TCBUILD CORPUS
    WCLBUILD CORPUS
    WCLBUILD 386 CORPUS

The makefile already works that way; `make` builds the packer and `make
check` builds the driver.

CI builds a shape container of its own and runs `p` and `u` over it,
which covers both directions and the refusal that keeps a mod from being
packed into something the game would transpose.

## What packing costs the game

A packed 2D shape is unflipped after it is decompressed, into a scratch
buffer sized from the largest shape in the file. A plain one takes the
`file_load_binary` path and allocates nothing.

`file_get_unflip_size` walks every shape in the container, reads the
width and height out of each 16-byte `SHAPE2D` header, and keeps the
largest of

    paragraphs = (width * height + 0x20) >> 4

then allocates `paragraphs * 16` bytes. The cost is the biggest single
shape rounded up to a 16-byte paragraph, plus two paragraphs of slack,
and not the size of the file or the number of shapes in it. A container
of many small shapes costs nothing extra; one oversized dashboard sets
the cost for the whole file.

That is 22432 bytes for a stock dashboard and 43552 for the widest mod
one measured. `p` prints the same number as `SCRATCH`, computed by
`sk_modpack_scratch` from the identical formula, so the cost is visible
before anything is written.

`mmgr_alloc_pages` evicts chunks to make room and the game holds 50 of
them, so once those buffers push demand past what fits, something still
in use is dropped and the next screen to touch it dies with `memory
manager - BLOCK NOT FOUND`.

### What is actually known

That the buffer exists and how it is sized. Whether it ever overflows is
not known.

A packed mod car loads, draws, races against an opponent, and reaches the
results screen. It has also been played for minutes at a time without
complaint. In those runs it was stable.

## The case of `EGA.CMN`: it will not fit in a segment

`EGA.CMN` is the reason this codebase looks the way it does on a 16-bit
host.

It is 147456 bytes unpacked in Stunts 1.0, and `SDOSEL.PVS` unpacks past
109 KB. Both are larger than a 16-bit segment, so neither the input nor
the output buffer can be addressed by a plain `far` pointer, whose offset
wraps at 64 KB and silently starts overwriting the beginning of its own
segment.

What follows from that is not a matter of style.

`rs_size` is `unsigned long` rather than `size_t`. On a 16-bit host
`size_t` is 16 bits, so every length and offset in the codec would wrap
at 64 KB. The wrap would not be reported; it would just produce a corrupt
file.

Buffers are `RS_HUGE`, which is `huge` on Microsoft C and Turbo C and
`__huge` under Watcom's strict ANSI mode. Huge-pointer arithmetic
normalises across segment boundaries, so `p[70000]` means what it says.
It also costs a runtime call per dereference, which is why it is a macro
that vanishes everywhere else rather than a blanket choice.

Allocation goes through `halloc` on Microsoft C and `farmalloc` on Turbo
C, because `malloc` cannot return more than a segment on those hosts.
`buf.c` carries both paths, and that is the specific thing the two 16-bit
builds in the compiler table are kept alive to check.

The guard is `#if defined(M_I86) || defined(_M_I86) ||
defined(__TURBOC__)`, which asks whether the *target* is 16-bit rather
than whether the host is DOS. DOS had 32-bit compilers too, and under
those the address space is flat, there is nothing to normalise, and
`huge` is not a keyword at all. Getting that test backwards is how a
32-bit DOS build stops compiling, which is the reason one is in the
matrix.

A modern host reaches none of this. `RS_HUGE` expands to nothing,
`rs_size` is merely a wide integer, and the whole chapter is invisible.
It only exists because one 147456-byte file shipped in the original
release.

## Format

The container and tree layout are based on the [Stunts
wiki](https://wiki.stunts.hu/wiki/Compression). What it does not cover,
the RLE escape table and how a packer chooses one, is described in the
code comments.

Two things the wiki gets differently. The maximum Huffman depth is 15 and
not 16: the game's level-offset accumulator is an `unsigned short`, and
at depth 16 the bound it compares against wraps to zero. And the packed
and plain forms of a 2D shape are not always the same bytes, because the
packed loader runs an unflip pass that the plain one does not.

### Why a `.VSH` or `.ESH` is sometimes refused

That unflip pass is the reason `p` will not always pack a 2D shape.
`file_unflip_shape2d` transposes a shape whose header asks for it, and it
runs only on the packed path. So for a shape marked that way the plain
bytes and the packed bytes are not the same picture, and packing it would
change what the game draws. `sk_modpack_flip_safe` reads the flag out of
the container and the file is skipped when any shape in it claims a flip.
`.3SH` has no such pass and is always safe.

## Releasing

`SK_VERSION` in [`src/version.h`](src/version.h) is the only place a
version appears. The banner reads it, and so does `/V`.

Binaries are built with Open Watcom and only with Open Watcom. It is the
one toolchain here whose licence allows shipping what it produces.

    WCLBUILD          16-bit DOS   runs on anything, needs nothing
    WCLBUILD 386      32-bit DOS   needs a 386, extender built in
    WCLBUILD WIN32    Win32        imports KERNEL32 and USER32

There are two ways to check a download. `SHA256SUMS` covers the archives
for anyone on a modern machine, and the ZIP format already stores a
CRC-32 per member, the same one PKZIP shipped in 1989 and the same one
`test/corpus.txt` uses. So a reader on DOS with no SHA-256 tool checks
the contents with

    PKUNZIP -T SKIDPK10.ZIP

### All three binaries reproduce, one of them with help

Both DOS builds are byte for byte reproducible on their own.

Watcom fills in a `TimeDateStamp` on a Win32 executable. It sits at
offset 8 from the PE signature, which is itself found through `e_lfanew`
at `0x3C`, and it is the only thing separating two builds of one commit.
The release post-processes the linker's output to zero those four bytes,
which makes the third binary reproducible too.
