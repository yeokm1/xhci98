@echo off
rem XHSETUP.CMD - once per starting point, on Windows 2000 to 7
rem (make-start.ps1 types it into Start, Run): the poller to C:\XHLEG, started
rem at every logon - a Run value on 2000 and XP, an elevated logon task on
rem Vista and 7 (an elevation prompt would stop an unattended install) - and,
rem on 2000 and XP, the unsigned-driver policy at Ignore so the x86 package's
rem unsigned drivers install without the Logo-testing dialog. %1 is this CD's
rem drive letter.
setlocal
if "%~1"=="" goto usage
if not exist C:\XHLEG\NUL md C:\XHLEG
copy /y %1:\XHPOLL.CMD C:\XHLEG\XHPOLL.CMD >NUL
if not exist C:\XHLEG\XHPOLL.CMD goto fail
ver | find " 6." >NUL
if not errorlevel 1 goto nt6
reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" /v XHLEG /t REG_SZ /d "cmd.exe /c start /min C:\XHLEG\XHPOLL.CMD" /f >NUL
if errorlevel 1 goto fail
reg add "HKLM\Software\Microsoft\Driver Signing" /v Policy /t REG_BINARY /d 00 /f >NUL
reg add "HKCU\Software\Microsoft\Driver Signing" /v Policy /t REG_DWORD /d 0 /f >NUL
goto ok
:nt6
schtasks /create /tn XHLEG /tr "C:\XHLEG\XHPOLL.CMD" /sc onlogon /rl highest /f >NUL
if errorlevel 1 goto fail
:ok
echo XHLEG SETUP OK nt>COM1
goto end
:fail
echo XHLEG SETUP FAIL nt>COM1
goto end
:usage
echo usage: XHSETUP.CMD drive-letter
:end
