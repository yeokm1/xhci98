@echo off
rem XHPOLL.CMD - the release matrix's poller on Windows 2000 to 7
rem (build-and-test.md, "Faster emulator legs"). XHSETUP.CMD copies it to
rem C:\XHLEG and starts it at every logon (elevated on Vista and 7). It says
rem BOOT on COM1, then waits for a CD whose root holds XHLEG.CMD and runs it
rem once, with the CD's drive letter. The host reads COM1; nothing here decides.
setlocal
mode COM1: BAUD=115200 PARITY=N DATA=8 STOP=1 >NUL 2>&1
echo XHLEG BOOT nt>COM1
:look
set "XC="
for %%d in (D E F G H I J K L M) do if exist %%d:\XHLEG.CMD set "XC=%%d"
if not "%XC%"=="" goto run
ping -n 4 127.0.0.1 >NUL 2>&1
goto look
:run
call %XC%:\XHLEG.CMD %XC%
