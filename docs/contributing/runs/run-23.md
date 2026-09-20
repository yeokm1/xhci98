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

Status: **the host side is done and the guest readings are owed.** The
Windows 98 line is in `src/xhci98.inf`, the INF gate has a `PROP-*` family
holding it in place, the footprint has learned it, and a before/after package
pair is staged. Nothing here has been read in a guest yet.

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

## 23.1 guest legs - OWED

Not run. The operator drives the GUI and the agent keeps the host side
(monitor, `device_add`/`device_del`, `screendump`), per
`build-and-test.md`, "Who drives the GUI".

Every step names the observation it should produce, so no clause can end up
satisfied by inference.

### Leg A - Windows 98 SE (target `2a`, `vm/win98.img`, monitor 56591)

The post-NUSB guest, which is the one the 2026-09-07 readings were taken on.

**A1. Before.** Install from `E:\T231\BEFORE` (Device Manager -> the xHCI
device -> Update Driver -> Specify a location). Open the controller's
Properties.
*Observation:* the list of tabs, in order. Expected: General, Driver,
Resources - and **no Advanced**. This is the baseline the whole leg is
measured against; if an Advanced tab is already there, stop, because the guest
is not in the state this reading assumes.

**A2. After.** Update Driver again, from `E:\T231\AFTER`. Reboot if the engine
asks. Open the controller's Properties.
*Observation:* the tab list again. Expected: General, **Advanced**, Driver,
Resources, with Advanced between General and Driver. A1 and A2 differ by one
INF line and nothing else, so whatever moves is that line's doing.

**A3. What the checkbox writes.** On the Advanced tab, tick "Disable USB error
detection" and press OK. Then in `REGEDIT`, open
`HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Usb`.
*Observation:* whether the key exists, which values are under it, and the
exact data of `ErrorCheckingEnabled` if it is there. Then reopen the tab,
**untick** the box, OK, and read the same value again.
*What this settles:* the static reading says the name and the key live in
`sysclass.dll` beside the caption. It does not prove the checkbox writes them.
Two read-backs do. Record the value's type as well as its data - if it is a
string rather than a DWORD that is worth knowing.

**A4. Bandwidth Usage, and the `usbui.dll` question.** With the Advanced tab
open, press "Bandwidth Usage".
*Observation:* every line the dialog renders, verbatim - the reserved
percentage, and each device row with whatever speed or bandwidth it is
charged. Then rename `C:\WINDOWS\SYSTEM\USBUI.DLL` in MS-DOS mode, restart
Windows, and press the button again.
*Observation:* whether the dialog still renders, or whether a "Data Access
Error" box appears instead. **Rename it back afterwards.**
*What this settles:* whether `usbui.dll` draws this dialog. The exports and
the adjacent strings say it does; the 2026-09-07 note says it does not. Also
read the Advanced tab itself in the renamed state - the tab is sysclass.dll's
on both accounts and should be unaffected, which is the control.

**A5. The issue 6 clause: root port versus behind a hub.** This is what the
roadmap asked for by name - what the dialog shows for root-port devices
reported as High Speed. Agent drives the monitor.
With a device on a **root port**, read Bandwidth Usage. Then with the **same
device behind a USB 2.0 hub**, read it again.
*Observation:* the reserved percentage and the device's row in each case.
*What this settles:* a Full-Speed device on a root port is reported High Speed
(`docs/issues/06-full-speed-root-port-bugcheck.md`), and `usbui.dll` computes
bandwidth from the speed it is told. So the root-port reading should charge
the device far less than the hub reading charges the same device. If it does,
this dialog is the first place in the UI where issue 6 is visible to a user,
and that belongs in issue 6's record and in the release notes' known
limitations, not just here.

### Leg B - Windows ME (target `2e`, `vm/winme.img`, monitor 56597)

SweetLow's stack only - Windows ME has no other supported configuration here
(`build-and-test.md`, "Windows ME target VM"). Start from the
`winme-sweetlow-driver` snapshot.

**B1. Before / B2. After.** As A1 and A2.
*Observation:* the tab lists. Expected: identical behaviour to Windows 98 SE,
because the module is the same one - see the table above.

**B3. The checkbox.** As A3.
*Observation:* the same key and value. Windows ME's `SYSTRAY.EXE` lacks
`USBErrorMessagesEnable`, so whether the box has any visible effect there is
genuinely open; the registry write is what this step reads.

**B4. Bandwidth Usage.** As A4's first half only - render the dialog and
record it. The rename control is Leg A's; repeating it here buys nothing,
since the two `sysclass.dll` builds agree string for string.

### What closes 23.1

A2 and B2 showing the tab, A3 and B3 naming what the checkbox writes, A4
resolving the `usbui.dll` question, and A5 recording what the dialog says
about a root-port device. Then: the `build-and-test.md` `EnumPropPages`
bullet finished (it was rewritten on 2026-09-20 and carries the guest legs as
owed),
`release-notes.md` gaining the tab, and issue 6 gaining A5 if A5 finds what it
predicts.
