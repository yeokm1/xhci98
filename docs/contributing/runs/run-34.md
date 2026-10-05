# Phase 34 Record - The Registry Values, the Device Manager Pages and the Intel Port Switchover

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 34 - Release
`2.1.1.0`". The roadmap entry carries the goal, the status, the task list and
the checkpoint; this file carries the hardware legs' procedures and what each
reading said. Where the two disagree about a clause, the roadmap wins.

**Written while the phase is open**, ahead of the hardware readings, so that
they only need filling in. A cell reading `TBD` is a clause still to be read.

**On `out\...` and `vm\...` paths in this file.** They say where a reading
was taken on the host that ran it; they are not files a clone has.

**Hardware, not VM (owner, 2026-10-05).** Tasks 34.2 and 34.4 are ticked on
real hardware. 34.4's switchover is read on the owner's Lenovo B490 (Ivy
Bridge), and its gate staying closed on the P14s Gen 1 (Comet Lake, 02ED).
Design record 16 is the switchover's design; its section 10 is the list this
file's 34.4 legs follow.

Opened: 2026-10-05.

## 34.1 - the registry values

Read on Windows 98 SE, Windows 2000 and Windows 7 x86 in virtual machines;
the roadmap's 34.1 and 34.1-V entries carry the readings.

## The package for the hardware legs

