@echo off
rem build-driver.cmd - the one way to build xhci98.sys.
rem
rem Non-interactive: DDK build, then the gates that must pass before a binary
rem is allowed near a VM (roadmap Phase 3 task 5):
rem
rem   1. src\usbport.lib exists (generated - see scripts\make-usbport-lib.cmd)
rem   2. the import gate's evidence-manifest regression tests, and its
rem      three-flavor FLAVORS-column tests
rem   3. the INF gate's own regression tests, then scripts\inf-gate\check-inf.ps1
rem      on src\xhci98.inf - the binary and the INF are deployed together and
rem      neither setup engine reports a malformed one usefully
rem   4. the packager's regression tests - it decides where each file lands on
rem      the install media, and a package staged at one path but authenticated
rem      at another verifies nothing
rem   5. the QEMU launcher regression tests - a stale append-only trace can
rem      falsely attribute an earlier DriverEntry to the current binary
rem   6. test\run-host-tests.cmd - the pure-core suite. It runs before the DDK
rem      builds, not after: it compiles the same core files in seconds, so a
rem      bad carve, ring or PORTSC constant should not cost two full builds
rem      first
rem   7. `build` for each requested flavor, with the compile-time layout and
rem      ABI asserts in src\xhci.h / src\xhci_usbport.h
rem   8. scripts\import-gate\check-imports.ps1 on each linked binary
rem
rem Any failure stops the run. scripts\local\ddk-debug.cmd still exists for an
rem interactive DDK prompt, but a binary built that way has not been through the
rem gates - do not deploy one.
rem
rem VOCABULARY. This project says "release", "debug" and "qemu" for the three
rem build flavors, everywhere except where the DDK's own words are literally
rem required: setenv.bat's third argument (free|checked), the src\objfre /
rem src\objchk / src\objchk_qemu output trees, and the buildfre / buildchk /
rem buildchk_qemu log names. The mapping is made once, in :flavordirs below.
rem
rem THREE FLAVORS, since roadmap task 13-L.1 (design record 08):
rem
rem   release  free     ships by default        no port-0xE9 mirror
rem   debug    checked  ships as the diagnostic download, and its DEFINING
rem                     requirement is that it LOADS on real Windows 98 and
rem                     Windows 2000 metal - so no port-0xE9 mirror either
rem   qemu     checked  the emulator/bench build: port-0xE9 mirror and the live
rem                     per-line trace.  NEVER PUBLISHED.
rem
rem debug and qemu are both CHECKED builds, so they need distinct output trees
rem or they overwrite each other's objects; BUILD_ALT_DIR is what separates
rem them (:flavordirs).  The one import that distinguishes them -
rem HAL.dll!WRITE_PORT_UCHAR - is the sole import delta between 0.0.0.4's two
rem published binaries, of which the debug one gave the ThinkPad E460 a Code 2.
rem WHY that build failed is not established (defect 2b), so the import gate
rem carries it as "qemu required" and no published binary can have it.
rem
rem This builds and gates; it does not package. The install media a VM is
rem fed is assembled by a separate explicit step, which re-runs the INF gate
rem against the finished directory so a package is never less gated than the
rem binary in it:
rem
rem   scripts\package\make-package.ps1 [-Flavor release|debug]
rem
rem The media is this project's two files and nothing else since 1.0.0.1; the
rem OS supplies usbd.sys and usbhub.sys (and, on the NT targets, usbport.sys)
rem through the INF's LayoutFile.
rem
rem Usage:  scripts\build-driver.cmd [release|debug|qemu|both|all]
rem                                                          (default: both)
rem
rem   "both" is the two SHIPPING flavors - release and debug - and it stays the
rem   default deliberately: widening it to three would build a binary that must
rem   never be published on every ordinary run.  "all" is the three, and is what
rem   a release cut uses so that every flavor is gated even though only two are
rem   staged.
rem
rem   The DDK and MSVC 6.0 both live inside this repository - tools\ntddk and
rem   tools\MSVC600 - and are found relative to this script, so a
rem   clone builds wherever it is unpacked and nothing is installed under C:\.
rem   Set DDKROOT to build against a DDK somewhere else.
rem   Add -NoTargetEvidence after the flavor to skip the gate's target-file
rem   evidence steps on a host that has none staged.
rem
rem Exit codes: 0 = built and gated, 1 = failure, 2 = host tests inconclusive
rem (a blocked exe launch, not a test failure - just run it again).

