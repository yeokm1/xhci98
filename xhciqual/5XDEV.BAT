@echo off
rem Step 5: full active xHCI device run after the empty-controller pass.
if exist XDEV.LOG del XDEV.LOG
echo Log preflight>XDEV.LOG
if not exist XDEV.LOG goto logerr
del XDEV.LOG
if exist XDEV.LOG goto logerr
if exist XDEV.FLG del XDEV.FLG
XHCIQUAL xhci --no-page --log XDEV.LOG --done-flag XDEV.FLG
rem MS-DOS 7.1 (Win98) COMMAND.COM has no %ERRORLEVEL% variable; bucket the
rem exit code with IF ERRORLEVEL (true when the code is >= n) before any
rem other command runs. The completion flag is checked with IF EXIST, so no
rem FIND.EXE is needed on the boot medium.
set XQRC=0
if errorlevel 1 set XQRC=1
if errorlevel 2 set XQRC=2
if not exist XDEV.LOG goto logerr
if not exist XDEV.FLG goto abnormal
if not "%XQRC%"=="0" goto failed
echo.
echo XDEV.LOG completed normally with a QUALIFIED verdict.
echo Keep all LOGs with this XHCIQUAL.EXE + XHCIQUAL.MAP.
goto end
:failed
echo XDEV.LOG completed normally but NOT QUALIFIED (exit %XQRC%).
echo Keep the log and cold boot before another active test.
goto end
:abnormal
echo ERROR: XHCIQUAL ended abnormally - no completion flag was written.
echo Partial XDEV.LOG preserved. Cold boot before another active test.
goto end
:logerr
echo ERROR: XDEV.LOG was not created or replaced. Active test stopped.
:end
set XQRC=
