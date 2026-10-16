# Tuning

Every registry setting of the xHCI98 controller driver: where it is, its range and default, and what it does. The [README](../../README.md#tuning) lists the few most often changed.

## Contents

- [Where the values are, and every value](#where-the-values-are-and-every-value)
- [The interrupt moderation interval (`XhciImodInterval250ns`)](#the-interrupt-moderation-interval-xhciimodinterval250ns)
- [Forcing Bulk-Only instead of UAS (`XhciForceBulkOnly`)](#forcing-bulk-only-instead-of-uas-xhciforcebulkonly)
- [Polling a Low- or Full-Speed mouse above 1000 Hz (`XhciFastPollFsLs`)](#polling-a-low--or-full-speed-mouse-above-1000-hz-xhcifastpollfsls)
- [The first report's wait (`XhciFirstEnumWaitMs`, `XhciFirstEnumPortMs`)](#the-first-reports-wait-xhcifirstenumwaitms-xhcifirstenumportms)
- [The Intel 7/8/9-series port switchover (`XhciIntelPortSwitch`)](#the-intel-789-series-port-switchover-xhciintelportswitch)
- [The Intel 7/8/9-series interrupt routing repair (`XhciIntelIrqRoute`)](#the-intel-789-series-interrupt-routing-repair-xhciintelirqroute)
- [Polling and message-signalled interrupts (`XhciInterruptMode`, `XhciPollIdleMs`, `XhciPollActiveMs`, `XhciPollFineTimer`)](#polling-and-message-signalled-interrupts-xhciinterruptmode-xhcipollidlems-xhcipollactivems-xhcipollfinetimer)
- [UAS command size and two transfer pieces at once (`XhciMaxTransferKB`, `XhciWideMap`)](#uas-command-size-and-two-transfer-pieces-at-once-xhcimaxtransferkb-xhciwidemap)
- [Low- and Full-Speed devices behind a built-in hub (`XhciUsb2HubSpeedFallback`)](#low--and-full-speed-devices-behind-a-built-in-hub-xhciusb2hubspeedfallback)
- [Selective suspend (`XhciSelectiveSuspend`)](#selective-suspend-xhciselectivesuspend)
- [The silent install (`XhciSilentInstall`)](#the-silent-install-xhcisilentinstall)
- [Controller faults (`XhciTolerance`, `XhciIntervalCap`, `XhciAvgTrbEsit`)](#controller-faults-xhcitolerance-xhciintervalcap-xhciavgtrbesit)
- [Linux's controller quirks (`XhciMsiAvoid` and thirteen more)](#linuxs-controller-quirks-xhcimsiavoid-and-thirteen-more)

## Where the values are, and every value

Each value below is a `DWORD` in the controller's driver (software) key. Here is where to find the key:

| Windows | Key |
|---|---|
| 98 SE, ME | `HKLM\System\CurrentControlSet\Services\Class\USB\NNNN` |
| 2000, XP, Vista, 7 (x86/x64) | `HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\NNNN` |

`NNNN` is the subkey whose `DriverDesc` is "xHCI98 USB 3.x eXtensible Host Controller". The number varies from machine to machine.

**The driver's own settings**

| Value | What it sets | Minimum | Maximum | Default when absent | Written by the install |
|---|---|---|---|---|---|
| `XhciImodInterval250ns` | The interrupt moderation interval | `10` | `4000` | `4000` | `160` |
| `XhciForceBulkOnly` | Bulk-Only instead of UAS | `0` | `1` | `1` | `0` |
| `XhciFastPollFsLs` | A root-port Low- or Full-Speed mouse polled above 1000 Hz | `0` | `4` | `0` | `0` |
| `XhciFirstEnumWaitMs` | The longest wait for a hub's first report | `0` | `30000` | `5000` | `5000` |
| `XhciFirstEnumPortMs` | The longest one port may hold that wait | `0` | `XhciFirstEnumWaitMs` | `2000` | `2000` |
| `XhciTolerance` | The handling of controller faults, all together; `0` turns most of it off (see "Controller faults") | `0` | `1` | `1` | `1` |
| `XhciMaxTransferKB` | The largest UAS command, in KiB (a multiple of `4`) | `64` | `2048` | `1024` | `1024` |
| `XhciInterruptMode` | How the controller's events arrive: `0` automatic, `1` the interrupt line only, `2` polling, `3` a message-signalled interrupt only (see "Polling and message-signalled interrupts") | `0` | `3` | `0` | `0` |
| `XhciPollIdleMs` | While polling, the period with nothing outstanding, in ms | `1` | `5000` | `50` | `50` |
| `XhciPollActiveMs` | While polling, the shortest period with transfers outstanding, in ms (with only a mouse's or keyboard's periodic transfers outstanding, the endpoint's interval is used, held between this and the idle period) | `1` | `XhciPollIdleMs` | `1` | `1` |
| `XhciPollFineTimer` | While polling at `XhciPollActiveMs`, a finer system clock asked of Windows (2000 to 7; Windows 98 SE and ME ignore it); off unless set: any value but `0` turns it on (see "Polling and message-signalled interrupts") | `0` | `1` | `0` | `0` |
| `XhciWideMap` | Two pieces of a large bulk transfer on the controller at once (2000 to 7) | `0` | `1` | `0` | `0` |
| `XhciSelectiveSuspend` | Selective suspend of idle devices when Windows asks | `0` | `1` | `0` | `0` |
| `XhciSilentInstall` | Windows 98 SE and ME: `1` installs a device whose class is in the `SilentClasses` list with no wizard (`silent-on.bat` and `silent-off.bat` in `tools\silent\`; see "The silent install" below) | `0` | `1` | `0` | `0` |

**Hardware quirks**, each for a particular controller or chipset

| Value | What it sets | Minimum | Maximum | Default when absent | Written by the install |
|---|---|---|---|---|---|
| `XhciIntelPortSwitch` | The Intel 7/8/9-series port switchover; `1` turns it on, `2` applies it to any Intel controller **at your own risk** (`portsw.vbs` sets `0` or `1`) | `0` | `2` | `0` | `0` |
| `XhciIntelIrqRoute` | The Intel 7/8/9-series interrupt routing repair; `1` the Dell Latitude E7250 only, `2` any reviewed Intel chipset, `0` report only | `0` | `2` | `1` | `1` |
| `XhciIntervalCap` | Interrupt endpoints polled at least every 32 ms: `1` on AMD controllers and the ATI Navi10's USB-C controller (`1002:7316`), `2` on every controller, `0` off | `0` | `2` | `1` | `1` |
| `XhciAvgTrbEsit` | A comparison switch for interrupt endpoints; leave at `0` unless comparing | `0` | `1` | `0` | `0` |
| `XhciUsb2HubSpeedFallback` | The standard speed ID for a Low- or Full-Speed device behind a hub where the controller's USB 2.0 speed table names neither (the VIA VL805); `0` turns it off | `0` | `1` | `1` | `1` |
| `XhciAccept096` | Controllers below xHCI 1.0 started (the 0.96 NEC/Renesas uPD720200 and uPD720200A, ASMedia ASM1042), driven as 1.0 ones but for the fields Linux writes differently on them; `0` refuses them (Code 10) as `2.3.1.0` did | `0` | `1` | `1` | `1` |

**Linux's controller quirks**, each for the controllers Linux names (see "Linux's controller quirks" below): `0` off, `1` the controllers listed there, `2` every controller where the row says so

| Value | What it sets | Minimum | Maximum | Default when absent | Written by the install |
|---|---|---|---|---|---|
| `XhciMsiAvoid` | Vista and 7: never a message-signalled interrupt (MSI), the line or polling instead; `2` on every controller | `0` | `2` | `1` | `1` |
| `XhciStreamsOff` | No UAS streams: Bulk-Only, or UAS without streams at USB 2.0; `2` on every controller | `0` | `2` | `1` | `1` |
| `XhciResumeReinit` | After standby or hibernation the controller is started afresh and its devices found again, instead of restored; `2` on every controller | `0` | `2` | `1` | `1` |
| `XhciIntelShutdownToEhci` | At shutdown, the Intel switchable connectors handed back to the USB 2.0 (EHCI) controller, whatever `XhciIntelPortSwitch` says; `2` is read as `1` | `0` | `1` | `1` | `1` |
| `XhciShutdownReset` | At shutdown, the controller reset once it has halted; `2` on every controller | `0` | `2` | `0` | `0` |
| `XhciSuspendHaltDelay` | A 1.5 ms pause before the controller is halted for standby; `2` on every controller | `0` | `2` | `1` | `1` |
| `XhciSuspendHaltTimeout` | Up to 640 ms, not 32 ms, for the controller to halt for standby; `2` on every controller | `0` | `2` | `1` | `1` |
| `XhciAsmediaFlowControl` | ASMedia's flow-control setting after each start, recovery and resume; `2` is read as `1` | `0` | `1` | `1` | `1` |
| `XhciTiIntervalCap` | High-Speed and faster interrupt endpoints polled at least every 8 ms; `2` on every controller | `0` | `2` | `1` | `1` |
| `XhciStuckLinkReset` | After a resume, a USB 3 port stuck in its link's Polling state reset; `2` on every controller | `0` | `2` | `1` | `1` |
| `XhciStreamRingPad` | Spare memory after each block of UAS stream rings; `2` on every controller | `0` | `2` | `1` | `1` |
| `XhciControlLinkNoOp` | A SuperSpeed control transfer kept from straddling the end of its ring; `2` on every controller | `0` | `2` | `1` | `1` |
| `XhciIsochSpuriousSuccess` | A false "success" after an audio packet's error ignored; `2` on every controller | `0` | `2` | `1` | `1` |
| `XhciAmdSuspendBitClear` | One AMD register bit cleared before standby; `2` is read as `1` | `0` | `1` | `1` | `1` |

**The driver's log**

| Value | What it sets | Minimum | Maximum | Default when absent | Written by the install |
|---|---|---|---|---|---|
| `XhciLogVerbosity` | The driver's log, read by `XHCISNAP` | `0` | `4` | `0` | `0` |
| `XhciLogDebugView` | The log sent to DebugView as well | `0` | `1` | `0` | `0` |

The xHCI98 USB Tree (`XHCITR`) shows and sets `XhciLogVerbosity` in every `xhci98.sys` controller's driver key, and the Thunderbolt driver's own level, `XhciCmLogVerbosity` in its controller's driver key, beside it: View > Logging level..., with "Set as administrator..." on Windows 2000 to 7 for a user without administrator rights (the release notes, "The xHCI98 USB Tree"). It writes only `0` to `4`, and offers the restart the new level needs.

The install writes every value in these tables, at the number in its last column, so each one is already in the key, ready to change. An install or update writes a value only where it is missing, so a value you changed stays as you set it. To go back to the table's value, set it again, or delete the value and update the driver.

Restart Windows after changing a value.

The two log values are described in the [release notes](release-notes.md), "The log, and how to send one". The others are described below, and all of them in the release notes' "Registry settings".

## The interrupt moderation interval (`XhciImodInterval250ns`)

`XhciImodInterval250ns` sets the shortest gap the controller leaves between two interrupts, in units of 250 ns. A shorter gap makes USB storage faster at the cost of more interrupts.

- `10` (the lowest accepted): 2.5 us.
- `160` (written by the install): 40 us, at most 25,000 interrupts a second. Linux uses the same value.
- `4000`: 1 ms, the controller's own power-on value.

A value out of range is replaced by `4000`, not rounded to the nearest limit, so a mistyped `0` cannot turn moderation off. If you get audio stutter or instability under load, raise it towards `4000` or delete it.

On my P14s with the MSSU10 over UAS (Windows 98 SE, 8 MB transfers), `500` gave about 181 MB/s, `160` gave 211 MB/s write and 221 MB/s read, and `40` added only 1 to 3% more.

## Forcing Bulk-Only instead of UAS (`XhciForceBulkOnly`)

`XhciForceBulkOnly` decides which transport a storage device that offers both uses.

- `0` (what the install writes): UAS wherever the device offers it.
- `1`: Bulk-Only (Windows' own `usbstor.sys`) on every such device on the controller.
- Absent: as `1`. That is how text-mode Setup sees it, before any INF has run, and Setup has only Windows' own `usbstor` for storage.

A UAS-only device stays on UAS either way. The value is read each time a device enumerates, so unplug and replug the drive after changing it. On Windows 2000 and later, a drive already installed keeps its driver until you uninstall it in Device Manager and replug it.

## Polling a Low- or Full-Speed mouse above 1000 Hz (`XhciFastPollFsLs`)

`XhciFastPollFsLs` polls a mouse faster than 1000 Hz, together with SweetLow's hidusbf. It is read when the controller starts, so restart after changing it.

- `0` (default): off.
- `2` or `3`: a mouse on a **root port**, set in hidusbf to "31 Hz" or "62 Hz", is polled at the rate below. A device behind a hub keeps its normal rate, and any root-port Low- or Full-Speed device that asks for 16 to 63 ms is polled faster too.

  | Value | "31 Hz" becomes | "62 Hz" becomes |
  |---|---|---|
  | `2` | 2000 Hz | 4000 Hz |
  | `3` | 4000 Hz | 8000 Hz |

- `4`: the rate the client chooses. An interrupt endpoint set to `128`, `129` or `130` is polled at 2000, 4000 or 8000 Hz, and every other interval as normal. It covers a Low- or Full-Speed device on a root port, and a High-Speed or SuperSpeed device at a root port or behind a hub.

With `4`, set the value and restart first, then set hidusbf (or another filter) to `128`, `129` or `130`. A device that itself asks for 128 to 130 ms is polled that fast too, and a code written without `4` set means 128 ms (Low or Full Speed) or 4.096 s (High Speed or SuperSpeed).

This is outside the xHCI specification, and untested ground. A controller that refuses it is caught, and the device falls back to its normal rate (every 1 ms for a `4` code). A controller that accepts it and then misbehaves cannot be caught.

## The first report's wait (`XhciFirstEnumWaitMs`, `XhciFirstEnumPortMs`)

When the root hub or an external hub first reports its devices, the driver waits for the devices already plugged in to be ready, so Windows 2000's text-mode Setup sees them.

- `XhciFirstEnumWaitMs`: the longest wait, in ms. Default `5000`, at most `30000`; `0` turns it off.
- `XhciFirstEnumPortMs`: the longest one port may hold that wait. Default `2000`, held to the total.

With the defaults, the root hub's first report typically comes 20 to 30 ms after its start with nothing plugged in, and 0.3 to 0.9 s after it with a mouse and a stick.

## The Intel 7/8/9-series port switchover (`XhciIntelPortSwitch`)

On Intel 7-, 8- and 9-series chipsets (Ivy Bridge to Broadwell) and C610/X99, each switchable connector, usually a blue one, is wired to both the USB 2.0 (EHCI) and the xHCI controller. With the firmware's USB 3.0 setting on Auto, or no setting at all, those connectors start on the USB 2.0 controller.

On these machines, a USB 3 stick or drive that copies only at USB 2.0 speed on a blue connector is most likely still on the USB 2.0 controller, because the switchover is off by default. Run `portsw-on.bat` and restart Windows.

`XhciIntelPortSwitch` lets the driver move them to the xHCI controller at each start and resume, and hand them back when the controller is disabled or removed, or the machine shuts down. The hand-back writes only the registers the driver itself wrote.

- `0` (default): off.
- `1`: on, for Intel xHCI device ids `1E31`, `8C31`, `9C31`, `8CB1`, `9CB1` and `8D31`.
- `2`: on any Intel xHCI controller, listed or not; on a listed one it is the same as `1`. **At your own risk**: see below.

With `2`, on a chipset without these registers (every Intel chipset from the 100-series on, and any not yet read) the driver writes registers of unknown meaning, with unknown results. Use it only for an unlisted Intel chipset with both EHCI and xHCI.

A device on a switchable connector under a running USB 2.0 driver (NUSB's, for example) is disconnected there when this driver starts, and comes back under it, so do not have a drive busy on a blue connector at that moment.

The value is read when the controller starts, so restart Windows after changing it.

`portsw.vbs`, in the download's `tools\portsw\` directory, shows the value on every xhci98 controller and sets it to `1` or `0`. Double-click it to see the value and be asked for a new one, or run it from a command prompt in that directory:

```
cscript //nologo portsw.vbs show
cscript //nologo portsw.vbs 1
cscript //nologo portsw.vbs 0
```

- `show`: the value on every xhci98 controller.
- `1`: turns the switchover on.
- `0`: turns it off.

Setting `1` or `0` needs an administrator; on Vista and 7 the script asks Windows for that itself. It never sets `2`, which is set by hand.

`portsw-on.bat` and `portsw-off.bat`, beside it, do the same in one double-click: each shows the value, sets `1` or `0` and says to restart. On 2000 and XP, run them as an administrator; on Vista and 7, right-click one and choose Run as administrator. At a prompt on 98 SE and ME, change to `tools\portsw\` first. Then restart Windows.

## The Intel 7/8/9-series interrupt routing repair (`XhciIntelIrqRoute`)

Two chipset registers decide which interrupt line the xHCI controller's interrupt reaches. Some firmware points them at a different line from the one it tells Windows: the Dell Latitude E7250 sends it to IRQ 16 while Windows listens on IRQ 21, so every USB device on it is slow or stops.

`XhciIntelIrqRoute` lets the driver read those registers at each start and resume and point the interrupt at the line Windows assigned.

- `0`: read and report only.
- `1` (default): only on the E7250 (`8086:9CB1`, subsystem `1028:062D`, chipset `9CC3`).
- `2`: on any of the six Intel controllers listed above, each with one of its own chipset ids.

It writes nothing on any other controller.

## Polling and message-signalled interrupts (`XhciInterruptMode`, `XhciPollIdleMs`, `XhciPollActiveMs`, `XhciPollFineTimer`)

A controller with no interrupt pin is polled instead of refused, and so is one whose line the driver sees raised and never delivered. On Windows Vista and 7 the controller may use a message-signalled interrupt (MSI or MSI-X) instead of its line: on x64 directly, on 32-bit through the helper driver `xhcihlpr.sys` in the package (`xhcimsi.sys` in `2.3.0.0` and `2.3.1.0`).

**The install and every update on 32-bit Vista and 7 ask for a restart**, because the helper starts only at boot. On 32-bit 2000 and XP, which have no message-signalled interrupts, the helper serves only the finer timer below, so the install asks for no restart there. `XhciInterruptMode` chooses how the controller's events reach the driver.

- `0` (default, automatic): picks, in this order:
  1. On Vista and 7, a message-signalled interrupt, where Windows grants one and it connects, except on a controller `XhciMsiAvoid` applies to (see "Linux's controller quirks"), which takes the line or is polled.
  2. The controller's interrupt line, where it has one and Windows assigned it.
  3. Polling: no interrupt pin, none assigned, or a granted message that could not connect.
  4. After the start, a line the driver sees raised that never reaches it switches to polling by itself.
- `1`: the interrupt line only. With no line, the controller fails to start (Code 10).
- `2`: always poll, at the cost of more processor wake-ups. Use it for an interrupt that fails in a way the driver cannot see in the controller's registers.
- `3`: a message-signalled interrupt only (Vista and 7). With no message, the controller fails to start (Code 10).

Under `1`, Vista and 7 are asked for the line, and a controller that is given a message instead (for example because it has no line) fails to start. Under `3`, Vista and 7 are asked for the message; it fails to start when none is granted or it cannot be connected, which on 32-bit means `xhcihlpr.sys` is not running. Windows 98 SE, ME, 2000 and XP have no message-signalled interrupts, so `3` always fails there.

Two more values set the polling periods, in ms:

- `XhciPollIdleMs` (default `50`): with nothing outstanding.
- `XhciPollActiveMs` (default `1`): with transfers outstanding. Windows runs the timer no faster than its clock allows: on the E7250 under XP x64, every 15 to 62 ms.

`XhciPollFineTimer` (default `0`, off; set it to `1` to try it) asks Windows 2000, XP, Vista and 7 to run their clock at `XhciPollActiveMs` while the driver polls at that period.

It gives the request back when it stops polling that fast (one idle period, `XhciPollIdleMs`, after the last transfer), and when the controller sleeps or stops. It applies only while the controller is polled, and it is read at each start (disable and enable the controller, or restart).

On 32-bit Windows it comes through `xhcihlpr.sys`, so on 2000 and XP it starts at the first boot after the install. Windows 98 SE and ME have no such call and keep their clock.

It ships off because, while the finer clock is held, the system clock can fall behind real time and USB keyboard input can be lost. With it off the clock keeps time.

The release notes' "Windows Vista and 7: message-signalled interrupts", "XhciInterruptMode, XhciPollIdleMs and XhciPollActiveMs" and "XhciPollFineTimer" have the details.

## UAS command size and two transfer pieces at once (`XhciMaxTransferKB`, `XhciWideMap`)

A UAS command moves up to 1 MiB by default. Restart after changing either value below.

On Windows 98 SE and ME the practical maximum is 64 KiB: Windows' storage layer there never sends a request larger than that, so a larger setting makes no difference.

- `XhciMaxTransferKB`: the largest UAS command for every UAS drive on the controller, `64` to `2048` KiB in multiples of `4`. Default `1024`.
- `XhciWideMap`: `1` keeps two pieces of a large bulk transfer on the controller at once, on Windows 2000 to 7. **Off by default** (`0`), because a regression would show as wrong data rather than an error.

Microsoft's per-device `MaximumTransferLength` under `HKLM\SYSTEM\CurrentControlSet\Control\usbstor\VVVVPPPP` (`65535` to `2097152` bytes) sets the command size for one drive model instead. The release notes, "XhciMaxTransferKB" and "XhciWideMap", have the details.

## Low- and Full-Speed devices behind a built-in hub (`XhciUsb2HubSpeedFallback`)

Some controllers reach Low- and Full-Speed devices only through a USB 2.0 hub built into the chip, and their USB 2.0 port's speed table names High Speed alone; the VIA VL805 (`1106:3483`) is one. Every mouse, keyboard and other Low- or Full-Speed device on such a controller sits behind that hub.

`XhciUsb2HubSpeedFallback` lets the driver assume such a controller handles Low and Full Speed behind a hub as well, even though its table names only High Speed. The driver then uses the standard speed ID for the device, Full Speed `1` or Low Speed `2`, unless the table already uses that number for another speed.

- `0`: off; such devices are refused.
- `1` (default): on.

A controller whose table lists Low and Full Speed, or has no table, is not affected, and a device on a root port of its own is never given a fallback. Restart after changing it.

## Selective suspend (`XhciSelectiveSuspend`)

The driver can suspend an idle USB 2.0 or SuperSpeed device when Windows asks, and wake it on Windows' request or the device's own remote wake. Hubs, the root hub and the controller are never suspended.

- `XhciSelectiveSuspend` `0` (default): off.
- `XhciSelectiveSuspend` `1`: on.

Restart after changing it. The release notes' "XhciSelectiveSuspend: putting idle devices to sleep" has the details.

## The silent install (`XhciSilentInstall`)

On Windows 98 SE, a device Windows already has drivers for still brings the Add New Hardware wizard whenever Windows makes a new entry for it: on a port it has never used, unless it carries a serial number Windows follows from port to port. With `XhciSilentInstall` at `1`, a device whose class is in the `SilentClasses` list installs with no wizard instead. Windows 2000 and later never read it.

- `0` (default): off. Any value but `1` is off.
- `1`: on.

`silent-on.bat` in `tools\silent\` merges the list (`SILENT.REG`: hubs, boot keyboards and mice, Bulk-Only storage) and then sets `1`. `silent-off.bat` sets `0` and keeps the list, as another USB stack may read it too. Restart after either. An update keeps the `1`.

Run `silent-on.bat` only once each listed class's drivers have been installed at least once, by plugging such a device and finishing its wizard. With no wizard, 98 SE asks for a missing driver file from its CD.

With it on, Windows 98 SE installs a keyboard, mouse, stick and hub, each a device of its own, with no wizard. Windows ME installs those with no wizard even without it. Composite keyboards, mice and sticks may still bring up the wizard.

SweetLow provided the information on how Windows decides a silent install, and the list.

## Controller faults (`XhciTolerance`, `XhciIntervalCap`, `XhciAvgTrbEsit`)

The driver does not leave a device dead until it is replugged after a transfer, port or controller fault. This follows a tester's report of a mouse that stopped at random on an AMD AM5 board. A missed interrupt is picked up by the driver itself, and a USB transaction error is retried up to three times before the transfer fails.

A fault the driver cannot tie to a transfer re-enumerates the device, as if it had been replugged. A USB 2.0 port the controller disabled, or one that reported an over-current, is brought back. A controller that halts is recovered, and one that stops answering is closed off safely.

Each is tried a few times at most; then the port or the controller is held until the device is unplugged or the controller is restarted. None of it acts unless a fault is reported.

`XhciTolerance` switches all of it together.

- `0`: most of it off.
- `1` (default): on.

`XhciIntervalCap` polls an interrupt endpoint that asks for more than 32 ms every 32 ms instead, as Linux does on some AMD controllers and on the ATI Navi10's USB-C controller.

- `0`: off.
- `1` (default): on AMD controllers (PCI vendor `1022`) and on `1002:7316`.
- `2`: on every controller.

`XhciAvgTrbEsit` changes one figure the controller is told for each interrupt endpoint: its average transfer size, which the controller uses in its own bandwidth reckoning. It is a switch for comparison: if your devices misbehave, try `1` and see whether anything changes. Only interrupt endpoints are affected.

- `0` (default): 1024 bytes, the figure the xHCI specification suggests.
- `1`: the endpoint's largest transfer per service interval, as Linux does.

All three are read when the controller starts, so restart after changing one.

## Linux's controller quirks (`XhciMsiAvoid` and thirteen more)

Linux's xHCI driver works around faults of particular controllers, each named by its PCI vendor and device ID. This driver takes fourteen of them, each behind a value of its own. The values, the faults and the controllers are Linux's. The code is this driver's own, written from [design record 20](../contributing/design/20-linux-xhci-quirks.md), which also lists the quirks not taken and why.

| Value | Linux flag | Controllers (`vendor:device`) |
|---|---|---|
| `XhciMsiAvoid` | `XHCI_BROKEN_MSI` | Fresco Logic `1B73:1000`, `1B73:1400` |
| `XhciStreamsOff` | `XHCI_BROKEN_STREAMS` | Fresco Logic `1B73:1000`, `1B73:1009`; Etron `1B6F:7023`, `1B6F:7052`; VIA `1106:3432`; ASMedia `1B21:1042`; Cadence `17CD:0200` |
| `XhciResumeReinit` | `XHCI_RESET_ON_RESUME` | AMD `1022:15E5`; Etron `1B6F:7023`, `1B6F:7052`; Renesas `1912:0015`; every VIA (`1106`) controller; Phytium `1DB7:DC27`; ASMedia `1B21:3042`; Zhaoxin `1D17:9202` |
| `XhciIntelShutdownToEhci` | `XHCI_SPURIOUS_REBOOT` | Intel `8086:1E31`, `8086:9C31`, `8086:9CB1` |
| `XhciShutdownReset` | `XHCI_SPURIOUS_WAKEUP` | Intel `8086:9C31`, `8086:9CB1` (off by default) |
| `XhciSuspendHaltDelay` | `XHCI_SUSPEND_DELAY` | AMD `1022:145C`, `1022:15E0`, `1022:15E1`, `1022:43BB` |
| `XhciSuspendHaltTimeout` | `XHCI_SLOW_SUSPEND` | Fresco Logic `1B73:1000` |
| `XhciAsmediaFlowControl` | `XHCI_ASMEDIA_MODIFY_FLOWCONTROL` | ASMedia `1B21:1142` (ASM1042A) |
| `XhciTiIntervalCap` | `XHCI_LIMIT_ENDPOINT_INTERVAL_7` | TI `104C:8241` (TUSB73x0) |
| `XhciStuckLinkReset` | `XHCI_MISSING_CAS` | Intel `8086:22B5`, `8086:9D2F`, `8086:A12F`, `8086:5AA8`, `8086:19D0` |
| `XhciStreamRingPad` | `XHCI_TRB_OVERFETCH` | VIA `1106:3483` (VL805); Zhaoxin `1D17:9202`, `1D17:9203` |
| `XhciControlLinkNoOp` | `XHCI_ETRON_HOST` (part) | Etron `1B6F:7023` (EJ168), `1B6F:7052` (EJ188) |
| `XhciIsochSpuriousSuccess` | `XHCI_ETRON_HOST` (part) | Etron `1B6F:7023`, `1B6F:7052` |
| `XhciAmdSuspendBitClear` | `XHCI_DISABLE_SPARSE` | AMD `1022:15E5` |

`XhciIntervalCap` (above) also covers Linux's `XHCI_LIMIT_ENDPOINT_INTERVAL_9` on the ATI Navi10's USB-C controller, `1002:7316`.

- `0`: off on every controller.
- `1` (the default, except `XhciShutdownReset`, which is `0`): on the controllers in the table.
- `2`: on every controller, except `XhciIntelShutdownToEhci`, `XhciAsmediaFlowControl` and `XhciAmdSuspendBitClear`, which are vendor-specific (a `2` there is read as `1`).

Absent, not a `DWORD`, or any other number is the default. The revision of the controller is logged but never matched. Each value is read when the controller starts, so restart after changing one. `XHCISNAP`'s report has a "Linux quirks" block: each value as read, whether this controller is listed, whether the quirk applies, and what it last did.

What `XhciIntelShutdownToEhci` and `XhciShutdownReset` do at shutdown is logged, but the log lives in memory and is gone at the next start. It can be seen only in a capture of that shutdown: `XhciLogVerbosity` at `2` or above (below it nothing is recorded) and `XhciLogDebugView` set to `1`, with a debug-output viewer or a kernel debugger attached (see the release notes, "The log, and how to send one").

The release notes' "Linux's controller quirks" has each value in full.
