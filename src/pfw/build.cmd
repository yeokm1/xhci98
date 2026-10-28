@echo off
rem build.cmd [qemu] - XHCIPFW.VXD, the Windows 98 SE and ME static VxD
rem that closes PCI bridges' prefetchable windows lying wholly above 4 GB
rem before the configuration manager reads them (roadmap-usb4 task 44.49,
rem design record 32).
rem
rem   (none)  the release image: obj\XHCIPFW.VXD
rem   qemu    the qemu flavour's image, which writes to QEMU's debug console
rem           (port 0E9h): obj\qemu\XHCIPFW.VXD
rem
rem Exit 0 built, 1 failed, 2 not built (no Open Watcom or no Windows 98 SE
rem DDK on this host), as src\vsd\build.cmd answers.
rem
rem The toolchain and its checks are S98VSD.VXD's (design record 30 section
rem 6.1): Open Watcom 2.0 wcc386, the Windows 98 SE DDK's LINK 5.12 /VXD
rem staged by src\vsd\stage-ddk.ps1 into src\vsd\obj\bin\, and src\vsd's
rem omfpub.py, omfcheck.py and callcheck.py. So no file-scope static in the
rem sources and the data in an object of its own (pfw_data.c).
setlocal
cd /d "%~dp0"
set "FLAV=%~1"
set "O=obj"
set "DEF="
if /i "%FLAV%"=="qemu" (
    set "O=obj\qemu"
    set "DEF=-dPFW_DEBUGCON"
)
if not "%FLAV%"=="" if /i not "%FLAV%"=="qemu" (
    echo usage: build.cmd [qemu]
    exit /b 1
)
rem The flavour's image goes first: whatever fails below, none is left.
if exist "%O%\XHCIPFW.VXD" del "%O%\XHCIPFW.VXD"
if exist "%O%\XHCIPFW.VXD" (
    echo XHCIPFW.VXD: the old %O%\XHCIPFW.VXD could not be deleted
    exit /b 1
)
if "%WATCOM%"=="" set "WATCOM=C:\WATCOM"
set "WBIN="
for %%D in (BINNT64 BINNT) do (
    if not defined WBIN if exist "%WATCOM%\%%D\wcc386.exe" set "WBIN=%WATCOM%\%%D"
)
if not defined WBIN (
    echo XHCIPFW.VXD was not built: no Open Watcom wcc386.exe under %WATCOM%.
    exit /b 2
)
set "PY="
for %%P in (python.exe) do set "PY=%%~$PATH:P"
if not defined PY (
    echo XHCIPFW.VXD was not built: no Python on PATH for its link checks.
    exit /b 2
)
set "V=..\vsd"
if not exist obj mkdir obj
powershell -NoProfile -ExecutionPolicy Bypass -File %V%\stage-ddk.ps1 >obj\stage.log 2>&1
if errorlevel 1 (
    type obj\stage.log
    echo XHCIPFW.VXD was not built: the Windows 98 SE DDK members are missing or not the recorded ones.
    exit /b 2
)
if not exist "%O%" mkdir "%O%"
set "PATH=%WBIN%;%PATH%"
rem -zp1: packed records, the layouts pfw_scan.h gives byte for byte. -zc:
rem string literals in the code segment. -s: no stack probes. -zl -zls: no
rem default-library records. -oxi: inline inpd/outpd. -we: a warning fails.
set "CF=-q -bt=windows -3r -zp1 -zc -s -zl -zls -oxi -w4 -we -i=%WATCOM%\H %DEF%"
for %%F in (pfw pfw_scan pfw_io pfw_data) do (
    wcc386 %CF% -fo=%O%\%%F.obj %%F.c || exit /b 1
    "%PY%" -I %V%\omfpub.py %O%\%%F.obj || exit /b 1
)
"%PY%" -I %V%\omfcheck.py %O%\pfw.obj %O%\pfw_scan.obj %O%\pfw_io.obj %O%\pfw_data.obj || exit /b 1
rem Linked as PFWNEW.VXD and made XHCIPFW.VXD only once every check below
rem has passed (the old image is deleted at the start).
if exist %O%\PFWNEW.VXD del %O%\PFWNEW.VXD
%V%\obj\bin\link.exe /VXD /NOD /NOLOGO /IGNORE:4033 /IGNORE:4078 /IGNORE:4039 /IGNORE:4070 /OUT:%O%\PFWNEW.VXD /MAP:%O%\xhcipfw.map /DEF:pfw.def %O%\pfw.obj %O%\pfw_scan.obj %O%\pfw_io.obj %O%\pfw_data.obj
if errorlevel 1 exit /b 1
"%PY%" -I %V%\callcheck.py %O%\PFWNEW.VXD %O%\xhcipfw.map %O%\pfw.obj %O%\pfw_scan.obj %O%\pfw_io.obj || exit /b 1
set "QF="
if /i "%FLAV%"=="qemu" set "QF=-Qemu"
rem Every object is disassembled. In the release image no port instruction
rem may be outside pfw_io.obj, and pfw_io.obj's ports are CF8h and CFCh
rem alone (record 32 section 8); the qemu image's debug-console writes are
rem its own.
for %%F in (pfw pfw_scan pfw_io pfw_data) do (
    wdis %O%\%%F.obj > %O%\%%F.dis || (
        echo XHCIPFW.VXD: wdis could not disassemble %%F.obj
        exit /b 1
    )
)
for %%F in (pfw pfw_scan pfw_io) do (
    findstr /r /c:"^0000 " %O%\%%F.dis >nul || (
        echo XHCIPFW.VXD: %%F.dis holds no disassembled code
        exit /b 1
    )
)
if /i not "%FLAV%"=="qemu" (
    for %%F in (pfw pfw_scan pfw_data) do (
        findstr /r /i /c:"[ 	]out[sbwd]*[ 	]" /c:"[ 	]out$" /c:"[ 	]in[sbwd]*[ 	]" /c:"[ 	]in$" %O%\%%F.dis >nul && (
            echo XHCIPFW.VXD: %%F.obj has a port instruction in the release build
            exit /b 1
        )
    )
)
findstr /r /i /c:"mov[ 	][ 	]*e*dx," %O%\pfw_io.dis > %O%\pfw_io.ports
findstr /r /i /v /c:"0x00000cf8" /c:"0x00000cfc" %O%\pfw_io.ports >nul && (
    echo XHCIPFW.VXD: pfw_io.obj loads a port other than CF8h and CFCh
    type %O%\pfw_io.ports
    exit /b 1
)
findstr /r /i /c:"0x00000cf8" %O%\pfw_io.ports >nul || (
    echo XHCIPFW.VXD: pfw_io.obj loads no port CF8h
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File check-pfw.ps1 %O%\PFWNEW.VXD %QF% || exit /b 1
move /y %O%\PFWNEW.VXD %O%\XHCIPFW.VXD >nul || exit /b 1
echo built %O%\XHCIPFW.VXD
exit /b 0
