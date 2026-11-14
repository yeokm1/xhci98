# Issue 27 - On HP EliteBook 7-series laptops the firmware gives the USB 3 controller no interrupt route: Code 12 under Windows XP and XP x64, STOP 0xA5 under Windows 2000

Status: **fixed in `3.4.2.0`** for Windows XP and XP x64 (roadmap-usb4
47.33) and for Windows 2000's stop on the listed boards (47.40); design
record 18 revisions 14 to 19, sections 16 to 16.4.
- **XP and XP x64:** the controller starts. On the listed HP boards it
  gets the interrupt line the chipset routes it to; anywhere no line can
  be given it is polled. Read on the owner's HP EliteBook 8470p under XP
  x64 (2026-11-07) and in the emulator under XP x64 and 32-bit XP; **not
  read on the reporter's machine**, and 32-bit XP has not run on the 8470p.
- **The blue ports** reach the USB 3 controller under XP only with
  `XhciIntelPortSwitch` 1, which stays 0 by default (section 6.4).
- **Windows 2000:** under an ACPI computer type it stopped with STOP
  0x000000A5 on this firmware once the controller had a driver (an
  emulator finding; Windows 2000 has not run on these machines). Fixed in
  `3.4.2.0` (record 18 section 16.4, option C; merge `303726e3`): on the
  listed boards Windows 2000 is asked for no interrupt for the
  controller, which starts polled (section 6.6). Read in the emulator in
  the 8470p's state; not yet read: a USB stick on the stripped list, and
  disable and enable, remove and rescan, a rebalance and a standby under
  it; nor on the 8470p (section 9).
  On a board whose firmware turns the controller's decodes on, that
  fix cannot help: install Windows 2000 there as "Standard PC" (F7 in
  text-mode Setup).

**Scope.** Intel 7-series (Panther Point) xHCI `8086:1E31` on HP
EliteBooks of 2012 whose firmware leaves the controller's memory decode
off at boot and whose ACPI `_PRT` has no entry for device 20 (the xHCI at
`00:14.0`). Every HP firmware family read lacks the entry (section 3):
68ICF and 68ICE (8470p, 8470w, 8570p; the ProBook 6470b and 6570b), 68IAV
(8570w, 8770w), 68ISB (2570p) and 68IMT (2170p); every public Linux log
of these models that carries the PCI enable line shows the decode off too
(none was found for the 2170p). It touches only the NT kernels below 6.0, Windows 2000, XP and XP x64 (and Server 2003 x64, the same
kernel), on the line interrupt. Windows 7 x64 (`xhci98.sys` on MSI since
`2.3.0.0`), Windows 10 (Microsoft's driver) and current Linux use MSI
and never need the missing route; Windows 98 SE and ME were not read on these
machines. Reported by a user on one of these EliteBooks under XP x64 on
2026-10-12 (it was Phase 37's task 37.4 until that phase was cut), and
reproduced by the owner on an HP EliteBook 8470p (BIOS 68ICF F.74, 8 GB)
under XP x64 with `3.4.1.0`.

## 1. What was seen

Under XP x64 Device Manager showed the xHCI98 USB 3 Controller with
**Code 12**, "This device cannot find enough free resources that it can
use", and its Resources tab named no conflicting device. Windows 7 x64 on
`xhci98.sys` and Windows 10 on Microsoft's driver started the same
controller on the same machine. The 8470p's BIOS setup has no USB 3.0
mode setting to change.

## 2. The first guess, and what the readings showed instead

The first guess was a 64-bit BAR the firmware had placed above 4 GB,
which XP cannot reach. The owner's readings put it out:

- **QUALREP** (`XHCIQUAL 3.4.1.0` under MS-DOS 6.22, before Windows): the
  xHCI's PCI Command register `0000` (memory decode and bus mastering off;
  both EHCIs `0006`); BAR0 `D4720000`, 64-bit, high half 0, so below
  4 GB and assigned; Interrupt Line 10, pin INTA; MSI capable and off. The
  PCH routes INTA to PIRQA (`D20IR` `3210`), which is GSI 16 in APIC
  mode. The `$PIR` table's 24 entries name no device `0x14`.
- **The MADT** (saved by XHCITR `/acpi` under XP x64): one I/O APIC at
  GSI base 0; overrides for IRQ 0 (to GSI 2) and IRQ 9 (level, active
  high) only. XP x64's firmware-table interface gave no DSDT, so the
  `_PRT` was not in that save (section 8).
- **A public Linux boot log of an 8470p** (BIOS F.65): `pci 0000:00:14.0:
  enabling device (0000 -> 0002)` (the same decode off), then `can't
  derive routing for PCI INT A` and `PCI INT A: no GSI`: no `_PRT` entry
  for device 20. Linux's xHCI driver runs on MSI there, so it works. (Not
  every Linux did: 2012-13 reports of an 8470p and a ProBook 6570b in UEFI
  mode, on kernels before Linux took MSI first, failed with "request
  interrupt 255 failed".) The root bus windows leave room for a 64 KiB
  BAR below 4 GB.

So the controller's resources were not out of reach; it had no
interrupt that Windows could be told about.

## 3. The firmware tables across HP's 2012 range

Public Linux logs of the 2570p, 8470w, 8570p, 8570w and 8770w show the
same two lines on `00:14.0`, "enabling device (0000 -> 0002)" and "no
GSI", across BIOS versions F.32 to F.73; a ProBook 6570b's shows "no GSI"
(its enable line not quoted). The 2170p had no public log found.

The tables themselves were then read. The owner's 8470p tables (F.74,
saved under Linux), and from HP's latest BIOS image of each family
(SoftPaqs SP96091 for 68ICF/68ICE F.74, SP96092 for 68IAV F.71, SP96085
for 68ISB F.73, SP96088 for 68IMT F.72) the ACPI tables alone: located by
their signatures and checksums, decoded with ACPICA's `iasl`, and read as
data; no firmware code module was read, no HP program was run, and the
images were deleted afterwards. In every one:

