# Phase 34 Record - The Registry Values, the Power Tab and the Intel Port Switchover

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 34 - Release
`2.1.1.0`". The roadmap entry carries the goal, the status, the task list and
the checkpoint; this file carries the hardware legs' procedures and what each
reading said. Where the two disagree about a clause, the roadmap wins.

**Written while the phase is open**, ahead of the hardware readings, so that
they only need filling in. A cell reading `TBD` is a clause still to be read.

**On `out\...` and `vm\...` paths in this file.** They say where a reading
was taken on the host that ran it; they are not files a clone has.

**Hardware, not VM (owner, 2026-10-05).** Tasks 34.2 and 34.3 are ticked on
real hardware. 34.3's switchover is read on the owner's Lenovo B490 (Ivy
Bridge), and its gate staying closed on the P14s Gen 1 (Comet Lake, 02ED).
Design record 16 is the switchover's design; its section 10 is the list this
file's 34.3 legs follow.

Opened: 2026-10-05.

## 34.1 - the registry values

What the install writes: `XhciForceBulkOnly` 0, `XhciFastPollFsLs` 0,
`XhciFirstEnumWaitMs` 5000 and `XhciFirstEnumPortMs` 2000 join the two log
switches at 0 and `XhciImodInterval250ns` at 160 (and, since 34.3,
`XhciIntelPortSwitch` at 1), on every install path of both INFs, each with
FLG_ADDREG_NOCLOBBER (`0x00010003`): an install or update writes only a
missing value. The owner chose the flag the same day over the unconditional
`0x00010001` first built (`9e6389f`), accepting that the footprint's verdict
for those rows is `keep`, that a later release can never change a default
on a machine that already holds the old one, and that a machine updated
straight from `1.2.0.0` keeps that release's `XhciImodInterval250ns` of 500.
`XhciSelectiveSuspend` (SUSPa.5) joins the same rule when it lands.

All readings 2026-10-05, `release` flavour, development host A:

- **The unconditional form** (`9e6389f`), on Windows 98 SE and 2000: a fresh
  install wrote all seven at their defaults, and the update wrote the two
  set by hand back to them.
- **The don't-overwrite form**, QEMU 11.1.0-rc2 TCG; Windows 98 SE NUSB 3.3
  through the Add New Hardware Wizard and Update Driver, Have Disk; 2000 SP4
  through `pnpctl update`: a fresh install wrote all seven as DWORDs at their
  defaults, and over `2.1.0.0` with `XhciLogVerbosity` 3 and
  `XhciFirstEnumWaitMs` 9999 set by hand the update kept both and added
  `XhciForceBulkOnly` 0, `XhciFastPollFsLs` 0 and `XhciFirstEnumPortMs`
  2000. So Windows 98's 16-bit engine honours the flag.
- **34.1-V, Windows 7 x86**, QEMU 11.1.0 TCG, the `win7-clean-autologon`
  guest, installed and updated through `pnpctl update` from an elevated
  prompt, one unverified-publisher box each: `2.1.0.0` installed as
  `oem2.inf` into driver key
  `...\Class\{36FC9E60-C465-11CF-8056-444553540000}\0000` with its three
  values; `XhciLogVerbosity` 3 and `XhciFirstEnumWaitMs` 9999 set by hand
  and still there after a restart; the update installed as `oem3.inf`, a new
  driver-store package, into the same key `0000`, kept both and added the
  same three. So the driver store gives an update over `2.1.0.0` no new
  driver key, and hand-set values survive it. Windows 7 x86 stands for
  Vista and the x64 half, which install through the same store, and needs
  no F8 boot.

The other paths are taken as covered by reasoning, not reading (owner,
2026-10-05, narrowing a leg per install path to one): ME runs 98 SE's
16-bit setup engine on the same undecorated INF half; 32-bit XP, Vista and 7
install through `Xhci.Dev.NTx86` and `Xhci.Dev6.NTx86`, which both name
`Xhci.AddReg.NT`, the lines read on 2000; XP x64 is NT 5.2 SetupAPI, and the
amd64 INF's own `Xhci.AddReg.NT` is held to the same values and flags by the
INF gate; and FLG_ADDREG_NOCLOBBER means the same to every SetupAPI from
2000 on. No fresh-install leg beyond 98 SE and 2000: a fresh install has no
value for the flag to keep, and the gate holds the defaults.

## The package for the hardware legs

One `release` x86 package built at `f393bb6` (34.2 on top of 34.3), staged
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
QEMU cannot show the bug: its SuperSpeed storage declares 0 mA.
The reading after the fix below was also on Windows 98 SE (owner,
2026-10-05).

