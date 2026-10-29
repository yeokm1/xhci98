# Run 45 - Release `3.4.0.0`

Phase 45 of `roadmap-usb4.md`, for release `3.4.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

## 45.12 the requirement lists of hot-added bridges on 2000 and XP: the code, and the readings R1 and R5 before it (2026-10-29)

Design record 33 coded: the pure rewriter `src/cm/cm_breq.c` and its
host suite `test\test_cm_breq.c` (the templates on the lists captured in
44.63 and 44.64, one change of every compared byte refused, the worked
examples of section 6.5 byte for byte on both systems, the rules' edges,
section 6's arithmetic bounds, the BAR walk, the diagnostic and a seeded
sweep of the invariants); the bridge case of the child filter's
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS` in `cm_filt.c` (NT 5.x alone, the
lowest of this driver's filters in the stack, below an active tunnel
port) with its snapshot read from configuration space in `cm_reenum.c`;
the registered child's role by position (`CmRolePlace`, after the NHI's
answer) and ancestry (`CmRegAttLink`'s second search over the child
filters' snapshots); the bridge inventory in the marks answer
(`cm_query.h` `CM_QB_*`); the mark service's bridge marks, the restart
and its trigger and the other restarts' wait (`xhcimark\`, its pure half
in `mark_core.c` with the new host suite `test\test_mark.c`);
`XhciCmBridgeReq` (read on NT 5.x alone) and `XhciCmBridgeReqDiag`. Not
made: the installer's "Advanced..." row for `XhciCmBridgeReq` (record 33
section 9.1).

**The readings before the commit (record 33 section 10.2, R1 and R5).**
Runtime, development host A, QEMU under TCG, 15:38 to 16:40; at most
three virtual processors. Emulator `asm4242-20261027-boardint`
(`qemu-system-x86_64.exe` SHA-256 `deca3fcb...`) with 44.63's ASM4242
layout (the root port `bus-reserve=103,mem-reserve=776M,io-reserve=24K,
pref32-reserve=4M,hotplug=off`; `usb4-asm-usp` `fw-bus-pad=48,
fw-io-pad=12K,fw-mem-pad=384M`; both tunnel ports
`power-controller=off`). The guests: fresh overlays of the q35 XP and
2000 bases (2000 on its Standard PC HAL), `3.3.0.0`'s release package
installed (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`; on
2000 twice, the first run's documented Code 12 restart between), then
this tree's test build put in its place by hand: the `qemu` flavour
`xhcicm.sys` (SHA-256 `67d46177...`, port `0xE9` log) and `XHCIMARK.EXE`
(`73628344...`, then `f0913e22...` for the R5b legs), the golden disk
shut down, and each leg a child overlay of it. The dock: `usb4-device
...,dialect=usb4,dock=on,drive=nvme0` on USB4 port 1 by QMP `device_add`
(the TI XIO3130 switch `104C:8232`/`8233` with the NVMe, an e1000e and a
qemu-xhci; for R5b also `usb-drive=` a USB disk on the dock's xHCI).
Evidence `E:\xhci98-vm\p45\w45-dock\` (`xp\`, `w2k\`: the port `0xE9`
logs `debugcon-r5a.log` and `-r5b.log`, the serial dumps and their
decodes `D*-r5a.txt`, `D*-r5b.txt`, `infopci-r5a.txt`).

| Leg | System | Result |
|---|---|---|
| R1 and R5a: a never-seen dock hot-plugged, the service running | XP | The upstream port's list rewritten at its first filter IRP with its secondary bus already numbered (its `18h` `00040403`), three downstream ports counted: I/O 12, 8, 4 KB; memory 384 MB halving to 2 MB, then 1 MB; prefetchable 2 MB, 1 MB (example 1). Each downstream port's list rewritten with every sibling's bus numbered at its IRP (`18h` `00050504`, `00101004`, `001B1B04`): the NVMe's and the xHCI's ports 128 MB of memory alone (I/O and prefetchable dropped), the NIC's 4 KB of I/O and 128 MB. Every start after a rewrite succeeded (`XhciCmBridgeReqDiag`: 8 rewrites, 8 starts, 0 failed). `info pci`: the upstream port the tunnel port's whole windows (I/O `1000h-3FFFh`, memory `CE000000h-E5FFFFFFh`, prefetchable `CD200000h-CD3FFFFFh`); each downstream port 128 MB; the NVMe's and the xHCI's ports started with their I/O and prefetchable windows closed, the NIC's with I/O `3000h-3FFFh`. All three downstream ports Code 0 (44.63: two at problem 12). The bridges installed silently; the service wrote the three bridge marks after their installs (16:00:42 to 16:00:53), waited while the dock's functions' installs were pending (Found New Hardware), then made one restart (16:10:17), with the NVMe's install set to run again first; after it the NVMe came back with the tunnel id at depth 3 below the registered children, Windows' wizard installed Stor98, and the drive mounted (`D:` ORICO); the dock's xHCI on xHCI98 with its USB tablet; the NIC at problem 28 (no XP driver, as 44.45). `LowerFilters` `xhcicm` on the upstream port and all three downstream ports; `CriticalDeviceDatabase\pci#cc_0604` clean (the service took out a copy the unit mark's install had made, see below) |
| R1 and R5a | 2000 | The same rewrites with 2000's template (shape 2) and the same windows in `info pci`; every start succeeded; the bridge marks written at 16:01:16; one restart at 16:14:43 after the wizards; the NVMe on Stor98 mounted (`E:` ORICO) on the Standard PC HAL |
| R5b: the service stopped at the plug, started after the bridges' installs | XP | With no bridge marks, the bridges' installs took the children away: the second and third downstream ports at problem 12, the first placed with no filter (the NVMe untagged, no driver). At its start the service wrote the three bridge marks and logged the trigger: "04:00.0 runs with no filter of xhcicm below it", "04:01.0 has no resources (problem 12)", "04:02.0 has no resources (problem 12)"; one restart (16:31:07); after it every port Code 0 and the NVMe on Stor98 |
| R5b | 2000 | Every port placed by the rewrite at the first start, then each run with no filter after its install (the trigger: all three "run with no filter"); the USB disk on the dock's xHCI started: the restart refused, "a disk or volume below the unit is in use. Unplug the dock safely and plug it in again", and the unit mark's own restart refused the same way. A re-plug (QMP `device_del`, `device_add`): the marks of the departed dock removed at the service's next poll ("the unit left"), the kernel chain on every bridge at the new plug, the lists rewritten again, the marks written again with no restart, the NVMe arriving with the tunnel id (Windows' wizard offering Stor98) |