- `\_SB.PCI0._PRT` has **no entry for `0x0014FFFF`**, in the APIC table
  or the PIC table, and no SSDT carries a `_PRT`. The device `XHC` itself
  is there, at `_ADR` `0x00140000`.
- Fourteen routing entries name GSI 16 (PIRQA) on the 8470p's tables, nine
  on the root bus (both EHCIs among them) and five behind the root ports;
  thirteen on 68IAV, which has no entry for the integrated graphics.
- The ports are routed to the xHCI (`XUSB2PR`, `USB3_PSSEN`, by the
  method `XSEL`, and only in the firmware's USB 3 setup states 2 and 3)
  first from `PCI0._OSC`, which calls it only for an OS that answers
  `_OSI("Windows 2012")` or later, that is Windows 8 and later, or for a
  driver that calls `_OSC` with the xHCI's own UUID; after that, the wake
  path from S3 and S4 repeats it. XP and 2000 take neither route to the
  first call [inf, from the tables]. Whether the firmware routes them
  before boot cannot be read from the tables.
- The downloaded 68ICF image's DSDT matched the owner's runtime copy in
  every object read here (`_PRT`, `XHC`, the EHCIs, `_OSC`, the links);
  only the header and some NVS and I/O addresses differed.

## 4. The reproduction and the cause

The emulator (QEMU's q35 machine, `qemu-xhci` at `00:14.0`, XP x64 SP2 with
`3.4.1.0` installed) was given the 8470p's state while SeaBIOS waited at
its boot menu, after its PCI set-up and before the OS loader: the xHCI's
Command register cleared through ports `CF8`/`CFC` from QEMU's monitor,
and the DSDT's eight `_PRT` entries for device 20 renamed in guest memory
to a device that does not exist, the checksum fixed.

| Leg | Decode | `_PRT` entry | Code | What was allocated |
|---|---|---|---|---|
| control | on | kept | 0 | memory at the firmware's address, GSI 20 |
| a1 | off | kept | 0 | no boot configuration; memory placed afresh, GSI 20 |
| a2 | on | removed | 0 | memory, IRQ 10 (the boot configuration's) |
| a3 | off | removed | **12** | none |
| a4 | off | removed, `/PCILOCK` | **12** | none |

32-bit XP SP3 in a3's state gave Code 12 too.

**The cause, as reproduced:** XP's PCI driver gave the decode-disabled
controller no boot configuration (a1), so its memory was placed afresh
without trouble, but its line could then come only from the firmware's
`_PRT`. With the entry missing, the boot configuration's IRQ (the
Interrupt Line register) still served (a2). With both, no line could be
given, the controller's one configuration (memory and an interrupt)
failed whole, and Windows reported Code 12 with no conflict to name.
`/PCILOCK` does not help. Not excluded by these legs: an HP-specific
memory reservation, and the reporter's own firmware (its family's tables
were read, the machine never).

