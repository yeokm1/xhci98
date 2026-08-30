# ThinkPad E460 - XHCIQUAL v0.9 bare-metal run (2026-07-25)

Field logs from the staged five-stage xHCI batch sequence
(`1PROBE`/`2XPOLL`/`3XIRQ`/`4XEMPTY`/`5XDEV`), kept verbatim as the evidence
behind "E460 QUALIFIED under v0.9". Format of this record follows
"What to record for each machine" in [hardware-testing.md](../../hardware-testing.md).

```text
Machine/model:      Lenovo ThinkPad E460
Chipset/CPU:        Intel Sunrise Point-LP (100-series), xHCI 8086:9D2F rev 21
BIOS version/date:  (not recorded)
Legacy USB setting: (not recorded)
xHCI/EHCI handoff settings: (not recorded; USBLEGSUP present, C1 handoff PASS)
xHCI mode/routing setting:  (not recorded)
DOS version and boot medium: MS-DOS 7.1 (Win98)
PS/2 input available: n/a (laptop)

Build stamp:        XHCIQUAL 0.9 (build Jul 25 2026 20:29:27)
Controller FACT:    id=8086:9D2F rev=21 bar=E1220000 irq=11 pin=1 hciver=0100
                    csz=32 ac64=1 ppc=0 slots=64 intrs=8 ports=18 scratch=34
                    usb2ports=12 legsup=1
PCI subsystem:      17AA:5048 (Lenovo)
Physical port -> controller mapping:
                    ports 1-12 USB 2.0 (managed), ports 13-18 USB 3.0
                    (unmanaged); internal devices on ports 7 and 8; the
                    left-hand external USB connector is controller port 3
                    for a USB 2.0 device (see notes)

PROBE:  completed, no disqualifiers, no verdict (read-only), probe safety PASS
XPOLL:  completed, C2/C3/C6 PASS, C4 SKIP, PROVISIONAL, no DOS/32A fault
XIRQ:   completed, C4 PASS (1 ISR entry on IRQ 11), IRQ SELF-TEST PASS
XEMPTY: completed, C1/C2/C3/C4/C6 PASS, C8 2/2, QUALIFIED (cross-target)
XDEV:   completed, C1/C2/C3/C4/C6 PASS, C8 3/3, QUALIFIED (cross-target)

Unexpected behavior and last printed line: none; every log ends "Done."
Cold-boot retry result: (not recorded)
```

## Cross-check against `lspci` - PASS

[hardware-testing.md](../../hardware-testing.md),
"Cross-checking the PCI block against `lspci`", requires a machine's PCI decode
to be verified once against an independent decoder - the PM capability above
all, because nothing in `docs/references/` mirrors the PCI Bus Power Management
specification and no QEMU case populates it. Done here and passing; it does not
repeat per run.

Linux on this same E460, `lspci -vv -s 00:14.0` in full - kept complete rather
than trimmed to the PM capability, because it cross-checks more of the PCI
block than PM alone:

```
00:14.0 USB controller: Intel Corporation Sunrise Point-LP USB 3.0 xHCI Controller (rev 21) (prog-if 30 [XHCI])
        Subsystem: Lenovo Device 5048
        Control: I/O- Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+
        Status: Cap+ 66MHz- UDF- FastB2B+ ParErr- DEVSEL=medium >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
        Latency: 0
        Interrupt: pin A routed to IRQ 123
        IOMMU group: 1
        Region 0: Memory at e1220000 (64-bit, non-prefetchable) [size=64K]
        Capabilities: [70] Power Management version 2
                Flags: PMEClk- DSI- D1- D2- AuxCurrent=375mA PME(D0-,D1-,D2-,D3hot+,D3cold+)
                Status: D0 NoSoftRst+ PME-Enable- DSel=0 DScale=0 PME-
        Capabilities: [80] MSI: Enable+ Count=8/8 Maskable- 64bit+
                Address: 00000000fee00318  Data: 0000
        Kernel driver in use: xhci_hcd
        Kernel modules: xhci_pci
```

### What agrees

Everything the qualifier decodes from hardwired configuration:

| XHCIQUAL v0.9 | `lspci -vv` | |
|---|---|---|
| `v2` | `Power Management version 2` | match |
| `PME_Support: D3hot, D3cold` | `PME(D0-,D1-,D2-,D3hot+,D3cold+)` | match |
| `D1=0  D2=0` | `D1- D2-` | match |
| `state=D0` | `Status: D0` | match |
| `PME_En=0` | `PME-Enable-` | match |
| `PME_Status=0` | trailing `PME-` | match |
| `NoSoftRst=1` | `NoSoftRst+` | match |
| `DSI=0 PMEClk=0` | `DSI- PMEClk-` | match |
| `Aux=375mA` | `AuxCurrent=375mA` | match |
| `raw: PMC=C1C2 PMCSR=0008` | (the two words all of the above decode from) | - |
| `PCI subsys: 17AA:5048` | `Subsystem: Lenovo Device 5048` | match (17AA = Lenovo) |
| `BAR0: E1220000 64-bit`, no `prefetchable` | `Memory at e1220000 (64-bit, non-prefetchable)` | match |
| `PCI caps: PM=1 MSI=1 MSI-X=0 PCIe=0` | only `[70]` PM and `[80]` MSI present | match, including the two absences |
| PCI status block silent | `>TAbort- <TAbort- <MAbort- >SERR- <PERR- ParErr-` | match - no error bits set |

