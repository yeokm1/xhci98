# Phase 20 Record - the 2026-09-05 audit fixes and the `1.0.2.0` readings

The detail behind `docs/contributing/roadmap.md`, "Phase 20 - Release
`1.0.2.0`: The 2026-09-05 Audit Fixes". The roadmap entry carries the goal,
the status, the findings and the task list; this file carries what each task
changed and what each reading said. Where the two disagree about a clause,
the roadmap wins.

The audit document itself (`issues-found.md`, kept at the repository root
while the phase ran, reviewed to convergence in three rounds on 2026-09-05,
then the fix pass reviewed by Codex in seven rounds to NONE) was removed on
2026-09-06 once every finding was closed. The roadmap's findings summary is
what remains of it, and citations of the form "roadmap Phase 20, F18" resolve
there.

**On `out\...` and `vm\...` paths in this file.** They say where a reading was
taken and what the file was called, on the host that ran it; they are not
files a clone has.

## 20.0 the matrix verdict (F3, F9, F11)

Driver-refusal evidence takes precedence over both `PASS` and the `NODRIVER`
inference in `scripts/vm-matrix/lib/verdict.ps1`, with the refusal reason in
the verdict, `zero` expectations for the open and configure failure counters
in `matrix.psd1`, and an all-`EXCLUDED` target reading `FAIL` or `ERROR` in
`Get-TargetVerdict`. The audit's exact reproduction vectors went into
`selftest.ps1`, a true never-claimed `NODRIVER` retained. The rule is design
record 06 section 2.1.

## 20.1 the packaging guards (F4, F14, F15)

