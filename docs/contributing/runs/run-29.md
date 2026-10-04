# Phase 29 Record - SuperSpeed on root ports

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 29 - SuperSpeed
on Root Ports". The roadmap entry carries the goal, the status, the task list
and the checkpoint; this file carries what each task did and what each
reading said. Where the two disagree about a clause, the roadmap wins.

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
development host A on 2026-10-04 under QEMU (TCG), from the Phase 26 golden
images (`vm\t26\win98-gold.qcow2`, `vm\t26\win2k-gold.qcow2`; `run-26.md`,
"The golden images") with a fresh overlay per leg, driven by a GUI subagent
through the QEMU monitor. A SuperSpeed leg uses a launcher that differs from
Phase 26's only in `qemu-xhci,p2=4,p3=4` (`out\phase29\win2k-ss.cmd`,
`win98-ss.cmd`); a High-Speed leg uses Phase 26's own (`p2=8,p3=0`). With
`p3` above 0 each QEMU port is one connector carrying an xHCI USB 2.0 port
and a USB 3 port, a device able to run SuperSpeed always lands on the USB 3
one, and QEMU puts the USB 3 ports at xHCI ports 1-4 - so no USB 2.0 leg of a
SuperSpeed-capable model can be taken on a SuperSpeed launcher
(`out\phase29\v1-2k-notes.md`, row 2).

**Whose read.** The sections citing `out\phase29\*-notes.md` are the GUI
subagents' notes as written, not re-read line by line by the coordinator or
by the docs subagent that drafted this file.

