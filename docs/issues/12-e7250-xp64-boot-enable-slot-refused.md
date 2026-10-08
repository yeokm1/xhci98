# Issue 12 - On a Dell E7250 under Windows XP x64, a SuperSpeed reader present at boot fails "no slot", and hot-plugged it binds `xhciuas` but shows no disk

Status: **open, root cause found (section 9.5); no fix yet.** A user report against `2.2.0.0`, read on 2026-10-07 from
XHCISNAP dumps and the source. Finding 1 (section 3) has a cause read from
the code and consistent with every counter, and no fix yet. Finding 2
(section 4) is not diagnosed; the leading reading is unconfirmed, and the
user will provide no further information. The work is roadmap Phase 36
(`roadmap-hcd.md`, opened 2026-10-07 on branch `2.2.1.0`): an interim build
from this evidence, checked again on the E7250.

**Update, 2026-10-07 evening:** the owner reproduced Finding 2 on the
owner's own E7250 under Windows 7 x64 through Microsoft's `usbstor.sys`
(section 8), which points to `xhci98.sys` rather than `xhciuas` (not yet
proven). The interim build
`2.2.0.1` (sections 3.5, 4.5 and 4.6) was withdrawn before it was sent and
its code reverted; a diagnostic build, `2.2.0.2`, takes its place
(section 8.4).

**Update, 2026-10-08:** the owner ran the diagnostic build `2.2.0.2` on the
E7250 under Windows 7 x64 and under a fresh XP x64 (section 9). **On both,
the controller's interrupt never reaches the ISR.** All event delivery
falls to the lost-interrupt backstop, at about one drain every half second.
Nothing is stuck and every stall is recovered; the I/O is only very slow.
That slowness explains the sluggish mouse and the missing drive letter.
`xpboot` reproduces Finding 1, and the same lost interrupt is behind it. So
section 8.3's reading is set aside, and section 5 is answered.

