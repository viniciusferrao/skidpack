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

We use a variety of compilers to aim for maximum compatibility. We know
Microsoft C was used to develop the game. Turbo C was famous at the time
as the cheap alternative. And Watcom is what we have today that is free to
use and reasonably maintained.

They also differ in how the memory allocator works, which lets us stress
different paths and keep the older systems honest.

Concretely: Turbo C reaches the allocator through `farmalloc` where
Microsoft C uses `halloc`, and Watcom's strict ANSI mode hides the
unreserved spellings of `huge` and `stricmp`, so only the reserved ones
survive it. The 32-bit target has no huge pointers at all, which is what
keeps the `M_I86` guards in `buf.c` honest, and it is a useful place to
start if you are reading this code for the first time. `EGA.CMN` below
explains why any of it exists.

What we are after is period-correct software. `skidpack` is a recreation,
built from what we could work out about a tool that never left Distinctive
Software, and keeping these compilers building is what lets it run on a
machine of the era.

## Traps found during development

**Turbo C does not support Unix line endings (LF).** This is generally not
an issue, because [`.gitattributes`](.gitattributes) checks the C sources
out CRLF, but be advised. DOS is the primary target and Turbo C is the only
compiler here with this issue.

If you do want or need LF in your working tree, you must use `dos2unix` or
similar to convert locally.

## Code formatting

CI runs clang-format and commits the result, so an unformatted push is
automatically cleaned up. Run it yourself if you would rather not have your
tree go out of sync:

    make format

## Testing

    make check DIR=/path/to/releases    # verify against the manifest
    make sweep DIR=/path/to/releases    # round-trip anything, no manifest

The `DIR` variable holds one directory per release, named `st10`, `st11`,
`4d90` and `4d91`, or a single unpacked release. That way you can test
against every release at once, which may be more than you want, or against
one of them, such as `st11`, whose dialect is the default `bb11`. Releases
you leave out are counted as absent and the run still succeeds.

`check` verifies every row of [`test/corpus.txt`](test/corpus.txt) end to
end: the shipped file matches its recorded CRC-32, unpacks to the
recorded CRC-32, and repacks to the shipped bytes. The manifest also
records the mode, dialect and flags each file needs, and those are checked
too.

`sweep` needs no manifest and tries both dialects and both modes. Use it
on a release the manifest does not cover.

[`test/corpus.c`](test/corpus.c) is plain C89 code, so the tests run on
DOS too rather than having to be driven from a modern host. The batch
files do not build it by default, because someone who wants the tool
should not have to wait for the checker. Ask for it explicitly:

    MSCBUILD CORPUS
    TCBUILD CORPUS
    WCLBUILD CORPUS
    WCLBUILD 386 CORPUS

The same applies to the Unix-like builds. The makefile already works that
way: `make` builds the packer and `make check` builds the driver.

CI builds a shape container of its own and runs `p` (packing) and `u`
(unpacking) over it, which covers both directions and the refusal that
keeps a mod from being packed into something the game would transpose.

## What packing costs the game (be ready for the technicalities)

A packed 2D shape is unflipped after it is decompressed, into a scratch
buffer. A plain one takes the `file_load_binary` path and allocates
nothing. The two packed 2D forms do not size that buffer the same way,
and `.P3S` does not unflip at all.

`.PVS` measures the file. `file_get_unflip_size` walks every shape in the
container, reads the width and height out of each 16-byte `SHAPE2D`
header, and keeps the largest of

    paragraphs = (width * height + 0x20) >> 4

then allocates `paragraphs * 16` bytes. The cost is the biggest single
shape rounded up to a paragraph, plus two paragraphs of slack, and not
the size of the file or the number of shapes in it. A container of many
small shapes costs nothing extra; one oversized dashboard sets the cost
for the whole file. That is 22432 bytes for a stock dashboard, and a mod
one drawn larger can run to roughly twice that.

`.PES` measures nothing. The loader asks for a flat 1000 paragraphs,
16000 bytes, whatever the file holds. A tiny `.ESH` and a large one cost
the same, and a small one costs more packed as `.PES` than it would as
`.PVS`.

Both buffers are released as soon as the shape is unflipped, so the game
never holds two at once. `p` therefore reports the largest single
allocation a run would provoke rather than the total of all of them,
which would describe a volume nothing has to satisfy at one time.

`v` reports the same figure for a single file and writes nothing, which is
how you ask a car that already ships packed:

    skidpack v STDACOUN.PVS

    STDACOUN.PVS: OK - tree consistent with its payload (MSB-first)
    STDACOUN.PVS: 22432 bytes of scratch to load

`u` prints the figure too, but only for a file it actually unpacks, so it
says nothing when the plain twin is already there, and nothing for a
dashboard it refuses over the flip flag.

