@echo off
rem build.cmd - build ATATEST.EXE, the pass-through test client (roadmap-usb4
rem task 45.27; atatest.c says what it does), with the in-tree MSVC 6.0, as
rem test\xfertest\build.cmd builds XFERTEST.EXE. The EXE is not tracked and
rem not in any package: it is copied onto a guest's transfer drive for a leg.
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

if exist ATATEST.EXE del ATATEST.EXE
cl /nologo /W3 /WX /O2 /FeATATEST.EXE atatest.c /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
if not exist ATATEST.EXE exit /b 1
del /q atatest.obj 2>nul
echo Built ATATEST.EXE

rem ASPI9X.EXE, the 98 SE and ME ASPI probe of task 45.52 (aspi9x.c says
rem what it does), built the same way.
if exist ASPI9X.EXE del ASPI9X.EXE
cl /nologo /W3 /WX /O2 /FeASPI9X.EXE aspi9x.c /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
if not exist ASPI9X.EXE exit /b 1
del /q aspi9x.obj 2>nul
echo Built ASPI9X.EXE

rem PTCANCEL.EXE, the pass-through cancel and removal client of task 45.52
rem (ptcancel.c says what it does), built the same way.
if exist PTCANCEL.EXE del PTCANCEL.EXE
cl /nologo /W3 /WX /O2 /FePTCANCEL.EXE ptcancel.c /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
if not exist PTCANCEL.EXE exit /b 1
del /q ptcancel.obj 2>nul
echo Built PTCANCEL.EXE
exit /b 0
