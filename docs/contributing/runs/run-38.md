# Phase 38 Record

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 38". The
roadmap entry carries the goal, the status, the task list and the
checkpoint; this file carries what each task did and what each reading
said. Where the two disagree about a clause, the roadmap wins.

**On `out\...` and `vm\...` paths in this file.** They say where a reading
was kept on the host that took it; they are not files a clone has.

## 38.5 to 38.7 - The documents and batch files the `2.3.1.0` legs found wanting

**Verdict: done** (2026-10-13). Documents and the two batch files only; no
driver change.

**The vehicle.** Development host A, QEMU 11.1.0, four guests at once, each
on its own overlay of the read-only bases the `2.3.1.0` legs used. Kit
`out\p38-d\kit` (the `2.3.1.0` legs' kit, retargeted); readings, shots and
serial captures in `out\p38-d\legs\<guest>\` (`notes.txt` is each guest's
full list). Packages: the `2.3.1.0` asset (zip SHA-256
`F835E1DC886D6AFDB84B25E7BD89C229AE56CC0B849BBA1E232C2DFE3B707AC6`,
`release-x86`) and the published `2.3.0.0` (`releases\2.3.0.0\release-x86`);
for 38.7 the asset's `portsw\` with `portsw-on.bat` and `portsw-off.bat`
replaced by the working tree's.

### 38.7 - "RESTART WINDOWS" printed once

`portsw-on.bat` and `portsw-off.bat` no longer echo their own "RESTART
WINDOWS for the change to take effect." after a successful set;
`portsw.vbs` already prints "RESTART WINDOWS for this to take effect: the
driver reads the value only when its controller starts.", and is the one
that says it when run alone. The package self-tests now refuse a batch file
that echoes it (`test-package.ps1`, with a control that inserts the old
line); the script's own needle stays.

| Guest | Run | Result |
|---|---|---|
| 98 SE (COMMAND.COM, WSH present) | `PORTSW-ON.BAT`, `PORTSW-OFF.BAT` from `D:\PORTSW`, on screen and through `COMMAND /C ... > file` | No error; "RESTART WINDOWS" once in each; `XhciIntelPortSwitch` 1 after on, 0 after off (regedit export of `Class\USB\0002`) |
| XP SP3 (cmd.exe) | Both from a prompt, redirected to a file, by double-click from Explorer, and through `cmd /k` | No error; "RESTART WINDOWS" once in each; the value 1, then 0 (`reg query`) |

Two cosmetic notes, left as they are. A successful run now ends with two
blank lines before "Press any key": the script's last line is blank and
the batch's `echo.` adds the second, where the removed echo used to sit
between them. And on 98 SE `cscript` adds a blank line after every run, at
the prompt with no batch file too. `PORTSW-ON.BAT > C:\ON.TXT` at a
COMMAND.COM prompt does not redirect a batch file's output; `COMMAND /C`
does.

### 38.6 - 32-bit Vista: the restart after a reinstall, and on an update

The `2.3.1.0` legs saw 32-bit Vista bring the controller back at Code 1
after a remove and a rescan with no restart prompt, only an "installed
successfully" balloon (run-37, "37.final"). The readme template and the
release notes now say that a reinstall on 32-bit Vista and 7 needs the
restart whether or not Windows asks (the install steps, "Windows Vista and
7: message-signalled interrupts", and the known limitations).

The question left was whether the prompt also goes missing on an update
over an installed `xhcimsi.sys`. Read on 32-bit Vista SP2 and, as the
control, 32-bit Windows 7 SP1: `2.3.0.0` installed through Device Manager
and restarted, then `2.3.1.0` through Device Manager.

| Step | Vista SP2 x86 | 7 SP1 x86 |
|---|---|---|
| Install `2.3.0.0` (Browse my computer, the drive root) | Restart asked: the wizard's "The hardware you installed will not work until you restart your computer", then "System Settings Change", "You must restart your computer for these changes to take effect"; Code 1 until it | The same two prompts |
| Update by Browse my computer, the folder typed in | Nothing installed: "The best driver software for your device is already installed"; no prompt | The same, three times |
| Update by Let me pick from a list, Have Disk | Restart asked, the same two prompts; before it "This device is working properly. You need to restart your computer before the changes you made to this device will take effect", no problem code, `2.3.0.0` and its helper still running | The same |
| After the restart | `2.3.1.0` and helper version `2.3.1.0` running, no warning mark | The same |
| `2.3.1.0` over itself (Vista only) | Browse: nothing installed; Have Disk: the same prompts, no publisher warning | Not read |

So the prompt is missing on the remove-and-rescan path only, not on an
update. The release notes' "Updating from 2.y.y.y" and the readme
template's update section now give Have Disk for Vista and 7 and say what
the update showed before its restart.

**Why Browse keeps the old driver**, read on 7 with verbose `setupapi`
logging: the copy in the driver store ranked `0x80ff2005` and the INF on
the drive `0xf0ff2005`, both unsigned, and the lower rank wins before date
or version are compared. Vista's log at its default level shows only the
wizard ending with nothing installed. The release notes already said so
of the `1.2.0.0` upgrade; the `2.y.y.y` section did not.

### 38.5 - The UAS driver's install on Windows 7 and Vista

From the `2.3.1.0` legs (run-37, "37.final"); no new reading. The readme
template, the release notes' install steps and the acceptance test's 5.7
now say that Windows 7 shows no wizard for a UAS device (it searches
Windows Update, reports the device was not installed, and the driver goes
in through Device Manager, Update Driver Software, Browse my computer), and
that Vista's wizard names the device by its own product string, "xHCI98 USB
Attached SCSI Storage" being its name once installed.

**A harness note for later legs.** A fresh `out\p38-d\legs` lacked
`dv\DBGVIEW.EXE`, `TONE.WAV` and `stick\RAND.BIN`, which `stage.ps1`
copies; they were copied from `out\p37-fin\legs`. `swapto.ps1` adds the
Windows 7 x64 bins stick and does not change the transfer drive's
flavour; a clean shutdown and a relaunch with the other `-Flav` does.

## 38.10 - The release's tools in `tools\`

**Verdict: done** (2026-10-13). `make-release.ps1` publishes `xhciqual\`,
`xhcisnap\` and `portsw\` under `<version>\tools\`, so the version root
holds only the flavour directories, `readme.txt` and `LICENSE`. The upload
assembly reads the layout off the tree: a tree with `tools\` is held to
this layout and to the cut's port switchover layout at any version, and a
tree without it is accepted only before `2.4.0.0`, each in the layout its
version was published with (`2.3.1.0` `portsw\` at the root, `2.3.0.0` the
scripts in each flavour directory, older none). The readme template, the
tools' readmes ("two directories up"), `make-package.ps1`'s help, the two
batch files' not-found message (`TOOLS\PORTSW`) and the documents follow.
Commits `fc58af43` to `2af5ec79` and the case fix after the legs.

**Gates.** `build-driver.cmd all` and `all -amd64`, run from PowerShell:
build and gates passed. `test-package.ps1`: 607 checks passed, among them
executed checks of `Get-ToolDirName`, `Assert-ToolsLayout` and the upload
assembly's `$pswVersion` rule, each shown to fail with the rule reverted.

**Review.** Codex thread `01a13334`: round 1 four findings (a `tools\` tree
still admitting the legacy port switchover layouts; two old paths in
generated text; and two on 38.11's licence conditions, all notices and the
source offer); round 2 one P3 (the three earlier layouts named exactly);
round 3 one P2 (a pin that also matched the validator); round 4 clean.

**The vehicle.** Development host A, QEMU, at most 3 of the host's 8
logical processors in use. Kit `out\p38-tools\kit` (`out\p38-d\kit`
retargeted, vCPU cap 8, a `cut` flavour staging the cut's `tools\` as
`TOOLS\`); notes `out\p38-tools\notes.txt`, shots under
`out\p38-tools\legs\<dos|w98|xp>\shots\`. A local x86 dry cut into
`out\p38-tools\releases` (version `2.3.1.0`, the branch not yet stamped),
with both tools rebuilt from tracked sources in a scratch copy
(`out\p38-tools\build\`): the freshness check refused the repository's own
`XHCIQUAL.EXE` and `XHCISNAP.EXE`, byte-identical to `2.3.1.0`'s, because a
checkout had given `src\xhci_version.h` and `xhcisnap.c` a newer file time.
The cut at 38.final meets the same refusal after any fresh checkout.

| Leg | Result |
|---|---|
| The package tree | Root: `debug-x86\`, `release-x86\`, `LICENSE`, `readme.txt`, `tools\`; `tools\xhciqual\`, `tools\xhcisnap\`, `tools\portsw\` complete; the zip the same; the readme names `TOOLS\XHCIQUAL\`, `TOOLS\XHCISNAP` and `TOOLS\PORTSW` (one lower-case path in the contents list, fixed after) |
| `XHCIQUAL` under MS-DOS | Windows 98 SE boot floppy (MS-DOS 7.1), `C:\TOOLS\XHCIQUAL\XHCIQUAL`: quick scan, QEMU's `1B36:000D` "LOOKS QUALIFIED", probe safety PASS |
| Windows 98 SE | From `TOOLS\XHCISNAP`: `XHCISNAP` and `-probe` (driver declined, the route works); from `TOOLS\PORTSW`: `portsw-on.bat` set 1 and `portsw-off.bat` 0, "RESTART WINDOWS" once each; run from another directory, the not-found message names `TOOLS\PORTSW` |
| Windows XP SP3 | The cut's `release-x86` installed by Have Disk; from `TOOLS\XHCISNAP` the same two screens; from `TOOLS\PORTSW` on 1, off 0, `XhciIntelPortSwitch` read back 0. On NT the batch files change to their own directory first, so the not-found message is 9x only |

## 38.8 - The USB device interface off external hubs

**Verdict: done** (2026-10-13). The reading is taken, the change is made
(`hcd_door.c`, `HcdDoorDeviceStart` refuses a PDO with `Hub` set; design
record 13 section 8.10 corrected; the release notes' entry), and the legs on
`xhci98` read on 2000, XP, XP x64 and 7 x64. Commits `c4c2bab4` to
`b113f3b4` and this record.

**The reading** (the checkpoint's: 2000, XP and 7 before the change). Stock
Microsoft stack, no `xhci98` bound. Development host A, QEMU 11.1.0, three
guests at once (5 of the 8 logical processors), kit `out\p38-tools\kit`, tag
`i38`, each on a fresh overlay: `w2kc` (Windows 2000 SP4 that never had
`xhci98`), `xp` (XP SP3) and `w7x86` (7 SP1 x86). Each with
`piix3-usb-uhci`, QEMU's hub (`0409:55AA`, "NEC USB Hub", `usbhub`) on
root port 1 with a keyboard behind it (port 1.1), and a mouse on root port 2,
all set at boot; the launch's `qemu-xhci` stayed driverless (its wizard
cancelled). Every driver came from the inbox set, no prompt for files.
Read with `regedit /e` (2000) or `reg query /s` (XP, 7) of the two
`DeviceClasses` keys and `Enum\USB`, sent over COM1, the byte counts
checked against the guest's own; raw files
`out\p38-tools\legs\<guest>\i38-d1` (`{A5DCBF10-...}`), `-d2`
(`{F18A0E88-...}`), `-d3` (`Enum\USB`).

| Guest | `GUID_DEVINTERFACE_USB_DEVICE` | `GUID_DEVINTERFACE_USB_HUB` |
|---|---|---|
| Windows 2000 SP4 | the keyboard (`...-1.1`), the mouse (`...-2`) | the root hub, the external hub (`...-1`) |
| Windows XP SP3 | the keyboard, the mouse | the root hub, the external hub |
| Windows 7 SP1 x86 | the keyboard, the mouse (linked; two stale entries of the base image's, neither linked nor enumerated) | the root hub, the external hub (linked; stale root hubs and hubs of the base image's, unlinked) |

`GUID_DEVINTERFACE_USB_HOST_CONTROLLER` (`{3ABF6F2D-...}`, the third of
SweetLow's list, added to the reading by the owner the same day), read on
the same overlays at a second boot (`i38-d5`): on XP and 7 on the UHCI's PCI
devnode alone, linked (7 also lists eight stale ICH7 controllers of the base
image's history, unlinked); on Windows 2000 **the key does not exist** - its
whole `DeviceClasses` exported (`i38-dc`, 20 interface classes) holds no
`{3ABF6F2D-...}` - so 2000's stock stack registers no host controller
interface at all. The qemu-xhci, driverless, carries none on any. `xhci98`
registers it on the controller on every target, 2000 included; kept
(owner, 2026-10-13: nothing on 2000 looks for it), recorded in record 13
section 8.10.

No devnode carries both. Windows 11 on development host A (`usbhub3`), read
the same way, agrees: its four external hubs and its root hub carry the hub
interface alone. On XP and 7 the hub's and the keyboard's `Enum` `Control`
keys had no `ActiveService` value while both worked; not followed, nothing in
38.8 depends on it.

**The change.** The hub FDO already registers the hub interface on the hub
PDO (`HcdDoorHubStart`, task 33.4); the device interface now stays off a
hub's PDO, a hub refused as too deep included (it has a hub object, so
`Hub` is set). A hub devnode `2.3.0.0` or `2.3.1.0` registered keeps its
`DeviceClasses` entry, never enabled again. No host test: the rule is one
condition inside a kernel routine on a flag set at the PDO's creation.
Both builds and gates pass (`build-driver.cmd all`, `all -amd64`).

**Review.** Codex thread `01a133b3`, round 1 on `c4c2bab4` (the hub flag on
every hub PDO before its start, the teardown of a never-registered link,
readers of the old rule across the tree, the documents against this
record): clean.

**The legs.** QEMU's hub is a Full Speed hub and QEMU has no USB 3 hub;
the owner amended the legs to it the same day, a USB 2.0 hub and both
halves of a USB 3 hub taking the same path (the `Hub` flag set at every hub
PDO's creation), reviewed statically. Development host A, four guests at
once (7 of the 8 logical processors), kit flavour `p388` (`make-package.ps1
-Flavor release` of `c4c2bab4` into `out\p38-8\pkg-release-<arch>`), tag
`l38`, each on a fresh overlay. `w2k` (the 2000 gold, `1.99.0.0` installed
and disabled) updated by Have Disk; `xp`, `xp64` and `w7x64` (7 x64 with
F8's signature enforcement off) installed through the wizard or Have Disk;
the installed `xhci98.sys` compared byte for byte (`fc /b`) with the
staged one, no difference. The mouse on root port 1, then QEMU's hub on port
4 and the keyboard behind it (4.1), hot-added in that order (or the mouse
at boot), no QEMU wedge; the keyboard behind the hub typed every command.
The three `DeviceClasses` keys, `Enum\XHCI98` and `Enum\USB` read as for
the reading, byte counts checked; raw files
`out\p38-tools\legs\<guest>\l38-e1` to `-e5`.

| Guest | `..._USB_DEVICE` (linked) | `..._USB_HUB` (linked) | `..._USB_HOST_CONTROLLER` (linked) | Device Manager, usbview |
|---|---|---|---|---|
| Windows 2000 SP4 | the keyboard (`...-4.1`), the mouse (`...-1`) | `XHCI98\ROOT_HUB`, `XHCI98\HUB&VID_0409&PID_55AA` | the controller (`1B36:000D`) | by connection: controller, root hub, the mouse, "xHCI98 USB Hub" with the keyboard; no yellow mark. `UV32.EXE` (WDK 7.1's `UVMS.EXE` does not run on 2000): 3 devices, 1 hub, the keyboard on the hub's port 1 |
| Windows XP SP3 | the keyboard, the mouse | the root hub, the external hub | the controller | the same tree, no yellow mark under USB (a VGA controller of the base image's is marked); `UVMS.EXE`: "External Hub: XHCI98HUB2", the keyboard on its port 1 |
| Windows XP x64 | the keyboard, the mouse | the root hub, the external hub | the controller | the same tree, no yellow mark; `UV64.EXE` as on XP |
| Windows 7 SP1 x64 | the keyboard, the mouse (two stale entries of the base image's, unlinked) | the root hub, the external hub (stale root hubs and hubs, unlinked) | the controller (eight stale ICH7 controllers, unlinked) | the same tree, no yellow mark; `UV64.EXE` as on XP |

No hub carries the device interface on any; every live entry is linked
once. On XP, XP x64 and 7 x64 the keyboard's `Enum` `Control` key again lacked
`ActiveService` while it worked, as on the stock stack.