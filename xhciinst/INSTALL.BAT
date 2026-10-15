@echo off
rem INSTALL.BAT - install, update or uninstall this package's drivers
rem (design record 23). It runs tools\xhciinst\XHCIINST.EXE (32-bit Windows)
rem or XHCIIN64.EXE (64-bit Windows) on this package's release-x86 or
rem release-x64 directory, and passes every argument on:
rem
rem   INSTALL            install or update, after one question
rem   INSTALL /list      show what would be done; change nothing
rem   INSTALL /uninstall remove the drivers (asks for the word REMOVE)
rem   INSTALL /thunderbolt  include the Thunderbolt 3 / USB4 support without
rem                      asking (otherwise it is asked)
rem   INSTALL /devices   show the connected Thunderbolt and USB4 devices
rem   INSTALL /eject     remove one of them safely (Windows 98 SE and ME:
rem                      the way to do it; 2000 and later: also for a
rem                      device Safely Remove Hardware does not list)
rem   INSTALL /debug     use the debug- directory (troubleshooting only)
rem
rem tools\xhciinst\readme.txt has the rest. ASCII, CRLF line endings: MS-DOS
rem 7.1's COMMAND.COM loses goto labels in a file with LF endings.
if "%OS%"=="Windows_NT" goto nt

rem Windows 98 SE and ME. COMMAND.COM has no %~dp0 and no %*: the tool is
rem found from the current directory, which is this file's own when it is
rem started from Explorer, and checks its own location itself.
if not exist TOOLS\XHCIINST\XHCIINST.EXE goto nodir
rem COMMAND.COM passes nine arguments at most (and splits Name=Value in two),
rem so a ninth argument may mean a tenth was lost: refuse rather than run
rem a shortened command.
if not "%9"=="" goto toomany
set XDIR=release-x86
for %%A in (%1 %2 %3 %4 %5 %6 %7 %8 %9) do if "%%A"=="/debug" set XDIR=debug-x86
for %%A in (%1 %2 %3 %4 %5 %6 %7 %8 %9) do if "%%A"=="/DEBUG" set XDIR=debug-x86
TOOLS\XHCIINST\XHCIINST.EXE /dir %XDIR% %1 %2 %3 %4 %5 %6 %7 %8 %9
set XDIR=
goto end

:toomany
echo Too many arguments for INSTALL.BAT on this Windows (nine at most, and
echo /set Name=Value counts as three). Run TOOLS\XHCIINST\XHCIINST.EXE /dir
echo release-x86 with the same arguments instead, or use fewer /set.
goto end

:nodir
echo INSTALL.BAT must be run from the package's top directory, the one that
echo holds release-x86 and tools. Change to that directory and run it again,
echo or double-click it in Explorer.
goto end

:nt
setlocal
set "XEXE=XHCIINST.EXE"
set "XARCH=x86"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "XEXE=XHCIIN64.EXE"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "XARCH=x64"
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "XEXE=XHCIIN64.EXE"
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "XARCH=x64"
set "XDIR=release-%XARCH%"
for %%A in (%*) do if /i "%%~A"=="/debug" set "XDIR=debug-%XARCH%"
if not exist "%~dp0tools\xhciinst\%XEXE%" (
    echo %~dp0tools\xhciinst\%XEXE% is missing. Unzip the whole package and
    echo run INSTALL.BAT from its top directory.
    exit /b 1
)
"%~dp0tools\xhciinst\%XEXE%" /dir %XDIR% %*
exit /b %ERRORLEVEL%

:end
