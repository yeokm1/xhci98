@echo off
rem build.cmd - build selective suspend's test client (roadmap-hcd.md task
rem 36.34): XHCIPM.SYS with the in-tree Windows 2000 DDK (x86, checked, so
rem its DbgPrint lines reach a debugger) and PMCTL.EXE with the in-tree
rem MSVC 6.0, as test\xfertest\build.cmd builds XFERTEST.EXE. Output: this
rem directory's out\ (XHCIPM.SYS, XHCIPM.INF, PMCTL.EXE), copied onto a
rem guest's transfer drive for a leg.
rem
rem TEST ONLY, NEVER SHIPPED: not tracked as a binary, in no package, and
rem scripts\package\make-release.ps1 refuses a release that carries any of
rem the three or the marker XHCI98_TEST_CLIENT_DO_NOT_SHIP.
setlocal
cd /d "%~dp0"

set "REPO=%~dp0..\.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
set "DDK=%REPO%\tools\ntddk"
for %%I in ("%DDK%") do set "DDK=%%~sfI"
if "%MSVC6%"=="" set "MSVC6=%REPO%\tools\MSVC600"
if not exist "%DDK%\bin\setenv.bat" (
    echo ERROR: no Windows 2000 DDK under %DDK%
    exit /b 1
)
if not exist "%MSVC6%\VC98\Bin\cl.exe" (
    echo ERROR: no cl.exe under %MSVC6%\VC98\Bin
    exit /b 1
)
if not exist out mkdir out
if exist out\XHCIPM.SYS del out\XHCIPM.SYS
if exist out\PMCTL.EXE del out\PMCTL.EXE

if exist objchk\i386\xhcipm.sys del objchk\i386\xhcipm.sys
if exist buildchk.err del buildchk.err
cmd /c "set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%" && call "%DDK%\bin\setenv.bat" %DDK% checked && cd /d "%~dp0" && build -cZ"
if errorlevel 1 (
    echo ERROR: the DDK build failed - see buildchk.log / buildchk.err
    exit /b 1
)
if exist buildchk.err (
    echo ERROR: the DDK build reported errors - see buildchk.err
    exit /b 1
)
if not exist objchk\i386\xhcipm.sys (
    echo ERROR: XHCIPM.SYS did not build - see buildchk.log / buildchk.err
    exit /b 1
)
copy /y objchk\i386\xhcipm.sys out\XHCIPM.SYS >nul
copy /y xhcipm.inf out\XHCIPM.INF >nul

cmd /c "set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%" && set "INCLUDE=%MSVC6%\VC98\Include" && set "LIB=%MSVC6%\VC98\Lib" && cl /nologo /W3 /WX /O2 /Feout\PMCTL.EXE pmctl.c /link /SUBSYSTEM:CONSOLE"
if errorlevel 1 exit /b 1
del /q pmctl.obj 2>nul
if not exist out\PMCTL.EXE exit /b 1
echo Built out\XHCIPM.SYS, out\XHCIPM.INF and out\PMCTL.EXE
exit /b 0
