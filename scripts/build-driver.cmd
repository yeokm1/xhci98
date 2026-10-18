@echo off
rem build-driver.cmd - the one way to build xhci98.sys.
rem
rem Since 2026-10-02 xhci98.sys is the successor host controller driver
rem (design record 13; roadmap-hcd.md). The usbport miniport it was until
rem 1.2.0.0 left the tree that day, and with it this script's usbport import
rem library step and its probe and failed-start artifacts.
rem
rem Non-interactive: DDK build, then the gates that must pass before a binary
rem is allowed near a VM (roadmap Phase 3 task 5):
rem
rem   1. XHCI_EXTRA_DEFINES is unset - the HCD has no diagnostic builds
rem   2. the import gate's evidence-manifest regression tests, and its
rem      three-flavor FLAVORS-column tests
rem   3. the INF gate's own regression tests, then scripts\inf-gate\check-inf.ps1
rem      on src\xhci98.inf - the binary and the INF are deployed together and
rem      neither setup engine reports a malformed one usefully
rem   4. the packager's regression tests - it decides where each file lands on
rem      the install media, and a package staged at one path but authenticated
rem      at another verifies nothing
rem   5. the QEMU launcher regression tests - a stale append-only trace can
rem      falsely attribute an earlier DriverEntry to the current binary, and
rem      two launchers sharing a monitor port cannot both run
rem   6. the vm-matrix verdict self-tests - a matrix that reads a refusal as a
rem      pass makes every later FAIL and NODRIVER word untrustworthy
rem   7. the tracked batch files' line endings - MS-DOS 7.1 COMMAND.COM can
rem      fail on an LF-only .BAT, and the field wrappers are .BAT files
rem   8. the XHCISNAP report self-test, when that EXE has been built (it has a
rem      build.cmd of its own, and a clone without Open Watcom still runs
rem      everything else here), and the XHCICLN self-test when that one has
rem      (task 38.8; xhcicln\build.cmd), and the XHCIINST self-test and import
rem      check when that one has (task 40.5; xhciinst\build.cmd)
rem   9. test\run-host-tests.cmd - the pure-core suite. It runs before the DDK
rem      builds, not after: it compiles the same core files in seconds, so a
rem      bad carve, ring or PORTSC constant should not cost two full builds
rem      first. Since task 21.4 it also builds and runs test_packet and
rem      test_membuf a second time for amd64, whichever architecture this
rem      wrapper was asked for, so the _WIN64 half of src\xhci_usbport.h is
rem      checked on every build rather than only on an -amd64 one
rem  10. `build` for each requested flavor, with the compile-time layout and
rem      ABI asserts in src\xhci.h / src\xhci_usbport.h
rem  11. scripts\import-gate\check-imports.ps1 on each linked binary, then
rem      scripts\check-flavour-marker.ps1 on it - which is what says the
rem      binary in objfre really is the release flavour and not a checked
rem      build staged under the wrong name
rem  12. the second binary, xhciuas.sys - the UAS class driver of roadmap task
rem      31-A.2, built from src\uas after xhci98.sys for each flavour, and
rem      held to the same gates: its own allowlists
rem      (scripts\import-gate\xhciuas-imports.allow on x86,
rem      xhciuas-imports-amd64.allow on amd64, passed with -AllowPath), the
rem      same flavour marker check and source stamp, and its two INFs' own
rem      gate (scripts\inf-gate\check-uas-inf.ps1, self-tested first, run
rem      over src\uas\xhciuas.inf and xhciuas-amd64.inf on every build) beside
rem      the two runs of check-inf.ps1
rem  13. the text-mode Setup driver descriptions of task 33.3,
rem      src\txtsetup.oem and src\txtsetup-amd64.oem, gated by
rem      scripts\inf-gate\check-txtsetup-oem.ps1 (self-tested first) beside
rem      the INF gates
rem  14. the third binary, xhcicm.sys - the Thunderbolt 3 connection manager
rem      of roadmap-usb4.md task 39-A.1 (design record 21), built from
rem      src\cm after xhciuas.sys for each flavour and architecture and held
rem      to the same gates: its own allowlists
rem      (scripts\import-gate\xhcicm-imports.allow on x86,
rem      xhcicm-imports-amd64.allow on amd64), the flavour marker check and
rem      source stamp, and its four INFs' own gate
rem      (scripts\inf-gate\check-cm-inf.ps1, self-tested first, run over
rem      src\cm\xhcicm.inf, xhcicm-amd64.inf, xhcicmf.inf and
rem      xhcicmf-amd64.inf on every build)
rem  15. the gated build's record, out\gated-<arch>-<flavour>.txt
rem      (scripts\package\write-gated-manifest.ps1; roadmap-usb4.md SIGN.1),
rem      which make-package.ps1 -SignOnly reads instead of running the host
rem      suite and the import gates again. The run holds out\gated-<arch>.lock
rem      throughout and refuses to start while another holds it, and notes
rem      the commit and the tree's state when it takes it, before any
rem      self-test. When a flavour's build starts its old record is deleted;
rem      each binary is hashed before its own import gate and again after it,
rem      and the two must agree; once the flavour's last gate has passed each
rem      file must still have that hash, and the record is written from it,
rem      clean only if the tree is still clean at the commit noted at the
rem      start. A run that fails leaves no record for the flavour that failed
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
rem   scripts\package\make-package.ps1 [-Flavor release|debug] [-Arch x86|amd64]
rem
rem The media holds no Microsoft file: it is the drivers (xhcihlpr.sys on x86
rem only) and their INFs, txtsetup.oem, on amd64 the test certificate, and a
rem pinned Stor98 build when one is asked for (make-package.ps1 lists them).
rem The OS supplies usbd.sys and usbui.dll through the INF's LayoutFile. The
rem HCD replaces usbport.sys and usbhub.sys, and the INF gate refuses an INF
rem that fetches either (OS-HCDREPLACED).
rem
rem Usage:  scripts\build-driver.cmd [release|debug|qemu|both|all]
rem                                  [-amd64] [-NoTargetEvidence]
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
rem SECOND ARCHITECTURE, since roadmap task 21.2. -amd64 builds the same three
rem flavors for amd64 with WDK 7.1 (tools\WinDDK71) instead of the Windows 2000
rem DDK, which cannot target it. The two toolchains are separate all the way
rem down and the x86 path is untouched by the switch:
rem
rem   x86    tools\ntddk       setenv <root> <flavor> w2k x86
rem                                                     src\obj*\i386
rem   amd64  tools\WinDDK71    setenv <root> <flavor> x64 WNET no_oacr
rem                                                     src\obj*\amd64
rem
rem BUILD_ALT_DIR is overridden back to fre/chk/chk_qemu after setenv.bat in
rem BOTH cases. WDK 7.1's setenv sets it to fre_wnet_AMD64, which src\sources
rem hard-errors on, and build.exe appends the architecture itself - so the
rem override is what puts the output at src\objfre\amd64 beside src\objfre\i386
rem with no second obj root and nothing renamed.
rem
rem WHAT AN amd64 BUILD IS GATED ON, since roadmap task 21.3 closed: the import
rem gate enforces xhci98-imports-amd64.allow and resolves every pair against
rem authenticated NT 5.2 amd64 baselines (winxp64-baselines.expected), and the
rem INF gates run over all eight production INFs on every build, whichever
rem architecture is being built: check-inf.ps1 over src\xhci98.inf (-Arch
rem x86) and src\xhci98-amd64.inf (-Arch amd64), check-uas-inf.ps1 over the
rem two UAS INFs and check-cm-inf.ps1 over the four connection manager INFs
rem (steps 3, 12 and 14 above). All of them, always, because each x86/amd64
rem pair is one release and either drifting from the other is silent on the
rem target; scripts\inf-gate\test-inf-checks.ps1 also compares the two
rem xhci98 INFs directly.
rem
rem Exit codes: 0 = built and gated, 1 = failure, 2 = host tests inconclusive
rem (a blocked exe launch, not a test failure - just run it again).

