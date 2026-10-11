# Issue 14 - An asynchronous USB audio device plays only hiss on Windows 7: its every isochronous request names a frame, and on a controller without Contiguous Frame ID the driver refused them all

Status: **fixed in source for the `2.3.0.0` re-cut (roadmap task 36.57); not
yet observed working on the bench.** Found by the owner on 2026-10-11 at the
E7250 bench, on the signed `2.3.0.0` cut, and diagnosed the same evening
from an `XHCISNAP` dump and a static reading of Windows 7's `usbaudio.sys`.
The refusal at its root has been in the host controller driver since
`2.0.0.0`.

Devices affected: USB Audio Class 1.0 devices whose playback or capture
endpoint is asynchronous with an explicit feedback endpoint (the endpoint
descriptor's `bSynchAddress` names one), on Windows 7 - the one target whose
audio driver is read here to name a frame for every such request - and on a
controller that does not report Contiguous Frame ID capability (HCCPARAMS1
CFC, xHCI 4.11.2.5). Adaptive and synchronous devices, the large majority,
were never affected on any target.

## 1. Symptom

On a Dell Latitude E7250 (Intel `8086:9CB1` xHCI) under Windows 7 x64 with
the signed `2.3.0.0` cut and the Intel port switchover on, a Creative Sound
Blaster Play! 2 (`041E:323D`, Full Speed, a composite of audio and HID)
played only hiss: the hiss began when playback began and stopped when it
stopped, the same at 44.1 kHz and at 48 kHz (Windows 7's Default Format).
The same device played correctly on Windows' own USB 2.0 (EHCI) stack on the
same machine. A C-Media USB Audio Device (`0D8C:0014`, Full Speed) played
correctly through this driver, on the same port. Both devices' composite
devnodes were correct.

## 2. What the reading shows

The owner's evidence, held off-tree in `temp/2300bench/` (git-ignored, the
maintainer's copy):

- **The descriptors** (`usbview` dumps, `play2.txt` and `cmedia.txt`). The
  Play 2's interface 2 alternate setting 1 carries two endpoints in 9-byte
  audio-class endpoint descriptors: `0x01`, isochronous OUT, 200 bytes,
  `bInterval` 1, asynchronous, `bSynchAddress` `0x83`; and `0x83`,
  isochronous IN, 3 bytes (the 10.14 feedback format at Full Speed),
  `bInterval` 1, `bRefresh` 5. The C-Media's playback endpoint names no
  feedback endpoint.
- **The dump during the hiss** (`e7250-w7-play2-hiss.TXT`, verbosity 3):
  1,882 completions of code 27 (Stopped - Length Invalid), 55 Success, 8
  Stall Error (`xfer.error=00020106`: slot 2, EP0, apparently audio class
  requests the device declines) and 7 Short Packet;
  `TransfersSubmitted` 42 and `TransfersCompleted` 38, `TransfersCancelled`
  0.
- **The extension image** (`e7250-w7-play2-hiss.BIN`), decoded against
  `XHCI_EXTENSION`'s amd64 layout, whose size, 132,944 bytes, is the dump's
  `ExtensionBytes`: `IsoSubmits` 0 and `IsoPacketsAnswered` 0 - not one
  isochronous TD had ever been put on a ring - `CommandsIssued` 4,420 (10
  in an earlier dump of the same machine without the device), and
  `HcInfo.Cfc` 0: this controller does not report Contiguous Frame ID.
- **The control** (`cm.TXT` and `cm.BIN`, the C-Media playing, taken for
  this issue): `IsoPacketsAnswered` 6,492, 6,546 Success, 3 Stopped -
  Length Invalid; `IsoSubmits` 650, every one without a Frame ID;
  `CommandsIssued` 15. The same controller, the same driver and the same
  Windows moved isochronous data for this device and none for the Play 2.

## 3. The wrong turn

The 1,882 Stopped events and the 4,420 commands looked like the fault: a
driver stopping an isochronous endpoint over and over, or Windows 7's audio
driver aborting a stream it could not keep running. The first questions
asked were whether the driver issued a Stop Endpoint per request, whether a
late start frame led to cancellations, and whether a wrong feedback value
made the class driver reset its stream. `TransfersCancelled` 0 and
`IsoSubmits` 0 turned the question round: nothing had ever been on a ring to
stop. The storm was a symptom of something else, and in the end (section 4)
not a symptom of the fault at all.

## 4. What `usbaudio.sys` sends

Static, over Windows 7 SP1's `USBAUDIO.sys` in both architectures (x86,
80,768 bytes, SHA-256 `72603e0a...b933`; x64, 109,696 bytes,
`de1cddee...bb15`), with the function names from Microsoft's public symbols
for those files; nothing executed. The addresses are in
`docs/contributing/legal-provenance.md` section 4.

- `USBType1BuildIsochUrbRequest` builds a stream's data request. When the
  stream has a feedback endpoint it writes an explicit `StartFrame` from a
  running frame counter and leaves `USBD_START_ISO_TRANSFER_ASAP` clear;
  otherwise it sets `TransferFlags` to 4, ASAP. The counter starts at the
  current frame + 2 and advances by one a packet.
- `USBType1AsyncEndpointPoll` reads the feedback endpoint: a `RESET_PIPE`
  (URB function `0x1E`) on the feedback pipe, then a one-packet, 3-byte
  isochronous IN request at an explicit `StartFrame` - the current frame +
  the refresh period (2^`bRefresh`, 32 frames here) + 2 at the start.
- The read's completion routine advances that frame by the refresh period
  and queues the next poll at once, through a work item and with no timer,
  whether the read succeeded or not.

So every isochronous request of an asynchronous stream names its frame,
and every request of an adaptive or synchronous one is ASAP. And the
`RESET_PIPE` before every feedback read is the class driver's own design:
in this driver each one is a Stop Endpoint on the running, idle feedback
endpoint, whose Transfer Event is the code 27, and a Configure Endpoint that
drops and adds it - two commands every 32 ms. **The storm was expected; it
was never the fault.**

## 5. The cause

Since the host controller driver's first isochronous work (roadmap Phase
26, `2.0.0.0`; design record 13 section 6.10), `hcdIsoPublish`
(`src/hcd_io.c`) refused an explicit `StartFrame` whose packets could not all
be given xHCI Frame IDs, answering `USBD_STATUS_BAD_START_FRAME` rather than
falling back to Start Isoch ASAP (SIA), which moves the start. Its policy
was to give a controller without CFC no Frame ID at all, so on the E7250
every request of the Play 2's stream - data and feedback alike - was refused
before a TRB was built. The device sat in its streaming alternate setting
with nothing sent to it, and its output stage hissed.

