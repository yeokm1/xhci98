# Issue 6 - A Full-Speed device on a root port bugchecks Windows 98 and 2000, and what reporting every root port as High Speed costs on every target

Status: **open.** The bugcheck this page is named for, on Windows 98 SE and
Windows 2000, has been worked around since roadmap Phase 5 task 7, before the
first release: the driver reports every connected root port to usbport as
High Speed and keeps the true speed for its own contexts. That workaround is
not a fix, and it has costs, which have grown with every Windows added
after the two it was made on. Usbport buckets interrupt intervals on the
wrong speed everywhere (section 5), and since 2026-09-20 one cost is visible
to a user rather than only in a trace: the controller's own **Bandwidth
Usage** figures charge a Full-Speed and a High-Speed root-port device the
same 1 %, on five USB 2.0 stacks across four operating systems - Windows 98
SE, ME, 2000 and XP (section 5.1). **Two further costs
were measured on
2026-09-19 and are known limitations of `1.1.0.0`, and of `1.1.1.0` after
it:** a USB 1.1 hub on a
root port bugchecks every Vista and Windows 7 build once a slower device
behind it is configured (section 6), and a Full-Speed USB audio device on a
root port plays nothing from Windows XP on (section 7). The candidate fix
for all of them, a virtual USB 2.0 hub per root port
([proposal](../future-plans/virtual-hub-per-root-port.md)), is not yet
decided (section 8). **`1.1.1.0` answers nothing on this page.** That
release carries the controller's property page and the interrupt moderation
value alone; the polling rates (section 5) and true speeds on root ports were
split out of Phase 23 by the owner on 2026-09-22 and are roadmap Phase 24's,
added on 2026-09-24 and open, and the speed report, its bands and both costs
are the same in `1.1.1.0` as in `1.1.0.0`.

Targets affected: all ten. The bugcheck itself was measured on Windows 98
SE under the NUSB stack and on Windows 2000 SP4, in QEMU virtual machines;
the same usbport logic is in both binaries, and SweetLow's XP-lineage
rebuild has the same unguarded branch (static). No other target ever ran
the truthful-speed build: the workaround predates every bare-metal batch
and every target added after Phase 5. What the workaround costs on each
target is section 1's table.

**Where each reading was taken matters on this page, and the table says
so cell by cell.** Windows 98 SE is the target with the long real-hardware
record (the ThinkPad E460 and P14s in batches 13-E and after, and users'
desktops). 32-bit Windows 7 SP1 has one real-hardware session, on the
E460 on 2026-09-19 (`run-22.md` 22.9), which measured both of the
limitation's workarounds and the audio limitation itself. Every other
target - Windows ME, 2000, XP, XP x64, Vista and 64-bit Windows 7 - has
only ever run in QEMU, whose only hub is a Full-Speed one; so the Vista
and 7 hub bugcheck is still a virtual-machine reading everywhere. On
Windows 98 the workaround's ordinary case has been on metal: a Low-Speed
mouse and a Full-Speed audio device on a root port and behind High-Speed
hubs on the E460 (batch 13-E), and the audio device again on `1.1.0.0`
itself, directly in a root port, on 2026-09-19. What has never been on metal, on any
target, is the topology sections 6 and 7 turn on - a USB 1.1 hub on a root
port with a slower device behind it - because no USB 1.1 hub was ever held
(batch 13-E, decision P3); the E460's hubs are USB 2.0, single-TT and
multi-TT.

The short version: usbport applies the EHCI model to a USB 2.0 miniport. On
EHCI a root port can only ever hold a High-Speed device, because Full and
Low Speed devices are handed to a companion controller, so when usbport is
told a root port holds a Full-Speed device it goes looking for the
transaction translator that model guarantees. There is none, the lookup
returns a garbage pointer instead of NULL, and the kernel faults on the
first list insertion. Reporting the true speed is therefore fatal on both
shipping usbport builds, and the driver lies at exactly one layer to avoid
it. The newer stacks accept the lie for a device on a root port and choke
on it one level down: Vista and 7 take a USB 1.1 hub on a root port for a
High-Speed hub, find no translator on it, and fault budgeting the device
behind it; XP and later schedule a Full-Speed audio stream on a root port as
a High-Speed one and never send it. This page is the story; the
instruction-level chain is in
[usbport-miniport-abi.md](../usb-xhci-info/usbport-miniport-abi.md) section
8, "The transaction-translator lookup", and the reporting rule in
[implementation-invariants.md](../contributing/implementation-invariants.md),
"Root Hub Reporting". Where this page and those disagree, they win.

---

## 1. The problem, on every target

