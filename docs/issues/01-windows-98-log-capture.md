# Issue 1 - Getting a kernel log off a Windows 98 machine

Status: solved, shipped in `0.0.0.6`.

Targets affected: Windows 98 SE on real hardware. The Windows 2000 target and
the QEMU guests were never affected.

The short version: Sysinternals DebugView, the tool every Windows 9x driver
author reaches for, bugchecks a real Windows 98 machine the moment a USB
device is plugged in while this driver is loaded. Even when it doesn't crash,
there is no moment in this driver's life on that target when a capture could
be running. The way out was a read channel rather than a new sink: a bounded
ring buffer inside the driver's own extension, pulled out from user mode
through a usbport vendor escape (`IOCTL_USB_USER_REQUEST` /
`USBUSER_PASS_THRU`). The IOCTL numbers had to be recovered by disassembling
`USBPORT.SYS`, because no DDK this project has ships the header that defines
them. The user-mode side is `XHCISNAP.EXE`.

This page is the story. The as-built contract is in
[passthru-snapshot-instrument.md](../contributing/passthru-snapshot-instrument.md)
and the design argument in
[design record 08](../contributing/design/08-build-flavours-and-the-log-channel.md).
Where this page and those disagree, they win.

---

## 1. The problem

A driver that doesn't work on real hardware needs to say why, and on Windows
98 this driver couldn't say anything at all.

`xhciDbgEmit` in `src/xhci_dbg.c` writes every debug line two ways: a byte at
a time to I/O port `0xE9` (QEMU's debug console, captured to a file on the
host) and through `DbgPrint`. In a VM that is a perfect trace. On real
hardware nothing decodes port `0xE9`, so `DbgPrint` is the only sink that
survives the trip to metal. On Windows 98 `DbgPrint` isn't the kernel's own;
it is NTKERN's WDM emulation of it, and where the output goes is not
documented.