Opened: TBD (the roadmap's status line is the record).

---

## 29-0 - the specification transcription

`cd65bda` added section 10 to `docs/usb-xhci-info/xhci-data-structures.md`.
That section states its own standing: it was drafted from the drafting
agent's knowledge of the xHCI 1.2 and USB 3.2 specifications with no PDF
open, and every row is to be verified against the PDF before 29-0 is ticked;
Codex's review of the Phase 29 draft (round 1) made a first check of some
rows against the local xHCI 1.2c PDF, and `017048c` took its corrections.
- The page-by-page verification of section 10: TBD.
- The USB 3.2 specification fetched into `docs/references/` with its SHA-256
  and licence note: TBD.
- Which P14s Gen 1 connectors reach which controller - the chipset
  controller's (`8086:02ED`) ports 13-18 or the Thunderbolt controller's own
  xHCI - and whether that one is present to Windows 98 SE: TBD (section 10.9,
  "Not answered by this draft").

## 29-A.1 to 29-A.6 - what was built

On branch `p29`, merged into `p28-31-int` at `5d67641`:

- **`cd65bda`, the draft.**
  - 29-A.1: every protocol port managed and powered, the rate read from the
    PSI dwords apart from the speed class, `LEC` read.
  - 29-A.2: the link state machine as pure logic (`xhci_link.c`, host suite
    `test_link`) with hot and warm reset and SS.Inactive, Compliance and CAS
    recovery, wired into `hcd_enum.c`.
  - 29-A.3: SuperSpeed enumeration - EP0 at 512 bytes, the BOS read, the
    SuperSpeed and SuperSpeedPlus companions in the Endpoint Contexts, the
    24-bit Max ESIT Payload.
  - 29-A.4: the 512-byte EP0 in the transfer engine.
  - 29-A.5: the passive fallback counted, the active hold as pure logic only.
  - 29-A.6: the SuperSpeedPlus isochronous companions, refused above three
    bursts without `LEC`.
  - The raw PSIV goes to the Slot Context only; the decoded class drives the
    endpoint rules. Counter block 48 fields.
- **`017048c`, Codex round 1**: SuperSpeed and SuperSpeedPlus isochronous
  endpoints above one page per interval refused at admission and counted; a
  FAILED link machine fed through disconnect so warm recovery re-enumerates,
  and the warm-reset budget refilled on unplug; truncated serials never
  identify a held device; section 10 corrections.
- **`0911520`, Codex round 2**: the USB 2.0 fallback BOS probe removed, so
  USB 2.0 enumeration traffic is Phase 26's again; the four-burst TBC bound on
  SuperSpeedPlus and `LEC` isochronous admission; a converted reset counted
  from the observed `WRC`.
- **`6fceeec`, 29-A.5's active hold wired**: `HcdHoldRequestUsb2` queues a
  send-back for a device on a SuperSpeed root port with a USB 2.0 companion;
  the thread reads the identity on the SuperSpeed port (VID, PID, serial),
  begins the hold (paired, unidentified or orphan), writes `PED` so the link
  goes to SS.Disabled and feeds the port a disconnect; an identified hold is
  released only when the held device is seen on the companion and leaves,
  re-armed with `PLS` = RxDetect and `LWS`, never a warm reset; holds end at
  the controller's next start. Seven hold counters (55 fields).
- **The integration's Codex rounds on the hold and the root port**:
  `81d3942` (a late hold refusal; root-port warm recovery abandoned when the
  disconnect's teardown handed the controller to recovery), `2943dfe` (a
  pending hold whose `PORTSC` reads all ones retried, then recovery after 50
  passes; `hcdHoldResolve` the one way out of Present-with-no-PDO),
  `3c56beb` (the hold committed only after a readable `PORTSC` and the `PED`
  write; failed PDO recreations counted, three tries), `bf4612c` (the hold
  revalidated against a fresh `PORTSC` before commit), `c0f9ad6` (an
  unreadable root `PORTSC` never drops pending port work).

**SuperSpeed isochronous (29-A.4) and SuperSpeedPlus (29-A.6)** are built
from the specification against host vectors and have no vehicle: QEMU models
neither, and no SuperSpeed isochronous device or SuperSpeedPlus link is held.
They are untested ground in the release notes and no clause of this
checkpoint. Which host vectors of 29-A.1 and 29-A.6's lists exist on the
merged build, by name: TBD.

---

## 29-V.1 - QEMU's `usb-storage` at SuperSpeed on both primaries

The clause: enumerated, bound to each target's `usbstor.sys`, a verified file
round trip; the USB 2.0 rows unchanged. The checkpoint's witness that a
transfer ran at SuperSpeed is `XHCISNAP`'s decoded port speed and the slot's
speed field; Device Manager's speed text is not a clause.

### Readings taken before the merged build

**Windows 2000 SP4**, `out\phase29\v1-2k-notes.md`:
- On `pkg-s1` (`p28-31-int` at `8e0e3bd`; `xhci98.sys` SHA-256
  `e35c8532...7768`), overlay `vm\t26\win2k-s29k.qcow2`:
  - Row 1: the controller and root hub on the SuperSpeed launcher, two
    protocol groups (USB 3 at xHCI ports 1-4, USB 2.0 at 5-8), eight managed
    ports powered.
  - Row 2: `usb-storage` at 5000 Mb/s; trace `port reset,
    location/ok/speed=00010104`, `hcd: BOS, bytes/capabilities=00001602`,
    `port speed decoded - superspeed=00000001`, `slot context speed -
    superspeed=00000001`; "USB Mass Storage Device" on `USBSTOR.SYS`
    5.00.2195.6655; `fc /b` of `RAND.BIN` and of a written `W.BIN` "FC: no
    differences encountered"; a clean unplug.
  - The USB 2.0 row on Phase 26's launcher: 480 Mb/s, `port speed decoded -
    high speed`, `fc /b` clean.
  - Row 4: controller disable and enable with nothing attached, and a clean
    shutdown.
- Again on `pkg-s2` (`p28-31-int` at `28e9254`; `xhci98.sys`
  `66290516...2d1e`), `out\phase29\v1-98-notes.md`, "Windows 2000
  re-confirmation": `usb-storage` at 5000 Mb/s, `fc /b` clean, a clean
  unplug.

**Windows 98 SE under NUSB 3.3**, on `pkg-s2` (`28e9254`), overlay
`vm\t26\win98-s29n.qcow2`, `out\phase29\v1-98-notes.md`:
- Row 1: the controller and root hub working, the new binary in place.
- Row 2: `usb-storage` at 5000 Mb/s, the same trace lines as on Windows 2000;
  the Add New Hardware Wizard found NUSB's `USBSTOR.INF` with no disk prompt
  (the golden image had not met that id); `fc /b` of `RAND.BIN` and of a
  written `W.BIN` clean; a clean unplug.
- The USB 2.0 row on Phase 26's launcher: 480 Mb/s, `fc /b` clean.
- Row 4: the controller disabled (Code 22) and enabled, a clean shutdown.

No `XHCISNAP` report was taken in any of these legs; the SuperSpeed witness
was the trace's decoded port speed and slot speed. The "burst" value is not
printed at the trace's verbosity (`v1-2k-notes.md`, row 2).

### The reading on the merged build

Build: `c0d8a51`, the first merged build of `p28-31-int`
(`out\merged\pkg-c0d8a51\qemu`, x86 `xhci98.sys` SHA-256 `8e14104f...f2fd`).
Evidence directory: `out\phase29\v1-merged` (the subagent's `notes.md`, the
traces and screenshots), summarised in `out\phase28\primaries\report.md`.
Read on development host A, 2026-10-04. On Windows 2000 `xhci98.sys` was
copied in by hand, since the kit's `pnpctl` update matches nothing there,
so that guest ran the old INF's IMOD value, 500.

| Clause | Windows 98 SE, NUSB 3.3 | Windows 2000 SP4 |
|---|---|---|
| Controller and root hub on the SuperSpeed launcher, every port powered | PASS | PASS |
| `usb-storage` at 5000 Mb/s, enumerated | PASS | PASS |
| Port speed decoded SuperSpeed and slot speed SuperSpeed (trace) | PASS | PASS |
| `XHCISNAP`: the port's decoded speed and the slot's speed field | NOTE: this build's `.TXT` has no slot-speed field; the trace's `slot context speed - superspeed=00000001` stands as the witness (the field was added in `a60f6e5`) | NOTE: the same |
| Bound to the target's `usbstor.sys` | PASS | PASS |
| `fc /b` read round trip | PASS | PASS |
| Write and `fc /b` | PASS | PASS |
| Unplug | PASS | PASS |
| USB 2.0 row: `usb-storage` at 480 Mb/s, `fc /b` | PASS | PASS |
| USB 2.0 rows of Phase 27 unchanged (matrix or soak) | PASS on Package B: the HID, storage and hub matrix groups and a 10-cycle soak (`out\phase28\primaries\report.md`) | PASS on Package B: the same |
| Controller disable and enable | PASS | PASS |
| Shutdown | PASS | PASS |

Verdict: **29-V.1 passes on both primaries.** SuperSpeed storage passed
again on the other eight install legs (the NT guests on Package A,
`2f6030a`, Windows ME on Package B, `417199e`) and on Package B's ten
`release`-flavour legs (`runs/run-28.md`, 28-V.1, clause 14 and 8a).

---

## 29-E.1 - the bench

**Waits for the combined bench session (owner, 2026-10-03).** The two
SuperSpeed drives of `test-equipment.md` at rig position D on Windows 98 SE
and 32-bit Windows 7: round trips, throughput against the same unit behind a
USB 2.0 hub, warm reset and SS.Inactive recorded if they occur. Nothing of it
has been read.

## 29-E.2 - the SuperSpeedPlus reading

**Waits for the combined bench session (owner, 2026-10-03).** A reading, not
a clause: a Gen 2 device on a P14s Gen 1 connector that 29-0 finds reaching
10 Gbit/s under this driver on Windows 98 SE, if both are held; otherwise
recorded as not taken. 29-0 has not answered which connector that would be
(above).

### An informal reading, not 29-E.2

The owner's own runs on the ThinkPad P14s Gen 1, 2026-10-04, relayed to the
coordinator; no copy of the screenshots is held under `out\`. They are not a
checkpoint reading and not 29-E.2: the link is SuperSpeed Gen 1 (5 Gbit/s),
the device is the MSSU10 under `xhciuas.sys` (Phase 31's driver), and the
build is the informal release package of `1ed1ba6` (`out\informal-p14s-uas98`,
`README.txt` there; `xhci98.sys` SHA-256 `23772b1e...91fb`).

- **Windows 98 SE with NUSB, UAS at SuperSpeed**, ATTO Disk Benchmark,
  Direct I/O: about 205 to 217 MB/s write and 198 to 224 MB/s read from 512 KB
  transfers up, against about 34 MB/s for `1.2.0.0` at High Speed on the same
  drive (the README on branch `readme-usb3`, `0320d59`, and its image
  `images/xhci98-flash-speed-test.jpg`). The interrupt moderation value in
  force for this run: TBD (`runs/run-28.md`, "The interrupt moderation
  default: 160", has the owner's sweep).
- **Windows 11 on the same machine** at 5 Gbit/s, the same drive (whether
  on the same connector: TBD): 389/428 MB/s write/read at 8 MB at queue
  depth 1 (ATTO "Neither"), and about 420/466 MB/s write/read from 512 KB up at queue depth 4
  (overlapped).
- What the gap is: Windows 98's path runs one command at a time, and above 64
  KB NUSB's `USBNTMAP.SYS` moves I/O through a 64 KB bounce buffer (the static
  reading in `runs/run-31.md`, "USBNTMAP.SYS: what is known and how"); that
  this is the ceiling is a hypothesis, not a measurement.
- `XHCISNAP` was in the package; a report from this run: TBD.

---

## Codex

Phase 29's own rounds are in `017048c` and `0911520`; the hold and root-port
findings of the integration's rounds 1 to 5 are listed under 29-A above
(`.claude\codex-p2831-r1-result.txt` to `-r9-`, not tracked; rounds 8 and 9
found no MAJOR or MINOR). A review of the merged build: TBD.

Closed: TBD.