setlocal

rem A pwsh -> cmd -> powershell.exe launch retains PS7's module paths, unlike
rem pwsh launching powershell.exe directly. Its Utility module hides the 5.1
rem Get-FileHash command used by the gates. Scope the fix to this build.
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules;%ProgramFiles%\WindowsPowerShell\Modules"

rem Normalize the repo root rather than carrying "scripts\.." through every
rem derived path: DDKROOT is one of them and reaches setenv.bat, whose own
rem derived paths are printed in error messages a developer has to read.
for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "FLAVORS=%~1"
rem Two optional switches in either order after the flavor, because -amd64 and
rem -NoTargetEvidence are independent and a caller should not have to remember
rem which comes first. :readopt leaves BADOPT set for anything else, which is
rem refused below before a single self-test has run.
set "GATEOPT="
set "ARCH=x86"
set "BADOPT="
call :readopt "%~2"
call :readopt "%~3"
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
if defined BADOPT goto badgateopt

rem The gated build's lock (roadmap-usb4.md SIGN.1): one run per architecture
rem in this tree at a time, held for the whole run, so no second build can
rem change a binary between its gate and its record. mkdir is the atomic step:
rem it fails when the directory exists. The body runs as :mainbody so that
rem every exit, whichever refusal it comes from, comes back here to drop the
rem lock and any half-made record.
rem :mainbody opens a scope of its own, so whatever its exits do with endlocal,
rem GATEDLOCK, REPO, ARCH and FLAVORS are this scope's and the cleanup below
rem reads them. The run's token lives in the lock's run.txt, and :gated hands it
rem to the writer for the one call and clears it again.
if not exist "%REPO%\out" mkdir "%REPO%\out"
set "GATEDLOCK=%REPO%\out\gated-%ARCH%.lock"
mkdir "%GATEDLOCK%" 2>nul
if errorlevel 1 goto gatedlocked
set "GATEDTOK=%RANDOM%%RANDOM%%RANDOM%%RANDOM%%RANDOM%"
> "%GATEDLOCK%\run.txt" echo %GATEDTOK%
set "GATEDTOK="
rem The commit and the tree's state, once, before any self-test: each
rem flavour's record is clean only if the tree still matches this at its end.
call :gated -RunStart
if errorlevel 1 goto gatedrunstartfail
call :mainbody
set "MAINRC=%errorlevel%"
call :gatedcleanup
endlocal & exit /b %MAINRC%

:gatedrunstartfail
call :gatedcleanup
echo ERROR: could not note the commit and the tree's state for the gated
echo build's record - see above.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :gatedcleanup - drop the lock and any half-made record, on every exit.
rem ------------------------------------------------------------------
:gatedcleanup
for %%F in (%FLAVORS%) do if exist "%REPO%\out\gated-%ARCH%-%%F.pending" del "%REPO%\out\gated-%ARCH%-%%F.pending"
if exist "%GATEDLOCK%" rmdir /s /q "%GATEDLOCK%"
exit /b 0

