Bit perfect packer and unpacker for the DSI resource container used by Stunts
and 4D Sports Driving. It reproduces all 314 shipped resources across the four
releases byte for byte, and packs mod shapes the game already knows how to load.

### Download

| file | for |
|---|---|
| `SKIDPK10.ZIP` | MS-DOS. `SKIDPACK.EXE` runs on anything; `SKID386.EXE` wants a 386 and carries its own extender. |
| `skidpack-1.0-win32.zip` | Windows 95 and later, from the command prompt. |

Both are built with Open Watcom 1.9, whose licence plainly allows shipping what
it produces. No Unix binary: `make` with any C89 compiler, and there is nothing
to configure.

### Packing a mod

    skidpack p *.VSH *.3SH      # pack, writing the twin beside each file
    skidpack u *.PVS *.P3S      # unpack it back again

Nothing is deleted or overwritten, a file that would not get smaller is left
alone, and everything written is unpacked again and compared before it is kept.
Across nine published cars this saved 71 percent, from 838375 bytes to 240747.

Packing is not quite free. A packed resource costs a little memory to load
that a plain one does not, and `p` reports it as `SCRATCH`. Whether that is
ever enough to matter is not known; `DEVELOP.md` says what was tried.

### Verifying

`SHA256SUMS` covers both archives. The ZIP format also stores a CRC-32 per
member, the same one PKZIP shipped in 1989, so a reader on DOS checks the
contents with no modern tool at all:

    PKUNZIP -T SKIDPK10.ZIP

### Credits

[stunpack](https://github.com/dstien/stunpack) by Daniel Stien as the reference
decompressor, [restunts](https://github.com/4d-stunts/restunts) for recording
the format as the game implements it, and the
[ZakStunts](https://zak.stunts.hu) and [Stunts Forum](https://forum.stunts.hu)
communities for the cars that gave this a reason to exist.
