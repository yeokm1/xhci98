@echo off
rem build.cmd - build TBSPEED.EXE (the single-threaded TCP throughput
rem reading) and TBTHREAD.EXE (the thread creation report), developer
rem diagnostics that no package carries.
rem
rem Nothing is installed machine-wide. Both are 32-bit console images built
rem with the in-repo MSVC 6.0 (tools\MSVC600; MSVC6 overrides it), /W3 /WX,
rem /SUBSYSTEM:CONSOLE,4.0, the C runtime linked statically, and start on
rem Windows 98 SE to 11. TBSPEED.EXE takes the single-threaded runtime (/ML)
rem and imports KERNEL32 and WSOCK32 alone; TBTHREAD.EXE takes the
rem multithreaded one (/MT), which _beginthreadex needs, and imports
rem KERNEL32 alone. The pure half tbs_core.c is also linked by
rem test\test_tbs.c.
setlocal
cd /d "%~dp0"

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
if "%MSVC6%"=="" set "MSVC6=%REPO%\tools\MSVC600"
if not "%~1"=="" (
    echo ERROR: unknown argument %1 ^(none^)
    exit /b 1
)
if not exist "%MSVC6%\VC98\Bin\cl.exe" (
    echo ERROR: no cl.exe under %MSVC6%\VC98\Bin
    echo        Unpack tools\MSVC600.zip, or set MSVC6.
    exit /b 1
)

if exist TBSPEED.EXE del TBSPEED.EXE
if exist TBTHREAD.EXE del TBTHREAD.EXE

set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"

cl /nologo /W3 /WX /O2 /ML /FeTBSPEED.EXE tbspeed.c tbs_core.c wsock32.lib /link /SUBSYSTEM:CONSOLE,4.0
if errorlevel 1 exit /b 1
if not exist TBSPEED.EXE exit /b 1
del /q tbspeed.obj tbs_core.obj 2>nul
echo Built TBSPEED.EXE

cl /nologo /W3 /WX /O2 /MT /FeTBTHREAD.EXE tbthread.c /link /SUBSYSTEM:CONSOLE,4.0
if errorlevel 1 exit /b 1
if not exist TBTHREAD.EXE exit /b 1
del /q tbthread.obj 2>nul
echo Built TBTHREAD.EXE
echo.
exit /b 0