**Update, 2026-10-08 evening - root cause (section 9.5).** The E7250's
BIOS routes the xHCI's INTA in the chipset to PIRQA (IRQ 16), while its
ACPI tables tell Windows the xHCI is on IRQ 21 (PIRQF, the EHCI's line).
The driver therefore listens on a line its controller never drives. The
owner rewrote the PCH's Device 20 Interrupt Route register (`D20IR`) to
PIRQF on the running machine. The mouse became smooth at once, and a
hot-plugged stick mounted with a drive letter. This is a firmware defect.
It goes unnoticed under Intel's own Windows 7 driver and under Windows 8
and later, because those use MSI and bypass the PIRQ routing.

**This is the first report of the HCD on real hardware under Windows XP
x64.** That tier had been virtual-machine only (AGENTS.md, the release
notes' "Targets and their standing").

Machine: Dell Latitude E7250, Intel Wildcat Point-LP xHCI `8086:9CB1` rev
03, subsystem `1028:062D`. XHCIQUAL `2.2.0.0` under DOS saw INTx pin A,
line 11, MSI present but not enabled, and no disqualifiers; USB 2.0 ports 1
to 11, USB 3.0 ports 12 to 15. What XP x64 routes the interrupt to is not
in the evidence. Build: `2.2.0.0`, `release` flavour, `XhciLogVerbosity` 2,
the amd64 `xhci98.sys` and `xhciuas.sys`. Device: Kingston USB 3.1 microSD
reader `11B0:3307` (UAS) holding a 256 GB SanDisk Extreme Pro card.

## 1. Symptom

The user reports the reader "not detected", and later that no other USB
device showed up either. Two cases, read separately:

- **Plugged in before boot** (`MYDUM`): port 13 trains at SuperSpeed (speed
  ID 4), then `enum.port.slot` reads completion 0 ("never completed") on
  attempt 0 and attempt 1. The port ends `Failed`, cause "no slot", retries
  used 1, and stays Failed until the device is unplugged.
- **Hot-plugged after boot** (`AFTERUSB`, `MYDUMP`): the device enumerates,
  but the user says it does not appear in Device Manager.

## 2. The evidence

`temp/e7250/` (git-ignored):

| File | What it is |
|---|---|
| `MYDUM.*` | device plugged in before boot |
| `MYDUMP.*` | after boot, device hot-plugged (port 13 empty at boot) |
| `BEFOREUSB.*` | a separate boot, before the device was plugged in |
| `AFTERUSB.*` | the same boot as BEFOREUSB, after the device was plugged in |
| `PROBE.LOG` | XHCIQUAL `2.2.0.0` probe-only run under DOS |

The dumps are from the amd64 driver: the extension holds 64-bit kernel
pointers, and an amd64 build of the offsets probe reproduces XHCISNAP's own
figures exactly (sizeof 132912, note ring at +83544, Tol at +116352).

How to read the `.BIN` again:

- `scripts\local\offsets.txt` is stale (2026-09-22) and 32-bit. Do not use
  it for these dumps.
- Build the probe for amd64: a `main()` that includes `src/xhci.h` and
  prints `offsetof(XHCI_EXTENSION, ...)`, compiled with
  `/DXHCI_HOST_TEST /I src` by `tools\WinDDK71\bin\x86\amd64\cl.exe`, with
  INCLUDE=`WinDDK71\inc\crt;WinDDK71\inc\api`,
  LIB=`WinDDK71\lib\crt\amd64;WinDDK71\lib\wnet\amd64`, linked
  `/subsystem:console`. It runs on an x64 host.
- Offsets used: InterruptCount 10472, DpcCount 10476, InterruptsClaimed
  10480, EventsTotal 10488, ControllerFailed 10612, StartEpoch 10660,
  CommandState 10664, CommandType 10676, LastCommandTrbPA 10680,
  CommandCompletionCode 10684, CommandsIssued 10692, CommandsCompleted
  10696, CommandStaleCallbacks 10736, CommandTimerFailures 10740.
- An earlier pass used an MSVC6 x86 build plus a constant +24. That was only
  the x86-to-amd64 layout gap in that one range. Every value it gave is
  confirmed by the amd64 probe, but do not reuse the +24.

## 3. Finding 1 - present at boot: Enable Slot is refused behind the start's No Op

### 3.1 What the dump shows

The command engine in `MYDUM.BIN`: CommandsIssued 1, CommandsCompleted 1,
last command type 0x17 (No Op), completion code 1, CommandState idle,
CommandTimerFailures 0, ControllerFailed 0. The only command issued in the
whole boot was the start's No Op. **Neither Enable Slot reached the ring.**
`Counters.CommandsGivenUp` is 0, so they did not time out either.
`BackstopDrains` is 1 on every boot dump, including BEFOREUSB's empty boot.

### 3.2 The mechanism (code read)

This is shared HCD code with no per-OS branch on the path, so it is not
specific to XP x64. What exposes it is that the No Op's completion raises no
interrupt, which has been seen only on this machine.

1. `XhciInitController` step 18 issues the No Op self-test
   (`src/xhci_init.c:5209`) while interrupts are still masked. `hcdStart`
   enables them only afterwards (`src/hcd_ctl.c:1418-1444`).
2. On this machine the No Op's completion raises no interrupt, even after
   the enable. `XhciEnableInterrupts` is written so that an IP held while
   masked is delivered once IE and INTE are set (`src/xhci_evt.c`, the
   comment above `XhciEnableInterrupts`, citing xHCI 4.17.2 and 5.5.2.2).
   Here it is not. Either this controller does not latch IP while masked,
   or the interrupt is lost the way about half of all interrupts are on this
   machine (section 5). Which one is not known.
3. The event is drained by the lost-interrupt backstop (`hcdBackstop`,
   `src/hcd_ctl.c:898`) only once it has stood 100 ms.
4. Until then `CommandState` is not idle, so `xhciCommandSubmitEx` returns
   `XHCI_CMD_BUSY` (`src/xhci_cmd.c:1006`, "BUSY covers both"), and
   `hcdCommand` returns 0 at once (`src/hcd_enum.c:180-183`). It counts
   `hc->EnumCommandsRefused` and nothing else. It does not wait, retry or
   request a reset.
5. `hcdThread` runs `hcdPoll`, which includes the backstop, and **then**
   `HcdEnumService` (`src/hcd_ctl.c:1149-1156`). On the first passes the No
   Op has not yet stood 100 ms, so the backstop queues nothing, and
   `HcdEnumService` runs port 13's whole enumeration, both attempts, before
   the thread gets back to the poll that would drain it.

In `2.2.0.0` `EnumCommandsRefused` lived in `HCD_CONTROLLER`, outside the
`XHCI_EXTENSION` that XHCISNAP dumps. So the refusal is inferred from
CommandsIssued 1 and CommandsGivenUp 0, not counted in the dump. (Task 36.1
moved it into the counter block; section 3.5.)

### 3.3 Workaround

Unplug and replug after boot. That gets past this failure, and then the
user meets Finding 2.

### 3.4 Fix candidates (owner to choose; either or both)

- **At start:** right after `XhciEnableInterrupts` in `hcdStart`, queue one
  `IsrDpc` drain through the ISR's admission (`DpcsInFlight` counted, as
  `hcdBackstop` does). An event posted while interrupts were masked is then
  retired at once, not after 100 ms.
- **In `hcdCommand`:** on `XHCI_CMD_BUSY`, queue a drain and wait, inside
  `HCD_COMMAND_WAIT_MS` (5 s), for the engine to go idle, then resubmit,
  instead of failing the port.

The first removes this one window. The second covers any BUSY that an
enumeration meets, such as a late completion of an earlier command.

Also worth doing: publish `EnumCommandsRefused` in a region XHCISNAP reads,
so a future dump names this failure directly.

A fix needs a host test, both builds (x86 and `-amd64`), the gates, and a
bench reading with a device present at boot, from this user or from another
Intel 9-series machine.

### 3.5 What the interim build does (task 36.1)

> **Withdrawn 2026-10-07 (owner):** this code was reverted before the build
> was sent (section 8.4). The section records what was built.

Both candidates, taken on 2026-10-07: asked to choose, the owner asked for
a recommendation, and both was it, because the first closes only the window
seen here and the second covers every busy engine an enumeration can meet -
an in-place recovery's own No Op (`xhci_init.c` enables interrupts there
itself, not through `hcdStart`) among them.

