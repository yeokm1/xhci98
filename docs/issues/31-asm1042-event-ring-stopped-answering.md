# Issue 31 - An early ASMedia ASM1042 "stopped answering" right after Windows started: the 1,024-entry event ring, and a recovery that could not wait out its reset

Status: **fixed for `3.5.0.0`** (task 49.35, branch `w49-rst`); **not yet read on the reporter's machine with
that build.** Two causes are kept apart below:

- The recovery's failure is established and fixed.
- That the smaller event ring cures the trigger is established. Whether the segment's size or its placement
  matters, and which escalation fired, is not known.

**Scope.** A controller that reports xHCI 0.96 (below 1.0), on every target. The report: an ASMedia ASM1042
(`1B21:1042`, revision 00, HCIVERSION 0.96, subsystem `1849:1042`, four ports, IRQ 19 level and shared, a
line interrupt). It sits beside an Intel 7 Series xHCI (`8086:1E31`) on an ASRock Z77 Extreme4 desktop board,
under Windows XP x64 SP2, with `3.4.3.0` release and logging level 3. It is the community reporter of issues
22, 24 and 26.

## 1. What was seen

The xHCI98 USB Tree's tray said "xHCI98 USB 3 Controller (ASMedia ASM1042) stopped answering" after each
restart. The Intel controller worked under the same driver. Three report sets:

- after the first restart following the install;
- after a second restart;
- after `XhciEventRingTrbs` 256 was set by hand on the ASMedia controller.

The first two snapshots were of the Intel controller (`/c 0`). Section 6 says why.

## 2. What the reports show

**The first two sets** show the same sequence in the ASMedia controller's note ring:

1. A normal start, ending with `iso.none`.
2. Then at once `ctrl.failed.here=1`: the first reset request, from a path that wrote nothing else in a release
   build.
3. Recovery 1 (`ctrl.recover.begin=1`) ran the re-initialization at DISPATCH_LEVEL, wrote HCRST, and refused
   at step 9, the reset, with USBSTS `00000001`: its 1 ms pause and 10 ms stall had run out with USBCMD.HCRST
   still set.
4. In the first set, recoveries 2 and 3 found the reset still running in the recovery's pre-check and refused
   before touching a register, each one charged. (The second set differs at recovery 3: item 6.)
5. All three began at tick 0 of the tolerance clock ("begun 1042, 1042, 1042 ticks ago"), and the controller
   was "LATCHED FAILED: three recoveries failed in a row".
6. In the second set the third attempt found the reset finished (`handoff.legctl.before=40000000`), wrote a
   fresh HCRST, and refused the same way. So this part's reset takes more than about 11 ms and well under a
   second.

PCI Command read `0002`, Bus Master Enable clear, against `0006` at the start. The driver's own proof that DMA
had stopped cleared it after recovery 1. It is a consequence, not a cause.

**The third set**, with `XhciEventRingTrbs` 256:

- `evt.ring=00040100`: the smaller ring, in the controller block, as before `3.4.2.0`;
- no `ctrl.failed` or `ctrl.recover` line;
- a SuperSpeed disk enumerated, and 4,928 transfers completed.

The Intel controller beside it stayed on the 1,024-entry ring (`evt.ring=00000400`) with no fault.

**Why the controller ran on `3.4.1.0`.** Issue 26's snapshots (`3.4.1.0`, the same machine) show the ASM1042
started, not failed, and enumerating a drive. The drive itself then stopped at Code 10, issue 26's separate
finding. `3.4.1.0` had only the 256-entry, one-page ring. The 1,024-entry segment in its own
16 KiB block arrived with `3.4.2.0` (task 47.30, design record 13 section 11.10).

## 3. The causes

**The recovery** (established; design record 17 section 4.6.1):

- The in-place recovery's waits are a 10 ms stall, and this part's HCRST outlasts it.
- Nothing paced the attempts. The thread is woken by the recovery's own invalidation, and the request was
  raised again at once.
- Each attempt was charged, including the refusals made while the first reset was still running, so three
  attempts were spent inside one tick. In the second report a later attempt found the reset finished and wrote
  a fresh HCRST, which ran out the same way.
- The code's own comment said the extra time would come from "the next health poll". In the HCD it did not.

**The trigger** (the cure established, the mechanism not):

- The 1,024-entry event ring of `3.4.2.0` on this pre-1.0 part. With the ring of the releases before, the
  controller was not failed.
- One machine and one test do not separate the segment's size (16 KiB, four pages) from its placement (a
  separate block instead of the controller block).
- Nor do they say which silent escalation fired first. The candidates are a Host Controller Event, which
  escalates on every completion code, a failed interrupter re-arm, and a command-ring divergence; `3.4.3.0`
  recorded none of them in a release build.
- What would settle it:
  - a 256-entry ring placed in the separate block (a test build); and
  - on a `3.5.0.0` build with the large ring forced, the new counters and notes (`evt.hc` with its code,
    `evt.rearm.failed`, `cmd.diverged`) in a controller-1 snapshot.

## 4. How Linux handles it

Linux gives every controller event-ring segments of 256 TRBs, one 4 KiB page each (`external/linux/xhci.h`,
`TRBS_PER_SEGMENT`). The number of segments is bounded by the controller's ERST Max. It has never put a
segment larger than a page in front of a controller. It applies `XHCI_SPURIOUS_SUCCESS` and
`XHCI_BROKEN_STREAMS` to `1B21:1042` and nothing about reset timing (`xhci-pci.c`). Linux's reset waits are
polled for 250 ms or 10 s (`xhci.h`, `XHCI_RESET_SHORT_USEC` / `XHCI_RESET_LONG_USEC`).