The part that matters is the hardwired fields, which no OS can change: the
`PME_Support` mask, D1/D2 support, `DSI`, `PMEClk`, `Aux`, the subsystem IDs,
the BAR attributes, and which capabilities exist at all. The live PMCSR fields
(`state`, `PME_En`, `PME_Status`) agreeing as well is a bonus rather than
proof, since Linux had `xhci_hcd` bound by then and could legitimately have
moved the D-state. So the decoder is validated on real silicon, and the
capability walk is confirmed too: `PCIe=0` is right, this PCH-internal
function genuinely exposes no PCIe capability structure.

### What differs, and why none of it is a disagreement

Three values differ from what DOS saw, and they are one story rather than
three problems: Linux switched the controller to MSI.

| | DOS (qualifier) | Linux (`lspci`) |
|---|---|---|
| MSI enable | `MSI=1(en=0)` | `MSI: Enable+ Count=8/8` |
| INTx Disable | `INTxDis=0` (cmd `0006`) | `DisINTx+` |
| Interrupt routing | `line=IRQ 11` | `pin A routed to IRQ 123` |

Enabling MSI requires setting INTx Disable, and the resulting vector is 123
rather than a legacy PIC line. All three are writable state that a driver owns,
so they say nothing about the silicon. All three are also what the target
drivers must not do: neither Win98 nor Win2000 SP4 has an MSI path (NT gained
MSI in Vista), so the driver depends on MSI staying disabled and INTx staying
enabled, which is the state DOS observed.

The `IRQ 123` line is worth pausing on. It is the same pin A that DOS reports
as `IRQ 11`, routed through the IOAPIC instead of the 8259. That is the
difference behind the `Win2000 APIC-HAL routing remains untested by DOS C4`
caveat in these logs, made concrete on this machine. C4 proved the 8259 path
here; the 123 path is the one a Win2000 ACPI multiprocessor HAL would use and
that DOS cannot exercise.

### What was left out of the tool, and why

The full output was mined for anything else the qualifier should report. It
yielded the subsystem IDs and the sticky PCI Status error bits, both now in
v0.9. Left out on purpose:

- BAR size (`[size=64K]`): determining it means writing all-ones to the BAR
  and restoring it, which breaks the read-only probe contract. The driver
  gets its register-space layout from CAPLENGTH/RTSOFF/DBOFF anyway.
- `Interrupt: pin A routed to IRQ 123`: Linux's IOAPIC/MSI vector, not a
  config-space value, and unreachable from DOS.
- MSI `Count=8/8`, `Maskable-`, `64bit+`, `Address`/`Data`: the enable bit is
  the one that matters and is already reported; neither target has an MSI
  path at all.
- `Latency: 0`, `DEVSEL=medium`, `66MHz-`, `FastB2B`, `UDF-`, `IOMMU group`,
  `Kernel driver`: no bearing on either target.

### Facts for Phase 8

Two from the PM block are Win2000 inputs rather than qualification inputs:

- `NoSoftRst=1`: the controller retains internal state across a D3hot -> D0
  transition, so a resume path need not treat every D0 return as a cold
  controller. A per-device capability bit, not a family guarantee; confirm it
  per controller before relying on it.
- `PME_Support: D3hot, D3cold`: it can assert PME from both D3 states, so the
  `QF_PME_STUCK` quirk on this VID/DID is a live work item here rather than a
  Win98 footnote. That the wake path exists is not evidence the latch actually
  sticks; that part remains a Linux-sourced quirk claim.

## Notes

- The PCI status block is silent in every log, which is the clean case: no
  bus-error bit was set before the run, and none turned on across the active
  tests (`lspci` agrees, in the table above).
- `XDEV` used a real external device in the left-hand USB connector: a SanDisk
  U3 Titanium (`0781:5408`, USB 2.00 mass storage), which enumerated at
  High-Speed on controller port 3. That makes `XDEV` a genuine variation on
  `XEMPTY` rather than a near-duplicate, and it is the first physical
  connector -> controller port mapping recorded for this machine.
  - The qualifier only sees the USB 2.0 half of that connector. A USB 3.0
    device in the same physical socket would land on one of ports 13-18, which
    the report marks `unmanaged` and the driver's USB2 scope does not cover.
    So "the left connector is port 3" holds for USB 2.0 devices specifically.
  - The other external connectors are still unmapped: managed USB2 ports 1, 2,
    4, 5, 6 and 9-12 stayed empty through every stage and are the remaining
    candidates.
- The other two devices are internal and cannot be unplugged: `5986:0708`
  SunplusIT Integrated Camera (High-Speed, port 7) and `138A:0011` fingerprint
  reader (Full-Speed, vendor-specific, port 8). So the "keep external test
  devices disconnected" instruction on the earlier stages applies only to
  external ones, and `XEMPTY` still enumerates those two.
- Port 6 is empty in this run. It is a managed USB2-only port, so a device
  appearing there later is expected to behave like ports 7 and 8.
- C6 and C8 therefore cover both Full-Speed and High-Speed on managed USB2
  ports, across a mass-storage, a video-class and a vendor-specific device.
- The logs were transcribed from the machine; trailing whitespace on the
  `Families:` line was not preserved. Everything else is verbatim.
