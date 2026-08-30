# Phase 0 - Hardware Qualification Tool (DOS) - Design

Design doc 01. Companion to the Phase 0 entry in [`../roadmap.md`](../roadmap.md).

## 1. Purpose

Before investing in the xHCI driver on a given machine, prove that the machine's xHCI controller and firmware can support it, and harvest the hardware-specific facts the driver will need. This is a DOS program that runs on bare metal, install-free, and answers one blunt question per candidate machine: can a legacy, line-interrupt operating system bring up this xHCI controller and receive its interrupts, and what are its parameters and quirks?

The verdict is about the controller, not about an OS. Both driver targets (Win98 SE and Win2000 SP4) place the same demands on the silicon, so a QUALIFIED machine is a candidate for either. Section 10 states how far that transfers, and where it does not.

The same executable also qualifies EHCI (`CC_0C0320`) and OHCI (`CC_0C0310`) controllers. No selector means all three families; `xhci`, `ehci`, or `ohci` (and the equivalent `--scan TYPE`) restrict discovery. The project remains an xHCI-driver project; the legacy-family backends make the field tool useful for inventorying and comparing all USB host controllers in a candidate machine.

Phase 0 is independent of the build chain and of both target OSes. Nothing about it requires the candidate machine to have Windows installed. It is not a prerequisite for Phases 1-3 (which run in QEMU); it is the tool you carry on an MS-DOS 7.1 (Windows 98) test medium to vet real candidate hardware before committing driver effort to it. Prefer a floppy or IDE/SATA medium not attached to the controller under active test.

## 2. Scope

In scope: the "silent death" risks and the facts Phase 4 otherwise guesses at.

- PCI discovery of xHCI controllers and their static configuration.
- xHCI capability-register introspection (context size, slots, ports, scratchpad, port topology).
- The three bring-up steps that fail silently on bad hardware/firmware: BIOS->OS ownership handoff, bus-master DMA, and INTx interrupt delivery in PIC mode.
- Basic port connect/reset, and the Intel EHCI->xHCI port switchover.
- Equivalent EHCI/OHCI ownership, reset, DMA, legacy-IRQ, and root-port gates. EHCI proves a halted-QH DMA read through asynchronous-schedule status plus Interrupt-on-Async-Advance and uses a second IAA for C4 ISR delivery. OHCI proves DMA through HCCA frame-number writeback. Its safe DOS/32A path verifies Start-of-Frame and PCI INTx assertion by polling but reports C4 WARN because it does not claim CPU ISR delivery.

Out of scope: that is the driver, not a qualifier.