setlocal

rem Normalize the repo root rather than carrying "scripts\.." through every
rem derived path: DDKROOT is one of them and reaches setenv.bat, whose own
rem derived paths are printed in error messages a developer has to read.
for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "FLAVORS=%~1"
set "GATEOPT=%~2"
if "%FLAVORS%"=="" set "FLAVORS=both"
if /i "%FLAVORS%"=="both" set "FLAVORS=debug release"
if /i "%FLAVORS%"=="all" set "FLAVORS=debug release qemu"
if /i "%FLAVORS%"=="debug" set "FLAVORS=debug"
if /i "%FLAVORS%"=="release" set "FLAVORS=release"
if /i "%FLAVORS%"=="qemu" set "FLAVORS=qemu"
rem Validate both arguments here, before the self-tests and the host suite
rem run: a misspelt flavor word, or -NoTargetEvidence given first, used to be
rem rejected only when :buildflavor was reached, minutes in. The second
rem argument goes verbatim to check-imports.ps1, where a mistyped switch fails
rem parameter binding and reads as the binary failing the gate.
set "FLAVOR="
for %%F in (%FLAVORS%) do call :validateflavor %%F
if defined FLAVOR goto badflavor
if "%GATEOPT%"=="" goto gateoptok
if /i "%GATEOPT%"=="-NoTargetEvidence" goto gateoptok
goto badgateopt
:gateoptok
rem The DDK is a repository directory, not a machine-wide install, so this
rem default follows the clone. scripts\install-w2kddk-cabs.ps1 puts it there.
if "%DDKROOT%"=="" set "DDKROOT=%REPO%\tools\ntddk"
rem src\sources says XHCI_EXTRA_DEFINES is empty in every normal build and that a
rem deploy build must be made with it unset, so any value at all means a probe.
rem Do not narrow this to a substring test: `set VAR 2>nul | findstr` looks like
rem it works but never matches - on the left of a pipe cmd hands the command to a
rem child cmd.exe and the stripped redirection leaves the SET query prefix with a
rem trailing space, which matches no variable.
set "XHCI_RESOURCE_PROBE="
if defined XHCI_EXTRA_DEFINES set "XHCI_RESOURCE_PROBE=1"
rem Task 12.3's failed-start artifact is a diagnostic build like any other - it
rem is caught by the line above and carries the same do-not-deploy marker - but
rem it is the one that may be packaged, under make-package.ps1
rem -FailStartArtifact, so its own marker is verified in the image too. The
rem expansion happens in this shell before the pipe, so this is not the
rem `set VAR | findstr` trap the comment above warns about.
set "XHCI_FAILSTART="
if defined XHCI_EXTRA_DEFINES echo %XHCI_EXTRA_DEFINES% | findstr /c:"XHCI_FAIL_START_CONTROLLER" >nul && set "XHCI_FAILSTART=1"
rem Review finding 2. Both markers are emitted by any build that
rem merely *includes* the failed-start define, and the packager keys its one
rem exception on their presence - so `-DXHCI_FAIL_START_CONTROLLER
rem -DXHCI_PROBE_RESOURCES_SIZE=4096` would be staged as task 12.3's artifact
rem while behaving like neither artifact. The artifact is the whole value of the
rem define, so require it to be the whole value of the variable. Refused before
rem the DDK is even located, because a mixed image must not exist to be found
rem later. src\xhci_dispatch.c carries the same refusal as an #error, which is
rem what binds a bare `build` from a DDK prompt.
if defined XHCI_FAILSTART if /i not "%XHCI_EXTRA_DEFINES%"=="-DXHCI_FAIL_START_CONTROLLER" goto failstartmixed

if not exist "%DDKROOT%\bin\setenv.bat" goto noddk

