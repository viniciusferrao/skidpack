@echo off
rem Build skidpack with Open Watcom 1.9.
rem
rem The other two batch files each build one thing. This one reaches all three
rem targets from the same source, which is what makes it useful: the 16-bit DOS
rem build and the 32-bit one disagree about pointers, and compiling both is how
rem the M_I86 guards in buf.c and skidpack.h stay honest.
rem
rem   WCLBUILD          16-bit DOS, large model, the default
rem   WCLBUILD 386      32-bit DOS, DOS/32A extender built into the exe
rem   WCLBUILD WIN32    Win32 console
rem
rem The last one is spelled WIN32 rather than NT, which is what Watcom calls the
rem switch. -bt=nt names the compiler's target, not the operating system that
rem has to run the result: the binary it writes is marked subsystem version 4.0
rem and runs on 95, 98 and ME as readily as on NT. Naming the target NT here
rem would tell the reader the opposite. NT is still accepted, since anyone who
rem knows the Watcom switch will type it.
rem
rem Set WATCOM to your installation before running, or edit the default here.

rem The test driver is opt-in. CORPUS may stand alone, for the default 16-bit
rem target, or follow one: WCLBUILD CORPUS, WCLBUILD 386 CORPUS.
set SKCORPUS=
if "%1"=="CORPUS" set SKCORPUS=1
if "%1"=="corpus" set SKCORPUS=1
if "%2"=="CORPUS" set SKCORPUS=1
if "%2"=="corpus" set SKCORPUS=1

if not "%WATCOM%"=="" goto havedir
set WATCOM=C:\WATCOM
:havedir

rem The DOS-hosted tools are in BINW and the Windows-hosted ones in BINNT. Only
rem cmd.exe sets OS, so this picks the set that can actually run. A 16-bit BINW
rem tool does not start on 64-bit Windows at all.
set WCLBIN=%WATCOM%\BINW
if "%OS%"=="Windows_NT" set WCLBIN=%WATCOM%\BINNT

rem Look before starting; see MSCBUILD.BAT for why. WCL is checked rather than
rem WCL386 because the default target is the 16-bit one, and the headers are
rem checked separately: WATCOM pointing at a tools-only tree gets through every
rem compile line before the first #include fails.
if exist %WCLBIN%\WCL.EXE goto havewcl
echo WCLBUILD: no WCL.EXE under %WCLBIN%
echo Set WATCOM to your Open Watcom 1.9 installation.
goto end
:havewcl
if exist %WATCOM%\H\STDIO.H goto haveinc
echo WCLBUILD: no STDIO.H under %WATCOM%\H
echo This installation has tools but no headers.
goto end
:haveinc

set PATH=%WCLBIN%;%PATH%
set INCLUDE=%WATCOM%\H

rem wcl expands a wildcard itself but hands wcc the bare file name, so
rem `wcl src\*.c` compiles the first file and then cannot find the second.
rem Building from inside src is what works; -fe puts the executable back up top.
cd src

if "%1"=="386" goto dos32
if "%1"=="WIN32" goto win32
if "%1"=="win32" goto win32
if "%1"=="NT" goto win32
if "%1"=="nt" goto win32

rem -ml is the large model, matching both other DOS builds. -za is strict ANSI,
rem which hides the unreserved spellings of huge and stricmp, so this is the
rem build that exercises the reserved ones in skidpack.h and cli.c.
wcl -bt=dos -ml -za -w3 *.c -fe=..\skidpack.exe
if errorlevel 1 goto fail
goto built

:dos32
rem A DOS target with a flat address space: no huge pointers and no halloc,
rem which Watcom declares inside `#if defined(_M_I86)`. That is why buf.c asks
rem what the pointer model is rather than whether the host is DOS. Without a
rem -l= the linker defaults to the host's own format and quietly produces
rem something that is not a DOS program at all.
rem
rem DOS/32A rather than DOS/4GW, and the difference matters for shipping. A
rem dos4g build is not a whole program: it needs DOS4GW.EXE beside it, and that
rem file is Tenberry's rather than Watcom's, so it cannot travel in a release
rem here. DOS/32A links its extender in as the stub, which makes the executable
rem self-contained and the release one file. The stub lives in BINW even when
rem the tools are the BINNT ones, so BINW goes on the end of PATH: the linker
rem finds the stub there, while wcl386 still resolves to the hosted build. Put
rem BINW first instead and a 64-bit Windows host tries to run the 16-bit
rem wcl386.exe, which it cannot load at all.
set PATH=%PATH%;%WATCOM%\BINW
wcl386 -bt=dos -l=dos32a -za -w3 *.c -fe=..\skidpack.exe
if errorlevel 1 goto fail
goto built

:win32
set INCLUDE=%WATCOM%\H;%WATCOM%\H\NT
wcl386 -bt=nt -za -w3 *.c -fe=..\skidpack.exe
if errorlevel 1 goto fail

:built
del *.obj
cd ..

if not "%SKCORPUS%"=="1" goto packonly

rem The test driver, so the corpus can be checked from the same build. It takes
rem the same target as the packer did, which is the point: a checker built for
rem one host cannot vet a packer built for another.
cd test
if "%1"=="386" goto tdos32
if "%1"=="WIN32" goto twin32
if "%1"=="win32" goto twin32
if "%1"=="NT" goto twin32
if "%1"=="nt" goto twin32
wcl -bt=dos -ml -za -w3 corpus.c -fe=..\corpus.exe
if errorlevel 1 goto fail
goto tbuilt

:tdos32
wcl386 -bt=dos -l=dos32a -za -w3 corpus.c -fe=..\corpus.exe
if errorlevel 1 goto fail
goto tbuilt

:twin32
wcl386 -bt=nt -za -w3 corpus.c -fe=..\corpus.exe
if errorlevel 1 goto fail

:tbuilt
del *.obj
cd ..

echo Built skidpack.exe and corpus.exe
goto end

:packonly
echo Built skidpack.exe
echo Add CORPUS to the command line to build the test driver as well.
goto end

:fail
cd ..
echo Build failed.

:end