:gatedlocked
echo ERROR: "%GATEDLOCK%" exists: another build-driver.cmd run for %ARCH% holds
echo this tree. Wait for it to finish. If none is running - a run that was
echo interrupted with Ctrl+C leaves the lock behind - delete the directory:
echo   rmdir /s /q "%GATEDLOCK%"
endlocal
exit /b 1

:mainbody
setlocal
rem The DDK output directory for the target architecture - build.exe's own
rem <arch> component, and the one place this script turns ARCH into a path.
set "ARCHDIR=i386"
if /i "%ARCH%"=="amd64" set "ARCHDIR=amd64"
rem The DDK is a repository directory, not a machine-wide install, so this
rem default follows the clone. scripts\install-w2kddk-cabs.ps1 puts it there.
rem
rem An amd64 build takes WDK 7.1 instead, and the trailing setenv.bat arguments
rem differ with it: the Windows 2000 DDK takes "w2k x86", WDK 7.1 takes
rem "x64 WNET no_oacr" - WNET because NT 5.2 is the only lineage that ships an
rem amd64 lib directory (there is no lib\wxp\amd64), which is what makes one
rem binary serve Windows XP x64 and Server 2003 x64.
set "SETENVARGS=w2k x86"
if /i not "%ARCH%"=="amd64" goto ddkdefault
set "SETENVARGS=x64 WNET no_oacr"
rem
rem **DDKROOT NAMES THE WIN2000 DDK AND MUST NOT REACH THIS LEG.**  It is an
rem override for the 32-bit build, and it reaches setenv.bat - so an exported
rem DDKROOT pointing at tools\ntddk was being called with `x64 WNET`, which the
rem Win2000 DDK does not have.  The run then died at :nooutput naming the wrong
rem cause entirely (the audit of 2026-09-16).  WDKROOT is this leg's own
rem override - the SAME name the import gate honours, so one
rem variable redirects the whole 64-bit toolchain rather than each script
rem having its own.
rem
rem A LABEL RATHER THAN A PARENTHESISED BLOCK, which is this file's idiom for
rem every other refusal and is not a style choice here: cmd ends an `if (`
rem block at the first unescaped `)`, including one inside an `echo`, so a
rem diagnostic that wants a bracketed aside cannot live in one.
if defined DDKROOT goto ddkrootset
set "DDKROOT=%REPO%\tools\WinDDK71"
if not "%WDKROOT%"=="" set "DDKROOT=%WDKROOT%"
goto ddkchosen
:ddkrootset
echo ERROR: DDKROOT is set, and it is the Windows 2000 DDK override.
echo   DDKROOT=%DDKROOT%
echo The -amd64 leg builds with WDK 7.1 and would call that DDK's setenv.bat
echo with "x64 WNET", which it cannot do - and the failure would be reported
echo as a missing output rather than as this.
echo Clear DDKROOT, or set WDKROOT to redirect the 64-bit toolchain - the same
echo variable the import gate reads:
echo   set DDKROOT=
echo   set WDKROOT=^<path to WDK 7.1^>
endlocal
exit /b 1
:ddkdefault
if "%DDKROOT%"=="" set "DDKROOT=%REPO%\tools\ntddk"
:ddkchosen
rem XHCI_EXTRA_DEFINES was the miniport's diagnostic hatch (its probe and
rem failed-start artifacts, which left the tree with it on 2026-10-02).
rem src\sources refuses any value too, which binds a bare `build`; this says
rem so before a single self-test has run.
if defined XHCI_EXTRA_DEFINES goto extradefines

rem Two refusals, because the two legs want two different toolchains: on the
rem -amd64 leg DDKROOT is tools\WinDDK71 (or WDKROOT), and a missing WDK used
rem to be reported as a missing Windows 2000 DDK, naming tools\ntddk and the
rem cab installer that cannot supply it (the audit of 2026-09-17).
if /i "%ARCH%"=="amd64" if not exist "%DDKROOT%\bin\setenv.bat" goto nowdk
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
rem The x86 helper's toolchain rules (design record 18 section 15.10): a
rem helper built for another target than WXP shows in no field of its header.
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-helper-build.ps1" -SelfTest
if errorlevel 1 goto gatetestfail

echo.
echo === INF gate self-tests ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\test-inf-checks.ps1"
if errorlevel 1 goto inftestfail

echo.
echo === INF gate (x86) ===
rem INFFILE is what :inffail names; the two gate runs share the label and used
rem to share the x86 file's name too (the audit of 2026-09-17).
set "INFFILE=src\xhci98.inf"
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-inf.ps1" -Arch x86
if errorlevel 1 goto inffail

rem The 64-bit package's INF, gated on every build and not only an -amd64 one.
rem The two ship as one release, neither engine reports a mistake in either,
rem and the cost of the second run is a second or two.
echo.
echo === INF gate (amd64) ===
set "INFFILE=src\xhci98-amd64.inf"
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-inf.ps1" -Arch amd64
if errorlevel 1 goto inffail

rem The UAS class driver's INF (task 31-A.2). check-inf.ps1 is built around
rem xhci98.inf's own facts and would refuse a second INF for lacking them, so
rem the second INF has a gate of its own, self-tested by mutation first.
echo.
echo === UAS INF gate self-tests ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-uas-inf.ps1" -SelfTest
if errorlevel 1 goto inftestfail

echo.
echo === UAS INF gate (x86) ===
set "INFFILE=src\uas\xhciuas.inf"
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-uas-inf.ps1" -Arch x86
if errorlevel 1 goto inffail

echo.
echo === UAS INF gate (amd64) ===
set "INFFILE=src\uas\xhciuas-amd64.inf"
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-uas-inf.ps1" -Arch amd64
if errorlevel 1 goto inffail

