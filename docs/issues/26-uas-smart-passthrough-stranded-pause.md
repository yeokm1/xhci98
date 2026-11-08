# Issue 26 - A SMART read of a UAS flash drive left its endpoint paused for good: timeouts, a lost volume, a freeze

Status: **fixed for `3.4.2.0`** (commits `d5dc1407`, `0e0488be` and
`a90afe96` on branch `w47-uasx`), reviewed; **not yet read on the
reporter's machine.** Why the controller restarted the endpoint against the
driver's stop is not known.

**Scope.** A UAS drive running with streams under `xhciuas.sys`, which is
Windows 98 SE to 7 (on 8, 8.1 and 10 Windows' own UAS driver serves it). The
HCD part of the fix, the bounded stop, applies to every endpoint on every
controller. Reported on Windows XP x64 SP2, an Intel 7 Series xHCI
(`8086:1E31`, revision 04), with a Fresco Logic FL1100 add-in card
(`1B73:1100`) as the comparison; `3.4.1.0`, logging level 3. The drive:
`0781:55A9`, a 460 GB flash drive formatted exFAT, offering Bulk-Only and
UAS (four streams on its status, data-in and data-out endpoints). The same
reporter as issues 22 and 24.

## 1. What was seen

Each time HWMonitor 1.62 started with its SMART reading on, the drive went
wrong on the Intel controller's ports: Windows showed it with its full
capacity and 0 bytes free, or raised "Windows - Wrong Volume" (`c0000012`,
`STATUS_WRONG_VOLUME`) and listed it with no size and no label; HWMonitor,
run from the drive, hung for a while and then opened or crashed; twice the
whole system froze until the drive was unplugged. Unplugging and plugging it
in again brought the volume back (265 GB free). With SMART off in HWMonitor
there was no problem in five runs; on the FL1100 there was none in five runs
with SMART on. A USB 2.0 Bulk-Only drive on the same Intel ports had no
trouble. The reporter had set the controller to polling (`XhciInterruptMode`
2) for an earlier freeze, and later took three snapshots with the line
interrupt back and the discrete graphics card removed.

## 2. What the snapshots show

Fourteen snapshots from six boots, five on the Intel controller and one on
the FL1100.

- **Every Intel episode was a CHECK CONDITION followed by command timeouts**,
  each ended by an ABORT TASK the drive answered: no LUN reset, and no device
  reset unless a babble came with it. Three episodes had one timeout; two
  ran to 20 and 21 timeouts and ended only at the unplug, the freezes.
- **On the FL1100 the refusals were the whole story**: five CHECK
  CONDITIONs in five runs, no timeout, five transfers cancelled - each the
  data-in transfer a refused command no longer needed.
- On the Intel each episode also carried a transfer event that fell inside
  no queued transfer (`QueueUnmatched`), with no error code.
- **One boot logged a stream's Set TR Dequeue issued on the data-in endpoint
  while its context read Running** (`strict.violation.epstate=10030541`:
  Set TR Dequeue, slot 3, DCI 5, endpoint state 1). The boot with the line
  interrupt logged the same on the status endpoint (`10020941`: slot 2, DCI
  9), five times by its counter.
- With the line interrupt the line itself was healthy - delivered, the
  watcher never found it asserted unserved, no move to polling - and the
  routing consistent (IRQ 16 is PIRQA, which the xHCI's INTA drives, shared
  with the chipset's EHCI, PCI Express root ports and the graphics). Each
  of those three episodes ended with the drive's link dropping and the drive
  coming back on the same port, with no reset or cycle by the driver: a
  surprise removal of the volume.
- Two babbles on the data-in endpoint were recovered as `3.4.1.0` meant:
  one on a queued transfer by `xhciuas`'s device reset, one inside no
  transfer by issue 22's endpoint recovery (`tol.stream.halt`). The drive
  stayed both times.

The counters could not say which command failed, which of its transfers was
outstanding, or what sense came back: `xhciuas` kept counts only.

## 3. The cause

A UAS command posts its status read and, for data in, its data-in transfer
on the stream of its tag before the COMMAND IU. A flash drive has no ATA
device inside, so it refuses HWMonitor's ATA PASS-THROUGH with a CHECK
CONDITION, and `xhciuas` cancels the data-in transfer it no longer needs.
The bus takes it off the stream: it pauses the endpoint, stops it with Stop
Endpoint, moves the stream with Set TR Dequeue, and resumes.

On the Intel controller that stop raced the endpoint's restart. In
`3.4.1.0`, when Stop Endpoint was answered Context State Error and the
endpoint's context then read Running, `hcdCfgQuiesce` returned Running with
no fault, and `hcdCfgCancelPipe` returned at once - with the endpoint still
paused, the cancel no longer owed, and nothing left to resume it. Every later
transfer on that endpoint was held. Commands timed out one after another;
each ABORT TASK was answered, so `xhciuas` never went on to the device reset
that would have restored the endpoint. The file system's reads failed, its
verify of the volume failed (`STATUS_WRONG_VOLUME`), and Explorer showed the
volume's last capacity with nothing free. Where the stop instead found the
endpoint Stopped and it ran again just after, the stream's Set TR Dequeue
went to an endpoint read Running - the strict notes - which a conforming
controller refuses, and which the Intel took.

The FL1100 never raced the stop. Why the Intel's endpoint restarted is not
known: a doorbell rung before the pause, which some controllers act on only
after reading Stopped for a while (Linux's comment in its Stop Endpoint
handler), or the drive's ERDY for another stream are both possible.

## 4. How Linux handles it

Read in `external/linux` at the pinned commit (`c6859eed`):

- `xhci-ring.c`, the Stop Endpoint completion: a Context State Error with
  the endpoint Running ("Race, HW handled stop ep cmd before ep was
  running") queues the Stop again, and one with the endpoint Stopped is
  retried the same way until 100 ms after the Stop was queued.
- `usb-storage/uas.c`: with `US_FL_NO_ATA_1X` an ATA PASS-THROUGH (12) or
  (16) is answered by the driver itself with ILLEGAL REQUEST, INVALID FIELD
  IN CDB, and never sent; `unusual_uas.h` gives the flag per device (not to
  this drive). `uas_eh_abort_handler` never sends ABORT TASK: a timeout
  always goes on to a device reset.

## 5. The fix

- **The stop, bounded** (`d5dc1407`; `xhci_tol.c` `XhciTolStopRest`,
  `hcd_cfg.c` `hcdCfgStopBounded`): a Stop after which the endpoint reads
  Running, or one answered Context State Error with the endpoint read
  Stopped, is issued again after 1 ms, at most three times. An endpoint that
  keeps running, or a Stop that fails otherwise, is given up and faults the
  device, which is then cycled alone, not the controller (issue 24's route;
  a hub, or a device whose PDO is not yet published, keeps the controller
  reset it always had); a paused cancel is never left with nothing owed. A stream's Set TR Dequeue is never issued on an endpoint
  read Running: it is stopped again first. Notes `cfg.stop.again`,
  `cfg.stop.giveup` and `cfg.deq.running`. Every endpoint, status pipe
  included; host vectors (`test_tol`) that fail against `3.4.1.0`'s single
  stop.
- **`XhciUasNoAtaPassThrough`** (`0e0488be`): a per-drive value, 0 by
  default, in the drive's hardware key. At 1 `xhciuas` answers ATA
  PASS-THROUGH (12) and (16), in an SRB or a program's pass-through, and the
  SMART requests itself, as Linux's `US_FL_NO_ATA_1X` does, and sends none
  of them to the drive; `SMART_GET_VERSION` is refused as Windows' own
  `usbstor.sys` refuses it for a Bulk-Only stick. For a drive like this one,
  which has no SMART to give, it removes the trigger. `docs/using/tuning.md`
  says where the value is.
- **The last failures in the snapshot** (`a90afe96`, `2ee9a552`): `xhciuas`
  keeps its last eight failure events - a command that did not end GOOD, or
  a timeout as it ran out, before recovery (so a timed-out command can take
  two entries) - with the operation code, the tag, the statuses and sense,
  and which transfers were outstanding; the bus keeps them beside the
  drive's telemetry, and XHCITR prints them under each class driver record.

- **`XhciUasFastReset`** (the owner's choice, 2026-11-07): a second
  per-drive value, 0 by default and then unchanged. At 1 a second timeout
  within 120 s of an answered ABORT TASK, with no progress since, resets
  the device at once, and a final IU's data cancel still outstanding after
  5 s does the same, as Linux always ends a timeout in a reset. It trades
  more resets, and a volume verified again, for no freeze of the kind seen
  here should another cause hold a drive's commands.

## 6. Open questions and a separate finding

- **What restarted the endpoint** on the Intel controller. The next report
  with this build should show `cfg.stop.again` where the old one stranded.
- **The ASMedia ASM1042** (`1B21:1042`) on the same board, also under this
  driver, could not start the drive at all (Code 10). A separate finding,
  not this issue's defect, reproduced in the emulator: the ASM1042 cannot
  use UAS streams (Linux's `XHCI_BROKEN_STREAMS`, `XhciStreamsOff`), so the
  bus offers the drive there as Bulk-Only; but Windows keeps one devnode,
  `USB\VID_0781&PID_55A9\<serial>`, for a drive with a serial number on
  every controller, and that devnode was installed with `xhciuas` on the
  Intel port. Windows starts `xhciuas` again on the ASM1042; it selects the
  UAS alternate, the bus refuses to open streams, and the start fails.
  `3.4.2.0` fixes it (roadmap-usb4 UASBOT.1; design record 13 section
  10.7.3): where the bus offers such a drive as Bulk-Only, that form has a
  devnode of its own, `USB\VID_0781&PID_55A9\<serial>&BOT`, which Windows
  installs with `usbstor`, while `XhciBulkOnlyOwnId` is on (the default).
  What a user is told:

  > On the ASMedia ASM1042 the drive stopped at Code 10 because Windows
  > kept one Device Manager entry for it on every controller, installed
  > with the UAS driver on the Intel port; the ASM1042 cannot use UAS
  > streams. From `3.4.2.0` the drive gets an entry of its own on the
  > ASM1042, which Windows installs with its own Bulk-Only driver the first
  > time it is plugged in there, and it keeps UAS on the Intel and FL1100
  > ports. No step is needed.

## 7. Rules kept

- **A recovery must leave every path it paused owed or resumed.** A return
  that is neither strands the endpoint as surely as a hang.
- **A command the driver logs is a command a user can report.** Counters
  showed that something failed; they could not show what.

## Sources

- The reporter's snapshots (`.TXT`, `.BIN`, `.DEV`, `.PSC`,
  `xhcitr-report.txt`) and photos, in the owner's bench folder (git-ignored,
  2026-11-06 and 2026-11-07).
- `external/linux` (`xhci-ring.c`, `usb-storage/uas.c`,
  `usb-storage/unusual_uas.h`), recorded in `external/README.md`.
- Issues 22 and 24; design record 13 section 16 (pass-through).
