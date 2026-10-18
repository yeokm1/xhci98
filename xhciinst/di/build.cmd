@echo off
rem build.cmd - build XHCIDI.EXE, the Windows 98 SE and ME device install
rem helper (roadmap-usb4.md task 42-A.1; design record 27 section 3.2), a
rem Win16 program, with Open Watcom 2.0 (C:\WATCOM, or WATCOM) against the
rem Windows 98 SE DDK's setupx.h and setupx.lib. Nothing is installed
rem machine-wide.
rem
rem   build.cmd      XHCIDI.EXE and XHCIDI.MAP
rem
rem Exit code 0 = built; 1 = a build or DDK check failed; 2 = not built,
rem because this host has no Open Watcom (xhciinst\build.cmd says so and
rem goes on: a package without the helper installs on 98 SE and ME through
rem the printed steps only, and make-release.ps1 refuses to cut one unless
rem told -SkipDiHelper).
rem
rem THE HEADERS. stage-ddk.ps1 holds the DDK's inc16\setupx.h, inc16\prsht.h,
rem inc16\windows.h and setupx.lib to the SHA-256s docs\references\README.md
rem records and copies them into obj\ under the names the sources include.
rem INCLUDE is obj\inc first and Open Watcom's h second, and h\win not at
rem all: setupx.h needs HKEY and prsht.h NMHDR, which the DDK's 98 windows.h
rem declares and Watcom's (the Windows 3.1 one) does not, so the DDK's
rem windows.h is the one compiled against and Watcom's h supplies only the C
rem runtime's headers. That is the include order record 27 section 3.2
rem leaves to the build.
rem
rem The compiler is found in BINNT64, BINNT, then BINW under WATCOM, the
rem order xhciqual\build.cmd sets on PATH; wlink reads the system windows
rem definition from the wlink.lnk beside it.
setlocal
cd /d "%~dp0"
if "%WATCOM%"=="" set "WATCOM=C:\WATCOM"
set "WBIN="
for %%D in (BINNT64 BINNT BINW) do (
    if not defined WBIN if exist "%WATCOM%\%%D\wcc.exe" set "WBIN=%WATCOM%\%%D"
)
if not defined WBIN (
    echo XHCIDI.EXE was not built: no Open Watcom 16-bit compiler ^(wcc.exe^) under %WATCOM%.
    exit /b 2
)
set "PATH=%WBIN%;%WATCOM%\BINW;%PATH%"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0stage-ddk.ps1"
if errorlevel 1 exit /b 1

set "INCLUDE=%~dp0obj\inc;%WATCOM%\H"
if exist xhcidi.exe del xhcidi.exe
if exist xhcidi.map del xhcidi.map
del /q *.obj 2>nul
wmake -h -f makefile
if errorlevel 1 exit /b 1
if not exist xhcidi.exe exit /b 1
del /q *.obj 2>nul
echo Built XHCIDI.EXE
exit /b 0
