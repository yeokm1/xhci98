@echo off
rem XHSETUP.CMD - once per starting point, on Windows 2000 to 7
rem (make-start.ps1 types it into Start, Run): the poller to C:\XHLEG, started
rem at every logon - a Run value on 2000 and XP, an elevated logon task on
rem Vista and 7 (an elevation prompt would stop an unattended install) - and
rem AutoRun off for every drive, so a leg's CD opens no window. XP's
rem unsigned-driver question is answered by the host (release-legs.psd1,
rem PromptStep): its policy value written here was not honoured (2026-10-22).
rem Windows 2000 has no reg.exe and no shutdown.exe: there the two values
rem come from XHSETNT.REG, and XHOFF.VBS is copied for the leg's shut down.
rem %1 is this CD's drive letter.
setlocal
if "%~1"=="" goto usage
if not exist C:\XHLEG\NUL md C:\XHLEG
copy /y %1:\XHPOLL.CMD C:\XHLEG\XHPOLL.CMD >NUL
if not exist C:\XHLEG\XHPOLL.CMD goto fail
copy /y %1:\XHOFF.VBS C:\XHLEG\XHOFF.VBS >NUL
if exist %SystemRoot%\system32\reg.exe goto hasreg
regedit /s %1:\XHSETNT.REG
if errorlevel 1 goto fail
goto said
:hasreg
ver | find " 6." >NUL
if not errorlevel 1 goto nt6
reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" /v XHLEG /t REG_SZ /d "cmd.exe /c start /min C:\XHLEG\XHPOLL.CMD" /f >NUL
if errorlevel 1 goto fail
goto ok
:nt6
rem A logon task is due from the moment it is made. A guest whose clock
rem starts at the same date and hour at every start (-rtc base=<date>, the q35
rem bases of Vista and 7) would log on before that moment at every later start
rem and never run it (read 2026-10-26: "Last Run Time: N/A"), so it is made with
rem the clock set to 2020 for an instant, then put back. The poller starts
rem minimised: its window would otherwise stand over the installer's.
set "XD="
for /f "tokens=2" %%d in ('date /t') do set "XD=%%d"
date 01-01-2020
schtasks /create /tn XHLEG /tr "cmd.exe /c start /min C:\XHLEG\XHPOLL.CMD" /sc onlogon /rl highest /f >NUL
set "XE=%ERRORLEVEL%"
if defined XD date %XD%
if not "%XE%"=="0" goto fail
rem Vista asks at every logon, for each device with no driver, "Found New
rem Hardware: Windows needs to install driver software", and an install made
rem while one is open ends with exit 4, "Nothing was changed" (the q35 base,
rem read 2026-10-26); the dialog does not take keys reliably. The policy
rem that suppresses that dialog is set (7 shows none).
reg add "HKLM\Software\Policies\Microsoft\Windows\DeviceInstall\Settings" /v SuppressNewHWUI /t REG_DWORD /d 1 /f >NUL
:ok
reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\Policies\Explorer" /v NoDriveTypeAutoRun /t REG_DWORD /d 255 /f >NUL
:said
echo XHLEG SETUP OK nt>COM1
goto end
:fail
echo XHLEG SETUP FAIL nt>COM1
goto end
:usage
echo usage: XHSETUP.CMD drive-letter
:end
