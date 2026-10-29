# Run 45 - Release `3.4.0.0`

Phase 45 of `roadmap-usb4.md`, for release `3.4.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

## 45.11 the kept kernel child: the prototype read under Driver Verifier (2026-10-29, DRAFT, partial)

DRAFT: the readings below are taken; the reading matrix is not complete,
the design section is not written and nothing here is decided. Record 31
section 9.3 items 11 and 12. Runtime, development host A, QEMU under TCG,
15:11 to 18:28.

### The vehicle

- The prototype: branch `w45-kept-proto` (never shipped, never merged),
  commits `f372c4ac`, `0b559009`, `30ead9fa`, `3cb28ab8` and `434665c8`
  on the base `70bd2e0e`; reviewed and revised up to `3cb28ab8`. On NT
  6.x a child the bridge's `BusRelations` attached does a logical removal
  at `IRP_MN_REMOVE_DEVICE` while its PDO stays (it stays attached and on
  the attachment list, its departure recorded, its snapshot cleared), is
  re-armed at its next `IRP_MN_START_DEVICE`, and is destroyed when its
  PDO is deleted, through the driver's `FastIoDetachDevice` callback
  (`cmFastIoDetach`, `CmFilterDetachCb`); one detach owner for the
  callback and every removal (`CmDetachOnce`); a reference across every
  other PnP request of a kept child; the links of a kept child to a
  departing filter dropped. The tunnel id is given on every NT system,
  8.1 included. `XhciCmKeptProto` 0 (DWORD, machine-wide `Services\USB`)
  turns the kept child off for the control leg. The `qemu` flavour,
  `xhcicm.sys` alone built (no gates: a test build), hashes `9a09615e...`
  (`3cb28ab8`) and `489cfc0d...` (`434665c8`); `cm.kept.*` records on the
  debug port.
- The package: the `3.3.0.0` release tree (`releases\3.3.0.0`), its
  `release-x86\stor98.inf` changed for the test alone: the
  `[Stor98_Models.NTx86.6.3]` line matches `PCI\XHCI98_TUNNEL&CC_010802`
  as the undecorated section does (`0285665c...`). Installed with
  `INSTALL.BAT /console /yes /nopause /thunderbolt /stor98` (exit 3), the
  prototype copied over `System32\drivers\xhcicm.sys` before the
  restart, Driver Verifier `verifier /standard /driver xhcicm.sys`.
- The guests: q35 bases `w81x86` and `w8x86`, fresh overlays, 2 vCPUs,
  2 GB; `launch-q35.ps1 -Usb4 asm4242 -EncAtBoot` (the ASM4242 model, the
  ORICO `asm2464` enclosure on USB4 port 1 at power-on: tunnel DSP,
  enclosure USP, DSP, NVMe, the drive at depth 5). Kits and logs:
  `E:\xhci98-vm\p45\w45-kept\k81a`, `k8a`.

### A defect found first: `cmQNt` under a spin lock (fixed, `3967a033`)

The first boot of 8.1 with the enclosure stopped with bug check 0xC4
(0x2000E, 0x8536C164, 0, 0): Driver Verifier's DDI compliance check
caught `IoIsWdmVersionAvailable` called by `CmQueryPublish` with the
query device's spin lock held (the thread's stack: `cmThread` ->
`CmQueryPublish` -> the folded `cmQNt`; read from the minidump's stack
against a map of the same objects). The same call stands in the PCI
answer under the lock. Not the prototype's; in every release with the
query device. Fixed on `w45-kept` (`3967a033`, reviewed): the answer is
read once in `CmQueryInit`, at `DriverEntry`.

### 8.1 x86, the kept child on (Verifier on `xhcicm.sys`)

| Step | Result |
|---|---|
| First boot, the drive tagged | `stornvme` first (rank `0xFF2007`); the mark service's 8.1 arm installed the id model (event 400 `oem3.inf`, rank `0x80FF2000`, Matching Device Id `PCI\XHCI98_TUNNEL&CC_010802`, `stornvme` outranked), two logical removals, the id given again at each restart; Stor98 running, `E:` ORICO; no second event 400 |
| Disable and enable (SetupAPI) | the id kept, Stor98 running, no event 400 |
| Stor98 installed again over itself (`DIF_INSTALLDEVICE`, its restart) | the same |
| The DSP above the drive disabled and enabled | the drive's PDO deleted with its parent's stack: `FastIoDetachDevice` called for the kept child at `PASSIVE_LEVEL`, outside any dispatch of its own, and it was destroyed; the DSP's kept chain child stayed, a new child was attached to the new PDO, the id given, Stor98 bound |
| Surprise unplug (QMP `device_del`) with a write loop and a file held open on `E:` | each kept child logically removed, then its callback, bottom-up (NVMe, DSP, USP); where the PDO went inside its own removal, the callback came inside the forwarded removal and the removal finished the destruction; a registered child (`LowerFilters`) above a kept child was called back when the kept child was deleted; no bug check |
| Hot re-plug | tagged again, Stor98 bound (the q35 Code 31 of 44.45 not met here) |
| Eject (`INSTALL /console /eject id=...`) | after the logical removal the kept child saw `IRP_MN_QUERY_RESOURCES`, `IRP_MN_EJECT`, `IRP_MN_QUERY_DEVICE_RELATIONS` and a second `IRP_MN_REMOVE_DEVICE`, the PDO deleted inside it; the departure recorded, `cm.reenum.eject`; the installer said "has not confirmed that the PCIe tunnel was closed", as it did with the kept child off |
| Cold boot | the id given, Stor98 running |
| Uninstall (`INSTALL /console /uninstall /yes`) | every kept child destroyed through its callback, the bridge filters' callbacks inside their removals, the driver unloaded; then bug check 0xC4 (0x62, ..., 1): one pool allocation of `xhcicm.sys` not freed at its unload. Whether the prototype's or earlier: not read (owed: the same with the kept child off and with `3.3.0.0`'s `xhcicm.sys`) |

### 8.1 x86, the kept child off (the control, the same overlay)

After a cold boot the drive ran Stor98 by the id; a disable and enable
lost the id at the enable, and 8.1 configured `stornvme` again: event
400, rank `0xFF2006`, Matching Device Id `PCI\CC_010802`, "Device
Updated: true". So a drive already on Stor98 leaves it at a later
restart of its stack on 8.1 (record 31 section 4.8's unread case), and
the kept child is what holds it there. Item 12's reading (why 8.1
chooses again): at a cold boot with the kept child back on, the drive
then on `stornvme` gained the id in its recorded ids and no event 400
followed (`stornvme` kept); 8.1 chose again only when the configured
driver's matching id had left the device's ids. Read as an observation
of 8.1's behaviour, not as its rule.

### Seen on the way (not the kept child's)

After `stornvme` had run the drive, Stor98 failed to start on it on 8.1
(problem 10, Stor98's error log entry 4 "internal error in its data
structures"), at an install and at a cold boot, with the device's
`Interrupt Management\MessageSignaledInterruptProperties` value
`MSISupported` 1 that `stornvme.inf` writes; with it set to 0 Stor98
started. Earlier boots of the same overlay with the value present
started Stor98, so the condition is not read whole.

### 8 x86

The prototype with Verifier booted with the enclosure; the drive tagged
and bound to Stor98 by Windows (event 400 `oem3.inf`). The matrix is
owed.

### Owed

The 8 and 7 x86 matrices; the unload leak's owner; the order of
`FastIoDetachDevice` with ACPI's bus filter (no child above an ACPI
filter on these q35 topologies); 2000, XP and 10 (record 31 section 9.3
item 11); the design section and its review; the owner's decision.