rem setenv.bat takes BASEDIR verbatim, so DDKROOT is passed unquoted at the
rem build line below or every derived path would carry literal quotes - which
rem means a DDKROOT containing a space is split into two arguments and the DDK
rem derives its paths from the first half. Now that the DDK lives in the repo
rem this is reachable without an override - a clone under "My Documents" hits it
rem - so try the 8.3 short name before refusing. That works only where the
rem volume still generates one (8dot3 creation is off on many non-system
rem volumes), hence the re-check afterwards rather than trusting the expansion:
rem %%~sfI silently returns the long path when there is no short name.
for /f "delims= " %%S in ("%DDKROOT%") do set "DDKFIRST=%%S"
if "%DDKFIRST%"=="%DDKROOT%" goto ddkpathok
for %%I in ("%DDKROOT%") do set "DDKROOT=%%~sfI"
for /f "delims= " %%S in ("%DDKROOT%") do set "DDKFIRST=%%S"
if not "%DDKFIRST%"=="%DDKROOT%" goto ddkspace
if not exist "%DDKROOT%\bin\setenv.bat" goto ddkspace
:ddkpathok
set "DDKFIRST="
echo DDK: %DDKROOT%

rem The import library is a build artifact, not a checked-in file. Generating it
rem here rather than failing keeps a fresh clone one command away from a build.
if not exist "%REPO%\src\usbport.lib" (
    echo.
    echo src\usbport.lib is missing - generating it.
    call "%REPO%\scripts\make-usbport-lib.cmd"
    if errorlevel 1 goto libfail
)

echo.
echo === import gate self-tests ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\import-gate\test-evidence-manifests.ps1"
if errorlevel 1 goto gatetestfail
rem The FLAVORS column is the whole of task 13-L.1's enforcement - one row
rem decides whether a published binary may carry the sole import delta of the
rem build that gave the E460 a Code 2 - so it is tested before anything is
rem built rather than after.
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\import-gate\test-flavour-rules.ps1"
if errorlevel 1 goto gatetestfail

echo.
echo === INF gate self-tests ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\test-inf-checks.ps1"
if errorlevel 1 goto inftestfail

echo.
echo === INF gate ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-inf.ps1"
if errorlevel 1 goto inffail

rem Stand-ins only - no build, no staged media, no VM - so this runs here with
rem the other self-tests rather than next to the packaging step it guards.
echo.
echo === packager self-tests ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\package\test-package.ps1"
if errorlevel 1 goto pkgtestfail

echo.
echo === QEMU launcher self-tests ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\test-qemu-launchers.ps1"
if errorlevel 1 goto qemutestfail

echo.
echo === host tests ===
call "%REPO%\test\run-host-tests.cmd"
if errorlevel 2 goto inconclusive
if errorlevel 1 goto failed

rem `if errorlevel` inside the block is evaluated per iteration, so a first
rem flavor that fails cannot be masked by a second one that succeeds.
for %%F in (%FLAVORS%) do (
    call :buildflavor %%F
    if errorlevel 1 goto failed
)

echo.
if defined XHCI_RESOURCE_PROBE goto probesuccess
echo BUILD + GATES PASSED (%FLAVORS%)
echo Next, to build the install media a VM can be pointed at:
echo   powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1
endlocal
exit /b 0

:probesuccess
rem The marker is what stops this artifact being packaged, so verify it is
rem actually in the image rather than trusting the wiring in src\sources. Repo
rem audit finding 4: the previous arrangement embedded it only for
rem XHCI_PROBE_RESOURCES_SIZE, so every other diagnostic build was packageable -
rem a hole that existed precisely because nothing checked the artifact. A silent
rem regression here would restore it, so this fails the build rather than warns.
for %%F in (%FLAVORS%) do (
    call :checkmarker %%F
    if errorlevel 1 goto markerfail
)
if defined XHCI_FAILSTART (
    for %%F in (%FLAVORS%) do (
        call :checkfailstart %%F
        if errorlevel 1 goto markerfail
    )
    goto failstartsuccess
)
echo PROBE BUILD + GATES PASSED (%FLAVORS%)
echo   XHCI_EXTRA_DEFINES=%XHCI_EXTRA_DEFINES%
echo This artifact is diagnostic-only and must not be deployed. src\sources
echo defines XHCI_DIAGNOSTIC_BUILD for any nonempty XHCI_EXTRA_DEFINES, so the
echo binary carries the marker make-package.ps1 rejects - this warning is a
echo courtesy and the packaging gate is the enforcement.
echo Clear XHCI_EXTRA_DEFINES and rebuild before packaging:
echo   set XHCI_EXTRA_DEFINES=
echo   scripts\build-driver.cmd both
endlocal
exit /b 0