- `hcdStart` queues one drain through the ISR's admission right after
  `XhciEnableInterrupts` (`HcdQueueDrain`, the queueing the backstop and the
  injector now share), counted as `StartDrainsQueued`.
- `hcdCommand`, on `XHCI_CMD_BUSY`, queues a drain and waits on the command
  event a 10 ms slice at a time, then submits again, for at most
  `HCD_COMMAND_WAIT_MS` (5 s) in all; the slices are the pure
  `XhciEnumBusyWait` (`src/xhci_enum.c`, host vectors in `test_enum`). A
  command that met a busy engine counts `EnumCommandsBusy` once, one that
  then went out `EnumCommandsBusyCleared`, and one that never reached the
  ring - busy past the wait, or refused for any other reason -
  `EnumCommandsRefused`. All three are in the counter block, so a dump now
  names this failure directly.

On the E7250 either would have let the first Enable Slot out: the start's
drain retires the No Op at once, and a busy engine met anyway is waited out
by the second.

## 4. Finding 2 - hot-plugged: `xhciuas` binds, and no disk appears (open)

### 4.1 What the dumps show

The dumps contradict a plain "nothing happened":

- Port 13, SuperSpeed, slot 1. The HCD's root-port record says `Bound`,
  "USB 3, PDO", so a PDO was created and reported.
- SelectEndpointsRequested 4, EndpointsOpened 4. That counter is incremented
  only from a client's URB (`src/hcd_cfg.c:331`, and the three select paths
  in that file). The bus's own composite set-up opens no endpoints. So a
  class driver was started on the reader's PDO and selected a 4-endpoint
  interface: the UAS alternate setting. That is very probably `xhciuas.sys`.
- Transfers keep flowing: TransfersSubmitted 90 then 194, Success 98 then
  202.
- `xfer.error=00010306` x4: slot 1, DCI 3 (EP1 IN), Stall. QueueHalts 4,
  QueueErrors 4. Also Short Packet 5 and Stopped - Length Invalid 2
  (cancelled transfers). MYDUMP also has SsResetsConverted 1.

Whether EP1 IN is the status pipe or the data-in pipe is unknown: the dump
keeps no descriptors.

### 4.2 What `xhciuas` does with a stall in bring-up (code read)

- The pipes are mapped from the Pipe Usage descriptors, not by endpoint
  order (`src/uas/uas_iu.c:258-337`). One `SELECT_CONFIGURATION` selects the
  UAS alternate setting (`src/uas/uas_fdo.c:152-208`). That matches the 4
  endpoints in the dump.
- On a SuperSpeed device, streams are opened, then the engine starts. The
  only SCSI commands sent before a disk PDO is created are REPORT LUNS on
  LUN 0 and then INQUIRY per LUN, each up to 3 tries with a 10 s timeout
  (`src/uas/uas_fdo.c:227-289`, `src/uas/uas_xport.c:1946`).
- **The UAS driver has no pipe-level stall handling.** There is no
  `URB_FUNCTION_RESET_PIPE`, `SYNC_RESET_PIPE_AND_CLEAR_STALL` or
  `ABORT_PIPE` anywhere in `src/uas`. A failed bulk transfer marks its slot
  Failed (`uasXferDone`, `src/uas/uas_xport.c:811-878`), and the recovery
  worker resets the whole port with `IOCTL_INTERNAL_USB_RESET_PORT`
  (`uasResetDevice`, `src/uas/uas_xport.c:1155-1181`). The command ends
  `SRB_STATUS_BUS_RESET`, and the caller's retry loop tries again. A device
  that keeps stalling can see several port resets during START.
- **If REPORT LUNS or every INQUIRY fails, START still succeeds**
  (`src/uas/uas_fdo.c:388-411`): a failed INQUIRY is `continue`, and the
  function returns `STATUS_SUCCESS`. No disk child is created, and the
  devnode shows no error.
- There is no fallback to Bulk-Only Transport (`src/uas/uas_fdo.c:9-14`).
- The UAS driver keeps 8 counters (`fdo->Counters`, `src/uas/uas.h:219`)
  but nothing exposes them, and it logs nothing. XHCISNAP does not cover it.

### 4.3 The leading reading (unconfirmed)

`xhciuas` bound to the reader. Its INF is `Class=USB`, so it lists under
"Universal Serial Bus controllers" as "xHCI98 USB Attached SCSI Storage",
not under Disk drives. Its INQUIRY or REPORT LUNS then failed on the EP1 IN
stalls (four stalls, port resets, cancels), START succeeded anyway, and no
disk child was ever created. A user looking under Disk drives would read
that as "not in Device Manager".

Not ruled out: the devnode appeared and was later removed, or it sits under
"Other devices". The dumps cannot separate these, and they hold no
evidence about the user's other devices: only port 13 ever held a device in
the four dumps.

**Nothing in the driver hides a devnode.** `IRP_MN_QUERY_PNP_DEVICE_STATE`
succeeds with nothing set on the device PDOs (`src/hcd_pdo.c:2597`) and the
root hub (`src/hcd_rh.c:381`). `SilentInstall` and `RawDeviceOK` are FALSE
everywhere (`src/hcd_pdo.c:2426`, `src/hcd_rh.c:299`,
`src/uas/uas_pdo.c:157`). There is no `NoDisplayInUI`.

