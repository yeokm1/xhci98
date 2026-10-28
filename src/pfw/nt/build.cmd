@echo off
rem build.cmd [qemu] - XHCIPFW.SYS, the boot-start driver that closes PCI
rem bridges' prefetchable windows lying wholly above 4 GB before the PCI
rem driver of Windows 2000 or the 32-bit Vista, 7, 8, 8.1 and 10 reads them
rem (roadmap-usb4 task 44.49, design record 32 section 11).
rem
rem   (none)  the release image: objfre\i386\xhcipfw.sys
rem   qemu    the qemu flavour's image, which writes its counts to QEMU's
rem           debug console (port 0E9h): objfre_qemu\i386\xhcipfw.sys
rem
rem Exit 0 built, 1 failed, 2 not built (no Windows 2000 DDK on this host).
rem
rem Built with the Windows 2000 DDK in tools\ntddk (DDKROOT overrides it), as
rem the x86 drivers are; gated by the import gate's hlprx86 profile with
rem scripts\import-gate\xhcipfw-imports.allow (every import an export of
rem every kernel and HAL of 2000, XP, Vista and 7 x86) and by
rem check-pfwsys.ps1 (the image, its imports against pfw-sys-imports.allow,
rem the stamp, the flavour marker, no debug-console writer in the release
rem image).
setlocal
cd /d "%~dp0"
set "FLAV=%~1"
set "ALT=fre"
set "QF="
set "GFLAV=release"
if /i "%FLAV%"=="qemu" (
    set "ALT=fre_qemu"
    set "QF=-Qemu"
    set "GFLAV=qemu"
)
if not "%FLAV%"=="" if /i not "%FLAV%"=="qemu" (
    echo usage: build.cmd [qemu]
    exit /b 1
)
set "REPO=%~dp0..\..\.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
if "%DDKROOT%"=="" set "DDKROOT=%REPO%\tools\ntddk"
if not exist "%DDKROOT%\bin\setenv.bat" (
    echo XHCIPFW.SYS was not built: no Windows 2000 DDK at %DDKROOT%.
    exit /b 2
)
set "OUT=obj%ALT%\i386\xhcipfw.sys"
if exist "%OUT%" del "%OUT%"
if exist "%OUT%" (
    echo XHCIPFW.SYS: the old %OUT% could not be deleted
    exit /b 1
)
if exist build%ALT%.err del build%ALT%.err
cmd /c "call "%DDKROOT%\bin\setenv.bat" %DDKROOT% free w2k x86 && set "BUILD_ALT_DIR=%ALT%" && cd /d "%~dp0" && build -cZ"
if errorlevel 1 goto fail
if exist build%ALT%.err goto fail
if not exist "%OUT%" goto fail
powershell -NoProfile -ExecutionPolicy Bypass -File check-pfwsys.ps1 "%OUT%" %QF%
if errorlevel 1 goto fail
powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO%\scripts\import-gate\check-imports.ps1" -Image "%OUT%" -Flavor %GFLAV% -Arch x86 -GateProfile hlprx86 -AllowPath "%REPO%\scripts\import-gate\xhcipfw-imports.allow"
if errorlevel 1 goto fail
echo built %OUT%
exit /b 0
:fail
echo XHCIPFW.SYS: the %ALT% build or its gate failed - see above and build%ALT%.log.
if exist "%OUT%" del "%OUT%"
exit /b 1