Four questions per target: what a Full-Speed device on a root port does
when its true speed is reported (the pre-Phase-5-task-7 build, run on the
two primary targets, and once more on 2026-09-25 as an uncommitted
experiment under SweetLow's stack); and, under the High-Speed report every
release has shipped, what a slower device on a root port does, what a USB
1.1 hub on a root port with a slower device behind it does, and whether a
Full-Speed audio device on a root port plays. Every cell names its
measurement and where it was taken: **VM** is a QEMU guest, **metal** is a
physical machine (the ThinkPad E460 unless another is named). "Never run"
means exactly that, and "not read" means no static reading of that usbport
either. Only the first row and 32-bit Windows 7's have any metal readings.

| Target | Full-Speed device on a root port, true speed reported | Full or Low Speed device on a root port, reported High Speed (as shipped) | USB 1.1 hub on a root port, Full or Low Speed device behind it | Full-Speed audio device on a root port |
|---|---|---|---|---|
| Windows 98 SE, NUSB 3.3 | **Bugchecks** (VM): `Windows protection error`, or `0028:C002F70E` in `NTKERN` (Phase 5, section 2). Never on metal: the workaround predates every bare-metal batch | Works, VM and **metal**: a Low-Speed mouse at a root port on the E460 (batch 13-E, stage E4.1) and a Full-Speed audio device there (E6.1); interrupt polling in 1, 2 or 4 ms bands (section 5; measured with hidusbf in the VM, Phase 20 - not measured on metal); the Bandwidth Usage dialog charges it the same 1 % as a High-Speed device (VM, 2026-09-20, section 5.1) | Works (VM): a mouse behind QEMU's `usb-hub` bound and ran (batch 7b-V0, section 6.1); the 22.9 hub rows PASS, the churn row is excluded on this target. Never on metal: no USB 1.1 hub was held (batch 13-E, P3). On metal the mouse and the audio device ran behind USB 2.0 hubs, single-TT and multi-TT (E4, E6.2), which is not this topology | **Plays on metal**: a physical UAC 1.0 device played clean at a root port on the E460 (batch 13-E, E6.1, and behind the multi-TT hub, E6.2), and again on `1.1.0.0` `release-x86` on 2026-09-19 - a Sound Blaster Play! 2 (`041E:323D`) directly in a root port, NUSB 3.3, heard by the owner. In the VM the OS's own `USBAUDIO.VXD` faults after one URB (batch 9-V, again 2026-09-19) - a vehicle artefact, not this issue |
| Windows 98 SE, SweetLow's stack | **Bugchecks** (VM, 2026-09-25): a fatal exception 0E at `0028:C002F70E` in `NTKERN`, the NUSB row's address, once a Full-Speed mouse on a root port was reset (an uncommitted truthful build, roadmap 24.3, `runs/run-24.md`). Static: its usbport's single-TT branch (`0x2667A`-`0x26686`) returns the same garbage pointer. Never on metal | Works (VM, observed); the bands not measured on this stack; the same 1 % in the Bandwidth Usage dialog (VM, 2026-09-20, section 5.1) | Not measured | Not measured |
| Windows ME, SweetLow's stack | Never run on ME; the same usbport as the row above, whose branch is read statically and whose crash was run on Windows 98 SE only | A HID mouse binds (VM, 2026-09-02); the bands not measured; the same 1 % in the Bandwidth Usage dialog (VM, 2026-09-20, section 5.1). Never on metal | Never plugged | Bound (VM, a composite audio device, 2026-09-02); playback not measured |
| Windows 2000 SP4 | **Bugchecks** (VM): `STOP 0x0000000A (0xFFFFFFFC, 0xFF, 0x00000000, 0x804006B2)` (Phase 5, sections 2 and 3) | Works (VM); the same bands (usbport's bucketing rule is common to every build; the readings are Windows 98's). Never on metal | Works (VM: batch 7b-V0; the 22.9 churn row PASS) | **Plays** (VM): 376 isochronous submits, 3,760 packets, `played.wav` 659,456 B (2026-09-19, section 7) |
| Windows XP SP3 x86 | Never run; XP SP3's own `USBPORT_GetTt` not read (SweetLow's rebuild is XP-lineage and unguarded) | Works (VM: issue 7's legs; the 22.10 install leg). Never on metal | A Full-Speed audio device behind the hub enumerated and played (VM, 2026-09-19, section 7); a mouse behind it never run | **Silent** (VM): 0 isochronous submits while Sound Recorder played 1.93 s; behind a Full-Speed hub 196 submits, 344,064 B (section 7) |
| Windows XP x64 SP2 | Never run; not read | Works (VM: the 22.9 matrix; the 22.10 install leg). Never on metal | Works (VM: the 22.9 hub rows, churn included, PASS) | **Silent** (VM): 0 submits; an endpoint opens on arrival, nothing is ever sent (section 7) |
| Windows Vista SP2 x86 | Never run; not read | Works (VM: the 22.10 install leg, three devices on a root port, disable / enable / remove / rescan). Never on metal | **Bugchecks** (VM): `STOP 0x0000007E` in `usbport!Allocate_time_for_endpoint` once a mouse behind it is configured (2026-09-19, section 6.2) | **Silent** (VM): 0 submits; the pipe opened as playback began (section 7) |
| Windows Vista SP2 x64 | Never run; not read | Works (VM: the 22.10 install leg). Never on metal | **Bugchecks** (VM), the same function, the pointer-sized offset (section 6.2) | **Silent** (VM): 0 submits (section 7) |
| Windows 7 SP1 x86 | Never run; not read | Works, VM (the 22.9 matrix, HID, storage, the hub alone; the 22.10 install leg) and **metal** (E460, 2026-09-19: the Low-Speed mouse and a USB 2.0 stick at a root port) | **Bugchecks** (VM: the 22.9 churn row; section 6.2). On **metal** only the workaround was measured: behind USB 2.0 hubs (`1A40:0201` multi-TT, `1A40:0101` single-TT) the Low-Speed mouse ran with **no bugcheck** (2026-09-19) | **Silent**, VM (0 submits; the isochronous pipe never opened) and **metal** (E460, 2026-09-19: a C-Media `0D8C:0014` bound with no error and played nothing at a root port, and **played** behind the USB 2.0 hub) (section 7) |
| Windows 7 SP1 x64 | Never run; not read | Works (VM: the 22.10 install leg). Never on metal | **Bugchecks** (VM, section 6.2) | **Silent** (VM): 0 submits; the pipe opened, nothing sent (section 7) |

Read down the columns. The first is why the workaround exists and why it
cannot simply be removed: the two primary targets die without it, and so
does Windows 98 under the reporter's own stack. The
second is the workaround doing its job on every target, at the cost of the
bands. The third and fourth are what the same report costs on stacks it was
never made for: Vista and 7 fault one level below a root port, and every
stack from XP on schedules a Full-Speed isochronous stream on a root port
as if it were High Speed. Windows 2000 is the only target on which
everything works, and the reason its usbport plays the stream where XP's
does not has not been read (section 9). And real hardware reaches only
two rows: Windows 98's root-port and audio columns, and 32-bit Windows
7's, where the one E460 session measured the audio limitation and both
workarounds. Every other row is a QEMU reading, and so is every 1.1-hub
bugcheck, because no USB 1.1 hub has ever been on metal.

## 2. How it was found, and pinned to speed (Windows 98 and 2000, Phase 5)

The Phase 5 root-hub checkpoint plugged Low, Full and High Speed devices
into the QEMU guests and watched port status, the asynchronous reset, the
decoded speed and the change bits. Everything the driver was responsible for
was right: connect, `C_PORT_CONNECTION` latched and cleared, reset completed
by PRC, `C_PORT_RESET` latched and cleared, a steady `0x0103` (connected,
enabled, powered, Full Speed) at the moment usbport took the device. Then
the machine died.

| Target | Device | Speed | Result |
|---|---|---|---|
| Windows 98 | `usb-audio` | Full | `Windows protection error`, or `0028:C002F70E` in `NTKERN` |
| Windows 98 | `usb-kbd` | High | healthy |
| Windows 2000 | `usb-audio` | Full | `STOP 0x0000000A (0xFFFFFFFC, 0xFF, 0x00000000, 0x804006B2)` |
| Windows 2000 | `usb-kbd,usb_version=1` | Full | the same STOP, the same four parameters |
| Windows 2000 | `usb-kbd` | High | healthy |

The first four runs confounded speed with class: every Full-Speed test was
an audio device and every High-Speed control a keyboard. One run with a
Full-Speed keyboard reproduced the Windows 2000 bugcheck with byte-identical
parameters and separated them. Speed was the variable, on both targets.

Twice in that session a complete, correct driver trace was read as a pass
and a screenshot then showed the bugcheck. The trace ends where the driver's
involvement ends, not where the system dies.

## 3. The mechanism: usbport looks up a transaction translator that does not exist

Static, on Windows 2000's four parameters. `0x804006B2` is `ntoskrnl.exe`
RVA `0x6B2`: the third instruction of `ExfInterlockedInsertTailList`,
`mov eax,[ecx+4]`, reading `ListHead->Blink`. `0xFFFFFFFC` is that read
with `ecx = 0xFFFFFFF8`. The IRQL `0xFF` is not a real IRQL; the routine's
own `pushfd; cli` is why. Among the USB stack only `usbport.sys` imports
that routine, which pins the caller.

The producer is `USBPORT_GetTt`. `USBPORT_CreateDevice` calls it for any
device that is not High Speed when the miniport declared
`USB_MINIPORT_FLAGS_USB2`. It walks up the hub handles to the first
High-Speed one, and on its `TtCount <= 1` branch takes `CONTAINING_RECORD`
of the TT list head unconditionally. An empty list gives `0xFFFFFFEC`, not
NULL. `USBPORT_OpenPipe`'s null check passes it, adds `0xC`, and hands
`0xFFFFFFF8` to the kernel's list insertion. Two facts make it deterministic
rather than unlucky:

- `USBPORT_RootHubCreateDevice` marks the root hub's own handle High Speed
  exactly when the miniport declares the USB2 flag, so the upward walk stops
  at the root hub instead of running off the top to the safe NULL exit.
- usbport's root-hub device descriptor template hardcodes
  `bDeviceProtocol = 0`, a hub with no transaction translator, and only
  `bcdUSB` is patched. No hub driver ever initialises a TT for the root hub,
  so its list is empty for the life of the controller.

The multi-TT branch twenty instructions away does test for an empty list.
ReactOS's `USBPORT_GetTt` has early returns for both cases; neither shipping
binary has them. A ReactOS read is interface documentation, not evidence of
safety, and this is the second place in this project where it added a guard
the shipping code lacks.

On genuine EHCI the combination is unreachable: a Full or Low Speed device
on an EHCI root port is released to a companion controller, and usbport is
never asked to create a non-High-Speed child of the EHCI root hub. The
companion is UHCI or OHCI at the chipset vendor's choice (Intel and VIA
paired EHCI with UHCI, most others with OHCI), and usbport does not care
which: the port's ownership bit flips to the companion, whose own miniport
(`usbuhci.sys` or `usbohci.sys`) enumerates the device under its own root
hub. Only an xHCI miniport, which has no companion and owns every speed on
every port, can reach this code.