### 4.4 Not yet checked

- Whether 4 stalls and 1 SsResetsConverted agree with the recovery path
  above (several port resets would be expected from 3 INQUIRY tries). Read
  how `IOCTL_INTERNAL_USB_RESET_PORT` is counted in the HCD before relying
  on SsResetsConverted.
- Why the device stalls EP1 IN at all. A UAS device stalling the status or
  data-in pipe during INQUIRY is not normal. It may be this device, or a
  stream or ring set-up problem in the HCD on this controller.

### 4.5 What the interim build records (task 36.2)

> **Withdrawn 2026-10-07 (owner):** this code was reverted before the build
> was sent (section 8.4). The section records what was built.

No fix for Finding 2; what the next dump needs to give it a cause:

- **A class-driver report in the dump.** `xhciuas.sys` sends the bus a
  report (`src/xhci98_report.h`, `IOCTL_XHCI98_CLIENT_REPORT`) at the end of
  every START, after every port reset it asks for, and at STOP. The bus
  keeps the newest four, one per device, in the snapshot's HCD region after
  the counters, and XHCISNAP decodes them. A report says how far START got
  and with what status; the interface, its alternate and the endpoint each
  Pipe Usage descriptor named (so EP1 IN is named the status or the data-in
  pipe); the streams wanted and granted; REPORT LUNS's and the last
  INQUIRY's SRB status, SCSI status, tries and sense; the LUNs reported,
  inquired, found and failed; the first and latest transfer failure with
  its USBD status, pipe and tag, and the failures per pipe; and the driver's
  eight counters, its port resets among them.
- **Sense for the driver's own commands.** REPORT LUNS and INQUIRY were sent
  with autosense disabled, so a CHECK CONDITION left nothing to read. They
  now carry an 18-byte sense buffer; the commands themselves and how their
  outcome is judged are unchanged.
- **Client port resets apart.** The bus counts the
  `IOCTL_INTERNAL_USB_RESET_PORT` requests clients send as
  `ClientPortResets`, beside its own SuperSpeed counts, which settles the
  first question of section 4.4.

The report needs `xhciuas.sys` from the same build: an older `xhciuas.sys`
sends none, and an older `xhci98.sys` refuses the request, which `xhciuas`
ignores.

### 4.6 The interim build

> **Withdrawn 2026-10-07 (owner), never sent;** superseded by the
> diagnostic build `2.2.0.2` (section 8.4).

Numbered `2.2.0.1` to mark it a test build, not a release (owner,
2026-10-07). Built x86 and amd64 in all three flavours with every gate, the
host suites passing, and reviewed by Codex to convergence over three rounds
on 2026-10-07: round 1 found a race between START's report and the recovery
worker's (fixed with a report gate and locked writes), round 2 that the
qemu start hold could time out the resubmitted command (fixed by ending the
hold at the first busy drain), round 3 nothing. The QEMU regression legs of
task 36.3 are not yet run.

## 5. Interrupt delivery is poor on this machine

*Answered by section 9: the controller's own interrupt is not delivered at
all. The claims counted here came from ISR calls made for the other device on
the shared line.*

DpcCount always equals InterruptsClaimed + BackstopDrains: 69 + 54 = 123
(AFTERUSB), 114 + 112 = 226 (MYDUMP). About half of all event deliveries
come from the 100 ms backstop rather than an interrupt. InterruptCount is
305 and 382, so the ISR sees other devices' interrupts on a shared line.
There are no mask or unmask failures, and InterruptEnables is 1.

I/O will be slow, and class-driver timeouts are plausible, which might be
what the Stopped - Length Invalid cancels are. This may also be the same
cause as step 2 of Finding 1. It needs its own look: how XP x64 routes INTx
on this chipset, and whether the backstop is draining events whose
interrupt was raised but not claimed.

## 6. What can no longer be asked

**The user will provide no further information** (owner, 2026-10-07).
These would have separated the readings in section 4.3, and the interim
build's diagnostics (roadmap 36.2) have to stand in for them where they can:

- `setupapi.log` and a Device Manager view by connection with hidden
  devices shown: where the reader's devnode was, and whether it stayed.
  Nothing in a dump can show this; a UAS START outcome in the dump shows
  at least whether `xhciuas` started and found a LUN.
- Whether other USB devices work, and a dump with one plugged in: no
  substitute. The interim build's dumps will show it if another device is
  attached when they are taken.
- The reader at `XhciForceBulkOnly` 1: not asked; the interim build does
  not change that value.

The card's format matters only once a disk appears: a 256 GB SDXC card ships
as exFAT, which XP x64 reads only with the exFAT update (KB955704). That
affects the drive letter, not whether a disk appears.

## 7. Next steps

The work is roadmap Phase 36: an interim build made from this evidence and
checked again on the E7250 by the owner.

1. Owner: choose the Finding 1 fix (section 3.4; task 36.1). Both, built
   (section 3.5).
2. Add the diagnostics that let one dump settle section 4.4 (task 36.2).
   Built (section 4.5).
3. Build x86 and amd64, host-test, run the gates and the QEMU regression,
   and send the interim build (task 36.3).
