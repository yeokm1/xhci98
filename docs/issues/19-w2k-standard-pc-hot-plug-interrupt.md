# Issue 19 - On Windows 2000 with the Standard PC computer type an NVMe drive plugged in behind a tunnel after the start gets no interrupt, and SCSIport refuses it

Status: **fixed for `3.3.0.0`, the drive running polled** (roadmap-usb4
task 44.53; design record 21 section 6.2.1). On Windows 2000 with the
Standard PC HAL the connection manager now gives such a drive the
interrupt of the tunnel port's own wire, so SCSIport starts it. The
bundled Stor98 then polls it, since its Interrupt Line still reads 0
(section 5). **Read on the emulator only:** QEMU's `q35` machine with
the emulated ASMedia ASM4242 USB4 host. No real machine has run Windows
2000 with this package and a tunnel (section 5).

**Scope.** This concerns only an NVMe drive behind a Thunderbolt 3 or
USB4 tunnel that is plugged in after Windows has started: a PCI device
that appears while Windows runs. Not affected:

- USB drives on `xhci98.sys`'s ports, including a dual-mode enclosure
  that came up on its USB connection;
- drives attached at power-on, which the BIOS sets up;
- an internal NVMe drive;
- Windows 2000 on the ACPI computer type, and XP, Vista, 7, 8 and 8.1 on
  theirs: on the emulator these gave the drive its interrupt themselves.
  XP on the Standard PC computer type could not be read (section 5).

Machine: QEMU `q35` (SeaBIOS) with the `usb4-emu` ASM4242 model, the
ASM4242 layout (`runs/run-44.md`, "44.50"):

- the controller's root port at 00:03.0;
- the tunnel ports at 02:00.0 and 02:01.0;
- the ORICO enclosure model (ASM2464 switch, `1B21:2463`) on USB4
  port 1, its NVMe at 05:00.0 when plugged in after the start.

Windows 2000 Professional SP4 on the Standard PC HAL (`hal.dll`,
hardware id `e_isa_up`, the q35 base `w2k`), with the ACPI PC HAL
(`halacpi.dll`, the base `w2kacpi`) for comparison.

## 1. What was seen

With the enclosure attached at power-on the drive worked: the BIOS had
written its Interrupt Line, `0Bh`, and it started on Stor98 with IRQ 11.
Plugged in after a start without it, the drive stayed at **Code 10**:

- Device Manager: "This device cannot start".
- The System log: source stor98, event 13, "The driver was configured
  with an incorrect interrupt", `IO_ERR_INCORRECT_IRQ`, unique value
  `418h`.
- The drive's resources: 16 KB of memory and no interrupt.
- Its configuration space: Interrupt Pin 1, Interrupt Line 0.

Stor98's debug-port trace showed `DriverEntry` and nothing after it.
SCSIport refused the start before it called Stor98's `HwFindAdapter`,
which is where Stor98's own no-interrupt polling would have taken over.
A test image of Stor98 with no interrupt routine failed the same way.
So nothing in Stor98 could reach it (`runs/run-44.md`, "44.50").

## 2. Why

On this HAL Windows 2000's `pci.sys` gives a PCI function the IRQ its
Interrupt Line holds, and SeaBIOS writes the Line at POST only [inf].
A function enumerated later reads 0 and gets no interrupt. The BIOS's
`$PIR` table names a PIIX3 router at 00:01.0, where `q35` has its VGA,
so no table routing is expected either [inf].

The ACPI HALs route a new function through the namespace's `_PRT`
whenever it arrives [inf]. On the same machine:

- Windows 2000 on the ACPI PC HAL gave the hot-plugged drive IRQ 11, and
  its volume mounted (`runs/run-44.md`, "44.53");
