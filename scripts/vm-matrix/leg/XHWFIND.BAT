@echo off
rem XHWFIND.BAT - LEG9X.BAT's window mode, under COMMAND /F: C:\XHLEG\WDONE.ON
rem once the installer's log says "Exit code"; a read refused while the
rem window holds the log open leaves nothing.
find "Exit code " C:\XHCIINST.LOG>NUL
if errorlevel 1 goto end
echo x>C:\XHLEG\WDONE.ON
:end