## 5. The fix

- **The recovery** (task 49.35):
  - A reset still running when the stall ends is not charged. The controller thread waits for it at
    PASSIVE_LEVEL for the start's own 1000 ms.
  - The same recovery then runs again and adopts the completed reset instead of writing another.
  - A cleared Bus Master Enable is put back before that run.
  - After an attempt that did not recover, the next waits at least 540 ms.
  - The latch is unchanged: a controller that never comes back is latched after three bounded attempts, about
    six seconds, not inside one tick.
- **The ring** (the owner's decision of 2026-11-13; design record 13 section 11.10.1). A controller below xHCI
  1.0 always uses the 256-entry ring, whatever `XhciEventRingTrbs` says, so this ASM1042 is fixed by the update
  alone, with the registry untouched. The driver's log notes the hold when the value asked for the larger
  ring (`evt.ring.held`), and the report says which ring is in force and why. For every other controller the
  value is read as `3.4.2.0` and `3.4.3.0` read it (256 the smaller ring, any other value the larger), and
  absent is 256; a fresh install writes 256, and an update keeps the 1024 those releases wrote.
  - Why a fresh install gets 256: no other system gives a controller one 1,024-entry segment. Linux uses
    segments of 256 entries, two for its first interrupter as ERST Max allows; FreeBSD one segment of 232;
    NetBSD one of 256; OpenBSD and Haiku one of 208.
  - It is as safe as `3.4.1.0`'s 256 with the eight requests in flight `3.4.2.0` gave an audio or video
    stream: what a controller can leave unread is bounded by the transfer rings, and the page a High Speed
    stream gets is sized from the ring the start allocated. At 256 entries, counting each stream at its ring
    plus nine:

    | Streams open | Now (8 requests) | `3.4.1.0` (4 requests) |
    |---|---|---|
    | one High Speed stream on a page | 121 of 256, with the 64-entry margin and room for a second stream | 125 |
    | a page and a second stream on the 62-entry ring | 192, inside the margin | 192 |
    | a page and two more streams | 263 | 259 |

    From three streams on, both versions count past the ring; filling it needs every stream's ring full and
    no event read for about 8 to 62 ms, as in `3.4.1.0`.
  - The cost on 256: a High Speed stream holds one page of 112 entries (about 14 ms of small packets) instead
    of 254, and a second High Speed stream gets the 62-entry ring (7.75 ms). The case `3.4.2.0` lifted (a USB
    Audio 2.0 device at High Speed with eight 4 ms requests queued) is held to three requests again unless
    the value is 1024.
  - The event ring as several 256-entry segments, which gives that headroom back in the shape other systems
    use, is left for a later phase.
- **The record**: release-build notes and counters at every escalation that was silent. These are `evt.hc`
  with the completion code, `evt.rearm.failed`, `cmd.diverged`, `cmd.fatal`, `ctrl.fatal`, `dma.proof` and
  the recovery's own notes, and a plain Save Report shows them.
- Read in the emulator on 98 SE and XP x64 with a reset held by the `qemu` flavour's fault `1B`
  (`docs/contributing/runs/run-49.md`, "49.35"). On both, a 50 ms reset recovers after one wait. On 98 SE a
  1.5 s reset latches after three attempts 27 ticks apart. On XP x64 a 1.5 s reset still recovers, because the
  1 s bound counted in 10 ms sleeps is about 1.5 s of wall time there; a reset that never ends latches after
  three attempts 27 ticks apart. Not read on the reporter's machine.

**The workaround for `3.4.2.0` and `3.4.3.0`**: `XhciEventRingTrbs` 256 in the ASMedia controller's driver key
(`docs/using/tuning.md` of those releases), then a restart. The USB Tree's settings page did not offer the
value. From `3.5.0.0` the update alone is enough on this controller: below xHCI 1.0 it uses 256 whatever the
value says, and the settings page offers the value.

## 6. The tool's lesson

File > Save Snapshot and the tray's quick save took "the first xhci98 controller" only, and `/c N` was
documented nowhere a user looks. Twice the snapshot was of the wrong controller, and the counters that name
the first cause were never captured. From `3.5.0.0` a snapshot takes every controller `xhci98.sys` drives, one
set of files each (design record 25 section 15).

## 7. Open

- Which escalation fired first, and whether size or placement matters (section 3).
- On the small ring the third set shows four Stall completions on the disk's endpoint (`xfer.error=00010306`,
  slot 1 DCI 3), and the command-state checker refusing a Stop Endpoint on an endpoint already stopped
  (`strict.refused.code=13`, `cfg.stop.again`): issue 26's shape. Transfers completed. Not analysed.

## 8. Rules kept

- **A bounded wait at DISPATCH_LEVEL is not the bound.** What it cannot finish belongs to a context that may
  sleep, and the retry must be paced by time, not by the next wake.
- **A snapshot is of every controller.** The wanted one is the one that failed, and a user cannot be expected
  to know its number.

## Sources

- The reporter's three report sets (`xhcitr-report.txt` / `REPORT.TXT`, the snapshots, `XHCISET.TXT`), in the
  owner's bench folder (git-ignored, 2026-11-11 to 2026-11-13).
- `external/linux` (`xhci.h`, `xhci-pci.c`, `xhci-mem.c`), recorded in `external/README.md`.
- Design record 13 sections 11.10 and 11.10.1; design record 17 section 4.6.1; design record 25 section 15;
  `docs/contributing/runs/run-49.md`, "49.35"; issue 26.
