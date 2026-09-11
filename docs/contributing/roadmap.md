# Development Roadmap

This roadmap is the project-status index: the phase sequence, what each phase
was for and what it delivered, the basis on which each closed, the task and
batch ids that other documents, scripts and source comments cite, and the two
acts that sit outside the phases (the upload and the hand-run acceptance).
It is meant to orient a contributor. The detail lives in the other documents:

- Build, VMs, install, packaging and the bench rig:
  [`build-and-test.md`](build-and-test.md).
- Component boundaries and data flows: [`architecture.md`](architecture.md).
- Rules a code change must preserve:
  [`implementation-invariants.md`](implementation-invariants.md).
- Numbered design records: [`design/`](design/README.md).
- Measured behaviour, traps and refuted hypotheses: [`lessons.md`](lessons.md);
  the per-run evidence in the run sheets [`run-11v.md`](runs/run-11v.md),
  [`run-13e.md`](runs/run-13e.md) and [`run-20.md`](runs/run-20.md).
- What a user is told (what the driver does, does not, and its known limitations):
  [`../using/release-notes.md`](../using/release-notes.md).

Targets. Windows 98 SE and Windows 2000 SP4 are both first-class. One
`xhci98.sys` binary must install and work on either, and from Phase 3 onward a
phase's checkpoint is not met until it has been observed on both. Every
Windows 2000 observation in this repository is a virtual-machine observation:
Windows 2000 Setup bugchecks during installation on both physical machines the
project has, no cause was investigated, no bugcheck code was captured, and no
further candidate is available. `AGENTS.md` says what "observed on both"
therefore means, and the release notes state it under "What this is".

Integration model. `xhci98.sys` is a `usbport.sys` miniport ("Option A"). It
plugs in beneath Microsoft's port driver the way `usbehci.sys` does and reuses
the Win2000-derived USB 2.0 stack that ships in NUSB. The port driver owns the
root hub PDO, `IOCTL_INTERNAL_USB`, URB parsing and enumeration; the miniport
owns only the xHCI hardware. "Option B", a monolithic HCD that re-implements
the port driver's role, was the documented fallback and was never needed. USB
3.0 SuperSpeed is out of scope. The rationale is in
`docs/usb-xhci-info/win98-wdm.md` ("USB Stack Architecture and the Integration
Decision") and `architecture.md`.

Current status: Phases 0-20 are closed. `1.0.0.0`, `1.0.0.1`, `1.0.1.0` and
`1.0.2.0` are cut, and none has been uploaded; Phase 15 moved the
tree from revision 1.2 of the xHCI specification to revision 1.2c, the only
revision Intel now serves, without a code change; Phase 16, the fully
automated run on freshly installed guests of both targets, closed on
2026-08-30 on its second run, a clean Windows 2000 reading and a Windows 98
reading with one row against, the USB Audio replug, published as a
limitation. Phase 17, opened and closed on 2026-09-02, has the operating
system supply `usbd.sys` and `usbhub.sys` from its own install source, so the
package stops carrying any Microsoft file; no driver code changes. Phase 18,
closed the same day, is release `1.0.0.1`, that change together with Windows
ME support. Phase 19, opened on 2026-09-03 on branch `1.0.1.0`, is release
`1.0.1.0`: Windows XP support, and the fix for the two gaps the first XP
guest measured, an NT install that never had a USB controller has no
`usbport.sys` for this driver to import, and XP's `usbport` idle-suspends the
controller so a later hot-plug is invisible; both are INF changes that reach
Windows 2000 too. Phase 20, opened on 2026-09-05 on branch `phase-20` and
closed on 2026-09-07, is release `1.0.2.0`: the fixes for the 2026-09-05
repository audit (nineteen findings and six documentation groups), and the
operating system supplying `usbui.dll` as well, which brings back the USB
Root Hub's Power tab on the NT targets. Two acts sit outside the task list
and are the project owner's to take: uploading the asset, and running the
release acceptance test by hand on a fresh VM and on a physical machine.
The section this roadmap ends on is the reminder for the second, which runs
before the upload: settled at `1.0.2.0`, and the order every release follows
now.

Two phases are open, both about operating systems this driver does not yet
claim, and neither blocks the other or anything already closed. **Phase 21**
is the 64-bit question - one NT 5.2 amd64 binary, which needed a second
toolchain before it needed anything else. Its Windows XP x64 and Server 2003
x64 checkpoint passed on 2026-09-09; what is left is the record and a second
guest leg for **Vista x64 and Windows 7 x64**, where the obstacles are code
signing and the driver store rather than the ABI. **Phase 22** is the 32-bit
one - whether the binary that already ships
runs on Windows Vista and Windows 7 as it stands, which needs no build at
all. They share the Vista and Windows 7 media and nothing else, and the one
pass they share was taken on 2026-09-09: the static ABI read of both
operating systems in both architectures, which passes everywhere and leaves
each phase with its own guests and, for 21, its own toolchain.

---

## Batching Convention

A phase splits by scope and has one checkpoint. A batch splits by where the
work can be confirmed and has no checkpoint of its own. A VM boot or a bench
trip is the expensive unit, and most tasks do not need one, so from Phase 6
onward a phase whose tasks are confirmed in more than one place groups them
into batches. Phases 6, 7a, 7b, 8, 9, 11 and 13 are of this shape; Phases 0-5,
10, 12, 14, 15, 16, 17, 18, 19, 20, 21 and 22 have plain per-phase task
numbers.

Task ids are `<batch>.<n>` in a batched phase (`6-B.4` is the fourth task of
batch `6-B`) and plain `<phase>.<n>` otherwise (`12.3`, `14.1`). Phases 0-5
keep their original plain numbers, so "Phase 4 task 7" is still an exact
citation. A phase with exactly one batch uses plain ids, since a single batch's
letter would only repeat the phase name.

| Suffix | Meaning |
|---|---|
| `-0` | Static. Reading the shipping binaries or the specification and producing documentation, not driver code. First, because everything after it is written against what it finds. |
| `-A`, `-B`, ... | Host. Driver code confirmed by `test\run-host-tests.cmd` and the import gate. Ordered by dependency; each is one review and commit unit. |
| `-V` | VM. The clauses no host test can observe. |
| `-M` | Bare metal. No live batch carries it. The last was Phase 7b's `7b-M`, whose two clauses were carried into Phase 13's machine-named batches. |
| `-E`, `-H` | A machine, named by its initial; Phase 13's alone. `13-E` is the E460 bench; `13-H` the modern Windows host. |
| `-R`, `-L` | A subject; also Phase 13's. `13-R` is the Finding 3 repair; `13-L` the Windows 98 log channel. Each ran on the development host and then the E460. |

A task inserted between two existing ones takes a fractional id (`13-R.3.5`,
`14.0`) and nothing is renumbered: task ids are cited from `docs/`, `src/`,
`test/`, `scripts/` and evidence logs, and a published `readme.txt` is never
edited in place. A `.5` id means "between these two" and is not a sub-task. A
sub-task is `<task>.<n>`: `7b-A.1.2` is sub-task 2 of task `7b-A.1`, and
`14.1.1` to `14.1.11` are the clauses of task `14.1`. No tracked file cites a
pre-renumbering id; task 14.1.9 checks that.

Three rules the batching exists to enforce:

1. Derive before you write. A callback body written against an assumed IRQL
   cost a rewrite (Phase 4 task 7). A `-0` batch is cheap; the rewrite it
   prevents is not. A checkpoint that cannot be observed to pass is the signal
   to stop and re-derive, not to push forward.
2. A batch is a review unit. Most defects were found by reviewing one coherent
   slice; a batch that mixes subsystems dilutes that.
3. Spend a boot on what only a boot can show. Where a phase's instrumentation
   clause and its checkpoint can ride the same binary, they are one `-V` batch.

Two batches carried stop rules and were gates in batch clothing: `7b-V0` and
`9-0` could each have ended their phase in a recorded Option A limitation.
Neither did.

---

## Phase sequence

Phase 0 is an optional, independent DOS qualifier for real machines. Phase 1
is the host build check. Phase 2 stands up the QEMU estate (2a Win98 SE, 2b
Win2000 SP4, 2c the WHPX-capable QEMU binary, 2d a multiprocessor Win2000 VM).
Phase 3 is the go/no-go gate for the miniport architecture. Phases 4-9 build
the driver up one capability at a time (controller init, root hub, enumeration,
HID, hubs, bulk, isochronous). Phase 10 is the automated device matrix, Phase 11
the stress and packaging pass, Phase 12 the machine-free decisions, Phase 13 the
bare-metal validation and Phase 14 the `1.0.0.0` release. Phase 15, added
after the cut, moves the tree to revision 1.2c of the xHCI specification, and
Phase 16 is the unattended post-release run on freshly installed guests. Phase
17 has the OS supply `usbd.sys` and `usbhub.sys`, Phase 18 is release
`1.0.0.1` with Windows ME, and Phase 19 is release `1.0.1.0` with Windows XP
and the NT-side install fixes the XP guest found. Phase 20 is release
`1.0.2.0`, the 2026-09-05 audit worked through and cut. Phase 21, open, asks
whether this driver can be a miniport on 64-bit Windows at all, taking
Windows XP x64 and Server 2003 x64 - one target, both NT 5.2.3790 - as its
subject, and since 2026-09-09 asking the same of Vista x64 and Windows 7 x64
in task 21.8. Phase 22, open, asks whether the 32-bit binary that already
ships runs on Vista and Windows 7 as it stands, and **carries the `1.1.0.0`
cut** - the version Phase 21 bumped to and never published, and the first
release to carry a 64-bit package. Phase
14 waited on Phase 13's bench batches reporting. Accepting the published release, from the download on a
freshly installed VM and on a physical machine, is not a phase and has no
task: it is a hand-run procedure the project owner takes before the upload,
and the end of this file says so.

---

## Phase 0 - Hardware Qualification (optional, independent)

Goal: a standalone DOS tool, `xhciqual/XHCIQUAL.EXE`, that proves a candidate
machine's xHCI can support the driver before any driver effort is spent on it -
BIOS handoff, reset, bus-master DMA, legacy-8259 INTx delivery, port connect and
reset - with no OS installed, and records the hardware facts the quirk tables
need.

Status: closed. Checkpoint: on a candidate machine the tool reports PASS for
reset, DMA round-trip and interrupt delivery, handoff PASS or WARN, and prints
the USB2 topology and context size; Interrupt Pin 0 is an unconditional failure
because neither target has an MSI path. Met on both fleet machines (ThinkPad
E460, ThinkPad P14s Gen 1) with a `QUALIFIED` verdict, `xhciqual/results/`.
Never run on AMD xHCI, on Intel 7/8-series (`XUSB2PR` mux) silicon, or on an
OHCI CPU-ISR path, and no machine remains that could; the v0.10 C7 (Intel port
routing) branches have executed only under the host tests.

Rationale: the silent-death risks - handoff, DMA, INTx in PIC mode - are
hardware and firmware properties. Real-mode DOS with direct PCI, IVT and MMIO
access isolates them from `usbport.sys` and from either driver model. The
verdict qualifies a controller, not an OS install, and does not replace
Phase 13.

What shipped:

- `xhciqual/` (Open Watcom, DOS/32A stub): tests C1-C8, EHCI and OHCI in the
  same binary, `--probe-only`, `--poll-only`, `--irq-selftest`, `--no-page`,
  `--set-intel-ports`, `--log`; a DPMI-locked ISR; the PCI power-management
  block, subsystem ids and sticky status bits; dead-MMIO causes.
- The QEMU regression matrix, the Windows 98 batch runner and the host unit
  tests for `mmiodiag.c`.
- Bare-metal records with an `lspci -vv` cross-check, and the connector-to-port
  maps of both fleet machines.

Tasks: 1 discovery; 2 capability introspection; 3 BIOS-to-OS handoff;
4 halt and reset; 5 DMA round-trip; 6 interrupt delivery; 7 port connect and
reset; 8 report.

Records: `design/01-hardware-qualification-tool.md`; `xhciqual/README.md`;
`xhciqual/hardware-testing.md`; the `README.md` of each run under `xhciqual/results/`;
`build-and-test.md` "Available Test Hardware".

## Phase 1 - Development Environment

Goal: the Windows 11 host compiles a WDM driver with MSVC 6.0 and the Windows
2000 DDK.

Status: closed. `scripts\local\verify-ddk-toaster.cmd` built the DDK's
toaster sample without errors; re-verified after the toolchain moved into the
repository (`scripts\build-driver.cmd both` passes with neither `DDKROOT` nor
`MSVC6` set).

Rationale: both targets need a Win2000-DDK-built WDM miniport, and `cl.exe`
12.00.8804 is the compiler the DDK expects. Toolchains used in place under
`tools/` mean a clone builds anywhere; QEMU is the only host prerequisite.

What shipped: `scripts\setup-all.ps1`, `setup-msvc6.ps1`, `setup-w2kddk.ps1`,
`install-w2kddk-cabs.ps1`; `tools\MSVC600` and `tools\ntddk` found by relative
path with `DDKROOT`/`MSVC6` overrides.

Tasks: 1 toolchain inventory; 2 MSVC 6.0; 3 DDK; 4 verification build.

Records: `build-and-test.md` "Automated Phase 1 Host Setup", "Win2K DDK
Installation Notes", "Setting Up the Build Environment", "Building".

## Phase 2a - Win98 SE Test Environment

Goal: a Windows 98 SE VM in QEMU with `qemu-xhci`, PS/2 input, the NUSB 3.3
`usbport.sys` stack, a file-transfer path, and the xHCI device unclaimed - the
primary iteration environment.

Status: closed. All clauses observed; baseline snapshot `phase2a-usbd-ok`
on `vm\win98.img`.

Rationale: the integration risk - a back-ported `usbport.sys` accepting a
third-party miniport - lives here. NUSB 3.3 rather than 3.6 because 3.6
replaces more core files with WinMe-derived ones.

What shipped: `scripts\setup-qemu.ps1` and the `qemu-win98-*` launchers; the
`setup /p j` install recipe; the VVFAT transfer drive; the `usbd.sys` baseline
fix (`usbhub20.sys` imports it and NUSB does not ship it); the finding that
NUSB places the stack unconditionally, so no EHCI is needed.

Tasks: 1 QEMU and launchers; 2 install; 3 PS/2; 4 NUSB; 5 transfer path;
6 `usbd.sys`; 7 the unclaimed xHCI.

Records: `build-and-test.md` "Option B: QEMU", "Getting files into the guest",
"VM snapshots - iterate without fear", "Installing the usbport USB 2.0 Stack
(NUSB) - Win98 SE only", "Carrying a per-target `usbd.sys`";
`docs/usb-xhci-info/usbport-miniport-interface.md` "Target ABI record";
`lessons.md`.

## Phase 2b - Windows 2000 SP4 Target Environment

Goal: a Windows 2000 SP4 VM with `qemu-xhci` and the native `usbport.sys` -
the second first-class target and the NUSB differential.

Status: closed. All clauses observed; snapshot `phase2b-clean` on
`vm\win2k.img`.

Rationale: every phase from 3 must be observed on both targets. The native
`usbport.sys` (5.00.2195.6681) and NUSB's (5.00.2195.5652) are different builds
that share exports and the registration version gate, so one binary serves
both.

What shipped: `scripts\setup-qemu-win2k.ps1` and launchers; the Standard-PC
HAL configuration (`-machine pc,acpi=off -cpu pentium3,-apic`) that gets Setup
past its hang; the mandatory `USBD.SYS` preparation boot (without it
`usbhub20.sys` bugchecks about a file that is present); the ABI record of both
`usbport.sys` builds.

Tasks: 1 VM and install; 2 SP4 and `USBD.SYS` prep; 3 version comparison;
4 transfer path; 5 the unclaimed xHCI.

Records: `build-and-test.md` "Windows 2000 SP4 Target VM (second first-class
target; also the differential)", "Run Driver Verifier here (there is no Win98
equivalent)", "Recovering from a driver that crashes at boot";
`docs/usb-xhci-info/usbport-miniport-abi.md` "1. Exports and the registration
call"; `lessons.md`.

## Phase 2c - QEMU x86_64/WHPX Migration

Goal: move every harness and VM from the TCG-only `qemu-system-i386.exe` to the
WHPX-capable `qemu-system-x86_64.exe`, re-verify each under TCG with only the
binary changed, enable the host hypervisor, and trial WHPX per component.

Status: closed. The qualifier matrix passes on the x86_64 binary; 2a and 2b
re-verified with snapshots intact; every WHPX trial recorded. WHPX proved
optional for the qualifier and incompatible with both target VMs, so every
launcher stays on TCG. Prerequisite for Phase 2d, not for Phase 3.

Rationale: WHPX is compiled only into the x86_64 binary, and Phase 2d's
multiprocessor HAL needs the APIC that TCG storms on this host. One variable at
a time.

What shipped: the x86_64 binary in every setup script and harness, resolved by
`Get-Command` with a fallback; the host hypervisor platform enabled; the
per-component WHPX outcomes in `lessons.md`.

Tasks: 1 enable WHPX; 2 confirm the binary; 3 scripts; 4-6 re-verify the
qualifier, 2a and 2b; 7-9 WHPX trials of each; 10 record.

Records: `build-and-test.md` "Windows 2000 SMP Stress VM (Phase 2d)" steps 1-2
and its contrast table, "Option B: QEMU"; `lessons.md` (the migration and
WHPX-trial entries).

## Phase 2d - Windows 2000 SMP Stress Environment

Goal: a third VM - Windows 2000 SP4 with the multiprocessor HAL on two vCPUs
under `-accel whpx,kernel-irqchip=off` - the only environment able to expose
cross-CPU ISR/DPC races. Not a target; a rig.

Status: closed. Boots on the multiprocessor HAL with distinct host threads
per vCPU, the native USB stack loaded, the xHCI unclaimed, snapshot
`phase2d-clean`. The BLOCKED alternative was not needed. WHPX behaves
differently on every host tried, so `-accel whpx` is probed per host before it
goes in a launcher.

Rationale: on a uniprocessor kernel `KeAcquireSpinLock` only raises IRQL, so a
missing lock between submit and `InterruptDpc` is invisible on 2a and 2b. Real
Windows 2000 deployments are multiprocessor. The value is the second CPU, not
throughput.

What shipped: `scripts/setup-qemu-win2k-smp.ps1` (accelerator ladder,
`-AcpiOff`, `-Smp`), the `qemu-win2k-smp-*` launchers,
`scripts\check-smp-parallelism.ps1`, `scripts\test-qemu-launchers.ps1`; the
finding that the vector-0xD1 storm is absent on this configuration.

Tasks: 1 tooling; 2 install with the accelerator rungs; 3 `USBD.SYS` prep;
4 boot; 5 verify the SMP kernel; 6 the 2b checks and usbport version;
7 snapshot; 8 record.

Records: `build-and-test.md` "Windows 2000 SMP Stress VM (Phase 2d)";
`docs/usb-xhci-info/usbport-miniport-abi.md` "7. Locking, IRQL, and threading
summary"; `failure-diagnosis.md` "The four differential axes";
`design/05-locking-model.md`; `lessons.md`.

## Phase 3 - Miniport Registration Spike (go/no-go gate)

Goal: prove Option A. A stub `xhci98.sys` registers with the shipping
`usbport.sys` on both targets, receives its lifecycle callbacks, and a fixed
common-buffer layout is feasible for every controller-owned object that must
survive usbport's endpoint close and reopen. The spike performs no MMIO, so an
ABI failure can never be confused with a bad init sequence.

Status: closed; Option A is the architecture. Checkpoint observed on
Win98+NUSB (task 8) and native Win2000 SP4 (task 9) with a byte-identical
binary: the controller bound with no yellow bang, registration and the first
lifecycle callback sequence proven without canary or stack corruption. Two
sub-clauses (disable/re-enable, rollback) could not be observed on Win98 -
disabling any USB controller there bugchecks at `0028:C00312EE`, Microsoft's
own `usbehci.sys` included - and were closed on Win2000.

Rationale: `MiniPortResourcesSize` is committed in `DriverEntry` before any
register can be read, and usbport frees endpoint buffers mid-enumeration, so a
fixed layout was the make-or-break question; the registration packet is a
private ABI with no import library, so it had to be read off the binaries.

What shipped:

- The registration ABI confirmed on three lineages (NUSB, SP4, XP SP3): a
  316-byte packet at `Version 200`; `USBPORT_GetHciMn` returns `0x57324B30` on
  both primary targets.
- The fixed common-buffer model (`design/04-controller-common-buffer.md`): 32 slots, 64 scratchpad buffers,
  one worst-case block; usbport's DMA adapter confirmed 32-bit, cached and
  page-aligned.
- `scripts/make-usbport-lib.cmd` and `scripts/usbport-lib/` (one import
  library for all three lineages); `src/xhci_dispatch.c` `DriverEntry` with
  every callback slot filled and reserved-field canaries.
- The post-link import gate (`scripts/import-gate/`) and
  `scripts\build-driver.cmd` as the one build entry point.
- `src/xhci98.inf` with both install paths and the per-target `usbd.sys`
  carriage; the INF gate (`scripts/inf-gate/`); the packager
  (`scripts/package/`).
- The port-`0xE9` debug console in the QEMU launchers.
- The static proof that usbport builds page-granular SG lists through its
  32-bit adapter and stores the high DWORD unmasked.

Tasks: 1 derive and confirm the packet; 2 the DMA-memory model; 3 the import
library; 4 `DriverEntry` and the stubs; 5 the import gate; 6 the INF; 7 the
per-target `usbd.sys`; 8 the Win98 spike; 9 the Win2000 spike; 10 what the
spike can and cannot prove about transfer mapping.

