# Phase 36 Record

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 36". The
roadmap entry carries the goal, the status, the task list and the
checkpoint; this file carries what each task did and what each reading
said. Where the two disagree about a clause, the roadmap wins.

**On `out\...` and `vm\...` paths in this file.** They say where a reading
was kept on the host that took it; they are not files a clone has.

## 98 SE hot-plug hangs

Task 36.44. During 36.17-36.19's legs (`out\p36-i17\legs\report.md`,
"98 SE hangs") Windows 98 SE stopped responding three times, each time
with the mouse pointer still moving. A fourth came in 36.43's legs
(`out\p36-comp\legs\w98`, `hang1.png` to `hang3.png`): the UI froze right
after usbview was closed. This section takes each one back to a
reproduction on the `qemu` flavour, against `2.2.0.0`, and against
Windows 98 SE's own USB stack with `xhci98` not loaded (36.41's method).

**Verdict: none of the four is the bus's.** Hang 1 reproduces exactly on
Windows 98 SE's own UHCI stack. Hang 2 is the audio-load wedge 36.41
measured (same CPU signature as its UHCI control; it does not come
without the audio device). Hang 3 is not a hang: Windows' own Power popup
ignores Esc, and the UI stays live. Hang 4 needs the USB audio device and
usbview together, and comes the same way on `2.2.0.0` and on Windows 98
SE's own UHCI stack. Every one also comes on `2.2.0.0`. No code changed,
so there was no review loop and no build beyond the `qemu` package below.

