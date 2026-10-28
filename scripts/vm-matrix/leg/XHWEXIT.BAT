@echo off
rem XHWEXIT.BAT n - "XHLEG EXIT install n" on COM1 when the installer's log
rem says "Exit code n." (LEG9X.BAT's window mode; COMMAND.COM has no FOR /F).
find "Exit code %1." C:\XHCIINST.LOG>NUL
if errorlevel 1 goto end
echo XHLEG EXIT install %1>COM1
:end
