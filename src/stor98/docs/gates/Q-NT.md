<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- Copyright (c) 2026, Yeo Kheng Meng -->

# Qualification before dependent product code: NT side (Q-NT)

> Prototype source is not on `main`. Branch `qual-nt` (worktree `nvme98-wt-q1`): 759d860 (G1/G8)
> merged with `phase0-g2g4` (df7aed4) and `phase0-g5g7` (53be672), plus the qualification layer
> `src/q1.c` (ac54a22). Test builds: `NVME98_DBGPORT=1 NVME98_G57=1 scripts\build.cmd all free`.

PLAN "Qualification before dependent product code" (R6), NT items:

1. Windows 7 x64 runs of G2, G4, G5 and G7.
2. Combined G1 + G7: resume failure with the deferred bring-up and `ScsiRestartAdapter` not
   advertised (R6-7), on 2000, XP x64 and 7 x64.
3. Windows 2000 two-SHUTDOWN sequence with CC.SHN written on each SHUTDOWN (R6-5).
4. Flush and SHST latency against the stop-callback budget (R6-3).
5. The port's `SRB_STATUS_BUSY` retry limit with a never-ready controller (G1 open item).

Date: 2026-10-15. usb4-emu: `src/qemu/build` binary of 2026-10-15 01:42, copied to
`vms/nvme98-q1/bin`. Windows 2000 SP4 (i440FX, `acpi=off`, TCG); XP x64 SP2 (i440FX, TCG);
Windows 7 x64 SP1 (q35, `usb4-tunnel-port`; WHPX for G2, G4, G5, item 4 disable and item 5;
TCG for hibernate and resume, which does not work under WHPX, see "Harness notes").

## Summary of verdicts

| Item | 2000 | XP x64 | 7 x64 |
|---|---|---|---|
| 1. G2 gone-state hold | (pass, phase 0.0) | (pass, phase 0.0) | **pass** |
| 1. G4 port after a failed reset | (pass) | (pass) | **pass** (failed disable; stuck controller not run) |
| 1. G5 memory lifetime at stop | (fail) | (fail) | **fail** for removal (port frees and reuses at once); driver-owned contiguous pool intact |
| 1. G7 resume path, no `ScsiRestartAdapter` | (partial) | (partial) | **pass** (resume path and allocations) |
| 2. Resume failure, deferred bring-up | **pass** | **pass** | **pass** |
| 3. Restartable SHUTDOWN with CC.SHN | **pass** (two SHUTDOWNs, re-enable between, data verified) | pass (one SHUTDOWN) | pass (one SHUTDOWN) |
| 4. Flush / SHST within the stop budget | OS flushes before every stop seen: stop is clean | **Flush not within budget** on disable (2.1-18.9 ms) | **Flush not within budget** on disable (5.8-17.1 ms) |
| 5. BUSY retry limit | **21 BUSY per request**, scan INQUIRY 3 times (63) | same | same |

With items 1, 2 and 7 x64's G7 done, G7 is a full pass on 2000, XP x64 and 7 x64 (resume
path, allocation reuse and resume-failure semantics). G5 stays a fail on all three NT ports
(MD-3 open). Item 4 measured the latency. The decision it feeds (R6-3 options) is still open.

## Prototype (what ran)

- **Merge.** G1/G8 deferred bring-up (`HwInitialize` writes CC.EN, `BringupTimer` finishes,
  hybrid hold-then-BUSY) is first in `HwStartIo`. The G2/G4 gone/failed gate follows it.
  `HwResetBus` uses the G2/G4 truthful reset once running, and the G1 behaviour (nothing
  issued) before that. `HwAdapterControl` advertises `ScsiStopAdapter` and
  `ScsiSetRunningConfig` only (G7). `ScsiStopAdapter` runs the G2/G4 no-hardware stop when
  not running, else the `stop=q` measuring stop (below), else the G5/G7 bounded stop.
  `HwFoundAdapter` now re-carves the queues after it re-zeroes the uncached extension.
  A resume re-runs it on a device extension that is not zeroed.
