@echo off
rem XHSETUP.CMD - once per starting point, on Windows 2000 to 7
rem (make-start.ps1 types it into Start, Run): the poller to C:\XHLEG, started
rem at every logon - a Run value on 2000 and XP, an elevated logon task on
rem Vista and 7 (an elevation prompt would stop an unattended install) - and
rem AutoRun off for every drive, so a leg's CD opens no window. XP's
rem unsigned-driver question is answered by the host (release-legs.psd1,
rem PromptStep): its policy value written here was not honoured (2026-10-22).
rem %1 is this CD's drive letter.
setlocal
if "%~1"=="" goto usage
if not exist C:\XHLEG\NUL md C:\XHLEG
copy /y %1:\XHPOLL.CMD C:\XHLEG\XHPOLL.CMD >NUL
if not exist C:\XHLEG\XHPOLL.CMD goto fail
ver | find " 6." >NUL
if not errorlevel 1 goto nt6
reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" /v XHLEG /t REG_SZ /d "cmd.exe /c start /min C:\XHLEG\XHPOLL.CMD" /f >NUL
if errorlevel 1 goto fail
goto ok
:nt6
schtasks /create /tn XHLEG /tr "C:\XHLEG\XHPOLL.CMD" /sc onlogon /rl highest /f >NUL
if errorlevel 1 goto fail
:ok
reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\Policies\Explorer" /v NoDriveTypeAutoRun /t REG_DWORD /d 255 /f >NUL
echo XHLEG SETUP OK nt>COM1
goto end
:fail
echo XHLEG SETUP FAIL nt>COM1
goto end
:usage
echo usage: XHSETUP.CMD drive-letter
:end
