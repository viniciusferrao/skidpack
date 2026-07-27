@echo off
rem Build skidpack as a 16-bit DOS program with Microsoft C 5.10.
rem
rem Microsoft C 5.10 is what the shipped executables fingerprint to, so it is
rem the most likely compiler behind them. skidpack itself is a recreation, not
rem recovered code, and nothing here came from Distinctive Software. Building it
rem on that compiler keeps the toolchain period-plausible and lets the whole
rem workflow run on a machine of the era.
rem
rem Set MSCDIR to your installation before running, or edit the default here.
rem TMP must exist and be writable: CL and LINK both spill to it.

rem The test driver is opt-in: MSCBUILD CORPUS builds it too. Someone who
rem only wants the tool should not wait for the checker.
set SKCORPUS=
if "%1"=="CORPUS" set SKCORPUS=1
if "%1"=="corpus" set SKCORPUS=1

if not "%MSCDIR%"=="" goto havedir
set MSCDIR=M:
:havedir

rem Look before starting. Without this a wrong MSCDIR produces seven "Bad
rem command or file name" lines and then a link that fails for its own reasons,
rem and the cause is the first thing that scrolled away. Both halves are worth
rem checking separately: CL is the install, LLIBCR is the one library the link
rem line names, and an installation carrying LLIBCE instead gets all the way to
rem the link before saying so.
if exist %MSCDIR%\BIN\CL.EXE goto havecl
echo MSCBUILD: no CL.EXE under %MSCDIR%\BIN
echo Set MSCDIR to your Microsoft C 5.10 installation.
goto end
:havecl
if exist %MSCDIR%\LIB\LLIBCR.LIB goto havelib
echo MSCBUILD: no LLIBCR.LIB under %MSCDIR%\LIB
echo The link line names it; an installation with LLIBCE needs that name here.
goto end
:havelib

set PATH=%MSCDIR%\BIN;%PATH%
set INCLUDE=%MSCDIR%\INCLUDE
set LIB=%MSCDIR%\LIB
if "%TMP%"=="" set TMP=C:\

cd src

rem /AL selects the large model. Headroom rather than a requirement: the
rem buffers that hold a resource come from halloc are far in any model.
cl /c /AL /W3 buf.c
if errorlevel 1 goto fail
cl /c /AL /W3 vle.c
if errorlevel 1 goto fail
cl /c /AL /W3 rle.c
if errorlevel 1 goto fail
cl /c /AL /W3 cli.c
if errorlevel 1 goto fail
cl /c /AL /W3 sdtitl.c
if errorlevel 1 goto fail
cl /c /AL /W3 modpack.c
cl /c /AL /W3 glob.c
if errorlevel 1 goto fail
cl /c /AL /W3 main.c
if errorlevel 1 goto fail

rem Three parts of the link are not guessable.
rem
rem   /NOD    kills the default library request. CL writes a request for
rem           LLIBCE.LIB into each object; an installation that ships LLIBCR.LIB
rem           instead cannot link until the explicit list is made to win.
rem
rem   LIBH    supplies __aFlmul, __aFuldiv and __aFulmul, the 32-bit helpers
rem           that huge-pointer arithmetic compiles down to. Leave it out and
rem           the link stops on three unresolved externals that name no source
rem           line and suggest nothing.
rem
rem   /STACK  the default is 2 KB, small enough that one 4 KB automatic buffer
rem           overflows it before main() reaches its first statement, which DOS
rem           reports as a bare abnormal exit. Large tables in this codec are
rem           static for the same reason, which is also why it is
rem           single-threaded.
rem
rem SETARGV.OBJ makes the C runtime expand wildcards in argv, which COMMAND.COM
rem does not do for you. `p` is the mode that wants it: it takes a list of
rem files, so `skidpack p *.VSH *.3SH` should behave here the way the same
rem line does under a Unix shell. The other modes take one named file and are
rem indifferent to it.
rem
rem   /NOE    goes with SETARGV. The C library carries its own __setargv, a stub
rem           that does nothing, and linking the real one alongside it is
rem           "L2044: symbol multiply defined" until the extended dictionary
rem           search is off. LINK says so itself and then writes an executable
rem           anyway, which is worse than refusing: the object that wins is
rem           whichever the dictionary reached first.
rem
rem The libraries are named bare rather than by full path deliberately: LINK
rem finds them through LIB, and the DOS command line stops at 127 characters.
link /NOI /NOD /NOE /STACK:8192 buf+vle+rle+cli+sdtitl+modpack+glob+main+%MSCDIR%\LIB\SETARGV,..\skidpack.exe,,LLIBCR+LIBH+EM;
if errorlevel 1 goto fail

del *.obj
cd ..

if not "%SKCORPUS%"=="1" goto packonly

rem The test driver, so the corpus can be checked here rather than from a
rem modern host. SETARGV.OBJ makes the C runtime expand wildcards in argv,
rem which COMMAND.COM does not do for you, so `corpus sweep skidpack *.PVS`
rem behaves the way the same line does under a Unix shell.
cd test
cl /c /AL /W3 corpus.c
if errorlevel 1 goto fail
link /NOI /NOD /NOE /STACK:8192 corpus+%MSCDIR%\LIB\SETARGV,..\corpus.exe,,LLIBCR+LIBH+EM;
if errorlevel 1 goto fail
del *.obj
cd ..

echo Built skidpack.exe and corpus.exe
rem
rem This build handles the whole corpus: all 314 compressed resources in the
rem four releases unpack and repack to the shipped bytes, the largest being
rem st10's EGA.CMN at 147456 bytes decompressed. Getting there meant not
rem holding several copies of a resource at once; see rs_decomp in rle.c.
goto end

:packonly
echo Built skidpack.exe
echo Run MSCBUILD CORPUS to build the test driver as well.
goto end

:fail
cd ..
echo Build failed.

:end
