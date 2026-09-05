# Repository audit: issues found - 2026-09-05

Audited revision: `6f356a98bc983835696704178ae9cea470390a3c`.
The working tree was clean before the audit, and this document was the only
repository-file addition until the owner had it renamed from `handoff.md` to
`issues-found.md` and committed on 2026-09-05, after the third check below.
Implementation fixes, release recuts, and guest testing belong to the next
session. Line numbers below refer to revision `6f356a9`.

## Review adjustment - 2026-09-05

The owner requested an independent check of this document and then these edits.
The assessments and suggestions below incorporate that check; the original
verification tables remain records of the two earlier passes, not claims that
every check was repeated. The major findings mostly hold, but their evidence
has different strengths:

- F1 and F2 were reproduced by rebuilding the retained host probes against
  the real driver source: 35 checks, the same four failing assertions. F1
  establishes callback-entry behavior, not the reachability of all three
  sequences through a supported usbport stack. F2 establishes permanent
  suppression after modeled delivery loss, not a new guest allocation failure.
- F3 and F9 were reproduced with the actual matrix expectations and verdict
  evaluator: a ring-pool refusal reads `NODRIVER`, and a configure failure
  after an accepted open reads `PASS`. F11's all-excluded tally reads `PASS`.
- F4's guard accepted an older-release descendant and refused a current-cut
  descendant. Only the guard was invoked for those paths; no release was
  written. F14 was reproduced with a scratch INF removing `Xhci.CopyFiles`
  from the Win98 device-install path; the actual gate still passed it.
- F15 was reproduced through `-UploadSetOnly`: an older version is refused
  by the current-INF version check, and a missing package root is refused
  before assembly. Neither invocation built or published anything.
- F5-F7, F10 and F13 have direct source/document support. F5's I/O failure
  effect and F10's performance effect were not measured. F8 is a credible
  synchronization defect and F12 a conditional command-ownership defect;
  their proposed runtime manifestations remain unobserved. F16-F17 are
  low-priority cleanup with the narrower impact described below.

Do not apply the suggested fixes mechanically. In particular, F8 must preserve
an active deferred drainer, F12 needs a command-ring No Op rather than the
existing transfer-ring helper, and D6's AbortTransfer row now identifies the
stale wrapper comment instead of treating it as the implementation. The smaller
items and D5-D6 include maintenance preferences and hypotheses, not a uniform
list of established correctness defects.

This review changed only this document among repository files. The probes and
scratch INF are under ignored `out/audit-20260905/`. No new guest, SMP,
Verifier, or hardware validation was performed, and no checkpoint is advanced.

## Third check - 2026-09-05, evening

A further pass re-read every cited line of this document against the tree,
then ran a Codex review round over the document itself
(`.claude/handoff-review-round1-{prompt,result}.txt`, thread
`01a07196-c11a-7751-9b1a-48875932b8ac`, 27 minutes); all nineteen of its
findings were checked against the tree and accepted. What changed in this
document:

- Citation and naming slips: F5's text-writer names and the `return 0` line;
  F7's heading line (1933) and paragraph lines (2440-2445); D4's
  `xhci_slot.c` and `xhci_log.h` lines; the `test_init.c` async-callback mock
  range and its function name; the `run-host-tests.cmd`,
  `xhci-programming.md`, `xhcisnap.c`, `report.c` and ABI-document lines;
  `build-and-test.md:286`'s actual sentence; the packager self-test count.
- Rows narrowed or corrected: F9's counter-move alternative is not a
  substitute for refusal expectations; F15's rebuild scope is the current
  cut only (the gate run read-only against the 1.0.0.1 INF fails it, six
  rules 1.0.1.0 added); F6's replacement keeps the 1.0.0.1 and 1.0.1.0 dates apart
  and drops the `README.md:232` citation; the `CANCELED` row names redundant
  terms, not unreachable branches; the failed-record reopen row names the
  header it actually contradicts; the RsvdZ-helper row records the existing
  high-DWORD rejection; the xhcisnap `-probe`/`-verbosity` row had its
  direction reversed (the verbosity write wins and the probe is dropped); the
  `xhciDevCancelWork` header row adds the stale future-work claim; the
  xhciqual and design-record-03 rows correct an argument, a function and two
  counts; the NUSB 3.3e and legal-provenance rows lose overclaims.
- Deleted: the `xhci_topo.h:197` row. Both comments share one origin: a hub
  on a root port is a device on a root port (`xhci_topo.c:418-419`, 244).
- The D2 list drops two passages found accurate as history; the tab/BOM
  inventory was completed.
- Added: F18, from the owner's report that the "Windows 2000 never
  idle-suspends this controller" statement is contradicted by their checks.
- Codex round 2 (same thread, over the revised document;
  `.claude/handoff-review-round2-{prompt,result}.txt`) confirmed all nineteen
  applications and returned three findings, all accepted: F15's scope (the
  1.0.0.1 INF fails the current gate, measured here before the report
  arrived); F18's `lessons.md:144-145` site is a dated entry and moved out of
  the active-statement table; D6's `FlushInterrupts` row must not import
  `xhci_evt.c`'s "resume reinitializes through HCRST" rationale, which the
  successful-restore path contradicts.
- Codex round 3 (same thread; `.claude/handoff-review-round3-{prompt,result}.txt`)
  confirmed the three applications and returned three precision items, all
  accepted: this section's closing sentence now records the read-only gate
  run; F4 places the ZIP beside, not inside, the staging directory; the QEMU
  generator row counts three bare-command generators, not two, and no longer
  says every launcher probes at run time.
- Codex round 4 (same thread; `.claude/handoff-review-round4-{prompt,result}.txt`)
  confirmed the three applications, re-read the whole document against the
  tree and AGENTS.md as the implementing engineer would, and returned NONE.
  The loop stopped by convergence, not by instruction.

No tracked file changed, and no build or guest/hardware test ran during this
third check. The current INF gate was run read-only against the 1.0.0.1 INF
and reported the six failures recorded under F15.

## Findings at a glance

P2 means an actionable correctness or reliability concern; P3 means lower-priority
tooling, defensive hardening, or documentation maintenance. Priority does not
establish runtime reachability. Reproduced means reproduced on the Windows host, not in a target
guest. No new Windows 98, Windows 2000, Windows ME, or Windows XP runtime
observation was taken, and no hardware checkpoint is being claimed.

| ID | Priority | Finding | Evidence |
| --- | --- | --- | --- |
| F1 | P2 | Superseded endpoint handles can modify the replacement endpoint | Three failing host-model assertions |
| F2 | P2 | An undelivered recovery callback leaves recovery armed indefinitely | Failing host-model assertion; existing source already acknowledges callback-allocation loss |
| F3 | P2 | Matrix classifies a driver endpoint-resource refusal as `NODRIVER` | Actual verdict evaluator, actual matrix expectations, synthetic counters |
| F4 | P2 | Upload containment check permits writing inside an older release | Read-only invocation of the actual guard |
| F5 | P2 | XHCISNAP reports a text report as written without checking write/close errors | Source inspection; failure injection still needed |
| D1 | P2 | Post-upload repair instructions contradict release immutability | Conflicting current procedures |
| D2 | P3 | Source/media descriptions retain superseded import and packaging statements | Comparison with current source and policy |
| D3 | P3 | Documentation navigation and current-status references lag phases 17-19 | Repository inventory and cross-reference review |

There is no basis here for a critical-severity finding or for declaring a
guest/hardware failure from a host simulation. F1 especially needs the callback
reachability distinction below preserved when choosing its fix.

## F1 - Superseded endpoint handles still reach live binding state

**Locations:** [src/xhci_slot.c](src/xhci_slot.c), replacement bindings at
5068 and 5360; `XhciSlotSetEndpointState` at 5588, especially 5621-5636 and
5705-5725; `XhciSlotSubmitTransfer` at 7374, especially 7441-7547.
The non-default submit helpers at 6984-6998 and 7153-7159 also check that a
binding exists without checking that it belongs to the supplied extension.

**Problem:** Endpoint lookup uses the saved device index/DCI, but most callback
paths do not verify that the current record's `EndpointExtension` equals the
caller-supplied endpoint. Opening a replacement extension updates that pointer
without invalidating every field in the old extension. The EP0 `REMOVE` branch
has an explicit superseded-handle guard at 5668-5697; the corresponding
non-default `REMOVE`, state changes, and submit admission do not.

Using the existing `test/test_init.c` fixture and the real driver functions,
the audit reproduced all three cases:

1. Open an addressed device's interrupt endpoint into extension A, then open
   the same endpoint into extension B. Both opens succeed and B is bound.
   `SetEndpointState(A, REMOVE)` clears the record's pointer to B. It can also
   start teardown of that record's queue. The assertion that B remains bound
   fails.
2. Open replacement EP0 extension B for an addressed device whose original
   extension is A. `SetEndpointState(A, PAUSED)` sets the current device's
   `Ep0Quiesce.Flags & XHCI_EPQ_PAUSED` to `0x400`; expected zero. The old handle
   can pause the replacement.
3. Activate B, then remove A. The EP0 remove guard correctly closes only A.
   Submitting a GET_DESCRIPTOR transfer through that now-closed A nevertheless
   increases the live device's `Ep0Queue.Count` from zero to one. Submit checks
   the device's EP0-open state, not A's open/current-binding state.

**Impact and reachability:** These are verified callback-entry behaviors.
[Issue 4](docs/issues/04-xp-restore-device-ep0-remove.md) already records real
XP overlap between EP0 handles and a late old-handle REMOVE. It does **not**
establish the exact stale PAUSED/submit sequence above, or overlapping
non-default handles. The third probe deliberately tests admission after close;
do not describe it as an observed usbport call sequence. Determine which
sequences the supported stacks can deliver before claiming field impact.
The existing fixture's EP0 superseded-REMOVE tests around 14254-14414 cover
only part of the binding-identity problem.

**Suggested fix:** Centralize current-binding validation under the existing
controller lock. Resolve the record and compare its endpoint pointer with the
incoming extension before changing state or accepting new work. Review
`Get/SetEndpointStatus` and all endpoint callback entry points under the same
rule. Close a stale handle locally without clearing or draining a replacement;
use the established miniport completion/status contract to reject stale submits.
Check whether device-record reuse also requires a tenancy/generation check;
that case was not reproduced in this audit.

Do not indiscriminately reject every old-handle operation: an abort or
completion may still have to withdraw work legitimately owned by that handle.
Preserve same-extension ReopenPipe, controller-owned EP0 memory, and issue 4's
working XP behavior.

**Validation:** Add the three fixture sequences as regression tests, including
successful use of B after each stale operation. Cover same-extension reopen,
queued old transfers, device-index reuse, and non-default endpoints. After the
fix, run both primary target VMs and the XP restore/lifecycle sequence. A
Windows 98 hardware reading does not cover Windows 2000; its validation remains
VM-only in this project.

## F2 - Lost callback delivery permanently suppresses recovery

**Locations:** [src/xhci_dispatch.c](src/xhci_dispatch.c),
`xhciRecoveryCallback` at 2841 and `xhciArmRecovery` at 2942;
[src/xhci_init.c](src/xhci_init.c), recovery-attempt increments at 3739/3748;
[src/xhci_cmd.c](src/xhci_cmd.c), failed-controller early return at 529-532.

**Problem:** Arming clears `RecoveryRequested` and sets `RecoveryArmed` before
calling `UsbPortRequestAsyncCallback`. If the callback never arrives, nothing
in repeated `CheckController` calls clears the armed latch or retries delivery.
The service's indistinguishable success/allocation-failure return is already
recorded in the comment at 2994-3001; this audit did not derive a new fact from
a third-party binary.

That comment also says loss "costs one attempt" and the attempt cap bounds it.
The code contradicts that explanation: attempts are counted only when recovery
executes, and execution requires callback delivery.

**Reproduction:** Initialize the existing host fixture, call `ResetController`
and `CheckController`, confirm `RecoveryArmed == 1`, then discard the mock's
pending async callback to model the documented silent allocation failure.
Call `CheckController` another 100 times without delivering it. Observed:

```text
failed=1 armed=1 requested=0 attempts=0 new_requests=0
```

The assertion that recovery either retries or recovers fails. The source's
arming predicate makes this stable beyond the 100-call test. This is a known
residual with an inaccurate bounding explanation, not evidence of a newly
observed guest allocation failure.