One `release` x86 package built at `f393bb6` (34.2 on top of 34.4), staged
2026-10-05 on development host A as `out\t342-344\release-x86\` with
`XHCISNAP.EXE` beside it: `xhci98.sys` 171099 bytes, SHA-256
`24F2A49BAE9182A0861D94216C17A520D2C67090F9FB8174CC003AC4386096C4`. Every
hardware leg below names the hash it ran.

On each machine, before the leg:

1. Install the package over what is there (Device Manager, the xHCI
   controller, Update Driver, Have Disk; on Windows 98 SE, "Display a list",
   then Have Disk, and the Windows 98 CD if asked).
2. Run `XHCISNAP -verbosity 2` from the package's `xhcisnap\` directory, then
   restart. The driver's log ring records only above verbosity 1.
3. After the leg: `XHCISNAP -o C:\<name>` writes `C:\<name>.TXT`, then
   `XHCISNAP -disable` turns the channel off again.

## 34.2-H - the Power tab on the P14s Gen 1

Reading taken before the fix (owner, 2026-10-05): a SuperSpeed bus-powered
device that UsbTreeView reads at 896 mA under Windows 11 (bMaxPower 0x70)
read 224 mA on the Power tab under `2.1.0.0` on Windows 98 SE, a quarter.

Use devices, not hubs, for clauses 1 to 3: the page costs a bus-powered hub
as (ports + 1) x 100 mA, capped at 500, whatever its descriptor says
(design record 13 section 8.3).

| # | Clause | Expected | Read |
|---|---|---|---|
| 1 | The same 896 mA device, same connector, at SuperSpeed (the `XHCISNAP` report shows it at SuperSpeed) | The root hub's Power tab shows 510 mA | **Pass** (owner, 2026-10-05): 510 mA, where `2.1.0.0` showed 224 mA |
| 2 | A SuperSpeed device declaring under 510 mA (UsbTreeView under Windows 11 gives its MaxPower first) | The Power tab shows exactly UsbTreeView's mA | **Not read**: the owner has no such device. The path is `test_desc`'s vectors (96 mA and 504 mA exact) |
| 3 | A USB 2.0 device | Unchanged from `2.1.0.0`: UsbTreeView's mA | **Pass** (owner, 2026-10-05): unchanged |
| 4 | The `XHCISNAP` report | `dev.ss.vidpid` and `dev.ss.maxpower.ma` for each SuperSpeed device; for the 896 mA device `dev.ss.maxpower.ma=00000380` | **Pass** (2026-10-05, report `P14S.TXT`, `XhciLogVerbosity` 2, the release build of `f393bb6`, its `psw.` and `dev.ss.` records being that build's): `dev.ss.vidpid=090C2320`, `dev.ss.maxpower.ma=00000380` (896 mA), the device in slot 5 on root port 16 at "SuperSpeed, 5 Gbit/s, Gen 1x1" |

## 34.4-Q - QEMU (passed)

Read 2026-10-05 on development host A, QEMU 11.1.0 TCG, a Windows 98 SE +
NUSB 3.3 guest on the `qemu` flavour (`xhci98.sys` SHA-256
`19291E9256912FA8FA905F207E8762216B0F08B2FDF3EE777BE00C1539C9E3C5`, built
at `bfe8f97`), overlay `vm\t33\w98n-t344.qcow2`, log
`out\phase33\legs\w98n\t344\debugcon-2.log`:

- line 7, "hcd: port switchover: not an Intel 7/8/9-series xHCI, no config
  write", once, at the one controller start;
- line 8, `PCI vendor/device=000D1B36`;
- no line naming `USB3_PSSEN`, `XUSB2PR` or a release;
- the No Op self-test completed (lines 66-67), the root hub's FDO attached
  and settled (lines 144-156), and Device Manager showed the controller and
  root hub without a yellow bang.

An "Unknown Device" at Code 28 under Other devices, with no USB device
attached and none addressed, is taken as the base image's; it was not
checked.

## 34.4-H1 - the gate closed on the P14s Gen 1

Same session as 34.2-H.

| # | Clause | Expected | Read |
|---|---|---|---|
| 1 | The `XHCISNAP` report | `psw.gate=00000000`; no other `psw.` record | **Pass** (2026-10-05, the same `P14S.TXT`): `hc.pci=02ED8086`, `psw.gate=00000000` before the start's gates, no other `psw.` record; the controller started (`imod.readback`, `door.interface=00000001`) with all 18 ports managed |
| 2 | Every external connector, a USB 2.0 and a SuperSpeed device on each | Works as under `2.1.0.0` | **Pass** (owner, 2026-10-05) |

## 34.4-H2 - the switchover on the Lenovo B490

The B490's firmware offers Disabled, Enabled, Auto and Smart Auto for USB 3.0
(owner, 2026-10-05). The leg runs on **Auto**, the setting under which
firmware leaves the switchable connectors on EHCI for the OS to move. Then
clauses 1, 2, 3 and 8 again on **Smart Auto**, under which firmware is said
to restore at reboot the routing the OS last set, so clause 8 tests whether
the shutdown's hand-back holds. **Enabled** (firmware routes to xHCI itself,
so the driver's writes change nothing) is a control, run only if Auto shows
a fault, to separate the switchover from the rest of the driver. Disabled
hides the xHCI and cannot run this leg. Each setting's behaviour above is
Intel's documented intent, not a reading.

| # | Clause | Expected | Read |
|---|---|---|---|
| 0 | The machine has the mux | Device Manager under any modern OS, or `XHCIQUAL`, shows `8086:1E31` (the HM77 variant); a B490 with the HM70 has no USB 3.0 and cannot read this leg. Note which connectors are blue | Owner, 2026-10-05: its USB connectors carry the SuperSpeed logo, so the USB 3.0 variant; the id `TBD` |
| 1 | Firmware's routing before the driver, with the USB 3.0 setting on Auto | `XHCIQUAL`'s report of `XUSB2PR`, `XUSB2PRM`, `USB3_PSSEN` and `USB3PRM`; predicted on Auto (`xhci-programming.md`): `XUSB2PR` and `USB3_PSSEN` 0, the masks nonzero | `TBD` |
| 2 | The `XHCISNAP` report after the driver starts | `psw.gate=00000001`, `psw.value=00000001`, `psw.route.step=00000000`, and `psw.usb3pssen` equal to `psw.usb3prm`, `psw.xusb2pr` equal to `psw.xusb2prm` | `TBD` |
| 3 | A SuperSpeed stick on a blue (switchable) connector | Under the xHCI controller, at SuperSpeed in the `XHCISNAP` report; a file round-trips | `TBD` |
| 4 | A USB 2.0 device on a switchable connector | Under the xHCI controller, working | `TBD` |
| 5 | A connector outside the masks, if the machine has one | Stays on EHCI | `TBD` |
| 6 | Device Manager: disable the xHCI controller, then enable it | Disabled: the switchable connectors work again under EHCI if an EHCI driver is loaded. Enabled: back under xHCI | `TBD` |
| 7 | `XhciIntelPortSwitch` set to 0, then a restart | `psw.value=00000000`, no route record; the connectors stay where firmware put them | `TBD` |
| 8 | The value back at 1, then a restart (the value is read only at start, so without it the driver would not hold the routing and the shutdown would release nothing), the report showing a route record with `psw.route.step=00000000`; then shut down | The machine stays off. On the next boot, `XHCIQUAL` (before Windows) shows the routing back at firmware's values | `TBD` |
| 9 | Standby and resume, if the OS offers it | The connectors work after the resume; a second route record in the report | `TBD` |
| 10 | A device attached under EHCI when the driver starts (with an EHCI driver loaded) | It disconnects from EHCI and reappears under xHCI. Not a storage device with writes in flight | `TBD` |

## Codex

The review rounds of 34.4 are listed in the roadmap's 34.4 entry.
