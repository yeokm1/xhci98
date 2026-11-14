# Issue 26 - A SMART read of a UAS flash drive left its endpoint paused for good: timeouts, a lost volume, a freeze

Status: **fixed in part for `3.4.2.0`** (commits `d5dc1407`, `0e0488be`
and `a90afe96` on branch `w47-uasx`), reviewed. **Read on the reporter's
machine with `3.4.3.0` on 2026-11-12: the hang came back** by another way
(section 8), and `3.5.0.0` adds the fixes of section 8.2 (task 49.39). Why
the controller restarts the endpoint, and where the stream's dequeue then
is, are not known. A second report of the same drive model on an AMD
500-series chipset controller (2026-11-13) is noted in section 9.

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

## 8. The hang again on 3.4.3.0 (2026-11-12)

The reporter read `3.4.3.0`, which carries section 5's fix, on the same
machine: the same drive (`0781:55A9`, serial form) on the same Intel 7
Series controller (`8086:1E31`, root port 8, slot 2, UAS with four streams),
HWMonitor 1.62 started from the drive itself with its SMART reading on.
HWMonitor reported "L:\Tools\Monitoring is not accessible. The semaphore
timeout period has expired" and showed the drive as full; Windows was shut
down from the Start menu. Eight quick saves from one boot (00:04:10 to
00:09:38), one from the next (00:27:03); the ASMedia ASM1042 on the same
board had no device in the first eight.

### 8.1 What the saves show

Tags: [obs] read in the saves; [inf] inferred from them; [hyp] not read.

