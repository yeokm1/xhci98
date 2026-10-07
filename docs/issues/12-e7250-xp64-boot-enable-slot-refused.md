# Issue 12 - On a Dell E7250 under Windows XP x64, a SuperSpeed reader present at boot fails "no slot", and hot-plugged it binds `xhciuas` but shows no disk

Status: **open.** A user report against `2.2.0.0`, read on 2026-10-07 from
XHCISNAP dumps and the source. Finding 1 (section 3) has a cause read from
the code and consistent with every counter, and no fix yet. Finding 2
(section 4) is not diagnosed; the leading reading is unconfirmed, and the
user will provide no further information. The work is roadmap Phase 36
(`roadmap-hcd.md`, opened 2026-10-07 on branch `2.2.1.0`): an interim build
from this evidence, checked again on the E7250.

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

## 5. Interrupt delivery is poor on this machine

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

## Sources

- `temp/e7250/` (git-ignored): `MYDUM`, `MYDUMP`, `BEFOREUSB`, `AFTERUSB`,
  `PROBE.LOG`.
- `src/hcd_ctl.c` (`hcdStart`, `hcdThread`, `hcdPoll`, `hcdBackstop`),
  `src/hcd_enum.c` (`hcdCommand`), `src/xhci_cmd.c`, `src/xhci_evt.c`
  (`XhciEnableInterrupts`), `src/xhci_init.c`, `src/hcd_cfg.c`.
- `src/uas/uas_fdo.c`, `src/uas/uas_xport.c`, `src/uas/uas_iu.c`,
  `src/uas/uas.h`.