The fix, as chosen (owner, 2026-10-05). Two were weighed: the proposed
rewrite - `hcdDoorDescriptor` alone, after the configuration copy, sets the
copied bMaxPower to min(255, b * 4) for request code 0 (usbui's),
configuration index 0, a SuperSpeed or SuperSpeedPlus connection and at
least 9 bytes copied, the cached descriptor and returned length untouched,
saturating at 510 mA - and Codex's alternative, descriptors left raw and the
exact demand shown in `XHCISNAP`. Since the 896 mA device would read 510 mA
under the rewrite, the owner chose both. Built the same day: bMaxPower's
units from USB 3.2 Table 9-23 into `xhci-data-structures.md` section 10.7;
`XhciDescMaxPowerMa` and `XhciDescPowerTabByte` in `xhci_desc.c` with
`test_desc`'s vectors (Table 9-23's own example, the owner's 0x70, 504 mA
exact, 512 mA and above at 0FFh); the bus logs `dev.ss.vidpid` and `dev.ss.maxpower.ma`
for each SuperSpeed device at enumeration, which `XHCISNAP` prints from the
log ring (no schema change); design record 13 section 8.3 is the design.
Codex round 5 found no defect in the code (`8820722` took its three P3).

Use devices, not hubs, for clauses 1 to 3: the page costs a bus-powered hub
as (ports + 1) x 100 mA, capped at 500, whatever its descriptor says
(design record 13 section 8.3).

