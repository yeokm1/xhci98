# Issue 18 - The ThinkPad P14s Gen 1 under Windows 98 SE: a Thunderbolt 3 NVMe enclosure plugged in after the start, from Code 29 to a drive letter in three prototypes

Status: **working on the third prototype, read by the owner on
2026-10-16**: the enclosure plugged in after a start without it got its
drive letter, with Stor98 on IRQ 10 and no problem code. The fix is
issue 17's, `XhciCmBridgeNumbering` in `xhcicm.sys` for `3.0.0.0`
(design record 21 section 6.1.1). The build as merged, which differs from
the third prototype (section 8), has not yet been read on the P14s.

Machine: the owner's Lenovo ThinkPad P14s Gen 1 (Intel), Windows 98 SE.
Device: an ORICO NVMe enclosure built on the ASMedia ASM2464, holding an
SK hynix HFS256GD4, attached by Thunderbolt 3.

## 1. The machine and the device

| | Where | Id | Notes |
|---|---|---|---|
| Thunderbolt 3 host interface (NHI) | | `8086:15BF` | Alpine Ridge LP (JHL6240), driven by `xhcicm.sys` |
| Tunnel root port | `04:01.0` | `8086:15C0` | bus range from the BIOS `06`-`2A`, memory window `D4100000`-`E00FFFFF` |
| Alpine Ridge's xHCI | | `8086:15C1` | driven by `xhci98.sys` |
| Enclosure switch, upstream port | `06:00.0` | `1B21:2463` | ASM2464, a PCI-to-PCI bridge |
| Enclosure switch, downstream port | `07:00.0` | `1B21:2463` | ASM2464, a PCI-to-PCI bridge |
| NVMe | bus `08` | `1C5C:174A` | SK hynix HFS256GD4, driven by Stor98 |

The firmware connection manager reported firmware version `0x60` and the
device as "ASMedia 246x", Thunderbolt 3, 20 Gbit/s per lane, x2.

**The BIOS settings that matter:**

- **Thunderbolt BIOS Assist Mode: Off.** With it On, Windows 98 SE
  showed no Thunderbolt controller at all, with or without the
  enclosure, and a plug brought up nothing.
- **Support in Pre Boot Environment: On.**
- **Security level 1.**

## 2. What the owner reported

In order, as reported:

1. A UAS enclosure mounted.
2. Behind Thunderbolt, the drive was absent, as if unplugged.
3. No drive letter until the NVMe driver, Stor98, was installed.
4. With the enclosure working, a Safely Remove, an unplug and a re-plug
   gave Code 29.

Items 1 to 3 have no capture here. Item 4 is series b below, and the
rest of this page is item 4.

## 3. The captures

Five series, a to e, each an `XHCITR` report, an export of
`HKLM\Enum\PCI`, an export of `HKEY_DYN_DATA\Config Manager\Enum` and
Device Manager's printed System Resource Report:

| Series | Time | State | What showed |
|---|---|---|---|
| a | 12:06 | the enclosure present at power-on | both ASM2464 bridges and Stor98 started, the NVMe as `D:`; Stor98 on IRQ 10, memory `D4100000`-`D4105FFF`, at the bottom of the port's window |
| b | 12:09 | after Safely Remove, unplug and re-plug, before any prototype | the upstream port at Code 29, status `180066A4`, an empty allocation; nothing below it |
| c | 15:17 | prototype 1, a hot plug after a start without the enclosure | the upstream port at Code 29 again; no `XhciCmBridgeDiag` value |
| d | 16:37 | prototype 2 | both bridges started, window `E0000000`-`E00FFFFF`; Stor98 bound and at Code 29, with no IRQ |
| e | 17:20 | prototype 3 | `D:` present; Stor98 on IRQ 10, problem code 0, memory `E0000000`-`E0005FFF` |

The tunnel port kept the same resources throughout. In series b to e the
firmware approved the device and the PCI Express tunnel was up: the
Thunderbolt half worked every time, and the failure was on the PCI side.

## 4. Why

Issue 17 has the mechanism. At POST the BIOS numbered the enclosure's
switch (upstream port `06/07/08`, downstream port `07/08/08`, the NVMe on
bus 8), gave it windows and routed its interrupts, so series a worked. A
switch that arrives after the start comes out of reset with no bus
numbers and its windows closed, and Windows 98's `PCI.VXD` does not set
it up: Code 29.

## 5. Three prototypes

Each was a replaced `xhcicm.sys`, read by the owner with a hot plug
after a start without the enclosure.

1. **Bus numbers only.** PCI.VXD re-read both ASMedia bridges, but the
   upstream port stayed at Code 29, its windows closed and no IRQ
   (series c).
