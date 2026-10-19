# Issue 17 - On Windows 98 SE and ME a PCI bridge a Thunderbolt tunnel brings up after the start shows Code 29: PCI.VXD never configures a hot-added bridge chain

Status: **fixed in source for `3.0.0.0` in `xhcicm.sys`, behind
`XhciCmBridgeNumbering` (on by default; design record 21 section 6.1.1),
on Windows 98 SE and ME only.** Read on the owner's ThinkPad P14s Gen 1
under Windows 98 SE on 2026-10-16 with the third of three prototypes:
an ASMedia ASM2464 NVMe enclosure plugged in after the start got its
drive letter (issue 18). The build as merged was read on the P14s by the
owner the same evening, a hot plug after a start and a re-plug after Safely
Remove, each to a drive letter (issue 18 section 8). Windows 2000 to 7 never had the defect:
`pci.sys` configures such a chain itself (section 3). What is still not
answered is a function behind the tunnel that Windows 98 can give no
IRQ at all (section 7).

Machines affected: any Thunderbolt 3 or USB4 host under Windows 98 SE or
ME, when the device plugged into it carries a PCI Express switch of its
own (a PCI-to-PCI bridge chain) and was not present when the BIOS ran.
Read on the P14s Gen 1 (Alpine Ridge LP, `8086:15BF`) with an ORICO
enclosure built on the ASM2464 (`1B21:2463`), and in the emulator with a
two-level bridge chain. A device whose function sits directly on the
tunnel port's secondary bus, with no bridge between, was never affected;
that is the emulator's own NVMe model, and the reason the earlier legs
passed.

## 1. Symptom

The enclosure, plugged in after Windows 98 SE had started, was approved
by the controller's firmware and its PCI Express tunnel came up. Device
Manager then showed the enclosure's switch upstream port, "PCI standard
PCI-to-PCI bridge" (`PCI\VEN_1B21&DEV_2463`), with Code 29
(`CM_PROB_HARDWARE_DISABLED`), not started and with an empty allocation
in `HKEY_DYN_DATA\Config Manager\Enum`. The downstream port and the NVMe
behind it never appeared. Device Manager's Refresh changed nothing; a
Remove of the devnode and a Refresh gave a fresh devnode, the wizard, a
restart request and Code 29 again. The same enclosure present at
power-on worked, and a Safely Remove, unplug and re-plug of that working
enclosure brought the Code 29 back. A restart always cured it.

## 2. Why

At POST the BIOS numbers every bridge it finds and gives each a memory
window, and Windows 98's PCI bus driver, `PCI.VXD`, takes what it finds.
A bridge that arrives later comes out of reset with secondary and
subordinate bus 0, so nothing behind it can be reached, and its windows
closed. PCI.VXD does not set such a chain up usefully:

- **In the emulator** (98 SE, an ACPI-mode image, a fresh two-level
  bridge chain hot-added behind the tunnel bridge, read before any
  restart) it gave the first bridge secondary = subordinate, leaving no
  bus for the next level, and asked for a restart; a second pass widened
  it and numbered the second bridge, but put the first bridge's window
  outside the tunnel bridge's window, and asked for a restart again; the
  NVMe got no BAR.
- **On the P14s** (APM mode) the outcome is the Code 29 of section 1.

Bus numbers and windows are not the whole of it. A function that PCI.VXD
can give no IRQ is also disabled with Code 29 before its driver loads,
and on the P14s that is what the NVMe showed once the bridges were
started (issue 18 section 5): the interrupt line had to be set up too.

## 3. Windows 2000 to 7

`pci.sys` numbers, nests and starts a hot-added bridge chain itself, with
no restart. That was read on 2000 and XP SP3 with a two-bridge chain
behind the emulator's tunnel port, first plugs and re-plugs alike
(`runs/run-41.md`, "Bridges behind the tunnel port on NT"), so the NT
build carries none of this issue's code and never reads
`XhciCmBridgeNumbering`. What bounds a chain on NT is the bus range and
memory window the BIOS reserved for the tunnel port: whether 2000 to 7
can enlarge that reservation is not read, and a chain that does not fit
is left at Code 12 (no resources), as the emulator showed with a window
too small for it
(`runs/run-41.md`'s traps; `runs/run-40.md`, U.11 on the `asm4242`
stand-in). Whether such a unit can be stopped before an unplug is a
separate question with its own fallback: a unit Windows does not mark
removable is stopped by query-and-remove, from XHCITR's "Stop as
administrator" where the user has no administrator rights (design record
24 revision 5, design record 25 revision 11).

