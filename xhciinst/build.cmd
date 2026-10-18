@echo off
rem build.cmd - build XHCIINST.EXE and XHCIIN64.EXE (roadmap-usb4.md task
rem 40.6; design record 23 section 3), exactly as xhcicln\build.cmd builds
rem XHCICLN.EXE and XHCICL64.EXE.
rem
rem   build.cmd        both executables
rem   build.cmd x86    XHCIINST.EXE alone, for a host without tools\WinDDK71
rem
rem Nothing is installed machine-wide. XHCIINST.EXE, the 32-bit build that
rem starts on Windows 98 SE, ME, 2000, XP and 32-bit Vista and 7, is built
rem with the in-repo MSVC 6.0 (tools\MSVC600; MSVC6 overrides it), /W3 /WX,
rem the C runtime linked statically (LIBC, no /MD). XHCIIN64.EXE is the same
rem sources built a second time with WDK 7.1's amd64 compiler (tools\WinDDK71;
rem WDK71 overrides it), /MT, for XP x64, Vista x64 and 7 x64: a 32-bit
rem program cannot install a driver on 64-bit Windows. Both import KERNEL32
rem and ADVAPI32 alone (selftest.cmd holds them to it); every SetupAPI,
rem newdev and configuration-manager routine is looked up at run time. Both
rem embed xhciinst.manifest through xhciinst.rc.
rem
rem The sources: xhciinst.c (the system half), inst_core.c (the pure half the
rem host suite test\test_inst.c also links) and ..\src\xhci_psw.c, the
rem driver's own port-switchover gate, compiled as the host suites compile
rem it (XHCI_HOST_TEST), so the list of Intel controllers exists once.
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
    echo        build.cmd x86 for XHCIINST.EXE alone.
    exit /b 1
)

if exist XHCIINST.EXE del XHCIINST.EXE
if exist XHCIIN64.EXE del XHCIIN64.EXE
if exist xhciinst.res del xhciinst.res
if exist xhciin64.res del xhciin64.res

rem ---- XHCIINST.EXE: MSVC 6.0 ----------------------------------------------
setlocal
set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"
rc /fo xhciinst.res xhciinst.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /DXHCI_HOST_TEST /c /Foinst_psw.obj ..\src\xhci_psw.c
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /FeXHCIINST.EXE xhciinst.c inst_eng.c inst_core.c inst_psw.obj xhciinst.res advapi32.lib /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
if not exist XHCIINST.EXE exit /b 1
endlocal
del /q xhciinst.obj inst_eng.obj inst_core.obj inst_psw.obj xhciinst.res 2>nul
echo Built XHCIINST.EXE

if "%WANT64%"=="0" goto done

rem ---- XHCIIN64.EXE: WDK 7.1, amd64 -----------------------------------------
setlocal
set "PATH=%WDK71%\bin\x86\amd64;%WDK71%\bin\x86;%PATH%"
set "INCLUDE=%WDK71%\inc\api;%WDK71%\inc\crt"
set "LIB=%WDK71%\lib\wnet\amd64;%WDK71%\lib\crt\amd64"
rc /i "%WDK71%\inc\api" /fo xhciin64.res xhciinst.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /DXHCI_HOST_TEST /c /Foinst_psw64.obj ..\src\xhci_psw.c
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /FeXHCIIN64.EXE xhciinst.c inst_eng.c inst_core.c inst_psw64.obj xhciin64.res advapi32.lib /link /SUBSYSTEM:CONSOLE,5.02 /MACHINE:X64
if errorlevel 1 exit /b 1
if not exist XHCIIN64.EXE exit /b 1
endlocal
del /q xhciinst.obj inst_eng.obj inst_core.obj inst_psw64.obj xhciin64.res 2>nul
echo Built XHCIIN64.EXE

:done
echo.
exit /b 0
