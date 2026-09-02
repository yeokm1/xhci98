# Lessons Learned

This file records project-specific lessons that are easy to lose between
debugging sessions: surprising real-hardware behaviour, toolchain and extender
traps, misleading symptoms, and conclusions supported by measurements. It is
an evidence log, not a replacement for the normative design documents.

For each new lesson, record:

- the machine or environment, and the narrow operation being tested;
- the observed symptom and the evidence that survived the failure;
- what is proven, what is inferred, and what remains unknown;
- the reusable rule or next discriminating test; and
- links to affected source and normative documentation.

**On `vm\...` paths in this file.** The per-run evidence they name - debugcon
traces, QEMU trace logs, screenshots, counter dumps - was discarded on
2026-08-30 after this file had transcribed every reading it rests on. A
`vm\` path here says where a reading was taken and what the file was called;
it is not a file a clone, or the maintainer's own tree, still has. `vm/` now
holds only the guest images and transfer disks the harness needs.

Do not turn a hypothesis into a settled hardware quirk. Move confirmed design
rules into the appropriate normative document while keeping the debugging
history here.

## The Windows 98 teardown bugcheck belongs to the Windows 2000-lineage usbport, and an XP-lineage rebuild of the same stack survives every door it dies at

Environment: the `2a-sweetlow` guest (`vm\sweetlow-2a.img`, a clone of the
stamped `fresh-2a.img`, driver 1.0.0.0 qemu flavour already installed), QEMU
11.0.92 with `-machine pc,smm=off` (next entry), 2026-09-02, the project
owner at the console and the trace read from the host. The stack swap was
done inside the guest: NUSB 3.3's own uninstall string
(`RUNDLL32.EXE C:\WINDOWS\SYSTEM\ADVPACK.DLL,LaunchINFSection C:\WINDOWS\INF\_USB2UN.INF,UNINSTALL`,
which deletes `USBPORT.SYS`, `USBEHCI.SYS`, `USBHUB20.SYS` and `USB2.INF` and
nothing else), then SweetLow's stack from the transfer drive with
`rundll32 setupapi,InstallHinfSection DefaultInstall 132 D:\SWEETLOW\USB2.INF`,
then a relaunch of QEMU (this guest halts on a reboot instead of restarting).
Issue #1 had claimed the crash was a fault of the Windows 2000 usbport and
that an XP-sourced build was free of it.

Observed. On the relaunch the driver registered against the new port driver
(`USBPORT_GetHciMn=10000001`, packet 0x13C, status 0), the No Op self-test
passed, the boot-attached High-Speed mouse bound, and a hot-plugged High-Speed
`usb-storage` enumerated (SET_ADDRESS intercepted, bulk pair opened) and
mounted as `F:`. Then, from Device Manager: disable, re-enable, Remove, and
Refresh with a reinstall from the transfer drive. The trace for the disable is
`RH_ClearFeaturePortEnable` on both occupied ports with their devices
disowned, `DisableInterrupts`, `StopController(TRUE)`, eight ports unpowered,
`quiesce: halted, USBSTS=00000001`. The re-enable is a `StartController` on
the same extension with no `DriverEntry`, so Windows 98 kept the image
loaded. The Remove is the same stop; the reinstall is a fresh `DriverEntry`
and a start on a new extension with both devices re-bound. No bugcheck at any
step. Under NUSB's usbport the same guest, and Microsoft's own `usbehci.sys`
on the same guest, die at `0028:C00312EE` after `RH_DisableIrq` on the first
of those steps (task 8, the release notes' first limitation).

Proven: the crash is not a property of Windows 98 and not of this driver's
teardown; it follows the port driver. The driver's Windows 98 stop path, which
until now had only ever executed on Windows 2000, runs on Windows 98 under
this stack and leaves the controller halted with its ports unpowered.
Inferred: the fault is in the 5.00.2195 usbport's own PnP stop handling on
9x, since the two lineages differ there and nothing else in the sequence
changed. Unknown: what exactly the 2195 build does after `RH_DisableIrq`;
whether the fix is in the XP sources or in SweetLow's rebuild; bare-metal
behaviour; and the full device matrix under this stack. Measured the same
day: the stack idle-suspends without `DisableSelectiveSuspend` exactly as
NUSB's does. Two boots with no keep-alive pointer: value present, no
`SuspendController` in four idle minutes and a hot-plugged keyboard
addressed at once; value deleted, `SuspendController` once shortly after
start and a keyboard hot-plugged afterwards never seen (QEMU shows it at the
port with address 0, the driver's addressed count stays 0). The INF's global
value stays, for both lineages.

Also measured the same day, from a `post-nusb` clone with the stack swapped
and the driver installed from an INF stripped of its `usbd` and `usbhub`
lines: without `usbd.sys` the driver registers and starts but the USB 2.0
Root Hub sits at Code 2 and no root-hub callback ever arrives (his
`usbhub20.sys` imports `USBD.SYS` by name; `USBDSTUB.SYS` does not stand in,
and his INF does not install it); a hand copy of `usbd98.sys` to
`usbd.sys` and a reboot brings the hub and the mouse up. Without
`usbhub.sys` a two-interface `usb-audio` enumerates and is parented as
"Composite Device" by his `usbccgp.sys`, which his Full INF binds to
`USB\COMPOSITE`; on NUSB that device is Code 2 without `usbhub.sys`. With
`usbhub.sys` present as well (driver installed from the full package) his
usbccgp still won the composite, his INF being the newer of the two
claiming `USB\COMPOSITE`. So the package's `usbd98.sys` copy is needed
under both lineages and its `usbhub98.sys` copy only under NUSB's, and
inert under his. The audio device is the only two-interface class-0 device
QEMU offers, and Windows 98's `USBAUDIO.VXD` still faults at `+00002ED4`
once it streams (a boot-time arrival with a stored assignment does so at
once), so it is plugged once per guest and read before it plays.
`docs/contributing/build-and-test.md`, "The SweetLow stack", has the run.

Rule: a limitation attributed to "the Windows 98 USB stack" names a lineage,
not an operating system, and the release notes now say which. When a
third-party component is the suspect, the cheapest discriminating test is a
second implementation of the same interface, and the miniport ABI made that
a two-hour swap here.

Affected: `docs/using/release-notes.md` (the first known limitation and the
Requirements row), `README.md`, `docs/contributing/build-and-test.md` ("The
SweetLow stack"), `docs/usb-xhci-info/usbport-miniport-interface.md` section
5 (the fourth-lineage table), `docs/contributing/legal-provenance.md` section
4, `.github/ISSUE_TEMPLATE/bug_report.yml`, `scripts/vm-matrix/` (the
`2a-sweetlow` target and `-XferAdd`).

## QEMU 11.1.0-rc2 parks a Windows 98 boot in SeaBIOS's SMM handler when a USB mouse is attached to the xHCI at launch

Environment: the host's `C:\Program Files\qemu` QEMU, "11.0.92
(v11.1.0-rc2-12128-gc65ddfcd01)", installed 2026-08-03; the first
`prepare-image.ps1 -Boot` of the `2a-sweetlow` guest, 2026-09-02. Every
matrix log from the evening of 2026-08-11 through the 2026-08-30 post-release
runs records QEMU 11.0.0, which the harness's resolver had found as a scoop
package that is no longer installed, so this was the first prep boot on the
newer binary.

Observed: a black 720x400 frame, the debug console at 0 bytes after minutes,
the QEMU process burning CPU, and `info registers` showing `SMM=1`, `CS=a000`,
`CR0=00000010` and an EIP that never moves (`0x130`, `0x1cd`, `0xbd4a` across
runs). `system_reset` reproduces it inside four seconds. With SeaBIOS's own
log (`-debugcon file:... -global isa-debugcon.iobase=0x402`) the last lines
are "Booting from 0000:7c00" and "set VGA mode 13", IO.SYS's logo, before the
"pnp call arg1=5" a good boot prints next. The wizard the black screen was
first read as, and the "OneDrive touched the image" hang the harness warns
about, were both wrong: the monitor answered throughout.

Bisected with `-snapshot` boots of the same image, so nothing was written:
the disk alone boots to ring 3 in thirty seconds with `pc` or `pc,smm=off`;
plus `qemu-xhci` alone, boots; plus `qemu-xhci` and a `usb-mouse` attached at
launch, at either port count, wedges; the same with `smm=off`, boots; the
vvfat transfer drive with or without a subdirectory, the chardevs, the
debug console and the boot flags, all innocent; disk-less guests never wedge,
because nothing reaches the path. Proven: the trigger needs a boot-attached
USB HID device on the xHCI and a Windows 98 boot, and SMM off avoids it.
Inferred, not verified: SeaBIOS polls a boot-attached USB HID from 16-bit
context through its SMI-based 32-bit call path, and this QEMU's SMM handling
under TCG breaks under it. Unknown: whether 11.1.0 final has it, and whether
`smm=off` changes anything Windows 98 sees beyond the BIOS enabling ACPI
itself (the guest booted, bound devices and shut down normally with it).

Rule: `prepare-image.ps1 -Boot` attaches the keep-alive pointer before the
guest starts on every Windows 98 target, so every 2a prep boot on this QEMU
wedges the same way. A black frame with an empty debug console and `SMM=1` in
`info registers` is this, not the image and not the driver. The `2a-sweetlow`
target carries `Machine = 'pc,smm=off'`; the other targets keep `pc` until
they are re-run on this QEMU, or QEMU 11.0.0 is put back. Record the QEMU
version in every run header, as the matrix already does; it is what made the
change visible.

Affected: `scripts/vm-matrix/matrix.config.psd1` (the `2a-sweetlow` entry's
comment), `scripts/vm-matrix/prepare-image.ps1`.

## Task 14.1.7 - the review-method rules earned across the audit rounds, salvaged out of the cross-phase review record

These rules were carried in a consolidated cross-phase review record under
`docs/contributing/` that has since been deleted. That file was a summary of
closed review rounds whose remaining receipts were commit hashes in a history
the published repository does not have. Its charter sent reusable lessons here,
and that charter was checked rather than trusted before the file went: these
are the rules that had no copy anywhere else. They are review rules rather than
hardware rules, and each was paid for by a round that found the previous
round's fix.

The whole series had one shape. Ten consecutive rounds went over the Phase 12
audit, and in nine of them the finding with teeth was inside the previous
round's fix; the eighteen-round Phase 13 review had the same shape six times
over. A fix is a change and earns the same scrutiny as the text it replaced.
The commonest concrete form was a structure extended in one place and not in
the three that index it: a RUN box extended without its numbered step list,
its at-a-glance row and its stage-order row, which happened twice in two
stages.

- A classification that enumerates goes stale; refer to the transcription.
  Three rounds running hand-wrote the list of completion codes the restore's
  event drain may not discard, and each was found short by the round after. It
  stopped going stale when the list was deleted and the code asked the table
  the specification had been transcribed into.
- When a spec sentence names two things, check that the fix reached both, and
  a field the spec says is always written must be read on the failing branch
  too. One sentence opening "any command or transfer" was acted on for
  transfers only. One Slot ID the specification writes unconditionally was
  adopted only on Success, which had leaked a slot on every failing Enable Slot
  since Phase 6.
- When a fix closes a window, look for the other door into the same room.
  Three rounds landed in one tenancy window because the same unlock was reached
  once on a completion and once on a refused submit.
- A fix that removes state to make a hazard go away has to ask who else is
  reading it. If the answer can change while you are waiting, the fix is to
  say something true, not to unsay something stale. Erasing quiescence state
  at a re-entry stopped a successful Stop Endpoint cancelling the transfers it
  had been issued to cancel; moving the erasure to a point where no such
  command can be outstanding still lost debts armed while it was. What worked
  was setting a flag that states what the situation is.
- A discard that rests on a gate elsewhere has to read the gate, not its
  summary. "The save is declined while any endpoint queue is non-empty" is a
  count of software queue entries, and an abort empties that queue while the
  controller may still own the TRBs, so the gate can pass with hardware work
  live.
- When a code leaves a matrix, its properties go with it. Moving two
  completion codes out of a test matrix to give them a new escalation carried
  the escalation and left the matrix's four explicit properties asserted for
  neither code.
- Documenting a hole is a legitimate outcome for one round and an invitation
  to the next. One round established that the save gate could not see a Busy
  endpoint and wrote it down; the next closed it. Both beat a round that argues
  the hole away.
- Print the match, not the line, and grep the concept rather than one
  spelling. A grep truncated per line declared a claim unique in a file whose
  table rows run to thousands of characters, and the next round found it alive
  in one; the same mistake with `head -3` produced a commit message asserting
  that a trace line does not exist, when it does. A search for `buy-or-publish`
  declared no other site and missed the claim spelled `purchase-or-publish`.
- When a rewrite is more accurate, check what the old passage was carrying.
  Two regressions in one round were accuracy improvements that dropped
  operator guidance and a correction record.
- Where a document disagreed with the driver, the driver was right every time,
  across eighteen rounds without exception. The source is the authority on its
  own semantics; do not change driver code on the strength of a documentation
  finding.
- Markdown integrity needs four checks, not one: balanced `**`, balanced
  inline backticks, balanced fences, and no `*(` nested inside another `*(`.
  Two rounds were each broken by one the `**` check does not catch.
- A round that reports a family of sites is reporting a hypothesis, not a
  list. Two such families were checked and every member was already correct;
  acting on either unread would have caused damage, and one would have broken
  a whole batch's audio stage by narrowing a finding further than the round
  that made it had. Read the members before acting on the family.

A review loop does not claim to finish. No round of the eighteen came back
clean, and the last four found 6, 9, 8 and 4 defects. That series ended by
decision rather than by convergence, which is the honest reading of every
review loop this project has run, and it is why "reviewed" is never written
down here as "defect-free".

## Task 13-L.4 - a byte compare cannot show "the published binary is the bench binary", because a re-link moves the PE timestamp

The `0.0.0.6` cut had to say that the published binaries were the bench
binaries plus a version number. That was checked rather than assumed, from
the commit log (nothing had touched `src/` or `xhcisnap/` since the bench
session; the only source change was `DriverVer`, the four `.rc` fields and one
comment) and from the debug image being the same 82,811 bytes the machine ran.

A byte compare cannot say it and must not be offered as if it could. A re-link
moves the PE timestamp with no source change at all, which this project has
already been caught by once. Identity across a re-link is a source-tree claim
made from history and the gates, not a file-compare claim.

## Batch 13-T - when a later plan contradicts an earlier written argument, the plan owes the argument a reason

Phase 13's "Machine choice is a bootstrap constraint, not a silicon one"
paragraph named a machine with EHCI as the easiest Windows 2000 vehicle, and it
was written before the phase existed. This batch's first task was then written
to send Windows 2000 at the xHCI-only machines first, against that paragraph's
own argument and without engaging it. Both of those machines bugcheck in Setup,
the vehicle the paragraph asked for never became available to the project, and
the batch was removed unobserved. A note that said the Windows 2000 install
"is what finally makes an xHCI-only machine a candidate at all" had it
backwards. When a later plan contradicts an earlier written argument, the plan
owes the argument a reason, and this one never gave it.

## "No memory manager" was read as excluding `HIMEM.SYS`, and the qualifier may need it

Reported by the project owner: running `XHCIQUAL.EXE` on a real machine can
require `DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V` in `CONFIG.SYS`.

What is proven: that line makes the tool run where it otherwise may not. What
is inferred: the mechanism. `XHCIQUAL.EXE` embeds the DOS/32A extender as its
EXE stub and runs 32-bit flat (`xhciqual/qual.h`, `xhciqual/build.cmd`), so it
needs extended memory to move into, and HIMEM is what provides it as XMS. What
is unknown: the failing symptom, meaning what the screen said on the machine
that needed the line, and whether the plain `DEVICE=` form without `/M:1`
would have done. Whoever meets it next should write the screen down before
adding the line; that is the half that would let the next person recognise it.

The reusable rule is about the wording, not the switch. Every DOS-prep
instruction in this repository said "boot without EMM386 or any other memory
manager", and `HIMEM.SYS` reads as "any other memory manager" to someone
following it literally, so the instruction could talk a reader out of the one
driver the tool needs. HIMEM is not one of the things the rule excludes: it is
an XMS driver, it does not enter V86 mode and it pages nothing, and the
exclusion exists because the DMA buffers require identity-mapped conventional
memory. A prohibition stated by category will be applied to the wrong member
of the category unless the exception is named beside it.

Fixed in the six places the instruction appears: `xhciqual/HARDWARE-TESTING.md`
(the "Safety and preparation" step 1, which now carries the reasoning, and the
Intel 7/8-series machine note), `xhciqual/README.md`,
`docs/using/release-notes.md`, `docs/using/release-acceptance-test.md`,
and both shipped `readme.txt` templates in `scripts/package/make-release.ps1`.
The templates take effect at the next cut, so a directory cut before the change
keeps the wording it was cut with; a version directory is written once.

## Six method rules batch 13-R earned, salvaged out of the handoff

These were carried in a working handoff document for a week, and that file has
been deleted (batch 13-R closed, `0.0.0.5` cut). They are here because each
one was paid for with bench time, and because a rule living only in a working
document is a rule with an expiry date. Their evidence is
`docs/contributing/runs/run-13e.md` Findings L-V and
`docs/usb-xhci-info/usbport-miniport-abi.md`'s census of both shipping
`usbport.sys` builds.

- An instrument pays for itself the first time it disagrees with an inference.
  Five bench boots of candidate remedies produced three wrong attributions.
  One reading of PORTSC refuted the standing headline in a minute, and one
  reading of the note ring named the root cause. Observation outranks remedy,
  and the repair had to be held to the corollary as well: a working replug is
  not a result.
- An indicator measures what it measures, not what you want it to. A mouse LED
  reports enumeration, not VBus. Finding Q was a correct observation and a
  wrong inference, and nothing in the run sheet flagged the gap because the
  inference had never been written down as one. Write down what a reading is
  evidence of, next to the reading.
- Checking one clause of a compound predicate does not clear the predicate.
  `xhciRhAdmitted` has four; a review traced one (`XHCI_EXT_FLAG_RH_CLOSED`),
  was right about that flag, generalised to the gate, and thereby discarded
  the correct hypothesis. `ControllerFailed`, the fourth clause, is reached by
  a plug through the command timeout.
- On a fault triggered by a count of cycles, no "quick check first" on the
  port under test is free. It spends one of the cycles the experiment is made
  of. Fold controls into the run, or take them on another port.
- A negative about a binary is only worth stating if it was a census, and a
  census of one instruction pair is not a census of control flow. "usbport
  never restarts the controller" is worth acting on because every call site
  was enumerated through the one base register usbport uses. The same sentence
  produced by grepping for a likely address would have been a guess wearing an
  address. The first draft's automated closure, which followed address-shaped
  immediates as callback registrations, invented an edge from
  `test eax,20300h` (a flag mask that looks like a code address) and made a
  500 ms timer appear to reach a controller restart. An analysis that invents
  an edge will also drop one: say which relation was searched, and claim only
  what that relation covers.
- Read who calls a callback before designing around what it means. This driver
  spent the whole investigation describing `ResetController` as usbport
  escalating. usbport has never asked for one; the driver asks, and usbport's
  entire contribution is a DPC and a lock. One disassembly pass over the other
  direction of the interface would have found it at any point.

Operational, and it cost a screen: redirect a bench tool's output to a file
(`> C:\NAME.TXT`, then `more <`). On real silicon a PORTSC table scrolls off a
DOS box, and a screen you cannot scroll is a reading you did not take.

## When a finding overturns a NUMBER, grep for the number

Finding V measured the E460's health-poll period at 36-80 ms and withdrew
Finding U's inferred "~1 ms", along with every figure derived from it. The
corrections landed the same day in the newest sections, in
`implementation-invariants.md` and in the roadmap's Phase 13 record, and the
session recorded that every file repeating ~1 ms had been corrected.

A pre-cut audit the next day found the claim false, and found a second
"corrected everywhere" claim that was also false. The withdrawn figures were
still standing as uncorrected fact in this file, the one the agent guide
mandates reading first before debugging anything. The sweep had gone to the
files the session remembered writing, and those were the newest ones. The
older records that restate a number are the ones nobody has open.

Reusable rule: when a finding overturns a number or a headline, grep for the
number and the headline, not for the files you remember touching. `~1 ms`,
`3.7 ms`, `thirty seconds`, `never completes`: each is a string, and a string
is searchable in a way "the documents about the poll rate" is not. Treat
"corrected everywhere" as a claim that needs the grep pasted beside it. It was
written twice in this project and was wrong both times.

## A threshold counted in POLLS is a threshold in somebody else's units

The command-age backstop counts `CheckController` invocations, by design: the
miniport has no clock it may read at DISPATCH without a new import, and the
comment argued the error direction was safe: "usbport's timer is nominally
500 ms, so a host that polls more slowly makes this fire later, never sooner."

On the E460 `HealthPolls` advanced 971,359 in a session under an hour. At
500 ms that is 5.6 days. The machine polls faster, not slower, so the safety
argument is inverted. `run-13e.md` Finding V measured the period directly
against `PollClockMs`: 36-80 ms, 54.9 ms over the whole boot, `PollClockStalls`
0. So the 64-poll backstop stood at 2.3-5.1 s rather than the nominal 32 s,
and the commands it pre-empted were slower than roughly 2.3 s.

The consequences were invisible for three boots. 2.3-5.1 s is at or under
`XHCI_COMMAND_TIMEOUT_MS` = 5,000, the watchdog this backstop was sized to sit
12 s behind, so it pre-empted the 5,000 ms command watchdog on every boot.
That is what `CommandsTimedOut 0` across 76, 635 and 123 commands says, while
`CommandTimeoutArrivals` showed 633 of 635 watchdogs arriving. And the driver
escalated to a controller reset 33 times in 15 plug cycles.

The first reading of this dump got the period wrong in the other direction. It
divided 971,359 polls by a session nobody had timed and concluded the period
was at most ~3.7 ms and the backstop fired in under a quarter of a second.
Those numbers were derived, written down as measurements, and withdrawn when
the driver was made to report the duration beside the count. Do not divide by
a session length you did not record. If a number matters, make the driver
print it rather than reconstructing it.

Two things made the defect invisible. The units are only wrong by a factor
nobody measured, so every derived statement stayed internally consistent: "32 s
against 20 s of ladder" reads fine until you learn what a poll costs. And the
batch's own repair made each escalation survivable, so a defect that used to
kill a port now produced no symptom at all.

Reusable rule: a threshold whose units belong to another component is a
threshold you have not set. If the quantity is time, express it in time. Here
the objection that blocked that ("no clock at DISPATCH") had already been
false for months, because `XhciFrameNumber` is MFINDEX-derived, import-free
and sampled by that very poll. When a design note justifies a proxy unit,
re-check the justification before reusing it, not the arithmetic built on top
of it.

A corollary worth its own line: a repair that makes a defect survivable also
makes it silent. The operator's honest report of fifteen cycles was "it
enumerated almost immediately each time", through 33 controller resets. Read
the counters, not the port.

## A retry budget that is spent by SUCCESS is an expiry date, not a safety bound

The in-place controller recovery was capped at three attempts, so that a
controller which would not come back could not be reset forever. On the E460
the recovery then worked three times out of three, and the fourth incident, an
ordinary new fault retrying nothing, found the budget gone and left the port
dead. `ResetControllerCalls 4`, `RecoveryAttempts 3`, `RecoveryFailures 0`.

The cap's own comment said "a controller that will not come back after that is
left latched", which is a statement about consecutive failures. The code
counted attempts of any outcome. The prose was right and the predicate was
wrong, and they had been sitting next to each other since the day it was
written.

No host vector caught it, and the reason is instructive: every existing vector
made the recovery fail, because that is the interesting case to write a test
for. The case that breaks a lifetime cap is the one where the mechanism
succeeds more often than the cap allows. That reads like the boring path and
is where the defect lived.

Reusable rule: when a counter bounds retries, say out loud what resets it. If
nothing does, it is a lifetime quota on a mechanism that is supposed to be
available whenever it is needed. Write the test for the success path
repeating past the bound, not only for the failure path reaching it.

## Four hangs, a timer armed for each, and not one verdict - so count the ARRIVAL

Four Stop Endpoint commands hung on the E460, every one of them with a
5,000 ms `UsbPortRequestAsyncCallback` armed and `CommandTimerFailures` at 0.
`CommandsTimedOut` read 0, and `cmd.timeout` appeared nowhere in 7,532 bytes
of note ring. The entire recovery ladder was being carried by the health
poll's command-age backstop, which exists only for commands that were never
timed at all.

How long the hangs lasted was never measured. The backstop's nominal 32 s was
at first read as the observed hang; at the 36-80 ms poll period Finding V
measured, the 64-poll backstop fired at 2.3-5.1 s, so what the four hangs are
known to have exceeded is a couple of seconds. Nothing timed them more finely,
and nothing now can, because the repaired build does not wedge. A threshold's
nominal value is not an observation of what crossed it.

Two explanations fit and they need opposite investigations: the callback
never arrived, or it arrived long after the poll had moved the state on.
Every counter in that function is written after a branch has already decided
what the callback was for, so none of them could tell the two apart. The dump
was consistent with both.

Reusable rule: when a path has several outcomes and you may need to know it
was reached at all, count the entry as well as the outcomes. One increment
immediately after the validity bracket and before every decision turns an
unanswerable dump into a one-boot question. The counter this entry asked for,
`CommandTimeoutArrivals`, answered on the very next boot (633 of 635, then 123
of 123): every watchdog arrives, and the poll simply got there first. Put the
increment after the bracket, never before; counting an unvalidated caller is
a write into somebody else's memory.

## A failure path whose recovery step has no owner is not a recovery path

`xhciResetController` masked its interrupt enables, latched `ControllerFailed`,
and said in its own trace text that recovery was "a stop/start". Nothing in
the driver, and nothing in either shipping `usbport.sys`, ever performed one.
The sentence read like a design because it named a real mechanism and pointed
at the right layer; it was in fact a `TODO` that had lost its marker. The cost
was Finding 3, a root port that goes permanently deaf until a cold boot, on
two machines, found by a bench trip that was booked for something else.

What made it look survivable for months is that every individual statement in
the comment was true: the callback really does run at DISPATCH inside
usbport's spin lock, `UsbPortWait` really is `KeDelayExecutionThread`, and a
stop/start really is the recovery. The false part was the unstated one, that
somebody would send it.

Reusable rule: when a failure path names the action that recovers it, name the
context that performs that action in the same breath. If there is no such
context, the path is not finished. Grep the rest of a driver for that shape,
"recovery is X" with no caller of X.

A corollary about negatives in somebody else's binary: "usbport never restarts
a controller by itself" was worth acting on only because it was a census,
every slot call in both images enumerated through the one base register
usbport reaches the miniport through, rather than a search that happened to
find nothing.

## A backstop that skips to the end of the ladder is not a backstop

The health poll's command-age detector exists to catch a command whose
watchdog was never armed. `UsbPortRequestAsyncCallback` answers 0 on success
and 0 on its own pool-allocation failure, so that state is invisible to any
return value. The detector's threshold was carefully sized to clear the whole
legitimate watchdog ladder, and then, on firing, it asked for a controller
reset, the thing that ladder ends in rather than the thing it starts with.

The wedged E460 is what that reads like from the counters: `CommandAgeResets
1` with `CommandsTimedOut`, `CommandAbortWaits` and `CommandAbortsNotWritten`
all 0. One Stop Endpoint that would not complete took the whole controller out
of service, and `CRCR.CA`, the specification's own first remedy and three
lines of code away, was never written once.

Reusable rule: a detector that fires because a mechanism did not run should
run that mechanism, not skip past it. The premise "we only get here when the
ladder never ran" is an argument for entering the ladder at rung 1.

A reading rule goes with it: an offset table is a subset, and the field you
need is the one it does not carry. Three of the six counters above were absent
from `scripts/local/offsets.c`, so the first reading of this dump named the
wrong defect. Adding them cost one recompile.

## The IRQL a sequence needs is a property of the services it calls, not of the sequence

Reusing `XhciInitController` from a DISPATCH-level DPC needed three things
changed, and none of them was in the initialization logic: the bounded waits
(`KeDelayExecutionThread`), the PCI configuration-space reads (they go out to
the bus driver), and `XhciFailClosedDma` (whose bugcheck is justified by a
reclamation that this path does not perform). Everything else, HCRST, the
register programming, the port-power pass and the root-hub seed, was already
legal at any IRQL and had simply inherited a PASSIVE_LEVEL tag from its only
caller.

Reusable rule: before concluding a sequence "needs PASSIVE", enumerate the
services in it and check each one, rather than reading the tag on the
function. Where the answer is a per-call-site difference, make it one word of
state with a named contract (`ext->InitBelowPassive`) that every affected site
reads, so the rule lives in one place instead of in a fork of the sequence.
State what skipping a service costs at the site that skips it. Here the
identification, INTx and bus-master gates are not re-run, which is defensible
only because the same controller answered them at the start this is recovering
from.

## A staging directory goes stale silently, and the size check that "verifies" the deploy confirms the wrong file

Environment: modern build host, QEMU 11.0.0. Deploying the current debug build
to the 2b guest (`vm\win2k.img`, Windows 2000 SP4) so `Assert-OffsetsFresh`
would stop refusing runs on it.

The narrow operation: pick a binary, stage it, copy it in, verify.

Before anything was copied, three different binaries on this host turned out
to be 155,227 bytes each:

```
a279144...  the cut 0.0.0.4 debug package, out\pkg-debug\ and its upload set
4f47480...  src\objchk\i386\         the tree's own build
1b93d91...  vm\xfer\CHECKED\, vm\xfer98\CHECKED\   staged earlier, STALE
```

`CHECKED\` is the directory `stage-driver.ps1` maintains and whose whole
purpose is "the driver under its REAL name, so the in-guest deploy is one
command with no rename". It had been left holding a build from a previous
session. A session copying `D:\CHECKED\XHCI98.SYS` in good faith would have
deployed the stale binary and then verified it by a size that matched. On this
target the verification that matters is a number the running driver prints,
so the error would have surfaced one boot later as a fresh mystery rather than
as a copy of the wrong file.

### What is proven, and what it cost

Proven, statically: the three hashes above, and that `src\objchk\i386\xhci98.sys`
differs from the `0.0.0.4` debug binary in exactly 18 bytes, all of
them build-time stamps: five 2-byte header/debug-directory sites, and the ASCII
build string, which reads `Aug 23 2026 12:27:20` in one and
`Aug 23 2026 08:59:51` in the other (`cmp -l`, then decoded). The only source
change since that release is in `src\xhci_dbg.c` and lies wholly inside
`#ifdef XHCI_DBG_E9_NOEXEC` / `#error` guards, so with no `XHCI_EXTRA_DEFINES`
the preprocessed input is identical and the two builds are the same binary.

Cost: none, because it was caught. That is the only reason this is a lesson
and not an incident.

### The reusable rules

- Verify a staged binary by hash before deploying it, never by size and never
  by the directory's name. `CHECKED\` means "this is where the checked build
  goes", not "this is the current checked build". A staging directory is a
  cache, and nothing in this repository invalidates it.
- Re-stage as part of the deploy, not as a thing done earlier. The copy into
  `CHECKED\` and the copy out of it should be one operation with one hash
  printed between them. Two operations separated by a day is what produced
  this.
- Prefer the published, tracked artifact over an object-directory build when
  deploying to a vehicle, once they are shown to be the same binary. A reading
  pinned to `releases\<ver>\<flavour>\` can be re-fetched and re-hashed by a
  later reader; one pinned to `src\objchk\` cannot, and `objchk` is
  overwritten by the next build of anything.
- A matching number is not a working vehicle. After the swap the guest
  reported the expected `MiniPortExtensionSize`, which says only that two
  values agree. What closes a vehicle item is a run: the `hid` group was
  re-run and completed (3 PASS / 3 NODRIVER, against the Phase 10 2b
  baseline's 2 PASS / 4 NODRIVER), which says the harness measured a guest it
  had previously refused.

### Two operational facts measured on the way

- The vvfat transfer volume is `E:` on 2b, not `D:`. `D:` is the CD-ROM and
  answers `The device is not ready`. The volume label is `QEMU VVFAT`. Every
  committed note that names `D:\CHECKED` was written for a different guest.
- `ren` over a running driver image succeeds on Windows 2000. NT permits
  renaming a file that has an open image section although it forbids
  overwriting one. The `ren`+`copy` swap route is recorded in
  `build-and-test.md` as a Win98 rule, adopted there because an INF
  update-over-install bugchecks at `0028:C00312EE`; it turns out to serve both
  targets, for an unrelated reason, so no INF install is needed on 2b either.

### What remains unknown

Whether any earlier reading was taken from a `CHECKED\` directory that had
gone stale the same way. The offset gate would have caught it on any target
whose driver prints `MiniPortExtensionSize` and whose run reads counters,
which is every matrix and soak run, so the exposure is limited to runs that
read no counter. Not audited.

### Affected

`scripts\local\stage-driver.ps1`,
`docs/contributing/build-and-test.md` (the Driver File Details note),
the Windows 2000 bench run sheet's Q2b (deleted with the batch and the vehicle
it was written for).

## Finding 3 went from unreproducible to a five-event recipe, and the two NEGATIVE runs are what made it a measurement

Environment: ThinkPad E460, Windows 98 SE, NUSB 3.3, `xhci98.sys` 0.0.0.4
release flavour, `usbhub.sys` present. Second bench session of the day. No
trace channel, so every reading here is behavioural.

The narrow operation: stages E3 and E4 of batch 13-E. The investigation below
was not planned and displaced the rest of the sheet.

Result: a defect that had resisted two sessions was reduced to a single named
variable in an afternoon, with no instrumentation. Finding 3, the most serious
thing outstanding and the only one with no mechanism, is now reproducible in
five plugs.

### What was found

The trigger, from four conditions at one root port, with device identity,
plug count and the port held constant:

| Sequence at position D | Events | Result |
|---|---|---|
| one High-Speed drive, plugged and unplugged repeatedly | 30 | clean |
| two different High-Speed drives, alternating | 16 | clean |
| High-Speed -> Low-Speed mouse -> High-Speed | 5 | WEDGE |
| High-Speed -> Full-Speed C-Media -> High-Speed | 5 | WEDGE |

The shape: a sub-High-Speed device disconnecting from a root port leaves state
behind. Teardown looks clean (the devnode disappears, the tree is intact,
port-change events are still arriving) and the next enumeration on that port
fails and wedges the controller machine-wide, recoverable only by cold boot. A
pause between the unplug and the next plug does not prevent it, so it is not a
race.

### The rules worth keeping

- A reproduction is an anecdote until a negative sits beside it. The wedge on
  the fifth event means nothing on its own; devices fail sometimes. It became
  a measurement only because thirty events with one drive and sixteen with two
  different drives had already come back clean on the same port. Budget bench
  time for the runs you expect to be boring; they are what the positive is
  read against.
- Do not remove two variables at once when isolating. The first counted
  protocol took away both the hub and the device variety, got a clean fifteen
  cycles, and could not say which removal mattered. Adding back one, variety,
  wedged it on the first alternation. The flawed step cost one boot and is the
  reason the next one was decisive.
- "It needs X" is often a description of how it was reached, not of what it
  requires. Finding 3 was written up for two sessions as needing isochronous
  audio playing, and its only named mechanism, a UP interrupt livelock, rested
  entirely on an isochronous endpoint at `bInterval`=1 posting an event per
  frame. It reproduces with no audio, no load and no traffic at all. Nothing
  was wrong with the earlier observations; the generalisation drawn from them
  named the vehicle instead of the fault. Two sightings of a fault under one
  condition are not evidence that the condition is necessary.
- Ask when it breaks, not only what breaks it. Three plugs separated
  "teardown is broken" from "teardown is clean and the next enumeration reads
  what it left", two different functions and two different fixes, with no
  instrumentation whatsoever. The cheap version of that question is: stop one
  step early and look.
- A reading taken after a stall is void, not negative. Stage E4.3 recorded
  "the USB 3.0 drive does not appear at D", which is one of the outcomes the
  sheet predicts and would have been published as a finding. A known-good
  drive did not appear either; the controller was already wedged. The
  known-good device between steps is not ceremony. It is what separates a
  result from an artefact, and it caught a false one here.

### What remains unknown

Whether this is the same fault as bench session 1's audio observation. The
terminal signature matches; the route in does not. That case had an
isochronous stream dying and no device swap, this one a device swap and no
stream. One fault with two routes, or two faults with one end state.

The mechanism. What the enumeration path reads that the previous device wrote
(slot, address, or per-port context) is inference from the shape and has not
been measured. This machine cannot measure it. A Windows 2000 machine on real
silicon with the counter block could have, and none was ever available to
this project, so it stays unmeasured. The stimulus is five plugs rather than a
recording session, so it can be applied on purpose while something watches
the slot, address and port-change counters.

### Affected

`docs/contributing/runs/run-13e.md`, "Session record - bench session 2"
(findings D to J).

## A tool's first real use found it wrong twice, and both defects produced output that looked like an answer

Environment: host side only, the modern Windows development host. No guest,
no bench, no target OS. `scripts\hub-characterise.ps1 -Walk`, written the same
day and never yet run against a hub from the bag. Branch `phase-13`.

The narrow operation: take the three hub socket maps stage E3 was blocked on.
Walk one flash drive down each hub's sockets in physical order and read the
logical port each arrival lands on.

Result: all three maps were taken, and the mode was wrong twice. Both defects
were in the same place, deciding what counts as an arrival, and neither
announced itself. Each produced a summary in the right shape, with the right
headings, that a reader would have transcribed.

### The two defects

One: the enumeration transient was counted as a socket. A plug is seen as
empty -> `** DeviceEnumerating **` -> the settled identity. The arrival test
was `if ($now)`, any non-empty signature, so a socket was numbered twice. The
first walk produced twelve numbers for seven sockets, and not uniformly:
whether the transient is caught at all depends on a poll landing inside it, so
five sockets doubled and two did not. An arrival is now strictly the empty ->
occupied edge; a change between two occupied signatures is a settle, printed
without a number, which amends the arrival it belongs to so the summary names
the device instead of the transient.

Two: a failed IOCTL was counted as a socket. `Get-PortSignature` gives a
failed IOCTL its own signature rather than folding it into "empty", and the
comment explaining why is correct: otherwise a hub unplugged mid-walk reads as
every child leaving. But that signature is non-empty, so it satisfied the
fixed rule too. Pulling the 4-port hub out to swap in the next one appended
four phantom arrivals (`?ioctl-failed(2)`, `ERROR_FILE_NOT_FOUND`). A failure
is now a third state: printed once per port per episode, never reaching the
transition logic, and `$state` keeps the last real signature across the
outage.

### What is proven, and what saved the first reading

The maps are proven, and by two passes each rather than one. Every hub was
walked again after the fix, and every re-walk reproduced its first map
exactly.

The first walk's map was recoverable only by accident. It was readable because
that hub's ports run in a monotone sequence, so a human could see which
numbers were doubled. On a hub with scrambled numbering the same transcript
would have been unreadable and nothing in it would have said so. That is the
property worth naming: the defect did not degrade the output into something
obviously broken, it degraded it into something plausible.

A third walk failed the count for an innocent reason, which is what turned the
check into code. The drive was left in the first socket when the walk began,
so that socket was listed as pre-occupied rather than numbered: six arrivals
for seven sockets, off by one, with the missing one recoverable only by
reading the occupancy listing and the first departure together. That is
hand-reconstruction, which is what the mode exists to remove.

### The reusable rules

- The first real use of a tool is its first test, and it needs an invariant
  to be tested against. Not "does it print something"; the broken version
  printed plenty. Here the invariant is arithmetic and belongs to the
  operator's own actions: every watched port appears exactly once. The
  summary now computes and prints it, naming each gap with its reason
  (occupied at start, never walked into, or arrived twice), so a bad run says
  so on its own instead of waiting to be noticed by whoever transcribes it.
- An untested branch that emits well-formed output is worse than one that
  crashes, and should be treated as the more urgent kind of untested. A crash
  is self-reporting; a plausible wrong answer is a measurement that enters the
  record.
- "Not empty" is not "a device". Both defects are the same error: a signature
  that encodes a state (mid-enumeration, unreadable) was read as the presence
  of a thing. Where a reading has more than two outcomes, enumerate them
  rather than testing for truthiness.
- A tool that watches hardware needs a rehearsal that moves hardware. The
  previous day's session recorded that a probe of an instrument must run the
  binary that uses the instrument; this is the same rule for a host script.
  The branches that had been exercised (selection, port counts, the summary)
  were the ones that need no plug, and they were exercised because they were
  the ones testable without one.

### What remains unknown

Nothing about the maps. Three units of two makes all number backwards from
the cable end. That is now a pattern to expect and is not established as a
property of hubs; walking a new unit is cheaper than assuming it, and the walk
is what would catch the exception.

### Affected

`scripts\hub-characterise.ps1` (the walk mode's transition logic and the new
coverage check); `docs/contributing/build-and-test.md`, "A hub socket is not a
hub port, and the two must be mapped" (the three maps, the procedure, and the
count check); `docs/contributing/runs/run-13e.md`, "Two socket maps that do
not exist, and were needed twice" (closed).

## A stale-table alarm was raised by counting commits to the file, and the struct it was about had not moved at all

Environment: host side only, no guest and no bench. Branch `phase-13`.
`scripts\local\regen-offsets.cmd` and `scripts\vm-matrix\gen-offsets.ps1`.

The narrow operation: the handoff document carried `MiniPortExtensionSize`
drift as its single most necessary action, escalated from a housekeeping
bullet on the grounds that both offset tables were older than `src/xhci.h`,
which "has changed ten times since". The stated consequence was retroactive:
the tracked table holds the `SIZEOF` every Win2000 launcher cross-checks the
trace's `MiniPortExtensionSize` against, so a moved layout would have silently
misaligned every counter read taken since.

Result: both tables regenerated byte-identical. `SIZEOF` 87592, 374 counter
rows, every offset unchanged; `git diff` over `scripts\vm-matrix\` empty.
`gen-offsets.ps1` derived 374 fields from 18 source files, and its
`-AllowRemovals` guard, which refuses a regeneration that drops a row because
the usual cause is a print site that stopped being matchable, refused
nothing. No reading needs re-checking, and stage T1's gate value is current.

### What is proven, and what the alarm rested on

The alarm rested on a proxy. `src/xhci.h` is ~4,000 lines and mostly prose;
commits to it are a poor stand-in for changes to `XHCI_EXTENSION`. Extracting
the struct at both ends of the range and diffing its non-comment lines gives
446 against 446, identical. Every difference in the eight commits (not ten)
is inside a comment, including both of the ones the bullet named. One of those
commits had said so in its own message ("no change to `XHCI_EXTENSION`, so
SIZEOF has not moved, no offset table needs regenerating, and stage T1's gate
value is untouched"), and the cross-phase review record had logged
`SIZEOF 87592` as current in the round before the bullet was promoted.

Two of that range's commits are worth naming as near misses that were not
misses. One added two `XHCI_DBG_VALUE_CHANGED` sites whose labels are wrapped
across two lines, the exact shape `gen-offsets.ps1` documents as its one
silent loss, but both print a composed word (`XHCI_HUBMARK_TRACE_WORD`,
`XHCI_TT_TRACE_WORD`) rather than an `ext->` field, so neither is addressable
by a single offset and neither belongs in the table. Another fixed a wrapped
label introduced earlier in the same range; the identical row count is what
says the pair netted out.

### The reusable rules

Check the thing, not the file that holds it. "Is the offset table stale" is a
question about `XHCI_EXTENSION`'s declarations, and it is answerable in two
commands:

```
EXT='/^typedef struct _XHCI_EXTENSION/,/^} XHCI_EXTENSION/'
git show <ref>:src/xhci.h | awk "$EXT" | grep -v '^ *[*/]' > old.txt
awk "$EXT" src/xhci.h        | grep -v '^ *[*/]' > now.txt
diff old.txt now.txt && echo 'DECLARATIONS IDENTICAL'
```

That is cheaper than the regeneration and it answers a question the
regeneration does not: whether the layout moved, as opposed to what it is now.
Both directions of the proxy are wrong. A comment-only churn raises a false
alarm, and one field added in a quiet week raises none at all.

Escalating an item is a claim, and it carries the same evidence burden as a
finding. This bullet went from housekeeping to "the most necessary action in
this file", with a retroactive consequence attached, on a commit count. The
promotion cost nothing here because the remedy was minutes and the answer was
benign, but the same reasoning writes "readings since <date> need re-checking"
into a handoff on no evidence, and a later session pays that down as fact.

A regeneration that changes nothing is a result, not a wasted step. Record the
identity, because the next session will otherwise re-derive the same alarm
from the same commit count. What it does not establish is unchanged and is
worth restating whenever the number is quoted: the table is a subset rather
than a mirror of the counter block, so freshness is not completeness; and a
host build's layout is only the deployed layout when the trace's own
`MiniPortExtensionSize` says so at the target.

Affected: `scripts\local\offsets.txt`, `scripts\vm-matrix\offsets.txt`,
`scripts\vm-matrix\offsets.labels.txt` (all regenerated, all unchanged);
`docs/contributing/build-and-test.md` "Reading counters out of a live guest".

## A probe of an instrument needs the binary that uses the instrument, and the debug flavour would not load on the metal that needed it

Environment: ThinkPad E460 (Skylake, xHCI-only), Windows 98 SE reinstalled
from scratch for bench session 1, NUSB 3.3, `xhci98.sys` 0.0.0.4. DebugView
v4.64 with Capture Kernel. Devices: Logitech `046D:C077` Low-Speed mouse, a
USB flash drive.

The narrow operation: settle whether `run-13e.md`'s DebugView ban is general.
It rested on a hub and one USB audio device, one observation each, and the
sheet recorded "the working assumption is that any device plug will do it",
an assumption that four documents then leaned on.

### Three things were established and they are of different kinds

1. The ban is general (measurement). A Low-Speed HID mouse at `bInterval`=10
   on a root port raised `fatal exception 0E at 0028:C20A3F4D`. Three device
   classes, three crashes, and this one is the cheapest stimulus the bench can
   offer. `build-and-test.md`'s explicit caveat, "the generalisation is an
   inference from them and is not established", is discharged.

2. The published debug flavour does not load on this machine (confirmed by
   remedy). Code 2 on the controller devnode, no crash, nothing loaded. The
   sole import difference between the two 0.0.0.4 binaries is
   `HAL.dll!WRITE_PORT_UCHAR` (DUMPBIN on both); a rebuild with
   `-DXHCI_DBG_NO_E9` loads clean, though not "otherwise identical", since any
   nonempty `XHCI_EXTRA_DEFINES` also gives it the do-not-deploy marker, which
   run-13e.md P6 names as the uncontrolled variable. Why is not established:
   either the import does not resolve there, or port `0xE9` is decoded on that
   chipset and the write faults during init, which would refute a comment in
   `src/xhci_dbg.c` that has never been measured on metal.

3. The instrument could not have served the target anyway (design). The
   debug build has no steady-state trace. Every per-transfer site is budgeted
   to 32 prints and spent within seconds of load; the budgets are driver-image
   statics no start, stop or resume resets.

### The reusable rules

A probe of an instrument must run the binary that uses the instrument. The
handoff proposed one boot with the machine as it stood. The machine ran the
standard build, whose trace channel is entirely inside `#if DBG`, so it makes
no live `DbgPrint` call and the hypothesised mechanism is never entered. A
pass there would have been a pass for a driver that emits nothing, and it
would have been written down as evidence. Before testing whether a channel is
safe, check that the binary under test can use the channel at all.

Read the before-state before you change anything, even when the change is
trivially reversible. Device Manager was first read after the binary swap, so
"Code 2 caused by the new binary" and "Code 2 that was already there" were
indistinguishable. One file copy and one boot restored the previous binary and
split them. This is the same failure `usbhub.sys` taught in the other
direction: that answer was sitting in stage E1.0's pre-install `dir` and was
read as "baseline is clean".

A null reading from a budgeted trace is unreadable, not negative. An attempt
to test steady state (attach the device, then start the capture) survived and
printed nothing. That is what the budget model predicts, because attaching
first guarantees the one dense burst happens before anything is listening. It
is not evidence of safety. The 7b-M run already paid for this once: "nothing
appears in the log" was unreadable until a positive control existed.

Swap a Win98 driver binary by rename, never by INF. Both flavours ship a
byte-identical `xhci98.inf`, so only `xhci98.sys` changes; `ren` + `copy` from
a DOS box leaves the devnode and its driver key untouched. Every
update-over-an-existing-install on Win98 bugchecks at `0028:C00312EE`, because
stopping the running driver is that fault.

Two binaries of the same build can be the same size. The 0.0.0.3 bench binary
and the 0.0.0.4 standard release are both 81,899 bytes; the two debug builds
are both 155,227. `dir` cannot tell them apart. Driver File Details is the
witness, and a restore verified by size verifies nothing.

### What remains unknown

- Whether finding 2 is an unresolved import or a decoded `0xE9`. A clean Code 2
  favours the former and does not settle it.
- Whether the crash tracks the enumeration burst rather than sustained
  interrupt rate. A mouse has no high steady-state rate and crashed at the
  plug, and 60 s of capture on a quiet bus was survivable: consistent, not
  established.
- Whether `ntkern-name` is a sound evidence tier. `ZwCreateFile` and
  `ZwWriteFile` rest on it alone and do resolve on this machine, so it is
  unreliable rather than wrong.

### Affected

`docs/contributing/runs/run-13e.md` ("Session record - the DebugView ban,
measured", and the DebugView bullet under "If something goes wrong"),
`docs/contributing/build-and-test.md` (the DebugView box),
`scripts/import-gate/xhci98-imports.allow` (the `WRITE_PORT_UCHAR` row),
`src/xhci_dbg.c` and `src/xhci_dbg.h` (the `0xE9` comment; the print
budgets), roadmap task 12.2 (strengthened, does not reopen).

## Windows 98's composite parent is `usbhub.sys`, and an xHCI-only machine never gets it, so every multi-function USB device on one is dead

Environment: ThinkPad E460 (Skylake, xHCI-only), Windows 98 SE reinstalled
from scratch for bench session 1, NUSB 3.3, `xhci98.sys` 0.0.0.3 release.
Control: ThinkPad X61 (ICH8M, UHCI + EHCI, no xHCI), same Windows 98 SE, same
NUSB 3.3. Reproduced afterwards in the 2a QEMU guest.

Symptom: every multi-interface device stops at `USB Composite Device` with
`Code 2`, "The NTKERN.VXD device loader(s) for this device could not load the
device driver." Four devices, three vendors, three speeds: a Full-Speed USB
Audio composite, a High-Speed one, and a plain Low-Speed two-interface HID
keyboard that would not type. The device is enumerated and named; nothing
beneath it binds.

What is proven: the file `usbhub.sys` is Windows 98's composite parent driver,
it was absent on the E460, and supplying it fixes every one of those devices.
Three independent lines:

- The X61 runs the same OS and the same stack and drives all of them. Its
  `USB Composite Device` devnode -> Driver -> Driver File Details names
  `usbhub.sys`, which is what identified the file.
- Stage E1.0's pre-install check on the E460 had already recorded the file as
  absent, alongside `usbd.sys`, `uhcd.sys` and `usbport.sys`, before anything
  was installed. The evidence was in hand a day before it was read.
- Copying the file in by hand and cold starting makes the E460's Device
  Manager read the same as the X61's: the keyboard types, the audio device
  works.

Why it happens is a shape this project has met before. Windows 98 ships
`USB.INF` on every install but copies the USB driver files only when Setup
detects a USB controller it recognises. An xHCI-only machine looks empty to
Win98 Setup, so the INF is present, the devnode gets named, and the file it
points at is not on disk. That is `Code 2`, matched and unloadable, as against
`Code 1`'s unmatched. It is the same absent-dependency shape as `usbd.sys`
(the `usbhub20.sys` / `USBD.SYS` entry below), which this package already
carried for the same reason.

Confirmed by controlled comparison, not by symptom match. The 2a guest was the
wrong vehicle as it stood: batch 9-V had installed the SE CD's USB components
there, so composites bound and always had, the same confound the X61 has.
Renaming `USBHUB.SYS` to `.BAK` and rebooting turns QEMU's own `usb-audio`, a
device Phase 9 had already measured binding on that guest for an unrelated
purpose, into `Code 2`. Installing the package puts the file back and it binds
again:

```
USBHUB20 SYS    50,032   01-16-04    NUSB's 2.0 hub
USBHUB   SYS    35,680   08-23-26    placed by this package's INF
USBHUB   BAK    35,680   04-23-99    the renamed original, untouched
```

The driver was never involved, and the same run proves it: `devices addressed
= 1`, `EP0 max packet size corrections = 1`, configuration descriptor read.
Everything below the composite parent worked throughout.

What is inferred rather than proven: that Windows 2000 needs no equivalent.
Its composite parent is a different file (`usbccgp.sys`), and Setup copies
`%SystemRoot%\Driver Cache\i386\driver.cab` unconditionally, so PnP can pull
it with no media, a mechanism Windows 98 has no guaranteed equivalent of. This
is structural reasoning and has not been observed. Every Windows 2000 vehicle
in this project has an EHCI, which is the same confound that hid the Windows
98 gap, and Windows 2000 Setup bugchecks on both xHCI-only machines here, so
it can never be observed either. It is published as a limitation rather than
carried as a check.

Rules this earns:

- On an xHCI-only machine, assume no USB file Windows ships is present.
  Windows placed none of them, because it never saw a controller. `usbd.sys`
  taught this earlier and it was read as a fact about `usbd.sys` rather than
  as a class of problem. It is a class of problem.
- A control that holds the OS and the stack constant is worth more than any
  amount of reasoning about the OS and the stack. The X61 owns no clause in
  any phase and was never in the plan; it settled in one reading what two
  days of inference had not.
- `Code 2` means matched-and-unloadable and should be read as "which file is
  missing?", not as "which driver is wrong?" The dialog names the loader, not
  the file, so the file has to come from a working machine's Driver File
  Details.
- A pre-install inventory is evidence, not ceremony. Stage E1.0's four `dir`
  commands contained the answer and were filed as "baseline is clean".

Affected: `src/xhci98.inf` (`[Xhci.CopyW98]`, Win98 path only),
`scripts/package/usbd-sources.expected`, `scripts/inf-gate/check-inf.ps1`
(`W98-MISSING` / `W98-ONWIN2K` enforce the asymmetry in both directions),
`docs/contributing/legal-provenance.md` section 5,
`docs/contributing/runs/run-13e.md` ("Session record - bench session 1"),
release 0.0.0.4.

## Windows 98 SE boots a 2020 Comet Lake ThinkPad, so the "2012-2018" bracket is a shopping heuristic and not a bound

Environment: ThinkPad P14s Gen 1 (Intel Comet Lake-LP, xHCI `8086:02ED` rev
00, subsystem `17AA:22B1`), reported by the project owner. Not observed here.

What is reported: Windows 98 SE is installed on that machine. Nothing else:
not whether NUSB 3.3 is on it, not whether `xhci98.sys` has ever been bound
there, not the BIOS/CSM and storage settings that made it work. The working
configuration lives in the owner's `retro-configs` repository, which is where
the per-machine setup records for this fleet have always lived; do not
reconstruct it from this entry.

What it corrects in this repository: two documents asserted the opposite by
inference rather than by measurement, and both are now fixed.

- `docs/contributing/build-and-test.md` "Available Test Hardware" told the
  reader to confirm Win98 bootability (CSM + a non-NVMe storage path) against
  the retro-configs records before counting on it. Confirmed, by installing
  it.
- The same file's "The Win98-bootable window bounds the realistic laptop set"
  brackets the target at 2012-2018 Intel laptops. A 2020 machine is outside
  that on date and boots regardless.

The reusable rule: the bracket's three bullets (legacy BIOS/CSM, a non-NVMe
storage path, a RAM workaround) are the properties that decide it, and a year
is a proxy for them, not a test of them. Check the properties per machine.
The bracket is still the right advice for buying a machine sight unseen,
which is what that section is for; it was never evidence about a machine
already in the fleet.

What it does not change: the fleet's xHCI coverage argument is unmoved. The
P14s is a third clean Intel controller beside the E460's, so it confirms
rather than extends, and no quirky Intel silicon is in the fleet at all. It
says nothing about Windows 2000, which bugchecks in Setup on this machine; the
era wall recorded there stands, and this is not a reason to retry it.

Where the consequence is owned: roadmap batch 13-E's heading and
`docs/contributing/runs/run-13e.md`'s closing section. The machine owns no
clause today, and giving it one is a project-owner decision.

## A counter label that is legal C and correct on screen can still be invisible to the tool that makes it readable

Environment: host-side only, no guest. `scripts\vm-matrix\gen-offsets.ps1`
against `src\*.c`; found by a review, reproduced directly by applying the
generator's own regular expression read-only.

The symptom: a review widened one counter's DebugView label. The new text did
not fit on one line, so it went out the ordinary way:

```c
XHCI_DBG_VALUE_CHANGED("device commands answered or refused for a "
                       "re-enumerated tenancy", ext->CommandsStaleTenancy);
```

That compiles, and DebugView prints the intended string, because C
concatenates adjacent string literals. But `gen-offsets.ps1`, which derives
the counter offset table the matrix harness reads a live guest with, matches
one quoted string followed by the comma. The field simply stopped existing as
far as the derivation was concerned: 373 fields derived against a checked-in
table of 374, `CommandsStaleTenancy` the only one missing.

What is proven: applying the generator's exact pattern to the tree yields 373;
restoring the label to a single literal yields 374. The five other split-label
print sites in `src\*.c` were already excluded and correctly so. They print
locals, function results or array-indexed fields, none of which is addressable
by one offset.

Why nothing caught it is the part worth keeping. The next driver change would
have regenerated both tables without the row, and every existing check would
still have passed, because a missing row does not change `SIZEOF`.
`Assert-OffsetsFresh`, the trap that exists specifically to catch a stale
offset table, compares `SIZEOF` against the `MiniPortExtensionSize` the
running driver prints, and both would have agreed. The loss surfaces only much
later, as a counter no expectation can name, and by then the change that
caused it is many commits back. It was additionally hidden because the same
review hand-edited `offsets.labels.txt`, a generated file that says
"Generated. Do not edit" in its own directory's README, so the label read
correctly in the file because it had been written there by hand rather than
derived.

The reusable rules:

- A derivation's parser is part of its contract. When a script derives
  something from source text, the source is no longer free to be written any
  way the compiler accepts. Write the constraint at the site the author will
  be standing at (there is now a comment beside this print site saying the
  label may not be wrapped) and not only in the script.
- Make the losing direction refuse. A table that grows is ordinary; a table
  that shrinks is this failure mode and essentially nothing else.
  `gen-offsets.ps1` now compares the derived field set against the table
  already on disk and throws, naming the field and the likely cause, unless
  `-AllowRemovals` says the retirement was intended. It runs before the
  compile so the existing table survives the throw. Watched failing once:
  re-splitting the label produces "derived 373 counter fields" and "1
  field(s) ... would be dropped: CommandsStaleTenancy", with both tables
  untouched.
- This is the third instance of one shape in one week: the `^0 checks` runner
  gate that could never match `test_ctx`'s prefixed verdict line, the bare-CR
  FILE-EOL rule with no mutation proving it fires, and this. A check that
  cannot distinguish "nothing to catch" from "cannot catch anything" is not a
  check. Watch every new gate fail once, and prefer a gate whose failing
  direction is the loud one.

Affected: `scripts\vm-matrix\gen-offsets.ps1`, `src\xhci_dispatch.c` and
`scripts\vm-matrix\README.md`'s "Generated. Do not edit." row.

## Task 12.5's control: the hub churn wedges Windows 98 only when this driver carries it, and two symptoms this project had been reading as liveness are frozen on healthy guests too

Environment: host `XT-F80DAC37B29E`, QEMU 11.0.92 (batch 11-V ran 11.0.0),
guest 2a (Windows 98 SE + NUSB), launcher
`scripts\local\qemu-win98-run-12v5.cmd` carrying `qemu-xhci` + `usb-ehci` +
`piix3-usb-uhci`, debug build `Aug 18 2026 11:58:03`,
`MiniPortExtensionSize=00015620` = 87,584. Three churn legs. Evidence
`vm\12v5-legA\`, `vm\12v5-legB\`, `vm\12v5-legA2\`.

The result: the identical churn (same guest, same boot, same populated bus,
same `usb-hub`, same 600 ms interval) was carried by Windows 98's own UHCI
stack and by `xhci98.sys` in turn. 122 enumerations through UHCI left the
guest responsive; 12 through this driver wedged it. Reproduced twice on the
xHCI side, the second time on the same boot as the clean UHCI leg, so the
vehicle is not a variable. Stage H's H2 wedge is therefore this driver's, not
a Windows 98 or QEMU limitation, and the mechanism is still unknown.

The discriminating pair is legs B and A', on the same boot: three-controller
vehicle, HID at port 1 and storage at port 2 both at 480 Mb/s on `xhci.0`,
600 ms churn interval; one variable, which controller carried the churn.

| | leg B (Windows 98's own UHCI stack) | leg A' (`xhci98.sys`) |
|---|---|---|
| attach / detach pairs | 122 / 122, 0 refusals | 121 / 120, 1 refusal |
| enumerations completed | 122 | 12 |
| guest afterwards | responsive, mouse and keys confirmed | wedged |
| IDE IRQ 14 across the delta | 35,770 -> 35,786, moved | 42,620 -> 42,620, frozen |
| `HealthPolls` = `CheckCallbacks` | 4,715 -> 5,301, climbing and equal | 24,616 -> 25,230, climbing and equal |
| failure-shaped counters | all 0 | all 0 |

The 12-against-122 gap is the wedge, not a dosing artefact. The churn kept
attaching hubs at the same rate on both legs, and leg A' stopped enumerating
them because the guest had already died at about twelve, so leg B absorbed
roughly ten times leg A''s enumeration dose and stayed healthy. Leg A, the
first xHCI leg on a two-controller vehicle, wedged too, at 18 enumerations;
leg A' exists to close the confound that leg A ran without the UHCI controller
present, and it does. The wedge is reproduced twice on `xhci.0` and absent
once on `uhci.0`.

Lesson 1: a symptom that is frozen in the healthy case is not a symptom. H2's
box recorded "mouse cursor still moving but no click accepted" as evidence the
guest was alive-but-stuck, and listed frozen PS/2 IRQ 12 among the wedge
signature. Both are wrong in this vehicle, and the control is what showed it:
IRQ 12 sat frozen across the entire clean UHCI leg on a guest the operator
confirmed was fully responsive. QEMU delivers no PS/2 mouse input to an
ungrabbed window at all, so IRQ 12 is frozen whether the guest is healthy or
dead, and the cursor that "still moves" is the host's.

A moving cursor shows the host is alive, not the guest. That is the same
sentence as this project's older "a screendump shows what is painted", and it
had to be learned twice because the first version was about a static image
and this one is about motion. Before citing a frozen counter as a wedge
signature, check what that counter reads on a working machine. Of the three
IRQs H2 named, only IDE IRQ 14 survives: it moved on the healthy leg and was
frozen solid across every wedged sample.

Lesson 2: generate the input before concluding input is dead. The
discriminating test cost one message to the operator (move the mouse and type
for ten seconds) and inverted the diagnosis. IRQ 1 climbed 110 -> 242 while
the guest ignored every key. So interrupts are delivered and acknowledged and
nothing is scheduled off them: the stall is above the interrupt layer, not a
failure of interrupt delivery, which is where H2's frozen-IRQ reading pointed.
An absence measured while nobody was producing the stimulus is not an absence.

Lesson 3: when a control cannot be run as specified, the choice of what to
hold constant is the finding's scope, and it must be written down rather than
absorbed. The run sheet said to churn a `usb-hub` on EHCI. That is impossible
in this vehicle: `usb-hub` is full-speed, QEMU's standalone `usb-ehci` is
high-speed only, and QEMU 11.0.92's `usb-hub` has no `usb_version` property,
so the attach is refused outright and produces zero `usb_ehci_*` lines.

The available moves were to change the churn device (keep EHCI, lose device
parity with leg A) or the controller (keep the hub, land on UHCI). The second
was taken. It buys a stronger negative in one direction, since Windows 98's
UHCI stack is the one the OS shipped, independent of NUSB's back-ported
`usbport.sys`, and gives up the narrower claim, because UHCI is a different
stack architecture rather than a sibling miniport under the same
`usbport.sys`. The EHCI leg with a high-speed device on both sides has not
been run and would test the narrower claim.

Lesson 4: a non-involvement proof stated as an absolute breaks the moment the
control needs a populated bus; state it as a delta instead. The batch 8-V
control's form was "`usb_xhci_xfer_start` and `usb_xhci_slot_enable` must
stay at 0". Unavailable here, because holding the population identical
between the legs means this driver is legitimately busy on the control leg.
The replacement is better: each churn device that reaches this driver is one
`SlotsEnabled` increment, so `SlotsEnabled` 2 -> 2 and `usb_xhci_slot_enable`
2 -> 2 while `usb_set_addr` went 2 -> 124 says the churn landed and landed
elsewhere. Prefer a proof that survives the experiment's own conditions.

Lesson 5: a stale generated table is silent, and the freshness check has to
run before the first boot rather than after the first surprise.
`scripts\local\offsets.txt` was found describing an 87,488-byte extension, the
batch 11-V layout, 96 bytes and five regenerations behind the repo audit's
(87,488 -> 87,512 -> 87,524 -> 87,528 -> 87,532 -> 87,584), while
`scripts\vm-matrix\offsets.txt` was current at 87,584. Every counter read
through the stale half would have been plausible and wrong.

The two tables disagreeing is itself the detector (`readcounters.ps1`
cross-checks overlapping fields and throws), but only if both are
regenerated; and `regen-offsets.cmd`'s own `MSVC6` default still pointed at
the `tools\extracted\` path that the toolchain move deleted, so the
regeneration a reader would reach for failed before it started. A per-host
generated artifact rots silently across a repository reorganisation; check it
against its committed twin, not against memory.

Lesson 6: a gate that asserts a neighbouring stage's clause will abort a run
it was never meant to judge. `lifecycle-11v.ps1` refuses to proceed unless
`transfers submitted` is moving, which is stage G's "stop with traffic in
flight" clause. Task 12.5's clause is the churn, and the bus's real background
rate is ~0.5/s, so a 12 s sample read 0 and killed the first leg.
`-SkipTrafficGate` is correct here and the report prints its own caveat ("this
run cannot claim traffic was in flight"), which is the form to prefer: leave
the disclaimer in the artifact rather than in the summary that cites it.

What the run sheet got wrong about its own churn device: "a hub is the churn
device deliberately ... it needs no class install on either target" is false
on this guest. The first churn hub raised an Add New Hardware Wizard for
Generic USB Hub, a modal box inside the churn window.

Still open: the mechanism; whether stage G's fatal `0E` and this silent wedge
are one failure or two; and the EHCI-proper leg. No fix may be written against
this yet. What exists is a reproducible discriminator, not a mechanism, and
that bar is the one task 12.5 existed to enforce. The mechanism hunt is
unowned on purpose: Phase 12's criterion for a task was that a single named
experiment can settle it, and a hunt is open-ended, so it is neither a Phase
12 clause nor a Phase 13 blocker. By the same rule that created task 12.5 it
needs an item the operator approves rather than one an agent invents, and
inventing the successor in the commit that closes the predecessor would be
the same mistake with a different number.

## Tasks 12.3 and 12.4 - an artifact nobody has watched behave is not evidence, and the standard earned its keep both ways

Phase 12's held-open standard, that an artifact nobody has watched behave is
not evidence (this repository's standard for a gate, and not weaker for a
package), paid off on both of the phase's build branches, in opposite
directions. Task 12.3's failed-start artifact was gated on the host and then
run on both guests, and the run turned up a failure mode nobody had predicted,
on the target nobody was watching: Windows 98 does not survive a failed
`StartController` (`docs/using/release-notes.md`, "Known limitations", the failed-start entry).

Task 12.4's unpadded-date experiment did the opposite and was just as useful:
it closed off a suspected defect. Had the padding hypothesis been published as
the likely cause instead of run, the project would have carried a one-line
INF "fix" that fixes nothing and relaxed a gate rule permanently to make room
for it. Neither outcome was reachable by host-side gating.

## Stage H on 2b: three readings of one ring are not meant to be equal, a wrap eats the header first, and the target-specific trap was not target-specific

Environment: 2b, Windows 2000 SP4 uniprocessor, `qemu-win2k-run-11v.cmd`,
QEMU 11.0.0, host minis-w11p-ykm, debug build `Aug 16 2026 18:01:46`,
`MiniPortExtensionSize=000155C0`, Driver Verifier active over `xhci98.sys` at
`Level 0000001B`. Four boots. Readings in `docs/contributing/runs/run-11v.md`
stage H.

Three readings of one counter are not an equality check, and a clause that
says "they must agree" needs reading for what it can mean. H4's sheet says the
flushed file's `flush.dropped` and `log.dropped` "must agree with" the live
`log bytes dropped by the wrap`. They came back 5,633, 6,168 and 6,341, and
that is correct rather than a discrepancy. They are three samples of a still
filling ring taken at three moments, so they are monotonic by construction:
the live read first, then the counter block's own snapshot, which is itself
appended to the ring and therefore pushes more out, then the flush's final
figure.

The rule: before treating a multi-source reading as a consistency check, ask
whether the sources could have been taken at the same instant. Where they
could not, what agrees is the answer, not the number, and writing "they
agreed" would leave the next reader thinking an instrument is broken when
they differ again. A prior stage lost a boot to the opposite error, quoting a
rule instead of applying it, and this is its mirror: applying an equality
that the mechanism forbids.

A ring buffer's wrap eats the oldest record, and the oldest record is the one
the documentation promised. `docs/using/release-notes.md` said, without
qualification, that the driver "records the path it actually chose as the
log's first line". H3's 2,107-byte file has that line. H4's 16,384-byte file
has no `log.file.path`, no `log.file.status`, no `start=` and no `hc.pci` at
all; it begins mid-record with a bare newline. The header is written once at
start, so it is the first thing a wrap discards.

The rule: a header written once into a circular buffer is not a header, it is
the record most likely to be missing. Any claim about "the first line" is
conditional on the buffer not having wrapped, and the condition has to be
published with the claim. The absence is usable rather than merely awkward:
no path line means the log is a window on a longer run, which is a second,
redundant signal beside the dropped-bytes count.

The 2a lesson "never find the driver key by `NTMPDriver`" was recorded as a
Windows 98 lesson and is not one. This Windows 2000 image also holds two
devnodes for `VEN_1B36&DEV_000D`, `2&ebb567f&0&18` and `&0&20`, because the
batch's launchers moved the controller between PCI slots. The devnode's own
`Driver` value resolved the live one to Class key `0023`, and the release
notes' example key said `0002`. The rule: when a trap is found on one target,
ask what produces it before filing it under that target. Here the producer is
"the controller has been enumerated at more than one PCI address", which no
operating system is immune to; the Windows 98 detail was only where it was
first met. The generic form, ask the object which key it resolves to, was
already written down, and it is what worked on both.

Measure the budget by counting what survived, not by dividing. The
user-facing number for "how much fits in 16 KB" could have been derived as
bytes-per-record, and would have been an estimate with a compounding error.
Counting the record types in the surviving window instead gives it exactly:
42 `slot.enabled`/`slot.addressed`/`slot.route`/`slot.parenthub`, 41
`port.connect`, 84 reset pairs, 102 `ep.open`. That is 42 enumerations, about
14 three-class replug rounds, and it is a statement a user can check rather
than trust. The same file's `xfer.submitted`/`completed`/`cancelled` matched
the soak harness's independent settled reading (3,866 / 3,805 / 61), which is
what proves the log and the counters describe one run.

A harness guard that refuses is worth more than a harness that measures.
`soak-11v.ps1` declined to start because the debugcon log spanned four driver
loads and it requires one continuous load. That cost a boot to rotate the log
properly. It was the right refusal: the alternative, pointing it at an edited
copy, would have produced a plausible number computed across a discontinuity.
Same family as `readcounters.ps1` throwing on mismatched offset tables.

Not a finding, recorded because it looked like one. An ordinary shutdown taken
between boots produced `flush create status=00000000`, `flush write
status=C0000189` `STATUS_TOO_LATE` and no file. Stage C measured that and the
release notes already publish it. It is a replication against a new binary
and a moved extension layout. The check that caught it was grepping the repo
for the status code before claiming novelty. Grep for your finding before you
call it one.

## Stage H on 2a: a sink that emits into a listener the OS has already closed, a driver key that was right by the documented rule and wrong in fact, and a "healthy" guest that had been wedged for twenty minutes

Environment: host `minis-w11p-ykm`, QEMU 11.0.0, guest 2a (Windows 98 SE +
NUSB), five boots. Stage H's H1 (task 11-V.9's run tail) and H2 (task
11-V.1's negative control). Evidence `vm\11v-stageH\`.

The interlock passed and is not the lesson. Three path roots refused the read
mask, so the write-mask request that hung stage C's boot 8 was never issued,
the file sink declined with a status naming which refusal, and no file was
created. A shipping decision previously argued from a matrix is now confirmed
by a machine.

Lesson 1: a sink is not reachable until the moment it fires is reachable.
`XhciLogDebugView` was designed to emit the ring at the PASSIVE flush, which
was the right safety choice, and it is what makes the sink useless on Windows
98. The ring is drained only at `StopController`; the only stop that target
offers is a shutdown (a disable bugchecks it); and Windows closes DebugView
before it stops the driver. The driver emitted 2,038 bytes into nothing.

The prior claim "reachable on Windows 98, proved in the 2a VM" had conflated
two different propositions: that `DbgPrint` output reaches DebugView there
(true, and re-observed live in this run) and that the flush reaches it
(false, and never tested until now). The reusable rule: when a mechanism is
gated to one moment, verify that moment's environment, not the mechanism's.
"The call works" and "the call works then" are different claims, and the
second is the one a user depends on. This cost task 12.2 its live candidate,
on ordering rather than on the interrupt rate everyone was watching for.

Lesson 2: a rule validated on a clean image can be false on a working one,
and identity rules are the ones to distrust. Stage B3 said to find the driver
key by "the subkey whose `NTMPDriver` is `xhci98.sys`". True on a clean image
with one install; on 2a there were three, because the same controller had
been enumerated at three PCI addresses by three of this batch's own
launchers, and two of them carried an identical `DriverDesc`, so the
documented fallback discriminator failed too.

Values typed into a stale key are read by nothing, and the service reports that as
`MP_STATUS_UNSUCCESSFUL`, indistinguishable from an absent value. Ask the
object itself which name it resolves to: the devnode's `Driver` value under
`Enum\PCI`, which is literally what
`IoOpenDeviceRegistryKey(PLUGPLAY_REGKEY_DRIVER)` returns, rather than
searching for a key that looks right.

Lesson 3: when the failure code carries no diagnosis, stop spending boots on
it. `UsbPortGetMiniportRegistryKeyValue` collapses "value absent", "key would
not open", "buffer too small" and "pool failed" into one `8` (abi section 6,
`neg/sbb/and 8`). Once that is recalled, no further guest boot can narrow the
question and the only productive move is to change instrument: two registry
branches exported to the writable floppy and read on the host settled in one
pass what three screendumps had left ambiguous. It also stepped around the
screendump-shows-what-is-painted trap, which had made a third driver key look
like a stale repaint. A cheap host-side reading beats another boot whenever
the guest's own answer is known to be undifferentiated.

Lesson 4: a ride-along must yield when the clause it rides has become
fragile. H2 was scheduled before H1's shutdown, per stage H's ordering note.
That was correct when written and wrong by the time it arrived: once H1 step
6 had put records in the ring, a churn crash would have destroyed steps 7-8.
The precedence note ("it must not displace a clause above") overrides the
ordering note, and re-checking which of the two applied, rather than
executing the plan as drafted, is what preserved the leg. Re-evaluate a
scheduling decision against the state that exists when its turn comes.

Lesson 5, and it is the one that cost the most: a single sample is not a
delta, and the mistake was made in the same paragraph that quoted the rule
against it. H2 ran 150 hub add/remove pairs over 415 s against a populated
bus, 25 enumerations reaching the driver (stage G boot 1: 122 pairs, 420 s,
6). It was recorded as "no crash, instruments clean, guest alive rather than
painted" and written into five documents. It was wrong. The evidence cited
was a screendump showing a rendered desktop, which this project's own harness
rule says proves painting, not liveness, and one `info irq` reading quoted as
though it established motion.

The operator reported minutes later that the guest took no clicks. Two `info
irq` samples then showed IDE IRQ 14 at 24,986 in both, the identical value it
had held at the end of the churn. It had never moved. The desktop in the
"healthy" screendump was painted and dead, and the guest had already wedged
at the moment it was declared clean. The timer kept counting and our own IRQ
kept counting, so one sample of any of them proves nothing.

Corrected result: the churn wedges Windows 98 with no shutdown involved. IDE
frozen, clock stopped, no bugcheck: a silent wedge rather than stage G's
fatal `0E` but with the same IRQ signature. So stage G's coincidence-of-one is
not broken; it has a second observation beside it, and this one removes the
shutdown from the picture entirely. The driver is exonerated as the thing
that stopped (`HealthPolls` = `CheckCallbacks` climbing and equal across both
wedge dumps, transfers still moving with the outstanding gap constant at 15,
every failure counter 0), which does not make it uninvolved, since the churn
is delivered through it.

Task 12.5's control entry above went further in two places. The control this
paragraph could not run has been run: the churn wedges Windows 98 only when
this driver carries it (122 enumerations through Windows 98's own UHCI stack,
guest responsive; 12 through `xhci98.sys`, guest wedged, same boot). And
"mouse cursor still tracking", which this run recorded as part of the
signature, is not a symptom at all: PS/2 IRQ 12 is frozen in this vehicle on
healthy and wedged guests alike, because QEMU delivers no mouse input to an
ungrabbed window, so the cursor being watched was the host's. Of the IRQ
signature named here only IDE IRQ 14 discriminates.

The reusable rules, and the first two are re-learned rather than new:

- Take two samples or make no claim about motion. "Alive" is a derivative; it
  cannot be read from a scalar.
- Quoting a rule is not applying it. The write-up cited harness rule 6 by name
  while breaking both of its halves.
- A wedge can be silent. Stage G taught this project to look for a fatal `0E`;
  this one had no bugcheck, no dialog and a normally-painted desktop. The
  only external symptom was a clock that had stopped, and the only reliable
  internal one was a frozen IDE interrupt count.
- Do the harness's prescribed order even when the result looks obvious: full
  counter dump first (a live wedge cannot be snapshotted; `savevm` refuses on
  these launchers), then a second dump, because the diff is the evidence.
  Doing that is what produced the correction.
- A repeated line at the end of a log is where the log stopped, not
  necessarily what the machine is doing. The trace's trailing run of `root
  hub: announcing a port change` read like a livelock; `RootHubInvalidates`
  was frozen at 357 and the trace held exactly 357 of them. Checking the
  counter against the line count stopped a third wrong finding from being
  written down.

## Task 11-V.9, host-side: a mutation sweep's own false negatives, and two rules from a previous task that had to be overturned rather than inherited

Environment: host `minis-w11p-ykm`, no guest booted. Task 11-V.9's host-side
pass: the producer set, the two registry values, the path validation and the
Windows 98 interlock. The run tail is owed and nothing here is a target
observation.

A mutation sweep can report a survivor that is an artefact of the sweep, and
the artefact looks like a gap in the tests. Three of thirteen mutations
"survived" on the first run because `src/xhci_dispatch.c` carries a
driver-side body and a host stand-in of the same function, one inside
`#ifndef XHCI_HOST_TEST` and one inside the `#else`, and a
replace-the-first-occurrence mutation changed the half the suite does not
compile. The reusable rule: in a file with a build-conditional twin of a
function, mutate every occurrence, or the sweep is measuring the branch that
is not under test. The symptom is indistinguishable from a real survivor,
which is what makes it worth writing down. A survivor is normally read as a
finding about the tests, and three of them at once would have sent the next
round looking in the wrong place.

The three genuine survivors were all gaps in the checks, and each had the
same shape: a net over something no vector had put anything into.

- An assertion that a stale composed path is cleared, made against a buffer
  no vector had ever composed into. It passed for the same reason a
  `writeCount == 0` assertion passes when nothing ever wrote: the state it is
  about was never created. Fixed by composing first, which is what the
  driver's probe does.
- A record cap this driver's own callers cannot reach (the longest label is
  13 characters and the longest composed path 75, against a cap of 96), so
  nothing drove it and it was deletable with the suite green. A defensive
  bound that no caller can reach is still a bound a later edit can delete,
  and the answer is to drive it through the function's contract (a long label
  is a legal argument) rather than to delete it or to pretend a caller
  reaches it.
- The clause "the probe opens the measured table's own names, never the
  user's string". The stand-in recorded the probed path's length, and a
  length cannot tell a default name from a user path of the same length. A
  test that records a measurement of the thing instead of the thing cannot
  support the negative; the same shape as batch 6-0's `PassThru` and stage
  C's cached-INF row.

Two rules from task 11-V.7 had to be overturned rather than inherited, and
both overturnings are about a premise that changed underneath them.

- 11-V.7's path probe ends "if no form resolves, the NT form is used anyway;
  the probe may improve a target and may never regress one". That was right
  while the path was a constant this project had measured on both targets. It
  is wrong once the path is a user string: the same fallback would carry an
  unmeasured path to `ZwCreateFile` on the boot path of the one target where
  a create has been measured never to return. So where no root resolves, the
  file sink is now off. The rule: when an input changes from a constant you
  measured to a value a user supplies, re-derive every decision that rested
  on having measured it. The code did not change; the premise did.
- 11-V.7's flush counts a pass that wrote no file as a `FlushFailures`. With a
  second sink that opens no file at all, that would report every successful
  DebugView dump on Windows 98, the one target the sink exists for, as a
  failure. A counter's meaning is relative to the set of outcomes that
  existed when it was written, and adding an outcome is an occasion to
  re-read it.

The extension moved with the ring (74,812 -> 87,488), which is the third time
this phase a layout change has forced both offset tables and the staged media
to be regenerated in the same commit. Nothing new was learned about that; it
is recorded here only because the count is now three.

## Windows 98 blocks its own shutdown on every running DOS program, so the load that makes a stop testable on Windows 2000 makes it untestable here

Environment: host `minis-w11p-ykm`, QEMU 11.0.0, the 2a Windows 98 SE guest
via `scripts\local\qemu-win98-run-11v.cmd`. Debug build, built
`Aug 14 2026 00:36:27`, `MiniPortExtensionSize=0001243C` = 74,812 = both
tables' `SIZEOF`. Batch 11-V stage G, task 11-V.1, the 2a leg.

### What was being done

`docs/contributing/runs/run-11v.md` stage G's eight-step recipe: attach three
classes, start `LOAD98.BAT`'s guest-side load, then commit an orderly
shutdown with `scripts\vm-matrix\lifecycle-11v.ps1 -Commit sendkey` and read
the teardown. The recipe had been written for 2a before any 2a boot, by
transcribing the 2b leg that had just passed.

### Observed

The stop was fired into a bus doing 1,909 transfers/s (74,402
`usb_msd_cmd_submit` in the trace, so the load was real) and no
`cb StopController` arrived in 420 s. The guest then showed Windows 98's
shutdown-time close-programs dialogs, and finally "You must quit this program
before you quit Windows", one per running DOS program, of which the load
leaves eight. The box marked "Closing - MS-DOS Prompt" never yielded:
`STGF98.BAT`'s disk stream is `goto diskloop` with no bound.

### Proven

- The Enter was delivered and the shutdown did start. The close-programs
  dialogs only appear once shutdown is under way, so this is measured, not
  inferred.
- The shutdown never reached the miniport, and the cause is above this
  driver: Windows 98 will not proceed past a running DOS program.
- The driver was healthy throughout: `OpenRefusals` 0, `CommandFailures` 0,
  `EndpointHalts` 0, `TransfersAborted` 0, no failure-shaped counter moved,
  `HealthPolls` = `CheckCallbacks` = 23,140.

### Inferred / unknown

A fatal `0028:C002FF2A in VXD NTKERN(01) + 0000E32E` occurred during the same
window. It is recorded as unattributed. The only boot that ran the hub churn
is also the only boot that crashed, which is a coincidence of one; the
negative control (churn with no shutdown) was not spent. Later boots ran
`-NoChurn` to keep the variable out of the clause, not because the churn was
shown guilty.

The control was then given an owner: it rode task 11-V.9's 2a boot, the next
boot this project took on that target. Run the churn, let it complete, do not
shut down. A crash implicates the churn independently of the shutdown; no
crash breaks the coincidence. The outcome is in the Stage H on 2a entry
above. An unspent control is not evidence in either direction, which is the
whole reason it was recorded rather than resolved.

### The reusable rules

- The change that makes a clause reachable on one target can be what makes it
  unreachable on another. `STAGEF.BAT`'s ten-pass bound was removed because a
  finished stream cannot supply a stop that lands on traffic. On Windows 98
  the opposite holds: an unfinished stream cannot supply a shutdown at all.
  This is "a tool validated on one target is validated on one target" applied
  to a behaviour rather than to a script's syntax, which is how it slipped
  through: `LOAD98.BAT`/`STGF98.BAT` were already the Windows-98-specific
  rewrite, so the pair looked target-aware.
- A tool's built-in explanation for a negative is a hypothesis, not a
  reading. `lifecycle-11v.ps1` reports an absent stop as "what a swallowed
  keypress looks like (a critical-error box owns the console)". On this run
  that sentence is wrong, and a leg that accepted it would have recorded the
  wrong cause and re-run the same broken recipe.
- On Windows 98 the traffic for a lifecycle stop must come from something the
  OS will close by itself: an Explorer copy (a Windows program), or
  host-driven HID via `hidpump-11v.ps1`, which costs the guest no process at
  all.
- Throttle the medium when the measurement has a preamble. An unthrottled
  96 MB copy completes in ~20 s on this vehicle, while `lifecycle-11v.ps1`
  spends 60-90 s on its identity read, liveness probe, traffic gate and
  pre-stop counter block before it commits, so the traffic was always gone by
  the commit. Re-attaching storage at `throttling.iops-total=30` stretched the
  same copy past 100 s and put the commit inside it. `STGF98.BAT`'s own header
  already said why the throttle exists; the leg had to rediscover it.
- `AbortsBeforeStopped` is the counter that says a stop met a live transfer,
  as against three classes each holding a parked interrupt IN. The
  weak-traffic stop set it 0 and the throttled replication set it 1, with the
  abort total 3 in both. Read it in preference to the gate rate.

Affected: `docs/contributing/runs/run-11v.md` stage G (the WIN98 SHUTDOWN
BLOCK box), `scripts\vm-matrix\guest\STGF98.BAT`,
`scripts\vm-matrix\lifecycle-11v.ps1`.

## A refused EP0 reopen is survivable on Windows 2000, which separates the refusal from the 2a wedge it preceded

Environment: host `minis-w11p-ykm`, QEMU 11.0.0, the 2b Windows 2000 SP4
guest via `scripts\local\qemu-win2k-run-11v.cmd`, under Driver Verifier
(`Level: 0000001B`, force IRQL on, `xhci98.sys` verified). Debug build,
`MiniPortExtensionSize=0001243C` = 74,812 = both tables' `SIZEOF`, extension
`8184292C`. Batch 11-V stage G, and the finding is a by-product of a failed
attempt at that stage's armed-callback clause, not of a test aimed at this.

### What was being done

A `usb-hub` was added and removed on a free root port every 250 ms for about
five minutes, to try to make a stop land on an armed port-reset or command
callback. It did not achieve that. What it did do is drive the guest's PnP
path far harder than any earlier leg.

### The observation

```
cb RH_ClearFeaturePortEnable irql=02 a=8184292C b=00000004
slot: EP0 open for an address no record holds=00000004
EP0 opens refused=00000001
```

This is the seam that ended 2a's `net` cycle leg at `-SettleSeconds 6` in
stage F (`xhciDevByAddress` in `src\xhci_slot.c` returning NULL for address 1):
usbport naming an address no record holds.

Those two lines are not a causal chain, and reading them as one is the same
error the 2a entry already warns about, committed one level up. The
`b=00000004` on `RH_ClearFeaturePortEnable` is a port number; the `=00000004`
on the refusal is a device address; the two 4s are unrelated quantities that
coincide, and the lines are 203 apart, with a full enumeration between them
(`slots enabled` and `devices addressed` both reaching 10). What immediately
precedes the refusal is a successful enumeration. What triggered the refusal
is not established by this run, only that it happened, during a plug storm,
to an address whose record had been torn down earlier in it (`devices torn
down` 7). Two adjacent-looking values of the same magnitude are a coincidence
until the units are checked.

What this run does establish is that it was survived. After the refusal,
enumeration continued to 10 devices addressed, open accounting stayed
balanced (`OpensTotal` 45 = `OpensAccepted` 44 + `OpenRefusals` 1, and the
driver's own "open accounting: OK"), `CommandFailures` 0, `EndpointHalts` 0,
`TransfersAborted` 0, transfers kept flowing at ~470/s, `HealthPolls` =
`CheckCallbacks` kept advancing, and the guest was driven normally
afterwards. The Device Manager asset capture with all three classes unbanged
was taken after it.

### What is proven, and what is not

Proven, on this target: a refused EP0 reopen at this site does not by itself
stop enumeration, does not corrupt the driver's open accounting, and does not
wedge Windows 2000.

Not proven: anything about Windows 98. 2a's signature was the opposite: not
one further transfer arrived across fourteen cycles while the kernel kept
running. The open question was whether the refusal causes that wedge or
merely precedes it, and this is a differential rather than an answer. The
refusal alone is not sufficient to wedge a usbport, so "the refusal is fatal"
is no longer the simplest reading of the 2a run. NUSB's usbport and SP4's
usbport are different builds and this says nothing about the first.

### What it still costs, twice now

The site increments the bare `OpenRefusals` with no sub-counter, so a release
build shows a refusal that cannot say why. That is the "eleven
`OpenRefusals++` sites, three sub-counters" gap, now hit in the wild on both
targets. The next thing worth doing here is sub-counters at that site, not
another run.

### Two vehicle facts from the same attempt

- A guest's PnP path, not the monitor, sets the plug rate the driver sees.
  Four `device_add`/`device_del` per second for five minutes produced seven
  completed enumerations. An armed-callback window cannot be brute-forced
  from the monitor.
- A blocked MMC takes the whole desktop's keyboard with it. A Device Manager
  disable of the controller with a storage stream writing never reached the
  miniport at all (zero `cb StopController`, no red X on the devnode
  afterwards), and MMC blocked on it with a menu open. An open Windows menu
  holds an input capture, so every keystroke went nowhere while the kernel,
  Explorer and this driver all kept running. `ctrl-alt-delete` breaks the
  capture. A screendump of the frozen menu is a picture of what is painted;
  the readings that separated it from a wedge were the IRQ delta, the
  transfer rate, `HealthPolls`, and Explorer's own clock advancing between
  two shots.

## A device can occupy a QEMU port without being attached, and `info usb` cannot tell you which - the `usb-bot`/`usb-uas` "+0" was never this driver

Environment: this development host, QEMU 11.0.92, the 2b Windows 2000 SP4
guest via `scripts\local\qemu-win2k-run-11ve.cmd` (one `qemu-xhci`, id
`xhci`, `p3=0`). Debug build (`DriverEntry (built Aug 14 2026 00:36:27)`,
`MiniPortExtensionSize=0001243C` = 74,812, equal to both counter tables'
`SIZEOF`). Roadmap task 11-V.4, batch 11-V stage E.

### The symptom, carried for two phases

`usb-bot` and `usb-uas` attached with `device_add`, `info usb` listed them,
and `devices addressed` moved by +0 on both targets. Phase 10 named it the one
open item that might be a defect in this driver. An earlier explanation, two
rows sharing one `-drive` backend, which QEMU auto-deletes on unplug, was
correct, was fixed, and did not move the rows, so it had been explicitly
discarded before this run.

### What it actually was

Both device models set `auto_attach = 0` at realize, because a SCSI host
adapter is meant to be presented to the guest only once its LUN exists. QEMU
therefore places the device in the port and never electrically attaches it.
Measured, each against a control taken on the same machine state:

| | `usb-storage` (control) | `usb-bot` | `usb-uas` |
|---|---|---|---|
| `qom-get /machine/peripheral/<id> attached` | `true` | `false` | `false` |
| QEMU trace for its port | `pls 7` -> `port_reset` -> `pls 0` -> `slot_address` | `port_link pls 5` (RxDetect) x3, no connect | same |
| `devices addressed` | +1 | +0 | +0 |

So no `PORTSC` change ever reached the driver. `RhFirstDecodes` never moved
and every endpoint- and open-refusal counter stayed 0, not because
enumeration forgave the device but because enumeration never saw one. A
single `qom-set /machine/peripheral/bot1 attached true` enumerated both
immediately: `devices addressed` 1 -> 3, `OpensTotal` 6 -> 14 all accepted,
573 submitted = 573 completed, zero refusals, Windows raising its own wizard
for each.

### The reusable rule, and why this one stings

`info usb` lists a device that merely occupies a port. It is not a witness of
attachment. `qom-get <path> attached` is, and the QEMU-side
`usb_xhci_port_link` / `usb_xhci_port_notify` trace is the second,
independent one: `pls 5` is RxDetect and means the controller has been told
nothing is there.

The sharper lesson is about the instrument. The harness already carried a
guard written against this exact failure ("an attach is only an attach if the
device arrived", added when these very rows first read +0), and that guard
asks `info usb`. A guard written against a failure can be blind to it in the
way the failure requires. "Check that your instrument could have shown the
positive" has now been paid four times in one batch; this is the first time
the blind instrument was the check itself rather than a missing one. Fixed
with `Confirm-DeviceAttached` in `scripts\vm-matrix\run-matrix.ps1`, which
reads the state, repairs it only when it reads false, and reads it back.

Scope: this is a property of two QEMU device models. Nothing here transfers
to a physical BOT or UAS enclosure, which remains unmet and belongs to task
13-E.4.

### A second fact from the same boot: a modal dialog blocks the Windows 2000 PnP install queue

A keyboard behind a second-tier hub (port `3.2.1`) sat at `Device 0.0`,
unaddressed, no refusal counter moving, for over a minute while a Found New
Hardware Wizard and an "Unsafe Removal of Device" box were open, then was
addressed within seconds of both being cleared. This is the 2a "the modal
wizard freezes an untaught image" fact in a milder form. An unaddressed
device is unreadable while a dialog is up, not negative. Clear the desktop
before concluding anything about enumeration, or a working multi-tier path
reads as a depth limit.

## Windows 98 offers a driver no way to write a file, and asking it for write access hangs the boot - the diagnostic that would have bricked a user's machine

Environment: this development host, QEMU 11.0.92, the 2a Windows 98 SE guest
via `scripts\local\qemu-win98-run-11vc.cmd`, NUSB 5652. Four debug builds
swapped in at `D:\PATHFIX\XHCI98.SYS` across boots 5-8 (`built Aug 14 2026
00:08:34` / `00:20:27` / `00:30:28`; extensions `0001242C`, `00012430`,
`0001243C`). Roadmap task 11-V.7, batch 11-V stage C. Evidence
`vm\11v-stageC\`.

### The symptom

The optional log flushes at `StopController` and writes `C:\XHCI.LOG`. On
Windows 2000 it produced a complete 690-byte file. On Windows 98 the same
binary failed at the create with `STATUS_OBJECT_PATH_NOT_FOUND` (`C000003A`)
and produced nothing. Two causes survived the run and the guest could not
separate them: (a) the shutdown had already taken the volume away, which is
demonstrably what breaks the Windows 2000 shutdown route, where the create
succeeds and the write returns `STATUS_TOO_LATE`; or (b) the path form
`\??\C:\XHCI.LOG` does not resolve under `ntkern` at all.

### Put the measurement where a failure can only mean one thing

A retry at the flush could not have decided it, because the flush is the one
moment both causes predict failure. The probe went at `StartController`
instead (PASSIVE, file system demonstrably up) and opened each form with
`FILE_OPEN`, so "the form resolves and the file is absent" (`C0000034`) and
"the form does not resolve" (`C000003A`) are different answers rather than
one failure.

It answered (b), at the start, with the machine up: `\??\` does not resolve
on Windows 98 at any time. A plain `C:\XHCI.LOG` tried beside it answered
`STATUS_OBJECT_PATH_SYNTAX_BAD`, which says `ntkern` is parsing NT
object-namespace syntax and rejects a drive-relative path. So the answer was
a different root, and the roots to try came out of `ntkern.vxd` itself rather
than out of a guess: `\??` appears in that binary in neither encoding, while
`\DosDevices`, `\DosDevices\`, `\SystemRoot\` and `\Device` all do.

### The instrument was confounded, in the shape this batch had already paid for

The first widened probe opened every form with `FILE_READ_ATTRIBUTES`, on the
reasoning that a read-only probe should ask for as little as possible. Both
real roots answered `STATUS_NOT_SUPPORTED`, and that was very nearly
published as "no file sink on Windows 98". It could not support that claim:
the only form ever tried with the writer's own mask was `\??\`, which had
already failed for the unrelated reason that its root does not exist. The
instrument could not have shown the positive. Batch 6-0's `PassThru` shape,
and stage E's `info usb` shape, for the third time in one batch.

### What the write mask found, and why it stopped the feature rather than fixing it

| Form | `FILE_READ_ATTRIBUTES` | `FILE_APPEND_DATA` |
|---|---|---|
| `\??\C:\XHCI.LOG` | `C000003A` no such root | `C000003A` no such root |
| `\DosDevices\C:\XHCI.LOG` | `C00000BB` not supported | never returned |
| `\SystemRoot\XHCI.LOG` | `C00000BB` not supported | unreachable behind the gate below |

Opening `\DosDevices\C:\XHCI.LOG` for write never returns. The guest sits on
the splash screen with the CPU live and the boot blocked inside
`StartController`, which is on the boot path. The trace ends mid-probe with
no line after it; the previous build differed only in the access mask and
booted through all three forms to a desktop, so it is the write request that
hangs and not the form.

So a diagnostic switch could leave a user unable to boot, and that outranks
the diagnostic. The probe now asks for the write mask only where the read
mask resolved, which costs no reachable measurement, since a path that will
not answer a request for attributes has nothing to offer a request for write
access. Windows 98 selects nothing, the file sink stays off whatever the
registry says, and the driver records that it declined.

### The reusable rules

- A probe's later cells sit behind its earlier ones. A boot that hangs
  half-way through a table does not return an incomplete table; it returns
  no table, and the cells after the hang are unmeasured however confidently
  the matrix is written up afterwards. Here the earlier read-mask boot is the
  only witness of the third root, and it survived in the launcher's rolling
  `.previous.log` rather than in the stage's evidence directory. Copy the
  boot that answered into the evidence directory on the day it answers.
- "Asked three ways" is not "three answers". The write mask reached two of
  the three roots and one of those did not answer at all. A write-up that
  flattens a hang and an unattempted cell into "all refused" is claiming a
  reading nobody took, and two documents in this repository did that until
  they were corrected.
- An untrusted-input diagnostic on the boot path is a safety question before
  it is a usability one. `StartController` runs before the machine is
  usable, so anything it can block on is a brick, and the gate that prevents
  it must be a measurement rather than a caution.

## Batch 11-V stage A: the Win98 idle hot-plug defect is FIXED by one registry value - after three correct derivations that were all answering the wrong question

Environment: this development host, QEMU 11.0.92, the 2a Win98 SE guest via
`scripts\local\qemu-win98-run-11va.cmd`, which puts a `usb-ehci` (ICH4,
`8086:24cd`) beside `qemu-xhci` so NUSB's own `usbehci.sys` and this driver
run as two miniports under one `usbport.sys`. Debug build (`DriverEntry
(built Aug 13 2026 10:58:30)`, `MiniPortExtensionSize=00012418` = 74,776,
equal to both counter tables' `SIZEOF`). Roadmap task 11-V.6.

### The differential, and what it cost to make it a measurement

The defect under investigation: on Windows 98 a device attached after the
controller idle-suspends is detected by nothing until a Device Manager
Refresh. The question was whether that is the back-ported `usbport.sys` (in
which case no miniport can fix it) or something the shipping miniport does
that we do not.

With Device Manager closed and the bus genuinely bare:

| | xHCI (ours) | EHCI (Microsoft's) |
|---|---|---|
| device attached while idle | nothing for 40 s, no wizard | Add New Hardware Wizard for `USB Human Interface Device` |
| driver counters across the attach | `HealthPolls` 41 -> 41, `RhPortStatusQueries` 20 -> 20, `SuspendCount` 1, `RootHubInvalidates` 1: nothing ran at all | n/a |

So `usbport.sys` is exonerated and the stop rule does not fire. The
interesting half is why.

Both controllers are halted when idle, and that had to be measured, not
assumed. With both buses bare and ~2 minutes idle, read straight off the
monitor with `xp`:

```
xHCI  op+0x00 USBCMD  0x00000000   op+0x04 USBSTS  0x00000001  (HCH=1)
EHCI  op+0x00 USBCMD  0x00010020   op+0x04 USBSTS  0x00001000  (HCHalted=1)
      op+0x08 USBINTR 0x00000004   <- Port Change Detect Enable, and only that
```

`usbehci.sys` halts on suspend just as we do. The difference is the one
enable it leaves standing. Re-attaching to the EHCI from a halt read
immediately beforehand moved `USBCMD 0x00010020 -> 0x00010031` and `USBSTS
0x00001000 -> 0x0000c000`: the connect raised an interrupt on a halted
controller, its ISR claimed it, and usbport resumed. The first EHCI attach
only inferred the halt from elapsed idle; the second one is the one that
proves it.

### The mechanism, read out of the binary rather than guessed

NUSB `usbehci.sys`, `SuspendController` (packet `+0x40`, packet base
`0x143A0`, image base `0x10000`) at VA `0x13C92`, `link -dump -disasm`,
static:

1. saves `PERIODICLISTBASE`, `ASYNCLISTADDR`, `CTRLDSSEGMENT`, `USBCMD` into
   its extension (`+0x164`, `+0x168`, `+0x16C`, `+0x170`);
2. `USBCMD &= ~0x40`, then `USBCMD &= ~0x01`, the halt;
3. `KeStallExecutionProcessor(0x7D)` (125 us), then acknowledges every
   pending `USBSTS` bit (`and eax,3Fh`, RW1C write-back);
4. `USBINTR = 0`: it masks everything, just as `XhciSuspendController` does;
5. polls `USBSTS` bit 12 (`test ah,10h`) for `HCHalted`, up to 10 times,
   through packet `+0x114` = `UsbPortWait`;
6. then, last thing before the epilogue, `USBINTR |= 4`: it re-arms Port
   Change Detect after the halt.

Imports resolve from `usbehci-imports.txt`: `[0x141B0]` `READ_REGISTER_ULONG`,
`[0x141AC]` `WRITE_REGISTER_ULONG`, `[0x141A0]` `KeStallExecutionProcessor`.
So the live measurement and the static read agree bit for bit.

### Why there is no xHCI equivalent, from the spec rather than by analogy

The obvious "fix", to stop masking in `XhciSuspendController`, is not
available, and the spec says so in a note under a figure rather than in a
register table:

- p.294, under Figure 4-34: "Under some conditions the xHC may not be capable
  of generating Port Status Change Events, i.e. if HCHalted (HCH) = '1' or
  the Event Ring is full. If the HCHalted (HCH) = '0' and the Event Ring is
  not full, the xHC shall generate Port Status Change Events." `HCHalted` is
  drawn as an explicit input to the PSCEG block.
- p.364: "The EINT flag does not generate an interrupt, it is simply a
  logical OR of the IMAN register IP flag '0' to '1' transitions", and
  EINT/PCD "are typically only used by system software ... when interrupts
  are disabled".

An xHCI interrupt exists only as an Event TRB reaching an Interrupter, so a
halted xHC has no interrupt to enable. `USBSTS.PCD` is a status bit a poller
reads, not an EHCI-style `USBINTR.PCD` enable. The two architectures differ
in kind here, not in configuration.

The same Figure 4-34 shows the one path that does survive a halt: PME#
generation, fed by Connect Detect and gated by the port's `WCE`/`WDE`/`WOE`
bits plus `PMCSR.PME_En`. That is the `USB_MINIPORT_FLAGS_WAKE_SUPPORT`
candidate, with its own obligations, and not the "leave an enable set"
candidate at all.

And p.295 explains why Refresh works and why nothing is lost meanwhile: a
port "may report a device is connected (CCS and CSC = '1') before the xHC is
running ... and when software enables the xHC and HCHalted transitions to
'0', PSCEG shall be asserted for each port with a connected device,
generating a respective Port Status Change Event."

### Rules

- A differential is not finished at "theirs works, mine doesn't"; it is
  finished at the register that differs. Here it was one bit in one register,
  visible from the monitor with no rebuild and no guest cooperation, and it
  turned a three-candidate design argument into a single measurement.
- "It woke" is inferred until the halted state is read immediately before
  the stimulus. Elapsed idle is not a state reading.
- Do not port a mechanism across controller architectures by shape. "Leave
  the port-change interrupt enabled across the halt" is a complete sentence
  on EHCI and a category error on xHCI. The prohibition lived in a note
  under a figure, which is where this kind of thing tends to live.
- When the spec's register table does not answer a behavioural question, the
  answer is in the section the table's Refer-to points at. Here `USBSTS.PCD`
  pointed at 4.19.3 and 4.15.2.3, and the gating sentence was on the page
  before 4.19.3 begins.

### The two surviving candidates, both closed by measurement in a second boot

The differential exonerating `usbport.sys` is not a licence to write a wake
path. It obliges the other candidates to be disposed of, and both were, with
one instrumented build and one idle boot.

A timer-driven poll has no clock. `HealthPolls` was frozen at 41 across the
whole suspension, which reads two ways with opposite conclusions: usbport
stopped calling, or usbport called and our own `INITIALIZED` gate declined
before the counter. The gate returns above the increment, so the counter
could not tell them apart.

A `CheckCallbacks` counter placed at the top of `xhciCheckController`, above every gate, settles it: across 90 s of
suspension `CheckCallbacks` and `HealthPolls` are equal and both frozen at
40. usbport does not call this miniport at all while it holds the controller
suspended: not calls declined, calls never made. `UsbPortRequestAsyncCallback`
takes usbport's own timer (`src/xhci_cmd.c`), so the mechanism that would
deliver a poll is the one measured stopped. (Measured for `CheckController`;
inferred for `RequestAsyncCallback` from the shared timer, which is not the
same thing and is recorded as an inference.)

PME# cannot be reached in this vehicle, and that is a property of the vehicle
rather than a result. With `pci_cfg_read`/`pci_cfg_write` traced for a whole
boot, `qemu-xhci`'s capability chain is `@0x34 -> 0x90`, `@0x90 -> 0x11`
(MSI-X), next pointer `0x00`: there is no PCI Power Management capability on
this controller at all, so there is no `PMCSR.PME_En` to arm and nothing to
observe. The same trace shows every config write to `00:05.0` is BAR sizing,
the Command register, and the interrupt line. Nothing writes a power register
at suspend time.

That last point is worth keeping on its own: Windows 98's idle "suspend" of
this controller is a software halt with the device left in D0. The D3
objection that made a poll's MMIO read look unsafe does not hold here, and
it is also why a Device Manager Refresh recovers cleanly rather than needing
a power-up path.

### And then the whole framing turned out to be wrong, because the question was

Everything above answers "how does a driver wake a sleeping controller", and
each answer was correct and each was a dead end. The user asked a different
question, "can the sleep be prevented at all?", and the answer was sitting in
the same binary the rest of this entry was derived from.

`USBPORT.SYS` contains the literal strings `HcDisableSelectiveSuspend` and
`DisableSelectiveSuspend`. Both are live reads:

- `HcDisableSelectiveSuspend` is read per controller from the driver key (VA
  `0x11C10`; `push 1` = the driver/software key, `push 34h` = the name's 52
  bytes), returning TRUE when the value is absent or zero.
- `DisableSelectiveSuspend` is read globally via
  `RtlQueryRegistryValues(RelativeTo = Services, L"usb", ...)` at VA
  `0x11DBE`, in a table beside `UsbBIOSx` and `DisableCcDetect`.

Their single caller needs both permissive to set flag `0x800` in the device
extension. Setting the per-controller value alone changed nothing (the
suspend still arrived), which falsified `0x800` as the gate and would have
ended the investigation if the two values had been treated as one lever. But
the global read sets a second flag (`0x08000000`) the per-controller one
never touches, which made it a different experiment rather than a repeat.

Measured on the 2a guest, one boot each:

| | `SuspendController` | `USBCMD` | hot-plug while idle |
|---|---|---|---|
| neither value | 1, within seconds | `0x00000000` (halted) | invisible until Refresh |
| `HcDisableSelectiveSuspend = 1` | 1, within seconds | `0x00000000` | (not retested) |
| `+ Services\USB\DisableSelectiveSuspend = 1` | 0 | `0x00000005` (R/S, INTE) | enumerates on its own |

`SlotsEnabled` 1, `DevicesAddressed` 1, `OpensTotal` 2, wizard raised with no
Refresh, and `CheckCallbacks` climbing continuously instead of freezing. So
the defect is fixed by one `AddReg` line, on the Windows 98 path only, with
no driver code at all.

### Rules

- "How do I recover from state X" and "can I avoid state X" are different
  questions, and the first one crowds out the second. Three candidate
  mechanisms were derived, objected to, and killed, correctly, while the
  cheaper question went unasked for the whole investigation. When a
  derivation keeps closing doors, check whether the room was the right one.
- A pair of registry values with near-identical names is two experiments,
  not one. The per-controller value failing is what looked like a refutation
  of the whole idea; the global one differed in a flag bit, and that bit was
  the reading.
- The strings were in a binary already disassembled twice in the same
  session. A `strings` pass over the other side is minutes of work and was
  not taken until prompted. It belongs beside "derive the mechanism from the
  binaries" rather than after it.
- The surviving driver-side mechanism (a PME-based wake, or a timer the
  driver owns rather than borrows) is still named and still carried to real
  hardware. The setting fixes the defect on this stack, and does not make the
  xHC able to wake itself.

### Vehicle facts established in the same session

- This Win98 guest cannot be restarted from inside: a Start -> Shut Down ->
  Restart leaves the next boot unable to start. Shut down, close QEMU,
  relaunch. Every stage sheet that says "reboot" costs a relaunch here.
- Attaching a device to a standalone `usb-ehci` needs a High-Speed device;
  `usb-kbd` is HS in QEMU and attaches. There is no companion controller to
  take a Full-Speed one.
- Cancelling the Add New Hardware Wizard leaves the devnode, so a second
  attach of the same device does not re-open the wizard. The wizard is a
  one-shot oracle; the register read is the repeatable one.

## Host-side traps from batches 11-A, 11-B and the pre-run pass, none of them about hardware

Environment: host only, no guest. Recorded because each one produced a green
result that was wrong, or a red one whose cause was nowhere near the symptom.

- A page-sized stack local in a driver fails to link, and that is the
  compiler catching a real defect. The first `xhciLogFlush` staged the whole
  4 KB ring in one stack local; MSVC emits a `__chkstk` probe for a
  page-sized frame and the Win2000 DDK's driver libraries do not provide one.
  A driver has about 12 KB of stack, so the unresolved symbol was the correct
  answer to the wrong code. The drain became a 256-byte FIFO take, which also
  decoupled the ring's size from the flush's.
- PowerShell's `return ,$found` wraps, so an empty result comes back as a
  one-element array holding an empty array and `$hits.Count` reads 1. An
  INF-gate rule was therefore structurally incapable of firing; its own
  self-tests are what caught it. Any gate rule whose "no hits" path has never
  been exercised is suspect.
- A backtick inside a double-quoted PowerShell string is an escape, so a
  literal `` `remove` `` in a legend expanded to a carriage return and split a
  header into a line that was neither comment nor row. Netted afterwards by a
  self-test asserting every non-comment line's first field is a known row
  type. The general form is that emitters need a structural check on their
  own output, not only on their inputs.
- `.cmd` files written by an agent default to LF, and `rem`/`for`/caret
  continuation in `cmd.exe` is not reliably LF-safe. Convert to CRLF and then
  execute the file against stand-ins rather than reading it.
- A hand-maintained offset table goes stale silently and reads every counter
  from the wrong place while looking perfectly plausible.
  `scripts\local\offsets.txt` still described a 70,608-byte extension against
  a 74,776-byte driver. `readcounters.ps1` now merges the derived
  `scripts\vm-matrix\offsets.txt` on top of it, refuses a field-offset
  disagreement outright, and prints the `SIZEOF` it is working from, which is
  the number to check against the trace's `MiniPortExtensionSize` before
  believing a single counter.
- The pre-run pass before stage A found the same table stale again, by four
  kilobytes of extension, which would have misread every counter in the first
  stage that used it. The same pass falsified the two-controller command line
  task 11-V.2 asks about guestless, against a paused QEMU with no boot spent:
  both `qemu-xhci` instances instantiate and a device attaches to the second.
  Both are the same rule: check the instrument and the vehicle on the host
  before a guest boot is paid for.

## Phase 10's device matrix: what an unattended harness measured about the two guests, and the vehicle traps it hit

Environment: this development host (QEMU 11.0.92, winget) and
`MINIS-W11P-YKM` (QEMU 11.0.0, scoop); both target VMs. The verdict record is
`docs/contributing/design/06-device-matrix-verdict.md`; these are the parts
that generalise beyond that harness.

### Guest behaviour

- Windows 98 keys a USB devnode by bus location. A device class taught at one
  root port is not recognised at another; it raises a fresh modal Add New
  Hardware Wizard that blocks the bind indefinitely. Pinning the device under
  test to one port and teaching it there turned two rows from `NODRIVER` to
  `PASS` with no driver change and no new install. Before the pin, which rows
  passed depended on where QEMU happened to place devices in an earlier pass,
  which is a test harness measuring itself.
- That modal wizard blocks the bind and silently kills a keep-alive. A
  boot-attached `usb-mouse` reached `devices addressed`=1 and `slots
  enabled`=1 while `endpoints opened` stayed 0 for 776 s with the CPU
  perfectly alive, and `mouse_move` only reaches the wire once a function
  driver holds the interrupt endpoint, so the next hot-plug is invisible too.
  Windows 2000 claims the identical device in ~26 s unattended.
- `StartController` is reached ~8 s into a boot, long before the desktop:
  "the driver started" is not "the guest is ready". Readiness has to be the
  guest's own signal.
- Win98 writes `SYSTEM.DAT` lazily and keeps `SYSTEM.DA0`, so a killed guest
  can roll back and undo a confirmed-working install, measured between two
  boots. Any prep pass must end in a clean shutdown.

### Vehicle traps

- `-netdev` on the QEMU command line suppresses the default NIC, and the
  resulting PCI layout change stops Windows 2000 starting an
  already-installed driver: zero-byte debug console, trace stopping 3 s in,
  boot ending at a wizard. Bisected: `-netdev` alone reproduces it, `-drive
  if=none` alone does not. Add backends through the monitor instead.
- A short `x/32wx` reply, mapped positionally with the word count unchecked,
  silently misfiles every later counter. Measured: `devices addressed
  +3777495686`, values shaped `0x8180xxxx`, kernel addresses filed under
  counter names. Check the word count, not the plausibility of the values.
- A refused monitor port is a dead QEMU process, not a dead guest. A Win98
  bugcheck leaves QEMU running and answering: a screenshot and a full
  299-counter read were both taken out of a bugchecked guest. The same
  blindness sat in three places at once, because a helper that swallowed a
  connect refusal returned an empty reply to every caller.
- A liveness probe calibrated on one target is not calibrated. Sampling `EIP`
  gives 2-6 distinct values of 12 on an idle 2a and 1 on 2b. And `info rtc`
  does not exist in QEMU 11, whose unknown-command reply compares equal
  between two samples, so the probe voted "dead" every time.
- QEMU auto-deletes a `-drive` backend when its device is unplugged, so two
  rows sharing one backend means the second can never get it. QEMU's own
  wording is the discriminator: `already in use` = held, `can't find value`
  = gone.
- A `usb-hub` creates no bus. A child is a hierarchical port path
  (`port=2.2.1`), not `bus=<hub>.0`; confirmed against a guestless QEMU.
- `usb_version=1` presents a model at 12 Mb/s and `=2` at 480, which is the
  only way this vehicle puts a Full-Speed HID on a root port; no Low Speed
  model exists in this build at all. `pcap=` is on the USB device base class,
  so every model has it: a host-side wire oracle needing no guest agent.

### Method

- A failure with a confident explanation is not re-read. A row blamed on
  `USBAUDIO.VXD` had in fact ended with the QEMU process dying; asking what
  the reading would have been if the blamed thing had not happened settled it
  in one step (`iso submits` had never left 0, and that fault needs a
  completed URB). When the class was later taught, the real fault reproduced
  at a byte-identical VxD offset across two load bases.
- To test a "what happened to the guest" path, make it happen. Killing QEMU
  mid-row on purpose is what found the second and third sites of the same
  blindness.
- A group that was attempted is not a group that ran. A filter matching
  nothing runs nothing and exits 0, and a checkpoint ticked off it is ticked
  off zero rows.

## Task 9-V.2: QEMU has two diagnostic channels, and a script watching the wrong one sees silence

Environment: host-side, 2b only. The full result is that the `usb-host`
passthrough rung is shut for IAD-grouped multi-interface functions, measured
upstream of every guest. The reusable half is the instrument:

- `error_vprintf` goes to the current monitor for a message caused by a
  monitor command, and to stderr only otherwise. A helper filtering QEMU's
  stderr for `Error|failed` therefore discards the messages its own
  `device_add` provoked, silently, because the filter matched nothing rather
  than erroring.
- This build prints nothing at all when a libusb interface claim gives up, so
  the absence of a message is not the absence of a failure. Walking all 16
  claim slots and counting how many succeeded (2 of 4) is what produced the
  evidence; a log read would have produced none.

## Task 9-0.2 - a delta fold bracketed around one path is silently lossy, and the reassuring zero is the one to check hardest

After task 9-0.2 moved the only tail-recording site into the settle pass, the
delta fold bracketed around `XhciXferEvent` could never move:
`MidTdTailsDroppedTotal` read 0 and `MidTdVerdictVoided` was dead with it. It
was found by arithmetic on the first run's own numbers rather than by any
counter: 607 retires, 0 claimed as tails, 0 censored, 4 alive at the
teardown, so 603 records had gone somewhere nothing named.

Fixed with `xhciDevFoldTailsDropped`, used at both sites so the two cannot
drift, and folded on every exit of the settle loop. On the re-run 760 (2b)
and 757 (2a) were each exactly `retires - 4`, and the four-state identity
`retired == tails + dropped + censored + outstanding` closes to zero after
the teardown fold on both (`764 = 0 + 760 + 4 + 0`, `761 = 0 + 757 + 4 + 0`).
The debug build's own channel corroborates it (`mid-TD tail records evicted
unanswered` climbing from the first transfers, a line that could not appear
before the fix). No new import and no size change, so the build stamp, not
`SIZEOF`, discriminated the two binaries on the second boot.

The general rule: a delta fold bracketed around one path is silently lossy
for a counter whose movers live on two paths that do not nest. The repo
already had "a fold assembled site-by-site is only as complete as whoever
assembled it" (task 7b-A.1.0); this is the same failure reached by moving a
mover rather than adding one, which no net over the existing sites could have
caught. It survived a nine-round review loop and a mutation sweep because the
wrong value was the reassuring one: a zero in a gate counter reads as
"nothing invalidated the verdict". A zero that confirms what the change hoped
for is the one to check hardest.

## Task 9-A.3: a budgeted trace channel cannot support a negative, and the callback I blamed was the teardown

Environment: no VM and no bench. Both shipping `usbport.sys` builds
(`tools/{nusb,win2ksp4}-extracted/`) disassembled with the Win2000 DDK's own
`link -dump -disasm`, plus a re-read of the batch 9-V evidence already in
`vm/`. The derivation and its conclusions are in
`docs/usb-xhci-info/usbport-miniport-abi.md` section 4; this records what is
reusable.

### The instrument decided the finding, and it was the wrong instrument

Batch 9-V read `InterruptNextSOF` out of a debug-build trace, saw it only in
the passthrough boots, and wrote "it had never fired in this project before"
into the roadmap, a memory entry and this file. The whole inference chain
that followed ("usbport asks for it only on some isochronous path a real
device takes", "the emulated device takes a different path through usbport",
"this driver answers that path with a stub") rests on it, and it is false.
`XHCI_DBG_CB` is budgeted per site (`XHCI_DBG_CALL_LIMIT`), so a site that
fires forty thousand times prints as many lines as one that fires four times.
`grep -c InterruptNextSOF vm/*.log` answers in a second: every debug-console
log from batch 6-V onward, on both targets, contains it, emulated device
included.

Rule: a budgeted, deduplicated or sampled channel can say "this happened"; it
can never say "this did not happen". The repository already had the counter
form of this remedy (`InterruptFlushes` exists so a release build can confirm
a call site read out of a disassembly) and it was not reached for, because
the trace did print something and a printing channel feels like a measuring
one. Before writing a negative, ask what would have to be true for the
channel to print the same thing either way. If the answer is "a cap", the
negative is not evidence.

### The correction contained the next finding, and the counter-evidence was already in it

The first draft of this entry replaced the retracted negative with an equally
unfounded positive: "it fires on every miniport-visible endpoint state
change, at exactly the same count as `SetEndpointState` because both sites
are capped." Review refused that sentence (independently budgeted sites
saturating together do not imply pairing), and re-deriving settled it:
`SetEndpointState` has four call sites per build and only one ends in
`InterruptNextSOF`. The other three (SP4 `000249F9`/`00024C92`/`0002765C`,
NUSB `0002437B`/`00024614`/`00026FD4`) load the same wrapper pointer into
`ecx` or `edx` rather than `eax`, push a literal state, call the miniport
directly and queue nothing.

Two rules, and the second is the sharper one.

Grep a slot across every base register. A pattern anchored on `[eax+` found
one site of four. This is batch 6-0's `PassThru` trap again, a negative
concluded from a search that could not have shown the positive, committed in
the very task whose hand-off warned the reviewer to check for it.

The measurement that refutes a claim is often already in the evidence, being
explained away by the claim. 11 of the 43 logs show the two printed counts
differing (`n = 2, s = 4` in all five passthrough boots). Under a genuine
one-to-one pairing those counters can never separate, so the divergence was
a refutation sitting in plain sight, and the draft cited the cap to account
for the 32 logs that agreed while treating the 11 that disagreed as noise.
When a rule explains most of the data, the exceptions are the test, not the
residue.

Nothing in the verdict moved: the three extra sites queue nothing onto the
state-change list, so they neither need the callback nor bear on whether a
stub is legal. What moved is a claim about frequency that two counter sites
and three documents had already been written against.

`InterruptNextSofRequests` now exists for this callback, and
`docs/contributing/design/03-host-unit-tests.md` carries its row.

### The callback in the trace was the teardown, not the trigger

The task text warned "do not assume the callback is the cause", and the
reason it was right is visible in the same logs once the budget is accounted
for: `InterruptNextSOF` appears twice per passthrough boot. The first is at
`SetEndpointState(state = 3, ACTIVE)` early in enumeration, after which the
device enumerated normally for a dozen more control transfers. The second is
at `SetEndpointState(state = 4, REMOVE)`, usbport already tearing the device
down. The trigger is ~20 lines earlier and is not a callback at all:
`SET_CONFIGURATION(1)` completing with completion code 6, Stall Error, EP0
halting, the driver resetting and re-dequeueing it correctly, usbport
retrying once and giving up.

Rule: when a trace line sits immediately before a failure, check whether the
same line also appears somewhere the failure did not follow. Here it did, in
the same file, forty lines up. That check is cheaper than any derivation and
would have re-scoped the task before it started.

### "Nothing waits on it" was a claim about the wrong object

The third round found the sharpest version of this task's recurring error.
"usbport does not wait on `InterruptNextSOF`" is true and was checked
properly: no event, no timeout, no return value read, the spin-lock release
is the next instruction at all four sites. It was then written down as
"nothing waits on it", which is a claim about something else entirely, and
that one is false. SP4 `000256DA` (endpoint removal path; NUSB's counterpart
reads `[esi+30h]` at `00025476`) runs an uncapped 1 ms poll loop at PASSIVE
whose exit test is `Endpoint->StateLast == Endpoint->StateNext`, made true
only by the walker, with `USBPORT_Wait` as the sole call in its body and no
iteration cap.

Rule: "X does not wait on the callback" and "nothing waits" differ by a
search you have not done. The first is a statement about four instruction
sites and is cheap to verify. The second quantifies over the whole image, and
the way to earn it is to enumerate the wait primitives and check each one:
here, `grep` the single `KeDelayExecutionThread` wrapper, find its 28
callers, detect which sit in loops, and test whether any loop's termination
depends on the state the callback accelerates. One of fourteen did.

The verdict survived, but its support changed shape: the stub is legal
because the walker has a second driver that owes nothing to the callback,
not because nobody is waiting. That promotes the 500 ms timer from a
convenience to the element the argument rests on, and with it the two open
questions about when that timer is armed and whether it can be cancelled.

### Attributing the STALL: a localization, after a review round refused the exclusion

The first draft of this section claimed the STALL was "the vehicle's, not the
device's" on three readings. A review scoped to that attribution alone
returned two majors and the categorical form did not survive. What follows
is the corrected version; the original three legs are kept because two of
them still carry weight and the third is instructive about how it failed.

What the capture shows, and the mechanism that breaks the argument: QEMU does
not forward `SET_CONFIGURATION` to a passed-through device at all in the
ordinary case. `usb_host_handle_control` calls `usb_host_set_config`, which
releases the interfaces, calls `libusb_set_configuration` only when
`bNumConfigurations != 1` (mapping any nonzero return to `USB_RET_STALL`),
and then calls `usb_host_claim_interfaces`, stalling if it cannot read the
active configuration or claim every interface. The pcap records neither the
branch nor the libusb error. So the honest result is that the failure is
surfaced by QEMU's set-configuration/interface-claim path, a localization,
and the physical device is not excluded.

That is the same mistake the batch 8-V.2 entry below ("What the control
cannot see") already records: name every component on the failing path
before declaring what a control proved. Here the unnamed component was inside
QEMU, one layer below the boundary the capture sits on.

The leading hypothesis is now sharper and testable on a bench: the device is
a composite rebound with Zadig/WinUSB, and `usb_host_claim_interfaces` must
claim every interface. A rebind leaving any interface with another driver
would stall without the request ever reaching the wire, which also fits the
single-function ASIX adapter working in batch 8-V.2. Hypothesis, not finding.

What did survive is the half that matters for this project: this driver's TD
shape is ruled out by direct comparison. The failing transfer's `probe.xfer`
inputs are byte-identical to a succeeding `SET_CONFIGURATION(1)` against the
emulated device and to the HID, storage and Ethernet cases, and
`src/xhci_xfer.c` builds every zero-length control request into the same
two-TRB group. QEMU also received the correct eight setup bytes.

The three original legs, re-weighed:

1. QEMU's own `pcap=` capture (`vm/2a-9v-realdev*.pcap`, DLT 220) records a
   completion record with an error status for exactly the `SET_CONFIGURATION`
   control transfers and for nothing else in the enumeration. QEMU produced
   the failure below the guest.
2. The same guest, the same binary and the same emulated xHCI complete
   `SET_CONFIGURATION(1)` against the emulated `usb-audio`, and have done so
   for every storage, HID and Ethernet device this project has enumerated.
   The zero-length control-OUT path is not the variable.
3. `SET_CONFIGURATION(0)` stalls too, in the pcap and, independently, in 2a
   boot 3's own driver trace (`00000900` where the other four boots carry
   `00010900`), found only because the setup packet was checked on every boot
   rather than on the first. Weakened: the draft said "no conforming device
   stalls that request: it is mandatory and always valid", and the normative
   text does not support the absolute. USB 2.0 section 9.4.7 leaves the
   behaviour unspecified in Default state, and section 9.4 allows a device
   whose Default Control Pipe has become unusable to require a reset first.
   It is not nothing: in `2a-9v-realdev2.pcap` the stalling
   `SET_CONFIGURATION(0)` at record 10 is not preceded by any configuration
   attempt, records 0-9 being the enumeration's own GET_DESCRIPTORs from
   devnum 0. But the pcap-1 instance does follow a failed
   `SET_CONFIGURATION(1)`, so it is not the clean independent test it was
   presented as.

Reading 3 was called the decisive one and the move behind it is still right:
when a peripheral appears to refuse a request, look for a variant of that
request it could not possibly refuse. The execution was not. "Could not
possibly refuse" is a claim about a specification, and it was made from
plausibility rather than from the text. A leg built on "no conforming X ever
does Y" needs the sentence that says so, and needs its preconditions checked;
here, the device's state at the time of the request, which nothing in the
capture establishes. That is the same error as Phase 5's round thirteen,
recorded in this file, committed again on a different subject.

Correction of record from the same round: the pcap's snaplen is 4160, not 64
as an earlier reading of these files stated. The control records are 64 bytes
because QEMU writes them header-only by design, so the setup payload cannot
be recovered by re-capturing with different settings.

### And the thing the derivation found that nobody was looking for

usbport's endpoint state-change list is drained only when
`Get32BitFrameNumber()` is strictly greater than the frame stamped at
`SetEndpointState`, and an endpoint that fails that test goes back on the
head of the list and aborts the entire pass. So a miniport whose published
frame number stops moving blocks every other endpoint's state change too, on
both the interrupt and the timer path. Batch 6-0 justified "the stall path
must advance" from usbport's uncapped post-open wait; this is a second,
independent reason for the same property, and it is a worse failure mode. The
callback that names SOF is the advisory half; `Get32BitFrameNumber` is the
half with teeth.

## Batch 9-V: what an audible fault is evidence of, and three claims I made from one observation each

Environment: host `fw-w11p-ykm`, QEMU 11.0.0, both target VMs, one binary
(debug, `built Aug 10 2026 15:54:15`, `MiniPortExtensionSize=0x000113CC`
checked against `offsets.txt`'s `SIZEOF 70604` on every boot before a counter
was read). Launchers `scripts\local\qemu-win{98,2k}-run-9v.cmd`. Full results
are in `build-and-test.md`, "USB Audio in QEMU: what the vehicle can and
cannot show"; this records what is reusable.

### One emulated device is one device, and it cannot reach the paths a real one takes

The batch was declared "playback met on Windows 2000" on three independent
oracles: ears, a continuous 120 s host capture, and 250,330 packets with zero
errors. Every one of those measurements used QEMU's emulated `usb-audio`.
Attaching a physical class-compliant device (`041e:323d`, passed through with
`usb-host` after a Zadig/WinUSB rebind) overturned the conclusion within the
hour: it enumerates, our descriptor walker parses 6-9 real isochronous
declarations off it with none malformed, and then usbport calls
`SetEndpointState` and `InterruptNextSOF` and abandons the device, disabling
its port, deterministically, on two consecutive boots.

The first reading of that trace concluded `InterruptNextSOF` had never fired
in this project before and that the emulated device took a different path
through usbport. Task 9-A.3 retracted that the same day (see the entry
above): the callback is in every log from batch 6-V onward on both targets
and is triggered by the emulated device too, and the trace channel is
budgeted, so its silence measured nothing. The conclusion of this section
stands on different evidence: the real device did fail where the emulated one
did not, on a STALLed `SET_CONFIGURATION` that the emulated device never
produces.

The file already carries `qemu-emulated-devices-never-fail`: ~19,000 transfer
events across four runs producing zero error completion codes, with every
error path ever exercised coming from `usb-host` passthrough. This is the
same lesson one level up. It is not only error paths that an emulated device
fails to reach, it is whole callback sequences.

Rules: before a clause about device behaviour is called met, ask which
devices could have exercised it and whether any of them was real. And when a
scoping doubt is written down for one target (this file had already scoped
the Windows 98 result to "in this vehicle"), apply it to the other target in
the same breath, because the reason is the vehicle and not the guest.

### An audible artefact is not evidence about the transfer path until something measures the transfer path

Windows 2000 played a 120-second tone through this driver with an obvious,
intermittent stutter. That is what a broken isochronous scheduler sounds
like, and it was not one. The host-side capture of the same stream was
continuous, 120.02 s with zero silent windows of any kind, while the
driver's own block read 250,330 packets answered with `IsoMissedServiceTotal`
0. The samples reached the device; what stuttered was downstream of the
mixer, in the host's DirectSound sink.

Three configurations separated it, and the ordering matters: the retail
control (`usbuhci.sys`, our driver out of the path) stuttered less, which
looked like evidence against us, and the release build of our own driver
stuttered no more than the control. The difference was never the controller.
It was the debug build's `DbgPrint`, one VM exit per character out of port
0xE9, in the same host thread that has to keep the audio sink fed.

Rules: put an instrument on the path before believing an ear, and measure
what was delivered rather than what was played. When comparing your driver
against a shipping one, check that the two sides differ only in the thing
being compared. A debug build against a retail binary is not a controlled
comparison, and here it produced a difference big enough to have been
written up as a defect. On this vehicle the debug build cannot be used to
judge audio quality at all.

### A counter that reads zero for a third reason cannot discriminate the two you had in mind

`IsoSubmitsWithFrameId` was documented as "one of the few readings where the
two targets are expected to differ by design": 0 on Win98 because
idle-suspend stalls the published frame axis, most of them on Win2000. It
read 0 on Windows 2000 with a congruent axis and no stalls.

The cause was found by reading the hardware rather than the driver:
`HCCPARAMS1 = 0x00087001`, so bit 11 CFC = 0. QEMU's xHC implements no
Contiguous Frame ID Capability, `Frames.Allowed` is never set, and the driver
correctly never names a frame, on either target, in every topology. The
prediction was not wrong about the targets; it was written without asking
whether the vehicle could express the difference at all.

Rule: before reading a counter as a differential between two configurations,
establish that a third, common cause cannot be pinning it. A register read
settles this faster than any amount of guest-side reasoning; `xp /2wx
<BAR0+0x10>` through the monitor needs no driver at all.

### Windows 98 SE cannot play USB audio here, and the control is what says so

Windows 98's audio stack failed five times out of five, in three shapes, and
never once inside this driver's transfer path. `USBAUDIO.VXD` faults after
exactly one 10 ms URB: `exception 00 in VXD USBAUDIO(01) + 00002ED4` through
`xhci98.sys`; `exception 0E at 0028:FF045748` twice through a UHCI controller
driven by Windows 98's own USB 1.1 stack, the same address to the digit; once
a ring-0 wedge instead; and once, with the device behind a hub, a PnP wedge
in which the device never bound at all (no "Sound, video and game
controllers" category, `iso submits` 0). In the three UHCI runs this driver
was idle-suspended with every isochronous counter at 0, and the host capture
stalled at a byte-identical 1,916 bytes whenever audio crossed at all.

Three things make that conclusive rather than suggestive. It is an
exoneration on the same target, the standard the `0028:C00312EE` teardown
bugcheck set, not a cross-OS differential. The control excludes the standing
suspect by construction: UHCI reports Full Speed honestly, so if Phase 5 task
7's High-Speed root-port report had been the cause, the control would have
played. And the behind-hub run removes that same suspect a second,
independent way, since usbport learns the true speed through a hub (which is
what took `IsoCadenceMismatches` to zero on Windows 2000), and it failed too,
with this driver's route, TT and descriptor snoop all demonstrably correct
first.

Rule: when your driver is one of several plausible causes, prefer the control
that removes a specific hypothesis over the one that merely removes your
code.

### Three claims I wrote from one observation each, and all three were wrong or too strong

Recorded because the pattern is the point, not the individual errors.

1. "DirectSound is unavailable on this host." QEMU died on `-audiodev dsound`
   once; the conclusion drawn was that the host had no usable endpoint, and
   the operator's ears were written out of the run. The identical command
   line started cleanly three times minutes later. The reusable part is the
   trap, not the conclusion: a declared audiodev that will not initialise is
   fatal to the whole boot because QEMU does not fall back, and which endpoint
   Windows currently calls the default is a host condition that changes under
   you.
2. "No audio control controller is possible on Windows 98." The UHCI control
   stopped the boot asking for `uhcd.sys`, `C:\WINDOWS\OPTIONS\CABS` did not
   have it, and a launcher header was written explaining why the control was
   structurally unavailable on that target. It needed the Windows 98 SE CD
   attached, which the host had. "I could not find it" is not "it does not
   exist", and a header that argues from the first to the second is worse
   than no header.
3. The bugchecks, from one occurrence each. Both faults were recorded before
   either had been reproduced. Reproduction then made the finding much
   stronger than the write-up had claimed: identical fault address,
   byte-identical capture, and a third shape (the wedge) that a single run
   would have hidden.

Rule: a single observation of an environmental condition is a reading, not a
property. Say which one you have, and reproduce before a negative goes into
a document that later work will rely on.

### Vehicle facts worth not rediscovering

- Every QEMU USB Audio model is Full Speed, so a standalone `-device
  usb-ehci` cannot host one at all (`speed mismatch ... to bus ehci.0`); a
  bare EHCI has no companion. Audio controls must be UHCI or OHCI. The 8-V
  storage controls could use EHCI because mass storage is High Speed.
- `usb-hub` is the only hub model and it is USB 1.1, so "behind a High-Speed
  hub" is a Phase 13 clause for the same reason Low Speed is.
- No reachable audio device declares `bInterval > 1`, read off the wire with
  `pcap=` on the device rather than assumed: the plain model and all three
  `multi=on` alternates (`wMaxPacketSize` 192/576/768) declare 1.
- `wavcapture` gives ears and a file at once, tapping the same mixer the
  backend drains. Use it rather than choosing between them.
- Validate monitor syntax and device combinations against a guestless paused
  QEMU (`-S -display none`) before spending a boot. That is how the EHCI
  speed mismatch, the `bus=hub1.0` error (hubs take `port=3.1`, not a bus
  name) and the dsound failure were all found for free.

## Batch 8-V.1's last clause on 2b: a false pass that the counters caught, a cache that ate the whole test, and a negative built on the wrong counter

Environment: host `fw-w11p-ykm`, QEMU 11.0.0, 2b Win2000 SP4 under Driver
Verifier with Force IRQL checking, binary `built Aug 9 2026 11:06:35`
(`MiniPortExtensionSize=0000C4F4`, checked against `offsets.txt`'s `SIZEOF
50420` before any counter was read). Launcher
`scripts\local\qemu-win2k-run-8v1.cmd`, medium `vm\usb8v-2b.img`. Evidence
`vm\8v1-2b-readunplug-pass\`. The clause passed and batch 8-V.1 is complete
on both targets; results are on the roadmap's 8-V.1 clause line. The driver
was never in question: `xfer_start` 24,325 = `xfer_success` 24,325 with no
`xfer_error` line at all, and every orphan/residual/overrun counter 0.

### A counter that names the other side's reply cannot tell you whether your own code ran

Three roadmap boxes recorded that task 8-A.3's
stop-endpoints-before-Disable-Slot ordering "has never been exercised on a
target", calling it a firm negative across three device classes. It rested
on `EndpointStoppedEvents` = 0.

That is the wrong counter for the question. `EndpointStoppedEvents` counts
the xHC's Stopped Transfer Event, the controller's reply to a Stop Endpoint
command. `EndpointStops` counts this driver issuing one. Zero in the first
does not imply zero in the second, and the discriminator was sitting in the
evidence directories the whole time as `usb_xhci_ep_stop`:

| leg | `usb_xhci_ep_stop` | `usb_xhci_ep_set_dequeue` |
|---|---|---|
| 2a storage read-unplug | 0 | 0 |
| 2b storage 08-08 | 0 | 0 |
| 2b Ethernet unplug | 1 | 1 |
| 2b storage read-unplug | 1 | 1 |

Both 2b legs show the required order from QEMU's own side: cancel ->
`port_link pls 5` -> `ep_stop` -> `ep_state running -> stopped` ->
`ep_set_dequeue` -> `slot_disable` -> `ep_disable`. The ordering is exercised
and correct. What is still unobserved is a Stopped Transfer Event, because
QEMU cancels the packet before the stop and leaves no active TD to report
against, a property of the vehicle, not a gap in the driver.

The generalisable rule: before recording a negative, check that the counter
you are reading is on your side of the interface. The negative here was
repeated into two later boxes on the authority of the first, and each
repetition made it sound better established.

### A monitor command whose reply is never read can report a confident false pass

`unplug-8v.ps1`'s `Send-Mon` writes `device_del` and closes the socket 150 ms
later without reading anything back. At 15:54 it printed

```
FIRED at 15:54:42.161
  READ commands before the pull : 1,759  (+376 since baseline)
```

and the device was still attached. `info usb` still listed `Device 1.1, Port
1, Speed 480 Mb/s, Product QEMU USB MSD, ID: stor`.

The driver's counters are what exposed it, because they disagreed with the
script and agreed with the device: no `SlotsDisabled`, no `DevicesTornDown`,
`Dev0_State` still 4 on root port 1, and submitted == completed with nothing
cancelled. A teardown that did not happen leaves that signature, and it is
only visible if the reconciliation is read before the run is written up.

Rules: never fire a state-changing monitor command without reading its reply,
and a pull is only a pull if the device left. Confirm with `info usb`; do not
infer it from the command having been sent. `pull.ps1` in the evidence
directory does both and supersedes `unplug-8v.ps1` for this purpose.

### A guest file cache can absorb an entire I/O test while the guest reports doing it

`8V1.BAT` was sized against Win98. On a 256 MB Win2000 guest its 24 MB
rotating working set fits entirely in the file cache: the guest completed all
24 copies and reported reading 192 MB, while `usb_msd_cmd_submit` moved 0
across the whole pass and the device had seen 8.8 MB in the entire boot.

The saturation point was then measured rather than assumed. Filling the
medium one 8 MB file at a time, the per-file cost jumped from ~140 to ~260
msd commands at `BIG22`, the copy source beginning to be re-read from the
device every iteration. That puts the Win2000 cache at ~170 MB on a 256 MB
guest, so the run used ~248 MB of distinct data.

Rule: a read clause needs a working set larger than the guest's cache;
issuing a lot of `copy` commands is not the same thing. This is the same
shape as the `removable=on` trap below, every layer reporting health while
nothing reaches the device, and it is the second time in one session that a
batch 8-V.1 clause was blocked that way.

### Tag-match the cancelled command; it is the strongest form this clause can take

The aggregate direction control was +235 qualifying reads against +1 write.
Better than that, the cancelled command is identifiable:

```
usb_msd_cmd_submit lun 0, tag 0xaf680e70, flags 0x00000080, len 10, data-len 65536
usb_msd_cmd_cancel tag 0xaf680e70
```

Data-IN, 10-byte CDB, 64 KB: a `READ(10)`. That settles "a read was in
flight" without any argument about what else the guest was doing, which
mattered here because the read traffic turned out to come from a different
loop than intended. It costs one grep. Prefer it to a window count on any
future unplug leg.

### Two console traps, and a keyboard layout

- The 2b guest keyboard is US-Dvorak, previously recorded only for 2d.
  QEMU's `sendkey` names keys by their US-QWERTY label and the guest applies
  its own layout, so injected text arrives Dvorak-shifted: `echo` lands as
  `.jdr`. `sendtext.ps1` in the evidence directory carries the inverse map.
  Verify a typed command on screen before pressing Enter; that is what caught
  it. Expect the same on 2d.
- `Ctrl+C` does not kill an interactive cmd.exe `for` loop here. It skips
  iterations and the loop resumes, observed continuing `BIG120` -> `BIG153`
  -> `BIG261` across two rounds. Close the console window instead.
- Keystrokes injected into a busy console land in the type-ahead buffer and
  are echoed at the prompt looking as though they had run. Two attempts at
  starting the read pass were lost that way. The trace is what showed it:
  the reads present were the other loop's copy-source reads. Confirm from the
  trace that the traffic you intended is the traffic you are seeing.

## Batch 8-V.1's owed halves on 2a: the clauses passed quickly, the vehicle cost the session

Environment: host `fw-w11p-ykm`, QEMU 11.0.0, 2a Win98 SE, binary `built Aug
9 2026 11:06:35` (`MiniPortExtensionSize=0000C4F4`). Launcher
`scripts\local\qemu-win98-run-8v1.cmd`, medium `vm\usb8v-2a-run2.img`.
Evidence `vm\8v1-2a-dirclause-pass\`. Both owed halves met; results are in
the roadmap's 8-V.1 clause lines. Everything below is what the run taught,
and almost none of it was about this driver, whose counters were clean
throughout (`xfer_error` 0, submitted == completed, every
orphan/residual/overrun counter 0).

### A launcher that records the backend but not the device properties does not record the vehicle

`usb-storage` needs `removable=on` or Win98 gives the disk no drive letter.
QEMU defaults to `removable=false`, so the guest sees a fixed disk; both 8-V
media are superfloppies (FAT boot sector at LBA 0, no partition table), a
fixed disk therefore has no partitions, and Win98 creates no volume.

What makes it expensive is that everything underneath reports perfect
health: `USB Mass Storage Device` under Universal Serial Bus controllers and
`USB Disk` under Storage device, both "This device is working properly", no
yellow bang, and a clean SCSI exchange (INQUIRY -> TEST UNIT READY -> READ
CAPACITY -> READ(10) of sector 0, every status 0). There is no error anywhere
to follow.

`qemu-win98-run-8v.cmd` declares only the `-drive` backend and leaves
`-device usb-storage` to a hand-typed monitor `device_add`, so the flag lived
in a command nobody wrote down. When a documented result will not reproduce,
check whether the setup is really the one that produced it before doubting
the code. This repo keeps screenshots for that reason:
`vm\8v-2a-wedge\8v-2a-t2.png` shows "Removable Disk (F:)" and had the answer
the whole time.

### A symptom invariant across three media is a statement about the attach

Three media were tried before the flag was found (an all-zero disk, the
half-torn 08-08 one, and a known-good `MSDOS5.0` FAT16 volume) and all three
behaved identically. That is the signal, and it was missed twice: the
variable already excluded is the one that kept being changed.

Two further diagnoses were made and then refuted by evidence, both worth
recording because each sounded right:

- "Win9x only assigns drive letters at startup, so the hot-add is the
  problem." Refuted: the 08-08 run hot-added the disk too (first
  `port_reset` at trace line 857, long after driver init) and mounted fine.
- "The receive fix regressed mass storage." A serious hypothesis, since the
  fix retires a TD early on a short packet and this was the first storage
  run on that binary; a truncated read would present with a clean CSW.
  Refuted by the right measurement: `MidTdShortRetiresTotal` is 0 on every
  storage boot, so the departure never fires on this path. Raise it, then
  close it with a counter rather than an argument.

### Win9x COMMAND.COM is not cmd.exe, and four assumptions cost boots

- `batchfile > log` does not redirect the commands inside the batch. Win9x
  applies the redirection to the interpreter's own launch, so every command
  still writes to the console and the log comes out 0 bytes. Redirect per
  command.
- `>` is stdout-only and there is no `2>&1`. A failing command's error text
  goes to the console and never reaches the log, so a log can look complete
  while steps inside it failed.
- `^` is not an escape character. A cmd.exe-style `^<DIR^>` inside an `echo`
  is parsed as redirection and dies with "File not found".
- CRLF is mandatory, for guest `.BAT` files and for the `.cmd` launchers; an
  LF-only launcher broke its `^` line continuations. Verify as bytes, do not
  trust the editor. This trap was walked into twice in one session, because
  the editing tool emits LF and nothing checks.

Also: `copy SRC DEST\` into a subdirectory did not work here.

### Prove a clause from the medium when the guest's own report is not capturable

Win98's DOS box cannot scroll, so the middle of a batch's output is
unrecoverable once it passes. The fix that worked: log to `C:`, copy the log
onto the USB volume at the end, and then, after the safe removal flushes it,
read the log out of the raw image from the host. Found at `0x1844200`. That
makes the flush clause self-proving: the evidence is bytes that reached the
medium.

FAT forensics alone were not sufficient for the directory clause, and it is
worth knowing why: a deleted entry keeps its name after the first byte, but
`rd` freed SUBDIR's directory slot and the next file created reused it,
erasing the trace. Consistent with create-then-remove, and equally
consistent with `md` having failed. The free-space arithmetic in the captured
listings is what settled it: one 4,096-byte cluster taken and returned
exactly.

### Two counters that mean something other than their name

- `usb_xhci_slot_disable` counts controller resets, not driver activity. The
  driver's own `SlotsDisabled` read 0 while QEMU logged 448, one per enabled
  slot (32) per reset (14). It is a sound restart-loop signature because it
  counts resets, but it is not a driver-behaviour counter.
- A frozen `HealthPolls` after a run is idle-suspend, not a wedge. The poll
  cannot advance on a halted controller and Win98 suspends within about a
  second of a quiet bus. Check the console for `SuspendController`, and check
  the screen, before calling it a liveness failure. This was briefly misread
  as the run failing when the guest was sitting healthily at its prompt.

Relatedly, Win98's safe removal is a logical dismount only: `SlotsDisabled` =
0 and `DevicesTornDown` = 0, with the slot still enabled and the device still
on the bus. Nothing should read those as a teardown signal.

### An instrument that measures "traffic" does not measure "a read"

`unplug-8v.ps1` counted every `usb_msd_cmd_submit`, which climbs for reads
and writes alike. That was adequate while only the unplug-during-write half
was being run and silently inadequate for the read half, where a pull landing
in a burst of writes would have produced identical output and quietly re-run
a clause that already passes. It now gates on the CBW direction bit the trace
already carried (`flags 0x00000080` read, `0x00000000` write) with
`-MinDataLen` so SCSI housekeeping cannot satisfy the threshold, and reports
opposite-direction traffic in the same window as a control. Validated against
`vm\8v-2b\`'s 2,018 commands: Read 108, Write 362, Any 2018, all three exact,
with `Any` left as the default so the recorded write-half evidence is
untouched.

Ordering rule: arm the watcher before the guest is driven. The first attempt
ran all 24 reads to completion before the watcher took its baseline, so it
had nothing to fire on, and the tempting explanation ("the reads were served
from cache") was invented to fit and was wrong. A baseline of 0 qualifying
commands is the sign the instrument is watching a quiet bus rather than
starting mid-stream.

## Batch 8-V.2 on 2b: the second target passes, and the install failure that looked like ours was a pruned vendor package

Environment: host `fw-w11p-ykm`, QEMU 11.0.0, 2b Windows 2000 SP4
(`pc,acpi=off`, `-cpu pentium3,-apic`), binary `built Aug 9 2026 11:06:35`
(`MiniPortExtensionSize=0000C4F4` = `offsets.txt` `SIZEOF 50420`), the same
physical ASIX AX88772A passed through with `-device usb-host`, cabled to a
switch with a DHCP server. Driver Verifier confirmed active on `xhci98.sys`.
Evidence `vm\8v2-2b-run2-traffic-pass\`, `vm\8v2-2b-run1-enum-wdfmissing\`.

Result: task 8-V.2 met on both targets. `epid 5` 196 starts under load,
`MidTdShortRetiresTotal` 281, zero `xfer_error`/`ep_stop`, real DHCP lease,
host ping 150/150 at 1400 bytes. Details in the roadmap's batch 8-V.2 boxes.

### The control path is a free negative control for the shape test

`ShortPacketsTotal` 293 split 281 retired mid-TD + 12 by the ordinary
positional rule, and the 12 are exactly `epid 1`'s 12 control-transfer short
packets. The new `wholeTdIsData && CC_SHORT` departure must fire on bulk
receives and not on a control transfer's Data Stage, and every run that
enumerates a device produces control short packets for free. When a change
is gated on a shape, find the shape it must not match in the same trace and
check the counts partition. "All of them retired mid-TD" would have been the
failure signature, and it is only visible because the two numbers were read
together.

### A submitted/completed gap is not a leak until the cancel and abort counters are read beside it

The mid-traffic `device_del` left `TransfersSubmitted` 4,614 against
`TransfersCompleted` 4,613. With traffic dead that is not a read race, and it
read as one unaccounted transfer for several minutes. It is
`TransfersCancelled` = 1: the in-flight receive cancelled exactly once, which
is what task 8-V.1's clause asks for. `readcounters.ps1 -Nonzero` does not
help here: the counter was nonzero and simply had not been asked for. Rule:
reconcile submitted against completed + cancelled + failed + aborted, not
against completed alone, and grep the header for the counter family before
concluding a number is missing rather than merely unrequested.

### Verify a driver package against its own INF before staging it

The 2b ASIX install failed at "Setup cannot copy the file
`WdfCoInstaller01007.dll`". The `tools\` package had been pruned to
`Ax88772.inf` / `ax88772.sys` / `ax88772.cat`, and there is no diagnostic
until the wizard fails mid-install: the INF parses, the models section
matches, the device enumerates, and the copy stage is the first thing that
knows.

Two steps made this cheap rather than a rabbit hole:

1. Check whether the dependency is real before hunting the file. A vendor
   INF's WDF sections can be copy-paste from another package, in which case
   the fix is to strip them, not to find a DLL. `link -dump -imports` on
   `ax88772.sys` shows `WDFLDR.SYS` with `WdfVersionBind`/`WdfVersionUnbind`
   alongside `NDIS.SYS` and `USBD.SYS`, so KMDF is genuine, and Win2000 ships
   no KMDF runtime, which is what the co-installer installs.
2. The package's own `history.txt` answered the Win2000 question. v3.4.3.37
   reads "Use WDF instead of WDM", "Let the driver only can be installed on
   Windows XP & 2000", and a Win2000-specific RX-buffer fix, all in one
   release. A vendor does not ship a WDF driver scoped to 2000 with a
   co-installer that cannot install there. That was enough to spend a boot
   on, without needing to settle KMDF 1.7's supported-OS matrix from memory.

A previous session's scratchpad does survive. The handoff had recorded that
Zadig's staging path "is a scratchpad path and will not survive". The
complete ASIX extraction, including the Microsoft-signed, Authenticode-valid
`WdfCoInstaller01007.dll` `1.7.6001.0`, was still in an earlier session's
scratchpad and is where the file came from. `tools\` is repaired. Search the
prior scratchpads before re-downloading anything a previous session fetched.

VVFAT is synthesized at QEMU start, already recorded, and it is what makes
this cost a whole guest restart: detach the USB device, shut down from the
GUI (so the lifecycle callbacks run), stage, relaunch, re-attach. Do not
`system_reset` with the device attached.

### Flush ARP before crediting a host-side ping

The 2b guest was handed `192.168.1.235`, the same address the 2a guest had
taken that morning, because it is the same adapter and the same DHCP server.
So `arp -a` resolving the hardware MAC proved nothing about this boot.
`arp -d` first, then ping: 150/150 with a fresh resolution. A check that sits
outside the guest is only outside it if its cache was cleared.

### The positive control for "Verifier was watching" is not always its counters

Driver Status listing `xhci98.sys` as `Loaded` with Force IRQL checking
Enabled is the reading that holds. Pool-allocation counters are not a usable
control for this driver: it is barred from `ExAllocatePoolWithTag` by policy,
so zero pool activity is the expected value and says nothing either way. Pick
a positive control the code under test can actually move.

## Batch 7b-V - the counter reader printed three device records, because no earlier run had ever enumerated a fourth

`scripts\local\offsets.c` printed device records 0-2 only, and
`readcounters.ps1` hard-coded `$i -lt 3` in each of its two decoded blocks,
so the fourth record, the second hub, which is what the two-tier measurement
is taken through, was simply absent, and the fifth would have been too. Three
was enough for every earlier run because no earlier run had got a fourth
device enumerated. An instrumentation limit sized against a broken driver
stops being a limit you can see the moment the driver starts working. The
count is now derived from `offsets.txt`.

## Batch 8-V.2: the receive stalls on the xHCI path, the EHCI control does not prove it is ours, and two exonerations in a row were taken on the wrong vehicle

Environment: host `fw-w11p-ykm`, QEMU 11.0.0, 2a Win98 SE, binary `built Aug
8 2026 20:53:00` (`MiniPortExtensionSize=0000C270`). A physical ASIX AX88772A
(`0b95:7720`) passed through with `-device usb-host` after a Zadig rebind to
WinUSB. Launcher `scripts\local\qemu-win98-run-8v2-ehci.cmd`; trace set
`xhci-trace-events-8v2-req.txt`. Evidence in `vm\8v2-ehci-run1\` and
`vm\8v2-ehci-run2-refreshloop\`.

Before reading the rest of this entry: its first drafts each claimed more
than they had shown, and the corrections are the substance. "The defect is
ours" is withdrawn; see "What the control cannot see" below. The measurements
are sound; two successive conclusions drawn from them were not.

### The control answers: RX comes alive on EHCI, so the fault is on the xHCI path

The open question left by the earlier run was that the adapter enumerated
through this driver, bound its vendor driver, read its MAC off the hardware
and moved 5,215 transfers with zero errors, and never passed IP traffic, with
bulk IN posted exactly once, ever. Our own counters could not settle it
because everything they report is healthy, so the path's other component,
QEMU's `usb-host`, was equally suspect.

Putting the same passed-through adapter on `bus=ehci.0` in the same guest,
with the xHCI still bound, separates them. Endpoint attribution is by
correlating `usb_host_req_complete` back to its request through the shared
`packet` pointer:

| endpoint | posted | completions | bytes | errs |
|---|---|---|---|---|
| control ep0 | 36 | 34 | 177 | 0 |
| `ep 3` OUT (bulk TX) | 35 | 35 | 4,870 | 0 |
| `ep 1` IN (interrupt) | 954 | 954 | 7,632 | 0 |
| `ep 2` IN (bulk RX) | 29 | 28 | 3,728 | 0 |

Against the xHCI leg's `epid 5` = 1 posted, 0 completed. Normalised against
interrupt polling, which is the only fair way to compare runs of different
lengths, the vendor driver reposts its receive buffer 1 per ~33 interrupt
polls on EHCI against 1 per 1,896 on ours, and the xHCI leg ran longer and
had more opportunity. Run length cannot explain the asymmetry.

Non-involvement was proven the way the batch 8-V EHCI control proved it:
`usb_xhci_xfer_start`, `usb_xhci_slot_enable`, `usb_xhci_port_reset` and
`usb_xhci_slot_address` all 0 across the whole run.

The traffic clause passes outright on EHCI, which is what makes this a
control rather than a suggestive difference: the guest took a real DHCP lease
(`192.168.1.235`, mask `255.255.255.0`, gateway `192.168.1.100`) and the host
pinged it 4/4 over the LAN with `arp -a` resolving `00-0e-c6-05-3a-a3`. That
last check is outside the guest and outside QEMU's trace, so it depends on
none of the instruments under suspicion.

- Result: task 8-V.2 has found a real defect on the xHCI path. Whose it is
  the control does not say; see "What the control cannot see". An earlier
  draft of this entry said "in this driver's bulk IN path" and that was more
  than the evidence supports.
- Reusable rule: an exoneration is only as good as the vehicle it was taken
  on. The earlier run cleared the adapter by binding it to Windows' own
  driver, where it "got a DHCP address and browsed the web", but that was a
  DHCP network with internet, while the failing guest sat on a direct cable
  to a host port with no DHCP server. Two different vehicles. On that link a
  failed DHCP and a never-completing bulk IN are the expected outcome of the
  topology, and prove nothing about the driver. Re-run the control and the
  failure on the same network before attributing the difference to code.
- Reusable rule: a NIC's bulk IN staying outstanding is not evidence of a
  fault. A receive URB is supposed to sit pending until a frame arrives. The
  reading that carries information is how often the buffer is reposted, not
  how many are outstanding, and it only means anything against a link that
  is demonstrably delivering frames.

### The same measurement on our own path: the receive completes, and the repost is what dies

Taken later the same day with the adapter moved to `xhci.0`, same guest
image, adapter, network, trace set and analyser. Not literally the same boot,
since QEMU had to be relaunched, but far tighter than the previous
cross-session comparison. Our driver's own view by endpoint:

| epid | endpoint | `usb_xhci_xfer_start` |
|---|---|---|
| 1 | EP0 control | 39 |
| 3 | EP1 IN interrupt | 579 |
| 5 | EP2 IN bulk RX | 1 |
| 6 | EP3 OUT bulk TX | 17 |

and the passthrough view of the same device now carried by us:

| endpoint | posted | completions | bytes | errs |
|---|---|---|---|---|
| control ep0 | 40 | 38 | 179 | 0 |
| `ep 3` OUT (bulk TX) | 17 | 17 | 2,358 | 0 |
| `ep 1` IN (interrupt) | 580 | 579 | 4,632 | 0 |
| `ep 2` IN (bulk RX) | 1 | 1 | 362 | 0 |

This corrects what earlier sessions recorded. The earlier run wrote the
symptom down as bulk IN "posted once and never completed". That is wrong. It
is posted once, completes successfully with 362 bytes (a real Ethernet frame,
matching the 362-byte maximum the EHCI leg saw), and is then never posted
again. `xfer_error` = 0 and `ep_stop` = 0 throughout.

A retracted lead, recorded because it was committed before it was checked.
The first reading of this was "a second doorbell on `epid 5` started no
transfer", inferred to be a cycle-bit or dequeue-pointer fault. That was
wrong. `usb_xhci_ep_kick` is not a doorbell indicator (QEMU emits it
internally as well, notably to re-examine a ring after a completion), and its
ratio to `xfer_start` is ~2 on every endpoint including the healthy ones
(epid 1: 77/39, epid 3: 2366/796, epid 5: 2/1, epid 6: 34/17). epid 5's "two
kicks, one start" is the normal ratio, not an anomaly, and a host vector
built on that inference would have chased nothing. Rule: before treating an
event count as anomalous, take the same count on an endpoint that works.

The actual finding: the receive completes as a short packet on a multi-TRB
TD, and the tail event never arrives. The one Transfer Event `epid 5` ever
produces in the whole run is

```
usb_xhci_queue_event idx 73, ER_TRANSFER, CC_SHORT_PACKET,
                     s 0x0d000476, c 0x01058001
```

against 852 `CC_SUCCESS` and 6 `CC_SHORT_PACKET` events in the trace as a
whole. The residual decodes consistently: `0x476` = 1142, and 1504 - 362 =
1142, so the 16,384-byte receive was built as a multi-TRB TD, the short
packet landed on the first TRB (1504 bytes), and no second event followed.

Batch 8-A established the two-event rule for this shape: the first event
names the TRB the short packet landed on and completes nothing, and "the tail
event still arrives" because a TRB with IOC set generates a Transfer Event
while the xHC advances to the end of the TD. This driver's terminating rule
depends on that tail event, and here it does not come. So the TD is never
retired, the transfer is never completed up to usbport, the vendor driver
never gets its receive back, and it never posts another. One stalled receive
is the whole failure.

Why nothing else shows it:

| endpoint | TD shape | goes short? | works |
|---|---|---|---|
| interrupt, 8 bytes | single-TRB, exact length | never | yes |
| bulk OUT | exact length | never | yes |
| storage bulk IN (13-byte CSW on a 512 pipe) | single-TRB short | constantly | yes |
| NIC bulk IN (16 KB posted, 362 arrive) | multi-TRB short | always | no |

Mass storage drove 2,042 short packets on 2b without trouble because every
one of them was a single-TRB TD, where the first event is the tail event. A
short packet on a multi-TRB TD is the case no device before this NIC could
produce, and it is the one that hangs.

Here the obvious fix is the wrong one, and it was checked before it was
written. The tempting change is "make the short-packet event terminal".
`docs/usb-xhci-info/xhci-data-structures.md` (transcribed and verified
against the spec PDF) forbids it outright, p.175:

> "software shall not interpret a Short Packet Event as indicating that the
> TD that it is associated with is `complete`, unless the TRB Pointer field of
> the Transfer Event references the last TRB of the TD"

Wait for the tail event. And the tail event does arrive, p.202: "while
advancing to the end [of] the current TD after generating this event, each
Transfer TRB encountered with its IOC flag set to `1` shall generate a
Transfer Event".

So batch 8-A's waiting rule is spec-correct, this driver sets IOC on the last
TRB of every Normal TD, and a host vector asserting the short packet
completes the transfer would pin behaviour the spec prohibits. One was
written and reverted before commit; the only reason it did not land is that
the normative document was read first. Rule: when a field failure suggests
reversing a documented design decision, re-read the source that decision was
made from before writing the test, never after.

Settled from the spec PDF itself (trust order #1), p.175, 4.10.1.1.2:

> "If the Short Packet occurred while processing a Transfer TRB with only an
> ISP flag set, then two events shall be generated for the transfer; one for
> the Transfer TRB that the Short Packet occurred on, and a second for the
> last TRB with the IOC flag set."

`docs/usb-xhci-info/xhci-data-structures.md` transcribed it correctly, this
driver's waiting rule is spec-correct, and QEMU's xHC is non-conforming: it
emits one event where two are mandated. No instrumented run was needed after
all; the PDF answered it. Rule: exhaust trust order #1 before designing an
experiment to recover what it already states.

The hang is still ours to fix, and Linux shows the shape.
`external/linux/xhci-ring.c`, `process_bulk_intr_td`: `case
COMP_SHORT_PACKET` sets `td->status = 0` and falls through to `finish_td`
whether or not the event named `td->end_trb`, computing the mid-TD length as
`sum_trb_lengths(td, ep_trb) + ep_trb_len - remaining` (the exact case) and
guarding the duplicate with `xhci_spurious_success_tx_event()`. Trust
order #2 (Linux for xHCI hardware behaviour) outranks #3 (this
repository's reading).

Completing early is safe rather than merely pragmatic because the
second event carries no new information (p.175: same length; ours is already
latched) and the xHC has provably finished the TD before the event is visible
(p.210). A conforming controller's second event then names a TRB below the
dequeue pointer, which the ring layer already rejects as a trailing event.
That is where no-double-completion comes from.

The change was written, run and reverted: it works, and its shape was wrong.
Making the short packet terminal for a Normal TD does end the receive, and
twelve existing vectors then fail (`test_ring.c`
954/958/959/960/969/1040/1041/1043, `test_xfer.c` 1305/1312, `test_init.c`
12135/12136), all of them asserting the old behaviour, so the direction is
confirmed by the vectors that should object. The flaw was in the ring half:
`CanRetire` was widened so a Short Packet retires mid-TD, but the ring layer
cannot make that call. It cannot tell a Normal TD from a control transfer's
Data Stage, and a short Data Stage must still retire only at its Status
Stage TRB. `test_ring.c` caught that.

- Reusable rule: put a policy decision in the layer that holds the knowledge
  it depends on. The retire is the ring's mechanism, but "may this TD end
  here" needs the transfer shape, which only the transfer layer knows.
  Widening the lower layer's rule to serve one caller is how an invariant
  quietly stops meaning anything.
- The design to finish it: leave `CanRetire` alone, make `terminal` depend on
  `wholeTdIsData && CC_SHORT` in the transfer layer, and give the ring an
  explicit caller-asserted retire. "Complete but do not retire" is not
  available; it trips `OrphanedGroups` and forces an endpoint stop per
  received packet.

### The fix, written to that design the same day - host-green, not confirmed on a target

`XhciRingClassifyEvent` is untouched. The departure is a second ring entry
point, `XhciRingRetireAdvancedTd`, which accepts Short Packet on an endpoint
ring and refuses every other code, every other ring kind, and a stale
classification; `XhciXferEvent` decides when to call it from the TD shape
(`DataFirstIndex == FirstIndex && DataTrbCount == TrbCount`), and refuses
even then unless the ring's own chain walk agrees with the transfer record
about where the TD ends. Suite 10,072 -> 10,163 green; extension 49,776 ->
50,420 bytes (the queue counter is per endpoint queue, and the extension
holds 160 of them, so one `ULONG` costs 644).
`docs/usb-xhci-info/xhci-data-structures.md` and
`docs/contributing/implementation-invariants.md` carry the departure so an
intended "shall not" does not read as an oversight.

Three things worth keeping from doing it:

- The re-run vector count was four, not twelve, and that is the design being
  right. `test_ring.c`'s eight objected to the first attempt because it moved
  a decision into their layer; a fix that leaves that layer alone never
  disturbs them. A vector set that stops objecting when you correct the shape
  is evidence about the shape, not a coincidence to note in passing.
- Removing the shape test makes `test_xfer` fault outright rather than fail a
  check: a control transfer completed at its Data Stage strands the Status
  TRB and the vector then dereferences a completion list that is not there.
  The strongest available statement that the restriction matters, and it
  arrived from the mutation sweep rather than from review.
- Two mutations failed zero checks and both are the same one: each half of
  `wholeTdIsData` alone decides every TD shape this driver builds today,
  because a control transfer differs in both. Kept as a conjunction and
  documented as consistency rather than claimed, which is the standing rule
  for a term the suite cannot justify.

Confirmed on 2a the same day; see the next section. This is also the first
code change since batch 8 began, so every "same binary on both targets" claim
in the roadmap's boxes needs re-establishing; `scripts\local\offsets.txt` has
been regenerated (`SIZEOF 50420`) and must be cross-checked against the
trace's own `MiniPortExtensionSize` before a single counter is read.

### The 2a confirmation run - the fix holds and the traffic clause passes

`epid 5` went from 1 posted, 0 completed, ever, to 294 transfers under
sustained load with zero errors. The counter is what makes it the fix rather
than a coincidence: `MidTdShortRetiresTotal` = 168, i.e. 168 of the 293 short
packets on that endpoint did not name their TD's last TRB and were retired by
the departure. The other 125 landed on their tail and retired by the ordinary
positional rule. That is the expected split, and worth stating because "all
of them" would have meant the shape test was firing where it should not.

The traffic clause passed on the xHCI path for the first time: a real DHCP
lease (`192.168.1.235`), `arp -a` resolving the adapter's hardware MAC, and a
host ping of 6/6, TTL=128, a check outside the guest and outside QEMU's
trace.

The backpressure latch is measured unreachable, which is the finding 8-A was
owed. `TransfersRefusedRingFull` and `EndpointRetriesAsked` are both 0 across
a working receive path under sustained load. Mass storage cannot fill a bulk
ring for BOT protocol reasons; a NIC does not because it keeps only a handful
of receives outstanding. No device class available to this project can
saturate a 62-TRB ring, a statement about the reachable devices, not a defect
in the latch.

Three vehicle facts, all paid for:

- A `system_reset` with a USB device attached wedges the guest at the Win98
  splash. It makes the device present at POST, which is the same condition
  as the recorded SeaBIOS command-line-device trap. Worse, the enumeration
  that appears in the trace at that moment is SeaBIOS's, not this driver's:
  `port_reset`, `slot_enable`, `slot_address` and EP0 transfers all appear
  with the guest's driver not yet loaded, and reading them as progress cost
  a boot. The tell is the debug console: `DriverEntry` had not been printed
  a second time. Detach before any reset, and check the console before
  crediting the trace.
- A plain relaunch clears that wedge, as this file already said of the
  command-line form.
- Device Manager Refresh did not trigger the restart loop here.
  `slot_disable` stayed frozen at 448 and the console went quiet after the
  enumeration burst, against the loop's continuous ~8 KB/s trace and ~4 KB/s
  console. So the loop is not a property of Refresh as such, which narrows
  the open question recorded earlier rather than answering it.

### QEMU's emulated devices do not fail, and its storage error knobs are the wrong layer

Task 8-A.2's injection clause was carried as "decide what a target can add
before spending a boot". Answered without one.

`usb-storage`'s `rerror`/`werror` are typed `BlockdevOnError`, a block-layer
policy, not USB fault injection, and `hw/usb/dev-storage.c` reports a failed
SCSI or block command as a normal CSW with `bCSWStatus = 1` (`s->csw.status =
req->status != 0;`, then `usb_msd_send_status()`) over bulk transfers that
complete perfectly. At the xHCI level that is `CC_SUCCESS`. The model's four
`USB_RET_STALL` paths all require a malformed host (bad CBW
signature/size/LUN, a USB/SCSI direction mismatch, `needs_reset`, an
unhandled control request), which a correct `usbstor.sys` never emits;
`USB_RET_NAK` and `USB_RET_BABBLE` are absent from the file entirely.

The measurement agrees. Completion codes across four substantial target runs
(8-V storage on 2a and 2b, 8-V.2 Ethernet session 1, 7b-V hub churn) are
`CC_SUCCESS`, `CC_SHORT_PACKET`, and 13 `CC_STOPPED` this driver issued
itself. Roughly 19,000 transfer events, zero error codes.

The one target that does produce errors is `usb-host` passthrough, and it
does so unprompted. The ASIX session logged 12 `CC_USB_TRANSACTION_ERROR` (11
on EP0, 1 on the interrupt endpoint) and the driver's own counters match the
controller's event count exactly: `endpoint halts = 0x0C` = 12, halted DCIs 1
and 3 carrying code 4, and the two counters that would report a botched
recovery (`endpoint resets on a ring not halted`, `transfers on a halted
endpoint`) both 0, with 5,215 faultless transfers following in session 1.

- Reusable rule: read what the model does with a knob before designing an
  injection around it. The property's type settled this (`BlockdevOnError` is
  a block-layer word) and no experiment had to be designed, let alone booted.
- Corollary for this project's vehicles: emulated QEMU devices exercise the
  success and short-packet paths only. Every error path that has ever run on
  a target got there through passthrough of real hardware, which is a reason
  to keep the passthrough rung open beyond its own clause.

### What the control cannot see - the gap that makes "the defect is ours" unproven

The EHCI control was built to separate QEMU's `usb-host` passthrough from
this miniport, and it does. But there is a third component on the failing
path that it never touches: QEMU's xHCI device model. The miniport and the
xHC model are both exercised only on the xHCI leg and neither is exercised on
the EHCI leg, so no comparison between the two legs can tell them apart.

What the control establishes is therefore narrower than this repository
claimed before this paragraph was written: the adapter, the cable, the
network and the passthrough layer are all good, and something on the xHCI
path is broken. Which of the two things on that path, it does not say.

- Reusable rule: a differential control separates the components it varies,
  and only those. Name every component on the failing path before declaring
  what the control proved, and check that each one is absent from the
  control leg. Here "our miniport" and "the emulated host controller" were
  silently treated as one component because they always appear together.
- This is the third vehicle error in the same task in two days: the adapter
  exonerated on a DHCP network the guest never used, the `ep_kick` count read
  as anomalous without taking it on a working endpoint, and now a control
  credited with separating something it never varied. The common shape is
  concluding about a component that the experiment did not independently
  move.

### An unplanned second finding: Device Manager Refresh puts the controller into an endless restart loop

Pressing Refresh in Device Manager on 2a wedged the guest: screen
byte-identical over 14 s (SHA-256 of two screendumps), `info status` still
`running`, while the debug console grew at ~4 KB/s and the QEMU trace at
~8 KB/s. That is the batch 8-V replug wedge's shape, a PASSIVE-level GUI
frozen while ring-0 runs, but with this driver storming rather than idling,
which is a different signature and probably a different bug.

What it is doing is a start -> stop -> start loop: the whole init and
port-map block repeats 28 times per 40 KB of debug console, each cycle
running `init complete` -> `announcing a port change to usbport` -> `quiesce:
halted` -> `SuspendController: halted` -> `save: state saved`, with
`usb_xhci_slot_disable` appearing 715 times per 60 KB of trace (32 slots per
teardown). The driver services every cycle correctly; it is being asked to
restart forever. `system_reset` clears it and the guest boots normally.

- Not established: whether the loop originates in usbport/PnP or in something
  this driver reports back to it. Both wedges on this target so far have had
  a Microsoft-miniport control behind them; this one does not yet, and the
  same standard applies before calling it ours or theirs. The discriminating
  test is a Refresh on the same guest with only NUSB's `usbehci.sys` present.
- Operational rule until then: do not press Refresh in Device Manager during
  an 8-V.2 run. Win98's idle-suspend means a Refresh is what an
  operator reaches for to make an attach visible.
- Reusable rule: distinguish a wedge that spins from a wedge that stops. Log
  growth rate separates them in fifteen seconds and points at completely
  different causes; "the guest is hung" does not.

### Two host-side traps this session paid for

- Microsoft Defender blocks socket one-liners as `Trojan:Win32/ClickFix`. An
  inline `powershell -Command` defining a `TcpClient` connect/read/write
  helper to reach QEMU's monitor is flagged and the process never spawns; the
  harness reports `EPERM: uv_spawn powershell.exe`, which reads as a
  transient glitch. The detection resource is a `CmdLine:`, not a file, so
  nothing is quarantined and nothing on disk is touched. The same code in a
  `.ps1` file runs fine; that is why `netattach-8v2.ps1`, `unplug-8v.ps1`
  and `readcounters.ps1` have never tripped it. There is a prior identical
  detection against an inline heredoc-written `qmon.ps1`. Rule: drive the
  QEMU monitor from a script file, never from an inline command line, and
  read an `EPERM` spawn failure as a possible AV block rather than noise.
- A host-side trace reader must open with `FileShare::ReadWrite`. Already
  documented for the disk image; `[IO.File]::ReadLines` does not do it and
  fails with "being used by another process" against a live QEMU trace.

## Batch 7a-V's checkpoint run: a test that under-ran silently, and two "expectations" that were never derived

Environment: this development host, QEMU under `C:\Program Files\qemu`. One
binary (90,011 B debug, `built Aug  5 2026 10:00:32`, extension `0xB160`)
across 2a Win98 SE, 2b Win2000 SP4 under Driver Verifier, and 2d Win2000 SMP
on WHPX. All three passed. Evidence in `vm\*batch7av-run*-fix.*`.

### A test can under-run silently, and the failure looks exactly like a pass

The ten unplug/replug cycles were driven through the monitor and reported ten
cycles. Eight of them never reached the guest. Win98 idle-suspends the
controller about half a second after the last transfer, and a `device_add`
onto a halted controller is seen by nothing: `PORTSC` sits at CCS=1 / PED=0 /
Polling with the connect change already acknowledged, and nothing above
issues the port reset. The driver was behaving correctly throughout.

What made it visible was not the counters the run was about (those simply
did not move, which reads identically to "nothing went wrong") but an
unrelated one: `restore-state failures` had climbed 0 -> 8, one per
suspend/resume pair.

- Reusable rule: a stimulus you sent is not a stimulus that arrived. When a
  run is driven from outside the guest, assert that each iteration reached
  the device under test, from a counter that only the arrival can move. Ten
  log lines saying "cycle N: replugged" are a statement about the harness.
- Reusable rule: check the counters a run is not about. The frozen ones named
  the defect the moving ones could not.

The fix generalises: QEMU's `sendkey` and `mouse_move` drive the USB HID
devices, not only the PS/2 pair (one keystroke = exactly two interrupt
completions, four seconds of idle = none). So bus traffic can be generated
from the monitor, which both proves the keyboard end to end without typing
into the guest and provides the keep-alive that makes the cycle test valid.
Recipe and traps in `docs/contributing/build-and-test.md`.

### Two "expected zero" readings, neither of which had been derived

Both were written from a counter's name and both were wrong in an informative
way.

- `EndpointStoppedEvents`. Zero across twelve unplug-driven stops, which the
  roadmap said would mean the controller never forces the Stopped Transfer
  Event (4.6.9 p.122), i.e. the placement running against an unannounced
  stop. Sixteen abort-driven stops then produced eight. QEMU does implement
  it; the zeros were stops with no TD in progress. A zero counter is a claim
  about the paths that ran, not about the controller.
- `EndpointRestartsByPoll`, documented "expected zero: usbport's pause is
  ending somewhere this driver is not watching", measured 1 / 3 / 4 on 2a /
  2b / 2d, scaling with abort count. The premise is refuted from both
  binaries: `USBPORT_SetEndpointState` is edge-triggered on usbport's own
  recorded state (SP4 `00016D8A`, NUSB `000169D2`, the identical `cmp
  eax,[ebp-10h] / je <skip the call>`), so a state that does not change
  produces no callback at all. A stop this driver takes on its own initiative
  never moves usbport's state, so usbport can sit ACTIVE -> ACTIVE across the
  whole stop and say nothing; the only other thing that restarts the endpoint
  is a new submission, which a HID device's already-queued read is not going
  to bring. The poll is essential, not a backstop, and the alarm is a
  climbing value against idle traffic rather than a nonzero one.

The same read settled a question asked before the run: whether
`OpenEndpoint` needed batch 7a-V's `SubmitDepth` bracket. It does not.
`UsbPortCompleteTransfer` re-enters no miniport slot in either build (SP4
`0001A4EA` -> `000183C0`/`000154B0`, NUSB `0001A0D4` -> `00017FA4`/`000152EE`:
unlink, `ExfInterlockedInsertTailList` onto the FDO done list,
`KeInsertQueueDpc`, `KeSetEvent`, return). So the hazard is only ever what
the caller touches after the callback returns, which only `SubmitTransfer`'s
caller does. Two in-code comments claiming the opposite were corrected.

- Reusable rule: an expectation about the other side of an ABI is a
  derivation, not a name. Every one of these was answerable from binaries
  already in the tree, in minutes, at any point before the run.

### Deploying to three guests: confirm from the binary's own output

All three VMs booted a stale driver first: 2a the pre-fix build, 2b a Phase 6
build, 2d a Phase 4/5 one. Staging a package into `vm\xfer*\` does not put it
in a guest; the copy is manual and per-guest. The `DriverEntry (built ...)`
and `MiniPortExtensionSize` lines cost one grep and are the difference
between testing the fix and re-testing the defect. The same two lines are
also the layout check for reading counters out of guest memory, and on this
run `scripts\local\offsets.c` did not compile at all (it named
`EventsConsumed`, which is `EventsTotal`), so the committed offset table was
a stale layout that would have shifted every reading silently.

## Two host-side traps that cost time in batch 7a-B, neither of them about hardware

### `Set-Content -Encoding utf8` writes a BOM, and MSVC 6 refuses it

Environment and operation: Windows host, editing `src/*.c` from PowerShell
while working batch 7a-B.

Symptom: the build fails with `unknown character '0xef'` on line 1 of a file
that looks correct in every editor.

Cause, proven: Windows PowerShell 5.1's `Set-Content -Encoding utf8` emits
UTF-8 with a byte-order mark (`EF BB BF`). MSVC 6.0 predates the convention
and reads those three bytes as source text.

Rule: do not write a source file with `Set-Content -Encoding utf8`. Use an
editor, or `[System.IO.File]::WriteAllText`, which takes a `UTF8Encoding` you
can construct with `$false` for the BOM. The same applies to `Out-File`,
which also defaults to a BOM in this environment. This is a toolchain trap,
not a Win98 one: it fails at compile time on the host, so it costs a build
cycle rather than a boot.

### A vector can be vacuous because of when the host model stamps a completion code

Symptom: a test that sets `hwCmdCompletionCode` and then asserts the driver's
reaction passes for the wrong reason, or fails while the driver is right.

Cause, proven: the model reads that global when the command is submitted, not
when its event is processed. Setting it after the call that issues the
command stamps the next command, not the one under test.

Rule: set it before the issuing call; restore it before the completion is
processed when only that command should see it. Recorded normatively in
`docs/contributing/design/03-host-unit-tests.md`, "Two properties of the
model itself that a vector must be written against", together with the
sharper version of the same problem: the model does not enforce command
preconditions the hardware does, so a green vector is evidence about this
driver and not about whether the sequence it drives is legal.

## Batch 6-V - a silent trace is not a dead guest, and a guard written for the first pass through a path is not a guard for the second

Two method notes from Phase 6, keyed by batch because the run they came from
(task 6-V.1 on both target VMs) has no entry of its own.

A silent trace is not a dead guest. This is the inverse of the Phase 5 rule
below (the Full-Speed bugcheck entry) that a healthy trace is not a living
guest, and it is now measured too. On 2b a transfer-refusal loop (batch 6-V's
blocker 2: a refusal "for retry" that could never stop being true) stopped
appearing in the log because `XHCI_DBG_VALUE_LIMITED` caps at 32 prints per
site and the loop spent that budget in seconds; the machine was still
spinning. The discriminator is a screendump either way. The livelock's own
signature, `transfers refused for retry` climbing against a frozen `transfers
submitted`, is in `docs/contributing/failure-diagnosis.md`, "Reading a trace
without fooling yourself".

A guard written for the first pass through a path is not a guard for the
second. Batch 6-B's stop-time review found re-enumeration without a
disconnect: usbhub gives up on a device by resetting its port and creating it
again at address 0, and the first draft re-entered the address chain only for
a FAILED record, leaving an ADDRESSED one bound to a bus address the device no
longer had, with nothing anywhere reporting it. Fixing that exposed the second
half in the port association, where the reset-derived hint required its port
to have no record bound, a condition that belongs to the fallback scan only,
where the work is disambiguation. The mechanism was later superseded by the
fifth repository audit's finding A2; the shape is the thing to look for next
time.

## On host `fw-w11p-ykm` - the Win2000 `ResumeController` gap is closed as a question: the blocker is the display adapter, not the HAL, and every QEMU route is measured shut

### Environment and operation

Host `fw-w11p-ykm`, scoop QEMU 11.0.0, `qemu-system-x86_64`, WHPX. Picking up
`vm\win2k-acpi.img` mid-install from the entry below (text-mode Setup
complete, first graphical boot hung at `EIP=0xf401118f`). Operation: finish
the ACPI Win2000 install and make `ResumeController` execute, the one
miniport lifecycle callback that has never run on Windows 2000. Driver:
`out\pkg-debug\` `xhci98.sys` 49,259 B, `built Aug  2 2026 14:38:28`, the
byte-identical binary that passed Phase 5 task 7 on both targets.

This entry supersedes the "what to try next" list of a since-deleted handoff
note, whose content is now distributed here, into `build-and-test.md` (the VM
recipe and traps) and `failure-diagnosis.md` (the trace-reading rules).

### What was measured, in the order it was measured

1. The hang was not real. A plain relaunch (`-boot c`, CD still attached)
   cleared `0xf401118f` outright: the guest went past "Starting up..." into
   GUI-mode Setup and completed it, disk 450 MB -> 955 MB. The one-minute
   test that had never been run was the entire blocker, and four hours of
   prior configuration work had been spent around it.
2. Device Manager -> Computer reads `ACPI Uniprocessor PC`. The
   HAL-chosen-by-Setup requirement is satisfied; configuration #5 (`-machine
   pc -accel whpx,kernel-irqchip=off -cpu pentium3 -vga std`) is the working
   recipe.
3. No Stand by, and no Hibernate. Power Options has exactly three tabs (Power
   Schemes, Advanced, UPS) with no "System standby:" row.
4. The machine does offer sleep. `info qtree` -> `PIIX4_PM` has `disable_s3 =
   0`, `disable_s4 = 0`. A `pmemsave 0 0x10000000` dump plus a
   checksum-validated table scan finds the DSDT at `0x0ffe0040` (8598 bytes)
   defining `_S3_`, `_S4_`, `_S5_` and no `_S1_`/`_S2_`.
5. The driver starts cleanly on the ACPI guest, a configuration it had never
   met: 4 managed ports, both port-map passes agreeing field-for-field, No Op
   self-test completion matched, the task 6 EHB hazard firing as predicted
   ("already holding events, pending=1"), every failure counter zero.
6. Idle produced no suspend. Left idle with no devices attached, usbport ran
   `cb PollController` four times and stopped; the log froze at exactly 7064
   bytes. No `cb SuspendController`, no `cb ResumeController`.
7. Shutdown produced a suspend; see the next section.
8. Neither controller has a Power Management tab. Ours shows General /
   Driver / Resources; the Intel 82801DB EHCI (Microsoft's driver, same
   `usbport.sys`, same emulated bus) shows General / Advanced / Driver /
   Resources.
9. With `-vga cirrus`, Stand by reappears and selecting it produces "System
   Standby Failed ... the device driver for the 'Cirrus Logic 5446 Compatible
   Graphics Adapter' device is preventing the machine from entering standby",
   verbatim the message `roadmap.md` recorded for the SMP 2d VM, now
   reproduced on a uniprocessor guest.
10. During that vetoed attempt the miniport saw no power transition at all.
    Census for the whole cirrus run: `cb SuspendController` 2 (the
    driver-install reboot and the closing shutdown; neither is the standby),
    `cb ResumeController` 0. Around the attempt itself the log carries a
    single lone `cb EnableInterrupts`.

### The one thing that did run for the first time: `SuspendController` on Win2000

Trace `vm\win2kacpi-debugcon.resume-dstate-closed.log`, an ordinary shutdown:

```
cb SuspendController irql=00
quiesce: halted, USBSTS=00000001
SuspendController: halted, USBCMD=00000000
cb DisableInterrupts irql=02
cb StopController irql=00 b=00000001
teardown: the stop arrived on a suspended controller - PORTSC is
          unwritable, so port power stays up
cb DisableInterrupts irql=02
```

- `SuspendController` has now executed on Windows 2000, and the body did its
  job: the quiesce halted the controller and `USBCMD` read 0.
- The teardown shape matches Win98 exactly (`Suspend -> DisableInterrupts ->
  Stop`), so the two primary targets agree here.
- Phase 4 task 8's "stop arrived on a suspended controller" branch is
  witnessed for the first time anywhere. That branch was added after a
  review found that the lifecycle entry point restated the quiesce's
  admission gate and so skipped the port pass on every ordinary unload. The
  log line proves the fixed path is the one that runs.

### What this proves, and what it does not

Proven: `ResumeController` cannot be reached under QEMU on Windows 2000, and
the HAL was never the obstacle; four earlier configurations chased it. The
blocker is the display adapter, and the two available choices fail in
opposite ways:

| `-vga` | Stand by offered? | Outcome |
|---|---|---|
| `std` | no | yellow-banged `Video Controller (VGA Compatible)`, no in-box driver, so Windows never offers it |
| `cirrus` | yes | offered, then vetoed at `QUERY_POWER`: no `SET_POWER`, so no D3, so no pair |

Device selective suspend is closed too, and on a control: Microsoft's own
EHCI lacks the Power Management tab as well, which makes the absence a
property of the machine and exonerates our INF and capability declarations.

Inferred, not observed: that the veto is what stops `SET_POWER` (rather than
some other abort) is inferred from the lone `EnableInterrupts` and the
absence of any suspend; the power IRP sequence itself was not traced.

Unknown: whether a third display driver exists that would both drive the
emulated adapter and survive S3. `roadmap.md` records that Win2000 SP4's
display-class list offers no `(Standard display types)` manufacturer, so
this looks closed, but it was not exhaustively searched.

Consequence: `ResumeController` on Win2000 was owed to bare metal, alongside
the Low-Speed leg, and no bare-metal Windows 2000 vehicle ever existed, so it
is published as a limitation (roadmap Phase 13). Do not spend another session
on QEMU configurations for it.

### Reusable rules

- Relaunch before diagnosing. A hang that has not survived one restart is
  not yet a symptom. This one had a four-sample EIP measurement attached to
  it and still evaporated on a plain relaunch.
- The repo's own records outrank a plausible inference from the guest in
  front of you. The display-adapter hypothesis was raised, then dropped on
  the reasoning that a missing Hibernate tab could not be video-related,
  while `roadmap.md` already held a direct observation settling it. Grep the
  repo for the symptom before reasoning about it.
- Give an anomaly a control before blaming your own code. One extra
  Properties dialog on Microsoft's EHCI converted "our driver may be missing
  a capability" into a machine fact.
- Scan inside a checksum-valid table, not across RAM. Grepping all 256 MB for
  `_S1_` returns a hit, because `acpi.sys` carries those names as lookup
  constants. Only the DSDT body answers the question; the narrower scan was
  right and the "more thorough" one was wrong.
- Let a chardev log settle before concluding a callback did not fire. The
  log read 7064 bytes with no suspend moments after the VM went away, then
  settled at 7541 with it. "Absent from the log" and "absent from the run"
  are different claims separated by a couple of seconds.
- A failed system transition is not necessarily a no-op; check. The
  hypothesis that a vetoed standby might still take devices to D3 and back
  was worth one experiment; it was cheaper to observe than to argue, and it
  turned out false.

### Affected

`docs/contributing/roadmap.md` (the Phase 4 checkpoint clause at task 10's
note; the `-vga std` prescription there is refuted in place),
`docs/contributing/build-and-test.md` (the ACPI VM recipe and its traps),
`docs/contributing/failure-diagnosis.md` (the trace-reading rules). Evidence:
`vm\win2kacpi-debugcon.resume-dstate-closed.log`,
`vm\win2kacpi-debugcon.cirrus-standby-vetoed.log`,
`vm\win2kacpi-qemu-trace.cirrus-standby.log`.

## The vector-0xD1 storm is the accelerator, not the QEMU binary: it reproduces on x86_64 under TCG and vanishes under WHPX

### Environment and operation

Host `minis-w11p-ykm`, scoop QEMU 11.0.0, `qemu-system-x86_64`. Operation:
install a new uniprocessor Windows 2000 SP4 VM (`vm\win2k-acpi.img`, fresh
4 G qcow2, same `D:\isos\win2ksp4.ISO`) with ACPI enabled, in order to obtain
a guest with sleep states, because `ResumeController` has never executed on
Windows 2000 and 2b cannot produce a D-state transition at all. This is
therefore also the Setup-workload experiment the Phase 2d WHPX entry asks for
below its observation 2a.

### What was measured, in the order it was measured

1. HAL swap on the installed 2b image: refuted, structurally. With ACPI
   tables present (`-machine pc`, local APIC still masked), Device Manager ->
   Computer -> "Advanced Configuration and Power Interface (ACPI) PC"
   installs and the next boot is `STOP 0x0000007B (0xEE01B84C, 0xC0000034,
   0, 0)` INACCESSIBLE_BOOT_DEVICE. Parameter 2 is
   `STATUS_OBJECT_NAME_NOT_FOUND`: an ACPI HAL re-enumerates the disk
   controller under the ACPI namespace and this install has no boot-start
   ACPI enumerator. The HAL has to be chosen by Setup. Reverted from the
   `pre-acpi` qcow2 snapshot; 2b is unharmed.
2. APM is a dead end, and it takes two boots to prove. Win2000 does offer an
   APM tab in Power Options under the Standard-PC HAL on this guest, and it
   does let you enable APM support, but Stand by never appears in the Shut
   Down dropdown, before or after the reboot that APM support requires. QEMU
   advertises an APM BIOS that yields no sleep states. (The first check was
   made before that reboot, and the reboot matters to the record: a
   negative taken before the setting was armed would not have been
   evidence.)
3. ACPI on with the local APIC masked off is a broken machine, not a middle
   ground. `-machine pc -cpu pentium3,-apic`: Setup sits on "Setup is
   starting Windows 2000" with EIP pinned at `0x8046566a` across nine samples
   over four minutes. Disassembly identifies that as ntoskrnl's idle loop
   (`sti; nop; nop; cli; cmp (%ebp),%ebp`, `ebp` inside the KPCR). The guest
   is not storming; it is idle, waiting for an interrupt that never arrives.
4. ACPI on with the local APIC present, TCG: the Win2000-Setup storm
   reproduces. `-machine pc -cpu pentium3`, no accelerator flag (TCG). EIP
   samples land on `0x800ca223` and `0x800ca1f6`, two of the three addresses
   the Standard-PC HAL entry below recorded for the storm window, in the
   same `~140`-byte range, at the same Setup screen.
5. The identical machine under `-accel whpx,kernel-irqchip=off`: Setup runs.
   EIP samples are varied and productive (`0xbffba71e`, `0x8045672b`,
   `0x803aba79`, plus the idle loop) and never in the storm window; text-mode
   Setup partitions, formats and copies (disk 393 KB -> 450 MB) and reboots
   into graphical Setup normally.

### What this proves, and what it does not

Proven: the QEMU binary target is not the variable. The Standard-PC HAL
entry's record was `qemu-system-i386` 11.0.0 and left open whether the
binary, the version, the accelerator or the workload mattered. Step 4
reproduces the storm on `qemu-system-x86_64` 11.0.0 with the same Setup
workload, so `i386` is eliminated. Step 5 then changes only the accelerator
on that same command line and the storm disappears, which makes the
accelerator the discriminating variable on this host and this QEMU build.

Not proven: the mechanism. An execution-rate explanation (WHPX drains the
timer queue fast enough) remains the natural reading and remains untested;
so does any explanation resting on how each accelerator delivers the
emulated local APIC's timer. The 11.0.0-versus-11.0.50 axis is also untouched
here; this was one build.

A correction to an assumption this session made and should not have. The 2b
workaround was read as two independently selectable halves, "remove the
local APIC" and "remove the ACPI tables", on the strength of that entry's
note that `acpi=off` alone did not stop the hang. Step 3 shows the other
single-sided combination is also broken, in a new way. The workaround is a
pair, and the storm-avoidance reading of it does not license taking either
half on its own.

### Reusable rules

- An interrupt storm and a dead machine look identical on the screen and are
  opposite in the registers. EIP cycling a small window with a device ISR
  above it is a storm; EIP pinned at ntoskrnl's `sti; nop; nop; cli` is an
  idle guest starved of interrupts. Sample EIP before naming the failure; the
  screen said "Setup is starting Windows 2000" for both.
- A qcow2 snapshot makes a destructive experiment free. The HAL swap was
  known-risky, ran anyway behind `qemu-img snapshot -c pre-acpi`, bugchecked,
  and cost one revert. Take the snapshot even when you expect success.
- Enabling a Windows power feature is not the same as having it: APM needed
  a reboot before its absence of sleep states could be honestly measured.

### Consequences

`vm\win2k-acpi.img` is a WHPX-only VM on this host; TCG storms it. It exists
solely to execute `ResumeController` on Windows 2000, which no other VM in
the estate can do. 2b keeps its Standard-PC HAL flags unchanged and remains
the Phase 3-5 evidence VM.

## The Full-Speed bugcheck localized: a missing guard on the branch nobody takes

### Environment and operation

Host-side static pass only, no VM run and no code change. `link -dump
-disasm` from `tools\ntddk\bin` over `ntoskrnl.exe` (Win2000 SP4), both
`usbport.sys` builds, both `usbhub20.sys` builds, and Win98 SE's 1.1
`usbhub.sys`. The operation was resolving the bugcheck parameters recorded in
the entry below.

### What is proven

The four `STOP 0x0000000A (0xFFFFFFFC, 0xFF, 0x00000000, 0x804006B2)`
parameters all resolve, and the instruction-level chain is transcribed in
`docs/usb-xhci-info/usbport-miniport-abi.md` section 8. Summary:
`USBPORT_GetTt`'s `TtCount <= 1` branch `CONTAINING_RECORD`s an empty
`TtList` into `0xFFFFFFEC`, `USBPORT_OpenPipe`'s null check passes it, and
`ExfInterlockedInsertTailList` faults reading `[0xFFFFFFEC + 0xC + 4]`. It is
deterministic rather than unlucky because declaring
`USB_MINIPORT_FLAGS_USB2` is what marks usbport's own root-hub device handle
`UsbHighSpeed`, and usbport's root-hub descriptor template hardcodes
`bDeviceProtocol = 0` so that handle can never acquire a TT.

Also proven, and it refutes the hypothesis this entry's predecessor recorded:
`MiniPortVersion` is not a lever on these builds. Neither contains a version
branch; the hub descriptor type is a fixed `0x29` template byte.

### What was inferred, not observed - and was then measured false

That reporting every managed root port as High Speed leaves a USB 1.1 hub on
a root port re-entering the same fault one level down. It follows directly
from the transcribed `GetTt`/`CreateDevice` behaviour, but nothing had run.

Batch 7b-V0 ran it on both targets, and it does not fault. A QEMU `usb-hub`
(USB 1.1, no TT) on a root port with a Full Speed device behind it produced
`HubAddr` = the hub's own address, the documented "the TT lookup succeeded"
reading, on Win98+NUSB and on Windows 2000 SP4 alike. So a TT record existed
for a hub that physically has none, and the empty-list branch was never
reached. What the topology costs instead is that the behind-hub device never
enumerates, and the driver's requeue-and-retry refusal is unbounded: 1,803
refusals and a dead Win98 guest (clock stopped, pointer still tracking, no
click accepted, no bugcheck), against 17 and an intact machine on Windows
2000.

Two rules, and the first is the reason this section existed:

- The inference was filed honestly here and overstated everywhere else. This
  entry said "nothing has run";
  `docs/contributing/implementation-invariants.md` said "Verified, not
  inferred" and a memory note said "Verified" outright, and a demo rule was
  written on the strength of it. When one document hedges a claim and
  another asserts it, the hedge is the one that was thinking, and the
  assertion is what a later reader will act on. Propagate the hedge, or
  measure it.
- A disassembly can say what a function does with an empty list; it cannot
  say whether the list is empty. The `GetTt` read was correct in every
  particular and still predicted the wrong outcome, because the missing fact
  was upstream: who calls `Initialize20Hub`, and on what belief about the
  hub's speed.

### Reusable rules

- A guard present on one branch is not a guard on the function. `GetTt`'s
  empty-list test is real, correct, and twenty instructions away on the
  branch that a zero `TtCount` never takes. Grepping the function for "is
  there a null check" would have answered yes.
- Map an IAT slot by how the code uses it, not by counting the import
  listing. An off-by-one in the name-to-address arithmetic produced six
  confident-looking call sites belonging to
  `KefReleaseSpinLockFromDpcLevel`. The fastcall argument shape (`ecx` =
  list head, `edx` = entry, one `push` for the lock) identified the true
  slot immediately and would have caught the error on the first site
  examined.
- `link -dump` prints VA against the image's own base (`0x10000` for
  usbport, `0x400000` for ntoskrnl even though it loads at `0x80400000`),
  and in these images a section's RVA equals its file offset, so `od -j
  <RVA>` reads a data template straight out of the file. Both descriptor
  templates that decided this analysis were read that way, not inferred.
- Extend "ReactOS adds guards the shipping binaries lack" to a search
  strategy. It has now produced the same shape twice (`NumberOfPorts = 0`,
  and this). When a ReactOS routine's early-return guards are what make a
  contract safe, check for them in the binary before relying on the
  contract.
- A whole-binary absence is a stronger negative than a traced path, and is
  sometimes available. "Win98's 1.1 `usbhub.sys` cannot see High Speed"
  rests on zero bit-10 references across the entire disassembly against two
  Low-Speed extractions, which needs no call graph, unlike the negative
  claims that cost three rounds.
- When a change makes the driver stop telling somebody the truth, ask what
  was reading that truth. Deciding to report every root port as High Speed
  was argued entirely in terms of what it fixes; a stop-time review found
  two things it destroys, and neither was visible from the fix's own
  reasoning. The checkpoint's speed evidence lived in a trace line derived
  from the very field being overridden, so it vanished silently; the suite
  stayed green because every host vector asserts on the shadow, which still
  holds the truth. And the interrupt interval turned out to be irrecoverable
  rather than merely recomputed, because usbport's `bInterval` conversion
  branches on the speed it was told and hands the miniport only the lossy
  result. The generalisation: an intended untruth has a blast radius, and it
  is found by listing the consumers of the value, not by re-reading the
  change.
- A replacement diagnostic can be the wrong shape and still pass. The first
  counter for the override incremented per status query, so it measured
  usbport's polling rate, could not name a port, and could not distinguish
  Full from Low Speed, which is the distinction the checkpoint asks for. A
  counter attached to a poll answers "is this being asked about"; one
  attached to a state transition answers "did this happen". Prefer the
  transition, and test it by polling twice and asserting the count did not
  move.
- A bounded trace site is a budget, and the budget is spent by whatever
  moves the value, which is usually not the thing being diagnosed. Every
  macro here caps at 32 prints per site on a driver-image static that no
  start, stop or resume resets. This took four rounds on one diagnostic, and
  the three rejected shapes are the lesson because each looked sufficient
  when written: a `_LIMITED` line with no counter behind it; a cumulative
  `CheckController` counter, which moved on every resume; and a tally of
  what is attached now, which fails too. "A resume recomputes the same
  number" was a race, since a resume genuinely takes the bus down (HCRST
  clears `PP`, ports are re-powered, and a device is not re-detected the
  instant the seed reads it). Change-gating saves none of them once more
  than one port is populated, because the distinct values cycle round and
  each differs from the one before it. On Win98, which idle-suspends within
  about half a second of a start, all three were exhausted within seconds.
  What worked was making the gate semantic rather than budgetary: a value
  monotone within a start, a set of "speeds seen", changes at most once per
  member however the bus behaves. The rules, in the order they are worth
  applying: ask what else moves this value, not just whether the interesting
  event moves it; prefer a monotone set over both a running total and a
  live-state snapshot when the question is "did this ever happen"; and if
  the gate is what you are relying on, count the firings, because a set is
  idempotent under OR and will look identical whether the gate works or not.
  That last one turned a review-only property into a number: the mutation
  scores 33 firings against a budget of 32.
- A trace macro's state is per expansion, so it is per driver image while
  the values it watches are per controller. This was the fourth round's
  finding and it is the one that generalises furthest. Two xHCI controllers
  both bind one image and reach the same expansion carrying their own
  extension's value; if the values differ, successive calls alternate, every
  sample counts as "changed", and a 500 ms site spends its 32 prints in
  about eight seconds. A value that is monotone or quiet per controller is
  not therefore quiet at the site. The only shape immune to it is a site
  with no budget whose gate is semantic, and such a line should carry a
  per-controller discriminator (`StartEpoch` here), or two controllers
  produce indistinguishable evidence. Every `XHCI_DBG_VALUE_CHANGED` in
  `xhciCheckController` has the flaw today; it is invisible on
  single-controller VMs, and that is the reason it is written down.

## A Full-Speed device on a root port bugchecks both targets, and a healthy trace is not a living guest

### Environment and operation

Host `minis-w11p-ykm`, scoop QEMU, 2a Win98 SE + 2b Win2000 SP4, debug build
`xhci98.sys` 49,131 B (`built Aug 2 2026 11:19:44`, SHA-256 `cfe5b8cb...`,
extension `0x1DC0`), byte-identical on both targets. Operation: the Phase 5
root-hub checkpoint. Plug/unplug LS/FS/HS devices and observe status
polling, asynchronous reset, decoded speed and change clearing.

### Observed

Any Full-Speed device on a managed root port bugchecks both primary targets;
High-Speed works on both. Controlled matrix, one variable:

| Target | Device | Class | Speed | Result |
|---|---|---|---|---|
| Win98 | `usb-audio` | audio | FS | `Windows protection error` at startup |
| Win98 | `usb-audio` | audio | FS | `0028:C002F70E` in VXD NTKERN(01)+0000E32E |
| Win98 | `usb-kbd` | HID | HS | healthy |
| Win2000 | `usb-audio` | audio | FS | `STOP 0x0A (0xFFFFFFFC,0xFF,0,0x804006B2)` `IRQL_NOT_LESS_OR_EQUAL` |
| Win2000 | `usb-kbd,usb_version=1` | HID | FS | identical STOP 0x0A, same four parameters |
| Win2000 | `usb-kbd` | HID | HS | healthy |

`0xFFFFFFFC` is NULL-4: a list or array walked from a null base. The crash
lands after the root-hub callbacks have done their work correctly. The trace
shows connect, `C_PORT_CONNECTION` latched and cleared, an asynchronous reset
completed by PRC, `C_PORT_RESET` latched and cleared, and a correct
steady-state `0x0103` (connected, enabled, powered, Full Speed), at the point
usbport takes the device.

### Proven vs inferred

Proven: speed is the variable, not device class and not target. The first
four runs used `usb-audio` for every FS test and `usb-kbd` for every HS
control, so "FS crashes" and "audio crashes" were indistinguishable; one
`usb-kbd,usb_version=1` run, a Full-Speed HID, reproduced the Win2000
bugcheck with byte-identical parameters and separated them.

Inferred, not proven: the cause is the registration packet still declaring
`MiniPortVersion = USB_MINIPORT_VERSION_EHCI` with `USB_MINIPORT_FLAGS_USB2`
(`src/xhci_dispatch.c`, whose own comment calls this provisional: "revisit
once the spike passes"). On the EHCI model a root port can only have a
High-Speed device enabled; Full/Low-Speed devices are released to a
companion UHCI/OHCI controller. This driver reports an FS device as
connected and enabled on what usbport believes is an EHCI root port, a state
EHCI cannot produce, and no companion exists.
`docs/usb-xhci-info/usbport-miniport-abi.md` carries
`USB20_PORT_STATUS_RESERVED1_OWNED_BY_COMPANION (1 << 2)` as "not used by
xHCI ports", but usbport does not know this is xHCI: the packet says EHCI.

Unknown: whether changing `MiniPortVersion` to the XHCI value, dropping
`USB2`, or reporting the companion-ownership bit is the right repair. Each
also re-gates `TakePortControl` and `USBPORT_RH_SetFeatureUSB2PortPower`, and
whether a 2195.x binary tolerates the XHCI version is still the open runtime
question Phase 3 recorded.

### Two artifacts that are not driver bugs

- QEMU asserts `PORTSC.CSC` on every port when port power is applied. All
  four ports report `C_PORT_CONNECTION` once per start/resume with nothing
  attached (`0x00010100`, then `0x00000100` for the rest of the run), on both
  targets. The driver is correct to latch, report and acknowledge it: the
  bit is a genuine `PORTSC.CSC` read (`XhciPortShadowUpdate` derives the
  hub-class change from that bit alone, with no shadow inference), and an
  unacknowledged change bit suppresses the controller's next real event.
- A USB device on the QEMU command line wedges SeaBIOS: black screen, IDE
  reads frozen at 413, because SeaBIOS tries to initialise it for boot
  input. Attach through the monitor at ~+4 s instead, after BIOS and before
  the driver. This looked like a corrupted disk image; `qemu-img check` was
  clean and a plain relaunch booted normally.

### Reusable rules

- A healthy trace is not a living guest. Always `screendump` before calling
  a VM run a pass. Twice in this session a complete, correct driver trace
  was read as success and a screenshot then showed a bugcheck, once a Win98
  protection error, once the Win2000 STOP 0x0A. The trace ends where the
  driver's involvement ends, not where the system dies, so the driver can be
  entirely right and the machine still dead. This is the single most
  expensive error of the run and it happened twice.
- Control for device class as well as the variable you think you are
  testing. Two devices that differ in speed usually differ in class too.
- A trace site's budget is not a count (recorded in an earlier entry, paid
  again): `cb ResumeController` stops at 4 (`XHCI_DBG_CALL_LIMIT`) and `RH
  reset: completed` is `XHCI_DBG_VALUE_CHANGED`, so it printed once for three
  resets. Count init sequences, not callback lines.
- A completion site inside a shared helper makes an asynchronous path look
  synchronous. `RH reset: completed` prints from inside `xhciRhRefresh`,
  which the event path calls before its own trace lines, so the completion
  appeared above the event that carried it. Read the call graph before
  concluding a callback did the work itself.
- Shut the guest down cleanly. Repeated `quit` on a running Win98 cost
  ScanDisk runs, a forced Safe Mode boot (which loads no driver, hence an
  empty trace), and a misdiagnosis.

### The task 7 run that closed it, per clause

Phase 5 task 7's remedy (every connected managed root port reported High
Speed, `USB_MINIPORT_FLAGS_USB2` kept) ran and passed on both targets with
one debug build: 49,259 B, SHA-256 `84165576...`, byte-identical on 2a and
2b, 5,565 host checks, both gates green, no new import (12 pairs). The
pre-fix binary is archived at `out\pkg-debug-pre-task7\` (SHA-256
`cfe5b8cb...`) so the comparison is reproducible rather than remembered.

| Device | Win98 SE (2a) | Windows 2000 SP4 (2b) |
|---|---|---|
| FS HID (`usb-kbd,usb_version=1`) | no crash; 3 reset cycles, then `RH_ClearFeaturePortEnable` | no crash (was `STOP 0x0000000A`) |
| FS audio (`usb-audio`) | no crash (was `Windows protection error` / `0028:C002F70E`) | no crash |
| FS unplug | `C_PORT_CONNECTION` latched and announced | same |
| HS HID (`usb-kbd`) | unchanged, healthy | unchanged, healthy |

Every device that bugchecked now enumerates to a devnode and stops there,
the correct Phase 5 ending, the absence of Phase 6. Device Manager on 2a
shows the root hub and the controller with no yellow bang and the
un-enumerated peripheral under Other devices; 2b ends at the "function
driver was not specified" wizard, the same reading the High-Speed control
gave. Both guests reached `QueryEndpointRequirements` -> `OpenEndpoint` with
a Full-Speed device attached, the call path that ran off `USBPORT_GetTt`'s
null base, which is what says usbport accepted the report. Every
failure/escalation/fatal/watchdog counter read `00000000` on both, `isr =
claimed = dpc = events` throughout (2a ends at `0x18`, 2b at `0x0F`), and
both shut down cleanly through `Suspend -> DisableInterrupts -> Stop`.

The replacement evidence for the speed decode worked as designed, and its
quietness is the part that was measured: exactly two `RH first decode of a
speed` lines per target, `00010102` (epoch 1, hub port 1, FULL) and
`00010203` (epoch 1, hub port 2, HIGH), and a second Full-Speed device on hub
port 3 correctly added no third line, because `FULL` was already in
`RhSpeedsSeen`. `XHCI_RH_SEEN_HIGH` being present is the negative control:
the override did not manufacture the reading.

## Three orderings and a vacuous vector: what writing Phase 5 tasks 4-6 measured

### Environment and operation

Host `fw-w11p-ykm`, no VM. Writing the root hub's two asynchronous port
operations (reset and resume), the `UsbPortInvalidateRootHub` announcement,
and the power/lifecycle clauses, against `test_init`'s synthetic controller
and `test_port`'s pure vectors. Suite 5,197 -> 5,527 checks; build gates pass
with no new import.

### Observed, with the evidence

Eleven review rounds went into what a preemption may conclude, each
overturning the one before, and the last one caught a hardware behaviour
invented from plausibility. The sequence is kept because each step is a
distinct way of getting the same register wrong.

Round three: preemption disarmed the port and reported nothing, so a reset
usbhub was blocked on ended with no `C_PORT_RESET` ever latched.

Round four: the repair latched that change before the interrupting write had
been issued, while PORTSC still had `PR` set, and `C_PORT_RESET` means the
sequence is complete. So it left the reset armed instead, to be completed by
the `PRC` the write would produce.

Round five: that `PRC` is forbidden. "Note that this flag shall not be set to
'1' if the reset processing was forced to terminate due to software clearing
PP or PED to '0'" (PRC, Table 5-27, p.377), and the same exclusion is on CSC,
PEC, PLC and WRC: a software-initiated power-off produces no change bits at
all. Round four had reasoned the follow-through instead of reading the
register table, and then the host model was written to match the invention,
which is what made the wrong rule look tested.

Round six: "after the write" is still not "after the port stopped." The
repair reported on the strength of having issued the write, and footnote 91
(p.375) names this exact case as why that is not safe: "the PP flag may be
delayed in reflecting this change, e.g. due to waiting for a port related
state machine to complete reset signaling", with p.375 adding "software shall
read PP and confirm that it is reached its target state ... undefined
behavior may occur if this procedure is not followed". A reset in progress is
the spec's own example of what delays a power-off.

Round seven: the read-back was of the wrong bit. `PR` "is '0' if PP is '0'"
(p.371), so after a power-off that landed it reads 0 whatever the port did;
the test stood on the state it was confirming. And inside the lag window it
is wrong the other way: a reset can complete normally (`PR` 1->0, `PRC` set)
while `PP` still reads 1, and a `PR` test calls that a preemption, reports
it, and leaves a real `PRC` pending to be mis-attributed to the next armed
operation. `PP` is the bit the spec says to confirm, and it is the state that
makes the termination true.

Round eight: the confirmed path left a genuine `PRC` behind. A reset that
completed just before the interrupting call leaves `PRC` set (RW1CS, so it
stays until software writes a 1, which the power-off's neutral write does
not), and retiring there reported that reset as preempted while leaving a
real completion in the register for the next refresh to attribute to
whatever is armed by then. Fixed by folding the port in through the ordinary
refresh before concluding: it acknowledges the bit and, when the completion
is real, claims the armed generation as the completion it was. That refresh
is gated on the same `PP` confirmation, because its acknowledgement is a
neutral write that carries `PP` as it reads.

Round nine: the confirmation was made once and then forgotten. "Before
modifying it again" binds every later writer of that port, not the callback
that issued the change, and the writer that makes it reachable is the least
conspicuous one. Every value this family writes is built on a neutral base,
and a neutral value carries `PP` exactly as it reads, so an ordinary status
query acknowledging a change bit on a port whose power-off is in flight
writes `PP` back as 1 and cancels it. No preemption, no armed operation, no
unusual controller: a lagging `PP` and a hot-plug. Fixed by making the
outstanding confirmation state on the port, routing all seven of the file's
PORTSC writes through one helper, refusing operations in that window rather
than skipping their write, and bounding the wait in the health poll.

Round ten found two more in round nine's own fix. The health poll collected
the confirmation with a bare read of PORTSC, one bit taken and the rest
discarded, which breaks the oldest port rule in this driver: a change bit is
acknowledged the instant it is observed, or the controller stops reporting
that port. It was the only PORTSC reader in the file that did not fold what
it read into the shadow. And the confirmation was being made inside the
write helper, so it only happened when there was something to acknowledge: a
quiet port never settled its wait. Both fixed by making the read the place
the confirmation happens, and by routing the poll through the ordinary
refresh.

Round eleven collected the debt the previous two left. Introducing a write
helper that can decline a write makes every caller's "I wrote it, therefore
X" unsound, and neither resume-completion site had been revisited: both
counted the resume complete whether or not the terminating write went out
and then disarmed the port, which is an orphan and a false completion in one
line. And the preemption gate tested only the reset, so an interrupting
write that had not taken effect still abandoned an in-flight resume,
disarming the one thing that was going to end the signalling.

Round twelve found the last of it a layer below where the rest had been
looking: the shadow's translation manufactured the completion the driver had
stopped claiming. `C_PORT_SUSPEND` was derived from "the link left U3 or
Resume", a shape other things produce too. Derived now only on a port still
enabled and connected.

Round thirteen corrected why. Round twelve's stated case, "a disable sets
`PLC`, because the flag's row excludes only a software-cleared `PP`", was
invented: "this flag is set to '1' due to the following PLS transitions"
(p.378) enumerates them, and a disable is not among them. That is round
five's error again, committed in a file that already carried round five's
lesson. The gate survives on a real path this driver builds itself: `PLC` is
RW1CS, the acknowledgement of a legitimate U3-to-Resume is deferred while a
Port Power change is in flight, and once the power-off lands `PLS` no longer
reads Resume, so a stale `PLC` beside it is indistinguishable from a wakeup.

Settled answer, one half from each round: the interrupting write does end
the reset and ends it silently, so the driver that issued it is the only
thing that can report `C_PORT_RESET` and must, after the write, and only
when a read-back of `PP` confirms the Powered-off state. Otherwise it
reports nothing and leaves the reset armed, where the deadline it already
had covers it.

Four further findings from the same task:

0. A stop-time review found the two the mutations did not reach, both about
   an operation that is not proceeding normally. A port could be armed for
   the life of the driver: `UsbPortRequestAsyncCallback` answers 0 whether it
   armed a callback or failed to allocate one, and only a callback disarms a
   port, so a pool failure leaves every later reset on that port refused as
   busy, with a device on it that never enumerates and nothing saying why.
   And the armed state excluded reset and resume from each other only, so
   `RH_ClearFeaturePortPower` could take VBus away mid-reset and
   `RH_ClearFeaturePortEnable` could write PED into a port the controller
   was resetting. The first is the same hazard the command engine already
   carries a detector for; building a second mechanism on the same
   uncancellable timer without the same detector was the gap.
1. A stale timer completed a fresh caller's reset. The reset watchdog read
   PORTSC before claiming its generation, on the sound argument that a
   deadline expiring is not evidence of failure. The vector "a pre-suspend
   timer arriving after the resume touches nothing" failed on three checks:
   the refresh that read the port claims whatever is armed there, so a
   callback with no claim of its own completed an operation belonging to
   somebody else.
2. A resume asked of a port already mid-reset returned success. The busy
   test sat last, inside the arming primitive, so the operation-specific
   branch ("this port is not suspended") answered first: true of the link
   state, and a lie about the request.
3. A mutation of the exact defect the task exists to prevent passed the
   whole suite. Applying reset's claim rule to whatever is armed, which is
   the silent-resume-truncation this task's argument is entirely about,
   failed 0 of 1,989 checks. The mid-interval event the vector posted latched
   nothing: a bare PLC while the port is still in Resume produces no
   hub-class change, because the port has not left the suspend yet.
   Re-posting it as an over-current change makes the same mutation fail 2.

### Proven / inferred / unknown

Proven on the host: the three behaviours above, and that five other
mutations (a DPC draining no deferred work, a rebuild restarting the per-port
generations, the busy test moved back, a quiesce retiring nothing, an
announcement ignoring the notification gate) are each caught.

Inferred, from the ReactOS mirror rather than from the shipping binaries:
that `USBPORT_InvalidateRootHub` calls `RH_DisableIrq` back into the miniport
as its first act (`roothub.c:916-956`), which makes announcing under the
driver's own lock a self-deadlock rather than a lock-order risk. The host
model performs the re-entry so the double acquire is caught mechanically,
but the re-read out of both `usbport.sys` builds is still owed.

Unknown until the checkpoint runs: everything about how `usbport.sys`
actually drives this family, the hub-class bit order included.

### Reusable rules

- "The spec does not forbid it" is not "the spec says it happens", and a
  list of what sets a flag is not the same as the exclusions under it. Round
  five learned this and it was written down here; round twelve committed it
  again, on the same register, arguing from `PLC`'s closing exclusion that a
  disable must set the flag. The exclusion narrows an enumerated list it
  does not define. The operational rule that would have caught both: when a
  claim is about hardware behaviour, quote the sentence that asserts the
  behaviour. If the only sentence you can quote is a prohibition on
  something else, you have not read the answer yet. And transcribe the list
  into `docs/usb-xhci-info/xhci-data-structures.md`, because what is absent
  from there is what gets reasoned about.
- Stopping the code from claiming something is not the same as stopping it
  from reporting it. Four rounds went into what the preemption path may
  conclude, and every one was about the driver's own bookkeeping, while the
  translation layer underneath went on deriving "the resume completed" from
  a link-state change a disable produces too. When a report carries an
  inferred meaning, ask what else produces the reading it is inferred from.
- Adding a gate to a shared helper invalidates every caller that assumed the
  helper always succeeds; audit them in the same change. Routing seven
  writes through one function that can now decline silently converted every
  "the write went out, so the operation is done" into a guess. Three rounds
  in a row were spent on consequences of that one refactor, each found by
  review rather than by the suite, because the callers still compiled and
  still passed. The mechanical form: when a helper gains a failure mode,
  grep its call sites and decide for each what a refusal means there.
- "One way to write it" needs "one way to read it" beside it. Both rounds of
  this fix were broken by a path that read or wrote the register outside the
  function that knows the obligations, and in both cases that path was added
  later, for an unrelated reason, by the same author who had just finished
  writing the rule down. Route the readers through one function too, and
  put the confirmation in the read rather than in the write it guards: a
  rule attached to a write only runs when there is something to write.
- An obligation the spec words as "before doing X again" is state, not a
  step. It binds every later writer, so checking it where it was incurred
  leaves every other path free to break it, and the path that does is
  usually the dull one nobody was thinking about (here: a status query
  acknowledging a change bit). The mechanical form of the fix is the useful
  part: make every writer go through one function and put the rule in it,
  rather than adding the check wherever the bug was found. Then bound the
  wait, because a confirmation that never arrives must not turn into a
  permanently unusable object.
- Sticky state you did not create is still yours to drain. An RW1C bit set
  before your call is not context you can ignore: leave it, and the next
  reader attributes it to the next operation. Whenever a path ends something
  without going through the normal observation, ask what the port was
  already holding.
- One flag cannot answer two questions, and the tell is a case that answers
  them differently. "May I write to this port again" and "did my write end
  the reset" looked like one condition until a disable turned up: `settled`
  but ending nothing. That is round seven's lesson repeating one level down,
  inside the fix for it.
- A read-back has to name the bit the operation targets. Confirming a bit
  that the target state masks is a tautology, not a confirmation: `PR` reads
  0 because power is off, so a `PR` test after a power-off proves nothing,
  and in the window where the two disagree it actively lies. Ask what the
  write was supposed to make true, and read that. The corollary is a nice
  one: the same question retired a special case instead of adding one,
  because "a disable can never end a reset" falls out of the single `PP`
  confirmation rather than needing a branch.
- "I wrote it" is not "it happened", and this project has now paid for that
  three times. Phase 4 task 5 learned it twice (a quiesce that trusted its
  own flag instead of reading PCI Command; a port-power pass that needed a
  read-back and a settle) and wrote down: after fixing "trusted a record
  instead of reading the register", grep for the other places doing the same
  thing. A path added later did it again. The grep is not a one-off chore at
  the end of a task; it belongs in the review of every new path that
  concludes something about the hardware.
- "The hardware will report this" is a claim about the hardware, so read the
  register table. The rule that a terminated reset still sets `PRC` was
  reasoned (a port leaves the Resetting state, so surely it reports the
  transition) and the spec says the exact opposite in the PRC row itself.
  This project already has the rule (`AGENTS.md`: bit positions and
  structure layouts come from the transcribed doc, verified against the PDF,
  never from memory) and it was applied to bit positions while a behavioural
  "shall not" in the same table went unread. The transcribed document is the
  mechanism, so anything a design rule leans on has to be in it. This
  exclusion was not, which is how it came to be taken from memory. It is
  now.
- A model written to match an invented behaviour makes the wrong rule look
  tested. The suite was green, with vectors specifically about the
  follow-through, because the model had been taught the same fiction as the
  driver. When a model gains a behaviour rather than a layout, the source
  has to be the spec text, cited in the model.
- Count the ways an operation can end, and check that each one reports what
  it observed. Five rounds of review here found four endings (normal
  completion, watchdog, age detector, preemption), each later one introduced
  by the fix for the one before. Two rules that pull against each other: an
  ending nobody is told about is worse than a failure, and a report that
  precedes the act it claims is worse than silence. Where the controller is
  architecturally silent about an ending, the software that caused it is the
  only thing that can report it, and reports after doing it.
- When you build a second mechanism on a service whose failure it cannot
  see, copy the detector as well as the pattern. `UsbPortRequestAsyncCallback`
  cannot report failing to arm, so anything it drives needs an independent
  age measure in the health poll, and the rescue must finish the operation
  the way its timer would have, or it trades one silent failure for another.
- An exclusion that "already works" through another rule is not an
  exclusion. Suspend on a port mid-reset is refused by its own PED
  precondition today; the armed check is written anyway, and its vector
  forces the precondition to be satisfied first so the refusal can only be
  the exclusion answering.
- Not every conflicting request should be refused. Power-off and disable are
  the caller taking the port out of service, and a power cycle is usbhub's
  recovery for a reset that is not finishing, so refusing it would block the
  recovery on the strength of the thing being recovered from. They preempt
  and proceed; retiring first makes the in-flight timer stale.
- In an uncancellable callback, prove ownership before touching anything,
  even when the thing you would touch first is only a read. A read that
  folds state into a shared structure is not read-only, and the ordinary
  case for these callbacks is having no claim at all.
- An exclusion belongs before the operation-specific decisions, not after
  them. A busy port must refuse every answer, including the ones that would
  not have written; otherwise a request gets an answer that is true of the
  state and false about the request.
- When a vector says "X arrives and Y is unaffected", assert that X actually
  happened. Otherwise the negative half is free, and it is the half the
  vector was written for. Found by mutation, which is the only thing that
  finds it. Three instances in one day, so it is a habit rather than a slip:
  the event that latched nothing, the age assertion that stopped after the
  first poll (an inherited count also answers "not yet", and never crosses
  at all), and the sweep-order vector whose shadow still described the port
  as it was before the suspend, because a port operation writes and returns
  without reading back. Where the subject is a count or a state machine,
  walk the whole cycle.
- A token that separates one start from the next does not separate a resume
  from what preceded it. usbport zeroes the miniport extension before
  `StartController` and not before `ResumeController`, so anything relying
  on the start epoch alone is unprotected across a suspend/resume pair,
  which on Win98 happens within about half a second of every start.

### Affected

`src/xhci_rh.c`, `src/xhci_port.c`, `test/test_init.c`, `test/test_port.c`;
rules moved into `docs/contributing/implementation-invariants.md` "Root Hub
Reporting" and `docs/contributing/design/05-locking-model.md` section 7.

## A negative claim grepped from one function's extent is not a negative claim about behaviour - and its repair is not a licence for the obvious consequence

### Environment and operation

Host `fw-w11p-ykm`, no VM. Phase 5 task 1's static pass over the root-hub
paths of both shipping `usbport.sys` builds (SP4 5.00.2195.6681, NUSB
5.00.2195.5652), disassembled with `tools\ntddk\bin\link.exe -dump -disasm`.
The Windows 2000 DDK ships the same COFF dumper as `dumpbin`, which removes
the Visual Studio dependency this workflow used to have. Call it from
PowerShell: Git Bash rewrites a `/dump` switch into a path.

### Symptom and evidence

Eight claims were written up and committed. It took three independent
re-reads of the same binaries to settle them, and the same claim was
defective in all three: first wrong, then wrongly repaired, then still
over-general. A fourth round found no defect in that claim but did find that
the pass had read the SP4 power/chirp path and written it up as though it
described both targets, which it does not.

Round one.

- Wrong: "None of the three root-hub request routines acquires a spin lock."
  The class GET_STATUS routine and the status-change scan really take none,
  but the feature-routing routine reaches `KfAcquireSpinLock` on the
  `SET_FEATURE(PORT_POWER)` branch: `0x20638` tests `MiniPortFlags & 0x10`,
  `0x20641` calls `USBPORT_RH_SetFeatureUSB2PortPower` (`0x229CA`), whose
  `0x22A2E` calls `0x27ADA`, whose `0x27AED` acquires lock `0x2D120` via IAT
  entry `0x2CA30` and `0x27C3C` releases via `0x2CA2C`.
- Overstated: "`RH_EnableIrq`/`RH_DisableIrq` are a re-poll latch, not an
  interrupt mask, so the miniport must never touch `IMAN.IE`." The control
  flow behind the first half is correct and reproducible. The second half
  does not follow from it: usbport's image cannot show what a miniport's
  callback body touches.
- A third, smaller miss: an early return in the status-change scan (bit 1 of
  SP4 `FdoExt+0x1C6` at `0x21488`, NUSB `FdoExt+0x1C2` at `0x20E16`) that
  yields "no changes" before validating the buffer, calling any callback, or
  calling `RH_EnableIrq`.

Round two, on the repair. The fix for the first item asserted the
consequence that seemed to follow, "so `RH_SetFeaturePortPower` can be
entered with a usbport spin lock held", and that is also wrong. The
replacement does not disprove the claim so much as fail to reach it: the
trace shows this helper releasing its own lock before its downstream calls,
and says nothing about a caller-held one. `0x27C49` returns to `0x22A33`,
and the callbacks are invoked afterwards at `0x22ADE` and `0x22B0A`: the
helper holds the lock only to build its companion-controller snapshot and
drops it before calling any miniport callback. NUSB has the same ordering
(`0x2004F` -> `0x20059` -> `0x222EC`, acquire `0x27465`, release `0x275B4`,
callbacks `0x22400`/`0x2242C`).

Round two also narrowed the gate wording: "closed before each poll" is not
shown, because the scan is dispatched with no disable beside it (SP4
`0x218FB`, NUSB `0x21289`). What is shown is disable-on-invalidate plus a
single enable site per image, SP4 `0x215F4`, NUSB `0x20F82`, against two
disable sites each (SP4 `0x21C56`/`0x1D6C9`, NUSB `0x215E4`/`0x1D265`).

Round three, on the retraction. Even the retraction generalised: "no
root-hub callback is entered holding that lock" is broader than a trace of
one helper supports while caller-held locking is untraced. The supportable
claim is "this helper releases its lock before either of its own downstream
calls".

Round three also caught a borrowed unchecked claim. Round two's report had
said the surviving GET_STATUS/SCE banners were "accurate for their own call
graphs", and that phrase was promoted verbatim into the documentation, but
it had not been a transitive walk, only a re-check of the routines' extents,
i.e. the same invalid test the whole episode started with. Asking the
question directly produced the real walk: no acquisition in usbport-owned
code on the GET_STATUS execution path or in the SCE scan, leaf helpers
included, with two exclusions worth keeping. Miniport callback bodies are not
in these binaries, and GET_STATUS's enclosing function (`0x2080C`) has other
request branches that do reach the port-power lock, so the result is about
the path, not the function.

Round four, on target scope. Not a locking finding at all: the pass had read
SP4's `USBPORT_RootHubPowerAndChirpAllCcPorts` and written its shape as the
contract. SP4 waits 100 ms (`0x22C55`) and gates the chirp loop on interface
`Version >= 0xC8` (`0x22CBB`) before calling at `0x22CCE`. NUSB does neither:
power at `0x225EC`, chirp gate at `0x225F7`/`0x225FC`, packet `0x12C` at
`0x22623`, no wait and no `Version` compare, because its wrapper has no
`Version` field.

The same round found the mapper is seven instructions and
not five, the Set/Clear family has twelve packet slots and not ten, and the
SCE early-return flag lives at `FdoExt+0x1C6` on SP4 but `+0x1C2` on NUSB.
When two builds are in scope, a fact read from one is a fact about one. The
existing "trust order" note says the binaries outrank ReactOS, but says
nothing about the binaries disagreeing with each other, and here they do.

Rounds five and six, on the chirp gate. Round four's own repair said "chirp
does not imply `Version >= 200`"; round five falsified that, and round six
falsified round five's replacement ("different mechanisms, same outcome").
The verified reading: the registration copy gate is identical in both
builds. Both withhold packet `0x12C` below Version 200 (SP4 `0x27D9E`,
`0x27E0F`, `0x27EC0`; NUSB `0x27716`, `0x27782`, `0x27836`). What differs is
that SP4 re-checks `Version` at the call site (`0x22CBB`) and NUSB does not:
NUSB tests only `FdoExt+0x48` bit 1 (`0x225FC`, set at `0x2D3D8` on a path
with no `Version` test) and calls the slot at `0x22623` with no null check.
So a sub-200 registration is a null call on Win98. The difference is a
hazard, not a stylistic variation, and three drafts in a row described it as
something milder.

### Proven / inferred / unknown

- Proven (re-derived from the binaries in rounds two and three): a lock is
  reached on the USB2 port-power branch, and this helper releases it before
  either of its own downstream `RH_SetFeaturePortPower` calls, in both
  builds, scoped to that helper and not to callback entry in general; the
  sole `RH_EnableIrq` site in each image is SP4 `0x215F4` / NUSB `0x20F82`;
  the SCE early return exists; and, from a completed transitive walk, no
  acquisition in usbport-owned code on the GET_STATUS execution path or in
  the SCE scan, leaf helpers included.
- Inferred: that never touching `IMAN.IE` is right. It rests on the observed
  lifecycle plus the xHCI fact that `IMAN.IE` gates the whole interrupter, so
  masking it would silence transfer completions too. It is a design
  decision, recorded as one.
- Unknown, and it is the half that decides the question: whether a caller
  holds `MiniportSpinLock` around these routines. Neither round traced it,
  so ReactOS's split root-hub locking contract is still neither confirmed
  nor refuted. The driver is written not to depend on the answer.

### Reusable rules

1. A negative claim about behaviour cannot be established by grepping one
   function's extent. "No lock is taken here" needs the call graph, not an
   address range. The grep also used the wrong IAT entries, which is the
   second half of the same failure: a negative result from a search is only
   as good as the proof that the search would have found a positive.
2. Finding a call-graph fact does not license its apparent consequence. This
   is the rule that cost the extra round. "A lock is acquired somewhere on
   this path" and "the callback runs under that lock" are two claims; the
   second needs the acquire/release/call ordering, which is one more read.
   The seductive part is that the consequence is what makes the fact matter,
   so it feels like part of the same finding. It is not. When a repair's
   whole value rests on a step you have not read, read it or do not write
   it.
3. A reviewer's summary phrase is not evidence. "Accurate for their own call
   graphs" was borrowed from a review report and promoted into a normative
   document without asking what had actually been checked, and it turned out
   to name the very test that was invalid. When a review's wording becomes a
   claim, ask the reviewer what they did, not what they called it. Asking
   cost one message and produced a real transitive walk.
4. Scope a trace to what was traced. One helper's acquire/release ordering
   supports a claim about that helper, not about "callback entry". Round
   three's fix was entirely a matter of putting the subject of the sentence
   back where the evidence is.
5. When two builds are in scope, a fact read from one is a fact about one.
   The power/chirp shape, the SCE flag offset, and the mapped-call-site
   count all differ between SP4 and NUSB. The project's trust order says the
   binaries outrank ReactOS; it does not cover the binaries disagreeing with
   each other, and they do. Read both before writing "the binary".
6. Separate what a binary shows from what the design concludes. A
   disassembly of the other side can establish a lifecycle or a contract; it
   can never establish what this driver's callback body should contain.
   Write the two as separate sentences, and label the second a decision.
7. Every defect here has one root: stating a conclusion at a confidence the
   evidence in hand does not carry. The locking claim alone survived three
   rounds by changing shape each time (an unfounded negative, then an
   unfounded positive, then an over-general true statement), and a fourth
   round found the separate target-scope defect, a single-target reading
   presented as target-neutral. A fifth then found that repair had
   over-corrected in turn: "chirp does not imply Version >= 200" was false,
   because NUSB withholds the slot at registration instead of testing at the
   call site. None was caught by the stop-time review gate, which classified
   the turns as documentation-only because they changed no code. A turn
   whose entire deliverable is a claim about a binary needs a reader who
   re-reads the binary; the gate's code/not-code test does not select for
   that.

### Affected documentation

`docs/usb-xhci-info/usbport-miniport-abi.md` section 4 root-hub block and
callback rows (corrected), section 9 item 9; `docs/contributing/roadmap.md`
Phase 5 task 1; `tools/{win2ksp4,nusb}-extracted/usbport-roothub-disasm.txt`
correction note.

## The 2d SMP VM boots fine on QEMU 11.0.0, so the previous entry's blocker is the host and not the version; and the plug/unplug clause was witnessing only the plugs

### Environment and operation

Phase 4 checkpoint, 2d SMP leg, host `fw-w11p-ykm` (the second development
machine, seen before), scoop QEMU 11.0.0 `qemu-system-x86_64.exe`, build
string `v11.0.0-12122-ga4bb4b10c9`. Same `vm\win2k-smp.img` as ever; the
working directory is OneDrive-synced, so this is the identical image file
the previous host could not boot. Debug build `xhci98.sys` 36,699 B (`built
Jul 30 2026 22:49:30`), byte-identical to the one 2a and 2b passed on, taken
from `out\pkg-debug\` rather than rebuilt, so the SMP leg tests the same
binary rather than a same-day rebuild. Trace archived as
`vm\win2k-smp-debugcon.phase4-checkpoint.log` (605 lines, four `DriverEntry`
cycles).

Launchers needed no regeneration here for the first time in four hosts: this
host's scoop path is what the committed `.cmd` files already carry.

### Finding 1: the 11.0.0 attribution is falsified

This file's Phase 4 checkpoint entry recorded that `win2k-smp.img` wedged in
boot driver init after exactly 12,691 IDE reads on `minis-w11p-ykm`,
identically under every accelerator, CPU count, snapshot and HPET
combination, and named QEMU 11.0.0 versus 11.0.50 the leading candidate. The
reasoning was that 11.0.0's vector-`0xD1` APIC storm is dodgeable only by the
Standard-PC HAL, which an SMP guest cannot use.

That reasoning does not survive the A/B it asked for. The same image, on the
same QEMU version (scoop 11.0.0) on a different host, sailed past that
stopping point: 13,304 reads at the first sample, 17,199 at the second, and a
Windows 2000 desktop with the `Computer` node reading ACPI Multiprocessor PC.
So the wedge is a property of `minis-w11p-ykm`, not of QEMU 11.0.0, and the
11.0.0-vs-11.0.50 A/B that was set up as the discriminating test would have
answered the wrong question. What is still unknown is what is different
about that host; nothing here narrows it, and the vector-`0xD1` storm stays
unresolved with one fewer candidate explanation.

The rule: when one host cannot do something, vary the host before varying
the software version it happens to carry. A single-host observation names a
configuration, not a component, and "the other host had a different version
of X" is the cheapest-looking hypothesis because the version number is the
one difference that is written down.

Also measured here, for the per-host WHPX record: plain `-accel whpx`
creates a partition on this host (the checkpointed `kernel-irqchip=off` rung
was still used, unchanged). That is a fourth distinct WHPX outcome across
four hosts.

### Finding 2: the checkpoint's plug/unplug clause never witnessed an unplug

The Phase 4 checkpoint names "the debug DPC log shows Port Status Change
events when a QEMU USB device is plugged/unplugged". Measured on the 2d VM
with five plug/unplug cycles:

| action | trace line | counters |
|---|---|---|
| plug port 5 | `event: port status change on port=00000005` | isr/claimed/dpc/events -> 2 |
| unplug port 5 | (silent) | -> 3 |
| plug port 6 | `event: port status change on port=00000006` | -> 4 |
| unplug port 6 | (silent) | -> 5 |

The site is `XHCI_DBG_VALUE_CHANGED` in `src/xhci_evt.c`, and the value it
watches is the port ID. An unplug always names the same port its plug did,
so the second edge is suppressed as "unchanged" every time. The archived 2b
checkpoint trace has the identical shape (ports 5, 6 and 7 each printed
exactly once, on their plug), which means the "and unplug" half of that
clause was never positively observed on any target: it was carried by the
ISR/DPC/event counters advancing.

This is the same defect as the No Op witness two entries below, at a second
site that the sweep did not reach, and it went unnoticed on 2a/2b for the
same reason it did there: a counter moving next to a silent site reads as
corroboration instead of as the only evidence present. The events themselves
are healthy: every edge produced an ISR, a claim, a DPC and a consumed
event.

The rule (a restatement, because restating it is what was missing): after
fixing a change-gated witness, `grep` for the macro rather than fixing the
site in hand. And when a checkpoint clause says A and B, check that A and B
have separate observable consequences. Here they share one, and the shared
one is the weaker.

The fix is task 7's shape: a bounded site that prints per occurrence rather
than per distinct value, plus a Port-Status-Change counter in the counter
block so a release build carries the clause too. It was not applied in this
session, so that the SMP leg ran the same binary as 2a/2b; it owes one
re-observation boot per VM once made.

### Finding 3: the 2d guest's keyboard layout is US-Dvorak

Driving the Found New Hardware wizard by `sendkey` produced garbage in the
path field: `E:\pkg` came out as `.S\lt`-shaped text, and a diagnostic
`a`..`m` came back as `axje.uidchtnm`, which is US-Dvorak exactly.
Non-character keys (`ret`, `tab`, `spc`, `backspace`, arrows, `alt-`) are
unaffected, so wizard navigation works and only typed text is scrambled.
`E:\pkg` is `d, shift-z, backslash, r, v, u`.

Whether the layout was chosen at install or set by accident in an earlier
session is unknown and does not matter much; the operational point is that a
scrambled `sendkey` string is a guest keyboard-layout symptom, not a QEMU
key-name error. Diagnose it by sending `a`..`m` into any text field and
reading the result, which identifies the layout in one shot. Fix inside the
guest at Control Panel -> Keyboard -> Input Locales -> Properties -> `US`.

### What the run observed

Four `DriverEntry` cycles in one trace: install boot, disable, enable,
shutdown, Driver Verifier boot, disable, enable, shutdown. Full init every
time (handoff, port map built twice and agreeing, `MaxSlotsEn=32`, four
managed USB2 ports powered, four USB3 left unpowered, R/S), the No Op
self-test completing with a matching TRB pointer on every start (`01D77000`,
`01D75000`, `01F81000`, `01DA0000`; the common buffer moves on every bind,
as Phase 3 task 9 found), `isr count` = `isr claimed` = `dpc count` = `events
consumed` at every sample, and every failure, escalation,
host-controller-event-reset, watchdog and fatal counter zero across all 605
lines.

Seventeen distinct callbacks were exercised at their expected IRQLs.
Both teardown shapes appeared and were correctly distinguished: the disable
path reached the port pass (`teardown: ports unpowered=00000008`, `0`
refusing) and the shutdown path recorded the skip on a suspended controller.

`scripts\check-smp-parallelism.ps1` exits 0 against the live VM: two vCPUs
with distinct `thread_id`s (18616, 17656) and process affinity `0xFFFF`, 16
of 16 logical processors allowed.

What is not observed: Driver Verifier's own Driver Status page was never
read back. The Verifier options were configured through the GUI by the
operator and the boot that follows them is in the trace, but "Verifier was
verifying `xhci98.sys`" is at this point reported, not witnessed, which is
the distinction finding 2 and the Phase 4 checkpoint entry are about, so it
is recorded as owed rather than assumed. The 2b Verifier clause taken there
is unaffected and was read back there.

## Second run on the same host - the fixed witnesses work, and hot-plug operations are not hot-plug events - which is most of the SMP VM's contention coverage

### Environment and operation

Same host and VM as the entry above, rebuilt debug driver `xhci98.sys`
36,987 B (`built Jul 31 2026 22:56:54`) carrying both the PSC witness fix
from finding 2 above and the No Op witness rework that had never run on a
VM. Installed by disabling the device, overwriting
`C:\WINNT\system32\drivers\xhci98.sys` from `E:\pkg`, and re-enabling.
Traces archived as `vm\win2k-smp-debugcon.phase4-newwitness.log` (342 lines)
and `vm\win2k-smp-qemu-trace.phase4-newwitness.log` (the host-side oracle
below).

### Both fixed witnesses do what they were supposed to

`No Op self-test completion matched TRB=01DA1000` / `completion
code=00000001` printed on the new binary's first start, the first time the
reworked token-bounded witness has run anywhere outside the host suite.

The PSC site now prints both edges: `event: port status change on
port=00000005` for the plug and for the unplug, with `port status change
events=00000001` then `=00000002` beside them. Over the whole run the bounded
site printed exactly 32 lines, its budget, while the counter carried on to
73. That is the intended division of labour observed working: the per-edge
line is the readable witness, the counter is what survives the budget, and
neither alone would have done.

### The finding: 24 hot-plug operations produced 6 events

A storm of `device_add`/`device_del` pairs was run to give the SMP VM the
contended ISR/DPC load it exists for. The driver's counters did not move
anywhere near the operation count, and the first instinct, that the driver
was missing events, was wrong. Two measurements settled it:

- Every `device_add`/`device_del` was accepted silently by the monitor (40
  of 40 in the instrumented burst), so no operation failed.
- QEMU's own xHCI trace, enabled at runtime with HMP `trace-event
  usb_xhci_port_notify on` / `trace-event usb_xhci_queue_event on`, shows
  the controller generated 6 `port_notify` and queued 6
  `ER_PORT_STATUS_CHANGE` events for 24 operations.

The driver consumed all six, and the two sides reconcile exactly: QEMU's
last queued event is `idx 73`, the 74th, and the driver reports `events
consumed=0000004A` = 74. Zero loss, `isr count` = `isr claimed` = `dpc count`
= 14 (one DPC drains many events under load), every failure counter zero.

Why the controller goes quiet: a Port Status Change Event follows a change
bit transitioning, and `docs/usb-xhci-info/xhci-data-structures.md` says what
to do on receipt: "read that port's PORTSC, update the shadow, clear the
change bits". Phase 4 does none of that; the port shadow and the acknowledge
are Phase 5 work, and `xhciHandleEvent`'s own comment says so. So each port
raises one event and then stays silent, because its change bit is still set
from the event nobody acknowledged. The trace shows that shape: `bits
0x20000` (CSC) on ports 6, 7 and 8, and nothing at all from port 5, which had
been latched by an earlier plug/unplug pair.

The consequence is about the SMP VM, not about the driver. Phase 2d exists
to put real cross-CPU pressure on the ISR/DPC path, and the roadmap says to
exercise every checkpoint's build here from Phase 4 onward. This run shows
that in Phase 4 that pressure cannot be generated by hot-plugging, however
fast: the event rate is bounded by un-acknowledged change bits, not by the
operation rate, so 24 operations buy 6 events and 40 buy 12. 2d's race
coverage therefore stays thin until Phase 5 acknowledges change bits (which
restores one event per real edge) and Phase 6 adds transfer traffic. Saying
"the SMP VM passed with a plug/unplug storm" would overstate what was
exercised.

The reusable rules: (1) an operation you issued is not an event the hardware
raised; count the events at the source before reading a load test as a load
test; (2) HMP `trace-event <name> on` turns QEMU's trace points on without
restarting, and `usb_xhci_queue_event`'s running `idx` is an independent
oracle for "did the driver consume everything the controller produced",
which no amount of driver-side counting can be; (3) when a driver counter is
lower than expected, get the producer's number before theorising about the
consumer.

Driver Verifier's Driver Status page was opened this time and `xhci98.sys`
reported still loaded/verified, closing the read-back the entry above
recorded as owed. Verifier stays configured across boots, so the new binary
ran under it.

### 2a Win98 SE: the resume case, and 29 of 29

Traces `vm\win98-debugcon.phase4-newwitness.log` and
`vm\win98-qemu-trace.phase4-newwitness.log`, with the before picture kept as
`vm\win98-debugcon.oldwitness-resume-gap.log`, in which the first start
prints `command: completion matched outstanding TRB=0FF01000` and the
resume's `No Op command issued at TRB=0FF01000` is followed by no match line
at all. That is the defect, archived.

On the fixed build the witness printed on 29 of 29 self-tests. The number is
that large because of a behaviour the record understated: Win98 does not
idle-suspend once, it cycles. An idle guest suspended and fully
reinitialised this controller 29 times in a few minutes (handoff, port map
built twice, port power, `HCRST`, fresh No Op each time), then parked
suspended until something touched the controller again. Under the old
witness this run would have produced one match line and 28 silences. Phase
5/6 inherit the constraint: initialization has to stay cheap and idempotent,
because Win98 runs it constantly.

A counting trap on the way: the `SuspendController`/`ResumeController`
callback lines stop at 4 per site (`XHCI_DBG_CALL_LIMIT`), so reading them
as totals gave "8 cycles" when there were 29. A trace site's budget is not a
count. Use a counter, or a site that is unbounded by design, which is what
the self-test witness is, and why it kept printing after the callback sites
went quiet.

Win98's plug/unplug window is sub-second. A trace-driven pair (fire on `init
complete`) with a 1.2 s gap caught only the plug: the driver had already
re-suspended, and the oracle showed the unplug's `port_notify port 6, bits
0x20000` with no `queue_event` behind it, since a halted xHC latches the
change bit and queues nothing. At a 0.25 s gap both edges landed, on the
same port (7), both printed. One honest nuance: a `SuspendController` and a
fresh `init complete` sit between the two lines, so the second edge surfaced
after the next resume rather than at the instant of the unplug; what is
proven is that two events naming one port both print, which is what the
change-gated witness suppressed.

### 2b Win2000 SP4: the easy one, as predicted

New binary in by disable -> overwrite -> enable (no reboot; the enable
reloads the image). Witness fired on the start, and a plain plug/unplug pair
produced both edges on port 5 with `port status change events` going 1 -> 2,
no timing work at all, because native Win2000 usbport never idle-suspends.
When a clause is hard to observe on one target, check whether another target
makes it cheap before engineering around the hard one.

### Status, and one more premature claim caught at the door

All three VMs have now run `xhci98.sys` 36,987 B with both reworked
witnesses observed, every failure/escalation/fatal/watchdog counter zero
throughout. Traces: `vm\win98-debugcon.phase4-newwitness.log`,
`vm\win2k-debugcon.phase4-newwitness.log`,
`vm\win2k-smp-debugcon.phase4-newwitness.log`.

The checkpoint was written up as MET while 2b was still running, with its
stop/teardown on that build not yet in any trace; the write-up even asked
for the shutdown in the same breath. A stop-time review caught it. The guest
was shut down shortly after and the teardown is now archived (`Suspend` ->
`DisableInterrupts` -> `Stop`, suspended-controller skip), so the conclusion
stands; the claim preceded the evidence by several minutes, which is the
identical error this entry and the one above are about, made while writing
them up. Being able to describe a trap is not the same as being immune to
it, and the moment of greatest risk is the summary, where a pending
observation reads as a formality.

### The one Phase 4 clause that stays open, and why it is not a HAL limitation

`ResumeController` has never run on Windows 2000. Win98 exercises it
constantly (29 full reinitialisations in one idle run), so the path is well
covered on the target that forgives, and not at all on the target that
enforces. Task 8 had already flagged this, qualified as "where the VM and
HAL permit it", and the attempt to close it found the qualifier is wrong
about the mechanism:

- 2b forces the Standard-PC HAL (`-machine pc,acpi=off -cpu pentium3,-apic`)
  to dodge the Win2000-Setup APIC storm, so it has no ACPI sleep states at
  all. That one is a HAL limitation.
- 2d has ACPI sleep, and Standby fails with "System Standby Failed ... the
  device driver for the 'Cirrus Logic 5446 Compatible Graphics Adapter'
  device is preventing the machine from entering standby". The obvious fix,
  swapping the display to the standard VGA driver, has no target: Win2000
  SP4's display-class list has no `(Standard display types)` manufacturer
  and nothing above `STB`. `scripts/setup-qemu-win2k-smp.ps1` exposes no
  `-Vga` parameter, and hand-editing a generated launcher is against the
  rule written in the launcher itself.

So the blocker is one emulated device, not the HAL and not the driver.
Recorded rather than worked around, because "the VM does not permit it" and
"the graphics card we chose vetoes it" send a future reader to different
places; the second one says the fix is a `-vga std` VM, which Phase 5 wants
anyway. Worth generalising: when a capability is refused, get the refusal
text before writing down the cause. Here it named the responsible device
outright and overturned the plausible explanation that had been sitting in
the roadmap since task 8.

The status line in `docs/contributing/roadmap.md` also spells out that the
evidence is split across two builds: the full lifecycle matrix (including
disable/enable and restart) on the earlier binary, and
start/stop/plug-unplug/witness/Verifier on the later one. A bare "MET"
implies the whole matrix was re-run on the later binary, and it was not.

## The Phase 4 checkpoint passes on both target VMs, and the SMP VM will not boot on QEMU 11.0.0 because the APIC HAL is the one thing 2b is allowed to dodge

### Environment and operation

Phase 4 checkpoint runs, host `minis-w11p-ykm` (a fourth development
machine), scoop QEMU 11.0.0 `qemu-system-x86_64.exe`. Debug build
`xhci98.sys` 36,699 B (`built Jul 30 2026 22:49:30`), staged with
`scripts\package\make-package.ps1` and installed from the VVFAT package
directory. Traces archived as `vm\win98-debugcon.phase4-checkpoint.log`,
`vm\win2k-debugcon.phase4-preverifier.log` and
`vm\win2k-debugcon.phase4-checkpoint.log`.

The per-host launcher re-generation rule held for the fourth time: all three
committed launchers hard-coded `C:\Program Files\qemu`, which does not exist
here, so nothing would start until `setup-qemu*.ps1` were re-run. That path
is itself the fingerprint of the installer QEMU build the 2b/2d checkpoints
used; see the SMP finding below.

### What was observed (2a Win98 SE, 2b Win2000 SP4)

Both targets pass every clause of the Phase 4 checkpoint. The full init
sequence runs on both: BIOS handoff (`no legacy capability` on `qemu-xhci`),
the port map built twice and agreeing field-for-field, `MaxSlotsEn=32`, four
managed USB2 ports powered, four USB3 ports left unpowered by design, R/S
set, and the No Op self-test completing. The interrupt path works on both:
`isr count` = `isr claimed` = `dpc count` = `events consumed` at every
sample, and every failure/escalation counter stayed at zero in every run.

Two things were observed here that no host model could produce:

1. Task 6's Event-Handler-Busy hazard is universal, not device-dependent.
   Every single start on both targets logged `EnableInterrupts: the
   controller was already holding events, pending=00000001`. The No Op
   self-test's own completion is always sitting on the event ring with
   `IP`/`EINT`/`EHB` set by the time usbport calls `EnableInterrupts`, so the
   deadlock task 6 predicted would have fired on every boot of every
   machine. Releasing EHB at the unmoved dequeue pointer before either
   enable is what makes the DPC reachable at all.
2. usbport idle-suspends the Win98 controller ~0.5 s after start, with a
   device connected, because nothing enumerates it yet, and a halted
   controller raises no Port Status Change Events. A timer-driven plug test
   silently observes nothing; the sequence has to be driven off the trace
   (`init complete` -> plug within the same second). Win2000's native
   usbport does not idle-suspend at all, so the same test needs no timing
   care there. Expect this window to widen once Phase 6 makes a device
   enumerable.

The Win98 run also caught a full `ResumeController` -> complete
reinitialisation -> No Op -> `init complete` cycle, and the shutdown
teardown `Suspend` -> `DisableInterrupts` -> `Stop`, where the stop
correctly reported the skip (`the stop arrived on a suspended controller -
PORTSC is unwritable`) that task 8's review added for this case. On Win2000
the same teardown reached the port pass proper: `teardown: ports
unpowered=00000008`, `0` refusing.

### The witness added for this run was itself blind to a repeat

`command: completion matched outstanding TRB` was added mid-run because the
checkpoint names the pointer match and only its absence was observable.
Review after the run caught that it used `XHCI_DBG_VALUE_CHANGED`, which
prints only when the value changes: there is one No Op per start and its
address is the same every time, so the line witnesses the first start of an
image's lifetime and no later one. On Win2000 that was invisible, because
disable/enable reloads the image and resets the per-site statics; on Win98
the image stays loaded, and the resume reinitialisation this run captured
reported nothing, which was visible in the archived trace and read past at
the time as "covered by the event counter". It is not: a witness a repeat
silences cannot tell "matched again" from "never completed".

Fixed by giving the self-test its own unbounded site (`No Op self-test
completion matched TRB` / `... completion code`); the generic completion site
keeps the change-gated macro, which is right for the many commands Phase 6
will issue. The rule: `XHCI_DBG_VALUE_CHANGED` is for watching a value move;
a per-start checkpoint witness needs a site that is once-per-start, or it
silently documents only the first start. Neither existing macro fits a
"print every time, but only from a once-per-start site" need by accident.
Pick the site, then the macro.

The first fix's "once per start" was asserted, not constructed. It gated on
`pointer == ext->NoOpTrbPA` alone, which is a self-test identity only while
the No Op is the only command that ever runs, i.e. only in Phase 4. The
command ring is reused: once Phase 6 issues real commands the ring wraps, a
later command occupies the No Op's TRB address, and an unbounded site fires
on ordinary traffic while labelling it the self-test. A physical address is
not an identity on a ring; it is a position that comes back around.

The second fix then re-broke the case the whole change existed for. Gating
on `CommandsCompleted == 0` and the address made the site once per start,
but a resume is not a start. usbport zeroes the miniport extension before
`StartController` only; `ResumeController` reinitialises the controller and
issues its own self-test while inheriting every counter, so the gate was
false exactly when Win98 resumed, which is the silence the first round set
out to remove. Two rounds of fixes, and the originally-reported symptom was
back.

The property wanted is neither a value nor a counter state but one line per
self-test issuance, so it is now a one-shot token in the extension
(`NoOpWitnessArmed`): armed by a successful submit and consumed by the
matching completion. Start and resume both print; Phase 6 traffic never
does.

Round four was the token outliving its command. It was cleared in the
abandon path, the path being looked at when the token was written, and not
in the two abort paths, so a self-test that timed out and was successfully
recovered (Command Aborted, or Command Ring Stopped when the abort found the
ring between commands) left the token armed for a later command to consume.
The fix is not a fourth clear: `xhciCommandEndOutstanding` is now the single
place an outstanding command stops being outstanding, and all five paths
(completion, Command Aborted, Command Ring Stopped, abandon, engine init) go
through it, so the address and the token cannot disagree. This repeats task
6's rule: give a property several functions share one mechanism, rather
than adding it where you happen to be looking.

Round five: none of the four rounds had a test. The witness's policy lived
inside a trace macro, and the host suite compiles the release path where
those macros are empty, so the policy was untestable by construction, which
is why three wrong bounds in a row survived review. The decision is now
ordinary code in both flavours (`xhciCommandWitnessSelfTest`) with a counter
beside it (`NoOpWitnessFired`), which also gives a release build a positive
reading of the checkpoint's pointer-match clause instead of only a
debug-build trace line. `test_selftest_witness_token` pins the invariant
across every path that ends a command, and the three testable defects are
mutation-checked: an address-only gate fails 2 checks, a per-start counter
fails 1, and a token that outlives its command fails 3.

Two traps met while writing that test, both worth keeping:

- The obvious version of the ring-reuse test is blind. Posting a stale event
  naming the self-test's address proves nothing: the completion path rejects
  an address that is not outstanding long before the witness sees it, so the
  mutation passed. The hazard needs a live command on that position, which
  means driving the command ring all the way round and issuing an ordinary
  command from the self-test's TRB. Ask what the guard above the code under
  test already rejects before believing a negative result.
- Do not revert a mutation with `git checkout <file>` when the file carries
  uncommitted work. It reverts to the last commit and silently takes the fix
  with it. Two of the four mutation runs measured a tree with no witness
  function in it at all and produced numbers that looked like results. Copy
  the file aside and copy it back; verify the restored baseline scores zero.

Note the shape of the three rounds: round one removed the symptom (silence
on repeat) and kept a flawed premise (that an address identifies the
self-test); round two fixed that premise and silently reintroduced the
symptom on the resume path, because it reasoned from "usbport zeroes the
extension per start" without asking which callbacks are starts. Same pattern
this phase hit in tasks 5 and 7: after fixing a diagnostic, re-derive the
property it is supposed to have, and check it against every path that
reaches the site, not just the one that failed.

The cheap check that would have caught round two: the self-test has exactly one call site, in
`XhciInitController`, which is reached from both `StartController` (with
resources) and `XhciResumeController` (with NULL), so any gate written in
terms of "a start" had to be checked against the resume arm, which zeroes
nothing.

### The SMP VM (2d) does not boot on this host, and it is not the driver

Not installed there, so nothing of ours ran. `win2k-smp.img` wedges during
boot driver init at the "Starting up..." progress bar, after reading exactly
6,497,792 bytes / 12,691 IDE read operations, byte-identical across every
configuration tried, with `idle_time_ns` then climbing for minutes with no
further disk access:

| Configuration | Result |
|---|---|
| `whpx,kernel-irqchip=off`, `-smp 2` (the 2d checkpointed rung) | wedged at 12,691 reads |
| `tcg,thread=multi`, `-smp 2` | wedged |
| `whpx,kernel-irqchip=off`, `-smp 1`, reverted to `phase2d-clean` | wedged, same counts |
| `-machine pc,hpet=off`, `-smp 2` | wedged, same counts |

So it is not the accelerator, not the second CPU, not dirty image state, and
not the HPET the 2d notes had left as an oddity. `qemu-img check` reports no
errors. The UP 2b image boots fine on this host the same evening.

The variable is QEMU, and the mechanism is already in this file. 2b and 2d
were checkpointed on QEMU 11.0.50, from the installer at `C:\Program
Files\qemu`; scoop's bucket tops out at 11.0.0 (`scoop update qemu` is a
no-op here). The Standard-PC HAL entry below records that on scoop QEMU
11.0.0 a Win2000 guest that selects an APIC HAL drowns in the vector-`0xD1`
local-APIC clock ISR, and that the fix for 2b was to remove the local APIC
and the ACPI tables so Setup picks the Standard-PC HAL. That is the reason
the 2b launcher still carries `-machine pc,acpi=off -cpu pentium3,-apic`. 2d cannot
use that fix: an SMP guest needs the APIC, so the one workaround that makes
Win2000 usable on 11.0.0 is unavailable to the very VM whose purpose is the
second CPU.

Proven: the wedge, its invariance across the four configurations, the QEMU
version split between this host and the checkpoint host, and that 2b's
APIC-avoiding flags are an intentional workaround for a documented 11.0.0
storm. Inferred, not confirmed: that this particular wedge is that storm.
The EIP sampling that would confirm it (a tight interlocked loop in a
~140-byte ntoskrnl window entered from the vector-`0xD1` prologue) was
started and not finished; two TCG samples sat at `0x8046566a`, consistent
with a tight kernel loop but not localised to the storm's window. The entry
above ("The 2d SMP VM boots fine on QEMU 11.0.0") later falsified the
version attribution: the same image booted on the same QEMU version on a
different host.

Consequence for the 2d record: the storm's cause is filed there as
unresolved, with a note that a same-image A/B could not explain it. The
earlier A/B varied the accelerator on one host with one QEMU, and could not
vary the QEMU build.

### The reusable rules

- Record the QEMU build with every VM checkpoint, not just the host. Two
  QEMU installs on one machine (scoop and installer) is the normal state
  here, the launchers pick one by hard-coded path, and `scoop update` cannot
  reach the installer's version. A VM that "worked last week" may only have
  worked on the other binary.
- A workaround that a VM is allowed to use is part of that VM's identity.
  2b's `acpi=off -cpu ...,-apic` is not cosmetic drift from 2d; it is the
  reason 2b is immune to a bug 2d must face, and it is why "2b boots here"
  proves nothing about 2d.
- `info blockstats` distinguishes wedged from slow in one command, and a
  byte-identical read count across configurations is strong evidence that
  the guest stops at a fixed point rather than failing randomly.

### Affected

`vm\win98-debugcon.phase4-checkpoint.log`,
`vm\win2k-debugcon.phase4-checkpoint.log`, `docs/contributing/roadmap.md`
Phase 4 checkpoint, the Standard-PC HAL and Phase 2d WHPX entries in this
file.

## Guard every operand of a reserved-preserving RMW, and lock the whole shared state word

### Environment and operation

Static review of Phase 4 lifecycle and interrupt admission, followed by the
host controller model. No target VM or real hardware was used.

### Observation

The earlier dead-window repair covered `XhciMaskInterrupts` only when
`USBCMD` read all ones. The inverse path still fed an all-ones `USBCMD` into
the defined-bit write and could assert R/S, HCRST, LHCRST, CSS and CRS
together; both paths could also derive an `IMAN` write from an independently
all-ones read. Separately, locking `INITIALIZED` and callback updates did
not protect the shared `Flags` word while quiesce cleared `RUNNING` or
suspend changed `SUSPENDED`. A locked `INTERRUPTS` update could therefore be
overwritten by an unlocked RMW based on an older word, leaving resume with
no request to restore.

The host model now covers independently dead `USBCMD` and `IMAN` reads,
refuses both writes if either operand is unavailable, and observes the
normal and both fallback `RUNNING` clears plus suspend/resume transitions at
controller-lock boundaries. The suite passes 4,429 checks. This proves the
software paths and lock placement in the single-threaded model; a true
cross-CPU interleaving remains target-only evidence for the Phase 2d SMP
run.

Reusable rules: pre-read and validate every operand before the first write
of a multi-register reserved-preserving transition. The second half of this
rule as first written, "so failure cannot leave a half-transition", was
wrong and is corrected in the next entry: validating every operand is right,
but refusing every write when one operand fails is only safe in the
direction where doing nothing is safe. Treat a shared bitmask as one
synchronization object: locking only the bit whose meaning motivated the
change does not protect it from an unrelated RMW of the same machine word.
Count a mask that cannot be applied; the no-write fallback may be the only
surviving explanation for a later live interrupt line.

Normative detail: `docs/contributing/implementation-invariants.md`, "Command
Ring".

## "Refuse the whole transition" is a direction-dependent rule, not a symmetry

### Environment and operation

Review of the repair above. Host model only; no target VM or real hardware.

### Observation

The repair made `XhciMaskInterrupts` and `XhciUnmaskInterrupts` symmetric:
both refuse both writes if either operand reads all ones. Symmetry was the
wrong instinct, and it reintroduced a failure this project had already paid
for once.

A refused unmask leaves the interrupt off: safe, and usbport enables again
on the next resume. A refused mask leaves the enable up. Since
`xhciResetController` publishes `ControllerFailed` on the statement after
the mask, the ISR then declines a still-asserted shared level-triggered INTx
without acknowledging it: the UP Win98 livelock the fifth review round fixed
by ordering mask before publication. Reaching it again through the refusal
path took one plausible-looking symmetry.

Two smaller things surfaced with it. The guard validated a `USBCMD` read
that was not the read supplying RsvdP (the write re-read the register), so a
window dying in that gap wrote reserved bits as all ones. And the
shared-flag helper introduced in the same repair was public with no
NULL/signature guard, unlike its neighbour.

The second one was untestable in the host model as it stood, which is the
reusable part: a mutation that re-read instead of using the validated value
failed zero checks, because the model's dead window was a level, not an
edge. Adding `mmioReadsBeforeDead` (answer N reads, then stop decoding)
turned it into a 1-check discriminator that observes exactly `0xFFFE1070`.
When a guard cannot be distinguished from its absence, the model is often
the thing missing a capability, not the guard the thing that is dead.

Reusable rules: when a safety guard refuses to act, ask what refusing leaves
behind, per direction, and write the answer down at both sites; a mask and
an unmask are not mirror images. Validate every operand, and then write from
the value that was validated, not from a fresh read of the same register. A
helper that acquires a lock needs its header to say so, because the callers
that must not use it are the ones already holding it.

Normative detail: `docs/contributing/implementation-invariants.md`,
"Interrupt Ordering".

## A guard that declines to act still has to say so to whoever acts next

### Environment and operation

Review of the repair above. Host model only; no target VM or real hardware.

### Observation

Both repairs so far had fixed the write and left the consequence of not
writing unreported, and each time the consequence landed somewhere else.

`XhciMaskInterrupts` had been taught to write whichever half it could
derive, which fixed the case where one operand was readable. When both read
all ones it still wrote nothing, and quiesce cleared `INITIALIZED` and
`ResetController` published `ControllerFailed` anyway, so the ISR declined
at DIRQL without acknowledging. That is the same shared-INTx livelock the
fifth round fixed by ordering mask before publication, arriving through the
gate instead of through the ordering. Masking before publishing is necessary
and not sufficient; what the gate needs is whether the mask applied.

Fixing it forced apart two questions one flag had been answering.
`INITIALIZED` meant both "HcInfo holds real register bases, so an accessor
is safe" and "this driver is admitted". Only the second may ever be relaxed
(a controller whose window was never decoded cannot have its interrupt
acknowledged either), so the first moved to `HcInfoStatus`, which quiesce
does not clear.

The unmask had the mirror problem: it refused, counted, and returned void
into a callback that also returns void. usbport calls `EnableInterrupts`
once after `StartController` and not after a successful resume, so nothing
was ever coming back for it, and one transient all-ones read could leave a
started controller that never interrupts again. The justification written
into that comment, "the next resume re-enables", was an assumption
contradicted by this project's own transcription of the ABI, sitting two
files away.

Reusable rules: when a guard declines to act, name who acts next and confirm
they will. A refusal that returns void into a caller that returns void is
not handled, it is lost. Check a claimed recovery path against the ABI notes
rather than against plausibility; this one was wrong in a comment written in
the same session that transcribed the contradicting fact. And when one flag
is read by two callers asking different questions, a change that is right
for one of them is the moment it has to be split.

Scope worth recording so it is not overstated: none of this rescues a
permanently dead window. The ISR could not acknowledge such a controller
either, and its all-ones `USBSTS` read declines the claim. The transient is
the whole target, which is also why the model needed `mmioDeadReads`;
`mmioDead` can only express the permanent case, in which every recovery path
is untestable by construction and therefore looks like dead code.

Normative detail: `docs/contributing/implementation-invariants.md`,
"Interrupt Ordering" (the decline-gate and escalation bullets).

## A model that cannot fail an operation cannot test the proof of it

### Environment and operation

Review of the repair above. Host model only; no target VM or real hardware.

### Observation

`EnablesProvablyDown` was derived from the two reads taken before the mask's
writes. Those prove the operands were derivable and nothing else: a window
that stops decoding in between swallows both writes silently, and the field
then published "the enables are down" about a controller whose enables were
exactly as up as before. Quiesce or `ResetController` then closed the ISR's
gate on an asserted INTx, the livelock the field had been added to prevent,
one level further in. This is the third round in a row where the write was
fixed and the proof about the write was taken from the wrong event.

The reason no test caught it is the part worth keeping.
`XhciHostWriteRegister` applied writes to the model's register file even
while `mmioDead` was set, so the model could express "reads fail" but never
"writes are lost", and every "did this take effect" proof in the suite was
therefore vacuously true. A model that cannot make an operation fail cannot
test the code that checks whether it succeeded. The read half had been
modelled first because reads are where all-ones is visible; the write half
is where the consequence is.

The read-back check has an asymmetry worth stating, because it is the
inverse of the one recorded in the previous entry. For a mask, an all-ones
read back has both enable bits set and so reads as "no proof" for free. For
an unmask, all-ones is indistinguishable from the success being looked for,
and has to be rejected explicitly. Same sentinel, opposite meanings, decided
by which direction the check is looking.

Separately, the escalation added last round had been described in two
places as producing a stop/start. It does not:
`UsbPortInvalidateController(RESET)` queues a DPC that calls the miniport's
own `ResetController` at DISPATCH inside a usbport lock, usbport does
nothing afterwards, and that callback marks the controller terminally
failed. The escalation is containment, a silently non-interrupting
controller traded for a visibly failed one, and describing it as recovery
was the second time in two rounds a usbport behaviour was asserted from
plausibility while this repository's own disassembly notes said otherwise.

Reusable rules: prove an MMIO transition from a read back, never from the
accessor returning or from the operands that fed it. Before trusting any
"did it apply" check, confirm the model can make the underlying operation
fail; otherwise the check and its absence are the same program. And when a
comment asserts what another driver does, cite the extract; twice now the
plausible version has been the wrong one.

Normative detail: `docs/contributing/implementation-invariants.md`,
"Interrupt Ordering" (escalation bullet, now stating containment).

## Publishing a proof is not the same as any caller reading it

### Environment and operation

Review of the repair above. Host model only; no target VM or real hardware.

### Observation

The previous round made the mask publish whether it had actually suppressed
delivery. Three of the four callers were then safe by construction, because
an unproven mask leaves `XhciIsr` admitted and the ISR can still
acknowledge. `XhciDisableInterrupts` was the exception and nothing checked
it: usbport's ISR wrapper only calls the miniport while its own
interrupt-enabled flags are set, and that callback is what clears them. So
on exactly one path, publishing the proof to a reader that did not exist
left the failure it described unhandled. With `INTE` and `IE` still up, an
asserted level-triggered INTx loses its last route to an acknowledgement.

The lesson is narrower than "check return values". Adding a state word
creates an obligation at every consumer, and the consumer that matters is
usually the one whose situation differs from the others; here, the only
caller that hands away the ability to acknowledge. Enumerate callers when
publishing a new proof, and say for each why it is safe; three of four being
safe by inheritance is the shape that hides the fourth.

Two smaller things arrived with it. The field was named
`EnablesProvablyDown` while the proof it carried had become "either enable
confirmed clear", true and sufficient, since either alone stops delivery,
but three contracts still said "both". A name is the cheapest place for a
proof to drift from its statement, and renaming it to
`InterruptDeliverySuppressed` is what stops the drift recurring. And
`InterruptMaskFailures` counted only an unreadable operand, so the
swallowed-write incident, the one the previous round had just made
detectable, left no trace at all once later lifecycle activity moved the
state word. A state field answers "now"; a counter answers "did this ever
happen", and an incident worth detecting is worth counting.

Reusable rules: when adding a proof, enumerate its consumers and justify
each, paying attention to the one whose failure mode differs. Keep the name
of a state word describing exactly what is proved, and re-check it whenever
the proof weakens or strengthens. Pair every new state field with a counter
if the condition it describes is transient, or diagnostics will only ever
see the last one.

Normative detail: `docs/contributing/implementation-invariants.md`,
"Interrupt Ordering".

A later correction to how the Win98 half of the checkpoint may be cited,
left as written rather than rewritten. The clause "start -> disable/enable
-> stop -> restart passed on both target VMs" cannot have been a Device
Manager controller disable on Win98: that bugchecks the guest, measured in
Phase 3 task 8 and reproduced on Microsoft's own `usbehci.sys`. The record
does not name the mechanism it actually used there, so read the Win98 half
as "a stop and a restart by some route", not as evidence that a disable
works. The observation is not second-guessed; only what it can be cited for.

## Phase 2d: the vector-0xD1 storm is absent under WHPX with `kernel-irqchip=off` - and it is not faster than TCG

### Environment and operation

Phase 2d, building the Windows 2000 SP4 multiprocessor stress VM from
scratch. Run on the third host of the multi-host set (the one carrying QEMU
outside scoop), QEMU 11.0.50 at `C:\Program Files\qemu`. Host hypervisor
present (`systeminfo`: "A hypervisor has been detected"). New disk
`vm\win2k-smp.img` (4G qcow2), monitor 55557, launchers from the new
`scripts/setup-qemu-win2k-smp.ps1`. Target config per roadmap Phase 2d task
1: `-machine pc` (ACPI on), `-cpu pentium3` (local APIC present), `-smp 2`,
`-m 512`, `-accel whpx`.

### Observation 1 - rung 0 never runs a single guest instruction

`-accel whpx` does not hang the guest. QEMU exits before the guest starts:

```
qemu-system-x86_64.exe: -accel whpx: WHPX: Failed to enable nested virtualization, hr=80370302
qemu-system-x86_64.exe: -accel whpx: failed to initialize whpx: Invalid argument
```

This is a partition-creation failure, not a guest problem, and it is nothing
to do with this VM's flags. Isolated across five invocations, each given
~4 s to live or die:

| Invocation | Result |
|---|---|
| `-M pc -accel whpx -display none -m 64` | init failure, same `hr` |
| ... `-smp 2` | init failure, same `hr` |
| ... `-cpu pentium3` | init failure, same `hr` |
| ... `-cpu pentium3 -smp 2` | init failure, same `hr` |
| ... `-accel whpx,kernel-irqchip=off -cpu pentium3 -smp 2` | runs |

So on this host WHPX is usable only with `kernel-irqchip=off`, which is
roadmap Phase 2d task 2's rung 1, reached for a completely different reason
than the ladder anticipated (the ladder expected to arrive there from a
Setup hang). `hr=80370302` is recorded raw rather than decoded: the
empirical rule is what matters and no WHP header is mirrored in this repo to
decode it against.

This is not the same failure as the Phase 2c 2b WHPX trial recorded below.
That one was an instant segfault caused by the `-apic` CPUID mask, and this
VM never masks the APIC off; without it no multiprocessor HAL can be
installed at all.

### Observation 2 - the storm did not reproduce; the phase's premise holds

With ACPI on, the local APIC present, `-smp 2`, and `-accel
whpx,kernel-irqchip=off`, Windows 2000 Setup walked straight past `Setup is
starting Windows 2000` to `Welcome to Setup` and installed normally. No EIP
sampling was needed; there was nothing to sample. This proves that an
APIC-present multiprocessor setup can pass under the working WHPX
configuration, so giving this guest an APIC is not sufficient by itself to
produce the storm. It does not establish which difference from the earlier
TCG run removed it.

The rung it had to run on narrows one part of that conclusion.
`kernel-irqchip=off` means QEMU emulates the APIC in userspace, as it does
under TCG, so an in-hypervisor irqchip is not a prerequisite for giving this
guest an APIC and its absence did not prevent this install from completing.
It does not isolate why the earlier TCG run stormed: the two observations
differ in host, QEMU build (11.0.0 versus 11.0.50), QEMU binary target
(`qemu-system-i386` in the Standard-PC HAL record versus
`qemu-system-x86_64` here), accelerator and CPU/timer-delivery path, and no
timer backlog was measured.

### Observation 2a - the controlled A/B covers installed boot, not the original Setup workload

The obvious inference from the above, that WHPX cured the storm by running
the guest fast enough to drain the timer queue, was written into these
documents at the time. A cheap A/B tested the narrower installed-boot
question the same afternoon: same host, same QEMU 11.0.50, same
`vm\win2k-smp.img`, same `-machine pc -cpu pentium3 -smp 2 -m 512`, launcher
regenerated with `-Accel tcg`, so the accelerator was the only command-line
variable in that boot trial.

Windows 2000 booted to the desktop under TCG, with the ACPI Multiprocessor
PC HAL, the local APIC present and two vCPUs: progress bar moving at the
25 s sample, desktop by 52 s, Found New Hardware for the xHCI as usual. No
storm, no hang, nothing to sample.

This proves that the installed image boots on this host and QEMU build
without WHPX. It does not falsify an execution-rate explanation for the
original storm, because that observation was during Setup and also differed
in host, QEMU version and binary target. The cause of the Setup storm
remains unresolved. The honest statement is narrower: an APIC-present
multiprocessor Windows 2000 installed image boots fine on this host under
both accelerators, and nothing here explains the original Setup hang.

The next discriminating step, if it ever matters, is to re-run the Setup
workload (not an installed boot) on one host and QEMU build under TCG and
WHPX-with-emulated-irqchip, then repeat under the relevant 11.0.0/11.0.50
and i386/x86_64 combinations to separate accelerator, version and binary
target.

Setup selected the multiprocessor HAL unaided (no F5), and all three roadmap
task-5 verifications passed in the guest: Computer node = ACPI
Multiprocessor PC, two Task Manager CPU graphs, and `ntoskrnl.exe` Original
File Name = `ntkrnlmp.exe`. Host side, `info cpus` shows two vCPU threads,
both `model=pentium3`, with distinct `thread_id`s (22176 and 8772), the
checkpointed rung's own record of the separate-host-thread check Observation
3a below makes mandatory.

### Observation 3 - WHPX is not faster here, which contradicts the phase's stated secondary benefit

Wall-clock boot to desktop, measured the same afternoon by screendumping
each VM at intervals from launcher start:

| VM | Config | To desktop |
|---|---|---|
| 2b | TCG, uniprocessor Standard-PC HAL, `-m 256` | already up at the 30 s sample |
| 2d | WHPX + `kernel-irqchip=off`, ACPI MP HAL, `-smp 2`, `-m 512` | still on `Starting up...` at 24 s; desktop by ~50 s |

That pair is not a controlled comparison (the two VMs differ in HAL, CPU
count and RAM as well as accelerator), so on its own it shows only that 2d
is not visibly faster in practice. The Observation 2a A/B supplies the
controlled version for free, because it boots the same disk image with only
the accelerator changed: WHPX + `kernel-irqchip=off` reached the desktop at
~50 s, plain TCG at ~52 s. Indistinguishable.

This one controlled installed-boot trial observed no material boot-time
advantage for WHPX over TCG. The earlier guess at a mechanism, that
`kernel-irqchip=off` turns every APIC access into a VM exit and penalises MP
timer/IPI traffic, is left as an untested hypothesis; the measurement does
not depend on it, and boot is I/O-bound enough that it may not be the
dominant term at all. The roadmap's and `build-and-test.md`'s "near-native
speed ... by far the fastest VM for long stress runs" is contradicted and
both documents were corrected. This costs the phase nothing: 2d exists to
detect cross-CPU races, and its value is the second CPU, not throughput.
Whether sustained CPU-bound load behaves differently from boot is untested.

Practical consequence worth noting: TCG can boot this installed
configuration on this host. That does not prove it can replace WHPX for
Phase 2d's race-detection role, but one of the reasons to doubt it has since
been measured away, so state the remaining ones rather than as a lump.

### Observation 3a - plain `-accel tcg` is already MTTCG here, while `thread=single` removes simultaneous vCPU execution

Whether the A/B's TCG side ran its two vCPUs on two host threads is a QEMU
property, not a guest one, so it needs no guest boot: `-machine pc -cpu
pentium3 -smp 2 -m 512 -display none`, monitor, `info cpus`.

| `-accel` | `thread_id`s | distinct |
|---|---|---|
| `tcg` | 20052, 20112 | 2 |
| `tcg,thread=multi` | 8048, 18856 | 2 |
| `tcg,thread=single` | 6600, 6600 | 1 |

So plain `-accel tcg` already defaults to MTTCG on this host, and the
Observation 2a A/B did run one host thread per vCPU. The `thread=single` row
matters as the control: it proves the check discriminates rather than
always reporting two.

It is also a trap. `thread=single` collapses both vCPUs onto one host thread
and removes true simultaneous execution. That weakens race coverage, but it
does not make every cross-CPU race structurally invisible: QEMU still
round-robin interleaves the guest vCPUs, so defects that need only an
instruction-stream interleaving can still fire.

The `thread=single` probe did not boot Windows. Because it leaves the
virtual CPU topology unchanged, the three guest-visible checks are expected
to remain satisfied, but that is an inference, not an observation. Distinct
`thread_id`s in `info cpus` establish that QEMU created separate host-vCPU
threads, a prerequisite for parallel execution, not proof that the host
scheduled those threads concurrently, which affinity, core count and load
decide. Record them at each checkpoint and re-run the check whenever the
accelerator string changes or the VM moves host, since the MTTCG default is
per host/target rather than guaranteed.

The other half of that prerequisite is the host process's effective
affinity, not merely the machine-wide processor count: Windows can give a
process a narrower inherited affinity mask. A paused, no-disk probe using
the checkpoint QEMU 11.0.50 binary and the same machine/accelerator/CPU/SMP
flags recorded 8 system logical processors, QEMU process affinity `0xFF`,
and 8 allowed logical processors. That establishes that at least two
processors were available to the vCPU threads in this launch environment. It
still does not demonstrate overlap; only a contended workload does. At each
checkpoint, inspect the actual running QEMU process, require at least two
set bits in its affinity mask, and repeat the check after a host move.

Both host-side prerequisites now live in `scripts/check-smp-parallelism.ps1`
rather than in prose, because a procedure nobody executes cannot fail
loudly; it just stops matching reality. It is a run-time check (it needs a
live VM), so unlike the import, INF and launcher gates it cannot join
`build-driver.cmd`. Validated against live probes in both directions: pass
on `whpx,kernel-irqchip=off`, and exit 1 on `tcg,thread=single` naming the
shared `thread_id`. A check for a silent failure is worth little until it
has been shown to fire on the failure.

What remains genuinely unvalidated for a TCG replacement is therefore
narrower than "explicit `thread=multi` plus concurrency and stress": the
thread model is now confirmed, and what is missing is concurrent driver load
under either accelerator. None has been run yet, on WHPX or TCG. The VM
stays on the checkpointed WHPX rung because that is what the install and
checkpoint were performed on.

### Observation 4 - the `usbd.sys` trap recurred, and on an MP kernel it announces itself one boot earlier and more gently

The `usbd.sys` lesson's fix was followed (preparation boot with no USB
controller, copy `USBD.SYS` from the VVFAT disk) but the copy did not land in
`C:\WINNT\system32\drivers`. The first boot of the run launcher (EHCI + xHCI)
therefore gave:

- the `usbhub20.sys ... could not be loaded` popup, as on 2b; but
- no bugcheck, and Device Manager showed a healthy EHCI controller with a
  yellow-bang "USB 2.0 Root Hub" underneath it.

A yellow bang on a freshly-installed device invites the reading "first boot,
it just needs a restart". That reading is actively dangerous here: on 2b the
restart is what converted this state into the boot-time `STOP: c000026c`
that Safe Mode does not escape. The correct response is to shut down and
re-boot the no-USB launcher, which is diagnosable and safe.

`dir c:\winnt\system32\drivers\usb*.sys` from that no-USB boot showed the
whole stack laid down by the failed run (`usbport.sys` 138,288,
`usbhub20.sys` 49,776, `usbehci.sys` 19,728, plus
`usbcamd.sys`/`usbintel.sys`) and `usbd.sys` absent, while `wmilib.sys`
(4,240) was present. Re-copying `USBD.SYS` (20,688) from the command line
rather than the shell fixed it: the next run boot came up with EHCI and USB
2.0 Root Hub both healthy.

### Observation 5 - a new Unknown device that 2b structurally cannot show

Device Manager lists an extra Unknown device beside the unclaimed xHCI. Its
Resources tab gives a single memory range `FED00000-FED003FF`: the HPET,
which QEMU's `pc` machine advertises through ACPI and which Windows 2000
predates and has no driver for. 2b never shows it because 2b runs
`acpi=off`, and 2d must run ACPI on to get a multiprocessor HAL. Expected,
verdict-neutral, and not to be re-diagnosed: Win2000 times off the PIT/RTC
regardless, and nothing in this driver goes near it.

### Result and reusable rules

Phase 2d checkpoint met. Native `usbport.sys` reads `5.00.2195.6681`,
identical to the 2b record, so the ABI record needed no amendment; the xHCI
sits unclaimed under Other devices with Code 1; VVFAT mounts as `E:`;
snapshots `phase2d-usbd-ok` and `phase2d-clean` exist on `vm\win2k-smp.img`.

- A WHPX failure that names nested virtualization is a partition-creation
  failure, not a guest failure. Check whether QEMU produced a monitor socket
  at all before theorising about the guest; and try `kernel-irqchip=off` as
  the first move, not the last.
- Do not read a WHPX result on one host as a property of WHPX. This project
  now has three distinct WHPX outcomes on three hosts for overlapping
  configs. Probe per host.
- `-accel whpx` alone is not a variable you can change in isolation. On this
  host it decides whether QEMU starts at all, so it must be cleared with a
  60-second throwaway probe before it is put in a launcher.
- A yellow-bang USB 2.0 Root Hub under a healthy EHCI is the
  missing-`usbd.sys` defect, one boot before the bugcheck. Never answer it
  with a restart; detach the USB hardware in the hypervisor and go look at
  the directory.
- Copy dependency files from a command prompt, not the shell, and `dir` the
  destination before trusting the copy. A drag-and-drop that silently lands
  somewhere else cost a full install-boot cycle here.
- When an environment change fixes something, the tempting mechanism is
  usually several uncontrolled variables away. A two-minute installed-boot
  A/B showed that this image boots under TCG too, but could not decide why
  an earlier Setup workload stormed. If the A/B is cheaper than the
  paragraph explaining the theory, run it first, then state which workload
  it tested.
- An uncontrolled comparison can be upgraded for free by re-running one side
  on the other's configuration. This A/B turned "2d feels no faster than 2b"
  into a controlled installed-boot timing comparison because it booted one
  disk image with only the accelerator changed. It did not control the
  earlier Setup workload and therefore did not settle that workload's
  failure.
- An SMP guest can lack true parallel execution while still presenting an
  MP topology. The no-guest-boot probe showed that `-accel
  tcg,thread=single` puts both vCPUs on one host thread. The three guest
  checks are expected, but were not observed, to remain satisfied because
  the virtual CPU topology is unchanged. Round-robin execution can still
  expose interleaving-dependent defects, but it weakens race coverage.
  Record distinct `thread_id`s in `info cpus` at each checkpoint and re-run
  the check whenever the accelerator string changes or the VM moves host.

## A structural zero is not an enforced zero: usbport never masks the SG high DWORD

### Environment and operation

Host-side only, no VM. Phase 3 task 10: `dumpbin /disasm` over the two
already extracted `usbport.sys` builds (NUSB `5.00.2195.5652`, SP4
`5.00.2195.6681`), looking for where the transfer scatter/gather elements
the miniport will program into TRBs actually come from. Extracts kept as
`tools/{nusb,win2ksp4}-extracted/usbport-sglist-disasm.txt`.

The routine was found without symbols by looking for the one thing an NT DMA
adapter call has to look like: `mov ecx,[adapter+4]` then `call dword ptr
[ecx+20h]`, `DMA_OPERATIONS.MapTransfer`. Six such indirect `DMA_OPERATIONS`
call sites exist in the whole image, and only one is at `+0x20`. That is a
cheaper entry point into an unsymbolised binary than following the transfer
path down from `SubmitTransfer`.

### What was observed

`USBPORT_MapTransfer` (the `AllocateAdapterChannel` execution routine, `ret
10h`) does reach the elements only through the adapter, and the two builds
are 540 instructions differing in three operands, all private structure
offsets. The page split ReactOS describes is really in the binary (`mov
ebx,1000h; and eax,0FFFh; sub ebx,eax`, clamped to the remaining length).

But the documented claim that usbport forces `HighPart = 0` is not what the
code does. `MapTransfer` returns a `PHYSICAL_ADDRESS` in `edx:eax` and the
binary stores `edx` into `SgPhysicalAddress.HighPart` verbatim: no mask, no
test. The high DWORD is expected to be zero because the adapter is created
`Dma32BitAddresses = 1` / `DmaWidth = Width32Bits` (read in task 2, a
different routine), so the HAL cannot hand back a PA above 4 GB.

The paths after `MapTransfer` rely on that adapter contract; they do not add
independent enforcement:

- inside one mapped chunk usbport advances only the LowPart (`add
  [ebp-40h],ebx`) and never carries into the HighPart, so it would produce
  wrong addresses itself if a chunk straddled the 4 GB line; and
- the split-transfer builder overwrites only the child element's LowPart
  after the zeroed child allocation has been copied from the parent, so the
  unwritten HighPart is inherited from the parent record rather than from
  zero-initialization.

### Proven / inferred / unknown

Proven: the adapter path, the page granularity, the 24-byte element stride
and 0x10 header (corroborated independently by an element-walk helper that
strides `add edx,18h`), and that the two builds share the code.

Required by the adapter contract, not enforced at the element: the zero high
DWORD. The mapper and split builder both rely on it, so the miniport checks
every SG high DWORD when the callback becomes reachable.

Unknown, and left so on purpose: element ordering versus `SgOffset` across
multiple map rounds or a bounce mapping, and everything else about
`SubmitTransfer`. That callback was unreached on both targets, because the
Phase 3 spike's one synthetic root-hub port is permanently disconnected.

### Reusable rules

- When a document says a value is "forced", check whether anything forces
  it. Here the value was right and the mechanism was wrong, which is the
  version of this mistake that survives testing: the driver would have
  worked, and the reason recorded for why it works would have been false. A
  borrowed reimplementation's behaviour can match while its guarantees do
  not.
- Inherited invariants get checked where the ABI exposes them. `xhci98.sys`
  checks every SG element's high DWORD rather than relying only on the
  adapter's declared width. `USBPORT_RESOURCES.StartPA` is already truncated
  to a `ULONG`, so its high DWORD cannot be checked; validate that the
  common-buffer range does not overflow 32 bits and rely on usbport's
  adapter contract for the hidden half.
- A static pass can settle where data comes from and what shape it has; it
  cannot settle what order it arrives in. Draw that line in writing before
  the next phase inherits the ambiguity.

Normative updates: `docs/usb-xhci-info/usbport-miniport-abi.md` section 5
(`USBPORT_SCATTER_GATHER_LIST`) and section 9 item 8;
`docs/usb-xhci-info/usbport-miniport-interface.md` "What Phase 3 can and
cannot prove about transfer mapping";
`docs/contributing/implementation-invariants.md` "DMA Addresses" and
"Transfer Buffers"; `docs/contributing/architecture.md`.

## The same binary passes on Win2000 SP4, and walking past Win98's crash point is what proves the crash is Win98's

### Environment and operation

Phase 2b VM (Windows 2000 SP4, native `usbport.sys` 5.00.2195.6681,
Standard-PC HAL via `-machine pc,acpi=off -cpu pentium3,-apic`, TCG,
`qemu-xhci` at PCI 0:4.0 IRQ 11 alongside the `usb-ehci` the native stack
already owns), reverted to the `phase2b-clean` snapshot. Phase 3 task 9:
install the debug media through the INF's `.NTx86` path, then bind, disable,
re-enable, uninstall, re-detect and shut down.

Host note: this was run from a third development machine, where QEMU lives
in `C:\Program Files\qemu` rather than under scoop. The committed launchers
hard-code a host path, so both `setup-qemu*.ps1` had to be re-run first; the
per-host re-generation rule held again.

The binary was not rebuilt. `out\pkg-debug\xhci98.sys` is byte-identical to
what Win98 ran (SHA-256 `4593D236...13BE3`); the only source commits since
that link touch code inside `#ifdef XHCI_PROBE_RESOURCES_SIZE`. "One binary
serves both targets" is worth nothing if the two targets ran two links.

### What was observed

Everything registration-related is identical to Win98: `GetHciMn`
`57324B30`, 316-byte packet at `Version 200`, `STATUS_SUCCESS`, 16 service
pointers in the tail, no `ABI-SUSPECT` from the canaries, the 51-slot check
or the ESP comparison, `StartController` at IRQL 0 with the whole
376,832-byte common buffer, and a bind callback sequence that matches Win98
call for call and IRQL for IRQL. Device Manager: no bang, "working
properly", own root hub.

Disable, re-enable and uninstall are all clean here. The teardown is
`RH_ClearFeaturePortEnable`(2) -> `RH_DisableIrq`(0) -> `DisableInterrupts`(2)
-> `StopController(TRUE)`(0). Win98's trace stops after `RH_DisableIrq` and
the machine bugchecks at `0028:C00312EE`. The same image continuing through
the two callbacks Win98 never reaches is stronger evidence than task 8's
`usbehci.sys` control: that showed a Microsoft miniport dying the same way,
this shows our miniport not dying when the OS underneath is different.

Win2000 unloads the image on disable and reloads it on enable. One boot
contains three complete `DriverEntry` -> `USBPORT_RegisterUSBPortDriver`
cycles (install, re-enable, re-detect), each succeeding with the same
service pointers. The static per-site counters in `XHCI_DBG_CB` resetting is
what made this visible; a reload looks like a first boot in the trace.

The common buffer moves between binds (`815B3000`, `815B3000`, `81549000`),
and on Win2000 `StartVA - StartPA` is exactly `0x80000000`, versus an
unrelated pair on Win98. Only page alignment and the sub-4 GB bound are
portable.

`RH_DisableIrq` arrived at IRQL 0 while the callbacks either side of it
arrived at 2.

No idle `SuspendController`/`ResumeController` cycling in about an hour,
where Win98 cycled continuously. Shutdown adds a second `DisableInterrupts`
after `StopController(TRUE)` that Win98 does not send.

### What is proven / inferred / unknown

- Proven: the packet, its size, the version gate, the service tail, the
  `USBPORT_RESOURCES` layout (decoded field-for-field, `HcFlavor = 1000`
  assigned by usbport from our `MiniPortVersion`), the common-buffer
  contract, the bind and teardown sequences, and clean PnP stop/remove on
  the target that actually enforces them. The Win98 disable bugcheck is not
  this driver's.
- Inferred: that the Win98 defect lives in NUSB's back-ported teardown or in
  Win98-under-QEMU rather than in Win98 proper. Nothing here distinguishes
  those two, and neither is worth chasing; the driver is not implicated.
- Unknown: everything the endpoint and transfer families would show. They
  were unreachable by design (one synthetic, permanently disconnected port),
  so this run says nothing about `SubmitTransfer`, SG ordering or IRQL
  there. `InterruptService` was never entered; the ISR/DPC counters stayed
  at zero. SMP is also untested; this VM is uniprocessor, which is what
  Phase 2d exists for.

### Reusable rules

- A crash that reproduces on one target and not the other is localized by
  where the trace stops, not by how the screen looks. Two callbacks past the
  Win98 stopping point was the whole finding.
- Give each target its own trace file. The Win2000 launcher now carries the
  same port-0xE9 `isa-debugcon` channel and per-boot rotation as the Win98
  one, writing `vm\win2k-debugcon.log`. Comparing the targets is the point
  of the gate; one shared log would make that comparison unreadable.
- Rate-limit every trace site, not just the callback banner.
  `XHCI_DBG_CALL_LIMIT` caps `XHCI_DBG_CB`, but the `XHCI_DBG_VALUE` counter
  lines inside `CheckController` are uncapped, so an idle guest produced
  14,000 lines / 396 KB in an hour and buried the lifecycle sequence the
  trace exists to show. Not fixed in this task on purpose: the fix would
  replace the binary the task validated. Phase 4 task 1 owns it.

## Option A works on Win98: the miniport registers and runs; disabling any USB host controller bugchecks the VM, Microsoft's own miniport included

### Environment and operation

Phase 2a VM (Win98 SE + NUSB 3.3, scoop `qemu-system-x86_64` 11.0.0, TCG,
ACPI HAL, `qemu-xhci` at PCI 0:4.0 IRQ 11), rolled back to the `post-nusb`
snapshot first so the INF's per-target `usbd.sys` copy would be genuinely
exercised. Phase 3 task 8: install the debug `out\pkg-debug` media through
Device Manager, then observe bind, disable/re-enable, and shutdown.

Trace capture is new and is what made the rest readable: the run launcher
now carries `-chardev file,... -device isa-debugcon,iobase=0xe9`, so the
`WRITE_PORT_UCHAR(0xE9)` half of `src/xhci_dbg.c` lands in
`vm\win98-debugcon.log` with no kernel debugger attached. The launcher does
not append: each launch archives a non-empty prior trace as
`win98-debugcon.previous.log` and creates a fresh current log, so old
`DriverEntry` lines cannot masquerade as evidence from a replacement image.
The "non-empty" condition matters: rotating unconditionally meant a launch
that died before QEMU wrote anything would archive a zero-byte log over the
previous real one, so two failed launches destroyed the trace the rotation
was added to protect.

### What was observed

The spike passes on Win98. `USBPORT_GetHciMn` returned `57324B30`, the
packet went over at 316 bytes / `Version 200`, `USBPORT_RegisterUSBPortDriver`
returned `0x00000000`, all 16 service pointers were written into the tail,
and no canary, callback-slot or ESP check reported `ABI-SUSPECT`. Device
Manager shows xHCI USB 2.0 Host Controller (xhci98) under Universal Serial
Bus controllers with no yellow bang, "This device is working properly", and
a USB Root Hub child. `StartController` arrived at IRQL 0 with
`StartVA=D2EEA000`, `StartPA=0FF00000`, both page-aligned, below 4 GB, and
the full 376,832-byte common buffer was really allocated, which is the one
half of the task 2 memory model that could not be argued host-side.

The observed bind sequence was `StartController` -> `EnableInterrupts` ->
`CheckController`/`PollController` -> `RH_GetRootHubData` ->
`RH_GetPortStatus` -> `RH_GetStatus` -> `RH_SetFeaturePortPower` ->
`RH_ClearFeaturePortConnectChange` -> `RH_GetHubStatus`/`RH_EnableIrq`, then
repeating `SuspendController`/`ResumeController` idle pairs. Clean shutdown
is `SuspendController` -> `DisableInterrupts` -> `StopController(TRUE)`, no
crash.

Disabling the controller bugchecks: `A fatal exception 0E has occurred at
0028:C00312EE`, page fault, no module named. Re-enabling bugchecks again.
The crash point relative to our callbacks moves between runs (once just
after `StopController`, once after `RH_DisableIrq` with `StopController`
never reached), so it is not one callback misbehaving.

### What is proven, and what the controls rule out

Four differentials, each cheap, in the order they were run:

1. Other devices disable fine. The Dial-Up Adapter disables and re-enables
   cleanly, so Win98's "Disable in this hardware profile" machinery works.
2. The USB Root Hub disables and re-enables fine. Only the controller
   devnode is fatal.
3. The common-buffer size is not the trigger. A probe binary declaring
   `MiniPortResourcesSize = 4096` instead of 376,832 (built through
   `XHCI_PROBE_RESOURCES_SIZE`, verified host-side that the immediate really
   changed) bugchecks at the identical `0028:C00312EE`. The 372 KB request is
   therefore exonerated and task 2's declared limits stand.
4. Microsoft's own miniport does the same thing. Adding `-device usb-ehci`
   let NUSB's `USB2.INF` bind the shipping `usbehci.sys` to a second
   controller; disabling that device bugchecks at the same address.

So the fault is a property of the Win98 + NUSB `usbport.sys` controller
teardown path (or of Win98-under-QEMU), reproduced with a Microsoft miniport
and with two miniport binaries of ours that differ in what they ask for. It
is not evidence against Option A, and it is not a defect in
`xhci_dispatch.c`, whose `StopController` is a flag-clearing no-op that
touches no MMIO.

Unknown, and not guessed at: which module owns `C00312EE`. Win98's
fatal-exception screen prints a bare `0028:XXXXXXXX` whenever the address is
outside a registered VxD's range, and the obvious fix does not work either:
Win98's NTKERN leaves `DriverObject->DriverStart` and `DriverSize` zero
(measured: the new `image base=00000000 image size=00000000` line). On
Win2000 the same line should be usable. The faulting instruction, read from
the frozen guest, is a two-step pointer walk (`mov ecx,[ecx+8]; mov
ecx,[ecx+0x10]`) in VMM-range code, i.e. a structure walk over a pointer
that is already garbage by the time the disable path runs.

### Second finding: the guest reboot after a driver install wedges at the splash

The reboot Win98 offers at the end of a driver install left the guest frozen
on the splash screen. It was not the driver: the debugcon log was 0 bytes,
so `DriverEntry` had not run, and the CPU was in real mode (`CR0=0x10`,
`PE=0`) spinning at `0x8EE4B` on `cmp %es:0x46c,%dx / je`, waiting for the
BIOS tick at `40:6C` to advance. `info irq` showed IRQ0 still being
delivered (81091 -> 81190 over 4 s) while the tick word never changed.
Killing QEMU and cold-starting boots straight through, every time.

### Reusable rules

- Cold-start this VM after a driver install: shut the guest down and
  relaunch rather than taking the offered reboot. `-action reboot=reset`
  warm resets leave it wedged in real-mode boot with a dead BIOS tick.
- Do not disable a USB host controller in the Win98 VM unless the bugcheck
  itself is the experiment. It takes the session down and is not our bug.
  The same teardown is what a driver rollback or uninstall would run, so
  those are unobservable on this VM too until the teardown defect is
  understood.
- Before blaming the miniport for a teardown crash, install a Microsoft
  miniport next to it. `-device usb-ehci` plus an already-installed NUSB is
  a two-minute control that answers "is this ours?" definitively.
- Win98 lets you overwrite a loaded driver binary in place (`copy
  d:\new.sys c:\windows\system32\drivers\xhci98.sys` answers `1 file(s)
  copied`), so a rebuild-and-retest cycle needs no reinstall, only a cold
  restart.
- Do not expect `DriverObject->DriverStart/DriverSize` to be filled on Win98.
- Diagnostic resource-size builds carry `XHCI98_PROBE_BUILD_DO_NOT_DEPLOY`;
  the packager rejects that marker, and `build-driver.cmd` prints `PROBE
  BUILD` for any `XHCI_EXTRA_DEFINES` value. Clear `XHCI_EXTRA_DEFINES` and
  rebuild before making install media.
  - The one exception, added by task 12.3: its failed-start artifact
    (`-DXHCI_FAIL_START_CONTROLLER`) carries a second marker,
    `XHCI98_FAILSTART_ARTIFACT_TASK_12_3`, and `make-package.ps1
    -FailStartArtifact` stages it, requiring both markers, so the exception
    cannot widen to any other diagnostic build. It lands in
    `out\pkg-failstart-<flavor>\`.
- `set VAR= && next` in a `cmd /c` one-liner assigns a space, not nothing.
  Measured while clearing `XHCI_EXTRA_DEFINES`: the "clean" rebuild that
  followed was a diagnostic build, and it announced itself as one, which is
  the only reason it was caught in seconds rather than reaching a package.
  Write `set "VAR="`, quotes inside, whenever the assignment is followed by
  anything on the same line.
- `set VAR 2>nul | findstr ...` never matches. The first attempt at that
  build-time probe label used it and silently labelled nothing. On the left
  of a pipe, cmd hands the command to a child `cmd.exe`, and the stripped
  redirection leaves the SET query prefix with a trailing space, which
  matches no variable name. `set VAR 2>nul` on its own is fine; piped, it
  prints nothing. Use `if defined VAR`. A guard that cannot fail its own
  test is worse than none: this one printed `BUILD + GATES PASSED` and the
  "next, build install media" hint for probe builds for as long as it
  existed.

Affected: `src/xhci_dispatch.c` (the `XHCI_PROBE_RESOURCES_SIZE` knob and the
image-base line), `scripts/setup-qemu.ps1` (debugcon capture),
`docs/contributing/roadmap.md` Phase 3 task 8,
`docs/usb-xhci-info/usbport-miniport-interface.md` ("Observed callback
sequence"), `docs/contributing/build-and-test.md` ("Deploying a build into
the Win98 VM").

## Shipping the wrong OS's `usbd.sys` would load, not fail: the per-target file split has to be authenticated, not trusted to break loudly

### Environment and operation

Host-side only, no VM. Phase 3 task 7: making `xhci98.inf` carry each
target's own `usbd.sys`, the file the `usbhub20.sys` / `USBD.SYS` lesson
below found missing on both VMs. Static reads with MSVC 6.0 `dumpbin`
against the binaries already staged under `tools\` (NUSB 3.3, Windows 2000
SP4, Windows 98 SE media).

### What re-verifying the premise showed

Confirmed, and the design rests on it:

- Both `usbhub20.sys` builds (NUSB `5.00.2195.6891` and SP4 `5.00.2195.6681`)
  import `ntoskrnl.exe`, `HAL.dll`, `WMILIB.SYS`, `USBD.SYS`.
- They need exactly four `USBD.SYS` symbols, the same four in both builds:
  `USBD_GetPdoRegistryParameter`, `_USBD_ParseConfigurationDescriptorEx@28`,
  `_USBD_CreateConfigurationRequestEx@8`, `USBD_CalculateUsbBandwidth`.
- `usbd.sys` is a leaf on both targets: it imports only `ntoskrnl.exe` and
  `HAL.dll`. Carrying it closes the chain rather than opening a new one,
  worth checking before shipping a file, since that whole bugcheck was one
  unresolved import behind a driver that was present on disk.

### The finding: a swapped pair would not announce itself

The instinct is that installing Win98 SE's `usbd.sys` (4.10.2222) on Windows
2000, or SP4's on Win98, would fail at load and be obvious. It would not:

- Both builds export all four symbols `usbhub20.sys` needs, in both
  directions. Cross-checking NUSB's hub against SP4's `usbd.sys` and SP4's
  hub against Win98 SE's leaves nothing unresolved either way.
- The two builds differ by exactly one kernel import (SP4's has
  `ntoskrnl.exe!ExQueueWorkItem` and Win98 SE's does not) and that symbol
  resolves on Win98: it is present in `ntkern.vxd`'s name table, and six
  Win98/NUSB precedent binaries import it (`usbport.sys`, `usbhub20.sys`,
  `usbhub.sys`, `usbstor.sys`, `1394bus.sys`, `sbp2port.sys`).

So the wrong build loads and then misbehaves, at a distance, on a file whose
name is `usbd.sys` on the target either way. There is nothing left to
compare it against after installation.

### Reusable rules

- When one destination name maps to two per-target binaries, structure the
  INF so the wrong one is unreachable, and authenticate the media by hash.
  Structure alone (distinct media names, one `CopyFiles` section per install
  path) proves the engine cannot pick the wrong file; it says nothing about
  which build was staged under which name.
  `scripts\package\usbd-sources.expected` binds media name -> target ->
  version/length/SHA-256, and is read by the stager, the packager, and the
  INF gate, including a `TGT-TARGET` rule that catches the two source names
  being swapped between the sections, which is structurally perfect and
  installs the wrong OS's driver. (Release 1.0.0.1 retired that mechanism:
  the OS supplies the file through the INF's `LayoutFile`, so there is no
  per-target binary on the media to authenticate. The rule stands for the
  next time one destination name means two binaries.)
- "It would fail loudly" is a claim to test, not an assumption to rest a
  gate on. Here it was false, and the cost of finding out at task 8 would
  have been a misattributed no-go on the architecture gate.
- Copy-flag semantics come from `C:\NTDDK\inc\SETUPAPI.H`, which has the
  header's own one-line description per bit. `16` (`COPYFLG_NO_OVERWRITE`)
  is "do not copy if file exists on target"; `32`
  (`COPYFLG_NO_VERSION_DIALOG`) is "do not copy if target is newer", not, as
  is easy to assume from its name, a purely cosmetic dialog suppressor.

### Unresolved, recorded rather than solved: `WMILIB.SYS` on Win98

`usbhub20.sys` also imports `WMILIB.SYS`, and the Win98 SE CD's own
`layout.inf` has no row for it; NUSB does not ship one either. Oney (ch.10
compatibility notes, p.261) puts WMILIB's absence in the original Windows 98
retail release only, and NUSB is a working Win98 SE package, so SE must
resolve it some other way. The `ntkern.vxd` name-table scan is silent on
`WmiSystemControl`/`WmiCompleteRequest`/`WmiFireEvent`, which is no
information; that scan has known false negatives (see the entry below).
There is no file on the SE media for `xhci98.inf` to carry even if it were a
real gap, so this is a risk only for the original Win98 retail release,
which this project does not target. Task 8's load on the 2a VM is the
discriminating test.

### Affected files

`src/xhci98.inf`, `scripts/package/*`, `scripts/inf-gate/check-inf.ps1`
(`TGT-*`, `PKG-IDENTITY`), `scripts/inf-gate/test-inf-checks.ps1`;
`docs/contributing/build-and-test.md` "Carrying a per-target `usbd.sys`".

## The import gate: Win2000 can be settled on the host, Win98 cannot, and `ExAllocatePoolWithTag` is not actually missing on 98 SE

### Environment and operation

Host-side only, no VM. Phase 3 task 5, building the post-link import gate
(`scripts\import-gate\check-imports.ps1`). Inputs: the two linked
`xhci98.sys` flavours; both Windows 2000 SP4 kernel images and all eight HAL
images, expanded from the SP4 ISO and authenticated by
`scripts\import-gate\win2k-baselines.expected`; Win98 SE's own
`usbd.sys`/`usbhub.sys` 4.10.2222; the NUSB 3.3 driver set; and `ntkern.vxd`
from the Win98 SE CD (`WIN98_54.CAB`, named by the media's own `layout.inf`).

### What the two halves can prove

Win2000 is a host-side question. Its loader resolves driver imports against
the export tables of the selected kernel and installed `hal.dll`, all of
which are real files on the media. Resolving the driver's imports against
both UP/SMP kernel images and all eight HAL images is therefore the same
check the targets will make, and the gate fails the build on a miss. All of
those images export the symbols this driver currently uses.

Getting the baseline set right took two corrections. The first
implementation staged only `ntoskrnl.exe`, `hal.dll`, `halapic.dll`, and
`halmps.dll`, then called that set authoritative. Direct listing of the SP4
ISO showed the omitted `ntkrnlmp.exe` plus `halaacpi.dll`, `halacpi.dll`,
`halmacpi.dll`, and `halsp.dll`. The first omission contradicted the Phase
2d SMP target; the ACPI omissions excluded the configurations most likely on
real hardware. The same review found that "a kernel plus any one `hal*.dll`"
was accepted without checking file identity. The repair made one tracked
manifest the source for staging and checking, authenticates
version/length/SHA-256, treats a partial or substituted set as failure, and
adds synthetic regressions for missing and same-length-tampered files.

The second correction found two more things wrong, both by checking the
claim against the media and against the gate's own new rule rather than
against its documentation:

- The SP4 media carries eight kernel HALs, not seven. `I386\HALBORG.DLL` is
  a real kernel HAL (export section `HAL.dll`, 98 names including
  `HalInitSystem` and `KeGetCurrentIrql`, `FileDescription` "Hardware
  Abstraction Layer DLL", 5.00.2195.6655) and it is the only one the media
  ships uncompressed, so it does not match the `HAL*.DL_` pattern the
  "complete" set was enumerated by. The enumeration that was meant to close
  a coverage gap had the same gap. It is now covered (it exports all three
  symbols in use), and the extraction path copies rather than expands a
  packed name that does not end in `_`.
- The Win98 evidence sources were unauthenticated, while the rule added one
  commit earlier said a recorded hash does nothing unless something compares
  it. Naming a path controls which file is read, not what it is; a
  substituted build would have produced evidence lines this project never
  verified, and those lines get quoted into the phase records.
  `win98-evidence.list` now carries version/length/SHA-256 for the
  `[precedent]` binaries and the `[nametable]` `ntkern.vxd`, with the
  asymmetry that matters: absent is silence, present and unrecognized is a
  failure. Two of those hashes matched the ABI record's independently-taken
  values, which is the cross-check that made the data worth trusting.

Win98 is not a host-side question. On Win98 the NT-style export tables are
built at init by `ntkern.vxd`; there is no export table on disk. Two
host-side sources give positive evidence only:

- Precedent: a driver known to resolve on Win98 importing the same
  module/symbol pair. NUSB's `usbport.sys` imports `HAL.DLL!KeGetCurrentIrql`,
  which is the strongest possible corroboration for that pair: it is the
  module this miniport registers with.
- Name-table scan, and this one has false negatives, which is the finding
  worth keeping. `KeGetCurrentIrql` and `KeQuerySystemTime` are absent from
  `ntkern.vxd`'s NUL-delimited name strings, yet NUSB's `usbport.sys` and
  `usbehci.sys` respectively import them on Win98. A hit is evidence; a miss
  is no information. Do not build a gate on it.

### `ExAllocatePoolWithTag` resolves on Win98 SE

Win98 SE's own `usbd.sys` and `usbhub.sys` import
`NTOSKRNL.EXE!ExAllocatePoolWithTag`, as do five drivers in the NUSB set.
Those are the drivers 98 SE loads for any UHCI/OHCI controller, so the
symbol resolves; Oney's Table A-2 row ("missing") is most plausibly about
Win98 gold. The DDK `ExAllocatePool` entry, and `AGENTS.md`, both asserted
the stronger claim.

This changes the reason for the rule, not the rule: the compatibility header
still undefines the DDK's `ExAllocatePool` macro and the gate still denies
both tagged names, because an Option-A miniport should not be allocating
private pool at all. That is now the whole reason. A second reason once
stood beside it, that the original Win98 retail release is unconfirmed and
exports strictly less than SE, and it went when that release stopped being a
target of this project. usbport supplies every buffer, so a pool call site
is a design regression before it is an import risk.

### Reusable rules

- Split an import audit by target: where the target's kernel is a file,
  resolve against it and fail the build; where it is built at run time,
  gather evidence, label its strength, and let the load be the check. Never
  let the two look alike in the output.
- A secondary source's "missing on Win98" row is a hypothesis. The cheapest
  test is to dump the imports of a driver the platform itself ships.
- Exercise a gate against inputs broken on purpose before trusting it.
  Each of the five failure paths here (denied symbol, unlisted pair, wrong
  provider, wrong flavour, baseline that does not export the symbol) was
  made to fire.
- An export baseline is evidence only after both its coverage and its
  identity are enforced. For Windows 2000 that means every kernel/HAL image
  Setup can select, not whichever files happen to be present in a staging
  directory; recorded hashes in prose do nothing unless the gate compares
  them. Apply the rule to every evidence source at once, or the next review
  finds the half you skipped; here, the Win98 files.
- Enumerate a "complete set" from the medium's own listing, never from the
  naming pattern the known members share. `halborg.dll` was missed twice by
  `HAL*.DL_`; one `7z l` of the whole `I386\` directory found it. A
  completeness claim is only as good as the query that produced it.
- Two MSVC 6.0 traps: `dumpbin /exports` exits 53 with no output unless
  `Common\MSDev98\Bin` is on `PATH` for `MSPDB60.DLL`, and the recorded
  module name casing varies between builds (`ntoskrnl.exe` here,
  `NTOSKRNL.EXE` in the shipping miniport) so module comparison must be
  case-insensitive.
- In Windows PowerShell 5.1, `native.exe 2>&1 | Out-Null` under
  `$ErrorActionPreference = "Stop"` turns any stderr line into a
  terminating error with an empty message. The Win98 CD's multi-volume cabs
  make 7z print "Can't open volume" while still extracting correctly, which
  is how this surfaced: check for the extracted file, not the exit code.

Normative updates: `docs/contributing/build-and-test.md` ("Building",
"Post-link import-compatibility gate"), `docs/usb-xhci-info/win98-wdm.md`,
`AGENTS.md`.

## Usbport's common-buffer contract, read off both shipping binaries: 32-bit DMA confirmed, page-aligned in both address spaces, and cached

### Environment and operation

Host-side only: no VM, no install, no guest. Phase 3 task 2. `dumpbin
/disasm` over `USBPORT_StartDevice` and `USBPORT_AllocateCommonBuffer` in
NUSB `5.00.2195.5652` and Windows 2000 SP4 `5.00.2195.6681`. The routine was
located by mapping the `NTOSKRNL.EXE` IAT entry for `IoGetDmaAdapter` to its
call site, then walking back to the `DEVICE_DESCRIPTION` local. Extracts
kept as `tools/{nusb,win2ksp4}-extracted/usbport-commonbuffer-disasm.txt`
(gitignored, regenerate in a minute).

### What is proven

Both builds are instruction-for-instruction the same here, differing only
in FDO-extension field offsets, so one fixed layout serves both targets.

- The DMA adapter is 32-bit. `Dma32BitAddresses = 1`, `DmaWidth =
  Width32Bits`, `Master`/`ScatterGather = 1`, `InterfaceType = PCIBus`,
  `MaximumLength = MAXULONG`, and `Dma64BitAddresses` is never written into
  the zeroed 40-byte `DEVICE_DESCRIPTION`. Matches ReactOS `pnp.c:555-562`
  field for field.
- `StartPA` is `LogicalAddress.LowPart` only. usbport truncates the physical
  address itself and keeps `HighPart` solely for `FreeCommonBuffer`.
- `StartVA` and `StartPA` are both masked to page alignment (`mov
  ecx,0FFFFF000h; and ebx,ecx; and edx,ecx`). This is the fact that makes a
  single table of byte offsets satisfy the xHCI spec's alignment and
  no-cross-boundary rules in virtual and physical space at once.
- The allocation is `ROUND_TO_PAGES(MiniPortResourcesSize + 0x30)`, with
  usbport's 48-byte header placed at the end of the block. Self-corroborating
  in the same function: usbport's own second request is `0xFD0` = 4048
  bytes, exactly one page once its own header is added.
- `AllocateCommonBuffer` is called with `CacheEnabled = TRUE` (`push 1`).
- usbport zeroes the block before `StartController`.
- `MiniPortFlags` bit `0x100` (`NO_DMA`) makes `StartDevice` skip
  `IoGetDmaAdapter` and overwrite its own copy of `MiniPortResourcesSize`
  with zero: no adapter, no common buffer, no diagnostic.
- Incidental packet-offset confirmations from the same routine, all agreeing
  with `docs/usb-xhci-info/usbport-miniport-abi.md` section 3:
  `MiniPortResourcesSize` at `0x24`, `MiniPortExtensionSize` at `0x10`,
  `StartController` at `0x38` called as `(MiniPortExt, Resources)`,
  `Resources->StartVA`/`StartPA` at `0x28`/`0x2C`.

### What this corrected

`AGENTS.md` said the common buffer "must be ... non-cacheable". It is
cached, and under Option A that is not the miniport's choice. This is
correct for x86 (coherent PCI DMA) but it removes an ordering crutch: TRB
publication ordering now rests entirely on `volatile` access, writing each
TRB's Cycle Bit last, and the `WRITE_REGISTER_*` accessors. `AGENTS.md` and
`docs/contributing/design/04-controller-common-buffer.md` section 6 carry
the corrected rule.

### Reusable rules

- Locate an undocumented routine through its imports, not by guessing.
  `dumpbin /imports` prints the IAT as virtual addresses for these drivers
  (unlike `/exports`, which prints RVAs, the trap recorded elsewhere in this
  file), and entries appear in IAT order, so entry n sits at `IAT base +
  4n`. Grepping the disassembly for `call dword ptr ds:[<that>]` lands on
  the call site directly. Cross-check the arity and the constant arguments
  before believing the mapping: `IoGetDeviceProperty` and `IoGetDmaAdapter`
  are adjacent in the SP4 IAT and an off-by-one lands in a plausible-looking
  wrong function.
- Read the caller as well as the callee. `USBPORT_AllocateCommonBuffer`
  alone gives the rounding rule; its `StartDevice` caller is what proves
  which packet field feeds it and what `Resources->StartVA`/`StartPA` mean.
- Phase 0's fact sheet is a design input, not just a go/no-go. The measured
  `scratch=34` on both fleet controllers is what set the scratchpad cap; a
  cap chosen from memory (32 would have been the natural guess) would have
  refused every machine the project owns.

### Affected

`docs/contributing/design/04-controller-common-buffer.md` (the design this
evidence supports), `docs/usb-xhci-info/usbport-miniport-abi.md` section 5
and section 8, `docs/usb-xhci-info/usbport-miniport-interface.md` "Target
ABI record", `AGENTS.md`, `src/xhci.h`, `src/xhci_mem.c`,
`test/test_membuf.c`.

## The usbport registration ABI, read off three shipping binaries: ReactOS's layout is right, its `USBPORT_HCI_MN` is not

### Environment and operation

Host-side only: no VM, no install, no driver code. Phase 3 task 1. `dumpbin
/disasm` (MSVC 6.0, `tools/MSVC600/VC98/Bin` with `Common/MSDev98/Bin` on
`PATH` for `MSPDB60.DLL`) over three `usbport.sys` builds and their matching
in-box `usbehci.sys`:

| Lineage | `usbport.sys` | Source |
|---|---|---|
| Win98 + NUSB 3.3 | `5.00.2195.5652` | `tools/nusb-extracted/` |
| Win2000 SP4 | `5.00.2195.6681` | `tools/win2ksp4-extracted/` |
| Windows XP SP3 (best-effort) | `5.1.2600.5512` | `tools/winxpsp3-extracted/`, newly extracted from `D:\isos\Win XP Pro SP3 OEM original.iso` |

The extracted routines are kept verbatim as
`tools/<dir>/usbport-registration-disasm.txt` and
`usbehci-driverentry-disasm.txt` (those directories are gitignored, so the
findings, not the dumps, are the durable record; the dumps regenerate in
about a minute).

Method note that mattered: `dumpbin` prints virtual addresses (image base
0x10000 for these drivers) while `dumpbin /exports` prints RVAs. Reading
`USBPORT_RegisterUSBPortDriver`'s export RVA as a disassembly address lands
0x10000 short, in the middle of an unrelated function that looks plausible
enough to waste time on. Add the image base.

### What was proven

The ReactOS packet layout is correct, field for field, including the tail.
Two independent reads agree on all three lineages:

1. `USBPORT_RegisterUSBPortDriver` loads `ecx = 0x12C`, adds `0x10` iff the
   `Version` argument is `>= 0xC8`, and `rep movs` that many bytes out of
   the caller's packet, so `sizeof(USBPORT_REGISTRATION_PACKET)` is 316
   (0x13C) exactly as ReactOS's `C_ASSERT` says, and 300 (0x12C) for a
   USB1.1 miniport. The 16-byte delta is the four tail fields
   `RH_ChirpRootPort`/`TakePortControl`/`Reserved4`/`Reserved5` at
   0x12C-0x138, which is how the binary implements ReactOS's "chirp only at
   interface version >= 200" rule.
2. Each shipping `usbehci.sys` `DriverEntry` writes its static packet at
   every ReactOS "in" offset from 0x00 to 0x130 and at no other offset:
   nothing at 0x0C/0x1C/0x20/0x134/0x138 (the `Reserved` fields) and nothing
   in the 0xE4-0x120 block that registration fills with its 16 service
   pointers.

The version gate is a range test, not a magic constant: `Version < 100`
returns `STATUS_UNSUCCESSFUL`, `Version >= 200` selects the long packet. All
three in-box miniports pass 200.

`USBPORT_GetHciMn` does not return `0x10000001` on either primary target.
Both 2195.x binaries are a one-instruction `mov eax,57324B30h; ret`, the
bytes `30 4B 32 57`, "W2K0". Only the XP build returns ReactOS's
`0x10000001`. Each in-box `usbehci.sys` opens `DriverEntry` by calling the
export and comparing against its own lineage's value, returning
`STATUS_UNSUCCESSFUL` on mismatch.

A prior record was wrong. The Phase 2b entry in
`docs/usb-xhci-info/usbport-miniport-interface.md` claimed `0x10000001`
appears "as an immediate inside `USBPORT_RegisterUSBPortDriver`'s argument
check" in both 2195.x binaries. It does not appear in that routine at all.
In both binaries the single occurrence is in usbport's USBUSER request
dispatcher, where it is `USBUSER_OP_SEND_ONE_PACKET`
(`USBUSER_OP_MASK_DEVONLY_API | 1` from `usbuser.h`), one arm of a jump table
over request codes 1-8, i.e. the `StartSendOnePacket`/`EndSendOnePacket`
debug path. The conclusion Phase 2b drew from it (the two builds accept the
same version) survives; the evidence cited for it was the wrong function.

### What is inferred, not read

The NUSB 5652 build allocates 0x14C (332) bytes for its private
`USBPORT_MINIPORT_INTERFACE` and copies the packet to `+0x10` without ever
storing the `Version` argument; SP4 and XP allocate 0x150 (336), store
`Version` at `+0x10`, and copy to `+0x14`. Since the allocation is zeroed
and a version-100 miniport gets only the first 300 bytes copied, the 5652
build can still gate the version-200-only tail callbacks on their pointers
simply being NULL. That is the only consistent reading of the two facts, but
no instruction was read that confirms it. None of this is miniport-visible
either way.

### Reusable rules

- If `xhci98.sys` retains the `USBPORT_GetHciMn` probe, accept both known
  values: `0x57324B30` for the primary 2195.x targets and `0x10000001` for
  XP. The second comparison is a small, isolated compatibility accommodation
  with no effect on either primary path; unknown values still fail. Copying
  ReactOS's single constant would abort `DriverEntry` on both primary
  targets, a self-inflicted no-go that would have looked like an ABI
  mismatch during the spike.
- Pass `Version = 200`, and treat a failed registration as not "nothing
  happened": in every build the version rejection branch sits after the
  driver-object takeover, so the caller's `MajorFunction`/`AddDevice` are
  already replaced and the interface allocation is leaked.
- First probe `MiniPortFlags = 0x95` (`INTERRUPT | MEMORY_IO | USB2 |
  POLLING`), what both primary targets' own `usbehci.sys` declares, rather
  than ReactOS's `0x295`. XP's miniport does set `WAKE_SUPPORT` (0x200); the
  2195.x pair does not, and claiming wake commits the driver to real wake
  behaviour on Win2000.
- Offset 0x8C (`ResetController`) is left NULL by every shipping
  `usbehci.sys`. That confirms the slot position, not usbport's call
  contract: a null-check, EHCI-specific gating, or an unexercised path are
  all consistent with the observation. Keep this unresolved until call-site
  disassembly or the runtime spike, and supply the callback in `xhci98.sys`.
- An in-box miniport built against the exact target binary is better ABI
  documentation than the port driver itself. The registration routine only
  proves the size and the service-block boundary; `usbehci.sys`'s packet
  fill proves the position of all 48 callback slots at once, for free. Reach
  for the consumer, not just the provider.

### Consequences for XP

Packet format is identical across all three lineages, so packet format does
not rule XP out of Option A. That is all it establishes, nothing about XP
callback contracts or runtime behaviour, and no XP validation follows from
it. XP stays a best-effort secondary target with no VM and no checkpoint
(`docs/usb-xhci-info/win98-wdm.md`, "What about Windows XP?").

### Affected documentation

`docs/usb-xhci-info/usbport-miniport-interface.md` "Target ABI record" (all
four "Confirmed" rows filled, XP column added, both corrections recorded);
`docs/usb-xhci-info/usbport-miniport-abi.md` sections 1, 2, 3
(binary-override/confirmation notes) and 9 (items 1-3 answered);
`docs/contributing/roadmap.md` Phase 3 task 1.

## P14s Gen 1 on XHCIQUAL v0.9: two Intel generations agree bit-for-bit on PM, and the "which port is the external connector" guess was wrong

### Environment and observation

ThinkPad P14s Gen 1 Intel (Comet Lake-LP, xHCI `8086:02ED` rev 00, subsystem
`17AA:22B1`), MS-DOS 7.1, XHCIQUAL v0.9 (build stamp in the log banners),
all five batch stages. Logs kept verbatim in
`xhciqual/results/p14s-gen1-2026-07-25/`; this replaced the machine's
earlier record, which was deleted. Every stage completed with no DOS/32A
fault and `C4 IRQ PASS` with one ISR entry on IRQ 11 in `3XIRQ` and both
full runs, so the second fleet Intel machine is now on the same binary as
the E460.

Four observations, none of them a defect:

1. The PM capability reads identically to the E460's: `PMC=C1C2 PMCSR=0008`
   on both, hence the same `v2`, `NoSoftRst=1`, `PME_Support: D3hot,
   D3cold`, `DSI=0 PMEClk=0`, `Aux=375mA`, across a four-year gap in PCH
   generation (100-series Sunrise Point-LP vs 400-series Comet Lake-LP) and
   two different Lenovo boards.
2. The strict-PSI advertised-table lookup cleared on Comet Lake: PSIC 3 on
   the USB2 protocol capability, C8 identified 5 of 5 across Full-Speed and
   High-Speed, no `PSIV n has no USB2 speed-class mapping` and no `PSIV n
   not advertised` line. That path arrived in v0.7, so the machine's earlier
   run never touched it, and QEMU cannot reach it at all (`PSIC 0`).
3. The external USB-A connector on the left side is controller port 4: a
   SanDisk U3 Titanium in it enumerated High-Speed there. The machine's
   earlier record had reasoned that the external connectors would be among
   the companion ports (7, 11, 12), since the four internal devices sat on
   6, 8, 9 and 10. Port 4 is a USB2-only managed port, so that reasoning was
   wrong. The E460 agrees with the corrected picture, not the guess: its
   left connector is port 3, also USB2-only.
4. `lspci` lists a capability the qualifier does not print: an Intel
   vendor-specific structure at `[90]`, which `lspci` itself renders
   `<unknown>`. Not a disagreement: the `PCI caps:` line reports four
   specific capability IDs the tool has a use for, and the walk demonstrably
   ran past `[90]` since it correctly reported `MSI-X=0 PCIe=0`.

### What is proven / inferred / unknown

Proven: the v0.9 report is correct on a second Intel generation, and this
machine's qualification now rests on the current tool. `sudo lspci -vv -s
00:14.0` on the same box was run and agrees on every field, including the
board-specific half that was genuinely new here (subsystem `17AA:22B1`, BAR
at `e33a0000`), so the PM decode is now independently confirmed on two
machines rather than one, and the expected MSI-related differences (`MSI:
Enable+`, `DisINTx+`, `pin A routed to IRQ 125` against the DOS `line=IRQ
11`) showed up as `HARDWARE-TESTING.md` predicts.

Inferred: a uniform Win2000 resume path across recent Intel PCH xHCI is
plausible, given identical PM content four years apart, but two data points
are not a family guarantee, so confirm per controller. Unknown: whether
Comet Lake shares Sunrise Point's `XHCI_PME_STUCK_QUIRK` latch defect. This
run proves only that the wake path exists on it, which is not evidence the
latch sticks; `quirks.c` has no `8086:02ED` row and Linux has no quirk entry
for this part either.

Reusable rules. Do not infer physical connector -> controller port from the
USB2-only / companion split. The split describes how a port is paired with
the USB3 half of the same physical socket, not whether the socket is
user-facing; the only way to learn the mapping is to plug something in
during `5XDEV` and read the C6/C8 port number. Treat the qualifier's `PCI
caps:` line as a presence test for four specific IDs, not an enumeration of
the capability chain; an extra capability in `lspci` is only a disagreement
if it is PM, MSI, MSI-X or PCIe. Finally, keep the `raw: PMC=....
PMCSR=....` line in every record: it is what made "these two controllers
report the same PM content" a checkable claim rather than an impression,
without another trip to either machine.

## E460 on XHCIQUAL v0.9: the PCI decode is confirmed correct against `lspci`, and reading the whole `lspci` output paid for two more fields

### Environment and observation

ThinkPad E460 (Intel Sunrise Point-LP, xHCI `8086:9D2F` rev 21, subsystem
`17AA:5048`), MS-DOS 7.1, XHCIQUAL v0.9 (build stamp in the log banners),
all five batch stages (`1PROBE`/`2XPOLL`/`3XIRQ`/`4XEMPTY`/`5XDEV`). Logs
kept verbatim in `xhciqual/results/e460-2026-07-25/`. Every stage completed
with no DOS/32A fault; `C4 IRQ PASS` with one ISR entry on IRQ 11 (level per
ELCR) in both `3XIRQ` and the two full runs, which is the on-metal
re-confirmation of the v0.7 interrupt-path rewrite (EINT-before-IP
acknowledge, EHB dequeue rule, per-controller memory release).

The PM capability read as:

```
PCI PM: v2  state=D0  D1=0 D2=0  PME_En=0 PME_Status=0
  PME_Support: D3hot, D3cold
  NoSoftRst=1 - keeps state across D3hot->D0
  flags: DSI=0 PMEClk=0  Aux=375mA
  raw: PMC=C1C2 PMCSR=0008
```

`lspci -vv -s 00:14.0` under Linux on the same box returned `Flags: PMEClk-
DSI- D1- D2- AuxCurrent=375mA PME(D0-,D1-,D2-,D3hot+,D3cold+)` and `Status:
D0 NoSoftRst+ PME-Enable- DSel=0 DScale=0 PME-`, matching the qualifier
field for field. The full output was kept, and it cross-checks more than PM:
`Subsystem: Lenovo Device 5048` against `PCI subsys: 17AA:5048`, `Memory at
e1220000 (64-bit, non-prefetchable)` against the BAR line, only `[70]` PM
and `[80]` MSI present against `PM=1 MSI=1 MSI-X=0 PCIe=0` (the two absences
confirmed too), and `>TAbort- <TAbort- <MAbort- >SERR- <PERR-` against a
silent PCI status block. Full comparison table in that results README.

### What is proven / inferred / unknown

Proven: (1) the PM decode is correct. The two hardwired capability fields no
OS can change, the `PME_Support` mask and the D1/D2 support bits, joined by
`DSI`, `PMEClk` and `Aux`, agree with an independent decoder. That is the
half that matters; the three live PMCSR fields agreeing too is a bonus,
since Linux had `xhci_hcd` bound by then and could legitimately have moved
the D-state. This matters because the PCI Bus PM specification is not
mirrored in `docs/references/` and QEMU cannot reach the populated path at
all (every emulated USB controller reports `PM=0`), so `lspci` was the only
check available. (2) The populated PM branch executes on real silicon
without fault, and `--probe-only` remains genuinely read-only (`Probe
safety: PASS`).

Inferred: this controller asserts PME from D3hot and D3cold, so its
`QF_PME_STUCK` entry is a live work item for Win2000 D-state transitions
rather than the Win98 footnote it was filed as. Unknown: whether the latch
actually sticks on this silicon. The capability proves the wake path
exists, not that the Linux-sourced quirk claim reproduces here.

### Reusable rules

Weight the hardwired configuration over the live state when comparing a DOS
reading against a Linux one. A mismatch in writable state proves nothing and
would send someone hunting a decoder bug that does not exist. On this
machine the live differences were `MSI: Enable+ Count=8/8`, `DisINTx+` in
the Control word, and `Interrupt: pin A routed to IRQ 123` where DOS saw
`line=IRQ 11`, which look alarming in isolation but are one fact: Linux
switched the controller to MSI, which requires setting INTx Disable and
produces a non-legacy vector. The current D-state, `PME_En` and `PME_Status`
can move for the same reason. This is also the configuration the target
drivers must avoid, since neither Win98 nor Win2000 SP4 has an MSI path.

Read the whole `lspci -vv`, not just the capability under test. Pulling the
full output for the cross-check surfaced fields worth reporting that nobody
had thought to ask for, and they went into v0.9: `NoSoftRst` (this
controller retains state across D3hot -> D0, so a Win2000 resume path need
not treat every D0 return as a cold controller; a per-device bit, confirm it
per controller), `DSI`, `PMEClk`, `Aux`, the capability version, the
subsystem IDs (the VID/DID names the silicon; these name the board), and the
sticky PCI Status error bits. The cheap diagnostic you already have is a
good source of requirements for the one you are building.

Print the raw words behind any decoded block. The first cross-check had to
reconstruct `PMC`/`PMCSR` from `lspci`'s decode because the tool printed
only the decoded fields; v0.9 prints `raw: PMC=.... PMCSR=....` so a future
disagreement is re-decodable from the log without another trip to the
machine. The captured words matched the reconstruction exactly.

Sticky error bits need two snapshots or they say nothing. The PCI Status
error bits are RW1C and survive across boots, so a set bit at probe time is
evidence about the machine, not about the run. v0.9 snapshots 0x06 before
touching anything and re-reads after the active tests, reporting
pre-existing state separately from what the run itself raised, and never
clears either, since clearing would destroy evidence for the next diagnosis.
On this machine both are clean, which `lspci` confirms (`>TAbort- <TAbort-
<MAbort- >SERR- <PERR-`).

Affected: `xhciqual/mmiodiag.c` (PM and status decoders), `xhciqual/pci.c`
(config reads and the post-test status re-read), `xhciqual/quirks.c`
(`QF_PME_STUCK`), `docs/usb-xhci-info/xhci-programming.md`,
`docs/contributing/roadmap.md` Phase 0.

## Smart App Control, not "Device Guard", was blocking freshly linked binaries; `xhciqual\build.cmd` now runs end to end

### Environment and observation

Host: this Windows 11 Pro build machine, Open Watcom 2.0 at `C:\WATCOM`.

Two symptoms, months apart, turned out to be one cause:

- `C:\WATCOM\BINNT64\wmake.exe` refused to run ("was blocked by your
  organization's Device Guard policy", exit 199), so `xhciqual\build.cmd`
  failed at its `wmake` step. The recorded workaround was to bypass the
  makefile and drive `wcc386` / `wasm` / `wlink` by hand.
- Adding the host-side unit runner (`xhciqual/test/run-host-tests.cmd`)
  produced the same message for its own freshly linked `test_mmiodiag.exe`,
  but only intermittently, roughly one launch in three, with the identical
  binary and command line.

### What is proven

- The block is not a classic WDAC/AppLocker path or publisher rule. It was
  Smart App Control, whose message names "Device Guard policy" and whose
  reputation check evaluates each newly written unsigned executable.
- It is location independent: measured 1-in-3-ish blocks with the test exe
  written to the OneDrive-synced repo and to `%TEMP%` on the local C:
  volume. Moving build output off the synced folder is not a workaround.
- Disabling Smart App Control removed both symptoms. Measured immediately
  after: 8/8 consecutive clean runs of `run-host-tests.cmd` (41 checks, 0
  failed each), and `wmake` runs normally. `xhciqual\build.cmd` completed
  end to end for the first time on this host (BAT EOL check -> host tests ->
  wmake -> linked `XHCIQUAL.EXE`).

### Reusable rules

- "Blocked by your organization's Device Guard policy" on a binary you just
  compiled is a reputation verdict, not a policy rule. An intermittent block
  on an unchanged binary is the tell: real WDAC rules are deterministic.
- Do not diagnose it by moving the output directory. That is the intuitive
  first move and it proves nothing here.
- Never let it read as a test failure. A blocked launch and a failing
  assertion look alike from a script's exit code. `build.cmd` now says so in
  a comment at the point of use.
- Smart App Control cannot be re-enabled without resetting Windows, so this
  host stays in the "off" state; a different build host may hit the original
  symptom again. The `wcc386`/`wasm`/`wlink`-by-hand path still works and
  remains the fallback.

### Affected

`xhciqual/build.cmd` (host tests wired in ahead of `wmake`),
`xhciqual/test/run-host-tests.cmd`.

## Phase 2c migration: whole QEMU estate moved to `qemu-system-x86_64` (TCG kept); WHPX proven-optional at best, incompatible at worst

### Environment and observation

Host: this Windows 11 Pro machine. Phase 2c enabled the Windows Hypervisor
Platform (`Enable-WindowsOptionalFeature -Online -FeatureName
HypervisorPlatform` + reboot; `systeminfo` now reports "A hypervisor has
been detected". This loads the Hyper-V hypervisor at boot, so other host
virtualization software falls back to its Hyper-V mode). QEMU is the single
scoop install at `%USERPROFILE%\scoop\apps\qemu\current\`.

The migration was done by the project's differential discipline (change one
variable at a time): first swap the binary with TCG kept and every
guest-visible flag unchanged, re-verify each component, then trial WHPX per
component as a separate recorded experiment.

- Why x86_64: in this scoop QEMU 11.0.0, WHPX is compiled into
  `qemu-system-x86_64.exe` only (`-accel help` -> `tcg`, `whpx`); the
  `qemu-system-i386.exe` that 2a/2b and the xhciqual were validated on is
  TCG-only. A 32-bit guest runs identically under the x86_64 system emulator
  (x86 boots through real then 32-bit protected mode regardless of the
  target's 64-bit capability), so no new QEMU install was needed; everything
  stays on the one scoop build.
- Scripts (`setup-qemu.ps1`, `setup-qemu-win2k.ps1`, `run-qemu-matrix.ps1`,
  `run-win98-batch.ps1`, and the regenerated `scripts\local\*.cmd`) now
  launch `qemu-system-x86_64.exe`; only the executable name changed. 2a
  keeps its ACPI HAL flags, 2b keeps `-machine pc,acpi=off -cpu
  pentium3,-apic`. The matrix harnesses resolve the binary from PATH so the
  29-case gate runs with no `-Qemu` argument.
- TCG re-verification (the regression gate): xhciqual 29/29 (task 4); Win98
  2a boots to desktop, PS/2 input, xHCI Code 28, VVFAT (task 5); Win2000 2b
  boots to desktop, EHCI + "USB 2.0 Root Hub" healthy, xHCI Code 1, VVFAT
  (task 6). All qcow2 snapshots (`post-nusb`, `phase2a-usbd-ok`,
  `phase2b-clean`) survived the binary swap untouched (snapshots are
  format-level).

### What is proven / inferred / unknown

Proven: the x86_64 binary under TCG treats all three existing guests (DOS
qualifier, Win98, Win2000-UP) identically to the retired i386 binary, so the
project's last dependency on the TCG-only i386 build is retired. The three
WHPX trials give a clean spread: xhciqual passes 29/29 under `-accel whpx`
(WHPX proven-optional); Win98 2a will not boot under WHPX (`failed to unmap
GPA range`, memory-region churn); Win2000 2b will not usably boot under WHPX
(`-apic`-mask segfault, else interrupt-starved stall). Adoption decision
across the board: everything keeps its committed TCG launcher/gate; WHPX was
adopted into no committed launcher.

Inferred: WHPX's value for this project is confined to the Phase 2d SMP
guest that structurally needs it (an APIC-based MP HAL that TCG cannot run
without the vector-0xD1 storm); the 2a/2b WHPX failures are guest-specific
and do not predict 2d, but two knobs carry forward: the `-apic` CPUID mask
is unusable under WHPX (2d avoids it), and `kernel-irqchip=off` is a real
effect-having rung for the 2d install ladder. Unknown: whether a newer
QEMU/WHPX clears the 2a/2b faults; not pursued, since neither guest gains
from WHPX.

### Reusable rule / adoption decision

Phase 2c checkpoint met: host reports the hypervisor; the 29-case matrix
passes on `qemu-system-x86_64.exe` with no `-Qemu` arg; 2a/2b boot from
regenerated launchers with their checkpoint clauses re-observed and
snapshots intact; every WHPX trial outcome is recorded (this entry + the
three per-component entries below). This gates Phase 2d, not Phase 3.
Normative updates: `docs/contributing/roadmap.md` (Phase 2c tasks 1-10) and
`docs/contributing/build-and-test.md` "Option B: QEMU" invocation examples
(`qemu-system-i386` -> `qemu-system-x86_64`).

## Phase 2c WHPX trial (Win2000 2b): the `-apic` mask segfaults WHPX, and even without it the boot stalls interrupt-starved

### Environment and observation

Host: this Windows 11 Pro machine with `HypervisorPlatform` enabled (Phase
2c task 1); `systeminfo` reports "A hypervisor has been detected"; scoop
QEMU 11.0.0 `qemu-system-x86_64.exe`, `-accel help` lists `tcg` and `whpx`.
Guest: the Phase 2b Win2000 SP4 uniprocessor VM (`vm\win2k.img`, restored
to the `phase2b-clean` snapshot), booted from hand-edited copies of
`scripts\local\qemu-win2k-run.cmd` with only the `-accel` line changed
(everything else identical: `-machine pc,acpi=off`, `-m 256`, `-vga
cirrus`, usb-ehci + qemu-xhci, VVFAT, monitor). The committed TCG launcher
was left untouched; all WHPX copies were throwaways. Three configurations,
none reaching the desktop, so none of task 6's clauses (desktop, EHCI + "USB
2.0 Root Hub" healthy, xHCI Code 1, VVFAT mount) could be exercised:

- `-accel whpx` with the committed `-cpu pentium3,-apic`: QEMU segfaults
  instantly (exit code 139, no QEMU output at all; it dies before the
  monitor port opens).
- `-accel whpx,kernel-irqchip=off` (QEMU emulates the interrupt controller
  instead of the WHPX in-hypervisor one, the Phase 2d task-2 mitigation
  rung): QEMU survives. The guest enters the NT kernel (monitor `info
  registers` shows 32-bit paged protected mode, `CR0=8001003b`, and the KPCR
  at `FS` base `ffdff000`) and paints the "Starting up..." splash, then
  stalls there for ~4 minutes with the progress bar never advancing (2b
  under TCG reaches the desktop in well under a minute). Per the "sample EIP
  before calling a fixed screen a freeze" rule (the Standard-PC HAL lesson
  below), it is not hard-frozen: EIP cycles among three addresses, a `dec
  eax; jne` countdown delay-spin in a boot driver (`0xf4011189`) and HAL
  code doing an 8259 EOI (`sti` at `0x80069a8a`, `outb %al,$0xa0` just
  after). That is the signature of a boot-start driver busy-waiting
  (KeStallExecutionProcessor-style) on a device interrupt that never lands
  through WHPX's emulated PIC.
- Diagnostic: plain `-accel whpx` with the APIC present (`-cpu pentium3`, no
  `-apic` mask; the roadmap's suggested experiment since 2b's installed
  Standard-PC HAL ignores the APIC anyway) does not segfault. It runs past
  120 s, then stalls at "Starting up..." exactly like the
  `kernel-irqchip=off` case.

### What is proven / inferred / unknown

Proven: (1) the instant segfault is caused specifically by the `-apic` CPUID
feature-mask under WHPX; removing the mask removes the crash (the roadmap
predicted "guest-feature masking there is limited"; confirmed). (2) 2b does
not reach the desktop under WHPX in any of the three configurations; the
best case is a permanent early-boot stall.

Inferred: the stall is interrupt starvation. A boot driver polls for a
device IRQ that WHPX's emulated PIC never delivers to the guest (EIP parks
in a driver delay loop and HAL PIC-EOI code). This is a different failure
from the Win98 2a WHPX result (2a: `failed to unmap GPA range`
memory-region churn; 2b: gets deep into the NT kernel then starves for
interrupts). Unknown: whether the in-hypervisor irqchip (plain `whpx`
without the mask, left running only ~2 min) would eventually progress, or a
newer QEMU/WHPX clears it. Not pursued, because 2b gains nothing from WHPX;
its uniprocessor differential checks run fine and fast under TCG.

### Reusable rule / adoption decision

2b stays on TCG. Per Phase 2c's "anything that misbehaves stays on TCG"
rule, the committed `qemu-win2k-run.cmd` is unchanged (TCG default on the
x86_64 binary, re-verified in task 6). WHPX acceleration is recorded as
incompatible for the Win2000 uniprocessor guest. Note for Phase 2d: 2d is a
different guest (APIC on, `acpi=on`, no `-apic` mask, `-smp 2`, and its
multiprocessor HAL actively uses the local APIC rather than ignoring it), so
neither the 2a nor the 2b WHPX result predicts 2d's outcome. Two facts carry
forward: the `-apic` CPUID mask is unusable under WHPX (2d already avoids
it), and `kernel-irqchip=off` is a real effect-having knob on this host and
belongs on the 2d install ladder as planned.

Normative updates: `docs/contributing/roadmap.md` (Phase 2c task 9).

## Phase 2c WHPX trial (Win98 2a): Win98 will not boot under WHPX - `failed to unmap GPA range`

### Environment and observation

Host: this Windows 11 Pro machine with `HypervisorPlatform` enabled (Phase
2c task 1); scoop QEMU 11.0.0 `qemu-system-x86_64.exe`, `-accel help` lists
`tcg` and `whpx`. Guest: the Phase 2a Win98 SE VM (`vm\win98.img`, restored
to the `phase2a-usbd-ok` snapshot), booted from a hand-edited copy of
`scripts\local\qemu-win98-run.cmd` with `-accel whpx` added and nothing else
changed (same `-machine pc -cpu pentium3 -m 256`, same qemu-xhci, VVFAT,
floppy, monitor). The committed TCG launcher was left untouched; both WHPX
copies were throwaways.

Two configurations, same fatal result: QEMU aborts with exit code 3 and
prints `qemu: WHPX: failed to unmap GPA range`; the Win98 desktop is never
reached, so none of task 5's clauses (desktop, PS/2 input, xHCI Code 28,
VVFAT mount) could be exercised.

- `-accel whpx` (plain): aborts immediately, before the QEMU monitor TCP
  port even opens. Also emits `warning: Ignoring request for interrupt
  vector 0`.
- `-accel whpx,kernel-irqchip=off` (the mitigation rung the Phase 2d task-2
  ladder lists; QEMU emulates the interrupt controller instead of the WHPX
  in-hypervisor one): gets further. The monitor port came up and the VM
  survived a few seconds into early boot, then died with the same `failed
  to unmap GPA range`, exit 3.

### What is proven / inferred / unknown

Proven: Win98 SE does not boot under WHPX in this QEMU 11.0.0 build, with or
without the in-hypervisor irqchip. The common failure is a WHPX GPA-remap
error, consistent with Win98's early-boot legacy memory churn (real-mode ->
protected mode, A20, BIOS/option-ROM shadow-RAM remapping) exercising a
memory-region unmap path WHPX handles differently than TCG. Inferred:
`kernel-irqchip=off` addresses the interrupt-controller aspect (it removed
the immediate abort and the vector-0 warning path) but not the GPA-unmap
fault, so the root cause is memory-region churn, not interrupt routing.
Unknown: whether a newer QEMU/WHPX or a stripped device set would clear it;
not pursued, because 2a gains nothing from WHPX (see below).

### Reusable rule / adoption decision

2a stays on TCG. Per Phase 2c's "anything that misbehaves stays on TCG"
rule, and because the roadmap already notes WHPX does not speed up the
Win98 driver-iteration loop (that loop lives on 2a regardless), there is no
reason to push WHPX onto this guest. The committed `qemu-win98-run.cmd` is
unchanged (TCG default on the x86_64 binary, re-verified in task 5). WHPX
acceleration is recorded as incompatible for the Win98 guest. This is a
data point for Phase 2d: the Win2000 SMP guest that actually needs WHPX is
a very different (APIC-based, NT) guest, so this Win98 result does not
predict 2d's outcome, but `kernel-irqchip=off` is confirmed to be a real,
effect-having knob on this host.

Normative updates: `docs/contributing/roadmap.md` (Phase 2c task 8).

## Phase 2c WHPX trial (xhciqual): the DOS qualifier runs clean under WHPX

### Environment and observation

Host: this Windows 11 Pro machine with `HypervisorPlatform` enabled (Phase
2c task 1); `systeminfo` reports "A hypervisor has been detected", and
`qemu-system-x86_64.exe -accel help` lists `tcg` and `whpx`. QEMU is the
scoop build at
`%USERPROFILE%\scoop\apps\qemu\current\qemu-system-x86_64.exe`.

Trial: the Phase 0 qualifier's 29-case QEMU regression (`XHCIQUAL.EXE`,
MS-DOS 7.1 / bare Win98SE floppy boot) was re-run under WHPX from a
hand-edited copy of the harness, `run-qemu-matrix-whpx.ps1`, identical to
`run-qemu-matrix.ps1` except that `-accel whpx` was inserted into the
per-case QEMU argument list. The committed TCG harness was left untouched.
Result: 29/29 PASS, including the paths expected to be fragile under WHPX:

- `xhci_irq_selftest` and `xhci_none_default` (C4 IRQ delivery on IRQ 11)
  both reported `C4 IRQ: PASS`. The DPMI 0204/0205 vector install + locked
  assembly one-shot ISR delivers a real hardware interrupt to the DOS guest
  under WHPX as under TCG.
- `--poll-only` cases, all EHCI/OHCI cases, and the mixed/multi-controller
  cases all matched their expected serial output.
- No `DOS/32A fatal | exception | page fault | general protection` text in
  any case (the harness fails the case on that text, so its absence is
  checked, not assumed).

### What is proven / inferred / unknown

Proven: the anticipated WHPX friction (the roadmap warned the DOS qualifier
"leans on real-mode/DPMI behavior that WHPX handles differently than TCG")
did not materialize as a failure for this harness. Real-mode boot, DPMI
protected-mode entry, PIC-line IRQ delivery, and DMA round-trips all behave
identically enough to pass every assertion. Inferred: WHPX is a viable
accelerator for the qualifier, not merely for the Win2000 SMP guest that
Phase 2d actually needs it for. Unknown: bare-metal-only questions (the
repaired ISR was confirmed on the E460/P14s under real DOS, not WHPX) are
unaffected either way; this trial speaks only to the QEMU regression
estate.

### Reusable rule / adoption decision

The xhciqual's committed regression gate stays on TCG default (Phase 2c task
4: 29 cases pass with no `-Qemu` argument, i.e. TCG); that is the portable,
host-agnostic gate. WHPX is a proven-optional accelerator for this
component: to reproduce, copy the matrix harness and add `-accel whpx` to
the `$args` line. No committed launcher was changed and the throwaway copy
was not retained.

Normative updates: `docs/contributing/roadmap.md` (Phase 2c task 7).

## The installed Win2K DDK rewrites `ExAllocatePool` to the Win98-incompatible tagged import

### Environment and observation

The exact headers installed by `scripts/install-w2kddk-cabs.ps1` were
inspected before the Phase 3 driver source was created:

- `C:\NTDDK\inc\wdm.h:155` and `inc\ddk\ntddk.h:171` unconditionally define
  `POOL_TAGGING 1`;
- later in each header, `ExAllocatePool(a,b)` is defined as
  `ExAllocatePoolWithTag(a,b,' mdW')` / `...' kdD'`;
- the actual two-argument `ExAllocatePool` function prototype is present
  earlier in the same headers.

This is direct local-toolchain evidence, not an inference from Oney's XP
DDK description. Source that visibly calls `ExAllocatePool` will otherwise
link an `ExAllocatePoolWithTag` import.

The tagged entry point is not, as this entry once said, something "Win98
does not export" (see the import-gate entry above): Win98 SE's own
`usbd.sys` imports it. The macro is still undefined and the tagged names are
still denied, but as policy because Option A needs no private pool, not
because the export is known to be absent.

### Reusable rule

After including the DDK header, the project's compatibility header must
`#undef ExAllocatePool` before any allocation call. The prior function
prototype remains in scope, so calls then bind to the real two-argument
entry point. Every linked driver must still pass the module/symbol import
audit; the binary, not the source spelling, is the proof.

Prefer avoiding private pool allocation under Option A: usbport allocates
the miniport extension and controller/endpoint common-buffer extensions,
and fixed metadata can usually live inside those declared sizes.

Normative updates: `AGENTS.md`, `docs/usb-xhci-info/win98-wdm.md`, and
`docs/contributing/build-and-test.md`.

## `fat:rw:` is host-writeback, not an immutable transfer disk

The Phase 2 launchers briefly used `file=fat:rw:<hostdir>` and described the
temporary qcow2 file visible under `%TEMP%` as proof that guest writes could
not reach the host. That conclusion was wrong: `rw` selects VVFAT's beta
host-writeback mode. The presence of an overlay is not an isolation
guarantee.

Bare read-only VVFAT cannot be attached directly as an IDE hard disk under
QEMU 11.0.50 (`Block node is read-only`) because the frontend requests
writable media. The safe construction is a read-only VVFAT backing with an
explicit per-drive snapshot:

```
-drive "file=fat:<hostdir>,format=raw,if=ide,snapshot=on"
```

A monitor `info block` check showed a temporary qcow2 top node backed by
`driver=vvfat, rw=false`. The guest can write to the apparent disk, but
those writes disappear with the overlay and the host directory remains
immutable. Both Phase 2 launchers and the deploy-loop documentation now use
this form.

Reusable rule: do not infer backing-store isolation from the presence of a
temporary overlay. Check the complete block graph and the backing driver's
write mode. Never use `fat:rw:` for a host-to-guest-only deploy path.

## `usbhub20.sys` bugchecks Win2000 with `c000026c` / `0xc0000034` when its import `USBD.SYS` is missing, not itself

### Environment and observation

Phase 2b Win2000 SP4 VM (QEMU 11.0.50, TCG, Standard-PC HAL per the
Standard-PC HAL lesson), first boot with `-device usb-ehci` present so the
native usbport stack would install. PnP installed the USB 2.0 stack, then:

- on the desktop: `Windows - Unable to Load Device Driver:
  \SystemRoot\system32\DRIVERS\usbhub20.sys device driver could not be
  loaded. Error Status was 0xc0000034`;
- after the requested restart, a boot-time bugcheck `STOP: c000026c (Unable
  to Load Device Driver)` naming the same file;
- Safe Mode and Safe Mode with Command Prompt bugchecked identically, so the
  usual safe-mode escape does not apply.

`dir c:\winnt\system32\drivers\usb*.sys` (taken before the restart) showed
`usbhub20.sys` present and the correct size (49,776 bytes, the SP4 build),
which makes the "object name not found" status look impossible.

### Mechanism

`0xc0000034` is `STATUS_OBJECT_NAME_NOT_FOUND`, and for a driver load it is
reported against the driver being loaded even when the missing object is one
of its imports. `dumpbin /imports` on the SP4 `usbhub20.sys` gives
`ntoskrnl.exe`, `HAL.dll`, `WMILIB.SYS`, `USBD.SYS`, and `USBD.SYS` was
absent from the guest's `usb*.sys` listing. Win2000 only lays down the USB
files a present controller needs; this PnP-triggered install placed
`usbport.sys`, `usbehci.sys` and `usbhub20.sys` but not `usbd.sys`, so the
hub driver had an unresolvable import from its first load onward.

### The fix

Boot with no USB controller on the QEMU command line (removing `-device
usb-ehci` stops PnP starting the stack; the xHCI is irrelevant here, since
nothing binds it), copy `USBD.SYS` in, then re-attach the controllers:

```
7z e win2ksp4.ISO -o<dir> I386\USBD.SY_    &&  expand USBD.SY_ USBD.SYS
copy X:\USBD.SYS C:\WINNT\system32\drivers\      (X: = the VVFAT transfer disk)
```

`scripts/setup-qemu-win2k.ps1` makes this ordering explicit. It can stage
the SP4 file with `-Win2KUsbdSys`, generates `qemu-win2k-prepare-usbd.cmd`
with no USB controller, and tells the user to complete that copy before
`qemu-win2k-run.cmd` attaches EHCI + xHCI. A fresh setup no longer presents
the known-crashing launcher as the immediate next step.

The SP4 ISO copy is `5.00.2195.6658`, 20,688 bytes, the system's own file,
so nothing foreign is introduced. After the copy the VM boots clean with
both `usb-ehci` and `qemu-xhci` attached, and Device Manager shows a healthy
"Intel(r) 82801DB/DBM USB Enhanced Host Controller" + "USB 2.0 Root Hub".

### Reusable rules

- A driver-load `0xc0000034` names the loader, not the missing file. Before
  believing a "file not found" about a file you can see on disk, dump the
  driver's imports and check every imported `.SYS` is present.
- Safe Mode is not a guaranteed escape from a failed USB-stack driver on
  Win2000; detaching the hardware in the hypervisor is the reliable lever,
  and it is available for free in a VM. Removing the QEMU device is cheaper
  and less destructive than Last Known Good or a Recovery Console session.
- Win2000 installs USB files on demand per detected controller, so a VM
  installed with no USB controller can end up with a partial stack when one
  is added later. Check for `usbd.sys` alongside `usbport.sys` before
  trusting a freshly-installed native stack.

### This is a hole in Microsoft's own INF, and it will recur on real hardware

Reading the SP4 `usb.inf` (expanded from `I386\USB.IN_`) shows the omission
is structural, not a VM accident:

- `[EHCI.CopyFiles.NT]` = `usbehci.sys` + `usbport.sys`
- `[HUB20.CopyFiles.NT]` (used by `[ROOTHUB2.NT]`, the `USB\ROOT_HUB20`
  install) = `usbhub20.sys`
- `[USB.CopyFiles.NT]` = `usbd.sys` + `usbhub.sys`, referenced only by the
  USB 1.1 (UHCI/OHCI) device sections

So `usbd.sys` arrives only via a USB 1.1 controller install or the base OS
install on a machine that had USB at setup time. A USB2-only, and by
extension an xHCI-only, machine never gets it, yet `usbhub20.sys` imports
it. NUSB's `usbhub20.sys` has the same import (`dumpbin /imports`), so Win98
targets have the same latent hole; base Win98 SE ships `usbd.sys`, but that
must be verified per machine rather than assumed.

Consequence for Phase 3: check `usbd.sys` exists before the miniport spike,
and carry it in `xhci98.inf`'s `CopyFiles`. Otherwise the root hub fails to
load with a `0xc0000034` that names `usbhub20.sys` and reads as "the
miniport didn't work", a false no-go on the architecture gate.

Done (Phase 3 task 7). `xhci98.inf` carries both builds under distinct media
names, selected by the install section each engine reads; see
`docs/contributing/build-and-test.md`, "Carrying a per-target `usbd.sys`",
and the `usbd.sys` authentication entry in this file for what re-verifying
this diagnosis turned up.

### Confirmed the same day on the primary target: the Win98 VM has no `usbd.sys`

`dir c:\windows\system32\drivers\usb*.sys` on the Phase 2a Win98 SE VM
(rolled back to its `post-nusb` snapshot) lists exactly the NUSB-placed set
(`USBAUDIO`, `USBAUTH`, `USBEHCI`, `USBHUB20`, `USBNTMAP`, `USBPORT`,
`USBSTOR`, `USBU2A`) and no `USBD.SYS`. The absence of `uhcd.sys`,
`openhci.sys` and `usbhub.sys` alongside it identifies the cause: this VM
never had a USB 1.1 controller, so Win98 never installed the USB 1.1 stack
that carries `usbd.sys`, and NUSB does not ship one (its file set is
`USBAUTH`/`USBEHCI`/`USBHUB20`/`USBNTMAP`/`USBPORT`/`USBSTOR`/`USBU2A`).

So the gap is not Win2000-specific and not a VM artifact; it is intrinsic
to this project's deployment target. An xHCI-only machine has no UHCI/OHCI
to trigger the USB 1.1 install, so "install NUSB" alone leaves
`usbhub20.sys` with an unresolvable import. The Win98 SE media does carry
the file (`BASE4.CAB` -> `usbd.sys`, 4.10.2222, 18912 bytes, SHA256
`0118DB14...F56A`; kept in `tools/win98se-extracted/`), so `xhci98.inf` can
simply carry it.

The earlier "NUSB places the stack unconditionally, confirmed on real
xHCI-only hardware" check looked for `usbport.sys`/`usbhub20.sys`/
`usbehci.sys` only. It did not check `usbd.sys`, so the real-hardware
machines are unverified on this point; re-check on the next bare-metal
session.

## Win2000 on QEMU 11 (TCG): force the Standard-PC HAL (`-cpu ...,-apic` + `acpi=off`) or Setup hangs at "Setup is starting Windows 2000"

### Environment and observation

Phase 2b. Host: Windows 11 x64, scoop QEMU 11.0.0, `qemu-system-i386`, TCG
only (that binary reports `-accel help` = `tcg`; the same scoop package's
`qemu-system-x86_64.exe` reports `tcg`, `whpx`, and that x86_64+WHPX path is
the basis of roadmap Phase 2c; everything below concerns the i386 binary).
Guest: Windows 2000 Professional SP4 integrated (`D:\isos\win2ksp4.ISO`,
volume `ZRMPFPP_EN`, retail FPP, `SETUPP.INI Pid=...000`). Booting the CD
reaches the text-mode banner and then hangs indefinitely at "Setup is
starting Windows 2000", the blue text screen that precedes "Welcome to
Setup". Observed unchanged for 8+ minutes across multiple attempts.

### Mechanism (localized from the monitor)

Not a hang in the "CPU halted" sense; the guest is executing. Sampling `info
registers` over the QEMU monitor showed EIP pinned to a ~140-byte window in
ntoskrnl (`0x800ca19b`/`0x800ca1f6`/`0x800ca223`), a tight interlocked loop
(`lock cmpxchg` on a lock word at `0x800ca300`, each leaf prefixed with an
`out 0x7e` I/O-delay). The caller is an interrupt prologue for IDT vector
0xD1, the Windows APIC-HAL clock interrupt. So the guest is drowning in the
local-APIC timer ISR: the clock fires faster than the ancient guest can
drain it under modern-QEMU TCG, and forward progress stalls (an interrupt
storm / livelock, not a device or media fault).

### What did NOT work (each tested on a fresh disk)

- `-device usb-ehci`/`qemu-xhci` present vs. all USB off (`-machine
  pc,usb=off`, no xHCI): identical hang. USB is a red herring here (the
  classic "remove the USB controller" VM advice did nothing).
- `-cpu pentium3` vs. `-cpu pentium-v1`.
- `-global ide-device.win2k-install-hack=on` (the documented Win2000 IDE
  hack) with `-vga cirrus`: still hung.
- `-machine pc,acpi=off` alone: still hung (QEMU still exposes the
  IOAPIC/local APIC, so Setup still picked an APIC clock path).
- Blind F5-spam to reach the text-setup "Computer Type" (HAL) menu:
  unreliable through the monitor.

### The fix

Remove the CPU local APIC and the ACPI tables so Setup selects the
Standard-PC HAL (8259 PIC + PIT, clock on IRQ0/vector 0x30):

```
-machine pc,acpi=off  -cpu pentium3,-apic
```

With that, Setup marched straight through text-mode into the graphical
"Installing Devices" phase. Kept alongside it (Win2000-on-QEMU hygiene, not
the cure): `-global ide-device.win2k-install-hack=on` and `-vga cirrus`.

### Why this is safe for Phase 2b (contrast with Win98)

Win98 needed the opposite (`setup /p j`, the ACPI HAL) because its legacy
PnP-BIOS enumerator is broken under QEMU with no PCI bus. Win2000 is
different: it has a real PCI bus driver that enumerates fine under the
Standard-PC HAL, so the xHCI (`PCI\CC_0C0330`) still appears as an
unrecognised device, and the Phase 2b checkpoint is unaffected. The choice
of HAL is orthogonal to which `usbport.sys` build ships.

### Reusable rules

- On this TCG-only host a hang at a fixed screen is not necessarily a
  freeze. Sample EIP over the monitor; a tiny EIP range under an
  interrupt-prologue caller means an ISR storm, and the culprit is the
  timer/HAL, not the peripheral on screen.
- For NT-class guests (Win2000/XP) that stall at boot on modern QEMU/TCG,
  force the Standard-PC HAL with `-cpu ...,-apic` + `-machine ...,acpi=off`
  before touching CPU model / USB / IDE knobs.
- Keep the same HAL flags in the run launcher as the install launcher, or
  the installed system re-hits the storm on normal boot.
- Baked into `scripts/setup-qemu-win2k.ps1` and the generated
  `scripts/local/qemu-win2k-{install,run}.cmd`.

## Win98 on QEMU: install with `setup /p j` (ACPI HAL) or PCI never enumerates

### Environment and observation

Standing up the Phase 2a Win98 SE VM on QEMU 11.0 (`-machine pc`, `-device
qemu-xhci`), a normal ISO install (Setup auto-run from the CD) booted to the
desktop but enumerated no PCI devices at all. Device Manager (connection
view) showed every device hanging directly off `Computer` with no "PCI bus"
node: the xHCI, the e1000 NIC, and even the PIIX IDE and QEMU VGA were
absent, Windows having fallen back to generic "Standard" drivers (Standard
Display Adapter (VGA), Standard IDE/ESDI Hard Disk Controller, etc.). Under
System devices, "Plug and Play BIOS" carried a yellow `!` with Code 24
("device is not present, is not working properly, or does not have all its
drivers installed"). The emulator side was fine: `info pci` showed
`qemu-xhci` at bus 0 dev 4 (`1b36:000d`, class `0C0330`) the whole time.

### Mechanism, and what did not work

In Win98's Standard-PC (non-ACPI) config the "PCI bus" device is a child of
the "Plug and Play BIOS" enumerator. Win98's legacy PnP-BIOS enumerator does
not initialize against QEMU/SeaBIOS (Code 24), so its child PCI bus is never
created and no PCI function is ever walked. Things that did not recover it
on an already-installed system: Device Manager "Refresh" at the Computer
root; removing the "Plug and Play BIOS" node and rebooting; the "Add New
Hardware" automatic detection (found nothing). The HAL/enumerator choice is
made at install time and cannot be cleanly switched afterward. QEMU version
was not the cause: the same failure reproduced identically on QEMU 7.0 and
11.0; and `-machine pc,acpi=off` is the wrong direction (it removes the ACPI
tables the fix relies on).

### The fix

Install Win98 with `setup /p j`, which forces the ACPI HAL (bypassing
Win98's BIOS-date whitelist) so Windows enumerates PCI via ACPI instead of
the broken PnP BIOS. QEMU's ACPI must stay enabled (the default; do not pass
`acpi=off`). Because `setup` run from a bare DOS prompt needs a formatted
`C:` first, the working sequence is: boot the CD -> "Start computer with
CD-ROM support" -> `fdisk` (create + activate a primary partition) -> reboot
-> `format c:` -> `D:\setup.exe /p j` -> run the GUI wizard. After this the
xHCI enumerates as an unrecognized "PCI Universal Serial Bus" (Code 28)
under Other devices, the Phase 2a "xHCI appears unrecognized" checkpoint.

An alternative that keeps the more-stable Standard-PC HAL is the "PCI bus
method": plain install, then Device Manager -> System devices -> Plug and
Play BIOS -> Update Driver -> "Display a list..." -> Show All Hardware ->
PCI Bus. Not used here; `/p j` is less GUI-fiddly.

### Operational notes (same session, same VM)

- This QEMU build exits on a guest-initiated reboot. Win98 Setup reboots
  several times; without mitigation the process dies each time. Launch with
  `-action reboot=reset -no-shutdown` to keep reboots/shutdowns inside one
  QEMU session. Do not `system_reset` from the monitor during Setup's
  "Setting up hardware and finalizing settings" / first-boot detection; it
  wedges Win98 on the splash (the one-time finalization is interrupted).
- After Setup completes it may leave the boot device on the CD ("Boot from
  Hard Disk / CD-ROM" menu on each reboot). `eject ide1-cd0` (then
  re-`change` it in when needed) or boot with `-boot c` to go straight to
  the HDD.
- Driving the GUI headlessly via the monitor: the PS/2 mouse is relative and
  too imprecise to click reliably (acceleration); use `sendkey` for keyboard
  and `screendump` (720x400 PPM in text mode, 640x480 in GUI; parse P6, this
  build ignores the file extension). The QEMU tablet driver would give
  absolute positioning but needs a working USB stack first.

### Reusable rules

1. For Win98 on QEMU, install with `setup /p j` (ACPI HAL); a plain install
   leaves the PnP-BIOS enumerator broken (Code 24) and no PCI bus, so
   nothing on PCI, including the xHCI, is ever seen. Verify PCI enumeration
   in Device Manager connection view (look for a "PCI bus" node) before
   trusting a VM.
2. "Plug and Play BIOS" Code 24 with no PCI bus is the signature; a redetect
   on an already-plain install will not fix it. Reinstall with `/p j` (or do
   the PCI bus driver-swap).
3. Keep QEMU ACPI on (default). `acpi=off` makes it worse.
4. Use `-action reboot=reset -no-shutdown`; never `system_reset`
   mid-first-boot.

Normative detail added to `docs/contributing/build-and-test.md` (QEMU
install procedure and reboot flags). Related: the NUSB/EHCI lesson below.

## The NUSB installer places the usbport stack unconditionally - no EHCI required (corrects an earlier wrong conclusion)

### The wrong theory (recorded, then debunked same day)

While standing up the Phase 2a Win98 SE VM (QEMU 11.0, `-device qemu-xhci`
only) with NUSB 3.3, `usbport.sys`/`usbhub20.sys` appeared absent, and the
conclusion drawn was that the stack only installs when NUSB's `USB2.INF`
binds an EHCI (`PCI\CC_0C0320`), so an xHCI-only machine (`0C0330`) would
get nothing, and `xhci98.inf` would have to carry the stack itself. Adding
`-device usb-ehci` seemed to "fix" it. This was wrong, on two counts below.

### What actually happened

1. Wrong-directory false negative. The "absent" check looked in
   `C:\Windows\System\`. Win98 keeps these WDM drivers in
   `C:\WINDOWS\SYSTEM32\DRIVERS` (dirid 10 + `system32\drivers`). The files
   were there the whole time. (This is the same wrong-path assumption
   corrected across the docs the same day.)
2. The NUSB installer copies the stack unconditionally. `_NUSB.INF`
   `[DefaultInstall]` runs `CopyFiles=...,W98Upd.Copy.Sys32,...` at setup
   time; `[W98Upd.Copy.Sys32]` (dest = dirid 10 + `SYSTEM32\DRIVERS`) lists
   `USBPORT.SYS`, `USBHUB20.SYS`, and `USBEHCI.SYS` outright. The copy runs
   when you install NUSB, with no dependence on any EHCI device.
   `USB2.INF`'s per-EHCI-device `CopyFiles` is a separate, redundant path;
   it is not what puts the files on disk.

Confirmed on real xHCI-only hardware: installing Win98 + NUSB on a machine
with only an xHCI controller (no EHCI/companion) placed `usbport.sys` +
`usbhub20.sys` correctly in `C:\WINDOWS\SYSTEM32\DRIVERS`.

### Deployment consequence (corrected)

The project's baseline is a full NUSB install (the representative end-user
environment). After it, the usbport stack is present in `SYSTEM32\DRIVERS`
regardless of controllers, so `xhci98.inf` (binding `PCI\CC_0C0330`) can
rely on the stack being there. Bundling the usbport files in `xhci98.inf`'s
own `CopyFiles` is now an optional/defensive measure for a hypothetical
non-NUSB host, not the hard requirement the earlier note claimed.

### Reusable rules

1. Win98 WDM drivers live in `C:\WINDOWS\SYSTEM32\DRIVERS`, not
   `C:\Windows\System\`. Check the right directory before concluding a file
   is missing; a wrong-path check spawned an entire false theory here.
2. NUSB's `_NUSB.INF` `[DefaultInstall]` places `usbport.sys` +
   `usbhub20.sys` + `usbehci.sys` unconditionally at install time. No EHCI
   is needed. Verify by `dir C:\WINDOWS\SYSTEM32\DRIVERS\usbport.sys` after
   a NUSB install.
3. An emulated EHCI (`-device usb-ehci` alongside `-device qemu-xhci`) is
   optional, useful only as a live `usbehci.sys`-on-`usbport.sys` miniport
   to observe while building `xhci98.sys`. It is not required for the stack
   to exist, and the real target is xHCI-only, so the default dev VM should
   omit it.

Normative detail added to `docs/contributing/build-and-test.md`
("Bootstrapping xHCI-only machines" and the QEMU/NUSB install notes).
Related: "Read installer INFs before describing what a package changes" and
`docs/contributing/roadmap.md` Phases 3/8.

## The field target is MS-DOS 7.1, not the FreeDOS CI stand-in

### Environment and observation

The QEMU regression matrix originally cold-booted FreeDOS (FD1.3), but the
qualifier's real target is MS-DOS 7.1, the DOS underneath Windows 98; that
is the interrupt/PIC/extender environment the driver must eventually
coexist with. The two DOSes are not interchangeable at the shell. FreeDOS's
FreeCOM expands the `%ERRORLEVEL%` pseudo-variable; MS-DOS 7.1 `COMMAND.COM`
does not. The field batch wrappers of the time captured the qualifier's
exit code with `set XQRC=%ERRORLEVEL%`. On FreeDOS that works and the matrix
stayed green; on the MS-DOS 7.1 target that expands to the empty string, so
`set XQRC=` unsets the variable and a passing run takes the
`statuserr`/`goto` path and reports a false "says PASS but XHCIQUAL
returned" error.

### What is proven and the repair

A green FreeDOS matrix does not validate MS-DOS 7.1 batch semantics; the
shells differ. The only portable exit-code test on MS-DOS 7.1 `COMMAND.COM`
is `IF ERRORLEVEL n` (true when the code is >= n), read before any later
command resets the code. `3XIRQ.BAT`, `4XEMPTY.BAT`, and `5XDEV.BAT` now
bucket the code immediately after the run:

```bat
set XQRC=0
if errorlevel 1 set XQRC=1
if errorlevel 2 set XQRC=2
```

The original wrappers also called `FIND` to confirm the run reached `Done.`
and to read the verdict string. The verdict is already the exit code, and
the completion check is better done without an external tool: XHCIQUAL
gained a `--done-flag FILE` option that deletes FILE at startup and
recreates it only after a normal finish, so a DOS/32A crash mid-test leaves
it absent. The wrappers now check it with the built-in `IF EXIST` and drop
`FIND` entirely; no `FIND.EXE` (absent from a bare Win98 boot disk) or any
other external tool is required. This also honours the rule to distinguish
an abnormal termination from a clean failing verdict.

Rather than keep a shell that masks target-only bugs, the CI moved off
FreeDOS entirely. Both `xhciqual/test/run-qemu-matrix.ps1` (the 29-case
controller matrix, driving `xhciqual` directly) and `run-win98-batch.ps1`
(driving the `.BAT` wrappers by name) now boot a bare Win98SE (MS-DOS 7.1)
floppy, the same `COMMAND.COM` the field procedure uses. All 29 matrix cases
and the wrapper harness pass there. The Win98 image is proprietary and
gitignored (MS-DOS cannot be committed), so both harnesses skip cleanly when
it is absent; a developer supplies a bare boot floppy at `tools\w98se.img`.

Building that bare disk is itself a lesson: a stock Win98 Emergency Boot
Disk carries a menu, RAMdrive setup, and CD/SCSI drivers that hang
unattended automation, and it lacks `FIND.EXE`. Strip it to
`IO.SYS`/`MSDOS.SYS`/`COMMAND.COM`/`HIMEM.SYS` with a minimal
`CONFIG.SYS`/`AUTOEXEC.BAT` (mtools under WSL) so it boots straight to a
prompt.

Reusable rule: run CI on the OS you actually ship against when the thing
under test is OS-visible. Batch control flow, environment expansion, and
exit-code handling run through the target `COMMAND.COM`, so a convenient
stand-in shell can hide real bugs; prefer built-ins (`IF ERRORLEVEL`, `IF
EXIST`) over external tools, and boot the target DOS in CI. This mirrors the
standing caveat that QEMU success is necessary but not sufficient for the
E460 (see the IRQ-source lesson below).

## Arm an owned IRQ source before exposing a DOS/32A vector

### Direct field observation

- The supplied ThinkPad E460 v0.5 logs pass C1/C2/C3 and stop before the C4
  checkpoint in both the empty and connected-device full runs. The two logs
  are identical. Poll-only reaches `Done.` and passes C6 on ports 6-8.
- Both photos show DOS/32A exception 0Dh at `0008:00001AC1`, whose first
  bytes are `66 CF` (`IRETD`). The field EXE build stamp is `Jul 20 2026
  23:25:01`; the repository MAP was from a later build and cannot resolve
  that EIP.
- `main.c` performs only `qual_irq()` between the durable C3 and C4 lines.
  Device attachment is therefore not the discriminating variable.

### Localized mechanism and repair

Version 0.5 installed the protected-mode handler and unmasked IRQ 11 inside
`irq_install()` before xHCI interrupt state was cleared, enabled, and armed
with the C4 No-Op. A shared or stale IRQ could therefore enter DOS/32A's
reflection/return path during that window. The ordering is the strongest
source-level explanation for the v0.5 regression, but the exact DPMI
transition remains an inference until traced on hardware.

The repair removes that window and the implicit extender machinery:

- vector save/install/restore uses checked DPMI `0204h`/`0205h` calls;
- DPMI `0600h` locks the complete assembly entry and its contiguous state;
- the wrapper preserves registers and segment registers and returns with an
  explicit 32-bit `IRETD`, without calling C or DOS services;
- the PIC line remains masked until the qualifier observes its own xHCI or
  EHCI interrupt-pending state;
- the owned one-shot ISR disables/acknowledges the controller source and
  re-masks the PIC line before EOI;
- teardown restores the old protected-mode vector before saved PIC masks;
- `--irq-selftest` runs only the prerequisites and C4, skipping C6/C8; and
- a failed full-mode C4 no longer continues into C6. The field batch
  wrappers also require both exit status and `Done.` and preserve partial
  crash logs.

Open Watcom built the change with `-wx`; the linked map places
`irq_pm_entry_` through `irq_pm_entry_end_` in a 216-byte locked range. The
isolated QEMU test passed with one IRQ 11 entry and clean teardown, and the
complete 29-case cold-boot matrix passed with no DOS/32A exception
signature, including shared-IRQ-11 xHCI+EHCI configurations. This proves
the QEMU paths and build integration.

A bare-metal E460 run (build `Jul 21 2026 21:04:54`) then confirmed the
repair on hardware: `--irq-selftest` reported one IRQ 11 entry with clean
teardown, and the empty and device full runs both reached a QUALIFIED
verdict with C4 IRQ 11 PASS and no DOS/32A exception, so the v0.5 regression
is resolved on that machine. Multi-cold-boot repetition through the field
batch wrappers continues on the P14s Gen 1; keep each exact EXE/MAP/log set.

Reusable rule: for a temporary DOS extender hardware-IRQ test, vector
installation is not the point at which delivery may be exposed. Keep the
PIC line masked until a source owned by the test is observably pending,
make the ISR one-shot, and restore the vector while still masked. A fixed
extender `IRETD` fault is tool-path evidence, not a silicon
disqualification.

## A DOS/32A IRQ fault is not an xHCI disqualification

### Environment and observation

- Machine: Lenovo ThinkPad E460 running Windows 98 SE DOS 7.1. The captured
  evidence did not establish which startup profile or TSR set was loaded, so
  do not describe this run as clean DOS without further confirmation.
- Controller: Intel Sunrise Point-LP xHCI, PCI `8086:9D2F` revision `21`.
- Read-only probe: BAR0 `E1220000` below 4 GB, MSE/BME enabled, MSI
  disabled, `INTA#` on IRQ 11, xHCI 1.00, 32-byte contexts, 34 scratchpads,
  12 managed USB2 ports, and USBLEGSUP present. No static disqualifier was
  found.
- Active command used `xhci --no-wait`. C6 printed `port 6 connect
  (PORTSC=000206E1), resetting...`, followed immediately by DOS/32A
  exception 0Dh (general-protection fault).
- The fault was at a 16-bit code segment whose bytes began `66 CF` (`IRETD`).
  XHCIQUAL's application code is USE32. This strongly points to the
  extender's interrupt reflection/return path rather than the C6 PORTSC
  write itself, but the exact mechanism remains an inference until
  reproduced with a matching release map or DPMI-level trace.

### What the evidence means

- Reaching C6 proves C2 reset returned success and C3 observed a command
  completion through controller-visible DMA: `main.c` calls `qual_ports()`
  only through those two continuation branches. It does not prove that the
  C3 completion code was `CC_SUCCESS`; `qual_dma()` also continues after a
  non-success completion and records C3 as WARN.
- Reaching C6 does not prove C1 or C4. The active sequence records but does
  not gate later work on `qual_handoff()` or `qual_irq()` success.
- The interrupt-path crash makes C4 and C6 inconclusive. It does not justify
  a `DISQUALIFIED` verdict for the controller. The defensible record is:

  ```text
  Static probe: PASS
  C2 reset: inferred PASS from control flow
  C3 DMA: completion observed; final PASS/WARN unavailable
  C4 IRQ: INCONCLUSIVE - qualifier/DOS32A GPF
  C6 ports: INCONCLUSIVE - GPF resetting connected port 6
  Overall: NOT YET QUALIFIED; not DISQUALIFIED
  ```

- `--no-wait` is not read-only and does not skip C6. It merely skips the
  15-second plug prompt; every already-connected managed USB2 port is still
  reset. Internal laptop devices can therefore trigger C6 even when all
  external USB devices are unplugged.

### Reusable rules and next tests

1. After a DOS/32A exception during an active test, cold boot before doing
   anything else. Normal cleanup did not run, so controller ownership, IRQ
   vectors, PIC masks, and controller state cannot be trusted.
2. Do not repeat the active test with the same binary merely to see whether
   it crashes again. `--probe-only` remains the safe inventory path.
3. Treat a fault in a DOS/32A 16-bit `IRETD` stub as an
   extender/interrupt-hook problem until a narrower test proves hardware
   failure. The repository has already seen the same class of DOS/32A
   protected-mode-vector failure in the OHCI path under QEMU.
4. Add a polling-only diagnostic before changing hardware verdict logic. It
   can test controller progress and inspect interrupt status without
   installing the protected-mode ISR, although it cannot award formal
   CPU/PIC delivery PASS.
5. For a repaired ISR path, investigate explicit DPMI protected-mode vector
   services and locking of ISR code/data, avoid unsafe chaining through an
   extender thunk, and drain C6 port events/update ERDP with EHB rather
   than only acknowledging USBSTS/IMAN.
6. Reproduce against the target DOS. (The QEMU matrix has since moved off
   the FreeDOS stand-in and now boots bare Win98SE/MS-DOS 7.1 directly, so
   the old FreeDOS-vs-DOS-7.1 differential no longer applies; see the
   MS-DOS 7.1 lesson above.)

### Repair and root cause

The observability and no-ISR mitigations from rules 1-5 above are now in
the tool (version 0.4), and building the expanded matrix reproduced the
crash under QEMU, which the prior note assumed impossible.

- QEMU did reproduce the GPF. While expanding the matrix, the
  `ehci_ohci_none` case (usb-ehci + pci-ohci, no devices) hit, at EHCI C4,
  the identical fault the E460 showed: `exception 0Dh` at `0008:00001AC1`,
  bytes `66 CF` (16-bit `IRETD`). It is intermittent (once in a 27-case run;
  0/6 and 10/10 clean in isolation), i.e. timing-sensitive, not
  input-sensitive. This is the same class as the earlier OHCI-under-QEMU
  vector crash; the EHCI path had simply not tripped it before.
- Root cause strongly supported: the fault address is fixed across machines
  because it is DOS/32A's own default hardware-IRQ reflection thunk. Our ISR
  reached it via `_chain_intr(old_isr)` on the not-ours path (`old_isr` is
  that thunk). Chaining a protected-mode interrupt frame into the extender's
  16-bit reflector faults at its `IRETD`. The E460 hit the same path when a
  C4-installed handler took an interrupt during the first C6 port reset.
- Fix (ISR repair, first item): `irq.c` no longer calls `_chain_intr`, so
  the crashing reflector path is deleted. Version 0.5 also makes the
  temporary no-chain policy safe for a level-shared line: a not-ours
  interrupt is counted, masks that PIC line before EOI, and is reported.
  The old vector is restored while the line is masked, then the saved masks
  are restored. This cannot service the foreign device, but it cannot
  livelock in the qualifier either; a proper DPMI tail-chain remains
  optional hardening for testing genuinely shared lines.
- Also mitigated independently: in the full run the xHCI interrupt source
  is shut down and the vector removed the instant C4 finishes, so C6 never
  runs with a live handler; and `--poll-only` installs no handler at all.
  These remove the crash exposure even without the chain fix.
- Optional hardening (was "bare-metal-gated"): a fully robust protected-mode
  ISR (explicit DPMI vector services 0204h/0205h, locking ISR code/data
  (0600h), an assembly entry wrapper, and a DPMI-correct tail-chain that
  services a genuinely shared IRQ instead of swallowing it) is now nice to
  have for robustness/other silicon, not required: the E460 passes C4 with
  the current chain-removed ISR (below).

### Confirmed on the E460

The owner re-ran the v0.4 build on the same ThinkPad E460 that faulted. Both
runs reached `Done.` with no DOS/32A exception, on the ports (6/7/8 = Intel
Bluetooth `8087:0A2B`, Bison camera `5986:0708`, Validity fingerprint
`138A:0011`) that crashed before:

- `xhci --poll-only`: C1/C2/C3 PASS, C4 SKIP, C6 reset ports 6/7/8, verdict
  PROVISIONAL. No fault; acceptance criterion 1 met.
- `xhci` (full): C1/C2/C3 PASS, C4 PASS (ISR fired on IRQ 11,
  level-triggered, count 1), C6 PASS (3/3 resets), C8 identified all three
  internal devices, verdict `QUALIFIED for the Win98 xHCI driver`. No
  `foreign` interrupts were counted, so the handler entered and returned
  cleanly for its own No-Op interrupt.

What this strongly supports: the E460 crash was a C4-installed handler
taking an interrupt during C6 (fixed by tearing the source/vector down the
instant C4 ends), compounded by the `_chain_intr` reflector path (now
deleted). The two mitigations changed together, so the E460 rerun alone
does not isolate them; the matching QEMU fault supplies the independent
`_chain_intr` evidence. CPU/PIC INTx delivery genuinely works on this
machine; the earlier run never got far enough to show it. The E460 is the
first fleet machine QUALIFIED on bare metal. Reproducibility across cold
boots is still worth a couple of repeats, and other fleet machines remain
to be run.

Reusable rules: (1) `_chain_intr` / `_dos_setvect`-installed hardware-IRQ
handlers are unsafe under DOS/32A; a fixed `0008:xxxx` `IRETD` fault is the
extender's reflection thunk, not your code. (2) A single
`qprintf()`/`vsprintf` formatting more than its stack buffer is a silent
stack-smash (it corrupted help output when the OPTIONS block grew); the
tool now bounds it with `_vsnprintf` into a 1 KB buffer. (3) Do not leave a
live hardware-IRQ handler installed across a later destructive gate; shut
the source down and restore the vector as soon as the interrupt test itself
is decided. (4) If an unsafe extender prevents correct shared-IRQ chaining,
mask an unowned level IRQ before EOI and restore the old vector before
unmasking; otherwise the still-asserted foreign source can livelock the
CPU.

Related implementation: `xhciqual/irq.c`, `xhciqual/bringup.c`,
`xhciqual/main.c`, and `xhciqual/report.c`. Related procedure and
diagnosis: `xhciqual/HARDWARE-TESTING.md`, `xhciqual/README.md`, and
`docs/contributing/failure-diagnosis.md`.

## QEMU USB placement follows speed, not xHCI port numbers

Observed while expanding the Phase 0 matrix: QEMU's `port=N` is its own
bus-slot numbering, not the logical Supported-Protocol port number seen by
the guest. QEMU places a device on the USB2 or USB3 side according to the
speed the model advertises. In particular, `usb-storage` normally lands on
an unmanaged SuperSpeed logical port, so a test can appear to have attached
storage while exercising none of this project's managed USB2 path.

Reusable rules:

- Omit `port=` for mixed-speed matrices unless the QEMU bus topology has
  been inspected explicitly.
- Use `qemu-xhci,p3=0` to force a SuperSpeed-capable storage model to fall
  back to High-Speed for USB2-path tests.
- Accept a Supported Protocol capability with a zero port count; `p3=0`
  still leaves such a USB3 capability in QEMU.
- QEMU auto-inserts a hub after root ports are exhausted. Do not infer a
  direct-root topology solely from the command line.
- QEMU's `usb-hub` is Full-Speed. It cannot validate transaction-translator
  behavior for FS/LS devices behind a High-Speed hub; that needs passthrough
  of a physical HS hub or real hardware.

Normative detail: `docs/contributing/build-and-test.md` "How QEMU places
devices on USB2 vs USB3 logical ports" and "QEMU coverage limits".

## Old PCI interrupt reporting can look like broken INTx

The first OHCI C4 implementation required PCI Status.Interrupt Status.
Review showed that this bit arrived with PCI 2.3 and is commonly hardwired
to zero on the older chipsets most likely to contain OHCI. Treating zero as
failure would have disqualified working period hardware. Treating every
zero as unsupported, however, would qualify a modern controller that never
asserted INTx. Both false conclusions were corrected.

Reusable rule: classify the PCI interface before interpreting a missing
status bit. PCIe is modern; conventional PCI can be probed using the paired
Interrupt Disable bit. Unsupported pre-PCI-2.3 reporting earns a documented
partial WARN, while a modern interface that fails to assert status earns
FAIL. QEMU's OHCI implements the PCI 2.3 mechanism, so the unsupported
branch needs real hardware coverage.

## Alignment does not prove boundary containment

The qualifier initially aligned xHCI DMA structures but did not guarantee
that the entire structure stayed within the boundary required by xHCI Table
6-1. A 2 KB DCBAA can be aligned suitably and still straddle a 4 KB page.

Reusable rules:

- DMA allocation APIs must take both alignment and a no-cross boundary.
- Ring segments must not cross 64 KB; the DCBAA, contexts, and scratchpad
  pointer array must not cross `PAGESIZE`.
- A transfer TRB's data buffer must not cross a 64 KB physical boundary;
  split a larger fragment into chained TRBs.
- Under the DOS qualifier, controller-visible memory comes from conventional
  DPMI 0x0100 allocations on a clean, identity-mapped boot. DPMI 0x0501 does
  not promise physical contiguity, and 0x0800 maps a known physical MMIO
  range rather than discovering a buffer's physical address.

Normative detail: `docs/contributing/design/01-hardware-qualification-tool.md`,
`docs/usb-xhci-info/xhci-data-structures.md`, and
`docs/contributing/implementation-invariants.md`.

## Headless capture is display-mode specific

The Phase 0 matrix established a reliable guest-agent-free DOS technique:
QEMU monitor `sendkey`, `pmemsave` of the 80x25 VGA text buffer at
`0xB8000`, and serial mirroring, with a cold boot per case. The important
limit: the same memory scrape does not capture the graphical Win98 desktop.

Reusable rule: use `pmemsave 0xb8000 4000` only for DOS VGA text mode. For
Win98 GUI state use QEMU `screendump` plus image comparison/OCR, and prefer
serial or debugcon for machine-readable results. Never wait for text in a
VGA buffer that the active display mode does not use.

## Plain `lib /def:` silently creates the wrong usbport ABI

The private usbport exports use stdcall implementations exported by
undecorated names. MSVC 6 `lib /def:` over a plain-name DEF produced
cdecl-style `_Name` archive symbols, while correctly declared `NTAPI`
callers reference `_Name@N`. Changing the prototypes to cdecl would make the
link succeed by creating a runtime stack-corruption bug.

Reusable rule: generate `usbport.lib` through a stub DLL containing the real
stdcall signatures plus a plain-name DEF, then verify with `dumpbin
/linkermember:1` that the archive contains `_USBPORT_GetHciMn@0` and
`_USBPORT_RegisterUSBPortDriver@12` and names `USBPORT.SYS`. Rebuild and
verify against the binary actually installed in the VM, not merely the
package copy.

## The exact target binary outranks an open reimplementation

ReactOS is the best readable source for the private usbport miniport
contract, but it describes its own XP-era implementation. NUSB ships a
Win2000-SP4-lineage binary whose registration-packet tail, accepted
versions, exports, and call behavior may differ. Earlier from-memory
documentation already got version families and callback details wrong; it
was replaced with a source-transcribed ABI while preserving
binary-validation TODOs.

Reusable trust order:

1. The installed NUSB `usbport.sys` binary and observed calls.
2. The pinned ReactOS source for readable contract reconstruction.
3. This repository's transcription.
4. Memory or analogy with another Windows generation.

Keep ABI structs, callbacks, and services isolated from hardware logic so
the Phase 3 spike can correct offsets without rewriting the controller.
Remember that nearly all callbacks are stdcall, but `UsbPortDbgPrint` is
cdecl varargs; one calling-convention error is enough to corrupt the stack.

## Read installer INFs before describing what a package changes

The project initially described NUSB 3.3 as avoiding core-file replacement.
Direct inspection of `_NUSB.INF` proved that a stock Win98 SE installation
receives newer Microsoft QFE builds of `explorer.exe`, `user.exe`,
`user32.dll`, `systray.exe`, `ios.vxd`, and `hotplug.dll`. The same
inspection showed the working EHCI installation model: the miniport is the
bound device driver (`DevLoader=*NTKERN` and `NTMPDriver=usbehci.sys`),
while `usbport.sys` loads as its import dependency rather than as a
separately registered service.

Reusable rules:

- Inspect `DefaultInstall`, `CopyFiles`, `DestinationDirs`, copy flags, and
  versions before making "stack-only" or rollback claims.
- Snapshot before NUSB installation even when using 3.3.
- Model `xhci98.inf` on the verified NUSB EHCI device and INF, not on an NT5
  service-install intuition.
- Record the installed `usbport.sys` hash/version; package contents are
  useful preparation but are not the final target ABI record.

## Old Microsoft toolchains are often portable but layout-sensitive

Phase 1 reached a checked Toaster build on Windows 11 without either legacy
GUI installer. MSVC 6 ran from its extracted tree only after
`Common\MSDev98\Bin`, containing `MSPDB60.DLL`, joined `VC98\BIN` on PATH.
The Win2000 DDK self-extractor was a Wextract cabinet whose component
INF/CAB files could be installed mechanically. Several plausible assumptions
were wrong: Toaster lived under `<DDK>\src\general`, the DDK's `mofcomp.exe`
failed on modern Windows, and passing a quoted path into `setenv.bat`
propagated literal quotes into every derived path.

Reusable rule: inspect archive metadata and component INFs before automating
a legacy installer. Validate the smallest real checkpoint (`toaster.sys`)
rather than treating environment-variable setup as success. Use the host OS
`mofcomp.exe` for the BMF step and keep batch arguments unquoted when the
called script performs its own path composition.

## Usbport enumeration semantics do not map directly to xHCI

Review found a mismatch that matters: usbport sends SET_ADDRESS as an
ordinary EP0 control transfer, while xHCI forbids placing SET_ADDRESS on a
transfer ring and performs it through Address Device. The miniport must
intercept that setup request and maintain a usbport-address-to-Slot-ID
mapping. It must also avoid tying slot teardown to EP0 close, because
usbport closes and reopens EP0 during enumeration.

Reusable rule: do not assume a reused USB2 stack's abstract operation has a
one-to-one xHCI TRB equivalent. Trace the actual usbport callback sequence,
translate SET_ADDRESS into Address Device, and keep the two address spaces
logically separate even when their numeric values happen to coincide in a
single-device test.

## A visible PCI interrupt pin is a Win98 prerequisite

xHCI permits MSI/MSI-X-only controllers, but Win98 and the Win2000-derived
usbport stack have no usable MSI allocation path. Research established that
legacy INTx is therefore a hard platform gate, not an optional fallback.

Reusable rule: check PCI Interrupt Pin before binding. Pin zero means the
controller is unsupported for this Win98 design regardless of otherwise
valid xHCI capabilities. Do not attempt to program MSI behind the OS's
back; the kernel/HAL must allocate the vector and APIC message, and Win9x
has no such infrastructure.
