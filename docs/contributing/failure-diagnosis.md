# Failure Diagnosis Guide (symptom -> ordered causes -> discriminating test)

The other docs say what to build and which invariants to keep. This one
captures the diagnostic reasoning for when a phase checkpoint fails: for each
symptom, the causes in descending order of likelihood, and the cheapest test
that discriminates between them. The dominant failure mode in this project is
silence rather than a crash (no callback, no interrupt, no event). Silence
cannot be debugged by staring at code; it has to be localized with
instrumentation and differentials.

Nothing here introduces new register or ABI facts. Bit positions and completion
codes stay in `docs/usb-xhci-info/xhci-data-structures.md`; ABI facts stay in
`docs/usb-xhci-info/usbport-miniport-interface.md`. When this guide names a completion code
or field, look up its exact value there. Do not take numbers from memory, and
do not take them from this file.

---

## Trust order when evidence conflicts

When observed behavior contradicts documentation, re-verify in this order
(most trustworthy first):

1. The spec PDF (`docs/references/`) and the actual binary facts
   (`dumpbin` output of the NUSB-installed `usbport.sys`, PCI config dumps
   from the real machine).
2. ReactOS source (for usbport behavior) / Linux source (for xHCI hardware
   behavior), with Haiku and FreeBSD as second opinions where Linux is
   ambiguous.
3. This repository's docs - they were transcribed carefully, but a
   transcription error is more likely than the spec being wrong.
4. Anyone's memory, including an AI assistant's. Never resolve a conflict by
   recall.

If a repo doc turns out wrong, fix the doc in the same change that fixes the
code, and note what the observed behavior was.

## The four differential axes

Almost every "is it my bug or the environment?" question is answered by moving
one variable along one of these axes:

| Axis | What it isolates |
|---|---|
| Win98+NUSB vs Win2000 SP4 (Phase 2b VM) | NUSB's back-ported `usbport.sys` vs the miniport itself. Fails on both = miniport bug is likely. Fails only on Win98 = NUSB adaptation, INF, or NTKERN/HAL export/emulation gap. Fails only on Win2000 = first test for a correctness bug Win98 hides (preemption, IRQL, lock ordering), then distinguish native-usbport/WDM differences; do not declare either cause from this axis alone. Run Driver Verifier here (`docs/contributing/build-and-test.md`). The Standard-PC 2b VM is uniprocessor and cannot expose cross-CPU races; that is the next axis. Both sides of this axis are shipping targets, so unlike the axes below, "fails on one side only" is never an acceptable resting place: it localizes the bug, it does not excuse it. |
| Uniprocessor vs SMP Win2000 (Phase 2b VM vs Phase 2d VM) | Cross-CPU races a UP kernel structurally hides: on UP, spinlock acquisition only raises IRQL and DISPATCH_LEVEL paths never overlap. Fails only on the 2d SMP VM = suspect the miniport's interior locking first: ring enqueue/dequeue state shared between the submit path (`MiniportSpinLock`) and `InterruptDpc` (`MiniportInterruptsSpinLock`), or an unsynchronized async-timer callback (`docs/usb-xhci-info/usbport-miniport-abi.md` section 7). Same OS build and native usbport on both sides, so this axis moves only the kernel/HAL/CPU-count variable. The 2d VM exists and passed its checkpoint, so this axis is available; a static review of every lock site against abi section 7 remains a separate required check, since Win2000 Verifier has no Deadlock Detection. |
| QEMU vs real hardware | Controller quirks, BIOS handoff, IRQ routing, XUSB2PR. QEMU has no BIOS ownership contention, no EHCI pairing, and forgiving timing; passing in QEMU proves logic, not hardware compatibility. |
| Interrupt-driven vs polled | "Controller works but interrupts don't arrive" vs "controller is dead". See the poll-mode fallback below; this is the single most valuable instrument to build early. |

Build a poll-mode fallback into the driver: a compile-time flag that drains
the event ring from a periodic timer DPC instead of (or in addition to) the
ISR path. It costs little, and on real hardware it instantly separates
IRQ-routing failures from everything else, the same differential Phase 0's
DOS tool runs, but available in-situ for the lifetime of the driver.

## Instrumentation ladder (cheapest first)

