@echo off
rem build.cmd - build the installer's five executables (roadmap-usb4.md
rem tasks 40.6, 42-A.2 and 42-A.31; design record 23 section 3, record 26
rem sections 2 and 4): the console tool XHCIINST.EXE and XHCIIN64.EXE, as
rem xhcicln\build.cmd builds XHCICLN.EXE and XHCICL64.EXE, the window
rem XHCIWIZ.EXE and XHCIWZ64.EXE, as xhcitr\build.cmd builds XHCITR.EXE,
rem and SETUP.EXE, the package root's starter of the window (setup.c), one
rem 32-bit image for every target.
rem
rem   build.cmd        all five
rem   build.cmd x86    XHCIINST.EXE, XHCIWIZ.EXE and SETUP.EXE alone, for a
rem                    host without tools\WinDDK71
rem
rem Nothing is installed machine-wide. The 32-bit pair, which starts on
rem Windows 98 SE, ME, 2000, XP and 32-bit Vista and 7, is built with the
rem in-repo MSVC 6.0 (tools\MSVC600; MSVC6 overrides it), /W3 /WX, the C
rem runtime linked statically: LIBC for the console tool, LIBCMT (/MT) for
rem the window, whose engine runs on a worker thread. The x64 pair is the
rem same sources built a second time with WDK 7.1's amd64 compiler
rem (tools\WinDDK71; WDK71 overrides it), /MT, for XP x64, Vista x64 and 7
rem x64: a 32-bit program cannot install a driver on 64-bit Windows. The
rem console tool imports KERNEL32 and ADVAPI32 alone, the window those and
rem USER32, GDI32, COMCTL32, COMDLG32 and SHELL32, SETUP.EXE KERNEL32,
rem USER32 and SHELL32 (selftest.cmd holds them to it); every SetupAPI, newdev and configuration-manager routine is
rem looked up at run time. Each embeds its manifest through its .rc.
rem
rem The sources: xhciinst.c (the console front end), xhciwiz.c (the window),
rem inst_eng.c (the engine both drive), inst_core.c (the pure half the host
rem suite test\test_inst.c also links) and ..\src\xhci_psw.c, the driver's
rem own port-switchover gate, compiled as the host suites compile it
rem (XHCI_HOST_TEST), so the list of Intel controllers exists once. The 98
rem SE and ME helper XHCIDI.EXE (record 27) has its own build in di\.
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
    echo        build.cmd x86 for the 32-bit pair alone.
    exit /b 1
)

for %%F in (XHCIINST.EXE XHCIIN64.EXE XHCIWIZ.EXE XHCIWZ64.EXE SETUP.EXE setup.res xhciinst.res xhciin64.res xhciwiz.res xhciwz64.res) do if exist %%F del %%F

set "WLIBS=user32.lib gdi32.lib advapi32.lib comctl32.lib comdlg32.lib shell32.lib"

rem ---- XHCIINST.EXE and XHCIWIZ.EXE: MSVC 6.0 ------------------------------
setlocal
set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"
rc /fo xhciinst.res xhciinst.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /DXHCI_HOST_TEST /c /Foinst_psw.obj ..\src\xhci_psw.c
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /FeXHCIINST.EXE xhciinst.c inst_eng.c inst_core.c inst_ej9x.c inst_psw.obj xhciinst.res advapi32.lib /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
if not exist XHCIINST.EXE exit /b 1
del /q xhciinst.obj inst_eng.obj inst_core.obj inst_ej9x.obj inst_psw.obj xhciinst.res 2>nul
echo Built XHCIINST.EXE
rc /fo xhciwiz.res xhciwiz.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /DXHCI_HOST_TEST /c /Foinst_psw.obj ..\src\xhci_psw.c
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /FeXHCIWIZ.EXE xhciwiz.c inst_eng.c inst_core.c inst_ej9x.c inst_psw.obj xhciwiz.res %WLIBS% /link /SUBSYSTEM:WINDOWS,4.0
if errorlevel 1 exit /b 1
if not exist XHCIWIZ.EXE exit /b 1
endlocal
del /q xhciwiz.obj inst_eng.obj inst_core.obj inst_ej9x.obj inst_psw.obj xhciwiz.res 2>nul
echo Built XHCIWIZ.EXE
setlocal
set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"
rc /fo setup.res setup.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /FeSETUP.EXE setup.c setup.res user32.lib shell32.lib /link /SUBSYSTEM:WINDOWS,4.0
if errorlevel 1 exit /b 1
if not exist SETUP.EXE exit /b 1
endlocal
del /q setup.obj setup.res 2>nul
echo Built SETUP.EXE

if "%WANT64%"=="0" goto done

rem ---- XHCIIN64.EXE and XHCIWZ64.EXE: WDK 7.1, amd64 ------------------------
setlocal
set "PATH=%WDK71%\bin\x86\amd64;%WDK71%\bin\x86;%PATH%"
set "INCLUDE=%WDK71%\inc\api;%WDK71%\inc\crt"
set "LIB=%WDK71%\lib\wnet\amd64;%WDK71%\lib\crt\amd64"
rc /i "%WDK71%\inc\api" /fo xhciin64.res xhciinst.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /DXHCI_HOST_TEST /c /Foinst_psw64.obj ..\src\xhci_psw.c
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /FeXHCIIN64.EXE xhciinst.c inst_eng.c inst_core.c inst_ej9x.c inst_psw64.obj xhciin64.res advapi32.lib /link /SUBSYSTEM:CONSOLE,5.02 /MACHINE:X64
if errorlevel 1 exit /b 1
if not exist XHCIIN64.EXE exit /b 1
del /q xhciinst.obj inst_eng.obj inst_core.obj inst_ej9x.obj xhciin64.res 2>nul
echo Built XHCIIN64.EXE
rc /i "%WDK71%\inc\api" /fo xhciwz64.res xhciwiz.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /FeXHCIWZ64.EXE xhciwiz.c inst_eng.c inst_core.c inst_ej9x.c inst_psw64.obj xhciwz64.res %WLIBS% /link /SUBSYSTEM:WINDOWS,5.02 /MACHINE:X64
if errorlevel 1 exit /b 1
if not exist XHCIWZ64.EXE exit /b 1
endlocal
del /q xhciwiz.obj inst_eng.obj inst_core.obj inst_ej9x.obj inst_psw64.obj xhciwz64.res 2>nul
echo Built XHCIWZ64.EXE

:done
rem ---- XHCIDI.EXE: Open Watcom, 16-bit (design record 27 section 3.2) -------
rem The 98 SE and ME device install helper, last, in both forms of this
rem script: di\build.cmd answers 2 when this host has no Open Watcom, which
rem is said and is no failure here (make-release.ps1 is what refuses a cut
rem without it).
call "%~dp0di\build.cmd"
if errorlevel 2 (
    echo XHCIDI.EXE not built on this host; the package would install on 98 SE and ME through the printed steps only.
    goto donedi
)
if errorlevel 1 exit /b 1
:donedi
echo.
exit /b 0
