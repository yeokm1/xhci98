@echo off
rem build.cmd - build XHCIPERF.EXE and XHCIPF64.EXE, the xHCI98 Disk
rem Benchmark (roadmap-usb4.md task 43.50; design record 29 section 10), as
rem xhcitr\build.cmd builds XHCITR.EXE and XHCITR64.EXE.
rem
rem   build.cmd        both executables
rem   build.cmd x86    XHCIPERF.EXE alone, for a host without tools\WinDDK71
rem
rem Nothing is installed machine-wide. XHCIPERF.EXE, the 32-bit build that
rem starts on Windows 98 SE, ME, 2000, XP, Vista, 7, 8, 8.1 and 10, is built
rem with the in-repo MSVC 6.0 (tools\MSVC600; MSVC6 overrides it), /W3 /WX,
rem the C runtime linked statically and multithreaded (/MT: the run uses
rem threads), /SUBSYSTEM:WINDOWS,4.0. XHCIPF64.EXE is the same sources built
rem with WDK 7.1's amd64 compiler (tools\WinDDK71; WDK71 overrides it), /MT,
rem /SUBSYSTEM:WINDOWS,5.02. Both import KERNEL32, USER32, GDI32, ADVAPI32,
rem SHELL32, COMCTL32 and COMDLG32 alone (selftest.cmd holds them to it).
rem
rem The sources: xhciperf.c (the system half), pf_core.c and pf_s98.c (the
rem pure halves test\test_pf.c and test\test_pfs98.c also link). It includes
rem Stor98's interface header src\stor98\include\stor98pf.h in place.
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
    echo        build.cmd x86 for XHCIPERF.EXE alone.
    exit /b 1
)

if exist XHCIPERF.EXE del XHCIPERF.EXE
if exist XHCIPF64.EXE del XHCIPF64.EXE
if exist xhciperf.res del xhciperf.res
if exist xhciperf64.res del xhciperf64.res

set "LIBS=user32.lib gdi32.lib advapi32.lib shell32.lib comctl32.lib comdlg32.lib"

rem ---- XHCIPERF.EXE: MSVC 6.0 ----------------------------------------------
setlocal
set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"
rc /fo xhciperf.res xhciperf.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /FeXHCIPERF.EXE xhciperf.c pf_core.c pf_s98.c xhciperf.res %LIBS% /link /SUBSYSTEM:WINDOWS,4.0
if errorlevel 1 exit /b 1
if not exist XHCIPERF.EXE exit /b 1
endlocal
del /q xhciperf.obj pf_core.obj pf_s98.obj xhciperf.res 2>nul
echo Built XHCIPERF.EXE

if "%WANT64%"=="0" goto done

rem ---- XHCIPF64.EXE: WDK 7.1, amd64 ----------------------------------------
setlocal
set "PATH=%WDK71%\bin\x86\amd64;%WDK71%\bin\x86;%PATH%"
set "INCLUDE=%WDK71%\inc\api;%WDK71%\inc\crt"
set "LIB=%WDK71%\lib\wnet\amd64;%WDK71%\lib\crt\amd64"
rc /i "%WDK71%\inc\api" /fo xhciperf64.res xhciperf.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /FeXHCIPF64.EXE xhciperf.c pf_core.c pf_s98.c xhciperf64.res %LIBS% /link /SUBSYSTEM:WINDOWS,5.02 /MACHINE:X64
if errorlevel 1 exit /b 1
if not exist XHCIPF64.EXE exit /b 1
endlocal
del /q xhciperf.obj pf_core.obj pf_s98.obj xhciperf64.res 2>nul
echo Built XHCIPF64.EXE

:done
echo.
exit /b 0
