@echo off
rem Step 3: isolated locked one-shot xHCI IRQ test; C6/C8 are not run.
if exist XIRQ.LOG del XIRQ.LOG
echo Log preflight>XIRQ.LOG
if not exist XIRQ.LOG goto logerr
del XIRQ.LOG
if exist XIRQ.LOG goto logerr
if exist XIRQ.FLG del XIRQ.FLG
XHCIQUAL xhci --irq-selftest --no-page --log XIRQ.LOG --done-flag XIRQ.FLG
rem MS-DOS 7.1 (Win98) COMMAND.COM has no %ERRORLEVEL% variable; bucket the
rem exit code with IF ERRORLEVEL (true when the code is >= n) before any
rem other command runs. The completion flag is checked with IF EXIST, so no
rem FIND.EXE is needed on the boot medium.
set XQRC=0
if errorlevel 1 set XQRC=1
if errorlevel 2 set XQRC=2
if not exist XIRQ.LOG goto logerr
if not exist XIRQ.FLG goto abnormal
if not "%XQRC%"=="0" goto failed
echo.
echo XIRQ.LOG completed normally with IRQ SELF-TEST PASS.
echo Cold boot, disconnect external devices, then run 4XEMPTY.BAT.
goto end
:failed
echo XIRQ.LOG completed normally but the IRQ self-test FAILED (exit %XQRC%).
echo Preserve the log and cold boot. Do not continue to 4XEMPTY.BAT.
goto end
:abnormal
echo ERROR: XHCIQUAL ended abnormally - no completion flag was written.
echo Partial XIRQ.LOG preserved. Cold boot before another active test.
goto end
:logerr
echo ERROR: XIRQ.LOG was not created or replaced. IRQ self-test stopped.
:end
set XQRC=