:failstartsuccess
echo FAILED-START ARTIFACT + GATES PASSED (%FLAVORS%)
echo   XHCI_EXTRA_DEFINES=%XHCI_EXTRA_DEFINES%
echo This is roadmap task 12.3's artifact: it installs, loads, and then refuses
echo the last step of StartController. It is a diagnostic build and carries the
echo do-not-deploy marker like any other, so it is the ONE artifact the packager
echo will stage - and only when asked for it by name:
echo   powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1 -Flavor debug -FailStartArtifact
echo Do not install it on a machine you are not prepared to recover. When you are
echo done, clear XHCI_EXTRA_DEFINES and rebuild before packaging anything else:
echo   set XHCI_EXTRA_DEFINES=
echo   scripts\build-driver.cmd both
endlocal
exit /b 0

rem ------------------------------------------------------------------
rem :checkfailstart <release|debug>
rem
rem Task 12.3's artifact must carry XHCI98_FAILSTART_ARTIFACT_TASK_12_3, which is
rem the string the packager's narrow exception keys on. Read out of the image for
rem the same reason :checkmarker is: this is a statement about the artifact, not
rem about the build files that were supposed to produce it. If the define reached
rem the compiler but the marker did not reach the image, the packager would
rem refuse the artifact - the safe direction - and this says so at build time
rem instead of after a staging attempt.
rem ------------------------------------------------------------------
:checkfailstart
setlocal
set "FLAVOR=%~1"
call :flavordirs %FLAVOR%
if "%OBJDIR%"=="" goto badflavor
set "OUTSYS=%REPO%\src\%OBJDIR%\i386\xhci98.sys"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$b=[System.IO.File]::ReadAllBytes('%OUTSYS%');" ^
    "$t=[System.Text.Encoding]::ASCII.GetString($b);" ^
    "if ($t.Contains('XHCI98_FAILSTART_ARTIFACT_TASK_12_3')) { exit 0 } else { exit 1 }"
if errorlevel 1 goto failstartmissing
endlocal
exit /b 0

:failstartmixed
echo.
echo ERROR: XHCI_EXTRA_DEFINES names XHCI_FAIL_START_CONTROLLER alongside
echo something else:
echo   XHCI_EXTRA_DEFINES=%XHCI_EXTRA_DEFINES%
echo Task 12.3's artifact must be built alone. Every build carrying that define
echo emits XHCI98_FAILSTART_ARTIFACT_TASK_12_3, and make-package.ps1
echo -FailStartArtifact keys its one exception to the do-not-deploy rule on that
echo string - so a mixed build is staged as the artifact and then behaves like
echo neither, which is a wasted guest boot at best. Build it on its own:
echo   set "XHCI_EXTRA_DEFINES=-DXHCI_FAIL_START_CONTROLLER"
echo   scripts\build-driver.cmd debug
endlocal
exit /b 1

:failstartmissing
echo.
echo ERROR: XHCI_EXTRA_DEFINES names XHCI_FAIL_START_CONTROLLER but the %FLAVOR%
echo image does not carry XHCI98_FAILSTART_ARTIFACT_TASK_12_3, so
echo make-package.ps1 -FailStartArtifact would refuse it. Check that
echo xhci_dispatch.c still emits the marker under that define.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :checkmarker <release|debug>
rem
rem A diagnostic build must carry XHCI98_PROBE_BUILD_DO_NOT_DEPLOY, because that
rem string is the only thing make-package.ps1 can see. Read out of the image, so
rem this is a statement about the artifact and not about the build files that
rem were supposed to produce it.
rem ------------------------------------------------------------------
:checkmarker
setlocal
set "FLAVOR=%~1"
call :flavordirs %FLAVOR%
if "%OBJDIR%"=="" goto badflavor
set "OUTSYS=%REPO%\src\%OBJDIR%\i386\xhci98.sys"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$b=[System.IO.File]::ReadAllBytes('%OUTSYS%');" ^
    "$t=[System.Text.Encoding]::ASCII.GetString($b);" ^
    "if ($t.Contains('XHCI98_PROBE_BUILD_DO_NOT_DEPLOY')) { exit 0 } else { exit 1 }"