**The vehicle** (2026-10-10, development host A, QEMU 11.1.0, TCG, one
`pentium3` processor, 256 MB, `qemu-xhci,p2=8,p3=4`, as 36.17's legs).
- **The guest is the one that hung:** a copy of 36.17's 98 SE overlay
  (`vm\t36i17\w98.qcow2`, stock 98 SE with `xhci98` installed, no NUSB
  runtime, the 36.17 device set's devnodes present), sealed read-only as
  `vm\t36h\w98-i17copy.qcow2`. One boot added two lines to `AUTOEXEC.BAT`
  that copy `D:\XHCI98.SYS` and `D:\XHCIUAS.SYS` over the installed files,
  so the transfer drive picks the build (`vm\t36h\w98h-base.qcow2`); one
  more set `XHCISNAP -verbosity 3` (`w98h-base2.qcow2`). Every run is a
  fresh overlay of that.
- **The builds:** `2.3.0.0` at `bfec7b0`, x86 `qemu` flavour, `xhci98.sys`
  272,783 bytes, SHA-256 `6edae084...8b45`, `XHCISNAP` built from the same
  tree; and `2.2.0.0`'s `release-x86` from `releases\2.2.0.0`
  (194,459 bytes, `98f1fb1d...b88a`).
- **The UHCI control:** the same base booted once with `piix3-usb-uhci` and
  no `qemu-xhci`; Windows 98 SE installed its own `uhcd.sys` from the CD
  (`USB.INF`); sealed as `vm\t36h\w98h-uhci.qcow2`.
- **The kit:** `out\p36-w98hang\kit` (`hang1.ps1`, `hang2.ps1`,
  `i17keys.ps1`/`i17rest.ps1`, `power.ps1`, `eip.ps1`, `alive.ps1`); logs,
  QEMU traces (`usb_set_addr`, port attach/detach, `usb_xhci_slot_*`,
  `usb_xhci_port_*`), `XHCISNAP` dumps and register samples in
  `out\p36-w98hang\logs`, screenshots in `shots`.
- **A note on the clock.** Under TCG the 98 SE taskbar clock fell one to
  two minutes behind the host during the legs, and a minute turned late.
  A clock that "stayed" across one reading is therefore no evidence of a
  hang; every verdict below is taken from the Start menu, a click or a
  key acting, or not.

### Hang 1: a stick unplugged while its Add New Hardware wizard is open

**Steps** (`kit\hang1.ps1`, as 36.17's `leg3w98.ps1`): the desktop plus
two minutes; an MS-DOS prompt, `start /m D:\USBIF watch 1800`; a
`usb-storage` with a never-seen serial hot-added at port 7; 60 s (the
wizard "This wizard searches for new drivers for: USB Mass Storage Device"
is up); `device_del`; 40 s; the same serial added again at port 7; 120 s.

**What the bus does** (`h1a`, `h1b`, `2.3.0.0` `qemu`):
- The first plug is enumerated (reset, Enable Slot, Address Device,
  descriptors, PDO). The unplug disables the slot, the PDO moves to the
  gone list, and the root hub invalidates its bus relations
  (`IoInvalidateDeviceRelations`, `hcd_pdo.c`).
- **No `QUERY_DEVICE_RELATIONS` comes.** The debug log shows no PnP IRP at
  all after the unplug while the wizard stays open.
- The replug is seen (`port status change on port=0000000B`, the change
  acknowledged), but the port is not reset. `XHCISNAP` read with the
  wizard open (`logs\h1b-snap-held.txt`): port 11 `PORTSC 00000EE1`, CCS 1,
  PLS 7; the enumeration machine **Gone**, no slot; no device holds a slot
  ("0 device(s)"); counters SlotsEnabled 1, DevicesAddressed 1 - the first
  plug's. QEMU's `info usb`: "Device 0.0, Port 7".
- That is design record 13's rule, not something left undone: a port in
  Gone waits until the PDO it reported has been reported missing to PnP
  (`hcdPortQuiet`, `hcd_enum.c`), and only a relations answer does that.
  Nothing is half-done: there is no Enable Slot or Address Device in
  flight, no slot held, no command outstanding.
- **It completes once Windows asks.** `h1a`: Enter through the wizard
  (its search finds `C:\WINDOWS\INF\USBSTOR.INF` in the guest) sends the gone PDO `FILTER_RESOURCE_REQUIREMENTS`, `START`
  (refused: "START refused, device gone"), one `SELECT_CONFIGURATION`
  (refused at the next tick, DEVICE_GONE) and `STOP`; the wizard then asks
  to restart. Answering **No** brought the root hub's
  `QUERY_DEVICE_RELATIONS` at once: the port left Gone, the replugged stick
  was reset, given slot 1, addressed (DevicesAddressed 2), its PDO created
  and started, the gone PDO removed, and the stick configured. QEMU:
  "Device 0.1, Port 7". The UI stayed live.

**The hang** (`h1b`): 36.17's leg went on to type its next `rc.ps1`
command lines blind into whatever had the focus, which was the wizard
(`kit\i17keys.ps1` and `i17rest.ps1` replay them with the same timing).
They stepped the wizard through to "ready to install `USBSTOR.INF`", the
same `START`/`STOP` on the gone PDO followed, and the leg's later lines
went into the wizard's remaining pages. The end state is 36.17's
screenshot for screenshot: the USBIF window in front, the wizard behind it
on its Finish page, the Start menu not opening, a click on Finish doing
nothing, Ctrl+Alt+Del's Close Program listing USBIF, MS-DOS Prompt,
Explorer, Systray and Winoldap; QEMU idle (1-2 s of CPU per 10 s, EIP
mostly the idle `FF0270ED`); `info usb` "Device 0.0". No PnP IRP reached
the bus after the `STOP`.

**The controls:**
- **Windows 98 SE's own UHCI stack, no `xhci98`** (`u1a`, same steps, port
  2 of `piix3-usb-uhci`): the same wizard; the replug **also** stays
  "Device 0.0, Port 2" while the wizard is open (98 SE's own `usbhub` does
  not address it either); the same keystroke replay ends in **the same
  wedge**: Finish page, Start menu dead, the same Close Program list.
- **`2.2.0.0` `release`** (`r1a`): the same as `2.3.0.0` at every step -
  port 11 Gone with CCS set and no slot in `XHCISNAP`, "Device 0.0", and
  the same wedge after the replay.

**Verdict: not the bus's.** Windows 98 SE's configuration manager does not
re-enumerate a hub while the Add New Hardware wizard holds a devnode that
has gone, on either bus; the wedge follows from driving that wizard (and
its restart prompt) for a device that is no longer there, and it happens
the same way with no `xhci98` loaded. The bus's part - waiting in Gone for
the relations answer, then enumerating the replug when it comes - is the
designed behaviour and recovers as soon as Windows asks.

### Hang 2: eight devices hot-added a minute after the desktop

**Steps** (`kit\hang2.ps1`, 36.17's `w98devs.ps1`): cold boot; the
desktop plus about a minute; an MS-DOS prompt; the 36.17 set added 10 s
apart - SuperSpeed stick (port 1), mouse (2), keyboard (3), full-speed hub
(4) with a mouse behind it, high-speed stick (5), **`usb-audio` (6)**, the
tool stick (8); then `dir C:\ > COM1` and a watch every minute.

**Readings:**

| Run | Build | Set | Result |
|---|---|---|---|
| `h2a` | `2.3.0.0` `qemu` | all eight | screen frozen from the first adds, QEMU at 100 % CPU; the `dir` output reached COM1; **recovered on its own** within 15 minutes |
| `h2b` | `2.3.0.0` `qemu` | all eight | the same; still at 100 % after 28 minutes, then quit |
| `h2c` | `2.3.0.0` `qemu` | **no audio** | live throughout (1-3 s of CPU per 10 s) |
| `h2e` | `2.3.0.0` `qemu` | **no audio** | live |
| `h2f`, `h2g`, `qa1` | `2.3.0.0` `qemu` | **audio only** | `h2g` and `qa1` wedged (100 % CPU, the same EIPs), `h2f` live |
| `qf1`, `qf2` | `2.3.0.0` `qemu` | all eight | live |
| `r2a`, `r2b`, `rf1` | `2.2.0.0` `release` | all eight | live |
| `ra1`, `ra2`, `rb1`-`rb3` | `2.2.0.0` `release` | **audio only** | `ra1` and `rb1` wedged (100 % CPU), three live |

Totals: with the audio device, `2.3.0.0` wedged 4 of 7 and `2.2.0.0` 2 of 8;
without it, 0 of 2. The `qa`, `qf`, `ra`, `rb` and `rf` runs read the
verdict from QEMU's CPU over three minutes (`kit\batch2.ps1`,
`out\p36-w98hang\reps2.txt`). The numbers are small; 36.41's larger
sample on both buses is the rate to quote, and these show only that the
same wedge comes on `2.2.0.0` and does not come without the audio device.
In every wedge QEMU itself answered: the monitor took `info registers`,
screendumps and trace commands throughout, so none of these is the QEMU
process-level stop seen elsewhere in the run (no monitor answer, 0 % CPU).

**During the stall** (`h2b`):
- The debug log was silent from the last device's start onward: the bus
  had nothing outstanding to say.
- 25 `info registers` samples: EIP in the VMM - `C005340E`, `C000A881`,
  `C000A933`, `C0003621`, `C000A7BF`, `C000A6E7` - and twice in V86 code.
  **These are the addresses of 36.41's wedge samples, including its UHCI
  control with no `xhci98`** (`out\p36-audio\logs\uk-2-regs.txt`:
  `C000A881`, `C00036xx`, `C005340E`, `C0051551`). None is in `xhci98`.
- The bus kept serving (`kit\alive.ps1`): in 15 s with the mouse behind the
  hub moved, 20 doorbells and 20 successful 4-byte interrupt transfers on
  its slot, each resubmitted, and the driver's ERDP writes - the reports
  delivered and nobody above consuming them.
- Win32 did not run (`XHCISNAP` typed into the MS-DOS prompt never
  started), while the V86 prompt did (`dir`).

**Verdict: not the bus's; it is 36.41's audio-load wedge.** It needs the
audio device, its CPU signature is the one 36.41's control produced on
Windows 98 SE's own UHCI stack, and the bus serves throughout. The new
facts for 36.41: the wedge spins (100 % CPU rather than idle) and can end
on its own after minutes. 36.17's "the same set 30 s apart, two minutes
later, was fine" is the wedge's own rate, not a cure (36.41 found waiting
120 s does not avoid it).

### Hang 3: closing the root hub's Power properties

**Steps** (`kit\power.ps1`): Device Manager, Universal Serial Bus
controllers, the root hub, Properties, the Power tab, "Power properties",
then Esc. Run on a guest with the device set (`h3a`, no audio) and on the
stalled-then-recovered `h2a` (with audio).

**Readings:**
- The popup opened and listed the devices, as in 36.17 (the bus saw no
  request worth a log line: the list comes from the hub's node IOCTLs).
- **Esc did nothing**, two presses on `h3a` and three on `h2a`. The popup stayed - 36.17's "the
  dialog stayed".
- Alt+C, or a click on Close, closed it at once. The properties sheet,
  Device Manager's tree (arrow keys), Cancel and the Start menu all
  answered afterwards; the guest idled at 1 s of CPU per 10 s.
- **UHCI control** (`u3a`, 98 SE's own stack, "USB Root Hub"): the same
  Power popup ("2 port(s) available") ignores Esc the same way, and Alt+C
  closes it.
- **`2.2.0.0` `release`** (`r2a`, with the audio device): the same - Esc
  ignored, Alt+C closes it, the Start menu opens.

**Verdict: not a hang and not the bus's.** The popup is Windows' own
(`usbui.dll`, which the INF takes from the system) and does not act on
Esc, on any stack.
36.17's reading took the popup staying after Esc and a taskbar clock that
had not turned for a minute (see the note on the clock) for a frozen UI;
the IRQ1 count it saw rising was the keys arriving.

### Hang 4: usbview closed with a USB audio device present

**Steps** (`kit\uv.ps1`): a fresh overlay, the desktop plus 2.5 minutes,
36.17's device set added 20 s apart (mass storage, two mice, keyboard,
hub; with or without the `usb-audio`), 30 s, then up to four cycles of:
`start D:\UV.EXE` (the SweetLow usbview of 36.17), F5, the tree walked node
by node with the arrow keys as 36.43's `uvkeys.ps1` did (each node a
node-connection and descriptor request through the hub door), Alt+F4,
then Ctrl+Esc - live if the Start menu opens. A run whose audio device
wedged the guest on arrival (hang 2) stopped before usbview and is not
counted here.

**Readings** (cycles to the first freeze):

| Set | `2.3.0.0` `qemu` | `2.2.0.0` `release` | UHCI control (no `xhci98`) |
|---|---|---|---|
| no audio, usbview | 9 of 9 cycles live (`u4d`, `u5a`) | 9 of 9 live (`u4e`, `u5b`) | - |
| audio, usbview | 2 of 3 runs froze at cycle 2 (`va2`: "fatal exception 00 ... in VXD USBAUDIO(01) + 00002ED4"; `vq1`: frozen frame); `uq1` live | 2 of 4 runs froze at cycle 2 (`vc1`, `vd1`: frozen frame); `us1`, `vc2` live | **2 of 2 froze at cycle 2** (`uhb1`: "fatal exception 0E at 0028:FF045748"; `uha1`: black screen) |
| audio, no usbview (the control) | 16 of 16 cycles live (`na1`, `na2`, `nb1`, `nb2`) | 16 of 16 live (`nc1`, `nc3`, `nd1`, `nd3`) | - |

- The `2.3.0.0` debug log at `va2`'s fault: the audio function's
  streaming interface selected (alt 1), isochronous URBs published and 30
  packets answered, then the exception. Windows started playing (a system
  sound) and its `USBAUDIO.VXD` faulted at the very offset Phase 9 and
  Phase 10 recorded on QEMU's `usb-audio`, through a UHCI control too
  (lessons.md; batch 13-E found Windows 98 SE plays clean through `xhci98`
  on real silicon, so the fault is the QEMU vehicle's).
- The frozen frames on `2.2.0.0` and `vq1` were idle (12 samples each:
  `FF0270ED`, V86 code and VMM addresses, none in `xhci98`), the screen's top rows torn the way a mode switch to the text
  screen leaves them; the UHCI control showed the fault screens
  themselves.
- 36.43's freeze had the same ingredients: its set carried `usb-audio`
  (port 6 and behind the hub), and it froze right after usbview was closed.
  Its Close Program list (MS-DOS Prompt, Explorer, Systray) is a live
  kernel with a dead UI, as here.
- The hub door's `IRP_MJ_CREATE`, `CLEANUP` and `CLOSE` on `2.3.0.0` at
  `bfec7b0` complete at once with no work (`HcdDoorCreateClose`), and the
  freeze comes the same way with no `xhci98` at all. 36.45's change to that
  path (branch `p36-strdesc`) is not in either build read here.
- QEMU answered its monitor through every freeze (register samples and
  screendumps were read from each), so none is the QEMU process stop seen
  elsewhere in the run.

**Verdict: not the bus's.** It needs the audio device and usbview
together, and it comes on `2.2.0.0` and on Windows 98 SE's own UHCI stack
at the same point; without either ingredient nothing froze. It is Windows
98 SE's `USBAUDIO.VXD` on QEMU's `usb-audio` (the vehicle fault Phase 9
recorded), set off by playback starting during the usbview cycles - most
likely a system sound; what starts it was not traced.

### Owed

Nothing for the bus. For the release notes' known limitations (36.20's
rewording, not this task): on Windows 98 SE, leave the Add New Hardware
wizard of a device before unplugging it - a device unplugged under its
wizard, and anything plugged into that port, waits for the wizard, on
every USB stack.
