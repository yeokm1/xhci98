@echo off
rem LEGNT.CMD - the standard leg on Windows 2000 to 7, run by XHPOLL.CMD from
rem the leg's CD as XHLEG.CMD, %1 the CD's drive letter (build-and-test.md,
rem "Faster emulator legs"). One phase per Windows start, chosen by the marker
rem files in C:\XHLEG; each phase ends by switching the machine off, and the
rem host starts it again cold. The host reads COM1 and gives the verdict.
rem XHMODE.CMD on the CD sets XHMODE: console (INSTALL /console /yes) or
rem window (the host or an operator runs the installer's window, then the
rem same checks). In window mode the first phase says the CD's letter, then
rem waits for the installer's log to end with "Exit code N." and says that
rem code and "WINDOW done": the run's end, which the screen cannot tell the
rem host. A log from before is moved to C:\XHLEG\PRIOR.LOG first. XHARGS,
rem set there too, is added to the console install (a leg's InstallArgs).
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
call INSTALL.BAT /console /yes /nopause /log %XL%\INSTALL.LOG %XHARGS% >%XL%\INSTALL.TXT 2>&1
>COM1 echo XHLEG EXIT install %ERRORLEVEL%
popd
call :send install %XL%\INSTALL.LOG
goto off
:operator
if exist C:\XHCIINST.LOG move /y C:\XHCIINST.LOG %XL%\PRIOR.LOG >NUL
echo XHLEG OPERATOR %XC% install with the window, then restart or shut down>COM1
:wwait
if exist C:\XHCIINST.LOG (
    find "Exit code " C:\XHCIINST.LOG >NUL 2>&1
    if not errorlevel 1 goto wdone
)
ping -n 6 127.0.0.1 >NUL 2>&1
goto wwait
:wdone
call :wrep
echo XHLEG WINDOW done>COM1
goto end

rem The window's run on COM1: its exit code and its log, once.
:wrep
set "XE=unread"
for /f "tokens=3 delims=. " %%c in ('type C:\XHCIINST.LOG ^| find "Exit code "') do set "XE=%%c"
>COM1 echo XHLEG EXIT install %XE%
call :send install C:\XHCIINST.LOG
echo x>%XL%\WSENT.ON
goto :eof

:ph2
rem A restart taken before the wait above read the log (an operator's quick
rem Finish): the run's exit code and log, said now.
if "%XHMODE%"=="window" if not exist %XL%\WSENT.ON call :wrep
echo x>%XL%\PH3.ON
echo XHLEG PHASE 2 check>COM1
if exist %SystemRoot%\System32\drivers\xhci98.sys (
    echo XHLEG CHECK driver-file PASS>COM1
) else (
    echo XHLEG CHECK driver-file FAIL no xhci98.sys in System32\drivers>COM1
)
pushd %XC%:\PKG
call INSTALL.BAT /console /list >%XL%\LIST2.TXT 2>&1
>COM1 echo XHLEG EXIT list %ERRORLEVEL%
popd
call :send list %XL%\LIST2.TXT
find "current (this" %XL%\LIST2.TXT >NUL
if errorlevel 1 (
    echo XHLEG CHECK list-current FAIL>COM1
) else (
    echo XHLEG CHECK list-current PASS>COM1
)
rem At a stick's first plug Windows installs its disk and then its volume,
rem and on XP the volume's install restarts the volume after its letter is
rem there: a file copied just before that restart was not on the volume
rem after it ("FC: cannot open E:\RAND.BIN", XP on q35, 2026-10-27). So the
rem copy waits for the volume's install to be finished in Windows'
rem device-install log: on 2000 and XP (setupapi.log) one more line than
rem before the plug naming a STORAGE\ device and "finished successfully";
rem on Vista and 7 (setupapi.dev.log) more lines naming a STORAGE\ device
rem than before and every section started also ended. Then the log must
rem not grow for 15 s; checked every 5 s, at most 200 s. At the bound, a
rem quiet log with nothing new (the stick known from before: no install
rem ran) goes on and says so; anything else fails the check. Copy's and
rem fc's own messages are kept, and a failed compare is never retried.
set "XSA=%SystemRoot%\setupapi.log"
if exist %SystemRoot%\inf\setupapi.dev.log set "XSA=%SystemRoot%\inf\setupapi.dev.log"
call :scount
set "XC0=%XCNT%"
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
set /a XN=0
set /a XQ=0
set "XZ0=none"
:sqwait
set /a XN+=1
call :scount
set "XZ=absent"
if exist "%XSA%" for %%f in ("%XSA%") do set "XZ=%%~zf"
if "%XZ%"=="%XZ0%" (set /a XQ+=1) else (set /a XQ=0)
set "XZ0=%XZ%"
if %XQ% GEQ 3 if %XCNT% GTR %XC0% if %XOPEN% LEQ 0 goto squiet
if %XN% GEQ 40 goto sbound
ping -n 6 127.0.0.1 >NUL 2>&1
goto sqwait
:sbound
if %XQ% LSS 3 goto sfail
if %XCNT% GTR %XC0% goto sfail
if %XOPEN% GTR 0 goto sfail
>COM1 echo XHLEG NOTE stick settle: no STORAGE\ install logged since the plug (%XC0% lines before)
goto squiet
:sfail
>COM1 echo XHLEG CHECK stick-copy FAIL no finished volume install in %XN% checks (STORAGE\ lines %XC0% then %XCNT%, sections open %XOPEN%, quiet %XQ%)
goto unin
:squiet
>COM1 echo XHLEG NOTE stick settle: %XN% checks, STORAGE\ lines %XC0% then %XCNT%
set "XS="
for %%d in (D E F G H I J K L M) do if exist %%d:\TESTDATA.BIN set "XS=%%d"
if not defined XS (
    echo XHLEG CHECK stick-copy FAIL no drive with TESTDATA.BIN after the settle>COM1
    goto unin
)
copy /y %XC%:\XHDATA\RAND.BIN %XS%:\RAND.BIN >%XL%\CP.TXT 2>&1
set "XCE=%ERRORLEVEL%"
fc /b %XC%:\XHDATA\RAND.BIN %XS%:\RAND.BIN >%XL%\FC.TXT 2>&1
set "XFE=%ERRORLEVEL%"
find "no differences" %XL%\FC.TXT >NUL
if errorlevel 1 (
    >COM1 echo XHLEG CHECK stick-copy FAIL on %XS%: copy exit %XCE%, fc exit %XFE%
    call :send copy %XL%\CP.TXT
    call :send fc %XL%\FC.TXT
) else (
    >COM1 echo XHLEG CHECK stick-copy PASS on %XS%:
)
echo XHLEG WANT unplug-stick>COM1
ping -n 6 127.0.0.1 >NUL 2>&1

:unin
pushd %XC%:\PKG
call INSTALL.BAT /console /uninstall /yes /nopause /log %XL%\UNINST.LOG >%XL%\UNINST.TXT 2>&1
>COM1 echo XHLEG EXIT uninstall %ERRORLEVEL%
popd
call :send uninstall %XL%\UNINST.LOG
goto off

:ph3
echo x>%XL%\PH4.ON
echo XHLEG PHASE 3 removed>COM1
pushd %XC%:\PKG
call INSTALL.BAT /console /list >%XL%\LIST3.TXT 2>&1
>COM1 echo XHLEG EXIT list-final %ERRORLEVEL%
popd
call :send list %XL%\LIST3.TXT
find "current (this" %XL%\LIST3.TXT >NUL
if errorlevel 2 (
    echo XHLEG CHECK list-removed FAIL find could not read the list>COM1
) else if errorlevel 1 (
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
if exist %SystemRoot%\system32\shutdown.exe (
    shutdown -s -f -t 0
) else (
    cscript //nologo C:\XHLEG\XHOFF.VBS
)
goto end

rem XCNT: on 2000 and XP the log's lines naming a STORAGE\ device and
rem "finished successfully", on Vista and 7 its lines naming a STORAGE\
rem device; XOPEN: on Vista and 7 the sections started less those ended.
:scount
set "XCNT=0"
set /a XOPEN=0
if not exist "%XSA%" goto :eof
if "%XSA%"=="%SystemRoot%\setupapi.log" (
    for /f %%c in ('type "%XSA%" ^| find /i "storage\" ^| find /c "finished successfully"') do set "XCNT=%%c"
    goto :eof
)
for /f %%c in ('type "%XSA%" ^| find /i /c "storage\"') do set "XCNT=%%c"
set "XST=0"
set "XEN=0"
for /f %%c in ('type "%XSA%" ^| find /c "Section start"') do set "XST=%%c"
for /f %%c in ('type "%XSA%" ^| find /c "Section end"') do set "XEN=%%c"
set /a XOPEN=XST-XEN
goto :eof

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
