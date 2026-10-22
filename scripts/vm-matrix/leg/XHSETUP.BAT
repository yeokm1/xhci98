@echo off
rem XHSETUP.BAT - once per starting point, on Windows 98 SE and ME
rem (make-start.ps1 types it into Start, Run): the poller to C:\XHLEG and a
rem Run value that starts it minimized at every Windows start. %1 is this
rem CD's drive letter.
if "%1"=="" goto usage
if not exist C:\XHLEG\NUL md C:\XHLEG
copy %1:\XHPOLL.BAT C:\XHLEG\XHPOLL.BAT >NUL
if not exist C:\XHLEG\XHPOLL.BAT goto fail
regedit /s %1:\XHSETUP.REG
echo XHLEG SETUP OK 9x>COM1
goto end
:fail
echo XHLEG SETUP FAIL 9x copy>COM1
goto end
:usage
echo usage: XHSETUP.BAT drive-letter
:end
