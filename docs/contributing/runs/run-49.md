# Run 49 - Release `3.5.0.0`

The run sheet for release `3.5.0.0` (Phase 49 of `roadmap-usb4.md`:
message-signalled interrupts before Vista, Thunderbolt standby, and the
`3.4.3.0` leftovers). This sheet records what each task did and every
reading; the roadmap's task list and decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

## 49.1 The release legs' own faults (2026-11-11, worker leg)

Branch `w49-leg` from `3.5.0.0` at `5458cd114`. The four faults of `runs/run-48.md`, "48.final", fixed in `scripts\vm-matrix` and read with the affected legs on a package built from the branch.

**Commit.** `6cbb89f24` (vm-matrix: the release legs' own faults of 48.final):

- `leg\LEGNT.CMD` `:wrep`: the window run's exit code is the word after "Exit code " on the log's last such line, wherever the `[hh:mm:ss.mmm]` stamp puts it (it was token 3 of the line, which the stamp made the word "Exit"). Read on the host first with `cmd.exe` over three logs: `[21:49:57.140] Exit code 3. The log is C:\XHCIINST.LOG.` gave 3, an unstamped `Exit code 4. ...` gave 4, a log without the line gave `unread`.
- `leg\LEGNT.CMD`, the stick: no drive is looked at until the stick's volume install is finished in Windows' device-install log (the existing settle, `setupapi.log` / `setupapi.dev.log`, moved ahead of the search for `TESTDATA.BIN`; its bound 300 s from the plug request instead of 200 s from the drive's letter); a fault before the drive is found fails `stick-seen`.
- `lib\leghost.ps1` `wizard98-hcd` (98 SE): the poller's `XHLEG BOOT` ends the step at once at every wait before the wizard's last page, the screen waits included (`Wait-LegScreen -Until`, a new optional scriptblock checked on every turn of both loops): in 48.final the step waited 300 s for a last page that never came while the guest's `WANT plug-stick` stayed unread.
- `lib\leghost.ps1` `Get-LegWavChk`: `WAVCHK.EXE` is built with `wavchk\build.cmd` when it is missing (a tree made for a cut has none; the EXE is not tracked), and the leg is refused only when that build fails.
- Docs: `build-and-test.md` ("Faster emulator legs": the stick bullet, the playback check's last sentence, the 98 SE wizard step), `scripts\vm-matrix\README.md` (`wavchk/` row), `run-leg.ps1` synopsis, `wavchk\build.cmd` header.

**Review.** Two rounds on the uncommitted change. Round 1: one P2 (the step's first `Wait-LegScreen`, up to 300 s, and the waits between Next keys read no COM1, so a `BOOT` could still go unread): fixed with `-Until`. Round 2: no material findings. Then committed.

**Build.** One gated x86 release build of `6cbb89f24` (slot `gated.1`, `XHCI98_SELFTEST_SKIP_OK=1`), 00:40 to 00:51: "BUILD + GATES PASSED (release)", exit 0 (`E:\xhci98-scratch\w49\leg\build-release.log`); `xhciinst\di`, `xhciinst`, `xhcimark`, `xhciperf` built and `make-package.ps1 -Flavor release -Arch x86` exit 0 (`tools.log`). Kit `E:\xhci98-scratch\w49\leg\kit\PKG`: family layout (`ConvertTo-FamilyLayout`), `tools\`, `SETUP.EXE` and `INSTALL.BAT` at the top, and the `3.4.3.0` download's `USBDRV\NUSB33\` (98 SE's storage); `PKGNF` the same with the `3.4.3.0` `-nonfree` download's `USBDRV\` (`b15dd972...`). `xhci98.sys` (both families) `a9c79ef2...`, `xhciuas.sys` `402465e8...`, `SETUP.EXE` `4edcb9b2...`, `XHCIINST.EXE` `a16329e3...`, `XHCITR.EXE` `f3596fc1...` (`kit\kit-sums.txt`). The tree's version stamp is still `3.4.3.0`.

**Legs.** Development host A, QEMU 11.1.2 `v11.1.2-244-g4b495b9` (`asm4242-20261027-fwpad`), TCG, `run-leg.ps1` with `scripts\vm-matrix\release.config.psd1` (git-ignored; WorkDir `E:\xhci98-vm\p49\leg\rm`, ports 49600-49617), the starting points of `p48\legs343` copied in (hardware hash checked, unchanged). Each leg on a fresh overlay; `-KeepDisk`.

| Leg | Package | Verdict | What the fix shows |
|---|---|---|---|
| `w98se-window-1` | PKG | **PASS** (821.6 s): install 3, list 0, uninstall 3, list-final 0, every check PASS | `wizard98-hcd: no wizard, the poller up (the controller had its driver)`; the stick plugged at once, `stick-seen PASS E:`, the stick's wizard walked (5 pages) |
| `w98se-console-1` | PKG | **PASS** (627.4 s), the same exits | the console install now gives the controller its driver too (exit 3, no wizard at the next start): `wizard98-hcd` "no wizard, the poller up" |
| `w2k-window-1` | PKG | **PASS** (323.1 s) | `XHLEG EXIT install 3` (48.final: `Exit`) |
| `xp-window-1` | PKG | **PASS** (529 s; 3 Found New Hardware pages answered) | `EXIT install 3` |
| `w7-window-1` | PKG | **PASS** (967.1 s; the unsigned-driver question twice) | `EXIT install 3` |
| `w2k-console-1` | PKG | **PASS** (323.5 s) | `WAVCHK.EXE` built by the leg (01:06:59, 45,056 bytes, `8b7a303b...`; none in the tree before); `audio-play` PASS, 2000 ms in 2083 ms, `urbs-malformed` PASS |
| `w98se-window-2`, `-3` | PKG | **PASS** (715.6 s, 602.5 s) | as `-1` |
| `w2k-nonfree-1` to `-10` | PKGNF | **10 of 10 PASS** (265.5, 227.6, 227.3, 241.3, 227.4, 256.9, 218.4, 229.3, 204.7, 211.3 s) | each `XHLEG NOTE stick settle: 5 checks` (one 4), `STORAGE\ lines 1 then 2`, then `stick-seen PASS E:`, `stick-copy PASS`, uninstall 3: no restart box. 48.final: 3 of 6 on `3.4.3.0`, 4 of 5 on `3.4.2.0` |

On XP the settle read `STORAGE\ lines 2 then 3`, on 7 `56 then 62`, both in 5 checks. Not exercised: the 98 SE wizard step's new exits after a `wizard` page was recognised (neither 98 SE start raised the controller's wizard), so the 48.final path itself (a wizard seen, then the poller up) is covered by the review, not by a leg.

Evidence: `E:\xhci98-vm\p49\leg\rm\runs\<leg>-<n>\` (`result.json`, `serial-b*.log`, screendumps, the kept disks), the queues' logs `E:\xhci98-vm\p49\leg\rm\queue*.log`. QEMU processes: `E:\xhci98-vm\p49\leg\rm\pids.txt`.

## 49.3 usb-wdm-free 0.1.10: the release prepared, the pin provisional (worker wdm, 2026-11-11)

usb-wdm-free main was still `6a266c7` (not moved since the brief); branch
`w48-rel0110` continued from `b352b60` (the version bumps). xHCI98 branch
`w49-wdm` from `3.5.0.0` at `5458cd114`.

### What changed since `v0.1.9`, against the bumps

`git diff --stat v0.1.9 6a266c7 -- src`: changed sources in `audio/common`
(uac2, uac1; `usbmidi` includes only `audio/common` headers it does not
use from the changed set), `audio/uac1`, `audio/uac2`, `net/ax88179`,
`net/ax88772`, `net/cdcnet`, `net/rtl815x`, `net/sr9700` and
`video/uvc98`; nothing else. `b352b60` bumps exactly those eight: uac2
0.1.11.0, uac1 0.1.1.0 (INF; its `.rc` read 0.1.1.0 since `a27d075`),
rtl815x 0.2.2.0, ax88179 0.1.4.0, ax88772 0.1.1.0, sr9700 0.1.1.0, cdcnet
0.1.4.0, uvc98 0.1.3.0. The INFs of xpad98 and absptr in `src\` equal
0.1.9's released bytes.

### Builds (development host A)

`src\audio\uac2\build.cmd wdmx86|wdm64 free`, `src\audio\uac1\build.cmd
wdmx86 free`, `src\net\<pkg>\build.cmd ndis5|ndis51 free` (ax88179 ndis5
only: it has no x64 folder), `src\video\uvc98\m2build.cmd free`, at
`b352b60`, clean tree: every build and gate PASS
(`E:\xhci98-scratch\w49\wdm\bldfree.log`).

### The definition and the notes (`9552368`, reviewed)

`release\0.1.10.psd1`: the eight packages from the new builds (uvc98's
from `m2build`, `out\video\uvc98\m2x86`), `usbmidi`'s 9x INFs, `xpad98`'s
and `absptr`'s images from 0.1.9's tree (`tree:`, their x64 images
`strip:`), every STAGE note of a changed package naming its 0.1.10
change; uac1 stays a `COPY` line (owner, 2026-11-09). Release notes
"Release 0.1.10" in 0.1.9's one-line style; README names `v0.1.10`.
`release.ps1 -Part x86 -Check`: PASS.

### The x64 images for the owner

`E:\xhci98-scratch\w49\wdm\rel0110\`: `x64-to-sign\` (the ten unsigned
images the definition's `X64Builds` name: uac2wdm.sys, RTL815X.SYS,
AX772F.SYS, SR9700F.SYS, CDCNET.SYS, two folders each),
`x64-signed-0.1.9\` (0.1.9's signed images of the unchanged packages,
which `release.ps1` takes as they are for the `strip:` entries),
`SHA256-x64.txt`, `OWNER-STEPS.txt`.

### The provisional tree and the pin

An x86-only tree (`-Part x86`) fails the cut's checks (`Invoke-OtherSetChecks`:
"free:net/rtl815x and nonfree:ethernet/rtl815x were excluded on W7X64 by
3.4.2.0 ... has no line for W7X64"). So a scratch clone of `w48-rel0110`
(`E:\xhci98-scratch\w49\wdm\wdmfree-prov`, never pushed, not tagged) took
the whole release with the unsigned x64 builds (its `release.ps1` told, in
that clone only, to note instead of refuse an unsigned x64 image): commit
`30bc6e4`, 137 files. `wdmfree-pin.txt` names `v0.1.10` with that commit
and every SHA256 line from it (141), and a PLACEHOLDER block saying what is
written again from the owner's tag; `INST_WDMFREE_SUMS`
`4B3F07DD24ADD3B3812593A5BED9427FB0763DA28334BB066C5C7557E339BE6C`, marked
provisional. `Export-WdmFreeSet` over the clone (the commit resolved
directly by a scratch harness, `pinchk.ps1`, since there is no tag) gives
that sum, and the cut's checks hold over it with usb-wdm-nonfree `v1.3.0`
(`LIFTED | free:net/rtl815x | nonfree:ethernet/rtl815x | W7 W7X64 WVISTA
WVISTA64 WXP WXP64`, as before). `make-release.ps1` now refuses a cut
while the pin keeps its PLACEHOLDER block. The installer's rows need no
change: 0.1.10's PACKAGE lines are 0.1.9's words, the relations are kept;
uac1 has no box on 98 SE now (a COPY line) and `/wdmfree:stage=audio/uac1`
is refused with the set's reason. xHCI98 commit `d3e050d14` (reviewed).

### The 98 SE "Insert Disk" side finding (49.5's leg)

Cause, read from the records: 98 SE's setup engine, installing from an INF
found on `DevicePath` (the installer's 98 SE staging), looks for a driver
file not yet in place at Windows' `SourcePath` (`C:\WIN98`), not in the
INF's folder (`runs/run-43.md`, "The 98 SE Insert Disk prompt, read
first"; record 26 section 6.7.9 owes it). It is the same for staged uac2
and usbmidi (`runs/run-44.md`, the 98 SE device leg) and was for
`xhciuas.inf` before `3.2.0.0`. uac1's INF is not at fault; its disk NAME
in 0.1.9 was "UAC1 experimental build folder", which usb-wdm-free main
already names "usb-wdm-free uac1 driver" (`8d7411c`, owner, 2026-11-09),
so 0.1.10 carries the owner's words. With 0.1.10 uac1 is copy-only, so the
installer no longer stages it; Update Driver pointed at its folder takes
the files from there. The general fix for staged uac2 and usbmidi is an
owner question (QUESTIONS.md Q2).

### The legs on the provisional pin (2026-11-11, 04:31 to 05:23)

The kit: `d3e050d14`'s gated x86 release build (`E:\xhci98-scratch\w49\wdm\gated-release.log`,
"BUILD + GATES PASSED (release)", slot 1, `XHCI98_SELFTEST_SKIP_OK=1`), the tools and
`make-package.ps1 -Flavor release -Arch x86` of the same tree, laid out by family
(`E:\xhci98-scratch\w49\wdm\kit\PKGF`, `kit-sums.txt`): `xhci98.sys` `0f56695c...`,
`XHCIINST.EXE` `4534ff31...` (it carries `4B3F07DD...`, not `553C5CA4...`),
`SETUP.EXE` `09d62577...`; `USBDRV\NUSB33` from the `3.4.3.0` base download (as the
49.1 kit), `USBDRV\FREE` the export of the provisional pin (its `SHA256.TXT`
`4B3F07DD...`). The release matrix's runner (`run-leg.ps1`, the 49.1 starting points
copied to `E:\xhci98-vm\p49\wdm\rm\start`) with three legs of a scratch legs file
(`E:\xhci98-scratch\w49\wdm\free-legs.psd1`, after `release-legs.psd1`'s nonfree legs):

| Leg | Install switches | Result |
|---|---|---|
| `w98se-free-1` | `/wdmfree:copy` | PASS 507 s: "143 of its 143 files are in C:\XHCI98\USBDRV\FREE"; uac1 "copied only on this Windows. Install it by hand from C:\XHCI98\USBDRV\FREE\audio\uac1\w98se"; every check PASS, exits 3/0/3/0 |
| `w2k-free-1` | `/wdmfree:stage=net/cdcnet,net/rtl815x,net/ax88179,net/ax88772,net/sr9700,video/uvc98,audio/usbmidi` | install PASS (143 of 143, 7 INFs staged); the uninstall exit 4 ("Windows is installing something"), a System Settings Change restart box after the stick's install behind the Unsafe Removal box (`b2-sig2k-043407.png`), so `list-removed` FAIL |
| `w2k-free-2` | the same, a fresh overlay | PASS 218 s, every check, exits 3/0/3/0: not reproduced |
| `xp-free-1` | `/wdmfree:stage=audio/uac2,net/cdcnet,net/rtl815x,net/ax88772,net/sr9700` | install exit 1 with no log: the harness's run folder had a literal `$L` in its name (my quoting slip); not counted |
| `xp-free-2` | the same | PASS 330 s: 143 of 143, 5 INFs staged, every check, exits 3/0/3/0 |

Two more readings by hand (`E:\xhci98-vm\p49\wdm\kit\g.py`, from the 49.5 leg kit):

- **2000, a 0.1.10 driver bound from the pin** (`E:\xhci98-vm\p49\wdm\s2k`, QMP 49618):
  the kit with `/wdmfree:stage=net/cdcnet` (the first run exit 4 for a PCI device's
  Found New Hardware window, walked with Skip; the second "free:net/cdcnet staged",
  the Digital Signature box answered Yes, "Done. Please restart Windows now."), shut
  down, cold start, QEMU `usb-net` plugged at port 2: the Digital Signature box for
  "USB Ethernet (Linux USB gadget)", Yes; then `oem2.inf` `DriverVer=11/10/2026,0.1.4.0`,
  `cdcnet.sys` 28,960 bytes (0.1.10's), setupapi.log "Copying file
  C:\XHCI98\USBDRV\FREE\net\cdcnet\w2k-xp\cdcnet.sys", and `ipconfig` "USB Ethernet
  (Linux USB gadget)" with its MAC (`serial-b.log`, `b-5.png`).
- **98 SE, uac1 copy-only** (`E:\xhci98-vm\p49\wdm\s98`, QMP 49619, a fresh overlay):
  `XHCIINST /dir 98-me /console /yes /nopause /nostor98 /storage /wdmfree:stage=audio/uac1`
  "Cannot continue: free:audio/uac1: its set stages nothing for it on this Windows, so
  it is only copied (use /wdmfree:copy, then point Windows' hardware wizard at the
  package's folder). Nothing was changed." exit 1; then `/wdmfree:copy` exit 3 (its
  Finish names `C:\XHCI98\USBDRV\FREE\audio\uac1\w98se`; COM1 held by the first DOS
  box, answered Ignore/Fail, as in 49.5), shut down, cold start, QEMU `usb-audio`
  hot-added: the Add New Hardware wizard for "USB Audio Device", Search, Specify a
  location `C:\XHCI98\USBDRV\FREE\audio\uac1\w98se`: "The updated driver
  (Recommended) USB Audio 1.0 (uac1)", Next, Finish: **no Insert Disk prompt**
  (`b-7.png` to `b-11.png`); `UAC1WDM.SYS` 57,824 bytes (0.1.10's) in
  `SYSTEM32\DRIVERS`.

QEMU processes: `E:\xhci98-vm\p49\wdm\rm\pids.txt` (the runner's, 15 started and 15
exited) and `E:\xhci98-vm\p49\wdm\pids.txt` (30976, 45644, 42460, 56472, 57112); each
shut down from inside or quit by QMP at the halt; none left running.

**Not merged (coordinator, 2026-11-11).** The merge review of `w49-wdm` found that a pin naming an
untagged commit stops the packager's self-tests on any host whose `scripts\local` holds both driver-set
checkouts (`test-package.ps1` exports the default pin, and the export requires the tag to resolve to the
pinned commit), so the routine gated build would fail there before compiling. The branch stays as it is
until the owner's tag; 3.5.0.0 keeps the `v0.1.9` pin.


## 49.4 The Windows 10 release legs under WHPX beside TCG (2026-11-11, worker leg)

Development host A. Nothing on the host was changed or enabled.

**Is WHPX usable.** Yes, as the host stands. Both emulator builds used (`q35-bases\bin\asm4242-20261025-2230`, `da60139f...`, and `asm4242-20261110-docks`, `4959a9ab...`, QEMU 11.1.2) list `tcg` and `whpx` (`-accel help`); a headless q35 machine started under `-accel whpx` with no error but QEMU's own "Ignoring request for interrupt vector 0" warning (`E:\xhci98-scratch\w49\leg\whpx-probe.txt`), and a 10 x86 overlay reached the desktop in under 90 s (`E:\xhci98-vm\p49\leg\whpx1\boot-2.png`; TCG takes about 8 min to the same point).

**Save and restore (the starting points).** Not possible under WHPX. On that 10 x86 guest, QMP `migrate` (the release matrix's `Save-LegState`) was refused: first by the hot-plugged `usb-uas` ("State blocked by non-migratable device ... usb-uas", the device, not the accelerator; the matrix's starting points carry no USB device), then, with the stick and the UAS disk unplugged, by the accelerator: "State blocked due to missing dirty memory tracking support, And some system register/state save-restore". So no starting point can be saved under WHPX with this QEMU, and the release matrix (98 SE to 7) stays on TCG. The 10 legs are not matrix legs: they are 47.final's cold-start kits (`rlx86` for 10 x86, `rlx64` for 10 x64), which need no saved state, and those are what ran below.

**qemu-xhci and the UAS stick under WHPX.** Both work as under TCG: the controller, root hub, hub, composite device and mouse on `xhci98` at Code 0, the Bulk-Only stick read, written and read back at every step; the UAS disk enumerated and, on 10 as documented, on Windows' `UASPStor` at Code 10 with no volume ("UASMX NOTSEEN", "UAS NODRIVE"), the same under both accelerators.

**The legs.** The same package for both, the published `3.4.3.0` download (`E:\xhci98-release\3.4.3.0\xhci98-3.4.3.0.zip`, `cc0ab19e...`, its x64 half signed by the owner's test certificate, trusted in the guest as on 48.final); copies of the kits in `E:\xhci98-vm\p49\leg\k86` (K343) and `k64` (K343), with one addition each: `--accel` passed to the launcher (`k86\scripts\launch-rlx.ps1 -Accel`, and for x64 `E:\xhci98-vm\p49\leg\kit\launch-q35w.ps1`, a copy of the q35 launcher with `-Accel`). Same emulator per OS for both accelerators (x86: `asm4242-20261025-2230`; x64: `asm4242-20261110-docks`), `-cpu qemu64,+3dnowprefetch -smp 2`, 2 GB (x86) and 4 GB (x64); TCG as the kits had it (`tcg,thread=multi`). Fresh overlay of the q35 base each. Each leg: setup (the kit, the certificate, test signing), `INSTALL /console /yes /nopause /usb`, a cold start with readings and copies, the controller disabled, enabled, removed and rescanned with readings and copies after each, the uninstall (run again on exit 4), the readings after the next cold start. The x64 kit's image line still says "SAME AS 3.4.2.0": the label is the kit's, the comparison is against K343 (3.4.3.0).

| Leg | Accel | Wall time | Phases | Verdict |
|---|---|---|---|---|
| `w10x86-T1` | TCG | 76.6 min (00:48:31 to 02:05:10) | setup 10, install 28, devices 17, uninstall 10, readings 9 min | install exit 3, `xhci98.sys` and `xhcihlpr.sys` SAME, controller tree on `xhci98` ("Keep (same version)"), stick PASS after the start, the enable and the rescan; disable and enable done; **remove "done"** (no restart): Windows gave the controller to `USBXHCI` before the rescan; uninstall exit 3; at the end `USBXHCI`, no xHCI98 file or service |
| `w10x86-W1` | WHPX | 21.0 min (00:50:32 to 01:11:33) | 9, 2, 5, 1, 1 | the same readings, but **remove "done, Windows asks for a restart"**: the tree stayed on `xhci98` through the rescan (48.final's TCG row read the same); uninstall exit 3; at the end `USBXHCI` |
| `w10x86-W2` | WHPX | 21.0 min (02:12:05 to 02:33:02) | 9, 2, 5, 1, 1 | as W1, the restart asked at the remove |
| `T1-w10x64` | TCG | 116.4 min (00:48:30 to 02:44:55) | setup 10.5, install 13.9, readings and the sequence 70.3 (of which the uninstall's "Removing the INF oem2.inf" 46.7 min, 01:35:26 to 02:22:11), 16.1, 4.7 min | install exit 3; every device Code 0, `xhci98.sys` SAME, signature verified; stick PASS three times; disable Code 22, enable; **remove "ok"** (no restart flag), the controller then on `USBXHCI`; the uninstall in the same start exit 3; at the end `USBXHCI` |
| `W1-w10x64` | WHPX | 28.3 min (00:50:32 to 01:18:49) | 10.3, 2.2, 8.2, 3.0, 2.8, 0.7 min | as 48.final's TCG row: Code 0, SAME, verified, stick PASS three times, disable Code 22, **remove `DI_NEEDREBOOT`**, uninstall exit 4 ("removed, restart pending"), then exit 3 after a cold start; at the end `USBXHCI` |
| `W2-w10x64` | WHPX | 28.0 min (02:12:05 to 02:40:06) | 10.4, 2.2, 8.0, 3.0, 2.8, 0.7 min | as W1 |

The setup times are mostly the kits' fixed waits for the desktop (480 s and 540 s), which do not shrink under WHPX. 10 x64's TCG pace is 48.final's again (88 and 104 min there); this run shows where most of it goes: the uninstall's removal of the package's INF from the driver store took 46.7 min under TCG and under a second under WHPX.

**Where the two disagree.** One step, the controller's remove: on TCG tonight it went through with no restart on both systems, under WHPX it asked for a restart all four times. The same kit on TCG asked for the restart at 48.final on both systems, so that step varies between TCG runs too (47.final's reading: Windows' audio engine holding the endpoint open); tonight's split (both TCG legs with no restart, all four WHPX legs with one) is therefore not explained, and whether the accelerator plays a part is open. Every other reading is the same. Under the task's rule the remove keeps a TCG reading; whether the 10 legs move to WHPX is the owner's, since it reverses part of the decision of 2026-10-20 (recommended: the 10 x86 and 10 x64 cold-start legs on WHPX, every other guest and every starting point on TCG, a complete TCG leg kept as a periodic cross-check).

Evidence: `E:\xhci98-vm\p49\leg\k86\legs\w10x86-{T1,W1,W2}\` (`leg.log`, `an.txt`, `serial-*.log`, `shots\`), `E:\xhci98-vm\p49\leg\k64\legs\{T1,W1,W2}-w10x64\` (`leg.log`, `summary.txt`, `serial-b*.log`, `shots\`), `E:\xhci98-vm\p49\leg\whpx1\`. QEMU processes: each leg folder's `pids.txt` (k86) and `E:\xhci98-vm\p49\leg\k64\pids.txt`.

## 49.5 The staging legs left: 2000's disable with copies in flight, the stress, UAS and audio together on 98 SE (2026-11-11, worker leg)

Readings only. The package of 49.1 (`6cbb89f24`, `kit\PKG`; `xhci98.sys` `a9c79ef2...`), `XhciDmaStaging` 0 as the INF writes it, and 1 written for the legs at 1. Development host A, QEMU 11.1.2 (`asm4242-20261110-docks`, `4959a9ab...`), TCG, q35, `-display none`, one processor each; fresh overlays of the q35 bases.

### 2000: what a disable does while files on the stick are in use

2000 SP4 (`E:\xhci98-vm\p49\leg\s2k`, QMP 49616), a Bulk-Only stick and a UAS disk on `qemu-xhci` at power-on (two overlays of `q35-bases\shared\nvme-fat.qcow2`, E: and F:), the kit `E:\xhci98-scratch\w49\leg\k2k` as the transfer disk (stg2k's `k2k` scripts, `devcycle.exe` from `src\stor98\tests\devcycle`, and new ones: `DO.BAT`, `DH.BAT`, `CH.BAT`, `CHV.BAT`, `ST.BAT`, `SL.BAT`, `SPF.BAT`, `STG.VBS`). Install `INSTALL.BAT /console /yes /nopause /nostor98`: the Found New Hardware wizards answered, "xHCI98 USB 3 Controller (Red Hat QEMU xHCI): xhci98 3.4.3.0, started", "xHCI98 USB Attached SCSI Storage: xhciuas 3.4.3.0, started", exit 3; then a cold start. Copy-compare (24 MiB, 3 rounds each) E: and F: PASS before every reading below.

`devcycle disable` is SetupAPI's property change (`DIF_PROPERTYCHANGE`, `DICS_DISABLE`), as Device Manager's Disable; it reports `ok (0)` when the call succeeds with neither `DI_NEEDREBOOT` nor `DI_NEEDRESTART`.

| Reading | Staging | Result |
|---|---|---|
| idle: the disable with nothing open on the stick | 0 | `ok (0)` and the controller's tree gone at once (`devcycle list USB` empty); enable `ok (0)`, everything back |
| `DO.BAT E`: `copy /b` of 24 MiB to E: and back in a loop (the file held open for each copy), the disable asked once 10 s in | 0 | `disable ... ok (0)` after 4.3 s; the stick still listed at every look for 60 s, also for the 49 s after the loop had ended (12 rounds, all PASS); never disabled: 2000 refused the disable silently (no restart flag, nothing in `setupapi.log`) and did not carry it out later |
| the same | 1 | the same: `ok (0)` after 6.1 s, the stick listed 60 s on; 12 rounds PASS |
| `DH.BAT E 3000 20`: 3000 copies of a 1 MiB file to E: (40 names in turn), each its own `copy`, so no handle on the stick is held from one to the next; the disable asked every 7 s while the stick is still listed | 0 | tries 1 to 13 (01:40:27 to 01:41:51, during the writes): each `ok (0)`, the stick still listed; the writer ended at 01:41:54; try 14 (01:41:56) landed at once; the drive then gone ("The system cannot find the path specified"); enable `ok (0)`; the 40 chunk files all compared equal (`CHV E pass 40 fail 0 missing 0`), copy-compare 3 rounds PASS |
| the same, `DH.BAT E 3000 30` | 1 | tries 1 to 12 during the writes refused the same way; the writer ended at 01:56:25; try 13 (01:56:27) landed; 40 of 40 chunks equal, copy-compare PASS |
| `DH.BAT E 600 20` and `E 120 20` (shorter streams that ended before the first try) | 0 | the first try landed; 40 of 40 and 120 of 120 chunks equal. (In the 120-chunk run the 120 MiB left on the stick then made the copy-compare fail for want of space: 7 MB free, read with `dir`; after the chunks were deleted, 3 rounds PASS) |

So in these readings no disable landed on 2000 while the stick was in use: while a file on it is open, or while it is being written (copies of separate 1 MiB files back to back, with no handle held from one to the next, refused all 13 and 12 tries for as long as they ran, and the disable landed within 2 s of the last one), Windows 2000 refuses it and says so to no one (SetupAPI returns success with no restart flag; Device Manager makes the same call), and the request is dropped, not deferred: it does not land later on its own. It lands when the volume is idle, and then every chunk written before it was on the stick intact. A disable cutting a copy off mid-transfer was therefore not observed, and is not ruled out under other timing; that path stays unread, and what happens to transfers in flight was read by a surprise removal instead (the hot unplug: 48.33 at 0, and below at 1). 48.33's first 2000 reading at 0 had a disable reach a copy-compare loop between its copies ("The specified network resource or device is no longer available" in its ninth round): that loop spends seconds comparing on C: with nothing open on the stick, a gap the back-to-back copies here never left.

### 2000: the stress

`ST.BAT 20 3`: copy-compare loops of 24 MiB on E: and F: at once (20 rounds each, each loop in its own window with its own result file), and three XHCIPERF runs on E: beside them (`/test seq,1024,4,1 /test seq,64,4,1`, 32 MiB, 3 passes, 8 s), then the device list and a copy-compare round. The value read in XHCITR's dump (`stage.switch`).

| Staging | Loops | XHCIPERF on E: (MB/s, 1 MiB write / read; 64 KiB write / read) | After |
|---|---|---|---|
| 1 (`stage.switch=00000001`, `stage.pool.slabs=00000020`, `stage.pool.short=00000000`, `stage.windows=00000004`) | E: 20 of 20 PASS, F: 20 of 20 PASS (02:04:50 to 02:06:40) | beside the loops 152.9 / 131.2; 11.8 / 10.8; alone 606.0 / 684.2; 233.7 / 235.3 and 701.1 / 717.4; 200.6 / 210.2 | every device listed; copy-compare E: and F: 3 of 3 PASS |
| 0 (`stage.switch=00000000`) | E: 20 of 20, F: 20 of 20 PASS (02:18:47 to 02:20:10) | beside 167.8 / 165.2; 13.3 / 259.2; alone 970.4 / 1085.9; 345.7 / 353.0 and 1129.7 / 1167.8; 279.8 / 266.0 | the same |

**The stress with a hot unplug, at 1.** `ST.BAT 30 1` again (`stage.switch=00000001`), and 40 s in the Bulk-Only stick (F:) removed by QMP `device_del` (03:42:52): F:'s round 5, in flight, failed at 03:42:57 and every later round at once ("The system cannot find the drive specified"; 4 PASS, 26 FAIL of 30); the UAS disk E: went on, 30 of 30 PASS, with XHCIPERF beside it to its end (828.3 / 739.4; 8.1 / 280.4 MB/s); Windows' own "Unsafe Removal of Device" box (USB Mass Storage Device, QEMU QEMU HARDDISK USB Device); the device list then without the stick, everything else there; no hang. The stick plugged back (`drive_add`, `device_add` on port 1): copy-compare E: and F: 3 of 3 PASS; `stage.pool.short=00000000` in the dump after it. (48.33 read the same unplug at 0.)

No error line in any XHCIPERF report, no hang, no device lost. The alone figures read staging's copy cost on 2000 here at about 35 to 40 % for 1 MiB transfers (one emulated processor, so the copy is in the way; the bench is what says what a real processor leaves).

Evidence: `E:\xhci98-vm\p49\leg\s2k\` (`serial-a.log` the install, `serial-b.log` to `serial-f.log` the readings, `g.log` every step, `*.png`). QEMU processes: `E:\xhci98-vm\p49\leg\pids.txt` (40440, 34424, 40944, 16544, 36772, 8884, 47584, 41952; each shut down from inside, then quit by QMP at the halt: this 2000 HAL does not power off).

### 98 SE: a UAS disk and USB audio together

98 SE (`E:\xhci98-vm\p49\leg\s98`, QMP 49617), one processor, 512 MB, no USB device at power-on (both hot-added), the kit `E:\xhci98-scratch\w49\leg\k98` as the transfer disk: the package of 49.1 with the `3.4.3.0` `-free` download's `USBDRV\` (NUSB 3.3 and `FREE\`), WAVCHK (built by 49.1's leg), a 4 s 48 kHz stereo tone, and batch files `UA.BAT`, `AU.BAT`, `LP.BAT` (stage's `k98` loop, 20 rounds). QEMU's `usb-audio` stops 98 SE's own `usbaudio.sys` (Phase 9), so the audio went to usb-wdm-free's `uac1` from the `-free` download, the one route a user has: `XHCIINST.EXE /dir 98-me /console /yes /nopause /nostor98 /storage /wdmfree:stage=audio/uac1` ("free:audio/uac1 staged", exit 3), Shut Down, cold start.

- **UAS on 98 SE.** `usb-uas` with a `scsi-hd` hot-added, then `qom-set .../uas1 attached true` (lessons.md, "The same, again, read as a Full Speed gap"): SuperSpeed, the wizard "xHCI98 USB Attached SCSI Storage" walked, drive E: (`Orico`, `TESTDATA.BIN`).
- **USB audio on 98 SE.** `usb-audio` hot-added on port 3: the wizard found "UAC1 USB audio (experiment)" by itself but then asked for its files: "Insert Disk: Please insert the disk labeled 'UAC1 experimental build folder'", then "The file 'uac1wdm.sys' on UAC1 experimental build folder cannot be found", Copy files from `C:\WIN98`; with `C:\XHCI98\USBDRV\FREE\audio\uac1\w98se` typed there it installed (`b-5.png`, `b-6.png`). Not this task's subject; reported for the installer's 98 SE staging of usb-wdm-free packages (STAGE.TXT says of `uac1`: "staged, opt-in ... no leg of that build").
- A first combined run (`serial-b.log`) was lost to a fault in my batch file (98 SE's `COMMAND.COM` has no `GTR`) and to COM1 held by another DOS box; it is not counted.
- Each reading below after a cold start with both devices hot-added again; WAVCHK `/name USB` found "QEMU USB Audio" at once each time. A play passes when it ends within twice its length plus 10 s (18 s).

| Staging | Run | Copies (20 rounds of 24 MiB, C: to the drive and back, `fc /b`) | Plays (8 of 4 s) |
|---|---|---|---|
| 0 | audio alone | - | 8 of 8 PASS, 4006 to 4032 ms |
| 0 | copies to the UAS disk E: and audio at once | 20 of 20 PASS | 4 of 8 PASS (8075, 7953, 4781, 4797 ms); 4 not done at the bound, the position at 78 to 93 % (`602496` to `713856` of `768000` bytes) |
| 0 | control: the same copies on C: alone (no USB transfer but the audio's) and audio at once | 20 of 20 PASS | 4 of 8 PASS (9441, 4172, 4216, 4227 ms); 4 not done, the position at 1 to 31 % |
| 1 (`stage.switch=00000001` in XHCITR's dump) | copies to E: and audio at once | 20 of 20 PASS | 7 of 8 PASS (8020, 7210, 4533, 17105, 17578, 4823, 4557 ms); 1 not done at 25 % |
| 1 | audio alone | - | 8 of 8 PASS, 4006 to 4073 ms |

So on 98 SE the UAS disk and USB audio work together through `xhci98.sys` at both values: every copy compared equal, no device was lost, no hang. The plays that fell behind did so as much or more with the copies on C: alone (read at 0 only), which points to the `COMMAND.COM` copy loop's processor load on one emulated processor (98 SE gives the DOS box's loop the processor) rather than to the USB disk's transfers; that is a likely cause, not a proven one, and a USB or staging share in it is not ruled out. Alone, every play kept time at both values. The emulator's 98 SE is no measure of real-time behaviour on a real processor.

Evidence: `E:\xhci98-vm\p49\leg\s98\` (`serial-a.log` the install, `serial-b.log` to `serial-e.log` the runs, `g.log`, `*.png`). QEMU processes 40636, 31368, 41340, 36968, 33200 (`E:\xhci98-vm\p49\leg\pids.txt`), each shut down from inside.

## 49.8 `usb-wdm-free`: ME's 44.1 kHz recording refused at the capture gate (2026-11-11, w49-uac)

usb-wdm-free, branch `w49-uac` from `main` `6a266c7`, worktree
`E:\usb-wdm-free-wt\w49-uac`. Not merged, not pushed. Separate from
`w48-rel0110` so it can go into 0.1.10 or the release after.

**The cause, by reading.** `UacStream::Create` (`src/audio/common/uacstream.h`)
takes the direction's one-stream gate (`UAC_DIR.Active`) and only the
stream's destructor frees it, after the transport's `Close` hook. A
published stream's final `Release` never destroys inline: it queues the
destroy to a work item (`DelayedWorkQueue`; on x86 an executive work item
behind `UacQueueItem`, `uacwdm.cpp`). Between that release and the work
item, the gate stays held, so a pin `NewStream` right after the close of the
one before returns `STATUS_DEVICE_BUSY` (`Uac2M0Stage` 30 with `80000011`,
as all three ME refusals read). `Close` takes the wave filter's operation
mutex, which `NewStream` holds around `Create`, so a wait there could not
work. This matches 48.1's readings (ME's audio stack creates and closes
several capture pins at each 44.1 kHz open; attempts climbing 2 to 6 a step)
but the old counters could not tell a queued destroy from a stream PortCls
still held; the new `Uac2Waits` value tells them apart, and the leg
below confirms the theory.

**The fix** (`34f1896`, design `docs/design/uac2.md` section 27): the gate
has three values, free, open and closing; the final release marks it
closing before it queues the destroy; `NewStream`, before it takes the
operation mutex, waits while the gate reads closing (10 ms sleeps, 2 s at
most), then creates as before. A gate held by an open stream is refused at
once, as before. `Uac2Waits` (REG_BINARY: opens that met a closing stream,
those still closing after the wait, the last wait in ms; written, never
read). Build stamps `uac2` 29, `uac1` 5 (`uacstream.h` and `uacwdm.cpp`
are shared). Docs: testing open item and the stalls record's Left item
(`8ad07a2`, `9c820be`).

**Review.** The design and code reviewed and revised before the commit; the commit and each docs
commit reviewed again: no material findings.

**Build and host tests** (development host A, from `34f1896`):
`uac2` `wdmx86` free PASS (`uac2wdm.sys` `20c1a9e7...`), `wdm64` free
PASS (`408d64ea...`, unsigned), `uac1` `wdmx86` free PASS (`uac1wdm.sys`
`710da697...`); `src/audio/uac2/test.cmd` PASS (26 suites), `uac1`
`test.cmd` PASS, `streamtest.cmd` PASS with the new cases (an open gate
returns from the wait at once and stays refused; a closing gate waited for
until its queued destroy frees it, 30 ms on the test clock; a destroy that
never runs ends the wait at 2 s and the open is refused; no wait above
PASSIVE). Logs `E:\xhci98-scratch\w49\uac\b-*.log`, `t-*.log`.

**Legs** (development host A, loaded by other workers; QEMU 11.1.0 as
installed by scoop (the base's usb4-emu build has no `usb-redir`), q35,
TCG, one vCPU, 512 MB, the q35 `me` base (`42fea19a...`) as a fresh
overlay, `piix3-usb-uhci` on `pcie.0` (ME's own UHCI driver), SeaBIOS
from the usb4-emu share dir, the transfer folder as a read-only FAT disk
(a writable one hung the boot at "Booting from Hard Disk" twice); the
simulator `uacemu.py --profile fs` (usbredir, port 49601) plugged after
the desktop by QMP `chardev-add` + `device_add usb-redir` on `uh.0`; the
INF by Add New Hardware, "Display a list", Have Disk `D:\`; no restart
asked. Steps typed into Run by QMP `sendkey`: `command /c for %i in (1 ...
20) do D:\UACWAV.EXE rec 1 16 44100 3 >COM1`, output in the serial log;
the `MEDIA` class key exported by `regedit /e` and typed to COM1.
`UACWAV.EXE` `20985f5e...`, INF `bfc5574b...` (the source INF at
`6a266c7`, unchanged).

| Leg | Build | Steps | Result | Counters after |
|---|---|---|---|---|
| `me0` | main `6a266c7` (`uac2wdm.sys` `099e07a4...`, `Uac2Build` 28) | 20 x rec 44.1 kHz 3 s | 14 PASS (8/8 buffers, 999 Hz tone), 6 `open=8` | `Uac2Streams` attempts 87, created 34, closed 34; `Uac2M0Stage` 31, status 0 (last open succeeded) |
| `me1` | `34f1896` (`20c1a9e7...`, `Uac2Build` 29) | 20 x rec 44.1 kHz 3 s | 20 of 20 PASS | capture `Uac2Streams` 40/40/40; `Uac2Waits` 15 waits, 0 still closing, last 14 ms |
| `me1` | (same boot) | 6 x rec 48 kHz 3 s; 3 x play 48 kHz 5 s; 3 x play 44.1 kHz 3 s | 12 of 12 PASS | render `Uac2Streams` 6/6/6; `Uac2Waits` unchanged (no 48 kHz recording and no play waited) |

So under ME's own UHCI the baseline refused 6 of 20 44.1 kHz recordings
(more than 48.1's 3 of 18 on a different machine profile) and 53 of its 87
pin opens were refused busy; with the fix every open succeeded and 15 of the
40 met the gate closing and waited for the queued destroy (the last wait
14 ms). The 48 kHz recordings, which ME opens with one pin, never waited.
The work-item theory is confirmed.

**98 SE: owed.** Two boots of the q35 `w98se` base with
`piix3-usb-uhci` (with the usb4-emu firmware and with QEMU's own) stopped
behind the boot logo with the CPU at `a000:8000`, CR0 `10`, after 4 to
5 minutes; not investigated (the base's own launcher uses the usb4-emu
QEMU build, which has no `usb-redir`). Owed: a 98 SE round (own UHCI or
NUSB, and under xHCI98) with the fixed build.

Evidence: `E:\xhci98-vm\p49\uac\me0\` and `me1\` (`serial-final.log`,
`s*.png`, `sim-*.jsonl`, `start.ps1`, `xf\` with the images run),
`se1\` (the 98 SE attempt), `pids.txt` (every start and stop; none
running).

## 49.9 Windows 2000: a mounted drive at Code 31 after a recovery or a hibernation (2026-11-11)

Worker c31, branch `w49-c31` from `3.5.0.0` at `d7d8769b`. Development
host A, QEMU `asm4242-20261110-docks` build, TCG, q35, one vCPU, 512 MB.

### Earlier incidents on the same path

- `runs/run-35.md`, "Known limitations": 5 of 5 on the 2000 SMP guest
  (two processors, Driver Verifier) at `efc6fe1`, the volume mounted at an
  in-place recovery; with nothing mounted 2 of 2 came back started. Read
  then as the device re-presented as a new PDO before the old devnode's
  REMOVE, the function driver's add failing (`CM_PROB_FAILED_ADD`).
- `runs/run-36.md`, "2000 S4": the Bulk-Only stick at Code 31 after a
  resume from hibernation (rel and qemu flavours, one vCPU): "stick
  re-created with the same instance id under the open stack" (its
  `debugcon-4.log` 72-94; the evidence folder is gone from the host).
- `runs/run-36.md` P8: the same after a forced recovery under
  `XhciInterruptMode` 2.
- `docs/issues/20`: a different Code 31 (the controller's own `HCD<n>`
  name), not this path.
- Code: `hcdPortQuiet` (`hcd_enum.c`) lets a dropped port go once its old
  PDO group is reported missing (the owner's ruling of 2026-10-04, made for
  ME); `hcdSerialTakenLocked` (`hcd_pdo.c`) does not count a gone PDO under
  the same parent, so the newcomer answers the same serial instance id.
  Since `3.4.0.0` the resume keep (task 45.23) keeps a UAS disk and other
  quiet devices across a resume's re-initialization; a Bulk-Only stick is
  still dropped (`XHCI_RKEEP_WHY_BULK_ONLY`).

### The vehicle

- Overlay `E:\xhci98-vm\p49\c31\w2k\disk.qcow2`: a copy of worker leg's
  `p49\leg\s2k` overlay of the q35 `w2k` base (the `3.5.0.0` release
  package of `6cbb89f2` installed by `INSTALL.BAT /console /yes /nopause
  /nostor98`; `XhciDmaStaging` removed, default), then `xhci98.sys` replaced
  by the `qemu` flavour of `d7d8769b` (gated build `build-base.log`, BUILD +
  GATES PASSED, `xhci98.sys` `4172a513...`; `xhciuas.sys` and the rest the
  release package's). Devices: `usb-storage` serial `W49STK01` on port 1
  (Bulk-Only, its volume F:), `usb-uas` with a `scsi-hd` on port 4 (E:),
  `usb-tablet` on port 2 (location 6). Command line `cmdline-b.txt` to
  `cmdline-d.txt`.
- Fault: `XhciQemuInject` `0n050000` (HCH: Run/Stop cleared), written by
  `INJ.VBS`; copies: `SL.BAT` (24 MiB copy, copy back, `fc /b`) on E: and F:
  at once; the trace from debugcon 0xE9 (`db-b.txt`, `db-c.txt`,
  `db-d.txt` in the VM folder).

### Readings on the build without the change

| # | Leg | Handles on the volumes at the fault | Old PDOs' REMOVE vs the new PDO | Result |
|---|---|---|---|---|
| b | recovery (HCH), copies running | none | missing reported, one SURPRISE_REMOVAL, the stick's new PDO created at once; the three old REMOVEs then arrived, all before PnP's first QUERY_ID of the new PDO | both drives back started, loops 6/6 and 6/6 PASS (the fault landed on the last rounds) |
| c | recovery (HCH), copies running | a `cmd` window with its current directory on each of E: and F: | the same order | both drives back, the same letters; "Delayed Write Failed" and "Unsafe Removal" boxes; F: loop 93 PASS 7 FAIL of 100 (the rounds across the fault), then passing; no Code 31 |
| d | hibernation (S4) with copies running, resumed by a cold start of the same overlay | the `cmd` windows of c still open | the resume keep kept the UAS disk (port 4, why 0) and the tablet; the stick dropped (why 08, Bulk-Only); its new PDO created right after the missing report, the old REMOVE before the new PDO's QUERY_ID; then START | the stick back started; F: loop 33 PASS 7 FAIL of 40 (the rounds across the resume) |
| d2 | recovery (HCH) on the resumed guest | `more` holding a file open on each of E: and F:, the `cmd` windows of c | the same order: every old REMOVE before the stick's new PDO was created | both back; E: 55 PASS 5 FAIL, F: 52 PASS 8 FAIL of 60; Device Manager shows no problem device under USB or Disk drives (`d-dm2.png`) |

Not reproduced in four tries on the one-vCPU guest: on it the PnP manager
sent the old PDO's REMOVE before it queried the new PDO, even with files
and directories held open on the vanished volumes (Windows 2000 left the
volumes as zombies, "Delayed Write Failed", and removed the USB stack under
them). The original reading was on two processors (run-35), where the
controller thread and the PnP manager's removal run at once; this phase's
rule keeps 2000 guests at one vCPU, so that reading is owed.

### The change

Design record 13 section 10.7, "The old devnode's REMOVE" (new). On
Windows 2000 alone (`RmWaitOn`, WDM 1.10 and not 1.20, at AddDevice), a
port that a recovery or a resume's re-initialization dropped
(`hcdDropPort`; a hub subtree's ports through `hcdPortGone` with
`HCD_GO_TAKEN`) waits after the missing report until no PDO of its old
group still waits for its REMOVE (`HcdDevicePdoRemovePending`), up to 30 s
on the tolerance clock (`hcdRmWaitHolds`; the controller thread's
`hcdRmWaitSweep` ends a wait the bound ran out), then goes on as before.
Notes `pdo.rmwait` (begun, location), `pdo.rmwait.done` (location, ticks)
and `pdo.rmwait.timeout`; qemu traces "waiting for the old PDOs' REMOVE"
and "old PDOs' REMOVE came". An ordinary unplug, 98 SE, ME and XP onward
are unchanged. Files: `src/hcd.h`, `src/hcd_entry.c`, `src/hcd_enum.c`,
`src/hcd_pdo.c`, `docs/contributing/design/13-superspeed-hcd.md`.

Why it is still the right change with no reproduction tonight: every
reading shows the new PDO created before the old REMOVE came (b, c, d,
d2); the PnP manager happened to send that REMOVE before it queried the
new PDO on one processor. The change makes that order the driver's own
rather than the scheduler's, at the cost of the REMOVE's latency (read
below) on 2000 only.

Reviewed and revised (one finding: a device the resume keep failed to
restore, `hcdRkeepDrop`, did not arm the wait; fixed), then no material
findings. Committed `9dd8773d2`.

### The build

One gated build of the reviewed tree (`d7d8769b` + the change, recorded
`dirty=1`; its content is commit `9dd8773d2`, nothing edited after it
started), `XHCI98_SELFTEST_SKIP_OK=1`: x86 `release qemu` BUILD + GATES
PASSED (`build-fix-x86.log`), amd64 `release qemu` BUILD + GATES PASSED
(`build-fix-amd64.log`). `xhci98.sys`: x86 release `db5dae69...`, x86 qemu
`cbcca6fd...`, amd64 release `fd4e8b03...`, amd64 qemu `ba34d317...`.

### Legs on the change (x86 `qemu` flavour `cbcca6fd...`)

| System | Leg | Result | Deciding lines / evidence |
|---|---|---|---|
| 2000 SP4 (guest e: the file put in place, then the controller disabled and enabled with `devcycle`, so the image is the new one) | recovery (HCH `04050000`), copies on E: and F:, a `cmd` window on each | **pass**: both drives back, the same letters, no problem device | "waiting for the old PDOs' REMOVE, location=00000001", the three REMOVEs, "old PDOs' REMOVE came, location/ticks=0001000C" (1.2 s), only then "device PDOs created, port/count=00010001" and its START; port 4 (UAS) the same, 10 ticks; the tablet (location 6) needed no wait (`db-e.txt`) |
| 2000 SP4 (guest e then f) | hibernation with copies on E: and F: and the `cmd` windows, resumed by a cold start | **pass**: UAS disk kept by the resume keep, E: 40 PASS 0 FAIL of 40 across the resume; the stick dropped (why 08), waited 7 ticks for its REMOVE, back started, F: 32 PASS 8 FAIL of 40 (the rounds across the resume, as without the change); Device Manager shows no problem device (`f-dm1.png`) | `db-f.txt` lines 74-130 |
| XP SP3 (fresh overlay of the q35 `xp` base, the `6cbb89f2` release kit installed, exit 3; the qemu file put in place and the controller cycled) | recovery (HCH `01050000`), a copy loop on F: | **pass**: F: 23 PASS 7 FAIL of 30 (the rounds across the fault), then `C.BAT` 3 of 3 PASS on F: and 1 of 1 on D:; no wait line (the change is off on XP, as designed) | `xp\debugcon-b.log`, `xp\serial-b.log` |
| 98 SE (copy of worker leg's `s98` overlay, its install; `XhciDmaStaging` set back to 0; the qemu file put in place, cold start) | recovery (HCH `02050000`) with a copy loop on the UAS disk E: (hot-added at the desktop; QEMU attached it only after `qom-set attached` off and on) | **pass**: the copy in flight met Windows 98's own "Disk Write Error" screen (a copy across a recovery fails, by design, `runs/run-35.md`); after it the UAS disk came back as a new PDO at port 4 and E: in Explorer, and a file copied to E: and compared with `fc /b`: no differences (`d-12.png`); no wait line (the change is off on 98 SE, as designed) | `w98\debugcon-d.log` 346-435 |


Vehicle notes. A Bulk-Only stick hot-added on this 98 SE overlay (no
Bulk-Only driver installed there) raised Add New Hardware; unplugged under
the cancelled wizard, the bus took no more port events and did not read
the injection until a restart (`w98\debugcon-c.log`): the known 98 SE
behaviour of `runs/run-36.md` M-1 (a device removed under its pending
install holds the ports), not this change's (it is off on 98 SE). After the
"Disk Write Error" screen the 98 SE guest's `COM1` writes faulted, so its
readings are screenshots. 2000's WMI queries failed ("Unspecified error")
on this overlay, so problem codes were read in Device Manager. One slip:
the kit's `devcycle.exe` was run once on development host A with no
arguments (it printed its usage line and changed nothing).

### Verdict

- Cause (read): a recovery and a resume's re-initialization drop the
  device and re-present it, under the same serial instance id, as soon as
  the old PDO is reported missing, before the old devnode's REMOVE
  (`db-b.txt`, `db-c.txt`, `db-d.txt`); on one processor the PnP manager
  happened to send that REMOVE before it queried the newcomer every time,
  so the Code 31 of runs 35 and 36 did not come tonight.
- Fix: the order is now the driver's own on Windows 2000 (read: the wait
  taken and ended by the REMOVE in 0.7 to 1.2 s on recovery and resume, the
  new PDO only after it); bounded at 30 s, so a REMOVE that never comes
  costs 30 s and then today's behaviour.
- Owed: the reading on two processors (the failure's own setup) or real
  hardware, with and without the change; the README's known-limitation line
  stays until then (`QUESTIONS.md`, Q2).

### Merge-review follow-up (`bd4014ff7`)

The coordinator's merge review of `9dd8773d2` found one P2: after a
recovery dropped a mounted drive whose REMOVE an open handle held, a user
who unplugged it and plugged in another device (a mouse, say) had that
port held until the old REMOVE or the 30 s bound. Now a root port in Gone
whose wait has begun, or whose departed hub's subtree still waits, that
reads no connection at least 1 s after its drop (every drop restarts that
second, for the links HCRST retrains) ends the wait with its subtree's
(`hcdRmWaitDeparted`, `hcdRmWaitCancelTree`, `pdo.rmwait.departed`); read
at each inspection of the port and each sweep; the missing-report rule
stands. Reviewed and revised over four rounds (the replug during the
settling time, the subtree still waiting after the port's own wait ended,
a second reset keeping an old drop time), then no material findings.
Legs on it are owed (an unplug while the wait holds needs a REMOVE that
stays pending, which the one-processor guest did not give tonight).

Second merge-review P2 (a0186fe7b): the 1 s departure settling was ten raw tolerance ticks, which can run out early; it is now `XHCI_TOL_TICKS(1000UL)`, a whole second at least. The 30 s bound stays in raw ticks (an early end only falls back to the behaviour before the task). Reviewed: no material findings. Gated x86 build passed.

**Not merged (coordinator, 2026-11-11).** The branch was reviewed again before a merge, twice
revised (`bd4014ff7`, a physically replaced device no longer held by the wait; `a0186fe7b`, the departure
settle a whole second at least) and built (x86 release and qemu passed on `a0186fe7b`), but no leg ran on
those two commits, so `w49-c31` waits for its legs before it is merged.


## 49.11 - a USB4 drive lost after a standby with no pre-boot connection manager (S.1): found, fixed, read (2026-11-11, worker tbs)

Branch `w49-tbs` from `3.5.0.0` at `5458cd114`. Commits `25ba99008` (the settle wait, record 22
section 11.1.14) and `8ae179779` (its function probe reads every device number).

**Vehicle** (rebuilt: the 48.35 kits under `E:\xhci98-vm\p48\tbs7` were gone). 7 SP1 x86 from the
q35 base (`q35-bases\w7x86`, an overlay) run on QEMU's `pc` machine: ICH9 AHCI system disk at
00:03.0, `qemu-xhci` 00:06.0, `usb4-tunnel-bridge` 00:05.0 (SHPC, `shpc-autoenable=on`, `msi=off`),
`usb4-host profile=usb4-generic` 00:04.0 with a fixed `uuid`, the ORICO model
(`usb4-device ... dialect=usb4,device-model=asm2464`, the shared FAT NVMe overlay) at power-on, no
pre-boot connection manager, `-vga none` for the legs (std VGA vetoes S3; std VGA for the installs),
TCG, 2 vCPUs, 2 GB, `-rtc base=2026-09-20`; emulator `q35-bases\bin\asm4242-20261110-docks`
(`4959a9ab...`), `-trace usb4_*`. Launcher `E:\xhci98-vm\p49\tbs\tools\gopc.ps1`, QMP 49640. Package:
`releases\3.4.3.0`'s `nt-x86` family with its tools (`INSTALL.BAT /console /yes /nopause /thunderbolt
/stor98`, publisher prompts clicked; exit 3: xHCI98 USB4 Router, xHCI98 PCI Bridge, Stor98 started,
ORICO D: mounted), then the `xhcicm.sys` under test copied over `System32\drivers` (`xfer\SW.CMD`,
`fc /b` no differences, `certutil` hash on COM1) and a cold start. The leg (`tools\leg.sh`, guest
`C:\T\LEG.CMD` and `CHK.CMD`): before the sleep a file written to the drive and `XHCITR /report`;
`SetSuspendState` with hibernation off; QMP `system_wakeup` 20 s after `suspended`; checks at about
t+40 s and t+190 s (the drive holding `TESTDATA.BIN` found, its directory listed, a new file written
and typed back, `wmic diskdrive`, the report), QEMU `info pci` at +5, +30 and +60 s; then the guest
shuts itself down.

**Builds.** Gated `build-driver.cmd all` x86 of `5458cd114` (`XHCI98_SELFTEST_SKIP_OK=1`):
`BUILD + GATES PASSED (release qemu)` (`E:\xhci98-scratch\w49\tbs\gated-x86-base.log`); of
`25ba99008`: x86 `all` and amd64 `release` both PASSED (`gated-x86-f1.log`, `gated-amd64-f1.log`);
of `8ae179779`: x86 `all` PASSED (`gated-x86-f2.log`; host tests `test_tbt_topo` 1071 checks,
`test_cm_ports` 397, 0 failures). `xhcicm.sys` x86: base qemu `26c154f2...`, `25ba99008` release
`8b7cc72e...`, `8ae179779` release `f66c95cb...`, qemu `cb2e6d3c...`; amd64 `25ba99008` release
`8b7b96d3...`.

**The cause** (leg `a1`, base qemu image, `XhciCmSleepEntry` 1, debugcon `s1\debugcon-q3.log`,
trace `s1\usb4-trace-q3.log`). Reproduced: the drive gone at t+40 and t+190 s. At the wake the
emulator's reset dropped the tunnel and removed the endpoint (`usb4_tp_surprise`, gen 1); the
resume rebuilt the PCIe tunnel (`u4.pcie.up=00010809` at `cm.ms` 0x377EC), released and invalidated
the bridge at once (`cm.reenum.resumed=1`, `cm.reenum.hold.released=00000028`), the drain's
`TUNNEL_UP` poll ended `NO_READING` (`cm.reenum.up.poll=00000404`: on NT this bridge gives the port
reading nothing to vote on), and the bridge's BusRelations answer 220 ms after the tunnel came up
held no PDO (`cm.chain.rel=00000028`); PnP removed the chain's two devices (`cm.chain.remove`
00010028 and 00010108). The endpoint answered again 1.7 s after the tunnel came up (the trace's
`usb4_tp_settle_wait ... 1680 ms left`, then `usb4_tp_insert` gen 2: the emulated bridge's
empty-slot settle) and QEMU `info pci` showed it from +5 s on, but nothing asked the bridge again.
Hibernation keeps the drive because a fresh machine has no departed endpoint: the function answers
as soon as the tunnel is up. **Control** (leg `c1`, the same image, the bridge's `settle-ms=0`): the
drive kept at t+40 and t+190 s. So the release asked the bridge before the function was back.

**The fix** (record 22 section 11.1.14). A USB4 resume that met every obligation, on the filter
route, reads the function behind each member held at the suspend whose bridge then reported a PDO
(latched at the suspend), until it answers, within one 5 s bound, before the liveness probe, the
stor98 UP, the release and the drain; logged `cm.reenum.resume.settle` and `.late`. A release
deferred behind a stor98 gate exposes first (owed behind the retirement barrier). Reviewed and
revised: design twice, code twice (the barrier), then approved; the follow-up `8ae179779`
reviewed, no findings.

**`8ae179779`'s finding.** The first leg on `25ba99008` (`f3`) kept the drive, but only through the
bound: `cm.reenum.resume.settle=01001390` (waited 5008 ms, none shown), `.late=00002802` (ABSENT). A
PCI bridge's slot puts its card at the slot's device number - the emulator's SHPC slot 0 is device
1 (`info pci`: `Bus 1, device 1`) - and the probe read device 0 only. It now reads function 0 of
every device number.

| Leg | Image | `XhciCmSleepEntry` | S3 / S4 | Result |
|---|---|---|---|---|
| a1 | base `5458cd114` qemu | 1 | S3 | **FAIL as S.1**: drive gone at t+40 and t+190 s; the cause above |
| c1 | base qemu, bridge `settle-ms=0` | 1 | S3 | control: drive kept, t+40 and t+190 s |
| f3 | `25ba99008` release | 1 | S3 | drive kept, t+40 and t+190 s, files written and read back; the settle wait ran to its bound (finding above) |
| g1 | `8ae179779` release | 1 | S3 | **PASS**: `cm.reenum.resume.settle=01010731` (the function back after 1841 ms), drive D: kept at t+40 and t+190 s, a file written and typed back at each, `QEMU NVMe Ctrl SCSI Disk Device OK`; the guest shut itself down after the leg |
| g0 | `8ae179779` release | **0** (`cm.usb4.sleep=00000100`) | S3 | **PASS**: `cm.reenum.resume.settle=01010750` (1872 ms), drive kept at t+40 and t+190 s; the guest shut itself down |
| h3 | `8ae179779` release (std VGA: S4 only) | 1 | S4 | **PASS**: the post checks run by `S4POST.CMD`, started before the hibernation: drive D: at p60 and p180 after the resume, files written and read back, `cm.reenum.resume.settle=01010000` (the function there at the first reading); the guest shut itself down |

The NT legs ran on `8ae179779`'s image; the later commits (`dbb00acfc`, `a3e312c7e`) change the 9x
(CONFIGMG) route only. Not read: 2000 and XP on this vehicle, 7 x64, 8 to 10 (owed; the change is
the same image path).

Vehicle notes. After a resume on this guest typed input is not taken (the wake's lock screen; the
legs therefore run their checks and their shutdown from the script started before the sleep). A
guest stopped by QMP quit after a resume can stop at the boot manager on the next cold start with
`-vga none` (74 MB resident, nothing on COM1): start it once with std VGA. Leg h1 (the S4 checks
typed after the resume) and h2 (that boot) are void for those reasons. Evidence:
`E:\xhci98-vm\p49\tbs\s1\` (`serial-*.log`, `debugcon-*.log`, `usb4-trace-*.log`, `pci-*.txt`,
`leg-*.txt`, `cmdline-*.txt`), every start and stop in `E:\xhci98-vm\p49\tbs\pids.txt`.

## 49.12 - 98 SE standby with a tunnelled drive: the bridge's header put back (2026-11-11, worker tbs)

Branch `w49-tbs`: `dbb00acfc` (w48-tbs6's five commits carried, as one; reviewed again: no
findings), `a3e312c7e` (option (a): record 21 section 6.1.4 items 1 to 5), and the endpoint
restore (item 6; see below for its commit).

**Choice** (`E:\xhci98-scratch\w49\tbs\QUESTIONS.md`, Q1): (a), the run's recommendation and the
reviewer's; (b) rejected (it takes the volume away at every standby); (c)'s user text kept until the
drive is shown back.

**Vehicle.** 98 SE from the q35 base (`q35-bases\w98se`) run on QEMU's `pc` machine with the system
disk on the PIIX IDE (no geometry hints: with `lcyls=1024,lheads=255,lsecs=63` the primary IDE
channel was Code 10, the disk ran in MS-DOS compatibility mode and Shut Down offered no Stand by;
without them Stand by is offered and works with no package), one vCPU, 512 MB, S.1's topology
(`usb4-tunnel-bridge` 00:05.0, `usb4-generic` 00:04.0, ORICO at power-on, `qemu-xhci` 00:06.0),
`tools\go98.ps1`. Platform wizards answered with Enter. `releases\3.4.3.0`'s `98-me` kit:
`INSTALL /console /yes /nopause /thunderbolt /stor98` exit 4, the Stor98 wizard, a cold start: xHCI98
USB4 Router started, ORICO (E:) mounted. That state is `n98\base98.qcow2` (read-only); each leg is a
fresh overlay of it with the `xhcicm.sys` under test copied over `SYSTEM32\DRIVERS` (`fc /b` no
differences) and a cold start (`tools\l98.sh`). The leg (`tools\leg98.sh`) uses a GUI helper,
`C98.EXE` (`E:\xhci98-scratch\w49\tbs\c98\c98.c`, no console after the wake: the known 0E at
`BFF9DFFF` comes with console programs): before the sleep the drive holding `TESTDATA.BIN` found, a
64 KiB file written and compared, `XHCITR /report`; `SetSystemPowerState` (SetSuspendState returns 0
on 98 SE); QMP `system_wakeup` 20 s after `suspended`; `info pci` at +5 and +30 s; the same checks
after the wake (typed without a drive letter: the first typed `:` loses its Shift after a resume).

| Leg | Image | Result |
|---|---|---|
| r1 | `3.4.3.0` release (base) | the loss reproduced: bridge 00:05.0 secondary bus 0 from +5 s to the end; a New Hardware wizard after the wake |
| q1 | `3.4.3.0` (the swap's command had failed) | the same as r1 |
| q2 | `a3e312c7e` qemu | **the bridge put back**: `cm.reenum.9x.resume-restore=00002801`, `resume-wait=00002805` after 2073 ms; the bridge read secondary bus 1 at +5 s; the walk found the NVMe (`noirq.walk.node=01087F00`), `cm.nvlink.up.late`. **The drive still gone** (`C98 drive=NONE`): the NVMe's BAR0 read "not mapped" and its interrupt line 0 - the tunnel's rebuild brings the endpoint back from reset behind a devnode that stayed, and PCI.VXD does not program it again. Hence item 6 |
| q3 | `7e0bf543c` qemu (item 6) | **the bridge and the NVMe put back**: `resume-restore=00002801`, `resume-restore.fn=00010801` (01:01.0 written), `resume-wait=00002805` after 2087 ms; `info pci` at +5 and +30 s: bridge secondary bus 1, the NVMe's BAR0 at `0xfe800000` and IRQ 10 as before the sleep; the walk found it, `cm.nvlink.up.late`. The drive's read after the wake **not made**: the post check's launch met the known fatal exception 0E at `0187:BFF9DFFF` and the shell ended |
| f1 | `7e0bf543c` release | void: the guest reset at the wake (QEMU exited under `-action reboot=shutdown`) before the connection manager rebuilt the tunnel (the trace ends at the wake's surprise removal); as tbs6's `0a1` and `rA3` (`3.4.3.0` and `ddf03ada`), not this change |
| q4 | `7e0bf543c` qemu | as q3: bridge and NVMe put back (`00002801`, `.fn=00010801`, outcome 5); the post check's launch met the fatal 0E at `0187:BFF9DFFF` |
| f2 | `7e0bf543c` release | the same fatal 0E at the post check's launch |
| q5 | `7e0bf543c` qemu, the checks by a `C98.EXE watch` started before the sleep (no program started after the wake) | **PASS**: bridge and NVMe put back (`00002801`, `.fn=00010801`, outcome 5); NVMe BAR0 `0xfe800000`, IRQ 10 at +30 s; about 70 s after the wake drive E: there, a new 64 KiB file written and compared, and again about 2 min later; the desktop answering at the end; no 0E |
| f3 | `7e0bf543c` **release**, the same | **PASS**: the same - E: back, written and compared at both checks, the desktop answering, no 0E |

The fatal 0E at `0187:BFF9DFFF` came in each leg that started a program after the wake once the
drive was back (q3, q4, f2) and in none that did not (q5, f3, and the later legs below); in q2 (the
drive not back) the same launch ran. It is the fault the lessons record for Windows 98 SE after a
standby in QEMU (a program started after the resume, seen with no USB controller at all), not read
further here.

| Leg | Image | Result |
|---|---|---|
| q6 | `7e0bf543c` qemu, the watcher under the leg's own tag | **PASS**: outcome 5, `.fn=00010801`; about 70 s after the wake the file written before the sleep (`Wq6A.BIN`) read back and compared OK, a new one written and compared; again 2 min later |
| f4 | `7e0bf543c` **release**, the same | **PASS**: the pre-sleep file compared OK after the wake, a new one written and compared, twice; the desktop answering |
| f5 | `7e0bf543c` **release**, two standbys in one boot (`C98.EXE watch2`: the second standby asked by the watcher itself after its first check) | **PASS**: after the first wake the pre-sleep file compared and a new one written and compared; after the second wake E: there, written and compared, and again 2 min later |
| q7 | `7e0bf543c` qemu, the same | **PASS**: each standby `resume-mark=00000001`, `resume-restore=00002801`, `.fn=00010801`, `resume-wait=00002805` (a fresh copy at the second suspend, the first one restored); the drive back and written after both wakes |

**ME** (`nme\baseme.qcow2`: the q35 ME base run on `pc`, its disk on the PIIX IDE with its geometry
hints, `3.4.3.0` installed, ORICO E: mounted and written; then `7e0bf543c`'s release and qemu
images): no leg read. `SetSuspendState` returned 1 and Shut Down's "Stand by" was chosen, but the
guest never entered S3 in 3 minutes (two guests, twice each); the desktop stayed. Not this change
(nothing of it runs before a suspend completes); the ME vehicle is owed (tbs5's ME image, which did
stand by on `pc`, is gone).

**What this says.** On 98 SE with a tunnelled NVMe attached, standby now keeps the drive and its
data on the emulator: the bridge's header and the NVMe's BARs and interrupt line are put back once
Windows has not done it within 2 s (both legs at `XhciCmSleepEntry` 1, the default). Not read: ME
(the write-back is expected never to run there: ME restores the bridge itself; leg owed), a standby with the drive unplugged during it, a hibernation, a dock or
chain (functions behind a switch below the tunnel port are not written back), and the bench. The
fatal 0E after the wake when a program is started is the QEMU 98 SE fault (above), unchanged.

**Builds of the tip** (`7e0bf543c`, `XHCI98_SELFTEST_SKIP_OK=1`): x86 `all` `BUILD + GATES PASSED
(release qemu)` (`gated-x86-h.log`; host tests passed, `test_cm_ports` 554 checks, `test_cm_noirq`
528, `test_tbt_topo` 1071), amd64 `release` PASSED (`gated-amd64-h.log`). `xhcicm.sys` x86 release
`680131fb...`, qemu `2d0b4417...`; amd64 release `2aa405d3...`.

**Review.** Option and design: one opinion round (recommended (a)). Code: the bridge write-back
reviewed and revised five times (the suspend's handoff, the kept and partial copies), then
approved; the functions' write-back four times (64-bit BARs, the command and interrupt line read
back, unreadable functions), then approved.

Evidence: `E:\xhci98-vm\p49\tbs\{n98,b98,q98,q98b..q98f,r98,r98b..r98d}\` (`serial-*.log`,
`debugcon-*.log`, `usb4-trace-*.log`, `pci-*.txt`, `leg-*.txt`, screenshots), every QEMU start and
stop in `E:\xhci98-vm\p49\tbs\pids.txt`.

## 49.18 (emulator part) - design only (2026-11-11, worker tbs)

Design draft `E:\xhci98-scratch\w49\tbs\design-4918.md`, five options. Recommended, after
review: option 5 (an SHPC reading for the NT learning) plus option 1 (keep the eject's notice where
the correspondence stays unknown), the start inference kept as the explicit, off-by-default choice;
option 4 (tear down and read back) rejected as drafted. Put to the owner; nothing coded, since it
touches the owner's decision of 2026-11-10.

## The networking and dock legs: the setup (2026-11-11, worker net)

Worker net, development host A, branch `w49-net` from `3.5.0.0` at `5458cd114`. No code change: the branch
carries no commit, no review was owed for code.

**The package under test.** `3.5.0.0` at `5458cd114` differs from the `v3.4.3.0` tag in documentation only
(`git diff --stat v3.4.3.0..HEAD -- src scripts tools` is empty), so the legs install the released
`xhci98-3.4.3.0.zip` (`E:\xhci98-release\3.4.3.0`), unpacked to `E:\xhci98-vm\p49\net\kit` with the guest
batch files of w48 net, net9 and net10 beside it (`TP.BAT`, `TN.BAT`, `TOG.BAT`, `VFY.BAT`, `S3.BAT`, x86
`DEVCON.EXE`). No build was run. SHA-256 (`E:\xhci98-scratch\w49\net\hashes.txt`):

| file | SHA-256 |
|---|---|
| `xhcicm.sys` (98-me and nt-x86, the same image) | `9DBD715B3C2DF443370DE2532C15DDA73E13027303443F84DB1E200A8C4E0A14` |
| `xhcinet.sys` (98-me and nt-x86) | `D84F5CABB409AE88D547C3CFF10605F5AFB9A53AFDCB106D2BB4213761B53E6D` |
| `xhcinet6.sys` (nt-x86) | `43F3224F430A652CB26F9807927CB0A38BE999F6A9A587D45DF010FA925375B2` |
| `xhci98.sys` (nt-x86) | `3B35B4AE102266B512DC97C8D4CE41746D53F4F51074508740FE81E6C9FC7266` |
| `XHCITR.EXE` | `369F9EB75BCE9F6FB1F1D3A92537AFA3DC53312FA52714FB255FF18EB3C49E94` |

Emulator `E:\xhci98-vm\q35-bases\bin\dock-20261110-bar0` (usb4-emu dock build, QEMU 11.1.2
v11.1.2-4-g2aff288, with `usb4-xdomain`), through `launch-q35.ps1 -Emu`. Guests are fresh overlays of the
q35 bases (`E:\xhci98-vm\q35-bases\<os>`), the package installed by
`INSTALL.BAT /console /yes /nopause /thunderbolt /tbnet` (scripts `E:\xhci98-scratch\w49\net\kit\`:
`prepnt.sh`, `end.sh`, `linux.sh`). VMs and logs: `E:\xhci98-vm\p49\net\` (`pids.txt`).

## 49.2 The other computer's MAC read in the emulator (2026-11-11, worker net)

The peer with a known MAC: Linux, not a Windows 11 guest. Linux's `thunderbolt_net` is the implementation
48.x's fix was read against (its Login Response carries its own `dev_addr` in `receiver_mac`, each DW sent
big-endian by `tb_ctl_tx`), its MAC is read directly (`ip link`), and it reads this host's MAC
independently (its neighbour table, from the frames), so a reversed byte order shows at either end.

**Cell M1.** A: XP (q35 base `xp`, image `xpa` = the package installed, exit 3), `-Usb4 tb3ar` (Alpine
Ridge NHI `8086:15BF`, firmware CM), 2 vCPU TCG, QMP 49626, the `usb4-xdomain` client end of TCP 49630.
B: Kubuntu 26.04.1 live (`E:\kubuntu-live\kubuntu-26.04.1-tbcap-amd64.iso`, kernel 7.0.0-30-generic, stock
`thunderbolt` and `thunderbolt_net`, booted by `-kernel/-initrd` from its `casper\`), q35 WHPX 2 vCPU 3 GB,
`usb4-host profile=usb4-generic,host-model=intel-adl` (software CM), the server end of TCP 49630
(`kit\linux.sh`); runtime power management off on `0000:00:04.0` and domain `0-0`; `thunderbolt0`
unmanaged by NetworkManager, `169.254.10.1/16`.

| Step | Read | Verdict |
|---|---|---|
| Linux, XP booting | "new host found, vendor=0x8086 device=0x1"; "login started", "remote login request received", "remote login response sent", "received login reply", "login successful, enabling paths", "network traffic started" | logged in |
| Linux's own MAC (`ip link show thunderbolt0`) | `link/ether 02:b5:28:1b:72:ce` | |
| XP `TP.BAT 169.254.10.1`: ping | 6 of 6, 13-38 ms | |
| XP's XHCITR report, networking node | "Thunderbolt networking - up"; "Adapter MAC: 92-65-7B-FA-2C-60"; **"Other computer's MAC: 02-B5-28-1B-72-CE"**; link "logged in (carries the network)"; "Other computer: kubuntu"; "Kind: Linux (thunderbolt_net)" | **pass**: the order Linux uses |
| Linux `ip neigh show dev thunderbolt0` | `169.254.120.10 lladdr 92:65:7b:fa:2c:60` (XP's address and its report's own MAC) | this end's MAC as the far end reads it |
| Linux `ping -c 4` XP | 4 of 4, 2.0-9.1 ms | |
| XP's stand-alone snapshot `TS.DEV` | networking section, "Adapter MAC: (not included in the snapshot)", the other computer's name and UUID withheld, "Kind: Linux (thunderbolt_net)" | as designed |

Evidence: `E:\xhci98-scratch\w49\net\M1-serial-A.log` (XP's COM1: ping, the report, the snapshot),
`M1-xp-report-net.txt` (the report's networking lines), `M1-linux.txt`; `E:\xhci98-vm\p49\net\cells\M1\`.

Verdict: **done** (emulator). 48.x's fix shows the far end's MAC in the order the far end itself uses.
The Windows 11 pairing on the bench (`B0-59-89-DC-A6-40`) stays the owner's to read, if wanted; the
emulator reading needs no Windows 11 guest, since Linux and Windows 11 put the MAC in the field the same
way (48.x).

## 49.7 The networking view and the connection manager's networking not yet read (2026-11-11, worker net)

The same package, emulator and Linux peer as 49.2 (the Linux end `lnx1` stays up across the cells; its cable
is pulled and replugged by QMP `qom-set /machine/peripheral/xd0 plugged false|true` at the Linux end).

**A finding about the emulator, not the driver.** On Vista and later, Windows names a PCI device from its
Device Serial Number when it has one; `usb4-host` derives the NHI's from its `uuid` property, which is a new
random value at every QEMU start unless the launcher passes one (`uuid=` or `dsn-value=`; `launch-q35.ps1`
passes neither). So each start of a prepared 7, 8, 8.1 or 10 image is a new NHI instance with a new driver
key, and the `XhciCmNet` 1 the installer wrote into the first instance's key is not there: the report reads
"Thunderbolt networking: off" and Linux finds the host but no network service ("failed to send properties
changed notification"). XP and 2000 name the device by its location and keep it. The cells on 7 and later
therefore set the machine-wide value (`HKLM\System\CurrentControlSet\Services\USB` `XhciCmNet` 1, which the
driver reads over the driver key, as 48.16's toggle did) and restart the NHI with `DEVCON`; Windows then
installed "xHCI98 Thunderbolt Networking #2" by itself. A real computer's NHI keeps its serial.

### 7 x86 (cell C1): the networking node, with the other computer and without

7 x86 (q35 base `w7x86`, image `w7a`: the package installed, exit 3), `-Usb4 tb3ar`, 2 vCPU TCG, QMP 49628,
`usb4-xdomain` client of `lnx1`. The commands typed into one elevated console in the guest (`kit\legc.sh`).

| Step | Read | Verdict |
|---|---|---|
| first report (the new NHI instance) | "Thunderbolt networking: off" | the emulator's new identity (above) |
| machine-wide `XhciCmNet` 1, `DEVCON restart` of `8086:15BF` | "Restarted"; balloon "xHCI98 Thunderbolt Networking #2 - Device driver software installed successfully"; Linux "login successful, enabling paths", "network traffic started" | |
| `TP.BAT 169.254.10.1`: ping | 6 of 6, 15-29 ms | |
| the report's networking node | "Thunderbolt networking - up"; "XhciCmNet: 1 (on)"; "Adapter MAC: 82-28-71-26-46-40"; "IP address: 169.254.145.181 (255.255.0.0)"; "Other computer's MAC: 02-B5-28-1B-72-CE" (Linux's, the right order on 7 too); "Sent: 250 packets", "Received: 46 packets", "Errors: 0 sending, 0 receiving"; "Network driver: xhcinet.sys, MTU 1500"; "Link changes: 1 up, 0 down"; "Ring interrupt: off (checked every clock tick)"; link node "logged in (carries the network)", "Other computer: kubuntu", "Kind: Linux (thunderbolt_net)", "Its vendor: 1D6B "Linux"", "Networking offered: yes (end-to-end flow control, frame IDs, large frames)", "Logins: 6 sent, 0 failed; 1 received, 0 refused" | **pass** |
| stand-alone snapshot (`XHCITR /snapshot TS`) | networking section; MAC, the other computer's name and UUID "not included in the snapshot" | **pass** |
| cable out at Linux, 60 s, `REP.BAT` | "Thunderbolt networking - no other computer connected"; "IP address: none yet"; "Link changes: 1 up, 1 down" | **pass** |
| cable in, Linux's new `thunderbolt0` raised, 60 s, `TP.BAT` | ping 6 of 6; "Thunderbolt networking - up"; "Link changes: 2 up, 1 down" | **pass** |

Evidence: `E:\xhci98-scratch\w49\net\C1-serial-A.log`, `E:\xhci98-vm\p49\net\cells\C1\` (`leg.txt`, `t*.png`).

### 2000, and XP under Driver Verifier: ping, the report and the `XhciCmNet` toggle (cell C2)

A: XP, image `xpv` (an overlay of `xpa` with `VFY.BAT on`: `verifier /standard /driver xhcicm.sys xhcinet.sys`,
restarted; `verifier /query` after the start lists both drivers, `Level: 000000BB`), `-Usb4 tb3ar`, 2 vCPU,
the `usb4-xdomain` server end of TCP 49632. B: 2000 SP4 (q35 base `w2k`, image `w2ka`), `-Usb4 tb3ar`, 1 vCPU.
2000's preparation needed every Found New Hardware window walked to Finish before the installer would run
(pass 1: exit 4 "Windows is still installing something"; then exit 2, "RESTART Windows to finish"; after a
cold start exit 3), cold starts only (`kit\inst2k.sh`), and 2000 on this base has no `shutdown` command (the
guest is stopped by QMP `quit` after its work). The commands typed into Run (`kit\leg2.sh`, `TOGA=15BF`).

| Step | XP (Verifier) | 2000 |
|---|---|---|
| address (APIPA) | `169.254.12.55` | `169.254.33.64` |
| ping the other end, 6 | 6 of 6, 34-240 ms | 6 of 6, 10-30 ms |
| report, networking node | "Thunderbolt networking - up", "XhciCmNet: 1 (on)", "Adapter MAC: 52-3C-7E-92-D7-A0", "Other computer's MAC: CA-D3-35-AF-66-60", "Errors: 0 sending, 0 receiving", link "logged in (carries the network)", "Kind: xHCI98" | "Thunderbolt networking - up", "XhciCmNet: 1 (on)", "Adapter MAC: CA-D3-35-AF-66-60", "Other computer's MAC: 52-3C-7E-92-D7-A0", "Sent: 99 packets", "Received: 69 packets", "Errors: 0 sending, 0 receiving", "Kind: xHCI98" |
| stand-alone snapshot | `TS.DEV` with the networking section | `TS.DEV` with the networking section |

Each end shows the other's own MAC, byte for byte: the MAC order between two xHCI98 hosts too.

**The toggle under Verifier at XP** (`TOG.BAT <2000's address> 15BF`: the machine-wide value, the NHI
restarted by `DEVCON` after each change):

| Step | Read | Verdict |
|---|---|---|
| `XhciCmNet` 0, NHI "Restarted", 45 s | `devcon status XHCICM\TBNET`: "No matching devices found."; ping "Destination host unreachable" 4 of 4 | off |
| 1, "Restarted", 90 s | "xHCI98 Thunderbolt Networking ... Driver is running."; APIPA `169.254.18.143`; ping 6 of 6, 17-172 ms | on |
| 0/1 back to back three times (six restarts, each "Restarted"), 90 s | "Driver is running."; APIPA `169.254.114.142`; the ping to 2000's first address 0 of 6 | see below |
| `verifier /query` after the toggles | `xhcicm.sys` loads 1, unloads 0; `xhcinet.sys` loads 5, unloads 4; "AllocationsFailed: 0"; no bug check in the whole cell (about 25 minutes) | **pass** |
| report at XP | "Thunderbolt networking - up", new "Adapter MAC: 32-30-5F-F0-50-90" (a new MAC at each network device start, as 48.16 read), "Other computer's MAC: CA-D3-35-AF-66-60", link "logged in", "Logins: 1 sent, 0 failed; 2 received" | up |
| 2000's report and ping | 2000 had taken a new APIPA address, `169.254.236.46` (2000 picks a new one when the medium comes back; XP keeps one per MAC); 2000 to XP's new address 6 of 6 | |
| XP to 2000's new address | 6 of 6, 10-202 ms | **pass**: the 0 of 6 was the stale address |

Evidence: `E:\xhci98-scratch\w49\net\C2-serial-A.log`, `C2-serial-B.log`, `E:\xhci98-vm\p49\net\cells\C2\leg.txt`,
`E:\xhci98-vm\p49\net\xpv\serial-s3.log` (Verifier set).

### Titan Ridge at one end, 10 x86 at the other (cell C3)

A: XP (an overlay of `xpa`) on `-Usb4 tb3tr` (`profile=tb3-titan-ridge`, NHI `8086:15EB`, the switch `8086:15EA`,
its xHCI `8086:15EC`), the server end of TCP 49636. XP found the new controllers at the first start (Found
New Hardware walked: "xHCI98 TB3 Controller (Intel Titan Ridge)", "xHCI98 USB 3 Controller (Intel Titan
Ridge)"; the bridges stay "PCI standard PCI-to-PCI bridge", the installer's tunnel-port filter not being on a
device first seen after the install); the new NHI's driver key has no `XhciCmNet` 1, so the machine-wide value
was set and the NHI restarted, and the network device's own Found New Hardware walked. B: 10 x86 (overlay of
`w10a`), `-Usb4 tb3ar`, machine-wide `XhciCmNet` 1 and the NHI restarted (the new identity, above).

| Step | XP on Titan Ridge | 10 x86 on Alpine Ridge |
|---|---|---|
| before the network device's driver was installed | "Thunderbolt networking - no network driver", "State: no network driver loaded", "IP address: none (Windows lists no adapter)" | "Thunderbolt networking - connecting" |
| after it | APIPA `169.254.243.104` | APIPA `169.254.32.169` |
| ping the other end, 6 | 6 of 6 | 6 of 6 |
| report | "Thunderbolt networking - up", "Adapter MAC: B2-BA-0D-95-4A-30", "Link: port 0", "Other computer's MAC: F6-2F-D6-F0-40-A0", "Errors: 0 sending, 0 receiving", "Network driver: xhcinet.sys, MTU 1500", "logged in (carries the network)", "Kind: xHCI98" | "Thunderbolt networking - up", "Adapter MAC: F6-2F-D6-F0-40-A0", "Link: port 1", "Other computer's MAC: B2-BA-0D-95-4A-30", "Network driver: xhcinet6.sys, MTU 1500", "logged in", "Kind: xHCI98" |

Verdict: **pass** (the emulator's Titan Ridge model, inferred from the Linux client, no capture): the
networking on a Titan Ridge firmware connection manager, and 10 x86's networking node with `xhcinet6.sys`.
Evidence: `E:\xhci98-scratch\w49\net\C3-serial-A.log`, `C3-serial-B.log`; `E:\xhci98-vm\p49\net\cells\C3\`.

### A resume on 7 x86 (cells C1s, C1t, C1u)

7 x86 (the C1 image with the machine-wide `XhciCmNet` 1) with Linux at the other end, the S3 SeaBIOS
(`E:\xhci98-vm\q35-bases\bin\asm4242-20261110-docks\bios-256k.bin`, SHA-256 `e5dc0550...`), `S3.BAT`
(`SetSuspendState`; then `powercfg /lastwake`, `ipconfig`, ping, the report).

- C1s, with the std VGA: `powercfg /a` "Standby (S3) ... The VGAPNP.SYS display driver does not support
  standby"; `SetSuspendState` returned at once. So the leg runs with `-vga none` (48.16's plan): no screen,
  the keys typed blind.
- C1t: S3 entered (QMP `suspended` 4 s after), `system_wakeup` 45 s later, `S3.BAT` finished; but the Linux
  end had died meanwhile (its console: "KERNEL PANIC! ... Attempted to kill init! exitcode=0x0000008b"; a
  Linux live session's `init` killed; not read further, its serial was not logged), so the report read "no
  other computer connected"; and 7, woken by the emulated power button and left untouched, went back to
  sleep about two minutes later (Windows' unattended-wake timeout; the later runs move the mouse after the
  wake). Not counted.
- **C1u** (fresh overlay, a new Linux end `lnx2` with its serial logged):

| Step | Read | Verdict |
|---|---|---|
| before | ping 6 of 6; "Thunderbolt networking - up", "Link changes: 1 up, 0 down" | |
| `S3.BAT` | QMP `suspended` 3 polls after; 45 s; `system_wakeup` | stand-by entered |
| after the resume | "== s3 back"; `powercfg /lastwake`: "Wake Source [0] Type: Fixed Feature Power Button"; APIPA `169.254.181.41`; ping Linux 6 of 6, 14-27 ms | |
| the report after it | "Thunderbolt networking - up", "Other computer's MAC: 02-B5-28-1B-72-CE", "Sent: 344 packets", "Received: 33 packets", "Errors: 0 sending, 0 receiving", **"Link changes: 2 up, 1 down"** (the link went down across the stand-by and came back), link "logged in (carries the network)", "Kind: Linux (thunderbolt_net)" | **pass** |

Evidence: `E:\xhci98-scratch\w49\net\C1u-serial-A.log` (and `C1s-serial-vga.log`, `C1t-serial-run1.log`),
`E:\xhci98-vm\p49\net\cells\C1u\leg.txt`, `E:\xhci98-vm\p49\net\lnx1\panic.png`.

### 8 x86 (cell C4) and 8.1 x86 (cell L1, below)

8 x86 (q35 base `w8x86`, image `w8a`) and 8.1 x86 (`w81x86`, `w81a`): the package installed in an
administrator console (Win+X, Command Prompt (Admin), Yes; typing a whole command into Run drops characters on
8, 8.1 and 10 under TCG), the publisher prompts clicked (`kit\redc.py`), exit 3 each. In the cell, the
machine-wide `XhciCmNet` 1 and the NHI restarted (the emulator's new identity).

**8 x86 with Linux (C4):** ping 6 of 6 (15-23 ms); "Thunderbolt networking - up", "Adapter MAC:
82-0A-E9-27-4A-10", "IP address: 169.254.26.242 (255.255.0.0)", "Other computer's MAC: 02-B5-28-1B-72-CE",
"Errors: 0 sending, 0 receiving", "Network driver: xhcinet6.sys, MTU 1500", link "logged in (carries the
network)", "Kind: Linux (thunderbolt_net)". **pass**. Evidence `E:\xhci98-scratch\w49\net\C4-serial-A.log`.

**8.1 x86 (B of cell L1, with 98 SE as the other end):** ping 6 of 6; "Thunderbolt networking - up", "Adapter
MAC: 3A-A1-6A-51-43-D0", "IP address: 169.254.94.241 (255.255.0.0)", "Other computer's MAC:
0E-A3-8C-B7-20-90" (98 SE's), "Errors: 0 sending, 0 receiving", "Network driver: xhcinet6.sys, MTU 1500",
"Ring interrupt: off (checked every clock tick)", link "logged in (carries the network)", "Kind: xHCI98".
**pass**. Evidence `E:\xhci98-scratch\w49\net\L1-serial-B.log`.

### The latch with 98 SE as the end that latches (cell L1): not reachable on this model

The qemu flavour of `xhcicm.sys` built for it: gated x86 `build-driver.cmd qemu` of `5458cd114` (slot gated.1,
`XHCI98_SELFTEST_SKIP_OK=1`): BUILD + GATES PASSED, 8:33 (`E:\xhci98-scratch\w49\net\gated-x86-qemu.log`;
record `out\gated-x86-qemu.txt`, `dirty=0`); `src\cm\objchk_qemu\i386\xhcicm.sys` SHA-256
`5532D65BA5DA95715AAFE009B1D793BE2FB19C18C5A591542772382BE97CA19F`.

A: 98 SE (q35 base `w98se`, image `w98a`: `I98.BAT`, exit 3, two cold starts), `-Usb4 tb3ar`, 1 vCPU, the
server of TCP 49638. B: 8.1 x86 (above).

| Step | Read |
|---|---|
| before, the release `xhcicm.sys` | ping 8.1 6 of 6; "Thunderbolt networking - up", "Ring interrupt: off (checked every clock tick)" (the 98 SE default, `XhciCmRingInterrupt` 0) |
| `L98.BAT` (the qemu `xhcicm.sys` as `C:\WINDOWS\SYSTEM32\DRIVERS\XHCICM.SYS`, the release one kept as `XHCICM.REL`; machine-wide `XhciCmRingInterrupt` 1 and `XhciCmQemuIrqDead` 3), shut down, cold start | |
| after | ping 8.1 6 of 6 (6-46 ms); 8.1 to 98 SE 6 of 6; "Thunderbolt networking - up", "XhciCmRingInterrupt (advanced): 1 - yes (machine-wide)", **"Ring interrupt: off (fell back to checking every clock tick)"**, link "logged in (carries the network)", "USB4 frames in: 175, 0 with errors", "Errors: 0 sending, 0 receiving" |
| the qemu flavour's log (debug port) | `cm.irq.value=00000001`, `cm.irq.qemu.dead-after=00000003`, **`cm.irq.resource=00000000`** (no line, no message), `cm.irq.none=00000000`; no `cm.net.irq.latched` |
| the report's PCI configuration of the NHI | "Interrupt pin INTA, line not assigned (FFh)" |

Verdict: **not read**. On this model 98 SE gives the Alpine Ridge NHI no interrupt line (line FFh), so the ring
interrupt asked for falls back to polling at once (`cm.irq.none`), before any interrupt; the qemu flavour's dead
line, which needs a connected line, never engages, and the latch itself (`cm.net.irq.latched`) is not reached.
What was read: on 98 SE the fallback with no line is reported as "fell back", and the network keeps working
on polling. The latch at a 98 SE end needs an NHI with a line on 98 SE (a model or configuration where the
NHI's interrupt reaches the tunnel port's line, record 21 section 6.1.1); left.
Evidence: `E:\xhci98-scratch\w49\net\L1-serial-A-after.log`, `L1-debugcon-A.log`, `E:\xhci98-vm\p49\net\cells\L1\leg.txt`.

### Vista x86 (cell C5)

Vista x86 (q35 base `vx86`, image `vxa`). The first two preparations stopped at exit 4 ("Windows is still
installing something ... usually a Found New Hardware window"): Vista's Found New Hardware for an "Unknown
Device" and the "SM Bus Controller" of the q35 base comes back at each start and "Ask me again later" leaves it
pending. Each answered "Don't show this message again for this device" (and the UAC prompt Continue), the
installer in an elevated console gave exit 3; after a restart, "xHCI98 Thunderbolt Networking - Device driver
software installed successfully". In the cell (new NHI identity): machine-wide `XhciCmNet` 1, the NHI restarted.

Vista with Linux: ping 6 of 6 (15-25 ms); "Thunderbolt networking - up", "Adapter MAC: C2-6C-27-FF-48-20",
"IP address: 169.254.208.12 (255.255.0.0)", "Other computer's MAC: 02-B5-28-1B-72-CE", "Errors: 0 sending, 0
receiving", "Network driver: xhcinet.sys, MTU 1500", link "logged in (carries the network)", "Kind: Linux
(thunderbolt_net)". **pass**. Evidence `E:\xhci98-scratch\w49\net\C5-serial-A.log`.

### The Linux end itself

The Kubuntu live end died twice after about 50 minutes up, under no particular traffic: "Kernel panic - not
syncing: Attempted to kill init! exitcode=0x0000008b", "CPU: 0 UID: 0 PID: 1 Comm: systemd" (systemd's own
segmentation fault in user space; `lnx2-serial.log`). Not this project's: the end was restarted (lnx3, lnx4)
before each 50 minutes from then on. On this host QEMU's TCP socket serial also stops accepting after a few
client connections (the server keeps the old ones in CLOSE_WAIT), so the Linux end's console is logged to a
file (`logfile=`) and its NetworkManager profile (`tbn`, 169.254.10.1/16) addresses each new `thunderbolt0`.

## 49.6 The two docks' remaining emulator legs: the network functions, and a chain behind a dock (2026-11-11, worker net)

The docks of `usb4-emu` (`docs/DOCKS.md`) on the emulator above, hot-plugged by QMP `device_add` on the
Alpine Ridge host (`-Usb4 tb3ar`), the network functions given a backend: QEMU's user-mode network with
`restrict=on` (`-netdev user,id=dn,restrict=on`: DHCP and the virtual gateway `10.0.2.2` only; nothing
reaches the host's network), `nic-netdev=dn` on the dock.

### 10 x86 (cell D1)

10 x86 (image `w10a`), 2 vCPU, `-Tablet`, QMP 49627; commands typed into an elevated console.

| Step | Read | Verdict |
|---|---|---|
| HP Thunderbolt Dock G2 (`dock-model=hp-g2,nic-netdev=dn`) plugged | XHCITR: "HP Thunderbolt Dock G2 (Thunderbolt 3), approved"; its xHCI `8086:15F0` "USB xHCI Compliant Host Controller (Windows)" (10 keeps its own driver on it), the two hubs, "RNDIS/QEMU USB Network Device" (`0525:A4A2`) bound by Windows to `usbser` ("USB Serial Device", configuration 2), no network adapter | the network function is not xHCI98's on 10: neither the xHCI nor a network driver is ours there |
| HP dock removed (`device_del`), the CalDigit TS3 Plus (`dock-model=ts3plus,nic-netdev=dn`) plugged | its three xHCIs (`1B73:1100` twice, `1B21:1242`) under Windows' xHCI driver, problem 0; "Intel(R) 82574L Gigabit Network Connection" (the I210's stand-in, Windows' own driver) behind the PCIe tunnel, `10.0.2.15` from DHCP server `10.0.2.2` | |
| ping `10.0.2.2` through the dock's NIC | 6 of 6, 1 ms | **pass** (the PCIe tunnel carries the dock's NIC) |
| **chain**: TS3 Plus with `downstream-ports=1`, then a Thunderbolt 3 NVMe enclosure (`usb4-device,parent=enc0,port=1,dialect=tb3,drive=nvme0`) plugged behind it | XHCITR: both "approved"; the dock "link 1, depth 1", route `0000000000000001`; the enclosure "link 1, depth 2", route `0000000000000301`; Windows: "QEMU NVMe Ctrl" (SCSI), volume E: "ORICO" | |
| a file written to E: and compared | `copy` then `fc /b`: "FC: no differences encountered" | **pass** |
| the chained enclosure removed by surprise, 30 s | E: gone, the console answering | **pass** |
| the dock removed by surprise, 30 s | the dock's adapter gone from `ipconfig`, no hang | **pass** |

Evidence: `E:\xhci98-scratch\w49\net\D1-serial-hp.log`, `D1-serial-ts3.log`, `D1-serial-chain.log`;
`E:\xhci98-vm\p49\net\cells\D1\`.

### XP (cell D2): the HP dock's network function under xHCI98

XP (an overlay of `xpa`), `-Usb4 tb3ar`, QMP 49627, the transfer disk the `-free` package of `3.4.3.0`
(`xhci98-3.4.3.0-free.zip`, `E:\xhci98-vm\p49\net\kitf`). `INSTALL.BAT /console /list` lists
"free:net/cdcnet USB Ethernet (cdcnet): CDC ECM, CDC NCM and RNDIS adapters"; then
`INSTALL.BAT /console /yes /nopause /thunderbolt /tbnet /wdmfree:stage=net/cdcnet`: "free:net/cdcnet staged.",
"Exit code 3".

| Step | Read | Verdict |
|---|---|---|
| HP Thunderbolt Dock G2 (`dock-model=hp-g2,nic-netdev=dn`) plugged; Found New Hardware walked (automatic) for each new device | "xHCI98 USB 3 Controller (Intel Titan Ridge)" (`8086:15F0`, driver `xhci98`, "behind HP Thunderbolt Dock G2"), "xHCI98 USB 3 Root Hub", the hubs; then "System Settings Change ... restart" (declined) | |
| `ipconfig /all` | "USB Ethernet (Linux USB gadget)" (`0525:A4A2`, "RNDIS/QEMU USB Network Device", configuration 1, at "USB port 5.3.3 on xHCI98 USB 3 Controller (Intel Titan Ridge)", driver `cdcnet`): IP `10.0.2.15` from DHCP server `10.0.2.2` | |
| ping `10.0.2.2` | 6 of 6, 1 ms | **pass**: the dock's network function under xHCI98 |
| dock removed by surprise (`device_del`), 40 s | the adapter gone, the TB networking adapter left, the desktop answering | **pass** |

Evidence: `E:\xhci98-scratch\w49\net\D2-serial-A.log`; `E:\xhci98-vm\p49\net\cells\D2\` (`wiz-fin-*.png`, `p1.png`).

### XP: the chain behind the HP dock, and two faults seen once each (cells D2 to D5)

**The chain on XP.** The HP dock with `downstream-ports=1` and a Thunderbolt 3 NVMe enclosure behind it
(`parent=enc0`), plugged while XP runs: Found New Hardware walked for the dock's functions; the chained drive is
not listed by `wmic diskdrive` until the next start (D3). After a restart with both present (D4), XP lists
"QEMU NVMe Ctrl SCSI Disk Device" (Stor98) beside the dock's RNDIS adapter (`10.0.2.15`, ping `10.0.2.2` 6 of
6), and `shutdown -s` with dock, chain and network up finished (D4, under 3 minutes). Restarts with the dock and
the chain present: D3 (no network driver) and D4 (cdcnet) each back at the desktop in about 1.5 minutes.

**Fault 1, a shutdown that does not finish (D2, and again in D6).** D2's order: the free cdcnet staged; the HP dock
without a chain plugged, its Found New Hardware walked, XP's restart question declined, the RNDIS ping; the
dock removed by surprise; the dock again with `downstream-ports=1`, then the chained enclosure, the Found New
Hardware walked by a key script whose Enter answered XP's "System Settings Change ... restart your computer
now?" Yes. XP then stayed at "Windows is shutting down..." from about 03:25 (the disk's last write) until it
was stopped at 03:34 (`E:\xhci98-vm\p49\net\cells\D2\c2.png`, `c4.png`; `D2-hang-regs.txt`: CPU 0 in kernel,
EIP not mapped to an image here). The release `xhcicm.sys`, so no CM log. D3 (the same dock and chain, no
cdcnet, the qemu flavour's `xhcicm.sys`) and D4 (D2's order without the first plug and its surprise removal)
restarted and shut down normally; D5, D2's whole order with the qemu flavour, stopped earlier at fault 2.
**D6, D2's whole order again on a fresh overlay with the qemu flavour: the same.** "Windows is shutting
down..." from about 05:19 (after the second Found New Hardware cycle answered the restart question) to the end
of the watch (`cells\D6\c2.png`, `sd-*.png`, `sd.txt`); both vCPUs halted in the idle loop (`HLT=1`, the same
EIP in three samples, `D6-hang-eip.txt`), so Windows waits on something rather than spinning. The CM log
(`D6-debugcon.log`) has no `cm.stop` (D3 and D4 log `cm.stop=00000002` at their shutdowns): the NHI was never
stopped. Its last entries, about a minute before the shutdown began: from the surprise removal earlier
`tbt.icm.device-gone=00000300`, `cm.action.tunnel-down`, `cm.reenum.departure=00000208`; the dock connected and
approved again; the chained router connected and approved (`tbt.icm.version-header=15D38086` ...
`tbt.icm.approved=00000001`); then `cm.chain.owed=00020001` (the chained unit's action owed, depth 2) and, 10 s
later, `cm.chain.owed-expired=00020001`: the chained unit never reached PCI enumeration (as in D3, whose
chained drive was not listed until the next start). Two of two with this order. The cause is not read. A
reading of the code (no run) qualifies the log and names where a wait could be indefinite: `cm.stop` is
written only at the end of the NHI's stop (`src\cm\cm_nhi.c` about line 4247, after the stop work is queued,
waited for and the thread joined), so its absence does not show that the NHI saw no removal; and
`cm.reenum.departure` is logged when a BusRelations population shrinks, not when the child's teardown is done.
Candidates, none confirmed: the tunnel-bridge filter's synchronous forward of a child's PnP IRP, which waits
without a limit for the lower stack (`cm_filt.c` about 1337, `CmForwardSync` in `cm_entry.c` about 134), with
the surprise-removed dock subtree's lower stack unfinished; a filter or bridge removal draining a reference
(`cm_filt.c` about 1344 and 1357, `cm_reenum.c` about 496); the NHI's stop or a retained system power IRP
waiting on the CM thread (`cm_thr.c` about 652 and 983, `cm_power.c` about 452 and 692); the ring interrupt's
DPC rundown (`cm_rirqos.c` about 643; unlikely with no line). The chain-owed expiry itself holds no IRP or
reference (`cm_reenum.c` about 5048): the owed entry and the hang look separate. Next step: entry and exit notes
on the filter's synchronous forwards and drains (filter, PDO, IRP minor) and on the NHI's PnP and power
dispatch, then D2's order once more.

**Fault 2, STOP 0x8E in ACPI.sys (D5, once).** D5: XP (fresh overlay of `xpa`), the qemu flavour's
`xhcicm.sys` copied in and XP restarted (no dock), then `INSTALL.BAT /console /yes /nopause /thunderbolt
/tbnet /wdmfree:stage=net/cdcnet`. The installer found "xhcicm.sys: same version, other bytes: replaced,
loaded at next start", staged, and was "Waiting for Windows to finish installing new devices" (its log's last
line, 04:47:18, read from the guest disk afterwards, `E:\xhci98-scratch\w49\net\d5logs\XHCIINST.LOG`). The
qemu flavour's log ends with the NHI's stop (`cm.live.final=00000001`, `cm.stop=00000002`) and no start; the
screen: "*** STOP: 0x0000008E (0xC0000005,0xF7806BAE,0xF729A980,0x00000000)", "*** ACPI.sys - Address F7806BAE
base at F77E8000, DateStamp 480252b1" (`D5-stop8e.png`, `D5-stop-regs.txt`, `D5-debugcon.log`). D4 ran the same
installer step on the same image (the same "replaced" line) and its log shows `cm.stop=00000002` followed by
`cm.start=00000001`, no fault. So once in two: an access violation inside ACPI.sys between the NHI's stop and
its next start on XP, the qemu flavour loaded, during the installer's device restart. Its cause is not read
(no dump was opened: the brief's rule); it lies in the NHI stop path's neighbourhood and is this project's to
chase. Recorded as a planned item (ACPI8E.1).

Evidence: `E:\xhci98-vm\p49\net\cells\D2\` to `D6\` (`leg.txt`, `sd.txt`, screenshots),
`E:\xhci98-scratch\w49\net\D3-debugcon.log`, `D4-debugcon.log`, `D4-serial-A.log`, `D5-debugcon.log`,
`D6-debugcon.log`, `D6-hang-eip.txt`.

### What 49.6 and 49.7 leave

- 49.7: the latch with 98 SE as the end that latches (98 SE's NHI has no interrupt line on the emulator's
  Alpine Ridge model, so the ring interrupt falls back before the qemu dead line can act).
- 49.6: nothing of its two legs; the two XP faults above (the shutdown that does not finish, two of two in D2's
  order; the STOP 0x8E in ACPI.sys, once in two installer runs) to be chased as their own items.
- The emulator note: `launch-q35.ps1` gives `usb4-host` no `uuid`, so Vista and later see a new NHI at every
  start; a fixed `uuid=` per image would keep the installer's per-NHI values (the cells here used the
  machine-wide value instead).

### Process notes (worker net)

- From about 01:57 to 02:10 this worker's guests used 11 vCPUs, one over its allotment of 10: the 10 x86
  preparation, believed stopped, was still running; stopped at 02:10.
- The kit scripts (`E:\xhci98-scratch\w49\net\kit\`) reuse w48 net, net9 and net10's; the guest batch files are
  theirs unchanged, plus `L98.BAT`, `LATCH98.REG` and `QCM.BAT` (in `E:\xhci98-vm\p49\net\kitq`).

## 49.6 (fault 1) XP's shutdown that does not finish after the HP dock's surprise removal: where it waits (2026-11-11, worker xpsd)

Worker net's fault 1 (49.6 above: D2 and D6, 2 of 2): XP, the HP Thunderbolt Dock G2 on the emulator
(`E:\xhci98-vm\q35-bases\bin\dock-20261110-bar0`, `-Usb4 tb3ar`), plugged, removed by surprise, plugged again
with a chain port and a chained NVMe enclosure, the restart question answered Yes: "Windows is shutting down..."
without end, both vCPUs halted.

### The probes (commit c8e594631, the qemu flavour alone)

The CM log's ring reaches port 0xE9 only when the thread or a stop emits, which a wait that never ends can keep
from happening, so the qemu flavour gained wait probes that write their line at once (`CmProbe`, `cm_log.c`, the
binary's one port-0xE9 site; no new DbgPrint site): `probe.<label>=<a>,<b>,<c>,<ms>,<stack page|IRQL>`.
- `CmWaitEvent` (every unbounded wait of the driver: `CmForwardSync`, the filters' drains, `CmRegWithdraw`'s
  rundown, the PnP path's waits on the thread, `CmPowerDrain`, ...) writes `wait.stuck` every 5 s while
  unsatisfied, `wait.ended` when it ends.
- Entry and exit of the PnP and power dispatch (`pnp.in/out`, `pwr.in/out`: the device, its kind's letter, major,
  minor, power type and state, the status), `CmForwardSync` (`fwd.in/out`), the drains (`drain.child`,
  `drain.bridge`, `withdraw.wait`), the thread's PnP work and join (`thr.runpnp`, `thr.stop`), its work bits
  (`thr.bits`), the NHI's power completions (`pwr.sysdone`, `pwr.devdone`, `pwr.syswork`, `pwr.d3work`).

Gated qemu x86 build of the branch (self-test skip on): **BUILD + GATES PASSED**
(`E:\xhci98-scratch\w49\xpsd\gated-qemu-r1.log`); `xhcicm.sys` (qemu) sha256
`79980c1b33ccf755944a7742ea66a0fa563d5c84b74ba0e17a7f9b59bdec38a7`.

### X1: net's D6 order, the probes loaded: reproduced (3 of 3 with D2 and D6)

XP (a fresh overlay of a copy of net's `xpa`, which carries this tree's package at 3.4.3.0 with `/thunderbolt
/tbnet`), 2 vCPU, QMP 49621, transfer disk net's `kitf` plus the probes' `xhcicm.sys` (`kitx`); net's `d6.sh`
order unchanged (`E:\xhci98-scratch\w49\xpsd\kit\x1.sh`): the probes' `xhcicm.sys` copied in and XP restarted;
`INSTALL.BAT /console /yes /nopause /thunderbolt /tbnet /wdmfree:stage=net/cdcnet` ("Exit code 3"); the dock
without a chain plugged, Found New Hardware walked, restart declined, ping 6 of 6; the dock removed by surprise
(`device_del`) at 06:05:50; the dock with `downstream-ports=1` at 06:06:30, the chained enclosure at 06:08:00;
the wizards walked, the restart question answered Yes.

| Read | Result |
|---|---|
| screen from about 06:09 | "Windows is shutting down..." until stopped at 06:24 (`cells\X1\now1.png`, `sd-*.png`) |
| HMP `info registers -a` | both vCPUs `HLT=1`, `EIP=f78e8d3e`, as in net's D6 (idle) |
| `wait.stuck` | **none**: no unbounded wait of `xhcicm.sys` was unsatisfied for 5 s, at any time |
| `pwr.in` | **none in the whole run**: no power IRP reached any `xhcicm.sys` device, so neither the shutdown's system IRP nor a device IRP is held by the NHI or a filter |
| `pnp.in` without `pnp.out`, `fwd.in` without `fwd.out` | none open at the log's end |
| the PnP IRPs to `xhcicm.sys` after the surprise removal | at ms `0xCE4C9` (the removal) the bridge filter's BusRelations forwarded and back, then QUERY_DEVICE_RELATIONS to the dock's child filters (status `C00000BB` from below); **after that, nothing but IRP_MN_QUERY_CAPABILITIES** to any device of the driver until the shutdown: no IRP_MN_SURPRISE_REMOVAL to the departed dock's child filters, no REMOVE, and no BusRelations, start or query for the dock plugged again 40 s later or for its chain |
| the CM log | `cm.chain.owed=00020001`, then `cm.chain.owed-expired=00020001`, as in D6: the chained router's PCI enumeration never came |
| the log's last line | ms `0xFE1CC`, about when the shutdown began; nothing after, for 10+ minutes |

Reading. Windows sends a surprise removal's IRP_MN_SURPRISE_REMOVAL to the departed devices from the leaves up, the
bridges (this driver's child filters) after the functions below them. None reached the child filters, and the
PnP manager sent nothing else to this driver's devices afterwards (no enumeration of the re-plugged dock, which is
also why the chained unit's action expired): the PnP manager's removal of the departed subtree stopped below the
bridges, in a function of the dock (its xHCI under `xhci98.sys` and the USB devices behind it, the RNDIS adapter
under `cdcnet`), not in a wait of `xhcicm.sys`. XP's shutdown then appears to wait for that PnP work (an
inference): no system power IRP is sent (none reached the NHI or a filter). The candidates the code reading
ranked (the filter's synchronous forward, the drains, the NHI's stop or a held power IRP) did not occur in this
order: each would have shown as an open probe or a `wait.stuck` line.

Side observation: throughout the run (from the probes' first start, before any dock), about 34
IRP_MN_QUERY_CAPABILITIES a second reach the tunnel's bridge and child filters (and one every 2.3 s the NHI), steadily, all answered; its sender is not read.

Evidence: `E:\xhci98-vm\p49\xpsd\cells\X1\` (`leg.txt`, `sd.txt`, screenshots, `A\debugcon-A.log`),
`E:\xhci98-scratch\w49\xpsd\X1.log`.

### X2 and X3: the re-plug and the chain are not needed; the network function is

Both on fresh overlays of the same image, the probes' `xhcicm.sys` loaded, one run each.

| Cell | Order | Shutdown | The probes |
|---|---|---|---|
| X2 (QMP 49622, `kit\x2.sh`) | X1's order up to the surprise removal (cdcnet staged, the dock plugged, wizards, restart declined, ping 6 of 6, `device_del`), then 40 s, then `shutdown -r` at 06:31; no re-plug, no chain | **"Windows is shutting down..." from 06:31 until stopped at 06:40**, both vCPUs `HLT=1` at `EIP=f78e8d3e`, the debug console silent (`cells\X2\sd.txt`, `sd-*.png`) | as X1: no IRP_MN_SURPRISE_REMOVAL to any child filter, no `pwr.in`, no `wait.stuck`, no open pair |
| X3 (QMP 49623, `kit\x3.sh`) | X2's order **without** the cdcnet staging and the ping (the dock's RNDIS function has no driver), the dock removed by surprise at 06:29:18, `shutdown -r` at 06:29:58 | **restarted, back at the desktop at 06:31** (`cells\X3\now.png`) | seven IRP_MN_SURPRISE_REMOVAL to the departed dock's child filters (ms `0xAC331` on), then the shutdown's system power IRPs (`pwr.in` ... `43160206`: SET_POWER, system, PowerSystemShutdown) through the filters, all returned |

Reading. The fault needs only the surprise removal of the dock while its RNDIS function is bound to `cdcnet` (and
was used); the re-plug, the chain and the chain's owed action are not part of it. With the network function
driverless the same removal is processed to the bridges and the shutdown finishes. So the removal stalls in the
stack of the dock's network function: the `cdcnet` miniport's surprise removal or halt (NDIS), or the USB stack
below it failing to give back what the miniport waits for at the surprise removal (the dock's xHCI under
`xhci98.sys`, its hubs). Which of the two is not read: the dock's `xhci98.sys` here is the release build (no
log), and `cdcnet` is usb-wdm-free's. A second reading of the code (no run) found this sound as the leading
hypothesis; not a proof that `xhcicm.sys` is uninvolved (an earlier action of its could cause a later failure
elsewhere; a forwarded IRP pending below would not show as an open probe; raw waits outside `CmWaitEvent`, in
`cm_oscnt.c` and the thread join, are unprobed but would leave an open dispatch probe), and that the shutdown
waits on the stalled PnP work is an inference. The kept-child path is NT 6.3 alone (not XP), and the children's
`C00000BB` answers are completed, not abandoned.

Next step (not run): X2's order with the qemu flavour of `xhci98.sys` too (its log on the debug console) and the
debug build of `cdcnet`, to read whether the miniport's halt waits for transfers the HCD does not complete after
the controller is gone, or the HCD's surprise removal itself waits.

Evidence: `E:\xhci98-vm\p49\xpsd\cells\X2\`, `cells\X3\` (`leg.txt`, `sd.txt`, screenshots, `A\debugcon-A.log`);
`E:\xhci98-scratch\w49\xpsd\X2.log`, `X3.log`; the second reading in the same folder.

**Not merged (coordinator, 2026-11-11).** The probes stay on branch `w49-xpsd` (`c8e594631`) for the
next leg of this fault: they write about 6 MB of debug-console output in a 40-minute qemu leg.

## 49.25 XP's shutdown that does not finish after the HP dock's surprise removal: the wait found (2026-11-11, worker xpsd, day wave)

The night run ("49.6 (fault 1)" above) left the removal stalled below the tunnel bridges, in the stack of the
dock's network function, without telling `cdcnet`'s halt from the dock's xHCI under `xhci98.sys`. This run reads
both. No code change of this project's: branch `w49-xpsd` is still at `c8e594631` (the probes, not merged), and
no fix branch was made (the fix is task 49.16's, below).

### What was loaded

- XP as in X2 (a fresh overlay of `E:\xhci98-vm\p49\xpsd\xpa\xpa.qcow2`, the package at 3.4.3.0 with
  `/thunderbolt /tbnet`), 2 vCPU, `-Usb4 tb3ar` (the firmware connection manager), the emulator
  `E:\xhci98-vm\q35-bases\bin\dock-20261110-bar0`, the HP Thunderbolt Dock G2 model with `nic-netdev=dn`.
- `xhcicm.sys`: the qemu flavour with the probes (`c8e594631`), sha256 `79980c1b...bdec38a7`, as in X1 to X3.
- `cdcnet.sys`: usb-wdm-free's own **trace** flavour (`build.cmd ndis5 trace`: the free build plus a trace on
  port 0xE9, `cdc: ...` lines), built unchanged from a copy of usb-wdm-free main (`6a266c7`) `src\net\cdcnet` in
  `E:\xhci98-scratch\w49\xpsd\wdmfree` (nothing written to usb-wdm-free), version 0.1.3.0, sha256
  `038478d36a954a7a012b10696e8ce26c0d8a095c61fdb54a33a572bd50975a22`; copied over the staged
  `C:\XHCI98\USBDRV\FREE\net\cdcnet\w2k-xp\CDCNET.SYS` after the staging and before the dock is plugged (`CT.BAT`).
- `xhci98.sys`: see the note on the qemu flavour below. In X4b, X5 and X7b the dock's controller ran the
  **released** `xhci98.sys` of 3.4.3.0 (sha256 `3b35b4ae...e6c9fc7266`); in X6 and X8 the qemu flavour of this
  tree (sha256 `85a7217be18ddf20ed8357335dc91a5d37086e245b8716a5ac81ada06f771b6a`, the gated qemu build of
  `c8e594631`, whose `src` outside `src\cm` is `3.5.0.0`'s).

**A note for later legs: putting a qemu flavour of `xhci98.sys` under a dock on XP.** Copying it over
`system32\drivers\xhci98.sys` and restarting is not enough, twice over. (1) The installer's run
(`INSTALL.BAT ... /wdmfree:stage=...`) puts the package's file back and restarts the controllers, so the image
is reloaded from it (the qemu flavour's last line is `hcd: release: BAR`, then nothing). (2) With the installer
run first and the flavour copied afterwards, XP's own install of the dock's new xHCI device copies `xhci98.sys`
from the install's copy (`C:\WINDOWS\XHCI98\<id>\`), finds another file in `drivers`, and reloads the driver from
it for every controller (again `hcd: release: BAR`, then silence). The flavour must replace that copy as well
(`kitx4\QX.BAT`). X4b, X5 and X7b were run before this was understood: their dock controller logged nothing
and ran the released binary, which the XHCITR report read instead.

### X4b and X5: the wait is `cdcnet`'s halt, on requests `xhci98.sys` keeps

X2's order unchanged (`E:\xhci98-scratch\w49\xpsd\kit2\x4.sh`): the flavours copied in, XP restarted;
`INSTALL.BAT /console /yes /nopause /thunderbolt /tbnet /wdmfree:stage=net/cdcnet` ("Exit code 3"); `CT.BAT`; the
dock plugged, Found New Hardware walked, restart declined, ping `10.0.2.2` 6 of 6; `device_del`; 40 s; X5 alone:
`XHCITR /report`; `shutdown -r -t 0`.

| Read | X4b (QMP 49624) | X5 (QMP 49625) |
|---|---|---|
| shutdown | "Windows is shutting down..." 09:16 to 09:21 (stopped), both vCPUs `HLT=1` at `EIP=f78e8d3e`, the debug console silent | the same, 09:44 to 09:49 |
| the removal in the CM log | `tbt.icm.device-gone=00000300`, `cm.reenum.departure=00000208` (ms `0xF36F6`) | the same (ms `0xF8F95`) |
| `cdc: halt: begin` (NDIS calls MiniportHalt for the surprise removal) | 16 ms after the departure | 16 ms after |
| what `cdcnet` then logs | **`sync: URB 13 timed out, cancel 1`** about 2.1 s later, and nothing more: the worker's departure probe (GET_STATUS_FROM_DEVICE, `cdcusb.c` `DeviceStatus`) timed out after its 2 s, `IoCancelIrp` returned TRUE, and the request **never completed** (no `completed after cancel`); `CdcUsbStop` waits for the worker (no `stop: worker exited`), so the halt never returns | `stop: worker exited`; `sync: URB 2 timed out, cancel 0` (the quiesce's first ABORT_PIPE, 2.2 s); `sync: URB 2 completed after cancel` (about 4.8 s after the halt began); **`sync: URB 2 timed out, cancel 0`** again (the next ABORT_PIPE) and nothing more: that request never completed, so the halt never returns |
| `halted:` | never | never |
| PnP and power IRPs to `xhcicm.sys` after the removal | no IRP_MN_SURPRISE_REMOVAL, no REMOVE, no `pwr.in` (as X1, X2) | the same (after the removal: 2030 QUERY_CAPABILITIES, 14 QUERY_DEVICE_RELATIONS, nothing else) |
| XHCITR report in the stalled state, the dock's controller (8086:15F0, released 3.4.3.0) | not taken | "controller CONTAINED: USBSTS read all ones, the controller unreadable", "**containment PINNED: no proof DMA stopped; buffer, transfers kept**", "pool rings held 5 of 32"; the note ring's end: `tol.dead.stamp`, `cmd.timeout`, `cmd.timeout.trb=0F030500` (a Stop Endpoint, slot 3, endpoint 5), `cmd.givenup=0F030500`, `ctrl.failed.here=1`, `cfg.stop.giveup=00030501`, `ctrl.failed.unreadable=000003FC`, `tol.contained=00000002` |

### X6: the same with the qemu flavour of `xhci98.sys` on the dock's controller

`kit2\x6.sh` (the installer first, then the flavours over the installed files and the install's own copy, XP
restarted, then X5's order; QMP 49626). The dock's controller starts in the qemu flavour
(`xhci98: PCI vendor/device=15F08086`, `hcd: XhciDmaStaging, effective=00000000`); ping 6 of 6; `device_del`
at 10:51:50; the report; `shutdown -r` at 10:53:17: "Windows is shutting down..." to 10:58 (stopped), both vCPUs
halted.

| ms after the departure (`0xB461C`) | Line |
|---|---|
| 16 | `cdc: halt: begin`, `cdc: stop: worker exited` |
| 2000 | `cdc: sync: URB 2 timed out, cancel 0` (the first ABORT_PIPE) |
| 3297 | `xhci98: command: timed out, aborting TRB=062820D0`, `CRCR: no valid operand for the abort write`, `command: abort not written, CRCR=FFFFFFFF`, `hcd: controller reset requested, calls=00000001`, `interrupt mask: a register window is not decoding`, `interrupt mask: delivery is NOT proven suppressed`, `hcd: thread URB done, function/status=02000800`, then `cdc: sync: URB 2 completed after cancel` |
| 7047 | `cdc: sync: URB 2 timed out, cancel 0` (the next ABORT_PIPE): the last `cdc:` line |
| to the end | PnP to `xhci98.sys`'s objects of the dock (controller, root hub, hubs, device PDOs): minor 09 (QUERY_CAPABILITIES) and 07 (QUERY_DEVICE_RELATIONS) only; **no IRP_MN_SURPRISE_REMOVAL (0x17) and no REMOVE reached any of them** |

The report in the stalled state: "controller CONTAINED: USBSTS read all ones, the controller unreadable",
"containment PINNED: no proof DMA stopped; buffer, transfers kept", `cmd.givenup=0F030500`,
`cfg.stop.giveup=00030501`, `tol.contained=00000002` (`X6-report.txt`).

### Reading

Each step is a line above. Windows sent the surprise removal to the leaf first; NDIS called `cdcnet`'s
MiniportHalt; the halt waits, without a limit, for requests it gave the dock's xHCI - in X4b the worker's
cancelled control transfer, in X5 and X6 an ABORT_PIPE, and behind them its five bulk and interrupt transfers
(`Quiesce` waits until none is outstanding) - and `xhci98.sys` does not complete them: the dock's controller
reads all ones, the Stop Endpoint a cancel or an abort needs times out, the reset is refused, and the
containment, with no proof that the controller stopped mastering, pins and keeps every transfer
(`HcdEnumService`'s unproven branch, `hcd_enum.c`; `HcdIoDeviceDrain` returns before draining anything when
pinned and unstaged). Nothing above `cdcnet` in the dock's tree is asked to go before it returns, so no
IRP_MN_SURPRISE_REMOVAL reaches `xhci98.sys`'s objects (X6) or the tunnel bridges' filters (X1 to X6).

That is design record 17 section 4.13.1's wait, the case section 4.13.6 leaves standing under a firmware
connection manager (the tunnel's read-back is this release's own connection manager's alone), and the README's
driver issue ("Stop a dock before unplugging it when a keyboard, mouse, tablet or similar device is on the
dock's own USB controller"; the release notes: "it is not found again when plugged back, and Windows does not
shut down"): **task 49.16's mechanism**, met through a network adapter. The HP dock's adapter is built in, so
with `cdcnet` bound every surprise unplug of this dock on such a host meets it: D2, D6, X1 and X2 with the
released `cdcnet`, X4b, X5 and X6 with the trace flavour, 7 of 7. `cdcnet` waits as WDM asks (a cancelled
request is its lower driver's until it completes); nothing here is a fault of `cdcnet`'s or of `xhcicm.sys`'s.

Qualified after a second reading of the code (reviewed and revised): the traces do not show whether X4b's
control transfer had reached its ring (a request merely held after the containment would have completed at the
cancel, `hcdParkedCancel`, so it was not that); and the second ABORT_PIPE of X5 and X6 need not be waiting on
the kept transfers themselves: an abort queued between the controller's failure and the containment is left in
the slow queue, which the pinned drain returns before flushing (`hcd_io.c` about 3135, `hcd_cfg.c` about 6497),
and has no cancel routine (`cancel 0`). Completing that abort alone, or a request with no data, would be safe
without staging and would not end this wait: `cdcnet`'s quiesce still waits for its published bulk and
interrupt transfers, which only a proof or staging releases. That XP's shutdown waits on the stalled removal
stays an inference (no power IRP is sent while it stalls; in X7b and X8, where the removal finishes, XP shuts
down).

### X7b: the same order with `XhciDmaStaging` 1 on the dock's controller: removed, and XP restarts

X5's order with the installer run before the flavours are copied (`kit2\x6.sh`, see the note above; the dock's
controller again on the released 3.4.3.0 `xhci98.sys`), and, after the ping, `SG.BAT`: `XhciDmaStaging` = 1
written in the driver key whose `MatchingDeviceId` is `pci\ven_8086&dev_15f0`
(`...\Class\{36FC9E60-C465-11CF-8056-444553540000}\0004`, read back `0x1`) and that controller restarted
(`devcon restart "PCI\VEN_8086&DEV_15F0*"`: "1 device(s) restarted"); 45 s; ping 6 of 6 again; then
`device_del`, 40 s, the report, `shutdown -r -t 0` at 10:29:38. QMP 49627.

| Read | Result |
|---|---|
| `cdc: halt: begin` | 31 ms after the departure (ms `0xD08AC`) |
| `cdc: sync: URB 13 timed out, cancel 1` | 1.9 s later, as in X4b |
| then | **`sync: URB 13 completed after cancel`** 6.5 s after the departure, `probe: device gone (c000009d)`, `device gone: 5 transfers outstanding`, `indicate MEDIA_DISCONNECT`, `gone: 0 of 5 transfers still outstanding after the abort`, `stop: worker exited`, `halted: tx 54/0 rx 9 nobuf 0` |
| PnP IRPs to the child filters | 7 IRP_MN_SURPRISE_REMOVAL from ms `0xD2617` (1 s after the halt ended), then 7 REMOVE |
| the report after the removal | two controllers left (the host's own and QEMU's), "controller running", "containment none"; the dock's is gone |
| shutdown | the system power IRPs through the NHI and the filters (`pwr.in ... 160206`), the firmware's screen at 10:30:05, the desktop at 10:32 (`cells\X7b\back.png`) |

So the one setting task 49.16 names changes this fault from a shutdown that never ends to a normal removal, with
the released binary: 49.25 is 49.16's mechanism, met through a network adapter instead of an input device.
`XhciDmaStaging` 1 was read back from the registry and the controller restarted; the released build prints no
line for it (X8 below reads the driver's own line).


### X8: the qemu flavour of `xhci98.sys` with `XhciDmaStaging` 1 on the dock's controller: removed, and XP restarts

X6's order (`kit2\x6.sh X8 49628 sg rep`; the qemu flavour of `xhci98.sys` sha256 `85a7217b...f771b6a` on every
controller, the trace `cdcnet`, the probes' `xhcicm.sys`) with `SG.BAT` after the ping: the dock's controller
restarted by `devcon` ("1 device(s) restarted") and its own line then reads
`xhci98: hcd: XhciDmaStaging, effective=00000001` (the four starts before it, the host's two controllers and the
dock's twice, read `effective=00000000`); ping 6 of 6 again; `device_del` at 11:19:38; 40 s; the report;
`shutdown -r -t 0` at 11:20:44. QMP 49628. (The stopped worker's guest was still running when this worker took
over; its screen then showed XP's screen saver. Stopped by QMP `quit` at 12:30, logged in `pids.txt`.)

| Order after the departure (`cm.reenum.departure=00000208`, ms `0xCE083`) | Line |
|---|---|
| 1 | `cdc: halt: begin` |
| 2 | `cdc: sync: URB 13 timed out, cancel 1` (the worker's GET_STATUS, as X4b and X7b) |
| 3 | `xhci98: command: timed out, aborting TRB=0629A0D0`, `CRCR: no valid operand for the abort write`, `command: abort not written, CRCR=FFFFFFFF`, `hcd: controller reset requested, calls=00000001`, `interrupt mask: delivery is NOT proven suppressed`, `hcd: stream dequeue refused, cycling location=00000110` (X6's lines) |
| 4 | **`cdc: sync: URB 13 completed after cancel`**, `probe: device gone (c000009d)`, `device gone: 5 transfers outstanding`, `indicate MEDIA_DISCONNECT` |
| 5 | `xhci98: hcd: hub leaving, location/slot/hubs=00050102`, **`hcd: held URBs released, count=00000004`**, **`count=00000001`** (`HcdIoParkedRelease`, at `cdcnet`'s two ABORT_PIPEs) |
| 6 | `cdc: gone: 0 of 5 transfers still outstanding after the abort`, `device gone: reported`, `stop: worker exited`, `stopped`, **`halted: tx 54/0 rx 9 nobuf 0`** |
| 7 | `xhci98: hcd: device PDO PnP minor=00000017` (IRP_MN_SURPRISE_REMOVAL), the hubs' FDOs, the root hub, `hcd: controller PnP minor=00000017`, `hcd: stop controller`, the thread's exit, `quiesce: PCI command reads all ones - DMA not proven stopped` (3), `teardown: DMA NOT PROVEN STOPPED and the common buffer is about to be reclaimed - failing closed`, `stage: copies through a TransferBuffer=00000A34`, `hcd: release: BAR` |
| 8 | 7 IRP_MN_SURPRISE_REMOVAL to the tunnel's child filters (`probe.pnp.in ... 431B1700`), `cm.reenum.unexpected=00000208`, then minor 02 (REMOVE) to every `xhci98.sys` object of the dock and 7 to the filters |
| the shutdown | `cm.shutdown`, a second `cm.start=00000001` and the host's two controllers started again; the desktop at 11:26 (`cells\X8\sd-20.png`) |

Steps 1 to 6 fall inside about 5 s of the departure (the probes' stamps are coarse there). Evidence:
`E:\xhci98-scratch\w49\xpsd\X8-debugcon.log` (the removal from byte 4602689), `X8.log`, `cells\X8\` (`leg.txt`,
`sd.txt`, `sg.png`, `sd-*.png`); the report of this leg was taken after the removal and lists the two controllers
left, "containment none" (`cells\X8\A\serial-A.log`).

### X6 against X8: which requests, in which state, and what completes them

The same binary and the same order; one registry value apart.

| | X6 (`effective=00000000`) | X8 (`effective=00000001`) |
|---|---|---|
| the command that times out | a Stop Endpoint for slot 3, endpoint 5 (`cmd.timeout.trb=0F030500`, `cfg.stop.giveup=00030501`): the stop `cdcnet`'s first cancel or abort needs | the same lines |
| the controller | `ctrl.failed.here=1`, `ctrl.failed.unreadable`, `tol.contained=00000002` (pinned); the report: "CONTAINED: USBSTS read all ones", "PINNED: no proof DMA stopped; buffer, transfers kept" | contained without proof as well (the stop later logs "DMA NOT PROVEN STOPPED ... failing closed") |
| the client's requests | the driver's own counters in the stalled state: `TransfersSubmitted 3168`, `TransfersCompleted 3163`, `PoolRingsInUse 5`: **five transfers on the rings, never completed** (the five `cdcnet` counts in X8: its bulk-IN reads and its interrupt-IN read); `cdcnet`'s second ABORT_PIPE never returns (no second `hcd: thread URB done`) | the five completed and held on the PDO, released at `cdcnet`'s aborts (`held URBs released` 4 + 1); "0 of 5 transfers still outstanding after the abort" |
| the removal | no IRP_MN_SURPRISE_REMOVAL to any `xhci98.sys` object or filter; the shutdown never ends | complete; XP restarts |

The code that decides it (`src`, this tree):
- A containment with no proof (`hcdContainProof` returns none: configuration space all ones is refused as a
  proof, and the firmware connection manager's client makes no tunnel statement, section 4.13.6) calls
  `HcdSvcDmaNotStopped` (`CommonBufferPinned = 1`) and then `HcdEnumContain(hc, 0)` (`hcd_ctl.c`, the
  containment; `hcd_enum.c`, `HcdEnumService`'s unproven invalidation takes the same two calls).
- `HcdEnumContain` (`hcd_enum.c`): `if (proof || hc->Staging) hcdDropAll(hc); else hcdDrainAll(hc);`.
- `HcdIoDeviceDrain` (`hcd_io.c`), which both call for each device, marks the device gone and then:
  `if (hc->CommonBufferPinned && !hc->Staging) return 0;` - before `HcdCfgFlushDevice` (the requests queued for
  the thread: a queued ABORT_PIPE among them) and before `HcdIoDrainPipe(..., HCD_USBD_DEVICE_GONE)` for every
  pipe. With staging 0 **nothing is completed and nothing is taken off a ring**: the rings name the client's own
  pages (direct DMA) and the controller is not known to have stopped, so a completed request's buffer could be
  written after its owner freed it. That is the whole reason the requests are kept; it is the buffer that must
  be held, and for a client's buffer the only hold a lower driver has is the pending request.
- With staging 1 the rings name only this driver's staging buffers, so the same drain runs to its end: every
  transfer ends `DEVICE_GONE` and is held on its PDO (`HcdIoPark`; one already cancelled is completed at once,
  which is X8's URB 13), an abort or a cancel releases the held ones `CANCELED` (`HcdIoParkedRelease`, the two
  `held URBs released` lines), `hcdDropAll` takes the PDOs off the bus, and the driver's own DMA memory is kept
  for the boot instead (section 4.13.8).

What stays code reading and is not a log line: that X6's second ABORT_PIPE sits in the thread's queue
(`HcdCfgService` serves nothing once `ControllerFailed`, and the pinned drain returns before the flush). The
qemu flavour prints no line for a kept request; the counters above are its own statement of the five.

### The fix: none that is independent of staging; 49.25 is task 49.16's

Asked of the code and reviewed (a second reading, kept in the same folder):

- **Completing the cancelled requests while the controller is contained and pinned is not sound with staging
  0.** The client did ask for the cancel (its `IoCancelIrp`, and an ABORT_PIPE for every transfer of a pipe),
  but that changes who waits, not where the controller may still write or read: the kept TDs name the client's
  own pages, a completed request hands those pages back, and a lower driver has no way to keep a client's
  allocation alive past its completion (an MDL of this driver's on the pages does not own them). So the request
  is the hold on the buffer, as record 17 section 4.13.7 already says ("a client's buffer cannot be kept once
  its request completes"). Rewriting the kept TDs (No Op, the cycle bit, a pointer to a page of this driver's)
  proves nothing on a controller that cannot be read: it may have fetched the TRBs already, and the driver's own
  rule is a completed Stop Endpoint before a TD is touched (`xhci_xfer.c`, the cancel's ownership rule), which is
  the command that timed out. An OUT-only transfer is not an exception (a read of freed and reused memory is
  still a use of it).
- **What could be completed safely does not end the wait.** A request that names no client page (a queued
  ABORT_PIPE; a control request with no data; a TD never published) can go: `hcdCfgHoldUnreadable`
  (`hcd_cfg.c`) already completes an abort that arrives after the containment, and one queued between the
  controller's failure and the containment is left in the thread's queue for ever (X5 and X6's second abort).
  But `cdcnet`'s quiesce waits until none of its transfers is outstanding, and those are its bulk-IN reads and
  its interrupt-IN read, which only a proof or staging releases. A side finding for 49.16, not a fix of this
  fault; nothing was coded.
- **A proof for a tunnel under the firmware connection manager** (its device-gone event taken as section
  4.13.6's statement) is excluded by 4.13.6 and 4.13.7 (it reads nothing back) and is a question of what counts
  as proof, the owner's, not a narrow fix.
- **Staging on for a controller behind a tunnel port only** is 49.16 with a narrower scope (the choice is made
  at the start, before the first transfer), not a fix apart from the staging default.

So no fix branch was made (`w49-xpsdfix` does not exist) and nothing was built: **49.25's fix is 49.16's**, and
X8 and X7b are its first evidence on XP (the dock unplugged with no stop, a busy network function on it, staging
1 on the dock's controller: removed, XP restarts; with the released 3.4.3.0 binary in X7b and this tree's qemu
flavour in X8).

**usb-wdm-free's `CDCNET.SYS` needs no change.** Read in its source at `6a266c7` and in its trace in seven runs:
after its 2 s it cancels its request and waits for the completion (`cdcusb.c`, `Sync`), its stop joins its
worker and waits until no transfer is outstanding before it frees its buffers and context (`CdcUsbStop`,
`Quiesce`), and its halt frees the adapter after that. A wait with a bound would have to return from the halt
with requests still below it, their memory and the image's code freed under the lower driver. With a lower
driver that completes what it is given (X7b, X8) the halt returns inside 7 s. The 3.4.3.0 package's own text for the fault until 49.16 lands is the README's driver
issue, widened to name a dock's built-in network adapter (the wording is in the report).

## 49.26 The STOP 0x8E in ACPI.sys on XP during the installer's update of the NHI: reproduced, and read (2026-11-11, worker xpsd, day wave)

Worker net's D5 order ("49.6", fault 2) on fresh overlays of `E:\xhci98-vm\p49\xpsd\xpa\xpa.qcow2` (XP SP3, this
tree's package at 3.4.3.0 with `/thunderbolt /tbnet`), the emulator `dock-20261110-bar0`, `-Usb4 tb3ar`, no dock:
a qemu flavour of `xhcicm.sys` copied over the installed one (`QCM.BAT`), XP restarted, then
`INSTALL.BAT /console /yes /nopause /thunderbolt /tbnet /wdmfree:stage=net/cdcnet` (`kit2\y.sh`, `kit2\rs.sh`). In
every run the installer says "xhcicm.sys: same version, other bytes: replaced, loaded at next start" and later
"Updating xHCI98 TB3 Controller (Intel Alpine Ridge) ...", which rebuilds the NHI's driver stack: the log's
`cm.stop=00000002` (the NHI's IRP_MN_REMOVE_DEVICE, noted before the request is passed down, `cm_nhi.c`,
`CmNhiPnp`) and, 4.4 s later in a good run, `cm.start=00000001` (noted after the start has come back from below).
The fault, when it comes, comes after that `cm.stop` and before any `cm.start`.

### The runs

| Series | `xhcicm.sys` (qemu flavour) | vCPUs | XHCIMARK | Faults |
|---|---|---|---|---|
| D4, D5 (worker net, night) | net's, sha256 `5532d65b...97ca19f` | 2 | running | 1 of 2 |
| U1..U5 (the stopped worker) | net's | 1 | running | 0 of 5 |
| V1..V5 (the stopped worker) | the probes' (`c8e594631`), `79980c1b...bdec38a7` | 1 | running | 0 of 5 |
| X1, X2, X3, X4b, X5 (the 49.25 legs' installer step) | the probes' | 2 | running | 0 of 5 |
| Y1..Y4 | net's | 2 | running | **2 of 4** (Y3, Y4) |
| S1, R1, S2, R2 (interleaved; S = `net stop xhcimark`, read back STOPPED, just before `INSTALL.BAT`; R = unchanged) | net's | 2 | S stopped, R running | S: 0 of 2; R: **1 of 2** (R2) |

With XHCIMARK running and net's flavour on two vCPUs: **4 of 8** (D5, Y3, Y4, R2). One vCPU: 0 of 10. The probes'
flavour, which writes four port lines per PnP request, on two vCPUs: 0 of 5 (its timing differs; kept apart). With
XHCIMARK stopped: 0 of 2, too few to conclude anything by themselves (the plan's third pair was not run: the
series stops at a fault so that the guest can be read). Every "no fault" run of the Y, S and R series had the
update line, one `cm.stop`, a second `cm.start` and "Exit code 3". Host A carried seven other workers' guests
throughout.

### The fault, the same four times

"*** STOP: 0x0000008E (0xC0000005,0xF7806BAE,<trap frame>,0x00000000)", "*** ACPI.sys - Address F7806BAE base at
F77E8000, DateStamp 480252b1"; trap frames `F729A980` (D5), `F6D22980` (Y3), `F6D32980` (Y4), `F6A5FA68` (R2)
(`cells\Y3\fault.png`, `cells\Y4\fault.png`, `cells\R2\fault.png`).

Read from the stopped guests over QMP as **data only**: the trap frame's register words, the stack words above
it, the loaded-image list (names, bases, sizes), two fields of the request and three of the device object named
on the stack, and the current threads' process names. No byte of any image's code or headers was read, no dump
file was opened, and offsets inside Microsoft's images are recorded, not interpreted (`kit2\mods.py`,
`kit2\fault.py`; `cells\R2\fault-read.txt`, `images.txt`, `stackwords.txt`; `cells\Y4\trapframe-words.txt`,
`stackwords-*.txt`, `images.txt`; `cells\Y3\trapframe-words.txt`, `stackwords-at-trap.txt`).

| Read | Y4 | R2 |
|---|---|---|
| the faulting processor | `CR2=00000004`; trap frame: EAX, ECX, EDX, EDI 0, EBX `C00000BB`, error code 0 (a read of a page not present): a read through a null pointer at offset 4 (Y3 the same) | the same |
| its thread | process `services.exe`, entered by a system call (the chain ends at a user-mode return address, `7C90E4F4`) | the same |
| the other processor's thread | process `System` | the same |
| the frame chain, by image | `ACPI.sys+1EBAE` (the fault) <- `ACPI.sys+25E3A` <- `ACPI.sys+1104B` <- `ntoskrnl.exe+A8EF` <- `pci.sys+1382` <- `ntoskrnl.exe+A3C9` <- `ACPI.sys+1FF38` <- `ACPI.sys+6E12` <- `ntoskrnl.exe+A3C9` <- `pci.sys` (four frames) <- `ntoskrnl.exe+A3C9` <- `ntoskrnl.exe+C9EE4` <- `+EA110` <- `+FA57A` <- `+BFCF4` <- `+698F` <- user mode | the same down to `ACPI.sys+6E12` <- `ntoskrnl.exe+A3C9`, then `ntoskrnl.exe+C9EE4` <- `+EA110` <- `+FA57A` <- `+BFCF4` <- `+698F` <- user mode (no `pci.sys` frames above ACPI's) |
| addresses inside `xhcicm.sys` (`F75B4000`, `73000h`), `xhci98.sys`, `xhcipfw.sys` or `xhcihlpr.sys` anywhere on that stack | none | none |
| the request ACPI.sys was handling | not read | IRP `863D8AC8`: major `1B` (IRP_MJ_PNP), minor `09` (**IRP_MN_QUERY_CAPABILITIES**) |
| the device object it was sent to | not read | `86678030`: its driver object's image is `ACPI.sys`; `AttachedDevice` 0 (nothing above it: the top of its stack); **`DeviceExtension` 0**; the object below it on the stack is `pci.sys`'s |

Reading (R2's lines; Y3 and Y4 fit them). A user-mode read of a device property, served in `services.exe` (the
Plug and Play service), made Windows send IRP_MN_QUERY_CAPABILITIES to the top of a device stack that at that
moment was a `pci.sys` device object with ACPI.sys's object on it and nothing above: a device whose function
driver had just been removed and not yet added again, which in this order is the NHI between its REMOVE
(`cm.stop=00000002`) and the rebuild of its stack. ACPI.sys's object there had no device extension any more, and
ACPI.sys, completing the request, read through that null pointer. On the other processor a `System` thread was
running (Windows' own removal or rebuild work, by inference). **No code of this project's is on the faulting
stack**, and the request is not one a driver of this project sent. That it is a race inside Windows between a
property reader and a device's removal is an inference (ACPI.sys was not read, and an earlier action of this
project's drivers on that device is not excluded by a stack); what is shown is the request, its sender's process,
its target's state, and the absence of this project's drivers from the stack.

### Who reads device properties once a second: XHCIMARK (T2)

The probes' flavour on one vCPU, no dock (`kit2\t2.sh`, QMP 49631): the PnP requests reaching `xhcicm.sys`'s
devices, counted over 60 s spans delimited by the debug console's size at each step.

| Span | `xhcimark` | PnP requests to the driver's devices in 60 s |
|---|---|---|
| A | as started | 840 to the bridge filters, 588 to the child filters, 42 to the NHI: all IRP_MN_QUERY_CAPABILITIES (24.5 a second) |
| B | `net stop xhcimark` ("1 STOPPED") | **none of any kind** (the debug console did not grow by a byte) |
| C | `net start xhcimark` ("4 RUNNING") | 880, 616 and 44, IRP_MN_QUERY_CAPABILITIES again |

So the steady capability queries noted in "49.6 (fault 1)" ("its sender is not read") are XHCIMARK's poll. About
once a second its walk (`xhcimark.c`, `mk_walk`) reads `CM_DRP_BUSNUMBER` and `CM_DRP_ADDRESS` of the devices it
passes, and on XP Windows answers such a read by asking the device's stack (record 21 section 7.7.2 already says
so of `DevicePropertyAddress`). The NHI gets one about every 1.4 s, so the 4.4 s in which its stack is rebuilt is
crossed by about three of them in every installer run. That this poll is the reader in the four faults is not
shown directly (the Plug and Play service serves every caller, and the installer reads devices too); it is the
reader that is always there, and the two runs without it did not fault.

### What this leaves

- 49.26 is not shown to be a fault of `xhcicm.sys`'s stop path. Whether it depends on the driver's flavour is not
  read: the release flavour was not run here, and the two qemu variants gave different counts; the service and the
  installer are the same in a release package. A release-flavour reproduction stays owed.
  (Sentence reworded by the coordinator at the fold, 2026-11-11.)
- Not built (a change to XHCIMARK's or the installer's behaviour, design first; the options are in
  `QUESTIONS.md`, 6): the walk reading a device's bus number and address once per device instead of every poll,
  and only for a device Windows reports started; the installer holding the service's poll off from before its
  first device update to after its last.
- Owed: a series long enough to tell (the same order with the service stopped, and with a changed service), the
  release flavour, 2000, and a real two-processor XP or 2000 machine.

Evidence: `E:\xhci98-vm\p49\xpsd\cells\Y1..Y4, S1, S2, R1, R2, T2\`, the three faulting guests' disks kept
(`cells\Y3\A`, `Y4\A`, `R2\A`, each with XP's own dump, not opened); `E:\xhci98-scratch\w49\xpsd\YY.log`,
`YY2.log`, `RS.log`, `T2.log`, `T2-debugcon.log`, `T2-serial.log`, `Y3-debugcon.log`, `Y4-debugcon.log`,
`R2-debugcon.log`; the plan as reviewed and the code reading asked for before the readings are in the same folder.txt`.

## 49.16 A dock unplugged with an input device on its own USB controller: the staged path audited, the lists before a change of default, and one XP leg (2026-11-11, worker s16, day wave)

The local, analysis-and-design part of 49.16. No source change and no default change. Branch `w49-s16` from `3.5.0.0` at `a4436a9b2`; commit `08bed2652` (design record 17, Revision 17 and section 4.13.9; documentation only). `scripts\check-text.ps1` passed; no gated build (a docs-only change).

### What was read

Design record 17 section 4.13 whole, record 13 section 11.11, `lessons.md` (the removed PDO on 98 SE), `runs/run-48.md` "48.33" sections, `runs/run-49.md` "49.5" and "49.6 (fault 1)", `runs/run-45.md` "45.35" and "45.49", worker xpsd's sheet for 49.25 (legs X4b to X8) with its logs, and the code at `a4436a9b2`: `hcd_ctl.c` (the containment, its proof, the start's values, `hcdStopBody`, `hcdRelease`), `hcd_enum.c` (`HcdEnumContain`, `hcdDropAll`, `hcdDrainAll`, the unproven invalidation, `HcdEnumService`'s halted tests, `HcdEnumDetach`), `hcd_io.c` (the cancel routines, `HcdIoDeferred`, `hcdDrainOne`, `HcdIoDeviceDrain`, the held requests, `HcdIoParkCloseAll`, `HcdIoIsoRelease`), `hcd_cfg.c` (the slow queue, `hcdCfgHoldUnreadable`, `HcdCfgFlushDevice`, `HcdCfgService`), `hcd_urb.c` (`hcdSubmitUrb`, `hcdUnreadable`, the idle request), `hcd_pdo.c` (`hcdPdoQuiesce`, `hcdDeletePdo`), `hcd_susp.c` (the idle rundown and the abandoned callback), `hcd_power.c`, `xhci_tol.c` (`XhciTolShutFlush`), `hcd_stage.c`, `hcd_dma.c` (the ledger), `hcd_svc.c`, `xhci_init.c` (`xhciTryClearBusMaster`), `xhci_stage.c`, `hcd_hub.c`, `hcd_hubfdo.c`, `hcd_rh.c`, `hcd_pnp.c`, and `src\cm\cm_filt.c` and `cm_iso.c` for the isolation channel (read only).

### Findings (record 17 section 4.13.9 has them in full)

- **What 1 changes on a controller closed off without proof**: five places (`HcdEnumContain` takes the drop; `HcdIoDeviceDrain` no longer returns early; `hcdPdoQuiesce` waits; `hcdStopBody` releases every held request; the quiesce's fallback no longer takes a Command register of `FFFFh` as proof). The safety argument holds in the paths read: the controller names only the driver's memory, all of it is kept while the pin stands, the pin is set before the drain, nothing is copied out of memory the controller may still write, the staging's processor work is joined before anything is freed, the ledger is the image's. No DMA-lifetime or double-completion defect was found in the staged path; **no source change is proposed**.
- **One liveness risk, not observed and not resolved.** On a contained controller every new request but an ABORT_PIPE is held on its PDO, at 0 and at 1 (`hcdSubmitUrb` tests `Tol.Unreadable` before the departed branch). A client that sends a request from its own removal handling and waits for it without a bound, before passing its REMOVE down, stalls the removal even with staging. On NT the client hears SURPRISE_REMOVAL first (X8; 45.35's legs). 98 SE and ME have none, and ME's `usbaudio.sys` sends a select before its REMOVE (record 13 section 18.2): on a contained controller that select is held and `HcdIoIsoRelease` is never reached. This is section 4.6's hold, not the staged path's code; it decides whether staging alone makes the unplug safe on 98 SE and ME, and no leg has read it.
- **With 0, a request that names no client page is also stranded**: an ABORT_PIPE, RESET_PIPE or select queued between the controller's failure and the containment stays in the thread's queue for good (the pinned drain returns before the flush). Worker xpsd's side finding, confirmed in the code; fixing it alone would not end the removal's wait.
- **A staging 0/1 difference outside the containment**: a stop that comes before any containment (a removal with nothing pending) whose fallback reads `FFFFh` frees everything at 0, the evidence section 4.13.7 rejects, and keeps the controller's DMA memory at 1. So with 1 each surprise removal of such a controller keeps some 2.5 MiB for the boot (an estimate from the constants: the common buffer, the event ring's block, the scratch, the 2048 KiB pool and the control slab, before any pipe's) against the image's 64 MiB ledger. No leg has read the bytes kept, the refusal at the cap or the ledger carried over; today's notes cannot give the totals.
- **Texts that describe the 0 route alone** (behaviour unaffected): the USB Tree's "PINNED: no proof DMA stopped; buffer, transfers kept" (`xhcitr/tr_snap.c`) at either value; the comment above `hcdContain`; the comment above `HcdIoIsoRelease`.
- **Not built**: record 13 section 11.11.9's fault points (a request held in each staging state at the stop) and its per-request completion log; `XhciQemuStageFault` bit 0 acts at the stop only. Every unproven stop read so far met requests that happened to be published on a ring (7 x86 and 7 x64 bulk writes; XP's network function).
- **The INFs' rule**: both write `XhciDmaStaging` 0 with don't-overwrite, so a new INF default would reach a fresh install alone.
- **49.25's X6 and X8** support the mechanism on XP for one dock model and one client; they are not a case for the default (no replug, one system, the emulator).

### The lists and the choices

Section 4.13.9 carries three acceptance lists (the unproven stop itself; the dock legs with 98 SE's and ME's address reading of task 49.15; what a default for every controller needs beyond them: 49.5's left items, the speed on a real NT machine, what 1 refuses that 0 serves, the systems no leg at 1 has run) and three intermediate choices: (A) the setting per controller as today, (B) staging forced on behind a tunnel port, (C) an id list, refused. Recommendation for the owner: keep 0 in `3.5.0.0`, (A) until list 1 is read, then (B) system by system. Questions for the owner: `E:\xhci98-scratch\w49\s16\QUESTIONS.md`.

### Review

The outline and the findings were reviewed before the section was written (nine points, among them the liveness risk above and the shutdown flush's rule), the section twice more (eight points, then nothing material). Reviewed and revised.

A second commit, `6febf0eb2` (documentation only), names the command stranded at 0 in the lists at the coordinator's request: list 1 item 2 gains an ABORT_PIPE sent while the controller is failed and not yet contained, and a paragraph folds the question of completing it at 0 into the task as the owner's, with its own pass criterion. Reviewed and revised (one point, then nothing material).

### The optional XP leg: attempted once, void, not read

One 1-vCPU XP guest (`E:\xhci98-vm\p49\s16\cells\S0`, QMP 48640, emulator `dock-20261110-bar0`, `-Usb4 tb3ar`, a copy of the 49.25 kit's XP disk and guest files; scripts `E:\xhci98-scratch\w49\s16\kit\s.sh`, `d2.sh`, `lib.sh`), the generic TB3 dock (`dialect=tb3,dock=on,drive=nvme0`: an NVMe, a NIC and a `qemu-xhci` with a USB tablet), meant to read the unplug with an input device on the dock's controller at 0 (cell S0) and at 1 with a replug (cell S1). Host at 44 to 55 % processor with 14 QEMU processes when it started.

Read: the guest started, the qemu flavour of `xhci98.sys` (`85a7217b...f771b6a`, the 49.25 kit's) was copied over the two installed copies (`regdone`), the dock was plugged at 14:23:34 and Stor98's wizard for the dock's NVMe finished. Then the kit's wizard keystrokes (written for the HP dock model's order of wizards) lost their place at the first wizard, which was already at its Finish page: later keys opened Outlook Express and its Internet Connection Wizard in front of the next Found New Hardware wizard ("PCI to PCI Bridge"), and the dock's own controller was never installed (two `XhciDmaStaging, effective=00000000` lines only, the board's two controllers). Stopped by the worker at 14:33 (QMP quit; `pids.txt`). **Void: nothing about the unplug was read at either value.** Not run again: walking this dock's wizards needs a step-by-step run by screenshots, at one vCPU, twice; left as section 4.13.9 list 2 item 6's XP row. S1 was not started. Evidence: `cells\S0\` (`leg.txt`, `w0.png` to `w6.png`, `A\serial-A.log`, `A\debugcon-A.log`), `E:\xhci98-scratch\w49\s16\S0.log`.

QEMU processes of this worker (`E:\xhci98-vm\p49\s16\pids.txt`): 102612 (started 14:12, stopped by QMP quit 14:12 to relaunch the script detached), 108680 (14:13 to 14:33, QMP quit). None left running.

## 49.28 (first form, superseded the same day) The installer's log: the temp folder when the root file cannot be written (2026-11-11, worker log)

Worker log, branch `w49-log` from `3.5.0.0` at `980245c14`, commits
`9fb350735` (the change) and `25454018f` ("Log: none" for a run whose log
failed). Owner's decision of 2026-11-11; design record 23 revision 56,
sections 7.5 and 7.5.1, decision D8 (reviewed and revised).

### What changed

- `XHCIINST.LOG` stays at the root of the system drive. A run given no
  `/log` whose root file cannot be started (it does not open for
  appending, or its first line does not go in) uses `XHCIINST.LOG` in the
  temp folder (`GetTempPathA`); when both fail it says so and runs with no
  log. A `/log` path that cannot be written is said and nothing else is
  tried, as before.
- The engine keeps the path it took; the console's `Log:` and `Exit code
  N. The log is` lines, the window's `Log file:` line (Welcome and the
  uninstall's first page) and its Finish page take it from there. The
  window's one-line control is `SS_NOPREFIX` and shows a path wider than
  itself shortened in the middle, the file name kept.
- Each file names the other in a log-only line: the temp file `Cannot
  write the log to <root> (<reason>). Another log may be there.`, the root
  file `Another log may be at <temp path>.` when a file of that name is in
  the run's temp folder.
- Pure parts in `inst_core.c` (`inst_log_temp_path`, `inst_log_pick`,
  `inst_log_fail_text`) and `wiz_lay.c` (`wiz_lay_path_mid`), with host
  checks in `test\test_inst.c` and `test\test_wizlay.c`.

### Builds and host checks

- Host suites built as `test\run-host-tests.cmd` builds them:
  `test_inst` 9126 checks, 0 failures; `test_wizlay` 208 checks, 0
  failures. `xhciinst\build.cmd`, `xhcitr\build.cmd` and
  `xhcimark\build.cmd` exit 0. `scripts\check-text.ps1` and
  `scripts\check-readme-text.ps1` OK.
- Gated builds with `XHCI98_SELFTEST_SKIP_OK=1`, each `BUILD + GATES
  PASSED (release)`: `9fb350735` x86 and amd64
  (`E:\xhci98-scratch\w49\log\build1\build-release.log`,
  `build-release-amd64.log`), `25454018f` x86 and amd64
  (`E:\xhci98-scratch\w49\log\build-release.log`,
  `build-release-amd64.log`).
- Kits (family layout of `out\pkg-release-x86` plus the tree's tools):
  of `9fb350735` `XHCIINST.EXE` `cbfbd6d9...`, `XHCIWIZ.EXE` `bc4acb67...`
  (`scratch\kit1\kit-sums.txt`); of `25454018f` `XHCIINST.EXE`
  `bf37d5ed...`, `XHCIWIZ.EXE` `4a31d9a3...` (`scratch\kit\kit-sums.txt`).

### The legs

Development host A, QEMU `asm4242-20261110-docks`, TCG, q35, one vCPU a
guest, `qemu-xhci` behind a root port, fresh overlays of the q35 bases in
`E:\xhci98-vm\p49\log\` (`l98` 98 SE, `lxp` XP SP3, `lw2k` 2000 SP4).
Console text captured over COM1 (`serial-<tag>.log`), window pages by
screenshot. `<temp>` is `C:\WINDOWS\TEMP` on 98 SE and
`C:\DOCUME~1\<user>\LOCALS~1\Temp` on XP and 2000. Tag a/b: the kit of
`9fb350735`; tag c and the 2000 guest: the kit of `25454018f`.

| Step | 98 SE | XP | 2000 (kit 2 only) |
|---|---|---|---|
| First install, `/yes` (root writable) | pass: `Log: C:\XHCIINST.LOG`, `Exit code 3. The log is C:\XHCIINST.LOG.` (`l98\a-c1.png`; the log in `serial-b.log`, "LG c1") | pass, same texts, exit 3 | pass, exit 3 |
| Cold restart, root file read-only, run | pass: first line `Cannot write the log to C:\XHCIINST.LOG. Using C:\WINDOWS\TEMP\XHCIINST.LOG.`; `Log:` and the exit line name the temp path; the temp file holds the run and the log-only line `(open: errno 13, error 5)` | pass; the first text wraps after `Using`, the path on the next line, two spaces in | pass, as XP |
| A directory named `XHCIINST.LOG` at the root, run | pass: same texts, appended to the same temp file | pass | not run |
| Root restored, run | pass: root path shown; the root file gains the run with `Another log may be at C:\WINDOWS\TEMP\XHCIINST.LOG.` | pass | pass |
| `/list`; `/list /log C:\LISTLOG.TXT`; `/list /log C:\NOSUCH\X.LOG` | pass: no log and the long `Log: none (...)` line; the named file written; `Cannot write the log to C:\NOSUCH\X.LOG. Continuing without a log.`, no fallback, `The log is (none)` | pass | not run |
| The same on kit 2 after a restart | pass: the failed `/log` run now prints `Log: none` alone | pass | - |
| Kit 2: read-only root run, then a normal run | pass: the fallback run appended to the temp file (its earlier runs still there), the normal run to the root file with its pointer line | pass (the fallback run was a real update, exit 3) | (rows above) |
| Window, root read-only: Welcome | pass: `Log file: C:\WINDOWS\TEMP\XHCIINST.LOG` (`l98\b-wiz1.png`) | pass: `Log file: C:\DOCUME~1\test\LOCALS~1\Temp\XHCIINST.LOG`, whole on one line (`lxp\b-wiz1.png`) | not run |
| Window, root read-only: Finish page | pass: `Exit code 4. The log is C:\WINDOWS\TEMP\XHCIINST.LOG.` (`l98\b-wiz7.png`) | pass: `Exit code 0. The log is <temp>\XHCIINST.LOG.` (`lxp\b-wiz6.png`) | not run |
| Window uninstall to the end, root read-only (kit 2) | pass, started from `C:\XHCI98` so it hands over to its temp copy: first page `Log file:` the temp path and the box's `Cannot write the log to ... Using ...` (`l98\c-un1.png`); Finish `Exit code 3. The log is C:\WINDOWS\TEMP\XHCIINST.LOG.` (`c-un4.png`); the temp file holds both processes (13:49:47.100 `C:\XHCI98\XHCIWIZ.EXE /uninstall`, exit 0; 13:49:47.540 `C:\WINDOWS\TEMP\XHCI98UN\XHCIWIZ.EXE /uninstall /from C:\XHCI98`, exit 3); the root file unchanged (107,642 bytes) | pass, started from the package (one process): `lxp\c-un1.png`, `c-un5.png` `Exit code 3. The log is <temp>\XHCIINST.LOG.`; root file unchanged (24,269 bytes) | not run |
| Limited user (`runas`, a user in Users only; root file writable by its attribute, not by its ACL) | - | pass: fallback to that user's temp folder, then `Cannot continue: administrator rights are needed (error 5).`, exit 1, the log written there (`serial-b.log`, "LIM") | not run |

No history lost: every run made appears once, by its header's time, in
one of the two files. 98 SE: root 12:55:39, 13:15:30, 13:44:25; temp
13:10:51, 13:13:56, 13:20:59 (window), 13:23:38 (window, cancelled),
13:42:25, 13:49:47.100 and 13:49:47.540 (the uninstall's two processes).
XP: root 13:01:17, 13:16:53, 13:42:06; temp 13:13:32, 13:15:27, 13:24:40
(window), 13:39:18, 13:45:22 (window uninstall); the limited user's run
in that user's own temp folder. 2000: root 13:55:25, 13:57:40; temp
13:56:38.

No console line of any captured run is past 79 columns (checked over
every `CONSOLE` and `LIST` block of the serial captures).

### Found in the legs

- A run whose log failed printed `Log: none (/list and /devices write
  none unless /log names one)`, also for a `/log` that failed. Fixed in
  `25454018f`: such a run prints `Log: none`; read on both guests.
- On XP a second package build of the same version is an update
  ("Replace files (same version)") and Windows asks about the unsigned
  drivers again; answered in the guest. Not this change's.

### Not read

A full system drive (the first-line test), both places failing at once
and a path too long for the window's line are checked in the host suites
and the window's self-test only. No x64 guest: the amd64 gated build
compiles the same sources. ME not run.

Evidence: `E:\xhci98-vm\p49\log\{l98,lxp,lw2k}\` (serial captures,
screenshots, command lines), `pids.txt` there.

## 49.28 The installer's log: the temp folder fallback on 98 SE and ME only (2026-11-11, worker log2)

Worker log2 (resumed worker log), branch `w49-log`, commits `160bcca65`
(record 23 revision 57) and `375436e06` (the code) on `25454018f`. The
owner's decision of 2026-11-11 after the merge review of `w49-log`: no
temp folder fallback on 2000 to 10 (the temp folder of an NT process can
belong to a less trusted account); 98 SE and ME keep it. Design reviewed
and revised before the code.

### What changed

- The engine reads the version before it starts the log; only where it
  says `VER_PLATFORM_WIN32_WINDOWS` (98 SE, ME) is the temp folder tried.
  On 2000 to 10 a root file that cannot be started gives `Cannot write the
  log to C:\XHCIINST.LOG. Continuing without a log.`, `Log: none`, `Exit
  code N. The log is (none).` and the window's `Log file: none`;
  `GetTempPathA` is not called and no pointer line is written.
- `inst_log_pick` takes the system (`nt`); `test_inst` has the NT cases
  (root taken, root failing with a temp path passed, no temp path, `/log`
  taken, `/log` failing). The usage line and `README.TXT` say the fallback
  is on 98 SE and ME.

### Builds and host checks

- `test_inst` 9131 checks, 0 failures; `test_wizlay` 208, 0.
  `xhciinst`, `xhcitr`, `xhcimark` `build.cmd` exit 0; `check-text.ps1`,
  `check-readme-text.ps1` OK.
- Gated builds of `375436e06`, `XHCI98_SELFTEST_SKIP_OK=1`: x86 release
  14:20-14:34, amd64 release 14:36-14:48, both `BUILD + GATES PASSED
  (release)` (`E:\xhci98-scratch\w49\log\build-release.log`,
  `build-release-amd64.log`; the earlier pair's logs in `build2\`).
- Kit: x86 family layout of `375436e06` plus the tree's tools,
  `XHCIINST.EXE` `2e9d3976...`, `XHCIWIZ.EXE` `86e81078...`
  (`scratch\kit\kit-sums.txt`).

### The legs

Development host A, QEMU `asm4242-20261110-docks`, TCG, q35, one vCPU a
guest, fresh overlays in `E:\xhci98-vm\p49\log\` (`n98` 98 SE, `nxp` XP
SP3, `nw2k` 2000 SP4). Console over COM1 (`serial-<tag>.log`), window by
screenshot. "No other file" = `dir /s /a C:\XHCIINST.LOG` over the whole
of `C:` (every profile's temp folder and `C:\WINDOWS\TEMP` included) lists
only the root entry.

| Step | XP | 2000 | 98 SE |
|---|---|---|---|
| First install, `/yes` | pass: `Log: C:\XHCIINST.LOG`, exit 3; no other file | pass, exit 3; no other file | pass, exit 3 |
| Root file read-only, run | pass: `Cannot write the log to C:\XHCIINST.LOG. Continuing without a log.`, `Log: none`, `Exit code 0. The log is (none).`; root unchanged (9,663 bytes); no other file | pass, same texts; root unchanged (9,182); no other file | (after a cold restart) pass: `Cannot write the log to C:\XHCIINST.LOG. Using C:\WINDOWS\TEMP\XHCIINST.LOG.`, temp path in `Log:` and the exit line; the temp file's log-only line `... (open: errno 13, error 5). Another log may be there.` |
| A folder named `XHCIINST.LOG` at the root, run | pass, same texts; only the folder | pass, same | not run (read on the previous revision) |
| Window, the folder in place (XP, 2000) / root read-only (98) | pass: Welcome `Log file: none` (`nxp\a-wiz1.png`), Finish `Exit code 0. The log is (none).` (`a-wiz4.png`); no file anywhere | pass: `nw2k\a-wiz1.png`, `a-wiz2.png` | pass: Welcome `Log file: C:\WINDOWS\TEMP\XHCIINST.LOG` (`n98\b-wiz1.png`), Finish `Exit code 4. The log is C:\WINDOWS\TEMP\XHCIINST.LOG.` (`b-wiz6.png`) |
| Root restored, run | pass: root path shown, appended to the root file, no pointer line | pass, same | pass: root file gains the run with `Another log may be at C:\WINDOWS\TEMP\XHCIINST.LOG.` |
| Limited user (`runas`, Users only) | pass: `Cannot write the log to C:\XHCIINST.LOG. Continuing without a log.`, `Log: none`, `Cannot continue: administrator rights are needed (error 5).`, exit 1; no `XHCIINST.LOG` in that user's temp folder or anywhere but the root | - | - |
| Window uninstall to the end, root read-only, from `C:\XHCI98` | - | - | pass: first page `Log file:` the temp path (`n98\b-un1.png`); Finish `Exit code 3. The log is C:\WINDOWS\TEMP\XHCIINST.LOG.` (`b-un5.png`); the temp file holds both processes (14:57:50.810 `C:\XHCI98\XHCIWIZ.EXE /uninstall` exit 0; 14:57:50.970 `C:\WINDOWS\TEMP\XHCI98UN\XHCIWIZ.EXE /uninstall /from C:\XHCI98` exit 3); root unchanged (78,410 bytes) |

Run headers: XP root 14:38:33, 14:54:09 (the four runs between them,
console and window, wrote no log anywhere); 2000 root 15:03:57, 15:11:30;
98 SE root 14:37:18, 14:44:56, temp 14:43:16, 14:47:06 (window),
14:57:50.810 and .970 (uninstall). Longest console line of every captured
run: 79 (98 SE), 77 (XP), 76 (2000).

On XP the first install asked about the unsigned drivers and opened
Windows' own wizard for the root hub; answered in the guest (not this
change's). 2000's base opens a Found New Hardware wizard at start;
cancelled.

### Not read

ME; x64 guests (the amd64 gated build compiles the same source); a full
system drive and both places failing (host suite only).

Evidence: `E:\xhci98-vm\p49\log\{n98,nxp,nw2k}\` (serial captures,
screenshots, `g.log`), `pids.txt` there.

## 49.18 The SHPC slots read by the learning on Windows 2000 and later (2026-11-11, worker shpc)

Worker shpc, development host A, branch `w49-shpc` from `3.5.0.0` at `980245c14` (worktree
`E:\xhci98-wt\w49-shpc`). Scratch `E:\xhci98-scratch\w49\shpc\`, guests `E:\xhci98-vm\p49\shpc\`, QMP ports
48500 to 48502 (the coordinator moved the range from 49660-49679, which does not bind on host A).

### The decision, and the design

The owner's decision of 2026-11-11 (roadmap decisions table, "Phase 49, the owner's answers of 2026-11-11"):
the SHPC slot reading for the start learning is built, the eject's notice kept where the port stays unknown,
`XhciCmInferAtStart` off by default. Design: record 22, new section 7.6.5.

- What is read: nothing new in the pure half (`XhciTbtPortRead` already reads an SHPC member's slots through
  DWORD Select when its accessor has a writer). The learning's three calls (the arm, the attempt, the
  recovery's arm) get an accessor of their own: the seam's reader with `CmRegConfigWrite`
  (`IRP_MN_WRITE_CONFIG` of the select byte to the bridge's PDO through the filter) as its writer, only under
  S1 (the filter route: Windows 2000 and later), S2 (every member of the set of source SHPC) and S3 (each
  member the emulator's bridge by the stor98 channel's admission: QEMU's vendor id 1B36h on it or on the bus-0
  bridge above it, an SHPC capability, no PCI Express capability). Every other reader keeps the read-only
  accessor.
- The reading owed: an arm that leaves an empty member without its evidence bit (the filter not yet published,
  the slot still occupied just after a tunnel down) is read again at idle passes by a refresh that keeps the
  baseline (`XhciTbtLearnEvidence`), for 2 s after the arm and then once each time a member's filter record
  changes; never while a port is pending.
- No registry value, no user-visible text. One log line, `u4.learn.shpc` (bit 0 the writer given, bit 1 a
  member's evidence owed).
- The SHPC specification is not on the host: `docs/references/README.md` gained a paragraph saying where it is
  obtained; the tree cites section 4.2 only, as `tbt_topo.h` already did; offsets read again against Linux
  `shpchp_hpc.c` (facts) and QEMU's `hw/pci/shpc.c`.

### Review

- Design (a task on the draft): three findings - a failed idle retry (then an arm) cleared the armed mark and
  could cost the next gain, a far end's included (P2: the retry became a refresh that keeps the baseline); the
  publication stamp as a sum could miss a change (P2: per-member records, taken before the reading); "never a
  binding" overstated (P3). Round 2: "Nothing material found in the revised design."
- Code (working tree, before the commit): one P2 - the refresh was gated on the last logged value, so one
  failed admission read switched the retries off (fixed: the gates are S1, S2, nothing pending, something
  owed and the schedule; the records and the clock taken at every arm on an S1+S2 set; S3 read again at each
  refresh after the record is acknowledged). Round 2: "approve ... No material findings."
- The results of those readings are kept in the worker's scratch folder.

### Commits

- `8852ac967` record 22 section 7.6.5 (the design), `docs/references/README.md` (the SHPC paragraph), record 21
  (one sentence).
- `d53e848be` the code: `src/cm/tbt_topo.c` / `.h` (`XhciTbtShpcSet`, `XhciTbtLearnOwed`,
  `XhciTbtLearnEvidence`, `XhciTbtEvidenceDue`), `src/cm/cm_nhi.c` (`cmU4LearnIo`, `cmU4ArmRead`,
  `cmU4ShpcRefresh`, `cmU4ShpcNote`), `src/cm/cm_nvlinkos.c` / `cm_nvlink.h` (`CmNvlinkShpcBridge`),
  `src/cm/cm_reenum.c` / `.h` (`CmRegMemberRecords`), `src/cm/cm.h` (three words), `test/test_tbt_topo.c`
  (`test_shpc_owed`), record 22 section 7.6.5 "As coded".

### Host tests and builds

`test_tbt_topo` 1071 -> 1156 checks, 0 failures. Mutations of the refresh (the occupancy test dropped, the bus
test dropped, the port reading dropped, the record compare weakened) each fail vectors
(`E:\xhci98-scratch\w49\shpc\mut\`). One gated build of `d53e848be`, `XHCI98_SELFTEST_SKIP_OK=1`, a slot of
`E:\xhci98-locks` taken and released for each: `all` x86 "BUILD + GATES PASSED (release qemu)"
(`gated-x86.log`), `release -amd64` "BUILD + GATES PASSED (release)" (`gated-amd64.log`). Release x86
`xhcicm.sys` SHA-256 `3B5EF80B53376E4191C40FA0DA0122E1433393896D711863BD53B5A0F61CE969` (the released
`3.4.3.0` one: `9DBD715B...`).

### The legs: setup

Kit `E:\xhci98-vm\p49\shpc\xfer\KIT`: this build's `nt-x86` and `98-me` family folders
(`ConvertTo-FamilyLayout` of `out\pkg-release-x86`) with `releases\3.4.3.0`'s root (its `tools`, `SETUP.EXE`,
`INSTALL.BAT`; no tool changed and the tree's version is still 3.4.3.0). Install by `INSTALL.BAT /console /yes
/nopause /thunderbolt /stor98` (`xfer\RINT.BAT`), which ends with `fc /b` of the kit's `xhcicm.sys` against
`System32\drivers\xhcicm.sys`: "no differences encountered" in every guest. Readings: `XHCITR /report`'s log
lines and `INSTALL /console /devices` (`xfer\T\R.CMD`), the eject by `INSTALL /console /eject "id=<instance>"
/yes` (`xfer\T\E.CMD`; where the id line carried no `/yes` the question was answered Y), all to COM1
(`serial-<tag>.log`). Emulator `q35-bases\bin\asm4242-20261110-docks` (`4959a9ab...`), TCG, one vCPU a guest
(three guests at a time), `-trace usb4_*`. Overlays of the read-only q35 bases `xp`, `w2k` (Standard PC HAL),
`w7x86`.

Two vehicles:

- **i440FX with the SHPC bridge** (`tools\gopc.ps1`): `-machine pc`, `usb4-tunnel-bridge` `tb0` 00:05.0
  (`shpc-autoenable=on,msi=off`), `usb4-host profile=usb4-generic,host-model=intel-adl` 00:04.0, `qemu-xhci`
  00:06.0. The q35 bases start on it (2000 and XP from the machine's own PIIX IDE, 7 from an `ich9-ahci`).
  With the default `host-model=minimal` the host router names the bridge (`u4.named=01002802`), so nothing
  is learned there; the `intel-adl` model answers no mapping (`u4.map.none=FFFFFFF5`), which is the case the
  learning serves. Overlays `xpb`, `w2kb`, `w7b`.
- **q35 with the ASM4242 model** (`tools\goq.ps1` over `q35-bases\launch-q35.ps1 -Usb4 asm4242`): the
  switch's two tunnel ports as members (source 1, PCI Express ports), the firmware's tunnel at power-on with
  `-EncAtBoot`. Overlays `xpq`, `w2kq`, `w7q`.

The warm restart of the 2000 base hung once in real mode (EIP `0d4f`, the base's README names it): every 2000
restart was a shut down and a cold start.

### The legs: the i440FX SHPC bridge, software connection manager (`host-model=intel-adl`)

At every start: `cm.reenum.member=00002805` (00:05.0, source 5, SHPC), `cm.reenum.member.flags=00002803`,
`u4.map.none=FFFFFFF5`, **`u4.learn.shpc=00000001`** (the writer given, no member's evidence owed).

| Guest (tag) | Step | Result |
|---|---|---|
| XP (`xpb`, a1) | a plain enclosure (`usb4-device ... port=1,drive=nvme0`: the NVMe at 01:01.0) `device_add` at the desktop; Stor98 by the Found New Hardware wizard | `u4.pcie.up=00010809`, **`u4.learn=00000103`**; /devices: "its PCIe connection reaches the port at PCI 00:05.0", "Stor98 Storage Controller for NVMe SSDs - stor98, started" |
| | `INSTALL /console /eject 1`, Y | "Windows has stopped Stor98 Storage Controller for NVMe SSDs. / **The PCIe tunnel was closed.** / It is now safe to disconnect the device.", exit 0; `cm.reenum.confirm=00000028`, `cm.reenum.eject=00000028`, `u4.eject.bridge=00000028`, `u4.eject=00000100`; emulator: "PCIe tunnel down reason 1" (admin-down) |
| | `device_del`, `device_add` again | `u4.learn=00000102` (KNOWN: a binding is kept by bridge for the instance's life, record 22 section 7.6); eject again: "The PCIe tunnel was closed.", `u4.eject=00000100` |
| 2000 (`w2kb`, a1) | the same plug; Stor98 by the wizard (Yes to the signature question) | **`u4.learn=00000103`**, "reaches the port at PCI 00:05.0", E: mounted |
| | eject 1, Y | "**The PCIe tunnel was closed.**", exit 0; `u4.eject.bridge=00000028`, `u4.eject=00000100` |
| 7 x86 (`w7b`, a1) | the same plug, the first one after the install | Windows installed Stor98 by itself and **stopped and started the NHI** (`u4.carry.in` 0 -> 1, `u4.preserved=00010809`, `u4.preserved.own=00010003`): the new instance finds its predecessor's tunnel up, the port pending, `u4.learn.expired=00000001` |
| | eject 1, Y | "Windows has stopped ... / **The PCIe tunnel stays up until the device is unplugged: the Thunderbolt driver does not know which of its ports the device is on. It learns that when a device is plugged in while Windows runs.** / It is now safe to disconnect the device.", exit 0 (the notice kept) |
| | `device_del`, `device_add` again | `u4.pcie.up=00010809`, **`u4.learn=00000103`** (the arm after the tunnel down took the slot's evidence), "reaches the port at PCI 00:05.0" |
| | eject 1, Y | "**The PCIe tunnel was closed.**"; `cm.reenum.confirm=00000028`, `u4.eject.bridge=00000028`, `u4.eject=00000100` |
| XP, 2000, 7 (o1, a fresh start, nothing attached) | the ORICO model (`dialect=usb4,device-model=asm2464`) `device_add`; behind the one-bus SHPC bridge it presents its NVMe directly (the same instance id as the plain enclosure) | `u4.router.id=174C2463`, **`u4.learn=00000103`** on all three (7: `u4.carry.in=0`, no restart this time) |
| | `E.CMD o1 1` (`/eject "id=..." /yes`) | all three: "**The PCIe tunnel was closed.**", exit 0, `u4.eject=00000100` |
| XP, 2000, 7 (s1) | the ORICO model at power-on with `preos-cm=on` (the emulator's pre-boot connection manager builds the tunnel) | `u4.preserved=00010809`, `u4.learn.shpc=00000001`, `u4.learn.expired=00000001` (found up at start: no transition to read) |
| | `E.CMD s1 1` | all three: "**The PCIe tunnel stays up until the device is unplugged: ...**", exit 0. 2000: `cm.reenum.eject=00000028`, `u4.eject.bridge=00000028`, `u4.eject.unmapped=00000028`; XP: `cm.reenum.eject.stale=00000028`, `...stale.why=00010108` (a function still had a stack at the hand-off, task 43.70's check; the same notice) |
| XP, 2000 (b0, `host-model=minimal`) | start only | `u4.named=01002802` (the router names 00:05.0 for port 1), `u4.learn.shpc=00000001`: this model needs no learning |

Evidence: `E:\xhci98-vm\p49\shpc\{xpb,w2kb,w7b}\serial-*.log`, `usb4-trace-*.log`, `cmdline-*.txt`, the
screenshots beside them.

Not read in a guest: the refresh itself (`u4.learn.shpc=00000003` then `1`): the line never read `3`. At the
starts with nothing attached the plug that followed was learned, and so was each plug after a tunnel down, so
no arm was seen to leave a member owed; at the starts with the enclosure at power-on the slot was occupied,
which owes nothing. It is host vectors only.


### The legs: q35 with the ASM4242 model, software connection manager

Members `cm.reenum.member=00020001` and `00020801` (02:00.0 and 02:01.0, the switch's tunnel ports, PCI Express
ports), `u4.map.none=FFFFFFF5`; no `u4.learn.shpc` line on this set (S2 fails with no access). Evidence
`E:\xhci98-vm\p49\shpc\{xpq,w2kq,w7q}\serial-<tag>.log`.

| Guest (tag) | Step | Result |
|---|---|---|
| XP (`xpq` s2, g1), 2000 (`w2kq` s1), 7 x86 (`w7q` s1) | the ORICO model (its own switch) at power-on, the firmware's tunnel kept (`u4.preserved=00010808`, `u4.learn.expired=00000001`); `/eject "id=<its upstream port>" /yes` | all three exit 0, "Windows has stopped ... / **The PCIe tunnel stays up until the device is unplugged: the Thunderbolt driver does not know which of its ports the device is on. It learns that when a device is plugged in while Windows runs.** / It is now safe to disconnect the device." 2000: `cm.reenum.eject=00000200`, `u4.eject.bridge=00000200`, `u4.eject.unmapped=00000200`; XP and 7: `cm.reenum.eject.stale=00000200`, `...stale.why=00010108` (below) |
| XP, 2000, 7 (d1): a cold start with nothing attached | the TB3 dock model (`dialect=tb3,dock=on`, its NVMe drive) `device_add` at the desktop | `u4.pcie.up=00010809`, **`u4.learn=00000103`** on all three (a far end: learned as before this task), "its PCIe connection reaches the port at PCI 02:00.0". On 2000 the dock's three downstream bridges read problem code 12 (no window for them on that Windows); its upstream port started and removable |
| | `E.CMD d1 1` (`/eject "id=<the dock's upstream port>" /yes`) | all three exit 0, "Windows has stopped ... / **The PCIe tunnel was closed.** / It is now safe to disconnect the device."; `cm.reenum.confirm=00000200`, `cm.reenum.eject=00000200`, `u4.eject.bridge=00000200`, `u4.eject=00000100` |
| XP (c1): a cold start with nothing attached | the ORICO model hot-plugged, ejected | `u4.learn=00000103`, "**The PCIe tunnel was closed.**", `u4.eject=00000100` |
| 2000 (s1, h1): the ORICO at power-on ejected (notice), unplugged, plugged again | | `u4.learn=00000103`, "**The PCIe tunnel was closed.**" |

**Not this task's, and unchanged: the ACPI bus filter.** On XP and 7 (ACPI machines), the same re-plug after a
unit that was there at power-on is learned (`u4.learn=00000103`) and its eject ends "The Thunderbolt driver has
not confirmed that the PCIe tunnel was closed." with `cm.reenum.confirm`, `cm.reenum.eject.stale=00000200`,
`cm.reenum.eject.stale.why=00020208` (bit 3: a function behind the bridge reads as having a stack): the new PDO
comes with ACPI's bus filter under this driver's child (`cm.reenum.child.under=41435049` "ACPI", decision 2),
and that filter stays on the PDO after the eject. Record 21 section 7.4 item 1's open limitation, read before
in 45's legs (`runs/run-45.md`, "The eject on XP once listed"). Read here on XP with this build (`xpq` s2, tag
h1) and with the released `3.4.3.0` `xhcicm.sys` put in place by `xfer\SW.CMD REL` and a restart (`xpq` r1, tag
rh): the same text and the same `stale.why`; on 7 with this build only (`w7q` s1, tags h1 and h2). On a cold
start with nothing attached ACPI's filter is not on the new PDOs (decision 1) and the eject closes the tunnel
(the d1 and c1 rows). On 2000 (Standard PC HAL) it never is. The at-power-on eject's `stale.why=00010108` on XP
and 7 is the same check; the notice is shown either way, because the port is not known.

### The "eject that did not return" on XP: the leg's own script

The first worker's XP eject at 10:14 (`xpq`, tag s1) did not return in 8 minutes and the connection manager
logged no request. Cause: the leg script, not the driver, the base, the kit or the tool. `/devices` prints the
script form on three lines when the instance id is long (`INSTALL /console /eject` / `"id=..."` / `/yes`); the
script of that hour took the `id=` line alone, so the tool was run without `/yes`, with its output redirected
to a file, and it waited at its question for the keyboard:

- The output file of that run is still in the guest (`C:\T\Es1.TXT`, 11-Nov-26 10:14, 2,269 bytes); typed to
  COM1 at 12:38 (`serial-g1.log`, "LEFTOVER ES1") it ends "Eject it now? / Y eject it / N cancel (anything else
  also cancels) / Enter for [N]:".
- Reproduced at 12:39 on this build (`serial-g1.log`, "EQ"): `INSTALL.BAT /console /eject 1 /nopause >
  C:\T\EQ.TXT 2>&1` had not returned after 2 minutes, the file ending at the same question (`g1b.png`); Y and
  Enter typed into that window: it returned at once, exit 0, "Windows has stopped PCI standard PCI-to-PCI
  bridge. ... The PCIe tunnel stays up until the device is unplugged: ...".
- With `/yes` after the id (the current `E.CMD`) every eject of this section returned in 10 to 20 s.
- The tool refuses `/yes` with a number ("/eject takes /yes only with id=<instance id>: a number is always
  confirmed", read on 98 SE), so a scripted eject must pass the id.
- Not repeated on the released driver: the wait is in the tool before any request reaches Windows or the
  driver, and the released tools are the ones in this kit.

### The legs: the firmware connection manager's host (q35, the Alpine Ridge model), a dock at power-on

`launch-q35.ps1 -Usb4 tb3ar -Enc dock -EncAtBoot`; the package installed again on the same overlays for the new
hardware (`fc /b` clean). No learning under the firmware connection manager; read for no change.

| Guest (tag) | `/devices` | `E.CMD <tag> 1` |
|---|---|---|
| 7 x86 (`w7q` t2) | "firmware connection manager, security level 1", "QEMU USB4 Dock", "its PCIe connection reaches the port at PCI 02:01.0", the dock's NVMe on Stor98, its network adapter and its xHCI started | exit 0, "Windows has stopped PCI Express standard Upstream Switch Port. / It is now safe to disconnect the device."; `cm.reenum.confirm=00000208`, `cm.reenum.eject=00000208`, `cm.reenum.eject.tunnel-left-up=00000208` |
| XP (`xpq` t2, again t6) | the same; "Ethernet Controller - no driver, problem code 28" (no driver for it on XP) | exit 0, "Windows has stopped PCI standard PCI-to-PCI bridge. / It is now safe to disconnect the device."; the same three lines |
| 2000 (`w2kq` t6) | the same (the network adapter disabled in the Found New Hardware wizard) | exit 0, the same text and the same three lines |

### 98 SE on the i440FX SHPC bridge: not learned, as before

`w98b`: an overlay of worker tbs's 98 SE install on this vehicle (`p49\tbs\n98\base98.qcow2`, copied), this
build's `xhcicm.sys` copied over the installed one (`fc /b`: no differences), a cold start, `host-model=intel-adl`,
nothing attached. `cm.reenum.member=00002805`, `u4.map.none=FFFFFFF5`, no `u4.learn.shpc` line (no writer on
98 SE and ME).

| Driver (tag) | Plain enclosure `device_add` at the desktop | `INSTALL.BAT /console /eject id=PCI\VEN_1B36&DEV_0010&SUBSYS_11001AF4&REV_02\082800 /yes` |
|---|---|---|
| this build, `3B5EF80B...` (a) | `u4.pcie.up=00010809`, **`u4.learn=0000010F`**, `u4.learn.expired=00000001` (not learned); Stor98 started, E: read and written (`C98 A compare=OK`, `B compare=OK`); `/devices` has no "its PCIe connection reaches the port" line | "Asked the Thunderbolt driver to stop the device. / Windows has stopped Stor98 Storage Controller for NVMe SSDs. / It is now safe to disconnect the device.", exit 0 (`a-e5.png`); `cm.eject.accepted=00280108` ... `cm.eject.retire=00000001`; no `u4.eject`, "PCIe tunnel: up" afterwards |
| released `3.4.3.0`, `9DBD715B...` (r) | the same three lines, E: read and written | the same text, exit 0 (`r-e1.png`) |

The 9x eject has no notice line: `inst_pcie_unbound_text` belongs to the watch after an NT eject (record 24
section 5.2). This is also 49.17's "read in a 9x guest" for the SHPC plain enclosure: not learned, the eject
stops the drive and the tunnel stays up until the unplug.

### The released driver on the i440FX SHPC bridge, and 10 x86

| Guest (tag) | Step | Result |
|---|---|---|
| XP (`xpb` v2), the released `3.4.3.0` `xhcicm.sys` put in place (`SW.CMD REL`, `fc /b`: no differences), shut down, a cold start | the plain enclosure `device_add` at the desktop; `E.CMD v2 1` | `u4.pcie.up=00010809`, **`u4.learn=0000010F`**, `u4.learn.expired=00000001` (not learned, 48.43's rule with no SHPC reading); exit 0, "Windows has stopped Stor98 Storage Controller for NVMe SSDs. / **The PCIe tunnel stays up until the device is unplugged: ...**"; `cm.reenum.eject=00000028`, `u4.eject.bridge=00000028`, `u4.eject.unmapped=00000028`. The same overlay with this build (a1, above): `0103` and closed. This build's file put back afterwards (`SW NEW`) |
| 10 x86 (`w10b` a1), the kit installed from an elevated prompt (five "Install this driver software anyway" answers, `fc /b`: no differences), shut down, a cold start | start | `cm.reenum.member=00002805`, `u4.map.none=FFFFFFF5`, **`u4.learn.shpc=00000001`** |
| | the plain enclosure `device_add` at the desktop (Windows put its own `stornvme` on the drive); `E.CMD a1 1` | `u4.pcie.up=00010809`, **`u4.learn=00000103`**, "its PCIe connection reaches the port at PCI 00:05.0"; exit 0, "Windows has stopped Standard NVM Express Controller. / **The PCIe tunnel was closed.** / It is now safe to disconnect the device."; `cm.reenum.confirm=00000028`, `cm.reenum.eject=00000028`, `u4.eject.bridge=00000028`, `u4.eject=00000100` |

The 10 guest on one TCG vCPU is slow: a start to the desktop 8 to 15 minutes, `XHCITR /report` 6 to 12 minutes; no
timing was read from it.

### Seen beside the legs, not attributed: XP on the firmware-CM host staying at the boot logo

XP (`xpq`) on the q35 Alpine Ridge model with the dock at power-on stayed at the boot logo twice: the first
worker's guest from about 11:15 until it was stopped at 12:31 (it had restarted Windows after the install on
that host, tag t1; `orphan-t1.png`), and tag t3 at 13:03 (14 minutes, `t3a.png`; the session before it had
ejected the dock and shut down by the power button). Both times the processor sat in the kernel's idle loop
(EIP `804dcbe1`), the emulator's trace showed the firmware's tunnel up and `xhcicm.sys`'s thread still reading
the host interface's ring, and nothing was written to COM1. Once (t4) the same guest ignored the ACPI power
button at the desktop for 3 minutes and was stopped by QMP.

Looked for with matched starts (the plan reviewed first): each measured start a cold start after a session
that had the driver under test loaded from its own start and ended with `shutdown -s -t 0` in the guest
(`tools\xpcycle.sh`, `xpq\cycle-n.txt`, `cycle-r.txt`; a start that had not answered on COM1 after 200 s counts
as hung).

| Driver | Measured starts | At the desktop within 200 s |
|---|---|---|
| this build (`3B5EF80B...`) | t6, t7 (t7 after an eject of the dock), n1 to n4 | 6 of 6 |
| released `3.4.3.0` (`9DBD715B...`) | r2 to r5 (r1 was the start that loaded it) | 4 of 4 |

Not reproduced in ten starts, so not attributed: the two hung starts were with this build's file on disk, and
no hung start was read with the released one, which ten clean starts cannot tell apart from chance. What can
be said from the code: every hook of `d53e848be` is in the software connection manager's learning
(`cmU4Arm`, `cmU4Learn`, `cmU4LearnPending`, the recovery's arm), none of which runs under the firmware
connection manager. Both hung starts followed a start or restart of Windows inside a session that had just
changed the device tree (the install; the eject), and XP on this host model was never read before
(`q35-bases\STATE.md`: "xp ... tb3ar/tb3tr/adl owed"). Left as an observation for 49.25's reader (XP, a dock,
Windows not finishing a transition): the recipe is not known. `xpq`'s `xhcicm.sys` was left as the released
one by the last cycle.

### 10 x86 on q35 with the ASM4242 model: the start read, the eject refused by Windows

`w10q`: the kit installed (`fc /b`: no differences), shut down, a cold start with the ORICO model at power-on
(tag s1). `cm.reenum.member=00020001` and `00020801`, `u4.map.none=FFFFFFF5`, `u4.preserved=00010808`,
`u4.learn.expired=00000001`, no `u4.learn.shpc` line: the same start as XP, 2000 and 7 on this host. `/devices`:
"connections: PCIe up (found at start)", the enclosure's upstream port "removable: yes ... listed in Safely
Remove Hardware", its drive on Stor98. The eject (`/eject "id=<its upstream port>" /yes`), three times over 25
minutes: "Windows refused: STORAGE\Volume\{...}#0000000000007E00 did not allow it. / No changes were made.",
exit 4 - Windows' own veto for a volume in use, before any request reaches the driver (no `cm.reenum.confirm`).
What held the volume was not found (no Explorer window on it; the guest had just mounted the drive for the
first time). So on 10 the at-power-on notice and a dock's hot plug on this host were not read; the changed path
(the SHPC set) was read on 10 above.

### What was not read

- The refresh in a guest (`u4.learn.shpc=00000003`, then `1`): never seen owed (above). Host vectors only.
- XP x64 and the other x64 systems, 8 and 8.1, ME.
- 10 x86 on q35: the at-power-on eject's notice and a dock's hot plug (above).
- The ACPI machines' re-plug limitation with the released driver on 7 (read on XP only).

### Verdict

49.18's emulator part is read: on Windows 2000, XP, 7 and 10 (x86) the i440FX SHPC bridge's plain enclosure
plugged after the start is learned again and its eject closes the connection; on 2000, XP and 7 a dock (and on
XP an enclosure with its own switch) plugged after a cold start with nothing attached closes as before and a
tunnel found up at start keeps the eject's notice (neither read on 10); on XP and 7 a unit plugged again after
one that was there at power-on is learned but its eject does not confirm the closing (the ACPI bus filter, as
with the released driver; sentence added by the coordinator at the fold, 2026-11-11);
`XhciCmInferAtStart` is untouched and off by default; the firmware connection manager's hosts and 98 SE read as
before. No registry value, no tool or installer text changed. Left: the bench reading (49.17's board reading
first), and the ACPI bus filter's limitation of record 21 section 7.4, which is not this task's.

For 49.17 on the owner's board (nothing here names it): with the unit plugged in after Windows has started,
`XHCITR /report` (or the tree's Thunderbolt page) - the driver log's `u4.map.none` or `u4.named` at the start
(whether the host router names its ports), `cm.reenum.member` and `cm.reenum.member.flags` for each tunnel port
(the source; an SHPC source would be new on a real board and is not admitted), then at the plug `u4.pcie.up`
and the `u4.learn` line: low byte `03` learned (a far end, or the link's evidence), `02` known from an earlier
plug, `0F` or `09` not learned (the shape or no evidence), and `u4.learn.expired` when the window ran out. In
`INSTALL /console /devices`: the line "its PCIe connection reaches the port at PCI bb:dd.f" (present only when
the port is known) and whether the unit's first device is a switch's upstream port (a far end) or the drive
itself. After an eject: "The PCIe tunnel was closed." with `u4.eject`, or the notice with `u4.eject.unmapped`,
or "has not confirmed" with `cm.reenum.eject.stale.why` (bit 3 on an ACPI machine: the bus filter). For a unit
attached at power-on: `u4.preserved` and `u4.learn.expired`, and the notice at its eject. No `u4.learn.shpc`
line is expected on a real board.

### The record, and the last commit

`83ce513f0` record 22 section 7.6.5 "As read" (docs only; `scripts\check-text.ps1` OK). Reviewed before the
commit against this sheet: three findings (a 10 leg the sheet did not yet carry; "the slot read empty in every
start" overstated; the section's two statements that 98 SE and ME keep the notice), then two smaller ones (one
more "every Windows" notice claim; this sheet's verdict grouping 10 with readings not made on it), then
nothing material. The plan for the second block of legs was reviewed before it was run (the matched starts
for the XP observation; a stale drop on a clean dock plug not to be taken as a pass). The results are in the same folder. Branch tip `83ce513f0`.

## 49.12 (continued) - ME's standby, a drive unplugged during the sleep, a dock and a chain behind the bridge (2026-11-11, worker stby)

Branch `w49-stby` from `3.5.0.0` at `980245c14`; **no commit** (no source change). The binaries read
are the tip's own (`7e0bf543c`'s write-back, merged at `8a3421ea`): `xhcicm.sys` x86 release
`680131fb...`, qemu `2d0b4417...` (SHA-256, the same files in `xfer98` and `xferme`, `NEW` and
`NEWQ`), copied over `SYSTEM32\DRIVERS` on a fresh overlay per leg (`fc /b` no differences) and
cold-started. Vehicles and helpers as "49.12" above (`C98.EXE`, a Windows program: `watch` started
before the sleep makes the checks after the wake, so no program is started after it), copied to
`E:\xhci98-vm\p49\stby\tools\`; QMP 48580 to 48582; emulator `asm4242-20261110-docks`.

**ME's vehicle.** The night's ME guest never entered S3 because of the launcher, not of ME: with
the IDE disk's geometry hints (`lcyls=1024,lheads=255,lsecs=63`, which the q35 ME base needs on
q35) on QEMU's `pc` machine ME refuses the suspend - `SetSuspendState` returns 1,
`SetSystemPowerState` fails with error 1140, no dialog and no `WM_POWERBROADCAST` at all in 100 s
(`med1\serial-b.log`, `mes1\serial-a.log`, `mes2\serial-a.log`; a monitor window, `P98.EXE mon`).
Without the hints (`gome.ps1 -NoGeo`) both a stock ME and the base with the package installed
broadcast the suspend, reach S3 about 3 s after the call and resume (`med2\serial-a.log`,
`mes2\serial-b.log`: `PBT w=0`, `w=4`, then `w=18` after the wake). 98 SE's launcher has the same
rule (run-49.md "49.12"). ME's base for the legs: `bme\baseme.qcow2` (the q35 ME base on `pc`,
`3.4.3.0`'s `98-me` kit installed, ORICO E: mounted). ME on q35 itself (no hints, the S3 BIOS)
broadcasts the suspend and resumes 4 s later with no QMP `SUSPEND` (`qme1`): no ME vehicle for a
dock or a chain.

| Leg | System, image | Result |
|---|---|---|
| mf1 | ME, release | **PASS**: one standby; about 70 s after the wake E: there, the file written before the sleep compared OK, a new one written and compared; again 2 min later (`mf1\serial-mf1.log`) |
| mq1 | ME, qemu | **PASS**, the same; `cm.reenum.9x.resume-wait=00002801` after 329 ms: ME restored the bridge itself (outcome 1), nothing written back |
| mf2 | ME, release, two standbys in one boot | **PASS**: after each wake E: there, compared and written, and again 2 min after the second |
| mq2 | ME, qemu, the same | **PASS**: `resume-wait=00002801` after 233 and 219 ms |
| mu1 | ME, qemu, the enclosure unplugged during the sleep (QMP `device_del` 8 s after `suspended`), put back 200 s after the wake | **PASS**: no drive at 70 s and 190 s after the wake (`drive=NONE`), `cm.vanish.down`, `resume-wait=00002801`; after the replug `u4.connect` generation 2, `cm.nvlink.up=02000101`, E: back, the old file compared and a new one written, twice; no fault screen |
| mr1 | ME, release, the same | **PASS**, the same |
| uq2 | 98 SE, qemu, unplugged during the sleep, put back 200 s after the wake | the drive's part **passes**: no drive after the wake, the bridge put back by this driver (`resume-restore=00002801`, `.fn=00010804`: the function not answering, as it must, `resume-wait=00002805`); after the replug `u4.connect` generation 2, `cm.nvlink.up=02000101`, E: back, compared and written. **But** a fatal exception 0E at `0187:BFF9DFFF` stood on the screen 30 s after the replug (`u98q2\uq2-r30.png`), and after its key the shell was gone (`uq2-end.png`) |
| ur1 | 98 SE, release, the same | the same: the drive back and written; the fatal 0E 30 s after the replug (`u98r\ur1-r30.png`) |
| uq1, ue1 | 98 SE, qemu, put back 31 s after the wake | the fatal 0E at `0187:BFF9DFFF` within 20 s of the replug (`uq1-r20.png`, `ue1-r20.png`); in ue1 the watcher went on after the key: E: back, compared and written (`serial-ue1.log`) |
| uf1 | 98 SE, release, put back 31 s after the wake, no watcher process | no fault to 90 s after the replug; the check started then (a program after the wake) ended the shell, the known fault |
| uf2 | 98 SE, qemu, the same | the fatal 0E within 20 s of the replug, no program started by the leg; E: back and written afterwards |

So a replug after a wake faulted in 5 of 6 legs on 98 SE, early or late (the day's first reading,
"a late replug does not fault", was wrong: `uq2-r30.png` and `ur1-r30.png` are the fault screen),
and in none of 2 on ME. The drive itself came back and was written in every leg that read it.

**A dock and a chain behind the bridge.** The i440FX vehicle's SHPC bridge takes the NVMe only (the
emulator: "dock=on and downstream-ports need a PCIe tunnel port"), so the vehicle is 98 SE on q35:
the system disk on a `piix3-ide` with no geometry hints, the S3 BIOS, the Intel ADL host model with
the software connection manager (tunnel root ports 00:05.0 and 00:06.0), `3.4.3.0` installed with a
dock (a USB4 device router with a PCIe switch and its own NVMe, `downstream-ports=1`) and an ASM2464
enclosure chained behind it, both at power-on (`b98\q98chain.qcow2`, `tools\goq.sh`, `lq.sh`); both
volumes (E:, F:) written and compared before the sleep.

| Leg | Image | Result |
|---|---|---|
| cq1 | `3.4.3.0` release | **frozen after the wake**: the watcher wrote nothing more to COM1 (its last line the sleep call), the leg was stopped by QMP quit |
| cq2 | `7e0bf543c` qemu | the tunnel port put back (`resume-restore=00002801`, `resume-wait=00002805`; `info pci`: 00:05.0 secondary bus 1, subordinate 9) and nothing below it: the dock's upstream port 01:00.0 read secondary bus 0 at +30 s and at the end (`qc3\pci-cq2-w30.txt`), the chain request's walk repeated without a function (`cm.chain.9x.walk`); **frozen after the wake** as cq1 |

So a dock or a chain across a standby on 98 SE is lost and the guest stops answering, on `3.4.3.0`
and on the tip alike: not a regression, and not what 49.12's write-back covers (it writes the tunnel
port and function 0 right behind it). A dock alone and ME (no ME vehicle on q35) were not read.

**The subtree write-back (record 21 section 6.1.4, an item 7): designed, not converged, not coded.**
Draft and its history: `E:\xhci98-scratch\w49\stby\design-sub.md` (draft 4; drafts 2 and 3 beside
it), reviewed four times (6, 4, 2 and 3 material findings). Settled: the table of bridges and
functions copied at the suspend (nested by bus range and windows, read twice), the member read
against the table's own reference at every pass, no permission carried between passes, a table kept
across a second standby. Not settled: what a copy below the member is bound to - each round found
another way the devices below the member change without the CONFIGMG worker knowing (a link-down
that is swept, not posted; a chained router rebuilt after its parent with no identity comparison;
an eject transaction in flight at the suspend; the order in which the resume wakes the worker and
delivers the chained device's verdict). As drafted the change reaches `cm_cfgwk.c`, `cm_ports.c`,
`cm_ejectos.c`, `cm_reenum.c`, `cm_power.c` and the USB4 core; the open points are listed at the end
of the draft ("HAND-OFF", O1 to O3). Put to the owner: `E:\xhci98-scratch\w49\stby\QUESTIONS.md`, Q1.

**Hibernation.** Neither guest offers it on these vehicles: ME reports S4 not supported and
`IsPwrHibernateAllowed` 0 (`P98.EXE caps`: `med1`, `mes1`, `qme1` serial logs; S3 supported); 98 SE
has no hibernation of its own. Not read; the bench's.

### The fatal exception 0E on 98 SE when a drive arrives after a wake: where it was and was not seen

Each leg plan was reviewed before it ran (three short plan reviews; their wording limits are kept
below). 98 SE, the i440FX vehicle, one vCPU, QMP 48580; "bare" is the clean q35 98 SE base with none
of this package, the same emulated hardware.

| Leg | System | What was done | Fault screen |
|---|---|---|---|
| ca2 | bare | standby, wake; 70 s later programs started from Start > Run | none for 70 s; `0167:BFF9DFFF` at a program's start (`ca2\a2-m1.png`, `a2-m4.png`): the lessons' fault |
| n2 | package (qemu) | **no standby**; the enclosure unplugged, put back 31 s later | none; E: back, written and compared (`pn2`) |
| n3 | package (qemu) | standby with the drive kept, wake; unplugged at +60 s, put back at +91 s | none after the unplug; **`0187:BFF9DFFF`** within 20 s of the replug (`pn3\n3-r20.png`) |
| n11 | package (qemu) | n3 again, one poll a second after the replug | **the fault**: the last normal screen 5 s after the replug, the fault screen at 7 s; the connection manager's log lines for the arrival (`u4.connect` generation 3, `u4.pcie.up`, `cm.reenum.9x.call`, `cm.nvlink.up=02000101`, the no-IRQ walk's `walk.node=01087F00`, `cm.nvlink.9x.proof-read`: debugcon lines 314 to 364) were all written by the poll at 4 s, and none followed (`pn11\leg-n11.txt`) |
| n12, n13 | package (qemu), **Stor98's port driver renamed away** before the cold start (`stor98.mpd` to `stor98.off`; no volume: `drive=NONE` before the sleep) | n3's steps, polled 40 s | **none**, twice; the connection manager did its part as in n11 (`cm.reenum.9x.call`, `walk.node=01087F00`, `cm.nvlink.up=02000101`) |
| n1, n4 | bare | the enclosure unplugged during the sleep | void: the guest restarted between 5 and 30 s after the wake, twice of two (the trace ends in a machine reset) |
| n7 | bare | n3's steps (the unplug and replug are not visible to Windows without the connection manager) | none |
| n5, n6 | bare, an empty IDE CD-ROM added | a disc (ISO 9660, no AUTORUN.INF; listed in a boot with no standby) put in 31 s after a wake | none, twice (to 150 s) |
| n8 | bare | **no standby**; a PCI-to-PCI bridge hot-added on bus 0 (ACPI hot-plug) | none; Windows shows the Add New Hardware Wizard for it (`cn8\n8-h90.png`) |
| n9, n10 | bare | standby, wake, the same hot-add 31 s after the wake | none, twice; the wizard as in n8 |

With the six replug legs of the first table (uq1, ue1, uf2, uq2, ur1 with the fault; uf1 without)
that is 7 of 8 replugs after a wake with the fault and Stor98 loaded, 0 of 2 with Stor98's port
driver away, 0 of 1 with no standby before the replug, and none on the bare platform by a disc's
arrival or a device's arrival after a wake. ME: 0 of 2 (mu1, mr1).

**Reading (observations so far, no cause decided).** On this emulator the fault after a replug that
follows a wake has been seen only with the package installed, and only in legs where Stor98's port
driver loads; it was not reproduced in two legs with it out of the way, where the connection
manager's re-enumeration, its no-IRQ walk and its statement still ran. That points a later reading
at Stor98's start of the function, or at the volume's arrival, after a wake on 98 SE - it does not
show that either is required (two legs; the fault had missed one leg in eight; taking the port
driver away also changes the standby itself). The fault address is the bare platform's own
after-standby fault's (`BFF9DFFF`, a program started after a wake; selector 0167 there, 0187 with
the package), and the shell is what ends in both, but a matching address is not a common cause. The
drive itself is back and written in every faulting leg that read it. Not read: a real machine (the
platform fault is this emulator's, so the bench decides whether a user ever sees this one); the
release flavour beyond ur1 (fault) and uf1 (none). No fix proposed.

## 49.13 - S.6 read to the end: 10 x64 on the Intel ADL model, the software connection manager (2026-11-11, worker stby)

**Vehicle.** 10 x64 from the q35 base (an overlay, `E:\xhci98-vm\p49\stby\w10`), q35, 2 vCPUs, TCG,
virtio-vga with viogpudo, the S3 BIOS, `-Usb4 adl` (the Intel ADL host model, the software
connection manager, no pre-boot one), the ORICO model at power-on; `3.4.3.0`'s `nt-x64` kit installed
(exit 3), then the `xhcicm.sys` under test signed inside the guest with a throwaway certificate and
copied over `System32\drivers` (`P3.CMD`; SHA-256 on COM1), `XhciCmSleepEntry` written machine-wide,
a cold start per leg (`tools\s6.sh`, `go10.sh`; QMP 48583). The leg (`LEG.CMD`, `HB.CMD`, started at
logon by `AUTO.CMD`): the drive found, a file written and compared, `wmic diskdrive`, the report; a
**heartbeat** started before the sleep - 90 rounds about 5 s apart, each announcing on COM1 a
listing of C:, a listing of the USB4 drive and a write to it with their return codes, round 12 also
`wmic diskdrive`, the compare of the pre-sleep file and the report - then `SetSuspendState`; QMP
`system_wakeup` 20 s after `suspended`; the serial log and the vCPUs watched for up to 11 minutes;
the heartbeat's own `shutdown` ends the leg. Pass: the heartbeat reaches its end and the guest shuts
itself down. A heartbeat that stops with QMP still `running` would be a suspected hang (registers
saved; in the last block also the ACPI power button tried).

| Leg | `xhcicm.sys` (amd64) | `XhciCmSleepEntry` | Result |
|---|---|---|---|
| a1, a2, a3 | this branch, qemu flavour (`8107bdbc...`, signed `c87ed384...`) | 1 (`cm.usb4.sleep=00000101`, `u4.sleep.entered=00000002`) | **no hang**, three of three: S3 reached, the wake, `cm.reenum.resume.settle=02020000`, all 90 rounds with the drive listed and written (return code 0 each), `wmic` `QEMU NVMe Ctrl SCSI Disk Device OK`, the pre-sleep file compared (no differences), the guest shut itself down 8 to 9 minutes after the wake |
| b1, b2, b3 | the same | 0 (`cm.usb4.sleep=00000100`) | **no hang**, three of three: the same |
| c1, c2 | release `3.4.3.0` (`56f05f9f...`, signed in the guest `dcebb361...`) | 1 | **no hang**, two of two: the same checks to the heartbeat's end and the guest's own shutdown |
| d1, d2 | this branch, release flavour (`7355e0de...`, signed in the guest `7aa164fd...`) | 1 (`cm.usb4.sleep=00000101`, from the report) | **no hang**, two of two: the same checks to the heartbeat's end and the guest's own shutdown |

**What this says.** In ten legs - this branch's qemu flavour at 1 and at 0 (three each), this
branch's release flavour at 1 (two) and the released `3.4.3.0` image at 1 (two) - 10 x64 on the ADL
model with the software connection manager went through S3 and back with the USB4 drive answering
for the whole watch, and shut itself down when asked by its own script. The "stops answering after
the wake" of 48.35 (two readings, on the amd64 kit of `89715976` in another guest image, with the
checks typed or started after the wake) **was not reproduced**, on the released image either; so
nothing here says what it was, and nothing says this branch changed it. The difference left
between the two readings is the guest image and how the checks were made (here everything after the
wake runs from a script started before the sleep; there input after the wake was needed, and
whether typed input reaches 10 x64 after a wake on that vehicle was never shown). Not read: S4 on
this model, the other legs 49.13 lists (S.2, S.3 on 7 x64, S.3's hibernation on 10 x64, S.5), and
the owner's machines.

Evidence: `E:\xhci98-vm\p49\stby\w10\` (`serial-*.log`, `debugcon-*.log`, `s6-*.txt`,
`*-regs-*.txt`, `*-cpus-*.txt`, `*-pci-*.txt`, screens), the swaps' hashes in `serial-p3b.log`,
`serial-swO1.log`, `serial-swR1.log`; the amd64 build of the base: gated `build-driver.cmd all
-amd64`, PASSED (`E:\xhci98-scratch\w49\stby\gated-amd64-base.log`). 49.12's evidence:
`E:\xhci98-vm\p49\stby\{mf1,mq1,mf2,mq2,ume,umer,u98q,u98q2,u98r,u98e,u98f,u98f2,qc2,qc3,ca2,cn1,
cn4..cn10,pn2,pn3,pn11..pn13}\`; every QEMU start and stop in `E:\xhci98-vm\p49\stby\pids.txt`.
The plan and design reviews are kept in the worker's scratch folder.

### Merge (stby)

Reading only: branch `w49-stby` has no commit.

## 49.4 (continued) The two Windows 10 legs moved to WHPX in the release matrix; the remove's restart read (2026-11-11, worker whpx)

Branch `w49-whpx` from `3.5.0.0` at `980245c14`. Development host A, QEMU 11.1.2 (`asm4242-20261110-docks`, `4959a9ab...`, `v11.1.2-8-g76e89a5`), ports 48600-48619 (the block first given, 49760-49779, lies inside Windows' excluded port ranges 49672 to 49971 on this host and cannot be bound: "Failed to bind socket"; `netsh interface ipv4 show excludedportrange protocol=tcp`).

**What changed (`3f0265601`, `ebe3e3b81`).** The Windows 10 legs were kits outside the tree (47.final's); the tree's release matrix had no Windows 10 guest and every leg's QEMU line said TCG. Now:

- `release-legs.psd1`: a leg's `Accel` (`tcg`, the default, or `whpx`), `Cold`, `TcgMinutes`, `TcgStallSeconds`; two new legs, `w10x86-console` and `w10x64-console`, the only WHPX legs (a self-test holds the file to that): console installs with `/usb`, `Devices` (the controller disabled, enabled, removed and rescanned with Windows' own `pnputil`, the stick copied to again after the enable and the rescan: six more checks, all needed for PASS), and for x64 `TrustCert` (the package's own `nt-x64\xhci98-test.cer` into Root and TrustedPublisher; the guest must already be in test-signing mode).
- `lib\legvm.ps1`: `Resolve-LegAccel` (the leg's own; TCG under `-ForceTcg`; nothing turns a TCG leg into a WHPX leg), `Get-LegAccelArg` (TCG's value as before, so no starting point's hash moves: a self-test holds the machine line and two hashes to their values at `980245c14`), `Assert-LegOneAccel` (exactly one `-accel` in the command line QEMU gets, the leg's: a second one, from a guest's `Extra`, is what would let QEMU fall back by itself), `Assert-LegWhpx` (the refusal), `Start-LegQemu -Accel` (no restore under WHPX; QEMU asked at every start which accelerator it runs on, QMP `query-accelerators`), `Get-LegStderrReason`, `Get-LegVerdictReasons` (run-leg.ps1's verdict, moved here to be tested), `Get-LegRemoveNote`.
- `run-leg.ps1`, `release-matrix.ps1`: `-ForceTcg`; a WHPX leg starts cold from the starting point's disk; the WHPX probe before anything of the leg is started (the matrix: once for each guest a selected WHPX leg runs on, before its first leg); `result.json` with `accel`, `accelOwn`, `accelForced` and each start's `accel`; the table's Accel column, taken from each leg's own record.
- `make-start.ps1`, `leg\XHSETUP.CMD`: Windows 10 (the elevated logon task as on Vista and 7; `SetupElevated` `run`: the setup typed into the Run box and started with Ctrl+Shift+Enter; `ConsentSeconds`, `SetupSeconds`, `DesktopSeconds`). Starting points are made and saved under TCG as before; `make-start.ps1` names no accelerator.
- `leg\LEGNT.CMD`: `:trust`, `:devseq` (and `:devsay`, `:dwait`, `:dquiet`, `:dcopy`), `:un4`, `:unagain`.
- `selftest.ps1`: a last section for the release legs, 79 checks (561 in all). `build-and-test.md` ("Faster emulator legs": "The accelerator of a leg", "The Windows 10 legs", "The controller's remove keeps a TCG reading"; "Windows 10 target VMs"), `scripts\vm-matrix\README.md`, `release.config.sample.psd1`.

**The refusal (no silent fallback).** `Assert-LegWhpx`: this QEMU must list `whpx` (`-accel help`), and a probe (the guest's own machine, processors, memory, firmware path and `-accel` value, no disk, `-S`) must answer QMP and say `whpx` is what it runs on; otherwise the leg, or the whole matrix, stops with QEMU's own message and "Nothing was started. A WHPX leg does not fall back to TCG; -ForceTcg runs it under TCG (the cross-check)." Read: against stand-in QEMUs in the self-test (one that lists no WHPX; one that lists it and ends with "failed to initialize whpx"; a taken port; a probe whose log line cannot be written), and once for real, by accident: a second WHPX leg started in a slot whose port another of my legs held was refused before it started ("port 48603 is in use (the WHPX probe)"), with no `result.json`. The real probe on this host comes up in about a second and prints QEMU's own "warning: Ignoring request for interrupt vector 0", which every WHPX start here prints; that one line is not a reason against a WHPX leg, anything else on QEMU's stderr still is.

**The controller's remove in the leg: what was chosen.** The step runs under either accelerator; `ctrl-remove` passes when `pnputil /remove-device` ends with 0 (removed) or 3010 (removed, Windows asks for a restart); which of the two is recorded (`ctrlRemove`: "done" or "restart asked") and judged under neither accelerator, because TCG itself has given both answers (49.4's first section). Under WHPX the record adds "recorded only: this step keeps a TCG reading, which this leg with -ForceTcg on the same package gives", and claims none. What follows is judged as always: the rescan and its copy, the uninstall, the list after the next cold start. An uninstall that stops with exit 4 is taken for a pending restart only when the guest finds "restart pending" in the installer's log (`uninstall-pending`), and is then run again after the next cold start (`uninstall-again`, which must end 3). The part of `setupapi.dev.log` the remove wrote goes to COM1. Rejected: failing a WHPX leg on "restart asked" (the night's four WHPX kit legs would all have failed), and leaving the remove out under WHPX (the rescan and the uninstall after a remove would then go unread on every routine leg).

**Not built.** Signing an unsigned x64 package inside the guest with a throwaway certificate: a package staged without the project's key has no catalogs (`make-package.ps1` makes them only when it signs), so that would mean making catalogs in the guest too. `w10x64-console` therefore needs a signed x64 package (a release's) and is listed as not in the package otherwise. WAVCHK's playback check is not on the Windows 10 legs: on 10 the endpoint is named "Speakers (Audio Device)", which WAVCHK's `/name USB` does not match (read in the experiments below).

**Review.** The design (a design round: 8 findings, all taken; `E:\xhci98-scratch\w49\whpx\design-49.4.md` and `design-49.4-revised.md`), then the uncommitted change in two rounds (round 1: QEMU's WHPX start warning failed every healthy WHPX leg; the probe could outlive a failed log line; both fixed; round 2: nothing material), then committed. The self-test's own fault below, one more round, nothing material.

**Build.** One gated x86 release build per reviewed change (slot `gated.1`, `XHCI98_SELFTEST_SKIP_OK=1`). The first, of `3f0265601`, failed in "VM matrix verdict self-tests": the new section's stand-in probe waited with `ping`, and the `ping` outlived its killed `cmd.exe` for 30 s holding the step's log, which `build-steps.ps1` then could not read (`build-release-1-failed.log`). `ebe3e3b81` makes the stand-in spin inside `cmd.exe`; the build of `ebe3e3b81`, 09:42 to 09:54: "BUILD + GATES PASSED (release)", exit 0 (`E:\xhci98-scratch\w49\whpx\build-release.log`); `xhciinst\di`, `xhciinst`, `xhcimark`, `xhciperf` built and `make-package.ps1 -Flavor release -Arch x86` exit 0 (`tools.log`). Kit `E:\xhci98-scratch\w49\whpx\kit\PKG` (family layout, `tools\`, `SETUP.EXE`, `INSTALL.BAT`): `xhci98.sys` `50bdb9d4...`, `xhciuas.sys` `595ad86d...`, `SETUP.EXE` `e5fb74a8...`, `XHCIINST.EXE` `87416b74...`, `XHCITR.EXE` `6cdae7e7...` (`kit\kit-sums.txt`). No amd64 build: an x64 package of this tree would be unsigned, which the x64 leg cannot install; the x64 legs below use the published `3.4.3.0` download (`E:\xhci98-release\3.4.3.0\xhci98-3.4.3.0.zip`, `cc0ab19e...`, unpacked to `E:\xhci98-scratch\w49\whpx\pkg343`).

**Starting points.** `make-start.ps1 -Guest w10x86` and `-Guest w10x64` on the q35 bases (TCG, `-smp 2`, 2048 and 4096 MB): the elevated Run box worked at the first try on both; cold start to the poller 402 s and 404 s, state 1.5 GB and 2.3 GB, 20 min each (08:55 to 09:15). The logon task fires at logon on both images. (A first try with the default 60 s desktop wait typed into a black screen: 10 under TCG shows one for minutes; hence `DesktopSeconds`.)

**The final legs on `e5dab6708` (replacement worker, 2026-11-11 afternoon).** The worker of the morning was stopped at about 11:20; what follows was run after it, on the worktree at `e5dab6708` with nothing uncommitted. Development host A with seven other workers' guests running (their load is not mine to state; the wall times below are with it).

| Run | Leg | Package | Accel (as `result.json` names it) | Verdict | Wall time | Starts | Exit codes | Remove |
|---|---|---|---|---|---|---|---|---|
| `final-w10x86-console` | `w10x86-console` | this tree's kit (`kit\PKG`, `xhci98.sys` `50bdb9d4...`) | `whpx` (own `whpx`, not forced) | PASS | 359.0 s (12:31:10 to 12:37:09) | 3 (130, 193, 36 s) | install 3, list 0, uninstall 3, list-final 0 | done ("Query-and-Remove succeeded"); the controller then on `usbxhci.inf` |
| `final-w10x64-console` | `w10x64-console` | the `3.4.3.0` download (`pkg343`) | `whpx` (own `whpx`, not forced) | PASS | 388.2 s (12:37:11 to 12:43:39) | 3 (88, 253, 47 s) | install 3, list 0, uninstall 3, list-final 0 | done; then `usbxhci.inf` |
| `final-tcg-w10x86-console` | `w10x86-console` `-ForceTcg` | this tree's kit | `tcg,thread=multi` (own `whpx`, forced) | PASS | 4127.9 s, 68.8 min (12:31:10 to 13:39:58) | 3 (1972, 1608, 548 s) | install 3, list 0, uninstall 3, list-final 0 | **restart asked**: "Query-removal was vetoed by STORAGE\Volume\...USBSTOR#Disk&Ven_QEMU&Prod_QEMU_HARDDISK...XHLEGSTICK01... (veto type 6: PNP_VetoDevice)", the stick's volume, not an audio function (the leg carries none) |

Every check PASS in all three (the forced-TCG cross-check too, whose `ctrl-disable` passed after the same wait; x86: `driver-file`, `list-current`, `stick-seen` E:, `stick-copy`, `ctrl-disable`, `ctrl-enable` "E: after 0 s", `stick-copy-enable`, `ctrl-remove`, `ctrl-rescan` "E: after 0 s", `stick-copy-rescan`, `list-removed`; x64 the same and `cert-trust`, `test-signing`). `e5dab6708`'s wait is in all three serial logs: "stick volume idle: 10 of 10 samples with no transfer in round 1". Evidence: `E:\xhci98-vm\p49\whpx\rm\runs\final-w10x86-console\`, `final-w10x64-console\`, `final-tcg-w10x86-console\` (`result.json`, `serial-b1..3.log`, `cmdline-b*.txt`, `qemu-b*.err`), `E:\xhci98-scratch\w49\whpx\logs\final-*.log`.

**The legs of the morning (worker whpx before the stop; read again from their records).** Every row is `run-leg.ps1` on the tracked legs (`E:\xhci98-vm\p49\whpx\rm\runs\<run>\result.json`), a fresh disk overlay of the starting point each; the accelerator is what `result.json` names.

| Run | Leg | Leg script of | Package | Accel | Verdict | Wall time | Remove |
|---|---|---|---|---|---|---|---|
| `dev1-w10x86` | `w10x86-console` | the uncommitted change | 49.1's kit (`w49\leg\kit\PKG`) | `whpx` | PASS | 338.4 s | done |
| `dev1-w10x64` | `w10x64-console` | the uncommitted change | `3.4.3.0` | `whpx` | PASS | 256.4 s | done |
| `proof-w10x86-console` | `w10x86-console` | `ebe3e3b81` | this tree's kit | `whpx` | PASS | 341.5 s | done |
| `proofb-w10x64-console` | `w10x64-console` | `ebe3e3b81` | `3.4.3.0` | `whpx` | PASS | 296.7 s | done |
| `whpx2-w10x86-console` | `w10x86-console` | `ebe3e3b81` | this tree's kit | `whpx` | PASS | 341.1 s | done |
| `tcg1-w10x86-console` | `w10x86-console` `-ForceTcg` | `ebe3e3b81` | this tree's kit | `tcg,thread=multi` (forced) | **FAIL** | 3488.9 s (58.1 min; starts 1737, 1227, 525 s) | done |
| release matrix `20261111-110241` | `w10x86-console`, `w10x64-console` (`release-matrix.ps1 -Only ... -MaxVcpu 2`) | `e5dab6708` | `3.4.3.0` | `whpx`, `whpx` | PASS, PASS | 6.2 and 5.2 min; the matrix 11.7 min | done, done |

(`proof-w10x64-console` has no record: it was started in a slot whose port another leg held and was refused before anything started, the refusal read by accident in the section above. `tcg2-w10x86-console` was stopped by the worker 18 s in, to fix the leg script first. `tcg3-w10x86-console`, the first forced-TCG run of `e5dab6708`, and `expD3` were cut off by the stop, in their first and second starts; neither is counted.)

**The forced-TCG leg's failure, and `e5dab6708`.** `tcg1`: `ctrl-disable FAIL pnputil exit 3010`. `pnputil /disable-device` answered "System reboot is needed to complete configuration operations!", the controller stayed started, and the enable after it "Failed to enable device ... Device is pending system reboot to complete a previous operation" while the check of `ebe3e3b81` passed it (the drive had never left). Five WHPX runs of the same script had passed the disable. What stood in the disable's way under TCG was not read (the device-install log part is taken only for the remove). `e5dab6708`: before the disable the guest waits, as best it can, for the stick's volume to show no transfer (`:dstill`, `typeperf`, at most 24 rounds of ten samples 2 s apart); `ctrl-disable` names the deferred case; `ctrl-enable` fails when the disable did not pass or `pnputil` says "Failed". Reviewed (nothing material), committed 11:00.

**Build of `e5dab6708`.** Its files (`leg\LEGNT.CMD`, `build-and-test.md`) changed after the gated build of `ebe3e3b81`, so the self-tests were run again through their entry, a gated x86 release build (slot `gated.1`, `XHCI98_SELFTEST_SKIP_OK=1`), 12:32:56 to 12:47:34: "VM matrix verdict self-tests: passed (exit 0, 0:33)" (run, not skipped: "selftest: 561 checks, all passed"), "BUILD + GATES PASSED (release)", exit 0 (`E:\xhci98-scratch\w49\whpx\build-release-e5dab.log`). The kit was not remade: no file of the package changed between the two commits.

**What asks for the restart at the remove (the open question of 49.4's first section).** Windows' device-install log names it; the legs now send the part the remove wrote to COM1 (`ctrl-remove-log`). Read on scratch copies of the leg (`E:\xhci98-scratch\w49\whpx\exp\`: `exp-legs.psd1`, a copy of `scripts\vm-matrix` whose `LEGNT.CMD` can remove with the night kit's small devcon `DC.EXE`, SetupAPI's `DIF_REMOVE`, wait before the remove (`XWAIT.TXT`), play Windows' own `Alarm01.wav` in a loop through the default audio device from 10 s before it (`XPLAY.TXT`), or install nothing at all, so that the same sequence runs with the controller on Windows' own xHCI driver (`XCTL.TXT`); `plug.ps1` adds a hub with a mouse, and a `usb-uas` that QEMU never attached and that is not counted). 10 x86 under WHPX unless said, this tree's kit; QEMU's `usb-audio` plugged by the host as in the matrix's audio legs. In every run the sequence is the leg's: disable, 16 s, enable, the stick's copy, the remove, so "no wait" is a remove a few seconds after the enable.

| Run | Controller's driver | On the bus beside the stick | Remove by | Wait before it | Answer | The remove's log section |
|---|---|---|---|---|---|---|
| `expA-w10x86-audio-whpx` | xhci98 | audio only | pnputil | none | done | 09:37:11, 0.11 s, "Query-and-Remove succeeded" |
| `expB-w10x86-hub-whpx` | xhci98 | hub, mouse (no audio) | pnputil | none | done | 09:42:52, 0.11 s, "Query-and-Remove succeeded" |
| `expC-w10x86-uas-whpx` | xhci98 | usb-uas (no audio) | pnputil | none | done | 09:48:32, 0.12 s, "Query-and-Remove succeeded" |
| `expD1-w10x86-stick-dc-whpx` | xhci98 | stick only | DC.EXE | none | done | 09:54:02, 0.14 s, "Query-and-Remove succeeded" |
| `expD2-w10x86-bus-dc-whpx` | xhci98 | audio, hub, mouse | DC.EXE | none | restart asked | 10:06:57, 5.54 s, vetoed, type 5 PNP_VetoOutstandingOpen |
| `expE2-w10x86-bus-dc-whpx-again` | xhci98 | audio, hub, mouse | DC.EXE | none | restart asked | 10:33:06, 5.44 s, vetoed, type 5 PNP_VetoOutstandingOpen |
| `expD5-w10x86-bus-dc-whpx-third` | xhci98 | audio, hub, mouse | DC.EXE | none | restart asked | 13:39:51, 5.50 s, vetoed, type 5 PNP_VetoOutstandingOpen |
| `expD4-w10x86-bus-pnputil-whpx` | xhci98 | audio, hub, mouse | pnputil | none | restart asked | 12:57:09, 5.50 s, vetoed, type 5 PNP_VetoOutstandingOpen |
| `expE3-w10x86-bus-dc-wait30-whpx` | xhci98 | audio, hub, mouse | DC.EXE | 30 s | done | 13:22:02, 0.22 s, "Query-and-Remove succeeded" |
| `expE4-w10x86-bus-dc-wait60-whpx` | xhci98 | audio, hub, mouse | DC.EXE | 60 s | done | 13:31:29, 0.16 s, "Query-and-Remove succeeded" |
| `expE1-w10x86-bus-dc-wait180-whpx` | xhci98 | audio, hub, mouse | DC.EXE | 180 s | done | 10:49:15, 1.80 s, "Query-and-Remove succeeded" |
| `expG2-w10x86-bus-pnputil-play-whpx` | xhci98 | audio, hub, mouse; a sound playing | pnputil | none | restart asked | 13:12:01, 5.74 s, vetoed, type 5 PNP_VetoOutstandingOpen |
| `expG3-w10x86-bus-dc-play-whpx` | xhci98 | audio, hub, mouse; a sound playing | DC.EXE | none | restart asked | 14:08:00, 5.65 s, vetoed, type 5 PNP_VetoOutstandingOpen |
| `expH1-w10x86-bus-dc-inbox-whpx` | Windows' own | audio, hub, mouse | DC.EXE | none | done | 12:48:45, 0.16 s, "Query-and-Remove succeeded" |
| `expH2-w10x86-bus-dc-inbox-whpx` | Windows' own | audio, hub, mouse | DC.EXE | none | done | 13:04:01, 0.14 s, "Query-and-Remove succeeded" |
| `expH3-w10x86-bus-dc-inbox-whpx` | Windows' own | audio, hub, mouse | DC.EXE | none | done | 13:46:54, 3.77 s, "Query-and-Remove succeeded" |
| `expH4-w10x86-bus-dc-inbox-whpx` | Windows' own | audio, hub, mouse | DC.EXE | none | done | 13:53:19, 0.16 s, "Query-and-Remove succeeded" |
| `expH5-w10x86-bus-dc-play-inbox-whpx` | Windows' own | audio, hub, mouse; a sound playing | DC.EXE | none | restart asked | 14:00:03, 5.62 s, vetoed, type 5 PNP_VetoOutstandingOpen |
| `expH6-w10x86-bus-dc-play-inbox-whpx` | Windows' own | audio, hub, mouse; a sound playing | DC.EXE | none | restart asked | 14:15:37, 5.69 s, vetoed, type 5 PNP_VetoOutstandingOpen |
| `expF1-w10x64-bus-pnputil-whpx` | xhci98 (10 x64, 3.4.3.0) | audio, hub, mouse | pnputil | none | done | 10:20:04, 0.16 s, "Query-and-Remove succeeded" |

- **The veto.** Every refusal reads the same: "Query-removal was vetoed by USB\VID_46F4&PID_0002&MI_00\... (veto type 5: PNP_VetoOutstandingOpen)", "Setting needs reboot", "Query-and-Remove failed: 0x17: CR_REMOVE_VETOED." The device named is the audio function of QEMU's `usb-audio`, which Windows' own `usbaudio` drives (on `xhci98` below "xHCI98 USB Composite Device", on Windows' driver below `usbccgp`). A refused query takes 5.4 to 5.7 s, a granted one 0.1 to 0.2 s (two took longer: 1.8 s on `xhci98` after the 180 s wait, 3.8 s on Windows' driver).
- **A device in use is refused on either driver.** With the sound playing: `xhci98` 2 of 2, Windows' own driver 2 of 2. So the refusal itself is Windows' and not `xhci98`'s doing.
- **With no sound, a difference between the drivers that is not explained.** A few seconds after the enable the remove was refused on `xhci98` 4 of 4 (`DC.EXE` three times, `pnputil` once) and granted on Windows' own driver 4 of 4 (one of them only after 3.8 s). On `xhci98` it was granted after 30 s, 60 s and 180 s (one run each), and with no wait in two other arrangements (the audio device with no hub or mouse, 49.1's package; 10 x64 with all of them, `3.4.3.0`). So on `xhci98`, in this arrangement, something still has the audio function open a few seconds after the controller's enable, for less than about half a minute; on Windows' driver it had been closed by the same moment. What has it open (Windows' audio stack building its endpoint for a device that has just come back is the likely holder, not read), and why it stays open longer on `xhci98` (the function coming back later after the enable, or something Windows asks of it at the close taking longer through `xhci98`) was not read: no trace was taken, the release build writes none to the debug port, and XHCITR's dump (which has a note ring) was not taken at the refusal. **This is a finding for the driver's owner task, not a fix made here**; nothing in these runs shows a wrong answer from `xhci98`, only the later close.
- **The accelerator.** All of these runs are WHPX's. The night's split (all four WHPX kit legs asked, neither TCG leg did, both kits carrying `usb-audio`) fits timing: under WHPX the kits' remove followed the enable within seconds, under TCG by minutes. That is a likely reading, not a measured one (no matched pair under TCG).
- **A second reason, under TCG, with no audio device.** The forced-TCG final leg above was asked for a restart by the stick's own volume ("vetoed by STORAGE\Volume\...USBSTOR#Disk&Ven_QEMU&Prod_QEMU_HARDDISK...XHLEGSTICK01... (veto type 6: PNP_VetoDevice)", the section 2.2 s), just after the leg's copy to it. The nine WHPX runs of the two tracked legs all answered "done".
- **What stays as decided.** The remove's answer is recorded under either accelerator and judged under neither; the rescan and its copy, the uninstall and the last list are judged, and passed in every tracked run, after "done" and after "restart asked" alike.

`build-and-test.md` ("The controller's remove keeps a TCG reading") says this now; reviewed (one point taken: the timing reading is not called measured and the accelerator not called excluded), `check-text.ps1` OK, committed as `7329e3429`.

Evidence: `E:\xhci98-vm\p49\whpx\rm\runs\<run>\` (`serial-b2.log` holds `ctrl-remove-log`; `result.json`; `plug.log`), `E:\xhci98-scratch\w49\whpx\logs\<run>.log`, `exp\remtable.py`. QEMU processes: `E:\xhci98-vm\p49\whpx\rm\pids.txt`.

## 49.5 (continued) 2000's disable with copies in flight again, on this tree's package (2026-11-11, worker whpx)

Readings only; no tracked file changed. The plan (what is run, in what order, what passes) was reviewed before the first start and two points taken: integrity is judged on the copies that ended with exit 0, a copy cut off being recorded apart; and the 98 SE plays count as "beside the copies" only where the copy loop is shown to be running (the review notes in the worker's scratch folder).

Development host A, QEMU 11.1.2 (`asm4242-20261110-docks`), TCG, q35, one processor, 512 MB, a fresh overlay of the 2000 SP4 q35 base (`E:\xhci98-vm\p49\whpx\s2k`, QMP 48610), a Bulk-Only stick (F:) and a UAS disk (E:) on `qemu-xhci` at power-on. Package: this tree's x86 release kit (built at `ebe3e3b81`; `xhci98.sys` `50bdb9d4...`, `xhciuas.sys` `595ad86d...`), with the night's batch files (`E:\xhci98-scratch\w49\whpx\k2k`).

**The kit's devcon fault.** It is the kit's, and it is already out of this kit: the `devcon.exe` of the earlier staging kit does not run on 2000 (48.33's "silent run"), and the night's kit replaced it with `devcycle.exe` (`src\stor98\tests\devcycle`, `740e76d8...`: SetupAPI's `DIF_PROPERTYCHANGE` with `DICS_DISABLE`, the call Device Manager's Disable makes), which runs on 2000. Nothing in the driver was touched.

Install `INSTALL.BAT /console /yes /nopause /nostor98` (one Found New Hardware wizard, for "xHCI98 USB Attached SCSI Storage", walked with Next and Finish): "xHCI98 USB 3 Controller (Red Hat QEMU xHCI): xhci98 3.4.3.0, started", "xHCI98 USB Attached SCSI Storage: xhciuas 3.4.3.0, started", "xHCI98 USB 3 Root Hub: xhci98 3.4.3.0, started", exit 3; shut down, cold start. The value read in XHCITR's dump before each set.

| Reading | Staging 0 (`stage.switch=00000000`, `stage.pool.slabs=00000000`) | Staging 1 (`stage.switch=00000001`, `stage.pool.slabs=00000020`, `stage.pool.short=00000000`, `stage.windows=00000004`) |
|---|---|---|
| copy-compare, 3 rounds of 24 MiB on E: and F: | 6 of 6 PASS | 6 of 6 PASS |
| `ID.BAT`: the disable with nothing open | `ok (0)` in 2.1 s, nothing listed 5 s and 20 s on; enable `ok (0)`, the mouse, the stick, the UAS disk and the root hub back | `ok (0)` in 2.4 s, the same |
| `DO.BAT E`: a 24 MiB copy to E: and back in a loop, the disable asked once 10 s in | asked 13:55:14.46, `ok (0)` at 13:55:19.98, and **it landed**: round 6 of the loop ended PASS at 13:55:19.70, rounds 7 to 12 "The system cannot find the drive specified" from 13:55:19.90; the stick not listed at any of the 12 looks. It landed in the gap after a round (the compare on C: and the delete), not inside a copy: no copy was cut off and every round that ran compared equal. Enable `ok (0)`, everything back | asked 14:08:13.80, `ok (0)` at 14:08:19.20, and it did **not** land: the stick listed at all 12 looks (to 14:09:33), the loop's 12 rounds all PASS; refused silently and dropped, as in the night's readings |
| `DH.BAT E 3000 20`: 3000 copies of a 1 MiB file back to back (110 s and 107 s of writing), the disable asked every 7 s | tries 1 to 14 (13:57:00 to 13:58:34) each `ok (0)` with the stick still listed; the writer ended at 13:58:35.25 with no copy failed; try 15 (13:58:38.53) landed; enable `ok (0)`; `CHV E pass 40 fail 0 missing 0 of 40`; copy-compare 3 of 3 PASS | tries 1 to 14 (to 14:11:38.48) refused the same way; the writer ended at 14:11:38.45, no copy failed; try 15 (14:11:43.55) landed; `CHV E pass 40 fail 0 missing 0 of 40`; copy-compare 3 of 3 PASS |

Nothing new in `setupapi.log` after either reading. No hang, no STOP, no device lost.

**What this adds to the night's reading.** Thirty more tries (28 while the stick was being written, 2 into a loop holding a file open), and none landed inside a copy. One of the two held-open loops was disabled, but between two rounds, where the loop has nothing open on the stick (48.33's first reading at 0 was the same case); the other refused the request, as both of the night's did. So on 2000 a disable is carried out only when nothing has the stick's volume in use at that instant, at either staging value, and what it does to a transfer in flight is still unread by this route: Windows 2000 does not let it happen. That one landed at 0 and one did not at 1 is the loop's phase when the request came (one run each), not something these two runs can lay at the value. The surprise removal stays the reading of transfers cut off (48.33 at 0, the night's at 1).

(Coordinator's note at the fold, 2026-11-11: read the paragraph above as bounded to what was run - one 2000 SP4 guest, one processor, one package, the test tool's property-change route. It shows that these requests did not interrupt a copy, not that 2000 cannot deliver such a stop; the wider check, a stop that is not a removal with transfers in flight, stays owed.)

Evidence: `E:\xhci98-vm\p49\whpx\s2k\` (`serial-a.log` the install, `serial-b.log` staging 0, `serial-c.log` staging 1, `g.log`, `*.png`). QEMU processes 98848, 78904, 94232 (`E:\xhci98-vm\p49\whpx\pids.txt`), each shut down from inside and quit by QMP at the halt.

### 98 SE: playback beside copies, with the C:-only control at both values (once each)

98 SE (`E:\xhci98-vm\p49\whpx\s98`, QMP 48611), one processor, 512 MB, TCG, a fresh overlay of the q35 base, no USB device at power-on. The kit is the night's (`E:\xhci98-scratch\w49\whpx\k98`) with this tree's package and the `3.4.3.0` `-free` download's `USBDRV\`, and one change the plan's review asked for: `AU1.BAT`, `LP1.BAT` and `UA.BAT` stamp the time of every play and every copy round, and `UA.BAT` says whether the copy loop was still running when the last play ended, so "beside the copies" is shown and not assumed. Install as the night's (`XHCIINST.EXE /dir 98-me /console /yes /nopause /nostor98 /storage /wdmfree:stage=audio/uac1`, exit 3), cold start, `usb-uas` with a `scsi-hd` hot-added (`qom-set ... attached true`; the wizard for "xHCI98 USB Attached SCSI Storage" walked; E:), `usb-audio` hot-added ("UAC1 USB audio (experiment)", the same "Insert Disk ... 'UAC1 experimental build folder'" question as the night's, answered with `C:\XHCI98\USBDRV\FREE\audio\uac1\w98se`). Each set below after a cold start with both devices hot-added again. A play is WAVCHK's (`/name USB`, "QEMU USB Audio" found at once) of the 4 s 48 kHz stereo tone, and passes when it ends within 18 s; a copy round is 24 MiB to the drive and back and `fc /b`.

**The host was loaded throughout**, by the other workers' guests and builds; no guest of mine but this one ran during the readings. Host processor load is `\Processor(_Total)\% Processor Time` on development host A (32 logical processors), sampled every 5 s (`s98\hostload.csv`), over each set of plays.

| Staging | Run | Copies | Plays (8 of 4 s) | Loop still running after the last play | Host load over the plays (mean; min to max) |
|---|---|---|---|---|---|
| 0 (`"XhciDmaStaging"=dword:00000000`) | audio alone | - | 8 of 8 PASS, 4015 to 4162 ms | - | 84 %; 77 to 94 |
| 0 | copies to the UAS disk E: and audio | 20 of 20 PASS (14:38:56 to about 14:52:55) | 7 of 8 PASS (8454, 8667, 17770, 4464, 9933, 4292, 4406 ms); 1 not done after 23.5 s, at 51 % | yes (plays 14:38:57 to 14:41:35) | 67 %; 57 to 74 |
| 0 | control: the same copies on C: alone and audio | 20 of 20 PASS (14:56:45 to about 15:25) | 4 of 8 PASS (4318, 17439, 4561, 4349 ms); 4 not done after 18.7 to 27.1 s, at 4 to 30 % | yes (plays 14:56:45 to 15:00:55) | 49 %; 39 to 61 |
| 1 (`stage.switch=00000001`, `stage.pool.slabs=00000020`, `stage.pool.short=00000000`, `stage.windows=00000004`) | audio alone | - | 8 of 8 PASS, 4007 to 4074 ms | - | 42 %; 36 to 51 |
| 1 | copies to E: and audio | 20 of 20 PASS (15:33:49 to about 15:47) | 6 of 8 PASS (8537, 8114, 5013, 4364, 4597, 4323 ms); 2 not done after 18.4 and 18.6 s, at 0 and 5 % | yes (plays 15:33:50 to 15:36:15) | 45 %; 35 to 56 |
| 1 | control: the same copies on C: alone and audio | 20 of 20 PASS (15:47:41 to about 16:12) | 4 of 8 PASS (4380, 4216, 4835, 4293 ms); 4 not done after 18.0 to 43.9 s, at 3 to 45 % | yes (plays 15:47:42 to 15:52:56) | 50 %; 40 to 68 |

What these runs show, and no more. Correctness: every copy round compared equal at both values, with and without the audio beside it; no device was lost; no hang. Alone, every play kept time at both values (also at 84 % host load). With a copy loop beside it, plays fall behind at both values, and they fall behind as much or more when the loop does not touch the USB disk at all (the C:-only control, read now at 1 as well as at 0). So in these runs the late plays go with the copy loop in a DOS box on the one emulated processor, not with the UAS disk's transfers and not with the staging value. The counts are one run each on a loaded host and are not a measure of either value: no conclusion about the driver's real-time behaviour is drawn from them, and the emulated processor is no measure of a real one. What is left of the task is the reading on a real processor.

Two things met on the way, neither the task's subject:

- In the Windows session in which the audio device had just been installed (the first start after the install, before any cold start), WAVCHK's first play did not return in the six minutes it was left (`b-10.png`, `b-11.png`); Windows then shut down normally. After a cold start every reading above ran. Not looked into (the night's first run in that same kind of session was lost too, to its batch file); every reading here and the night's is after a cold start.
- A DOS window that has finished and is left open ("Finished - UA") keeps COM1, and the next run's report then stops at "Write fault error writing device COM1" (`c-4.png`); closing the finished window and answering Retry delivered it. The later runs are started as `command /c D:\UA.BAT ...`, whose window closes. A harness matter.

Evidence: `E:\xhci98-vm\p49\whpx\s98\` (`serial-a.log` the install, `serial-c.log` staging 0, `serial-d.log` staging 1, `hostload.csv`, `g.log`, `*.png`); `E:\xhci98-scratch\w49\whpx\cmd\ua.py` and `load.py` make the table's figures from them. QEMU processes 105404, 93408, 113004, 115180 (`E:\xhci98-vm\p49\whpx\pids.txt`), each shut down from inside.

## 49.4 (the copy verdict)

The merge review's point: the legs' copy checks gave PASS on fc's "no differences" alone (`LEGNT.CMD` `:dcopy` captured copy's and fc's exit codes and ignored them; `stick-copy` in the same script and `LEG9X.BAT`'s `stick-copy` had the same shape), so a copy that failed over a destination already holding the same bytes passed. Commit `52e5404e2` (reviewed: plan round with two findings folded, the 9x folder guard and a held-open test; working-tree round approve): `leg\XHCOPY.CMD`, new, on the NT leg's CD, removes a destination from before (attributes cleared) and needs it gone, fails a folder of that name without touching it, then needs copy exit 0, fc exit 0 and "no differences"; all three NT copy checks go through it. `LEG9X.BAT`: COMMAND.COM's exit codes are not relied on; it removes first, needs the file gone, then there after the copy, and fc's "no differences". The 9x script was read, not run in a guest this time.

Self-test (`selftest.ps1`, real cmd.exe on temporary files): a copy exit 1 with an fc log saying "no differences" fails; a read-only or held-open destination from before is removed; a folder (empty or not) fails and is untouched; a missing source fails on copy's exit code. With the old judgment put back (the exit-code lines and the removal taken out) the section gives 6 FAILs. Gated x86 release build of `52e5404e2` (`XHCI98_SELFTEST_SKIP_OK=1`): BUILD + GATES PASSED, "VM matrix verdict self-tests" ran, "selftest: 582 checks, all passed" (`E:\xhci98-scratch\w49\whpx\build-release-52e54.log`).

Legs under WHPX on that script (XHCOPY.CMD `c0b7eba4...`, LEGNT.CMD `a506166a...`), one guest at a time, slot 0:

| Leg | Package | Stick | Result | Copy checks |
|---|---|---|---|---|
| final2-w10x86-console | kit2 (52e5404e2, xhci98.sys `c8a12844...`) | the standard one | PASS 355.3 s | stick-copy, stick-copy-enable, stick-copy-rescan: "PASS on E:"; remove done |
| final2-w10x64-console | 3.4.3.0 download (signed) | the standard one | PASS 318.9 s | the same three "PASS on E:"; remove done |
| fail-w10x86-console | kit2 | `stk\stick-full.qcow2` (`96a3ae3d...`): RAND.BIN already there, the leg's bytes, read-only; every free cluster marked bad | FAIL 344.5 s | stick-copy "PASS on E:, an earlier RAND.BIN removed" (the old script's copy /y over the read-only file would have failed and fc passed it); stick-copy-enable and stick-copy-rescan "FAIL on E: copy exit 1, fc exit 2" ("There is not enough space on the disk.", "FC: cannot open E:\R2.BIN") |

The failure leg's verdict names exactly those two checks; everything else in it passed. A FAIL with nothing removed also sends "LOG-MISSING remove" (no removal log): noise, harmless.

Evidence: `E:\xhci98-vm\p49\whpx\rm\runs\{final2-w10x86-console,final2-w10x64-console,fail-w10x86-console}` (serial-b*.log, result.json), logs `E:\xhci98-scratch\w49\whpx\logs\`; the stick made by `stk\mkstick.py`; the review notes in the worker's scratch folder, the review notes in the worker's scratch folder. QEMU processes in `rm\pids.txt`, each ended.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** the change in one sentence: the
legs' copy checks, found at the merge review to pass after a failed write onto matching old data, now need the copy's
and the comparison's results and remove a destination from before; the 98 SE and ME form was read in a 98 SE and an ME
guest afterwards ("49.4 (the leftovers)"), where a comparison that differs (no partial file was ever left) and a
destination that cannot be removed were not met.

## 49.9 (day) The change at `a0186fe7b` read on one and on two processors (2026-11-11, worker c31)

Worker c31, branch `w49-c31` at `a0186fe7b` (three commits on `d7d8769b7`:
`9dd8773d2`, `bd4014ff7`, `a0186fe7b`). Development host A, QEMU
`asm4242-20261110-docks` build, TCG, headless. The worker was stopped at
about 11:20 with no run sheet written; a replacement read every claimed leg
back from the logs on disk (first table), then went on with the
two-processor readings. Evidence root `E:\xhci98-vm\p49\c31\` (`d2k`,
`dxp`, `d98`, `smp`; in each `dbt-<tag>.txt` is the debugcon trace and
`srt-<tag>.txt` the COM1 log, every line stamped with the host's clock;
`g.log` the commands typed), scripts in `E:\xhci98-scratch\w49\c31\b\` and
`p49\c31\kit\`.

### Builds

- Gated builds at the tip, `XHCI98_SELFTEST_SKIP_OK=1`: amd64 `release`
  BUILD + GATES PASSED (`build-tip-amd64.log`; `out\gated-amd64-release.txt`
  commit `a0186fe7b`, `dirty=0`, `xhci98.sys` `8c27fe00...`); x86 `all`
  BUILD + GATES PASSED (`build-tip2-x86.log`; `out\gated-x86-release.txt`
  and `-qemu.txt`, `dirty=0`; `xhci98.sys` release `4106135e...`, qemu
  `6dd70d80...`). Builds are not bit-reproducible, so these hashes differ
  from the night's builds of the same commit.
- The unfixed base for the reproduction: x86 `qemu` at `d7d8769b7`, BUILD +
  GATES PASSED (`build-base2-x86.log`), `xhci98.sys` `bbc3f322...`
  (`E:\xhci98-scratch\w49\c31\base\`).
- XHCIINST refuses a qemu-flavour folder, so every guest installs the
  release family kit of the tip (`p49\c31\kitr`, exit 3) and then has the
  qemu-flavour `xhci98.sys` copied over the installed file (`QS.BAT`), a
  cold start, and `fc /b` of the installed file against the kit's copy
  (`V.BAT`, later `FH.BAT`) before any leg.

### One processor: the legs on the tip's qemu driver (`6dd70d80...`), read back from the logs

Devices as in the night's section: `usb-storage` serial `W49STK01` on port 1,
`usb-tablet` on port 2 (location 6), `usb-uas` with a `scsi-hd` on port 4.
Fault `XhciQemuInject` `0n050000` (HCH). Problem codes by `PROB.EXE` (a
test aid: SetupDi + `CM_Get_DevNode_Status`, to COM1).

| System | Leg | Result | Deciding lines |
|---|---|---|---|
| 2000 SP4 (`d2k`, tag d; `fc`: no differences against the tip's qemu file, `serial-c.log`) | recovery `01050000`, both volumes mounted and idle | **pass**: 11 devices, 0 problem codes at 45 s; one copy-compare round on each drive passes | `dbt-d.txt` 513-638: "waiting for the old PDOs' REMOVE" location 1, "REMOVE came" after 1 tick, "device PDOs created" after it; location 4 the same after 6 ticks |
| 2000 SP4 (d) | recovery `02050000` with copy loops on both drives | **pass**: 0 problem codes at 45 s and 75 s; the rounds across the fault fail (E: 10 of 20, F: 15 of 20, by design), a round after it passes on each | `dbt-d.txt` 853-905: waits of 6 and 12 ticks, each new PDO after its "REMOVE came" |
| 2000 SP4 (d) | the stick unplugged and plugged with no recovery (twice) | **pass**: no wait line, the PDO 0.3 s after the plug | `dbt-d.txt` 1104, 1192; `host-d.txt` |
| 2000 SP4 (d) | recovery `05050000` with `HOLD.EXE` keeping `\\.\PhysicalDrive2` and `3` open 90 s (the disks' REMOVE pending); the stick unplugged 5 s into the wait, another stick plugged 4 s later | **pass**: "port emptied while waiting for the old REMOVE, location=00000001" 0.17 s after the `device_del`; the other stick's PDO 0.3 s after its plug; the untouched port 4 "old PDOs' REMOVE not come, going on" 30.2 s after its wait began, its new PDO right after, and 30 s later 11 devices, 0 problem codes, both drive letters present, with the old disk still held open (`HOLD closed` 56 s after the timeout) | `dbt-d.txt` 2227-2479, `l5-d3.log`, `srt-d.txt` 454-607 |
| 2000 SP4 (d) | recovery `04050000`, the fourth in ten minutes | not a leg: the recovery window latched the controller failed (by design); both waits ran out at 30 s; a controller disable and enable brought everything back (it does not reset the window) | `dbt-d.txt` 1717-2027 |
| 2000 SP4 (d to e) | hibernation, volumes mounted and idle | **pass**: the resume keep kept the UAS disk and the tablet, the stick dropped (why 08), wait 2 ticks, new PDO after; 11 devices, 0 problem codes, both drives present, a copy-compare round passes on each | `dbt-e.txt` 76-118, `srt-e.txt` |
| 2000 SP4 (e to f) | hibernation with copy loops on both drives | **pass**: wait 7 ticks, new PDO after; 0 problem codes; the UAS disk's loop 40 of 40 across the resume, the stick's loop fails from the resume on (its volume went with the old PDO, as before the change); a round after it passes on each | `dbt-f.txt` 74-112, `srt-f.txt` |
| XP SP3, two vCPUs (`dxp`; the tip's qemu file in place: `find /c` 1 against it, `serial-c.log`) | recovery `01050000`, volumes mounted | **pass**: no wait line (the change is off on XP), 10 devices, 0 problem codes, a copy-compare round passes on each | `dbt-c.txt`, `srt-c.txt` |
| XP SP3, two vCPUs (e to f; needs `-vga cirrus` and `ICH9-LPC.disable_s4=0`) | hibernation, volumes mounted | **pass**: no wait line; 11 devices, 0 problem codes; a round passes on each | `dbt-f.txt`, `serial-f.log` |
| 98 SE (`d98`, tag c; `find /c` 1 against the tip's qemu file) | recovery `01050000` with the UAS disk's volume E: open in Explorer | **pass**: no wait line (off on 98 SE); the UAS disk back as a new PDO 0.2 s after the fault; a file copied to E: before the fault compares with `fc /b`: no differences | `dbt-c.txt`, `c-10.png` |

All ten rows have their logs or screens on disk; none needed a rerun.

One reading of interest from the held-port row: on one processor a new PDO
created with the old disk's REMOVE still pending for another 56 s started
with no problem code. So a pending old REMOVE alone does not give Code 31
there.

### Two processors: the vehicle

The Windows 2000 SMP guest of `build-and-test.md` ("Windows 2000 SMP Stress
VM"): `-machine pc`, `-accel tcg,thread=multi`, `-smp 2`, `-cpu pentium3`,
512 MB, ACPI Multiprocessor HAL, Driver Verifier as its base image has it,
`usb-ehci` and `qemu-xhci`; an overlay `p49\c31\smp\disk.qcow2` of
`vm\t36pv\w2ksmp-base.qcow2` (read-only backing). `kit\lsmp.ps1`; command
lines `smp\cmdline-<tag>.txt`. The tip's release kit installed (exit 3,
`serial-a.log`), then the qemu-flavour file under test copied in.
`NUMBER_OF_PROCESSORS=2` read in the guest. Devices as above; here the
UAS disk's volume is D: and the stick's F:. This is run-35's setup (its 5
of 5 Code 31 "with the volume mounted at the fault" at `efc6fe1`).

### Two processors, the build WITHOUT the change (`d7d8769b7`, qemu `bbc3f322...`): Code 31 not reproduced

`fc /b` of the installed file against the base file: no differences (tags b
and f). R1 to R9 and H1 are the stopped worker's (read back from `dbt-b`,
`dbt-c`, `dbt-d`, `dbt-e`, `srt-*`), R10 to R12 and H2 the replacement's
(`dbt-f`, `dbt-g`).

| Try | Recipe | Location 1's new PDO created | First REMOVE of an old PDO | Result at the listing |
|---|---|---|---|---|
| R1 (b) | recovery `01050000`, `dir` of D: and F: just before | +1.1 s after the fault | +1.9 s | 11 devices, 0 problem codes |
| R2 (b) | `02050000`; a `cmd` window with its directory on each volume, copy loops on both | +1.4 s | +3.1 s | 0 problem codes |
| R3 (b) | `03050000`; Explorer windows on D: and F: | +1.5 s | +3.8 s | 0 problem codes |
| R7 (c) | `07050000`; as R2 | +3.3 s | +9.8 s | 12 devices, 0 problem codes |
| R8 (c) | `08050000`; `dir` of each | +2.3 s | +2.7 s | 0 problem codes |
| R9 (c) | `09050000`; Explorer on D:, copy loop on F: | +3.6 s | +6.4 s | 0 problem codes |
| R10 (f) | `01050000`; `dir` of each | +2.1 s | +4.1 s | 12 devices, 0 problem codes |
| R11 (f) | `02050000`; as R2 | +4.3 s | +5.1 s | 0 problem codes |
| R12 (f) | `03050000`; as R9 | +1.9 s | +2.2 s | 0 problem codes |
| H1 (d to e) | hibernation, `dir` of each first | 14.8 s into the resume | 1.8 s after that PDO | 12 devices, 0 problem codes |
| H2 (f to g) | hibernation with copy loops on both, Explorer on D:, the held `cmd` windows of R11 | 25.6 s into the resume | 9.1 s after that PDO | 12 devices, 0 problem codes |

0 of 9 recoveries and 0 of 2 hibernations gave Code 31. In all eleven the
stick's new PDO was created before any old PDO's REMOVE arrived (the
order the earlier runs blamed), and it started every time. R4, R5 (the
fourth recovery inside the ten-minute window latched the controller
failed) and R6 (the injection did not fire) are not tries. The trace of the
base build does not name the PDO a REMOVE is for; "first REMOVE" is the
first of any old PDO after the fault.

**Stated plainly: the fault 49.9 is about was not reproduced on this
vehicle, on one processor or two, at the base of the change.** What made
run-35's and run-36's guests show it and this one not is not known (the
driver has changed much since `efc6fe1`; nothing here isolates it).

### Two processors, the tip (`a0186fe7b`, qemu `6dd70d80...`): the recoveries

`fc /b` against the tip's qemu file: no differences; `NP=2` (tag i).

| Try | Recipe | The wait at location 1 (the stick) | Location 4 (UAS) | Result |
|---|---|---|---|---|
| F1 (i) | recovery `01050000`, `dir` of each first | begun +0.9 s, "REMOVE came" after 27 ticks (2.7 s), the new PDO 1.9 s after that | 38 ticks, PDO after | **pass**: 12 devices, 0 problem codes (second listing, 40 s later; the first came while port 4 was still being enumerated and listed 9) |
| F2 (i) | `02050000`; held `cmd` windows and copy loops on both | 207 ticks (20.7 s: the SURPRISE_REMOVAL itself came 15.1 s after the fault under the copy load), the new PDO 3.3 s after | 67 ticks | **pass**: 12 devices, 0 problem codes |
| F3 (i) | `03050000`; Explorer on D:, copy loop on F: | 50 ticks, PDO 2.1 s after | 93 ticks | **pass**: 12 devices, 0 problem codes |

In every wait the new PDO came only after "old PDOs' REMOVE came". F2's
20.7 s is two thirds of the 30 s bound; host A was running other workers'
guests at the time, so the figure is one loaded reading, not a measure.

### Two processors, the tip: a hibernation after which Windows stopped enumerating (FH1, seen once)

Guest i after F1 to F3 (the held `cmd` windows, the Explorer window, and
the "Unsafe Removal of Device" and "Delayed Write Failed" boxes those
recoveries left on the desktop), then copy loops on both drives and
`SetSuspendState`; resumed as tag j (`dbt-j.txt`, `srt-j.txt`, `j-*.png`).

- 13:14:27 the resume keep kept the UAS disk and the tablet and dropped the
  stick (why 08), as on every resume here.
- 13:14:53.184 "waiting for the old PDOs' REMOVE, location=00000001";
  SURPRISE_REMOVAL; 13:15:02.140 REMOVE; "old PDOs' REMOVE came,
  location/ticks=00010055" (8.5 s).
- 13:15:02.545 QUERY_DEVICE_RELATIONS at the root hub FDO (it reached the
  root hub PDO at .697, and two device PDOs saw minor 07 at .748); the
  stick was reset and addressed meanwhile; 13:15:06.041 "device PDOs
  created, port/count=00010001" (the code asks for the root hub's relations
  again right after that line, unconditionally: `hcdInvalidateFor`).
- After that: no QUERY_DEVICE_RELATIONS at the root hub for the ten
  minutes the guest was left up. The listing at 13:15:32 has 10 devices, 0
  problem codes: the stick's USB devnode and its disk are absent. A
  `usb-kbd` hot-added at 13:21:17 was reset, addressed and given a PDO, and
  PnP did not ask for it either. The ACPI power button (set to Power Off)
  did nothing in 3 minutes. The UAS disk's copy loop went on writing
  (`uas.qcow2` kept changing) and the pointer and dialogs worked, so the
  machine and the driver's transfers were alive; Windows' enumeration was
  not.
- The desktop was locked after the resume (this image asks for the
  password, which is not blank), so nothing more could be read inside the
  guest: no later listing, no look at which dialogs were up under the lock.
  Quit by QMP.

The same recipe on the build without the change (H2: after R10 to R12, the
same windows and boxes, the same lock) enumerated the stick again, and so
did H1. `lessons.md` records that a modal dialog can stop Windows 2000's
PnP install queue ("a modal dialog blocks the Windows 2000 PnP install
queue"); FH1 had such boxes up and under a lock, and so had H2. With the
change the stick's new PDO needs one more relations query after the REMOVE
(here 3.5 s after it); without it the PDO is already in the first answer.
Whether that later query is what met the stall is not shown by one try.
The matched trials below were run to find out.

### Two processors: matched hibernation trials, the tip and the base in turn

Reviewed as a plan first and revised: every trial starts from an identical
powered-off copy of the overlay (`smp\snapF.qcow2` with the tip's qemu
file, `smp\snapB.qcow2` with the base's; the two differ by that file and
one boot), a cold start, 150 s, then `FH.BAT` in the guest (`xfer\FH.BAT`):
the installed file's size and `fc /b` against the expected file, the
recipe, a listing, `SetSuspendState`; after the resume eight readings 30 s
apart, each stamped at its begin and end (`PROB.EXE` listing, each drive's
`TESTDATA.BIN` visible), after the sixth a request to enumerate the device
tree again (`RESCAN.EXE`, a test aid: `CM_Reenumerate_DevNode` at the
root), then the guest's own shut down. Host side (`b\trial.sh`): screens at
+60 s and +200 s, a `usb-kbd` hot-added 120 s after the resume's start as a
probe of whether Windows still enumerates (installed once beforehand), a
QMP quit if the guest has not shut down 540 s after the resume. "Prompt for
password when computer goes off standby" was unticked in the overlay
before the copies, so the desktop is not locked after a resume. Pass: at
the fifth reading the stick's USB devnode and its disk present with
problem code 0 and its test file visible.

"Load" is an Explorer window on D: and copy loops (`SL.BAT`, 40 rounds) on
both drives started 15 s before the hibernation; "idle" is a `dir` of each.

| Trial | Driver | Recipe | The stick after the resume | Readings | Probe keyboard | End |
|---|---|---|---|---|---|---|
| T1 (n2 to o) | tip | load | wait 28 ticks, "REMOVE came", PDO 0.7 s later, START 2.7 s after that | 8 of 8, 13 devices (12 at the first, before the probe), 0 problem codes, both test files visible; the rescan request returned | PDO, START 0.7 s later | **pass**; shut down by itself |
| T2 (p to q) | base | load | new PDO 2.6 s after the SURPRISE_REMOVAL and 10.2 s before the REMOVE; START 1.4 s after the REMOVE; configured | **the first reading's listing never returned** (begun 14:10:19, 16 s after the START) | PDO created 14:11:13, never queried | **stall**: no PnP request reached xhci98 after 14:10:05 other than the listing's own; closing the "Delayed Write Failed" and "Unsafe Removal of Device" boxes by hand (14:13:44, 14:14:06) changed nothing in the next minute; the UAS disk's copy loop went on writing; no shut down, QMP quit at 540 s |
| T3 (r to s) | tip | load | wait 94 ticks, "REMOVE came" 14:23:40, PDO created 14:23:41.797, **never queried** | the first reading's listing never returned (begun 14:23:59) | PDO created 14:24:45, never queried | **stall**, FH1's shape; QMP quit at 540 s |
| T4 (t to u) | base | load | new PDO 1.8 s after the SURPRISE_REMOVAL, 9.4 s before the REMOVE; START 2.4 s after it | 8 of 8, 0 problem codes, both files visible; rescan returned | PDO, START 1.1 s later | **pass**; shut down by itself |
| T5 (v to w) | tip | idle | wait 14 ticks, PDO 0.7 s after "REMOVE came", START 0.4 s later | 8 of 8, 0 problem codes, both files visible; rescan returned | PDO, START 0.2 s later | **pass**; shut down by itself |
| T6 (x to y) | base | idle | new PDO 0.7 s after the SURPRISE_REMOVAL, 1.6 s before the REMOVE; START 0.6 s after it | 8 of 8, 0 problem codes, both files visible; rescan returned | PDO, START 0.1 s later | **pass**; shut down by itself |

In each trial the first serial lines confirm the driver file: 435,557 bytes
and `fc` identical to the tip's file in T1, T3, T5; 434,277 bytes and
identical to the base's in T2, T4, T6.

### Two processors: is it the UAS disk under load? (U1 to U4)

The same trial with the copy loop on the stick alone (`xfer\FU.BAT`: no
loop and no Explorer window on the UAS disk D:), then with no UAS device in
the machine at all (`kit\lsmp2.ps1 -NoUas`). Reviewed as a plan first.

| Trial | Driver | Machine | The stick after the resume | Readings, probe | End |
|---|---|---|---|---|---|
| U1 (ua to ub) | tip | UAS disk attached, idle | wait 46 ticks, PDO 1.2 s after "REMOVE came", START 1.0 s later | 8 of 8, 0 problem codes, both files visible; keyboard started | **pass** |
| U2 (uc to ud) | base | the same | new PDO 6.0 s before the REMOVE, START 0.8 s after it | 8 of 8; keyboard started | **pass** |
| U3 (ue to uf) | tip | no UAS device | wait 78 ticks, PDO 0.3 s after "REMOVE came"; its START came 25 s after the PDO (15:30:12 to 15:30:37) | 8 of 8, 10 devices, 0 problem codes, F: visible; keyboard started | **pass** (the 25 s is noted; every other START here came within 3 s) |
| U4 (ug to uh) | base | no UAS device | new PDO 4.2 s before the REMOVE, START 0.9 s after it | 8 of 8; keyboard started | **pass** |

### The hibernation tally so far

| | Copy load on the UAS disk across the resume | No load on the UAS disk (idle, load on the stick only, or no UAS device) |
|---|---|---|
| tip `a0186fe7b` | FH1 **stall**, T1 pass, T3 **stall** | T5, U1, U3 pass |
| base `d7d8769b7` | H2 pass, T2 **stall**, T4 pass | H1, T6, U2, U4 pass |

3 stalls in 6 tries with the UAS disk under load (2 of 3 on the tip, 1 of 3
on the base), none in 7 without. That is consistent with the stall needing
I/O on the UAS disk across the resume, and not demonstrated by it (thirteen
tries; by chance alone about 1 in 14). The stall is seen with and without
the change. Its consequence differs: in the base's stall the stick had
already been queried and started when Windows stopped enumerating; in both
of the tip's stalls the stick's new PDO, created after the relations query
that followed the REMOVE, was never queried. H1, H2 and FH1 are the earlier
tries on the working overlay (locked desktop, FH1 and H2 after three
recoveries), not matched trials.

### Two processors: the UAS disk under load with no stick to drop (V1 to V3)

The guest started without the Bulk-Only stick (`kit\lsmp3.ps1 -NoStick`), so
a resume drops nothing: no SURPRISE_REMOVAL, no REMOVE, no new PDO, and no
REMOVE wait on the tip. Copy loop and Explorer window on the UAS disk
(`xfer\FV.BAT`); the probe keyboard about 60 s after the resume's start,
with the UAS disk's bytes written sampled twice 5 s apart just before it.
Reviewed as a plan first.

| Trial | Driver | UAS writes just before the probe | Probe keyboard | Readings | End |
|---|---|---|---|---|---|
| V1 (va to vb) | base | 61.3 MB then 73.1 MB (moving) | PDO, START 0.1 s later | 8 of 8, 10 devices, 0 problem codes | **pass** |
| V2 (vc to vd) | tip | 60.55 MB then 60.60 MB (nearly still for those 5 s; 190 MB a minute later) | PDO, START 2.3 s later | 8 of 8 | **pass** |
| V3 (ve to vf) | base | 100.8 MB then 105.4 MB | PDO, START 1.7 s later | 8 of 8 | **pass** |

No stall in three. By the plan's rule nothing is concluded from that: the
stall may need the dropped stick's removal as well as the load on the UAS
disk, or three tries may simply have missed it.

### Verdict (day)

- **Evidence of the claimed legs: all on disk**, none rerun (first table).
- **Code 31 was not reproduced at the base of the change**: one processor 0
  of 4 (night), two processors 0 of 9 recoveries and 0 of 5 hibernations
  (H1, H2, T2's start before its stall, T4, T6; U2 and U4 with the stick
  loaded add two more). In every one the stick's new PDO was created before
  the old PDO's REMOVE and started all the same. So the change is **not
  shown to cure a reproduced fault**.
- **The change waits as designed** on two processors too: recoveries F1 to
  F3 and hibernations T1, T5, U1, U3 all show "waiting for the old PDOs'
  REMOVE", then "REMOVE came" (14 to 207 ticks), the new PDO only after it,
  the devices started with no problem code. XP and 98 SE take no wait.
  The held-port bound (30.2 s) and the unplug during the wait were read on
  one processor.
- **Found: Windows 2000 on two processors can stop enumerating after a
  resume from hibernation** when copies were running on both the UAS disk
  (kept by the resume keep) and the Bulk-Only stick (dropped): 3 of 6 such
  tries, with the change (FH1, T3) and without it (T2); 0 of 7 with no load
  on the UAS disk; 0 of 3 with the UAS disk loaded and no stick. After it
  nothing new is enumerated (a hot-added keyboard gets a PDO from the
  driver and no query from Windows), a device listing from user mode never
  returns, the guest does not shut down; the UAS disk's I/O and the
  pointer carry on. Closing the boxes on the desktop does not free it. The
  cause is not known. xhci98's trace shows its own part done each time
  (PDO created, relations invalidated; the last PnP requests it saw were
  completed), which does not clear the project's drivers: the UAS function
  driver and the resume keep are candidates to look at (a queued UAS
  request has no timeout until it is given a slot; the wake worker's
  synchronous request has none), named as places to instrument, not as
  findings.
- **What the change does to that stall's outcome**: on the base the stick
  had been queried and started before Windows stopped (T2); on the tip the
  stick's new PDO, which the wait puts after the relations query that
  follows the REMOVE, was never queried (FH1, T3). Two tries against one;
  whether the change makes the stall likelier (2 of 3 against 1 of 3) is
  not resolved by counts this small.
- **Readiness**: the change's intended waiting was observed; Code 31 was not
  reproduced on the base, so no cure was demonstrated; a PnP stall also
  occurs without the change; in the tip's stalls the replacement stick PDO
  stayed unqueried, in the base's it had started. A regression in timing or
  consequence is unresolved. Merging needs the coordinator's explicit
  acceptance of that; the worker's own recommendation is to leave
  `w49-c31` unmerged until Code 31 is reproduced (real hardware, or
  run-35's and run-36's guests at their commits) and the change read
  against it, and the stall is understood.

### Process notes (worker c31, day)

- The stopped worker's `STATE.md` lagged its work (R6 to R9 and H1 were on
  disk and not in it) and its `pids.txt` logged guest e stopped at 11:15:09
  while it ran on until the replacement shut it down at 12:31.
- `g.py off` took "OFFSET" in a `PROB` line for `OFF.BAT`'s marker and quit
  guest g by QMP at 12:55:35 with no shut down (nothing lost: the next
  start booted clean). `b\off.sh` waits for QEMU to end instead.
- The SMP image locks the desktop after a resume and its password is not
  blank; the replacement did not look for the password. Unticking "Prompt
  for password when computer goes off standby" (Power Options, Advanced)
  removes the lock; it is in `snapF`/`snapB`.
- `fc /b` of two different 430 KB files piped to `find` did not end in
  eight minutes on this guest: compare only against the file expected.
- `ping -n` delays in a batch ran short on this guest more than once (a
  listing came 13 s after a 30 s ping began); the logs' host timestamps are
  the times to trust.
- A fourth recovery inside ten minutes latches the controller failed, and a
  controller disable and enable does not reset that count: restart the
  guest every three recoveries.
- Host A ran other workers' guests throughout; waits and delays here are
  loaded readings.

## 49.8 (continued) `usb-wdm-free`: the gate fix's 98 SE round under 98 SE's own UHCI; the q35 98 SE boot hang (2026-11-11, uac98)

usb-wdm-free branch `w49-uac` (fix `34f1896`, docs `8ad07a2`, `9c820be`),
read only; no tracked file changed anywhere. Plan reviewed and revised
before the legs (three rounds, the last with nothing material).

### The hang (legs A1, A2, B; development host A, one guest at a time)

Every leg: q35, TCG, `-cpu pentium3`, one vCPU, 512 MB, a fresh qcow2
overlay of the q35 `w98se` base (`ab1d4d65...`), the same firmware for
every leg (`-L` usb4-emu `vms\qemu-baseline\share`: SeaBIOS rel-1.17.0
`bios-256k.bin`), SeaBIOS's log on `isa-debugcon` 0x402, `info
registers` and a screendump every 30 s. Launcher
`E:\xhci98-scratch\w49\uac98\bin\launch.ps1`; each leg's `cmdline.txt`.

| Leg | Emulator | C: on | USB | Result |
|---|---|---|---|---|
| night `se1` (x2) | QEMU 11.1.0 (scoop, `v11.1.0-12130-ge470268ff4`) | ICH9 AHCI (`ide.0`) | `piix3-usb-uhci` | stopped behind the boot logo, `a000:8000`, CR0 `10` (night worker) |
| `a1` | QEMU 11.1.0 (scoop) | ICH9 AHCI | none | behind the boot logo from 30 s to 330 s: `SMM=1`, `CS=a000 EIP=00008000`, CR0 `00000010` at every sample (once `f000:ff53`, still `SMM=1`); eight samples 0.3 s apart identical (`EAX=000000b5 ESP=000ea194`, `HLT=0`); SeaBIOS log ends at "Booting from Hard Disk ... set VGA mode 13" |
| `a2` | usb4-emu build 11.1.2 (`v11.1.2-8-g76e89a5`, `q35-bases\bin\asm4242-20261110-docks`) | ICH9 AHCI | `piix3-usb-uhci` | boots: `SMM=0` at every sample, protected mode from 30 s, the UHCI's New Hardware wizard on the screen at 210 s |
| `s0` (vehicle B) | QEMU 11.1.0 (scoop) | `piix3-ide` on `pcie.0` 0x1e (no geometry override: QEMU's default 8322/16/63, SeaBIOS LBA 522/255/63, as on AHCI) | `piix3-usb-uhci` (`pcie.0` 0x1d) | boots: `SMM=0` at every sample; wizards for the UHCI ("Intel 82371SB PCI to USB Universal Host Controller", `USB.INF`), the PIIX IDE controller and the USB root hub, all from the disk; restart asked -> cold start -> desktop |

Reading: the hang needs neither the UHCI nor any USB device (a1). It
follows the emulator binary (11.1.0 hangs, the pinned 11.1.2 boots, same
firmware, same AHCI disk: a1 against a2) and, on 11.1.0, the disk path
(AHCI hangs, PIIX boots: a1 against s0, emulator and firmware the same).
With C: on AHCI, 98 SE has no AHCI driver, so every disk access goes
through SeaBIOS's 32-bit AHCI code, which SeaBIOS reaches from V86 mode
by an SMI (`EAX=b5` fits the SMI command SeaBIOS writes for that call;
inferred, the SeaBIOS source was not read in this leg). On QEMU 11.1.0
under TCG the boot stalls with the CPU in SMM: ten of eleven 30 s samples
at the SMM entry `a000:8000` (one at `f000:ff53`, still `SMM=1`), eight
samples 0.3 s apart identical; whether it executes nothing there or loops
through SMM entries was not determined. The lessons entry on 11.1.0-rc2
(a boot-attached USB HID on xHCI, `SMM=1`, `CS=a000`, CR0 `10`) looks
alike; that it is the same failure is not shown. Proven: the stall and
the three comparisons above. Inferred: that the SMI path is SeaBIOS's
call32 from V86 for its AHCI INT 13h. Not checked: which QEMU change
between 11.1.0 and 11.1.2 is involved.

Other facts: the pinned 11.1.2 build crashes at once (exit `C0000374`,
heap corruption) unless usb4-emu's `toolchain\msys64\ucrt64\bin` is first
on PATH, as `launch-q35.ps1` sets it; the night worker's se1 and the
base's own launcher never met this. The pinned build has no `usb-redir`
(the simulators need it); only scoop's 11.1.0 has it on this host (11.0.0
has none).

Vehicle: s0's rig (stock 11.1.0, C: on `piix3-ide`, 98 SE's own UHCI
driver). Readiness: System Properties > Performance lists only drive A in
MS-DOS compatibility mode (C: on 98 SE's protected-mode IDE driver;
`s0\ready-perf.png`); Device Manager: "Intel 82371SB PCI to USB Universal
Host Controller" and "USB Root Hub" with no problem mark
(`s0\ready-dm.png`).

### The round (98 SE's own UHCI, vehicle B)

Rig per leg: vehicle B as above, a fresh overlay of the q35 `w98se` base,
the transfer folder (`xf\`, vvfat, `snapshot=on`) as D:, no USB device at
power-on; the platform wizards (UHCI, PIIX IDE, root hub) answered with
Enter from `C:\WINDOWS\INF`, the restart asked as a cold start. The
simulator (`uacemu.py --profile fs` or `uac1emu.py --profile adsrc|alaw`,
usb-wdm-free `w49-uac` worktree, run read only) over usbredir on
127.0.0.1:4866x, plugged by QMP `chardev-add` + `device_add usb-redir`
on `uh.0` after the desktop; "USB Composite Device" by its wizard
(`USB.INF`), then the audio function by "Display a list" > Have Disk
`D:\UAC2` or `D:\UAC1`; for `uac2` a cold start and a second plug before
the round. Steps typed into Start > Run by QMP `sendkey`: `command /c for
%i in (1 ... N) do call D:\STEP.BAT <UACWAV args>`; `STEP.BAT` runs
`UACWAV.EXE ... >COM1` and, when it returns 1 (every FAIL path), exports
the `MEDIA` class key (`start /w regedit /e`, typed to COM1) before the
next step; after each batch `EXP.BAT` exports it again. For `uac1`,
`C:\N.BAT` = `set UACWAV_NAME=UAC1` + `STEP.BAT` (UACWAV picks devices
named "UAC2" otherwise). Every recording of both builds' `fs` and
`adsrc` rounds completed 8/8 buffers with a 999 Hz tone. Host CPU load (`\Processor(_Total)\% Processor
Time`, two samples) and the host's QEMU count at each batch's start and
end (`<leg>\batches.txt`). Counters decoded by `bin\tally.py`.

Binaries (SHA-256 prefixes): `UACWAV.EXE` `20985f5e...` (the ME round's),
`uac2wdm.inf` `bfc5574b...`, `uac1wdm.inf` `1d6bfde1...` (both unchanged
from `6a266c7` to `34f1896`).

| Kit | `uac2wdm.sys` | `uac1wdm.sys` |
|---|---|---|
| `s0` baseline, main `6a266c7` | `099e07a4b16d3b8b...` (the ME round's me0 image, `Uac2Build` 28) | `cd2aa869b1c48236...` (built here from a `git archive` of `6a266c7`, wdmx86 free, PASS; build stamp 4) |
| `s1` the fix, `34f1896` | `20c1a9e7b4baafb0...` (the ME round's me1 image, `Uac2Build` 29) | `710da69716599cef...` (the w49-uac worktree's `out\`, build 5) |

#### `s0` / `s0u`: main `6a266c7` (baseline)

`s0` (`uac2`, `fs`): the platform wizards, restart, cold start, plug, the
uac2 wizard (Have Disk `D:\UAC2`), `UACWAV list` (1 in, 1 out), Shut
Down, cold start, plug again, `UACWAV list` (1 in, 1 out), the round:

| Batch | Steps | Result | `MEDIA\0000` after (`Uac2Build` 28) | Host load start / end |
|---|---|---|---|---|
| `s0r44a` | 20 x rec 1 16 44100 3 | 20 PASS (8/8 buffers, 999 Hz) | `Uac2Streams` 20/20/20; `Uac2M0Stage` 31, status 0 | 46 % / 50 %, 11-12 QEMUs on the host |
| `s0r48` | 6 x rec 48 kHz 3 s | 6 PASS | 26/26/26 | 37 % / 44 % |
| `s0p48` | 3 x play 2 16 48000 5 | 3 PASS | render 3/3/3 | 43 % / 48 % |
| `s0p44` | 3 x play 44.1 kHz 3 s | 3 PASS | render 6/6/6 | 40 % / 40 % |
| `s0r44b` (repeat) | 20 x rec 44.1 kHz 3 s | 20 PASS | capture 46/46/46 | 55 % / 69 % |

So under 98 SE's own UHCI the baseline passed 52 of 52 and every one of
46 capture opens was created (attempts = created = closed): 98 SE opens
one capture stream per recording in these batches, where ME opened
several (the ME round: 87 attempts for 20 recordings).

`s0`, then `uac1` on the same boot: `device_del` of the `fs` device,
`uac1emu.py --profile adsrc` plugged, its wizard to Have Disk `D:\UAC1`;
at the install's last step "A fatal exception 0E has occurred at
0028:FF09278C in VXD wdmaud(07) + 00002F3C" (`s0\fault-wdmaud-uac1-install.png`),
then 0E faults at `C000BC4E` and `00010289` at Shut Down
(`fault-shutdown*.png`); QEMU quit. Two later cold boots of `s0` with the
`adsrc` device plugged wedged in VMM (`c000a7bf`/`c000a881`/`c0003621`,
the clock stopped; the simulator's trace ends at the string descriptors
after `SET_INTERFACE`). `s0` retired. Not repeated on purpose; whether
it is 98 SE's `wdmaud` after a hot unplug of a WDM audio device in use by
the tray volume control or anything of the driver's is not known (the
`s1` legs below avoid the hot swap the same way, so the comparison
stands; the fault itself is a lead, recorded in Facts).

`s0u` (`uac1`, fresh overlay, baseline kit): platform wizards, restart,
cold start, `adsrc` plugged, wizard to `D:\UAC1` ("USB Audio 1.0
(uac1)", build stamp 4 in its key), `UACWAV list` (1 in, 0 out), the
round; then Shut Down, cold start, `alaw` plugged (installed by itself
from the INF copied at the first install), the round:

| Batch | Device | Steps | Result | Device key after (build 4) | Host load |
|---|---|---|---|---|---|
| `s0uA44a` | `adsrc` | 20 x rec 1 16 44100 3 | 20 PASS | `MEDIA\0000` 20/20/20, stage 31, status 0 | 50 % / 49 %, 14 QEMUs |
| `s0uA48` | `adsrc` | 6 x rec 48 kHz | 6 PASS | 26/26/26 | 48 % / 49 % |
| `s0uA44b` (repeat) | `adsrc` | 20 x rec 44.1 kHz | 20 PASS | 46/46/46 | 50 % / 59 % |
| `s0uLp48` | `alaw` | 3 x play 2 16 48000 5 | 3 PASS | `MEDIA\0007` render 5/5/5 | 43 % / 56 % |
| `s0uLp44` | `alaw` | 3 x play 44.1 kHz 3 s | 3 PASS | render 8/8/8 | 54 % / 48 % |
| `s0uLr48` | `alaw` | 6 x rec 1 16 48000 3 | 6 PASS | capture 12 attempts / 8 created / 8 closed | 49 % / 46 % |
| `s0uLr16` | `alaw` | 6 x rec 1 16 16000 3 | 6 PASS (1000 Hz, -6.2 dBFS) | capture 18/14/14 | 39 % / 37 % |

(The first `rec` on `s0u` before `C:\N.BAT` existed read "FAIL: no UAC2
input": UACWAV's device name filter, not a driver result.)

#### `s1` / `s1u`: the fix `34f1896`

The same procedure, the same order of batches, each on its own fresh
overlay; the `uac2` and `uac1` devices again on separate overlays (no hot
swap before the rounds).

| Batch | Device | Steps | Result | Device key after (`Uac2Build` 29 / `uac1` build 5) | Host load start / end |
|---|---|---|---|---|---|
| `s1r44a` | `fs` | 20 x rec 1 16 44100 3 | 20 PASS | `Uac2Streams` 20/20/20, 4th word 2; stage 31, status 0 | 25 % / 42 %, 12 QEMUs |
| `s1r48` | `fs` | 6 x rec 48 kHz | 6 PASS | 26/26/26 | 33 % / 25 % |
| `s1p48` | `fs` | 3 x play 48 kHz 5 s | 3 PASS | render 3/3/3 | 28 % / 46 % |
| `s1p44` | `fs` | 3 x play 44.1 kHz 3 s | 3 PASS | render 6/6/6 | 30 % / 27 % |
| `s1r44b` (repeat) | `fs` | 20 x rec 44.1 kHz | 20 PASS | capture 46/46/46 | 27 % / 51 % |
| `s1uA44a` | `adsrc` | 20 x rec 44.1 kHz | 20 PASS | 20/20/20 | 31 % / 48 %, 15 QEMUs |
| `s1uA48` | `adsrc` | 6 x rec 48 kHz | 6 PASS | 26/26/26 | 51 % / 53 % |
| `s1uA44b` (repeat) | `adsrc` | 20 x rec 44.1 kHz | 20 PASS | 46/46/46 | 34 % / 36 % |
| `s1uLp48` | `alaw` | 3 x play 48 kHz 5 s | 3 PASS | render 5/5/5 | 24 % / 48 % |
| `s1uLp44` | `alaw` | 3 x play 44.1 kHz 3 s | 3 PASS | render 8/8/8 | 35 % / 43 % |
| `s1uLr48` | `alaw` | 6 x rec 48 kHz | 6 PASS | capture 12/8/8 | 35 % / 42 % |
| `s1uLr16` | `alaw` | 6 x rec 16 kHz | 6 PASS | capture 18/14/14 | 37 % / 30 % |

`Uac2Waits` was never written in any `s1`/`s1u` export: no open met a
closing stream on 98 SE. The 4th word of `Uac2Streams` (the gate's value
when `Close` records it, `uacwdm.cpp` `Close` hook) reads 1 (held) under
the baseline and 2 (`GateClosing`, `uacstream.h`) under the fix: the
final release marked the gate closing before its queued destroy, on every
close, as designed. Every counter of every batch is otherwise the same
under both builds, including `alaw`'s 4 capture attempts that were not
created at 48 kHz (12/8/8), present under both builds and never a
failed step. Their cause was not read: `Uac2Waits` absent means no open
waited for a closing stream, but an open gate is refused at once without
a wait and a later open overwrites `Uac2M0Stage`/`Status`, so a busy
refusal among them is not excluded.

Extra, one try (`s1u`, the fix): the hot swap that faulted on `s0`, in
the other order: `alaw` used, `device_del`, `fs` plugged and installed
by Have Disk `D:\UAC2` on the same boot: no fault, `UACWAV list` 1 in,
1 out, 3 x rec 44.1 kHz PASS (its key 9 attempts, 5 created, `Uac2Waits`
absent), Shut Down clean in 5 s. One try in the other direction: it
neither reproduces nor rules out the `s0` fault.

### Totals

| Build | `uac2` `fs` | `uac1` `adsrc` | `uac1` `alaw` | Application-visible open failures |
|---|---|---|---|---|
| baseline `6a266c7` | 52/52 (rec 44.1 kHz 40/40) | 46/46 (rec 44.1 kHz 40/40) | 18/18 | 0 |
| the fix `34f1896` | 52/52 (40/40) | 46/46 (40/40) | 18/18 | 0; no closing wait recorded |

Both builds passed 116 of 116 steps under 98 SE's own UHCI; no closing
wait was recorded under the fix. In the `fs` and `adsrc` batches 98 SE
created one capture stream per recording (attempts = created = closed),
where ME created several (the ME round: 87 attempts for 20 recordings);
the ME failure and the fix's waiting were therefore not exercised in this
98 SE round (one stream a recording can still meet the previous
recording's queued destroy under other scheduling; not seen here). The
44.1 kHz recordings were run twice on each build; host load 24 % to 69 %
at batch edges, 10 to 16 QEMUs of the other workers running.
Not run here: 98 SE under NUSB's UHCI and under xHCI98 (the night
worker's "owed" list named both); real USB 1.1 hardware.

Evidence: `E:\xhci98-vm\p49\uac98\` (`a1`, `a2`, `s0`, `s0u`, `s1`,
`s1u`: `cmdline.txt`, `watch.txt`, `batches.txt`, `serial*.log`,
`sim-*.jsonl`, screendumps, `fault-*.png`), `pids.txt` (every start and
stop; none running). Scripts `E:\xhci98-scratch\w49\uac98\bin\`.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** on 2026-11-13 the reading on real
USB 1.1 hardware was dropped by the owner (no such computer at hand, the reasoning that closed 49.14). The fix was
merged into usb-wdm-free main and released in usb-wdm-free 0.1.10 (tagged 2026-11-11; its release notes: an open just
after a close waits up to 2 s for the one before to finish closing), and `3.5.0.0` carries it through its pin to that
tag (49.3). Read on 2026-11-13 on the released 0.1.10 files, the installed driver compared byte for byte with the
release's in each guest: ME under its own UHCI, the 44.1 kHz recordings 18 of 18 in each of two fresh guests (the
earlier build refused 3 of 18), the full matrix 72 of 72 each and 18 of 18 back to back each, the driver's own counter
showing about 20 opens a guest that met a closing stream and waited 13 to 15 ms; 98 SE under the third-party USB
stack's UHCI of the earlier rounds 40 of 40 and under xHCI98 40 of 40 (the Full Speed device); XP under its own UHCI
24 of 24 with volume and mute. Not read: the High Speed device on 98 SE under xHCI98 (the wizard's search did not find
its driver in that guest; not pursued), and XP under EHCI in the stock emulator.

## 49.31 - 98 SE: the fatal exception 0E at `0187:BFF9DFFF` after a tunnelled drive arrives after a wake, read as data (2026-11-11, worker s98w)

Branch `w49-s98w` at `30051d7ed`, no commit (no source change). Follows the 49.12 continued section
("The fatal exception 0E on 98 SE when a drive arrives after a wake"). Each block of legs was planned,
reviewed and revised before it ran. No Microsoft
code was read, disassembled or dumped: what is read is QEMU's own exception log, register and stack
values, page-table presence, interrupt numbers and the loaded-image list a helper obtains from ToolHelp.

**Vehicle.** As 49.12: 98 SE from the q35 base on QEMU's `pc` machine, PIIX IDE with no geometry
hints, one vCPU, 512 MB, `usb4-tunnel-bridge` 00:05.0, `usb4-generic` host 00:04.0, the ORICO model,
`qemu-xhci` 00:06.0 (`E:\xhci98-vm\p49\s98w\tools\go98.ps1`), QMP 48700 to 48702. "Bare" is the clean
q35 98 SE base, none of this package, the same emulated hardware. "Package" is this tree's own build
(gated x86 build of `30051d7ed`, all passed, `E:\xhci98-scratch\w49\s98w\gated-x86-base.log`): the
release `98-me` kit installed on a fresh overlay (`INSTALL /console /yes /nopause /thunderbolt
/stor98`, exit 4, the Stor98 wizard, a cold start), then the `qemu` flavour of `xhci98.sys`
(`11703a6c`), `xhcicm.sys` (`9db1d9bf`), `xhciuas.sys` (`9b944a9e`) and `S98VSD.VXD` (`e58e3627`)
copied over the installed files (`fc /b`: no differences; the installer refuses a `qemu` folder), a
cold start, E: mounted, written and compared: `bq\disk.qcow2`, read-only, every package leg a fresh
overlay of it. `stor98.mpd` is the free build (`93080489`): the gated build makes no other 9x
flavour. Two helpers run in the guest only, both started before the sleep: the previous worker's
`C98.EXE`, and `W98.EXE` (`E:\xhci98-scratch\w49\s98w\w98\w98.c`: the ToolHelp image list, a
`WM_DEVICECHANGE` / `WM_POWERBROADCAST` log to COM1, one file call after a delay, a standby asked by
itself).

**Method.** QEMU's exception log (HMP `logfile`, `log int`) switched on a few seconds before the
trigger: every interrupt and exception with vector, error code, CS:EIP, SS:ESP, CR2 and the register
set. One screen poll a second (720x400 is the fault screen), with the line counts of the debug
console and the serial log. With the fault screen up: HMP `x` on the faulting stack, HMP `info tlb`
(QEMU walks the guest's page tables for it) for whether a page is mapped.

### The fault itself

The same in every faulting leg, bare or package (13 legs; `int-<leg>.log`, `ev-<leg>.txt`):

- **No CPU exception has EIP `BFF9DFFF`.** The last ring-3 page fault before the fault screen is
  `v=0e e=0004 cpl=3` in **16-bit code of a low-memory segment** (CS base `00091200`, EIP `78BC`,
  linear `00098ABC`; the selector is `016F` in the package legs and `014F` in the bare ones, the flat
  code selector `0187` and `0167`, as in the two forms of the screen's address), with DS = ES = the
  flat data selector, `ESI = EDX = BFFD2510`, `ECX = 00000150`, `EBX = 00008BD2` (`EAX` is `015F` in
  the bare legs and `017F` in the package ones; other registers vary), **CR2 = `BFFD2510`**, error code 4
  (not present, read, user). Protected mode, ring 3, LDT selectors: not a V86 VM.
- SS has base `00847000`; SS:SP is linear `0084FBB2`. Read with the fault screen up (so after the
  exception, not at it): `BFF9DFFF` is **a word at `0084FBB8`** (r0, p1, p2), and the words above it
  include `BFFD2510` and `0150`. Of the words of `0084FBB0`-`0084FFFF` read in r0, 34 lie in
  `BFF713E2`-`BFFCB510`; they are candidate image references in the captured region, not a call
  chain, and the region holds other values too.
- By the helper's ToolHelp list (p1; user-mode images of the helper and of `EXPLORER.EXE` only):
  `KERNEL32.DLL` base `BFF70000`, size `00073000`; `EXPLORER.EXE` `00400000`, `SHELL32.DLL`
  `7FCB0000`, `USER32.DLL` `BFF50000`. The reported address, the address in CR2 and those 34
  candidates are inside `KERNEL32.DLL`'s range; none of the 34 is in the range of `EXPLORER.EXE` or
  `SHELL32.DLL`. Whose thread it is, is not decided (the process list has a `KERNEL32.DLL` entry
  beside `EXPLORER.EXE`).
- After that fault and before the screen, in every leg: a ring-3 `v=0d` in 16-bit code of another
  low-memory segment (EIP `0B5D`, linear `0000D9BD`) with SS selector `2087`, and later ring-0
  entries holding `ESI = 0000000E`, `EDI = BFF9DFFF`, `EBX =` the flat code selector (the screen's
  numbers). In seven of the thirteen legs (a1, t3, r0, q2, v2, n2, z3) a ring-0 page fault (CR2
  `829A9FF4` or `829A5FF4`) is the entry right after it; in the other six it is not.
- By HMP `info tlb`, page `BFFD2000` has no mapping before the standby, 10 s and 60 s after the wake
  (t1, t3) or at the fault screen (r0, a1; not read in the legs of this tree's build); `BFFD0000` and `BFFDF000` have; two other pages
  of `BFF70000`-`BFFEFFFF` gained a mapping after the wake (t1). With no standby an entry with the
  same CS:EIP has CR2 `7FD445B0` (t2) and no fault screen follows.
- With the package, the last lines of the connection manager and of `S98VSD.VXD` on the debug console
  (`cm.nvlink.up`, `s98vsd claimed dcb`, `cm.nvlink.9x.proof-read`) are written before the fault and
  no line of ours follows; the watcher received the volume's `DBT_DEVICEARRIVAL` (unit mask
  `00000010`, E:) and a `DBT_DEVNODES_CHANGED` before the fault screen (p1, q1, v2).

### The legs

Fault = the screen and the signature above. A clean leg is "not seen in the stated time".

| Leg | System | What was done after the wake | Result |
|---|---|---|---|
| a1 | bare | 60 s idle; Start > Run, a command typed | fault while the command was typed (the next key dismissed the screen); signature |
| t1, t3 | bare | 60 s idle (none); Win+R (none in 10 s); the letter `c` | **fault** within 6 s, twice of two; signature |
| t2 | bare, **no standby** | the same keys | none in 30 s |
| f2 | bare | helper: one `FindFirstFile("C:\*")` 70 s after the wake, nothing typed | none in 150 s; the call succeeded |
| f3 | bare, no standby | the same | none in 150 s |
| g1 | bare | helper: one `GetPrivateProfileString` on `C:\AUTORUN.INF` (no such file) | none in 150 s |
| g2 | bare, no standby | the same | none in 150 s |
| r0 | the previous worker's image (`3.4.3.0` + `7e0bf543c` qemu `xhcicm.sys`) | drive kept across the standby; unplug at +60 s, replug at +91 s; nothing typed | **fault** 7 s after the replug; signature |
| p1 | package, watcher | the same | **fault** 7 s after the replug; signature; the volume's arrival broadcast first |
| p2 | package, no W98 watcher (C98 only) | the same | **fault** 7 s after the replug; signature |
| p3 | package, **no standby** | unplug, replug 31 s later | none in 65 s; E: back, written and compared |
| z3 | package, zero-filled drive, no watcher | the p1 / p2 sequence (standby, wake, unplug, replug); then, 135 s after the replug, Win+R and the letter `c` | none in 135 s after the replug; **fault** within 5 s of the letter; signature |
| z1, z2 | package, the drive **zero-filled** (no partition table) | the same as p1 / p2 | **none** in 60 s (z1) and 65 s (z2); in z1 (watcher) Stor98 started the function, `s98vsd claimed dcb`, two devnode arrivals broadcast, no volume broadcast |
| q1, q2 | package, the drive **partitioned, never formatted** (the FAT16 entry, a zero boot sector) | the same | **fault**, twice of two; the letter E: arrives (volume broadcast) with no file system |
| v1, v2 | package, **`S98VSD.VXD` renamed away** (no `s98vsd` line on the debug console) | the same | **fault**, twice of two |
| n2 | package, **`stor98.mpd` renamed away** (`drive=NONE`) | the same replug: none in 65 s; then Win+R and the letter `c` | **fault** within 4 s of the letter; signature |
| n3 | package, `stor98.mpd` renamed away | as n2 | **fault** at the letter (2 of 2); signature |
| h1 | bare | helper: `GetLogicalDrives`, each letter's `GetDriveType`, `GetVolumeInformation` of C: and D:, 70 s after the wake | none in 150 s; the calls succeeded |
| h2 | bare | helper: `SHGetFileInfo("C:\")` (name, icon, type) 70 s after the wake | none in 150 s; the call succeeded |
| s5 | bare + an AMD 53C974 SCSI adapter present from power-on (98 SE's in-box driver; with no standby a `scsi-hd` hot-added and Device Manager's Refresh mount E:, written and compared: `s1`, boot `s2`) | Device Manager opened before the sleep; after the wake the disk hot-added, Refresh (Alt+F) | **void as a control**: no disk under the adapter after the wake, three devnode removals broadcast, no volume arrival; no fault in 120 s |
| s6 | the same, no disk added | Refresh only | the same broadcasts; no fault in 120 s |
| s3, s4 | the same | - | void: the Run dialog's Esc keys had closed Device Manager before the sleep |
| f1, n1 | bare / package | - | void: the guest reset at the wake (QEMU exited), the lessons' vehicle trap |

Counts with this tree's build: a lettered volume arriving after a wake, 6 of 6 with the fault (p1, p2,
q1, q2, v1, v2); no letter arriving, 0 of 2 (z1, z2; 49.12's n12 and n13 were the same case by
another route, 0 of 2) and 0 of 1 in 135 s (z3); no standby, 0 of 1 in 65 s (p3; 49.12's n2 the same).
Bare platform: a Run-dialog entry typed after a wake 3 of 3 (t1, t3 at the first letter; a1 while a
command was typed), 0 of 1 in 30 s with no standby (t2); with the package and
Stor98's port driver away 2 of 2 (n2, n3).

### Reading

1. **One failure signature, with and without the package.** The fault of the package legs and the
   bare platform's own after-standby fault (the lessons' entry; 49.12's `ca2`) carry a matching failure
   signature as far as it can be read without reading Windows: the same 16-bit instruction (linear `00098ABC`),
   the same CR2 (`BFFD2510`), `ESI`, `EDX`, `ECX` and `EBX`, the same stack region, the same word reported
   (`BFF9DFFF`). None of this package's drivers is needed to reproduce it: the
   bare platform gave it three times of three when a Run-dialog entry was typed after a wake (twice at
   the first letter), and not in 30 s without a standby.
2. **What "Stor98's port driver loaded" brought was the drive letter.** With the package the fault
   came each time a lettered volume arrived after a wake (formatted or not, with `S98VSD.VXD` or
   without it), and did not come on a drive with no partition (three legs: 60, 65 and 135 s; in z1, the one with
   the watcher, Stor98 started the same function, `S98VSD.VXD` claimed the same DCB and the disk's
   devnodes arrived with no volume). With Stor98's port
   driver away the replug gave no fault in 65 s, and the same boot then gave it at the bare platform's
   trigger. The connection manager's and the VSD's lines are written before the fault.
3. **What Windows does at that moment is not known.** CR2 lies in `KERNEL32.DLL`'s image range on a
   page with no mapping; the candidate image references read on that stack are of that range.
   A directory search, a profile read, a drive enumeration and a shell file-information call made by
   another program after a wake did not give the fault (f2, g1, h1, h2: once each, 150 s), so the
   trigger seen is something that happens when a letter arrives or a Run-dialog entry is typed, and
   it was not reproduced by those calls. An `int 21h` with `AX=714E`, then one with `AX=71A1`, issued from the same low-memory
   segment (linear `000968D2`) precedes the fault in all 12 faulting legs read for it (by 92 to 125 log entries in eleven, 352 in v2); it occurs once in p3 (the arrival with no standby, no fault) and in no other clean
   leg, t2 (the same keys with no standby) included. What issues it is not read.
4. **Limits.** These are readings on one emulator. They show a failure signature that this package
   is not needed for, and that no driver change is justified by them; they do not show the cause
   inside Windows, they do not exclude an earlier effect of a driver of ours in the package legs
   (the bare legs have none), and they do not say what a real machine does (ME on the same vehicle:
   0 of 2 in 49.12). A bare-platform control with a real fixed volume arriving after a wake was not
   achieved: IDE has no hot-add, the SCSI adapter tried does not find a disk added after a wake
   (s5), and a USB stick needs storage drivers the bare 98 SE does not have (the continuation below
   has the other routes tried, and the synthetic broadcast). The previous worker's
   one clean leg of eight (`uf1`) is not explained. Whether a real 98 SE machine shows any of this is
   the bench's.

### Continued: the bare volume-arrival controls and the repeats (worker s98w2, the same day)

The same vehicle, tools and judging rule (`sig.sh`; a clean leg states its interval, 120 s at least
from the event; a leg meant to show an arrival is void unless the watcher logged it). Each block was
planned, reviewed and revised before it ran. `W98.EXE` gained three modes (source as above): `re`
(30 s after the resume, one `FindFirstFile("E:\*")`, a 64 KiB read from the tail of
`E:\TESTDATA.BIN` and a new file written and flushed, four times), `bc<L>` (60 s after the resume
one synthetic `WM_DEVICECHANGE` / `DBT_DEVICEARRIVAL` with a `DEV_BROADCAST_VOLUME` for letter L,
flags 0, sent by `BroadcastSystemMessage` to `BSM_APPLICATIONS` from a shared mapping) and `nb<L>`
(the same 90 s after its start, with no standby). In the broadcast legs a second instance, started
first, is the observer: its log of the receipt is what counts. The bare legs below run a fresh
overlay of `s1\disk.qcow2` (the bare base with 98 SE's in-box AMD driver installed, none of this
package); the CD legs one of `cdb\disk.qcow2` (the same, an ATAPI CD drive detected: `QEMU DVD-ROM`,
letter E:, Auto insert notification on).

| Leg | System | What was done after the wake | Result |
|---|---|---|---|
| u1 | bare + AMD 53C974; disk hot-added and mounted **before** the sleep (E:, C98 written and compared) | `re`: the first `FindFirstFile("E:\*")` 30 s after the resume | this one call **did not return** in about 290 s (E: still in the drive mask); no fault; the shell kept running |
| L0 | bare + `lsi53c895a` from power-on (no standby) | - | 98 SE's new-hardware wizard, searching with its defaults, found no driver ("Unknown Device") |
| L1 | bare + `lsi53c810` from power-on (no standby) | - | 98 SE's in-box "Symbios Logic 8100S PCI SCSI Adapter; 53C810" installs from `SCSI.INF`, then **Code 10** at the next cold start and at a second clean one (L4) |
| c1, c3 | bare + ATAPI CD, tray empty; c1 `cd-plain.iso`, c3 `cd-auto.iso` (with an `AUTORUN.INF` holding only a label) | the medium inserted (HMP `change`, read-only) 36 s after `system_wakeup`; nothing typed | **void**: no volume broadcast in 270 s / 286 s after the insertion; no fault |
| c2, c4 | the same, **no standby** | the same insertion | volume arrival broadcast (mask `00000010`, E:, flags `0001` = `DBTF_MEDIA`) about 3 s after the insertion; none in 270 s; c2: `E:\README.TXT` then opened and read |
| e1, e4, e6 | bare, observer + `watch zz bcC` | the synthetic arrival for C: 60 s after the resume; nothing typed | **fault** within about 5 s of the observer's receipt, 3 of 3; signature (CS:EIP `014F:78BC`, CR2 `BFFD2510`, `EDI = BFF9DFFF` entries, the `int 21h` `AX=714E` / `71A1` at linear `000968D2` before it) |
| e8 | the same, `bcM` (M:, a letter with no drive) | the same | **fault**, signature (1 of 1; a broadcast naming no drive, reported apart) |
| e2, e7 | the same, `nbC`, **no standby** | the broadcast 90 s after the start | received; none in about 300 s and 340 s after it |
| e3 | the same, `watch zz` (no broadcast) | nothing | none from 31 s to about 365 s after the wake |
| e5 | as e1 | - | void: the guest reset at the wake (rerun as e6) |
| z4, z5 | package, zero-filled drive (as z1-z3) | the p1 / p2 sequence (the replug logged 103 s after `system_wakeup`); then Win+R and the letter `c` | none in 160 s and 162 s after the replug; then **fault** at the letter, both; signature |
| q3 | package, partitioned, never formatted, watcher | the p1 / p2 sequence (replug logged 104 s after `system_wakeup`, as in v3 and n4) | volume arrival broadcast (E:); **fault** 7 s after the replug; signature |
| v3 | package, `S98VSD.VXD` renamed away, watcher | the same | no `s98vsd` line; volume arrival broadcast (E:); **fault** 7 s after the replug; signature |
| n4 | package, `stor98.mpd` renamed away (`drive=NONE`) | the same replug; then Win+R and the letter `c` | none in 158 s after the replug; then **fault** at the letter; signature |

Counts. Bare 98 SE, none of this package: a volume-arrival broadcast after a wake, 3 of 3 with the
fault for C: (e1, e4, e6) and 1 of 1 for an unused letter (e8); the same broadcast with no standby,
0 of 2 (e2, e7, about 300 s and 340 s); the same wake with no broadcast, 0 of 1 (e3). A real fixed volume
arriving after a wake: no successful route was demonstrated on this vehicle (u1, L0, L1); a CD medium inserted after a wake: no
arrival broadcast (c1, c3, void); inserted with no standby: arrival broadcast, 0 of 2 (c2, c4).
Package, both workers: a lettered volume arriving after a wake 8 of 8 (p1, p2, q1, q2, q3, v1, v2,
v3); the zero-filled drive, 0 of 5 (z1 60 s, z2 65 s, z3 135 s, z4 160 s, z5 162 s; at least 120 s:
z3, z4, z5); `stor98.mpd` away, 0 of 1 in 158 s (n4; n2, n3 65 s each); each of z3, z4, z5, n2, n3,
n4 then faulted at the Run-dialog letter.

Reading, continued.

5. **A volume-arrival broadcast after a wake is enough on the bare platform.** Without this
   package and without a storage-device arrival, one `DBT_DEVICEARRIVAL` volume broadcast to the
   applications 60 s after a resume gave the fault with the same signature each time (4 of 4, a real
   letter or not), within seconds of its receipt; the same broadcast with no standby did not (0 of 2),
   and the same wake without it did not (0 of 1). With the package, the fault comes when the volume
   broadcast of the arriving letter comes (8 of 8) and not when Stor98 starts the function on a drive
   with no partition (0 of 5). The two are consistent: in the package legs the fault coincides with the volume arrival, and on
   the bare platform a volume-arrival broadcast after a wake is sufficient for it. That is not a
   complete account of what the package contributes. Which receiver of the
   broadcast faults, and why only after a standby, is not read.
6. **Limits of the continuation.** The bare trigger is a synthetic application broadcast, not a disk
   arriving: whatever else a real arrival does was not reproduced without the package, because no
   successful bare route was demonstrated on this vehicle (with the AMD adapter one file call on the
   volume mounted before the sleep did not return after the wake; for `lsi53c895a` the wizard found no
   driver, `lsi53c810` bound and showed Code 10; a CD medium inserted after a wake was not announced,
   twice). It does not exclude an earlier effect of a driver of ours in the package legs. These
   are one emulator's readings; no real machine has been read.

No fix: nothing in this tree's code is shown at fault. No user-document sentence is proposed from
emulator readings (the user documents do not carry them); a draft for the case that the bench sees
the fault is in the worker's report.

Evidence: `E:\xhci98-vm\p49\s98w\<leg>\` (`leg-<leg>.txt`, `int-<leg>.log`, `ev-<leg>.txt`,
`<leg>-fault.png`, `debugcon-<leg>.log`, `serial-<leg>.log`, `tlb-*.txt`, `r0\stack-84fbb0.txt`),
`pids.txt`, `STATE.md`; tools `E:\xhci98-vm\p49\s98w\tools\` (`legA`, `legT`, `legF`, `legP`, `legS`, `legS2`,
`legL0`, `legC`, `legB`, `prepB`, `prepQ`, `sig.sh`); the CD images `img\cd-plain.iso` and
`img\cd-auto.iso` (made by `E:\xhci98-scratch\w49\s98w\mkiso.py`).

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** the bench reading left was whether a
real 98 SE machine shows the fault at all (standby, wake, connect a Thunderbolt or USB4 drive; and with no package:
standby, wake, Start > Run, one letter), with a user sentence only if it did, worded by the owner and naming no
workaround: "Windows 98 SE: connecting a TB / USB4 drive after Stand by can end in a fatal exception 0E." The owner
dropped that reading on 2026-11-13 (no computer on the bench offers Stand by under 98 SE or ME, and the user documents
already say to stop and unplug the drive first), so the sentence is not written.

## 49.29 The staged path's test instrumentation, in the qemu flavour only (2026-11-11, worker ins, day wave)

Branch `w49-ins` from `3.5.0.0` at `ab3d629a5`. Commit `e5b2df8cd` (design record 13 section 11.11.12, new; pointers in record 17 section 4.13.9 list 1; `src\hcd.h`, `hcd_stage.c`, `hcd_dma.c`, `hcd_urb.c`, `hcd_entry.c`, `hcd_io.c`, `hcd_cfg.c`, `hcd_inj.c`, `hcd_ctl.c`).

### What was built

All of it under `XHCI_FLAVOUR_QEMU`; no import added; no value name added (three new fields of `XhciQemuStageFault`, qemu only, written by no INF).

- **The hold** (bits 19:16, the state; bit 2, the containment). Once a start the first record to reach the chosen state is kept in it until staging's admission closes or 500 polls of 20 ms pass: 1 `WAITING` (every take fails), 2 `FILLING` (an OUT's pass stopped before its copy), 3 `READY` (stopped before `HcdIoMapped`), 4 `DRAINING` (an IN's copy-out stopped), 5 `COPY_PENDING` (every copy-out fails as an unmapped window, the give-up suspended), 6 the staging DPC (its work deferred, still counted). The hold is counted in `StageOwners` from its start to the end of its own timer DPC, after the work it carried on, so `HcdStageDrain` joins it. With bit 2 it arms `DEAD_NOPROOF` until CLEAR as it begins (`HcdInjContainNoProof`, `InjLock` only; refused on a failed controller), so the health poll reads all ones and the containment finds no proof. Lines: `stage.hold: taken, state`, `stage.hold: let go, state`, `... admission closed` (1: by the closure, 0: by the bound), note `qemu.stage.hold` = state << 8 | closed.
- **The per-request log** (bit 3, image-wide from the first start that reads it until Windows restarts; `urb: log on` marks the moment): `urb: in seq/irp/pdo/function` at the device PDO's internal device control dispatch, `urb: out seq/irp/ntstatus/usbd` before each `IoCompleteRequest` that can complete a URB IRP at a device PDO (`HcdCompleteIrp`, the deferred work, the held requests', the refusals', the slow queue's). Each line under the log's own lock; on amd64 the IRP's address in two words. Totals at each stop (`urb.log: in, total`, `out, total`). Host reader: `E:\xhci98-scratch\w49\ins\tools\urbcheck.py`.
- **`stage.copy.detached`**: a copy for a record already detached from its request (its URB released), at an OUT's fill and at the head of a copy-out; must stay 0.
- **The allocations and the ledger**: per start, `dma.alloc: made, freed, kept, left (n/pages each)` at the end of `HcdDmaClose`; the image ledger's live and kept totals at each `HcdDmaOpen` (`dma.ledger: at the start, live/kept`), after each release, and at each refusal (`dma.ledger: REFUSED, bytes asked`).

### Review

The design (record 13 section 11.11.12 and the 49.16 paragraph of record 17) was reviewed before code and revised twice (six points, then one, then nothing material); the code was then reviewed against it, nothing material. Reviewed and revised.

### Build

One gated build of `074a96c79` (both commits), slot `gated.1`, `XHCI98_SELFTEST_SKIP_OK=1`, 18:22 to 18:54: x86 `all` **BUILD + GATES PASSED (release qemu)**, amd64 `all` **BUILD + GATES PASSED (release qemu)**. Host tests: 97 suites, 622,239 checks, 0 failures, on each run (`Host tests PASSED`). Log `E:\xhci98-scratch\w49\ins\gb.log`; images in `E:\xhci98-scratch\w49\ins\bin86`: x86 release `6D3B1AF4...B673` (366,240 bytes), x86 qemu `5FD34AE4...7AD7` (438,021), amd64 release `8B139ED9...FB90` (418,816), amd64 qemu `79E31A85...1BC1` (816,128). (The worktree's `tools\` held no toolchain: its entries were junctioned to the main checkout's, 33 junctions, before the build.)

**The release images carry none of it.** Searched in both architectures: `stage.hold`, `urb: in seq`, `urb: out seq`, `urb: log on`, `dma.ledger`, `dma.alloc`, `stage.copy.detached`, `qemu.stage.hold`, `DEAD_NOPROOF`, `urb.log`, `XhciQemuStageFault`: 0 in each release image, present in each qemu image (`cfg.stranded`, 49.16's note, in both, as it should be). Import tables of the release images identical to another `3.5.0.0` build's: x86 82 rows against `w49-whpx`'s (`52e5404e2`), amd64 71 rows against `w49-mark`'s (`e273a4e2f`).

### The readings (record 17 section 4.13.9, list 1), qemu flavour, in the emulator

Guests (worker ins): XP SP3 (`E:\xhci98-vm\p49\ins\xp`, one processor, QMP 48800, pid 167904; a USB tablet and a BOT stick, an overlay of `shared\nvme-fat.qcow2`, on qemu-xhci at power-on) and 98 SE (`...\s98`, one processor, QMP 48801, the tablet at power-on, the stick hot-added; one cold start per setting: pids 157460, 166060, 165340, 178240, 176172, 158400, 178840, 173800, each in `pids.txt`). Both fresh overlays of the q35 bases; this tree's x86 package installed (`INSTALL.BAT /console /yes /nopause /nostor98`, on 98 SE with `/storage`; exit 3; the installer and tools from worker net2's kit of the same base `ab3d629a5`, the package laid out by `ConvertTo-FamilyLayout` from this build's `out\pkg-release-x86`), then the qemu image copied over `xhci98.sys` (`fc /b`: no differences; on 98 SE once more after the first start's hardware wizard put the package's file back). Settings machine-wide in `Services\USB` (`XhciDmaStaging` 1, `XhciQemuStageFault`, `XhciStagePoolKiB`, `XhciQemuStagePool`, `XhciQemuInject`); on XP each change followed by `devcon restart` of the controller (WDK 7.1 x86 devcon), on 98 SE by a Shut Down and a cold start. One processor, so the 0xE9 lines stay whole. Kits `E:\xhci98-scratch\w49\ins\kx` and `k98`; slices and reports `E:\xhci98-scratch\w49\ins\xp-*.log`, `xp-m*.ser`, `s98-inj.log`; whole traces `xp\debugcon-a.log`, `s98\debugcon-{c,d,m2..m5}.log`; the reader `tools\urbcheck.py`. Host load: 8 to 12 other workers' guests; nothing here is timing-bound.

| List 1 item | System | Run? | What it showed |
|---|---|---|---|
| 1. `WAITING` at the stop (hold 1) | XP, 98 SE | run | the hold took the first record of the start (a client's first request after it), armed `DEAD_NOPROOF`, and was let go by the closure (`let go, admission closed=1`); report `tol.contained=00000002` (no proof), `qemu.stage.hold=00000101`; every device PDO removed (minor 02; on XP the stick's start refused, "device gone") with the controller left started; no URB left pending |
| 1. `FILLING` (2), `READY` (3), `DRAINING` (4), `COPY_PENDING` (5) | XP, 98 SE | run | the same each time (`qemu.stage.hold` 0x201, 0x301, 0x401, 0x501 on XP); on 98 SE the record held was the tablet's read each time |
| 1. The staging DPC (6) | XP | run, not reached | no `StageDpc` ran in the start with the copies tried (a 24 MiB write to the stick and the read back, compared equal, with `XhciQemuStagePool` 1): nothing was held. Not tried on 98 SE |
| 1. A cancel or an abort racing each state; a mapping failing N times or never; the retry timer | - | not run | no client here times a cancel; the map-failure bits were not used |
| 1. Each request completed exactly once | XP, 98 SE | run | the per-request log, read by `urbcheck.py`: XP 9,820 lines in the injection slice and 5,986 over the first sessions, 98 SE 14,110 in its injection session and 4 to 16 in each hold start; for every IRP the lines alternate `in` and `out`, no `out` twice, none left pending at the end, no damaged line; at each XP stop `urb.log: in, total` equals `out, total` (0x13B, then 0xA76 at the cap run's last stop). One sequence number out of order on XP marks the image reloading at the first restart, before any memory was kept |
| 1. Nothing copied after a completion | XP | run | `stage: copies for a record already detached=00000000` at every stop (the narrower check of record 13 section 11.11.12; the pattern check is not built) |
| 1. `stage.foreign` 0 | - | not read | a log note only, not looked up in the reports |
| 1. Allocations made = freed + kept | XP | run | each unproven stop at the default pool: made 38 (638 pages), freed 0, kept 38 (638), left 0; with `XhciStagePoolKiB` 16384: made 271 (4,292 pages), freed 11 (74), kept 260 (4,218), left 0; each proven stop: everything freed, left 0 |
| 2. A containment of a started controller (all ones without the proof), a HID read pending, a copy in flight | 98 SE, XP | run, in part | 98 SE: injected (`XhciQemuInject` 0x01070000) during a copy loop to the stick: fired, contained without proof (`tol.contained=2`), the stick's and the tablet's PDOs removed, the copy ended with VFAT's "Disk Write Error", E: gone, Windows went on, every URB completed. XP: injected after its copies had ended: SURPRISE_REMOVAL then REMOVE to both PDOs, the controller left started. Not run: a UAS stream request, audio playing, an ABORT_PIPE sent while failed and not yet contained |
| 3. The request a client sends as it is removed (98 SE and ME first) | 98 SE | run: HID, storage | contained without proof at 1: the tablet's and the stick's removals completed (six containments with the tablet, one with the stick in a copy), and each following Shut Down finished. Not run: audio through `usbaudio.sys`; ME |
| 4. The bytes kept | XP | run | one unproven stop keeps 0x27E000 bytes (2,613,248, 2.49 MiB) at the default pool, the ledger rising by that each time (0x27E000, 0x4FE000, 0x77C000, 0x9FA000, 0xC78000); with the pool at 16 MiB each stop keeps 0x107A pages (16.5 MiB): 0x1CF2000, 0x2D6C000, 0x3DE6000 (61.9 MiB); the next start refused at its first 64 KiB slab (`dma.ledger: REFUSED, bytes asked=00010000`, live 0x21A000, kept 0x3DE6000) and at the control slab, the controller at Code 10, the start's own allocations freed. Carried over: the controller's devnode removed (`devcon remove`) and found again (`rescan`, the wizard): the new FDO's start read live/kept 0 / 0x3DE6000 and was refused the same way. After a restart of Windows the ledger read 0 / 0. Not read: the emulated controller removed and added (QEMU `device_del`): XP sent the FDO its REMOVE but never released the slot, so no controller could be added again |
| 5. The idle request | - | not run | no client with selective suspend in these guests |
| Every system list 1 names | 2000, ME, Vista, 7 | not run | only XP and 98 SE were read |

(Coordinator's note at the fold, 2026-11-11: the row for states 2 to 5 says that on 98 SE the record held was the tablet's read each time. The filling state is taken only by an OUT transfer (the description above; `src\hcd_stage.c`), so a read cannot have been held in it: the filling state is shown on XP only, and on 98 SE is not shown by these legs.)

No hold was let go by its 10 s bound: every one by the closure.

## 49.16 (continued) The command stranded at `XhciDmaStaging` 0, completed with an error (2026-11-11, worker ins, day wave)

Branch `w49-ins`, commit `074a96c79` (design record 17 section 4.13.9's new paragraph and Revision 18; `src\hcd_cfg.c`, `src\hcd_io.c`, `src\hcd.h`). The owner's decision of 2026-11-11 (the decisions row "Phase 49, the owner's decision of 2026-11-11 (evening)", last sentence).

- **Where it was.** `HcdIoDeviceDrain` (`hcd_io.c`) returns before `HcdCfgFlushDevice` when the buffer is pinned and staging is off, so the commands queued for the controller thread between the failure and the containment (`SlowIrps`, and selective suspend's deferrals) were never completed: the thread serves nothing on a failed controller, and they have no cancel routine. Worker xpsd's X5 and X6 (`runs/run-49.md`, "49.25") met `cdcnet`'s second ABORT_PIPE there.
- **The change.** Before that return, `HcdCfgFailDevice` (`hcd_cfg.c`) takes the device's queued commands off under the controller lock (the deferral's take shared with `HcdCfgFlushDevice`, now `hcdCfgTakeDevice`; any a cancel routine owns left to it) and completes each `DEVICE_NOT_RESPONDING`, one without a URB `STATUS_UNSUCCESSFUL`, as the shutdown latch's flush does, noted `cfg.stranded`. It reaches the containment, the unproven invalidation and a later stop of the same controller. The transfers on the rings stay pending, as before; at 1 nothing changes.
- **Review.** Its design was reviewed with 49.29's (nothing material on this part); the code, nothing material. Reviewed and revised.
- **Build.** The gated build above (`074a96c79`), x86 and amd64 passed.
- **Leg.** Not run. Its leg (an abort queued at 0 between the failure and the containment, completing once while the transfers stay pending) needs a client that sends an abort in that window, as `cdcnet` did under the HP dock model on XP (49.25's X6 order); this session's guests had none.

QEMU processes of this worker: XP 167904 (19:00 to 20:10, QMP quit); 98 SE 157460, 166060, 165340, 178240, 176172, 158400, 178840 (each ended by the guest's Shut Down), 173800 (QMP quit at 20:10). None left running.

## 49.33 The USB audio function still open a few seconds after the controller's enable: reproduced, not yet explained (2026-11-11, worker audop; handed back mid-task)

Branch `w49-audop` from `3.5.0.0` at `a7a472fd6`; **no tracked file changed, no commit**. Development host A, QEMU 11.1.2 (`asm4242-20261110-docks`), WHPX, Windows 10 22H2 x86, `-smp 2`, 2048 MB, ports 48760 to 48762, one guest at a time; seven other workers' guests and builds ran on the host throughout.

**What was asked.** 49.4's finding: with QEMU's `usb-audio`, a stick, a hub and a mouse on `qemu-xhci`, a removal of the controller a few seconds after its enable was vetoed by the audio function (`USB\VID_46F4&PID_0002&MI_00`, veto type 5, `PNP_VetoOutstandingOpen`) 4 of 4 on `xhci98` and granted 4 of 4 on Windows' own xHCI driver. How long the function stays open on each driver, what holds it, and whether `xhci98` is slow or wrong at something.

**The plan and its review.** Reviewed before anything was run (the plan files in the scratch folder; round 1, seven points, all taken; round 2, one point taken: host stamps of guest lines are receipt times and carry no bound; nothing else material). An addition (the hypothesis below and its four tests) was sent for review and had not been answered at the hand-back.

**Step B: both columns reproduced, and the `xhci98` column is not constant.** Worker whpx's single-shot sequence unchanged (its tree and packages copied to `E:\xhci98-scratch\w49\audop\exp`: disable, 15 s, enable, the stick's 4 MiB copy, the remove by `DC.EXE`, SetupAPI's `DIF_REMOVE`; the same plug order: the audio device by the leg, then the stick, a hub on port 3 with a mouse behind it, and the `usb-uas` object on port 4 that never attaches), with one addition: a small guest tool (`AUDP.EXE mark`) writes the guest's clock to COM1 when `pnputil /enable-device` has returned and when the remove begins.

| Run | Controller's driver | Enable returned to remove asked (guest clock) | Answer | The remove's log section |
|---|---|---|---|---|
| `B1-xhci98-single` | xhci98 (`xhci98.sys` sha256 `a9c79ef2...`, the package of whpx's `DC.EXE` runs) | 0.77 s (17:38:01.421 to 17:38:02.187) | restart asked | 17:38:02.436 to 17:38:08.031, 5.60 s, "Query-removal was vetoed by USB\VID_46F4&PID_0002&MI_00\1-0000:00:1c.0:00.0-1&00... (veto type 5: PNP_VetoOutstandingOpen)" |
| `B2-inbox-single` | Windows' own | 0.63 s (17:44:10.553 to 17:44:11.182) | done | 17:44:11.369 to 17:44:11.494, 0.13 s |
| `B3-xhci98-single` | xhci98, the same package | 0.71 s (17:52:12.937 to 17:52:13.642) | **done** | 17:52:13.859 to 17:52:14.083, 0.22 s, "Query-and-Remove succeeded" |
| `B4-inbox-single` | Windows' own | 0.87 s (17:58:52.371 to 17:58:53.240) | done | 17:58:53.459 to 17:58:53.647, 0.19 s |

So the remove in these runs comes well under a second after the enable has returned (the earlier section's "a few seconds" was not measured), and on `xhci98` the same sequence on the same package was vetoed once and granted once. With 49.4's runs: `xhci98` vetoed 5 of 6 at no wait, Windows' own driver granted 6 of 6. The difference between the drivers stands as a tendency; "always vetoed on `xhci98`" does not.

QEMU's `info usb` at the plug (`plug.log`): the audio device on port 1 (12 Mb/s), the stick on port 2 (5000 Mb/s), the hub on port 3, the mouse on 3.1, the `usb-uas` object on port 4. The leg's `audio-play` check fails on Windows 10 by the kit's design (WAVCHK's `/name USB` does not match "Speakers (Audio Device)") and each leg's verdict is FAIL for that reason alone; neither is a reading.

The host's receipt stamps of the two marks (`serial-b2.stamped`, a 20 ms file poller) came 0.74, 0.13, 0.54 and 0.97 s after the guest-clock text they carry: the guest clock's offset and the line's delivery together, not separable, so no ordering between guest and host events finer than about a second is claimed anywhere.

**Not done: everything after step B.** No cycle, no capture, no window table, no qemu-flavour run, no handle list. Prepared and untested in a guest: the cycle branch of the leg script (one start holding many disable/enable/probe cycles, the probe a `CM_Query_And_Remove_SubTree` on the controller with `CM_REMOVE_UI_NOT_OK`), `AUDP.EXE`'s watch, tree and handle list, QEMU's per-device packet capture (`pcap=`, present in this QEMU: read from `-device usb-audio,help`) wired to the four devices, the readers `pcapread.py` and `cyc.py`, and a control that empties Windows' three device sounds. The exact recipe is in `E:\xhci98-vm\p49\audop\STATE.md`, section 6.

**A hypothesis, not a finding.** Windows plays its device-arrival sound when the devices come back at the enable; if the USB audio endpoint already exists when a later device arrives, the sound would play through the USB audio device itself and keep a stream open on it for the sound's length and the audio engine's own close delay. It would fit what is known (vetoed on both drivers with a sound playing; never vetoed with the audio device and no hub or mouse; granted from 30 s on; the varying answer on `xhci98`), and two earlier records lean the same way (`run-22.md`, the uninstall on 7 x86: the function held by a user-mode handle, gone after `net stop audiosrv`; `run-36.md`, "most likely a system sound"). Nothing read today tests it. What would confirm it: `SET_INTERFACE` to a non-zero alternate setting and isochronous OUT records on the audio device inside a vetoed window, and no veto with the three sounds emptied. What would refute it: a vetoed window with no streaming, or vetoes that stay with the sounds off.

Read in the code, not measured: `xhci98`'s controller thread enumerates the root ports one after another, and a hub's first answer to Windows waits, bounded at 5 s, until its ports are enumerated (record 13 section 5.7), so devices behind a hub reach Windows in a later batch than the root-port devices. Whether that spacing differs from Windows' own driver's, and whether it matters here, is unread.

**Build.** One gated x86 `qemu` build of the unchanged tree (slot `gated.1`, `XHCI98_SELFTEST_SKIP_OK=1`, 17:21 to 17:34): "BUILD + GATES PASSED (qemu)", exit 0 (`E:\xhci98-scratch\w49\audop\logs\build-qemu.log`); its `xhci98.sys` kept at `E:\xhci98-scratch\w49\audop\qemu\xhci98.sys` (sha256 `1d391a69...`) for the trace run, not yet used.

**Discarded.** `discard-B1-plugfault` (17:19): my own edit of the scratch harness broke both plugs, so the guest had neither the audio device nor the stick; fixed, the run counts for nothing.

Evidence: `E:\xhci98-vm\p49\audop\rm\runs\B1-xhci98-single`, `B2-inbox-single`, `B3-xhci98-single`, `B4-inbox-single` (`serial-b2.log` with `ctrl-remove-log`, `serial-b2.stamped`, `plug.log`, `cmdline-b*.txt`), `E:\xhci98-scratch\w49\audop\logs\`. QEMU processes: `E:\xhci98-vm\p49\audop\rm\pids.txt`, each ended by its leg (one, pid 141716, with the discarded chain).

## 49.33 (continued) The USB audio function open after an enable: the package's own tray notice and Windows' notification sound hold it (2026-11-11, worker audop2)

Branch `w49-audop` from `3.5.0.0` at `a7a472fd6`, carried on from the section above; **no tracked file changed, no commit**. Development host A, QEMU 11.1.2 (`asm4242-20261110-docks`), WHPX, Windows 10 22H2 x86, `-smp 2`, 2048 MB, QMP 48760, VNC 48761, one guest at a time; about fourteen other workers' guests and builds ran on the host throughout. Every leg is a fresh overlay of worker whpx's starting point, the earlier section's kit (`E:\xhci98-scratch\w49\audop\exp`), the same plug order (the audio device, then the stick, then a hub on port 3 with a mouse behind it, and the `usb-uas` object on port 4 that never attaches). xhci98 legs install whpx's package (`xhci98.sys` sha256 `a9c79ef2...`, release flavour, the sources of this base) with `INSTALL /console ... /usb`; in-box legs install nothing. Each leg's own verdict is FAIL by design of the cycle branch (the release checks are not reported) and means nothing here.

**Method.** One start holds many cycles (`XCYC.TXT`): the tree complete, 60 s, `pnputil /disable-device` of the controller, 15 s, then the guest tool `AUDP.EXE wp`: it starts `pnputil /enable-device` itself (T0 just before), watches the devnodes and the active `KSCATEGORY_AUDIO` interfaces about every 0.2 s, and N s after pnputil has returned asks `CM_Query_And_Remove_SubTree` on the controller with `CM_REMOVE_UI_NOT_OK` (the probe); a granted probe is followed by `pnputil /scan-devices`. QEMU's per-device capture (`pcap=`) of the audio device, the stick, the hub and the mouse; the readers `cyc.py` (per cycle), `isogaps.py` (isochronous bursts), `isoxc.py`, `isoxc2.py`, `isofit2.py`, `isomp.py` (the bursts' PCM, left channel, cross-correlated and fitted with known sounds by least squares). Times "+x s" on the bus are host clock from the host's receipt of the T0 line, and a probe's begin and end on that scale are the receipts of its COM1 lines; a receipt carries a delivery delay that is not bounded, so every interval between a guest event and a bus event below is receipt-relative and carries no bound. Only intervals on one clock are measured as such: on the bus (a burst's length, a burst's end to the stream's close) and on the guest (pnputil's return, a probe's own duration). Plan reviewed before anything ran (rounds on each addition, each closed with nothing material), and this write-up reviewed and revised.

**The window table** (mode w; "vetoed" = `CR_REMOVE_VETOED` with veto type 5, `PNP_VetoOutstandingOpen`, the veto name `USB\VID_46F4&PID_0002&MI_00\...` in all but three, `R12` cycles 3, 7 and 8, whose name came back empty; wait = after pnputil returned):

| Driver, install | Device sounds | 0 | 1 | 2 | 5 | 6 | 7 | 8 | 9 | 10 | 15 | 30 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| xhci98, default | on | | | vetoed 7 of 7 | vetoed 3 of 4 | vetoed 2 of 2 | vetoed 2 of 2 | vetoed 2 of 2 | granted 2 of 2 | granted 1 of 1 | | |
| xhci98, default | off | | | vetoed 1 of 2 | vetoed 2 of 2 | | | | | | | |
| xhci98, `/nofaster` | on | | | granted 3 of 3 | granted 3 of 3 | | | | | | | |
| in-box | on | granted 1 of 1 | granted 1 of 1 | granted 3 of 3 | granted 2 of 2 | | | | | granted 1 of 1 | granted 1 of 1 | granted 1 of 1 |
| in-box | off | | | granted 2 of 2 | granted 2 of 2 | | | | | | | |

Runs: `C1-xhci98-scout` (2, 5, 10), `R7-xhci98` (sound on 2, 5, 2; off 2, 5, 2, 5), `R9-xhci98` (2 x4, 5 x2), `R12-xhci98-edge` (7, 9, 6, 8, 9, 6, 8, 7), `R11-xhci98-nofaster` (2, 5 x3 each), `C1-inbox-scout` (0, 1, 2, 5, 10, 15, 30), `R7-inbox` (on 2, 5, 2; off 2, 5, 2, 5). The x axis is the wait after pnputil returned; what decides is when the notice's stream closes (below), so a late enable (pnputil returning after 8.95 s in `R9` cycle 5, its notice already playing) can be granted at 5 s. A vetoed probe took 5.20 to 5.53 s to answer (guest clock), every one; a granted one on xhci98 0.08 to 0.23 s with no stream (`R7` cycle 6 and all of `R11`), 2.27 to 4.89 s when the stream closed while it waited (four); on the in-box driver 0.09 to 0.67 s, once 21.2 s (`R7-inbox` cycle 4, sound off, no audio stream at all: something else delayed that removal; not followed).

**What is on the bus.** Under xhci98 with the default install, after every enable but one in 24 cycles (all but `R7-xhci98` cycle 6): `SET_CONFIGURATION` at about +0.4 s, `SET_INTERFACE` interface 1 alternate 1 at +1.70 to +2.45 s (four late enables later), one burst of 2650 isochronous OUT packets (192 bytes, one a millisecond; 2.65 to 3.37 s on the host clock as the host's load varied), and `SET_INTERFACE` alternate 0 9.4 to 9.6 s after the burst's last packet (+14.0 to +15.0 s). A granted probe that began inside that hold returned right after the alternate 0 (`C1` cycle 3: probe +11.27, alternate 0 +14.10, grant +14.31; `R9` cycle 5: +14.03, +16.07, +16.35). Under the in-box driver, in 14 of 14 cycles, **no** `SET_INTERFACE` alternate 1 at all between the enable and the probe; the only stream of a cycle follows the rescan after a grant: one 1.71 s burst, alternate 0 within 2 ms of its end, bit-identical from cycle to cycle.

**What the sound is.** Each candidate file of Windows' media folder was played once by `System.Media.SoundPlayer` in the in-box start and captured on the bus; xhci98's burst fitted against each: "Windows Notify System Generic.wav" (the scheme's `Notification.Default`) fits with gain 1.000 and residual 0.0000 in a sound-off cycle (`R7-xhci98` cycle 7); Hardware Fail, Background, Hardware Remove, Notify Messaging, Foreground and Ding leave 0.91 to 1.00. With the device sounds on, the burst is that sound (gain 0.77 to 0.82) mixed with "Windows Hardware Insert.wav" (gain 0.37, 14 to 20 ms later), residual 0.17 to 0.23. Played by `SoundPlayer` the same file closes its stream 2 ms after its end, as do one and two overlapping plays of Hardware Insert 400 ms apart on xhci98 and in-box alike (8 plays): so the 9.6 s hold is not in the file, not in overlap and not in xhci98's isochronous path for `SoundPlayer` playback; this notice's playback produces it in this setup, and whether that playback would hold the same under Windows' own driver is untested.

**What shows it.** QMP screendumps after each enable (`R9-xhci98`, `shot-NN-SS.png`): at about +3 and +4 s a Windows notification from **"xHCI98 USB Tree"**, the package's own tray tool (`XHCITR.EXE`, started at logon by the install): "A USB device can run faster - QEMU USB Mouse runs at Full Speed (12 Mbit/s) behind a Full Speed hub; it can run at High Speed (480 Mbit/s). Click here for what to try." (cycles 1 and 3 looked at; gone by +9 s). `tasklist`: `XHCITR.EXE` runs in the xhci98 legs and not in the in-box legs, where nothing of the package is installed. This is the tool's documented "Can run faster" notice (`xhcitr\README.TXT`): the emulator's hub is Full Speed only and its mouse can run at High Speed.

**The control** (`R11-xhci98-nofaster`): the same package installed with `INSTALL /console ... /usb /nofaster`. Before the cycles the guest read the setting in force as off (the user's `HKCU\Software\xHCI98\XHCITR` `FasterNotice` absent, the computer's `HKLM\Software\xhci98` `TrayFasterNotice` 0) with `XHCITR.EXE` running. Waits 2, 5, 2, 5, 2, 5: **granted 6 of 6**, in 0.08 to 0.23 s; no `SET_INTERFACE` alternate 1 between any enable and its probe; no notice on the screendumps; `setupapi.dev.log` held 853 lines before every enable and after every probe (Windows did no device installation at the enables).

**The edge** (`R12-xhci98-edge`, default install, waits in mixed order): 6 vetoed 2 of 2, 7 vetoed 2 of 2, 8 vetoed 2 of 2, 9 granted 2 of 2 (in 4.33 and 4.89 s). In all eight cycles the answers fit one reading: on the receipt scale the stream's close came 5.49 to 10.9 s after the probe-begin line in the vetoed cycles and 2.96 and 4.15 s after it in the granted ones, and every vetoed probe answered after 5.2 to 5.5 s of its own (guest clock). Read together (an inference across the two clocks): a removal on Windows 10 waits about 5.3 s for the open audio function to close, is granted if the hold ends inside that wait and refused if it lasts beyond it. With this notice, over all the runs, a removal asked 2 to 8 s after pnputil returned was refused in 20 of 23 valid cycles (granted: `R7` cycle 6 with no notice stream, `R9` cycle 5 after a late enable, `R13` cycle 2 at the 8 s edge) and one asked 9 or 10 s after was granted, 3 of 3, slowly; the notice's stream opened +1.7 to +2.5 s and closed +14.0 to +15.0 s after the T0 line's receipt, while pnputil returned 1.2 to 1.5 s after T0 on the guest's clock. (Two enables in R12 whose stream began late, +5.97 and +6.81 s, moved the close with it; the rule held.)

**The perturbation control** (`R13-xhci98-q`, default install, mode q: no watch, no capture, no screendumps): two valid cycles, wait 2 vetoed (after 5.24 s) and wait 8 granted (after 5.16 s, at the edge). Cycles 3 and 4 are void: their disable was first refused (below), and after the retry pnputil's enable returned in 62 and 47 ms with the controller still flagged for a restart (`DN_NEED_RESTART` in its status), so the controller had not been cycled; their vetoes are not counted. Two readings cannot show that the watch and the capture leave the timing alone; they show only the same answer at 2 s and the edge near 8 s without them.

**After a rescan.** Twice a disable 60 s after a grant and its rescan was itself refused (`pnputil` 3010: `C1-xhci98-scout` cycle 4, which stopped that series, and `R13` cycle 3, whose retry after an enable and 30 s left the controller uncycled, voiding cycles 3 and 4; the retry is a harness fault of this run). In `C1` the capture shows why in part: the rescan's devices brought the same notice sound (Notify System Generic + Hardware Insert by fit, residual 0.18), and that stream stayed at alternate 1 for about 98 s, through the refused disable, instead of 9.5 s. Why the hold lasted so much longer there is unread. No disable was refused in the in-box starts or in `R11` (12 and 5 disables that followed a rescan).

**Who holds the handle.** The guest tool's handle list (every File handle whose object name starts with a `USB\VID_46F4` devnode's PDO name) found 0 hits in every list: while Windows' Alarm01 looped (its validation, mode p), in twelve lists inside an enable's window (mode h) and after the plays; each time 270 to 310 File handles could not be opened. The list does not reach the process that holds the audio function's handle, so the holding process is **unresolved** by this means; what the stream belongs to is read from the bus, the fingerprint and the screen instead.

**The sound control (device sounds off).** Emptying `DeviceConnect`, `DeviceDisconnect` and `DeviceFail` in HKCU (read back empty) did not remove the vetoes on xhci98 (3 of 4 vetoed); it removed the Hardware Insert part of the burst only after a delay (`R7-xhci98` cycle 4, about half a minute after the change, still carried it; cycles 5 and 7 did not). The device-arrival sound is therefore not the holder; the earlier hypothesis (the arrival sound through the returning endpoint) is refuted, and so is the reading that two overlapping arrival sounds play.

**The arrival order** (watch, guest ms since T0, 16 xhci98 and 14 in-box cycles): under xhci98 the root hub starts at 0.16 to 0.20 s and every device (the composite and its audio function, the stick and its disk and volume, the mouse and its HID devnode) starts in one batch at 0.95 to 1.70 s, and pnputil returns at 1.23 to 1.86 s; under the in-box driver the stick and its volume start at 0.34 to 0.81 s, the audio function at 0.41 to 0.98 s (once 1.25 s), the HID devnode last at 0.98 to 1.42 s, and pnputil returns at 0.34 to 0.98 s, before the HID devnode in most cycles. (Four slow enables left out of these ranges: pnputil returning after 8.95 and 10.77 s on xhci98, 4.59 and 4.73 s in-box; host load not excluded.) xhci98's single batch is its root hub's first answer waiting for its ports by design (record 13 section 5.7). That spacing differs, but it is not what holds the function: the notice is.

**Verdict.** No fault of `xhci98.sys` shown, and not the driver's arrival spacing: in these legs the difference between the two columns is that the xhci98 legs install the package, whose tray tool raises its "can run faster" notice for the emulator's mouse behind the emulator's Full Speed hub at every enable, and Windows plays that notification's sound through the only audio endpoint, the USB audio device, holding its stream about 9.5 s past the sound; a removal waits about 5.3 s for the open audio function and is refused when the hold lasts beyond that wait. With the notice off (`/nofaster`, a valid control) the same driver was granted 6 of 6 at 2 and 5 s, like Windows' own driver, which runs no tray tool in these legs. Measured, not inferred: the notice on the screen, its sound on the bus by fingerprint, the hold on the bus, the control. Inferred, not measured: the "wait about 5.3 s, then refuse" reading (it joins guest-clock durations to receipt-relative bus times), that the hold is Windows' notification playback keeping its audio client open (the same file played by `SoundPlayer` closes at once), and which process holds the handle (the list cannot see it). Not tested: a notification's sound under Windows' own driver (no tray runs there), so that the same hold would follow under it is not shown; nor any other notification than this one. Whether the tray should raise the notice again at every enable of the controller is a design question for the owner, not a driver change. The release legs carry no audio device, so their remove step is not touched by this.

**Not run, and why.** The qemu-flavour trace (the plan's R10) and further single-shot legs (R5) were dropped once the notice-off control removed the vetoes and no fault of `xhci98.sys` was shown; B1 to B4 and 49.4's single-shot runs had no capture; a possible explanation, not shown, is the same mechanism: a removal at 0.6 to 0.9 s after the enable is refused when the notice's hold outlasts its wait; how B3 was granted is not known (no notice that time, a query through before the stream opened, or a close inside the wait are all open). The scratch `qemu` build (`scratch\qemu\xhci98.sys`) was not used.

Evidence: `E:\xhci98-vm\p49\audop\rm\runs\C1-xhci98-scout`, `C1-inbox-scout`, `R7-xhci98`, `R7-inbox`, `R9-xhci98`, `R11-xhci98-nofaster`, `R12-xhci98-edge`, `R13-xhci98-q` (`serial-b2.log` with the cycle logs, `*.pcap`, `shot-*.png`, `shots.log`, `plug.log`); readers and kit in `E:\xhci98-scratch\w49\audop\exp`; QEMU processes in `E:\xhci98-vm\p49\audop\rm\pids.txt`, each ended by its own leg.

## 49.4 (the leftovers) The 9x copy check in 98 SE and ME guests; the remove under TCG on 10 x86 (2026-11-11, worker leg2)

Branch `w49-leg2` from `3.5.0.0` at `ab3d629a5`; no tracked file changed (readings only). Development host A with eleven other workers' guests and builds running beside these (their load is not mine to state; every wall time below is with it). Ports 48840-48859. The plan was reviewed and revised before the first 9x leg (a cluster or attribute byte alone does not prove a removal, so the copy's own date stamp is read too; `FC.TXT` is sent on the differences path only).

**Vehicle.** The tracked legs, unchanged: `scripts\vm-matrix\run-leg.ps1 -Leg w98se-console`, `-Leg me-console` and `-Leg w10x86-console -ForceTcg`, on the worktree's scripts (`leg\LEG9X.BAT` `ab258f26...`, `leg\LEGNT.CMD` `a506166a...`, `leg\XHCOPY.CMD` `c0b7eba4...`, all as at `52e5404e2`). Package: the x86 release kit the 49.4 copy-verdict worker built at `52e5404e2` (`E:\xhci98-scratch\w49\whpx\kit2\PKG`, copied; `xhci98.sys` `c8a12844...`, `SETUP.EXE` `e5fb74a8...`, `XHCIINST.EXE` `87416b74...`; the files report version 3.4.3.0, the tree's number before the bump), with NUSB 3.3's `USBDRV\NUSB33\` added from 49.1's kit for 98 SE's stick. Starting points copied: `w98se` and `me` from `E:\xhci98-vm\p48\legs343\work\start` (emulator `asm4242-20261027-fwpad`, the one they were saved under), `w10x86` from `E:\xhci98-vm\p49\whpx\rm\start` (`asm4242-20261110-docks`). Configurations `E:\xhci98-scratch\w49\leg2\cfg\` (`c10.psd1`; `c9-<stick>.psd1`, one per stick; `run-*.psd1` the per-run copies with their ports); `-KeepDisk` on every 9x leg so the stick could be read afterwards.

**The sticks** (`E:\xhci98-scratch\w49\leg2\stk\mkstick9x.py`, from the leg's 128 MiB FAT16 stick `q35-bases\shared\nvme-fat.qcow2`, 2 KiB clusters; `rdstick.ps1` reads a stick's root directory after a leg):

| Stick | Made as | qcow2 |
|---|---|---|
| std | the leg's own stick, no RAND.BIN | `nvme-fat.qcow2` |
| prev | RAND.BIN already there, the leg's exact 4 MiB (`System.Random` seed 4312, `5261ca5f...`), attributes read-only, hidden, archive (`23`), stamped 2026-11-11 12:00:00; free space as it was | `d70bed20...` |
| full | no RAND.BIN; every free cluster marked bad (`FFF7h`, both FATs) | `7d589c8d...` |
| short | no RAND.BIN; 1024 free clusters (2 MiB) kept, the rest marked bad | `3c579b0e...` |
| dir | RAND.BIN an empty folder | `cb255119...` |

**The 9x legs.** One 9x guest at a time beside the TCG leg, then two at a time; TCG, one processor, 512 MB.

| Run | System | Stick | Verdict | Wall time | `stick-copy` | The stick afterwards |
|---|---|---|---|---|---|---|
| `w98se-std` | 98 SE | std | PASS | 746.8 s | PASS | RAND.BIN attr `20`, the CD file's stamp (17:58:52) |
| `w98se-prev` | 98 SE | prev | PASS | 667.2 s | PASS | RAND.BIN attr `20`, stamped 18:11:20 (the CD file's), not the fixture's 12:00:00 |
| `w98se-full` | 98 SE | full | FAIL, `stick-copy` only | 871.9 s | FAIL "no RAND.BIN after the copy" | no RAND.BIN |
| `w98se-short` | 98 SE | short | FAIL (`stick-copy`, and boot 2 stalled) | 1500.4 s | FAIL "no RAND.BIN after the copy" | no RAND.BIN |
| `w98se-short-r2` | 98 SE | short | FAIL, `stick-copy` only | 718.5 s | FAIL "no RAND.BIN after the copy" | no RAND.BIN |
| `w98se-dir` | 98 SE | dir | FAIL (`stick-copy`, and boot 3 stalled) | 1942.1 s | FAIL "RAND.BIN from before is a folder" | - |
| `w98se-dir-r2` | 98 SE | dir | FAIL, `stick-copy` only | 713.6 s | FAIL "RAND.BIN from before is a folder" | the folder there, attr `10`, untouched |
| `me-std` | ME | std | PASS | 388.0 s | PASS | - |
| `me-prev` | ME | prev | PASS | 337.4 s | PASS | RAND.BIN attr `20`, the CD file's stamp |
| `me-full` | ME | full | FAIL, `stick-copy` only | 355.6 s | FAIL "no RAND.BIN after the copy" | no RAND.BIN |
| `me-short` | ME | short | FAIL (boot 1 stalled before any check) | 1063.3 s | not reached | - |
| `me-short-r2` | ME | short | FAIL, `stick-copy` only | 356.6 s | FAIL "no RAND.BIN after the copy" | no RAND.BIN |
| `me-dir` | ME | dir | FAIL (boot 1 stalled before any check) | 1063.5 s | not reached | - |
| `me-dir-r2` | ME | dir | FAIL, `stick-copy` only | 396.1 s | FAIL "RAND.BIN from before is a folder" | the folder untouched |

Every other check of every leg that reached it passed (`driver-file`, `list-current`, `stick-seen` E:, `list-removed`; the exit codes those the leg allows).

- **A destination from before.** On both systems the read-only, hidden RAND.BIN with the leg's own bytes was removed and the copy's own file compared; the file left is the copy's (attributes `20`, the CD file's date stamp). The old script would have compared the fixture's file and passed whatever the copy did.
- **A failed write, with no space and with 2 MiB free.** On both systems COMMAND.COM's COPY, with no free space (full) or 2 MiB free for the 4 MiB file (short), says "Insufficient disk space" (98 SE's screen, `w98se-short\b2-last.png`, with ATTRIB's "File not found - E:\RAND.BIN" before it) and leaves no destination, so the check fails on "no RAND.BIN after the copy". How much COPY wrote before it gave up (or whether it refused before writing) was not read, so a partial write is not shown; the FC-differences branch (a partial file left) did not occur on either system and stays unread in a guest.
- **A folder of that name** fails and is left alone, on both.
- **Not run:** a RAND.BIN from before that cannot be removed (a write-protected stick): the leg plugs its stick by `drive_add` with no read-only option.
- **The stalls, each once and not again on a fresh overlay.** `w98se-short` boot 2: after the failed copy the hidden DOS box sat at the 20 s `CHOICE` for 15 min (the screen's clock moving), so the uninstall never started. `w98se-dir` boot 3 (after the uninstall): the guest stopped at 19:13 by its own clock, ring 0 busy (`EIP` `c00514a1`, `c00039ac`, `c000b404` in three samples; no module map taken), no poller. `me-short` and `me-dir` boot 1: ME's switch-off after the install did not finish (a black screen with a cursor, the leg's off-retry screens). Each reran clean (`-r2`). Not looked into further; none is in the copy check's path.

**The remove under TCG (10 x86).** `tcg-w10x86-console`, `run-leg.ps1 -Leg w10x86-console -ForceTcg`, a fresh overlay of the `w10x86` starting point, `tcg,thread=multi`, `-smp 2`, 2 GB: **PASS** in 5262.1 s (87.7 min; boots 2140, 2573 and 548 s; 69 min at 49.4's first forced-TCG leg, so read the time as the loaded host's), `accel` `tcg,thread=multi`, `accelOwn` `whpx`, `accelForced` true, every check PASS (`stick-copy`, `ctrl-disable` after "stick volume idle: 10 of 10 samples with no transfer in round 1", `ctrl-enable` "E: after 0 s", `stick-copy-enable`, `ctrl-remove`, `ctrl-rescan` "E: after 0 s", `stick-copy-rescan`, `list-removed`), exits install 3, list 0, uninstall 3, list-final 0. **`ctrlRemove`: done.** pnputil: "Device removed successfully."; the device-install log part: `[Delete Device - PCI\VEN_1B36&DEV_000D&SUBSYS_11001AF4&REV_01\4&23F93F77&0&00E0]`, section 18:41:38.737 to 18:41:40.066, "dvi: Query-and-Remove succeeded", "Exit status: SUCCESS"; then "No devices were found" for the controller and the rescan brought the stick back as E:. So the remove has now been seen to complete under TCG on this leg (one run); the earlier refusal (the stick's volume, veto type 6) did not recur here.

Evidence: `E:\xhci98-vm\p49\leg2\rm\runs\<run>\` (`result.json`, `serial-b*.log`, `*.png`, `stick-1.qcow2` of the 9x legs), host logs `E:\xhci98-scratch\w49\leg2\logs\<run>.log` and `chain-*.log`. QEMU processes: `E:\xhci98-vm\p49\leg2\rm\pids.txt` (every start and exit, by PID).

## 49.20 The BIOS-presented USB disk after the takeover: the emulator readings of design record 37 (2026-11-11, worker leg2)

Readings only; no tracked file changed, nothing of an interception built (the owner's decision of 2026-11-06, `runs/run-47.md`, "47.12 The P14s's EDD reading ...": gather data, take the emulator readings, no production `XHCII13.VXD`; the roadmap line: an interception VxD's design goes to the owner before any code). The plan (what is read, how, what each reading can and cannot show) was reviewed and revised before the first start; what that changed is in the readings' limits below.

**Which of record 37's readings an emulator can take without new code, and which were taken.** Of the readings sections 4.3, 4.4, 5.1, 5.2 and 5.4 owe:

| Record 37 | Reading owed | Here |
|---|---|---|
| 5.1 | how IO.SYS's retry loop and the drive-walking component treat a quick AH=80h failure in place of the firmware's slow one | **taken** (B1), on 98 SE and ME, twice each, by answering the requests at the firmware's entry from the host |
| 4.3 | how a drive letter maps to its INT 13h unit "at that time" | **the MS-DOS half, at two end points** (B2): the firmware's real-mode prompt and Windows' MS-DOS box; the moment itself and IOS's route owed |
| 5.4 | that the bus, device and function `xhci98.sys` reads are the firmware's numbers | **partly** (B3): the firmware's numbers against the emulator's and Windows' PCI enumerator's after the start; what `xhci98.sys` itself reads owed |
| 4.4 | a static VxD's `Device_Init` before the controller's start; `Exec_Int` there reaching the firmware's INT 13h through the same chain | owed: needs a VxD of this project's in the guest |
| 5.2 (a) | the step-aside predicate held against the trace; whether `ISP_GET_FRST_NXT_DCB` / `IOS_Find_Int13_Drive` may be called from a V86 hook | owed: needs a VxD (and a V86 hook, whose design goes to the owner first) |
| 5.2 (b) | the chain-placement experiment, a handler added after IOS's start | owed: needs a VxD that changes the chain (design to the owner first) |
| 5.4 | `Directed_Sys_Control` from the WDM start context on both systems | owed: needs the link coded in `xhci98.sys` and a VxD |

**Vehicle.** Development host A, eleven other workers' guests and builds beside these; TCG, `-cpu pentium3`, one processor, 512 MB, q35, the emulator of 47.12's trace (`q35-bases\bin\asm4242-20261025-2230`, QEMU 11.1.2 `v11.1.2-239-g02dda40-dirty`, `da60139f...`), SeaBIOS rel-1.17.0 (`bios-256k.bin` `ae6f6aa9...`). A fresh overlay of each q35 base; this tree's x86 release kit (`52e5404e2`, `xhci98.sys` `c8a12844...`) installed by `XHCIINST.EXE /dir 98-me /console /yes /nopause /nostor98` (98 SE with `/storage` too), exit 3 on both; the transfer folder as a FAT disk (98 SE: AHCI port 1; ME: the PIIX slave), so unit 81h, D:; the standard 128 MiB FAT16 stick (`nvme-fat.qcow2`, MBR) on `qemu-xhci` port 1 at power-on, unit 82h, as in 47.12 (which used a 256 MB stick: its counts are a history to compare with, not a criterion). Each system was installed once and shut down cleanly, and that image kept read-only (98 SE `b98\inst.qcow2` `0869dabe...`; ME `bme\inst.qcow2` `3781d4ca...`, with `AutoScan=0` in `MSDOS.SYS` so a start after a stopped shut down runs no ScanDisk); every C and F start below is a fresh overlay of it with a fresh overlay of the stick. QEMU's `usb_xhci_reset` and `usb_msd_cmd_submit` events to a log with UTC stamps (`-msg timestamp=on`). Launcher, helpers and client: `E:\xhci98-scratch\w49\leg2\b\` (`lb.ps1`, `g2.py`, `qmp.py`, `shots.py`, `run1.ps1`, `i13.py` `56a5b312...`, `i13sum.py`, `desk.ps1`; the guest's batch files in `b\xfer\`).

### B1: a quick failure in place of the firmware's slow one (record 37 section 5.1) [debugger]

**How.** QEMU's gdbstub and a small host client of this worker's (`i13.py`; nothing in the guest): a breakpoint at SeaBIOS's INT 13h entry, `f000:e3fe` (`IVT[13h]` read `f000:e3fe` at the first hit; the entry's own instruction, `E9 17 F0`, a near jump to `f000:d418`, and the `CFh` at `f000:ff53` read from the open SeaBIOS image). Every hit for unit 82h is logged with its registers and the INT frame at `SS:SP` (other units only sampled: the first two, the parameter functions, V86-mode resets and every 500th); a request passed on continues at `f000:d418` (the jump the entry would make), and a one-shot breakpoint at the frame's return address logs `AX` and `CF` when the immediate caller gets control back. In **C** (control) the client only logs. In **F** it answers, without entering the firmware, the requests to unit 82h made in V86 mode once a `usb_xhci_reset` stamped after the first V86-mode hit is in QEMU's log (the takeover's reset, correlated in C: the third reset of each start, 1.9 to 3.5 s after the first V86 hit, and 0.5 to 1.1 s before the first request to 82h, over the eight starts), as section 5.1 lists: for 02h (the only function that came) CF set, AH=80h, AL=0, written into the frame's FLAGS and `AX`, then `IRET` at `f000:ff53`; every write read back, any failure leaving the guest stopped (none happened). The guest's time held stopped by the client is subtracted for the `run` figures (about 2.5 % of a start). Screens every 5 s (samples). Then, at the desktop, `T.BAT`: each letter listed and, where the stick is, a 1 MiB file written and compared with `FC /B`.

| Start | System | Requests to 82h after the takeover | Each answered | IO.SYS's attempt counter (BP at the entry) | The window (first request to last answer, host) | First BOT command after it | The desktop (host s after power-on, 5 s samples) | The stick at the desktop |
|---|---|---|---|---|---|---|---|---|
| C | 98 SE | 15 reads (fn 02h, CHS 1 sector) | by the firmware, after 5.10 s, AX=0C00h CF=1 | 5 4 3 2 1, three times | 20:11:10 to 20:12:26, 76 s | 4 s after | 100 to 105 | F: write/read PASS; E: listed (C2) |
| C2 | 98 SE | 15 reads | the same | the same | 20:28:54 to 20:30:11, 77 s | 4 s after | 110 to 115 | E: listed |
| F | 98 SE | 15 reads | by the client, at once (0.0 s), AX=8000h CF=1 arriving at the caller | 5 4 3 2 1, three times | 20:16:20, 0.04 s | 4 s after | 25 to 30 | F: write/read PASS; E: listed |
| F2 | 98 SE | 15 reads | the same | the same | 20:36:28, 0.05 s | 3 s after | 30 to 35 | - |
| C | ME | 20 reads | by the firmware, 5.10 s, AX=0C00h CF=1 | 5 4 3 2 1, four times | 21:04:08 to 21:05:50, 102 s | 2 s after | 115 to 120 | - |
| F | ME | 20 reads | by the client, at once, AX=8000h CF=1 | 5 4 3 2 1, four times | 21:12:53, 0.04 s | 2 s after | 15 to 20 | E: and F: write/read PASS |
| C2 | ME | 20 reads | by the firmware, 5.10 s, AX=0C00h CF=1 | 5 4 3 2 1, four times | 21:29:46 to 21:31:29, 103 s | 2 s after | 115 to 120 | E: and F: write/read PASS |
| F2 | ME | 20 reads | by the client, at once, AX=8000h CF=1 | 5 4 3 2 1, four times | 21:36:34, 0.06 s | 4 s after | 25 to 30 | E: and F: write/read PASS |

- **IO.SYS treats the quick failure as it treats the slow one, as far as this shows.** The same requests came, in the same groups (98 SE three, ME four, as 47.12 counted with a larger stick), each tried five times with the counter running 5 to 1, the resets between attempts (fn 00h) going to unit 00h (`DX=0100h`), never to 82h; no request of another function to 82h, and no extended request. In F every answer reached the immediate caller (`0070:0522` on 98 SE, `0070:04f8` on ME, IO.SYS's segment: the frame at the firmware's entry was IO.SYS's own for every request after VMM's start) with CF set and AH=80h; what IO.SYS's code above did with it is read from what followed, not seen.
- **No new screen.** No MS-DOS critical-error box or other new screen in F's samples (5 s apart: a short-lived one could fall between them); the boot went on to the desktop, about 75 to 85 s (98 SE) and 90 to 100 s (ME) sooner, about the window's length.
- **The protected-mode service came as before.** The first USB storage command of the protected-mode disk after the window came 2 to 4 s after it in every start, C and F alike; the commands are counted from QEMU's events and are not a count of INT 13h requests IOS served (unknown here).
- **The letters afterwards.** In C2 and F on 98 SE and in the later starts on ME, the BIOS letter (E:) answered `DIR` from the protected-mode disk with no request reaching the firmware, and the protected-mode letter (98 SE F:; ME E: and F:) read and wrote. In the one start right after the stick's driver was installed, it was otherwise: on 98 SE an access to E: went to the firmware and ended "General failure reading drive E", each try stopping the machine for minutes (its clock still, 3 to 4 minutes a prompt); on ME the desktop of that start stayed in the firmware's time-outs for 10 minutes (no census on that start, so which unit was not read). Not looked into further.
- **Limits.** The answer is given at the firmware's entry, below IO.SYS's own hook and IOS's layer, where design record 37's option (a) would answer at the head of the V86 chain; what IO.SYS's disk code receives is the same AX and CF, what the layers in between see is not. The arming is later than section 5.4's production point (the takeover's reset rather than before it) and discharges nothing of 5.4. One emulator and one firmware, whose own answers are slow errors; a firmware that fails differently, or the P14s's, is not read by this. The host was loaded; the timings are one host's, repeated once (C2, F2).

### B2: the letters and the units, MS-DOS's half (record 37 section 4.3)

MS-DOS's own drive data table (INT 2Fh AX=0803h, each entry's unit at +4 and drive at +5), with the boot drive (INT 21h AX=3305h), read by `DEBUG.EXE` typed by hand (a script redirected into `DEBUG` stopped the real-mode prompt and was abandoned; `b98\d3-ddt-*.txt`, `bme\b2-ddt-win.txt`):

| Where | Entries (unit, drive) | Boot drive | Windows' drive | Swap file |
|---|---|---|---|---|
| 98 SE, "Command prompt only", the stick in, the firmware's | (00h A:) (00h B:) (80h C:) (81h D:) (82h E:), terminator after five | C: | - | - |
| 98 SE, an MS-DOS box at the desktop, after the takeover | the same five | C: | `windir=C:\WINDOWS` | `C:\WINDOWS\WIN386.SWP` present |
| ME, an MS-DOS box at the desktop | the same five | C: | `windir=C:\WINDOWS` | `C:\WINDOWS\WIN386.SWP` present |

So MS-DOS's table names the stick's BIOS unit 82h as E: before and after the takeover, unchanged; the protected-mode letters (98 SE F:, ME F:) are not in it. ME has no "Command prompt only", and a start-up disk would be another session, so ME's pre-Windows table is not read. These are two end points; the table at the moment a VxD would read it (`Device_Init`, before the takeover) and IOS's own route (`ISP_GET_DCB`, `DCB_BDD_Int_13h_Number`) stay owed, and the swap file is the file present, not VMM's own view.

### B3: the controller's numbers (record 37 section 5.4)

QMP `info pci` on 98 SE at the firmware's prompt and again at the desktop, and on ME at the desktop: the root port `00:1c.0` (`1b36:000c`) with secondary and subordinate bus 1 and the controller `1b36:000d` at `01:00.0` throughout; between the two 98 SE readings only the root port's I/O window changed (`[0xc000, 0xbfff]` to `[0xf000, 0x0fff]`). Windows 98 SE's PCI enumerator keys the controller as `Enum\PCI\VEN_1B36&DEV_000D&SUBSYS_11001AF4&REV_01\00E000` (the bridge's device-function `E0h` and the controller's `00`, no bus number in the id), the bridge as `...VEN_1B36&DEV_000C...\BUS_00&DEV_1C&FUNC_00`. So on this emulator 98 SE did not renumber the bus (before and after read); on ME only the desktop was read (`01:00.0`; that it is the firmware's assignment is inferred, not read before Windows); what `xhci98.sys`'s own `IoGetDeviceProperty` returns on 9x is in no note of the snapshot and stays owed.

### What stays owed (49.20)

- Record 37's VxD readings: 4.4 (both clauses), 4.3's IOS route and the table at the moment, 5.2 (a) and (b), 5.4's `Directed_Sys_Control` and the driver's own numbers. A probe limited to `Device_Init`, IOS and MS-DOS queries and logging, with no V86 hook, would take 4.3 and 4.4 (what `xhci98.sys` itself reads for 5.4 needs a note in the driver or a debugger reading instead); anything that installs a V86 hook or changes the chain is section 5.2's, whose design goes to the owner first (the question is in the worker's `QUESTIONS.md`).
- Not mine: the field reports `docs/using/edd-reports.md` asks for, the handoff's diagnostic notes read on hardware, and every reading on real firmware (the P14s's answer to a quick failure, its ownership-change SMI).

Evidence: `E:\xhci98-vm\p49\leg2\b98*`, `bme*` (`i13-<tag>.log` the census, `trace-<tag>.log`, `serial-<tag>.log`, `shots-<tag>\`, `*.png`, `*-infopci-*.txt`, `*-ddt-*.txt`, `cmdline-<tag>.txt`); QEMU processes `E:\xhci98-vm\p49\leg2\pids.txt` (every start and stop by PID; `b98` tag `d3`, PID 173132, powered off by the guest at 20:10:41 by `b98\g.log`, got its STOP line late; the ME guests that stopped inside their shut down were quit by QMP, as each line says).

## 49.32 (was RESUMESTALL.1) Windows 2000, two processors: enumeration stops after a resume from hibernation - first pass, not concluded (2026-11-11, worker stall)

Worker stall, branch `w49-stall` at `a7a472fd6` (no commit, no build). Development host A, QEMU
`asm4242-20261110-docks`, TCG, headless, one two-processor guest at a time. Stopped by the coordinator before a
stall was reproduced. Evidence root `E:\xhci98-vm\p49\stall\smp\` (`trial-<name>.log`, `dbt-<tag>.txt`,
`srt-<tag>.txt`, screens), scripts `E:\xhci98-scratch\w49\stall\b\`; the plans and their reviews are beside them
in the scratch folder.

### Vehicle

A copy of the earlier worker's `snapB.qcow2` (the Windows 2000 SMP guest, base driver: `xhci98.sys` qemu flavour
`bbc3f322...`, `xhciuas.sys` release `5328d936...`), its `FH.BAT` recipe unchanged (Explorer on D:, 40-round
copy-compare loops on the UAS disk D: and the stick F:, `SetSuspendState`, readings 30 s apart, a `usb-kbd` probe
120 s after the resume's start). Each trial starts from a fresh copy of the overlay. Each trial's first serial
lines give the installed file's size and the `fc` result against the base file.

### Trials

| Trial | Host side | Result |
|---|---|---|
| S1 (b1 to b2) | as it was (about 60 % busy, 18 QEMU processes) | **pass**: readings 1 to 3, 12 then 13 devices, 0 problem codes; quit after the third |
| S2 (s2a to s2b) | the same | **pass** to reading 4; root hub QDR 10 s after the resume began, the stick's REMOVE 2.8 s after its SURPRISE_REMOVAL |
| S3 | the same | **pass** to reading 4 |
| S4 | the same | **pass** to reading 4; no "Delayed Write Failed" box on its screen |
| S5 | the resumed QEMU bound to one host processor | **void**: the image took 2 min 44 s to load, the probe keyboard was plugged before the guest ran, "Resuming Windows..." still on screen at the 540 s quit |
| S6 | the resumed QEMU bound to two host processors | **pass** to reading 4, slowly: the first listing took 107 s; root hub QDR 22 s after the resume began; the probe keyboard queried and started |

A first run of S1 is void (the script typed a wrong command line; nothing ran). 0 stalls in 5 valid trials,
against the earlier worker's 3 in 6 with the same recipe on a busier host. Why is not shown.

### What was read in the earlier worker's evidence

- Screens 200 s after the resume: a "Windows - Delayed Write Failed" box for
  `\Device\HarddiskVolume4\SLF.BIN`, the stick's loop file, in both stalls looked at (T2, T3); none in the passes
  T1, T4, U1, U2. U3 (no UAS device) has a "Delayed Write Failed" box "for the file F:" and passed, with the
  stick's START 25 s after its PDO. T5, T6, U4, V1 to V3 and FH1 not read.
- Traces: in T2 and T3 the last requests Windows sent xhci98 are device-relations queries at device PDOs; the
  root-hub query that follows 3 s later in the pass T4 never comes. The listing's capabilities walk passes the
  controller, the root hub and the USB devices and does not return.

### Reading

Nothing is established about the cause. A lost delayed write on the dropped stick's old volume goes with both
stalls looked at and is not sufficient (U3). Whether it is a trigger or a consequence, and whether the UAS driver
is needed, is open; the next trials (a write-only loop on the stick, then with no load on the UAS disk, then with
no UAS device) are prepared and not run. No path that strands one request while others run was found by reading
`src\uas\` and the bus's removal path; the places still to look at are listed in the plan reviews.

### Aids made

- `b\w2kmem.py`: reads a QMP `pmemsave` image of this guest as data (loaded-image list, processes, threads with
  wait objects and stack words as image+offset, IRPs and who holds them, kernel queues, device and driver
  objects). Checked on a capture of a booted guest. No code or image header of a closed binary is read.
- `b\trial.sh`: on a first reading not ended in 150 s the guest is paused and its registers and memory saved
  twice, 60 s apart.

### Process notes

- Plan reviewed three times before and during the work (answers on disk); every round left limits on what a
  capture may be taken to show, none stopped the reproduction.
- Binding the resumed QEMU to one host processor does not imitate a loaded host; it breaks the resume.

## 49.32 (continued) The stall reproduced and read down to Fastfat (2026-11-11, worker stall2)

Worker stall2 took 49.32 over in the same folders; branch `w49-stall` still at `a7a472fd6` (no commit, no build).
Same vehicle, same base driver, one two-processor guest at a time. Host A about 87 to 88 % busy (15 QEMU
processes), comparable to the conditions of the earlier worker's 3 in 6. No affinity restriction in any trial
below.

### The earlier screens read (step 1)

"Box" is a "Windows - Delayed Write Failed" box on the screen 60 s or 200 s after the resume, or at the end.

| Trials | Stick's loop | Box | Outcome |
|---|---|---|---|
| T2, T3 | running | yes, `\Device\HarddiskVolume4\SLF.BIN` | stall |
| FH1 | running | yes at +60 s, `\Device\HarddiskVolume5\.`; boxes were left from three recoveries, so its origin is not certain | stall |
| U3 | running, no UAS device | yes, "for the file F:" | pass |
| T1, T4, U1, U2, U4, S2, S3, S4, S6 | running | no | pass |
| T5, T6, V1 to V3 | idle or no stick | no | pass |

### Trials

| Trial | Recipe | Stick and UAS images | Box | Result |
|---|---|---|---|---|
| W1 (w1a to w1b) | FW (the stick's loop write-only) | carried from S6 | yes at +60 s, `\Device\HarddiskVolume4\SWF.BIN` | **pass** to reading 4 |
| C1 (c1a to c1b) | FH, unchanged | carried | **no**, at +60 s nor at the stall | **stall**: reading 1 begun 18:18:26, never ended (15 min, then quit); the probe keyboard's PDO created 18:19:17 and never queried; stick wr_bytes 25.2 MB then 50.4 MB in the 44 s before the power-off |

So the box is neither necessary (C1) nor sufficient (W1, U3) for the stall.

### C1's captures read (memory values only)

Two `pmemsave` images 60 s apart (`mem\C1-1.bin`, `C1-2.bin`), read with `b\w2kmem.py`, `b\res.py` (walks the
ERESOURCE list) and `b\fo.py` (names file and device objects). Structures by their `ntddk.h` layout (IRP, stack
location, ERESOURCE, FILE_OBJECT, DEVICE_OBJECT, wait blocks) and pool tags; no code of any closed binary read.

1. Four threads wait on SynchronizationEvent `81D782E8` (pool tag `ReEv`), the exclusive-waiters event of
   ERESOURCE `81E97804`, which lies in the extension of Fastfat volume device `81E97600` (the stick's old FAT
   volume): FH.BAT's own `cmd.exe` (`81D8A020`) and the stick loop's (`81D79AE0`), an `svchost.exe` thread
   (`81DE7080`) and a System worker (`81DA3580`); each holds an IRP_MJ_CREATE at that device. The third `cmd.exe`,
   the UAS disk's loop, was Ready inside Ntfs. `81E97804`: owned exclusive, OwnerThreads[0] = `820A1DA0`, 4
   exclusive waiters. FH.BAT's PROB.EXE never started: the batch's `if exist F:\PROB.EXE` is its blocked create, so
   "the listing never returns" is that create, not PnP.
2. Fastfat's global ERESOURCE (at `Fastfat.SYS+32B8`): owned exclusive by the same `820A1DA0`; 2 shared waiters,
   critical-queue worker threads running Fastfat work items.
3. `820A1DA0`, a System worker, holds IRP `BCF94E70`: IRP_MJ_PNP / IRP_MN_SURPRISE_REMOVAL with Fastfat's
   `81E97600` as the current location; the next location, at Ftdisk volume `81EF6030`, carries Fastfat's completion
   routine, and the IRP is back at Fastfat's location. It waits on NotificationEvent `81E96C48`, inside a pool block
   tagged `CcSc` (a cache manager shared cache map) that points at FILE_OBJECT `81E96CF8`: an unnamed stream on the
   same Ftdisk volume, pointer count 3, handle count 0.
4. Capture 2, 60 s later: the same owners, waits and counts.
5. No thread was identified that would set `81E96C48`. The pool scan found no live read or write IRP; the only
   pending internal-device-control IRPs at xhci98 were xhciuas's standing stream requests and the HID interrupt
   reads; a UAS WRITE(10) at xhciuas in capture 1 was gone in capture 2 (the UAS loop went on). The old stick PDO
   had its SURPRISE_REMOVAL at 18:17:59.58 and its REMOVE at 18:18:07.53 (`dbt-c1b.txt`); every PnP request xhci98
   received was completed.

Reading, within those limits: in both captures a System worker handling the stick volume's surprise removal owns
Fastfat's global and volume resources exclusively and waits on an event in that volume's shared cache map; creates
on the volume and two Fastfat work items are blocked behind it. The probe keyboard's enumeration stops after its
PDO is created; that this is the same worker's doing is inferred, not shown. No request of xhci98 or xhciuas was
identified in the chain. What would set the event, and what began the state, are not known. The driver's own part
is that the resume path drops a Bulk-Only drive by design (design record 20 section 4.3.3), so a mounted stick's
volume is surprise-removed after such a resume.


### A surprise removal with no hibernation (arms A and B)

Reviewed as a plan first. A fresh copy of `snapB` and of the stick and UAS images (the copies taken at the
take-over, `smp\stick-at-stall2.qcow2`, `uas-at-stall2.qcow2`) each trial; `xfer\PU.BAT`: FH's loops (Explorer on
D:, copy-compare loops on D: and F:), then "now" and, in place of the hibernation, a QMP `device_del` of the stick;
the readings run `PROB.EXE` from the transfer drive directly (`PNP2.BAT`) and never touch F: (a changed recipe).
Probe keyboard on xHCI port 3 60 s after the removal. A: the stick on xhci98 (`b\trial3.sh`). B: the stick on the
machine's EHCI controller, under Windows 2000's own `usbehci`, `usbhub20` and `usbstor` (`kit\lsmp4.ps1 -StickBus
ehci`); the UAS disk and the probe stay on xhci98. Stick bytes written by QMP blockstats at "now" and 3 s later.

| Trial | Stick on | Stick wr_bytes at the removal, then 3 s later | Probe PDO / first query | Box | Result |
|---|---|---|---|---|---|
| A1 (xa1) | xhci98 | 50.44 MB, 51.82 MB (writing) | 18:53:18.337 / 18:53:18.337 | | **pass** to reading 4 |
| B1 (xb1) | EHCI | 36.50 MB, 45.08 MB (writing) | 19:01:09.088 / 19:01:09.139 | | **pass** |
| A2 (xa2) | xhci98 | 50.45 MB, 50.45 MB (still) | 19:09:26.004 / 19:09:26.004 | | **pass** |
| B2 (xb2) | EHCI | 50.45 MB, 50.45 MB (still) | 19:17:35.293 / 19:17:35.344 | | **pass** |
| A3 (xa3) | xhci98 | 50.39 MB, 50.45 MB | 19:25:49.657 / 19:25:49.707 | | **pass** |
| B3 (xb3) | EHCI | 26.93 MB, 35.58 MB (writing) | 19:33:41.485 / 19:33:41.536 | yes, `\Device\HarddiskVolume3\` | **pass** |

An earlier A1 is void (its tag matched an old run's, whose `.stop` files ended the log followers at once).
0 of 3 on each arm: not reproduced in those trials (weak negative evidence; the removal came at the same point of
the loops each time, only partly while the stick was being written). B3 shows Windows' own stack giving the same
"Delayed Write Failed" box for the volume root as FH1 showed.


### Hibernations with the stick on EHCI (arm C), and xHCI controls on restored images

The FH recipe unchanged (`b\trial2.sh`, `RESTORE=1`: snapB, the stick and UAS images restored each trial). C: the
stick on EHCI under Windows 2000's own stack, the UAS disk and the probe on xhci98.

| Trial | Stick on | Stick wr_bytes about 50 s and 10 s before the power-off | Unsafe Removal box | Box (Delayed Write) | Result |
|---|---|---|---|---|---|
| H1 (xh1a to xh1b) | EHCI | 8.59 MB, 50.45 MB | **none**; F: present, its loop window carried on | no | **pass** |
| H2 (xh2a to xh2b) | xhci98 | 7.41 MB, 50.45 MB | yes (the drop) | no | **pass** |
| H3 (xh3a to xh3b) | EHCI | 0, 25.61 MB | **none**; F: present | no | **pass** |
| H4 (xh4a to xh4b) | xhci98 | 11.86 MB, 50.39 MB | yes (the drop) | no | **stall**: reading 1 begun 20:09:23, not ended; probe PDO created, not queried; captures `mem\H4-1.bin`, `H4-2.bin` |
| H5 (xh5a to xh5b) | xhci98 | 0, 50.45 MB | yes (the drop) | not read | **stall**: reading 1 not ended in 150 s; captures `mem\H5-1.bin`, `H5-2.bin` |
| H6 (xh6a to xh6b) | xhci98 | 25.17 MB, 40.42 MB | yes (the drop) | not read | **pass** |

H4's captures show C1's signature with this boot's addresses: System worker `820A1020` owns Fastfat's global
ERESOURCE (2 shared waiters) and the stick volume's ERESOURCE `81E93224` (4 exclusive waiters: a System worker, an
svchost thread, two cmd.exe) exclusively; it holds an IRP_MN_SURPRISE_REMOVAL at Fastfat volume device `81E93020`
(next location Ftdisk `81EEF4B0`, completion `Fastfat.SYS+16DA`) and waits on NotificationEvent `81E93B48`, inside
a `CcSc` block pointing at an unnamed stream FILE_OBJECT on that Ftdisk volume; the pool scan found no read or
write IRP; unchanged 60 s later. H5's two captures show the same ownership and wait signature (owner `820A1020` of Fastfat's
global ERESOURCE, 1 shared waiter, and of the volume's ERESOURCE `81E92E84`, 4 exclusive waiters).

With the stick on Windows' own stack, no surprise removal was seen across the hibernation in 2 of 2 (no Unsafe
Removal box, F: and its loop carried on; the old devnode's continuity itself was not read). On xhci98 the resume
keep drops the stick (why 08, Bulk-Only) every time, and with the FH recipe this vehicle stalled in C1 and H4 and
passed in H2.


### Windows' own stack removing the stick after the hibernation (arms E', E'')

Reviewed as a plan first. The FH recipe with the stick on EHCI under Windows 2000's own stack, the UAS disk and the
probe on xhci98, images restored; only the RESUMED machine differs (`kit\lsmp4.ps1 -StickPort/-StickSerial`, for
the resume start only through `b\s2.sh`'s `LSMPB`). E1: the same port, another serial (W49STK02): Windows kept the
old devnode (the listing still shows `USB\VID_46F4&PID_0001\W49STK01`, no Unsafe Removal box): not an exposure, so
the stick was moved as well. E': EHCI port 2 and serial W49STK02. E'': EHCI port 2, the same serial W49STK01 (the
same instance presented again on another port, as xhci98 presents it again on the same one).

| Trial | Arm | Removal seen | Result |
|---|---|---|---|
| E1 (xe1a to xe1b) | serial only | no: the old devnode kept | pass (not an exposure) |
| E4 (xe4a to xe4b) | E' | Unsafe Removal box | **pass** |
| E9 (xe9a to xe9b) | E' | Delayed Write Failed for the volume root; Found New Hardware | **pass** |
| F1 (xf1a to xf1b) | E'' | Delayed Write Failed for `\Device\HarddiskVolume3\SLF.BIN` | **stall**: reading 1 begun 21:55:59, not ended; the probe keyboard's PDO created at xhci98, **no PnP request to it**; captures `mem\F1-1.bin`, `F1-2.bin` |
| F4 (xf4a to xf4b) | E'' | Unsafe Removal box | **pass** |
| F5 (xf5a to xf5b) | E'' | (not read at +60 s) | **stall**: reading 1 not ended in 150 s; the probe's PDO created, no PnP request to it; captures `mem\F5-1.bin`, `F5-2.bin`: the same signature (System worker `820A1020` in IRP_MN_SURPRISE_REMOVAL at Fastfat `81E7F020`, owner of Fastfat's global ERESOURCE and of the volume's ERESOURCE `81E7F224`, 3 then 4 waiters, waiting on an event in a `CcSc` block; no read or write IRP) |

F1's captures show the same signature with Windows' own stack under the volume: System worker `820A1020` holds
IRP_MN_SURPRISE_REMOVAL at Fastfat volume device `81E7D0C0` (a ten-location IRP: the EHCI storage stack is
deeper; next location Ftdisk `81F0FA50`, completion `Fastfat.SYS+16DA`), owns the volume's ERESOURCE `81E7D2C4`
exclusively (3, then 4 exclusive waiters) and Fastfat's global ERESOURCE exclusively, and waits on NotificationEvent
`81E7CA48` inside a `CcSc` block; no read or write IRP found; unchanged 60 s later. xhci98 served only the UAS
disk, the tablet and the probe in that machine.

Void, not counted: E5 and E6 (a `tasklist` call failed under load, the script took the live guest as ended, and
the next trial's start copied the images over that guest's disk while it ran: E5's guest restarted after it, E6
was overwritten mid-trial), H7 and H8 (never ran), H10 and F2 (the Run-box typing missed; trial2.sh now checks
the recipe's start line and types again once, and F4 and F5 waited 210 s after the cold start). The scripts now use
`Get-Process` and refuse to touch the images while the guest's QMP port is listening.

### Verdict

- **Reproduced**, unrestricted, on the base driver: with the stick on xhci98 and the unchanged recipe, 3 stalls in
  5 hibernations (carried images: C1, 1 of 1; restored images: H4 and H5, 2 of 4 with H2 and H6), each captured,
  all three with one signature. No affinity restriction was used in any trial.
- **The stalled guest, link by link** (memory values, C1, H4, H5, F1 alike): user-mode creates on the stick's old
  FAT volume (FH.BAT's own `if exist F:\...`, the stick loop, an svchost thread) and a System worker wait for that
  volume's ERESOURCE; its exclusive owner, a System worker, holds the volume's IRP_MN_SURPRISE_REMOVAL inside
  Fastfat, owns Fastfat's global ERESOURCE too, and waits on an event in the cache map of the volume's own stream
  file. The event's signaller was not identified; no live read or write IRP was identified by the pool scan, and
  no request of this project's drivers was identified in the reconstructed chain. The same ownership and wait
  signature at both captures of each stall (waiter counts can grow). The probe device's enumeration stops after its PDO is created; that this worker is what PnP's
  enumeration queues behind is inferred, not shown. "The listing never returns" was the batch's own create on the
  dead volume, not PnP.
- **With Windows' own stack removing the stick** after the hibernation (E'', the same serial on another EHCI port):
  2 stalls in 3 (F1, F5; F4 passed), both captured with the same signature; with a new serial (E') 0 of 2.
- **Whose**: F1 and F5 reproduce the Fastfat/cache-map wait signature with the stick served and surprise-removed by
  Windows 2000's own `usbehci`/`usbhub20`/`usbstor` (xhci98 still served the UAS disk, the tablet and the probe).
  xhci98 performing that removal is therefore not necessary for this signature. The initiating cause remains
  unresolved. What xhci98 does contribute is exposure: its resume keep drops a Bulk-Only drive by design on the resume re-initialization path (design
  record 20 sections 4.3.2 and 4.3.3), where Windows' own stack carried the same stick across the hibernation 3 of 3
  (H1, H3 functionally: no Unsafe Removal box, F: and its loop carried on; E1 with the old devnode listed). Without a hibernation a removal under the same load did not stall (A 0 of 3, B 0 of 3; weak
  negatives), and E' (a new instance id) 0 of 2; small counts, not compared as rates.
- Not shown: what sets or should set the cache-map event (Fastfat's and the cache manager's own code is not read),
  why the removal after a resume stalls and the hot unplug did not, and how often Windows' own stack drops a stick
  across a hibernation on real machines (not read here).
- No fix was made: no fault in this project's code was identified, and the removal xhci98 performs is its
  designed behaviour. The mitigation in reach is a design question for the
  owner (QUESTIONS.md Q1): keep a Bulk-Only drive across the resume as the UAS drive is kept.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** on 2026-11-13, after 49.34, the
two-processor 2000 hibernation leg was run on this release with `XhciResumeKeepBot` at `1`, its default, and at `0`:
no stall in any run, so the stall was not reproduced, and these runs do not show the keep's effect. The owner closed
49.32 on 2026-11-13 with no known-limitation text: the keep is in, the stall was not seen in 12 runs that day at any
setting, so its effect is neither confirmed nor contradicted by them; it is reopened if the stall is seen again.

## 49.7 (continued) The networking view and the connection manager's networking: the latch at a 98 SE end, and Driver Verifier on `xhcinet6.sys` (2026-11-11, worker net2, day wave)

Worker net2, development host A, branch `w49-net2` from `3.5.0.0` at `ab3d629a5`. No code change: the branch carries
no commit.

**What was left.** The night's 49.7 (above) read the networking node on 2000, 7, Vista, 8, 8.1 and 10 x86, ping, the
report and the `XhciCmNet` toggle under Driver Verifier on XP, a resume on 7 x86 and Titan Ridge; its "Left" was the
latch with 98 SE as the end that latches, not reached because the NHI had Interrupt Line `FFh` on 98 SE. That line is
this package's own: `XHCIPFW.VXD`'s interrupt-line step (record 32 section 12.10, task 46.11) writes `FFh` on the NHI
and its NHI-only downstream port at every start unless `XhciPfwNhiLine` is 0, and with ACPI on 98 SE then gives the
NHI an IRQ from its line (`runs/run-46.md`, "46.11 Steering enabled"; `runs/run-48.md`, 48.26's 98 SE reading on
IRQ 10). So the latch at a 98 SE end is reachable on this model with `XhciPfwNhiLine` 0; this section reads it, and
adds Driver Verifier on `xhcinet6.sys` (8 to 10's NDIS 6 driver; the night's Verifier cell was XP, `xhcinet.sys`).
The legs' plan was reviewed and revised before they ran (the pass condition of the loss path below in particular).

**The package under test.** `src\cm` changed since the night's base (`5458cd114..ab3d629a5`: 49.11, 49.12, 49.18), so
this tree was built: gated x86 `build-driver.cmd all` of `ab3d629a5` (slot gated.1, `XHCI98_SELFTEST_SKIP_OK=1`):
BUILD + GATES PASSED (release qemu) (`E:\xhci98-scratch\w49\net2\gated-x86-all.log`). The gated wrapper's PATH held
no Python, so `src\pfw\build.cmd` skipped `XHCIPFW.VXD` ("no Python on PATH for its link checks", as in the night's
log) and `make-package.ps1` staged neither it nor `S98VSD.VXD`; both were then built by their own `build.cmd` with
Python on PATH (`check-pfw: obj\PFWNEW.VXD passes`, `check-vsd: obj\S98NEW.VXD passes`) and the package staged again
(`make-package.ps1 -Flavor release -Arch x86`, `ConvertTo-FamilyLayout`), the tools of this build at the root.
Kit `E:\xhci98-vm\p49\net2\kit`. SHA-256 (`E:\xhci98-scratch\w49\net2\hashes.txt`):

| file | SHA-256 |
|---|---|
| `xhcicm.sys` (98-me and nt-x86, the same image) | `5AF942B43C27669D93FD5553CD1B3AC1E7220260D9F5548B2A449CDC11CDE19C` |
| `xhcinet.sys` | `7D7A8BBCDF106FF83720D82F72FACA580C9D8461BDF8FD001ECC11D3D2D27466` |
| `xhcinet6.sys` | `D877050CE5B7A0A79899FE48D8F82E458D0D39D8793CD4B002C05FDD29835F6F` |
| `xhci98.sys` | `007C0F2DCD124ECB9BE2034D6A3B87CEFCD298857A9D885CF6F5EF4F05082A5B` |
| `XHCIPFW.VXD` | `83B2B6A891883E108707C94C936B0B7BC12C4E13CBC9CFE590586696CC8373B1` |
| `XHCITR.EXE` | `0AB7CD6916313E1F82D0EDB46990E55DEAD7A9B9FBFDC5A570290825966ED058` |
| `XHCIINST.EXE` | `4FE4CC776EC6C50C98263374CA62BCCFBE7CA63FACC594B3F8C45F0CF53884F6` |
| `xhcicm.sys`, qemu flavour (`src\cm\objchk_qemu\i386`, kit `XHCICMQ.SYS`) | `600E9669FC3C36D58FECBF236049B7EFF03A9A55E4DFBE44901B44820402B41A` |

Emulator `E:\xhci98-vm\q35-bases\bin\dock-20261110-bar0` (`usb4-xdomain`) through `launch-q35.ps1 -Emu`, `-Usb4 tb3ar`
at both ends (Alpine Ridge NHI `8086:15BF`, firmware CM), the cable `usb4-xdomain` over TCP 48822 (98 SE the server).
Images, fresh overlays of the q35 bases: `w98b` (98 SE, `I98.BAT`: `INSTALL.BAT /console /yes /nopause /thunderbolt
/tbnet`, exit 3, two cold starts), `w81b` (8.1 x86, the same in an administrator console opened by Win+X, A and Yes;
the publisher prompts clicked; exit 3, restarted). The cell's ends are fresh overlays of those. 98 SE one vCPU, 8.1
two, TCG. Scripts `E:\xhci98-scratch\w49\net2\kit\` (`cell.sh`, `n1.sh`, `n2a.sh`, `n2b.sh`), guest batch files in
the kit (`VFY6.BAT`, `NET1.BAT`, `PFW0.REG`, `L98.BAT`, `DEAD*.REG`, `P98.BAT`, `FC98.BAT`, `HASH.BAT`). VMs and logs
`E:\xhci98-vm\p49\net2\` (`pids.txt`).

### 8.1 x86 under Driver Verifier on `xhcicm.sys` and `xhcinet6.sys`, 98 SE at the other end (cell N1)

A: 98 SE (overlay of `w98b`), QMP 48820, the cable's server. B: 8.1 x86 (overlay of `w81b`), QMP 48821. At B, in an
administrator console: the machine-wide `XhciCmNet` 1 and the NHI restarted (`NET1.BAT`; the emulator's new NHI
serial at each start, as the night recorded), then `verifier /standard /driver xhcicm.sys xhcinet6.sys`
(`VFY6.BAT on`: "Enabled Flags Value: 0x000209BB", special pool, IRQL checking, pool tracking, I/O verification,
deadlock detection, DMA checking, security checks, miscellaneous checks, DDI compliance checking; "Verified
drivers: xhcicm.sys xhcinet6.sys"), restarted.

| Step | Read | Verdict |
|---|---|---|
| `verifier /query` after the start | "Name: xhcicm.sys, loads: 1, unloads: 0"; "Name: xhcinet6.sys, loads: 1, unloads: 0" | both under Verifier |
| installed files | B `certutil -hashfile`: `xhcicm.sys` `5af942b4...cde19c`, `xhcinet6.sys` `d877050c...835f6f` (the kit's); A `FC /B` of `XHCICM.SYS` against the kit's: "FC: no differences encountered" | the package under test |
| addresses (APIPA) | A `169.254.18.10`, B `169.254.140.54` | |
| A pings B, 6 | 6 of 6, 9-37 ms | |
| A's report | "Thunderbolt networking - up", "XhciCmNet: 1 (on)", "Adapter MAC: 76-A3-C1-CD-B6-00", "Other computer's MAC: 4A-18-7C-6F-4D-50", "Errors: 0 sending, 0 receiving", "Network driver: xhcinet.sys, MTU 1500", "Ring interrupt: off (checked every clock tick)" (the 98 SE default: no line), "Kind: xHCI98", "Logins: 1 sent, 0 failed; 1 received, 0 refused" | up |
| B pings A, 6 | 6 of 6, 10-36 ms | |
| B's report | "Thunderbolt networking - up", "XhciCmNet: 1 (on)", "Adapter MAC: 4A-18-7C-6F-4D-50", "IP address: 169.254.140.54 (255.255.0.0)", "Other computer's MAC: 76-A3-C1-CD-B6-00", "Sent: 262 packets", "Received: 23 packets", "Errors: 0 sending, 0 receiving", "Network driver: xhcinet6.sys, MTU 1500", "Link changes: 1 up, 0 down", link "logged in (carries the network)", "Kind: xHCI98" | **pass**: each end shows the other's own MAC |
| stand-alone snapshots | B `TS.DEV` with the networking section, "Adapter MAC: (not included in the snapshot)"; A the same (98 SE's snapshot window waits for OK before `TP98.BAT` goes on) | **pass** |
| `TOG.BAT 169.254.18.10 15BF` at B: `XhciCmNet` 0, NHI "Restarted", 45 s | `devcon status XHCICM\TBNET`: "No matching devices found."; ping "PING: transmit failed. General failure." 4 of 4 | off |
| 1, "Restarted", 90 s | "xHCI98 Thunderbolt Networking ... Driver is running."; APIPA `169.254.140.54` (kept); ping 6 of 6, 14-33 ms | on |
| 0/1 back to back three times (six restarts, each "Restarted"), 90 s | "Driver is running."; ping 6 of 6, 14-41 ms | on |
| `verifier /query` after the toggles | `xhcicm.sys` loads 1, unloads 0; `xhcinet6.sys` loads 5, unloads 4 (one per "on"); "AllocationsFailed: 0", "RaiseIrqls: 0"; no bug check in the cell (about 35 minutes under Verifier) | **pass** |
| after the toggles | B's report "Thunderbolt networking - up", "Adapter MAC: 4A-18-7C-6F-4D-50" (the same MAC after five starts of `xhcinet6.sys`; XP's `xhcinet.sys` took a new one at each start, the night's C2), ping both ways 6 of 6 | up |

Verdict: **pass** (emulator): the networking node, ping and the `XhciCmNet` toggle on 8.1 with Driver Verifier on
`xhcicm.sys` and `xhcinet6.sys`, and 98 SE's node with this tree's package. Evidence:
`E:\xhci98-vm\p49\net2\cells\N1\` (`leg.txt`, `A\serial-A.log`, `B\serial-B.log`, `b-*.png`).

### The latch with 98 SE as the end that latches (cell N1 continued, A = 98 SE, B = 8.1 with Verifier still on)

**Step a: the line given back, the release `xhcicm.sys`.** At A, `PFW0.REG` imported (`VAL98.BAT PFW0`):
`HKLM\System\CurrentControlSet\Services\VxD\XHCIPFW` `XhciPfwNhiLine` 0 and the machine-wide `Services\USB`
`XhciCmRingInterrupt` 1; Windows shut down, a cold start.

| Step | Read | Verdict |
|---|---|---|
| A's report, values | "XhciCmRingInterrupt (advanced): 1 - yes (machine-wide)"; "XhciPfwNhiLine (advanced): 0 - no; default 1 - yes" | taken |
| A's report, the NHI | Device Manager entry "Resources: IRQ 10 (level, shared)"; the PCI configuration read through `xhcicm.sys`: "Interrupt pin INTA, line 10" (the night's: "line not assigned (FFh)") | the line kept |
| A's driver log (in the report) | `cm.irq.value=00000001`, `cm.irq.resource=00011101` (a line, level, shared), `cm.irq.connected=0000003A`, `cm.irq.armed=00001000`, `cm.irq.selftest=00000000` (passed), `cm.net.irq.open=00000001` (the data ring's cause opened) | the ring interrupt and the data ring's on 98 SE |
| ping B 6, the report | 6 of 6; "Thunderbolt networking - up", "Ring interrupt: on (190 wake-ups)", "Errors: 0 sending, 0 receiving" | |
| ping B 60, the report | 60 of 60, 4-43 ms; "Ring interrupt: on (272 wake-ups)" | |
| B pings A, its report | 6 of 6; "Thunderbolt networking - up" | **pass** |

So on this model 98 SE does give the NHI a line once `XHCIPFW.VXD`'s step is off, and the ring interrupt, its
self-test and the data ring's cause all run there under the network.

The harness on 98 SE: a finished DOS window of a batch file that wrote to `COM1` keeps the port, and the next batch
file's write then stops at "Write fault error writing device COM1 / Abort, Retry, Ignore, Fail?" (48.38's harness
fault); from step b each step's window is closed by its system menu when its word has come. Once (after step a)
three such windows stacked up and A was powered off by QMP `quit` (logged in `pids.txt`) and cold-started.

**Step b, first form (`XhciCmQemuIrqDead` 500; the plan's 50 was skipped, step a's reading putting the start's
own claims well above 50).** `L98.BAT DEAD500` at A (the qemu flavour's `xhcicm.sys` over the installed one, the
release one kept as `XHCICM.REL`; `XhciCmRingInterrupt` 1 and `XhciCmQemuIrqDead` 1F4h machine-wide); that run's
own COM1 output was lost to the write fault above, but the next cold start's debug port showed the flavour and the
value in force, and `FC /B D:\XHCICMQ.SYS ...\XHCICM.SYS` "FC: no differences encountered".

- The start (debug port, in order): `cm.irq.value=00000001`, `cm.irq.qemu.dead-after=000001F4`,
  `cm.irq.resource=00011101`, `cm.irq.connected=0000003A`, `cm.irq.armed=00001000`, `cm.irq.selftest=00000000`,
  `cm.irq.suspect=0000000D` with `cm.irq.selftest=00000000` after it (an early suspicion, cleared by its test),
  `cm.net.irq.open=00000001`, `cm.net.link.up`, `cm.net.up`.
- Ten runs of 60 echoes from A (600, all answered, 4-64 ms): no loss path; the report at the end "Ring interrupt: on
  (484 wake-ups)". The driver log reaches the debug port only at a flush (a report's query, `flush.reason`), so the
  log was read after each report.
- Then **A froze** (about 20:14): the 98 SE clock stopped, B's ping "Destination host unreachable", the screen held a
  `PING` window at "Write fault error writing device COM1 / Abort, Retry, Ignore, Fail?" over a snapshot window. The
  processor, sampled six times by QMP `info registers` (`cells\N1\a-hang-regs.txt`): `CS=1008` (16-bit, DPL 3),
  `EIP=00002FF9` every time, `EFL=00023046` (virtual-8086 mode, IOPL 3, interrupts disabled), only `EAX` changing;
  `info pic` (`a-hang-pic.txt`): master `irr=07 imr=b8` (the timer pending, COM1's IRQ 4 masked), slave `irr=06`
  (IRQ 9 and 10 pending), `elcr=0e` (9 to 11 level). So a virtual-8086 program, in the DOS window that was writing to
  COM1, spins with interrupts off; nothing of this project's images is on the processor at the moment read (no
  address in a driver image). A was powered off by QMP `quit`. Not read further (the code is Windows' own).

Recorded as an observation, not a proven driver fault: the COM1 write faults at a 98 SE end came in this task only
with the NHI's line connected (step a and after; none in cell N1, polled, with as many batch files), and 48.38 saw
the same write faults on 98 SE under the data ring's interrupt. Whether the line's interrupt (IRQ 10, level, shared)
delays the serial port's own service enough to time a DOS write out is not read; a reading would need the COM1
traffic without the line on the same image, and with it, side by side.

**Step b, second form**: no DOS window at A at all: the traffic from B (8.1's console pings A, its address resolved
by name), the cable pulled for 30 s and put back at B's end by QMP (`qom-set /machine/peripheral/xd0 plugged
false|true`: ring 0 traffic, which is what the loss watch counts, since the data ring's frames are taken by the poll
whatever the line does), and at A only `C:\XHCI98\XHCITR.EXE /report C:\R.TXT` typed into Run (a Win32 window, no
COM1), whose query flushes the driver log to the debug port (`kit\n2d.sh`).

- Second form, first run (the same overlay, cold-started after the freeze; `XhciCmQemuIrqDead` 500 in force, the
  qemu flavour checked by `FC /B`: "no differences encountered"): A's address `169.254.189.251` (resolved by name,
  `XHCI98`); the start's log as above (`cm.irq.connected`, `cm.irq.selftest`, `cm.irq.suspect=0000000D` cleared by
  its test, `cm.net.irq.open=00000001`); B's ping runs of 120: run 1 and 2 120 of 120; the cable out 30 s and back:
  `cm.net.down=00000103`, `cm.net.link.down`, `cm.net.irq.open=00000001` again, `cm.net.link.up`, `cm.net.up` (no
  loss path: the claims had not reached 500); run 3 120 of 120; **run 4 103 of 120, then no answer**: A's QEMU process
  had exited by itself (about 20:37:45; its overlay last written 20:32:19, the debug port 20:31:55; `qemu-A.out`
  empty; 98 SE runs with `-action reboot=shutdown`, so a guest reset ends the process too). The driver log of those
  last minutes was never flushed, so nothing of the loss path, if it ran, was read.
- Second form, second run, **on a fresh overlay** (`A2`, of `w98b`; QEMU with `-d cpu_reset -D A2\cpureset.log` and
  `-action shutdown=pause` added): `PFW0.REG`, `L98.BAT DEAD500` ("FC: no differences encountered",
  `"XhciCmQemuIrqDead"=dword:000001f4`), Windows shut down, a cold start. A's address `169.254.69.9`. The debug port,
  in order, with the driver's own clock (`cm.ms`):

| `cm.ms` | Line | Reading |
|---|---|---|
| | `cm.irq.value=00000001`, `cm.irq.qemu.dead-after=000001F4`, `cm.irq.resource=00011101` | the ring interrupt asked for, the qemu flavour's dead line after 500 claims, a line (level, shared) |
| 7Ch | `cm.irq.connected=0000003A`, `cm.irq.armed=00001000`, `cm.irq.selftest=00000000` | connected, armed, the start's self-test passed |
| 1138h | `cm.net.irq.open=00000001`, `cm.net.link.up=01030000`, `cm.net.up=01000808` | the data ring's cause opened; the network up |
| | B's ping runs 1 and 2: 120 of 120 each | |
| 5ACF8h | `cm.net.down=00000103`, `cm.net.link.down=00000001` | the cable out at B (30 s) |
| 628FAh | **`cm.irq.suspect=000001F4`** | ring 0's frames of the new login pending with no claim since the 500th: the loss suspected |
| 62967h | **`cm.irq.selftest.fail=00000000`**, **`cm.irq.lost=00000001`**, **`cm.irq.down=00000111`** | the diagnostic self-test failed, the line counted lost, taken down: reason 1 (fall back), end 1 (disconnected), the source proven quiet |
| 62BC4h on | `cm.net.link.up=01030000`, `cm.net.up=01000808` | the network up again, on the poll |
| | B pings A after it: 6 of 6; B's report "Thunderbolt networking - up" | traffic after the fall-back |

  No `cm.irq.storm` line anywhere in the cell; no `cm.net.irq.latched` (the data ring's cause had been shut by the
  link going down before the loss was found, so this path closes ring 0's alone, as the reviewed expectation said);
  `cm.irq.qemu.dropped` is printed only at a stop, which this run did not reach, so that the injection was what
  dropped the interrupts is inferred from the threshold (the suspicion's claim count is exactly 500) and the failed
  test. A2's own report after the fall-back was **not read**: A2's screen was a black 720x400 text-mode screen at
  every screendump from its cold start on (21:07), a power-button shutdown was not taken in 150 s and its overlay
  was not written after 21:02, while B's pings were answered and the driver log kept flushing; the processor sampled
  in the VMM's code (`CS=0028`, `EIP` `C000A881`, `C0053B7A`; `A2\black-regs.txt`). A2 was stopped by QMP `quit`.
  Cause not read (the first A's cold starts showed the desktop; A2 differed by the two QEMU options and by having
  been stopped from the paused power-off).
- Second form, third run, **on a fresh overlay** (`A3`; QEMU with `-d cpu_reset` only): the same setup and the same
  order on the debug port: `cm.irq.connected=0000003A`, `cm.irq.selftest=00000000`, `cm.net.irq.open=00000001` (at
  `cm.ms` C3Bh); B's runs 1 and 2 120 of 120; the cable out at 6D8E7h (`cm.net.down`, `cm.net.link.down`); at 75866h
  `cm.irq.suspect=000001F4`, at 758D4h `cm.irq.selftest.fail=00000000`, `cm.irq.lost=00000001`,
  `cm.irq.down=00000111`; at 759A2h `cm.net.link.up`, `cm.net.up`; no storm line. B pings A after: 6 of 6; B's report
  "Thunderbolt networking - up". Then at A `XHCITR /report C:\R.TXT` from Run, Windows shut down (QEMU exited by
  itself, no reset logged but the two at its start), and the report taken out of the overlay afterwards (`qemu-img
  convert -U` to a raw image in scratch, `7z e ... R.TXT`; `E:\xhci98-scratch\w49\net2\x-A3\R.TXT`): "XhciCmRingInterrupt
  (advanced): 1 - yes (machine-wide)", "XhciPfwNhiLine (advanced): 0 - no", the NHI "IRQ 10 (level, shared)",
  "Thunderbolt networking - up", "State: up", "Other computer's MAC: 4A-18-7C-6F-4D-50", "Errors: 0 sending, 0
  receiving", "Link changes: 2 up, 1 down", **"Ring interrupt: off (fell back to checking every clock tick)"**, link
  "logged in (carries the network)". The shutdown logged no counter dump (98 SE gave no stop before power-off).

Verdict, the latch with 98 SE as the end that latches: **pass** (emulator; 2 of 2 on fresh overlays): on 98 SE
with `XhciPfwNhiLine` 0 the NHI keeps its line, the ring interrupt connects and passes its self-test, the data
ring's cause opens under the network; the qemu flavour's dead line after 500 claims is found as lost at the next
ring-0 traffic (`cm.irq.suspect`, the failed diagnostic test, `cm.irq.lost`), the interrupt is taken down to the
poll (reason fall back, disconnected, quiet proven), the report says "off (fell back to checking every clock tick)",
and the network carries on (B to A 6 of 6, 0 errors). On this path `cm.net.irq.latched` is not printed (the data
ring's cause was already shut by the link going down); the data ring's own latch, with the data ring open at the
moment of the loss, was not read on 98 SE (it was at the XP end in 48.38). That the injection dropped the
interrupts is inferred (the suspicion at exactly 500 claims, the failed test); its counter (`cm.irq.qemu.dropped`)
prints only at a stop, which 98 SE did not give.

**A repeat with the dead line in force under traffic (`A4`, a fresh overlay, `XhciCmQemuIrqDead` 300 by
`DEAD300.REG`, `-d cpu_reset`).** To read again the first form's case (the claims passing the threshold during the
data traffic, before any ring-0 frame), B pinged A for six runs of 120 with no cable pull before the sixth; the
log flushed after runs 2 and 4 showed only the start's lines (`cm.irq.connected`, `cm.irq.selftest`,
`cm.net.irq.open=00000001` at `cm.ms` 14B5h); all 720 echoes answered, no stall, no exit of the process. The cable
out and back after run 6: `cm.net.down` at 179869h, `cm.irq.suspect=0000012C` at 182723h (exactly 300),
`cm.irq.selftest.fail`, `cm.irq.lost=00000001`, `cm.irq.down=00000111`, the network up again; B to A 6 of 6;
A's report (taken out of the overlay after Windows' shut down, `x-A4\R.TXT`): "Ring interrupt: off (fell back to
checking every clock tick)", "Errors: 0 sending, 0 receiving", "Link changes: 2 up, 1 down". **pass** (the third
reading of the loss path on 98 SE). So the dead line itself, in force through many minutes of data traffic, does not
stop the network or the guest (the data ring is taken by the poll whatever the line does); the first `A`'s exit
during its run 4 was not seen again in three runs on fresh overlays.

**B at the end.** `verifier /query` after about four hours under Verifier (18:51 to 22:45: the toggles, the 98 SE
end's cold starts and cable pulls, about 3,000 echoes): "Name: xhcicm.sys, loads: 1, unloads: 0", "Name:
xhcinet6.sys, loads: 5, unloads: 4", "AllocationsFailed: 0", "RaiseIrqls: 0"; no bug check; B shut down by Windows.

### What stays open

- **The 98 SE end's own faults in this cell, not read to a cause (none in this project's images on the processor
  when read):** (1) "Write fault error writing device COM1" in batch files at the 98 SE end from step a on (the line
  connected), none in cell N1 (polled); (2) the first overlay froze once with a virtual-8086 program spinning with
  interrupts off (`a-hang-regs.txt`); (3) the same overlay's QEMU process ended by itself once during traffic with
  the dead line probably in force, no reset logged (that run had no `-d cpu_reset`); (4) `A2` showed a black
  text-mode screen from its cold start on while its network and connection manager ran. (3) was not seen again in
  `A3` and `A4`; (1), (2) and (4) were not chased. A reading that separates the line's effect: the same 98 SE image's
  COM1 batch files with `XhciPfwNhiLine` 0 and the ring interrupt 1 against 0, side by side, on fresh overlays.
- The data ring's own latch at a 98 SE end (`cm.net.irq.latched`, the loss found while the data ring's cause is
  open) was not read: on this model the loss is found only at ring-0 traffic, and the cable pull that makes ring-0
  traffic shuts the data ring's cause first. A loss found with the link up needs ring-0 traffic without a link
  change (an inter-domain request from the far end, for one).
- `XhciPfwNhiLine` 0 is a value the user sets; the default (1) leaves 98 SE polled. Nothing here changes it.

## 49.20 (continued) Record 37's VxD readings with a probe that hooks nothing (2026-11-11, worker leg2)

The owner's answer to the earlier question (2026-11-11, Q1 (a)): a probe VxD that hooks nothing (`Device_Init` marks, `Exec_Int` 41h/48h, an IOS DCB walk, logging only) may be written as a scratch tool that is never tracked. No tracked file changed. The plan and the code were reviewed and revised before any guest ran them (scratch `plan-C.txt` and its review notes); what that changed is in the limits below. A pass-through V86 hook (section 5.2) was not designed or built.

**The tools** (`E:\xhci98-scratch\w49\leg2\probe\`, sources and `BUILD.TXT`; built on development host A with the main checkout's toolchains in place, nothing installed: Open Watcom 2.0 and the 98 SE DDK's LINK 5.12 `/VXD` with src\vsd's OMF and call checks, as `S98VSD.VXD` is built; the Windows 2000 DDK with MSVC 6.0, as `test\pmclient` is built):

- `LEG2PRB.VXD` (`9c1d44f9...`), a static VxD (`HKLM\System\CurrentControlSet\Services\VxD\LEG2PRB`, `StaticVxD`), `Init_Order` 16300000h (XHCIPFW's, record 37 section 4.4's "earliest place"). Every line to port E9h with VMM's `Get_System_Time`. `Sys_Critical_Init`: a mark and a 256-byte global V86 data area. `Device_Init` and again `Init_Complete`: a mark, then in the system VM (VMM's save/restore of the client state, `Begin_Nest_V86_Exec`, `Exec_Int`, `End_Nest_Exec`) MS-DOS's drive data table (INT 2Fh AX=0803h, walked) and boot drive (INT 21h AX=3305h), then INT 13h 41h and 48h (a cleared 4Ah buffer) for each unit from 80h up to the count at 0040:0075. One private `Directed_Sys_Control` message (7C2A0001h): its registers, the current and system VM and the current thread logged, EDX=4C320001h returned. It installs no hook. `LEG2PRBM.VXD` (`def84e65...`) is the same image built with marks and the message only (no `Exec_Int`).
- `LEG2VSD.VXD` (`2463e2d9...`), an IOS layer loaded from `SYSTEM\IOSUBSYS`, on `S98VSD.VXD`'s registration skeleton, whose AER only logs: every AEP function; `AEP_CONFIG_DCB`, the unconfigure functions, `AEP_ASSOCIATE_DCB` and `AEP_MOUNT_NOTIFY` with their DCB fields (the physical-only fields, port name and `DCB_bdd`, only for a DCB with `DCB_DEV_PHYSICAL`); at `AEP_BOOT_COMPLETE` a walk of every DCB with `ISP_GET_FIRST_NEXT_DCB`. It inserts no calldown and has no request routine: no request reaches it.
- `LEG2CL.SYS` (`03f199f2...`), a WDM function driver bound by its own INF to QEMU's `usb-tablet` (`VID_0627&PID_0001`, as `test\pmclient\xhcipm.inf` binds). In `IRP_MN_START_DEVICE`, once `xhci98.sys`'s device PDO had completed it (completion proven by a flag, as pmclient does on 98), at PASSIVE_LEVEL in the dispatch routine: `IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS` (22043Fh) sent down, whose bus, device and function `xhci98.sys` takes from its own `IoGetDeviceProperty` of the controller PDO (`src\hcd_urb.c`, `hcdTopologyAddress`); then VMM's `Get_DDB` ("LEG2PRB ") and `Directed_Sys_Control` with LEG2PRB's message through VMM dynalinks built in non-paged pool. It is an instrument beyond the probe VxD the answer names, needed because the link's caller must be a WDM driver; it intercepts nothing.

**The starts.** As B1: the emulator of 47.12's trace (`asm4242-20261025-2230`), TCG, one processor, 512 MB, the standard stick at power-on (unit 82h), the transfer disk (81h), the gdbstub census in log mode (nothing answered), QEMU's `usb_xhci_reset` and `usb_msd_cmd_submit` events, a screen every 5 s, and a host poller stamping each new debug-console line (receipt times, polled every 50 ms; VMM's own milliseconds kept as a second clock). From each system's installed baseline of B1 (`b98\inst.qcow2`, `bme\inst.qcow2`) three prep images, each made by one start that copied the files in (A: `LEG2PRBM.VXD` as `LEG2PRB.VXD`; P: `LEG2PRB.VXD`; B: `LEG2PRB.VXD`, `LEG2VSD.VXD`, `LEG2CL.SYS` and its INF, then a second start with the tablet in which Windows bound `LEG2CL.SYS` to it with no question); every reading start a fresh child of its prep image. A (marks only, the control), P (LEG2PRB), B and B2 (all three, the tablet at power-on), one at a time per system; the two systems' starts ran side by side, on a loaded host.

| Start | System | `Device_Init` (host receipt, against the takeover's reset) | `Init_Complete` | Requests to 82h after the takeover (census) | First BOT command after the window | LEG2CL's start | LEG2VSD registered | The desktop (5 s samples) |
|---|---|---|---|---|---|---|---|---|
| A (marks) | 98 SE | 2.1 s before | 81.4 s after | 15 reads, 76 s, by the firmware (5.10 s each) | 4 s after | - | - | 105 to 110 s |
| P | 98 SE | 2.6 s before | 82.1 s after | the same 15 reads | 5 s after | - | - | 100 to 105 s |
| B | 98 SE | 2.8 s before | 82.8 s after | the same 15 reads | 5 s after | 0.5 s after the reset | 80.4 s after (2.2 s after the window's last answer) | 100 to 105 s |
| B2 | 98 SE | 4.3 s before | 86.6 s after | the same 15 reads | 8 s after | 1.1 s after | 82.9 s after (5.0 s after it) | 115 to 120 s |
| A (marks) | ME | 2.0 s before | 105.7 s after | 20 reads, 102 s, by the firmware | 3 s after | - | - | 120 to 125 s |
| P | ME | 3.4 s before | 107.5 s after | the same 20 reads | 4 s after | - | - | 120 to 125 s |
| B | ME | 3.4 s before | 108.1 s after | the same 20 reads | 4 s after | 1.3 s after | 105.3 s after (1.5 s after it) | 130 to 135 s |
| B2 | ME | 2.6 s before | 108.3 s after | the same 20 reads | 4 s after | 1.4 s after | 105.2 s after | 125 to 130 s |

The control (A) reads as B1's control starts did (98 SE 15 reads over 76 s and the desktop at 105 to 115 s; ME 20 over 102 s, 115 to 120 s), and P, B and B2 kept the same requests and window; ME's two B starts came to the desktop 5 to 15 s later than its A and P (one start each: not attributed). Host receipt times are the poller's (50 ms) on a loaded host; VMM's own times agree with them in order.

**4.4 (a): a static VxD's `Device_Init` comes before the controller's start. Run, as far as the takeover's reset; the start IRP itself not seen.** In all eight starts LEG2PRB's `Device_Init` (VMM time 0.18 to 0.31 s) came 2.0 to 4.3 s before the takeover's `usb_xhci_reset` (the third reset of the start, as in B1) and before the window's V86 reads of 82h (the probe's own 41h/48h to 82h at `Device_Init` excepted), and in P and B the firmware still answered the stick at that point (below); `Sys_Critical_Init` up to 0.35 s before it. `Init_Complete` came after the window, 81 to 87 s (98 SE) and 106 to 108 s (ME) after the reset, 3 to 9 s after the window's last answer. So the window lies between this init order's `Device_Init` and `Init_Complete` [emu]. That `Device_Init` also comes before `xhci98.sys` enters its controller start (rather than before the reset that start makes) is [inf]: the start IRP was not observed. Which component's `Device_Init` sends the drive walk is not read.

**4.4 (b): `Exec_Int` at `Device_Init` reaches the firmware's INT 13h. Run; the chain only as far as the frame shows.** Every INT 13h LEG2PRB issued at `Device_Init` (41h and 48h for 80h, 81h and 82h) reached SeaBIOS's entry, on the stack of VMM's nested execution (`7700h` on 98 SE, `8F00h` on ME), with the immediate caller IO.SYS's own call point, `0070:0522` on 98 SE and `0070:04F8` on ME: the same frame as MS-DOS's own requests of the window (B1) [debugger]. So the nested INT 13h passed IO.SYS's hook on its way to the firmware like the window's requests; what lies above IO.SYS's call is not seen. The firmware answered at `Device_Init` for every unit, the stick included: 41h AH=30h BX=AA55h CX=0007h, CF clear; 48h on 98 SE a 26-byte table (size word 1Ah, no path) for every unit; on ME 1Eh for its two PIIX IDE disks (80h, 81h) with a device path in the 4Ah buffer (key BEDDh, length 2Ch, "PCI ", "ATA", PCI 00:1E.0, the guest's PIIX function; logged, not checksummed) and 1Ah with no path for the stick (82h). So this firmware fills an EDD path for its ATA disks but not for the USB stick, on either system. At `Init_Complete` the requests for 80h and 81h reached the firmware again (`0070:0522` / `04F8`, stack `07C5h` / `0D64h`), but those for 82h did not reach it: they were answered elsewhere, 41h with AX=0100h and CF clear, 48h with AX=0, CF clear and a 26-byte table [emu]. That those come from IOS's protected-mode INT 13h service for the associated disk is [inf].

**4.3 at the moment: the letter and its unit. Run (MS-DOS's half) and IOS's route as event-time records.**
- MS-DOS's drive data table at `Device_Init` (all four P and B starts, and the same at `Init_Complete`): (00h A:) (00h B:) (80h C:) (81h D:) (82h E:), five entries, then FFFFh; boot drive C: (3305h DL=3) [emu]. The same as B2's two end points, so MS-DOS's map does not change over the start.
- IOS, through LEG2VSD: IOS called the observer's registration only after the window (above), so nothing of IOS's state during the window is seen directly. At its registration IOS replayed `AEP_CONFIG_DCB` for the DCBs it then had: on both systems one physical DCB with flags 0000A014h (`DCB_DEV_PHYSICAL` and `DCB_DEV_RMM`, no port name, `DCB_BDD_Int_13h_Number` 0, unit FFh) and its logical DCBs for A: to E: (E: on 98 SE and ME alike: letter 4, flags 00006413h, its physical DCB that real-mode-mapper DCB); no DCB of the stick's protected-mode disk.
- 2 to 3 s later IOS configured the stick's physical DCB (port name `USBMPHLP`, flags 00008404h, `DCB_BDD_Int_13h_Number` 0), then `AEP_ASSOCIATE_DCB` for it (drive bitmap 0; the DCB now flags 01018407h, `DCB_DEV_INT13_DRIVE` set, unit number 82h), and at `AEP_BOOT_COMPLETE` the walk showed it with flags 11098407h (`DCB_DEV_INT13_CONFIGURED` added), `DCB_BDD_Int_13h_Number` 82h, `DCB_BDD_flags` 7, none of `DCB_DEV_RMM`, `DCB_DEV_REAL_MODE` and `DCB_DEV_REMOVAL_PENDING`, and a logical DCB for F: (letter 5) on it [emu]. The same records on 98 SE and ME, in both B starts each; another `AEP_ASSOCIATE_DCB` for the same DCB came with the desktop's letter test and the shut down.
- So in these starts, read as "DCB X had these fields at event Y": the DCB record 37 section 5.2 (a)'s predicate names (a protected-mode physical DCB with `DCB_DEV_INT13_DRIVE`, `DCB_BDD_Int_13h_Number` the unit, none of RMM, REAL_MODE, REMOVAL_PENDING) was first observed after the window in all four instrumented starts (the observer is registered only after it, so the DCB's absence during the window is not shown) and had all of those fields by `AEP_BOOT_COMPLETE` (at the first `AEP_ASSOCIATE_DCB` it had `DCB_DEV_INT13_DRIVE` and unit number 82h but `DCB_BDD_Int_13h_Number` still 0); the BIOS letter E: stayed a logical DCB of the real-mode-mapper physical DCB throughout. Whether IOS serves 82h from the first instant of the association, and the predicate's value request by request, are not read.

**5.4: the numbers and the link. Run, with one equivalence inferred.**
- What `xhci98.sys` reads for the controller's location: the topology answer was 00000000h for bus, device and function (root port 6, the tablet's, right) in all four B starts and both prep starts, on 98 SE and ME, with status success. By `hcdTopologyAddress` that is what its `IoGetDeviceProperty` of the controller PDO (`DevicePropertyBusNumber`, `DevicePropertyAddress`) left: no location, failed or zero (the answer does not say which), where the firmware's and the PCI enumerator's numbers are 01:00.0 (B3) [emu]. So on 98 SE and ME this route does not give `xhci98.sys` the firmware's numbers; the source shows no other place where it reads them on 9x (the NT-only route of `HcdNoRouteAddDevice` aside) [inf from the source].
- `Directed_Sys_Control` from a WDM driver's start on 98 SE and ME: from LEG2CL's `IRP_MN_START_DEVICE` at PASSIVE_LEVEL (in the four B starts during the window, 0.5 to 1.4 s after the takeover's reset; in the two prep starts, with no stick, after `Init_Complete`), VMM's `Get_DDB` found LEG2PRB (DDB `C0070614h` on 98 SE, `C006D488h` on ME), and the message reached its control procedure with EBX, EDX, ESI and EDI as sent, in the system VM (current VM = system VM, `C2920074h`), and came back with carry clear and EDX = 4C320001h, the probe's version, in all six starts [emu]. This is a child function driver's start, not `xhci98.sys`'s controller start; that the controller's start context allows the same call is [inf] (both are WDM start IRPs under NTKERN at PASSIVE_LEVEL; the controller's comes earlier).

**Is the pass-through V86 hook of section 5.2 (Q2) still needed?** For what 5.2 (a) asks, yes: these readings give the predicate's fields at IOS's events, not its value at each request of the window, and they say nothing about calling `ISP_GET_FIRST_NEXT_DCB` or `IOS_Find_Int13_Drive` from a V86 hook; only a hook that sees each request can read those. What the readings add for the owner's decision: in the four instrumented starts the stick's protected-mode DCB was first observed, associated and marked `DCB_DEV_INT13_DRIVE` with unit number 82h only after the window, and an IOS layer is told of that association (`AEP_ASSOCIATE_DCB`), so a design that steps aside on an IOS event, told by an IOS layer rather than asked from the hook, is a candidate the owner may want weighed before a hook probe is designed [inf]. Not established: that the association callback identifies the unit by the predicate's field (`DCB_BDD_Int_13h_Number` was still 0 there and 82h only by `AEP_BOOT_COMPLETE`), nor that IOS serves 82h from that callback on. 5.2 (b) (a handler added to the chain after IOS's start) is untouched by these readings.

**Limits.** One emulator and one firmware; the probe's own BIOS calls at `Device_Init` (6 requests) and its debug-port writes are in P and B, A shows the marks alone change nothing seen; LEG2VSD's registration came with B's other instruments, so B's ME desktop delay is not attributed. The observer's records are snapshots at IOS's calls; DCB addresses are this start's. Debugger readings: register values, frames and the probes' own output only.

Evidence: `E:\xhci98-vm\p49\leg2\r98{A,P,B,B2}\`, `rme{A,P,B,B2}\` (`dbg-<tag>.txt` the stamped debug console, `debugcon-<tag>.log`, `i13-<tag>.log`, `trace-<tag>.log`, `serial-<tag>.log`, `shots-<tag>\`), the prep images `c98{A,P,B}\`, `cme{A,P,B}\` (`dbg-prep2.txt` of the tablet's first start), `E:\xhci98-scratch\w49\leg2\b\keys.py`, `ana.py`; QEMU processes `E:\xhci98-vm\p49\leg2\pids.txt`.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** the owner closed 49.20 on 2026-11-11
(night) with the limitation the release notes already carry (a USB disk left plugged in at the start can hold 98 SE
and ME for minutes, or stop the start; plug it in after Windows has started): there is a way round it, and the remedy
would be a new part in the path of the disk requests at the start. That the start goes on after the delay is the
emulator's reading only; on the owner's bench computer the start never reached the desktop with the stick present
(record 37 section 1), and nothing was read on real firmware.

## 49.30 - 98 SE and ME standby with a dock, a switch enclosure or a chain behind the tunnel port: lost, then found again (2026-11-11, worker sub)

Branch `w49-sub` from `3.5.0.0` at `30051d7ed`. Commits `f28f295ab` (record 21 section 6.1.5, the design),
`44f266d47` (the code: `cm_ports.c/.h`, `cm_cfgwk.c`, `test_cm_ports.c`), `946e84ff0` (item 7: no
re-enumeration through a reset bridge; two log lines) and the record's last wording (see "Commits" below).
No emulator change (`E:\usb4-emu-wt\w49-stby` untouched).

**The shape, and why.** The inherited subtree write-back (worker stby's draft 4, four review rounds, not
converged: what a saved copy is bound to) was not continued. Decided, with the plan reviewed three times
before any design text: after the wake a switch right behind the tunnel port that came back reset under a
devnode that stayed is treated as a device unplugged during the sleep and plugged in again - its devnode
removed by the worker's own removal step (a refusal respected, never forced), then numbered and enumerated
anew by the hot-plug path. Nothing below the tunnel port is written, so no copy has to be bound to "the
same devices"; a unit changed during the sleep, an eject in flight and a late chained device all end in the
same removal and a fresh enumeration. Rejected: the write-back at once (B); a removal at the suspend, inside
Windows' own suspend broadcast (C1); refusing the suspend (C2). The write-back for a dock alone on top of
this stays the owner's choice (`E:\xhci98-scratch\w49\sub\QUESTIONS.md`, Q1).

**Reviews.** Plan: three rounds (7, 4, 3 points; the third "fit to write as a draft"). Design (record 21
section 6.1.5): three rounds (4, 2, 0). Code: four rounds (2, 1, 1, 0) before the first build. After the
first legs (below), item 7 and its code: three rounds (2, 2, 0). Nothing material left at each commit.
Prompts and results: in the worker's scratch folder.

**Builds** (`XHCI98_SELFTEST_SKIP_OK=1`, one slot each): x86 `all` on `44f266d47`: `BUILD + GATES PASSED
(release qemu)` (`gated-x86-a.log`); x86 `all` on `946e84ff0`: `BUILD + GATES PASSED (release qemu)`
(`gated-x86-b.log`; `test_cm_ports` 615 checks, 0 failures - 554 before). `xhcicm.sys` x86 of `946e84ff0`:
release `7cac0cec...`, qemu `5c4f220f...` (SHA-256; `xfer98\NEW`, `NEWQ`); of `44f266d47`: release
`9552ad79...`, qemu `16cb4ad2...` (`MID`, `MIDQ`); the base's (`7e0bf543c`): `680131fb...`, `2d0b4417...`
(`OLD`, `OLDQ`). amd64 not built: the change is inside the CONFIGMG route (`cm_cfgwk.c`, compiled for x86
only) and two pure helpers.

**Vehicles** (copied from worker stby's kit to `E:\xhci98-vm\p49\sub\tools`; every guest one vCPU, TCG;
QMP 48580 to 48585).
- 98 SE on q35: the disk on a `piix3-ide` with no geometry hints, the S3 BIOS, the Intel ADL host model
  with the software connection manager, tunnel root ports 00:05.0 and 00:06.0, `3.4.3.0` installed with
  the dock (a USB4 device router with a PCIe switch, `downstream-ports=1`, its own NVMe) and an ASM2464
  enclosure chained behind it (`b98\q98chain.qcow2`); a fresh overlay per leg, the `xhcicm.sys` under test
  copied over `SYSTEM32\DRIVERS` (`fc /b`: no differences), a cold start (`tools\ld.sh`).
- ME with a dock: found for this task. QEMU's `pc` machine takes the emulator's PCI Express tunnel port on
  `pci.0` once the CPU has its APIC (`-cpu pentium3`, not `pentium3,-apic`: "MSI-X is not supported by
  interrupt controller" otherwise); the q35 ME base on it with no geometry hints, `3.4.3.0` installed, the
  dock's devnodes installed once (`bme\medock.qcow2`, `tools\gomed.ps1 -Pcie`, `tools\lmd.sh`). It reaches
  S3 by `SetSuspendState` and offers Hibernate in the Shut Down dialog.
- The i440FX vehicles of sections 6.1.3 and 6.1.4 (the SHPC tunnel bridge, the enclosure's NVMe right
  behind it) for 98 SE and ME (`tools\li.sh`).
- The leg (`tools\legd.sh`): `C98.EXE` (a Windows program; this task's copy adds `beat`, `tick`, `hold`,
  `watch3`): the drive found, a file written and compared, then, all started BEFORE the sleep - a heartbeat
  every 5 s (`beat`: the drives that hold `TESTDATA.BIN`; `tick`: the line alone) and the watcher (about
  70 s after the wake and again 2 min later: the pre-sleep file compared, a new one written and compared);
  `SetSuspendState`; QMP `system_wakeup` 20 s after `suspended`; `info pci` and screens after the wake. No
  program is started after the wake.

**Baselines** (the base's binaries). `bd1`, 98 SE, the dock alone, qemu `2d0b4417`: the tunnel port put
back (`resume-restore=00002801`), the dock's upstream port reset for good, `cm.brnum.9x.held`; six
heartbeats before the sleep and none after it: frozen, as stby's `cq1` / `cq2` with the chain. ME with the
dock, release `680131fb` of `3.4.3.0` (`mv1`, the watcher alone): S3, the wake, at +5 s the dock's switch
reads secondary bus 2 and the NVMe's BAR mapped (ME restores the subtree itself), E: there at 70 s and
2 min later, the pre-sleep file compared. The same ME guest hibernated from the Shut Down dialog and
resumed with the drive back (the watcher's late check; `mv1\serial-b.log`). ME with the heartbeat's drive
polling and the base's qemu binary hung at the wake before `cm.resume` twice (`m0`, `m0b`): the polling
program, not this change, is the difference from `mv1`.

**First legs** (build `44f266d47`; exploratory, they shaped item 7 and the wording).

| Leg | 98 SE q35, the dock | Result |
|---|---|---|
| d1, d5 (qemu), d2 (release) | one standby, heartbeat | **removed and found again**: `resume-restore=00002801`, `resume-lost=00002801`, `query-remove=00000000`, `remove=00000000`, `resume-wait=00002807` 2018 ms after the resume, the numbering (`cm.brnum.9x.outcome=00002801`, the dock's three bridges 1/2..6, 2/3, 2/4..6 as before), `reenumerate=00002800`; the heartbeat: no drive for one beat, E: back at the next and to the end (d1: 46 beats); the watcher's checks pass; the desktop at the end, an Explorer window opened on the returned drive (`d1\d1-final.png`). The other members' bridges read restored by Windows 4.1 s after the resume, while the worker was inside the removal and the re-enumeration |
| d3b (qemu), d4b (release) | two standbys, the heartbeat polling the drive, the watcher writing 5 s before the second | first wake as d1. **Second wake: Windows' own full-screen message** "The file system mounted on the volume had data that needed to be flushed. Some data on the volume was lost. Next time please check first to see if the volume can really be removed." (`d3b\d3b-b.png`), `query-remove=00000017` (refused), `resume-wait=00002808` once a key was pressed 130 s later; the heartbeat then reads no drive at once and goes on (nothing frozen). Then the member's `Reenumerate_DevNode` over the still reset switch: New Hardware Found "PCI Host Bridge" (`d3b\d3b-f.png`) and an Add New Hardware Wizard for a PCI-to-PCI bridge - PCI.VXD taking bus 0's functions for children of the reset bridge. Hence item 7 |
| x1c (qemu) | the same, a key 8 s after the second wake | the query refused again at each of the five retries inside the bound (no second message), `00002808` |
| e3 (qemu) | two standbys, no heartbeat, the watcher writing **30 s** before the second (`watch3`) | removed and found again at **both** wakes (`00002807` twice), the checks pass after each |
| e1 (qemu) | one standby, no heartbeat | as d1 |
| e2 (qemu) | two standbys, no heartbeat | first wake removed and found; the fatal exception 0E at `0197:BFF9DFFF` on the screen after the arrival (`e2\e2-x.png`), and after its key no second standby: not read further |
| d4, x2, s1 (heartbeat) | first wake | **the same message at the first wake**, the last write a minute old (`d4\d4-now.png`, `x2\x2-st.png`, `s1\s1-st.png`); in the seven other legs of this build whose heartbeat polled the drive the first wake was clean, and in the three legs without the heartbeat too |
| h1 (qemu) | a file held open for writing across the sleep, written through its handle every 5 s | no message; `query-remove=00000017` at each of seven attempts, `00002808`; the handle dead (error 6) and no drive from the first beat after the wake; Windows answering throughout; then the wizard as d3b |
| c1 (qemu) | the dock and the chained enclosure, both volumes | removed and found again (`00002807`); the chain port found from the new devnode, numbered and re-enumerated (`cm.chain.9x.number=00020802`, `cm.chain.9x.reenumerate=00020800`); both volumes back and the pre-sleep files compared on both - behind the fatal exception 0E at `0197:BFF9DFFF`, which stood on the screen after the arrivals until a key (`c1\c1-now.png`) |
| u1 (qemu) | the dock unplugged during the sleep, put back 3.5 min after the wake | no drive after the wake, the heartbeat alive throughout, E: back after the replug |
| d3, x1, x1b | | void: the guest reset at the wake (QEMU exited; d3, x1) or had not finished its boot (x1b) |

What these first legs say about a refusal: the query is refused when the file system is not done with the
volume - where a helper read the drive every 5 s across the standby (d4, x2, s1 at the first wake; d3b, d4b,
x1c at the second), with Windows' own message, and where a file was held open (h1), without it. A file
written 5 s before the standby is not enough by itself (the final build's f5, below). A retry does not pass.

**The legs on the final build** (`946e84ff0`: release `7cac0cec...`, qemu `5c4f220f...`; `fc /b` "no differences"
in every leg). "Removed and found" is the log's `resume-note=00002801` at the suspend, then
`resume-restore=00002801`, `resume-lost=00002801`, `query-remove=00000000`, `remove=00000000`,
`resume-wait=00002807` about 2 s after the resume, the numbering and `reenumerate=00002800`. A full-screen
message of Windows after a wake was answered with Enter by a script (`tools\keyer.sh`), its picture kept
(`bs-*.png`), so that the leg could go on; each is named below.

| Leg | System, vehicle, image | What | Result |
|---|---|---|---|
| f1 | 98 SE q35, the dock, qemu | one standby, the heartbeat polling the drive | **PASS**: removed and found; no drive for one beat, E: back at the next, to the end (80 beats); the pre-sleep file compared at 70 s, a new one written and compared, again 2 min later |
| f5 | 98 SE q35, the dock, qemu | two standbys in one boot, no heartbeat, the watcher writing 5 s before the second | **PASS**: removed and found at both wakes (`00002807` twice, `u4.connect` generation 2 then 3); after each wake the earlier file compared and a new one written and compared, twice |
| f3, f3b (qemu), f4, f4b, f4c (release) | 98 SE q35, the dock | two standbys, the second 30 s (f4c: 5 s) after the watcher's writes | first wake: removed and found (f3, f3b: `00002807`), the pre-sleep file compared at the watcher's first check in each; **the fatal exception 0E at `0197:BFF9DFFF`** stood on the screen after the arrival (`bs-*.png`), and after its key the watcher's second `SetSystemPowerState` did not suspend the guest: the second wake **not read** in these five |
| f2 | 98 SE q35, the dock, release | one standby, the heartbeat polling the drive | **the refusal**: Windows' data-lost message at the first wake (`f2\f2-st.png`); after its key no drive (the heartbeat and both watcher checks), Windows answering to the end, the desktop clean - **no New Hardware dialog, no wizard** (`f2\f2-st2.png`; item 7) |
| f10 | 98 SE q35, the dock, qemu | a file held open for writing across the sleep | **the refusal, held**: `query-remove=00000017` at nine attempts, `resume-wait=00002808`, then `cm.reenum.9x.reenumerate.held=00002800` and no re-enumeration of the member (the other members' ran); the handle dead (error 6), no drive to the end, also after the handle was closed; Windows answering (81 beats); no dialog |
| f6 (qemu), f7 (release) | 98 SE q35, the dock and the chained enclosure | one standby | **PASS behind the 0E screen**: removed and found (f6: `00002807`), the chain port numbered and re-enumerated (`cm.chain.9x.number=00020802`, `cm.chain.9x.reenumerate=00020800`); the fatal exception 0E on the screen 25 s after the wake (`f6\bs-192722.png`, `f7\bs-192724.png`), answered; both volumes (E:, F:) there at 70 s with their pre-sleep files compared, new ones written and compared, again 2 min later; the heartbeat to its end |
| f8 | 98 SE q35, the dock, qemu | the dock unplugged during the sleep, put back 3.5 min after the wake; the heartbeat polling | **PASS**: `cm.vanish.down`, the tunnel port put back (`resume-restore=00002801`), the noted bridge absent (`resume-lost=00002800` once 5 s had passed, `resume-wait=00002805`); no drive at either watcher check; E: back after the replug (the heartbeat from beat 44). Windows' data-lost message stood 17 s after the wake (the polling; `f8\bs-194031.png`) and a second full-screen message after the replug (`bs-194356.png`), both answered |
| f9 | 98 SE q35, qemu | the dock unplugged during the sleep and **another unit** (another serial, its own disk) put in its place | **PASS, nothing written to the new unit**: `cm.vanish.down`, the DOWN's own transaction removed the old devnode (`cm.vanish.retired=00280000`); the new unit's switch read reset once (`resume-lost=00002801`), then the wait ended with the inventory empty (`resume-lost=00002800`, `resume-wait=00002805` at 5053 ms); the numbering of the new switch and `reenumerate=00002800`; the drive at 70 s is the new unit's (the pre-sleep file is not on it: written anew and compared). The fatal exception 0E after the arrival (`f9\bs-194034.png`), answered |
| f12 (release), f13 (qemu) | 98 SE i440FX, the enclosure's NVMe right behind the SHPC bridge | one standby (sections 6.1.3 / 6.1.4's leg) | **PASS, unchanged**: f13 `resume-mark=00000001`, no note, `resume-restore=00002801`, `resume-restore.fn=00010801`, `resume-wait=00002805` at 2087 ms; the drive kept, the pre-sleep file compared, new ones written and compared at both checks; no full-screen message |
| f14 (release), f15 (qemu) | ME i440FX, the same | one standby | **PASS, unchanged**: f15 `resume-wait=00002801` at 219 ms (Windows' own restore), nothing written; the checks pass |
| f16 | ME, `pc` with a PCI Express tunnel port, the dock, qemu | one standby | **PASS, nothing removed**: `resume-note=00002801`, `resume-lost=00002800` (the noted switch reads configured: ME restored it), `resume-wait=00002801` at 247 ms; the pre-sleep file compared at 70 s, new ones written and compared |
| f17 | ME, the same, release | two standbys in one boot (30 s after the writes) | **PASS**: after each wake E: there, the earlier file compared, a new one written and compared, twice |
| f11 | 98 SE q35, the ASM2464 enclosure alone on the dock-and-chain base | | void: the guest did not come up on that base with that topology (black screen at the leg's start); a switch enclosure alone on q35 is not read |

**Owed** (the worker was handed back before them): the second wake of the RELEASE flavour on the final build (f4d was not started; the qemu flavour's is f5, and e3 on the earlier build); a switch enclosure alone on q35 (f11 void: its base needs that topology installed first); see `E:\xhci98-vm\p49\sub\STATE.md`, "REMAINING".

**What this says.**
- 98 SE with a dock (and with a chain behind it): the freeze is gone. With nothing using the drive around the
  standby the dock's subtree is removed about 2 s after the resume and found again by the hot-plug path about
  5 s after the wake, data intact (f1, f5 at both wakes; d1, d2, d5, e1, e3 on the earlier build; the chain:
  f6, f7, c1).
- A program reading the drive every 5 s across the standby made the removal be refused in some legs: at 4
  of 12 first wakes with it (d4, x2, s1, f2) and at 3 of 3 second wakes (d3b, d4b, x1c), each time with
  Windows' own message that data on the volume was lost (the same message came once with the dock really
  unplugged, f8); a file held open made it be refused without a message (h1, f10). After a refusal the drive
  stays absent, Windows answers, and with item 7 nothing is re-enumerated through the reset switch (f2,
  f10). None of the 13 wakes without such a program was refused (e1, e2, e3 twice, f5 twice, f3, f3b, f4,
  f4b, f4c, f6, f7).
- A different unit in the dock's place after the sleep is enumerated as new; nothing is written to it (f9).
- A dock unplugged during the sleep is handled as before (f8, u1).
- The single drive of sections 6.1.3 and 6.1.4 is unchanged on 98 SE and ME, release and qemu (f12 to f15).
- ME restores the dock's switch itself and the new code removes nothing there (f16, f17, m1).
- The fatal exception 0E at `0197:BFF9DFFF` after a drive's arrival following a wake (the other task's
  fault) stood on the screen after 11 of this task's 24 such arrivals on 98 SE (c1, e2, f3, f3b, f4, f4b,
  f4c, f6, f7, f9 and f8's replug; not after d1, d2, d5, d3b, d4b, x1c, e1, e3 twice, f1, f5 twice, u1's
  replug), every time with the drive itself back and its data compared once the message was answered;
  after it a further standby in the same boot did not happen. It is why the second wake of the release
  flavour on the final build was hard to read.
- Not read: a real machine; a switch enclosure alone on q35; ME with a chain; a restart of Windows after a
  refusal; the amd64 build (no change reaches it).

**Commits.** `f28f295ab` record 21 section 6.1.5 (the design); `44f266d47` the code; `946e84ff0` item 7 (no
re-enumeration through a reset bridge) and two log lines; `089ec2c1b` the record's wording of what a refusal
is. Files: `src\cm\cm_cfgwk.c`, `src\cm\cm_ports.c`, `src\cm\cm_ports.h`, `test\test_cm_ports.c`,
`docs\contributing\design\21-thunderbolt3-icm-client.md`.

Evidence: `E:\xhci98-vm\p49\sub\<leg>\` (`leg-*.txt`, `serial-*.log`, `debugcon-*.log` (qemu flavour),
`usb4-trace-*.log`, `pci-*.txt`, screens, `bs-*.png`), `keyer.log`, every QEMU start and stop in `pids.txt`;
`tools\sum.sh <leg>` prints a leg's deciding lines. Builds and reviews: `E:\xhci98-scratch\w49\sub\`.
Owner questions: `E:\xhci98-scratch\w49\sub\QUESTIONS.md` (Q1 found again or kept; Q2 a refused removal;
Q3 the two seconds' grace on 98 SE).

### Merge (sub)

`w49-sub` at `089ec2c1b`, four commits on `30051d7ed`; touches `cm_cfgwk.c` and `cm_ports.c/.h`, which other
branches of the day also touch.

## 49.30 (continued) - the release flavour's second wake, a write in the last second, the enclosure alone, ME polling, and the guard's unknown reading (2026-11-11, worker sub2)

Worker sub2 took over from sub at `089ec2c1b` (same worktree, scratch and VM folder; `E:\xhci98-vm\p49\sub\STATE.md`
section 8 is the step-by-step record). Every guest one vCPU, TCG, at most three at once, starts staggered by
40 s; QMP 48580 to 48587.

**Vehicle changes.** `tools\keyer.sh` answered a blank text-mode screen too: that is the screen of a 98 SE
suspend in progress, and a key there wakes the guest at once (f4e's second standby was never seen suspended
for that reason). `tools\keyer2.sh` / `keyer3.sh` / `keyer4.sh` (ports 48580-82 / 83-85 / 86-88) keep such a
screen (`ks-*.png`) and send no key; a blue full-screen message is answered as before (`bs-*.png`). A 98 SE
guest showing a fatal exception 0E at `0197:BFF86EBD` / `BFF87164` at its BOOT, before any standby (f4d, and
sub's x1b, whose "not up" was this), is void. The guest resetting at a wake (QEMU gone, the debugcon log
ending at `u4.sleep.entered`) is void as before (z2, r5).

**A correction to the legs before.** Mode `two`'s check after the SECOND wake compares no file written before
the second standby: the watcher's tag gains `S` there, so its A file is new (`C98 A written=1` at f5S, e3S,
f17's and f4f's second checks). f5, e3, f17 and f4f show the drive back and working after the second wake;
they do NOT show that the file written 5 s (f5) or 30 s (e3) before the second standby was intact. The
sentences above that say so ("data intact (f1, f5 at both wakes ...)" in "What this says", and sub's
STATE.md section 7) over-read those legs; the last-second legs below are the first reading of data written
just before a standby. The refusal reading of f5 (no refusal at either wake, record 21 section 6.1.5 "What a
refusal is") stands.

**1. 98 SE, the dock, RELEASE (`7cac0cec`), two standbys in one boot, no heartbeat.**

| Leg | Result |
|---|---|
| f4d | void: the 0E at `0197:BFF86EBD` at the boot (`f4d\f4d-boot.png`), never suspended |
| f4e | first wake: E: back, the pre-sleep file compared; the second standby woken at once by `keyer.sh`'s Enter on the suspend's blank screen (`f4e\bs-205258.png`), so not a clean second wake: void past the first wake |
| f4f | **PASS**: both standbys suspended (QMP 21:04:01, 21:05:45), both wakes by `system_wakeup`; after wake 1 E: back and the pre-sleep file compared; after wake 2 E: back, files written and compared at +70 s and +2 min; no full-screen message at all (only `ks-*.png`); Windows answering to the end. Release: no debugcon, so "removed and found" is inferred from the qemu twin (f5), not logged |

**2. A write in the last second before the standby** (`C98.EXE zsleep <tag>`, scratch `c98\c98.c`, reviewed
twice, the second round nothing material; `tools\ldz.sh` / `legz.sh`, mode `last`; heartbeat `tick`, no
other program touching the drive). `zsleep` resolves `SetSuspendState` and opens COM1, then writes
`Z<tag>.BIN` (64 KiB) through the cache (no write-through, no flush), closes it and asks for the standby at
once, logging the ticks after the wake. The watcher reads `Z<tag>.BIN` first at each check (before it
writes anything), at its exact size; ABSENT only for file/path not found. The pre-sleep run reads it ABSENT.

| Leg | Image | Close to the standby call | After the wake |
|---|---|---|---|
| z1 | release `7cac0cec` | 0 ms (`closed=149368 call=149368`) | `Z compare=OK size=65536` at +70 s and +2 min; no full-screen message |
| z3 | release | 0 ms (`closed=114323 call=114323`) | OK at both checks; no message |
| z2 | qemu `5c4f220f` | | void: the guest reset at the wake |
| z4 | qemu | 0 ms (`closed=90899 call=90899`) | removed and found (`resume-lost=00002801`, `query-remove=00000000`, `remove=00000000`, `resume-wait=00002807`); OK at both checks; no message |
| z5 | release, the fixed build `bf740d8b` | 0 ms (`closed=212281 call=212281`) | OK at both checks; no message |
| z6 | qemu, fixed `90c8d0c2` | | void: the guest reset at the wake |
| z7 | qemu, fixed | 0 ms (`closed=215676 call=215676`) | removed and found (`00002807`); OK at both checks; no message |
| z8 | qemu, fixed | 0 ms (`closed=211461 call=211461`) | removed and found (`00002807`); OK at both checks; no message |
| o3 | release, fixed, the ASM2464 enclosure alone (`q98orico`) | 0 ms (`closed=210961 call=210961`) | OK at both checks; the 0E after the arrival (`o3\bs-234406.png`), answered |

What it says: a file written through the cache and closed, the standby asked within the same millisecond,
nothing else using the drive, was intact once the dock had been removed and found again - read back after the
volume was unmounted and mounted again, so not from the cache that wrote it. Windows' data-lost message did
not appear. It does NOT say the data was still unwritten at the suspend (Windows' own suspend may write the
cache back first; not measured), and nothing here reads a real machine.

**3. The ASM2464 enclosure alone on q35** (a switch enclosure, no dock). Base `b98\q98orico.qcow2`: an
overlay of `b98\q98piix` (which did not hold the package) booted with the enclosure, `3.4.3.0` installed by
`INSTALL /console /yes /nopause /thunderbolt` (`xfer98z\T\INS2.BAT`; without `/thunderbolt` only the
controller is installed), the wizards answered, the drive read (`o1\serial-k1.log`). o2 (qemu `5c4f220f`,
one standby, tick): **PASS** - removed and found (`resume-lost=00002801`, `query-remove=00000000`,
`remove=00000000`, `resume-wait=00002807`), the pre-sleep file compared, new files written and compared at
both checks; the 0E at `BFF9DFFF` after the arrival (`o2\bs-211806.png`), answered.

**4. ME with the heartbeat polling the drive** (`tools\lmd.sh`, `BEAT=beat`): the base's qemu `2d0b4417`:
m4, m5 **both hung at the wake** (no `cm.resume`, the heartbeat stopped at the sleep, the screen's clock
frozen: `m4\m4-chk.png`); with sub's m0, m0b: 0 of 4 woke. This branch's qemu `5c4f220f`: m2, m3 **both hung**
the same way (`m2\m2-hung.png`); with sub's m1 (passed): 1 of 3 woke. The hang comes before this driver's
resume line on both binaries: it is ME with a program polling the drive across the standby on this vehicle,
not this change. m6 (the fixed qemu build, polling): hung the same way; the new binaries 1 of 4 woke, the base 0 of 4. Not read: what hangs.

**5. The merge review's finding, and the fix.** Item 7's guard (`cmWkResetUnder`) let a member's or a chain
port's `Reenumerate_DevNode` go through a reset bridge whenever a read failed: a child devnode's id not read or
the child list ending in an error made it answer "no reset bridge". Checked against the code: it held. Fixed
(design and code reviewed: design two rounds, the second nothing material; code one round, nothing material):
the guard answers proven safe / proven reset / unknown and unknown holds (`cm.reenum.9x.reenumerate.unread`,
`cm.chain.9x.reenumerate.unread`); the criterion is a type 1 header at secondary bus 0; a bridge whose
secondary bus is not read (no-configuration mode, or its own secondary bus 0) is safe only on a complete empty
child list (FEh). Pure helpers in `cm_ports.c`, host tests `test_reset_guard` (an identity-read failure and
child-list failures after a refused removal among them; 656 checks). Gated x86 `all` on the uncommitted
tree: `BUILD + GATES PASSED (release qemu)` (`E:\xhci98-scratch\w49\sub\gated-x86-c.log`); `xhcicm.sys`
release `bf740d8b...`, qemu `90c8d0c2...` (`xfer98\FIX`, `FIXQ`).

| Leg | Image, what | Result |
|---|---|---|
| r1 | qemu, a file held open across the sleep | **PASS**: `query-remove=00000017` nine times, `resume-wait=00002808`, `cm.reenum.9x.reenumerate.held=00002800` (proven reset); the handle dead (error 6), no drive to the end, 81 beats, no dialog |
| r2 | release, the same | **PASS**: no drive from the first beat after the wake, the handle dead, 81 beats, no New Hardware dialog (`r2\r2-end.png`) |
| r3 | qemu, polled, two standbys | first wake: Windows' data-lost message 11 s after the wake (`r3\bs-223355.png`), `query-remove=00000017` four times, then **accepted at the fifth attempt** once the message was answered: removed, `00002807`; the 0E after the arrival; the second standby not entered |
| r3b, r4 | qemu / release, polled, two | first wake removed and found, no refusal; the 0E after the arrival; the second standby not entered |
| r5 | qemu, idle | void: the guest reset at the wake |
| r5b | qemu, idle, tick | **PASS**: removed and found (`00002807`), the pre-sleep file compared, writes compared at both checks; the 0E after the arrival, answered |
| p1 | qemu, polled, one standby | no refusal: removed and found (`00002807`), one beat without the drive |
| p2 | release, polled, one | void: a blank screen at the boot, never suspended |
| p3 | qemu, polled, one | **PASS, the lasting refusal read on the fix**: Windows' data-lost message (`p3\bs-232500.png`), `query-remove=00000017` seven times, `resume-wait=00002808`, `cm.reenum.9x.reenumerate.held=00002800`; no drive from the beat after the wake to the end, 81 beats, no dialog (`p3\p3-end.png`) |
| p4 | release, polled, one | the data-lost message (`p4\bs-232831.png`), then the drive back at the next beat (a retry passed), the checks pass |
| r6 | release, idle, tick | **PASS**: E: back, the pre-sleep file compared, writes compared |

A refusal that passes on a retry (r3) is new: sub's x1c, h1 and f10 never saw one pass. Bears on the owner's
Q2 (a slow retry after the bound).

Totals on the fixed build (`4331b9f6c`'s `xhcicm.sys`, 98 SE q35 dock unless named): the lasting refusal and its
hold, 3 of 3 read (r1, r2 with a file held open; p3 with the polling): PASS; the idle drive: r5b, r6 PASS (r5
void); the last-second write: z5, z7, z8, o3 PASS (z6 void); a refusal that passed on a retry: r3, p4; no
refusal with the polling: r3b, r4, p1. ME with the fixed qemu build: m6 (polling) hung at the wake as the
others; m7 (tick, no polling) **PASS**: `resume-lost=00002800` (the switch reads configured: ME restored it), `resume-wait=00002801`, nothing removed, the pre-sleep file compared, writes compared at both checks.

**Last-second write, in all**: 7 legs read, 7 PASS (release z1, z3, z5 and o3, which is also the enclosure
alone; qemu z4, z7, z8), 2 void (z2, z6: the guest reset at the wake). (Coordinator's correction at the fold: the worker's line counted o3 twice and said 9.) No data-lost message in any of them.

**Commits.** `4331b9f6c` the guard's unknown reading (record 21 section 6.1.5 item 7; `cm_cfgwk.c`,
`cm_ports.c/.h`, `test_cm_ports.c`); `186da5eaa` record 21 section 6.1.5: a refused removal can pass on a
later retry. Reviews: design two rounds (two points on the exceptions taken, then nothing material), code
one round (nothing material), the docs change two rounds (one wording point taken, then nothing material).
Build: gated x86 `all` on the reviewed tree (`gated-x86-c.log`), the binaries the legs ran.

**Bench (the owner's)**: everything above is the emulator's. Owed on real hardware: 98 SE and ME standby with
a real dock, a real switch enclosure and a chain behind a real Thunderbolt / USB4 port (removed and found
again; the refusal with a program using the drive; the last-second write); whether a real machine's resume
leaves the switch reset as the emulator does; the 0E after a drive's arrival (49.31) on real hardware; ME
with a program polling the drive across the standby.

Evidence: `E:\xhci98-vm\p49\sub\<leg>\` as before; `keyer.log`; `pids.txt` (every start and stop);
`E:\xhci98-scratch\w49\sub\` (the review prompts and results, `gated-x86-c.log`,
`c98\` the helper's source and diff).

## 49.3 (continued)

usb-wdm-free 0.1.10 brought up to the gate fix (worker rel0110b, 2026-11-11).
usb-wdm-free main `2a539ae` (the merge of `w49-uac`: `34f1896`, `8ad07a2`,
`9c820be`); release branch `w48-rel0110` at `9552368`, worktree
`E:\usb-wdm-free-wt\w48-rel0110`.

### How the branch stages a package

`release\0.1.10.psd1` names each folder's files by source: uac2's and
uac1's x86 images and INFs from `out\audio\uac2\wdmx86\free` and
`out\audio\uac1\wdmx86\free`, uac2's x64 image through `X64Builds` from
`out\audio\uac2\wdm64\free` (the owner signs it, `-X64Dir`). Nothing under
`free\` is written before the owner's signed run of `release.ps1`; the
branch's state before signing is the definition, the notes, the `out\`
builds and `release.ps1 -Part x86 -Check` PASS (section 49.3 above).

### The merge

`git merge --no-ff main` in the worktree, as earlier release branches took
their sources ("Merge <branch> (<sha>) into w47-rel019"): `aa942c9`, no
conflicts (main touched `uacstream.h`, `uacwdm.cpp`, `uac1\build.cmd`,
`streamtest.cpp` and docs; the branch only the version bumps of `b352b60`
and the definition, notes and README).

### Builds and host tests (development host A, at `aa942c9`, clean tree)

`src\audio\uac2\build.cmd wdmx86 free`, `wdm64 free`,
`src\audio\uac1\build.cmd wdmx86 free`: PASS each (gates included).
`src\audio\uac2\test.cmd` PASS (26 suites: 10525 assertions and 1143 byte
mutations, 500107 watchdog checks, 21 topology cases, 525491 volume checks,
infchk with 36 mutations refused), `streamtest.cmd` PASS (the open gate
refused at once, the closing gate waited for), `src\audio\uac1\test.cmd`
PASS (565533 checks, Od and O2). Log `E:\xhci98-scratch\w49\rel0110b\bld.log`.

| Image | SHA-256 | Tested image (from `34f1896`) | Difference |
|---|---|---|---|
| uac2wdm.sys x86 | `e89b1e4dcbcd66a5d6e5394d79366781fb753a59e07d383cfa51b066e8e85b1c` | `20c1a9e7...` | PE timestamp, checksum, version resource 0.1.10.0 -> 0.1.11.0 (`b352b60`); 9 bytes |
| uac2wdm.sys x64 (unsigned) | `013051fb3aab667c0abdc8600be061b44cdc88db0a8104a5042635306410251d` | `408d64ea...` | the same three; 10 bytes |
| uac1wdm.sys x86 | `ecef19fac88a1edd0fe04355e0a7bce6b42dcda6f08d797295c29b981a820c64` | `710da697...` | PE timestamp, checksum, and the link order of six this-adjusting thunks (and the six vtable slots that point at them); 27 bytes; sources equal |
| uac2wdm.inf (out) | `58e42ad64d83c0f9f6eaa9e4d43a62e0d57659a6fd65a3f75a20a24c2c112a52` | | DriverVer 0.1.11.0 (`b352b60`) |
| uac1wdm.inf (out) | `e542504b9f12c6c0bfcee9d6cc3436bc4931cbd903bcb12273e66279ed3b84e3` | | DriverVer 0.1.1.0 (`b352b60`) |

Build stamps unchanged: `Uac2Build` 29, uac1 5. So the images are not
byte-identical to the ones the 49.8 rounds ran; the code is the tested
code but for uac1's thunk order.

### The definition and the notes (`9cb28de`, reviewed)

Each uac2 folder's STAGE note and uac1's begin their 0.1.11.0 / 0.1.1.0
entry with "an open waits up to 2 s for the previous stream's queued
destroy (uac2.md 27; found on ME under its own UHCI, record 2026-11-09
uac-uhci-stalls)"; the header names it. `docs\release-notes.md`: the
"Known issue, uac2 on ME" line replaced by the fix's line.
`release.ps1 -Part x86 -Check`: PASS
(`E:\xhci98-scratch\w49\rel0110b\relcheck-x86.log`). Review: no material
findings.

### For the owner

`E:\xhci98-scratch\w49\rel0110b\rel0110\`: `x64-to-sign\` (the ten
unsigned x64 images; only the two uac2 copies changed), `SHA256-x64.txt`,
`OWNER-STEPS.txt`. Nothing signed, merged, pushed or tagged.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** 0.1.10 was first prepared on
usb-wdm-free branch `w48-rel0110` (the release manifest, the release notes draft and the README; every changed package
built, x86, and the x64 images unsigned for the owner). A provisional pin and the installer's sum were proved by
install legs on 98 SE, 2000 and XP on xHCI98 branch `w49-wdm` (`d3e050d1`, `50cc8c70`) and not merged then, because a
pin naming an untagged commit stops the packager's self-tests wherever the driver-set checkouts are present. After the
owner signed the x64 images and tagged the release `v0.1.10` (annotated, 2026-11-11), `wdmfree-pin.txt` was written
again from the tag (its Commit line and every SHA256 line from the tag's blobs), with `INST_WDMFREE_SUMS` and record
26's pin sentence (`e75fb247`); the release's `LICENSE-docs.md` is carried at the top of `USBDRV\FREE` (`0c8d06e7`);
usb-wdm-nonfree is pinned from its tag `v1.4.0` (`9c455d72`); `w49-wdm` was merged on 2026-11-12 (merge `7c414631d`).
The task was closed on 2026-11-13 by the owner for what it delivered; publishing the release moved to 49.final.
(Later: 49.final was closed by the owner on 2026-11-14; the publishing is not recorded in this sheet.)

## 49.22 and 49.23 MSI stability improvements

MSI stability improvements: the work on a separate component is recorded with that component.

## 49.34 (design) A Bulk-Only drive kept across a resume that re-initializes the controller: two captures of the stick (2026-11-11, worker bkeep)

Worker bkeep, branch `w49-bkeep` from `03d417940`: design only (design record 20 section 4.3.4), no build. Two
readings taken to settle what the design could not take from the record: what Windows 2000's own stack sends a
Bulk-Only stick across a hibernation, and where the stick's transport stands at the power-off under copy load.
Development host A, QEMU `asm4242-20261110-docks` (11.1.2), TCG, headless, one two-processor guest at a time.

### Vehicle

Copies of worker stall2's vehicle (`runs/run-49.md`, "49.32 (continued)"), never run in place: `snapB.qcow2` (the
Windows 2000 SMP guest, base driver `xhci98.sys` qemu flavour `bbc3f322...`), `stick-at-stall2.qcow2`,
`uas-at-stall2.qcow2` and the `xfer` folder, restored before each trial into `E:\xhci98-vm\p49\bkeep\smp\`.
Launcher `kit\lsmp4.ps1` (stall2's, changed only in ports 49600-49619, its own `pids.txt`, and `-Pcap`, which adds
`pcap=<file>` to the `usb-storage` device); trial script `E:\xhci98-scratch\w49\bkeep\br.sh <name> <ehci|xh>`: the FH
recipe unchanged (Explorer on D:, copy-compare loops on the UAS disk D: and the stick F:, `SetSuspendState`), the
stick captured in the run that hibernates (`<name>a.pcap`) and in the resumed run (`<name>b.pcap`), the resumed guest
quit by QMP at 300 s. Captures read by `E:\xhci98-scratch\w49\bkeep\pc.py` (usbmon records: control transfers,
CBWs, CSWs, data totals), outputs `R1a.txt`, `R1b.txt`, `R2a.txt`, `R2b.txt` beside it. The capture is the device's
side: control transfers and bulk packets; a port reset is not a packet and does not appear. Two void starts of R1
(the pcap path was not passed; stopped by PID, logged).

### R1: the stick on the EHCI controller, under Windows 2000's own `usbehci`, `usbhub20` and `usbstor`

Cold start 23:35:51, "now" 23:39:52 (the hibernation asked), QEMU ended by the hibernation 23:40:25; resumed
23:40:28, quit at 300 s; the four readings after the resume: 12 devices, 0 problem codes each.

- At the cold start: the firmware's enumeration (tag 3E7h: SET_ADDRESS, descriptors, SET_CONFIGURATION, Get Max
  LUN, INQUIRY, TEST UNIT READY answered CHECK CONDITION, REQUEST SENSE, READ CAPACITY, MODE SENSE), then Windows':
  descriptors, the language and serial strings (`W49STK01`), SET_CONFIGURATION, **Get Max LUN (class request FEh)
  answered 00**, then its commands.
- Before the power-off, under the loop: WRITE(10) commands of 64 KiB, three of 512 bytes, a **SYNCHRONIZE CACHE
  (35h)**, then one more WRITE(10) of 64 KiB whose CSW (status 0) is the last bulk traffic; nothing for the 20 s
  until the power-off. 689 CBWs, 689 CSWs. **The stick was at a transport boundary at the power-off**, and the
  flush was not the last command.
- After the resume: first the firmware's enumeration again (the same sequence; its TEST UNIT READY took the
  stick's power-on attention), then at +17 s Windows': GET_DESCRIPTOR(device, 64), SET_ADDRESS 2, GET_DESCRIPTOR
  (device, 18), GET_DESCRIPTOR(configuration, **9 bytes only**), SET_CONFIGURATION 1, and **the next transfer is the
  class driver's CBW**: a WRITE(10) at LBA 6060h, the next block run after the last write before the sleep (5FE0h +
  80h). No serial string read, no Bulk-Only Mass Storage Reset, no CLEAR_FEATURE, no Get Max LUN, no CHECK
  CONDITION on the first command (the firmware had taken the attention). 3889 CBWs and 3889 CSWs in the 300 s, every
  CSW matched.

### R2: the stick on xhci98 (the base driver's resume keep drops it)

Cold start 23:47:39, "now" 23:51:33, QEMU ended by the hibernation 23:52:07; resumed, quit at 300 s.

- At the start: this driver's enumeration (descriptors, the BOS, the serial), SET_CONFIGURATION, and a SET_INTERFACE
  alternate 0 that the bus sends inside its own SELECT_CONFIGURATION ("interface selected" in the trace, the same
  millisecond), then the class driver's Get Max LUN (answered 0) and its commands.
- Before the power-off: WRITE(10) commands, then a **SYNCHRONIZE CACHE (35h)** whose CSW (status 0) is the last bulk
  traffic, about 10 s after "now" and 23 s before the power-off: **a transport boundary**. 902 CBWs, 902 CSWs.
- The keep pass (`dbt-R2b.txt` 23:52:36.716): `port/flags/why=0110A508` - flags `0x10A5`, no URB outstanding, why 8
  (Bulk-Only) - so the stick was dropped, as in every one of 49.32's eleven; the UAS disk (`0414E500`) and the tablet
  restored. Then the firmware's enumeration of the stick, and this driver's fresh enumeration of it as a new device.

### Reading

- Windows 2000's own stack carries a Bulk-Only stick across a hibernation by addressing it again, reading its
  device descriptor and the head of its configuration descriptor and setting the configuration; it sends nothing of
  the Bulk-Only class and the class driver goes on with its next command. It does not read the serial (so, with
  49.32's E1, it does not tell a stick with another serial on the same port). This driver's restore (design record
  20 section 4.3.2, K5 and K6) sends the same and reads more.
- In both trials the stick was between commands at the power-off (R1: after a WRITE that followed the flush; R2:
  after the flush), with no URB outstanding at the keep pass in R2, matching what 49.32's
  traces show at the base driver's keep pass (no URB outstanding on the stick's PDO in all eleven hibernations with
  copies running). One trial each: comparative evidence, not a rate.
- Fed into design record 20 section 4.3.4 (commit `b18db2554`), M6 and M15.
- The firmware's own enumeration at the resume takes the stick's power-on attention before Windows runs (in QEMU,
  SeaBIOS); a real machine's firmware may or may not do the same.

## 49.5 (bench) and 49.22 (bench, a first try): the owner's readings of the night of 2026-11-11 (written by the coordinator from the files brought back)

**49.5, USB audio beside file copies, a real 98 SE computer (the owner's P14s Gen 1; this tree's package, release flavour, `xhci98.sys` 50bdb9d4...; Windows' own `usbaudio.sys`, device "USB Audio Device"; the kit of "49.5 (continued)" with its COM1 writes taken out; `XhciDmaStaging` 0 only; files kept by the owner).** One pass of three runs, each of eight plays of the 4 s tone:

| Run | Plays | Copy rounds | Plays that took about 11 s |
|---|---|---|---|
| A, audio alone | 8 of 8 PASS | - | play 1 (10954 ms) |
| B, copies beside it | 8 of 8 PASS | 20 of 20 PASS | play 4 (11294 ms) |
| C, the control, copies on C: only | 8 of 8 PASS | 20 of 20 PASS | plays 1 and 3 (11299 and 11327 ms) |

Every other play took 4000 to 4019 ms. The four long plays are all inside the kit's 18 s limit and all about 7.3 s over; one is in the run with no copies at all and two in the control, so they do not go with copies to the USB disk. Each coincides with a DOS window starting or ending (the first play after the batch file starts; the copy window's start and its end): a pattern from four events, its cause not read. The copy loop was too short for this computer: 20 rounds took 15 s (B) and 22 s (C), so the copies overlapped only the first three plays ("the copy loop had ENDED before the last play ended"). Not read: `XhciDmaStaging` 1; the overlap of all eight plays. The kit was then changed so that in the "both" mode the loop runs until the last play has ended (by a kit worker; run in a 98 SE guest, not yet on the bench), and the owner's first try of the changed kit printed "Bad command or file name" twice, its cause not established (the drive scan was widened and made to say when it finds no kit).

The owner's 98 SE computer offers no Stand by in its Shut Down dialog, and did not before xHCI98 was installed: the standby readings of 49.12, 49.30 and 49.31 cannot be taken on it.

**49.22, the MSI stability improvements' bench build of a test driver on a real computer, a first try that gave no reading (an HP laptop of 2012, Windows XP SP3 x86, "ACPI Multiprocessor PC" (`halmacpi.dll`, APIC mode), 4 logical processors; the device its built-in Intel 7 Series USB 3 controller at bus 0, device 20, function 0, which this computer's firmware gives no interrupt under 2000 and XP; a kit worker's kit).** The inventory step ran and changed nothing. The owner had disabled the controller in Device Manager and cold-started, as the kit's run list said; the install step reported the test driver installed and enabled, but at every later step the device still read disabled (problem 22), and no line of the driver's log was ever written: the test driver never started, so the own-line and ISA steps tested nothing. After the removal step the device was left with no driver (problem 1). Read from the worker's code afterwards: Device Manager's disable is recorded for the hardware profile, the kit's enable cleared only the global flag and printed its result without reading the state back. Also expected on this computer once that is put right: Windows refuses to start a device its firmware gives no interrupt (Code 12) unless the driver takes the interrupt out of the requirements as `xhci98.sys` does from `3.4.2.0`; the bench driver is being given the same means. Nothing was claimed or programmed on the computer.

## 49.23 a separate component

MSI stability improvements: the work on a separate component is recorded with that component.

## 49.10 The ME first-boot fatal exception 0E: the worker's operation split, the controls, and a fixed batch with and without the package (workers me0e and me0e3, 2026-11-11 to 2026-11-12)

(Coordinator's note at the fold, 2026-11-12: where this section says the fault "is not in" the member operation, the poll or the re-enumeration, read "none of them is necessary for the faults seen". The controls show faults with each removed; they do not show that none of them contributes to the higher rate found with the package installed, whose faults are at other addresses.)

Worker me0e (a first worker to 11:20, a replacement from 12:30), development
host A, branch `w49-me0e` from `980245c14` (3.5.0.0), worktree
`E:\xhci98-wt\w49-me0e`. QMP 47900 to 47904; every QEMU start and stop in
`E:\xhci98-vm\p49\me0e\pids.txt`. Kit scripts `E:\xhci98-scratch\w49\me0e\kit`,
VMs, `results.txt` and kept logs in `E:\xhci98-vm\p49\me0e`.

### Build and kit (diagnostics 1, `ff544a565`)

`ff544a565` (qemu flavour only): `XhciCmQemuSplit`, a test value whose bits
leave parts of the 98 SE / ME CONFIGMG worker out (001 the opening's
baseline, 002 the member operation, 004 its vanish step, 008 its PCI route
and numbering, 010 its `Reenumerate_DevNode` call, 020 what follows it, 040
the ticks, 080 the call workers), and one step byte (80h | code) written
straight to the debug console as each step and each CONFIGMG service of
the worker begins. `build-driver.cmd all`, x86, `XHCI98_SELFTEST_SKIP_OK=1`:
"BUILD + GATES PASSED (release qemu)" (`gated-1.log`); `make-package.ps1`
release and qemu x86 (`pkg.log`); kit `E:\xhci98-vm\p49\me0e\xfer`
(`SHA256.TXT`): release `KIT\98-me\cm\xhcicm.sys` `FF9C03D2...`,
`KIT\98-me\xhci98.sys` `13C8EE68...`; qemu `Q\xhcicm.sys` `B28FBA73...`,
`Q\xhci98.sys` `2427D08A...`. Emulator
`E:\xhci98-vm\q35-bases\bin\asm4242-20261110-docks\qemu-system-x86_64.exe`
(`4959a9ab...`), TCG, `-cpu pentium3 -smp 1 -m 512`.

### Method

As run-48's fair batch (w48-boot0e2), with its arm A as the base. One
install image per arm (`prep.sh`): a fresh overlay of the q35 ME base booted
with `-Usb4 adl` and nothing attached, the wizards answered with Enter,
`D:\IM.BAT <arm> <reg file>` from Start, Run (`INSTALL /console /yes
/nopause /thunderbolt /stor98`, the qemu `xhcicm.sys` and `xhci98.sys`
copied over the release ones, then the arm's `.REG` file into
`HKLM\System\CurrentControlSet\Services\USB`), then Shut Down before the
first boot after the install. Each trial (`trial.py`): the arm's image
copied afresh (and a fresh NVMe overlay), a cold start with chain's dock
model at power-on (`usb4-device,id=enc0,bus=u4.0,port=1,serial=USB4EMU01,
dialect=usb4,downstream-ports=1,chain-power-controller=off,drive=nvme0`), a
QMP screendump every 10 s; from 30 s on, a 720x400 text screen is read from
the VGA buffer for "exception", "error has occurred" or "Error:". The trial
ends at a fault, or 60 s after the worker's first step byte once the
screen is graphics, or at the cap. Three slots, the arms interleaved in
each, so host load falls on all arms alike; host load sampled before each
start (`load.ps1`).

The runner's words in `results.txt`: **FAULT** = the text screen "An error
has occurred ... Error: 0E : <selector> : <address>" was read (kept:
`logs\<tag>-text.txt`, `-fault.png`, the debug console as it stood then
`debugcon-<tag>-atfault.log`, and after a key `-afterkey.png` and the full
log); **clean** = no such screen by the trial's end, the screen in graphics
mode (the boot is then at "Updating hardware information database" or the
Add New Hardware wizard for the dock's bridge; first boots of this image
show those); **gone** = the screendump request failed (QEMU no longer
answering QMP): not read. Checked by hand: `mO-0-03-text.txt` ("Error: 0E :
0187 : BFF7B018"), `mO-0-03-afterkey.png` (the black text screen that
follows the key, as dock5 read), the 13 faults' logs (below).

### Batch 1: A (split 0), N (002, no member operation), O (0C3: opening's guards only, no member operation, no ticks, no call workers)

Rounds 1 to 33 in three slots (round 34 was cut off with the wave: its
three boots are the "gone" lines, and their QEMUs were stopped at 12:30).
Host CPU at the starts 47 to 99 % (mean 86), 9 to 19 other QEMU processes.

| arm | boots read | fatal 0E | not read | faults' host CPU at start (%) | clean boots' (min-mean-max) |
|---|---|---|---|---|---|
| A, split 0 | 100 | 7 | 1 | 71 to 99 (mean 88) | 52-87-99 |
| N, 002 | 99 | 3 | 1 | 87 to 93 | 47-86-99 |
| O, 0C3 | 99 | 3 | 1 | 88 to 94 | 47-85-99 |

The 13 faults (address as the screen gave it; the worker's last step
bytes as the debug console stood at the fault):

| boot | screen | worker's last steps |
|---|---|---|
| mA-1-01, mA-2-09, mA-2-10, mA-1-32 | 019F : BFF62170, BFF77328 (2), BFF7B018 | a tunnel-id tick's walk (5A, Get_Device_ID / Get_Child / Get_Sibling), then 5E: waiting |
| mA-1-11, mA-1-19, mA-2-31 | 019F : BFF775DD, BFF6E365, BFF7B018 | `_PCI_Read_Config` / `_PCI_Write_Config` (0A, 0B): inside the bridge numbering |
| mN-2-07, mN-0-14, mN-0-32 | 019F : BFF7B018 (2), BFF775DD | a tunnel-id tick's walk, then 5E: waiting |
| mO-0-03, mO-2-22, mO-0-32 | 0187 : BFF7B018 (2), BFF77328 | 40 41 00 01 04 42 45 5E (the opening: Get_Version, Locate_DevNode, Get_Device_ID), then one or two idle turns 40 5E: waiting |

The selector is 019F in every A and N fault and 0187 in every O fault
(three LDT slots lower; O makes three threads fewer, the call workers).

**What the counts can and cannot tell.** Run-48's estimate was 6 in 43
(14 %; 95 % interval 5 to 28 %). A's 7 in 100 (interval 3 to 14 %) is at
the low end of that and puts the rate nearer 1 in 14 than 1 in 7. At 1 in
7, 99 boots without the faulting part would show 0 and with it about 14;
3 in 99 is neither. Against A's own 7 %: 3 or fewer in 99 has probability
0.08 if the rate were unchanged, so neither N nor O is shown to differ
from A (Fisher, one-sided: A against N 0.17, A against N and O together
0.10). What the counts do tell needs no statistics: **the fault occurs
with no member operation at all (N), and with no member operation, no
ticks and no call workers (O), the worker waiting**. So it is not in the
vanish step, the PCI route, the numbering, `Reenumerate_DevNode` or what
follows: the split the roadmap asked for ends with none of those. And
run-48's "none in 44 that do not run it" could not carry its verdict
alone: at O's 3 %, 44 boots show no fault 26 times in 100.

### Where the connection-manager thread was (batch 1's 13 logs)

The log's lines leave only when the CM thread drains the ring, so each
faulting log has a gap where the screen stood. In all 13 the last line
before the gap is at 5.07 to 6.36 s of the CM's clock
(`cm.reenum.up.poll=00000303`, `cm.brnum.9x.pci-open` or
`cm.qemu.split.member`), and the first line after it is
`cm.nvlink.up.waited` with a wait of 20.8 to 22.9 s where a clean boot
logs 1.0 s: **the CM thread was inside `CmNvlinkExpose`'s wait for a
function to answer** (cm_nvlinkos.c: on 98 SE and ME a fresh exposure of a
silent bus waits `CM_NV_ANSWER_MS`, 1 s, scanning the member's secondary
bus through `HalGetBusData` every 50 ms, with `CmNhiWaitService` and a
50 ms `KeDelayExecutionThread` between) - the third or the fourth such
wait of the boot, the two that follow the bridge poll (clean boots: 5.0 to
6.0 s and 6.0 to 7.0 s). Run-48's logs fit: its faults stop after
`version` (inside the 2 s bridge poll, `cmPoll`, the other 50 ms loop of
the same thread) or after `up.poll` / `pci-open` (inside these waits). Arm
B (`XhciCmReenum` 0) makes the first two waits (1.2 to 3.3 s) and neither
the poll nor the later two.

### Batch 2: the controls again (A, B = `XhciCmReenum` 0, D = no xHCI98)

Images `me-B` (`IM.BAT` with `NOREEN.REG`; read back as `cm.reenum=00000000`)
and `me-D` (nothing installed). Three slots, A B D interleaved, 80 s cap, to
13:18, then B and D alone in one slot beside batch 3 to 14:35. Host CPU at
the starts 53 to 100 % (mean 91), 8 to 18 other QEMU processes.

| arm | boots read | fatal 0E |
|---|---|---|
| A, split 0 | 32 | 0 |
| B, `XhciCmReenum` 0 | 51 | 0 |
| D, no xHCI98 | 51 | 0 |

A's 0 in 32 is within its rate (probability 0.10 at 7 %, 0.28 at 3.9 %).
With run-48's and dock5's boots: B 0 in 67, D 0 in 72 (and C, no
connection manager, 0 in 15).

### Diagnostics 2 (`354c21541`) and batch 3: S (1000), P (1300), V (1400)

`354c21541` (qemu flavour only; reviewed, working tree, "approve"):
`XhciCmQemuSplit` bits for the connection-manager thread's two 50 ms loops
at a tunnel's coming up - 100 the bridge poll not made (`cmPoll` returns
no reading), 200 an exposure's answer wait not made, 400 that wait without
its scans, 800 without `CmNhiWaitService`, 1000 the thread's own step
bytes (71 to 7C: around each `HalGetBusData` read and around the two
loops' pauses). `build-driver.cmd all` x86: "BUILD + GATES PASSED (release
qemu)" (`gated-2.log`); kit `E:\xhci98-vm\p49\me0e\xfer2` (qemu
`Q\xhcicm.sys` `C8F2583C...`). The plan was put to review first
(`review-plan-b3.txt`, answer `review-plan-b3-result.txt`): arms kept, the
reading narrowed as written below.

Arms, each a full install differing only in the value: **S** = 1000 (the
whole path, with the thread's step bytes), **P** = 1300 (no bridge poll and
no answer wait: `up.poll=00000400`, `up.waited=00000000`; the worker's
operation then begins about 3 s earlier), **V** = 1400 (the answer wait
kept at its 1 s with its pauses and `CmNvlinkStep`, without its repeated
scans; the bridge poll and the late scan unchanged). Slots 1 and 2, S P V
interleaved, 13:38 to 15:19; host CPU at the starts 45 to 99 % (mean 85), 7
to 15 other QEMU processes.

| arm | boots read | fatal 0E | the CM thread at each fault (last step bytes) |
|---|---|---|---|
| S, 1000 | 51 | 2 | mS-2-03: in the 3rd answer wait, inside its pause (72 last; the scan before it had returned); mS-1-10: in the 4th answer wait, the worker between `_PCI_Read_Config` calls of the numbering |
| V, 1400 | 50 | 2 | mV-2-07 (4th wait) and mV-1-22 (3rd wait): 73 75 72, inside the wait's pause |
| P, 1300 | 51 | 0 | - |

"No fault" is "none within the trial's window" (80 s; every fault of the
day stood on the screen by 31 s).

Read with the review's cautions:

- **The answer wait's repeated `HalGetBusData` scans are not needed for
  the fault** (two V faults).
- At the four faults our thread was not executing: it had entered the
  wait's pause (`cmSleep`: `KeDelayExecutionThread`, then `CmNvlinkStep`)
  and had not returned.
- P's 0 in 51 against S and V together (4 in 101) is not significant by
  itself (Fisher, one-sided, 0.19; 0 in 51 bounds P's rate below 5.7 % at
  95 %). Pooled over the day and run-48: boots in which the CM thread
  runs the bridge poll and the later answer waits (A, N, O, S, V; run-48's
  A, E, H) 17 faults in 431 today (3.9 %) and 4 in 39 then; boots in which
  it runs neither (B, C, D, P) 0 in 205; today's alone against today's 154:
  p = 0.006. And every one of today's 17 faults has the CM thread inside
  the third or fourth answer wait. So the verdict "ours" stands, but its
  object changes: **not the worker's member operation; the fault needs
  the connection-manager thread's 50 ms loops after the worker's start
  (or the 4 s they take), and comes while that thread is in them.**

### Diagnostics 3 (`809ba436d`) and batch 4: the loops replaced by bare waits; a fault with neither loop

`809ba436d` (qemu flavour only; plan and diff reviewed, "approve"):
`XhciCmQemuStorm`, a loop of waits of known content run once per load
where the first bridge poll for a tunnel's coming up begins (kind 1
`KeDelayExecutionThread` alone, 2 a `KeWaitForSingleObject` timing out, 3
with reads of an absent bus, 4 with `CmNvlinkStep`, 5 with reads of the
NHI's function; each turn's wait and the loop's run in the value).
`build-driver.cmd all` x86 "BUILD + GATES PASSED (release qemu)"
(`gated-3.log`); kit `E:\xhci98-vm\p49\me0e\xfer3` (qemu `Q\xhcicm.sys`
`53DEA4A8...`).

Arms on split 1300 (no bridge poll, no answer wait), three slots
interleaved, 15:20 to 16:07, stopped at about 20 boots an arm once Q had
answered the question:

| arm | value(s) | read back | boots read | fatal 0E |
|---|---|---|---|---|
| X1 | 1300 + storm 01032190 (bare delays, 50 ms asked, 4 s) | 38 turns in 4.0 s | 20 | 0 |
| X2 | 1300 + storm 01FA0190 (one 4 s delay) | 1 turn, 4.26 s | 20 | 0 |
| X3 | 1300 + storm 01005190 (5 ms asked, 4 s) | 162 turns in 4.0 s | 21 | 0 |
| Q | 1300 alone (P again, on this build) | `up.poll=00000400` | 21 | **1** |
| T | 1000 (S's image; the image made for this build stuck in a hardware wizard and was not used) | - | 21 | 0 |

`mQ-2-07` ("Error: 0E : 019F : BFF62170"): no bridge poll and no answer
wait ran. The worker had made its three `Reenumerate_DevNode` calls (CM
clock 4.70 s) and the first no-IRQ walks (4.94 s); the log then stops
until the key. The CM thread's last step bytes are `HalGetBusData` reads
of the late nvlink scan. **So the two 50 ms loops are not needed either**
(P and Q together: 1 in 72). What the day's 18 faults share is the
moment - 5 to 7 s of the CM's clock in every arm, whatever the CM is
doing then - and `XhciCmReenum` 1.

### Batch 5: `XhciCmReenum` 0 and no connection manager, on matched images - a fault in each

Plan reviewed first (`review-plan-b5.txt`, answer folded: images made
together from one kit, a fixed stop rule: a fault in both control arms, or
250 boots an arm). Images from kit `xfer3` (`809ba436d`): **F1** =
`XhciCmReenum` 0 with split 1000 (read back `cm.reenum=00000000`), **F2** =
`IC.BAT` (`INSTALL /console /yes /nopause /stor98`, no `/thunderbolt`),
**F3** = split 1000. Three slots interleaved, 16:18 to 17:39.

| arm | boots read | fatal 0E | screen |
|---|---|---|---|
| F1, `XhciCmReenum` 0 | 58 | 1 | mF1-2-13: "Error: 0E : 019F : BFF62170" |
| F2, see below | 58 | 1 | mF2-2-19: "Error: 0E : 016F : BFF62170" |
| F3, split 1000 | 58 | 1 | mF3-1-18: "Error: 0E : 019F : BFF77328" |

- **F1: the fault with `XhciCmReenum` 0.** The boot's log reads
  `cm.reenum=00000000`: no bridge poll, no member operation, no
  `Reenumerate_DevNode`; the call workers' openings were read back at
  4.26 s, the worker's last steps are a tunnel-id tick's walk and its wait,
  and the log stops there until the key. So `XhciCmReenum` 0 does not
  prevent the fault (whether it lowers its rate is not known: 1 in 58 here,
  0 in 67 before).
- **F2: the fault with no xHCI98 driver at all.** F2's install did not
  install: `INSTALL` with `/stor98` and no `/thunderbolt` printed its usage
  text and changed nothing (`me-F2\p-end.png`; the image's
  `SYSTEM32\DRIVERS` holds only the `xhci98.sys` the batch file copied
  there, which nothing refers to). F2 is therefore the base Windows ME
  image, as arm D - and run-48's arm C, made with the same command, was
  most likely the same. Its faulting boot shows the same screen at the same
  check (31 s), the address six of the day's faults with the connection
  manager have (BFF62170), and the selector 016F: six LDT slots below 019F
  and three below O's 0187, in step with the threads each configuration
  makes (lessons.md records 016F for 98 SE's KERNEL32 faults in QEMU with
  no USB controller). The debug console is empty, as it must be.

**So the fault is met in this emulator with no driver of ours loaded.**
The day's descriptive totals: with the connection manager installed, all
arms, 20 faults in 748 boots (2.7 %); with nothing of xHCI98, 1 in 109
today (D 0 in 51, F2 1 in 58; 0 in 36 in earlier runs). No difference is
shown (Fisher, two-sided, about 0.3), and none is excluded.

### Batch 6: the base rate without and with the release package (workers me0e and me0e3, 2026-11-11 17:45 to 2026-11-12 08:39)

Plan reviewed first (`review-plan-b6.txt`, answer `review-plan-b6-result.txt`).
Question: does the release package, installed as a user installs it, raise
the rate of a fault Windows ME shows in this emulator without it? Arms, all
three images made at 17:45 from kit `xfer3` on the q35 ME base: **D6** = a
fresh overlay, nothing installed (`me-D6\p-end.png`: the desktop); **R6** =
the release package as a user installs it (`IR.BAT`: `INSTALL /console /yes
/nopause /thunderbolt /stor98`, release drivers as installed, no test value;
`me-R6\serial-p.log` "===I done R6", `me-R6\p-end.png` "xHCI98 USB4 Router:
xhcicm 3.4.3.0, started", "Exit code 3", `XHCI98.TMP` pending); **Z6** = D6's
image (the same bytes, MD5 `59a2b171...`) started with `-Usb4 none` (plain
q35: no USB4 host router, no enclosure). D6 and R6 boot with the chain dock
model at power-on, as every earlier batch. Fixed count set in advance: 201
boots an arm (67 rounds in each of three slots, the arms interleaved in a
rotated order per slot), no early stop; R6 against D6 by Fisher two-sided on
the observed counts; Z6 beside them.

**Three runs, one counted.** (1) Rounds 1 to 3 (me0e, 17:49 to 17:59, 7
boots an arm, all clean), stopped at the hand-over: not counted. (2) Rounds
5 to about 42 (me0e3, 18:03 to 21:10): **aborted by a vehicle break** and not
counted. From about 21:03 the host's WMI answered `Get-CimInstance` with
"Call cancelled" (0x80041032); the shared launcher's process query after a
start threw, so a trial saw no PID while its QEMU ran on holding the slot's
QMP port, and `taskkill` (also WMI) left two QEMUs alive that the trial had
logged as stopped. Every later start in those slots failed at the port, and
the batch script moved on: slots 1 and 2 ran through their remaining rounds
with no boots (44 and 48 "no start"), slot 0 nine. No result is written for
a start that fails, so no boot was misread; three left-over QEMUs of mine
(PIDs 169956, 170248, 194696) were stopped by QMP quit. The kit was
repaired and the repair reviewed in four rounds until nothing material was
left (`review-b6b-vehicle*.txt` and their results): a kit copy of the
launcher with no WMI call (`kit\launch-q35-k.ps1`, two lines changed); the
QEMU held by a process handle and shown to be the trial's own before any
stop (image name, created after the launch began, the PID listening on the
slot's QMP port, `query-name` answering the trial's machine name); QMP quit,
then `TerminateProcess` on the handle, the exit verified; timeouts on every
helper; a failed text read counted as "gone"; any uncertain state halts the
slot instead of moving on. Four smoke boots (`xsmoke-0-01` to `-04`, not
counted). As the rule asks, all three arms then restarted from zero. The
aborted run's read boots, for the record only (not pooled, not tested): D6 2
in 111, R6 7 in 111, Z6 2 in 111. (3) **Batch 6c, rounds 101 to 167, the
counted run.**

Batch 6c ran in **two parts with a host restart between**: Windows Update
restarted the host at 02:02 on 2026-11-12, and every guest and script died
with it. Part 1, 21:28 to 01:59: 503 boots completed and classified. The
three boots in flight at 02:02 (`mR6-0-156`, `mZ6-1-157`, `mZ6-2-157`) had
not been read and were treated as not run. Before part 2 every image was
checked: `qemu-img check` found no errors on `me-D6`, `me-R6` and `me-Z6`
`inst.qcow2` or on the ME base; their modification times were still those of
the preparation (17:45 to 17:47); D6 and Z6 still had MD5 `59a2b171...`;
R6's done line was present. No image was rebuilt. Each slot resumed at its
next tag (the three in-flight tags re-run first). Part 2 ran from 07:41 to
08:39: 100 boots. Reconciliation: 603 tags, 67 per arm in each slot, no
duplicate. One boot, `mD6-1-147` (clean, 01:15), had lost its
`results.txt` and `pids.txt` lines to a concurrent append. Its result line
as `trial.py` printed it is in `b6c-1.log`, and it was restored to
`results.txt` (its last screen, `hand\mD6-1-147-cur.png`, is the desktop).
No boot was "gone" or "text". Results: `E:\xhci98-vm\p49\me0e\b6c-results.txt`
(one line a boot); summary `kit\sum6c.py`; statistics `kit\stats6.py`.

| arm | part 1 (before 02:02) | part 2 (after) | **total** | rate (95 % exact) |
|---|---|---|---|---|
| D6, nothing installed | 2 / 168 | 0 / 33 | **2 / 201** | 1.0 % (0.1 to 3.6) |
| R6, release package | 5 / 168 | 5 / 33 | **10 / 201** | 5.0 % (2.4 to 9.0) |
| Z6, D6 on plain q35 | 6 / 167 | 0 / 34 | **6 / 201** | 3.0 % (1.1 to 6.4) |

Host load at each start (load sampler; the CPU field could not be read for
60 part-1 starts while WMI was failing). Part 1: host CPU 43 to 100 %, mean
84 %, with 8 to 19 other QEMU processes (mean 14, together about 16 CPUs
busy). Part 2: CPU 43 to 100 %, mean 94 %, with 0 to 12 other QEMU processes
(mean 8, about 10 CPUs). Among the starts with the CPU field read, the mean
host CPU was 86 % for the faulting boots and 86 % for the clean ones; that
does not make their loads equal (60 starts unread, other guests' CPU
differing between the parts).

Every fault was read by the runner from the text screen ("An error has
occurred ... Error: 0E : <selector> : <address>", detected at 31 to 34 s;
the screens are read from 30 s on, so that is when it was seen, not when it
began). Checked by
hand: the fault screens of `mR6-2-12`, `mR6-1-16`, `mZ6-2-21` and `mD6-2-29`
of the aborted run, and `mR6-1-125`, `mZ6-1-124` and `mR6-1-160` of 6c, all
the blue "An error has occurred" screen with the text the runner logged.
Clean last screens checked by hand: `mD6-1-06`, `mZ6-1-06`, `mD6-0-131`,
`mZ6-0-130` and `mD6-1-147` show the desktop; `mR6-0-06` and `mR6-2-130`
show the Add New Hardware wizard for "PCI standard PCI-to-PCI bridge" (the
dock's tunnelled bridge, as every first boot with the package shows). The
release build writes nothing to the debug console, so the R6 logs are empty.

**Fault addresses** (descriptive; the endpoint stays every fault): D6 2 and
Z6 6, all `016F : BFF62170`. R6 10, all with selector `019F`, **none at
`BFF62170`**: `BFF7B018` 5, `BFF77328` 3, `BFF775B4` 1, `BFF775DD` 1 (the
aborted run: D6 and Z6 all `016F : BFF62170`; R6 one `019F : BFF62170` and
six elsewhere). The selector follows the thread count (016F with nothing of
ours, 019F with the connection manager), as in batch 5.

**The comparison.** R6 against D6: 10 in 201 against 2 in 201, **Fisher
two-sided p = 0.036**. The difference is 4.0 points (Newcombe 95 % 0.6 to
8.0). R6 against Z6: p = 0.45. Z6 against D6: p = 0.28.

What this shows, and what it does not:

- **The prespecified comparison supports a higher fault rate with the
  release package in this experiment**: 5.0 % against 1.0 %, a difference
  of 4.0 points (Newcombe 95 % 0.6 to 8.0), Fisher two-sided p = 0.036. As
  the plan's review set out, that points at *installing the package*: its
  drivers, the first boot's database update for their devnodes and the
  WININIT rename. It does not single out `src\cm` or any one driver. Each
  arm is one prepared image booted many times, so it says nothing of how
  other installations vary.
- The excess is not uniform across the two parts. In part 1, the larger
  part with the heavier host load, R6 against D6 is 5 / 168 against 2 / 168
  (exploratory: p = 0.45). Part 2 gave 5 / 33 against 0 / 33: half of R6's
  faults came in its last 33 boots. The two parts ran under different host
  loads (fewer other guests, higher CPU in part 2), and the result is a rate
  under these loads. The parts were not designed as strata; their difference
  is reported, not explained.
- Z6 faults at 3 % with no USB4 machine and nothing installed, at the same
  address as D6. **The fault occurs in this emulator without anything of
  ours** (F2 repeated: 8 in 402 boots without the package, all at `016F :
  BFF62170`). D6 and Z6 are different machines and are not shown to share
  one rate; their mixture, 8 in 402, is descriptive, and R6 against it (10
  in 201, p = 0.07) is exploratory: it neither replaces nor weakens the
  prespecified R6 against D6.
- R6's faults are at other addresses than D6's and Z6's. This fits two
  readings: the package adds a fault of its own, or it moves the same
  underlying fault (more threads, a different moment). These counts do not
  tell which.
- It is an emulator result (QEMU TCG, one vCPU, a 512 MB guest) under a
  heavily shared host. It says nothing of real hardware, where the fault
  has never been reported.

The aborted run, read on its own, pointed the same way (R6 7 / 111 against
D6 2 / 111); it is not pooled with batch 6c.

### Verdict so far

Proven (each by a kept boot):

1. The fault does not need the CONFIGMG worker's member operation (N, O),
   its ticks or the call workers (O), the answer wait's scans (V), the
   bridge poll or the answer wait (Q), the connection manager's re-enumeration calls (`XhciCmReenum`
   0: F1), or any driver of ours (F2). The roadmap's split (the vanish,
   the PCI route, the re-enumeration of the devnode) ends with none of the
   three.
2. `XhciCmReenum` 0 does not prevent it.
3. With the connection manager present, the faults' logs stop at 4.3 to
   6.4 s of the CM's clock (all 20 read; of the 18 in arms with the bridge
   poll and answer waits, 17 are inside the third or fourth answer wait,
   5 to 7 s, and one, mF3-1-18, inside the bridge poll's pause at 4.4 s),
   about 9 to 10 s after power-on, whatever our threads were doing; in
   the seven read with the CM thread's own step bytes (S, V, Q, F1, F3),
   the thread was in a wait's or the poll's pause or between two
   `HalGetBusData` reads
   and the worker was waiting or between two PCI.VXD calls.

Shown by the prespecified comparison (statistical, not by one boot):

4. Batch 6 (fixed 201 boots an arm): with the release package installed as
   a user installs it the fault rate was higher than on the same image with
   nothing installed, 10 in 201 against 2 in 201 (difference 4.0 points,
   95 % 0.6 to 8.0; Fisher two-sided 0.036). The fault also occurs with
   nothing of ours, on the USB4 machine (2 in 201) and on plain q35 (6 in
   201). The excess points at installing the package (drivers, first-boot
   database update, WININIT rename) in this emulator under this host load,
   not at any one driver, and was uneven between the batch's two parts
   (5 / 168 against 2 / 168 before the host restart, 5 / 33 against 0 / 33
   after).

Not shown: which part of the package makes the excess, and whether the
package adds a fault of its own or moves the base one (R6's faults are at
other addresses: none of its ten at the base's `016F : BFF62170`); whether
any of it exists on real hardware. Void: run-48's "none in 44 boots that do
not run the operation" as a verdict (too few boots for a 3 % fault, and its
arm C most likely installed nothing); run-48's attribution to the worker's
first member operation; the workaround.

Host load: batches 1 to 5 ran at 45 to 100 % host CPU with 7 to 19 other
QEMU processes; batch 6 at 43 to 100 % with 8 to 19 (part 1) and 0 to 12
(part 2), as given in its section. The mean host CPU at the start was
about the same for faulting and clean boots (batch 1: 88 % against 87 %;
batch 6: 86 % against 86 %, where read), which does not make their loads
equal; run-48 met the fault at 3 to 97 %.
A timing-dependent fault could still be moved by load through the guest's
pace under TCG (the fault's place on the CM's clock was 3.5 to 6 s in
run-48 and 5 to 7 s today), so rates measured today are rates under this
load. The deciding observations of batches 3 to 5 (a fault with the loops
removed, with `XhciCmReenum` 0, with no driver) are single boots each; batch
6 repeated the last one eight times (D6, Z6).

No fix: nothing in `src/cm` is changed in the release flavour. Three
commits on `w49-me0e`, qemu flavour only (`ff544a565`, `354c21541`,
`809ba436d`), each reviewed before its commit, each with a passed gated
x86 build (`gated-1.log`, `gated-2.log`, `gated-3.log`).

### QEMU processes

Starts and stops are in `pids.txt`, with two gaps covered elsewhere: the
lost lines of `mD6-1-147` (its result is in `b6c-1.log`) and the unlogged
start of the aborted run's slot 2 (170248). The three batch-6c starts cut by
the host restart are interrupted, unclassified attempts; the 603 counted
boots are completed observations. Stopped outside the runner: the three
round-34 guests of the first worker (74908, 62820, 65080, QMP quit at
12:30) and the image preparation that stuck in a hardware wizard (104604,
QMP quit at 15:19), the three left-over guests of batch 6's aborted run
(169956, 170248 and 194696, QMP quit at 21:10), and the three batch-6c guests
that died with the host restart at 02:02 (251072, 203864, 258944; never
read). None running at 08:40 on 2026-11-12.

## 49.35 The in-place recovery and a slow HCRST: an ASMedia ASM1042 "stopped answering" (2026-11-12, worker asm)

Branch `w49-rst` from `3.5.0.0` at `7c414631d`, with `3.5.0.0` merged in at `7e82e5a55` (mode 4),
`47ac9424c` and `c840aa772`.
Development host A. Design record 17 section 4.6.1 (new); records 07 and 17 corrected.

### The report

A user on `3.4.3.0` (Windows XP x64 SP2, an ASMedia ASM1042 `1B21:1042` rev 00, HCIVERSION 0.96, shared
line IRQ 19, release flavour, `XhciLogVerbosity` 3) saw the tray say the controller had stopped answering. The
machine's Intel 7 Series xHCI worked under the same driver. Two XHCITR report sets, both of the Intel controller
(`/c 0`). The ASMedia part is read from their decoded sections.

- The note ring, in order:
  - `iso.none`
  - `ctrl.failed.here=1`
  - `door.alias`
  - recovery 1: `ctrl.recover.begin=1`, the whole re-init, then `init.refused.step=9` (RESET) with status
    `00000001` (USBSTS: HCH, CNR clear)
  - recoveries 2 and 3: the same refusal from `XhciRecoverController`'s pre-check, with no `hc.*` notes, which
    means USBCMD.HCRST still read 1
- All three recoveries began at tick 0 of the tolerance clock.
- The result: "LATCHED FAILED: three recoveries failed in a row".
- The second report set: recovery 3 found the reset finished (`handoff.legctl.before=40000000`), wrote a fresh
  HCRST, and refused at step 9 again.
- PCI Command read 0002 (BME clear), against 0006 at the start.
- The first request's caller is release-silent: no `ctrl.hch`, `cmd.*` or `FatalStatus` note or counter was
  written.

### The cause, as read

- The recovery runs at DISPATCH_LEVEL with `InitBelowPassive`. Its HCRST and CNR waits are a 10 ms stall
  (`XhciWaitForBits`), and this part's reset takes longer.
- Nothing paced the attempts:
  - `XhciSlotInvalidateAll` wakes the thread;
  - `hcdRecover` re-requested at once;
  - every attempt was charged.
- BME was cleared by the driver's own DMA-stopped proof in `HcdEnumService`, because recovery 1 invalidated
  with `halted` 0 and its HCRST never completed. That proof was silent on success. It is a consequence of the
  failure, not its cause.
- The first trigger is not known. The candidates, all silent in `3.4.3.0`:
  - a Host Controller Event (`xhciHandleEvent` escalates every code);
  - a failed interrupter re-arm;
  - a command divergence.
- The strongest regression suspect is 3.4.2.0's separate 1024-TRB event ring (task 47.30; `evt.ring=00000400`
  in the reporter's log). Linux never gives a controller an event segment larger than one 256-TRB page. The user was asked
  to try `XhciEventRingTrbs` `256` on that controller.

### The change (commits 569d6ea85, 204696851)

- **The split.** `XhciRecoverController` returns:
  - `XHCI_RECOVER_RECOVERED`;
  - `XHCI_RECOVER_REFUSED` (charged, as before);
  - two new outcomes that are not charged: `XHCI_RECOVER_RESET_BUSY` (a reset still running, at the pre-check
    or at the reset step) and `XHCI_RECOVER_BME_RESTORE`.

  `hcdRecover` then works at PASSIVE_LEVEL with the power gate held:
  1. waits for the reset, sleep-polling every 10 ms for up to 1000 ms (`XhciTolResetSettle`);
  2. marks the reset proven (`RecoverResetProven`) when it was this driver's;
  3. puts a cleared BME back;
  4. runs the same recovery again, at most three times, without charging the window again. `xhciReset` adopts
     the proven reset (`reset.adopted`) instead of writing another.

  An attempt it cannot finish is charged once (`XhciRecoverCharge`). The proof never outlives its call.
- **Pacing.** After an attempt that did not recover, the next waits `XHCI_TOL_TICKS(500)` (13 ticks: at least
  540 ms, about 1.3 s nominal) from `RecoveryFailTick` (`XhciTolRecoverPaced`). The latch is unchanged.
- **Mode 4 (with the merge).**
  - The continuation re-reads `Msg4Barred` and `Msg4Lost` after each settle.
  - A success is not taken while a loss is latched: `ctrl.recover.msg4` 1 or 2.
- **Notes**, at most 4 per start for each escalation:
  - `evt.hc` (completion code)
  - `evt.rearm.failed` (IMAN)
  - `intr.unmask.failed`, `intr.mask.unproven`
  - `cmd.diverged` (reason in 31:28), `cmd.fatal` (code), `cmd.abort.stuck`, `cmd.age.reset`
  - `ctrl.fatal` (USBSTS)
  - `dma.proof` (every Bus Master Enable proof's verdict)
  - `ctrl.recover.resetbusy`, `ctrl.recover.settled`, `ctrl.recover.unsettled`, `ctrl.recover.bme`
- **Counters.** `XHCI_TOL_STATS` words 34-45, named in XHCITR's "tolerance counters" (Save Report):
  `ResetSettles`, `ResetSettleMsMax`, `ResetUnsettled`, `ResetBusy`, `RecoverPaced`, `RecoverReruns`,
  `HcEvents`, `HcEventCode`, `RearmFails`, `CmdDiverged`, `CmdFatal`, `MaskFails`.
  - XHCITR still accepts a 34-word driver; an older XHCITR shows the new words by number.
  - `XHCIHC_COUNTERS` (302 words after mode 4) is not touched.
  - `XHCI_EXTENSION` gains four ULONGs before `TrailingPad`, so the parity is kept. `ExtensionBytes` moves, and
    the key is printed.
- **qemu flavour.**
  - Fault `1B`: every HCRST held `arg` x 10 ms (255 for ever); port byte 0 holds HCRST, 1 holds CNR. It is
    applied at the HCRST write, at `XhciWaitForBits`' reads, at the pre-check, at the settle and at the
    adopted reset's check.
  - Fault `1C`: one Host Controller Event.

### Reviews

- Plan: two rounds, converged (the first: 7 findings, all fixed; the second: none).
- Code:
  - the first round: no findings;
  - a deeper round: one P3 (the proof outliving a restore refusal), fixed;
  - the last round: none.
- Merge:
  - one P2 (mode 4 loss during the settle), fixed;
  - one P2 (the same race in the rerun and the first call), fixed;
  - final round: none.

### Host tests (run alone before the gated build)

- `test_tol` 3675 checks, 0 failures:
  - `XhciTolResetSettle` against a fake that holds HCRST, and separately CNR, for 0, 5, 50, 500 and 1500 ms
    and for ever;
  - all-ones reads DEAD;
  - the pacing, including across the clock's wrap;
  - a model of `hcdRecover`'s loop with and without a cleared BME:
    - 0 and 5 ms recover with no wait;
    - 50 and 500 ms recover after one settle, with one HCRST written and BME restored before the one rerun;
    - 1500 ms and for ever reach the latch after three charged failures at least 13 ticks apart, never inside
      one tick;
    - a refused restore is charged once per attempt.
- `test_inj` 1443 checks, 0 failures.
- `test_snap` 541 checks, 0 failures.

### Gated build

Commit `204696851` (dirty: only the record 17 paragraph later committed as `b0a4443c3`), slot `gated.2`,
`XHCI98_SELFTEST_SKIP_OK=1`, x86 all then amd64 all, then `xhcitr\build.cmd`. Logs
`E:\xhci98-scratch\w49\asm\build-m1-{x86,amd64,xhcitr}.log`.

| Part | Result |
|---|---|
| x86 | BUILD + GATES PASSED (release qemu) |
| amd64 | BUILD + GATES PASSED (release qemu) |
| xhcitr | exit 0 |

The self-tests include the XHCITR snapshot self-test: "counter names: all 302 words agree", 53 runs.

| Binary | SHA-256 |
|---|---|
| xhci98.sys x86 release | `260fcfb0...` |
| xhci98.sys x86 qemu | `976649ca...` |
| xhci98.sys amd64 release | `f4f714da...` |
| xhci98.sys amd64 qemu | `8bae57e2...` |
| XHCITR.EXE | `d54b6a46...` |
| XHCITR64.EXE | `79ed80c5...` |

Copies are in `E:\xhci98-scratch\w49\asm\bin`.

### Legs

Emulator: the kit launcher's (`asm4242-20261110-docks`), TCG, q35. One guest at a time, at most 2 vCPU.

**98 SE** (1 vCPU, 512 MB, QMP 49640). Vehicle:

- the overlay `E:\xhci98-vm\p49\asm\w98\disk.qcow2`, a copy of worker c31's `d98` (c31's release kit
  installed);
- this tree's qemu x86 `xhci98.sys` (`976649ca`) put in place by `QS98.BAT` in tag a;
- `--nobus`: no USB device;
- faults written by `regedit /s INJ<x>.REG` to `Class\USB\0000` and `0001`;
- readings by `REP98.BAT` (`XHCITR /report` with this tree's `XHCITR.EXE`; the lines to COM1);
- the trace from debugcon 0xE9.

| Tag | Fault | Result |
|---|---|---|
| b | 1B 5, then 1C 21 | trace: `HOST CONTROLLER EVENT, completion code=15`, `controller reset requested, calls=1`, `HCRST held, ms=32`, `reset: HCRST never cleared`, `init REFUSED at step=9`, then a second run with no HCRST written, `init complete`, `No Op self-test completion code=1`. The report was lost: V98.BAT's DOS box held COM1 at "Write fault error writing device COM1". |
| c | 1B 5 (50 ms), then 1C 21 | **PASS**: `evt.hc=15`, `ctrl.failed.here=1`, `ctrl.recover.begin=1`, `ctrl.recover.resetbusy=1`, `ctrl.recover.settled=14` (20 ms slept), `ctrl.recover.begin=2`, `reset.adopted=1`, `ctrl.recovered=2`; counters `ResetSettles 1`, `ResetSettleMsMax 20`, `ResetBusy 1`; "recovery window 1 of 3" |
| d | 1B 150 (1.5 s), then 1C 21 | **PASS**: `evt.hc=15`, three attempts, each `resetbusy`, then `ctrl.recover.unsettled=2` (USBCMD.HCRST) and `ctrl.recover.refused=9`; "LATCHED FAILED: three recoveries failed in a row"; "begun 1644, 1617, 1590 ticks ago" (27 ticks apart: 1 s settle + the 13-tick gap); `ResetSettles 3`, `ResetSettleMsMax 1000`, `ResetUnsettled 3`, `ResetBusy 3` |

Evidence is in `E:\xhci98-scratch\w49\asm\legs\w98`.

**XP x64 SP2** (2 vCPU, 2 GB, QMP 49642). Kit `E:\xhci98-vm\p49\asm\k64` (a copy of worker leg's `k64` /
rlx64 kit, `tools\leg.py`). Vehicle:

- a fresh overlay of the q35 `xpx64` base;
- S.BAT imports 3.4.3.0's `xhci98-test.cer`;
- phase I: 3.4.3.0's `INSTALL /console /yes /nopause` (exit 3), then this tree's qemu amd64 `xhci98.sys`
  (`8bae57e2`) copied over the installed file (`FC: no differences encountered`);
- devices: a Bulk-Only stick (port 1), the tablet, a Full-Speed hub with a mouse and USB audio (port 3), and a
  UAS disk (port 4);
- faults by `reg add` on the controller's driver key (`INJ.BAT`);
- readings by this tree's `XHCITR64 /report` (`REP.BAT`), and `C.BAT` copies a 16 MiB file to the stick and the
  UAS disk and compares it.

Leg X1:

| Phase | Fault | Result |
|---|---|---|
| J | 1B 5 (50 ms), then 1C 21 | **PASS**: `evt.hc=15`, `ctrl.failed.here=1`, `ctrl.recover.begin=1`, `init.refused.step=9`, `ctrl.recover.resetbusy=1`, `ctrl.recover.settled=1E`, `ctrl.recover.begin=2`, `reset.adopted=1`, `ctrl.recovered=2`; the four devices reset again and came back; counters `ResetSettles 1`, `ResetSettleMsMax 30`, `ResetBusy 1`, `RecoverReruns 1`, `HcEvents 1`; copies STICKMX PASS, UASMX PASS |
| K | 1B 150 (1.5 s), then 1C 21 | **recovered** (not latched): `ctrl.recover.settled=3B6` (950 ms counted), `reset.adopted=1`, `ctrl.recovered=2`, `HcEventCode 21`. On NT a 10 ms `KeDelayExecutionThread` sleeps to the next system tick (about 15.6 ms), so the settle's 1000 counted ms is about 1.5 s of wall time. That is the same "longer, never shorter" rounding `XhciWaitForBits`' sleep phase has always had. On 98 SE the same fault latched (leg d). |
| L | 1B port 1 arg 50 (CNR held 500 ms), then 1C 21 | **PASS**: recovered after one settle (`settled=136`, 310 ms counted), `reset.adopted`; copies PASS |

Leg X2 (a fresh overlay, the same install and swap), phase M: 1B 255 (held for ever), then 1C 21. **PASS**:

- `evt.hc=15`, `ctrl.failed.here=1`;
- three times: `ctrl.recover.begin=n`, `ctrl.recover.resetbusy`, `ctrl.recover.unsettled=2`,
  `ctrl.recover.refused=9`;
- "LATCHED FAILED: three recoveries failed in a row";
- "begun 835, 808, 781 ticks ago" (27 ticks apart);
- counters `ResetSettles 3`, `ResetSettleMsMax 1000`, `ResetUnsettled 3`, `ResetBusy 3`, `RecoverPaced 48`,
  `HcEvents 1`, `HcEventCode 21`;
- the controller's devnode still Code 0.

Evidence is in `E:\xhci98-scratch\w49\asm\legs\X1`, `X2` and `k64-pids.txt`.

**XP x86 SP3, release no-regression** (2 vCPU, 1 GB, QMP 49641). A copy of worker c31's `dxp` overlay, this
tree's release x86 `xhci98.sys` (`260fcfb0`) put in place (`FC: no differences encountered`); a Bulk-Only
stick (port 1), the tablet and a UAS disk (port 4). `NR.BAT` writes to `C:\XNR.TXT` and types it to COM1 at
the end (the earlier `V.BAT` held COM1). Tag e, **PASS**:

- the devices listed, then a 16 MiB copy to the stick and the UAS disk, compared: PASS, PASS;
- the controller disabled and enabled (`devcycle`, both `ok (0)`), the same devices, copies PASS;
- standby (on this guest a hibernation, resumed by a cold start), the same devices, copies PASS;
- the report: `controller running`.

Evidence is in `E:\xhci98-scratch\w49\asm\legs\xp` (`serial-e.log`).

What the legs show: the driver's side of 49.35 against a reset held by the injection layer:

- the stall runs out;
- the refusal is uncharged;
- the PASSIVE settle;
- the adopted reset;
- one recovery;
- the paced latch;
- the notes and counters in a plain report.

What they do not show: the ASM1042's real reset timing, or why it raised the first request.

### Other paths below PASSIVE_LEVEL

- `InitBelowPassive` is set by the in-place recovery alone.
- The resume's reinitialization (`XhciResumeController` from `hcd_power.c`, at PASSIVE_LEVEL) and the start
  use the 1000 ms bound. The ASM1042 would pass there, as its start did in the report.
- The all-ones containment writes no HCRST.
- Windows 98 SE and ME run the same thread and paths.

### The event ring by the controller's version (commit 586873973)

The owner's decision after the reporter's second reading (`XhciEventRingTrbs` 256 cures the ASM1042;
the Intel controller stays on 1,024 and is fine): a controller below xHCI 1.0 takes the 256-entry ring in
the controller block by itself.

- A new value `XhciEventRing`: 0 automatic (the default), 256 small, 1024 large; any other value is read as
  0 and noted. The old `XhciEventRingTrbs` is no longer read. The INFs write 0 without overwriting.
- Automatic: the small ring when HCIVERSION is below 0x0100, the large one otherwise
  (`XhciEventRingPick`, route 5 "version").
- The note `evt.ring` carries the pick in bits 31:24 and the route in 23:16; `evt.ring.value.auto` and
  `evt.ring.hciversion` say why. XHCITR's snapshot reader names the line.
- The `qemu` flavour's `XhciQemuHciVersion` (latched at start) makes an emulated controller report 0.96.
- Reviews: five rounds; the last none.
- Host tests: `test_caps` (the pick for every value and version), the INF gate's self-tests (1923 checks,
  exit 0), the footprint files.
- The settings page and Setup rows for `XhciEventRing`, and Setup's removal of the stale
  `XhciEventRingTrbs`, are worker sets' part.

### Issue page 31 (commit 8ad09c5ae)

`docs/issues/31-asm1042-event-ring-stopped-answering.md` and its README row: the report, the two
snapshots read, the cause, the reporter's cure and what 49.35 changes. Three review rounds; the last none.

### The all-controllers snapshot (commit 9fe63c164)

Save Snapshot, the tray's quick save and `XHCITR /snapshot` without `/c` take every controller `xhci98.sys`
drives, one set each under one BASE: `BASE.D<k>`, `.T<k>`, `.B<k>`, `.P<k>`. `/c N` is unchanged. Record 25
revision 32, section 15 item 12; `snapshot-reader.md`.

- Reviews: eight rounds. The last round's one P3 was declined: with no xhci98 controller, `/snapshot NAME`
  runs `/c 0` as before and writes only the engine's names, which the engine refuses to overwrite, so an
  existing `NAME.D0` is not a collision.
- Host tests: `test_tr` 1827 checks, 0 failures (`test_snap_all`); `test_trset` 1975 checks, 0 failures.

### Gated build m2 (commit 9fe63c164)

Slot `gated.2`, `XHCI98_SELFTEST_SKIP_OK=1`. Logs `E:\xhci98-scratch\w49\asm\build-m2-{x86,amd64,xhcitr}.log`.

| Part | Result |
|---|---|
| x86 | BUILD + GATES PASSED (release qemu) |
| amd64 | BUILD + GATES PASSED (release qemu) |
| xhcitr | exit 0; the snapshot self-test "counter names: all 302 words agree" |

| Binary | SHA-256 |
|---|---|
| xhci98.sys x86 release | `903306e5...` |
| xhci98.sys x86 qemu | `b6f170e7...` |
| xhci98.sys amd64 release | `d12e6a3a...` |
| xhci98.sys amd64 qemu | `aac5e121...` |
| XHCITR.EXE | `3cfd77a7...` |
| XHCITR64.EXE | `1838e223...` |

Copies are in `E:\xhci98-scratch\w49\asm\bin2`.

### Leg X3: two controllers, the ring by version, the all-controllers snapshot (XP x64)

Kit `k64`, `tools\leg2.py X3 xpx64 49642 F` (2 vCPU, 2 GB). Vehicle as X1 (3.4.3.0 installed, this tree's qemu
amd64 `xhci98.sys` `aac5e121` copied over it), with a second `qemu-xhci` on its own root port and no device.
3.4.3.0's install left `XhciEventRingTrbs` on both controllers; no `XhciEventRing` is written. Phase I sets the
second controller's `XhciQemuHciVersion` to 0x96 (it reports xHCI 0.96). Phase S, after 120 s: fault `1B` 255
(the reset held for ever), then `1C` 21 on the second controller only; then this tree's `XHCITR64` (`1838e223`)
`/snapshot` with no `/c`, then `/snapshot /c 0`. **PASS**:

- `/snapshot`: "Snapshot saved in C:\RL\SN:", "Controller 0 (XHCI98HC0): B121558A.D0, .T0, .B0 and .P0.",
  "Controller 1 (XHCI98HC1): B121558A.D1, .T1, .B1 and .P1.", exit 0; the folder holds exactly the eight files.
- `.T0`: "controller running"; `.T1`: "controller LATCHED FAILED: three recoveries failed in a row". Each set's
  `.D<k>` names its own door (`/c 0`, `/c 1`).
- The ring, from each `.T<k>`: controller 0 `evt.ring=00010400`, "1024 entries, the larger ring: automatic, the
  controller is xHCI 1.00"; controller 1 `evt.ring=02050100`, "256 entries, the smaller ring: automatic, the
  controller is below xHCI 1.0 (0.96)". The stale `XhciEventRingTrbs` was not read (`evt.ring.value=FFFFFFFF`:
  no `XhciEventRing`).
- The Thunderbolt networking section: in `.D0` only; `.D1`'s says "in the first controller's device report of
  this run."
- `/snapshot /c 0`: one set under the engine's names (`B121559A.DEV`, `.TXT`, `.BIN`, `.PSC`), exit 0, as before.

The run's closing text ended its lines in CR CR LF (the message is built with CR LF and `printf` adds a CR);
fixed in the commit after 9fe63c164, not rerun in the emulator.

The first attempt's setup boot failed: the second controller carried a `usb-kbd`, which took QEMU's key events
before Windows had a driver for it. Without it the leg ran (`legs\X3-fail1` kept).

Evidence is in `E:\xhci98-scratch\w49\asm\legs\X3` (`serial-b3.log`).

### XHCITR build m3 (commit b586c34bf)

`xhcitr\build.cmd` alone in slot `gated.2`: exit 0. `XHCITR.EXE` `a2225e9a...`, `XHCITR64.EXE` `17f17d20...` (in
`bin2`). The drivers are unchanged from m2.

### The user documents (with the merge of `c840aa772`)

README's "Reporting a bug" and XHCITR's `README.TXT` name the snapshot's files for every controller
(`.D<k>`, `.T<k>`, `.B<k>`, `.P<k>`) and `/c N`'s single set; `README.TXT` adds `/snapshot`'s exit 9.

## 49.34 (code) The Bulk-Only resume keep built (2026-11-12, worker bkeep)

Worker bkeep, branch `w49-bkeep` (3.5.0.0 merged at `b592002b9` after the host's restart at 02:02, which killed
an x86 gated build of `83b3e5c64`: void). Commits, each reviewed and revised:
`aed173118` (the owner's decisions in record 20 section 4.3.4, M16), `5b36d493c` (the pure machine
`xhci_bot.c`, `test_bot` 159 checks), `aa8d4eb1a` (the UAS and link keeps' duplicate-serial fix, shipped
behaviour), `cde55f7ff` (`hcd_bot.c`, the bus's half), `83b3e5c64` (`XhciResumeKeepBot` at 0 in both INFs,
the gate rows, `tuning.md`), `7567646e0` (the qemu-only leg aid `XhciQemuBotDelayMs`).

### Builds

Q2 to Q5 name the qemu-flavour `xhci98.sys` builds staged for the legs.

All with `XHCI98_SELFTEST_SKIP_OK=1`, Python on the wrapper's PATH, check-pfw and check-vsd passing in every x86 log;
logs in `E:\xhci98-scratch\w49\bkeep\`.

| Commit | Build | Result | xhci98.sys |
|---|---|---|---|
| `7567646e0` | x86 all; amd64 release | BUILD + GATES PASSED (`build-x86-7567646e0.log`, `build-amd64-7567646e0.log`) | qemu `10DC8ABA...` (legs' Q2) |
| `5ee1d2fb2` | x86 qemu | PASSED | qemu `8935A52F...` (Q3) |
| `4ff6155f9` | x86 qemu; then x86 all and amd64 release | PASSED (`build-x86-4ff6155f9.log`, `build-amd64-4ff6155f9.log`) | qemu `e5ecbe0d...` (Q4), release `B91A5044...`, amd64 `20424A74...` |
| `b795f0381` (3.5.0.0 at `7e82e5a55` merged) | x86 all; amd64 release | PASSED (`build-x86-b795f0381.log`, `build-amd64-b795f0381.log`) | qemu `6417E1C7...` (Q5) |

### Vehicles and recipe

- 2000: copies of worker stall2's Windows 2000 two-processor guest (`snapB`, base driver installed), fresh images
  each trial, the qemu-flavour `xhci98.sys` under test and `XhciResumeKeepBot` set in
  `HKLM\SYSTEM\CurrentControlSet\Services\USB` by `PREP.BAT` into a snapshot; `FHK.BAT`: stall2's FH recipe (Explorer
  on the UAS disk D:, copy-compare loops of 10 rounds on D: and the stick F:, `SetSuspendState` 16 s later), then
  fourteen readings 60 s apart printing the device list and both loops' result files (every round compared with
  `fc /b`). Runner `E:\xhci98-scratch\w49\bkeep\bl.sh`; a STALL is a reading not ended in 180 s.
- XP: a copy of worker c31's XP SP3 q35 overlay (`dxp`), the same recipe (`FHKX.BAT`, D: UAS, F: stick; each recipe ends with
  a shutdown of its own after its readings), runner
  `blx.sh`; standby runner `bls.sh`.
- ME: a copy of worker stby's ME base with the package (`baseme`), a Bulk-Only stick added, runner `lme.sh`:
  start, report (C98.EXE writes and compares a file on each USB drive), standby (QMP suspended, wake after 20 s),
  report, shutdown.

### Legs

Before the merge (binaries of `7567646e0` to `4ff6155f9`):

| Leg | System, binary | Variation | Keep decision and restore | Loops after the resume | Result |
|---|---|---|---|---|---|
| K1 | 2000 SMP, Q2 | - | kept (latch 1, IDLE) but no capacity recorded: dropped | F 5/10, D 10/10 | drop path, no stall |
| K2 | 2000 SMP, Q3 | - | the same; trace: READ CAPACITY data in an MDL with flags 8Ah (locked, unmapped) | - | stopped early (reading taken) |
| K3 to K7 | 2000 SMP, Q4 | - | kept; medium check: TUR good, capacity 3FFFFh equal; restored in place | F 10/10, D 10/10 each | **PASS x5**, 12 devices, 0 codes, no Unsafe Removal box |
| U1 | 2000 SMP, Q4 | stick removed during the sleep | dropped (port empty) | F 2/10 | as designed, no stall |
| S1 | 2000 SMP, Q4 | another serial on the same port | dropped (serial differs) | F 2/10 | as designed, no stall |
| T1 | 2000 SMP, Q4 | same serial, image of another size | medium check 5FFFFh vs 3FFFFh: dropped | - | dropped as designed; **49.32's stall followed** (reading t1 never ended) |
| T2 | 2000 SMP, Q4 | same serial, same size, other contents | - | - | **VOID**: another worker's script typed into this guest on QMP 49601, about 11:24 to 11:28 |
| O1 | 2000 SMP, Q4 | `XhciResumeKeepBot` 0 | why 8 (Bulk-Only), as the base driver | F 9/10, D 10/10 | as before |
| D1 | 2000 SMP, Q4 | `XhciQemuBotDelayMs` 20000 | kept; reopened 20 s later, latch intact | F 10/10, D 10/10 | PASS; no request came in the window, so the latch was not exercised |
| P1 | 2000 one processor, Q4 | `-smp 1` | kept | F 10/10, D 10/10 | PASS |
| X1 to X3 | XP SP3, Q4 | - | kept | 10/10 both each | **PASS x3** |
| XO | XP, Q4 | value 0 | why 8 | 10/10 | as before |
| XU1 | XP, Q4 | stick removed during the sleep | dropped (port empty) | - | as designed |
| XR1 | XP, Q4 | another serial | the first run's QEMU wrote nothing (no serial or driver line) and ended at 11:09:21, read by the runner as the hibernation; the second run was a cold start and stopped at its logo | - | **no hibernation**: a fault of the vehicle run, not a reading ("49.34 (defaults and the settings)") |
| XT1 | XP, Q4 | image of another size | medium check: dropped | F 2/10 | as designed, no stall |
| XS1 | XP, Q4 | standby (value 3, hibernation off) | - | - | not read: XP refused standby ("System Standby Failed": the Cirrus display driver) |
| r0 | ME, Q4 | start, standby, shutdown (value absent: 0) | - | C98 compare OK on E: and F: after the wake | PASS |

After the merge (`b795f0381`, Q5):

| Leg | System, binary | Variation | Keep decision and restore | Loops after the resume | Result |
|---|---|---|---|---|---|
| r1 | ME, Q5 | start, standby, shutdown (value absent: 0) | - | C98 compare OK on E: and F: after the wake; drives E, F, G before and E, F after, as in r0 | PASS, no regression |
| M1 | 2000 SMP, Q5 | - | kept; medium check capacity 3FFFFh equal; restored in place | F 10/10, D 10/10 | **PASS**, 12 devices, 0 codes |
| M2 | 2000 SMP, Q5 | - | the same | F 10/10, D 10/10 | **PASS**, 12 devices, 0 codes |
| T3 | 2000 SMP, Q5 | rerun of the void T2: same serial, same size, other contents | kept (the residue accepted in M16: nothing on the bus tells the two apart) | F 10/10, D 10/10 | as designed |

XR2 (XP, Q4, another serial; run after the merge on the pre-merge binary, repeating XR1): XP resumed; the stick was
dropped (serial differs) and found again at once; seven readings: F 2/10, D 10/10, 11 devices, 0 problem codes. Its
60 s screenshot shows XP's Welcome screen, which follows every resume of this XP vehicle (X1's shows it too), and a
"Delayed Write Failed" box for F: (the stick removed with writes pending). The power-off at 12:31 is `FHKX.BAT`'s
own shutdown after the readings, as "49.34 (defaults and the settings)" records.

Void legs: T2 (another worker's keystrokes, 11:24 to 11:28, on QMP 49601), K2b and K3b (a second start caused by
editing the runner while it ran; each stopped by PID and logged in `pids.txt`), r0i (ME typed into before its desktop
was up; r0 repeated with longer waits), and the build and legs running when the host restarted at 02:02.

## 49.34 (defaults and the settings) XhciResumeKeepBot on by default for a hibernation on Windows 2000, offered and written by Setup, shown by the xHCI98 USB Tree (2026-11-12 to 2026-11-13, worker bkeep)

Branch `w49-bkeep2`, cut from `3.5.0.0` at `5653a1069` (49.34 merged). The owner's decisions of 2026-11-13:
- on for a hibernation on 2000, off on every other Windows and for every standby;
- Setup and the Tree offer the value, and Setup writes it at install (every value present after an install);
- XP's two swapped-stick legs (XR1, XR2) to be explained;
- the reopening latch exercised once;
- one 2000 hibernation with the installed default.

Development host A, QEMU `asm4242-20261110-docks` (11.1.2), TCG, headless, one guest at a time.

### Commits

| Commit | What |
|---|---|
| `53ee4c1b1` | No INF path writes `XhciResumeKeepBot`; absent or not a DWORD, the driver gives `XhciBotKeepDefault` (1 on 2000, 0 elsewhere); gate VAL-BOTABSENT (any root) with self-test cases on both files; footprints; tuning row; record 20 section 4.3.4 M18 |
| `cedb57cb9` | Setup: the row, asked on 2000 and later (0 or 1; 0 to 3 by `/set` and Advanced), NoInf, default `inst_keepbot_default` |
| `c57d4ef62` | The USB Tree: the advanced row beside `XhciResumeKeep`, default by Windows (`TRS_DEF_SYSTEM`), `TRS_EFF_LOW2` |
| `1614563d1` | Setup: an unset NoInf value reads "not set, the driver uses N" |
| `f055ec60a` | `3.5.0.0` at `c840aa772` merged (the installer's import list, the release notes) |
| `85e61ab08` | Setup writes the value itself at this Windows's default in a run that installs or updates the driver (`inst_keepbot_seed`, M19); the screens self-test's NT script and order gain the question `cedb57cb9` added |
| `4bcc03777` | `3.5.0.0` at `ba292831e` merged (w49-rst, 49.35); one conflict, the gate self-test's fixture block |
| `1eb85f316` | `3.5.0.0` at `2f05c3167` merged (w49-gate2: the build scripts) |
| (this commit) | this run sheet and "49.34 (code)" in `runs/run-49.md` |

Each was reviewed and revised. Host suites pass (test_bot 163
checks); the INF gate's self-tests pass (1914 checks in six parts).

**A latent self-test failure, found and fixed here.** `cedb57cb9` made the question asked on 2000 and later, which
shifted the installer's screens self-test (its NT answer script and expected order). The passing gated builds of
`c57d4ef62` and `1614563d1` had no installer EXE in `xhciinst\`, so the step read "absent". Built by hand at
`85e61ab08`'s parent, the EXEs failed the self-test ("the questions came in another order"); `85e61ab08` fixes the
script and the order, and its own `xhciinst\selftest.cmd` and the `4bcc03777` build's step pass with the EXEs there.

### Builds

`XHCI98_SELFTEST_SKIP_OK=1`, Python on the wrapper's PATH; check-pfw and check-vsd pass in each x86 log. Logs in
`E:\xhci98-scratch\w49\bkeep\`. A Q label names a qemu-flavour `xhci98.sys` staged for the legs; Q2 to Q5 are
"49.34 (code)"'s (its first build, `7567646e0`, was Q2).

| Commit | Build | Result | xhci98.sys |
|---|---|---|---|
| `c57d4ef62` | x86 all; amd64 release | PASSED (`build2-x86-c57d4ef62.log`, `build2-amd64-c57d4ef62.log`) | qemu `06D2BC64...` (Q6) |
| `1614563d1` | x86 all; amd64 release | first run FAILED at `xhciinst-selftest`, then PASSED (`build2-*-1614563d1.log`) | qemu `D5ACD809...`; release `E7D75B22...` (ID2's and FI1's) |
| `85e61ab08` | the installer, `xhciinst\build.cmd` (all five EXEs), then `xhciinst\selftest.cmd` | passed, 1035 checks (`inst-selftest.log`); these EXEs are FI1's and XFI's Setup | - |
| `4bcc03777` | x86 qemu, the installer EXEs present | PASSED, `xhciinst-selftest` passed (`build2-qemu-4bcc03777.log`) | qemu `35F1EBE3...` (Q7, M3's) |

The failed run of `1614563d1` met the installer EXEs built here by hand for ID1 and ID2: `XHCIINST.EXE` imports
`OpenEventA`, which 49.26's mark-service hold calls and `xhciinst-imports.allow` did not list; with the EXEs moved
out the step read "absent" and the build passed. Fixed on `3.5.0.0` by `c840aa772` (w49-mark2).

### Vehicles

As in "49.34 (code)": the Windows 2000 two-processor guest (stall2's, runner `bl.sh`) and the XP SP3 q35 guest
(c31's, runner `blx.sh`), copy-compare loops of 10 rounds on the UAS disk D: and the stick F:, one hibernation each,
the stick captured by QEMU's `usb-storage` `pcap` in both runs of every trial (`<leg>a.pcap`, `<leg>b.pcap` beside
the guest's disk in `E:\xhci98-vm\p49\bkeep\smp\` or `...\xp\`, with the serial and driver logs). `FHK.BAT` and `FHKX.BAT` each end
with a shutdown of their own after their readings. Installs by Setup: the package's own `INSTALL.BAT /console
/yes` (`PREPI.BAT`), removals by `INSTALL.BAT /console /uninstall /yes` (`PREPU.BAT`), one boot each, runner
`fprep.sh`; the machine-wide key, the USB class key and the Tree's `XHCITR /set /show` printed after the install.

### Legs

Driver Q6 unless said.

| Leg | System | Setting | Variation | At the resume | After it | Result |
|---|---|---|---|---|---|---|
| LT1 | 2000 SMP | 1, `XhciQemuBotDelayMs` 30000 | the reopening delayed 30 s, loops busy at the sleep | kept; medium check 3FFFFh equal; reopened 30 s after the check | F 10/10, D 10/10, 12 devices, 0 codes | kept; the latch was not reached (below) |
| XR3 | XP | 1 | a stick of another serial swapped in during the hibernation | resumed; refused at the restore (serial differs), found again at once | F 5/10, D 10/10, 11 devices, 0 codes | dropped as designed |
| XR4 | XP | 0 | the same | resumed; refused at the decision (why 8), found again | F 7/10, D 10/10, 11 devices, 0 codes | as the base driver |
| XR5 | XP | 1 | the same | as XR3 | F 2/10, D 10/10, 11 devices, 0 codes | as XR3 |
| XR6 | XP | 0 | the same | as XR4 | F 7/10, D 10/10, 11 devices, 0 codes | as XR4 |
| XC1 | XP | 1 | cold start, a stick of a serial the snapshot never saw, nothing typed | desktop at 240 s, 544 driver lines | - | no stop at the logo |
| XC0 | XP | 0 | the same | desktop at 240 s, 529 driver lines | - | no stop at the logo |
| ID1 | 2000 SMP | - | the package installed by Setup | - | - | **VOID**: Setup refused the package ("A driver from another build is in the folder": a qemu-flavour `xhci98.sys` in a release folder) |
| ID2 | 2000 SMP | installed default, none set | `1614563d1`'s Setup over a build without the value, release driver | kept (below) | F 10/10, D 10/10, 12 devices, 0 codes | **PASS**: the default path end to end |
| FI1 | 2000 SMP | written by Setup | `85e61ab08`'s Setup: the package removed, then installed afresh; release driver | kept (below) | F 10/10, D 10/10, 12 devices, 0 codes | **PASS** |
| XFI | XP | written by Setup | `85e61ab08`'s Setup: the package removed, then installed afresh; release driver | - | Setup's exit 3, every device started | the value written 0 (below) |
| M3 | 2000 SMP | 1 | on the merged build (Q7: `3.5.0.0` at `ba292831e`, 49.35's `hcd_ctl.c`) | kept; medium check 3FFFFh equal; restored in place | F 10/10, D 10/10, 12 devices, 0 codes | **PASS** |

**ID2.** Read from the guest before its restart: Setup's exit 3 (restart), every device "started"; the
machine-wide key `Services\USB` empty; no `XhciResumeKeepBot` in the USB class key; the Tree's `/set /show`:
"XhciResumeKeepBot (advanced): not set (the driver uses 1 - after hibernation); default 1 - after hibernation".
After the hibernation the installed `xhci98.sys` was the release build (`fc`), and the stick's capture shows the
restore's descriptor reads, then the bus's TEST UNIT READY (tag 42550001) and READ CAPACITY (42550002), and no Get
Max LUN or INQUIRY of a new start: kept.

**FI1 and XFI: Setup writes the value (M19).**
- 2000. The removal: every xhci98 device, both INFs, the three services and the files removed, exit 3, the
  machine-wide key empty. The install after the restart: "XhciResumeKeepBot set to 1 for all controllers (this
  Windows's default).", exit 3, every device "started"; the machine-wide key `"XhciResumeKeepBot"=dword:00000001`;
  the Tree: "XhciResumeKeepBot (advanced): 1 - after hibernation (machine-wide); default 1 - after hibernation".
  FI1's hibernation then kept the stick as ID2's did.
- XP. The same removal and install: "XhciResumeKeepBot set to 0 for all controllers (this Windows's default).",
  every device "started"; `"XhciResumeKeepBot"=dword:00000000`; the Tree: "XhciResumeKeepBot (advanced): 0 - off
  (machine-wide); default 0 - off".
- On both, Windows' own Found New Hardware wizard for the controller left without a driver held Setup's wait. It
  was cancelled with Esc over QMP (Setup says what to do: answer it, or it goes on after ten minutes). On XP,
  Windows' unsigned-driver prompts ("Software Installation", then "Hardware Installation" for the controller, the
  root hub and the UAS disk) were answered Continue Anyway over QMP.

**XR1 and XR2, re-read.**
- **XR2's power-off was `FHKX.BAT`'s own shutdown, after its seven readings.** Its readings were F 2/10, D 10/10,
  11 devices and 0 problem codes. Its 60 s screenshot shows XP's Welcome screen, which follows every resume of this
  XP vehicle (X1's shows it too). It also shows a "Delayed Write Failed" box for F:, because the stick was removed
  with writes pending.
- **XR1 had no hibernation.** XR1a's QEMU wrote nothing: no serial line and no driver line. The runner's start line
  never came (typed again twice), and the process ended at 11:09:21. The runner read that end as the hibernation, so
  XR1b was a cold start of the snapshot with a stick it had never seen, not a resume. Two cold starts of that kind,
  XC1 (value 1) and XC0 (value 0), reached the desktop, and XR1's stop at the logo was not seen again. It is read as
  a fault of that vehicle run, whose first QEMU never ran Windows, and not of either value.
- **With value 0 and value 1 (XR3 to XR6), XP resumes every time.** There is no fresh boot and no hang. The stick is
  dropped and found again, and its loop fails the rounds that ran across the removal, as the base driver does.

**The latch (M13).** In this vehicle no request of the class driver arrives between the decision and the delayed
reopening:
- In D1 (20 s) and LT1 (30 s), 2000's `usbstor` sent its first command 0.4 s after the reopening (LT1's stick
  capture), although the controller's D0 waits at most 15 s (`HCD_RKEEP_D0_WAIT_MS`).
- Something above the bus holds the stack until the restore ends. That was not read further.
- The latch's paths stay covered by the host vectors (`test_enum`, `test_bot`) and not by a leg.

Every QEMU of these legs was started and stopped by its runner, or stopped by PID (ID1's), and logged in
`E:\xhci98-vm\p49\bkeep\pids.txt`.

## 49.24 `XhciInterruptMode` 4: the code and its legs (workers m4c, m4x and m4n, 2026-11-11 to 13)

Mode 4 ships as a hidden beta for testers (owner, 2026-11-12; record 15 section 4.5.18). Every reading below
is from QEMU guests on development host A. Each subsection names the xHCI98 build it ran (commit and the
`xhci98.sys` SHA-256); the MSI stability improvements are cited by version or pre-release state and file SHA-256.
`build-and-test.md`, "Mode 4 for testers", summarises where each Windows stands.

### 49.24.1 The driver code and its first legs (worker m4c)

Branch `w49-m4c` from `3.5.0.0` at `e2b83953c`. Interfaces:
the MSI stability improvements' header vendored in four pre-release states (the xHCI98 commits below say which).
The leg folders are under `E:\xhci98-vm\p49\m4c\`, with
every guest start and stop in `pids.txt`. Development host A restarted at
02:02 on 2026-11-12 (Windows Update). The one guest running then (me1 e,
already hung) is logged as lost, and no result below depends on anything
running at that time.

#### Commits (each reviewed until nothing material was left)

| Commit | What |
|---|---|
| 23cf1ca79 | record 15 section 4.5.16 (revision 12), pointers; record 18 names |
| fe2bd21a3 | pure core: `src/xhci_msg4.c/.h`, poll reasons 21-34, walks SHARING/MATCH, `test/test_msg4.c` |
| cc4a8c0db | the 98 SE and ME client header vendored |
| 365d90180 | the MSI stability improvements' header vendored (pre-release state 1) |
| fe88a0391 | quiet step, the common lifetime, the 98 SE/ME seam (`hcd_msg4.c`, `hcd_msg9x.c`), counters 264 to 302 words, XHCITR |
| ed02796eb | INF rows, INF gate rows, `tuning.md` section, `build-and-test.md` |
| 212832f13 | the MSI stability improvements' header from pre-release state 2 (MSI-X/APIC 32-bit accessor) |
| 0bc0e4c47 | the NT seam (`hcd_msgnt.c`), the NT branches, the removal bar, the backstop, the import-gate guard |
| c848c2114 | the header from pre-release state 3 (the published state); records 15 and 18 |
| dc6526b51 | merge of `3.5.0.0` (faeeaba2b, the build fix) |
| eb6c319fd | the header from pre-release state 4 (no `_allmul`) |
| b38a54473 | 98 SE/ME: the shutdown step moved to the D3 dispatch; no configuration IRP in it |

#### Host tests

The full `test\run-host-tests.cmd` printed "Host tests PASSED" at
fe88a0391, ed02796eb, 0bc0e4c47 and dc6526b51 (`hostall.log`). New or
grown suites: test_msg4 248, test_poll 562, test_tol 3469, test_snap 540,
test_trlog 387 checks. The INF gate passes on both INFs, and its
self-tests pass all 6 parts (608 checks in part 2).

#### Builds

- Gated x86 `all` of ed02796eb: PASS (`gated-x86-c7.log`). Release and qemu
  x86 packages RC 0 (`pkg.log`).
- Gated x86 `all` of dc6526b51: FAILED, import gate:
  `ntoskrnl.exe!_allmul`, from the vendored header.
  Fixed in the library (pre-release state 4) and re-vendored
  in eb6c319fd.
- Gated amd64 `all` of dc6526b51: PASS (`gated-amd64-c10.log`).
  Release and qemu amd64 packages RC 0 (`pkg64.log`).
- Gated x86 `all` of eb6c319fd: PASS (`gated-x86-c11.log`), with no
  `_allmul`. Not used for any leg: the shutdown fix was in the worktree
  for a few minutes while it ran.
- Gated x86 `all` of b38a54473: PASS (`gated-x86-c12.log`). Gated amd64
  `all` of b38a54473: PASS (`gated-amd64-c12.log`). Release and qemu
  packages for both architectures RC 0 (`pkg2.log`, `pkg64b.log`).
  Release xhci98.sys sha256: x86 `457e2ba7...`, amd64 `032343b6...`. Kits:
  `xfer98\KITR` (98/ME) and `xfernt\KITR`, `KITR64` (made from those
  packages, with a byte compare against the build output).

#### Legs, 98 SE and ME

The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13).

One first boot after an install on 98 SE (leg se4, tag b) stopped at a fatal exception 0E at `016F:BFF72170`, not
seen again; it looks like the ring-3 KERNEL32 faults that task 49.31 found on the bare q35 platform.

#### Legs, 2000, XP, XP x64

| Leg | System, configuration | Result |
|---|---|---|
| xp1 a | XP SP3 x86 (q35 base, 2 vCPU), release package of b38a54473, install by `INSTALL.BAT /console /yes` (two unsigned-driver prompts and the root hub's wizard answered), then mode 4, restart | started: XhciMsiStrategy 0, **claimed IRQ 3** ("1 on it; the claim's status 0 (OK)"; IRQ 4/6/7 "used by a device", 5/10/11 "a firmware override"), delivery MSI-X, vector 1B2h, routines in the driver; its registry state read with `reg query`: the key `...\F7622368\1_0_0` with its `Snapshot` (REG_BINARY, 200 bytes); stick (D:): 8 MB copy + `fc /b`, no differences; standby (`SetSuspendState`): never entered; copy + compare again, no differences; unplug, dump (`serial-a.txt`) |
| xp1 a (cont.) | mode 0 set, restart | the restart from mode 4 completed (Windows came back); mode 0: "automatic: the line interrupt is there"; `reg query` of the state key: not found (RQX rc=1), so nothing is published outside mode 4; ACPI power-off in 9 s |
| xp64 a/b | XP x64 SP2 (2 vCPU), release amd64 package of b38a54473 | the console install refused the unsigned 64-bit package ("carries the Thunderbolt driver without its catalog, xhcicm.cat"); with `cm\` left out of the test kit the installer then waited on a Found New Hardware window, so the controller was installed through that wizard from the kit's folder. Mode 4, restart: **claimed IRQ 3**, MSI-X, vector 161h, its registry state `...\FFFFFADFC7C85D50\1_0_0` present; stick (D:): copy + `fc /b`, no differences; unplug, dump; ACPI power-off in 6 s |
| w2k1 | 2000 SP4, the ACPI base (`halacpi.dll`, non-APIC: the MSI stability improvements refuse it) | **not completed**: the first runs typed Dvorak into a QWERTY base; the qemu-flavour install found no kit (the kit was made after the drive was mounted); the second boot's logs were overwritten by the relaunch. No mode-4 result on 2000 |

Not run: brake/stranded injections (`XhciQemuMsg4Fail`), strategy 1,
query-stop/cancel, the quarantine path, the upper pool, mode 1/3
regression on NT, 2000 with an APIC HAL, standby/resume with mode 4 on any
system (standby was never entered on these guests).

### 49.24.2 XP SP3 x86, XP x64 and 7 x86 (worker m4x; interim)

XP x86 and XP x64 legs of `XhciInterruptMode` 4 (the MSI stability improvements, vendored) that worker m4c had no room for, the
no-regression legs with the value not 4 (XP x86, Windows 7 x86), and the evidence the owner's decision of
2026-11-12 asks for: the installer offers mode 4 per Windows only where that Windows' legs passed. Windows 2000
is worker m4n's (`run49-m4n.md`).

Build: the merged tip of `3.5.0.0` at 7e82e5a55 (the merge of `w49-m4c` at b38a54473), gated x86 and amd64
`all` (m4c, `E:\xhci98-scratch\w49\m4c\gated-l-*.log`, PASS). The four staged packages of worktree
`E:\xhci98-wt\w49-m4l\out` were copied, never written in place, to `E:\xhci98-scratch\w49\m4x\pkg\`; every
file's sha256 equals the source's (`pkg\src.sha`, `pkg\dst.sha`). `xhci98.sys` sha256: release x86
`756941a2...`, qemu x86 `4aab0b9c...`, release amd64 `9223a9a2...`, qemu amd64 `8e1c87ed...` (each the gated
record's); `xhcihlpr.sys` release `e83c26db...`. The driver reports its version as 3.4.3.0 (the tree's version
stamp at that tip). Family kits by `ConvertTo-FamilyLayout` (the m4l worktree's `package-common.ps1`) into
`E:\xhci98-vm\p49\m4x\xfernt\KITR` (release x86) and `KITQ` (qemu x86); x64 kits below.

Guests: q35 bases of `E:\xhci98-vm\q35-bases` (`xp` XP SP3 x86 `halmacpi` 2 vCPU 1 GB; `w7x86` 7 SP1 x86 2 vCPU
2 GB; `xpx64` XP x64 SP2 2 vCPU 2 GB) as overlays under `E:\xhci98-vm\p49\m4x\<leg>`, one at a time, QMP port
48720, every start and stop in `E:\xhci98-vm\p49\m4x\pids.txt`. Tools copied from m4c's
(`E:\xhci98-vm\p49\m4x\tools`) with its port guard kept (every QMP command through `lib.sh`'s `qg`: the QEMU on
the port must name itself as a guest of my folder and its pid file's process must carry my folder and port on
its command line). Copy+compare: an 8 MB file copied to a hot-plugged `usb-storage` stick on the controller and
`fc /b`'d ("FC: no differences encountered"). Dumps: `XHCITR /dump /nosysinfo` typed to COM1.

#### First starts after an install

(table: `E:\xhci98-scratch\w49\m4x\firststarts.md`, copied at the end)

#### XP SP3 x86, release (leg `xr`, tag a): the values not 4, then mode 4

| Step | Result | Evidence |
|---|---|---|
| install | `INSTALL.BAT /console /yes /nopause` with the base's xHCI wizard cancelled first (Esc); prompts answered by keys: two "Software/Hardware Installation" Continue Anyway, the root hub's Found New Hardware wizard (No, not this time / Install automatically), its Continue Anyway, Finish. Exit code 3 ("restart"); controller and root hub "started" | `xr\serial-a.log`, `a-k*.png` |
| first start, mode 0 | clean (first start 1); dump: "automatic: the line interrupt is there", INTA, IRQ 16; 2 x copy+compare: no differences | `a-reboot.png`, dumps r0/r0b/r0c |
| mode 1, restart | "forced line (XhciInterruptMode 1)", the line; 2 x copy+compare: no differences | dump r1 |
| hibernation (mode 1) | `powercfg /hibernate on`: "System does not support hibernation" on this q35 board as launched (S4 not offered); `SetSuspendState` then never suspended (QEMU never reported suspended in 300 s) | `a-hb.png`, `a-nohib.png` |
| mode 2, restart | "forced polling (XhciInterruptMode 2)", POLLING; 2 x copy+compare: no differences | dump r2 |
| mode 4, restart | started: claim strategy 0, **IRQ 3** (4, 6, 7 "used by a device", 5, 10, 11 "a firmware override"), status 0 (OK), MSI-X, vector 1B2h, routines in the driver, the No Op by interrupt seen (`msg4.noop` 01010000); its registry state `...\F7622368\1_0_0` with its `Snapshot`; brake counters 361 entries, 2 stray; 2 x copy+compare: no differences | dump r4, RQX |
| mode 4, devcon disable + enable | disabled, enabled, "Driver is running"; dump r4e: the claim taken again (IRQ 3, MSI-X, 1B2h); copy+compare: no differences | dump r4e |
| mode 4, restart | Windows came back; IRQ 3, MSI-X, 1B2h, stranded 0; copy+compare: no differences | dump r4b |
| shutdown under mode 4 (mode 0 set, not yet restarted) | ACPI power-off in 9 s | `leg-a.txt` 14:19:48 |

#### Windows 7 SP1 x86, release (leg `wr`, tags a and b): modes 0 and 3, and 4 refused

Launched with `-global ICH9-LPC.disable_s3=0 -global ICH9-LPC.disable_s4=0`. Every batch elevated (Start search,
Ctrl+Shift+Enter, UAC Yes).

| Step | Result | Evidence |
|---|---|---|
| install | elevated `INSTALL.BAT /console /yes /nopause`, "Install this driver software anyway" answered; controller "problem code 28" until the restart, exit code 3 | `wr\serial-a.log`, `a-k4.png` |
| first start, mode 0 | clean (first start 2): "xHCI98 USB 3 Root Hub ... installed successfully", "Driver is running"; dump: "automatic: a message interrupt was granted", MSI-X, routines "the helper xhcihlpr.sys", helper BOUND; 3 x copy+compare: no differences | `a-reboot.png`, dump w0, w0s |
| standby | not available on this guest: `powercfg -a`: "Standby (S3): The VGAPNP.SYS display driver does not support standby" (S1, S2 also "firmware does not support"); `SetSuspendState` never suspended (twice) | `serial-a.log`, `a-nosleep.png` |
| hibernation, mode 0 | `powercfg -h on`; hibernated (QEMU ended after 9 s); cold start resumed to the locked session ("test, Locked"). The leg script then typed its next command into the password box (a failed logon in the guest; no password was entered by intent, none is known). The resume's copy was not taken: the guest was stopped by QMP quit at the logon screen (it ignored the ACPI power button there) and cold-started as tag b | `a-bootr.png`, `a-k6.png`, `a-offhang.png`, pids.txt |
| cold start (tag b), mode 0 | "Driver is running"; copy+compare: no differences | dump w0b |
| mode 3, restart | "forced message (XhciInterruptMode 3): a message interrupt", MSI-X; 2 x copy+compare: no differences | dump w3 |
| mode 4, restart | the controller does not start: devcon "The device has the following problem: 10" (record 15: mode 4 on Vista/7 is refused); XHCITR finds no controller to open (empty dump) | `serial-b.log`, `b-xq.png` |
| mode 0, restart | "Driver is running", MSI-X; copy+compare: no differences; `shutdown`: ACPI power-off in 9 s | dump w5 |

#### XP SP3 x86, qemu flavour (leg `xq`, tag b): the mode-4 safety paths

The installer of this tree takes only a release folder ("xhci98.sys has 1 build marker(s), the first QEMU;
this folder is RELEASE ... A driver from another build is in the folder"), so the release kit was installed
(prompts answered as on `xr`) and the qemu `xhci98.sys` (sha256 `4aab0b9c...`) copied over
`system32\drivers\xhci98.sys` before the first restart; the qemu trace on 0xE9 (`debugcon-b.log`) shows that
build running. Launched with `-Tablet` and the S3/S4 globals. The values are set in the machine-wide key.

| Step | Result | Evidence |
|---|---|---|
| first start (mode 0) | clean (first start 3); the line, "Driver is running" | `b-reboot.png`, dump q0 |
| mode 4, restart | IRQ 3, MSI-X, vector 1B2h, routines in the driver; state key; 3 x copy+compare under 40 tablet moves: no differences | dumps q4, q4b |
| disable + enable, twice (2e) | each: "Driver is running", the claim taken again; copy+compare: no differences | dumps e1, e2 |
| remove + rescan (2e) | the removal ran; the rescan made a new devnode that XP left "not configured" (devcon "problem: 01", "Universal Serial Bus (USB) Controller") until a Found New Hardware wizard was answered: XP's client-side install of an unsigned driver for a new devnode, not a driver refusal (the same INF path the install took). Repaired by running the installer again (release file back), the qemu file copied again, restart: IRQ 3 claimed again (so the removal had released it), MSI-X, 2 x copy+compare: no differences | `b-k6.png`..`b-k13.png`, dump e6 |
| lost interrupts, arg 3 (2d) | written live during 12 copies and 40 tablet moves: trace "qemu inject: fired=01010003", "interrupts lost so far" 1..3; log `msg4.stranded` 1, 2, 3; `Msg4Stranded` 3, `BackstopDrains` 3, no trip, "Driver is running"; 12 x copy+compare: no differences | dump j3, `debugcon-b.log` |
| every interrupt lost, arg FF (2d) | written live (seq 03): "fired=030100FF", then 8 lost interrupts in the tablet's traffic (trace "interrupts lost so far" 4..0B); then the controller's IRP_MN_QUERY_PNP_DEVICE_STATE (trace "controller PnP minor=00000014") and "stop controller": Windows was told it failed. **Device Manager: Code 43** (devcon "problem: 43"); the root hub and the stick went with it. XHCITR found no controller to open while it stood (empty dump). CLEAR (JCB) and the value deleted, then devcon disable + enable: "Driver is running", IRQ 3 again, stranded 0; copy+compare: no differences (no restart) | dump jfr, `debugcon-b.log` lines 2200-2222 |
| XhciQemuMsg4Fail 0x1 (2a, the release's disconnect fails; refusal table) | value set, a disable + enable so a lifetime reads it (log `qemu.inj.msg4`=1, "Driver is running"); devcon disable: the release cannot prove the message off (its configuration writes fail), so the connection is quarantined; devcon enable: **Code 10** (devcon "problem: 10"); XhciInterruptMode 0 set, disable + enable: **still Code 10** (refused under any value); the injection deleted, mode 4, restart: "Driver is running", IRQ 3 claimed, copy+compare: no differences - the restart cleared it. What the user sees: Device Manager's Code 10 ("This device cannot start") on the controller, every USB device gone, until Windows restarts (tuning.md says so) | `serial-b.log`, dumps f1a, f1d |
| XhciMsiIsaIrq 3 (2h, forced onto the free IRQ) | disable + enable: "Driver is running", `XhciMsiIsaIrq 3`, claimed IRQ 3 (OK); copy+compare: no differences | dump iq3 |
| XhciMsiIsaIrq 4 (2h, forced onto COM1's IRQ) | disable + enable: **Code 10** (the controller does not start) | `serial-b.log` |
| XhciMsiAvoid 2 (2i) | disable + enable: **Code 10**; cleared: "Driver is running" | `serial-b.log` |
| the helper disabled (2j: xhcihlpr Start 4, restart) | mode 4: **Code 10**; mode 0 the same boot, disable + enable: "Driver is running", "automatic: the line interrupt is there", helper "not looked for"; restored (Start 0), mode 4, restart | dumps hl0, hl5 |

On every refused start the reason (24, 26, 30 or 33) could not be read: XHCITR opens only a started controller,
the qemu trace carries no `msg4.*` note, and the log ring is not reachable without a debugger
(`XhciLogDebugView`). The refusal is read as the Device Manager code only; the cause is inferred from the one
value changed.
| XhciMsiStrategy 1 (3a, the controller's own line borrowed) | disable + enable: **Code 10**; the stick's copy failed (no controller); cleared, disable + enable: "Driver is running". The controller's line here is IRQ 16 (INTA through the root port), which other q35 functions share; the refusal is read as the veto on a shared line (inferred, reason not readable). Strategy 1 on a private line did not run | `serial-b.log` |
| hibernation under mode 4 (2f) | with `-vga std` XP said "System does not support hibernation" (the standard VGA driver, as msint4 found); cold-started as tag f with `-vga none` (blind, batch-driven; the resume password turned off first): `powercfg /hibernate on` accepted; `SetSuspendState`: QEMU ended in 9 s (S4); cold start: Windows resumed; log `msg4.rearm`=0 (the re-arm OK), "Driver is running", IRQ 3, MSI-X, stranded 0; stick plugged after the resume, 2 x copy+compare: no differences | `xq\serial-f-pre181432.log`, `serial-f.log` dump h1 |
| XhciQemuMsg4Fail 0x2 (2b, the re-arm after a resume fails) | value set, disable + enable (log `qemu.inj.msg4`=2); hibernation: QEMU ended in 9 s; cold start, Windows resumed: **Code 43** on the controller (devcon "problem: 43"), its devices gone; disable + enable with the value deleted: "Driver is running", IRQ 3; copy+compare: no differences (no restart) | `serial-f-pre182636.log`, `serial-f.log` |
| query-stop / cancel-stop (0x4, 0x8; 2c) | **not run**: no route to a PnP-sent IRP_MN_QUERY_STOP_DEVICE was found in this guest (devcon restart and disable send removals; XP has no native PCIe hot-plug for a rebalance) | - |
| an actual unplug of the controller (2a's absence branch) | **not run** (time; ACPI hot-plug of the root port not tried) | - |
| f1 through remove + rescan | **not run** (the XP client-side install on a new devnode needs the wizard answered, which costs a reinstall each time) | - |

#### The tip changed: items 3 and 4 on 5a03b9b8a

At 18:50 the coordinator asked for items 3 and 4 on `3.5.0.0` = 5a03b9b8a (49.35's recovery pacing; mode 4's
NT path of `hcd_msg4.c` unchanged in behaviour). Worktree `E:\xhci98-wt\w49-m4x`
(branch `w49-m4x` at 5a03b9b8a, no commit of mine), `E:\xhci98-scratch\w49\m4x\chain.cmd`: gated x86 `all` PASS
(`gated-x86.log`), gated amd64 `all` (`gated-amd64.log`), release and qemu packages, tools. `xhci98.sys` sha256:
release x86 `b87ace91...`, qemu x86 `bbac54bf...`; `xhcihlpr.sys` `9563c951...` (the gated records'). Items 1 and 2
above ran on 7e82e5a55.

#### XP SP3 x86, release 5a03b9b8a: claim variants (leg `xq`, tags c and later)

| Step | Result | Evidence |
|---|---|---|
| update install over 7e82e5a55, a second qemu-xhci (xh2, root port 00:1d.0) present, mode 4 set | installer: "xhci98.sys: same version, other bytes: replaced", exit code 2 (the second controller "problem code 10"); restart: first start 4 clean; dump: build flavour release | `xq\serial-c.log` |
| two controllers, one IRQ eligible, XhciMsiShare 0 (3b) | controller 1: "Driver is running", IRQ 3 (OK), MSI-X; controller 2: **Code 10** - no IRQ left (reason 33 as designed; the reason itself not readable) | dumps c0, c1 |

### 49.24.3 Windows 2000, 98 SE and ME (worker m4n)

Worker m4n, 2026-11-12, from `3.5.0.0` at `7e82e5a55` (the merge of `w49-m4c` at `b38a54473`). Worktree
`E:\xhci98-wt\w49-m4n`, branch `w49-m4n`. Guests one at a time, at most 2 vCPUs, QMP 48700-48719; every start and
stop in `E:\xhci98-vm\p49\m4n\pids.txt`. The leg tools (`E:\xhci98-vm\p49\m4n\tools`) refuse a port that is already
listening, and every QMP command first proves that the QEMU on the port is this worker's (its pid file, its
command line naming the guest folder and the port). Development host A ran at about 87% CPU during the legs: 2000
boots took 4 to 9 minutes under TCG.

#### Build

Gated x86 `all` of `7e82e5a55` (no change): `BUILD + GATES PASSED (release qemu)`
(`E:\xhci98-scratch\w49\m4n\chain.log`). `xhci98.sys` x86 SHA-256: release `c6ee2972...`, qemu `ad1c0dcc...`
(`kits\hashes.txt`). Kits: the family layout of those packages with this tree's installer and tools
(`E:\xhci98-scratch\w49\m4n\mkkit.cmd`), copied to the transfer folders `xfer2k` and `xfer98`.

#### Windows 2000 (group 1)

Vehicle: a standalone copy of xhci98's `vm\win2k-smp.img` (2000 SP4; `HKLM\HARDWARE\RESOURCEMAP\Hardware
Abstraction Layer` reads "ACPI 1.0 - APIC platform MP", `halmacpi`), QEMU `pc` machine (i440FX), `-cpu pentium3`,
one vCPU, TCG, the emulator snapshot `asm4242-20261110-docks`, `qemu-xhci` on `pci.0` (MSI-X), the stick a
`usb-storage` on it (D:), the transfer folder as a FAT disk (E:). The image held an older `xhci98.sys`
(2026-09-06), so the install is an update. `-vga std` brings a Found New Hardware wizard at every start (no 2000
driver), `-vga cirrus` does not.

| Leg | What | Result | Evidence (`E:\xhci98-vm\p49\m4n\w1\`) |
|---|---|---|---|
| 1a | install (`INSTALL.BAT /console /yes /nopause`), restart: the first start after the install, mode 0 | exit 3; both devices started; first start clean; delivery the line interrupt; copy 8 MB + `fc /b` three times, no differences; restart | `serial-a.log`, `a-r1.png` |
| 1b | mode 4 (`XhciInterruptMode` 4), restart | started: claimed IRQ 3 (IRQ 4, 6, 7 used by a device; 5, 10, 11 a firmware override), delivery MSI-X, vector 51h, `msg4.noop=01010000`, routines in the driver; its registry state `...\F88D1368\0_6_0` published; copy + `fc /b` four times, no differences; restart under mode 4: the same claim and key; ACPI power-off in 3 s | `serial-a.log` (dumps b0, b1) |
| 1c | SetupDi disable and enable of the controller, twice (`dc.exe`, worker leg's small devcon; WDK 7.1's devcon is not a valid Win32 application on 2000) | each enable: connected again, IRQ 3, vector 51h; copy compares | `serial-d.log` (d1) |
| 1c' | SetupDi remove, then a rescan | the controller comes back at problem 1 (not configured, no driver) - **the same in mode 0** (control, `serial-f.log`): 2000 wants the reinstall; after `INSTALL.BAT` again: started in mode 4, IRQ 3, 51h, copy compares | `serial-d.log` (d2, d3) |
| 1d | standby and hibernation | **not available on this vehicle**: the Cirrus driver vetoes standby (dialog, `d-nosleep.png`); with `-vga std` and the S3 SeaBIOS, `SetSuspendState` returns 0 at once for standby and for hibernation (after `CallNtPowerInformation(SystemReserveHiberFile)` succeeded), in mode 4 and in mode 0 alike (control), and no power IRP reached the driver. The re-arm after a resume is not read on 2000 | `serial-f.log`, `f-*.png` |
| 1e | qemu flavour (`xhci98.sys` `ad1c0dcc...` copied over the release one), `XhciQemuInject` 01 arg 3, tablet moved and a copy loop on D: | 3 interrupts lost, `BackstopDrains` 2, `Msg4Stranded` 2, the connection kept (status 0) | `serial-f.log` (g2) |
| 1e' | the same, arg FF, tablet only (no stick I/O) | after 8 stranded events: `msg4.lost`, `IRP_MN_QUERY_PNP_DEVICE_STATE` answered failed, `IRP_MN_SURPRISE_REMOVAL`, the slots invalidated, the teardown, `IRP_MN_REMOVE_DEVICE`; the controller then reads "stopped" (no problem code) and a SetupDi disable and enable does not start it; a Windows restart does (mode 4, IRQ 3, 51h) | `debugcon-h.log`, `serial-h.log` |
| 1e'' | the same, arg FF, with the copy loop running on D: | **DEFECT, 3 of 3**: see below | `debugcon-f-hang.log`, `h-hang.mem` (not kept), `debugcon-i.log` |

#### 1e'': the loss under bulk I/O on 2000 (the qemu flavour's injection)

Read: `XhciQemuInject` 01 arg FF while `CL.BAT` copies and compares 8 MB on D: in a loop (and, in the first run, the
tablet moved). Four runs (f, h, i, j), every one the same: the copy loop stops after its next pass, the shell
(Explorer, then Winlogon: Ctrl+Alt+Del draws nothing) stops answering, the guest's processor idles. The driver's
note ring, read from the guest's memory as data (QMP `pmemsave`, the ASCII notes of this driver's own log ring),
ends with `qemu.inj.irq.lost` up to 0Fh, `msg4.stranded=00000001` to `00000008` and `msg4.lost=00000002`, and
nothing after: no `msg4.lost.counts`, no `msg4.pnpstate`, so Windows was never told. Two uncommitted qemu-flavour
builds with port-E9 markers (`E:\xhci98-scratch\w49\m4n\diag12.patch`; `xhci98.sys` `415971b3...`, `0dc25a68...`)
show the controller thread completing three more passes after the last lost interrupt (with `Msg4Lost` still
read 0, i.e. before the latch) and then never starting the next one: it stops between the end of
`HcdEnumService` and the injection poll at the head of the next pass - the window that holds the periodic flush,
the work-event wait and `HcdInjPoll`, whose trigger read goes through `IoOpenDeviceRegistryKey` on the
controller's driver key once a second. With the tablet alone (h) the same injection ends cleanly.

**The control run, and the verdict: the harness, not the release path.** Run k: the same guest, the same copy loop,
the same injection (`070100FF`), the trigger written to the machine-wide key
`HKLM\SYSTEM\CurrentControlSet\Services\USB` instead (`INJM.BAT`), so `hcdReadDword` never reaches
`IoOpenDeviceRegistryKey`. The loss then ends cleanly (`debugcon-k.log`): eight stranded events, the thread's lost
step, `IRP_MN_QUERY_PNP_DEVICE_STATE` answered failed, "slots invalidated, dropping every device", the one
transfer in flight cancelled, `IRP_MN_SURPRISE_REMOVAL`, the teardown with the quiesce's halt, `IRP_MN_REMOVE_DEVICE`;
Windows shows "Delayed Write Failed" for the stick's volume (`k-diag.png`), the copy loop goes on reporting the
drive gone, and the shell answers. So the wedge was the qemu flavour's own trigger read: `HcdInjPoll` opened the
controller's driver key once a second from the controller thread, and that open waited behind the PnP manager
while the stick's I/O stood, before the latch, so the lost step never ran. Nothing in the release flavour sits in
that window (the trigger read is qemu-only; every other registry read is at the start or at `AddDevice`; the
flush and the wait are bounded; the lost step's `IoInvalidateDeviceState` does not wait). Fixed in the harness:
`18d298553` (the periodic read on 2000 to 10 from the machine-wide key alone; `build-and-test.md` says so),
reviewed (one round, nothing material). The diagnostic markers were never committed.

#### 98 SE and ME

The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13).

**A fatal exception 0E on ME, not this project's.** Guest mb1, a fresh overlay of the q35 ME base with the `edu`
whose install batch never ran (the run box was lost), so it had nothing of this project: "0E : 016F :
BFF62170" at its second cold start (`mb1\a2-0e.png`; the registers, read as data, in `mb1\a2-0e-regs.txt`: ring
0 at C00093B0, the VMM's error display). Nothing of this project was loaded: the ME platform's fault (task
49.10's), not mode 4's.

**First starts after an install** in this section: 2000 (w1, mode 0), 98 SE (se1, se2, se3 in mode 0; br1), ME (mb2,
mode 0): six, none faulted. The fatal exception 0E at `016F:BFF72170`
(49.24.1 se4 b) was not seen.

Not run, paused by the owner's priority (2026-11-12): two controllers.

## 49.37 - the connection manager's state snapshot: CM_QS_IOCTL, BASE.CT<n> and BASE.CB<n> (2026-11-12, worker cmsnap)

Branch `w49-cmsnap` from `3.5.0.0` at `47ac9424c`. Commits: `a4d71d80b` (the change: driver, tool,
generator, host suite, design record 21 section 8.4, record 25 section 23), `c49846278` (the driver
version through an array: MSVC 6.0 does not spread `XHCI_VER_CSV` into a macro's four parameters - the
first gated build's C2059), `5507e8d23` (record 25 section 23: the measured report growth),
`194699058` (the owner's Q2: Save Snapshot names the images), `c6bdc2470` (merge of `3.5.0.0` at
`ba292831e`, task 49.35's all-controllers snapshot, with 49.37 wired into it), `39b2339d6` (merge of
`3.5.0.0` at `2f05c3167`), `ca15f82ed` (Save Snapshot at a drive's root), `0d3bfabbd` (the image check: records apart
from the metadata, a hash field one word), `829be39f9` (merge of `3.5.0.0` at `b354dc1fd`). The owner's decision of
2026-11-13: the snapshot covers `xhcicm.sys` as it covers `xhci98.sys`; his answers of the same day:
Save Report carries the whole `.CT<n>` (Q1), Save Snapshot's message names the images (Q2), the names
`.CT<n>` and `.CB<n>` stand (Q3).

**What was missing.** Save Report carried each NHI's counters, its 16 KiB log ring, its PCI view, its
status record and its networking answer; Save Snapshot carried of `xhcicm.sys` only the `.DEV`'s
networking section. Nothing served the connection manager's current state. 49.11, 49.12 and 49.30 read
it from the `qemu` flavour's debug console; on the `release` flavour the ring wraps and the counters
are totals (49.30 f4f: "removed and found" inferred from the `qemu` twin, not logged).

**What was built.** A sixth query device request, `CM_QS_IOCTL` (`0x00222918`), answering a
self-describing, pointer-free image `XCMS` version 1 (a 64-byte header with a tear detector before and
after the gather, a table directory, a field directory with names, records gathered field by field):
`nhi`, `cm.ctr`, `u4`, `u4.port`, `u4.link`, `u4.kid`, `u4.oblig`, `u4.xd`, `u4.ctr`, `icm`,
`icm.dev`, `icm.ctr`, `ring`, `rirq`, `reenum`, `reenum.port`, `chain.bind`, `net`. Read at
PASSIVE_LEVEL in the IOCTL with no lock held under the query device's `SnapUse` hold, which
`CmQueryClose` drains; windows of up to 60 KiB; served at every level. The field directories are
generated from the structures (`scripts\gen-cm-snap.py`; a member no rule places stops it; the gated
build's `cm-snap-fields` step runs it with `-Check`); no pointer, kernel object, address (BARs, ECAM,
the 9x diagnostics' windows), devnode handle, string, property block, held packet or the ICM's key is
listed; identities only as a 16-bit hash salted anew at every start. `XHCITR /snapshot` writes
`BASE.CT<n>` (the decode, `name=value`, nonzero only, 79 columns) and `BASE.CB<n>` (the image)
exclusively. In the all-controllers run (49.35) every controller's child runs with `/nocm` and one more
child, `/snapshot BASE /cmonly`, takes the state once in the destination folder; BASE is chosen over
the whole set, the `.CT<n>`/`.CB<n>` names included; the message ends with "The Thunderbolt / USB4
state is in <name>.CT0 and .CB0." (or why none was kept) and the exit joins the run's. `/c N` takes the
images inside its own run. Save Report carries each `.CT<n>` once, after the first controller's dump.
`/nosysinfo` withholds the hashes.

**Reviews.** Design, code, each follow-up and both merges reviewed and revised until nothing material
was left before each commit; the findings that changed the work: a physical ECAM base, a held XDomain
packet (both domains' UUIDs) and the 9x bridge diagnostics' memory windows listed by the first
generator (now excluded by rule), the hash argument overclaimed (now worded as a pseudonym), the
detector taken after part of the gather, a `.CB` deleted without ownership on a clash, exit 3 lost
with no dump, in the merge the connection manager's read on quick save's worker thread (single-threaded
CRT) and a process-wide directory change (now a child run), the name check without the `.CT`/`.CB`
names, and the child's failures not reaching the exit.

**Builds** (`XHCI98_SELFTEST_SKIP_OK=1`). `a4d71d80b`: x86 FAILED (`cm_snapos.c(188)` C2059, with
C4003); `gated-1.log`. `c49846278`: x86 `all` and amd64 `release` PASSED (`gated-2.log`; host suite
`test_cm_snap` 74 checks, "cm snapshot: all 356 fields agree"). `39b2339d6`: x86 `all` `BUILD + GATES
PASSED (release qemu)` and amd64 `all` `BUILD + GATES PASSED (release)`, every tool self-test run, none
absent (`gated-3.log`). The tip `ca15f82ed`: x86 `all` `BUILD + GATES PASSED (release qemu)` and
amd64 `all` `BUILD + GATES PASSED (release)`, no step passed over (`gated-4.log`); SHA-256
`xhcicm.sys` x86 release `19ef054a...`, amd64 release `2b48d7d4...`, `XHCITR.EXE` `e058b5ca...`,
`XHCITR64.EXE` `9b6e43fa...`. The final tip `829be39f9`: `build-driver.cmd all` and
`all -amd64` both `BUILD + GATES PASSED (release qemu)`, no step passed over (`gated-5.log`); the
image check is host-tested only, the guest legs ran on builds before it. The legs below ran the release `xhcicm.sys` of `c49846278`
(`53bbfe0f...`; the driver is unchanged since) with the XHCITR named per leg.

**Vehicles.** 98 SE: `bases\base98.qcow2` (a copy of worker tbs's: the q35 98 SE base on QEMU's `pc`,
`3.4.3.0` installed, the ORICO model `asm2464` behind the SHPC `usb4-tunnel-bridge`, `usb4-generic`, one
vCPU, 512 MB, TCG), `tools\go98.ps1`, QMP 48860. XP and 2000: `bases\basexp.qcow2` and
`basew2k.qcow2` (copies of worker shpc's `xpb` and `w2kb`: `pc`, the package installed, `usb4-generic
host-model=intel-adl`, the ORICO model at power-on), `tools\gopc.ps1` / `gopc2.ps1` (a second
`qemu-xhci` at 00:07.0), QMP 48861, one vCPU. Each leg a fresh overlay with the `xhcicm.sys` under
test copied over `system32\drivers` (`fc /b`: no differences) and a cold start; XHCITR run through a
helper, `SNAPW.EXE` (scratch `snapw\snapw.c`: starts `XHCITR /snapshot NAME` with its output in a file,
waits, copies the output, the `.CT0` and the `.DEV`'s connection manager lines to COM1; `SNAPW WINDOW`
ends the installed tray through the installer's quit event and opens this XHCITR's window). One guest
at a time; every start and stop in `E:\xhci98-vm\p49\cmsnap\pids.txt`.

| Leg | System | Step | Result |
|---|---|---|---|
| v98 | 98 SE | `3.4.3.0`'s `xhcicm.sys`, the new XHCITR | **PASS**: "XHCICM0: not served by this xhcicm.sys", no `.CT0`, the controller's set written |
| r98 S1 | 98 SE | idle, the drive mounted | **PASS**: whole (42,964 bytes, 18 tables, about 1 s); `u4.port` #0 `State=5`, `Pcie.Up=1`, `Dev.Uuid=#be0a`; "Attach: S1.DEV S1.TXT S1.CT0" |
| c98b S2 | 98 SE | during a 24 x 4 MiB copy to the drive | **PASS**: the copy running at the snapshot's start and end; whole |
| c98b S5 | 98 SE | after the copies | **PASS**: whole; ring counters moved on |
| r98 S3 | 98 SE | 40 s after the ORICO model's `device_del` | **PASS**: `Pcie.Up` gone; `u4.ctr` `PcieTornDown=1 Unplugs=1` |
| r98 S4 | 98 SE | `XhciCmMode` 0, cold start: inert | **PASS**: "no connection manager, inert", `Inert=1`, `u4` tables empty |
| rxp S1, S3, S4 | XP | idle, after the unplug, inert | **PASS**: whole; "the NHI at bus 0, device 4, function 0" |
| cxp S2 | XP | during a copy | **PASS**: the copy running at the start |
| r2k S1, S3, S4 | 2000 | idle, after the unplug, inert | **PASS**: whole, the same readings as XP |
| w98 | 98 SE | the window (XHCITR of `194699058`): Save Report, Save Snapshot to C: | **PASS**: the report holds "xhcicm.sys state snapshot", the `[u4.port]` table and "XHCICM0: state saved in XHCITR.CT0 and XHCITR.CB0"; Save Snapshot wrote `B121719A.CT0` and `.CB0` and its box read "The Thunderbolt / USB4 state is in B121719A.CT0 and .CB0." |
| w98m | 98 SE | the window again, the merged XHCITR (`ceae5cac...`) | Save Report **PASS** as above; Save Snapshot to C: **FAIL**: "No snapshot was saved in C:\: all 26 names for this minute are taken." - the all-controllers path joined "C:\" and a name with a second backslash; fixed (`ca15f82ed`) |
| w98m c | 98 SE | Save Snapshot to C: with the fix | **PASS**: `B121832A.D0`, `.T0`, `.B0`, `.P0`, `.CB0`, `.CT0` in C:\; the box "Snapshot saved in C:\: Controller 0 (HCD0): B121832A.D0, .T0, .B0 and .P0. ... The Thunderbolt / USB4 state is in B121832A.CT0 and .CB0." |
| x2c S7 | XP | two xHCI controllers (the second installed through its wizard) and the NHI, the merged XHCITR's all-controllers `/snapshot` | **PASS**: "Snapshot saved in C:\CMS: Controller 0 (XHCI98HC0): S7.D0, .T0, .B0 and .P0. Controller 1 (XHCI98HC1): S7.D1, .T1, .B1 and .P1. ... The Thunderbolt / USB4 state is in S7.CT0 and .CB0."; `S7.CT0` whole (7,596 bytes) |

Void, not counted: r98 S2, rxp S2 and r2k S2 (the snapshot started before the copy began, or typed
while the copy's window held the focus), the first c98 S2 (98 SE's DOS box held COM1), v98 tag x (a
script edited while a stopped job still read it).

**XP x64: not read, and accepted as not read (the coordinator, 2026-11-13).** The amd64 package built
here is unsigned and has no catalog, and the installer refuses it ("this 64-bit package carries the
Thunderbolt driver without its catalog, xhcicm.cat"); installing the NHI by hand through XP x64's Found
New Hardware wizard did not reach it in the time given. The amd64 `xhcicm.sys` and `XHCITR64.EXE`
passed their gates only. No certificate was made.

Decoded text size: 7,444 bytes (98 SE, the drive attached), 7,593 (XP), 3,111 (inert). On 98 SE the
NHI's location reads "not read" (its PCI bus answers no location for the NHI's PDO, record 25 section
4.7).

**What this says.** On the emulator, on 98 SE, XP and 2000, the snapshot carries the connection
manager's state at idle, after an unplug and in the inert start, whole each time, and during a copy
on 98 SE and XP; an older `xhcicm.sys` is named as not serving it; Save Report and Save Snapshot from
the window carry it on 98 SE; the all-controllers run takes it once beside two controllers on XP. Not
read: XP x64 and every amd64 target, Vista to 10, a torn read in a guest (host suite only), a failed
client (`CM_STAGE_FAILED`) or a contained USB4 connection manager, the ICM profile, more than one NHI,
the bench.

Evidence: `E:\xhci98-vm\p49\cmsnap\{v98,r98,c98b,rxp,cxp,r2k,w98,w98m,x2c,x64}\` (`serial-*.log`,
`cmdline-*.txt`, screenshots), the legs' `*.out`.

## 49.35 (continued) The event ring's final shape: XhciEventRingTrbs kept, 256 on a fresh install, always 256 below xHCI 1.0 (2026-11-13, worker asm)

Branch `w49-rst2` from `3.5.0.0` at `2d4099944`. Development host A. Design record 13 section 11.10.1
(rewritten) and the section 11.10 amendment; issue 31 section 5.

### What was decided, in order

1. The owner's check, asked before any code: is a 256-entry ring safe with the eight requests in flight that
   `3.4.2.0` gave an audio or video stream? Read from the code (record 13 sections 11.8 to 11.10,
   `XhciPipeIsoGrant`, `hcdCfgIsoPage`): what a controller can leave unread is bounded by the transfer rings
   (a pipe at its ring's TRBs plus its records plus one), and a page's allowance is computed from the ring
   the start allocated. At 256 entries one page counts 121, a page and a pool pipe 192, against 125 and 192
   in `3.4.1.0`; three streams 263 against 259. So 256 is as safe as `3.4.1.0`'s 256, and the cost is
   headroom: one page of 112 instead of 254, a second High Speed stream on the 62-TRB pool ring, and
   46.30's case held to three 4 ms URBs.
2. An interim "automatic, 256 on every controller" under `XhciEventRing` was drafted and not committed.
3. The final shape: the value is `XhciEventRingTrbs` as `3.4.2.0` and `3.4.3.0` read it; the INFs write
   256 with don't-overwrite (a fresh install has 256, an update keeps the 1024 those releases wrote); absent
   is 256; a controller below xHCI 1.0 always uses 256, noted (`evt.ring.held`) when the value asked for the
   larger ring, and the report says which ring is in force and why. `XhciEventRing` (w49-rst's
   `586873973`, in no release) is gone from the driver, the INFs, the gate and the docs.

### The change (commit 87c4b62bc)

- `XhciEventRingPick` (`xhci_caps.c`): below 1.0 held (pick 6, route 5); absent the smaller ring (pick 5,
  route 6); a DWORD 256 the smaller ring (pick 4, route 4); any other DWORD and a value of another type the
  larger ring (pick 7), as `v3.4.3.0`'s `hcdReadEventRing` read them (exactly a DWORD 256 was the smaller
  ring; nothing clamped or rounded). Picks start at 4, so the note's top byte tells this rule from `3.4.x`'s
  0 and the interim test build's 1 to 3.
- `hcdReadEventRing` tells absent from another type (`hcdReadDword`'s answers). Notes: `evt.ring.value`,
  `evt.ring.read` (0 absent, 1 a DWORD, 2 another type), `evt.ring.hciversion`, `evt.ring.held`.
- XHCITR names the ring in force and why, with the value read (`snaptest.cmd` checks each line).
- The INFs (`HKR,,XhciEventRingTrbs,0x00010003,256`), the INF gate's entry, tests and both footprints;
  `tuning.md`'s row with the owner's text and the fact that any other value is 1024 and absent 256.
- Record 13 section 11.10.1 (the decisions, the rule, the bound table, the cost, other systems' event
  rings, the next phase), `target-standing.md`, issue 31 and its README row.

Reviews: the design note one round (three P2s: a policy discriminator for the reused numbers, a value of
another type kept as the larger ring, Setup told not to remove the value; all taken) and the code two rounds
(two P3s on XHCITR's wording, fixed with `evt.ring.read`; then none).

### Host tests and gates

- `test_caps` 1305 checks, 0 failures (every read and value at HCIVERSION 0.90 to 1.2 and unread, the
  update and fresh-install cases, the hold's note).
- The INF gate's self-tests: 1914 checks, exit 0.
- Gated pair of `87c4b62bc` (slot `gated.3`, `XHCI98_SELFTEST_SKIP_OK=1`, plain `all`, which builds the
  tools): x86 and amd64 BUILD + GATES PASSED; the xhciinst, xhcimark, xhcidi, xhcitr, xhcitr snapshot and
  xhciperf self-tests each passed in both. Logs `E:\xhci98-scratch\w49\asm\r2b\build-{x86,amd64}.log`.

| Binary | SHA-256 |
|---|---|
| xhci98.sys x86 release | `93239dac...` |
| xhci98.sys x86 qemu | `6a44d49e...` |
| xhci98.sys amd64 release | `c403bc1b...` |
| xhci98.sys amd64 qemu | `a0d7d0ae...` |
| XHCITR.EXE | `ff2000f0...` |
| XHCITR64.EXE | `a293ee9f...` |

An earlier pair of the same change on `b354dc1fd` failed on x86 only in the VM-matrix verdict self-test
("...and leaves no probe behind"); the pair on the rebased tip passed whole.

### Legs

**Fresh install, XP SP3 x86** (run N1; a fresh overlay of the q35 `xp` base, 2 vCPU, QMP 49643; this tree's
x86 TEST cut, `make-release.ps1 -Arch x86 -TestLabel`, its `xhci98.sys` the release `93239dac`; a Bulk-Only
stick and a UAS disk). **PASS**:

- `INSTALL.BAT /console /yes /nopause`: exit 3;
- the controller's driver key: `XhciEventRingTrbs REG_DWORD 0x100`; no machine-wide value;
- XHCITR `/report`: `evt.ring.value=00000100`, `evt.ring.read=00000001`, `evt.ring.hciversion=00000100`,
  `evt.ring=04040100`, "256 entries, the smaller ring: XhciEventRingTrbs 256 asks for it", controller running;
- the stick and the UAS disk: the disk's data read and compared, a test file written, read back and
  compared: OK on both.

**Update from `3.4.3.0`, XP x64, two controllers** (run X5, the X3 vehicle: `3.4.3.0` installed, so
`XhciEventRingTrbs` 1024 on both controllers; this tree's `qemu` amd64 `xhci98.sys` `a0d7d0ae` copied over
it, `FC: no differences encountered`; the second controller set to present xHCI 0.96 by
`XhciQemuHciVersion`). **PASS** for the ring:

- controller 0 (1.0): `evt.ring.value=00000400`, `evt.ring.read=00000001`, `evt.ring=07000400`, "1024
  entries, the larger ring: XhciEventRingTrbs 1024 asks for it" - the 1024 kept;
- controller 1 (0.96): `evt.ring.held=00000400`, `evt.ring=06050100`, "256 entries, the smaller ring: the
  controller is xHCI 0.96, below 1.0, which always uses 256; XhciEventRingTrbs 1024 asks for the larger ring"
  - held, with the registry untouched;
- `/snapshot` with no `/c`: both controllers' sets, eight files, exit 0; the networking section in `.D0`
  only; `/snapshot /c 0` one set under the engine's names.

The faults the kit wrote to the second controller's driver key (`1B` 255, `1C` 21) did not fire, and both
controllers read running: since `18d298553` (49.24) the `qemu` flavour's periodic trigger read on 2000 to 10
looks at the machine-wide key alone, which would arm both controllers. X3 (above) read the latch on the
same vehicle before that change; X5 is the ring's leg.

**Not run: the High Speed audio case** (46.30/47.30: `uac2`, `uacemu.py --profile cx31993`, `bInterval` 1,
at 256 and at 1024). It needs an emulator with `usb-redir`; none of the QEMU builds on this host has it
(`-device help`), and 47.30's build `cb36497` is not here. 47.30's own legs read the same case on both rings
(`runs/run-47.md`, "47.30 (the legs)": the playback pipe's ring 254 TRBs and seven 4 ms requests at 1,024
entries, 112 TRBs and three at 256, with more underruns at 256). Owed to a rig with `usb-redir`.

Evidence: `E:\xhci98-scratch\w49\asm\legs\{N1,X5}` (`serial-*.log`, `debugcon-b3.log`).

## 49.39 Issue 26 again on 3.4.3.0: the timeout with its status in, the drive's refusal replayed, the stream dequeue read back, and what the next report says (2026-11-12, worker smart)

Branch `w49-smart` from `3.5.0.0` at `10b07c166`, with `3.5.0.0` merged in at `ba292831e` (`04cdb3d31`) and
`2f05c3167` (`46d14f6da`) and `b354dc1fd` (`842d2dc36`). Development host A. The emulator change is on usb4-emu branch `w49-smart`, not yet published;
its commit ids are in the worker's report, not here.

### The evidence

The reporter's `3.4.3.0` quick saves of 2026-11-12, held outside the repository:

- r4: eight sets, 00:04:10-00:09:38, one boot;
- r5: 00:27:03, the next boot.

Issue 26 section 8 is the reading. The signature checklist (S1-S16) is with the HWMonitor legs below.

### The owner's decisions (2026-11-12)

- All four fixes go in `3.5.0.0`.
- (b): "after the first refusal, until replug". Narrowed in review to per LUN and per operation code (accepted).
- (c): on by default. `XhciStreamDequeueRepair` 0 checks and logs only.
- The emulator's UAS drive is changed (route 1).
- A before and after with the real HWMonitor 1.62, run from the emulated drive.
- The owner's texts: failure event 8 and the tuning row.

### Commits

| Commit | What |
|---|---|
| `6c838f379` | design: issue 26 section 8, record 13 16.15 and 16.16 |
| `2c2920da7` | (a) xhciuas: a timeout with its final IU in and its data transfer outstanding goes to the device reset |
| `de002bb90` | (b) xhciuas: the drive's own ATA PASS-THROUGH refusal replayed per LUN and operation code |
| `20c348b4f` | design: record 17 4.12.8, issue 26 8.2 |
| `e70249269` | (c) the stream dequeue read back, every other stream placed, `XhciStreamDequeueRepair`, two qemu faults |
| `f5e6d0a0f` | design: record 17 4.12.9, record 25 22.14 |
| `74cccb0e3` | (d) the stream contexts and trace in the snapshot, the failure ring's times, an unmatched event's TRB, the quick save's flush and warning |
| `5727c53c2` | the owner's texts: failure event 8, the `XhciStreamDequeueRepair` row |
| `04cdb3d31` | merge of `3.5.0.0` at `ba292831e` (49.35); this task's qemu faults renumbered `1D` and `1E` (49.35 has `1B` and `1C`); the quick save flushes each controller's set |
| `46d14f6da` | merge of `3.5.0.0` at `2f05c3167` (build scripts and build-and-test.md only) |
| `a16dc9d62` | design: record 13 16.17 and issue 26 8.2: a drive a reset does not bring back is given up until it is replugged (the owner's choice (A)) |
| `b685e36c0` | xhciuas: the give-up (event 10), per command marks taken up by the recovery worker; XHCITR names event 10 |
| `842d2dc36` | merge of `3.5.0.0` at `b354dc1fd` (49.34's XhciResumeKeepBot absent; mode 4 on 98 SE and ME); the INF gate's count paragraph and self-test row list resolved |
| `650c1c789` | xhciuas: only data in the timed-out commands' direction clears the give-up count (after leg A3K2, the owner's narrowing) |
| `def3a7aee` | run-49.md 49.39; record 13 16.17 and issue 26 with what the legs read |
| `214780404` | merge of `3.5.0.0` at `763b38506`; the quick save also flushes the connection manager's BASE.CT<n> and BASE.CB<n> |
| `f8225169b` | merge of `3.5.0.0` at `f49975b28`; `XhciStreamDequeueRepair`'s settings row (instValues, the Settings page) |
| `88227cb79` | the owner's shorter choice-1 text, which fits Setup's drop-down (XHCIWIZ's self-test) |
| `ba12fc5e3` | the quick save finds the cm files by name (GetFileAttributesA), importing nothing the 98 SE allowlist lacks |
| `a59f1a4da` | design: record 17 4.12.8, a restart between c1's difference and its resend (the merge review's finding) |
| `a9bef71df` | c1's resend read for a restart first, a refused resend read the same way, only an empty stream resent; fault 1F |
| `ba3d3be76` | the empty stream re-read and resent after the bounded stop, not placed; two resends and two stops |
| `9a72b1eae` | fault 1F rings an empty stream when no other holds a TD, so the restart always happens |
| `43f71ef18` | merge of `3.5.0.0` at `018d08d21` |
| `bfcba9c3a` | merge of `3.5.0.0` at `59c7b9244` |
| (this commit) | the give-up counts per direction (the second merge review): two read resets, one write reset and a good write no longer erase the reads' count |

usb4-emu `w49-smart`, five commits:

- usb-uas: an orphaned data packet found by its cancel, no assertion;
- usb-uas `x-ata-passthrough-refuse`;
- qemu-xhci `x-stream-stop-stale` (first form: a late Stopped event);
- the same, the late event a Success completion;
- qemu-xhci `x-stream-stop-stale-sticky`; migration blocked with `x-stream-stop-stale` on.

All default off.

### Review

| Item | Rounds |
|---|---|
| Plan | three (P1: (c) could rewind progress; (a)'s predicate a policy, not proof; (b) narrowed; failure-time compatibility), then converged |
| Design (a)(b) | no findings |
| (a) code | no findings |
| (b) code | no findings |
| Design (c) | one finding (the QEMU fetch pointer would cycle a healthy drive), fixed |
| (c) code | two findings (a running endpoint's contexts read; the off switch still cycling), then a declaration-order P1, then clean |
| Design (d) | one finding (a read thawing the capture), fixed |
| (d) code | findings fixed (the flush "NOT" text; the trace before early returns; a milliseconds overflow), then clean |
| Merge `ba292831e` | clean |
| Merge `2f05c3167` | clean |
| Design 16.17 | one finding (the give-up skipped L5's escape when the cancels never come back: now the bounded drain and the cycle if not drained), then clean |
| 16.17 code | two findings in turn (the tick's decision stale if data arrived before the worker; FDO-level debt outliving a reset drain: now per command), then clean |
| Merge `b354dc1fd` | one finding (the count paragraph's numbers: 51, 55 on the 32-bit file), fixed |
| Emulator, usb-uas | two findings (unbounded orphans, sense length), then clean |
| Emulator, stale | clean |
| Emulator, sticky | three findings in turn (a second silent endpoint overwrote the first; the state not migrated; the blocker outlived a failed PCI realize), each fixed, then clean |

### Builds

| Build | Commit | Result | Binaries |
|---|---|---|---|
| gated #1, x86 all + amd64 all | `de002bb90` | BUILD + GATES PASSED both | `E:\xhci98-scratch\w49\smart\bin1\SHA256SUMS` |
| gated #2, x86 all + amd64 all | `74cccb0e3` | BUILD + GATES PASSED both | `E:\xhci98-scratch\w49\smart\bin2-74cccb0e3` |
| gated #3, tools first, x86 all + amd64 all | `04cdb3d31` | BUILD + GATES PASSED both; xhciinst, xhcimark, xhcidi, xhcitr, xhcitr snapshot and xhciperf self-tests passed in both | `E:\xhci98-scratch\w49\smart\bin3-04cdb3d31\SHA256SUMS` |
| gated #4, tools first, x86 all + amd64 all | `b685e36c0` | BUILD + GATES PASSED both; every tool self-test passed in both | `E:\xhci98-scratch\w49\smart\bin4-b685e36c0\SHA256SUMS` |
| gated #5, tools first, x86 all + amd64 all | `650c1c789` | BUILD + GATES PASSED both; every tool self-test passed in both | `E:\xhci98-scratch\w49\smart\bin5-650c1c789\SHA256SUMS` (release amd64 xhci98.sys `3c564208...`, xhciuas.sys `265569ab...`) |
| gated, tools first, x86 all + amd64 all | `ba12fc5e3` | BUILD + GATES PASSED both; every tool self-test passed in both (after two stopped attempts: XHCIWIZ's width check, then XHCITR's import check) | release amd64 xhci98.sys `450c17e9...`, xhciuas.sys `8c4f600f...` |
| gated #6, tools first, x86 all + amd64 all | `ba3d3be76` | BUILD + GATES PASSED both; every tool self-test passed in both | `E:\xhci98-scratch\w49\smart\bin6-ba3d3be76` |
| gated #7, tools first, x86 all + amd64 all | `43f71ef18` | BUILD + GATES PASSED both; every tool self-test passed in both | `E:\xhci98-scratch\w49\smart\bin7-43f71ef18` |

Gated #3's hashes:

- release amd64: xhci98.sys `07e569ac...`, xhciuas.sys `2db45d19...`;
- qemu amd64: xhci98.sys `b3354bb6...`;
- release x86: xhci98.sys `78142686...`, xhciuas.sys `edacf325...`;
- XHCITR64 `9e8f0be6...`.

Emulators, under `E:\xhci98-vm\p49\smart\`:

| Folder | qemu-system-x86_64.exe | Holds |
|---|---|---|
| `bin-emu-49smart` | `efd29bc0...` | refusal |
| `bin-emu-49smart3` | `cf4e9880...` | + stale, Success form |
| `bin-emu-49smart4` | `ea3611bc...` | + sticky |

The version stamp of this tree still reads `3.4.3.0`. Every AFTER leg shows the loaded files are this tree's by what
they do (events 8 and 9, `StreamDeqChecked`, the stream sections) and by fc after the copy.

### The signature checklist

The reporter's record (r4), item by item, against the BEFORE (H3: `3.4.3.0`, the refusal and the stale-stream
model) and the AFTER (A3S: this tree's drivers, the same emulator):

| Item | Reporter | BEFORE | AFTER |
|---|---|---|---|
| S1 the drive | UAS, 4 streams, Intel xHCI | UAS with streams, QEMU's xHCI | the same |
| S2 healthy before | sense 0, timeouts 0 | timeouts 0 | timeouts 0 |
| S3 the trigger | a program's A1h refused 5/20/00, data-in posted | the same | the same, at 137 s |
| S4 one unmatched event | QueueUnmatched 0 -> 1, staying 1 | the same | the same |
| S5 working a while first | about 180 data commands | none (differs: the model goes silent at once) | - |
| S6 timeout with status in | "timed out ... final-IU-in data-posted", status GOOD | the same lines | one, at 147 s, then event 8 |
| S7 each timeout aborted | aborts = timeouts (29/29) | 9/9 at 60 s, 18/18 at 120 s | aborts 0: the device reset instead |
| S8 never a reset | LUN 0, port 0 | LUN 0, port 0 | port resets 1 |
| S9 a command repeated | READ CAPACITY (10) | READ CAPACITY (10) and READ (10) | none |
| S10 no-data commands go on | yes | not decisive | - |
| S11 back to back | 29 in about 5 minutes | 9 a minute | 1 in 5 minutes |
| S12 no fault, window 0/3 | yes | yes | yes |
| S13 cfg.deq.running, one strict violation | once each, after the hang began | absent (differs: QEMU never restarts an endpoint by itself) | absent |
| S14 stream contexts | not in 3.4.3.0 reports | not in 3.4.3.0 reports | in the report: every stream at the driver's dequeue after the reset |
| S15 what Windows shows | the program hangs, "semaphore timeout" | HWMonitor stuck at its splash's "Display", Windows' "Device Timeout" box, fc on L: never returns | HWMonitor opens, no box |
| S16 how long | until unplugged | no recovery in 5 minutes | back in about 10 s |

### Legs

Rig: XP x64 SP2 (the reporter's system), a fresh overlay of the shared q35
base for each leg, two vCPUs, QEMU's `qemu-xhci` behind a PCIe root port,
`usb-uas` + `scsi-hd` (128 MB, FAT16: XP refuses NTFS on a removable
drive and the image has no exFAT) on port 4 at SuperSpeed, HWMonitor 1.62
(`HWMonitor_x64.exe`, the reporter's version, zip SHA-256 `06a44969...`)
copied to `L:\Tools\Monitoring\hwmonitor_1.62\` and run from there. The
xHCI98 `3.4.3.0` release installed by its `INSTALL.BAT /console`; this
tree's drivers copied over the installed files where a leg says. Reports:
`XHCITR64 /report` and `/snapshot` at fixed times (what a Quick save
takes), their output over COM1. Kits `E:\xhci98-vm\p49\smart\k64`
(from worker asm's), evidence under `k64\legs\<leg>`; screenshots in
`E:\xhci98-vm\p49\smart\shots`.

| Leg | Build | Emulator | What it shows |
|---|---|---|---|
| H1 | `3.4.3.0` release | usb-uas `x-ata-passthrough-refuse=on` (`bin-emu-49smart`, `efd29bc0...`) | HWMonitor's ATA PASS-THROUGH (12) refused 5/20/00 with its data-in TD cancelled on the stream (S3, `Stopped` 1), and then nothing: reads go on, HWMonitor opens and shows L:'s space with no SMART, no timeout in 5 minutes. The refusal alone does not reproduce the hang on QEMU's xHCI: the second half of the mechanism is the controller's |
| QA1 | bin1 qemu amd64 (`de002bb90`) | stock refusal off | qemu fault 14 on endpoint 0x82 - on QEMU's usb-uas the STATUS pipe, not the data-in one: three lost status completions, each an ordinary timeout without its final IU, ABORT TASK answered, the command ended and retried; 17 copy-compares and 40 reads of a 16 MiB file all PASS. The old path unchanged by (a) |
| QA2 | the same | the same | fault 14 on 0x83 (data-in): armed, never placed on a stream TD in this emulator (no timeout in three tries); 17 copy-compares, 40 reads PASS |
| H2 | `3.4.3.0` release | + qemu-xhci `x-stream-stop-stale=on`, first form (late event Stopped) (`bin-emu-49smart2`, `a6443635...`) | the hang: back-to-back timeouts of READ (10) and READ CAPACITY (10) with "final-IU-in" and status GOOD and data outstanding, every ABORT TASK answered, no reset; HWMonitor stuck at its splash's "Display" stage and Windows' "Device Timeout" box for the drive. QueueUnmatched stayed 0 (S4 differs: a Stopped event takes the driver's stop path), so the late event was changed to a Success completion |
| H3 | `3.4.3.0` release | + `x-stream-stop-stale`, the Success form (`bin-emu-49smart3`, `cf4e9880...`) | **the BEFORE**: the reporter's signature item by item (S3, S4 0 -> 1 staying 1, S6-S12, S15, S16); S5 and S13 differ (the model goes silent at once; QEMU never restarts an endpoint by itself). HWMonitor stuck at "Display", Windows' "Device Timeout" box, fc of a file on L: never returns; timeouts = aborts, 9 at 60 s and 18 at 120 s, no reset |
| A3S | gated #3 release amd64 (`04cdb3d31`) over the 3.4.3.0 install | as H3 | **the AFTER**: the refusal at 137 s (QueueUnmatched 0 -> 1); at 147 s one timeout with its final IU in, event 8, one device reset, "ended by recovery"; no other timeout in 280 s (timeouts 1, aborts 0, port resets 1); the LUN latch line; at the second start "answered here: the drive refused it before" (event 9). Copy-compare 19/19 PASS while HWMonitor polled, the one that spanned the reset too; HWMonitor opens and shows L:; no box, the volume stayed |
| A3C | the same | refusal only (`bin-emu-49smart3`) | the control: the refusal, the latch, the replay at the second start; no timeout, no reset; 19/19 PASS |
| A3K | the same | + `x-stream-stop-stale-sticky` (`bin-emu-49smart4`, `ea3611bc...`) | the reset-proof variant on `04cdb3d31`: every READ (10) times out with its status in, fix (a) resets every 10 s (27 resets by 432 s), every command ends (BUS_RESET, the class driver's retry), fc of a file on L: errorlevel 1; the copy loop still PASS because served from Windows' cache. Not latched: the owner chose 16.17's give-up (`a16dc9d62`, `b685e36c0`) |
| QC4 | gated #3 qemu amd64 (`04cdb3d31`) | refusal (`bin-emu-49smart3`) | (c): C1 fault 1D once, the refusal's cancel left the stream's dequeue on the cancelled TD (`qemu.inj.staledeq=00010709`), the read-back found it (`StreamDeqMismatch` 1) and set it again (`StreamDeqResent` 1), the drive went on, copies PASS. C2 (1D three times) and C5 (1E) were armed but never placed: the kit's REPLUG.BAT matched no device (DC64 takes a substring, not a wildcard), so the latch held and no second cancel came; rerun on the new build. C4 (`XhciStreamDequeueRepair` 0): the mismatch found and logged (`StreamDeqMismatch` 1, no resend), the stream did not starve in this run (stream 9 not used again in the window), no timeout, 13/13 copies PASS |
| X86S | gated #3 release x86 (`04cdb3d31`) over the 3.4.3.0 install, XP SP3 x86 | as H3 | the AFTER on XP x86 (HWMonitor_x32): the refusal at 204 s (QueueUnmatched 1), at 214 s one event-8 reset, then none; the latch; the replay at 378 s; copies PASS; dir and fc of a file on L: PASS |
| w98se-window | gated #3 x86 release package (family layout + the 3.4.3.0 USBDRV) | the release matrix (`scripts\vm-matrix`, slot 0) | PASS in 857 s under TCG |
| w2k-console | the same | the same | PASS in 338 s |
| w7-window | the same | the same | PASS in 898 s |
| xp-window | the same | the same | PASS in 662 s |
| A3K2 | gated #4 release amd64 (`b685e36c0`) | as A3K, with the copy loop | the give-up did not come: the loop's writes on data-out delivered bytes every 30 s and cleared the count (the rule then: either direction); port resets 36 by the replug, reads failing (fc errorlevel 1), the loop PASS from the cache. The owner narrowed the clear to the timed-out commands' direction (`650c1c789`). The host replug by device_del and device_add left the port empty (the drive never came back in 90 s); the replug is done with the device's `attached` property from then on |
| RP1 | gated #4 release amd64 | no fault | the host replug by the device's QOM `attached` property (false, 15 s, true): record 1 departed, record 2 present; dir, fc and the copy-compare PASS after it |
| QC5 | gated #4 qemu amd64 (`b685e36c0`) | refusal | as QC4, the same results: C1 found and set again (`StreamDeqMismatch` 1, `StreamDeqResent` 1), C4 logged only (`StreamDeqMismatch` 1); C2 and C5 again not placed: DC64 has no `restart` verb ("unknown command restart"), so the latch held; REPLUG.BAT now disables and enables the device |
| A3K4 | gated #5 release amd64 (`650c1c789`) | as A3K, replug by `attached` | **the give-up**: after the refusal, reads time out with their status in; three event-8 resets 10 s apart (port resets 3; the ring's last eight hold those at 156 and 166 s), the fourth at 176 s "given up: the drive's data never arrived after 3 resets; until it is replugged"; no reset after it (port resets 3 at 505 s); reads of L: fail at once (fc errorlevel 1 at once, where before each took 10 s); the host replug: record 1 departed, record 2 present (sense 1, the UNIT ATTENTION, no timeout), dir, fc and a copy-compare PASS. The copy loop's uncached check stopped at its first round on a command-line refusal of XHCIPERF (`/size` takes 16 to 16384), so this run had no writes in the window; rerun as A3K5 |
| QC6 | gated #5 qemu amd64 | refusal | no fault fired: since `3.5.0.0` `b354dc1fd` (task 49.24) the qemu flavour reads the trigger once a second from the machine-wide key `Services\USB` alone on 2000 to 10, and the kit wrote the driver key; stopped by hand in its last phase, rerun as QC7 with INJ.BAT writing the machine-wide key. REPLUG.BAT's disable and enable worked (the refusal came again after it) |
| A3K5 | gated #5 release amd64 (`650c1c789`) | as A3K, the copy loop with its uncached check (XHCIPERF `/run /verify`, FILE_FLAG_NO_BUFFERING), replug by `attached` | **the give-up with the copy loop running**. At HWMonitor's start the refusal's cancel met the stale fault and fix (c) gave the stream up and cycled the drive (`cfg.deq.giveup=00010705`, `cfg.deq.cycle=1`): record 1 departed. The re-enumerated drive (record 2) delivered no data from its first commands (REPORT LUNS A0h, INQUIRY 12h): event 8 at 20 and 30 s (and one before), then at 40 s "given up: ... until it is replugged" (port resets 3). From then on: the copy loop FAILED every round, cached (`FAIL-copy-back`) and uncached (XHCIPERF exit 2, the folder refused), 21 rounds over 10 minutes; L: was gone (dir errorlevel 1: the given-up drive never mounted); HWMonitor could not be started from L: ("Windows cannot find ...", dismissed by the host); port resets stayed 3 for 20 minutes. The host replug: a new record (sense 1, the UNIT ATTENTION, no timeout), dir and fc of L: PASS, a copy-compare PASS, XHCIPERF uncached PASS (exit 0). No "Delayed Write Failed" box was on its screenshots (A3K6 has one) |
| A3K6 | gated #5 release amd64 (`650c1c789`), `XhciStreamDequeueRepair` 0 for this leg (so fix (c)'s cycle cannot come first) | as A3K5 | **the give-up with writes in the window**: the volume mounted, the copy loop's rounds 1-3 PASS cached and uncached; after the refusal (137 s) the reads time out with their status in, three event-8 resets 10 s apart, and at 176 s "given up: ... until it is replugged" for the fourth (port resets 3), while the copy loop's round 4 tried its 16 MiB write to L: inside that window (the copy taken by the cache, the uncached check failing). The stream trace's capture at the third reset (35 s back, from about 131 s) holds data-out (DCI 8) Success completions only up to about 132 s, before the refusal: no write completed between the resets, so this leg shows write attempts in the window and their loss, not writes landing while the count held. After it: every round FAILED, cached and uncached; L: gone; Windows' "Delayed Write Failed" balloon for L: ("Windows was unable to save all the data for the file L:. The data has been lost.", screenshot); no further reset (port resets 3 through the replug); the host replug brought the drive back: a new record (sense 1, no timeout), dir and fc of L: PASS, two copy-compares PASS, the uncached check PASS |
| QC7 | gated #5 qemu amd64 | refusal; INJ.BAT to `Services\USB` | (c), all four: C1 fault 1D once: found (`StreamDeqMismatch` 1) and set again (`StreamDeqResent` 1), the drive went on. C2 fault 1D three times after a replug (disable and enable): three resends, then the stream given up and the drive cycled (`cfg.deq.giveup=000107A2`, `cfg.deq.cycle=1`, `StreamDeqGiveUps` 1), the drive back and copies PASS. C5 fault 1E (a restart between the read and the command): fired, no mismatch or give-up added, copies PASS. C4 (`XhciStreamDequeueRepair` 0): the stale dequeue found and logged only (`StreamDeqMismatch` 1, no resend), no timeout, 13/13 copies PASS |
| QC8 | gated #6 qemu amd64 (`ba3d3be76`) | refusal; copy loop running | C6, faults 1D and 1F once each: the stale dequeue found (`cfg.deq.readback` low nibble 1), resent and read back equal (nibble 0), no cycle; but 1F fired and rang nothing - no other stream held a TD at that instant - so the restart was not shown; 1F made to ring an empty stream then (`9a72b1eae`). C7, 1D alone after a replug: repaired the same way; copies PASS |
| QC9 | gated #7 qemu amd64 (`43f71ef18`) | refusal; copy loop running | **the restart inside the repair**. C6, 1D and 1F once each: the stale dequeue found (`StreamDeqMismatch` 1, `cfg.deq.readback.at` the cancelled TD), then 1F rang stream 1 before the resend (`qemu.inj.repairrace=00010701`); the pre-send read found the endpoint Running (`cfg.deq.readback` low nibble 3) and no resend went out; the bounded stop left it Stopped, and the stream read back equal (nibble 0): the stop's own QEMU resync (4.12.4) had set it right. No give-up, no cycle (`StreamDeqGiveUps` 0, port resets 0), the drive went on, every copy PASS (11/11). C7, 1D alone after a replug: resent and read back equal (`StreamDeqResent` 1), no cycle |


### What is owed

- The real Intel controller's mechanism: the emulator's fault is a model chosen because it reproduces the reporter's record, and a device reset clears it there; whether the Intel controller recovers the same way is the reporter's next report (issue 26 section 8, the request in the drafts).
- XhciStreamsOff with a UAS-only drive on a companion-paired root port: the send-back hold has no time limit, so a drive that never shows on USB 2.0 stays invisible until replugged (sent to the coordinator; not in this task).
- A leg with writes that complete between the counted resets while the reads stall, the count held: the rule is the host vector's (`UasDeliveryClears`) and A3K2 shows such writes clearing the count under the rule before; no leg has shown the narrowed rule with a write landing in the window (A3K6's writes did not complete there).
- The legs ran on a heavily loaded host: the timings in the tables are the guests', 10 s timeouts at the class driver's seconds; nothing here is a throughput reading.

## 49.10 (the control without the Thunderbolt and USB4 support) (2026-11-12)

The owner's decision of 2026-11-12: one more control, then the task parked for `3.5.0.0` whatever it showed.
Development host A, 10:48 to 17:12. The plan was reviewed before the first counted boot until nothing material
was left. Question: does batch 6c's excess of R6 (the release package, 10 in 201) over D6 (nothing installed, 2
in 201) go with the Thunderbolt and USB4 support, or with the rest of the package?

**Arm U7.** A fresh overlay of the same q35 ME base, prepared by the same recipe from the same release package
bytes R6 was installed from, with `INSTALL /console /yes /nopause`: R6's line without `/thunderbolt` and
`/stor98`. Without `/thunderbolt` the console's Thunderbolt question takes its default N on 98 SE and ME, and
Stor98, XHCIPFW and Thunderbolt networking come only with that support, so U7 lacks the whole bundle:
`xhcicm.sys`, its filter, `XHCIPFW.VXD`, Stor98 with `S98VSD.VXD`, and the networking miniports. Each counted boot
starts with the chain dock model at power-on, as D6 and R6 did.

**Counts and readings, fixed in advance.** 201 boots of U7, and 20 fresh boots each of the unchanged D6 and R6
images as a load check, in the order 20 cycles of U U U U U D6 U U U U U R6 and one U (241 positions, one guest at
a time, one vCPU), no early stop, every position ending in exactly one classified boot. k is U7's count; each test
Fisher two-sided against batch 6c's arms: k <= 2 consistent with a contribution from the omitted bundle (not
excluding the remaining drivers, the install's activity or interactions); k >= 10 a rate above D6 that persists
without the bundle; 3 to 9 inconclusive; all three nominal historical comparisons, the attribution exploratory. A
load-check arm differing from its 6c arm at p < 0.05 would make the reading "not comparable across host loads".

**The image.** The installer's end screen has "xHCI98 USB 3 Controller (Red Hat QEMU xHCI): installed, restart
pending" and no "xHCI98 USB4 Router" line (R6's has one), exit code 3 as for R6. No `xhcicm.sys` under
`SYSTEM32\DRIVERS`, no `stor98.mpd` or `S98VSD.VXD` under `SYSTEM\IOSUBSYS`, no `XHCIPFW.VXD`, no added `device=`
line in `SYSTEM.INI`; `WININIT.INI` has R6's pending rename of `xhci98.sys`; the registry's `Services\Class` holds
`USB\0000` (`xhci98.sys`, 3.4.3.0) and nothing naming `xhcicm`, `stor98` or `XHCIPFW`. **A deviation from the plan:**
the plan's two registry exports (`HKLM\Enum` and `Services\VxD`) failed in the guest ("Sharing violation", "File
not found") and the image was accepted without them, so whether the Enum tree holds a driverless devnode for the
USB4 host router is not shown; since none of the bundle's files is on the image, none of them can load. U7's clean
last screens show the desktop with the USB tray icon; R6's still show the Add New Hardware wizard for the "PCI
standard PCI-to-PCI bridge" that the tunnel makes. Three earlier preparation attempts were discarded (a port busy
with another guest of this host; a script edited while it ran; that script's survivor able to send keys to the
next guest).

**Result.** 241 positions, 11:13 to 17:12, every one with exactly one classified boot, no replacement, every stop
verified.

| Arm | Boots | Fatal 0E | Rate (95 % exact) | Against batch 6c |
|---|---|---|---|---|
| U7, the package without the Thunderbolt and USB4 support | 201 | 1 | 0.5 % (0.01 to 2.7) | R6 10/201: Fisher two-sided p = 0.011, difference -4.5 points (Newcombe 95 % -8.4 to -1.3); D6 2/201: p = 1.0, difference -0.5 points (-3.1 to 1.9) |
| D6 load check | 20 | 1 | 5 % (0.1 to 24.9) | D6 2/201: p = 0.25 |
| R6 load check | 20 | 0 | 0 % (0 to 16.8) | R6 10/201: p = 0.60 |

The two faults, read on their fault screens: U7 "Error: 0E : 0177 : BFF62170"; D6 "Error: 0E : 016F : BFF62170".
U7's selector 0177 sits between the 016F of nothing installed and the 019F of the full package; its address is the
base fault's, where none of batch 6c's ten R6 faults was (addresses read from the screen as data only). Host
processor 10 to 100 % (mean 80 %) with 4 to 14 other QEMU processes; batch 6c ran three guests at once on a host
at a mean of 84 % and 94 %.

**Reading (as fixed in the plan).** k = 1: with the bundle left out the rate was lower than the full package's in
batch 6c (0.5 % against 5.0 %, p = 0.011), and no difference from 6c's D6 was detected (an interval that still
allows one either way). Consistent with a contribution from the omitted bundle, if the runs are comparable; a part
played by `xhci98.sys`, `xhciuas.sys`, the install's activity or interactions is not excluded. The load check did
not flag incompatibility, weak evidence at 20 boots. Not shown: which member of the bundle (the installer offers no
finer split on 98 SE and ME), whether the bundle adds a fault or moves the base one, anything of real hardware. It
is a QEMU TCG result, one vCPU, 512 MB, one prepared image an arm, on a shared host. The fault itself occurs
without anything of this project (D6, Z6 and this batch's D6 check).

**A further control point (2026-11-13).** On a fresh q35 ME overlay with nothing of this project installed (no
xHCI98; the emulator's test device and its xHCI controller with no driver), ME
stopped with a fatal exception 0E at `016F:BFF62170` at its second cold start (its registers and screen kept with
49.24's legs). The task is parked here, by the owner's decision.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** on 2026-11-13 a fresh ME guest with
nothing of this project installed (no xHCI98, the emulator's devices with no driver) stopped with the same fatal
exception 0E at `016F:BFF62170` at its second cold start, a further control point that the fault occurs without this
package. The same day the fault was seen again on this release, the enclosure attached at power-on, in two of three
power-on starts on one image and in none on a second image; the reading on real hardware was dropped by the owner, and
the emulator side investigated at once (owner, 2026-11-13): 46 first starts on ME across eight variants on this
release (the enclosure at power-on with and without the pre-OS connection manager, hot-plugged, a dock, nothing
attached, with and without the bundled Stor98 updated, with new hardware being installed at start-up, and on the
previous release) gave no fault; the two faults of earlier that day came on a heavily loaded test host with Windows'
device installer running at start-up; the fault addresses lie in Windows' own KERNEL32, at the first instruction of a
new thread and in its module-loading code, ring-3 page faults on memory new at first touch, and nothing points at a
call into this package's drivers. Not reproduced, the cause not found; closed by the owner on 2026-11-13 on that
record, a planned item only if seen again, with its fault screen. The test values stay unmerged on `w49-me0e`, with no
code change.

## 49.36 98 SE and ME in the emulator: the switch-off after an uninstall (2026-11-12)

Development host A, with eight other guests and builds beside these (host processor 76 % on average over the
batch, 11.8 QEMU processes on average, often 95 to 100 % from 15:00). No tracked file changed. The plan was reviewed and
revised before the first run and fixed the arms, the order, the counting rule and the readings in
advance.

**Question.** On 98 SE and ME in the emulator the switch-off after the package's uninstall sometimes never ends:
the 98 SE logo "Windows is shutting down" stays, and a second Start, Shut Down, Enter changes nothing (seen in
three earlier legs). This package, Windows, or the emulator under load?

**What the leg's guest has of this project at each switch-off.** `INSTALL /console /yes` without `/thunderbolt`
leaves out `xhcicm.sys`, `XHCIPFW.VXD`, Stor98 and `S98VSD.VXD`, and the leg's machine has no NVMe and no USB4
host, so `xhci98.sys` is the only kernel image of this project that can be active; neither VxD has a System_Exit
handler. On 98 SE the first switch-off (S1) comes before the controller has a driver at all (the next start's
wizard installs it). A diagnostic trace showed that at the switch-off after the uninstall (S2) `xhci98.sys` is no
longer in memory: the uninstall's DIF_REMOVE sends QUERY_REMOVE and REMOVE to the root hub FDO, the root hub PDO
and the controller FDO (every dispatch and call-down returned 0), then DriverUnload runs about 28 s before the
power-off; no power IRP reached the image in that start (runs 04 and 10, both traced).

**Vehicle.** The tracked runner `scripts\vm-matrix\run-leg.ps1` in untracked copies per arm, with the "no power-off
after END" limit raised from 180 s to 900 s and, from run 3, the cold step's 420 s limit to 1200 s. Package: a
gated build of `3.5.0.0` at `7e82e5a55` (`xhci98.sys` `756941A2...`). QEMU 11.1.2, TCG, 98 SE, one vCPU, 512 MB,
cold starts. Arms: **a** nothing of this project (three starts with waits); **b** the install, a stick copy, a wait
in place of the uninstall; **c** the standard leg; **cq** c with a diagnostic qemu-flavour `xhci98.sys` (entry and
return witnesses on PnP and power dispatch, every completion and call-down, DriverEntry's address, the unload;
never packaged) over every copy the install may load; **w** (added) the install with `WININIT.INI` set aside before
the first switch-off. Load: every run pinned to one logical processor; **L** with a TCG burner spinning on the same
processor (the guest then got about a third of a processor on a host already near saturation), **U** without. A
probe 300 s after the power-off: three screen dumps and the registers six times over a minute, no guest memory
read.

**The batch.** A fixed order of 32 98 SE rows and 12 ME rows. After run 07's stall ten control rows (45 to 54: arm
a six times, arm w four times, all L) were moved to run next; after row 45 stalled (a bare control) the batch was
stopped there, by the coordinator's decision. Run: rows 1 to 10 and 45. Not run: rows 11 to 32, 46 to 54 and every
ME row.

| Run | Arm | Load | Result | Switch-offs (s from the power-off to QEMU's exit) |
|---|---|---|---|---|
| 01 | a | L | pass | S1, S2, S3 off |
| 02 | b | L | censored: no boot line in the cold step | none |
| 03 | c | U | pass | S1 4, S2 28, S3 4 |
| 04 | cq | U | pass | S1 4, S2 4, S3 2 (trace valid) |
| 05 | b | U | fail only on "list removed" (expected: nothing uninstalled) | S1 7, S2 12, S3 4 |
| 06 | c | L | fail: a DOS box waiting in `choice` after the copy, desktop alive | S1 29; no S2 |
| 07 | cq | L | fail: **S1 stall** at the logo | S1 stall |
| 08 | a | U | pass | S1 5, S2 5, S3 5 |
| 09 | c | L | fail on a host step (timing under load) | S1 17, S2 31, S3 29 |
| 10 | cq | U | pass | S1 14, S2 11, S3 5 (trace valid) |
| 45 | a | L | fail: **S3 stall** at the logo | S1 16, S2 13, S3 stall |

| Group (switch-off attempts) | Stalls / attempts | Exact 95 % interval |
|---|---|---|
| all | 2 / 26 | 0.9 % to 25.1 % |
| L | 2 / 11 | 2.3 % to 51.8 % |
| U | 0 / 15 | 0.0 % to 21.8 % |
| S2 after an uninstall (c, cq) | 0 / 4 | 0.0 % to 60.2 % |
| arm a (nothing of this project), L / U | 1 / 6, 0 / 3 | 0.4 % to 64.1 %, 0.0 % to 70.8 % |

**The two stalls.** Run 07 (cq, L, S1: after the install, nothing of this project loaded; the controller had no
driver yet): the 98 SE logo, the runner's retry at 120 s changed nothing; twelve register samples, no HLT, eleven in
ring 0 with the flat code segment and interrupts mostly off (EIP `c000a7bf`, `c00036df`, `c000365e`, `c000a6f9`,
`c000a881`, `c0003621`, `c00535a5`), one in V86 at CPL 3; QEMU kept consuming processor time; the guest clock
about 10 minutes behind the host's. Row 45 (a, L, S3: nothing of this project ever on the disk): the same logo, the
retry changed nothing; six samples, no HLT, ring 0, interrupts off, EIP `c000a933`, `c000a700`, `c0003621`,
`c0003f59`, `c005335e`: the same addresses as run 07. Both lie in the VMM's static region, near earlier legs'
stalls; the addresses are values only, no closed code was read. Under L the guest clock fell 10 to 16 minutes
behind within one start, under U 90 to 147 s.

**Reading.** Read as not ours (the coordinator's placement, 2026-11-12): a bare 98 SE guest with nothing of this
project ever installed stalls the same way, with the same failed retry and the same busy ring-0 loop with interrupts off at the same
addresses as the stall after the install, where nothing of this project was loaded either; at the switch-off after
an uninstall no kernel code of this project is loaded on 98 SE. Every switch-off after an uninstall completed (4 of
4). Both stalls came with the guest starved of processor time, so they are load-associated; the mechanism inside
Windows or the emulator is not identified. Inferred, not established: the earlier legs' stalls are this same shutdown stall
under host load. Neither stall of this batch came after an uninstall, only four switch-offs after an uninstall were
read, and an unloaded driver's earlier actions are not excluded by its unload, so a contribution of the package to
the earlier uninstall stalls is not ruled out. Not known: ME's rate and shape (not run), and whether it
occurs on real hardware (never seen there). No fix in the package; a harness note.

## 49.7 (the remaining readings) The COM1 faults with the line's interrupt on and off; the data ring's latch at a 98 SE end (2026-11-12)

Development host A, no code change. The plan was reviewed and revised before the legs (and the latch leg's
stimulus again after the Linux image was found to lack `thunderbolt_dma_test`). The package: `3.5.0.0` at
`7e82e5a55`, gated builds of both architectures (`xhcicm.sys` `FA9D7A8A...`, `xhcinet.sys` `A78717E1...`,
`xhci98.sys` `756941A2...`), and the qemu flavour's `xhcicm.sys` for the latch leg.

**Vehicle.** A: 98 SE on q35 with the Alpine Ridge NHI model (firmware connection manager), one vCPU, 512 MB, TCG,
installed with `/thunderbolt /tbnet`; two images derived from it with `XhciPfwNhiLine` 0 and machine-wide
`XhciCmRingInterrupt` 1 (ON) or 0 (OFF), each arm a fresh overlay. B: a Kubuntu 26.04.1 live image (kernel 7.0, the
stock `thunderbolt` and `thunderbolt_net`) on a USB4 host model, WHPX, pinging A continuously (one summary a
minute). The harness: a batch file typed into Start, Run writes a numbered word, the time and a 2 KB text to COM1;
strictly serial; "closed" (each finished DOS window closed before the next run) and "kept" (the finished window
left on the screen).

**The COM1 faults.** Both arms give the NHI its line (IRQ 10, level, shared); they differ in one value. ON connects
the line's interrupt ("Ring interrupt: on (N wake-ups)"), OFF leaves it unconnected ("off (checked every clock
tick)"). B's summaries read 300 of 300 throughout but where noted.

| Cell | Arm | Windows closed: runs, ok, write faults | Windows kept: runs, ok, write faults | B |
|---|---|---|---|---|
| on 1 | ON | 40, 40, 0 | 6, 1, 5 | pinging through the closed runs; its own kernel panic at about 44 min, before the kept runs |
| off 1 | OFF | 20, 20, 0 | 10, 1, 9 | pinging throughout |
| on 2 | ON | 25, 25, 0 | 5, 1, 4 | pinging throughout |
| off 2 | OFF | 25, 25, 0 | 5, 1, 4 | pinging throughout; its panic after the last run |

Every miss was a write fault: the new DOS window at "Write fault error writing device COM1 / Abort, Retry, Ignore,
Fail?" over the previous run's "Finished" window. In every kept phase the first run passes and every later one
faults; with each window closed none faults (110 of 110). Link changes 1 up, 0 down, no errors, in every report.
**Reading:** the faults come from a finished DOS window that keeps COM1, with the line's interrupt on and off and
the peer pinging or dead - the harness's, not the line's. Not seen in 135 closed and 26 kept runs on four fresh
overlays: a freeze, an exit of the emulator, a black screen. The freeze in virtual-8086 code and the emulator's exit
seen once each in 49.7's earlier legs did not come back and are not read.

**The data ring's own latch with the data ring open at a 98 SE end.** The earlier legs met the line's loss only at
traffic made by a cable pull, whose link-down had already shut the data ring's cause. Here A moved to the Titan
Ridge 4C model (two ports): the cable on port 1, and on port 2 a TB3 device router with no drive, hot-added, whose
connection was toggled every 150 ms for 15 s (about 100 toggles a burst) so that ring-0 frames stay pending across
consecutive maintenance passes; the qemu flavour's `xhcicm.sys` with `XhciCmQemuIrqDead` 300 (the interrupts
dropped after 300 claims) and `XhciCmNet` 1 machine-wide (Titan Ridge's NHI instance, installed after the installer,
had networking off). Two fresh overlays, B pinging at 0.2 s throughout.

| Step | First overlay | Second overlay |
|---|---|---|
| the start | the line connected, its self-test passed, the network's interrupt open, the link up | the same |
| after 900 s of pings (about 4,500 echoes, no loss) | "Ring interrupt: on (286 wake-ups)", no loss line | "on (287 wake-ups)", no loss line |
| the bursts | at the first: `cm.irq.suspect=0000012C`, `cm.irq.selftest.fail`, `cm.irq.lost=00000001`, `cm.irq.down=00000111` | at the second: the same |
| between the network's start and the suspicion | no network down, no link down, no storm | the same |
| `cm.net.irq.latched` | not printed | not printed |
| the report after | "Ring interrupt: off (fell back to checking every clock tick)", 1 up, 0 down, no errors | the same |
| B's pings across it | 300 of 300 every minute | the same |
| A's shutdown after | did not finish (the VMM's range, nothing of this project's images; 49.36's shape) | finished |

**Verdict: pass** (2 of 2 in the emulator). With the data ring's cause open and the link up the suspicion comes at
exactly 300 claims, the self-test fails, the line is counted lost and taken down to the poll, and the network carries
on with no link change and no error. That the injection dropped the interrupts is inferred (the claim count, the
self-test, the wake-ups stopping). Not read: Alpine Ridge (one port), the latch through a storm with the data cause
open. **Found (passed to 49.27):** `cm.net.irq.latched` is never printed for a lost line - the suspect path shuts the
data cause first (`cmRirqDataShut` clears `DataOpen`), and `CmRirqDown`'s note is printed only while `DataOpen` is
set, so it fires only for a storm or a data cause that cannot be masked, where record 35 section 5.5 lists the line
as the data ring's latch.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** found on 2026-11-11: the gated
build's wrapper had no Python on its path, so `XHCIPFW.VXD` and `S98VSD.VXD` were silently not built there. Closed on
2026-11-13: the silent skip had already been fixed in this phase (an unbuilt `XHCIPFW.VXD` or `S98VSD.VXD` fails the
gated build, and the packager refuses an image missing or older than its sources, merge `faeeaba2b`), and the two
failures that named the wrong cause are fixed: a gated build checks for Python in a step of its own and names it, and
the Microsoft Store's `python.exe` alias counts as no Python to the VxD builds (merge `b85099dba`;
`build-and-test.md`). The owner closed 49.7 the same day: the networking views read and the build wrapper fixed; the
two oddities, each seen once in the emulator (the freeze in virtual-8086 code and the emulator's own exit), stay noted
here and become a planned item only if seen again.

## 49.13 (S.2: a write in flight across a sleep) (2026-11-12 and 13)

In the emulator: q35, the ASM4242 model, the plain NVMe enclosure model (a USB4 device router whose PCIe switch is
03:00.0 upstream and 04:00.0 downstream, the NVMe at 05:00.0) plugged in after the desktop, Stor98 on the drive.
"I/O" means a write loop running at the sleep; "quiet" means none for 35 s before and 30 s after.

**S.2** (no firmware connection manager; the tunnel port 02:00.0 a native PCI Express hot-plug port), 7 x64:

| Tree | State | Load | Result |
|---|---|---|---|
| `3.5.0.0` | standby | I/O | hang (the write never returns; no shutdown), twice |
| `3.5.0.0`, `XhciCmSleepEntry` 0 | standby | I/O | hang |
| `3.4.3.0` released | standby | I/O | hang |
| `3.5.0.0` | standby | quiet | the drive missing after the wake, a clean shutdown |
| `3.5.0.0` | hibernation | I/O | hang, twice |
| `3.4.3.0` released | hibernation | I/O | once resumed with the drive gone and its writes failing, no hang; once hang |
| `3.5.0.0` | hibernation | quiet | the drive missing, a clean shutdown |
| `3.5.0.0`, Stor98 disabled | hibernation | (no drive) | clean |
| `3.5.0.0` | hibernation | quiet, Safely Remove first | not read (three attempts void: the drive was still there at the sleep) |

**S.2**, 7 x86 (qemu-flavour images; the emulator's configuration-cycle and NVMe traces; Stor98's debug-port build):

| Image | State | Load | Result |
|---|---|---|---|
| `3.5.0.0` at `7e82e5a55` | standby | I/O | hang, twice; the second is the trace of the cause |
| a later tip | standby | I/O | hang; Stor98 reads a capability of 0 (`ENG_FAIL_CAPS`), no SRB after HwInitialize |
| `7e82e5a55` | standby | quiet | the drive missing; START STOP UNIT, REQUEST SENSE and INQUIRY failed by Stor98 |
| `7e82e5a55` and a later tip | hibernation | I/O | hang |
| 49.11's settle compiled out | hibernation | I/O | hang: the chain kept and numbered, the NVMe restored, Stor98 failed as above (the image's presence in the guest not separately proven) |
| a diagnostic writing the bridges' configuration through their own PDOs | standby | quiet | the drive missing; the diagnostic's effect never showed in the trace: void |

**S.3** (a firmware connection manager), 7 x64: standby and hibernation pass, the drive kept and its files compared.

**Not run:** S.5 (Titan Ridge, 7 x64); S.3's hibernation on 10 x64; S.6's hibernation; S.2 on 10 x64.

**The cause** (the trace on 7 x86; the hibernations the same):

- The router's reset at the wake resets everything behind the tunnel: the command, bus numbers and windows of both
  switch bridges read 0.
- Windows' PCI driver restores the tunnel port in full while the tunnel is still down; no configuration access reaches
  bus 3 or 4 before the tunnel's return.
- After the return the bridges receive only their bus numbers, each after an enumeration's reads that find it
  unnumbered - never their windows or command (Windows cleared the command before the sleep).
- Windows then restores the NVMe in full, behind a switch that forwards nothing. Stor98's HwFindAdapter and
  HwInitialize run again, read a capability of 0 and fail, and the adapter stays started in FAILED.
- With nothing in flight, the port driver at once sends START STOP UNIT, REQUEST SENSE and INQUIRY; Stor98 fails each
  and Windows drops the disk. With a write in flight, no request of any kind reaches Stor98 after HwInitialize: the
  write and everything behind it are held in Windows' storage stack above Stor98 (port or class driver; closed code,
  not read).
- On a real computer a read that a reset switch does not forward ends in an Unsupported Request and reads all ones (an
  inference), so Stor98 takes its "gone" path instead; what Windows' storage stack does then is unread.
- 49.11's settle wait is not the decider: with it compiled out the tunnel returned before Windows' rescan as with it,
  the switch was numbered and left forwarding nothing, and the write hung; `3.4.3.0`, which has no settle, hangs too.

**Designs written and rejected in review.** (1) Writing the switch's headers back from a copy taken at the suspend:
`HalSetBusDataByOffset` refuses base and limit registers by contract; `IRP_MN_WRITE_CONFIG` through the connection
manager reaches only a member's own PDO and refuses the header; a raw write under an interprocessor rendezvous, as
`XHCIPFW.SYS` writes, failed on four counts (the route's own lock inside the rendezvous is a deadlock; the rendezvous
does not exclude Windows' own configuration transaction; nothing proves a saved assignment is still Windows'; a
part-written bridge can forward); the bridges' own PDOs through the chain's child filters were not measured (the
diagnostic above). (2) Holding the bridge's D0, or a non-pageable NHI: record 22 section 11.1.8's reasons stand. (3)
Ejecting the member at the resume: no link-down upcall as an unplug makes, so the storage channel never hears a DOWN
and the eject's acknowledgement and retirement are bypassed; no field says this resume rebuilt this port's tunnel; the
predicate must not depend on bus numbers Windows' rescan may already have written; and the outcome is a surprise
removal, not Safely Remove's.

**What the next design must provide:** a per-resume, per-port record of a PCIe tunnel rebuilt after its router read
lost; a reset-switch predicate on the command enables and windows, read at the decision point, with a leg that reads
the headers there; the storage channel's DOWN, acknowledgement and retirement for that member as an unplug delivers
them; and either a removal left until a replug or a rebuild after the retirement is observed, so that Windows
enumerates the switch afresh. It must first measure, on 7 x86, whether a chain removed at the wake releases a write
already held in the storage stack. Nothing changed in `3.5.0.0`.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** read after the table above,
2026-11-13. On 7 x86 and 7 x64, with a write arriving while the wake finishes, Windows' own storage port driver keeps
the disk powered down with its queue frozen and no power request pending, and the write, the unplug and the shutdown
wait behind it; a writer that starts later, or an idle drive, gets clean errors and the unplug is processed. A Stor98
change that reports the drive gone at the wake was built and tested and does not clear it, because no request reaches
Stor98; it is left out of `3.5.0.0`. The stuck write and the shutdown that never ends were then read on 10 x86 with
Stor98 on the drive after a standby, on 7 x86 after a hibernation with a write issued before it, on 7 x64 after a
standby (an unplug after the wake does not free it), on 10 x64 after a standby and on 8.1 x86 and x64 after a
hibernation, each with Stor98 on the drive; with Windows' own NVMe driver on the drive (10 x64) the writes failed at
once and nothing hung; with the drive idle at the sleep, or a writer that starts after the wake, the errors are clean
and the shutdown finishes; the replug after the wake brought the drive back on 7 after a standby and after a
hibernation. 8 and 8.1 offer no standby on the test guests. Kernel memory dumps of the hung state on 7 x86 (four,
against one of the clean failure): in every hung case the disk's power-up to D0 had failed with
`STATUS_NO_SUCH_DEVICE` before any request was sent to Stor98 (the class driver's power-up record holds that status;
START UNIT was never sent); the storage port driver, having asked for D0 once when the first request met the unit in
D3, marks the unit and parks every later request in its queue ahead of the code that would ask again, and nothing
checks whether that D0 succeeded; the bus rescan's own INQUIRY parks in the same queue, which blocks the PnP worker,
the unplug and the shutdown; Stor98's state makes no difference (three different states, the same picture), and
nothing a miniport can do releases it. Likely, not proven then: the failed status comes from Windows' PCI driver
finding the device not there because the enclosure's switch did not yet have its bus numbers back when the power-up
arrived (the configuration trace shows them restored much later in the hung case than in the clean one); so the fix's
direction is the switch's configuration back before the drive's power-up arrives, or a second power-up request on the
disk's stack once the tunnel is rebuilt. The owner's machines: dropped by the owner the same day, the owner's board
cannot produce the case (after a hibernation, even with the mains off for 30 s, its firmware had the tunnel up again
before Windows resumed; it offers no standby); read there under Windows 10 x64 the same day, a copy running across the
hibernation went on after the wake and compared clean, and with the drive idle the drive was usable. The stop before
the sleep, read on 7 x86: the drive stopped from the USB Tree before a standby and before a hibernation, Windows woke
clean, a replug brought the drive back with a clean copy and compare, and the shutdown finished; the release notes and
the README add that advice to the known limitation (owner, 2026-11-13). The probe (a test-only build of the connection
manager that, a few seconds after its resume finished, asks Windows for a second power-up of the parked disk): on 7
x86 it released the hang after a standby twice and after a hibernation once (the copy in flight returned, later copies
failed at once as for a drive that is gone, the unplug was processed and the forced shutdown powered off within
seconds); in the healthy case (the firmware bringing the tunnel back) the extra request changed nothing and the copy
went on; the build's record shows Windows' PCI driver failing the resume power-up of the enclosure's two switch
functions and of the NVMe function with "no such device", which until then was only likely; on 10 x86 the hang did not
reproduce in the probe's two runs, and the probe's way of finding the disk did not work there; x64, 8.1, 2000 and XP
were not read with it; the test build is not part of the release. A correction to the 7 x86 readings of that day: the
test image used for them put back an older copy of the connection manager at each start (a build of 2026-11-12, not
the release tip), while `xhci98.sys` and Stor98 were the tip's; the 7 x64, 8.1 and 10 readings were of the tip, proven
in each guest, and show the same. Owner, 2026-11-13: `3.5.0.0` is released with the known limitation as written
(Windows 7 to 10), and the fix is the first task of the next release (TBSLEEPWRITE.1). What the fix needs: find the
disk's logical unit below the tunnelled NVMe function without the port driver's private layout; act only after the
function's power-up was seen to fail at this wake and the tunnel is back, once per wake, with one retry at most; the
released write's data is lost where today it hangs; this driver is not the disk's power policy owner; Windows 8 to
10's power management is unread. To test: 7 x86 and x64 (standby and hibernation, a writer running and idle, with and
without the firmware setting up the port), 8.1 and 10, 10 x64 with Windows' own NVMe driver left untouched, two
drives, the stop before the sleep, 2000 and XP hibernation, and real hardware. The x64 sleep legs of the same day
found 49.46. On 2026-11-14 the owner dropped the legs left unread (S.5 on Titan Ridge, 7 x64; S.3's hibernation on 10
x64; S.6's hibernation; 8 x64's S.2) and the task was closed.

## 49.5 (bench, 2026-11-12) The owner's ThinkPad P14s Gen 1, 98 SE: staging 1 and 0 beside copies and audio

**The kits, prepared and read in a 98 SE guest first.** The laptop kit (49.5, 49.15, 49.19), the USB4 desktop kit
(49.17) and the networking kit (49.21), built from the gated build of `3.5.0.0` at `7e82e5a55`. Found while preparing:
the earlier kit's staging-1 file wrote `XhciDmaStaging` 1 into the controllers' own keys while the laptop's
machine-wide key held 0, and `xhci98.sys` takes a machine-wide value first, so no staging-1 set had ever run on the
laptop (the new kit sets the machine-wide value and refuses unless the report shows the running driver's
`stage.switch` at the value it needs, which also catches a missed cold start); 49.15's address counters cannot be read
on a real computer with any existing build (they are qemu-flavour notes made at the controller's release, their
addresses going to port 0xE9 alone), so the kit runs record 17 section 4.13.9 item 8's fallback; XHCITR has no trace
of its own; and COMMAND.COM expands `%VAR%` once a line, so a one-line drive scan took the last drive holding the kit
(one IF line a drive letter now). In the guest (TCG, one processor, a host at about 96 %): every refusal fired where
it should (staging not set; the driver still running another value), A1, B1 and W1 at 1 and C0 at 0 passed (C1 7 of
8 plays, the emulator's load); the kits made relocatable at the owner's request (internal disk or the USB disk, the
USB disk under test marked by a file at its top) and read again. Each kit was reviewed and revised.

**The owner's run (2026-11-12 evening).** The kit on the internal disk, the USB disk under test a UAS drive
(SuperSpeed, on the Intel 400 Series LP controller, root port 15), `xhci98.sys` a development build carrying the old
version stamp. Every run passed by the kit's measure: W1 at staging 1 (XCOPY 10 of 10, XHCIPERF exit 0 with 128 MiB
verified, the C: loop 133 passes and no failure); A1, B1 and C1 at 1 and A0, B0 and C0 at 0, 8 of 8 plays each, the copy
loops 81 of 81 and 85 of 85; set back as found (staging 0). Two stalls and three slow plays are in the files:

- **W1 at staging 1: about 12.5 minutes, the whole machine (the owner's account: it did not finish in 10 minutes and
  came back by itself; no restart).** XHCIPERF's exit is stamped 8:24:40 p.m. and the C: loop's round 132 started 8:24:40
  and passed; both files' last writes are 20:37. The stall began within a second of XHCIPERF's exit (it deletes its
  128 MiB test file on the USB disk as it ends), while the C: loop was copying.
- **C1 at staging 1: 5 min 19 s**, at the end of a C:-only copy loop (no USB transfer but the idle audio device).
- **Slow plays:** C0 play 1 15,591 ms for the 4 s tone (staging 0, the control), C1 16.8 s between two plays, B0 play
  5 4,231 ms; every other play 4,000 to 4,032 ms. The two long ones at the start of a C:-only copy loop, at both values.

The driver's records, the report before W1 against the first after the stall (one driver session): both controllers'
logs line for line the same but for counters, no new note at all (no timeout, recovery, reset, staging give-up or
error); completion codes grew only in Success and Short Packet; the UAS drive's 25,088 commands, at most one in flight,
all ended; the tolerance clock ran at 100.1 ms a tick on both controllers over the 20.5 minutes of Windows' clock, so the
driver's timer DPC ran at its full rate as far as Windows' clock measured (which does not exclude a stretch that also
stopped Windows' clock; the owner's watch says Windows' clock kept time). No bound in the driver is near 13 minutes (the
UAS driver's longest is an SRB's 30 s; staging's window give-up 5 s). **Reading** (one run each, not settled): a
whole-machine freeze with the driver's timers running, no USB error and no driver bound near its length, ending by
itself, and a second stall at the end of a C:-only loop, point at Windows 98 held on a disk - most likely C:, through
VCACHE - rather than at the USB disk's transfers; staging 1 is in both long stalls and not in C0, so a share of staging
is not excluded. What would settle it: which driver runs C: (`IOS.LOG`), a heartbeat from a third window, stamps around
each step (all in the next kit). The owner, 2026-11-12: `XhciDmaStaging` stays 0 by default, 1 an advanced setting, and
the release notes carry the limitation in his words. Part 1 (49.19) was not run.

**The gating kit against a development package of the tip at `763b38506`, in a guest (2026-11-12 and 13).** A fresh 98 SE
install: the first-start check reads "As installed: staging off and the 256-entry event ring" (the INF wrote
`XhciEventRingTrbs` 256 and `XhciDmaStaging` 0); A0's check passes; a second A0 refuses ("has been run already"). The
USB Mass Storage wizard then gave "A fatal exception 00 has occurred at 0028:FF0AE734" and the next play hung: placed
later as Windows 98 SE's own `USBAUDIO.VXD` with the emulator's audio device (the known fault of `run-13e.md`,
`run-22.md`, `run-36.md` and `run-46.md`), reproduced with audio alone and absent with the stick alone; not this
project's. The gating repeat on the release candidate, on a freshly installed laptop, is the owner's next bench.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** the Left of that day was the gating
repeat on the release candidate on a freshly installed P14s, at 0 with the event ring at 256 and at 1024, staging 1
optional and last; the owner dropped it on 2026-11-13 (the default passed on a real 98 SE computer on 2026-11-11; the
decisions table, "Phase 49, the bench decisions of 2026-11-13"), and the task was closed on it.

## 49.17 (bench) The owner's board, 98 SE: the tunnel ports named, the eject, and the BIOS-held drive (2026-11-12)

The owner's run of the USB4 desktop kit, 98 SE with `3.4.3.0` installed, a plain NVMe enclosure (ASMedia ASM2464PD
with an SK hynix drive), no dock; reports R1 to R6. The host names both tunnel ports at the start (`u4.named=01130002`,
`02130802`), so `u4.map.none` is rightly absent and 48.43's learning never decides there: at the hot plug
`u4.learn=00000102` is port 1 already known ("named: a member already"), not learned. The enclosure's first device
behind the tunnel bridge is its switch's upstream port, the far-end shape. The tunnel ports' Link Active and Presence
Detect reporting was not read and is not needed on this board. The eject closes the PCIe tunnel after a hot plug
(`u4.eject=00000100`, `u4.pcie.torn-down 1`, "PCIe closed after an eject") and after a power-on (`u4.preserved`, "PCIe
up (found at start)", the same eject notes). The ports are named at the start, the hot plug finds the port, Stor98
starts and the drive is listed.

**The BIOS-held drive.** At power-on Stor98 shows Code 10, its resources assigned as asked: the documented
limitation (Stor98 leaves a drive the BIOS has set up to the BIOS; `StorTakeFirmware` 0), and the owner used the drive
as D: through the BIOS. The power-on eject's Stop dialog named only the bridges and Stor98 (no disk, no letter: the
BIOS-driven volume has no devnode under the branch, and the dialog lists only a unit's disk children; the owner's
photographs of both dialogs agree), the tunnel closed, D: stayed in Windows, and a write to it gave Windows 98's "Disk
Write Error": the eject pulled a BIOS-driven disk from under Windows. Present in `3.4.3.0`; not a regression. Both
dialogs also carry the dock sentence ("Before you eject it, make sure none of its other devices ... are in use") for a
plain enclosure, a wording point. The owner, 2026-11-12: refuse that stop in `3.5.0.0` (49.38). The learning itself
stays unread on real hardware until a host that does not name its ports is at hand.

## 49.22 (bench) MSI stability improvements

MSI stability improvements: the work on a separate component is recorded with that component.

## 49.24.2 (continued) XP x86 and XP x64: the final legs (2026-11-12 and 13)

The rest of 49.24.2's legs, after the interim recorded under 49.24, in the emulator. No STOP, hang or fatal error anywhere in
them; no defect found in xHCI98 or the MSI stability improvements. Seven first starts after an install (XP SP3 x86 four times, one of
them an update with two controllers and one an update with mode 4 set; 7 SP1 x86 once; XP x64 twice), four under
mode 0 and two under mode 4 (one with two controllers, the second at Code 10 by design with no IRQ left): no fault at
any.

**The MSI stability improvements' header re-vendored** at version 1 (read 2026-11-12; this tree's copy SHA-256
`d5b4f45d7dda00eea11e7d85af80b12292c6e87db31686e379b6a1de986a5ac0`, CRLF; the LF original
`dc24c5f84b2fddcc524d45ca9e4857e80e7574ca22a62c6b4bf4049d17ec3b13`): the event history, the power-state guard and
an absent-only disconnect; no import row changed (the hosted build resolves `KeQueryInterruptTime` through this
driver's resolver); the x86 gated build passed. It was rebased before its merge, its documents citing the header by
version and SHA-256 alone.

**What passed, per Windows (mode 4 unless named).**

- **XP SP3 x86** (`halmacpi`, 2 vCPUs): modes 0, 1 and 2 with copies and restarts; mode 4's start (an ISA claim of IRQ
  3, MSI-X), the published state and its history read with a test program, copies,
  restarts, disable and enable, remove and rescan (with XP's reinstall), a shutdown under a live mode-4 lifetime (9
  s), hibernation and resume with the re-arm (`-vga none`), on three builds of the tip; with the qemu flavour: three
  lost interrupts served by the backstop, every interrupt lost giving Code 43 and a disable and enable recovering,
  the release's failed disconnect giving a quarantine and Code 10 under every value until a restart, the injected
  failed re-arm after a resume giving Code 43 and a disable and enable recovering; a forced IRQ 3 starting, a forced
  IRQ 4 (in use) and `XhciMsiAvoid` 2 refused with Code 10, the helper disabled giving Code 10 (mode 0 fine), the
  borrowed line refused on a shared line, two controllers on one IRQ (share 0: the second at Code 10; share 2: both
  on IRQ 3 with copies on each); the upper pool on an emulator with a wide I/O APIC (`XhciMsiPools` 1 on input 24, a
  range on input 30, an invalid pair refused with Code 10 before the library, a budget of 1 re-using the charged
  input, `XhciMsiPools` 2 taking an ISA IRQ first).
- **XP x64 SP2** (2 vCPUs, the package as a throwaway-signed copy inside the guest): the console installer to its end
  on a clean base, twice; modes 0, 1 and 2 with copies and restarts; mode 4 (IRQ 3, MSI-X vector 161h, the history
  read by the 32-bit test program under WOW64), copies, Device Manager disable and enable, a restart, a forced IRQ 4
  refused with Code 10 and its history, `XhciMsiPools` 1 not offered there (an ordinary ISA claim), hibernation and
  resume with the re-arm; with the qemu flavour: every interrupt lost giving Code 43 and a disable and enable
  recovering, the failed disconnect giving a quarantine ("kept for the boot") and Code 10 under modes 4 and 0 until a
  restart, a shutdown under a live lifetime (6 s).
- **7 SP1 x86:** modes 0 and 3 unaffected (MSI-X through the helper), hibernation entered and resumed, mode 4 refused
  (Code 10) as designed.

**After a loss of every interrupt.** On XP (x86 and x64) the trace shows the PnP state answered failed, a surprise
removal of the controller and its children and the stop; the devnode is then at Code 43, and a disable and enable
starts it again with no restart. 2000 (49.24.3) shows the same driver sequence but leaves the devnode stopped with no
problem code, and only a restart starts it: the PnP managers differ (read from the IRP traces; no system code read).

**A refused start: what a tester can read.** XHCITR cannot open a controller that did not start. The library's
published history (version 1), printed by a test program (32-bit, also under WOW64), shows every refusal the library makes, with its
status and each IRQ's reason, and a quarantine. A refusal xHCI98 makes before it calls the library (a value out of
range, `XhciMsiAvoid`, the helper not running, the refusal table after a quarantine) adds nothing, and the newest
event shown is then an earlier attempt's. The cheapest way to close that gap, a candidate for the next phase: a
failed start writes its reason and status under the controller's driver key, and `XHCITR /dump` prints it.

**Not run.** XP x86: a query-stop and its cancel (no route to a PnP-sent one in this guest), an unplug of the
controller (the absence branch), the borrowed line on a private line, two controllers with two IRQs, three
controllers, standby (not offered by the standard VGA driver), the brake by a driven input, audio, a sustained storage
run longer than 12 copies of 8 MB; on the upper pool, two controllers with the second on an upper input, a budget
exhausted by new inputs, an input forced above 23. XP x64: the failed re-arm, the three-interrupt backstop, remove and
rescan, two controllers, a query-stop and its cancel, an unplug, standby, the borrowed line. Real hardware.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** read on 2026-11-13 in the emulator,
this release's binaries proven in each guest: on XP x86 a hibernation and resume (the nearest reading to standby
there) re-armed the interrupt and the copies compared equal, and with three controllers `XhciMsiShare` 0 left the
second and third refusing to start for want of a vector, as designed, and 3 had all three share one; on XP x64 a
hibernation and resume passed, and two controllers at share 0 and 2 behaved as designed. Not run: two controllers on
two IRQs (the test machine has one free ISA IRQ), no local APIC (no such XP image); the owner closed the task on
2026-11-13 with those two named as not run. After the close, sharing on by default (the decisions table, "Phase 49,
mode 4's sharing on by default"; record 15 section 4.5.19) was built and merged the same day (merge `d5d069c78`) and
read on 32-bit XP with nine controllers and three free IRQs (eight on one IRQ and the ninth on a second, a stick on
each clean, a disable and enable rejoining, a hibernation keeping the claims; at 1 and a stored 0 three started and
six refused cleanly) and with sixteen controllers (at the default 8, eight on IRQ 7 and eight on IRQ 6, IRQ 3
untouched, all sixteen running; with `XhciMsiShare` 16 all sixteen on IRQ 7, a stick copy on each 16 of 16 clean, the
power-off in 9 s). Not covered: 2000 and XP x64 with more than eight on one IRQ, more than sixteen controllers,
standby (only hibernation, on XP), real hardware, the USB4 host beside mode-4 controllers in a guest, a user's own
per-controller value surviving an update (only the INF's 8 and the machine-wide value were read).

## 49.16 (the remaining legs) The staged path's legs on XP, 2000, Vista, 7 and ME, and a ME start that does not finish (2026-11-12)

Development host A, no code change. Binaries: the gated build of `3.5.0.0` at `7e82e5a55` (x86 and amd64 `all`,
"BUILD + GATES PASSED (release qemu)"; x86 `xhci98.sys` release `756941a2...`, qemu flavour `4aab0b9c...`). The plan
was reviewed and revised before any leg. Guests one at a time; host processor often 73 to
100 % with 5 to 13 QEMU processes, nothing in these legs timing-bound. The legs read record 17 section 4.13.9's lists
with 49.29's instrumentation (`XhciQemuStageFault`, the per-request log, checked whole by a log checker: no request
completed twice, none damaged).

**A1: the command stranded at `XhciDmaStaging` 0, its own leg (XP SP3, the HP Thunderbolt Dock G2 model) - pass.** A
first attempt was void (the wizard keystrokes lost their place; `cdcnet` never bound). Then, the wizards walked by
screenshot: XP with `3.4.3.0` and the Thunderbolt support, the package of the tip installed over it, usb-wdm-free's
`cdcnet` staged with a tracing build over it, the qemu flavour over every copy of `xhci98.sys`, staging 0 as
installed; the dock's controller ran the qemu flavour at 0; ping 6 of 6. The dock removed by `device_del`. In order:
`cdcnet`'s halt; its first ABORT_PIPE and a timed-out request cancelled; the command timed out, its abort not
written (CRCR all ones), a controller reset requested, delivery not proven suppressed; the cancelled request completed;
**a second ABORT_PIPE queued while the controller was failed, completed once with USBD_STATUS_DEV_NOT_RESPONDING**; a
third abort after the containment answered at dispatch with success; then `cdcnet`'s quiesce waiting for its five
outstanding transfers to the end (expected). The report: "CONTAINED: USBSTS read all ones", "containment PINNED: no proof
DMA stopped; buffer, transfers kept", `cfg.stranded=00000001`; 10,587 log lines, five requests pending at the end (the
five `cdcnet` counts), errors 0; no DMA memory of the dock's controller freed. Pass on every clause: the queued abort
completed exactly once while the transfers stayed held; as designed, the removal never finishes at 0.

**A1b: the same order at `XhciDmaStaging` 1 on the dock's controller (XP) - removed, XP restarts; the abort in the
window not met.** After the removal: `cdcnet`'s aborts came after the drop, the held requests released, 0 of 5 transfers
outstanding, the surprise removal to every object of the dock's controller, "DMA NOT PROVEN STOPPED ... failing
closed", the allocations made 2Eh, freed 0, kept 2Eh, left 0, then the removals; 17,552 log lines, none pending, errors
0; `shutdown -r` reached the desktop again with the ledger at 0 at the next start. This repeats 49.25's X8 on
`3.5.0.0`'s qemu flavour. Not met: an abort queued while the controller was failed and before the containment (here
`cdcnet`'s first request after the failure was a GET_STATUS on the ring, cancelled).

**A2 and A3: the five held states and the bytes kept - pass on 2000, Vista x86 and 7 x86.** A USB tablet and a
Bulk-Only stick at power-on, staging 1 and the state's hold machine-wide, the controller restarted (on 2000 by a test
aid of this task's that sends the property change `devcon restart` sends, never tracked or shipped), a copy loop to the
stick.

| System | Each of the five states (WAITING, FILLING, READY, DRAINING, COPY_PENDING) | At each next stop | After a restart of Windows |
|---|---|---|---|
| 2000 SP4 (Standard PC HAL, one vCPU) | taken, armed `DEAD_NOPROOF`, let go by the closure; the device PDOs removed ("START refused, device gone") | allocations made = freed + kept, left 0; 26h or 27h kept (about 2.5 MiB), the ledger rising to C80000h; detached copies 0; requests in = out | the ledger 0 / 0 |
| Vista Business SP2 x86 (2 vCPUs) | the same, four PDO removals each | the same, the ledger rising to EFA000h before the last | 0 / 0 |
| 7 Professional SP1 x86 (2 vCPUs) | the same | the same, the ledger rising to C78000h | - |

No hold was let go by its bound. As on XP and 98 SE (49.29), the record held was the first to reach the state - a
device's own start requests - so the copy loop never reached the stick; a copy in flight at the hold is not what this
shows. On 2000 the installer stopped with exit 4 until the base's pending wizards were walked; on Vista an Explorer
window opened by AutoPlay on the stick vetoes the controller's restart (AutoPlay turned off).

**ME: a containment during the start, while ME has the child stopped for its start-up rebalance - ME never finishes
starting (found, placed, not fixed).** ME, staging 1 machine-wide, a tablet at power-on, the hold WAITING with the
containment armed (`XhciQemuStageFault` 0x0001000C). Twice, within about 10 s of the launch: the tablet's PDO START,
its first request held, armed, completed once (DEV_NOT_RESPONDING), let go by the closure; then **the child's STOP**,
the controller's and the root hub's relations queries - and nothing more: no REMOVE ever reached the tablet's PDO. The
screen stayed at SeaBIOS's "Booting from Hard Disk..." for 9 minutes; the registers (as data) show a spin in the VMM's
range. After each forced stop ME started next in Safe Mode, where `xhci98.sys` is not loaded.

| Run | Fault word | Device at power-on | After the hold | Outcome |
|---|---|---|---|---|
| ME control | 0x00010008 (the hold, no containment) | tablet | let go by its 10 s bound; the same STOP, the child still in the root hub's relations | desktop |
| ME, the hold armed after the desktop | 0x0001000C | none; the stick hot-plugged at the desktop | STOP, relations, REMOVE | ME alive; Shut Down finishes |
| 98 SE | 0x0001000C | tablet | the hold, the closure, the relations, REMOVE; no STOP before | desktop |

Placement (from the request order alone; no closed code read): the hang needs the containment's drop (`hcdDropAll`) to
take the child out of the bus relations while ME holds it stopped for its rebalance; with the child still listed, or
with no stop before the drop, the REMOVE comes. Every request of the hung run that reached this driver is answered at
once in the code, so the wait is read as ME's own (an inference). At `XhciDmaStaging` 0 an unproven containment only
drains and the child stays listed, so this order needs 1, or a proof at the start, which none of the 9x routes gives
easily. A known limitation of `3.5.0.0` (staging stays off by default); the change - keep such a child listed until
its STOP has ended and refuse its next START - is a candidate for the next phase (record 17 section 4.13.9).

**A2 on ME with the hold armed after the desktop - pass; A3 on ME not readable.** States 1 to 5 each taken, armed, let go
by the closure, the stick's STOP and REMOVE, ME alive, every following Shut Down finished; the log checker found none
pending and errors 0. A3 cannot be read on ME this way: a 9x Shut Down takes the controller to D3 and never stops it,
so no allocation line is printed, and there is no devcon on 9x.

**A7: the request a client sends as it is removed - ME with audio playing and a stick: pass; 98 SE audio not
makeable.** ME's own audio driver playing the emulator's `usb-audio` (13,022 isochronous requests logged), three copy
loops to the stick, then an injected failure (`DEAD_NOPROOF` until cleared): ME's clients sent a SELECT_CONFIGURATION
from two PDOs before their REMOVE (one completed CANCELLED, one success), then the removals; ME stayed up ("Unsafe
Removal of Device"); none pending, errors 0. So on ME the select before the REMOVE is not held on a contained
controller at 1 in this order; the liveness risk of section 4.13.9's fourth row did not show. Not read: a select
arriving after the containment was in place. On 98 SE the first playback stopped Windows with "A fatal exception 00
has occurred at 0028:FF0C2ED4 in VXD USBAUDIO(01) + 00002ED4" before any injection: the known Windows 98 SE finding
with this emulated device (`run-13e.md`, `run-22.md`, `run-36.md`; the same offset under Windows 98 SE's own UHCI
stack), not attributed to this build.

**XP, one vCPU: A4, A6, A5 and map failures.** The tablet, the stick, a `usb-uas` disk (streams granted) and
`usb-audio` at power-on, staging 1; 41,826 log lines, none damaged, none pending, errors 0; detached copies 0 at all 16
stops.

- **A4, the staging DPC (state 6): not reached.** With `MaximumTransferLength` 2 MiB for both disks and both copied at
  once (24 MiB each, compared equal), no hold was taken in 240 s: the copies gave no admission pass of 1 MiB. Still
  owed; it needs a client that queues 1 MiB in one pass, or a pool short enough that a record waits.
- **A6, the injection with everything busy: pass.** Three copy loops (stick and UAS disk) and audio playing, then the
  injection: the relations, five surprise removals and five removals (tablet, stick, UAS disk, the audio functions),
  the held requests released, the report "CONTAINED" and "containment PINNED", Windows went on; at the later stop the
  allocations made 45h, freed 9, kept 3Ch, left 0; requests in = out.
- **A5, a cancel racing each held state** (no containment; the device removed by `device_del` within 50 ms of the
  hold): no ABORT_PIPE was logged in any race (XP's HID and storage clients cancelled their requests instead, which the
  instrumentation does not log as a call), so by the plan's rule the abort race is unproven; the cancel race is read
  from the completions: in WAITING and COPY_PENDING the cancel acted while held, in FILLING, READY and DRAINING the
  request was served after the release, as designed; in every race no fault, the device found again when plugged
  back, each request completed exactly once.
- **Map failures: pass.** On NT the window route serves MDLs with no system address (`stage: window maps=00000051`, the
  copy equal); with every window failing (bits 15:8 = FF) 28 requests ended C000009A at the give-up, the copy failed,
  nothing faulted, the next stop's log clean.

**A8: the idle request (XP) - ordinary pass; the callback held at the stop not reached; 2000 not run.** With
`XhciSelectiveSuspend` 1 and a mouse bound to the project's selective-suspend test client (`test\pmclient`, never
packaged): the idle request submitted and its callback returning, then an injection: surprise removal and removal of
the client's PDO, the devnode gone, `IdleCalls 1`, `IdleReturns 1`, no quarantine. The deliberate attempt (the callback
asking D2 and waiting) had returned before the injection fired, so a callback still running at the stop was not caught
(the client has no latch to hold it); still owed. 2000 not run (WDK 7.1's devcon does not run there).

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** read on 2026-11-13: on Windows Vista
x86 with `XhciDmaStaging` 1 (not the default), after a controller stops answering while audio plays and copies run,
Windows' removal reaches only one or two of the devices and then stops, the audio function's isochronous requests stay
held, opens of the removed volumes hang and a forced shutdown does not finish (2 of 2); Windows 2000 and Windows 7
pass the same leg (7: the full removal, a forced shutdown in 79 s; 2000: the full removal with everything busy, and
the idle-request leg an ordinary pass). At the shipped default (0) on Vista nothing is removed and a forced shutdown
did not finish in about 17 minutes, the designed behaviour where containment is not proven. At the owner's request,
before that limitation's wording was touched, whether a restart finishes at the default was read on 2000 SP4, XP SP3,
Vista SP2 x86 and 7 SP1 x86 with the failure injected: neither a normal nor a forced shutdown finished (8 of 8 timed
runs, held 10 and 20 minutes), the devices stayed listed and reading the drives did not hang, and only a power-off
ended it; with audio playing the shutdown waits on the audio application, without it it stops after the shutdown's
power query with one request to the USB disk held (record 17 section 4.13.9). Owner, 2026-11-13 ("Say power button"):
the user documents no longer say to restart the machine but not to disable the controller, to save the work, and to
switch the computer off at the power button if Windows does not finish; a wording change only. Still not reached in
the emulator: the staging DPC's state 6, the abort race, the idle callback held at the stop, the 1 MiB pass. Closed
2026-11-13 by the owner for what it delivered; the Vista finding moved to VISTASTAGE.1. The ME start that does not
finish became MEBOOTDROP.1, whose change keeps a dropped child in the relations while its device object is stopped,
refuses its next start ("device gone") and asks for the relations again so that the next answer omits it; its limits:
the device object and its held requests then live until ME's next query, a refused start during ME's start-up is
unread, and the answer lists a device the bus has dropped, which must be neither enumerated nor served; legs on ME at
the start and after the desktop, and on 98 SE.

## 49.18 (the remaining legs) The SHPC learning on XP x64, 7 x64, 8 x64 and 8.1 x64; the ASM4242 model on 10 x86 (2026-11-12 and 13)

The same host and binaries as 49.16's remaining legs. The x64 packages as a throwaway-signed copy inside the guest.
Vehicle: the i440FX SHPC bridge with a generic USB4 host model that names no port (`host-model=intel-adl`:
`u4.map.none=FFFFFFF5`, the case the learning serves), TCG, one vCPU.

| System | At a cold start, nothing attached | The plain enclosure plugged after the start | At power-on, no pre-boot connection manager | At power-on, the tunnel up before the start |
|---|---|---|---|---|
| XP x64 SP2 | `u4.learn.shpc=00000001` | first plug: Windows restarted the NHI during Stor98's install, the learning expired, the eject kept the notice; a plug after a cold start with the driver present: learned (`u4.learn=00000103`), "The PCIe tunnel was closed." | learned, "its PCIe connection reaches the port at PCI 00:05.0", closed | the notice ("The PCIe tunnel stays up until the device is unplugged") |
| 7 x64 SP1 | the same | learned, closed; plugged again: known, closed again | learned, closed | the notice |
| 8 x64 | the same | first plug: the 60 s window ran out on a slow guest (its screen 1 min 42 s late), the notice; second plug: learned, closed, Stor98 (not `stornvme`) had the drive | - | the notice |
| 8.1 x64 | the same | learned, closed; plugged again: known, closed again | - | not finished (stopped at its boot at the session's end) |

A first XP x64 run without the generic host model read the minimal host naming the bridge (nothing to learn) and was
discarded. **10 x86 on q35 with the ASM4242 model:** the guest was a copy of an earlier 10 x86 install with this kit's
`xhcicm.sys`, `xhci98.sys` and `xhcihlpr.sys` copied over it (a fresh install stopped at "xhcicmf.inf could not be
staged (0xE000022F)"). The check of the copy, read again later by the drivers' hashes, showed that the guest ran an
earlier build of this phase, not this release's drivers: **the two readings below are not proven for `3.5.0.0` and
are to be repeated.** B1 (the ASM2464 enclosure at power-on): the eject by id, exit 0, "Windows has stopped PCI
Express Upstream Switch Port." with the notice, as expected. B2 (a cold start with nothing attached, the TB3 dock model
hot-plugged): **learned**, then before the eject the controller's instance stopped and started again (one tunnel handed
on, the earlier instance's record matched, the learning expired), and the eject gave the notice rather than "closed".
Per record 22 section 7.6 bindings, named or learned, live for the instance's life, so the eject's text is right for
that state; what stopped the instance is inferred (Windows 10 placing the dock's resources), no PnP trace was taken; 7
x86 and XP on the same model closed it with no restart. Carrying a learned binding across a restart is a candidate for
the next phase, its observation unconfirmed on the release until it is read again on this release's drivers.

**Not read:** 8 x86, 8.1 x86 and 10 x86 with this release's package (the package must be signed to install there);
10 x64; Vista x64 (its pending wizards, the installer's exit 4). **Still to be read:** B5, the Windows ME plain-enclosure
leg (stopped at its first boot at the hand-back), and B3, the XP refresh leg (the SHPC refresh; scripts ready).

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** read on 2026-11-13: B3, the SHPC
refresh leg, on 32-bit XP: after the filter came back and the bridge restarted, the learned state was refreshed before
any plug, the plug was learned and the eject closed the tunnel. Then on x86 with this release's binaries proven in
each guest (installed by the boot with signature enforcement off on 8, 8.1 and 10): B1 (an enclosure present at
power-on: stopped, the notice saying the tunnel stays up until the device is unplugged) passes on 10, 8.1 and 8; B2 (a
dock hot-plugged after a cold start: learned, the eject closing the tunnel) passes on 8.1 and 8 and in 2 of 3 runs on
10; in the third run on 10 Windows stopped and restarted the controller after the dock's plug before anything was
learned, and the eject then left the tunnel up with the notice (the documented notice path), so a learned binding lost
at such a restart was not reproduced (TUNBINDCARRY.1). The bench reading was dropped by the owner on 2026-11-13; on
2026-11-14 the owner dropped the ME leg (B5), still unread, and the task was closed.

## 49.27 The Thunderbolt and USB4 host's ring interrupt by a message on 98 SE, ME, 2000, XP and XP x64 (2026-11-12 and 13)

Development host A. Design record 21 sections 4.5.12 to 4.5.18. The
library by version, date read and SHA-256 alone: the MSI stability improvements' version 1 (read 2026-11-12; this tree's
header, `d5b4f45d7dda00eea11e7d85af80b12292c6e87db31686e379b6a1de986a5ac0`).

**The design and the owner's answers (2026-11-13).** The design and its NT extension were each reviewed and revised. The value is `XhciCmRingInterrupt` `2`; after a failed message the ring is polled, never the line; the message is
built for 2000, XP and XP x64 through the MSI stability improvements; where the library says the message is in use or its
configuration cannot be reached, or the handoff check finds an Enable set, every cause is masked, proven by read-back,
and the ring polled; on 2000 and XP the host yields to a machine-wide `XhciInterruptMode` 4.

**Commits.** The design; the NT part (`cm_rirqnt.c` through the MSI stability improvements, the yield to a
machine-wide mode 4); the message client following mode
4's re-arm rule; the USB Tree's "Ring interrupt:" line (record 21 section 4.5.17, `Rirq.Why`, `CM_Q_RM_IRQ`; a connect
that cannot reach the configuration shown as "could not be connected", not "in use"); a lost interrupt noting the data
ring's latch (`cm.net.irq.latched`, 49.7's finding); the snapshot's field tables regenerated after 49.37; and, after the
merge review, the routine's hardware gate (section 4.5.18: `Rirq.HwGate`, closed at every D3 and stop order down, at
the detached shutdown and at the revocation, opened on the thread alone in D0; `test_cm_rirq`'s `test_gate`, 440
checks). Each was reviewed; four commits were rewritten before any push to take a library's commit id out of a
message. Every gated pair of the branch passed (x86 and amd64, the tools' self-tests among them), the last on the tip
that was merged.

**Legs** (the emulator; the Alpine Ridge NHI model with the host's MSI of one vector; a fresh overlay of each base):

| System | What was read | Verdict |
|---|---|---|
| 98 SE and ME | The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13). | - |
| XP SP3 x86 (through the MSI stability improvements) | value 0 "polled - set to 0"; value 2 "on, by message (IRQ 3)" (ISA IRQ 3 claimed, vector B2h, self-test under 1 ms); the enclosure, Stor98, a restart, copies compared equal, unplug and replug; mode 4 machine-wide: "on, by the line (2 works as 1 here)"; mode 0 with value 1: "on, by the line"; power-off | pass |
| 2000 SP4 (the multiprocessor APIC HAL, no enclosure on that machine) | value 2 "on, by message (IRQ 3)" (vector 51h); value 1 "on, by the line"; the yield to mode 4; value 0 polled; power-offs in 3 s | pass |
| XP x64 SP2 (a throwaway-signed copy inside the guest, catalogs only, no driver file changed) | value 0 polled; value 2 "on, by message (IRQ 3)" (vector B2h, self-test 16 ms); the enclosure (Stor98 after the installer ran again), copies compared equal, unplug and replug; the yield to mode 4; value 1 the line; power-off | pass |

The first starts after each install were clean. One XP leg was restarted from a fresh overlay after keys typed into the
wrong windows (harness).

**Not run.** The lost-message leg (staged, not run, by the owner's priorities); standby on any system (not tried), so
the re-arm at D0 is not read in a guest; no fault injection makes both of the
hardware gate's disconnects fail, so the gate has no guest leg.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** the 98 SE and ME part, moved to
RIRQ9X.1 on 2026-11-13: on the owner's board Windows 98 SE stopped during start-up with `XhciCmRingInterrupt` `2`
switched on (the start log's last entries are the USB4 router started and the Thunderbolt networking child loading);
under light traffic the driver does not log that messages went missing, because the periodic look drains the work
first; standby unread. Its code left the tree on 2026-11-13 with the 98 SE and ME message path (`cm_rirq9x.c` and the
separate component's vendored header removed; on 98 SE and ME a 2 is polled, and Setup and the USB Tree offer 2 on
2000 and later alone), so taking the item up starts from the history; the same holds for the controllers' mode 4 on 98
SE and ME (49.24), where a stored 4 runs as 0.

## 49.38 The xHCI98 USB Tree's settings page covers every registry value; the 98 SE and ME eject guard (2026-11-12 and 13)

The owner, 2026-11-13: "go through xhci98 USB tree to make sure it's advanced settings cover all the registry
entries"; his answers of 2026-11-12: Setup grows too (one table), mode 4 shown, a UAS drive's two values on the page, a
text row for `XhciCmTunnelBridge`, the uninstall removes every value; the leave-out list accepted otherwise.

**The audit.** Every value the release builds read, from the code's value definitions (`hcd_ctl.c`, `xhci_quirk.h`,
`hcd_msi86.c`, `cm_reg.c`, `uas_fdo.c`, the networking miniports, `pfw.c`, `xhcimark.c`), the INFs' lines and
`tuning.md`, against the Tree's rows, Setup's table and `XHCISET.TXT`. Found: 49 values read and on no page; the Tree
read an `XhciInterruptMode` 4 as "the driver uses its default, 0"; `Restart` and `LogBytes` (the mark service's) had no
`tuning.md` row. Left out (accepted): the two log levels (their dialog), `XhciCmNet` (Setup's networking box),
`XhciNetMtu` and NDIS's three (the adapter's own page), every qemu-flavour value, what the drivers write for the tools,
Stor98's test keys, Windows' `MaximumTransferLength`.

**The design and the code** (each reviewed and revised): one table both tools read, grouped; a drive row's write path
(the key opened with the process's rights; on 2000 and later, where that is refused, the drive's existing Enum key opened
and its `Device Parameters` beneath it under the backup and restore privileges, given back on every exit, the owner and
the permissions never changed; `Enum\<id>\Device Parameters` on NT, `Enum\<id>` on 9x); the text row's parser-exact
check; Find (the owner, 2026-11-12: the Settings window's "Find:" box filters the rows by name, meaning and choice texts,
without case, listing the advanced rows too while a search is on and saying so in the head line; a staged change the
search hides is still applied and counted; `XHCITR /set /show <text>` and `XHCIINST /set /show [text]` filter the same
way; no new import, the dialog's size unchanged); mode 4 shown as the hidden test feature (its sentences said only where
4 is offered or asked about); `XhciResumeKeepBot` as a row written by Setup and seeded only where no controller's own key
holds it; a UAS drive replugged while Settings is open (the drives compared before every Apply, a write only to a drive
present with the same driver key: "The controllers or UAS drives have changed since this window opened. Close it and
open it again; nothing was changed."); the event ring's row back to `XhciEventRingTrbs` with the owner's final texts,
read as the driver reads it; after the second merge review, a drive's `Device Parameters` never made, a drive that left
after the write said so, the elevated run refusing a mismatched driver key, a zero-length `REG_NONE` absent for every row.
Host suites: `test_trset` and `test_inst`, 0 failures at every step. Every gated build passed, the final tips with every
tool's self-test.

**The eject guard on 98 SE and ME** (the owner, 2026-11-12, after 49.17's bench; record 24 section 7.6): no Thunderbolt
or USB4 stop under a drive the BIOS holds. The design note was reviewed; the rule matches Stor98's report by its memory
range, not by bus, device and function, which 98 does not give for a function behind a bridge; the code reviewed and revised
(the report's range rather than the port slot's bars, no cap on the reports, a memory list that would overflow
refusing, sibling units behind the same tunnel port covered). After a later review: an NVMe function at Code 10 with no
Stor98 report of its own is "cannot tell", and refused too. Residual (record 24 section 7.6): a stale "taken" report
from an earlier boot cannot be told from a current one without a change to Stor98.

**Legs** (q35 bases, a `usb-uas` drive on port 4):

- **XP** (2 vCPUs): Setup `/set` of a controller value and the text row, both written machine-wide; `XHCITR /set` of
  five values of each kind (a per-controller row on every controller, a connection manager row, the text row, the drive
  row in the drive's `Device Parameters`), each read back in `/set /show` and in the report; the defaults restored
  every one; the Settings window's groups and range box; Find ("ring" listing only the matching rows, advanced ones
  too; "ringzzz": No setting matches "ringzzz"; the mode 4 sentences only with "Show advanced settings"); the drive
  detached while Settings was open and Apply refused with the text above.
- **98 SE** (1 vCPU): Setup's `/set` machine-wide; `XHCITR /set` read back through registry exports (both controllers'
  keys and the drive's Enum key); `/set /show` with a search and with one that matches nothing; `XHCIINST /set /show`
  with a search; the defaults restored. (A Win32 window program does not wait in COMMAND.COM and a typed line stops at
  127 characters: the commands ran from a DOS prompt with `start /w`, in short lines.)
- **2000** (1 vCPU): the same five values and the ring row set and read back; then the drive's key given the
  permissions a 2000 Enum key has, so a plain open for writing is refused, and `XHCITR /set XhciUasNoAtaPassThrough=1`
  and `=default` each written and read back through the privileged route, the key's whole security descriptor byte for
  byte the same before and after, five dumps. Seen on the way: with `XhciStreamsOff` 2 on every controller and a
  restart, QEMU's `usb-uas` drive was no longer shown on its port ("no device connected") until the defaults were back:
  a UAS-only device with streams off is held for a USB 2.0 reconnection it never makes (a time-out for that hold is a
  candidate for the next phase).
- **98 SE, the eject guard** (the ASM4242 model, the ASM2464 enclosure with NVMe at power-on): SeaBIOS does not enable
  the NVMe controller, so Stor98 takes it and the emulator cannot give the real BIOS-held case. No false refusal: with a
  hand-written report for another range marked "left: already set up" (the drive started, not at Code 10), the eject by
  id answered "Windows has stopped QEMU NVMe Ctrl (E:). It is now safe to disconnect the device." (with the kit's
  XHCITR of before the guard's last code changes). **At this leg the guard had host vectors only:** neither its
  refusal (Code 10 with a matching report) nor its "cannot tell" case (Code 10 with no report of its own, added after
  this leg) could be staged in a guest; host vectors cover them with the memory ranges of 49.17's bench, and the
  owner's board is the real check.
- **ME and 98 SE, the eject guard's first guest readings** (2026-11-13, the release tip's x86 binaries, in the
  emulator): on both systems a hot-plugged tunnelled NVMe enclosure was written, compared clean and stopped, from the
  USB Tree and from the console installer, both with exit code 0. On ME a drive the BIOS holds (StorFind "left:
  already set up; result 0", Stor98 at Code 10) was refused by the console installer with the accepted text and exit
  code 1. The USB Tree refused it too (exit code 1), but with the "not started - problem code 10" text, because that
  check runs before the guard; a change of that order is in work. Not shown: that the BIOS-held drive stays usable
  after the refusal (the emulator's BIOS failed reads before any eject), the tray's Safely Remove, and the BIOS-held
  case on 98 SE.

Not covered by a leg: XP x64.

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** before the task about fifty
documented values were not on the page, left out by design (record 25 section 22 item 3: the page was Setup's Advanced
list), a rule the owner's request changed. The owner decided on 2026-11-13 to take his own reading of the eject guard
on his hardware before the cut on the final package; it is a cut gate under 49.final, and the row stays ticked on that
decision. (Later: 49.final was closed by the owner on 2026-11-14; his reading of the guard is not recorded in this
sheet.)

## 49.26 (the change) XHCIMARK's reads kept per device, the installer's hold, one Setup run at a time (2026-11-11 and 12)

The owner's decision of 2026-11-11, both halves: (a) XHCIMARK reads a device's bus number and address once per device
instance and never for a device that is not started; (b) the installer holds the service's poll off while it updates
devices. The starting point is 49.26's reading above: STOP 0x8E in `ACPI.sys` completing an IRP_MN_QUERY_CAPABILITIES
that `services.exe` sent to the NHI's stack between its removal and its rebuild (4 of 8 runs on two vCPUs with the
service running; none on one vCPU or with it stopped), and the service's poll sending about 24.5 such requests a second.

**The change.** Design record 21 section 7.8 (the reading, which reads ask the stack, the location kept per instance,
the hold, the pure half, the legs), with sentences in records 21 section 7.4, 26 section 6.2, 31 section 6.2 and 33
section 4.3; the code in `xhcimark` (`mk_loc_*`, `mk_hold_step` in `mark_core.c` with their self-test cases; the walk,
the fresh checks, the bridge judgement and the hold in `xhcimark.c`) and in `xhciinst` (`inst_mk_hold_how`,
`mk_hold_begin` and `mk_hold_end` around the run's and the uninstall's work), host-tested (`test_mark` 231 checks,
`test_inst` 9110). The plan, the design and the code were each reviewed and revised. After the merge review: the idle
event `Global\Xhci98MarkIdle` replacing the poll mutex, both named objects SYSTEM and Administrators only and trusted by
their owner, a restart marker `Parameters\SetupStopped` for a run killed while the service is stopped; each run's hold
told apart (`HoldRun`, `HoldAck`); one Setup run at a time, the guard taken at the run's first read (the release notes'
"Cannot continue: another Setup run is in progress" line). The gated pair of the change passed.

**A second run with the same package rebuilds no stack** (every device "Keep (same version)"), so the reproduction's
rebuild needs "same version, other bytes": before the update another build's `xhcicm.sys` of the same version is
copied over the installed one (compared) and Windows cold-started.

**The query rate** (a probe build of `xhcicm.sys` counting every PnP request at the tunnel's filters and the NHI; one
vCPU, no dock):

| Guest | Service | The boot and its start | 60 s steady | 60 s stopped | The start + 30 s | 60 s steady |
|---|---|---|---|---|---|---|
| XP | unchanged | 5889 (5658 capability) | 1645 (all capability) | 0 | 1094 | 1596 |
| XP | this build | 464 (233 capability) | 0 | 0 | 35 | 0 |
| 2000 | unchanged | 7207 (6955 capability) | 1470 | 0 | 1050 | 1505 |
| 2000 | this build | 487 (235 capability) | 0 | 0 | 35 | 0 |

So on XP and 2000 the service's remaining reads send no PnP request to the tunnel's devices at a steady poll; the
address read was the only one, and it now comes once per device at the service's start.

**The reproduction series** (the update run with `/thunderbolt /tbnet /wdmfree:stage=net/cdcnet`; a run counts only with
"Exit code 3." and the TB3 controller updated in its own console):

| System, `xhcicm.sys` before the update | Arm | Valid runs | STOP 0x8E |
|---|---|---|---|
| 2000 SP4 (the ACPI HAL, one vCPU), the published `3.4.3.0` release | this build | 20 | 0 |
| XP, two vCPUs, the published `3.4.3.0` release | unchanged | 10 | 0 |
| XP, two vCPUs, the published `3.4.3.0` release | this build | 20 | 0 |
| XP, two vCPUs, the qemu flavour of the earlier reading | unchanged | 8 | **2** |
| XP, two vCPUs, the qemu flavour | this build | 8 | 0 |

Every changed run's installer log shows the hold made and its barrier passed, and the service logs no read during it.
Both faults: "*** STOP: 0x0000008E (0xC0000005,0xF7806BAE,...)" in `ACPI.sys` at the earlier reading's address; one read
over the emulator's monitor as data only: the faulting thread in `services.exe`, entered by a system call, no address of
this project's images on its stack, the request IRP_MN_QUERY_CAPABILITIES to a device object of `ACPI.sys` whose device
extension is 0, as before. What it shows: with the release flavour the fault did not come in either arm on this host (0
of 10 unchanged), so those 20 runs compare nothing; with the qemu flavour it came with the unchanged service (2 of 8)
and not with the change (0 of 8), weak by itself (0.23 under the exact test). Taken with the query counts, the hold and
the fault's reading, the service's address read is the likely trigger by every reading here and the change removes it
during the update; that the service is the reader is not shown directly, and the race itself is inside Windows by
inference. 2000 was read on one processor only (the two-processor 2000 image runs on a machine with no PCIe root
complex, where the Thunderbolt host model cannot attach).

**The update from the earlier service.** The installer finds a service with no hold ("hold 1": no idle event), stops it
before updating the TB3 controller, and the new service starts held and ends the hold a second later.

**The mark after the change.** A first plug of the TB3 enclosure on XP: the earlier service wrote its mark at once,
while the drive still waited for its Found New Hardware wizard; this build writes none until the drive has started (its
location is not read before), then marks it once the wizard is answered (83 s after the plug). On XP the earlier
service's early mark did not survive the first install anyway: XP's install of the drive deleted the `LowerFilters`
value and the service wrote it back after the drive started, at about the same point. 7 x86 (which installs the drive
with no wizard) marked it 18 s after the plug. An enclosure plugged during an installer run is marked at the first poll
after the hold ends: late, not never.

**The merge-review fixes' legs** (XP, one vCPU, an earlier service and a squatter program holding the idle event with
its own security): an idle event not owned by SYSTEM is not taken for the service's (the run stops the service, sets
the marker, starts it again and clears the marker); a fresh service's idle event was at first not SYSTEM-owned (a
defect, fixed and read again); a reset of the event cannot make a false pass; a run killed while the service was
stopped leaves the marker, and the next run restarts the service and clears it; two runs back to back each see their
own hold acknowledged; two runs started together: the first runs, the second ends "Cannot continue: another Setup run
is in progress:" with exit 1 and nothing changed; the same with the guard taken at the first read.

**The import list** (2026-11-12): the installer images imported `OpenEventA`, which `xhciinst-imports.allow` did not
list; the four rows added, and a gated pair with the tools built first passed with every tool self-test "passed" and
none absent; the installer completes on 98 SE (window and console) and on a 2000 update. **The gate** (2026-11-12): a
gated "all" build now builds every tool it self-tests (XHCIDI in a step of its own, so a missing Open Watcom fails it),
and a self-test whose EXE is missing fails; the pair from a fresh tree with no tool built passed. **The integration
gated pair** of `3.5.0.0` at `763b38506` from a fresh tree passed on both architectures with every tool build and
self-test, and its unsigned x86 package was the laptop's development package (49.5's bench); a 98 SE guest installed it
with SETUP.EXE and started clean.

## 49.13 and 49.18 (the x64 legs left unread on 2026-11-13) Vista x64, 10 x64 and 8 x64 (2026-11-14)

Read on 2026-11-14 in QEMU guests of one virtual processor each, on the test-signed x64 package and tools of the tree
at `1c476d0b3` (version stamp `3.4.3.0`), every driver and tool image compared by hash with that build before the first
reading. The readings are of that build, not of the release tip.

**49.18, Vista x64** (the i440FX vehicle with the SHPC slot; `xhci98.sys` and `xhcicm.sys` running): an enclosure
plugged in after a cold start is learned and both ejects close the connection; one present at power-on with no pre-boot
connection manager is learned and its eject closes the connection; a tunnel up before the start is kept and the eject
keeps its notice (the tunnel stays up until the device is unplugged). The first try of the hot-plug leg learned the
enclosure and Windows then refused both ejects for the volume; with AutoPlay off it passed.

**49.18, 10 x64** (the drive on Windows' own NVMe driver, `xhcicm.sys` running): the same three cases with the same
results. The first try of the power-on leg learned the enclosure and Windows refused the eject (veto type 6, a handle
open on the volume); with AutoPlay off and three minutes of quiet after the logon it passed. In that refusal the
installer's line and its log carried stray characters after the volume's name, taken as a defect of the installer's
own to look into.

**49.18, 8.1 x64 at power-on** (read on 2026-11-13 on the same build and vehicle, a fresh install; not written down
here until now): the plain enclosure present at power-on with no pre-boot connection manager is learned ("its PCIe
connection reaches the port at PCI 00:05.0") and the eject closes the tunnel; with the tunnel up before the start it
is kept ("PCIe tunnel: up (found at start)") and the eject keeps its notice.

**49.13, the second idle leg** (10 x64, Windows' own NVMe driver, the Alpine Ridge model, standby with nothing
running): after the wake the drive letter and "PCIe tunnel: up" were there at 45 s and at 150 s, no unexpected removal
was counted, and the shutdown asked for in the guest finished. With the earlier three that is four legs of four on
this vehicle, two with a copy running and two idle, none hanging.

**49.13, 8 x64, S.2 at hibernation: not readable in these guests.** Both legs stopped during the hibernation itself
with bug check `0xA0` (first parameter `0x107`), and so did three controls on one virtual processor: this package
installed with no enclosure, the stock Windows 8 x64 image with nothing of this package, and that image with no USB4
model in the machine at all. Windows 8 x64 stops at every hibernation in this guest setup, whatever is installed;
whether two virtual processors avoid it was not read.


## 49.47 the Intel port switchover on by default, and tools\portsw\ removed (2026-11-14)

The owner's final design of 2026-11-14 (about 08:15) supersedes the default
below: both INFs write `XhciIntelPortSwitch` 0 again and a missing value is
off; Setup proposes 1 at a first install on a listed board. The readings
below are of the first design; the section "49.47 (Setup proposes the
switchover)" has those of the final one.

Branch `w49-portsw`, from `3.5.0.0` at
`23c1672fa`, `993ed4a48` merged first (fast-forward). Commits, each reviewed
and revised before it was made:

- `f47a4f4d4` the default: both INFs write `XhciIntelPortSwitch` 1 under the
  don't-overwrite flag; on a listed controller a value missing, of another
  type or unreadable was on (`XHCI_PSW_ABSENT_ROUTES`, `src\xhci_psw.c`),
  off again from the owner's second decision of 2026-11-14 (49.47's later
  commit: text-mode Setup keeps the ports where the firmware put them);
  Setup's default follows the INF; the USB Tree's row, `tuning.md`'s
  columns, the INF gate's row, self-tests and footprints and the host
  vectors with it.
- `c76c1d5e9` the removal: `src\portsw.vbs`, `portsw-on.bat` and
  `portsw-off.bat` deleted; `make-release.ps1` publishes none and refuses a
  `tools\portsw\` or a copy at the cut and in a release published from
  `3.5.0.0`, published releases keeping theirs (`portsw` among the retired
  tools); the checkers out of `package-common.ps1` and `check-text.ps1`; the
  packager's self-tests on stand-ins.
- `dca72fdce` the documents: README.md, the release notes, `tuning.md`,
  design record 16 revision 8, record 25, failure-diagnosis, build-and-test,
  source-files, xhci-programming, hardware-testing, the acceptance test and
  issue 27.
- `7e2d885d4` merge of `3.5.0.0` at `80d8fabe3` (49.44), no conflict.
- `9215777d4` merge of `3.5.0.0` at `3ebdeefc2` (run 49, roadmap, 49.45), no
  conflict.
- `09cbf4465` merge of `3.5.0.0` at `f9e698c81` (documents; the
  maker-to-manufacturer sweep), no conflict.

### Host checks

- At `f47a4f4d4`'s tree (the default): the host suite PASSED (test_psw 300
  checks, test_trset 7655, test_inst 9008, 0 failures); the INF gate PASSED
  on both INFs; its self-tests PASSED (1931 checks in six parts).
- At `c76c1d5e9`'s tree (the removal): the packager's self-tests PASSED
  (1945 checks in five parts); `check-text.ps1` OK.
- At `7e2d885d4`: the host suite PASSED (test_inst 9058). At `9215777d4`: the
  host suite PASSED. `check-text.ps1` OK at every commit.

### Gated pairs

- `7e2d885d4`: x86 `all` PASSED (15:06) and amd64 `all` PASSED, both
  `dirty=0` (`logs\pair1\gb-x86.log`, `gb-amd64.log`); `xhci98.sys` x86
  release `9AC6A3A6...`.
- `9215777d4`: x86 and amd64 PASSED, both `dirty=0` (`logs\pair2\`).
- `09cbf4465` (the final tip): x86 and amd64 PASSED, both `dirty=0`
  (`logs\pair3\`; `xhci98.sys` x86 `DB83205A...`, amd64 `DAACA93F...`).
  Before it, at its tree: the
  host suite PASSED and the packager's self-tests PASSED (1945 checks).

### Guest legs (development host A, QEMU q35, TCG, 1 vCPU, ports 49450-49454)

Kit: the gated x86 package of `7e2d885d4` (`out\pkg-release-x86`) in the
family layout, with this build's `XHCIINST.EXE`, `XHCIWIZ.EXE`,
`XHCIDI.EXE`, `SETUP.EXE`, `INSTALL.BAT` and `XHCITR.EXE`; `xhci98.sys`
`9ac6a3a64a47946a...` (the gated record's), `XHCITR.EXE` `3ffb6a99abd3e564...`,
`XHCIINST.EXE` `c4fdf946fbf93be1...`. The released `3.4.3.0` from
`releases\3.4.3.0` (`nt-x86\xhci98.sys` `3b35b4ae102266b5...`). The
emulator's controller is `1B36:000D`. Readings by the guests' batches over
COM1.

| Leg | System | Steps | Result |
|---|---|---|---|
| new install by Setup | 7 x86 (`w7n`) | `INSTALL /console /yes` | `XhciIntelPortSwitch` 0x1 in the controller's key; no machine-wide value; exit 3. PASS |
| new install by Setup | 98 SE (`w98n`) | `INSTALL /console /yes`; cold start; `regedit /e` | `"XhciIntelPortSwitch"=dword:00000001` in the controller's key; none in `Services\USB`; exit 3 ("installed, restart pending"). PASS |
| new install by hand | 7 x86 (`w7h`) | `pnputil -i -a nt-x86\xhci98.inf` | 0x1 in the controller's key; no machine-wide value; exit 0. PASS |
| update over 3.4.3.0 | 7 x86 (`w7u`) | 3.4.3.0 by Setup (0x0), cold start, this build by Setup | 0x0 kept, no machine-wide value written; exit 4 (the same-version reinstall's root-hub step: the tree still stamps 3.4.3.0). PASS |
| update over 3.4.3.0 | XP (`xpu`) | 3.4.3.0 by Setup (0x0, the root hub's Found New Hardware wizard answered by keys), cold start, this build by Setup | 0x0 kept, no machine-wide value; exit 3. PASS |
| the Tree on the emulator's controller | 7 x86 (`w7u`, `w7n`) after a restart | `XHCITR /set XhciIntelPortSwitch=1`; `/set /show XhciIntelPortSwitch`; `/snapshot` | refused: "XhciIntelPortSwitch=1: not a setting of this computer. Nothing was changed.", exit 1; `/show`: "XhciIntelPortSwitch: (Intel 7-9 series, C610 only); default 1 - on"; the snapshot's only `psw.` record `psw.gate=00000000`. PASS |

Seen, not this task's: `XHCITR /set XhciIntelPortSwitch=1` on a computer
without a listed controller prints the switch's restart warning ("At
Windows' next start the shared connectors ... move ...") before it refuses
the change.

Not read here (the owner's board): the route itself, a device moving,
standby and hibernation with the switch on, text-mode Setup with the
default on, the 7-series periodic limit; the x64 leg and the trial cut (left
for the final pass).


## 49.44 Setup takes over a controller another manufacturer's USB 3 driver runs (2026-11-13 and 14)

Branch `w49-takeover`, from `3.5.0.0` at `6b0105ca7`. The branch at `c458da01d` (the takeover and the give-back's
settle) was merged into `3.5.0.0` by `80d8fabe3`, and the branch at `2045a7c74` (the give-back's lines, controller
by controller) by `3d3bc8e16`. Design: record 23 section 5.4.2 (revisions 58 to 62), record 26 section 5.3.10.

The vehicles: QEMU q35 guests on development host A, each an overlay of its clean base, one virtual processor, TCG,
with two xHCI controllers, the emulator's `qemu-xhci` (`1B36:000D`) behind a root port and its `nec-usb-xhci`
(`1033:0194`) on the root bus (98 SE: `qemu-xhci` alone). Results by the guests' batches over COM1, with the
installer's own log and, where named, Windows' setup log.

Another manufacturer's driver is played by a stand-in made for the test, never tracked or packaged: an INF
(`tstxhc.inf`, SHA-256 `0c1cb3bd...4e2543a`), provider "Test Manufacturer", that binds both controllers' hardware IDs
to a service `tstxhc` whose image is the guest's own `null.sys`; it is not signed. It was put in place with
`pnputil -i -a` on Vista and 7, through Found New Hardware on 2000 and XP, and on 10 x86 by a forced
`UpdateDriverForPlugAndPlayDevices` (Windows' own signed `USBXHCI` otherwise outranks an unsigned hardware-ID match).

The kits: each the family layout of the x86 release package of a clean gated pair (`dirty=0`) at the commit named,
with Setup and the tools of the same run, the drivers' hashes as in the gated record:

- `c458da01d`: `SETUP.EXE` `a53c2bb2...`, `XHCIINST.EXE` `5663fa7f...`, `XHCIWIZ.EXE` `1591ab1a...`, `XHCITR.EXE`
  `1d64c802...`, x86 `xhci98.sys` `02ce0f3c...`, `xhcihlpr.sys` `4bb98b43...`.
- `d9bd3df29`: `xhci98.sys` `472B310E...`, `xhcihlpr.sys` `0474E106...`.
- `90f81a12f`: `xhci98.sys` `5CCE854E...`, `xhcihlpr.sys` `FA5E62B7...`. `2045a7c74` adds to it only merges of
  `3.5.0.0` (the host suite passed at each); no guest reading was taken on those merges.

**The first legs, before `c458da01d`** (the kit's drivers from a gated x86 build whose tree was not clean, so not a
counted build; the installer as named). 7 x86, the stand-in on both: off, both "Skip (other driver)", the hint, nothing
replaced; `/list /takeover`, both "Replace other driver (/takeover)" and the warning. The ticked run, on an installer
before `abecbed86`, replaced the `qemu-xhci` controller and then stopped with "Could not replace", because that
controller still showed Code 38 with its restart pending: a fault in the installer, fixed in `abecbed86` (a replaced
controller whose call asked for a restart waits for it). After a restart, on the installer of `abecbed86`: the first
controller started on `xhci98` and the second was replaced ("Replaced tstxhc with xHCI98 on ..."), its restart
pending, exit 3, the record holding `done;tstxhc;oem2.inf` for both; after the next restart the run started both
controllers and both root hubs, exit 0. A first XP leg is VOID: it was stopped before the stand-in was installed (its
Found New Hardware wizard driven by keys) and no reading was taken; the XP readings below are of a later overlay.

**The give-back read too early (7 x86, with the installer of `abecbed86`, before `c458da01d`).** The uninstall said
"has no driver now" for both controllers, and after the restart both ran `tstxhc` again. Windows' own
`setupapi.dev.log` of the uninstall: the installer's wait returned at 12:07:12.567 (`takeover: Windows' installs
waited for: 0x00000000`); Windows' installs of the two controllers began at 12:07:12.816 and 12:07:12.988 and ended
"Exit status: SUCCESS" with `tstxhc` by 12:07:22.800. `CMP_WaitNoPendingInstallEvents` had answered before Windows
queued the installs its re-scan started, so the lines read the controllers too early: a fault in the installer, not
in the stand-in or in Windows. Fixed in `c458da01d`: the give-back waits for record 23 section 5.8's settle over the
recorded controllers alone (Code 1 or 18 expected, 60 s at most; revision 60).

**On `c458da01d`.** "Off" is a run without `/takeover`, "on" one with it; each system's legs ran in this order: the
plan and the run off and on, a restart, the uninstall, a restart, a reading.

| System | Takeover | Uninstall | After its restart |
|---|---|---|---|
| 7 x86 SP1 (`w7b`) | off: both "Skip (other driver)" and the hint "2 USB 3 controllers run another manufacturer's driver: /takeover replaces it."; `/yes` exit 3, both still `tstxhc`, no record. `/list /takeover`: both "Replace other driver (/takeover)" and the warning. `/yes /takeover`: "Installing ... done, restart needed." twice, "Replaced tstxhc with xHCI98 on xHCI98 USB 3 Controller." twice, one controller at a time (the two calls in turn in `XHCIINST.LOG`), both on `xhci98`, record `done;tstxhc;oem2.inf` for both, exit 3. PASS | "xHCI98 USB 3 Controller (Red Hat QEMU xHCI): given back to tstxhc." and the same for "(NEC uPD720200)"; `takeover: Windows' installs settled after 16833 ms`; both on `tstxhc` at once; the record deleted. PASS | both `tstxhc`. PASS |
| Vista x86 SP2 (`vx`) | the stand-in's first try retried (the kit's drive mounted late). The first and second tries of the run both stopped at exit 4, "Windows is still installing something after a minute ... Nothing was changed.", with Found New Hardware open for the base's Unknown Device and SM Bus Controller; both still `tstxhc`; answered "Don't show this message again". Third try: as on 7, record `done;tstxhc;oem3.inf`. PASS | "given back to tstxhc." for both, settled after 16364 ms, both `tstxhc`, the record deleted. PASS | both `tstxhc`. PASS |
| XP SP3 (`xp2`) | the stand-in through both Found New Hardware wizards (Continue Anyway). XP's `reg` 3.0 refuses `/s /v`, so the batches' service snapshots are empty on XP and the installer's rows and results carry the drivers. Off: both "Skip (other driver)", results "tstxhc 1.0.0.0, started". On: "Installing ... done, restart needed." twice (XP's Hardware Installation prompt answered for each), results "xhci98 3.4.3.0, started" for both and "Replaced tstxhc with xHCI98 on xHCI98 USB 3 Controller (NEC uPD720200)." and "... (Red Hat QEMU xHCI)."; record `done;tstxhc;oem90.inf` for both. PASS. After the restart Found New Hardware for the two xHCI98 USB 3 Root Hubs, answered | both "... : not found yet; Windows sets it up again at the next restart."; log `takeover: Windows' installs not settled (3, wait 0x00000102)`: the 60 s ran out while XP waited at a Found New Hardware wizard for each returning controller (the stand-in is unsigned, so XP asks the user). Answering the two wizards put `tstxhc` back on both at once. The line is the designed one for a wait that ran out; the controller had been found, so the case got a line of its own (`d9bd3df29`, below) | `INSTALL /console /list`: both "driver tstxhc 1.0.0.0; Skip (other driver)". PASS |
| 10 x86 19045 (`w10`) | the stand-in forced on both (`ok=True error=0 reboot=True` each). With `/usb` on every run: the plans as on 7, with "/usb: the xHCI98 USB drivers are installed on Windows 10."; off: both still `tstxhc`; on: both replaced, both controllers and root hubs started at once, record `done;tstxhc;oem2.inf` for both. PASS. `/list /takeover` without `/usb` while the controllers ran `xhci98`: no refusal (the USB drivers already run there, so they are in the run); after the uninstall, with the controllers on Windows' own driver: "Cannot continue: /takeover: on Windows 8, 8.1 and 10 give /usb with it." "No changes were made.", exit 1. PASS | "given back to USBXHCI." for both, settled after 3484 ms, both `USBXHCI`: Windows ranks its own signed `USBXHCI` above the unsigned stand-in, and the line names what Windows chose. PASS. The warning's "gives the controllers back to that driver" was put to the owner for 8 to 10 | both `USBXHCI`, as the line said. PASS |
| 98 SE (`w98`) | `INSTALL /console /list`: the `qemu-xhci` "INF xhci98.inf; driver none; Install", no takeover row, no hint. `/list /takeover` and `/yes /takeover`: "Cannot continue: /takeover: on Windows 98 SE and ME Setup does not replace another driver." "No changes were made.", exit 1; `regedit /e HKLM\Enum\PCI` before and after: "FC: no differences encountered". PASS (the readings are the tool's own `/log` files and `XHCIINST.LOG` typed to COM1: COMMAND.COM's redirection did not capture the console tool's output) | - | - |

**On `d9bd3df29`: a line for a controller Windows is still setting up** (the owner's decision of 2026-11-14; record 23
revision 61): a recorded controller that is present while Windows' installs have not settled gets "<name>: Windows is
still setting it up. Answer its Found New Hardware window if one is open." The overlays `xp2` and `w7b`, with the
stand-in on both controllers again.

- XP, the first ticked run: "Installing Test Manufacturer USB 3.0 Host Controller ... FAILED (error 0xE000022F)." and
  "Could not replace tstxhc on Test Manufacturer USB 3.0 Host Controller. The other controllers on another driver are
  left as they are.", exit 2, both still `tstxhc`: the takeover stopped at the first failure, as designed. The cause,
  read in XP's `setupapi.log`: `#E368 An unsigned or incorrectly signed file ... for driver "xHCI98 USB 3 Controller
  (NEC uPD720200)" blocked (Policy=Warn, user said no). Error 0xe000022f`, then `#W187 Install failed, attempting to
  restore original files.` The test's key presses had answered Stop Installation at XP's Hardware Installation
  prompt: a matter of the test, not a fault of the installer.
- XP, the same run again with each prompt answered Continue Anyway: both replaced, both "xhci98 3.4.3.0, started",
  record `done;tstxhc;oem90.inf` for both, exit 3. PASS. Restart, the two root-hub wizards answered; the uninstall:
  for both controllers "xHCI98 USB 3 Controller (NEC uPD720200): Windows is still setting it up. Answer its Found New
  Hardware window if one is open." and the same for "(Red Hat QEMU xHCI)", log `takeover: Windows' installs not
  settled (3, wait 0x00000102)`, XP's Found New Hardware wizard open; answering both wizards put the stand-in back on
  both (a restart asked). PASS. Restart; `INSTALL /console /list`: both "driver tstxhc 1.0.0.0; Skip (other driver)"
  and the hint. PASS.
- 7 x86, the control: both replaced, record `done;tstxhc;oem2.inf`; restart; the uninstall "given back to tstxhc."
  for both, settled after 15616 ms; restart; both `tstxhc`. PASS, unchanged.

**On `90f81a12f`: judged controller by controller** (record 23 revision 62). On `d9bd3df29` the settle answered once
for all the recorded controllers, so a controller already back on its driver would have said "Windows is still
setting it up." while another was not finished; `90f81a12f` reads each controller's Service value and problem code
whatever the settle.

- XP, the mixed case, staged: both replaced again (each unsigned-driver prompt answered); restart, the root-hub
  wizards answered; the uninstall with ONE Found New Hardware wizard answered inside its 60 s wait (the stand-in
  installed on the NEC controller at 04:12:51, the wait ran out at 04:12:53, `Windows' installs not settled (3, wait
  0x00000102)`): "xHCI98 USB 3 Controller (NEC uPD720200): given back to tstxhc." and "xHCI98 USB 3 Controller (Red
  Hat QEMU xHCI): Windows is still setting it up. Answer its Found New Hardware window if one is open." PASS.
  Answering the second wizard put `tstxhc` back on that one too.
- XP, both pending: both replaced again; restart, the root hubs; the uninstall with both wizards left open: both
  lines "Windows is still setting it up. ...", not settled (3, wait 0x00000102). PASS. Both wizards answered,
  restart, `INSTALL /console /list`: both "driver tstxhc 1.0.0.0; Skip (other driver)". PASS.
- 7 x86, the control: both replaced (record `done;tstxhc;oem2.inf`); restart; the uninstall "given back to tstxhc."
  for both, settled after 12355 ms, both `tstxhc` at once. PASS.

Not read:

- Windows 2000 SP4: the stand-in was put on both controllers through Found New Hardware, but the guest was not
  started again on the gated kit (the session's permission check refused its relaunch), so no reading of the
  takeover or the give-back was taken on these kits; read later on a kit of `e96d6d1b6` (below).
- The x64 systems: they need a signed kit; none was read on these kits; 7 x64, XP x64 and 10 x64 read later (below).
- The refusal when the Windows disk is behind a controller: not stageable here (no guest of these bases starts from a
  USB disk); every leg's log reads "the Windows disk behind ...: 0". Held by the host vectors only.
- The window's Components row: no screenshot of it was taken; the readings are of the console.

**The legs left unread, read on 2026-11-14.**

*The refusal with the Windows disk behind the controller (10 x86).* The kit: the family layout of the x86 release
package of a clean gated build at `57ae3f5cd` (`dirty=0`), with Setup and the tools of the same run (`xhci98.sys`
`476A1B1A...`, `XHCIINST.EXE` `862d04e4...`, `SETUP.EXE` `06ebcb29...`; the build still stamps `3.4.3.0`). The guest:
an overlay of the clean 10 x86 19045 base, one virtual processor, TCG, with one controller, the emulator's `qemu-xhci`
behind a root port. Another manufacturer's driver was played by a second stand-in made for this leg, never tracked or
packaged, that really runs the controller: an INF, provider "Test Manufacturer", binding `PCI\VEN_1B36&DEV_000D` to a
service `tstxhc` whose image is a copy, made in the guest, of the guest's own `USBXHCI.SYS` (equal by SHA-256), forced
over Windows' own driver (Windows' unsigned-driver prompt answered "Install this driver software anyway"; the
controller "Test Manufacturer USB 3.0 Host Controller", `tstxhc` running). With `BootDriverFlags` 0x1C and
`PortableOperatingSystem` 1 the guest was started again from the same disk image as a USB disk (`usb-storage`) on that
controller, the firmware starting from it and no other USB device: Windows started to the desktop, `C:` on disk 1, bus
type USB, and the walk from that disk to the root, read from Windows' own parent links, passed through the controller.
Every run with `/usb` (on 10 `/takeover` needs it):

| Run | Read |
|---|---|
| `/console /list /usb` | "INF xhci98.inf; driver tstxhc 1.0.0.0; Skip (other driver)" and the hint "1 USB 3 controller runs another manufacturer's driver: /takeover replaces it."; "No changes were made.", exit 0 |
| `/console /list /takeover /usb` | "Skip (other driver; the Windows disk is behind it)"; "No changes were made.", exit 0. PASS for the row; the warning's takeover paragraph was printed although no candidate could be replaced (below) |
| `/console /yes /takeover /usb` | the log `takeover: the Windows disk behind PCI\VEN_1B36&DEV_000D&...: 1` and the same row; no call on the controller, no "Replaced" or "Could not replace" line, no takeover record; every PCI devnode's `Service` value the same before and after ("FC: no differences encountered"); the controller still `tstxhc`, running. PASS for the refusal |

The `/yes` run did besides what any run with nothing to install does (it staged `xhci98.inf`, copied the tools, listed
itself in Add/Remove Programs, exit 3); the controller and the Windows disk were not touched. After the restart
Windows started again from the USB disk to the desktop; the controller's reading after that restart was not taken.

*The text finding, and its fix.* Record 23 section 5.4.2 set the warning's takeover paragraph only when at least one
candidate is eligible, but the code counted every candidate, the refused one included, so this run's warning (and the
console's hint, and the window's box) announced a replacement that the row and the run did not make. Fixed after these
legs: `61e729b4b` (the paragraph, the hint and the box shown only when a candidate can be replaced with the choice on;
record 23 revision 63) and `afbec41b9` (a run takes over only the controllers its warning covered, so a candidate
found later in the run stays "Skip (other driver)"; revision 64), merged into `3.5.0.0` by `c322a7b31`. Held by host
vectors in `test_inst` alone; no guest reading of the fixed text was taken. With every candidate refused the window
says "Skip (other driver)" without the reason: accepted by the owner (2026-11-14), no follow-up.

*XP x64 and 10 x64.* The package: the owner-signed amd64 release of `837210f81`, with Setup and the tools of the same
gated run (still stamped `3.4.3.0`). The stand-in as in the x86 legs (provider "Test Manufacturer", service `tstxhc`,
image the guest's own `null.sys`): on XP x64 unsigned, put on both controllers through Found New Hardware with
"Continue Anyway"; on 10 x64 its catalog signed with a test certificate made for that guest alone, forced over
Windows' own driver on both. Two controllers in each guest as above (`qemu-xhci` behind a root port, `nec-usb-xhci` on
the root bus), QEMU q35, one virtual processor; the guests driven by hand, the readings from the console and the
guests' COM1 logs.

| System | Takeover | Uninstall | After its restart |
|---|---|---|---|
| XP x64 SP2 | off: both "Skip (other driver)" and the hint for 2 controllers, exit 0; `/list /takeover`: both "Replace other driver (/takeover)" and the warning; `/yes` without the option: both still `tstxhc`, exit 3. `/yes /takeover`: "Replaced tstxhc with xHCI98 on xHCI98 USB 3 Controller." twice, both controllers and both root hubs "xhci98 3.4.3.0, started", record `done;tstxhc;oem0.inf` for both, log "the Windows disk behind ...: 0" for both, exit 3 (Windows' installation prompts answered Continue Anyway, a Found New Hardware wizard for each root hub answered). After a restart both on `xhci98`, "Keep (same version)", exit 0, the record kept. PASS | both "Windows is still setting it up. Answer its Found New Hardware window if one is open.", log `Windows' installs not settled (3, wait 0x00000102)`, as on 32-bit XP; the two wizards answered (the stand-in, Continue Anyway). PASS | both `tstxhc`; `/list`: both "driver tstxhc 1.0.0.0; Skip (other driver)". PASS |
| 10 x64 19045 | `/usb` on every run. Off: both "Skip (other driver)", the hint and "Test-signing mode is on: the test-signed x64 drivers will load.", exit 0; `/list /takeover`: both "Replace other driver (/takeover)" and the warning; `/yes` without the option: both still `tstxhc` (Code 38: the stand-in does not start), exit 3. `/yes /takeover`: "Replaced tstxhc with xHCI98 on xHCI98 USB 3 Controller." twice, both controllers and root hubs "xhci98 3.4.3.0, started", record `done;tstxhc;oem2.inf` for both, exit 3. After a restart all four "Keep (same version)", exit 0, the record kept. PASS | "xHCI98 USB 3 Controller (Red Hat QEMU xHCI): given back to USBXHCI." and the same for "(NEC uPD720200)", `Windows' installs settled after 3094 ms`, the controllers on `USBXHCI`, exit 3: as on 10 x86, the line names what Windows chose. PASS | all three xHCI functions in the guest's registry, the two controllers among them, on `USBXHCI`; `/list /usb`: both controllers "INF xhci98.inf; driver USBXHCI 10.0.19041.2546; Install", the ordinary install offer, no "Skip (other driver)" row and no hint; "No changes were made.", exit 0. PASS |

Seen on 10 x64, not explained: during the takeover one Windows Security box "Windows can't verify the publisher of
this driver software" appeared once and was answered "Install this driver software anyway"; which package it was for
was not read. The kit's driver files are byte-equal to the owner-signed package, which installed with no publisher
prompt on clean 7 x64 and 10 x64 guests ("The x64 check of the build signed on 2026-11-14", below), so it is taken as
the guest's stand-in arrangement's, not the package's.

*Windows 2000 SP4.* The kit: the family layout of the x86 release package of a clean gated pair at `e96d6d1b6`
(`dirty=0`), with Setup and the tools built for that pair (`xhci98.sys` `84182A00...`, `xhcihlpr.sys`
`F8990AF5...`); it holds the give-back's lines controller by controller (`3d3bc8e16`), not the warning's fix
(`c322a7b31`). The guest: the 2000 overlay of the first legs, the stand-in already on both controllers through Found
New Hardware, the two controllers as in the x86 legs. 2000 has no `reg.exe`, so the services were read from
`regedit /e` exports over COM1.

- The takeover: `/list` without the option, both "Skip (other driver)" and the hint; `/list /takeover`, both "Replace
  other driver (/takeover)" and the warning; `/yes` without it, both "tstxhc 1.0.0.0, started", exit 3;
  `/yes /takeover`, "Installing Test Manufacturer USB 3.0 Host Controller ... done, restart needed." twice, one call
  after the other, both "xhci98 3.4.3.0, started", "Replaced tstxhc with xHCI98 on xHCI98 USB 3 Controller (NEC
  uPD720200)." and "... (Red Hat QEMU xHCI).", services `xhci98`, record `done;tstxhc;oem90.inf` for both, exit 3.
  PASS.
- The restart: the guest did not come back from a warm restart. It stayed in real mode with a blank screen, before
  Windows started, and a reset from the emulator reached the same place, while a cold start booted normally; it
  happened again at the second warm restart, so every restart below was a cold start. Windows was not reached, so it
  is taken as the guest's warm restart, not the driver's; not looked into further.
- The uninstall, after a cold start (both controllers on `xhci98`, the record `done` for both): both removed, then
  "xHCI98 USB 3 Controller (NEC uPD720200): Windows is still setting it up. Answer its Found New Hardware window if
  one is open." and the same for "(Red Hat QEMU xHCI)", log `takeover: Windows' installs not settled (3, wait
  0x00000102)`: 2000 had opened Found New Hardware for both (the stand-in is unsigned), as XP does; the record
  deleted. Answering both wizards put "Test Manufacturer USB 3.0 Host Controller" back on both (a restart asked).
  PASS.
- After a cold start: both controllers `Service` and `ActiveService` `tstxhc`. PASS.

*7 x64, on the morning's test-signed build.* The takeover and the give-back were read on 7 SP1 x64 in the morning's
x64 pass, on a build of `a30ddf03e` signed with a test certificate made for those guests alone ("The x64 pass on a
test-signed build of `a30ddf03e`", below), which has the code of `3d3bc8e16` and not the warning's fix. The two
controllers as above; the stand-in (unsigned, its own publisher prompt answered) by `pnputil -i -a`, both at Code 38.
`/list`: both "Skip (other driver)" and the hint; `/list /takeover`: both "Replace other driver (/takeover)" and the
warning; `/yes` without the option: both still `tstxhc`, exit 3; `/yes /takeover`: both replaced one after the
other, both controllers and root hubs "xhci98 3.4.3.0, started", record `done;tstxhc;oem2.inf` for both, exit 3;
after a restart, a run again: every device "Keep (same version)", exit 0; after another restart, the uninstall:
"given back to tstxhc." for both, `Windows' installs settled after 8721 ms`, the record deleted, exit 3, both
`tstxhc` at once and after the restart. No publisher prompt during the kit's runs. PASS on that build; not read on
the owner-signed build.

Not read after these legs: the takeover and the give-back on Vista x64, 8 and 8.1, and on 7 x64 on an owner-signed
build (read on the test-signed build of `a30ddf03e` alone); the refusal on any system but 10 x86, in the window, and
with the Windows disk's controller one of two candidates; the fixed warning in a guest. The owner closed 49.44 on
2026-11-14 with the legs not read in TAKEOVER2K.1.


## 49.45 A later Windows' UAS class driver on xhci98: the log notes, and an image with a later USB 3 stack (2026-11-14)

Branch `w49-uaslog`, from `3.5.0.0` at `23c1672fa`: the notes and counters of design record 13 section 6.13.11
(`cbbee0a48`, `14867aa04`), which change no request, answer, completion status or timing; `3.5.0.0` at `80d8fabe3`
merged into the branch by `97b04528e`, which changed no driver source; the branch at `97b04528e` merged into
`3.5.0.0` by `3ebdeefc2`.

**The gated builds.** At `14867aa04`, the tree clean, on development host A: the tool builds, then
`scripts\build-driver.cmd release -amd64` and `release`, both BUILD + GATES PASSED (with `XHCI98_SELFTEST_SKIP_OK=1`);
the USB Tree's snapshot self-test "all 320 counter words agree". x86 `xhci98.sys` `F556C414...CD7D`, `xhciuas.sys`
`071F5D2F...59BD7`, `xhcihlpr.sys` `B8E36356...C3F2B`; amd64 `xhci98.sys` `22C6610A...7745E`. Every x86 guest leg
below ran these files, checked by hash before the first reading and again in the USB Tree's "installed files" lines.
The host suite passed at `14867aa04` (`test_trlog` included) and at `97b04528e`. The final pair at `97b04528e` passed
too (x86 `xhci98.sys` `DE724D67...F73A`, `xhciuas.sys` `B0E1F00B...5234`; amd64 `xhci98.sys` `1860477A...1811`); no
guest leg was read on it.

**Windows 7 x86** (an overlay of the q35 base, one virtual processor, TCG; QEMU's `usb-uas` with a `scsi-hd` on a
VVFAT folder on the `qemu-xhci`, at SuperSpeed; `XhciLogVerbosity` 3 in the kit's copy of `xhci98.inf`; `XHCITR.EXE`
`BAAC733C...3E72`, devcon 6.1.7600). The later Windows' UAS class driver backported by a third party (x86
`uaspstor.sys` 6.2.9200.16384 `F7C87F8C...` and `usbd8.sys` 6.2.9200.20761) was put only inside the guest.

| Step | Result |
|---|---|
| devcon puts `xhci98.inf` on `PCI\CC_0C0330` | "Drivers installed successfully"; the UAS update waits for the restart |
| cold start: `xhciuas.inf` on the UAS interface | "xHCI98 USB Attached SCSI Storage" running (`xhciuas`), "QEMU QEMU HARDDISK UAS Device" running; the disk Offline in diskpart (both VVFAT disks carry one disk signature, so Windows takes the second offline), so the traffic step found no letter and did not run |
| its notes, level 3 | one pair for the UAS PDO, `pdo.qi=70211B0E` (Windows' own PnP location query) and `pdo.qi.vs=04001014` (the device's port, version 1, size 0x14); counters `PdoQiUnanswered` 4, every other new word 0; no `uas.urb` line (`xhciuas` sent its private IOCTL) |
| the backported `uaspstor.inf` forced on (devcon exit 1) | Code 10 on "USB Attached SCSI (UAS) Mass Storage Device" (`UASPStor`) |
| its notes | `pdo.qi=496B8280` (`GUID_BUS_INTERFACE_STANDARD`) and `pdo.qi.vs=04001020` (version 1, size 0x20), `PdoQiUnanswered` 1; no `usbdx.v1`, `usbdx.cap`, `usbdx.open`, `pdo.ioctl`, `xfer.short.*` or `uas.urb` line; before it three `xfer.error=00010106` at one instant |
| cold start again | Code 10 again; the same one pair, `PdoQiUnanswered` 1, nothing else new |

So the notes do not flood: with `xhciuas` one note pair a device and start, with the backported driver one pair. Of
the three candidate causes (a short IN completed as an error for want of `USBD_SHORT_TRANSFER_OK`; the standard bus
interface not answered at a device PDO; the exact-grant rule for a static streams open), the notes here speak to the
second only: the backported driver asks for the standard bus interface, is not answered, and stops before any V1
query, stream request or bulk transfer, so the first and the third are not reached. The emulator's UAS device stops
that driver at its first descriptor read under every stack tried (below), so the field symptom (the driver started
with four pipes and no disk, a field report of 2026-11-13) is not reproduced and the cause is not settled.

**Windows XP** (an overlay of the q35 base, one virtual processor): the family layout of the gated x86 package, with
Setup and the tools from the same branch's builds, installed with `INSTALL.BAT /console /yes /nopause` ("Done.
Please restart Windows now.", exit 3; XP's unsigned-driver prompts answered Continue Anyway); restart; a SuperSpeed
Bulk-Only stick (`usb-storage`) plugged in on the root; the level the default, 2. The USB Tree's report: the stick as
"QEMU QEMU HARDDISK USB Device (D:)" on `USBSTOR`, no `pdo.qi`, `pdo.ioctl`, `usbdx.*`, `xfer.short.*` or `uas.urb`
line, and none of the twelve new counter words nonzero. Nothing new at level 2.

**A Windows 7 x64 install image that carries a later Windows' USB 3 stack, read as data** (listed and read with 7-Zip
and a byte parser on development host A; nothing of it run there). Five images, all Windows 7 SP1 x64; the files
below are the same size in all five:

- The USB 3 stack as one driver package, `usbxhci.inf` (`DriverVer 05/01/2018,6.2.9200.22453`), staged in the driver
  store and copied to `System32\drivers`: `USBXHCI.SYS` 6.2.9200.22099, `UCX01000.SYS` 6.2.9200.22453,
  `USBHUB3.SYS` 6.2.9200.21180, `usbd8.sys` 6.2.9200.20761. Its Windows 7 x64 models section matches
  `PCI\CC_0C0330`, ten vendor forms of it and `ACPI\PNP0D10`, so on a fresh install every xHCI controller starts on
  that stack.
- KMDF 1.11: `Wdf01000.sys` 1.11.9200.16648.
- The UAS class driver `uaspstor.sys` 6.2.9200.16384 from `uaspstor.inf` (service `UASPStor`), whose only match is
  `USB\Class_08&SubClass_06&Prot_62`. No other xHCI or UAS driver is in the driver store; `USBSTOR.SYS` is Windows
  7's own, 6.1.7601.19144.
- Its x64 `uaspstor.sys`, compared with the x64 `uaspstor.sys` of the backported driver (whose x86 copy ran in the 7
  x86 legs), has the same version and link date but not the same bytes: the two x64 import tables show the two were
  made to load on Windows 7 by different changes.

The image installed unattended in a guest (q35, one virtual processor, TCG, 2 GB, image index 1), the installed disk
kept read-only and every leg on an overlay of it, with QEMU's `usb-uas` and a blank raw 64 MB `scsi-hd` on the
`qemu-xhci`; "SS" a `qemu-xhci` with USB 3 ports, "HS" one with none, so the device trains at high speed. The kit was
the test-signed x64 package of `1c476d0b3` (version stamp `3.4.3.0`), which has none of the new notes and no takeover.

| Leg | Result |
|---|---|
| the image's own stack, SS | the controller on `USBXHCI`, the root hub on `USBHUB3`, both started; the UAS device on `UASPStor`: Code 10. The emulator's limit holds on this stack too |
| the image's own stack, HS | `USBXHCI` itself at Code 10 on a `qemu-xhci` with no USB 3 port, so no UAS reading at high speed on that stack |
| `pnputil -i -a` of `xhci98.inf` | staged; Windows keeps `USBXHCI` on the controller (the Microsoft-signed driver outranks a test-signed one) |
| the package's own Setup, `INSTALL.BAT /console /yes /nopause` | "Test-signing mode is on"; the controller "INF xhci98.inf; driver USBXHCI 6.2.9200.22453; Skip (other driver)", the UAS device "Skip (other driver) ... problem code 10"; both INFs staged; exit 3. After the restart the USB Tree: "No controller here is driven by xhci98.sys". Setup skips a controller that stack runs: the case 49.44 takes up |
| `xhci98` put on the controller by hand (`UpdateDriverForPlugAndPlayDevices`, `INSTALLFLAG_FORCE`, as Device Manager's Have Disk), SS | "ok=True reboot=False"; after a cold start the controller and the root hub on `xhci98`, the UAS device on `xhciuas` (staged by Setup), started, the 64 MB disk Online in diskpart |
| the same, HS (a fresh overlay) | "ok=True reboot=False"; the UAS device on the image's own `UASPStor`, Code 10, at High Speed |
| the image's `uaspstor.inf` forced on the UAS interface over `xhci98`, SS | "ok=True"; after a cold start `UASPStor` at Code 10 at SuperSpeed, the controller and the root hub on `xhci98` and started; the log of `1c476d0b3` has only the device's `pnp.pdo` line around it |

One HS overlay was set aside after a hard stop left it in Startup Repair; its HS legs with `xhci98` were read on a
fresh overlay. So the image's UAS driver behaves on `xhci98` in the emulator as the backported one of the 7 x86 legs
did: Code 10 at SuperSpeed and at high speed, before any disk; and with `xhci98` on the controller the drive comes up
on `xhciuas` at SuperSpeed.

Not read: disk traffic under `xhciuas` (on 7 x86 the disk was Offline and the leg on a disk of its own was not run; on
the image only diskpart's Online); the new notes on x64 (the image legs ran `1c476d0b3`, which has none; they need the
final signed kit); 49.44's takeover on the image's stack; a real drive (not for `3.5.0.0`, the owner's decision of
2026-11-14).

## 49.43 A held root port tells the user what to do (2026-11-14)

Branch `w49-lvl4`, merged at `00fb5fcaf`. Read in QEMU guests of one virtual processor, each on the release package of
the named commit of that branch with its `qemu` flavour `xhci98.sys` copied over the installed one, so that the
flavour's fault injection could raise a hold: Setup refuses a `qemu` flavour folder, and on 98 SE and ME the copy had
to be made again after the first restart. The tray was the installed XHCITR of the same build, started at login. The
injection is a failed enumeration repeated at a USB 2.0 mouse on a root port (the emulator's storage device links at
SuperSpeed, where the injection is refused).

| Build | System | Read |
|---|---|---|
| `5e1086727` | XP, 98 SE, ME | after three failed re-enumerations: the " (on hold)" tag at the port and the note under it on all three; the tray's notice once, as a balloon on XP and ME and as the tool's own pop-up on 98 SE; after the port was switched off: its note and its own notice the same way; neither notice repeated afterwards |
| `5e1086727` | XP | with "Warn when a device is removed unexpectedly" cleared, a new hold raised no balloon; the device plugged in again at the held port was enumerated and the note went away |
| `dcef92035` | XP | the notice with the icon there (the control); with Explorer ended, a hold raised no balloon, and the same balloon came once at the next watch after the taskbar and the icon were back |
| `631d3d338` | XP | the notice with the icon there, once more |

The gated pair passed at `5e1086727`, `53e048c0d`, `dcef92035`, `631d3d338`, `5141d311f` and `00fb5fcaf`. No guest
reading was taken on the builds after `631d3d338`, which changed how the tray judges a reading (below) and are held by
host vectors alone.

**Found and changed before the merge, each by reading the code:** the hold job's thread used the C runtime heap, which
the x86 tool's build does not make safe between threads (the job and its buffer are now allocated and freed on the
tray's thread alone); a notice sent while the tray's icon was absent, or refused by the shell, was lost for good (it
now waits for the icon, and a refused send is tried again at later watches, thirty times at most); a reading taken
early in a job could be used after the hold it showed had ended (a reading is used only within 3 s of its own read and
only when no sleep or device change came after that read began); and a controller whose door is slow at every watch
could keep another controller's hold from ever being announced (the read order rotates from watch to watch).

**Not read in a guest:** the note at a port where the hold cannot be read (the door closed or the read failing), the
note at a controller for a hub's port, a release by replug on 98 SE and ME (the hardware wizard was in the way), and
anything on a 64-bit system. Known and left: a balloon Windows accepted but hid behind one of its own is not sent
again, and a controller whose door never returns holds the job, so that the tray announces no hold until it does; the
port's note in the window does not depend on the job (HOLDNOTES.1).

**The legs left unread, read on 2026-11-14** (the owner's decision of that day: read before the cut). Build: `3.5.0.0`
at `57ae3f5cd`, gated (`xhci98.sys` release `EE7A5D91`, `qemu` flavour `14FB2AAB`); the commits after it up to
`95ffa1f6a` change neither `xhci98.sys` nor the tool's hold code. Guests as above, 32-bit only: XP SP3, 98 SE and ME,
one virtual processor, the release package installed by Setup and the `qemu` flavour's `xhci98.sys` copied over the
installed one. A hold at a root port is four failed enumerations 20 s apart at a USB 2.0 mouse; a hold at a hub's port
is four refused completions on the interrupt-IN transfer of a mouse behind the emulator's full-speed hub, 25 s apart,
each cycling the device.

| Leg | System | Read |
|---|---|---|
| A hub's port held | XP | the driver logged the hub location held, and the snapshot read it "NOW HELD"; the controller's details, after "Driver version:", carry "Note: A port of a hub on this controller is on hold after repeated failures. Unplug the device and plug it in again. The hold also ends when the controller restarts."; the hub's port reads "(empty)" with no tag; no balloon |
| The same, the hub unplugged | XP | the controller's note gone; the snapshot still shows the hub location held (kept until the driver reuses that hub object or the controller starts, record 25 section 24 item 3) |
| Release by replug | 98 SE, ME | the held root port "(empty) (on hold)" with its note; the mouse unplugged and plugged in again at the same port: the device listed again, no note, the location's re-enumerations back to 0 of 3. On 98 SE the mouse was installed once before the hold, so that the hardware wizard did not come at the replug |
| The tray's notice on this build | XP, 98 SE, ME | none after the third failure; after the fourth, "USB port on hold", the port and the controller named, as a balloon on XP and ME and as the tool's own pop-up on 98 SE; on 98 SE and ME one frame each, whether it repeated not watched |
| A held root port at logging level 0 | XP | the driver logged the location held; the tree "(empty)", "no device connected", no tag and no note; no balloon in the 36 s after: with the door closed nothing is known (record 25 section 24 item 1) |
| The fallback note | XP | **not read**: no port reporting "the device failed enumeration" could be made (below) |

The fallback note ("If this keeps failing, unplug the device and plug it in again.") needs a port whose connection is
"the device failed enumeration" with no hold known. The bus reports that status at a root port alone, while its
enumeration has failed for good (`hcd_door.c`, `hcdDoorConnection`); a hub's port with no device record is answered as
no device connected. Read on XP, with `XhciTolerance` 0: the mouse behind the hub failed its enumeration and its hub
port read "(empty)", "no device connected", with no note. So the note cannot appear at a hub's port, and record 25
section 24 item 4 is corrected (revision 34). At a root port the status could not be made: at `XhciTolerance` 1 a
refused completion on the default endpoint cycles the device and the third cycle holds the port; at 0 it ended in a
controller reset from the default endpoint's recovery, repeated while the test-only fault stayed armed (not looked into
further, a test-only setting under a test-only fault); the emulator's xHCI has no property limiting its slots, so a
refused Enable Slot was not available either.

Left after these legs: the fallback note, and the 64-bit systems (HOLDNOTES.1). The 64-bit systems were read later the
same day ("The x64 check of the build signed on 2026-11-14", below). The owner closed 49.43 on 2026-11-14, its
leftovers in HOLDNOTES.1.


## 49.47 (Setup proposes the switchover) (2026-11-14)

The owner's final design: the INFs write `XhciIntelPortSwitch` 0 and a
missing value is off, as in `3.4.3.0`; `SETUP.EXE` proposes 1 at a first
install on a listed board (`/yes` included); an update keeps what is set.
Commits `f786ad3dc` (missing is off), `1a3733580` (INFs back to 0),
`3e3606b5c` (Setup's proposal, host vectors), `708af051a` (documents), on
`450db6764` (merge of `3.5.0.0` at `e96d6d1b6`).

Host checks at `708af051a`: host suite PASSED (test_inst 9088 checks with
the ten `inst_psw_propose` vectors), INF gate both INFs PASSED, its
self-tests PASSED (1931), the packager's self-tests PASSED (1945),
`check-text.ps1` OK. Gated pair at `708af051a`: x86 and amd64 PASSED, both
`dirty=0` (`logs\pair4\`; `xhci98.sys` x86 `939E4FD2...`, amd64
`514D0DE7...`).

Guest legs on the x86 package of `708af051a` (QEMU `1B36:000D`, not a
listed controller), ports 49455-49458:

| Leg | Result |
|---|---|
| 7 x86 (`w7f`), `INSTALL /console /list` on a fresh guest | no `XhciIntelPortSwitch` question and no "would be set" line (only `XhciResumeKeepBot`'s). PASS |
| 7 x86 (`w7f`), first install by Setup `/yes` | controller key 0x0, no machine-wide value; exit 3. PASS |
| 98 SE (`w98f`), first install by Setup `/yes`, cold start | `"XhciIntelPortSwitch"=dword:00000000`, none in `Services\USB`. PASS |
| 7 x86 (`w7g`), by hand (`pnputil -i -a`) | 0x0, no machine-wide value. PASS |
| 7 x86 (`w7v`), released `3.4.3.0` (0x0), restart, this build by Setup | 0x0 kept, no machine-wide value; exit 4 (the same-version reinstall's root-hub step). PASS |

The listed-board path (the proposed 1 in the question, the row and the
written value) cannot be read in the emulator: no test-only detection
override exists in the shipped tools; it rests on the host vectors and the
owner's bench.

Changed after these legs, by reading the code before the merge: the proposed 1 is written only after the install, and
only where this run installed xhci98 on a listed controller (`03233213a`: `inst_psw_counts_nt`, `inst_psw_counts_9x`,
`inst_psw_write_now`, host vectors); a refused, failed or cancelled install writes nothing. The gated pair passed at
`03233213a`; no guest leg was read on it.

The x64 leg (a computer without a listed controller: no question and no value) was read on the owner-signed amd64
build of `837210f81`: "The x64 check of the build signed on 2026-11-14", below.

## 49.40 to 49.42 Logging level 4 shown as it is, a mixed install named, and where an entry came from (2026-11-13)

Three tasks of 2026-11-13 from one field report (a user who chose level 4 to help sent a report with no log; a
controller's entry installed from `2.2.0.1`'s files under a running `3.4.3.0`). Built on branch `w49-lvl4` and merged
at `23c1672fa` (the branch at `2db3baf6b`): `fdcabd069` (49.40, both drivers and the USB Tree), `af9dc5460` (record
21's log answer), `e1b58399a` (49.41), `2db3baf6b` (49.42). The decisions are the decisions table's rows "Phase 49,
logging level 4 shown as it is", "Phase 49, the USB Tree names a mixed install" and "Phase 49, the USB Tree names
where an entry came from".

**49.40.** Until then `xhci98.sys` withheld its log text from the query answer at level 4, because it holds kernel
memory addresses (`xhci_qry.c`), and the USB Tree's report and snapshot left it out; `xhcicm.sys` already returned its
log text at level 4 with the address values written as `--------` (`CmQueryLogAnswer`, `cm_query.c`), and the USB Tree
printed those records (`tr_qlog_text`, `tr_log.c`). Now `xhci98.sys` serves the ring at level 4 with the same bounds,
replacement, cut and size cap, `XHQ_F_ADDRESSES` set beside `XHQ_F_WITHHELD` (kept, so that an XHCITR before `3.5.0.0`
still says it shows no log), the layout and `XHQ_VERSION` unchanged; `xhcicm.sys` blanks address values below level 4
only; the USB Tree shows the level-4 log in the window, the report (every dump's text appended whole) and the
snapshot, "Recording:  on (XhciLogVerbosity 4)" as at 2 and 3 with no warning line, the old message kept for an older
driver. Host tests: `test_xhq`, `test_trlog` and `test_cm_query` pin level 4 in both answers and both texts, the
older-driver text and the older reader's two flags. Documents: record 13 section 8.12 (with the accepted cost),
records 21 and 25.

**49.41.** The Device Manager entry block of an xhci98 controller and of an NHI compares the entry's `DriverVersion`
with the version the running driver reports and, for an entry older than the running driver, prints after "Version:"
the accepted sentence naming both versions and SETUP.EXE; nothing for equal versions, a newer entry or an unknown
version; `xhciuas` and the device entries are left out. Setup's update itself re-installs every present older entry
and was not at fault in the field case. Host tests: `test_tr` holds the cases. Record 25 section 18 item 9.

**49.42.** The block prints, from the entry's driver key and only where the value is a nonempty string, "INF file:"
(`InfPath`), "Matching ID:" (`MatchingDeviceId`) and "Provider:" (`ProviderName`); Windows' driver-install log is not
read; the snapshot's `.DEV` lists the same three values. No host test of its own was added. Record 25 section 18 item
10.

**Read in a guest (2026-11-14).** The release x86 build of `3.5.0.0` at `e2a3947c1`, gated (`xhci98.sys`
`1cc29ec8...`; its files still stamp `3.4.3.0`, so every running version below reads `3.4.3.0`). Two XP SP3 guests,
each with the emulator's Alpine Ridge model (its xHCI, and its NHI under `xhcicm.sys`) beside QEMU's xHCI, and one 98
SE guest with QEMU's xHCI alone, all on fresh overlays of the q35 bases; the level set with `XHCITR /setlevel` and a
restart; the readings the USB Tree's report and snapshot over COM1.

| Task | System | Read |
|---|---|---|
| 49.40 | XP | at level 4 both controllers' `xhci98.sys` logs shown ("Recording:  on (XhciLogVerbosity 4)"), no withheld-log message, and the level-4 records that carry addresses (`start.bar`, `start.common.pa`) with their values, in the tree's log section, the report's snapshot part and the saved snapshot; at 2 the log as before, without them. `xhcicm.sys`'s log shown at 4 and at 2; no record it blanks below level 4 occurred (`cm.live.bar-changed` and the 9x devnode records; none also with the enclosure plugged), so its unblanked address values were not seen |
| 49.40 | 98 SE | the same for its one controller at 4 and at 2; `xhcicm.sys` not installed there |
| 49.41 | XP | the mixed install made by hand: the published `3.4.2.0` installed with `/thunderbolt`, then `xhci98.sys` alone replaced by this build's, and a restart. Both xHCI blocks: "Version: 3.4.2.0" and after it "Note: This entry was installed from another version's files (3.4.2.0) than the driver that is running (3.4.3.0): run SETUP.EXE to update it."; the NHI's block (entry and driver both `3.4.2.0`) and the bridges' with no note. On the clean install no note at level 2 or 4 |
| 49.41 | 98 SE | the controller's driver key holds no `DriverVersion`, so no "Version:" line and no note, as the code has it for an unknown version |
| 49.42 | XP | every entry block, clean and mixed, prints the three lines: both xHCI controllers "INF file: oem0.inf" with "Matching ID:" `pci\ven_1b36&dev_000d` and `pci\ven_8086&dev_15c1`, the NHI `oem2.inf` and `pci\ven_8086&dev_15bf`, the tunnel bridges `oem3.inf` and `pci\ven_8086&dev_15c0&cc_0604`, each with "Provider:" |
| 49.42 | 98 SE | "INF file:" (the 8.3 name 98 SE recorded for the copied INF) and "Provider:", no "Matching ID:": 98 SE's key for the controller holds no `MatchingDeviceId` |

Seen beside, not followed: on the first start after the clean install one XP guest listed no NHI, though its entry had
been installed; from the next start on it was listed and running. No fault of 49.40, 49.41 or 49.42 was seen.

## 49.46 Standby with a firmware-run connection and the drive present from power-on: the unit left in D3 (2026-11-13 and 14)

**From the roadmap row (moved there on 2026-11-14, when the row was shortened).** Found on 2026-11-13 by 49.13's x64
sleep legs: with a firmware-run Thunderbolt connection and the drive present from power-on (Windows 7, the bundled
Stor98), about half the wakes leave the drive's logical unit in D3 with its queue frozen and Plug and Play locked
behind it, the same end state as TBSLEEPWRITE.1's, reached on this path too (7 x64: 3 hangs in 5 with a writer, 2 in 2
idle; 7 x86 hangs too), named by a kernel memory dump; Stor98 is healthy in that state. A test-only change that asks
for D0 again on the parked unit released it in its first legs. Owner, 2026-11-14 (the decisions table, "Phase 49, the
owner's decisions of 2026-11-14"): fixed in `3.5.0.0`, on by default, as `XhciCmWakePowerUp` (1 by default), the
request for D0 coming from a driver that is not the unit's power policy owner, with three limits (only a drive the
bundled Stor98 runs, only after that drive's D0 at the wake was seen to fail, once a wake); on 2000, XP and XP x64 the
fix may find no unit and do nothing (the legs of 2026-11-14 show that it does act on XP SP3 and 2000 SP4: "49.46 (the
fix's legs)"). The fix's legs and its merge, and a reading on real hardware (the owner's bench,
after the cut), are owed; the alternative of answering `PowerRelations` is WAKEPWRREL.1.

Later, 2026-11-14: the legs were read and the fix merged (below), and the owner read it on real hardware at his
bench after the cut and closed 49.46 the same evening; his readings are not recorded in this sheet. He closed
49.47 the same way, the port switch read on a listed Intel board.

## 49.46 (the fix's legs) The wake power-up read in the emulator (2026-11-14)

Branch `w49-d0fix`, merged at `7ac749c74` (the branch at `9016cf577`); design record 21 section 6.3.1. Every leg below
ran a build of `1b09d44bb`, whose sources are the merged tip's (the commits after it change documents only): the x86
gated pair's `xhcicm.sys`, release `B3DCA5AF` and `qemu` flavour `5147C7FA`, and the amd64 release signed by the owner,
`CA714E79`; in every guest the installed `xhcicm.sys` was checked against its build before the first reading. The
`qemu` flavour ran with `XhciCmLogDebugView` 1, so its legs show every `cm.wakepowerup` note on the debug port; the
release flavour's USB Tree report shows the wake's notes (`fail`, `ask`, `done`) but not the start's (`rel`, `owner`).
Earlier builds of the branch were read the same way and gave two of its changes: on 7 x86 the kernel did not answer the
member's `DEVPKEY_Device_Service`, so the owner test became the install section and the top of the member's stack
(`1d3dc3039`), and on 10 x86 the install section reads `Stor98_Inst.NTx86` (accepted since `1db848f65`).

**The forms.** QEMU q35, two virtual processors. S.5: the Titan Ridge model under its firmware connection manager, the
NVMe enclosure present from power-on, Stor98 on the drive, a standby with a paced writer (copies to the drive across the
sleep) or idle, and the guest's own shutdown asked about 300 s after the wake. S.2 (49.13's form): the ASM4242 model
with no pre-boot connection manager, the drive plugged in after the desktop, a write in flight at the sleep, on some
legs the drive removed about 60 s after the wake. "Parked" is the member's wake D0 failed with `C000000E`;
"released" is `fail`, then `ask` and `done.status` 0 once. A pass is the guest powered off after the shutdown request;
a hang is a copy held and no power-off 600 s after it.

| System, form | Build | Value | Legs | Read |
|---|---|---|---|---|
| 7 x86, S.5, paced | release, `qemu` | 1 | 5 | 4 parked, each released once; 1 not parked; no write failed; shut down 6 to 9 s after the request |
| 7 x86, S.5, idle | release | 1 | 2 | not parked; shut down |
| 7 x86, S.5, paced | `qemu` | 0 | 2 | **hang** in both; the log only `value=00000100`: nothing read, nothing asked |
| 7 x86, S.5, two standbys (copies, idle, copies, a second standby, then an 80 MiB write) | `qemu` | 1 | 1 | both wakes parked, each released once (`counts=02020200`); the 80 MiB file written after the second wake compared equal (`fc /b` and SHA-256); shut down 17 s after the request |
| 7 x86, S.5, the adapter disabled and enabled, the NHI disabled across a standby and enabled, a cancelled query-remove (a file held open), standbys between them, no writer across the sleeps | `qemu` | 1 | 1 | seven standbys, none parked; every write after each wake good; the shutdown finished. A parked wake after the adapter's disable and enable was released once in a first form of this leg |
| 7 x86, S.5, the power button pressed 1 to 3 s after the wake (sleep or shut down) | `qemu` | 1 | 3 | the press had no visible effect; the wake parked and released once; shut down on the leg's own request |
| 7 x86, S.2, standby, removal | release | 1 | 1 | parked, asked, done 0; the removal processed; shut down |
| 7 x86, S.2, standby, removal | release | 0 | 1 | **hang**: the copy in flight held through the wake and the removal, the shutdown asked 240 s after the wake never finished, and the guest started again by itself about 11 min later (cause not read) |
| 10 x86, Stor98, S.2, standby, drive kept | `qemu` | 1 | 7 | 2 parked, each released once (`owner=00050011`, `fail=00000500`, `ask=00050001`, done 0); 5 not parked (the wake D0 succeeded); in all 7 the writes failed after the wake (the drive lost, 49.13's S.2 limitation) and the shutdown finished, in up to about 2 min |
| 10 x86, Windows' own NVMe driver, as above | `qemu` | 1 | 4 | `owner=00050022` (both readings another's); the wake D0 failed `C000000E` in all four, then `skip` reason 5, **nothing asked**, two of them with the test-only forced trigger set (the real failure armed first, so the forced path was not the one read); writes failed after the wake; shut down 139 to 160 s after the request |
| 7 x64, S.5, paced | signed amd64 | 1 | 4 | all 4 parked, each released once; shut down 6 to 8 s after the request |
| 7 x64, S.5, idle | signed amd64 | 1 | 2 | not parked; shut down 9 s after the request |
| 7 x64, S.5, paced | signed amd64 | 0 | 2 | **hang** in both; the report only `value=00000100` |
| 7 x64, S.5, two standbys as on 7 x86 | signed amd64 | 1 | 1 | both wakes parked, each released once; the 80 MiB write compared equal |
| 7 x64, S.5, the power button 2 or 3 s after the wake | signed amd64 | 1 | 2 | no visible effect of the press; shut down |
| 7 x64, S.2, standby / hibernation, drive kept | signed amd64 | 1 | 2 | standby: `owner=00050011`, parked, released; hibernation: nothing armed; writes failed after the wake (the drive lost) in both; shut down |
| 7 x64, S.2, standby, drive kept | signed amd64 | 0 | 1 | no hang: every copy after the wake failed at once; shut down 10 s after the request |
| 10 x64, Stor98, S.2, standby | signed amd64 | 1 | 2 | `owner=00050011` (the `.NTamd64` section); one parked and released (shut down 47 s after the request), one not parked (26 s) |
| 10 x64, Windows' own NVMe driver, Titan Ridge, paced | signed amd64 | 1 | 1 | `owner=00040022`; nothing armed, nothing asked; shut down 29 s after the request |
| Vista x86 SP2, S.5, paced | release | 1 | 5 | 4 parked, each released once; 1 not parked; shut down 6 to 14 s after the request |
| Vista x86 SP2, S.5, paced | `qemu` | 1 | 4 | `rel=00040001`, `owner=00040011`; 2 parked, each released once; 2 not parked; shut down 6 to 17 s after the request |
| Vista x86 SP2, S.5, paced | release | 0 | 2 | **hang** in both: the copy held from about 20 s after the wake, no power-off 600 s after the request |
| 8.1 x86, S.5, hibernation | release, `qemu` | 1 | 3 | no wake parked; shut down 16 to 29 s after the request |
| 8.1 x86, S.5, hibernation | release | 0 | 2 | no wake parked; shut down 16 and 30 s after the request |
| Vista x64 SP2, S.5, paced | signed amd64 | 1 | 6 | 4 parked, each released once; 2 not parked; no write failed; shut down 6 to 13 s after the request (one leg not timed) |
| Vista x64 SP2, S.5, paced | signed amd64 | 0 | 2 | one **hang** (the copy held from the wake, no power-off 600 s after the request); one not parked, shut down |
| XP SP3, S.5, hibernation | release | 1 | 5 | `rel=00040001` and `owner=00040011` (both readings Stor98's) in every leg; 1 parked (the wake D0 failed `C000000E`) and released once, shut down 17 s after the request; 4 not parked, shut down 11 to 18 s after it; no write failed |
| XP SP3, S.5, hibernation | release | 0 | 4 | 3 not parked, shut down 6 to 12 s after the request; in 1 the copy held from 10 s after the wake and the writer stalled, so the leg's shutdown request was never made and the guest had not powered off 900 s after the wake: the hang's signature |
| 2000 SP4 (ACPI), S.5, hibernation | release | 1 | 3 | `rel=00040001` and `owner=00040011` in every leg; no wake parked, nothing asked; no write failed; shut down 7 to 17 s after the request |

8.1 x86 offers no standby in the emulator's guest (`powercfg /a`: standby disabled by an internal system component,
"Graphics"), so its legs are hibernations, which parked nothing either way. With the fix the release of an S.2 wake
lets the held requests fail: the write in flight is lost and the drive is gone after the wake, as record 21 section
6.3.1 says; what remains of TBSLEEPWRITE.1 is that.

**XP and 2000.** Read in hibernation alone: standby could not be read in the emulator on either. XP suspended but
never resumed after the host's wake (no guest output for 900 s, the processor spinning in the kernel), and the same
with nothing of xHCI98 installed and with no USB4 model at all, so it is the emulator's; 2000 never suspended with the
Cirrus adapter and did not reach the logon with no display adapter. XP and 2000 ran one virtual processor, the other
systems two. Record 21 section 6.3.1 expected that on 2000 and XP the fix might find no unit and do nothing; it acts
there: R1 found the unit and both owner readings were Stor98's at every start on XP SP3 and 2000 SP4, and on XP a
hibernation's wake D0 failed and was released once (the record is corrected). In 2000's three hibernations no wake D0
failed, so nothing was asked. Vista x64 behaves as Vista x86: the fault at 0 (one hang in two) and every parked wake
released.

**Driver Verifier** (7 x86, S.5, `qemu` flavour, `verifier /standard`): no Verifier bug check in any leg (after each hang
the System log had no BugCheck event and no dump was written). With `xhcicm.sys` alone verified: five paced legs and one
S.2 standby, every one parked and released once (`owner=00040011` or `00050011`), the paced legs shut down 6 to 15 s
after the request and the S.2 leg on its own request; at value 0, one idle leg passed. With `xhcicm.sys` and `stor98.sys` verified: the owner test's second reading
(the top of the member's stack) names another driver (`owner=00040021`, `00050021` on S.2), so the fix leaves the drive
alone: in two of three paced legs the wake parked, was skipped with reason 5 (`skip=00050400`), and the old hang
followed; the third was not parked and passed; at value 0, one idle leg passed. Which driver is on top under Verifier
was not read. The owner, 2026-11-14: no issue for users, recorded as a known limit of the owner test (record 21
section 6.3.1).

**Void, not readings:** two Verifier legs whose standard VGA adapter made Windows 7 refuse standby; a 10 x86 image whose
driver-store prompt was left unanswered; two Vista legs whose writer did not run across the sleep; two power-button legs whose
first sleep hibernated; images whose control port was taken on the host before they started.

**The window before the ask, and the restart paths (the last legs, 7 x86 and 7 x64).** The ask comes about 5 s after
the resume's release of the tunnel port. That window was not reached in the emulator: every request a guest made
itself (a second standby, a shutdown, a hibernation), from 2 s after it saw the wake, reached the driver after the
ask, and no guest acted on the ACPI power button pressed by the host, at any delay from 1 to 60 s after the wake, with
the button set to sleep, shut down or hibernate (7 x86 and 7 x64); in those legs the wake parked and was released once
where it parked, and the leg's own shutdown finished. A hibernation asked 10 s after a standby wake (7 x86): the
standby wake not parked, the hibernation 26 s after it, the resume, the writes going on, shut down 20 s after the
request. The restart paths with a copy loop across every sleep (7 x86, `qemu` flavour, one leg of seven standbys): the
adapter disabled and enabled, the NHI stopped across one standby (nothing asked) and enabled, and a query-remove
refused with a file held open (devcon "Disabled on reboot"); four wakes parked, each released once
(`counts=04040400`), among them the wakes after the NHI's restart and both after the cancelled query-remove; every
write after every wake good; the shutdown finished. No hang, no bug check and no second request for a unit within one
wake in any of these legs.

**Not read:** standby on 8, 8.1, XP and 2000 (none in their guests here); XP x64 and 8.1 x64 (8.1 x64 not run: no
standby on 8.1 here, and its hibernation read on 8.1 x86 with nothing to recover); a second sleep or a shutdown inside
the window between the wake and the ask (not reachable in the emulator, above); the kept child on 8.1 (not staged); a
request still out across another wake; two drives; more than two processors; real hardware, at the owner's bench after
the cut.

## The x64 pass on a test-signed build of `a30ddf03e` (2026-11-14)

The morning's x64 pass, before the owner-signed build of `837210f81`. The kit: the amd64 release package of a clean
gated pair at `a30ddf03e` (`dirty=0`, the `3.5.0.0` tip that morning; the binaries still stamp `3.4.3.0`), signed by
the packager with a test certificate made for those guests alone, not the owner's, in the family layout with Setup
and the tools of the same run (`xhci98.sys` `0CA928A8...`, `xhcicm.sys` `7E3227B9...`, `stor98.sys` `34F66F66...`,
`xhciuas.sys` `AD6461E2...`, `XHCIIN64.EXE` `46DDF92B...`). Each guest's copy was compared by hash with the kit before
the first boot that read it. That certificate was trusted inside each guest alone (Root and TrustedPublisher); its
key was deleted once the kit was made, and nothing of it is in the tree. Guests on development host A: QEMU q35, TCG,
one virtual processor, fresh overlays of the clean bases. Setup: `INSTALL.BAT /console /yes /nopause /thunderbolt
/stor98`, with `/usb` on 10. Each check boot: a SuperSpeed stick present at power-on, a copy and a byte compare; the
USB4 enclosure model (the ASM4242 model on Vista to 10, the Alpine Ridge model on XP x64) plugged in by the host, its
NVMe drive copied and compared; `XHCITR64 /eject` of it, the drive unplugged and plugged in again, copied and
compared; a shutdown, timed on the host.

| System | Install | Stick | The hot-plugged NVMe drive | Eject and replug | Shutdown |
|---|---|---|---|---|---|
| 7 SP1 x64 | exit 3, no publisher prompt; the controller and root hub "xhci98 3.4.3.0, started", the router `xhcicm`; the installed images' hashes those of the kit | SuperSpeed, "no differences" | on Stor98, "no differences" | rc 0, "It is now safe to disconnect the device."; back, "no differences" | 15 s |
| 8.1 x64 | exit 3, no publisher prompt; without `/usb` Windows' own USB 3 driver stays, the connection manager, the filters and Stor98 installed | SuperSpeed on Windows' own stack, "no differences" | on Stor98, "no differences" | the same | 21 s |
| 10 22H2 x64 (`/usb`) | exit 3, no publisher prompt; the controllers and root hubs "xhci98 3.4.3.0, started", the router `xhcicm` | SuperSpeed on `xhci98`, "no differences" | on Windows' own NVM Express driver, as designed on 10, "no differences" | the same | 56 s |
| Vista x64 SP2 | exit 3, no publisher prompt; `xhci98` and `xhcicm` started | SuperSpeed, "no differences" | "no differences" | the same | 10 s |
| XP x64 SP2 | exit 3 after three "has not passed Windows Logo testing" warnings answered Continue Anyway, as with the owner-signed x64 package of `1c476d0b3` installed the same way on the same base as a control; both xHCI controllers and root hubs "xhci98 3.4.3.0, started", the NHI "xhcicm 3.4.3.0, started" | SuperSpeed, "no differences" | after Found New Hardware for Stor98 and one Logo warning, "no differences" | the same | 10 s |

The only problem devices were the bases' own. On XP x64, after those legs, `XhciCmRingInterrupt` 2 and then
`XhciInterruptMode` 4 with it: Windows started each time, both copies "no differences", the report naming each
setting in effect, no Code 10. On 7 x64 the same build's reports were read for 49.40 to 49.42 (logging level 4, both
drivers' logs) and, with a UAS device at level 3, for 49.45 (no flood of notes), and `XhciIntelPortSwitch` was 0x1 in
each controller's key, the build's default before Setup's proposal of 49.47. The 7 x64 takeover is in "49.44" above;
on 10 x64 the unsigned stand-in was refused by Windows, so no takeover was read there on this build. No fault was
seen.

The owner-signed build of `837210f81` ("The x64 check of the build signed on 2026-11-14", below) supersedes this pass
for 7 x64 and 10 x64; for 8.1 x64, Vista x64 and XP x64 this pass, on a test-signed build of an earlier commit, is
the latest reading.

## The x64 check of the build signed on 2026-11-14 (49.43, 49.46, 49.47)

The owner-signed amd64 release package of the gated pair at `837210f81` (`xhci98.sys` `E5B3C68D...`, `xhcicm.sys`
`0C6FA085...`, `stor98.sys` `ED1BC986...`), in the family layout with `SETUP.EXE` (`105EC70A...`), `INSTALL.BAT` and
the tools of the same gated run (`XHCIIN64.EXE` `C1ED7BED...`, `XHCITR64.EXE` `44E81030...`). The binaries still stamp
`3.4.3.0`, so Setup and the report say "xhci98 3.4.3.0". Guests on development host A: QEMU q35, TCG, two virtual
processors, each a fresh overlay of the clean 7 SP1 x64 or 10 22H2 x64 base, test-signing mode on in both; inside each
guest alone the package's own public `xhci98-test.cer` was put in the Root and TrustedPublisher stores. Setup ran
`INSTALL.BAT /console /list`, then `/console /yes /nopause /thunderbolt /stor98`, with `/usb` on 10. The same legs
were rehearsed first on the owner-signed release of `1b09d44bb`; those runs are not counted.

**The install, a device and the report.**

| System | Install | Device | `XHCITR64` | Shutdown |
|---|---|---|---|---|
| 7 SP1 x64, the Titan Ridge model beside `qemu-xhci` | exit 3, no publisher prompt; both xHCI controllers and root hubs "xhci98 3.4.3.0, started", the NHI "xhcicm 3.4.3.0, started"; in the guest every `.sys` and `.cat` "Signature verified." by the package's certificate, and `setupapi.dev.log` naming its signer | a SuperSpeed stick (D:) and the enclosure hot-plugged (its NVMe drive F:), a copy to each "FC: no differences" | `/probe` rc 0 ("the driver ANSWERED"), `/dump` rc 0, `/report` taken | 6 s |
| 10 22H2 x64, `qemu-xhci` alone | exit 3, no publisher prompt; the controller and root hub "xhci98 3.4.3.0, started"; `setupapi.dev.log` names the same signer | a SuperSpeed stick (E:) on xhci98, a copy "FC: no differences" | `/probe` rc 0, `/dump` rc 0, `/report` taken | 54 s |

The installed images read in the check boots were the package's. The only problem device on either is the base's own
`QEMU0002` (Code 28).

**49.47: no listed controller, so no proposal.** The controllers present, QEMU's `1B36:000D` on both and the Titan
Ridge's `8086:15EC` on 7, are not listed. On both: no value anywhere before Setup; `/list` has no
`XhciIntelPortSwitch` question and no "would be set" line (only `XhciResumeKeepBot`'s); after Setup
`XhciIntelPortSwitch` 0x0 in each xHCI controller's key (two on 7, one on 10), the same in the check boot, and none in
`Services\USB`. Read.

**49.46: one standby on 7 x64.** On a fresh overlay of the 7 x64 install, 49.46's x64 form: the Titan Ridge model with
the enclosure at power-on, `XhciCmWakePowerUp` absent (the default), Stor98 on the drive, a paced 64 KiB copy about
every second across an S3 standby, the wake by the host, the shutdown asked 300 s after the wake. Both checks after
the wake read `value=00000101`, `fail=00000400` with `fail.status=C000000E`, `ask=00040001`, `done=00000400` with
`done.status=00000000`: the failed wake power-up released once. The writer ended ok=288 fail=0, the drive still there
("PCIe tunnel: up", "Unexpected removals: none since this start"), and the guest exited 11 s after the shutdown
request. Read.

**49.43: a held port on 64-bit Windows.** A hold comes only from a charged device cycle (a refused or unattributed
completion code, a slot-fatal code, PED or over-current at a root port, a hub disabling a port under its device), and
the emulator's xHCI raises none of them, so these legs used the x86 legs' recipe: the `qemu` flavour's fault
injection. Its amd64 `xhci98.sys` of the same commit, from a gated `qemu` build at `837210f81` (`dirty=0`,
`F826AE58...`), is **unsigned**: it was copied over the installed copies in a leg overlay of each install above, and
the leg ran in a boot with driver signature enforcement disabled (Advanced Boot Options, "Disable Driver Signature
Enforcement"). Nothing was signed for these legs. So the readings are of the owner-signed package's tray and tree with
the unsigned test-flavour driver file swapped in; the flavour's own notes in the report (`qemu.inj.fire`,
`tol.loc.hold`) show it was the image running. On 7, Windows' Found New Hardware balloons were turned off by its own
policy first: each cycle of the mouse otherwise left Windows' "Your device is ready to use" balloon up, with nothing
to dismiss it, and the tray's notice behind it. The device: a USB 2.0 mouse on `qemu-xhci` (root port 6 is the
emulator's port 2, root port 7 its port 3).

| Leg | 7 SP1 x64 | 10 22H2 x64 |
|---|---|---|
| A root port held: four refused completions on the mouse's interrupt-IN transfer, the fourth refused (`tol.loc.hold=00000601`) | the report "[Port 6, USB 2] (empty) (on hold)" with "Note: This port is on hold after repeated failures. Unplug the device and plug it in again. The hold also ends when the controller restarts."; the tray's balloon "USB port on hold", "Port 6 of xHCI98 USB 3 Controller (Red Hat QEMU xHCI) is on hold after repeated failures. Unplug the device and plug it in again."; no second balloon in the 40 s after it timed out; the hold still reported 3 min later. Read | the tag and the note, the same text. Read. **The notice unclear**: the screens began about 30 s after the fourth injection, after a banner would have gone, and the action center's count read 1; the same banner was seen in the rehearsal on `1b09d44bb` |
| The release by a replug (unplug, 20 s, plug) | "[Port 6, USB 2] USB Input Device", no note; the dump "re-enumerations 0 of 3, repowers 0 of 3". Read | the same. Read |
| A hub's port held (the emulator's hub at root port 5, the mouse at its port 1; `tol.loc.hold=00010001`) | at the controller "Note: A port of a hub on this controller is on hold after repeated failures. ..."; the hub's port with no tag; no tray notice in 15 screens (none is designed for a hub's port); the note gone after the hub was unplugged. Read | the note, gone after the unplug; no banner in 15 screens. Read. The action center's count had gone to 2 before the hub's injections ended, from a source not identified |
| A root port switched off (one over-current injection; `tol.port.oc=00000007`, `tol.loc.hold=00000703`) | "[Port 7, USB 2] (empty) (on hold)" with "Note: This port is switched off after repeated failures. It stays off until the controller restarts: restart Windows, or disable and enable the controller in Device Manager." Read. **The notice not seen** in three attempts: once hidden behind Windows' own activation balloon, then 45 s and 3 min of screens with no other balloon up and the tray running. Not explained | the note as on 7; the banner "USB port on hold", "Port 7 of xHCI98 USB 3 Controller (Red Hat QEMU xHCI) is switched off after repeated failures. Restart Windows to turn it on again.", and the action center holding that one entry. Read |

Not read: the fallback note (not reachable in the emulator, as on x86).

What this leaves: the "switched off" notice on 7 x64, not seen and not explained (the hold, its note and the same
notice on 10 x64 were read); the "on hold" notice on 10 x64 on this build, unclear (read in the rehearsal on
`1b09d44bb`, and XHCITR's changes between the two commits touch no hold code); and the installed files' version, still
`3.4.3.0`, so the version lines are not a reading of `3.5.0.0` as such.

## 49.final: the install legs on the cut's downloads (2026-11-14)

The install legs of the owner's cut of `3.5.0.0`, run on the cut's three zips as they lie in `out\`, before any
upload: that the published assets are these files (the hashes below) is not read here. The cut: branch `3.5.0.0`,
the version commit `005d46eb3`; the pre-cut passed with the two gated builds at `0600e7a8b` (x86 in the main
checkout, amd64 in a second tree). The owner cut twice: the second cut, with `-Force`, came after the readme's
sentence on the snapshot's files was corrected (`b81d7eda8`); the release directory is `fe2736d47`. The re-cut is
recorded here and in the roadmap, not in `releases/history.md` (`releases/README.md`). Development host A, QEMU q35,
every guest on a fresh overlay of its base. Nothing was built and nothing was signed for these legs.

**The downloads** (`out\` of the main checkout, as cut; not yet uploaded when read):

| Zip | Bytes | SHA-256 |
|---|---|---|
| `xhci98-3.5.0.0.zip` | 4,913,941 | `75ec5d806ba9dd5a53763dd0e950935b54d6b9d27bf0d43b8f96f497557abca4` |
| `xhci98-3.5.0.0-free.zip` | 6,251,276 | `7426cf165dffeead4fbbf48e1a764d2a459ee47c56ed131a7d7d0cea553c02ef` |
| `xhci98-3.5.0.0-nonfree.zip` | 103,141,696 | `2824d85124f2b82acffc0f7bf871bbe31fe8ebafa80c5f4532c62d3c18fd600f` |

### The downloads read on the host

Read only; each zip unpacked into its own empty folder (95, 239 and 1051 files). The outputs are in
`E:\xhci98-scratch\w49\dlcheck\` (`parta-out.txt`, `step2-out.txt`, `verinfo-out.txt`, `NOTES.md`).

- **Nesting.** The base download is inside `-free`, and `-free` inside `-nonfree`, byte for byte but `readme.txt`.
  `-free` adds 144 files, all under `USBDRV\FREE`; `-nonfree` adds 812, all under `USBDRV\NONFREE`.
- **Against `releases\3.5.0.0\`.** In each download every file outside `USBDRV\` is there and the same, with nothing
  extra. The base download's `readme.txt` is the directory's; `-free`'s and `-nonfree`'s differ from it, as each
  names its own download. The four record files (`downloads.txt`, `drvpack.txt`, `otherpkgs.txt`,
  `wdmfree-pin.txt`) are in no zip, and no `portsw` file is in any zip or in the directory.
- **The sets.** `USBDRV\FREE\SHA256.TXT` (`509A46B8...`) is `downloads.txt`'s Free line and the installer's compiled
  sums; `USBDRV\NONFREE\SHA256.TXT` (`674BAFDE...`) is its NonFree line and the installer's compiled sums;
  `USBDRV\NUSB33\SHA256.TXT` (`F3239C4A...`) is `drvpack.txt`'s. Every file each one lists matches its line.
- **The readmes.** All three ASCII, CRLF only, no tabs, at most 78 columns, "Released 2026-11-14."; each names its own
  download, and those of `-free` and `-nonfree` the sets' versions (`v0.1.10`, `v1.4.0`).
- **Versions.** The 15 project INFs in each download carry `DriverVer=11/14/2026,3.5.0.0`. The version resource
  reads `3.5.0.0` on every `.sys` and `.mpd` and on `SETUP.EXE`, `XHCIWIZ.EXE`, `XHCIWZ64.EXE`, `XHCITR.EXE` and
  `XHCITR64.EXE`; the other tools and the two VxDs carry none, as in `3.4.3.0`.
- **`release-acceptance-test.md` section 2.** Every row read on the host passes (`step2-out.txt`); row 2.9's `/list`
  was read in the guests below.

### The release matrix

`scripts\vm-matrix\release-matrix.ps1` on the unpacked downloads (`pkg-base`, `pkg-nonfree`; their files' SHA-256 in
`logs\sums-*.txt`): QEMU 11.1.2 (`v11.1.2-244-g4b495b9`, TCG) for the base and `-nonfree` runs, and
`v11.1.2-8-g76e89a5` under WHPX for the two Windows 10 legs. Configurations in `cfg\`, launchers in `cmd\`, logs in
`logs\`. A failed leg was run again alone with `run-leg.ps1` from its starting point. Paths here are relative to
`E:\xhci98-scratch\w49\matrix\`.

18 legs, each with a PASS: install 3, list 0, uninstall 3, list-final 0. No leg was Manual, so no Manual leg is owed.

The base download (`rmA\runs\20261114-161020\`, 12 legs, 27.9 min of wall time):

| System | Leg | Verdict | Evidence |
|---|---|---|---|
| 98 SE | `w98se-console`, `w98se-window` | **PASS** both | `rmA\runs\20261114-161020\` |
| ME | `me-console` | **PASS** on a re-run; the matrix's run FAILED (below) | `rmA\runs\rerun-me-console\`; failed run `rmA\runs\20261114-161020\me-console\` |
| ME | `me-window` | **PASS** | `rmA\runs\20261114-161020\` |
| 2000 SP4 | `w2k-console` (playback) | **PASS**; `audio-play` and `urbs-malformed` PASS | `rmA\runs\20261114-161020\w2k-console\result.json` |
| 2000 SP4 | `w2k-window` | **PASS** | `rmA\runs\20261114-161020\` |
| XP SP3 | `xp-console`, `xp-window` | **PASS** both | the same |
| Vista SP2 | `vista-console`, `vista-window` | **PASS** both | the same |
| 7 SP1 | `w7-console`, `w7-window` | **PASS** both | the same |

The `-nonfree` download (`rmN\runs\20261114-161025\`, 4 legs, 20.1 min):

| System | Leg | Verdict | Evidence |
|---|---|---|---|
| ME | `me-nonfree` | **PASS** on the second re-run (812 of 812 files, list-removed PASS); the matrix's run and the first re-run FAILED (below) | `rmN\runs\rerun2-me-nonfree\`; failed runs `rmN\runs\20261114-161025\me-nonfree\`, `rmN\runs\rerun-me-nonfree\` |
| 2000 SP4 | `w2k-nonfree` | **PASS** | `rmN\runs\20261114-161025\` |
| XP SP3 | `xp-nonfree` | **PASS** | the same |
| 7 SP1 | `w7-nonfree` | **PASS** | the same |

Windows 10 under WHPX (`rmB\runs\20261114-161020\`, 2 legs, 6.3 min):

| System | Leg | Verdict | Evidence |
|---|---|---|---|
| 10 22H2 x86 | `w10x86-console` | **PASS**: ctrl-disable, ctrl-enable, ctrl-remove, ctrl-rescan and the copies after each PASS | `rmB\runs\20261114-161020\w10x86-console\` |
| 10 22H2 x64 | `w10x64-console` | **PASS**: cert-trust and test-signing PASS, then the same as 10 x86 | `rmB\runs\20261114-161020\w10x64-console\` |

**ME's failures, as seen.**
- `me-console` in the matrix: install 3, list 0, the stick seen and copied, uninstall 3; then the guest did not
  switch itself off after the uninstall ("no power-off 120 s after POWEROFF", the host sent Start, Shut Down, Enter),
  and COM1 stayed silent for 900 s, so list-removed and list-final were not reported. The re-run (16:35 to 16:41)
  passed every check.
- `me-nonfree` in the matrix: install 3, then no power-off after the install, the same keys sent, 900 s of a silent
  COM1; nothing after the install was read.
- `me-nonfree`, re-run 1 (16:30 to 17:00): install 3, list 0, the stick seen and copied, uninstall 3; at the start
  after the uninstall the guest reached the desktop with the poller's window open and blank (`b3-stalled.png`), no
  BOOT line in 420 s and COM1 silent for 900 s. This is the picture run 48 records for `3.4.3.0`'s `me-nonfree`
  (48.final).
- `me-nonfree`, re-run 2 (17:02 to 17:08): every check PASS; the installer's log has "812 of its 812 files are in"
  for the non-free set.
- Judgment: the host's load, as in run 48, not the package: when the x64 legs below started at 16:13 the host ran
  15 other QEMUs, and these three failures fell in that hour. Not read further.

**The TCG cross-check of the Windows 10 legs.** The same two legs with `-ForceTcg` (`rmB\runs\20261114-161655\`) were
started at 16:16 and stopped at the owner's word at 16:50 (`logs\stop-bt.log`): by then 10 x64 had passed every
step up to and including ctrl-remove under TCG (`w10x64-console\serial-b2.log`), and 10 x86 had reached its
install's exit 3. So the remove step of these legs is read under WHPX alone; W10TCGCHECK.1 is planned for it.

### The `-free` download in guests

`E:\xhci98-vm\p49\dlcheck\`, the `-free` download unpacked by the host read above, a configuration and starting points
of its own (`E:\xhci98-scratch\w49\dlcheck\release.config.psd1`, `free-legs.psd1`), one virtual processor.

| System | Leg | Verdict | Evidence |
|---|---|---|---|
| XP SP3 | `/wdmfree:stage=audio/uac2,net/cdcnet,net/ax88772` | **PASS**: install 3, list 0, uninstall 3, list-final 0, every check PASS; "144 of its 144 files are in", 3 INFs staged; `/list` "driver xhci98 3.5.0.0; Keep (same version)" | `runs\xp-free-1\result.json` |
| XP SP3 | the same, read by hand before any uninstall | the three staged INFs (`uac2` 0.1.11.0, `cdcnet` 0.1.4.0, `ax88772` 0.1.1.0), DevicePath unchanged; 4 files compared by `fc /b`, no differences | `hand\hxpfree\` |
| 98 SE | `/wdmfree:copy` | **PASS**: the same exits, every check PASS | `runs\w98se-free-1\result.json` |
| 98 SE | the same, read by hand | `C:\XHCI98\USBDRV\FREE` 144 files and its record; 5 files compared by `fc /b`, no differences | `hand\h98free\` |
| 98 SE | the base download, no NUSB on the guest | `/list` offered the storage row ("Enter would give [Y]"); the install put in NUSB 3.3's seven storage files, the hashes those of `drvpack.txt`; exit 3 | `hand\h98base\serial-h1.log` |

### The x64 systems outside the matrix

`E:\xhci98-vm\p49\x64legs\r35\<guest>\` (`serial-inst.log`, `serial-<g>a.log`, `serial-UNI.log`, `serial-<g>p.log`,
`pass.txt`); notes `E:\xhci98-scratch\w49\x64legs\NOTES.md`. The base download, checked by hash before it was
unpacked and in each guest's transfer folder; QEMU q35, TCG, two virtual processors (XP x64: one); fresh overlays of
the clean bases. The package's own `nt-x64\xhci98-test.cer` was put into Root and TrustedPublisher inside each guest
alone, the method of the earlier x64 check above; nothing was signed. Setup: `INSTALL.BAT /console /yes /nopause
/thunderbolt /stor98`. The check boot: `/list`, the installed files against the package, a SuperSpeed stick at
power-on copied and compared, the USB4 enclosure (the ASM4242 model; on XP x64 the Alpine Ridge model) plugged in by
the host and its NVMe drive copied and compared, `XHCITR64 /eject` of it, unplug and replug and the copy again, the
installer's second run, a timed shutdown. Then the uninstall and a check boot after it.

| System | Install | The check boot | Uninstall | Evidence |
|---|---|---|---|---|
| 7 SP1 x64 | exit 3, no publisher prompt; "xhci98 3.5.0.0, started", "xhcicm 3.5.0.0, started" | every `/list` line "3.5.0.0; Keep (same version)" but the router's (below); files the package's; stick and NVMe drive on Stor98 "no differences"; eject "It is now safe to disconnect the device.", replug "no differences"; second run exit 0, all Keep; shutdown 11 s | exit 3; after it the controllers "driver none" (7 has no USB 3 driver of its own) | `r7\` |
| Vista x64 SP2 | exit 3, no publisher prompt; the same lines | every `/list` line "3.5.0.0; Keep (same version)"; the rest as on 7; shutdown 11 s | exit 3; the controllers "driver none" | `rv\` |
| 8.1 x64 (no `/usb`) | exit 3, no publisher prompt; "xhcicm 3.5.0.0, started", Windows' own USB 3 driver kept | every `/list` line "3.5.0.0; Keep (same version)"; the rest as on 7, the stick on Windows' own stack; shutdown 16 s | exit 3; the router "driver none", the stick still on Windows' own USB 3 driver | `r81\` |
| XP x64 SP2 | exit 3, the "has not passed Windows Logo testing" warnings answered Continue Anyway; the same lines | every `/list` line "3.5.0.0; Keep (same version)"; the rest as on 7, Stor98's Found New Hardware walked first; shutdown 5 s | exit 3; the controllers "driver none" | `rx3\` |

**Seen beside the legs.**
- 7 x64's check boot: `/list` showed the emulated USB4 router under a new instance id as "Update"; the emulator's
  model changes that id at each start, and the earlier x64 pass saw the same. The report of the same boot showed the
  router on `xhcicm`, and the second run listed it "Keep (same version)". Judgment: the emulator, not the package.
- XP x64 counts from a third fresh overlay (`rx3`). The first (`rx`) is void: its check script hung in `wmic` at its
  first use on that base, and stopping that lane by mistake left a process that started the uninstall boot. The
  second (`rx2`, one virtual processor) hung the same way in `wmic`, which the scripts then skipped on XP x64; its
  next check boot was killed mid-leg at about 17:19, when the host stopped responding and was restarted
  (Kernel-Power 41, no bug check recorded). After the restart the package, the transfer folder and the bases were
  hashed again, unchanged.
- On XP x64 a base device's "could not find the software" wizard pages were closed by hand after the plug, and one
  Logo warning that had no focus was given it by hand. The harness, not the package.

**Not read.**
- 98 SE's DevicePath after the `-free` install (the batch that read it hung; the guest was stopped).
- USB audio on XP from the `-free` download (no earlier method binds `uac2` to QEMU's audio device).
- The installer's own sums hash on `downloads.txt`'s NonFree line.

The owner closed 49.final on 2026-11-14, after these legs. His own readings (the 98 SE or ME eject guard on his
hardware, his bench of 49.46 and 49.47) and the publishing are his, not recorded here.

QEMU processes: the matrix's in `logs\pid-check.txt` and `pid-check2.txt` (none alive at 17:08); the x64 legs' in
`E:\xhci98-vm\p49\x64legs\r35\pids.txt`; the `-free` legs' in `E:\xhci98-vm\p49\dlcheck\rm\pids.txt`.
