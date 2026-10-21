@echo off
rem LEGNT.CMD - the standard leg on Windows 2000 to 7, run by XHPOLL.CMD from
rem the leg's CD as XHLEG.CMD, %1 the CD's drive letter (build-and-test.md,
rem "Faster emulator legs"). One phase per Windows start, chosen by the marker
rem files in C:\XHLEG; each phase ends by switching the machine off, and the
rem host starts it again cold. The host reads COM1 and gives the verdict.
rem XHMODE.CMD on the CD sets XHMODE: console (INSTALL /console /yes) or
rem window (the operator runs the installer's window, then the same checks).
setlocal
set "XC=%~1"
call %XC%:\XHMODE.CMD
set "XL=C:\XHLEG"
if exist %XL%\PH4.ON goto again
if exist %XL%\PH3.ON goto ph3
if exist %XL%\PH2.ON goto ph2

echo x>%XL%\PH2.ON
echo XHLEG PHASE 1 install %XHMODE%>COM1
ver>%XL%\VER.TXT
call :send ver %XL%\VER.TXT
if "%XHMODE%"=="window" goto operator
pushd %XC%:\PKG
call INSTALL.BAT /console /yes /nopause /log %XL%\INSTALL.LOG >%XL%\INSTALL.TXT 2>&1
echo XHLEG EXIT install %ERRORLEVEL%>COM1
popd
call :send install %XL%\INSTALL.LOG
goto off
:operator
echo XHLEG OPERATOR install with the window, then restart or shut down>COM1
goto end

:ph2
echo x>%XL%\PH3.ON
echo XHLEG PHASE 2 check>COM1
if exist %SystemRoot%\System32\drivers\xhci98.sys (
    echo XHLEG CHECK driver-file PASS>COM1
) else (
    echo XHLEG CHECK driver-file FAIL no xhci98.sys in System32\drivers>COM1
)
pushd %XC%:\PKG
call INSTALL.BAT /console /list >%XL%\LIST2.TXT 2>&1
echo XHLEG EXIT list %ERRORLEVEL%>COM1
popd
call :send list %XL%\LIST2.TXT
find "current (this" %XL%\LIST2.TXT >NUL
if errorlevel 1 (
    echo XHLEG CHECK list-current FAIL>COM1
) else (
    echo XHLEG CHECK list-current PASS>COM1
)
echo XHLEG WANT plug-stick>COM1
set "XS="
for /l %%n in (1,1,40) do (
    if not defined XS (
        for %%d in (D E F G H I J K L M) do if exist %%d:\TESTDATA.BIN set "XS=%%d"
        if not defined XS ping -n 4 127.0.0.1 >NUL 2>&1
    )
)
if not defined XS (
    echo XHLEG CHECK stick-seen FAIL no drive with TESTDATA.BIN in 120 s>COM1
    goto unin
)
echo XHLEG CHECK stick-seen PASS %XS%:>COM1
copy /y %XC%:\XHDATA\RAND.BIN %XS%:\RAND.BIN >NUL
fc /b %XC%:\XHDATA\RAND.BIN %XS%:\RAND.BIN >%XL%\FC.TXT
find "no differences" %XL%\FC.TXT >NUL
if errorlevel 1 (
    echo XHLEG CHECK stick-copy FAIL>COM1
    call :send fc %XL%\FC.TXT
) else (
    echo XHLEG CHECK stick-copy PASS>COM1
)
echo XHLEG WANT unplug-stick>COM1
ping -n 6 127.0.0.1 >NUL 2>&1

:unin
pushd %XC%:\PKG
call INSTALL.BAT /console /uninstall /yes /nopause /log %XL%\UNINST.LOG >%XL%\UNINST.TXT 2>&1
echo XHLEG EXIT uninstall %ERRORLEVEL%>COM1
popd
call :send uninstall %XL%\UNINST.LOG
goto off

:ph3
echo x>%XL%\PH4.ON
echo XHLEG PHASE 3 removed>COM1
pushd %XC%:\PKG
call INSTALL.BAT /console /list >%XL%\LIST3.TXT 2>&1
popd
call :send list %XL%\LIST3.TXT
find "current (this" %XL%\LIST3.TXT >NUL
if errorlevel 1 (
    echo XHLEG CHECK list-removed PASS>COM1
) else (
    echo XHLEG CHECK list-removed FAIL a device still current>COM1
)
echo XHLEG END>COM1
goto off

:again
echo XHLEG END already>COM1
:off
echo XHLEG POWEROFF>COM1
shutdown -s -f -t 0
goto end

:send
if not exist "%~2" (
    echo XHLEG LOG-MISSING %1 %2>COM1
    goto :eof
)
echo XHLEG LOG-BEGIN %1>COM1
type "%~2">COM1
echo.>COM1
echo XHLEG LOG-END %1>COM1
goto :eof

:end
