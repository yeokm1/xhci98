@echo off
rem XHPOLL.BAT - the release matrix's poller on Windows 98 SE and ME
rem (build-and-test.md, "Faster emulator legs"). XHSETUP.BAT copies it to
rem C:\XHLEG and starts it minimized at every Windows start. It says BOOT on
rem COM1, then waits for a CD whose root holds XHLEG.BAT and runs it once,
rem with the CD's drive letter. The host reads COM1; nothing here decides.
echo XHLEG BOOT 9x>COM1
set XC=
:look
for %%d in (D E F G H I J K L M) do if exist %%d:\XHLEG.BAT set XC=%%d
if not "%XC%"=="" goto run
choice /c:x /t:x,3 /n >NUL
goto look
:run
%XC%:\XHLEG.BAT %XC%