if errorlevel 1 goto markermissing
endlocal
exit /b 0

:markermissing
echo.
echo ERROR: the %FLAVOR% diagnostic build does not carry the do-not-deploy
echo marker, so make-package.ps1 would accept it as install media.
echo XHCI_EXTRA_DEFINES is set, so src\sources should have defined
echo XHCI_DIAGNOSTIC_BUILD and xhci_dispatch.c should have emitted the string.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :checkflavour <release|debug|qemu>
rem
rem Exactly one XHCI98_FLAVOUR_* string, and it must be this flavor's. Both
rem halves are the check: a binary carrying two markers would be one the
rem preprocessor let through with more than one flavor define, and a binary
rem carrying the wrong one is a qemu build wearing a debug name - which is the
rem confusion the marker exists to make impossible, since both are checked
rem builds and VS_FF_DEBUG cannot tell them apart.
rem ------------------------------------------------------------------
:checkflavour
setlocal
set "FLAVOR=%~1"
call :flavordirs %FLAVOR%
if "%OBJDIR%"=="" goto badflavor
set "OUTSYS=%REPO%\src\%OBJDIR%\i386\xhci98.sys"
rem In a script rather than inline, unlike :checkmarker next door. It needs a
rem pipeline and a comparison, and a `powershell -Command` continuation is the
rem wrong place for either: the first version compared $found[0] without
rem wrapping the pipeline in @(), so a single match arrived as a string whose
rem [0] is its first CHARACTER and the gate refused every correct build, and
rem the second tripped over `|` inside a caret-continued quoted line.
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-flavour-marker.ps1" -Image "%OUTSYS%" -Flavour %FLAVOR%
if errorlevel 1 goto flavourmissing
endlocal
exit /b 0