There is a further problem that is specific to this driver rather than to
Windows 98. A usbport miniport does not own a driver object.
`USBPORT_RegisterUSBPortDriver` overwrites seven `MajorFunction` slots of the
miniport's driver object (`IRP_MJ_CREATE`, `IRP_MJ_CLOSE` and
`IRP_MJ_DEVICE_CONTROL` among them) before it even checks the miniport's
version, in every shipping `usbport.sys`. So the usual way a Windows 98 driver
exposes a log (create a device object, publish a `\DosDevices\` link, answer a
private IOCTL) is off the table. DebugView itself is such a driver-plus-client
pair: `Dbgview.exe` carries an embedded VxD, `DBGDD`, and the names
`\Device\Dbgv` / `\DosDevices\Dbgv`. It can log because it owns a driver
object. This driver runs inside somebody else's.

Design record 08 puts it in one line: "Other Windows 98 drivers can log
because they own a driver object. This one runs inside somebody else's."

## 2. How it was discovered: four failures, in the order they were met

It would be easy to compress these into "DebugView does not work on Windows
98". That sentence is false, and it was measured to be false, so the four
failures are kept apart.

### 2.1 The worry that turned out to be unfounded (VM)

Sysinternals documents that on Windows 95/98/Me DebugView captures the VxD
services `Out_Debug_String` and `_Debug_Printf_Service`, and captures
`DbgPrint` only on NT-based systems. Since Win98's `DbgPrint` is NTKERN's
emulation, it was an open question whether this driver's lines would arrive
at all.

Measured in the Windows 98 VM with Capture Kernel enabled: DebugView v4.64
displays the driver's `xhci98:` lines, and each visible line was cross-checked
as present in the `0xE9` log taken at the same moment. Both sinks carry the
same content.

Note the build. The 9x-capable DebugView is v4.64 (`Dbgview.exe` dated
2007-01-08). The live Sysinternals download is a Win7+/Win10+ build that will
not run on Windows 98 at all; the usable one comes from the Internet Archive
and is kept, git-ignored, in `tools/DebugView/`. See
the DebugView notes in
[build-and-test.md](../contributing/build-and-test.md).

### 2.2 The bugcheck on metal (three observations, each widening the ban)

First bare-metal run, ThinkPad E460 (Skylake, `8086:9D2F`, xHCI-only),
Windows 98 SE + NUSB 3.3. With DebugView capturing, plugging any hub
bugchecked the machine: fatal exception 0E at 0028:C208D79D, both hubs,
reproducibly. With DebugView closed the same hub on the same connector worked.
An apparent left-connector/right-connector dependence was chased for a while
and turned out to be a red herring; the A/B showed DebugView was the variable.

The ban then widened. A plain USB audio device on a root port, no hub
anywhere, bugchecked the same way at `0028:C207B26D`. Then a Low-Speed mouse
(`046D:C077`) on a root port, at `0028:C20A3F4D`. Three device classes,
three addresses in the same region.

| Date | Device | Fault address |
|---|---|---|
| first | hub, both units | `0028:C208D79D` |
| second | USB audio, root port | `0028:C207B26D` |
| third | Low-Speed mouse, root port | `0028:C20A3F4D` |

The likely mechanism is `DbgPrint` from DPC and ISR context meeting a Win9x
VxD capture hook at real interrupt rates, which QEMU survives because it is
slower and more serialised. This is still an inference, not an established
fact. A discriminating boot that would have settled it was dropped, and a
dropped boot is not evidence in either direction.

### 2.3 The one-shot sink that fires after the viewer has closed (VM)

If per-line `DbgPrint` crashes the machine, hand DebugView the whole log at
once, at a safe moment. Task 11-V.9 added `XhciLogDebugView`: a registry
switch that dumps the driver's ring buffer through `DbgPrint` in 256-byte
chunks at the PASSIVE-level flush in `StopController`.

Stage H of the Phase 11 run killed it. The lesson is in
[lessons.md](../contributing/lessons.md) under "Stage H on 2a": a sink is not
reachable until the moment it fires is reachable. The ring is drained only at
`StopController`. The only stop Windows 98 offers is a shutdown, since a
Device Manager disable bugchecks the target (release notes, "Known limitations").
And Windows closes DebugView before it stops the driver. Measured: DebugView
v4.64 capturing to file, demonstrably receiving 707 of the driver's lines,
still open at shutdown, and its saved capture contains zero occurrences of the
flush, while the `0xE9` trace of the same shutdown records
`flush emitted bytes to DebugView=000007F6`. The driver emitted 2,038 bytes
into nothing.

The same batch had already killed the ring-0 file sink. `\??\C:\XHCI.LOG`
does not resolve under NTKERN, `\DosDevices\` and `\SystemRoot\` refuse a
read with `STATUS_NOT_SUPPORTED`, and asking `\DosDevices\C:\XHCI.LOG` for
write access never returns and hangs the boot inside `StartController`
(lessons.md has the entry). A serial sink was the last candidate on paper and
was withdrawn: no xHCI-era laptop has an RS-232 port, and WinDbg's KD
protocol does not speak to Windows 9x anyway.

Task 12.2 closed with the answer "there is none". No PASSIVE moment exists
between `StartController` and shutdown on a Windows 98 machine running this
package, so any sink that needs one has nowhere to fire, and the alternative
is the per-line profile that crashes the machine. `AGENTS.md` carries the
rule that fell out: `DbgPrint` only inside `#if DBG`, with the one-site
`XhciLogDebugView` flush as the sole exception, never to be widened.

One PASSIVE emit that is not the shutdown does exist, and it's named here so a
later reader doesn't rediscover it as an opening. The failed-start flush
(`XHCI_LOG_REASON_FAILURE` in `xhciStartController`) runs at PASSIVE and
reaches whichever sinks are configured. It fires only when the start fails,
and on Windows 98 that happens during boot, before a capture can be running.
So it is not a trace channel for a controller that works, which is what task
12.2 was about.

### 2.4 There was nothing to capture anyway (E460)

Finding C of the batch 13-E bench session: even a DebugView that did not
crash could not have shown the interesting part. Every per-transfer trace
site in the debug build is `XHCI_DBG_VALUE_CHANGED` or
`XHCI_DBG_VALUE_LIMITED`, capped at 32 emissions per site and spent within
seconds of driver load; the budgets are image statics that no stop/start
resets. The driver loads at boot; DebugView can only be started after the
desktop appears. On Windows 98 metal you cannot start a capture before the
driver's only dense trace has already happened. It's the same ordering
failure as 2.3, arriving from the other end of the driver's life.

The same session found (Finding A) that the published debug flavour of
`0.0.0.4` did not even load on the E460: yellow bang, Code 2, with one import
differing from the release flavour, `HAL.dll!WRITE_PORT_UCHAR`, the `0xE9`
writer. So the build with the diagnostics in it had never actually run on the
target that needed them. Design record 08 calls this the project's own defect,
not Windows 98's: "it is the reason the project has spent months concluding
that Windows 98 cannot be instrumented when what was actually true is that its
instrumented binary had never run there." The three-flavour split (`release`
/ `debug` / `debug-e9`) is what fixed it.

