@echo off
rem build.cmd - build XHCISNAP.EXE with the in-repo MSVC 6.0.
rem
rem Nothing is installed machine-wide: the compiler is used in place from
rem tools\MSVC600, found relative to this script, exactly as the driver build
rem and the host tests do.  MSVC6 overrides the location.
rem
rem The result is a Win32 console EXE that must run on Windows 98 SE, so the
rem source is ANSI-only, C89, and calls nothing newer than Win95.
rem
rem /Za is NOT used here, and that is not an oversight: the SDK headers this
rem compiler ships with are themselves full of anonymous unions and structs,
rem which /Za rejects, so windows.h does not compile under it.  The driver and
rem the host tests keep /Za because they include no SDK header.  /WX still
rem makes a warning a build failure, and the source is written to the same C89
rem rules by hand.
setlocal
cd /d "%~dp0"

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
if "%MSVC6%"=="" set "MSVC6=%REPO%\tools\MSVC600"
if not exist "%MSVC6%\VC98\Bin\cl.exe" (
    echo ERROR: no cl.exe under %MSVC6%\VC98\Bin
    echo        Unpack tools\MSVC600.zip, or set MSVC6.
    exit /b 1
)

rem MSPDB60.DLL lives under Common\MSDev98\Bin, not VC98\Bin; without it cl
rem exits 53 with no message at all.
set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"

rem advapi32.lib is for the Reg* half (task 13-L.2): the tool finds the driver's
rem own keys and sets XhciLogVerbosity in each of them from ring 3, which is the
rem whole reason `regedit` appears nowhere in the four steps a user runs. (This
rem comment said "three registry values" until the snapshot-value merge: the INF places TWO,
rem and the tool writes only the one.) It is not in the default
rem library set, and without it the link fails on five __imp__Reg* symbols.
rem Every one of those APIs is Win95-era, so this costs the Windows 98 SE
rem target nothing.
if exist XHCISNAP.EXE del XHCISNAP.EXE
cl /nologo /W3 /WX /O2 /FeXHCISNAP.EXE xhcisnap.c advapi32.lib /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
if not exist XHCISNAP.EXE exit /b 1

del /q xhcisnap.obj 2>nul
echo.
echo Built XHCISNAP.EXE
exit /b 0