rem The connection manager's four INFs (task 39-A.1): the NHI's pair and the
rem NT-only tunnel-bridge filter's pair, under a gate of their own,
rem self-tested by mutation first.
echo.
echo === CM INF gate self-tests ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-cm-inf.ps1" -SelfTest
if errorlevel 1 goto inftestfail

echo.
echo === CM INF gate (NHI, x86) ===
set "INFFILE=src\cm\xhcicm.inf"
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-cm-inf.ps1" -Arch x86
if errorlevel 1 goto inffail

echo.
echo === CM INF gate (NHI, amd64) ===
set "INFFILE=src\cm\xhcicm-amd64.inf"
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-cm-inf.ps1" -Arch amd64
if errorlevel 1 goto inffail

echo.
echo === CM INF gate (filter, x86) ===
set "INFFILE=src\cm\xhcicmf.inf"
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-cm-inf.ps1" -Filter -Arch x86
if errorlevel 1 goto inffail

echo.
echo === CM INF gate (filter, amd64) ===
set "INFFILE=src\cm\xhcicmf-amd64.inf"
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-cm-inf.ps1" -Filter -Arch amd64
if errorlevel 1 goto inffail

rem The text-mode Setup driver descriptions (task 33.3): src\txtsetup.oem and
rem src\txtsetup-amd64.oem, self-tested by mutation first, then each gated
rem against the INF of its own architecture and against the other file.
echo.
echo === txtsetup.oem gate self-tests ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-txtsetup-oem.ps1" -SelfTest
if errorlevel 1 goto inftestfail

echo.
echo === txtsetup.oem gate (x86) ===
set "INFFILE=src\txtsetup.oem"
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-txtsetup-oem.ps1" -Arch x86
if errorlevel 1 goto inffail

echo.
echo === txtsetup.oem gate (amd64) ===
set "INFFILE=src\txtsetup-amd64.oem"
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\inf-gate\check-txtsetup-oem.ps1" -Arch amd64
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

rem The verdict evaluator's suite (design record 06) and the batch-file
rem line-ending check were not wired in here until the audit of
rem 2026-09-05; a build that ran every other self-test could still ship a
rem matrix that read a refusal as a pass. Both are guestless and quick.
echo.
echo === VM matrix verdict self-tests ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\vm-matrix\selftest.ps1"
if errorlevel 1 goto matrixtestfail
rem The M86 trace validator (design record 18 section 9): a capture it
rem accepts is what R1, R2 and the lifecycle legs are read from.
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\vm-matrix\check-msi86-trace.ps1" -SelfTest
if errorlevel 1 goto matrixtestfail

echo.
echo === batch-file line endings ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\xhciqual\test\check-bat-eol.ps1"
if errorlevel 1 goto eoltestfail

rem And the bytes INSIDE tracked source, which nothing checked until task
rem 22.7: every other gate here reads src\ through a compiler or a parser
rem that accepts any byte at all inside a comment or a string literal. A BEL
rem written into src\xhci_slot.c by a PowerShell escape passed all three x86
rem flavours and every gate on 2026-09-12.
echo.
echo === source charset ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-source-charset.ps1"
if errorlevel 1 goto charsetfail

rem The snapshot reader's report path, when its EXE has been built (it is a
rem separate build.cmd, and a clone without Open Watcom still has this one).
if exist "%REPO%\xhcisnap\XHCISNAP.EXE" (
    echo.
    echo === xhcisnap report self-test ===
    call "%REPO%\xhcisnap\selftest.cmd"
    if errorlevel 1 goto snaptestfail
)

rem The cleanup tool's pure half (task 38.8), when its EXE has been built (it
rem has a build.cmd of its own, as XHCISNAP has).
if exist "%REPO%\xhcicln\XHCICLN.EXE" (
    echo.
    echo === xhcicln self-test ===
    call "%REPO%\xhcicln\selftest.cmd"
    if errorlevel 1 goto clntestfail
)

rem The install tool (task 40.5, design record 23), when its EXE has been
rem built: its /selftest, its switch refusals and its import table, which
rem must name KERNEL32 and ADVAPI32 alone (xhciinst\selftest.cmd).
if exist "%REPO%\xhciinst\XHCIINST.EXE" (
    echo.
    echo === xhciinst self-test ===
    call "%REPO%\xhciinst\selftest.cmd"
    if errorlevel 1 goto insttestfail
)

rem The USB tree tool (Phase 41, design record 25), when its EXE has been
rem built: its /selftest, its switch refusals and its import table, which
rem must name KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, COMCTL32 and
rem COMDLG32 alone (xhcitr\selftest.cmd).
if exist "%REPO%\xhcitr\XHCITR.EXE" (
    echo.
    echo === xhcitr self-test ===
    call "%REPO%\xhcitr\selftest.cmd"
    if errorlevel 1 goto trtestfail
)