4. Read it on the E7250 with a device present at boot and the reader
   hot-plugged (task 36.4), then fix what that leaves owed (task 36.5).

Superseded on 2026-10-07 by section 8.4: the diagnostic build `2.2.0.2`
(task 36.6), read on the owner's E7250 (task 36.7), then the fix.

## 8. Reproduced on the owner's E7250 under Windows 7 x64

### 8.1 The run

On 2026-10-07 the owner ran `2.2.0.0` (release, amd64, `XhciLogVerbosity`
3) on the owner's own Dell E7250: the same controller as the user's
(XHCIQUAL quick scan: `8086:9CB1` rev 03, subsystem `1028:062D`). Windows 7
x64, because XP x64 would not install. Device: a SanDisk USB 3.2 Gen 1 flash
drive, `0781:55AB`, Bulk-Only; `xhciuas` was not installed, so Windows'
own `usbstor.sys` drove it. Root port 13, SuperSpeed.

- **Hot-plugged:** "USB Mass Storage Device" and the SanDisk disk appear
  in Device Manager, both working properly. No drive letter ever appears,
  and Disk Management never gets past "Connecting to Virtual Disk Service".
- **Present at boot:** Device Manager will not open and Explorer hangs.
  Unplugging the stick does not release them.
- **A USB mouse** on the same machine works, but moves sluggishly.

Dumps (git-ignored, the owner's): `temp/e7520/hotplug.*` and `atboot.*`,
with `setupapi-hotplug.log`.

### 8.2 What the dumps show

Decoded from the `.TXT` and from the `.BIN` with an amd64 probe built
against `2.2.0.0` (`sizeof` 132912 matches the dump):

| | hot-plug | at boot |
|---|---|---|
| TransfersSubmitted / Completed | 99 / 98 | 332 / 331 |
| TransfersCancelled | 0 | 0 |
| Stall Error, slot's EP1 IN (DCI 3) | 7 | 7 |
| Stopped - Length Invalid | 2 | 2 |
| Commands issued = completed | 21 | 24 |
| InterruptCount / InterruptsClaimed | 750 / 80 | 885 / 242 |
| BackstopDrains (DpcCount = claimed + backstop) | 60 | 133 |

- **One transfer was outstanding when each dump was taken**, both taken
  well into the hang: submitted, with no completion and nothing retired by
  a cancel or abort (TransfersCancelled 0). Which endpoint it is on, and
  for how long, is not in the dump.
- The command engine is idle (the last command a Set TR Dequeue,
  completed Success). Every event was matched to its TD, and the
  controller thread had been polling up to the dump (its poll counter and
  clock agree); one dump cannot show that it went on doing so.
- **The same signature as the user's UAS dumps** (section 4.1: stalls on
  DCI 3, two Stopped - Length Invalid, no disk). A class driver Microsoft
  wrote fails here much as `xhciuas` did there. That points to a cause in
  `xhci98.sys` rather than in `xhciuas`, and replaces section 4.3's
  leading reading; it does not prove that the two reports share one fault,
  nor rule out the devices or the controller.
- Interrupt delivery (section 5) is just as poor under Windows 7: 43% and
  35% of DPCs come from the 100 ms backstop. That is the likely cause of
  the sluggish mouse.
- At boot, port 13 took slot 1, its machine ended Empty, and it enumerated
  again as slot 2; and USB 2.0 port 2 saw a connect and failed its reset.
  Why is not in the dump (a link bounce at the BIOS hand-off, and an
  internal device during the port switch, are guesses). The hot-plug run
  fails the same way without either, so neither is needed for the hang.

### 8.3 What the code allows (read, not yet confirmed)

*Set aside by section 9: the `2.2.0.2` dumps show every RESET_PIPE succeeding
and no Halted endpoint. The 4.6.8 ordering below is still a deviation from
the spec, but it is not this hang.*

Read against `2.2.0.0`, by a Claude subagent and independently by Codex:

- **The stall recovery runs in the wrong order.** `hcdCfgReset`
  (`src/hcd_cfg.c`, RESET_PIPE) sends the device `CLEAR_FEATURE(ENDPOINT_HALT)`
  before Reset Endpoint (TSP 0) and Set TR Dequeue. xHCI 4.6.8 resets the
  host endpoint first, then clears the halt on the device. When the clear
  fails or returns busy, the pipe resumes with the controller's endpoint
  still Halted (unless EP0 is stuck, when it is left paused).
- **A transfer on a Halted endpoint waits until something else retires
  it.** `HcdIoMapped` (`src/hcd_io.c`) does not check the pipe's Halted
  state: it publishes the TD and rings the doorbell, which a Halted
  endpoint ignores. Only a later recovery, cancel or abort ends it; the HCD
  has no transfer timeout of its own (usbport had one).
- RESET_PIPE, ABORT_PIPE and RESET_PORT pend on one queue with no cancel
  routine. A device's departure parks its in-flight transfers rather than
  completing them (`HcdIoPark`, `src/hcd_io.c`). Parked transfers stay
  cancellable and are released by an abort, the PDO's stop or its removal,
  so this fits "unplugging does not release it" only if the PnP hang keeps
  those from arriving: a possible explanation, not a shown one.

