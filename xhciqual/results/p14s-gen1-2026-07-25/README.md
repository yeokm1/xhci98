# ThinkPad P14s Gen 1 (Intel) - XHCIQUAL v0.9 bare-metal run (2026-07-25)

Field logs from the staged five-stage xHCI batch sequence
(`1PROBE`/`2XPOLL`/`3XIRQ`/`4XEMPTY`/`5XDEV`), kept verbatim as the evidence
behind "P14s Gen 1 QUALIFIED under v0.9". Format of this record follows
"What to record for each machine" in [hardware-testing.md](../../hardware-testing.md).

This is the current record for this machine; the earlier one it replaced has
been retired. Both fleet Intel machines now run the same binary as the
[E460](../e460-2026-07-25/README.md).

```text
Machine/model:      Lenovo ThinkPad P14s Gen 1 (Intel)
Chipset/CPU:        Intel Comet Lake PCH-LP (400-series), xHCI 8086:02ED rev 00
BIOS version/date:  (not recorded)
Legacy USB setting: (not recorded)
xHCI/EHCI handoff settings: (not recorded; USBLEGSUP present, C1 handoff PASS)
xHCI mode/routing setting:  (not recorded)
DOS version and boot medium: MS-DOS 7.1 (Win98)
PS/2 input available: (not recorded; "no" would be an inference from the machine being xHCI-only, not a reading; see `build-and-test.md`, "Bootstrapping xHCI-only machines")

Build stamp:        XHCIQUAL 0.9 (build Jul 25 2026 20:29:27)
Controller FACT:    id=8086:02ED rev=00 bar=E33A0000 irq=11 pin=1 hciver=0110
                    csz=32 ac64=1 ppc=0 slots=64 intrs=8 ports=18 scratch=34
                    usb2ports=12 legsup=1
PCI subsystem:      17AA:22B1 (Lenovo)
Physical port -> controller mapping:
                    ports 1-12 USB 2.0 (managed), ports 13-18 USB 3.1
                    (unmanaged); internal devices on ports 6, 8, 9 and 10; the
                    left-hand USB-A connector is controller port 4 for a
                    USB 2.0 device (see notes)

PROBE:  completed, no disqualifiers, no verdict (read-only), probe safety PASS
XPOLL:  completed, C2/C3/C6 PASS, C4 SKIP, PROVISIONAL, no DOS/32A fault
XIRQ:   completed, C4 PASS (1 ISR entry on IRQ 11), IRQ SELF-TEST PASS
XEMPTY: completed, C1/C2/C3/C4/C6 PASS, C8 4/4, QUALIFIED (cross-target)
XDEV:   completed, C1/C2/C3/C4/C6 PASS, C8 5/5, QUALIFIED (cross-target)

Unexpected behavior and last printed line: none; every log ends "Done."
Cold-boot retry result: (not recorded)
```

## Cross-check against `lspci` - PASS

[hardware-testing.md](../../hardware-testing.md),
"Cross-checking the PCI block against `lspci`", asks for this once per machine
whose PCI block has not been verified before. Done here and passing; it does not
repeat per run.

Linux on this same P14s, `sudo lspci -vv -s 00:14.0` in full - kept complete
rather than trimmed to the PM capability, because it cross-checks more of the
PCI block than PM alone:

```
00:14.0 USB controller: Intel Corporation Comet Lake PCH-LP USB 3.1 xHCI Host Controller (prog-if 30 [XHCI])
        Subsystem: Lenovo Device 22b1
        Control: I/O- Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+
        Status: Cap+ 66MHz- UDF- FastB2B+ ParErr- DEVSEL=medium >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
        Latency: 0
        Interrupt: pin A routed to IRQ 125
        IOMMU group: 4
        Region 0: Memory at e33a0000 (64-bit, non-prefetchable) [size=64K]
        Capabilities: [70] Power Management version 2
                Flags: PMEClk- DSI- D1- D2- AuxCurrent=375mA PME(D0-,D1-,D2-,D3hot+,D3cold+)
                Status: D0 NoSoftRst+ PME-Enable- DSel=0 DScale=0 PME-
        Capabilities: [80] MSI: Enable+ Count=8/8 Maskable- 64bit+
                Address: 00000000fee00318  Data: 0000
        Capabilities: [90] Vendor Specific Information: Intel <unknown>
        Kernel driver in use: xhci_hcd
        Kernel modules: xhci_pci
```