rem The safe-removal mark service (task 42-A.5, design record 21 section
rem 7.4 revision 3), when its EXE has been built: its -selftest, its switch
rem refusals and its import table, which must name KERNEL32 and ADVAPI32
rem alone (xhcimark\selftest.cmd). Nothing of it installs or starts the
rem service.
if exist "%REPO%\xhcimark\XHCIMARK.EXE" (
    echo.
    echo === xhcimark self-test ===
    call "%REPO%\xhcimark\selftest.cmd"
    if errorlevel 1 goto mktestfail
)

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
echo BUILD + GATES PASSED (%FLAVORS%)
echo Next, to build the install media a VM can be pointed at:
if /i "%ARCH%"=="amd64" (
    echo   powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1 -Arch amd64
    echo and, to test-sign the gated binaries without running the gates again:
    echo   powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1 -Arch amd64 -Flavor ^<flavour^> -TestSignPfx ^<pfx^> -SignOnly
) else (
    echo   powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1
)
endlocal
exit /b 0

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
set "OUTSYS=%REPO%\src\%OBJDIR%\%ARCHDIR%\xhci98.sys"
rem In a script rather than inline. It needs a
rem pipeline and a comparison, and a `powershell -Command` continuation is the
rem wrong place for either: the first version compared $found[0] without
rem wrapping the pipeline in @(), so a single match arrived as a string whose
rem [0] is its first CHARACTER and the gate refused every correct build, and
rem the second tripped over `|` inside a caret-continued quoted line.
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-flavour-marker.ps1" -Image "%OUTSYS%" -Flavour %FLAVOR%
if errorlevel 1 goto flavourmissing

rem Record which sources this binary came from, beside it. make-release.ps1
rem checks it and refuses to publish a .sys the tree can no longer reproduce -
rem the driver's equivalent of the "EXE newer than its own sources" refusals it
rem already makes for XHCIQUAL and XHCISNAP (the audit of 2026-09-07). It is
rem content rather than timestamps, because an mtime moves on a checkout or a
rem comment-only commit; scripts\source-stamp.ps1 says why at length.
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\source-stamp.ps1" -Write "%REPO%\src\%OBJDIR%\%ARCHDIR%"
if errorlevel 1 goto stampfailed
endlocal
exit /b 0

:flavourmissing
echo.
echo ERROR: the %FLAVOR% image does not carry exactly its own flavour marker.
echo src\sources derives XHCI_FLAVOUR_RELEASE / _DEBUG / _QEMU from
echo BUILD_ALT_DIR and src\hcd_entry.c emits the string; DriverEntry reads
echo it so the linker cannot drop it. A binary that cannot be identified from
echo the file is one a user cannot report against and one the packager cannot
echo refuse by name.
endlocal
exit /b 1

:stampfailed
echo.
echo ERROR: could not record the source stamp beside the %FLAVOR% image.
echo scripts\source-stamp.ps1 hashes every file src\sources names plus every
echo header in src\, and writes the list beside the binary so make-release.ps1
echo can refuse to publish a .sys the tree can no longer reproduce. A build
echo that cannot write it has a src\sources this script cannot read, or an
echo obj directory it cannot write to.
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
set "OUTSYS=%REPO%\src\%OBJDIR%\%ARCHDIR%\xhci98.sys"
rem objchk -> buildchk, objfre -> buildfre, objchk_qemu -> buildchk_qemu: the
rem names build.exe writes its log and error file under, which are BUILD_ALT_DIR
rem with "build" in front. The substitution below is the same rule spelled once.
set "LOGBASE=%REPO%\src\build%OBJDIR:obj=%"
rem The record of the last gated build of this flavour goes before anything is
rem rebuilt, and the commit and the tree's state are noted (roadmap-usb4.md
rem SIGN.1; scripts\package\write-gated-manifest.ps1 -Begin).
call :gated -Begin
if errorlevel 1 goto gatedrecfail

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
cmd /c "call "%DDKROOT%\bin\setenv.bat" %DDKROOT% %DDKFLAVOR% %SETENVARGS% && set "BUILD_ALT_DIR=%ALTDIR%" && cd /d "%REPO%\src" && build -cZ"
if errorlevel 1 goto buildfail

rem build.exe's exit code is not sufficient on its own: it writes the errors it
rem found to build<flavor>.err and that file only exists when there were some.
if exist "%LOGBASE%.err" goto builderrfile
if not exist "%OUTSYS%" goto nooutput

echo.
echo === import gate (%FLAVOR%) ===
rem The bytes the gate is about to read, hashed again after it (:gated).
call :gated -Hash -Name xhci98.sys -Image "%OUTSYS%"
if errorlevel 1 goto gatedrecfail
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\import-gate\check-imports.ps1" -Image "%OUTSYS%" -Flavor %FLAVOR% -Arch %ARCH% %GATEOPT%
if errorlevel 1 goto gatefail
rem The bytes the gate just read, for the gated build's record: the same hash
rem as before the gate, or the flavour fails.
call :gated -Capture -Name xhci98.sys -Image "%OUTSYS%"
if errorlevel 1 goto gatedrecfail

rem The image has to say which of the three it is, from an ASCII scan and with
rem no PE knowledge - that is what a user sending a capture quotes and what
rem make-package.ps1 refuses a qemu binary by. Checked here rather than trusted
rem to src\sources, because this is a statement
rem about the artifact, not about the build files meant to produce it. It also
rem catches the one mistake the directory layout cannot - an image built in the
rem right tree with the wrong define.
call :checkflavour %FLAVOR%
if errorlevel 1 goto flavourfail

call :builduas %FLAVOR%
if errorlevel 1 goto uasfail

call :buildcm %FLAVOR%
if errorlevel 1 goto cmfail

rem xhcihlpr.sys, the x86 helper (design record 18, section 15), with every
rem x86 flavour and never with -amd64: the amd64 HCD has no helper.
if /i "%ARCH%"=="amd64" goto skiphlpr
call :buildhlpr %FLAVOR%
if errorlevel 1 goto hlprfail
:skiphlpr

rem Every gate of this flavour has passed: record what they read (roadmap-usb4.md
rem SIGN.1), for make-package.ps1 -SignOnly - from the hashes captured after
rem each import gate, and only if every file still has its captured hash.
echo.
echo === gated build record (%FLAVOR%, %ARCH%) ===
call :gated -Finish %GATEOPT%
if errorlevel 1 goto gatedrecfail

