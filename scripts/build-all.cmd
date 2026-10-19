@echo off
rem build-all.cmd - one command for everything the tree's drivers need built:
rem
rem   1. scripts\build-driver.cmd <flavours> for x86, with every gate
rem   2. scripts\build-driver.cmd <flavours> -amd64, with every gate
rem
rem Since 3.1.0.0 (roadmap-usb4 task 42-A.30) build-driver.cmd builds and
rem gates Stor98 (src\stor98) too - stor98.mpd and the x86 stor98.sys on the
rem x86 run, after its unit tests, and the amd64 stor98.sys on the amd64 run -
rem and the package bundles that build, so there is no separate Stor98 step
rem here any more. Stor98's checked and debug-port builds are still run by
rem hand (src\stor98\docs\BUILD.md).
rem
rem build-driver.cmd is called unchanged, so its gates, its lock and its
rem gated-build records are exactly those of a run by hand.
rem
rem Usage:  scripts\build-all.cmd [release|debug|qemu|both|all]
rem                               [-NoTargetEvidence]
rem                                                     (default: both)
rem
rem   -NoTargetEvidence  passed to both build-driver.cmd runs
rem
rem Any failure stops the run with build-driver.cmd's own exit code (1, or
rem 2 for inconclusive host tests).

setlocal
for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "FLAVORS=%~1"
if "%FLAVORS%"=="" set "FLAVORS=both"
set "GATEOPT="
set "BADOPT="
call :readopt "%~2"
call :readopt "%~3"
if defined BADOPT (
    echo ERROR: unknown option "%BADOPT%" - use -NoTargetEvidence
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
set "BADOPT=%~1"
exit /b 0
