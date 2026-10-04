# Phase 28 Record - The seven other guests, the amd64 build, and the bench

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 28 - The Seven
Other Guests, the amd64 Build, and the Bench". The roadmap entry carries the
goal, the status, the task list and the checkpoint; this file carries what
each task did and what each reading said. Where the two disagree about a
clause, the roadmap wins.

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
development host A under QEMU 11.1.0 (TCG; WHPX left the XP SP3 guest at
"Windows is starting up..." for over 30 minutes, `out\phase28\pre\STATUS.md`),
each on a fresh qcow2 overlay over a read-only base: `vm\t28pre\*-base.qcow2`
for the pre-read, `vm\t27\winxp64-base.qcow2` for XP x64. Installs and
disable/enable went through `pnpctl`, a scratch SetupDi tool run in an
elevated command prompt on the transfer drive; devices were QEMU's,
hot-plugged from the monitor (mouse on port 1, stick on port 2, `usb-hub` on
port 3 with a mouse at 3.1, a stick at 3.2 and audio at 3.3, audio on port
4). Vista x64 and Windows 7 x64 boot through Advanced Boot Options, "Disable
Driver Signature Enforcement", on every boot. The merged build's legs: TBD.

**Whose read.** The pre-read section is the pre-read subagent's reports as
written (`out\phase28\pre\<guest>\report.md`), not re-read line by line by the
coordinator or by the docs subagent that drafted this file.

Opened 2026-10-04 at Phase 27's close: branch `phase-28` from `2.0.0.0` at
`78ade37`. The work itself ran ahead on `p28-a1` and `p28-31-int`.

---

## 28-A.1 - the per-target deltas for NT 5.1 onward

Built on branch `p28-a1` and merged into `p28-31-int` (`5fdb9af`):

- **`39d6105`**: `USB_BUS_INTERFACE_USBDI` versions 0 to 3 answered, their
  layout asserted against design record 13 section 6.1's sizes and, on amd64,
  against the WDK's own structures; `GET_DEVICE_HANDLE`;
  `SUBMIT_IDLE_NOTIFICATION` held with a cancel routine and never called back
  (there is no selective-suspend policy - 28.3 below), flushed at stop and
  removal; `GET_TOPOLOGY_ADDRESS`; `RECORD_FAILURE`; URB functions 0x30
  `SYNC_RESET_PIPE` and 0x31 `SYNC_CLEAR_STALL` through a split reset
  (`XhciPipeResetParts`). `GET_MS_FEATURE_DESCRIPTOR`,
  `CONTROL_TRANSFER_EX` and the Windows 8 functions are refused as the XP and
  2000 `usbport.sys` refuse them. Host suite `test_xp_requests` in
  `test_pipe`.
- **`fe8480b`**, the Codex review's fixes: `SYNC_RESET_PIPE` takes its own
  path (`hcdCfgResetHost`) - a busy pipe answered `ERROR_BUSY`, then Reset
  Endpoint with TSP=1 and Set TR Dequeue, never `CLEAR_FEATURE(ENDPOINT_HALT)`
  or a recreated context; the held idle IRP counted until its completion
  returns, and drained at quiesce and deletion; the USBDI `BusContext` a slot
  in an image-lifetime table, a gone slot answering not-connected, released at
  PDO deletion (Windows 7's `usbaudio.sys` 6.1.7601.17514 never calls
  `InterfaceDereference`, a static read in the commit message, so retaining
  the PDO or failing QUERY_REMOVE would leak or pin every audio device);
  `QueryBusTimeEx` answers `STATUS_NOT_SUPPORTED`.
- **`196c3c3`**, at the integration: `GET_TOPOLOGY_ADDRESS` fills the hub
  chain from the Route String, since a device PDO's port is its location from
  Phase 27 on.

