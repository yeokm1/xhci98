@echo off
rem build.cmd - build XHCITR.EXE and XHCITR64.EXE (roadmap-usb4.md tasks
rem 41-A.2 to 41-A.5; design record 25 section 9), as xhciinst\build.cmd
rem builds XHCIINST.EXE and XHCIIN64.EXE.
rem
rem   build.cmd        both executables
rem   build.cmd x86    XHCITR.EXE alone, for a host without tools\WinDDK71
rem
rem Nothing is installed machine-wide. XHCITR.EXE, the 32-bit build that
rem starts on Windows 98 SE, ME, 2000, XP and 32-bit Vista to 10 (on 64-bit
rem Windows it starts XHCITR64.EXE in its place), is built with the in-repo
rem MSVC 6.0 (tools\MSVC600; MSVC6 overrides it), /W3 /WX, the C runtime
rem linked statically, /SUBSYSTEM:WINDOWS,4.0. XHCITR64.EXE is the same
rem sources built with WDK 7.1's amd64 compiler (tools\WinDDK71; WDK71
rem overrides it), /MT, /SUBSYSTEM:WINDOWS,5.02, for XP x64 and 64-bit
rem Vista to 10. Both import KERNEL32, USER32, GDI32, ADVAPI32, SHELL32,
rem COMCTL32 and COMDLG32 alone, and each function each imports is a row of
rem xhcitr-imports.allow (selftest.cmd holds them to it); every SetupAPI and
rem configuration-manager routine is looked up at run time.
rem
rem The sources: xhcitr.c (the system half), tr_core.c (the pure half the
rem host suite test\test_tr.c also links), tr_red.c (the red tray icon's
rem decision and tooltip, test\test_trred.c's), tr_pci.c (the PCI configuration
rem nodes' pure half, test\test_trpci.c's), tr_diag.c (the PCI and bridge
rem diagnostics node's pure half, task 44.62, test\test_trdiag.c's),
rem tr_acpi.c (the firmware tables' pure half, task 46.21, test\test_tracpi.c's),
rem tr_log.c (the driver logs' pure half, test\test_trlog.c's), tr_pop.c and tr_popw.c (the 98 SE pop-up in
rem the balloon's place, task 43.55: its pure half, test\test_trpop.c's, and
rem its window), tr_snap.c (XHCISNAP's reading, moved here
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
cl /nologo /W3 /WX /O2 /DXHCI_HOST_TEST /c /Fotr_psw.obj ..\src\xhci_psw.c
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /DXHCI_HOST_TEST /c /Fotr_pirq.obj ..\src\xhci_pirq.c
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /FeXHCITR.EXE xhcitr.c tr_core.c tr_set.c tr_red.c tr_log.c tr_pci.c tr_net.c tr_netw.c tr_diag.c tr_acpi.c tr_pop.c tr_popw.c tr_snap.c tr_cms.c tr_cmsw.c tr_ctx.c ..\xhciinst\inst_core.c ..\xhciinst\inst_silent.c ..\xhciinst\inst_ej9x.c ..\xhcimark\mark_core.c tr_psw.obj tr_pirq.obj xhcitr.res %LIBS% /link /SUBSYSTEM:WINDOWS,4.0
if errorlevel 1 exit /b 1
if not exist XHCITR.EXE exit /b 1
endlocal
del /q xhcitr.obj tr_core.obj tr_set.obj tr_red.obj tr_log.obj tr_pci.obj tr_net.obj tr_netw.obj tr_diag.obj tr_acpi.obj tr_pop.obj tr_popw.obj tr_snap.obj tr_cms.obj tr_cmsw.obj tr_ctx.obj inst_core.obj inst_silent.obj inst_ej9x.obj mark_core.obj tr_psw.obj tr_pirq.obj xhcitr.res 2>nul
echo Built XHCITR.EXE

if "%WANT64%"=="0" goto done

rem ---- XHCITR64.EXE: WDK 7.1, amd64 ----------------------------------------
setlocal
set "PATH=%WDK71%\bin\x86\amd64;%WDK71%\bin\x86;%PATH%"
set "INCLUDE=%WDK71%\inc\api;%WDK71%\inc\crt"
set "LIB=%WDK71%\lib\wnet\amd64;%WDK71%\lib\crt\amd64"
rc /i "%WDK71%\inc\api" /fo xhcitr64.res xhcitr.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /DXHCI_HOST_TEST /c /Fotr_psw.obj ..\src\xhci_psw.c
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /DXHCI_HOST_TEST /c /Fotr_pirq.obj ..\src\xhci_pirq.c
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /FeXHCITR64.EXE xhcitr.c tr_core.c tr_set.c tr_red.c tr_log.c tr_pci.c tr_net.c tr_netw.c tr_diag.c tr_acpi.c tr_pop.c tr_popw.c tr_snap.c tr_cms.c tr_cmsw.c tr_ctx.c ..\xhciinst\inst_core.c ..\xhciinst\inst_silent.c ..\xhciinst\inst_ej9x.c ..\xhcimark\mark_core.c tr_psw.obj tr_pirq.obj xhcitr64.res %LIBS% /link /SUBSYSTEM:WINDOWS,5.02 /MACHINE:X64
if errorlevel 1 exit /b 1
if not exist XHCITR64.EXE exit /b 1
endlocal
del /q xhcitr.obj tr_core.obj tr_set.obj tr_red.obj tr_log.obj tr_pci.obj tr_net.obj tr_netw.obj tr_diag.obj tr_acpi.obj tr_pop.obj tr_popw.obj tr_snap.obj tr_cms.obj tr_cmsw.obj tr_ctx.obj inst_core.obj inst_silent.obj inst_ej9x.obj mark_core.obj tr_psw.obj tr_pirq.obj xhcitr64.res 2>nul
echo Built XHCITR64.EXE

:done
echo.
exit /b 0