- **Stage codes, one table** (`src/dbgport.h`): shared layer 0x01-0x7F; G2/G4 0x80-0x8E;
  G5/G7 0x90-0x99 (renumbered from 0x80-0x89, which collided with G2/G4); qualification
  0xA0-0xB4. G9 (9x) keeps its own `g9dbg.h` table on `phase0-g9`. Decoder: usb4-emu
  `vms/nvme98-q1/q1log.py` (local).
- **Qualification layer `src/q1.c`** (DriverParameter tokens):
  - `shn=1`: restartable shutdown notification (PLAN 0.11 class 2). `SRB_FUNCTION_SHUTDOWN`
    gets an NVMe Flush, then CC.SHN=01b. The SRB pends until SHST=10b, polled by a 10 ms
    timer (limit 5 s, logged as not clean). The state is then `SHUTDOWN` with every resource
    kept. The next request of any other kind re-enables: CC.EN=0, RDY=0 waited by the
    bring-up timer, then the full G1 bring-up (queues re-created, identify). The request is
    held meanwhile. A SHUTDOWN or FLUSH while already shut down completes at once.
  - Conservative dirty tracking (PLAN 0.10 durability at stop). A write submission makes
    the adapter dirty. Only a Flush submitted after every tracked write, and completed
    successfully, makes it clean.
  - `stop=q`: `ScsiStopAdapter` submits an NVMe Flush if dirty, then CC.SHN, then CC.EN=0.
    Each wait is checked against the stall budget (10 x 100 us), then, for measurement
    only, polled on for up to 3 s.
  - `rdyto=<ms>`: CAP.TO override for the bring-up waits (0 = never), and every BUSY
    returned while starting is logged.

## Setup