1. `DbgPrint` in a `qemu` build, the only flavour with per-line trace sites -
   the published `debug` flavour has none (see
   `docs/contributing/build-and-test.md`, "Per-line trace output, and which
   flavour has it").
2. QEMU debug console, port 0xE9. Works even when the display is dead
   (`docs/contributing/build-and-test.md`).
3. QEMU xHCI trace events (`-trace "usb_xhci_*"`). The emulated controller
   logs every register write, TRB fetch, doorbell, and IRQ assertion on the
   host side, with zero guest cooperation. The first tool to reach for when
   the question is "did the hardware see what the driver thinks it did"
   (`docs/contributing/build-and-test.md` "QEMU xHCI trace events").
4. QEMU GDB stub (`-s -S`). Real breakpoints in either QEMU guest, with
   no guest-side debugger setup; use it before theorizing about a crash
   address. It lacks automatic kernel awareness and requires manual symbol
   setup, but costs little to reach for.
5. A serial kernel debugger is not a rung. On Win2000 WinDbg/KD would give
   symbols and kernel awareness (`docs/usb-xhci-info/win98-wdm.md`, "What
   Win2000 gives back"), and on Win98 the 9x DDK's WDEB386.EXE provides an
   assembly-level serial debugger, but no fleet machine has an RS-232 port,
   and a machine new enough to be xHCI-only is unlikely to have one, so this
   would need a dock or an add-in card before anything else. The project
   decided not to plan around that (`docs/contributing/build-and-test.md`,
   "Getting a trace off a bare-metal machine"). Inside QEMU the GDB stub
   above is better and needs no guest-side setup. (QEMU's virtual COM1 is
   still the Phase 0 DOS qualifier's output channel under `--serial`, and
   `xhciqual\test\run-qemu-matrix.ps1` reads its results from it.)

   On bare metal the trace ladder therefore ends at the rungs below: roadmap task
   12.2 closed with no Windows 98 bare-metal trace channel, and that is a
   settled answer rather than a gap somebody is still working on. On Windows
   2000 metal DebugView remains the trace channel.
6. The snapshot read, the one rung that works on Windows 98 bare metal.
   The driver's counters and its note ring sit inside `XHCI_EXTENSION`, and
   `XHCISNAP.EXE` reads them out of the running machine from ring 3, through
   usbport's PassThru escape, so nothing is pushed, nothing is printed, and
   no PASSIVE moment has to be found. It ships in every flavour from
   `0.0.0.6`, is off until `XhciLogVerbosity` is set and the machine
   restarted, and works the same on both targets.

   Keep the distinction clear: what task 12.2 closed is the trace, a running record of what
   happened as it happens plus a capture of a crash, and neither exists on
   that target. What this rung gives is the driver's state now, on demand,
   from a machine that is still running. See
   `docs/contributing/passthru-snapshot-instrument.md` for the route and
   `docs/contributing/design/08-build-flavours-and-the-log-channel.md` section
   13 for why it is shaped this way.
7. Poll-mode fallback flag (above), the only rung that also works on real
   hardware without a debugger.
8. Sentinel-fill: before calling into an undocumented interface (the
   registration packet above all), fill unknown/uncertain fields with
   distinctive sentinels (e.g. incrementing recognizable constants). A crash
   or misbehavior that surfaces a sentinel value tells you which field
   usbport consumed at the wrong offset.

## Reading a trace without fooling yourself

Each of the rules below has already produced a wrong conclusion in this
project. They apply to every phase.

An absent line is not evidence until you have an anchor. A trace site inside
a conditional prints nothing on the path that skips it, which is
indistinguishable from the enclosing code never having run. Pair the line you
care about with one that is printed unconditionally by a caller you know runs,
and read the pair.

The worked example is the one that bites at Phase 5/6: the predicted value of
`RhPortsDriventoU0` is zero. `xhciRhDriveSuspendedPortsToU0` returns early when
it signalled no port, before the pass's own completion line. That early return
prints its own line (a counter whose expected value is zero and whose only
trace site is past an early return proves nothing when it is right), so
absence means the pass never ran. The two lines are `root hub: no port needed
driving out of U3, total` and `root hub: ports driven out of U3 after the D0
transition`, both in `xhciRhDriveSuspendedPortsToU0` (`src/xhci_rh.c`). The
anchor is `XhciRootHubInit`, which calls the pass and then prints four lines
unconditionally. Line numbers are not given: they drift, and every one of
these labels is unique in the tree, so `grep` finds them.

| `root hub: managed ports` | `...no port needed...` | `...driven out of U3...` | Meaning |
|---|---|---|---|
| present | present, `= 0` | absent | The predicted pass: the pass ran, no port was in U3/Resume, counter is 0 |
| present | absent | present, `= 0` | Ports were signalled but none completed in the 20 ms; an anomaly, not the prediction |
| present | absent | present, `= N` | Prediction refuted: N ports survived in U3 across the resume |
| present | absent | absent | The pass never ran at all |
| absent | absent | absent | The root hub never rebuilt; look for `root hub: refusing, build status` |

Note the spelling: `RhPortsDriventoU0`, lowercase `t` in "to", declared in
`src/xhci.h`. `RhPortsDrivenToU0` matches nothing in `src/`. All these lines are
`XHCI_DBG_*`, so `qemu` build only: diagnose from `out\pkg-qemu-x86\`
(`out\pkg-qemu-amd64\` for the 64-bit build).
`src\sources` defines `XHCI_DBG_LIVE` for that flavour and for nothing else,
and without that define each macro compiles to nothing. The shipping `debug`
build keeps the DDK's `DBG` (which here is `/Oy-` and `VS_FF_DEBUG`, not
unoptimised code; both flavours are `/Oxs`) and the log ring, not the
per-line trace.

There is a fifth row's worth of state, and it is the one a bare-metal restore
will produce: the pass is skipped outright after a successful CSS/CRS restore,
because there is no HCRST on that path and the U3 ports it would find are the
ones usbhub itself suspended. That case prints `root
hub: restore succeeded, so U3 ports are left as the suspend left them` and
counts `RhU3PassSkippedAfterRestore`, so it reads as neither of the two
silences above. On every vehicle that exists today the restore fails and the
reinit path runs the pass as before.

The general lesson is worth more than the table: adding the anchor line an
"absent line" diagnosis asks for invalidates that diagnosis. When a trace site
is added to close a silence, grep the diagnosis tables for the silence it used
to mean.

Let a chardev log settle before concluding a callback did not fire. The
debugcon file is written by QEMU, not by the guest, and the last writes land
after the guest has stopped executing. A log read immediately after the VM
disappeared showed 7064 bytes and no `cb SuspendController`; it settled seconds
later at 7541 with it. "Absent from the log" and "absent from the run" are
different claims.

Sample EIP before naming a hang. An interrupt storm and a dead machine look
identical on screen and are opposite in the registers: EIP cycling a small
window with a device ISR above it and interrupts off, versus EIP pinned on
`sti; nop; nop; cli` with interrupts on. Both said "Setup is starting Windows
2000". Use `info registers` over the guest's monitor socket; `HLT=1` on a repeated
sample is a healthy idle guest, not a hang. `scripts\vm-matrix\lib\monitor.ps1`
is the tracked way to open that socket and send a command (any per-host
wrapper under `scripts\local\` is one host's file, not a committed procedure).

A healthy trace is not a living guest. Screendump before concluding anything
from the log alone; this project has paid for it twice.

A silent trace is not a dead guest. The inverse also bit (batch 6-V):
`XHCI_DBG_VALUE_LIMITED` caps at 32 prints per site, so a refusal loop spent its
budget in seconds and vanished from the log while the machine kept spinning.
The discriminator is a screendump in either direction.

A climbing `transfers refused for retry` against a frozen `transfers
submitted` is a livelock, not a busy bus. A refusal usbport is told to retry is
unbounded, so a refusal that can never stop being true (a released record, a
failed or undecodable controller) hangs the thread that issued the URB; on 2b
it left Device Manager half-painted. Those cases now complete with an error and
count `TransfersFailedGone`; a suspended or reinitialising controller, and the
REMOVE case, still refuse for retry, and that counter split is what names the
looping gate if the shape recurs.

`RhSpeedsSeen` is "what has this driver ever decoded", not what is attached.
It is a sticky set of the speed classes decoded since the last
`StartController`: an unplug does not retract a bit, and neither does a
suspend/resume cycle. Every connected root port is reported to usbport as
High Speed (`0x0503`), so the reported port status does not distinguish
the speeds (a run that reads `0x0103` for a Full-Speed device has an old
binary). The decode is read from the `RH first decode of a speed` line
and the set behind it instead.

---

## Phase 3 - usbport never calls the miniport

Symptom: driver installed, but no lifecycle callback ever logs.

1. Driver never loaded at all. Test: does the `DriverEntry` DbgPrint
   appear? If not, the code never ran. Check the import table before the
   INF: any unresolved NTOSKRNL/HAL/USBPORT module-symbol import makes the
   driver fail to load with no call-site diagnostic, looking identical to a
   bad INF (`docs/usb-xhci-info/win98-wdm.md`,
   "Imports are a silent load-time gate"). The post-link gate in
   `docs/contributing/build-and-test.md` should have caught this at build time, but it
   proves resolution only for Win2000, so on Win98 run the bisect below before
   suspecting anything else. If the imports are clean, the problem is
   INF/registry: diff the device's registry keys byte-for-byte against the
   installed NUSB EHCI device (`docs/contributing/build-and-test.md`
   "INF-Based Installation").
2. `DriverEntry` runs, `USBPORT_RegisterUSBPortDriver` rejects the packet.
   Test: log the returned status. Most likely a version/size field mismatch;
   try each candidate version constant found in ReactOS before concluding the
   ABI is unmatchable.
3. Registration succeeds, callbacks never fire. usbport is reading
   callback pointers at different offsets than the packet was filled at.
   Test: sentinel-fill (ladder rung 8); also re-check the packet layout
   against the disassembly of the NUSB binary, not only ReactOS headers.
   ReactOS documents its own build, which may differ.
4. Crash inside usbport during or after registration. Same root cause as
   (3): a misplaced field consumed as a pointer. The faulting address often
   is the sentinel, and that identifies the field.

Differential: run the identical binary on the Win2000 SP4 VM before touching
the packet layout. It decides whether the mismatch is NUSB-specific.

Declare Option B only per the exit criteria in
`docs/usb-xhci-info/usbport-miniport-interface.md` section 7, not after the first crash.

### Is a silent Win98 load failure an import problem? The release/qemu bisect

This procedure bisects a load failure by comparing a binary that carries the
extra trace imports against one that does not. `release` and `debug` import
the same set: what separates them is `/Oy-` (both compile `/Oxs`, and there
are no asserts), `VS_FF_DEBUG`, the snapshot header's `DEBUG` bit and the
`XHCI98_FLAVOUR_*` marker, so a release-versus-debug comparison discriminates
nothing where imports are concerned. The flavour carrying the extra imports is
`qemu`: `HAL.dll!WRITE_PORT_UCHAR`, plus the call sites for the two symbols
both published binaries already import. `0.0.0.4`'s debug build was the one
that would not load, and the flavour split is what moved that import out of
the published binaries.

The build-time gate settles Win2000 (it resolves against real export tables)
but can only gather evidence for Win98, whose export tables are built at init
inside `ntkern.vxd` (`docs/contributing/build-and-test.md`, "Post-link
import-compatibility gate"). So on a Win98-only silent load failure, the
loader itself is the oracle. Bisect with builds that already exist, in this
order, and do not start by writing a symbol checker:

1. Install the release binary and prove that it starts. Confirm the target has
   the recorded NUSB `usbport.sys` build (or that NUSB's own `usbehci.sys`
   loads against it), then use Device Manager/root-hub state to distinguish a
   successful start from silence; the release build has no trace channel,
   so "no debug line" proves nothing. Release starts, qemu does not: the
   failure is in the qemu-only import set.
2. There is no finer import bisect inside the trace channel. `XHCI_DBG_NO_IRQL`
   does not drop `HAL.DLL!KeGetCurrentIrql`: the flush guard (`xhciLogAtPassive`,
   `src/xhci_dispatch.c`) calls that symbol in every flavour and the
   allowlist carries it as `all required`. All the define still does is put
   `0xFF` in a trace line's IRQL field, which no published binary prints. A
   target that rejects that HAL symbol therefore cannot be bisected around, and
   would have to be answered by removing the guard, which is a design change
   rather than a build flag. Dropping `HAL.DLL!WRITE_PORT_UCHAR` is what
   building `debug` instead of `qemu` means; no published binary has it at all.
3. Both flavours fail: only qemu-only imports are ruled out. Compare
   both linked import tables. Any shared import remains a candidate: the two
   USBPORT names and the module/dependencies that provide them, plus every
   service used by production code. Verify the exact target `usbport.sys`
   identity/exports, then probe any shared NTOSKRNL/HAL pair that has no Win98
   precedent before moving to INF/registry/hardware ID.

Use release-versus-qemu only to isolate that flavour's trace imports. New
feature imports normally appear in all flavours. Keep those changes in small
groups, compare the two linked import tables, and use the gate's evidence
report to identify the unproven shared pairs. NUSB precedent for common
MMIO/stall services (`READ`/`WRITE_REGISTER_ULONG` from `NTOSKRNL.EXE`,
`KeStallExecutionProcessor` from `HAL.DLL`) is strong evidence, not a reason
to remove shared imports from the suspect set.

Build a target-side symbol checker only on one of the triggers below, and
prefer a single-symbol probe driver (our own source, same INF, importing the
one candidate and logging from `DriverEntry`) over porting Oney's
`_PELDR_GetProcAddress`-walking WDMCHECK, which is only worth it for testing
many symbols in one pass:

- a Win98-only silent load failure where both flavours fail and a shared import
  remains unproven after checking the exact USBPORT/kernel/HAL evidence;
- wanting a symbol with neither precedent nor an `ntkern.vxd` name hit
  (most likely if timing or work-item-shaped services are needed),
  especially more than one at a time; or
- seriously considering the `WDMSTUB`-style export-table extender
  (`docs/usb-xhci-info/win98-wdm.md`), which needs to know exactly which names are missing.

## Phase 4 - controller initialized but dead (no events, ever)

This is the "perfectly initialized but silent" matrix. Work it top to bottom;
each row's test requires nothing from the rows below it.

| # | Cause | Discriminating test |
|---|---|---|
| 1 | MMIO reads return all-ones | Read HCIVERSION first; 0xFFFF/0xFFFFFFFF means the device is not decoding (Memory Space Enable clear, device in D3, BAR unmapped). Abort: all-ones is never real data. |
| 2 | BIOS handoff never completed (real hw only) | Read USBLEGSUP: BIOS-Owned still set. QEMU cannot reproduce this. |
| 3 | Bus-master DMA off | Ring a No-Op Command, then read the event ring memory directly (ignore interrupts). No completion TRB in memory => check PCI Command register Bus Master Enable; do not assume the OS set it. |
| 4 | Ring programming wrong (DMA works, no completion) | CRCR written with a virtual address instead of physical, or wrong initial Ring Cycle State. Verify every address written to CRCR/DCBAAP/ERSTBA came from the common-buffer physical side. |
| 5 | Events reach memory, ISR never fires | This is the poll-vs-interrupt differential: events visible in ring memory but no interrupt = IRQ delivery problem (Interrupt Pin = 0? routing? see `docs/contributing/implementation-invariants.md` "Interrupt Delivery"), not a driver-logic problem. |
| 6 | ISR fires once, never again | ERDP written back without setting EHB, or USBSTS.EINT / IMAN.IP not acknowledged; the interrupter stays latched busy. |
| 7 | Ports powered but no connect events (Intel 7/8/9-series PCH or C610/X99, real hw) | XUSB2PR still routes the USB2 ports to EHCI (`docs/usb-xhci-info/xhci-programming.md`, the `XUSB2PR` section). Invisible in QEMU. **Since `2.1.1.0` the driver routes them itself** on Intel 1E31, 8C31, 9C31, 8CB1, 9CB1 and 8D31 (roadmap-hcd task 34.3, design record 16), so check the switchover first, from an `XHCISNAP` report with `XhciLogVerbosity` at 2 or above: `psw.gate` 0 means the controller is not on the list (an Intel part with the mux that the list misses lands here, and the switchover never ran unless `psw.mode` reads 2: since `2.2.0.0` an Intel controller's report carries `psw.mode`, the setting in effect - 0 off, 1 on a listed part, 2 an unlisted Intel part taken past the list by `XhciIntelPortSwitch` 2, at the user's own risk (design record 16 section 4a) - so an unlisted part with the mux is answered by 2 rather than by a new release), and `psw.pci.unread` that the gate could not read the id; `psw.value.found` 1 with `psw.value` 0 means `XhciIntelPortSwitch` turned it off (`psw.value.found` 0 means no valid DWORD was read - absent, unreadable, or of the wrong type or length - and the switch is then on, with `psw.value` 0 only a placeholder); `psw.board.unread` or `psw.board.refused` means the subsystem id stopped it; a `psw.route.step` other than 0 names the step that was refused; and step 0 with a read-back (`psw.usb3pssen`, `psw.xusb2pr`) unequal to its mask (`psw.usb3prm`, `psw.xusb2prm`) means a write was accepted and did not take. Since `2.2.0.0` `psw.route.written` follows each route and `psw.release.written` precedes each release, each a set of the registers written (bit 0 `USB3_PSSEN`, bit 1 `XUSB2PR`): a release gives back only what a route of the same started lifetime wrote, so an empty `psw.release.written` after a refused route is correct, not a fault. With step 0 and each read-back equal to its mask the routing is not the cause; a connector outside the masks stays on EHCI by design. Before the driver, or under a release up to `2.1.0.0`, confirm with xhciqual test C7 (`2XPOLL` or later; `1PROBE` does not print it) or the firmware's own xHCI-mode setting, per the `XUSB2PR` run sheet in `xhciqual/hardware-testing.md`. Observed once: on a Lenovo B490 (1E31, Windows 98 SE, firmware on Auto) `XHCIQUAL` read `XUSB2PR` and `USB3_PSSEN` at 0 with the masks nonzero before Windows, and under the driver the route ran at step 0 with each read-back equal to its mask (`docs/contributing/runs/run-34.md`, 34.3-H2; the values themselves were not transcribed). The rest of the predicted signature (C1-C4 PASS, ports powered and no connect event at all under a driver that does not route) is read off the Intel datasheet and Linux's `usb_enable_intel_xhci_ports()`, not off silicon. 100-series and later Intel and all modern AMD have no such mux, so on those the symptom means something else. |

A release build gives two readings before any of the rows above.
`XHCI_EXTENSION.InitStep` and `.InitStatus` record the failing step and its
refusal code, because a release build has no trace channel and most refusal
codes are shared between steps (`XHCI_HC_WINDOW_TOO_SMALL` alone does not say
which window). Read them out of the extension (`XHCISNAP`, or the monitor-side
counter reader) before reasoning from the symptom. On a shared interrupt
line, also read `InterruptCount` (ISR entries) against `InterruptsClaimed` (the
entries that were ours). "Our interrupt never routes here" and "it routes but
the controller has never had anything to report" are different diagnoses, row
5 against rows 1-4, and one counter cannot tell them apart.

### Cycle-bit bug taxonomy

Ring/cycle bugs have distinctive signatures. Recognize them by shape:

- Works for exactly one ring's worth, then stops (e.g. exactly 64 commands
  or 256 events) => producer or consumer cycle state not toggled at wrap.
- First command never completes => initial cycle state disagrees with what
  was programmed into CRCR (RCS) or with the ring's zero-fill.
- Stale or duplicate events processed; "impossible" TRB types => consumer
  reads TRB fields before checking the cycle bit, or CCS out of sync.
- Hardware starts a half-written TD => the first TRB's cycle bit was
  written before the rest of the chain (see `docs/usb-xhci-info/xhci-programming.md`
  "Transfer Ring Operation" step 3: the first TRB's cycle bit goes in last).
- Random corruption elsewhere in memory (often real hw only) => scratchpad
  buffers not allocated despite HCSPARAMS2 Max Scratchpad > 0; the controller
  writes into whatever the stale DCBAA[0] points at. Also check for TRB data
  buffers spanning a 64 KB physical boundary.

## Phases 5/6 - root hub up, enumeration fails

1. Reset requested, device never addressed. Test: log the port-reset
   callback path. Was PRC seen, and what speed was reported back? A wrong
   speed encoding (see `docs/usb-xhci-info/xhci-programming.md` "Speed Encoding") makes
   usbport compute the wrong EP0 max packet, which fails later and is
   misleading. Verify speed first, not last.

   **On the HCD, read the root port's own account first** (since `2.2.0.0`,
   roadmap-hcd task 35.3). With `XhciLogVerbosity` at 2 or above each root
   port's enumeration leaves `enum.port.` notes, which `XHCISNAP` decodes
   after the note ring ("root port enumeration notes, decoded"):
   `enum.port.look` says why the port was inspected and what PORTSC read,
   `enum.port.reset` the reset's result, `enum.port.speed` the raw speed ID,
   the class it decoded to and where that meaning came from (listed in the
   protocol's PSI table, the defaults of a protocol with none, or 35.1's
   fallback for an ID 4 to 7 a USB 3 table does not list), `enum.port.rate`
   the rate, `enum.port.slot` Enable Slot's completion and Slot ID,
   `enum.port.fail` a failed attempt's cause and whether a retry follows,
   `enum.port.end` the state the run left, and `enum.port.quiet` that the
   port's eight bursts are spent until it next enumerates. From
   `XhciLogVerbosity` 1, the snapshot's HCD region gives every root port's
   machine state, failure cause, retries, slot, last raw speed ID with its
   class and source, link warm resets and give-up, and the counter block,
   which before `2.2.0.0` lay outside the snapshot. Issue 11 is the case it
   was built for: a USB 3 port trained at U0 with speed ID 4, and no slot
   ever made, read under `2.1.1.0` only by a hand-built offset table and a
   static inference. With the notes, a failure of that kind reads
   `enum.port.speed` with class 0 and `enum.port.fail` with cause 7, the
   speed; on the E460 under `2.2.0.0` the port reads class 4 by the
   fallback and Enable Slot succeeds (issue 11 section 6). Ports behind
   a hub are not noted. `xhcisnap/README.md` has every layout.
2. Address Device completes with Parameter/Context State Error (codes in
   `docs/usb-xhci-info/xhci-data-structures.md` "Completion Codes"). The input context
   content is wrong. Checklist, in order of how often each is the culprit:
   Input Control Context A0/A1 flags; Root Hub Port Number (1-based); Context
   Entries count; EP0 Max Packet Size for the port speed; Route String
   nonzero for a root-attached device; context stride hardcoded to 32 while
   CSZ = 1.
3. Device unresponsive right after addressing. Suspect the SET_ADDRESS
   interception is not intercepting: a SET_ADDRESS setup packet went on the
   ring (forbidden by spec section 4.5.4.1; the xHC puts nothing on the bus and
   completes that TRB with TRB Error, code 5, so a TRB Error on EP0 right after
   reset is this bug's signature), or Address Device (BSR = 0) was never
   issued. Test: log bmRequestType/bRequest of every EP0 setup packet
   during enumeration; exactly one SET_ADDRESS must appear in the log and
   zero on the ring.
4. Wrong device responds / transfers hit the wrong device. The
   usbport-address -> Slot ID map is broken, typically because the two
   address spaces coincided during single-device testing and the map was
   short-circuited by equality. Test with two devices attached so the
   allocators diverge (`docs/contributing/implementation-invariants.md` "Device
   Addressing").
5. Transfer "succeeds" but data is garbage or short. Check, in order:
   the actual length, which is the sum of the TRB lengths of the TRBs the
   event completes, minus the event's residual (`XhciRingSumTrbLengths`), and
   which is fixed by any event that measured a byte count, success included;
   Data/Status stage direction bits; IDT set on a TRB whose buffer field was
   meant as a pointer; bounce-buffer copy direction (if bounce buffers are in
   play at all). Do not use `requested - residual`: that is a single-TRB-TD
   rule, and a control transfer is 2-3 TDs with a data stage that can span
   many TRBs, so on anything larger it silently reports the wrong number.
   The normative statements are
   `docs/contributing/implementation-invariants.md` ("Transfer Buffers") and
   `docs/usb-xhci-info/xhci-programming.md`.
6. First GET_DESCRIPTOR fails at Full-Speed. EP0 max packet guessed wrong;
   the fix is Evaluate Context with the max packet from the first 8 bytes of
   the device descriptor. This is normal protocol, not an error path.

## Phases 7/8 - class drivers and stability

- Interrupt endpoint opens but never delivers. Almost always the Interval
  field: xHCI encodes intervals as a power-of-two exponent of 125 us frames,
  which is not the raw USB2 bInterval (`docs/usb-xhci-info/xhci-data-structures.md`,
  Interval/DCI math). Second suspect: doorbell rung with the wrong DCI.
- Direct-attach works, behind-hub fails. Route String / TT fields are the
  first suspect. The design is `docs/contributing/design/02-hub-topology-route-string.md`,
  it is implemented, and single-TT and multi-TT trees with five working
  children each passed on the E460 (`run-13e.md`, stage E3). What remains
  unobserved is the numerical confirmation of the context fields and a
  Full-Speed hub under a multi-TT hub. QEMU's `usb-hub` cannot exercise the
  TT paths (it is Full-Speed only).
- Endpoint stalls once, then wedges forever. Reset Endpoint issued without
  the follow-up Set TR Dequeue Pointer; the endpoint stays stopped with a
  stale dequeue.
- Unplug during traffic crashes or leaks. Ordering on disconnect: stop
  endpoints, complete pending transfers as failed, then Disable Slot and
  free contexts/rings. Freeing ring memory the hardware might still DMA into
  is a delayed-corruption bug that looks unrelated when it fires.
- A new kernel service is needed and the Win98 driver stops loading. The
  import surface grew: run the bisect in "Is a silent Win98 load
  failure an import problem?" above, which also states the only conditions
  under which building a target-side symbol checker is the right move.
- Works for hours in QEMU, dies in minutes on hardware. Look the specific
  VID/DID up in Linux `drivers/usb/host/xhci-pci.c` before suspecting the common
  code. This project keeps no general quirk table of its own
  (`docs/usb-xhci-info/xhci-programming.md`); what it does key on a
  controller id is the Intel switchover's list (design record 16), the soft
  retry's exclusion of Linux's `XHCI_NO_SOFT_RETRY` parts and the interval
  cap on AMD (design record 17 sections 4.2 and 4.7), and `XHCIQUAL`'s
  `quirks.c` names the rest, report-only. Capture the Phase 0 tool's fact
  sheet for that machine if it exists.
- **A device stops until it is replugged, or a port goes quiet** (since
  `2.2.0.0`, design record 17). The bus now answers the faults that used to
  leave a device dead - a lost interrupt, a Transaction Error, a code
  nothing claims, a halt with no TD, a root port disabled or over-current, a
  controller halted or unreadable - each within a budget but the lost
  interrupt's backstop, which keeps delivering (at most one drain per 100 ms
  while an event stands) and has no terminal, so the symptom
  under `2.2.0.0` is either a recovery that worked (and is only counted) or
  a stated terminal state. Read, in this order, from an `XHCISNAP` report
  taken after the symptom (and one taken before it, to subtract):
  1. The values in effect, `XhciTolerance`, `XhciIntervalCap` and
     `XhciAvgTrbEsit`. At `XhciTolerance` 0 none of the behaviours acts
     but the two that apply at every value, the Incompatible Device Error
     teardown with its charged re-enumeration and the containment when DMA
     cannot be proven stopped (record 17 sections 4.3 and 4.6); the
     counts of what was seen - the completion codes, the queues' errors,
     refused codes and unmatched and foreign events, the halts, and the root
     ports' disables and over-currents (`PedFaults`, `OcFaults`) - still
     move, and the counts of what a behaviour did stay at 0 (record 17
     section 4.11, "As built"). A report at 0 therefore shows the faults the
     behaviours would have met; that is the differential, a restart apart.
  2. `BackstopDrains` nonzero: events were picked up by the thread, not the
     interrupt. Evidence of a lost or late interrupt, not proof; rising
     steadily, suspect interrupt delivery (Phase 4 row 5) before the device.
  3. `QueueErrors` and `QueueHalts` (the queues' counts summed over the
     controller; no per-endpoint figure is published), the soft retry's
     `RetryDiverts`, `RetryResets`, `RetryRecovered`, `RetryExhausted`,
     `RetryReplayed` and `RetryResetFailed`, and the completion-code
     histogram. Which device and endpoint an error came from is in the note
     ring's `xfer.error` records (`XhciLogVerbosity` 2), the first four of
     each code with the slot and DCI. `RetryRecovered` is a Transaction Error
     the retry absorbed; `RetryExhausted` is today's error completion, and
     what the class driver then sends is design record 13 section 6.7;
     `RetryResetFailed` sent the device to its teardown.
  4. `QueueBadCodes`, `QueueUnmatched` and `QueueForeign` with the device
     cycles by reason - `CyclesRefusedCode` and `CyclesHaltNoTd` (a refused
     code, a halt with no TD; record 17 section 4.3), `CyclesPrePdo` of
     them before the device's PDO - and `HaltReads` and `HaltStale`, the
     context reads a halt with no TD cost and those that found the endpoint
     neither Halted nor Error. A mark dropped because the device had left or
     its location's generation moved is `CyclesDropped`, and a cycle the
     spent budget refused, the device removed all the same, `CyclesRefused`.
  5. Each location's budget, charges and hold: a held port enumerates
     nothing until stable-disconnect evidence or, held unpowered after
     over-currents, a controller start. Root-port PED and over-current
     faults are counted separately from the cycles.
  6. The controller's recovery window and containment: a fourth recovery in
     ten minutes is not begun and the controller stays latched failed; a
     contained controller says whether Bus Master Enable read back clear
     (devices dropped) or the buffer was pinned.
  A HID device on an AMD controller that stops at random is the report the
  work came from: read the controller id behind the port (`XHCIQUAL`'s
  probe) first, and if it still stops with these at or near 0, compare with
  `XhciAvgTrbEsit` 1. QEMU raises none of these faults; only the `qemu`
  flavour's injection (record 17 section 5) makes them.

## When stuck for more than two sessions

- Re-run the narrowest passing configuration and walk the single-variable
  axes (see "The four differential axes") until a single change flips pass to fail.
- Reproduce the same topology under Linux on the same hardware (live USB) and
  compare dmesg/xhci tracing against the driver's logs. That separates
  "this hardware is quirky" from "this driver is wrong".
- Downgrade the goal: a checkpoint that cannot be observed to pass is the
  roadmap's signal to stop and re-derive, not to push forward
  (`docs/contributing/roadmap.md` rule 1).
