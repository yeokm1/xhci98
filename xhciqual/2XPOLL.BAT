@echo off
rem Step 2: poll-only active probe - resets/DMA/port reset, NO interrupt
rem handler installed (C4 SKIP, verdict PROVISIONAL).
if exist XPOLL.LOG del XPOLL.LOG
echo Log preflight>XPOLL.LOG
if not exist XPOLL.LOG goto logerr
del XPOLL.LOG
if exist XPOLL.LOG goto logerr
XHCIQUAL xhci --poll-only --no-wait --no-devid --no-page --log XPOLL.LOG
if not exist XPOLL.LOG goto logerr
echo.
echo Saved XPOLL.LOG. Cold boot, then run 3XIRQ.BAT.
goto end
:logerr
echo ERROR: XPOLL.LOG was not created or replaced. Active test stopped.
:end
