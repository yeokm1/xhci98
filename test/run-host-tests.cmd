@echo off
rem run-host-tests.cmd - build and run the xhci98 host-side unit tests.
rem
rem These run on the Windows build host, not in a VM: they cover the pure
rem hardware-logic core and the ABI declarations in src\, compiled with
rem XHCI_HOST_TEST so they take their types from src\xhci_compat.h instead of
rem ntddk.h (docs\contributing\design\03-host-unit-tests.md).
rem
rem   test_membuf  - the controller common-buffer layout and context strides
rem                  (src\xhci_mem.c)
rem   test_packet  - the usbport miniport ABI (src\xhci_usbport.h) and the
rem                  extension structures usbport allocates for the miniport
rem   test_ring    - TRB encoding, ring wrap/cycle, ring-full detection, and
rem                  the event-ring consumer (src\xhci_ring.c)
rem   test_caps    - the extended-capability walk, port classification, and
rem                  PSI speed decoding (src\xhci_caps.c)
rem   test_port    - PORTSC write construction (src\xhci_port.c)
rem   test_xfer    - the control-transfer engine (src\xhci_xfer.c): Setup/Data/
rem                  Status TD construction, the scatter/gather walk and its
rem                  64 KB splits, the pending-transfer queue, and what a
rem                  Transfer Event means to one usbport transfer
rem   test_iso     - the isochronous engine (src\xhci_xfer.c, task 9-A.1): the
rem                  Isoch TRB's own fields including TBC/TLBPC, the Valid Frame
rem                  Window across both of its wraps, and the per-packet
rem                  completion write-back into usbport's parameter block
rem   test_ctx     - the Slot, Endpoint and Input Control Context encoders
rem                  (src\xhci_ctx.c): the golden vectors for every speed class,
rem                  both context strides, and the field-by-field refusals
rem   test_topo    - the hub topology graph (src\xhci_topo.c): the snooped
rem                  hub-class requests as measured on the wire, the hub
rem                  descriptor and port-status folds, the pending-parent
rem                  claim, Route String nibble arithmetic with its five-tier
rem                  refusal, and subtree/generation pruning
rem   test_desc    - the configuration-descriptor snoop (src\xhci_desc.c, task
rem                  9-A.2): which EP0 setup packets are worth capturing, the
rem                  descriptor walk fed at every chunk size, the isochronous
rem                  bInterval table with its alternate-setting conflicts, and
rem                  the Table 6-12 conversion in src\xhci_ctx.c
rem   test_log     - the optional log ring (src\xhci_log.c, tasks 11-V.7 and
rem                  11-V.9): the record shape byte for byte, the switch gating
rem                  the append itself, the record cap, the wrap and which byte
rem                  it loses, the drain across a wrap and into a short buffer,
rem                  the three flush verdicts, that a failed hand-over still
rem                  empties the ring, and the per-code error budget. (Until
rem                  a later review this entry also claimed 11-V.9's path
rem                  validation, root composition and sink selection; task
rem                  13-L.2 retired the ring-0 file sink and those functions
rem                  with it, and test_log.c had said so for
rem                  two days while this summary had not.)
rem   test_init    - the driver's MMIO-facing code against a synthetic
rem                  controller: the init sequence (src\xhci_init.c), the
rem                  interrupt path (src\xhci_evt.c), the asynchronous command
rem                  engine (src\xhci_cmd.c), the root-hub callback family
rem                  (src\xhci_rh.c), the Phase 6 device layer (src\xhci_slot.c),
rem                  the task 6-V.1 transfer-contract probe (src\xhci_probe.c -
rem                  its classification and gates, since the suite compiles the
rem                  release path where its printing does not exist),
rem                  and the registered callback surface (src\xhci_dispatch.c,
rem                  with DriverEntry excluded under XHCI_HOST_TEST), all through
rem                  src\xhci_pci.c. The one suite here that is not over pure
rem                  code: it redirects the DDK register, stall and spin-lock
rem                  primitives at a model, which is what makes access *order*,
rem                  lock scope, and a refusal writing nothing at all checkable.
rem
rem Uses the same MSVC 6.0 the driver build uses, run in place out of
rem tools\MSVC600 - found relative to this script, so a clone compiles wherever
rem it is unpacked (docs\contributing\build-and-test.md "Automated Phase 1 Host
rem Setup"). Set MSVC6 to override the location. Any C89 compiler works - having
rem no DDK dependency in the core is the point.
rem
rem Exit code 0 = all checks passed. Run this before every VM deploy.

setlocal

set REPO=%~dp0..
if "%MSVC6%"=="" set MSVC6=%REPO%\tools\MSVC600
if not exist "%MSVC6%\VC98\BIN\cl.exe" goto nocompiler

set PATH=%MSVC6%\VC98\BIN;%MSVC6%\Common\MSDev98\Bin;%PATH%
set INCLUDE=%MSVC6%\VC98\INCLUDE
set LIB=%MSVC6%\VC98\LIB

cd /d "%~dp0"

rem Every suite runs even after one fails: the whole point of the host suite is
rem that a second failure costs milliseconds, not another build.
set "SUITEFAILED="
set "SUITEBLOCKED="

call :run test_membuf "test_membuf.c ..\src\xhci_mem.c"
call :run test_packet "test_packet.c"
call :run test_ring "test_ring.c ..\src\xhci_ring.c"
call :run test_caps "test_caps.c ..\src\xhci_caps.c"
rem test_port links xhci_caps.c too, since Phase 5: the root-hub map is derived
rem from the port classification, so XhciRootHubBuild asks XhciPortIsManaged
rem which ports it may present rather than re-deciding it from the class codes.
call :run test_port "test_port.c ..\src\xhci_port.c ..\src\xhci_caps.c"
rem test_xfer links xhci_ring.c: the control-transfer engine's whole job is to
rem produce TRBs and then read completion events back off the ring it wrote
rem them to, so testing it against a stub ring would test neither half.
call :run test_xfer "test_xfer.c ..\src\xhci_xfer.c ..\src\xhci_ring.c"
rem test_iso links the same two files and for the same reason: the isochronous
rem engine builds TRB groups onto a ring and then reads its own controller's
rem events back off it, so a stub ring would test neither half.
call :run test_iso "test_iso.c ..\src\xhci_xfer.c ..\src\xhci_ring.c"
rem test_ctx links xhci_mem.c: the context *encoders* are its subject, but where
rem one context ends and the next begins is the carve's answer, and pairing them
rem is what makes "eight DWORDs whatever the stride" checkable at both strides.
call :run test_ctx "test_ctx.c ..\src\xhci_ctx.c ..\src\xhci_mem.c"
call :run test_topo "test_topo.c ..\src\xhci_topo.c"
rem test_log links nothing else: task 11-V.7's ring is deliberately pure, so
rem every decision it makes - the wrap, the record cap, the flush verdict, the
rem drain's ordering - is drivable with no file system, no registry and no IRQL.
rem The DDK half (the KeGetCurrentIrql guard, the two registry reads and the
rem DbgPrint emission) is exercised through test_init.c's stand-ins instead.
rem It named ZwCreateFile/ZwWriteFile too until task 13-L.2 retired
rem the ring-0 file sink and those imports with it.
call :run test_log "test_log.c ..\src\xhci_log.c"
rem test_desc links xhci_ctx.c: the walk recovers a bInterval and the context
rem encoder is what turns it into an Interval, so the two halves of task 9-A.2
rem are one subject and a vector that stopped at the table would not have tested
rem the number the hardware sees.
call :run test_desc "test_desc.c ..\src\xhci_desc.c ..\src\xhci_ctx.c ..\src\xhci_mem.c"
call :run test_init "test_init.c ..\src\xhci_init.c ..\src\xhci_evt.c ..\src\xhci_cmd.c ..\src\xhci_rh.c ..\src\xhci_slot.c ..\src\xhci_probe.c ..\src\xhci_topo.c ..\src\xhci_desc.c ..\src\xhci_log.c ..\src\xhci_pci.c ..\src\xhci_caps.c ..\src\xhci_mem.c ..\src\xhci_ring.c ..\src\xhci_port.c ..\src\xhci_ctx.c ..\src\xhci_xfer.c ..\src\xhci_dispatch.c"

if defined SUITEFAILED goto testfail
if defined SUITEBLOCKED goto blocked

echo.
echo Host tests PASSED.
endlocal
exit /b 0

rem ------------------------------------------------------------------
rem :run <name> "<source list>" - run one suite and record its verdict.
rem A blocked launch is not a failure, so the two are tracked separately.
rem ------------------------------------------------------------------
:run
call :suite %1 %2
if errorlevel 2 goto runblocked
if errorlevel 1 goto runfailed
goto :eof

:runfailed
set "SUITEFAILED=1"
goto :eof

:runblocked
set "SUITEBLOCKED=1"
goto :eof

rem ------------------------------------------------------------------
rem :suite <name> "<source list>"
rem   0 = passed
rem   1 = build failure, check failure, or a binary that ran and crashed
rem   2 = the launch was blocked before the binary ran (see :blocked)
rem ------------------------------------------------------------------
:suite
setlocal
set NAME=%~1
set SRC=%~2
echo.
echo === %NAME% ===
if exist %NAME%.exe del %NAME%.exe

rem /Za enforces C89 (no // comments, no mid-block declarations) - the same
rem dialect gate the DDK build applies, caught here first.
rem
rem /WX makes a warning a build failure (repo audit C5). This runner exists to
rem catch the DDK dialect gate early, and a warning that sails through here is
rem one the DDK build is left to find - or does not, since the two compilers do
rem not warn about the same things. There is no allowance for "just a warning" in
rem a driver whose C89 conformance is a target constraint.
rem XHCI_HOST_TEST_DEFINES lets a suite be compiled a second time with a
rem candidate's define set, so an #ifdef'd repair can carry its own regression
rem rather than being verified only by a bench plug. It is empty in every
rem ordinary run and is NOT a way to ship behaviour: the DDK build's own
rem XHCI_EXTRA_DEFINES is the one that decides what a binary contains.
cl /nologo /W3 /WX /Za /DXHCI_HOST_TEST %XHCI_HOST_TEST_DEFINES% /Fe%NAME%.exe %SRC%
if errorlevel 1 goto suitebuilderr
if not exist %NAME%.exe goto suitebuilderr

rem Absolute path: this host sets NoDefaultCurrentDirectoryInExePath, so a bare
rem exe name in the working directory is not found.
rem
rem The output is tee'd through a file so a launch that never ran can be told
rem apart from a launch that ran and failed. On a host with Smart App Control
rem enabled, a freshly linked unsigned exe is blocked roughly one launch in
rem three with "blocked by your organization's Device Guard policy" and a
rem nonzero exit - which is indistinguishable from a failed assertion by exit
rem code alone (docs\contributing\lessons.md, "Smart App Control"). Never report that as a test
rem failure; re-run instead.
rem
rem BUT "no result line" is two different things, and calling both of them
rem blocked is how a real defect reads as inconclusive on every run (repo audit
rem F1). A binary that starts and then dies - a wild pointer in a
rem vector, a stack overflow, an assert that aborts - also prints no
rem `checks, N failures` line, and re-running it just reproduces the crash. The
rem discriminator is the reputation block's own message, which the redirection
rem above captures: only that is "run this again". Anything else that ran and
rem produced no verdict is a FAILURE, because a suite that cannot finish is not
rem a suite that passed.
"%~dp0%NAME%.exe" > "%TEMP%\xhci98-%NAME%.out" 2>&1
set RC=%ERRORLEVEL%
type "%TEMP%\xhci98-%NAME%.out"
findstr /c:"checks, " "%TEMP%\xhci98-%NAME%.out" >nul 2>&1
if not errorlevel 1 goto suiteresult
findstr /i /c:"Device Guard" /c:"blocked by" "%TEMP%\xhci98-%NAME%.out" >nul 2>&1
if not errorlevel 1 goto suiteblocked
goto suitecrashed

:suiteresult
rem A suite that ran **zero checks** printed `0 checks, 0 failures` and exited 0,
rem which both tests above accept (repo audit C4). That is the one result no
rem runner may pass: a vector file whose registrations were removed, a `main`
rem that returned before its first call, a suite that built against a stub - all
rem of them look exactly like a green run. A verdict line is evidence only when
rem something was measured.
rem
rem The pattern is anchored on a WORD boundary, not the line start: eleven suites
rem print `N checks, N failures` at column 0 but test_ctx prints
rem `test_ctx: N checks, N failures`, and a `^0 checks` pattern could never
rem match it - the one suite of twelve this gate could not see. `\<0 checks,`
rem still cannot match `20 checks,` (the 0 there follows a word character), and
rem the trailing comma keeps it on the verdict line.
findstr /r /c:"\<0 checks," "%TEMP%\xhci98-%NAME%.out" >nul 2>&1
if not errorlevel 1 goto suitenochecks
if not "%RC%"=="0" goto suitefailed
call :cleanup %NAME%
endlocal
exit /b 0

:suitebuilderr
echo ERROR: could not build %NAME%.
call :cleanup %NAME%
endlocal
exit /b 1

:suitefailed
echo %NAME% FAILED - see the FAIL lines above.
call :cleanup %NAME%
endlocal
exit /b 1

:suitenochecks
echo %NAME% FAILED - it printed a result line but ran 0 checks.
echo A suite that measured nothing has not passed. Look for vectors that are
echo built but never registered, or a main that returned before calling them.
call :cleanup %NAME%
endlocal
exit /b 1

:suitecrashed
echo %NAME% FAILED - it ran and died before printing a result line (exit %RC%).
echo This is a crashing vector, not a launch that was blocked: the output above
echo is the suite's own, up to the point it stopped. Re-running will reproduce
echo it. Run %NAME%.exe under a debugger, or bisect the vectors.
call :cleanup %NAME%
endlocal
exit /b 1

:suiteblocked
echo %NAME% never started - the launch was blocked (exit %RC%).
call :cleanup %NAME%
endlocal
exit /b 2

:blocked
echo.
echo INCONCLUSIVE: a test binary never started. The output above names "Device
echo Guard policy", which is Smart App Control's reputation check on a freshly
echo linked unsigned exe - not a test failure and not a policy rule. Just run
echo this again. (A binary that started and then died is reported as FAILED
echo instead, and re-running that one changes nothing.)
endlocal
exit /b 2

:nocompiler
echo.
echo ERROR: cl.exe not found under %MSVC6%.
echo Run scripts\setup-msvc6.ps1, or set MSVC6 to the extracted MSVC 6.0 root.
endlocal
exit /b 1

:testfail
echo.
echo Host tests FAILED - see the FAIL lines above.
endlocal
exit /b 1

:cleanup
if exist %~1.exe del %~1.exe
if exist "%TEMP%\xhci98-%~1.out" del "%TEMP%\xhci98-%~1.out"
if exist *.obj del *.obj
goto :eof