### What agrees

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
| `PCI subsys: 17AA:22B1` | `Subsystem: Lenovo Device 22b1` | match (17AA = Lenovo) |
| `BAR0: E33A0000 64-bit`, no `prefetchable` | `Memory at e33a0000 (64-bit, non-prefetchable)` | match |
| `PCI caps: PM=1 MSI=1 MSI-X=0 PCIe=0` | `[70]` PM and `[80]` MSI present, no MSI-X and no PCIe capability | match, including the two absences |
| PCI status block silent | `>TAbort- <TAbort- <MAbort- >SERR- <PERR- ParErr-` | match - no error bits set |

The interesting part is that these are the same two PM words the E460
reported (`PMC=C1C2 PMCSR=0008`), across a four-year gap in PCH generation
and a different Lenovo board, and Linux decodes them the same way on both.
The board-specific half is what was genuinely new here and it also agrees:
subsystem `17AA:22B1` and the BAR at `e33a0000`.

The fields that matter are the hardwired ones, which no OS can change: the
`PME_Support` mask, D1/D2 support, `DSI`, `PMEClk`, `Aux`, the subsystem IDs,
the BAR attributes, and which capabilities exist at all. The live PMCSR fields
(`state`, `PME_En`, `PME_Status`) agreeing as well is a bonus rather than
proof, since Linux had `xhci_hcd` bound by then and could legitimately have
moved the D-state.

### One capability the qualifier does not print

`lspci` lists a third capability this machine has and the E460 did not:

```
Capabilities: [90] Vendor Specific Information: Intel <unknown>
```

This is not a disagreement. The qualifier's `PCI caps:` line reports the
presence of four specific capability IDs it has a use for, not an exhaustive
list of the chain, and the walk demonstrably ran past `[90]`, since it
correctly reported `MSI-X=0 PCIe=0` for capabilities that are absent.

Not worth adding to the tool. The contents are undocumented (`lspci` itself
prints `<unknown>`), so the qualifier could only report that a vendor
capability exists, which changes no verdict and gives the driver nothing to
act on. If a Comet Lake-specific quirk ever turns up that is traced to this
structure, that is when it earns a line.

### What differs, and why none of it is a disagreement

Three values differ from what DOS saw, and they are one story rather than
three problems: Linux switched the controller to MSI.

| | DOS (qualifier) | Linux (`lspci`) |
|---|---|---|
| MSI enable | `MSI=1(en=0)` | `MSI: Enable+ Count=8/8` |
| INTx Disable | `INTxDis=0` (cmd `0006`) | `DisINTx+` |
| Interrupt routing | `line=IRQ 11` | `pin A routed to IRQ 125` |

Enabling MSI requires setting INTx Disable, and the resulting vector is 125
rather than a legacy PIC line. All three are writable state that a driver owns,
so they say nothing about the silicon. All three are also what the target
drivers must not do: neither Win98 nor Win2000 SP4 has an MSI path (NT gained
MSI in Vista), so the driver depends on MSI staying disabled and INTx staying
enabled, which is the state DOS observed.

`IRQ 125` is the same pin A that DOS reports as `IRQ 11`, routed through the
IOAPIC instead of the 8259: the concrete form of the
`Win2000 APIC-HAL routing remains untested by DOS C4` caveat in these logs. C4
proved the 8259 path here; the 125 path is the one a Win2000 ACPI
multiprocessor HAL would use and that DOS cannot exercise. (The E460 showed
the same thing at IRQ 123.)

Also present and not reported, for the reasons given in the E460 record:
`[size=64K]` (determining BAR size means writing the BAR, which breaks
the read-only probe contract), the MSI `Count`/`Maskable`/`64bit`/`Address`/
`Data` detail, and `Latency`/`DEVSEL`/`66MHz`/`FastB2B`/`UDF`/`IOMMU group`/
`Kernel driver`.

### Facts for Phase 8

Both PM facts the E460 record calls out hold here too, on silicon with no
quirk-table row at all:

- `NoSoftRst=1`: the controller retains internal state across a D3hot -> D0
  transition, so a resume path need not treat every D0 return as a cold
  controller.
- `PME_Support: D3hot, D3cold`: it can assert PME from both D3 states.
  `quirks.c` has no `8086:02ED` row and Linux has no `XHCI_PME_STUCK_QUIRK`
  entry for Comet Lake, so this is not a known-affected part; that the wake
  path exists is not evidence of a stuck latch.

Two controllers a generation apart now report identical PM capability content,
which is a reason to expect the Win2000 resume path to be uniform across recent
Intel PCH xHCI. It is still two data points, not a family guarantee. Confirm
per controller.

## Notes

- Only one host controller is present. `PROBE.LOG` scanned all three families
  (`xHCI EHCI OHCI`) and found a single xHCI function, confirming the fleet
  record that Comet Lake is xHCI-only with no EHCI fallback.
- `XDEV` used a real external device in the left-hand USB-A connector: a
  SanDisk U3 Titanium (`0781:5408`, USB 2.00 mass storage), which enumerated at
  High-Speed on controller port 4. That makes `XDEV` a genuine variation on
  `XEMPTY` rather than a near-duplicate, and it is the first physical
  connector -> controller port mapping recorded for this machine.
  - Port 4 is a USB2-only managed port, not one of the companion ports.
    Do not infer the mapping from the USB2-only/companion split: that split
    describes how a port pairs with the USB3 half of the same physical socket,
    not whether the socket is user-facing. The E460 behaves the same way; its
    left connector is port 3, also USB2-only.
  - The qualifier only sees the USB 2.0 half of that connector. A USB 3.0
    device in the same physical socket would land on one of ports 13-18, which
    the report marks `unmanaged` and the driver's USB2 scope does not cover.
    So "the left USB-A connector is port 4" holds for USB 2.0 devices
    specifically.
  - The other external connectors are still unmapped: managed USB2 ports 1, 2,
    3, 5, 7, 11 and 12 stayed empty through every stage and are the remaining
    candidates.
- The four other devices are internal and cannot be unplugged: `04F3:2D4A`
  ELAN Touchscreen (FS, port 6), `04F2:B6D9` Chicony Integrated Camera (HS,
  port 8), `06CB:00BD` Synaptics fingerprint reader (FS, vendor-specific,
  port 9), `8087:0026` Intel Bluetooth (FS, port 10). So the "keep external
  test devices disconnected" instruction on the earlier stages applies only to
  external ones, and `XEMPTY` still enumerates those four.
- C8 cleared the strict-PSI lookup on Comet Lake. The USB 2.0 protocol
  capability advertises `PSIC 3`, so C6/C8 took the advertised-table path
  rather than the default-ID fallback, the path QEMU cannot reach, since
  `qemu-xhci` reports `PSIC 0`. It identified 5 of 5 across Full-Speed and
  High-Speed, with no `PSIV n has no USB2 speed-class mapping` and no
  `PSIV n not advertised` line. That extends the E460's clearance of this path
  from Sunrise Point-LP to a second Intel generation. (The `PSIC 8` on this
  machine is on the USB 3.1 capability, covering the unmanaged ports 13-18,
  which C6/C8 never touch.)
- The PCI status block is silent in every log, which is the clean case: no
  bus-error bit was set before the run, and none turned on across the active
  tests (`lspci` agrees, in the table above).
- Differences against the E460 (`8086:9D2F`): HCIVERSION 1.10 vs 1.00, a
  USB 3.1 vs USB 3.0 supported-protocol capability (PSIC 8 vs 3 on the USB3
  half), the extra Intel vendor-specific PCI capability at `[90]`, and no
  quirk-table entry. Otherwise the geometry is identical: IRQ 11, 64 slots,
  8 interrupters, 18 ports (12 USB2 + 6 USB3), 34 scratchpad buffers, 32-byte
  contexts, AC64=1, PPC=0, BAR below 4 GB, and the PM capability content
  matches word for word.
- The logs were transcribed from the machine; trailing whitespace on the
  `Families:` line was not preserved. Everything else is verbatim.
