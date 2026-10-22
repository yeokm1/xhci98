@echo off
rem build.cmd [qemu] - STOR98.VXD, the Windows 98 SE and ME IOS layer that
rem lets large requests reach Stor98 whole (roadmap-usb4 task 43.46, design
rem record 30 section 6).
rem
rem   (none)  the release image: obj\STOR98.VXD
rem   qemu    the qemu flavour's image, which writes to QEMU's debug console
rem           (port 0E9h): obj\qemu\STOR98.VXD
rem
rem Exit 0 built, 1 failed, 2 not built (no Open Watcom 2.0 or no Windows 98
rem SE DDK on this host), as xhciinst\di\build.cmd answers.
rem
rem Compile: Open Watcom 2.0 wcc386 under WATCOM (default C:\WATCOM). Link:
rem the Windows 98 SE DDK's LINK 5.12 /VXD, staged with the DDK's headers
rem by stage-ddk.ps1 (held to docs\references\README.md's hashes), which
rem reads Watcom's OMF objects (warning LNK4033). LINK 5.12 mislinks two
rem OMF shapes Watcom writes (record 30 section 6.1): so no file-scope
rem static in the sources, the data in an object of its own (vsd_data.c),
rem omfpub.py moving every PUBDEF to just before MODEND, omfcheck.py failing
rem on either shape left, and callcheck.py failing if a call in the image
rem lands anywhere but the function the compiler named.
setlocal
cd /d "%~dp0"
set "FLAV=%~1"
set "O=obj"
set "DEF="
if /i "%FLAV%"=="qemu" (
    set "O=obj\qemu"
    set "DEF=-dVSD_DEBUGCON %VSD_TEST_DEFS%"
)
if not "%FLAV%"=="" if /i not "%FLAV%"=="qemu" (
    echo usage: build.cmd [qemu]
    exit /b 1
)
if "%WATCOM%"=="" set "WATCOM=C:\WATCOM"
set "WBIN="
for %%D in (BINNT64 BINNT) do (
    if not defined WBIN if exist "%WATCOM%\%%D\wcc386.exe" set "WBIN=%WATCOM%\%%D"
)
if not defined WBIN (
    echo STOR98.VXD was not built: no Open Watcom wcc386.exe under %WATCOM%.
    exit /b 2
)
set "PY="
for %%P in (python.exe) do set "PY=%%~$PATH:P"
if not defined PY (
    echo STOR98.VXD was not built: no Python on PATH for its link checks.
    exit /b 2
)
if not exist obj mkdir obj
powershell -NoProfile -ExecutionPolicy Bypass -File stage-ddk.ps1 >obj\stage.log 2>&1
if errorlevel 1 (
    type obj\stage.log
    echo STOR98.VXD was not built: the Windows 98 SE DDK members are missing or not the recorded ones.
    exit /b 2
)
if not exist "%O%" mkdir "%O%"
set "PATH=%WBIN%;%PATH%"
rem -zp1: the DDK's C headers mirror the packed MASM layouts. -zc: string
rem literals in the code segment. -s: no stack probes. -zl -zls: no
rem default-library records. -oxi: inline outp. -we: a warning fails.
set "CF=-q -bt=windows -3r -zp1 -zc -s -zl -zls -oxi -w4 -we -i=obj\inc -i=%WATCOM%\H %DEF%"
for %%F in (vsd vsd_list vsd_data) do (
    wcc386 %CF% -fo=%O%\%%F.obj %%F.c || exit /b 1
    "%PY%" -I omfpub.py %O%\%%F.obj || exit /b 1
)
"%PY%" -I omfcheck.py %O%\vsd.obj %O%\vsd_list.obj %O%\vsd_data.obj || exit /b 1
obj\bin\link.exe /VXD /NOD /NOLOGO /IGNORE:4033 /IGNORE:4078 /IGNORE:4039 /IGNORE:4070 /IMPLIB:%O%\stor98.lib /OUT:%O%\STOR98.VXD /MAP:%O%\stor98.map /DEF:vsd.def %O%\vsd.obj %O%\vsd_list.obj %O%\vsd_data.obj
if errorlevel 1 exit /b 1
"%PY%" -I callcheck.py %O%\STOR98.VXD %O%\stor98.map %O%\vsd.obj %O%\vsd_list.obj || exit /b 1
set "QF="
if /i "%FLAV%"=="qemu" set "QF=-Qemu"
rem The release image writes to no I/O port: Open Watcom's disassembler
rem lists every object's code, and no IN/OUT-family instruction may be in it
rem (the qemu image's debug-console writes are its alone).
if /i not "%FLAV%"=="qemu" (
    for %%F in (vsd vsd_list vsd_data) do (
        wdis %O%\%%F.obj | findstr /r /i /c:"[ 	]out[sbwd]*[ 	]" /c:"[ 	]out$" >nul && (
            echo STOR98.VXD: %%F.obj has an OUT instruction in the release build
            exit /b 1
        )
    )
)
powershell -NoProfile -ExecutionPolicy Bypass -File check-vsd.ps1 %O%\STOR98.VXD %QF% || exit /b 1
echo built %O%\STOR98.VXD
exit /b 0