The same model is what costs sections 6 and 7 on the newer stacks. Their
usbport is built on the same assumption - nothing slower than High Speed
ever sits directly on a USB 2.0 root port - and each part of it that
touches speed (device creation here, bandwidth budgeting on Vista and 7,
isochronous scheduling from XP on) trusts whatever the miniport reported.

## 4. The remedy that was refuted, and the workaround chosen

The leading hypothesis named the right mechanism, the EHCI companion model,
and the wrong lever: declare `MiniPortVersion` as xHCI rather than EHCI.
ReactOS switches the root-hub descriptor type on that version. Both shipping
builds copy a fixed seven-byte hub descriptor template and contain no
version branch at all, so declaring the xHCI version changes nothing here.
`USB_MINIPORT_FLAGS_USB2` is the only lever, and it has two positions:

- **Drop the flag.** Structurally immune: without it `CreateDevice` never
  calls `GetTt` in any topology. But the root hub then enumerates as
  `USB\ROOT_HUB`, NUSB's `USB2.INF` claims only `USB\ROOT_HUB20`, and Windows
  98 binds its own USB 1.1 `usbhub.sys`, which extracts one speed bit from
  the port status and has no notion of High Speed. High Speed is lost on the
  primary target.
- **Keep the flag and never report a root port as anything but High Speed.**
  `CreateDevice` then skips the lookup for every root-port device, because
  the lookup is gated on the reported speed.

The second was chosen, and ran the same day on both targets: Full-Speed HID
and Full-Speed audio plugged, reset, decoded Full Speed, reached
`QueryEndpointRequirements` and `OpenEndpoint` (the very path that had run
off the null base), and unplugged, with no bugcheck and every failure
counter at zero. High Speed unchanged. The pre-fix binary was archived so
the comparison is reproducible rather than remembered.

It is a workaround, not a fix: usbport's lookup is still there, still
unguarded, and the driver simply never lets a root-port device reach it.
Every later section is what that costs.

## 5. How the workaround is confined, and what it costs on every target

The lie lives in one place, `XhciPortShadowReport` in `src/xhci_port.c`: a
connected managed root port sets the High-Speed status bit whatever the
decoded speed, and the Low-Speed bit is never set at all. Devices behind
an external hub report their true speed, whatever the hub is; a genuine
2.0 hub really has a TT, and a 1.1 hub is section 6's subject.

usbport derives four things from the speed it believes, and the trade-off
is what happens to each:

- **The device's own programming: corrected.** The Slot Context speed,
  EP0's max packet size and every Endpoint Context are programmed from
  the decoded speed the driver keeps in the port shadow; usbport's
  `DeviceSpeed` is never used for them. This is what makes the devices
  work at all, and Low-Speed HID and Full-Speed audio have both run on
  real silicon under it (Windows 98 SE on the E460, batch 13-E: the mouse
  at a root port and behind the hubs, the audio device playing at a root
  port and behind the multi-TT hub).
- **The TT fields for devices behind a hub on a root port: corrected**, in
  Phase 7b, by deriving them from the driver's own topology graph (section
  6.1). Corrected for the driver's contexts, that is; usbport's own use of
  the missing TT is section 6.2.
- **The interrupt interval: lost.** Below.
- **Periodic bandwidth accounting: unmeasured on Windows 98 and 2000,
  fatal in one topology on Vista and 7.** usbport budgets a
  believed-High-Speed device against the High-Speed bus budget rather
  than a frame budget. The xHC does its own admission check when an
  endpoint is configured, and no symptom has been seen on the two primary
  targets, but no run has loaded a root port with enough Full-Speed
  periodic traffic to test it. On Vista and 7 the budgeter is where the
  1.1-hub bugcheck lands (section 6.2).
