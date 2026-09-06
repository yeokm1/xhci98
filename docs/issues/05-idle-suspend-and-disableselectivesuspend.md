# Issue 5 - A device plugged into an idle Windows 98 controller is seen by nothing, and why the package writes `DisableSelectiveSuspend`

Status: fixed by one registry value, no driver code. The Windows 98 install
path has written it since roadmap task 11-V.6; the NT path since release
`1.0.1.0`, when Windows XP showed the same idle. The value is
listed under "Known limitations" in `docs/using/release-notes.md` because
it is machine-wide and outlives an uninstall.

Targets affected: Windows 98 SE under either USB 2.0 stack (NUSB's
`usbport.sys` and SweetLow's XP-lineage rebuild, both measured), and Windows
XP (measured in a virtual machine). Windows 2000 SP4's own stack
was not seen idling this controller in any recorded run, with or without the
value; that is a bounded VM observation, not a "never" (roadmap Phase 20,
F18). Real hardware behaves the same as the VM on Windows 98: the fix went
into the media before the bare-metal batches and the idle hot-plug has not
been reported on metal since.

The short version: usbport idle-suspends a controller whose bus has gone
quiet, about half a second after the last transfer on Windows 98. This
driver's `SuspendController` halts the xHC, as the miniport contract
requires. A halted xHC cannot generate a Port Status Change Event, so a
device plugged in afterwards raises nothing, and Windows learns of it only
when the user presses Refresh in Device Manager, which resumes the
controller. Microsoft's own EHCI miniport survives the same halt because
EHCI has a port-change interrupt enable that works while halted; xHCI has no
such bit. Three correct derivations of "how does a driver wake from this"
each ended in a dead end. The fix came from a different question, "can the
sleep be prevented", and from two strings in usbport's own binary: usbport
reads `DisableSelectiveSuspend` from `Services\USB` and stops idling when it
is 1. This page is the story; the mechanism's evidence is in
[lessons.md](../contributing/lessons.md), "Batch 11-V stage A", and the
shipped reasoning in the comment block above `[Xhci.AddReg.Global]` in
`src/xhci98.inf`. Where this page and those disagree, they win.

---

## 1. The problem

A Windows 98 machine with this driver, nothing on the bus, a minute idle: a
mouse plugged in does nothing. No Add New Hardware wizard, no new device,
no error. Device Manager -> Refresh, and the mouse appears and works. Every
later plug on the same session behaves the same way once the bus has gone
quiet again. A device that was attached at boot keeps working, and while
anything is attached the hub never asks for the idle, a device with no
driver included (measured: a smart-card reader Windows 98 cannot bind, left
alone on the bus with the value deleted, kept the controller running while
the bare-bus control suspended within seconds). That is why a machine with
a USB keyboard, or a laptop with internal USB devices, never shows the
defect and a bare machine always does.

The driver's own counters say what "nothing" means. Across a 40 s window
with the device attached to the idle controller: `HealthPolls` 41 -> 41,
`RhPortStatusQueries` 20 -> 20, `SuspendCount` 1. Not a poll declined, not a
port query answered wrongly: nothing ran at all (runtime, the Windows 98
guest).

## 2. How it was discovered: a differential against Microsoft's EHCI miniport

The question was whether the back-ported `usbport.sys` was at fault (in
which case no miniport could fix it) or whether this miniport did something
the shipping ones do not. The vehicle put NUSB's own `usbehci.sys` beside
this driver under the same `usbport.sys`: QEMU's `usb-ehci` (ICH4,
`8086:24cd`) next to `qemu-xhci`, one usbport, two miniports.

With Device Manager closed and both buses bare, a device attached to the
EHCI raised the wizard; the same device attached to the xHCI raised nothing
for 40 s. usbport was exonerated. Both controllers were halted while idle,
read straight off the monitor with `xp` after about two minutes, not
inferred from elapsed time:

```
xHCI  USBCMD  0x00000000   USBSTS  0x00000001   (HCH = 1)
EHCI  USBCMD  0x00010020   USBSTS  0x00001000   (HCHalted = 1)
      USBINTR 0x00000004   Port Change Detect Enable, and only that
```

The whole difference was one bit in one register. Re-attaching to the EHCI
moved `USBCMD` to `0x00010031` and `USBSTS` to `0x0000c000`: the connect
raised an interrupt on a halted controller, usbehci's ISR claimed it, and
usbport resumed.

## 3. How it was troubleshot: three correct answers to the wrong question

