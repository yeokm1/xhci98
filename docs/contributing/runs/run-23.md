# Phase 23 Record - GitHub issue 4's open requests, and the moderation experiment

The detail behind `docs/contributing/roadmap-phases-17-on.md`, "Phase 23 -
GitHub Issue 4's Open Requests, and the Interrupt Moderation Experiment". The
roadmap entry carries the goal, the status, the task table and the checkpoint;
this file carries what each task did and what each reading said. Where the two
disagree about a clause, the roadmap wins.

**Written while the phase is open**, and kept as written rather than rewritten
in the past tense. A sentence saying something "is owed" describes the day it
was written; the roadmap's task table says how each task closed.

**On `out\...` and `vm\...` paths in this file.** They say where a reading was
taken and what the file was called, on the host that ran it; they are not
files a clone has.

Opened 2026-09-20 by task 23.1, which is the first task of this phase to take
a reading.

---

## 23.1 - the controller's property page (GitHub issue 4 item 5)

Status: **done. The host side, all three guest legs and the documents were
taken on 2026-09-20**, the legs being Windows 98 SE under NUSB (A), Windows
98 SE under SweetLow's stack (C) and Windows ME under SweetLow's (B). The
Windows 98 line is in `src/xhci98.inf`, the INF gate has a `PROP-*` family
holding it in place, the footprint has learned it, and the tab, the checkbox
and the Bandwidth Usage dialog have all been read in a guest on every stack.
Leg C also settled the `usbui.dll` conflict (A4) and showed that an in-place
Update Driver, which NUSB cannot survive, delivers the line cleanly on
SweetLow's stack; Leg B repeated that on Windows ME. The NT half is not this
task's: it is deferred to roadmap task 23.1.5. What remains of task 23.1 is
the merge.

### What landed

One directive, in `[Xhci.AddReg]`, which only the 16-bit engine reads:

```ini
HKR,,EnumPropPages,,"sysclass.dll,USBControllerPropPage"
```

It is the third value NUSB 3.3's own `[EHCI.AddReg]` writes, and the 9x half
of this INF is transcribed from that section, so the line is a transcription
rather than an invention. Windows 98 SE and Windows ME both read this section.

### The static readings 23.1 owed, and what they answered

The roadmap owed three things before the line ships. Two of them are now
answered statically, and the third is narrowed to a prediction a guest can
confirm or refute in one step. **Everything in this section is `static`** -
strings and headers out of the shipping binaries, nothing executed - and is
tagged that way wherever it is cited.

#### What "Disable USB error detection" writes

`sysclass.dll` is a 16-bit NE module (`e_lfanew` 0x80, signature `NE`), and it
carries, adjacent in its data:

| string | what it is |
|---|---|
| `ErrorCheckingEnabled` | the value name |
| `SOFTWARE\Microsoft\Windows\CurrentVersion\Usb` | the key |
| `SystemTray_Main` | the window the setting is handed to |
| `&Disable USB error detection` | the checkbox's own caption |

That value name appears in **no USB driver this repository has extracted**.
The six trees searched were `tools/nusb-extracted` and `tools/nusb36-extracted`
(NUSB 3.3 and 3.6, including `USBPORT.SYS` and `USBHUB20.SYS`),
`tools/sweetlow-extracted` (`USBPORT.SYS` and four `USBHUB20.SYS` variants),
`tools/win98se-extracted` (`usbd.sys`, `usbhub.sys`, `ntkern.vxd`),
`tools/win2ksp4-extracted` and `tools/winxpsp3-extracted`. It appears in
exactly one file: `SYSTRAY.EXE`, the shell's system tray, which carries it
beside `usbui.dll`, `USBErrorMessagesEnable` and `\\.\HCD%d`.

**So the box is a shell switch, not a stack one.** It turns off the tray's own
USB error reporting. Three things follow, and they are what the roadmap asked
for:

1. **This driver cannot be harmed by it.** Whatever `SYSTRAY.EXE` does with
   `\\.\HCD%d` it does on every 9x machine today, with or without this
   property page and with or without the value - so the checkbox can only
   quiet traffic that already exists, never create any. And if any of it
   reaches the miniport at all, it arrives through usbport's PassThru, which
   `xhciPassThru` GUID-matches and answers `MP_STATUS_NOT_SUPPORTED`
   (`src/xhci_dispatch.c`).
2. **The key is machine-wide**, so a user who ticks the box changes every
   controller usbport drives and the value outlives this devnode. That is the
   exact shape that got `DisableSelectiveSuspend` removed at `1.1.0.0`
   (`docs/issues/05-idle-suspend-and-disableselectivesuspend.md`). The
   difference, and it is the whole difference: this package writes nothing
   there. It exposes a box Windows already has.
3. **Windows ME's `SYSTRAY.EXE` is a different build** and carries
   `ErrorCheckingEnabled`, `usbui.dll` and `\\.\HCD%d` but **not**
   `USBErrorMessagesEnable`. The Windows 98 guest's `SYSTRAY.EXE` is
   byte-identical to NUSB 3.3's (SHA-256 `45c94ee5...`), which is expected -
   that guest is the post-NUSB one.

What is still inference and is owed a guest: that ticking the box writes
`ErrorCheckingEnabled` to that key, and what data it writes. The strings put
the name and the key in the same module as the caption; they do not prove the
checkbox is the writer. **One read-back settles it** - leg A3 below.

#### Windows ME: the same module

| | Windows 98 SE | Windows ME |
|---|---|---|
| `SYSCLASS.DLL` | 27,184 B, 1999-04-23, `4.10.2222` | 27,408 B, 2000-06-08, `4.90.3000` |
| extracted strings | 254 | 254 |

The two string lists are **identical except for four entries**: one code
fragment, and the three version-resource lines (`4.10.2222` -> `4.90.3000`,
and the product name gaining "Millennium"). Every caption, every value name,
every key and both provider entry points match. So the Windows ME half of
the line is the same code drawing the same page, and the guest leg below is a
confirmation rather than an open question.

Read out of `vm/winme.img` and `vm/win98.img` with 7-Zip (`7z e <img> -r
sysclass.dll`), which does not write to the image.

#### The Bandwidth Usage dialog, and a conflict worth resolving

**RESOLVED 2026-09-20 by A4, taken on the Leg C guest: the static prediction
was right and the 2026-09-07 reading was wrong. See "A4" below.** The section
that follows is kept as it was written, because it is what the prediction
rested on before it was measured.

This one is **not** settled, and the static reading puts it in tension with
the 2026-09-07 measurement rather than confirming it.

