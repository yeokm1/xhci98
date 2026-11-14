@echo off
rem INSTALL.BAT - install, update or uninstall this package's drivers
rem (design records 23 and 26). To install or update, double-click SETUP.EXE
rem beside this file: it opens the installer's window with no console
rem behind it (to uninstall: Add/Remove Programs, or SETUP /uninstall).
rem INSTALL.BAT is for the command line and scripts
rem (INSTALL /console ...); run with no arguments it opens the same window,
rem and waits for it. With no /console it opens the installer's
rem window, tools\xhciinst\XHCIWIZ.EXE (32-bit Windows) or XHCIWZ64.EXE
rem (64-bit Windows); with /console as its first word it runs the console
rem tool, XHCIINST.EXE or XHCIIN64.EXE, on this package's folder for this
rem Windows (98-me, nt-x86 or nt-x64), and passes every other argument on:
rem
rem   INSTALL            the installer's window: install or update
rem   INSTALL /uninstall the window's uninstall (as Add/Remove Programs)
rem   INSTALL /console   the console tool, for scripts and the F6 floppy:
rem   INSTALL /console /list       show what would be done; change nothing
rem   INSTALL /console /uninstall  remove the drivers (the word REMOVE)
rem   INSTALL /console /yes /nopause /set XhciInterruptMode=2   unattended
rem   INSTALL /console /devices    the connected Thunderbolt and USB4
rem                                devices; /eject removes one safely
rem
rem tools\xhciinst\readme.txt has the rest. ASCII, CRLF line endings: MS-DOS
rem 7.1's COMMAND.COM loses goto labels in a file with LF endings.
if "%OS%"=="Windows_NT" goto nt

rem Windows 98 SE and ME. COMMAND.COM has no %~dp0 and no %*: the tools are
rem found from the current directory, which is this file's own when it is
rem started from Explorer, and check their own location themselves.
if not exist TOOLS\XHCIINST\XHCIINST.EXE goto nodir
rem COMMAND.COM passes nine arguments at most (and splits Name=Value in two),
rem so a ninth argument may mean a tenth was lost: refuse rather than run
rem a shortened command.
if not "%9"=="" goto toomany
set XDIR=98-me
rem COMMAND.COM compares case for case (no if /i): these three spellings.
if "%1"=="/console" goto console98
if "%1"=="/Console" goto console98
if "%1"=="/CONSOLE" goto console98
if not exist TOOLS\XHCIINST\XHCIWIZ.EXE goto nodir
rem COMMAND.COM does not wait for a Windows program: its ERRORLEVEL would
rem be its own, 0, while the window is still open. START /W waits for the
rem window and passes its exit code on.
start /w TOOLS\XHCIINST\XHCIWIZ.EXE %1 %2 %3 %4 %5 %6 %7 %8 %9
set XDIR=
goto end

:console98
TOOLS\XHCIINST\XHCIINST.EXE /dir %XDIR% %2 %3 %4 %5 %6 %7 %8 %9
set XDIR=
goto end

:toomany
rem The console tool says it and exits with 1: COMMAND.COM sets an
rem errorlevel only from a program, and a script reads that one.
TOOLS\XHCIINST\XHCIINST.EXE /toomany
set XDIR=
goto end

:nodir
echo INSTALL.BAT must be run from the package's top directory, the one that
echo holds 98-me and tools. Change to that directory and run it again,
echo or double-click it in Explorer.
goto end

:nt
setlocal
set "XEXE=XHCIINST.EXE"
set "XWIZ=XHCIWIZ.EXE"
set "XARCH=x86"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "XEXE=XHCIIN64.EXE"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "XWIZ=XHCIWZ64.EXE"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "XARCH=x64"
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "XEXE=XHCIIN64.EXE"
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "XWIZ=XHCIWZ64.EXE"
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "XARCH=x64"
set "XDIR=nt-%XARCH%"
if /i "%~1"=="/console" goto consolent
if not exist "%~dp0tools\xhciinst\%XWIZ%" (
    echo %~dp0tools\xhciinst\%XWIZ% is missing. Unzip the whole package and
    echo run INSTALL.BAT from its top directory.
    exit /b 1
)
rem A batch file waits for the window to close, so its exit code is passed on.
"%~dp0tools\xhciinst\%XWIZ%" %*
exit /b %ERRORLEVEL%

:consolent
if not exist "%~dp0tools\xhciinst\%XEXE%" (
    echo %~dp0tools\xhciinst\%XEXE% is missing. Unzip the whole package and
    echo run INSTALL.BAT from its top directory.
    exit /b 1
)
rem The console tool takes the word /console and goes on with the rest.
"%~dp0tools\xhciinst\%XEXE%" /dir %XDIR% %*
exit /b %ERRORLEVEL%

:end