Records: `docs/usb-xhci-info/usbport-miniport-interface.md` ("Target ABI
record", the two observed callback sequences, "6. Validation procedure");
`docs/usb-xhci-info/usbport-miniport-abi.md` sections 1, 3, 5, 9;
`design/04-controller-common-buffer.md`; `build-and-test.md` "Post-link
import-compatibility gate", "INF-Based Installation", "Carrying a per-target
`usbd.sys`", "QEMU Debug Console (port 0xE9)"; `docs/usb-xhci-info/win98-wdm.md`
"Go/no-go validation gate", "Imports are a silent load-time gate";
`lessons.md`.

## Phase 4 - Controller Initialization

Goal: the xHCI hardware fully initialised inside usbport's `StartController`;
the ISR/DPC path through usbport-owned interrupt plumbing; port topology
classified; the complete controller lifecycle (stop, suspend, resume, check,
reset) and the asynchronous command engine in place before any slot code
depends on them.

Status: closed. Checkpoint observed on 2a, 2b (under Driver Verifier) and
the 2d SMP VM: start, disable/enable, stop and restart with no crash or stale
MMIO access - with one qualification on the Windows 98 half, added later. A
Device Manager controller disable bugchecks Windows 98 (Phase 3 task 8, and
reproduced on Microsoft's own `usbehci.sys`; Phase 11 found the same through
every door), so whatever the 2a leg did here was not that. The record does
not name the route it used, so read the Windows 98 half as "a stop and a
restart by some route" rather than as evidence that a disable works
(`lessons.md`, "A later correction to how the Win98 half of the checkpoint
may be cited"). The rest of the line is unqualified: the No-Op command completing with the expected TRB pointer, Port
Status Change events on plug and unplug. `ResumeController` has never run on
Windows 2000 (no QEMU configuration delivers a sleep state; published as a
limitation by Phase 13). Task 10 - the `XUSB2PR` run on Intel 7/8-series
silicon - was withdrawn when the only such machine left the project;
`XUSB2PR` is published as untested ground.

Rationale: `StartController` plays the role `IRP_MN_START_DEVICE` would in a
monolithic driver, and each of the specification's ordering rules (INTx before
any MMIO, low-DWORD-first 64-bit writes, the CNR embargo, EHB/IP semantics, PP
after R/S) was read from the PDF rather than from memory. The host suite
(`design/03-host-unit-tests.md`) came first so that each VM boot spends the expensive resource on
a question only a VM can answer.

What shipped:

- `src/xhci_ring.c`, `xhci_caps.c`, `xhci_port.c` (pure core), `xhci_pci.c`,
  `xhci_init.c`, `xhci_evt.c`, `xhci_cmd.c`, `xhci_hw.h`.
- The init sequence: a write-nothing preflight (INTx gate first), BIOS handoff,
  halt and reset, full capability re-derivation after reset, the port map
  built twice and compared, DCBAA/rings/ERST, bounded R/S, explicit power-off
  of USB 3.x ports, the No-Op self-test; `InitStep`/`InitStatus` readable from
  a release build.
- ISR and DPC (EINT before IP, a bounded drain, the unconditional final ERDP
  write with EHB), `EnableInterrupts`/`DisableInterrupts`/`FlushInterrupts`,
  the re-arm with read-back and escalation.
- The command engine: one outstanding, generation-tagged, a watchdog, the
  recovery ladder (abort, Command Ring Stopped adoption, controller reset);
  the driver-image controller lock.
- The lifecycle: ordered teardown, a quiesce that proves DMA stopped,
  `CheckController` fault detection, fail-closed on an unprovable teardown.
- `design/05-locking-model.md`; the host suite grew to 4,806 checks; the
  deploy gates made mechanical in `build-driver.cmd` and `make-package.ps1`.

Tasks: 1 pure-core harness and first ring/caps/port code; 2 the start
sequence; 3 the extended-capability walk and port map; 4 ISR and DPC;
5 R/S, port power and the minimal quiesce; 6 the interrupt-state callbacks;
7 the asynchronous command engine and No-Op self-test; 8 the complete
lifecycle; 9 lock scope and lock order; 10 the `XUSB2PR` bare-metal run
(withdrawn).

Records: `design/03-host-unit-tests.md`; `design/05-locking-model.md`;
`implementation-invariants.md` (Command Ring, Locking, Interrupt Delivery and
Ordering, Event Ring Draining, MMIO Sanity, DMA Teardown, PORTSC Writes,
Starting and Stopping, Suspend and Resume); `docs/usb-xhci-info/xhci-programming.md`
("Initialization Sequence", "Port Topology Classification", "Command Ring
Discipline, Timeout, and Abort", "Event Ring Operation", "Firmware Handoff",
"`XUSB2PR`"); `docs/usb-xhci-info/usbport-miniport-abi.md` section 4
(controller lifecycle) and 7; `architecture.md` "IRQ and DPC Model";
`failure-diagnosis.md` "Phase 4 - controller initialized but dead";
`build-and-test.md` "Run Driver Verifier here", "QEMU xHCI trace events";
`lessons.md`.

## Phase 5 - Root Hub

Goal: usbport creates the root hub PDO and `usbhub20.sys` loads on it; the
miniport implements the whole root-hub callback family - status, power,
enable, suspend/resume, change clears, the IRQ gate, chirp, asynchronous reset
and resume - presenting only USB 2.0-class ports.

Status: closed. Checkpoint observed on 2a and 2b: "USB Root Hub" under the
controller with no yellow bang, the port count equal to the managed USB2
ports, asynchronous reset completed by PRC, both plug and unplug edges, change
bits latched and cleared through the matching callbacks, FS and HS devices
enumerating to a devnode. The first run was not met - any Full-Speed device
bugchecked both targets - and task 7 fixed it. Two clauses moved rather than
carried: the Low-Speed leg (no QEMU model declares LS; its behavioural half
was later observed on the E460, batch 13-E) and `ResumeController` on Windows
2000 (published as a limitation by Phase 13).

Rationale: under Option A usbport owns the PDO and hub descriptor; the
miniport's job is accurate, non-blocking port state. The Full-Speed bugcheck
was a usbport defect - `USBPORT_GetTt` walks an empty TT list because the
packet declares `USB_MINIPORT_FLAGS_USB2` - and the remedy chosen, reporting
every connected root port as High Speed while keeping the flag, preserves High
Speed on Windows 98 (dropping the flag would bind Win98's USB 1.1
`usbhub.sys`). The cost is that a device's true `bInterval` is irrecoverable
through usbport; the driver schedules more often than asked, never less.

What shipped: `src/xhci_rh.c` (the callback family, the PORTSC event path,
asynchronous port operations and their timer) and the pure port shadow and map
in `src/xhci_port.c`; named PORTSC operation builders with golden vectors;
asynchronous reset and USB 2.0 resume with opposite completion rules (a reset
timer is a deadline, a resume timer is a floor); the `UsbPortInvalidateRootHub`
announcement decided under the lock and called after it; the `RH_IRQ` gate
that suppresses without losing; the High-Speed report with the `RhSpeedsSeen`
sticky set as the surviving decode evidence; the root-hub disassembly record.

Tasks: 1 the callback family and the static pass on both binaries; 2 the
logical-port map and per-port shadow; 3 PORTSC operation builders; 4 asynchronous
reset and resume; 5 status-change announcement; 6 power and lifecycle
interactions; 7 the Full-Speed root-port bugcheck.

Records: `docs/usb-xhci-info/usbport-miniport-abi.md` (the root-hub block of
section 4, "8. Enumeration flow facts the miniport must survive" and its
transaction-translator subsection); `implementation-invariants.md` ("Port
Speed Decoding", "PORTSC Writes", "Root Hub Reporting");
`design/05-locking-model.md` "Port state and reset generations";
`design/02-hub-topology-route-string.md` open question 6;
`docs/usb-xhci-info/xhci-programming.md` "Port Management (PORTSC)";
`lessons.md`; `failure-diagnosis.md` "Phases 5/6 - root hub up, enumeration
fails".

## Phase 6 - Device Enumeration

Goal: a directly attached device gets a USB address through this miniport and
reaches Device Manager with its correct VID/PID - usbport runs the enumeration
state machine, the miniport does the xHCI steps (slot, EP0 context,
SET_ADDRESS interception, MPS0 correction, teardown, save/restore).

Status: closed. Checkpoint observed on 2a and 2b with one binary: a
directly attached FS and HS device enumerates to its VID/PID; multi-element-safe
control TD construction, exact completion matching, SET_ADDRESS interception,
EP0 close/reopen without slot loss and Short Packet handling all proven in the
logs. Not observed here: the Low-Speed leg (to bare metal), the multi-element
SG clause (Phase 6 traffic never maps a second element; measured in Phase 8)
and a disconnect mid-transfer (QEMU completes control transfers instantly;
the mid-command-chain half unwound cleanly).

Rationale: batch 6-0 read both shipping `usbport.sys` builds first because
three of the six ABI assumptions the plan rested on were wrong
(`CloseEndpoint` and `GetEndpointState` are never called; the post-open wait is
an uncapped loop, so a frozen `Get32BitFrameNumber` hangs the enumerating
thread). Pure core first (6-A), the slot layer as one batch (6-B), then one VM
trip (6-V) that also carried Phase 7b's topology probe.

What shipped: `src/xhci_xfer.c` (control TD groups published with one store,
the pending-transfer queue, event-to-transfer matching, the completion-code
mapping restricted to the Win2000 DDK's vocabulary, the "any measured length
wins" rule); `src/xhci_ctx.c` (context encoders, both strides, all speeds);
`src/xhci_slot.c` (the slot table and address map, the asynchronous EP0 chain,
SET_ADDRESS interception, the MPS0 Evaluate Context, three teardown triggers
including port disable - a device usbhub abandons is a device gone - and the
deferred completion drain); CSS/CRS with the reinitialise fallback in
`src/xhci_init.c`; `src/xhci_probe.c`, the runtime transfer-contract probe of
task 6-V.1; `QueryEndpointRequirements` asking for no per-endpoint buffer.

Tasks: batch 6-0 (static: the reopen sequence, poll deadline, transfer
parameter lifetime, the empty SG list, CSS/CRS in QEMU); batch 6-A - 6-A.1
control TD construction, 6-A.2 pending TDs and completion matching, 6-A.3
completion behaviour; batch 6-B - 6-B.1 endpoint callbacks and the frame
counter, 6-B.2 the EP0-open machine, 6-B.3 SET_ADDRESS interception,
6-B.4 the MPS0 correction, 6-B.5 disconnect at every state, 6-B.6 save and
restore state; batch 6-V - 6-V.1 the runtime probe and the checkpoint.

Records: `docs/usb-xhci-info/usbport-miniport-abi.md` sections 4, 5, 8;
`docs/usb-xhci-info/usbport-miniport-interface.md` "What Phase 3 can and cannot
prove about transfer mapping", "The probe that discharges it";
`docs/usb-xhci-info/xhci-data-structures.md` (save/restore, control transfers,
completion codes, contexts); `docs/usb-xhci-info/xhci-programming.md`
("Completion Status Mapping", "Spurious success"); `implementation-invariants.md`
("Transfer Buffers", "Control Transfers", "Device Addressing", "DMA Teardown");
`design/04-controller-common-buffer.md` sections 3.4 and 3.6; `design/05-locking-model.md` section 7; `architecture.md`
"Data Flow: Device Enumeration"; `lessons.md`.

## Phase 7a - Interrupt Transfers and Direct HID

Goal: the existing HID stacks on both targets drive directly attached USB
keyboards and mice through one `xhci98.sys` - non-default endpoint contexts,
Configure Endpoint, interrupt transfers and the quiescence family (stop, abort,
reset-pipe).

Status: closed. Checkpoint observed on 2a, 2b (under Driver Verifier) and
2d with one binary: keystrokes and pointer movement through each OS's own HID
drivers, ten unplug/replug cycles per guest, abort and reset survived,
concurrent traffic on the SMP VM. Two clauses are unobservable in QEMU and are
recorded as such: an alternate-interface change (no QEMU HID has a second
setting) and reset-pipe after a STALL (nothing stalls on demand). The 2a fatal
`0E` carried from Phase 6 reproduced on every HID unplug here, was explained
(a completion delivered inside `SubmitTransfer`, which usbport writes after)
and closed with the `SubmitDepth` bracket.

Rationale: batch 7a-0 read `AbortTransfer`'s post-return lifetime from both
binaries first, because "immediately reclaimable" decides whether the
cancellation machine may hold any usbport pointer across a command chain (it
may not). Hub topology is a separate phase so an unresolved Route
String contract cannot obscure direct-endpoint correctness.

What shipped: Configure Endpoint for non-default endpoints (completion codes
7/8/35 treated as scheduling refusals, answered `USBD_STATUS_NO_BANDWIDTH`);
interrupt transfers through the Phase 6 TD builder; non-EP0 rings from the
32-ring pool in the controller block; the quiescence machine (`XHCI_EP_QUIESCE`:
REMOVE, PAUSED, abort that copies nothing usbport owns, No-Op rewriting of
leftover TRBs, Drop+Add for alternate settings, `PendingParams` until a command
succeeds); reset-pipe through `GetEndpointStatus`/`SetEndpointStatus`; the
`SubmitDepth` bracket; ~45 endpoint counters and the monitor-side counter
reader.

Tasks: batch 7a-0 (static: `AbortTransfer` lifetime and Phase 7b's field
half); batch 7a-A - 7a-A.1 endpoint contexts and Configure Endpoint,
7a-A.2 interrupt transfers; batch 7a-B - 7a-B.1 the endpoint-state machine,
7a-B.2 cancellation, 7a-B.3 reset-pipe; batch 7a-V - 7a-V.1 the direct HID
lifecycle and the checkpoint.

Records: `docs/usb-xhci-info/usbport-miniport-abi.md` (Endpoints and Transfers
in section 4; "Periodic scheduling: what `Period` actually carries");
`docs/usb-xhci-info/usbport-miniport-interface.md` "3. Callback families";
`docs/usb-xhci-info/xhci-data-structures.md` ("Which command may set which Slot
Context field", "Which Slot State each command requires"); `design/04-controller-common-buffer.md`
(the shared pool, resolved); `design/05-locking-model.md` "Endpoint records and
the quiescence machine", "10. Open against the SMP checkpoint";
`implementation-invariants.md` "Doorbells"; `build-and-test.md` "`sendkey` and
`mouse_move`", "A replug onto an idle-suspended controller is invisible",
"Reading counters out of a live guest"; `lessons.md`.

## Phase 7b - External Hub Topology

Goal: settle the usbport-to-xHCI topology contract - usbport exposes only the
transaction-translator pair (`HubAddr`, `PortNumber`), never the route - and
support devices behind USB 2.0 hubs: Route Strings, hub Slot Context marking,
TT fields, hub churn, without weakening the direct path.

Status: closed, with three clauses deferred to Phase 13 by decision. Observed
on both VMs with one binary: Route Strings one and two tiers deep, the full
churn list, the five-tier ceiling refused at `route 0x11111`, the phantom-TT
negative control `TtPairsDisagreed = 10`, all under Driver Verifier on 2b, with
QEMU's own `usb_xhci_slot_address` trace as the oracle.

On the E460 under Windows 98 (batch 7b-M): HS, FS and LS devices on root ports,
three hubs as one daisy-chained tree, a mouse and a Low-Speed keyboard working
behind a real High-Speed hub, so split transactions ran.

Not observed here: any `TT`/`MTT`/`TTT` number on real translators, multi-TT
behaviour, and the Windows 2000 half on metal, all Phase 13's. The stop rule
(if snooping cannot reconstruct the path on both builds, ship a limitation) was
answered affirmatively on both builds; no Option A hub limitation is owed.

Rationale: the first batch was a measurement, not code - task 6-V.1's probe
let the feasibility gate run on the Phase 7a binary against a QEMU `usb-hub`.
It confirmed the hub's own EP0 traffic carries usable SETUP bytes on both
stacks, and found the behind-hub refusal was unbounded (a dead Win98 guest),
which became task 7b-A.0 before any topology code. QEMU's `usb-hub` is USB 1.1
and `usb-host` passthrough of a hub was measured shut, so anything needing a
real transaction translator went to metal.

What shipped: `src/xhci_topo.c` - the pure-core hub graph learned by snooping
`GET_DESCRIPTOR(Hub)`, `SET_FEATURE(PORT_RESET)` and `GET_STATUS(port)`
replies at placement time; one address-0 claim per root-port reset and a
progress detector that fails a record refusing with nothing placed; hub Slot
Context marking (Hub, Number of Ports, TTT, MTT) by an A0-only Configure
Endpoint; behind-hub slots with Route String, root port, inverted-PSI speed
lookup and the TT triple from the graph, too-deep routes refused; the
`OpensTotal` identity and the TT pair table as release-build measurements;
`scripts/hub-characterise.ps1`.

Tasks: batch 7b-V0 - 7b-V0.1 the feasibility gate; batch 7b-A -
7b-A.0 bound the behind-hub refusal, 7b-A.1 the snooping graph (sub-tasks
7b-A.1.0 open accounting, 7b-A.1.1 the pre-SET_ADDRESS reset, 7b-A.1.2 the TT
pair table), 7b-A.2 hub Slot Context marking, 7b-A.3 behind-hub slots; batch
7b-V - 7b-V.1 hub churn, 7b-V.2 the five-tier ceiling and the phantom-TT
control; batch 7b-M - 7b-M.1 single-TT versus multi-TT numbers, 7b-M.2 the
hub replacement (both carried to Phase 13).

Records: `design/02-hub-topology-route-string.md` (the whole record, with the
7b-V0 box, the step boxes and the verdict); `docs/usb-xhci-info/usbport-miniport-abi.md`
"The transaction-translator lookup, and why `USB_MINIPORT_FLAGS_USB2` must
be set"; `docs/usb-xhci-info/xhci-data-structures.md` ("Route String tier
order", the Slot Context MTT/TTT rows); `docs/usb-xhci-info/xhci-programming.md`
"Downstream Hub Addressing"; `implementation-invariants.md` "Hub Paths";
`build-and-test.md` "QEMU coverage limits", "Bootstrapping xHCI-only machines
(no EHCI)", "Getting a trace off a bare-metal machine", "The bench rig";
`design/06-device-matrix-verdict.md` section 3.2; `docs/issues/01-windows-98-log-capture.md`;
`lessons.md`.

## Phase 8 - Bulk, Mass Storage, and Ethernet

Goal: a USB flash drive is accessible and a USB Ethernet adapter passes
sustained traffic on both targets, with the Normal-TRB engine scaled to bulk
load and its failure recovery systematically exercised.

Status: closed. Checkpoint observed on 2a and 2b (and 2d for the SMP unplug
clause): checksums match across transfers larger than one ring, sustained
bidirectional Ethernet, unplug during a read and a write completes or cancels
every transfer exactly once, and `probe max SG elements` finally reads above 1.
One clause is accepted as the vehicle's and named rather than ticked: after a
mid-write unplug, a re-attach on a different root port wedges Win98+NUSB,
reproduced with Microsoft's `usbehci.sys` on the same guest. Carried forward:
suspend/resume (to Phase 13), the Windows 2000 driver-date question (task
12.4), Low Speed (to bare metal).

Rationale: bulk is the first traffic that gives usbport's mapper something to
split and the first that fills rings, so wrap and boundary arithmetic is proven
by host vectors (8-A) and everything device-bound by VM runs (8-V). Driver
identity was pulled forward from Phase 11 because every screenshot and bug
report until then carried a placeholder name.

What shipped: bulk endpoint open and submit; the backpressure latch that
re-offers a ring-full refusal (measured unreachable by any device class the
project can present - Bulk-Only Transport is strictly serial - and kept as
defensive); a ten-code failure-recovery matrix; the withheld second Short
Packet Event departure, found on a passed-through ASIX AX88772A (QEMU's xHC
emits one event where the specification mandates two); driver identity - the
devnode name `USB 2.0 eXtensible Host Controller (xhci98)`, `src/xhci98.rc`,
the `DriverVer`/`FILEVERSION` cross-check in the INF gate; the "regenerate, do
not adjust" rule for the counter-offset table.

Tasks: batch 8-A - 8-A.1 the bulk engine, 8-A.2 failure recovery,
8-A.3 active-unplug teardown with mixed traffic, 8-A.4 driver identity; batch
8-V - the multi-element SG clause, 8-V.1 mass storage, 8-V.2 USB Ethernet.

Records: `docs/usb-xhci-info/xhci-data-structures.md` "The withheld second
Short Packet Event"; `implementation-invariants.md`
("Completion Matching", "Ring Full and Backpressure"); `build-and-test.md`
("Versioning the driver", "QEMU monitor - hot-plug USB devices without
rebooting", "Target Class Devices"); `docs/issues/README.md` (the multi-TRB
short packet); `docs/using/release-notes.md` "Known limitations";
`lessons.md`.

## Phase 9 - Isochronous ABI and USB Audio

Goal: prove the isochronous `SubmitIsoTransfer` ABI from the shipping
`usbport.sys` binaries and use it for USB Audio on both targets, or publish an
explicit Option A limitation. A guessed parameter layout was not an acceptable
third outcome.

Status: closed. The gate passed statically (one contract, nothing to
discriminate) and dynamically (250,330 packets on 2b with no malformed
refusal). Playback met on Windows 2000 under Driver Verifier with three
oracles. Named rather than met on Windows 98 in the VM: Win98 SE's own
`USBAUDIO.VXD` divides by zero after one URB, reproduced through a UHCI control
with this driver idle - later shown to be a vehicle artefact when a physical
UAC 1.0 device played clean on the E460 (batch 13-E). Recording not applicable
(the emulated device has no input). Deferred to Phase 13 with measured reasons:
a physical audio device, audio behind a High-Speed hub, a device declaring
`bInterval > 1`.

Rationale: ReactOS's isochronous path is a stub; the parameter block existed
only in the binaries. Task 9-0.2 (the mid-TD short-packet retire, the only
known correctness defect leaving Phase 8) was placed ahead of the engine because
it touches the hardware-established receive path.

What shipped: the ABI (`SubmitIsoTransfer`'s fifth argument is a `'Isoc'`
block of `0x48 + 0x38*n` bytes; `UsbPortCompleteIsoTransfer` at packet slot
`0x100`); the settle rule for mid-TD short packets (retire deferred to a drain
pass that observed the ring empty); the isochronous engine (N packets = N TDs,
IOC per TD, SIA unless CFC, the frame axis made congruent to MFINDEX, a declared
cap of 62 single-fragment packets per request); the configuration-descriptor
snoop (`src/xhci_desc.c`) that derives an isochronous endpoint's `bInterval`,
which usbport cannot supply; the `InterruptNextSOF` contract (a stub is legal);
the passthrough rung published shut for IAD-grouped multi-interface functions
on a Windows host - every USB Audio device.

Tasks: batch 9-0 - 9-0.1 the ABI gate, 9-0.2 the mid-TD retire; batch
9-A - 9-A.1 the isochronous engine, 9-A.2 the descriptor snoop, 9-A.3
`InterruptNextSOF`; batch 9-V - 9-V.1 USB Audio validation, 9-V.2 the
passthrough rung.

Records: `docs/usb-xhci-info/usbport-miniport-abi.md` ("Isochronous transfers",
"`InterruptNextSOF`", "Periodic scheduling"); `docs/usb-xhci-info/xhci-data-structures.md`
("Isochronous scheduling", "The withheld second Short Packet Event");
`docs/usb-xhci-info/xhci-programming.md` "Never set BEI";
`implementation-invariants.md` "Completion Matching"; `build-and-test.md` "USB
Audio in QEMU: what the vehicle can and cannot show"; `design/06-device-matrix-verdict.md` section 7;
`docs/using/release-notes.md` "Known limitations" (the USB Audio entry); `test-equipment.md`; `lessons.md`.

## Phase 10 - Automated VM Device Matrix

Goal: an unattended harness that boots each target VM, walks every USB device
model QEMU can present, and produces a diffable, machine-readable pass/fail
report per device per target - so "does the driver still handle the device
population" can be re-run after any change.

Status: closed. One command runs the whole matrix unattended on both
targets (17 rows each, no `ERROR`); `NODRIVER` and `inert` are explicit
outcomes; the harness reproduces Phase 7a's HID result, Phase 5 task 7's
speed-mismatch untruth and batch 7b-V's churn `TtPairsDisagreed +10`, and fails
on the broken `matrix.broken.psd1` fixture; it runs on a second host. No
row-level `INERT` instance exists in this population - every row inherits the
live `Always` block. The
`usb-bot`/`usb-uas` `+0` carried out of this phase was later settled as a QEMU
`auto_attach = 0` artefact, not the driver (batch 11-V).

Rationale: placed before Phases 11 and 13 because they are its heaviest
consumers. Assembly rather than invention - the monitor scripts, counter readers
and QEMU's `-trace usb_xhci_*` oracle already existed; the missing piece was
the matrix and the verdict. The committed harness depends on nothing in the
git-ignored `scripts/local/`.

What shipped: `scripts/vm-matrix/` - `run-matrix.ps1`, `probe-devices.ps1`,
`gen-offsets.ps1` (the counter-offset table derived from the driver's own
sources, checked against the running driver every boot), `prepare-image.ps1`,
`selftest.ps1`, `matrix.psd1`, `matrix.broken.psd1`, `lib/`; the verdict model
(five outcomes, four expectation kinds, the `endpoints opened` discriminator,
the nine-term open identity); the trap guards (stale offsets, liveness by
`info irq`, a leftover guest on the monitor port, a short counter read voids
the run).

Tasks: 10.1 enumerate the device population; 10.2 design the verdict;
10.3 build the runner; 10.4 validate against known answers; 10.5 make it
re-runnable by someone else.

Records: `design/06-device-matrix-verdict.md`; `scripts/vm-matrix/README.md`;
`build-and-test.md` "The automated VM device matrix (Phase 10)", "Reading
counters out of a live guest"; `lessons.md`; `run-11v.md` Stage E.

## Phase 11 - Power, Packaging, and Stress

Goal: prove on both target VMs, the SMP VM and Driver Verifier that the driver
survives sustained mixed load, 20-cycle device churn per class, the reachable
power lifecycle and package install/upgrade/uninstall/reinstall - and that what
the end user receives is usable: a one-screen qualifier default, release notes,
and a diagnostic log that needs no kernel debugger.

Status: closed. Checkpoint met on the rule "every reachable clause passes on
both targets, every unreachable clause is recorded with its reason and none is
reported as passed", across stages A-H of `run-11v.md`.

Ten clauses were never observable in this vehicle. Nine are published; the
tenth, restart after controller invalidation, is recorded as not reached in
`run-11v.md` and is deliberately NOT in the release notes, because the batch
13-R census found usbport never issues that stop/start, so there is nothing to
publish a limitation about. The list: Win98 disable/re-enable (bugchecks
through every door), `ResumeController` on Windows 2000, restart after
controller invalidation (not published, as above), recovery after a controller
fails, the scratchpad-limit refusal, rollback after
a failed start (no *Roll Back Driver* before XP; task 12.3), Low Speed,
single-/multi-TT hub trees, isochronous on Win98, and the qualifier's
one-screen budget on a real console.

One obligation survived: the INF engine's delivery of the log registry values
on a fresh install (task 11-V.9), which task 14.1.2 later closed by publishing
it as a limitation rather than by measuring it.

Rationale: Phases 5-10 each exercised one class at a time and never the
package, the lifecycle under traffic, or two controllers. Two user-facing
deliverables were pulled forward because the bench trip needed them: a log
channel that works without a debugger, and a qualifier whose default is safe.
Batch order followed what needs the operator at the machine - host work first
(11-A, 11-B), the run last (11-V).

What shipped:

- the global-state audit (the release image has exactly three writable
  process-globals);
- the `UsbPortGetMiniportRegistryKeyValue` ABI read out of four binaries;
- the bounded in-memory log ring (`src/xhci_log.c`) flushed only from
  `StopController` at PASSIVE_LEVEL, with the `XhciLogDebugView` sink, the one
  `DbgPrint`-outside-`#if DBG` exception in `AGENTS.md` (the file sink built
  here was retired by task 13-L.2);
- the INF gate's `VAL-*` rules and `-EmitFootprint` with the tracked
  `expected-footprint.txt`;
- `make-11v-media.ps1`;
- the Windows 98 idle hot-plug defect fixed by one INF line
  (`DisableSelectiveSuspend = 1`; a halted xHC cannot raise a port-change
  interrupt, unlike EHCI);
- the `XHCIQUAL` no-argument quick scan;
- `docs/using/release-notes.md` and `run-11v.md` themselves;
- the stress results (four-class load, 20 cycles per class per target, slot
  exhaustion at 32+1, two controllers bound on both targets);
- the package results (fresh, uninstall, reinstall pass on both; Windows 2000
  refuses to upgrade itself; Windows 98 upgrade delivers the binary and
  bugchecks before the registry phase).

Tasks: batch 11-A (host: the global-state audit, the registry ABI, the log
ring, the quick-scan default); batch 11-B - 11-B.1 create the release
notes, 11-B.2 write the run sheet, 11-B.3 the media and the footprint; batch
11-V - 11-V.1 lifecycle with traffic, 11-V.2 two controllers and slot
exhaustion, 11-V.3 the package on clean snapshots, 11-V.4 the backwards-
compatibility subset, 11-V.5 the stability matrix, 11-V.6 the idle hot-plug
defect, 11-V.7 the optional log's outcome, 11-V.8 the qualifier default,
11-V.9 the log producer set and the ring.

Records: `run-11v.md` (stages A-H); `design/05-locking-model.md` "11.
Process-global storage"; `docs/usb-xhci-info/usbport-miniport-abi.md` "6. usbport
service functions"; `design/08-build-flavours-and-the-log-channel.md`;
`docs/issues/01-windows-98-log-capture.md`; `build-and-test.md` "INF-Based
Installation", "Getting a trace off a bare-metal machine";
`docs/using/release-notes.md` ("The log, and how to send one", "Known
limitations");
`xhciqual/README.md`; `lessons.md`.

## Phase 12 - Host- and Guest-Side Decisions

Goal: take the decisions the project was carrying that need no machine, each
of the shape "build the artifact, or publish the gap", so that nothing
decidable on the host or in a 2a/2b guest stays parked behind bare-metal work
that may never happen.

Status: closed. Every task closed on one of its two named outcomes, none
reported as blocked by Phase 13, and the two obligations the phase carried in
the release notes are cleared. A decision deferred out of this phase is a
limitation, not a pending item, and is published.

Rationale: the bare-metal phase's checkpoint was chained to a Windows 2000
install on real hardware that may never be achievable, and decisions needing
nothing but a decision were being reported as blocked by a prerequisite they
never had. Keeping them open as decisions paid off both ways: 12.3's run found a failure mode nobody predicted,
and 12.4's closed off a suspected defect that would otherwise have shipped a
useless INF change.

What shipped:

- 12.1 - publish: the `FSC = 0` suspend path is the standby entry in the release notes' "Known limitations";
  `HCCPARAMS2` added to the qualifier's capability dump (a fleet reading of
  `FSC = 0` was later taken on the E460); the `EndpointQuiesceFailures`
  counter.
- 12.2 - publish: the `DbgPrint` exception is not widened - a shipping Windows
  98 install has no PASSIVE moment between `StartController` and shutdown for
  a second emit site to fire in; the serial sink withdrawn; Windows 98 metal
  has no push channel (the read channel came later, batch 13-L).
- 12.3 - build: the failed-start artifact (`-DXHCI_FAIL_START_CONTROLLER`,
  `make-package.ps1 -FailStartArtifact`), run on both guests: cleanup is
  correct on both, and Windows 98 does not survive a failed
  `StartController` (the failed-start entry in the release notes).
- 12.4 - build: the unpadded-`DriverVer` experiment package, run on 2b: the
  unpadded date records no date either, so padding is not why Windows 2000
  shows no driver date; "unsigned" remains by elimination (the Windows 2000 upgrade entry in the release notes).
- 12.5 - the control run: 150 fast hub attach/detach pairs wedge Windows 98
  only when `xhci98.sys` carries them (122 clean pairs on UHCI against 12 on
  xHCI in the same boot) - the rapid plug/unplug entry in the release notes, this driver's; nobody owns the mechanism hunt.

Tasks: 12.1 the `FSC = 0` suspend path; 12.2 a Windows 98 bare-metal trace
channel, or the statement that there is none; 12.3 the failed-start rollback
artifact; 12.4 why Windows 2000 records no driver date; 12.5 the hub-churn
wedge control.

Records: `docs/using/release-notes.md` ("DebugView", "Known limitations"); `build-and-test.md` ("Staging a driver that starts
and fails (task 12.3)", "Staging the unpadded-date experiment package (task
12.4)", "Getting a trace off a bare-metal machine");
`implementation-invariants.md` "Suspend and Resume"; `design/01-hardware-qualification-tool.md` row B9;
`design/08-build-flavours-and-the-log-channel.md` section 1; `docs/issues/01-windows-98-log-capture.md`;
`docs/issues/README.md` (the hub-churn wedge); `lessons.md` (task 12.5);
`run-11v.md` Stages B and H.

## Phase 13 - Final Bare-Metal Validation

Goal: discharge every clause earlier phases deferred for want of a real
machine or a piece of equipment, rather than for want of code, in one place;
and repair the one defect the bench trip itself found (a root port going
permanently deaf until a cold boot) on the machine and with the instrument
that found it.

Status: closed on the published-limitation branch, not on the main clause.
Every task is ticked. Batches 13-H, 13-E, 13-R and 13-L reported. The Windows
2000 batch (`13-T`) never could and was removed, its clauses published as
limitations: Windows 2000 SP4 Setup bugchecks during installation on both fleet
machines, no cause was investigated and no bugcheck code captured, no further
candidate exists, and so Windows 2000 has never run on real hardware in this
project.

Closed on that branch: the transaction-translator `MTT`/`TTT` numbers, the
Windows 2000 audio and isochronous-counter clauses, the Low-Speed trace half
and `ResumeController` on Windows 2000, the qualifier on a real
multi-controller console, and `XUSB2PR` against the driver. Also closed on the
limitation branch, by equipment: a Full-Speed hub behind a multi-TT hub (no USB
1.1 hub is held or reliably purchasable; decided before the trip, no purchase).

Number-bearing Windows 98 clauses are published as not taken, never as not
possible: a read route exists through the PassThru instrument.

Three items are open and gate nothing: why the `0.0.0.4` debug flavour failed
to load on the E460 (the import or the port; two binaries built, boots not
taken); whether the bulk-dump profile survives DebugView on Windows 98 metal
(the discriminating boot was dropped, so it is evidence in neither direction);
and the multi-controller console reading, unreachable on this fleet rather than
as such.

Do not read this status as "validated on both targets". The phase's closing
rules: a clause deferred into this phase and then deferred again out of it is a
limitation, not a pending item, and is published; and an equipment clause does
not close by being carried to a later phase, because a purchase decision
belongs before booking, not at the bench.

Rationale: batching by machine (the modern host, then one E460 trip) made each
batch one visit's worth of work and stopped tasks straddling bench trips. The
phase's rule that nothing here is new implementation work was broken once, for
batch 13-R, because the repair had to be validated with the same machine and
instrument before the trip's context was gone. Batch 13-L joined from Phase
14's task 14.0 because its first clause was a bench reading (does the
diagnostic build load on the E460?) rather than a design. Nothing was ever
allowed to be reported as blocked by the Windows 2000 batch, which was
sequenced last for that reason.

What shipped:

- The equipment record (`test-equipment.md`): twelve devices and six hub
  units characterised in the rig, the three buy-or-publish decisions taken with
  no purchase, rig positions D and T labelled on the E460,
  `scripts\hub-characterise.ps1` extended.
- `usbhub98.sys` (Finding D): the Windows 98 composite-device gap was one
  missing file - `usbhub.sys`, which Setup never copies on an xHCI-only
  machine - now carried on the Win98 path only under `COPYFLG_NO_OVERWRITE`
  with the same provenance and gate treatment as `usbd98.sys`.
- Bench results on real xHCI silicon under Windows 98 SE (E460): multi-TT
  and single-TT hub behaviour with five children identical either side of the
  one-variable hub swap; USB Audio plays clean at a root port and behind a
  multi-TT hub (the published Windows 98 audio limitation was a QEMU artefact
  and was corrected); the UAC 2.0 device with `bInterval` 3/4 does not bind, so
  Interval > 0 remains unexercised everywhere; the first bulk-IN class traffic
  (ASIX Ethernet) and the first data-verified round trip through a real
  USB-to-SATA bridge on this driver; `FSC = 0` read on the E460.
- The Finding 3 repair and the `0.0.0.5` cut (batch 13-R): recovery in
  place for the `ControllerFailed` latch (`design/07-controller-recovery-in-place.md`); the command-age
  detector abort rung; every poll-counted threshold re-expressed in
  milliseconds on the `PollClockMs` axis after the E460's poll period measured
  36-80 ms rather than the assumed 500 ms; the qualifier's untested-routing
  caveat moved out of its verdict line; the upload asset renamed
  `xhci98-<version>.zip`.
- The three-flavour build and the log channel, `0.0.0.6` (batch 13-L):
  `release`, `debug` and `qemu` with the safe configuration as the default and
  the emulator-only one opt-in and never published (`design/08-build-flavours-and-the-log-channel.md`); the PassThru
  read channel in every flavour behind `XhciLogVerbosity` (default 0); the
  ring-0 file sink retired; `XHCISNAP`, the host tool that switches the channel
  on and reads the driver's log ring off the machine; the `debug` flavour
  confirmed to load on the E460, and a shipping binary carrying the driver's
  own log off a Windows 98 machine for the first time.

Tasks:

- Batch 13-H (the characterisation bench): 13-H.1 characterise every hub and
  device, 13-H.2 fix the plug plan and prove the equipment fits it.
- Batch 13-E (the E460 trip): 13-E.1 the composite-device gap (closed on a
  fix), 13-E.2 multi-TT hub behaviour (the behavioural half), 13-E.3 Phase 9's
  deferred audio clauses on Windows 98, 13-E.4 the trip's own record and the
  free re-observations.
- Batch 13-R (the Finding 3 repair): 13-R.1 make the `ControllerFailed` latch
  non-terminal, 13-R.2 the command-age abort rung, 13-R.3 read the repair off
  the E460, 13-R.3.5 thresholds in time rather than polls, 13-R.4 remove the
  snapshot instrument before the cut, 13-R.4.5 the qualifier caveat, 13-R.5 cut
  `0.0.0.5`.
- Batch 13-L (the Windows 98 log channel): 13-L.0 why the dead ends are dead
  (`design/08-build-flavours-and-the-log-channel.md`), 13-L.1 the three-flavour
  split, 13-L.2 the log channel and its registry values, 13-L.3 install and
  read both shipping binaries on the E460, 13-L.4 cut `0.0.0.6`, 13-L.5 the
  seam between `design/08-build-flavours-and-the-log-channel.md` and the
  instrument document, 13-L.6 the `XHCISNAP` console at 80x25.

Records:

- `run-13e.md` (the E460 run sheet: the finding-status table, the stages,
  Findings 1-Y and Stage L3)
- `test-equipment.md` (the characterisation record and the Phase 13 equipment
  requirements)
- `build-and-test.md` ("Available Test Hardware", "The bench rig", "Getting a
  trace off a bare-metal machine", "Bootstrapping xHCI-only machines (no
  EHCI)")
- `design/07-controller-recovery-in-place.md`;
  `design/08-build-flavours-and-the-log-channel.md`;
  `passthru-snapshot-instrument.md`
- `docs/issues/01-windows-98-log-capture.md`,
  `02-bare-metal-wedge-and-portsc-watchdog.md` and
  `03-usbhub-sys-composite-devices.md`
- `docs/using/release-notes.md` ("The log, and how to send one", "DebugView"). The two limitations this batch
  wrote, the dead root port and the debug build that would not load, were
  removed at the `1.0.0.0` cut: both were fixed before the release, so neither
  is a limitation of it. What they measured is in `run-13e.md` and in the two
  design records below.
- `run-13e-evidence/README.md`;
  `xhciqual/results/e460-2026-08-22/`

## Phase 14 - The `1.0.0.0` Release

Goal: close the project's first final release on what Phase 13 found. Publish
what a Windows 98 user can send back when something goes wrong, write down how
a new release is accepted on a new machine, clean the repository so that it
says what is true at the end of the work, then cut `1.0.0.0`.

Status: closed on the cut. Tasks 14.0-14.2 are done, 14.1's eleven clauses
included, every clause taken on this host. A task 14.3, an unattended run on
fresh guests, was added after the cut and never started; it is now Phase 16's task 16.1,
moved unchanged with its design record. Other files still name the old id as
the task's history, and nothing resolves it by that id.

Uploading the asset is not a task anywhere: it is one act, the project
owner's, taken when they choose (`legal-provenance.md` section 5 carries the
status note). Installing what was cut is the acceptance run's, not this
phase's: the install worth taking is a stranger's, from the published asset,
on a fresh guest of each target, and taking it here too would have measured
the same install twice, the second time from the tree that built it.

Rationale: the first two tasks are about the reader, a user with a broken
machine and a tester with a fresh one, which is what changes at `1.0.0.0`. A
pre-release is met by the person who built it; a final release by people this
project will never watch. The tasks are ordered: 14.0 writes the wording 14.1
verifies and 14.2 ships; 14.0.5 creates a document 14.1's index and path
checks then read; 14.2 is strictly after 14.1, because a `1.0.0.0` whose notes
still carry an open obligation has made a claim Phase 13 spent its length
refusing to make. The `0.0.0.5` and `0.0.0.6` cuts were vehicles that carried
a repair to a bench, not this phase's releases.

What shipped:

- Batch 13-L's answer to the Windows 98 log question, said the same way in
  the release notes, the `readme.txt` template and the bug-report template: a
  channel exists in every flavour, the same on both targets, read with
  `XHCISNAP`; a `0.0.0.6` build under DebugView on Windows 98 hardware is not
  tested, and not tested is not cleared.
- `docs/using/release-acceptance-test.md`: nine steps with expected readings,
  equipment named by property, the version named in one place, handable to
  someone who has never seen this repository.
- The cleanup (14.1): this file reduced to a status index; the release notes
  carry no open obligation; every cited path resolves (five exceptions, each
  explained where cited); every tracked document reachable from
  `docs/README.md` or `AGENTS.md`; nothing third-party tracked, and Oney's WDM
  book given a tracked record in `docs/references/README.md`; the licensing
  texts read against the download and corrected in three places; the two
  history documents deleted with their review-method rules moved to
  `lessons.md`; 1,854 of 2,071 dates removed from tracked prose; every task
  id in the tree resolving to this file; the version and date in one place,
  `src\xhci_version.h`, with the INF gate cross-checking `DriverVer` (261 to
  279 self-tests); the retired-instrument `.BIN`/`.PSC` dumps dropped from
  `runs/run-13e-evidence/`.
- `1.0.0.0`: `1,0,0,0` / `08/29/2026` in the header, both tools rebuilt,
  `releases/history.md` rewritten to a single entry, every `0.x` directory
  removed (none was ever given to anyone) with their path citations replaced
  by version, size and hash, the readme template's "beta software" passages
  rewritten, and `releases/1.0.0.0/` plus `out\xhci98-1.0.0.0.zip` written by
  `make-release.ps1`. The claim the number makes, stated in `history.md` and
  the release notes: validated behaviourally on real hardware on Windows 98,
  with no running trace or crash capture on that target; Windows 2000 on
  metal unreachable; "final" means the driver does what this repository says
  and the limitations are published, not that nothing is left. Four
  limitations recording something fixed before the release were removed; the
  live residue of one (a suspend during in-place recovery on a multiprocessor
  machine) is no longer listed there.

Tasks:

- [x] 14.0 publish the Windows 98 log answer where a user will meet it.
- [x] 14.0.5 write the release acceptance test.
- [x] 14.1 repository cleanup: 14.1.1 this roadmap as a status index; 14.1.2
  no open obligation in the release notes (task 11-V.9 published as a
  limitation); 14.1.3 every cited path resolves; 14.1.4 every tracked
  document reachable from the indexes; 14.1.5 nothing third-party tracked;
  14.1.6 the licensing texts true against the download; 14.1.7 the history
  documents deleted; 14.1.8 no dates in tracked prose; 14.1.9 every task id
  resolves; 14.1.10 the version in one place; 14.1.11 the bench evidence
  pruned.
- [x] 14.2 cut `1.0.0.0` with `make-release.ps1`, never by hand: bump the
  version, write the `history.md` entry first, re-read the readme template
  against the release notes, remove every `0.x`, state the claim.
Records: `releases/README.md` (the written-once rule, the upload set);
`releases/history.md`; `docs/using/release-notes.md`; `build-and-test.md`
"Versioning the driver"; `release-acceptance-test.md`;
`.github/ISSUE_TEMPLATE/bug_report.yml`; `legal-provenance.md` sections 2 and 5;
`docs/references/README.md`; `lessons.md` (the review-method rules);
`runs/run-13e-evidence/README.md`.

## Phase 15 - Specification Revision 1.2c

Goal: move the repository from revision 1.2 of the xHCI specification to
revision 1.2c, the only revision Intel now serves, without changing what the
driver does, and prove that by rebuilding and testing it.

Status: closed on 2026-08-29, the day it opened. No code changed, so no
release was re-cut. The matrix clause of the checkpoint was met by the
owner's decision on byte identity rather than by a run: the rebuilt
`release` and `debug` `xhci98.sys` differ from the `1.0.0.0` files only at
the PE timestamps and checksum, every section byte-identical. The unattended
post-release run that opened here as task 15.5 became Phase 16.

Why a phase: every `p.N` in this tree was verified against revision 1.2 (645
pages). Revision 1.2c (600 pages, October 2025) repaginates the whole
document and Intel publishes no version-pinned link to 1.2, so a reader who
follows the recorded URL lands none of those page numbers. 1.2b added the
USB3 Tunneling extended capability (ID 18) and PORTSC bit 2 (TM); 1.2c added
eUSB2 (HCCPARAMS2 bits 11 and 12, PORTPMSC bit 27, the eUSB2 isochronous
companion rules), the Camera Sideband capability and CONFIG bit 10 (SOC),
split 4.8.2.4 into 4.8.2.4 Isoch and 4.8.2.5 Interrupt, renumbered 5.4.11's
PORTEXSC subsections to 5.4.12 and Appendix I, and dropped 4.14.2's "80% of
a microframe" sentence and its Max ESIT Payload list. None of the new
features is reachable from a USB 2.0 root port on this driver.

Tasks:

- [x] 15.1 adopt 1.2c as the reference document: its row in
  `docs/references/README.md` (`xHCI__Rev1.2c.pdf`, 600 pages, SHA-256
  `0b06318005c3e0c8b896f2a002c2a3c78426b5fdacac4ad1cc02ffec15835190`), the
  extraction recipe fixed for 1.2c's odd-page headers, the section anchors
  re-derived, and every statement of the revision in the tree moved.
- [x] 15.2 migrate the page citations by shingle overlap between the two text
  dumps, settled per citation by the quoted phrase or section number, the
  rest read by a person: 1,286 matches, 1,219 moved, 22 corrected that were
  already a page off against 1.2, Oney's 56 untouched. The method and the
  counts are in `docs/references/README.md`, "How the citations were moved
  from 1.2 to 1.2c".
- [x] 15.3 read the tree against what changed. Outcome: comments and documents
  only. `XHCI_CONFIG_RSVDP_MASK` keeps SOC and `XHCI_PORTSC_RSVDZ_MASK` keeps
  bit 2, each with the reason at its definition; the extended-capability walk
  steps over ID 18; CErr stays 3 for interrupt and 0 for isochronous, which
  1.2c's 4.8.2.5 says outright; nothing cites the two deleted 4.14.2
  sentences.
- [x] 15.4 rebuild and test: `build-driver.cmd all`, the host tests and both
  gates green; the matrix clause met on byte identity, as above.

Checkpoint: 1.2c is the only revision the tree cites, every `p.N` lands on
the page a 1.2c copy prints, `docs/references/README.md` records the
revision, the hash and the count re-verified, and the driver built from the
tree passes the host tests, both gates and the device matrix on both targets.

Records: `docs/references/README.md`;
`docs/usb-xhci-info/xhci-data-structures.md` (the HCCPARAMS2, CONFIG, PORTSC
and Table 7-2 rows).

## Phase 16 - The Unattended Post-Release Run

Goal: one command drives a freshly installed Windows 98 SE guest and a
freshly installed Windows 2000 SP4 guest from boot to teardown with nobody at
the keyboard, plugging and unplugging every device this QEMU build can
present, and writes a diffable record and a verdict per target.

Status: closed on 2026-08-30, on the reading rather than on a clean verdict,
the way Phase 13 closed. The harness was built on 2026-08-29 and the run made
twice on 2026-08-30 against the re-cut `1.0.0.0`. The first run found three
harness defects and no driver defect; the second read `PASS` on the fresh
Windows 2000 guest with nothing against, and `FAIL` on the fresh Windows 98
guest on two rows: `usb-uas/fs`, `NODRIVER` on both legs with its
`ExpectNoDriver` entry written after the runner had loaded the matrix, and
the `usb-audio` replug's Insert Disk prompt, reproduced in both runs, which
the owner published as a limitation in `docs/using/release-notes.md` rather
than answer from the harness or pin as non-counting.

Why a phase: Phase 10's matrix measures a change to the driver on guests
carried along since Phase 2, so an install onto them is an upgrade. What a
release needs measured is the install path, the first bind, and the plug and
unplug on an operating system with no history, and nobody re-runs that by
hand more than once per release. It is not the acceptance run this file ends
on, which a person takes from the download. Design record 09 draws the three
apart and holds the owner's decisions the task may not re-argue
(single-processor targets, one manual driver install on an image cloned from
the pre-driver snapshot and stamped, the `qemu` flavour as the binary, the
tools out of scope, TCG).

Tasks:

- [x] 16.1 `run-matrix.ps1 -PostRelease` on a stamped fresh image of each
  target (`-Clone` and `-Stamp` on `prepare-image.ps1`, `lib/fresh.ps1`, the
  two fresh targets, the guestless self-test). Done 2026-08-30, twice; the
  second run's reports are in `runs/run-16-post-release/`.

Checkpoint: `run-matrix.ps1 -PostRelease` has run to completion on a stamped
fresh image of each target with nobody at the keyboard after the command was
given, and each target has its report with a header and a verdict under
`out/post-release/<DriverVer>/`. Both readings are virtual-machine readings,
and a run on the `qemu` flavour is a driver reading taken after a release,
not acceptance of it.

Records: `design/09-post-release-unattended-run.md` (the design, and in its
last two sections what was built and what the runs found);
`runs/run-16-post-release/`; `scripts/vm-matrix/README.md` (the commands,
and notes 14 and 15 for what the first run corrected).

## Phase 17 - The OS Supplies `usbd.sys` and `usbhub.sys`

Goal: the release download carries the driver's own files, the tools and the
readme, and nothing of Microsoft's. `src/xhci98.inf` asks the Windows setup
engine, through `LayoutFile=layout.inf`, to copy `usbd.sys` (both targets)
and `usbhub.sys` (Windows 98 only) from the operating system's own install
source, with `COPYFLG_NO_OVERWRITE` so a file already on the machine is never
touched. The driver code is unchanged; `xhci98.sys` is rebuilt only because
its version resource must match the INF's `DriverVer`.

Status: closed on 2026-09-02, the day it opened, every task done or observed
that evening and the cut deferred to Phase 18. The decision is the owner's,
taken after the SweetLow-stack work measured what each stack needs:
`usbd.sys` is required under every USB 2.0 stack on both targets because
`usbhub20.sys` imports it by name, `usbhub.sys` is required under NUSB's
stack and inert under SweetLow's, and both are the OS's own files. Release
`1.0.0.0` carried them on the media under per-target names; that exception
(`legal-provenance.md` section 5) was withdrawn before any upload.

Why a phase: it changes the install procedure a user follows, the packaging
scripts and gates, the provenance record, and every user-facing statement
about what the download holds. It changes no driver behaviour, which is why
its checkpoint is an install reading rather than a device reading.

Tasks:

- [x] 17.0 record the decision in `legal-provenance.md` section 5 before any
  script change, pointed at from `AGENTS.md`. Done 2026-09-02.
- [x] 17.1 prove the mechanism in the VMs, the owner at the console, all on
  2026-09-02: (a) Windows 98 under SweetLow's stack with no driver, no
  `usbd.sys`, no `usbhub.sys` and no CABs: the install raised the engine's
  own `Insert Disk` prompt naming the Windows 98 Second Edition CD-ROM, and
  after a relaunch the driver registered, `StartController` ran and the
  keep-alive mouse bound; (b) Windows 98 under NUSB 3.3's stack (a fresh
  `post-nusb` clone): the same prompt, the 1.0.0.1 build under NUSB's
  usbport (`USBPORT_GetHciMn=57324B30`), the mouse bound, and a hot-plugged
  `usb-audio` as "USB Composite Device" with "USB Audio Device" beneath, the
  `usbhub.sys` half of the route; (c) Windows 2000 SP4 (a fresh
  `phase2b-clean` clone), Have Disk: no prompt, started without a reboot,
  root hub and HID mouse bound.
- [x] 17.2 the change: the INF's four directive edits, `DriverVer` and
  `src/xhci_version.h` at `1.0.0.1`; the INF gate's `TGT-*` and `W98-*`
  families replaced by the `OS-*` rules and `PKG-MSFILE` with self-tests;
  `make-package.ps1`, `make-release.ps1` and `test-package.ps1` without the
  Microsoft files and the source manifest; every document the change
  touches, including the statement that the Windows 98 SE CD may be asked
  for; the `1.0.0.1` entry in `releases/history.md`.

Checkpoint: the package `make-package.ps1` assembles holds `xhci98.sys` and
`xhci98.inf` and no other file; the INF gate and its self-tests are green on
the new shape; and the install with the OS supplying the two files has been
observed on Windows 98 under both USB 2.0 stacks and on Windows 2000, in the
VMs, with the root hub up afterwards.

Records: `legal-provenance.md` section 5; `build-and-test.md` ("The files
the OS supplies: `usbport.sys`, `usbd.sys`, `usbhub.sys` and `usbui.dll`",
"The SweetLow stack");
`releases/history.md`.

## Phase 18 - Release `1.0.0.1`: Windows ME, and the Cut

Goal: the driver observed on a Windows ME guest with its standing stated in
every document that names the targets, and `1.0.0.1` cut carrying Phase 17's
install change together with the Windows ME support that observation
justifies.

Status: closed on 2026-09-02, the day it opened, by the owner, who decided
that morning that Windows ME support is part of `1.0.0.1`. The guest was
installed and observed under SweetLow's stack only (the owner's decision:
NUSB is a Windows 98 SE package), the tier decided as supported in virtual
machines, stated the way Windows 2000's status is, and `1.0.0.1` cut and
its install route checked from the asset on all three targets. Nothing has
run on Windows ME on real hardware.

Why a phase: Windows ME is the same 16-bit setup engine and VxD-hosted WDM
model as Windows 98 SE, so the INF's undecorated half is the half it reads,
but its CD carries no USB 2.0 stack and the import gate held no Windows ME
evidence, so the load itself was the first thing to observe, and what the
observation justifies decides how every document names the targets.

Tasks:

- [x] 18.1 install a Windows ME guest by hand (`vm\winme.img`, snapshot
  `winme-clean-install`), then SweetLow's stack from the transfer drive.
  Done 2026-09-02. Two vehicle facts, recorded in `build-and-test.md` and
  `lessons.md`: the Windows ME CD's own `FORMAT C:` never writes a sector
  under QEMU, so the format is taken from the Windows 98 SE CD's boot floppy
  with the ME CD as the second CD-ROM (`setup-qemu.ps1 -WinMeIso -Win98Iso`
  writes that launcher); and every guest-initiated restart wedges at the
  logo as Windows 98's does, LINT0 masked after the warm reset, so every
  restart is a shutdown and a cold launch. Observed first on the stock
  stack: the package installs and the controller shows Code 2 with
  `DriverEntry` never run, `usbport.sys` being absent.
- [x] 18.2 the driver through the INF (`prepare-image.ps1 -Target 2e -Boot
  -Xfer -XferPackage`): no CD asked for (the OEM Setup leaves the CABs on
  the hard disk), and after a cold start `DriverEntry`,
  `USBPORT_GetHciMn=10000001`, `USBPORT_RegisterUSBPortDriver status=0`,
  `StartController`, the `RH_*` family, and the keep-alive mouse bound.
- [x] 18.3 HID, mass storage and one composite device: the mouse at boot,
  `-Attach storage` bound with no wizard, a hot-plugged `usb-audio` bound as
  "Composite Device" (Windows ME's own `usbccgp` parent) with "USB Audio
  Device" under Sound; no refusal counter moved.
- [x] 18.4 the tier, decided by the owner: supported in virtual machines,
  under SweetLow's stack only, no checkpoint tax; stated in `AGENTS.md`,
  `README.md`, the release notes, both issue forms, the INF header comment,
  the generated `readme.txt` and the acceptance test (rows 4.5, 7.7, 7.8).
- [x] 18.5 first-class only: not applicable.
- [x] 18.6 the `1.0.0.1` history entry carries the Windows ME line; the cut
  fell on the date the three fields already carried, so none moved.
- [x] 18.7 cut `1.0.0.1` with `make-release.ps1`, every gate green
  (re-cut the same day for readme wording, before any upload):
  `releases/1.0.0.1/` and `out\xhci98-1.0.0.1.zip` (245,067 B), the two
  files per flavour, the two tools with their readmes and NOTICEs, `LICENSE`
  and `readme.txt`, nothing else. The published `xhci98.sys` differs from
  `1.0.0.0`'s only in timestamps, checksum and version resource (22 bytes),
  so the release changes the install route, and that was run from the asset
  the same night, the owner at the console: Windows 98 SE (a fresh
  `post-nusb` clone, no CABs) asked for `usbd.sys` with the CD prompt and
  came up with the root hub clean and the mouse enumerated; Windows 2000
  SP4 (a fresh `phase2b-clean` clone) asked for nothing, root hub and mouse
  working; Windows ME (`winme-clean-install`, SweetLow's stack first) asked
  for nothing, controller, root hub and HID clean. The Windows 98 run with
  the CABs present was not made (no image carries them), and the nine-step
  acceptance test is the post-release reminder below, taken from the
  published download.

Checkpoint: a Windows ME guest boots the driver, the root hub comes up, a HID
device and a mass-storage device work, and the tier is stated; and the asset
`make-release.ps1` assembles for `1.0.0.1` holds `xhci98.sys`, `xhci98.inf`,
the two tools and the readmes and no other file, with the install route
checked on each target from that asset.

Records: `build-and-test.md` ("Windows ME target VM"); `lessons.md`
("Windows ME on QEMU"); `scripts/vm-matrix/README.md`; `releases/history.md`.
## Phase 19 - Release `1.0.1.0`: Windows XP, and the NT Install Fixes

Goal: the driver observed on a 32-bit Windows XP guest with its standing
stated in every document that names the targets, and `1.0.1.0` cut carrying
the two INF changes that guest showed an xHCI-only NT machine needs (the
operating system supplying `usbport.sys`, `usbd.sys` and `usbhub.sys` on the
NT install path, and that path disabling usbport's idle suspend as the
Windows 98 path already does), together with one driver change, issue 4's
identity check on the EP0 REMOVE path.

Status: closed on 2026-09-04, its second day, by the owner. It opened on
2026-09-03 as `1.0.0.2`, when the owner asked whether XP could be supported
and installed the guest by hand that afternoon, and was renumbered to
`1.0.1.0` the same night because task 19.7 makes it a driver code change
(the third field moves for code, the fourth for install media and
documents). XP is supported in virtual machines, stated the way Windows 2000
and Windows ME are, and `1.0.1.0` was cut and its install route read from
the asset on all five targets. Nothing has run on XP on real hardware; the
upload and the push are the owner's.

Why a phase: `win98-wdm.md` had kept XP best-effort for cost, and the first
guest settled the runtime side in an afternoon: the driver registered and
ran under XP's own usbport once that file was on the disk. What the guest
also showed were two install-time gaps that belong to the NT path, not to
XP. An NT install that never saw a USB controller has no `usbport.sys`
(both targets' `layout.inf` give it, `usbhub.sys`, `usbehci.sys` and
`usbd.sys` the disposition Setup does not copy, so the file reaches the disk
only when a controller's install pulls it from the driver cache), and on an
xHCI-only machine the package installed but could not load, Code 39; every
Windows 2000 vehicle in this project had carried an EHCI that hid it. And
XP's usbport idles a controller with nothing attached about thirty seconds
after start, after which a hot-plugged device is invisible, the reading the
SweetLow record had predicted for an XP-lineage usbport without
`DisableSelectiveSuspend`. Fixing both changes the INF, its gate, the
release notes and every statement about what the OS supplies, and needs an
install reading on an xHCI-only guest of each NT target, which no existing
image could give.

Tasks:

- [x] 19.0 the XP guest recorded before anything changed: `build-and-test.md`
  "Windows XP target VM" (WHPX, the two launchers from
  `scripts\setup-qemu-winxp.ps1`, the Code 39 reading, the suspend reading),
  `lessons.md` with the `layout.inf` disposition table, `win98-wdm.md`, and
  the SweetLow table's suspend row. Done 2026-09-03.
- [x] 19.1 the INF, NT path, file placement: `[Xhci.CopyNT]` copies
  `usbport.sys`, `usbd.sys` and `usbhub.sys` through `LayoutFile` (the
  owner's instruction); in the gate `OS-ONWIN2K` retired, `OS-MISSING`
  extended to the three, `OS-ONWIN98` keeping `usbport.sys` off the Windows
  98 path (its `layout.inf` cannot resolve it), `OS-NEVER` for `usbhub20.sys`
  on no path (the owner's decision: Windows 2000's own `USB.INF` places it
  when usbport creates the root hub PDO). Landed 2026-09-03.
- [x] 19.2 the INF, NT path, idle suspend: `Services\USB\DisableSelectiveSuspend`
  written from `[Xhci.Dev.NTx86]` and `[DefaultInstall.NTx86]` as the 9x
  path has done since `1.0.0.0`, the gate requiring it once per route on
  both targets; `HcDisableSelectiveSuspend` considered and not taken (under
  NUSB the per-controller value alone still idled the controller). Read
  first with the value hand-set, then from the 19.4 package install: no
  `SuspendController` in two minutes idle, and a mouse hot-plugged after
  them bound.
- [x] 19.3 the XP readings on the qemu flavour (run `p194`): the Device
  Manager door sequence (disable, enable, remove, rescan) clean, where
  Windows 98 under NUSB bugchecks; `usb-storage` formatted, written and read
  back; `usb-audio` bound as a composite with its isochronous endpoint
  opened from the descriptor. Both bound on their second attach only: on
  the first, XP re-created the device through a second usbport device
  handle and removed the first handle's EP0 last, which the REMOVE path
  read as the live pipe closing
  (`docs/issues/04-xp-restore-device-ep0-remove.md`). The default 4+4 port
  layout had put the SuperSpeed-capable disk on an unmanaged USB3 port, so
  the launcher defaults to `p3=0` since.
- [x] 19.4 the fix observed on XP: `winxp-clean-install`, the launcher with
  no EHCI, Have Disk from the transfer drive: no CD prompt, no reboot,
  `usbport.sys` and `usbhub.sys` placed from `sp3.cab` beside XP's
  `usbd.sys` stub, the driver loaded on that boot, "USB Root Hub" installed
  by XP's own `usbport.inf`, the value under `Services\USB`, no
  `SuspendController` in two minutes, and the hot-plugged mouse bound.
- [x] 19.5 the fix observed on Windows 2000: a fresh SP4 install with no USB
  controller (`vm\win2k-xonly.img`, `win2k-xonly-clean-install`, the new
  `2b-fresh` base; `scripts\setup-qemu-win2k-xonly.ps1`). Its disk, read
  from the snapshot without a boot: `usbcamd.sys` and `usbintel.sys` and no
  other `usb*.sys`, not even `usbd.sys`; the four stack files in `sp4.cab`.
  The package install with the xHCI alone (run `p195`): no CD prompt, no
  reboot, `usbport.sys`, `usbd.sys` and `usbhub.sys` from the cab, "USB 2.0
  Root Hub" up through the OS's own `USB.INF` placing `usbhub20.sys`, a
  mouse bound, every refusal counter at zero.
- [x] 19.6 the tier, decided by the owner on 2026-09-03 night: supported in
  virtual machines, stated the way Windows 2000 and Windows ME are, in
  `AGENTS.md`, `README.md`, the release notes, the INF header comment, the
  `readme.txt` template, the acceptance test (rows 4.6 and 7.9 to 7.12),
  `win98-wdm.md` and `build-and-test.md`.
- [x] 19.7 issue 4, the one driver change (the owner's decision of 2026-09-03
  evening, reversing the afternoon's): in `XhciSlotSetEndpointState`'s
  REMOVE branch for the default pipe, a REMOVE whose extension is neither
  NULL nor the one the record is bound to names a superseded handle, so it
  closes that extension alone and counts `Ep0RemovesSuperseded`, leaving
  the binding, the owed invalidate, the EP0 queue and any pending
  SET_ADDRESS to the live handle. Two host vectors model the `p194` order
  and failed on the old path exactly as the run did. The closing
  reading (run `i4b`, a clean-snapshot reinstall): the restore recurred on
  the first attach of both `usb-storage` and `usb-audio`, the counter moved
  to 1 and 2, and both bound with every failure counter at zero. Both
  primary targets on the same binary (`run-matrix.ps1` on 2a and 2b, the
  Windows 98 door sequence on the SweetLow guest; `out\phase10\matrix-19.7-*.txt`
  and `prep-2a-sweetlow-debugcon-19.7-door.log`): the counter at zero
  throughout, nothing changed. What the runs left is a correlation: every
  first attach that took the restore path was also the first-ever install
  of that class driver.
- [x] 19.8 the primary targets unchanged: `run-matrix.ps1 -PostRelease` on
  freshly re-taken 2a and 2b clones, 2b from the xHCI-only base: `2b-fresh`
  PASS, 17 rows, 6 NODRIVER expected, 0 against, the report identical to
  Phase 16's outside its header; `2a-fresh` PASS, 17 rows, 5 NODRIVER
  expected, 3 not reached (the declared exclusions), 0 against, identical
  to Phase 16's except that the `usb-audio/fs` replug leg passed where
  Phase 16 read the Insert Disk failure the release notes carry, one better
  reading recorded and not promoted. A first run had read that row ERROR on
  both targets because `device_add usb-audio` with no `audiodev=` opened
  QEMU's default host backend and stalled the monitor; the run declares
  `-audiodev none` since. Reports in
  `docs\contributing\runs\run-19-post-release\`.
- [x] 19.9 the cut, 2026-09-04 (re-cut the same morning
  for the readme's opening paragraph, before any upload): the date in
  `src\xhci_version.h`, the INF's `DriverVer`, the history heading and the
  release notes; `build-driver.cmd all` and both tools rebuilt after the
  header, every gate green, `make-release.ps1` exit 0: `releases\1.0.1.0\`
  and `out\xhci98-1.0.1.0.zip` (244,237 B, thirteen files, no Microsoft
  file). The install route from that asset the same morning, the owner at
  the console: Windows 98 SE under NUSB (a fresh `post-nusb` clone) asked
  for `usbd.sys` with the CD prompt and came up with the root hub clean and
  the mouse bound; Windows 98 SE under SweetLow (over the installed driver)
  and Windows ME asked for nothing, controller, root hub and HID clean;
  Windows 2000 (a fresh clone of the xHCI-only base) asked for nothing,
  root hub and mouse working; XP (`winxp-clean-install`) asked for nothing,
  a mouse attached after the install bound with no Refresh, and a
  mass-storage device bound on its first-ever attach, disk and volume in
  turn. Screens in `out\post-release\1.0.1.0\asset-legs\`.

Checkpoint: on an XP guest that has never had another USB controller, the
`1.0.1.0` package installs from the asset, the driver loads on the first
boot with `usbport.sys` supplied by the OS from its own cache, the root hub
comes up, a HID device hot-plugged after the idle window binds, and a
mass-storage device works on its first attach (issue 4); the same install
reading on a Windows 2000 SP4 guest that has never had another USB
controller; the three 9x-family install routes unchanged from the asset;
the tier stated; and the asset holds `xhci98.sys`, `xhci98.inf`, the two
tools and the readmes and no other file.

Records: `build-and-test.md` ("Windows XP target VM", "Windows 2000
xHCI-only VM", "The files the OS supplies"); `lessons.md`; `win98-wdm.md`
("What about Windows XP?"); `usbport-miniport-interface.md` ("The SweetLow
rebuild"); `docs/issues/04-xp-restore-device-ep0-remove.md`;
`scripts/inf-gate/`; `releases/history.md`;
`docs/contributing/runs/run-19-post-release/`.

## Phase 20 - Release `1.0.2.0`: The 2026-09-05 Audit Fixes

Goal: every finding of the 2026-09-05 repository audit (no
critical defect; nineteen findings F1-F19 and six documentation groups D1-D6)
either fixed with the regression vector that pins it or recorded as an owner
decision with its reason; the gates green; both targets' post-release matrix
no worse than Phase 19's; and the result cut as `1.0.2.0`.

Status: closed on 2026-09-07 on the cut and its guest readings. It opened on
2026-09-05 on branch `phase-20`, renamed to `1.0.2.0` on 2026-09-06, the
third field moving because the phase carries driver code changes. Every gate
is green, the version is cut, the install route from the published download
reads clean on four targets, and the confirming matrix on this release's own
driver matches 20.8's reports but for one transfer count. One checkpoint
clause is met on the development machine rather than in a guest: no guest has
been made to produce the state this release's new control-endpoint refusal
guards, so the guests read its other half, that a legitimate reopen is not
refused. The acceptance test from the download, the upload and the push
remain, and are the owner's alone.

The install gains one file: the OS supplies `usbui.dll` as well, by the same
`LayoutFile` route as the three drivers but to dirid 11 rather than their
dirid 10, the INF gate holding it there. Windows 2000's `USB.INF` and Windows XP's `usbport.inf` already name
that file as the root hub's property-page provider, so on an xHCI-only
machine the reference dangled and the Power tab was silently absent.
Measured in both NT guests (`build-and-test.md`), and the acceptance test
takes the tab at step 4.7.

Why a phase: the findings interact - F3 and F9 are one verdict rule from two
sides, F1 and F8 both table ownership, F6, F7, F18 and D1 the same shipped
statements - so a piecemeal fix on a release branch would repeat their drift.

Tasks, in the audit's revised order, all closed.
[`runs/run-20.md`](runs/run-20.md) is the record: what each task changed, the
vectors behind it, and every reading.

| Task | Subject |
|---|---|
| 20.0 | the matrix verdict (F3, F9, F11) |
| 20.1 | the packaging guards (F4, F14, F15) |
| 20.2 | endpoint and device-table ownership (F1, F8) |
| 20.3 | recovery delivery loss (F2) |
| 20.4 | the shipped statements (F6, F7, F18, D1) |
| 20.5 | the register and tool items (F5, F10, F12, F13, F16, F17) |
| 20.6 | the smaller items and D2-D6 |
| 20.7 | the gates and the guest readings; F19 was found and fixed here |
| 20.8 | the Windows 98 audio replug row, read with that target run alone |
| 20.9 | the cut, `usbui.dll`, and the readings on the published asset |

Checkpoint: every finding closed with a cited commit and regression vector or
recorded as an owner decision with its reason; every gate and self-test
green; the post-release matrix on both primary targets no worse than the
Phase 19 reports; the Windows 2000 SMP recovery and XP lifecycle readings
taken for 20.2; and the version cut. Not a checkpoint: a host test standing
in for a guest.

Records: `runs/run-20.md`; design records 05, 06 and 07; `build-and-test.md`;
`lessons.md`; `runs/run-20-post-release/`; `releases/history.md`.

## Phase 21 - 64-bit Targets: Windows XP x64 and Server 2003 x64, then Vista x64 and Windows 7 x64

Goal: whether this driver can be an Option A miniport on a 64-bit Windows
settled from the shipping binaries first and a guest second, and - if it can
- one NT 5.2 amd64 binary observed on a Windows XP Professional x64 guest,
with its standing stated in every document that names the targets. Since
2026-09-09 the goal also asks the same of **Vista x64 and Windows 7 x64**
(task 21.8): 21.7 read their interface and found nothing against them, and
what is left is whether a binary the static pass says should work can be made
to install and load on systems that enforce kernel-mode code signing and stage
drivers through a driver store.

Status: open, 2026-09-09 - **checkpoint passed, the record written, and task
21.8 the only thing left.** It
opened on 2026-09-08 when the owner asked whether a WDK 7.1 build could give
this driver 64-bit guests. Task 21.1 is complete: all six measurements are
read and all six pass, so nothing in the ABI argues against Windows XP x64 and
Server 2003 x64. That was the limit of what a static pass could show, and the
paragraphs below are the record of what the phase then did about it.
Task 21.5's guest was begun on 2026-09-08: `scripts\setup-qemu-winxp64.ps1`,
its two launchers and the 16 GB `vm\winxp64.img` exist and the launcher gate
covers them. It cost one finding already, before any driver: **this guest
needs `-accel tcg`, and WHPX - which every other guest here uses - wedges XP
x64 Setup**, the reverse of the Windows 2000 reading (`lessons.md`, "The
accelerator is the discriminating variable in both directions"). A **compile
scout** was then taken on 2026-09-08 - WDK 7.1's x64 `cl` pointed at `src/`
through `build /L`, compile-to-object only, so none of 21.2's plumbing and no
amd64 import library were needed. It answers the question it was taken for:
**this source produces exactly one diagnostic on amd64**, a `ULONG_PTR` ->
`ULONG` truncation at `xhci_dispatch.c:1001`, so 21.2 is a plumbing job with a
known end. It also found one thing nobody was looking for -
**`USBPORT_ENDPOINT_PROPERTIES` changes size on amd64 and none of M1-M6
measured it** - which adds a reading to 21.4. Design record 11 section 8, "The
compile scout", has it. Task 21.7 is complete as of 2026-09-09: the same six
measurements taken on Vista x64 and Windows 7 x64, and **all six pass on
both**, so one `WNET` amd64 binary can serve them too and nothing here
narrows to Windows XP x64 and Server 2003 x64. It cost only the extraction,
and it found one thing worth carrying: **Windows 7's usbport has a second
`IoGetDmaAdapter` call that asks for a 64-bit adapter**, reachable only by a
miniport declaring `Version >= 310`, which is the one path by which a future
change here could move physical addresses above 4 GB. That reading was taken
when nothing else in the phase had started and no binary existed.

**That order was taken on 2026-09-09 and it held.** The
`USBPORT_ENDPOINT_PROPERTIES` reading went first and became M7; the rest of
21.4's declaration work followed from it; then 21.2's plumbing, which is
indeed where the weight was. **An amd64 `xhci98.sys` now exists in all three
flavours** and the x86 binaries were held byte-identical across the whole
change - `.text`, `.data`, `INIT`, `.rsrc` and `.reloc` unchanged against the
published `1.0.2.0` pair, differing only in the enumerated PE metadata bytes a
re-link always moves. The first build then made 21.3 larger and more
interesting than the plan assumed - the amd64 import surface is seven
module/symbol pairs against x86's eleven, and the differences are structural -
and **21.3 was closed the same day**: the import gate, the library generator,
a second INF, and both packagers.

21.4's last two boxes closed the same day: design record 04's arithmetic was
re-run against the amd64 `sizeof`s and does not move - nothing in the
common-buffer layout is a `sizeof` - and the host suite now compiles and runs
`test_packet` and `test_membuf` for amd64 as well as x86, so the `_WIN64` half
of the ABI declaration is checked on the build host instead of only inside a
driver build.

**And then task 21.5 ran it, the same evening, and every checkpoint clause
passes.** An amd64 `xhci98.sys` has now installed on an xHCI-only Windows XP
Professional x64 SP2 guest, registered with that system's own `usbport.sys`,
started its controller, passed its No Op self-test, answered the root-hub
family, bound a HID mouse, a mass-storage device and a composite audio device,
and survived the Device Manager disable/enable/remove/rescan sequence - with
321 transfers completed and every refusal and error counter zero.

**It cost one defect, and it was the one thing in the phase still running on
an assumption.** `USBPORT_SCATTER_GATHER_LIST`'s amd64 layout had never been
read off a binary; the compiler's guess was four bytes wrong in the element
array's offset, and the first amd64 run refused every control transfer because
of it. It is now measurement **M8**, taken statically off the producer in the
NT 5.2 amd64 `usbport.sys`, and the fix is `_WIN64`-only - the x86 binary does
not move. Task 21.5 has the account.

**The publisher was then changed, on the owner's instruction and not as a
consequence of the checkpoint.** `make-release.ps1 -Arch` defaults to both
architectures since 2026-09-09, so an ordinary cut publishes four directories.
The step that licensed it was not the checkpoint alone but the **release**
flavour leg above: the clauses had been taken on the `qemu` build, and cutting
a package on the strength of a flavour that is never published would have been
the one soft spot in the phase. One consequence came with it and is fixed:
`-UploadSetOnly` re-assembles an already-published version, every such version
is x86-only, and under a both-arch default it began demanding a `release-x64\`
those cuts never had - so that mode now derives the architectures from the
published tree unless `-Arch` is passed.

So Phase 21 is a target observed and a package that can be cut. What may be
written is that the binary has been observed running: in one virtual machine,
on one guest, never on real hardware, and the x64 half of any cut carries
exactly that standing against the x86 half's four install legs. **The cut
itself is not this phase's.** `1.1.0.0` is published by Phase 22, on the
owner's instruction of 2026-09-09, because this phase closes on task 21.8's
guests and a release should not wait on a leg whose answer changes nothing
about what is being published.

**One task is open, and the phase closes on it.** 21.6, the record, was
written on 2026-09-09: the tier is stated in `AGENTS.md`, `build-and-test.md`,
`win98-wdm.md`, the release notes and `README.md`, and what it cost was
saying in each of them the thing this tier does not share with the other four
- that it is not the same binary. 21.8
was added on 2026-09-09 on the owner's instruction and is a second guest leg:
Vista x64 and Windows 7 x64, whose interface 21.7 already read and found
nothing against. It is not a repeat of 21.5, because the two obstacles there
are not ABI obstacles - both systems enforce kernel-mode code signing, so an
unsigned driver loads only with enforcement disabled or test-signing on, and
both stage a package through a driver store the `LayoutFile` route was not
written for. Either could end the leg, and a well-characterised "no" is a
complete result.

The version was bumped to `1.1.0.0` in the same session, on the owner's
instruction, as a separate step after that identity was proved - its only
effect on the x86 binaries is eight bytes inside `.rsrc`, measured rather than
assumed. **`src/xhci98.inf` is therefore no longer byte-identical to
`1.0.2.0`'s**, since `DriverVer` moved with it; what decision 2 in design
record 11 section 12 actually buys is unchanged and is the thing that
mattered - no new install path, no widened `[Manufacturer]` line, and no
existing install leg to re-validate.

Why a phase: the same reason Phase 19 was one. A target is not a build. The
static pass has to settle the ABI before any code is written, the guest has
to settle the runtime the static pass cannot, and the gates, the INF and the
packager all have to learn a second architecture before anything can ship.
`design/11-x64-targets.md` is the record; it carries the measurements, the
decision gate they feed, and the five decisions that are the owner's.

Windows XP x64 and Server 2003 x64 are one target, not two: both are NT
5.2.3790, and the WDK ships `lib\wxp\i386` with no `amd64` counterpart, so
NT 5.2 (`WNET`) is the only route to a 64-bit XP driver. Vista x64 and
Windows 7 x64 are a separate question and a second leg, because both enforce
kernel-mode code signing and both stage driver packages differently. Task
21.7 reads their binaries by the same static method, because doing so costs
only the extraction; nothing else in the phase waits on it.

Tasks. A box is ticked only on the evidence its line names; `21.1`'s boxes
are static readings and no other task's box may be ticked on one.

- [x] **21.1 - the static ABI pass.** Read 2026-09-08 from
      `tools/winxp64-extracted/`, NT 5.2 amd64 `usbport.sys` and
      `usbehci.sys` 5.2.3790.3959. Method `static` throughout; design record
      11 section 5 has each reading with its RVA and instruction bytes, and
      `usb-xhci-info/usbport-miniport-abi.md` carries the transcription.
  - [x] M1 the two private exports - three names, same three ordinals as
        every 32-bit lineage
  - [x] M2 the `USBPORT_GetHciMn` value - `0x10000001`, already accepted, so
        no code change follows
  - [x] M3 the version gate and copied packet sizes - unchanged gate,
        `0x250` / `0x230`, the exact widening of `0x13C` / `0x12C`
  - [x] M4 the `USBPORT_RESOURCES` layout - `sizeof` `0x48`, and `StartPA`
        stays a 4-byte `ULONG`, so nothing after it shifts
  - [x] M5 the DMA adapter width - still created 32-bit
        (`Dma32BitAddresses = 1`, `DmaWidth = Width32Bits`), so nothing
        lands above 4 GB whatever the guest's RAM
  - [x] M6 the offset map end to end - all 50 slots the amd64 `usbehci`
        fills land on `f(X) = 0x28 + (X - 0x28) * 2`
- [x] **21.2 - the x64 build path.** Done 2026-09-09. All three flavours
      compile and link for amd64 from `tools/WinDDK71` at
      `src/obj<flavour>/amd64/`, with **no diagnostic on any source file** - no
      `build<flavour>.err`, no `.wrn`, and not one `warning Cnnnn` in the log.
      Read the build summary's "21 files compiled - 5 Warnings" against that:
      those five are `build.exe`'s own "x64 Native compiling isn't supported.
      Using cross compilers." notice, counted once per pass, and say only that
      an x64 host is using the x86-hosted cross compiler. Design record 11
      section 8, "The first build".
  - [x] the `qemu` flavour's `__asm` exclusion under `_WIN64`, without which
        that flavour does not compile at all. Both sites and their two locals
        now sit behind one named guard, `XHCI_CHECK_STACK_DELTA`; the check is
        x86-only by nature, since the calling-convention mismatch it watches
        for is a `__stdcall` decoration problem amd64 does not have
  - [x] the first build that links. `build-driver.cmd -amd64` selects WDK 7.1
        and `setenv <root> <flavor> x64 WNET no_oacr`, `make-usbport-lib.cmd
        -amd64` produces `src/usbport_amd64.lib` through the same five
        verification steps (the `_Name@N` decoration check becoming its mirror
        - plain names present, decorated forms absent, machine `8664`), and
        `src/sources` picks the library by `_BUILDARCH`. Both traps the scout
        named held: `BUILD_ALT_DIR` is overridden back to the flavour word
        after `setenv.bat`, which puts the output beside `i386` with no second
        obj root, and `release` was built first precisely because `/WX` is on
        there
- [x] **21.3 - the gates. Complete 2026-09-09.** None of these may be
      skipped for an amd64 binary, and none was. An amd64 binary passes the
      import gate in full in all three flavours, `src/xhci98-amd64.inf`
      exists and passes an INF gate that has learned a second profile, and
      the packager and the publisher both take an architecture. The INF
      gate's self-tests went from 350 checks to 450 and the packager's from
      205 to 243; `build-driver.cmd` now runs the INF gate over **both**
      INFs on every build, whichever architecture it is building.
  - [x] an `amd64` dimension in the import gate, with NT 5.2 amd64 baselines
        behind it as `win2k-baselines.expected` has for SP4. Done 2026-09-09,
        and task 21.2's first build made it concrete: the amd64 binary imports
        **seven** pairs where x86 imports eleven, and the difference is not
        cosmetic. `READ`/`WRITE_REGISTER_ULONG`, `InterlockedIncrement`,
        `KeInitializeSpinLock` and `KeGetCurrentIrql` become intrinsics or
        inlines and vanish; `HAL!KfAcquireSpinLock` and `KfReleaseSpinLock` -
        x86-only fastcall HAL exports - are replaced by
        `ntoskrnl!KeAcquireSpinLockRaiseToDpc` and `ntoskrnl!KeReleaseSpinLock`,
        a different module as well as a different name; and
        `ntoskrnl!KeBugCheckEx` appears, **which was investigated rather than
        allowlisted on sight** (the owner's call). It comes from
        `__report_gsfailure`, WDK 7.1's `/GS` stack-cookie handler: one call
        site in the whole of `.text`, reached only from
        `__security_check_cookie`, issuing bugcheck `0xF7`
        (`DRIVER_OVERRAN_STACK_BUFFER`). So it is unreachable except on a real
        stack-buffer overrun and is a safety feature the x86 binary cannot
        have, MSVC 6.0 predating `/GS` entirely - which is why it is accepted
        rather than the flag being turned off to match x86. What was built:
        `scripts\import-gate\xhci98-imports-amd64.allow` (**a sibling file,
        not an arch column** - the owner's decision; only 2 of the 13
        kernel/HAL rows are shared, and the x86 file's 27 denials are
        justified end to end by absence on Windows 98 or by blocking the load
        on Windows 2000, reasoning that does not transfer),
        `winxp64-baselines.expected` and its three authenticated files, an
        `-Amd64Iso` arm in `extract-target-baselines.ps1`, and an `-Arch`
        dimension in `check-imports.ps1`. **The evidence rule for amd64 is
        stronger than the Windows 98 one rather than a relaxation of it**, the
        second decision the owner took: the Windows 98 rule compensates for a
        target whose export tables are built at run time by `ntkern.vxd` and
        so cannot be resolved against, while NT 5.2 amd64 has a real export
        table, so direct resolution against both kernels and the one HAL is
        the rule and no proxy is wanted
  - [x] arch-conditional checks in `scripts\make-usbport-lib.cmd` - the
        undecorated-name check replacing the `@N` one, the other four steps
        unchanged. Done 2026-09-09 as part of 21.2, and it is the mirror check
        rather than merely a weaker one: the plain names must be present *and*
        the `@N` forms absent, plus a machine check, because a decorated
        symbol in an "amd64" library would mean an x86 library published under
        the 64-bit name
  - [x] the second INF. **The decision was taken 2026-09-09: a separate x64
        package**, so no existing install leg is re-validated and Windows 98's
        engine is never asked about a widened `[Manufacturer]` line. Written
        the same day as `src/xhci98-amd64.inf`: `.NTamd64` throughout - the
        `[Manufacturer]` TargetOSVersion field, the models section, the
        install section, its `.Services`, and the right-click section - and
        the same `LayoutFile` route the 32-bit NT path uses, which task 21.5
        had already read as needed here too.

        **The owed reading was taken first**, because design record 11 said
        it had to be: *whether the x64 media's `layout.inf` carries a
        `usbui.dll` row*. It does. `AMD64\LAYOUT.INF` gives `usbui.dll =
        1,,222222,,,,,2,1,3`, disk 1 being `\amd64` on the base CD, so it
        comes from `AMD64\DRIVER.CAB` at 123,392 B / 5.2.3790.1830 - beside
        `usbd.sys` from the same cabinet, and `usbport.sys` and `usbhub.sys`
        from disk 100's `SP2.CAB`. `usbhub20.sys` has no row, as on 32-bit XP.
        The three sizes that can be cross-checked match
        `tools/winxp64-extracted/` exactly, which is what authenticates it,
        and `7z l vm\winxp64.img -r usbui.dll` on the 21.5 guest returns zero
        files, so the file is as absent there as it is on 32-bit XP. Method
        `static` throughout; `legal-provenance.md` section 4 carries the rows.

        The gate learned the path as a **profile switch, `-Arch`**, not a
        second script - the opposite of the allowlist's sibling file next
        door, and for the opposite reason: there the data forked, here the
        rules are the same rules and only the path list differs. It is not a
        relaxation either. It drops the Windows 98 rules because there is no
        Windows 98 path, and adds two refusals the 32-bit file has no need
        of: **`PATH-NO9X`**, an undecorated install section in the 64-bit
        file, and **`OS-DEFAULT`** extended to refuse an undecorated
        `[DefaultInstall]` there. Both are sections a 32-bit engine falls
        back to, and reaching either puts an amd64 binary on a 32-bit
        machine with a service pointing at it. **`PATH-MFGDEC`** holds
        `[Manufacturer]`'s decoration in both directions, which is what pins
        decision 2 in place: widening the 32-bit file's line now fails the
        build.

        The accepted cost of two files - that they drift - is mechanised
        rather than promised. `test-inf-checks.ps1` compares the two INFs
        directly (hardware ID, service and its five values, both log values
        and their defaults, the selective-suspend value, the OS-supplied
        file lists and flags, `[SourceDisksFiles]`, the `[Version]`
        identity, every shared `[Strings]` token) and asserts each file is
        **refused** under the other's profile - the check without which the
        two profiles could accept everything and distinguish nothing.
        `expected-footprint-amd64.txt` is the 64-bit package's own tracked
        footprint, task 11-V.3's claim for the second media
  - [x] the packager and the publisher staging a second architecture. Done
        2026-09-09 to the shape decided the same day: `releases\<version>\`
        carries `release-x86`, `debug-x86`, `release-x64` and `debug-x64`,
        four self-contained directories each with its own `xhci98.inf` and
        `xhci98.sys`, the x86 pair renamed so that no untagged directory
        silently means x86 - free now, because nothing has been uploaded, and
        `releases/README.md`'s write-once rule leaves the four cut versions
        alone.

        `make-package.ps1` gained **one** `-Arch` that moves three things
        together and cannot be split: the obj subdirectory, which of the two
        INFs is staged, and the architecture both gates run under. Three
        switches that could disagree would have had a silent wrong answer -
        an amd64 binary staged around the 32-bit INF installs on a 32-bit
        machine and then fails its load with no diagnostic - and the
        packager's self-tests drive exactly that mismatch in both directions
        and assert the refusal. Its default output carries the architecture
        too (`out\pkg-release-x86`), for the same reason the published
        directory does.

        `make-release.ps1` was the larger half. Every place the flavour word
        was a directory name now goes through a **release leg**, one object
        per published directory carrying its flavour, architecture, obj tree
        and INF; the identical-hash refusal compares within an architecture,
        where a collision is the only kind that could happen; and the
        generated `readme.txt` names the directories the cut actually wrote
        rather than a hardcoded `RELEASE\`, with a placeholder-completeness
        check so a template token can never render literally into a
        user-facing file.

        **`-Arch` defaulted to `x86` alone, and that default was asserted by
        the self-tests** - the plumbing staged four directories the moment it
        was asked to (`-Arch x86,x64`), and what had not happened was task
        21.5, so an ordinary cut published exactly what `1.0.2.0` did.
        Changing that line was the deliberate act a passing 21.5 licenses, and
        it was taken on 2026-09-09 once the release flavour had been read on
        the guest too. The assertion moved with it rather than being deleted
- [x] **21.4 - the code changes task 21.1 implies. Complete 2026-09-09**
      (design record 11 section 9). Five of the seven were done with 21.2;
      the last two, both of which needed no guest, were done the same day.
      **An eighth followed the same evening and did need a guest** - the
      `_WIN64` `USBPORT_SCATTER_GATHER_LIST` layout, which no measurement had
      covered until the guest forced M8. It is recorded under 21.5 rather than
      here, because what makes it worth reading is how it was found.
  - [x] the `_WIN64` packet declaration at the measured `0x250`. Reached by
        widening the two trailing `Reserved` canaries to `ULONG_PTR`, which
        M6 records as one of the two valid ways and which leaves the x86
        layout untouched. The asserts carry M3's and M6's measured numbers on
        each architecture rather than the compiler's own, so the eight-byte
        shortfall the scout found can actually fail one
  - [x] **`USBPORT_ENDPOINT_PROPERTIES` read off the amd64 `usbehci.sys`** -
        design record 11 section 5, **M7**, read 2026-09-09, method `static`.
        `sizeof` `0x48` measured from `OpenEndpoint`'s nine-qword copy rather
        than inferred, `BufferVA` at `0x20` read as a QWORD, `BufferPA` still
        4 bytes at `0x28`, and the TT pair at `0x38`/`0x3A`. The compiler's
        natural widening turned out to be right, so no `#ifdef` was needed -
        what the reading bought is the right to assert
  - [x] `USBPORT_RESOURCES` under `_WIN64` at M4's measured `0x48` -
        `InterruptAffinity` is now a `ULONG_PTR`, being a `KAFFINITY`, which
        is the whole of the gap and is identical on x86
  - [x] `src/xhci_dispatch.c:1001`'s implicit `ULONG_PTR` -> `ULONG`
        truncation, the one thing the amd64 compiler objects to in this
        source, and fatal in `release` only
  - [x] `src/xhci_compat.h`'s `ULONG_PTR` typedef guarded for 64-bit hosts.
        Windows is LLP64, so `unsigned long` stays 32 bits on amd64; the old
        unconditional typedef was accidentally correct only because the host
        suite has always been built x86
  - [x] design record 04's common-buffer arithmetic re-run against the amd64
        `sizeof`s and the result stated. **Nothing in the layout moves, and
        the reason is that nothing in it is a `sizeof`**: every region offset
        and size is a spec constant times a declared policy limit, written as
        a `UL` literal, and the only two `sizeof`s the carve touches are
        `XHCI_TRB` and `XHCI_ERST_ENTRY`, four `ULONG`s each. So
        `MiniPortResourcesSize` is 409,600 bytes on both architectures and
        usbport is asked for the same 101 pages on both. Measured rather than
        argued - the same headers through both compilers - and it is now a
        standing check rather than a reading, since `test_membuf` is one of
        the two suites the box below runs twice. What the pass did find is
        outside the buffer: the extensions usbport allocates for the miniport
        grow (`XHCI_EXTENSION` 91,612 -> 95,496, `XHCI_TRANSFER` 120 -> 152),
        which needs no code change because `DriverEntry` publishes each as a
        `sizeof`, but does mean the snapshot channel's x86 offset table cannot
        decode an amd64 extension. Design record 04 section 8 states the
        result and flags that for whoever first runs an amd64 binary
  - [x] `test/test_packet.c` compiling the header for amd64, so
        `test\run-host-tests.cmd` checks the layout on the build host
        rather than only inside a driver build. Done 2026-09-09, and it runs
        the file rather than only compiling it: **191 checks pass on each
        architecture**, with every expectation that differs carrying both
        numbers - M4's `USBPORT_RESOURCES` table, M7's
        `USBPORT_ENDPOINT_PROPERTIES` table, M3's packet size and short-copy
        boundary, and M6's widening map for the 50 callback slots between
        hand-typed anchors. `test_membuf` is built and run for amd64 by the
        same leg, for the opposite reason - it is where the numbers must NOT
        move - and passes 2,027 there. Two limits on the leg: it drops
        `/Za`, because the WDK's own CRT headers are not C89-clean and the
        x86 leg compiles the same files under it anyway; and it is skipped
        by name, with the run's verdict reading "x86 only", on a host with no
        WDK 7.1, since `tools/` is fetched per host rather than cloned
- [x] **21.5 - the Windows XP x64 guest. COMPLETE 2026-09-09, and it cost one
      defect.** Every checkpoint clause below was taken on the guest that
      evening, on host `minis-w11p-ykm`, with the `qemu`-flavour amd64
      `1.1.0.0` package staged to `vm\xferxp64`. The guest was a vehicle and
      building it was preparation; this is the leg.

      **The first amd64 binary ever to run did everything the static pass
      predicted and then refused every control transfer.** It installed on an
      xHCI-only machine, registered, started, passed its No Op self-test and
      answered the whole root-hub family - and then failed each device
      enumeration at the first control transfer with
      `slot: control transfer refused by the builder, status=5`
      (`XHCI_XFER_SG_HIGH_ADDRESS`). On a 2048 MB guest no physical address
      above 4 GB exists, so the driver was reading the wrong bytes. The cause
      was the one structure design record 11 had carried as an assumption
      rather than a reading - `USBPORT_SCATTER_GATHER_LIST` - and it is now
      **M8**: `SgElement[]` is at `0x20`, not the `0x1C` an all-`ULONG`
      declaration produces, and the element's `SgTransferLength` and
      `SgOffset` are each four bytes higher than on x86 while its `sizeof`
      stays 24. Read statically off the producer in the NT 5.2 amd64
      `usbport.sys` at RVA `0xF468`, fixed in `src/xhci_usbport.h` under
      `_WIN64` alone, and now asserted there and in `test/test_packet.c` on
      both architectures. The x86 binary does not move: two builds of
      identical source differ in the same six bytes as a build across the
      change, three import-table RVAs at the head of `.text`.

      **Two things are worth carrying out of that.** The refusal came from the
      scatter-gather high-DWORD check, which guards a condition M5 says cannot
      arise and which design record 11 section 9 item 3 calls optional on the
      evidence; it caught a defect nobody had predicted, on the first transfer,
      cleanly and with a counter, instead of a TRB built from two unrelated
      halves. And the reading needed a technique: a linear sweep of an amd64
      `.text` desynchronises and lies, so the function boundaries come from
      `.pdata` and each one is disassembled from its true entry with
      `kd -z` - this DDK's `dumpbin` has no amd64 disassembler. Both are in
      `lessons.md`.
  - [x] the guest installed from `scripts\setup-qemu-winxp64.ps1` and
        snapshotted `winxp64-clean-install` - 2026-09-08, the owner at the
        console for the product key, guest shut down from inside and the
        snapshot taken with the image cold (`qemu-img check`: no errors,
        13.36% of 16 GB allocated; snapshot ID 1, `VM_SIZE` 0 B, which is
        what a powered-off snapshot should read). The static listing taken for
        the readings below then corroborated the install independently: an
        11,767-file NTFS volume with `Documents and Settings\Administrator`,
        a System Restore `RP1`, and `Prefetch` entries for `CTFMON`,
        `RUNDLL32`, `MSIEXEC` and `MMC` timestamped after the last reboot -
        that is a system that reached a desktop and ran programs on it. **The
        installed system has still not been booted from the run launcher
        here**, so nothing about the xHCI is known yet
  - [x] the USB stack the guest will install hashed against
        `tools/winxp64-extracted/`. **All three match byte for byte**
        (`sha256`): `usbport.sys` `6fc83f49...05e1d`, `usbhub.sys`
        `92b1744e...30198`, `usbehci.sys` `657daf4a...83d9a`. Read
        2026-09-08 from the `winxp64-clean-install` snapshot, statically,
        with no boot: 7-Zip 26.00 lists straight through the qcow2's MBR and
        NTFS, so no `qemu-img convert` was needed. **Taken from
        `Driver Cache\amd64\sp2.cab` rather than from an installed copy,
        because there is no installed copy** - which is the box below
  - [x] whether an xHCI-only XP x64 install has `usbport.sys` on disk at all
        - **it does not, exactly as 32-bit XP.** No `usbport.sys`,
        `usbhub.sys`, `usbehci.sys` or `usbd.sys` anywhere in the 11,767-file
        image, `dllcache` included; the only `usb*.sys` present are
        `usb8023.sys` and `usbcamd2.sys`. They sit in
        `WINDOWS\Driver Cache\amd64\` - `usbport`/`usbhub`/`usbehci` in
        `sp2.cab`, and `usbd.sys` (7,552 B, RTM-dated) in `driver.cab`, not
        `sp2.cab`. So `Driver Cache\amd64` does behave like
        `Driver Cache\i386` and the INF's `LayoutFile` route is needed on the
        `.NTamd64` path too. **One difference from 32-bit XP worth carrying:**
        there, `usbd.sys` was on disk as a 4,736-byte stub; here it is not on
        disk at all
  - [x] the checkpoint clauses below, on the amd64 binary. Taken 2026-09-09,
        all seven, on `1.1.0.0` with the M8 fix. The owner drove the GUI (the
        standing 2026-09-03 decision); the hot-plugs and the readings were
        taken over the monitor on port 55562, and the trace is the
        `qemu`-flavour port-`0xE9` log
  - [x] **and then the same again on the `release` flavour**, taken the same
        evening on the same guest, because the clauses above were taken on the
        `qemu` build and **that flavour is never published**. Without this the
        64-bit package would have been cut on the strength of a binary no
        released package contains - on amd64 the two are not near-copies
        either, `release` being a free build where `qemu` is checked. Installed
        by Have Disk from the transfer drive's `RELEASE\` directory (94,720 B,
        `sha256 6dac158d...d7b5`, byte-identical to `src\objfre\amd64`), and it
        read: the mouse, the mass-storage device and the composite audio device
        all bound (**USB Human Interface Device**, **HID-compliant mouse**,
        **USB Mass Storage Device**, **USB Composite Device**, **USB Audio
        Device**), and the disable/enable/remove/rescan sequence survived.

        **How a release flavour is read at all, since it writes no port-`0xE9`
        trace, is the reusable part.** Three independent things say it: the
        boot log holds one `DriverEntry` from the outgoing `qemu` build, then
        `StopController`, then **nothing** - the replacement loaded and never
        printed, and the file stayed at 31,548 bytes through three device
        enumerations and the whole door sequence; the staged binary's hash
        matches the built one; and **QEMU's own `usb_xhci_*` trace witnesses
        what the driver was built not to say** - `slot_enable` 3 -> 9,
        `slot_address` 6 -> 18, `slot_configure` 4 -> 12 across the sequence,
        exactly three full re-enumerations of three devices. The silence and
        the trace are complementary: one proves which flavour is loaded, the
        other proves it is working
- [x] **21.6 - the record. Done 2026-09-09.** The tier is stated where
      Windows ME's and 32-bit XP's are stated: `AGENTS.md` "Project Purpose"
      and its Quick Reference, `build-and-test.md` "Windows XP x64 target
      VM", `win98-wdm.md`'s new "And Windows XP x64?" beside "What about
      Windows XP?", and `docs/using/release-notes.md`. `README.md` took a row
      in its own target table with them, because that table states the same
      thing to the same reader. The provenance is beside it in
      `legal-provenance.md` section 3, whose amd64 paragraph now records that
      a 64-bit binary has *run* and that this upgrades no `static` tag - the
      rows themselves (M7, M8, the two `LayoutFile` readings, the NT 5.2
      amd64 material, WDK 7.1) were already there from 21.1-21.5.

      **The wording could not simply be reused, and the difference is the
      point of the entry: this is the only target that is not the same
      binary.** Every one of those documents says so, because the sentence
      the other four tiers rest on - one binary, coded to the Windows 98
      export baseline, therefore safe everywhere above it - has nothing to
      say about a kernel Windows 98 never had. Two narrower qualifications
      travel with it: only XP x64 was booted, so Server 2003 x64 rests on the
      NT 5.2.3790 identity rather than on a run of its own; and Vista x64 and
      Windows 7 x64 are **outside** the tier, which is task 21.8's question
      and the open half of design record 11's decision 1. Three further
      places were corrected in the same pass rather than left to drift:
      `AGENTS.md`'s "a single `xhci98.sys` binary" now says which targets it
      means (design record 11 section 11 asked for exactly that),
      `AGENTS.md`'s Compiler and DDK rows name WDK 7.1 as the amd64
      toolchain, and `xhci-data-structures.md`'s "the driver runs 32-bit
      only" is now the measurement it rests on (M5) rather than a claim the
      amd64 build appears to contradict. The user-facing copy of the release
      notes - `make-release.ps1`'s readme template, which the script's own
      comment binds to that file - gained the same statements behind three
      placeholders that render empty on an x86-only cut, because
      `-UploadSetOnly` re-renders the readme of already-published versions
      and every one of those is x86-only. Packager self-tests still pass.
- [x] **21.7 - the same six measurements on Vista x64 and Windows 7 x64.**
      Read 2026-09-09, extraction and disassembly only, no VM and no build.
      **All six pass on both**, so one `WNET` amd64 binary can serve them as
      far as this interface goes and the claim is not narrowed. Design record
      11 section 6 has the outcome and its consequences; the transcription is
      in `usb-xhci-info/usbport-miniport-abi.md`, "The 6.0 and 6.1 lineages".
      Method `static` throughout.
  - [x] Vista x64 - `usbport.sys` and `usbehci.sys` extracted from
        `sources\install.wim` image 1, hashed, version-stamped
        (6.0.6002.18005), and the six read
  - [x] Windows 7 x64 - the same, 6.1.7601.17514
  - [x] recorded, and three differences from NT 5.2 amd64 with them, none of
        which reaches a miniport that registers with `Version = 200`: a fourth
        export (`DllInitialize`) shifting all three ordinals, two further
        packet version tiers above `0x250`, and - on 6.1 only - **a second
        `IoGetDmaAdapter` call that asks for a 64-bit adapter**, gated on the
        miniport declaring `Version >= 310` and filling packet slot `0x398`.
        The tier claimed is unchanged, and so are the two things true of both
        regardless: kernel-mode code signing, and the driver-store install
        path the `LayoutFile` route was not written for - though the media
        also shows both systems shipping `usbport.sys`, `usbhub.sys`,
        `usbd.sys` and `usbehci.sys` in `System32\drivers` outright, which is
        the opposite of an xHCI-only XP or 2000 install
  - [x] **the `i386` halves taken in the same pass** and handed to task 22.1,
        together with each system's `ntoskrnl.exe` and `hal.dll` for task 22.2
- [ ] **21.8 - the Vista x64 and Windows 7 x64 guests**, on the owner's
      instruction of 2026-09-09. Task 21.7 read the interface and found
      nothing against them; this is the leg that finds out whether the binary
      runs. **It is a second leg, not a repeat of 21.5**, and the reason is
      that neither of the two things 21.7 named as true regardless is an ABI
      question - both are install-and-load questions that only a guest
      settles, and one of them may not have an acceptable answer at all.

      **The gate to settle first, because everything else is wasted if it
      fails: kernel-mode code signing.** Windows XP x64 does not enforce it,
      which is why 21.5 took that target. Vista x64 and Windows 7 x64 both do,
      and the cross-certificate route that once made third-party Windows 7 x64
      signing possible is no longer available in practice (design record 11
      section 6). So an unsigned `xhci98.sys` loads on those systems **only**
      on a boot with driver signature enforcement disabled (F8) or with
      test-signing on. Establish which of those works on each guest, and what
      the user has to do at every boot, before spending a day on anything
      else. If the answer is that the driver cannot be made to load at all,
      that is the task's result and it is worth having.

      **The second question, and 21.7 left it explicitly open: the driver
      store.** The INF's `LayoutFile=layout.inf` route is a Windows 2000 and
      XP mechanism; Vista and later stage the package into the driver store
      first and validate its file list more strictly. 21.7 read one thing that
      makes this look easier - both install images carry `usbport.sys`,
      `usbhub.sys`, `usbd.sys` and `usbehci.sys` in `Windows\System32\drivers`
      outright, in every architecture, so the Code 39 that Phase 19's fix
      answers cannot arise and every `COPYFLG_NO_OVERWRITE` copy should skip
      without needing a source. **"Should" is doing work there**: that is a
      reading of an install image, not of an installed system, and it says
      nothing about whether the driver store accepts the package's file list.
      If it does not, the fix is a third INF or an `.NTamd64.6.0` decorated
      section, and **that is a decision, not a change to make in passing** -
      design record 11 section 12's decision 2 kept the 32-bit and 64-bit INFs
      apart for a reason.

      What is already settled and must not be re-litigated: `USBPORT_GetHciMn`
      returns `0x10000001` on both, so no code change follows; the packet the
      miniport fills is the same `0x250` one, because the higher version tiers'
      extra stores sit behind version tests a `Version = 200` miniport never
      passes; and Windows 7's second `IoGetDmaAdapter`, the one asking for a
      64-bit adapter, is gated on `Version >= 310` and is unreachable from
      here - **which is exactly why the high-DWORD check at
      `src/xhci_xfer.c:542` must stay**, and M8 is the standing argument that
      such checks earn their keep.
  - [x] the two guests built, from the media already on the development host:
        `en_windows_vista_sp2_x64_dvd_342267.iso` and
        `en_windows_7_professional_with_sp1_vl_build_x64_dvd_u_677791.iso` -
        the same media 21.7 read its measurements out of, so the stack a guest
        installs is the one that was measured, and saying so is cheap. Done
        2026-09-10: `scripts\setup-qemu-vista-x64.ps1` and
        `setup-qemu-win7-x64.ps1`, their launchers, `vm\vista-x64.img` and
        `vm\win7-x64.img` at 32 GB, and their rows in
        `scripts\test-qemu-launchers.ps1` (294 checks, 11 monitor ports, none
        shared). **Not two more copies of `setup-qemu-winxp64.ps1`**: they are
        the 64-bit half of the Vista and Windows 7 recipe and share
        `scripts\qemu-nt6-common.ps1` with the 32-bit pair, with a new `-Arch`
        selecting the four things that differ - the CPU refusal's missing
        feature (long mode, not the NX bit), the INF half and the
        `make-package.ps1 -Arch amd64` staging command, the code-signing
        paragraph, and the memory note. The 32-bit pair's generated launcher
        text was held byte-identical across that change and checked by
        generating both ways and comparing. The planned names
        `setup-qemu-winvista64.ps1` / `setup-qemu-win7x64.ps1` were dropped:
        they disagreed with each other and were coined when these guests were
        expected to be XP x64's siblings rather than Vista's and Windows 7's.
        **Monitor ports 55563 and 55564 are now claimed rather than reserved**,
        so the gate's assertion that nothing takes them is gone, replaced by
        one that these two still do; the ordinary no-two-guests-share-a-port
        scan covers them like any other guest. **Both guests were then
        installed on 2026-09-10** by the owner at the console, each with one
        snapshot of the clean install - `vista-x64-clean-install` and
        `win7-x64-clean-install`, `qemu-img check` clean on both
  - [x] **the accelerator probed on each, and the result recorded whichever
        way it goes.** Done 2026-09-10: **both guests want `tcg,thread=multi`**,
        each confirmed by an install that COMPLETED under it and was shut down
        from the Start menu. Both generators now carry that as a measured
        default, the launcher gate's refuses-without-`-Accel` check was deleted
        in the same change (294 checks to 290), and the gate's two 64-bit rows
        now pass no `-Accel` so its assertions read those defaults.

        **The two failed differently under `whpx,kernel-irqchip=off`, and the
        difference is what this box was for.** Vista x64 bugchecks inside WinPE
        before Setup writes a byte - STOP `0x0000000A`, address `0x10` at IRQL
        `0xC` on a read, a near-null dereference at device IRQL, which is the
        surface `kernel-irqchip=off` touches. Windows 7 x64 clears WinPE, runs
        its **entire first phase**, writes 7.27 GB, and only then wedges at the
        first restart: screen unchanged for eight minutes, `ide0-hd0` idle
        climbing monotonically past nine, RIP revisiting the same three
        addresses with `HLT=0`. Screens at `out\task-21-8\`.

        **For most of an afternoon that second reading looked like a
        disagreement with the first, and it was only a slower failure.** Which
        is precisely the rule this project paid for on 2026-09-10 and nearly
        paid for twice: an accelerator may not be written down until an install
        has COMPLETED under it. A prompt, a progress bar and a whole finished
        phase all prove only that the guest has not failed yet.

        **A wedge does not always leave a resumable image, and that qualifies a
        rule this repository had stated flatly.** "Switching to TCG costs no
        reinstall" held for `vm\vista.img`, which wedged on a boot *after* a
        completed phase and so still had a bootable disk. Windows 7 x64 wedged
        *at* the transition, before Setup laid its boot files down; the TCG
        relaunch met `BOOTMGR is missing` and Setup had to run again from the
        DVD. The rule needs its qualifier: no reinstall **provided the guest
        already has a bootable disk**.

        Worth noting and not worth promoting: the 64-bit pair agree where the
        32-bit pair disagreed, and with XP x64 also on `tcg` every 64-bit guest
        here now wants TCG. That is an observation about four guests, not a
        property of bitness
  - [ ] the code-signing gate above: which route loads an unsigned driver on
        each guest, what it costs the user at every boot, and whether it
        survives a reboot at all.

        **F8 TAKEN ON THE VISTA X64 GUEST, 2026-09-10, AND THE ANSWER IS YES:
        the amd64 binary loads and runs on 6.0.** `DriverEntry` completes,
        `USBPORT_GetHciMn` returns `0x10000001` and the packet size is `0x250`
        - task 21.7 read both statically and both are now measured live - the
        registration succeeds, usbport writes back its 16 service pointers and
        calls `StartController`. **Box 3's decisive question is answered and
        boxes 4 to 7 are not foreclosed.** The cost is what the box predicted:
        the F8 menu item applies to exactly one boot, was re-chosen on every
        boot of the session, and survives nothing. `TESTSIGNING` was not
        reached and Windows 7 x64 was not booted, so the box stays open.

        Three things had to be fixed or found on the way, in the order they
        bit, and none of them was a signing question:

        1. **The install aborted before the driver existed on disk.** The
           driver store *accepted and staged* the package - the refusal box 4
           anticipated did not happen - but `_COMMIT_FILE_QUEUE` then aborted
           on `usbport.sys`: setupapi resolved it through
           `LayoutFile=layout.inf` against Vista's own `usbport.inf_518a1f35`
           driver-store package, built an unresolvable source path, and
           `SPFILENOTIFY_NEEDMEDIA` returned `FILEOP_ABORT`.
           `COPYFLG_NO_OVERWRITE` does not save it: the queue resolves the
           source **before** it decides to skip, which is exactly the "should
           is doing work there" caveat this task put on 21.7's reading.
           `usbui.dll` pruned cleanly; the three `[Xhci.CopyNT]` files did not.
        2. **A `.6.0`-decorated install section is silently never read.** The
           OS-version part of a `TargetOSVersion` decoration selects the
           **models** section only; the install section takes the platform
           extension and nothing more, so setupapi ran `[Xhci.Dev.NTAMD64]`
           while logging a driver node of `XhciModels.NTamd64.6.0`. The shape
           that works names a **different install section** from the 6.0 models
           section - `Xhci.Dev6` - and it passes the INF gate as written.
           **Measured on a staged copy only; `src/xhci98-amd64.inf` is
           untouched and the shipping shape is not decided.**
        3. **`USBPORT_RegisterUSBPortDriver` takes a fourth argument on NT
           6.x**, and a three-argument call bugchecks `0x7E` inside usbport's
           `memmove` before any of section 5's measurements is exercised.
           Fixed for amd64 under a runtime arity branch; design record 11
           section 6.1 and decision 10 carry the evidence and the reasoning,
           and **Phase 22 will meet the same boundary on 32-bit, where the fix
           is harder**.

        **WHERE IT STOPPED NEXT, AND THAT IS NOW READ AND FIXED.** The driver
        refused its own initialisation at `XHCI_INIT_STEP_RESOURCES` with
        `ResourcesTypes = 0x0C`, which Device Manager shows as Code 10. Our
        constants were `PORT=1, INTERRUPT=2, MEMORY=4`, so `0x0C` had memory
        but not interrupt, and an undefined bit 3 instead. The rest of the
        resource block mapped onto the amd64 `USBPORT_RESOURCES` layout
        perfectly and the interrupt fields were fully populated - vector
        `0x92`, IRQL 9, affinity `0x0F` for the guest's four vCPUs,
        `ShareVector` set - and `HcFlavor = 1000` is `EHCI_Generic`. The mask
        was also **exactly the NT 5.x value shifted one bit left**
        (`INTERRUPT|MEMORY = 0x06`, and `0x06 << 1 = 0x0C`), which would follow
        from 6.x inserting a member at the bottom of the resource-type enum -
        an inference from a structural coincidence, and the task refused to
        change a constant on it.

        **Read out of `USBPORT_ParseResources` on 2026-09-10, static, in five
        shipping binaries, and the inference was right.** NT 6.x splits the
        port bit in two - an I/O-space port keeps `0x01`, a memory-mapped one
        takes `0x02` - and interrupt and memory move up to `0x04` and `0x08`.
        `MEMORY|INTERRUPT` is therefore `0x06` on NT 5.x and `0x0C` on NT 6.x.
        **XP x64 is what makes it a reading rather than a guess**: the same
        function in `winxp64-extracted` writes `1`, `2` and `4` where
        `vista-x64-extracted` writes `1`/`2`, `4` and `8`, so the value moved
        rather than the build merely differing. The x86 pair were read in the
        same pass and agree, which is Phase 22's half of it. Which branch is
        which is fixed twice - by the descriptor-scan loop's registers and,
        independently, by the `USB_MINIPORT_FLAGS_*` bit each branch tests as
        its guard. Design record 11 **section 6.2** has the RVAs, the
        instructions and the command.

        **Fixed for amd64 and not for x86**, on section 6.1's reasoning:
        `DriverEntry` already asks `IoIsWdmVersionAvailable` for the
        registration arity, and the same answer now settles
        `XhciResourcesRequired`, which `XhciInitController` reads at step 1.
        Unlike the arity branch **this one fails towards NT 5.x**, because the
        cost of being wrong is a legible refusal rather than a bugcheck. No
        import was added on either architecture, every gate is green, and the
        host tests carry both arms - which is the only place any host here can
        exercise the NT 6.x arm at all.

        **The version predicate itself was wrong until 2026-09-11 and both
        branches inherited it.** It was written `(1, 0x30)`; Server 2003
        reports WDM 1.30 exactly and Windows XP x64 *is* Server 2003, so XP x64
        answered TRUE, took the NT 6.x arm and refused at step 1 - **a
        regression in committed code, in the one x64 target the project
        claims.** It is now `(6, 0)`, read out of three kernels
        (`winxp64` 1.30, `vista-x86` 6.00, `win7-x86` **6.00, not 6.01**).
        Design record 11 section 6.3 is the record. This is exactly what box 5
        existed to catch, and it caught it on the first boot.

        **Two things about this are the owner's to weigh, and neither is
        settled here.** First, `XhciResourcesRequired` is one unconditional
        global rather than an `#ifdef`, so the *32-bit* binary changes too -
        not in behaviour, since nothing outside the `_WIN64` guard ever writes
        it and the mask it holds on x86 is the `0x06` the code always compared
        against, but the codegen is a load where it used to be an immediate and
        **the binary is therefore not byte-identical to the one the four x86
        install legs were taken on**. The alternative - a macro on x86 and a
        global elsewhere - buys byte-identity at the price of the host test
        exercising a shape the shipping x86 driver does not have, which is the
        trade this project has refused before. Whether that warrants an x86
        re-validation pass is a call, not a fact. Second, Phase 22 inherits the
        32-bit half of this alongside the arity, and of the two this is much
        the cheaper: the arity needs a second decorated import stub, this needs
        only the version predicate that stub's branch already computes.

        **Both guests have now answered.** Vista x64, 2026-09-10: the mask fix
        clears step 1, all 22 init steps run, Device Manager's Code 10 goes and
        a `USB Root Hub` appears - but `dpc count=00000000`, which is task
        21.8's remaining wall and design record 11 section 6.4. XP x64,
        2026-09-11, with the corrected predicate above: `wdm pre-6.00 ...=1`,
        `resource bits required=00000006`, registration status 0 on the
        **three-argument arm, executed for the first time anywhere**, `init
        step=00000016 / init status=00000000`, No Op self-test `CC_SUCCESS`, a
        hot-plugged HID mouse addressed and carrying transfers, and `isr count
        == isr claimed == dpc count` climbing together. **Box 5 is closed.**
        Evidence: `vm\task218-evidence\winxp64-wdm600-revalidation-boot.log`.
        Vista x64 was then re-checked with the corrected predicate on
        2026-09-11 (`built Sep 11 2026 00:18:56`, QEMU 11.1.0, from the
        clean-install snapshot through the staged `Xhci.Dev6` INF under F8):
        `wdm pre-6.00 ...=0`, `resource bits required=0000000C`, registration
        status 0, all 22 init steps, and the same `dpc count=00000000` wall -
        so both arms of `(6, 0)` are observed, not reasoned. Evidence:
        `vm\task218-evidence\vista-x64-wdm600-revalidation-boot.log`.
        **The host-side half is done, 2026-09-10, before either guest existed
        - which is the point, since this is the gate that comes first.** A
        complete test-signed `Vista_X64,7_X64` package can be produced from
        tools already in this repository with no network and nothing
        installed: `MakeCert`, `SignTool` and `CertMgr` in
        `tools\WinDDK71\bin\x86`, `Inf2Cat` in `tools\WinDDK71\bin\selfsign`.
        Sign the `.sys` first, then `Inf2Cat`, then sign the `.cat` - the
        catalog hashes the signed binary. One host prerequisite, and its
        absence is silent: `Inf2Cat` is a managed .NET 2.0 application, and on
        a stock Windows 11 host it exits `0x80131700` **printing nothing at
        all**. The owner enabled .NET 3.5 on 2026-09-10 and it now runs in
        place; where that is not possible, a `.config` beside a copy of it
        runs it on .NET 4, and both routes give **identical** diagnostics -
        so the two findings below are readings of the package, not of the
        runtime. `build-and-test.md`, "Vista x64 and Windows 7 x64 target
        VMs", has the commands; two of the findings change what this task
        owes:

        **The package is not signed, and as of 2026-09-10 that is decided
        rather than pending** (owner). Signing a release buys the claimed tier
        nothing: XP x64 and Server 2003 x64 do not enforce kernel-mode code
        signing, which is the whole reason 21.5 took that target, and the
        published package installs and loads there unsigned today. Vista x64
        and Windows 7 x64 are outside the tier until this task says otherwise,
        so a decision never to sign costs the tier nothing at all. **What the
        guests measure is what a USER would have to do**, not what this project
        ships, and that measurement stands either way.

        **So the INF line is retired as an open question rather than
        deferred.** `Inf2Cat` refuses `src\xhci98-amd64.inf` outright -
        `22.9.4: Missing AMD64 CatalogFile entry ... from [Version] section` -
        and adding `CatalogFile.NTamd64=xhci98.cat` makes it pass with zero
        errors. **It has not been added and does not need to be**, because a
        catalog is not what the loader checks. `src\xhci98-amd64.inf` stays
        byte-identical, design record 11 section 12's decision 2 is
        undisturbed, and the INF gate is not asked for an opinion it does not
        owe.

        **What makes that true: the test-signing route needs no catalog and so
        no INF change.** The service is `StartType=3`, demand-start rather than
        boot-start, and the load-time check takes an embedded Authenticode
        signature on `xhci98.sys` directly - `SignTool sign` on the binary and
        stop there, no `Inf2Cat`, no `.cat`. The catalog governs the
        install-time publisher prompt rather than the loader, and an unsigned
        package there costs a "Windows can't verify the publisher" dialog and
        an "Install anyway" click, which is a cost to record and not a block.
        Boot-start would have been the other answer, since that case requires
        the embedded signature and admits no catalog at all; this driver is not
        boot-start, so both are open to it and the cheaper one is enough.

        **That paragraph is a reading and not a measurement, and the guest is
        what settles it**, which is the right place for it. Try embedded-only
        first. If it loads, the INF line is never needed and the question is
        closed. If it does not, the catalog question reopens, and only then is
        that line a decision - taken on a staged copy and measured there before
        it is ever proposed for the shipping INF.

        **And Microsoft's own package validator blesses the `LayoutFile`
        route, by name, for exactly these two systems.** All four OS-supplied
        files come back as `22.9.10: ... missing from [SourceDisksFiles]
        section ...; ok if file source is provided via LayoutFile in
        [Version].` and nothing else. That is a better prior than the "should"
        the second question rests on - it is the vendor's tool asked about
        `Vista_X64` and `7_X64` specifically - and **it is still not the
        reading**: a signability test is not the driver store at install time.

        What is left is entirely guest-side, and it is two routes on each
        guest. **F8, Disable Driver Signature Enforcement**, which needs no
        signing at all and is per boot by design. And **`bcdedit -set
        TESTSIGNING ON`** over an embedded-signed `.sys`, where the test root
        has to reach the guest's Trusted Root **and** Trusted Publishers
        stores, both, before it buys anything. Record what each costs the user
        at every boot and whether either survives a reboot.

        **TAKE F8 FIRST, AND ON ONE GUEST BEFORE BOTH.** It needs no
        certificate, no catalog and nothing staged, so it is the cheapest path
        to the question that actually decides this task: does the amd64 binary
        load and work on these systems at all. A negative there closes 21.8 and
        boxes 4 to 7 never happen. Only if it loads is the test-signing route
        worth the time, and then what that route buys is a specific,
        answerable thing: whether the cost can be reduced from per-boot to
        one-time.

        **F8 ANSWERS HALF THIS BOX AND THE OTHER HALF IS THE ONE THAT REACHES
        THE TIER.** It applies to exactly one boot, by design, so the finding
        it produces is "the user must press F8 and choose that option every
        time the machine starts". For a USB host controller driver that is a
        heavy cost rather than a footnote: the machine can never boot
        unattended into working USB. That is a materially different claim from
        the one this project makes about XP x64 and Server 2003 x64, where the
        package installs and loads with nothing asked of the user, and task
        21.6's tier wording has to say so rather than list both routes as
        equivalent. `TESTSIGNING` is the route that persists across reboots,
        being a BCD setting; its own costs are the two certificate stores above
        and a permanent desktop watermark.

        Two things F8 does NOT do, so neither reads as a failure when it
        happens:

        - **It does not silence the install-time publisher prompt.** "Windows
          can't verify the publisher of this driver software" still appears
          during Update Driver, and still wants "Install this driver software
          anyway". That prompt is the catalog's business, not the loader's -
          the same distinction the paragraphs above rest on - and it is a cost
          to write down rather than a block.
        - **It says nothing about the box below it.** Whether the driver store
          accepts this package's file list is a different mechanism with a
          different failure mode. A clean load under F8 is not evidence about
          it in either direction.

        On the guests the F8 menu is reachable over the PS/2 keyboard, so there
        is no chicken-and-egg between the boot menu and the controller being
        installed
  - [ ] the `.NTamd64` package installed on each guest, or the driver-store
        refusal characterised precisely enough to decide what would fix it

        **ANSWERED FOR VISTA X64, 2026-09-10, AND NOT THE WAY THIS BOX
        EXPECTED.** The driver store does **not** refuse the package: it
        accepted and staged it, `xhci98.inf_0da41353\` in the FileRepository,
        with the only complaint a `sto:` warning that the INF carries no
        `CatalogFile` for the architecture - which did not stop staging, so
        decision 9's no-signing choice costs nothing structural on 6.0. What
        failed is a later and separate step, the file copy queue, and box 3
        above carries it. The `.NTamd64` package therefore installs on 6.0
        once the copy list stops asking for the four OS-supplied files, which
        the `Xhci.Dev6` shape does. **Windows 7 x64 is still owed, and so is
        the decision about whether that shape reaches the shipping INF** - it
        has only ever run from a staged copy.  - [ ] then the same clauses 21.5 took, on each guest: registered and
        started, the No Op self-test, the root-hub callbacks, a HID mouse, a
        mass-storage device and a composite audio device bound, and the Device
        Manager disable/enable/remove/rescan sequence. **On the `release`
        flavour as well as `qemu`**, for the reason 21.5 found the hard way -
        the clauses were taken on a build that is never published, and closing
        that gap is what licensed the 64-bit publisher default
  - [ ] **memory above 4 GB, as a deliberate experiment with its own record
        and not as a bumped default** (raised by the owner 2026-09-10). These
        are the first guests where it is even askable: every guest this
        project has ever booted has had less than 4 GB, so **the HAL's
        double-buffering has never once executed**. Give one guest 8192 MB
        by regenerating its launcher with `-MemoryMb`, leave the default at
        2048, and record the reading either way.

        **What it does NOT test, so that nobody reads more into a pass than
        is there: 64-bit addressing stays unreachable.** Windows 7's second
        `IoGetDmaAdapter` is gated on `Version >= 310` plus a packet slot this
        driver does not fill; it is version-gated, not RAM-gated, and no
        amount of memory opens it. usbport's adapter stays
        `Dma32BitAddresses = 1` / `DmaWidth = Width32Bits`.

        **What it DOES open is the shape of the scatter-gather list.** With
        memory above the line the HAL must bounce high buffers down through
        map registers, and that changes what `MapTransfer` produces -
        fragment count, lengths, offsets. That is the real target, and it is
        this driver's code that walks it: the `XHCI_XFER_MAX_DATA_TRBS` cap
        (`src/xhci_xfer.c:517`), the 64 KB physical-boundary splitting, and
        the `SgOffset` ordering and gap detection. Those edges are exercised
        today only on the shapes a small 32-bit guest happens to produce.

        **The read-out already exists and needs no new code**: the four probe
        counters `ProbeSgDisordered`, `ProbeSgGapped`, `ProbeSgHighDwords` and
        `ProbeSgMapped`, which every build maintains. All four at zero is the
        static record confirmed under a workload that could have broken it.

        **The dangerous case fails safe and the safe-looking one does not**,
        which is the asymmetry to carry into the run. A high address in an SG
        element is refused at `src/xhci_xfer.c:542` before anything is read or
        written, and counted - visible, clean, no corruption. But
        `USBPORT_RESOURCES.StartPA` is only a `ULONG`, so the common buffer's
        high DWORD is not exposed and **cannot be checked at all**
        (`implementation-invariants.md`, "DMA Addresses"); there the driver
        rests on the 32-bit adapter contract alone, and a violation would be
        silent. Read the common buffer's behaviour on its own terms rather
        than inferring it from a clean transfer counter.
  - [ ] the tier decided and stated with the rest in task 21.6, including the
        signing requirement, which belongs in the release notes beside the
        tier rather than in a footnote

      Not a checkpoint clause of this phase. Phase 21's checkpoint below is
      written about a Windows XP x64 guest and passed on 2026-09-09; this task
      does not reopen it. **The phase closes when 21.6 and 21.8 are done**, and
      21.8 may close with a negative result - "the driver cannot be loaded on
      these without disabling signature enforcement, and here is exactly what
      that costs" is a complete answer to the question asked.

Checkpoint (the Windows XP x64 guest). Every clause, or the phase is not
closed:

- [x] the static pass complete and transcribed into
      `usb-xhci-info/usbport-miniport-abi.md`, every fact tagged `static`.
      **Eight measurements, not the six the phase opened with**: M7 came from
      the compile scout and M8 from this guest
- [x] the gates green on an amd64 binary. All three flavours, every gate,
      2026-09-09: the amd64 import gate resolving all seven module/symbol
      pairs directly against the NT 5.2 amd64 kernels and the one HAL, the
      flavour marker, both INF profiles, and the host suite running
      `test_packet` and `test_membuf` for amd64 as well as x86
- [x] on a Windows XP x64 guest: the package installed on an xHCI-only
      machine. No `usbport.sys`, `usbhub.sys`, `usbehci.sys` or `usbd.sys`
      existed on that install; the `.NTamd64` half's `LayoutFile` route placed
      the stack from `Driver Cache\amd64` with no prompt for the CD, and
      Device Manager showed **USB 2.0 eXtensible Host Controller (xhci98)**
      and **USB Root Hub** with no yellow bang
- [x] the driver registered and started, and its No Op self-test passed.
      `USBPORT_GetHciMn=10000001`, `packet size=00000250`,
      `MiniPortExtensionSize=00017508`, `MiniPortTransferSize=00000098`,
      `MiniPortResourcesSize=00064000` and `common buffer usbport will
      request=00065000` - M2's and M3's measured values and task 21.4's
      computed amd64 sizes, now read off a running driver, with the common
      buffer identical to x86 as design record 04's re-run said it would be.
      `USBPORT_RegisterUSBPortDriver status=00000000`,
      `No Op self-test completion code=00000001`
- [x] the root-hub callbacks answered. `RH_GetRootHubData` reporting 4 managed
      ports, `RH_GetPortStatus`, `RH_SetFeaturePortPower` on all four,
      `RH_ClearFeaturePortConnectChange`, `RH_ChirpRootPort`,
      `RH_GetHubStatus`, `RH_EnableIrq`/`RH_DisableIrq`; port map 4 USB2-only
      ports, 0 USB3, all four powered. No `SuspendController` on an idle
      controller, so the NT half's `DisableSelectiveSuspend` works here as it
      does on 32-bit XP
- [x] a HID mouse, a mass-storage device and a composite audio device bound.
      Hot-plugged from the monitor onto the `p3=0` root ports: **USB Human
      Interface Device**, **USB Mass Storage Device**, and **USB Composite
      Device** plus **USB Audio Device**, all without a yellow bang. The audio
      device declared isochronous endpoints and they were opened; its
      `endpoint speed differs from the port's, usbport << 8 | decoded=00000302`
      line is the byte-for-byte precedented one from the Windows 98 audio run,
      a 12 Mb/s device on a 480 Mb/s port
- [x] the Device Manager disable, enable, remove and rescan sequence survived.
      Read in the trace as `SuspendController: halted, USBCMD=00000000` then
      `StopController` (disable), `DriverEntry` + `StartController` with all
      three devices re-enumerating (enable), `StopController` (remove), and a
      fresh `DriverEntry` + `StartController` reinstalling from the driver
      store with no media prompt and "Your new hardware is installed and ready
      to use" (rescan). 321 transfers completed across the run and **every
      refusal and error counter zero**, `transfer error events` included

Not a checkpoint: a build that links, or a static reading standing in for a
guest. No primary target's checkpoint waits on any of this, and none of it
may cost a primary target anything.

Records: `design/11-x64-targets.md`; `usb-xhci-info/usbport-miniport-abi.md`;
`legal-provenance.md` section 4; `build-and-test.md`.

## Phase 22 - The Existing 32-bit Binary on Windows Vista and Windows 7, and Release `1.1.0.0`

Goal: whether the binary this project already ships installs, loads and works
on 32-bit Windows Vista and 32-bit Windows 7 - settled from the shipping
`usbport.sys` first and guests second - and, if it does, its standing stated
in every document that names the targets; **and the tree cut as `1.1.0.0`**,
which is the first release to carry a 64-bit package.

**The cut was added to this phase on 2026-09-09 on the owner's instruction,
and it is not a consequence of the Vista and Windows 7 work.** What it
publishes is Phase 21's: an amd64 `xhci98.sys`, the second INF, and a
publisher whose `-Arch` defaults to both architectures, so an ordinary cut
now writes four directories - `release-x86`, `debug-x86`, `release-x64`,
`debug-x64`. Phase 21 bumped `src\xhci_version.h` to `1.1.0.0` and stopped
there; the number has never been spent, and until it is uploaded it stays
free (`releases/README.md`). It lands in Phase 22 rather than in 21 because
21 closes on task 21.8's guests, which may end in a negative result, and a
cut should not wait on a leg whose answer changes nothing about what is being
published.

Two things follow from putting it here and both are ordering constraints
rather than extra work. **The cut waits on task 22.5 being settled, not on a
yes from the Vista and Windows 7 guests.** If 22.5 turns out to be a driver
change it is a change to the shipping 32-bit binary, it goes into this
release, and all four existing 32-bit install legs are re-validated behind it;
if 22.5 stays empty, as everything read so far says it will, the 32-bit
binary in this release is `1.0.2.0`'s code at a new version. Either way the
cut is last. And **the asset gains a fifth install leg**, the amd64 package on
the XP x64 guest, which no previous cut had; the x86 half keeps its four.

**What the x64 half of this release may be said to be is exactly what task
21.5 observed and no more**: one guest, one virtual machine, never real
hardware, against the x86 half's four install legs. `AGENTS.md` states it and
the release notes state it for the user; do not let the act of publishing
inflate it.

Status: open, 2026-09-08, on the owner's instruction, with the `1.1.0.0` cut
added 2026-09-09. Tasks 22.1 and 22.2
are complete as of 2026-09-09, taken in one pass with 21.7 off the same
media: the six measurements on 6.0 and 6.1 x86 all pass, `USBPORT_GetHciMn`
returns the value this driver already accepts, and every import the shipping
binary names resolves in the right module on both systems. So **nothing read
statically argues against the existing 32-bit binary on Vista or Windows 7,
and task 22.5 has no work from either.** What is left of the Vista and
Windows 7 question is entirely the guest half - 22.3 and 22.4 - which is
where the two hard parts below live and where the unsigned-driver assumption
is confirmed or refuted. Left beside it, and independent of how that comes
out, is the `1.1.0.0` cut in 22.7 to 22.9.

**As of 2026-09-10, 22.3 and 22.4 are both closed.** Both guests are
installed, shut down and snapshotted (`vista-clean-install`,
`win7-clean-install`), and the static reading is taken off each. **It comes
back the same on both, and it is the answer that makes the rest of this half
cheap: everything the install path needs is already on disk.** All four
Microsoft USB files are in `Windows\System32\drivers`, the driver store stages
them again with their INF, and `usbui.dll` is there too - so the Code 39 that
XP and Windows 2000 suffer does not arise on either system, and the INF's
`LayoutFile` route is not needed on either.

Two things the pair taught that outlive it. **The two guests do not share an
accelerator** - Vista wants `tcg` because WHPX wedges its Setup after the first
reboot, Windows 7 runs under `whpx,kernel-irqchip=off` - so task 22.4's
"probe per host AND per guest" rule paid for itself for the first time
*within* a single pair. And **an accelerator may not be written down until an
install has completed under it**: the first probe stopped at the Setup
language page, recorded WHPX for both, and was wrong about one of them.

What the guest half still owes is the checkpoint itself: install the driver on
each guest and record what the unsigned-driver prompt actually did.
`build-and-test.md`, "Windows Vista and Windows 7 target VMs", is the recipe
and the readings.

**As of 2026-09-11 the premise of this phase is overtaken, and 22.5 is its
main work rather than an empty box.** Task 21.8's guests and the static
reading behind them (design record 11 sections 6.1 to 6.4) found that no
Version 200 miniport can run on Vista or Windows 7 in either architecture:
registration takes a fourth argument, the resource bits moved, and the
interrupt DPC is taken from a Version 300 slot past the end of what a Version
200 packet copies - and on x86 the registration is callee-cleaned, so there is
no inert-extra-argument escape. The owner decided the same day that the driver
is to run on Vista and Windows 7 on both architectures, with 64-bit users told
about signature enforcement in the release notes (design record 11 section 12,
decision 12). So 22.5 is a change to the shipping 32-bit binary and to the
amd64 one, this release carries it, and every install leg is re-validated
behind it - which is the case the paragraph above 22.5 always provided for.

Why a phase, and why it is a different one from 21: **this asks nothing of
the toolchain.** Phase 21 needs a second DDK, a second import library and an
arch fork before a compiler is even reached; this phase's subject is the
`xhci98.sys` that exists, unchanged, on the `.NTx86` half of the INF that
exists. If the answer is yes, it costs guests and readings and no build at
all. That is a different shape of work from 21 and a different set of ways to
fail, which is why it is not a task inside it.

Two things make it cheaper than 21's 64-bit leg:

- **32-bit Vista and Windows 7 do not enforce kernel-mode code signing.**
  That enforcement is x64-only. An unsigned build can load; PnP still warns
  at install time that the publisher cannot be verified, which is a prompt
  and not a refusal. **Confirm this on the guest rather than taking it from
  here** - it is the single assumption that would make the phase pointless if
  wrong, and it is cheap to check.
- **Both still ship `usbport.sys`.** Vista and 7 carry the USB 1.1/2.0 stack
  natively, so Option A - be a miniport under the OS's own usbport - is still
  on the table. It is Windows 8 that replaces it for xHCI, and Windows 8 is
  not in scope here.

Two made it harder, both written down as consequences of Phase 21's reading.
The second is settled as of 2026-09-09; the first is not, and is now the whole
of the phase's risk:

- **They stage driver packages into the driver store**, which the INF's
  `LayoutFile` route was not written for. That route is Phase 19's fix - the
  NT install path pulling `usbport.sys`, `usbd.sys` and `usbhub.sys` from the
  operating system's own cache, because an xHCI-only NT install has none of
  them on disk. Whether that mechanism survives into 6.0 and 6.1 is a
  reading, not a deduction. One half of it looks easier than it did: both
  install images carry all four Microsoft USB files in `System32\drivers`
  outright, so every `COPYFLG_NO_OVERWRITE` copy should skip and never need a
  source. What that does not answer is the driver store's own validation of
  the package's file list, which is the part no static reading reaches.
- **`USBPORT_GetHciMn` has changed across lineages before - settled
  2026-09-09, and it did not change here.** The worry was real: the 5.0 -> 5.1
  step kept the registration packet byte-identical and *still* changed the
  value, and a fourth constant on 6.0 or 6.1 would have been a fourth arm on
  the refusal at `src/xhci_dispatch.c:4640` - a code change to the **shipping
  32-bit binary**, with all four existing install legs to re-validate behind
  it. Both 6.0 and 6.1 return `0x10000001`, which that refusal already
  accepts. Task 22.1 has the reading.

Tier, if the phase closes yes: supported in virtual machines, stated the way
Windows ME and 32-bit XP are. **No checkpoint tax.** No phase waits on a
Vista or Windows 7 observation, "observed on both" does not include them, and
the standing rule holds unchanged - accommodate them where the change is
small and low-risk, never at a primary target's expense.

Tasks. A box is ticked only on the evidence its line names, and a static
reading may not tick a box whose line names a guest.

- [x] **22.1 - the static ABI pass on 6.0 and 6.1 x86.** Taken 2026-09-09 in
      one pass with 21.7, extraction and disassembly only. **All six pass on
      both**, and the transcription is in
      `usb-xhci-info/usbport-miniport-abi.md`, "The 6.0 and 6.1 lineages".
      Method `static` throughout. Nothing here implies a driver change, so
      task 22.5 has no work from this task.
  - [x] Vista x86 - `usbport.sys` and `usbehci.sys` extracted from
        `sources\install.wim` image 1 (`Windows Vista Business`), hashed,
        version-stamped 6.0.6002.18005, and the six read
  - [x] Windows 7 x86 - the same, 6.1.7601.17514
  - [x] **M2 recorded: `USBPORT_GetHciMn` returns `0x10000001` on both** -
        the XP-lineage value `src/xhci_dispatch.c:4640` already accepts. No
        fourth constant, and no code change to the shipping binary
  - [x] and the differences the pass did find, none of which reaches this
        driver as it is built today: a fourth export (`DllInitialize`) that
        shifts all three ordinals but is bound by name and so is inert; two
        packet version tiers above `0x13C`, whose extra service-pointer stores
        sit behind the version tests so a `Version = 200` packet is never
        written past `0x120`; a `USBPORT_RESOURCES` that keeps every field
        this driver reads at its NT 5.x offset while growing a tail past
        `0x34`, which means its `sizeof` is not a fact about 6.0 or 6.1; the
        usbport-internal wrapper moving the packet to `interface+0x1C`, which
        no miniport reads; and the one to remember - **Windows 7's usbport has
        a second `IoGetDmaAdapter` call that passes `Dma64BitAddresses = 1`**,
        reached only by a miniport declaring `Version >= 310` *and* filling
        packet slot `0x1F8`. This driver declares 200 and fills neither, so
        usbport copies its 32-bit adapter into the 64-bit slot and there is
        one 32-bit adapter as on every earlier lineage. Raising the declared
        version some day is the one change that would move physical addresses
        above 4 GB, and `src/xhci_xfer.c:542`'s high-DWORD check is what
        stands between that and corruption
- [x] **22.2 - the imports.** Read 2026-09-09 from the two systems'
      `ntoskrnl.exe` and `hal.dll`, extracted in the same pass. **All ten
      module/symbol pairs the shipping binary can name resolve, in the right
      module, on both**: the five `ntoskrnl.exe` rows (`DbgPrint`,
      `InterlockedIncrement`, `KeInitializeSpinLock`, `READ_REGISTER_ULONG`,
      `WRITE_REGISTER_ULONG`), the four `HAL.dll` rows (`KeStallExecutionProcessor`,
      `KeGetCurrentIrql`, `KfAcquireSpinLock`, `KfReleaseSpinLock`) and, though
      it is never published, `HAL.dll!WRITE_PORT_UCHAR` for the `qemu` flavour.
      Every one is a real code export with an RVA rather than a forwarder. The
      `USBPORT.SYS` pair is covered by 22.1's M1. `scripts\import-gate\` still
      has no 6.0/6.1 baseline behind it and this reading does not add one; it
      answers the question ahead of a guest, which is the point of taking it.
- [x] **22.3 - the install path, read statically off a clean guest.** The two
      cheap readings 21.5 took for XP x64, on each of these. **Both taken
      2026-09-10 off `vista-clean-install` and `win7-clean-install`, and both
      come back the same way on both guests: everything the install path needs
      is already on disk.** So the Code 39 that XP and Windows 2000 suffer
      does not arise on either system, and the INF's `LayoutFile` route is not
      needed on either - which settles the question this task was opened to
      ask:
  - [x] whether an xHCI-only Vista/7 install has `usbport.sys`, `usbhub.sys`,
        `usbd.sys` or `usbehci.sys` on disk at all. **A strong prior, but not
        this box:** all four are in `Windows\System32\drivers` inside the
        `install.wim` of both systems in both architectures (read 2026-09-09
        with 22.1), and Vista and later apply the whole image rather than
        copying drivers on demand the way XP Setup does. That is a reading of
        the install media, not of an installed system, so the box stays open
        until a guest is looked at. **Asked and answered on 2026-09-10 - why
        not just read the WIM?** Because the gap between what Setup carries
        and what Setup leaves is where the defect this box hunts actually
        lived: on the XP lineage those same four files were on the media the
        whole time, in `Driver Cache\i386`, and an xHCI-only install still had
        none of them on disk, which is the Code 39 that `1.0.1.0`'s INF fix
        answers. The WIM says what an install would apply, not what it has.
        **VISTA READ 2026-09-10 OFF `vista-clean-install`, AND THE ANSWER IS
        YES - ALL FOUR ARE ON DISK.** `Windows\System32\drivers` holds
        `usbport.sys` (226,304), `usbhub.sys` (196,096), `usbehci.sys`
        (39,936) and `usbd.sys` (5,888), plus `usbuhci.sys`, `usbohci.sys`,
        `usbccgp.sys` and `hidusb.sys`. And it is a **stronger** reading than
        the box asks for: this guest was installed with **no USB host
        controller at all**, not merely an xHCI-only one, so if the files
        survive that they survive the xHCI case a fortiori. So the Code 39
        that XP and 2000 suffer **does not arise on Vista**, and every
        `COPYFLG_NO_OVERWRITE` copy should skip with no source needed.
        **`usbui.dll` is on disk as well** (`Windows\System32\usbui.dll`,
        83,456 bytes, 6.0.6001.18000, with its `en-US` MUI and a WinSxS
        component behind it) - the fifth file the install path needs, and the
        one release `1.0.2.0` had to add to the package for the four 9x and NT
        sources. **WINDOWS 7 READ THE SAME DAY AND AGREES ON EVERY COUNT**:
        `usbport.sys` (284,672), `usbhub.sys` (258,560), `usbehci.sys`
        (42,496), `usbd.sys` (5,888), the same four companions, and
        `usbui.dll` at 80,896 / 6.1.7600.16385. **Listing trap**: Windows 7
        Setup makes the 100 MB System Reserved partition, so `7z l` on the
        image **stops at the MBR** with three volumes instead of recursing -
        no error, just 32 lines that read like an empty disk. Extract the
        volume (`7z e vm\win7.img 1.ntfs`) and list that
  - [x] where the operating system keeps them if it does not, and whether the
        `LayoutFile` route can still reach them from a driver-store install.
        **This half cannot be read off media at any price**:
        `System32\DriverStore\FileRepository` is made by the install, so a WIM
        listing cannot be asked the question at all. **VISTA READ 2026-09-10,
        and the store carries the whole payload, not just the INFs.** Three
        generations of `usbport.inf_*` are staged - `_4d107f9d` (RTM,
        2006-11-02), `_dab84ba6` (SP1, 2008-01-21) and `_2c537348` (SP2,
        2009-04-11) - and each holds `usbport.sys`, `usbhub.sys`, `usbd.sys`,
        `usbehci.sys`, `usbuhci.sys` and `usbohci.sys` beside its `.inf`,
        `.PNF`, `hccoin.dll` and `hcrstco.dll`. So the operating system's own
        copies are on disk in a place Setup can source from, and **the
        `LayoutFile` route is not needed here at all** - the question it
        exists to answer, a file that is not on the machine, does not arise
        when the file is already at its destination. **Windows 7 agrees**:
        `usbport.inf_x86_neutral_f9abf85fd00186bd` stages `usbport.sys`,
        `usbhub.sys`, `usbd.sys` and `usbehci.sys`. Note the store's naming
        differs between the two - Vista's `<inf>_<hash>` against Windows 7's
        `<inf>_<arch>_<lang>_<hash>` - so anything matching those directories
        by pattern must know which system it is reading
- [x] **22.4 - the guests.** One Vista x86 and one Windows 7 x86 QEMU guest,
      each with a committed generator and a launcher-gate row, the way
      `setup-qemu-winxp.ps1` and `setup-qemu-winxp64.ps1` were done.
      **Probe the accelerator per host AND per guest** - Phase 21 paid for
      that rule twice, in opposite directions, and neither reading
      generalises. **All of it done 2026-09-10**, both guests installed,
      shut down and snapshotted. The accelerator rule paid for itself a third
      time and for the first time *within* a pair: **these two guests do not
      share one.** What this task does NOT cover, and what the phase's
      checkpoint still owes, is installing the driver on either guest and
      recording what the unsigned-driver prompt did.
  - [x] the generators, and they are **one recipe written once**:
        `scripts\qemu-nt6-common.ps1` holds the machine and
        `scripts\setup-qemu-vista.ps1` / `setup-qemu-win7.ps1` are thin
        callers. That is the one structural departure from the six generators
        before them and the 2026-09-07 audit's H28 is the reason - five copies
        of one resolver had drifted apart unnoticed, and two guests born the
        same day out of one recipe are the pair that would drift next
  - [x] their launcher-gate rows, plus two refusals the gate now asserts:
        `-cpu pentium3` (the 32-bit XP recipe's own value, one line away in
        the same directory) **predates the NX bit and Windows 7 Setup refuses
        such a processor**, so the shared body throws rather than writing a
        launcher that installs nothing; and the install and run launchers must
        agree on the accelerator, since the HAL is fixed at install time.
        209 checks, 9 monitor ports, none shared
  - [x] **the accelerator probed on each, 2026-09-10, host `minis-w11p-ykm` -
        and the first probe was WRONG, which is the useful part.** It ran each
        guest to its "Install Windows" language page (Vista under four
        minutes, Windows 7 about two) and recorded WHPX for both. Installing
        Vista the same day under `whpx,kernel-irqchip=off` ran the whole first
        phase and then **wedged on the boot after it**: `EIP` confined to two
        addresses, interrupts enabled at `CPL=0`, half a core burning, and the
        disk idle for **twenty-two minutes** while the boot marquee kept
        animating. Relaunched on the same half-installed image under
        `-accel tcg`, Setup **resumed** and ran to the desktop. So **Vista is
        `tcg`**, as the XP x64 guest is - and **Windows 7 is not**: installed
        the same day under `whpx,kernel-irqchip=off` it ran the whole way
        through, first reboot included, to a finished desktop, so its value is
        confirmed the same way Vista's is. **The pair disagrees**, which is
        the point: two guests one WDM revision apart, from one recipe on one
        host in one afternoon, do not share an accelerator, and neither answer
        could have been inherited from the other. Three rules came out of it,
        all now in
        `build-and-test.md`: reaching the first prompt probes nothing but
        WinPE, so **an accelerator may not be written down until an install
        has completed under it**; a pinned `EIP` is read differently at a
        prompt (screendump) than at a boot screen (disk idle time, since the
        marquee animates either way); and **plain `-accel whpx` cannot
        initialise on this host at all** (it wants nested virtualisation), so
        `kernel-irqchip=off` is the only WHPX there is here and TCG is the
        only alternative when it wedges
  - [x] **four vCPUs on both, and `thread=multi` when the accelerator is
        TCG.** Not a property of these guests but a consequence of the line
        above: TCG is the fallback, and QEMU emulates x86-on-x86 with
        multi-threaded TCG, so vCPUs become host threads. Measured on the
        Vista guest - all four threads busy and roughly even - which also
        says Setup chose the multiprocessor HAL. `thread=multi` is never
        handed to WHPX, which refuses the whole `-accel` argument rather than
        ignoring an option it does not know, and the gate asserts both that
        and the install/run agreement on `-smp`
  - [x] the disks (`vm\vista.img`, `vm\win7.img`, 32 GB each) and a smoke test
        of the generated install launcher itself, which the gate cannot do:
        `qemu-vista-install.cmd` boots the DVD and reaches "Windows is loading
        files"
  - [x] **the two Setup runs, both taken 2026-09-10.** The owner drove Setup
        at the console - the standing decision of 2026-09-03, taken for the
        32-bit XP guest and unchanged here - and both guests are shut down and
        snapshotted, `vista-clean-install` and `win7-clean-install`. Vista is
        Business (the edition the media's image 1 and tasks 22.1/22.2 name),
        confirmed on its own logon screen. **`vm\vista.img`'s provenance is
        not what a clean run of the recipe would produce and the record says
        so**: its first phase ran under WHPX up to the wedge, the rest under
        single-vCPU TCG after the relaunch resumed it, and it has been booted
        since under `tcg,thread=multi` with four vCPUs, which it uses - so
        Setup chose the multiprocessor HAL. If anything ever turns on that
        provenance, reinstall rather than argue from this line
  - [x] **a monitor-port collision caught before it was generated, and the
        gate widened so the next one is not.** These two guests were drafted
        onto **55563 and 55564**, which task 21.8 reserved in writing for the
        Vista x64 and Windows 7 x64 guests on 2026-09-09. The launcher gate
        compares generated launchers against each other, so it is blind to a
        port reserved in prose and would have stayed silent until 21.8's
        generators were written - months later, with both records believed in
        between. The 32-bit pair took **55565 and 55566**, 21.8's reservation
        stood untouched, and the gate asserted the reservation itself.
        **Released 2026-09-10, the way it was meant to be**: task 21.8's
        generators now claim 55563 and 55564, so the assertion that nothing
        takes them is gone - keeping it would assert that those guests must
        not exist - and in its place the gate asserts that they still do, so a
        guest that gave one up cannot leave it reserved for nothing. The
        ordinary no-two-guests-share-a-port scan now covers all four
- [ ] **22.5 - Version 300 on NT 6.x, both architectures.** The owner's
      decision of 2026-09-11 (design record 11 section 12, decision 12). The
      driver presents `Version = 300` to an NT 6.x `usbport.sys` and `200` to
      everything else, from the `IoIsWdmVersionAvailable(6, 0)` answer the
      amd64 build already takes and the x86 build will now take too; its
      packet grows to the 300 tier (`0x1E0` x86, `0x368` amd64); its interrupt
      DPC returns the `ULONG` NT 6.x reads; and x86 registration passes the
      fourth argument through a cast the same way amd64 does. **Read first,
      then design, then build, then guests, in that order, and no constant
      moves before its reading is written down.** Four NT 6.x divergences were
      each invisible until the one before it was fixed (task 21.8), and that
      task's method lesson stands: disassemble the *callers*, not just the
      structures.
  - [x] **the static reading of the 300 tier**, `static`, off the four NT 6.x
        `usbport.sys` and `usbehci.sys` already in `tools\`, transcribed into
        `usbport-miniport-abi.md` with its provenance rows. **Done 2026-09-11,
        the same day, with Microsoft's public symbol files loaded** - every
        usbport-to-miniport call on NT 6.x is a named `usbport!MPf_<Callback>`
        wrapper, which turned the reading into an afternoon (`lessons.md`).
        `usbport-miniport-abi.md`, "The Version 300 tier, slot by slot", and
        design record 11 section 6.5 are the record; the tier is twelve
        `ULONG`s and 29 pointers, zero is safe for every one of them, and the
        one slot that must be filled is `InterruptDpcEx`:
    - [x] every test of the interface `Version` field (`interface+0x10` x86,
          `+0x20` amd64) in all four binaries, so that what `300` turns on is
          enumerated rather than assumed. The `>= 0x12C` copy size and its two
          service pointers are known, the `>= 0x136` 64-bit DMA adapter gate is
          known and must stay closed; anything else is the finding. **Found:
          every `>= 300` test outside registration is a wrapper gate that
          NULL-checks the slot behind it, plus one in
          `USBPORT_AllocateControllerCommonBuffers` that reads packet `0x13C`
          as a count of extra common buffers, zero meaning none. Three
          wrappers on 200-tier slots - `CloseEndpoint`, `RebalanceEndpoint`,
          `TakePortControl` - test `>= 300` before calling at all, so those
          three become reachable on NT 6.x for the first time**
    - [x] every read of a packet slot in `[0x13C, 0x1E0)` x86 and
          `[0x250, 0x368)` amd64: which slots usbport calls, from where, under
          which lock, NULL-checked or not, with what arguments and return - a
          table with a verdict per slot, must-fill, may-zero, or written by
          usbport. The slots Microsoft's own `usbehci.sys` fills (x86
          `0x16C`-`0x180`, `0x1CC`, `0x1D8`; amd64 `0x280`-`0x2A8`, `0x340`,
          `0x358`) are identified by reading what usbehci puts there. **Found:
          29 named slots, all NULL-checked; two OUT services at `0x1B0`/`0x1B4`
          (`0x308`/`0x310`); six slots with no reader at all; the amd64 map is
          `0x280 + (X - 0x16C) * 2`, confirmed at every site. One hard rule
          from the tier above: the 310 slot at `0x1E0`/`0x368` is called
          without a NULL check behind `MiniPortFlags & 0x4000` - never set it**
    - [x] the `InterruptDpc` return value: what bits 0 and 1 mean, read from
          usbport's consumer and from usbehci's producer both, and whether NT
          6.x still reads `packet+0x4C` / `+0x70` for anything at all. **Found:
          `ULONG (extension, BOOLEAN enableInterrupts)` under usbport's DPC
          lock; bits 0 and 1 are read identically and either invalidates the
          root-hub interrupt endpoint; usbehci returns 2 for a port change, 1
          for a transfer interrupt with a pending count, 0 otherwise, and its
          own `0x4C`-slot function is a thunk onto the same routine. NT 6.x
          never reads `0x4C` / `0x70`**
    - [x] the NT 5.x control: that NUSB, SP4, XP SP3 and XP x64 `usbport.sys`
          test `Version` nowhere but at `>= 100` and `>= 200`, so a grown
          packet under `200` is inert there by reading and not by hope.
          **Found: exactly that, on all four, every compare in `.text` listed
          in `legal-provenance.md` section 4**
  - [ ] **the design, written into design record 11 before code**: the
        300-tier packet declaration with measured sizes and offsets on both
        architectures, the two-version registration, the x86 four-argument
        call through a cast of the one import (the import library binds by
        plain name, so no second stub is needed and `XHCI_CHECK_STACK_DELTA`
        is the net under the `qemu` flavour), the `ULONG` DPC serving both
        slots, and `XhciResourcesRequired` settled on x86 from the same answer.
        **Recommended and not yet decided: `300` on NT 6.x only, `200`
        everywhere else** - it keeps every NT 5.x and 9x system wire-identical
        to what it has been observed with, at the price of the runtime branch
        decision 10 already chose
  - [ ] **the build**: `src\xhci_usbport.h`, `src\xhci_dispatch.c`,
        `test\test_packet.c` and host tests carrying both arms; the x86
        allowlist row for `ntoskrnl.exe!IoIsWdmVersionAvailable` with its
        Windows 98 evidence - `w2k-export` in both SP4 kernels,
        `win98-precedent` in NUSB's own `USBPORT.SYS`, which imports it, and
        `ntkern-name`; every gate green on all three flavours of both
        architectures
  - [ ] **the guests, in this order, `qemu` flavour first, and the readings
        named before each boot**: Vista x64, where the wall is known (`dpc
        count` climbing with `isr count`, No Op `CC_SUCCESS`, then the clauses
        21.5 took - HID, mass storage, composite audio, disable / enable /
        remove / rescan); Windows 7 x64, never yet booted with the driver;
        Vista x86 and Windows 7 x86 off `vista-clean-install` and
        `win7-clean-install`, which also takes this phase's unsigned-prompt
        reading; then XP x64 again, for the `200` arm on amd64; then the four
        32-bit legs - 98, ME, 2000, XP32 - because the shipping binary changed
  - [ ] **the INF for the 6.x installs, an owner's decision on each file.**
        Vista's file queue aborts on the `LayoutFile` copies (task 21.8), and
        the shape that installs is a `.6.0`-decorated models section naming
        its own install section (`Xhci.Dev6`, a staged copy only so far). On
        amd64 that is a line in `src\xhci98-amd64.inf` that no 9x engine
        reads. **On x86 it is `%Mfg%=XhciModels,NTx86.6.0` in the one line
        Windows 98's 16-bit engine parses**, the measurement decision 2
        declined to make - so measure it on the Windows 98 guest during that
        leg's re-validation before the shipping x86 INF is touched, and record
        the answer either way
  - [ ] **`TESTSIGNING` tried once on an x64 guest**, for the release notes'
        64-bit paragraph: what the user does once, what it costs at every
        boot, and whether it survives a reboot. F8 is already measured
  - [ ] **the record**: task 21.6's tier wording for Vista and Windows 7 on
        both architectures - VM-supported, no checkpoint tax, and for x64 the
        signing paragraph beside the tier rather than in a footnote -
        `AGENTS.md`, `build-and-test.md`, `win98-wdm.md`, the release notes,
        and the `legal-provenance.md` section 4 rows; task 21.8's last box
        closes with it
- [ ] **22.6 - the record.** The tier stated where Windows ME and 32-bit XP
      are stated, in `AGENTS.md`, `build-and-test.md`, `win98-wdm.md` and the
      release notes, with the provenance rows beside it. Task 21.6 is the
      worked example and its lesson transfers: check first whether the wording
      the other tiers carry is true of this one before reusing it.

The `1.1.0.0` cut. These three run last, after 22.5 is settled either way,
and they are the phase's other half rather than a coda to the first.

- [ ] **22.7 - what a cut needs that no gate supplies.** Each of these is
      hand-written or hand-bumped, and the first two are refusals rather than
      omissions:
  - [ ] the `releases\history.md` entry for `1.1.0.0`, dated to agree with
        the INFs' `DriverVer` - `make-release.ps1` checks for it **before**
        anything is built and refuses without it. What the entry says is
        addressed to a user, because the download's `readme.txt` embeds it
        verbatim; how the release was assembled does not belong in it
        (`releases/README.md`)
  - [ ] the release date in `src\xhci_version.h` and the `DriverVer` line in
        **both** INFs set to the day of the cut. The INF gate checks the two
        files against the header, and `check-inf.ps1 -Arch` runs over each,
        so a stale date in the amd64 file fails the build rather than
        shipping
  - [ ] `docs/using/release-notes.md`'s opening line, which states the
        version the file describes. **Nothing reads it and no gate catches
        it**, and it has sat stale across cuts before
        (`build-and-test.md`, "Versioning the driver"). The x64 statements
        task 21.6 wrote into that file say "from `1.1.0.0`"; at the cut they
        stop being a forward reference and should read as current
  - [x] **the version scheme, which no longer has a gap. Settled by the owner
        2026-09-10** and written into `build-and-test.md`, "Versioning the
        driver": first field a change really major enough to warrant it (and
        the `0` -> `1` step to a final release, which is the only time it has
        moved), **second a major change - a new architecture, or a new
        function**, third a patch or bug fix in the driver's code, fourth a
        release that changes only the install media or the documents. Read
        the second and third as a pair rather than by size of diff. This box
        existed because the paragraph documented only the third and fourth
        while `1.1.0.0` moved the second; all four published numbers agree
        with the rule as now written
- [ ] **22.8 - the primary targets unchanged**, the way task 19.8 and Phase
      20 did it: `run-matrix.ps1 -PostRelease` on freshly re-taken 2a and 2b
      clones, against the Phase 20 reports. Both primary targets are 32-bit
      and neither is touched by anything in this release, so a difference
      here is a finding about the release rather than about the phase.
      Reports under `docs\contributing\runs\run-22-post-release\`.
- [ ] **22.9 - the cut itself, and the install route read from the asset.**
      `build-driver.cmd all` and `build-driver.cmd all -amd64` after the
      header change, both tools rebuilt, every gate green, then
      `make-release.ps1` with its default `-Arch`. It writes
      `releases\1.1.0.0\` with **four** flavour directories and
      `out\xhci98-1.1.0.0.zip`.
  - [ ] the four x86 install legs from the unzipped asset, as every cut since
        `1.0.1.0` has taken them - Windows 98 SE (on both stacks, NUSB and
        SweetLow), Windows ME, Windows 2000 SP4 and 32-bit Windows XP
  - [ ] **the fifth leg, which is new: the amd64 package on the XP x64
        guest**, installed from the asset's `RELEASE-X64\` directory rather
        than from `src\objfre\amd64`. Task 21.5 already read the `release`
        flavour on that guest and the recipe for reading a flavour that
        writes no port-`0xE9` trace is in its entry; what this leg adds is
        that the bytes came out of the published download
  - [ ] the asset's file list checked against what the packager staged, and
        no Microsoft file in it. `PKG-MSFILE` refuses one by name; the rule
        that actually closes it is the packager publishing nothing it did not
        itself stage (`AGENTS.md`, `legal-provenance.md` section 5)
  - [ ] a re-cut, if one is needed, under the same number with `-Force` while
        nothing has been uploaded - recorded here and not in the `history.md`
        entry, for the embedding reason `releases/README.md` gives

Checkpoint, the first half. Every clause, on **each** of the two guests, or
the phase is not closed:

- [ ] the existing package installed on an xHCI-only machine, and what the
      unsigned-driver prompt actually did recorded
- [ ] the driver registered and started, and its No Op self-test passed
- [ ] the root-hub callbacks answered
- [ ] a HID mouse, a mass-storage device and a composite audio device bound
- [ ] the Device Manager disable, enable, remove and rescan sequence survived

Not a checkpoint: a static pass standing in for a guest, or one of the two
guests standing in for the other. No primary target's checkpoint waits on any
of this, and none of it may cost a primary target anything.

**A negative closes this half.** If neither guest will load the driver, or
the driver store will not take the package, that is a complete result: the
tier is not claimed, 22.6 records why, and the cut below goes ahead
regardless, because nothing in it depends on the answer.

Checkpoint, the second half - the cut. All of it, or the phase is not closed:

- [ ] every gate and self-test green on both architectures, and
      `make-release.ps1` exit 0 with `releases\1.1.0.0\` holding four flavour
      directories
- [ ] the post-release matrix on both primary targets no worse than Phase
      20's reports
- [ ] the install route read from the published asset on all five legs -
      the four x86 ones every cut since `1.0.1.0` has taken, and the amd64
      one on the XP x64 guest
- [ ] the asset holding exactly what the packager staged - `xhci98.sys` and
      its INF in each of the four flavour directories, the two tools, the
      readmes and the licence texts - and no Microsoft file under any name
- [ ] the prose that no gate reaches bumped: `history.md`'s entry, the
      release notes' opening line, and the release date in the header and
      both INFs

Not a checkpoint: a `qemu`-flavour reading standing in for the published
`release` binary, on either architecture - Phase 21 paid for that rule on the
x64 side and it is why the `release` leg exists at all. Not a checkpoint
either: the acceptance test from the download, or the upload. Both are the
owner's, and the end of this file says so.

Records: `usb-xhci-info/usbport-miniport-abi.md`; `build-and-test.md`;
`usb-xhci-info/win98-wdm.md`; `lessons.md`; `releases/README.md` and
`releases/history.md`; `docs/using/release-acceptance-test.md`;
`docs/contributing/runs/run-22-post-release/`.

## Post-Release - Run the Acceptance Test by Hand

This is not a phase, has no task id, and nothing in this repository closes
it. It is the reminder the roadmap ends on.

Before the newest cut's asset (`out\xhci98-<version>.zip`, for the version
`releases/history.md` names first) is uploaded, run
[`release-acceptance-test.md`](../using/release-acceptance-test.md) end to end,
by hand, twice: on a freshly installed VM of the target, and on a physical
machine. Take the release from that asset, unzipped, not from this tree, and
follow the document the asset ships. Running it before the upload is what lets
it stand as the install reading for the targets no other run covers, and it is
why a finding against the driver can still be answered by a re-cut under the
same number rather than by a new one. Neither run substitutes for the
other: a fresh VM is the only cheap, repeatable clean install carrying nothing
this project put there, but cannot test the BIOS handoff, a real interrupt pin
or an uncharacterised controller; a physical machine tests those and cannot be
reinstalled on a whim. `scripts/vm-matrix/` is how a guest is built; the run
itself uses only what the asset provides.

No machine model is named: whatever machine is to hand, on whichever target it
boots, is the subject, and step 1 records what it was (on Windows 98, whether
NUSB is installed). Record the reading, not the verdict; what a VM cannot
reach is recorded as not reached, never as a pass. Do not improvise around the
document: where it is ambiguous, wrong, or assumes something the machine
lacks, that is the finding.

Nothing is reported back into this repository. What comes back is a defect
against the driver, as an issue, and a defect against the procedure, as an
edit to `release-acceptance-test.md`. A driver defect found this way is
fixed before the upload by a re-cut under the same number, which
`releases/README.md` permits while nothing has been uploaded. If the finding
arrives after the upload instead, the same README's rule binds and the fix
opens a new version number, with its own `history.md` entry; it is not a
reason to withdraw the release. The one thing a
failure changes immediately is what `docs/using/release-notes.md` claims.

Records: `docs/using/release-acceptance-test.md`; `releases/README.md`;
`build-and-test.md` ("Available Test Hardware", "The bench rig",
"Bootstrapping xHCI-only machines"); `test-equipment.md`;
`scripts/vm-matrix/README.md`; `docs/using/release-notes.md`.