`usbui.dll` (the Windows 98 guest's, 147,456 B, a 32-bit PE) exports
`USBControllerBandwidthPage` and `USBHubPowerPage` among its named exports,
along with a `UsbItem` class whose members include `ComputeBandwidth`,
`CalculateTotalBandwidth(ULONG, UCHAR, PUSB_PIPE_INFO)`,
`EndpointBandwidth(ULONG, UCHAR, UCHAR)` and `CalculateBWPercent`. And
`sysclass.dll` carries, adjacent: `usbui.dll`, `Data Access Error`, "An error
occurred while trying to access the requested data.", `USBHubPowerPage`,
`USBControllerBandwidthPage`. That is a `LoadLibrary32W` / `GetProcAddress32W`
shape - the documented 9x route by which a 16-bit module calls a 32-bit one -
with an error message for when it fails.

**It predicts that the Advanced tab is sysclass.dll's but the Bandwidth Usage
dialog behind its button is usbui.dll's.** The 2026-09-07 reading recorded
"the tab and the dialog render identically with `usbui.dll` renamed away". One
of the two is wrong, and which matters: the owner's E460 has `sysclass.dll`
and no `usbui.dll`, so on a machine that never installed `1.0.2.0` or later
the button could raise "Data Access Error" rather than a dialog.

If the prediction holds it is not a problem, it is a second reason for a
decision already taken: since `1.0.2.0` this INF copies `usbui.dll` on all
four install paths, so any machine that installs this package has it. What it
would change is the record - `usbui.dll` on 9x is described everywhere in this
repository as buying nothing, and it would turn out to buy the dialog.

Leg A4 below is what resolves it.

### The NT half: deferred, not refused

The counterpart pair is
`HKR,,EnumPropPages32,,"usbui.dll,USBControllerPropPageProvider"` plus
`HKR,,Controller,1,01`, and **all three NT references this project reads write
exactly it** for their own EHCI controller:

| reference | read | carries the pair |
|---|---|---|
| Windows 2000 SP4 `USB.INF` `[EHCI.AddReg.NT]` | `build-and-test.md`, verified model | yes |
| Windows XP SP3 `usbport.inf` (UHCI, OHCI, EHCI) | `vm/winxp.img`, static, 2026-09-20 | yes, three times |
| Windows Vista SP2 `usbport.inf` (UHCI, OHCI, EHCI) | `vm/vista.img`, static, 2026-09-20 | yes, three times |

The Vista file is **UTF-16**, so a byte grep for the name finds nothing in it;
it has to be decoded first. That is worth recording because it is how the NT
6.x half looked settled-in-the-negative for an afternoon.

Windows 7's own `usbport.inf` has **not** been read: 7-Zip sees `vm/win7.img`
as a qcow2 whose live state is in a snapshot and will not walk its NTFS.

The provider is present on every NT path this package installs on - this INF
copies `usbui.dll` on the NT 5.x paths, and Vista ships it at
`Windows\System32\usbui.dll` (83,456 B, read the same day). So neither the
evidence nor the file is what holds the NT half back.

**What holds it back is the owner's decision of 2026-09-20**: one
`[Xhci.AddReg.NT]` serves four install paths - Windows 2000, 32-bit XP, and
Vista and Windows 7 x86 through `[Xhci.Dev6.NTx86]` - and the page it would
draw has been opened in none of their guests against this controller. The 9x
half ships first with a before-and-after reading behind it. Taking the NT half
later means: the guest readings on Windows 2000 and Vista, inverting
`PROP-NTHALF` in the gate, and the pair landing in **both** INFs at once or
the two files drift.

### The gate: the `PROP-*` family

`scripts/inf-gate/check-inf.ps1`. The value is read by nobody in this project -
it is a `REG_SZ` the 9x shell reads - so it gets rules for the reason every
cosmetic line here gets them: nothing checks the provider string at install
time, and a typo in it is invisible. The tab simply does not appear, on a
target whose setup engine has no log at all.

| rule | fires when |
|---|---|
| `PROP-MISSING` | the Windows 98 path writes no `EnumPropPages` |
| `PROP-PROVIDER` | it does not name exactly `"sysclass.dll,USBControllerPropPage"` |
| `PROP-FLAGS` | it carries a numeric flags field (`FLG_ADDREG_TYPE_SZ` is 0, spelled empty) |
| `PROP-SUBKEY` | it is written under a subkey rather than on the devnode key |
| `PROP-DUP` | it is written more than once on one path |
| `PROP-NTHALF` | any NT path writes `EnumPropPages32` - the deferral, enforced |
| `PROP-STRAY` | an NT path writes the 9x `EnumPropPages`, which no NT engine reads |

`PROP-PROVIDER` is the one that earns its keep: the NT provider is
`usbui.dll`, this INF copies `usbui.dll` on all four paths, and on 9x
`usbui.dll` draws no tab at all. A file naming it here would install cleanly
and show nothing.

Eight cases in `scripts/inf-gate/test-inf-checks.ps1`, one per rule plus a
second `PROP-PROVIDER` case for the right-file-wrong-entry-point shape.

**Two existing self-test cases broke on this change and were re-anchored.**
`logverbosity-no-9x` and `logdbgview-no-9x` were anchored on
`HKR,,NTMPDriver,,xhci98.sys` followed directly by the log values, and the new
line landed between them. `Assert-RuleFires`' unchanged-mutation guard is what
caught it - the second time that guard has earned its place. Both are now
anchored on the `EnumPropPages` line, which is 9x-only like `NTMPDriver` was,
so they still cannot drift onto an NT path.

### A consequence worth stating: already-published INFs now fail the gate

`PROP-MISSING` fails every INF this project has published, because none of
them carries the line. That is not new in kind - `releases/1.0.2.0`'s INF
already failed the current gate on `PATH-MFGDEC` and `SUSP-GLOBAL`, both rules
added after it was cut - and it follows from the gate being a check on the INF
this project builds now rather than a validator of historical artifacts.

**But `1.1.0.0` is cut and not yet uploaded**, and
`scripts/package/make-release.ps1 -UploadSetOnly` re-gates the published tree.
So assembling `1.1.0.0`'s upload set after this change refuses on
`PROP-MISSING`, where it passed before. That is the owner's call, and the two
ways out are to upload `1.1.0.0` before this lands or to re-cut it with the
line in.

**Decided 2026-09-20 (owner): upload `1.1.0.0` first.** Its upload set is
assembled and uploaded before branch `23.1` merges, so `-UploadSetOnly` never
re-gates a published tree against a rule written after the cut, and `1.1.0.0`
is not re-cut a ninth time for a cosmetic INF line. The consequence is
deliberate and belongs in the next version's notes rather than this one's:
**`1.1.0.0` ships without the Advanced tab**, so a Windows 98 user gets the
page only by installing the version that follows it. Nothing in this branch
may be merged to `phase-23` until that upload has happened.

### The before/after pair, staged and not yet read

The owner asked for a reading that shows what the line does, so the pair is
built to isolate it and nothing else:

| | `out/t23.1/before` | `out/t23.1/after` |
|---|---|---|
| `xhci98.sys` | `releases/1.1.0.0/release-x86/xhci98.sys` | the same file |
| SHA-256 | `e97fa781...13dc7d` | `e97fa781...13dc7d` |
| `xhci98.inf` | `releases/1.1.0.0/release-x86/xhci98.inf` | `src/xhci98.inf` |
| directive lines differing | \- | exactly one: the `EnumPropPages` line |

Identical binary, and the two INFs differ by one directive and comments. The
`before` directory is the published `1.1.0.0` media copied as-is rather than
re-staged, because re-staging it through `make-package.ps1` is exactly what
the new gate rule now refuses.

Staged for the guests at `vm/T231/BEFORE` and `vm/T231/AFTER`, reachable with
`prepare-image.ps1 -Target <id> -Boot -Xfer -XferAdd vm\T231`.

---

## 23.1 guest legs

### Leg A - Windows 98 SE under NUSB: TAKEN 2026-09-20

Vehicle: `vm/fresh-2a.img` at its only snapshot, `base-1.1.0.0-qemu` - a clean
install of the published `1.1.0.0`, which **is** the before state (the shipped
INF writes no `EnumPropPages`). Run on a local work copy so the
OneDrive-synced `vm/fresh-2a.img` was never written; the driver reported
`DriverEntry (built Sep 18 2026 23:47:30)` and eight USB2-only root ports.
The operator drove the GUI, the agent the monitor and the screendumps.

**A0, the baseline.** The controller's Properties carried **General, Driver,
Resources** and no Advanced tab. A shipped `1.1.0.0` install behaving as
released.

**A0b, the root hub - and it corrects something this repository says.** The
USB 2.0 Root Hub carried **General, Power, Driver**, and the Power dialog
rendered live: "The hub is self powered", "Total power available: 500 mA per
port", and a device list reading "HID-compliant mouse 100 mA" and "7 port(s)
available 0 mA". That is the same page, with the same strings, that the
2026-09-07 readings found on Windows 2000 and Windows XP and recorded as an
**NT** finding; the 9x root hub was evidently never opened. It is **not**
ours: NUSB's own `USB2.INF` registers it -

```ini
[Usb2Hub.AddReg]
HKR,,DevLoader,,*NTKERN
HKR,,NTMPDriver,,usbhub20.sys
HKR,,EnumPropPages,,"sysclass.dll,USBHubPropPage"
```

so on Windows 98 the hub's page comes from the USB 2.0 stack's own INF
through `sysclass.dll`, with nothing from this package. It is therefore the
**control** for this task: task 23.1 writes to the controller's devnode key,
so the hub's page must not move, and it did not.

**A2, the line.** Taken through the real install path rather than by merging
the value by hand. Result: the controller's Properties now carries **General,
Advanced, Driver, Resources**, with the Advanced tab between General and
Driver, holding an unticked "Disable USB error detection" box and a
"Bandwidth Usage" button. The INF delivered the value.

**How A2 had to be sequenced, and why an in-place upgrade was not used.**
This is the NUSB teardown fault (`0028:C00312EE`,
`usbport-miniport-interface.md` section 8): disable, uninstall, in-place
upgrade and rollback all reach it, and **an upgrade commits its file copy and
loses its registry phase**. An Update Driver over the running install would
therefore have copied the binary, dropped the `AddReg`, and read as the line
not working - a false negative, on top of the crash. The operator's route
avoided both:

1. `ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.SAV`
2. shut down and **cold** boot (a warm restart wedges Windows 98 at the splash)
3. with no driver loaded there is no controller to stop, so Update Driver from
   `D:\T231\AFTER` ran to "Windows has finished installing an updated driver"
   with no crash
4. shut down and cold boot again

**So the lost registry phase is a consequence of the crash, not an independent
defect**: remove the crash and the `AddReg` commits normally. That is new, and
it is the first recorded way to upgrade this driver on NUSB without losing the
registry phase.

**A2 raised an Insert Disk prompt for `usbui.dll`, and that is a finding.**
The image already holds `C:\WINDOWS\SYSTEM\USBUI.DLL` (147,456 B), which is
dirid 11 - the destination this INF copies it to, with flag 16,
`COPYFLG_NO_OVERWRITE`. The copy should have been skipped. Windows 98 asked
for a source anyway, so **the 9x file queue resolves a source before
`COPYFLG_NO_OVERWRITE` can skip it** - the same shape as the NT 6.x file-queue
behaviour that forced `[Xhci.Dev6.NTx86]` to copy nothing (roadmap task 21.8).
Answering the prompt with `C:\WINDOWS\SYSTEM` satisfied it immediately. The
release notes already warn that the Windows 98 CD may be asked for; what is
new is that having the file does not prevent the ask.

**A3, what the checkbox writes. The static reading was right.** Ticking
"Disable USB error detection" and reading
`HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Usb`:

| box | value | type | data |
|---|---|---|---|
| ticked | `ErrorCheckingEnabled` | `REG_DWORD` | `0x00000000 (0)` |
| unticked | `ErrorCheckingEnabled` | `REG_DWORD` | `0x00000001 (1)` |

A straight 0/1 toggle that **persists either way** - unticking writes 1 rather
than deleting the value, so a user undoing it by hand sets it to 1. The key is
machine-wide, not the devnode's, and the name appears in no USB driver in any
of the six `tools/*-extracted` trees and in exactly one file, `SYSTRAY.EXE`.
**This closes the roadmap's "what does it write and whether this driver can be
harmed by it"**: it is a shell switch over a key this package never writes,
and it can only quiet reporting that already runs on every 9x machine.

**A5, Bandwidth Usage and issue 6.** The dialog rendered, and with no USB
device attached read "System reserved 10 %" - the same figure the 2026-09-07
reading recorded. Devices were then added from the monitor onto root ports:

| bus | System reserved |
|---|---|
| no USB devices | 10 % |
| + `usb-mouse,usb_version=1` (12 Mb/s) on root port 2 | 11 % |
| + `usb-mouse,usb_version=2` (480 Mb/s) on root port 3 | 12 % |

**A Full-Speed device and a High-Speed device on root ports cost the same
1 %.** That can only hold if the Full-Speed device is budgeted as High Speed,
which is exactly what `docs/issues/06-full-speed-root-port-bugcheck.md`
describes: `usbui.dll` computes from the speed it is told
(`CalculateTotalBandwidth(ULONG, UCHAR, PUSB_PIPE_INFO)` takes a speed byte),
and every root-port device is reported High Speed. On a true Full-Speed bus
that mouse's interrupt endpoint is a far larger slice. **So this dialog is the
first place in the user interface where issue 6 is visible to a user** - which
matters, because task 23.1 is what puts the button there.

Two limits on that reading, stated rather than left implied. The dialog
**never itemises devices**: only the "System reserved" row exists and only its
percentage moves, so the per-device figure is a delta across three readings,
not an attribution the dialog makes. (The root hub's Power dialog does itemise,
so this is the Bandwidth page's behaviour, not a failure to enumerate.) And the
contrast case - the same Full-Speed device behind a hub, where its true speed
is reported - was **not** taken: QEMU's `usb-hub` is Full Speed, so it would put
a Full-Speed hub on a root port, which is the topology that bugchecked Windows 7
in task 22.9 (`STOP 0x7E` in `USBPORT`, issue 6's residual) and is unread on
Windows 98.

**Owed on Leg A: A4**, the `usbui.dll` rename control - see the conflict
above. It now has **two** dialogs to kill or spare rather than one, because
the root hub's Power dialog is the same shape (`USBHubPowerPage` is a
`usbui.dll` export and a `sysclass.dll` string, exactly as
`USBControllerBandwidthPage` is). The image carries `usbui.dll`, so the test
discriminates. Rename `C:\WINDOWS\SYSTEM\USBUI.DLL` away in MS-DOS mode, cold
boot, and press both **Bandwidth Usage** and the root hub's **Power
properties**: if either raises "Data Access Error" instead of its dialog, the
exports win and the 2026-09-07 note is wrong about the dialog. Rename it back
afterwards. The Advanced tab itself is the control and should be unaffected.

**A4 was taken on 2026-09-20, on the Leg C guest rather than Leg A's** - it
needed a 9x machine carrying `usbui.dll` and an Advanced tab, and by then the
SweetLow guest had both, at `C:\WINDOWS\SYSTEM\USBUI.DLL`, 147,456 B,
`04-23-99 10:22p` - byte-identical in size to Leg A's. It is recorded under
Leg C, A4, and its result is that **the exports win**.

### Leg B - Windows ME under SweetLow's stack: TAKEN 2026-09-20

**The vehicle is not the one planned.** The plan named the
`winme-sweetlow-driver` snapshot; the owner stopped the first launch on the
ground that it already carries an xhci98 driver and so cannot produce a clean
`BEFORE`. A new base was built instead - the ME analogue of
`sweetlow-2a.img`'s `sweetlow-stack-nodriver`, by the same recipe:
`vm/winme.img` reverted to `winme-clean-install` (stock ME: no USB 2.0 stack,
no driver), SweetLow's `USB2.INF` installed by hand from `vm\T231\SWEETLOW`,
a Start-menu shutdown, and the result snapshotted as
**`winme-sweetlow-nodriver`**, which the owner asked to keep permanently.
Everything below therefore rests on a genuine **fresh first install** of
`BEFORE`, as Leg C's did, rather than on an upgrade over something. The leg
ran on a local work copy (`-WorkDir`), `vm/winme.img` being in a synced tree,
and was copied back after a clean shutdown.

**A second reason this vehicle is the right one, not known when the leg was
planned.** SweetLow's `USB2.INF` writes, in `[EHCI.AddReg]`, the line

    HKR,,EnumPropPages,,"sysclass.dll,USBControllerPropPage"

character for character the line this task adds, for SweetLow's own EHCI
controller; `[Usb2Hub.AddReg]` writes the hub's
`"sysclass.dll,USBHubPropPage"`, and `[Composite.AddReg]` writes a bare
`HKR,,EnumPropPages` clearing it for composite devices. So the directive under
test is the one both back-ported stacks already use for their own controllers,
and the root hub's page is a control this leg did not have to construct.

**B1, the `BEFORE` install.** From `D:\T231\BEFORE` onto the undriven
`PCI Universal Serial Bus` node. It asked to restart; the answer was No,
followed by a Start-menu shutdown and a cold launch - the only route
`lessons.md` records as working on an ME guest, since a warm restart wedges
the 9x splash and `system_powerdown` hibernates ME rather than shutting it
down.

**B2, the baseline, and it is clean.** `USB 2.0 eXtensible Host Controller
(xhci98)`, Manufacturer `xHCI98 Project`, Hardware version 001, tab strip
**General, Driver, Resources and no Advanced**. General reports "This device
is working properly", which is also the first observation of this driver
registering and starting under SweetLow's stack on Windows ME from a fresh
install rather than from the 2026-09-02 image.

**B2c, the control, taken before the change.** The `USB 2.0 Root Hub` sheet
reads **General, Power, Driver** - no Resources - and **Power properties**
opens a working dialog: "The hub is self powered", "Total power available:
500 mA per port", one row "8 port(s) available" at 0 mA. The 8 matches QEMU's
`p2=8`.

**That dialog also answers a question A4 left open for this target.** A4
established that on 9x `sysclass.dll` draws the tabs and `usbui.dll` the
dialogs behind their buttons, so a missing `usbui.dll` would make Bandwidth
Usage raise "Data Access Error" for a reason having nothing to do with this
change. The hub's Power dialog rendering means **Windows ME has a working
`usbui.dll`**, and it means it on a page this project did not add.

**B3, the in-place upgrade, and it behaves as SweetLow does on 98 SE.** Update
Driver to `D:\T231\AFTER`, run in place over the running driver - the route
that is fatal on NUSB - **completed with no crash and did not ask for a
restart at all**.

**B4, the tab arrives on the same boot, with no reboot.** The strip becomes
**General, Advanced, Driver, Resources**; the Advanced tab carries "USB
Settings:" with a `Disable USB error detection` checkbox and a `Bandwidth
Usage` button. So Windows ME keeps the upgrade's registry phase exactly as
Windows 98 SE under SweetLow does, and the tab needs the registry write rather
than a driver reload. Both 9x targets now show that, and both show it only
under SweetLow's stack; NUSB is the one that loses the phase.

**B5, the bandwidth ladder, and it repeats to the digit.** Bandwidth Usage
read **System reserved 10 %** with no device, **11 %** with a Full-Speed mouse
on a root port (QEMU `usb_version=1`, port 2, reported by the monitor at
12 Mb/s), and **12 %** with a High-Speed mouse added (`usb_version=2`, port 3,
480 Mb/s). **Both devices cost the same 1 %.** That is the 10 / 11 / 12 ladder
Legs A and C produced, so it is now three USB 2.0 stacks across two operating
systems, and **issue 6 visible in the user interface is neither an NUSB
artifact nor a Windows 98 shell artifact**. Its limits are Leg A's: the
Bandwidth dialog never itemises devices, so this is a delta measurement, and
the behind-a-hub contrast was not taken here either.

**B6, the checkbox writes what the static reading said.** With the box
**ticked**, `ErrorCheckingEnabled` under
`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Usb` reads REG_DWORD
**0x00000000 (0)**; **unticked**, **0x00000001 (1)**. A persisting 0/1 toggle,
not a create/delete, and the same inversion Leg A measured. Windows ME's
`SYSTRAY.EXE` lacks `USBErrorMessagesEnable`, so whether the box has any
visible effect on this target is **not established** - the write is what was
read, and the write is what this task cares about, the value being
machine-wide and this package writing nothing there.

**B7, the control re-read, and it did not move.** The hub still reads
**General, Power, Driver** with a working Power dialog, so the change touched
only the controller's devnode. With both mice attached the dialog now
itemises them: two rows of `USB Human Interface Device` at **100 mA** each and
`6 port(s) available` at 0 mA, 8 - 2 = 6. Worth keeping on its own: **the
hub's Power dialog itemises devices where the controller's Bandwidth dialog
only ever shows "System reserved"**. Leg A recorded the Bandwidth page as a
delta measurement only; that is a property of that dialog, not of `usbui.dll`.
If issue 6 ever needs a per-device witness in the user interface, the hub's
Power page is the one that gives it.

**B9, the Windows CD: never asked for, at any point.** Three data points now:

| Leg | OS | Stack | Install | CD asked |
|---|---|---|---|---|
| A | 98 SE | NUSB | upgrade | yes, for `usbui.dll`, already at dirid 11 |
| C | 98 SE | SweetLow | fresh, then in-place upgrade | no |
| B | ME | SweetLow | fresh, then in-place upgrade | no |

A and C hold the operating system constant and differ in the stack, so the
stack correlates and the OS does not. **That is a correlation across three
runs with install type confounded alongside it, and nothing here establishes a
mechanism.** It is a lead for the `COPYFLG_NO_OVERWRITE` finding owed to 23.7,
not a conclusion.

#### B8, read off the disk afterwards, and it found something the leg did not go looking for

The guest `dir` was skipped on the argument that the stack's provenance was
established by construction - stock ME ships no `usbport.sys`, this driver is
a `usbport.sys` miniport, and it started. The files were then read out of the
image with `7z e` instead, which costs no guest time, and the argument was
right about what it would confirm and blind to what it would reveal.

Stock `winme-clean-install` carries **no `USBPORT.SYS` at all** (the extract
returned only the other file asked for), which verifies that documented claim
directly rather than by inference, and `USBCCGP.SYS` at **18,288** bytes.
After SweetLow's install the image carries `USBPORT.SYS` **134,912**,
`USBEHCI.SYS` **20,224** and `USBHUB20.SYS` **50,560** - SweetLow's, matching
the staged `vm\T231\SWEETLOW` sizes - and `USBCCGP.SYS` still at **18,288**,
unchanged.

**So the three files Windows ME never had were placed, and the one it already
had was not overwritten.** That is not the INF declining to: `USB2.INF`'s
`[Composite.CopyFiles]` is a bare `usbccgp.sys` with no copy flags, reached
from `DefaultInstall`, so an unconditional overwrite was asked for.

Two ordinary explanations were tested and both fail.

**It is not a replacement queued for a restart that never came** (the
operator's hypothesis, and the right one to test first). The snapshot taken
immediately after the SweetLow install and shutdown, before any further boot,
already carries `USBPORT.SYS` at 134,912 dated 23 Apr 2024 - SweetLow's file,
placed - and `USBCCGP.SYS` at 18,288 dated **8 Jun 2000**, which is Windows
ME's own; and the image carries **no `WININIT.INI`**, so nothing was queued
for the next boot. The copy operation ran in that same install. The guest then
cold booted several times before the file was read.

**It is not the setup engine refusing to replace a newer file with an older
one.** SweetLow's staged `usbccgp.sys` is **5.1.2600.2180** against Windows
ME's **4.90.3000.1**, so the incoming file is the higher version and a version
comparison would have permitted the overwrite.

**The mechanism is therefore not established, and one candidate remains.**
Windows ME's System File Protection fits the pattern exactly - ME is the 9x
release that introduced it, and what survived is precisely the file ME itself
ships while the three it never shipped were placed - but nothing here tested
it, and it should not be written down as if it had been.

It reaches past this leg: **a Windows ME composite-device observation rests on
Windows ME's own `usbccgp.sys`, not on SweetLow's**, the composite audio
device of roadmap task 18.4 included. `build-and-test.md`'s "Windows ME target
VM" section describes the stack as SweetLow's without that qualification and
is owed the correction.

#### Two operating notes

The Add New Hardware wizard does not appear, and Bandwidth Usage does not
change, while a modal property sheet is open: the wizard is the shell's, and
the shell is the process holding the sheet. Close the dialog and the sheet,
let the wizard run, then re-open to read. Not a defect, and not C4's wedge -
the ordinary 9x shell - though it happens to point the same way C4's
operational rule does.

`device_add usb-mouse` moves QEMU's host pointer to the new mouse (`info mice`
shows the `*` move), and the guest has no driver for it until its wizard has
run, so the operator's mouse appears to die. `mouse_set 2` pins it back to the
PS/2 mouse. Host-side, nothing to do with the driver.

#### What the leg left on the image

Snapshots on `vm/winme.img`, in order: **`winme-sweetlow-nodriver`** (the
permanent clean base), `winme-before-installed` (the `BEFORE` baseline, kept
as the leg's fallback point), and `winme-23-1-after-tab` (the finished state,
both mice attached). No device was ever detached, per C4.

### Leg C - Windows 98 SE under SweetLow's stack: TAKEN 2026-09-20

Vehicle: `vm/sweetlow-2a.img` at `sweetlow-stack-nodriver`, which carries **no
xhci98 driver**, so the `BEFORE` install is a genuine **fresh first install** -
the clean before/after Leg A could not give, because Leg A's before was a
pre-installed `1.1.0.0` rather than an install of the `BEFORE` package. Run on
a local work copy; the OneDrive-synced `vm/sweetlow-2a.img` was never written
and nothing was copied back. The operator drove the GUI, the agent the monitor
and the screendumps. Both packages are the `release` flavour, so the debug
console stayed 0 bytes throughout and **Device Manager is the only witness** -
that is correct, not a failure.

The target had to be re-added to this host's `matrix.config.psd1` first; it is
git-ignored, so that was a per-host repair and not a tracked change. The same
pass corrected `Win98Cd`, which named a file that does not exist on this host.

**C1, the fresh `BEFORE` install.** The wizard bound the devnode as **USB 2.0
eXtensible Host Controller (xhci98)** and, after a cold boot, Device Manager
showed it with a **USB 2.0 Root Hub** beneath it and no yellow bang. The
controller's Properties carried **General, Driver, Resources**, General
reading "This device is working properly", Manufacturer "xHCI98 Project",
Hardware version 001. No Advanced tab - the published `1.1.0.0` INF behaving
as released, this time reached by installing it rather than by inheriting it.

**It never asked for the Windows 98 CD.** Leg A's `usbui.dll` prompt (A2a,
`COPYFLG_NO_OVERWRITE` resolving a source before the skip applies) did not
recur here. The CD was attached throughout, so this says the queue was
satisfied without asking, not that an ask was answered. Why the two guests
differ is **not** established and should not be written down as if it were.

**C2, the in-place upgrade to `AFTER` - and it is the contrast this leg
existed for.** Update Driver over the *running* `BEFORE` install, pointed at
`D:\T231\AFTER`, on the same boot. On NUSB that route is fatal: the teardown
fault `0028:C00312EE` takes the upgrade's registry phase with it and the
`AddReg` is silently dropped (Leg A, A2). On SweetLow's stack it was **clean -
no crash, no error dialog, nothing unusual on screen** - and the controller's
Properties then carried **General, Advanced, Driver, Resources**, with the
Advanced tab holding an unticked "Disable USB error detection" box and a
"Bandwidth Usage" button.

**So SweetLow's stack keeps the registry phase that NUSB loses**, and on it the
line arrives through an ordinary Update Driver. That is the route the reporter
of GitHub issue 4 - who runs this stack - would actually take.

**And the tab appeared with no reboot at all.** The upgrade was not followed by
a restart; the Advanced tab was there the moment the property sheet was
re-opened on the same boot. The page is drawn from `EnumPropPages` on the
devnode key when Device Manager builds the sheet, so it needs the registry
write and not a driver reload. Nothing in this task's design depended on that,
but it is worth knowing before anyone writes "restart required" into the
release notes.

**C3, Bandwidth Usage, and issue 6 again.** Read on the same boot, with devices
added from the monitor onto root ports. **Each mouse was installed, not merely
attached**: the Add New Hardware wizard ran for both, so the operator closed
the property sheet each time to let it complete and re-opened it to press
Refresh. The figures below are therefore taken against bound HID devices, the
same condition as Leg A's A5.

| bus | System reserved |
|---|---|
| no USB devices | 10 % |
| + `usb-mouse,usb_version=1` (12 Mb/s) on root port 2 | 11 % |
| + `usb-mouse,usb_version=2` (480 Mb/s) on root port 3 | 12 % |

The same 10 / 11 / 12 ladder Leg A read on NUSB, and the same conclusion: **a
Full-Speed and a High-Speed root-port device cost the same 1 %**, which is
issue 6 visible in the user interface. It is not an NUSB artifact - it
reproduces on SweetLow's stack, under the same `usbui.dll`-computed page.

**C4, the guest wedged on removal. Recorded as unattributed.** Both mice were
removed with two `device_del`s, with **System Properties -> Device Manager open
and the controller's property sheet open on the Advanced tab** (the Bandwidth
dialog had been closed). The guest then spun in ring 0: QEMU burning a full
core (10.09 s of CPU in 10 s of wall clock), the framebuffer byte-identical
across 70 s with the taskbar clock stopped at 4:05, and `sendkey esc` having no
effect. It is **not a spinlock deadlock** - five `info registers` samples gave
EIP `c002f692`, `ff084439`, `ff03ed9f`, `ff041952`, `c0015284`, so varied
ring-0 code was executing, which reads as a storm rather than a stall.

What was removed were **two bound HID devices**, since the wizard had installed
both (C3). So this is a surprise removal of installed devices at root ports,
which does reach this driver, and the honest statement is that **the cause is
not established**. Three candidates are open and this leg separates none of
them: the 9x shell re-enumerating with Device Manager open, SweetLow's
`usbport.sys`/`usbhub20.sys` removal path, and this miniport's. The operator's
reading is the first, and Leg A is weak negative support for it - Leg A
attached the same two devices with the same window open, never removed them,
and did not wedge - but "never removed them" is exactly the untested half, so
that is a hypothesis and not a control.

**Both packages were the `release` flavour, so there is no trace of any kind
behind this**, which is why nothing above is written as a finding. What would
settle it is a `qemu`-flavour reproduction with the log channel live, once with
Device Manager open and once with it closed; that is the cheapest experiment
that splits candidate one from the other two. Until then the operational rule
stands on its own: on a 9x guest, close Device Manager before detaching a USB
device from the monitor.

The wedged guest was quit rather than shut down, which costs the image state
but nothing else - every C reading above was already captured.

**A side reading worth keeping: the `AFTER` registry write survived that
kill.** The guest was quit while spinning, never shut down, and on the next
cold boot the Advanced tab was still there. That is the opposite of the
`SYSTEM.DAT`-rollback trap `lessons.md` records for a killed 9x guest, and the
difference is timing rather than luck - the upgrade's registry phase had
completed minutes earlier and a boot had not intervened. Do not read it as
"killing a 9x guest is safe".

**A4, the `usbui.dll` rename control. The exports win, and the 2026-09-07
reading was wrong.** Taken here because this guest had what the test needs: an
Advanced tab and `C:\WINDOWS\SYSTEM\USBUI.DLL`, 147,456 B, `04-23-99 10:22p`.
Renamed to `USBUI.SAV` from "Restart in MS-DOS mode", cold booted, and both
buttons pressed:

| pressed | with `usbui.dll` present | renamed away |
|---|---|---|
| controller -> Advanced -> **Bandwidth Usage** | the Bandwidth dialog, "System reserved 10 %" | **"Data Access Error - An error occurred while trying to access the requested data."** |
| USB 2.0 Root Hub -> Power -> **Power properties** | the Power dialog | **the same "Data Access Error"** |
| the **Advanced tab** itself (the control) | General, Advanced, Driver, Resources | unchanged - tab, caption and checkbox all render |

Two independent witnesses, one of them (the root hub's) registered by
SweetLow's own `USB2.INF` and nothing to do with this package. **So the tab is
`sysclass.dll`'s and the dialogs behind the buttons are `usbui.dll`'s**,
exactly as the export and string layout predicted: `sysclass.dll` carries
`usbui.dll`, `USBControllerBandwidthPage`, `USBHubPowerPage` and the "Data
Access Error" text adjacent, which is the `LoadLibrary32W` /
`GetProcAddress32W` shape with its failure message, and the failure message is
what a missing `usbui.dll` produces.

**What this corrects.** The 2026-09-07 note recorded "the tab **and the
dialog** render identically with `usbui.dll` renamed away"; the tab half is
right and the dialog half is wrong, and the likeliest explanation is that the
button was never pressed in that session. More broadly, **this repository says
in several places that copying `usbui.dll` on 9x buys nothing, and that is now
false**: it buys both dialogs. Everything that says otherwise has to be
corrected - `src/xhci98.inf`'s own comment block above `[Xhci.AddReg]` says it
(it is quoted in "The before/after pair" above), and
`build-and-test.md`'s `usbui.dll` sections say it.

**What it does not change.** Since `1.0.2.0` the INF copies `usbui.dll` on all
four install paths, to dirid 11, so any machine that installs this package has
it and gets working dialogs. The decision needs no revisiting; what changes is
the reason recorded for it, which until now was "consistency, since it buys
nothing on 9x". It also settles the E460 question raised when the conflict was
opened: a 9x machine with `sysclass.dll` and no `usbui.dll` gets the tab and
the checkbox, and "Data Access Error" from either button - and this package's
own install is what puts `usbui.dll` there.

**`usbui.dll` was left renamed.** The standing instruction for this test is to
rename it back, and on a guest that is kept that is right; here it would have
been theatre. Every boot of this leg ran on a local work copy under the
session scratchpad, `vm/sweetlow-2a.img` was never written, and nothing was
copied back - so the copy carrying `USBUI.SAV` is discarded whole, and the
`sweetlow-stack-nodriver` snapshot in `vm/` is exactly as it was before this
leg started. A future leg gets a fresh copy of it.

### What 23.1 still owes the documents

**All three were taken on 2026-09-20, on this branch, in one commit.** The
list below is kept as it was written, because it is what was owed; what each
document now carries is:

- `docs/using/release-notes.md`: a new section, "Not in this release: the
  controller's Advanced tab", which describes the tab as the **next**
  release's and says `1.1.0.0` does not have it - the checkbox as a
  machine-wide shell setting this package never writes, the Bandwidth Usage
  figures as issue 6 arriving in the user interface, the NT half as not
  included, and the NUSB upgrade as the case that does not get the tab. The
  Known limitations entry for the Windows 98 controller-stop crash now
  carries the rename-and-cold-boot upgrade route in place of the
  right-click-`Install` remedy, and says why that remedy delivers nothing
  (`[DefaultInstall]` writes no registry value since `1.1.0.0`, and never
  wrote a devnode one); it also records that SweetLow's stack takes an
  in-place upgrade. The Insert Disk material gained leg A2's finding: a
  machine that already has the file can still be asked for it, and
  `C:\WINDOWS\SYSTEM` answers the prompt.
- `docs/issues/06-full-speed-root-port-bugcheck.md`: a new **section 5.1**,
  carrying the A5/C3/B5 ladder as one table across the three stacks, what the
  reading is not (the page's own arithmetic, not usbport's budget; deltas,
  not attributions; no behind-a-hub contrast), and B7's point that the hub's
  Power page is where a per-device witness would come from. The status
  paragraph, section 5's cosmetic-effect note, three cells of the section 1
  table, section 9's accounting item and the Sources list all point at it.
  A new section number rather than an insertion: nothing was renumbered,
  because other documents cite sections 6 and 7 by number.
- `docs/contributing/build-and-test.md`: the 9x root hub's Power tab (A0b,
  B2c, B7) and which INF registers it, as a table of the two 9x pages and
  what a missing `usbui.dll` costs each, in "The files the OS supplies";
  the `COPYFLG_NO_OVERWRITE` prompt finding with the three-leg tally, beside
  the copy-flag table; and, in "Windows ME target VM", B8 - three of the four
  files are SweetLow's and `usbccgp.sys` is Windows ME's own, both ordinary
  explanations eliminated, the mechanism not established, and what that means
  for task 18.4's composite reading.

`docs/issues/README.md`'s issue 6 row gained a clause for section 5.1, so the
index and the page agree.

- `docs/using/release-notes.md`: the Advanced tab as a feature; **and two
  corrections**. The NUSB upgrade note's remedy - "right-click `xhci98.inf` ->
  *Install* to deliver the registry values the crashed phase did not" - has
  been **empty since `1.1.0.0`**, when `[DefaultInstall]` lost its only
  `AddReg` (`Xhci.AddReg.Global`) along with the selective-suspend mechanism.
  It could never have delivered a devnode value such as `EnumPropPages`
  anyway: those live in `Xhci.AddReg`, which only a device install runs. The
  remedy that does work is Leg A's rename-and-cold-boot sequence, and it
  should replace it. Until then a Windows 98 + NUSB user upgrading into the
  release that carries this line does not get the tab.
- `docs/issues/06-full-speed-root-port-bugcheck.md`: the A5 reading - issue 6
  is now visible in the UI, in a dialog this task adds the button for, and
  B5 makes it three stacks across two operating systems rather than one
  reading. B7 adds that the hub's Power page, unlike the Bandwidth page,
  itemises devices, so it is the place a per-device witness would come from.
- `docs/contributing/build-and-test.md`: the 9x root hub has a Power tab from
  NUSB's own INF (A0b), which the `usbui.dll` sections do not say; the
  `COPYFLG_NO_OVERWRITE` prompt finding; and **the "Windows ME target VM"
  section describes the guest as running SweetLow's stack without saying that
  `usbccgp.sys` is Windows ME's own** (B8). Three of the four files are
  SweetLow's and the composite driver is not, so a Windows ME composite-device
  observation - task 18.4's composite audio device included - rests on Windows
  ME's `usbccgp.sys`. The mechanism that preserved it is not established.

---

## 23.1.5 - the NT half of the controller's property page

Status: **settled on 2026-09-20, and the line shipped.** The task was deferred
by the owner "to be settled rather than left open", and what settled it is
seven guest readings, not the two `PROP-NTHALF` asked for: the owner widened
the task mid-run to Windows 7 x86 and every 64-bit target.

| guest | NT | install section | miniport version | Advanced tab | pair written by |
|---|---|---|---|---|---|
| Windows 2000 SP4, xHCI-only | 5.0 | `[Xhci.Dev.NTx86]` | 200 | **yes** | hand |
| Windows XP SP3, 32-bit | 5.1 | `[Xhci.Dev.NTx86]` | 200 | **yes** | hand |
| Windows XP x64 SP2 | 5.2 | `[Xhci.Dev.NTamd64]` | 200 | **yes** | the INF |
| Windows Vista SP2 x86 | 6.0 | `[Xhci.Dev6.NTx86]` | 300 | **yes** | the INF |
| Windows Vista SP2 x64 | 6.0 | `[Xhci.Dev6.NTamd64]` | 300 | **yes** | the INF |
| Windows 7 x86 | 6.1 | `[Xhci.Dev6.NTx86]` | 300 | **yes** | the INF |
| Windows 7 x64 | 6.1 | `[Xhci.Dev6.NTamd64]` | 300 | **yes** | the INF |

**Every NT target this project has shows the tab, and all four install
sections of both INFs were read.** `[Xhci.AddReg.NT]` is reached through
`[Xhci.Dev.NTx86]` and `[Xhci.Dev6.NTx86]` in `src/xhci98.inf` and through
`[Xhci.Dev.NTamd64]` and `[Xhci.Dev6.NTamd64]` in `src/xhci98-amd64.inf`; each
of the four was exercised by at least one guest. The amd64 file, which the
first draft of this change shipped with a comment saying no x64 reading stood
behind it, now has three.

### The finding that matters beyond this task

**On NT 6.x, writing `EnumPropPages32` by hand into an existing devnode does
nothing, and looks exactly like the line not working.**

The roadmap's method for this task was the one that took the 9x line on
2026-09-07: write the value by hand in regedit first, look, and only touch the
INF if it passes. On Windows 2000 and Windows XP that works - the tab appears
the moment the property sheet is re-opened, with Device Manager still running
and no restart. **On Windows Vista it does not.** A hand-written pair on an
already-installed devnode produced no tab at all: not on re-opening the sheet,
not with Device Manager closed and restarted, not after a full reboot with the
values confirmed still present. The same pair, written by the INF at install
time on the same guest from the same clean snapshot, produces the tab.

So NT 6.x consults the provider list when the devnode is built, and a value
added afterwards is never read. Nothing reports this: there is no error, no
log line and no "Data Access Error" - just a sheet with one fewer tab, which
is indistinguishable from a line that does not work.

**This task nearly shipped the wrong conclusion on it.** Vista was recorded as
a genuine negative, with a control that made it look airtight - Microsoft's own
EHCI controller, hot-plugged into the same guest on the same boot, carrying the
identical pair from its own `usbport.inf`, drew its page while ours did not.
That control was sound and its reading was true; it simply could not see the
variable that mattered, because the in-box EHCI driver had its pair written by
`usbport.inf` **at install time** and ours did not. The INF change had already
been written, gated and committed to a comment block asserting "its reach is
NT 5.x" before the owner asked for Windows 7 and the 64-bit targets, and
Windows 7 x86 - same install section as Vista, same Version 300 path - showed
the tab, which is what broke the false conclusion open.

`docs/contributing/lessons.md` carries this; `src/xhci98.inf`'s block above
`[Xhci.AddReg.NT]` warns against re-verifying the line by hand on NT 6.x.

### What the page is, per generation

Two shapes, and nothing in this package depends on either:

- **NT 5.x** (Windows 2000, XP, XP x64): bandwidth text, a
  "Bandwidth-consuming devices" list reading **System reserved 10%**, a
  Refresh button, and ONE checkbox - "Disable USB error detection" on Windows
  2000, renamed "Don't tell me about USB errors" from XP on.
- **NT 6.x** (Vista, Vista x64, Windows 7, Windows 7 x64): the same, reading
  **System reserved 20%**, and TWO checkboxes - "Tell me if my device can
  perform faster" (ticked by default) above "Don't tell me about USB errors".

The 9x page is a third shape again: there the Advanced tab carried a
**Bandwidth Usage button** opening a separate dialog, because `sysclass.dll`
drew the tab and `usbui.dll` only the dialog behind it (23.1 leg A4). On NT
`usbui.dll` draws the whole page, so there is no button and the list is inline.

The checkbox writes `ErrorCheckingEnabled` REG_DWORD under
`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Usb` - **0** ticked, **1**
unticked, persisting either way rather than deleting the value - the same name,
key and polarity the three 9x legs recorded. Read on Windows 2000 (both
directions) and Windows XP (ticked only).

### Issue 6 in the user interface, on Microsoft's own stacks

The bandwidth ladder was taken on the two NT 5.x 32-bit guests, by adding
devices from the monitor onto root ports and pressing Refresh:

| bus | Windows 2000 | Windows XP |
|---|---|---|
| no USB devices | 10 % | 10 % |
| + `usb-mouse,usb_version=1` (12 Mb/s) on root port 2 | 11 % | 11 % |
| + `usb-mouse,usb_version=2` (480 Mb/s) on root port 3 | 12 % | 12 % |

Digit for digit the ladder all three 9x legs gave. **A Full-Speed device and a
High-Speed device on root ports cost the same 1 %**, and Windows 2000 SP4's is
Microsoft's own native NT 5.0 `usbport.sys` and `usbui.dll` - so the reading
can no longer be explained as an artifact of a back-ported stack (NUSB's
Win2000-derived build or SweetLow's XP-derived rebuild). Issue 6 in the user
interface is now **five stacks across four operating systems**.

Both NT 5.x guests also logged the driver's own witness to the same event,
which the 9x legs had no equivalent of:

```
xhci98: slot: endpoint speed differs from the port's, usbport << 8 | decoded=00000302
xhci98: endpoint speed mismatches=00000001
```

usbport reporting speed 3 where the driver decoded 2, **exactly one mismatch**,
for the Full-Speed mouse and not the High-Speed one, with the identical
encoding on both guests. A `runtime` reading.

The same two limits hold as on 9x: the page **never itemises** - only the
"System reserved" row exists and only its percentage moves, so the per-device
figure is a delta across three readings and not an attribution the page makes -
and the behind-a-hub contrast was not taken.

### Which of the two values does the work

**`EnumPropPages32` alone is load-bearing.** On Windows 2000, with the guest
snapshotted, `Controller` was deleted and the sheet re-opened: the Advanced tab
survived and still read its bandwidth figure live. Both lines ship because all
three NT references write both and this INF follows them, not because the pair
is indivisible.

### The vehicles, and one correction to the handoff

All seven guests were reverted to their clean-install snapshots first. The
package is the **`qemu`** flavour throughout - the leg is not an
install-fidelity reading, so the debug console costs nothing and buys a
witness, which is the other half of C4's lesson.

| guest | image | launcher | monitor |
|---|---|---|---|
| Windows 2000 SP4, xHCI-only | `vm\win2k-xonly.img` | `qemu-win2k-xonly-run.cmd` | 55560 |
| Windows XP SP3 | `vm\winxp.img` | `qemu-winxp-run.cmd` | 55559 |
| Windows XP x64 SP2 | `vm\winxp64.img` | `qemu-winxp64-run.cmd` | 55700 |
| Windows Vista SP2 x86 | `vm\vista.img` | `qemu-vista-run.cmd` | **55465** |
| Windows Vista SP2 x64 | `vm\vista-x64.img` | `qemu-vista-x64-run.cmd` | 55563 |
| Windows 7 x86 | `vm\win7.img` | `qemu-win7-run.cmd` | 55466 |
| Windows 7 x64 | `vm\win7-x64.img` | `qemu-win7-x64-run.cmd` | 55564 |

**Vista's monitor is 55465 on this host, not the 55565 the handoff and
`build-and-test.md` both give.** That is not an error in either: 55565/55566 is
what `setup-qemu-vista.ps1` generates by default, and `build-and-test.md`'s own
monitor-port section records why this host differs - on 2026-09-13 the excluded
ranges swallowed every port from 55555 to 55564 and only 55465 and 55466 were
left bindable, so these launchers were regenerated onto them. `scripts\local\`
is git-ignored per-host tooling and the J3 rule is to regenerate rather than
hand-edit, so a host's launcher legitimately differs from the generated
default. **Read the port out of the launcher you are about to run**, not out of
a document.

Two mechanics worth carrying forward:

- **`savevm` is refused on the WHPX guests** - "State blocked due to missing
  dirty memory tracking support" - which is why every pre-existing snapshot on
  `vm\winxp.img` has `VM_SIZE 0`. Those guests take a disk-only
  `qemu-img snapshot -c` after a clean shutdown instead.
- **The x64 NT 6.x guests need the signature-enforcement boot every time.**
  `build-and-test.md` records F8 as the route and `sendkey f8` every 200 ms for
  about 30 seconds as what works (Windows 7 x64, 2026-09-18), with a single
  late `sendkey f8` too late; Vista x64 is separately recorded as not yielding
  to the spam at all. **This task used a third route on both guests and it
  needed no timing**: an elevated `bcdedit /set {current} advancedoptions true`
  and a reboot, after which the Advanced Boot Options menu comes up on every
  start and waits. It is an addition to F8, not a replacement for it, and it
  costs one reboot to set up. Before that boot both guests install
  the package and then refuse to load it - **Vista x64 shows Code 39** with a
  Program Compatibility Assistant box saying "Windows requires a digitally
  signed driver", **Windows 7 x64 shows Code 52** from the wizard itself. The
  install still writes the devnode values, which is why the reading afterwards
  is of an INF-written pair.

### Leg W - Windows 2000 SP4, xHCI-only: PASSES

Unlike every 23.1 leg this one needed an **install leg first**, because the
driver key the experiment writes into does not exist until the controller is
bound. The guest auto-logged in with the Found New Hardware Wizard already up,
and the driver installed from the transfer drive: `DriverEntry (built Sep 20
2026 10:33:35)`, `USBPORT_RegisterUSBPortDriver status=00000000`,
`cb StartController`, `No Op submit status=00000000`,
`root hub: managed ports=00000004`, and the `RH_*` family answered.

**One difference from task 19.5 worth stating rather than smoothing over.**
19.5's install (2026-09-03, Have Disk from the same transfer drive) asked for
no reboot. This one raised Windows 2000's "System Settings Change" box,
although the driver was already registered, started and polling when it
appeared. The two installs differed in route - the wizard's search path here,
Have Disk there - and which fact explains the box was **not** established. The
restart was taken and the driver came up again on the boot path, so nothing
rests on it; it is recorded because "no reboot" is a claim a release note could
inherit.

**W1, the baseline.** The controller's Properties carried **General, Driver,
Resources** and no Advanced tab; "This device is working properly",
Manufacturer "xHCI98 Project", Location 2 (PCI bus 0, device 3, function 0).
Snapshotted as `win2k-xonly-before-installed`.

**W2, the control.** The USB 2.0 Root Hub carried **General, Power, Driver,
Power Management**, and the Power tab rendered live: "The hub is self powered",
"Total power available: 500 mA per port", "4 port(s) available. 0 mA" - four,
matching the driver's own `root hub: managed ports=00000004`. Windows' own
`USB.INF` registers `usbui.dll,USBHubPropPageProvider` there and this package
registers nothing, so the control says what it is meant to: `usbui.dll` is
present and its provider mechanism works here. Green **before** the experiment,
which is the order A4's lesson asks for.

**The driver key.** `HKLM\SYSTEM\CurrentControlSet\Control\Class\`
`{36FC9E60-C465-11CF-8056-444553540000}\0000`, holding `DriverDesc` = "USB 2.0
eXtensible Host Controller (xhci98)", `DriverVersion` 1.1.0.0, `InfPath`
`oem0.inf`, `InfSection` `Xhci.Dev`, **`InfSectionExt` `.NTx86`**,
`MatchingDeviceId` `pci\cc_0c0330`, `ProviderName` "Yeo Kheng Meng", and this
package's three DWORDs. `0001` is the root hub.

**W3, the reading.** The pair was written by hand and, with Device Manager
still running and no restart, re-opening the sheet gave **General, Advanced,
Driver, Resources**.

**A trap in the writing, caught by the operator and not by the agent.** The
string value was first created as `EnumPropPages` - the 9x name, without the
`32` - which would have produced a false negative. See leg V for the second,
worse instance of the same class of error.

**W4, every control pressed** (A4's lesson - a control called unaffected
without being operated cost four documents two weeks):

- **Refresh** with nothing changed: no visible change, no error. With a device
  newly attached the figure moves, so the button works and the page is live.
- **Selecting the "System reserved" row**: the row highlights and the Bandwidth
  indicator bar does **not** visibly respond, though it carries one segment
  throughout. With a single row there is nothing to distinguish; recorded as
  read, not as a defect.
- **The checkbox**: ticked wrote `ErrorCheckingEnabled` 0, re-opening showed it
  still ticked, unticking wrote 1.

**W5, the bandwidth ladder and the driver's witness**: see the shared sections
above. **W6:** `Controller` deleted, tab survived.

Snapshots left: `win2k-xonly-before-installed` and
`win2k-xonly-23-1-5-after-tab`.

### Leg X - Windows XP SP3, 32-bit: PASSES

The roadmap called this one "nearly free if a guest is already warm ... the
middle case between the two"; no guest was warm and it was taken anyway,
because Vista's apparent failure made it the leg that placed the boundary.
Installed from `E:\` through *Install from a list or specific location*, XP's
"has not passed Windows Logo testing" warning answered Continue Anyway, **no
restart asked for**, `interface version presented=000000C8` - 200, the NT 5.x
path.

Baseline **General, Driver, Details, Resources**; after the write, **General,
Advanced, Driver, Details, Resources** at the first re-open with Device Manager
still running. Page as the NT 5.x shape above. Ladder 10 / 11 / 12 %.
`ErrorCheckingEnabled` 0 after ticking; the untick-writes-1 half was not taken
here.

**How this leg was driven, because it changes what a reading costs.** The pair
was written with `reg.exe` from a command prompt and read back with
`reg query`. A value written and then queried back **in a console font** cannot
hide a comma that is really a full stop, and it needs no magnification step.
Prefer it on any guest that has `reg.exe`, which is every NT target here.

No live snapshot: `savevm` is refused under WHPX. Disk-only
`winxp-23-1-5-after-tab` taken after a clean shutdown.

### Leg V - Windows Vista SP2 x86: the leg that was wrong, then right

**This leg was taken twice and only the second reading counts.**

**V-first, the hand-written pass, which FAILED.** From `vista-clean-install`,
the old package installed (no `EnumPropPages32` in it), **no restart asked
for**, `interface version presented=0000012C` - 300, the NT 6.x path.
Baseline: **General, Driver, Details, Resources**. The control was green: the
root hub - Vista names it plainly "USB Root Hub" - carried **General, Power,
Advanced, Driver, Details, Power Management**, its Power tab reading "The hub
is self-powered", "Total power available: 500 mA per port", "4 port(s)
available / 0 mA", and its Advanced tab **"Hub is operating at high-speed"**
with a Reset Hub button (not pressed: resetting the hub is a live operation).
That high-speed line is worth keeping beside issue 6.

The pair was then written by hand into
`...{36FC9E60-...}\0000` (`InfPath` `oem3.inf`, **`InfSection` `Xhci.Dev6`**,
`InfSectionExt` `.NTx86`) and read at three points - sheet re-opened, Device
Manager restarted, machine restarted with the values confirmed still present.
**No tab at any of them.**

**The control that made it look airtight.** An EHCI controller was hot-plugged
from the monitor (`device_add usb-ehci`, Intel 8086:24cd). Vista installed its
own in-box driver and the resulting "Intel(R) 82801DB/DBM USB 2.0 Enhanced Host
Controller" **did** get its Advanced tab, reading "System reserved 20 %". And
Vista's own `C:\Windows\inf\usbport.inf`, opened in the guest, reads verbatim:

```ini
[EHCI.Dev.NT]
AddReg=EHCI.AddReg.NT
...
[EHCI.AddReg.NT]
HKR,,EnumPropPages32,,"usbui.dll,USBControllerPropPageProvider"
HKR,,Controller,1,01
```

which upgrades the one reference this project had read only statically - and
flagged as uncertain, the file being UTF-16 - to a `runtime` reading of the
shipped file on the running system. So the pair was right, the provider worked,
and ours was refused.

**V-first also carried a typo, and the typo was real.** The first write was
`usbui.dll` **`.`** `USBControllerPropPageProvider` - a full stop where the
separator must be a comma - and steps (a), (b) and (c) including a restart were
all run against it before the operator caught it. Those readings are void. The
re-run with a verified comma still showed no tab, which is what made the
negative look solid. The verification method that settles this class of error:
crop the value row out of the screendump and magnify it, or - better - write it
with `reg.exe` and query it back. At the guest's 800x600 a comma and a full
stop differ by one pixel below the baseline, and **a value read off a screen at
guest resolution has not been read**.

**V-second, the INF-written pass, which SUCCEEDED.** After Windows 7 x86 showed
the tab from the same install section and the same Version 300 path, the guest
was reverted to `vista-clean-install` and the **new** package installed -
identical pair, written by the INF. The controller's Properties then carried
**General, Advanced, Driver, Details, Resources**, the page reading "System
reserved 20 %" with both NT 6.x checkboxes.

One variable changed between the two readings: **who wrote the value.** See
"The finding that matters beyond this task" above.

Snapshots left: `vista-before-installed`, `vista-23-1-5-no-tab` (the
hand-written state, kept because it is the evidence for the finding) and
`vista-23-1-5-inf-written-tab`.

### Leg XP64 - Windows XP x64 SP2: PASSES

The first reading ever taken behind the amd64 INF's property-page line.
Installed the amd64 package from `E:\` through `[Xhci.Dev.NTamd64]`, Continue
Anyway at the Logo warning, no restart, `interface version presented=000000C8`
- 200. Properties carried **General, Advanced, Driver, Details, Resources**,
the page reading **System reserved 10%** with a Refresh button and the single
"Don't tell me about USB errors" box - the NT 5.x shape, from the INF rather
than by hand. Disk-only snapshot `winxp64-23-1-5-after-tab`.

Server 2003 x64 rests on its identity with XP x64, as everything else about
that tier does.

### Leg W7 - Windows 7 x86: PASSES, and it is the leg that broke the false conclusion

Installed through Device Manager's Update Driver Software from `E:\`, "Windows
can't verify the publisher" answered Install anyway, root hub installed in the
same pass, **no restart asked for**, `interface version presented=0000012C` -
300, the same NT 6.x path Vista takes and reached through the same
`[Xhci.Dev6.NTx86]` section. Properties carried **General, Advanced, Driver,
Details, Resources**, the page reading **System reserved 20%** with both NT 6.x
checkboxes.

At the time this was taken, Vista was on record as a negative and the INF
comment asserted the line's reach was NT 5.x. Windows 7 shares Vista's install
section, its registration path and its generation, so the tab appearing here
made that account impossible and sent leg V back to the guest.

Disk-only snapshot `win7-23-1-5-after-tab`.

### Legs V64 and W764 - Vista x64 and Windows 7 x64: both PASS

Both installed the amd64 package through `[Xhci.Dev6.NTamd64]`, both refused to
load it until the signature-enforcement boot (Code 39 with a PCA box on Vista
x64, Code 52 from the wizard on Windows 7 x64), and both came up on the
`bcdedit advancedoptions` boot with `DriverEntry`, `interface version
presented=0000012C`, registration status 0, `No Op submit status=00000000` and
`root hub: managed ports=00000004`.

Both then carried **General, Advanced, Driver, Details, Resources** with
**System reserved 20%** and both NT 6.x checkboxes - the NT 6.x shape, on the
second binary, through the second INF.

### What landed, and what it owes

The INF work the roadmap made conditional on a passing reading was done on the
same day and on the same branch:

- **Both INFs carry the pair**, in `[Xhci.AddReg.NT]`, added in one change
  because the gate's rule is that they may not drift. `src/xhci98.inf`'s block
  above that section carries the seven readings, the NT 5.x / NT 6.x page
  shapes, the `EnumPropPages32`-alone finding, and the warning against
  re-verifying the line by hand on NT 6.x. `src/xhci98-amd64.inf` carries its
  own x64 readings in place of the "no x64 reading stands behind this file"
  caveat the first draft of this change shipped with.
- **`PROP-NTHALF` was inverted rather than deleted**, which is what "taking
  it" was defined to look like. The rule that refused `EnumPropPages32` is
  gone; in its place `$propPages` is a table keyed by install-path kind, so
  the existing `PROP-MISSING` / `PROP-DUP` / `PROP-SUBKEY` / `PROP-FLAGS` /
  `PROP-PROVIDER` / `PROP-STRAY` checks run over the NT half exactly as they
  ran over the 9x half, and a new `PROP-CTRL*` family holds `Controller` to
  REG_BINARY, flags 1, data 01.
- **Six self-test cases replaced the one** `proppage-nt-half-taken` case:
  `proppage-missing-nt`, `proppage-sysclass-provider-on-nt`,
  `proppage-nt-hub-entrypoint`, `proppage-controller-missing`,
  `proppage-controller-flags`, `proppage-controller-data`. The suite is 546
  checks and green.
- **Both footprints were regenerated** (`expected-footprint.txt`,
  `expected-footprint-amd64.txt`), which is the uninstall expectation task
  11-V.3 keeps, and `build-driver.cmd` was re-run: both 32-bit flavours build,
  the import gate passes, both INF gates pass.

Still owed, and 23.7's to carry unless taken sooner:

- **`docs/using/release-notes.md`.** The "Not in this release: the controller's
  Advanced tab" section 23.1 wrote says the NT systems are not included. That
  is now false: the tab is the next release's on **every** target this package
  installs on, 9x and NT alike. The x64 half needs its usual qualification -
  the tab is there, but so is the F8 boot.
- **`docs/contributing/lessons.md`**, the NT 6.x by-hand trap, which is the
  reusable part of this task.
- **`docs/contributing/build-and-test.md`**: the NT root hub's own pages (the
  Vista hub's Power AND Advanced tabs) beside the 9x ones 23.1 added, and the
  two mechanics above - `savevm` under WHPX, and the `bcdedit advancedoptions`
  route to the x64 signature boot, which is better than the F8 spam the
  existing text implies.
- **`docs/issues/06`** section 5.1's ladder table, which currently reads three
  stacks across two operating systems and is now five across four, with
  Windows 2000's being Microsoft's own native stack - the clause that removes
  the back-ported-stack explanation.
