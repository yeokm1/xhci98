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
rem   0. every text check of this build and of the release cut, first, so a
rem      text failure costs no build (roadmap-usb4 task 44.1;
rem      scripts\check-text.ps1, self-tested first): the tracked batch
rem      files' line endings - MS-DOS 7.1 COMMAND.COM can fail on an
rem      LF-only .BAT, and the field wrappers are .BAT files - the source
rem      charset, the tool readmes and the release history the cut
rem      publishes held to its 78-column printable-ASCII rule (task 43.61),
rem      and the texts make-release.ps1 renders from its own templates and
rem      package-common.ps1's readme parts, held to the same rule and to
rem      the cut's other text refusals, the changelog entry for the
rem      version, and the content rules of the batches the cut publishes
rem   1. XHCI_EXTRA_DEFINES is unset - the HCD has no diagnostic builds
rem   2. the import gate's evidence-manifest regression tests, and its
rem      FLAVORS-column tests (release, qemu and the retired words)
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
rem   7. the two copies of Stor98's interface
rem      header held equal (task 42-A.20, scripts\check-stor98-header.ps1),
rem      and on the x86 run XHCITR rebuilt (xhcitr\build.cmd), the tool
rem      that compiles xhciinst\inst_core.c (XHCICLN did too, until it was
rem      dropped in 3.3.0.0)
rem   8. XHCITR's own and its snapshot self-tests, when that EXE has been
rem      built (xhcitr\snaptest.cmd,
rem      XHCISNAP's, moved by roadmap-usb4 task 42-A.4), and the XHCIINST self-test and import
rem      check when that one has (task 40.5; xhciinst\build.cmd), and the
rem      XHCIDI import gate when the 16-bit helper has (task 42-A.1;
rem      xhciinst\di\build.cmd)
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
rem  14b. the fourth binary, xhcinet.sys - the NDIS 5 miniport of
rem      Thunderbolt networking (roadmap-usb4.md task 45.59 step S6, design
rem      record 35), built from src\net\ndis5 after xhcicm.sys for each
rem      flavour and architecture: its import gate (-GateProfile net98 over
rem      scripts\import-gate\xhcinet-imports.allow on x86, the default amd64
rem      rules over xhcinet-imports-amd64.allow), the frame-pointer check and
rem      the flavour marker check, and its two INFs' own gate
rem      (scripts\inf-gate\check-net-inf.ps1, self-tested first, over
rem      src\net\xhcinet.inf and xhcinet-amd64.inf on every build). It is
rem      hashed into the gated record and shipped. It has no source stamp of
rem      its own (source-stamp.ps1 takes no -Driver for it): make-release.ps1
rem      holds the published image to this build by hash and to its sources
rem      by time, and the gated record's input rules (package-common.ps1)
rem      include those sources
rem  14c. the fifth binary, xhcinet6.sys - the NDIS 6.20 miniport of
rem      Thunderbolt networking for 8, 8.1 and 10 (task 45.59 step S7,
rem      record 35 section 7.9), built from src\net\ndis6 after xhcinet.sys
rem      for each flavour and architecture with WDK 7.1's WIN7 environment
rem      (setenv <WDK> <flavor> x86 WIN7 no_oacr, or x64 WIN7 no_oacr; the
rem      one kit here with NDIS 6.20), so every build needs tools\WinDDK71
rem      (or WDKROOT): its import gate (-GateProfile net6 over
rem      scripts\import-gate\xhcinet6-imports.allow or
rem      xhcinet6-imports-amd64.allow) and the flavour marker check. Its INF
rem      sections are 14b's two INFs'. In the gated record, with no source
rem      stamp, as 14b
rem  14a. Stor98 (src\stor98; roadmap-usb4 task 42-A.30), which the package
rem      bundles from this build since 3.1.0.0: its three INFs held to
rem      src\xhci_version.h's DriverVer (scripts\inf-gate\check-stor98-inf.ps1,
rem      self-tested first) beside the other INF gates; on the x86 run its
rem      host unit suite (src\stor98\tests\unit\build.cmd) after this
rem      project's; then, once per run, src\stor98\scripts\build.cmd builds
rem      the free images of this architecture (stor98.mpd and the x86
rem      stor98.sys, or the amd64 stor98.sys) into src\stor98\out\free\,
rem      and every flavour gates them (:gatestor98): the version, the
rem      STOR98_FLAVOUR_FREE marker and the import allowlist
rem      (scripts\check-stor98-build.ps1, scripts\import-gate\
rem      stor98-imports.allow), the source stamp, and the gated record
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
rem Steps 2 to 9 and the INF, UAS, CM, Stor98 and txtsetup.oem gates of
rem steps 3, 12, 13, 14 and 14a need no DDK, and since roadmap-usb4 task
rem 45.3 they run at once, after step 0, through scripts\build-steps.ps1
rem (build-and-test.md, "A faster gated build"); each step of the run says
rem its time in the log ("--- <step>: m:ss").
rem
rem Any failure stops the run. scripts\local\ddk-debug.cmd still exists for an
rem interactive DDK prompt, but a binary built that way has not been through the
rem gates - do not deploy one.
rem
rem VOCABULARY. This project says "release" and "qemu" for the two build
rem flavors, everywhere except where the DDK's own words are literally
rem required: setenv.bat's third argument (free|checked), the src\objfre /
rem src\objchk_qemu output trees, and the buildfre / buildchk_qemu log
rem names. The mapping is made once, in :flavordirs below.
rem
rem TWO FLAVORS, since roadmap-usb4 task 45.45 (design record 08 section 14):
rem
rem   release  free     ships; no port-0xE9 mirror. Its MSVC 6.0 x86 builds
rem                     keep frame pointers (/Oy-, NTNOFPO in each sources,
rem                     held by scripts\check-frame-pointers.ps1) and the
rem                     command-state checker runs unless XhciCommandCheck
rem                     is 0
rem   qemu     checked  the emulator/bench build: port-0xE9 mirror and the live
rem                     per-line trace.  NEVER PUBLISHED.
rem
rem The third, debug (checked, the diagnostic download, task 13-L.1), was
rem retired with task 45.45: "debug" and "both" are refused. qemu's import
rem HAL.dll!WRITE_PORT_UCHAR is the sole import delta between 0.0.0.4's two
rem published binaries, of which the debug one gave the ThinkPad E460 a Code 2.
rem WHY that build failed is not established (defect 2b), so the import gate
rem carries it as "qemu required" and no published binary can have it.
rem
rem This builds and gates; it does not package. The install media a VM is
rem fed is assembled by a separate explicit step, which re-runs the INF gate
rem against the finished directory so a package is never less gated than the
rem binary in it:
rem
rem   scripts\package\make-package.ps1 [-Flavor release|qemu] [-Arch x86|amd64]
rem
rem The media holds no Microsoft file: it is the drivers (xhcihlpr.sys on x86
rem only) and their INFs, txtsetup.oem, on amd64 the test certificate, and a
rem pinned Stor98 build when one is asked for (make-package.ps1 lists them).
rem The OS supplies usbd.sys and usbui.dll through the INF's LayoutFile. The
rem HCD replaces usbport.sys and usbhub.sys, and the INF gate refuses an INF
rem that fetches either (OS-HCDREPLACED).
rem
rem Usage:  scripts\build-driver.cmd [release|qemu|all]
rem                                  [-amd64] [-NoTargetEvidence]
rem                                                       (default: release)
rem
rem   "release" is the default deliberately: widening it would build a binary
rem   that must never be published on every ordinary run.  "all" is the two,
rem   and is what a release cut uses so that every flavor is gated even though
rem   only release is staged.
rem
rem   The DDK and MSVC 6.0 both live inside this repository - tools\ntddk and
rem   tools\MSVC600 - and are found relative to this script, so a
rem   clone builds wherever it is unpacked, at a path with no space (one
rem   with a space is refused at once), and nothing is installed under C:\.
rem   Set DDKROOT to build this tree's 32-bit drivers against a DDK
rem   somewhere else, and WDKROOT for WDK 7.1 (the -amd64 leg, xhcihlpr.sys
rem   and xhcinet6.sys). Stor98's build reads neither: it takes MSVC 6.0,
rem   both kits and the 98 DDK headers from STOR98_TOOLS, by default tools\
rem   (src\stor98\scripts\build.cmd).
rem   Add -NoTargetEvidence after the flavor to skip the gate's target-file
rem   evidence steps on a host that has none staged.
rem
rem SECOND ARCHITECTURE, since roadmap task 21.2. -amd64 builds the same
rem flavors for amd64 with WDK 7.1 (tools\WinDDK71) instead of the Windows 2000
rem DDK, which cannot target it. The two toolchains are separate all the way
rem down and the x86 path is untouched by the switch:
rem
rem   x86    tools\ntddk       setenv <root> <flavor> w2k x86
rem                                                     src\obj*\i386
rem   amd64  tools\WinDDK71    setenv <root> <flavor> x64 WNET no_oacr
rem                                                     src\obj*\amd64
rem
rem BUILD_ALT_DIR is overridden back to fre/chk_qemu after setenv.bat in
rem BOTH cases. WDK 7.1's setenv sets it to fre_wnet_AMD64, which src\sources
rem hard-errors on, and build.exe appends the architecture itself - so the
rem override is what puts the output at src\objfre\amd64 beside src\objfre\i386
rem with no second obj root and nothing renamed.
rem
rem WHAT AN amd64 BUILD IS GATED ON, since roadmap task 21.3 closed: the import
rem gate enforces xhci98-imports-amd64.allow and resolves every pair against
rem authenticated NT 5.2 amd64 baselines (winxp64-baselines.expected), and the
rem INF gates run over all thirteen production INFs on every build, whichever
rem architecture is being built: check-inf.ps1 over src\xhci98.inf (-Arch
rem x86) and src\xhci98-amd64.inf (-Arch amd64), check-uas-inf.ps1 over the
rem two UAS INFs, check-cm-inf.ps1 over the four connection manager INFs,
rem check-net-inf.ps1 over the two networking INFs and check-stor98-inf.ps1
rem over Stor98's three (steps 3, 12, 14, 14b and 14a above). All of them, always, because each x86/amd64
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
rem A REPOSITORY PATH WITH A SPACE IS REFUSED HERE, before anything has run.
rem Every toolchain this run uses defaults to one under %REPO%\tools, the
rem DDK's and the WDK's setenv.bat take their root verbatim and unquoted,
rem and Stor98's build, which every run makes, refuses such a path outright
rem (src\stor98\scripts\build.cmd) - so an 8.3 short name for the DDK alone,
rem which this script once tried, could never have carried the run through.
for /f "tokens=2 delims= " %%S in ("%REPO%") do goto repospace
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
if "%FLAVORS%"=="" set "FLAVORS=release"
rem Task 45.45 retired the debug flavour, and "both" meant debug and
rem release: refused by name, before a single self-test, rather than read as
rem release, which would hide a caller that still expects two binaries.
if /i "%FLAVORS%"=="both" goto retiredflavor
if /i "%FLAVORS%"=="debug" goto retiredflavor
rem "all" is what a cut builds (cut-builds.ps1, or by hand for -ImportOnly or
rem a one-tree cut), so it runs every self-test in full: the INF gate's
rem self-tests skip themselves when nothing they read has changed since
rem their last passing run, except under this (roadmap-usb4 task 45.30).
rem A pre-merge build of a branch sets XHCI98_SELFTEST_SKIP_OK=1 to let that
rem skip stand in an "all" build too; a cut never sets it, and
rem XHCI98_FULL_SELFTESTS=1 set by the caller still wins (owner, 2026-10-30).
if /i "%FLAVORS%"=="all" if not "%XHCI98_SELFTEST_SKIP_OK%"=="1" set "XHCI98_FULL_SELFTESTS=1"
if /i "%FLAVORS%"=="all" set "FLAVORS=release qemu"
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
rem And the tree's own lock, beside it: the two architectures' runs write the
rem same build logs (src\buildfre.log and .err, and each sub-driver's), so an
rem x86 and an amd64 run in one tree at once delete and fail on each other's
rem .err, and the frame-pointer check can read the other's log. The
rem per-architecture lock stays, because the gated record and the tools that
rem take it from outside (package-common.ps1) are per architecture.
set "TREELOCK=%REPO%\out\gated-tree.lock"
mkdir "%TREELOCK%" 2>nul
if errorlevel 1 goto treelocked
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
if exist "%TREELOCK%" rmdir /s /q "%TREELOCK%"
exit /b 0

:treelocked
rmdir /s /q "%GATEDLOCK%"
echo ERROR: "%TREELOCK%" exists: another build-driver.cmd run holds this
echo tree, for the other architecture - both write the same build logs. Wait
echo for it to finish. If none is running - a run that was interrupted with
echo Ctrl+C leaves the lock behind - delete the directory:
echo   rmdir /s /q "%TREELOCK%"
endlocal
exit /b 1

:gatedlocked
echo ERROR: "%GATEDLOCK%" exists: another build-driver.cmd run for %ARCH% holds
echo this tree. Wait for it to finish. If none is running - a run that was
echo interrupted with Ctrl+C leaves the lock behind - delete the directory:
echo   rmdir /s /q "%GATEDLOCK%"
endlocal
exit /b 1

:mainbody
setlocal
call :stepclock RUNT0
rem Every text check of this build and of the release cut, before anything
rem else: none needs a toolchain, and each costs seconds against a build's
rem hour (task 44.1). Until then the build met its text checks after its
rem other self-tests and the cut met its own only after the gated builds;
rem the 3.2.0.0 builds of 2026-10-24 stopped twice on a line past 78
rem columns. Self-tested first. The bytes inside tracked source are among
rem them (task 22.7): every other gate here reads src\ through a compiler
rem or a parser that accepts any byte inside a comment or a string literal,
rem and a BEL written into src\xhci_slot.c by a PowerShell escape passed
rem all three x86 flavours and every gate on 2026-09-12.
echo.
echo === text checks ===
call :stepclock STEPT0
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-text.ps1" -SelfTest
if errorlevel 1 goto textfail
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-text.ps1"
if errorlevel 1 goto textfail
call :steptime STEPT0 "text checks"

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
rem variable redirects the 64-bit toolchain of this tree's drivers rather
rem than each script having its own (Stor98's build takes STOR98_TOOLS
rem instead; see the usage above).
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
rem derives its paths from the first half. The repository's own path is
rem refused with a space at the top (:repospace), so only a DDKROOT override
rem outside it can hold one here: :shortpath tries its 8.3 short name before
rem refusing, and the re-check afterwards is the proof the short name names
rem the same DDK.
call :shortpath DDKROOT
if errorlevel 1 goto ddkspace
if not exist "%DDKROOT%\bin\setenv.bat" goto ddkspace
echo DDK: %DDKROOT%

rem The checks that need no DDK, at once (roadmap-usb4 task 45.3;
rem build-and-test.md, "A faster gated build"): the import gate's
rem self-tests, the INF, UAS, CM, Stor98 and txtsetup.oem gates with their
rem self-tests, the packager's, the QEMU launchers' and the VM matrix's
rem self-tests, the Stor98 interface header, on the x86 run XHCITR and
rem XHCIPERF rebuilt (XHCITR compiles the installer's pure half, so a cut
rem never needs it built by hand) and their self-tests, the XHCIINST,
rem XHCIMARK and XHCIDI self-tests when their EXEs are there, the host suite
rem and on the x86 run Stor98's unit suite. scripts\build-steps.ps1 holds
rem them as a table, with what each waits for and the message each failure
rem gives; XHCI98_BUILD_STEPS says how many run at once (default 4, 1 one at
rem a time in the table's order). The runner is self-tested first. The host
rem suite still runs before the DDK builds, not after: it compiles the same
rem core files in seconds, so a bad carve, ring or PORTSC constant should
rem not cost two full builds first.
echo.
echo === the build-step runner's self-tests ===
call :stepclock STEPT0
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\build-steps.ps1" -SelfTest
if errorlevel 1 goto stepstestfail
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\build-steps.ps1" -Arch %ARCH%
if errorlevel 2 goto inconclusive
if errorlevel 1 goto stepsfail
call :steptime STEPT0 "the checks that need no DDK"


rem Stor98 (task 42-A.30): built once per run, free only - the package ships
rem its free build whatever this project's flavour - for this architecture,
rem then gated in each flavour below (:gatestor98). The images it replaces
rem (this architecture's only; the other run's stay) are deleted first, so a
rem failed build cannot leave an old one to gate.
echo.
echo === build Stor98 (free, %ARCH%) ===
call :stepclock STEPT0
set "STOR98_DBGPORT="
set "S98TARGETS=9x x86"
set "S98IMAGES=win9x\stor98.mpd x86\stor98.sys"
if /i "%ARCH%"=="amd64" set "S98TARGETS=amd64"
if /i "%ARCH%"=="amd64" set "S98IMAGES=amd64\stor98.sys"
for %%T in (%S98IMAGES%) do if exist "%REPO%\src\stor98\out\free\%%T" del "%REPO%\src\stor98\out\free\%%T"
call "%REPO%\src\stor98\scripts\build.cmd" "%S98TARGETS%" free
if errorlevel 1 goto stor98fail
call :steptime STEPT0 "build Stor98"

rem S98VSD.VXD (task 43.46, design record 30 section 6.4), the Windows 98
rem SE and ME IOS layer for Stor98's disks, on the x86 run: the release
rem image always, the qemu flavour's (its debug-console writes) when this
rem run builds qemu. Each link is gated by src\vsd\check-vsd.ps1 inside
rem src\vsd\build.cmd; a 2 is "not built" (no Open Watcom or no 98 SE DDK
rem on this host), said and passed over as for XHCIDI.EXE, and
rem make-release.ps1 refuses a cut without the image.
if /i "%ARCH%"=="amd64" goto skipvsd
echo.
echo === build S98VSD.VXD ===
call :stepclock STEPT0
for %%V in (obj\S98VSD.VXD obj\qemu\S98VSD.VXD) do if exist "%REPO%\src\vsd\%%V" del "%REPO%\src\vsd\%%V"
call "%REPO%\src\vsd\build.cmd"
if errorlevel 2 goto vsdnotbuilt
if errorlevel 1 goto vsdfail
set "VSDQ="
for %%F in (%FLAVORS%) do if /i "%%F"=="qemu" set "VSDQ=1"
if not defined VSDQ goto skipvsd
call "%REPO%\src\vsd\build.cmd" qemu
if errorlevel 1 goto vsdfail
goto skipvsd
:vsdnotbuilt
echo S98VSD.VXD was not built on this host (see above); the package goes without it.
:skipvsd
if /i not "%ARCH%"=="amd64" call :steptime STEPT0 "build S98VSD.VXD"

rem XHCIPFW.VXD (task 44.49, design record 32 section 8), the Windows 98
rem SE and ME static VxD that closes PCI bridges' prefetchable windows
rem lying wholly above 4 GB before the configuration manager reads them,
rem on the x86 run as S98VSD.VXD is: the release image always, the qemu
rem flavour's when this run builds qemu, each link gated by
rem src\pfw\check-pfw.ps1 inside src\pfw\build.cmd; a 2 is "not built"
rem (no Open Watcom or no 98 SE DDK on this host), said and passed over.
if /i "%ARCH%"=="amd64" goto skippfw
echo.
echo === build XHCIPFW.VXD ===
call :stepclock STEPT0
for %%V in (obj\XHCIPFW.VXD obj\qemu\XHCIPFW.VXD) do if exist "%REPO%\src\pfw\%%V" del "%REPO%\src\pfw\%%V"
call "%REPO%\src\pfw\build.cmd"
if errorlevel 2 goto pfwnotbuilt
if errorlevel 1 goto pfwfail
set "PFWQ="
for %%F in (%FLAVORS%) do if /i "%%F"=="qemu" set "PFWQ=1"
if not defined PFWQ goto skippfw
call "%REPO%\src\pfw\build.cmd" qemu
if errorlevel 1 goto pfwfail
goto skippfw
:pfwnotbuilt
echo XHCIPFW.VXD was not built on this host (see above); the package goes without it.
:skippfw
if /i not "%ARCH%"=="amd64" call :steptime STEPT0 "build XHCIPFW.VXD"

rem XHCIPFW.SYS (task 44.49, design record 32 section 11), the same step for
rem Windows 2000 and the 32-bit Vista, 7, 8, 8.1 and 10: a boot-start driver
rem built with the Windows 2000 DDK by src\pfw\nt\build.cmd on the x86 run,
rem the release image always and the qemu image when this run builds qemu,
rem each gated there (the import gate's hlprx86 profile, check-pfwsys.ps1).
if /i "%ARCH%"=="amd64" goto skippfwsys
echo.
echo === build XHCIPFW.SYS ===
call :stepclock STEPT0
for %%V in (objfre\i386\xhcipfw.sys objfre_qemu\i386\xhcipfw.sys) do if exist "%REPO%\src\pfw\nt\%%V" del "%REPO%\src\pfw\nt\%%V"
call "%REPO%\src\pfw\nt\build.cmd"
if errorlevel 2 goto pfwsysnotbuilt
if errorlevel 1 goto pfwsysfail
set "PFWQ="
for %%F in (%FLAVORS%) do if /i "%%F"=="qemu" set "PFWQ=1"
if not defined PFWQ goto skippfwsys
call "%REPO%\src\pfw\nt\build.cmd" qemu
if errorlevel 1 goto pfwsysfail
goto skippfwsys
:pfwsysnotbuilt
echo XHCIPFW.SYS was not built on this host (see above); the package goes without it.
:skippfwsys
if /i not "%ARCH%"=="amd64" call :steptime STEPT0 "build XHCIPFW.SYS"

rem `if errorlevel` inside the block is evaluated per iteration, so a first
rem flavor that fails cannot be masked by a second one that succeeds.
for %%F in (%FLAVORS%) do (
    call :buildflavor %%F
    if errorlevel 1 goto failed
)

call :steptime RUNT0 "the whole run"
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
rem :checkflavour <release|qemu>
rem
rem Exactly one XHCI98_FLAVOUR_* string, and it must be this flavor's. Both
rem halves are the check: a binary carrying two markers would be one the
rem preprocessor let through with more than one flavor define, and a binary
rem carrying the wrong one is a qemu build wearing a release name, or an old
rem debug image - which is the confusion the marker exists to make impossible.
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
rem already makes for XHCIQUAL (and made for XHCISNAP; the audit of 2026-09-07). It is
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
echo src\sources derives XHCI_FLAVOUR_RELEASE / _QEMU from
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
rem :flavordirs <release|qemu>
rem
rem The one place a flavor name becomes a DDK output tree. Sets OBJDIR and
rem ALTDIR, and leaves OBJDIR empty for a name this project does not know -
rem every caller tests that and goes to :badflavor.
rem
rem ALTDIR is the DDK's BUILD_ALT_DIR, which build.exe concatenates onto `obj`
rem to make the output tree: fre -> objfre, chk_qemu -> objchk_qemu.
rem setenv.bat sets the first itself (and chk, the retired debug flavour's,
rem which no sources accepts since task 45.45); the second is this
rem project's, a tree of its own for the checked qemu build.
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
if /i "%~1"=="qemu" (
    set "OBJDIR=objchk_qemu"
    set "ALTDIR=chk_qemu"
    set "DDKFLAVOR=checked"
)
exit /b 0

rem ------------------------------------------------------------------
rem :buildflavor <release|qemu>
rem
rem DDKFLAVOR is the DDK's own word for the same thing, and it exists only
rem because setenv.bat takes it as an argument. It no longer identifies the
rem build on its own: the retired debug flavour and qemu were both "checked",
rem which is why ALTDIR exists.
rem ------------------------------------------------------------------
:buildflavor
setlocal
set "FLAVOR=%~1"
call :stepclock FLVT0
call :flavordirs %FLAVOR%
if "%OBJDIR%"=="" goto badflavor
set "OUTSYS=%REPO%\src\%OBJDIR%\%ARCHDIR%\xhci98.sys"
rem objfre -> buildfre, objchk_qemu -> buildchk_qemu: the
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
call :checkfpo "%LOGBASE%.log"
if errorlevel 1 goto fpofail

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

call :buildnet %FLAVOR%
if errorlevel 1 goto netfail

call :buildnet6 %FLAVOR%
if errorlevel 1 goto net6fail

rem xhcihlpr.sys, the x86 helper (design record 18, section 15), with every
rem x86 flavour and never with -amd64: the amd64 HCD has no helper.
if /i "%ARCH%"=="amd64" goto skiphlpr
call :buildhlpr %FLAVOR%
if errorlevel 1 goto hlprfail
:skiphlpr

rem Stor98's images, built once before the flavours, in every flavour's
rem record (task 42-A.30).
call :gatestor98
if errorlevel 1 goto stor98gatefail

rem Every gate of this flavour has passed: record what they read (roadmap-usb4.md
rem SIGN.1), for make-package.ps1 -SignOnly - from the hashes captured after
rem each import gate, and only if every file still has its captured hash.
echo.
echo === gated build record (%FLAVOR%, %ARCH%) ===
call :gated -Finish %GATEOPT%
if errorlevel 1 goto gatedrecfail
call :steptime FLVT0 "the %FLAVOR% flavour, built and gated"

endlocal
exit /b 0

rem ------------------------------------------------------------------
rem :stepclock <var> and :steptime <var> "<step>" (roadmap-usb4 task 45.3):
rem each step's time in the build log. :stepclock puts the time of day in
rem <var> as hundredths of a second since midnight, read from %TIME% (its
rem leading space made a 0, and each field taken with a 1 in front less
rem 100, so 08 and 09 are not read as octal); :steptime prints
rem "--- <step>: m:ss" from it, a step that crossed midnight given a day.
rem Neither opens a scope: :stepclock's variable is the caller's.
rem ------------------------------------------------------------------
:stepclock
set "STEPNOW=%TIME: =0%"
for /f "tokens=1-4 delims=:.," %%a in ("%STEPNOW%") do set /a "%~1=((1%%a-100)*3600+(1%%b-100)*60+(1%%c-100))*100+(1%%d-100)"
set "STEPNOW="
exit /b 0

:steptime
call :stepclock STEPEND
set /a "STEPCS=STEPEND-%~1"
if %STEPCS% lss 0 set /a "STEPCS+=8640000"
set /a "STEPMIN=STEPCS/6000"
set /a "STEPSEC=(STEPCS/100)%%60"
if %STEPSEC% lss 10 set "STEPSEC=0%STEPSEC%"
echo --- %~2: %STEPMIN%:%STEPSEC%
set "STEPEND="
set "STEPCS="
set "STEPMIN="
set "STEPSEC="
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

rem ------------------------------------------------------------------
rem :gatestor98
rem
rem The Stor98 images of this architecture (task 42-A.30), in the current
rem flavour's record: each hashed, held to scripts\check-stor98-build.ps1
rem (version from src\xhci_version.h, STOR98_FLAVOUR_FREE, machine, the
rem import allowlist scripts\import-gate\stor98-imports.allow), captured,
rem and stamped (scripts\source-stamp.ps1 -Driver stor98 / stor98mpd).
rem ------------------------------------------------------------------
:gatestor98
if /i "%ARCH%"=="amd64" (
    call :gateonestor98 amd64 stor98.sys stor98
    exit /b
)
call :gateonestor98 win9x stor98.mpd stor98mpd
if errorlevel 1 exit /b 1
call :gateonestor98 x86 stor98.sys stor98
exit /b

:gateonestor98
set "S98IMG=%REPO%\src\stor98\out\free\%~1\%~2"
echo.
echo === Stor98 gate (%FLAVOR%: %~1 %~2) ===
if not exist "%S98IMG%" (
    echo ERROR: no "%S98IMG%"
    exit /b 1
)
call :gated -Hash -Name %~2 -Image "%S98IMG%"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-stor98-build.ps1" -Image "%S98IMG%" -Target %~1
if errorlevel 1 exit /b 1
call :gated -Capture -Name %~2 -Image "%S98IMG%"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\source-stamp.ps1" -Driver %~3 -Write "%REPO%\src\stor98\out\free\%~1"
exit /b %errorlevel%

:pfwsysfail
echo.
echo ERROR: XHCIPFW.SYS (src\pfw\nt) failed to build or its gate failed - see above.
endlocal
exit /b 1

:pfwfail
echo.
echo ERROR: XHCIPFW.VXD (src\pfw) failed to build or its gate failed - see above.
endlocal
exit /b 1

:vsdfail
echo.
echo ERROR: S98VSD.VXD (src\vsd) failed to build or its gate failed - see above.
endlocal
exit /b 1

:stor98fail
echo.
echo ERROR: Stor98 (src\stor98) failed to build or its unit tests failed - see
echo above. The package bundles the Stor98 this run builds.
endlocal
exit /b 1

:stor98gatefail
echo.
echo ERROR: a Stor98 image failed its gate in the %FLAVOR% build - its version,
echo flavour marker, imports, record or stamp; see above. Do not package it.
endlocal
exit /b 1

:gatedrecfail
echo.
echo ERROR: the gated build's record of the %FLAVOR% build could not be begun,
echo captured or written - see above. make-package.ps1 -SignOnly needs it; a
echo package built without -SignOnly runs the gates again and does not.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :buildhlpr <release|qemu>
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
rem Passed unquoted to setenv.bat below, as DDKROOT is: a WDKROOT with a
rem space is shortened, or refused.
call :shortpath HLPRWDK
if errorlevel 1 goto hlprspace
if not exist "%HLPRWDK%\bin\setenv.bat" goto hlprspace
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

:hlprspace
echo.
echo ERROR: the WDK path contains a space and has no usable 8.3 short name:
echo   %HLPRWDK%
echo WDK 7.1's setenv.bat takes its root verbatim and cannot be quoted. Set
echo WDKROOT to a copy of WDK 7.1 at a path without a space.
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
rem :builduas <release|qemu>
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
call :checkfpo "%UASLOG%.log"
if errorlevel 1 goto uasbuildfail

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
rem :buildcm <release|qemu>
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
call :checkfpo "%CMLOG%.log"
if errorlevel 1 goto cmbuildfail

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

rem ------------------------------------------------------------------
rem :buildnet <release|qemu>
rem
rem xhcinet.sys, the NDIS 5 miniport of Thunderbolt networking
rem (roadmap-usb4.md task 45.59 step S6, design record 35 section 7.1):
rem the same toolchain, flavour and BUILD_ALT_DIR as xhcicm.sys, in
rem src\net\ndis5, then the import gate (net98 on x86), the frame-pointer
rem check and the flavour marker check.
rem ------------------------------------------------------------------
:buildnet
setlocal
set "FLAVOR=%~1"
call :flavordirs %FLAVOR%
if "%OBJDIR%"=="" goto badflavor
set "NETALLOW=%REPO%\scripts\import-gate\xhcinet-imports.allow"
set "NETPROFILE=-GateProfile net98"
if /i "%ARCH%"=="amd64" set "NETALLOW=%REPO%\scripts\import-gate\xhcinet-imports-amd64.allow"
if /i "%ARCH%"=="amd64" set "NETPROFILE="
set "NETSYS=%REPO%\src\net\ndis5\%OBJDIR%\%ARCHDIR%\xhcinet.sys"
set "NETLOG=%REPO%\src\net\ndis5\build%OBJDIR:obj=%"

echo.
echo === build %FLAVOR% xhcinet.sys ===
if exist "%NETSYS%" del "%NETSYS%"
if exist "%NETLOG%.err" del "%NETLOG%.err"
cmd /c "call "%DDKROOT%\bin\setenv.bat" %DDKROOT% %DDKFLAVOR% %SETENVARGS% && set "BUILD_ALT_DIR=%ALTDIR%" && cd /d "%REPO%\src\net\ndis5" && build -cZ"
if errorlevel 1 goto netbuildfail
if exist "%NETLOG%.err" goto netbuildfail
if not exist "%NETSYS%" goto netbuildfail
call :checkfpo "%NETLOG%.log"
if errorlevel 1 goto netbuildfail

echo.
echo === import gate (%FLAVOR% xhcinet.sys) ===
call :gated -Hash -Name xhcinet.sys -Image "%NETSYS%"
if errorlevel 1 goto netgatefail
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\import-gate\check-imports.ps1" -Image "%NETSYS%" -Flavor %FLAVOR% -Arch %ARCH% -AllowPath "%NETALLOW%" %NETPROFILE% %GATEOPT%
if errorlevel 1 goto netgatefail
call :gated -Capture -Name xhcinet.sys -Image "%NETSYS%"
if errorlevel 1 goto netgatefail

powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-flavour-marker.ps1" -Image "%NETSYS%" -Flavour %FLAVOR%
if errorlevel 1 goto netgatefail
endlocal
exit /b 0

:netbuildfail
echo.
echo ERROR: the %FLAVOR% build of xhcinet.sys failed. See "%NETLOG%.log" and
echo "%NETLOG%.err".
endlocal
exit /b 1

:netgatefail
echo.
echo ERROR: the %FLAVOR% xhcinet.sys failed the import gate or carries the
echo wrong flavour marker - see above. Do not deploy it.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :buildnet6 <release|qemu>
rem
rem xhcinet6.sys, the NDIS 6.20 miniport of Thunderbolt networking for 8,
rem 8.1 and 10 (roadmap-usb4.md task 45.59 step S7, design record 35
rem sections 7.1 and 7.9): WDK 7.1's WIN7 environment for the run's
rem architecture, the same BUILD_ALT_DIR as every binary of the run, in
rem src\net\ndis6; then the import gate's net6 profile and the flavour
rem marker check. No frame-pointer check: that rule is MSVC 6.0's, and
rem this image is WDK 7.1's on both architectures.
rem ------------------------------------------------------------------
:buildnet6
setlocal
set "FLAVOR=%~1"
call :flavordirs %FLAVOR%
if "%OBJDIR%"=="" goto badflavor
set "NET6WDK=%REPO%\tools\WinDDK71"
if not "%WDKROOT%"=="" set "NET6WDK=%WDKROOT%"
if not exist "%NET6WDK%\bin\setenv.bat" goto net6nowdk
call :shortpath NET6WDK
if errorlevel 1 goto net6space
if not exist "%NET6WDK%\bin\setenv.bat" goto net6space
set "NET6ENV=x86 WIN7 no_oacr"
set "NET6ALLOW=%REPO%\scripts\import-gate\xhcinet6-imports.allow"
if /i "%ARCH%"=="amd64" set "NET6ENV=x64 WIN7 no_oacr"
if /i "%ARCH%"=="amd64" set "NET6ALLOW=%REPO%\scripts\import-gate\xhcinet6-imports-amd64.allow"
set "NET6SYS=%REPO%\src\net\ndis6\%OBJDIR%\%ARCHDIR%\xhcinet6.sys"
set "NET6LOG=%REPO%\src\net\ndis6\build%OBJDIR:obj=%"

echo.
echo === build %FLAVOR% xhcinet6.sys (WDK 7.1 %NET6ENV%) ===
if exist "%NET6SYS%" del "%NET6SYS%"
if exist "%NET6LOG%.err" del "%NET6LOG%.err"
if exist "%NET6LOG%.log" del "%NET6LOG%.log"
cmd /c "call "%NET6WDK%\bin\setenv.bat" %NET6WDK% %DDKFLAVOR% %NET6ENV% && set "BUILD_ALT_DIR=%ALTDIR%" && cd /d "%REPO%\src\net\ndis6" && build -cZ"
if errorlevel 1 goto net6buildfail
if exist "%NET6LOG%.err" goto net6buildfail
if not exist "%NET6SYS%" goto net6buildfail

echo.
echo === import gate (%FLAVOR% xhcinet6.sys) ===
call :gated -Hash -Name xhcinet6.sys -Image "%NET6SYS%"
if errorlevel 1 goto net6gatefail
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\import-gate\check-imports.ps1" -Image "%NET6SYS%" -Flavor %FLAVOR% -Arch %ARCH% -AllowPath "%NET6ALLOW%" -GateProfile net6 %GATEOPT%
if errorlevel 1 goto net6gatefail
call :gated -Capture -Name xhcinet6.sys -Image "%NET6SYS%"
if errorlevel 1 goto net6gatefail

powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-flavour-marker.ps1" -Image "%NET6SYS%" -Flavour %FLAVOR%
if errorlevel 1 goto net6gatefail
endlocal
exit /b 0

:net6nowdk
echo.
echo ERROR: xhcinet6.sys needs WDK 7.1 (its WIN7 environment), which was not
echo found at "%NET6WDK%". Unpack it into tools\WinDDK71 or set WDKROOT.
endlocal
exit /b 1

:net6space
echo.
echo ERROR: the WDK path contains a space and has no usable 8.3 short name:
echo   %NET6WDK%
echo WDK 7.1's setenv.bat takes its root verbatim and cannot be quoted. Set
echo WDKROOT to a copy of WDK 7.1 at a path without a space.
endlocal
exit /b 1

:net6buildfail
echo.
echo ERROR: the %FLAVOR% build of xhcinet6.sys failed. See "%NET6LOG%.log" and
echo "%NET6LOG%.err".
endlocal
exit /b 1

:net6gatefail
echo.
echo ERROR: the %FLAVOR% xhcinet6.sys failed the import gate or carries the
echo wrong flavour marker - see above. Do not deploy it.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :checkfpo "<build log>" - on the x86 release build of xhci98.sys,
rem xhciuas.sys, xhcicm.sys and xhcinet.sys (MSVC 6.0), every compile kept
rem frame pointers (/Oy-; task 45.45, design record 08 section 14). Nothing to
rem check on amd64 (WDK 7.1) or in the qemu flavour.
rem ------------------------------------------------------------------
:checkfpo
if /i not "%ARCH%"=="x86" exit /b 0
if /i not "%FLAVOR%"=="release" exit /b 0
powershell -NoProfile -ExecutionPolicy Bypass -File ^
    "%REPO%\scripts\check-frame-pointers.ps1" -Log "%~1"
exit /b %errorlevel%

:fpofail
echo.
echo ERROR: the release build of xhci98.sys was compiled without frame
echo pointers - see above. src\sources sets NTNOFPO=1 in its fre branch.
endlocal
exit /b 1

:retiredflavor
echo ERROR: "%FLAVORS%" names the debug flavour, which roadmap-usb4 task
echo 45.45 retired (design record 08 section 14): "both" was debug and
echo release. Use release, the default, or all for release and qemu.
echo The release build keeps the debug build's frame pointers and its
echo command-state checker (XhciCommandCheck).
endlocal
exit /b 1

:badflavor
echo ERROR: unknown build flavor "%FLAVOR%" - use release, qemu or all.
echo (The flavor formerly called "standard" is now "release".
echo It is a hard cut: the old word is not accepted.)
echo (There are TWO flavors since task 45.45: release ships and is the
echo default; "all" adds qemu, which carries the port-0xE9 mirror and must
echo NEVER be published. debug and both were retired with it.)
echo (The DDK's own words are free and checked; this project uses them only
echo where setenv.bat forces it - see :flavordirs.)
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

:netfail
echo.
echo ERROR: the %FLAVOR% xhcinet.sys did not build or did not pass its gates.
endlocal
exit /b 1

:net6fail
echo.
echo ERROR: the %FLAVOR% xhcinet6.sys did not build or did not pass its gates.
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
echo path with a space is split and every derived path is wrong. Install the
echo DDK to a path without a space and point DDKROOT (WDKROOT on the -amd64
echo leg) at it:
echo   powershell -ExecutionPolicy Bypass -File scripts\install-w2kddk-cabs.ps1 -DdkPath C:\NTDDK
endlocal
exit /b 1

:repospace
echo.
echo ERROR: the repository path contains a space:
echo   %REPO%
echo The DDK's and WDK's setenv.bat take their root unquoted, every toolchain
echo defaults to one under tools\, and Stor98's build refuses such a path.
echo Clone this repository to a path without a space.
endlocal
exit /b 1

rem ------------------------------------------------------------------
rem :shortpath NAME - the variable NAME, if its value holds a space, set to
rem that path's 8.3 short name; errorlevel 1 when the short name still holds
rem one. A short name exists only where the volume generates them (8dot3
rem creation is off on many non-system volumes), and %%~sfI silently returns
rem the long path when there is none, hence the re-check rather than trusting
rem the expansion. The caller checks the shortened path still names its kit.
:shortpath
call set "SPVAL=%%%~1%%"
set "SPFIRST="
for /f "delims= " %%S in ("%SPVAL%") do set "SPFIRST=%%S"
if "%SPFIRST%"=="%SPVAL%" goto shortpathok
for %%I in ("%SPVAL%") do set "SPVAL=%%~sfI"
set "SPFIRST="
for /f "delims= " %%S in ("%SPVAL%") do set "SPFIRST=%%S"
if not "%SPFIRST%"=="%SPVAL%" (
    set "SPVAL="
    set "SPFIRST="
    exit /b 1
)
set "%~1=%SPVAL%"
:shortpathok
set "SPVAL="
set "SPFIRST="
exit /b 0

:noddk
echo.
echo ERROR: %DDKROOT%\bin\setenv.bat not found.
echo The Windows 2000 DDK is expected inside this repository at tools\ntddk.
echo Nothing is installed machine-wide - unpack it from the archive with:
echo   powershell -ExecutionPolicy Bypass -File scripts\install-w2kddk-cabs.ps1
echo (or set DDKROOT to a DDK installed elsewhere).
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

:stepstestfail
echo.
echo ERROR: the build-step runner's self-tests failed, so it cannot be trusted
echo to run the checks or to report what they found
echo (scripts\build-steps.ps1 -SelfTest).
endlocal
exit /b 1

:stepsfail
echo.
echo ERROR: a check that needs no DDK failed - each failed step's message is
echo above, after the table, and its whole output under its own heading. Its
echo log is out\steps-%ARCH%\^<step^>.log. Nothing was built.
endlocal
exit /b 1

:textfail
echo.
echo ERROR: a text check failed - see its output above. Nothing was built.
echo Each FAIL line names the file and line: a batch file that is not CRLF,
echo a byte the toolchain should not see in tracked source, or a text the
echo release cut publishes with a line past 78 columns, a character that is
echo not printable ASCII, or another of the cut's text refusals. The same
echo checks run alone, in seconds:
echo   powershell -ExecutionPolicy Bypass -File scripts\check-text.ps1
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
