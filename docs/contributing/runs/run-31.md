# Phase 31 Record - Streams and UAS

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 31 - Streams and
UAS". The roadmap entry carries the goal, the status, the task list and the
checkpoint; this file carries what each task did and what each reading said.
Where the two disagree about a clause, the roadmap wins.

**Drafted ahead of the readings.** This file was written on 2026-10-04,
before the merged build of `p28-31-int` existed, so that the checkpoint's
readings only need filling in. A cell reading `TBD(merged build)` is a
clause of a V leg still to be read on that build; a plain `TBD` is a fact
this draft found no evidence for. Neither is a result. What is written as
known below cites the file it was read from.

**Written while the phase is open**, and kept as written rather than
rewritten in the past tense. A sentence saying something "is owed" describes
the day it was written; the roadmap's task list says how each task closed.

**On `out\...`, `vm\...` and `tools\...` paths in this file.** They say where
a reading was taken and what the file was called, on the host that ran it;
they are not files a clone has.

**How the guests were driven.** Every guest leg cited so far ran on
development host A on 2026-10-04 under QEMU (TCG, one virtual processor),
from the Phase 26 golden images or overlays of the Phase 29 legs' images,
with a fresh overlay per leg, driven by a GUI subagent through the QEMU
monitor. SuperSpeed legs use the `qemu-xhci,p2=4,p3=4` launchers of Phase 29
(`runs/run-29.md`, "How the guests were driven"), High-Speed legs Phase 26's.
QEMU's `usb-uas` is added with `device_add usb-uas` and a `scsi-hd` on its
bus, and a hot-plugged `usb-uas` is not attached until told: `qom-set
/machine/peripheral/<id> attached true` (`out\phase29\v1-2k-notes.md`, row
3). 31-V.2 passes a physical bridge through with `usb-host`.

**Whose read.** The sections citing a subagent's notes or report relay them
as written; neither the coordinator nor the docs subagent that drafted this
file re-read the evidence line by line, except the trace counts quoted under
"Windows 98 SE: the LUN's Code 10, and its fix".