- The trigger is section 3's [obs]: the first failure is HWMonitor's ATA
  PASS-THROUGH (12) (`A1h`, 512 bytes in, a program's request) refused by
  the drive with CHECK CONDITION 5/20/00 (ILLEGAL REQUEST, INVALID COMMAND
  OPERATION CODE), with one transfer event inside no queued TD
  (`QueueUnmatched` 1). The drive went on working for about 180 data
  commands.
- Then one data-in command after another timed out [obs]: a READ (10) first,
  after HWMonitor's second refusal READ CAPACITY (10) over and over, 29
  timeouts in five minutes, every one ended by an ABORT TASK the drive
  answered, so no LUN reset, no device reset and no cycle followed.
- Every timed-out command had its final IU in, with SCSI status GOOD, and
  its data-in transfer still outstanding (the failure ring's "final-IU-in"
  with "data" busy) [obs]. The drive's status said the data was sent; the
  driver never had the data-in transfer back. Commands with no data phase
  kept completing GOOD throughout [obs].
- No further event fell inside no queued TD across those 29 timeouts [obs]:
  the missing data did not come back to a stale TRB. The data-in
  endpoint's stream was not serving the driver's TDs [inf]; whether its
  saved dequeue sat away from them or the controller had stopped serving
  the stream cannot be told from these saves [hyp], because no report of
  `3.4.3.0` carries the endpoint's or the streams' contexts.
- Section 5's guard ran once [obs]: `cfg.deq.running=00020531`, the data-in
  endpoint (slot 2, DCI 5) read Running before stream 3's Set TR Dequeue,
  and it was stopped again; no `cfg.stop.again` and no `cfg.stop.giveup`
  anywhere, so the Stop itself held each time. Just before it,
  `strict.violation.epstate=10020541`: one stream's Set TR Dequeue reached
  the controller with the endpoint Running, although the check before it
  had read Stopped [obs]. The controller restarted the endpoint between
  the driver's read of its state and the command [inf]. Both came after the
  first timeouts [obs].
- `XhciUasFastReset` would not have ended it [obs, code]: every command that
  ends GOOD counts as progress, and the no-data commands did.
- In the next boot (00:27:03) the drive moved 1.5 GB on the Intel ports with
  no failure and no pass-through, and on the ASM1042 ports (Bulk-Only under
  Windows' `usbstor.sys`, streams off there) it took 14 refusals in the
  Bulk-Only way (a STALL on the data-in endpoint, recovered) with every
  transfer completed [obs].

So section 5's fix did what it was built for and the hang came back by
another way: after a data-in cancel on a stream, the Intel controller's
handling of the stream's dequeue around the driver's Set TR Dequeue is not
what the driver believes [inf], and `xhciuas` had no rule that ends a
drive's answered-but-unfinished commands with a reset. What restarts the
endpoint, and where the stream's dequeue then is, stay not known [hyp].

### 8.2 The 3.5.0.0 fixes

- **The timeout with its status in** (design record 13 section 16.15): a
  command whose timeout runs out with its final IU in and its data transfer
  still outstanding goes to the device reset, not to ABORT TASK. This is
  what ends a hang like this one, whatever its cause below.
- **A drive a reset does not bring back** (record 13 section 16.17, the
  owner's choice): after 3 such resets in a row with no data delivered
  between them, the drive is given up until it is replugged; every command
  then fails at once (a device whose transfers do not come back is cycled
  instead, as a failed reset's). Only data delivered in the direction of
  the timed-out commands (reads, in every reading so far) counts as the
  drive working: writes that land do not keep it. Once it is given up,
  writes fail too: a write Windows had cached and could not save shows
  "Delayed Write Failed" (read on XP x64: a balloon, "Windows was unable to
  save all the data for the file L:. The data has been lost."; 2000 and 7
  the same text [hyp]), and on 98 SE a program writing gets a write error
  for the drive [hyp]. A user whose drive does this can set
  `XhciForceBulkOnly` 1 for it ([tuning](../using/tuning.md#forcing-bulk-only-instead-of-uas-xhciforcebulkonly)),
  so that it runs as Bulk-Only, without streams.
- **The drive's own refusal replayed** (record 13 section 16.16): once the
  drive has refused ATA PASS-THROUGH (12) or (16) with 5/20/00, `xhciuas`
  answers that operation for that drive itself with the same CHECK
  CONDITION until the drive is replugged, in memory only. This removes the
  repeat trigger.
- **The stream's dequeue read back** (design record 17 section 4.12.8): after
  a cancel on a stream endpoint, with the endpoint at rest, the stream just
  set is read back against what was sent and set again if it differs, and
  every other stream's saved dequeue is placed against its own ring; one the
  controller could not have reached by executing its ring cycles the device
  alone, never rewound. On by default; `XhciStreamDequeueRepair` 0 keeps
  the checks and their notes without acting. The emulator's
  `usb-uas x-ata-passthrough-refuse` gives the drive's refusal with the
  data-in TD left to cancel, and the qemu fault 1D a stream's dequeue left
  on the cancelled TD - independent of each other. Fault 1D is a
  deliberately stale hardware dequeue: a pass shows the driver recovers
  from it, not that it is the Intel controller's mechanism.

## 9. A second report of the same drive, on an AMD chipset (2026-11-13)

Reported 2026-11-13 against the released `3.4.3.0` (`xhci98.sys` and
`xhciuas.sys` x64 matched by SHA-256), Windows 7 x64, an MSI MAG B550
TOMAHAWK with a Ryzen 7 2700X. The drive is `0781:55A9` ("SanDisk
3.2Gen1", four streams) on the AMD 500-series chipset's controller
(`1022:43EE`, subsystem `1B21:1142`), root port 3: the same drive model
on a second controller family (sections 1 to 8 are an Intel 7 Series
controller's).

Tags as section 8.1.

- Under `xhciuas`, two sessions of about 200 MB recorded no failure [obs].
  Then a READ (10) of 651,264 bytes timed out after 60 s with neither its
  data nor its status returned ("transfers status data data-posted"), a
  TEST UNIT READY timed out after it, and 1 abort and 1 port reset
  followed [obs]. In the recovery a stream's Set TR Dequeue on DCI 9 was
  refused with Context State Error because the endpoint read Running
  again (`cfg.deq.retry` three times; `strict.refused.unpredicted`), then
  the link dropped to SS.Inactive; `SsWarmResets` reached 5 and root port 3
  was held after 3 of 3 re-enumerations while PORTSC still showed a
  device at SuperSpeed [obs].
- `XhciForceBulkOnly` set while the port was held did not bring the drive
  back [obs]. The value is read at the device's next enumeration
  (`HcdCtlForceBulkOnly`, called from `hcdXportFlags` at each device
  PDO's creation) [code], and a held port does not enumerate: the hold
  lasts until a stable disconnect (an unplug and plug in again) or the
  controller's restart [code]. The reporter restarted, and Bulk-Only then
  ran clean: 39,545 successful transfers, 4 Bulk-Only stalls recovered,
  no link drop [obs].
- A UAS class driver of a later Windows, not this project's, on the same
  controller started on the device and selected alternate setting 1 (four
  pipes open) but put no disk on it; a Transaction Error on the data-out
  stream (slot 1, DCI 6) and a warm reset followed [obs]. Why it put no
  disk is not recorded.
- Read against `3.5.0.0` from the source, not run [code]: the stop of a
  stream endpoint read Running before its Set TR Dequeue, and the bounded
  retries after a Context State Error (`hcdCfgSetDequeueOne`), were
  already in `3.4.3.0`; `3.5.0.0` adds the read-back of the stream's
  dequeue after a command that succeeds (`hcdCfgDeqReadBack`). A dequeue
  refused at every retry, as here, ends in `3.5.0.0` as in `3.4.3.0`: the
  refusal is logged and the device is cycled. The timeout of a command
  with no final IU in is still the ordinary ABORT TASK
  (`UasFinalTimeoutDue`, `uas_iu.h`: section 8.2's device reset is for a
  command whose final IU is in), and the port hold after three
  re-enumerations (`XhciTolLocCharge`) is unchanged, so the 60 s stall
  would probably still occur and be recovered by reset [inf].
- Not settled: whether the fault belongs to the drive, the controller or
  the link (a cable or port cause is not ruled out) [hyp]. Asked of the
  reporter: the same copy at logging level 2 on the chipset port, on a
  port of the processor's own controller and on a USB 2.0 port, and the
  drive on another system. Also seen and not explained: the controller's
  PCI Status reads `0810h` (target abort sent) in every report [obs].
- The owner's decision (2026-11-13): noted only; no default change for
  this drive in `3.5.0.0`.

## Sources

- The reporter's snapshots (`.TXT`, `.BIN`, `.DEV`, `.PSC`,
  `xhcitr-report.txt`) and photos, in the owner's bench folder (git-ignored,
  2026-11-06 and 2026-11-07).
- `external/linux` (`xhci-ring.c`, `usb-storage/uas.c`,
  `usb-storage/unusual_uas.h`), recorded in `external/README.md`.
- Issues 22 and 24; design record 13 section 16 (pass-through).
