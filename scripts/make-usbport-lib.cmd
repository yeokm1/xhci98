@echo off
rem make-usbport-lib.cmd - generate src\usbport.lib, the import library for
rem usbport.sys's private exports.
rem
rem USBPORT_RegisterUSBPortDriver and USBPORT_GetHciMn are private exports;
rem the Windows 2000 DDK ships no import library for them, so src\sources
rem names this generated one in TARGETLIBS. It is a build artifact (*.lib is
rem git-ignored) - regenerate it after a fresh clone, before `build`.
rem
rem Usage:  scripts\make-usbport-lib.cmd [reference-usbport.sys]
rem
rem No reference binary is required for a fresh checkout: the tracked
rem usbport-imports.expected manifest records the exact two names verified
rem against all three supported lineages.
rem
rem A reference usbport.sys adds an independent exact-name check against that
rem binary's own export table - real evidence rather than self-consistency
rem between two tracked files. The script therefore takes the strongest check
rem available without being asked: argument 1 if given, else the first known
rem extracted binary that happens to be present, else manifest-only. It always
rem prints which of the three ran, so a weaker check is never silent.
rem
rem The reference is only ever *read*. The lib's contents always come from the
rem tracked stub DLL sources, so the choice of lineage cannot change the lib.
rem
rem Method (docs\contributing\build-and-test.md "Build Files"): compile __stdcall stubs,
rem link them into a throwaway DLL whose .def exports plain names, and keep
rem the /implib. Plain `lib /def:` cannot be used - it emits cdecl-style
rem symbols an NTAPI-prototyped miniport cannot link against.
rem
rem Exit code 0 = lib built and verified.

setlocal

set "REPO=%~dp0.."
set "STUBDIR=%~dp0usbport-lib"
set "OUTLIB=%REPO%\src\usbport.lib"
set "DEFFILE=%STUBDIR%\usbport-stub.def"
set "EXPECTED=%STUBDIR%\usbport-imports.expected"
set "VERIFY=%STUBDIR%\verify-exports.ps1"
set "COMMON=%~dp0common.ps1"
set "TMPID=%RANDOM%-%RANDOM%"
set "TMPLIB=%REPO%\src\usbport-%TMPID%.lib"
set "TMPEXP=%REPO%\src\usbport-%TMPID%.exp"
set "STUBOBJ=%TEMP%\xhci98-usbport-stub-%TMPID%.obj"
set "STUBSYS=%TEMP%\xhci98-usbport-stub-%TMPID%.sys"
set "LINKOBJ=%TEMP%\xhci98-usbport-linktest-%TMPID%.obj"
set "LINKSYS=%TEMP%\xhci98-usbport-linktest-%TMPID%.sys"
set "EXPORTDUMP=%TEMP%\xhci98-usbport-exports-%TMPID%.txt"
set "LIBDUMP=%TEMP%\xhci98-usbport-lib-%TMPID%.txt"
set "HEADERDUMP=%TEMP%\xhci98-usbport-hdr-%TMPID%.txt"
set "IMPORTDUMP=%TEMP%\xhci98-linktest-%TMPID%.txt"

set "REFSYS=%~1"

if "%MSVC6%"=="" set "MSVC6=%REPO%\tools\MSVC600"
if not exist "%MSVC6%\VC98\BIN\cl.exe" goto nocompiler

rem Quoted assignments throughout (set "VAR=value"). The unquoted form takes
rem the rest of the line verbatim, so a repo or TEMP path carrying a trailing
rem space or a metacharacter (&, ^, parentheses) corrupts the variable or the
rem enclosing command line. Spaces alone are survivable; those are not.
set "PATH=%MSVC6%\VC98\BIN;%MSVC6%\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%MSVC6%\VC98\INCLUDE"
set "LIB=%MSVC6%\VC98\LIB"

if not exist "%DEFFILE%" goto missinginput
if not exist "%EXPECTED%" goto missinginput
if not exist "%VERIFY%" goto missinginput
if not exist "%COMMON%" goto missinginput

