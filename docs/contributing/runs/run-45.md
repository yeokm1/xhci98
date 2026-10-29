# Run 45 - Release `3.4.0.0`

Phase 45 of `roadmap-usb4.md`, for release `3.4.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

## 45.5 98 SE's storage record written out before each storage file (2026-10-29)

Commit `e4097713` (reviewed and revised): `stor_place_to` flushes the
temporary copy (`FlushFileBuffers`) and then, after the `Files` or
`Placed` value is written, the registry (`RegFlushKey(HKEY_LOCAL_MACHINE)`)
before `MoveFileA`; `Published` is written out when first set; a flush
that fails stops the placement, and for an INF the publication too.
Design record 26 section 6.5.6 (draft 46). Gated build 3 (x86 and amd64,
every gate, the window self-test 673 checks with no failure) and a test
cut labelled "Release 3.3.0.0 TEST" from it.

**The leg** (development host A, QEMU q35, a fresh 98 SE overlay `w98c`,
the cut's `-nonfree` download with `USBDRV\NUSB33`, 2026-10-29): the
install from the window with the storage row ticked, a gdb stub on
`KERNEL32!MoveFileA` (`0xBFFA1A44`) and the power cut (QMP `quit`)
before the move of `XHCIINST.NEW\USBSTOR.INF`, 44.20's cut b. After a
cold start `HKLM\Software\xhci98\Storage` was whole: `Choice` 1,
`PreExisting` 0, `BeforeOk` 1, `Source` 1, `Published` 1, the five
`Files` values and the two `Placed` ones. `DevicePath` read
`C:\WINDOWS\INF` (the run had not reached its own entry). The next run
read all seven files as this installer's (state 1, case 4, "Installed by
this installer earlier."), and the uninstall deleted the seven and the
`Storage` key. **Pass.**

## 45.7 and 45.8 98 SE and ME: every package staged again, and the page (2026-10-29)

Commits `26973326` and `86fa7cee` (the page's words for the rest in two
lines), then `29d6177b` (ME's limit, below); all reviewed and revised.
Design record 26 section 6.7.13 (drafts 47 and 50). Gated build 3 and its
test cut for 98 SE; gated build 4 for `29d6177b`.

**98 SE, the window** (`w98s`, a fresh overlay, the `-nonfree` download):
the free page with its copy box unticked listed every stageable package
greyed and, below them, "Copied only (no automatic install on this
Windows):" with `rtl815x` (`USBDRV\FREE\net\rtl815x\w98se-me`) and
`noaud`, and "Nothing of this set is copied in this run: tick the box
above to copy it and to choose packages to stage."; End and Home on the
copy box scrolled the list. Ticked, the two lines read "Ticked: staged,
so Windows' hardware wizard finds the driver by itself." and "Not
ticked: copied only; when the wizard asks for a driver, choose Specify a
location and the package's folder under C:\XHCI98\USBDRV\FREE.". Ticking
down the list (Down and Space inside it): `uac2`, `usbmidi`, `uvc98`,
`xpad98`, `cdcnet` and `ax88179` took their ticks, then `usbflop`,
`usbcom` and `libusb0` read "(not available: see below)" with "DevicePath
would pass 280 characters, past which Windows 98 SE's hardware wizard
finds no driver: point the wizard at ... instead."; unticking `cdcnet`
and `ax88179` gave the three back. On the non-free page `ftdi` ticked
and the rest of that set barred the same way. Installed: `DevicePath`
239 characters, `C:\WINDOWS\INF`, `C:\XHCI98` and the folders of `uac2`,
`usbmidi`, `uvc98`, `xpad98` and the non-free `ftdi`, each under
`Other\Free\Staged` or `Other\NonFree\Staged` with the data
`DevicePath`. After a cold start QEMU's `usb-serial` (FTDI 0403:6001)
plugged in: "New Hardware Found", "USB Serial Converter", no wizard
(Windows took `FTDIBUS.INF` from the staged folder by itself), then the
vendor's file copy asking for "FTDI USB Drivers Disk" as in run 43's
leg (cancelled; the non-free drivers themselves are not checked, the
owner's rule of 2026-10-22). QEMU's `usb-serial` enumerates only with a
chardev that opens: with a `null` one it stays unattached and nothing
happens, as record 9 says. The console uninstall took the five folders
out of `DevicePath` one by one and left `C:\WINDOWS\INF`. **Pass.**

**98 SE, an update over an earlier staging** (`w98v`, an overlay of a
98 SE disk with the `3.3.0.0` release installed and its free `uac2`
staged): the free page, its copy box unticked, listed "Staged by an
earlier run: free:audio/uac2 (still offered to Windows); leaving a box
unticked does not undo it; the uninstall removes what this installer
placed and no device uses." and "Nothing of this set is copied in this
run: ... Earlier staging kept is listed above."; ticked, "Ticked: staged,
... ; earlier staging kept is listed above." With every box ticked,
`usbflop`, `usbcom` and `libusb0` were barred; installed, `DevicePath`
278 characters with `uac2` kept and five more folders. **Pass.**

**ME** (`wme`, a fresh overlay, the `-free` download): the page as on 98
SE, its copied-only list `usbmidi` (`USBDRV\FREE\audio\usbmidi\wme`) and
`rtl815x`. Every box ticked fitted 280: installed, `DevicePath` was
exactly 280 characters. At the next cold start ME's "New Hardware Found"
window for "xHCI98 USB 3 Root Hub" stayed at "Windows has found new
hardware and is locating the software for it" for more than twelve
minutes, the guest busy; quit and started cold again, the same. ME0E.1's
stop was not seen. A control install on another fresh overlay (`wme2`,
the free set unticked, `DevicePath` 24) started clean, its root hub
installed. On that control, with `DevicePath` set by hand through
`regedit /s` (the folders copied from the download, or an absent folder
as padding), a QEMU USB keyboard, mouse or tablet plugged in was
installed within seconds and with no wizard at 24, 238 and 259
characters, and stayed at "locating the software" at 260 (padding), 270
(real folders) and 280 (the install's own value). So ME's limit is 259,
and past it ME's wizard stops responding rather than finding no driver,
which holds the root hub too. Fixed by `29d6177b`: the limit is the
system's (`inst_9x_devicepath_max`: 280 on 98 SE, 259 on ME), and a
barred box's reason says "past which Windows ME's hardware wizard stops
responding". The ME leg on the fixed build: below.

**ME on the fixed build** (`wm4`, a fresh overlay, gated build 4's test
cut, `-free`, 2026-10-30): with every free box ticked down the list,
`usbcom` read "(not available: see below)" with "DevicePath would pass
259 characters, past which Windows ME's hardware wizard stops
responding: point the wizard at
C:\XHCI98\USBDRV\FREE\serial\usbcom\w98se-me instead."; installed,
`DevicePath` 235 characters with `uac2`, `cdcnet`, `ax88179`, `xpad98`
and `usbflop`. At the cold start the root hub was installed with no
window left open and the tray tool came up; a QEMU mouse plugged in was
installed silently. The console uninstall took the five folders out of
`DevicePath` (`C:\WINDOWS\INF` left) and removed `C:\XHCI98`. **Pass.**

## 45.6 an exclusion lifted where the devices are apart (2026-10-29)

Commit `26973326` (reviewed and revised): the cut reads the free and
non-free sets' device ids and lifts an `EXCLUDES` between two packages
whose devices are apart on a system (`Get-OtherLifted`, `LIFTED` lines
in the cut's record, `Test-OtherTableLifts` both ways), and the
installer's table carries such a pair as kind 2 (`INST_REL_LIFTED`), which
holds it against an earlier staging of another release. Design record 26
section 6.7.14 (draft 47). The packager's self-tests passed (1700
checks) with the table as it was. The owner's decision of 2026-10-29:
`usb-wdm-free` 0.1.5 splits its `rtl815x` line, `COEXISTS` on XP to 7 x64
and `EXCLUDES` on the rest. The installer's row for the pair, split to
match (`0x1E07` kind 1 and `0x1F8` kind 2, with its host vector), is not
in this branch's commits: with the pin still at 0.1.4 the cut refuses it
("the installer's table marks free:net/rtl815x and nonfree:ethernet/rtl815x
lifted on WXP WXP64 WVISTA WVISTA64 W7 W7X64, and this cut lifts no
EXCLUDES of the pair there"), so it goes in with the pin's move (the
worker's patch `D-45.6.diff`), and the XP and 7 leg after it. **Partial.**

## 45.28 the Components page evenly spaced (2026-10-29)

Commit `f7876c31` (reviewed and revised): `comp_layout` stacks the shown
rows with one gap each, every row's sentence at the height its words
take, and puts the buttons and the folder line at the page's foot.
Design record 26 section 5.3.4 (draft 48). The window's self-test checks
nine variants and every storage note (gated build 3: 673 checks, no
failure). Screenshots of the page from the test cut, nothing installed
(development host A, QEMU q35): 98 SE (with the storage row), ME, 2000,
XP, 7 x86 (with the greyed "Replace the stored build" row) and 10 x86
(the controller row unticked, the Thunderbolt row ticked and greyed, the
Stor98 rows open): on each the rows evenly spaced, no sentence clipped
or overlapping, the buttons and the folder line at the foot. **Pass.**

## 45.12's installer half: `XhciCmBridgeReq` under "Advanced..." (2026-10-29)

Commit `9d6a8290` (reviewed and revised): design record 33 section 9.1's
switch is a row of the Thunderbolt page's "Advanced..." list on 2000, XP
and XP x64 (`INST_APPLY_NT5_CM`), greyed elsewhere with "Windows 2000 and
XP only"; never asked, not written by the INFs, the driver's default 1;
`/set XhciCmBridgeReq=0..2` on those systems only. Host vectors; design
record 26 section 5.5 (draft 49). Gated build 4.

## 45.32 the 98 SE and ME hardware-wizard question only when needed (2026-10-29)

Commit `1084624b` (reviewed and revised): on a system read as English the
first gate looks for a hardware wizard too and asks only when it finds
one or cannot look; elsewhere it asks once as before; `/yes` unchanged.
English: `GetSystemDefaultLangID` and the language of `KERNEL32.DLL`'s
version resource both `LANG_ENGLISH` (both through KERNEL32, so the
console's imports stay KERNEL32 and ADVAPI32; the resource's language is
the build's, which Regional Settings do not move). Before the change,
the ME install above asked the question with no wizard open. Design
record 27 section 6 and H6 (draft 16). Gated build 4.

**Legs** (gated build 4's test cut, fresh 98 SE overlays, 2026-10-30):
with no wizard open (`w9g`) the install asked no question; its log read
"gate: system language 0409, KERNEL32's version resource 0409: English,
the first gate looks" and "gate: no hardware wizard open". With the Add
New Hardware Wizard open (`w9w`, opened by `control sysdm.cpl,Add New
Hardware`) the question came, the log adding "A hardware wizard is open
("Add New Hardware Wizard")." On ME (`wm4`, above) no question with no
wizard open. **Pass.**

## A second xHCI controller asks for the driver disk on 98 SE (2026-10-29, reproduced)

Reported from task 45.10's leg (a 98 SE install with `/thunderbolt`: the
ASM4242's own xHCI controller asked twice for "xhci98 xHCI Host
Controller Disk"). Reproduced here on a fresh 98 SE overlay (`w98x`)
with two QEMU xHCI controllers, the test cut and the default rows: the
helper's job for the first controller installed it ("installed, restart
pending"); its job for the second, the same INF `C:\XHCI98\xhci98.inf`
with `LDID_SRCPATH` set to `C:\XHCI98` exactly as in the first job,
brought "Insert Disk: Please insert the disk labeled 'xhci98 xHCI Host
Controller Disk'" and then "The file 'xhci98.sys' ... cannot be found",
copy from `C:\WIN98`; typing `C:\XHCI98` let it finish. The helper's
result lines of the two jobs are the same to the flags. setupx is one
instance for every process on 98 SE, so its state from the first job's
copy outlives the helper process; the second copy is looked for at the
Windows source. Seen again on `w9w` above (two controllers). Not fixed in
this branch: a fix, a `nocopy` job flag with which the helper sets
`DI_NOFILECOPY` for a later controller of an INF a controller's job of
the same run installed, is drafted, not committed, for a task of its
own. **Reproduced; open.**