`Assert-UploadSetOutsideRelease` checks the upload tree and the ZIP against
the whole `releases\` root. The INF gate's `PATH-W98` mirrors `PATH-NT`'s
own-`CopyFiles` check for `NTMPDriver`, with the self-test that failed to
catch the audit's scratch INF. `-UploadSetOnly` assembles the current cut's
asset from its tracked directory alone; the `pkg-` hash check is retired, and
older cuts are refused with a message that says the gate's rules have moved
(the `1.0.0.1` INF fails six of them, read-only, 2026-09-05). A self-test for
each, on isolated temporary trees only, and a `test-package.ps1` case for a
clone with no `out\`.

## 20.2 endpoint and table ownership (F1, F8)

Current-binding validation under the controller lock at every endpoint
callback entry; a stale handle is closed locally without touching its
replacement, and stale submits are rejected through the completion contract.
`XhciSlotInit`'s device-table reset and `DeferredBusy` clearing are
serialised with the callbacks that read them, the active drainer's ownership
preserved across its unlocked interval.

Host vectors first: the audit's three F1 sequences with successful use of the
replacement after each, same-extension reopen, device-index reuse,
non-default endpoints, and an interleaving vector for the table reset. Then
design record 05 section 2 and the invariants were corrected. Issue 4's
working XP behaviour and `Ep0RemovesSuperseded` are preserved. The guest
readings are 20.7's.

## 20.3 recovery delivery loss (F2)

The policy is the bounded age-out, taken on 2026-09-05 and confirmed by the
owner on 2026-09-06 after a comparison with what Linux, Microsoft's own
usbport miniports and UCX do: none has the lost-delivery class, and the
bounded retry is the closest analogue to UCX's bounded controller reset. That
closed the documented terminal residual as an option.

Twenty health polls, a delivery generation that invalidates late callbacks
and cannot start two recoveries, the loss charged to the consecutive count,
an arming the latch no longer needs retired uncharged, aged by a clock that
still advances while `ControllerFailed` is set (not `PollClockMs`). Vectors:
one lost delivery, eventual delivery, a late callback from an expired
request, repeated loss to the terminal state, and suspend and restart between
arming and delivery.

## 20.4 the shipped statements (F6, F7, F18, D1)

The `LICENSE` scope paragraph was rewritten as history in
`legal-provenance.md` section 5's form. The `make-release.ps1` readme
template and `release-notes.md` were corrected of the claims false since
`1.0.0.1` ("WINDOWS 98 ONLY", "redistributes nothing of Microsoft's", the
stale table of contents, the 0.x sentence, "Until 1.0.0.1"), with a packager
self-test that greps the rendered readme for the two forbidden phrases.

The "Windows 2000's native `usbport` never idle-suspends this controller"
statement and its "changes nothing" conclusion were removed or qualified at
every active site F18 lists: the INF comment, the readme template, the
release notes, the acceptance test, both gate comments, `xhci_dispatch.c`,
`xhci.h`, the invariants and `build-and-test.md`. Dated entries and
`history.md` were given a dated qualification rather than a rewrite. The
post-upload paragraph at the end of the roadmap now defers to
`releases/README.md`'s uploaded rule. The Windows 2000 SP4 idle observation
the qualification owed was taken in 20.7. No cut directory was edited: F6 and
F7 reached the download at the next cut.

## 20.5 the register and tool items (F5, F10, F12, F13, F16, F17)

- `XHCISNAP` tracks `ferror` and `fclose` and exits nonzero on a report it
  could not finish, and carries the extension-size mismatch into the summary
  instead of contradicting itself. Deterministic write-failure and
  close-failure injection behind both.
- `xhciRestoreState` restores IMOD rather than writing zero, read back
  through `test_init`'s successful-restore model, and the three IMOD
  statements were made to agree. QEMU fails every restore, so the reading on
  a controller whose restore succeeds is owed, through the release notes'
  Force Save Context limitation.
- `XHCI_USBLEGCTLSTS_SMI_ENABLES` is the five enable bits with the RsvdP
  fields preserved and documented.
- The Command Ring Stopped case whose reported pointer still names the
  abandoned command is pinned by a host vector and resolved with a
  command-ring No Op (type 23, not `XhciRingNoOpAt`'s type 8) rather than the
  divergence reset. The hardware trigger is unobserved.
- `xhciqual`'s EHCI cleanup masks the RW1C status bits. No DOS run was made.

## 20.6 the smaller items and D2-D6

Each smaller item was promoted to a fix only once its contract was
established and the failure reproduced, and the D2-D6 drift rows were
corrected from implementation behaviour: the roadmap's Phase 18 pointer to a
removed `handoff.md`, the `1.0.0.1` name in its post-release paragraph,
`docs/README.md`'s phase-reading table carried to Phases 17-20, the
design-record and ABI-document rows, and the stale comments and drifted IRQL
tags in D4. Cut directories and dated evidence were untouched. The Transfer
Event RsvdZ low-bits row is done, one shared mask in `xhci_xfer.c`, counted
per queue and folded.

Four items were left at the cut with their dispositions and taken up by the
owner on 2026-09-07 after it, as working-source changes implying no guest
reading and no change to the `1.0.2.0` cut - except the first, which the
third re-cut then published:

- The addressed-`FAILED`-record EP0 reopen. `xhciSlotOpenControl` had been
  accepting a record `xhciDevFailRecord` left `ADDRESS_VALID` on, and
  queueing `EVALUATE_MPS` on it. `test_slot_failed_record_ep0_reopen`
  reproduced it - the progress detector really does fail an addressed record,
  and same-MPS and changed-MPS opens and reopens must refuse without
  rebinding or issuing a command; twelve assertions failed before the fix.
  `xhciSlotOpenControl` now applies the shared `xhciDevMayOpenEndpoint`
  admission check, preserving the address for teardown.
- The import gate's duplicate-row matching. Its split debug/qemu rows failed
  in both orders until the matcher included flavour; synthetic vectors now
  cover both orders, release refusal, provider and symbol identity, required
  imports and deny precedence.
- The async mock gained an opt-in copied-context queue. F2's suspend and
  resume vector now holds both recovery and watchdog callbacks, delivers the
  newer watchdogs first and then the original recovery, and verifies that the
  recovery arming is released without another recovery. (The one vector that
  needed two pending callbacks had captured its recovery callback explicitly
  instead; the Phase 20 review caught the first version firing the wrong one.)
- The `PSModulePath` guard, which the audit had left unreproduced. It
  reproduces only through the actual launch chain: PowerShell 7.6.5 -> `cmd`
  -> Windows PowerShell 5.1 retained the PS7 module paths and could not
  resolve `Get-FileHash`, where a direct PowerShell child did.
  `build-driver.cmd` now selects the Windows PowerShell module paths inside
  `setlocal` (`lessons.md`).

Verification: `build-driver.cmd all` from that PS7 parent, all three flavours
and import gates; 20,325 host checks across twelve suites (`test_init`
12,585), evidence manifests 15, import flavour rules 53, INF 312, packager
179, launchers 116, matrix 230, and the snapshot-reader self-test. Log:
`out/open-items-build-all.log`. The optional D5 status clarification was left
to the owner who names the version.

## 20.7 the gates and the readings

The guest half was taken on 2026-09-06 with the owner driving every guest GUI
and the harness driving the monitor. Environments: QEMU 11.0.0, TCG for the
fresh clones, WHPX for the XP guest and for the SMP guest, which ran two CPUs
with Driver Verifier listing `xhci98.sys` (the owner's `verifier
/querysettings`). The binary the readings stand on is the qemu flavour built
11:10:51, sha256 `b75f48eeb9f29ff8`, with the F19 fix in and only comments
differing from the commit; a first pass on the pre-F19 build
(`e888718098f6983b`) read the same on every row.

The host gates: `build-driver.cmd all` with every self-test,
`xhciqual\test\run-host-tests.cmd`, and `vm-matrix\selftest.ps1`.

**The post-release matrix**, on fresh clones of `win98.img @ post-nusb` and
`win2k-xonly.img @ win2k-xonly-clean-install`, the package installed by the
owner in each, fourteen device classes taught on the Windows 98 image (the
two tablets left out as in run 19), both stamped `base-1.0.1.0-qemu` since
the version was not yet named. `2b-fresh` PASS, 17 rows, 6 NODRIVER expected,
0 against, 1:16:32, the report identical to Phase 19's outside the seven
refusal-counter expectations 20.0 added to each row. `2a-fresh` FAIL, 17
rows, 5 NODRIVER expected, 3 not reached, 1 against, 0:57:44, and the one row
is the `usb-audio/fs` replug: the second arrival's connect change was
announced to usbport and no port reset followed, so the device was never
addressed. It read the same on both binaries. That row was read four times
across 20.7, 20.8 and 20.9 and traced; it is host contention from a second
concurrent emulator, not a driver defect, and `lessons.md` carries the
evidence, the refuted hypotheses and the one caveat. 20.8 is the resolution.

**The Windows 2000 SMP in-place recovery** for 20.2 (F8), on `win2k-smp.img`
with the new build copied in. The controller was killed from outside the
guest through QEMU's gdb stub - interrupter 0's `ERSTBA` written to an
unmapped address, which makes QEMU's model set `USBSTS.HCE`, the method
verified on a throwaway instance first - with a monitor-pumped mouse and a
bulk-only disk behind a Full-Speed hub attached. On the pre-F19 build: one
recovery (attempt 1, completion 1, every device re-enumerated, the guest
healthy) and then three further HCEs never escalated, which is F19. On the
fixed build, four provocations: fatal status detected 4, resets requested 4,
recovery attempts 4, completions 4, refusals 0, the devices re-enumerated
each time with mouse traffic resuming, and `Ep0RemovesSuperseded` climbing
three to six per recovery as Windows 2000 re-created each device through a
new handle - which exercised F1's path on two CPUs under Verifier. No
bugcheck.

**The XP restore and lifecycle sequence** for 20.2 (F1), on `winxp.img`
reverted to `winxp-clean-install` (the previous state kept as
`pre-phase20-xp-2026-09-06`) with the new package installed by Have Disk.
`usb-storage` on its first-ever attach reproduced issue 4's two-handle
restore (slots reset to Default 1, the superseded-handle counter 1) and bound
(bulk pair open, 385 transfers); `usb-audio` first-ever attach the same
(reset 2, counter 2, the isochronous endpoint opened). Then disable, enable,
uninstall and scan for hardware changes: three `StartController` and two
`StopController` across three extensions, both devices rebound on the last,
every refusal counter at zero.

**The Windows 2000 SP4 idle observation** owed since 20.4 (F18,
`build-and-test.md`): SP4's stack was not seen idling the controller, value
present or deleted, nothing attached and then a mouse on the Standard PC
guest, a mouse attached and no value on the ACPI SMP guest, in the intervals
recorded there. A string-level candidate reason is recorded in
`legal-provenance.md` section 4 as unconfirmed.

**20.5's successful-restore reading** is the host model's (`test_init`'s
conforming controller). QEMU fails every restore, so no VM can supply the
hardware reading, and the release notes' Force Save Context limitation says
it is owed.

Reports in `run-20-post-release/`.

## 20.8 the audio replug row

The owner's chosen way (2026-09-06) to resolve the one row 20.7 left against
the checkpoint: the two targets run sequentially, not side by side.

Solo `2a-fresh` on the night of 2026-09-06: PASS, 17 rows, 5 NODRIVER
expected, 3 not reached, 0 against, 0:57:13, started 23:24:51, the same
binary as 20.7 (`b75f48eeb9f29ff8`), QEMU 11.0.0 under TCG, no other guest on
the host; both `usb-audio/fs` legs PASS. The report body is Phase 19's with
nothing removed and only the seven refusal-counter expectations 20.0 added on
each of the 28 legs, which satisfies the checkpoint's "no worse than the
Phase 19 reports" clause. Three things about how it was taken:

- The clean boot was done on a copy of `fresh-2a.img` on the local disk (the
  synced tree is where a writable boot has hung QEMU before). It reached the
  normal desktop in about 23 s with the driver up, no Safe Mode and no
  ScanDisk pass seen, so the dirty-shutdown reading of the first attempt is
  not confirmed by this boot; the Start-menu shutdown was driven over the
  monitor and QEMU reported a clean shutdown, the image checks clean and its
  stamp stays its only snapshot. That copy was not written back over
  `vm\fresh-2a.img`, and the run booted the copy through a configuration
  differing from the real one only in `VmDir`, which is why that report's
  image line names a scratch path.
- The owner chose, asked mid-run, to read the audio group FIRST: a pure
  reorder of the matrix, so the row was the first boot of the run rather than
  the fifth, which is the condition 20.7 could not clear. A first solo run in
  the matrix's own order was stopped eight minutes in for it and its partial
  output discarded.
- Windows 2000 was not run in that solo pass. `2b-fresh` had passed
  identically in every run, and a sequential 2b could not change the 2a
  result.

Then, on 2026-09-07 at the owner's order, the audio group was made the first
group of `scripts/vm-matrix/matrix.psd1` itself (a pure reorder of the block,
with a comment at the group; self-test 230 checks and validation green) and
both fresh targets were run SIDE BY SIDE again in the 20.7 shape, two
invocations at once from the `vm\` images, started 00:27:00 and 00:27:01:
`2a-fresh` PASS, 17 rows, 5 NODRIVER expected, 3 not reached, 0 against,
0:57:07, both `usb-audio/fs` legs PASS with the Windows 2000 guest running
beside it throughout; `2b-fresh` PASS, 17 rows, 6 NODRIVER expected, 0
against, 1:16:08. The Windows 98 body is identical to the solo report's; the
Windows 2000 body is the 20.7 report's, reordered, with the storage row's
transfer identity reading a different count (437 against 354) as such counts
do. Evidence in `out\post-release\phase20-8-paired-{2a,2b}\`. These two paired
reports were the ones in `run-20-post-release/` until the confirming run of
20.9 replaced them; they read the pre-cut `1.0.1.0` qemu binary
`b75f48eeb9f29ff8`, which is why they were replaced. A single-invocation
`-Target 2a-fresh,2b-fresh` start, which runs the targets one after the other
rather than in the paired shape, was stopped a minute in and its partial
output discarded.

The sequential procedure this task was first written as is retired, not
deferred, and nothing is owed against it. It was carried out on 2026-09-06
and passed, and the de-paired pass of 2026-09-07 repeated that result on the
same image, stamp, binary and host twenty minutes after the paired attempt
failed. Across the four readings this row has had, it passes in every shape
where the Windows 98 guest runs alone and has failed only with a second guest
beside it, which is as far as a matrix run can take it; re-running the long
solo matrix would re-measure a row already read four times and could not
change what the driver does.

## 20.9 the cut, and the readings on it

The version is `1.0.2.0`, named by the owner on 2026-09-07 when the cut
opened: `1,0,2,0` and the date `09/07/2026` in `src\xhci_version.h`, the
INF's `DriverVer`, the `history.md` entry (what changed, F1-F19 and D1-D6
named, F10's hardware reading owed through the release notes' Force Save
Context entry), the release notes' header with its XP "since this release"
phrase pinned to `1.0.1.0`, `legal-provenance.md` section 5's newest-cut
sentence, the two issue forms' example number, and the roadmap's Status
paragraph. The DisableSelectiveSuspend and Force Save Context bullets were
re-read and left as 20.4 and 20.7 wrote them.

Three cuts under the same number and the same date, which the uploaded-nothing
rule in `releases\README.md` allows:

1. The cut. `build-driver.cmd all` on that header (host suite
   20,325 checks across twelve suites, `test_init` 12,539 of them; INF gate
   self-tests 312, packager 179, launchers 116,
   matrix 230, `XHCISNAP` 4 cases, the import gate on all three flavours),
   `XHCIQUAL.EXE` and `XHCISNAP.EXE` rebuilt after the header and both
   printing `1.0.2.0`, `make-release.ps1` exit 0, zip 254,452 B.
2. The first re-cut, because the driver changed after the cut and
   `releases\1.0.2.0\` no longer matched the source it is published from.
   Every gate green a second time on the tree it was taken from (host suite
   20,325 checks across twelve suites, `test_init` 12,585; the rest as
   above); `make-release.ps1 -Force` exit 0, zip 254,751 B and then 254,821 B
   once the guest readings requalified the `history.md` sentence the
   download's `readme.txt` embeds. That regeneration restaged the same object
   trees, so both driver binaries and the INF are byte-identical across it.
3. The evening re-cut, when the owner decided `usbui.dll` goes
   into this release rather than the next. `make-release.ps1 -Force` exit 0,
   zip 257,160 B and thirteen files, `readme.txt` 57,082 -> 55,159 B, both
   flavours' INF 23,541 -> 27,840 B and byte-identical to the source INF and
   to each other. Two prose-only regenerations followed, neither of them a
   fourth cut and neither touching a binary: the shipped artefacts are zip
   257,313 B and `readme.txt` 55,775 B, which are what `releases\1.0.2.0\` and
   `out\xhci98-1.0.2.0.zip` hold.

NO BINARY MOVED across any re-cut, which is the check a `-Force` re-cut owes:
it stages from the existing object trees rather than building, and an
INF-only change cannot reach a `.sys`. Published sha256 prefixes: release
`69e7de836f047558`, debug `9581458ef960e920`, and the qemu flavour from the
same build `df4d16fc249905b7`. The first cut's own binaries
(`c4cedeee4449434d`, `49703e29539fe177`) are superseded and were never
published anywhere.

**`usbui.dll`.** The INF copies it on all four install paths at dirid 11
through the same `LayoutFile` route and flag 16 as the three drivers, and the
INF gate holds it to that destination. The reason is the NT root
hub, measured that day in both NT guests: Windows 2000's `USB.INF`
`[ROOTHUB2.NT]` and Windows XP's `usbport.inf` `[ROOTHUB.Dev.NT]` already
register `usbui.dll` as the hub page's provider, so on an xHCI-only machine
that reference dangles and the Power tab is silently absent; placing the file
brings it back, with no registry change. On Windows 98 it buys nothing, also
measured: the 9x controller page comes from `sysclass.dll`, and the tab
renders identically with `usbui.dll` renamed away in MS-DOS mode. The owner's
E460 fits, carrying `sysclass.dll`, no `usbui.dll` and no tab, because this
INF registers no property page at all; it still does not.

The Windows 2000 prompt risk the file introduced was read and is silent: on
a clone of `win2k-xonly.img` rolled back to
`win2k-xonly-clean-install` and verified to hold none of the four files, the
install fetched three from `sp4.cab` and `usbui.dll` from `driver.cab` in one
pass and asked for nothing, the root hub starting and its Power tab rendering
being the two halves of that proof. The acceptance test now takes that
reading itself, step 4's per-target rows naming the file and a new 4.7 taking
the Power tab, which is what lets the Windows 98, Windows ME and
Windows XP install legs ride the acceptance test rather than be re-read here;
the owner settled that, the test running before the upload rather than after.
`build-and-test.md` has the measurements and each target's source cabinet.

`history.md` was shortened and both re-cut paragraphs removed from it, its
own and `1.0.0.0`'s, so the download's `readme.txt` no longer
explains how the release was assembled.

**What the published driver is.** It differs from the binary 20.7 and 20.8
read (`b75f48eeb9f29ff8`) by one code path rather than by comments alone:
`xhciSlotOpenControl` refuses an addressed EP0 open or reopen on a FAILED
record through the shared `xhciDevMayOpenEndpoint` guard, with the host
vector `test_slot_failed_record_ep0_reopen` behind it (20.6). Everything else
between the two binaries - the Codex round-4 and round-5 commits and the
citation rewrite - changed no code line. The new refusal is on a path no
guest run has been through, but the guard it goes through sits in the
ordinary EP0 open path, so the readings below are the first guest exercise of
the published binary. They were taken over the sessions of 2026-09-07 in the
order the owner set that evening - the legs first, then the re-stamp, then
the confirming run - and all pass.

**The asset legs.** Four, each installing the published release binary
`69e7de836f047558` from a `RELEASE\` directory on the transfer drive, each
read from Device Manager on screen: leg 1 Windows 98 SE under NUSB 3.3 on a
fresh `post-nusb` clone, leg 3 Windows ME, leg 4 Windows 2000 SP4 on the
xHCI-only image, leg 5 Windows XP SP3 from the clean-install snapshot.
Controller and root hub clean on all four, a HID device bound on all four,
and each screen matches its `1.0.1.0` counterpart line for line, the base
images' own yellow bangs included. Leg 5 also re-read issue 4's clause: a
`usb-storage` on its first ever attach bound in turn with no replug. Screens
in `out\post-release\1.0.2.0\asset-legs\`. Four distinct usbport builds, so
the new guard is not a regression in the ordinary control-endpoint open path
on any of them, which is what a regression there would look like.

Leg 2, the SweetLow stack, was REMOVED rather than deferred, and not for the
reason first written down. That the INF has not changed since `1.0.1.0`
settles the packaging, and packaging was not this release's risk. What leg 2
alone could have answered is the one code change: the guard sits on the
reopen branch, where usbport has torn the control endpoint's private state
down and rebuilt it, so the regression it could cause is refusing a
LEGITIMATE reopen, and SweetLow's guest was the only one whose disable and
re-enable sequence survives, NUSB bugchecking on it. That question was
answered by the reopen check instead, and the guard reads only this driver's
own record state and address map, never which usbport build called it. What
was left in leg 2 was stack coverage `1.0.1.0` already had, against
rebuilding a guest whose image and config entry are both gone.

**The reopen check**, which is what was kept out of leg 2 and is the reading
aimed at the change itself: the XP disable and re-enable with the device
still attached at QEMU, and the same sequence repeated on the re-stamped
Windows 2000 guest reading `DevicesReopened` 1 and every refusal counter 0.
Both traversals are clean. `lessons.md` carries the method and what each
source proves; screens `reopen-xp-*.png` beside the legs, and the XP trace in
`vm\winxp-qemu-trace.reopen.log`. Not repeated on Windows 98: that sequence
is the one NUSB bugchecks on, the same fact that made leg 2 need SweetLow's
guest.

**The re-stamp.** Both fresh images were re-cloned from their bases, given
the re-cut's qemu build `df4d16fc249905b7`, and stamped `base-1.0.2.0-qemu`,
each stamp the only snapshot on its file; the fourteen device classes were
taught on the Windows 98 image, and the Windows 2000 one is prepared
install-only as every run of this project has prepared it.

**The confirming run**, `run-matrix.ps1 -PostRelease` on both re-stamped
images, 2026-09-07. Both PASS. `2a-fresh` 17 rows, 5 NODRIVER expected, 3 not
reached, 0 against, 0:57:52; `2b-fresh` 17 rows, 6 NODRIVER expected, 0 not
reached, 0 against, 1:16:39. Set against the 20.8 reports the checkpoint
names, the Windows 98 body is IDENTICAL, all 505 lines, and the Windows 2000
body differs in one line of 626: the storage row's transfer identity, which
holds in both and reads a different count, 357 against 437, as such counts
do. So the matrix is no worse than before on this release's own driver. Note
which binary this reads: the qemu flavour `df4d16fc249905b7`, not the
published release binary the legs install, from the same build; the two
readings do not substitute for one another. Evidence in
`out\post-release\1.0.2.0\`, and the two reports themselves are committed as
`run-20-post-release/post-release-{2a,2b}-fresh.txt`, replacing the 20.8
pre-cut pair that stood there before. Both images were booted with
`-snapshot` and neither was written: each still carries its stamp as its only
snapshot.

How it was taken, because the shape was chosen mid-run. It was started in
20.8's paired shape, two invocations side by side at 14:50:51, and the owner
stopped it when the Windows 98 `usb-audio/fs` REPLUG leg read FAIL again on
the 20.7 signature, the replugged device never addressed at all. The targets
were then de-paired on the owner's instruction: `2a-fresh` alone from
15:09:12, and `2b-fresh` started at 15:14:36 once the Windows 98 guest had
cleared its audio group. Alone, both audio legs PASS. That is a direct A/B on
one image, one stamp, one binary and one host twenty minutes apart, and it is
the strongest evidence for 20.8's isolation of that row to a second
concurrent guest rather than to the driver; moving the audio group first,
which was 20.8's remedy, did not by itself hold here. The Windows 2000 audio
row passed both legs in both shapes, as it always has. The aborted attempt's
partial output is parked under
`out\post-release\1.0.2.0\aborted-paired-1450\` with a note saying it is not
a reading.

One observation with no consequence: the Windows 2000 guest raises a Found
New Hardware wizard on the `usb-net` row and it goes unanswered, which costs
nothing - the harness reads counters over the monitor and the debug port
rather than off the screen, the row is one the matrix expects no driver for,
and the guest is discarded at the end of its group.

## What remains

The upload of the asset, the push, and the by-hand acceptance test the
roadmap ends on, taken from the release asset rather than from this tree and
run before the upload. All three are the owner's.