- Full device enumeration (Address Device, Get Descriptor chains). One bounded, informational exception exists as test C8: single-device identification per root port (Enable Slot -> Address Device -> GET_DESCRIPTOR on EP0 only), DOSUSB-style, to show the user what is plugged in and to exercise the command and control-transfer path on real silicon. It never affects the section 8 verdict. Configured endpoints, hubs, and class traffic remain out of scope.
- Any data transfer beyond EP0 control, class-driver behavior, mass storage, HID.
- SuperSpeed / USB 3.x anything (see [What SuperSpeed Support Would Require](../../usb-xhci-info/xhci-programming.md#what-superspeed-support-would-require)).
- The `usbport.sys` miniport ABI (Phase 3's job, excluded here).

The line is drawn at "does the fundamental machinery respond, and can a PIC-mode OS receive its interrupts." Handoff OK, reset OK, DMA round-trip OK, IRQ fires, a port detects a connect, topology and quirks recorded, then stop.

## 3. Why DOS (and why not Win32, Win98, or Win2000)

The risks being tested (handoff, DMA, INTx delivery) are properties of the silicon and firmware, not of either target's software stack. The ideal test environment is the barest one that still routes a line interrupt.

- DOS is ring 0 with direct hardware access. It can do PCI config I/O (`0xCF8/0xCFC`), map high MMIO (via unreal mode / a DOS extender), hook the 8259 IRQ vector in the IVT, and receive a real hardware interrupt. User mode cannot do this on any Windows.
- DOS is inherently PIC mode, which is Win98's interrupt reality with the OS stripped away. If DOS-in-PIC receives the xHCI interrupt, Win98 (also PIC) almost certainly will; if it does not, Win98 will not. For Win2000 the same holds only on a PIC-based HAL; both uniprocessor and multiprocessor APIC HALs use the IOAPIC. See the routing caveat below.
- Install-free, and OS-free. Boot MS-DOS 7.1 from a stick, run one `.exe`. No INF, no reboot into a configured Windows, no NUSB, no SP4. A candidate machine can be qualified before either target OS is installed on it. That is why Phase 0 comes first.
- Total isolation. Removes `usbport.sys` and both driver models from the equation, so a failure points at hardware/firmware rather than at the driver design.

Why not the alternatives:

| Environment | Can read assigned IRQ/mem | Can read raw xHCI regs | Can prove interrupt fires | Notes |
|---|---|---|---|---|
| Win32 user-mode | Yes (`CM_*` APIs) | Needs a helper VxD | No (no user-mode ISR) | Good instant pre-filter only |
| DOS (this tool) | Yes | Yes | Yes (ring 0, IVT/PIC) | Portable bare-metal go/no-go, needs no OS installed |
| Standalone WDM driver | Yes | Yes | Yes, in that OS only | Definitive and reusable as Phase 4 code, but needs install/reboot, and confirms only the target it ran on |

DOS is the portable pre-commit qualifier; the WDM driver remains the target-exact confirmation, and since there are two targets, a WDM confirmation is per-OS. See the interrupt-delivery rationale in [`../implementation-invariants.md`](../implementation-invariants.md) ("Interrupt Delivery") and [`../architecture.md`](../architecture.md) ("IRQ and DPC Model").

### What a C4 PASS proves for each target

C4 proves two separable things: that the controller asserts INTx at all (spec 4.17.3 makes PCI interrupt pins optional, so this is a real risk and is target-independent), and that this machine's firmware routes that assertion to an 8259 IRQ the OS can take. The first transfers to both targets unconditionally. The second depends on the HAL the target OS runs:

| Target | Interrupt routing | What C4 covers |
|---|---|---|
| Win98 SE | 8259 PIC via the VMM/CONFIGMG | Both halves. The strong proxy this tool was built for |
| Win2000 SP4, Standard PC (`hal.dll`) or ACPI PC (`halacpi.dll`) | 8259 PIC | Both halves, same as Win98 |
| Win2000 SP4, ACPI Uniprocessor (`halaacpi.dll`) or ACPI Multiprocessor (`halmacpi.dll`) | IOAPIC | INTx assertion only. The IOAPIC routing path is untested by this tool |
| Win2000 SP4, MPS Uniprocessor (`halapic.dll`) or MPS Multiprocessor (`halmps.dll`) | IOAPIC | INTx assertion only. The IOAPIC routing path is untested by this tool |

The last two rows are the real gap, and the ACPI multiprocessor row is the likely configuration: a modern multi-core machine installing Win2000 gets the multiprocessor ACPI HAL. Consequences to keep straight:

- A C4 PASS says the controller asserts INTx and the PIC path works. For Win2000 on any APIC HAL (`halaacpi`, `halmacpi`, `halapic`, or `halmps`), treat the assertion as proven and the routing as unproven until the driver runs there.
- A C4 FAIL is disqualifying as stated in section 8, but read the note. If the controller never asserts INTx (Interrupt Pin = 0, MSI-only) the machine is dead for both targets, because neither has an MSI path (NT gained MSI in Vista; see [`../implementation-invariants.md`](../implementation-invariants.md), "Interrupt Delivery"). If instead the assertion happens but PIC routing is broken on this firmware, that is Win98-fatal and only suspicious for Win2000 under an APIC HAL.
- Nothing in the tool distinguishes those two failure modes beyond the Interrupt Pin value and the C4 note. Record both from the log.

## 4. Environment and toolchain

- Boot medium: MS-DOS 7.1 (the Windows 98 target DOS) on floppy, CD, or IDE/SATA storage is preferred. A USB boot stick is safe only when its controller is excluded from the active run and logs are written elsewhere. Boot USB relies on BIOS legacy-USB emulation, which itself touches the host controller; see the SMM caveat in section 9.
- Input: use a PS/2 keyboard where possible, or expect the USB keyboard to drop when the tool takes ownership of the controller. Disabling "Legacy USB Support" in BIOS is the clean path for the active tests.
- Toolchain, split by what each tier needs:
  - Tier A (real mode): PCI config space only. Builds with any DOS C compiler (OpenWatcom 16-bit, DJGPP, even Turbo C). No extender.
  - Tiers B-C (MMIO + interrupts): needs 32-bit flat access to high MMIO BARs and IVT/PIC control. The current build uses OpenWatcom C + DOS/32A, embedded as the EXE stub; DJGPP + CWSDPMI is an alternative. This is a new toolchain relative to the driver's MSVC 6.0 + Win2K DDK; keep the Phase 0 tool in its own build so it does not entangle the driver build.
- DMA memory. The rules matter, because a wrong physical address here produces the same silent DMA failure the tool exists to detect:
  - Allocate all controller-visible structures (DCBAA, command/event rings, ERST, scratchpad array and pages) from conventional memory (below 1 MB): DOS INT 21h/AH=48h in real mode, or DPMI 0x0100 "Allocate DOS Memory Block" from protected mode. Under a clean boot (raw or HIMEM-only, no EMM386) conventional memory is identity-mapped and physically contiguous, so linear address = physical address.
  - The budget is tighter than it looks. The boundary rules below force slack around each ring, and a controller with a real scratchpad adds a page each. Measured on the ThinkPad P14s Gen 1 (34 scratchpad buffers): roughly 430 KB for a single controller including the C8 buffers, a large fraction of a clean DOS boot's free conventional memory.

    Two consequences follow. Every safe block must be released at controller cleanup (DPMI 0x0101) so a machine with more than one controller can qualify them all in one run, and an allocation failure must be reported as its own outcome (C3 SKIP), never as a DMA failure (section 8). A block is safe to release only after controller reset completes or PCI BME is confirmed clear. If neither proof is available, retain its selector so no later controller can free/reuse it, report cleanup failure, and require a cold boot.
  - Do not use DPMI 0x0501 extended-memory allocations for DMA buffers: DPMI guarantees neither physical contiguity nor any standard call to learn the block's physical address.
  - DPMI 0x0800 (Physical Address Mapping) is for the opposite direction: it maps a known physical range (the controller's BAR0 MMIO) into the program's linear space. Use it for register access, never to "discover" a buffer's physical address.
  - Boot clean MS-DOS 7.1 (no EMM386/paging manager) when running the active tests. If a V86/paged environment is unavoidable, the correct API is VDS (INT 4Bh Virtual DMA Services) to lock regions and obtain physical addresses, but "boot clean" is the simpler and more reliable rule for a qualification tool.
  - Keep ring structures 64-byte aligned and scratchpad buffers page-aligned (per `PAGESIZE`) by over-allocating and aligning within the block, and honor the spec Table 6-1 boundary-crossing rules too: ring segments must not span a 64 KB physical boundary, and the DCBAA, contexts, and scratchpad array must not span a `PAGESIZE` boundary. The tool's `dma_alloc` takes an explicit boundary argument per structure (alignment alone does not prevent a 2 KB DCBAA from straddling a 4 KB page). It asks for the tight size first and only re-requests with a whole boundary of slack when the block DOS returned straddles one; requesting `size + align + boundary` unconditionally would cost 65 KB for a 1 KB command ring.

## 5. Tool architecture

Two build targets sharing a common core, so Tier A can run anywhere and the heavy tests are opt-in. This is the tool's source layout (its own directory, separate from the driver source):

```
tool source/
  pci.c        PCI config access (0xCF8/0xCFC), device scan, cap-list walk   [real mode OK]
  quirks.c     VID/DID -> known-quirk table (Intel/AMD/Renesas/ASMedia/VIA/Fresco)
  xhcicap.c    Map BAR0; decode capability + extended-capability registers    [needs extender]
  bringup.c    Handoff, halt/reset, DCBAA + rings, No-Op command, port reset  [needs extender]
  devid.c      C8: Enable Slot, Address Device, GET_DESCRIPTOR device listing [needs extender]
  legacy.c     EHCI/OHCI capability, handoff, reset, DMA, IRQ, port tests     [needs extender]
  irq.c        IVT hook, 8259 mask/EOI, ISR, poll-vs-interrupt differential   [needs extender]
  report.c     Per-controller fact sheet + PASS/FAIL verdict; console pager
  main.c       Sequencing, family selection (default all), CLI flags
               (--probe-only/--no-active, --poll-only, --irq-selftest, --set-intel-ports,
               --no-wait, --no-devid, --no-page, --serial, --log [filename],
               --done-flag FILE)
```

`--probe-only` runs Tier A and any Tier B reads the firmware's existing PCI state permits. It is read-only: it never enables MSE, clears INTx Disable, writes PMCSR, or takes ownership. If MSE is clear, the report records that Tier B was unavailable and leaves the bit unchanged. The default runs the full suite.

`--poll-only` is a third, intermediate mode. It takes ownership and runs the active C1/C2/C3/C6 gates by foreground event-ring polling but installs no protected-mode interrupt handler (C4 is reported `SKIP`, the verdict `PROVISIONAL`). It is the safe active probe after a DOS/32A interrupt-path fault, because it isolates the controller/port/DMA behavior from the extender's interrupt reflection. In the full mode the interrupt source is shut down and the vector removed the instant C4 finishes, so C6 never resets a port with a live handler installed. Active tests always perform the BIOS handoff first and restore controller state on exit.

Console output is paginated by default (a `-- More --` pause every screenful, ESC to stop pausing) so the report can be read on bare metal; `--no-page` disables it and `--serial` implies it off, since serial capture is non-interactive. The log and serial copies are never paginated. Each build also emits `XHCIQUAL.MAP` and stamps a build identifier into the run header for post-fault EIP resolution. No log file is created by default. Pass `--log` to save the report as `XHCIQUAL.LOG`, or `--log filename` to choose a filename (`--log=filename` is also accepted).

`--irq-selftest` is an isolated xHCI C4 path: it performs prerequisite C1/C2/C3 initialization, verifies the test's own interrupt-pending state while the PIC line remains masked, exposes one interrupt to a locked assembly handler, tears down, and skips C6/C8. Family selectors are repeatable. Examples: `XHCIQUAL xhci`, `XHCIQUAL --scan ehci`, and `XHCIQUAL --scan ehci --scan ohci`.

## 6. Test matrix

Register offsets below are relative to BAR0 unless noted. Capability regs at BAR0+0; operational regs at BAR0+`CAPLENGTH`; runtime regs at BAR0+`RTSOFF`; doorbells at BAR0+`DBOFF`.

The detailed matrix below is the xHCI matrix. EHCI/OHCI reuse its C1/C2/C3/C4/C5/C6 meanings with the family-specific mechanisms described in section 2. C7 and C8 remain xHCI-only.

### Automated QEMU regression matrix (40 cases)

`xhciqual/test/run-qemu-matrix.ps1` cold-boots a fresh Win98SE (MS-DOS 7.1) QEMU process per case, the same DOS the field procedure uses. The matrix stands at 40 cases as of v0.11 (`xhciqual/README.md` carries the version history). The `HCCPARAMS2` line (row B9) is pinned in the `xhci_probe_only` case, so a regression in the read-only U3C/CMC/FSC report fails the matrix.

The cases are: standalone help, an isolated locked xHCI IRQ self-test, two `--probe-only` cases, and the controller/device cases. Controller presence is exhaustive across the requested families: none, xHCI, EHCI, OHCI, xHCI+EHCI, xHCI+OHCI, EHCI+OHCI, and all three. Empty and attached-device variants cover xHCI HS storage, FS HID, a hub with downstream HID, and an unmanaged SuperSpeed-only placement; EHCI HS storage and FS HID; OHCI HID and storage; pairwise mixed attachments; and all-three mixed attachments. It also covers default-all scanning, combined selectors, selecting an absent family, and --log ohci log-name/selector disambiguation.

Four `--poll-only` cases (xHCI empty, xHCI HS storage, an xHCI hub that produces repeated port-status-change events, and EHCI HS storage) confirm the no-ISR active path drains the event ring, reaches a `PROVISIONAL` verdict, and completes with no DOS/32A exception.

The two `--probe-only` cases enforce the read-only contract by requiring the `Probe safety: PASS - no PCI configuration writes.` line the tool prints from its own write counter. One runs xHCI alone; the other scans xHCI + EHCI + OHCI together, because the config writes that must stay behind the active gate (EHCI legacy handoff, the PCI 2.3 INTx-status probe, cleanup) live in the legacy paths, and the safety line is global to the run.

### Host-side unit tests

`xhciqual/test/run-host-tests.cmd` builds and runs `xhciqual/test/test_mmiodiag.c` on the Windows build host in seconds, with no VM and no DOS. It covers `xhciqual/mmiodiag.c`, the pure `PCIINFO` -> report-text logic behind the PCI Power Management block and the two dead-MMIO classifiers.

That code is a separate translation unit so it can be tested here, because the QEMU matrix cannot reach any of it. SeaBIOS always leaves Memory Space Enable set and the BAR assigned, so `mmio_ok` is always true and neither classifier runs, and no emulated USB controller exposes a PM capability, so only the "capability absent" branch executes. Without the host runner, every other branch would first execute on a field machine.

It doubles as the cheapest check on the PCI PM bit positions (section 11): the expected strings are transcribed by hand from the specification's field definitions rather than generated by the code under test, the same rule design doc 03 applies to TRB golden vectors. It does not replace the bare-metal `lspci -vv` cross-check, which is what validates the transcription itself.

The matrix runner requires expected serial results and normal completion, scans serial plus VGA text for DOS/32A exceptions and CPU faults, captures QEMU stderr, uses a new monitor-port range per invocation, and stops every VM in a `finally` block. It is exhaustive for controller-presence masks but representative, not exhaustive, for the unbounded universe of USB device types and hub depths.

The matrix boots the bare Win98SE (MS-DOS 7.1) target DOS, the same shell as the field procedure, so it runs the qualifier on the `COMMAND.COM` it will meet in the field (an earlier FreeDOS stand-in masked a `%ERRORLEVEL%` batch bug; see lessons.md). A companion harness, `xhciqual/test/run-win98-batch.ps1`, drives the `.BAT` field wrappers by name on the same disk; those wrappers use only built-ins (`IF ERRORLEVEL` for the exit code, `IF EXIST` on the `--done-flag` completion sentinel, no `FIND.EXE`). The Win98 boot image is proprietary and supplied locally, not committed, so both harnesses skip cleanly when it is absent.

QEMU also cannot validate real BIOS ownership or motherboard PIC routing, and its `pci-ohci` implements the PCI 2.3 Interrupt Disable/Status mechanism, so the pre-PCI-2.3 SOF-only C4 WARN branch (`PCI 2.3 INTx status unsupported`) is reachable only on real hardware. Bare-metal testers should expect it on older machines even though no QEMU log shows it.

During development, enabling OHCI SOF at the protected-mode vector caused a repeatable DOS/32A general-protection fault before even a minimal ISR could safely acknowledge it. The released test does not retain that crash path: it polls SOF and PCI Interrupt Status, reports C4 WARN, and leaves CPU-level OHCI interrupt delivery as bare-metal follow-up. Because the PCI Interrupt Status bit is a PCI 2.3 (2002) addition that most OHCI-era chipsets hardwire to zero, the test treats PCIe as modern and otherwise probes the paired PCI 2.3 Interrupt Disable bit. If neither mechanism is present, SOF without PCI status is C4 WARN; otherwise, failure to observe PCI Interrupt Status is C4 FAIL. A controller that never asserts SOF is also C4 FAIL.

### Tier A - Discovery and static facts (real mode, always safe)

| # | Test | How | Pass / record |
|---|---|---|---|
| A1 | Enumerate xHCI | Scan bus/dev/func; config dword 0x08, match class/subclass/prog-if `0x0C0330` | Count, BDF, VID/DID, revision |
| A2 | PCI Command + Status | Config 0x04: Memory Space Enable, Bus Master Enable; can BME be set. Config 0x06: the sticky bus-error bits (master/target abort, parity, SERR), snapshotted before the run and re-read after the active tests. Config 0x2C/0x2E: subsystem IDs, which name the board the silicon is fitted to | BME settable (DMA prerequisite). Error bits are reported, not judged: they are RW1C and can predate the run by a boot, so only a bit that turns on across the active tests is attributed to the tool's own traffic, and even that is verdict-neutral, since a shared bus means the neighbour could be the culprit. Never cleared: clearing would destroy evidence for the next diagnosis |
| A3 | BAR0 | Config 0x10: address, 32/64-bit, prefetchable | Below 4 GB. The driver is 32-bit on both targets (Win2000/PAE could map higher, but this project does not) |
| A4 | Capabilities | Walk cap ptr (0x34): MSI (0x05), MSI-X (0x11), PM (0x01), PCIe; Interrupt Pin (0x3D) / Line (0x3C). For PM, read the capability's contents too: PMC (cap+2) and PMCSR (cap+4) | Pin != 0 and Line in 0-15 (see [invariants](../implementation-invariants.md) "Interrupt Delivery"). Record current D-state, D1/D2 support, PME_En/PME_Status, PME_Support list, capability version, No_Soft_Reset, DSI, PME_Clock, Aux_Current, and the raw PMC/PMCSR words. These are Win2000 inputs: it does real D-state transitions and acts on PME, so they pair with the PME-stuck quirk (Linux `XHCI_PME_STUCK_QUIRK`, reported here as `QF_PME_STUCK`); No_Soft_Reset and DSI in particular say how much a resume path must redo. Read-only: the qualifier never writes PMCSR, which also rules out reading the Data register (that needs Data_Select written first) |
| A5 | Quirk lookup | VID/DID against a small table | Intel PCH, AMD, NEC/Renesas uPD72020x, ASMedia ASM104x/114x/214x, VIA, Fresco Logic (broken-MSI) |
| A6 | Intel port regs | If Intel: read config `XUSB2PR` (0xD0), `XUSB2PRM` (0xD4), `USB3_PSSEN` (0xD8), `USB3PRM` (0xDC) | Report routing state (see C7) |

### Tier B - Capability introspection (needs MMIO)

| # | Test | How | Record |
|---|---|---|---|
| B1 | Context size | `HCCPARAMS1` (0x10) bit CSZ | 32- vs 64-byte contexts, an [invariant](../implementation-invariants.md) the driver depends on |
| B2 | Addressing | `HCCPARAMS1` AC64 (bit 0), PPC (bit 3) | 64-bit capable? Port Power Control? |
| B3 | Sizes | `HCSPARAMS1` (0x04): MaxSlots, MaxIntrs, MaxPorts | Slot/interrupter/port counts |
| B4 | Scratchpad | `HCSPARAMS2` (0x08): Max Scratchpad Buffers = (Hi[25:21]<<5) \| Lo[31:27]; bits 25:21 are the high 5 bits, bits 31:27 the low 5 | If > 0 the driver must allocate that many page buffers or the HC will not run |
| B5 | Version / page | `HCIVERSION` (0x02); `PAGESIZE` (op 0x08) | 1.0/1.1/1.2; supported page size. Bit 0 (4 KB) must be set before carving page-aligned structures; the allocator assumes it |
| B6 | Legacy cap | Walk extended caps (`HCCPARAMS1.xECP`): find USBLEGSUP (ID 1) | Handoff semaphore present (drives C1) |
| B7 | Port topology | Supported Protocol caps (ID 2): USB2 vs USB3 port offset/count, slot type | The USB2 protocol ports the driver will manage |
| B8 | Speed IDs | Same caps: PSIC (`0x08` bits 31:28) and the Protocol Speed ID dwords that follow at `+0x10` | When PSIC > 0 the controller defines its own PSIVs, so the default 1=FS/2=LS/3=HS/4=SS mapping does not apply. Retain all 15 possible entries and decode PORTSC speeds against the advertised table (spec 7.2.2.1.2). An absent PSIV remains unknown. Both Intel machines tested advertise a table |
| B9 | Save-state capability | `HCCPARAMS2` (0x1C) bits 0-2: U3C, CMC, FSC. Read only when `CAPLENGTH >= 0x20`, and all ones refused | FSC decides whether `xhci98` may take a Save State at all. With `FSC = 0` the driver declines every `CSS` (4.23.2 p.313 needs Stop Endpoint on Idle Running endpoints too, which the suspend path does not issue), so a suspend/resume pair rebuilds the bus instead of restoring it (`docs/using/release-notes.md`, "Known limitations"). The bit is read rather than inferred from HCIVERSION, which Appendix H.1.6 (p.593) forbids: FSC is optional at xHCI 1.0, not absent, so a 1.0 part may advertise it. The gate is reach, not version: a controller whose `CAPLENGTH` stops at `0x1C` has its operational registers on that address, so the read is skipped and reported as `absent` rather than printed as a 0. Only bits 0-2 are decoded, being the ones `docs/usb-xhci-info/xhci-data-structures.md` transcribes; the raw DWORD is printed beside them so nothing undecoded is lost. The `FACT` line carries the reading as `fsc=`, and `fsc=-1` is a third answer meaning not readable (register absent, or all ones), so a consumer cannot fold it into 0; the `HCCPARAMS2` line prints `absent` in that case |

### Tier C - Active bring-up de-risk (the silent-death gates)

| # | Test | How | Pass |
|---|---|---|---|
| C1 | BIOS->OS handoff | Set OS-Owned in USBLEGSUP; poll BIOS-Owned | Clears within timeout (driver's first action; a hang here dooms Phase 4) |
| C2 | Halt + reset | Clear R/S, wait HCH; set HCRST, wait it + CNR clear; time it | Reset completes; record latency |
| C3 | DMA round-trip | Alloc DCBAA + command ring + event ring; write CRCR/ERSTBA/ERDP; enqueue a No-Op Command TRB (type 23); ring DB[0] | A Command Completion Event (type 33, code Success) appears on the event ring -> bus-master DMA works |
| C4 | Interrupt delivery | Hook the Line-register IRQ vector (IRQ0-7->INT 08-0F, 8-15->INT 70-77); unmask in 8259; enable IR0.IE + USBCMD.INTE; power a port | ISR fires on a port event; EOI correctly |
| C5 | Poll-vs-interrupt | Also poll USBSTS.EINT / event ring for the same events | Poll sees event but ISR does not -> routing failure pinpointed (vs dead controller) |
| C6 | Port connect + reset | Power a USB2 port (PP=1 if PPC); plug a device; observe CCS/CSC; issue Port Reset (PR); read speed | Connect detected, reset completes, plausible speed |
| C7 | Intel switchover | If Intel and USB2 ports look unrouted: set `XUSB2PR` to route switchable USB2 ports to xHCI, leave `USB3_PSSEN` unchanged for this USB2-only project, then re-run C6. The verdict is pure PCIINFO->text in `mmiodiag.c` (`report_xusb2pr`) because no QEMU case reaches C7 at all, so the host runner is its only coverage. Neither decided branch has ever executed on hardware, because no machine in this project has an Intel 7/8-series mux. It reports UNDETERMINED rather than "routed" when `XUSB2PRM` is `0` or all ones, or `XUSB2PR` is all ones: a zero mask makes "every switchable port is routed" true by containing nothing, and that false negative would retire the very question C7 exists to answer. The `--set-intel-ports` write is held to the same rule. Its read back is re-classified through the same function into three outcomes: confirmed; the write demonstrably did not take; or the read back cannot say, where the report declines to attribute the following C6 result to routing or to the ports, since "the write did not take" is itself a claim that needs readable words | USB2 ports now detect connects (classic "no devices show up" trap) |
| C8 | Device identification (informational) | Per port that passed C6: Enable Slot -> Address Device (BSR=0) -> GET_DESCRIPTOR device/config/strings on EP0 (FS: Evaluate Context to fix MPS0) -> Disable Slot | VID/PID, class, interface classes, strings reported (`DEV` lines); proves Enable Slot/Address Device/control transfers work. Never part of the section 8 go/no-go |

### Tier D - Report

Emit a per-controller qualification report: the overall verdict plus every fact gathered (CSZ, slot/port counts, scratchpad count, USB2 topology, IRQ, quirks, and PASS/FAIL for handoff/reset/DMA/IRQ). This is not only go/no-go; it is the seed for the Phase 4 quirk and parameter tables.

## 7. Silent-death gates (why Phase 0 pays for itself)

Four steps fail with no useful error if the hardware/firmware misbehaves; the driver would just hang or see nothing. Phase 0 exercises each in isolation, on bare metal:

1. BIOS->OS handoff (C1). Some BIOSes never release ownership or SMI-storm.
2. Bus-master DMA (C3). If the controller cannot read your rings or write events back, nothing works.
3. INTx interrupt delivery in PIC mode (C4/C5). The core risk; MSI is unreachable on both targets (NT gained MSI only in Vista).
4. Intel port switchover (C7). Ports silently absent until routed to xHCI.

## 8. Go / No-Go criteria

A machine is qualified for the driver (either target; see section 3's per-target table for how far C4 transfers) when the tool reports PASS for C2 (reset), C3 (DMA round-trip), and C4 (interrupt on a legacy 8259 IRQ), C1 (handoff) is PASS or WARN, and it prints a non-empty USB2 port topology (B7) and context size (B1).

C1 is not a hard gate. Some firmware never clears the BIOS-Owned semaphore yet stops touching the controller anyway, so a handoff timeout is reported WARN and the run continues; the machine can still qualify, but only as "QUALIFIED (with warnings)". A C1 that hangs the machine is a different outcome: no verdict is printed at all.

A machine is disqualified if any of the following hold, regardless of other results:

- Interrupt Pin = 0 / MSI-only, or C4 never fires -> neither target's line-based stack gets interrupts. Interrupt Pin = 0 disqualifies for both; a PIC-routing failure with a non-zero pin is Win98-fatal and unproven for Win2000 under an APIC HAL (section 3).
- C3 never completes -> DMA/bus-mastering is broken.
- BAR0 above 4 GB with no 32-bit alias -> incompatible with the driver's 32-bit addressing on both targets.
- C2 halt/reset fails, or the controller reports no USB2 protocol ports.

Two outcomes are neither a pass nor a disqualification, and both force "QUALIFIED (with warnings)" or better wording rather than a clean verdict:

- C6 SKIP or FAIL. SKIP means no device was attached, so connect/reset was never exercised; FAIL means a connected port refused to reset. Neither condemns the silicon on its own (a bad cable or an unrouted Intel port mux, C7, produces both), but neither may be reported as a clean QUALIFIED.
- C3 SKIP. The tool could not run C3 at all: out of conventional memory, or PAGESIZE does not advertise 4 KB support. This is a tool limitation, not evidence about the controller. The run reports NOT QUALIFIED with the reason, and must not claim bus-mastering is broken.

C8 is never part of this section (see the Tier C table).

## 9. Caveats and limitations

- Strong proxy, not identity. DOS PIC routing closely matches Win98 PIC routing, but Win98's VMM/CONFIGMG does its own PCI IRQ steering and could differ. The standalone WDM confirmer is the only test that is byte-for-byte Win98.
- Weaker proxy for Win2000 on an APIC HAL. Under `halaacpi.dll`, `halmacpi.dll`, `halapic.dll`, or `halmps.dll`, the same INTx assertion is routed through the IOAPIC instead of the 8259, and this tool never exercises that path (section 3, "What a C4 PASS proves for each target"). The tool is OS-agnostic by construction, but its interrupt evidence is not equally strong for both targets.
- BIOS legacy-USB / SMM. The firmware's SMM USB emulation may already own the xHCI. The active tests must perform the C1 handoff, and ideally "Legacy USB Support" is disabled and a PS/2 keyboard used. Otherwise the tool fights SMM and can freeze the keyboard.
- Shared IRQ. A production ISR must chain the prior vector after checking ownership. DOS/32A's reflector cannot be reached safely with `_chain_intr`, so the qualifier instead masks a not-ours line before EOI, reports C4's foreign count, restores the old vector while masked, and only then restores the saved PIC masks. This prevents a level-IRQ livelock but does not prove coexistence with the foreign device; a DPMI-correct tail-chain is needed for that stronger test.
- New toolchain for Tiers B-C (DOS extender) versus the driver's MSVC 6.0.

## 10. Relationship to the rest of the project

- Phase 0 does not replace Phase 3 (the miniport-registration go/no-go) or Phase 4 (real init under `usbport`). It de-risks the hardware before those, and its C1-C6 code is a close cousin of Phase 4's init sequence (handoff, reset, rings, ports), a useful reference even though the driver runs under `usbport.sys` and does not own `IoConnectInterrupt`.
- A verdict is per-controller, not per-OS. A QUALIFIED machine is a candidate for Win98 SE, for Win2000 SP4, or for both; the tool asks nothing about which OS will be installed and does not need one installed to run.

  What it does not do is discharge Phase 13's per-target bare-metal clause ("at least one real machine confirmed working per target OS"): the shared risks it clears sit below the miniport, and everything above the miniport differs between the two stacks.

  That clause was the phase's main requirement and was not met for Windows 2000 (Setup bugchecks on every machine this project has), so Phase 13 closed on its published-limitation branch, with Windows 98 metal and Windows 2000 virtual-machine evidence only (roadmap, Phase 13 checkpoint; `build-and-test.md`, the "Per-target-OS coverage" paragraph under "Controller identity and qualification status"). Section 3's C4 table is the one place the evidence itself is target-dependent.
- The facts in the Tier D report feed the Phase 4 parameter tables; archive confirmed per-controller findings under `xhciqual/results/<machine>-<date>/`. There is no per-controller quirk document to fold them into: the driver acts on no controller-specific workaround, and what survives on that subject is `docs/usb-xhci-info/xhci-programming.md`, "Firmware Handoff, and the Controller Deviations This Driver Acts On".

## 11. Open questions / future work

- Toolchain and build: settled. Open Watcom 2.0 (`C:\WATCOM`) with DOS/32A embedded as the EXE stub (`wlink system dos32a`), a single 32-bit build covering all tiers (`xhciqual/build.cmd`). Every xHCI machine is 386+, so there is no separate real-mode Tier A build; `--probe-only` covers the safe subset. The QEMU regression is the `xhciqual/test/run-qemu-matrix.ps1` matrix, and `run-qemu-test.cmd` remains as an interactive smoke test. Both QEMU harnesses boot the bare Win98SE/MS-DOS 7.1 target DOS directly, so CI runs on the same `COMMAND.COM` as the field.
- Grow the `quirks.c` VID/DID table as real controllers are tested, taking entries from Linux [`xhci-pci.c`](https://github.com/torvalds/linux/blob/master/drivers/usb/host/xhci-pci.c); archive the runs that justified them under `xhciqual/results/`.
- A deeper command-path check after C3: done, and extended into test C8, informational per-port device identification (Enable Slot -> Address Device -> GET_DESCRIPTOR, DOSUSB-style), skippable with `--no-devid`; see section 2.
- A per-machine results log is worth keeping as more machines are tested. Decide whether that lives with the tool source or as a further numbered design doc here in `docs/contributing/design/`.
- The A4 power-management decode is verified against a primary source. The bit positions come from the PCI Bus Power Management Interface Specification rev 1.2, which (unlike the xHCI spec) is not mirrored in `docs/references/`, and the populated path is unreachable in QEMU (no emulated USB controller exposes a PM capability).

  A bare-metal run on the ThinkPad E460 exercised it and an `lspci -vv` cross-check of the same function agreed field for field, hardwired capability bits included (`xhciqual/results/e460-2026-07-25/README.md`). That comparison also showed which fields were missing, and v0.9 added them (version, No_Soft_Reset, DSI, PME_Clock, Aux_Current) plus the raw PMC/PMCSR words, so a future disagreement is re-decodable from the log.

  Still worth mirroring the PCI PM spec into `docs/references/` if this area grows. The Data register (power consumption/dissipation) remains undecoded on purpose: reading it requires writing Data_Select, which the read-only contract forbids.
- Testing IOAPIC interrupt routing was considered and rejected for the C4 gap in section 3. Reprogramming redirection entries under a PIC-mode DOS risks hanging the machine, and it still would not reproduce Win2000's routing, which its HAL programs from ACPI `_PRT` tables this tool does not parse. The Windows 2000 SMP virtual machine exercises an emulated APIC/IOAPIC route; real Windows 2000 hardware routing remains unobserved, because Phase 13 closed without a Windows 2000 machine (section 10), and the documented caveat is the resolution. Parsing the ACPI MADT to predict which HAL Win2000 will install was rejected for the same cost/value reason: every candidate machine is multicore and will get `halmacpi`.
