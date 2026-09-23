# Phase 23 Record - GitHub issue 4's open requests, and the moderation experiment

The detail behind `docs/contributing/roadmap-phases-17-on.md`, "Phase 23 -
Controller Property Page and Interrupt Moderation" (titled "GitHub Issue 4's
Open Requests, and the Interrupt Moderation Experiment" until the owner moved
the issue's polling rates and true speeds to Phase 24 on 2026-09-22). The
roadmap entry carries the goal, the status, the task table and the checkpoint;
this file carries what each task did and what each reading said. Where the two
disagree about a clause, the roadmap wins.

**Written while the phase is open**, and kept as written rather than rewritten
in the past tense. A sentence saying something "is owed" describes the day it
was written; the roadmap's task table says how each task closed.

**On `out\...` and `vm\...` paths in this file.** They say where a reading was
taken and what the file was called, on the host that ran it; they are not
files a clone has.

**On task ids inside artifact names.** The phase's tasks were renumbered on
2026-09-22 (`7488f13`): 23.1.5 became 23.2, 23.2 became 23.3, 23.3 became
23.4 and 23.3.5 became 23.5. This file cites every task by its new id, but the
snapshots, markers, branches and `out\` directories named at the time keep
the old one - `*-23-1-5-*` is a 23.2 snapshot, `..._TASK_23_2` is 23.3's
marker, `out\bench-23.2\` is 23.3's package and branch `23.3` carried 23.4.

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
task's: it is deferred to roadmap task 23.2. What remains of task 23.1 is
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
mechanism.** It is a lead for the `COPYFLG_NO_OVERWRITE` finding owed to 23.6,
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

## 23.2 - the NT half of the controller's property page

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

**The documents were all taken on this branch rather than left to 23.6**, in
the order the list below was written. Three of the four were corrections
rather than additions - 23.2 turned statements in each from incomplete into
false - which is why none of them waited:

- **`docs/using/release-notes.md`** said "Windows 2000, Windows XP, Windows
  Vista and Windows 7 are not included. Those systems draw the same page
  through a different provider, and it has not been opened against this
  controller on any of them yet." Both sentences were false. It now describes
  the tab as the next release's on **every** target, gives the NT page's
  different layout and its two generations of checkbox, and keeps the x64
  qualification: the tab is there, and so is the signature boot.
- **`docs/contributing/lessons.md`** gained the NT 6.x by-hand trap, which is
  the reusable part of this task.
- **`docs/contributing/build-and-test.md`** said the NT half was "not taken"
  and that `PROP-NTHALF` "holds that half shut", and that F8 "is the only
  route". It now carries the seven readings, the NT root hub's own pages
  (Vista's hub has an Advanced tab of its own, "Hub is operating at
  high-speed"), the `savevm`-under-WHPX refusal, and
  `bcdedit /set {current} advancedoptions true` as a third signature-boot
  route needing no timing - an addition to F8, not a replacement.
- **`docs/issues/06` section 5.1** read "Three legs, three USB 2.0 stacks, two
  operating systems" and said this package "registers no controller property
  page" on any NT target. It now carries the five-column ladder, the argument
  Windows 2000 closes (the three 9x stacks are all back-ported; NT 5.x is
  Microsoft's own native one, so no back-ported-stack explanation survives),
  and the `endpoint speed mismatches=00000001` counter as a second witness.
  `docs/issues/README.md`'s issue 6 row moved with it.

23.6 therefore inherits nothing from 23.2.

---

## 23.3 - the moderation experiment (the owner's measurement)

Status as of 2026-09-22: **DONE, and it measured.** The reading was taken on
the night of 2026-09-21 on the ThinkPad P14s Gen 1 under Windows 98 SE, not on
the E460 the roadmap named (the owner's call, after one E460 control boot). At
IMODI 200 or 160, mass-storage reads ran at 33.1 MB/s against 17.6 at the
hardware default of 4000, with the two control boots 0.4% apart; 1000 gave
29.8. So the stop rule does not fire and 23.4 is taken. The owner set its
shape the same night: the INF writes **1000**, the driver falls back to 4000,
and one isochronous pass at 1000 on bare metal gates the release. *(The next
day, after 23.5 measured 500, the owner moved the INF's value to **500**;
the fallback and the gate are unchanged, the gate now read at 500.)* The reading
and its limits are under "Results" below.

Until 2026-09-21 this section said the host side was done and the reading
owed; the paragraphs from here to "Results" were written then, and are
corrected in place where the bench proved them wrong.

### The read-first question: does QEMU's xHC model IMODI at all?

**No. It stores the register, returns it, migrates it, and never consults
it.** That was the roadmap's gate on whether a guest leg is worth taking, and
it fails it.

Read in QEMU's own source at tag `v11.1.0`, and again at `master`
`c1c18d1e640b64292859ce9f30f3c344edfb0294` (2026-09-19) - identical, to the
line number. `imod` appears in exactly five places in the whole of `hw/usb/`:

| Site | What it is |
|---|---|
| `hw/usb/hcd-xhci.h:154` | `uint32_t imod;` - the field, in `XHCIInterrupter` |
| `hw/usb/hcd-xhci.c:2730` | `xhci->intr[i].imod = 0;` - controller reset |
| `hw/usb/hcd-xhci.c:3063` | `ret = intr->imod;` - the runtime-register read |
| `hw/usb/hcd-xhci.c:3120` | `intr->imod = val;` - the runtime-register write |
| `hw/usb/hcd-xhci.c:3597` | `VMSTATE_UINT32(imod, XHCIInterrupter)` - migration |

Nothing else in `hw/usb/` names it, and the string `moderat` appears in
neither file. There is no timer, no comparison, no deferral: an event that
would raise an interrupt raises it, whatever IMODI says. Re-derive with a
blobless sparse clone of `hw/usb` and `grep -n imod hw/usb/hcd-xhci.c
hw/usb/hcd-xhci.h`; `external/README.md` carries the row.

**Two things fall out of that reading which the task did not go looking for.**

**QEMU resets IMOD to 0, where hardware resets it to 4000.** The spec's reset
default is 4000, 1 ms (Table 5-39, p.392), and `hcd-xhci.c:2730` writes 0. The
start has never written the register, so this is not a divergence the driver
causes or can see - but it does mean **every guest run this project has ever
taken ran with IMOD 0 and every bare-metal run with 4000**, which are the two
ends of the interval this task is about. That is worth holding onto beyond
this task: wherever a guest reading and a metal reading of the same workload
have disagreed about interrupt or DPC counts, moderation is a difference
between them that no run sheet has named.

**It also means the save/restore path's IMOD write has only ever been
exercised with 0.** `xhciRestoreState` writes back what the save read
(`src/xhci_init.c`), which the 2026-09-05 audit's F10 changed from a literal
0 precisely so that moderation survives a resume. The fix is right and it is
still unverified against a nonzero IMOD, because no guest can supply one: in
QEMU the saved value is 0 and the corrected code writes 0, which is what the
defect did. An experimental build with a value set is the first thing that
can tell those two apart, and a suspend/resume cycle is worth adding to the
bench sheet for that reason alone.

**And the guest read-back proves less than it looks.** `intr->imod = val;`
stores the whole 32-bit word with no mask - IMODI is not separated from IMODC
- so a write-then-read agreeing in a guest says the value reached a variable
and nothing more. It is still worth one guest leg as a smoke test of the
experimental build's path end to end (does the value arrive from the registry,
does the start write it, do the notes come out of `XHCISNAP`), and it is worth
nothing at all as evidence about an interval.

### What landed: the experimental build

The roadmap asked for "an experimental build that writes IMOD in the start
after the interrupter is programmed". It is built, and the owner's decision of
2026-09-20 settled the one open question in how: **the value comes from the
registry, not from a compile-time constant**, so that one binary sweeps the
whole ladder. The alternative was a `-D` per value, which is seven rebuilds
and seven copies onto a Windows 98 machine for a sweep that otherwise takes
seven `regedit` edits and seven controller restarts.

**Nothing of it is in a shipping binary.** The whole change is behind
`XHCI_IMOD_EXPERIMENT`, which only `XHCI_EXTRA_DEFINES` sets, and `src/sources`
turns any nonempty `XHCI_EXTRA_DEFINES` into `XHCI_DIAGNOSTIC_BUILD` - the
marker `make-package.ps1` refuses to package. Built both ways on 2026-09-20:
`scripts\build-driver.cmd both` reports `BUILD + GATES PASSED`, and with the
define set it reports `PROBE BUILD + GATES PASSED` and the do-not-deploy
warning. **The import gate lists the same twelve pairs in the binary either
way** (of the allowlist's thirteen; the thirteenth, `HAL.dll!WRITE_PORT_UCHAR`,
is the `qemu` flavour's alone), which is
the "no new import" clause 23.4 also has to keep: the registry read goes
through `UsbPortGetMiniportRegistryKeyValue` in usbport's own packet, not
through an import of ours.

The pieces, all `#ifdef`-ed:

- **`XhciImodInterval250ns`** (named `XhciImodExperiment` until 2026-09-21;
  see below), a `REG_DWORD` in the driver's own software key -
  the same key and the same service as `XhciLogVerbosity`, read once per start
  at PASSIVE from `xhciStartController`, immediately after `xhciLogStart`.
- **The write**, at the end of `xhciProgramEventRing` in `src/xhci_init.c`:
  after ERSTSZ, ERDP and ERSTBA, and before R/S is set in `xhciRunController`,
  so the interval is in force from the first event the controller posts. A
  plain `XhciWriteIr0` with no read-modify-write, because IMOD has no reserved
  field (Table 5-39, p.392), and writing the word whole leaves IMODC 0.
