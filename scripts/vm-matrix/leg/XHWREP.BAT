@echo off
rem XHWREP.BAT cd - LEG9X.BAT's window mode: the installer's exit code
rem ("XHLEG EXIT install n") and its log on COM1, then C:\XHLEG\WSENT.ON so
rem it is said once (at the run's end, or at the next start when a restart
rem came first).
for %%c in (0 1 2 3 4 5 6 7 8 9) do call %1:\XHWEXIT.BAT %%c
call %1:\XHSEND.BAT install C:\XHCIINST.LOG
echo x>C:\XHLEG\WSENT.ON
