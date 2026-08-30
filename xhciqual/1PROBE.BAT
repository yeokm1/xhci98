@echo off
rem Step 1: read-only inventory - safe, takes no controller ownership.
if exist PROBE.LOG del PROBE.LOG
echo Log preflight>PROBE.LOG
if not exist PROBE.LOG goto logerr
del PROBE.LOG
if exist PROBE.LOG goto logerr
XHCIQUAL --probe-only --no-page --log PROBE.LOG
if not exist PROBE.LOG goto logerr
echo.
echo Saved PROBE.LOG. Cold boot, then run 2XPOLL.BAT.
goto end
:logerr
echo ERROR: PROBE.LOG was not created or replaced. Fix logging first.
:end