## 4. The fix

Before each `Reenumerate_DevNode` of a tunnel port (design record 21
section 6.1), the 9x CONFIGMG worker of `xhcicm.sys` sets up the bridges
below it as the BIOS would have at POST (`src/cm/tbt_brnum.c`, called
from `src/cm/cm_cfgwk.c`). It does so only while the request is still
current (no newer one for the port, the controller still available) and
the port's devnode has no child in CONFIGMG's tree, so a subtree PCI.VXD
still holds is never renumbered under it; and only when every bridge on
the port's secondary bus reads secondary and subordinate 0. A chain the
BIOS numbered, or an endpoint directly behind the port, is left alone.

1. **Bus numbers**, depth first inside the tunnel port's own secondary
   to subordinate range: each bridge gets primary = its bus, secondary =
   the next free bus, and a subordinate narrowed to the last bus of its
   subtree once that subtree is walked.
2. **Memory windows**, sized from the memory BARs behind the chain and
   placed from the top of the port's own memory window downward, the
   largest alignment first, each subtree inside its bridge's window; each
   bridge's I/O and prefetchable windows closed and its Memory Space and
   Bus Master Enable set. A port with no window, or one too small,
   gets the bus numbers alone.
3. **Interrupt lines.** A function's interrupt is provably routed only
   where it travels a wire whose IRQ is already known, and the tunnel
   port's own Interrupt Line is known (the BIOS wrote it, and PCI.VXD
   holds it). A function below the port whose Interrupt Pin, swizzled up
   through the bridges between ((pin - 1 + its device + the bridges'
   device numbers) mod 4), arrives on the port's own pin shares the
   port's wire, and gets the port's line where its own reads 0 or `FFh`.
   Any other function gets nothing.

Every access goes through PCI.VXD's own `_PCI_Read_Config` and
`_PCI_Write_Config`, reached through the VMM dynalink like the CONFIGMG
services, so no import is added; Windows 98's `HalGetBusData` does not
answer for a function PCI.VXD does not know yet. A read guard comes
first: PCI.VXD's reading of the tunnel port's identity must equal
`HalGetBusData`'s, or the numbering is off for the worker's life. Each
write is preceded by a read that the function is still the one
journalled and read back after; a refusal part-way puts back what was
written. A chain whose switch is still training is read again every
100 ms, twenty times at most. Record 21 section 6.1.1 has the bounds
and every refusal; the host suite is `test\test_tbt_brnum.c`.

A chain already numbered whose every bridge window is closed - how
Windows' stop leaves it after a Safely Remove, given back that way by a
re-plug that did not reset it - has its windows and lines set up again,
its bus numbers left as found; a chain with any window open is left
alone (record 21 section 6.1.1, "A stopped chain given back").

The USB4 software connection manager's profile reaches the same worker
through the shared re-enumeration seam, so it shares this code path.
It was not read with a bridge chain under the USB4 profile
(`runs/run-41.md`, "Bridges behind the tunnel port on 98 SE and ME").

## 5. The switch and the diagnostic