endlocal
exit /b 0

rem ------------------------------------------------------------------
rem :gated <-RunStart | -Begin | -Hash or -Capture -Name <file> -Image <path>
rem         | -Finish [...]>
rem
rem One step of the gated build's record for the current ARCH and, but for
rem -RunStart, FLAVOR (scripts\package\write-gated-manifest.ps1, which refuses
rem unless this run holds the lock taken above). The run's token is read from
rem the lock into XHCI98_GATED_RUN for this one call and cleared after it.
rem ------------------------------------------------------------------
:gated
set /p XHCI98_GATED_RUN=<"%GATEDLOCK%\run.txt"
set "GATEDFLV="
if defined FLAVOR set "GATEDFLV=-Flavor %FLAVOR%"
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\package\write-gated-manifest.ps1" -Arch %ARCH% %GATEDFLV% %*
set "GATEDRC=%errorlevel%"
set "XHCI98_GATED_RUN="
exit /b %GATEDRC%

:gatedrecfail
echo.
echo ERROR: the gated build's record of the %FLAVOR% build could not be begun,
echo captured or written - see above. make-package.ps1 -SignOnly needs it; a
echo package built without -SignOnly runs the gates again and does not.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :buildhlpr <release|debug|qemu>
rem
rem xhcihlpr.sys, the x86 helper (design record 18 section 15.10; until task
rem 38.3 xhcimsi.sys, built for Vista): WDK 7.1, x86, Windows XP target
rem (setenv <WDK> <flavor> x86 WXP no_oacr), subsystem 5.00 by its sources,
rem the same BUILD_ALT_DIR as the x86 binaries just built, in src\hlpr; then
rem the build's target line, libraries and image header
rem (scripts\check-helper-build.ps1), the import gate's hlprx86 profile
rem against its own allowlist and every kernel and HAL of Windows 2000, XP,
rem Vista and 7 x86, the flavour marker check and the source stamp. An x86
rem build therefore needs tools\WinDDK71 (or WDKROOT) as well as MSVC 6 and
rem the 2000 DDK, and fails without it: a package whose INF names
rem xhcihlpr.sys and does not carry it is refused by make-package.ps1
rem anyway, so there is no switch to skip it.
rem ------------------------------------------------------------------
:buildhlpr
setlocal
set "FLAVOR=%~1"
call :flavordirs %FLAVOR%
if "%OBJDIR%"=="" goto badflavor
set "HLPRWDK=%REPO%\tools\WinDDK71"
if not "%WDKROOT%"=="" set "HLPRWDK=%WDKROOT%"
if not exist "%HLPRWDK%\bin\setenv.bat" goto hlprnowdk
set "HLPRALLOW=%REPO%\scripts\import-gate\xhcihlpr-imports.allow"
set "HLPRSYS=%REPO%\src\hlpr\%OBJDIR%\i386\xhcihlpr.sys"
set "HLPRLOG=%REPO%\src\hlpr\build%OBJDIR:obj=%"

echo.
echo === build %FLAVOR% xhcihlpr.sys (WDK 7.1, x86, Windows XP target) ===
if exist "%HLPRSYS%" del "%HLPRSYS%"
if exist "%HLPRLOG%.err" del "%HLPRLOG%.err"
if exist "%HLPRLOG%.log" del "%HLPRLOG%.log"
cmd /c "call "%HLPRWDK%\bin\setenv.bat" %HLPRWDK% %DDKFLAVOR% x86 WXP no_oacr && set "BUILD_ALT_DIR=%ALTDIR%" && cd /d "%REPO%\src\hlpr" && build -cZ"
if errorlevel 1 goto hlprbuildfail
if exist "%HLPRLOG%.err" goto hlprbuildfail
if not exist "%HLPRSYS%" goto hlprbuildfail

echo.
echo === helper toolchain (%FLAVOR% xhcihlpr.sys: WXP, lib\wxp\i386, subsystem 5.00) ===
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-helper-build.ps1" -LogPath "%HLPRLOG%.log" -Image "%HLPRSYS%" -Dumper "%HLPRWDK%\bin\x86\x86\link.exe"
if errorlevel 1 goto hlprgatefail

echo.
echo === import gate (%FLAVOR% xhcihlpr.sys, hlprx86) ===
call :gated -Hash -Name xhcihlpr.sys -Image "%HLPRSYS%"
if errorlevel 1 goto hlprgatefail
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\import-gate\check-imports.ps1" -Image "%HLPRSYS%" -Flavor %FLAVOR% -Arch x86 -GateProfile hlprx86 -AllowPath "%HLPRALLOW%" %GATEOPT%
if errorlevel 1 goto hlprgatefail
call :gated -Capture -Name xhcihlpr.sys -Image "%HLPRSYS%"
if errorlevel 1 goto hlprgatefail

powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-flavour-marker.ps1" -Image "%HLPRSYS%" -Flavour %FLAVOR%
if errorlevel 1 goto hlprgatefail

powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\source-stamp.ps1" -Driver xhcihlpr -Write "%REPO%\src\hlpr\%OBJDIR%\i386"
if errorlevel 1 goto hlprgatefail
endlocal
exit /b 0

:hlprnowdk
echo.
echo ERROR: xhcihlpr.sys, the x86 helper, builds with WDK 7.1 and none was
echo found at "%HLPRWDK%". Unpack it into tools\WinDDK71 or set WDKROOT
echo (design record 18 sections 8.1 and 15.10; docs\contributing\build-and-test.md).
endlocal
exit /b 1