usb4-emu `vms/nvme98-q1/` (local, git-ignored): launcher `run.sh` (2000 QMP 48150, XP x64
QMP 48152, 7 x64 QMP 48154 with VNC 127.0.0.1:47850), `mkov.sh`, `q1log.py`, guest
scripts `xfer-x86/`, `xfer-amd64/` (`i.cmd`, `w.cmd`/`v.cmd` write and verify known 4 MiB
+ 1 MiB files, `*.reg` option sets), the G5/G7 agent's `g57dev.exe` and the G2/G4 agent's
`gio.exe`. Decoded evidence is in `vms/nvme98-q1/evidence/`; the files under 40 KB are
also committed in [evidence/q-nt/](evidence/q-nt/). Screenshots are in
`vms/nvme98-q1/shots/`. Raw traces and disk images: D: filled up during the run, so they
went first to the session scratchpad on C:, and after the maintainer's request to
`E:\nvme98-vm\q1\`.

Images are new overlays only. 2000 and XP x64 are on the G1/G8 agent's overlays
(`vms/nvme98-g18/{w2k,xp}.qcow2`, 2000 through a copy). 7 x64 is on
`vms/nvme98-g18/w7.qcow2` (test-signing already on), used read-only as backing. The test
build replaced `system32\drivers\nvme98.sys`.

The 7 x64 image was signed with a throwaway self-signed certificate made with openssl in
the scratchpad (CN "THROWAWAY nvme98 qual-nt agent test", SHA-1 certificate signature,
SHA-1 file digest, no timestamp). A first certificate signed with SHA-256 gave Code 52:
Windows 7 SP1 code integrity does not take a SHA-256-signed certificate. Nothing was
imported into any certificate store on the host or in the guest. The two key containers
SignTool created were deleted right after each signing. Key, certificate and signed
copies are deleted at the end of the session.

Times are VM time from the debug-port trace (`usb4_dbg_stage vm_ns`), in ms relative to
the excerpt, unless stated.

## Item 1: Windows 7 x64 runs of G2, G4, G5, G7

### G2 (gone-state hold): pass

Workload: `gio 2 r 8 64` (unbuffered overlapped reads, QD 8, 64 KiB on PhysicalDrive2).
The harness held read fetches 500-507 and took the endpoint away on 507, so 8 SRBs were
held at T0. Options `gone=5000 st=nodev bcd=1`, or `gone=0`.

| Run | Stimulus | What the port did | Evidence |
|---|---|---|---|
| A | `act=surprise` (tunnel down; native PCIe hot-plug on q35) | The OS surprise-removed the controller at once. **The port completed the 8 held SRBs itself** (gio: error 2 on all 8 within one ms, `GONE open err 2`). No `ScsiStopAdapter`; the miniport timer stopped (no tick after `G24_GONE_ENTER`). This is removal established by the OS, which the pass condition allows | `w7-g2-md1.txt`, `shots/w7-g2a.png` |
| B | `act=gate` (gate closed, DMA closed, no stage 2-4, so no hot-plug event) | port-owned 8/8 every second, no timeout, no reset. MD-1 deadline: 8 x `HELD_DONE` NO_DEVICE, `DEADLINE done=8`, `BusChangeDetected`. gio got error 2 on all 8, and the PhysicalDrive was gone 1 s later. Two INQUIRYs from the rescan were fast-failed | `w7-g2-md1-gate.txt`, `shots/w7-g2b.png` |
| C | `act=gate`, `gone=0` | port-owned 8/8. **One `HwResetBus` about 221 s (host) after `gone`**, state GONE, FALSE, no waits. Then nothing for 4.5 min: no second reset, no completion. A later `tunnel surprise` (PnP removal): the port completed the 8 SRBs itself (error 2); no stop callback | `w7-g2-g4-nodeadline.txt`, `shots/w7-g2n-end.png` |

The deadline counts 50 ticks of 100 ms. Under WHPX with a loaded host it ended 12.7 s
(host) after `gone`, and gio's own clock showed the error 6.7 s after its start. In run C
the guest clock ran at about 0.58 x host time (gio: 290 s blocked, about 500 s host). That
is more evidence for PLAN's rule that deadlines are elapsed time, not tick counts. On this
setup even the guest's own clock lagged the host.

### G4 (port after a failed reset): pass

`x-rdy-clear-delay-ms=-1` and one read completion held (`at=complete op=read nth=300
act=hold`), with gio QD 8 running (`w7-g4-faileddisable.txt`, `shots/w7-g4-end.png`):

- `HwResetBus` #1: CC.EN=0 written, CSTS 0x01 after the budget, FALSE, `RESET_WAIT`. One
  queued READ reached the miniport and was failed with HARDWARE ERROR.
- `HwResetBus` #2 66 s (host) later: FALSE. Then nothing until the acknowledgement. LU
  dispatch stalled and gio blocked without errors; the port completed nothing itself.
- `x-rdy-clear-delay-ms=0`: the watchdog saw RDY=0 with CC.EN=0. The held SRB completed
  with `BUS_RESET` plus `ResetDetected` (`G24_RESET_ACK`), and the port dispatched the
  stalled requests. They failed with HARDWARE ERROR (the prototype stays `failed`) and gio
  got error 1117 on all 8, then kept getting fast errors. No double completion, no
  bugcheck.

Same behaviour as 2000 and XP x64. The stuck-controller (`x-hang`) variant was not run on 7.

### G5 (memory lifetime at stop): fail for port-owned memory

Device Manager disable (`g57dev disable`) with `x-rdy-clear-delay-ms=-1` and the G5/G7
bounded stop (`G57_STOP_DONE 0x100`: not acknowledged). The QEMU monitor read the reported
pages (`w7-g5-disable-rdynever.txt`, `w7-g5-memdump.txt`):

| Page (PA) | Right after the stop | 90 s later (after file activity) |
|---|---|---|
| SRB extension 0x7fc6a000 | **NTFS `RCRD` log record** | other data |
| uncached spare page 0x7fc69000 (held `G5ST`) | sentinel gone (zeroed page) | kernel pointers |
| I/O CQ 0x7fc67000 | not the CQ any more | `CScc` pool tag data |
| quarantine pool (`MmAllocateContiguousMemory`) 0x7fc1c000 | `G5QP` intact | intact |

The port frees and reuses the uncached extension and the SRB extensions after an
unacknowledged removal stop at once, as on 2000 and XP x64. Power stop: preserved (G7
below). Surprise removal with the DMA path open was not run on 7 (G5 already fails on the
orderly stop).

Consequence: MD-3 covers 7 x64 too. On 7 x64 the driver-owned contiguous pool survived the
stop and the reuse (host read of its page, 90 s after the stop). The image was unloaded:
the next enable logged a fresh `DriverEntry` (`G57_DRIVER_ENTRY 1`). The pool's page was
not re-read after that point.
Option (b) is therefore no longer untested on 7 x64 for survival, but it is still
unprototyped with operational queues.

### G7 (resume path without `ScsiRestartAdapter`): pass

TCG, hibernate (`shutdown /h`) with the merged prototype (`w7-q4-hibernate.txt`,
`w7-g7-q2-resumeA-normal.txt`, `shots/w7-resA.png`):

- Power-down: `ScsiStopAdapter` after SYNCHRONIZE CACHE and START STOP UNIT. Clean (no
  writes since the SYNC), CC.SHN, SHST and RDY=0 all within the budget.
- Resume: `HwFindAdapter` (`G57_FIND 0x20003`: second find, device extension kept and not
  zeroed) and then the deferred `HwInitialize`. `G57_UNCACHED_CHECK 0x10f`: same PA, same
  VA, sentinel kept. START STOP UNIT (TimeOutValue 240 s) was held during the bring-up and
  dispatched about 80 ms later. On 7 this came during the resume itself, about 24 s after
  power-on, before any user I/O; 2000 and XP x64 run it at the first I/O.
- No `HwAdapterControl` call at all on resume: neither `ScsiQuerySupportedControlTypes`
  nor `ScsiSetRunningConfig`.
- Files written before hibernate verified, and new writes worked.

## Item 2: resume failure with the deferred bring-up, no `ScsiRestartAdapter`: pass on all three

Each OS was hibernated with dirty data written just before. The hibernated overlays were
copied, so each variant resumed from the same state: (A) normal; (B) Identify Controller
failing (`at=complete op=identify repeat=on status=0x6`); (C) RDY never set
(`x-rdy-set-delay-ms=-1`). In every run `HwInitialize` returned TRUE (state `DISABLING`;
SeaBIOS leaves CC.EN=1). Then:

| | 2000 | XP x64 | 7 x64 |
|---|---|---|---|
| A normal | ok, data verified (`w2k-q2-resumeA-normal.txt`) | ok, verified (`xp-q2-resumeA-normal.txt`) | ok, verified |
| B identify fails | `BRINGUP_FAIL 3`, CC.EN=0, `DISABLE_ACK`, `FAILED` 104 ms after `HwInitialize`. The held START STOP UNIT completes ERROR with sense 04/44/00 | same, 96 ms | same, 108 ms |
| C RDY never set | `BRINGUP_FAIL 1` after CAP.TO (11.8 s with 10 ms nominal ticks), disable acknowledged, `FAILED`; START STOP UNIT completed ERROR | same, 12.2 s | same, 11.9 s |
| What the port and class driver did next (B and C) | START STOP UNIT and REQUEST SENSE re-sent 5 times about 300 ms apart (all failed at once), then the writes: each failed at once (`Delayed Write Failed`). Reads gave "I/O device error". The volume stayed mounted. 6 920 fast-failed SRBs in the first 48 s from lazy-writer retries, then none (the next 60 s: 0) | START STOP UNIT/REQUEST SENSE 5 times, reads and writes failed, then a rescan: 6 INQUIRYs failed, **the disk was removed** (F: gone: "cannot find the drive") | as XP x64 (START STOP twice), disk removed |
| Hang? | no | no | no |
| `HwResetBus`, `ScsiStopAdapter`, any `HwAdapterControl` | none | none | none |

The volume never hangs. Contrast G7's measurement: `HwInitialize` returning FALSE on resume
hung the I/O for more than 4 min on 2000 and XP x64. `ScsiAdapterControlUnsuccessful` is
never used, because `ScsiRestartAdapter` is not advertised. Evidence:
`{w2k,xp,w7}-q2-resume{B,C}-*.txt` (the 2000 ones are too large to commit; they are in
usb4-emu `vms/nvme98-q1/evidence/`), and screenshots `w2k-resB-2.png`, `xp-resB-1.png`,
`w7-resB.png`.

In failed state INQUIRY returned ERROR/HARDWARE ERROR (0x84), not SELECTION_TIMEOUT. The
prototype keys "no disk" on "identify never succeeded", and `IdentifyDone` survives
hibernate because the device extension is kept. XP x64 and 7 removed the disk anyway.

**Finding: `ArgumentString` is not passed at resume.** On all three ports the resume
`HwFindAdapter` saw no DriverParameter: `OPTIONS 0x2`, `Q_OPTIONS shn=0 stop=0`, G2/G4
defaults. Every option parsed from it fell back to its default, including the prototype's
`ConfigInfo->CachesData`, which follows `cd=1`. The device extension itself survives. A
product driver therefore must not derive `ConfigInfo` settings (`CachesData` above all)
from DriverParameter, and must keep any registry-derived tunable in the device extension
instead of re-parsing it when `ArgumentString` is NULL. Whether the port honoured the
changed `CachesData` after resume (that is, still sent SHUTDOWN/FLUSH) was not tested.

## Item 3: restartable SHUTDOWN with CC.SHN

### Windows 2000: pass

`shn=1`, `CachesData` TRUE. Two runs, Shut Down and Restart, each after writing 5 MiB of
known data to both NVMe volumes (F: FAT32, H:). Shut Down, `w2k-q3-shutdown1.txt`:

```
   0.0 ms  SYNC10 -> Flush, complete (Q_CLEAN)
   0.4 ms  SHUTDOWN #1 (untagged) -> NVMe Flush completes (depth 1, clean)
   0.6 ms  CC.SHN=01b written; state SHN_WAIT
   7.0 ms  SHST=10b at the first timer tick; SHUTDOWN #1 completed SUCCESS; state SHUTDOWN
  17.6 ms  READ10 arrives: Q_REENABLE: CC.EN=0 (SHN cleared), held
  38.0 ms  RDY=0 acknowledged -> CC.EN=1 -> RDY -> create CQ/SQ, identify
 114.7 ms  RUNNING; held READ dispatched; then READ, WRITE x3 (dirty), READ
 118.6 ms  SYNC10 -> Flush (clean)
 120.0 ms  SHUTDOWN #2 -> Flush, CC.SHN, SHST at the first tick (9.4 ms), SUCCESS; state SHUTDOWN
           nothing follows; "It is now safe to turn off your computer"
