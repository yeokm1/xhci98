@echo off
rem build.cmd - build WAVCHK.EXE, the release legs' playback check
rem (wavchk.c says what it does; build-and-test.md, "Faster emulator legs").
rem The EXE is not tracked and in no package: run-leg.ps1 copies it onto
rem the leg's CD for a leg whose Audio is set, and refuses such a leg until
rem it is built. A 32-bit console image with the in-repo MSVC 6.0
rem (tools\MSVC600; MSVC6 overrides it), /W3 /WX, the C runtime linked
rem statically, importing KERNEL32 and WINMM alone; starts on Windows 98 SE
rem to 11.
setlocal
cd /d "%~dp0"

set "REPO=%~dp0..\..\.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
if "%MSVC6%"=="" set "MSVC6=%REPO%\tools\MSVC600"
if not "%~1"=="" (
    echo ERROR: unknown argument %1 ^(none^)
    exit /b 1
)
if not exist "%MSVC6%\VC98\Bin\cl.exe" (
    echo ERROR: no cl.exe under %MSVC6%\VC98\Bin
    echo        Unpack tools\MSVC600.zip, or set MSVC6.
    exit /b 1
)

set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"

if exist WAVCHK.EXE del WAVCHK.EXE
cl /nologo /W3 /WX /O2 /ML /FeWAVCHK.EXE wavchk.c winmm.lib /link /SUBSYSTEM:CONSOLE,4.0
if errorlevel 1 exit /b 1
if not exist WAVCHK.EXE exit /b 1
del /q wavchk.obj 2>nul
echo Built WAVCHK.EXE
exit /b 0