- **Six fields** at the end of `XHCI_EXTENSION`, after `Log` and before the
  trailing pair, so an experimental build moves no existing field's offset and
  a `scripts\local\offsets.txt` taken from a shipping build still reads every
  counter out of an experimental one.

  *(True of the offsets and false of the tool, found on the bench on
  2026-09-21. `readsnap.py` checks the dump's size against the table's
  `SIZEOF` and refuses a mismatch - 92,328 against 92,304 - so a shipping
  table decodes nothing out of these dumps, and it lists no `ImodExperiment`
  field to read even if it did. The sweep was decoded against a table built
  with the define: `scripts\local\offsets.c` compiled with
  `/DXHCI_IMOD_EXPERIMENT` on the `cl` line and the six `ImodExperiment` rows
  under the same `#ifdef`, into a separate file passed with `--offsets`.
  `build-and-test.md` carried that recipe in a staging section until the
  experimental build was retired on 2026-09-22; what stands there now is "The
  moderation experiment package (task 23.3) - retired".)*
- **Five `XHCISNAP` notes**, and `imod.exp.readback` and `imod.exp.written`
  also in the always-on counter block beside `isr.entries` and `dpc.count`, so
  one flush carries the arm of the sweep and its effect together.

  *(Only on a DebugView flush. The counter block is composed into the ring by
  the flush, and the flush returns early unless `XhciLogDebugView` selects the
  sink, so a machine read with `XHCISNAP` alone never has these four lines in
  its `.TXT` - the first E460 control dump had none, and looked broken for
  it. They are in the `.BIN`, as `InterruptCount`, `DpcCount`,
  `ImodExperimentWritten` and `ImodExperimentReadback`. The `.TXT` does carry
  `imod.exp.status` and `imod.exp.value`, which are start notes, and those
  are what the operator checks on the machine.)*

Three decisions in it that are the measurement's rather than taste:

**The name is deliberately not 23.4's.** 23.4 proposes `XhciImodInterval` for
a value that would ship, with a contract this one has not got - default 4000,
floor 10, 4000 substituted for anything invalid. If the two shared a name, a
bench machine left carrying an experimental setting would have it become
load-bearing the moment a 23.4 build was installed over the top. Under a
separate name a leftover is inert.

*(Reversed by the owner on 2026-09-21. The experiment now reads
`XhciImodInterval250ns`, which is also the name 23.4 will ship if it is taken,
with the unit in the name so 4000 cannot be misread as microseconds. The same
day the owner set 23.4's accepted range to 10 to 4000, with 4000 substituted
for anything outside it, in place of 10 to 65535. The cost is the inertness
argued above: under a 23.4 build a leftover 0 falls back to 4000, but a
leftover 10 to 4000 stays in force, so the bench helpers now say to clear the
value when a session ends. The experimental build itself still accepts 0 to
65535, because the sweep has to reach 0 and 10.)*

**Absent means the start writes no IMOD at all** - which is exactly the driver
as it shipped. So the experimental binary is its own control: the same `.sys`
with the value unset is the 4000 arm of the sweep, and no rebuild separates
the baseline from the readings. A missing value, a failed read and a NULL
service in the packet all land there, and none of them can fail a start.

**A value IMODI cannot carry is refused, not clamped.** IMODI is bits 15:0
and IMODC is 31:16, so writing a larger value whole would set an interrupt
*counter* while the run sheet recorded an interval, and masking would write
4464 for a typed 70000 and look like it worked. Refusing leaves the register
alone and `imod.exp.refused` at 1, so the reading is visibly absent instead of
quietly wrong. **Zero is not refused**: it is moderation off, the far end of
the sweep, and `imod.exp.status` is what tells a deliberate 0 from a machine
that has no value set.

### The sweep, when the bench is to hand

Written before the bench and left as written; **"How it was actually run",
after it, says what the night changed**, and where the two disagree that
section is the one that was measured.

Build and package once:

```
set XHCI_EXTRA_DEFINES=-DXHCI_IMOD_EXPERIMENT
scripts\build-driver.cmd release
powershell -File scripts\package\make-package.ps1 -Flavor release -Arch x86 -ImodExperimentArtifact
```

The `release` flavour, not `qemu` - this is bare metal, and `qemu`'s
port-`0xE9` mirror must never leave the emulator.

**`-ImodExperimentArtifact` is new, and it is the second exception to the
packager's do-not-deploy rule.** It exists because the experiment's machine is
a **clean** Windows 98 SE install: there is no driver on it for a `ren` +
`copy` binary swap to replace, and an INF install is the only thing that
creates the devnode and the software key the value is read out of. The switch
is narrow the same way task 12.3's is, by construction rather than by
intention - it admits only an image carrying
`XHCI98_IMOD_EXPERIMENT_ARTIFACT_TASK_23_2`, which only `-DXHCI_IMOD_EXPERIMENT`
emits, **and** the do-not-deploy marker, so an artifact that had lost the
second one could not sneak in behind the first. The two switches key on
different markers and neither stages the other's build, which the packager's
self-tests drive as five cases. `make-release.ps1` is unchanged and still
refuses this binary on the do-not-deploy marker, as it always did.

Built alone, and refused in the three places 12.3's is: `scripts\build-driver.cmd`
before the DDK is even located, `src\sources` as a string comparison that binds
a bare `build` from a DDK prompt, and `src\xhci_dispatch.c` as an `#error` for
the two combinations this tree can name.

**Unlike 12.3's artifact this one is not a broken driver.** With the value
unset it writes no IMOD and behaves exactly as the shipping build, so the one
install is both the control and every arm of the sweep. It is still a bench
instrument and must never be published.

**A clean Windows 98 SE needs NUSB 3.3 (or SweetLow's stack) installed first**,
because it has no `usbport.sys` of its own; without it this package installs
and the device shows `Code 2`. Then Device Manager, the unclaimed xHCI
controller, Update Driver, Specify a location, the package directory. On an
xHCI-only machine the copy phase asks for the Windows 98 SE CD, since
`usbd.sys`, `usbhub.sys` and `usbui.dll` come from the OS's own source through
`LayoutFile` and nothing on that machine ever placed them.

**Install once, then only reboots.** Under NUSB, disabling, removing or
upgrading this driver in Device Manager blue-screens the machine at
`0028:C00312EE` (release acceptance test, 7.1). The sweep needs none of the
three: it changes a registry value and reboots, which is exactly why the value
was put in the registry rather than in a compile-time constant.

Per value, in the driver's own software key - the same key `XhciLogVerbosity`
goes in, whose instance number is fixed by nothing on either target and has to
be found by content:

1. Set `XhciImodInterval250ns` (`REG_DWORD`, 250 ns units). `scripts/bench/`
   carries a helper per target: `IMOD.BAT` on the NT side, which finds the key
   itself through `reg.exe`, and `IMOD98.BAT` on Windows 98 and ME, which has
   a `FIND` step for the instance number and takes the ladder steps by name
   because COMMAND.COM cannot convert a decimal to the hex a `REGEDIT4` dword
   needs. `scripts/bench/README.md` says why that is two files.
2. **Reboot**, and confirm it took: `XHCISNAP -verbosity 2`, then
   `imod.exp.written` 1 and `imod.exp.readback` equal to what was asked. A
   figure taken without that check is not attributable to an interval.

   A reboot rather than a Device Manager disable and re-enable, on **both**
   targets and for two different measured reasons. On Windows 98, disabling any
   USB host controller devnode bugchecks the machine before the teardown
   completes (`build-and-test.md`, "Do not disable the controller in Device
   Manager"). On the E460 under 32-bit Windows 7, the first Disable of this
   controller never finished (`runs/run-22.md`, task 22.9). The sweep is nine
   reboots on either target; plan it that way rather than discovering it.
3. Run the workload. Take `XHCISNAP` at the start and at the end, noting the
   wall-clock gap, so `isr.entries` and `dpc.count` give a rate rather than a
   total.

The ladder: **unset** (the control - no IMOD write, hardware's own 4000),
then 4000, 2000, 1000, 500, 200, 100, 40, 10. Take the control first and
again last; a machine that has drifted between them has invalidated the run,
and that is the cheapest way to find out.

**Unset and 0 are different arms and neither substitutes for the other.**
Unset means the start writes no IMOD at all, which is the shipping driver, so
the same binary with nothing set is the baseline and no rebuild separates it
from the readings. 0 is moderation off. The driver tells them apart by
`imod.exp.status`, not by the value, which is why the read's status is a note
of its own.

The workloads, per the roadmap: sustained mass-storage **read** and sustained
mass-storage **write**, and a USB Ethernet transfer. Each long enough that the
figure is a rate and not a burst.

**And an isochronous stream playing throughout at least one pass**, which the
roadmap asks for and which is the half most likely to be skipped: 1,000 events
a second at Full Speed and 8,000 at High Speed is what moderation exists to
absorb, and per-interrupt cost at real rates is what has bugchecked Windows 98
on bare metal before. A throughput number taken with the bus otherwise idle
does not answer the question the register is for.

Targets, in the roadmap's order: the E460 under **Windows 98 SE** first, and
under **32-bit Windows 7** if to hand.

Worth adding while the rig is set up, because nothing else can take it: **one
suspend/resume cycle with a nonzero value set**, reading `imod.exp.readback`
against IMOD after the resume. That is the only way to exercise the F10 fix
above against a value that is not 0.

### How it was actually run

2026-09-21, 23:47, to 2026-09-22, 01:05, by the owner at the machine, with
the procedure relayed step by step.

- **The machine was the P14s Gen 1, not the E460.** Windows 98 SE with nothing
  USB on it; NUSB 3.3 installed first, then the package from
  `out\bench-23.2\` (`xhci98.sys` SHA-256 `274d04a2...`, built at `863d537`,
  checked on the target by date because Windows 98 has no hash tool and the
  pre-rename build is the same 86,187 bytes). The kit went on through DOS and
  the BIOS's legacy USB support, since Windows 98 has no USB before the
  driver. The driver's software key was `...\Class\USB\0010`. The E460 took
  one control boot first (key `0000`, control notes read correctly) before the
  owner moved the session; it was left with no value set and logging at
  level 2.
- **Mass storage only, by the owner's decision.** No USB Ethernet transfer
  and **no isochronous stream**, so the roadmap's clause that a stream play
  during at least one pass is not answered by this reading. It is carried
  into 23.4's release gate rather than dropped.
- **The workload was ATTO Disk Benchmark 2.41, not a file copy**, at its
  defaults: transfer sizes 0.5 to 8192 KB, total length 256 MB, Direct I/O,
  neither overlapped I/O nor comparison. The internal disk runs through CSM
  compatibility mode on this machine and is slow, so a copy from `C:` would
  have measured `C:`. The target was a USB 3 stick enumerated at High Speed.
  One ATTO pass per arm.
- **The ladder was shortened by the owner**, and 160 added as Linux's own
  default of 40 us: control, 4000, 1000, 200, 160, control. 160 is not one of
  `IMOD98.BAT`'s named steps and went in as `IMOD98 HEX 000000a0 0010`. The
  owner stopped at 160 because reads had levelled off (below), and 40 and 10
  were not taken. The second control was taken, at the cost of one more boot,
  and it is what saved the write figures from being misread.
- **Per boot**: `SWEEP98 ARM A` (a dump and a time), `find "imod"` on the new
  `.TXT` to see `imod.exp.status=00000000` and the value asked for before
  running anything, ATTO, a screenshot of its result, `SWEEP98 ARM B`, the
  next `IMOD98`, a reboot. `SWEEP98.BAT` was written during the session and is
  now in `scripts/bench/`.
- **The suspend/resume cycle was not taken**, so `xhciRestoreState`'s IMOD
  write has still only ever carried 0 (the second finding above stands).

Three things the bench found wrong with its own tools, all fixed in the same
commit as this record:

1. **COMMAND.COM performs redirection on a `REM` line.** `IMOD98.BAT`'s header
   quoted the redirection syntax it warned about, so every run left files
   named after the targets in the current folder and printed `File not
   found` for an input redirection. The value writes were unaffected - the
   junk targets carried a trailing backtick and the real `.REG` is rebuilt
   from its first line - and every arm's value read back right. `LOAD98.BAT`,
   `STGF98.BAT` and three `xhciqual` DOS batch files had the same pattern.
   `lessons.md` has it.
2. **`PAUSE` did not return** in the Windows 98 DOS box once ATTO had run in
   the GUI, focused and with a key pressed, so `SWEEP98.BAT` takes its two
   dumps in two calls.
3. **The counters are not in the `.TXT`, and the stock offsets table cannot
   read the `.BIN`.** Both corrected in place above.

### Results

Every arm was verified in its own dumps before its figures were used, and
every dump reported its tear detector unchanged across every window:

| Boot | `ImodExperimentStatus` | Value | Written | Readback | Interrupts in the run | DPCs | Window |
|---|---|---|---|---|---|---|---|
| CTL1 | 8 (absent) | - | 0 | - | 274,315 | 274,314 | 568 s |
| 4000 | 0 | 4000 | 1 | `0fa0` | 274,367 | 274,366 | 583 s |
| 1000 | 0 | 1000 | 1 | `03e8` | 337,307 | 337,306 | 376 s |
| 200 | 0 | 200 | 1 | `00c8` | 346,301 | 346,300 | 491 s |
| 160 | 0 | 160 | 1 | `00a0` | 334,465 | 334,380 | 520 s |
| CTL2 | 8 (absent) | - | 0 | - | 268,375 | 268,374 | 627 s |

"Interrupts in the run" is `InterruptCount` in dump B less dump A, and
`InterruptsClaimed` equals it on every row, so none was spurious. The window
is the operator's time between the dumps and includes the screenshot, so it
is not a rate; **interrupts per ATTO pass is the comparable figure**, because
the pass is the same work every time.

ATTO's own table, KB/s, write / read:

| Size (KB) | CTL1 | 4000 | 1000 | 200 | 160 | CTL2 |
|---|---|---|---|---|---|---|
| 0.5 | 62 / 161 | 61 / 161 | 71 / 400 | 80 / 440 | 79 / 442 | 71 / 161 |
| 1 | 182 / 321 | 183 / 318 | 179 / 1077 | 153 / 1528 | 184 / 1509 | 179 / 323 |
| 2 | 475 / 646 | 451 / 645 | 480 / 2178 | 400 / 2989 | 424 / 2982 | 438 / 648 |
| 4 | 1252 / 1290 | 1230 / 1288 | 2748 / 4366 | 2837 / 5342 | 1492 / 5285 | 1025 / 1290 |
| 8 | 2524 / 2580 | 2584 / 2572 | 5636 / 8641 | 5401 / 9592 | 1945 / 9729 | 1710 / 2588 |
| 16 | 5048 / 5152 | 4833 / 5184 | 9488 / 14727 | 9969 / 16181 | 7585 / 16221 | 4783 / 5176 |
| 32 | 9752 / 10113 | 10066 / 10369 | 15678 / 22392 | 15566 / 23976 | 11872 / 24094 | 7858 / 10304 |
| 64 | 16262 / 18029 | 15566 / 17979 | 21312 / 29654 | 20480 / 31736 | 15887 / 31659 | 14124 / 18029 |
| 128 | 11270 / 12603 | 11527 / 12459 | 17736 / 24918 | 6570 / 26804 | 5128 / 28743 | 4912 / 12554 |
| 256 | 13443 / 14735 | 13395 / 14727 | 19190 / 27478 | 5563 / 30588 | 5550 / 30768 | 5455 / 14785 |
| 512 | 14455 / 16278 | 14573 / 16268 | 20062 / 28957 | 5571 / 32147 | 5508 / 32147 | 5729 / 16268 |
| 1024 | 15286 / 17086 | 15226 / 17076 | 21086 / 29793 | 6584 / 32419 | 6584 / 32419 | 6449 / 17076 |
| 2048 | 15679 / 17521 | 15818 / 17567 | 21338 / 29925 | 6714 / 32656 | 6778 / 32896 | 6614 / 17556 |
| 4096 | 15679 / 17453 | 15615 / 17442 | 21389 / 29661 | 6780 / 32896 | 6837 / 33140 | 6568 / 17476 |
| 8192 | 15679 / 17706 | 15670 / 17567 | 21474 / 29793 | 6858 / 33140 | 6753 / 33140 | 6766 / 17637 |

Transcribed from the six ATTO screenshots (the first a photograph), which the
owner holds; they are not committed.

What it says:

- **Reads: 17.6 MB/s at 1 ms, 29.8 at 250 us, 33.1 at 50 us and at 40 us.**
  The two controls are 0.4% apart at 8 MB and identical at 4 KB, so +69% and
  +88% are not noise. Small transfers gain most - a 4 KB read goes from 1.29
  to 5.3 MB/s, a factor of four - which is the hypothesis's own shape: a
  Bulk-Only command waits out the interval at each of its completions, and a
  small transfer is all completions. Reads level off between 200 and 160 at
  about 33 MB/s, and nothing below 160 was needed to answer the question.
- **4000 is the control, to the kilobyte.** Writing 4000 changes nothing, so
  this Comet Lake xHC resets IMODI to 4000 as the spec says, and every bare-
  metal reading this project has taken ran at 1 ms. The first of the two QEMU
  findings above is now confirmed from the hardware side.
- **Writes from 200 on are not a reading.** At 128 KB and above, writes fell
  to about 6.7 MB/s at 200 and stayed there at 160 - and **the second control
  fell with them**, to 6.8, with no IMOD write at all. So that is the stick,
  degrading under several passes of ATTO writes, and not the interval.
  The one clean write comparison is 1000 against the first control: 21.5
  against 15.7 MB/s at 8 MB, +37%.
- **The cost is about a quarter more interrupts for the same work**: 274,000
  a pass at 1 ms, 334,000 to 346,000 below it. The machine stayed up through
  every pass, including 40 us, with no refusal and no fault in either dump.

The limits, which 23.4 inherits: one pass per arm, one stick, High Speed only,
one machine, and no isochronous stream and no Ethernet. The write gain below
1000 is unmeasured, not absent.

### The stop rule, as the roadmap set it

If no value below 4000 measures faster outside run-to-run noise: record the
numbers in `lessons.md`, leave the start not writing IMOD - which is where it
already is, since the write is behind a define that no shipping build sets -
and **close 23.4 as not taken**. That outcome costs no revert: the shipping
binary never changed.

If a value does measure, 23.4 builds the registry value that ships, and which
number becomes the default is the owner's decision and not this task's.

**Applied, 2026-09-22: a value measured, so 23.4 is taken.** The owner then
made the decision this task left to them. The first choice was 200, the read
optimum; it was moved to **1000** after the trade was set out - most of the
read gain (29.8 against 33.1 MB/s) and the only clean write gain, for a
worst-case rate of 4,000 interrupts a second where 200 allows 20,000, on
Windows 98, where per-interrupt cost at real rates is what has bugchecked
bare metal before. So the INF writes `XhciImodInterval250ns` = 1000; a value
that is absent, unreadable or outside 10 to 4000 falls back to 4000, the
hardware's own; and because this reading had no stream playing, **one
isochronous pass at 1000 on bare-metal Windows 98 gates 23.4's release**.
*(Superseded later the same day: 23.5 measured 500 at 32.5 MB/s with a clean
audio pass, and the owner moved the INF's value to **500**; see 23.5
below.)*

### What 23.3 leaves

Done: the reading, above; `lessons.md`; `build-and-test.md`'s staging
section for the experimental build, written after the procedure was run; and
the two QEMU findings, the first now confirmed on hardware. Still open, and
none of it this task's to close:

- The isochronous clause - roadmap task 23.5, the audio test, which gates
  23.4's release.
- Writes below 1000, on a target that does not degrade under the benchmark
  (an SSD in a USB enclosure).
- One suspend/resume with a nonzero value, which is still the only way to
  exercise `xhciRestoreState`'s IMOD write against anything but 0.
- The E460 and 32-bit Windows 7, neither measured. The E460's row in
  `build-and-test.md` still says "Windows 98 only" against the Windows 7 run
  of 2026-09-19 (`run-22.md`, 22.9); unresolved.

## 23.5 - the audio test, read first on the 23.3 build

Status as of 2026-09-22: **the read-first half is taken, and 1000 passes it.**
The owner closed the roadmap box on this half the same day: the gate proper
is read once, at 500 on the cut's own `release` binary, as roadmap 23.9,
and not also on a pre-cut 23.4 build.

### How it was run

2026-09-22, the P14s Gen 1 under Windows 98 SE and NUSB 3.3, the 23.3
experimental driver still installed (key `...\Class\USB\0010`), logging at
level 2. A Full-Speed USB audio device on one root port and the 23.3 stick on
another; Windows 98's own USB audio driver; Media Player looping a WAV. Per
boot: the WAV started first, then `SWEEP98 ARM A`, one ATTO 2.41 pass at the
23.3 settings, `SWEEP98 ARM B`, the audio stopped last, so both dumps and the
whole pass include the stream. The owner listened throughout. Arms: control,
1000, control. 200 was offered and not taken. The owner then added **500**,
later the same morning, as one boot carrying two passes: a silent one first,
which puts 500 into 23.3's read ladder, then one with the stream.

**The first 1000 boot read `imod.exp.status=00000008`** - the value was absent
- and was not run: the `IMOD98 1000 0010` before its reboot had not been
typed. Set again, rebooted, and the boot that counts read status 0, value
1000, written 1, readback 1000. The on-machine `find "imod"` check before
starting the benchmark is what caught it; without it a control would have
been recorded as 1000.

### What it read

**Audio plays on bare-metal Windows 98.** That settles what `LOAD98.BAT`'s
header left open: Windows 98's audio stack failed five of five in the Phase 9
vehicle, and whether that was the VM or Windows 98 was not known. It was the
vehicle.

| Boot | Status | Readback | Iso packets | Interrupts | Interrupts/s | `IsoRingUnderruns` | `IsoPacketErrorsTotal` / `IsoMissedServiceTotal` / overruns / TRB error recoveries |
|---|---|---|---|---|---|---|---|
| CTL1 | 8 (absent) | - | 602,570 in 603 s | 568,269 | 942 | 1 | 0 / 0 / 0 / 0 |
| 1000 | 0 | `03e8` | 509,710 in 512 s | 745,703 | 1,457 | 2 | 0 / 0 / 0 / 0 |
| CTL2 | 8 (absent) | - | 661,420 in 665 s | 624,787 | 939 | 3 | 0 / 0 / 0 / 0 |
| 500 | 0 | `01f4` | 504,500 in 506 s | 778,481 | 1,540 | 1 | 0 / 0 / 0 / 0 |

Deltas between each boot's two dumps. **Every dump taken with the stream
playing is torn** - its tear detector moved by 6 to 170 between the first and
last window, because the stream posts about a thousand events a second while a
dump is read out - against passes of half a million interrupts and more, so
under 0.05% of any delta and no figure above changes. The silent 500 pass's
two dumps are coherent. *(This paragraph first said every dump was coherent.
It was written from the 23.3 dumps and not checked against these; corrected
the same day.)* Isochronous
packets ran at 1,000 a second throughout, as a Full-Speed stream should.
`IsoCadenceMismatches` was 10.0% of packets on every row, the same ratio at
both intervals, and `IsoEventsUnattributed` tracked the underruns. Here the
window is nearly all benchmark, and the stream sets a floor of about 1,000
events a second, so interrupts per second is a fair figure: the control sits
just under the 1,000-a-second ceiling a 1 ms interval allows, and 1000 at
about 1,460, far below its ceiling of 4,000.

By ear, per the owner:

- **CTL1**: a dropout and repeated sound as ATTO started, then clean, then
  stutter and repeats on reads from 2048 KB up. Writes clean.
- **1000**: stutter on reads from 1024 KB up. Writes clean.
- **CTL2**: stutter on reads from 1024 KB up, as at 1000. Writes clean.
- **500**: better than 1000 and both controls - the owner's word, one pass.

ATTO with the stream playing, KB/s at 8 MB, beside the same interval without
it:

| | Read with stream | Read without | Write with stream |
|---|---|---|---|
| CTL2 | 15,055 | 17,637 (23.3) | 6,342 |
| 1000 | 29,051 | 29,793 (23.3) | 6,746 |
| 500 | 31,655 | 32,537 (this boot) | 6,717 |

(CTL1's screenshot was not saved. Writes are the degraded stick's, as in 23.3.)

**The silent 500 pass is 23.3's missing rung.** Reads at 8 MB: 17.6 at 4000,
29.8 at 1000, **32.5 at 500**, 33.1 at 200 and 160. So 500 takes 98% of the
plateau, and almost all of the gain between 1000 and the plateau lies between
1000 and 500. Its interrupts per silent pass, 334,344, are the same as 160's
(334,465) and within 4% of 200's (346,301). It was taken later on 2026-09-22, hours after the overnight 23.3 sweep, with no control of
its own that morning; reads were stable across 23.3's two controls and the
two audio controls, which is the ground for comparing it.

500 at 8 KB to 64 KB, silent, against 1000 and 200 (reads, KB/s): 9,525 /
16,141 / 23,350 / 31,133 at 500; 8,641 / 14,727 / 22,392 / 29,654 at 1000;
9,592 / 16,181 / 23,976 / 31,736 at 200. At small transfers too, 500 is
within a few percent of 200.

### The verdict against 23.5's rule

**Passes.** At 1000 the stream played through the whole pass, the machine
finished it, and nothing moved that the controls do not also show: the same
stutter on large reads - the two controls themselves differed by one row,
2048 against 1024 KB, and 1000 matched the second - and ring underruns of 2
against the controls' 1 and 3, with every isochronous error counter at 0 on
every boot. And the stream costs 1000 less than it costs the control: reads
fell 15% under the stream at 4000, 2.5% at 1000 and 2.7% at 500. **500 passes
the same rule**, with one underrun and every error counter 0, and sounded
better than 1000 by ear. Whether that moves the default is the owner's
decision - **and it did**: on 2026-09-22 the owner moved the INF's value from
1000 to **500**, keeping 4000 as the fallback. The gate proper is therefore
read at 500 - on the cut's own `release` binary, as roadmap 23.9, and not
also on a pre-cut 23.4 build.

**What it does not settle, and is not IMOD's:** Windows 98 stutters on this
machine during large sustained reads **at the hardware default**, with this
driver as it has always shipped. It is audible, it starts at 1 or 2 MB
transfers, and it is not carried by a driver error counter - one to three ring
underruns a pass do not account for repeated stutter across several rows. So
the cause is not located: the audio stack's own buffering under CPU or DPC
load is as plausible as this driver's ring refill. It is recorded here as a
finding for later and does not block 23.4.

**Superseded (2026-09-22):** this section first ended "Owed: the same pass
on the 23.4 build, which is the gate proper". The same day the owner closed
23.5 on this read-first half and moved the gate proper to the cut's own
`release` binary, read once as roadmap 23.9, with no separate pre-cut pass,
so nothing is owed here.

## 23.4 - the registry value

Status as of 2026-09-22: **written on branch `23.3` and green on the host;
the guest readings the checkpoint names are owed**, so the roadmap box stays
open. **Later the same day the guest readings were taken on branch
`23.4-guest-readings` and both targets pass** ("Guest readings" below), so
the box closes.

### What landed

- **Every build reads `XhciImodInterval250ns`** beside the two log values,
  through the same `UsbPortGetMiniportRegistryKeyValue` service and key, with
  no new import (on x86 the binary imports twelve pairs on `release` and
  `debug`, thirteen on `qemu`, and the allowlist holds thirteen, as before;
  the amd64 import set is unchanged too). The read
  only records what the registry said (`ImodStatus`, `ImodRequested`); the
  choice is `XhciImodIntervalChoose`'s, a pure function in `src/xhci_init.c`:
  10 to 4000 as given, anything else - a failed read, 0, 9, 4001, 70000 -
  replaced by 4000. Because the chooser also sees a zeroed extension as
  "nothing read", a start that somehow skipped the read still writes 4000.
- **Every start writes IMOD**, the default included, at the end of
  `xhciProgramEventRing` after ERSTBA and before R/S, and reads it back
  (`ImodInterval`, `ImodReadback`). A recovery's reinitialisation passes
  through the same function after HCRST, so it rewrites the interval. The
  restore path is unchanged and now carries a nonzero interval across a
  resume.
- **The four fields sit after `Log`**, where 23.3's six experimental fields
  stood, so no existing counter moved: `SIZEOF` 92,304 to 92,320 on x86, and
  the trailing pair shifted by 16. `scripts/vm-matrix/offsets*.txt` were
  regenerated and have the four new rows, through new
  `XHCI_DBG_VALUE_CHANGED` sites.
- **The snapshot header carries them, schema 3 to 4**, 88 to 104 bytes.
  `XHCISNAP` prints them under "registry values": the value and whether it
  was read, the interval in force in 250 ns units and microseconds, what the
  register read back, and in words why a default was used. The counter block
  carries `imod.interval` and `imod.readback`, and the start notes
  `imod.status`, `imod.requested`, `imod.interval` and `imod.readback`.
- **Both INFs write 500** on all install paths (`[Xhci.AddReg]` and
  `[Xhci.AddReg.NT]`, which the NT 5.x and NT 6.x paths of both files share).
  The INF gate's `VAL-*` table has a third row with its own reason for the
  default, since "must ship off" is false of this one, and both footprints
  learned the rows (three x86, two amd64).
- **23.3's experimental build is retired**, by the owner's decision at the
  start of this task: the define, its marker, `make-package.ps1
  -ImodExperimentArtifact` and the refusals in `src/sources`,
  `build-driver.cmd` and `xhci_dispatch.c` are gone, the two packaging
  scripts restored to their state before `e2aa325`. `IMOD.BAT`, `IMOD98.BAT`,
  `SWEEP98.BAT` and `scripts/bench/README.md` describe the shipping contract;
  `IMOD98.BAT` lost its 0 step, which the driver now refuses.

### Host readings

- The host suite: two new vectors. `test_imod_choose` pins the three
  fallbacks and both bounds (11 checks); `test_imod_start` drives six values
  through the registered start and checks status, value, interval, register
  and read-back for each, the re-read at a second start, and a packet with no
  registry service. The conforming save/restore vector now starts at 500 and
  resets the register to 4000 between suspend and resume, so the value the
  restore writes back can be neither the model's 0 nor the reset value by
  coincidence. `test_init` 16,782 checks, every binary 0 failures.
- **A clamping mutant fails five checks**, all below the floor (0 and 9 come
  back as 10). Above the ceiling a clamp and a substitution give the same
  4000, because the default is the maximum, so no vector can tell them apart
  there and none claims to.
- The INF gate's self-tests: seven new cases (`imod-no-9x`, `imod-no-nt`,
  `imod-default-is-fallback`, `imod-default-hex-9x`, `imod-type`,
  `amd64-imod-missing`, `amd64-imod-default`), 565 checks passed.
- `build-driver.cmd all`: every flavour built, every gate green, the packager
  self-tests 307 checks with the experiment's cases gone. `release` x86 links
  at 86,059 bytes.

### What 23.4 still owes

- **The checkpoint's 23.4 clause, read in guests**: on a Windows 98 SE and a
  Windows 2000 SP4 guest, an INF install reading 500 back from the register,
  and the value deleted, 0, 5000 and 4000 each reading 4000, with the start
  never failed. QEMU stores IMOD and returns it (`hw/usb/hcd-xhci.c`, 23.3),
  so the register value is observable there even though no rate is. This is
  also the first reading of Windows 98's 16-bit engine storing a nonzero
  decimal DWORD from this INF. **Taken 2026-09-22 and passed on both; see
  "Guest readings" below.**
- **One suspend/resume with 500 in force** - the first exercise of
  `xhciRestoreState`'s IMOD write with anything but 0 or 4000 outside the
  host model. **Dropped by the owner on 2026-09-22**, because a guest could
  not witness it: `ImodReadback` is taken only at the start
  (`xhciProgramEventRing`), so `XHCISNAP` after a resume repeats the start's
  reading whatever the restore wrote; QEMU does not model IMODI, so no rate
  shows it either; and the restore path is unchanged by 23.4 and
  value-agnostic, and the host's save/restore vector already pins it with
  500 against a register reset to 4000.
- **23.5's gate proper**, the audio pass at 500 on bare-metal Windows 98,
  now read on the cut's own `release` binary (roadmap 23.9).

### Guest readings, 2026-09-22: both targets pass

Taken on branch `23.4-guest-readings` off `1.1.1.0` @ `2197d51`, after
`build-driver.cmd all` and `all -amd64` had both passed every gate on that
tree (the first build since `7488f13`). Both guests ran the same `qemu`
package, `out\pkg-qemu-x86`, `xhci98.sys` SHA-256 `7f3102342e3bf525...`,
reporting `DriverEntry (built Sep 22 2026 19:58:00)`, and `XHCISNAP` rebuilt
from the same tree. The `qemu` flavour was chosen so each start had two
witnesses: the debug console's four `imod` lines and `XHCISNAP`'s
"registry values" section. The agent drove both GUIs through the monitor.

**The vehicles.** Windows 98 SE was `vm\sweetlow-2a.img` at
`sweetlow-stack-nodriver` (SweetLow's stack, no xhci98), so the first arm is a
genuine first install through the INF. The owner chose it over the NUSB image
because the value is stack-independent and a fresh install needs no upgrade
route. Windows 2000 SP4 was `vm\win2k-xonly.img` reverted to
`win2k-xonly-clean-install`. Both ran on local work copies (`C:\work\t234`,
`C:\work\w2k`), and neither OneDrive image was written or copied back: the
Windows 2000 image's live state is newer than its last snapshot, so reverting
it in place would have lost it. Windows 2000 ran from a scratch copy of
`qemu-win2k-xonly-run.cmd` repointed at the work copy and a local transfer
directory.

**How each arm was set.** On Windows 98, `IMOD98.BAT` under instance `0002`:
`CLEAR`, `HEX 00000000`, `HEX 00001388` (5000), `4000`. On Windows 2000,
where there is no `reg.exe`, a REGEDIT4 file was written with `echo` in `cmd`
and imported with `regedit /s`, under
`Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\0000`: `=-` to delete,
then `dword:00000000`, `dword:00001388` and `dword:00000fa0`. Each arm was
one restart. On Windows 98 that was a cold one: shut down, let QEMU exit,
relaunch, because a guest-initiated restart wedged at the splash as
`lessons.md` records.

| Arm | Status | Requested | In force | Readback | `XHCISNAP` |
|---|---|---|---|---|---|
| INF install | 0 | 500 | 500 | 500 | `read, value 500`; `500 x 250 ns = 125.00 us; register reads 500` |
| value deleted | 8 | 0 | 4000 | 4000 | `NOT read, value 0`; `4000 x 250 ns = 1000.00 us; register reads 4000` |
| 0 | 0 | 0 | 4000 | 4000 | `read, value 0`; 4000; register 4000 |
| 5000 | 0 | 5000 | 4000 | 4000 | `read, value 5000`; 4000; register 4000 |
| 4000 | 0 | 4000 | 4000 | 4000 | `read, value 4000`; 4000; register 4000 |

The table is both targets: **Windows 98 SE and Windows 2000 SP4 gave the same
row, digit for digit, at every arm**, on both witnesses. Status 8 is
`MP_STATUS_UNSUCCESSFUL`, what usbport answers for an absent value; the
driver then substitutes 4000, as it does for the out-of-range 0 and 5000.

**No start failed.** Every start on both targets - seven on Windows 98 (the
install boot, the five arms and a final check boot) and six on Windows 2000 -
reached `DriverEntry`, completed the No Op self-test with one witness,
answered `RH_GetRootHubData`, and logged the four `imod` lines. On Windows
2000 the driver started on the install boot itself.

**What Windows 98's 16-bit engine wrote.** Before any arm, a `regedit /e`
export of `Services\Class\USB\0002` read
`"XhciImodInterval250ns"=dword:000001f4`: the engine stored the INF's decimal
500 as a proper `REG_DWORD`. That is the first observation of it doing so
from this INF.

**Left behind.** Both guests set back to 500 and `XHCISNAP -disable`d, then
shut down cleanly. The work copies are throwaway.

#### Two defects in `IMOD98.BAT`, found by this leg and fixed on its branch

- **`IMOD98 FIND` reported a failure for an export that succeeded.** In an
  MS-DOS Prompt window, COMMAND.COM does not wait for a Windows program, so
  `if not exist C:\USBCLASS.REG` ran before regedit had written it. The file
  was there a moment later (1,775 bytes, read by hand to find `0002`). Fixed
  with `start /w regedit /e ...`, after deleting any old export so a stale one
  cannot pass the check. **Re-read on the same guest: `FIND` now lists the
  xhci98 lines.** The `/s` imports in the same file were not changed: each is
  followed by a restart, and none was seen to lose a value.
- **One line of its REBOOT advice never printed.** A line beginning
  `echo on this target...` is taken by COMMAND.COM as the `ECHO ON` command,
  so the sentence was replaced by `ON`. Reworded, and re-read on the guest.
  `scripts/vm-matrix/guest/LOAD98.BAT` had the same shape (`echo On a disk
  error box...`) and is reworded the same way. It was not re-run, and its
  cmd.exe twin `LOAD.BAT` is unaffected.

#### Operating notes

- **This host's monitor ports moved.** On 2026-09-22, Windows' excluded TCP
  range 56646-56745 swallowed the prep port of `2a-sweetlow` (56596 + 100), so
  QEMU could not bind it. The git-ignored `matrix.config.psd1` now gives that
  target 56790. Read the port from the config you are about to use, as the
  23.2 vehicles table says.
- **`prepare-image.ps1 -Boot` passes `-no-shutdown`**, so a Windows 98 shut
  down from inside stays at `paused (shutdown)` and must be `quit` at the
  monitor. Waiting for QEMU to exit waits forever.
- **The Windows 2000 xHCI-only guest sees the CD as `D:` and the transfer
  drive as `E:`.** A wizard pointed at `D:\` says the location "does not
  contain information about your hardware". Like the 2b and 2d guests, its
  keyboard is US-Dvorak (`build-and-test.md`).
- **No restart prompt followed this Windows 2000 install**, unlike 23.2's leg
  W, which raised "System Settings Change". Both went through the wizard's
  search, so leg W's box stays unexplained. Explorer did raise "E:\ is not
  accessible" after Finish, although `dir E:\` read the drive at once. It is
  recorded, not explained.

---

## 23.6 - the record

Status as of 2026-09-22: **done, on branch `23.6-the-record` off `1.1.1.0`
@ `44b3837`.** Documents, plus one prose subsection of `make-release.ps1`'s
`readme.txt` template on the owner's instruction (below). No version or date
moved, no build input changed, and no build was run: nothing under `src/` or
`xhcisnap/` is touched, and the one script touched changed only text inside
a single-quoted here-string. What the task owed was set by the roadmap's 23.6
entry, and 23.1 and 23.2 had already taken their own shares on their
branches (above), so this task inherited 23.4's changes and the two
"Not in this release" sections alone.

### The version line stays at `1.1.0.0`, and the new text says `1.1.1.0`

The release notes' opening line still names `1.1.0.0` and its `DriverVer`,
because 23.7 owns "the release notes' version" and this task moves none.
The sections this task brought in are therefore written **forward-dated** -
"From `1.1.1.0` ..." - which is the 22.6 precedent exactly: 22.6 wrote the
`1.1.0.0` tier into a file whose opening line still said `1.1.0.0`'s
predecessor, and 22.8 (`26162ae`) turned "From" into "Since" and moved the
opening line in the same commit as the date. 23.7 does the same here. Until
then the file is in the state the audit's item L10 describes, and that is
deliberate.

### What each document gained

- **`docs/using/release-notes.md`.** The opening paragraph lost its
  sentence about the two sections describing "the release after this one".
  "What this is" gained one paragraph naming the two things a user can see
  from `1.1.1.0`. "Not in this release: the controller's Advanced tab" is
  now "The controller's Advanced tab (from `1.1.1.0`)", its first paragraph
  rewritten as this release's and the rest as 23.1 and 23.2 left it; the
  NUSB upgrade paragraph names `1.1.1.0`. "Not in this release: the
  interrupt moderation setting" is now "The interrupt moderation setting
  (from `1.1.1.0`)", **written the way README's "Tuning" section is, on the
  owner's instruction of 2026-09-22 given while this task ran**: the same
  sequence and wording as README at `44b3837` - the one-paragraph
  definition, the key table and `NNNN`, the three-row value table, the
  replacement rule, the ATTO sentence (about 33 to 34.6 MB/s read and write
  at 500 against about 18 at 4000, pointing at README for the screenshot),
  the Linux 160 comparison with "more conservative since this is a generic
  driver", "Feel free to tune it" and the NUSB upgrade sentence. README is
  the source and the section follows it, so 23.3's 17.6-to-32.5 figures,
  23.5's audio pass and the "crashed Windows 98 before" reason are no longer
  in the release notes; `run-23.md` 23.3 and 23.5 keep them. Two things from
  23.4 are kept at the end of the tuning paragraph, since README has no
  place for them: that the read never fails a start, and that `XHCISNAP`
  from this download must be used because the snapshot schema moved (3 to
  4) and an older tool refuses the driver. 23.4's guest readings are this
  file's, not the notes'.
  **The same instruction covered the download's `readme.txt`**, so
  `make-release.ps1`'s template, section 9, "XhciImodInterval250ns", was
  rewritten to the same sequence and wording in that file's plain-text
  conventions, keeping its own two operating sentences (decimal or
  hexadecimal entry, and what `XHCISNAP` shows after the restart) and its
  pointer at section 5's upgrade steps; the key paths stay where that
  section already had them, shared by all three values. That template is
  otherwise 23.7's, and its `XHCISNAP` schema line is still 23.7's to write.
  The two pointers at "the next release" - the `usbui.dll` paragraph under
  "Installing" and the NUSB controller-stop entry under "Known limitations" -
  point at the two sections by name. The log section says a third `DWORD`
  now sits in the same key and where it is described. The High Speed entry
  under "Known limitations" says `1.1.1.0` changes none of it, and the FSC
  entry says the interval the restore path carries is the setting's own
  value from `1.1.1.0`.
- **`docs/issues/06-full-speed-root-port-bugcheck.md`.** The status
  paragraph says the two XP-and-later costs are known limitations of
  `1.1.1.0` as well, and that `1.1.1.0` answers nothing on the page: polling
  rates and true speeds are Phase 24's, split out by the owner on 2026-09-22
  to run after the cut. Section 5.1's pointer at "Not in this release" names
  the new section. Section 7's closing paragraph says both findings stay
  known limitations of `1.1.1.0`. Section 9 gained a first item saying the
  same, with Phase 24's order (24.1 to 24.3) and that section 8's virtual hub
  is 24.3's candidate. Section 8's heading, which names `1.1.0.0`, is
  unchanged, since the section is about that release's decision.
- **`docs/issues/README.md`.** The "Issue 6 is open" paragraph and the
  issue 6 row say `1.1.1.0` answers none of it and that polling rates and
  true speeds are Phase 24's.
- **`README.md`: checked, not changed.** `2e69e4e` and the owner's three
  edits after it (`18a6d55`, `37cfa70`, `44b3837`) already carry the Tuning
  section and the throughput row the handoff said this task would otherwise
  have owed.

### What this task did not do, and why

- The version and date, the history entry and the issue forms are 23.7's,
  by the roadmap's split, and were not touched; of the `readme.txt`
  template only the moderation subsection moved, on the owner's
  instruction above.
- The ATTO figures at 500 (about 33 to 34.6 MB/s read and write) now stand
  in the release notes and the `readme.txt` template as README states them,
  on the owner's instruction. They are the owner's reading from the README
  photo, and the photo's write figure has not been written into this file
  as a reading (the handoff's open item on clean write figures).
- The release notes say nothing of Phase 24 by number; a user reads "the
  work after this release". The issue pages, which name roadmap tasks
  throughout, name the phase.

### What was run

No build, since no build input changed. `scripts\check-source-charset.ps1`:
166 files clean. `make-release.ps1` parses (the PowerShell parser, no
errors), and `scripts\package\test-package.ps1` passed its 307 checks with
the template edit in place. Every edited file kept its CRLF endings and
carries no byte above 0x7F.

## 23.7 - what the cut needs that no gate supplies

**Done 2026-09-22**, the day of the cut, on branch `1.1.1.0` after
`23.6-the-record` was fast-forwarded into it (the owner, the same day).

### The version and the date

`1.1.1.0`, dated `09/22/2026`: `XHCI_VER_CSV`, `XHCI_VER_STR` and
`XHCI_DRIVERVER_DATE` in `src\xhci_version.h`, and `DriverVer` in both
INFs. The INF gate's `amd64-driverver-drift` self-test matched the literal
version `1.1.0.0`, so this bump would have turned it vacuous as 22.8's date
bump did; it now matches any version and writes `9.9.9.9`, so the next bump
cannot. `build-and-test.md`'s unpadded-date example follows the new date.

### What each document gained

- **`releases\history.md`**: the `1.1.1.0` entry, written for the
  installer - the Advanced tab on every supported system, the moderation
  value (units, the install's 500, the 4000 fallback and the replacement
  rule, that a lower value raises the interrupt rate, and README's ATTO
  figures), the NUSB upgrade that gets neither, `XHCISNAP`'s schema 4, and
  what did not change.
- **`docs\using\release-notes.md`**: the opening line names `1.1.1.0`, the
  sixth release, and its `DriverVer`; every "from `1.1.1.0`" that 23.6 wrote
  forward-dated reads "since `1.1.1.0`", the two section headings included.
- **`make-release.ps1`'s `readme.txt` template**, two changes. Section 6
  gained the paragraph this task was owed: use the `XHCISNAP.EXE` from the
  package, because the report is snapshot schema 4 and an older tool
  refuses this driver with "schema mismatch". A literal schema number in a
  perpetual template would go stale unseen the next time the schema moved,
  so the script now refuses a cut whose template names a snapshot schema
  other than `src\xhci.h`'s `XHCI_SNAPSHOT_SCHEMA`. And section 9 said "what
  every release before 1.1.1.0 ran at" - **a four-part version written by
  hand, which the template's own version-literal check refuses, so the cut
  would have failed on it**; it now reads "every earlier release", which is
  README's own wording.
- **Both issue forms**: the example version is `1.1.1.0`. Their
  operating-system lists already carried every supported system.
- **`README.md`: checked, not changed.** The Install section does not move
  with 23.4: the install writes the value itself, and the Tuning section is
  the owner's.

### Phase 24 removed

The owner removed roadmap Phase 24 on 2026-09-22, while this task ran.
Its section is gone from `roadmap-phases-17-on.md`, its row from
`docs/README.md`'s phase table, and every current-state sentence that
named it - Phase 23's goal, status and checkpoint, `roadmap.md`'s status
and the renumbering note, `docs/issues/06` (status and section 9),
`docs/issues/README.md`, and the release notes' High Speed entry - now
says polling rates and true speeds on root ports are **not scheduled**.
Issue 6 section 9 keeps the reporter's order for whoever takes them up.
Entries that record what a task wrote on its own day (23.6's, above and in
the roadmap) still name Phase 24, as the record of that day.

### What was run

`build-driver.cmd all` and `build-driver.cmd all -amd64` from this tree:
both passed every gate (host tests, the import gate, the INF gate and its
565 self-test checks over both INFs, the packager's 307, the launchers'
336, `xhcisnap`'s 5), the amd64 legs with their 5 known compiler warnings
and the x86 legs with none. All six binaries carry file version `1.1.1.0`:
`release-x86` 86,059 bytes, `debug-x86` 86,699, `release-x64` 97,280,
`debug-x64` 181,760. `make-release.ps1` parses with the new check. Every
edited file kept its CRLF endings.
## 23.8 - the cut, and the install legs read from the asset

**Cut 2026-09-22, all ten install vehicles read 2026-09-22/23.** One
finding, and it is not against this release: `docs/contributing/lessons.md`,
"Windows 98 wedges when a USB audio device is replugged after a cold boot".

### The cut

`build-driver.cmd all` and `all -amd64` from `c1ec345`, every gate green.
`XHCIQUAL.EXE` and `XHCISNAP.EXE` were rebuilt first: `make-release.ps1`
refuses a qualifier older than its own sources and the 23.7 header bump had
made it so. `make-release.ps1` with its default `-Arch` then wrote
`releases\1.1.1.0\` with the four flavour directories, the two tools, the
`LICENSE` and the generated `readme.txt`, and `out\xhci98-1.1.1.0.zip`
(committed as `7236636`).

**Re-cut the same night with `-Force`, nothing uploaded** (`5799ca2`): the
owner read the download's readme and struck the `history.md` entry's opening
sentence, "Two things you can see, and faster USB mass storage." Only
`readme.txt` changed among the published files; the asset went from
377,516 B to **377,497 B**.

The asset holds **17 files and so does `releases\1.1.1.0\`**, every pair
SHA-256 identical, nothing on either side alone. No Microsoft file: the four
`xhci98.sys` carry this project's version resource, and the only "Microsoft"
string in any published binary is the statically linked C runtime's error text
inside `XHCISNAP.EXE`. The published x86 binary is `15C99E9F...` (86,059 B) and
the amd64 one `3C597BE3...` (97,280 B), each identical to its `src\objfre`
build, and each published INF identical to its source.

**The asset is about 20 KB smaller than `1.1.0.0`'s** and the reason is not a
missing file: `74c35c7` trimmed the INF comments, so `xhci98.inf` went from
29,554 to 8,280 bytes and the amd64 file from 15,258 to 4,659.

### The legs

Each leg ran off a read-only clean copy in `vm\t238\` through a throw-away
overlay, with the unzipped asset as the transfer drive, so the wizard was
pointed at `E:\release-x86` (`D:\RELEASE-X86` on the 9x guests, where the
transfer drive is `D:` and the CD `E:`) or `E:\release-x64`. **On every leg the
port-`0xE9` log stayed at 0 bytes**, as the `release` flavour should, and each
installed `xhci98.sys` was SHA-256 identical to the asset's.

| Leg | Target | Result |
|---|---|---|
| 1 | Windows 98 SE, NUSB 3.3 | pass, with the 500 readback |
| 2 | Windows 98 SE, SweetLow | pass, full teardown |
| 3 | Windows ME, SweetLow | pass, full teardown |
| 4 | Windows 2000 SP4 | pass, with the 500 readback |
| 10 | Windows XP SP3, 32-bit | pass, full teardown |
| 8 | Windows Vista SP2 x86 | pass, full teardown |
| 9 | Windows 7 SP1 x86 | pass, full teardown |
| 6 | Windows Vista SP2 x64 | pass, full teardown |
| 7 | Windows 7 SP1 x64 | pass, full teardown |
| 5 | Windows XP x64 SP2 | pass, full teardown; the NT 5.2 half |

**Leg 4, Windows 2000 SP4** (`win2k-xonly-clean-install`). Found New Hardware
wizard at `E:\release-x86`, no media prompt, no restart prompt - unlike 23.2's
leg W, which raised one. `setupapi.log`: `Found PCI\CC_0C0330 in
e:\release-x86\xhci98.inf ... Section: Xhci.Dev`, `Decorated section name:
Xhci.Dev.NTx86`, `Installing section Xhci.Dev.NTx86`, and **`Xhci.Dev6`
nowhere**. `oem0.inf` is the asset's INF byte for byte. The three devices
bound; disable, enable, uninstall and rescan were taken with the audio device
unplugged, the release notes' documented way round Windows 2000's restart
prompt, and all four applied live. **23.4's clause on the `release` flavour**:
`XHCISNAP` reported build flavour `release`, snapshot schema 4,
`XhciImodInterval250ns   read, value 500` and `interval in force 500 x 250 ns
= 125.00 us; register reads 500`. `slot_enable` 14.

**Leg 1, Windows 98 SE under NUSB.** The Add New Hardware wizard found
`D:\RELEAS~2\XHCI98.INF` - the 8.3 alias a Windows 98 engine reads a
`release-x86` directory by - asked for the CD for `usbd.sys` (answered
`E:\WIN98`) and then for a restart, taken as a shutdown and a cold launch.
The controller, **USB 2.0 Root Hub**, the HID mouse (its `hidclass.sys` from
the CD), **USB Mass Storage Device** with its **USB Disk**, and **USB
Composite Device** with **USB Audio Device** and the Kernel Audio Mixer all
bound, read from a `regedit /e` export of `HKLM\Enum` off the disk afterwards.
Disable, enable, remove and rescan are **not taken on this leg**: NUSB's
`usbport.sys` crashes on any controller stop, the release notes' first known
limitation. `XHCISNAP` on the install's own value: `read, value 500`,
`register reads 500`, flavour `release`, schema 4. `slot_enable` 3.

*This leg was taken twice.* The first vehicle hit the wedge the lessons entry
above describes and was set aside once the investigation was done; the reading
recorded here is a second, clean install on a fresh overlay of the same clean
copy (`vm\t238\win98-l1b.qcow2`).

**Leg 2, Windows 98 SE under SweetLow's stack** (`vm\sweetlow-2a.img @
sweetlow-stack-nodriver`, copied into `vm\t238\win98-sl.qcow2`). Same install
route and CD prompt. The three devices bound, the audio one under **Composite
Device** - SweetLow's `usbccgp`, as 22.10 recorded. **The teardown this stack
allows**: the controller's properties showed the Advanced tab this release
ships and "This device is working properly"; ticking "Disable in this hardware
profile" gave **Code 22** with no crash and no restart prompt; unticking it
brought every device back; **Remove** cleared the USB class; and **Refresh**
reinstalled it through the wizard from the cached
`C:\WINDOWS\INF\OTHER\YEOKHE~1.INF` and brought all three devices back. The
`HKLM\Enum` export afterwards holds the same device set as before.
`slot_enable` 9, `slot_configure` 9.

**Leg 3, Windows ME under SweetLow's stack** (`winme-sweetlow-nodriver`).
Advanced route, Removable Media unticked, `D:\RELEASE-X86`, **no CD prompt**,
restart taken as a cold launch. Mouse and stick bound silently; the audio
device came up through ME's own `WDMA_USB.INF` with the Kernel Audio
Mixer, Renderer and Splitter behind it. Disable gave Code 22 - and the mass
storage device's own property page said so in words, "a device it depends on,
USB 2.0 eXtensible Host Controller (xhci98), has been dynamically disabled" -
enable restored it, Remove cleared the class and Refresh reinstalled it, the
audio device's wizard running again as 22.10 saw. Two `HKLM\Enum` exports,
before the teardown and after the rescan, hold the same device set.
`slot_enable` 9.

**Leg 10, 32-bit Windows XP SP3** (`winxp-clean-install`). Update Driver from
`E:\release-x86` behind **XP's Windows Logo prompt**, taken with Continue
Anyway (`#E366 ... (Policy=Warn, user said ok)`); no CD, no restart.
`setupapi.log`: `#I022 Found "PCI\CC_0C0330" in e:\release-x86\xhci98.inf ...
Section name: "Xhci.Dev"`, `#I063 Selected driver installs from section
[Xhci.Dev]`, `[Xhci.Dev.NTx86.Interfaces]` installed, and **`Xhci.Dev6` appears
zero times**. The three devices bound (USB Composite Device, USB Mass Storage
Device, USB Root Hub, USB Audio Device, HID-compliant mouse). Disable, enable,
uninstall and rescan with all three attached, no restart prompt; the rescan's
server-side install was refused as unsigned (`#E358`) and the wizard
reinstalled from `oem0.inf`, exactly as 22.10 read it. `slot_enable` 9.

**Legs 8 and 9, Vista x86 and Windows 7 x86.** Device Manager -> Update Driver
Software -> Browse -> `E:\release-x86`, behind the **Windows Security dialog**
("Windows can't verify the publisher of this driver software", with "Don't
install" holding the focus); **Install this driver software anyway** took it
on both, logged as `Driver package does not contain a catalog file, but user
wants to install anyway`, and **neither x86 guest showed a second box**. The
driver node is `xhci98.inf:XhciModels.NTx86.6.0:Xhci.Dev6:1.1.1.0:pci\cc_0c0330`
on both and the install runs `[Xhci.Dev6.NTx86]`; each store directory holds
`xhci98.inf`, `xhci98.PNF` and `xhci98.sys` alone, and the four OS-supplied
files were already on disk at the sizes 22.10 recorded (Vista 226,304 /
196,096 / 5,888 / 83,456; Windows 7 284,672 / 258,560 / 5,888 / 80,896).
Three devices on each, then disable, enable, uninstall with the driver kept,
and rescan - the rescan reinstalling from the store with no prompt.
`slot_enable` 9 on both.

**Legs 6 and 7, Vista x64 and Windows 7 x64**, from `E:\release-x64`, each off
its clean snapshot on a boot with driver signature enforcement disabled.
Windows 7 x64 reaches Advanced Boot Options from `sendkey f8` every 200 ms
from QEMU's start; **Vista x64 still does not** - it took 22.10's route,
`bcdedit /set {bootmgr} displaybootmenu yes` and `/timeout 30` from an
elevated prompt, then a reset, F8 at the boot manager menu and nine `down` to
*Disable Driver Signature Enforcement*. Both showed the same Windows Security
dialog and then **the Program Compatibility Assistant box that is x64's
alone** ("Windows requires a digitally signed driver", naming the service and
`C:\Windows\System32\...\xhci98.sys`); as 22.10 found, it is wrong - the root
hub had already installed and every clause below ran on that boot. The node is
`xhci98.inf:XhciModels.NTamd64.6.0:Xhci.Dev6:1.1.1.0:pci\cc_0c0330` and the
install runs `[Xhci.Dev6.NTAMD64]`, with the asset's amd64 INF (4,659 B) and
binary (97,280 B) in the store. **Vista x64 alone logs the driver-store import
trying to stage the four OS files** (`CopyFile ... usbport.sys ... failed 2`,
and the same for `usbd.sys`, `usbhub.sys` and `usbui.dll`), which is 22.10's
finding repeated and is not an error to the install. Three devices on each,
then the full teardown; `slot_enable` 9 on both.

*One reading about the teardown that is new, and it is not this driver.* On
Windows 7 x86 the **disable** raised "You must restart your computer" and did
not apply - the tree was unchanged and the devices stayed enumerated. `net
stop audiosrv` from an elevated prompt and the same disable applied live, with
every child gone and no restart prompt. 22.10 read that veto on this guest's
*uninstall* and traced it to the audio function's open user-mode handle
(`PNP_VetoOutstandingOpen`); it reaches the disable too. The x64 legs and
Vista x86 were driven with the service already stopped, so their clean
disables do not speak to it.

### Leg 5, Windows XP x64: taken 2026-09-23, and it is the NT 5.2 reading

Held up overnight because that guest's Administrator account has a password -
**`test`, which the owner supplied on 2026-09-23; the
`phase21-task-215-guest-2026-09-09` memory had recorded it as written down
nowhere.** `winxp64-clean-install-smp4`, four processors under TCG, the amd64
package from the asset's `release-x64\`.

- the install: Found New Hardware Wizard, "No, not this time", "Install from
  a list or specific location", `E:\release-x64` with removable media
  unticked. **XP's Windows Logo prompt**, taken with Continue Anyway. No CD
  prompt and no restart, as 22.10 read
- **the clause no other leg in this cut covers**: `setupapi.log` records
  `#I022 Found "PCI\CC_0C0330" in e:\release-x64\xhci98.inf ... Section name:
  "Xhci.Dev"` and `#I023 Actual install section: [Xhci.Dev.NTAMD64]. Rank:
  0x0000a005. Driver date: 09/22/2026. Version: 1.1.1.0`, then
  `[Xhci.Dev.NTAMD64.Interfaces]`. **`Xhci.Dev6` appears zero times**, so the
  NT 5.2 engine took the `NTamd64` field of
  `%Mfg%=XhciModels,NTamd64,NTamd64.6.0` and skipped `NTamd64.6.0` - 22.10's
  reading of that line, repeated on this release
- the installed `xhci98.sys` is the asset's amd64 binary (97,280 B) and
  `oem0.inf` the asset's amd64 INF (4,659 B), both SHA-256 identical. The four
  OS-supplied files came off `Driver Cache\amd64` with no prompt, at 22.10's
  sizes: `usbport.sys` 212,480, `usbhub.sys` 102,400, `usbd.sys` 7,552 and
  `usbui.dll` 123,392
- the three devices bound (**USB 2.0 eXtensible Host Controller (xhci98)**,
  **USB Root Hub**, **USB Composite Device**, **USB Mass Storage Device**,
  **USB Human Interface Device** and **USB Audio Device**), no bang
- disable (children gone, no restart prompt), enable (all back), uninstall
  (the USB class gone, no restart prompt), rescan - **the server-side install
  refused as unsigned, `#E358`**, then the wizard reinstalled from `oem0.inf`
  behind the same Logo prompt, and every device came back. That is leg 10's
  shape on 32-bit XP and 22.10's on this guest
- the flavour: the port-`0xE9` log stayed at **0 bytes**; QEMU's trace shows
  `slot_enable` 9, `slot_address` 20, `slot_configure` 12

Evidence `out\post-release\task23-8\l5log\setupapi.log`.
### What the finding cost, and why the cut stands

Leg 1's first vehicle wedged after a cold-boot replug, and the investigation
that followed is the lessons entry above: 24 guest boots across `1.1.1.0` at
three IMOD values, a diagnostic build with the IMOD write compiled out, and
`1.1.0.0` as a control. It is intermittent, it predates this release, and no
guest can implicate the moderation value because QEMU never consults IMOD.
**No re-cut for it**; the release notes gain nothing here, and whether it
becomes a known limitation is the owner's call.

Harness `out\post-release\task23-8\` (git-ignored: the 22.10 scripts with
their paths moved, `cycle.ps1` which drives one boot/set/plug/check cycle
unattended, `win98t.cmd` which adds `-msg timestamp=on` and the interrupt
trace events, the unzipped asset, the per-leg log directories and the
screenshots); disks `vm\t238\`; traces `vm\t238-*-qemu-trace*.log`.

## 23.10 - the post-release matrix

Status as of 2026-09-23: **taken, and the primary targets are no worse than
`runs/run-22-post-release/`** - Windows 98 SE is better by one row and Windows
2000 is unchanged, row for row. Run before 23.9 by the owner's choice (the
roadmap says "runs last"), driven by Claude, with the owner deciding each open
question. The four reports are in `run-23-post-release/`; the evidence is in
`out\post-release\1.1.1.0*\`.

### What was run, and on what

`build-driver.cmd qemu` and `qemu -amd64` at `157cb18`, both packages, both
`gen-offsets` runs, every gate green, the offset tables unchanged (`SIZEOF`
92320 x86, 95560 amd64; the 16 bytes since `1.1.0.0` were already
committed). Every report on both hosts names the same two builds: x86
`cf00ced51460cf10` (164,880 B) and amd64 `c5c3ec47df2142b9` (280,064 B). All
four images re-cloned with `-Clone -FreshCopy` and stamped
`base-1.1.1.0-qemu`, each stamp its file's only snapshot.

**Two hosts and two QEMU builds, which is a variable 22.9 did not have.**
`2b-fresh` and the first `xp64-fresh` and `win7-fresh` runs were taken on host
`XT-F80DAC37B29E` under QEMU 11.0.92 (`v11.1.0-rc2-12128-gc65ddfcd01`); the
`2a-fresh` run and the two reruns on host `MINIS-W11P-YKM` under 11.1.0
(`v11.1.0-12130-ge470268ff4`), the build 22.9 read. Each report's header
names its own.

| Target | Host / QEMU | Verdict | Against 22.9 |
|---|---|---|---|
| `2a-fresh` (Windows 98 SE) | MINIS / 11.1.0 | **PASS**, 17 rows, 5 NODRIVER expected, 3 not reached, 0 against, 1:26:18 | FAIL on the audio replug then; every other row identical |
| `2b-fresh` (Windows 2000) | XT / 11.0.92 | **PASS**, 17 rows, 6 NODRIVER expected, 0 not reached, 1:51:10 | identical, row for row |
| `xp64-fresh` (XP x64) | MINIS / 11.1.0 | FAIL, 4 NODRIVER expected, 2 against (`usb-ccid`, `u2f-emulated`), 1:50:51 | 22.9: 1 against (`usb-net`, since made expected) |
| `win7-fresh` (Windows 7 x86) | MINIS / 11.1.0 | FAIL, 4 NODRIVER expected, 0 not reached, 2 against (`usb-audio`, `usb-hub/churn`), 1:43:23 | 22.9: 3 expected, 1 not reached, 4 against |

The XP x64 and Windows 7 rows carry no checkpoint tax; the paragraphs below
say what each against-row is.

### Preparation, and one void run

As 22.9, with the harness's `-Attach <class> -AtPort 2` teaching on Windows 98
(drivers for four HID classes, storage, hub and audio, the composite then
`WDMA_USB.INF`, audio left attached through the shutdown; no driver for uas,
serial, braille, ccid, net; `-Status` 15 addressed, 13 endpoints opened).
Windows 7 took the package by `pnputil -i -a e:\xhci98.inf` from an elevated
prompt, not through Device Manager; the INF and the device are the same.

**The first `2b-fresh` run is void, and the mistake was Claude's.** It read
NODRIVER on audio and the keyboards because two Found New Hardware wizards
cancelled in prep (Video Controller, PCI Ethernet) came back at every boot and
queued every later install behind them. Both were finished with "Disable the
device" and the image re-stamped; the rerun is the PASS above. The void output
is `out\post-release\1.1.1.0-aborted-2b-wizard\`.

### Windows 98 SE: the audio replug passes

22.9's one against-row, the `usb-audio/fs` replug, passed both legs in the
full run, on the same QEMU build under which 22.9 saw it fail four times in
five. The audio group ran with no other guest on the host (the two NT reruns
were started once it finished). A partial run on the other host the same
afternoon, stopped by the owner after its audio group, passed it too, beside
the Windows 7 run (`out\post-release\1.1.1.0-2a-audio-partial\`). One full run
and one partial is not a rate; `lessons.md`'s entry for the row carries it.

### XP x64: the audio row needs a session, and a session costs two rows

The first run, at the login screen as design record 09 and
`scripts\vm-matrix\README.md` prepare it, read **`usb-audio/fs` NODRIVER on
both legs**, where `1.1.0.0` passed it; `-Group audio` alone read it again
(`1.1.1.0-xp64-audio-recheck\`). On a `-WorkDir` scratch copy, never copied
back: logged in, the device's isochronous endpoint opened on arrival
("endpoints opened" +1) and Windows said the device was ready; logged off and
replugged at the login screen, it was addressed and nothing opened in 105 s;
logged back in, it opened at once. Every fault and refusal counter zero. The
owner then logged in to a second copy by hand while Claude attached the device
at port 2 - opened 1 to 2, replug 2 to 3 - and **passed the row on that
check** ("if it works, can pass it"; `1.1.1.0-xp64-audio-loggedin\`). The
first run's own report still reads FAIL and was not edited. Whether the
difference from 22.9 is QEMU rc2 against final, the host, or session timing
was not attributed.

**The owner then had `xp64-fresh` log in by itself** (2026-09-23 16:40): on a
`-WorkDir` copy, `AutoAdminLogon=1`, `DefaultUserName=administrator`,
`DefaultPassword=test` under `HKLM\SOFTWARE\Microsoft\Windows
NT\CurrentVersion\Winlogon`, a clean shutdown, `-CopyBack`, re-stamped
`base-1.1.1.0-qemu` (still the only snapshot), verified to boot to the desktop
with no key sent. **This departs from the documented prep**, which leaves NT
guests at the login screen.

On that image the rerun **passed `usb-audio/fs` on both legs by itself**, and
read two new against-rows, `usb-ccid/fs` and `u2f-emulated/fs` NODRIVER, both
of which passed in the first run and in 22.9. The screenshots show why: with a
desktop, the first device XP has no driver for, `usb-net` (RNDIS), raises a
modal Found New Hardware wizard, which stays up and queues every later install
behind it - the mechanism of the void 2b run. At the login screen no wizard is
shown, which is why the first run and 22.9 passed those rows.

`-Group other` was rerun into `1.1.1.0-xp64-other-wizards-cancelled\` with a
scratch watcher that took a `screendump` every few seconds, looked for the
wizard's panel and sent Esc: `u2f-emulated` **PASS both legs**, `usb-ccid`
**PASS on the replug**. Its first attach read NODRIVER because the wizard had
opened cascaded, at a position the watcher's first version did not look at;
cancelling that one by hand let XP install "QEMU USB CCID" at once. So on
XP x64 both rows bind whenever nothing is queued ahead of them; `usb-ccid`'s
first attach was not read clean on the autologin image. The watcher and its
frames are evidence, not a harness change.

### Windows 7: the login hypothesis refuted, and the churn bugcheck again

The first run, at the login screen: `usb-audio/fs` NODRIVER both legs (as
22.9) and `usb-hub/churn` ERROR, the group not completed. The owner's
hypothesis was that the audio row fails because nobody is logged in, as XP
x64's does. `win7-fresh` was re-stamped with autologin the same way
(`DefaultUserName=test`, 17:07) and rerun: **the same two rows against, digit
for digit in the verdict line.** The audio row's screenshot shows a logged-in
desktop with the speaker marked unavailable, as 22.9's did. So the session is
not it on Windows 7, and 22.9's reading stands: QEMU's `usb-audio` has
endpoints only in alternate setting 1, and Windows 7 opens nothing until
something streams (and from XP on nothing plays on a root port, a known
limitation of `1.1.0.0`).

`usb-hub/churn` is 22.9's bugcheck, not a new one: both runs lost the guest
mid-row to a restart, one captured at Windows Error Recovery and the other a
few seconds later inside Startup Repair (`matrix-win7-fresh-hub-groupfail.png`
in each directory). That is issue 6 section 6.1's residual topology, a
known limitation since `1.1.0.0`; the stop code was not re-captured.
`usb-net` is now expected (22.9's measured entry), and the HID group, which
did not complete in 22.9, completed: the tablet rows passed.

### What is left for the owner

Whether the autologin images stay (they read XP x64's audio row and cost it
two rows to a wizard the login screen never shows); and whether XP x64's
`usb-ccid` first attach is worth a clean reading. Neither blocks 23.10, whose
clause is the two primary targets.

**Decided 2026-09-24: later post-release runs may use autologin NT
guests** (the owner). Design record 09 section 8 and `scripts\vm-matrix\README.md`
carry it, with the wizard cost.

## 23.9 - 23.5's gate proper, on bare metal

Status as of 2026-09-24: **read, and passed by the owner's ruling**: the
stream played through every pass with no dropout, and 500 stuttered where
neither control did, which the owner published as a known limitation. That
re-cut `1.1.1.0` under the same number, dated `09/24/2026`. Taken on the night
of 2026-09-23 by the owner at the machine, Claude reading the dumps. The
evidence is in `temp\rel\` and `temp\rel2\` (git-ignored).

### How it was run

The P14s Gen 1, and **a fresh Windows 98 SE install**: the owner reinstalled
it rather than take the rename-and-restart route off 23.3's experimental
build. Then NUSB 3.3, `1.1.1.0`'s `release-x86` from the published asset
(86,059 B, `15C99E9F...`), `XHCISNAP -verbosity 2` and a cold boot. The first
dump read `XhciImodInterval250ns` read, value 500, register reads 500, status
0: the INF's value in force on the `release` binary on metal. The kit was
`out\bench-23.9\`; every boot as its `README.TXT` and 23.5 say (the WAV
looping on a Full-Speed audio device at a root port, `SWEEP98 <arm> A`, the
value checked with `find`, one ATTO 2.41 pass at 23.3's settings on the 23.3
stick, the screenshot, `SWEEP98 <arm> B`, the audio stopped last). The owner
listened throughout.

**The instance, and a void first boot.** `IMOD98 FIND` exports the class key
and lists the lines naming xhci98; the instance is read by eye off the key
line above. It was read as `0002`, and `IMOD98 CLEAR 0002` went there - a
"USB Root Hub" key (`usbhub.sys`) that never held the value, so the delete
did nothing. The first control boot read 500 and was not run; its dumps were
kept as `XCTL1A.*`. The driver's key is **`0010`**, the same number as the old
install. CTL1's value was deleted there by hand in RegEdit; the later arms
used `IMOD98` against `0010`, and `CLEAR 0010` worked for CTL2.

### What it read

Five passes. Deltas between each boot's two dumps, decoded against
`scripts/vm-matrix/offsets.txt` (`SIZEOF` 92320 = the dumps); ATTO read and
write at 8 MB from the screenshots.

| Arm | In force | By ear (owner) | Read / write, KB/s | Pass | Iso packets/s | `IsoRingUnderruns` | Stops / aborts / ring-full | Interrupts/s |
|---|---|---|---|---|---|---|---|---|
| CTL1 | 4000 | from the 8192 KB write | 15,080 / 6,249 | 725 s | 999 | 2 | 0 / 0 / 0 | 942 |
| 500 | 500 | from the 2048 KB read onwards | 31,469 / 6,705 | 575 s | 926 | 28 | 98 / 165 / 32 | 1,397 |
| CTL2 | 4000 | from the 8192 KB write | 15,038 / 6,339 | 672 s | 998 | 3 | 0 / 0 / 0 | 941 |
| 500 again (`5002`) | 500 | from the 2048 KB read onwards | 31,543 / 6,739 | 510 s | 997 | 1 | 0 / 0 / 0 | 1,528 |
| 1000 | 1000 | from the 2048 KB read onwards | 29,114 / 6,712 | 557 s | 999 | 2 | 0 / 0 / 0 | 1,431 |

`IsoPacketErrorsTotal`, `IsoMissedServiceTotal`, `IsoRingOverruns` and
`IsoTrbErrorRecoveries` were 0 on every pass, and no command or endpoint
stop failed on any.

**The first 500 pass is the only one with driver-side activity.** Besides the
28 underruns, usbport stopped an endpoint 98 times, 165 transfers were
aborted, 32 were refused ring-full and retried, and an interface was selected
twice (a stream closed and reopened). The driver logged
`ep.recovery=00020713` 66 times - DCI 2 (endpoint 1 OUT), Stop Endpoint,
Context State Error: the endpoint was no longer Running when the stop
arrived, which the quiescence path handles by reading the endpoint state.
Every one completed; nothing failed or was given up. Which device's endpoint 1
OUT it was is not matched, and the audio device's streaming endpoint is the
likely one. It did not recur in the repeat at 500 or at 1000.

**The stutter reproduced and follows the read speed, not the value.** Both
controls stuttered only on the last row; 500 twice and 1000 once stuttered
from the 2048 KB read, where reads pass about 29 MB/s. In the repeat at 500
and at 1000 the stream was delivered at its full rate (997 and 999 packets a
second) with underruns no higher than the controls', so what was heard there
went wrong above this driver - the packets arrived, their content did not
keep up. Windows 98's own audio mixing falling behind while reads run twice
as fast fits, and is not measured.

Throughput is 23.5's: 500 reads at 31.5 MB/s with the stream against its
31.7, the controls at 15.0 to 15.1 against its 15.1.

**Against 23.5 the audio verdict reversed.** On the old install, with the
experimental build, 500 had one underrun and sounded better than both
controls, and the controls themselves stuttered on large reads. This install's
controls stutter only at the end. What differs is the installation and the
binary (`release` against the experimental build); which one matters is not
known.

### The verdict, and the owner's decision

**What the rule met and what it did not.** The stream played through the
whole pass at every arm, the machine finished every pass, and nothing dropped
out; no isochronous error counter moved on any pass. But at 500 it stuttered
on rows where neither control did, and in one pass of two the driver's
counters moved where the controls' did not. Claude first read that as a fail
of 23.5's clause "nothing the control does not also show". 1000 behaved the
same by ear, with clean counters in its one pass. Asked whether the data
favours 1000: no - the two are the same by ear and by counter, 500 reads 8%
faster and 1000 takes 6% fewer interrupts, and the only bad pass was one of
two at 500 against one pass at 1000.

**The owner's ruling (2026-09-24): 23.5's gate passes** - the audio works,
nothing dropped out, and the stutter is a limitation rather than a failure.
**The owner kept 500 and made the stutter a known limitation** (2026-09-23): the
release notes, the README's table, the readme template's section 7 and the
`history.md` entry say that on Windows 98 USB audio can stutter while a USB
drive is read at full speed, that it follows the doubled read speed, and that
raising the value towards `4000` or deleting it is the remedy where audio
matters more. The INF's value is unchanged.

### The re-cut

**Dated `09/24/2026` at the owner's instruction** (`XHCI_DRIVERVER_DATE` and
both INFs' `DriverVer`; `build-and-test.md`'s unpadded-date example follows).
Before it, `src\objfre\i386\xhci98.sys` was found to be **not the published
binary**: `915C53EB...`, the same 86,059 bytes, built at 01:00 on 2026-09-23 -
23.8's diagnostic build with the IMOD write compiled out, left there by the
wedge investigation. A `-Force` re-cut restages from `src\obj*`, so it would
have been published. `build-driver.cmd all` and `all -amd64` rebuilt every
flavour, every gate green (the same 5 amd64 compiler warnings as 23.10's
build); `XHCIQUAL.EXE` and `XHCISNAP.EXE` were rebuilt because
`make-release.ps1` refuses tools older than `xhci_version.h`.

`make-release.ps1 -Force`: `releases\1.1.1.0\` and `out\xhci98-1.1.1.0.zip`
(377,827 B), **17 files, each SHA-256 identical to the tracked tree**, no
Microsoft file. The four drivers now read `90DD7823...` (release x86),
`4D8A1A57...` (release x64), `26CE8CDD...` (debug x86) and `FA0E6D16...`
(debug x64), each identical to its fresh `src\obj*` build. **Against the
binaries 23.8's legs and these passes ran, only link metadata differs** - the
PE timestamps, checksums, debug-directory records and, on x64, the PDB
signature: 18, 24, 19 and 24 bytes, every run of them in a header or debug
record. The date is not compiled into the binaries. Code and data are
byte-identical, so no reading here or in 23.8 is re-taken. Nothing has been
uploaded.

**Re-cut once more the same night, for the tuning text** (the owner): the
README's "Tuning", the release notes' moderation section and the readme's
section 9 now say that `500` may produce audio stuttering while a USB drive
is read at full speed, and to raise the value to prioritise audio over
bandwidth; the README's "slow before 1.1.1.0" row was removed. `src\obj*`
was checked identical to the published drivers first. Only `readme.txt`
changed; asset 377,889 B, 17 files, each identical to `releases\1.1.1.0\`.
