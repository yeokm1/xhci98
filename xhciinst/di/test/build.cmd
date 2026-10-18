@echo off
rem build.cmd - build DITEST.EXE, XHCIDI.EXE's test driver for the 42-V.1
rem legs (ditest.c says what it does), with the in-repo MSVC 6.0
rem (tools\MSVC600; MSVC6 overrides it), /W3 /WX, the C runtime linked
rem statically. Never packaged.
setlocal
cd /d "%~dp0"
set "REPO=%~dp0..\..\.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
if "%MSVC6%"=="" set "MSVC6=%REPO%\tools\MSVC600"
if not exist "%MSVC6%\VC98\Bin\cl.exe" (
    echo ERROR: no cl.exe under %MSVC6%\VC98\Bin
    exit /b 1
)
set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"
if exist DITEST.EXE del DITEST.EXE
cl /nologo /W3 /WX /O2 /FeDITEST.EXE ditest.c user32.lib advapi32.lib /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
del /q ditest.obj 2>nul
echo Built DITEST.EXE
exit /b 0