### What is actually known

We know the buffer exists and how it is sized. Whether it ever overflows,
we do not know yet.

A packed mod car loads, draws, races against an opponent, and reaches the
results screen. We have also played one for minutes at a time without
complaint. In those runs it was stable.

## The curious case of `EGA.CMN`:

`EGA.CMN` is the reason this codebase looks the way it does on a 16-bit
host.

It is 147456 bytes unpacked in Stunts 1.0, and `SDOSEL.PVS` unpacks past
109 KB. Both are larger than a 16-bit segment, so neither the input nor
the output buffer can be addressed by a plain `far` pointer, whose offset
wraps at 64 KB and silently starts overwriting the beginning of its own
segment.

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

If you only ever build on a modern host you reach none of this. `RS_HUGE`
expands to nothing, `rs_size` is merely a wide integer, and the whole
chapter is invisible. It only exists because one 147456-byte file shipped
in the original release.

## Format

We based the container and tree layout on the [Stunts
wiki](https://wiki.stunts.hu/wiki/Compression). What it does not cover,
the RLE escape table and how a packer chooses one, we describe in the code
comments.

Two things differ from the Wiki. The maximum Huffman depth is actually 15
and not 16: the game's level-offset accumulator is an `unsigned short`, and
at depth 16 the bound it compares against wraps to zero. Much like the
classic joke about arrays starting at 0. And the packed and plain forms of
a 2D shape are not always the same bytes, because the packed loader runs an
unflip pass that the plain one does not.

### `.XVS` support

The loader probes extensions in order, and `.XVS` is not used by anything
that ships:

    .PVS  decompress, allocate scratch, unflip, return
    .XVS  decompress, return
    .VSH  load plain, return

`.XVS` is a compressed `.VSH`. It takes the same container `.PVS` does and
skips both the unflip pass and the buffer that pass needs, so it compresses
exactly as well while allocating nothing.

The string table

    .PVS\0.XVS\0.VSH\0

appears byte for byte in all sixteen code overlays, four video modes across
four releases, `.XVS` always ahead of `.VSH`. Decompress `CGA.COD`,
`EGA.COD`, `MCGA.COD` and `TDY.COD` and look for yourself.

It works in the game, tested in MCGA on cars whose dashboards were present
only as `.XVS`. They render correctly and cost no scratch at all.

`p` writes `.PVS` anyway, because that is what published mods use. If you
change that, `.XVS` is wrong for a shape claiming a flip, since nothing
would transpose it, so the check guarding `.PVS` guards this too. And not
every video mode and release pairing has been tried in the game.

### No published modded car supports the pre-VGA video modes

A dashboard is a container of named resources, four characters each,
readable at offset 6:

    !cg0 !eg0 dash gbox inm1 inm3 ins1 ins2 ins3 roof whl1 whl2 whl3

`!cg0` and `!eg0` are 256-entry tables that convert the artwork down for
CGA and EGA. MCGA reads the artwork directly and needs neither. Stock cars
carry tables cut for their own picture. Of all the modded cars we tested,
none shipped support for EGA or the older modes. A tool to convert them
automatically could be made, but that is an enhancement for another update.

### Why a `.VSH` or `.ESH` is sometimes refused

`file_unflip_shape2d` transposes a shape whose header asks for it, and it
runs only on the packed path. So for a shape marked that way the plain
bytes and the packed bytes are not the same picture, and packing it would
change what the game draws. `sk_modpack_flip_safe` reads the flag out of
the container and the file is skipped when any shape in it claims a flip.
`.3SH` has no such pass and is always safe.

## Releasing

`SK_VERSION` in [`src/version.h`](src/version.h) is the only place a
version appears. The banner reads it, and so does `/V`.

We build the binaries with Open Watcom and only with Open Watcom. It is the
one toolchain here whose licence allows shipping what it produces for DOS.

    WCLBUILD          16-bit DOS   runs on anything, needs nothing
    WCLBUILD 386      32-bit DOS   needs a 386, extender built in
    WCLBUILD WIN32    Win32        imports KERNEL32 and USER32

There are two ways to check a download. `SHA256SUMS` covers the archives
for anyone on a modern machine, and the ZIP format already stores a
CRC-32 per member, the same one PKZIP shipped in 1989 and the same one
`test/corpus.txt` uses. So a reader on DOS with no SHA-256 tool checks
the contents with

    PKUNZIP -T SKIDPK10.ZIP

### All binaries are reproducible build

Both DOS builds are byte for byte reproducible on their own.

Watcom fills in a `TimeDateStamp` on a Win32 executable. It sits at
offset 8 from the PE signature, which is itself found through `e_lfanew`
at `0x3C`. We zero it after linking, which makes that build deterministic
too.