`XhciCmBridgeNumbering`, a `REG_DWORD` in the Thunderbolt or USB4
controller's driver key: 1 by default, 0 to restore the old behaviour (a
hot-added bridge left as it came up: Code 29 until a restart); any other
value is refused and 1 stands. It is read on Windows 98 SE and ME only,
it is set in the registry only, and the install does not write it or
ask for it (`docs/using/tuning.md`; the release notes' "Thunderbolt 3
and USB4: registry settings").

On every attempt the worker records what it did, and the driver writes
it as `XhciCmBridgeDiag`, a `REG_BINARY` of twenty dwords under the
NHI's own key in `HKLM\Enum\PCI`, beside `XhciCmStatus`, in every build
flavour, so a release build's outcome can be read from a registry
export (layout in `src/cm/cm_statb.h`):

| Dword | Holds |
|---|---|
| 0 | the attempt's sequence number |
| 1 | the tunnel port (bus 15:8, device and function 7:0) |
| 2 | the bus numbers' outcome (1: the chain numbered) |
| 3 | functions on the port's secondary bus (23:16), bridges met (15:8), tries (7:0) |
| 4 | the port's bus-number register (18h) |
| 5 | the windows' outcome (1: every window written) |
| 6 | the port's memory window (20h) |
| 7 | the memory the chain needs |
| 8 | where a refusal stopped (`FFFFFFFF`: none) |
| 9 | bridges written back (15:0) and those that could not be (31:16) |
| 10, 11 | the first two bridges |
| 12, 13 | their bus-number registers as left |
| 14, 15 | their memory windows as written |
| 16 | flags: 1 held by a child devnode, 2 PCI.VXD's guard refused, 4 the port read link-up |
| 17 | what the guard read |
| 18 | the interrupt lines: outcome (7:0; 1 every eligible line written, 2 the port has no line), the port's line (15:8), its pin (23:16) |
| 19 | functions below with a pin (7:0), given the port's line (15:8), already set (23:16), arriving on another pin (31:24) |

## 6. What was read

- **The emulator**, 98 SE, with the bus numbers alone: PCI.VXD found the
  second bridge and the NVMe behind it, which it never had, but gave the
  first bridge a window outside the tunnel bridge's, so nothing behind it
  decoded. With a window already in place, PCI.VXD takes it as the
  bridge's boot configuration, as it does the BIOS's. That is why the
  windows are written too.
- **The P14s**, 98 SE, 2026-10-16, three prototypes, each a replaced
  `xhcicm.sys` and a hot plug after a start without the enclosure (issue
  18 section 5): bus numbers alone, still Code 29 with the windows
  closed; with the windows, both bridges started and the NVMe appeared
  at Code 29 for want of an IRQ; with the interrupt lines, the drive
  letter, Stor98 on IRQ 10 with no problem code, and `XhciCmBridgeDiag`
  reading every step done.

On Windows ME, a hot-added two-bridge chain in the emulator: bus numbers
set, both windows inside the tunnel bridge's, the NVMe behind found by
PCI.VXD, and an interrupt line on the one function of four whose pin
reaches the port's own, the other three left alone (`runs/run-41.md`,
the same section). Not read on ME: Stor98 on that chain.

## 7. What is not answered

- **A function with no provable IRQ.** On a machine whose routing gives
  the tunnel port no line of its own, or for a function whose pin
  arrives on another of the port's pins, nothing is written, and PCI.VXD
  leaves the function at Code 29 before its driver loads. A function
  with no interrupt pin at all (MSI only, which Windows 98 cannot use)
  is in the same place. That is roadmap-usb4 tasks 42-0.6 (design record
  28, the design) and 42-A.15 (its code), which replaced the planned item
  NOIRQ.1 (`docs/contributing/roadmap-usb4.md`, Phase 42), paired with Stor98's
  planned polling mode; until then such a device needs to be attached at
  power-on with pre-boot support on.
- I/O and prefetchable windows, a CardBus bridge, a chain the BIOS left
  half-numbered, BARs above 1 GiB, and a port whose bus range or window
  is too small for the chain: the BIOS chose those, and the way round is
  the device attached at power-on with pre-boot support on.

## 8. Rules kept

- **What the BIOS does at POST, nobody does at a hot plug on 98.** A
  device that works when present at power-on and fails when plugged in
  later points at the set-up POST gave it, not at the device or the
  driver.
- **An emulator leg proves the topology it had.** The earlier NVMe legs
  had no bridge between the tunnel port and the NVMe, so they said
  nothing about a bridge chain, on 98 or on NT; the NT claim was read
  again with a chain (section 3).
- **Set only what is provable.** The interrupt line is written only where
  the swizzle puts the function on the port's own wire; a guess at any
  other routing is left to tasks 42-0.6 and 42-A.15.
- **A release build reports in the registry.** A field machine has no
  debugger; twenty dwords in an export were enough to read each
  prototype.

## Sources

- Design record 21 sections 6.1, 6.1.1 and 8 (`docs/contributing/design/21-thunderbolt3-icm-client.md`);
  `src/cm/tbt_brnum.c` and `.h`, `src/cm/cm_cfgwk.c`, `src/cm/cm_cfgmg.c`,
  `src/cm/cm_statb.h`, `test/test_tbt_brnum.c`.
- `docs/contributing/legal-provenance.md` section 4, "PCI.VXD's
  configuration-space services".
- `docs/contributing/runs/run-41.md`, "Bridges behind the tunnel port on
  NT"; `docs/contributing/runs/run-40.md`, 40-V.2 U.11.
- Design record 24 revision 5 and design record 25 revision 11 (the
  stop of a unit not marked removable on NT).
- `docs/contributing/roadmap-usb4.md`, tasks 42-0.6 and 42-A.15 (was NOIRQ.1).
- Issue 18, the P14s reading, and its sources.
