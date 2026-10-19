@echo off
rem SPDX-License-Identifier: BSD-3-Clause
rem build.cmd - builds cm9x.sys (Windows 2000 DDK, WDM) and cm9xctl.exe (MSVC 6.0)
rem with the kits in ..\..\tools (docs\BUILD.md). Output: out\cm9x\.
setlocal
for %%I in ("%~dp0..\..") do set "REPO=%%~fI"
rem The kits root, as scripts\build.cmd finds it (STOR98_TOOLS)
if not defined STOR98_TOOLS if exist "%REPO%\tools\" set "STOR98_TOOLS=%REPO%\tools"
if not defined STOR98_TOOLS for %%I in ("%REPO%\..\..\tools") do set "STOR98_TOOLS=%%~fI"
set "DDKROOT=%STOR98_TOOLS%\ntddk"
cmd /c "call %DDKROOT%\bin\setenv.bat %DDKROOT% free w2k x86 >nul && set "BUILD_ALT_DIR=fre" && cd /d "%~dp0" && build -cZ"
if not exist "%~dp0objfre\i386\cm9x.sys" (
    echo cm9x build failed
    if exist "%~dp0buildfre.err" type "%~dp0buildfre.err"
    exit /b 1
)
if not exist "%REPO%\out\cm9x" mkdir "%REPO%\out\cm9x"
copy /y "%~dp0objfre\i386\cm9x.sys" "%REPO%\out\cm9x\cm9x.sys" >nul
cmd /c "call %DDKROOT%\bin\ddkvars.bat && cd /d "%~dp0" && cl /nologo /W3 /WX /O2 /MT cm9xctl.c advapi32.lib /Fe"%REPO%\out\cm9x\cm9xctl.exe" /Fo"%~dp0objfre\cm9xctl.obj""
if errorlevel 1 (
    echo cm9xctl build failed
    exit /b 1
)
echo Built "%REPO%\out\cm9x\cm9x.sys" and cm9xctl.exe