## 3. How it was troubleshot: the turn from "record" to "read"

The turn came from the other issue in this folder. At bench session 2 the
E460 was wedged by [issue 2](02-bare-metal-wedge-and-portsc-watchdog.md), and
the investigation had spent five cold boots on candidate remedies without
once reading the register it was theorising about. The counters and the note
ring that would have answered it were sitting in RAM on a machine with no
sink. The observation in `run-13e.md` after Finding Q is the pivot: "What is
missing is a way to read, not a way to record."

Reading needs a path from user mode into the miniport, and the miniport owns
no door. But usbport does, and it forwards. `IOCTL_USB_USER_REQUEST` carries a
request code `USBUSER_PASS_THRU` that usbport hands straight to the miniport's
`PassThru` slot (packet offset `0xE0`), which this driver registers. The
Win2000 DDK in `tools/ntddk` has no `usbuser.h`, so the numbers were recovered
from the binaries the same evening, with no bench time:

| Question | Answer | Read from |
|---|---|---|
| Which IOCTL | `0x00220438`, `METHOD_BUFFERED`, `FILE_ANY_ACCESS` | usbport's comparison chain (NUSB `0002E00C`, SP4 `0002E8E6`) |
| Which request code | `UsbUserRequest = 3` | the 8-entry jump table (NUSB `00027E9E`, SP4 `00028528`) |
| Buffer shape | in length == out length, >= `0x28`; 16-byte header, GUID at `+0x10`, `ParameterLength` at `+0x20` | the dispatcher |
| Openable name | `\\.\HCD<n>` - usbport unconditionally `IoCreateSymbolicLink`s `\DosDevices\HCD<n>` at FDO start | NUSB `00011D19` |
| Gated by test mode? | No - the gate only fires for requests with `0x30000000` set | the dispatcher |