- **Isochronous scheduling: lost from XP on.** Not among the four when
  this section was first written; measured 2026-09-19 (section 7).

And one cosmetic effect: Device Manager and any tool that asks usbport
report every root-port device as High Speed, whatever it is. The true
speed is visible only in this driver's own trace and counters. Since
2026-09-20 the report has a second place it shows, and a user can open it:
section 5.1.

A stop-time review found that the first draft of the override destroyed the
checkpoint's own evidence: the decoded speed had only ever been visible in
the port-status trace, which now read High Speed for everything. The
replacement, a first-decode trace line and a monotone set of speed classes
seen since the last start, took four rounds to make quiet, and left a rule
about trace budgets that later pages lean on.

The cost on every target is the interrupt interval, and it is irrecoverable
through this usbport. usbport turns a device's `bInterval` into the `Period`
it hands the miniport using the speed it believes: for a High-Speed device
that is `2^(bInterval-1)` microframes capped at 32, for a Full or Low Speed
one the frame count rounded down to a power of two. No raw `bInterval`
reaches the miniport. A Full or Low Speed device on a root port is therefore
bucketed on High-Speed rules, and the driver then floors its interrupt
endpoints at 1 ms because the xHCI specification allows no less at those
speeds (Table 6-12). The result is three bands: `bInterval` 1 to 4 gives
1 ms, 5 gives 2 ms, 6 and above gives 4 ms. A stock mouse at `bInterval` 10
runs at 4 ms; nothing slower is reachable, nothing faster than 1 ms either,
and two values inside one band are indistinguishable. Always the faster
direction: latency only, never a missed poll. An interval override tool that
changes `bInterval` within a band shows no effect for this reason, and one
that crosses a band does. Measured with such a tool, SweetLow's hidusbf and
its Windows 9x lower filter, on a Full-Speed mouse in the Windows 98 SE
virtual machine (Phase 20): `bInterval` 1, 2, 5 and 8 on a root port arrived
as `Period` 1, 2, 16 and 32 and were programmed as Interval 3, 3, 4 and 5
(1, 1, 2 and 4 ms), the first two through the floor; the same mouse behind a
Full-Speed hub arrived at its true speed, with `bInterval` 10, 1 and 4
bucketed in frames as `Period` 8, 1 and 4 and programmed as Interval 6, 3
and 5 (8, 1 and 4 ms), nothing floored. The bucketing rule is usbport's and
common to every build this project targets, so the bands hold on every
target; only Windows 98 has the readings. The prohibition on
"reconstructing" `bInterval` from `Period` is in the invariants: the
information is gone before the miniport sees it.

**Roadmap task 24.2 (2026-09-25) found no narrower remedy for the bands,
and they stay 24.3's.** The owner decided it with no code change and no
boot. On a root port, hidusbf's 1000, 500 and 250 Hz already land exactly:
it treats the device as High Speed and writes `bInterval` 4, 5 and 6, and
24.1 read those rates on all four Vista and 7 guests. What is lost is 125 Hz
and slower, a stock rate slower than 4 ms, and any difference inside one
band. The one mechanism that could recover any of that is the driver's own
snoop of `GET_DESCRIPTOR(Configuration)` (`src/xhci_desc.h`). It was
rejected because it reads the device's `bInterval` from below the filter and
never sees the override. Using it would slow every stock root-port device
on every target to its declared rate, and telling an override from a stock
value would be the forbidden reconstruction. `docs/contributing/runs/run-24.md`,
"24.2", has the reasoning.

### 5.1 The cosmetic effect became a dialog a user can open (2026-09-20)

Until roadmap task 23.1 the report was visible only in the speed Device
Manager prints and in this driver's own trace. It is now visible in a dialog
Windows draws, and tasks 23.1 and 23.2 are what put it there: the
controller's **Advanced** tab, whose figures are computed from the speed
usbport was told (`CalculateTotalBandwidth(ULONG, UCHAR, PUSB_PIPE_INFO)`
takes a speed byte). On Windows 98 SE and Windows ME it sits behind a
**Bandwidth Usage** button and the dialog is `usbui.dll`'s
`USBControllerBandwidthPage`; on the NT targets `usbui.dll` draws the whole
page and the same list is inline on the tab, with no button.

Five legs, five USB 2.0 stacks, four operating systems, all on 2026-09-20,
in QEMU guests - the three 9x legs on the `release` flavour and the two NT
legs on `qemu` - devices added from the monitor onto root ports and installed
by the guest's own wizard (`../contributing/runs/run-23.md`, legs A5, C3, B5
and 23.2's legs W and X):

| bus | 98 SE, NUSB 3.3 | 98 SE, SweetLow | ME, SweetLow | 2000 SP4 | XP SP3 |
|---|---|---|---|---|---|
| no USB device attached | System reserved 10 % | 10 % | 10 % | 10 % | 10 % |
| + a Full-Speed mouse on a root port (`usb-mouse,usb_version=1`, 12 Mb/s) | 11 % | 11 % | 11 % | 11 % | 11 % |
| + a High-Speed mouse on a root port (`usb-mouse,usb_version=2`, 480 Mb/s) | 12 % | 12 % | 12 % | 12 % | 12 % |

**A Full-Speed device and a High-Speed device on a root port cost the same
1 %**, to the digit, on all five. That can only hold if the Full-Speed
device is budgeted as a High-Speed one, which is this page's subject: on a
true Full-Speed bus that mouse's interrupt endpoint is a far larger slice.

**Windows 2000 is what closes the argument.** The three 9x legs ruled out an
NUSB artifact and a Windows 98 shell artifact by reproducing the ladder on
NUSB's Windows 2000-lineage stack, on SweetLow's XP-lineage rebuild and on a
second operating system - but all three are *back-ported* stacks, and a
sceptic could still have hung the miscount on the back-porting. Windows 2000
SP4 and Windows XP run **Microsoft's own native `usbport.sys` and
`usbui.dll`, on the operating systems they shipped with**, and they produce
the identical ladder. There is no back-ported-stack explanation left.

**The driver's own counter agrees, which no 9x leg could show.** The two NT
legs ran the `qemu` flavour, so the same attaches are in the debug console:

```
xhci98: slot: endpoint speed differs from the port's, usbport << 8 | decoded=00000302
xhci98: endpoint speed mismatches=00000001
```