**Suggested fix:** Decide and document an explicit delivery-failure policy.
Keeping silent delivery loss as a terminal residual is an option; in that case,
correct the claim that it consumes an attempt or is bounded by the attempt cap.
If automatic retry is required, implement it with explicit delivery ownership.
A bounded health-poll mechanism could age an armed request, account for a lost
delivery, and retry with a distinct delivery generation. It must invalidate
late callbacks and prevent simultaneous recoveries. Do not simply clear the
latch and resubmit with an identical token. Also do not base the timeout on
`PollClockMs` without changing its advancement: the normal health-poll path
returns early while `ControllerFailed` is set. Maintain the existing usbport
lock ordering and API ceiling.

**Validation:** Exercise one lost delivery, eventual successful delivery, a
late callback from an expired request, repeated loss reaching a bounded terminal
state, and suspend/restart between arming and delivery. Update the source and
recovery documentation to state the actual residual if automatic retry is
deliberately left unsupported.

## F3 - Resource refusal is misreported as a missing function driver

**Locations:** [scripts/vm-matrix/lib/verdict.ps1](scripts/vm-matrix/lib/verdict.ps1),
`Get-RowOutcome`, 218-252;
[scripts/vm-matrix/matrix.psd1](scripts/vm-matrix/matrix.psd1), `Always` at
58-84 and `usb-mouse/hs`;
[scripts/vm-matrix/lib/fresh.ps1](scripts/vm-matrix/lib/fresh.ps1), expected
`NODRIVER` handling at 255-267.

**Problem:** The evaluator infers `NODRIVER` from an addressed device with zero
successfully opened non-default endpoints, provided the failed expectations
look like missing class-driver activity. It does not distinguish "no open was
attempted" from "our driver refused the attempted open." The global accounting
identity includes refusal counters, so it can hold even when that refusal is
the reason enumeration never reaches function traffic.

**Reproduction:** Load the actual counter table and actual `usb-mouse/hs` row.
Create a readable, non-restarted delta with all counters zero except:

| Counter label | Delta |
| --- | ---: |
| devices addressed | 1 |
| slots enabled | 1 |
| endpoint opens seen | 2 |
| endpoint opens accepted | 1 |
| endpoint refusals - ring pool | 1 |

The accepted open represents EP0; `endpoints opened` remains zero. Evaluate
both `matrix.Always` and the row's `Expect`, not just a hand-selected subset.
The sole failed expectation is `advance endpoints opened >= 1`. Actual result:

```text
Outcome : NODRIVER
Why     : addressed (+1) but no function driver opened a non-default endpoint
```

This is an internally accounted-for driver ring-pool refusal. A function
driver tried to open an endpoint. On a target/row with `ExpectNoDriver`, the
post-release evaluator can accept this outcome as expected and conceal the
driver-side failure. The synthetic reproduction does not invalidate old
recorded matrix runs; their actual refusal counters would need examination.

**Suggested fix:** Make refusal evidence take precedence over a missing-driver
inference, or add explicit applicable refusal expectations to the matrix.
Preserve the distinction between tolerated transient enumeration retries and
a non-default open refused by this driver. If aggregate counters cannot prove
which kind of open failed, classify the evidence conservatively rather than
claiming the OS lacks a driver. Include the refusal reason in the verdict.

**Validation:** Add the exact full-matrix vector above to `selftest.ps1`, plus
parameter/no-device refusal variants, and verify that `ExpectNoDriver` cannot
waive them. Retain a true addressed-but-never-claimed `NODRIVER` test and the
existing single-failure/unread-counter tests.

## F4 - Upload output can be placed inside another immutable release

**Locations:** [scripts/package/make-release.ps1](scripts/package/make-release.ps1),
`Assert-UploadSetOutsideRelease` at 638-675; upload paths and removal at
755-771; early validation at 1274.

**Problem:** The guard compares the upload tree only with the release currently
being assembled. It rejects their ancestor/descendant overlap, but accepts an
upload destination under a different version in `releases/`. `New-UploadSet`
then deletes and recreates the `upload-<version>` staging directory under
`UploadDir` (755, 769, 788) and writes the sibling `xhci98-<version>.zip`
directly under `UploadDir` (764). Both destinations can therefore modify an
older cut even though the diagnostic says outputs must be outside
`releases/`.

**Read-only reproduction:** Extract just the guard function from the script
and invoke it with normalized absolute paths; do not run a release assembly
against these destinations. With `PublishedRoot` set to
`<repo>\releases\1.0.1.0`:

| UploadRoot | Actual guard result |
| --- | --- |
| `<repo>\releases\1.0.1.0\upload-1.0.1.0` | Refused |
| `<repo>\releases\1.0.0.1\upload-1.0.1.0` | Accepted |

Thus `-UploadDir releases\1.0.0.1` has a hole in the preflight guard when
assembling 1.0.1.0. The audit invoked only the guard and did not write to a
release directory.

**Suggested fix:** Validate upload directory/tree and ZIP destinations against
the entire protected release root, not solely the current version. Account
for configured release/output roots as well as the repository's tracked
release tree. Retain the ancestor protection, and perform all checks before
builds, publication, deletion, or output creation. This finding does not depend
on junctions, aliases, or unresolved path normalization.

**Validation:** Test current-version descendants, older-version descendants,
release-root destinations, containing ancestors, the ZIP path, and a normal
`out/` destination using isolated temporary trees. Assert rejected arguments
leave all cut contents unchanged. Never use the real release trees for a
destructive regression test.

## F5 - Text-report output errors are not checked

**Locations:** [xhcisnap/xhcisnap.c](xhcisnap/xhcisnap.c), text writers
`put_wrapped_to` (`fprintf` at 188 and 195; `comp_wrapped` at 732-743 reaches it) and `comp` (`vfprintf` at 774), and finalization at
2838-2841, 2874-2905.

**Problem:** `companionWasWritten` records only whether `fopen` returned a
stream. Text output ignores `fprintf`/`vfprintf` results, and finalization
ignores `fclose`. If writing or flushing the report fails after a successful
open, the summary still tells the user to send that TXT file and exits zero.
Buffered output makes close-time failure particularly relevant: successful
open does not establish a complete report.

**Impact:** A full/removable/failing destination can leave a truncated report
reported as successfully written. This is established by source inspection;
no target disk-full or injected CRT failure was executed. The raw binary
writer already checks its `WriteFile` result and byte count, so its success
does not imply text-report success.

**Suggested fix:** Track text-stream errors and check both `ferror` and
`fclose` before claiming the report was written. Preserve an earlier error even
if close succeeds. Return a failure result and print a specific incomplete-
report message when output fails, while retaining any useful raw dump. Keep
the existing target CRT/API constraints.

**Validation:** Inject a text write failure and a close/flush-only failure;
verify nonzero exit and no successful-TXT summary. Also cover successful output
and the existing open-failure behavior. A full-disk manual check is optional
after deterministic failure injection, not a prerequisite to reproducing the
control-flow defect.

## D1 - Post-upload repair policy contradicts immutable releases

**Locations:** [docs/contributing/roadmap.md](docs/contributing/roadmap.md),
1526-1533; [releases/README.md](releases/README.md), 19-30.

The roadmap's hand-run acceptance procedure explicitly starts after upload,
then instructs fixing a driver defect by recutting the existing release
without a new version number. The release policy explicitly forbids this once
a version has been uploaded; same-number recuts are permitted only before the
first upload when nobody holds that version.

**Suggested fix:** Make the roadmap defer to the release policy: a post-upload
driver fix gets a new version and history entry. Keep the pre-upload exception
separate. Update release-notes claims when a limitation is found, but do not
silently replace bytes under an already distributed version. No actual release
directory needs editing to fix this documentation conflict.

## D2 - Import, INF, and media descriptions need updating

These statements describe current behavior but retain older implementation
assumptions. Update prose/comments, not the correct existing import/INF rules.

| Location | Inconsistency | Suggested correction |
| --- | --- | --- |
| [docs/contributing/source-files.md](docs/contributing/source-files.md), 37 | The `xhci_dbg.c` row's statement that neither published binary carries its `DbgPrint` import is misleading as a shipping-import description. Both shipping flavours have the intentional `XhciLogDebugView` sink. | Distinguish the QEMU per-line trace from the shipping PASSIVE-level sink; identify `WRITE_PORT_UCHAR` as the QEMU-only import. Match AGENTS.md and the release guide. |
| [docs/contributing/source-files.md](docs/contributing/source-files.md), 62; [src/xhci98.inf](src/xhci98.inf), 5-7 | The two OS install paths are described as pointing at one CopyFiles section. Current install sections use shared `Xhci.CopyFiles` plus different `Xhci.CopyW98` / `Xhci.CopyNT` OS-source sections. | Describe the shared project file list and the target-specific OS file lists. The INF's actual directives at 127 and 275 already show the split. |
| [.gitignore](.gitignore), 85-97 | Comments say generated media deliberately contains Microsoft drivers from `usbd-sources.expected`, which no longer exists. That distribution decision was withdrawn. | Explain that `out/` holds generated media/assets; the OS now supplies those files through the INF. Preserve the ignore rule. |
| [scripts/package/make-release.ps1](scripts/package/make-release.ps1), 673 and the unexpected-file diagnostic around 711-718 | Diagnostics still describe Microsoft files living in the output and third-party files being admitted by adding INF declarations. This conflicts with the current no-Microsoft-files-on-media policy and gate. | Describe unexpected files and current OS-source behavior without suggesting that adding an INF entry authorizes bundling a Microsoft file. |

Use [legal-provenance.md](docs/contributing/legal-provenance.md) section 5 for
the recorded historical change. Do not rewrite old release artifacts, erase
historical decisions, or turn these corrections into legal conclusions.

## D3 - Current references and navigation lag the repository

| Location | Finding | Suggested correction |
| --- | --- | --- |
| [docs/contributing/roadmap.md](docs/contributing/roadmap.md), 1508 | The future upload/acceptance instruction names `xhci98-1.0.0.1.zip`, but the newest cut is 1.0.1.0. | Refer to the newest release/history entry or update the active instruction. Leave historical phase-17 references to 1.0.0.1 intact. |
| [releases/README.md](releases/README.md), 9-12 | Says the tree holds one version directory and history one entry; the tree now has 1.0.0.0, 1.0.0.1, and 1.0.1.0. | Describe removal of the old 0.x cuts as a historical event without asserting a permanent count of current releases. |
| [docs/README.md](docs/README.md), 68-70 | Says there are three issue narratives; issue 4 documents XP restore-device EP0 removal. | Include issue 4, preferably without a count that must be updated separately. |
| [docs/README.md](docs/README.md), phase-reading table at 86-104 | The phase map ends at 11-16 despite the roadmap and evidence now including completed phases 17-19. AGENTS.md directs readers to this map. | Add the OS-supplied-media, ME, XP, and phase-19 post-release reading paths, matching the roadmap and build/test guide. Preserve the ME/XP no-checkpoint-tax qualification. |

Local Markdown destination and heading-anchor checks found no unresolved local
links. These are content/navigation omissions, not broken-link findings.

## Verification performed

| Check | Result |
| --- | --- |
| `scripts\build-driver.cmd all` | Passed: debug, release, and qemu builds and their import/flavour gates |
| Host driver/core suites run by that build | 19,937 checks across 12 suites; zero failures |
| Evidence manifest self-tests | 15 checks passed |
| INF gate self-tests and current INF gate | 310 self-test checks passed; current INF passed |
| Packager self-tests | 153 checks passed |
| QEMU launcher self-tests | 116 checks passed |
| `xhciqual\test\run-host-tests.cmd` | 146 checks; zero failures |
| `scripts\vm-matrix\selftest.ps1` | 196 checks passed |
| Parser scan of tracked `.ps1`/`.psd1` files | No syntax errors with the host PowerShell parser |
| Local Markdown destinations and heading anchors | No unresolved references found |
| Added audit-only driver probes | 35 checks, four intentional failing assertions: three for F1, one for F2 |
| Actual matrix verdict evaluator with F3 counters | Incorrect `NODRIVER` reproduced |
| Read-only release containment guard probe | Older-release descendant incorrectly accepted |

The first build attempt failed before compilation because a child Windows
PowerShell process inherited the host PowerShell 7 module search path and
could not resolve `Get-FileHash`. For the successful rerun only, the process
environment was set to:

```powershell
$env:PSModulePath = "$env:WINDIR\System32\WindowsPowerShell\v1.0\Modules"
scripts\build-driver.cmd all
```

No script or machine-wide setting was changed to address that environment
issue. The matrix self-test's refused local monitor connection was its expected
negative test, followed by its all-passed result.

### Reproduction material on this host

Ignored scratch material remains under `out/audit-20260905/`:

- `build-all.log`, `qualifier-tests.log`, `matrix-selftest.log`.
- `make-probes.py`, `audit_init.c`, `run-probes.cmd`, and `probes.log` for F1/F2.
  The generator copies the existing `test_init.c` fixture, replaces only its
  main test selection, and compiles against the real source list from
  `test/run-host-tests.cmd` with MSVC 6.0 and `XHCI_HOST_TEST`.