## 6. Why it surfaced only now

Every audio device this project had played through the host controller
driver was adaptive or synchronous: QEMU's `usb-audio` in every virtual
machine, and the C-Media and similar devices on real hardware. Their
requests are all ASAP, and the ASAP path never reached the refusal. QEMU's
own xHCI reports no CFC either, so a virtual machine would have shown the
fault at once - had it any asynchronous audio device to offer, which it does
not. (The Play 2 is in the README's device table from earlier work; this is
its first reading on Windows 7 through this driver.)

## 7. The fix (roadmap 36.57)

The first fix sent every such request with SIA, as Linux does
(`external/linux/xhci-ring.c`, `xhci_queue_isoc_tx`: SIA by default, a Frame
ID only when the controller has CFC). Its review found that this does not
pace the feedback read: run as SIA, a read completes a frame or two after it
is sent, the class driver asks for the next one 32 frames further on at
once, and the requested frame runs ahead of the bus by about 30 frames a
read until, within some 28 reads, it is further ahead than a Frame ID can
name (895 frames) and is refused again - each refusal one more reset. A
feedback read has to run at the frame it names.

xHCI 4.11.2.5 (p.200) allows that on a controller without CFC: "software may
set the Frame ID (that is, SIA = '0') only in the first Isoch TD of an Isoch
data flow, and shall set SIA = '1' in all subsequent Isoch TDs of the data
flow". As built:

- **With CFC**, as before: a Frame ID on every TD where every packet can have
  one.
- **Without CFC, a request that starts a data flow** - nothing of the pipe's
  on the ring - carries a Frame ID on its first TD and SIA on the rest. The
  feedback read is such a request every time, because the `RESET_PIPE` before
  it leaves its endpoint new and its ring empty.
- **Without CFC, a request that continues a flow** goes out with SIA, one
  Interval after the last TD, which for the data stream's contiguous frame
  counter is the frame it asked for.
- **Anything the controller cannot be told** - no frame sample, a first frame
  inside the scheduling threshold or behind the current frame - goes out
  with SIA, never refused. A `StartFrame` more than 895 frames ahead, or
  further behind than usbdi's range, is still `BAD_START_FRAME`.

The decision is pure and host-tested (`XhciXferIsoFramePolicy` and
`XhciXferIsoStartPlan`, `src/xhci_xfer.c`; `test_td`'s
`test_iso_start_plan`, which builds each case and checks every TD's SIA or
Frame ID, and runs 300 paced feedback reads with the lead held inside the
window). `test_pipe` walks the Play 2's own descriptors. The note ring
records the first four first-TD Frame IDs given without CFC (`iso.fid1`) and
the first four SIA publications of a named frame (`iso.sia`).

**On the bench:** _PLACEHOLDER - the owner's reading of the fix on the
E7250 (the Play 2 at 44.1 and 48 kHz, and the `XHCISNAP` dump taken during
it) goes here once taken._

## 8. What the project keeps from it

- **The test set must include an asynchronous audio device with a feedback
  endpoint.** Every isochronous reading before this one ran the ASAP path,
  and QEMU offers no such device, so the bench owes one on each release that
  touches isochronous scheduling.
- A refusal is not a neutral answer to a request the driver cannot schedule
  exactly. Refusing every request silenced a whole class of device. Schedule as closely as the hardware allows and say so in the log.
- A command storm in a dump is evidence of activity, not of fault. Read
  what the class driver sends before reading the storm as the driver's.
- A client that paces itself by completions needs its requests to complete
  at the time it asked for, not merely to complete.

## Sources

- The owner's bench evidence, held off-tree in `temp/2300bench/`:
  `play2.txt`, `cmedia.txt`, `e7250-w7-play2-hiss` and `cm`
  (`.TXT`, `.BIN`, `.PSC`), and the earlier `e7250-w7-line` dump.
- `docs/contributing/design/13-superspeed-hcd.md` section 6.10, and
  `docs/contributing/legal-provenance.md` section 4 (the `usbaudio.sys`
  reading).
- `docs/references/spec-1.2c-dump.txt` (per-machine, git-ignored): 4.11.2.5
  p.199-200.
- `external/linux/xhci-ring.c`, `xhci_queue_isoc_tx` and
  `xhci_queue_isoc_tx_prepare`.
- `docs/contributing/runs/run-26.md`, round 16 of the batch (c) review, where
  the refusal was chosen.