**On a guest.** Windows XP x64 SP2, the `qemu` flavour of `p28-a1` at
`fe8480b` (`xhci98.sys` 254,464 bytes, SHA-256 `a7731448...6200`): the
composite `usb-audio` device that showed Code 10 without 28-A.1 read "This
device is working properly.", the trace `hcd: USBDI interface asked,
version/size=00000040` answered, and a 20 s 440 Hz tone played in real time,
17.49 s of player position in 17.30 s of host time (`runs/run-27.md`, "28-A.2
on Windows XP x64", leg q2; notes `out\phase27\xp64\q1-notes.md`). Only the
guest's position was read; the guest has no audio path out.

**Still open for 28-A.1:**
- Windows ME under SweetLow's stack with its own `usbccgp.sys` left unused:
  TBD. The ME pre-read row reads "NOT CHECKED - no composite device plugged".
- Every NT guest of the pre-read logs `QUERY_INTERFACE not answered` once per
  boot for GUID Data1 `4747B320` and `496B8280`
  (`GUID_BUS_INTERFACE_STANDARD`); nothing failed for it (each pre-read
  `report.md`, "Findings"). Whether 28-A.1 answers it or records it as not
  needed: TBD.

## 28-A.2 - the amd64 build

Moved to the start of Phase 27 by the owner's decision of 2026-10-03 and
recorded there: `dff49ae` readied the amd64 HCD for a guest, and Windows XP
x64 SP2 passed the clauses on the `qemu` flavour (q1) and on the `release`
flavour (r1, `xhci98.sys` 124,928 bytes, SHA-256 `cab1db08...0111`)
(`runs/run-27.md`, "28-A.2 on Windows XP x64"). With 28-A.1 the same guest
bound and played audio (q2, above). `3c2bd27` added `xhciuas.sys` for amd64
(its own allowlist and `xhciuas-amd64.inf`), which is Phase 31's
(`runs/run-31.md`).

---

## 28-V.1 - the seven other guests

The clause: ME, XP SP3, XP x64, Vista x86 and x64, 7 x86 and x64, each the
same clauses as 26-V and 27-V, Vista and 7 at four virtual processors, Vista
x64 and Windows 7 x64 on an F8 boot. With the three Windows 98 SE and Windows
2000 legs of Phases 26 and 27 these are the ten install legs.

### The pre-read on `1ed1ba6` (not the reading)

Taken on development host A on 2026-10-04 on `p28-31-int` at `1ed1ba6`,
`qemu` flavour (packages `out\phase28\pre\pkg-1ed1ba6\x86`, `xhci98.sys`
SHA-256 `d7c5c281...68a5`, and `...\amd64`, `d7ae21fd...6a9d`; full hashes in
`SHA256SUMS.txt` beside them), and paused at about 10:20 on the
coordinator's instruction so that every guest is read once, on the merged
build. It is an early look for target-specific defects, not 28-V.1's reading.
Evidence: `out\phase28\pre\STATUS.md` and `out\phase28\pre\<guest>\report.md`,
screenshots and traces beside each report.

| Guest | Install | HID | Storage `fc` | Unplug/replug | Hub + devices behind | Audio (bind) | Root hub dis/en | Controller dis/en | Shutdown | Soak x10 |
|---|---|---|---|---|---|---|---|---|---|---|
| Windows 7 x86 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| XP SP3 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | not taken |
| XP x64 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Windows 7 x64 (F8) | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | not taken |
| ME (SweetLow) | PASS | PASS | PASS | PASS | PASS | not taken | PASS | not taken | PASS | not taken |
| Vista x86 | not taken | | | | | | | | | |
| Vista x64 | not taken | | | | | | | | | |

- **No HCD defect** on any guest taken: no bugcheck, no hang, no `not served`
  or `refused at dispatch` line, no nonzero fatal or gave-up counter.
- **Audio** is binding only, at a root port and behind the hub; no playback
  was attempted. The ME audio unplug and audio-behind-hub rows were deferred
  for the Windows 98 family's removal defect then being fixed on
  `p28-pdoleak`.
- **The Windows 7 x86 controller disable** with devices attached: Code 22,
  the root hub gone, then enable with a second No Op self-test and the
  devices back; "the 1.2.0.0 Win7 disable hang did not reproduce in QEMU"
  (`out\phase28\pre\w7x86\report.md`). The miniport's VMs did not show it
  either (release notes, "Known limitations"), so this says nothing about the
  E460; see 28.3.
- **Soak**: Windows 7 x86 and XP x64, hid, hubmouse and storage 10 cycles
  each, 30 of 30, +10 addressed per class, the settled gap 2 stable
  (`w7x86\soak2\soak-w7x86.txt`, `xp64\soak2\soak-xp64.txt`).
- **Vehicle notes, not HCD defects** (`STATUS.md`): `soak-11v.ps1` read an
  empty bus as an incomplete reply (a keep-alive keyboard on port 8 cured it)
  and had no `-Arch` (it read XP x64's counters as 32-bit; patched locally);
  the packager self-test's repository-root refusal fails on a long worktree
  path; Windows ME's warm restart wedges at its logo, so it is relaunched
  cold; XP SP3 under WHPX hung at boot. The harness changes are on branch
  `p28-v1pre` (`2527746`); whether they are merged: TBD.
- ME showed one "Unknown Device" under Other devices, not identified (the
  base image's, possibly): TBD on the rerun.

### The reading on the merged build, `qemu` flavour

Build: TBD (commit, `xhci98.sys` SHA-256 for x86 and amd64, `xhciuas.sys`
likewise). Evidence directory: TBD.

| Clause | ME (SweetLow) | XP SP3 | XP x64 | Vista x86 | Vista x64 (F8) | 7 x86 | 7 x64 (F8) |
|---|---|---|---|---|---|---|---|
| Install from clean, prompts as recorded | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Controller and root hub working, No Op self-test | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| HID mouse on a root port | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Storage, `fc /b` read and write round trip | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Unplug and replug | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| `usb-hub` with a mouse and a stick behind it | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Composite audio bound, root port and behind the hub | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Audio unplugged | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Hub pulled with devices beneath | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Root hub disable and enable | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Controller disable and enable | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Advanced tab and Power tab | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| `XHCISNAP` report | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Shutdown | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Soak, 10 cycles per class | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| 28-A.1: ME's own `usbccgp.sys` left unused | TBD(merged build) | n/a | n/a | n/a | n/a | n/a | n/a |

### The ten install legs on the `release` flavour

The checkpoint reads the ten install legs on the `qemu` build and then on the
`release` flavour. Release package: TBD (commit, hashes).

| Install leg | `release` package installed | Controller and root hub working | HID, storage `fc`, audio bound | Controller disable and enable | Shutdown |
|---|---|---|---|---|---|
| Windows 98 SE, NUSB 3.3 | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Windows 98 SE, SweetLow | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Windows 2000 SP4 | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Windows ME, SweetLow | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| XP SP3 | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| XP x64 | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Vista x86 | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| Vista x64 (F8) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| 7 x86 | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |
| 7 x64 (F8) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) | TBD(merged build) |

---

## 28-E.1 - the bench

**Waits for the combined bench session (owner, 2026-10-03).** Nothing of it
has been read. Its parts, as the roadmap lists them:
- the E460 on Windows 98 SE and on 32-bit Windows 7, and the second Windows
  98 SE machine, through `run-13e.md`'s stage list: install, HID, storage,
  Ethernet, audio played and heard on a root port and behind a hub (Phase
  27's playback clause), five hot-plugs, and the controller disable that hung
  Windows 7 under the miniport - waits for the combined bench session (owner,
  2026-10-03);
- Phase 27's High-Speed hub clauses: the hub rig at positions H1 to H4 with
  the single-TT and multi-TT units of `test-equipment.md`, Low and Full Speed
  devices behind each, and the Full-Speed-hub-behind-High-Speed-hub clause if
  a specimen is held, otherwise untested ground - waits for the combined bench
  session (owner, 2026-10-03);
- Phase 27's Low-Speed mouse at `bInterval` 10 polled every 8 ms on a root
  port and behind a hub, moved to this bench by the owner's decision of
  2026-10-04 because no QEMU model is Low Speed (its host vectors are
  `test_hub`'s `test_low_speed_mouse`) - waits for the combined bench session
  (owner, 2026-10-03).

---

## 28.3 - the limitations of `1.2.0.0` under the HCD

Each limitation is to be written down as gone, carried or new. The list as
known on 2026-10-04:

| Limitation | Under `1.2.0.0` | What has been read under the HCD | Verdict |
|---|---|---|---|
| The NUSB stop crash | Stopping a running controller on Windows 98 under NUSB 3.3 crashes (`fatal exception 0E at 0028:C00312EE`, the same with Microsoft's `usbehci.sys`); it is NUSB's `usbport.sys`, which the HCD replaces | In QEMU, Windows 98 SE under NUSB 3.3: the controller's disable, enable, remove and rescan clean on `a7ddbfa` (`runs/run-26.md`, "The Windows 98 door sequence"); disable and enable clean with a hub subtree beneath it on `981f56b` (`runs/run-27.md`, "The orderly path with a hub subtree beneath it"). Physical machine: none | TBD |
| The idle that never sleeps | The controller never idles; the driver tells Windows so as it registers | The bus initiates no suspend: the owner's decision of 2026-10-04 on 27-A.1 ("handle, not initiate", `runs/run-27.md`, "The hub-port resume"); `SUBMIT_IDLE_NOTIFICATION` is held and never called back (28-A.1) | Carried, by the owner's ruling (as relayed by the coordinator on 2026-10-04; where the ruling itself is recorded: TBD) |
| The Windows 98 churn wedge | Plugging and unplugging a device every 0.6 s for minutes froze Windows 98 (the miniport's own defect, at 12 and 18 enumerations) | In QEMU, Windows 98 SE under NUSB 3.3: the 120-hub churn with storage resident enumerated 120 of 120 and the guest stayed responsive, on `f99f184` (`soak-h98f`) and `09ed9d1` (`soak-h98j`) (`runs/run-27.md`, "The soak") | TBD |
| The Windows 7 disable hang | On the E460, 32-bit Windows 7, the first controller disable never finished (2026-09-19); VMs did not show it | Not reproduced in QEMU on Windows 7 x86 in the pre-read (above); the miniport's VMs did not show it either. The E460 re-measure is 28-E.1's | Waits for the combined bench session (owner, 2026-10-03) |
| The Windows 98 audio-load wedge | The miniport had it: an intermittent wedge on an audio replug after a cold boot (`1.1.0.0` 2 of 10, `1.1.1.0` 5 of 10; `lessons.md`) | Seen on every HCD build tried, timing-dependent (5 of 6 at about 40 s after boot, 0 of 20 after 120 s), every IRP to the function PDO completed and nothing outstanding at the HCD (`runs/run-27.md`, "The Windows 98 SE audio-load wedge") | Carried: pre-existing, above the HCD |
| Windows 2000: an audio device unplugged during playback gets no REMOVE | Not on the `1.2.0.0` list | SURPRISE_REMOVAL and ABORT_PIPE, then no REMOVE within minutes, the desktop responsive; the same on Phase 26's `a7ddbfa`; an idle audio unplug is clean (`runs/run-27.md`, "Windows 2000: the audio device unplugged during playback") | Carried to this list by Phase 27; new against `1.2.0.0`'s list or not: TBD |

---

## The interrupt moderation default: 160

**The ruling.** The HCD's INFs write `XhciImodInterval250ns` = 160 (40 us) on
every install path of both packages (owner, 2026-10-04, as the `p28-imod`
working tree states it), where the miniport's wrote 500 (125 us) from
`1.1.1.0`.

**The evidence**, the owner's ATTO Disk Benchmark readings on the ThinkPad
P14s Gen 1, Windows 98 SE with NUSB, the MSSU10 under `xhciuas.sys` at
SuperSpeed, I/O "Neither" (queue depth 1), the informal release package of
`1ed1ba6` (`out\informal-p14s-uas98`), relayed to the coordinator on
2026-10-04 (no copy of the screenshots is held under `out\`):

| `XhciImodInterval250ns` | 0.5 KB write/read | 64 KB write/read | 8 MB write/read |
|---|---|---|---|
| 500 (125 us, the INF's value until the ruling) | 2.89 MB/s (one figure) | 181/182 MB/s | 181/181 MB/s |
| 160 (40 us) | 2.84 MB/s (one figure) | 203/209 MB/s | 211/221 MB/s |
| 40 (10 us) | 2.84/2.87 MB/s | 208/214 MB/s | 217/225 MB/s |

- At 8 MB, 160 reads 17 to 22% above 500; 40 adds only 2 to 3% over 160,
  near noise, for up to four times the interrupt rate.
- Small commands do not move with the value (about 175 us a command at
  0.5 KB under all three), so that overhead is elsewhere; unmeasured.
- QEMU models no interrupt moderation, so no guest reading can stand in for
  this (`lessons.md`; task 23.2).

**The change** is on branch `p28-imod`, uncommitted when this was drafted:
both INFs, `src\xhci.h`'s comment, the INF gate's expected footprints and
self-tests, `scripts\bench\IMOD.BAT` and `IMOD98.BAT`, and the release notes,
README, acceptance test and build-and-test text. Its commit: TBD. The
`p28-imod` comment says a Full-Speed audio stream was read again at 160 on
Windows 2000 before the change; the evidence for that reading: TBD.

---

## Codex

The Phases 28-31 integration was reviewed in nine rounds over `p28-31-int`
(`.claude\codex-p2831-r1-result.txt` to `-r9-`, not tracked): rounds 1 to 7
taken in `81d3942`, `2943dfe`, `3c56beb`, `bf4612c`, `c0f9ad6`, `d05990a`
and `28e9254`; round 8 over `28e9254` found no MAJOR or MINOR; round 9 over
the `p27-fix` merge `239d23a` found no MAJOR or MINOR and one NOTE. 28-A.1's
own review is in `fe8480b`. A review of `1ed1ba6` (an INF and gate change)
and of the merged build: TBD.

Closed: TBD.