- `probe-verdict.ps1` and `probe-verdict.log` for F3.
- `inventory.py`, `inventory.json`, and `anchors.py` for review inventory and
  local-link checks. Numbered comment-stripped source copies aided review;
  findings were checked against original files and comments.

These are optional local receipts, not inputs a fresh clone is expected to
have. The F1 fixture sequence can be recreated with
`slot_enumerate_addressed(3, 3, 5, 7)`,
`slot_open_ep(..., 7, UsbHighSpeed, 0x81,
USBPORT_TRANSFER_TYPE_INTERRUPT, 8, 8, 1)`, and `deliver_events()` between
the first open and its replacement. EP0 uses `slot_properties(7, UsbHighSpeed,
64)` and `open_endpoint_now`; the stale submit uses
`slot_setup(0x80, 0x06, 0x0100, 0)`. Use the fixture's original, second, and
restore endpoint extensions as distinct handles.

For F3, dot-source `lib/counters.ps1` and `lib/verdict.ps1`, use
`Import-CounterTable` and `Import-PowerShellDataFile matrix.psd1`, resolve the
table labels with `Resolve-CounterLabel`, and build a delta with `Restarted =
$false` and `WentBackwards = @()`. Pass every `Always` and row expectation
through `ConvertTo-Expectation` and `Test-Expectation` before calling
`Get-RowOutcome`. The counter table above contains all nonzero inputs.

## Scope and next-session order

The audit inventoried the tracked tree (296 files, including 52 Markdown files
before this document), reviewed driver control/data paths and their documented
contracts, inspected host/build/import/INF/package/matrix tooling and both
diagnostic tools, and cross-checked current documentation against source,
release history, and existing evidence. Detailed attention went to endpoint
ownership, transfers/completions, command/recovery callbacks, controller
lifecycle, root-hub/topology behavior, report output, release containment, and
test-verdict correctness. Passing tests or finding no issue in a reviewed area
is not a proof that every path is correct.

No guest was booted, no SMP/Verifier or physical-controller run was performed,
no release was assembled/uploaded, and no third-party binary was patched or
new ABI contract inferred. Host mocks cannot establish real interrupt/DMA
ordering, timing, or usbport callback reachability. Existing unobserved
hardware cases and recorded platform limitations remain as documented; this
audit does not close them or reinterpret Windows 2000 installation failures.

Original next-session order (superseded by the revised order at the end):

1. Reproduce F1 and settle the supported callback contracts; add targeted
   regression coverage before changing endpoint lifetime handling.
2. Define F2's retry/terminal policy and test stale-delivery behavior before
   changing recovery scheduling.
3. Fix F3 and F4 with isolated regression tests, then F5 with deterministic I/O
   failure injection.
4. Correct D1-D3 in active source documents/generators, retaining historical
   release artifacts and evidence as recorded.
5. Run the full existing gates after implementation changes, then relevant
   primary-target VM tests and XP restore tests. Schedule any needed Windows 98
   hardware observation separately. Report exactly which environment was used.

Do not recut or advance a checkpoint merely because the host tests pass.

---

# Second pass - 2026-09-05, later session

Same audited revision, `6f356a98bc983835696704178ae9cea470390a3c`; the working
tree was clean apart from this file, and this file is still the only
addition. Nothing else was written into the repository. Line numbers refer to
that revision. This pass re-verified every first-pass finding against the
source and then swept the whole tracked tree again by area (driver core,
rings/commands/events, init/dispatch/root hub, INF and gates, packaging and
releases, the VM matrix, the two diagnostic tools, the host-test harness, and
the documentation tree against the code it describes). Read the refinements
below before fixing anything from the first pass's order: three of the five F
items have a wider shape than first recorded.

## Verification of the first pass

