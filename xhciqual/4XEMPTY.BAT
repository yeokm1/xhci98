@echo off
rem Step 4: full active xHCI run with no external USB test device.
rem C4 installs the ISR; --no-wait keeps the empty C6 pass bounded.
if exist XEMPTY.LOG del XEMPTY.LOG
echo Log preflight>XEMPTY.LOG
if not exist XEMPTY.LOG goto logerr
del XEMPTY.LOG
if exist XEMPTY.LOG goto logerr
if exist XEMPTY.FLG del XEMPTY.FLG
XHCIQUAL xhci --no-wait --no-page --log XEMPTY.LOG --done-flag XEMPTY.FLG
rem MS-DOS 7.1 (Win98) COMMAND.COM has no %ERRORLEVEL% variable; bucket the
rem exit code with IF ERRORLEVEL (true when the code is >= n) before any
rem other command runs. The completion flag is checked with IF EXIST, so no
rem FIND.EXE is needed on the boot medium.
set XQRC=0
if errorlevel 1 set XQRC=1
if errorlevel 2 set XQRC=2
if not exist XEMPTY.LOG goto logerr
if not exist XEMPTY.FLG goto abnormal
if not "%XQRC%"=="0" goto failed
echo.
echo XEMPTY.LOG completed normally with a QUALIFIED verdict.
echo Review the log for any WARN/SKIP lines, cold boot, attach the device,
echo then run 5XDEV.BAT.
goto end
:failed
echo XEMPTY.LOG completed normally but NOT QUALIFIED (exit %XQRC%).
echo Review the saved log and cold boot before another active test.
goto end
:abnormal
echo ERROR: XHCIQUAL ended abnormally - no completion flag was written.
echo Partial XEMPTY.LOG preserved. Cold boot before another active test.
goto end
:logerr
echo ERROR: XEMPTY.LOG was not created or replaced. Active test stopped.
:end
set XQRC=
