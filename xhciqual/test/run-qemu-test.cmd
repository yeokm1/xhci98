@echo off
rem run-qemu-test.cmd - XHCIQUAL smoke test: boot the bare Win98SE (MS-DOS
rem 7.1) target DOS in QEMU with a qemu-xhci controller forced to INTx
rem (msi=off,msix=off) and the built tool on a virtual FAT hard disk (C:).
rem
rem QEMU proves the code paths, not the firmware: the Phase 0 checkpoint
rem verdict only counts on bare metal.
rem
rem Needs:
rem   - qemu-system-x86_64 on PATH (scripts\setup-qemu.ps1 -Install, or scoop)
rem   - tools\w98se.img: a bare Win98SE boot floppy (proprietary, supplied
rem     locally, not committed - see xhciqual\README.md). This is the same
rem     target DOS the automated matrix uses.
rem
rem In the guest (boots straight to A:\>):
rem   1. c:
rem   2. xhciqual --serial --no-wait
rem Output lands in xhciqual\test\serial.log; read results there. The FAT hard
rem disk (C:) is a read-only host dir behind a throwaway snapshot overlay
rem (snapshot=on), so guest writes - including any --log copy on C: - are
rem discarded on exit and never touch the host. Do not switch to fat:rw:, which
rem writes back to the host directory. The usb-storage device attaches
rem as SuperSpeed on a USB3 port
rem (correctly unmanaged); the usb-mouse attaches as a USB2 device, giving
rem C6 its connect+reset and C8 a device to identify (expect
rem "0627:0001 QEMU USB Mouse", interface class 03 HID). More devices can
rem be hot-added from the QEMU monitor (Ctrl-Alt-2):
rem   device_add usb-mouse,bus=xhci.0

setlocal
rem Quoted assignments (set "VAR=value"). The unquoted form takes the rest of
rem the line verbatim, so a checkout path containing '&' truncates the value
rem there and runs the remainder as a command.
set "HERE=%~dp0"
set "REPO=%HERE%..\.."
set "QEMU=qemu-system-x86_64"
where "%QEMU%" >nul 2>nul
if errorlevel 1 set "QEMU=%USERPROFILE%\scoop\apps\qemu\current\qemu-system-x86_64.exe"

set "BOOTIMG=%REPO%\tools\w98se.img"
rem %BOOTIMG% is quoted inside this block on purpose: cmd expands it when it
rem parses the whole parenthesized block, so a ')' anywhere in the path would
rem close the block early and abort with a parse error instead of printing the
rem message. Quotes make the parser skip the metacharacters.
if not exist "%BOOTIMG%" (
  echo Missing "%BOOTIMG%" - supply a bare Win98SE boot floppy there.
  exit /b 1
)
if not exist "%HERE%..\xhciqual.exe" (
  echo Build first: xhciqual\build.cmd
  exit /b 1
)

if not exist "%HERE%hdd" mkdir "%HERE%hdd"
copy /y "%HERE%..\xhciqual.exe" "%HERE%hdd\XHCIQUAL.EXE" >nul

if not exist "%HERE%usb.img" fsutil file createnew "%HERE%usb.img" 8388608 >nul

"%QEMU%" -machine pc -m 64 -boot a ^
  -drive if=floppy,file="%BOOTIMG%",format=raw ^
  -drive file=fat:"%HERE%hdd",format=raw,if=ide,media=disk,snapshot=on ^
  -device qemu-xhci,id=xhci,msi=off,msix=off ^
  -drive if=none,id=ud,file="%HERE%usb.img",format=raw ^
  -device usb-storage,drive=ud,bus=xhci.0,port=1 ^
  -device usb-mouse,bus=xhci.0,port=2 ^
  -serial file:"%HERE%serial.log"
endlocal
