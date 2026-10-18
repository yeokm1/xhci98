@echo off
rem SPDX-License-Identifier: BSD-3-Clause
rem build.cmd - builds cmnt.sys (Windows 2000 DDK, x86), the test-only NT
rem link-state table stand-in, with the kits in ..\..\tools. Output: out\cmnt\.
rem A failed build never leaves an earlier image in place (Codex round 3).
setlocal
for %%I in ("%~dp0..\..") do set "REPO=%%~fI"
set "DDKROOT=%REPO%\tools\ntddk"
if exist "%~dp0objfre\i386\cmnt.sys" del /f /q "%~dp0objfre\i386\cmnt.sys"
if exist "%REPO%\out\cmnt\cmnt.sys" del /f /q "%REPO%\out\cmnt\cmnt.sys"
if exist "%~dp0buildfre.err" del /f /q "%~dp0buildfre.err"
cmd /c "call %DDKROOT%\bin\setenv.bat %DDKROOT% free w2k x86 >nul && set "BUILD_ALT_DIR=fre" && cd /d "%~dp0" && build -cZ"
if errorlevel 1 goto failed
if exist "%~dp0buildfre.err" goto failed
if not exist "%~dp0objfre\i386\cmnt.sys" goto failed
if not exist "%REPO%\out\cmnt" mkdir "%REPO%\out\cmnt"
copy /y "%~dp0objfre\i386\cmnt.sys" "%REPO%\out\cmnt\cmnt.sys" >nul || goto failed
echo Built "%REPO%\out\cmnt\cmnt.sys"
exit /b 0
:failed
echo cmnt build failed
if exist "%~dp0buildfre.err" type "%~dp0buildfre.err"
exit /b 1
