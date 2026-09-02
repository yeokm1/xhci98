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
  the per-run evidence in the run sheets [`run-11v.md`](runs/run-11v.md) and
  [`run-13e.md`](runs/run-13e.md).
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

Current status: Phases 0-17 are closed and Phase 18 is open. `1.0.0.0` is
cut, and it is the only release this repository carries; Phase 15 moved the
tree from revision 1.2 of the xHCI specification to revision 1.2c, the only
revision Intel now serves, without a code change; Phase 16, the fully
automated run on freshly installed guests of both targets, closed on
2026-08-30 on its second run, a clean Windows 2000 reading and a Windows 98
reading with one row against, the USB Audio replug, published as a
limitation. Phase 17, opened and closed on 2026-09-02, has the operating
system supply `usbd.sys` and `usbhub.sys` from its own install source, so the
package stops carrying any Microsoft file; no driver code changes. Phase 18 is release
`1.0.0.1`, which ships that change together with Windows ME support; its
Windows ME side is written down and not started. Two acts sit
outside the task list and are the project owner's to take: uploading the
asset, and then running the release acceptance test by hand on a fresh VM
and on a physical machine. The section this roadmap ends on is the reminder
for the second.

---

## Batching Convention

A phase splits by scope and has one checkpoint. A batch splits by where the
work can be confirmed and has no checkpoint of its own. A VM boot or a bench
trip is the expensive unit, and most tasks do not need one, so from Phase 6
onward a phase whose tasks are confirmed in more than one place groups them
into batches. Phases 6, 7a, 7b, 8, 9, 11 and 13 are of this shape; Phases 0-5,
10, 12, 14, 15, 16, 17 and 18 have plain per-phase task numbers.

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
14 waited on Phase 13's bench batches reporting. Accepting the published release, from the download on a
freshly installed VM and on a physical machine, is not a phase and has no
task: it is a hand-run procedure the project owner takes after the upload,
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
MMIO access, the No-Op command completing with the expected TRB pointer, Port
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

Ten clauses were never observable in this vehicle and are published, not
ticked: Win98 disable/re-enable (bugchecks through every door),
`ResumeController` on Windows 2000, restart after controller invalidation,
recovery after a controller fails, the scratchpad-limit refusal, rollback after
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
  script change, pointed at from `AGENTS.md`. Done 2026-09-02 (`2256779`).
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
the OS supplies: `usbd.sys` and `usbhub.sys`", "The SweetLow stack");
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
  (`1cad620`, re-cut `1a286ca` for readme wording before any upload):
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
("Windows ME on QEMU"); `scripts/vm-matrix/README.md`; `releases/history.md`;
`handoff.md`.

## Post-Release - Run the Acceptance Test by Hand

This is not a phase, has no task id, and nothing in this repository closes
it. It is the reminder the roadmap ends on.

Once `out\xhci98-1.0.0.1.zip` is uploaded, run
[`release-acceptance-test.md`](../using/release-acceptance-test.md) end to end,
by hand, twice: on a freshly installed VM of the target, and on a physical
machine. Take the release from the published download, not this tree, and
follow the document the download ships. Neither run substitutes for the
other: a fresh VM is the only cheap, repeatable clean install carrying nothing
this project put there, but cannot test the BIOS handoff, a real interrupt pin
or an uncharacterised controller; a physical machine tests those and cannot be
reinstalled on a whim. `scripts/vm-matrix/` is how a guest is built; the run
itself uses only what the download provides.

No machine model is named: whatever machine is to hand, on whichever target it
boots, is the subject, and step 1 records what it was (on Windows 98, whether
NUSB is installed). Record the reading, not the verdict; what a VM cannot
reach is recorded as not reached, never as a pass. Do not improvise around the
document: where it is ambiguous, wrong, or assumes something the machine
lacks, that is the finding.

Nothing is reported back into this repository. What comes back is a defect
against the driver, as an issue, and a defect against the procedure, as an
edit to `release-acceptance-test.md`. A driver defect found this way is
fixed and the existing release re-cut with the fix; it is not a reason to
withdraw the release, and it does not open a new version number. The one
thing a failure changes immediately is what `docs/using/release-notes.md`
claims.

Records: `docs/using/release-acceptance-test.md`; `releases/README.md`;
`build-and-test.md` ("Available Test Hardware", "The bench rig",
"Bootstrapping xHCI-only machines"); `test-equipment.md`;
`scripts/vm-matrix/README.md`; `docs/using/release-notes.md`.
