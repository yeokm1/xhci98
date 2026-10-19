@echo off
rem build.cmd - build XHCIMARK.EXE and XHCIMK64.EXE, the safe-removal mark
rem service (roadmap-usb4.md task 42-A.5; design record 21 section 7.4,
rem revision 3).
rem
rem   build.cmd        both executables
rem   build.cmd x86    XHCIMARK.EXE alone, for a host without tools\WinDDK71
rem
rem Nothing is installed machine-wide. XHCIMARK.EXE, for 32-bit Windows 2000,
rem XP, Vista and 7, is built with the in-repo MSVC 6.0 (tools\MSVC600; MSVC6
rem overrides it); XHCIMK64.EXE, for XP x64, Vista x64 and 7 x64, with WDK
rem 7.1's amd64 compiler (tools\WinDDK71; WDK71 overrides it), as XHCICLN's
rem pair is. Both link the C runtime statically and import KERNEL32 and
rem ADVAPI32 alone; every SetupAPI and configuration-manager routine is
rem looked up at run time. The sources: xhcimark.c (the service), mark_core.c
rem (its pure half, which -selftest runs) and ..\xhciinst\inst_core.c,
rem record 24's protected rule and the status record's check.
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
    echo        build.cmd x86 for XHCIMARK.EXE alone.
    exit /b 1
)

if exist XHCIMARK.EXE del XHCIMARK.EXE
if exist XHCIMK64.EXE del XHCIMK64.EXE
if exist xhcimark.res del xhcimark.res
if exist xhcimk64.res del xhcimk64.res

rem ---- XHCIMARK.EXE: MSVC 6.0 ----------------------------------------------
setlocal
set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"
rc /fo xhcimark.res xhcimark.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /FeXHCIMARK.EXE xhcimark.c mark_core.c ..\xhciinst\inst_core.c xhcimark.res advapi32.lib /link /SUBSYSTEM:CONSOLE,4.0
if errorlevel 1 exit /b 1
if not exist XHCIMARK.EXE exit /b 1
endlocal
del /q xhcimark.obj mark_core.obj inst_core.obj xhcimark.res 2>nul
echo Built XHCIMARK.EXE

if "%WANT64%"=="0" goto done

rem ---- XHCIMK64.EXE: WDK 7.1, amd64 ----------------------------------------
setlocal
set "PATH=%WDK71%\bin\x86\amd64;%WDK71%\bin\x86;%PATH%"
set "INCLUDE=%WDK71%\inc\api;%WDK71%\inc\crt"
set "LIB=%WDK71%\lib\wnet\amd64;%WDK71%\lib\crt\amd64"
rc /i "%WDK71%\inc\api" /fo xhcimk64.res xhcimark.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /FeXHCIMK64.EXE xhcimark.c mark_core.c ..\xhciinst\inst_core.c xhcimk64.res advapi32.lib /link /SUBSYSTEM:CONSOLE,5.02 /MACHINE:X64
if errorlevel 1 exit /b 1
if not exist XHCIMK64.EXE exit /b 1
endlocal
del /q xhcimark.obj mark_core.obj inst_core.obj xhcimk64.res 2>nul
echo Built XHCIMK64.EXE

:done
echo.
exit /b 0