| # | Clause | Expected | Read |
|---|---|---|---|
| 1 | The same 896 mA device, same connector, at SuperSpeed (the `XHCISNAP` report shows it at SuperSpeed) | The root hub's Power tab shows 510 mA | **Pass** (owner, 2026-10-05): 510 mA, where `2.1.0.0` showed 224 mA |
| 2 | A SuperSpeed device declaring under 510 mA (UsbTreeView under Windows 11 gives its MaxPower first) | The Power tab shows exactly UsbTreeView's mA | **Not read**: the owner has no such device. The path is `test_desc`'s vectors (96 mA and 504 mA exact) |
| 3 | A USB 2.0 device | Unchanged from `2.1.0.0`: UsbTreeView's mA | **Pass** (owner, 2026-10-05): unchanged |
| 4 | The `XHCISNAP` report | `dev.ss.vidpid` and `dev.ss.maxpower.ma` for each SuperSpeed device; for the 896 mA device `dev.ss.maxpower.ma=00000380` | **Pass** (2026-10-05, report `P14S.TXT`, `XhciLogVerbosity` 2, the release build of `f393bb6`, its `psw.` and `dev.ss.` records being that build's): `dev.ss.vidpid=090C2320`, `dev.ss.maxpower.ma=00000380` (896 mA), the device in slot 5 on root port 16 at "SuperSpeed, 5 Gbit/s, Gen 1x1" |

## 34.3 - what was built

Beyond design record 16's driver half: `xhciqual`'s `quirks.c` gains the
`9C31` and `9CB1` rows (`QF_XUSB2PR`), and `XhciIntelPortSwitch` joins
the INF gate's `VAL-*` table, its self-tests and both footprints at 1 with
FLG_ADDREG_NOCLOBBER, under 34.1's rule.

## 34.3-Q - QEMU (passed)

Read 2026-10-05 on development host A, QEMU 11.1.0 TCG, a Windows 98 SE +
NUSB 3.3 guest on the `qemu` flavour (`xhci98.sys` SHA-256
`19291E9256912FA8FA905F207E8762216B0F08B2FDF3EE777BE00C1539C9E3C5`, built
at `bfe8f97`), overlay `vm\t33\w98n-t344.qcow2`, log
`out\phase33\legs\w98n\t344\debugcon-2.log`; the driver installed through
the Add New Hardware Wizard on the clean guest, then a cold boot:

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

## 34.3-H1 - the gate closed on the P14s Gen 1

Same session as 34.2-H.

| # | Clause | Expected | Read |
|---|---|---|---|
| 1 | The `XHCISNAP` report | `psw.gate=00000000`; no other `psw.` record | **Pass** (2026-10-05, the same `P14S.TXT`): `hc.pci=02ED8086`, `psw.gate=00000000` before the start's gates, no other `psw.` record; the controller started (`imod.readback`, `door.interface=00000001`) with all 18 ports managed |
| 2 | Every external connector, a USB 2.0 and a SuperSpeed device on each | Works as under `2.1.0.0` | **Pass** (owner, 2026-10-05) |

## 34.3-H2 - the switchover on the Lenovo B490

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
| 0 | The machine has the mux | Device Manager under any modern OS, or `XHCIQUAL`, shows `8086:1E31` (the HM77 variant); a B490 with the HM70 has no USB 3.0 and cannot read this leg. Note which connectors are blue | Owner, 2026-10-05: its USB connectors carry the SuperSpeed logo; **Pass**: `XHCIQUAL` shows `8086:1E31` |
| 1 | Firmware's routing before the driver, with the USB 3.0 setting on Auto | `XHCIQUAL`'s report of `XUSB2PR`, `XUSB2PRM`, `USB3_PSSEN` and `USB3PRM`; predicted on Auto (`xhci-programming.md`): `XUSB2PR` and `USB3_PSSEN` 0, the masks nonzero | **Pass** (`B490A.LOG`): `XUSB2PR` and `USB3_PSSEN` 0, the masks nonzero, as predicted |
| 2 | The `XHCISNAP` report after the driver starts | `psw.gate=00000001`, `psw.value=00000001`, `psw.route.step=00000000`, and `psw.usb3pssen` equal to `psw.usb3prm`, `psw.xusb2pr` equal to `psw.xusb2prm` | **Pass** (`C:\B490B.TXT`): `psw.gate`, `psw.value` 1, `psw.route.step` 0, each read-back equal to its mask |
| 3 | A SuperSpeed stick on a blue (switchable) connector | Under the xHCI controller, at SuperSpeed in the `XHCISNAP` report; a file round-trips | **Pass**: the owner's MSSU, whose own display reported a 5 Gbit/s link; a file round-tripped |
| 4 | A USB 2.0 device on a switchable connector | Under the xHCI controller, working | **Pass**: under the xHCI controller, in the slot table, working |
| 5 | A connector outside the masks, if the machine has one | Stays on EHCI | **Pass**: the B490 has one; a device there stays with NUSB's EHCI driver |
| 6 | Device Manager: disable the xHCI controller, then enable it | Disabled: the switchable connectors work again under EHCI if an EHCI driver is loaded. Enabled: back under xHCI | **Pass**: disabled, the device on a blue connector worked under NUSB's EHCI driver; enabled, back under `xhci98.sys` |
| 7 | `XhciIntelPortSwitch` set to 0, then a restart | `psw.value=00000000`, no route record; the connectors stay where firmware put them | **Pass** (`C:\B490C.TXT`): `psw.value` 0, no route record; the device on a blue connector stayed with EHCI |
| 8 | The value back at 1, then a restart (the value is read only at start, so without it the driver would not hold the routing and the shutdown would release nothing), the report showing a route record with `psw.route.step=00000000`; then shut down | The machine stays off. On the next boot, `XHCIQUAL` (before Windows) shows the routing back at firmware's values | **Pass**: `C:\B490D.TXT` showed the route again (step 0, read-backs equal to masks); the machine stayed off after Shut Down; `B490E.LOG` on the next boot showed `XUSB2PR` and `USB3_PSSEN` at 0 |
| 9 | Standby and resume, if the OS offers it | The connectors work after the resume; a second route record in the report | **Not read**: Windows 98 SE offers no Standby on this machine |
| 10 | A device attached under EHCI when the driver starts (with an EHCI driver loaded) | It disconnects from EHCI and reappears under xHCI. Not a storage device with writes in flight | **Pass**: a USB 2.0 mouse on a blue connector at boot worked under `xhci98.sys` once Windows was up |

Read 2026-10-05 by the owner on the B490 under Windows 98 SE with NUSB
(its EHCI driver loaded), the release package at `f393bb6` (`xhci98.sys`
`24F2A49B...96C4`) installed through the Add New Hardware Wizard; the table
above is the **Auto** run, `XHCISNAP` at verbosity 2. The results are the
owner's report, clause by clause; the `.LOG` and `.TXT` files named are on
the B490 and were not transcribed here, so the four register values
themselves are not recorded.

**Smart Auto**, the same day, clauses 1, 2, 3 and 8 again:

| Clause | Read |
|---|---|
| 1 | **Pass** (`B490F.LOG`): `XUSB2PR` and `USB3_PSSEN` 0 before Windows. The previous shutdown's hand-back had left them at 0, so this does not separate Smart Auto's restore from Auto's default |
| 2, 3 | **Pass** (`C:\B490G.TXT`): routed, step 0, read-backs equal to masks; the SuperSpeed stick at 5 Gbit/s |
| 8 | **Pass** (`B490H.LOG`): the machine stayed off after Shut Down, and `XUSB2PR` and `USB3_PSSEN` were 0 on the next boot |

Enabled, the control, was not needed: Auto showed no fault.

## Codex

- 34.2: round 5 found no defect in the code; its three P3 (the run sheet
  and the checkpoint) were taken in `8820722`.
- 34.3, on `0def9a3`: round 1 one P2, taken in `ea707a6` (an unread
  subsystem id skips the switchover rather than lapsing the Sony
  exemption); round 2 clean; rounds 3 and 4, on the 8D31 change and design
  record 16, wording only (nine P3, then one), taken in `dfc2b33` and
  `e591717`.
- 34.4, on the docs (`874085b`, `8a8514d`, `dc071b3`): round 1 no P1, seven
  P2 and one P3, taken in `c2e648b` and `5729ada`; round 2, on the
  `2.1.1.0` release documents too, four P2 and two P3, taken in `6241eb5`;
  round 3, two P2 and three P3, taken in `e36a05b`; round 4 clean.