## 5. The wrong turns

- **The BAR above 4 GB** (section 2): the leading guess of the brief, out
  at the first register read.
- **"Not done: routing the line."** The first fix (section 6.1) left the
  controller polled and set the chipset's route aside: an interrupt at a
  GSI no `_PRT` names would need the ACPI arbiter to accept it, shared with
  fourteen other routing entries. A review of that option and the owner's
  decision of 2026-11-06 brought it back as a guarded offer behind a
  switch, with polling kept as the base (section 6.2).
- **XP x64 only, then every NT 5.** The offer was first made only on 64-bit
  XP, because a 32-bit system may run a HAL with no I/O APIC, where PIRQA
  to H are not inputs 16 to 23. Record 18 section 16.2 worked through each
  32-bit HAL and found every wrong number ends in a polled start under the
  default mode; the MPS HALs, where the number is bus-relative, are left
  unresolved.
- **Sharing in the emulator.** A UHCI placed where QEMU routes it to the
  same GSI as the xHCI made XP refuse the offered GSI beside it (the xHCI
  took the memory-only configuration and polled). QEMU names that GSI
  through link devices; the 8470p names its sharers directly. Whether XP
  shares an offered GSI there was for the owner's bench, and it did
  (section 6.3).
- **Windows 2000 was the one leg that stopped.** Only when the offer was
  extended to 32-bit 2000 did its legs run on this firmware state, and
  under either ACPI HAL Windows 2000 stopped where XP gives Code 12
  (section 6.5). Its fix (section 6.6) first failed the filter IRP where
  no interrupt-free list could be made; a leg showed the PnP manager then
  went on with the PCI driver's list and stopped, so the filter now
  withdraws the list instead.
- **A strip only at the offer.** As first built, the strip applied only
  where the chipset read reached the offer, so a listed board whose read
  failed later would have kept its interrupt and stopped. The gate was
  widened at the merge (section 6.6).

## 6. The fix

### 6.1 A memory-only configuration: polled instead of Code 12