### 3.1 What usbehci does (static)

NUSB's `usbehci.sys`, `SuspendController` at VA `0x13C92`, read with
`link -dump -disasm`, nothing executed: it saves the list registers, clears
`USBCMD` bit 0 (the halt), stalls 125 us, acknowledges `USBSTS`, sets
`USBINTR = 0`, waits for `HCHalted`, and then, last thing before returning,
sets `USBINTR |= 4`. It masks everything and re-arms Port Change Detect
across the halt. The live registers and the listing agree bit for bit.

### 3.2 Why xHCI has no equivalent (specification)

The obvious port of that trick, "stop masking in `XhciSuspendController`",
does not exist on xHCI, and the specification says so in a note under a
figure rather than in a register table. Under Figure 4-34 (p.294): the xHC
"may not be capable of generating Port Status Change Events, i.e. if
HCHalted (HCH) = '1'", and "if the HCHalted (HCH) = '0' and the Event Ring
is not full, the xHC shall generate Port Status Change Events". An xHCI
interrupt exists only as an Event TRB reaching an interrupter; `USBSTS.PCD`
is a status bit a poller reads, not an EHCI-style enable (p.364: EINT and
PCD "do not generate an interrupt"). The two architectures differ in kind
here. The same page explains why Refresh works and nothing is lost meanwhile:
a port with CCS and CSC set before the xHC runs generates its Port Status
Change Event when HCHalted goes to 0 (p.295).

### 3.3 The two other candidates, closed by measurement

A timer-driven poll has no clock. A `CheckCallbacks` counter placed above
every gate in the health poll read equal to `HealthPolls` and both frozen
across 90 s of suspension: usbport does not call the miniport at all while
it holds the controller suspended, and `UsbPortRequestAsyncCallback` runs
on the same usbport timer (measured for `CheckController`, inferred for the
async callback).

PME# cannot be reached in the vehicle. With `pci_cfg_read`/`pci_cfg_write`
traced through a boot, `qemu-xhci`'s capability chain is MSI-X and nothing
else: no PCI Power Management capability, no `PMCSR.PME_En`. The trace also
showed that Windows 98's idle "suspend" of this controller is a software
halt with the device left in D0; nothing writes a power register at suspend
time. That is why a Refresh recovers cleanly.

Each derivation was correct. Each answered "how does a driver wake a
sleeping controller". None was a fix.

## 4. How it was solved: prevent the sleep

The owner asked a different question: can the sleep be prevented? The
answer was in the same usbport binary the rest of the investigation had
been reading. `USBPORT.SYS` carries two live registry reads (static, NUSB's
build, VAs `0x11C10` and `0x11DBE`):

- `HcDisableSelectiveSuspend`, per controller, from the driver's own key;
- `DisableSelectiveSuspend`, global, through
  `RtlQueryRegistryValues(RelativeTo = Services, L"usb", ...)`, in a table
  beside `UsbBIOSx` and `DisableCcDetect`.

Setting the per-controller value alone changed nothing, which would have
ended the idea if the two had been treated as one lever. The global read
sets a second flag the per-controller one never touches, so it was a
different experiment, and it worked. Measured on the 2a guest, one boot
each (runtime):

| | `SuspendController` | `USBCMD` | hot-plug while idle |
|---|---|---|---|
| neither value | 1, within seconds | `0x00000000` (halted) | invisible until Refresh |
| `HcDisableSelectiveSuspend = 1` | 1, within seconds | `0x00000000` | (not retested) |
| plus `Services\USB\DisableSelectiveSuspend = 1` | 0 | `0x00000005` (R/S, INTE) | enumerates on its own |

With the value set: `SlotsEnabled` 1, `DevicesAddressed` 1, the wizard
raised with no Refresh, `CheckCallbacks` climbing instead of frozen. One
`AddReg` line, `[Xhci.AddReg.Global]` in `src/xhci98.inf`, delivered from
four routes (the device install and the right-click Install on each target)
because a Windows 98 update over an existing install can bugcheck before
its registry phase and the value must still arrive.

The same reading was repeated under SweetLow's XP-lineage rebuild of
usbport (value present: no suspend in four idle minutes, a
hot-plugged keyboard addressed at once; value deleted: suspend shortly after
start, the keyboard never seen), and with the value present but set to 0
under NUSB's build, three boots: 0 behaves exactly like absent
(`SuspendController` seconds after start, `USBCMD` `0x00000000` with HCH
set, a keyboard plugged two minutes later still at address 0 after forty
seconds, Refresh bringing `ResumeController` and the enumeration). The
value has to be 1. Both readings are transcribed in `build-and-test.md`.