:flavourmissing
echo.
echo ERROR: the %FLAVOR% image does not carry exactly its own flavour marker.
echo src\sources derives XHCI_FLAVOUR_RELEASE / _DEBUG / _QEMU from
echo BUILD_ALT_DIR and src\xhci_dispatch.c emits the string; DriverEntry reads
echo it so the linker cannot drop it. A binary that cannot be identified from
echo the file is one a user cannot report against and one the packager cannot
echo refuse by name.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :flavordirs <release|debug|qemu>
rem
rem The one place a flavor name becomes a DDK output tree. Sets OBJDIR and
rem ALTDIR, and leaves OBJDIR empty for a name this project does not know -
rem every caller tests that and goes to :badflavor.
rem
rem ALTDIR is the DDK's BUILD_ALT_DIR, which build.exe concatenates onto `obj`
rem to make the output tree: fre -> objfre, chk -> objchk, chk_qemu ->
rem objchk_qemu. setenv.bat sets the first two itself; the third is this
rem project's, and it exists because DEBUG AND QEMU ARE BOTH CHECKED BUILDS and
rem would otherwise overwrite each other's objects in src\objchk\i386.
rem
rem Verified against this DDK's build.exe rather than assumed:
rem the value may be at most 10 characters ("BUILD: environment variable
rem BUILD_ALT_DIR may not be longer than 10 characters."), the tree becomes
rem src\objchk_qemu\i386 and the log becomes buildchk_qemu.log, and overriding
rem it AFTER setenv.bat has run still links, because setenv.bat expands
rem SDK_LIB_PATH / DDK_LIB_PATH eagerly and they keep pointing at libchk.
rem
rem src\sources reads BUILD_ALT_DIR too - it is where XHCI_DBG_E9 and the
rem in-image flavor marker come from - so a binary's defines and the directory
rem it was linked in cannot disagree, and a bare `build` from a DDK prompt
rem reaches the same answer as this wrapper.
rem ------------------------------------------------------------------
:flavordirs
set "OBJDIR="
set "ALTDIR="
set "DDKFLAVOR="
if /i "%~1"=="release" (
    set "OBJDIR=objfre"
    set "ALTDIR=fre"
    set "DDKFLAVOR=free"
)
if /i "%~1"=="debug" (
    set "OBJDIR=objchk"
    set "ALTDIR=chk"
    set "DDKFLAVOR=checked"
)
if /i "%~1"=="qemu" (
    set "OBJDIR=objchk_qemu"
    set "ALTDIR=chk_qemu"
    set "DDKFLAVOR=checked"
)
exit /b 0

rem ------------------------------------------------------------------
rem :buildflavor <release|debug|qemu>
rem
rem DDKFLAVOR is the DDK's own word for the same thing, and it exists only
rem because setenv.bat takes it as an argument. It no longer identifies the
rem build on its own: debug and qemu are both "checked", which is exactly why
rem ALTDIR exists.
rem ------------------------------------------------------------------
:buildflavor
setlocal
set "FLAVOR=%~1"
call :flavordirs %FLAVOR%
if "%OBJDIR%"=="" goto badflavor
set "OUTSYS=%REPO%\src\%OBJDIR%\i386\xhci98.sys"
rem objchk -> buildchk, objfre -> buildfre, objchk_qemu -> buildchk_qemu: the
rem names build.exe writes its log and error file under, which are BUILD_ALT_DIR
rem with "build" in front. The substitution below is the same rule spelled once.
set "LOGBASE=%REPO%\src\build%OBJDIR:obj=%"

echo.
echo === build %FLAVOR% ===
if exist "%OUTSYS%" del "%OUTSYS%"
rem A stale .err from an earlier run would fail this build for last time's
rem errors.
if exist "%LOGBASE%.err" del "%LOGBASE%.err"

rem setenv.bat is not idempotent across flavors (it rewrites PATH/INCLUDE/LIB
rem and the build-flavor variables), so each flavor gets its own child cmd.
rem It also takes BASEDIR verbatim - pass it unquoted or every derived path
rem carries literal quotes.
rem BUILD_ALT_DIR is set AFTER setenv.bat, which is what makes the qemu flavor
rem possible at all: setenv.bat sets it to chk and derives the lib paths from it
rem in the same breath, so overriding it here moves the OBJECT tree without
rem moving the LIBRARY path. Overriding it before setenv.bat would do the
rem opposite and look for a libchk_qemu that does not exist.
cmd /c "call "%DDKROOT%\bin\setenv.bat" %DDKROOT% %DDKFLAVOR% w2k x86 && set "BUILD_ALT_DIR=%ALTDIR%" && cd /d "%REPO%\src" && build -cZ"
if errorlevel 1 goto buildfail

rem build.exe's exit code is not sufficient on its own: it writes the errors it
rem found to build<flavor>.err and that file only exists when there were some.
if exist "%LOGBASE%.err" goto builderrfile
if not exist "%OUTSYS%" goto nooutput

echo.
echo === import gate (%FLAVOR%) ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\import-gate\check-imports.ps1" -Image "%OUTSYS%" -Flavor %FLAVOR% %GATEOPT%
if errorlevel 1 goto gatefail

rem The image has to say which of the three it is, from an ASCII scan and with
rem no PE knowledge - that is what a user sending a capture quotes and what
rem make-package.ps1 refuses a qemu binary by. Checked here rather than trusted
rem to src\sources, for the same reason :checkmarker is: this is a statement
rem about the artifact, not about the build files meant to produce it. It also
rem catches the one mistake the directory layout cannot - an image built in the
rem right tree with the wrong define.
call :checkflavour %FLAVOR%
if errorlevel 1 goto flavourfail

endlocal
exit /b 0

:badflavor
echo ERROR: unknown build flavor "%FLAVOR%" - use release, debug, qemu, both
echo or all.
echo (The flavor formerly called "standard" is now "release".
echo It is a hard cut: the old word is not accepted.)
echo (There are THREE flavors since task 13-L.1. "both" is the two that ship -
echo release and debug - and stays the default; "all" adds qemu, which carries
echo the port-0xE9 mirror and must NEVER be published.)
echo (The DDK's own words are free and checked; this project uses them only
echo where setenv.bat forces it. They no longer identify a build on their own,
echo because debug and qemu are both checked - see :flavordirs.)
endlocal
exit /b 1

:badgateopt
echo ERROR: unknown second argument "%GATEOPT%". The only option after the
echo flavor is -NoTargetEvidence, which skips the import gate's target-file
echo evidence steps on a host with none staged.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :validateflavor <word>
rem
rem Leaves FLAVOR set to the word when :flavordirs does not know it, so the
rem caller can refuse before any self-test has run.
rem ------------------------------------------------------------------
:validateflavor
call :flavordirs %~1
if "%OBJDIR%"=="" set "FLAVOR=%~1"
exit /b 0

:buildfail
echo.
echo ERROR: the %FLAVOR% DDK build failed. See "%LOGBASE%.log" and
echo "%LOGBASE%.err".
endlocal
exit /b 1

:builderrfile
echo.
echo ERROR: the %FLAVOR% build reported errors - see "%LOGBASE%.err".
endlocal
exit /b 1

:nooutput
echo.
echo ERROR: the %FLAVOR% build produced no "%OUTSYS%".
endlocal
exit /b 1

:flavourfail
echo.
echo ERROR: the %FLAVOR% binary carries the wrong flavour marker - see above.
endlocal
exit /b 1

:gatefail
echo.
echo ERROR: the %FLAVOR% binary failed the import-compatibility gate. Do not
echo deploy it - an unresolved or wrong-module import stops the driver before
echo DriverEntry, and on Win98 the only symptom may be a yellow bang.
endlocal
exit /b 1

rem ------------------------------------------------------------------
:ddkspace
echo.
echo ERROR: the DDK path contains a space and has no usable 8.3 short name:
echo   %DDKROOT%
echo The DDK's setenv.bat takes BASEDIR verbatim and cannot be quoted, so a
echo path with a space is split and every derived path is wrong. Either clone
echo this repository to a path without a space, or install the DDK to one and
echo point DDKROOT at it:
echo   powershell -ExecutionPolicy Bypass -File scripts\install-w2kddk-cabs.ps1 -DdkPath C:\NTDDK
endlocal
exit /b 1

:noddk
echo.
echo ERROR: %DDKROOT%\bin\setenv.bat not found.
echo The Windows 2000 DDK is expected inside this repository at tools\ntddk.
echo Nothing is installed machine-wide - unpack it from the archive with:
echo   powershell -ExecutionPolicy Bypass -File scripts\install-w2kddk-cabs.ps1
echo (or set DDKROOT to a DDK installed elsewhere).
endlocal
exit /b 1

:libfail
echo.
echo ERROR: could not generate src\usbport.lib.
endlocal
exit /b 1

:gatetestfail
echo.
echo ERROR: the import gate's evidence-manifest self-tests failed.
endlocal
exit /b 1

:inftestfail
echo.
echo ERROR: the INF gate's own self-tests failed, so its verdict on
echo src\xhci98.inf cannot be trusted either. Fix the gate first.
endlocal
exit /b 1

:inffail
echo.
echo ERROR: src\xhci98.inf failed the setup-engine gate. Do not install it -
echo Win98's setup engine has no log, and a Win2000 install that creates no
echo service looks the same in Device Manager as a driver that loaded and
echo failed.
endlocal
exit /b 1

:pkgtestfail
echo.
echo ERROR: the install-media packager's self-tests failed, so any package it
echo builds is untrustworthy - including where it puts each file and whether
echo a Microsoft file has crept back onto the media.
endlocal
exit /b 1

:qemutestfail
echo.
echo ERROR: the QEMU launcher self-tests failed. The generated launch command
echo may lose or conflate qemu-driver trace evidence.
endlocal
exit /b 1

:inconclusive
echo.
echo INCONCLUSIVE: a host test binary never produced a result line, so the run
echo stopped before building the driver. That is Smart App Control blocking a
echo freshly linked unsigned exe, not a test failure - run this again.
endlocal
exit /b 2

:markerfail
echo.
echo BUILD FAILED: a diagnostic build is not marked as undeployable.
endlocal
exit /b 1

:failed
echo.
echo BUILD + GATES FAILED.
endlocal
exit /b 1