`38b36c2b` (design record 18 revision 14, section 16). On Windows 2000,
XP and XP x64 (an NT kernel below 6.0; Windows 98 and ME pass the IRP down
untouched, Vista and 7 unchanged), `xhci98.sys` answers
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS`: for the shape the PCI driver gives
(one alternative list, no message interrupt), a second list is appended,
the first with its interrupt removed. The list with the line stays first
and is taken wherever a line can be given. Where it cannot, the start gets
the memory alone and takes polled delivery, record 15's mode 0 path.
`XhciInterruptMode` 1 (the line only) and 3 ask for no copy. XHCITR names
the filter outcome (13).

Read in the emulator in a3's state: 32-bit XP SP3 and XP x64 SP2, Code 12
on `3.4.1.0`, Code 0 with the fix, the memory alone granted, "delivery in
effect POLLING", "no interrupt resource was granted", a USB stick read;
under mode 1, Code 12 as designed; in the control state the PCI driver's
own configuration and line still granted.

### 6.2 The chipset-routed GSI offer (`XhciIntelNoRouteIrq`)

`8c06e6e1` (revision 15, section 16.1), then `b05b9353` (revision 16,
section 16.2). The PCH does route the xHCI's INTA: `D20IP` names the pin
and `D20IR` the PIRQ, A to H, which in APIC mode are I/O APIC inputs 16 to
23. Once per controller, in AddDevice, read-only, the driver proves the
PDO's location is `00:14.0` and that the header there carries the
controller's own identity, checks a reviewed xHCI and a listed LPC, maps
one page of the chipset's RCBA space for `D20IP` and `D20IR`, and unmaps
it: the GSI is 16 + the PIRQ. Nothing is written. The filter's list is
then the PCI driver's configuration unchanged, a clone of it with its
interrupt run replaced by that GSI (only for a shared, level-sensitive
line), and the memory-only copy. A route the firmware can give still wins,
being first. Under the default `XhciInterruptMode` 0: if the GSI is
refused, the memory-only copy is taken; if it is granted and cannot be
connected, the start is polled; if it is granted and silent, the
dead-line watcher of record 15 moves it to polling. Under mode 1 there is
no memory-only copy, so a refusal leaves Code 12 and a silent line is
kept, as that mode asks; and what an MPS HAL makes of the offered number
is not read (section 9).

`XhciIntelNoRouteIrq` (written by the INFs at 1, no-overwrite): 0 never
offers; 1 offers on an allowlist of HP boards; 2 on any reviewed Intel
xHCI and LPC pair of the `XhciIntelIrqRoute` table. The list shipped empty
at first, so value 1 offered nothing until the bench passed.

Read in the emulator, a value standing in for the chipset read: on XP x64
granted and delivering, refused (polled), connection refused (polled),
granted and silent (moved to polling), mode 1 refused (Code 12, as asked)
and the sharing leg above; on 32-bit XP under the ACPI Multiprocessor PC
HAL the same five, and a stick read; on 2000 under the Standard PC HAL the
PCI driver's own line won, as designed.

### 6.3 The owner's bench, and the HP list in force

On 2026-11-07 the owner read the merged build (`d3a596b7`) on the 8470p
under XP x64 with `XhciIntelNoRouteIrq` 2: no Code 12; the chipset read
(`noroute.lpc` `1E558086`, `noroute.d20ir` `3210`), the offered GSI 16
granted (`noroute.grant` 1) and delivering on the line; a SuperSpeed stick
enumerated; a load with both EHCIs and the xHCI in use; a cold boot and a
warm restart with the stick plugged; disable and enable. All passed. The
readings are held on the maintainer's disk.

The owner then put value 1's list in force (`04dbd018`, revision 17,
section 16.3; merged `39f0317b`): Intel `1E31` with HP subsystems `179B`
(8470p, 8470w), `17A7` and `17AB` (8570p), `17DF` (2570p), `176C` (8770w)
and `176B` (8570w), every guard kept. `17AB` is also the ProBook 6570b's
board id, which the list cannot tell apart; the 2170p (`1815`) and the
ProBook 6470b are not on it. The list serves the 32-bit and the 64-bit
package alike, so the default value now offers the line on these boards
with nothing to set.

### 6.4 The ports: `XhciIntelPortSwitch` 1

On the bench the blue ports reached the xHCI only with
`XhciIntelPortSwitch` 1: the firmware routes the ports to it only from
`_OSC` for Windows 8 and later (section 3), so under XP they stay on the
EHCIs. The default stays 0 (owner, 2026-11-07); `docs/using/tuning.md`
tells these computers' users to set it. Superseded by roadmap-usb4 task
49.47 (owner, 2026-11-14): the INF default stays 0, and from `3.5.0.0`
Setup writes 1 at a first install on a listed board. A Settings window in XHCITR that
shows and sets the controller's values, this one among them, came from
this bench as task 47.35 (in progress at this writing).

### 6.5 Windows 2000: STOP 0xA5 under an ACPI HAL

The 32-bit legs of revision 16 read Windows 2000 SP4 in the 8470p's
state under three HALs:

| HAL | `3.4.1.0` or the fix | Result |
|---|---|---|
| Standard PC | either | Code 0, the PCI driver's own line (IRQ 10, the Interrupt Line register's) |
| ACPI PC (the 8259) | either | **STOP 0x000000A5 (0x00010003, ..., 0x00000014, ...)** at start |
| ACPI Uniprocessor PC (the I/O APIC) | either | the same STOP |
| ACPI PC, no driver for the controller | - | starts |
| ACPI PC, `_PRT` entry removed, decodes on | `3.4.1.0` | the same STOP |

Parameter 1 `0x10003` is, in Microsoft's ACPI_BIOS_ERROR reference, "ACPI
could not find a mapping in the `_PRT` package for a device", parameter 3
the device: 20. With the fix the filter had already run (outcome 13) when
the stop came, so the stop is in the arbitration of the filtered list,
before the memory-only alternative could serve. Windows 2000's ACPI treats
the missing route as fatal where XP's refuses the interrupt and gives
Code 12.

Record 18 revision 18 (section 16.4) designs the
fix. A diagnostic build whose filter left the memory-only copy as the
list's only alternative showed the idea holds with the decodes off: no
stop, Code 0, polled, under both ACPI HALs. With the decodes on the stop
came before the filter ran, so no filter can avoid it there. The design:
on Windows 2000 alone, on value 1's listed boards (the list check holds at
value 2 too), and unless `XhciIntelNoRouteIrq` is 0, the filter replaces
the list with the memory-only copy, so no configuration carries an
interrupt, under every `XhciInterruptMode` (modes 0 and 2 then poll;
modes 1 and 3 refuse the start, Code 10, in place of the stop). Under the
Standard PC HAL the same board is then polled rather than given its line
(value 0 gives the line back there), a cost and not a failure. Option C is that strip plus a known limitation for a board
whose firmware turns the decodes on. The owner chose it, and it is in
`3.4.2.0` (task 47.40; section 6.6).

The known limitation for the case no filter reaches: on such a board
install Windows 2000 as "Standard PC" (F7 in text-mode Setup, which
disables ACPI for the whole system: no ACPI power management, battery or
thermal support). There the controller starts: on the PCI driver's own
line under `3.4.1.0` and with no strip (in the emulator, IRQ 10), and
under the strip polled (`XhciInterruptMode` 0 or 2) unless
`XhciIntelNoRouteIrq` is 0.

### 6.6 Windows 2000: the strip, as built

`37a9f39d`, `2c8e9cdd` and `90f8ecd4` (record 18 revision 19, section
16.4's "As built"), then `c07a6d56`; merged `303726e3` (2026-11-07).

- **The gate** (`XhciPirqW2kStrip`, decided once per controller at the end
  of AddDevice's chipset read): Windows 2000 (WDM 1.10, not 1.20), a board
  on value 1's list (at value 2 too), never at value 0, and the chipset
  read past its location, identity and reviewed-controller checks. As
  first built it took only a read that reached the offer; at the merge
  (`c07a6d56`, the owner's decision of 2026-11-07) it was widened to every
  decision from an unread LPC to the offer, so a listed board whose read
  failed later (the chipset window's mapping, the LPC, RCBA, the pin,
  `D20IR`) does not keep its interrupt. XP and XP x64 never strip.
- **The list:** the PCI driver's one configuration with its interrupt
  removed, as the list's only alternative, under every
  `XhciInterruptMode`. XHCITR names the outcome: 16, the line removed; 17,
  withdrawn. Where no interrupt-free list can be made (no memory, a shape
  or walk the driver refuses), the filter frees the list and returns none,
  successfully: the controller then shows Code 10 instead of the stop.
- **Recovery** from a stop after `XhciIntelNoRouteIrq` 0: "Last Known
  Good Configuration" at F8, not Safe Mode, which loads the USB
  controllers' drivers too and stops the same way. It works when 0 was
  set after the last start that reached the desktop; if Windows started
  with 0 before, Last Known Good keeps it too, and only the Standard PC
  computer type starts Windows 2000 there (`docs/using/tuning.md`).

Read in the emulator in the 8470p's state (`runs/run-47.md`, "47.33
(continued, 2)"):

| System | Values | Result |
|---|---|---|
| 2000, ACPI PC or ACPI Uniprocessor PC | listed, `XhciInterruptMode` 0 | **no stop**: Code 0, the memory alone, outcome 16, polled |
| 2000, ACPI PC | listed, `XhciInterruptMode` 2 | no stop; Code 0, forced polling |
| 2000, ACPI PC | listed, `XhciInterruptMode` 1 or 3 | no stop; Code 10, as designed |
| 2000, ACPI PC | listed, `XhciIntelNoRouteIrq` 0 | STOP 0xA5, as designed; then Last Known Good reached the desktop |
| 2000, ACPI PC | listed, each strip failure injected (no memory, a refused shape, a refused walk) | no stop; Code 10 |
| 2000, ACPI Uniprocessor PC | listed, no memory or a refused shape injected | no stop; Code 10 |
| 2000, ACPI PC | listed, the chipset read ended at a failed mapping (`c07a6d56`) | no stop; Code 0, outcome 16, polled |
| 2000, ACPI PC | not listed, control state | the list kept; Code 0 on the PCI driver's line |
| 2000, Standard PC | listed | Code 0, polled: the cost |
| 32-bit XP and XP x64 | listed | no strip; the offered GSI granted, a stick read |

On Windows 2000 a stick plugged after the snapshot was not mounted in
the legs' wait (no volume appeared); the controller's start and polled
delivery are the snapshot's.

## 7. Not the same as issue 12 (the E7250's interrupt routing repair)

Issue 12's Dell Latitude E7250 and these HPs both read `D20IR`, and on
both the xHCI's INTA goes to PIRQA, GSI 16. What the firmware tells
Windows differs, and so does the repair:

| | Dell Latitude E7250 (issue 12) | HP EliteBook 7-series (this issue) |
|---|---|---|
| The `_PRT` for device 20 | present, naming **GSI 21** | **absent** |
| What Windows does | connects the ISR on IRQ 21, where the xHCI never asserts: devices crawl, hang or fail | can offer no line: Code 12 under XP, STOP 0xA5 under 2000 with an ACPI HAL |
| The repair | `XhciIntelIrqRoute`: at each start and resume, **writes** `D20IR` so INTA reaches the line Windows assigned | `XhciIntelNoRouteIrq`: **writes nothing**; asks Windows for the GSI the chipset already routes INTA to, or polls |
| Where | every target that uses the line interrupt | the NT kernels below 6.0 |
| What is shared | the table of reviewed xHCI and LPC pairs (task 36.3), which value 2 of both switches reads | |

Issue 12's fix moves the chipset to match Windows; this one moves
Windows to match the chipset, because there is nothing in the firmware's
tables for Windows to be moved from.

## 8. XHCITR's firmware tables: the registry's copy (47.34)

On the 8470p under XP x64, Windows' firmware-table interface listed
eleven tables to `XHCITR /acpi` and no DSDT, nor either root table, so
the `_PRT` this issue turned on had to come from a Linux save instead. Task
47.34 (record 25 revision 28, section 21 item 10; `54ad49eb` to
`8ef8bf2d`, merged `177da81f`) made XHCITR read, where that interface
gives no good DSDT, the copies Windows itself keeps under
`HKLM\HARDWARE\ACPI` (DSDT, FACP, RSDT, FACS), read only, as it already
did on 2000 and 32-bit XP. Read in the emulator: on XP x64 the DSDT saved
from the registry byte for byte as the guest's memory, with both
executables; 32-bit XP, 2000, 7 x64 and 98 SE unchanged. The same task
made sure the save never carries the computer's licence tables: they are
listed and not saved, on every system. A real machine with no DSDT from
the interface (the 8470p) has not yet been read on that build.

## 9. Open questions

- **The reporter's machine** has not been read on any build with the fix.
  Its family's tables have the same missing entry; its decode state at
  boot rests on a public Linux log.
- **32-bit XP on the 8470p** (the bench checklist's part B): not read; no
  32-bit XP install is at hand there.
- **Windows 2000 on these machines** has never run; every 2000 finding
  here is the emulator's. The owner's reading on the 8470p, after the
  cut, if a Windows 2000 install is made there (the checklist's section
  C).
- **Windows 2000 under the strip:** a USB stick on the stripped list (in
  the legs one plugged after the snapshot was not mounted in the wait),
  and disable and enable, remove and rescan, a rebalance and a standby
  under it, are not read; the gate is evaluated on every filter IRP.
- **The MPS HALs** (record 18 section 16.2): what an MPS HAL makes of
  16 + the PIRQ for a device its MP table names no entry for is not read.
- **A storm** from an unserviced sharer on the offered GSI was not tried.
- **An HP-specific memory reservation** as a second cause is not excluded
  by the legs; the bench's Code 0 suggests it does not matter on the 8470p.

## 10. Rules kept

- **Read the hardware before reasoning about it.** One QUALREP put the
  leading guess out (the BAR was below 4 GB) and named the real state
  (decode off, no route).
- **Set the firmware's state in the emulator, then read Windows.** The
  two firmware facts were set one at a time before the loader, which
  showed it takes both, and no machine was needed to show it.
- **Keep a safe path under the better one.** The memory-only
  configuration (polled) came first and stays behind the GSI offer, so
  under the default mode a refused, unconnectable or silent line still
  ends in a working start on XP and XP x64. Windows 2000 under an ACPI
  HAL stops before any alternative is tried (section 6.5), so on the
  listed boards it is given the safe path alone (section 6.6).
- **A machine list ships empty until the machine is read.** Value 1's HP
  list was compiled in only after the owner's bench passed, and every
  guard stays when it applies.
- **A change taken to another system takes that system's legs.** The 2000
  stop was found only because the offer's extension to 32-bit 2000 ran
  2000 in the firmware's state.
- **Firmware images are read as data.** Only their ACPI tables were
  extracted and decoded; the images were deleted after; licence tables are
  not saved.

## Sources

- `docs/contributing/runs/run-47.md`: "47.33 HP EliteBook 2570p and
  8470p: XP x64's Code 12 on the USB 3 controller", "47.33 (continued):
  the GSI offer on 32-bit 2000 and XP, the owner's bench, the HP list",
  "47.33 (continued, 2): Windows 2000's STOP 0xA5 on the listed HPs" and
  "47.34 XHCITR's firmware tables".
- `docs/contributing/roadmap-usb4.md`, tasks 47.33, 47.34, 47.35 and 47.40.
- Design record 18 (`docs/contributing/design/18-msi-helper-x86.md`),
  revisions 14 to 17, sections 16 to 16.3; revisions 18 and 19, section
  16.4 (`a70e5b85`). Commits `38b36c2b`, `8c06e6e1`, `fe11ac5a`,
  `b05b9353`, `04dbd018`, `37a9f39d`, `2c8e9cdd`, `90f8ecd4`, `c07a6d56`;
  merges `d3a596b7`, `39f0317b`, `303726e3`.
- Design record 25 revision 28, section 21 item 10 (47.34).
- `docs/using/tuning.md`, "An interrupt where the firmware gives none
  (`XhciIntelNoRouteIrq`)" and "The Intel 7/8/9-series port switchover".
- Issue 12 (`12-e7250-xp64-boot-enable-slot-refused.md`) for section 7.
- The owner's QUALREP log, Device Manager readings, XHCITR snapshots and
  the 8470p's tables saved under Linux, held on the maintainer's disk
  (git-ignored).
- Public Linux boot logs of the 8470p, 8470w, 8570p, 8570w, 8770w, 2570p
  and ProBook 6570b (linux-hardware.org probes, Launchpad bug
  attachments, a 2013 LKML thread).
- HP SoftPaqs SP96091, SP96092, SP96085 and SP96088: their ACPI tables
  alone, read as data, the images deleted after.
- Microsoft, "Firmware Allocation of PCI Device Resources in Windows"
  (2006); Microsoft's bug check reference for 0xA5, ACPI_BIOS_ERROR.
- The worker's notes on the HP range's logs and tables, kept untracked
  with the run's scratch files.