## 5. Why the value is the fix rather than a workaround, and what it costs

The reason also says what any future wake path would have to overcome.
`SuspendController` must halt: the save/restore path the miniport contract
needs (`src/xhci_init.c`, the quiesce and the restore) rests on it, and a
halted xHC cannot report a port change. Nothing the miniport can leave
armed changes that. So the fix is to stop usbport asking for the idle, and
usbport itself provides the switch.

Three consequences, all in the release notes:

1. It is global, under `Services\USB`, so it changes behaviour for every
   controller usbport drives on the machine, Microsoft's EHCI included. On
   an xHCI-only machine, where this driver is the whole USB stack, that is
   the intent.
2. The controller never idles, which costs a little power. That is the same
   trade Microsoft's own `HcDisableSelectiveSuspend` exists to let an
   administrator make.
3. It outlives the devnode. An uninstall does not remove it, because the
   package cannot know whether something else wanted it. Delete it by hand,
   or set it to 0, to get the idle back.

Until `1.0.1.0` the NT install path omitted the value on the assumption
that Windows 2000's native usbport never idles this controller. That was
never measured (roadmap Phase 20, F18). What was measured is that Windows
XP's usbport idles it about thirty seconds after a start with
nothing attached, with the same invisible hot-plug, so the NT path writes
the value too; on Windows 2000 it is the same machine-wide value with the
same three consequences. The per-controller alternative was considered for
the NT path and not taken: under NUSB's build it alone still idled the
controller, and one mechanism on both paths is one thing to check.

## 6. What is still open

- Reproducing the defect, or checking the value, needs an observed
  suspend: an empty root hub at boot, a few seconds of idle, then the plug,
  with the halted state read before the plug. Any device attached at boot
  keeps the hub awake, driver or not, so a laptop with internal USB devices
  cannot show it, and a 0 written by another tool reads as "no effect". A
  reading taken without those conditions says nothing either way.
- A non-halting idle (leave R/S set with the rings quiet so a Port Status
  Change Event could wake usbport) is neither forbidden by the specification
  nor by the miniport ABI as documented, and is unverified. It is a roadmap
  candidate, not a promise.
- PME# on real hardware (`USB_MINIPORT_FLAGS_WAKE_SUPPORT`) was never
  evaluated; the VM has no PCI Power Management capability to arm.

## 7. Lessons the record kept

- "How do I recover from state X" and "can I avoid state X" are different
  questions, and the first crowds out the second. Three candidates were
  derived, measured and closed before the second question was asked.
- A differential is finished at the register that differs, not at "theirs
  works, mine doesn't". Here it was one bit, visible from the monitor.
- Do not port a mechanism across controller architectures by shape. "Leave
  the port-change interrupt enabled across the halt" is a complete sentence
  on EHCI and a category error on xHCI.
- "It woke" is inferred until the halted state is read immediately before
  the stimulus. Elapsed idle is not a state reading.
- Two registry names with similar meanings are two levers until measured
  otherwise. Treating them as one would have ended the investigation one
  experiment early.
- A value has to be checked for its content, not its presence: present and
  0 is absent.

## Sources

- [lessons.md](../contributing/lessons.md), "Batch 11-V stage A: the Win98
  idle hot-plug defect is FIXED by one registry value" (the differential,
  the usbehci listing, the specification pages, the two closed candidates,
  the usbport strings and the measured table) and its postscript.
- [build-and-test.md](../contributing/build-and-test.md): the idle-suspend
  paragraph (Windows 2000 SP4's bounded observation, the value = 0 reading)
  and the SweetLow stack section (the reading under the XP-lineage
  rebuild).
- `src/xhci98.inf`, the comment block above `[Xhci.AddReg.Global]` (the
  four delivery routes and the three consequences).
- [release-notes.md](../using/release-notes.md), "Known limitations", the
  `DisableSelectiveSuspend` entry.
- [roadmap.md](../contributing/roadmap.md): task 11-V.6, task 19.2 (the NT
  path), Phase 20 finding F18.
- [legal-provenance.md](../contributing/legal-provenance.md) section 4: the
  static rows for `usbehci.sys` `SuspendController` and the usbport registry
  reads.
- xHCI 1.2c: Figure 4-34 and its note (p.294), section 4.19.3 (p.295),
  `USBSTS` (p.364).
