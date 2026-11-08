# Issue 22 - One babble on a UAS disk's stream ring, inside no queued transfer, made the driver cycle the disk: a surprise removal of a volume in use

Status: **fixed for `3.4.1.0`** (design record 17's tolerance; commits
`ea54714b` and `fc5eef40` on `3.4.1.0`), reviewed and built; **not yet
read on the reporter's machine.** The babble's own cause is not known.

**Scope.** A UAS disk running with streams under `xhciuas.sys`, which is
Windows 2000 to 7 (on 8, 8.1 and 10 Windows' own UAS driver serves the
disk; owner, 2026-11-04). Reported by a user on Windows XP x64 SP2, an
Intel 7 Series xHCI (`8086:1E31`, revision 04), with a
SanDisk 3.2Gen1 disk (`0781:55A9`, UAS with four streams on its data-in
endpoint) on a root port at 5 Gbit/s, `3.4.0.0`, logging level 3.

## 1. What was seen

The user's disk disconnected twice; the system then froze and later
stopped with PAGE_FAULT_IN_NONPAGED_AREA (0x50). No dump was kept. The
user found the disconnect came every time a program was run from the
disk, HWMonitor named among them; run from the internal drive it did
not. Four XHCITR snapshots, two boots, were sent.

## 2. What the snapshots show

In each boot, about six minutes after the start, the same sequence:

- one **Babble Detected** (completion code 3) on slot 1, endpoint DCI 5,
  the disk's UAS data-in endpoint (`IN 2`, streams); no other error code
  in either boot;
- the event's TRB pointer lay on one of that endpoint's stream rings but
  **inside no transfer the driver had queued** (`QueueUnmatched 1`; no
  event left unclaimed);
- tolerance took its "halt with no TD" route (design record 17 section
  4.3): it read the endpoint context (`HaltReads 1`), confirmed the halt
  and **cycled the device** (`CyclesHaltNoTd 1`, note `tol.cycle`);
- Windows received a surprise removal of the disk's PDO, and the disk
  came back on a new slot.

The controller itself stayed healthy throughout: running, no
containment, no recovery window, no all-ones register reads; the class
driver's counters showed no timeout, abort or reset. A second disconnect
in the second boot had no transfer error at all (a port reset, a
surprise removal, a re-enumeration) and is not explained by this page.

## 3. Why it hurt

A babble halts a non-isochronous endpoint (xHCI 4.10.2.4). What should
follow is a recovery of that endpoint. What followed here was a reset of
the whole device, which Windows sees as the disk pulled out while a
program ran from it. A freeze and a fault are plausible after that; the
fault itself was not read (no dump).

## 4. How Linux handles the same case

Read in `external/linux` at the pinned commit (`c6859eed`):

- `xhci-ring.c`, `handle_tx_event`: an event on a No-op TRB or outside
  the current TD with a halting code goes to `check_endpoint_halted`,
  then `xhci_halted_host_endpoint` (a babble halts every non-isochronous
  endpoint) and `xhci_handle_halted_endpoint(..., EP_HARD_RESET)`: the
  endpoint is reset and its TDs given back.
- `handle_transferless_tx_event`: an event whose pointer is on no stream
  ring gets an endpoint reset, soft first, hard after repeats.
- The HCD never resets the device itself. `uas.c` ends the one command
  with an error (`uas_data_cmplt`, `DID_ERROR`); a device reset is the
  SCSI error handler's (`uas_eh_device_reset_handler`, `usb_reset_device`,
  the device kept bound).
- No quirk is listed for this controller's streams (Panther Point has
  `XHCI_EP_LIMIT_QUIRK` and software bandwidth checking only), nor for
  this disk in `unusual_uas.h` (the nearest, `0781:55E8`, runs on
  Bulk-Only by `US_FL_IGNORE_UAS`).

## 5. The fix

On an endpoint with streams open, a Stall, Transaction, Babble or Split
Transaction event that no stream's TD claims - its pointer on no stream
ring, on one inside no TD, or above 4 GB - marks **the endpoint** Halted
with every stream's requests on it: the existing recovery for a STALL in
a Prime Pipe transaction (`hcdCfgCancelOne`: the requests completed as
stalled, the endpoint reset on both sides, the streams left open). It
never cycles the device. `xhciuas.sys` then runs its own recovery, a
device reset through `IOCTL_INTERNAL_USB_RESET_PORT` with the requests
ended `SRB_STATUS_BUS_RESET` for the class driver to retry, and the disk
stays present. The event is noted as `tol.stream.halt` (slot, DCI,
code). `XhciTolStreamHaltDrain` and its host vectors carry the rule.
Reviewed and revised: the review found the pointer-above-4 GB path still
cycling, fixed in `fc5eef40`.

## 6. Wrong turns and open questions

- **A chipset erratum** (Intel 7 Series specification update, "USB
  Babble Detected with SW Overscheduling") was considered and set aside:
  it concerns full-speed isochronous traffic through the integrated Rate
  Matching Hub on the EHCI side, not a SuperSpeed bulk endpoint on the
  xHCI.
- **Turning streams off** was not the fix: Linux runs this controller
  with streams, and the fault was the reaction, not the streams.
- **Why the disk babbled** is not known. Software that reads SMART
  through ATA pass-through (HWMonitor does) is a likely trigger: a
  pass-through whose buffer is shorter than its command asks for makes a
  UAS device overrun the data-in transfer. Linux's per-device answer is
  `US_FL_NO_ATA_1X` (`uas.c` answers ATA PASS-THROUGH itself). Not done
  here; the fix above already keeps the disk.
  The same reporter's later snapshots on `3.4.1.0` tied the drive's trouble
  to HWMonitor's SMART reads and found a second defect behind it, a cancel
  that left the data-in endpoint paused for good; `3.4.2.0` fixes that and
  adds the per-device answer, `XhciUasNoAtaPassThrough`
  ([issue 26](26-uas-smart-passthrough-stranded-pause.md)).
- **Why the event fell inside no TD** is not known either: a TD cancelled
  just before, or the controller's own reporting, are both possible.

## 7. Rules kept

- **A recovery's scope must match the fault's.** One endpoint's halt is
  recovered on that endpoint; a device cycle is for a device that cannot
  be recovered otherwise.
- **Compare with Linux before choosing a device-level action.** It had
  the endpoint-level answer for both of this case's paths.

## Sources

- The user's snapshots in the owner's bench folder
  (git-ignored, 2026-11-04): the `Desktop`, `too`, `tree` and `forte`
  sets (`.TXT`, `.BIN`, `.DEV`, `.PSC`, `xhcitr-report.txt`).
- `external/linux` (`xhci-ring.c`, `xhci-pci.c`, `usb-storage/uas.c`,
  `usb-storage/unusual_uas.h`), recorded in `external/README.md`.
- Design record 17 section 4.3; `src/hcd_dev.c`, `src/xhci_tol.c`.
