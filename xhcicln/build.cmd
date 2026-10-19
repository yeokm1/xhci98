@echo off
rem build.cmd - build XHCICLN.EXE and XHCICL64.EXE (roadmap-hcd.md task
rem 38.8; design record 13 section 10.13.8).
rem
rem   build.cmd        both executables
rem   build.cmd x86    XHCICLN.EXE alone, for a host without tools\WinDDK71
rem
rem Nothing is installed machine-wide. XHCICLN.EXE, the 32-bit build that
rem starts on Windows 98 SE, ME, 2000, XP and 32-bit Vista and 7, is built
rem with the in-repo MSVC 6.0 (tools\MSVC600; MSVC6 overrides it), as
rem XHCISNAP.EXE is: ANSI C89 kept by hand, /W3 /WX, no /Za because the SDK
rem headers do not compile under it (xhcisnap\build.cmd says why).
rem XHCICL64.EXE is the same source built a second time with WDK 7.1's amd64
rem compiler (tools\WinDDK71; WDK71 overrides it), as xhci98.sys amd64 is,
rem for Windows XP x64, Vista x64 and 7 x64: a 32-bit program cannot remove
rem a device on 64-bit Windows. Both link the C runtime statically (MSVC
rem 6.0's LIBC, WDK 7.1's LIBCMT) and import KERNEL32 and ADVAPI32 alone;
rem every SetupAPI and configuration-manager routine is looked up at run
rem time. Both embed xhcicln.manifest through xhcicln.rc.
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
    echo        build.cmd x86 for XHCICLN.EXE alone.
    exit /b 1
)

if exist XHCICLN.EXE del XHCICLN.EXE
if exist XHCICL64.EXE del XHCICL64.EXE
if exist xhcicln.res del xhcicln.res
if exist xhcicl64.res del xhcicl64.res

rem ---- XHCICLN.EXE: MSVC 6.0 ----------------------------------------------
rem MSPDB60.DLL and RC.EXE live under Common\MSDev98\Bin, not VC98\Bin.
setlocal
set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"
rc /fo xhcicln.res xhcicln.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /FeXHCICLN.EXE xhcicln.c ..\xhciinst\inst_core.c xhcicln.res advapi32.lib /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
if not exist XHCICLN.EXE exit /b 1
endlocal
del /q xhcicln.obj inst_core.obj xhcicln.res 2>nul
echo Built XHCICLN.EXE

if "%WANT64%"=="0" goto done

rem ---- XHCICL64.EXE: WDK 7.1, amd64 -----------------------------------------
rem The cross compiler under bin\x86\amd64 takes mspdb80.dll and rc.exe from
rem bin\x86; the subsystem version 5.02 lets it start on XP x64 and Server
rem 2003 x64.
setlocal
set "PATH=%WDK71%\bin\x86\amd64;%WDK71%\bin\x86;%PATH%"
set "INCLUDE=%WDK71%\inc\api;%WDK71%\inc\crt"
set "LIB=%WDK71%\lib\wnet\amd64;%WDK71%\lib\crt\amd64"
rc /i "%WDK71%\inc\api" /fo xhcicl64.res xhcicln.rc
if errorlevel 1 exit /b 1
cl /nologo /W3 /WX /O2 /MT /FeXHCICL64.EXE xhcicln.c ..\xhciinst\inst_core.c xhcicl64.res advapi32.lib /link /SUBSYSTEM:CONSOLE,5.02 /MACHINE:X64
if errorlevel 1 exit /b 1
if not exist XHCICL64.EXE exit /b 1
endlocal
del /q xhcicln.obj inst_core.obj xhcicl64.res 2>nul
echo Built XHCICL64.EXE

:done
echo.
exit /b 0
