# skidpack

Bit perfect packer and unpacker for the DSI resource container used by the
Stunts / 4D Sports Driving MS-DOS game from 1990.

The compressor was an internal tool at Distinctive Software (DSI) and
no copy has ever surfaced, so `skidpack` is a recreation of what it may have
been, including the `SDTITL.PVS` [bug](src/sdtitl.c) that you can reproduce
with the option `--sdtitl`.

| release | files | shipped | unpacked | saved |
|---|---|---|---|---|
| Stunts 1.0 | 77 | 1116066 | 2537956 | 56% |
| Stunts 1.1 | 79 | 1110757 | 2513445 | 56% |
| 4D Sports Driving 1990 | 79 | 1169624 | 2625856 | 55% |
| 4D Sports Driving 1991 | 79 | 1110979 | 2563128 | 57% |
| **total** | **314** | **4507426** | **10240385** | **56%** |

`skidpack` is able to generate every one of those 314 files byte for byte.
It also checks a file's Huffman tree against the data that tree
encodes, which catches bit rot that unpacking alone would not.

## Use

    skidpack -h|--help       # on Unix
    skidpack /?              # on DOS and Windows

## Packing a mod

The game picks a loader from the file's extension and looks for the packed one
first: `.P3S` before `.3SH`, `.PVS` before `.VSH`. So a mod ships its shapes
plain, which is how we have been publishing cars without a compressor.

    skidpack p *.VSH *.3SH      # pack, writing the twin beside each file
    skidpack u *.PVS *.P3S      # unpack it back again

No file is deleted or overwritten. A file is skipped when it has no packed
form, when the packed one already exists, when packing would not make it
smaller after the overhead, or when it cannot be packed safely. Every file
written is unpacked again and compared before it is kept.

### What it saves

As an experiment we picked some modified cars published for Stunts by
different authors and measured what packing them saves, as shown in the table:

| car | by | files | plain | packed | saved |
|---|---|---|---|---|---|
| DAF Siluro Turbo Spec 1.0 | Overdrijf | 3 | 169324 | 32434 | 81% |
| Fiat Uno, Ladder Edition | Erik Barros | 3 | 120378 | 29684 | 75% |
| Ikarus 260 rev2 | CTG | 3 | 71464 | 18829 | 74% |
| Caterham Super Seven JPE v1.1m | Zapper | 3 | 112884 | 30916 | 73% |
| Peugeot Oxia, 2024-12-24 | Ryoma | 3 | 111440 | 35309 | 68% |
| Speedgate XSD rev3 | Mark L. Rivers | 2 | 59487 | 19875 | 67% |
| Ford Ranger v1.6.1 | cody | 2 | 64595 | 21674 | 66% |
| Nissan Skyline R32 GT-R v1.1.1 | Duplode | 3 | 64089 | 23048 | 64% |
| Melange XGT-88 v1.0 | Alan Rotoi | 3 | 64714 | 28978 | 55% |
| **total** | | **25** | **838375** | **240747** | **71%** |

Results are pretty good, and worth it if we are targeting old systems with
little to no disk capacity.

Community developed cars can be downloaded from the
[Stunts Car Repository](https://scr.stunts.hu/mods.html).

### What it costs

Packing is not quite free. The game unpacks a 2D shape through a scratch
buffer that a plain one does not need, so a packed car asks for more memory
while it loads. Each buffer is freed as soon as its shape is unflipped, so
what costs you is the largest single one, and `p` reports that as `SCRATCH`.

You can also ask a file that is already packed, without unpacking it:

    skidpack v STDACOUN.PVS

    STDACOUN.PVS: OK - tree consistent with its payload (MSB-first)
    STDACOUN.PVS: 22432 bytes of scratch to load

For more information check the "What is actually known" part of
[DEVELOP.md](DEVELOP.md).

## Build

If you want to build the software yourself, we provide a batch file per tested
compiler and a `makefile` for Unix-like systems. Working on the code rather
than using it? See [DEVELOP.md](DEVELOP.md).

### Unix

    make

Any C89 compiler. `CC` and `CFLAGS` override the defaults.

### DOS

    MSCBUILD          Microsoft C 5.10, 16-bit, large model
    TCBUILD           Turbo C 2.01, 16-bit, large model
    WCLBUILD          Open Watcom 1.9, 16-bit
    WCLBUILD 386      Open Watcom 1.9, 32-bit, extender built in

Set `MSCDIR`, `TCDIR` or `WATCOM` to your installation. If unset, the script
looks for the compiler in the default location.

### Windows

    WCLBUILD WIN32    Open Watcom 1.9, Win32 console

Runs on Windows 95 and later, from the command prompt.

Any modern toolchain should work with the Makefile. Example with MinGW-w64:

    mingw32-make CC=gcc

## Testing

    make check DIR=/path/to/releases

The test suite verifies every one of the shipped resources from all versions,
end to end. But you have to provide the data: the manifest only holds its
checksums. See [DEVELOP.md](DEVELOP.md) for more details.

## Credits

- [stunpack](https://github.com/dstien/stunpack) by Daniel Stien: reference
  decompressor.
- [restunts](https://github.com/4d-stunts/restunts): `fileio.c` records the
  format as the game implements it.
- [w4kfu/Stunts](https://github.com/w4kfu/Stunts): a `comp` tool for this
  format, used as inspiration.

## Acknowledgements

A massive thanks to all the members of the [ZakStunts](https://zak.stunts.hu)
community and the [Stunts Forum](https://forum.stunts.hu), who made all those
modifications. Without them there would be no reason to develop `skidpack`.

## Licence

MIT. See [LICENSE](LICENSE).