- so did XP, Vista, 7, 8 and 8.1 (44.45's legs).

The failure is the Standard PC HAL's.

## 3. The fix

Design record 21 section 6.2.1. `xhcicm.sys`'s child filter sits above
every PDO the tunnel's chain reports. On Windows 2000 with the Standard
PC HAL alone (the gate: NT 5.0, and the first hardware id of
`Root\PCI_HAL\0000` is `e_isa_up`), for a function whose requirements
from `pci.sys` hold no interrupt, it:

1. **Finds the wire.** It walks up to the active tunnel port and
   applies the PCI-to-PCI bridge swizzle to the function's pin. When
   the pin arrives on the port's own pin, the function shares the
   port's wire, and the port's BIOS-written Interrupt Line is its IRQ.
2. **Adds the interrupt.** One shared, level-triggered descriptor at
   that IRQ goes into each alternative of the function's requirements
   (the query, and the filter request when a held configuration lacks
   it).
3. **Keeps it out of `pci.sys`'s start.** The bus filter's contract
   says an added resource is taken out of the start passed to the bus
   driver. SCSIport, above, still receives the interrupt.

The location comes from the function's `DevicePropertyAddress` and the
secondary bus register of the bridge above it. Windows 2000 does not yet
answer `DevicePropertyBusNumber` when the requirements are queried
(`STATUS_OBJECT_NAME_NOT_FOUND`; the first gated image failed on that,
44.53 (continued)).

Read on the emulator (`runs/run-44.md`, "44.53 (continued)"):

- **The failing case** (a hot plug after a Shut Down and a cold start
  without the enclosure): the drive at 05:00.0 given IRQ 11, its start
  stripped. Stor98 started and reported "pin 1 line 0 ... port 11/11
  polling (interrupt line not set)". The ORICO volume was read.
- **Unchanged:** the cold boot with the drive, Windows 2000 on the ACPI
  HAL, 7 x86 and XP. The gate is shut there or `pci.sys` gives the
  interrupt itself.

## 4. What was tried and not taken

**Writing the Interrupt Line.** A test image wrote the IRQ into the
drive's Interrupt Line (3Ch) through its own stack once `pci.sys` had
started it. The request succeeded, but the byte came back 0, and the
Line read back 0 through the stack and through the HAL. Stor98 still
polled. `pci.sys` keeps the Line itself [inf: it puts its own value in
place of a written one, and SCSIport reads the miniport's configuration
through it], so this write cannot reach Stor98.

**Leaving the interrupt in `pci.sys`'s start.** A second test image
skipped the stripping of step 3. `pci.sys` started the drive with IRQ 11,
and Stor98 reported "line 11 ... port 11/11 interrupt". A 100 MiB file of
random bytes was copied to the drive and compared with `fc /b`; the
counts are QEMU's `info irq`, IRQ 11 on the 8259, during the copy:

| Run | Stor98 | IRQ 11 during the copy | Copy | Compare |
|---|---|---|---|---|
| hot plug, the start stripped as shipped (a test image that also tried the Line write) | polled | never fired | 8 s | no differences |
| hot plug, the interrupt left in `pci.sys`'s start | interrupt | 1,607 | 10 s | no differences |
| cold boot with the enclosure (reference) | interrupt | 1,606 | 10 s | no differences |

So the drive's interrupt does fire on the tunnel port's wire once
`pci.sys` holds it, as at a cold boot. The owner kept the polled start
(2026-10-27) because:

- that route departs from the requirements contract the stripped start
  follows;
- the other route to an interrupt-driven drive, Stor98 trusting the
  port's interrupt when the Line reads 0, is a change to Stor98 on every
  NT system;
- the polled drive showed no slowdown in the copy.

## 5. Limitations

- **The drive runs polled on 2000 with the Standard PC HAL.** With
  requests outstanding, Stor98 polls at the timer's period: the OS clock
  tick, 10 to 15.6 ms. It spins up to 500 us after each submission, and
  polls once a second when idle (Stor98's `PollBusyUs`, `PollSpinUs` and
  `PollIdleMs` defaults on NT). That costs some CPU time, which was not
  measured. The 100 MiB copy above took 8 s polled against 10 s with the
  interrupt, so no slowdown was measured.
- **The other non-ACPI HALs could not be read on the emulator**
  (`runs/run-44.md`, "44.53 (continued, 2)"). Each was booted through
  `boot.ini`'s `/HAL=` on a copy of an installed system:
  - Windows 2000's MPS HALs, `halmps.dll` and `halapic.dll`: the
    ASM4242's own upstream switch port was at Code 35 [inf: the
    firmware's MP table describes too little], so the USB4 host never
    started.
  - XP's Standard PC HAL, on an XP installed with the ACPI HAL:
    Windows enumerated its devices again, and the hot-plugged chain
    stopped below the enclosure's upstream port.

  The gate covers Windows 2000's Standard PC HAL alone. Whether the
  others have the gap is not known.
- **A Windows Restart on the `q35` Standard PC base stops in the loader**
  (a blank screen at `2000:0D4F`), as the base's README records from
  before this package was on it. It is the emulator's, not the
  package's: read when the ACPI base was made, after a reset QEMU leaves
  the local APIC's LINT0 masked, so no 8259 interrupt reaches the loader;
  that base's boot sector works round it (its README). The legs used
  Shut Down and a cold start.
- **Resume from standby or hibernation was not read** with the added
  interrupt. The design keeps its records across a stop; standby and
  hibernation with a tunnelled device attached are not supported anyway
  (release notes, "Thunderbolt and USB4: known limitations").
- **Real hardware was not read.** No real machine has run Windows 2000
  with a tunnel and this package yet, and the hardware bench (task
  44-E.6) reads 98 SE, XP and 10 x64 only (owner, 2026-10-27), so 2000
  stays read on the emulator alone. Which HAL Windows 2000 installs on a real board, and
  that board's `$PIR` and MP tables, are not known.

## 6. Evidence

`E:\xhci98-vm\p44\` (git-ignored, on the development host):

- `p44-w2khp\w2k\`: 44.50's debug-port traces `debugcon-a.log` to
  `-d.log` and `infopci-a-plug.txt`.
- `p44-w2kirq\acpi\`: the ACPI HAL reading (`serial-b.log`,
  `infopci-b-plug.txt`, `serial-c.log`).
- `p44-w2kirq\sp\`: the fix's legs (`serial-j.log` and `serial-k.log`),
  the first image's failure (`serial-b.log`) and its diagnostic
  (`serial-d.log`); the Line write, interrupt and copy runs
  (`serial-n.log`, `-p.log`, `-q.log`, `irq-*.txt`, `copy-*.txt`).
- `p44-w2kirq\spm\`: the MPS HALs (`serial-c.log`, `serial-e.log`).
- `p44-w2kirq\xps\`: XP's Standard PC HAL (screenshots `a2.png` to
  `a8.png`).
- `p44-w2kirq\pids.txt`.

## Sources

- `docs/contributing/runs/run-44.md`, sections "44.50", "44.53", "44.53
  (continued)" and "44.53 (continued, 2)".
- Design record 21 section 6.2.1; design record 31 sections 4.1 to 4.3
  (the child filter's chain); Stor98's `src/stor98/docs/PLAN.md`,
  "Polling mode".
- `src/cm/cm_irq.c`, `cm_filt.c`, `cm_reenum.c` (`CmRegIrqWire`),
  `cm_reg.c` (`CmReadIrqGate`); `test/test_cm_irq.c`.
- The q35 bases' READMEs, `E:\xhci98-vm\q35-bases\w2k\` and `w2kacpi\`.