:hlprbuildfail
echo.
echo ERROR: the %FLAVOR% build of xhcihlpr.sys failed. See "%HLPRLOG%.log" and
echo "%HLPRLOG%.err".
endlocal
exit /b 1

:hlprgatefail
echo.
echo ERROR: the %FLAVOR% xhcihlpr.sys was not built for the WXP target at
echo subsystem 5.00, failed the import gate, carries the wrong flavour marker,
echo or could not be stamped - see above. Do not deploy it.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :builduas <release|debug|qemu>
rem
rem xhciuas.sys, the UAS class driver (roadmap task 31-A.2): the same DDK,
rem the same flavour and BUILD_ALT_DIR as the xhci98.sys just built, in
rem src\uas, then the import gate against the driver's own allowlist for the
rem architecture, the flavour marker check, and the source stamp
rem make-release.ps1 checks beside xhciuas.sys as it does beside xhci98.sys.
rem ------------------------------------------------------------------
:builduas
setlocal
set "FLAVOR=%~1"
call :flavordirs %FLAVOR%
if "%OBJDIR%"=="" goto badflavor
set "UASALLOW=%REPO%\scripts\import-gate\xhciuas-imports.allow"
if /i "%ARCH%"=="amd64" set "UASALLOW=%REPO%\scripts\import-gate\xhciuas-imports-amd64.allow"
set "UASSYS=%REPO%\src\uas\%OBJDIR%\%ARCHDIR%\xhciuas.sys"
set "UASLOG=%REPO%\src\uas\build%OBJDIR:obj=%"

echo.
echo === build %FLAVOR% xhciuas.sys ===
if exist "%UASSYS%" del "%UASSYS%"
if exist "%UASLOG%.err" del "%UASLOG%.err"
cmd /c "call "%DDKROOT%\bin\setenv.bat" %DDKROOT% %DDKFLAVOR% %SETENVARGS% && set "BUILD_ALT_DIR=%ALTDIR%" && cd /d "%REPO%\src\uas" && build -cZ"
if errorlevel 1 goto uasbuildfail
if exist "%UASLOG%.err" goto uasbuildfail
if not exist "%UASSYS%" goto uasbuildfail

echo.
echo === import gate (%FLAVOR% xhciuas.sys) ===
call :gated -Hash -Name xhciuas.sys -Image "%UASSYS%"
if errorlevel 1 goto uasgatefail
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\import-gate\check-imports.ps1" -Image "%UASSYS%" -Flavor %FLAVOR% -Arch %ARCH% -AllowPath "%UASALLOW%" %GATEOPT%
if errorlevel 1 goto uasgatefail
call :gated -Capture -Name xhciuas.sys -Image "%UASSYS%"
if errorlevel 1 goto uasgatefail

powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-flavour-marker.ps1" -Image "%UASSYS%" -Flavour %FLAVOR%
if errorlevel 1 goto uasgatefail

powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\source-stamp.ps1" -Driver xhciuas -Write "%REPO%\src\uas\%OBJDIR%\%ARCHDIR%"
if errorlevel 1 goto uasgatefail
endlocal
exit /b 0

:uasbuildfail
echo.
echo ERROR: the %FLAVOR% build of xhciuas.sys failed. See "%UASLOG%.log" and
echo "%UASLOG%.err".
endlocal
exit /b 1

:uasgatefail
echo.
echo ERROR: the %FLAVOR% xhciuas.sys failed the import gate, carries the
echo wrong flavour marker, could not be stamped, or could not be recorded - see
echo above. Do not deploy it.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :buildcm <release|debug|qemu>
rem
rem xhcicm.sys, the Thunderbolt 3 connection manager (roadmap-usb4.md task
rem 39-A.1, design record 21): the same toolchain, flavour and
rem BUILD_ALT_DIR as the xhciuas.sys just built, in src\cm, then the import
rem gate against the driver's own allowlist for the architecture, the
rem flavour marker check, and the source stamp.
rem ------------------------------------------------------------------
:buildcm
setlocal
set "FLAVOR=%~1"
call :flavordirs %FLAVOR%
if "%OBJDIR%"=="" goto badflavor
set "CMALLOW=%REPO%\scripts\import-gate\xhcicm-imports.allow"
if /i "%ARCH%"=="amd64" set "CMALLOW=%REPO%\scripts\import-gate\xhcicm-imports-amd64.allow"
set "CMSYS=%REPO%\src\cm\%OBJDIR%\%ARCHDIR%\xhcicm.sys"
set "CMLOG=%REPO%\src\cm\build%OBJDIR:obj=%"

echo.
echo === build %FLAVOR% xhcicm.sys ===
if exist "%CMSYS%" del "%CMSYS%"
if exist "%CMLOG%.err" del "%CMLOG%.err"
cmd /c "call "%DDKROOT%\bin\setenv.bat" %DDKROOT% %DDKFLAVOR% %SETENVARGS% && set "BUILD_ALT_DIR=%ALTDIR%" && cd /d "%REPO%\src\cm" && build -cZ"
if errorlevel 1 goto cmbuildfail
if exist "%CMLOG%.err" goto cmbuildfail
if not exist "%CMSYS%" goto cmbuildfail

echo.
echo === import gate (%FLAVOR% xhcicm.sys) ===
call :gated -Hash -Name xhcicm.sys -Image "%CMSYS%"
if errorlevel 1 goto cmgatefail
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\import-gate\check-imports.ps1" -Image "%CMSYS%" -Flavor %FLAVOR% -Arch %ARCH% -AllowPath "%CMALLOW%" %GATEOPT%
if errorlevel 1 goto cmgatefail
call :gated -Capture -Name xhcicm.sys -Image "%CMSYS%"
if errorlevel 1 goto cmgatefail

powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-flavour-marker.ps1" -Image "%CMSYS%" -Flavour %FLAVOR%
if errorlevel 1 goto cmgatefail

powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\source-stamp.ps1" -Driver xhcicm -Write "%REPO%\src\cm\%OBJDIR%\%ARCHDIR%"
if errorlevel 1 goto cmgatefail
endlocal
exit /b 0

:cmbuildfail
echo.
echo ERROR: the %FLAVOR% build of xhcicm.sys failed. See "%CMLOG%.log" and
echo "%CMLOG%.err".
endlocal
exit /b 1

:cmgatefail
echo.
echo ERROR: the %FLAVOR% xhcicm.sys failed the import gate, carries the
echo wrong flavour marker, or could not be stamped - see above. Do not deploy it.
endlocal
exit /b 1

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
echo ERROR: unknown option "%BADOPT%". The options after the flavor are
echo -NoTargetEvidence, which skips the import gate's target-file evidence
echo steps on a host with none staged, and -amd64, which builds for amd64 with
echo WDK 7.1 instead of x86 with the Windows 2000 DDK. Either order.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :readopt <word>
rem
rem One optional switch, in either position. An empty argument is the ordinary
rem case of a caller passing fewer than three. Anything unrecognised is left in
rem BADOPT rather than refused here, so that the caller can refuse once, before
rem any self-test has run - a mistyped switch used to reach check-imports.ps1
rem verbatim and fail parameter binding, which reads as the binary failing the
rem gate rather than as a typo.
rem ------------------------------------------------------------------
:readopt
if "%~1"=="" exit /b 0
if /i "%~1"=="-amd64" set "ARCH=amd64" & exit /b 0
if /i "%~1"=="-NoTargetEvidence" set "GATEOPT=-NoTargetEvidence" & exit /b 0
set "BADOPT=%~1"
exit /b 0

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

:hlprfail
echo.
echo ERROR: the %FLAVOR% xhcihlpr.sys, the x86 helper, failed - see above.
endlocal
exit /b 1

:uasfail
echo.
echo ERROR: the %FLAVOR% xhciuas.sys did not build or did not pass its gates.
endlocal
exit /b 1

:cmfail
echo.
echo ERROR: the %FLAVOR% xhcicm.sys did not build or did not pass its gates.
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
echo ERROR: %INFFILE% failed the setup-engine gate. Do not install it -
echo Win98's setup engine has no log, and a Win2000 install that creates no
echo service looks the same in Device Manager as a driver that loaded and
echo failed.
endlocal
exit /b 1

:nowdk
echo.
echo ERROR: %DDKROOT%\bin\setenv.bat not found.
echo The -amd64 leg builds with WDK 7.1, expected inside this repository at
echo tools\WinDDK71. Nothing is installed machine-wide - it is unpacked there
echo with `msiexec /a` and installs nothing; see
echo docs\contributing\design\11-x64-targets.md section 4.
echo (or set WDKROOT to a WDK 7.1 installed elsewhere - the same variable the
echo import gate reads).
endlocal
exit /b 1

:pkgtestfail
echo.
echo ERROR: the install-media packager's self-tests failed, so any package it
echo builds is untrustworthy - including where it puts each file and whether
echo a Microsoft file has crept back onto the media.
endlocal
exit /b 1

:matrixtestfail
echo.
echo ERROR: the VM matrix verdict self-tests failed, so a matrix run's PASS,
echo FAIL and NODRIVER words cannot be trusted. Fix scripts\vm-matrix first.
endlocal
exit /b 1

:eoltestfail
echo.
echo ERROR: a tracked batch file is not CRLF. MS-DOS 7.1 COMMAND.COM can fail
echo to find goto labels in an LF-only file, silently breaking its error paths.
endlocal
exit /b 1

:charsetfail
echo.
echo ERROR: tracked source carries a stray control byte or a UTF-8 BOM where the
echo 1998-era toolchain cannot read one. See the check's output above.
endlocal
exit /b 1

:mktestfail
echo.
echo ERROR: xhcimark's self-test failed: its pure half, its switches or its
echo import table cannot be trusted (xhcimark\selftest.cmd).
endlocal
exit /b 1

:trtestfail
echo.
echo ERROR: xhcitr's self-test failed: its switches, its pure half or its
echo import table cannot be trusted (xhcitr\selftest.cmd).
endlocal
exit /b 1

:insttestfail
echo.
echo ERROR: xhciinst's self-test failed: its switches, its decisions or its
echo import table cannot be trusted (xhciinst\selftest.cmd).
endlocal
exit /b 1

:clntestfail
echo.
echo ERROR: xhcicln's self-test failed, so its choice of which old devnodes
echo to remove cannot be trusted.
endlocal
exit /b 1

:snaptestfail
echo.
echo ERROR: xhcisnap's report self-test failed, so a dump's "send this" line
echo cannot be trusted to mean the report was written in full.
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

:extradefines
echo.
echo ERROR: XHCI_EXTRA_DEFINES is set:
echo   XHCI_EXTRA_DEFINES=%XHCI_EXTRA_DEFINES%
echo xhci98.sys has no diagnostic builds since the HCD replaced the miniport
echo on 2026-10-02 (design record 13). Clear it and build again:
echo   set XHCI_EXTRA_DEFINES=
endlocal
exit /b 1

:failed
echo.
echo BUILD + GATES FAILED.
endlocal
exit /b 1