rem --- 1. The .def must exactly match the tracked expected-export manifest.
rem Where a reference binary is available, require those exact names in its
rem export table too; substring matches such as USBPORT_GetHciMnEx do not pass.
rem
rem Take the strongest check available rather than the one that was asked for:
rem an explicit argument wins, otherwise use a known extracted binary if this
rem working copy happens to have one. Those live under tools\, which is
rem git-ignored, so a fresh clone legitimately has none - that case falls back
rem to the manifest and says so instead of failing.
if not "%REFSYS%"=="" goto haveref
for %%R in (
    "%REPO%\tools\nusb-extracted\USBPORT.SYS"
    "%REPO%\tools\win2ksp4-extracted\USBPORT.SYS"
    "%REPO%\tools\winxpsp3-extracted\usbport.sys"
) do if not defined REFSYS if exist %%R set "REFSYS=%%~R"
if not defined REFSYS goto verifytracked
echo Checking exact exports against auto-discovered "%REFSYS%" ...
goto dumpref

:haveref
if not exist "%REFSYS%" goto norefsys
echo Checking exact exports against "%REFSYS%" ...

:dumpref
dumpbin /exports "%REFSYS%" > "%EXPORTDUMP%" 2>&1
if errorlevel 1 goto dumpfail
powershell -NoProfile -ExecutionPolicy Bypass -File "%VERIFY%" ^
    -DefPath "%DEFFILE%" -ExpectedPath "%EXPECTED%" -DumpPath "%EXPORTDUMP%"
if errorlevel 1 goto badexports
goto exportsok

:verifytracked
echo Checking tracked USBPORT export manifest (no reference binary present) ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%VERIFY%" ^
    -DefPath "%DEFFILE%" -ExpectedPath "%EXPECTED%"
if errorlevel 1 goto badexports

:exportsok

rem --- 2. Build the stub DLL and keep its import library.
rem LNK4070 ("/OUT:USBPORT.SYS directive in .EXP differs from output filename")
rem is expected and is the point: the .def's LIBRARY name is what gets baked
rem into the import library, while the throwaway DLL is named otherwise.
cl /nologo /c /W3 /Za /Fo"%STUBOBJ%" "%STUBDIR%\usbport-stub.c"
if errorlevel 1 goto builderr
link /nologo /DLL /NOENTRY /NODEFAULTLIB /machine:ix86 ^
     /def:"%DEFFILE%" "%STUBOBJ%" ^
     /out:"%STUBSYS%" /implib:"%TMPLIB%"
if errorlevel 1 goto builderr
if not exist "%TMPLIB%" goto builderr

rem --- 3. The lib must carry the decorated stdcall symbols, not cdecl ones.
dumpbin /linkermember:1 "%TMPLIB%" > "%LIBDUMP%" 2>&1
findstr /c:"_USBPORT_GetHciMn@0" "%LIBDUMP%" >nul || goto baddecoration
findstr /c:"_USBPORT_RegisterUSBPortDriver@12" "%LIBDUMP%" >nul || goto baddecoration

rem --- 4. ...and must name USBPORT.SYS as the providing module. A matching
rem symbol from the wrong module does not satisfy the PE import descriptor.
dumpbin /headers "%TMPLIB%" > "%HEADERDUMP%" 2>&1
findstr /c:"DLL name     : USBPORT.SYS" "%HEADERDUMP%" >nul || goto badmodule

rem --- 5. Prove an NTAPI-prototyped caller actually links against it, and
rem that the resulting image imports both names from USBPORT.SYS.
cl /nologo /c /W3 /Za /Fo"%LINKOBJ%" "%STUBDIR%\linktest.c"
if errorlevel 1 goto linktesterr
link /nologo /driver /subsystem:native /entry:DriverEntry@8 /NODEFAULTLIB ^
     /machine:ix86 "%LINKOBJ%" "%TMPLIB%" /out:"%LINKSYS%"