usbport reporting speed 3 where the driver decoded 2 - **exactly one
mismatch**, for the Full-Speed mouse and not the High-Speed one, with the
same encoding on both guests. So the 11 % the page showed and the driver's
own counter are two witnesses to one event, in one run, on one machine. A
`runtime` reading. The 9x legs read the page alone, because they were taken
on the `release` flavour, which has no console.

Read it for what it is, and not for more. **It is a reading of that page's
own arithmetic, not of usbport's periodic budget.** The figure is the report
arriving in the user interface; what the schedule is actually charged is
still unmeasured, and section 9's open item on the accounting stands
unchanged. **And the per-device figures are deltas rather than
attributions**: the dialog carries one row, "System reserved", and only its
percentage moves - it never itemises devices, on any of the five legs.
The contrast case, the same Full-Speed device behind a hub where its true
speed is reported, was taken on no leg: QEMU's only hub is a Full-Speed one,
so putting it on a root port is section 6's topology rather than this one's.

**Where a per-device witness would come from, if one is wanted.** The USB 2.0
Root Hub's **Power** page does itemise. On Windows ME with both mice attached
it listed two rows of `USB Human Interface Device` at 100 mA each and
`6 port(s) available` against QEMU's eight ports (leg B7), and the same page
listed a mouse at 100 mA on Windows 98 SE under NUSB (leg A0b). It is
`usbui.dll`'s `USBHubPowerPage`, registered on 9x by the USB 2.0 stack's own
`USB2.INF` and not by this package, so it needs nothing this project ships
and it was the control both legs used. It reports power rather than
bandwidth - but naming devices is exactly the half the Bandwidth page lacks.

