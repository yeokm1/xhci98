# Issue 5 - A device plugged into an idle Windows 98 controller is seen by nothing, and how the package stops the idle

Status: fixed, and the fix has moved once. From roadmap task 11-V.6 to
release `1.0.2.0` it was one registry value and no driver code: the
machine-wide `Services\USB\DisableSelectiveSuspend`, on the Windows 98 path
from 11-V.6 and on the NT path from `1.0.1.0`, when Windows XP showed the
same idle. Since `1.1.0.0` it is one bit in the driver instead -
`USB_MINIPORT_FLAGS_DISABLE_SS` (0x20) in the `MiniPortFlags` the miniport
registers with - and **the INFs write no idle-suspend value at all**. Section
5.4 is the reasoning and the readings; section 4 is how the original value
was found, which is still how the defect is understood.

The two mechanisms reach the same state inside usbport, so nothing about the
defect or its cure changed - only what carries it, and what else it touches.
Machines upgraded from `1.0.0.0` to `1.0.2.0` keep the old value, because it
sits outside the devnode and no `DelReg` removes it; `docs/using/release-notes.md`
says so and says how to delete it by hand.

Targets affected: Windows 98 SE under either USB 2.0 stack (NUSB's
`usbport.sys` and SweetLow's XP-lineage rebuild, both measured), Windows
XP (measured in a virtual machine), and 32-bit Windows 7 (measured in a
virtual machine, 2026-09-16, section 5.3). 32-bit Windows Vista was not seen
idling this controller without the fix in the same runs, although its
stack reads the value; the x64 editions of both were not measured without
it. Windows 2000 SP4's own stack
was not seen idling this controller in any recorded run, with or without the
value; that is a bounded VM observation, not a "never" (roadmap Phase 20,
F18), and usbport's own start routine explains it statically: on Windows 2000
it defaults the global value to 1 unless `Services\usb` holds an explicit 0. Real hardware behaves the same as the VM on Windows 98: the fix went
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
is 1. What ships now says the same thing to the same code by a different
route, the miniport flag of section 5.4. This page is the story; the
mechanism's evidence is in [lessons.md](../contributing/lessons.md), "Batch
11-V stage A", and the shipped reasoning in the comment block above
`XHCI_MINIPORT_FLAGS` in `src/xhci_dispatch.c` and under "Idle suspend" in
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

At the time, setting the per-controller value alone appeared to change
nothing, and the global read was taken to set a second flag the
per-controller one never touches, so the global value was tried as a
different experiment, and it worked. Measured on the 2a guest, one boot
each (runtime):

| | `SuspendController` | `USBCMD` | hot-plug while idle |
|---|---|---|---|
| neither value | 1, within seconds | `0x00000000` (halted) | invisible until Refresh |
| `HcDisableSelectiveSuspend = 1` | 1, within seconds (one boot; not reproduced) | `0x00000000` | (not retested) |
| plus `Services\USB\DisableSelectiveSuspend = 1` | 0 | `0x00000005` (R/S, INTE) | enumerates on its own |

**The middle row is wrong.** Re-taken on 2026-09-16 to check a suggested
`HKR,,HcDisableSelectiveSuspend,0x00010001,1` in `[Xhci.AddReg]`: with only the per-controller value set (by hand, in the controller's software
key `Services\Class\USB\<NNNN>`) and the global value deleted, the controller
did not idle in two boots under NUSB's usbport and two under SweetLow's
(150 to 300 s each, `USBCMD` `0x00000005`, a hot-plugged keyboard addressed
at once, no re-idle after unplugging it), while a control boot with neither
value on the same disk suspended within seconds and missed the keyboard until
Refresh (runtime). The binaries say the same (static, all three 32-bit
builds): usbport sets one flag, "selective suspend allowed", only when both
values are absent or 0, and the root hub's idle request is refused whenever
that flag is clear, so either value alone stops the idle. The second flag the
global value sets gates only whether the hub may switch selective suspend back
on at run time. Why the one boot of 2026-08-13 suspended is not established;
its evidence no longer exists. [lessons.md](../contributing/lessons.md) has
the correction with addresses.

With the value set: `SlotsEnabled` 1, `DevicesAddressed` 1, the wizard
raised with no Refresh, `CheckCallbacks` climbing instead of frozen. That
shipped as one `AddReg` line, `[Xhci.AddReg.Global]` in `src/xhci98.inf`,
delivered from four routes (the device install and the right-click Install
on each target) because a Windows 98 update over an existing install can
bugcheck before its registry phase and the value must still arrive. It is
what releases `1.0.0.0` to `1.0.2.0` carry; **section 5.4 is what replaced
it in `1.1.0.0` and why**. Everything above this line is unaffected by that
change - it is how the defect was found and what it is.

The same reading was repeated under SweetLow's XP-lineage rebuild of
usbport (value present: no suspend in four idle minutes, a
hot-plugged keyboard addressed at once; value deleted: suspend shortly after
start, the keyboard never seen), and with the value present but set to 0
under NUSB's build, three boots: 0 behaves exactly like absent
(`SuspendController` seconds after start, `USBCMD` `0x00000000` with HCH
set, a keyboard plugged two minutes later still at address 0 after forty
seconds, Refresh bringing `ResumeController` and the enumeration). The
value has to be 1. Both readings are transcribed in `build-and-test.md`.

## 5. Why prevention is the fix rather than a workaround, and what carries it

### 5.1 Why prevention

The reason also says what any future wake path would have to overcome.
`SuspendController` must halt: the save/restore path the miniport contract
needs (`src/xhci_init.c`, the quiesce and the restore) rests on it, and a
halted xHC cannot report a port change. Nothing the miniport can leave
armed changes that. So the fix is to stop usbport asking for the idle, and
usbport provides more than one switch for that. Which switch is section 5.4;
the cost is the same whichever it is:

**The controller never idles, which costs a little power.** That is the same
trade Microsoft's own `HcDisableSelectiveSuspend` exists to let an
administrator make. And the user cannot make it the other way while this
package is installed - usbport refuses the root hub's "Allow the computer to
turn off this device" while selective suspend is disabled by either
mechanism, so the flag takes away no knob the value left standing.

### 5.2 What `1.0.0.0` to `1.0.2.0` shipped, and the two costs that moved it

The machine-wide value carried two further consequences, both stated in the
release notes of those versions:

1. It is global, under `Services\USB`, so it changes behaviour for every
   controller usbport drives on the machine, Microsoft's EHCI included. On
   an xHCI-only machine, where this driver is the whole USB stack, that is
   the intent - but the package cannot know that a given machine is one.
2. It outlives the devnode. An uninstall does not remove it, because the
   package cannot know whether something else wanted it. Delete it by hand,
   or set it to 0, to get the idle back.

Those two are what the owner asked to be replaced on 2026-09-16, and section
5.4 is the replacement. They still describe any machine upgraded from one of
those releases: the value is still there and this package does not delete it.

### 5.3 The NT 6.x readings of 2026-09-16

Until `1.0.1.0` the NT install path omitted the value on the assumption
that Windows 2000's native usbport never idles this controller. That was
never measured (roadmap Phase 20, F18). What was measured is that Windows
XP's usbport idles it about thirty seconds after a start with
nothing attached, with the same invisible hot-plug, so the NT path wrote
the value too; on Windows 2000 it was the same machine-wide value with the
same consequences. The per-controller alternative was considered for
the NT path and not taken, on the strength of the 2026-08-13 boot in which it
alone still idled the controller under NUSB's build, and because one
mechanism on both paths is one thing to check. That reason fell on
2026-09-16 (section 4).

The NT 6.x install path, `[Xhci.Dev6.*]`, wrote the value as well, and on
2026-09-16 the question was asked of it directly on the two 32-bit guests,
Vista SP2 and Windows 7 SP1 (runtime, `qemu` build, the committed
`src/xhci98.inf`, the owner at the console). Each was read the way section
6 says a reading has to be: nothing attached at boot, the controller's
callbacks on the debug console, `USBCMD`/`USBSTS` read off the QEMU monitor,
then a hot-plugged mouse. With the value present, neither guest suspended
the controller at all, from an install start or from a boot.

- **Windows 7 needs it.** With the value deleted and the guest restarted,
  `SuspendController` arrived 9 s after `StartController` and nothing
  resumed it: `USBCMD` `0x00000000`, `USBSTS` `0x00000001`. A mouse plugged
  in at the desktop sat at address 0 for 60 s, and Device Manager's Scan for
  hardware changes brought `ResumeController` and the enumeration. After it
  was unplugged the controller was suspended again about 30 s later. That
  is the Windows 98 defect exactly, on Windows XP's timing.
- **Vista did not idle the controller without it, and does read it.** Two
  runs, the second on a fresh install: with the value deleted, each boot
  showed a `SuspendController` 3 to 4 s after `StartController` and a
  `ResumeController` 2 to 3 s later, which a boot with the value present
  did not show; then no suspend in five idle minutes at the desktop
  (`USBCMD` `0x00000005`), a mouse addressed at once, and no suspend in the
  three minutes after it was unplugged. That is a bounded reading, five
  minutes, not a "never".

Those two readings are why the NT 6.x path needed a mechanism at all, and
they are what the replacement had to keep: Windows 7 is the hardest case on
any target, at 9 seconds.

### 5.4 What `1.1.0.0` ships: `USB_MINIPORT_FLAGS_DISABLE_SS`

The owner asked on 2026-09-16 for the machine-wide value to be replaced, for
5.2's two costs, and for the replacement to be confirmed against each OS's
own `usbport.sys`. Three candidates were read.

**The per-controller registry values.** `HcDisableSelectiveSuspend` on 9x and
NT 5.x, `HcDisableAllSelectiveSuspend` on Windows 7 - which has no
`HcDisableSelectiveSuspend` string at all - both read from the software key
(`IoOpenDeviceRegistryKey` type 2), which is where a plain `HKR` under an
install section writes. Statically they work on all nine builds: the start
routine sets its "selective suspend allowed" flag only when the
per-controller and the global values are both absent or 0, and the root hub's
idle IOCTL is refused whenever that flag is clear. On Windows 98 they were
measured working, twice under NUSB's usbport and twice under SweetLow's
(section 4).

They were refused on a runtime reading taken on a Vista x86 guest on
2026-09-17. usbport's own `USBPORTBUSIF_ControllerSelectiveSuspend` writes
`HcDisableSelectiveSuspend = !Enable` back to that key, and where the 9x, XP
and 2000 hubs call it only from the root hub's "Allow the computer to turn
off this device" checkbox - an explicit user action - Vista's `usbhub` also
registers `UsbhPowerCallback` for `GUID_USB_SETTING_SELECTIVE_SUSPEND`
(`48e6b7a6-50f5-4782-a5d4-53bb8f07e226`) at hub start, so the power plan
reaches it with nobody touching anything. Measured: with the global value
deleted and `HcDisableSelectiveSuspend = 1` in the controller's class key,
the value survived a restart and the boot suspend/resume did not appear; then
`powercfg -setacvalueindex <Balanced> ... 48e6b7a6-... 1` and `-setactive`
rewrote it to **0** and `SuspendController` arrived at once, `USBCMD`
`0x00000000` / `USBSTS` `0x00000001`. The Balanced plan ships that setting as
Disabled on AC and **Enabled on battery**, so a laptop on `[Xhci.Dev6.*]`
would lose the fix the moment it was unplugged. Vista and Windows 7 also
share that one install section and do not share the value's name, so the
registry route needed two spellings on one path.

**The miniport flag, `USB_MINIPORT_FLAGS_DISABLE_SS` (0x20).** Taken. It is
declared in the `USBPORT_REGISTRATION_PACKET` at registration, so it names
this controller and nothing else, writes no registry key, and leaves nothing
behind when the device goes.

What it does was read on all nine builds rather than assumed, by sweeping
every read of `MiniPortFlags` in each `.text` (37 to 65 per build, traced
from usbport's copy of the packet) and then sweeping in reverse for every
test of bit 5. **Bit 0x20 is tested exactly once per build, in the start
routine, after the registry reads, and does nothing but force the
selective-suspend-disabled state**: on NUSB (`0x109BC`), SweetLow (`0x11687`),
SP4 (`0x10A2C`), XP x86 (`0x11862`) and XP x64 (`0x129A5`) it clears the FDO's
"SS allowed" flag `0x800` and sets `0x08000000`; on Vista x86 (`0x2AB35`), x64
(`0x14C7A`), Win7 x86 (`0x25308`) and x64 (`0x1427A`) it sets SS state 4.
Every reader treats that identically to the state the machine-wide value
produces (`0x08000000`, or state 3), and nothing tests the bit for S3/S4,
D-state choice, wake, root-hub power or `EnIdleEndpointSupport`. So the flag
reaches the same place the shipped value reached, by a route no OS path can
undo: `ControllerSelectiveSuspend` refuses while `0x08000000` is set and
refuses unless the NT 6.x state is 1 or 2, and the flag's state is 4.

The flag costs the same power as the value and, like it, offers no user
switch (5.1). It is a one-bit change in `XHCI_MINIPORT_FLAGS`
(`src/xhci_dispatch.c`, `0x95` to `0xB5`) with a `C_ASSERT` holding it and a
host-test row pinning the word; both INFs lost `[Xhci.AddReg.Global]` and
every `AddReg=` reference to it, and the INF gate's `SUSP-*` rules were
inverted to refuse either registry spelling anywhere in either file.

**The global value, kept.** Not seriously - it is what was being replaced -
but it is worth recording that it was never found wrong, only too wide. Every
reading in sections 4 and 5.3 stands.

One loose end was chased down on 2026-09-17 and is closed. On Vista and
Windows 7 the bus-interface routine `USBPORTBUSIF_UsbdQueryControllerType`
hands the raw `MiniPortFlags` word out to whoever holds the USBDI interface -
`mov ecx,[fdo+310h]` / `mov ecx,[ecx+20h]` / store to the caller's first
output parameter (Vista x86 `0x26955`, and `interface+0x20` is confirmed as
`MiniPortFlags` by the identical addressing in `USBPORT_OpenEndpoint`'s
`0x800` test at `0x1FB05`). That is the one place above usbport where
something could tell the flag's state 4 apart from the machine-wide value's
state 3, since inside usbport every reader treats them alike.

**Nothing calls it.** The routine is stored at bus-interface offset `0x2C` on
x86 (Vista `0x2279F`... `0x227DE`, Windows 7 `0x21492`) and `0x58` on amd64
(Vista x64 `0x212F7`, Windows 7 x64 `0x1D20D`). A byte sweep for every
indirect call through that slot across all four NT 6.x `usbhub.sys` images
finds **zero**, while the neighbouring slots used as controls - `GetUSBDIVersion`
and `QueryBusTime` - find 4 and 5 on each x86 build and 3 and 4 on each x64
one, which is what says the sweep works. The handful of `mov reg,[reg+2Ch]`
loads in the x86 hubs were read and are unrelated: a stack parameter in
`UsbhException`, a field-pair copy in `UsbhGetHubDeviceInformation`, one
mid-instruction byte coincidence in `UsbhDisableTimerObject`, and Windows 7's
WPP trace cleanup.

The bounded form of the claim: the hub driver is the USBDI interface's
consumer on these systems and it never asks. A third-party driver on a
particular machine could ask, and no reading here can enumerate what is not
on the machine.

### 5.5 The runtime readings of 2026-09-17

Section 5.4's case is static: nine disassemblies and a bit swept for every
reader. On 2026-09-17 the flag was put in front of the operating systems
themselves, one clean install at a time. Eight of the ten targets have been
read; two have not (below), so this section is a record in progress and no
roadmap box is ticked on it.

**What a leg is.** Per target, one clean disk copy, two throw-away overlays
over it, and the `qemu` build on both. The *test* leg installs the `1.1.0.0`
package - the miniport flag, no registry value written anywhere - and the
*control* leg installs the previous build, whose INF writes the machine-wide
value, and then deletes that value. The control is the point: it says the
operating system under test would have idled the controller, so that the test
leg's quiet five minutes mean something. Each leg was read the way section 6
requires - nothing attached at boot, an empty root hub, the controller's
callbacks on the debug console, `USBCMD`/`USBSTS` read from the QEMU monitor
at `BAR0+0x40`, then a keyboard hot-plugged onto the quiet bus and unplugged
again. `USBCMD 0x00000005` / `USBSTS 0x00000000` is running; `0x00000000` /
`0x00000001` is halted. `info usb` reports `Device 0.1` for an addressed
device and `Device 0.0` for one the stack never saw.

| target | test leg: the flag, no value | control leg: previous build, value deleted |
|---|---|---|
| Windows 98 SE + NUSB 3.3 | running 5 min, no `SuspendController`; keyboard addressed within 5 s; no re-idle 90 s after the unplug | suspends right after start; keyboard unseen for 40 s |
| Windows 2000 SP4 | running 5 min; addressed within 8 s; no re-idle 102 s | suspends shortly after start; keyboard unseen for 40 s |
| Windows XP SP3 x86 | no `Services\USB` key at all; running 5 min; addressed within 5 s; no re-idle 96 s | halts within about 45 s of launch; keyboard unseen at +5, +20 and +40 s |
| Windows XP x64 SP2 | no such key; running 5 min; addressed within 6 s; no re-idle 96 s | suspends right after start, no resume; keyboard unseen at +5, +20 and +40 s |
| Windows Vista SP2 x86 | no boot-time suspend/resume pair; running 5 min; addressed; **survives the Balanced plan flip** (below) | boot-time pair; **halts 4 s after the same flip**; keyboard unseen for 40 s |
| Windows Vista SP2 x64 | `Services\usb` holds only `FastS4_OverrideBiosS4`; running 5 min; addressed within 6 s; no re-idle 98 s; **survives the Balanced plan flip for 3 min 55 s** and addresses a second keyboard within 6 s; no re-idle 102 s | boot-time pair; runs 5 min and addresses a keyboard before the flip; **halts within 5 s of the same flip**; keyboard unseen at +5, +20 and +40 s |
| Windows 7 SP1 x86 | no `Services\USB` key at all; running 5 min, well past the 9 s mark; addressed within 10 s; no re-idle 101 s | halts between 10 and 20 s after start; keyboard unseen for 40 s |
| Windows 7 SP1 x64 | no such key; running 5 min; addressed within 6 s; no re-idle 97 s | suspends before the desktop was reported, no resume; keyboard unseen at +5, +20 and +40 s |

Four of those readings are worth singling out.

- **Windows 7 x86 is the hardest case and it passes.** 5.3 measured the
  value-less build halting 9 s after `StartController`; the control leg
  reproduced it at 10 to 20 s, and the flag build ran five minutes and saw
  the hot-plug at once.
- **Vista has no idle to prevent unless it is provoked**, so the discriminator
  there is 5.4's own stimulus: `powercfg -setacvalueindex <Balanced> ...
  48e6b7a6-... 1` followed by `-setactive`, the flip that rewrote
  `HcDisableSelectiveSuspend` to 0 and killed the per-controller route. Under
  the flag the controller was still running 3 min 35 s after the flip and
  addressed a keyboard hot-plugged afterwards; the value-less control halted
  4 s after the same `setactive` and never saw its keyboard. Vista x64
  repeated it: 3 min 55 s running under the flag, halted within 5 s on the
  control. That is the clearest evidence that the flag is not reachable from
  the power plan.
- **Windows 2000 needed an explicit `DisableSelectiveSuspend = 0` on both
  legs** or the two could not differ, for the reason section 6 gives: its
  usbport defaults the absent value to 1. With that 0 in place the control
  idled - the first Windows 2000 idle observed in this project - and the flag
  build did not. That it is also the explanation of Phase 20's F18 remains the
  static reading's claim; these boots did not test it.
- **Windows XP x64, Windows 7 x64 and Vista x64 had never been read without
  the value at all** (section 6's fourth bullet). They have now, on the amd64
  build, and all three controls idle (Vista x64's on the plan flip). One binary difference separates each pair of legs, and
  one INF difference: `[Xhci.AddReg.Global]`, referenced from both install
  routes in the previous INF and present only as a comment in the shipped one.

Two cautions for anyone repeating this. **A clean shutdown logs
`SuspendController` and then `StopController`** - that pair is the shutdown
path and is not an idle; on XP and XP x64, where the controller had already
idled, the shutdown appended only the `StopController`, usbport not suspending
an already-suspended controller twice. And on four controls a single
`ResumeController` landed after the last hot-plug reading and before the
shutdown, with no device addressed in between; whether the unplug or the
power-down drew it was not separated on the first three, and nothing in these
verdicts rests on it. On Vista x64 the unplug did not draw it within 42 s (the
controller was still halted then), so there it came with or after the
power-down.

**Windows Vista x64, the test leg (2026-09-17 evening).** The amd64 build of
`457da8c` (`xhci98.sys` 277,504 bytes, SHA-256 `43C91FE5...BE78`), on a
clean copy of the guest's clean-install snapshot, under F8 -> Disable Driver
Signature Enforcement at both boots (`DriverEntry` in each log). After the
install, `Services\usb` existed holding only `(Default)` and
`FastS4_OverrideBiosS4` = 1, as on Vista x86. On the observation boot
(`StartController` at log line 22, BAR0 `0xfebf0000`) `USBCMD`/`USBSTS` read
`0x5`/`0x0` at twelve reads from 19:53:38 to 19:58:54; a keyboard
hot-plugged at 19:59:05 was `Device 0.1` within 6 s (`devices
addressed=00000001`, line 873), and seven reads over the 98 s after its
unplug stayed `0x5`/`0x0`. Then the flip: Balanced
(`381b4222-f694-41f0-9685-ff5bb260df2e`, the same GUID as x86) read AC 0 /
DC 1 for USB selective suspend, `-setacvalueindex ... 1` and `-setactive`
(Enter at 20:02:21) made it AC 1 / DC 1, and twenty-two reads from 6 s to
3 min 55 s after the `setactive` stayed `0x5`/`0x0`. A second keyboard
hot-plugged at 20:06:05 was addressed within 6 s (`devices
addressed=00000002`, line 1437), and seven reads over the 102 s after its
unplug stayed `0x5`/`0x0`. The log's only `SuspendController` (line 1606)
sits directly before `StopController` (line 1611): the power-down, not an
idle.

**Windows Vista x64, the control leg (2026-09-17 evening).** The previous
amd64 build (`19fc4c4`, SHA-256 `E2634A89...D11A`), on its own overlay over
the same clean copy, F8 at both boots (`DriverEntry` shows its 18:26:46 build
stamp, not the test leg's 18:15:39). After the install, an elevated
`reg query` showed `DisableSelectiveSuspend` REG_DWORD `0x1` as the previous
INF wrote it; `reg delete ... /v DisableSelectiveSuspend /f` reported success
and a second query showed only `FastS4_OverrideBiosS4`. This was the first
Vista x64 boot ever taken without the value. On the observation boot the
value-less build showed the **boot-time pair** Vista x86 showed -
`SuspendController` (line 614) and `ResumeController` (line 635) within
seconds of `StartController` (line 22) - which the test leg did not. It then
ran like the test leg until provoked: twelve reads of `0x5`/`0x0` from
20:15:15 to 20:20:27, a keyboard hot-plugged at 20:20:37 addressed within 6 s
(line 997), and four reads of `0x5`/`0x0` over the 63 s after its unplug.
The flip, typed identically: AC 0 / DC 1 before, `-setacvalueindex ... 1`,
`-setactive` with Enter at 20:24:08. The controller read `0x5`/`0x0` at
20:24:09 and **`0x0`/`0x1` (halted) at 20:24:14**, with `SuspendController`
at line 1449, and stayed halted at every read to 20:26:46. A second keyboard
hot-plugged at 20:24:45 was `Device 0.0` at +5, +20 and +40 s with the
controller still halted and no `devices addressed=00000002`; the after-query
read AC 1 / DC 1. One `ResumeController` (line 1482) came after the 20:26:46
read and before the power-down's `SuspendController` (1566) and
`StopController` (1570).

**Verdict: PASS.** On Vista x64 the flag removes the boot-time pair and keeps
the controller running through the Balanced plan flip that halts the
value-less previous build within 5 s. A caution for anyone typing into a
guest from the QEMU monitor: while a hot-plugged `usb-kbd` is attached,
`sendkey` goes to that keyboard, so on a halted control whose keyboard the
stack never saw the keys are lost. Unplug it first.

**Not yet read at run time:** the two SweetLow-stack
9x targets (Windows 98 SE under SweetLow's USB 2.0 stack, and Windows ME).
Until those are done, "read at run time on every target" is not a claim this
repository can make. Every reading here is a virtual-machine reading; none of
this has been taken on real hardware.

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
- ~~Vista x64 and Windows 7 x64 were never read without the value.~~
  **Windows 7 x64 was read without it on 2026-09-17** (section 5.5): its
  control leg idles. **So was Vista x64, the same evening** (section 5.5): its
  control shows the boot-time pair and halts within 5 s of the plan flip.
- ~~The flag of section 5.4 has not been read at run time on any target.~~
  **Eight of the ten targets were read on 2026-09-17** (section 5.5), each
  against a control leg that idles, Windows 7 x86's 9-second case and Vista
  x64 included. **Two remain: the two SweetLow-stack targets** (Windows 98
  SE under SweetLow, Windows ME), each of which needs its stack installed
  before the legs can run. Windows 2000's control leg needs an explicit
  `DisableSelectiveSuspend = 0`, because its usbport defaults the value to 1
  when the WDM version check says 1.10 but not 1.20 and so never idles
  otherwise (static: NUSB `0x1082B`, SP4 `0x10890`; this is also what
  explains Phase 20's F18) - that is how its leg was run. On both Vista
  guests the plan's USB selective suspend setting was flipped to Enabled
  during each leg, since that is what broke the registry route and, beyond
  the boot-time pair, it is the only discriminator Vista offers.
- ~~`USBPORTBUSIF_UsbdQueryControllerType` hands the raw `MiniPortFlags` word
  to its callers on Vista and Windows 7, and those callers were not read.~~
  **Closed 2026-09-17** (section 5.4): no `usbhub.sys` on any of the four NT
  6.x builds calls that slot at all. What remains is not a gap in the reading
  but a limit on what any reading here can cover - a third-party driver
  holding the USBDI interface on a particular machine could ask, and this
  repository cannot enumerate what is not on the machine.

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
  experiment early. And the converse cost a month: one boot that
  contradicted the binary's own flag logic was taken as a refutation of the
  per-controller value instead of a reading to repeat.
- A value has to be checked for its content, not its presence: present and
  0 is absent.
- **A setting that works is not the same as a setting that holds.** The
  per-controller value was read correctly by every one of the nine builds
  and would have passed any install-time check; what disqualified it is that
  Vista's own power plan writes it back, on a callback nobody invokes by
  hand, with a default that differs on battery. "Does the OS honour this?"
  and "does the OS leave it alone?" are two questions, and only the first
  one gets asked by default.
- Where a mechanism can live in the driver or in the registry, the driver is
  the narrower place. The registry value had to be delivered from four
  install routes, survive an upgrade that bugchecks mid-install, outlive
  nothing, and mean the same thing under two different names on two OS
  families sharing one install section. The flag is one bit in a structure
  this driver already fills in.

## Sources

- [lessons.md](../contributing/lessons.md), "Batch 11-V stage A: the Win98
  idle hot-plug defect is FIXED by one registry value" (the differential,
  the usbehci listing, the specification pages, the two closed candidates,
  the usbport strings and the measured table) and its postscript.
- [build-and-test.md](../contributing/build-and-test.md): the idle-suspend
  paragraph (Windows 2000 SP4's bounded observation, the value = 0 reading),
  the SweetLow stack section (the reading under the XP-lineage rebuild),
  and "Windows Vista and Windows 7 target VMs" (the NT 6.x readings of
  section 5).
- `src/xhci_dispatch.c`, the comment block above `XHCI_MINIPORT_FLAGS` (the
  flag, the nine addresses it was read at, and why it replaced the value),
  and `src/xhci98.inf` under "Idle suspend" (what the INF stopped writing
  and what upgraders keep).
- [release-notes.md](../using/release-notes.md), "Known limitations", the
  `DisableSelectiveSuspend` entry.
- [roadmap.md](../contributing/roadmap.md): task 11-V.6, task 19.2 (the NT
  path), Phase 20 finding F18.
- [legal-provenance.md](../contributing/legal-provenance.md) section 4: the
  static rows for `usbehci.sys` `SuspendController`, the usbport registry
  reads, the NT 6.x usbport strings, and - for section 5.4 - the selective
  suspend gate on all nine builds, the `MiniPortFlags` bit 0x20 sweep, the
  hub callers of `ControllerSelectiveSuspend`, and the Vista x86 runtime row
  for the power-plan rewrite.
- xHCI 1.2c: Figure 4-34 and its note (p.294), section 4.19.3 (p.295),
  `USBSTS` (p.364).
