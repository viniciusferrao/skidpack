@echo off
rem Build skidpack as a 16-bit DOS program with Turbo C 2.01.
rem
rem Microsoft C is the period-plausible toolchain for this codec; see
rem MSCBUILD.BAT. Turbo C is here as a second opinion. It reaches the codec
rem through a different allocator, farmalloc instead of halloc, and that branch
rem of src/buf.c is compiled nowhere else.
rem
rem Turbo C miscompiles a source file with Unix line endings. It emits a nearly
rem empty object and reports no error, so the failure surfaces much later as a
rem pile of undefined externals at link time. The sources are stored with LF.
rem Convert them to CRLF before running this.
rem
rem Set TCDIR to your installation before running, or edit the default here.

rem The test driver is opt-in: TCBUILD CORPUS builds it too.
set SKCORPUS=
if "%1"=="CORPUS" set SKCORPUS=1
if "%1"=="corpus" set SKCORPUS=1

if not "%TCDIR%"=="" goto havedir
set TCDIR=C:\TC
:havedir

rem Look before starting; see MSCBUILD.BAT for why.
if exist %TCDIR%\BIN\TCC.EXE goto havetcc
echo TCBUILD: no TCC.EXE under %TCDIR%\BIN
echo Set TCDIR to your Turbo C 2.01 installation.
goto end
:havetcc
if exist %TCDIR%\LIB\CL.LIB goto havelib
echo TCBUILD: no CL.LIB under %TCDIR%\LIB
echo The large model libraries are missing from this installation.
goto end
:havelib

set PATH=%TCDIR%\BIN;%PATH%
set INCLUDE=%TCDIR%\INCLUDE
set LIB=%TCDIR%\LIB

cd src

rem -ml selects the large model, matching the Microsoft build.
rem
rem -c matters. Without it TCC links as well as compiles, and the link it builds
rem for six objects overruns the DOS command line.
rem
rem No wildcard expansion in argv either: Turbo C 2.01 ships no WILDARGS.OBJ,
rem the counterpart of the SETARGV.OBJ the Microsoft build links. `skidpack p`
rem therefore wants its files named one by one here, or the Microsoft build.
tcc -c -ml -I%TCDIR%\INCLUDE buf.c vle.c rle.c cli.c sdtitl.c modpack.c glob.c main.c
if errorlevel 1 goto fail

rem TLINK is driven from a response file because the object list and the two
rem library paths together pass 127 characters, and DOS truncates there.
rem
rem The format is not obvious: a newline REPLACES the comma between fields, it
rem does not accompany one. Write both and the fields in between come out empty,
rem which links the startup object alone and leaves every symbol undefined.
rem
rem The four fields are objects, executable, map, libraries. nul discards the
rem map. /c makes the link case sensitive, matching the compiler.
echo /c %TCDIR%\LIB\C0L.OBJ buf.obj vle.obj rle.obj cli.obj sdtitl.obj modpack.obj glob.obj main.obj> tclink.rsp
echo ..\skidpack.exe>> tclink.rsp
echo nul>> tclink.rsp
echo %TCDIR%\LIB\CL.LIB %TCDIR%\LIB\MATHL.LIB>> tclink.rsp
tlink @tclink.rsp
if errorlevel 1 goto fail

del tclink.rsp
del *.obj
cd ..

if not "%SKCORPUS%"=="1" goto packonly

rem The test driver, so the corpus can be checked here rather than from a modern
rem host. No wildcard expansion in argv: Turbo C 2.01 ships no WILDARGS.OBJ, the
rem counterpart of the SETARGV.OBJ the Microsoft build links. Name files
rem individually, or use the Microsoft build for sweeps.
cd test
tcc -c -ml -I%TCDIR%\INCLUDE corpus.c
if errorlevel 1 goto fail
echo /c %TCDIR%\LIB\C0L.OBJ corpus.obj> tclink.rsp
echo ..\corpus.exe>> tclink.rsp
echo nul>> tclink.rsp
echo %TCDIR%\LIB\CL.LIB %TCDIR%\LIB\MATHL.LIB>> tclink.rsp
tlink @tclink.rsp
if errorlevel 1 goto fail
del tclink.rsp
del *.obj
cd ..

echo Built skidpack.exe and corpus.exe
rem
rem This build handles the whole corpus, the same 314 resources the Microsoft
rem build reproduces.
goto end

:packonly
echo Built skidpack.exe
echo Run TCBUILD CORPUS to build the test driver as well.
goto end

:fail
cd ..
echo Build failed.

:end
