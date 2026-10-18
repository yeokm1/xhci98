@echo off
rem build-all.cmd - one command for everything the tree's drivers need built:
rem
rem   1. scripts\build-driver.cmd <flavours> for x86, with every gate
rem   2. scripts\build-driver.cmd <flavours> -amd64, with every gate
rem   3. Stor98 (src\stor98, roadmap-usb4 task 42-A.20): its own
rem      scripts\build.cmd all free - stor98.mpd for 98 SE and ME and
rem      stor98.sys for x86 and amd64 - then its host unit tests
rem      (tests\unit\build.cmd). Its output stays in src\stor98\out\, which
rem      is git-ignored, and none of it reaches xhci98's own objects,
rem      records or packages: Stor98 is still its own set of files, bundled
rem      only from a pinned release folder (make-package.ps1 -Stor98*).
rem
rem build-driver.cmd is called unchanged, so its gates, its lock and its
rem gated-build records are exactly those of a run by hand. Stor98 is built
rem free only, the flavour its releases ship; its checked and debug-port
rem builds are run by hand (src\stor98\docs\BUILD.md).
rem
rem Usage:  scripts\build-all.cmd [release|debug|qemu|both|all]
rem                               [-NoTargetEvidence] [-NoStor98]
rem                                                     (default: both)
rem
rem   -NoTargetEvidence  passed to both build-driver.cmd runs
rem   -NoStor98          skip step 3
rem
rem Any failure stops the run with build-driver.cmd's own exit code (1, or
rem 2 for inconclusive host tests) or 1 for Stor98.

setlocal
for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "FLAVORS=%~1"
if "%FLAVORS%"=="" set "FLAVORS=both"
set "GATEOPT="
set "NOSTOR98="
set "BADOPT="
call :readopt "%~2"
call :readopt "%~3"
if defined BADOPT (
    echo ERROR: unknown option "%BADOPT%" - use -NoTargetEvidence and/or -NoStor98
    exit /b 1
)

echo.
echo ##### xhci98, x86 #####
call "%REPO%\scripts\build-driver.cmd" %FLAVORS% %GATEOPT%
if errorlevel 1 exit /b %errorlevel%

echo.
echo ##### xhci98, amd64 #####
call "%REPO%\scripts\build-driver.cmd" %FLAVORS% -amd64 %GATEOPT%
if errorlevel 1 exit /b %errorlevel%

if defined NOSTOR98 (
    echo.
    echo Stor98 skipped [-NoStor98].
    goto done
)

echo.
echo ##### Stor98 #####
call "%REPO%\src\stor98\scripts\build.cmd" all free
if errorlevel 1 (
    echo ERROR: the Stor98 build failed - see above.
    exit /b 1
)
echo.
echo ##### Stor98 unit tests #####
call "%REPO%\src\stor98\tests\unit\build.cmd"
if errorlevel 1 (
    echo ERROR: Stor98's unit tests failed - see above.
    exit /b 1
)

:done
echo.
echo BUILD-ALL PASSED (%FLAVORS%; x86 and amd64)
exit /b 0

:readopt
if "%~1"=="" exit /b 0
if /i "%~1"=="-NoTargetEvidence" (
    set "GATEOPT=-NoTargetEvidence"
    exit /b 0
)
if /i "%~1"=="-NoStor98" (
    set "NOSTOR98=1"
    exit /b 0
)
set "BADOPT=%~1"
exit /b 0