The derivation is in
[usbport-miniport-abi.md](../usb-xhci-info/usbport-miniport-abi.md) ("Debug /
single-packet"). The kernel side (`xhciPassThru` in `src/xhci_dispatch.c`) and
the user side (`xhcisnap/xhcisnap.c`, built with the in-repo MSVC 6.0) were
written that night behind `XHCI_OBS_SNAPSHOT`, and the derived route executed
as derived. The next morning it read the root cause of issue 2 off the wedged
machine.

## 4. How it was solved: the channel that ships

The instrument was then deleted (task 13-R.4) so that an unofficial escape
hatch built on an undocumented vendor IOCTL would not ship in `0.0.0.5`.

It came back the next day, once batch 13-L worked through what the log
question actually was (design record 08). The reasoning that brought it back:
it is the only mechanism that has ever carried this driver's own evidence off
a Windows 98 machine; usbport's door is already open, so the driver adds no
attack surface it did not already have; and a channel that is switched off
answers the same way as a binary built without one (`MP_STATUS_NOT_SUPPORTED`,
value 6, which usbport's own root-hub probe depends on receiving; see rule 1
in the instrument document).

What shipped in `0.0.0.6` (task 13-L.2):

- Kernel side. A 16 KB note ring (`src/xhci_log.c`, `XHCI_LOG_RING_BYTES`)
  inside `XHCI_EXTENSION`. `xhciPassThru` serves a windowed copy of the whole
  extension plus a raw, unacknowledged PORTSC array. That is the one
  sanctioned exception to the "reading PORTSC obliges you to acknowledge it"
  invariant, because an instrument that acknowledged what it came to measure
  would destroy the evidence. usbport refuses `ParameterLength > 0x10000` and
  the extension is ~90 KB, so the reader loops on an offset, with a tear
  detector built from counters read inside the lock.
- The switch. A registry ladder `XhciLogVerbosity` 0-4 (0 off, 1 counters,
  2 adds the note ring, 3 adds the PORTSC table, 4 everything including kernel
  addresses); out-of-range values are refused rather than clamped. This also
  retired a defect the instrument had been working around. `Log.Enabled` used
  to be derived from naming a sink, so the maintainer's first operating trap
  was "set `XhciLogDebugView=1`; it does not have to reach anywhere",
  switching on a sink known to be dead so that the ring would fill. Not an
  instruction a user can be given.
- User side. `XHCISNAP.EXE`: `-verbosity N`, `-dump`, `-o NAME`, `-probe`,
  `-disable`. Writes `NAME.BIN`, `NAME.PSC` and a plain-text `NAME.TXT`, the
  one a user pastes into an issue. Present in every shipping flavour,
  including `release`.
- Measured on the E460, seven cold boots, both shipping flavours, each
  reading back its own image: the first time a shipping binary carried this
  driver's own log off a Windows 98 machine.

The user-facing procedure is three commands and none of them is `regedit`
(release notes, "The log, and how to send one"):

```text
XHCISNAP -verbosity 2
(restart, reproduce)
XHCISNAP -o C:\MYDUMP
```

## 5. Known limits

- The route can be silently absent. usbport publishes `\DosDevices\HCD<n>`
  at a fixed index with no retry. On a machine where Windows 98's own USB
  stack already owns `HCD0` (any UHCI/OHCI it drives), no usbport link ever
  appears; the driver runs fine and only the reading channel is missing.
  Measured in the VM. `XHCISNAP -probe` reports it.
- The DebugView crash is still an inference. Three addresses and three
  device classes are measured; the mechanism is not. Don't publish the VxD
  hook theory as fact.
- The debug flavour's Code 2 on the E460 is still open, with two live
  readings: the import does not resolve, or something on that chipset decodes
  port `0xE9` and the write faults. The `debug-e9` flavour exists so the
  question can stay open without blocking anyone.

## 6. Lessons the record kept

- A sink is not reachable until the moment it fires is reachable.
- "The sink is reachable" and "the sink is safe" are different claims; the VM
  established only the first.
- An instrument pays for itself the first time it disagrees with an inference.
- When a driver cannot own a door, look for the one its host already answers
  on its behalf.

## Sources

`docs/contributing/design/08-build-flavours-and-the-log-channel.md`;
`docs/contributing/passthru-snapshot-instrument.md`;
`docs/contributing/build-and-test.md` ("Getting a trace off a bare-metal
machine", the DebugView ban); `docs/contributing/runs/run-13e.md` (session
record for the DebugView check, Findings A-C);
`docs/contributing/lessons.md`'s Windows 98 file-write entry;
`docs/contributing/roadmap.md` tasks 11-V.7, 11-V.9, 12.2, batch 13-L;
`docs/using/release-notes.md` ("The log, and how to send one", "DebugView").