These make one path to the hang plausible. They do not prove it: the
snapshot does not hold the pipes, their transfers or the controller's
Endpoint Contexts. Other explanations remain: the endpoint Running but the
device out of step after recovery; the stuck transfer a control transfer
on EP0; an event the controller never generated.

### 8.4 The diagnostic build (`2.2.0.2`)

The owner's choice, 2026-10-07: withdraw `2.2.0.1` and revert its code,
keeping these documents with the withdrawn sections marked; then build a
diagnostic build, `2.2.0.2`, on `2.2.0.0`'s code, with no change in
behaviour, so the hang reproduces and one dump taken during it names its
cause. It records:

1. **The stuck transfer and its pipe:** for each open pipe, its flags and
   queue, its oldest outstanding transfer (URB function, setup bytes,
   length, age, TD address), and the Output Endpoint Context's state, TR
   Dequeue and DCS, read when the dump is taken.
2. **The recovery timeline:** the last RESET_PIPE, ABORT_PIPE, RESET_PORT
   and cancel requests: when each arrived and completed, the endpoint, the
   `CLEAR_FEATURE` outcome, the commands sent and their status, and how
   many slow requests were queued.
3. **Interrupt state at the backstop:** before each backstop drain,
   USBSTS, IMAN, ERDP, IMOD and USBCMD, and whether an event was waiting,
   with ISR and DPC times.

Roadmap tasks 36.6 (the build) and 36.7 (the read on the E7250 under
Windows 7 x64), then the fix.

Built on 2026-10-07 (`src/hcd_diag.c`; the HCD region's header grown to 31
words, version 1; `xhcisnap/README.md`, "The diagnostic build's records").
What a dump now carries, read under the controller lock:

1. Up to 24 endpoint pipes, those with a transfer outstanding first, the
   oldest first (the outstanding ones that do not fit are counted), each
   with the transfer it is on - the bus's own EP0 transfer (a
   CLEAR_FEATURE among them) when it heads the queue, else the oldest
   client transfer: record state, URB function, TransferFlags, SETUP bytes,
   lengths, TD addresses and cancel marks, the ticks (100 ms) since it was
   claimed and last published - and the Output Endpoint and Slot Contexts
   read at the dump; with the slow queue's depth, the URBs parked on PDOs,
   and the thread's wakes, cancel routines and cancels served.
2. The last 32 timeline entries: each slow request from its queueing to its
   completion, or to its hold on the PDO or its return to the queue, with
   the CLEAR_FEATURE outcome (never sent included) and the first four
   commands with their codes; each transfer cancel routine's run; and the
   thread's cancels, drains and removal aborts.
3. The last 16 backstop drains whose DPC was inserted, each with USBCMD,
   USBSTS, IMAN, IMOD, ERDP and the event at the software dequeue, and the
   ISR's, claim's and DPC's ages; the refused insertions counted; counts of
   the backstop's looks by IP set, IP clear and nothing waiting; and the
   same registers read live at the dump.

The only behaviour it adds is the recording itself: a few stores per
transfer, ISR and DPC, five register reads when the backstop finds an event
waiting, and the controller lock taken around each record on the slow
paths.

## 9. The `2.2.0.2` read: the controller's interrupt never reaches the ISR (task 36.7)

### 9.1 The runs

On 2026-10-08 the owner ran `2.2.0.2` (release, amd64, verbosity 3, applied
3) on the E7250. The device was the same SanDisk stick, on root port 13 at
SuperSpeed, driven by `usbstor`, with a low-speed USB mouse on port 1. The
dumps are in `temp/e7250-2202/` (git-ignored):

| Dump | Windows 7 x64 | XP x64 (fresh install) |
|---|---|---|
| clean boot, nothing plugged in at first | `w7start` | `xpstart` |
| mouse only | `w7mouse0` | `xpmouse0` |
| about 30 s after the stick was hot-plugged | `w7hot1` | `xphot1` |
| about 2 min later, mouse moved just before | `w7hot2` | `xphot2` |
| stick and mouse present at boot | `w7boot` | `xpboot` |

What the owner saw was the same on both systems:
- The mouse is sluggish, even with no stick plugged in.
- A hot-plugged stick gets no drive letter, even after more than 2 minutes.
- A stick present at boot hangs PnP.