if errorlevel 1 goto linktesterr
dumpbin /imports "%LINKSYS%" > "%IMPORTDUMP%" 2>&1
findstr /c:"USBPORT.SYS" "%IMPORTDUMP%" >nul || goto linktesterr
findstr /c:"USBPORT_GetHciMn" "%IMPORTDUMP%" >nul || goto linktesterr
findstr /c:"USBPORT_RegisterUSBPortDriver" "%IMPORTDUMP%" >nul || goto linktesterr

rem Publish only the fully verified library. Until this move, a failure cannot
rem overwrite the last known-good src\usbport.lib with a partial artifact.
move /y "%TMPLIB%" "%OUTLIB%" >nul
if errorlevel 1 goto publisherr
call :cleanup
echo.
echo Built and verified "%OUTLIB%"
echo   decorated symbols  : _USBPORT_GetHciMn@0, _USBPORT_RegisterUSBPortDriver@12
echo   providing module   : USBPORT.SYS
echo   NTAPI link proof   : PASSED
if defined REFSYS goto summaryref
echo   export evidence    : tracked manifest only - no reference usbport.sys
echo                        in this working copy, so nothing checked the
echo                        names against a real binary.
goto summarydone

:summaryref
echo   export evidence    : exact names read out of "%REFSYS%"

:summarydone
endlocal
exit /b 0

:nocompiler
echo.
echo ERROR: cl.exe not found under "%MSVC6%".
echo Run scripts\setup-msvc6.ps1, or set MSVC6 to the extracted MSVC 6.0 root.
endlocal
exit /b 1

:missinginput
echo.
echo ERROR: tracked usbport-lib generator input is missing.
call :cleanup
endlocal
exit /b 1

:norefsys
echo.
echo ERROR: reference binary not found: "%REFSYS%"
echo Omit argument 1 to use the tracked exact-export manifest.
call :cleanup
endlocal
exit /b 1

:dumpfail
echo.
echo ERROR: dumpbin could not read "%REFSYS%".
type "%EXPORTDUMP%"
call :cleanup
endlocal
exit /b 1

:badexports
echo.
echo ERROR: the DEF, expected-export manifest, or optional reference binary
echo failed exact-name validation. Do not paper over this ABI mismatch.
call :cleanup
endlocal
exit /b 1

:builderr
echo.
echo ERROR: could not build the stub import library.
call :cleanup
endlocal
exit /b 1

:baddecoration
echo.
echo ERROR: the staged import library lacks the decorated stdcall symbols.
echo That is the signature of a lib built with plain `lib /def:` - see
echo docs\contributing\build-and-test.md "Build Files".
call :cleanup
endlocal
exit /b 1

:badmodule
echo.
echo ERROR: the staged import library does not name USBPORT.SYS as the
echo providing module.
call :cleanup
endlocal
exit /b 1

:linktesterr
echo.
echo ERROR: an NTAPI-prototyped caller could not link against the staged lib,
echo or the linked image does not import both names from USBPORT.SYS.
call :cleanup
endlocal
exit /b 1

:publisherr
echo.
echo ERROR: verified import library could not be published to "%OUTLIB%".
call :cleanup
endlocal
exit /b 1

:cleanup
if exist "%TMPLIB%" del "%TMPLIB%"
if exist "%TMPEXP%" del "%TMPEXP%"
if exist "%STUBOBJ%" del "%STUBOBJ%"
if exist "%STUBSYS%" del "%STUBSYS%"
if exist "%LINKOBJ%" del "%LINKOBJ%"
if exist "%LINKSYS%" del "%LINKSYS%"
if exist "%EXPORTDUMP%" del "%EXPORTDUMP%"
if exist "%LIBDUMP%" del "%LIBDUMP%"
if exist "%HEADERDUMP%" del "%HEADERDUMP%"
if exist "%IMPORTDUMP%" del "%IMPORTDUMP%"
goto :eof