One limit on availability, and it is only a version. The tab is **not** in
`1.1.0.0`; it ships in `1.1.1.0` (`../using/release-notes.md`, "The
controller's Advanced tab"). **The second limit that used to stand here is gone**:
this package registered no controller property page on the NT targets until
roadmap task 23.2 took that half on 2026-09-20, and the page now opens on
every one of them - Windows 2000, Windows XP in both architectures, and
Windows Vista and Windows 7 in both. `PROP-NTHALF`, which refused the value
until those readings existed, was inverted in the same change. Only the two
NT 5.x 32-bit guests were walked up the ladder above; the other five were
read for the tab's presence and its idle figure, which is 10 % on NT 5.x and
20 % on NT 6.x.

## 6. A USB 1.1 hub on a root port: harmless on Windows 98 and 2000, fatal on Vista and 7

The workaround leaves one topology structurally exposed: a hub with no TT
(any USB 1.1 hub, or a 2.0 hub with `bDeviceProtocol = 0`) on a root port,
with a Full or Low Speed device behind it. The hub is a root-port device,
so the driver reports it High Speed; the device behind it is reported at
its true speed by the hub; and usbport now believes it has a Full-Speed
device under a High-Speed hub with no transaction translator. What each
usbport does with that belief differs.

### 6.1 Windows 98 and 2000: inferred fatal, measured harmless (batch 7b-V0)

By the transcribed `GetTt` logic (section 3) the lookup would run one level
down and fault the same way. One document filed this as "inferred, nothing
has run"; the invariants and a memory note said "verified".

Batch 7b-V0 attached exactly that topology on both targets, a QEMU
`usb-hub` with a Full-Speed mouse behind it, and neither faulted. The TT
lookup returned the hub's own address, the documented "lookup succeeded"
reading, so a TT record existed for a hub that physically has none: the
override marks the hub High Speed, and a believed-High-Speed hub gets a TT
initialised for it. Why that holds is not confirmed against the binaries
(design record 02, open question 6). What the topology cost at the time
was that the device behind such a hub never enumerated, because usbport
handed the driver TT fields naming a translator that does not exist, and
the driver's refusal of those transfers was unbounded: a dead Windows 98
guest. Both halves were closed in Phase 7b: the refusal got a bound (task
7b-A.0), and the driver now derives the TT fields from its own topology
graph, which carries each hub's speed as the driver decoded it, so a
Full-Speed hub's children get no TT whatever usbport says (task 7b-A.3).
The disagreement is counted (`TtPairsDisagreed`) rather than resolved.

The same topology passed again on 2026-09-19: the 22.9 matrix's hub rows on
Windows 98 and Windows 2000 (the churn row, a mouse behind the hub, on
Windows 2000; it is excluded on Windows 98 for an unrelated reason), and on
XP x64, whose churn row passed both legs. On 32-bit XP the hub carried a
Full-Speed audio device instead, which enumerated and played (section 7);
a mouse behind a hub on 32-bit XP has not been run.

The lesson kept its own section in the lessons file: a disassembly says
what a function does with an empty list, not whether the list is empty.

### 6.2 Windows Vista and Windows 7: the bugcheck (2026-09-19)

**What happens.** A Full-Speed hub on a root port, and a Full or Low Speed
device with a periodic endpoint behind it - a mouse was enough - stops the
machine with `STOP 0x0000007E` (an access violation) in `USBPORT.SYS` the
moment that device is configured. The hub alone enumerates and works. All
four NT 6.x builds do it; the same steps passed on Windows 2000 and XP x64
in the same run (6.1). Roadmap task 22.9, the post-release run of
`1.1.0.0`, was the first time a hub was put on a Windows Vista or Windows 7
guest at all.

| Target | Stop parameters | Faulting instruction (public PDB) | Read of |
|---|---|---|---|
| Windows 7 SP1 x86, DateStamp `4CE79C15` | `0xC0000005, 0x8EAEED30, 0x8A6D749C, 0x8A6D7080` | `usbport!Allocate_time_for_endpoint+0x15d` | `0x00000A04` |
| Vista SP2 x86, `49E01FCF` | `0xC0000005, 0x8E1A9E9C, 0x881C750C, 0x881C7208` | `Allocate_time_for_endpoint+0x162` | `0x00000A04` |
| Windows 7 SP1 x64, `4CE7A670` | `0xFFFFFFFFC0000005, 0xFFFFF88002D96B4C, ...` | `Allocate_time_for_endpoint+0x19c` | `0x0000000000000C08` |
| Vista SP2 x64, `49E02D1B` | `0xFFFFFFFFC0000005, 0xFFFFFA6002A72398, ...` | `Allocate_time_for_endpoint+0x194` | `0x0000000000000C08` |

The stack walked from the Windows 7 x86 context record is usbport's alone,
from `USBPORT_Dispatch` through `USBPORT_ProcessURB`,
`USBPORT_SelectConfiguration`, `USBPORT_InternalOpenInterface`,
`USBPORT_OpenEndpoint`, `MPx_AllocateBandwidth`,
`USBPORT_AllocateBandwidthUSB20` and `USB2LIB_AllocUsb2BusTime` to the fault;
no frame is in `xhci98.sys`.

**Why (static, confirmed by the runtime values).** Opening the mouse's
interrupt endpoint runs usbport's USB 2.0 bus-time budgeter.
`USBPORT_AllocateBandwidthUSB20` passes it the endpoint's transaction
translator as `[ep+1Ch] ? [[ep+1Ch]+38h] : 0` - it sees the missing TT and
passes NULL on. `USB2LIB_AllocUsb2BusTime` takes the schedule to charge as
`TT + 4` for a Full or Low Speed endpoint (`bus + 414h` for High Speed), and
`Set_endpoint` stores it at `+0Ch` of the budget object; on x64 the fields are
pointer-sized, so it is `TT + 8`. `Allocate_time_for_endpoint` then reads
`[schedule + 0A00h]` (`0C00h` on x64): `0 + 4 + 0xA00 = 0xA04`,
`0 + 8 + 0xC00 = 0xC08`, the addresses in the table. The budget object
itself held the mouse's endpoint - maximum packet 4, period 8. The mouse has
no TT because the driver reports the hub, as a root-port device, as High
Speed; the hub's own descriptor offers no TT; and the mouse's speed comes
from the real hub, Full. So the stack looks for a translator on a hub it
believes is High Speed, finds none, and budgets against address 4.

Windows 98 and 2000 survive the same belief because their usbport
initialises a TT record for the believed-High-Speed hub (6.1) and has no
USB 2.0 bus-time budgeter of this shape; XP x64 survives it for a reason
not read.

**What it predicts, not measured.** Any Full or Low Speed device with an
interrupt or isochronous endpoint behind a USB 1.1 hub on a root port does
the same - a keyboard, a mouse, an audio device, a keyboard with a built-in
hub. A bulk-only device there may not reach the budgeter. A USB 2.0 hub has a
real TT, so devices behind it should budget correctly; QEMU cannot show it,
because its only hub, `usb-hub`, is a Full-Speed hub with no speed option (it
enumerates at 12 Mb/s). All four readings in the table are QEMU guests,
and no USB 1.1 hub has ever been on a root port on real hardware under any
target (none was held, batch 13-E). The topology itself is real - any USB
1.1 hub, or a 2.0 hub declaring no TT, on a root port of a physical machine
- and nothing about the fault depends on the emulator: the stack, the NULL
translator and the budget object are usbport's own.

**Workarounds** (in the release notes): plug Full and Low Speed devices into
a root port directly, where the driver's report covers them, or behind a USB
2.0 hub. **The USB 2.0 hub was measured on real hardware on 2026-09-19**:
32-bit Windows 7 SP1 on the E460, a Low-Speed mouse behind the multi-TT
Terminus `1A40:0201` at a root port, and again with a Full-Speed audio
device playing and a flash drive copying beside it, and the same three
behind the single-TT `1A40:0101` - no bugcheck, everything worked
(`run-22.md` 22.9). The driver's dump shows the mouse addressed Low Speed
behind the hub with its own route, so usbport budgeted it against the
hub's real translator. One machine, two hubs, these devices; Vista and
64-bit Windows 7 not measured on metal.

**How it was found and read.** The harness reported the churn row's guest as
"not executing": its debug console showed four `DriverEntry` lines in one
group, three with no teardown before them - Windows 7 restarting after each
bugcheck - and the harness went on reading the first boot's extension
address. A run through an untracked copy of the runner with
`-action reboot=shutdown` held the guest on the stop screen; the exception
and context records and the stack were read from the halted guest's memory
over QEMU's monitor, and the whole guest memory was saved
(`out\post-release\1.1.0.0-win7-hub-diag\`). The other three builds were
installed on fresh overlays and given only the hub and the mouse
(`out\post-release\1.1.0.0-t229-{vista,vista64,win764}\`). Every
binary-derived fact has its row in `legal-provenance.md` section 4; the run
record is `docs/contributing/runs/run-22.md`, 22.9.

## 7. A Full-Speed audio device on a root port plays nothing from Windows XP on (2026-09-19)

**What happens.** The device installs, is the default playback device, and
Windows shows it playing, but no sound reaches it: no isochronous transfer
reaches the driver at all. Task 22.9 was the first time audio was played,
rather than only bound, on any target after Windows 2000. The rows not
marked metal are QEMU guests, measured with QEMU's `usb-audio` and its
`wav` backend as the oracle, the driver's counters read over the monitor
while the guest played; the rows marked metal are the real-hardware
playback readings this project has, on Windows 98 and on 32-bit Windows 7,
all on the E460 and judged by ear:

| Target | Played with | Isochronous submits | Audio reaching the device |
|---|---|---|---|
| Windows 98 SE (VM) | the startup sound | 1 (10 packets, all answered), then the OS's own `USBAUDIO.VXD` faults | - (batch 9-V's known VM artefact) |
| Windows 2000 SP4 | Sounds and Multimedia | 376 (3,760 packets, all answered) | 659,456 B |
| XP SP3 x86, on a root port | Sound Recorder, 1.93 s of 1.93 s | 0 | 0 B |
| XP SP3 x86, **behind a Full-Speed hub** | Sound Recorder, the same file | **196 (1,960 packets, all answered)** | **344,064 B** |
| XP x64 SP2 | Sounds tab | 0 | 0 B |
| Vista x86 and x64 | Speakers Properties, Test showing Stop | 0 | 0 B |
| Windows 7 x86 and x64 | Speakers Properties, Test showing Stop | 0 | 0 B |
| **Windows 98 SE on the E460 (metal)**, batch 13-E | a physical UAC 1.0 device, at a root port (E6.1) and behind the multi-TT hub (E6.2) | not read | **played clean** (Finding X: the first audio through this driver on real silicon, and the first split-transaction isochronous path in any vehicle) |
| **Windows 98 SE on the E460 (metal)**, `1.1.0.0` `release-x86`, 2026-09-19 | a Sound Blaster Play! 2 (`041E:323D`, Full Speed, UAC 1.0) directly in a root port, NUSB 3.3; the owner's hand-test | not read (release flavour) | **played**, heard by the owner (`run-22.md` 22.9) |
| **Windows 7 SP1 x86 on the E460 (metal)**, `1.1.0.0` `release-x86`, 2026-09-19, **on a root port** | a C-Media `0D8C:0014` (UAC 1.0), Sound Test and Windows Media Player | not read | **nothing heard**; no Device Manager error |
| **Windows 7 SP1 x86 on the E460 (metal)**, the same, **behind a USB 2.0 hub** (`1A40:0201`, then `1A40:0101`) | the same | not read | **played**, heard by the owner |

Nothing is refused on the driver's side: where every counter was dumped
(Windows 7 x86, XP x64, Vista x86) every open usbport asked for was accepted
and no refusal counter moved. On Vista x86 and Windows 7 x64 an endpoint was
opened as playback began and nothing was ever submitted down it.

**Why.** 32-bit XP runs the same binary through the same NT 5.x registration
as Windows 2000, so the difference is the OS's own stack, and the hub row
isolates it: behind a hub the device is reported at its true Full speed and
plays; on a root port it is reported High Speed and does not. XP's
`USBPORT_IsochTransfer` takes a separate branch for a High-Speed endpoint -
`cmp dword ptr [eax+110h],2` - which allows up to 0x400 packets and counts
them as microframes (`packets x period >> 3`), so a Full-Speed stream of one
packet per frame is scheduled as a High-Speed one (static, the public PDB).
How that branch then loses the stream is not read, and neither is why
Windows 2000's usbport plays the same stream on a root port.

**On real hardware.** The one physical NT 6.x reading so far, 32-bit
Windows 7 on the E460 on 2026-09-19, agrees with the virtual machines: a
C-Media `0D8C:0014` directly on a root port bound with no error, looked
as if it was playing, and nothing was heard; behind a USB 2.0 hub the same
device played. The driver's counters were not read for it. XP, XP x64,
Vista and 64-bit Windows 7 have not been measured on metal.

A second device from that session, the Sound Blaster Play! 2, is **not**
this limitation and not evidence for it: on Windows 7 its audio function
reads Code 10 both on a root port and behind the USB 2.0 hub, where it is
reported at its true speed. The driver refused nothing; one class request
stalled and `usbaudio.sys` never opened a stream. The control without
this driver has not been run (`run-22.md` 22.9).

**Workarounds** (in the release notes): put the audio device behind a hub.
On XP that can be a USB 1.1 hub (measured on 32-bit XP only, in QEMU); on
Vista and 7 it must be a USB 2.0 hub, because of section 6.2 - measured on
real 32-bit Windows 7 on 2026-09-19 (above); Vista and 64-bit Windows 7
not measured.

**Consequences for the record.** Every device-matrix audio row judged
arrival, not playback: XP x64's row passed because its audio stack opens an
endpoint on arrival, and Windows 7's read NODRIVER because it does not.
Before 2026-09-19 the NT 5.2 and NT 6.x tiers were accepted on "the audio
device bound"; this is a first measurement, not a regression. Roadmap task
22.12 (d), which wants isochronous counters moving on a QEMU audio row, can
only be read on Windows 2000 as things stand.

Both findings in sections 6.2 and 7 are known limitations of `1.1.0.0` by
the owner's decision of 2026-09-19 (`docs/using/release-notes.md`, "Known
limitations"; the download readme, section 7; `README.md`, "Known
limitations"). Neither is a defect in the workaround on the systems it was
made for: Windows 98 and 2000 behave as before. Both stay known limitations
of `1.1.1.0`, which changes nothing about the speed report (the status
paragraph at the head of this page).

## 8. What would fix it, and why nothing was attempted for `1.1.0.0`

Everything above follows from the one lever section 4 describes, and
changing what the driver reports on a root port is exactly what bugchecks
Windows 98 and 2000 - the primary targets. The options, as they stand:

- **A virtual USB 2.0 hub behind every root port**, the idea in
  [`docs/future-plans/virtual-hub-per-root-port.md`](../future-plans/virtual-hub-per-root-port.md):
  a slower device is then reported at its true speed behind a hub that has a
  transaction translator, which removes the interval bands (section 5), the
  Vista and 7 bugcheck (6.2) and the silent audio (7) together. It is the
  only candidate that addresses all three, and it is an idea only - nothing
  of it is built, it is off by default by its own rules, and nothing above
  has been measured under it. Its free prototype is a real USB 2.0 hub on a
  Vista or 7 machine with a slower mouse and a Full-Speed audio device
  behind it: if that did not fix both, the virtual one would not. **It was
  taken on 32-bit Windows 7 on the E460 on 2026-09-19 and fixed both** - no
  bugcheck, and the audio played (6.2, 7) - so the proposal's premise holds
  on the one machine measured. Discussed 2026-09-19 and not decided; the suggested scope if
  taken is XP and later only, Windows 98 and 2000 byte-for-byte unchanged.
- **A true-speed report as an opt-in.** Under a usbport that guards the
  empty TT list, reporting the real speed would remove the interval bands
  as well, because usbport would then bucket `Period` in frames, which
  `XhciIntervalFromPeriod` already handles. It must never be enabled by
  build detection: under NUSB's and SP4's usbport it bugchecks the machine,
  and under SweetLow's XP-lineage rebuild it does too. His single-TT branch
  at `0x2667A`-`0x26686` returns the same `0xFFFFFFEC` for an empty list
  (static, ABI document section 8), and on 2026-09-25 an uncommitted
  truthful build on Windows 98 SE under his stack took a fatal exception at
  `0028:C002F70E` in `NTKERN` - the address NUSB gave in section 2 - after
  usbhub reset a root port holding a Full-Speed mouse and before usbport
  opened its default pipe (roadmap 24.3, `runs/run-24.md`; VM only). No
  truthful-speed run has been made on any NT 5.1+ usbport; a lineage is
  not a basis for enabling an option, and on Vista and 7 it would meet the
  same budgeter that faults in 6.2.
- **Dropping the USB2 flag** loses High Speed on Windows 98 (section 4).
- **Patching a real 1.1 hub's descriptor to claim a TT** would address the
  Vista and 7 bugcheck only, and the hub would then stall the TT requests
  usbport sends it. Not attempted.

## 9. What is still open

- **Polling rates and true speeds on root ports are roadmap Phase 24's,
  and `1.1.1.0` does not answer them.** The owner split them out of Phase 23
  on 2026-09-22 so that release could ship the property page and the
  moderation value alone, and Phase 24, added on 2026-09-24, carries them in
  the reporter's order, which is also the order of ease: the Low-Speed rates
  behind a hub first (task 24.1: the reporter's Code 10 at 250 Hz and above,
  with the Low-Speed `Period` bound in `XhciIntervalFromPeriod` as the
  candidate - not this section's bands, which are a root-port matter), then
  rates on a root port as a decision (24.2: closed 2026-09-25 as owned by
  true speeds, section 5), then true speeds on root ports as a decision with its reason, for
  which the virtual hub in section 8 is the candidate (24.3). Task 24.4
  reads the reporter's pointer for the USB 1.1 hub this project never held,
  once the part is to hand.
- **The bandwidth accounting on Windows 98 and 2000** (section 5) has no
  measurement either way. The Bandwidth Usage readings of 2026-09-20
  (section 5.1) are not one: they are what `usbui.dll` computes from the
  speed that was reported, on the five legs (the three 9x ones, Windows 2000
  SP4 and Windows XP SP3); on the NT targets the same list is inline on the
  tab rather than behind a button.
- **Metal never ran the truthful build**, so the bugcheck itself is a VM
  observation. Nothing suggests real hardware differs: the fault is in
  usbport's own list handling, not in anything the controller does.
- **After Windows 98, real hardware has one session.** 32-bit Windows 7 on
  the E460 (2026-09-19) measured the audio limitation at a root port and
  both USB 2.0 hub workarounds. Windows ME, 2000, XP, XP x64, Vista and
  64-bit Windows 7 have only ever run in QEMU, and the Vista/7 hub
  bugcheck (6.2) and the Windows 2000 and XP x64 hub passes (6.1) are
  virtual-machine readings on every target. The 1.1-hub topology has never
  been on metal under any target (no USB 1.1 hub held).
- **Why a believed-High-Speed 1.1 hub gets a TT record** on Windows 98 and
  2000 (6.1) is unconfirmed against the binaries. The driver no longer
  depends on the answer, and measures the disagreement instead.
- **Why XP x64 survives the topology that bugchecks Vista and 7** (6.1) is
  not read, and a mouse behind a 1.1 hub on 32-bit XP has not been run.
- **A USB 2.0 hub on Vista or Windows 7** with a slower mouse and a
  Full-Speed audio device behind it, on real hardware: **taken on 32-bit
  Windows 7 on 2026-09-19, and both workarounds held** (6.2, 7). It is also
  the virtual hub's free prototype (section 8), and it came out the way
  that proposal needs. Vista and 64-bit Windows 7 remain unmeasured on
  metal. The release notes, README and the download's `readme.txt` were
  changed the same day to say the workaround was measured on one real
  32-bit Windows 7 machine.
- **Whether a bulk-only device behind a USB 1.1 hub is safe on Vista and 7**
  (6.2's prediction).
- **How XP's High-Speed isochronous branch loses the stream, and why
  Windows 2000's usbport plays it** (section 7). Worth reading only if the
  virtual hub is rejected and a narrower audio fix is wanted.
- **Windows ME** has never had a hub plugged or audio played; **SweetLow's
  stack on Windows 98** has neither reading either (section 1).

## 10. Lessons the record kept

- A healthy trace is not a living guest. Screenshot before calling a VM run
  a pass; the driver can be entirely right and the machine still dead.
- Control for device class as well as the variable under test. Two devices
  that differ in speed usually differ in class too.
- A guard present on one branch is not a guard on the function. Grepping
  `GetTt` for "is there a null check" answers yes, twenty instructions from
  the branch that runs.
- Map an import slot by how the code uses it, not by counting the import
  listing. An off-by-one produced six plausible call sites of the wrong
  function; the fastcall argument shape identified the right one at once.
- ReactOS adds guards the shipping binaries lack. Twice now.
- When a change makes the driver stop telling someone the truth, ask what
  was reading that truth. The override silently deleted the checkpoint's
  evidence and silently broke the interval, and both were found by review
  rather than by a run.
- When one document hedges a claim and another asserts it, the hedge was
  the one thinking. Propagate the hedge, or measure it.
- A fix that holds on the stacks it was made for is a new experiment on
  every stack added after it. This one was made on Windows 98 and 2000 and
  carried to XP, Vista and 7 unchanged; the hub and the audio playback that
  found its cost there were simply never tried on them until 2026-09-19.
- "Bound" is not "working". An audio device that installs, is the default
  and shows as playing can move no data at all; the oracle is the stream
  itself (QEMU's `wav` backend), not the device list.
- A workaround is not a fix, and the record should not call it one. This
  page said "fixed" for six weeks while the lever it pulls was costing
  every newer target something; the status line is the one place a reader
  checks.

## Sources

- [lessons.md](../contributing/lessons.md), "A Full-Speed device on a root
  port bugchecks both targets, and a healthy trace is not a living guest"
  (the matrix, the proven-versus-inferred split, the task 7 closing run) and
  the static-pass entry before it (the four parameters resolved, the refuted
  `MiniPortVersion` hypothesis, the refuted 1.1-hub residual).
- [usbport-miniport-abi.md](../usb-xhci-info/usbport-miniport-abi.md)
  section 8, "The transaction-translator lookup, and why
  `USB_MINIPORT_FLAGS_USB2` must be set": the instruction-level chain with
  addresses in both builds (static).
- [implementation-invariants.md](../contributing/implementation-invariants.md),
  "Root Hub Reporting": what is programmed from the decoded speed, and the
  prohibition on reconstructing `bInterval`.
- `src/xhci_port.c`, `XhciPortShadowReport` (the override and its comment);
  `src/xhci_ctx.c`, `XhciIntervalFromPeriod` and `XhciIntervalForSpeed`
  (the bucketing contract and the floor).
- [roadmap.md](../contributing/roadmap.md), Phase 5 status and task 7; task
  22.9.
- [run-23.md](../contributing/runs/run-23.md), 23.1 legs A, B and C: the
  Bandwidth Usage ladder on three 9x stacks, the root hub's Power page as
  the control, and the `usbui.dll` rename control that established which
  module draws the dialog (section 5.1).
- [run-22.md](../contributing/runs/run-22.md), 22.9: the hub bugcheck on
  all four NT 6.x builds, the played-stream table, the XP behind-a-hub
  reading, the owner's decisions; the reports in
  `run-22-post-release/`.
- [design record 02](../contributing/design/02-hub-topology-route-string.md),
  open question 6 (why a believed-High-Speed 1.1 hub gets a TT).
- [legal-provenance.md](../contributing/legal-provenance.md) section 4: the
  static rows for `USBPORT_GetTt`, `USBPORT_CreateDevice`,
  `USBPORT_RootHubCreateDevice` and the descriptor templates; the Windows 7
  x86 budgeter row and the XP `USBPORT_IsochTransfer` row; the hidusbf rows.
- [virtual-hub-per-root-port.md](../future-plans/virtual-hub-per-root-port.md):
  the candidate fix.