**What the readings settle.** R1: at each bridge's filter IRP its own
bus numbers and, for a downstream port, its siblings' are already
written (record 33 section 2.4's [inf], read on XP and 2000), and a
bridge found closed starts from a list with its I/O group removed, its
I/O window left closed. R5: the bridge marks are written after the
bridges' silent installs; the restart's trigger reads both the problem 12
and the placed-without-attachment cases; the restart waits for pending
installs and is refused with a started disk below; a re-plug places the
dock with no restart.

**Changed by the readings.** The unit's own mark was written while the
unit's install was pending, and Windows copied it into
`CriticalDeviceDatabase\pci#cc_0604` (the service's scrub took it out at
its next poll, 16:00:40): where the bridge marks follow a unit that is a
bridge, its own mark now waits for its install as the bridge marks do
(record 33 section 4.3, "the unit itself when it is a bridge"). The
service's "not made yet" line was written at every poll while installs
were pending; it is now written once a reason, and what asked for the
restart once an arrival.

**The legs on the gated build (record 33 section 10.2).** Runtime,
development host A, 19:08 to 22:21, at most five virtual processors. The
gated x86 build of `98c22600` (debug, release and `qemu` flavours, gates
and host suites passed: `test_cm_breq` 17130 checks, `test_mark` 149);
the amd64 release compiler refused the zero plan's declaration (C4132),
initialised in `4d4aacd0`, and the gated amd64 build then passed; both
release packages made (x86 `xhcicm.sys` SHA-256 `587b7e96...`,
`XHCIMARK.EXE` `3ca901f3...`). Guests: fresh overlays of the q35 XP,
2000 (Standard PC HAL) and 7 x86 bases with this release installed from
the package (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`),
the same emulator and layout as above; the XP legs R7d to R7g below are
child overlays of the R5a leg's disk with the test build. Evidence
`E:\xhci98-vm\p45\w45-dock\` (`xp2\`, `w2k2\`, `w7\`, `xp\`: `D*.txt` and
`W*.txt` dumps, `report-*.txt` XHCITR reports, screenshots).

| Leg | System | Result |
|---|---|---|
| R3: the dock hot-plugged on a fresh install | XP | All three downstream ports Code 0; the NVMe on Stor98 (`D:` ORICO), the dock's xHCI on xHCI98 with its USB tablet, the NIC at problem 28 (no XP driver). `XhciCmBridgeReqDiag`: 8 rewrites (the first starts and the restart's), 8 starts, 0 failed. `LowerFilters` `xhcicm` on the upstream port and the three downstream ports. Seen: with the unit's mark now waiting for the unit's install, the downstream ports went to problem 12 after the upstream port's install rebuilt their stacks, and stayed there until the restart for the bridges (19:53:34), made once the Found New Hardware wizards had been answered: per the record (the restart waits for pending installs), but an interval the user sees |
| R3 | 2000 | All ports placed; one restart (19:43:08) after the wizards; the NVMe on Stor98 (`E:` ORICO). Diagnostic: 7 rewrites, the upstream port's second list (after the restart) in the configured shape and left (shape 0, as section 7.4 says of 2000); `LowerFilters` on the four bridges |
| R8: 7 x86 | 7 | 7 placed the dock itself: every function Code 0, the NIC on its inbox driver, the NVMe on Stor98. No `XhciCmBridgeReqDiag`, no bridge marks, the unit's mark alone. `FilteredConfigVector` differs from `BasicConfigVector` on all four bridges: the basic list asks 1 MB of memory and of prefetchable memory and 4 KB of I/O, the filtered one 4 MB preferred with 1 MB alternatives and an I/O alternative of length 0. That is 7's own filtering (the rewrite does not act on NT 6, and no diagnostic entry was posted), so record 33's R8 expectation "filtered equal to basic" is restated as "the lists as 7 makes them" |
| The switch, `XhciCmBridgeReq` 0 | XP | Outcome "off" on all four bridges (no location recorded), no rewrite, the second and third downstream ports at problem 12 as `3.3.0.0`, the unit's mark alone, no bridge mark |
| The switch, 2 | XP | Outcome "record" on all four with the computed sizes (the upstream port 384 MB; each downstream port 21 MB), nothing replaced: the second and third downstream ports at problem 12, the first with the NVMe placed (`D:` ORICO); no bridge mark |
| R6: a cold start with the dock attached | XP | Every port Code 0, the NVMe on Stor98 (`D:` ORICO); each bridge's `FilteredConfigVector` equal to its `BasicConfigVector`, in the configured shape (`148h` and `E8h` bytes, not the closed list's `268h`); no rewrite attempt recorded in that start; the bridge marks and the unit's mark written at the service's first poll, no restart |
| R7: unplug and plug in again, the dock with a USB disk on its xHCI | XP | Three times (a plug after the switch's legs, a re-plug 3 s after the unplug, one 90 s after): at each unplug the service removed all four marks within a second ("the unit left"), so the case of section 7.5 with the bridge marks still in place is not reached by an unplug the service sees; at each plug the kernel chain alone rewrote the four lists in the closed shape (4 more rewrites each time, 12 in all, every start succeeded), the marks were written again with no restart, every port Code 0, `D:` ORICO back |
| R7, the same | 2000 | Twice (3 s and 90 s): the same, 2000's template each time (shape 2), the marks written again with no restart, `E:` ORICO back |
| R7 with the dock's USB tablet | XP, 2000 | **Fails: Windows' removal of the dock stalls.** On XP the unplug's departure is read (`cm.reenum.departure`), but the surprise removal never reaches the switch's own filters (no `cm.reenum.unexpected`, no `cm.chain.remove`): the dock's devices stay listed Code 0, a re-plug is not enumerated (no `BusRelations` reaches the tunnel port after the tunnel comes up again), and the shutdown does not finish. 2000 shows "Unsafe Removal of Device" and then the same: the re-plug not enumerated. Read on the gated release (XP and 2000) and on the test build (XP, legs R7d, R7e with no drive, R7g). Seen: with `XhciCmBridgeReq` 2 (R7f: the dock's xHCI and NIC never start) the unplug is removed normally; with switch 1 and a USB disk in the tablet's place (R7g) it is removed normally (`cm.reenum.unexpected`, seven `cm.chain.remove`); the tablet again in the same guest stalls. The cause is not established. No earlier release started the dock's xHCI on XP (its downstream port stayed at problem 12); owed to a task of its own |
| R9: the single enclosure (ORICO model) hot-plugged | XP | Its two bridges' lists not matched by a template (outcome "shape", five attempts), left; the drive on Stor98 (`D:` ORICO). The service wrote the downstream port's bridge mark and asked for the restart ("runs with no filter of xhcicm below it"), refused: "a disk or volume below the unit is in use. Unplug the dock safely and plug it in again" (the XP refusal with a started volume below) |
| R9, the same | 2000 | Lists left (outcomes "left" and "shape"); the drive on Stor98 (`E:` ORICO); the bridge mark written, no restart asked |

**Not read in this run (owed).** R4 (XP x64: the amd64 package is made;
its closed list is not read, so its template is not added), 2000 on the
ACPI HAL, R6 on 2000, R7's replug with the bridge marks still in place
on XP and 2000 (not reached: the service removed them within a second of
each unplug), R8's single enclosure on 7 x86 and R8 on 7 x64, R9 at a
cold start, R10 (the chain, after 45.16), R11 (standby and hibernation),
R12 (the uninstall and the update over `3.3.0.0`), R13 (98 SE and ME).
