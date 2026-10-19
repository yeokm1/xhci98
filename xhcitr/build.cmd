@echo off
rem build.cmd - build XHCITR.EXE and XHCITR64.EXE (roadmap-usb4.md tasks
rem 41-A.2 to 41-A.5; design record 25 section 9), as xhciinst\build.cmd
rem builds XHCIINST.EXE and XHCIIN64.EXE.
rem
rem   build.cmd        both executables
rem   build.cmd x86    XHCITR.EXE alone, for a host without tools\WinDDK71
rem
rem Nothing is installed machine-wide. XHCITR.EXE, the 32-bit build that
rem starts on Windows 98 SE, ME, 2000, XP and 32-bit Vista and 7, is built
rem with the in-repo MSVC 6.0 (tools\MSVC600; MSVC6 overrides it), /W3 /WX,
rem the C runtime linked statically, /SUBSYSTEM:WINDOWS,4.0. XHCITR64.EXE is
rem the same sources built with WDK 7.1's amd64 compiler (tools\WinDDK71;
rem WDK71 overrides it), /MT, /SUBSYSTEM:WINDOWS,5.02, for XP x64, Vista
rem x64 and 7 x64. Both import KERNEL32, USER32, GDI32, ADVAPI32, SHELL32,
rem COMCTL32 and COMDLG32 alone (selftest.cmd holds them to it); every
rem SetupAPI and configuration-manager routine is looked up at run time.
rem
rem The sources: xhcitr.c (the system half), tr_core.c (the pure half the
rem host suite test\test_tr.c also links), tr_pci.c (the PCI configuration
rem nodes' pure half, test\test_trpci.c's), tr_log.c (the driver logs' pure
rem half, test\test_trlog.c's), tr_snap.c (XHCISNAP's reading, moved here
rem by roadmap-usb4 task 42-A.4: /snapshot, /dump and /probe) and
rem ..\xhciinst\inst_core.c, record 24's
rem unit rules and eject texts.
setlocal
cd /d "%~dp0"

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
if "%MSVC6%"=="" set "MSVC6=%REPO%\tools\MSVC600"
if "%WDK71%"=="" set "WDK71=%REPO%\tools\WinDDK71"
set "WANT64=1"
if /i "%~1"=="x86" set "WANT64=0"
if not "%~1"=="" if /i not "%~1"=="x86" (
    echo ERROR: unknown argument %1 ^(none, or x86^)
    exit /b 1
)

if not exist "%MSVC6%\VC98\Bin\cl.exe" (
    echo ERROR: no cl.exe under %MSVC6%\VC98\Bin
    echo        Unpack tools\MSVC600.zip, or set MSVC6.
    exit /b 1
)
if "%WANT64%"=="1" if not exist "%WDK71%\bin\x86\amd64\cl.exe" (
    echo ERROR: no amd64 cl.exe under %WDK71%\bin\x86\amd64
    echo        Unpack WDK 7.1 into tools\WinDDK71, set WDK71, or run
    echo        build.cmd x86 for XHCITR.EXE alone.
    exit /b 1
)

if exist XHCITR.EXE del XHCITR.EXE
if exist XHCITR64.EXE del XHCITR64.EXE
if exist xhcitr.res del xhcitr.res
if exist xhcitr64.res del xhcitr64.res

set "LIBS=user32.lib gdi32.lib advapi32.lib shell32.lib comctl32.lib comdlg32.lib"

rem ---- XHCITR.EXE: MSVC 6.0 ------------------------------------------------
setlocal
set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"
rc /fo xhcitr.res xhcitr.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /FeXHCITR.EXE xhcitr.c tr_core.c tr_log.c tr_pci.c tr_snap.c ..\xhciinst\inst_core.c ..\xhcimark\mark_core.c xhcitr.res %LIBS% /link /SUBSYSTEM:WINDOWS,4.0
if errorlevel 1 exit /b 1
if not exist XHCITR.EXE exit /b 1
endlocal
del /q xhcitr.obj tr_core.obj tr_log.obj tr_pci.obj tr_snap.obj inst_core.obj mark_core.obj xhcitr.res 2>nul
echo Built XHCITR.EXE

if "%WANT64%"=="0" goto done

rem ---- XHCITR64.EXE: WDK 7.1, amd64 ----------------------------------------
setlocal
set "PATH=%WDK71%\bin\x86\amd64;%WDK71%\bin\x86;%PATH%"
set "INCLUDE=%WDK71%\inc\api;%WDK71%\inc\crt"
set "LIB=%WDK71%\lib\wnet\amd64;%WDK71%\lib\crt\amd64"
rc /i "%WDK71%\inc\api" /fo xhcitr64.res xhcitr.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /FeXHCITR64.EXE xhcitr.c tr_core.c tr_log.c tr_pci.c tr_snap.c ..\xhciinst\inst_core.c ..\xhcimark\mark_core.c xhcitr64.res %LIBS% /link /SUBSYSTEM:WINDOWS,5.02 /MACHINE:X64
if errorlevel 1 exit /b 1
if not exist XHCITR64.EXE exit /b 1
endlocal
del /q xhcitr.obj tr_core.obj tr_log.obj tr_pci.obj tr_snap.obj inst_core.obj mark_core.obj xhcitr64.res 2>nul
echo Built XHCITR64.EXE

:done
echo.
exit /b 0