2. **Bus numbers and memory windows**, placed from the top of the port's
   window. Both bridges started with `E0000000`-`E00FFFFF`, inside the
   port's `D4100000`-`E00FFFFF`, and the NVMe appeared, but at Code 29:
   PCI.VXD had no IRQ to give it (series d). This prototype's
   `XhciCmBridgeDiag` was eighteen dwords, without the interrupt words.
3. **And the Interrupt Line**, the port's own line written to each
   function below whose pin, swizzled up through the bridges, arrives on
   the port's pin. It worked (series e).

## 6. The working reading

Series e, decoded against `src/cm/cm_statb.h` (issue 17 section 5 has the
layout). `XhciCmBridgeDiag` read: one attempt on the first try, on the
port `04:01.0`; bus numbers done (two bridges below, one function on the
port's secondary bus); the port's bus register `06`-`2A`; windows done,
1 MiB needed, the port's window `D4100000`-`E00FFFFF`; no refusal and
nothing undone; the bridges at `06:00.0` (`06/07/08`) and `07:00.0`
(`07/08/08`), each with the window `E0000000`-`E00FFFFF`; the port read
link-up; PCI.VXD's guard read the port's own id, `8086:15C0`. Then:

- **Word 18 = `0x00010A01`**: the interrupt lines' outcome 1 (every
  eligible line written), the port's line `0Ah` (IRQ 10), its pin 1
  (INTA).
- **Word 19 = `0x00000303`**: three functions below the port with an
  interrupt pin, all three given the port's line; none already set, none
  arriving on another pin.

The resource report agrees. IRQ 10 holds Stor98 and two more PCI-to-PCI
bridges than in series d, and the IRQ assignments of the tunnel's
functions are the ones series a shows for the same enclosure present at
power-on. The difference from series a is the memory: the driver places
the chain at the top of the port's window, where the BIOS had put it at
the bottom.

## 7. Wrong turns

- **"Windows 2000 to 7 handle the bridges."** It was first said from
  emulator NVMe legs whose NVMe sat directly on the tunnel port's
  secondary bus, with no bridge between, so they could not show it either
  way. It held, but only once read with a two-bridge chain on 2000 and XP
  (`runs/run-41.md`, "Bridges behind the tunnel port on NT"; issue 17
  section 3).
- **An emulator reading of the Code 29 taken after a restart** was
  withdrawn. The emulator reading design record 21 section 6.1.1 rests on
  was taken before any restart.

## 8. The recheck of the merged build

The build as merged differs from the third prototype: it places the
windows largest alignment first, undoes a superseded attempt's bus
numbers, and checks every value it puts back. The owner read it on the
P14s the same evening (`b9c18c41`, 3.0.0.0 merged in), with the release
`xhcicm.sys` alone replaced:

- **Series f, a hot plug after a start without the enclosure.** D:
  present, XHCITR showing the SK hynix drive behind the Thunderbolt port
  with every problem code 0; Stor98 on IRQ 10 with its memory at
  `E0000000-E0005FFF`, both bridges at `E0000000-E00FFFFF`, and
  `XhciCmBridgeDiag` the same as the third prototype's, words 18 and 19
  `00010A01` and `00000303`. The largest-alignment-first order places
  this chain as the bus order did: both windows are 1 MiB aligned.
- **Series g, the re-plug after Safely Remove** - series b's Code 29
  case: the unit stopped from XHCITR, unplugged and plugged in again.
  D: back, no device in error, the same resources and the same
  diagnostic.

A last change after these readings covers two cases the P14s chain does
not reach (a link behind a newly numbered bridge still training, and a
sibling BAR beside windows aligned above 1 MiB); the release carries it.

## 9. Rules kept

- **Ask for the registry, not a debugger.** A release-build value in the
  NHI's key told each prototype's outcome from a `.reg` export.
- **Present at power-on and plugged in later are two different tests.**
  Series a passing said nothing about series b.
- **One layer at a time.** Each prototype added one register class and
  each capture named the next missing one: bus numbers, then windows,
  then the IRQ.

## Sources

- `temp\p14-resource\` (git-ignored, the owner's captures, 2026-10-16):
  for each series a to e, `xhcitr-report-<s>.txt`, `enumpci_<s>` or
  `enumpci_<s>.reg`, `dyncm_<s>.reg` and `<s>.prn`. Not tracked, and not
  quoted beyond the values above.
- The owner's reports of 2026-10-16 (section 2).
- Issue 17; design record 21 sections 1.1 and 6.1.1; `src/cm/cm_statb.h`,
  `src/cm/tbt_brnum.h`.
- `docs/contributing/runs/run-41.md`, "Bridges behind the tunnel port on
  NT".