Opened: TBD (the roadmap's status line is the record).

---

## 31-0 - the static readings

- **Streams**: `506d1ac` added 31-0 notes to
  `docs/usb-xhci-info/xhci-data-structures.md` (Stream Context Arrays, stream
  ids in TRBs and events, xHCI section 4.12), every row marked to verify
  against the PDF. The verification: TBD.
- **NUSB's Windows 98 mapping path**: read statically for the Windows 98 SE
  fix below ("USBNTMAP.SYS: what is known and how").
- **Whether the 98 SE CD's `layout.inf` carries any of the mapping files**:
  stock Windows 98 SE has an `ntmap.sys` 4.10.2222 (the stock-98 SE import
  evidence in `legal-provenance.md`, e.g. its `KeGetCurrentThread` row);
  whether the CD's `layout.inf` names `USBMPHLP.PDR`, `USBNTMAP.SYS` or
  `usbntmap.inf`: TBD.
- **How `usbstor.sys` presents its disks on the NT targets**: NT's
  `disk.inf` binding of `GenDisk` was not read (no `disk.inf` is extracted
  under `tools\`; `legal-provenance.md`, the NUSB `usbstor.sys` id-vocabulary
  row): TBD.
- **QEMU's `usb-uas`**: whether it offers a Bulk-Only alternate setting at
  either speed, read from `hw/usb/dev-uas.c`: TBD. Two things are known from
  the guests: it offers UAS at SuperSpeed and at High Speed (the transport
  line `port/transport/why/alternate=00012200` at both speeds,
  `out\phase29\v1-2k-notes.md`), and its control handler STALLs an endpoint
  `CLEAR_FEATURE(ENDPOINT_HALT)` (`c4ec1c3`'s message).

## 31-A.1 - primary streams in the bus

On branch `p31-streams`, merged into `p28-31-int` at `6f9f24d`:
- **`506d1ac`**: pure stream planning, layout, context, TRB and doorbell
  encoders and event lookup in `xhci_stream.c` (host suite `test_stream`,
  185 checks); a private interface, `src/xhci98_streams.h`:
  `IOCTL_XHCI98_OPEN_STREAMS` and `_CLOSE_STREAMS` to the device PDO hand out
  one pipe handle per stream, so a stream transfer is an ordinary bulk URB on
  that handle; stream-aware Configure Endpoint, Set TR Dequeue (SCT),
  doorbell, event routing, abort of one stream or all, close on select or
  removal; a controller with `MaxPSASize` 0 refuses the open.
- **`3611d81`, Codex round 1**: an OPEN_STREAMS Configure Endpoint issued and
  never completed retires its block until ownership provably ends; halt and
  error recovery moves only the failed stream's dequeue; a zero-pointer STALL
  on a streams endpoint is the endpoint's; retired blocks are reclaimed.
- **The merge, `6f9f24d`**: Phase 27's teardown stop counts a streams
  endpoint as queued when any stream's ring holds a TD.
- **The SuperSpeed UAS reset loop, `c4ec1c3`** (branch `p31-ssuas`, merged
  `fe2577b`): on Windows 2000, `usb-uas` at 5000 Mb/s looped through about 229
  full controller re-inits (`out\phase29\v1-2k-notes.md`, row 3, on `pkg-s1`),
  because the streams sequence sent `CLEAR_FEATURE(ENDPOINT_HALT)`, QEMU's
  `usb-uas` STALLed it, and the refusal reset the controller. No clear is now
  sent for an endpoint that has carried no request since the select that
  opened it, and a device's refused clear no longer resets the controller.
- **The recovery model, after Codex**: `6574583` (RESET_PIPE clears first, as
  `usbport.sys` does; stream use a persistent per-endpoint flag), then the
  integration's round 6, `d05990a` (a Halted endpoint's quiesce is Reset
  Endpoint with TSP 1 plus Set TR Dequeue and no device CLEAR_FEATURE;
  OPEN_STREAMS and CLOSE_STREAMS on a used endpoint send the clear on the
  paused, settled endpoint) and round 7, `28e9254` (a failed stream open
  restores the host's sequence with a fresh context; ABORT_PIPE on a streams
  endpoint's own handle cancels every stream's requests and closes nothing).
- **The departure drain over streams**, at the `p27-fix` merge `239d23a`: a
  departed device's queued, held or waiting stream URBs are held under the
  stream's own handle, and an abort horizon on the stream's or its
  endpoint's handle covers them.

## 31-A.2 - the UAS class driver, `xhciuas.sys`

On branch `p31-uas`, merged into `p28-31-int` at `4d32167`:
- **`4e66702`**: a separate binary and INF (`src\uas\`, `xhciuas.inf`
  binding only `USB\Class_08&SubClass_06&Prot_62`): an FDO on the bus's PDO,
  a PDO per LUN with `usbstor`-shaped ids under its own `XHCIUAS` enumerator,
  the four pipes from Pipe Usage descriptors, the information units (pure,
  host suite `test_uas`, 151 checks), tags and queue depth, autosense, task
  management and reset recovery, both transports - streamed at SuperSpeed
  through `src\xhci98_streams.h` and streamless at High Speed with READ READY
  and WRITE READY; its own import allowlist and INF gate.
- **`8619f23`, Codex round 1**: slot release only when idle, an FDO rundown
  count over every completion, worker and timer, per-transfer cancel
  references, per-LUN admission with drain at STOP, SURPRISE_REMOVAL and
  REMOVE; the reset SRBs report their real outcome. A short transfer keeps
  `STATUS_SUCCESS` on the outer IRP until NUSB's mapping layer is read.
- **`21dda9e`, Codex round 2**: LUN close - a tag returns to the allocator
  only after the device ended the task, otherwise it is quarantined until a
  port reset; a LUN closed with commands in flight runs ABORT TASK per tag,
  escalating to LUN RESET, port reset, then Dead.
- **`3c2bd27`**: `xhciuas.sys` for amd64 (its own allowlist,
  `xhciuas-amd64.inf` decorated `NTamd64` only), and the UAS pair published
  by `make-release.ps1` with `xhci98.sys`'s refusals.
- **`1ed1ba6`**: the Windows 98 half of `xhciuas.inf` - below.

## 31-A.3 - the id policy

On branch `p31-a3`, merged into `p28-31-int` at `030362f`:
- **`e0309f9`**: a pure decision (`xhci_xport.c`, host suite `test_xport`)
  applied once at PDO creation: UAS when offered and usable (below
  SuperSpeed always, at SuperSpeed only when the controller streams) unless
  `XhciForceBulkOnly` is set and alternate 0 is Bulk-Only; Bulk-Only
  otherwise; the chosen setting's compatible-id triple only. Under UAS the
  VID/PID hardware id stays, so a device `usbstor.inf` lists by hand still
  binds `usbstor.sys` - the residual case the roadmap records rather than
  fights.
- **`ffe00b1`, Codex round 1**: a refused interface's only hardware id is the
  project-owned `USB\XHCI98_NOXPORT&VID_v&PID_p&REV_r[&MI_nn]`, with no
  `USB\VID_` form and no compatible ids; `XhciXportRefusedAt` classifies
  behind a hub, a root port with a companion and a root port alone (host
  vectors).
- **The merge, `030362f`**: the send-back is Phase 29's `HcdHoldRequestUsb2`
  (`runs/run-29.md`, 29-A.5); a device it accepts keeps its record to the
  hold service's identity read, `PED` write and disconnect.

The non-streaming controller's branch rests on 31-A.3's host vectors, since
QEMU's controller streams (`MaxPSASize` 7 in the traces) and no
non-streaming one is held.

---

## Windows 98 SE: the LUN's Code 10, and its fix

**What failed.** On `pkg-s2` (`28e9254`), Windows 98 SE under NUSB 3.3 and
under SweetLow's stack (`out\phase29\v1-98-notes.md`, row 3;
`v1-98sl-notes.md`):
- `xhciuas.inf` installed, but the first install left the binary as
  `XHCIUAS.TMP` with a `WININIT.INI` rename and asked for a restart (Code 2
  until then);
- after the restart the UAS FDO worked - at SuperSpeed 16 streams granted on
  each of `0x82`, `0x83` and `0x04`, streamless at High Speed - but the LUN,
  "USB Disk" (Storage device, `USBMPHLP.PDR` and `IOS.vxd`, bound by NUSB's
  `usbntmap.inf`), read Code 10 and no drive letter appeared, at both speeds
  and under both stacks.

The owner's informal P14s Gen 1 run of the same day met the same Code 10 on
real silicon with the MSSU10.

**The fix, `1ed1ba6`** (branch `p31-uas98`, fast-forwarded into
`p28-31-int`): the undecorated install of `xhciuas.inf` writes
`HKR,,upperfilters,0,"USBNTMAP.SYS"` through `[Uas.Dev.HW]` - exactly the
value NUSB's own `USBSTOR.INF` writes for Bulk-Only sticks - naming the file
only, never copying it; and `[Uas.CopyFiles]` loses the 9x temporary-file
field, so a first install copies `xhciuas.sys` directly with no restart. The
`.NTx86` path has no `.HW` section and is unchanged. `check-uas-inf.ps1`
gained `UAS-FILTER` and `UAS-TMPNAME` and ten mutations (32 in all).

**Verified** on the `qemu` package `out\phase31\uas98\pkg-qemu`
(`xhci98.sys` SHA-256 `d739125d...7ea8`, `xhciuas.sys` `95cf360f...dace`),
the fix subagent's legs, traces `out\phase31\uas98\*-debugcon.log`,
screenshots `out\phase31\uas98\shots\`:

| Leg | Overlay | Result |
|---|---|---|
| NUSB 3.3, update over the old install, SuperSpeed (`uas98b`) | `vm\t26\win98-uas98b.qcow2` | Code 10 reproduced first; the fix applied through "Display a list of all the drivers", Have Disk (Search picks the cached old INF, since `DriverVer` is equal); no restart; F:, `fc /b` read and write clean; `upperfilters` = `USBNTMAP.SYS` under the device's `Enum\USB` key; 16 streams on each of three endpoints; one stall event while the old stack was torn down for the update, not recurring; a clean unplug |
| NUSB 3.3, the same overlay, High Speed (`uas98b2`) | as above | 480 Mb/s, streamless (no `streams open` line), F:, `fc /b` clean, a clean unplug |
| NUSB 3.3, clean first install, SuperSpeed (`uas98a`) | `vm\t26\win98-uas98a.qcow2`, from the golden image | no restart prompt, no `.TMP`, no `WININIT.INI`; F: within about 30 s; `fc /b` read and write clean; a clean unplug |
| SweetLow from clean, SuperSpeed (`uas98sl`) | `vm\t26\win98-uas98sl.qcow2` | the controller's own install asked for the CD and a restart as usual; UAS with no restart prompt, F:, 16 streams on each of three endpoints, `fc /b` clean |
| Windows 2000 regression, SuperSpeed (`uas98k`) | `vm\t26\win2k-uas98k.qcow2` | both binaries copied by hand; 5000 Mb/s, 16 streams on each of three endpoints, F:, `fc /b` read and write clean, no stall, no controller re-init, a clean unplug |
| Remove and re-plug (`uas98c`) | `win98-uas98c` | abandoned on the coordinator's instruction; no result |

- The `.NTx86` path's shared `[Uas.CopyFiles]` edit was not re-read through
  an INF install on NT; the Windows 2000 leg copied the files by hand.
- What NTKERN does with a named upper filter whose file is missing was not
  measured. NUSB 3.3's and 3.6's own installers place `USBNTMAP.SYS`,
  `USBMPHLP.PDR` and `usbntmap.inf` unconditionally; SweetLow's package has
  no storage half.
- The release-note line that Windows 98 SE and ME UAS needs NUSB's
  mass-storage component: TBD (not written yet).

**Real silicon, informally.** On the owner's P14s Gen 1 with the informal
release package of `1ed1ba6` (`out\informal-p14s-uas98`), Windows 98 SE with
NUSB: the MSSU10 under `xhciuas.sys` at SuperSpeed got a drive letter; its
ATTO figures are in `runs/run-29.md`, "An informal reading, not 29-E.2". Not
a checkpoint reading.

### USBNTMAP.SYS: what is known and how

Drafted by the fix subagent for `legal-provenance.md` section 4; that row is
not in the tree yet (TBD), and this is its statement for the run record.

NUSB's Windows 98 storage path needs `USBNTMAP.SYS` as an upper filter on
the parent of each unit. NUSB's `usbntmap.inf` binds `USBSTOR\GenDisk` (and
`GenCdRom`, `GenSfloppy`, `GenOptical`, `GenSequential`) to `DevLoader=*IOS`
with `PortDriver=USBMPHLP.PDR`. `USBMPHLP.PDR` (SHA-256 `e8e0604f...698c1c`)
is a stub that forwards to `USBNTMAP.SYS` (SHA-256 `d1595cdb...cc6e4`, the
same in NUSB 3.3 and 3.6). `USBNTMAP.SYS`'s AEP_INITIALIZE handler (VA
`0x10AA6`) walks the unit device object's `AttachedDevice` chain for an
object its own driver object owns and fails with `0xFFFF` if there is none;
its BusRelations completion routine (`0x104B4`) creates a `PDO$` filter
object on each child PDO - which it can do only as an upper filter on the
parent. AEP_DEVICE_INQUIRY sends `IOCTL_STORAGE_QUERY_PROPERTY` (`0x2D1400`)
in three steps and needs `Size >= 0x4B` and `RawPropertiesLength >= 0x24`;
I/O is `IRP_MJ_SCSI` through a 64 KB bounce MDL. NUSB's `USBSTOR.INF`
writes `HKR,,upperfilters,0,"USBNTMAP.SYS"` in `[USBSTOR.HW.AddReg]`, and
NUSB 3.3's and 3.6's `_NUSB.INF` `[DefaultInstall]` copies `usbntmap.sys` to
`10,SYSTEM32\DRIVERS` and `usbmphlp.pdr` to dirid 12 unconditionally.

Method: **static**. The two binaries were read by disassembly
(`tools\nusb-extracted\USBNTMAP.SYS` and `USBMPHLP.PDR`; the coordinator's
read, 2026-10-04, not re-read by the fix subagent); the INFs by text
(`tools\nusb-extracted\USBSTOR.INF`, `USBNTMAP.INF` and `_NUSB.INF`, and
`tools\nusb36-extracted\usbstor.inf` and `_nusb.inf`), and the 3.3 and 3.6
copies compared by SHA-256, by the fix subagent; nothing executed. The guest
runs (`out\phase31\uas98`) observed only that, with the value written, the
unit starts and gets a drive letter; nothing about `USBNTMAP.SYS`'s internals
was observed. The files are Microsoft's and NUSB's installer places them;
this project names `USBNTMAP.SYS` in `xhciuas.inf` and ships neither file
(`src/uas/xhciuas.inf` `[Uas.Dev.HW]`; `check-uas-inf.ps1` `UAS-FILTER`).

---

## 31-V.1 - QEMU's `usb-uas` on both primaries

The clause: at SuperSpeed and at High Speed, enumerated, bound to the UAS
driver, a verified round trip in each mode, on a fresh install and again
after an uninstall and re-plug. The forced-Bulk-Only leg cannot be shown on
this model if 31-0 confirms it has no Bulk-Only setting.

### Readings taken before the merged build

**Windows 2000 SP4:**
- SuperSpeed, 16 streams: on `c4ec1c3`'s package `out\phase31\ssuas-pkg1`
  (`xhci98.sys` `c6cb2041...1693`; traces `out\phase31\ssuas-launch1-
  debugcon.log`, screenshots `out\phase31\shots\ssuas-*`); again on `pkg-s2`
  (`28e9254`; `out\phase29\v1-98-notes.md`, "Windows 2000
  re-confirmation": `streams open` granted 16 on `0x82`, `0x83` and `0x04`,
  no re-init, `fc /b` read and write clean, a clean unplug); and on
  `1ed1ba6`'s package in the regression leg above.
- High Speed, streamless: on `pkg-s1` (`8e0e3bd`; `v1-2k-notes.md`, launch 3:
  "xHCI98 USB Attached SCSI Storage" working, "QEMU QEMU HARDDISK UAS
  Device", `fc /b` read and write clean, a clean unplug), on `c4ec1c3`
  (`ssuas-launch2-debugcon.log`) and on `pkg-s2` (`v1-98-notes.md`, launch
  `s29k6`).
- Its first install (on `pkg-s1`) went through the Found New Hardware Wizard
  to `e:\xhciuas.inf` with no signature prompt and asked for a restart.

**Windows 98 SE:** Code 10 at the LUN on `28e9254`; fixed by `1ed1ba6` and
read at both speeds under NUSB 3.3 and at SuperSpeed under SweetLow (above).

### The reading on the merged build

Build: TBD (commit, `xhci98.sys` and `xhciuas.sys` SHA-256). Evidence
directory: TBD.

| Clause | Windows 98 SE, NUSB 3.3 | Windows 2000 SP4 |
|---|---|---|
| SuperSpeed: enumerated at 5000 Mb/s, transport UAS | TBD(merged build) | TBD(merged build) |
| SuperSpeed: bound to `xhciuas.sys`, LUN started, drive letter | TBD(merged build) | TBD(merged build) |
| SuperSpeed: streams granted (count per endpoint) | TBD(merged build) | TBD(merged build) |
| SuperSpeed: `fc /b` read and write round trip | TBD(merged build) | TBD(merged build) |
| High Speed: enumerated at 480 Mb/s, transport UAS, streamless | TBD(merged build) | TBD(merged build) |
| High Speed: bound to `xhciuas.sys`, LUN started, drive letter | TBD(merged build) | TBD(merged build) |
| High Speed: `fc /b` read and write round trip | TBD(merged build) | TBD(merged build) |
| Fresh install: prompts and restart as recorded | TBD(merged build) | TBD(merged build) |
| After uninstall and re-plug, SuperSpeed: round trip | TBD(merged build) | TBD(merged build) |
| After uninstall and re-plug, High Speed: round trip | TBD(merged build) | TBD(merged build) |
| `XhciForceBulkOnly` set: the UAS-only device stays UAS, counted | TBD(merged build) | TBD(merged build) |
| Unplug, no controller re-init, no stall storm | TBD(merged build) | TBD(merged build) |

---

## 31-V.2 - the passed-through ASMedia bridge

The clause: the Windows 2000 half of the dual-transport clauses, which has
no bench - the ASMedia bridge passed through to the Windows 2000 guest with
`usb-host` at High Speed, UAS chosen on a fresh install, the forced-Bulk-Only
value selecting Bulk-Only, and the switch between them through uninstall and
re-plug; the same leg on the Windows 98 SE guest under NUSB as a VM control
for 31-E.1.

### The leg on `1ed1ba6`, as its report stood when this was drafted

Running on 2026-10-04 on branch `p31-v2` at `1ed1ba6`; its report,
`out\phase31\v2\report.md` (written 10:47), is quoted here as it then stood,
and its logs and screenshots are in the `p31-v2` worktree's
`out\phase31\v2\`. Device: the StoreJet, an ASMedia bridge `174C:5106`, at
480 Mb/s, `usb-host,bus=xhci.0,port=1,hostbus=1,hostaddr=8`, under
`qemu-xhci,p2=8,p3=0`. Package `pkg-1ed1ba6` (x86, `qemu` flavour;
`xhci98.sys` `b4b4f2ba...2176`, `xhciuas.sys` `e343b702...fa2e`).

| Clause | Windows 2000 SP4 | Windows 98 SE + NUSB |
|---|---|---|
| (a) UAS chosen, `xhciuas.sys` bound, round trip | PASS (runs 1 and 2) | PASS |
| (b) value set: Bulk-Only selected, `usbstor` bound, no `Prot_62` | PASS | not reached |
| (b) Bulk-Only round trip | FAIL: Code 10 (finding 1) | not reached |
| (c) value cleared: back to UAS, round trip | PASS (run 2) | not reached (finding 2) |

- Transport lines: UAS `00012101`, Bulk-Only `00011400`; UAS runs
  streamless at High Speed, alternate 1.
- Under UAS the device carries `Prot_62` compatible ids and service
  `xhciuas`; under Bulk-Only the `Prot_50` triple and service `USBSTOR`, no
  `Prot_62`. On Windows 98 the UAS device carries the upper filter
  `USBNTMAP.SYS`. Neither target's `usbstor.inf` names `174C:5106`, so the
  hand-listed vendor case did not arise.
- **Finding 1, Windows 2000 Bulk-Only Code 10** - the report's hypothesis is
  the vehicle: the first Bulk-Only transfer never completes, and
  passthrough emulates SET_INTERFACE and SET_CONFIGURATION and does not issue
  a real bus reset, so the bridge may stay in its UAS setting. Not settled;
  the report proposes a physical replug with the value already set, or the
  bench.
- **Finding 2, Windows 98: port 1 not re-enumerated after Device Manager
  Remove and replug** - the report's hypothesis is a driver defect of the
  same family as the removed-PDO defect found in Phase 27, with a fix in
  progress on branch `p28-pdoleak`. Not settled.
- QEMU itself froze about 30 s after a probe tablet's first class request on
  the Windows 98 leg; cause unconfirmed.
- Data: every write was `F:\XHCI98RT.BIN`, checked with `fc /b` and deleted.

### The reading on the merged build

Build: TBD. Evidence directory: TBD.

| Clause | Windows 2000 SP4 | Windows 98 SE + NUSB (control) |
|---|---|---|
| Fresh install: UAS chosen (`Prot_62` ids), `xhciuas.sys` bound | TBD(merged build) | TBD(merged build) |
| UAS round trip, `fc /b` | TBD(merged build) | TBD(merged build) |
| Value set, uninstall and re-plug: Bulk-Only chosen (`Prot_50` ids), `usbstor.sys` bound | TBD(merged build) | TBD(merged build) |
| Bulk-Only round trip, `fc /b` | TBD(merged build) | TBD(merged build) |
| Value cleared, uninstall and re-plug: back to UAS | TBD(merged build) | TBD(merged build) |
| UAS round trip after the switch back | TBD(merged build) | TBD(merged build) |
| Finding 1 settled (vehicle or driver) | TBD(merged build) | n/a |
| Finding 2 settled (port re-enumerates after Remove and replug) | n/a | TBD(merged build) |

---

## 31-E.1 - the bench

**Waits for the combined bench session (owner, 2026-10-03).** On Windows 98
SE and 32-bit Windows 7: the ASMedia bridge under UAS at SuperSpeed and,
behind a USB 2.0 hub, under UAS at High Speed; the MSSU10 under UAS at
SuperSpeed, and behind a USB 2.0 hub under Bulk-Only, which is all it offers
there and is the expected result; the forced-Bulk-Only value on both units at
SuperSpeed; round trips and throughput against Bulk-Only in each case. On
Windows 98 SE every clause is taken with NUSB installed.

---

## Codex

31-A.1's round 1 is `3611d81`, 31-A.2's rounds 1 and 2 are `8619f23` and
`21dda9e`, 31-A.3's round 1 is `ffe00b1`, and the SuperSpeed UAS fix's review
is `6574583` (`.claude\codex-ssuas-r1-result.txt`, not tracked); the
integration's rounds 6 and 7 (`d05990a`, `28e9254`) are the recovery model
above, and rounds 8 and 9 found no MAJOR or MINOR
(`.claude\codex-p2831-r8-result.txt`, `-r9-`). A review of `1ed1ba6` and of
the merged build: TBD.

Closed: TBD.