The interrupt resource, read from the start of the `.BIN`'s `XHCI_EXTENSION`
(amd64: vector +44, level +48, mode +52):
- Windows 7: vector `0xB2`, IRQL 11, LevelSensitive.
- XP x64: vector `0x171`, IRQL 7, LevelSensitive. Device Manager shows IRQ
  21, shared only with the EHCI controller ("Standard Enhanced PCI to USB
  Host Controller").

The INF does not ask for MSI.

### 9.2 What the dumps show

| | w7hot1 | w7hot2 | w7boot | xphot1 | xphot2 | xpboot |
|---|---|---|---|---|---|---|
| Transfers submitted / completed | 182 / 179 | 464 / 461 | 417 / 414 | 62 / 59 | 440 / 437 | 7 / 5 |
| Stall Error / Stopped - Length Invalid | 3 / 2 | 7 / 2 | 7 / 5 | 2 / 2 | 3 / 2 | 0 / 0 |
| ISR entered / claimed | 859 / 92 | 1081 / 203 | 964 / 283 | 587 / 61 | 793 / 164 | 322 / 13 |
| Backstop drains | 65 | 202 | 175 | 29 | 164 | 3 |
| Backstop looks: event waiting with IP set / with IP clear | 459 / 0 | 1402 / 0 | 1195 / 0 | 208 / 0 | 1146 / 0 | 21 / 0 |

- **Nothing is stuck.** In every dump the oldest outstanding bulk transfer
  is 0.1-0.4 s old, the pipes' Endpoint Contexts are Running, and transfers
  keep completing. Every stall on the bulk IN endpoint is recovered: each
  RESET_PIPE takes CLEAR_FEATURE, then Reset Endpoint = Success, then
  Set TR Dequeue = Success. But each one takes 2-3 s. At the start of
  `w7boot` the first five took 0-80 ms, then the later ones slowed to 2-3 s.
- **The controller raises the interrupt, and the ISR is not called for it.**
  At every backstop sample:
  - USBCMD has R/S and INTE, USBSTS has EINT, IMAN has IP = 1 and IE = 1, and
    an event is waiting.
  - The ISR was last entered 0.4-1.0 s before.
  - The backstop never once found an event waiting with IP clear.

  With a level-sensitive line held asserted, the ISR would be called again
  and again. It is not.
  - The ISR is entered a few times a second, for the other device on the line.
  - Its last USBSTS read is `0x10` or `0`, without EINT.
  - Its claims track the backstop drains (`w7hot2` 203 vs 202, `xphot2` 164
    vs 164).
- **It fails from the very first event.** In `xpstart` and `xpboot`, backstop
  sample #1, at tick 5, shows the start's No Op Command Completion waiting
  with IP = 1 and IE = 1, and "ISR entered never". So delivery never worked,
  rather than working for a while and then stopping.
- **Finding 1, reproduced (`xpboot`).** Port 13's Enable Slot read "never
  completed" on attempts 0 and 1, and the port ended Failed, cause "no slot",
  with PORTSC still CCS and PED. This is section 3.2's mechanism. It also
  answers that section's open question, step 2: the controller did latch IP
  while masked, and the interrupt was not delivered. The No Op's completion
  waited for the backstop, and port 13's enumeration had finished failing
  before the backstop drained it. `w7boot` did not show it: every Enable Slot
  succeeded.
- **Two qualifications (Codex review).**
  - `xphot1` shows the bulk IN endpoint Halted, then Running again in
    `xphot2`: a fresh stall waiting for service.
  - Some `w7boot` recoveries took 0-80 ms, when the EHCI happened to
    interrupt.

  Starvation explains the slowness. The fix still has to show that the mount
  completes.
- **The cost.** Every event waits for the backstop. The mouse's reports
  arrive about twice a second, and usbstor's commands take several half
  seconds each. A recovery takes several control transfers and so takes
  2-3 s. That is slow enough that partmgr and the mount never finish, so
  there is no drive letter and PnP appears hung.

### 9.3 The EHCI test

- **Why the controller's INTx does not arrive.** The candidates were a
  mismatch between ACPI `_PRT` and the PCH's own routing, a masked I/O APIC
  entry, and the PCI Interrupt Disable bit. Section 9.5 settles it: the
  routing mismatch.
- **The EHCI test, done the same day (`xpnoehci`, XP x64).** The owner disabled
  the EHCI controller in Device Manager and restarted with the mouse and the
  stick plugged in. Neither was detected, and the xhci98 controller showed
  "working" with no devices under it. The dump settles it:
  - **ISR entered 0 times** in about 90 s, while IMAN showed IP = 1 and IE = 1
    and USBCMD showed INTE. **The xHCI's INTx never reaches the ISR.** Every
    ISR call in the earlier dumps was made for the EHCI on the shared line,
    and the claims were events that happened to be waiting when the EHCI
    interrupted.
  - With no ISR calls at all, the backstop drained only about every 5 s.
    Its interval did not change; its thread was blocked:
    - `hcdThread` runs `hcdBackstop` and the enumeration on one thread.
    - `hcdCommandBody` waits up to 5000 ms on `CmdDoneEvent`
      (`src/hcd_enum.c`), and only the ISR's DPC sets that event.
    - Each wait expired, counted a give-up and asked for a reset. Samples #3
      to #5 show a fresh start No Op at dequeue 0 each time.
  - So every command timed out: the mouse's Enable Slot read "never
    completed", and port 1 ended Failed, cause "no slot". `CommandsGivenUp`
    was 4. That started three controller recoveries inside ten minutes, and
    the window refused a fourth. **The controller latched FAILED**, with INTE
    and IE cleared, and the backstop was refused 534 times.
  - So on this machine the driver works at all only because the EHCI shares
    the line.

### 9.4 PCI and chipset reads (RW-Everything, XP x64, EHCI enabled)

`temp/e7250-2202/pci-idle.jpg`, `pci-busy.jpg`, `lpc.jpg` and
`rcba-3100.jpg`.

xHCI `00:14.0` configuration space:

| Offset | Value | Meaning |
|---|---|---|
| 04 Command | `0006` | memory and bus master on; **Interrupt Disable 0** |
| 06 Status | `0290` idle, **`0298` busy** | the Interrupt Status bit sets during traffic: **the controller asserts INTx** |
| 80 MSI capability | Message Control `0086` | **MSI Enable 0**; 64-bit, 8 vectors capable |
| 70 PM capability | PMCSR `0008` | D0 |
| 3C / 3D | `15` / `01` | line 21, pin INTA |

The chipset, through the LPC bridge `00:1F.0`:
- F0 is `FED1C001`, so RCBA = `FED1C000`.
- The offsets below are from coreboot's Broadwell `rcba.h`.
- A route register's low nibble (bits 3:0) routes INTA: 0-7 = PIRQA-H. In
  APIC mode PIRQA-H are GSI 16-23.

| Register | Device | Value | INTA goes to |
|---|---|---|---|
| `D29IP` (3108) / `D29IR` (3144) | EHCI | INTA / `2035` | PIRQF = **IRQ 21**, as Windows has it |
| `D20IP` (3128) / `D20IR` (3160) | xHCI | INTA / `3210` | **PIRQA = IRQ 16**; Windows has IRQ 21 |

### 9.5 Root cause, and the live test

**The BIOS routes the xHCI's INTA to PIRQA (GSI 16), and its ACPI `_PRT`
tells the OS the xHCI is on GSI 21.**
- Windows 7 and XP x64 both connect the driver's ISR on IRQ 21, the line
  the EHCI really uses.
- Every interrupt the xHCI asserts goes to I/O APIC input 16, where nothing
  listens. The IOAPIC entry for input 16 is masked, or is owned by another
  device that ignores it.
- The ISR runs only when the EHCI interrupts, and then finds the xHCI's
  events waiting. With the EHCI disabled it never runs.

That is every observation in sections 5, 8 and 9:
- the backstop's share of DPCs;
- ISR claims tracking EHCI activity;
- the sluggish mouse with nothing else plugged in;
- the start No Op stranded with IP set (Finding 1);
- I/O too slow for the mount (Finding 2);
- the controller failing outright without the EHCI.

**The live test, 2026-10-08.**
- The owner wrote `D20IR` = `3215` (INTA to PIRQF) with RW-Everything, as a
  16-bit write at `FED1F160`, on the running XP x64 system with the EHCI
  enabled.
- The mouse became smooth at once.
- A hot-plugged SanDisk stick then mounted with a drive letter.
- A restart restores the BIOS value. The dump `XPFIXED` was taken after
  the change, before the restart.

Why this has not been seen before: Intel's Windows 7 xHCI driver, and
Microsoft's from Windows 8, enable MSI. MSI does not use PIRQ routing, so the
wrong `D20IR` never mattered to them. XHCIQUAL under DOS reported line 11,
which is the BIOS's legacy PIC routing and is not affected.

### 9.6 Fix candidates (owner to choose; Codex consulted)

- **A routing quirk.** On Intel PCHs that keep `D20IR` in RCBA (8 and 9
  series, -H and -LP), read `D20IR` at start and on every return to D0. If
  its INTA PIRQ does not match the GSI the OS assigned, write the IAR field
  to match.
  - Gate it by device ID.
  - Record what it found and did in the dump.
  - Add a registry value to turn it off.
  - Open questions: the GSI on each OS (the raw interrupt resource; PIC mode
    on Windows 98, 2000 and XP without an APIC); S3 resume; other devices on
    PIRQA.
- **MSI on NT6** through `IoConnectInterruptEx`. This HCD connects its own
  interrupt, so the work is in this driver, not just an INF setting. It
  does not help Windows 98, 2000 or XP.
- **A polling safety net** for an interrupt that stays dead.
  - An independent 10-20 ms timer drain that does not depend on
    `hcdThread`.
  - A BUSY retry for the start No Op.
  - Polling kept across a recovery.
- XHCIQUAL could detect and report the mismatch from DOS.
- Separately, the 4.6.8 order in `hcdCfgReset` (section 8.3), as a spec fix.

## Sources

- `temp/e7250/` (git-ignored): `MYDUM`, `MYDUMP`, `BEFOREUSB`, `AFTERUSB`,
  `PROBE.LOG`.
- `temp/e7520/` (git-ignored): the owner's `2.2.0.0` Windows 7 dumps.
- `temp/e7250-2202/` (git-ignored): the owner's `2.2.0.2` dumps, Windows 7
  x64 (`w7*`) and XP x64 (`xp*`, `xpnoehci`, `xpfixed`), and the RW-Everything
  screenshots.
- coreboot, `src/soc/intel/broadwell/include/soc/rcba.h`: the DnnIP and DnnIR
  offsets and the DIR_ROUTE layout.
- `src/hcd_ctl.c` (`hcdStart`, `hcdThread`, `hcdPoll`, `hcdBackstop`),
  `src/hcd_enum.c` (`hcdCommand`), `src/xhci_cmd.c`, `src/xhci_evt.c`
  (`XhciEnableInterrupts`), `src/xhci_init.c`, `src/hcd_cfg.c`.
- `src/uas/uas_fdo.c`, `src/uas/uas_xport.c`, `src/uas/uas_iu.c`,
  `src/uas/uas.h`.
