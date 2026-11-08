# Issue 24 - Invalid Stream ID errors on a UAS disk's status pipe ended in a reset of the whole controller, dropping every device on it

Status: **fixed for `3.4.1.0`** (commits `5215323d`, `4d865ccd` and
`3966fff3` on `3.4.1.0`), reviewed and built; **not yet read on the
reporter's machine.** Why the controller reported the errors is not known.

**Scope.** A UAS disk running with streams under `xhciuas.sys`, which is
Windows 2000 to 7. The fix itself is not specific to one controller.
Reported by a user on Windows XP x64 SP2 with a Fresco Logic FL1100 add-in
card (`1B73:1100`, revision 10, the line interrupt), `3.4.0.0`, logging
level 3. On the card: a SanDisk 3.2Gen1 disk (`0781:55A9`, UAS, four
streams on each of its status, data-in and data-out endpoints) at
5 Gbit/s, a VIA Labs VL812 hub, and a UAC2 audio dongle (`3302:12C3`).

## 1. What was seen

The user was loading a game from the disk when every device on the card
disconnected at once. Windows reported "Delayed Write Failed" for a file
on the disk. One XHCITR snapshot was sent, with a screenshot.

## 2. What the snapshot shows

About 13 minutes after the controller's start:

- three **Invalid Stream ID** events (completion code 34) on slot 1,
  endpoint DCI 9, the disk's UAS **status** endpoint (`IN 4`, streams);
  none of them matched a transfer the driver had queued
  (`QueueUnmatched 3`), and nothing recovered the endpoint;
- the UAS driver's record for the disk: two command timeouts and one
  abort;
- the abort's **Set TR Dequeue Pointer** for one of that endpoint's
  streams, with the endpoint context read **Stopped** just before, refused
  with **Context State Error** (`strict.refused.unpredicted`
  `10010943`, `strict.refused.code` `13h`);
- a refused command on a device still present is answered by a reset of
  the whole controller (`hcd_cfg.c`, `hcdCfgFault`): `ctrl.failed.here`,
  `ctrl.recover.begin`, `ctrl.recovered`; every device was enumerated
  again, the disk on a new slot.

Apart from that one refusal the controller was healthy: no containment,
no all-ones reads, and every other command succeeded.

## 3. Why it hurt

The disk, the hub and the audio dongle all went away together, so a
volume in use was surprise-removed and the audio stopped. A refusal on
one endpoint of one device does not need a controller reset.

## 4. How Linux handles the same case

Read in `external/linux` at the pinned commit (`c6859eed`):

- `xhci-ring.c`, `handle_transferless_tx_event`: an Invalid Stream ID or
  Invalid Stream Type event that names no transfer resets the endpoint
  (`xhci_handle_halted_endpoint`, soft reset first, then a hard reset).
- `xhci-ring.c`, the Set TR Dequeue completion handler: a Context State
  Error is logged with the slot and endpoint states, and nothing more.
- `xhci-pci.c`: Linux gives the FL1100 no `XHCI_BROKEN_STREAMS`; only
  Fresco's `1B73:1000` and the FL1009 have it.

## 5. The fix

- **Invalid Stream ID or Invalid Stream Type inside no stream's TD** on an
  endpoint with streams is now that endpoint's recovery, the route
  issue 22 added for a babble (`xhci_tol.c`, `XhciTolStreamHaltDrain`):
  the endpoint is reset and the requests on it completed, never ignored.
- **A stream's Set TR Dequeue refused with Context State Error** is
  retried on every controller, up to twice, when the endpoint reads
  Running again (stopped once more, then the command again). This was
  already done on QEMU's xHCI for the same symptom. If it is still
  refused, only that device is cycled, never the controller
  (`hcdCfgSetDequeueOne`, `hcdCfgFault`). The mark stays until the device
  is gone, so a second fault for the same failure asks for no reset.
- New notes: `cfg.deq.retry`, `cfg.deq.refused` and `cfg.deq.cycle`
  (slot, endpoint, stream and the endpoint's state).

No quirk keyed on the FL1100: a controller that never reports these codes
or refuses this command never takes the new path.

## 6. Left

The reporter's reading on `3.4.1.0`. A workaround on `3.4.0.0` is
`XhciStreamsOff` `2` in the controller's driver key: the controller is
treated as having no streams, and this disk falls back to Bulk-Only.

## Sources

- The user's snapshot `B041343A` (`.TXT`, `.BIN`, `.DEV`, `.PSC`,
  `xhcitr-report.txt`) and a screenshot, in the owner's bench folder
  (git-ignored, 2026-11-05).
- The three commits were reviewed and revised: the review found a Reset
  Endpoint retry that skipped the other streams' repair, and a second
  fault that still reset the controller; both fixed.