| ID | Verdict | How it was checked this pass |
| --- | --- | --- |
| F1 | Legitimate, P2, wider than recorded | Read `xhci_slot.c` at every cited line. The identity gap also covers `XhciSlotSetEndpointStatus(RUN)` (5873-5875) and `XhciSlotGetEndpointStatus` (5828-5831), and record-index reuse after `xhciDevRelease` (3530-3560) makes a stale non-default handle resolve to the *next* device without any two-handle restore. Not observed on a primary target; issue 4's run had only EP0 open on the restored device. |
| F2 | Confirmed modeled delivery-loss behavior, P2; retry policy remains a decision | The explicit clearing assignment is `RecoveryArmed = 0` in `xhci_dispatch.c:2866`, inside the callback. `RecoveryAttempts`/`RecoveryFailuresConsecutive` move only inside `XhciRecoverController` (`xhci_init.c:3739-3748`, 3803). After one lost arm the state is `Armed=1, Requested=0` with no counter moving. A health-poll-based age-out could count polls with appropriate synchronization; `PollClockMs` cannot supply it unchanged because `XhciControllerHealthPoll` returns before `XhciPollClockAdvance` while failed (`xhci_cmd.c:529-551`). |
| F3 | Legitimate, P2, reproduced again | Dot-sourced the real libraries and fed `Get-RowOutcome` the stated deltas: `NODRIVER`. Same for the other four non-default refusal counters. All five are incremented only inside `xhciSlotOpenNonDefault` / the non-default branch of `XhciSlotOpenEndpoint` (`xhci_slot.c:5214-5569`), i.e. only after a function driver asked for a pipe. See F9 for the adjacent gap the reproduction surfaced. |
| F4 | Legitimate, P2, reproduced | Extracted guard invoked with fake paths: `releases\1.0.0.1\upload-1.0.1.0`, `releases\upload-1.0.1.0` and `<repo>\upload-1.0.1.0` all accepted; only paths at, under or around `releases\1.0.1.0` refused. `.gitignore` ignores only `releases/.staging-*` and `.replaced-*`, so the stray output would be committed by `git add -A`. `make-package.ps1`'s own containment fix (205-221) is present and tested. |
| F5 | Legitimate, P2 | `put_wrapped_to` 188/195, `comp` 774, `fopen` 2733, `companionWasWritten` 2838, `fclose` 2840 unchecked; summary 2874-2904 and `return 0` at 2905. `-o` onto a filling FAT floppy is the concrete case. |
| D1 | Legitimate | Roadmap 1526-1531 is explicitly post-upload and says "does not open a new version number"; `releases/README.md:19-30` says the uploaded rule is absolute. |
| D2 | Legitimate, more sites | All four rows confirmed. Further stale passages of the same kind: `make-release.ps1:6-9`, 823-828, 914 (`-SourceManifest`, a `check-inf.ps1` switch that no longer exists), 1382-1384; `make-package.ps1:205-220`; `src/xhci98.inf:6-7` (the INF's own header makes the "one CopyFiles section" claim); `docs/contributing/build-and-test.md:4211-4235` (see D5); `docs/contributing/test-equipment.md:58`. Two passages checked and found accurate as history: `package-common.ps1:6-11` records the removed manifest and needs no correction; `make-release.ps1:793-800` describes a historical Microsoft-file assembly defect in present-tense wording, so separate the record of that defect from the current directory-discovery rule rather than deleting it. |
| D3 | Legitimate | All four rows confirmed against the current text. `build-and-test.md:286` still says `releases\history.md` "holds one entry". |

The first pass's `PSModulePath` build failure did not reproduce on this host:
`pwsh` is not on this session's path and PowerShell 7's module directory is
not present, so the child `powershell.exe` resolved `Get-FileHash` normally.
The scripts carry no guard (the hardening row is under "Smaller code and
script items"). Do not treat it as a repository defect until it is reproduced
from a PowerShell 7 parent.

## New findings at a glance

| ID | Priority | Finding | Evidence |
| --- | --- | --- | --- |
| F6 | P2 | The root `LICENSE` scope note says the download contains three Microsoft files | Current text; identical copy shipped in `releases/1.0.1.0/LICENSE` |
| F7 | P2 | The generated `readme.txt` makes claims false since 1.0.1.0, and one AGENTS.md forbids | Generator template vs the shipped 1.0.1.0 readme, which contradicts itself |
| F8 | P2 | `XhciSlotInit` zeroes the device table and `DeferredBusy` outside the lock while slot callbacks can access that state | Credible SMP recovery race from source and documented lock contracts; corruption not reproduced |
| F9 | P2 | A Configure Endpoint failure after an accepted open is invisible to the matrix verdict; the row reads `PASS` | `EndpointsOpened++` at open time; no expectation names the configure-failure counters |
| F10 | P2 | After a successful Save/Restore resume IMOD is 0, contradicting the isochronous builder's moderation assumption | Source-confirmed policy inconsistency; interrupt-rate impact unmeasured; QEMU falls back to reinitialization |
| F11 | P3 | A target whose every row was `EXCLUDED` reads `PASS` | Reproduced with the real `Get-TargetVerdict` |
| F12 | P3 | Command Ring Stopped may adopt the abandoned command's own TRB, which the next doorbell re-executes | State-machine reading; hardware trigger plausible, not observed |
| F13 | P3 | The BIOS-handoff write to USBLEGCTLSTS clears RsvdP bits 3:1 and 12:5 | Mask `0x0000FFFF` vs spec Table 7-5 |
| F14 | P3 | INF gate: the Win98 path's own `CopyFiles` is not checked to deliver `NTMPDriver`'s file | Actual gate accepts the scratch mutation; the shipping INF is correct |
| F15 | P3 | `make-release.ps1 -UploadSetOnly` requires current-version sources and matching local packages | Older-version and missing-package refusals reproduced; workflow limitation, not forced release replacement |
| F16 | P3 | `xhciqual` EHCI `legacy_cleanup` writes RW1C SMI status bits back verbatim | Source reading |
| F17 | P3 | `XHCISNAP` prints "Do not decode this dump" yet publishes, says "send", exits 0 | Source reading |
| F18 | P2 | "Windows 2000's native `usbport` never idle-suspends this controller" is asserted in the INF, the readme template, two gates, the driver and six documents; the owner's checks contradict it | Owner report of 2026-09-05; no site cites a measurement, and the only qualified statement in the tree limits it to one observation window |
| D4 | P3 | Stale or drifted comments and IRQL tags in the driver source | Comparison with the code beside them |
| D5 | P3 / optional | Documentation drift plus sample/configuration maintenance candidates | Cross-reference review; branch names and example paths are not defects by themselves |
| D6 | P3 / optional | Technical-document drift plus precision improvements | Source review; the AbortTransfer correction was revised after tracing the implementation |

Nothing found this pass is a critical-severity defect. No guest, VM,
hardware, SMP or Driver Verifier run was performed. Design record 10 has no
code behind it: `git diff --stat 40a739a..HEAD` touches four documentation
files only, and every statement the record makes about the current tree that
this pass checked (speed classification refusals, `XhciInitialMps0`, the
registry read order in `StartController`, PORTSC change-bit acknowledgement,
the stale `PortsPowered` comment) is accurate.

## F6 - The root LICENSE still describes Microsoft files in the download

**Location:** [LICENSE](LICENSE), the scope paragraph at 427-439; byte-identical
in `releases/1.0.1.0/LICENSE` (a cut directory, never edited; the fix rides
the next cut).

**Problem:** "The release download - as distinct from this repository -
contains three files that are not this project's work ... usbd98.sys and
usbd2k.sys ... and usbhub98.sys ... They are included because ..." That
decision was withdrawn on 2026-09-02 before any upload; since 1.0.0.1 the
media carries no Microsoft file. The paragraph contradicts the current
download policy in `docs/using/release-notes.md:291-298`, `AGENTS.md:357-366`,
`legal-provenance.md:515-525`, `releases/README.md:128-156` and the shipped
`releases/1.0.1.0/readme.txt:232-235`. AGENTS.md ("Third-Party Material")
requires the LICENSE claims to be kept true.

**Suggested fix:** Rewrite the paragraph as history, in the form
`legal-provenance.md` section 5 uses: from 0.0.0.4 to 1.0.0.0 the assembled
asset carried the three files; withdrawn 2026-09-02 before any upload; since
1.0.0.1 the download carries no Microsoft file, the INF having the OS place
`usbd.sys` on both primary paths and `usbhub.sys` on Windows 98 from its own
install source; 1.0.1.0 adds OS-sourced `usbport.sys` and `usbhub.sys` on
the NT path (`releases/history.md:35-42`). Keep the two dates apart, as the
D5 `README.md:146` row asks. Facts only, no verdict. `make-release.ps1` copies this file
into every cut, so the correction reaches the next release automatically.

## F7 - The generated readme.txt carries claims false since 1.0.1.0

**Locations:** [scripts/package/make-release.ps1](scripts/package/make-release.ps1)
readme template; the shipped `releases/1.0.1.0/readme.txt` is byte-identical
to the template apart from placeholders, so each item is in both.

| Template line | Text | Why wrong |
| --- | --- | --- |
| 2093-2098 (section 5) | "WINDOWS 98 ONLY: INSTALLING CHANGES ONE MACHINE-WIDE SETTING. It writes DisableSelectiveSuspend = 1" | Since 1.0.1.0 the NT path writes it too (`src/xhci98.inf:144`, `history.md` 1.0.1.0 entry). The same readme says so at 2440-2445, so the shipped file contradicts itself. |
| 2314-2315 (section 9) | "one the Windows 98 installer writes machine-wide" | Same. |
| 2440-2445 (section 9) | "Until 1.0.0.1 the Windows 2000 install withheld it, because that system's USB stack never idles this controller ... On Windows 2000 it still changes nothing you can see" | The NT path withheld the value until 1.0.1.0, not 1.0.0.1 (`src/xhci98.inf:190`, the 1.0.1.0 `history.md` entry). The never-idles claim is F18's. |
| 1942-1943 (section 3) | "this download redistributes nothing of Microsoft's" | AGENTS.md forbids exactly this sentence: `XHCISNAP.EXE` statically links the MSVC 6.0 runtime, which the release's own `xhcisnap/NOTICE.TXT` attributes to Microsoft. The defensible form is the LICENCE section's "No Microsoft *file* is in this download" (2461). `docs/using/release-notes.md:295` ("nothing in the download is Microsoft's") has the same defect. |
| 1768 vs 1933 | TOC says "3. Check the media is complete"; the heading is "3. THE FILES WINDOWS SUPPLIES" | Stale TOC. |
| 2410-2411 | "ON WINDOWS 98 THE SNAPSHOT ROUTE IS THE ONE THAT WORKS, and that is the whole of what changed in this version" | Written for a 0.x release; false for every 1.x. |
| 1752, 2461-2462 | "two operating systems"; "(on Windows 2000) usbport.sys" | Omit ME and XP (nit). |
| 1914, 2011, 2013 | "NUSB 3.3e" | The tree uses both forms: "NUSB 3.3" in AGENTS.md, history.md and the release-notes Requirements, "NUSB 3.3e" at `release-notes.md:120`. Terminology consistency only (nit); the package named is the right one. |

**Suggested fix:** Correct the generator only; do not touch the cut
directories. Fix `release-notes.md:295` in the same change. Consider a packager
self-test that greps the rendered readme for "redistributes nothing" and
"WINDOWS 98 ONLY".

## F8 - XhciSlotInit rewrites the device table outside the lock

**Locations:** [src/xhci_slot.c](src/xhci_slot.c) 3872-3892 (zeroing loop and
the `CommandOwner`/`PumpCursor`/`EndpointInvalidatesOwed`/`DeferredBusy`
writes after `XhciControllerLockRelease` at 3872); caller
[src/xhci_init.c:4489](src/xhci_init.c#L4489) inside `XhciInitController`,
which the in-place recovery (`xhci_init.c:3681-3790`) runs from a DPC with the
controller still live from usbport's point of view.

**Problem:** Design record 05 section 2 and `implementation-invariants.md`
("Locking and Lock Order", last bullet) justify the lockless init sequence by
"every other context tests `INITIALIZED` before touching controller state".
The slot callbacks do not: `XhciSlotSubmitTransfer` (7462-7480),
`XhciSlotSetEndpointState` (5624, 5654), `XhciSlotAbortTransfer` (7773-7826)
and `XhciSlotDeferredWork` (10340-10360) read and write `ext->Devices[]` and
`DeferredBusy` under the controller lock with no admission gate;
`xhciDevAdmitted` is consulted only after the record has been read. During an
in-place recovery on SMP another CPU inside a usbport callback can therefore
read a half-zeroed record, or write into one already zeroed (for example
`xhciEpArmQuiesce` at 2810-2817 setting `FAILED|UNAVAILABLE` on
`Ep0Quiesce.Flags`), leaving a `FREE` record with non-zero quiesce state that
`xhciDevAllocate` (1297) hands to the next device unzeroed. `DeferredBusy = 0`
written bare while another CPU may be inside the drain loop defeats the
single-drainer guard.

**Impact:** A credible race during in-place recovery on SMP. The half-zeroed
record and competing-drainer outcomes are source-derived possibilities, not
observed corruption. Windows 2000 SMP is the primary validation environment;
the common code also warrants considering XP SMP. The particular simultaneous
CPU interleaving does not apply to Windows 98; this is not proof that every
other initialization/lifecycle interleaving is safe.

**Suggested fix:** Serialize the table reset with its callback readers and
writers under the controller lock, and separately establish ownership of the
deferred drain across recovery. Merely moving `DeferredBusy = 0` under the
lock is insufficient: an active drainer deliberately drops that lock around
usbport service calls, so clearing its ownership there can still admit a
second drainer. Preserve the active owner's guard or use an explicit handoff
that also handles its return from the unlocked interval.

Do not simply reject all four callbacks when `INITIALIZED` is clear.
Recovery intentionally calls `XhciSlotDeferredWork` to deliver owed
completions, and aborts may still need to withdraw previously accepted work.
Separate admission of new work from retirement of existing work. Add targeted
interleaving tests before claiming a fix, then validate on Windows 2000 SMP
in a VM. Update design record 05 section 2 and the invariants accordingly.

## F9 - A refused Configure Endpoint still reads PASS in the matrix

**Locations:** [src/xhci_slot.c:5475](src/xhci_slot.c#L5475)
(`EndpointsOpened++` at open time, record `PENDING`); configure failures at
9269-9278, 9997, 10250 (`EndpointConfigureFailures`,
`EndpointsNoBandwidth`, `EndpointsNoResources`);
[scripts/vm-matrix/matrix.psd1](scripts/vm-matrix/matrix.psd1) `Always` at
58-84.

**Problem:** `endpoints opened` advances when usbport's open is accepted, before
the Configure Endpoint command has run. If that command fails, the record ends
`REFUSED`/`FAILED` but the counter has already moved, and no `Always` or row
expectation names the three failure counters. A row whose only bind clause is
`advance endpoints opened >= 1` therefore reads `PASS` for a device the
controller refused to schedule. This is F3 from the other side: F3 lets a
refusal read as the OS's silence, F9 lets a refusal read as success.

**Suggested fix:** Together with F3, give driver-refusal evidence precedence
over both success and the missing-driver inference. Applicable `zero`
expectations for open/configure failures can enforce this in the current
evaluator; a failed `zero` already disqualifies `NODRIVER`, so duplicating
that logic in the NODRIVER branch is not automatically necessary. Review which
transient retries are deliberately tolerated before making all eight counters
unconditionally fatal, and report the actual refusal reason. Add both deltas
to `selftest.ps1` and retain a true never-claimed `NODRIVER` case. Record the
rule in design record 06 section 2.1.
Moving `EndpointsOpened++` to the point the record becomes `CONFIGURED` is an
optional counter-semantics change, not a substitute for the refusal
expectations: without them the same configure failure leaves `endpoints
opened` at zero and the row falls into F3's misleading `NODRIVER` branch
(`verdict.ps1:226-251`). It also changes a counter every recorded run has
read, so the table and the design record would need the note.

## F10 - The restore path programs IMOD to zero and nothing restores it

**Locations:** [src/xhci_init.c:3239](src/xhci_init.c#L3239) (the only
`XHCI_IR_IMOD` write in the driver, inside `xhciRestoreState`), its comment at
3225-3226 ("IMOD 0 is the value this driver programs everywhere");
[src/xhci_xfer.c:2969-2972](src/xhci_xfer.c#L2969-L2972) (isochronous builder:
"The interrupter's own moderation (IMOD, left at its 1 ms default) is what
absorbs that"); `docs/usb-xhci-info/xhci-data-structures.md:357` ("IMOD: leave
default (4000 = 1 ms moderation)").

**Problem:** Three statements disagree. The init path never writes IMOD and so
leaves the reset default, as the document says. The Save/Restore resume path
writes 0, so after any successful restore the interrupter has no moderation,
and the isochronous builder's reason for setting IOC on every TD's last TRB
(an interrupt opportunity per interval, up to 8,000 TDs a second for a
High-Speed audio endpoint with a 125 us interval, moderated by IMOD) no
longer holds. The comment's "everywhere" is false: one
site. The spec's restore step list (transcribed at `xhci-data-structures.md:280`)
requires IMOD to be written, not written as zero.

**Impact:** Successful controller-state restoration removes IMOD's deliberate
interrupt spacing. That can increase interrupt load for an IOC-per-TD audio
stream; an exact interrupt rate or resulting failure was not measured, and
event delivery can still combine work for other reasons.

The documented QEMU 11.0.0 CRS implementation reports a restore error, so the
existing guest path falls back to full reinitialization (`xhci_init.c:3255`
onward). An ordinary QEMU suspend/resume pass therefore does not validate
successful restoration or its IMOD policy. Use a successful-restore host model
for the register check; any runtime validation must first establish that the
restore actually succeeded. Windows 2000 validation remains VM-only.

**Suggested fix:** Restore IMOD to the pre-suspend value (or the reset default
4000) in `xhciRestoreState`, add a `test_init` vector over the restore that
reads it back, and make the three statements agree. If IMOD = 0 is intended,
say so in all three places and revisit the isochronous IOC policy.

## F11 - An all-EXCLUDED target passes

**Locations:** [scripts/vm-matrix/lib/fresh.ps1](scripts/vm-matrix/lib/fresh.ps1)
`Get-TargetVerdict` 418-423; [scripts/vm-matrix/run-matrix.ps1](scripts/vm-matrix/run-matrix.ps1)
921-922.

**Problem:** `EXCLUDED` rows count toward `Rows`, so `Rows=3, NotReached=3,
Against=0` yields `PASS` (reproduced with the real function). It also bypasses
the `$report.Count -eq 0 -> exit 2` guard because excluded rows add report
lines. `-PostRelease -Group <g> -Target 2a-fresh` with every row of the group
excluded on 2a exits 0 with nothing measured.

**Suggested fix:** `if (Rows - NotReached -le 0) { "FAIL" }` or `ERROR`, with a
self-test case.

## F12 - Command Ring Stopped can adopt the abandoned command's own TRB

**Location:** [src/xhci_cmd.c](src/xhci_cmd.c) `xhciCommandRingStopped`
1239-1273.

**Problem:** In `ABORTING` with `CommandTrbPA != 0`, the command is declared
lost and the reported dequeue pointer is adopted through `XhciRingSetDequeue`.
The comment's premise (1171-1180) is that the xHC has advanced past the aborted
TRB, which holds when the command was executing. If the controller never
fetched the doorbelled TRB (wedged fetch with CRR still 1, so `CA` is written
and the stop event does arrive), the reported pointer is the abandoned
command's own TRB; `XhciRingSetDequeue` accepts it (outstanding, TD head) and
the TRB stays valid on the ring. The next `xhciCommandSubmitEx` doorbell can
therefore make the abandoned command executable before the new one (possibly
a Disable Slot or Address Device the slot layer has already been told was
lost). Its completion would no longer match the outstanding command's
identity. The resulting device state and whether the new command completes
normally have not been demonstrated.
The transfer-ring equivalent is handled by the `XhciRingNoOpAt` pass at
`xhci_slot.c:2405`; the command ring has no such rewrite.

**Suggested fix:** Preserve the abandoned command's identity before ending
its outstanding state, and test the case where the stopped pointer still
names it. Treating that position as diverged and requesting reset is one
option. An in-place rewrite needs a command-ring-specific No Op and a review
of cycle publication, ring accounting, and completion handling.

Do not use `XhciRingNoOpAt` unchanged: `xhci_ring.c:1225-1265` emits
`XHCI_TRB_TYPE_NOOP` (type 8, transfer No Op), whereas this ring requires
`XHCI_TRB_TYPE_NOOP_COMMAND` (type 23). The original suggestion confused
those types. Pin the pointer/ownership behavior with a host vector. The
hardware trigger and its downstream effects remain plausible, not observed;
do not assume the subsequent command necessarily completes normally.

## F13 - The BIOS-handoff write clears RsvdP bits of USBLEGCTLSTS

**Locations:** [src/xhci_init.c:621-623](src/xhci_init.c#L621-L623);
`XHCI_USBLEGCTLSTS_SMI_ENABLES = 0x0000FFFF` at `src/xhci.h:1768`.

**Problem:** Spec Table 7-5 makes bits 3:1 and 12:5 of USBLEGCTLSTS RsvdP; the
enables are bits 0, 4, 13, 14, 15 only. The write
`(dw1 & ~0xFFFF) | STATUS` zeroes the RsvdP fields, the class of error the
same file's helper block (255-343) exists to prevent for USBCMD, IMAN, CONFIG,
DNCTRL, ERSTSZ, ERSTBA and CRCR. `xhciqual/qual.h` already has the right
constants (`LEGCTL_RSVDP 0x000E1FEE`, enables `0xE011`).
`docs/usb-xhci-info/xhci-data-structures.md:387` lists the five enable bits but
not the RsvdP fields, which is how the blanket mask survived.

**Suggested fix:** `XHCI_USBLEGCTLSTS_SMI_ENABLES 0x0000E011UL`, add the RsvdP
fields (`3:1`, `12:5`, and `19:17`, which the write already preserves) to the doc row, and a `test_init` vector that the write preserves them.
Practical risk low; no controller has been seen to care.

## F14 - INF gate: the Win98 path is not checked to deliver its own driver file

**Location:** [scripts/inf-gate/check-inf.ps1](scripts/inf-gate/check-inf.ps1)
556-563 (records `NTMPDriver`), 968-971 (global "some CopyFiles section
delivers it"); compare `PATH-NT` at 617-633, which checks `ServiceBinary`
against the `.NTx86` section's *own* `CopyFiles`.

**Problem:** If `[Xhci.Dev]` loses `Xhci.CopyFiles` while `[Xhci.Dev.NTx86]` or
`[DefaultInstall]` keeps it, the gate passes, the Win98 install writes
`NTMPDriver=xhci98.sys` but does not copy that driver binary, and a clean
install would then lack the file its loader value names. This is consistent with
the silent-INF symptom the gate exists to catch. No self-test covers it;
`test-inf-checks.ps1` has the NT-side case only. The review reproduced the
gate pass with `out/audit-20260905/recheck-w98.inf`; the OS-source CopyFiles
list remained present. The current shipping INF still delivers the binary
correctly, and no broken target install was performed.

**Suggested fix:** Mirror the `PATH-NT` check for the undecorated section under
`PATH-W98`, plus a self-test.

## F15 - `-UploadSetOnly` depends on current sources and matching packages

**Location:** [scripts/package/make-release.ps1](scripts/package/make-release.ps1)
1017-1024 (`-Version` must equal the current INF's `DriverVer`), 1050-1085
(`out\pkg-<flavour>\` must hash-match the published binaries), parameter text
150-165.

**Problem:** Rebuilding an older version's asset from the current tree is
refused by the version check, and since 1.0.0.1 `$expected` is empty and
nothing from the `pkg-` directories enters the asset. A fresh clone without
those packages is refused, and a rebuild that changes their binary hashes
also prevents upload-only assembly. These prerequisites are documented, but
retain dependencies from the former media layout that obstruct rebuilding a
lost current asset from its tracked cut alone.

The review invoked `-UploadSetOnly -Version 1.0.0.1` and, separately,
`-UploadSetOnly -PackageRoot out/audit-20260905/nonexistent-packages`, with
an ignored scratch upload destination. Both refused at the expected checks,
before assembly. This is a workflow limitation, not release corruption or a
requirement to use `-Force`: matching package inputs can be restored, and an
older source checkout can satisfy the version check. No recut is authorized
or necessary merely because these invocations fail.

**Suggested fix:** Assemble from the published tree alone (the `pkg-` check is
vestigial) without requiring matching local packages or current-version
sources, so that a lost asset for the current cut can be rebuilt from its
tracked directory alone. The scope is the current cut, not "any existing
`releases\<v>`": the INF gate that `-UploadSetOnly` runs on each assembled
directory encodes the current release's rules, and every older cut fails it.
Checked read-only on 2026-09-05: `check-inf.ps1 -InfPath
releases/1.0.0.1/release/xhci98.inf` reports six failures (`OS-MISSING` for
`usbport.sys` and `usbhub.sys` on both NT routes, `SUSP-MISSING` on both),
all rules 1.0.1.0 added; `releases/1.0.0.0/release/xhci98.inf:101-103` still
names `usbd98.sys`, `usbd2k.sys` and `usbhub98.sys`, which `OS-MEDIA`
(`check-inf.ps1:1049-1052`) and AGENTS.md's no-Microsoft-file rule forbid.
Refuse a `-Version` older than the INF gate's rule set with a message that
says so, rather than weakening the gate or pinning it per version; rebuilding
an older cut's asset is a separate decision for the owner. Keep the INF gate
on the assembled directories and leave cut directories unchanged. Add a
`test-package.ps1` case for "asset rebuilt from a clone with no `out\`".

## F16 - xhciqual EHCI cleanup writes RW1C status bits back

**Location:** [xhciqual/legacy.c:766-769](xhciqual/legacy.c#L766-L769)
(`legacy_cleanup`, EHCI branch).

**Problem:** `legctl_orig` is written back to USBLEGCTLSTS verbatim through PCI
config. Bits 31:29 are RW1C SMI status, so saved 1s become acknowledgements
at cleanup. However, `ehci_handoff` already writes `0xFFFF0000` at line 195:
the original pending status was cleared there. The cleanup cannot be claimed
to newly lose that same original status. It can acknowledge a subsequently
reasserted bit whose saved value was 1; no such event or harm was observed.
The xHCI path masks status out (`bringup.c:992`,
`LEGCTL_RSVDP | LEGCTL_ENABLES`). Treat this as low-priority defensive
cleanup in the DOS qualifier, not a demonstrated xHCI driver failure.

**Suggested fix:** Restore `legctl_orig & 0x0000FFFF`. Also `legacy.c:195`
writes literal `0xFFFF0000`, including RO and reserved fields; no resulting
hardware failure was measured. Review the EHCI register contract before
changing either write.

## F17 - XHCISNAP contradicts itself on an extension-size mismatch

**Location:** [xhcisnap/xhcisnap.c:2686-2691](xhcisnap/xhcisnap.c#L2686-L2691).

**Problem:** On `extBytes != ExtensionBytes` the tool prints "WARNING ... Do
not decode this dump", then publishes the set, prints the "send/attach"
summary and exits 0. The file's own comment says the tail of the screen is all
that survives on a 25-row console, so the warning is the line least likely to
be seen.

**Suggested fix:** Carry the mismatch into the final summary and report the
collection as incomplete/inconsistent, with an appropriate nonzero result.
Preserving and sending the raw evidence can still be useful; publication is
not itself the defect. State which outputs remain usable and avoid claiming
the dump is normally decodable. Fold into the F5 change. No malformed-reply
runtime test was performed.

## F18 - The "Windows 2000 never idle-suspends this controller" assumption is contradicted

**Reported by the owner on 2026-09-05** after the review above: their checks
contradict the statement, so it is an owner finding rather than an audit one,
and the audit did not measure it either way. The assumption is stated
categorically at the sites below, and one conclusion is drawn from it: that
`DisableSelectiveSuspend` "changes nothing" on Windows 2000. One site is
different in kind and is listed for completeness: `xhci_init.c:1665-1667`
says Windows 2000 *may* never idle-suspend, a conditional whose point is that
recovery cannot depend on a future resume being delivered.

**Locations (active statements):**

| Site | Text |
| --- | --- |
| [src/xhci98.inf:190-194](src/xhci98.inf#L190-L194) | "Until release 1.0.1.0 the NT path omitted it, because Windows 2000's native usbport never idle-suspends this controller and the value would have changed nothing there ... on Windows 2000 it still changes nothing (the reading roadmap task 19.8 takes)". Byte-identical in `releases/1.0.1.0/release/xhci98.inf` and `debug/`, cut directories that are never edited; the correction rides the next cut. |
| [scripts/package/make-release.ps1:2440-2445](scripts/package/make-release.ps1#L2440-L2445) | The readme template: "Until 1.0.0.1 the Windows 2000 install withheld it, because that system's USB stack never idles this controller ... On Windows 2000 it still changes nothing you can see". Shipped verbatim in `releases/1.0.1.0/readme.txt`. The version is also wrong: the NT path withheld the value until 1.0.1.0, not 1.0.0.1 (F7 table). |
| [docs/using/release-notes.md:250-251](docs/using/release-notes.md#L250-L251) | "Windows 2000 never does, and the value changes nothing there". |
| [releases/history.md:50-51](releases/history.md#L50-L51) | The 1.0.1.0 entry: "Windows 2000's never idles this controller, so there the value changes nothing you can see". Embedded into every generated `readme.txt`. |
| [docs/using/release-acceptance-test.md:349-352](docs/using/release-acceptance-test.md#L349-L352) | "Windows 2000's native `usbport` never idle-suspends this controller, so on this target the value changes nothing this test can see". |
| [scripts/inf-gate/check-inf.ps1:1158-1163](scripts/inf-gate/check-inf.ps1#L1158-L1163); [scripts/inf-gate/test-inf-checks.ps1:824-827](scripts/inf-gate/test-inf-checks.ps1#L824-L827) | Rule and self-test comments: "Windows 2000's native build never idles this controller and the value changes nothing there"; "that target's native usbport never idle-suspends this controller". The `SUSP-*` rules themselves are unaffected: they require the value on every route. |
| [src/xhci_dispatch.c:1113](src/xhci_dispatch.c#L1113); [src/xhci.h:5073-5074](src/xhci.h#L5073-L5074); [src/xhci_init.c:1665](src/xhci_init.c#L1665) | "native Win2000 usbport never idle-suspended the controller at all" (the `SuspendController` header and the `SuspendCount` field comment); "Win2000 may never idle-suspend, so 'the next resume will re-enable' is not a recovery path". |
| [docs/contributing/implementation-invariants.md:1599-1600](docs/contributing/implementation-invariants.md#L1599-L1600); [docs/contributing/build-and-test.md:988](docs/contributing/build-and-test.md#L988) | "native Win2000 `usbport.sys` never idle-suspended at all"; "On 2b it reads 0, because native usbport never idle-suspends the controller". |

The dated entries at `lessons.md:5040` and `5142` and the run sheets record
what an observation window showed at the time and keep their dates; they are
not in scope. `lessons.md:144-145` ("Windows 2000's native usbport never
idles this controller, so the NT half of the INF had deliberately withheld
`DisableSelectiveSuspend`") is the same kind: it sits inside the dated XP
entry whose environment is recorded at 72-73, so preserve that account and
append a dated qualification rather than rewriting it; apply the same rule to
any other dated lesson, keeping the observation and marking the generalisation
separately. `usbport-miniport-interface.md:534-536` is the one site that
states the qualified form ("did not idle-suspend this controller at all in the
observation window ... not a contract") and is the model for the rewrite.

**Problem:** No site cites a measurement that Windows 2000 SP4's `usbport`
never idles this controller; the statement was generalised from the Phase 3
spike's observation window, which the interface document itself declines to
treat as a contract. The INF's appeal to roadmap task 19.8 does not support
it either: 19.8 is the post-release matrix run whose reading is "report
identical to Phase 16's", a counter-table identity, not an idle observation.
The owner's checks of 2026-09-05 contradict the statement. Whether Windows
2000 idles the controller, and under what conditions, is therefore
unestablished in this repository until a reading is recorded.

**Suggested fix:** Remove or qualify the categorical never-idles claims and
their "changes nothing on Windows 2000" conclusion at every active site in
the table; state only what is recorded: the value has been written on the Windows 98 path since 1.0.0.0
and on the NT path since 1.0.1.0, because Windows 98's `usbport` was measured
idling the controller within about half a second and XP's within about
thirty seconds (2026-09-03), and a halted xHC cannot report a hot-plug. Say
nothing about Windows 2000's idling beyond a dated observation with its
environment, in the qualified form the interface document uses. For
`history.md`, append a dated correction to the 1.0.1.0 entry rather than
rewriting its text, since the entry is embedded in a shipped readme; the
owner decides the form. The driver code needs no behavioural change on this
finding: the suspend/resume pair is target-agnostic. Correct the two
categorical comments (`xhci_dispatch.c:1113`, `xhci.h:5073-5074`); keep the
reasoning at `xhci_init.c:1665-1667`, optionally stated without the
OS-specific example, since occasional suspends would not make a future
resume a recovery path. Fix the
readme paragraph together with F7 and the release-notes sentence together
with D5.

**Validation:** Record a Windows 2000 SP4 VM observation with the
`SuspendController` count and the conditions (nothing attached versus a
device attached, time from start) in `build-and-test.md`, and cite it from
the rewritten sites. Windows 2000 observations remain VM-only in this
project.

## Smaller code and script items

These are review candidates with mixed confidence. Some concern malformed
hardware replies, unsupported interleavings, or currently unused inputs;
others are style/test-maintenance choices. Establish the relevant contract
and reproduce the failure before promoting one to a correctness defect or
changing driver behavior. In particular, a controller setting RsvdZ bits is
not evidence of a failure on conforming hardware, a missing Before field is
not produced by the current reader, and a single-timer mock is not evidence
that an existing vector is wrong when no such vector has been identified.

| Location | Finding | Suggested fix |
| --- | --- | --- |
| [src/xhci_xfer.c](src/xhci_xfer.c) 1875, 1494, 3442 | Transfer Event TRB Pointer RsvdZ bits 3:0 are passed raw to `XhciRingIndexFromPA`, which refuses a non-16-byte-aligned value, so a controller that sets them reads as `ForeignEvents` (1878, 3443) or, on the stopped-transfer lookup at 1494, as a bare failed lookup with no count, and the transfer sits until usbport's timeout; the command path (`xhci_cmd.c:1315-1320`) masks and counts them. The high DWORD is already rejected on both paths (`xhci_slot.c:8161-8167` counts `TransferEventsForeign`, `xhci_cmd.c:1298-1302` counts `CommandsUnmatched`). | One shared low-bits helper for both event families that preserves each path's existing high-DWORD rejection and accounting. |
| `src/xhci_xfer.c` 2063, 2277, 2354 | The `XHCI_XFER_CC_CANCELED` terms in these three compound conditions are redundant: 1870-1873 return on that class first. The containing branches (error override, terminal detection, orphaned-group recovery) are live for their other terms. | Remove or annotate only the `CANCELED` terms; keep the containing logic. |
| `src/xhci_slot.c` 4970-5012 | `xhciSlotOpenControl`'s address-not-0 reopen accepts a `FAILED` record (`xhciDevByAddress` at 169 needs only `ADDRESS_VALID`, which `xhciDevFailRecord` at 3802 does not clear) and, if the legal MPS0 changes, queues `XHCI_DEV_OP_EVALUATE_MPS` on it (4999-5011). This contradicts the admission claim above `xhciDevMayOpenEndpoint` at 395-397 ("while the record is in one of these states no endpoint open can reach it"); `xhciDevFailRecord`'s own header (3772-3775) promises only that the record stops serving transfers. | Test the failed-record reopen path first, then either test `xhciDevMayOpenEndpoint` there or clear `ADDRESS_VALID` on fail after checking the disown paths. |
| [scripts/vm-matrix/lib/counters.ps1](scripts/vm-matrix/lib/counters.ps1) 229-233 | `Get-CounterDelta` treats a field missing from Before as 0, yielding an absolute value rather than `<unread>`. Unreachable today (`Read-Counters` throws on a short chunk) but contradicts the file's "unread is ERROR, never a zero" rule. | Omit the key. |
| `scripts/vm-matrix/run-matrix.ps1` 816-830, 948 | The extension VA/`Spans` check runs once per group; a mid-group driver reload at a new VA (Win2000 disable/enable) reads freed memory. `soak-11v.ps1:182-189` re-checks per read. | Re-run `Find-ExtensionIdentity` in `Invoke-AttachLeg` before each read. |
| `scripts/vm-matrix/run-matrix.ps1` 271-273 | `chardev-add file,...,path=<OutDir>` is not passed through `ConvertTo-HmpArgument`; a path with a space makes `usb-serial/fs` and `usb-braille/fs` `ERROR` on every target. | Quote it as `Save-GuestScreenshot` does; refuse a `,` in the path. |
| `scripts/vm-matrix/run-matrix.ps1` 502 | `Start-Sleep -Seconds $Row.Settle` with no `Settle` key throws and ends the group; load-time validation does not check the key. | Validate at load. |
| [scripts/package/make-11v-media.ps1:115](scripts/package/make-11v-media.ps1#L115) | `-BaselineVersion` defaults to `"0.0.0.6"`; `build-and-test.md:291` says it defaults to the version cut before the current one. The ordering check passes silently against a package that no longer exists. | Default to the previous `releases\` entry, or require the switch. |
| [scripts/build-driver.cmd](scripts/build-driver.cmd) 171-206 | The self-test block runs the evidence-manifest, flavour-rule, INF-gate, packager and launcher self-tests and then the host suite; `vm-matrix/selftest.ps1` (the verdict suite) and `xhciqual/test/check-bat-eol.ps1` are not wired in. | Add both to the self-test block. |
| `scripts/build-driver.cmd` 171-206 | No guard against a PowerShell 7 parent's `PSModulePath` reaching the child `powershell.exe` (the first pass's build failure; not reproduced here). | `set PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules;%ProgramFiles%\WindowsPowerShell\Modules` after `setlocal`, or replace `Get-FileHash` in `evidence-common.ps1` with `SHA256.ComputeHash`. |
| [scripts/import-gate/check-imports.ps1](scripts/import-gate/check-imports.ps1) 502-507, 529 | The allowlist takes the first row matching module+symbol, so two rows for the same pair (`debug` and `qemu`) can never both apply; `FLAVORS` cannot express "two of three". Latent; no row needs it today. | Match on flavour too, or accept a comma list. |
| [xhciqual/devid.c](xhciqual/devid.c) 121, 337 | `*got = wlen - TRB_GET_RESID(e.st)` is unclamped; a residual larger than the TRB length underflows `got` and the config-descriptor walk reads past `DESC_BUF` (256). Informational tier only. | Clamp `resid` to `wlen`. |
| [xhciqual/report.c](xhciqual/report.c) 463-474 vs 554-556 | `--irq-selftest` turns a C3 WARN (No-Op completed with a non-success code) into a self-test FAILURE; the full run treats the same observation as warned-only. | Make the two branches agree. |
| [xhcisnap/xhcisnap.c](xhcisnap/xhcisnap.c) 2302, 2790, 2794 | `print_portsc` is passed the unclamped `portscBytes / 4` over the 1020-byte `portsc_values`; driver-gated (8-bit `MaxPorts`) but the tool elsewhere refuses to trust the reply. | Clamp to `sizeof(portsc_values) / 4`. |
| `xhcisnap/xhcisnap.c` 909 | `h->RingOffset + h->RingBytes > extBytes` can wrap in 32 bits. | `RingOffset > extBytes \|\| RingBytes > extBytes - RingOffset`. |
| [test/test_init.c](test/test_init.c) 1999-2050 | The mock `UsbPortRequestAsyncCallback` (`hc_async_callback`) holds one outstanding timer; usbport holds N. A vector that arms the command watchdog, then a port timer, then fires "the" timer fires the wrong callback and passes for the wrong reason. No current vector does this. | Keep a small pending array; pop by callback identity. |
| [test/test_xfer.c](test/test_xfer.c) 384-410 | `test_build_no_data` pins a SET_ADDRESS TD (`bRequest 0x05`) as a valid ring shape. The driver never puts one on a ring (`test_init.c:13083` asserts it), so AGENTS.md is not violated, but the suite encodes the forbidden request as a correct TD. | Use SET_CONFIGURATION (`bRequest 9`) for the no-data vector. |

## D4 - Stale comments and drifted IRQL tags in the driver source

| Location | Finding | Correction |
| --- | --- | --- |
| [src/xhci_dispatch.c](src/xhci_dispatch.c) 2744-2754 | `xhciResetController` header says "Recovery is a stop/start ... stays out of service permanently, which is the honest terminal state". The body (2793-2807) and design record 07 say the opposite. | Rewrite the header to the request/arm/perform model. |
| `src/xhci_dispatch.c` 2993-3001 | "A failure there costs one attempt ... the attempt cap bounds it either way" is false (F2). | Fix with F2. |
| `src/xhci_dispatch.c` 3009-3022 | `xhciGet32BitFrameNumber` comment describes a Phase 4 placeholder; the body calls `XhciFrameNumber` and `test_init.c:9872` already drives a model clock. | Keep only the last paragraph. |
| `src/xhci_init.c` 3225-3226 | "IMOD 0 is the value this driver programs everywhere" (F10). | Fix with F10. |
| `src/xhci_rh.c` 2652, 2544; `src/xhci_hw.h` 896 | `XhciRootHubInit` "PASSIVE_LEVEL (init only)" and `xhciRhDriveSuspendedPortsToU0` "PASSIVE_LEVEL (it waits)" are reached at DISPATCH from `XhciRecoverController` (`xhci_init.c:3789-3790`). Legal (`XhciDelayMs` stalls under `InitBelowPassive`) but the tags lack the two-form wording `xhci_init.c`/`xhci_pci.c` use. | Use the "PASSIVE, or DISPATCH with InitBelowPassive" form. |
| `src/xhci_init.c` 1971-1972 | `PortsPowered` "the number Phase 5's root hub is built on"; `XhciRootHubBuild` counts `XhciPortIsManaged`. Design record 10 sections 4.2/14.4 already flag it. | Correct the comment. |
| `src/xhci_init.c` 4334 | Logs `gate.busmaster.command = 0` on the recovery path because the gate was skipped (4326); reads as "PCI command register was 0". | Log "skipped" distinctly. |
| `src/xhci_slot.c` 1949-1962 | Block describing `xhciDevCancelWork` sits above `xhciDevCancelQueue` with a second header between, and its 1952-1955 still say Stop Endpoint and Set TR Dequeue Pointer "cannot be issued from a context that may not wait" and are task 7a-B.1's; the abort path issues them asynchronously through `xhciEpOweReposition`/`xhciEpArmIfBusy` (7821-7822, the machinery D6's AbortTransfer row describes). | Merge and place the headers beside their functions; drop the future-work claim and describe queue detachment separately from the implemented quiesce/reposition machinery. |
| `src/xhci_slot.c` 3439-3472 | Two full headers stacked on `xhciDevRelease`; the first discusses a retired sentinel design. | Merge. |
| `src/xhci_slot.c` 4500, 9888 | Names `xhciEpContextRestore` (is `xhciEpOweContextRestore`) and `ConfigureDci` (is `EndpointOpDci`). | Rename in the comments. |
| `src/xhci_slot.c` 3962-3963 | `XhciSlotInvalidateAll` comment says `EndpointExtension` "is what a later completion is answered through"; completions use `XHCI_TRANSFER.EndpointExtension`. | Correct. |
| `src/xhci_log.h` 202, 225 | "12 KB of non-paged pool" (ring is 16384 bytes; release notes say 16 KB); "106 in all" (96 + `=` + 8 hex + CRLF = 107). | Fix the arithmetic. |
| [src/xhci98.inf:6-7](src/xhci98.inf#L6-L7) | Header says both paths point at one `CopyFiles` section (the D2 claim, in the INF itself). | Describe `Xhci.CopyFiles` plus `Xhci.CopyW98`/`Xhci.CopyNT`. |

Known and deliberately not re-raised: the shipping arm of `xhciRhRefresh`
(`xhci_rh.c:327-330`) drops a change-bit acknowledgement when the PP-settle
holdback refuses the write; the fix is behind `XHCI_FIX_ACK_OWED` and the
comment at 298-315 records it as bench candidate W13 / Finding O.
`XHCI_FIX_QUIESCE_GATE` (`xhci_slot.c:3600-3632`) is likewise an `#ifdef`
block no flavour defines, documented as an experiment.

## D5 - Documentation, sample configuration and issue-form items

Active statements only; run sheets, dated lessons entries and cut release
directories are not flagged.

Treat optional precision changes separately from contradictions in active
instructions. A sample filesystem path is not a dependency on that host, a
branch name does not commit a release version, and a prose reference to a
paragraph is not automatically a broken Markdown link. None of those alone
justifies a correctness finding.

| Location | Finding | Suggested correction |
| --- | --- | --- |
| [docs/using/release-notes.md](docs/using/release-notes.md) 62-64 | "**NUSB must be installed first** ... Without it the driver will not load" is contradicted three sentences later by the SweetLow "third option", by 120-121 ("your choice") and by `README.md:37`. | "A USB 2.0 stack must be installed first: NUSB (tested) or SweetLow's." |
| `docs/using/release-notes.md` 295 | "nothing in the download is Microsoft's" (see F7). | "No Microsoft file is in the download." |
| [docs/using/release-acceptance-test.md](docs/using/release-acceptance-test.md) 51, 30-33, 146-151 | Equipment row names Windows 98 SE with NUSB or Windows 2000 only, though rows 4.5/4.6 and 7.7-7.12 test ME and XP; 30-33 says the SweetLow stack is not covered while row 4.5 requires it for ME; 146-151 predates the NT-path `usbport.sys`. | Bring the equipment row and the two paragraphs to 1.0.1.0. |
| [.github/ISSUE_TEMPLATE/bug_report.yml](.github/ISSUE_TEMPLATE/bug_report.yml) 76-91 | The required "USB stack underneath" dropdown has only Windows 98 and Windows 2000 entries while the OS dropdown (28-33) offers ME and XP; roadmap 18.4 (1316-1318) says the ME tier was stated in both forms. Placeholders `driver-version: "1.0.0.0"` (42; `hardware_report.yml:56`) read as stale. 19-21 states the disable/remove bugcheck unconditionally; the record says NUSB-specific. | Add "Windows ME: SweetLow's USB 2.0 stack" and "Windows XP: its own stack"; neutral placeholder. |
| [docs/contributing/roadmap.md](docs/contributing/roadmap.md) 40-59, 112-130; design record 10 707-708 | Optional status clarification: the design record names `1.0.2.0` while the branch is `1.1.0.0`, but a branch name is not a release-version commitment. No source-version conflict or authority to open a phase follows from this difference. AGENTS.md's SuperSpeed scope still describes the shipped driver. | Clarify pending work and numbering when the owner opens the phase; do not infer that decision from the branch name. |
| `docs/contributing/roadmap.md` 1347 | Phase 18 "Records: ... `handoff.md`" points at a file removed in `cd596cc`. | Drop the pointer. |
| [AGENTS.md](AGENTS.md) 88-134 | Layout block omits `images/` (tracked; used by README). 108 says `tools/` holds "the NUSB 3.3 package"; `legal-provenance.md:306-307` records `nusb36e.exe` there too. 243-249 reads as though `ExAllocatePool` is gate-permitted; the allowlist has no row for it (a call would fail as "not in the allowlist", not with the tag diagnosis). | Add `images/`; say "NUSB 3.3 and 3.6 packages"; say a row with Win98 evidence would be needed and none is intended. |
| [docs/contributing/legal-provenance.md](docs/contributing/legal-provenance.md) 521, 541-547 | "the first upload is intended to be 1.0.0.1" (newest cut is 1.0.1.0); "`README.md` links a releases page" (it does not; only `config.yml:7` and the generated readme do); 521 says Windows 2000's `usbd.sys` comes from `driver.cab` where the INF (344-346), `history.md:37` and roadmap 19.5 say `sp4.cab`. | Bring to 1.0.1.0; `sp4.cab`. |
| [docs/contributing/build-and-test.md](docs/contributing/build-and-test.md) 4211-4235 | "Post-authoring gate" describes a `TGT-*` family and `usbd-sources.expected` (removed in `556dcfa`), says `W98-*` covers a Win98-only `usbhub.sys` (on both paths since 1.0.1.0, `check-inf.ps1:852`), says `-PackageDir` authenticates per-target files by SHA-256 (`PKG-*` now checks presence and no-Microsoft-file only, 1705-1731); the rule-group list omits `OS-*` and `SUSP-*` though 4092-4103 describes them. 286 still says `history.md` "holds one entry". | Rewrite from the current rule list in `check-inf.ps1`. |
| [.gitignore](.gitignore) 85-107, 136, 63/156, 157 | Comment blocks describe the withdrawn Microsoft-files decision and `usbd-sources.expected` (D2); 136 says `test-package.ps1` scratch lives at `scripts/package/.testsrc-*/` (it uses `%TEMP%`, `test-package.ps1:99-100`); `!xhciqual/build.cmd` appears twice; `!xhciqual/test/run-qemu-test.cmd` is covered by `!xhciqual/test/*.cmd`. | Reword; dedupe. |
| [README.md:146](README.md#L146) | "since 1.0.0.1 ... (and, on Windows 2000 and XP, `usbport.sys`)": the `usbport.sys` half dates from 1.0.1.0. `release-notes.md:292-293` omits XP. | Split the two dates. |
| [docs/contributing/architecture.md](docs/contributing/architecture.md) 322-324 | Spike-era to-dos in the present tense ("add specific vendor/device IDs", "Confirm the exact ID ... during the spike"); prerequisite names NUSB 3.3 only. | Retire or date them. |
| [scripts/vm-matrix/README.md](scripts/vm-matrix/README.md) 230, 223-228; design record 06 sections 2/6; `run-matrix.ps1:1184` | "Five outcomes": the runner emits a sixth, `EXCLUDED`, and the Phase-10 report header lists five words while its body contains `EXCLUDED` rows. The README's example rows show a format (`NODRIVER  advance ... +0`) the tool does not write (`-> NODRIVER`, width 28 in post-release mode). | Add `EXCLUDED`; regenerate the examples from a real report. |
| [scripts/vm-matrix/config.sample.psd1](scripts/vm-matrix/config.sample.psd1) 181, 28, 226 | The example snapshot stamp is older than run-19, and the ISO paths are examples requiring local substitution; neither proves a broken configuration. The per-target `ReadySeconds` consumed at `run-matrix.ps1:864-865` is undocumented. | Mark the snapshot and ISO paths as examples to replace with actual local inputs; blanking valid examples is optional. Document `ReadySeconds`. |
| `scripts/setup-qemu.ps1:262` vs `build-and-test.md:2045` | ME install launcher uses `$MonitorPort + 3` = 55558, the port the document assigns to the win2k-acpi VM; the ME section names no port. | Pick a free port and name it. |
| QEMU generators | Optional consistency cleanup: `setup-qemu-winxp.ps1:85` and `setup-qemu-win2k-xonly.ps1:87` use a Program Files fallback and their generated launchers probe for it at run time (`setup-qemu-winxp.ps1:139`); the other three generators (`setup-qemu.ps1:69`, `setup-qemu-win2k.ps1:84`, `setup-qemu-win2k-smp.ps1:114`) use bare `qemu-system-x86_64` and their launchers invoke it directly (173, 181, 281). No failure from these differences was established. | Share the fallback if it simplifies maintenance; do not treat the difference alone as a defect. |
| [docs/contributing/design/03-host-unit-tests.md](docs/contributing/design/03-host-unit-tests.md) sections 3 and 5; [test/run-host-tests.cmd:166-167](test/run-host-tests.cmd#L166-L167) | Section 3 rows "Context builders" and "SET_ADDRESS interception" still show `-` though `test_ctx` and `test_init` (`test_slot_transfers` 13083-13092, `test_slot_set_address_refusals` 13427-13492) cover them. Section 5 and the runner comment say `/Za` is "the same dialect gate the DDK build applies"; the DDK build compiles with `/Ze` (every `cl` line in a build log), so the host suite is the only C89 gate and `xhci_dbg.c` is outside it. Section 5 also omits `/WX`, and its `test_init` row (455) names nine subject files where the runner links 17; section 2's redirected-primitive list (68-70) omits `InterlockedIncrement`. | Fill the suite column; reword the gate claim; update the lists. |
| [docs/contributing/design/05-locking-model.md](docs/contributing/design/05-locking-model.md) section 2; `implementation-invariants.md` "Locking and Lock Order" last bullet | "every other context tests `INITIALIZED` before touching controller state" is not true of the slot callbacks (F8). | Fix with F8. |
| [docs/contributing/design/07-controller-recovery-in-place.md](docs/contributing/design/07-controller-recovery-in-place.md) sections 5 and 7 | "20 ms once per attempt" omits confirmation polling. `xhciPowerPorts` calls `xhciSettlePortPower` twice: an optional 20 ms transition delay plus up to 20 ms polling in each call, in 5 ms steps (`xhci_init.c:1930-1934`, 1975, 1985; `xhci_hw.h:598-607`). That power-up routine can spend 60 ms; it is not a bound on the entire recovery, which has other waits and possible teardown. Section 7 omits F2's lost delivery. | Correct the power-settle accounting and its scope; add the chosen F2 policy. The earlier "three times per attempt" statement was not established. |
| [docs/usb-xhci-info/xhci-data-structures.md](docs/usb-xhci-info/xhci-data-structures.md) 357, 387, 150, 576 | 357 "IMOD: leave default" (F10); 387 USBLEGCTLSTS row lacks the RsvdP fields (F13); 150 states the PORTSC safe-write rule without the RsvdZ bits (2, 29:28) that `XHCI_PORTSC_UNSAFE_MASK` and `xhci-programming.md:604` include; 576 "TD composition and Link placement" is a paragraph lead-in, not a heading, yet `xhci-programming.md:523` and `implementation-invariants.md:187` cite it as a section (the enclosing heading at 474 is "Control transfers are two or three TDs", which does not describe the Link/CH material). | Amend 357 and 387; add the RsvdZ bits at 150; promote 576 to a `####` heading. |
| [docs/usb-xhci-info/xhci-programming.md:108-110](docs/usb-xhci-info/xhci-programming.md#L108-L110) | `XHCI_CAPS_NO_MANAGED_PORTS` "is the preflight's"; `xhci_init.c:719` raises it on either pass (`afterReset` passed through), so a re-parse reports it at `XHCI_INIT_STEP_PORT_MAP_RECHECK` (= 11). | "normally the preflight's; raised on either pass". |
| [xhciqual/README.md](xhciqual/README.md) "Command-line reference" (319-348), 396-397 | The reference omits `--done-flag FILE` and `--no-active` (both in `print_usage()`, `main.c:146`, 156) and `--controller TYPE`, an alias of `--scan TYPE` whose TYPE is required (`main.c:278-282`) and which `print_usage()` also omits; the parser strips any run of leading `-`/`/` (undocumented leniency). 396-397 says context size is "required" for xHCI; `report_controller` (`report.c:277-280`) prints `csz` and `final_verdict` (408) makes no requirement of it. `hardware-testing.md` never mentions `--quick`/`--serial`. | Reconcile parser, `print_usage()` and the README into one table, aliases included. |
| [xhcisnap/README.md](xhcisnap/README.md) 96-98; `xhcisnap.c:2349`, 2599-2604 | README promises an on-screen explanation only for `SNAP_S_NO_MMIO`; a controller with `PortCount == 0` yields a 0-byte `.PSC` with none. Usage says `-force` "asks first" but nothing prompts. `-probe`/`-dump` combined with `-verbosity`/`-disable` performs the verbosity write (2631-2636) and returns before the probe or dump runs, so the probe/dump is silently dropped. | Align the wording; refuse the combination. |
| [releases/history.md](releases/history.md) 61-62 | No blank line between the last 1.0.1.0 bullet and the `## 1.0.0.1` heading; this text is embedded into every `readme.txt`. | Add the blank line. |

## D6 - Technical documents vs the code

An identifier sweep (about 440 backticked tokens in `implementation-invariants.md`,
79 in the ABI/interface pair, 110 in design record 02, and roughly 230 register
offsets, masks and codes in `xhci-data-structures.md`) found no stale driver
identifier except the two named below, and every `USBPORT_*` struct offset,
size and constant in `usbport-miniport-abi.md` matches `src/xhci_usbport.h`
field for field. That is the earlier pass's inventory report. The rows below
include actual behavior/reason drift, optional wording precision, and prose
reference improvements; they are not all correctness defects. The independent
review corrected the AbortTransfer row by following its callee rather than
trusting the wrapper comment.

| Location | Finding | Suggested correction |
| --- | --- | --- |
| [docs/usb-xhci-info/usbport-miniport-abi.md](docs/usb-xhci-info/usbport-miniport-abi.md) 798-799, 502; [docs/usb-xhci-info/usbport-miniport-interface.md:119](docs/usb-xhci-info/usbport-miniport-interface.md#L119) | `FlushInterrupts` is counter-only because "this caller cannot hold the controller lock every ERDP writer holds". `src/xhci_evt.c:1204-1241` and design record 05 lines 283-286 say the reason changed: the lock exists and taking it is legal; there is simply nothing to do at the call site. The behaviour is right, the reason is the superseded one, and it points a future phase away from a legal action. | Remove the obsolete lock prohibition in all three places, but do not copy `xhci_evt.c`'s rationale whole: its 1228-1230 say the pending state is cleared by "the resume, which reinitializes through HCRST", and a successful `xhciRestoreState` (`xhci_init.c:3500-3501`) returns `MP_STATUS_SUCCESS` at 3563 with no HCRST, handling stale events through `XhciEventDiscardStale` (3351) before the enables return. Say that resume owns the pending event state on both paths, and correct the `xhci_evt.c` comment too (F10's successful-restore distinction). |
| `usbport-miniport-abi.md:857`; [docs/contributing/design/03-host-unit-tests.md:645](docs/contributing/design/03-host-unit-tests.md#L645) | `XhciControllerLock` and `CommandLock` do not exist; the lock is `xhciControllerLock` (`src/xhci_cmd.c:128`), reached through `XhciControllerLockAcquire`/`Release`. | Use the real name. |
| `usbport-miniport-interface.md:152`; `src/xhci_dispatch.c:3530-3534` | The wrapper's "not task 7a-B.2's cancellation machine" and "ring is left alone" comment is stale. The actual `XhciSlotAbortTransfer` removes the queued transfer, calls `xhciEpOweReposition` and `xhciEpArmIfBusy`, and drives deferred work (`xhci_slot.c:7819` onward). PAUSED can start the stop before AbortTransfer, which cannot wait for completion. | Correct the wrapper comment. Describe the implemented stop/reposition chain and the synchronous abort return separately; do not label ring cleanup as future work or promise a second canceled completion after usbport has reclaimed the transfer. |
| `usbport-miniport-abi.md:696-697` | `XhciRequestControllerReset` "on a command timeout" only; there are five callers: `xhci_cmd.c:1598`, `xhci_evt.c:931` (HCE/HSE), `:1072`, `:1168` (interrupt enable/disable could not be proven), `xhci_dispatch.c:2579` (the health poll, which the same document's `ResetController` narrative rests on). | List all five triggers. |
| `usbport-miniport-interface.md:116`, 130-133 | `Get32BitFrameNumber` row still says "`MFINDEX >> 3` + software-extended rollover"; the ABI document (498) and `xhci_init.c:2374-2382` describe the masked-delta form. `RebalanceEndpoint` "accept and update the context if intervals changed" is a trace line (`xhci_dispatch.c:3382-3390`); `PollEndpoint` "stall-recovery sweep" is log-only. | Point the interface rows at the ABI rows. |
| [docs/usb-xhci-info/win98-wdm.md:389](docs/usb-xhci-info/win98-wdm.md#L389) | Cites `usbport-miniport-abi.md` "Trust order" as a heading; it is prose in the preamble (abi 15). `usbport-miniport-abi.md:2782`, 3049 cite the async-callback heading truncated; `legal-provenance.md:388` cites the `USBPORT_GetTt` defect as "abi §6" (it is section 8, 2788/2830). | Promote or reword; fix the section number. |
| [docs/contributing/implementation-invariants.md](docs/contributing/implementation-invariants.md) 1284-1290 | "There is one way to read a port, and everything uses it ... one sanctioned exception" (`xhciPassThru`). `XhciReadPortsc` has 11 call sites; nine are neither the refresh nor the instrument (`xhci_init.c:1783`, 1919; `xhci_rh.c:723`, 783, 1039, 1293, 1608, 2297, 2614). All are pure single-field reads that acknowledge nothing, so they are safe, but the invariant as written is refuted and a reader would flag them. `XhciWritePortsc` likewise has three writers, not one. | Scope the rule to change-bit acknowledgement: "any path that acknowledges PORTSC change bits does so through `xhciRhRefresh`; raw single-field reads are permitted". Re-point `passthru-snapshot-instrument.md:34` at the exception bullet. |
| `implementation-invariants.md` 193-195 | "`CheckController` polls USBSTS.HCE and HSE every invocation" omits admission guards. The health poll declines before reading when the hardware status is bad, the controller has failed, or `INITIALIZED` is clear; an all-ones result is handled after the read. | Scope the statement to admitted invocations and distinguish a declined read from a failed read. |
| `implementation-invariants.md` 762-764; `src/xhci_pci.c:357-359`; design record 07 section 5 | "20 ms once per attempt" omits the two power-up confirmation loops, each bounded at 20 ms with 5 ms steps, plus the conditional 20 ms transition delay. The 60 ms figure describes `xhciPowerPorts`, not every wait in recovery. | Use D5's corrected accounting in all three places; the previous "bounded 5 ms confirmation poll" wording confused step size with total allowance. |
| `implementation-invariants.md` 133-135, 165-167 | Optional precision: production creates the lock through `XhciControllerGlobalInit` called by `DriverEntry`; the host suite calling the helper does not violate a production initialization invariant. The two admission fields are both changed on init entry, though the prose lists them in reverse statement order. No functional failure follows from either wording difference alone. | Name the helper or clarify ordering if useful; do not classify the host-only call as a driver defect. |
| `xhci-data-structures.md` 350, 576; `xhci-programming.md:350` | "ISR/DPC rules" and "TD composition and Link placement" are paragraph labels cited elsewhere as sections (`implementation-invariants.md:647`, 187; `xhci-programming.md:523`); "Command Ring Discipline" (`invariants:90`) is a prefix of the real heading. | Promote the labels to headings. |
| [docs/contributing/design/03-host-unit-tests.md:460](docs/contributing/design/03-host-unit-tests.md#L460) | The `test_log` row says `ZwCreateFile`/`ZwWriteFile` "live in `src/xhci_dispatch.c`"; task 13-L.2 retired the file sink and both imports (`run-host-tests.cmd`, `src/sources`, `xhci_log.h:36`, `xhci98-imports.allow:119`, design record 08 line 541 all say so). Only comments recording the removal remain in `xhci_dispatch.c`. | Drop the two names; keep the `KeGetCurrentIrql` guard and the two registry reads. |
| [docs/contributing/design/05-locking-model.md](docs/contributing/design/05-locking-model.md) 326, 300-333, 246-249 | 326 names `ReopenPipe` in the table of this driver's entry points; the callback is `ReopenEndpoint` (`ReopenPipe` is usbport's own routine, used correctly at 628). The "every entry point" table, stated as derived from `grep "static .*NTAPI"`, omits `PassThru` (which takes the controller lock across the whole snapshot, record 08 section 13's subject), `CloseEndpoint`, `GetEndpointState` and `QueryEndpointRequirements` (each takes the lock through `XhciProbeEndpoint`, `xhci_probe.c:595`), and `SetEndpointStatus`, `GetEndpointStatus`, `SetEndpointDataToggle`, `RebalanceEndpoint`, `StartSendOnePacket`, `EndSendOnePacket`. 246-249 "three ERDP writers": a fourth sits behind `XHCI_FIX_EVT_REARM` (`xhci_cmd.c:747`, under the lock). | Re-run the stated derivation and add the rows; note the bench candidate. |
| [docs/contributing/design/04-controller-common-buffer.md](docs/contributing/design/04-controller-common-buffer.md) 122, 449, 492 | "enabling all 64 offered slots would cost 96 KB": at the record's 3 KB/slot that is the cost of the extra 32, not of 64. The committed value is `XHCI_DECLARED_RESOURCES_SIZE`, which `XHCI_PROBE_RESOURCES_SIZE` overrides (`xhci_dispatch.c:103-105`); the record never mentions the override though section 5 open item 1 would use it. Every offset and the 409,600-byte total re-derive correctly. | "a further 96 KB"; mention the override. |
| [docs/contributing/design/06-device-matrix-verdict.md](docs/contributing/design/06-device-matrix-verdict.md) 100-108 | "Five kinds" lists `advance-by` as a Kind; `verdict.ps1:28,35` produces four, with `advance >= N` as `Kind = "advance"`, `Min = N`. | Four kinds. |
| [docs/contributing/design/07-controller-recovery-in-place.md](docs/contributing/design/07-controller-recovery-in-place.md) 105-113; [docs/contributing/design/08-build-flavours-and-the-log-channel.md:868](docs/contributing/design/08-build-flavours-and-the-log-channel.md#L868) | The recovery sequence diagram omits the USBSTS halt read feeding `XhciSlotInvalidateAll`'s `halted` argument and the `XhciSlotDeferredWork` that follows (`xhci_init.c:3774-3787`). `REGION_EXTENSION` is `XHCI_SNAPSHOT_REGION_EXTENSION` (`xhci.h:7691`; 673 spells the pair out as `XHCI_SNAPSHOT_REGION_PORTSC` and `..._EXTENSION`). | Add the two steps; full name. |
| `usbport-miniport-abi.md` 2041, 1756 | `USBPORT_RESOURCES.Reserved` typed `ULONG_PTR` (header: `ULONG`, same width on x86); the root-hub rows at 1756-1757 use ReactOS's `PUSB_PORT_STATUS_AND_CHANGE` and `PUSB_HUB_STATUS_AND_CHANGE` where the header's signatures (`src/xhci_usbport.h:443-444`, typedefs at 313-321) use `PUSBPORT_PORT_STATUS_AND_CHANGE` and `PUSBPORT_HUB_STATUS_AND_CHANGE`. | One parenthetical each, both spellings, so a grep succeeds. |

The earlier pass reported matching structure/register tables and broad
agreement in records 02 and 09 and the named invariant sections. Treat that as
the scope of its comparison, not proof that every behavioral statement or
suggested correction is exact; the review adjustments above demonstrate why
source behavior must still be traced before editing those documents.

## Verification performed this pass

| Check | Result |
| --- | --- |
| `scripts\build-driver.cmd all` (with `PSModulePath` pinned to Windows PowerShell, as the first pass did) | Passed: debug, release, qemu; import gate, INF gate, flavour gates |
| Host driver/core suites run by that build | 19,937 checks across 12 suites; zero failures |
| Evidence manifest / flavour-rule / INF-gate / packager / launcher self-tests | 15 / 42 / 310 / 153 / 116 checks; passed |
| `scripts\vm-matrix\selftest.ps1` | 196 checks, all passed (the refused monitor connection is its negative test) |
| `xhciqual\test\run-host-tests.cmd` | 146 checks, 0 failed |
| `xhciqual\test\check-bat-eol.ps1` | All 10 tracked `.BAT` are CRLF; `git ls-files --eol` shows `w/crlf` for every pinned file |
| PowerShell 5.1 parser over every tracked `.ps1`/`.psd1` | No syntax errors, no PS6+ idioms |
| Relative Markdown links and heading anchors (top-level, release and usb-xhci-info documents) | No unresolved file links; one cited heading that does not exist (D5, `xhci-data-structures.md:576`) |
| ~230 register offsets, bit positions, masks, TRB types, completion codes and context fields in `xhci-data-structures.md` vs `src/xhci.h` | All match; the miniport ABI struct sizes in `xhci_usbport.h` match `usbport-miniport-abi.md` |
| `offsets.txt`/`offsets.labels.txt` re-derived from `src\*.c` with `gen-offsets.ps1`'s own regex | 384 fields, 386 labels, byte-identical to the committed tables; `SIZEOF 90928` matches the run-19 header |
| F3 reproduction with the real verdict functions | `NODRIVER` for all five non-default refusal counters |
| F4 reproduction with the extracted guard | Three destinations outside the current cut accepted |
| F11 reproduction with the real `Get-TargetVerdict` | `Rows=3, NotReached=3` gives `PASS` |
| Line-ending, tab and BOM scan | Tabs in two text files: `xhciqual/makefile` (required) and the tab-separated `scripts/vm-matrix/offsets.labels.txt`. UTF-8 BOM on nine tracked files: `roadmap.md`, `check-inf.ps1`, `test-inf-checks.ps1`, `xhcisnap/README.md`, `offsets.labels.txt` (stripped by `Get-Content`), and the 2a/2b post-release reports under `runs/run-16-post-release/` and `runs/run-19-post-release/` (dated evidence; leave them). Harmless throughout |

Build artefacts from this pass (`src\obj*`, `src\build*.log`) are git-ignored
and were left in place.

## Next-session order, revised

1. Fix the reproduced validation gaps F3, F9 and F11 together with matrix
   regression vectors. Fix F4 and F14 using isolated containment/INF tests;
   address F15 as an upload-assembly workflow improvement without recutting.
2. Investigate F1 and F8 together as endpoint/table ownership problems. Pin
   binding identity, record reuse, owed completions, and the active-drainer
   interleaving before implementation. Establish supported callback sequences;
   do not equate a host probe with a guest observation.
3. Decide F2's delivery-loss policy and correct its comments/design record.
   If retry is chosen, test expiry, late delivery, bounded repeated loss,
   suspend/restart, and single-recovery ownership. Use a synchronized age
   source that continues while failed; an unqualified `INITIALIZED` gate or
   `PollClockMs` is not the answer.
4. Correct F6-F7's active provenance/readme statements, F18's Windows 2000
   idle-suspend assumption at every active site, and D1's release-policy
   conflict. Address F5/F17 with deterministic output-failure/mismatch tests.
   Handle F10/F13 with register-model checks, F12 with a command-specific
   ownership vector, and F16 as low-priority qualifier cleanup.
5. Correct confirmed D2-D6 drift using implementation behavior, including the
   revised AbortTransfer row. Keep optional style/sample changes separate.
   Leave cut directories and dated evidence alone.
6. After implementation, run the relevant suites and required build gates,
   then primary-target VM checks, Windows 2000 SMP recovery, and the XP
   restore/lifecycle sequence as applicable. F10 needs an explicitly
   successful restore model/path; a QEMU fallback resume is insufficient.
   Windows 2000 observations remain VM-only, and Windows 98 metal does not
   cover that target.

Do not recut or advance a checkpoint merely because the host tests pass.
