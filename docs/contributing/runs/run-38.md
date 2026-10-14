# Phase 38 Record

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 38". The
roadmap entry carries the goal, the status, the task list and the
checkpoint; this file carries what each task did and what each reading
said. Where the two disagree about a clause, the roadmap wins. The
roadmap's full task text before it was condensed is at the end, "Moved
from the roadmap before the close".

**On task numbers.** They follow the roadmap's numbering after the
renumbering of 2026-10-13 (the decisions table's Phase 38 row); commits
of that day before it use the old numbers. The 38.12 of 2026-10-13 is
the refused interface's device id, "Planned work" NOX.1 since that day;
from 2026-10-14 38.12 is the composite devnode's own select.

**On `out\...` and `vm\...` paths in this file.** They say where a reading
was kept on the host that took it; they are not files a clone has.

## 38.4 to 38.6 - The documents and batch files the `2.3.1.0` legs found wanting

**Verdict: done** (2026-10-13). Documents and the two batch files only; no
driver change.

**The vehicle.** Development host A, QEMU 11.1.0, four guests at once, each
on its own overlay of the read-only bases the `2.3.1.0` legs used. Kit
`out\p38-d\kit` (the `2.3.1.0` legs' kit, retargeted); readings, shots and
serial captures in `out\p38-d\legs\<guest>\` (`notes.txt` is each guest's
full list). Packages: the `2.3.1.0` asset (zip SHA-256
`F835E1DC886D6AFDB84B25E7BD89C229AE56CC0B849BBA1E232C2DFE3B707AC6`,
`release-x86`) and the published `2.3.0.0` (`releases\2.3.0.0\release-x86`);
for 38.6 the asset's `portsw\` with `portsw-on.bat` and `portsw-off.bat`
replaced by the working tree's.

### 38.6 - "RESTART WINDOWS" printed once

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

### 38.5 - 32-bit Vista: the restart after a reinstall, and on an update

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

### 38.4 - The UAS driver's install on Windows 7 and Vista

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

## 38.9 - The release's tools in `tools\`

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
generated text; and two on 38.10's licence conditions, all notices and the
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

## 38.7 - The USB device interface off external hubs

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
38.7 depends on it.

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

## 38.1 - The field reports' batch files

**Verdict: done** (2026-10-13). `XHCISNAP -report` writes a device report
(`BASE.DEV`: each xHCI controller's Device Manager status, problem code and
allocated resources through `cfgmgr32.dll` loaded at run time, and the
installed files' sizes, versions and SHA-256) and then the dump, under an
8.3 name built from the clock (`mDDhhmmS`, a letter for the first free
slot) or one the user gives, never over an earlier run's files.
`SNAPON.BAT [LEVEL]`, `SNAPGET.BAT [NAME]` and `SNAPOFF.BAT` in
`tools\xhcisnap\`, one file each for COMMAND.COM and cmd.exe, pass `%1`
straight to `XHCISNAP -on %1` or `-report %1` and end on the tool's own
last line, which names the files to attach (owner, 2026-10-13).
`XHCIQUAL --log-stamp` names its log from the DOS clock the same way, and
`QUALREP.BAT` in `tools\xhciqual\` runs the probe-only reading with it.
Gated by `Get-ReportBatchFaults` (`package-common.ps1`) on the sources, the
staged release and the upload set, and at the cut byte for byte against the
repository. Commits `bbc4aafd` to `512546e5`.

**Gates.** `xhcisnap\build.cmd` and `selftest.cmd` (32 cases, the batches
run through cmd.exe among them); `xhciqual\build.cmd` (host tests);
`xhciqual\test\run-win98-batch.ps1`, 12 of 12 under MS-DOS 7.1 in QEMU,
`qualrep_abort_rerun` among them; `test-package.ps1`, 1182 checks.

**Review.** Codex: tools round 1 six P2, round 2 one P2, packaging round 1
one P2, round 3 two P2 (a quoted `%1` breaking cmd.exe's `if`, and a
dispatch bypass: the batches now test nothing on `%1`), round 4 one P2
(`f66411ea`) and two P3 (`0b7fcbe9`), round 5 (`task-mv6kaiqc-9o61ed`)
clean with one P3 (registry paths wrapped at every backslash that keeps
79 columns, `4c5a6500`). The two leg fixes below: round 1
(`task-mv6nkdce-u8gsu5`) two P2, fixed in `512546e5` (`dev_prop`'s walk of
every string limited to the two id lists; `XHCIQUAL` flushes stdout and
stderr before it unchains INT 24h), round 2 (`task-mv6o0n1y-q4t1rk`) clean
on the code.

**The vehicle.** Development host A, QEMU, kits copied from
`out\p38-tools\kit` per run (`out\p38-1\legs-a` to `legs-g`, notes in
each). Driver flavour `p388` (`out\p38-8\pkg-release-<arch>`). Legs a and b
(tools of `27dbc0b1`) found the defects fixed in `f8dc5690` and
`b24b3dbe`: the resources never decoded (NT's count-0 descriptor, 9x's
68-byte layout), `XHCIQUAL`'s stamped log 0 bytes (DOS/32A's 5Bh returns
no handle in protected mode; made in real mode), QUALREP's Abort path, the
attach advice after a failed dump. Legs c (`b24b3dbe`) read XP and 7 x64,
legs d (`0b7fcbe9`) the other six targets, and three further runs read the
fixes their defects needed.

| Leg | Result |
|---|---|
| MS-DOS 7.1 (legs d, e) | QUALREP twice: "Log written: A131900A.LOG", "...B.LOG", 2,463 bytes each, read back over COM1. Read-only `B:`, Fail: "cannot write here", "No log was written". **DOS-1**: after an Abort the next `XHCIQUAL` in the same boot hung (CS16, IF clear, HLT): DOS's Abort exits inside DOS without the INT 21h AH=4Ch that DOS/32A's teardown waits for (MS-DOS 4.0 `CTRLC.ASM`; DOS/32A 9.1.2 `exit.asm`), leaving INT 21h hooked into the freed extender (vector read live). Fixed in `f2260d10`: a real-mode INT 24h stub returns Abort as Fail where Fail is allowed; on the fix, Abort then a second run in the same boot completes, Retry and the writable run unchanged. `512546e5`'s flush is harness-read only |
| Windows 98 SE, ME (legs d, f) | SNAPON level 2, restart, SNAPGET: "memory FEBF0000-FEBF3FFF (16 KB)", "IRQ 11, shared, level", last line "Attach: A131906A.DEV A131906A.TXT (...)"; SNAPON 3, SNAPON 0 refused pointing at SNAPOFF.BAT; SNAPGET MYNAME twice, the second refused, nothing overwritten; "A&B", `-c`, `-o` refused, "Nothing to attach: nothing was written."; SNAPOFF 2 to 0. **9X-1**: a disabled controller's dump kept but not flagged, the controller not read as a PCI xHCI one (its class-code id not the first string `cfgmgr32` returned). Fixed in `ffe2c23d`; on the fix both read "device 1: a PCI xHCI controller, driven by xhci98" and, disabled, "*** dump taken from a controller Device Manager reports as disabled (Code 22)" |
| Windows 2000 (legs d, g) | as above; "IRQ 11", "private device private data, 36 bytes"; on the final tool the device section byte-identical to legs d's |
| Windows XP SP3 (legs c, g) | "IRQ 10, shared, level"; the class key split after `Class\` within 79 columns; on the final tool byte-identical to legs c's |
| Windows XP x64 (legs d) | the x86 tool under WOW64: resources decoded, `xhci98.sys`'s SHA-256 the amd64 package's |
| Windows 7 SP1 x86 (legs d) | elevated as above, "IRQ 4294967294, which is -2 as a signed number: a message-signalled interrupt (MSI)"; not elevated, "WRITE FAILED" and the Run-as-administrator advice. The instance-key line was 87 columns on that build (fixed in `4c5a6500`) |
| Windows 7 SP1 x64 (legs c, g) | as 7 x86; on the final tool, not elevated, the instance key wraps at its backslashes, the longest line 77 columns |

The final legs ran the `XHCISNAP` of `ffe2c23d` (SHA-256 `54FD6683...F66406`);
`512546e5` narrows `ffe2c23d`'s walk back to the first string for every
property but the two id lists, so it changes nothing those legs read.

## 38.2 - The Windows 98 SE and ME silent install

**Verdict: done** (2026-10-13). A device PDO answers `SilentInstall` TRUE
on 98 SE and ME when `XhciSilentInstall` is 1 under the controller's key
(off by default; the INFs write nothing) and an interface of its
configuration matches an entry of `Services\USB\SilentClasses`, read once
per controller start into the bus's own table (design record 13 section
6.11, approved `869db5ff`). `tools\silent\` carries the six-entry
`SILENT.REG`, `SILENT.VBS` and `silent-on.bat` / `silent-off.bat`; the
packager publishes them there alone and holds their content to the
checkers, and the cut to `src\` byte for byte. Commits `c248e3dc` to
`0338f690`.

**Changed by the owner, 2026-10-14** (`7131bd5b`): both INFs now write
`XhciSilentInstall` 0 on every install path, under the same
don't-overwrite rule as the other values (the INF gate's `VAL-*` rules,
thirty-six values); "the INFs write nothing" above describes the task as
first built. The `SilentClasses` list is still written by
`silent-on.bat` alone, never by the INFs. The fresh-install 0 was read
on all nine targets from the signed asset (38.final, S1 and S2), and
98 SE's update keeping a 1 (S3).

**Gates.** `test\test_silent.c` (1201 checks) with the host suites; both
builds and their gates at `d8c8fa63`; `test-package.ps1`, 1182 checks.

**Review.** Codex: the design rounds 1 to 3 (`4c25f131`, `663882ac`,
`7d92f987`), the code clean at round 1, the packaging round 1
(`task-mv6kaj9w-704vbh`) two P2 (content never checked at a cut or upload;
`*.reg` line endings left to the host, both fixed in `4c5a6500`) and round 2
(`task-mv6kod02-4jf7e7`) clean; the legs' documents rounds 1 to 3, each
finding a claim the notes did not carry (`d61d329b`, `e3f9f0b1`,
`0338f690`, the last after a further 98 SE leg), round 4
(`task-mv6pma9g-yj5yvq`) clean.

**O1** (record 13 section 6.11.10): NTKERN's `RtlQueryRegistryValues` with
a NULL `Name` enumerates every value, `silent.entries=00000006` on 98 SE and
on ME (runtime; `legal-provenance.md` section 4). No fallback.

**The legs.** Development host A, QEMU, the `release` package of
`d8c8fa63` (`out\p38-2\pkg-release-x86`), every device on a fresh serial;
notes `out\p38-2\legs\notes.txt`. What was read is record 13 section
6.11.11, in short:

| Guest | Result |
|---|---|
| Windows 98 SE | (0): the gate read off, no list read. (c): six entries; a mouse, a stick, a hub and a keyboard MATCH, no wizard, the mouse and the keyboard asking for the CD (`hidusb.sys`, `kbdhid.vxd`); the hub's devnode a failed install under the base's `1.99.0.0` INF (no `XHCI98\HUB` model before `2.1.0.0`); with the current INF a new hub, a mouse behind it and a new keyboard silent, installed and working (the keyboard typing); UAS barred, its wizard came. (b) and (a): a new mouse brought the wizard. `silent-on.bat` refused a wrong value (exit 4) and named extra values; `silent-off.bat` left the list. `usb-audio` with a hand-added 0x40 entry: parent and function silent and working. The parent's disable and enable re-decided nothing (`HcdDevicePdoRepresentFunctions` not reached). A refused device: barred, the wizard found no driver; its `HardwareID` also carries the VID/PID device id (task NOX.1) |
| Windows ME | (0): a mouse, stick, hub and keyboard installed with no wizard and every answer FALSE; audio brought the wizard. (c): six entries, MATCH, a new mouse silent; with the 0x40 entry the audio device installed with no wizard. ME needs no list for a keyboard, mouse, stick or hub of its own |

Not read: a composite keyboard, mouse or stick (no QEMU device offers one),
on ME the (a), (b), refusal and parent re-enable legs, the case with no CD,
and Windows 2000 (the base at hand had no `xhci98` controller).

## The VM matrix on the `2.4.0.0` dry cuts (2026-10-14)

One matrix on the integrated build read the legs 38.3, 38.8, 38.10,
38.11 and 38.12 name, each guest carrying every task's legs in one
install (Phase 36's condensed matrix), but for the legs each task section
below lists as not run, not makeable, deferred or owed. The task sections
take their verdicts from it.

**The vehicle.** Development host A, QEMU 11.1.0, TCG, the total of the
guests' vCPUs kept within the host's logical processors. Kit
`out\p38-matrix\kit`; plan `out\p38-matrix\plan.md`. Guests, each on its
own overlay under `vm\t38mx` of a read-only base: 98 SE under NUSB 3.3 and
under SweetLow's stack, ME on its stock stack, 2000 SP4 (uniprocessor, and
the ACPI multiprocessor HAL with Driver Verifier), XP SP3, XP x64, Vista
SP2 x86 and x64, 7 SP1 x86 and x64; for the update legs, overlays with
`2.3.1.0` installed (and `2.3.0.0` on 32-bit Vista and 7), kept read-only.
Three builds, every staged file checked against its cut's hashes each
time:

| Build | Commit | What it is |
|---|---|---|
| Dry cut 3 | `86a0f6d3` | The stamped `2.4.0.0`, `DriverVer` `10/14/2026`, with 38.3, 38.8, 38.10 and 38.11 (`out\p38-final\dryrun-x86-3`, `pkg-x64`, `pkg-qemu-*`, `hashes.txt`); the bulk of the matrix |
| Dry cut 4 | `f7a409ad` | The XP F6 fix (below); the F6 re-runs |
| Dry cut 5 | `64d2801f` | 38.12 and `XhciPollFineTimer` off by default (`hashes-5.txt`); 38.12's legs, the timer's default, the composite re-runs |

Evidence: `out\p38-matrix\results.md` (every leg's verdict, the veto
table, the defects and their attributions), `legs\<guest>\notes.txt`,
`reports\`, `fails.md`, `progress.md`.

**What it found, and how each was attributed:**

| Finding | Attribution |
|---|---|
| XP SP3 from the F6 floppy: GUI-mode Setup installs the controller from `OemDir`, the INF's copy of `xhcihlpr.sys` fails (`txtsetup.oem` named no such file), the controller gets the NULL driver, nothing asked | **New in `2.4.0.0`** (38.3's helper on XP). Fixed in `f0c28bc6`..`d967daa2`: `txtsetup.oem` carries `xhcihlpr.sys` on a `dll` line, which text mode copies and does not load, and the gate rule `OEM-COPY` ties the option's files to the INF's NT 5.x `CopyFiles` (the reading of 2000 SP4's and XP SP3's `setupdd.sys` and `setupldr.bin` is `legal-provenance.md` section 4). Review: Codex thread `01a1377a`, clean at round 3. Read on dry cut 4: XP F6 to the desktop, the helper copied in text mode and running at the first boot, the USB keyboard typing in GUI mode |
| Windows 2000 from the F6 floppy: STOP `c000026c` at the first GUI-mode boot (`hidusb.SYS` with a USB keyboard or mouse, `usbstor.SYS` with a stick alone), `0xc0000034` | **Pre-existing**: the same on `2.3.1.0` with both triggers. Text mode lays down the class driver but not `usbd.sys`, `hidclass.sys` or `hidparse.sys`, which it imports. Documented as a known limitation (`1afcd46f`, `8653ad50`). **Fixed** by a Windows 2000 entry in `txtsetup.oem`, `XHCI98W2K`, the user copying those three files (`USBD.SY_`, `HIDCLASS.SY_`, `HIDPARSE.SY_`) from their own Windows 2000 CD onto the floppy (owner, 2026-10-14; the release carries no Microsoft file), with the gate rule `OEM-W2K` and `docs/issues/16-w2k-f6-first-gui-boot-usbd.md`; branch `p38-w2kf6`, merged `b8e48823`. Review: Codex threads `01a1393c` (2 rounds) and `01a1398f` (converged at round 5). Legs on the branch's package (`out\p38-matrix\legs\f6w2k\notes.txt`, runs 81 to 85): 2000 SP4 through the new entry with a keyboard, mouse and audio device behind the hub, and with a stick at a root port, a hub and an audio device and no USB keyboard or mouse, both to the product key page with no STOP; without the files, the documented prompt for them; XP's first entry clean to GUI mode. A slipstreamed variant did not load `xhci98` in text mode and was not pursued. Then both 2000 disks past the key page on the same package (runs 86 to 92): to the desktop and one ordinary restart, the hub disk's every Setup page and desktop action on the USB keyboard and mouse, the stick disk (PS/2 input) with the stick readable; Device Manager clean, no event-log error, `XHCISNAP` `2.4.0.0` on both boots of each; `usbd`, `hidclass`, `hidparse` and `xhci98` at `Start` 3 afterwards (the GUI-mode install rewrote `xhci98`'s 0). One unexplained guest reset during GUI-mode Setup's "Installing Components" on the stick disk, not repeated in a watched re-run or at any later boot |
| Windows 98 SE under SweetLow's stack: exception 0E at `0028:FF0AA6E0` once | **Not reproduced**: `2.4.0.0` 0 of 6 runs (3 with `usb-audio`, 3 without; 90 dialogs), `2.3.1.0` 0 of 3; the first run 1 of 1. Unattributed, no regression pattern |
| Mode 2 with the finer timer held: the guest's clock slows and USB key presses are lost at QEMU's queue | Accepted; the owner shipped `XhciPollFineTimer` off (38.3). Guest clock over host clock, the timer held against off: 7 x64 0.23 to 0.42 (0 to 67 of 199 keys arrived; PS/2, the value 0 and mode 0 199 of 199), 7 x86 0.62 idle and 0.11 with the mouse moving against 1.00, Vista x86 0.50 idle and 0.27 under a copy against 1.000, XP 0.975 and 0.904, 2000 0.967 and 0.994; 98 SE and ME 0.99 to 1.00 (no finer timer there). Off by default read on dry cut 5 on 98 SE and XP |
| ME: Shut Down hangs (black screen, the processor busy) after a controller disable and enable with the devices attached | **Pre-existing**: `2.3.1.0` 2 of 2, dry cut 3 1 of 1, dry cut 5 2 of 2; without the cycle dry cut 5 shut down in 7 s. Among the release notes' known limitations since the documents pass (`8fd25530`) |
| Windows 98 SE: `usb-audio` hot-added, or audio during an update, wedges the desktop | Known (run-36, 36.41 and 36.44), on the published `2.3.1.0` too; 98 SE audio play not makeable (accepted) |
| Windows 2000: the controller stopped with problem 31 until the restart after an update, once | Not reproduced, 0 of 3 |

No other driver failure. The readings that disagree with a document,
not with the driver, are `results.md` section 4's ten points, for the
documents pass at 38.final.

## 38.11 - xHCI 0.96 controllers accepted

**Verdict: done** (2026-10-14). A controller whose HCIVERSION is 0.96 or
above starts while `XhciAccept096` is 1 (the default; `0` restores the 1.0
floor), the floor latched per start; below 1.0 HCCPARAMS2 is read as 0, TD
Size is in Linux's KiB form (the last TRB included), the Setup TRB's
Transfer Type and the isochronous TBC and TLBPC are written 0, and every
isochronous TD takes SIA (D8 taken (b), owner); three counter words, the
refused start's diagnosis flushed to DebugView (D5), the `qemu` flavour's
`XhciQemuHciVersion` (D7), the INF rows and gate, `XHCISNAP`'s and
`XHCIQUAL`'s lines and host tests. Design record 17 section 4.14 (merged
`113cdf2c`, `8f6644ec`), code `5e192d2d`, `14831c6c`, `855276cb`.

**Review.** The design: Codex thread `01a135e2`, converged at round 4. The
code: thread `01a13634`, converged at round 2. `855276cb` (a stale
sentence about D8 in record 17) was not reviewed.

**Owner's answers to the audit** (2026-10-14): Transfer State Preserve on
Reset Endpoint and Suspend on Stop Endpoint stay as on 1.0 (as Linux), to
be recorded in record 17 section 4.14; record 20's `XHCI_NEC_HOST` row
gains a note only (Linux rings the command doorbell after Run and takes
vendor event 48 as a command completion; the driver needs neither). Both
are for the documents pass at 38.final.

**The legs** (the matrix, dry cut 3):

| Leg | Guests | Result |
|---|---|---|
| L1, `XhciAccept096` absent then 1 | 98 SE, 2000, XP, Vista x86, 7 x86 | PASS: starts, devices work, "xHCI 1.00: XhciAccept096 not applicable" |
| L2, `XhciAccept096` 0 | the same | PASS: "floor 1.00", unchanged |
| L3, the controller reporting 0.96 (`XhciQemuHciVersion` 0x96), the gate 1 | 98 SE, 2000 (`qemu` flavour) | PASS: starts, devices work, the 0.96 lines |
| L4, as L3 with the gate 0 | the same | PASS: Code 10; DebugView `hc.versions 00960100`, `hc.accept096 0`, `hc.version.policy 00300100`, `init.step 4`, `init.status 4` |
| L5, 0.96 with `XhciWideMap` 1 and big UAS copies | 2000 | Not makeable (accepted): with `XhciQemuBigRefuse` 1 a 1 MiB read aborts QEMU (`dev-uas.c:895`); with it 0 the 1 MiB requests went as one 256-TRB TD, nothing bad |

**Owed after the cut:** a real 0.96 controller (the release notes'
"Untested ground"). A14 and A21 of the audit are unread on silicon.

## 38.8 - The bus's own ids under `USB\`, and `XHCICLN`

**Verdict: done** (2026-10-14). The root hub, hub and composite ids are
`USB\XHCI98_ROOT_HUB`, `USB\XHCI98_HUB`, `USB\XHCI98_HUB30` and
`USB\XHCI98_COMPOSITE` with their `&VID_`, `&PID_` and `&REV_` forms,
spelled once in `src\xhci_hub.h`; the INFs and both `.oem` files name only
the new ids, the root hub's install copies nothing (D3); gates `HCD-USBRES`,
`HCD-IDSRC` and `OEM-USBID`. The cleanup tool `XHCICLN.EXE` (MSVC 6.0) and
`XHCICL64.EXE` (WDK 7.1 amd64), source `xhcicln\`, shipped in
`tools\xhcicln\`: it lists by default and removes on `-remove`, on NT only.
Design record 13 section 10.13 (merged `4c5567af`, decisions `f85cbc5a`);
code `ffdeebee`..`0009d1ed`, merged `73d6a005` with `60124cff`; record 13
section 10.13.13 is the as-built.

**Review.** The design: Codex thread `01a135b5`, converged at round 5. The
code: thread `01a13644`, round 1 a P1 (`%token%` ids slipping past the
scan) and three P2, round 2 one P3, round 3 clean.

**The legs** (the matrix, dry cut 3; 98 SE's composite part on dry cut 5):

| Leg | Result |
|---|---|
| I1, a fresh install, on 98 SE (NUSB and SweetLow), ME, 2000, XP, XP x64, Vista and 7 x86 and x64 | PASS: the root hub under `USB\XHCI98_ROOT_HUB`, its install copying no file; a hub and a composite device each with one driver node; `XHCICLN` (`XHCICL64`) "nothing to do", exit 0 |
| I2, an update over `2.3.1.0` on the same ten | PASS: devices with a serial number (the sticks, the serial audio device, the hub) keep their devnodes; the serial-less mouse, keyboard and audio device (composite and function) are new, the mouse losing its mark value; one root hub; the old entries non-present (hidden view, Safe Mode) |
| I3, serial-less USB input alone through the update | Recorded: on XP the root hub's wizard cannot be answered with USB input, PS/2 answers all, a keyboard on a second controller does not help (its root hub is new too); on 98 SE USB input works until the update wizard's last page, which needs Finish from other input, and after the restart the serial-less mouse's HID wizard cannot be answered with USB |
| I4, 98 SE, a chain of five serial-less hubs ending in a composite device | Not run (owed) |
| I5, XP with two controllers | Recorded: one update ran `2.4.0.0` on both at once; the second's driver key keeps `DriverVersion` `2.3.1.0` until an uninstall and rescan |
| I6, the tool on 2000, XP (both configurations), XP x64, Vista and 7 x86 and x64 | PASS: the list, `-remove` ("4 removed."), a second run with nothing, the refusals before the restart and with the controller disabled, and on XP after a return to `2.3.1.0`. With two project controllers on XP, one uninstalled and not rescanned, the tool did not refuse, as its readme says (it refuses only when no project controller is running); the checklist's wording disagrees |
| I7, the 32-bit tool under WOW64 | PASS on XP x64, Vista x64 and 7 x64: `XHCICLN.EXE` refuses naming `XHCICL64.EXE`, which removes |
| I8, 98 SE and ME | PASS: lists only, `-remove` before the restart exit 1, the `HKEY_DYN_DATA` walk finding no live `XHCI98\` devnode (runtime). Static, from each guest's `CFGMGR32.DLL` with nothing executed: `CM_Uninstall_DevNode` is a stub on both (returns success, removes nothing), and `CM_Locate_DevNodeA` honours `CM_LOCATE_DEVNODE_PHANTOM` |
| I9, F6 with the input behind the hub | XP PASS on dry cut 4; XP x64 PASS to the product key page; 2000 the pre-existing STOP (above), lifted by the Windows 2000 entry merged later the same day, with which 2000 PASSED to the desktop and one restart (runs 86 to 92) |
| I10, a High Speed and a USB 3 hub | After the cut (owner, 2026-10-13) |

**The controller's veto**, with the tool holding the door (the keyboard
detached, since a present USB keyboard alone makes Windows veto a
disable; a control without the tool in each cell): disable vetoed on 2000,
XP (a stock UHCI beside it, and two project controllers), Vista x86 and
x64, 7 x86 (both configurations) and x64; update vetoed on 2000, XP, Vista
x64 and 7 x64; roll back vetoed on XP. On 32-bit Vista and 7 Windows
defers every update to the restart, and on 32-bit 7 every rollback too,
with or without the tool, so the hold is not needed there; Vista x86's
rollback was not run; 2000 has no Roll Back. XP x64's
stock-controller configuration was not reachable (`xhci98` kept `HCD0`);
it was read the same day, at 18:36, with two project controllers
instead (the owner's decision; `out\p38-matrix\legs\xp64\notes.txt`,
"VETO addendum", overlay `mtx-xp64-2cv`, the unsigned `pkg-x64-5`):
`XHCICL64` at its question holding both, a disable, an update (Have Disk,
`2.4.0.0` over `2.4.0.0`) and a rollback (`2.4.0.0` to `2.3.1.0`) of one
controller each vetoed - the controller not restarted, the change held to
the restart - and each taking effect at once in its control without the
tool. No target read showed a missing veto, so removal stays enabled on
NT 5.0, 5.1, 5.2, 6.0 and 6.1, as built.

**Owed after the cut:** a High Speed and a USB 3 hub (bench or
passthrough; `USB\XHCI98_HUB30` rests on host vectors and the INF gate),
and I4 on 98 SE. Before the cut, under 38.final: XP x64's F6 install
past the product key page.

## 38.3 - A finer system timer while polling, and `xhcihlpr.sys`

**Verdict: done** (2026-10-14). Under a poll delivery on Windows 2000 and
later a timer thread of the controller's asks `ExSetTimerResolution` for
`XhciPollActiveMs` x 10000 while the poller is active and releases it
when the poller leaves that state, when the controller stops or leaves D0,
and at S3, S4 and S5;
`XhciPollFineTimer` turns it on. The x86 image reaches the routine through
the helper, renamed from `xhcimsi.sys` to `xhcihlpr.sys`, its device
`\Device\Xhci98Helper` and channel V2, built with WDK 7.1's `WXP`
environment at subsystem 5.00 and loading on 2000 and XP as well as Vista
and 7; an update deletes `xhcimsi`. Counter words for the timer and the
helper. Design record 15 section 7.2 and record 18 section 15 (merged
`3d77d318`, decisions `b70d4087`); code `317d005e`..`2acf4e10`, merged
`7aa959e4` with `a1ae5c39`; record 15 section 7.2.14 and record 18
section 15.14 are the as-built.

**Off by default** (owner, 2026-10-14, after the matrix's clock reading
above): both INFs write `XhciPollFineTimer` 0 under the don't-overwrite
rule, an absent, unreadable or other-type value is off; record 15 section
7.2.4's amendment, the documents, `XHCISNAP`'s "not set; in effect off (the
default)" for an absent value and "0; in effect off" for a written 0
(`17888a97`, review fixes `6eff1869`, `76327494`, `64d2801f`). Real
hardware is unread, and the default is reconsidered on a reading there.

**Review.** The design: Codex thread `01a135b1`, converged at round 6. The
code: thread `01a13649`, round 1 four P2 and a P3, round 2 a P2 and two
P3, round 3 clean; the merge's delta, thread `01a1368e`, clean. The
default's change with 38.12's merge: thread `01a138ad`, converged at round
4. `a1ae5c39` (one README phrase) was not reviewed.

**The legs** (the matrix, dry cut 3; the default on dry cut 5):

| Leg | Result |
|---|---|
| K0, the kernel image and an ordinary boot | Recorded on every guest |
| N1, the install on 2000, an update on 2000 SMP, XP | PASS: no forced restart; from the next boot the helper runs as a boot driver |
| N2, Driver Verifier on both drivers, 2000 SMP | PASS (`/flags 0x1B` writes level 0 on 2000) |
| N3, the helper's injected faults | PASS: 7 x86 values 1 to 9, XP 7 and 8 |
| M and H, the MSI legs through channel V2 | PASS on 7 x86 and Vista x86 |
| U3, uninstall | PASS on 2000 and 7 x86 |
| U4, an update from `2.3.0.0` and `2.3.1.0` on Vista x86 and 7 x86 | Recorded: `xhcimsi.sys` deleted at once, no `PendingFileRenameOperations` (the checklist expected it queued), no event 7026, the helper bound, MSI |
| U5, `2.3.1.0` over `2.4.0.0` on 7 x86 | PASS |
| U6, `2.4.0.0` over `2.3.1.0` on 2000 and XP, and a boot with the helper disabled | PASS |
| D2, D3, from new media | PASS: no helper on 98 SE, ME or x64; the helper on 2000, XP, Vista and 7 |
| T1, T2, polled transfers and the timer's hold with the value 1 | Not run: off by default, and the owner asked for no more readings with it on; the clock reading above stands for T1 |
| T3, a stop with the timer held | Recorded on 7 x86 (`qemu`): cause 4, `TmrThread` 3, held 0, no bugcheck, the next start from zero; `qemu.stop.dx` is not reachable by a disable (Windows removes the children first) |
| T4, S4 and S5 | PASS on 2000 SMP with Verifier, Vista x86, 7 x86 (with Verifier on hibernate), 7 x64, XP S5, 2000 shutdown; S3 not makeable on any NT base |
| T5, `XhciPollActiveMs` 1 and 4 | Recorded: 2000 1.2 ms and 8.3 ms, 7 x86 1.3 ms and 5.2 ms |
| T6, the helper disabled on NT, and 98 SE and ME | PASS |
| T7, the value 0 | PASS: no thread |
| T8, `XhciPollIdleMs` 50 against 500 | **Deferred by the owner** (2026-10-14), the default staying 50. Partial, value 0: plug-to-working 0.3 to 0.7 s at both values everywhere, remote wake at both on the x86 guests (the test client is x86; XP x64 and Vista x64 not applicable), idle host load halved at 500 on Vista x86 (12.7 to 5.4 %) and about 30 % lower on Vista x64, no difference beyond noise on XP, XP x64 and 2000; not taken on 98 SE, ME and 7 (`results.md` section 5) |
| C5b, the default on dry cut 5 | PASS on 98 SE and XP: the value 0 as installed, `TmrThread` 0 in mode 2 |

**Owed after the cut:** the finer timer on real hardware, and with it the
default; T8 on every target; S3.

## 38.10 - Linux's xHCI controller quirks, and the endpoint rings

**Verdict: done** (2026-10-14). Fourteen registry values (design record 20
sections 4 and 6), each matched by vendor and device id, the revision only
logged: `XhciMsiAvoid`, `XhciStreamsOff`, `XhciResumeReinit`,
`XhciIntelShutdownToEhci`, `XhciShutdownReset`, `XhciSuspendHaltDelay`,
`XhciSuspendHaltTimeout`, `XhciAsmediaFlowControl`, `XhciTiIntervalCap`,
`XhciStuckLinkReset`, `XhciStreamRingPad`, `XhciControlLinkNoOp`,
`XhciIsochSpuriousSuccess`, `XhciAmdSuspendBitClear`, and `1002:7316`
under `XhciIntervalCap`; their INF rows and gate, the counter block's quirk
words, `XHCISNAP`'s and `XHCIQUAL`'s rows, host vectors. Every quirk was
written fresh from Linux's handling and the xHCI specification, no Linux
code copied, so `legal-provenance.md` gained no row; 38.final's check of
the release notices reads record 20 section 13.6. Code `68c756dd`..`72995562`, merged
`e1727c3c` with `b14edbed`.

**The endpoint rings** (record 20 section 5): a ring or a stream block is
returned to the pool only when a command proves the controller has given
it up, `SELECT_INTERFACE` an ownership switch (`src\xhci_own.c`), a
reserved pool; a STALLed `SET_INTERFACE` on an interface with one
alternate setting is a success after `CLEAR_FEATURE(ENDPOINT_HALT)` to
each new non-isochronous endpoint, as Linux does (owner, 2026-10-14;
record 20 revision 7). Code `ac5bf01f`..`2fd452c6`, merged `59879e2d`;
`31f3a929` regenerated the offset tables for the merged counter block (241
words then), not reviewed.

**Review.** The survey: Codex thread `01a135c5`, converged at round 5. The
quirks' code: thread `01a13639`, round 1 four findings (two P2), round 2
three (one P2), round 3 three P3, round 4 clean. The ring fix: thread
`01a13619`, the amendment clean at round 2, the code round 1 three P3, a
confirmation round and two full adversarial rounds clean. `b14edbed` (one
counter-total phrase) was not reviewed.

**The legs** (the matrix, dry cut 3; the ring on dry cut 5 too):

| Leg | Result |
|---|---|
| Q1, every value deleted | PASS on 98 SE, 2000, 7 x86: each at its default, QEMU's `1B36:000D` named by none |
| Q2, the INF's values | PASS on the same and Vista x86 |
| Q3, each value at 2 in turn | PASS: `XhciMsiAvoid` (7 x86, Vista x86: the line under mode 0, `QuirkMsiRefused` 1, mode 3 overridden), `XhciResumeReinit` (ME, Vista x86 S4; XP has no standby on its base), `XhciStreamsOff` (its High Speed half not makeable), `XhciStreamRingPad`, `XhciControlLinkNoOp`, `XhciSuspendHaltDelay`, `XhciSuspendHaltTimeout`, `XhciStuckLinkReset`, `XhciIsochSpuriousSuccess`, `XhciShutdownReset` (its DebugView half not makeable on 2000); `XhciTiIntervalCap` recorded (QEMU's High Speed mouse at Interval 6, the capped half not makeable) |
| Q4, the three vendor-register values at 2 | PASS: "a 2 read as 1", nothing acts |
| Q5, `XHCIQUAL` under MS-DOS | PASS: `1B36:000D` "none known" |
| R1, `usb-audio` started and stopped about 20 times | PASS on ME, XP x64, Vista x64, 7 x86 and x64 (dry cut 3), XP, 2000, 7 x64 (dry cut 5): the pool back after each stop; 98 SE not makeable (accepted) |
| R2 to R6, the injected select faults | PASS on XP and 2000 (`qemu`); R2 on 7 x86 |

**Owed after the cut:** each quirk on a controller it names; none has been
read on real hardware. The owner's decision of 2026-10-14 drops the
"Untested ground" rows for the controller-fault handling on real hardware
and `XhciIntervalCap` on AMD (the documents pass at 38.final).

## 38.12 - The composite devnode's own `SELECT_CONFIGURATION`

**Verdict: done** (2026-10-14). Opened by the owner on 2026-10-14 from
GitHub issue 17 item 11, in `2.4.0.0`. The composite FDO's `START`, after
the `START` has come back up, sends down its own stack - a lower filter
included - `GET_DESCRIPTOR` for the device and the configuration and one
`SELECT_CONFIGURATION`, as `usbccgp` does; the parent PDO answers from its
copies and records the select, configuring nothing; a function's select
takes each endpoint's `bInterval` from its own caller, else from the
record, else the device's. Counters at words 241 to 245, `comp.*` log
records, `XHCISNAP`'s "composite selects" section, host tests. Design
record 13 section 10.14, its decisions D1 to D13 recommended and adopted
under the owner's instruction to proceed after review (2026-10-14); code
`785a5f08`..`151a6eaa`, merged `667ae804`; section 10.14.10 is the
as-built.

**Review.** The design: Codex thread `01a137cb`, converged at round 3. The
code: thread `01a137f4`, converged at round 4. The merge: thread
`01a138ad` (with the timer's default), converged at round 4.

**The legs** (the matrix, dry cut 5):

| Leg | Result |
|---|---|
| C5a, the regression with QEMU's `usb-audio`, no filter, on 98 SE, ME, 2000, XP, XP x64 and 7 x64 | PASS: `CompositeSelects` one per select, every `comp.select` with 0 rewrites and 0 mismatches, the other composite counters 0, no `comp.interval`; `TONE.WAV` whole from both audio devices; the composite and the controller disabled and enabled recover; 98 SE's play not makeable. The serial-less audio device's slot now reads "configured" at once (on `2.3.1.0` and dry cut 3 "addressed" until its first stream) |
| C5c, the composite re-runs of the other tasks' legs | PASS on 98 SE, 2000, XP and 7 x64 |
| hidusbf on the composite devnode and on the mouse function's | Not makeable: QEMU has no composite HID device. Owed to the bench after the cut (owner, 2026-10-14) |

**Owed after the cut:** hidusbf on a composite HID device, on the
composite devnode and on a function's, on 98 SE, 2000, XP and 7 x86.

## 38.final - So far

**Verdict: done.** What was done, all 2026-10-14:

- **The stamp.** `b4b8237c`: `2.4.0.0`, `DriverVer` `10/14/2026`, the four
  INFs and the release notes' header; `2fd6a25a` and `86a0f6d3`, the
  readme template (ME needs no USB 2.0 stack, the helper's row, 98 SE's
  Stand by under the known limitations). Review: Codex thread `01a1372e`,
  converged at round 4 (round 5 and thread `01a137da` on `cd51c188` and
  `f7a409ad`, the Windows 2000 F6 limitation's wording, clean).
- **The integration.** Every task merged on `phase-38` (above), the
  merges' own reviews (threads `01a1368e`, `01a136ab`, `01a136b2`,
  `01a136ea`, `01a13706`) converged.
- **The dry cuts.** Built and gated, x86 and amd64, all three flavours
  each, with the host tests, the tools rebuilt first (a fresh checkout
  leaves their sources newer than the executables, which the cut
  refuses); an x86 dry cut through
  `make-release.ps1`, unsigned x64 and `qemu` packages beside it, every
  file's hash recorded. Dry cut 3 (`86a0f6d3`, zip SHA-256
  `48DE4F12...DC9628`), 4 (`f7a409ad`, `880856F4...8EDE79`), 5
  (`64d2801f`, `F80212CC...2B9405`, `out\p38-final\dryrun-x86-5`,
  `hashes-5.txt`). On dry cut 5 the rehearsal of `verify-cut.ps1` passed
  30 checks; its six failures are the x64 package's absence from an x86
  dry cut (the twelve x64 files, the four drivers each checked present,
  and the readme's section 8 listing `RELEASE-X64\` and `DEBUG-X64\`). The
  owner's cut command is `out\p38-final\cut-2.4.0.0.ps1.txt`; a draft
  history entry is `out\p38-final\releases\history.md`.
- **The VM matrix** (above), the XP F6 fix it called for, and the
  Windows 2000 F6 entry (`b8e48823`, issue 16).
- **Windows 2000 from the F6 floppy to the desktop.** The Windows 2000
  entry's two disks carried past the product key page to the desktop and
  one ordinary restart (runs 86 to 92; the VM matrix's table above and
  issue 16 section 5), on the branch package whose `txtsetup.oem` is the
  merged one.
- **XHCICLN's veto on XP x64**, read with two project controllers: all
  three vetoed, so removal stays enabled on NT 5.2 as built (38.8).
- **The documents pass** of the matrix's readings and the owner's
  decisions of 2026-10-14 (`8fd25530`..`1c409399`, through four review
  rounds), then the two readings above.

- **Dry cuts 6 and 7 of the version branch.** The owner's changes of
  2026-10-14 evening merged first (`7131bd5b`..`714ec712`: both INFs
  write `XhciSilentInstall` 0 on every path, `VAL-*` thirty-six; README
  trims), then `53ddd829` (the readme template's and release notes'
  status sentences for 0.96 and the quirks dropped). Dry cut 7 at
  `53ddd829` from dry cut 6's builds of `714ec712` (`src\` and the tool
  sources unchanged since): zip SHA-256 `2DC37472...CE2238`,
  `hashes-7.txt`; the `verify-cut.ps1` rehearsal 30 PASS, the six
  x64-absence FAILs; both x86 `txtsetup.oem` byte-identical to the
  Windows 2000 legs' floppy.
- **The owner's signed cut** at `53ddd829`: `verify-cut.ps1` 0 FAIL (48
  zip entries, byte-identical to the tree). `xhci98-2.4.0.0.zip`,
  1,234,974 bytes, SHA-256
  `BF5B62DAB3772E83F69054649D5334F7314404DCE673D2601E2BA739CFD08C79`.
  Committed as `releases/2.4.0.0` with the `releases/history.md` entry
  (`7e13fc81`).
- **The install legs from the asset** (`out\p38-matrix\asset-legs.md`;
  readings in each guest's `legs\<g>\notes.txt`, "ASSET 2026-10-14"):
  98 SE (NUSB), ME, 2000, XP, XP x64, Vista x86 and x64, 7 x86 and x64,
  each a new overlay with the zip's files byte for byte (A0). On every
  guest the install (A1), the controller and `USB\XHCI98_ROOT_HUB` with
  no `XHCI98\` devnode (A2), a mouse, a stick (`fc /b` clean) and the
  audio device (A3; the tone played whole but on 98 SE, NOT MAKEABLE as
  before), the versions 2.4.0.0 (A4) and `XHCISNAP` with every tolerance
  counter 0 (A7) PASS; `XhciSilentInstall` 0 with thirty-six `Xhci*`
  values (S1), and on 98 SE and ME `silent.gate.value` 0 (S2). Signing:
  Vista x64 and 7 x64 load the test-signed driver at a cold start with
  no F8 (not Code 39 / 52; A5), XP x64 with no test mode (A5b); the
  Driver tab reads "Not digitally signed", as record 19 expects. Vista
  x64 shows no "Test Mode" watermark: both Vista overlays have fallen
  into Vista's not-genuine state, whose text stands in that corner,
  `bcdedit` reading `testsigning Yes` (not this driver). A6 (disable,
  enable, remove, rescan) not run, owner 2026-10-14, for time: the dry
  cuts' matrix read it on the same build. 98 SE's S3 (`silent-on.bat`,
  an update, the 1 kept) runs after publishing (owner, 2026-10-14).

- **XP x64 from the F6 floppy past the product key page** (owner-supplied
  key, never recorded), the floppy the asset's `release-x64` byte for
  byte, a new disk (`legs\f6xp64\notes.txt`, "F6 PAST KEY
  2026-10-14"): text mode on the USB keyboard behind the hub, no PS/2
  event; GUI mode's five Installing Devices Logo prompts answered with
  PS/2 (the USB keyboard has no HID driver yet there, as in the morning's
  run and as the release notes say); from Regional Settings on, the key
  page included, USB only; the desktop, a restart (the owner's Start-menu
  shutdown, event 6006 and no 6008, then a cold start) and the desktop
  again with USB input; the controller "working properly", no warning
  mark. PASS.
- **98 SE's S3** (after publishing was cleared): `silent-on.bat` set 1
  and merged the six entries; an update from the asset's `release-x86`
  through Have Disk kept the 1 (`silent.gate.value` 1, thirty-six
  values); `silent-off.bat` set 0 and kept the list. PASS. Re-adding the
  audio device with the switch on met the known 98 SE audio-load wedge
  (36.41), so the update ran with the mouse and the stick.

**Verdict: done** (2026-10-14). Left: publishing (the owner's).

## Moved from the roadmap before the close (2026-10-14)

On 2026-10-14, before the phase closed, the owner had Phase 38's roadmap
entry cut to a line or two a task (`roadmap-hcd.md`, "Phase 38"), as the
earlier phases' are. The entry as it stood then follows verbatim, so the
decisions, the reviewed design facts and the evidence pointers inside the
task text stay on record.

**Phase 38 - The Planned Work After `2.3.1.0`: the Field-Report Batch Files, the 98 SE and ME Silent Install, Pinned Containment, the Polling Timer and the Tools Directory**

Goal: the reporters' helper batch files; the Windows 98 SE and ME silent
install; the transfers kept by a pinned containment released on proof; a
finer system timer while polling; the release's tools moved into a
`tools\` directory; Linux's xHCI controller quirks, each behind a registry
gate; and the documents the `2.3.1.0` legs found wanting.

Status: open. Opened 2026-10-13 (owner) with every task then in "Planned
work", numbered in the order it was listed. On branch `phase-38`; the
version is `2.4.0.0` (owner, 2026-10-13). The same day (owner): two tasks joined
from SweetLow's comments on GitHub issue 17 of 2026-10-12 (38.7 and 38.8),
and the tools directory (38.9), and later the Linux quirk population (38.10, first 37.17); the real-hardware bench tasks were
removed, what they would have read staying under "Untested ground" in the
README and the release notes; the UAS class driver's fold into
`xhci98.sys` was removed; and the tasks left were numbered in order. The
decisions table's Phase 38 row maps placeholder ids and old numbers to new. Later the same day (owner): 38.12 was
deferred to "Planned work" (NOX.1) and 38.3 too (PCR.1), the tasks after 38.3
were numbered down by one, and the acceptance of xHCI 0.96 controllers
joined as 38.11. On 2026-10-14 (owner) the composite devnode's own
`SELECT_CONFIGURATION` joined as 38.12, from SweetLow's item 11 of GitHub
issue 17.

- [x] 38.1 (in Phase 37 until 2026-10-12) helper batch files for the field reports (owner, 2026-10-12). Done 2026-10-13 (`bbc4aafd` to `512546e5`; Codex clean at round 5 and on the leg fixes; legs on MS-DOS, 98 SE, ME, 2000, XP, XP x64, 7 x86 and 7 x64, two defects found and fixed, `runs/run-38.md`, "38.1"): one command each that gathers what the field reports (37.2) ask their reporters for, so a reporter runs a batch and attaches one output instead of following a list. On Windows (98 SE and ME under COMMAND.COM, 2000 to 7 under cmd.exe, so each batch runs under both): the controller's Device Manager status and problem code, its assigned resources, the installed xhci98 version and file hashes, and an `XHCISNAP` dump (channel on, restart, reproduce, dump, channel off), with what a stock 98 SE or ME cannot report said in the output; under MS-DOS: an `XHCIQUAL` reading with its log kept. Every output is saved in the current directory, never under `C:\`, and its name carries the date and time so a second run does not overwrite the first (owner, 2026-10-12); a batch cannot build that name (COMMAND.COM on 98 SE and ME has no `%DATE%` or `%TIME%`, cmd.exe's follow the locale), so `XHCISNAP` builds it from the local clock, 8.3-safe, prints the names it wrote, and never overwrites a dump from the same minute, its self-test gaining the rule. Each batch's last line names the files to attach. 8.3 names, ASCII, CRLF, only tools a stock install of each target has; shipped in the release beside the tool each one runs - the Windows batches in `tools\xhcisnap\`, the MS-DOS one in `tools\xhciqual\` (owner, 2026-10-13) - and gated as `portsw-on.bat` and `portsw-off.bat` are (COMMAND.COM-safe, line endings), and named in the readme template and the user documents. Decided at the task's start (owner, 2026-10-13): `XHCISNAP` itself reads the controller's status and problem code and its allocated resources (the configuration manager, loaded at run time) and the SHA-256 of the installed files, so every target reports the same, rather than each batch using what that target's stock tools reach; the Windows side is three batches, `SNAPON.BAT` (channel on), `SNAPGET.BAT` (the report and the dump, the channel left on) and `SNAPOFF.BAT` (channel off), with `SNAPON.BAT` taking an optional verbosity (1 to 4, default 2) and `SNAPGET.BAT` an optional name to save to (an 8.3 base in the current directory, refused rather than overwritten when any of its files exists; without it, the clock-built name) (owner, 2026-10-13); and `XHCIQUAL` builds its log's name from the DOS clock under the same rule, since a batch cannot. Legs: each batch on 98 SE, ME, 2000, XP, XP x64 and 7, the output checked against what the reporters were asked
- [x] 38.2 (in Phase 37 until 2026-10-12) the Windows 98 SE and ME silent install (issue 17 item 4, the one item of that issue Phase 36 did not take; owner, 2026-10-09). Done 2026-10-13 (`c248e3dc` to `0338f690`; record 13 section 6.11, read in 6.11.11; Codex clean on the design, the code, the packaging and the documents; legs on 98 SE and ME: 98 SE silent for a keyboard, mouse, stick and hub and a matching composite with the switch on, ME silent for the four without it, so ME needs no list for them; composite keyboards, mice and sticks not read, no QEMU device offering one; `runs/run-38.md`, "38.2"). **Unblocked 2026-10-12.** The rule: a device PDO reports `SilentInstall` TRUE in `IRP_MN_QUERY_CAPABILITIES` (today FALSE on every PDO: `hcd_pdo.c`, `hcd_rh.c`, `src\uas\uas_pdo.c`) when any interface descriptor of its configuration matches an entry of `HKLM\System\CurrentControlSet\Services\USB\SilentClasses`, the key already in use on these systems, so an existing `.reg` and a machine that merged it work unchanged (owner, 2026-10-12). Each entry is a 4-byte `REG_BINARY`, its name ignored: byte 0 a control byte (0x80 match on a device that is not composite, 0x40 on one that is, 0x01 compare the subclass, 0x02 compare the protocol; an entry with neither 0x80 nor 0x40 is ignored), then class, subclass and protocol. Decisions (owner, 2026-10-12): **Windows 98 SE and ME only**, never on 2000 and later; **opt-in**: the INFs write nothing, and a missing, empty or unreadable list means nothing is silent; the list is read once per controller start into the bus's own storage (no per-device registry read or allocation), descriptors are walked by `bLength`, and composite means what the bus's own composite split decides. Two traps to avoid: an absent list must not be dereferenced at the first interface, and a flag test must mask before it casts to `BOOLEAN`. The package carries six entries (hubs alone; boot keyboard and mouse alone or composite; Bulk-Only SCSI storage alone or composite), as a `.reg` in its own directory under 38.9's `tools\` (owner, 2026-10-13, replacing the `release-x86` placement of 2026-10-12), `tools\silent\`, which `Assert-ToolsLayout`'s list of tool directories then names: never in a flavour directory, never at the version root, with a readme section on when to merge it: once the classes' drivers have been installed at least once, because a silent install on 98 SE asks for no CD (a keyboard needs `kbdhid.vxd` from it, run-36 K5) and a class with no driver ends unworking with nothing said; a class this package's own INFs install (UAS) is not in the list. **Gated by the driver too** (owner, 2026-10-13, after the design draft): a value `XhciSilentInstall` under the controller's driver key, read at each controller start, **off by default**, so the list alone silences nothing; `tools\silent\` carries `silent-on.bat`, which sets the value to 1 on every xhci98 controller and imports the list, and `silent-off.bat`, which sets it to 0 and leaves the list (another stack may share the key), both COMMAND.COM-safe and gated as `portsw-on.bat` is, with the restart they need said. 37.1's stock-ME reading installed the mouse, hub and stick silently with no list (the wizard came only for UAS and composite audio), so whether ME needs the list is read, not assumed. Design text in record 13 first, a review loop, host tests of the match (control bits, composite, subclass and protocol, empty and malformed lists), both builds. Legs on 98 SE and ME, each without the list and then with it merged: a device installed once and moved to another port, a hub, a boot keyboard and mouse, a composite device, a stick; the readme then says whether ME needs the list. The README, the release notes and the readme template credit SweetLow for the information on how the silent install is decided and for the list (owner, 2026-10-13)
- [ ] 38.3 (in Phase 37 until 2026-10-12) a finer system timer while polling, on Windows 2000 and later (owner, 2026-10-12). Under a poll delivery the active period of 1 ms runs at the clock tick, about 10 ms on a uniprocessor HAL and 15.6 ms on an ACPI or multiprocessor one (design record 15 section 7), so polled storage completes about once a tick. `ExSetTimerResolution` raises the tick for the whole system; record 15 section 7 left it as "the next question" (no allowlist row, no Windows 98 export). The design (record 15 section 7.2, reviewed to convergence 2026-10-13): request `XhciPollActiveMs` x 10000 only while the poller is in its active state and release it when the poller leaves that state - the idle period, `XhciPollIdleMs`, the only linger (owner, 2026-10-13) - when the delivery leaves polling, the controller stops or leaves D0, and at S3, S4 and S5; one request per controller at most, every TRUE paired with its FALSE, a boundary generation forcing the release across any power transition, recovery or stop; from a timer thread of the controller's at PASSIVE_LEVEL, created after a successful start and joined at the stop after the children's power IRPs are settled (owner, 2026-10-13), never the poll DPC or the controller thread. A registry value, `XhciPollFineTimer`, turns it off, **on by default** (owner, 2026-10-12): polling runs only when interrupts failed or the user chose it, and the cost lasts only while the poller is active. `XhciPollIdleMs` keeps its default of 50; the legs measure 50 against 500 on every target, 98 SE and ME included (CPU, hot-plug and hub latency, USB 2.0 remote wake, the timer's toggles; Function Wake unread on QEMU), and the default is decided on that reading; an update keeps an existing value (owner, 2026-10-13). How the x86 image reaches the routine: it loads on Windows 98 SE and ME too, so it may import neither `ExSetTimerResolution` nor `MmGetSystemRoutineAddress`; it goes through `xhcimsi.sys` (design record 18), extended to load on Windows 2000 and XP (owner, 2026-10-13), and **renamed `xhcihlpr.sys`** in the same task, since the helper will do more than MSI (owner, 2026-10-13): the image, its service, its source directory, its device object (`\Device\Xhci98Helper`) and its channel (V2, MSI and the timer negotiated apart), so an old and a new helper loaded in one boot never collide (owner, 2026-10-13), and every reader of the name - the driver's lookup of the helper, the INFs' file and service rows, `txtsetup.oem`, the build, the import, INF and footprint gates and the import allowlist, the packager and its self-tests, the release notes, the README, `source-files.md` and the design records that name it - with an update from `2.3.0.0` or `2.3.1.0` leaving no `xhcimsi` service or file active beside the new one (`DelService=xhcimsi` and a deferred `DelFiles` of `xhcimsi.sys` on the 32-bit Vista and 7 path; owner, 2026-10-13), read on Vista and 7 as part of the mixed-version legs below; published releases and run records keep the old name, and a release cut before the rename is reconstructed from its own cut commit; the amd64 image asks `MmGetSystemRoutineAddress` as it does for MSI. An export-table walk is undocumented internals and is not the route. The helper cannot load on 2000 or XP today (the review, 2026-10-13: its static Vista imports, its NT 6 entry check and its NT 6-only install rows), so the extension resolves the MSI routines at run time, negotiates the timer separately from MSI, which stays Vista and 7 only, audits every import of the linked libraries against 2000 and XP with a missing baseline fatal for a release, builds the helper with WDK 7.1's `WXP` environment at subsystem 5.00 with the target checked in the build log (owner, 2026-10-13), and widens the INF rows and the install, INF and import gates; the 2000 and XP install asks for no restart of its own, the finer timer working from the next boot, documented (owner, 2026-10-13); mixed helper and driver versions are read across updates and restarts; F6 Setup is documented as keeping the clock tick until the installed system's own install brings the helper (owner, 2026-10-13). Microsoft warns that `ExSetTimerResolution` can deadlock against a power IRP, so no power IRP's completion may depend, directly or indirectly, on the call completing: the release at a power transition is made asynchronously, with the lifetime of what it uses kept safe; the request is balanced across several controllers. Record 15 gains the section first, then the import gate's evidence, a review loop, and legs: polled UAS VERIFY and TONE on 2000, XP, XP x64 and 7 with the switch on and off against the readings of record 15 section 11; the tick read back by phase observations of short requests (`KeQueryTimeIncrement` is not the current resolution; the poll interval counters of record 15 section 8 are) against a switch-off baseline; the release seen at idle, stop (with a child's power IRP outstanding and the timer call delayed), S3/S4 and shutdown; two controllers asking different values; and the idle period measured at 50 and 500. **Changed by the owner, 2026-10-14: `XhciPollFineTimer` ships off by default (opt-in)**: both INFs write 0 on every path, under the same don't-overwrite rule, and an absent, unreadable or other-type value is off; the test matrix read on QEMU guests under TCG, polled (`XhciInterruptMode` 2), on 7 x64, 7 x86 and Vista x86, that with the finer timer held the guest's clock ran at 0.1 to 0.7 of real time and USB keyboard input was lost, and with the value 0 or under an interrupt the clock ran at real time and nothing was lost; real hardware unread, and the default is reconsidered on a reading there (design record 15 section 7.2.4's amendment)
- [x] 38.4 (from the `2.3.1.0` legs, 2026-10-13, run-37 "37.final") the readme and release notes on a UAS disk's install under Windows 7: they say the Found New Hardware wizard asks for `xhciuas.inf`, but Windows 7 shows no wizard - it searches Windows Update, reports the device was not installed, and the driver goes in through Device Manager, Update Driver Software -> Browse my computer -> the `release` directory. Also on Vista the wizard names the device by its own product string (QEMU's "USB Attached SCSI HBA"), not "xHCI98 USB Attached SCSI Storage", which is the name once installed. Documents only (the readme template in `make-release.ps1`, the release notes' UAS section, the acceptance test's 5.7); a later cut carries it. Done 2026-10-13 (run-38, "38.4"): the three documents, and the release notes' install steps
- [x] 38.5 (from the `2.3.1.0` legs, 2026-10-13) 32-bit Vista after a controller remove and rescan: the controller came back at Code 1 until a restart and no restart prompt was shown, only an "installed successfully" balloon (32-bit Windows 7 prompted on the same path; the first install prompts on both). The restart is the INF's flag for `xhcimsi.sys`, unchanged since `2.3.0.0`. Say in the release notes and the readme that a 32-bit Vista or 7 reinstall needs a restart whether or not Windows asks; read whether Vista's missing prompt also shows on an update over an installed `xhcimsi.sys`. Done 2026-10-13 (run-38, "38.5"): the documents say it; on an update through Have Disk, 32-bit Vista and 7 both asked for the restart, so the prompt goes missing only on remove and rescan. Found on the way: Browse my computer with the folder typed in keeps `2.3.0.0` on both; the update sections now give Have Disk
- [x] 38.6 (from the `2.3.1.0` legs, 2026-10-13) `portsw-on.bat` and `portsw-off.bat` print "RESTART WINDOWS" twice, once from `portsw.vbs` and once from the batch file's own echo; drop one, keeping the batch files COMMAND.COM-safe, and read the output on 98 SE or ME and on one NT target. Cosmetic. Done 2026-10-13 (run-38, "38.6"): the batch files' echo dropped, the package self-tests refuse it; read once on 98 SE and on XP
- [x] 38.7 the USB device interface off external hubs (GitHub issue 17 item 3, reopened by SweetLow on 2026-10-12: `GUID_DEVINTERFACE_USB_DEVICE` is for "all USB devices except hubs", a hub carrying `GUID_DEVINTERFACE_USB_HUB` alone). Since 36.17 `HcdDoorDeviceStart` (`hcd_door.c`) registers it on every device PDO and every external hub's PDO, which record 13 section 8.10 says is "as usbhub registers it", with no reading behind it. First the reading, per target as AGENTS.md requires: on Windows 2000, XP and 7 with the stock stack, a USB 2.0 hub with a device behind it, which devnodes carry the interface (`SetupDiGetClassDevs` with `DIGCF_DEVICEINTERFACE`, or the `DeviceClasses` key), and with it the other two of SweetLow's list (owner, 2026-10-13): `GUID_DEVINTERFACE_USB_HUB` (`{F18A0E88-C30C-11D0-8815-00A0C906BED8}`, every hub, root and external) and `GUID_DEVINTERFACE_USB_HOST_CONTROLLER` (`{3ABF6F2D-71C4-462A-8A92-1E6861E6AF27}`, the controller). If hubs carry none, the hub PDO stops registering it; record 13's paragraph is corrected either way. A review loop, host tests where the rule is testable, both builds. Legs: the interface enumerated on 2000, XP, XP x64 and 7 x64 with QEMU's hub (Full Speed) and a device behind it, which suffices (owner, 2026-10-13): the rule turns on the hub flag every hub's PDO gets at its creation, whatever the hub's speed, so a USB 2.0 hub and both halves of a USB 3 hub take the same path, which the review checked statically: the devices listed, the hubs not; the hub interface on the root hub and each hub alone; the host controller interface on the controller alone; `usbview` and Device Manager unchanged. Done 2026-10-13 (`c4c2bab4` to `b113f3b4`; Codex `01a133b3`, clean at round 1; run-38, "38.7"): the stock reading on 2000, XP and 7 showed the device interface on devices only, the hub interface on hubs only and the host controller interface on the controller only (on 2000 on none, the bus keeping it there, owner); the hub PDO now registers no device interface; the legs read on 2000, XP, XP x64 and 7 x64
- [ ] 38.8 the bus's own device ids under `USB\` (GitHub issue 17 item 10, SweetLow, 2026-10-12: there is no `XHCI98` bus): `XHCI98\ROOT_HUB`, `XHCI98\HUB`, `XHCI98\HUB30` and `XHCI98\COMPOSITE`, with their `&VID_`, `&PID_` and `&REV_` forms (`hcd_rh.c`, `xhci_hub.c`, `xhci_func.c`), become `USB\XHCI98_ROOT_HUB`, `USB\XHCI98_HUB`, `USB\XHCI98_HUB30` and `USB\XHCI98_COMPOSITE`. The private ids are meant to keep every stock row from matching those too, which the design verifies by audit, so the root hub still never binds to `USB\ROOT_HUB` and no OS composite parent comes back (record 13 section 10.12). The device id's first component names the enumerator, so every root hub, external hub and composite devnode moves from `Enum\XHCI98` to `Enum\USB`. First a design section in record 13: the update over an installed `XHCI98\` tree on every install path (9x, NT 5.x, NT 6.x, amd64), what becomes of the old devnodes and what removes them; Windows 98 SE's and ME's handling of `Enum\USB`, with and without SweetLow's stack; and every reader of the ids (both INFs, `txtsetup.oem`, the INF and package gates, the import gate's evidence list, the VM matrix scripts, the host tests, the documents). The owner decides the design, then a review loop; the code, host tests, both builds with the INF gate. Decided already (owner, 2026-10-13): the whole task in this phase, and the old `XHCI98\` devnodes removed by an opt-in tool under the release's `tools\` (its source tracked in the tree), which removes only non-present devnodes of this project through supported PnP routes, an install needing no run of it. From Codex's review: changed parent ids need the parents installed anew and may change the instances and settings of the devices beneath, so the design measures both the serial-number and the location-based cases (record 13 section 10.13; on 98 SE as well as NT, whose location-form key carries the parent's path, record 13 section 10.7.1); the private hardware ids and the empty compatible-id lists stay, and stock and vendor INF matches are audited, not assumed impossible; both F6 files and their gates change. Decided on the design (owner, 2026-10-13; record 13 section 10.13, reviewed to convergence): the task stays in `2.4.0.0` with its update costs; no old `XHCI98\` id as a further hardware id, and the INFs and `txtsetup.oem` files drop the old ids (a clean move: on 98 SE and ME no new hub or composite device is connected before the restart); the root hub's install sections copy nothing; the prompts the update brings on XP, XP x64 and 98 SE need a keyboard and mouse independent of the updated driver (PS/2, or a controller another stack drives), and the new devnodes and lost settings of serial-less devices are documented, not worked around; the tool is `XHCICLN.EXE` with a native `XHCICL64.EXE` (source `xhcicln\`, shipped `tools\xhcicln\`), the 32-bit build listing but refusing removal under WOW64; it lists by default and removes on `-remove` after one question (`-yes` skips it), only conclusively absent `XHCI98\` devnodes of the four old forms, with every project controller started, its new root hub started and its door held open for the run, re-checked around each removal, through `DIF_REMOVE` with no fallback; on 98 SE and ME it lists only in this release, `CM_Uninstall_DevNode` there waiting on readings of its exports, the configuration manager's devnode list and an exclusion during a run; old refused-interface devnodes are NOX.1's and the tool leaves them. Legs: a fresh install and an update over `2.3.1.0` on 98 SE (stock and under SweetLow's stack), ME, 2000, XP, XP x64, Vista and 7 x86 and x64, with a hub and a composite device, and on each a device with a serial number and one on the location form, at a root port and behind the hub, a composite's function in both forms; Device Manager clean (one current root hub per controller), the old entries read in the hidden view (NT, `DEVMGR_SHOW_NONPRESENT_DEVICES=1`) and in Safe Mode (98 SE, ME) before and after the tool, and no stale devnode claiming a device; on 98 SE and XP a serial-less USB keyboard and mouse as the only input; on 98 SE a chain of five serial-less hubs ending in a serial-less composite device, its keys read before and after; a two-controller update; the tool listed and run on every target, its refusals forced (a present old devnode, a disabled controller, no present controller), the controller's veto read on each NT target, `XHCICL64.EXE` on XP x64, Vista x64 and 7 x64; F6 installs on 2000, XP and XP x64 with the input behind the hub and a composite bound. QEMU's hub is Full Speed: it stands for the USB 2.0 hub's id before the cut (owner, 2026-10-13); a High Speed hub and a USB 3 hub are read on the owner's bench or by passthrough after the cut, and `USB\XHCI98_HUB30` rests until then on host vectors and the INF gate
- [x] 38.9 the release's tools in a `tools\` directory (owner, 2026-10-13). Done 2026-10-13 (`fc58af43` to `2af5ec79`; Codex `01a13334`, clean at round 4; legs on MS-DOS, Windows 98 SE and XP from a local dry cut, `runs/run-38.md`, "38.9"): `xhciqual\`, `xhcisnap\` and `portsw\` move from the version root to `tools\xhciqual\`, `tools\xhcisnap\` and `tools\portsw\`, so the root holds only the driver directories, the readme and the licence; 38.1's batch files go inside `tools\xhcisnap\` and `tools\xhciqual\`, beside the tool each one runs (owner, 2026-10-13), not loose in `tools\`. Releases already published keep their layout. Every reader of the paths changes with it: `make-package.ps1`, `make-release.ps1` and its readme template, `test-package.ps1` and the package self-tests, `build-driver.cmd`, the release notes, the acceptance test, the README, `xhciqual\README.md`, `xhcisnap\README.md`, design record 16 (37.5 placed `portsw\` at the version root, revision 6) and `source-files.md`; run records keep the paths they read. Legs: the package tree read; `XHCIQUAL` from `tools\xhciqual\` under MS-DOS, `XHCISNAP` from `tools\xhcisnap\`, and `portsw-on.bat` and `portsw-off.bat` from `tools\portsw\` on 98 SE or ME and on one NT target
- [ ] 38.10 the Linux xHCI quirk population in the driver, each behind a registry gate for the controllers it names (owner, 2026-10-13; first opened as 37.17 on 2026-10-12 and removed from Phase 37 the same day). Linux is already the stated authority for the quirk population of `xhciqual/quirks.c` (`legal-provenance.md`, the mirrors); the driver itself carries only its own (the routing quirk `XhciIntelIrqRoute`, the port switch `XhciIntelPortSwitch`, the interval cap `XhciIntervalCap`). First a survey, written as a design record: every quirk flag Linux's xHCI host driver sets by vendor and device id (PCI and its quirk table), from the pinned mirror in `external/` with the commit named; for each, what the controller does wrong, the controllers, whether it applies to this driver's targets and code paths at all (a quirk for a feature the driver does not use, such as LPM or a platform glue, is listed and set aside), whether the driver already covers it another way (Phase 35's tolerance, design record 17, among them), and the proposed gate and default. The owner decides which are taken and each default. Then the taken ones are implemented, adapted from Linux's code where that is the clearer source (owner, 2026-10-13: Linux's xHCI files are GPL-2.0 and so is this project), under the conditions of `legal-provenance.md`'s exception for Linux's quirks (every upstream copyright, licence and disclaimer notice kept, the change marked and dated, a row per copy, the release notices naming it and the release's tag offered as its source, checked at 38.final; no file under another licence), and checked against the xHCI specification, each with its registry value under the controller's key, matched by vendor and device id (no revision matching; the revision is logged; owner, 2026-10-13), reported by `XHCISNAP` and `XHCIQUAL` (which already names them), and a host test of the match. The survey and the decisions: design record 20, reviewed to convergence 2026-10-14 - fourteen values named for their effect (one widening `XhciIntervalCap`), value 2 (every controller) only where the fix is generic xHCI behaviour and a 2 read as 1 on the vendor-register ones, the forced BIOS handoff dropped on the specification, Etron's Reset Device left as it is, platforms with no CSM not applicable and those whose CSM is not established deferred pending evidence (owner, 2026-10-13); and the endpoint-ring ownership fix the survey exposed (record 20 section 5: a ring is returned to the pool only when a command proves the controller has given it up, Linux's rule; the old alternate setting re-instated after a STALLed SET_INTERFACE), which bounds Panther Point's 64 endpoint contexts by the pool, so it needs no quirk of its own (owner, 2026-10-13). Legs: QEMU for what it can show, which is little, since its controller carries no quirk, and each quirk's registry value documented with the controllers it was read on
- [ ] 38.11 xHCI 0.96 controllers accepted (owner, 2026-10-13): today `XhciCheckCapDword0` (`xhci_caps.c`) refuses a controller whose HCIVERSION is below 1.0, so its start fails, which is where the first-generation NEC/Renesas uPD720200 and uPD720200A (`1033:0194`) and other 0.96 parts stop. The floor becomes 0.96 behind a registry value under the controller's driver key, on by default, `0` restoring the 1.0 floor. A 0.96 controller is driven as a 1.0 one but for the rules Linux applies below 1.0 (owner, 2026-10-13): the TD Size field in the units the controller's version defines - remaining packets from 1.0, remaining bytes divided by 1024 on 0.96 (each capped at 31), Linux's form on the last TRB; the Setup TRB's Transfer Type and the isochronous TBC and TLBPC written 0; HCCPARAMS2 treated as 0; and SIA on every isochronous TD, never a Frame ID (design record 17 section 4.14, D1 to D4 and D8) - checked against xHCI 1.2c Appendix G ("0.96 Exceptions") and Linux's handling of 0.96 parts, since no official public copy of the 0.96 specification was found (Intel distributed the draft under a contributor agreement; design record 17 section 4.14, `docs/references/README.md`); the other 0.96-era quirks Linux applies (NEC's isochronous Link TRB chaining, the AMD 0x96 rules; design record 20) are not taken, and any other behaviour of 1.0 the driver depends on that 0.96 lacks is found by an audit against that specification and put to the owner. Design record 17 section 4.14 first (reviewed to convergence 2026-10-13; a refused start's diagnosis carried by the counters and flushed to DebugView at every refusal of the initial start, D5; a `qemu`-flavour `XhciQemuHciVersion` letting a guest's controller report 0.96, D7), a review loop, host tests of the floor, the gate and both TD Size forms, both builds with the INF gate (the value's row), `XHCISNAP` and `XHCIQUAL` naming the version and the gate. No virtual machine presents a 0.96 controller (QEMU's reports 1.0), so the legs are the gate's default and `0` on QEMU, unchanged, and a 0.96 controller is read on real hardware, after the cut if not before, said so in the release notes
- [ ] 38.12 the composite devnode's own `SELECT_CONFIGURATION` (GitHub issue 17 item 11, SweetLow, 2026-10-14: \"Under Microsoft stack URB_FUNCTION_SELECT_CONFIGURATION come for composite device, not for separate items of this device. Not so in xHCI98 implementation.\"; opened 2026-10-14 by the owner from that item, in `2.4.0.0`). Under Microsoft's stack the composite parent (`usbccgp.sys` from XP and on ME, `usbhub.sys` on 98 SE and 2000) selects the whole configuration down the composite devnode's own stack - read for `usbccgp` (ReactOS's, and XP SP3's statically: before it creates its function PDOs); 98 SE's and 2000's `usbhub` composite code is not read, hidusbf's notes implying the same route there - so hidusbf as a lower filter on the composite devnode (its Setup's \"Process Parent\") rewrites every function's `bInterval` there; under this bus since 36.43 the `USB\XHCI98_COMPOSITE` devnode is bound to `xhci98.sys`, the bus configures the device itself (record 13 section 10.8) and the parent PDO refuses every URB, so a filter there sees nothing, and only a filter on a function devnode reaches a function (10.9). The design (record 13 section 10.14, reviewed to convergence before the code; its decisions D1 to D13 recommended and adopted 2026-10-14 under the owner's instruction to proceed after review): the composite FDO's `START`, after the `START` has come back up, sends down its own stack - a lower filter included - `GET_DESCRIPTOR` for the device and the configuration and one `SELECT_CONFIGURATION` over the descriptor that came back, every interface at alternate 0, as `usbccgp` does (ReactOS's `usbccgp` and a static read of XP SP3's, `legal-provenance.md` section 4), the old record forgotten first; a failure never fails the start. The parent PDO answers those two `GET_DESCRIPTOR`s from its copies and records the select in its dispatch, with no queue and no controller thread - validated against the device's own configuration, the caller's descriptor kept on the parent PDO as a reference-counted copy, nothing configured, no `SET_CONFIGURATION`, no endpoint touched - and answers an unconfigure, which the FDO sends at its `REMOVE` as ReactOS's `usbccgp` does, by forgetting the record. A function's select then takes each endpoint's `bInterval` from its own caller where that differs from the device's (today's rule), else from the record where the endpoint's first six bytes match the device's, else the device's own; a function's later `SELECT_INTERFACE` takes the record's too; a rewrite of anything but `bInterval` is ignored per endpoint and counted; open pipes are never re-opened for a record that arrives after them. Counters appended after word 240, `comp.*` log records, the stop's counter lines, `XHCISNAP`'s names; host tests of the precedence, the select's builder and its validation; both builds with every gate; a full review loop on the code. Legs: hidusbf on the composite devnode, and for comparison on the mouse function's devnode, with a composite HID device by passthrough on 98 SE, 2000, XP and 7 x86 (QEMU offers no keyboard-and-mouse composite); with no filter, the regression on QEMU's `usb-audio` on every target; and mode 4's codes from the composite. Without a passthrough device the matrix reads the regression and the rest is owed to the bench, said so in the release notes
- [ ] 38.final the builds, cut and check of `2.4.0.0` (owner, 2026-10-13). Stamped with the version and a `DriverVer` of the cut's date; built and gated, x86 and amd64, all three flavours each (the INF, import and package gates and the host tests); cut by the owner to `releases\<version>\` in 38.9's layout, signed and verified, the zip's SHA-256 recorded; the install legs read from the asset on every target (the release notes' row "Every target, from the release package"), with the legs each task above leaves to the cut; "Untested ground" in the README and the release notes keeping only design-level gaps (the owner removed the suspend, standby, remote wake and real-hardware bench rows from both on 2026-10-13); the `releases/history.md` entry; then publishing

Checkpoint: the design-first tasks (38.2, 38.3, 38.8, 38.11 and 38.12)
decided by the owner and reviewed to convergence before their code; 38.7's
reading taken on 2000, XP and 7 before the hub's interface changes; each
task's legs read as it names them; 38.4 to 38.6 in the readme template
and the release notes; and 38.final's install legs read from the asset; and 38.8's High Speed and USB 3 hub readings, taken after the cut, said so in the release notes' "Untested ground"; and 38.12's composite-devnode filter read on a composite HID device, or owed to the bench and said so there.

Records: `runs/run-38.md`; design record 13 (sections 8.10 and 10.12, and
the sections 38.2 and 38.8 add); `design/15-polled-event-delivery.md`;
`design/16-intel-port-switchover.md`; `design/17-controller-tolerance.md`;
`design/18-msi-helper-x86.md`; `docs/contributing/legal-provenance.md`;
`runs/run-37.md`, where 38.4 to 38.6 come from; GitHub issue 17;
`docs/using/release-notes.md`.