```

Restart showed the same sequence (`w2k-q3-restart.txt`; re-enable 96 ms; the second
SHUTDOWN was followed only by the reboot, `DRIVER_ENTRY`). After each boot,
`v.cmd` compared every file written before the shutdown with its source: all identical
(`shots/w2k-q3-verify1.png`, `w2k-q3-verify-restart.png`). Every request between the two
SHUTDOWNs completed with SUCCESS. The interrupt and timer were re-established by the
bring-up, and no resource was released or reallocated: the queues carved at the first
`HwInitialize` were reused.

### XP x64 and 7 x64 (one SHUTDOWN each)

`xp-q3-shutdown-shn.txt`, `w7-q3-shutdown-shn.txt` (7 under TCG): SYNC, then **one
SHUTDOWN SRB, which is tagged on both** (on 2000 it is untagged), then Flush (depth 1),
CC.SHN, SHST at the first tick, SUCCESS, and nothing after. The guest powered off.

Consequence for 0.11 class 2: the design works as written on all three ports. The product
must not rely on the SHUTDOWN SRB being untagged or alone. Here it was the only
outstanding request each time, but on XP x64 and 7 it is a tagged request.

Not tested: SHST slower than one tick (`x-shst-delay-ms`) during a SHUTDOWN (QEMU completes
SHST at once), and a SHUTDOWN arriving with writes still outstanding.

## Item 4: Flush and SHST latency against the stop budget

Budget: 10 polls of 100 us (under 1 ms) per callback (PLAN 0.10). Every number is the
emulator's: QEMU's NVMe Flush is an fsync of the qcow2 overlay on the host (NTFS on C: or
E:), and QEMU completes SHST and RDY=0 at once. **Real SSDs differ**: flush time depends
on the volatile cache contents, and SHST can take up to RTD3E (seconds). These figures
are lower bounds for shape, not predictions for hardware.

| Measurement | 2000 | XP x64 | 7 x64 |
|---|---|---|---|
| Runtime Flush (SYNCHRONIZE CACHE / `SRB_FUNCTION_FLUSH`, StartIo to completion) | 0.1-3.8 ms (median about 0.3-1.9) | 0.2-6.3 ms | 0.3-6.1 ms |
| Dirty at `ScsiStopAdapter`, Device Manager disable | **no**: SYNC + FLUSH 33 ms before the stop | **yes, every run** (writes, then the stop) | **yes, every run** |
| Stop-path Flush (`stop=q`), disable with dirty data | not needed | **2.1, 2.5, 18.9 ms**: never within budget | **5.8, 7.1, 17.1 ms** (WHPX); 0.8 and 0.9 ms (in budget) in two stops during a TCG boot (PnP reinstall) |
| Dirty at the hibernate stop | no (SYNC, FLUSH, START STOP UNIT, stop: 60 ms after the last flush) | no (SYNC, START STOP UNIT, stop: 36 ms) | no (SYNC, START STOP UNIT, stop: 15 ms) |
| CC.SHN to SHST complete, in the stop | 0 polls (first read) | 0 polls | 0 polls |
| CC.EN=0 to RDY=0, in the stop | 0 polls | 0 polls | 0 polls |
| CC.SHN to SHST complete, SHUTDOWN SRB (timer) | first tick (6-9 ms) | first tick (20 ms) | first tick (24 ms) |

Evidence: `{w2k,xp,w7}-q4-disable*.txt`, `*-q4-hibernate.txt`,
`w7-q4-tcg-boot-pnp-reinstall-stops.txt`.

**Does any OS give the miniport a chance to flush before `ScsiStopAdapter`?**

- Windows 2000: yes, for both disable and hibernate. It sends SYNCHRONIZE CACHE and
  `SRB_FUNCTION_FLUSH` before the stop.
- XP x64 and 7 x64: yes for hibernate (SYNCHRONIZE CACHE, then START STOP UNIT).
- XP x64 and 7 x64 Device Manager disable: **no** (as G8 found). Writes completed
  milliseconds before were still unflushed when `ScsiStopAdapter` arrived, in all six
  runs. START STOP UNIT (stop) is a usable early sign only for hibernate.

**What the dirty tracking implies.** Under the conservative rule the adapter is clean at
every stop that follows an OS flush (2000 always; XP x64 and 7 at hibernate) and at every
shutdown (the OS sends SYNC before SHUTDOWN). The stop path then needs only CC.SHN and
CC.EN=0, which fit the budget here. The adapter is dirty at the XP x64 and 7 disable
(and eject) stop, and only a Flush issued by the stop path covers it. That Flush did not
complete within 1 ms even in the emulator.

Plan consequence (R6-3): "a durability path within the stop budget" is **not
demonstrated, and on XP x64 and 7 disable it is refuted in the emulator**. The decision
between the R6-3 options has to be put to the maintainer. The measurement favours **idle
flush from the timer**, combined with **accept and document** for the residual window: it
is the only option that covers the XP x64 and 7 disable stop without stalling the
callback. The measurement build polled 2-19 ms in `ScsiStopAdapter` without visible
trouble, and the inherited stop path polls for seconds. That only shows the ports
tolerated it; the stop callback's IRQL is still unmeasured, so a longer bounded poll is
not established as acceptable.

## Item 5: the port's BUSY retry limit

Never-ready controller (`x-rdy-set-delay-ms=-1`) and `rdyto=0` (bring-up never times out).
Device Manager enable, so the first request is the bus-scan INQUIRY. Policies: `g1=busy`
(BUSY at once) and `g1=hybrid` (hold a quarter of TimeOutValue, then BUSY).

| | 2000 (INQUIRY 4 s) | XP x64 (10 s) | 7 x64 (60 s) |
|---|---|---|---|
| `g1=busy`: BUSY completions until the port stops sending | **63** = 3 x 21. Each group of 21 is one SRB issue; the next starts 1-6 ms after the last | 63 = 3 x 21 | 63 = 3 x 21 |
| Interval between retries of one SRB | 1.56 s | 1.0 s | 1.0-1.1 s |
| Total | 93 s | 62 s | 62 s |
| `g1=hybrid` | 63 (holds of 100 ticks, about 1.56 s each), 108 s | 63 (holds of 250 ticks, about 4.1 s each), 260 s | not run (about 15 s holds, would be about 16 min) |
| Port reset during the BUSY run | none | none | none |
| End | scan gives up: no disk, controller started, adapter still `WAIT_RDY` | same | same |

Evidence: `{w2k,xp,w7}-q5-busy-scan.txt`, `{w2k,xp}-q5-hybrid-scan.txt`.

**Answer:** each port re-issues an SRB that completes with `SRB_STATUS_BUSY` 20 times
(21 BUSY completions), then fails it to its owner. The limit is a count, not a time: hybrid
holds of 1.5-4 s give the same count. The bus scan issues INQUIRY 3 times. The G1 worst
case (5 in a row) was far from the limit. Not measured: what the disk class driver does
when a READ/WRITE fails after 21 BUSYs (its own retries come on top). That needs a running
disk with a never-ready re-enable, and `rdyto` cannot be set at resume (no
`ArgumentString`, item 2).

Plan consequence (0.3, hybrid hold-then-BUSY):

- One request can be carried for at most 21 x (hold + the port's retry delay).
- On 2000 with a quarter-TimeOutValue hold that is about 33 s per INQUIRY issue, and about
  100 s for the whole scan. That is **shorter than the largest CAP.TO (127.5 s)**.
- Lengthen the hold to just under the SRB's TimeOutValue minus a margin, measured as
  elapsed time: on 2000 about 3 s, giving about 95 s per issue.
- Do not complete BUSY more than 20 times for one SRB. Failing it is the alternative to
  the port failing it.
- The bring-up deadline remains CAP.TO; once it expires, the request completes in the
  failed state.

## Harness notes

- **D: filled up** during the run (0 bytes free at about 06:20). My VM was quit at once and
  its images checked (`qemu-img check` clean). From then on images and raw traces went to
  C: (scratchpad), and later to `E:\nvme98-vm\q1\` as the maintainer asked. Other agents'
  QEMU processes were not touched.
- **Windows 7 under WHPX**: boots and runs, but resume from hibernation does not (black
  screen, kernel running, no disk I/O). Hibernate runs used TCG. On the first WHPX boot
  of the G1/G8 image PnP reinstalls the driver from the DriverStore and asks for a restart.
  After that, the copy in `system32\drivers` is replaced from the DriverStore at every
  boot. The test build was loaded by disable/enable after copying. For the TCG hibernate
  runs the DriverStore copy was replaced too (takeown/icacls in the guest overlay).
- **Resume password**: 7 x64 locked the session at resume. The session was not unlocked
  with a password. Instead `powercfg ... CONSOLELOCK 0` was set in the guest overlay and
  the hibernate repeated. XP x64 likewise, with `powercfg /globalpowerflag off
  /option:resumepassword`.
- **Fault at power-on vs resume**: with Identify failing from power-on, SeaBIOS no longer
  lists the NVMe and Windows 7 discarded the hibernation and cold-booted. The fault was
  then set by QMP after POST. On XP x64 setting it after the desktop came too late: the
  resume itself had already touched the disk.

## Proposed PLAN changes (not applied here)

> Applied to PLAN.md on `main` (2026-10-15). Item 5 became maintainer decision MD-7
> (durability at orderly disable on XP x64 and 7), decided 2026-10-15 as this record
> recommended: idle flush from the timer plus accept-and-document.

1. **0.0 results / G7**: full pass on 2000, XP x64 and 7 x64 (resume path, allocations,
   failure semantics with the deferred bring-up). Remove "resume-failure handling is
   unverified" from 0.10, 0.11 and 2.5. Record that a resume failure leaves the volume
   failing fast (2000: stays mounted, errors; XP x64 and 7: disk removed after a rescan),
   with no reset and no stop.
2. **0.0 results / G2, G4 on 7 x64**: pass. Add to G2: on q35 with native PCIe hot-plug a
   surprise removal makes the OS remove the controller at once, and the port retires held
   SRBs itself before any gone deadline. MD-1's deadline matters there only when presence
   is lost without a hot-plug event (hung device, harness gate).
3. **0.0 results / G5 on 7 x64**: fail for removal, like 2000 and XP x64. MD-3 now covers
   7 x64. The contiguous pool survives on 7 x64 too.
4. **0.11 class 2 (restartable SHUTDOWN)**: demonstrated on 2000 (CC.SHN on each of two
   SHUTDOWNs, re-enable in about 100 ms, data verified) and on XP x64 and 7. The R6-5
   qualification item is closed. Note that SHUTDOWN is tagged on XP x64 and 7.
5. **0.10 durability at stop (R6-3)**: Flush within the budget is not achievable for the
   XP x64 and 7 disable stop, even in the emulator. Put the option choice to the
   maintainer, with idle flush plus accept-and-document as the measured recommendation.
   2000 and every hibernate stop are clean by OS flush.
6. **0.3 hybrid**: the port's BUSY limit is 20 retries (21 BUSY completions) per SRB on all
   three NT ports, count-based. The hold must use most of TimeOutValue (elapsed time) for
   long CAP.TO, and the driver should fail a request itself before the 21st BUSY.
7. **New rule (from item 2)**: `HwFindAdapter` gets no `ArgumentString` at resume on any NT
   port. `ConfigInfo` values (`CachesData`) must be constants, and tunables must persist in
   the device extension.
8. **TESTING, debug-port stage codes**: replace the per-branch table with the merged one
   above (shared 0x01-0x7F, G2/G4 0x80-0x8E, G5/G7 0x90-0x99, qualification 0xA0-0xB4;
   G9 separate until the 9x merge).

## Open

- G4 stuck-controller and G5 surprise-removal-with-DMA-open variants on 7 x64.
- Hybrid BUSY run on 7 x64 (15 s holds).
- Class-driver retries on top of the port's BUSY limit for READ/WRITE.
- Real-SSD Flush and SHST latency (P14s with an enclosure).
- `ScsiStopAdapter` IRQL.
- XP x86, Vista, 7 x86: no VM.
