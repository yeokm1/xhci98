@echo off
rem build.cmd - build XFERTEST.EXE, the transfer-size test client (roadmap-hcd
rem tasks 36.21, 36.22 and 36.26; xfertest.c says what it does), with the
rem in-tree MSVC 6.0, as xhcisnap\build.cmd builds XHCISNAP.EXE. The EXE is
rem not tracked and not in any package: it is copied onto a guest's transfer
rem drive for a leg.
setlocal
cd /d "%~dp0"

set "REPO=%~dp0..\.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
if "%MSVC6%"=="" set "MSVC6=%REPO%\tools\MSVC600"
if not exist "%MSVC6%\VC98\Bin\cl.exe" (
    echo ERROR: no cl.exe under %MSVC6%\VC98\Bin
    exit /b 1
)

set "PATH=%MSVC6%\VC98\Bin;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\Include"
set "LIB=%MSVC6%\VC98\Lib"

if exist XFERTEST.EXE del XFERTEST.EXE
cl /nologo /W3 /WX /O2 /FeXFERTEST.EXE xfertest.c /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
if not exist XFERTEST.EXE exit /b 1
del /q xfertest.obj 2>nul
echo Built XFERTEST.EXE
exit /b 0
