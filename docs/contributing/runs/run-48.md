# Run 48 - Release `3.4.3.0`

The run sheet for release `3.4.3.0` (Phase 48 of `roadmap-usb4.md`: the
Thunderbolt chains, the tools' and installer's follow-ups, and the audit's
rest). This sheet records what each task did and every reading; the
roadmap's task list and decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

## 48.25 A USB keyboard in 2000 and XP text-mode Setup on an xHCI-only machine: the driver as it stands (2026-11-09, w48-inst)

The owner's instruction (2026-11-09): check whether the driver already
works as it is, before any design. Development host A, the pinned
emulator `asm4242-20261025-2230` (QEMU 11.1.2, SeaBIOS rel-1.17.0), TCG,
one vCPU, 512 MB, headless; a blank disk on IDE, the CD on IDE, a
`qemu-xhci` with a `usb-kbd` at its root port 1 and nothing else on USB,
the PS/2 controller present (NTDETECT needs it); QEMU traced
`ps2_keyboard_event` and `usb_xhci_xfer_success`
(`E:\xhci98-vm\p48\inst\kbd\`, `leg.ps1`; XP on q35 with the system disk on
a `piix3-ide` and an `isa-fdc` floppy, 2000 on i440FX as 47.15's 2000 legs,
since 2000's text mode stops on q35's ACPI HAL). The F6 floppy is a
read-only VVFAT folder holding the `3.4.2.0` release's `nt-x86\`
`txtsetup.oem`, `xhci98.sys` (`6575E65E...5EDF`), `xhci98.inf` and
`xhcihlpr.sys`: this branch changes no driver since that release
(`git diff v3.4.2.0 -- src` is empty), so it is the driver as it stands.
For the "files from CD" option the floppy also holds `USBD.SY_`,
`HIDCLASS.SY_` and `HIDPARSE.SY_` from the medium's own `I386\`
(hashes in `kbd\floppy-hashes.txt`, `floppy-xp2-hashes.txt`). Every key
was sent with QMP `send-key`; each start and stop is in
`E:\xhci98-vm\p48\inst\pids.txt`.

| Leg | Medium, option | Result |
|---|---|---|
| a (control) | XP SP3 VL and 2000 SP4, no F6 | Welcome to Setup; ENTER: the page unchanged, no PS/2 event and no transfer after the loader (`xp\a-welcome.png`, `w2k\a-welcome.png`) |
| b, text mode | XP, `XHCI98`; 2000, `XHCI98W2K` | F6 taken through SeaBIOS's USB keyboard, the specify screen, S, ENTER at the disk (2000), the option list, the option, ENTER; Welcome ENTER to the licence, F8 to the partition list, ENTER to make the partition, ENTER for NTFS: every key after the kernel's start two interrupt-IN completions (`len 8`, press and release), 0 PS/2 events in the whole leg (`b1` to `b5`) |
| b, 2000 first GUI-mode boot | `XHCI98W2K` | no STOP; Installing Devices finished by itself; Regional Settings: ENTER to Personalize, "Test User" TAB "xHCI98" typed, ENTER to the product key page, every key two completions, 0 PS/2 (`w2k\b8` to `b11`) |
| b, XP first GUI-mode boot | `XHCI98` | Installing Devices stops at "Software Installation ... has not passed Windows Logo testing ... Do you want to continue installing the software?" (`xp\b8.png`); ENTER: no transfer on the keyboard's endpoint at all, the question stays (`b9`, `b10`, `b11`, 25 minutes) |
| b, XP disk read offline | | `WINDOWS\system32\drivers\` holds `hidusb.sys`, `kbdhid.sys`, `kbdclass.sys` and `xhci98.sys`, and no `hidclass.sys`, `hidparse.sys` or `usbd.sys` (`7z l` of the stopped disk); `setupapi.log` ends with the class installs before the question |
| c, XP | `XHCI98W2K` with XP's own three files | text mode as in b; the first GUI-mode boot: a key is two completions from the start; at Installing Devices three questions, each answered from the keyboard (Left arrow to Yes, ENTER): "Software Installation" with no name (`c6`, `c8`, `c9`), then "Hardware Installation" for "xHCI98 USB 3 Controller (Red Hat QEMU xHCI)" (`c13`) and for "xHCI98 USB 3 Root Hub" (`c15`), after which the keyboard was enumerated again under the installed controller and went on working; Regional and Language Options ENTER, Personalize typed "Test User" TAB "xHCI98" (`c18`, `c19-typed`); 0 PS/2 events in the whole leg |

What it says:

- **2000: works as it is**, with the `XHCI98W2K` option (design record 13
  section 5.6, limit 7) and the three files from the 2000 CD.
- **XP: text mode works as it is; GUI mode does not with the `XHCI98`
  option.** XP's text mode lays down Setup's own `hidusb.sys` and
  `kbdhid.sys` for the keyboard it drove but not `hidclass.sys`,
  `hidparse.sys` or `usbd.sys`, which they import (record 13 section 5.6
  read the same on 2026-10-04), so after the text-mode restart the USB
  keyboard has no driver until Installing Devices has finished, and
  Installing Devices waits at XP's unsigned-driver question for a key.
  Only a PS/2 or built-in keyboard answers it.
- **XP with the `XHCI98W2K` option and XP's own three files works**: the
  keyboard is alive in GUI mode and answered the question. No driver
  change is needed; the option's name and the documents say "Windows
  2000" only. The owner's question is in `QUESTIONS.md` (Q1): rename the
  option to cover XP (recommended), a third option, documents only, or
  nothing.
- Not read: XP x64 (its `txtsetup.oem` has no "files from CD" option),
  and a real BIOS's PS/2 emulation across the hand-off (47.15).

## 48.2 Setup's refusal while Windows is installing something names the program that was run (2026-11-09, w48-inst)

`nothing_pending` (the NT check before the first change: Windows still
installing something after a minute, usually a Found New Hardware window
waiting) and its "devices kept changing" refusal said "run INSTALL.BAT
again" whichever program was run. They now name it through `run_name`:
`SETUP.EXE` in the window (which `SETUP.EXE` starts, and `INSTALL.BAT`
with no `/console`, the same run), `INSTALL.BAT` on the console tool, and
"the uninstall" for an uninstall (reviewed and revised: the window an
Add/Remove Programs entry starts runs from the install folder, where no
`SETUP.EXE` is). Commits `96915ee5`, `d93119be`.

## 48.9 build-driver.cmd stor98: Stor98 built and gated alone (2026-11-09, w48-inst)

`scripts\build-driver.cmd stor98 [-amd64]` (the word in the flavour's
place): the text checks, then `build-steps.ps1 -Stor98Only` (Stor98's INF
gate and interface header with their self-tests and, on x86, its unit
suite), then the Stor98 build step 14a makes (win9x `stor98.mpd` and x86
`stor98.sys`, or the amd64 `stor98.sys`), each image gated as step 14a
gates it (`:gatestor98`: hash, `check-stor98-build.ps1`, capture, source
stamp) once for each flavour's record. A gated record of this
architecture is amended in place (`write-gated-manifest.ps1 -Begin -Amend`
and `-Finish -Amend`: its Stor98 hashes replaced, the rest kept) only when
it was written at the run's commit from a clean tree and the tree is
still clean at that commit; any other record is deleted, since it names
Stor98 images the tree no longer holds; with none, nothing is written.
`write-gated-manifest.ps1` refuses another image name in an amending run
and a `-Finish` without `-Amend` on a record begun with it. Self-tests:
`build-steps.ps1 -SelfTest` checks the Stor98-only table per
architecture; `test-package.ps1` runs the writer eleven times over a
fixture repository (amended, deleted, the two refusals). Commit
`b31b6f9e`. The run itself is read in the gated build of this branch
(below) only as far as its tables; a real `stor98` run is owed to the
next cut tree.

## 48.10 pre-cut.ps1 (2026-11-09, w48-inst)

`scripts\package\pre-cut.ps1 [-WdmFreeRoot <checkout>] [-TwoTrees
[-X64Tree <tree>]] [-Plan]`: in the cut's tree after the version bump is
committed, the text checks, `src\pfw\build.cmd`, `xhciqual\build.cmd`,
`xhcimark\build.cmd`, `xhciinst\build.cmd`, `xhciinst\di\build.cmd`, then
`build-driver.cmd all` and `all -amd64` here one after the other (or
`cut-builds.ps1`), then `make-release.ps1 -CheckOnly` (with
`-WdmFreeRoot`), stopping at the first failure, each step's log in
`out\pre-cut\` and its own TEMP; then it prints the cut's command. It
refuses before building anything on a dirty tree, a version whose
`releases\<version>\` exists, a held gated lock or pending record, and,
while `wdmfree-pin.txt` names a release, a usb-wdm-free checkout
(`-WdmFreeRoot` or `scripts\local\usb-wdm-free`) that is missing or does
not hold the pinned tag at the pinned commit. `-SelfTest` (15 checks, in
the build's no-DDK steps) runs it over fixture trees under the self-tests'
git policy with stand-in steps; `-Plan` was read in this worktree, which
it refused (uncommitted changes, `releases\3.4.2.0` there, no
`scripts\local\usb-wdm-free`). Not run against the main tree (the
coordinator's instruction). Commits `57ba5d9b`, `3f6185a3`;
`build-and-test.md` and `releases\README.md` say how it is used.

## 48.12 The scripts: the upload set's family rule and the key's path (2026-11-09, w48-inst)

- The family rule: `Assert-FamilyLayoutVersion` (`package-common.ps1`),
  called by `New-UploadSet` and the `-UploadSetOnly` path: a tree laid out
  by Windows family is a version from `3.4.0.0`, the first cut that way; a
  family tree of an earlier version is refused before anything is
  assembled. The other direction is not held: the packager's stand-in
  trees are flavour trees at this tree's version.
- The key's path: `Get-TestSignPfxFault`, in `make-package.ps1` and
  `make-release.ps1` before the password is asked: a `.pfx` inside the
  repository is read only under its git-ignored `scripts\local\`, never a
  file git tracks there, and never inside a folder the run fills for
  publication (`-OutDir`; `-ReleasesDir`, `-UploadDir`, `-PackageRoot`).
  Paths are compared as written, as `Resolve-DirectoryArgument` compares
  its own: a net for a key named at the wrong path, not a boundary
  against an alias.
- Self-tests in `test-package.ps1`, no key used. Commits `cbda3267`,
  `3839d15c`.
- Left for the owner's session with the key: see the final report (the
  password on SignTool's command line, and a cut with the real key
  layout).

## 48.5 The 98 SE uninstall's XHCIUAS\ pass and a unit of several LUNs, read on ME (2026-11-09, w48-inst)

Development host A, QEMU (the pinned `asm4242-20261025-2230`), q35 `me`
base, overlay `E:\xhci98-vm\p48\inst\me5`, one vCPU, 512 MB. The kit
(`E:\xhci98-scratch\w48\inst\kit\PKG`) is the `3.4.2.0` download
(`out\upload-3.4.2.0`; its drivers are this branch's, which changes none)
with this branch's installer images put in: `XHCIINST.EXE` `b299cf2a...`,
`XHCIWIZ.EXE` `62116762...`, `XHCIDI.EXE` `d99a7f57...`, `SETUP.EXE`
`143d256e...` (built from `70ce0512`), `98-me\xhci98.sys` `6575e65e...`,
`98-me\xhciuas.sys` `17f0440e...` (`E:\xhci98-vm\p48\inst\kit-hashes.txt`).
Captures by `regedit /e` and `type ... > COM1`
(`E:\xhci98-vm\p48\inst\me5\ev\`).

| Step | Result |
|---|---|
| `INSTALL /console /yes /nopause` | exit 3, "Done. Please restart Windows now."; the note "On Windows ME, plug in any USB stick once before the first UAS disk" (`s9-install.png`) |
| cold start | the root hub installed by itself |
| a stick (`usb-storage`, serial W48STICK1), then a UAS device (`usb-uas`, two `scsi-hd` at LUN 0 and LUN 1, `attached` set after both) | both installed with no question; capture U1: `Enum\XHCIUAS\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\27842-0000:00:1C.0:00.0-3&0` and `...&1`, `Enum\SCSI\QEMU____QEMU_HARDDISK___2\XHCIUAS&...&27842-0000:00:1C.0:00.0-3&000` and `...&100`, and the stick's `USBSTOR&...&W48STICK1&000` (ME's own storage) |
| both unplugged; `INSTALL /console /uninstall /yes /nopause /log C:\UN.LOG` | exit 3. The package's devices listed: the controller, the root hub, "xHCI98 USB Attached SCSI Storage (xhciuas, not plugged in)" and two "USB Disk (USBMPHLP, not plugged in)" (the two `XHCIUAS\` units, ME's `USBMPHLP` above them). Removed, in order: the LUN 0 unit's `Enum\SCSI\...&000` disk, the LUN 0 unit, the LUN 1 unit's `...&100` disk, the LUN 1 unit, the UAS device, the root hub, the controller; the two INFs in `INF\OTHER` deleted, each "Removed Windows Setup's record of it (SetupX OEM Name)" (`ev\UN-log.txt`, `s16-uninst.png`) |
| cold start; the controller's wizard: Next, "unable to locate the software", Finish; capture U2 | **pass**: no `Enum\SCSI` entry of either XHCIUAS unit, no `Enum\XHCIUAS` unit and no key of the UAS device; the stick's (ME's `USBSTOR`) entries kept, as they are not the package's; no SetupX `OEM Name` value naming `xhci98.inf` or `xhciuas.inf` (`ev\U2-after.reg.txt`) |

Seen: an empty `Enum\XHCIUAS` key stays after its last unit is removed
(no value, no subkey; it was not there before the install). Harmless;
noted for the owner.

## 48.19 The installer's audit rest (2026-11-09, w48-inst)

- **Stale error numbers after the copy chains** (`70ce0512`): a message
  after `CopyFile`, the comparison and further steps named whatever
  error the last call left. `copy_same` takes the copy's own
  `GetLastError()` at once and tells a copy that compares different;
  `copy_err_text` words it ("error <n>", "the copy is not the same
  bytes", "the copy is not this release's version"); used by
  `own_put_as`, the two Stor98 INF sweeps and the 9x `stor98.mpd`
  placement, and `own_put_as` and `own_delete` take each failing step's
  error before the clean-up calls.
- **Unused parameters** (`70ce0512`): `vsd_do`'s `file`, `other_remove`'s
  `uninstall`, `inst_eng_other_choose`'s `stage` (with `xhciwiz.c` and
  `inst_eng.h`) and `own_src`, gone. Kept on purpose: the callbacks'
  (`other_*_line`, window and dialog procedures) and the pure half's
  decision functions in `inst_core.c` whose host vectors pass every input
  (`inst_usb_decide`'s `nt`, `inst_tb_alone`'s `optCmOnly`,
  `inst_fast_default`'s three, `inst_di_outcome`'s `job`,
  `inst_other_sentence`'s `nt`).
- **The dead window resources** (`70ce0512`): `IDC_SUM_S98`, defined in
  `xhciwiz_res.h` and used nowhere; every other id of `xhciwiz.rc` is
  used.
- **The 98 SE `WININIT.INI` read-back** (a reading, no change): a scratch
  probe (`E:\xhci98-scratch\w48\inst\probe\iniprobe.c`, MSVC 6.0, not
  tracked) wrote `[rename] NUL=C:\XHCIPRB.TMP` with
  `WritePrivateProfileString`, read the file raw before and after the
  flush call (`WritePrivateProfileString(NULL, NULL, NULL, ini)`), took
  the line out and read again. 98 SE (`w98\s2-iniprobe.png`): the new
  line is not in the file before the flush and is after it; the deleted
  line is still in the file before the flush and gone after it; the
  flush call returns 0 (FALSE) though it flushes. ME
  (`me5\s3-iniprobe.png`): the same, but for a new file, whose first line
  is there before the flush. The installer flushes before every raw
  read-back and does not test the flush's answer (`sys_9x_queue`,
  `sys_9x_take_back`), which is right on both.
- **The DOS box's wait and ERRORLEVEL** (a reading, then `efe6bf03`):
  from an MS-DOS Prompt, `INSTALL /console /bogus` ran the console tool
  in the box and left ERRORLEVEL 1 or more, on 98 SE and ME; `INSTALL
  /bogus` (the window) returned to the prompt at once, with the window's
  "unknown argument: /bogus" still open, and left ERRORLEVEL 0, on both
  (`w98\s4`, `s5`; `me5\s5`, `s6`): COMMAND.COM does not wait for a
  Windows program. With `start /w` before the window's line (typed on ME,
  and as the batch on 98 SE, `w98\s6`, `s7`) the box waits until the
  window ends and ERRORLEVEL is 1 or more after it. `INSTALL.BAT`'s 9x
  window line is now `start /w`; its header already said it waits.
- **Very long HardwareID, CompatibleIDs and Driver values**: the design
  is record 23 section 5.3.1 (reviewed and revised three times: a rule
  that judged a trimmed list by prefixes, and a `Driver` left empty, were
  withdrawn for holding the whole value, an overflow copy each record
  owns read through one accessor, and `Driver` at 511 characters with
  every reader and path sized for it). Not implemented in this phase:
  see the final report.

## 48.11 The packager's self-tests in five parts at once (2026-11-09, w48-inst)

`test-package.ps1` is now five parts, each a block of the script
(`package`, `upload`, `download`, `cut`, `sign`), which a run that is not
skipped starts at once as children (`-Part <name>`), printing each log
whole with its exit, checks and time and recording the pass when all
five passed; `-Serial` keeps the old one-process run. Each part says its
slowest sections. Commit `8561d58c`; `build-and-test.md`, "The
packager's self-tests in five parts at once".

Measured on development host A, `-Force`, this worktree, **under heavy
load** (three workers' gated builds and about a dozen guests at once:
processor load 97 per cent), so the times are each other's, not a quiet
host's:

| Run | Result | Time |
|---|---|---|
| serial, before (`tp-base.log`) | 1872 checks | 26:59 |
| serial inside the gated build (`out\steps-x86\packager.log`) | 1885 checks | 28:17 |
| four parts (`tp-par1.log`) | 1900 checks; upload 18:22, package 10:05, sign 3:10, cut 2:54 | 18:24 |
| five parts (`tp-par2.log`) | 1939 checks; upload 16:23 | 16:25 |
| five parts rebalanced (`tp-par3.log`) | 1939 checks; upload 10:41, package 8:57, download 8:38, sign 2:59, cut 2:40 | **10:43** |

The parts' checks are the serial run's plus the common setup's counted
in each part (its fixtures and the baseline flat package). The upload
part's slowest sections alone (`tp-upload.log`): the connection
manager's four files 207 s, the x64 directory's signatures 189 s, the
family tree's upload set 130 s, the three downloads 89 s.

Not done: running cases in this process instead of a child (each case
runs the fixture repository's `make-package.ps1` or `make-release.ps1`,
which end with `exit`, set their own preference and strict mode, and
start the gates as processes of their own; the grandchildren are most
of the cost), and running only the parts a change touches on ordinary
builds (every case runs the fixture repository, a copy of the whole
packager set, so which part reads which file is not known without a
read trace).

## The w48-inst builds (2026-11-09)

- Before the build: `src\pfw\build.cmd`, `xhciinst\build.cmd` (all five
  images), `xhciinst\di\build.cmd`, `xhcimark\build.cmd`, each exit 0.
- Gated x86 `release` and `qemu` (`build-driver.cmd all`) of `70ce0512`
  with the then uncommitted design section and the parts' first edits in
  the tree (the packager step had read the serial suite before them):
  **BUILD + GATES PASSED**, the packager self-tests 1885 checks (28:17
  under the load above), the host suites 0 failures, the xhciinst,
  xhcitr, xhcimark and xhcidi self-tests passed. Log
  `E:\xhci98-scratch\w48\inst\logs\gated-x86.log`.
- No `-amd64` run: no driver or amd64-only file changed; the x64
  installer images are built and gated by `xhciinst\build.cmd` and its
  self-test.
- `efe6bf03` (`INSTALL.BAT`) and `8561d58c` (the parts) came after the
  build: the batch was read on 98 SE as `INST2.BAT` (above), and the parts
  passed 1939 checks three times (`tp-par1` to `tp-par3`).

### Merge

Merged into `3.4.3.0` at `a52d8fd5` (2026-11-09), with no conflict; the
merge reviewed, nothing material. The owner's decisions the same day:
48.25 option A (the "files from CD" F6 entry renamed to cover 2000 and
XP), 48.11 accepted as done at the five parts, and for 48.12 the
password typed at the hidden prompt, a set but empty
`XHCI98_TESTSIGN_PW` to mean the prompt, and SignTool's `/p` accepted and
recorded in design record 19; these, and 48.19's code, continue on
`w48-inst2`.

## 48.4 XHCITR64's timing note on a processor with an invariant counter (2026-11-09, w48-tools)

Was TRTSCLEFT.1 (47.6's rest). The vehicle: a fresh overlay of the q35 7 x64 base, the published
`3.4.2.0` drivers (`releases\3.4.2.0\nt-x64`, re-signed in the guest with a throwaway certificate
made there, test signing on) and the tools built from `c3a65f1d` (their sources are `3.4.2.0`'s:
`git diff f30a8f32 c3a65f1d` touches documents only), installed with `INSTALL.BAT /console /yes
/nopause` (exit 3, then a cold start). `XHCITR64.EXE` SHA-256 `9739c64c...0c1f6c9b`, `XHCITR.EXE`
`4a90f7ed...96b53e5b`; the running `xhci98.sys` (the in-guest re-signed copy) `4e60c294...761d28dd`.

QEMU's TCG refuses the bit (`-cpu qemu64,+invtsc,enforce`: "TCG doesn't support requested feature:
CPUID[eax=80000007h].EDX.invtsc [bit 8]"), so the invariant leg ran under the host's hypervisor
platform: `-accel whpx -cpu qemu64,+invtsc,enforce -smp 2`, the same pinned emulator binary. The
control is the same overlay under `-accel tcg,thread=multi -cpu qemu64`.

| run | `XHCITR64.EXE /dump` (native) | `XHCITR.EXE /dump` (WOW64) |
|---|---|---|
| WHPX, `+invtsc` | transfer-size heading: "the time-stamp counter at 2997.0 counts a microsecond", the two-processor line, and no "not invariant" line | the same |
| TCG, no `invtsc` | the same heading and "(this processor's time-stamp counter is not invariant: its rate changes with the processor's speed, so every time in this section is approximate)" | the same |

So both images' `CPUID` reads (the compiler's `__cpuid` on x64, the inline `0F A2` on x86 under
WOW64) report the bit as the processor gives it, and the note follows it. Evidence (on the
maintainer's disk): `E:\xhci98-scratch\w48\tools\evidence\48.4-T64W-whpx-invtsc.TXT`, the COM1
logs `w7x64-serial-b.log` (WHPX) and `w7x64-serial-c-tcg.log` (TCG). PASS.

Rig note: under WHPX a guest restart left QEMU in "prelaunch" ("WHPX: Unexpected VP exit code
4"); a cold start (shut down, launch again) is the route.

## 48.6 The tools: XHCIPERF's outstanding request at exit, XHCITR's hand-over to XHCITR64 (2026-11-09, w48-tools)

Was AUDLEFT.6. The same 7 x64 guest and kit as 48.4 (WHPX), a 1 GiB QEMU `usb-storage` stick on
the xHCI98 controller's port 3, formatted FAT32 as S:.

**XHCITR.EXE handing over to XHCITR64.EXE** (`tools\xhcitr\` of the kit, both images side by side,
an elevated prompt):

| mode | result |
|---|---|
| `/report C:\H\R32.TXT` | exit 0; the report's system block "64-bit Windows (x64); this x64 tool ran natively" |
| `/set /show > file` (redirected) | exit 0; the settings list in the file (the console handles handed on) |
| `/set XhciInterruptMode=1` | exit 3 (passed back), "XhciInterruptMode set to 1 - interrupt only." and the restart line; set back with `=default` (exit 3) |
| `/setlevel xhci98=2` | exit 0, "xhci98: XhciLogVerbosity set to 2 - the log (default), for 1 controller." |
| `/eject id=USB\VID_0000&PID_0000\NONE` | exit 1 (passed back), "No USB device, and no device behind a Thunderbolt port, has the instance ID ..." |
| `/acpi C:\H\ACPI32` | exit 0; APIC, FACP, WAET, HPET, MCFG, DSDT, RSDT saved with TABLES.TXT (the x64 firmware route) |
| `/snapshot SN32` | stays in XHCITR.EXE: "64-bit Windows (x64); this x86 tool ran under WOW64" |
| the window (Run box, unelevated) | `XHCITR64.EXE` running, title "xHCI98 USB Tree 3.4.2.0", `XHCITR.EXE` gone (`tasklist /v`); screenshot `ho-win.png` |
| `XHCITR.EXE` alone in `C:\H32` (no `XHCITR64.EXE` beside it) | `/report`: exit 0, the WOW64 limits text on the console, the report written; the window: the same text in a warning box over the window (`ho-alone.png`), 47.23's untaken reading |

**XHCIPERF's outstanding requests at exit.** `XHCIPF64.EXE /run /folder S:\ /size 256 /time 60
/test rnd,64,32,4 /ops write /report C:\H\PF.TXT /exit` (32 requests outstanding on each of four
threads, `pf-run.png` mid-run), then `taskkill /im XHCIPF64.EXE` without `/f` (a `WM_CLOSE`) at
12:12:09.69; the process was polled every 0.2 s: running at 10.03 and 10.23, gone at 12:12:10.34.
The report: "Stopped: Stopped before the plan ended (Stop pressed, or the window closed).",
"Rnd 64K d32 t4 write aligned stopped", pass 1 40.5 MB/s. The test file was deleted (S:\ empty)
and no ownership record was left (`HKCU\Software\xHCI98\XHCIPERF` holds `Params` and `Folder`
alone). So on NT the close waits for every outstanding request, cancelled or completed, before the
buffers go and the process ends (record 29 section 4.4). Not read: 98 SE's and ME's case (no
cancel; the request in flight is waited for) and the exit's wait for an ASPI counter read still out
(record 29 section 6.2), which needs Stor98 with a stalled answer on 98 SE.

Evidence: `E:\xhci98-scratch\w48\tools\evidence\` (`w7x64-serial-b.log`, `ho-win.png`,
`ho-alone.png`, `48.6-pf-run.png`).

## 48.7 XHCITR's USB and Stor98 tabs and "Apply as administrator..." for a standard user (2026-11-09, w48-tools)

Was TRSETLEFT.1 (47.35's and 47.41's rest). The vehicle: a fresh overlay of the q35 7 x86 base
with `-Usb4 asm4242 -EncAtBoot` (the ORICO enclosure's NVMe at power-on), the same base kit as
48.4 (the published `3.4.2.0` x86 drivers, tools built from `c3a65f1d`: `XHCITR.EXE` SHA-256
`4a90f7ed...96b53e5b`, 565,248 bytes, as installed in `C:\XHCI98`), installed elevated with
`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98` (exit 3; both controllers on `xhci98`,
the NHI on `xhcicm`, Stor98 staged); after the restart `xhci98`, `xhcicm`, `xhcihlpr`, `xhcipfw`
and `stor98` running (`driverquery`; `xhci98.sys` `6575e65e...f3da5edf`, `stor98.sys`
`43b14a13...53e2b192`). A standard user `std` (Users only) was made, the administrator `test`
given a password, and Windows set to log `std` on: a separate login, not an unelevated
administrator. In `std`'s session (`whoami /groups`: Users, Medium Mandatory Level):

| step | result |
|---|---|
| XHCITR's window, View > Settings... | the USB tab's head "A change needs administrator rights: Apply asks for them."; Apply reads "Apply as administrator..." with the shield (`s5.png`) |
| USB tab: XhciInterruptMode 0 -> 2 - always poll | the row "-> 2 - always poll" (`s7.png`); Apply as administrator...: UAC's credential prompt for `test` ("unknown publisher", XHCITR.EXE, `s8.png`); password given: "XhciInterruptMode set to 2 - always poll." with the restart line and "Restart Windows now?" (`s9.png`), No; the head "Changed, waiting for a restart: XhciInterruptMode." |
| Stor98 tab | writable (Setup installed this Stor98), "this computer (every Stor98 drive)" (`s10.png`); StorPollMode 0 -> 1 - always poll, Apply as administrator..., the credential prompt (`s12.png`), "StorPollMode set to 1 - always poll." and the restart question (`s13.png`), No; the head lists both |
| `XHCITR /set /show` as `std` | "XhciInterruptMode: 2 - always poll; default 0 - automatic", "StorPollMode: 1; default 0" |

PASS. Evidence: `E:\xhci98-scratch\w48\tools\evidence\48.7\` (screenshots, COM1 log). Not read
here: quick save as a separate standard user (47.41's hotkey and menu ran as an unelevated
administrator, the same paths).

## 48.17 The USB drive notice (was TRSTORNOTE.1; 2026-11-09, w48-tools)

**The design.** Record 25 section 7.8 (revision 30), from the reviewed draft of 2026-11-07
(revised: the observation model, the identity and the delivery), reviewed and revised to
convergence before the code (`65fc5244`): the setting `StorageNotice` (per user, a tick box "USB
&drive notices" on the Settings window's tool tab, off by default); a drive is a connected port of
an `xhci98.sys` controller holding a storage record in the controller's query answer (section 19),
so a drive with no storage driver counts and no device request classifies it; the key is the
faster notice's key and the record's Slot ID; a failed read is unknown, never a removal; first
scans, a controller answering again and a sleep are taken silently; removals and arrivals are
queued and coalesced, sent from the 1 s tick after a 1.5 s gather, 5 s after any other balloon and
after the faster notice, the arrivals held for their devnode and 3 s (10 s at most) with the model
read again before their text; its own balloon kind, 98 SE pop-up id and re-send watch.

**The code.** `c312115f`, then the review's fixes `03a148bd`, `f06f087d`, `e6c6b654`, `cd39f529`
(the review converged at `cd39f529`, no material findings). The gated x86 build passed at
`cd39f529` (`gated-x86-cd39f529.log`); host suites `test_tr` 1727 checks, `test_trset` 1854,
`test_trred` 161, 0 failures.

**What the 98 SE leg found, and the fix.** On 98 SE without NUSB (the feature's main case) a
QEMU stick with no driver brought the tray no `DBT_DEVNODES_CHANGED` until its Add New Hardware
wizard was dismissed, and its unplug none at all: the arrival was announced only at the next
refresh (opening the window), the removal likewise (`48.17-98se-removal-at-refresh.png`; nothing
in 30 s before it, `48.17-98se-unplug-no-notice-before-fix.png`). `ed5a6fce`: while the notice is
on, each 1 s tick asks each `xhci98.sys` controller for its query header alone (88 bytes, no
device request) and asks for the refresh a devnode change asks for when the storage part's size
changed; `6df340d6` and `d0be0d34` (review): the baseline kept while the Settings window is open,
and a refresh at its close while the notice is on. Reviewed to convergence at `d0be0d34`.

**98 SE legs** (q35 98 SE overlay, the published `3.4.2.0` 98-me drivers installed with
`INSTALL /console /yes /nopause`, no NUSB, no storage driver; `XHCITR.EXE` of `ed5a6fce`, SHA-256
`a78db741...6fb19d14`, copied over the installed one; QEMU `usb-storage` sticks over read-only
`vvfat` folders, hot-plugged on the xHCI98 controller):

| step | result |
|---|---|
| the tool tab | "USB drive notices" under "Watch Windows' own USB ports too", unticked by default; ticked, Close |
| a stick plugged, the Add New Hardware wizard left open | the pop-up "USB drive connected", "QEMU USB HARDDRIVE is connected." about 21 s after the plug (the product string asked; no devnode the tree matches, so the 10 s hold) (`48.17-98se-arrival-poll-wizard-open.png`) |
| the wizard cancelled, the stick unplugged | "USB drive removed", "QEMU USB HARDDRIVE was removed." within 6 s (`48.17-98se-removal-poll.png`) |
| a hub, two sticks behind it | one pop-up "USB drives connected", "QEMU USB HARDDRIVE and QEMU USB HARDDRIVE are connected." (`48.17-98se-two-arrivals.png`) |
| the hub unplugged | one pop-up "USB drives removed", "... were removed." (`48.17-98se-hub-two-removed.png`) |
| unticked, a stick plugged | nothing in 30 s (`48.17-98se-off-no-notice.png`) |

**XP and 7 legs, on the reviewed tip's gated build** (`d0be0d34`; `XHCITR.EXE` SHA-256
`a73eaec7...6a16d5e8d` from the gated x86 run; the same base installs as above, the new
`XHCITR.EXE` copied over the installed one, the tray started again):

| system | step | result |
|---|---|---|
| XP SP3 (q35, 1 vCPU) | `StorageNotice` 1 set before the tray started; a stick plugged for the first time | Windows' "Found New Hardware" balloons (Disk drive, QEMU QEMU HARDDISK USB Device, "ready to use"); the tool's arrival balloon was **not seen** in the 70 s watched (`48.17-xp-firstplug-*.png`) |
| XP | the stick unplugged | "USB drive removed", "QEMU QEMU HARDDISK USB Device (D:) was removed." within 6 s (`48.17-xp-removal.png`) |
| XP | the stick plugged again | "USB drive connected", "QEMU QEMU HARDDISK USB Device (D:) is connected." about 8 s after the plug (`48.17-xp-arrival-replug.png`) |
| 7 x86, the standard user `std` | the tool tab (the window's View > Settings...): "USB drive notices" ticked, Close; Settings opened again, a stick plugged while it was open (Windows' "Your device is ready to use", then "Windows could not activate" held the area, `48.17-7x86-settings-open-plug.png`), Close with no change | once Windows' balloon was closed: "USB drive connected", "QEMU QEMU HARDDISK USB Device is connected." (`48.17-7x86-arrival-after-settings.png`) |
| 7 x86 | unplugged; plugged again | "USB drive removed", "... was removed." (`48.17-7x86-removal.png`); "USB drive connected" again (`48.17-7x86-arrival-replug.png`) |

On 7 the stick's volume had no drive letter (`wmic logicaldisk`: C:, D: ORICO, E: the transfer
disk): the two `vvfat` disks carry the same disk signature, which 7 takes offline; the name is
therefore the device's without a letter. That is the vehicle's, not the tool's; XP gave D:.

**Left.** XP's first-plug arrival: sent, as far as the tool can tell, while Windows' Found New
Hardware balloons held the area, and not shown; the drive balloon's re-send (section 7.8 item
10, section 7.7 item 9's rule) re-sends a balloon with no answer only where the shell has
answered in the session or the system is Vista or later, and on XP no answer had come yet. Not
traced (a build that logs the `NIN_BALLOON*` messages, as task 43.66's, would say which). A drive
re-plugged, and every removal, were shown. Not read: 2000 (a shell that reports no balloon), ME, a
sleep and resume with the notice on, a controller disable and enable (the silent rebase).

**Builds.** At the reviewed tip `d0be0d34`: `build-driver.cmd release` x86 and `-amd64`, BUILD +
GATES PASSED both (`E:\xhci98-scratch\w48\tools\gated-x86.log`, `gated-amd64.log`); `XHCITR.EXE`
`a73eaec7...6a16d5e8d` (the XP and 7 legs' binary), `XHCITR64.EXE` `94d995d1...6c8419` (not run in a
guest after the change; 48.4's and 48.6's x64 readings are of the `3.4.2.0` sources). Earlier, at
`cd39f529`, the x86 gated build passed too (`gated-x86-cd39f529.log`).

### Merge (w48-tools)

Merged into `3.4.3.0` at `f3cffa3b` (2026-11-09), with no conflict. The merge
review found two gaps in 48.17's notice, carried on `w48-dock`: a removal
missed when a stick leaves between the tray's first refresh and its first poll
on 98 SE, and an arrival retried from the model the Settings window holds.

## 48.12 The test-signing password: an empty variable means the prompt (2026-11-09, w48-inst2)

The owner's decision (2026-11-09): the password is typed by hand at the
existing hidden prompt. `Get-TestSignPassword` (`package-common.ps1`)
now takes `XHCI98_TESTSIGN_PW` set but empty as unset - the prompt, never
an empty password - and refuses an empty answer at the prompt ("no
password was typed for the test-signing .pfx; nothing is signed with an
empty password"), so no caller holds an empty password to sign with or
hand on; `make-release.ps1` also checks that before it sets the variable
for its `make-package.ps1` child (an empty value, which the child would
read as "ask" again, is never handed over). Design record 19 section 5
says that WDK 7.1's SignTool takes the password only as `/p` on its
command line, visible for the few seconds of each signing run to the same
user's processes that can read command lines, and that this is accepted
(owner, 2026-11-09). Self-tests in `test-package.ps1` (the upload part),
no key opened: `Read-Host` stood in by a function in a child scope; the
variable set, unset, and set but empty (passed as read through
`-FromEnvironment`, since .NET Framework deletes a variable set to "" and
reads one set through Win32 as "" or `$null` by the thread's last error,
read 2026-11-09), each with a typed and an empty answer: 11 checks; with
the old rule in a copy, 4 of them fail. Commit `b175dd33`.

## 48.25 The "files from CD" F6 option named for 2000 and XP, and read on both (2026-11-09, w48-inst2)

The owner's decision (2026-11-09): option A of `QUESTIONS.md` Q1. The
32-bit `txtsetup.oem`'s second option keeps its id `XHCI98W2K` and its
shape (`OEM-W2K`) and is now described "xHCI98 USB 3 Controller (32-bit
Windows 2000/XP, files from CD)", its disk `d2` "USBD.SY_ HIDCLASS.SY_
HIDPARSE.SY_ from the 32-bit Windows 2000/XP CD". The gate holds the new
wording (`OEM-DEFAULT`: the option names Windows 2000/XP and says "files
from CD"; `OEM-DISK`: `d2` names the Windows 2000/XP CD; three new
mutation cases; self-tests 256 cases, both files pass).
`INTEGRATE.TXT`'s F6 paragraph, record 13 section 5.6, issues 10 (status,
and a section 7) and 16, the issues index, `build-and-test.md`'s F6 leg
procedure, `legal-provenance.md` section 5 and `AGENTS.md` follow. Commit
`b3782299`. `README.md` and the release notes' current F6 sections still
say the entry is for Windows 2000 only: their new text is in the final
report for the owner.

Legs: development host A, the pinned emulator `asm4242-20261025-2230`
(QEMU 11.1.2, SeaBIOS rel-1.17.0), TCG, one vCPU each, 512 MB, headless;
the first worker's kit (`E:\xhci98-vm\p48\inst\kbd\`, copied to
`E:\xhci98-vm\p48\inst2\kbd\`, `leg.ps1`): a blank IDE disk, the CD on
IDE, a `qemu-xhci` with a `usb-kbd` at root port 1 and nothing else on
USB, the PS/2 controller present; QEMU traced `ps2_keyboard_event` and
`usb_xhci_xfer_success`. XP SP3 VL on q35 (system disk on a `piix3-ide`,
`isa-fdc` floppy), 2000 SP4 on i440FX. The F6 floppy (read-only VVFAT):
the `3.4.2.0` `nt-x86\` `xhci98.sys` (`6575e65e...`), `xhci98.inf`
(`add3bb0a...`), `xhcihlpr.sys` (`e4be5f12...`) - this branch changes no
driver since that release - with this branch's `src\txtsetup.oem`
(`d233ead9...`), and each medium's own `USBD.SY_`, `HIDCLASS.SY_` and
`HIDPARSE.SY_` from its `I386\` (`kbd\floppy-hashes.txt`). Keys by QMP
`send-key`; starts and stops in `E:\xhci98-vm\p48\inst2\pids.txt`.

| Leg | Result |
|---|---|
| both, the F6 list | F6 held through SeaBIOS's USB keyboard after a reset, S, (2000: ENTER at the disk), the list shows "xHCI98 USB 3 Controller (32-bit Windows 2000/XP)" and "xHCI98 USB 3 Controller (32-bit Windows 2000/XP, files from CD)" (`xp\b2-list.png`, `w2k\b2-list.png`); Down arrow, ENTER: "Setup will load support for ... (32-bit Windows 2000/XP, files from CD)" (`b4-load`) |
| both, text mode | Welcome ENTER to the licence, F8, ENTER at the unpartitioned space, ENTER for NTFS: every key two interrupt-IN completions; text mode copied with no prompt for `d2` (the three files on the floppy) and restarted |
| 2000, first GUI-mode boot | no STOP; Installing Devices finished by itself (`w2k\g2`); Regional Settings ENTER, "Test User" TAB "xHCI98" typed (`g5-typed`), ENTER to the product key page (`g6`) |
| XP, first GUI-mode boot | Installing Devices asked three questions, each answered from the USB keyboard (Left arrow to Yes, ENTER, each four or more completions): "Software Installation" with no name (`xp\c1-logo-q`), "Hardware Installation" for "xHCI98 USB 3 Controller (Red Hat QEMU xHCI)" (`c4-hw-ctrl`) and for "xHCI98 USB 3 Root Hub" (`c6-hw-roothub`); Regional and Language Options ENTER, Personalize "Test User" TAB "xHCI98" typed (`c9-typed`) |
| both, whole leg | 0 `ps2_keyboard_event`; 4,290 (XP) and 1,679 (2000) `usb_xhci_xfer_success` lines (`xp\trace-a.log`, `w2k\trace-a.log`) |

So the renamed option drives a USB-only keyboard through text mode and
the first GUI-mode boot on both, as the first worker read it under the
old name (its legs b on 2000 and c on XP). Not read: XP x64 (its file has
no such option), and the option on XP without a USB keyboard (no second
disk is needed there; `XHCI98` stays the default).

## 48.19 Very long HardwareID, CompatibleIDs and Driver values held whole: the code (2026-11-09, w48-inst2)

The code of record 23 section 5.3.1 (the first worker's converged
design). `DEVREC` keeps its 2,048-byte lists and gains an overflow copy
of each, owned by the record (`HwLong`, `CompatLong`), which a list
longer than the array is read whole into, the array left empty: on NT
`enum_ids` (the Enum key's `REG_MULTI_SZ` read again at its length,
`enum_value_whole`, at most 1 MB); on 98 SE and ME `ids9x` (the Enum
key's comma string read whole, `reg9x_value_whole`, split once; the
configuration manager's answer kept only where the registry proves the
value absent, asked again at its size when too big, `cm_prop_whole`,
and kept whole with its several strings, `inst_ids_commas`); the NT
Thunderbolt reader through `cm_prop_whole`, an answer read again checked
for a list's type (`inst_cm_ids_type_ok`). Every reader takes the lists
through `dev_ids` (`dev_has_id`, `dev_has_hw`), never a pointer kept
across the list's `realloc`. The copies are freed before a record is
overwritten or discarded (`devs_release` before each scan and at the
run's end, the Thunderbolt inventory before each reading, a dropped
registry-only candidate, the 9x Stor98 baseline's temporary record on
every exit). `Driver` is 515 bytes, held to 511 characters
(`driver_fits`, `enum_driver`, `reg9x_driver`), and so are the three
readers outside the record; every driver key path is built by
`inst_driver_key_path` into `INST_DRIVER_PATH_CHARS` (the longer 9x class
root, the separator, 511, the terminator) and refused, before any key is
opened, when it cannot be built. Host vectors (`test_inst`, 9,015 checks,
0 failures): the accessor, a comma list longer than the array split at
its final length, a configuration manager answer of several strings held
whole, the 9x decision (`inst_ids9x_ok`), the list types, the driver path
at 511 and 512 characters and one byte short. Commits `0c72f57e`,
`346ea0c2`; reviewed and revised five times (a failed configuration
manager read taken as an absent list; an answer of several strings cut to
its first; an answer read again of another type; then the 98 SE leg
below).

The legs: development host A, the pinned emulator, the q35 bases (98 SE,
ME, XP SP3), one vCPU, 512 MB, fresh overlays; the kit
`E:\xhci98-scratch\w48\inst2\kit\` - `OLD\` the first worker's 3.4.2.0
kit with the installer of `70ce0512` (before this change,
`XHCIINST.EXE` `b299cf2a...`), `NEW\` the same with this branch's images
of `346ea0c2` (`XHCIINST.EXE` `4adcedeb...`, `XHCIWIZ.EXE` `5704cf00...`,
`kit-hashes.txt`) - and a scratch fixture, `FIXLONG.EXE` (MSVC 6.0, not
tracked, `E:\xhci98-scratch\w48\inst2\fix\fixlong.c`): for every Enum
instance of the standard VGA (`PCI\VEN_1234&DEV_1111`; with an argument,
of another prefix) it appends 100 ids to `CompatibleIDs` (5,835 bytes),
and where the device has a `Driver`, copies its driver key to a new
instance key of 250 characters and points `Driver` at it (258 characters
on 98 SE and ME, `Display\W48LONG...`; 289 on XP); on 98 SE and ME it
also adds a registry-only `Enum\PCI\VEN_FFFF&DEV_FF01...\W48FAKE` entry
with a 5,745-byte `HardwareID`. On XP it ran as SYSTEM (`schtasks /ru
SYSTEM`). Each run's log was typed to COM1 (`w98\serial-*.log`,
`me\serial-a.log`, `xp\serial-*.log` under `E:\xhci98-vm\p48\inst2\`).

| System | Run | Result |
|---|---|---|
| 98 SE (run 1, `w98-run1`) | old `/list` | exit 1: "unreadable: a value of HKLM\Enum\PCI\VEN_1234&DEV_1111...\BUS_00&DEV_02&FUNC_00" and "...\W48FAKE", "2 device entries could not be read" |
| | new `/list` (`0c72f57e`) | both lists "held whole", but **exit 1: "unreadable: a property of HTREE\ROOT\0"** - the review's stricter rule (absent from the registry, only a configuration manager answer read or absent kept) met the root devnode, which has no registry list and whose answer is `0x1D` (`CR_NO_SUCH_REGISTRY_KEY`); fixed in `346ea0c2`: only a list too big that cannot be read again is unreadable, any other answer as before |
| 98 SE (run 2, fresh) | old `/list` | exit 1, 3 entries unreadable (the live VGA devnode `...DEV_01...`, the stale one, `W48FAKE`) |
| | new `/list` | exit 0; "HTREE\ROOT\0: no HardwareID in the registry; the configuration manager answered 0x0000001D" (and CompatibleIDs), three lists "held whole" (5,835, 5,835 and 5,746 bytes); the VGA devnode's `Driver` `Display\W48LONG...` read |
| | (the guest restarted itself after the list run, under `-action reboot=shutdown`; at the next start 98 SE had rewritten the VGA's Enum values and dropped `W48FAKE`, so the install there, exit 3, ran without the fixture: `serial-b-install-nofixture.log`) | |
| 98 SE (run 3, the same overlay, the driver installed and running) | fixture, new `/list`, new install in one batch (`serial-c.log`) | `/list` exit 0; install **exit 4**: the controller "Nothing to do: this version already runs it", and the steps by hand for a registry-only `PCI\VEN_1B21&DEV_2426...` entry the base image carries (no driver, no devnode: an ASM4242 slot from the base's making), the exit such an entry gives; in all five readings the three lists held whole and the root devnode as above, no entry unreadable |
| ME (fresh) | old `/list` | exit 1, 3 entries unreadable |
| | new `/list` | exit 0, the root devnode `0x1D` as on 98 SE, three lists held whole |
| | new install `/yes /nopause` | **exit 3**, "xHCI98 USB 3 Controller (Red Hat QEMU xHCI): installed, restart pending"; every one of the run's seven readings held the three lists whole |
| XP (fresh, pass 1) | fixture (VGA: `CompatibleIDs` 5,835 bytes; no `Driver` on either instance) | |
| | old `/list` | exit 1, "2 device entries could not be read" |
| | new `/list` | exit 0, both VGA lists held whole |
| XP (the same overlay, pass 2) | fixture on every `PCI\VEN_8086` instance (`CompatibleIDs` 5,784 to 5,835 bytes; `Driver` of 289 characters on the three with a driver: `DEV_1237`, `DEV_2918`, `DEV_29C0`) | |
| | new `/list` | exit 0, ten lists held whole, no unreadable entry |
| | new install | **exit 3**, the controller and root hub "xhci98 3.4.2.0, started" (XP's Logo questions and the root hub's Found New Hardware wizard answered) |

Not read: a `Driver` of more than 511 characters (unreadable, as before:
`driver_fits`), the configuration manager's too-big answer on 98 SE and ME
(the registry held every list here), the NT Thunderbolt reader with a long
list (no Thunderbolt host in these guests), and 2000.

## 48.34 The INF gate's self-tests in six parts at once, and the pre-merge skip (2026-11-09, w48-inst2)

`scripts\inf-gate\test-inf-checks.ps1` runs in six parts at once
(children with `-Part 1` to `-Part 6`, the pass recorded only when all six
passed; `-Serial` or `XHCI98_INFTEST_SERIAL=1` the one process of
before), each part calling the gate and counting checks only in the
sections the part table gives it; the registry values' section (35 values,
four gate runs each) is five sections of seven values. The owner chose
option (a): pre-merge gated builds set `XHCI98_SELFTEST_SKIP_OK=1`, so a
pass any worktree recorded in `%LOCALAPPDATA%\xhci98\selftest-pass`
skips the suite; `pre-cut.ps1` gives each step `XHCI98_FULL_SELFTESTS=1`
and clears `XHCI98_SELFTEST_SKIP_OK` (its self-test checks both).
`build-and-test.md`, "The INF gate's self-tests in six parts at once, and
the pre-merge skip". Commit `bd3a6d30`; reviewed and revised.

Readings on development host A (32 logical processors, other workers'
builds running: load about 80 % in the second), `-Force`, the same tree,
`TEMP` outside it:

| Run | Parts | Checks | Wall |
|---|---|---|---|
| One process (`runs/run-45.md`, 1,555 checks then) | 1 | 1,555 | 57:10 |
| Six parts, sections dealt by their place (no table yet) | 11:05, 4:54, 28:51, 9:15, 7:58, 8:10 | 168 + 105 + 578 + 476 + 310 + 121 = 1,758 | 28:52 (the registry values' one section 1,225 s) |
| Six parts, the table balanced by the first run's section times, the registry values in five sections | 17:22, 17:07, 17:11, 17:06, 15:33, 17:17 | 383 + 522 + 164 + 173 + 356 + 160 = 1,758 | 17:23 |

The sum of the sections' times was 4,072 s in the first run and about
6,200 s in the second (the host busier); the second's parts are within
two minutes of each other. Logs: `E:\xhci98-scratch\w48\inst2\logs\inf-par1.log`,
`inf-par2.log`. A passing run with the same inputs recorded in the store
makes a pre-merge gated build skip the suite (the gated build below).

The gated x86 build at `bd3a6d30` (the branch's tip, covering 48.12, 48.19,
48.25 and 48.34; `build-driver.cmd all` with `XHCI98_SELFTEST_SKIP_OK=1`,
slot `gated.2`, 13:50 to 14:06): exit 0, "BUILD + GATES PASSED (release
qemu)", the whole run 16:18. The INF gate's self-tests were skipped in
0:01 on the pass the second timing run recorded ("nothing they or the
gate read has changed since the run that passed at 2026-11-09T05:41:20Z
(1758 checks; digest 1d0d32538a93a492)"); the packager's self-tests ran
(12:37, the commit having changed their digest). Log
`E:\xhci98-scratch\w48\inst2\logs\gated-x86.log`.

### Merge (w48-inst2)

Merged into `3.4.3.0` at `44e8a39f` (2026-11-09), with no conflict; the merge
reviewed, nothing material. The other branches merge `3.4.3.0` in before their
next gated build (the coordinator, on the owner's 48.34) to take the faster
self-tests. 48.25's nLite leg continues on `w48-inst2`.

## 48.25 The nLite leg: XP SP3 with nt-x86\ integrated as Textmode, the "files from CD" entry, a USB keyboard only (2026-11-09, w48-inst2)

The kit: `make-package.ps1` and `ConvertTo-FamilyLayout` at `bd3a6d30`
(the merged tip's files; `E:\xhci98-scratch\w48\inst2\nlkit\nt-x86\`):
`txtsetup.oem` `4c7d9a96...`, `xhci98.sys` `4d9456e5...`, `xhci98.inf`
`add3bb0a...`, `xhcihlpr.sys` `3dc8e39c...`; then, as `INTEGRATE.TXT`
says, `USBD.SY_` (`1ba8c232...`), `HIDCLASS.SY_` (`ab4b4c4c...`) and
`HIDPARSE.SY_` (`d5f79f77...`) copied unchanged from the source's `I386\`
(`WinXP_pro_SP3_vl_x86.iso`, SHA-256 `fd8c8d42...`) into `nt-x86\`.

nLite: `nLite-1.4.9.3.setup.exe`, version 1.4.9.3, 3,092,150 bytes, not
Authenticode-signed, SHA-256 `3854c1b6c60f2f4a2b07c824661ea518444004c115c04f19415f0429e5b11e08`.
Its URL `http://www.nliteos.com/download/nLite-1.4.9.3.setup.exe` answers
404 now (the site serves NTLite's pages); the file was taken from the
Wayback Machine's captures of that URL (2014-02-09 and 2016-03-16,
byte-identical), into `E:\xhci98-dl-nlite\` (with `PROVENANCE.txt`; not
tracked). Installed and run only in a 7 SP1 x86 guest (the q35 base,
overlay `E:\xhci98-vm\p48\inst2\nl7\`, 2 vCPUs, 2 GB; the XP CD on the
guest's CD drive, copied to `C:\XPSRC`; the kit by the read-only transfer
disk; the ISO written to a FAT32 output disk read back on the host).

nLite: source `C:\XPSRC` (Windows XP Professional SP3, 5.1.2600.5512,
English); tasks Drivers and Bootable ISO; Insert, "Multiple driver
folder", `C:\NL\nt-x86`, All: four folders. For `nt-x86\` nLite offered
Textmode with the two entries in full ("xHCI98 USB 3 Controller (2000/XP,
USB keyboard/mouse, files from CD)" and "... (32-bit Windows 2000/XP)",
`nl7\shots\n14.png`); the first was chosen (`n15`); `cm\`, `cm\tbnet\` and
`stor98\` Regular PNP. Its session file: `xhci98.inf,XHCI98W2K`, the other
three `,0`. ISO `XPNL.ISO`, 619,315,200 bytes, SHA-256 `72969cf5...`.

What nLite wrote (`TXTSETUP.SIF`, `E:\xhci98-scratch\w48\inst2\nl\out\`):

- **The three files are taken.** `I386\NLDRV\001\` holds `USBD.SY_`,
  `HIDCLASS.SY_`, `HIDPARSE.SY_` beside the driver (disk 1984);
  `[SourceDisksFiles.x86]` has `usbd.sys`, `hidclass.sys` and
  `hidparse.sys` `= 1,,,,,,4_,4,1,,,1,4` (copied to `system32\drivers` in
  text mode) and `usbd.sy_`, `hidclass.sy_`, `hidparse.sy_` `= 1984`;
  `[SCSI.Load]` four lines all keyed `xhci98` (`xhci98.sys`, `usbd.sys`,
  `hidclass.sys`, `hidparse.sys`); `[HardwareIdsDatabase]` the five ids;
  `[FileFlags]` `xhci98.sys = 16`.
- **The description is cut at its first comma**: `[SCSI]` `xhci98 =
  "xHCI98 USB 3 Controller (2000/XP"`. nLite's own list showed the whole
  name; the cut is in what it writes to `TXTSETUP.SIF`, where only a text
  mode list of loaded adapters shows it. Nothing else read here depends on
  it.

The install: development host A, the pinned emulator, TCG, one vCPU,
512 MB, q35, a blank 8 GB disk on a `piix3-ide`, the ISO on IDE, a
`qemu-xhci` with a `usb-kbd` on root port 1 and nothing else on USB, the
PS/2 controller present, no floppy (`E:\xhci98-vm\p48\inst2\nlxp\`,
`leg.ps1`, the 48.25 kit's with the ISO changed; `xp\cmdline-a.txt`). Keys
by QMP `send-key`; QEMU traced `ps2_keyboard_event` and
`usb_xhci_xfer_success`.

| Step | Result |
|---|---|
| Text mode | no F6 pressed; Welcome (`xp\t1-welcome.png`) ENTER, the licence F8, the unpartitioned space ENTER (`t2-parts`), NTFS (Quick) ENTER; it copied with no prompt and restarted by itself (`t3`) |
| First GUI-mode boot | no STOP; Installing Devices finished with no question (`g1`, `g2`): `setupapi.log` installs `xhci98` on `PCI\VEN_1B36&DEV_000D` from `C:\WINDOWS\NLDRV\001\xhci98.inf` (`[Xhci.Dev.NTx86]`), the root hub `USB\XHCI98_ROOT_HUB`, the keyboard `USB\VID_0627&PID_0001` and `HID\VID_0627&PID_0001` as "HID Keyboard Device", every file `Policy=Ignore` (as run-45's L8) |
| Typing | Regional and Language Options ENTER (`g3`), Personalize "Test User" TAB "xHCI98" typed (`g5-typed`), ENTER to "Your Product Key" (`g6-key`): the volume-licence key page, where it stops (no key for this medium here) |
| Whole leg | **0 `ps2_keyboard_event`**; 624 `usb_xhci_xfer_success` (`xp\trace-a.log`) |
| The disk, read offline at the key page | `system32\drivers\usbd.sys`, `hidclass.sys`, `hidparse.sys` (XP's), `hidusb.sys`, `kbdhid.sys`, `xhci98.sys`, `xhcihlpr.sys`; `inf\oem0.inf` = `xhci98.inf` (`add3bb0a...`) |

So nLite takes the "files from CD" entry with the three files copied
into `nt-x86\`, and the integrated XP SP3 drives a USB-only keyboard on
an xHCI-only machine through text mode and the first GUI-mode boot, with
no key pressed for drivers. Not read: the first desktop and `SETUP.EXE`
after it (the key page), the "32-bit Windows 2000/XP" entry under nLite,
2000 under nLite.

## 48.8 the once-a-boot rule's host cases (2026-11-09)

Worker cm, branch `w48-cm` (from `c3a65f1d`), development host A.
`6a847b66` (reviewed and revised): `test_u4cm` `test_preos_once_more`,
which reads each start's `u4.preos.once` line (1 left whole, else the
failing test of record 22 section 9.3.1 in bits 15:8 and its port in
7:4):

| Case | Read |
|---|---|
| A mixed port: the record's USB3 HopIDs not the fabric's | `00000210` (test 2, port 1), the port cleaned, the cleaning hands `Cleaned` on |
| A mixed domain: port 1 this driver's own, a firmware's tunnel on port 2 | `00000220` (test 2, port 2), both ports cleaned (the start cleaned whole) |
| An inventory read that fails: the device's downstream lane's HopID 8 entry, its read swallowed where the inventory makes it (the last read of that entry in a control start, which was left whole) | the control `00000001`; then `00000310` (test 3, port 1), cleaned |
| A stray enable: Path Enable on the device's PCIe Down adapter | `00000310`, cleaned |
| The cleaned record on a version 2 host (`fabHostV2`, `FAB_DEV_USB4V2`, host interface version 40h) | A at 2 hands `Cleaned` with no reset (nothing to clean); B at 2 left whole (`00000001`), no Host Router Reset written or counted, nothing cleaned, port 1 UP and its own, the fabric's tunnel kept, `Cleaned` handed on |
| The same record with the stray enable | `00000310`, one Host Router Reset, no Downstream Port Reset, `Cleaned` handed on |

`test_u4cm` 1984 checks, 0 failures; the gated x86 build of `c18e9679`
ran every host suite ("Host tests PASSED"). Record 22 section 9.3.1
names the vectors.

## 48.24 a version 2 host's Host Router Reset at value 2 (2026-11-09)

**The emulator change first** (`usb4-emu` branch `w48-cm`, `de1ed69`;
its QEMU tree's `cf09993`). USB4 Specification 2.0 section 6.10: a Host
Router Reset "is confined to the Host Router within a USB4 Host" (the
Note) and the host router shall "Remove all Tunneled Paths" and
disconnect each Downstream Facing Port; nothing in it runs the
platform firmware. The model ran its pre-OS connection manager
(`preos-cm`) again at a guest's Host Router Reset, as at a power-on,
because `usb4_host_router_reset` called the power-on `usb4_fabric_reset`.
Now `fabric_reset` takes the kind of reset: a Host Router Reset (the
guest's, an injected `x-host-reset=router`, and the Thunderbolt 1 reset
packet) leaves `preos_window` closed and runs no pre-OS CM, so the
tunnels stay down until the OS CM builds them; power-on, system reset
and S3 wakeup are unchanged, and an ICM still resets with its host.
qtest `/usb4-tunnel-port/preos-hrr` (`minimal` and `intel-adl`): the
tunnel up after the power-on, the OS takes over, HRR written at
`0x39898`, the NVMe gone and still gone a second later; on the
baseline binary the same test fails at "should be FALSE" (the firmware
tunnelled it again). `usb4-tunnel` (32), `-retrain` (11), `-fault`
(9), `-tbhost` (13), `-tb3dev` (13), `-host` (46) pass.
`USB4-HOSTS.md` section 3, `FAULTS.md` and `TB-SWCM-HOSTS.md` say so.
Reviewed: nothing material.

**The leg** (7 x86, the released `3.4.2.0`, whose Host Router Reset path
this branch does not change: `xhcicm.sys` x86 `1f8929cf...`; the emulator
built from that tree, `E:\xhci98-vm\p48\cm\bin\qemu-system-x86_64.exe`
`396e4de4...`; run-45 "45.56"'s layout: `usb4-host host-model=minimal,
usb4-version=2,preos-cm=on,hi-version=0x40` on `pcie.0`, two
`usb4-tunnel-port`s, a `usb4-device dialect=usb4,usb4-version=2` with
an NVMe on each port at power-on; fresh overlay `v2\`, installed at
value 0 and `hi-version` 0 first, QMP 47961):

| Leg | Configuration, start | Read | Verdict |
|---|---|---|---|
| a | `hi-version` 0, value 0, the install's warm restart | two "USB4 (version 2.0), port 1/2, depth 1, approved", D: ORICO | install: pass |
| h4 | `hi-version=0x40`, value 2, a cold start | the trace: both firmware tunnels up at power-on, the OS takes over (`usb4_preos_end`), both down (admin), **one** `usb4_host_reset reset: host router reset`, **no pre-OS CM after it** (`usb4_preos_start` three times, all before `preos_end`), both tunnels up again (the driver's); `cm.select.caps=00400010` (host interface version 40h); the second start: both ports "PCIe up (found at start)", `u4.carry.in=00000001`, `u4.preos.once=00000001` (left whole), two Stor98 controllers and disks started, D: ORICO, `TESTDATA.BIN` SHA-1 `dacf51b2...838bba` | the version 2 cleaning through the Host Router Reset read: **pass** (on 3.4.2.0's emulator, 47.32's `h4`, the reset re-ran the firmware CM and the drives were lost) |

Evidence: `E:\xhci98-vm\p48\cm\v2\` (`trace-h4.log`, `serial-h4.log`,
`rep-a.txt`, `rep-h4.txt`, `cmdline-h4.txt`, `pids.txt`).

## 48.23 the chained drive lost after a cleaning (2026-11-09)

The vehicle of 47.32's legs (`E:\xhci98-vm\a347\cr4732\launch-cr.ps1`,
copied to `E:\xhci98-vm\p48\cm\launch-cr.ps1`): q35, the `intel-adl`
host on `00:04.0` with `preos-cm=on`, tunnel ports `00:05.0` and
`00:06.0`, a `usb4-device dialect=usb4,downstream-ports=1` on port 1 and
an ASM2464 child behind it with the NVMe, and the board's `qemu-xhci`
behind a `pcie-root-port` at `00:1C.0`; 7 x86, the released `3.4.2.0`
(`1f8929cf...`), my emulator (`396e4de4...`). Fresh overlay `ch\`,
QMP 47960.

| Leg | What | Read |
|---|---|---|
| a | install, value 0, warm restart | the chain present, D: ORICO |
| p2 | value 2, a cold start (47.32's case), with `pci_nvme_*` and `usb4_tp_*` traced | cleaned twice (test 4), the NVMe "PCI Device" **Code 28**; the chain port `tp0-sw3-usb4dp1` powered off by Windows and the endpoint reinserted three times (`usb4_tp_guest_unplug`, `usb4_tp_power_retry`); Stor98 never bound; `cm.chain.owed=02020101` then expired |
| hp0 | value 0, booted with no chain, the whole chain hot-plugged at once | **Code 28** too: not the cleaning |
| hp1 | as hp0, the dock with `chain-power-controller=off` | no slot churn, still **Code 28**; `cm.chain.ports=00020000`, `cm.chain.owed=00020101`: the chain walk ran on member 2 alone |
| - | XHCITR's member list | three tunnel ports: `00:05.0`, `00:06.0` and **`00:1C.0`, active** (the board xHCI's root port), the real port 1 "not active" |
| p2r | value 2, cold, the xHCI on `pcie.0` (`-XhciRc`, no root port above it) | two tunnel ports now, still **Code 28**; the walk on member 1 only |
| hp2 | value 0, `-XhciRc`, the child plugged 60 s after the dock | **Code 28**; `cm.reenum.member.flags=00002811`, `00003011` (exclusive, **no filter**); `reg query` in the guest: `UpperFilters xhcicm` on ten instances of `1B36:000C`, one per cold start (`48E123...28`, `8314...28`, `F6D6...28`, ...) |
| hp3 | as hp2, `usb4-host uuid=4a8f5f10-3c2e-4b7a-9d11-48ab48230001`, the first start with it | **Code 28** (a new instance once more) |
| hp4 | the same, the second start with that uuid | members `03` (active), `cm.chain.ports=00000001`, `cm.chain.invalidate=01020208`, **D: ORICO**, `TESTDATA.BIN` SHA-1 `dacf51b2...` |
| p2s | value 2, a cold start, the chain at power-on, `-XhciRc` and the fixed uuid | the cleaning (`u4.preos.once=00000100`), `cm.chain.invalidate=01020208`, no slot churn, **D: ORICO**, SHA-1 `dacf51b2...` |

**The cause is the test vehicle, in two parts; neither the driver's
rebuild of the chained tunnel nor Stor98.** (1) `usb4-host` with no
`uuid` draws a random one per QEMU process (`DEFINE_PROP_UUID`), and on
`usb4-generic` the NHI's Device Serial Number is that uuid's, mirrored
into the q35 tunnel ports' (`usb4-tunnel-port.c` `tp_machine_done`);
Windows builds the instance IDs from it, so every cold start met the
NHI and both tunnel ports as new devices, whose `xhcicm` upper filter
was not yet loaded when the connection manager started (`NO_FILTER`):
no member active, so the chain action for the child's tunnel found no
chain port and the chained NVMe never got the tunnel id (Code 28 on
3.4.1.0; on 3.4.2.0 47.32's run saw Stor98 bound but no disk, one of
the same race's outcomes). A preserved chain (value 0, the install's
warm restart in one QEMU process) never needs that action, which is why
value 0 kept the drive. (2) The board's `qemu-xhci` behind a QEMU
`pcie-root-port` sits behind `1B36:000C`, the id the connection manager
lists for the emulator's tunnel ports (`cm_ports.c` `cmEmuRoot`), so it
was a third tunnel port and the active one, and the chain action walked
it instead of `00:05.0` (that is also 47.32's side observation, "Behind
the Thunderbolt port ... 00:1C.0"; and 47.32's warm-restart leg `w2`,
which kept the uuid, failed through it).

**Fixed where it lies**: the launch. `usb4-emu` `702d4f1` puts it in
`XHCI98-HANDOFF.md` section 6 (a fixed `uuid=` on `usb4-host`; on
`usb4-generic` `dsn-value` is not read; the board's xHCI off a QEMU root
port on the `intel-adl`/`minimal` layout). This worker's launcher has
`-XhciRc` and passes the uuid. A change to the emulator's default (a
uuid derived from the device's path) was written and dropped after
review: two connected QEMU processes (host-to-host links) would get one
identity. Also seen, not needed for the fix: the chain port
(`usb4-tunnel-dsp`) reports a Power Controller, while the captured TS3
Plus downstream port `7e:04.0` reads "AttnBtn- PwrCtrl- MRL- AttnInd-
PwrInd- HotPlug+ Surprise+" (`usb4-emu`
`tests/captures/framework13-12th/linux-ts3plus-orico`), which gave
Windows the slot to turn off in `p2`.

Evidence: `E:\xhci98-vm\p48\cm\ch\` (`trace-*.log`, `serial-*.log`,
`rep-*.txt`, `plug-*.txt`, `cmdline-*.txt`, `pids.txt`), the scripts
`vleg.sh`, `hp-ch.sh`, `hp-ch2.sh`, `launch-cr.ps1`.

## 48.26 ring interrupts for the connection manager: 98 SE and ME, and the legs left (2026-11-09)

**The code** (`c18e9679`, reviewed and revised; `c6e7dfe7` docs): task
45.22 connected `XhciCmRingInterrupt`'s line on NT alone, 98 SE and ME
polling until their route was read; it was read in 45.22 (an IRQ, shared
and level, on both systems and both profiles). The gate (`cm.irq.9x`)
is removed and `CmRirqWanted` no longer takes the system. The value
follows: every path of the x86 NHI INF writes it (`[Cm.Values]`), the CM
INF gate holds it there, and the installer's row applies on every
system (`INST_APPLY_CM`, not greyed on 98 SE and ME). `test_cm_rirq` 117
checks, `test_inst` 8982. Gated builds: x86 `all` at `c18e9679` "BUILD +
GATES PASSED (release qemu)" (`E:\xhci98-scratch\w48\cm\gb-x86-1.log`),
amd64 `all` at `c6e7dfe7` the same (`gb-amd64-1.log`; the wrapper's own
exit 9009 came after the verdict, from a line-ending slip in it). Kit:
`make-package.ps1` release x86, `ConvertTo-FamilyLayout`, this build's
tools (`E:\xhci98-vm\p48\cm\kit\k26`; `xhcicm.sys` x86 `3b5669ce...`,
`XHCIINST.EXE` `d4acd1b9...`, `XHCITR.EXE` `d5e92b6a...`).

**98 SE** (q35 base, `-Usb4 tb3ar`, one vCPU, my emulator `396e4de4...`,
`w98\`, QMP 47962): installed with `/thunderbolt` (exit 3), then
`XHCIINST /set XhciCmRingInterrupt=1` (taken on 98 SE: "set to 1 for all
controllers"), a cold start:

| Leg | Read | Verdict |
|---|---|---|
| b, `XhciPfwNhiLine` 1 (Setup's default with `XHCIPFW.VXD`) | `cm.irq.value=00000001`, `cm.irq.resource=00000000`, `cm.irq.none`: XHCIPFW freed the NHI's IRQ, the start had none, polling | the default leaves 98 SE polling (recorded in `tuning.md` and record 21 4.5.4) |
| c, `XhciPfwNhiLine` 0, a cold start | the NHI on IRQ 10 (level, shared); `cm.irq.resource=00011101` (a line, shared, level), `cm.irq.connected=0000003A`, `cm.irq.armed=00001000` (bit 12: RX ring 0 for Total Paths 12), `cm.irq.selftest=00000000` (passed at once); the TB3 enclosure plugged over QMP: `tbt.icm.connected=00000811`, approved, `cm.action.tunnel-up`, "QEMU USB4 NVMe Enclosure (Thunderbolt 3), approved" | the connection and its self-test on 98 SE: **pass** |

**ME** (the same on the ME base, `me\`, QMP 47964): installed, value 1
and `XhciPfwNhiLine` 0 set (the console's "set to 0 by /set"), a cold
start: IRQ 10 (level, shared), `cm.irq.resource=00011101`,
`cm.irq.connected=0000003A`, `cm.irq.armed=00001000`,
`cm.irq.selftest=00000000`; the enclosure plugged: connected, approved,
tunnel up: **pass**. (An earlier attempt of this script typed into a
wizard and launched into a misnamed folder; that VM was stopped and the
folder removed, `pids-me.txt`.)

**7 x86, the USB4 profile** (`launch-cr.ps1 -HostKind min`: the
`minimal` host on `00:04.0`, its tunnel ports `00:05.0`/`00:06.0`, a
`usb4-device` with the NVMe, the board's xHCI on `pcie.0`, a fixed
`uuid`; fresh overlay `ri\`, QMP 47963, kit `k26`):

| Leg | Read | Verdict |
|---|---|---|
| l1b, value 1 | the NHI on IRQ 20 (level, shared), `cm.irq.connected=000000B2`, `cm.irq.armed=00010000`, `cm.irq.selftest=0000002F` (47 ms), D: ORICO | control: pass |
| l2, value 1, `usb4-host intx=off` (no pin) | `cm.irq.resource=00000002` (Windows 7 offered a message only), `cm.irq.none=00000001`, polling, D: ORICO | **the no-interrupt leg: pass** |
| idle, ten minutes each in two halves (nothing attached, then the drive attached and idle), the QEMU process's host CPU over each 332 s window; the guest's `% Processor Time` and `Interrupts/sec` where its CSV reached COM1 | value 1 at 200 ms: 228.3 s / 207.2 s (guest 24.2 %, 173/s with nothing attached); value 0 at 200 ms: 262.4 s / 238.1 s (guest 26.1 %, 135/s); value 0 at 50 ms: 245.6 s / 190.5 s (guest 29.0 %, 165/s; 21.3 %, 141/s with the drive) | **the USB4 idle cost at value 1 and at 50 ms: none above the noise** of a TCG guest on a host near 90 % load (other workers' guests) |

**Not read** (owed): the latch to polling and the shared-line leg. The
qemu-flavour leg (its `xhcicm.sys` `78f2e08a...` with
`XhciCmQemuIrqDead` 3 and a burst of plugs; the shared line with
`qemu-xhci msi=off,msix=off` on `pcie.0` and a moving tablet) was
started, but two copies of the step script ran at once and their
keystrokes collided in the guest: the driver file was renamed and not
replaced, and the next start had the NHI at Code 39. The scripts are in
`E:\xhci98-vm\p48\cm\ri-legs.sh` (`swapq`, `shared`, `dead`, `latch`).

Evidence: `E:\xhci98-vm\p48\cm\w98\` (`serial-b.log`, `serial-c.log`,
`rep-c1.txt`, `a*.png`, `b*.png`, `c*.png`), `me\` (`serial-a.log`,
`serial-b.log`, `serial-c.log`), `ri\` (`rep-l1b.txt`, `rep-l2.txt`,
`rep-i*.txt`, `serial-i*.log`, `idle.txt`), `pids.txt` in each.

### Merge (w48-cm)

Merged into `3.4.3.0` at `3f4bfa08` (2026-11-09), with no conflict; the merge
reviewed, nothing material. 48.26's latch and shared-line legs stay owed.

## 48.21 The xHCI core: an isochronous final packet's error, and a Max Packet Size outside its speed's table (2026-11-09, w48-hcd)

Branch `w48-hcd` from `3.4.3.0` at `c3a65f1d`; commits `0f314958`,
`a8301f2e`, `835ee646`. Design record 13 section 19.

**The error.** An error other than TRB Error on an earlier TRB of an
isochronous request's final packet (a packet split across a page, two
TRBs) now holds the request as the audit's fix held a short packet there
(`XHCI_XFER_FLAG_SHORT_DEFERRED` with the new `XHCI_XFER_FLAG_ERROR_HELD`,
`IsoErrorHolds` counting them inside `MidTdDeferrals`). The first draft
settled that hold on an empty event ring, as the short packet's is; the
review found that no proof - after an error the xHC may advance only "for
the next ESIT" (xHCI 1.2c 4.10.3.2 p.187), so its tail can come after a
pass that read the ring empty. The hold now ends on its tail (any code), a
later group's sweep (the empty ring's settle of a short packet's hold
behind it included), a stopped endpoint's saved dequeue
(`HcdDevStopSettle`), or a Ring Underrun or Overrun on the endpoint
(`hcdIsoEvent`, through the new `XhciXferDrainSettledPassed`), which the
controller posts only after consuming every TRB queued before it; the
underrun's settle runs before its 36.57 flow reading and its doorbell, so
a queue that held only that request is not restarted for an empty ring
at every interval.

**The size.** At USB 2.0 speeds `XhciPipeEndpointParams` no longer
refuses a `wMaxPacketSize` its speed's table does not list: under the
speed and type's maximum it is programmed as declared
(`XHCI_PIPE_MPS_UNLISTED`), above it as that maximum
(`XHCI_PIPE_MPS_CLAMPED`; LS interrupt 8, FS interrupt and bulk 64, FS
isochronous 1023, HS bulk 512, HS interrupt and isochronous 1024), the
client given the programmed size; `MpsDeclared` keeps the descriptor's;
`cfg.ep.mps` (slot, DCI, kind, declared size) is noted when such an
endpoint opens. Still refused: size 0 off isochronous, a zero-size
endpoint with additional transactions, a high-bandwidth count of 3.
SuperSpeed unchanged (USB 3.2 Table 9-26's sizes).

**Host vectors**: `test_iso.c` (the error's hold armed, passed over by
the empty ring's settle, settled on proof, tailed with its own code and
with Success after Missed Service, swept by a short hold behind it; TRB
Error, an interior packet and the last TRB arm nothing), `test_pipe.c`
(every row of the table on both sides of its limits, kind and declared
size). Host suites pass (`E:\xhci98-scratch\w48\hcd\ht2.txt`).

**Reviewed and revised** in three rounds (the empty-ring proof; a
missing prototype and the flow reading's order), the last with nothing
material.

**Owed**: no QEMU device declares an unlisted size or produces a mid-TD
isochronous error on a split final packet, so no guest reads either case;
the host vectors are the evidence.

## 48.3 The streams table's "to verify" rows against USB 3.2 (2026-11-09, w48-hcd)

Commit `cee24272`. The eleven rows of `xhci-data-structures.md`'s streams
table (the table's own section, "Streams ... task 31-0's static notes")
read against the one usb.org specification that speaks to them, USB 3.2
revision 1.1 (`docs/references/usb_32_202206/USB 3.2 Revision 1.1.pdf`,
SHA-256 `26e025a56ce0352f4f91fcaaca67b622d8e9e030ca429804bf7da98f52e674fd`,
https://www.usb.org/document-library/usb-32-revision-11-june-2022, already
in the git-ignored folder; page numbers as printed):

| Row | Result |
|---|---|
| Device support | verified: 9.6.7 p.367 ("shall immediately follow the endpoint descriptor"), Table 9-28 p.368-369 (6 bytes; MaxStreams 4:0, valid 0 to 16, 2^MaxStreams streams), Table 9-6 p.332 (type 48, 30h). The row cited Table 9-27, which in revision 1.1 is another table: the row and `src\xhci_stream.h` now cite 9-28 |
| Stream ID 0 | the reservation verified (8.12.1.4.1 p.265, Table 8-13 p.233); the Stream Context and doorbell half is the xHCI specification's: still marked |
| Prime Pipe STALL | USB 3.2 gives only the pipe's side (8.12.1.4 p.264: a Stall moves the stream pipe's state machine to Disabled); the event's zero TRB Pointer is the xHCI specification's: still marked |
| Controller support, Endpoint Context streams, Array alignment, Stream Context, SCT values, Doorbell, Set TR Dequeue Pointer, Reset/Stop Endpoint | the xHCI specification's alone; nothing in USB 3.2 settles them: still marked |

`scripts\check-text.ps1` passed. Left: nine rows (and Stream ID 0's xHCI
half) for a reading of the xHCI specification, which is Intel's, not
usb.org's.

## 48.22 2000 and XP started from a USB disk behind xHCI: the users' method, tested (2026-11-09, w48-hcd)

A test first (owner: "Do a test. I have heard some people have tried
it."). No code; the question of what the package should do is the
owner's (`E:\xhci98-scratch\w48\hcd\QUESTIONS.md`, Q1).

**What users report** (public guides and forum threads on starting XP
from a USB disk with Windows' own USB 2.0 stack, read 2026-11-09: a
PCReview tutorial thread "Tutorial how to boot from any USB", guides
copied across several sites): (1) the USB services (`usbehci`, `usbuhci`,
`usbhub`, `USBSTOR`) set to `Start` 0 in group "Boot Bus Extender",
with CriticalDeviceDatabase rows for the controllers, hubs and disk; (2)
the known failure - "when a new USB device is detected, start is set
back to 3" and the next start is STOP 0x7B - answered either by setting
the value back by hand after each new device, or by editing the INFs in
`%windir%\inf` (`usbstor.inf`'s `[USBSTOR.AddService]`, `usb.inf`,
`usbport.inf`) to `StartType = 0` and `LoadOrderGroup = Boot Bus
Extender`, so that a reinstall writes the boot values itself. Some
guides use "System Bus Extender"; one poster reports that group still
stopping with 0x7B. That is 47.16's reading of the failure, from the
users' side.

**The leg** (XP SP3 q35 base on the pinned emulator, TCG, two vCPUs,
fresh overlay `E:\xhci98-vm\p48\hcd\xb1`; the published `3.4.2.0`
package, its event ring and bus the same as this branch's for this
purpose; `INSTALL.BAT /console /yes /nopause` exit 3 after the root
hub's wizard; a stick on `qemu-xhci` so `USBSTOR` installed): 47.16's
rows imported (`USBBOOT.REG`, `UASBOOT.REG`: `xhci98`, `xhciuas`,
`USBSTOR` Start 0 in Boot Bus Extender, the CDDB rows), then the users'
INF edit, by a script of the leg (`INFEDIT.VBS`), on
`%windir%\inf\usbstor.inf` and on Windows' copies of this package's
INFs (`oem0.inf`: `[Xhci.AddService]`; `oem1.inf`: `[Uas.AddService]`),
each `.PNF` deleted. Then the system disk moved to a `usb-storage` on
`qemu-xhci` port 1, `bootindex=0`:

| Start | Result |
|---|---|
| u1, the first USB start | the desktop; Found New Hardware for the disk's new instance (the edited `usbstor.inf` is no longer signed: the wizard and "has not passed Windows Logo testing", answered Continue Anyway), then "restart your computer" (answered No); `USBSTOR`, `xhci98`, `xhciuas` still Start 0 in Boot Bus Extender (47.16 without the edit: `USBSTOR` 3) |
| u1, a new hub and a new stick behind it hot-plugged | the hub's wizard (`xHCI98 USB Hub`, the edited `oem0.inf`) and the stick's, a restart prompt each; every value still 0; devcon: both disks, the hub, the root hub running |
| u2, the second USB start | the desktop (47.16: STOP 0x7B here); the values 0; both USB disks running |

So the users' method carries over to xHCI98 unchanged, applied to this
package's INF copies as well as Windows' `usbstor.inf`, at the cost of
an unsigned-INF prompt for every new USB disk, and of being undone by
anything that rewrites those INFs. The page file, hibernation and crash
dumps on the USB disk stay as 47.16 read them (not offered). Not run:
2000 with the edit (47.16's 2000 u2 stopped at 0x7B as XP did), a UAS
system disk, real hardware's BIOS.

Evidence: `E:\xhci98-vm\p48\hcd\xb1\` (`serial-a.log`, `serial-u1.log`,
`serial-u2.log`, `cmdline-*.txt`, `s*.png`, `u1-*.png`, `u2-1.png`,
`pids.txt`); the leg's scripts `E:\xhci98-vm\p48\hcd\xfer\G.CMD`,
`INFEDIT.VBS`, `USBBOOT.REG`, `UASBOOT.REG`, `tools\mkusb.py`.

## 48.13 The device PDOs' answer to IRP_MN_QUERY_BUS_INFORMATION (2026-11-09, w48-hcd)

**Windows' own answer, read at run time** (nothing disassembled): QEMU
guests on the pinned emulator, TCG, fresh overlays, Windows' own EHCI
(a stick, a tablet) and UHCI (a hub, a mouse behind it, QEMU's composite
`usb-audio` on its port 2) and, on 10, its own xHCI stack for
`qemu-xhci` (a stick, a hub with a mouse, a UAS disk). `BUSQ.EXE`, a
small Win32 tool of the leg (MSVC 6.0, SetupAPI; source
`E:\xhci98-scratch\w48\hcd\busq\busq.c`, not tracked), lists each
present devnode's stored `BusTypeGuid`, `LegacyBusType` and `BusNumber`
to COM1:

| System (base) | Root hubs | Hub, devices, composite parent | Composite function (`&MI_00`) | HID child / USBSTOR disk |
|---|---|---|---|---|
| 2000 SP4 (`w2kacpi`) | USB, 15, 0 (`ROOT_HUB`, `ROOT_HUB20`) | USB, 15, 0 | none (error 13) | none / none |
| XP SP3 (`xp`) | USB, 15, 0 | USB, 15, 0 | USB, 15, 0 (`usbccgp`) | HID GUID, 15, 0 / none |
| 7 SP1 x86 (`w7x86`) | USB, 15, 0 | USB, 15, 0 | USB, 15, 0 | HID GUID, 15, n / none |
| 10 x64 19045 (`w10x64`) | USB, 15, 0 (`ROOT_HUB30` under `USBXHCI` too) | USB, 15, 0 (`USBHUB3` alike) | USB, 15, 0 | HID GUID, 15, n / none |

USB is `{9D7DEBBC-C85D-11D1-9EB4-006008C3A19A}` (`GUID_BUS_TYPE_USB`),
15 `PNPBus`. Before this task no PDO of this bus answered (the IRP kept
its status; `hcd_pdo.c`'s default arm).

**The change** (`465c39b8`, design record 13 section 20,
`legal-provenance.md` section 4 row): `HcdPdoBusInformation` answers
USB, 15, 0 on the root hub PDO and every device PDO, and on a function
PDO only where WDM 1.20 is available (XP on), matching 2000's absence.
Reviewed: no material findings.

**This driver's answer, after the change** (the gated builds of `465c39b8`, x86 `all` and amd64 `all`, both
"BUILD + GATES PASSED" (`E:\xhci98-scratch\w48\hcd\gb-x86.log`,
`gb-amd64.log`), x86 `xhci98.sys`
`DE752155001B807C88C5155AA1D8136F8F009BE9C5D3EBD9F5C72208E15A5F30`,
the x86 package by `make-package.ps1` and `ConvertTo-FamilyLayout`;
installed by `INSTALL.BAT /console /yes /nopause` on the same overlays;
QEMU `usb-audio` hot-plugged behind the `qemu-xhci` hub for a composite
device):

| System | Root hub, hub, devices, composite parent | Composite function | Same as Windows' own |
|---|---|---|---|
| 2000 SP4 | USB, 15, 0 | none (error 13) | yes |
| XP SP3 | USB, 15, 0 | USB, 15, 0 | yes |
| 7 SP1 x86 | USB, 15, 0 | USB, 15, 0 | yes |

8.1 x86 (`w81x86`, installed with `/usb`): the root hub, hub and devices under `xhci98` answer USB, 15, 0, as Windows' own `USBHUB3` does there (8.1's own stack read on the same guest first). 10 x64: Windows' own read; this driver's answer owed - the amd64 gated build of the same commit passed (`gb-amd64.log`), but an unsigned x64 package has no catalog, which 10 x64 refuses, and the in-guest catalog was not made in this session. The HID children and the
`USBSTOR` and `XHCIUAS` disks above this bus's PDOs answer as on
Windows' own stack (they are not this bus's).

## 48.15 The larger event ring in the emulator: stand-by, hibernation, XP's disable and remove, 98 SE's Device Manager (2026-11-09, w48-hcd)

The same build and package as 48.13's (the 1,024-entry event ring of
47.30 unchanged by this branch); q35 bases on the pinned emulator, TCG,
the host near 90 % busy; devices on `qemu-xhci`: a stick on port 1, a hub
on port 2 with a mouse and (hot-plugged) QEMU `usb-audio` behind it, a
UAS disk on port 3's SuperSpeed half; Windows' own EHCI and UHCI with
their own devices beside it.

| System | Step | Result |
|---|---|---|
| 2000 SP4 (`w2kacpi`, the ACPI HAL) | install | exit 3 after one Found New Hardware wizard (UAS), every device started; XHCITR `evt.ring` `00000400` |
| | hibernate (`rundll32 powrprof.dll,SetSuspendState`) and resume | QEMU powered off at S4; the cold start resumed Windows to the desktop with an "Unsafe Removal of Device" box for one USB stick and its disk (the hot-plugged audio device was not in the resumed machine); both sticks, the hub and the UAS disk present after it; `evt.ring` `00000400`; controller, root hub, hub, UAS started |
| | stand by (Shut Down, Stand by) | refused by Windows: "the device driver for the 'Cirrus Logic 5446 Compatible Graphics Adapter' device is preventing the machine from entering standby" (lessons.md; the base's display) |
| XP SP3 | install | exit 4 at the first run (wizards pending), exit 3 at the retry, the wizards answered "No, not this time" / automatic / Continue Anyway; the controller, root hub, hub, composite, UAS disk (`XHCIUAS`) and stick running |
| | devcon disable, enable | 10 devnodes while disabled, 19 after enable, none with a problem code; `evt.ring` `00000400` before |
| | devcon remove, rescan | the controller came back through Found New Hardware (problem 01 until it was answered), then the root hub, hub, composite and UAS disk each through theirs (answered as above, not the 47.30 leg's Enter); after them all 19 running, no problem code; XHCITR `evt.ring` `00000400`, root hub, hub, composite started |
| 7 SP1 x86 | install | exit 3 after "Install this driver software anyway" prompts; the controller under `xhci98` but its root hub not started until the restart the `Xhci.Dev6` path asks for; after it every device running |
| | hibernate (`powercfg -h on`, `shutdown -h`) | the first image refused at resume with `0xc0000411` (the audio device hot-plugged after the start was not in the resumed machine: the boot's device set changed, the harness's, run-36's note); the second, with the boot's set, resumed to the locked desktop; controller, root hub, hub, UAS started, `evt.ring` `00000400` |
| | stand by | not offered on this base (run-36: VGAPNP blocks S1 to S3) |
| 98 SE | install (`XHCIINST.EXE /dir 98-me /yes /nopause`) | exit 3, "restart pending, problem code 2", then a cold start (98 SE hangs on a warm one); the HID and other wizards answered; XHCITR: controller and three devnodes started (root hub, hub, composite), 3 slots |
| | Device Manager: controller Properties, "Disable in this hardware profile" | the HID children shown removed (red X), no hang |
| | the box cleared again | the four USB devnodes back, no problem mark |
| | Remove, then Refresh | the controller's Add New Hardware wizard (the updated driver), then the HID wizard (the audio function's cancelled: Windows' own `usbaudio` files); XHCITR afterwards: controller and the same three devnodes started, 3 slots |

Stand-by under this driver stays unread on these bases (2000's Cirrus
display, 7's VGAPNP); 3.3.0.0's 44.22 read it on 8.1 x86. On 8.1 x86 stand-by was not offered either (`powercfg /a`: S1 to S3 "disabled by an internal system component: Graphics", the base's basic display).

Evidence: `E:\xhci98-vm\p48\hcd\` (`w2a\serial-b.log`, `serial-c.log`,
`b*.png`, `c*.png`; `xp1\serial-b.log`, `b*.png`; `w7a\serial-b.log`,
`serial-c.log`, `serial-d.log`, `b*.png`, `c*.png`, `d*.png`;
`s98\serial-a.log`, `c*.png`; `pids.txt` in each); copies in
`E:\xhci98-scratch\w48\hcd\` (`busq-*.log`, `xp-legs.log`,
`w2k-resume.log`, `w7-legs.log`, `w7-resume.log`).

### Merge (w48-hcd)

Merged into `3.4.3.0` at `48e062d2` (2026-11-09), with no conflict; the merge
reviewed, nothing material. 48.15's stand-by leg stays owed: the bases'
display drivers refuse stand-by (44.22 read it on 8.1 x86 with `virtio-vga`
and `viogpudo`).

## 48.37 The streams table's rows left by 48.3 against the xHCI specification 1.2c (2026-11-09, w48-xrow)

Commit `4a9f4498`. The ten rows of `xhci-data-structures.md`'s streams table
that 48.3 left marked (eight the xHCI specification's alone, and the xHCI
halves of "Stream ID 0" and "Prime Pipe STALL") read against the xHCI
specification, revision 1.2c (`docs/references/xHCI__Rev1.2c.pdf`, SHA-256
`0b06318005c3e0c8b896f2a002c2a3c78426b5fdacac4ad1cc02ffec15835190`, matching
`docs/references/README.md`), through its text dump `spec-1.2c-dump.txt`
beside it (the README's recipe; `===PAGE N===` is the printed page). The
Read tool could not render the PDF on development host A (no `pdftoppm`),
so every quotation was taken from the dump of that hash-checked copy.

| Row | Result |
|---|---|
| Controller support | verified: Table 5-13 p.353 (MaxPSASize 15:12, array size 2^(MaxPSASize+1), "Valid MaxPSASize values are 0 to 15, where '0' indicates that Streams are not supported"); the row's quotation, "a value of '0' indicates ...", corrected to the table's words |
| Stream ID 0 (xHCI half) | verified: 4.12 footnote 39 p.219, 6.2.4 p.421 (references no ring or array), Table 5-43 p.397 (shall not be written to DB Stream ID), 4.12.2.1 p.227 (a device's ID 0 halts the endpoint with Invalid Stream ID); the all-zero context is the driver's way of meeting it (Table 6-14's note p.422). The section number corrected: 4.12.2 mentions the reservation only in passing (p.225-226) |
| Endpoint Context, streams | verified: Table 6-8 p.414 (MaxPStreams 14:10, LSA 15, 1..15 with LSA = 1, range set by MaxPSASize), Table 6-10 p.416 (DCS "shall be '0' if MaxPStreams > '0'": required, not ignored). 4.12.1 p.224 (MaxPStreams not above the companion's MaxStreams) added; `XhciStreamPlan` meets it |
| Array alignment | verified: Table 6-1 p.404 (Stream Context 16 B / PAGESIZE / 16; Linear array 1M / none / 16; Primary/Secondary 4K / PAGESIZE / 16); the one-page placement is the driver's choice. The three rows added to section 1 |
| Stream Context | verified: Figure 6-4, Tables 6-13 and 6-14 p.421-422 (Stopped EDTLA 23:0, 63:24 RsvdO); 5.1.1 p.338 for the zeroed reserved fields |
| SCT values | verified: Table 6-13 p.421; 4.12.2.1 p.227 (with LSA = 1 only SCT 1 is valid) |
| Doorbell | corrected: per-stream ringing verified (Table 5-43 p.397, 4.7 p.143), but "a stopped stream endpoint restarts only for the streams rung" is not in the specification: the device selects the stream (4.12.1 p.220, p.223) and the xHC keeps no record of active streams (4.12 p.220). The driver rings each stream with a TD, which is what the specification asks; no code disagreement |
| Set TR Dequeue Pointer | verified: Table 6-67 p.455 (SCT required for a nonzero Stream ID, 0 otherwise), Table 6-68 p.455, 4.6.10 p.126-128; corrected to add the non-active-stream exception while Running (p.127; 4.8.3 p.148; Table 6-69's note p.456 says "active") |
| Reset Endpoint, Stop Endpoint | verified: Tables 6-65, 6-66 p.453-454 (no Stream ID), 4.12 p.219 (all streams stall; forward progress saved), 4.6.9 p.119, 4.6.8 p.116 (state kept whatever TSP), 4.6.10 p.127, p.129, 4.8.3 p.149 |
| Prime Pipe STALL (xHCI half) | verified: 4.12 p.219 (TRB Pointer and TRB Transfer Length 0, the Slot Context's Interrupter Target), 4.12.1 p.223. The row's driver half disagreed with the code and was rewritten to it (below) |

The driver's code against the rows: one disagreement, in "Prime Pipe
STALL"'s description of the driver, which the row now states as the code
does it:

- The row said Reset Endpoint and CLEAR_FEATURE(ENDPOINT_HALT). The drain
  (`src\hcd_dev.c` 601-620, `hcdStreamHaltDrain`, then `src\hcd_cfg.c`
  6067-6074, `hcdCfgCancelOne` -> `hcdCfgAbort`) reaches `hcdCfgQuiesce`,
  whose Halted branch issues Reset Endpoint with TSP 1 and sends no
  CLEAR_FEATURE (`src\hcd_cfg.c` 4222-4236: the device's halt is the
  client's RESET_PIPE). `hcdCfgCancelOne`'s own comment
  (`src\hcd_cfg.c` 6069-6071, "the endpoint reset on both sides") reads
  the other way and was left as it is.
- The row said no Set TR Dequeue for a stream that did not fail. On this
  path `hcdCfgAbortPaused` (`src\hcd_cfg.c` 4460-4466) empties every
  stream's ring and `hcdCfgSetDequeue` (3782-3795) issues one for every
  stream, at its enqueue position; every request has completed, so nothing
  is replayed.
- The row said a nonzero pointer no stream ring holds is counted unclaimed.
  A halt code with such a pointer (Stall, USB Transaction, Babble, Split
  Transaction, Invalid Stream ID or Type) is drained as the endpoint's
  (`src\xhci_tol.c` 172-193, issue 24); only other codes are unclaimed
  (`src\hcd_dev.c` 622-635).

Comments citing the same facts updated: `src\xhci_stream.h` (SCT, MaxPSASize
wording, the Stream Context's reserved fields), `src\xhci_stream.c` (Stream
ID 0, the Endpoint Context's DCS), `src\hcd_cfg.c` (TSP with streams,
`hcdCfgStreamKick`'s restart claim), `test\test_stream.c` (Table 9-28 for
48.3's leftover 9-27, the "to verify" pointer) and design record 17
section 4.12.2's restart sentence. No code changed.

`scripts\check-text.ps1` passed. The gated x86 `release` build of
`4a9f4498` passed (pre-merge, `XHCI98_SELFTEST_SKIP_OK=1`: the INF gate's
self-tests skipped on a recorded pass, the host suite run, `test_stream`
included; the xhcidi self-test not run, no `xhciinst\di\xhcidi.exe` in the
worktree); amd64 not built, the change being comments only. Reviewed and
revised.

### Merge (w48-xrow)

Merged into `3.4.3.0` at `1d4bd249` (2026-11-09), with no conflict; the merge
reviewed, nothing material.

## 48.17 The USB drive notice: the merge review's two fixes, XP's answering shell, and the legs owed (2026-11-09, w48-dock)

**What changed** (`49e1749d`, on `w48-dock` from `f3cffa3b`; record 25 section 7.8 items 3, 10
and 13, revision 30 revised). The merge review of `w48-tools` (base `b1c6d4a3`) found two P2s:

- **The poll's first tick.** `stor_poll` compared only controllers already in its baseline, so a
  stick pulled between the tray's first refresh and the first tick (or after the notice was turned
  on) became the baseline, and on 98 SE without a storage driver the removal was never seen. A
  first draft seeded the baseline from the refresh's query; the review found that its ports are read
  before its query, so a seed could absorb an unplug in between. The fix kept: the baseline stays the
  ticks' own reading (`TR_STOR_POLL`, `tr_stor_poll_step`), and a controller with no baseline owes one
  refresh; the baseline is dropped when the tick box is clicked (`stor_set`) and when the set is
  cleared (`stor_clear`), so turning the notice off and on while Settings holds the poll still gives
  the first tick after the close no baseline. A failed read is kept as a baseline, so a controller
  that keeps failing asks for one refresh only.
- **The re-send against a frozen model.** `stor_balloon_event`'s re-send checked an arrival's
  drives against the model the Settings window holds; its ticks now wait while the window is open
  (`tr_stor_resend_due`), the watch's state kept, and the model read at the window's close decides.
- **XP's first plug** (the old worker's finding): XP and later (NT 5.1 up) count as shells that
  answer for the drive balloon (`tr_stor_answers`), so an arrival unanswered for 60 s behind XP's
  Found New Hardware balloons is sent again once; 2000, ME and 98 SE keep section 7.7 item 9's rule.

Reviewed and revised (the seed's race; the toggle while Settings holds the
poll), nothing material at the last. Host: `test_tr` 1763 checks, 0 failures (1727 before; the
new `test_stor_poll`: no baseline owes a refresh, a size change, a drive gone during the refresh it
asked for, a controller new to the model once and its failed read then settled, matching by key; the
answering shells per system; XP's re-send at 60 s, 2000's never; a lost balloon's due tick held while
the window is open and sent at the first tick after; a show while held).

**Builds.** `3.4.3.0` (`be204d55`, w48-inst2's merge) merged into `w48-dock` at `1d78d9ff` (the
coordinator's leave). At `1d78d9ff`: `build-driver.cmd release` x86 and `-amd64` with
`XHCI98_SELFTEST_SKIP_OK=1`, BUILD + GATES PASSED both (`E:\xhci98-scratch\w48\dock\gated-x86.log`,
15:52; `gated-amd64.log`).

**Legs** (q35 bases, fresh overlays in `E:\xhci98-vm\p48\dock\`; the w48-tools base kit, i.e. the
published `3.4.2.0` drivers, with `XHCITR.EXE` of `1d78d9ff` built by `xhcitr\build.cmd`, SHA-256
`94a6a3a3...5de71978`, 573,440 bytes, installed by `INSTALL.BAT /console /yes /nopause`; QEMU
`usb-storage` sticks over read-only `vvfat` folders hot-plugged on the xHCI98 controller, `stick.sh`):

| system | step | result |
|---|---|---|
| XP SP3 (1 vCPU) | `StorageNotice` 1 before the tray started (after the install's restart); a stick plugged for the first time | Windows' "Found New Hardware ... ready to use" at about 20 s; then "USB drive connected", "QEMU QEMU HARDDISK USB Device (D:) is connected." from between 30 and 40 s after the plug, still shown at 120 s (idle user); one balloon, no second (`xp\fp20.png`, `fp40.png`, `fp90.png`). Whether the shell showed the first send late or the re-send is not told apart (no message trace); with the old build the same step showed nothing in 70 s |
| XP | unplugged | "USB drive removed", "... (D:) was removed." within 10 s (`xp\rm10.png`) |
| XP | a second stick plugged, then the controller disabled with `devcon disable *DEV_000D*` | the arrival announced (`xp\de0.png`); after the disable nothing in 30 s (the drive unknown, not removed; `xp\de30.png`) |
| XP | `devcon enable` | nothing in 40 s (the controller answering again is taken silently; `xp\en20.png`) |
| XP | the stick then unplugged | "USB drive removed", "... (D:) was removed." (`xp\en-rm.png`) |
| XP | standby (`rundll32 powrprof.dll,SetSuspendState 0,1,0`) | the guest did not sleep (QMP status running, desktop unchanged): no sleep leg on this XP image |
| ME (1 vCPU) | `StorageNotice` 1 with the tray running (regedit), a stick plugged | "USB drive connected", "QEMU USB HARDDRIVE is connected." at about 20 s (`me\c-fp20.png`); ME then mounted it as E: |
| ME | unplugged | Windows' "Unsafe Removal of Device" and "USB drive removed", "QEMU QEMU HARDDISK (E:) was removed." (`me\c-rm10.png`) |
| 2000 SP4 (ACPI HAL base `w2kacpi`, 1 vCPU) | the tray did not start by itself after the install's restart (started by hand, `C:\XHCI98\XHCITR.EXE /tray`); the kit's `reg add` for `StorageNotice` failed silently (2000 has no `reg.exe`), so the first plug and unplug gave nothing; `regedit /s STORON.REG` | |
| 2000 | a stick plugged | "USB drive connected", "QEMU QEMU HARDDISK USB Device (E:) is connected." at about 10 s, gone by 20 s; no second balloon in 120 s (2000's shell sends no balloon message, so no re-send is ever made; `w2k\y10.png`, `y30.png`) |
| 2000 | hibernation (the same `SetSuspendState` call hibernated: QEMU exited at S4), the stick left out of the cold start, Windows resumed | "USB drive removed", "QEMU QEMU HARDDISK USB Device (E:) was removed." about 30 to 45 s after the start, beside Windows' "Unsafe Removal of Device" (`w2k\r45.png`) |
| 2000 | after the resume: a stick plugged, unplugged, plugged | each announced: "connected" 9 to 15 s after the plug (`w2k\rb9.png`), "removed" with Windows' dialog (`w2k\rb-rm.png`) |

**Found.** (1) On 2000 a drive removed during a hibernation was announced at the resume, which
section 7.8 item 6 says is taken silently (quiet from `PBT_APMSUSPEND` to 15 s after the resume
broadcast). Not traced: either the tray got no suspend or resume broadcast around S4, or the port read
empty only after the 15 s on this slow TCG resume. Reported, not changed. (2) On 2000 the tray did not
start after the install's restart (the old worker's 2000 reading was not made; worth a look by whoever
owns the installer's Run entry). (3) The w48 kit's `G.CMD on` needs `reg.exe`, absent on 2000:
use `STORON.REG`.

**Not read.** 98 SE with this build (the old worker's 98 SE legs were on `ed5a6fce`; the two fixes
change only the poll's first tick and the re-send's tick); a standby and resume with a drive kept
attached (XP did not sleep; 2000 hibernated instead); the race of the first fix (a stick pulled
within the second between the tray's first refresh and its first tick) is host-vectored, not timed in
a guest.

**Proposed roadmap text.** 48.17: partial 2026-11-09 -> "(done 2026-11-09, `c312115f` to
`d0be0d34`, `49e1749d`; `runs/run-48.md`, "48.17": the merge review's poll and re-send fixes, XP
counted as an answering shell; read on 98 SE, ME, 2000, XP and 7 x86; left: 2000's announced
removal across a hibernation, the controller disable and enable read on XP only)" - or keep it open
for finding (1).

## 48.36 Two Thunderbolt docks in the emulator: the CalDigit TS3 Plus and the HP Thunderbolt Dock G2 (2026-11-09, w48-dock)

**The real TS3 Plus, read.** The owner's captures of 2026-11-09 (`temp\p48-docks`, read only): the
P14s's 98 SE quick save with xHCI98 3.4.2.0 (`2026-11-09_140352`) and Windows 11's PnP dump with and
without the dock (`DOCK-nodock-20261109-141034`, `DOCK-ts3plus-20261109-141401`). Behind the tunnel
port (P14s 04:01.0): the dock's switch `8086:15d3` rev 02, subsystem `1ab6:0011` (06:00.0 upstream,
07:00.0 to 07:04.0 downstream, every port pin A); port 0 a Fresco Logic FL1100 `1b73:1100` rev 10,
subsystem `1ab6:1106`, with the dock's audio, a composite `2188:6533` (USB Audio, HID consumer
control) on root port 2; port 1 a second FL1100, `1ab6:1105`, with the card reader `2188:0747`
(mass storage) on root port 8; port 2 an ASMedia ASM1142 `1b21:1242` rev 00, `1ab6:1143` (the dock's
own USB ports, nothing plugged); port 3 an Intel I210 `8086:1533` rev 03, `1ab6:0214` (Windows 11:
"Error", a Windows-side state); port 4 the chain port, empty. Windows 11's instance IDs and location
paths put each function on the same port as 98 SE's bus numbers. Router: "CalDigit, Inc. TS3 Plus",
IDs `003D:0011`, link 1 depth 1, 20 Gb/s x2, approved by the firmware CM.

**What 98 SE made of it (the real dock).** The switch, port 0's FL1100 with its root hub, the
composite and USB Audio started; port 1's FL1100, the ASM1142 and the I210 at problem 29 with no
interrupt line (FFh or 0) and their decode off. Cause, from the readings and the driver's code: those
ports' pin A reaches the tunnel port as pins B, C and D (devices 1 to 3 on the switch's internal
bus), and `xhcicm`'s 98 SE re-enumeration writes the tunnel port's line only to functions arriving on
its own pin (record 21 section 6.1.1 step 5, `tbt_brnum.c` `brLines`), so `PCI.VXD` disables the
others at their first enumeration (record 28 section 12.1 item 1). The dock's card reader and its own
USB ports therefore do not work on 98 SE. The fix is a change of record 28's scope and is **put to
the owner** (`E:\xhci98-scratch\w48\dock\QUESTIONS.md`, Q1: record 28's reprocess-only form extended
to an xHCI function `xhci98.sys` drives, which polls with no interrupt; recommended).

**The HP Thunderbolt Dock G2, from public sources only.** HP's data sheet and QuickSpecs (the ports:
one Thunderbolt 3 downstream, two USB-C, three USB-A 3.0, two DisplayPort 1.4, VGA, RJ45, a combo
audio jack); a Thunderbolt community list naming its controller as the Intel JHL7440 (Titan Ridge DD)
with PCI IDs `8086:15ef` and `8086:15f0`; Linux's names for those IDs (`pci_ids.h`
`TITAN_RIDGE_DD_BRIDGE`, `xhci-pci.c` `TITAN_RIDGE_DD_XHCI`) and its Thunderbolt driver's use of
`0x15ef` as a router ID; HP's audio package (`03f0:0269` "HP Thunderbolt Dock Audio Headset",
`03f0:0567` "Audio Module"); a Linux user's report (Ethernet an RTL8153 on USB, `r8152`; audio behind
hubs at USB paths 1.3.5 and 1.3.6, the RTL8153 at 1.3.3). Not established publicly, and modelled from
the closest documented part: the switch's port count and which port carries the xHCI, subsystems and
revisions, the DROM's IDs and strings, the RTL8153's USB ID. No real HP dock or capture was read.

**The model** (`usb4-emu` `0477013` and `3bbb5c1` on branch `w48-dock` from `60c5d55`; QEMU fork
commits `1795bf2` and its follow-up; `docs/DOCKS.md`, reviewed and revised, the last two rounds with
nothing material): `usb4-device dialect=tb3,dock-model=ts3plus|hp-g2` (q35 tunnel port only; refused
on the i440FX tunnel bridge), with `audiodev=`, `usb-drive=`, `nic-netdev=`. TS3 Plus: the switch's
identity on the upstream and five downstream ports, two `qemu-xhci` with the FL1100's IDs and
subsystems and one with the ASM1142's, `usb-audio` on port 0's xHCI port 2, `usb-storage` (the card
reader's stand-in) on port 1's, QEMU's `e1000e` for the I210 (QEMU has none; `igb` cannot be
realized behind a port with hot-plug off), the empty chain port or the chain's gated port; the DROM's
`003d:0011` and "CalDigit, Inc." "TS3 Plus". HP: `8086:15ef` switch, `qemu-xhci` with `8086:15f0`
carrying two `usb-hub`, `usb-net` (the RTL8153's stand-in) at 1.3.3, `usb-audio` at 1.3.5, and the
chain port; DROM strings "HP" "Thunderbolt Dock G2". The gaps: DOCKS.md section 6. xHCI98 has no
quirk for `1b73:1100`, `1b21:1242` or `8086:15f0`, so the identities change nothing in its controller
code. qtest `usb4-dock-test` 6/6; `usb4-tunnel` 31, `usb4-icm` 40, `usb4-icm-tr` 16, `usb4-usb3` 9,
`usb4-tb3dev` 13, `usb4-asm2464` 4, all passing.

**The owner's decision of 2026-11-09, applied** (`0477013`): a chain port (`usb4-tunnel-dsp` built
for `downstream-ports`) has no Power Controller unless `chain-power-controller=on` (or a `-global
usb4-tunnel-dsp.power-controller`) asks for one, as the captured TS3 Plus's chain port 7e:04.0 reads
("PwrCtrl-"); `auto` is off unless the `-global` says otherwise. qtest
`/usb4-tunnel-dsp/chain-power-controller`, `USB4-HOSTS.md` 8, `ICM-PROTOCOL.md` 15 item 4 and
`XHCI98-HANDOFF.md` 7.1 updated (98 SE, ME, 2000 and XP no longer need `=off`). Whether the HP dock's
chain port has one is not public; nothing was made per dock.

**xHCI98 legs on the models** (emulator `E:\xhci98-scratch\w48\dock\emu\qemu-system-x86_64.exe`,
SHA-256 `364b5c36...89b1a5`, the build of `1795bf2`; the P14s's Alpine Ridge host as `launch-q35.ps1
-Usb4 tb3ar`, its firmware CM; the dock hot-plugged over QMP; this tree's x86 release package of
`1d78d9ff` (`out\pkg-release-x86`, family layout, `xhci98.sys` `2353722e...`, `xhcicm.sys`
`074e0c6b...`) installed with `INSTALL.BAT /console /yes /nopause /thunderbolt` on fresh overlays):

| system | dock | step | result |
|---|---|---|---|
| 98 SE | TS3 Plus | plugged | XHCITR: "QEMU USB4 Dock" (this build's DROM strings; the follow-up gives the dock's), announced by the firmware, approved, PCIe up, 20 Gb/s x2; the wizards for six bridges (each asking a restart, declined), two "xHCI98 USB 3 Controller (Fresco Logic FL1100)" and "xHCI98 USB 3 Controller (ASMedia ASM1142)" from `xhci98.inf`, "PCI Ethernet Controller" with no driver (cancelled), then "USB Audio Device" (`WDMA_USB.INF`) |
| 98 SE | TS3 Plus | report (`d98\report-ts3.txt`) | as the real dock: port 0's FL1100 started (IRQ 10) with its root hub, composite and USB Audio Device; port 1's FL1100 and the ASM1142 at problem 29, line 0; the upstream port and ports 0 and 4 line 10, ports 1 to 3 and their functions line 0 |
| 98 SE | TS3 Plus | `XHCITR /eject id=` the switch's upstream port | the confirmation listing every function of the dock; OK: "Windows has stopped PCI standard PCI-to-PCI bridge. It is now safe to disconnect the device."; then unplugged (`device_del`), plugged again (only the NIC's wizard again), then removed by surprise: no hang, the report then 0 devices |
| 98 SE | HP G2 | plugged | three bridges (restart declined each); "xHCI98 USB 3 Controller (Intel Titan Ridge)" started with no wizard, its root hub, two "xHCI98 USB Hub", the composite with USB Audio Device; "RNDIS/QEMU USB Network Device" (the RTL8153's stand-in) with no 98 SE driver; the name read "HP HP Thunderbolt Dock G2" (fixed in the follow-up); then removed by surprise: no hang |
| XP | TS3 Plus | plugged (`dxp\report-ts3.txt`) | XHCITR's balloon "QEMU USB4 Dock connected (Thunderbolt 3)."; approved, PCIe up; the switch and port 0 started, port 0's FL1100 found (problem 1, its driver not installed: the Found New Hardware wizards were not driven through by keyboard); **ports 1 to 4 at problem 12**, Windows having given ports 0 and 1 the same memory and prefetchable windows (FA200000-FE1FFFFF, F1200000-F13FFFFF) inside the tunnel port's, with bus numbers 06, 0B and 10. Not ruled out on a second overlay; to be read again before it is called a driver or emulator fault |

**Not read, owed.** ME, 2000, 7 x86/x64 and 10 on the models; XP's dock functions past the plug
(the problem 12 above first); the card reader's stand-in with a drive (port 1's xHCI is at problem 29
on 98 SE, as on the real dock); the network functions with a backend; the HP dock's eject and XP
leg; a chain behind either dock; the real HP dock; the quirk (Q1, the owner's).

**Proposed roadmap text.** 48.36: partial 2026-11-09 (`usb4-emu` `0477013`, `3bbb5c1`;
`runs/run-48.md`, "48.36": both docks modelled, the TS3 Plus from the owner's captures and the HP
dock from public sources; read on 98 SE, where the real dock's problem 29 on ports 1 to 3 is
reproduced, and XP's plug. Left: the owner's answer to Q1 and its code, XP's problem 12, the legs on
ME, 2000, 7 and 10, the network functions, the HP dock's real readings).

### Merge (w48-dock)

Merged into `3.4.3.0` at `91d90820` (2026-11-09), with no conflict; the merge
reviewed, nothing material. 48.36's `usb4-emu` dock models stay on `usb4-emu`'s
branch `w48-dock`; the rest of 48.36 and 48.17 continues on `w48-dock2`.

## 48.14 Stor98's native view of a 4 KiB namespace on 8.1 (2026-11-09)

The legs S98VIEWLEFT.1 left after 47.31 ("47.31" and "47.31 (the legs
owed)", `runs/run-47.md`): the `vfyblk` edge checks in the native view,
8.1, the frag=64 split, a flush, and the drive back on `stornvme`, read on
8.1 x86 (10 x86 had them in "47.31 (the legs owed)", 10 x64 across a
replug, a restart and hibernation after the cut).

**The vehicle.** Development host A, QEMU TCG, the q35 bases' pinned
emulator (`asm4242-20261025-2230`), `launch-q35.ps1 -Os w81x86 -Usb4
asm4242 -EncAtBoot -Tablet` with `-device usb4-debug-port` and the
tunnelled drive's namespace 4 KiB (`-global
nvme.logical_block_size=4096 -global nvme.physical_block_size=4096`),
QEMU's `pci_nvme_read`, `pci_nvme_write` and `pci_nvme_flush_ns` traces,
a fresh overlay of the q35 8.1 x86 base. Stor98's source is unchanged
since the `3.4.2.0` cut, so the package is the `3.4.2.0` download's
(`stor98.sys` x86 SHA-256 `43b14a13...`), and the test image is the
debug-port build of this tree (`stor98.sys` `c0428675...`, built by
`src\stor98\scripts\build.cmd x86` with `STOR98_DBGPORT=1`), put in its
place with `frag=64` added to the install's `DriverParameter`. The guest
tool `tests/vfyblk` built with MSVC 6.0 (`VFYBLK.EXE` `c1cd54f2...`).

| Step | Result |
|---|---|
| `stornvme` on the drive (before the package): `diskpart` clean, a 100 MB primary partition (the drive's end left unpartitioned), NTFS quick format (V4K, T:), two files written and compared | "Bytes Per Sector : 4096", "Bytes Per Physical Sector : 4096"; `fc /b` clean |
| The package installed (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`, exit 3; the unsigned-driver prompts accepted; "Stor98 Storage Controller for NVMe SSDs: stor98 3.4.2.0, started", "Stor98 now runs ...: recorded as installed by Setup") | `stornvme`'s two files compare under the release Stor98 before the restart |
| The test image with `frag=64`, a restart | stage `0xCC` `00040040` (frag 64 KiB, parsed); stage `0x49` `0000010C` (LBADS 12, the native view, shift 0) |
| Two files written through Stor98 and all four compared | "Bytes Per Sector : 4096"; `fc /b` clean on all four; the write trace: 786 writes of 16 blocks (64 KiB) at LBAs advancing by 16, none larger; stages `0xB8`: 214 parents of four children, 7 of two, 4 of three, the rest one; no stage `0x190` (no read-modify-write) |
| `vfyblk 2 edge bs=4096` | 0 failed: READ CAPACITY(10)/(16) last LBA 32767, 4096 bytes, exponent 0; READ(10)/(16) of the last block GOOD, past it 05/21/00; VERIFY(10)/(16) of the last block GOOD, two from the last and one past 05/21/00, a count of 0 GOOD; VERIFY(6) 05/20/00; BYTCHK 05/24/00; VERIFY(16) of 8193 blocks 05/24/00; the raw one-block WRITE(10) of the last block written, read back and restored (one 4096-byte command at LBA `0x7fff` each in the trace); WRITE(10) past the last 05/21/00 |
| A file overwritten, then `fsutil volume dismount T:` | 48 NVMe Flush commands in the trace; the four files compare |
| Replug (QMP `device_del` / `device_add` of the enclosure) | a new adapter instance (stage `0x01`), `0x49` `0000010C` again; the four files compare, 4096 bytes |
| Restart | the same |
| The drive back on `stornvme`: the guest shut down, a fresh overlay of the clean 8.1 x86 base (no package) started with the same drive | `stornvme` alone; the volume as E:, "Bytes Per Sector : 4096"; `fc /b` clean on the two files `stornvme` wrote and the two Stor98 wrote |

**Not run here**: 8.1 x64 (read below, "48.14 and 48.20: 8.1 x64"), a
resume and a controller reset in place on 8.1 (read on 10 x86 in "47.31
(the legs owed)").

Evidence: `E:\xhci98-vm\p48\stor\v81\` (`serial-a.log`, the `ev-*.log`
copies, `debugcon-a.log`, `trace-a.log`, `s*.png`, `cmdline-a.txt`,
`pids.txt`) and `E:\xhci98-vm\p48\stor\v81s\`.

## 48.20 UAS and Stor98: the readings the audit of 2026-11-06 left (2026-11-09)

The items AUDLEFT.4 kept (`runs/run-47.md`, "47.1", "Left"), read on the
drivers of the `3.4.2.0` download (the audit's fixes are in it) unless
named. Development host A, QEMU TCG, the q35 bases' pinned emulator.

### A bridge that implements fewer than 32 bits of Prefetchable Base Upper (host)

`test/test_pfw.c` `test_narrow_upper` (`2c904543`): a bridge model whose
Prefetchable Base and Limit Upper 32 Bits registers implement 8 or 16
bits (a 40- or 48-bit decoder; the rest read 0), each of three low
windows. `XHCIPFW.SYS`'s close writes 28h = FFFFFFFFh first and reads
back the mask, so the close is refused and that write undone (two
writes): the bridge is left exactly as found and every state on the way
is closed or above 4 GiB. `test_pfw`: 567 checks, 0 failures. So on such
a bridge the window is not closed (the refusal counted as
`XhciPfwRefused`), never left half-written; nothing in the close was
changed.

### Sense data with autosense off, UAS and Stor98 (XP)

XP SP3 (`E:\xhci98-vm\p48\stor\xpu\`, `-Os xp -Usb4 asm4242 -EncAtBoot
-Tablet`, a QEMU `usb-uas` disk of 256 MiB (`scsi-hd`) on the board's
`qemu-xhci` port 3, `-device usb4-debug-port`), the `3.4.2.0` package
installed (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`,
exit 3 on the third run: the first two ended exit 2 when a driver prompt
was refused while the wizards were walked; then a restart), Stor98's
x86 debug-port image of the same source (`c0428675...`, `shnflush=1`
added to `DriverParameter`). `tests/sensechk` (MSVC 6.0) on each disk:

| Disk | Sense length 0 (autosense off) | 14 | 18 |
|---|---|---|---|
| UAS (`xhciuas.sys`, PhysicalDrive3) | CHECK CONDITION, no sense bytes; the next REQUEST SENSE returns 70h 05/21/00 | the port's own buffer: 05/21 returned; the next REQUEST SENSE again 05/21 | 05/21 with the READ; the next REQUEST SENSE again 05/21 |
| Stor98 (PhysicalDrive2) | `DeviceIoControl` 1117, no sense; the next REQUEST SENSE returns 05/21/00 (kept) | 05/21 returned; the next REQUEST SENSE NO SENSE | 05/21 with the READ; the next REQUEST SENSE NO SENSE |

So with autosense off the condition is not lost on either: Stor98 keeps
its sense for the next REQUEST SENSE (PLAN "Sense rules"), and on UAS
`xhciuas.sys` drops the Sense IU's bytes (`uas_xport.c`, autosense
disabled) while the device itself answers the following REQUEST SENSE
with the condition. QEMU's `scsi-hd` also reports it again after it was
delivered as autosense (sensechk's two "REQUEST SENSE afterwards" checks
fail on the UAS disk for that reason): a property of the emulated
device, not of the driver; a real UAS bridge may clear it.

### Stor98's shutdown-state case (XP)

The same guest and image: `shnflush=1` makes `SRB_FUNCTION_FLUSH` run
the restartable shutdown (TESTING), driven by `FlushFileBuffers` on the
drive's volume (a test tool, `flushd F:`; on XP `FlushFileBuffers` on
`\\.\PhysicalDrive2` returns error 1 and `fsutil` needs NTFS), then:

| Step | Debug-port stages |
|---|---|
| A file written, the volume flushed | `0x62` SYNCHRONIZE CACHE, `0x61` FLUSH, `0xA3` the flush done, `0xA4` CC.SHN, `0xA5` SHST complete (state 4, shut down) |
| `vfyblk 2 edge`: READ CAPACITY(10) and (16) | `0x10` StartIo in state 4 and `0x21` completed, **no `0xA7`**: answered in place, the controller left shut down |
| its READ(10) | `0xA7` re-enable, then the bring-up (`0x45`, `0x40` steps) and the read |
| Again shut down by a flush; a test tool (`rcloop`) sending READ CAPACITY(10) every 100 ms on one open handle; the enclosure removed (QMP `device_del`) | 77 READ CAPACITY answered in state 4; then `0x81` gone entered from StartIo (source 2, from state 4: the presence check before a shut-down answer), each later one fast-failed `0x82` with `SRB_STATUS_NO_DEVICE` (the tool: error 55 from request 151 on) |

So a request needing no hardware is answered after the shutdown
notification without restarting the controller, a hardware request
restarts it, and a drive gone with no notice is found by the presence
check and failed, never answered present.

### The LUN PDO opened by a non-administrator (XP and 2000)

`\Device\XHCIUAS1` (the UAS disk's LUN PDO; `XHCIUAS0` was the removed
first instance) opened through `NtOpenFile` by a test tool (`uasopen`),
then an INQUIRY by `IOCTL_SCSI_PASS_THROUGH` on the handle:

| Caller | `\Device\XHCIUAS1`, read and write | `...\x`, read and write | either, attributes only |
|---|---|---|---|
| The administrator | `00000000`, INQUIRY GOOD | `00000000`, INQUIRY GOOD | opened; INQUIRY error 5 |
| A member of Users (`runas`) | `C0000022` (access denied) | `C0000022` | opened; INQUIRY error 5 |

2000 SP4 (`E:\xhci98-vm\p48\stor\w2k\`, `-Os w2k -Tablet`, the same
`usb-uas` disk on `qemu-xhci` port 3; the `3.4.2.0` package,
`INSTALL.BAT /console /yes /nopause`, exit 3, its wizard for the UAS disk
answered): the same table, line for line (administrator: both names open
for read and write, INQUIRY GOOD; a member of Users by `runas`:
`C0000022` for both; the attributes-only opens refused the pass-through
with error 5).

So on 2000 and XP an open of a name below the LUN PDO is held to the
device object's security descriptor (`FILE_DEVICE_SECURE_OPEN`,
`a973ea8e`): a non-administrator gets no handle that can send a
pass-through.

Evidence: `E:\xhci98-vm\p48\stor\xpu\` (`ev-inst.log`, `ev-sense.log`,
`ev-sd3.log`, `ev-uasopen.log`, `serial-a.log`, `debugcon-a.log`,
`s*.png`, `pids.txt`); the test tools' sources in
`E:\xhci98-scratch\w48\stor\src\` (not tracked).

## 48.28 Taking a controller the BIOS set up (2026-11-09)

The design, PLAN "Taking a controller the BIOS set up" (`370f15a2`,
reviewed and revised over two rounds before the code), and the code
(`2c904543`, reviewed and revised over two rounds): per controller an
exact `stortakefirmware=1` in its `AdapterSettings`, else the global
`StorTakeFirmware` = 1; the "already set up" refusal alone overridden;
CC all ones classed no answer; the string given to no other 9x parser;
`StorFind <bus>.<device>.<function>` kept per adapter; the 9x INF writes
`StorTakeFirmware` 0; the installer's Stor98 Advanced row with its
warning; host tests `pciscan_ut`, `test_inst`, `test_trset`.

**The reading first (98 SE, 2026-11-09)**: what the 9x port hands
`HwFindAdapter` as `ArgumentString`. A test image of the `3.4.2.0`
source that records the pointer and up to 64 bytes of the string
(debug-port stages `0x1B0`/`0x1B1`, a patch applied for the build and
reverted, never committed; `stor98.mpd` `693ece4f...`), on a fresh 98 SE
overlay with the `3.4.2.0` package (`E:\xhci98-vm\p48\stor\s98a\`), the
tunnelled drive's devnode given `AdapterSettings` in its driver key
(`Class\SCSIAdapter\0000`, "drv-as;stortakefirmware=1"), in its hardware
key (`Enum\PCI\VEN_1B36&DEV_0010...\00000000001800`, "hw-as"), and
`Settings`/`DriverParameter` decoys in both, then a cold start: the
string was "drv-as;stortakefirmware=1", the driver key's
`AdapterSettings`, whole. With the driver key's value deleted, the next
cold start passed "hw-as", the hardware key's. The decoys never.

**Builds**: the gated `all` x86 build of `2c904543`: "BUILD + GATES
PASSED (release qemu)" (`E:\xhci98-scratch\w48\stor\gb-x86.log`); the
gated `release -amd64` build: "BUILD + GATES PASSED (release)"
(`gb-amd64.log`); `xhciinst`, `xhcidi` and `xhcitr` rebuilt (the
installer row); the x86 kit by `make-package.ps1 -Arch x86` and
`ConvertTo-FamilyLayout`, its tools from the tree
(`E:\xhci98-vm\p48\stor\kit86\PKG`; `stor98.mpd` SHA-256 `6fd54564...`,
`stor98.sys` x86 `153d8053...`, amd64 `bf998890...`); the 9x debug-port
image of the same source (`stor98.mpd` `002ebc96...`).

**98 SE** (`E:\xhci98-vm\p48\stor\f98\`, a fresh overlay, `-Usb4 asm4242
-EncAtBoot`, `-device usb4-debug-port`, a second QEMU `nvme` at `00:05.0`
with `bootindex=1` so SeaBIOS sets it up and serves it through INT 13h,
its disk a copy of the FAT test disk; the kit installed by `XHCIINST.EXE
/dir 98-me /yes /nopause /thunderbolt /stor98 /storage`, exit 3; both
drives given Stor98 by Windows' wizards; cold starts):

| Setting | The internal drive (`00:05.0`, set up by SeaBIOS) | The tunnelled drive (`05:00.0`) | The start |
|---|---|---|---|
| As installed (`StorTakeFirmware` 0) | `StorFind 0.5.0` "cmd 0107 cc 00460001 csts 00000001 left: already set up; result 0" (Code 10); its volume D: through the BIOS | `StorFind 5.0.0` "... taken; result 1"; `StorFind` the last of the two | desktop |
| `StorTakeFirmware` = 1 (the debug-port image) | `0x0A` `01010101` (set up), `0x0C` `0121FF01` (no setting of its own, the global value 1, taken), the bring-up's disable acknowledged (`0x45`), running; `StorFind 0.5.0` "... taken: already set up, overridden by StorTakeFirmware; result 1" | taken | desktop in about 3 minutes, as without the take |
| The same, then `dir D:` in a DOS box (D: the BIOS's compatibility-mode letter of the internal drive) | "General failure reading drive D / Abort, Retry, Fail?" after about 7 minutes (the BIOS's queues reset by the take) | - | - |
| The same: F: and G:, Stor98's two drives (both the test image, label ORICO) | a file (`VMM32.VXD`) written to each and compared by `fc /b`: no differences | (the other of F:/G:) | - |
| The controller's own `stortakefirmware=0` (`AdapterSettings` in `Class\SCSIAdapter\0000`) against `StorTakeFirmware` = 1 | `0x0C` `00110001`: its own setting, left; "left: already set up" | taken | desktop |
| Hot-plug: the enclosure unplugged and plugged in again on the desktop (the setting above) | `StorFind 0.5.0` kept as it was | `StorFind 5.0.0` and `StorFind` rewritten ("cmd 0006 ... taken") | - |

So on 98 SE the default and a controller's own 0 leave a set-up
controller, the global 1 takes it (the bring-up of every start, its
disable acknowledged before reuse) and the start completes, and each
adapter's report is kept; the drive's BIOS letter stays and fails
(General failure) once taken: the duplicate the design names. Two
attempts to read the take's start earlier the same day were stopped by
the tester after under two minutes, too soon to call a hang (a wait
helper returned at once on a black text screen); they are not counted.
Not separable here: in this emulator, `dir D:` through SeaBIOS's NVMe
INT 13h path before any take also did not return in six minutes (the
first install's guest, stopped), so how long the BIOS letter takes to
fail is not the take's alone.

**Also read here (48.20)**: Stor98's software answers on 9x through the
scatter/gather check (the debug-port image's stage `0x170`): the first
INQUIRY (`00120F1C`) and READ CAPACITY(10) (`00250C1C`) of each start
and each plug-in, each a probe of a non-READ/WRITE request with data
(use 3) on a linear buffer, resolved. And UAS on 98 SE with NUSB's
storage files (installed by `/storage`): a QEMU `usb-uas` disk
hot-plugged on `qemu-xhci` (port 3, then 4; QEMU attached it at 12 Mb/s
both times) was **not enumerated** by 98 SE at all (no new device, no
wizard), while a QEMU `usb-storage` stick hot-plugged on port 4
(5000 Mb/s) came up through NUSB's storage driver ("USB Mass Storage
Device", "USB Disk"); not read further (the UAS disk at power-on, a
SuperSpeed attachment): owed.

**Not run on 98 SE**: the controller's own `stortakefirmware=1` with
the global value 0 (the host vectors hold the decision; ME below tried
it), and shutdown and removal of a taken drive (a shutdown asked from
the DOS box did not start within 200 s; the guest was stopped).

**ME** (`E:\xhci98-vm\p48\stor\fme\`, the same machine on the q35 ME base,
the kit installed with `/storage`, exit 3): the installer left the
internal drive ("an internal drive, not on a Thunderbolt / USB4 port:
Setup leaves it unchanged") and Windows offered no driver for it, so
Stor98 was bound to it by an `XHCIDI` job (`DIF_INSTALLDEVICE` from
`C:\XHCI98\STOR98.INF`, "installed-restart"; its driver key
`SCSIAdapter\0001`), then `AdapterSettings` `stortakefirmware=1` there
with `StorTakeFirmware` 0. The next start was stopped by the tester
after about one minute (the same wait helper fault), and every later
start, in Safe Mode after that stop, faulted in this emulator's
SeaBIOS NVMe INT 13h path with Stor98 not loaded (Windows fault "0D :
8A99 : 00001576", then ScanDisk's fault in `KRNL386.EXE` while it
scanned, the desktop not drawn): **the controller's own take on ME is
not read** (owed, on a machine whose BIOS-served drive survives a Safe
Mode start).

## 48.14 and 48.20: 8.1 x64 (2026-11-09)

**The vehicle**: a fresh overlay of the q35 8.1 x64 base
(`E:\xhci98-vm\p48\stor\v64r\`, `-Os w81x64 -Usb4 asm4242 -EncAtBoot
-Tablet`, the tunnelled drive's namespace 4 KiB, the read/write trace);
the amd64 package of `2c904543` (the gated `release -amd64` build above;
`make-package.ps1 -Arch amd64`, `ConvertTo-FamilyLayout`, the tree's
tools; its six catalogs made unsigned with the packager's own
`New-PackageCatalogFile`), copied into the guest and its images and
catalogs signed there with a throwaway certificate made there
(MakeCert, valid 2026 to 2028, Root and TrustedPublisher; SignTool: 12
files signed and verified; never the project's key, nothing signed on
the host); test signing on; Driver Verifier's standard checks on
`xhcicm.sys` (`verifier /standard /driver xhcicm.sys`). `stor98.sys`
amd64 SHA-256 `bf998890...`, `xhciuas.sys` `fe640d97...`, `xhcicm.sys`
`2f6d4f3a...` (before the guest's signature).

| Step | Result |
|---|---|
| `stornvme` on the drive (before the package): `diskpart` clean, a 100 MB partition, NTFS quick format, two files written and compared | "Bytes Per Sector : 4096"; `fc /b` clean |
| The signed kit installed (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`, exit 3: "Stor98 Storage Controller for NVMe SSDs: stor98 3.4.2.0, started"), a restart | the drive on Stor98 (`Service=stor98`, problem 0, the class model `Stor98_Inst.NTamd64`; no tunnel id on 8.1, by design) |
| Under Stor98: `stornvme`'s files compared; two files written and compared | "Bytes Per Sector : 4096"; `fc /b` clean on all four |
| `vfyblk 2 edge bs=4096` | 0 failed: READ CAPACITY(10)/(16) last LBA 32767, 4096 bytes, exponent 0; the last block read, written raw and restored (one command at LBA `0x7fff` each, seven in the trace) |

So 48.14's native view holds on 8.1 x64 too, and 48.20's x64 guest leg
of the changed drivers ran `stor98.sys` amd64 (`xhciuas.sys` amd64 was
installed but not bound: 8.1 keeps its own USB 3 driver on the
controllers).
| The uninstall under Verifier's standard checks on `xhcicm.sys` (`INSTALL.BAT /console /uninstall /yes /nopause`, exit 3: the filters taken off, the NHI removed, `oem1`/`oem2`/`oem3.inf` removed, the `stor98` service and `stor98.sys` removed, "RESTART Windows"), then the restart | **no bug check** (45.31's 0xC9 in `storport.sys` at this step did not recur); after the restart the drive on `stornvme` alone, "Bytes Per Sector : 4096", `fc /b` clean on the two files `stornvme` wrote and the two Stor98 wrote |

So on 8.1 x64 the drive goes back to `stornvme` with every file
readable (48.14), and 45.31's open Verifier stop at the 8.1 x64
uninstall was not met on this run of the current drivers (one run; its
cause, read in 45.31 as a request sent to `stornvme`'s removed adapter
with no `xhcicm.sys` frame, stays unexplained).

Evidence: `E:\xhci98-vm\p48\stor\v64r\` (`ev-sign.log`, `ev-inst.log`,
`ev-native.log`, `ev-uninstall.log`, `serial-a.log`, `trace-a.log`,
`s*.png`, `pids.txt`).

## 48.29 The kept child: record 31 section 4.9's owed readings on 8.1 x64 (2026-11-09)

The owner's answer to the question of the day (option (a)): the
prototype of 45.11 ported to `3.4.3.0` and section 4.9's owed readings
taken first, with Driver Verifier on `xhcicm.sys`.

**The prototype**: branch `w48-stor-proto` (never merged, never shipped),
the five commits of 45.11 (`f372c4ac` to `434665c8`, still in the
repository's objects) applied to `src/cm` at `c3a65f1d`: `83c867cb` (the
conflicts with `3.4.x` resolved by keeping both sides; `CmFilterPnp`'s
mailbox-member wrapper taking a kept child's reference first; the waits
through `CmWaitEvent`; the ASM4242 mailbox member, new since 45.11,
detached through the one detach owner and called back like the NHI;
reviewed: no material findings), `dce8a90c` (the chain resync the
three-way merge had moved into the logical removal put back in
`CmRegAttRemoved`), `8a6115c9` (the import gate admits the prototype's
`RtlQueryRegistryValues` in `cm_entry.obj`). Gated `qemu` builds of
`8a6115c9`: x86 "BUILD + GATES PASSED (qemu)" (`xhcicm.sys`
`801e632c...`), amd64 "BUILD + GATES PASSED (qemu)" (`237066e1...`); the
legs used `xhcicm.sys` amd64 `2c4e0fee...`, built from the same commit by
the DDK alone before the gated run (45.11's way: a test build), signed in
the guest (`ecec6101...` as signed).

**The vehicle**: a fresh overlay of the q35 8.1 x64 base
(`E:\xhci98-vm\p48\stor\v64p\`, `-Usb4 asm4242 -EncAtBoot -Tablet`,
`-device usb4-debug-port`; the qemu image's `cm.*` records on the 0xE9
port); the amd64 package of `2c904543` with `stor98.inf`'s
`[Stor98_Models.NTamd64.6.3]` line changed for the test to the tunnel id
(A1's model on 8.1), its catalogs made over that and signed in the guest
with the throwaway certificate; test signing on; `verifier /standard
/driver xhcicm.sys`. The installer refuses a qemu-flavour `xhcicm.sys`
("1 build marker(s), the first QEMU; this folder is RELEASE", exit 1), so
the kit was installed with the release image (exit 3; the drive "left
unchanged ... the safe-removal mark service installs Stor98 then") and
the prototype copied over `System32\drivers\xhcicm.sys` before the
restart, as 45.11 did.

| Reading (record 31 section 4.9) | Result |
|---|---|
| First start with the prototype: the drive tagged, the 8.1 arm | the drive's first compatible id `PCI\XHCI98_TUNNEL&CC_010802`; the mark service installed the id model: event 400 `oem3.inf`, rank `0xFF2000`, Matching Device Id the tunnel id, `stornvme.inf:PCI\CC_010802:00FF2007` outranked, "Device Updated: true"; Stor98 running (problem 0); one event 400 |
| **8.1 x64**: disable and enable (SetupAPI, `devcycle`) | `cm.kept.logical` then `cm.kept.rearm` for the drive's child (depth 5); the id kept, Stor98 running, no new event 400 |
| 8.1 x64: the enclosure DSP above the drive disabled and enabled (a parent's removal) | the drive's child logically removed, then `cm.kept.cb` `9` (a kept child, called back outside a dispatch of its own) and `cm.kept.final`; the DSP's own kept child logically removed and re-armed; a new child on the new PDO, the id, Stor98 running |
| **A parent's removal with its child already disabled**: the drive disabled, the DSP disabled and enabled, the drive enabled | the disabled drive's child destroyed by its callback (`cb` `9`, `final`) when the DSP's removal deleted its PDO; the DSP's child re-armed; the new PDO's child (`logical` while disabled, then `rearm` at the enable); the id, Stor98 running |
| **A removal delayed by a handle on the PDO**: a test tool holding `\.\Scsi0:` and `\.\Scsi1:` (the adapters on the drive's and the AHCI's stacks) for 150 s, the enclosure removed (QMP `device_del`) 15 s in | during the hold only `cm.reenum.departure` (the NHI's departure); when the handles closed: the chain removals, the drive's child logical, `cb` `9`, `final`; the DSP's child logical, `cb` `9`, `final`; the USP's child called back inside its removal (`cb` `5`), `cm.kept.gone`, `final`; no bug check; the enclosure plugged in again: tagged, Stor98 |
| **A drive directly on a tunnel port**: a second enclosure model with no PCIe switch (`usb4-device` with no `device-model`, the NVMe at bus 6 directly below tunnel port 2) | tagged, the id model installed (event 400), Stor98; disable and enable: `logical`, `rearm` (depth 3); the tunnel port itself disabled and enabled: the drive's child called back outside its dispatch and destroyed, the port's child re-armed; both drives on Stor98 afterwards |
| A cold start (the second enclosure not present) | the id, Stor98 running |
| The uninstall under Verifier (`INSTALL.BAT /console /uninstall /yes /nopause` from the release kit, the prototype image running), then the restart | exit 3 ("The stor98 service was removed", the filters taken out); the kept children destroyed through their callbacks (`cm.kept.cb` `9`, `final`), the NHI's object called back too (`cb` `6`); **no bug check** (neither 45.11's unload stop, fixed by 45.31, nor 45.31's 0xC9 in `storport.sys`); after the restart the drive on `stornvme`, problem 0 |

**Not read** (section 4.9's fifth reading): **the update from `3.3.0.0`
on 8.1 with one drive on Stor98 and one on `stornvme`** (item 10's order).
It needs the `3.3.0.0` package installed first, a second tunnelled drive
left on `stornvme`, then this tree's package with the A1 INF and the
prototype, and the run after the restart: not taken in this session.

**What the readings say** (for the owner's code decision, QUESTIONS.md
Q2): the kept child behaves on 8.1 x64 as it did on 8.1 x86 in 45.11, and
the four new conditions hold: a parent's removal with the drive disabled
destroys the disabled drive's child by its callback and the next start
gets the id; a removal delayed by handles finishes only when they close,
and then cleanly bottom-up; a drive with no switch above it (depth 3) is
kept and re-armed, and destroyed with its tunnel port; the uninstall
under Verifier destroys every kept child and stops nothing. The `cm.kept`
records are 45.11's (event, IRQL, depth, location); every callback came
at `PASSIVE_LEVEL`.

Evidence: `E:\xhci98-vm\p48\stor\v64p\` (`ev-*.log`, `ev-*-dbg.log`
(`cm.*` records), `serial-*.log`, `debugcon-*.log`, `s*.png`,
`pids.txt`); the prototype at `w48-stor-proto` `8a6115c9`; the leg's
`stor98.inf` change and the test tools (`devc64.exe` from
`tests/devcycle`, `hold.exe`) in `E:\xhci98-scratch\w48\stor\` and the
kit folders.

### Merge (w48-stor)

Merged into `3.4.3.0` at `46b618b5` (2026-11-09), with no conflict; the merge
reviewed, nothing material. The 45.11 prototype stays on `w48-stor-proto`,
never merged. 48.29's kept child (owner, 2026-11-09: write it now), 48.28's ME
leg and 48.20's UAS on 98 SE continue on `w48-stor2`.

## 48.30 and 48.31 the chained unit's correspondence, ownership and eject; the 9x half of the chains (2026-11-09)

Worker w48-chain, development host A, branch `w48-chain` from `c3a65f1d`.

### The design

Design record 34 revision 13 (`cdc5d4b2`), reviewed and revised before
code (three rounds):

- section 3.12 (task 48.30): the structural binding (a parent's link to
  one chain port) and the occupancy binding (the kid on it), learnt only
  in a transaction the connection manager core opens immediately before
  the kid's first Path Enable against a stable baseline of the chain
  ports under the port's member (read twice, the same); one gain on one
  empty port with nothing else in the domain able to carry an unowned
  live path (another kid unsettled or in use without an occupancy, a
  watched link) learns it; fresh kid connects serialised, bounded at
  10 s; the chained marks entry's token granted from publication
  provenance (a PDO identity shared by the filters on one PDO, each chain
  child `BusRelations` request's begin stamp) by one rule at birth and
  later; the confirmation by the kid's generation; the checklist and the
  commit before `TbtU4EjectKid`; the withdrawals; the status record's
  kind 3 bridge; the mark service's confirmation of a chained unit.
  Stor98's chained entries stay with the D2 work (item 10); 98 SE and ME
  learn no binding (item 11). It replaces section 3.1's "Correspondence".
- section 3.3.7 (task 48.31): chain requests to the CONFIGMG worker from
  the seam's depth-2 actions, the walk through PCI.VXD's reader with its
  owed retry (10 s per kind), the chain port's devnode found below the
  member's by location or path and identity, the numbering with the chain
  port as the range and window bridge and the member as the interrupt
  anchor (a pin rotated through the path), the removal of a chained
  device whose function is gone, and `DN_REMOVABLE` on the chained unit.

### The code

`f0a3116f` (48.30 and 48.31, reviewed and revised over six rounds),
`466abcc5` (3.4.3.0 merged in at the coordinator's request), `20e163d1`
(98 SE: a chained tunnel's up numbers its chain port before any member is
re-enumerated; found by leg k below, reviewed and revised over two
rounds). Files: `src/cm/tbt_chain.c`, `tbt_chain.h` (new), `tbt_u4cm.c`,
`tbt_u4cm.h`, `cm_nhi.c`, `cm_reenum.c`, `cm_reenum.h`, `cm_filt.c`,
`cm_marks.c`, `cm_marks.h`, `cm.h`, `cm_statb.h`, `cm_status.c`,
`cm_statos.c`, `cm_cfgwk.c`, `tbt_topo.c`, `tbt_topo.h`, `tbt_brnum.c`,
`tbt_brnum.h`, `src/cm/sources`, `xhcimark\mark_core.c`, `mark_core.h`,
`xhcimark.c`, the host suites and `test\run-host-tests.cmd`.

### Host tests

`test_tbt_topo` (tbt_chain.c: the reading, arm, attribution outcomes,
re-plug, conflict, withdrawals; the walk's rotation), `test_cm_marks`
(CmMarksGrant, CmMarksConfirmEx), `test_u4cm` (the kid gate: refused
connect deferred, armed before the first Path Enable, never for an
adoption, a resume rebuild armed without MAY_CONNECT), `test_tbt_brnum`
(XhciTbtNumberBridgesAt at a chain port, rotations 0 and 1, a chained
ASM2464 numbered inside the padded range), xhcimark's self-test
(the chained entry's landing parsed; mk_descends).

### Builds

- x86 `all` of `f0a3116f` (working tree identical): "BUILD + GATES PASSED
  (release qemu)" (`E:\xhci98-scratch\w48\chain\gb-x86.log`).
- amd64 `all -amd64` of `f0a3116f`: "BUILD + GATES PASSED (release qemu)"
  (`gb-x64.log`).
- x86 `all` of `20e163d1` (after the 3.4.3.0 merge, with
  `XHCI98_SELFTEST_SKIP_OK=1`): "BUILD + GATES PASSED (release qemu)"
  (`gb-x86b.log`).
- amd64 `all -amd64` of `20e163d1` (with `XHCI98_SELFTEST_SKIP_OK=1`):
  "BUILD + GATES PASSED (release qemu)" (`gb-x64b.log`).
- Packages: release x86 of `f0a3116f` (`xhcicm.sys` `32b90e34...`,
  `xhci98.sys` `1bc816f5...`, `XHCIMARK.EXE` `f88bc43b...`); qemu x86 of
  `f0a3116f` (`xhcicm.sys` `7c28044a...`, `xhci98.sys` `cfa29021...`) and of
  `20e163d1` (`xhcicm.sys` `37bbc24d...`, `xhci98.sys` `2ab49db9...`).

### Legs

Emulator: QEMU v11.1.2 with `usb4-emu` `de1ed69` (main `60c5d55` plus the
48.24 Host Router Reset change, which these legs do not exercise),
`qemu-system-x86_64.exe` `396e4de4...`, copied to
`E:\xhci98-vm\p48\chain\bin\`. q35, `-Usb4 adl` (`usb4-generic`,
`host-model=intel-adl`), the parent `usb4-device ... port=1, dialect=usb4,
downstream-ports=1, chain-power-controller=off` with its NVMe at power-on,
the child plugged by QMP: `usb4-device,id=enc2,bus=u4.0,parent=enc0,port=1,
drive=nvme3` (plain enclosure) or with `device-model=asm2464`. Fresh
overlays of the q35 bases; the release package installed (XP `INSTALL.BAT
/console /yes /nopause /thunderbolt /stor98`, 98 SE `XHCIINST /dir 98-me
/console /yes /nopause /thunderbolt /stor98`), then the qemu flavour's
`xhcicm.sys` and `xhci98.sys` copied over the installed ones. Evidence
`E:\xhci98-vm\p48\chain\xp\`, `w98\`, `w98b\` and `me2\` (debugcon, serial,
screenshots, `cmdline-*.txt`; the CFGMGR32 walks `dn*-*.txt`, read by a
scratch `DNSTAT.EXE` built with Visual C++ 6 in
`E:\xhci98-scratch\w48\chain\dn\`).

| Leg | System | Read | Verdict |
|---|---|---|---|
| x1 | XP SP3 | The dock preserved at the start (`u4.pcie.up=00010809`), its chain port classified (`cm.chain.class=00050208`: 02:01.0, secondary bus 5). The child plugged: `u4.kid.connect`, `cm.chain.arm=01000000` (open), `u4.kid.pcie.up`, `cm.chain.txn=01000002` (LEARNED), `cm.chain.bind=01010208` (kid 1, epoch 1, chain port 02:01.0), `cm.chain.kick`, `cm.chain.tok=01010208` (the token granted), `cm.chain.removable=21050208`; Stor98 by its wizard; D: (the dock's) and F: (the chained); `XHCIINST /devices`: the kid "behind another device, approved", "its PCIe connection reaches the port at PCI 02:01.0" (the kind 3 entry's chain port). The applet did not list the chained unit in the session of its first install (as 45.13's x3); the child unplugged and plugged again: torn down, connected again, `cm.chain.txn` LEARNED on the same binding (epoch 1). Safely Remove Hardware then listed "Stor98 Storage Controller for NVMe SSDs" on its own; a file written to F:, the unit stopped (Generic volume F:, its disk), and: `cm.chain.confirm=00010208` (the mark service's confirmation of the chained entry), `cm.chain.eject=00010208`, `u4.kid.down=00011101`, `u4.eject.kid=00010100` (kid 1, port 1, TBT_OK), `cm.chain.unbind`; `XHCIINST /devices`: D: kept, F: gone, the kid "PCIe closed after an eject". The child unplugged and plugged again: connected and learnt again, F: back, `fc /b` of the file: no differences. The dock unplugged with the child attached: `u4.kid.dropped` first, then the port's link-down, both bindings withdrawn, no hang; powered down at once | 48.30 clauses (learn, token, confirmation, checklist, `TbtU4EjectKid`, the dock's drive untouched, the middle unplug): pass |
| k | 98 SE | The dock at power-on (the chain port padded, `cm.brnum.9x.pad=00000001`, 4 MiB); a chained ASM2464 plugged: the kid UP, but the members' re-enumeration ran first and PCI.VXD made a devnode for the ASM2464's upstream port in reset state (`info pci`: secondary 0), so the chain request found the chain port populated (`cm.chain.9x.held=00000208`). The child unplugged: `cm.chain.9x.remove=00000208`, the worker's `Query_Remove_SubTree` (accepted once the Add New Hardware wizard was closed), `Remove_SubTree`, `cm.chain.9x.reenumerate=00020800` | the removal (item 5): pass; the numbering: failed, fixed by `20e163d1` |
| n | 98 SE, `20e163d1`'s qemu `xhcicm.sys` | The chained ASM2464 plugged: no member re-enumeration first; `cm.chain.9x.number=00020801` (DONE), `cm.chain.9x.irq=01030001` (lines set on 3 functions, rotation 1), `cm.chain.9x.reenumerate=00020800`; `info pci`: the ASM2464's ports 04/05/06 and 05/06/06 inside the padded chain port, its NVMe on bus 6; Windows' Add New Hardware wizards for the new bridges and Stor98. The Stor98 install's restart was accepted, which ends QEMU on 98 SE; the next cold start stopped with "A fatal exception 0E has occurred at 0197:BFF86EBD" (a ring-3 address) and a black screen, and the one after it came up in safe mode. Not chased | the chain port's numbering, its interrupt lines and its re-enumeration: pass; the chained drive's start, `DN_REMOVABLE` and a file: not read |
| n2 (`w98b` tags i to m) | 98 SE, a fresh overlay, `20e163d1`'s qemu `xhcicm.sys` and `xhci98.sys` | Release package installed with Stor98 (`/thunderbolt /stor98`), the qemu drivers copied, then the restart prompt that ends QEMU taken once more on purpose (tag j) and a cold start: the desktop with no fault (tag k), so the 0E at `0197:BFF86EBD` did not recur (seen once, on the first overlay). First chained plug on the fresh overlay (tag k): numbered, but the new bridges' install asked for a restart (answered No) and the NVMe stayed without its BAR until a cold start. Tag l (cold start, the dock at power-on, the child plugged): `cm.chain.9x.number=00020801`, `cm.chain.9x.irq=01030001`, `cm.chain.9x.reenumerate=00020800`, no wizard; Stor98 started on the chained NVMe, F: (E: the dock's); `copy` then `fc /b` of a file on F:: no differences; `XHCIINST /devices`: the ASM2464 "behind another device, approved", its Stor98 started under the chain port. The child unplugged: `u4.kid.teardown`, `cm.chain.9x.remove=00000208`, `Query_Remove_SubTree` and `Remove_SubTree` CR_SUCCESS, `cm.chain.9x.reenumerate=00020800`; F: "Invalid drive specification", E: and its file kept, no hang. Plugged again: numbered again, F: back, `fc /b`: no differences. Tag m (cold start, plugged once more): a scratch CFGMGR32 walk (`CM_Get_DevNode_Status`, `dn0-m.txt`, `dn1-m.txt`): `DN_REMOVABLE` (0x4000) set on the chained ASM2464's two bridges, its NVMe and the disk (status `18086ACE`/`18086A4E`), as on the dock's devnodes; no `cm.chain.9x.removable` line because the worker found the bit already set and had nothing to add (98 SE sets 0x4000 on nearly every devnode of a dynamically loaded driver, the host bridge and root devices included; `XHCIINST`'s "marked removable by the Thunderbolt driver" reads the same bit) | the chained drive's start with a letter, the file, the removal and the re-plug: pass; `DN_REMOVABLE`: set on the chained unit's devnodes (not by the worker on 98 SE); the 0E fault: not reproduced |
| me (`me2` tags a, b) | ME, a fresh overlay, `20e163d1`'s qemu `xhcicm.sys` and `xhci98.sys` | (A first ME overlay, `me`, was lost: the release install's shutdown hung, as the q35 base notes say ME does with `xhcicm` installed, the kill left a registry error and a wizard stuck searching A:; not a driver reading.) Fresh overlay: the release package with `/thunderbolt /stor98` (exit 3, restart pending), the bridge and Stor98 wizards, the qemu drivers copied, Start, Shut Down: powered off. Cold start (tag b): `cm.reenum.9x.version=0000045A`, the dock's chain port padded (`cm.brnum.9x.pad=00000001`), the dock's E:. The chained ASM2464 plugged: `cm.chain.9x.number=00020801`, `cm.chain.9x.irq=01030001`, `cm.chain.9x.reenumerate=00020800`; `info pci`: the ASM2464's ports 04/05/06 and 05/06/06 under the chain port 02:01.0, its NVMe on bus 6. At this first plug Windows' wizards installed the new bridges and Stor98 (no restart asked) and the NVMe was left at problem 29 with its BAR unmapped, as at 98 SE's first plug; the child unplugged (`cm.chain.9x.remove=00000208`, query-remove CR_SUCCESS, `Remove_SubTree` CR_NO_SUCH_DEVNODE - the devnode already gone, which the removal step takes as done - and `cm.chain.9x.reenumerate=00020800`; the chained subtree gone, the dock's kept) and plugged again with no restart: numbered again, the BAR at `fe600000`, Stor98 started, F:; `copy` then `fc /b`: no differences; the CFGMGR32 walk (`dn3-b.txt`): `DN_REMOVABLE` on the two ASM2464 bridges, the NVMe and its disk, as on the dock's devnodes, no `cm.chain.9x.removable` line (already set). Unplugged: the same removal lines, F: gone, E: and its file kept, no hang. Plugged again: F: back, `fc /b`: no differences. Shut Down powered off | ME: up, numbering, the drive, the file, the removal and the re-plug: pass; `DN_REMOVABLE`: set on the chained unit's devnodes (not by the worker) |

### Left

- 98 SE and ME: the first plug of a chained unit on a system that has not
  seen one leaves its NVMe at problem 29 with no BAR while the wizards
  install the new bridges' and Stor98's drivers; the next plug (or a cold
  start) brings the drive up. Both systems read so once each; whether the
  worker should re-enumerate the chain port once more after the wizards is
  not decided here.
- 98 SE and ME: `DN_REMOVABLE` (0x4000) reads set on nearly every devnode
  (the host bridge's and root devices' too), so the worker's marking finds
  nothing to add and logs nothing; `XHCIINST`'s "marked removable by the
  Thunderbolt driver" reads the same bit. A wording question for record 24,
  not this task's.
- The chained functions' Interrupt Line reads 0 in `info pci` after
  Windows configured them, though `cm.chain.9x.irq` read back 3 lines
  written at the numbering (the dock's own functions keep 10); the drive
  worked, and the log carries the no-IRQ window's `cm.reenum.9x.noirq`
  lines. Not chased.
- 7 x86 and 10 x64 for 48.30 (the applet's stop and the confirmation);
  2000 has no confirmation from the mark service (problem 24 is not 47).
- Stor98's chained entries and the D2 and M statements (record 34
  section 3.12 item 10; 45.18's left).

### Merge (w48-chain, w48-chain2)

Merged into `3.4.3.0` at `1b4814a6` (2026-11-09). The merge review found a
race on 98 SE and ME: a chain operation took its generation after its gates,
so a removal posted in between could be adopted and the chain port
re-enumerated before the removal ran. Fixed on `w48-chain2` (`5a2080d2`,
record 34 revision 14, section 3.3.7 item 1: the generation taken first, every
chain step and the re-enumeration admission guarded; host vector
`test_chain_admit`), merged at `9c5ea69e`, reviewed again, nothing material.
The first-plug problem 29, the removable wording on 9x and the chained
functions' interrupt line continue on `w48-chain2`.

## TBSPEED and TBTHREAD, and why iperf cannot start a thread on the P14s (2026-11-09, w48-spd)

Branch `w48-spd` from `31eb2dc4`, two commits: `c6611e05` (the tools, the
host suite, the rows in AGENTS.md, `legal-provenance.md` and
`source-files.md`) and `fae73df4` (the server waits in `select` before
`accept`). Owner's decision of 2026-11-09 after the bench: iperf 2.0.8 and
1.7.0 both fail on the P14s's 98 SE (PATCHMEM, 32 GB).

**The tools.** `tbspeed\`, built by `tbspeed\build.cmd` with MSVC 6.0,
`/SUBSYSTEM:CONSOLE,4.0`, git-ignored, never in a package.
- `TBSPEED.EXE`: TCP throughput with no thread (blocking connect and accept
  behind `select`, non-blocking connected sockets bounded by `select`,
  Winsock 1.1 from WSOCK32, LIBC `/ML`); imports KERNEL32 and WSOCK32 only.
  `-s` waits and prints the computer's addresses; `-c <addr>` connects and
  runs one test, `-t sec` (default 10) or `-n MB`; `-r`: the `-s` side sends
  on the connection the `-c` side opened; `-o file` appends what is shown.
  The receiver counts from its first byte to the sender's close and sends
  the count back (16-byte request and result blocks), so both ends print the
  receiver's figure. Idle limit 30 s either way; a timed send ends on time.
- `TBTHREAD.EXE`: the system's memory figures and the image's default stack,
  then `CreateThread` (0, 4 KB to 64 MB; flags 0, `CREATE_SUSPENDED`,
  `STACK_SIZE_PARAM_IS_A_RESERVATION`), `_beginthreadex`, `_beginthread`,
  `VirtualAlloc` reserve/commit, the largest reservable block, and how many
  threads can exist at once; each failure with `GetLastError` and its text.
  Imports KERNEL32 only (`/MT`).
- Host suite `test_tbs` (77 checks, 0 failures; in `test\run-host-tests.cmd`,
  the whole runner PASSED on development host A). Reviewed and revised over
  four rounds (unbounded blocking send, the one-shot server's exit status,
  the 64 KB label, premature EOF in a byte-count test, then nothing
  material). `scripts\check-text.ps1` OK. No gated build: no driver, INF or
  build-driver input changed; the runner was run on its own.
- Binaries read in the emulator: `TBSPEED.EXE` SHA-256 `b79c8ad9...6f34b67`
  (65,536 bytes, `fae73df4`), `TBTHREAD.EXE` `9d9b7489...c424ea5e9` (49,152
  bytes, `c6611e05`; its source is unchanged at `fae73df4`).

**Emulator legs** (launch-q35, TCG; throughput relative only).
- 98 SE (512 MB, pcnet) and XP SP3 (pcnet) on a QEMU socket network
  (`-netdev socket`, APIPA 169.254.81.212 / .195): XP client to the 98 SE
  server, 20 s: 7.75 Mbit/s sent, 7.77 received; `-r` (98 SE sends) 12.15.
  With the kit's scripts (98 SE `SPEED.BAT`, XP `WIN11\SPEED.CMD`, 30 s
  each): XP to 98 SE 8.33, 98 SE to XP 13.50 Mbit/s, both ends' files agree.
  Other way round (XP `SPEEDS.CMD` after the firewall's Unblock, 98 SE
  `SPEED2.BAT`): 98 SE to XP 15.13, XP to 98 SE 8.63.
- Ctrl+C stops `TBSPEED -s` on 98 SE, and Windows shuts down with it
  waiting; before `fae73df4` the blocking `accept` made it "not responding"
  at shutdown.
- XP x64 SP2 (WOW64): `TBSPEED` loopback both ways (253.6 / 250.1 Mbit/s),
  `-h`, and `TBTHREAD` (all creations ok, 512 threads) - the x86 EXE runs
  there; the firewall's alert appears for `-s` and loopback works anyway.

**Why iperf cannot start a thread (sources read, facts only).**
- iperf 2.0.8 (`iperf-2.0.8.tar.gz` from the iperf2 SourceForge project,
  SHA-256 `c98d5dca...6b4d2`): `compat/Thread.c` `thread_start` calls
  `pthread_create(&mTID, NULL, thread_run_wrapper, thread)` and prints
  "pthread_create failed" through `WARN` when it returns non-zero (the
  native path would print "CreateThread failed"). The Windows binary
  (`575691cf...`, MinGW ld 2.24, image stack reserve 2 MB, commit 4 KB)
  links mingw-w64's winpthreads statically and imports `_beginthreadex`
  from msvcrt.dll. winpthreads' `pthread_create` (thread.c, v3.3/v4.0)
  returns EAGAIN when its thread record cannot be allocated, when
  `CreateEvent` fails five times, or when
  `_beginthreadex(NULL, 0, ..., CREATE_SUSPENDED, NULL)` returns 0 (stack
  size 0 with a NULL attribute: the image's 2 MB reserve; no
  `STACK_SIZE_PARAM_IS_A_RESERVATION`).
- iperf 1.7.0 (`iperf-1.7.0-source.tar.gz` from iperf.fr, `f54fabaf...`):
  `lib/Thread.cpp` uses `CreateThread(NULL, 0, Run_Wrapper, ptr, 0, &mTID)`
  and on failure prints "CreateThread failed: <Winsock text>" and exits;
  the binary (`d702305b...`, 1 MB reserve) holds that string and no
  "pthread_create". Its Win32 condition wait is `SignalObjectAndWait`, which
  Windows 98 does not have, and `-s` first calls `OpenSCManager`. Read on
  98 SE in the emulator (1 GB and 4 GB guests): `IPERF17 -s` prints "Server
  listening", then "OpenSCManager failed - This function is only valid in
  Win32 mode. (0x78)", and exits at once. iperf 1.7.0 cannot serve on any
  98 SE, whatever the memory.
- Reproduction: plain 98 SE refuses 1536 MB at boot ("Insufficient memory
  to initialize Windows") with and without `[vcache] MaxFileCache=524288`;
  with `[386Enh] MaxPhysPage=40000` it boots in a 4096 MB guest and uses
  1 GB. At 512 MB, 1024 MB and 4096 MB/MaxPhysPage every `CreateThread`,
  `_beginthreadex` and `_beginthread` (0 to 64 MB, every flag) ran, 512
  threads existed at once, the largest reservable block was 2,088,576 KB,
  and `IPERF -s` listened. PATCHMEM is not available here, so the P14s's
  state (more than about 1 GB in use) was not reproduced.
- The tree's tools: XHCITR (`CreateThread` for the device-change watch and
  the quick save), XHCIWIZ (`_beginthreadex` for its job), XHCIINST's engine
  (`CreateThread`) and XHCIPERF (`_beginthreadex`) all pass stack size 0
  and no flag, as iperf does; none sets a stack size. If thread creation
  fails system-wide on that machine, they fail the same way (XHCITR's watch
  is then silently absent and its quick save says "Quick save could not
  start."; XHCIWIZ's job does not start).

**Bench kit** `E:\xhci98-bench\p48-tbnet` (kept as it was in
`E:\xhci98-scratch\w48\spd\kit-backup`): 98SE gains `TBSPEED.EXE`,
`TBTHREAD.EXE`, `THREAD.BAT`; `SPEED.BAT` (now `TBSPEED -s -o SPEED98.TXT`)
and `SPEED2.BAT` (both tests as the connecting side) rewritten; WIN11 gains
`TBSPEED.EXE` and `SPEEDS.CMD` (the waiting side); `SPEED.CMD` runs both
tests as the connecting side with no PowerShell or `choice` (XP-safe);
`SHA256.TXT` two lines; `BENCH-STEPS.txt` steps 8a to 8c. The iperf files
stay. The scripts were reviewed and revised (the log path quoting, the
"same connection" wording).

Owed: the P14s's `THREAD.TXT` (step 8c) and the bench speed readings; the
98 SE to 98 SE usb4-xdomain reading was not taken (that rig is net3's).

### Merge (w48-spd)

Merged into `3.4.3.0` at `5bce9bf5` (2026-11-09), with no conflict; the merge
reviewed, nothing material. TBSPEED and TBTHREAD serve the Thunderbolt
networking bench (48.18, issue 28); they are developer diagnostics, never in a
package.

## 48.15 (the stand-by leg) The larger event ring through stand-by and resume (2026-11-09, w48-sby)

The build of the hcd section's 48.13 and 48.15 (the x86 gated build of
`465c39b8`, `xhci98.sys` `DE752155...5F30`, its package by
`make-package.ps1` and `ConvertTo-FamilyLayout`; the 1,024-entry event
ring of 47.30). QEMU under TCG on development host A, the pinned emulator
(`asm4242-20261025-2230`), the host near 90 % busy; fresh overlays of the
q35 bases; devices on `qemu-xhci`: a stick on port 1 (a VVFAT disk holding
a 4 MiB random file, `BIG.BIN`, whose copy is on the transfer disk), a hub
on port 2 with a mouse, a UAS disk on port 3; Windows' own EHCI with the
tablet. Each step is a line of the leg's `G.CMD` (or, with no display,
`SBY7.CMD`) writing to COM1. Stand-by is `rundll32
powrprof.dll,SetSuspendState 0,1,0` from an elevated prompt with
hibernation off, the wake QMP `system_wakeup` over TCP (a QMP on a named
pipe is not served while the guest is in S3: QEMU's main loop sleeps and
the pipe is never polled, so the first attempt could not be woken and was
stopped; every later leg used TCP). The stick's disk comes up offline
(its VVFAT signature is the transfer disk's) and is put online with
`diskpart` before each read. The machines offer S3: q35's `ICH9-LPC` and
the `pc` machine's `PIIX4_PM` both read `disable_s3` 0 and `disable_s4` 0
(`qom-get`).

**8.1 x86, the precedent's recipe on q35** (run-44 "Stand by on 8.1
x86"): `-vga none -device virtio-vga`, virtio-win 0.1.302's `viogpudo`
(`w8.1\x86`) forced onto the device (WDK 7.1 devcon `update`, which is
`UpdateDriverForPlugAndPlayDevices` with `INSTALLFLAG_FORCE`: "Drivers
installed successfully"); `powercfg /a` then "Standby (S3)" (before:
S1 to S3 "disabled by an internal system component: Graphics").

| Leg | Read | Verdict |
|---|---|---|
| xHCI98 (`INSTALL /console /yes /nopause /usb`, exit 3, restart) | before: the controller, root hub, hub and both mice running, the stick compared, `evt.ring` `00000400`; stand by: QEMU "suspended" 18 s after the call, held 100 s, `system_wakeup`: the OS ran (the line after `SetSuspendState` reached COM1 3 s later), then the guest restarted about 10 s after the wake (event log: 6008 only, no bug check record, no dump). Again with `AutoReboot` 0 and `-d cpu_reset`: the same full reset, no "Triple fault" line, no stop screen | not read: the guest resets after the wake |
| Control: a fresh overlay, Windows' own `USBXHCI`, the same devices | the same reset after the wake. With `-action reboot=shutdown,shutdown=pause` the guest paused at its own reset request: CPU 0 at CPL 0, CPU 1 halted with interrupts off; both processors in one image (the same offsets in two runs whose bases differ by `805000h`) | the vehicle, not this driver |
| Control on upstream QEMU 11.1.0 (scoop) | the same | the vehicle |
| 10 x64 (the 48.13 overlay, xHCI98 x64 installed; `viogpudo` `w10\amd64` by `pnputil`: "Standby (S3)") | S3 entered; after the wake (by keys sent after the call) the same reset request (CPU 1 halted, interrupts off) | the vehicle |

**8.1 x86 on the `pc` machine** (i440fx and `PIIX4_PM`; the q35 command
line with the ICH9 AHCI added as `-device ahci`, `qemu-xhci` on `pci.0`,
everything else unchanged; a fresh overlay, `viogpudo` forced again,
"Standby (S3)"):

| Leg | Read | Verdict |
|---|---|---|
| Control, Windows' own `USBXHCI` | "suspended" 9 s after the call, held 45 s, `system_wakeup`: no reset; Windows at the lock screen (the account's password on wake), `powercfg /lastwake` "Fixed Feature, Power Button"; every device present, the stick read and a written copy compared ("FC: no differences encountered") | the machine resumes |
| xHCI98 on the same overlay (`/usb`, exit 3, then a cold start: the overlay's power-off came by QMP before the typed shutdown acted) | before: `xhci98.sys` `DE752155...5F30`; the controller, "xHCI98 USB 3 Root Hub", "xHCI98 USB Hub", both mice, the stick running (the UAS disk Code 10 under `UASPStor`, as under Windows' own and in 44.22); XHCITR: controller and devnodes started, 4 slots, `evt.ring.value` `00000400`, `evt.ring` `00010400`; the stick compared. Stand by: "suspended" 9 s after the call, held 60 s, `system_wakeup`: the lock screen, unlocked; `lastwake` "Power Button"; the same devices running; XHCITR: started, 4 slots, `evt.ring` `00010400`; the stick read and a written copy: "FC: no differences encountered" | **pass** |

**7 SP1 x86.** With the standard VGA `SetSuspendState` returned at once
("Wake History Count - 0"): `VGAPNP` refuses S1 to S3 (run-36). With no
display adapter at all (`-vga none`, no display device) `VGAPNP` has no
device and `powercfg /a` lists "Standby (S3)". The legs ran blind (no
screen), the whole post-resume part from the one batch file
`SBY7.CMD` (the devices' status, the stick read and a written copy every
30 s for 5 minutes, the snapshot, then a shutdown):

| Leg | Read | Verdict |
|---|---|---|
| q35, xHCI98 (installed on the same overlay with a display first: exit 3, Code 28 until the restart, then every device running) | S3 entered (QEMU "suspended" 7 s after the call), held 60 s, `system_wakeup`, `lastwake` "Power Button"; then for 5 minutes SetupAPI listed no device at all ("No matching devices found", the EHCI and the PCI devices included), the stick gone, the keyboard and the batch file's shutdown not acted on, the processors idle; XHCITR still answered (`evt.ring` `00010400`). An earlier attempt woke within 4 s by itself, then the same | not read |
| Control: a fresh overlay, Windows' own EHCI (the stick, a mouse, the tablet) | the same, twice | the vehicle, not this driver |
| `pc` machine, xHCI98 (the same overlay; the PCI addresses pinned as in its displayed boot, so no devnode is new) | S3 entered (7 s), held 60 s, `system_wakeup`, `lastwake` "Power Button"; at every one of the ten rounds the controller, "xHCI98 USB 3 Root Hub", "xHCI98 USB Hub", both mice, the stick and the UAS disk (`XHCIUAS`) running and the stick's file and a written copy compared ("FC: no differences encountered"); XHCITR before `evt.ring` `00000400`, after `00010400`; the batch file's shutdown then powered the guest off | **pass** |

`evt.ring` reads the TRB count in bits 15:0 and the route in 23:16 (record
13 section 11.10, point 9): 1,024 entries before and after every resume,
through the 16 KiB block (route 0) at 7's boots and 8.1's q35 boot, and
the 32 KiB route (route 1) at 7's resume re-initialization and at 8.1's
boot on the `pc` machine.

**XP SP3** with QEMU's `ati-vga` (`model=rv100`, Radeon 7000,
`1002:5159`) for Windows XP's own ATI driver: the display was never
initialized in 4 minutes and the guest did not answer the keyboard, so
nothing was read. XP's other choices are known: Cirrus vetoes stand-by,
the standard VGA has no driver and no Stand By (run-36, lessons.md).
2000 was not tried (the Cirrus veto and the standard VGA's missing
driver, lessons.md). Whether 7's no-display route also serves XP and
2000 on the `pc` machine is not read.

What the readings say: stand-by and resume with devices attached under
this driver on the 1,024-entry ring pass on 8.1 x86 and 7 SP1 x86 on the
`pc` machine; on q35 neither 8.1 (a reset after the wake) nor 7 with no
display (no device listed after the wake) resumes, and the same happens
under Windows' own drivers there, so the q35 failures are the vehicle's.
No driver defect was seen.

Evidence: `E:\xhci98-vm\p48\sby\` - `w81\` (q35, xHCI98: `serial-a.log`
to `serial-c.log`, `qemulog-c.txt`, `a*.png`, `b*.png`, `c*.png`,
`d*.png`), `w81c\` (q35 controls: `serial-a.log` to `serial-c.log`,
`qemulog-*.txt`, `regs-at-reset.txt`, `regs-at-reset-c.txt`,
`sby-c.out`), `w10\` (`serial-c.log`, `regs-at-reset-c.txt`), `w81p\`
(`pc`: `serial-a.log` the control, `serial-b.log` xHCI98,
`launch-a.cmd`, `launch-b.cmd`, `cmdline-a.txt`, `sbyp-b.out`,
`unlock2-b.png`), `w7n\` (7, xHCI98: `serial-b.log` the install,
`serial-c.log`, `serial-f.log` q35 with no display, `serial-e.log` the
VGA refusal, `serial-pc.log` and `cmdline-pc.txt` the `pc` pass,
`run-*.out`), `w7c\` (the 7 control: `serial-a.log`, `serial-b.log`),
`xpa\` (`a*.png`, `infopci.txt`), `pids.txt` in each; the scripts
`tools\` (`start.sh`, `go.ps1`, `launch-q35.ps1` with `-Vga`,
`q35topc.py`, `sby81.sh`, `sbyp.sh`, `run7.sh`) and `xfer\G.CMD`,
`xfer\SBY7.CMD`; copies in `E:\xhci98-scratch\w48\sby\` (`w81p-*.log`,
`w7n-*.log`, `w7c-*.log`, `w10x64-serial-c.log`).

## 48.13 (the owed cell) The device PDOs' answer to IRP_MN_QUERY_BUS_INFORMATION on 10 x64 (2026-11-09, w48-sby)

The method of the hcd section's "48.13": QEMU guest on the pinned
emulator (`asm4242-20261025-2230`), TCG, the `w10x64` q35 base (10 Pro
22H2 19045, test signing on), a fresh overlay; Windows' own EHCI (the
tablet) and, on `qemu-xhci`, a stick on port 1, a hub on port 2 with a
mouse behind it and QEMU `usb-audio` hot-plugged behind it (port 2.2, the
composite device), a UAS disk on port 3. `BUSQ.EXE` (the hcd leg's tool,
`E:\xhci98-scratch\w48\hcd\busq\busq.c`) lists each present devnode's
stored `BusTypeGuid`, `LegacyBusType` and `BusNumber` to COM1.

**This driver's package**: the amd64 gated build of `465c39b8` (the hcd
branch's, "BUILD + GATES PASSED", `out\gated-amd64-release.txt`:
`xhci98.sys` `78AC1813...48560`, `xhciuas.sys` `587B5DF7...712C`),
staged unsigned; each INF's catalog made on the host from those files by
`package-common.ps1`'s `New-PackageCatalogFile` (WDK 7.1 MakeCat, the
packager's own route; the catalogs unsigned), the family layout by
`ConvertTo-FamilyLayout`; inside the guest the binaries and catalogs
signed with a throwaway certificate (MakeCert `-b 01/01/2026 -e
01/01/2030`, Root and TrustedPublisher, SignTool, SHA-1; the signed
`xhci98.sys` file reads `BBE96379...EDAA`, the image digest the catalog
lists unchanged). `INSTALL.BAT /console /yes /nopause /usb`: exit 3 with
no prompt, "xHCI98 USB 3 Controller ... started", root hub, hub and
"xHCI98 USB Composite Device" started.

| Devnode | Windows' own (`USBXHCI`, `USBHUB3`, `usbccgp`) | xHCI98 (before and after the restart) |
|---|---|---|
| Root hub (`USB\ROOT_HUB30`; `USB\XHCI98_ROOT_HUB`) | USB, 15, 0 | USB, 15, 0 |
| Hub (`USB\VID_0409&PID_55AA`; `USB\XHCI98_HUB&...`) | USB, 15, 0 | USB, 15, 0 |
| Stick (`USB\VID_46F4&PID_0001`, `USBSTOR`) | USB, 15, 0 | USB, 15, 0 |
| UAS device (`USB\VID_46F4&PID_0003`, `UASPStor`) | USB, 15, 0 | USB, 15, 0 |
| Mouse behind the hub (`HidUsb`) | USB, 15, 0 | USB, 15, 0 |
| Composite parent (`usbccgp`; `USB\XHCI98_COMPOSITE&...`) | USB, 15, 0 | USB, 15, 0 |
| Composite function (`&MI_00`, `usbaudio`) | USB, 15, 0 | USB, 15, 0 |
| HID child | HID GUID, 15, n | HID GUID, 15, n (not this bus's) |

USB is `{9D7DEBBC-C85D-11D1-9EB4-006008C3A19A}`, 15 `PNPBus`. 10 x64
matches Windows' own on every PDO of this bus, the composite function
included (WDM 1.20 available). With this the cell the hcd section left
owed is read: 2000, XP, 7, 8.1 and 10 x64 all match.

Evidence: `E:\xhci98-scratch\w48\sby\busq-w10x64-own.log`,
`busq-w10x64-ours-pre-restart.log`, `w10x64-serial-a.log` (the whole
COM1 log, the after-restart BUSQ at its end); `E:\xhci98-vm\p48\sby\w10\`
(`pids.txt`, `cmdline-a.txt`, `a*.png`, `b*.png`); the package
`E:\xhci98-scratch\w48\sby\pkg64\` (`flat\`, `fam\`, `mk.ps1`).

## 48.26 (the owed legs) Ring interrupts: the shared line and the latch to polling (2026-11-09, w48-sby)

The vehicle of the cm section's 7 x86 legs: the `w7x86` q35 base, worker
cm's layout (`launch-cr.ps1 -HostKind min`: `usb4-host host-model=minimal`
on `00:04.0`, its tunnel ports `00:05.0` and `00:06.0`, a fixed `uuid`),
cm's emulator build (`396e4de4...`), a fresh overlay (`ri\`, QMP 46910),
cm's kit `k26` (the gated x86 build of `c18e9679`: `xhcicm.sys`
`3b5669ce...`, `xhci98.sys` `2acce9db...`), installed by `RINT.BAT`
(`/thunderbolt /stor98`, exit 3, "ORICO (D:)"), then `INSTALL /set
XhciCmRingInterrupt=1` and a restart.

**The shared line** (the board's `qemu-xhci` moved onto `pcie.0` with
`msi=off,msix=off`, the tablet on it, no enclosure at the start; 60
pointer moves, the enclosure plugged over QMP, 200 more moves, then
`R.BAT` and `devcon resources`):

| Run | Read | Verdict |
|---|---|---|
| sh, `qemu-xhci` at `00:1c.0` (cm's placement) | the controller a new devnode (it moved off the root port): Code 1, "needs restart", not started; its firmware line IRQ 10, the NHI's 20 | not a shared leg |
| sh2, at `00:0c.0` (the NHI's PIRQ) | again a new devnode, Code 1, not started | not a shared leg |
| sh3, the sh2 machine after a guest restart | the controller "IRQ 20, shared, level", started, `IntrDelivery` 1 (the line); the NHI "IRQ 20 (level, shared)"; `cm.irq.value=00000001`, `cm.irq.resource=00010A01`, `cm.irq.connected=000000B2`, `cm.irq.armed=00010000`, `cm.irq.selftest=00000010` (16 ms); the tablet served throughout; the enclosure plugged after the traffic: "ORICO (D:)"; no `cm.irq.suspect`, `.lost`, `.storm` or `.down` note | **the shared-line leg: pass** |

**The latch to polling**: the qemu flavour's `xhcicm.sys` (`78f2e08a...`,
cm's `k26\Q`, the qemu build of the same commit) copied over the release
file (kept as `xhcicm.rel`, the hash read back in the guest),
`XhciCmQemuIrqDead` 3 written beside `XhciCmRingInterrupt` in the NHI's
driver key, a cold start with no enclosure, then four plug and unplug
cycles 2 s apart, a last plug, 120 s:

| Read | Meaning |
|---|---|
| `cm.irq.suspect=00000003` | two maintenance passes in a row found an unannounced frame; the line had stopped counting after its third claim (the dead value) |
| `cm.irq.selftest.fail=00000000` | the self-test that followed saw no claim |
| `cm.irq.lost=00000001` | the activation returned to polling |
| `cm.irq.down=00000111` | taken down: why 1 (`CM_RIRQ_DOWN_FALLBACK`), end 1 (disconnected), the line proven quiet |
| "ORICO (D:)" after the last plug | the connection served by polling |

**The latch to polling: pass.** With these two legs every leg of 48.26
is read in the emulator (98 SE and ME, the no-interrupt leg and the idle
cost in the cm section).

Not done: the reconnection only at the next activation was not read
(no stop and start after the latch).

Evidence: `E:\xhci98-vm\p48\sby\ri\` (`rep-sh.txt`, `rep-sh2.txt`,
`rep-sh3.txt`, `rep-la.txt`, `serial-*.log`, `cmdline-*.txt`,
`pids.txt`, `i*.png`); copies `E:\xhci98-scratch\w48\sby\ri-rep-*.txt`;
the scripts `E:\xhci98-vm\p48\sby\tools\ri.sh`, `launch-cr.ps1`,
`E:\xhci98-vm\p48\sby\xfer26\RES.BAT`.

## 48.35 - standby and hibernation with a device attached (2026-11-09, partial)

Task 48.35 (owner, 2026-11-09). Design: design record 22 section 11.1
(`926f7ffe`), the owner's answers the same day (Q1 (a), Q2 yes, Q3 to Q5
yes with `XhciCmSleepEntry` held at 0 until the emulator legs pass, Q7 (b),
Q6 at the end), the code `da0573e7` (and the record's revisions with it),
reviewed and revised: the design in three rounds, the code in ten, the
last with no material finding. Branch `w48-tbs`.

**Builds.** Gated `build-driver.cmd release` x86 and `release -amd64` at
`da0573e7`, each `BUILD + GATES PASSED (release)` (logs
`E:\xhci98-scratch\w48\tbs\gated-x86.log`, `gated-amd64.log`), with
`XHCI98_SELFTEST_SKIP_OK=1`. The host suites the change reaches, run alone
on the same tree first: `test_u4cm` 2097 checks, `test_u4v2` 510,
`test_u4xd` 251, `test_tbt_hw` 183, `test_tbt_select` 306, `test_cm` 299,
`test_mark` 192, no failure. `XHCIMARK.EXE` and `XHCIMK64.EXE` built by
`xhcimark\build.cmd`.

**The kit.** The x86 release package staged by `make-package.ps1 -Arch
x86` from that build and laid out by `ConvertTo-FamilyLayout` (x86
`xhcicm.sys` `161fccfb714d24c2...`, `xhci98.sys` `8018d4a1feefd7fb...`,
`stor98.sys` `d2816156e506b380...`), with `XHCIMARK.EXE`
`e3e860b7dcc6174d...` from this tree and the installer and the other
tools from the shared x86 kit (`E:\xhci98-vm\p48\sby\xfer\KIT`, built at
`465c39b8`); `E:\xhci98-vm\p48\tbs\xfer\KIT`.

**Vehicle.** Development host A, the q35 base `w7x86` (7 SP1 x86) on an
overlay `E:\xhci98-vm\p48\tbs\w7`, `launch-q35.ps1` (the `w48-sby` copy
with `-Vga`) `-Usb4 asm4242 -EncAtBoot` (the ASM4242 model, preset
x870e-apex, its pre-OS CM on unless said; the ORICO ASM2464 enclosure with
its NVMe at power-on), the pinned emulator `asm4242-20261025-2230`, TCG,
2 vCPUs, QMP on TCP 47900. Installed by `INSTALL.BAT /console /yes
/nopause /thunderbolt /stor98` (exit 3; the publisher prompts answered),
then `XhciCmSleepEntry` 1 in the machine-wide `Services\USB` key and a
restart.

| Leg | What was done | What was read |
|---|---|---|
| S4, pre-OS CM on (tags b, c) | `W1.BIN` (4 MiB) written to the ORICO volume `D:` and compared; `XHCITR /report`; `shutdown /h /f`; the emulator started again cold (a hibernation resume); the session unlocked | **PASS.** Before: `cm.usb4.sleep=00000101` (1, found), `u4.preserved=00010808` (the pre-OS CM's tunnel kept). After: `u4.sleep.entries 1`, `u4.sleep.routers 2`, `u4.sleep.ready 2`, `u4.sleep.entered=00000002` (the host router and the ASM2464 given Enter Sleep, both read Sleep Ready), `cm.suspend=00000005`, `cm.reenum.hold.passed` 0200, 0208, 0210, `cm.resume=00000001`, `cm.reenum.resumed=00000001` (every obligation met), `cm.reenum.hold.released` for each member; `D:` with `TESTDATA.BIN` and `W1.BIN`, `W2.BIN` written and compared after the resume |
| S3 (tag d, `-vga none`, so that 7 offers Standby) | `W3.BIN` written and compared, `XHCITR /report`, `SetSuspendState`; QEMU read `suspended`; 60 s; `system_wakeup` | The guest suspended and woke (`powercfg /lastwake`: power button); 40 s after the wake `D:` was absent and the report (`XHCITR /report`) carried no connection-manager line; then the guest answered neither the keyboard nor the ACPI power button (both CPUs `HLT`), and was stopped. Not a reading of this change: worker sby read the same on these q35 bases with Windows' own drivers (`E:\xhci98-scratch\w48\sby\run48-sby.md`: 8.1 and 10 reset themselves after the wake, 7 with no display never finishes the resume) - **S3 is not readable on the q35 machine the USB4 models need**; owed to an i440FX kit with the usb4-emu SHPC tunnel bridges, or the bench |
| S4, pre-OS CM off (tags e, f) | the same guest started with `-HostOpts ,preos-cm=off` | no drive at the start (no tunnel reached Windows on this emulator's q35 port, as in 44.27's reading without a pre-boot tunnel); the entry ran on the host router alone (`u4.sleep.routers 1`, `u4.sleep.ready 1`) and the resume met its (no) obligations (`cm.reenum.resumed=00000001`); not a reading of a tunnelled device's sleep |

Not read here, owed: every other leg of record 22 section 11.1.12 (S.1 on
the i440FX SHPC bridge across 2000 to 10, the native-port case with a
working post-start tunnel, the Alpine Ridge and Titan Ridge ICM profiles,
`intel-adl`, 98 SE and ME, the chain, the XP x64 / 7 x64 stall re-read),
the x64 half (the amd64 package needs signing in the guest), the flush of
`xhcimark` at a suspend notice (no S3 readable; at the hibernation leg the
service's log was not collected), and the bench. `XhciCmSleepEntry` stays
0 in the tree.

Evidence: `E:\xhci98-vm\p48\tbs\w7\` (`serial-b.log` to `serial-f.log`,
`cmdline-*.txt`, the screens, `pids.txt`).

### Merge (w48-tbs)

Merged into `3.4.3.0` at `6cb32205` (2026-11-09); the merge reviewed, nothing
material. `XhciCmSleepEntry` stays 0 until the stand-by legs pass; the S3 legs
wait for a q35 machine that resumes (worker q35s3 in `usb4-emu`).

## 48.29 The kept child's code on 8.1 (2026-11-09)

The owner's answer of the day to 48.29's second question (option (a)):
the kept child's real code for 8.1, with the prototype (`w48-stor-proto`,
never merged) as the behavioural reference.

**The design**: design record 31 section 4.10 (`2b72e7b7`), reviewed and
revised over three rounds before the code: the systems (the id on 2000
to 8.1, the kept child on 8.1 alone, `FastIoDetachDevice` installed only
there), the state (Attached, Starting, Logical, Detached) and the
reference rule over the existing `Outstanding` count, every dispatch
admitted under the child's lock with the object below referenced in the
same critical section, the chain's holds referencing the PDO, the
logical removal with its departure recorded once, the destruction (a
work item above `PASSIVE_LEVEL`, the image kept loaded on 8.1), one
detach owner for every object kind whose loser (the callback included)
waits, presence in the marks only at Attached, the 6.3 models the id
alone and a 10.0 class model for 10, and the update from an earlier
release (the installer's three "no tunnel id" reasons, the mark
service's `MK_S98_W_NOID`). What the review changed from section 4.9:
the persistent gate of item 10 became the mark service's own rule (on
8.1 a drive is wanted only with the id); Starting is not presence (a
start that failed would have lost a pending departure); the callback
waits when it loses the detach claim; the route for a drive that never
gets the id on 8.1 (an override-only port, past the chain's depth) is
withdrawn and said.

**The code**: `8a7c25c6`, reviewed and revised over two rounds (the
first found the lower object referenced after the admission's lock was
released, fixed by taking it inside; the second: approve). `src/cm`
(`cm.h`, `cm_entry.c`, `cm_filt.c`, `cm_reenum.c`, `cm_nhi.c`,
`cm_oscnt.c`, `cm_ports.c`), the import gate's `ExQueueWorkItem` rows,
`stor98-x86.inf` and `stor98-x64.inf` (`NTx86.6.3`/`NTamd64.6.3` the id
alone; `NTx86.10.0`/`NTamd64.10.0` the class as the compatible id) with
`check-stor98-inf.ps1` (86 self-test cases), `xhcimark`
(`MK_S98_IN.TunnelId`, `MK_S98_W_NOID`), `xhciinst`
(`inst_s98_noid_why`, `INST_S98_SKIP_NOID_RUN`/`_MARK`/`NOID`, the
earlier `stor98.inf` kept while a drive on it was left). Host tests:
`test_cm_ports` 360 checks (the systems, every state and event, and the
orders: a disable and enable, a parent's removal, a surprise unplug, a
refused and a failed start, a repeated removal, a callback under a usage
notification released above `PASSIVE_LEVEL`, the uninstall), `test_inst`
9035, `test_mark` 190, all passing.

## 48.28 Taking a controller the BIOS set up: ME (2026-11-09)

**ME, the controller's own `stortakefirmware=1`** (owed from the
morning: every start of stor's `fme` faulted in Safe Mode in SeaBIOS's
NVMe INT 13h path). A copy of that overlay (`E:\xhci98-vm\p48\stor2\fme\`,
the `2c904543` x86 kit installed, Stor98 bound to the internal drive
`00:05.0` by `XHCIDI`, `AdapterSettings` `stortakefirmware=1` in its
driver key `SCSIAdapter\0001`, `StorTakeFirmware` 0). The way round the
Safe Mode fault: one start **without** the internal NVMe (the machine
otherwise the same), which came up in normal mode to the desktop and
was shut down cleanly (`rundll32 shell32.dll,SHExitWindowsEx 1`); then
the cold start with the internal drive back (`bootindex=1`, so SeaBIOS
sets it up and serves it through INT 13h):

| Step | Result |
|---|---|
| Normal-mode start, the take on | the desktop drawn in about 2 minutes (`s-f2.png`) |
| Then | the guest stopped: the clock frozen at the minute the desktop came, the hourglass, no input taken, for 16 minutes (`s-f4.png`, `s-f5.png`); the processor sampled again and again in SeaBIOS's 32-bit code (EIP in E0000h to EFFFFh) and low memory, the BIOS's own code, not Windows' |

So on ME a set-up controller taken by its own setting hangs the start
once the desktop is drawn: something of ME's (not read which: the shell
or System Restore touching every drive) reaches the BIOS's INT 13h for
the drive whose queues the take has reset, and SeaBIOS's NVMe code does
not return, the hazard the installer's warning names ("Can hang Windows
startup ..."). On 98 SE the same take started to the desktop and only
the BIOS's own drive letter failed ("General failure", stor's leg). The
registry (`StorFind 0.5.0`) could not be read: the guest never took
input again. Not separable here: whether SeaBIOS's INT 13h path hangs on
an untaken drive too (stor's 98 SE note: `dir D:` through it did not
return in six minutes before any take).

**Not run**: the warm start and the shutdown and removal of a taken
drive on ME (the start does not survive the take).

Evidence: `E:\xhci98-vm\p48\stor2\fme\` (`cmdline-e.txt`,
`cmdline-f.txt`, `s-e*.png`, `s-f*.png`, `pids.txt`). Two launches at
17:39 and 17:41 kept stor's paths by mistake and ran about a minute
each on stor's own `fme` disk before they were stopped (logged in
`pids.txt`); that overlay was already the faulting one, and no leg reads
it.

## 48.20 UAS on 98 SE with NUSB's storage (2026-11-09)

Stor's morning leg left it: a QEMU `usb-uas` disk hot-plugged on the
board's `qemu-xhci` attached at 12 Mb/s and 98 SE showed nothing. Read
again on a copy of that overlay (`E:\xhci98-vm\p48\stor2\f98\`, 98 SE
with the `2c904543` x86 kit installed with `/storage`, so NUSB's storage
files are in), the internal NVMe left out:

| Step | Result |
|---|---|
| `usb-uas` with a 256 MiB `scsi-hd` **present at power-on** on `qemu-xhci` port 3 | attached at 5000 Mb/s, address 1 (QEMU `info usb`); at the desktop Windows' wizard for "xHCI98 USB Attached SCSI Storage", which found `C:\XHCI98\XHCIUAS.INF` and installed it; `Enum\XHCIUAS\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\...` ("USB Disk", `Storage\0001`) and its disk `Enum\SCSI\QEMU____QEMU_HARDDISK___2\XHCIUAS&DISK...` (`DiskDrive\0005`, no letter: the disk has no partition); `FDISK /STATUS` lists it as disk 4, 254 MB, unpartitioned, beside the IDE disk, the NVMe drive and NUSB's `usb-storage` disk |
| That disk unplugged, and a `usb-uas` hot-plugged on port 3 again | QEMU attaches it at **12 Mb/s**, address 0: not enumerated |
| A `usb-storage` stick hot-plugged on port 3 | 5000 Mb/s, address 1: enumerated |
| A `usb-kbd` hot-plugged on port 3 | 480 Mb/s, address 1: enumerated |
| A `usb-serial` (full speed only) hot-plugged on port 3 | 12 Mb/s, address 0: not enumerated |
| A `usb-serial` **present at power-on** on port 4 (beside the UAS disk on 3) | 12 Mb/s, address 0 after the desktop: not enumerated; the UAS disk at 5000 Mb/s enumerated again |

So UAS on 98 SE with NUSB's storage installed works at SuperSpeed: the
disk is enumerated, `xhciuas.sys` installs and binds, and the disk
answers. What failed in the morning is not UAS: this emulator attaches a
hot-plugged `usb-uas` at full speed (12 Mb/s), and **no full-speed
device on this machine's `qemu-xhci` is enumerated by `xhci98.sys` on
98 SE, at power-on or hot-plugged** (a `usb-serial` the same), while
high-speed and SuperSpeed devices are. Whether that full-speed gap is
`xhci98.sys`'s or this emulator's `qemu-xhci` model's is not read here
(the HCD's own work, outside 48.20; real full-speed keyboards and mice
run on `xhci98.sys` on real boards); a UAS disk hot-plugged at
SuperSpeed cannot be made in this emulator.

Evidence: `E:\xhci98-vm\p48\stor2\f98\` (`cmdline-a.txt`,
`cmdline-b.txt`, `ev-a-enum.log`, `serial-a.log`, `s-w*.png`, `s-b0.png`,
`pids.txt`).

## 48.29 The code's legs on 8.1 x64 under Driver Verifier (2026-11-09)

**The vehicle**: a fresh overlay of the q35 8.1 x64 base
(`E:\xhci98-vm\p48\stor2\v64u\`, `-Usb4 asm4242 -EncAtBoot -Tablet`,
`-device usb4-debug-port`; drive A in the ORICO model (`asm2464`, its
switch) on USB4 port 1; from the second start drive B in a second
`asm2464` enclosure on port 2). The `3.3.0.0` release
(`releases\3.3.0.0`, `release-x64` and `tools`) and this tree's amd64
package (the gated `release -amd64` build of `dc8f51a2`, "BUILD + GATES
PASSED (release)"; `make-package.ps1 -Arch amd64`,
`ConvertTo-FamilyLayout`, the tree's rebuilt tools, its six catalogs made
unsigned with the packager's `New-PackageCatalogFile`; `xhcicm.sys`
`289dc8db...`, `stor98.sys` `4e717913...`, `xhci98.sys` `4426d4b5...`
before the guest's signature), each signed in the guest with a throwaway
certificate made there (MakeCert, Root and TrustedPublisher; never the
project's key, nothing signed on the host); test signing on. A first try
turned Verifier on before `3.3.0.0` was installed: `3.3.0.0`'s own
`xhcicm.sys` then stopped Windows at its first start (the Recovery
screen; 45.31's Verifier stop in that release), so that overlay was
dropped and the leg run again with Verifier on only from the update's
restart: `verifier /standard /driver xhcicm.sys` on this tree's image.

**The update from `3.3.0.0`, one drive on Stor98 and one on `stornvme`**:

| Step | Result |
|---|---|
| `3.3.0.0` installed (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`), drive A present | exit 3; "Stor98 Storage Controller for NVMe SSDs: stor98 3.3.0.0, started"; the mark service installed |
| Its mark service stopped (`sc stop`, start demand), shut down, drive B added, cold start | A: `stor98`, problem 0, no tunnel id (8.1 under `3.3.0.0`); B: `stornvme`, no tunnel id |
| This tree's kit run as the update, the earlier connection manager still running | exit 4; A "INF stor98.inf; driver stor98 3.3.0.0; Update / Skipped: no tunnel id yet (needed on 8.1): run Setup again after the restart"; B "driver stornvme ...; Install / Skipped: no tunnel id yet (needed on 8.1): the mark service adds Stor98 after the restart"; `stor98.inf` staged as `oem6.inf`, the earlier one (`oem3.inf`) kept; "Stor98 is skipped (see above): restart Windows, then run INSTALL.BAT again to update it." |
| The restart (Verifier on `xhcicm.sys` from here) | both drives carry `PCI\XHCI98_TUNNEL&CC_010802` first; A: event 400 at the start, `oem6.inf`, rank `0xFF2000`, Matching Device Id the tunnel id, `stornvme.inf:PCI\CC_010802:00FF2007 oem3.inf:PCI\CC_010802:00FF3007` outranked, "Device Updated: true" (Windows itself moved it to the id model: its ids had changed and the id model outranks the earlier class model); B: the same event 29 s later, the mark service's 8.1 arm; both `stor98` 3.4.2.0, problem 0 |
| Setup run again | both "Reinstall (same version)"; "All devices are up to date"; "Stor98's earlier oem3.inf was removed from the driver store."; exit 3 |

**The conditions**, the same machine, a cold start with drive B now in
an enclosure with no switch (`usb4-device` with no `device-model`: the
drive at bus 6 directly below tunnel port 2), each step read by the
devnodes' service, problem and compatible ids and the last PnP events
(`R.CMD`), Verifier on `xhcicm.sys` throughout:

| Condition | Result |
|---|---|
| A cold start, the direct drive new | the direct drive tagged at its first enumeration and configured by Windows itself on `oem6.inf` (rank `0xFF2000`, the tunnel id; `stornvme` outranked); A on Stor98 |
| Drive A disabled and enabled (`devc64 cycle`) | Stor98, problem 0, the id kept, no new event 400 |
| The enclosure DSP above A disabled and enabled (a parent's restart) | the same |
| **A parent's removal with the drive already disabled**: A disabled, its DSP disabled and enabled, A enabled | the same |
| **A drive directly on a tunnel port**: the direct drive disabled and enabled, then tunnel port 2 itself disabled and enabled | the same, for both drives |
| **A removal held by open handles**: `hold 150` opening `\\.\Scsi0:` to `Scsi2:`, enclosure A removed (QMP `device_del`) 15 s in | the handles held 150 s, then closed; drive A gone, the direct drive on Stor98; no bug check |
| **A re-plug**: enclosure A plugged in again | A back with the id, Stor98, problem 0, no new event 400 |
| **The uninstall** (`INSTALL.BAT /console /uninstall /yes /nopause`), then the restart | exit 3 ("Stor98's oem6.inf was removed", "The stor98 service was removed", the filters taken off, "RESTART Windows"); the restart clean, no bug check; both drives on `stornvme`, problem 0 |

So on the real code the update from `3.3.0.0` ends with both drives on
Stor98's id model and the earlier INF gone: the first run leaves both
drives as they were and says why, the restart brings the id (and here
Windows itself moved the drive already on Stor98), the mark service
takes the `stornvme` drive, and the second run removes the earlier
`stor98.inf`. Every condition section 4.9 named holds on 8.1 x64 under
Verifier with the release image, with no bug check. The `cm.kept`
records were not read: the release image does not write them where the
emulator can read them (the prototype's readings had them).

**Not run**: 8.1 x86; the eject; a driver update over a running drive
other than the update leg.

Evidence: `E:\xhci98-vm\p48\stor2\v64u\` (`ev-i33.log`, `ev-r1.log`,
`ev-inew-a.log`, `ev-r2.log`, `ev-inew-b.log`, `ev-r4.log`,
`ev-list.log`, `ev-c1.log` to `ev-c7.log`, `ev-u.log`, `serial-*.log`,
`s*.png`, `pids.txt`; the first try in `old-a\`).

## 48.28 98 SE: a warm start with the BIOS's controller taken (2026-11-09)

The `f98` copy, the internal NVMe (`00:05.0`, `bootindex=1`) back,
`StorTakeFirmware` 1 (as stor left it): the start took both controllers
("StorFind 0.5.0 ... cmd 0107 cc 00460001 csts 00000001 taken: already
set up, overridden by StorTakeFirmware; result 1"; real-mode ScanDisk
first reported "cannot read from the last cluster on drive D", the
BIOS's own letter of that drive, after the earlier unclean stop). The
per-adapter `StorFind` values deleted, then a warm start of Windows alone
(`rundll user.exe,exitwindowsexec`: back to real mode and Windows started
again, no BIOS): the screen stayed black and the processor in the VMM
(`c000a8xx`) for 11 minutes; Windows did not come back. Not separated
here from the warm restart this emulator's 98 SE and ME guests are known
not to survive (the shut-down-and-cold-start rule), and no control
without the take was run. The shutdown and removal of a taken drive stay
unread (stor's shutdown attempt did not start in 200 s).

Evidence: `E:\xhci98-vm\p48\stor2\f98\` (`cmdline-c.txt`,
`ev-c-take.log`, `serial-c.log`, `s-c*.png`, `pids.txt`).

### Merge (w48-stor2)

Merged into `3.4.3.0` at `dda619e8` (2026-11-09), the conflicts in
`cm_ports.c`, `cm_ports.h` and `test_cm_ports.c` resolved by keeping both
sides (the chain admission and the kept child). The merge review found
one defect: a kept child's departure recorded under the member's
generation instead of its token's kid generation, so a chained entry's
eject was never confirmed. Fixed in `45ceb0c1` (`CmMarksDepartGen`, used
by both departure paths, with a host vector), merge `10cd187a`. The
integration build of `a2d7fc7d` passed. The owner's answers of the day:
on 8.1 a drive with no tunnel id yet is accepted and the installer says
so; on ME `StorTakeFirmware` is refused (the start does not survive the
take there).

## 48.20 (follow-up) The 98 SE "no Full Speed device" reading (2026-11-09)

Stor2's 48.20 reading said no Full Speed device is enumerated by
`xhci98.sys` on the q35 98 SE rig's `qemu-xhci`, at power-on or
hot-plugged (a hot-plugged `usb-uas` and a `usb-serial` on
`-chardev null`, both "12 Mb/s, address 0" in `info usb`). That
contradicted the uac1 legs (Full Speed audio under xHCI98 on 98 SE,
2026-11-07, 2026-11-09). Read again on a copy of stor2's overlay
(`E:\xhci98-vm\p48\fs98\a\`, an overlay of the q35 `w98se` base with the
`2c904543` x86 kit, `/storage`), stor2's own `cmdline-b` (the
`asm4242-20261025-2230` emulator, QEMU 11.1.2 `02dda40`, `-machine q35`,
`qemu-xhci,p2=4,p3=4` behind a `pcie-root-port`, the asm4242 USB4 stack)
with QEMU's `usb_port_*`, `usb_xhci_*` and `usb_desc_*` trace added
(`cmdline-a1.txt`, `cmdline-a2.txt`, `trace-a1.log`, `trace-a2.log`):

| Step | Result |
|---|---|
| a1: `usb-serial` on `-chardev null` at power-on, port 4 (stor2's case) | `qom-get ser0 attached` **false**; xHCI port 8 (its USB 2.0 port; this QEMU numbers the SuperSpeed ports 1-4 first) at `pls 5`, `PORTSC` `000202A0` (CCS 0) through every reset; `info usb` "12 Mb/s", `Device 0.0`: QEMU never attached it, the driver was never told |
| a1: hot-plugged `usb-mouse`, port 2 | attached at 480 Mb/s, enumerated (wizard "USB Human Interface Device") |
| a1: hot-plugged `usb-serial`, `always-plugged=on`, port 1 | **Full Speed**, port 5: reset, Enable Slot, Address Device, the 8-byte read, Evaluate Context (its `bMaxPacketSize0` 8), the descriptors and strings; address 3; Windows' wizard "QEMU USB SERIAL" |
| a1: hot-plugged `usb-uas` + `scsi-hd`, port 4 | before `qom-set attached true`: "12 Mb/s", `Device 0.0`, `attached` false (stor2's reading exactly); after it: 5000 Mb/s, address 4, BOS read, wizard "xHCI98 USB Attached SCSI Storage" |
| a2: stor2's `cmdline-b` with `always-plugged=on` on the power-on `usb-serial` | port 8 connected (`pls 7`, then `pls 0` after reset), address 2, Evaluate Context, the descriptors; wizard "QEMU USB SERIAL" |

**Cause: the setup, not `xhci98.sys` or QEMU's xHCI model.** Both devices
were never attached in QEMU: `usb-serial` attaches only when its chardev
raises the open event (or with `always-plugged=on`), and `char-null` never
raises it; a hot-plugged `usb-uas` (like `usb-bot`) sets `auto_attach = 0`
and waits for `qom-set ... attached true`. "12 Mb/s" in `info usb` is the
default speed `usb_desc_init` gives a device before any attach, and
`info usb` lists unattached devices (`hw/usb/bus.c`). The same trap is
lessons.md's "A device can occupy a QEMU port without being attached"
(11-V); a paragraph on this reading was added there (`a35c3256`). Full
Speed enumerates on this rig at power-on and hot-plugged; nothing for XP
or 7 to bound; no issue document. A hot-plugged UAS disk at SuperSpeed can
be made in this emulator (stor2's "cannot be made" no longer holds): add
`usb-uas`, its `scsi-hd`, then `qom-set /machine/peripheral/<id> attached
true`.

Evidence: `E:\xhci98-vm\p48\fs98\a\` (`cmdline-a1.txt`, `cmdline-a2.txt`,
`trace-a1.log`, `trace-a2.log`, `s2.png` to `s7.png`), `pids.txt`.

### Merge (w48-fs98)

Merged into `3.4.3.0` at `a2d7fc7d` (2026-11-09), `lessons.md` only.

### Merge (w48-r57, the hub retry)

The merge review of `e840d1df` found that a hub port's failed request to
disable its SuperSpeed side was not retried. Fixed in `aea4405a`
(`HcdHubFlapSend`: the request kept pending and retried on the hub's
re-arm tick, three attempts in all, a fresh port status read before each,
and the re-arm scheduled whenever the port may be left at SS.Disabled;
design record 13 section 21.5), merge `63128209`, reviewed with no
findings. Not read on hardware: it needs a SuperSpeed hub between the
adapter and the port.

## 48.35 (the vehicle) S3 resume on QEMU's q35 machine (2026-11-09, w48-q35s3)

Owner decision 2026-11-09: find why q35 cannot resume Windows guests from S3 (sby's
48.15 readings) and fix it in usb4-emu. QEMU under TCG on development host A, the
emulator of the q35 bases (`asm4242-20261025-2230`, `-L` usb4-emu
`vms\qemu-baseline\share`), fresh overlays of the q35 bases, SeaBIOS's debug port
(0x402) logged, the chipset's state read over QMP (`qom-get` of `ICH9-LPC`
`pm_io_base` and `q35-pcihost` `MCFG`). Evidence `E:\xhci98-vm\p48\q35s3\`.

**Cause: SeaBIOS, not QEMU's ICH9 model or Windows.** QEMU wakes a machine with a
full reset (`pc_machine_wakeup()`), which on q35 disables the MCH `PCIEXBAR`
(MMCONFIG) and resets the ICH9 LPC `PMBASE`, `ACPI_CNTL` and `RCBA` and the SMBus
base, as a chipset losing core power would. SeaBIOS rel-1.17.0 (the one QEMU 11.1
ships) keeps the MMCONFIG address it set at boot (its shadowed F segment survives,
PAM is not reset) and sends every `pci_config_*()` call of its resume path through
it, so every chipset write of `s3_resume()` went to a window that no longer decodes:
`smm_setup()` (first, and reading `SMI_EN` through the not yet restored PM base),
`pci_resume()`'s `PMBASE`/`ACPI_CNTL`/`RCBA`/SMBus writes, and last the `PCIEXBAR`
write itself. Windows got control with no ACPI PM block (no PM timer, PM1, GPE0) and
no MMCONFIG. The `pc` machine restores its PIIX4 PM base through port I/O, so it
resumes. QEMU cannot repair this on its side: SeaBIOS writes `PCIEXBAR` to 0 first,
through MMCONFIG, so the window is off at the end whatever the reset leaves.

**Fix (usb4-emu `w48-q35s3`, commits `dca43fd`, `153add5`, `06f2d70`; QEMU fork commit
`b5927e8`):** a SeaBIOS patch (`src/seabios/0001-resume-restore-the-q35-MMCONFIG-window-before-any-co.patch`):
`pci_resume()` programs `PCIEXBAR` first, by port I/O, and `s3_resume()` restores the
chipset before `smm_setup()`. Built by `src/build-seabios.sh` (QEMU's
`config.seabios-256k`, WSL Ubuntu gcc 15.2.0, reproducible) into the fork's
`pc-bios/bios-256k.bin`, version `rel-1.17.0-1-g7a5a6afa-usb4emu`, SHA-256
`e5dc0550ec5e8613e1abaeae8b5102b660e451e78c55c8f5746dca9b8619138b`; copy for other
workers `E:\xhci98-vm\p48\q35s3\bin\bios-256k.bin`. A launcher that keeps its own
`-L` adds `-bios <that file>`; no emulator rebuild is needed (QEMU's code is
unchanged).

| Leg | SeaBIOS | Read | Verdict |
|---|---|---|---|
| 7 x86, no USB, `-vga none` (`w7a`) | stock | resumed, 92 devices running before and after; after the wake `pm_io_base` 0, `MCFG` unmapped (the I/O tree shows the PM block disabled) | the fault read |
| 8.1 x86, Windows' own drivers, virtio-vga + `viogpudo`, `qemu-xhci` behind a `pcie-root-port` (stick, hub, mouse), EHCI tablet (`w81`, leg a) | stock | "suspended" 10 s after `SetSuspendState`, held 30 s, `system_wakeup`: the guest asked for a reset 5 to 10 s later; CPU 0 `EAX=0000000F EDX=00000CF9` (the FADT reset register and value), CPU 1 `HLT`, `regs-at-reset-a.txt`; `pm_io_base` 0, `MCFG` unmapped | sby's failure reproduced |
| the same (leg b) | fixed (code of `e5dc0550`, version `-usb4emu-s3`) | resumed to the lock screen, `lastwake` "Power Button"; the 11 devices (controller, root hubs, hub, both HID, stick, disks) running; stick `BIG.BIN` and a written copy: "FC: no differences encountered"; `pm_io_base` 600h, `MCFG` B0000000h | **pass** |
| 8.1 x86, xHCI98 (sby's kit, `xhci98.sys` `DE752155...5F30`, `/usb`, exit 3, cold start; leg c/d) | `e5dc0550` | before: controller "xHCI98 USB 3 Controller", root hub, hub, both mice, stick running, `evt.ring` `00000400`, stick compared; S3 10 s, held 30 s, wake: unlocked, the same 11 devices running, XHCITR started, `BIOSVersion rel-1.17.0-1-g7a5a6afa-usb4emu`, `evt.ring` `00000400`, the stick and a written copy compared | **pass** |
| 7 x86, Windows' own drivers (EHCI stick, mouse, tablet; `qemu-xhci` behind the root port, no driver), `-vga none`, `Q7.CMD` (`w7c`, leg a) | stock | S3 entered, woken: 101 devices running before, 92 in each of 6 rounds after; `pm_io_base` 0, `MCFG` unmapped; the keyboard not answered afterwards | sby's failure reproduced |
| the same (leg b) | `e5dc0550` | woken: 102 running in every round (the stick's volume mounted after the wake), the stick and a written copy compared in all 6 rounds; `pm_io_base` 600h, `MCFG` B0000000h | **pass** |
| 7 x86, xHCI98 (`/usb`, exit 3; stick, hub and mouse on `qemu-xhci`), `-vga none` (`w7c\x`, leg l) | `e5dc0550` | S3 entered (7 s), held 30 s, woken: 106 devices running before, 107 in each of 6 rounds after (the stick's volume mounted after the wake); the stick (on `qemu-xhci`, which only xHCI98 drives on 7) read and a written copy compared in all 6 rounds; `pm_io_base` 600h, `MCFG` B0000000h | **pass** |
| Thunderbolt: 7 x86, `launch-q35.ps1 -Usb4 asm4242 -EncAtBoot -Vga none` (ASM4242 host, ORICO at power-on), Windows' own drivers (`tb\`) | `e5dc0550` | S3 entered (6 s), held 30 s, woken: 100 devices running before and in each of 6 rounds after (the NVMe and the NHI without a driver on 7, Code 28, as before); `info pci` after the wake: the ASM4242 switch, both tunnel ports, the board xHCI and the enclosure's NVMe (`tp0-sw1-nvme`) at bus 5 (`tb\infopci-after.txt`); `pm_io_base` 600h, `MCFG` B0000000h | **pass** (enters and resumes) |

Side readings: the wake reset also clears `PM1_CNT.SCI_EN` (QEMU keeps it only with SMM
off); Windows 7 set it again itself (`PM1_CNT` 0001h after the wake with the fix), so
a second patch restoring it in SeaBIOS was built, tried and dropped. Woken by
`system_wakeup` with no input afterwards, Windows 7 goes back to S3 about two minutes
later (its unattended-wake timeout), which made the power-button checks of the 7 legs
unreadable. On 8.1 the lock screen's password box needs a click before typing.

Reviewed: the SeaBIOS patch approved (no findings; the dropped `SCI_EN` patch
also approved); the usb4-emu commit's first review two P2s in `build-seabios.sh` (version guard
glob, a stale `git am`), fixed in `153add5`; then approved (no findings).

Upstream: the fault is SeaBIOS's (present in its master `28aa4634`, 2026-10-22); the
patch applies there; a short description is in usb4-emu `docs/Q35-S3-RESUME.md`
section 5. The owner decides whether it is sent.

### Merge (w48-q35s3, usb4-emu)

Merged into `usb4-emu` main at `f800432` (2026-11-09). The S3 legs of
48.35 run with that BIOS image (`-bios`, SHA-256 `e5dc0550...`), the
pinned emulator unchanged.

## 48.31 follow-up: the chain operation's admission, the first plug's problem 29, the 9x removable wording, the chained functions' interrupt (2026-11-09)

Worker w48-chain2, development host A, branch `w48-chain2` from
`1b4814a6` (3.4.3.0 with w48-chain merged).

### The chain operation's admission (merge review of w48-chain)

The cause: in `cm_cfgwk.c`'s chain loop the gates (`ReqGone`, `NoScan`,
`Hold`, the prune, the vanished-child holds) were read before the
operation's generation was taken (`cmWkGenOf`), so a
`CmCfgwkUnavailable` or `CmCfgwkGone` posted between the two lent the
operation the loss's own generation: `cmWkCurrent` then passed while the
loss's removal and no-scan re-enumeration were owed, and the down path
could reach `Reenumerate_DevNode` of the chain port (whose admission,
`cmWkEnumAdmit`, does not read `ReqGone` or `NoScan`) before the owed
removal.

The fix (`5a2080d2`, reviewed and revised over four rounds): the
generation taken before the gates; one predicate, `cmWkChainCurrent`
(pure half `CmPortsChainCurrent`: not leaving, owned, the generation
taken, no `ReqGone`, `NoScan` or `Hold`), holds every step of the chain
operation: the walk's, the port reading's and the path scans' reads
(accessors of the chain operation's own), each numbering attempt (its
reads refused only before its first write, so its restoration still
reaches the bridges; its writes never refused), the vanished-child
scan, every CONFIGMG call (`cmWkGo`), the removal's query (before the
claim and after the identity read back), and the chain port's
`Reenumerate_DevNode` admission (`cmWkEnumAdmitEx`, under the lock that
marks the call in flight), the rescan an accepted removal owes after
going stale included. The member's own path unchanged. Design record 34
revision 14, section 3.3.7 item 1, "The admission". Host vector:
`test_cm_ports` `test_chain_admit` (a loss posted between the gates'
read and the admission refused; the loss's own generation refused while
owed; stale after a loss whatever follows, a healthy post that clears
`NoScan` included; another member's loss; a barrier; a lost claim; the
exit).

### The first plug's problem 29 (98 SE and ME)

The cause, read on 98 SE (leg c and d below, a fresh overlay, the
emulator's configuration-write trace and a CFGMGR32 walk): at the first
chained ASM2464 the worker numbered the chain port, re-enumerated it and
opened the member's no-IRQ window (record 28, 10 s); `PCI.VXD` made
devnodes for the ASM2464's two bridges with no driver (problem 28),
which the window excluded by class; the NVMe at 06:00.0 got no devnode
until the Add New Hardware wizard had installed "PCI standard
PCI-to-PCI bridge" (on 98 SE it then asked "To finish setting up your
new hardware, you must restart your computer", answered No), and a
wizard bound Stor98 to it minutes after the window had closed: problem
29 (`st=08000620 pr=29`), no BAR, no `cm.reenum.9x.noirq` line for it.
At the next plug (the bridges' driver in place) the window admitted it
within its 10 s and the memory-only form started it. Neither the chain
request's window (it had done its work) nor the numbering was at fault;
the no-IRQ window was too short for a function behind bridges being
installed.

The fix (`8edfd362`, record 28 section 14, reviewed and revised over a
design round and two code rounds, and `59a28f59`, reviewed, after the
first ME leg below found the hold never fired): the window is held while the walk
meets a descendant being installed (`DN_HAS_PROBLEM` with problem 1, 8,
18 or 28, bridges included, or no `Driver` value in its hardware key -
the new bridges read no problem during the window and problem 1 only
once their wizard was up, `59a28f59`) or an entry is PENDING for an installation
(an installation problem, or its Stor98 binding not written yet): its
start moves to the reading, bounded at `CM_NOIRQ_HOLD_MS` (10 minutes)
from its opening; the walk reads each descendant's status; the first
hold of a window logs `cm.reenum.9x.noirq.hold` (depth 15:8, problem
7:0) and is counted (`Held`). Record 34 section 3.3.7 item 10 says why
the chain operation itself is not changed. Host vectors:
`test_cm_noirq` `test_hold` (the predicate; a hold at 9 s, the close 10 s
after the last hold; the exact 10-minute bound; no reopening of a
closed window; a late function admitted and started; a new opening's
own bound; problem 28 then 29), the binding-absent vector revised (held,
then closed at the bound) and a pass delayed past 10 s held by the
entry's own last reading.

### The chained functions' interrupt line (item 3)

Harmless, and the design's: `cm.chain.9x.irq=01030001` is outcome DONE
(1), no line set (0), three functions on another pin (3), rotation 1 -
not three lines written, as task 48.31's run read it. The emulator's
chain port is device 1 of the dock's internal bus, so the ASM2464's
functions (pin A, device 0) arrive at the member as pin B, whose route
this driver does not know; the line rule writes nothing and the
Interrupt Line stays 0. The emulator's configuration-write trace
(`-d trace:pci_cfg_write`) shows no write to the NVMe's 3Ch from this
driver or `PCI.VXD` at either plug (only `PCI.VXD`'s FFh then 0 to the
ASM2464's downstream bridge). `PCI.VXD` gives it no IRQ (problem 29) and
record 28's memory-only form starts it with Stor98 polling: "StorInterrupt
6.0.0 = pin 1 line 0 msi no msix yes port 0/0 polling (interrupt line
not set)", against the dock's NVMe, "StorInterrupt 3.0.0 = pin 1 line
10 ... port 10/0 interrupt". Recorded in record 34 section 3.3.7 item 4,
"As read". Nothing to fix.

### The 9x removable wording (item 2)

98 SE and ME set `DN_REMOVABLE` on nearly every devnode (task 48.31's
DNSTAT walks; here again: the host bridge, the root devices, ACPI's), so
"marked removable by the Thunderbolt driver" was not true there. Places
that showed it: `XHCIINST /devices` (`inst_eng.c`, the 9x branch of a
unit's line) and the USB Tree's unit details (`xhcitr.c`), and record
24's section 4.3 and its example (record 25 had no 9x statement of it).
`92f3d039` (reviewed, nothing material): both from one pure helper,
`inst_removable_9x_text`: "Marked removable." / "Not marked removable."
(XHCITR), "marked removable" / "not marked removable" (XHCIINST). NT
unchanged. Records 24 (revision 9) and 25 (revision 31). Host vector:
`test_tr` `test_removable_9x`.

### Builds

- x86 `all` of `5a2080d2` (`XHCI98_SELFTEST_SKIP_OK=1`): "BUILD + GATES
  PASSED (release qemu)" (`E:\xhci98-scratch\w48\chain2\gb-x86.log`).
- amd64 `all -amd64` of `5a2080d2`: "BUILD + GATES PASSED (release
  qemu)" (`gb-x64.log`).
- x86 and amd64 `all` of `8edfd362`: both "BUILD + GATES PASSED (release
  qemu)" (`gb-x86b.log`, `gb-x64b.log`).
- x86 and amd64 `all` of `59a28f59`: both "BUILD + GATES PASSED (release
  qemu)" (`gb-x86c.log`, `gb-x64c.log`).
- XHCIINST and XHCITR of `92f3d039` built by their own `build.cmd`
  (`tools.log`; `XHCIINST.EXE` `601c3b1f...`, `XHCITR.EXE` `be2a1ff6...`);
  `test_tr` 1769 checks, `test_cm_ports` 288, `test_cm_noirq` 331, 0
  failures (with the gated builds' full suites).

### Legs

Emulator: chain's copy, `E:\xhci98-vm\p48\chain\bin\qemu-system-x86_64.exe`
(QEMU v11.1.2 with `usb4-emu` `de1ed69`), its launch wrapper `lx.ps1`;
q35, `-Usb4 adl`, the dock `usb4-device,id=enc0,...,downstream-ports=1,
chain-power-controller=off` at power-on, the child plugged by QMP
`usb4-device,id=enc2,bus=u4.0,parent=enc0,port=1,drive=nvme3,
device-model=asm2464`. Fresh overlays of the q35 bases; the release
package installed (`XHCIINST /dir 98-me /console /yes /nopause
/thunderbolt /stor98`), the qemu flavour's `xhcicm.sys` and `xhci98.sys`
copied over (`copy /y`; without `/y` the 9x `COPY` waits on its
overwrite prompt). Evidence `E:\xhci98-vm\p48\chain2\<vm>\`
(debugcon, serial, `qemu-*.out`, screenshots, `pids.txt`); the
configuration-write trace `-d trace:pci_cfg_write` (`w98c\qemu-d.out`),
`info pci` polled every 150 ms (`w98c\poll-*.txt`,
`E:\xhci98-scratch\w48\chain2\pcipoll.py`), chain's scratch
`DNSTAT.EXE` for `CM_Get_DevNode_Status`.

| Leg | System, build | Read | Verdict |
|---|---|---|---|
| c, d (`w98c`) | 98 SE, a fresh overlay, `5a2080d2` (qemu `xhcicm.sys` `aa2f4e2c...`) | First chained ASM2464: `cm.chain.9x.number=00020801`, `cm.chain.9x.irq=01030001` (no line set, three on another pin), `cm.chain.9x.reenumerate=00020800`, the bridges excluded by class (`noirq.refused=04000203`, `05000203`), the bridge wizard (tag c: its restart taken; tag d: "To finish setting up your new hardware, you must restart your computer" answered No), then Stor98's wizard for the NVMe minutes later; `DNSTAT`: the NVMe `st=08000620 pr=29`, `info pci`: no BAR; no `noirq` line for 06:00.0. The trace: no write to 3Ch of 04:00.0, 05:00.0 or 06:00.0 by anyone but `PCI.VXD`'s FFh then 0 to 05:00.0. Unplugged (`cm.chain.9x.remove`, query-remove and remove CR_SUCCESS) and plugged again: `noirq.after=06000300` then `06000500` (started), `DNSTAT` all `pr=0`; the stor98 key: "StorInterrupt 6.0.0 = pin 1 line 0 msi no msix yes port 0/0 polling (interrupt line not set)", "StorInterrupt 3.0.0 = pin 1 line 10 msi no msix yes port 10/0 interrupt" | item 1 reproduced and its cause read; item 3 read (polling by design) |
| P0 (`w98c` tag d) | 98 SE, `5a2080d2` | The dock removed by QMP 2.0 s after the chained plug, while the chain request was posted: `cm.chain.9x.walk=00280000` (incomplete, no candidate) fifty times over 10 s, `u4.kid.dropped`, `u4.linkdown`, the vanish sweep, the member's removal and re-enumeration, `cm.chain.9x.owed-expired=00002801`, `cm.chain.9x.fallback=00000028`; no `cm.chain.9x.number`, no chain-port re-enumeration; the desktop alive. (A first attempt at 2.2 s came after the chain operation had finished: number, irq, reenumerate, then the loss's removal.) The exact interleaving of the merge review (a loss between the gate read and the generation) is not reachable by timing in the emulator; the host vector holds it | pass (no hang, no chain-port re-enumeration after the loss) |
| me1 | ME, a fresh overlay, `8edfd362` (qemu `xhcicm.sys` `c5a41881...`) | First chained plug: the window opened, the bridges excluded, no `cm.reenum.9x.noirq.hold`; `DNSTAT` during the wizard: both new bridges `st=18086ECE pr=1`; their `Enum` keys (regedit export): `HardwareID`, `CompatibleIDs`, `DeviceDesc`, `HWRevision`, no `Driver`, against the dock's bridge's `"Driver"="System\\0057"` | the fix as first coded: fail (the hold never fired); `59a28f59` |
| me2 | ME, a fresh overlay, `59a28f59` (qemu `xhcicm.sys` `30d480d2...`, `xhci98.sys` `632983de...`; release `xhcicm.sys` `2782f360...`) | First chained plug: `cm.chain.9x.number=00020801`, `reenumerate=00020800`, `cm.reenum.9x.noirq.hold=000003FF` (depth 3, no `Driver` value), the bridge wizards answered; then with no second plug `noirq.after=06000300`, `cm.reenum.9x.noirq=0B000000`, `0D000000`, `noirq.after=06000500`: started. `DNSTAT`: both NVMe `pr=0`; "StorInterrupt 6.0.0 = pin 1 line 0 ... polling (interrupt line not set)"; F: ("ORICO"): `copy` of `WIN.INI` then `fc /b`: "no differences encountered". `XHCIINST /console /devices`: "marked removable" on both units; `XHCITR /report`: "Marked removable." on every unit line. Unplugged: `cm.chain.9x.remove=00000208`, query-remove CR_SUCCESS, `Remove_SubTree` CR_NO_SUCH_DEVNODE (already gone), `reenumerate`; plugged again: numbered, started (`noirq.after=06000500`), no hold (nothing being installed) | item 1 on ME: pass; item 2 on ME: pass |
| w98d tag e | 98 SE, `59a28f59` | First chained plug: `cm.reenum.9x.noirq.hold=00000301` (depth 3, problem 1); the bridge wizard's restart prompt was meant to be answered No, but the restart was taken (a key sent to the wrong window), so the start without a second plug was not read on 98 SE | the hold on 98 SE: fires; the start: owed |
| w98e | 98 SE, a fresh overlay, release `59a28f59` | Install, restart, the wizards with Enter (a restart accepted), then the cold start: "A fatal exception 0E has occurred at 0197:BFF86EBD", a black screen after a key, and safe mode at the next start - the same address task 48.31's leg n met once (after the first Stor98 install's restart). Seen now on a second fresh overlay with the release drivers alone (the qemu copy had not taken). Not chased | not this task's; a reading for the owner |
| XP | XP SP3, a fresh overlay | Abandoned: the Found New Hardware wizards' keystroke driving went astray (Internet Explorer and Outlook Express opened); `92f3d039` changes only the 9x branches, the NT lines are as they were (host vectors) | owed |

### The readings owed, on 3.4.3.0 at `8a20f134` (2026-11-09, later)

Branch `w48-chain2` fast-forwarded to `8a20f134` (3.4.3.0 with this
work merged); x86 `all` gated: "BUILD + GATES PASSED (release qemu)"
(`gb-x86d.log`); packages of it (release `xhcicm.sys` `04a37a42...`,
qemu `xhcicm.sys` `d8136c90...`, `xhci98.sys` `13abe75f...`), XHCIINST
`e1e8be97...` and XHCITR `f5af1b43...` by their own `build.cmd`
(`tools4.log`); kit `E:\xhci98-vm\p48\chain2\kit4\`.

| Leg | System | Read | Verdict |
|---|---|---|---|
| `w98f` tags a to c | 98 SE, a fresh overlay | The release package installed (the installer ran twice, a script's keystrokes repeated; both exit 3), the qemu drivers copied with `copy /y` and read back ("2 file(s) copied"), Restart; tag b: the dock's bridge wizards and Stor98's answered, each "To finish setting up your new hardware, you must restart your computer" answered No (Tab, then Enter), then Restart; tag c: a clean desktop (no 0E this time). The first chained ASM2464 plugged: `cm.chain.9x.number=00020801`, `cm.chain.9x.reenumerate=00020800`, `cm.reenum.9x.noirq.hold=00000301` (depth 3, problem 1); two bridge wizards, each restart prompt answered No; Stor98's wizard; then, with no second plug, `noirq.after=06000300`, `cm.reenum.9x.noirq=0B000000`, `0D000000`, `noirq.after=06000500`: started, and Windows opened F: ("Orico"). `DNSTAT`: both NVMe `st=18086ACE pr=0`, their disks `pr=0`; "StorInterrupt 6.0.0 = pin 1 line 0 msi no msix yes port 0/0 polling (interrupt line not set)", "StorInterrupt 3.0.0 = pin 1 line 10 ... port 10/0 interrupt"; `copy` of `WIN.INI` to F: then `fc /b`: "FC: no differences encountered"; `XHCIINST /console /devices`: "marked removable" on both units | item 1 on 98 SE: pass (the first plug starts the chained drive with no second plug) |
| `xp2` | XP SP3, a fresh overlay | `INSTALL.BAT /console /yes /nopause /thunderbolt /stor98` (the unsigned-driver prompts answered Continue Anyway, the Found New Hardware wizards "No, not this time" and automatic, the restart prompt No): exit 3. `XHCIINST /console /devices`: the units read "removable: yes; surprise removal OK: no" and "listed in Safely Remove Hardware", the NT lines as before; no 9x line (screenshot `x8.png`) | item 2 on XP: pass (unchanged) |

### Left

- 98 SE: "A fatal exception 0E has occurred at 0197:BFF86EBD" at the
  first cold start after the install's restart and the wizards' restart,
  then safe mode, now on two fresh overlays (task 48.31's leg n and
  `w98e`), with the release drivers. Not chased (with the boot0e task);
  not met on `w98f`, where every restart prompt was answered No and one
  Restart made from the Start menu after the wizards.
- The merge review's exact interleaving (a loss between the gate read and
  the generation) in a guest: not reachable by timing; the host vector
  holds it.

### Merge (w48-chain2)

Merged into `3.4.3.0` at `8a20f134` (2026-11-09; the race fix earlier at
`9c5ea69e`); the merge reviewed, nothing material; the integration build
passed. The 98 SE first plug and the XP wording read on `8a20f134`.

## 48.16 / 48.18: the owner's bench, 2026-11-09 (P14s 98 SE to Framework 13 Windows 11)

Bench (owner, `3.4.2.0`): Alpine Ridge firmware CM (8086:15BF) on 98 SE,
cabled to a Framework 13 (12th gen) on Windows 11 with its inbox USB4 P2P
adapter. Windows showed the adapter at 20 Gb/s; no bytes either way; ping
failed both ways. Evidence: the owner's quick save of 09:38:11 (held on
the maintainer's disk).

Reading of the xhcicm log (worker net): the property exchange completed
both ways. Windows 11's block: `vendorid` 045Eh, `deviceid` 083Fh, with
USB4NET. This host's Logins went unanswered for the minute
(`cm.net.login.none=01000044`) and every 5 s after. 144 inter-domain
packets reached the engine and were dropped with no trace; neither the
engine's counters nor the firmware CM's were written anywhere.

Change (`a168a650`, branch w48-net; record 35 section 3.12, revision 18):
- the first 16 drops of each peer record logged with their reason, PDF,
  kind and type;
- the four UUIDs logged at the first UUID mismatch;
- Error Responses counted, the first 16 logged;
- the engine's and the firmware CM's inter-domain counters logged at the
  Login's minute and at each record's end.
Logging only. Reviewed and revised (two rounds).

Tests and build:
- Host suites: `test_tbt_xdp` 200 checks, 0 failures (new
  `test_drop_log`); `test_tbt_icm` 1051/0; `test_u4xd` 251/0.
- Gated x86 release build on development host A: BUILD + GATES PASSED
  (log `E:\xhci98-scratch\w48\net\gated-droplog-x86.log`).

Bench kit `E:\xhci98-bench\p48-tbnet\`:
- `98SE\XHCICM.SYS`: 458,323 bytes, SHA-256
  7ee53a900ad45f3f733ae6a47221956b9fd82ce00596a4302ed9ecae0954b7a0.
- `INSTDIAG.BAT`, `SAVE.BAT`, `RESTORE.BAT`.
- `WIN11\COLLECT.CMD` and `COLLECT.PS1`.
- `BENCH-STEPS.txt`.
The scripts were reviewed and revised. Bench reading owed (the owner).

Correction (coordinator, 2026-11-09): the first bench kit put the driver in
C:\WINDOWS\SYSTEM. On 98 SE `xhcicm.inf` copies `xhcicm.sys` to
C:\WINDOWS\SYSTEM32\DRIVERS (`Cm.CopyFiles`, 10,System32\Drivers), so
INSTDIAG answered "not there". The coordinator corrected INSTDIAG.BAT,
RESTORE.BAT and BENCH-STEPS.txt to SYSTEM32\DRIVERS, and added next-step
prompts to SAVE.BAT and INSTDIAG.BAT.

### The bench reading with the diagnostic driver, and the fix (2026-11-09)

The owner ran the three plug orders with a168a650's image. Evidence:
`temp\p48-tbnet\S1` to `S3` and three `W11-...` folders, held on the
maintainer's disk.

- **Plug order 1** (98 SE up first, then the cable): fail.
  - Every one of this host's 68 Logins was answered by a Login Response
    and dropped (`cm.xdp.drop=01077301`, reason 7).
  - Their Request IDs ran 8, 9, ... 0x17 (`cm.xdp.drop.id`), while this
    host's count from 1.
  - Windows had sent 8 Logins of its own, all answered with success
    (`cm.net.n.logins=01044008`).
  - No UUID mismatch, no Error Response, nothing malformed.
  - Windows: "Destination host unreachable"; 0 bytes received.
- **Plug order 2** (98 SE started with the cable in): pass.
  - `cm.net.link.up=01070000`, `cm.net.up=0100080F`.
  - Ping from Windows 4/4; Windows' adapter received 5 unicast packets.
- **Plug order 3** (the cable pulled and put back at the Framework):
  fail.
  - The link went down and the record ended.
  - No XDomain connected followed. Eleven requests (PDF 6, route 1)
    reached no record (`cm.icm.xd.nopeer.route=06000001`).
  - Windows' adapter was gone ("transmit failed. General failure").

Reading:
- (1) Windows 11 answers a Login with a Request ID from its own counter,
  not the Login's. IDS Table 5-3 and Linux's `tbnet_login_response` copy
  it. Linux checks no response's id. Plug order 2 worked only because the
  counter fell inside this host's range.
- (3) The Alpine Ridge firmware announced the far host only after its own
  UUID Request was answered, and did not ask again after the replug.

Fix (`27b06dbd`, record 35 revision 20; `20755bf6` the delete claim's
import):
- A Login Response is matched by type, route, protocol UUID and UUID pair
  while a Login is out, whatever its Request ID; a foreign ID is counted
  and logged once a login.
- A request from a direct link with no record asks for another driver
  ready: once each 10 s at most, six times per route until that route is
  announced.
- Reviewed and revised (three rounds).
- Host suites: `test_tbt_xdp` 204/0 (`test_login_resp_other_id`),
  `test_tbt_icm` 1061/0 (`test_xd_nopeer_ask`), `test_u4xd` 251/0.
- Gated x86 release build of `20755bf6`: BUILD + GATES PASSED (log
  `E:\xhci98-scratch\w48\net\gated-bench3-x86.log`).
  - The first attempt failed the import gate on `InterlockedExchange`,
    from 48.18's delete claim. It is now `InterlockedIncrement`.

New bench image in `E:\xhci98-bench\p48-tbnet\98SE\XHCICM.SYS`: 462,803
bytes, SHA-256
4b181435156ca587b6f2bd9aad599a6fe68d82989e6e3fd093e9fc49dbbc79df. It
also carries 48.18's query and 48.16's PDO changes. Its reading is owed
(the owner).

Emulator: not reproduced. usb4-emu's far ends are real guests (Linux,
xHCI98, Windows 11 in a VM). Windows 11 in the emulator (47.44) answered
in range. Making a model peer answer with a foreign Request ID would be a
usb4-emu change of its own, not made. The host vector stands in for it.

### Bench round 2 (2026-11-09, image 4b181435 = 20755bf6) and round 3's image

Round 2 (the owner; `temp\p48-tbnet\round2\`):
- **Order 1:** PASS. `cm.net.login.id=01000008` logged once,
  `cm.net.up=0100080F`; ping from Windows 4/4, about 15 ms.
- **Order 2:** PASS.
- **Order 3:** FAIL.
  - At the pull: `cm.xdp.disconnected`, `cm.net.down`, the record's
    end.
  - Then 11 requests with no record (`cm.icm.xd.nopeer.route=06000001`)
    and two driver readies asked (`cm.icm.xd.nopeer.ask`).
  - No event of any kind from the firmware after that.
  - Windows' adapter absent.

Change `38144519` (record 35 revision 21, section 3.14): a UUID Request
with no record is answered with this domain's UUID, from the last
connected event's `local_uuid`; such packets log their kind, type and SN.
- Reviewed and revised.
- `test_tbt_icm` 1067/0 (`test_xd_nopeer_uuid`), `test_u4xd` 251/0.
- Gated x86 release build: BUILD + GATES PASSED (log
  `E:\xhci98-scratch\w48\net\gated-bench4-x86.log`).

Round 3's image: `98SE\XHCICM.SYS` 463,123 bytes, SHA-256
c0d318343c668a5f790fa6bdc7df44a364faf417986c6edc6d886e79174c6a5f. Its
reading is owed (the owner).

### Bench round 3 (2026-11-09, image `c0d31834` = `38144519`) and the fix

Round 3 (the owner; `temp\p48-tbnet\round3\`, `S1` to `S3` and three
`W11-...` collections):
- **Order 1:** PASS. **Order 2:** PASS (ping answers).
- **Order 3:** FAIL. Windows' adapter gone (`adapter-none.txt`).

The S3 log read:
- the pull: `cm.xdp.disconnected`, `cm.net.down=00000103`,
  `cm.net.link.down`, the record's end (`cm.xdp.end=00000103`);
- 11 packets for no record (`cm.icm.xd.nopeer.route=06000001`), about 1 s
  apart over 10.5 s from 15 s after the pull, every one a **Properties
  Read Request**: `cm.icm.xd.nopeer.type` 00020300, 00020301, 00020302,
  00020303 in turn (kind 2 discovery, type 3, SN 0 to 3 with IDS 3.1's
  resends);
- **no UUID Request at all** (`cm.icm.xd.nopeer.uuids=00000000`), so
  revision 21's answer never ran;
- the two driver readies of revision 20 (`cm.icm.xd.nopeer.ask`) each
  answered with the firmware's ready flags (`tbt.icm.ready-flags=000000E2`,
  `tbt.icm.boot-acl=00000110`) and **no event of any kind**;
- `cm.icm.rediscoveries` 2, `cm.icm.xd.packets` 0Ah before the pull.

Reading: Windows 11 keeps the far domain's UUID across a replug and reads
this domain's properties straight away, so it never answers a UUID
Request of the firmware's, and the Alpine Ridge firmware - which
announced the peer at the first connection - does not announce it again.
Nothing public names an ICM command that makes it announce one.

Fix (`ba9bac2d`, record 35 revision 22, section 3.15): the place of each
direct link's last connected event is kept (link, depth, route, both
UUIDs), and a Properties Read Request at a kept place, for this domain as
that event named it, from the computer announced there, is adopted into a
record of its own and answered; the login and the approve follow at the
kept place. Adoption has its own path - no device replaced, no other
record ended, no owed teardown settled, the fence untouched - and is
bounded: at most three a place until it is announced again; the record
swept when its discovery fails (`cm.xdp.adopt.end`); an approve refused
latched (`cm.xdp.adopt.refused`); a Changed Notification of another
domain ending it with its Logout and teardown (`cm.xdp.adopt.other`); a
real connected event making it an ordinary record. Logged
`cm.xdp.adopt`, counted `cm.icm.xd.adopted`.
- Reviewed and revised (a design round and three code rounds; the last
  returned nothing material).
- Host suites: `test_tbt_icm` 1101 checks, 0 failures (new
  `test_xd_adopt_props`, which replays round 3); `test_tbt_xdp` 204/0;
  `test_u4xd` 251/0; `test_cm_qnet` 60/0; `scripts\check-text.ps1` OK.

## 48.18 Thunderbolt networking in the xHCI98 USB Tree (2026-11-09, w48-net)

Design: record 35 revision 19 section 17 (`6421800b`), converged over four
rounds before the code. Code `8db64c87`: `xhcicm.sys` answers `CM_QN_IOCTL`
on its query device with the adapter's state, MAC, link, flow control and
counters, one entry a link with another computer (state, the peer's UUID,
ids and texts, speed and lanes, inter-domain counters) and the network
driver's counters, gathered on the connection-manager thread after each
pass and laid out by the pure `cm_qnet.c`; `xhcinet.sys` and
`xhcinet6.sys` give their counters through a new `Stats` callback, taken
only from a callbacks `Size` that covers it, `Open` still accepting the
three-callback size of earlier drivers; XHCITR shows a "Thunderbolt
networking" node per NHI and a node per link, the adapter's IP address
from `GetAdaptersInfo` loaded at run time, the far side's kind, the same
in the saved report, and in the snapshot's device report with the MACs,
UUID, IP and peer texts withheld. The far side's kind names Windows from
vendor `045Eh` (the owner's bench, record 35 section 3.12).

- Host suites: `test_cm_qnet` 60 checks, 0 failures; `test_trnet` 67/0;
  after the merge of `3.4.3.0` every XHCITR suite again: `test_tr`
  1727/0, `test_trset` 1854/0, `test_trred` 161/0, `test_trlog` 349/0,
  `test_trnet` 67/0, `test_trpci` 271/0, `test_trdiag` 185/0,
  `test_tracpi` 10594/0, `test_trpop` 207/0.
- Builds on development host A: gated x86 release of `20755bf6`
  (`gated-bench3-x86.log`) and of `38144519` (`gated-bench4-x86.log`);
  gated **amd64 every flavour** of `38144519` BUILD + GATES PASSED
  (`gated-amd64-all.log`); XHCITR x86 and x64 `/WX` clean after the
  merge. Reviewed and revised, nothing material at the
  last.
- **The VM legs are owed.** The kit and the scripts are ready
  (`E:\xhci98-scratch\w48\net\kit\`: `prep9x.sh`, `prepnt.sh`,
  `cell48.sh`, `leg48.sh`, `guest\*.BAT` - which now take XHCITR's
  report and its snapshot; the emulator copy in
  `E:\xhci98-vm\p48\net\bin`), and the amd64 package is staged at
  `E:\xhci98-vm\p48\net\kit64`. Two findings from the attempt:
  - **The x64 guest legs need the in-guest certificate.** This clone has
    no signing key, so the staged x64 package carries no `xhcicm.cat` and
    `xhciinst` refuses it ("Cannot continue: this 64-bit package carries
    the Thunderbolt driver without its catalog"). XP x64 and the other
    x64 legs need the throwaway-certificate route inside the guest.
  - **Ports.** TCP 49680 to 49699 are inside a Windows reserved exclusion
    range on this host (49672 to 49771, `netsh int ipv4 show
    excludedportrange`), so QEMU cannot bind QMP there ("An attempt was
    made to access a socket in a way forbidden by its access
    permissions"). Only 47940 to 47959 of this worker's two ranges are
    usable.

## 48.16 the connection manager and networking in the emulator (2026-11-09, w48-net)

The PnP question of the task (whether the network PDO's removal can
overlap a later parent START or BusRelations) was answered by making the
code correct in either order rather than resting on the serialisation
nothing in the DDK promises: design record 35 revision 19 section 6.6
(`6421800b`), code in `8db64c87` - `CM_NET.PdoLock` guarding `Pdo`,
`Report`, a new `Retired` and each PDO's `Reported` and `Retired`; a PDO
retired at the BusRelations answer that leaves it out, once that answer
has succeeded (the answer forwarded and waited for); no fresh PDO while a
retired one awaits its removal; one deleter by an interlocked claim, each
holding a reference; the NHI's removal waiting for a retired PDO's
removal. `20755bf6` made that claim `InterlockedIncrement` (the import
gate allows no `InterlockedExchange`).

- Host suites and builds: with 48.18's above.
- **The emulator legs are owed**: a peer on the cable, the Thunderbolt 3
  routers (`-Usb4 tb3ar`, `tb3tr`), a resume, the amd64 images in a guest
  (the certificate above), and the overlap itself (`XhciCmNet` off and on
  in quick succession with a peer on the cable, on 98 SE and on XP, the
  adapter leaving and coming back, `cm.net.pdo` serials each new).
- Gated x86 release build of `ba9bac2d`: BUILD + GATES PASSED (log
  `E:\xhci98-scratch\w48\net\gated-bench5-x86.log`).
- Round 4's bench image in `E:\xhci98-bench\p48-tbnet\98SE\XHCICM.SYS`:
  464,803 bytes, SHA-256
  `b0b2003757d52d0871d7baeab5a71472f72e33db5260f2dbba721dab3bf68d32`
  (`BENCH-STEPS.txt` and `INSTDIAG.BAT` refreshed). Its reading is owed
  (the owner).
- Gated x86 **every flavour** of `ba9bac2d`: BUILD + GATES PASSED (records
  `out\gated-x86-release.txt` and `out\gated-x86-qemu.txt`, commit
  `ba9bac2d`, `dirty=0`). The leg kit is the qemu flavour of that build
  (`E:\xhci98-vm\p48\net\kit86`); its `98-me\cm\xhcicm.sys` SHA-256
  `08F88D20...A4D1` equals the record's, so the kit is the gated build.
- The x64 leg kit (`E:\xhci98-vm\p48\net\kit64`, the gated amd64 release
  build) carries the five catalogs made host-side with the packager's own
  `New-PackageCatalogFile`, unsigned, with `MKCERT\` (WDK 7.1 MakeCert,
  SignTool, CertMgr) and `SIGN.BAT`, which makes a throwaway certificate
  inside the guest ("CN=xHCI98 leg throwaway", 01/01/2026 to 01/01/2030),
  trusts it as a root and as a publisher there, signs each `.sys` and then
  each `.cat` (a catalog hashes the signed file) and verifies them. No
  project or owner key is used.

### The guest legs attempted (2026-11-09, worker net2)

Both 48.18 and 48.16's legs are still owed; what the attempt established:

- **The leg kit must be the release flavour.** This tree's installer
  wants `RELEASE` (`xhciinst\inst_eng.c`, `wantFlavour`), so a qemu-flavour
  kit is refused on sight: "xhci98.sys has 1 build marker(s), the first
  QEMU; this folder is RELEASE". The release build logs as fully (the
  owner's bench reads its CM log), so the legs use the gated release kit.
- **XP (and every NT guest) needs the unsigned-driver policy set first.**
  With the stock policy the install reaches the devices and then stops:
  "xhci98.inf could not be staged (error 0xE000022F)", exit 2.
  `build-and-test.md` says the value a script writes is not honoured and
  the policy has to be set to Ignore through System Properties' Driver
  Signing dialog (`PrepSteps`, `xp-signing-ignore`); the other route is
  catalogs made host-side and signed in the guest, as the x64 kit now
  does.
- **98 SE:** the guest ran the kit's `I98.BAT` (the DOS box shows the
  three lines and "Finished"), but `INSTALL` printed nothing and nothing
  reached COM1, so the install is unproven. The guest's own
  `C:\XHCIINST.LOG` and a hand-typed `INSTALL /console ...` settle it.

### Bench round 4 (2026-11-09, image `b0b20037` = `ba9bac2d`) (worker net3)

Round 4 (the owner; `temp\p48-tbnet\round4\`, `S1` to `S3` and three
`W11-...` collections):
- **Order 1:** PASS (Windows pings 169.254.103.76, 4 of 4).
- **Order 2:** PASS.
- **Order 3:** the peer comes back, but no data passes.
  - 98 SE: the pull is logged (`cm.xdp.disconnected`, `cm.net.down=00000103`, `cm.xdp.end=00000103`).
  - Then a Properties Read Request for no record (`cm.icm.xd.nopeer.type=00020302`) is **adopted** (`cm.xdp.adopt=01010001`).
  - The block is read (`cm.xdp.ready=015E083F`, `cm.xdp.peer=0145E83F`), and this host's Login is answered (`cm.net.login.id=01000008`).
  - Windows' Login is answered (`cm.net.up` needs it). The firmware answers the approve (`cm.net.paths=01080F01`: TX HopID 8, RX HopID 0Fh, ring 1, as in order 1), then `cm.net.link.up=01070000` and `cm.net.up=0100080F`.
  - `cm.icm.commands` 3 against `cm.icm.responses` 3. There is no approve failure, unknown outcome or refusal.
  - Windows: the USB4NET adapter is re-created (new PDO, arrival 16:42:29), status OK, Up, 20 Gbps, Connected. Its counters restart with the fresh-adapter burst (17 broadcasts, 68 multicasts).
  - The ping sends 12 more broadcasts and gets "Destination host unreachable" from its own 169.254.143.30. The adapter receives nothing (ReceivedBytes 0), and no neighbor entry appears for 98 SE.

Reading: since round 3, order 3 has turned from a lost peer into a dead
data path. Discovery, both logins and the firmware's approve complete,
and they log the same as order 1, but no frame reaches Windows. The
direction cannot be told from this log:
- the network ring's counters are xhcicm's query (48.18);
- the owner's report was made with XHCITR 3.4.2.0, which does not show them.

Leading hypothesis (unproven): the Alpine Ridge firmware accepts the
approve for a link it never announced again after the replug, but builds
no working paths there.

Fifth reading staged (the coordinator, 2026-11-09). The driver is
unchanged. New in the bench kit:
- `98SE\XHCITR.EXE`: this branch's gated x86 build, 585,728 bytes, SHA-256
  `12881ed9258b79f1ea1a8dd49a997789eca828f04ee6a3e40e649d3bd2323af7`;
- `SAVE.BAT`, which uses that XHCITR when it is beside it;
- the `SHA256.TXT` line;
- `BENCH-STEPS.txt` steps F1 to F6: order 3 only, then two saves 30 s
  apart after the Framework's ping.
Its reading is owed (the owner).

Gated amd64 **every flavour** of `ba9bac2d`: BUILD + GATES PASSED (log
`E:\xhci98-scratch\w48\net\gated-amd64-all-ba9.log`; records
`out\gated-amd64-release.txt` and `-qemu.txt`, commit `ba9bac2d`,
`dirty=0`).

### Bench round 5 (2026-11-09, the Fifth reading: `ba9bac2d` with this branch's XHCITR) (worker net3)

The owner, order 3 only (`temp\p48-tbnet\round5\`: `S1`, `S2`, the quick
save `2026-11-09_171033` and `W11-20261109-170755`). Windows' ping got
"Destination host unreachable" again. The new "Thunderbolt networking"
node reads the same in all three saves over about 70 s:
- State up; USB4 frames in 85, 0 errors, 0 dropped; frames out 22, 0 refused;
- link changes 2 up, 1 down;
- the link logged in, inter-domain 24 sent and 13 received, logins 1 sent and 8 received, services 1 up.

The 85 frames in are exactly Windows' fresh-adapter burst of the order-2
session before the pull (17 broadcasts and 68 multicasts in round 4).
After the replug, Windows sent 84 frames, then 12 ARP broadcasts for the
ping, and none of them arrived. Windows received nothing either. The log
repeats round 4's adoption line for line, and no ring note fired.

Reading: after the adoption no frame crosses this host's router in either
direction, though every control step succeeds.

### The emulator replug: the ring restarts, so order 3 is the firmware's (2026-11-09) (worker net3)

Cell C2 on development host A:
- two XP SP3 guests, `-Usb4 tb3ar` (the Alpine Ridge firmware CM model);
- `usb4-xdomain` between them over a socket;
- the gated release kit of `ba9bac2d` (`E:\xhci98-vm\p48\net\kit86`), each guest prepared with `prepnt.sh` (the unsigned-driver policy set to Ignore through System Properties' dialog first).

Before the replug: ping 6 of 6 each way. XHCITR's node read State up,
`xhcinet.sys` MTU 1500, frames in 81 and out 36, link changes 1 up and 0
down, the link logged in (`Kind: xHCI98`).

The replug: the cable pulled at B (`qom-set` `xd0` `plugged` false), out
for 15 s, then back in. The emulated firmware announced the peer again,
and both ends logged in again. Afterwards:
- each node reads "Link changes 2 up, 1 down" and Services 1 up;
- ping from A to B is 6 of 6 (10 to 16 ms);
- the xdomain device's own counters at A: frames out 82 to 108, in 36 to 98;
- B's node: frames in 108, frames out 100.

So ring 1 went down and came up again in the same boot and carried
frames. `cm_net.c` is shared by 98 SE and NT. Order 3's dead data path is
therefore the firmware's: it was never re-announced. Evidence:
`E:\xhci98-vm\p48\net\cells\C2\` (`leg.txt`, `replug.txt`, serial logs).

### 98 SE: a fault on a first boot after the install (2026-11-09; open) (worker net3)

Several boots of one 98 SE image after this branch's install (`tb3ar`, the gated release kit):
- **Faulted twice.** "A fatal exception 0E has occurred at 018F:BFF8AB97" (the first cold start after the install, no peer) and "018F:BFF8713B" (the first normal boot after a Safe-mode boot, with an XP peer on the cable). Both came about 150 s in, and a black text-mode screen followed.
- **Clean four times.** Three more first normal boots without a peer, each reaching "New Hardware Found: xHCI98 USB 3 Root Hub" and the restart question, and one later boot.

Both faults are a ring-3 program inside KERNEL32, at a boot where 98 SE
installs the new devices while the Run programs start. The program is
not named yet. Worker chain saw the same once, on a tree without 48.18.

Legs on 98 SE use `p98b`, the image taken past those boots.

### 48.18 and 48.16: 98 SE with XP x64 over usb4-xdomain (2026-11-09) (worker net3)

The cell was C3 on development host A:
- A: 98 SE (`p98b`), on the gated x86 release kit of `ba9bac2d`;
- B: XP x64 SP2 (`pxp64b`), on the amd64 release package of `ba9bac2d` (`E:\xhci98-vm\p48\net\kit64b`, made with `make-package.ps1` and `ConvertTo-FamilyLayout`);
- both `-Usb4 tb3ar`, joined by `usb4-xdomain`.

The XP x64 install needed no signing in the guest. The five catalogs were
made host-side, unsigned, with the packager's own
`New-PackageCatalogFile` (`cats.ps1`), and the unsigned-driver policy was
set to Ignore through System Properties' dialog. `nt-x64` carries
`xhcicmf.inx`, which the installer makes into the INF, so no
`xhcicmf.cat` is needed. Results: `xhcicm` started, `xhcinet` started
("xHCI98 Thunderbolt Networking", the amd64 image in a guest), exit 3.

Results:
- **Ping:** 6 of 6 both ways, 7 to 44 ms (A to B and B to A).
- **XHCITR's report on 98 SE** (`XHCITR.EXE`) and on XP x64 (`XHCITR64.EXE`): a "Thunderbolt networking - up" node.
  - It shows State up; `Network driver: xhcinet.sys, MTU 1500`; the adapter's MAC and its APIPA address with mask; frames in and out with errors, drops and refusals.
  - A "Link on port 1 - connected: a computer with xHCI98" node shows logged in, `Kind: xHCI98`, the peer's UUID, and logins 1 sent and 1 received.
- **The snapshot** (`/snapshot /o C:\TS`) on both: the same section, with the MAC, UUID, IP and the peer's texts written "(not included in the snapshot)".
- **Evidence:** `E:\xhci98-vm\p48\net\cells\C3\` (`leg.txt`, `A\serial-A.log`, `B\serial-B.log`).

98 SE on the `p98b` image booted without the fault above.
- Correction to the above: the guest BATs' `XHCITR /snapshot /o C:\TS` is refused by this XHCITR ("it names a drive"), so no stand-alone snapshot files came from C3. The snapshot text in the evidence is the one the report embeds (the device report, with the MAC, UUID, IP and texts withheld). The stand-alone snapshot with the networking section is on real 98 SE in the owner's round-5 quick save (`B091710A.DEV`, "Thunderbolt networking (xhcicm.sys)"). The BATs now run `XHCITR /snapshot TS` from `C:\`.

### 48.16: the network PDO across back-to-back NHI restarts with a peer, XP (2026-11-09) (worker net3)

Cell C4: XP SP3 (`pxp`, gated release kit plus the WDK's `devcon.exe`), cabled over `usb4-xdomain` to the running 98 SE of C3.
- **Before:** ping 6 of 6, `cm.net.pdo=00000001`.
- **The restarts:** the NHI restarted twice back to back (`devcon restart PCI\VEN_8086&DEV_15BF*`, "Restarted" twice). The `XhciCmNet` writes in between missed their key (the value is in the device's hardware key, not under Control\Class), so `XhciCmNet` stayed 1. This leg exercises the PDO's removal against the next start, not the value's toggle.
- **After:** ping 6 of 6 again, the adapter back, `cm.net.pdo=00000003` (a new serial for each start), no bugcheck.
- **98 SE's side:** its peer was swapped from XP x64 to XP (a link down and up on the firmware CM, the ring restarted in one boot on 98 SE), and it answered both.
- **Owed:** the value toggle itself on XP and on 98 SE (a Device Manager disable and enable on 98 SE), and Driver Verifier.

### The owner's Linux run on the P14s, and revision 23 (2026-11-09) (worker net4)

The owner ran Linux on the P14s (the same Alpine Ridge firmware CM) against
the Framework on Windows 11, with `thunderbolt` dynamic debug
(`temp\p48-tbnet\linux\tbnet-20261109-200230\`):
- **Ping 4 of 4** after a fresh plug, after a re-plug at the Framework and
  after a re-plug at the P14s end.
- `thunderbolt0` was made again each time (ifindex 4, 5, 6), and its
  receive count started again at Windows' fresh burst of 85. So Linux got
  a fresh XDomain peer, an announcement from the firmware, after each
  re-plug.
- **Step 1's dmesg** (1227 lines) shows logins, "login successful,
  enabling paths", rings 1 with E2E. It begins with ring 0 stopped
  ("control channel stopped", 20:02:54) and started again (20:03:00)
  just before the firmware's announcement (local and remote UUID):
  the NHI's runtime suspend and resume around the plug.
- Steps 2 and 3's dmesg are empty (the script cleared the log after
  the re-plug).

What Linux does at a disconnect that this driver did not (Linux
`c6859eed`, facts only; `drivers/net/thunderbolt/main.c` fetched by
`external/README.md`'s recipe, SHA-256 matching):
- After the disconnected event the XDomain's services are unregistered
  (`icm_handle_notification`, `tb_domain_unregister_unplugged_xdomains`).
- `thunderbolt-net` stops (`tbnet_remove`, `tbnet_stop`) and calls
  `tbnet_tear_down`. Where both logins were done, that sends up to ten
  Logouts and then disables the paths: `icm_fr_disconnect_xdomain_paths`,
  mailbox `DISCONNECT_PA` data 1 then data 2.
- This driver sent neither: an end the firmware announced was taken as
  the paths gone.
- The second difference: Linux runtime-suspends the NHI when nothing is
  connected (`icm_runtime_suspend`, `DRV_UNLOADS`) and resumes it with a
  driver ready. The run cannot tell which of the two brought the
  announcement back. A Linux run with the NHI held awake is staged by the
  coordinator.

**The change** (`c9b30c64`, record 35 revision 23):
- **Section 3.17.** The paths of an approved record are torn down at
  every end, the firmware's own included: a disconnect, another host or a
  device at its place, a disconnect during the approve's wait. Logged as
  `cm.net.teardown.gone`. Never owed, no Logout into the dead link. Every
  teardown is now sent outside the record's waits.
- **Section 3.16.** An adopted record (section 3.15, no announcement)
  stops short: its block offers no USB4NET, and it sends no Login, approve
  or teardown and raises no rings or link (`cm.xdp.adopt.unannounced`). A
  real connected event promotes it. XHCITR shows it as "found, not
  connected" with the reason (`CM_QN_LF_UNANNOUNCED`).
- **Reviewed and revised.** Two rounds. The first found a disconnect
  cutting a Titan Ridge teardown between its stages; fixed by sending
  every teardown unstamped, with a vector.
- **Host suites:** test_tbt_icm 1127/0, test_trnet 74/0, and test_tbt_hw,
  test_tbt_select, test_u4cm, test_u4v2, test_u4xd and test_cm_qnet pass.

### Bench round 7 (2026-11-09, image `fe50c28e` = `c9b30c64`) (worker net4)

Plug order 3 at the Framework, a 10 s gap (`temp\p48-tbnet\round7\`):
- **After the pull:** `cm.xdp.disconnected`, then `cm.net.teardown.gone=00000001` and `cm.net.teardown=01000001` (mailbox 0x10, answered OK), the rings down and `cm.xdp.end`.
- **After the re-plug:** a fresh `cm.xdp.connected=01000001` straight after `cm.xdp.end`, with no driver ready, ring 0 restart or rediscovery between them. Then discovery, the logins (`cm.net.login.id=01000008`, Windows' own Request ID), `cm.net.paths=01080F01`, link up and `cm.net.up`.
- **The fallbacks:** `cm.icm.xd.nopeer` 0, `adopted` 0, `unannounced` 0, so neither ran.
- **Ping** from the Framework: 4 of 4 before and 4 of 4 after (12 to 704 ms; the first reply waits for the login and ARP).
- **Order 3 is fixed.** The teardown alone brings the announcement back.

TBSPEED, 30 s each way with the default socket buffers:
- **Framework to P14s:** 10.23 Mbit/s.
- **P14s to Framework:** 25.30 Mbit/s.
- **No errors** on either side.
- **Ping** under 98 SE is 7 to 19 ms, against 0.9 to 2.8 ms under Linux on the same pair.
- **The limit is ours:** the data rings are only polled, once per clock tick (`cm_net.c`), and the window is 98 SE's default. Task 48.38 (record 35 section 5.5, designed in `fdcdd459`, `5a9634d5` and `0f255e9d`) adds the data ring's interrupt.

The owner also found XHCITR's peer name cut at 32 characters ("Windows USB4(TM) Connection Mana"). It was cut on screen and in the saved report. Fixed in `38e0b49f` (64 characters, the query's link entry 292 bytes). Gated x86 release passed, but its record shows `0f255e9d dirty=1`, because docs commits landed during the build. The code is the same as `38e0b49f`; rebuild before any kit.

### 48.18 / 48.16 legs attempted (2026-11-09) (worker net4)
- **Windows 2000** (`prepnt.sh w2k p2k tb3ar`, the gated release kit of `ba9bac2d`): the install exited 4 both times ("Windows is still installing something... a Found New Hardware window"). `xpwiz.sh` did not clear the pending window. On the second pass the serial log could not be truncated, so the wait loop read the first pass's exit code. Stopped by PID; owed.
- **Vista x86** (`prepnt.sh vx86 pvx86`): the Run keystrokes went into the Start menu's search box (screenshot `E:\xhci98-vm\p48\net\pvx86\now1.png`, with a Found New Hardware window open). Stopped by PID; owed.

# Run 48 - worker net5 (Thunderbolt networking, continuing net4)

## net5 takeover (2026-11-09, worker net5; w48-net tip 0f255e9d)
- Tasks: P0 merge-review UAF fix (cm_net.c PDO removal); step 1 design 5.5
  (auto-clear / storm mask); step 2 code 48.38; step 3 owed 48.16/48.18
  legs; step 4 round-8 kit (prepare only).

### P0: the network PDO's removal holds the NHI (2026-11-09)
- Branch w48-net5 from 3.4.3.0 07ebe109, worktree E:\xhci98-wt\w48-net5
  (tools\ junctions made as w48-net's).
- COMMIT f36bf51d: IRP_MN_REMOVE_DEVICE takes Net.PdoCalls unconditionally
  before deciding under PdoLock; closes the PDO's gate (CM_NETR_GATE, pure,
  cm_netr.c) and its own call in one step; waits Users 0; invalidation if
  asked; then drops the hold. Kept PDO drops it at once. Record 35 6.6.
- test_cm_net 81/0 (gate interleavings incl. the paused call).
- Review --base 07ebe109: approve (first pass).

### Step 1: record 35 section 5.5 converged (2026-11-09)
- COMMIT a44373f5 (w48-net): status protocol = every ISR DW read, ISC
  written with AutoClearOff (each DW, data bit in its own DW), fast storm
  masks both causes; host vectors for ack with AutoClearOff + two-cause
  storm mask; no emulator leg for AutoClearOff (usb4-generic honours bit 17
  but resets it clear, no property; driver never sets it). 5.4 aligned.
- Review --base fdcdd459^: approve, first pass.
  DESIGN CONVERGED AT a44373f5.
- w48-net5 (3.4.3.0 + f36bf51d) merged into w48-net for step 2.

### P0 builds
- f36bf51d gated x86 release FAILED: `leave` is a DDK macro (__leave) -
  CmNetrGateClose's parameter. COMMIT 8ee40ce6 renames it (`own`).
- 8ee40ce6 gated x86 release PASSED (gated-x86-fix2.log; out\gated-x86-*.txt
  commit=8ee40ce6 dirty=0). amd64 running (gated-amd64-fix.log).
- Stray junction tools$d in w48-net5 (my mklink loop) removed.

### Step 2: 48.38 code (2026-11-09)
- COMMIT 3791821e (w48-net, on 5f2a8e98 = merge of w48-net5 into w48-net):
  pure half cm_rirq (GeomData, Causes, Classify(v), DataMaskWrite/Proven,
  MaskBoth/BothMasked, AckWrites, WatchDataStart/WatchData); rirqos (Claims0,
  ClaimsData, ISR ack via AckWrites, storm masks both, DPC -> CmNetIrqKick
  while DataOpen, DataOpen/Close/State, arm re-opens DataWant, ITR0 500);
  cm_net (RxTaken, CmNetIrqKick, RingsUp opens, rundown closes first and
  falls back if unproven, counter cm.net.irq.kicks, query 92/96);
  XhciCmNetInterrupt CM_VAL_NET_IRQ (rule table, cm_reg, both INFs,
  tuning.md); query service block 96 -> 104 (CM_QN_SVC_MIN_BYTES 96 for
  readers); XHCITR "Ring interrupt:" line; record 35 revision 25 (5.4, 5.5
  "As coded", 8.1 row, section 17 rows).
- DESIGN DEVIATION found while coding: revision 24 said query words "88 and
  92, reserved before", but 88 is RingsFailed; coded as 92 (state) and 96
  (kicks), block grown to 104; recorded in revision 25.
- Host: test_cm_rirq 233/0, test_cm_qnet 64/0, test_trnet 94/0,
  test_cm 303/0, test_cm_net 81/0.
- First review (--base 5f2a8e98): needs-attention: [high] an opening
  whose rollback mask is unproven left an unwatched cause enabled; [medium]
  data interrupts could pass ring 0's self-test. COMMIT 0dfe42e6 fixes both
  (cmRirqDataTry after the self-test, Claims0 in the test, retest masks the
  data cause, LIVE -> fallback).
- Second review: needs-attention: [high] a re-arm/reconnect replaced
  the geometry and dropped a retained, unproven data bit (shared line with
  AutoClearOff could be left asserted). Fix drafted (o4.spec: carry the data
  bit through cmRirqUp/IoConnectInterrupt, prove it masked in the arm before
  unmasking ring 0, else no arm). Applied after the running gated build.
- My mistake: edited w48-net while its gated build of 3791821e ran; killed
  that build tree (PID 137464) and removed its slot and two stale locks
  (out\gated-x86.lock, out\gated-tree.lock).
- Prep fixes drafted (not run): E:\xhci98-scratch\w48\net5\kit\prepnt.sh
  (Vista/7: command typed straight into Start Search, Ctrl+Shift+Enter) and
  kit\w2kwiz.sh (2000 FNH walked to Finish with Enter before INSTALL).
- Round-8 kit staged (not in the bench folder yet): E:\xhci98-scratch\w48\
  net5\kit8 (98SE IRQON/IRQOFF/WIN64K .REG+.BAT, SPEED8.BAT; WIN11
  SPEED8.CMD; STEPS8.TXT). Bench kit backed up: net5\bench-r7-backup.
- 8ee40ce6 gated amd64 release PASSED. FIX READY 8ee40ce6 (fix-ready.txt).
- COMMIT 3ffb7715 (retained unproven data bit carried through reconnect/arm,
  proven masked before ring 0 unmasks). Third review: APPROVE.
- Gated x86 release 0dfe42e6 PASS (INF gate incl. XhciCmNetInterrupt row);
  3ffb7715 x86 release PASS (gated-x86-irq3.log); amd64 running.

### Step 4: round-8 kit PREPARED (not given to the owner)
- Backup: net5\bench-r7-backup. kit3.py 3ffb7715 464,979 Eighth notes8.txt:
  98SE\XHCICM.SYS 494,707 B SHA-256 4761b73d...241a; XHCITR.EXE ee6418d5...
  (585,728 B, 3ffb7715). Added 98SE IRQON/IRQOFF/WIN64K (.BAT+.REG),
  SPEED8.BAT; WIN11\SPEED8.CMD; STEPS.TXT (round-8 owner steps, runs in
  order 1, 2, 4, 3 to save restarts).
- Values via HKLM\System\CurrentControlSet\Services\USB (cm_reg.c reads the
  machine key first) and Services\VxD\XHCIPFW; MSTCP DefaultRcvWindow
  "64240", restored by hand (98 SE regedit cannot delete a value).
- 3ffb7715 gated amd64 release PASS (gated-amd64-irq3.log).

### Hand-over (net5, ~23:30) - stopped at a clean point (context)
- Nothing of mine running: no QEMU started this run, no build, no review job;
  no slot held.
- OWED: step 2 emulator legs (none run): kit86 from 3ffb7715 (bin\pkg.cmd
  x86 + mkkit.sh + net5\guest\*.BAT + TBSPEED.EXE), values by .REG under
  Services\USB (and XHCIPFW on 98 SE). Step 3 legs: prep fixes drafted in
  net5\kit (prepnt.sh, w2kwiz.sh), untested. Step 4 kit prepared, not given.

### Merges (w48-net)

Merged into `3.4.3.0` at `59b6f5db` (2026-11-09, up to `38e0b49f`). The
merge review found a use-after-free: the network PDO's removal unlinked
the PDO before draining the calls inside, so a parent removal could free
the NHI extension under one. Fixed in `f36bf51d` and `8ee40ce6` (the
removal holds `Net.PdoCalls` and closes the PDO's gate before either PDO
is unlinked; record 35 section 6.6). The second merge, `7db0e1f9`, brings
that fix and 48.38's code (record 35 revision 25); reviewed, nothing
material; the integration build of `7db0e1f9` passed. The owner's decision
of the day: `XhciCmRingInterrupt` stays 0 by default, revisited after 48.38
is read on the P14s with `XhciPfwNhiLine` 0 for that run only.

## The XDomain receive entry's credits held to the 7-bit field (2026-11-09, w48-dmacr)

`tbtU4XdUp` (`src/cm/tbt_u4cm.c`) set the receive entry's credits at the
lane to the lesser of the host lane's spare credits and the host router's
`baMaxHI`, and `tbtU4XdEntry` shifted that into PATH_CS_0's Path Credits
Allocated (bits 23:17, 7 bits, Spec 8.2.3) under the mask alone. `baMaxHI`
is a 16-bit Buffer Allocation value and Total Buffers is 10 bits, so a
figure of 128 or more wrapped: 128 wrote 0 credits, 600 wrote 88. A
software-CM host reporting a large `baMaxHI` (USB4 version 2 hosts are the
likely ones) would have had no working Thunderbolt networking.

| Writer | Credit source | Before | Now |
|---|---|---|---|
| `tbtU4XdUp` -> `tbtU4XdEntry` (XDomain receive, lane) | min(lane spare, `baMaxHI` or 14) | unclamped | clamped to 127, logged once per service built (`cm.net.credits.clamp`, port << 16 \| figure) |
| `tbtU4XdEntry` (XDomain transmit, host interface) | kept as read | - | - |
| PCIe / USB3 tunnel (`tbtU4WriteEntry` from the tunnel build, about line 4692) | min(lane avail, `baMaxPCIe`/`baMaxUSB3`) | already clamped (`TBT_PATH_CREDITS_MAX`) | unchanged |
| PCIe kid tunnel (about line 8708) | min(lane avail, `baMaxPCIe`) | already clamped | unchanged |
| Protocol-adapter entries (TBT3 7, `asm7` 7, else 0) | constants | fit | unchanged |
| `tbtU4XdEntry`, `tbtU4WriteEntry` | any caller | masked | saturate at 127 as a backstop |

No DisplayPort path is written by the software CM, and no other file in
`src/cm/` writes a path entry.

Vectors: `test_u4xd` `test_credit_clamp`, a host lane of 1000 Total Buffers
and `baMaxHI` 127, 128, 600 and 0xFFFF: 127 written each time, no log at
127, one `cm.net.credits.clamp` line otherwise (the figure `baMaxHI`, or
the lane's spare at 0xFFFF). Without the fix the three clamped cases read
0, 88 and 102. Host tests: PASSED. Record 35 section 5.1: one sentence.

Commit `6b6dbdf2` on `w48-dmacr` (base `6287fba9`). Gated build (slot
`gated.3`, `XHCI98_SELFTEST_SKIP_OK=1`): **BUILD + GATES PASSED (release)**,
4:29.

Reviewed: approve (no findings).

### Merge (w48-dmacr)

Merged into `3.4.3.0` at `99789062` (2026-11-09).

### The host-test runner and an antivirus block (w48-xhq)

From the stor2 merge on, every gated build failed in the host tests:
test_xhq built, linked, and then "The system cannot execute the specified
program" (exit 9020). Microsoft Defender flagged the fresh exe as a
heuristic match and blocked it; the merge had only added installer
strings and headers to its link set. The owner excluded the build drive.
The runner now reports such a launch as blocked (return 2) with a hint,
not as a crashing vector (`0ba46ccc`, design record 03), merged at
`07ebe109`.

## 48.29 A kept child's departure under its token's kid generation (2026-11-09)

From the review of the stor2 merge: on 8.1 `CmRegAttDepart` (record 31
section 4.10, K4) recorded a kept child's departure under the member
port's generation unconditionally, while `CmMarksConfirmEx` confirms a
chained entry with a token by its kid generation (record 34 section
3.12). After a chained device connected again without its parent the two
differ: the confirmation was refused (`CM_MK_CONF_GEN`), no teardown
followed, and `KeptDeparted` kept the departure from being recorded
again. `45ceb0c1`: `CmRegAttRemoved`'s rule became `CmMarksDepartGen`
(`cm_marks.c`), used by both; `test_cm_marks`'s `test_depart_gen`
confirms a departure whose kid and member generations differ and refuses
the one recorded under the member's (253 checks, 0 failures). Reviewed:
approve.

## 48.28 ME: the take refused (2026-11-09)

The owner's decision of the day: taking a controller the BIOS set up is
refused on Windows ME (stor2's ME leg: the start hung after the desktop,
16 minutes in SeaBIOS's own code). `2df96ef8`, reviewed (approve):

- Stor98: on ME (VMM 4.90 and later, `PlatIsMe`) a take decision from
  either source is set aside (`PciScanTakeMe`, `src/pciscan.h`) and the
  controller left; `StorFind` says " left: already set up,
  StorTakeFirmware ignored on Windows ME" or "... stortakefirmware=1
  ignored on Windows ME"; stage `0x0C` sets bit 25. `pciscan_ut`: every
  decision and source on both systems.
- The installer: `StorTakeFirmware`'s Advanced row greyed on ME,
  "(Windows 98 SE only: on ME it can hang Windows' start)", never
  written there (`inst_s98_applies_os`, `inst_s98_greyed_os`,
  `inst_s98_only_os`: the read, the console prompt, the save, the
  wizard's rows and choices). XHCITR's row the same (`trs_state`,
  `trs_only` now given the system).
- The INF writes 0 on both systems as before (one section), with a
  comment; the INF gate unchanged (its rows name no system).
- Documents: Stor98's README, package readme (and
  `test\stor98-settings.txt`, "98 SE only, not ME"), PLAN ("Never on
  Windows Me"), TESTING, history; `tuning.md`; record 26 section 5.6.
- Host: `test_inst` 9056 checks, `test_trset` 1975, Stor98's unit tests
  all passing.

`d0e3eea0`: record 31 section 4.10 names the owner's decision for 48.29
(a drive that never gets the id on 8.1 stays on `stornvme`, with the
installer saying so).

**Builds**: the gated `release` x86 and `release -amd64` builds of
`d0e3eea0` ("BUILD + GATES PASSED (release)" both,
`E:\xhci98-scratch\w48\stor3\build-x86.log`, `build-amd64.log`), the
tools rebuilt and `make-package.ps1 -Arch x86` and `-Arch amd64`
(`tools-pkg.log`); the x86 kit by `ConvertTo-FamilyLayout`
(`stor98.mpd` `4e905335...`, `stor98.sys` `df9232ba...`, `xhcicm.sys`
`475f93db...`, `xhci98.sys` `0cd3e078...`).

## 48.28 ME: the setting ignored, the start normal (2026-11-09)

A copy of stor2's `fme` overlay (`E:\xhci98-vm\p48\stor3\fme\`: ME, the
`2c904543` kit, Stor98 bound to the internal drive `00:05.0`,
`AdapterSettings` `stortakefirmware=1` in its driver key
`SCSIAdapter\0001`, `StorTakeFirmware` 0; the overlay on which that
kit's take hung the start). A start without the internal NVMe, this
tree's x86 kit run (`INSTALL.BAT /console /yes /nopause`, exit 4: the
drive's entry "Reinstall (same version)", `stor98.mpd` not placed since
the tree's version is still `3.4.2.0`, the earlier one's), so this
tree's `stor98.mpd` (`4e905335...`) was copied over
`SYSTEM\IOSUBSYS\STOR98.MPD` by hand (`fc /b`: no differences), and a
clean shutdown. Then cold starts with the internal drive back
(`bootindex=1`, SeaBIOS sets it up and serves it):

| Step | Result |
|---|---|
| The controller's own `stortakefirmware=1` (`fme` b) | the desktop at about 2 minutes, then normal: the clock running, Run and the export taken 5 minutes on (`s-b1.png` to `s-b3.png`); `StorFind 0.5.0` "... cmd 0107 cc 00460001 csts 00000001 left: already set up, stortakefirmware=1 ignored on Windows ME; result 0"; the tunnelled drive `5.0.0` taken as before; a clean shutdown in 11 s |
| `AdapterSettings` emptied and `StorTakeFirmware` 1 (`fme` c) | the same start; `StorFind 0.5.0` "... left: already set up, StorTakeFirmware ignored on Windows ME; result 0"; a clean shutdown in 11 s |

So on ME the setting at 1, from either source, is ignored and said, and
ME starts normally with the BIOS's drive left to the BIOS.

Evidence: `E:\xhci98-vm\p48\stor3\fme\` (`cmdline-a.txt` to
`cmdline-c.txt`, `serial-a.log` to `serial-c.log`, `s-*.png`,
`pids.txt`).

## 48.28 98 SE: a warm start without the take, the shutdown (2026-11-09)

A copy of stor2's `f98` overlay (`E:\xhci98-vm\p48\stor3\f98\`: 98 SE, the
`2c904543` kit with `/storage`, `stor98.mpd` `6fd54564...`; the 98 SE
take path is unchanged in this tree), the internal drive (`00:05.0`,
`bootindex=1`) and the tunnelled drive present:

| Step | Result |
|---|---|
| `StorTakeFirmware` 0 (set from a start without the internal drive), a cold start (`f98` b) | `StorFind 0.5.0` "... left: already set up; result 0" (the control: nothing taken) |
| Then a warm start of Windows alone (`rundll user.exe,exitwindowsexec`, 21:53) | the screen black, the processor in the VMM (`EIP` `c000a4d8` to `c000a882` sampled over 15 minutes, `s-b3.png`, `s-b4.png`); Windows did not come back |
| `StorTakeFirmware` 1, a cold start (`f98` d) | "... taken: already set up, overridden by StorTakeFirmware; result 1" |
| Then the shutdown (`rundll32 shell32.dll,SHExitWindowsEx 1`) | the guest powered off 11 s later |

So the warm-start hang stor2 read with the take is this emulator's known
98 SE warm-restart hang: the control without any take hangs the same way
in the same code. A taken drive shuts down cleanly. **Not run**: the
removal of a taken drive; the only controller SeaBIOS sets up here is the
internal one on the root bus, which cannot be removed in this emulator,
and the tunnelled drive is never set up by it.

Evidence: `E:\xhci98-vm\p48\stor3\f98\` (`cmdline-a.txt` to
`cmdline-d.txt`, `ev-b-control.log`, `ev-d-take.log`, `s-*.png`,
`pids.txt`).

## 48.20 UAS hot-plugged at SuperSpeed on 98 SE (2026-11-09)

fs98's finding (a hot-plugged `usb-uas` waits for `qom-set ... attached
true`) read on the same `f98` copy, the internal drive left out (`f98` e):

| Step | Result |
|---|---|
| `drive_add` a 256 MiB qcow2 (stor2's `uas.qcow2`), `device_add usb-uas,id=uas0,bus=xh.0,port=3`, `device_add scsi-hd,bus=uas0.0,drive=uasd` | `info usb` "Device 0.0, Port 3, Speed 12 Mb/s"; `qom-get /machine/peripheral/uas0 attached` false |
| `qom-set /machine/peripheral/uas0 attached true` | "Device 0.1, Port 3, Speed 5000 Mb/s" (addressed); no wizard (the driver installed by stor2's power-on leg); the live devnodes (`HKEY_DYN_DATA\Config Manager\Enum`): `USB\VID_46F4&PID_0003\...-3`, `XHCIUAS\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\...` and its disk `SCSI\QEMU____QEMU_HARDDISK___2\XHCIUAS&DISK...`, each `Problem` 0 |
| `device_del uas0` | the three devnodes gone from the live list |

So a UAS disk hot-plugged at SuperSpeed is enumerated on 98 SE with
NUSB's storage, bound by `xhciuas.sys`, its disk started, and removed
cleanly.

Evidence: `E:\xhci98-vm\p48\stor3\f98\` (`cmdline-e.txt`,
`ev-e-uas.log`, `s-e*.png`, `pids.txt`).

## 48.29 The kept child on 8.1 x86 under Driver Verifier (2026-11-10)

**The vehicle**: a fresh overlay of the q35 8.1 x86 base
(`E:\xhci98-vm\p48\stor3\v86u\`, `-Usb4 asm4242 -EncAtBoot -Tablet`,
`-device usb4-debug-port`; drive A in the ORICO model (`asm2464`, its
switch) on USB4 port 1; from the second start drive B in a second
`asm2464` enclosure on port 2). `3.3.0.0` (`releases\3.3.0.0`,
`release-x86` and `tools`; `stor98.sys` `6e859863...`, `xhcicm.sys`
`851f245f...`) and this tree's x86 kit (above). No signing: 32-bit 8.1
does not enforce it (each unsigned-driver prompt answered "Install this
driver software anyway"). Verifier on `xhcicm.sys` only from the update's
restart (`verifier /standard /driver xhcicm.sys`; `verifier /query`
"xhcicm.sys, loads: 1" at every reading after it).

**The update from `3.3.0.0`, one drive on Stor98 and one on `stornvme`**:

| Step | Result |
|---|---|
| `3.3.0.0` installed (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`), drive A present | exit 3; "Staged stor98.inf as oem3.inf"; Stor98 on A; the mark service installed (`ev-i33.log`) |
| The mark service stopped (`sc stop`, start demand), shut down, drive B added, cold start | A: `stor98` (3.3.0.0), problem 0, no tunnel id; B: `stornvme`, no tunnel id (`ev-r1.log`) |
| This tree's kit run as the update, the earlier connection manager running | the plan as on x64: A "INF stor98.inf; driver stor98 3.3.0.0; Update / Skipped: no tunnel id yet (needed on 8.1): run Setup again after the restart", B "... driver stornvme ...; Install / Skipped: no tunnel id yet (needed on 8.1): the mark service adds Stor98 after the restart"; `stor98.inf` staged as `oem6.inf`. Unlike x64, the x86 connection manager was updated in place ("Updating xHCI98 USB4 Router (ASMedia ASM4242) ... done"; "xhcicm 3.4.2.0, started"), so the tunnel id came during the run: "Results: Stor98 Storage Controller for NVMe SSDs: stor98 3.4.2.0, started" twice, "Stor98's earlier oem3.inf was removed from the driver store.", exit 3 (`ev-inew-a.log`) |
| Before the restart | both drives `PCI\XHCI98_TUNNEL&CC_010802` first, `stor98` 3.4.2.0, problem 0; events 400 on `oem6.inf` with "Matching Device Id: PCI\XHCI98_TUNNEL&CC_010802" (`ev-r2.log`) |
| The restart, Verifier on from here | both the same, no new event 400 (`ev-r3.log`) |

Setup was not run a second time: the first run had already removed the
earlier INF.

**The conditions**, Verifier on `xhcicm.sys` throughout, each read by the
devnodes' service, problem and compatible ids and the last PnP events
(`R.CMD`):

| Condition | Result |
|---|---|
| Drive A disabled and enabled (`devcycle cycle`) | Stor98, problem 0, the id kept, no new event 400 (`ev-c1.log`) |
| The enclosure DSP above A disabled and enabled (a parent's restart) | the same (`ev-c2.log`) |
| **A parent's removal with the drive already disabled**: A disabled, its DSP cycled, A enabled | the same (`ev-c3.log`) |
| **The eject**: `XHCITR /eject id=` enclosure A's upstream port, as an administrator | the confirmation "Stop QEMU NVMe Ctrl SCSI Disk Device (E:)?" listing the upstream and downstream switch ports, "Stor98 Storage Controller for NVMe SSDs" and "Disk drive"; OK: "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (E:). The eject was confirmed to the Thunderbolt driver. The Thunderbolt driver has not confirmed that the PCIe tunnel was closed. It is now safe to disconnect the device." (`s24.png`, `s25.png`); 20 s later drive A's devnode was gone and its functions were no longer in the emulator's PCI tree (`info pci`: only B's NVMe): the tunnel closed after the tool's wait; no bug check (`ev-ej2.log`) |
| **A re-plug** after the eject (`device_del enc0`, then `device_add` again) | A back with the id, Stor98, problem 0, no new event 400 (`ev-rp.log`) |
| **A removal held by open handles**: `hold 150` opening `\.\Scsi0:` to `Scsi2:`, enclosure A removed (`device_del`) 15 s in | "A Thunderbolt or USB4 device was removed unexpectedly"; the handles held 150 s, then closed; A gone, B on Stor98; no bug check (`ev-hold.log`); plugged again: A back with the id, Stor98 (`ev-rp2.log`) |
| A cold start, drive B now in an enclosure with no switch (directly below tunnel port 2) | the direct drive configured by Windows itself on `oem6.inf` at its first enumeration; A on Stor98 (`ev-d1.log`) |
| **A drive directly on a tunnel port**: the direct drive cycled, then tunnel port 2 itself | Stor98, problem 0, the id kept, for both drives, no new event 400 (`ev-c5.log`) |
| **The uninstall** (`INSTALL.BAT /console /uninstall /yes /nopause`), then the restart | exit 3 (the filters taken off, "The service xhcimark was removed.", "Stor98's oem6.inf was removed from the driver store.", "The stor98 service was removed.", `xhcicm` and `xhcihlpr` "marked for deletion; removed at the restart"); the restart clean, no bug check; both drives on `stornvme`, problem 0; `xhcicm.sys` "loads: 0" (`ev-u.log`, `ev-u2.log`) |

So on 8.1 x86 the update from `3.3.0.0` ends with both drives on
Stor98's id model and the earlier INF gone in the first run (the x86
connection manager is replaced in place, so the id comes during the run;
on x64 it came at the restart), and every condition of section 4.9, the
eject and a re-plug after it hold under Verifier on `xhcicm.sys` with the
release image, with no bug check. The eject's own message said the
tunnel had not been seen closed within the tool's wait; it closed
moments later.

**Not run**: a second Setup run after the restart (nothing left for it);
the `cm.kept` records (the release image).

Evidence: `E:\xhci98-vm\p48\stor3\v86u\` (`cmdline-a.txt` to
`cmdline-c.txt`, `ev-i33.log`, `ev-r1.log`, `ev-inew-a.log`, `ev-r2.log`,
`ev-r3.log`, `ev-list.log`, `ev-c1.log` to `ev-c3.log`, `ev-c5.log`,
`ev-ej2.log`, `ev-rp.log`, `ev-hold.log`, `ev-rp2.log`, `ev-d1.log`,
`ev-u.log`, `ev-u2.log`, `serial-*.log`, `s*.png`, `pids.txt`).

### Merge (w48-stor3)

The kept child's departure fix merged at `10cd187a`; the rest at
`411b55a5` (2026-11-10), the merge reviewed, nothing material; the
integration build of `411b55a5` passed.

## The 98 SE first-boot fatal exception 0E (worker boot0e, 2026-11-09)

Worker boot0e, development host A, branch `w48-boot0e` from `5bce9bf5`.
No code change: the fault did not reproduce, and nothing read points at
this project's code. The readings below are what a further attempt starts
from.

### What was seen before

| Who | Image and device set | Boot | Screen |
|---|---|---|---|
| net3 | fresh 98 SE overlay, `-Usb4 tb3ar`, gated release of `ba9bac2d` with `/thunderbolt /tbnet` | the first after the install's shutdown (`p98` p2) | "A fatal exception 0E has occurred at 018F:BFF8AB97", first screenshot at 160 s; after the key a black text-mode screen |
| net3 | an overlay of the same disk after p2's fault and a safe-mode boot, an XP peer on an inter-domain cable (`cells\C1\A`) | normal start | "018F:BFF8713B", first screenshot at 150 s; three more overlays of the same disk without the peer (`t98` t1, t2, `w1`) started clean |
| chain | 98 SE, `-Usb4 adl`, a USB4 enclosure with a downstream port, qemu `xhcicm.sys` of `20e163d1` | the cold start after a Stor98 and bridge install's restart (`w98` p) | "0197:BFF86EBD", black screen, safe mode next |
| chain2 | a fresh 98 SE overlay, the same device set, release `59a28f59` with `/thunderbolt /stor98` | install and restart (a), the wizards answered with Enter and a restart accepted (b), the cold start (c) | "0197:BFF86EBD" at the first screenshot (174 s), black after the key, safe mode next |

The time in the reports ("about 150 s") is the time of each script's
first screenshot, not of the fault. Chain's connection-manager log
(debugcon, `E:\xhci98-vm\p48\chain\w98\debugcon-p.log`) dates it: the
first tunnel's bridge poll, whose budget is 2 s, read
`cm.reenum.9x.version` at 6.1 s and `cm.reenum.up.poll=00000303` at
113.6 s of the connection manager's clock, where the three clean boots of
the same disk (k, m, n) read it 2.0 s later. The 107 s gap reads as the
blue screen holding the machine (the screenshot at 170 s showed it), so
the fault came within about 8 s of the
connection manager's start, early in the boot, before the desktop. The
black text-mode screen after the key agrees: the shell never came up. In
the boots that install new devices on this vehicle, Windows asks to
restart, or starts the shell, only after the drivers' first minute; the
Run key (XHCITR's tray, Dr. Watson) is read only after that.

### The reproduction

Images, each a fresh overlay of the q35 98 SE base, made in my folder
(E:\xhci98-vm\p48\boot0e; scripts in E:\xhci98-scratch\w48\boot0e\kit):

- i0: `-Usb4 tb3ar`, net's gated release kit of `ba9bac2d` (copied,
  `98-me\cm\xhcicm.sys` b74265a5...), Dr. Watson added to the Run key,
  `INSTALL /console /yes /nopause /thunderbolt /tbnet`, shut down before
  the first post-install boot (`prep.sh`).
- i2: the same with `/notray` (no XHCITR Run value).
- i3: i0 with the qemu flavour of `ba9bac2d` copied in (debugcon log).
- n0: `-Usb4 none` (plain qemu-xhci), plain install.
- j0: chain2's device set (`-Usb4 adl`, the enclosure with one downstream
  port and its NVMe, a second NVMe drive), `/thunderbolt /stor98`, shut
  down (`prepj.sh`); j1 the same with the qemu drivers and `BootLog=1`.
- k0: chain2's exact recipe: its kit (`59a28f59`), its emulator binary,
  its `R9.BAT`, the install's restart.
- l0, o0: copies of the two disks that had faulted (chain2's `w98e` after
  its fault and safe mode; net's `p98` after p2's fault and safe mode).

Each trial is a fresh overlay of one image, cold-started, the VGA text
buffer read every 5 s for "exception" (`trial*.sh`), a screenshot every
30 s.

| Image | Boot | How | Boots | Faults |
|---|---|---|---|---|
| i0 (tb3ar, `/tbnet`) | first after the install | answered No at 240 s, watched to 450 s (random `uuid` 5, fixed `uuid=5a3b1c2d-...-0001` 3) | 8 | 0 |
| i0 | first | no QMP request before 200 s (one screenshot at 150 s, as net's scripts), Yes at 240 s | 10 | 0 |
| i0 | first, then the cold start after the accepted restart | Yes at 240 s, then No | 2 + 2 | 0 |
| i0 | first, the QEMU process suspended 300 of every 600 ms or 500 of every 700 ms for 290 s (a starved host) | Yes at 300 s | 8 | 0 |
| i0 | first and second, `-m 1024` | Yes, then No | 1 + 1 | 0 |
| i0 | first, two overlays joined by an inter-domain cable (both ends 98 SE) | No at 240 s | 4 | 0 |
| i2 (i0 with `/notray`) | first, then after the restart | Yes, then No | 2 + 2 | 0 |
| i3 (i0, qemu drivers) | first | as the quiet row | 3 | 0 |
| n0 (`-Usb4 none`, plain install) | first | watched to 400 s (desktop and Run programs by 150 s) | 3 | 0 |
| j0 (chain2's device set, `/stor98`) | b: wizards answered with Enter, a restart; c: the cold start after it | trial3 | 5 + 5 | 0 |
| j1 (j0, qemu drivers) | b, c | trial3 | 1 + 1 | 0 |
| k0 (chain2's kit, emulator and steps) | b, c | trial3 | 3 + 3 | 0 |
| l0 (copy of chain2's faulted disk) | normal start | watched 330 s | 4 | 0 |
| o0 (copy of net's faulted disk) | normal start, no peer | watched 330 s | 1 | 0 |
| o0 | normal start, two overlays joined by an inter-domain cable | watched 400 s | 4 | 0 |
| total | | | 73 | 0 |

The screenshots of every boot were searched for a text-mode screen: none
showed a blue screen; two black text-mode screens at about 60 s were the
start after an unclean stop (`a2`, `c1`, whose restart boot had been
stopped by QMP), and both reached the desktop.

### What the readings say

- **Not reproduced**: 73 boots of 15 configurations, among them
  net's and chain2's exact device sets, chain2's own kit, emulator binary
  and steps, and fresh overlays of both disks that had faulted, gave no
  fatal exception. The other workers met it in about one boot in ten or more
  (net 2 of 5 boots of one disk; chain and chain2 one each in about ten
  98 SE boots); at one in ten, 73 clean boots in a row have a chance
  below one in a thousand, so the cause is a condition those runs had and these
  did not, not the device set, the package, the install or the emulator
  binary alone. Not found here: the host's load at the time (the
  starvation rows imitate it only roughly) is the one condition left
  untested that all four faults could share.
- **Not XHCITR, not Dr. Watson**: the fault came before the desktop (the
  black text-mode screen after the key in three of four cases; chain's
  log stalled 6-8 s after the connection manager's start, at the boot's
  device phase), and 98 SE starts the Run key's programs only once the
  shell is up, after the new devices' restart question is answered. No
  program of the Run key was running. The task list after the key
  (Ctrl+Alt+Del) was not reached in the earlier runs and could not be
  read here.
- **Not specific to Thunderbolt networking or to Thunderbolt 3**: chain
  and chain2 had no networking node (`/stor98`, a USB4 host with the
  software connection manager); net had Alpine Ridge with its firmware
  connection manager and `/tbnet`. What all four share is 98 SE's boot
  that installs (or has just installed) new devices, with `xhcicm.sys`
  loaded and its 9x CONFIGMG worker starting (`cm.reenum.9x.version`):
  on Alpine Ridge no tunnel is re-enumerated at boot (the qemu-flavour
  logs of `i3`: no `cm.reenum.up.poll`, no `cm.reenum.9x.call`), so the
  re-enumeration chain's log shows after the stall is not a condition.
  Whether a plain `qemu-xhci` system shows it is open: `n0` gave 3 clean
  boots, which says nothing at this rate.
- **The same family on this vehicle**: lessons.md ("Windows 98 SE does
  not resume reliably from Stand by in QEMU") and run-36 record KERNEL32
  fatal exceptions 0E (`0167:BFF87EDE`, `0167:BFF9DB34`) on 98 SE in QEMU
  with no USB controller in the guest at all; run-42 (42-A.9) records
  `017F:BFF87EDE` on keys sent into a bridge wizard, the same with the
  `3.0.0.0` connection manager. The addresses here (`BFF86EBD`,
  `BFF8713B`, `BFF8AB97`) are in the same part of KERNEL32. That is not a
  proof that the fault is 98 SE's or the emulator's, only that this
  vehicle has produced the same signature without this project's code.

### Verdict

Unattributed. No code of this project is implicated by any reading, and
no change is made. The fault list stays open as a vehicle reading.

### Left

- A boot that faults with the qemu flavour and `BootLog=1` (MSDOS.SYS's
  `[Options]`; appending the line after the padding did not take, as `j1`
  showed: BOOTLOG.TXT kept its October date) would date the fault against
  the boot's own stages and against the drivers' log.
- The host's load at the time of each fault (gated builds running beside
  the guests) is the untested common condition.
- Whether real hardware shows it: no report from the owner's 98 SE bench.

## 48.1 `uac2` and `uac1` on 98 SE: the 5.5 dB level offset, and streams that stop under 98 SE's own UHCI (2026-11-09, w48-aud98)

usb-wdm-free, branch `w48-aud98` from `main` `2fea99e`. Owner decisions
of 2026-11-09 (the 98 SE level; the UHCI stalls).

**The level offset: 98 SE's own, no driver change.** `UACWAV mix` (it now
lists the source lines; `srcvol` sets them, `8c1eb43`) read 98 SE's mixer
for a `uac2` `hs24` device under xHCI98: the Speaker line's Master Volume
(the driver's node) at 65535, and the Wave and SW Synth source lines,
which 98 SE adds and applies before the samples reach the driver, at
32845 of 65535. As found and with the speaker line at the top the device
received -25.5 dBFS for `UACWAV`'s -20; with the Wave line at the top
-20.0 dBFS, at 48 and 44.1 kHz and in mono. Under Windows' own
`usbaudio.sys` (a `uac1` headset model) the same Wave default (its plays
did not open, `MMSYSERR_NOTSUPPORTED`, so no level there). The driver's
gain is unity; the earlier 98 SE readings (-25.5, -11.52) stand as
readings at 98 SE's Wave default. Record usb-wdm-free
`docs/testing/2026-11-09-uac-98se-level.md` (`924fc62`, `757a420`).

**The UHCI stalls: the driver's, fixed.** Under 98 SE's own UHCI a failed
stream's counters (`uac2` 0.1.10.0, `uacemu.py --profile fs`; four `uac1`
`adsrc` recordings likewise) read 250 data URBs in a row refused with
`USBD_STATUS_BAD_START_FRAME`, 25.8's run limit, so the driver ended the
stream; a trace build read the stack's ASAP chain lagging the bus after a
stall (URBs placed on frames already past, each lost whole) until it
refuses a frame list later. Design 26.13 (`uac2.md`): after a refusal on
any pipe, or a data URB lost whole that the stack placed behind the bus
frame read at its submission, the worker holds every slot until none is
with the stack (at most 1 s), reads the bus frame and submits each pipe's
slots at explicit start frames (current + 8 + k x F), ASAP after that; 4
resyncs with no good data URB between end the stream as before. `uac1`'s
adaptive-source synch endpoint: a synch URB with a packet delivered ends
26.12's run, and once Ff has been delivered a failed synch URB never ends
the stream (three 0.1.1.0 recordings, and two of the fix before this
step, had ended at that run with no data failure). Stats words 95 to 97.
Build stamps `uac2` 27, `uac1` 3. Commits `744bc33`, `0c5a312`, `cb6d547`,
`1b60605`, `2cd03b1`, `fae6fe1`, `04bf305`, `98270a5`, records `cfdd0c1`, `fd435b7`; reviewed and revised
(eight code rounds and three record rounds, converged). Host tests: `uac2` `test.cmd` PASS (`desctest`
10094 assertions), `uac1` `test.cmd` 565533 checks PASS; builds at
`98270a5`, tree clean: wdmx86 free `uac2wdm.sys` `035acb6f...`,
`uac1wdm.sys` `674f2eaa...`, wdm64 free `uac2wdm.sys` `c08c8656...`
(unsigned), wdmx86 trace `db993afd...`.

98 SE's own UHCI, `w98p`, a fresh overlay a leg, rounds of play 48k 5 s,
play 44.1k 3 s, rec 48k 3 s, rec 44.1k 3 s:

| Leg | Build | Device | Streams passed | Ended by |
|---|---|---|---|---|
| `uac2-w98-own-b1u2` | 0.1.10.0 (released) | `fs` | 23/24 | 1: 250 refusals |
| `uac1-w98-own-b1` | 0.1.0.1 (`free/`) | `adsrc` | 5/8 | 3: 250 refusals |
| `uac1-w98-own-b2` | 0.1.1.0 (`5e1e4eff`) | `adsrc` | 1/8 | 4: 250 refusals; 3: synch run |
| `uac1-w98-own-a4` | the fix at `04bf305` | `adsrc` | 4/6 | 2: synch run after delivery |
| `uac1-w98-own-a5` | the fix (`98270a5`) | `adsrc` | 8/8 | none (one resync) |
| `uac1-w98-own-a5` | the fix (`98270a5`) | `alaw` | 16/16 | none |
| `uac2-w98-own-a6u2` | the fix (`98270a5`) | `fs` | 24/24 | none (one resync) |
| `uac2-w98-own-a7u2` | the fix (`98270a5`) | `fs` | 22/24 | none; 2 recordings got no buffers (below) |
| `uac2-w98-own-b3u2` | 0.1.10.0, beside `a7u2` | `fs` | 20/24 | 4: 250 refusals |

`uac2` 43 of 48 before, 46 of 48 after, no stream ended by the driver
after; `uac1` `adsrc` 1 of 8 (0.1.1.0) before, 8 of 8 after. The two
failures after came in the minutes the release in the other guest failed
too: one recording ran to its end at the driver (288768 bytes, no failed
URB) while `UACWAV` got no buffer; one resynced 22 times and got 192 bytes,
the stack returning its URBs placed behind the bus successful and empty
(not lost, so the lag test does not see them) and the simulator's data not
reaching the driver. A first build that resynced only at the refusal
(`cb6d547`) left about 1.3 s without packets before each resync; its trace
build gave the lag reading. Development host A was loaded by other work
throughout; first-plug wedges (36.41) cost many plugs.

Record usb-wdm-free `docs/testing/2026-11-09-uac-uhci-stalls.md`;
`docs/testing/open-items.md` entry "`uac2`, `uac1` on 98 SE: the level
offset and the stalls under 98 SE's own UHCI".

Left: the lag test for recordings and the two failed recordings above (a
trace on this stack); the fix under NUSB's UHCI, ME's and 2000's own UHCI
and on real USB 1.1 ports; `uac1`'s other profiles with the fix; one guest
that stopped answering after a `uac2` play under the release (not seen
again); `free/`'s `uac1wdm.sys` is still 0.1.0.1 and `uac1wdm.rc` on `main`
reads `FILEVERSION 0,1,1,0` with the string "0.1.0.1" (the release owes
both). Proposed roadmap text: 48.1 (aud98 part) done for the level (98
SE's own, recorded) and partial for the stalls: "Left: the recording form
of the lag test, NUSB/ME/2000 UHCI legs, real USB 1.1 hardware".

## 48.1 (continued) `uac2`, `uac1`: the merge review's two [high] defects in design 26.13 (2026-11-10, w48-aud2)

usb-wdm-free, branch `w48-aud2` from `main` `639947c` (the w48-aud98 merge),
worktree `E:\usb-wdm-free-wt\w48-aud2`. Not merged, not pushed.

**Empty capture URBs (defect 1): fixed.** Only a data URB that carried audio
now clears the resync limit's run and the lag test. For playback that is any
good URB. For capture it is a URB that delivered at least one PCM frame. A
successful capture URB that delivered nothing arms the lag test, as a URB lost
whole does. If the stack placed it behind the bus frame read before it was
submitted, it starts a resync. Rules in `uacplan.c`: `UacPlanDataDone`,
`UacPlanResyncStart`. Regression: `desctest.c` replays the recorded failure,
refusals alternating with successful empty capture completions. The stream
gets four resyncs and the fifth ends it. With an audio URB between refusals it
never ends.

**Synchronous frame request on the worker (defect 2): fixed.** The frame
requests are now sent and not waited for: one IRP of the worker's own, with
its URB stored in the stream (`FrameSend`, `FrameDone`, `FrameTake`). The
worker keeps retiring completions, keeping the hold deadline and seeing a
Stop. A Stop cancels the request at once. A submission-frame request still out
after 100 ms is also cancelled, and the slots then go without a frame. The
request's storage is kept until the stack gives it back, and the worker does
not exit while one is out. A data slot is marked as owned by the lower stack
only in the step that passes it on. Stop is read again after the frame is
taken, and once more under the lock just before that marking.

Review follow-ups:
- A request back but not yet taken still holds the storage.
- The hold deadline applies whenever a resync is still wanted, and is checked
  again just before the resync runs.
- The resync uses the frame plus its time out only up to 64 ms; past that the
  frame is read again. This keeps the slots inside the stack's 1024-frame
  start range.

Host vectors cover the worker's passes with a delayed request during a stop,
completions at every point of a pass, and the deadline edge. No host harness
runs `uacusb.c` itself.

**Side fix.** `uac1wdm.rc`'s FileVersion and ProductVersion strings now read
"0.1.1.0", matching `FILEVERSION`. The repo has no rc consistency test. The
`uac1` INF still says `DriverVer ...,0.1.0.1`; that is left to the release.

Commits: `ac0e5bc` (both fixes, design 26.13 items 4, 5 and 8 revised, build
stamps `uac2` 28 and `uac1` 4), `20a3b56` (rc), `31138cd`, `c5dcc35`,
`dca05e0` (review follow-ups). Reviewed and revised: the first three passes each raised
one [medium] defect, all fixed; the fourth approved at `dca05e0` (scratch `r1.txt`
to `r4.txt`). Host tests at `dca05e0`: `uac2` `test.cmd` PASS (`desctest` 10525
assertions), `uac1` `test.cmd` PASS (565533 checks). wdmx86 free builds at
`dca05e0`, tree clean: `uac2wdm.sys` `cf243b47...`, `uac1wdm.sys`
`568c81a5...`. The `uac2` wdmx86 trace build compiles; `uac1` has no trace
flavour.

**98 SE legs: not completed.** Kit `E:\xhci98-vm\p48\aud2\kit` (a copy of
aud98's, QMP 47980/47981, models 47982+, `scripts\stopmine.py`). The host was
loaded by other work: 10 other QEMUs, CPU 71 to 76 %.
- `c1`/`c1u2` (build `31138cd`): stopped during install, because a later
  commit replaced that build.
- `c2u2` (`uac2` `fs` `--stall 6`) and `c2` (`uac1` `adsrc,alaw` `--stall 4`)
  on `dca05e0`'s build: started 00:36, stopped at 00:39 during first install
  when this run ended. No stream results.

Every QEMU, simulator and harness of this run was logged in the kit's
`pids.txt` and stopped.

Left: the 98 SE legs on `dca05e0`'s build (staged as `stage\rel\uac2-aud2c`
and `uac1-aud2c`; the commands are in STATE.md). After them, the testing
record `docs/testing/2026-11-09-uac-uhci-stalls.md` and its open-items entry
need updating ("Left" still lists the empty-URB rule as owed). The recording
cases need a trace on this stack.

### Merges (usb-wdm-free, w48-aud98 and w48-aud2)

w48-aud98 merged into usb-wdm-free main at `639947c` (2026-11-10). Its
merge review found two defects: an empty successful capture URB reset
the resync bound, and a synchronous frame query could block the worker
that serves Stop. Both fixed on w48-aud2 (`ac0e5bc` to `dca05e0`),
merged at `a27d075`. The owner's answer of the day: Setup leaves 98 SE's
Wave line at its default. The 98 SE legs on `a27d075` are owed.

## 48.35 (the stand-by legs) - standby and hibernation with a device attached (2026-11-09 and 10)

Task 48.35's stand-by legs, which worker tbs could not read because q35 did
not resume these bases from S3 ("48.35" above), on the fixed SeaBIOS image
of worker q35s3 ("48.35 (the vehicle)"), then the owner's decision of
2026-11-10: with every leg passing at `XhciCmSleepEntry` 1 and nothing
regressing at 0, the default to 1 (decision U36) and the README narrowed to
what was read. Branch `w48-tbs2` from `3.4.3.0` at `a2d7fc7d`.

**Build and kit.** Gated `build-driver.cmd release` x86 of `a2d7fc7d`
(`XHCI98_SELFTEST_SKIP_OK=1`, slot `gated.4`): `BUILD + GATES PASSED
(release)` (`E:\xhci98-scratch\w48\tbs2\gated-x86.log`); `xhcicm.sys`
`c2566d4a9c6a8b12...`, `xhci98.sys` `6a3480b7ff2a208e...`, `stor98.sys`
`d0faf471da57b926...`. The kit: `make-package.ps1 -Arch x86`, laid out by
`ConvertTo-FamilyLayout`, with `XHCIMARK.EXE` (`cd4d4a36d533f9a9...`) from
this tree and `SETUP.EXE`, `INSTALL.BAT` and the other tools from worker
tbs's kit (`E:\xhci98-vm\p48\tbs2\xfer\KIT`). Each guest's driver hashes read
back after the install (`G.CMD drv`).

**Vehicle.** Development host A, TCG, 2 vCPUs, the pinned emulator
`asm4242-20261025-2230`, `launch-q35.ps1 -Usb4 asm4242 -EncAtBoot -Vga none`
(the ASM4242 model, its pre-OS CM on, the ORICO ASM2464 enclosure with the
FAT NVMe at power-on) plus `-bios E:\xhci98-vm\p48\q35s3\bin\bios-256k.bin`
(SHA-256 `e5dc0550...`, checked at every start), TCP QMP 46940-46942. 7 x86
(`w7x86` base, US-Dvorak, no display adapter, so that 7 offers Standby); 8.1
x86 and 10 x86 (`w81x86`, `w10x86`) with `-device virtio-vga` and Red Hat's
`viogpudo` (virtio-win 0.1.302, `devcon update` forced) so that they offer
Standby, Fast Startup off (`HiberbootEnabled` 0, so that the shutdown after
a leg is a shutdown). Fresh overlays; installed by `INSTALL.BAT /console
/yes /nopause /thunderbolt /stor98` (exit 3 on each; the unsigned-driver
prompts clicked), then `XhciCmSleepEntry` in the machine-wide `Services\USB`
key and a cold start. One script per leg, `C:\T\ONE.CMD <s3|s4> <tag> off`
(`E:\xhci98-vm\p48\tbs2\xfer\ONE.CMD`), spans the sleep: before it the value,
`devcon status *` counted, `W<tag>A.BIN` (4 MiB) written to the enclosure's
volume and compared, `XHCITR /report`; `SetSuspendState` (hibernation off
for S3, on for S4); after it `powercfg /lastwake`, 40 s, the count again,
the volume listed, `W<tag>A.BIN` compared again, `W<tag>B.BIN` written and
compared, the report, `XHCIMARK.LOG`'s flush lines, then `shutdown /s`.
S3: QEMU read `suspended`, held 45 s, `system_wakeup`. S4: QEMU exited at
the hibernation's power-off and was started again cold.

| Guest | Leg | `XhciCmSleepEntry` | Read | Verdict |
|---|---|---|---|---|
| 7 x86 | a, S3 | 1 | 113 devices running before and after (the q35 `fw_cfg` device at Code 28 both times); the ORICO volume `D:`, both files compared; `u4.sleep.entries 1`, `.routers 2`, `.ready 2`, `u4.sleep.entered=00000002`, `cm.suspend=00000004`, `cm.resume=00000001`, `cm.reenum.resumed=00000001`; the shutdown after it 18 s | **pass** |
| 7 x86 | b, S4 | 1 | 115 before and after; `D:` and its files; the same entry lines, `cm.suspend=00000005`, resumed `00000001`; shutdown 19 s | **pass** |
| 7 x86 | c, S3 | 0 | 115 before and after; `D:` and its files; `cm.usb4.sleep=00000100`, no `u4.sleep` line, `cm.reenum.resumed=00000001`; shutdown 18 s | **pass** |
| 7 x86 | d, S4 | 0 | 115 before and after; `D:` and its files; no `u4.sleep` line, resumed `00000001`; shutdown 18 s | **pass** |
| 8.1 x86 | a, S3 | 1 | 61 before and after; `E:` and its files; Enter Sleep on both routers read Sleep Ready; `cm.reenum.hold.passed` / `.released` for 0200, 0208, 0210; resumed `00000001`; wake source "Power Button"; shutdown 20 s | **pass** |
| 8.1 x86 | b, S4 | 1 | 61 and 61; `E:` and its files; the entry lines, `cm.suspend=00000005`, resumed `00000001`; shutdown 23 s | **pass** |
| 8.1 x86 | c, S3 | 0 | 61 and 61; `E:` and its files; no `u4.sleep` line, resumed `00000001`; shutdown 17 s | **pass** |
| 8.1 x86 | d, S4 | 0 | 61 and 61; `E:` and its files; resumed `00000001`; shutdown 18 s | **pass** |
| 10 x86 | a, S3 | 1 | 64 and 64; `E:` and its files; the entry lines, resumed `00000001`; shutdown 42 s | **pass** |
| 10 x86 | b, S4 | 1 | 64 and 64; `E:` and its files; the entry lines, `cm.suspend=00000005`, resumed `00000001`; shutdown 28 s | **pass** |
| 10 x86 | c, S3 | 0 | 64 and 64; `E:` and its files; resumed `00000001`; shutdown 41 s | **pass** |
| 10 x86 | d, S4 | 0 | 64 and 64; `E:` and its files; resumed `00000001`; shutdown 32 s | **pass** |
| 10 x86, `-Usb4 tb3ar` (Alpine Ridge, the firmware CM, the TB3 enclosure behind the native hot-plug tunnel port 00:08.0) | e, S3 | 0 (not read by this profile) | 61 and 61; `E:` and its files; `cm.suspend=00000004`, `cm.resume=00000001`, `cm.reenum.resumed=00000001`; shutdown 23 s | **pass** |
| the same | f, S4 | 0 | before 61, `E:` compared; after the resume 57, no volume holding `TESTDATA.BIN`, while `cm.suspend=00000005`, `cm.resume=00000001`, `cm.reenum.resumed=00000001` | **fail: the drive missing** (record 22 section 11.1.8, the native port; see below) |
| the same | g, S4 again (no shutdown after it) | 0 | before 61, `E:` compared; after the resume 57 and no volume, the same counters; still no volume about five minutes after the resume (`G.CMD ls`); the guest then answered no ACPI power button and was quit over QMP | **fail, reproduced** |

The shutdown times are from the script's last line (`shutdown /s /t 3`) to QEMU's exit, read on the host at 2 s polls.
At 1 the entry ran on the host router and the ASM2464 and both read Sleep
Ready on every leg; at 0 nothing was written to the fabric; the outcome
was the same at both values on every ASM4242 leg. `XHCIMARK.LOG` held no
flush line on any leg (`findstr flush` empty): the flush at a suspend notice
was not read.

**The Alpine Ridge hibernation.** The firmware connection manager's own
path, which `XhciCmSleepEntry` does not reach, behind the emulator's q35
native hot-plug port: the driver's resume met its obligations, but Windows
had read the slot empty before the firmware brought the tunnel back, the
case record 22 sections 11.1.2 and 11.1.8 already describe (Q1 (a), "a
known limitation"). Not a regression of this change; the README names
only the Alpine Ridge standby.

**The default.** Every ASM4242 leg passed at 1 and nothing differed at 0,
the owner's condition: `318860c0` sets `XhciCmSleepEntry` 1 (`cm_vals.h`,
both `xhcicm` INFs under the don't-overwrite flag, the CM INF gate's rows,
`test_cm`, `tuning.md` with the row's missing range and default columns,
`build-and-test.md`, record 22); `7e0bed2e` records the readings in
record 22 section 11.1.12 and narrows the README.
Both commits reviewed and revised (the first round asked for the readings
to be in the tree before the flip; the second, on both, found nothing
material). Gated `release` x86 of `7e0bed2e`: `BUILD + GATES PASSED`
(`gated-x86-flip.log`, `xhcicm.sys` `54843e04db5e487d...`), the CM INF gate
with the moved mutation rows and `test_cm` passing; gated `release -amd64`:
`BUILD + GATES PASSED` (`gated-amd64-flip.log`). The flipped build was not installed
in a guest: the legs above ran at 1 through the machine-wide value, the
same reading the INF default now writes.

**README** (the owner's extra item): "Things to know" and "Known
limitations", Thunderbolt and USB4, narrowed from "not supported" to what
was read: standby and hibernation on 32-bit 7, 8.1 and 10 with an ASM4242
host, standby alone on 32-bit 10 with an Alpine Ridge controller, and
"stop and unplug" kept for everything else (98 SE, ME, 2000, XP, Vista, 8,
the x64 editions, Titan Ridge, the integrated hosts, other USB4 hosts).
`docs/using/release-notes.md` describes `3.4.2.0` and was left (48.final);
`tuning.md` moved with the default.

Not read, owed: section 11.1.12's S.1 to S.3, S.5, S.6, S.8 to S.10 (98 SE,
ME, 2000, XP, the chain, the XP x64 / 7 x64 stall), the x64 half,
`xhcimark`'s flush, and the bench.

Evidence: `E:\xhci98-vm\p48\tbs2\` (`w7\`, `w81\`, `w10\`: `serial-*.log`,
`cmdline-*.txt`, `leg-*.txt`, `wake-*.txt`, `pids.txt`, screens), the
scripts in `tools\` (`go.ps1`, `boot.sh`, `adm10.sh`, `seq7.sh`,
`seq10.sh`, `leg.sh`, `wake.sh`, `kclick.sh`, `k7.sh`, `ps2to.py`,
`pix.py`), `xfer\G.CMD`, `xfer\ONE.CMD`; every QEMU start and stop in
`pids-all.txt`.

### Merge (w48-tbs2)

Merged into `3.4.3.0` at `1b73216e` (2026-11-10). The owner had decided the
flip in advance: 1 if every stand-by and hibernation leg passed on 7, 8.1
and 10 at 1 with nothing regressing at 0. The merge review found the
README lines claimed more than was read: every leg had the enclosure
attached at power-on with the pre-OS connection manager on, and the native
hot-plug case of section 11.1.8 still loses the drive. Both lines now say
so (`8043a417`), reviewed, nothing material; the integration build of
`8043a417` passed.

## 48.33 Every transfer staged through buffers the driver owns: the legs and the measurement (2026-11-09, worker pci4)

Build under test: branch `w48-pci` at `74c59858` (the gated x86 and amd64 `all` builds of the run above), unchanged here: release `xhci98.sys` `69490C6C...0067` (356,288 bytes; "after"), qemu `3D538ACD...ADD8C1` (422,021 bytes); "before" = `xhci98.sys` `31800DAD...B5FE` (349,072 bytes, `46397ba0`, the map-register route). Kits: `E:\xhci98-scratch\w48\pci4\k86` (the family layout of `out\pkg-release-x86`, the three `xhci98.sys` side by side, XHCIPERF `92837b57...`, a 24 MiB random `BIG.BIN`, the batches; `USBDRV\NUSB33\` from the `3.4.2.0` download for 98 SE's storage drivers), `k2` (the same and a WAV). Emulator: the pinned q35 build (`asm4242-20261025-2230`); QMP over a named pipe (the worker's port range lies in Windows' excluded ranges). Each USB disk an overlay of the shared 128 MiB FAT image.

**The unproven stop with requests pending (7 x86).** Overlay of the first leg's disk (qemu flavour and `XhciQemuStageFault` 1 on the first controller), now with `-Usb4 tb3ar` and a QEMU usb-storage disk on the Alpine Ridge xHCI (`usb4-tb3-xhci`, 8086:15C1), plus the usb-storage and usb-uas disks on qemu-xhci. Windows bound the Thunderbolt xHCI to xHCI98 from the driver store, which put the release image back; the qemu image was copied over it again and `XhciQemuStageFault` 1 written to both controllers' keys (`\0000`, `\0004`), restart. Three XHCIPERF runs at once, unbuffered sequential 1 MiB writes at depth 4, one per disk; then QMP `qom-set tbrp tunnel surprise` (this emulator has no `present` property; `tunnel` `surprise` is its surprise removal). Read: the controller's commands timed out, reinitialisation refused (CAPLENGTH 0), PnP surprise removal and removal of its root hub and disk, the stop logged `quiesce: PCI command reads all ones - DMA not proven stopped` and `teardown: DMA NOT PROVEN STOPPED ... failing closed` (so `CommonBufferPinned`, every DMA block kept), then `release: DMA` and `release: BAR` and the removal completed (minor 2). The disk behind it: XHCIPERF "The disk stopped answering (error 2)" in its third pass, the run ended - the pending requests completed with an error, none held. The other two disks ran all nine passes on (UAS 302.7 MB/s read best, BOT 82.6). Windows went on running, no device left with a problem but ACPI\QEMU0002. Then the last controller (qemu-xhci) removed by QMP `device_del` after its volumes were dismounted: no xHCI98 device was left and `sc query xhci98` still read RUNNING - the image stayed loaded with no device, as a cleared `DriverUnload` makes it (`hcdLedgerKeep`); a controller added again (`device_add qemu-xhci` on the same root port) started on the loaded image (OK, its root hub OK). Not read: the `stage.*` and `dma.quarantine.*` notes, which go to the controller's in-memory log (`XhciLogNote`), not the 0xE9 channel, and leave with the removed controller. Evidence: `E:\xhci98-vm\p48\pci4\u7\yank.log`, `serial-leg-unproven.log`, `a*.png`, `pids.txt`. **Pass.**

**98 SE** (fresh overlay, 1 processor; the base leaves qemu-xhci at "PCI Universal Serial Bus", Code 28): `INSTALL.BAT /console /yes /nopause /nostor98 /storage` (exit 4: the Device Manager step), then Device Manager, Reinstall Driver, files from `C:\XHCI98`: "xHCI98 USB 3 Controller (Red Hat QEMU xHCI)", restart. The BOT stick hot-added (QMP `device_add`; none at power-on on 9x): the Add New Hardware wizard found `USBSTOR.INF` (NUSB33), drive E:. The usb-uas disk never enumerated on 98 SE (no `Enum\USB` entry for it, re-plugged twice): UAS on 98 SE not read here. Copy-compare (24 MiB, copy to E: and back, `fc /b`): 4 rounds PASS (rounds 1-3, then a fourth after the hot unplug and re-plug). Hot unplug: QMP `device_del` of the stick during an unbuffered XHCIPERF write: XHCIPERF "The disk stopped answering (error 31)" in pass 7, no hang; re-plugged (`drive_add` + `device_add`), the wizard ran again for the new port, then copy-compare PASS. Disable/enable: not taken. Audio: none (QEMU usb-audio faults 98 SE's own USBAUDIO in this emulator, memory of Phase 9). Evidence: `E:\xhci98-vm\p48\pci4\s98\serial-*.log`, `b*.png`.

**2000** (fresh overlay, 1 processor, BOT stick + UAS disk + usb-audio at power-on): `INSTALL.BAT /console /yes /nopause /nostor98`, the Found New Hardware windows answered (xHCI98 USB Attached SCSI Storage among them), exit 3 "Done. Please restart Windows now."; a warm restart hung in the boot loader (real mode 2000:0D4F, not this driver's), cold start: both disks mounted (E: UAS, F: BOT, read by unplugging the stick), the audio device installed. Copy-compare 3 rounds PASS on E: and F:. Hot unplug of the stick during copies: Windows' "Unsafe Removal of Device" box, nothing else; re-plug. Audio beside: **not achieved** - Sound Recorder refused both a 22.05 kHz mono and a 48 kHz stereo 16-bit file ("Your audio hardware cannot play files like the current file") on the usb-audio device, so the 2000 numbers below have no isochronous stream beside them. Disable/enable: not taken (WDK 7.1 devcon does not start on 2000). Evidence: `E:\xhci98-vm\p48\pci4\w2k\serial-*.log`, `c*.png`.

**The measurement** (XHCIPERF, unbuffered sequential, `/size 32 /passes 3 /time 8 /nolatency`, 1 MiB and 64 KiB at depth 1, one thread; three runs a set; each run's figure = all its passes' bytes over their time; the table gives the median run; same overlay, same disk images, only `xhci98.sys` swapped and a cold start between sets). Emulated (TCG) and the host busy with other workers' guests: relative only.

| System, disk | Test | after (staging) | before | after vs before |
|---|---|---|---|---|
| 98 SE, BOT stick | seq 1 MiB read | 49.8 (6 runs: 50.8 48.8 45.4 45.2 56.7 54.1) | 60.7 (58.6 60.7 60.7) | -18% |
| 98 SE, BOT stick | seq 1 MiB write | 29.9 (30.9 28.8 28.1 31.0 27.4 31.4) | 32.8 (33.6 32.8 31.6) | -9% |
| 98 SE, BOT stick | seq 64 KiB read | 34.8 (31.1 33.1 33.0 36.4 37.0 38.8) | 41.5 (41.5 44.3 39.0) | -16% |
| 98 SE, BOT stick | seq 64 KiB write | 18.9 (18.0 17.8 17.6 19.8 23.0 22.6) | 23.4 (22.8 23.4 24.2) | -19% |
| 2000, UAS (E:) | seq 1 MiB read | 504.2 (479.3 504.2 517.7) | 555.6 (595.2 555.6 476.4) | -9% |
| 2000, UAS (E:) | seq 1 MiB write | 394.9 (573.1 394.9 389.4) | 581.5 (603.2 581.5 471.2) | -32% |
| 2000, UAS (E:) | seq 64 KiB read | 157.4 (133.6 157.4 167.2) | 122.3 (124.6 122.3 116.5) | +29% |
| 2000, UAS (E:) | seq 64 KiB write | 149.6 (132.5 158.7 149.6) | 134.7 (135.3 134.7 108.9) | +11% |
| 2000, BOT (F:) | seq 1 MiB read | 235.5 (243.3 235.5 215.9) | 178.6 (184.9 145.4 178.6) | +32% |
| 2000, BOT (F:) | seq 1 MiB write | 217.0 (243.1 217.0 203.7) | 160.3 (163.2 132.3 160.3) | +35% |
| 2000, BOT (F:) | seq 64 KiB read | 182.3 (183.9 182.3 160.8) | 104.4 (104.4 104.0 144.7) | +75% |
| 2000, BOT (F:) | seq 64 KiB write | 162.9 (162.9 166.5 161.8) | 109.8 (109.8 90.6 120.2) | +48% |

MB/s, XHCIPERF's 1,000,000-byte unit. 98 SE: sets in the order after, before, after, each beside the 2000 guest; a fourth set (before, 32.4 / 27.3 / 18.4 / 12.5) ran beside a Windows 10 guest's start and is left out as a host-load artefact (every figure fell by a third or more, the before build included). 98 SE ran at 100 % processor in every test (one emulated processor; the copy is processor work), so staging's copy shows directly: **on 98 SE staging costs more than 15 % sequential in three of the four tests (-16 % to -19 %), 9 % on 1 MiB writes.** 2000: one set each way (after, then before), no audio beside (above); the spread between runs of one set reaches 30 %, and the sign differs by disk and size (the BOT stick faster after, UAS 1 MiB writes slower after), so 2000 shows no cost that stands out of this emulator's noise; 7 x64 not measured. The 2000 "after" set read faster at 64 KiB and on BOT, which fits the larger TDs staging builds (chunks up to 1 MiB against the map-register route's 128 KiB grants) but is not proven by one set.

Owed for 48.33: 7 x64, XP and ME legs (the amd64 package is staged unsigned at `E:\xhci98-scratch\w48\pci4\pkg-release-x64` from the gated `74c59858` binaries; no amd64 "before" binary exists, so 7 x64's comparison needs one built at `46397ba0`); UAS on 98 SE; audio beside on 2000; disable/enable on each system; 98 SE's reading of `MmMapLockedPages`' failure (no probe built).

## 48.27 Q27: INSTALL.BAT again after the repair installation (2026-11-09, worker pci4)

Overlay of pci's repair-installed 10 x86 disk (`E:\xhci98-vm\p48\pci\q27\disk.qcow2`, left untouched), the same 4G-off layout and command line (`-Usb4 asm4242 -AsmReserve4gOff -EncAtBoot -Tablet`, emulator `asm4242-20261026-4goff`), the same kit (a copy of pci's `x10`, `46397ba0`). Read first (QQ.BAT): as pci read it after the repair - build 19045.2965, no `xhcipff` service, no `UpperFilters` on ACPI\PNP0A08\0, the installer's record (File `C1C2C511...47E6`) and `xhcipff.sys` (13,672 bytes) still there, root port 00:03.0 (`...&18`) at problem 12. The rerun: `L1.BAT` from the Run box ran at medium integrity, so its `> C:\inst1.log` could not be created and `INSTALL.BAT` never started (the batch then typed out the first install's `inst1.log`, dated 13:28, which reads like a fresh run: beware); its `shutdown /r` restarted Windows; nothing had changed (QQ again: as before). An elevated start (`Start-Process cmd -Verb RunAs`) met a consent prompt this session could not see in the screendump or answer. **Not read: owed** - the rerun from an elevated console (or with the kit's batch writing its log where a standard user may write), then QQ after the restart. Evidence: `E:\xhci98-vm\p48\pci4\q27b\serial-p4d.log`, `serial-rerun.log`, `r*.png`. Record 32 section 11.5.6's other 10 x86 legs and the 8.1 previews: not started.

## 48.33 XhciDmaStaging, the 98 SE mapping probe, and the legs (2026-11-09, worker pci5)

Branch `w48-pci5` from `w48-pci4` (`74c59858`, 48.33's code). Commits: `be41fa47` (the qemu probe, first form), `b673deb4` (XhciDmaStaging, and the probe holding one window MDL a mapping).

**XhciDmaStaging** (owner's requirement of 2026-11-09, passed on by the coordinator): read once at each start (`hcd_ctl.c`, `stage.switch` noted, `hcd: XhciDmaStaging, effective` on the 0xE9 channel), written by both INFs at 1 with `0x00010003` (NOCLOBBER), absent or anything but a DWORD 0 is 1 (`xhci_stage.c`, `XhciStagingOn`). At 0 every transfer goes direct as in 3.4.2.0: the map pump, `hcdWideCheck`, promotion and `MapQuarantined` restored in `hcd_dma.c` from `46397ba0`; `hcdPlanChunk`, `hcdWideClaim` and `HcdPoolMdlBuild` (with the `MmBuildMdlForNonPagedPool` rows) back; every staging call site branched on `hc->Staging`; the quiesce reads all ones as a function off the bus again (`XhciStageCommandProof`); an unproven stop keeps its devices' records and the clients' requests (the old wait: `HcdIoDeviceDrain` returns 0, `HcdEnumContain` drains, the PDO quiesce stops waiting, no `HcdIoParkCloseAll`). INF gate row (`XhciDmaStaging`, 1), footprints (5 rows), INF self-test vectors (two tables, the value block), host vectors in `test_stage` (`test_switch`, `test_proof`), tuning.md and build-and-test.md rows, record 13 section 11.11 ("The switch").

**The probe** (`XhciQemuStageMapProbe`, qemu only, written by no INF): the first window copy of a start maps its span through that many window MDLs of the probe's own (at most 32,768), none unmapped, until a mapping returns NULL; prints `stage.probe: maps held`, the outcome (1 NULL, 2 MDLs ran out) and the first and last addresses on 0xE9, notes `stage.probe.*`, then unmaps each through its own MDL. The first form (`be41fa47`) rebuilt one MDL for every mapping; reviewed and revised in `b673deb4`.

Review: the switch and the probe reviewed against `74c59858` (and the direct path against `46397ba0`): approved, no findings.

Builds: gated x86 `all` (`b673deb4`, slot gated.2, `XHCI98_SELFTEST_SKIP_OK=1`): **BUILD + GATES PASSED** (release `xhci98.sys` 361,921 bytes; qemu `F29D7888...78152`, 428,982 bytes; 17:47); gated amd64 `all` (`b673deb4`): **BUILD + GATES PASSED**. Copies in `E:\xhci98-scratch\w48\pci5\bin86`.

## 48.27 Q27: INSTALL.BAT again after the repair installation, elevated (2026-11-09, worker pci5)

Overlay `E:\xhci98-vm\p48\pci5\q27c` of pci's repair-installed 10 x86 disk (`E:\xhci98-vm\p48\pci\q27\disk.qcow2`, left untouched), pci4's command line (`-Usb4 asm4242 -AsmReserve4gOff -EncAtBoot -Tablet`, emulator `asm4242-20261026-4goff`), kit `E:\xhci98-scratch\w48\pci4\kq27` (`46397ba0`). QEMU pid 168244 (22:50:53 to the QMP powerdown at 23:35). The elevated console: Run box, `cmd`, Ctrl+Shift+Enter, then Alt+Y at the consent prompt, which the screendump does not show (the secure desktop) but the keyboard reaches; the window read "Administrator: C:\WINDOWS\system32\cmd.exe". `L1.BAT` from it at 22:58:49. The log read is the new one: `C:\XHCIINST.LOG` 25,135 bytes dated 23:00, its run headed `==== xhciinst 3.4.2.0, 2026-11-09 22:58:52 ====` (the first install's run of 13:14 to 13:28 above it); `C:\inst1.log` 0 bytes at 22:58 (written only at the batch's end). The run: the root port `...&18` at problem code 12 before; the filter added to both PCI Express Root Ports (`UpdateDriverForPlugAndPlayDevices` ... "done, restart needed"); `xhcipfw: C:\WINDOWS\system32\drivers\xhcipff.sys deleted`, the record cleared and written again (`File=C1C2C511...47E6`), `service Start 0`, `xhcipff added to the UpperFilters of ACPI\PNP0A08\0`; "xHCI98 PCI Bridge: pci 3.4.2.0, problem code 12 ... was not installed" (before the restart), "RESTART Windows to finish", exit 2 at 23:00:36. INSTALL.BAT then waited at "Press Enter to close this window." (L1 passes no `/nopause` and its output goes to the log, so the prompt is invisible); the guest restarted by `shutdown /r /t 5` from a Run box (the batch's own exit reads 1073807364, the restart ending it). QQ.BAT after the restart: build 19045.2965 (UBR 0xb95); `xhcipff` Start 0, group PnP Filter, `XhciPfwMode` 2, `XhciPfwRole` 2, `XhciPfwResultAny` 1, `XhciPfwClosedAll` 9, `XhciPfwVersion` "XHCIPFW.SYS 3.4.2.0"; `UpperFilters` = `xhcipff` on ACPI\PNP0A08\0; `xhcipff.sys` 13,672 bytes; every PCI device at problem 0, root port `...&18` and `...&E0` included, the ASM4242 functions (1B21:2463, 2421, 2423 x4, 2425, 2426) all started; the only device with a problem ACPI\QEMU0002 (28). **Pass: INSTALL.BAT run again after a repair installation puts the filter back, and after one restart the root port that read problem 12 starts.** Evidence: `E:\xhci98-vm\p48\pci5\q27c\serial-L1-full.log`, `serial-QQ.log`, `s*.png`, `pids.txt`.

## 48.33 98 SE: the copy-compare with XhciDmaStaging 0 and 1, and the MmMapLockedPages probe (2026-11-09/10, worker pci5): not read

Overlays (`E:\xhci98-vm\p48\pci5\s98`) of pci4's installed 98 SE disk and stick (`E:\xhci98-vm\p48\pci4\s98\disk.qcow2`, `stick.qcow2`, both left untouched), 1 processor, the pinned q35 emulator; kit `E:\xhci98-scratch\w48\pci5\k5` (pci4's batches, `BIG.BIN`, `QEMU5.SYS` = this branch's qemu image, `REL5.SYS`, pci4's `AFTER.SYS` and `BEFORE.SYS`). The controller's key is `Class\USB\0000`; the values were merged with a `.REG` typed at a DOS prompt (`start /w regedit /s`; COMMAND.COM does not wait for regedit without `start /w`, and the kit's vvfat drive is read once at boot, so no file can be added to it while the guest runs).

- **QEMU5, XhciDmaStaging 0** (pid 138344, then 159404): read on 0xE9 `hcd: XhciDmaStaging, effective=00000000`, `start, map registers granted=00080001`; the stick hot-added (E:, ORICO); C.BAT **3/3 FAIL**. By hand: `copy /b` C:\BIG.BIN (25,165,824 bytes) to E: said "1 file(s) copied" but the file on E: was 21,196,800 bytes ("C:\BIG.BIN longer than E:\X.BIN"); nothing but zero counters in the trace.
- The next restart was made with `rundll32 shell32.dll,SHExitWindowsEx 2`: the start after it met Registry Checker's "You Have Restored a Good Registry", which put back a registry from before xHCI98's install (no `Class\USB` key at all). Overlays discarded and made again from pci4's (pid 168076, 170328 ended).
- **Control: pci4's AFTER (`74c59858`, release, staging; it passed 4/4 for pci4 on this disk)**, fresh overlays (pid 160824): C.BAT **3/3 FAIL** too, each round in about a minute. `fc /b D:\BIG.BIN C:\BIG.BIN` then printed differences without end (the kit's copy and the one on C: differ), and the guest sat in that pipe; quit.

So the failure is in this session's environment (the host at 88 % processor with ten QEMUs; the disk lineage or the vvfat read are the suspects), not in the switch: the build that passed for pci4 fails the same way. **Owed:** the copy-compare with 0 and 1 on a fresh 98 SE install (not pci4's overlay) at a quieter time, and the probe (`XhciQemuStageMapProbe` 32768 with staging 1), which needs that same working copy path; whether 48.33's fallback handles NTKERN's failure is therefore not read. Restarts on 98 SE end QEMU (`-action reboot=shutdown`), so each change costs a cold launch; use the Start menu's Restart, never the rundll32 call. Evidence: `E:\xhci98-vm\p48\pci5\s98\pids.txt`, `old1\`, `serial-p5s.log`, `a*.png`, `b*.png`.

## 48.33 7 x64, XP and ME legs; 10 x86 legs of record 32 section 11.5.6: not started

QEMU pids this run: 168244 (q27c), 138344, 159404, 168076, 170328, 160824 (s98); all ended (pids.txt in each directory). No VM left running.

## 48.33 98 SE: the short copy settled, the copy-compare with XhciDmaStaging 0 and 1, the hot unplug and the mapping probe (2026-11-10, worker pci6)

Builds under test, unchanged from pci5's run: `b673deb4` release `xhci98.sys` `3234AC5E...2224` (361,921 bytes; "REL6"), qemu `F29D7888...78152` ("QEMU6"); the control is the `3.4.2.0` release's own `98-me\xhci98.sys` `6575E65E...5EDF` (349,083 bytes; "R342"; no `xhci98.sys` source differs between `v3.4.2.0` and `46397ba0`, `git diff --stat` touching only `src/pfw`). Kit `E:\xhci98-scratch\w48\pci6\k6`: the `3.4.2.0` download unpacked (its `INSTALL.BAT`, `98-me\`, `tools\`, `USBDRV\NUSB33\`), the same 24 MiB `BIG.BIN` as pci4's and pci5's kits (`F24533C4...7F05`), the three images and pci4's batches; `C2.BAT` now also prints the size of each copy on the stick, `V.BAT` copies `BIG.BIN` to C: and compares it with the kit's, and `.REG` files set `XhciDmaStaging` and `XhciQemuStageMapProbe` in `Class\USB\0000` (the controller's key, read with `K.BAT`). The pinned q35 emulator, 1 processor, QMP on TCP 47960 (the named-pipe relay of pci5's kit crashed on a file race once and the pipe takes one client for its life, so that guest, pid 178104, was ended by its PID after its boot; ScanDisk ran at the next start and found nothing to report).

**The short copy was the disk, not the driver.** pci5's guests ran on overlays of pci4's installed disk (`E:\xhci98-vm\p48\pci4\s98\disk.qcow2`). Read offline (`qemu-img convert` to raw, the FAT32 volume walked by a script of the worker's, `E:\xhci98-scratch\w48\pci6\tmp`): on that disk `C:\BIG.BIN` (25,165,824 bytes, 6,144 clusters of 4 KiB) has its chain broken in the first FAT after 5,175 clusters - the next entry reads 0 - while the second FAT holds the whole chain; the two FATs differ in 979 entries, all in sectors LBA 768 to 776 of the first FAT, which read zero there (8 of the 9 sectors entirely). MS-DOS reads the first FAT, so `copy` read 5,175 clusters = **21,196,800 bytes**, exactly the short file pci5 saw on the stick every time, with any build; `fc /b` of the kit's `BIG.BIN` against `C:\BIG.BIN` differed for the same reason. The stick images were clean (one `TESTDATA.BIN`, free space as the base's). When the first FAT was damaged is not read; pci4's session records one unclean stop of that disk (pid 140468, killed at 21:56 after a Shut Down that did not finish), and its earlier rounds passed.

**Fresh overlay of the clean base** (`E:\xhci98-vm\q35-bases\w98se\disk.qcow2`, `AB1D4D65...`; `E:\xhci98-vm\p48\pci6\s98`), host at 76 to 87 % processor with 8 to 9 QEMUs (other workers' and mine), stick a fresh overlay of the shared 128 MiB FAT image, hot-added with QMP `device_add usb-storage` (Add New Hardware wizard, `C:\WINDOWS\INF\USBSTOR.INF`, E:):

| Step | Build, switch | Result |
|---|---|---|
| `V.BAT` before any USB driver | - | kit `BIG.BIN` and the copy on C: "same" (`fc /b`); read offline after the Shut Down: `C:\BIG.BIN` SHA-256 `F24533C4...7F05` = the host's file, FATs equal - the kit's vvfat drive delivered it intact |
| `INSTALL.BAT /console /yes /nopause /nostor98 /storage` | `3.4.2.0` package | "installed, restart pending", exit 3; Shut Down, cold start |
| Control copy-compare (`C.BAT`, copy to E: and back, `fc /b`) | R342 | **3/3 PASS**, each copy 25,165,824 bytes on E: |
| Copy-compare | REL6, `XhciDmaStaging` 0 | **3/3 PASS** |
| Hot unplug (QMP `device_del` of the stick 30 s into a loop of eight `copy /b` to E:) | REL6, 0 | VFAT's "Disk Write Error ... Unable to write to disk in drive E:", a key, the loop ran out ("1 file(s) copied" each), no hang; re-plug (`drive_add` + `device_add`): E: back, `BIGU.BIN` 0 bytes and the interrupted write's clusters lost on the stick (ScanDisk's business, not xHCI98's) |
| Copy-compare after the re-plug | REL6, 0 | **3/3 PASS** |
| Copy-compare | REL6, `XhciDmaStaging` 1 | **3/3 PASS** |
| Hot unplug as above | REL6, 1 | the same: "Disk Write Error", the last copy "Invalid drive specification", no hang; re-plug, E: back |
| Copy-compare after the re-plug | REL6, 1 | **3/3 PASS** |
| Copy-compare | QEMU6, 1, `XhciQemuStageMapProbe` 32768 | **3/3 PASS**; 0xE9 `hcd: XhciDmaStaging, effective=00000001`, `start, map registers granted=00080001` |

After every Shut Down the disk was read offline: the two FATs equal and `C:\BIG.BIN` `F24533C4...7F05` each time (five reads). So with the 98 SE disk sound, staging off and on both pass, as `3.4.2.0` does.

**The probe: not reached on 98 SE.** With the probe armed (`XhciQemuStageMapProbe` 32768, the value read back from `Class\USB\0000`), neither the three copy-compare rounds nor an unbuffered XHCIPERF run on E: (`/test seq,1024,1,1`, 43.7 MB/s write, 80.6 read) printed any `stage.probe:` line: on 98 SE every data buffer `USBSTOR.SYS` (NUSB33) hands the bus has a system address (`hcdStageDirect`), so no copy goes through a window and `MmMapLockedPages` is never called on this path. What NTKERN does when it cannot map therefore stays unread, and 48.33's fallback for a window that will not map (the TD back to `WAITING`, the retry timer) is not exercised on 98 SE by mass storage. Reaching it needs a 98 SE client that passes an MDL without a system address (UAS, which pci4 could not enumerate on 98 SE, or another class driver).

Evidence: `E:\xhci98-vm\p48\pci6\s98\pids.txt`, `serial-p6s.log` (last session), `debugcon-p6s.log`, `a*.png`, `b*.png`, `c*.png`; the offline reads in `E:\xhci98-scratch\w48\pci6\tmp` (`fatchk.py` in the worker's scratchpad, its output quoted above). **Pass** (copy-compare and hot unplug, both switch settings); probe not reached.

From the roadmap (2026-11-10): the copy's cost measured under emulation, 98 SE 9 to 19 %, 7 x64 up
to 73 %; the default revisited after the bench. 98 SE's mapping failure (left): its storage path
never maps a window.

## 48.33 ME quick leg (2026-11-10, worker pci6)

Fresh overlay of the ME base (`42FEA19A...`; `E:\xhci98-vm\p48\pci6\me`), kit k6, 1 processor, QMP 47962. `V.BAT`: kit and C: copies "same". `INSTALL.BAT /console /yes /nopause /nostor98` (`3.4.2.0`): "installed, restart pending", exit 3; REL6 copied over `xhci98.sys` before the restart (`fc /b` no differences), Shut Down, cold start; `XhciDmaStaging` absent (= 1). The stick hot-added: ME's own driver, E: at once (Explorer opened it). Copy-compare **3/3 PASS**; hot unplug during a copy loop: "Disk Write Error" (twice), the loop ran out, no hang; re-plug, copy-compare **3/3 PASS**. After the Shut Down: FATs equal, `C:\BIG.BIN` `F24533C4...7F05`. Evidence: `E:\xhci98-vm\p48\pci6\me\pids.txt`, `serial-p6m.log`, `a*.png`. **Pass.**

## 48.33 7 x64: the copy-compare, the hot unplug, the unproven stop with requests pending, the mapping fallback, and the measurement against 3.4.2.0 (2026-11-10, worker pci6)

Fresh overlay of the 7 x64 base (`B40BAA38...`, test-signing on; `E:\xhci98-vm\p48\pci6\w7`), 2 processors, 2 GB, QMP 47961, a BOT stick and a UAS disk on qemu-xhci at power-on. Kit `E:\xhci98-scratch\w48\pci6\k7`: the `3.4.2.0` download's `nt-x64\` and `tools\`, the gated `b673deb4` amd64 images (release `E7E8A055...6EBF` "REL6", qemu `AE893726...79C8` "QEMU6"; unsigned, this clone holds no key), the release's own `nt-x64\xhci98.sys` as "R342" (`C299D294...CCAF`; it stands for the "before" build: no `xhci98.sys` source differs between `v3.4.2.0` and `46397ba0`, so no amd64 build at `46397ba0` was made), and WDK 7.1's amd64 `MakeCert`, `CertMgr` and `SignTool`. Signing in the guest only, the brief's route: the kit copied to `C:\KIT`, `SIGN7.BAT` (after net2's `SIGN.BAT`) made a throwaway "CN=xHCI98 leg throwaway pci6" (01/01/2026 to 01/01/2030), trusted it as root and publisher, and signed every `.sys` and `.cat` of `nt-x64\` and the three images (all "Successfully verified"); no project or owner key used. Install: `INSTALL.BAT /console /yes /nopause /nostor98` from an elevated console (Run box, `powershell -c start-process cmd -verb runas`, Alt+Y at the consent prompt; the base's keyboard is Dvorak): controller, root hub and "xHCI98 USB Attached SCSI Storage" started, exit 3. Each image put in place by copying it over `drivers\xhci98.sys` (`SW7.BAT`, the loaded file's SHA-256 read each time) and a restart. 7 sets the second 128 MiB disk offline (both overlays of one image, one disk signature); `diskpart` `online disk` gave it F:. D: = BOT (`USBSTOR`), F: = UAS (`XHCIUAS`).

| Leg | Build | Result |
|---|---|---|
| Copy-compare, 24 MiB to D: and F: and back, `fc /b` | REL6 (staging) | **D: 3/3, F: 3/3 PASS** |
| Hot unplug: QMP `device_del` of the BOT stick 12 s into a loop of 80 `copy /b` to D: | REL6 | the loop ran on, "The system cannot find the drive specified.", no hang; re-plug (`drive_add` + `device_add`), D: back, a round **PASS** (a first try came after a shorter loop had ended and is not counted) |
| Copy-compare with 16 window mappings made to fail (`XhciQemuStageFault` 0x1001 on both controllers' keys: bits 15:8 = 16 failures, bit 0 = an unproven stop) | QEMU6 | D:, F: and the Thunderbolt disk **PASS**; XHCITR64 `/snapshot`: `qemu.stage.fault=00001001`, `stage.windows=00000004`, `xfer.stage.giveup=00000001` - on NT the window path is used, the failures were retried from the timer, and one request reached record 13 section 11.11.4's 5 s give-up (16 consecutive failures at 10 ms doubling to 1 s outlast it) and was ended `USBD_STATUS_NO_MEMORY`; the storage stack retried it and the copy still compared equal |
| The unproven stop with requests pending: `-Usb4 tb3ar`, a usb-storage disk on the Alpine Ridge xHCI (`8086:15C1`, bound to xHCI98 from the driver store at the first start; QEMU6 copied over again, restart), XHCIPERF unbuffered 1 MiB writes at depth 4 on all three disks, QMP `qom-set /machine/peripheral/tbrp tunnel surprise` | QEMU6 | 0xE9: `init REFUSED at step=00000004`, `recover: reinitialization refused`, `quiesce: PCI command reads all ones - DMA not proven stopped` (three), `quiesce: DMA NOT PROVEN STOPPED`, `teardown: DMA NOT PROVEN STOPPED ... failing closed`, `release: DMA`, `release: BAR`, PnP minor 2 for the device PDO, the root hub and the controller. The Thunderbolt disk's XHCIPERF: "The disk stopped answering (error 2 ...). The run was stopped." in pass 6 - its pending requests completed with an error, none held; the UAS and BOT runs did all nine passes. Windows went on; afterwards only qemu-xhci's controller and root hub, both OK, nothing with a problem but `ACPI\QEMU0002`. A first removal of the same kind came after the runs had ended (no I/O in flight) and read the same teardown. **Pass** |

Disable/enable: not taken (no amd64 devcon in the tree).

**The measurement** (XHCIPERF `/size 32 /passes 3 /time 8 /nolatency`, seq 1 MiB and 64 KiB at depth 1; each run's figure = all its passes' bytes over their time; a set = three runs a disk, the median given; same overlay and disks, only `xhci98.sys` swapped and a restart between sets). Five sets in the order B1, A1, A2, B2, A3 (B = R342, A = REL6), MB/s:

| Disk, test | B1 | A1 | A2 | B2 | A3 |
|---|---|---|---|---|---|
| BOT, 1 MiB read | 222.5 | 150.9 | 207.6 | 356.1 | 149.3 |
| BOT, 1 MiB write | 73.7 | 61.0 | 80.6 | 91.6 | 62.5 |
| BOT, 64 KiB read | 144.2 | 91.7 | 153.5 | 198.2 | 100.1 |
| BOT, 64 KiB write | 54.7 | 47.3 | 66.6 | 69.5 | 47.8 |
| UAS, 1 MiB read | 634.9 | 278.4 | 395.9 | 963.4 | 264.5 |
| UAS, 1 MiB write | 152.4 | 111.2 | 143.8 | 187.3 | 108.5 |
| UAS, 64 KiB read | 146.0 | 108.1 | 156.6 | 194.1 | 111.0 |
| UAS, 64 KiB write | 56.1 | 46.3 | 69.8 | 75.1 | 50.2 |

The host was shared (other workers' guests, 54 to 87 % processor) and moved a set's figures by up to half between neighbours (A1 ran beside this worker's 98 SE and ME copies; A2, B2 and A3 with no other guest of this worker's). Read only side by side: every "before" set is faster than the "after" set next to it, B2 against A2 and A3 by 4 to 49 % on BOT writes and 64 KiB transfers, 42 to 58 % on BOT 1 MiB reads, 7 to 43 % on UAS writes and 64 KiB transfers, and 59 to 73 % on UAS 1 MiB reads, where the copy at emulated speed bounds the rate. **On 7 x64 in this emulator staging costs more than 15 % in most tests against the set beside it, up to 73 % on UAS 1 MiB reads**; how much of that a real processor's copy leaves is the bench's reading. Evidence: `E:\xhci98-vm\p48\pci6\w7\pids.txt`, `serial-set*.log` (each set's reports), `serial-p6u.log`, `dc-yank1.log`, `dc-yank2.log`, `a*.png`, `u*.png`, `v*.png`; `E:\xhci98-scratch\w48\pci6\perf.py`.

## 48.33 XP quick leg (2026-11-10, worker pci6)

Fresh overlay of the XP base (`64ADA103...`; `E:\xhci98-vm\p48\pci6\xp`), 2 processors, a BOT stick and a UAS disk at power-on, kit `E:\xhci98-scratch\w48\pci6\kx` (the `3.4.2.0` download's `nt-x86\` and `tools\`, REL6 = the `b673deb4` x86 release image). `INSTALL.BAT /console /yes /nopause /nostor98`: three "has not passed Windows Logo testing" questions answered Continue Anyway, two Found New Hardware wizards cancelled; controller, root hub and "xHCI98 USB Attached SCSI Storage" started, exit 3. REL6 copied over `xhci98.sys` (`fc /b` no differences), restart. Copy-compare **D: 3/3, F: 3/3 PASS**; hot unplug of the BOT stick 15 s into an 80-copy loop: the loop ran on ("cannot find the drive specified"), no hang; re-plug, a round **PASS**. Evidence: `E:\xhci98-vm\p48\pci6\xp\pids.txt`, `serial-p6x.log`, `a*.png`, `b*.png`. **Pass.**

## 48.27 record 32 section 11.5.6's other 10 x86 legs: not started (worker pci6)

QEMU pids this run (all started and stopped by this worker, each in its folder's `pids.txt`): 98 SE 161056, 172848, 178104 (ended by its PID), 155484, 174812, 42496; ME 177736, 179276; 7 x64 176252, 177680, 189760; XP 188016. No VM left running.

### Merge (w48-pci5, 48.33)

Merged into `3.4.3.0` at `f372fa2f` (2026-11-10), the INF gate's conflict
resolved by hand (issue 29's `XhciSsFlapFallback` row before 48.33's
`XhciStagePoolKiB` and `XhciDmaStaging`, as `src\xhci98.inf` has them; the
gate's self-tests passed, 1809 checks); the merge reviewed, nothing
material; the integration build of `f372fa2f` passed. The owner's answers
of the day: the 98 SE cost accepted, with a bench check before and after;
the staging gated by `XhciDmaStaging`, 1 by default, revisited after the
bench's speed checks.

## The eject's wait (2026-11-10)

Worker ejw, branch `w48-ejw` from `411b55a5`. From stor3's 8.1 x86 leg
(48.29 section above): `XHCITR /eject id=<enclosure A's upstream switch
port>` answered "... The eject was confirmed to the Thunderbolt driver.
The Thunderbolt driver has not confirmed that the PCIe tunnel was
closed.", and about 20 s later the enclosure's functions had left the
emulator's `info pci`, read then as the tunnel closing after the tool's
wait. The drive's own instance ID was refused ("No USB device, and no
device behind a Thunderbolt port, has the instance ID ...").

**Machine.** A fresh `w81x86` q35 overlay (`launch-q35.ps1 -Usb4 asm4242
-EncAtBoot -Tablet`: the ASM4242 model, the ASM2464 enclosure `enc0`
with the FAT NVMe on USB4 port 1 at power-on), 2 vCPUs, TCG; stor3's
x86 kit of `d0e3eea0` (`INSTALL.BAT /console /yes /nopause /thunderbolt
/stor98`, exit 3, the unsigned-driver prompts clicked, a restart;
Stor98 on the drive, `xhcimark` running). No Verifier. Evidence
`E:\xhci98-vm\p48\ejw\a\` (`cmdline-a.txt`, `serial-a.log`, `r1.txt` to
`r3.txt` (XHCITR `/report`), `ej1-pci.txt` to `ej3-pci.txt` (the
emulator's `info pci` polled after OK, host-timed), `s*.png`,
`pids.txt`).

| Step | Result |
|---|---|
| Eject 1: `XHCITR /eject "id=<A's upstream switch port>"` from an elevated prompt, OK in the box | "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (E:). The eject was confirmed to the Thunderbolt driver. The Thunderbolt driver has not confirmed that the PCIe tunnel was closed." (stor3's reading reproduced). The driver log (`r1.txt`): `cm.reenum.departure`, `cm.reenum.confirm=00000200`, 250 ms later `cm.reenum.eject=00000200`, `u4.eject.bridge=00000200`, **`u4.eject.unmapped=00000200`**; earlier `u4.map.none=FFFFFFF5` and `u4.learn.expired=00000001`. The router's "PCIe tunnel: up (found at start)" after the eject, two minutes on and at the report. The NVMe left `info pci` only after about 2 minutes, because the enclosure's upstream port (`tp0-sw2`) then read secondary bus 0: Windows had cleared the removed bridge's bus numbers. The tunnel never closed |
| `enc0` `device_del`, then `device_add` again (a re-plug) | the enclosure back, `u4.pcie.up=00010809`, **`u4.learn=00000103`** (port 1 learned); Stor98, problem 0 |
| Eject 2 | "Windows refused: STORAGE\Volume\{...} did not allow it." (the new volume held, probably by the shell after the re-plug); nothing stopped |
| Eject 3 | "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (E:). The eject was confirmed to the Thunderbolt driver. **The PCIe tunnel was closed.** It is now safe to disconnect the device." Log: `cm.reenum.departure`, `cm.reenum.confirm`, 200 ms later `cm.reenum.eject`, `u4.eject.bridge`, `u4.eject=00000100` (port 1, closed). The switch ports left the emulator's tree between 1.8 s and 4.2 s after OK; the router reads "PCIe tunnel: closed after an eject" |

**The cause.** Not the tool's wait and not a late confirmation: the
connection manager reads the confirmation within one status-ask read
(at most every 500 ms, `CM_ST_ASK_MS`, at the 200 ms idle poll) and
hands the eject off 200 to 250 ms later; with the port known the tunnel
closed about 4 s after OK, well inside the ten seconds. Stor3's eject
and eject 1 here were of an enclosure attached at power-on: its tunnel
was found up at start (the pre-OS connection manager's), the `asm4242`
host router answers Get PCIe Downstream Entry Mapping Not Supported, and
a tunnel found up at start never shows the gain record 22 section 7.6
learns from, so its port has no bridge, the hand-off cannot name a USB4
port (`u4.eject.unmapped`), and the tunnel stays up until the unplug.
The tool's "not confirmed" line was true. The status record already
shows it: no router entry names that tunnel port (`CM_ST_E_BRIDGE` stays
`CM_ST_UNKNOWN`).

**The change** (`7ac73c98`, `c738df61`, `c99ef52d`; reviewed and
revised):

- `inst_port_pcie` (`xhciinst/inst_core.c`): a port's PCIe tunnel from
  a status record - closed after an eject, named, unmapped (an active
  member named by no entry beside a host port's router with its PCIe
  tunnel up and no tunnel port known), or none of these.
- XHCITR's watch (`ej_watch_tunnel`, `tr_eject_end_text`) and XHCIINST's
  (`tb_watch_tunnel`): when the record read immediately before the call
  (the unit present, so that router is the unit's own) was unmapped, no
  read of the watch named the port, and the last read at the bound is
  unmapped still (`inst_pcie_unbound`), the line at the end is "The PCIe tunnel stays up until the
  device is unplugged: the Thunderbolt driver does not know which of its
  ports the device is on. It learns that when a device is plugged in
  while Windows runs." in place of "The Thunderbolt driver has not
  confirmed that the PCIe tunnel was closed." (kept for a port that was
  named and not seen closed). The bounds are unchanged (10 s; 30 s for
  the mark service's confirmation).
- `XHCITR /eject id=` of a devnode inside a unit (the drive behind the
  enclosure's switch ports) names that unit, as the window's Safely
  Remove on any node under a unit does; the confirmation lists the
  whole unit. Before, the drive's ID was refused with "No USB device,
  and no device behind a Thunderbolt port, has the instance ID ...",
  which was untrue for it.
- Records 24 section 5.2 and 25 sections 6.2 step 7 and 7.3; the
  tool's `README.TXT`.

**XP to 10 and 98 SE / ME.** The watch and the hand-off are the same
code on every NT target (XP to 10, 2000 too), so the reading holds for
all of them: a tunnel found up at start and never learned is not torn
down by an eject on any of them. On 98 SE and ME the eject is asked of
the connection manager (record 24 section 7.1's revision) and its
teardown runs inside the transaction (`CmNhiU4EjectMember`, the same
`cmU4EjectBridge`), so the same port stays up there too, but no tool
waits on the record and none of these lines is shown; not read in a
9x guest here.

**Host.** `test_inst` 9084 checks, `test_tr` 1775 checks, 0 failures.

**Builds.** The gated `release` x86 and `release -amd64` builds of
`c99ef52d` ("BUILD + GATES PASSED (release)" both,
`E:\xhci98-scratch\w48\ejw\build-x86.log`, `build-amd64.log`); the
tools rebuilt and `make-package.ps1 -Arch x86` and `-Arch amd64`
(`tools-pkg.log`, all rc 0).

**The leg on the change** (the same overlay, a cold start with `enc0`
at power-on again; `XHCITR.EXE` and `XHCIINST.EXE` of `c99ef52d` copied
over `C:\XHCI98` after the tray was ended, `fc /b` equal; the drivers
are not touched by the change):

| Step | Result |
|---|---|
| Eject 4: `XHCITR /eject "id=<the drive's NVMe controller>"` (Stor98's devnode) | the box for the whole unit, as for the upstream port's ID: "Stop QEMU NVMe Ctrl SCSI Disk Device (E:)?", the upstream and downstream switch ports, Stor98 and the disk drive (`t02.png`). OK: "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (E:). The eject was confirmed to the Thunderbolt driver. The PCIe tunnel stays up until the device is unplugged: the Thunderbolt driver does not know which of its ports the device is on. It learns that when a device is plugged in while Windows runs." then "The device has started again, so it is not safe to unplug. ..." Log: `cm.reenum.confirm`, `cm.reenum.eject`, `u4.eject.unmapped` (`r4.txt`); about 7.5 s after the hand-off a new `BusRelations` answer reported the upstream port again (`cm.chain.rel.new=00000001`) and Windows started the unit again, which the tool's after-check read. With the tunnel left up the unit can come back by itself; eject 1 ended before such a rescan |
| `enc0` `device_del`, `device_add` | `u4.learn=00000103` (port 1 learned) |
| Eject 5: `XHCITR /eject "id=<the upstream port>"` | "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (E:). The eject was confirmed to the Thunderbolt driver. The PCIe tunnel was closed. It is now safe to disconnect the device." `u4.eject=00000100`, "PCIe tunnel: closed after an eject" (`r5.txt`) |

**Left.** The rescan that started the unit again after an eject whose
tunnel stayed up (eject 4) was not followed. Teaching the connection
manager the correspondence of a tunnel found up at start (one PCIe
tunnel up and one member with a device behind it, say) is a change of
record 22 section 7.6's learning rule, for the owner; until then a
drive connected at power-on on such a host stops on the eject, but its
tunnel closes only at the unplug, and Windows may start it again.

### Merge (w48-ejw)

Merged into `3.4.3.0` at `cbbb357b` (2026-11-10). The merge review found
that the new message could rest on a stale status snapshot; that fix
follows on w48-ejr.

# Run 48 - worker net6 (Thunderbolt networking, continuing net5)

## net6 takeover (2026-11-09 ~23:20, worker net6; 3.4.3.0 at 7db0e1f9)
- Worktree E:\xhci98-wt\w48-net6, branch w48-net6 from 3.4.3.0 7db0e1f9
  (mk-wt48.ps1 takes -Xhci net6, not -Name; it prefixes w48-).
- Gated x86 release of 7db0e1f9 with the pre-step (pfw, xhciinst, di,
  xhcimark built first in the fresh tree): PASSED, slot 1
  (gated-x86.log; out\gated-x86-release.txt commit 7db0e1f9 dirty=0,
  xhcicm.sys 49B725B3...2954). Gated x86 qemu flavour: PASSED, slot 2
  (xhcicm.sys F4FD4849...C60D, staged as net6\qcm\XHCICMQ.SYS).
- Leg kit E:\xhci98-vm\p48\net6\kit6 = pkg.cmd x86 release + mkkit.sh +
  guest BATs (net's, net5's SPD/SRV, net6 REP/REP98 by the kit's XHCITR,
  NTIRQ.BAT, VAL98.BAT) + IRQON/IRQOFF.REG + TBSPEED.EXE + DEVCON.EXE.
  Kit hashes = the gated record.
- Images (overlays): p98c = p98b + this kit installed (exit 4: xhcicm.sys
  and xhcinet.sys replaced by WININIT at the next start, which each
  overlay's first boot does); pxpc = pxp + this kit (exit 3, FNH walked);
  p98i = p98c + Services\USB XhciCmRingInterrupt 1 (read back by
  REGEDIT /E); pxpi = pxpc + the same by reg add.
- Scripts: E:\xhci98-scratch\w48\net6\kit (lib.sh, up98.sh, upnt.sh,
  setv.sh, cell6.sh, spd6.sh, summ.sh). PIDs: net6\pids.txt.

### Script lessons
- MSYS_NO_PATHCONV=1 (needed for qmp.py's E:/ paths) also stops the
  //F //FI conversion: tasklist/taskkill take /FI and /F then, and qmp
  shot paths must be E:/..., not /e/... The first image scripts' waitexit
  therefore returned at once; the second and third cold starts of p98c
  never ran (the image was still locked by the shutting-down guest).
- The installer leaves C:\XHCI98\XHCITR.EXE as it is when the version
  string matches ("is up to date"): this tree still says 3.4.2.0, so the
  guests' installed XHCITR is the older one without the "Ring interrupt:"
  line. Reports are read with the kit's tools\xhcitr\XHCITR.EXE.
- A .REG import on 98 SE needs the .REG in the kit (mkkit copies *.BAT
  only); verify with REGEDIT /E to COM1.

## 48.38 legs (record 35 revision 25 section 5.5)

| Cell | Ends | Interrupt | Read |
|---|---|---|---|
| C1off | 98 SE p98c (A) - XP pxpc (B) | off both | ping -n 20: 20/20, 13/37/15 ms; TBSPEED -w 64 30 s: B->A 2.04, A->B 2.39 Mbit/s; frames 8513/9061, 0 errors |
| C1on-a98off | 98 SE (value not set: the .REG missing) - XP on | A off, B ring 0 armed | link up, login stalled: A read B's properties before B offered the network, B's Properties Changed never reached A (C1off: re-read at 20 s); no IP, ping unreachable. Not the data cause (never opened) |
| C1on (first, both on) | 98 SE p98i (on) - XP pxpi (on) | A ring 0 armed (cm.irq.armed=00001000, selftest 0), B armed | the same stall: A's first Properties Read answered by the far firmware (cm.xdp.ready=01860001 = slot 1, vendor 8086, device 1: no network), no Changed from B (B's service offered before its record began), A never logs in; B "logging in", 1 Login sent |

### The fix: a Properties Changed Notification to every new peer (record 35 rev 26, section 3.18)
- Linux sends one at the start of every handshake (xdomain.c
  tb_xdomain_state_work INIT/UUID -> tb_xdomain_queue_properties_changed);
  ours only when its own block changed while the record existed. With the
  ring interrupt the XP end was quick enough to have its service offered
  before its record began, so nothing told the 98 SE end to read again.
- COMMIT c4027e4b: TbtXdpPeerStart and the firmware-CM newpeer restart owe
  one (ChangedOwed); a Changed in ERROR before the far UUID restarts the
  handshake (the nudge vector met it). test_tbt_xdp 214/0, test_u4xd
  281/0 (pump(50) for the re-read), test_tbt_icm 1127/0. Gated x86
  release PASS (615C87F4...5568). First review: needs-attention [high]: a
  lost startup notification erased by the peer's notification's re-read
  (shared Disc slot).
- COMMIT 28bfebcb: ReadOwed (the peer's notification's read waits while
  ours is out), ChangedSends cap; test_changed_lost (fails without).
  Gated x86 release PASS. Second review: needs-attention [medium]: a new
  obligation after the last send discarded (Disc.Tries not renewed;
  adopted record promotion).
- COMMIT 8a4863a2: fresh budget for a new obligation;
  test_changed_after_budget (fails without). test_tbt_xdp 227/0,
  test_u4xd 281/0, test_tbt_icm 1127/0, check-text OK. Third review: APPROVE.
  Gated x86 release PASS (gated-x86-fix3.log; xhcicm.sys
  B155ADA6CB9F3B738A55E02A2DC2DFBE8ADE7295463E91E645D939EF5B0E3CE7).
- Kit kit6c = 8a4863a2 release; images p98k (= p98i + kit6c) and pxpk
  (= pxpi + kit6c), both with XhciCmRingInterrupt 1.

### C3 / C4 on the 8a4863a2 images, and the XP images' stale driver
- C3 (98 SE p98k - XP pxpk, both on): the same stall. The 98 SE end now
  sends its startup notification (16 inter-domain packets sent,
  cm.xdp.changed.none: never answered); the XP end sent none.
- C4 (XP - XP, pxpk both, on): the same stall at both ends, neither sending
  a notification. Cause: the XP guests' xhcicm.sys is 464,803 bytes, the
  ba9bac2d image of the original pxp base. The upgrades to 7db0e1f9,
  c4027e4b and 8a4863a2 printed "xhcicm 3.4.2.0, started", exit 3, and
  replaced nothing: the version string and INF date did not change
  (3.4.2.0, 11/9/2026), so XP kept the installed driver. Every XP end of
  C1off, C1on-a98off, C1on and C3 ran ba9bac2d (98 SE ends were upgraded:
  9x installs by hash, "Setup will replace ... at the next start").
  SIDE FINDING: a same-version update on 2000/XP leaves the drivers as
  they were while reporting them started (the installer should compare
  the file, or the dev builds bump the version/date).
- Repaired by hand: NTUPD.BAT (kit6q) copies xhci98, xhciuas, xhcicm,
  xhcinet over the installed files; read back 494,980 bytes etc.
  (ntupd6.sh; typed copies through the Run line failed, the '-' in
  nt-x86 likely mistyped by the Dvorak typer, leaving files renamed .o6).
- Emulator note: usb4-icm.c answers a Properties Read with its own block
  (vendor 8086, device 1, no network) while driver_ready is false
  (XDP_PROP_REQ); that is the block the 98 SE end read first every time.
- Bench kit E:\xhci98-bench\p48-tbnet refreshed: kit3.py (net6 copy)
  8a4863a2 494,707 Eighth net6\notes8.txt: 98SE\XHCICM.SYS 494,980 B,
  SHA-256 b155ada6...3ce7; SHA256.TXT, INSTDIAG.BAT sizes, BENCH-STEPS
  paragraph. XHCITR.EXE kept (3ffb7715, unchanged). Backup:
  net6\bench-r8-backup.

### C4 on 8a4863a2 at both XP ends (drivers copied in by NTUPD.BAT)
| Leg | Read |
|---|---|
| on, 30 s | ping 20/20, 2/31/4 ms; TBSPEED -w 64 4.24 (B->A) / 1.93 (A->B) Mbit/s; cm.irq.armed=00001000, cm.net.irq.open=00000001 at both ends; "Ring interrupt: on" 1,277 / 2,882 wake-ups |
| on, 120 s sustained | 4.20 / 2.39 Mbit/s; still "on" (12,708 / 15,509 wake-ups); no cm.irq.storm/.lost/.down/.suspect, no cm.net.irq.latched. About 10-20 claims a 200 ms period at TCG rates: the >1,000-a-period condition is not reachable here (host vectors carry it) |
| off (NTIRQ.BAT 0, restart), 30 s | ping 20/20, 13/43/15 ms; 4.21 / 2.38 Mbit/s |
- Verdict file: E:\xhci98-scratch\w48\net6\p1-verdict.txt.
- Owed (scripts ready): 98 SE p98k vs an XP end on 8a4863a2 (NTUPD.BAT
  first; legs6.sh does on30, the 120 s with a tablet hot-added on the
  board's xHCI, IRQ 10 shared with the NHI on 98 SE, the re-plug at B and
  off30); the latch (kit6q LATCH.BAT: qemu-flavour xhcicm.sys
  46DA5938...7A18 of 8a4863a2 + XhciCmQemuIrqDead 3); priority 2 (48.18,
  48.16) not started.
- All QEMUs of mine stopped (pids.txt); no gated slot held; no review job.

### Merge (w48-net6)

Merged into `3.4.3.0` at `2907b77e` (2026-11-10). The merge review found
that a Changed Notification arriving during a properties read was
acknowledged and dropped; that fix follows on w48-net7.

From the roadmap (2026-11-10): the owner's round 7 before the task: 10.2 Mbit/s into 98 SE and 25.3
out, the rings served only on the clock tick. Read 2026-11-10 XP to XP: ping 4 ms with the interrupt
on against 15 ms polled, 120 s with no latch. 98 SE against XP with the interrupt on logs in (ping 3
ms against 15 polled), the shared IRQ 10 line and the re-plug pass, the latch read at the XP end.
The owner's round 8: 180-260 Mbit/s into 98 SE with the interrupt but stalls in 3 of 4 tests, a
steady 100 Mbit/s polled with the 64 KB window. The MSTCP fatal exception was seen when sending at
15-40 Mbit/s with the interrupt.

## 48.1 (continued) `uac2`, `uac1`: the 98 SE legs of 26.13 as revised (2026-11-10, w48-aud3)

usb-wdm-free, branch `w48-aud3` from `main` `a27d075`, worktree
`E:\usb-wdm-free-wt\w48-aud3` (tools\ junctions to the main repo's tools).
Not merged, not pushed.

**Build.** aud2's staged x86 free images of `dca05e0` (the code of
`a27d075`): `uac2wdm.sys` `cf243b47...` (`Uac2Build` 28 read in every
leg), `uac1wdm.sys` `568c81a5...` (build 4). The INFs staged with them are
the source INFs at `dca05e0` (names without "(experiment)"), not the
released ones.

**Kit.** `E:\xhci98-vm\p48\aud3\kit` (copy of aud2's; mx.py, sum48.py,
stopmine.py repathed; QMP 47980/47981, models 47982+, slots 0 and 1, two
guests at a time). Host at start: six other QEMUs, CPU 88 %. Starts via
`E:\xhci98-scratch\w48\aud3\start.ps1`; new `scripts\stallinj.py` (host-
stall injector: NtSuspendProcess / NtResumeProcess of the leg's own QEMU,
checked by command line, each stall logged to `g\<leg>\stallinj.log`),
attached after the device's listing by `inj.ps1` / `inj-alaw.ps1`.

**Legs** (98 SE's own UHCI, `w98p`, fresh overlay, rounds of play 48k 5 s,
play 44.1k 3 s, rec 48k 3 s, rec 44.1k 3 s; counters exported after
every step):

| Leg | Stalls | Device | Passed | Resyncs |
|---|---|---|---|---|
| `uac2-w98-own-d1u2` | none | `fs` | 24/24 | 0 |
| `uac2-w98-own-d2u2` | none | `fs` | 24/24 | 0 |
| `uac1-w98-own-d1` | none | `adsrc` | 8/8 | 0 |
| `uac1-w98-own-d1` | none | `alaw` | 16/16 | 0 |
| `uac2-w98-own-i1u2` | 150-600 ms / 2-6 s, 213 | `fs` | 24/24 | 12 plays (15) at refusals; 9 recs behind |
| `uac2-w98-own-i3u2` | 300-1000 ms / 2-5 s, 214 | `fs` | 24/24 | 10 plays (13) at refusals; 10 recs behind |
| `uac1-w98-own-i2` | 150-600 / 2-6, 59 | `adsrc` | 8/8 | 7 recs (9), 7 behind |
| `uac1-w98-own-i4` | 300-1000 / 2-5, 67 | `adsrc` | 8/8 | 8 recs (9), 7 behind |
| `uac1-w98-own-i4` | 300-1000 / 2-5, 151 | `alaw` | 16/16 | 8 plays (12) at refusals; 6 recs behind |

Before/after against aud98's table: `uac2` `fs` 46/48 (aud98, fix) ->
48/48 as found, 48/48 with stalls; `uac1` `adsrc` 8/8 -> 8/8, 16/16 with
stalls; `alaw` 16/16 -> 16/16, 16/16 with stalls. `LastStatus` 0 in every
stream; every recording delivered all buffers. aud98's two failed
recordings (no buffer to UACWAV; 22 resyncs / 192 bytes) not reproduced,
as found or with stalls. First-plug wedges: 4 (d1 1, i1u2 2, i3u2 1),
each recovered by the harness.

**Stats words (95-97 with DataUrbFailed).** As found nothing resynced, so
the new rules had nothing to do. With stalls, of 48 recordings 40
resynced, 38 begun by a URB behind the bus (`ResyncBehind`), 31 of those
with `DataUrbFailed` 0. `DataDiag` counts every data URB whose IRP or URB
status failed, a URB lost whole included, so in those 31 the URB behind
was a successful empty capture URB (`UAC_DATA_EMPTY`): the empty-capture
rule engaged. The lag test needs `SubmitFrameValid`, set only from a frame
`FrameTake` took (the asynchronous request, item 5), so each such
`ResyncBehind` is an asynchronous frame request back and used;
`ResyncFrame` set in every resynced stream (item 3's request); no stream
ended at the 1 s hold. Nothing counts the 100 ms cancel or the 64 ms
re-read; not read. Plays resynced only at refusals (`ResyncBehind` 0).
`adsrc` synch failures (`BadFeedback`) 1-2 a stream as found, 1-15 with
stalls, none ending a stream.

**Record.** Commit `4626085` on `w48-aud3`:
`docs/testing/2026-11-09-uac-uhci-stalls.md` (new section "The revision
on 98 SE (2026-11-10)", builds row, Left: the empty-URB rule no longer
owed, the two failed recordings still open, the item-8 paths with no
counter; the `uac1wdm.rc` strings corrected to 0.1.1.0 since `20a3b56`;
Evidence) and its `open-items.md` entry. Reviewed: approved at `4626085`, no findings (scratch `r1.txt`).

Every harness, injector, simulator and QEMU of this run is in the kit's
`pids.txt`; all exited at their legs' ends and `stopmine.py` found none
alive.

### Merge (usb-wdm-free, w48-aud3)

Merged into usb-wdm-free main at `7071de4` (2026-11-10), docs only.

From the roadmap (2026-11-10): usb-wdm-free's open items as task 48.1 named them: the ALC4080 under
`uac2`, the serial bridges under `usbcom`, the SR9700 and SR9800, the preferred audio device on a
real swap, `uvc98` at Full Speed under UHCI, `usbmidi`'s one fatal exception, the real Sound Blaster
Play! 2 under `uac1`, the `rtl815x` matrix harness, and the Ethernet drivers' EEE and Advanced tab
as the makers' INFs offer them. The reading on `a27d075`'s code (usb-wdm-free `7071de4`) on 98 SE:
`uac2` full speed 48/48, `uac1` recordings 16/16 and A-law 16/16, with host stalls injected, the
empty-capture rule seen engaging.

## 48.36 (continued) The TS3 Plus's second FL1100 and ASM1142 on 98 SE and ME, XP's Code 12 ruled out (2026-11-09, w48-dock2)

**The owner's decision of 2026-11-09** (worker dock's question 1, option A): an xHCI function below
a tunnel port whose pin does not arrive on the port's own starts on 98 SE and ME with no interrupt
line, by record 28's reprocess-only (memory-only) form, which until now admitted an NVMe on Stor98
alone; `xhci98.sys` polls with no interrupt resource (record 15 section 4).

**The design** (record 28 draft 13, section 14 and its 14.3 to 14.6; record 21 section 6.1.1 step 5;
reviewed and revised, eight rounds over the design and the code, the last with nothing material):

- 14.2: rule 3 admits class `0C0330h` beside `010802h` and records the entry's kind; rule 4 for the
  xHCI kind reads `DevLoader` `*NTKERN` and `NTMPDriver` `xhci98.sys` (`xhci98.inf`'s 98 SE and ME
  pair) and no installer record (no version is read: a pre-`2.3.0.0` `xhci98.sys` left by a hand copy
  refuses the start with no interrupt descriptor, so the function shows that failure instead of problem
  29); the xHCI kind takes the memory-only form alone (an IRQ descriptor refuses it, nothing is
  created). `XhciInterruptMode` 1 or 3 make `xhci98.sys` refuse such a start, as for any controller.
- 14.3: the window gets one deadline, held while an entry waits for its driver (no binding yet, or an
  installation problem), up to 300 s from its opening.
- 14.5 (found by the 98 SE leg, below): a member keeps 8 entries and the TS3 Plus brings ten PCI
  devnodes; header-refused bridges kept their slots, so the ASM1142 and the I210 were never met. A
  header-refused entry with no journal now moves to a per-member refused list (handle and id hash,
  32 lines), freed at the next pass so the refusal is still logged; a complete walk drops the lines it
  did not meet; a new generation empties the list.
- 14.6 (found by the 98 SE re-leg, below): every plug of the dock opens the New Hardware wizard for
  its I210 (no 98 SE driver), and CONFIGMG blocks while it is open; the second FL1100's reprocess was
  held about 80 s, its attempt read "timeout" though it started, and the window closed before the
  ASM1142 was met. A call's measured duration above 2 s (an operation from its hand-out to its result,
  or the walk via `CmNiHeld`) is credited to the window's opening and to the deadlines of the attempts
  in progress, up to 300 s a window and an attempt (amending section 4's "a call that blocks spends
  the budget").
- `XhciCmNoIrqDiag` word 4 carries the kind in bits 31:24; XHCITR prints "Kind n: ..." and reads
  reason 4 as "not its driver (Stor98 / xhci98.sys)".

**Commits** (`w48-dock2` from `91d90820`): `7621fd7c` (record 28 section 14, record 21), `2df76062`
(the code: `cm_noirq.c`/`.h`, `cm_cfgwk.c`'s binding read by kind, XHCITR's decoder, vectors,
`docs/using/tuning.md`'s two rows), `254da009` (14.5), `8d7bc126` (14.6). Host: `test_cm_noirq` 388
checks, 0 failures (346 at `2df76062`); `test_trdiag` 189, 0 failures.

**Builds.** `build-driver.cmd release` with `XHCI98_SELFTEST_SKIP_OK=1`: at `2df76062` x86 and
`-amd64` BUILD + GATES PASSED (`E:\xhci98-scratch\w48\dock2\gated-x86.log`, `gated-amd64.log`); at
`254da009` x86 PASSED (`gated-x86b.log`); at `8d7bc126` x86 and `-amd64` PASSED
(`gated-x86c.log`, `gated-amd64c.log`); packages by `make-package.ps1 -Flavor release -Arch x86`.

**Legs** (emulator `E:\xhci98-scratch\w48\dock2\emu\qemu-system-x86_64.exe`, SHA-256
`e3fc8ed3...f5713a9`, the build of the QEMU tree's `0d3e056`, `usb4-emu` `3bbb5c1`; `launch-q35.ps1
-Usb4 tb3ar`, the P14s's Alpine Ridge with its firmware CM; the dock hot-plugged over QMP as
`usb4-device,id=dock0,bus=u4.0,port=1,dialect=tb3,dock-model=ts3plus[,usb-drive=]`; this tree's x86
release package in the family layout, installed with `XHCIINST.EXE /dir 98-me /yes /nopause
/thunderbolt` (98 SE) or `INSTALL.BAT /console /yes /nopause /thunderbolt` (XP); QMP 47922 and 47923,
47921 being held by a QEMU this worker did not start):

| system | build | step | result |
|---|---|---|---|
| 98 SE (fresh overlay `d98s`) | `2df76062` | install, cold start, notice on (`StorageNotice` 1), the dock's first plug, the wizards answered one by one (six bridges each asking a restart, declined; both FL1100s and the ASM1142 from `xhci98.inf`; the NIC's cancelled), 15 min in all | FL1100 #2 and the ASM1142 at Code 29 with `xhci98` bound; the pass met neither: their devnodes appeared only once the bridges had drivers, after the 10 s window (no entry, so section 14.3's hold had nothing to hold). On a first plug the second and third controllers need a re-plug |
| 98 SE | `2df76062` | the dock plugged again (known; cold start between) | **FL1100 #2 started with no IRQ**: `cm.reenum.9x.noirq.after=07000300` (ACTIVE), `noirq=0B000000` (the reprocess), status started, memory FE000000-FE003FFF, `intr.delivery=2` `intr.reason=4` (polling, no interrupt resource), the card reader's stick enumerated SuperSpeed on its port 4; FL1100 #1 on IRQ 10 with stick A; the ASM1142 still Code 29 (never met: the eight entries, 14.5); the attempt logged `refused=0700060C` (FAILED timeout) 80 s later, the NIC's wizard having held the reprocess (14.6) |
| 98 SE | `2df76062` | `XHCITR /eject id=` the dock's upstream port | the list of every function, both FL1100s, their root hubs and the ASM1142 included; OK: "Windows has stopped PCI standard PCI-to-PCI bridge. It is now safe to disconnect the device." (`d98s\j1.png`, `j2.png`) |
| 98 SE | `2df76062` | unplugged, plugged again, then removed by surprise (`device_del` with no eject) | FL1100 #2 started again (its stick's wizard); after the surprise removal the desktop and Start menu answer (`j5.png`, `j6.png`): no hang |
| 98 SE | `254da009` (14.5) | update install, cold start, the dock plugged | FL1100 #2 started again; the ASM1142 still Code 29: the walk had met it only after the NIC's wizard released the worker, the window then closed (14.6) |
| XP SP3 (fresh overlay `dxp`) | `2df76062` | install (`Continue Anyway` answered; the logo box's default button is STOP, so Enter refuses it: two runs ended exit 2, error `0xE000022F`, the third exit 3), restart, the dock's first plug: XHCITR "CalDigit, Inc. TS3 Plus connected.", seven Found New Hardware wizards answered (bridges, controllers, root hubs; the NIC "Cannot Install this Hardware") | 90 s later, **every port Code 0**: 04:00.0, 05:00.0 to 05:04.0; FL1100 #1 (06:00.0), FL1100 #2 (0B:00.0) and the ASM1142 (10:00.0) started with their root hubs; the NIC (15:00.0) Code 28 (no XP driver); each bridge logs `cm.breq.start` twice (record 33 section 4.3's unit restart) (`dxp\rx1.txt`) |
| XP | `2df76062` | a stick on each controller (A on FL1100 #1 port 1, B on FL1100 #2, C on the ASM1142) | B as F:, C as G:: `fc /b` against the kit's copies "no differences", a 1 MiB file written to each and compared equal; A as D: (the eject box names D:, F:, G:) |
| XP | `2df76062` | `XHCITR /eject id=` the upstream port | "Windows has stopped QEMU QEMU HARDDISK USB Device, ... (D:, F:, G:). The eject was confirmed to the Thunderbolt driver. It is now safe to disconnect the device." (`dxp\g1.png`, `g2.png`) |
| 98 SE | **`8d7bc126`** (14.6) | update install, cold start, the dock plugged (known), the NIC's wizard left open about a minute, then cancelled | **all three controllers started**: FL1100 #2 `noirq.after=07000500` and the ASM1142 `08000500` (DONE), the bridges 05:18 and 05:20 now met and set aside (`refused=05200203`, `05180203`); FL1100 #1 on IRQ 10 (`intr.reason=1`), FL1100 #2 (FE000000) and the ASM1142 (FDF00000) polling (`intr.delivery=2`, `intr.reason=4`); a stick on each enumerated SuperSpeed (A on FL1100 #1 port 1, the card reader's on FL1100 #2 port 4, B on the ASM1142 port 1) (`d98s\serial-g-R7.txt`, `-R9.txt`). This base has no NUSB storage part, so no drive letter and no file comparison on 98 SE |
| 98 SE | `8d7bc126` | `XHCITR /eject` of the upstream port, unplugged, plugged again, removed by surprise | the list now names the ASM1142 and its root hub; "Windows has stopped PCI standard PCI-to-PCI bridge. It is now safe to disconnect the device."; after the surprise removal the Start menu answers (`d98s\s6.png`, `s7.png`, `s8.png`) |
| ME (fresh overlay `dme`) | `8d7bc126` | install, cold start, notice on, the dock's first plug (ME's wizards install from `xhci98.inf` with no questions) | FL1100 #2 started on the first plug, polling (`07000500`), the card reader's stick mounted as E:; FL1100 #1 and the ASM1142 not yet bound (their wizards skipped by this worker's keys) |
| ME | `8d7bc126` | the dock plugged again | the ASM1142 started too (`08000500`, polling); A on FL1100 #1 as F:, B on the ASM1142 as G:, C on FL1100 #2 as E:; `fc /b` of each stick's file against the kit's copy "no differences", a 1 MiB file written to E: and G: and compared equal (`dme\serial-b.log`) |
| ME | `8d7bc126` | `XHCITR /eject` of the upstream port, unplugged, plugged again, removed by surprise | "Windows has stopped QEMU QEMU HARDDISK, QEMU QEMU HARDDISK, QEMU QEMU HARDDISK (E:, F:, G:). It is now safe to disconnect the device." (`dme\g1.png`, `g2.png`); after the surprise removal the Start menu answers (`g4.png`) |
| XP | `2df76062` | the dock plugged again (known), then removed by surprise | every port and controller Code 0 again, the NIC 28 (`dxp
x2.txt`); after the surprise removal XHCITR "A Thunderbolt or USB4 device was removed unexpectedly.", the sticks' letters gone (`dxp\g4.png`) |
| XP | `2df76062` | the HP Thunderbolt Dock G2 model (`dock-model=hp-g2`): XHCITR "HP Thunderbolt Dock G2 connected (Thunderbolt 3).", the wizards answered | the switch's ports, the "xHCI98 USB 3 Controller (Intel Titan Ridge)", its root hub and both "xHCI98 USB Hub" Code 0; the "RNDIS/QEMU USB Network Device" (the RTL8153's stand-in) Code 28, no XP driver (`dxp
x3.txt`). No `audiodev=`, so no audio device |

**XP's problem 12 (dock's leg), ruled out as a defect.** On a fresh overlay with the same package
and emulator, once the first plug's Found New Hardware wizards are answered, the mark service's
unit restart (record 33 section 4.3) places every port; it waits while any install below the unit is
pending, and dock's leg had left the FL1100's wizard open ("problem 1, its driver not installed"), so
the ports stayed at the first start's placement (record 33 section 4.1: a bridge's first-time install
drops the kernel-attached filter). Nothing to change.

**No fatal exception 0E** on 98 SE (the coordinator's watch, 2026-11-09): this tree's `XHCITR.EXE`
(`2df76062`, `254da009` and `8d7bc126`, the 48.17 revision) started from the Run key after each
install and cold start; no 0E on any of ten starts of two overlays (screenshots at 60 to 240 s,
`d98-old\c*.png`, `d98s\c150.png`, `c240.png`), nor in the about 3 hours of guest time from 18:26 to
22:10 with the notice on from 18:30 and the dock plugged, unplugged and ejected, its sticks plugged.
ME (`8d7bc126`), notice on, about 40 minutes: none either.

**Owed (48.36).** 2000, 7 and 10 with either preset; the HP dock's eject (read on XP only as a plug);
the network functions with a backend (`nic-netdev=`); a chain behind a dock; the TS3 Plus's audio with
an `audiodev=`; 98 SE's file comparison (the base has no NUSB storage part); the real dock (the
owner's bench: whether the second FL1100 and the ASM1142 start on the P14s under 98 SE, and how long
the I210's wizard holds the worker there). On a dock's very first plug under 98 SE (no bridge driver
yet) the second and third controllers come up only at the next plug: their devnodes appear after the
bridges' wizards, outside any window.

## 48.17 (continued) The owed items (2026-11-09, w48-dock2)

- **98 SE with the revised XHCITR**: run on 98 SE above (the Run key, the notice on, cold starts, no 0E);
  this base has no USB storage driver, so no drive balloon was read there.
- **A standby with a drive attached**: not run (the q35 bases that stand by are none of those used here;
  the 48.15 record: 2000's Cirrus display and 7's VGAPNP refuse it, XP did not sleep in dock's leg).
- **Finding 1 of w48-dock** (2000: a stick removed during a hibernation announced "removed" 30 to 45 s
  after the cold start): read from the code, not traced in a guest. `WM_POWERBROADCAST` reaches the
  tray (a top-level `WS_POPUP` window, `xhcitr.c`): `PBT_APMSUSPEND` sets the quiet and
  drops the queues, a resume broadcast starts `TR_STOR_RESUME_MS` (15 s). The drive stays in the set
  across the sleep (record 25 section 7.8 item 6: "a drive left in ... across a sleep gets 'was removed'
  when it goes"), so its removal is quiet only if the model loses it within 15 s of the resume
  broadcast. Windows' own "Unsafe Removal of Device" came at the same 30 to 45 s, so the devnode left
  only then on that TCG resume: item 6's rule is met as written; the 15 s bound is shorter than this
  emulated resume. Not changed; a bound tied to the controllers' first answering refresh after the
  resume (item 6's per-controller rule) would cover a slow resume and is the owner's call (question 2).

## 48.36, the two joined: one design for the 9x no-IRQ window held while Windows installs (2026-11-09 to 11-10, w48-dock3)

Worker w48-dock3, development host A, branch `w48-dock3` from `7db0e1f9`
(3.4.3.0), worktree `E:\xhci98-wt\w48-dock3`, `w48-dock2` (`8d7bc126`)
merged into it.

**The problem.** 3.4.3.0 (`8edfd362`, `59a28f59`, task 48.31's
follow-up) and `w48-dock2` (`7621fd7c` to `8d7bc126`, task 48.36) each
held record 28's no-IRQ window open while a driver was being installed,
two ways: 3.4.3.0 moved the window's start while the walk met any
descendant at an installation problem or with no `Driver` value, or an
entry was PENDING for one, bounded at 10 minutes; `w48-dock2` moved a
deadline while an entry waited for its driver, bounded at 300 s, and
credited a blocked call's time. The merge conflicted in record 28,
`cm_cfgwk.c`, `cm_noirq.c`/`.h` and `test_cm_noirq.c`.

**The design** (record 28 draft 13, section 14 rewritten as one design
with its own history, 14.8; reviewed and revised over three design
rounds and two code rounds, the last of each with nothing material):

- 14.3, one hold of two readings: a PCI-to-PCI bridge below the member
  being installed (an installation problem, no `Driver` value, or - added
  after the first 98 SE leg below - a function behind it and no child
  devnode yet), read by the walk for bridges alone; or an entry PENDING
  for its own driver (rule 2's installation problem, rule 4's pair not
  written). Restricted to bridges so a function with no driver for the
  system (the dock's I210) holds nothing.
- One clock: the opening (moved only by a credit) and the last hold; the
  window closes 10 s after the later, never later than
  `CM_NOIRQ_HOLD_MS` (10 minutes) after the opening.
  `CM_NOIRQ_INSTALL_MS` (300 s) is gone.
- 14.6, the credit, bounded by the same `CM_NOIRQ_HOLD_MS`; extended to
  the worker's other work between two of a member's ticks (marking,
  tunnel ids, a chain operation, another member's calls), after the
  design review found the shared worker could block outside the no-IRQ
  path; and the rule that no pass judges time while a call is
  outstanding (true of the interim; owed with 42-A.12).
- 14.4, a pair of log lines: `cm.reenum.9x.noirq.hold` (once a window;
  31:24 the source, 1 a bridge with depth 15:8 and problem 7:0 - `FFh` no
  `Driver`, `FEh` no child yet - 2 an entry with its location) and
  `cm.reenum.9x.noirq.credit` (each credited call, its ms); counters
  `Held`, `Credited`, at the stop `cm.reenum.9x.noirq.held`.
- Kept from `w48-dock2`: the xHCI kind (14.2), the refused list (14.5),
  the kind in `XhciCmNoIrqDiag` and XHCITR.

**Commits**: `0445ad58` (the merge, resolved by design), `8c5e02af` (the
no-child hold, after the first 98 SE leg), `13493457` (CM_NOIRQ_HOLD_MS
10 to 30 minutes, after the second), `3ae22bf6` (README, release notes,
record 28 section 14.7 as read; the owner's decision of 2026-11-09). Host: `test_cm_noirq` 482 checks, 0 failures (both
branches' vectors and `test_combined`: a chained NVMe behind bridges
being installed then waiting for Stor98; two xHCI controllers, the
second waiting for `xhci98.inf`; a held call credited, the bound on the
window's own time; the worker's other work credited between ticks; an
I210-only member closed at 10 s); `test_tbt_topo` 799, `test_tbt_brnum`
561, `test_cm_ports` 376, `test_cm_marks` 253, `test_trdiag` 189, all 0
failures.

**Builds** (`build-driver.cmd`, `XHCI98_SELFTEST_SKIP_OK=1`, gated slots):
x86 `all` of `0445ad58`, `8c5e02af` and `13493457`: "BUILD + GATES PASSED
(release qemu)" (`gated-x86.log`, `-x86b`, `-x86c`); amd64 `release` of
the same three: "BUILD + GATES PASSED (release)" (`gated-amd64.log`,
`-amd64b`, `-amd64c`). Packages by `make-package.ps1` release and qemu
x86; kits `E:\xhci98-vm\p48\dock3\xfer` (`8c5e02af`) and `xfer3`
(`13493457`, release `xhcicm.sys` `7C38738B...`, qemu `4C1EA9D9...`).

**Legs** (emulator: the TS3 Plus model with `w48-dock2`'s
`E:\xhci98-scratch\w48\dock2\emu\qemu-system-x86_64.exe`, `-Usb4 tb3ar`,
the dock hot-plugged `usb4-device,id=dock0,bus=u4.0,port=1,dialect=tb3,
dock-model=ts3plus`; the chain with `w48-chain`'s
`E:\xhci98-vm\p48\chain\bin\qemu-system-x86_64.exe`, `-Usb4 adl`, the
dock with a chain port at power-on, the child `usb4-device,id=enc2,
bus=u4.0,parent=enc0,port=1,drive=nvme3,device-model=asm2464`; fresh q35
overlays, `XHCIINST /dir 98-me /console /yes /nopause /thunderbolt
/stor98` (`R9.BAT`), the qemu drivers copied by `QP9.BAT` where said;
98 SE's restart questions answered No with Tab, Enter; evidence and QEMU
PIDs in `E:\xhci98-vm\p48\dock3\<vm>\` and `pids.txt`):

| system, VM | build | step | result |
|---|---|---|---|
| ME `dme` | `0445ad58` release | install, cold start, the dock's **first plug**, the wizards answered (the NIC's left open, then cancelled) | **all three controllers started on the first plug**: `cm.reenum.9x.noirq.hold=010001FF` (a bridge at depth 1, no `Driver` value), FL1100 #2 `07000300` and the ASM1142 `08000300` reprocessed, both polling (`intr.delivery=2`, `intr.reason=4`), FL1100 #1 on IRQ 10; every devnode problem 0 (`dme\serial-b-R1.txt`) | pass |
| ME `dme` | `0445ad58` | a stick on each controller (A FL1100 #1, B FL1100 #2, C ASM1142) | E:, G:, F:; `fc /b` of each stick's file, a 1 MiB file written and compared: "FC: no differences encountered" six times | pass |
| ME `dme` | `0445ad58` | `XHCITR /eject id=` the upstream port; unplugged; plugged again; removed by surprise | "Windows has stopped QEMU QEMU HARDDISK, ... (E:, F:, G:). It is now safe to disconnect the device." (`dme\c2.png`); the re-plug: both `07000500`, `08000500` (DONE), `cm.reenum.9x.noirq.credit=000008A2`; after the surprise removal the Start menu answers (`c4.png`) | pass |
| 98 SE `d98` | `0445ad58` release | the dock's **first plug**, ten wizards answered at a deliberate pace | `hold=01000101`, `refused=04000203`, then no line: the window closed while a bridge's restart question was open (no bridge read as being installed); the controllers at problem 29 | fail, so `8c5e02af` |
| 98 SE `d98q` | `8c5e02af` qemu | the dock's first plug, restart questions left 25 to 40 s | the walk went on past the gap (`refused=05000203`, `06000203`), then FL1100 #2 appeared after the 10-minute bound (13 min of wizards) and stayed at 29 | fail, so `13493457` |
| 98 SE `d98q` | `8c5e02af` qemu | eject, unplug, **re-plug**, sticks, surprise removal | eject "Windows has stopped PCI standard PCI-to-PCI bridge. It is now safe to disconnect the device."; re-plug `hold=010002FE` (the no-child hold), FL1100 #2 `07000500` and the ASM1142 `08000500` (DONE), credits `0C0A`, `0D37`, `13A2`; two controllers polling (reason 4); three sticks enumerated (no storage driver on this base: problem 28, so no file comparison); after the surprise removal the Start menu answers | pass |
| 98 SE `c98` | **`13493457`** qemu | chain2's method (install, the dock's wizards, cold start); the **first chained ASM2464** plugged; two bridge wizards, each restart question left 25 s; Stor98's wizard | `cm.chain.9x.number=00020801`, `reenumerate=00020800`, `hold=010002FE`, `noirq.after=06000300`, `0B000000`, `credit=00003250`, `0D000000`, `after=06000500`; Windows opened F: ("Orico") with no second plug; `copy` of `WIN.INI` to F: and `fc /b`: "FC: no differences encountered" | **pass** (the 98 SE leg chain2 owed, on the joined build) |
| 98 SE `d98r` | **`13493457`** qemu | the dock's **first plug**, ten wizards, restart questions left 25 s, the NIC's open 60 s | `hold=01000101`, `refused=04000203`, `05000203`, `06000203`, then no line for 20 minutes; FL1100 #2 and the ASM1142 there, bound to `xhci98`, problem 29 (`d98r\serial-c-R1.txt`) | **fail, open** (the user documents say "on a dock's first plug under 98 SE they start at the next plug") |
| ME `cme`, `cme2` | `8c5e02af`, `13493457` | the chained first plug | not reached: a cold start with the dock at power-on stopped at a black text screen until a key was sent (`cme2` tag c; `cme` tags b and e were killed there, came up in safe mode and were abandoned) | owed |

**The 98 SE first plug, open.** On `d98r` the walk met the dock's 04:00,
05:00 and 06:00 within the first minutes and then logged nothing for 20
minutes, although both controllers' devnodes existed and were bound; on
the earlier `d98` the worker was later seen alive (it handled the dock's
removal). The logs have no line for the window's close or for a walk
that ends early, so whether the window closed (a hold missed while 98 SE
enumerates, or the bound) or the walk stopped short (`Get_Child` of a
bridge whose restart is pending) is not known. Next: a qemu build
logging each walk's end and the window's close, on a fresh overlay. ME
starts them on the first plug; 98 SE at the re-plug, as before this
work.

**Side findings.**
- XHCITR's no-IRQ diagnostic prints "Kind 0: an NVMe controller, for
  Stor98" for an entry refused by its header (a bridge): the kind is
  recorded only for the two classes, so 0 there means none. Cosmetic;
  for the string review (print the kind only for an admitted class).
- ME's cold start with the dock at power-on twice stopped at a black text
  screen waiting for a key; not this driver (no debugcon line before it).
- The dock2 guest scripts (`G9.BAT` ...) had LF endings, which 9x's
  COMMAND.COM runs as "File not found"; converted in this kit.

**Not done (dock2's owed 48.36 items)**: the HP G2 and the TS3 Plus on
2000, 7 and 10; the HP dock's eject; the ME chained first plug on the
joined build.

### Merge (w48-dock2 through w48-dock3)

w48-dock2 and the chain's first-plug hold (w48-chain2) had each built a hold
of the 9x no-interrupt window in record 28 section 14; w48-dock3 joined them
into one design and merged w48-dock2 into it. Merged into `3.4.3.0` at
`a808dbc6` (2026-11-10), reviewed, nothing material; the integration build of
`a808dbc6` passed. The owner's answers of the day: the README and release
notes name no dock; the drive notice's quiet after a resume stays 15 s
(48.17's Q2).

## The ejected unit that started again (2026-11-10)

Worker ejr, branch `w48-ejr` from `cbbb357b` (3.4.3.0 with w48-ejw merged).
Evidence `E:\xhci98-vm\p48\ejr\a\` (`pids.txt`, `launch-a.cmd`,
`launch-b.cmd`, `qemu-a1.out` / `qemu-a.out` (the emulator's traces
`usb4_tp_*`, `pcie_cap_slot_write_config`, timestamped), `serial-a*.log`,
`r*.txt` (XHCITR `/report`), `s*.png`, `t*.png`).

### Priority 0: the watch's freshness (the merge review of w48-ejw)

`ej_watch_tunnel` (XHCITR) took any successful read of the query
device's cached answer as fresh, and `tb_watch_tunnel` (XHCIINST) any
record echoing the ask made before the call; a stalled publication could
then give the definitive "stays up until unplugged" line from the
pre-eject snapshot.

Commits (reviewed and revised until approved):

- `4b82c447` XHCITR `tr_query_watch_current` (eject-ready, two passes on
  from the answer before the call, same start, age under
  `CM_Q_CURRENT_MS`); XHCIINST late ask 3 s before the bound,
  `inst_watch_current` (echoes the late ask, stage active).
- `d189c39c` the late ask kept beside the run's own (`TBNHI.Late`,
  `tb_mine` for every confirmation): the first form replaced the run's
  ask and so made `tb_after` skip its walk (and the restart check) when
  the late ask was refused or not yet echoed.
- `758ca464` the late echo counts only for a read within
  `INST_WATCH_ASK_MAX_MS` (6 s) of the ask.
- `78995675` that clock starts before the write.
- `88559e5a` XHCITR: the answer's age plus the time the tool held it
  (from before the call) at the decision (`tr_query_watch_new`,
  `tr_query_age_within`).

Host: `test_inst` 9096 checks, `test_tr` 1794 checks, 0 failures. Tools
built (XHCIINST, XHCITR rc 0). Gated `release` x86 of `88559e5a`: BUILD +
GATES PASSED (`build-x86.log`); amd64: see below.
Gated `release -amd64` of `88559e5a`: BUILD + GATES PASSED
(`build-amd64.log`). Slot `E:\xhci98-locks\gated.1`, removed after.

### Task 1: what started the ejected unit again

**Machine.** A copy of ejw's `w81x86` overlay (`disk.qcow2`,
`nvme.qcow2`; kit `d0e3eea0`, Stor98 on the drive, `xhcimark` running),
ejw's command line (`launch-q35.ps1 -Usb4 asm4242 -EncAtBoot -Tablet`,
the pinned emulator `bin\asm4242-20261025-2230`), 2 vCPUs, TCG, QMP
47862, plus the traces `usb4_tp_*` and `pcie_cap_slot_write_config`
(`-msg timestamp=on`). `enc0` at power-on: its tunnel found up at start,
`u4.map.none=FFFFFFF5`, `u4.learn.expired=00000001`.

| Run | Step | Result |
|---|---|---|
| a (ejw's line as is) | eject 1: `XHCITR /eject id=<USP>` (c99ef52d tools), OK | at OK+1.0 s Windows wrote tp0's Slot Control: power on->off, then the power indicator off (`pcie_cap_slot_write_config`, `usb4_tp_guest_power`, `usb4_tp_guest_unplug ... remove`); the emulator removed the endpoint (`usb4_tp_removed gen 1`) and, the tunnel still up, inserted it again at once as a hot plug (`usb4_tp_insert gen 2 cold=0`, Presence Detect Changed and Attention Button Pressed; Windows cleared both, `cfg write 0xaa=0x109`). This time Windows did not power the slot on: the unit stayed stopped 8 min (`r1.txt`, `r2.txt`). The CM: `cm.reenum.confirm`, `cm.reenum.eject`, `u4.eject.unmapped` as ejw read |
| b (`-global usb4-asm-tunnel-dsp.power_controller_present=off`) | eject 2, same | no slot write at all; the upstream port's devnode stays, **problem 47** (held for eject), at 2 min (`r4.txt`) |
| b | `enc1` (a second ASM2464, `nvme1.qcow2`) `device_add` on USB4 port 2 | its tunnel up, the CM re-enumerated every member (`cm.chain.rel` on 02:00.0, 02:08.0, 02:10.0): 02:00.0 answered its child again (count 1, new 0) and Windows kept it at problem 47 (`r5.txt`). Port 2 was not learned: `u4.learn=0000020A` (`SEVERAL`: port 1 pending). enc1's upstream port read **Code 31** (not followed) |
| c (as b, tools of `88559e5a`) | eject 3: `XHCITR /eject id=<USP>` | "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (E:). The eject was confirmed to the Thunderbolt driver. The PCIe tunnel stays up until the device is unplugged: the Thunderbolt driver does not know which of its ports the device is on. It learns that when a device is plugged in while Windows runs." / "It is now safe to disconnect the device." (`c03.png`); problem 47 at 75 s (`r6.txt`) |
| c | restart of Windows, eject 4: `XHCIINST /console /eject id=<USP> /yes` | the same unknown-port line; the late ask written 03:54:00, about 7 s into the watch (`XHCIINST.LOG`); exit 0 |

**The cause.** Not ours: no CM call, no mark-service call and no tool
call rescans the port. The pinned emulator build models the ASM4242's
tunnel ports with QEMU's power controller. Windows' native hot plug
answers the eject by powering the slot off; the model then removes the
endpoint and, the PCIe tunnel being up, inserts it again as a fresh hot
plug with Attention Button Pressed. Windows may take that as a request to
power the slot on (the button's 5 s window, then the enumeration: about
7.5 s, ejw's eject 4) and then starts a new instance of the unit; or not
(eject 1 here, ejw's eject 1). The board's tunnel ports have no power
controller, and usb4-emu dropped it from `usb4-asm-tunnel-dsp` on
2026-10-31 (its ROADMAP, "Tunnel ports with no power controller"); with
it off (run b, c) Windows holds the ejected unit at problem 47, through
the CM's re-enumeration of every member, until the unplug or until the
user asks for it back. Nothing to fix in the driver.

**The launcher.** `E:\xhci98-vm\q35-bases\launch-q35.ps1 -Usb4 asm4242`
pins `asm4242-20261025-2230` and passes no
`usb4-asm-tunnel-dsp.power_controller_present=off`: its eject legs read a
power controller the board lacks. Not changed here (shared); the
coordinator's call (add the global, or a newer emulator).

**98 SE / ME.** The 9x targets have no native PCIe hot plug and never
power a slot, so the emulator's path does not arise there; the
CONFIGMG route's eject (record 24 section 7.1) was not read in a 9x
guest here.

**User-visible strings.** None changed by this branch; the lines read
are record 24 section 5.2's (above).

### Task 2: the design proposal

Record 22 section 7.6.2, "A tunnel found up at start: inferring its port
(proposed, for the owner)", commits `ea779132`, `0d777732` and `472a26d3` (the
revisions; approved on the third read); options: none (as now), one tunnel and one occupied member,
the same cross-checked by both links (Link Active Reporting required),
by position, by ACPI `usb4-port-number`, by a probe. Recommendation:
option 1 now; option 3 eject-only, kept apart from the shared binding
(the closed-path and isolation statements read it), behind
`XhciCmInferAtStart` default 0, only once the board's tunnel ports are
read reporting Data Link Layer Link Active.

### Left

- enc1's upstream port at Code 31 on port 2 while port 1 was pending
  (run b), not followed.
- A pending at-start port blocks the learning of every other port
  (`SEVERAL`), so the tool's "It learns that when a device is plugged in
  while Windows runs" holds for the same port after its unplug, not for
  another port while that device stays. Record 22 section 7.6.2 names a
  narrower learning rule for the owner; not proposed.

The inferred port stays pending for the learning (`472a26d3`): clearing
the mark would let a returning function of the inferred member be
learned as another port's. The design approved after three reads; the
tool changes after five.

### Merge (w48-ejr)

Merged into `3.4.3.0` at `432713e9` (2026-11-10); the integration build of
`432713e9` passed. The merge review found one more gap: XHCIINST decided the
late ask's freshness before the watch's bound, so a tool descheduled between
the two could still use an expired record; that fix follows on w48-ejr2. The
emulator's ASM4242 tunnel ports modelled a slot power controller the board
does not have; the shared q35 launchers now set it off on every system.

## 48.35 (the remaining legs) - 2000, XP, 98 SE, the flush, the XP x64 stall (2026-11-10)

Task 48.35's legs that worker tbs2 left (record 22 section 11.1.12's S.1
to S.3, S.5, S.6, S.8 to S.10 and `xhcimark`'s flush), on `3.4.3.0` at
`2907b77e` (`XhciCmSleepEntry` 1 by default since `318860c0`). Branch
`w48-tbs3`, no commit: nothing read here failed on this package's code.

**Build and kit.** Gated `build-driver.cmd release` x86 and `release
-amd64` of `2907b77e` (`XHCI98_SELFTEST_SKIP_OK=1`, slot `gated.1`), each
`BUILD + GATES PASSED (release)` (`E:\xhci98-scratch\w48\tbs3\gated-x86.log`,
`gated-amd64.log`). `make-package.ps1 -Arch x86` and `-Arch amd64`
(unsigned), laid out by `ConvertTo-FamilyLayout`; `xhciinst`, `xhcitr` and
`xhcimark` built from this tree (`XHCIMARK.EXE` `a40653b24f232f10...`,
`XHCIMK64.EXE` `6fd029d8b31e0d45...`), the root files (`INSTALL.BAT`,
`LICENSE`, `readme.txt`) and the other tools from tbs2's kit. x86
`xhcicm.sys` `25d07bfd84c4dbcb...`, `xhci98.sys` `3936c53debf482cc...`,
`stor98.sys` `3ab9bb22c09449ef...`; amd64 `xhcicm.sys`
`088a643fe6994717...`, `xhci98.sys` `be81b882bac2e8f9...`, `stor98.sys`
`413d2c1172255bc5...`. The amd64 package carries no catalog without
`-TestSignPfx`, and the installer refuses it on XP x64 ("this 64-bit
package carries the Thunderbolt driver without its catalog"); so its six
catalogs were made unsigned on the host with the packager's own
`New-PackageCatalogFile` (WDK 7.1 MakeCat, no key) and the family folder
laid out again (`E:\xhci98-scratch\w48\tbs3\cat64.ps1`). Nothing was
signed, on the host or in a guest. Each NT guest's installed
`xhcicm.sys`, `xhci98.sys` and `stor98.sys` compared with the kit's
(`fc /b`: no differences).

**Vehicle.** Development host A, TCG, the pinned emulator
`asm4242-20261025-2230`, tbs2's copy of `launch-q35.ps1` (so the
ASM4242 tunnel ports keep the emulator's slot power controller, as on
tbs2's legs; the shared launchers drop it from 2026-11-10) with
`-Usb4 asm4242 -EncAtBoot` (the ASM4242 model, its pre-OS CM on, the
ORICO ASM2464 enclosure with the FAT NVMe at power-on) and `-bios
E:\xhci98-vm\p48\q35s3\bin\bios-256k.bin` (SHA-256 `e5dc0550...`, checked
at every start), TCP QMP 47840-47844, at most two guests of mine at once.
Fresh overlays of the q35 bases `xp`, `w2kacpi` (2000 SP4 with the ACPI
HAL, `-Base`), `xpx64` and `w98se`. Installed by `INSTALL.BAT /console
/yes /nopause /thunderbolt /stor98` (2000, XP: exit 3; 98 SE: exit 3).
One script per leg spans the sleep (`E:\xhci98-vm\p48\tbs3\xfer\ONE.CMD`
on NT, `ONE9.BAT` on 98 SE): the value as read in the machine-wide key,
the device count (XP; WDK 7.1's `devcon` does not start on 2000 or x64),
`W<tag>A.BIN` (4 MiB) written to the enclosure's volume and compared,
`XHCITR /report`; a scratch `SLP.EXE` (Visual C++ 6, `SetSuspendState`
with Hibernate FALSE or TRUE, so the leg does not depend on the
hibernation setting); after the resume 40 s, the volume listed,
`W<tag>A.BIN` compared again, `W<tag>B.BIN` written and compared, the
report, and the mark service's log
(`%SystemRoot%\system32\LogFiles\xhci98\XHCIMARK.LOG`, "suspend notice"
lines), then the shutdown (`ExitWindowsEx`), timed on the host. S3: QEMU
read `suspended`, held 45 s, `system_wakeup`. S4: QEMU exited at the
hibernation's power-off and was started again cold.

### The flush at a suspend notice (`xhcimark`)

It runs. tbs2's `ONE.CMD` read `%SystemRoot%\LogFiles\xhci98\XHCIMARK.LOG`,
which does not exist; the service writes
`%SystemRoot%\system32\LogFiles\xhci98\XHCIMARK.LOG` (`mk_log_open`,
`GetSystemDirectoryA`; record 21's "The log"). Read here:

- **7 x86, tbs2's own legs**: a copy of tbs2's installed overlay
  (`E:\xhci98-vm\p48\tbs2\w7\disk.qcow2`, copied, booted once): its log
  holds one `suspend notice 4: D: flushed` for each of tbs2's four legs
  (a to d, S3 and S4, at 1 and at 0). Windows 7 sends no
  `PBT_APMQUERYSUSPEND`; `PBT_APMSUSPEND` (4) was taken and flushed the
  ORICO volume every time.
- **2000** (the legs below): every `PBT_APMQUERYSUSPEND` (0) flushed `E:`
  (5 of 5); at the standby attempts `PBT_APMSUSPEND` flushed `E:` too
  (2 of 2); at each hibernation (3 of 3) `PBT_APMSUSPEND`'s outcome was
  `E: flush not finished within 1500 ms`, logged after the resume (the
  worker's bound passed while the system went down; the query's flush a
  few seconds earlier had completed). As designed (record 22 section
  11.1.11 Q2: best effort, the outcome logged); no change made.

### Legs

| Guest | Leg | `XhciCmSleepEntry` | Read | Verdict |
|---|---|---|---|---|
| 2000 SP4 (ACPI HAL) | a, S3 | 1 (the INF default; no machine-wide value) | `SetSuspendState` returned FALSE at once after the suspend notices (`PBT_APMSUSPEND` was broadcast and flushed); the guest never suspended. Controls on fresh overlays of the same base: without this package, with the ASM4242 model, and without any USB4 device: the same refusal | **not readable**: this base on q35 does not enter standby, with or without the package |
| 2000 | b, S4 | 1 (default) | `E:` and its files before and after, `W<b>B.BIN` written and compared; the report empty before and after (`XHCITR /report` gave no matching line on that boot); shutdown about 20 s | **pass** (the entry not shown; see e) |
| 2000 | c, S3 | 0 | the same refusal as a | not readable |
| 2000 | d, S4 | 0 | `E:` and its files; `cm.usb4.sleep=00000100`, no `u4.sleep` line; `cm.suspend=00000005`, `cm.reenum.hold.passed` / `.released` for 0200, 0208, 0210, `cm.resume=00000001`, `cm.reenum.resumed=00000001`; shutdown about 11 s | **pass** |
| 2000 | e, S4 | 1 (machine-wide) | `E:` and its files; `u4.sleep.entries 1`, `.routers 2`, `.ready 2`, `u4.sleep.entered=00000002`, `cm.suspend=00000005`, the hold lines, `cm.resume=00000001`, `cm.reenum.resumed=00000001`; shutdown about 20 s | **pass** |
| XP SP3 x86 | a, S3; b, S4 | 1 (default) | `SetSuspendState` FALSE for both, no suspend notice; `powercfg /query`: "System standby ... Not Supported", "System hibernates ... Not Supported", and `powercfg /hibernate on`: "System does not support hibernation" - before this package was installed too, and again with `-Vga cirrus` (XP's own Cirrus driver running) | **not readable**: the XP x86 q35 base offers neither (as 44.27 found its image offering no hibernation); the ORICO volume `D:` and its files read before and after each attempt, the shutdown after it 2 s from the request |
| 98 SE | a, S3 | 1 (default) | the ORICO volume `E:` on Stor98 before and after, files compared; `SetSuspendState` FALSE; from Run, `SetSystemPowerState`: "Your computer cannot go on standby because a device driver or program won't allow it". The control, a fresh overlay of the base without this package (same machine, wizards answered): the same message | **not readable**: the 98 SE q35 base refuses standby without the package too |

| XP x64 SP2 (S.10, the stall) | - | - | not reached: the unsigned amd64 package (with the unsigned catalogs) installed most of its drivers (`xhcicm` on the ASM4242 router, `xhci98` on both controllers, the filter on the tunnel ports), but each of three runs ended at exit 4 behind Found New Hardware and "Security Alert - Driver Installation" windows, and Stor98 was not bound to the tunnelled NVMe (the installer: "its location could not be read (no Thunderbolt connection manager answered)"; the mark service's later install goes through XP's own wizard); the guest was shut down there | **not read** |

The x64 legs need a kit that installs unattended on XP x64 (the
driver-signing policy set to Ignore first, as the q35 base's README
advises, or the wizards walked with a pointer); 7 x64 needs the package
signed in the guest, which was not done.

Not read, owed: ME (its q35 base is 98 SE's family and was not tried
after 98 SE's refusal), S.1 (the i440FX SHPC bridge), S.2/S.3 (the q35
native port without and with a pre-boot CM on 7 x64 and 10 x64), S.5
(Titan Ridge), S.6 (`intel-adl`), S.8 (the chain), 7 x64 (the x64
package would need signing in the guest), and the bench.

Evidence: `E:\xhci98-vm\p48\tbs3\` (`xp\`, `w2k\`, `w2kc\` (the 2000
controls), `w98\`, `w98c\` (the 98 SE control), `x64\`, `w7\` (the copy
of tbs2's 7 overlay): `serial-*.log`, `cmdline-*.txt`, screens,
`pids.txt`), the scripts in `tools\` (`go.ps1`, `inst.sh`, `ans.sh`,
`ansy.sh`, `leg.sh`, `leg9.sh`, `seq.sh`, `seq2k.sh`, `slp.c`), `xfer\G.CMD`,
`xfer\ONE.CMD`, `xfer\G9.BAT`, `xfer\ONE9.BAT`; every QEMU start and stop
in `pids-all.txt`.

# Run 48 - worker net7 (Thunderbolt networking, continuing net6)

## net7 takeover (2026-11-10, worker net7; 3.4.3.0 at 2907b77e)
- Worktree E:\xhci98-wt\w48-net7, branch w48-net7 from 2907b77e (net6's
  handshake fix merged). Scripts copied from net6 into net7\kit and net7\bin
  (net6 -> net7, kit6 -> kit7); new ntimg.sh (XP image + NTUPD.BAT);
  guest set gains NTUPD.BAT and LATCH.BAT.

### Merge-review finding on 2907b77e (coordinator): Changed during PROPS
- [medium] tbt_xdp.c: a Changed Notification in PROPS was answered and
  dropped; a delayed last Properties Response with the old block (no
  network directory) was adopted, the peer stopped resending: the stall
  revision 26 meant to cure.
- COMMIT b84b5a32: latch in PROPS (ReadOwed), completed block not taken,
  read again (cm.xdp.props.again 0). test_changed_during_read (fails
  without). First review: needs-attention [high]: a read that fails (far block
  shrank under an offset request, resends exhausted) or is refused
  (Error Response) strands the latch in ERROR.
- COMMIT 0ed7ebec: tbtPropsAgain at every end of the read (complete 0,
  give-up 1, size refusal 2); starting it clears the latch.
  test_changed_read_fails, test_changed_read_refused (fail without).
  test_tbt_xdp 247/0, test_tbt_icm 1127/0, test_u4xd 281/0. Record 35
  revision 27 (section 3.18).
- Second review (--base 2907b77e): APPROVE, no material findings.
- Gated x86 release of 0ed7ebec (pre-step): net7\gated-x86.log.
- 0ed7ebec gated x86 release: PASSED (slot 2; out\gated-x86-release.txt
  commit 0ed7ebec dirty=0). xhcicm.sys 495,156 B, SHA-256
  5472ea324a2085cdd5345355bb517123994661a06a4776e728d7d39e498dc971
  (copy net7\rel\xhcicm.sys).
- Kit E:\xhci98-vm\p48\net7\kit7 (pkg.cmd x86 + mkkit.sh + NTUPD/LATCH +
  TBSPEED/DEVCON/NTIRQ/IRQON/IRQOFF); both xhcicm.sys = 5472ea32.
- BENCH KIT refreshed (backup net7\bench-r8-backup): kit3.py 0ed7ebec
  494,980 Eighth net7\notes8.txt: 98SE\XHCICM.SYS 495,156 B 5472ea32...;
  SHA256.TXT, INSTDIAG.BAT size, BENCH-STEPS paragraph. XHCITR.EXE kept.
- Images: p98m = net6 p98k + kit7 (up98.sh, I98.BAT, 98 SE by hash);
  pxpm = net6 pxpk + kit7 NTUPD.BAT (ntimg.sh). Both carry
  XhciCmRingInterrupt 1 (98 SE also XhciPfwNhiLine 0) from p98i/pxpi.
- 0ed7ebec gated x86 qemu: PASSED (slot 1), xhcicm.sys 496,425 B
  440C64FC...343E -> kit7q\XHCICMQ.SYS (kit7q = kit7 + it).
- 0ed7ebec gated amd64 release: PASSED (slot 2), xhcicm.sys 0F474493...CFCC.

## Priority 1 legs (0ed7ebec at both ends)

### C1: 98 SE p98m (A, IRQ 10 NHI shared with the board's xHCI) - XP pxpm (B)
- Driver check: A DIR98: XHCICM.SYS 495,156 (11-10 3:00a), XHCIUAS 43,878
  current; XHCI98.SYS 349,680 (11-09 3:59p) - NOT the kit's 352,481
  (SIDE FINDING: 98 SE's update replaced xhcicm/xhciuas but kept an older
  same-version xhci98.sys; exit 4). B NTUPD: all six 0ed7ebec sizes.
- on30: LOGS IN. ping 20/20 2/17/3 ms; TBSPEED -w 64 B->A 3.71, A->B
  2.41 Mbit/s; "Ring interrupt: on" A 5,056 / B 5,881 wake-ups;
  cm.net.irq.open=1, cm.irq.armed=00001000, cm.irq.connected=3A (IRQ 10).
- on120 with a usb-tablet hot-added on the board's xHCI (xh.0, bound
  0627:0001 HS, on IRQ 10 with the NHI) and moved every 0.5 s: ping 20/20
  2/37/5 ms; 3.82 / 2.40 Mbit/s; still on (A 25,927 wake-ups); no
  cm.irq.storm/.lost/.down/.suspect, no latch.
- re-plug at B (plugged false 15 s, true; both ends cable=1 up=1 after):
  ping 20/20 1/16/2 ms; 10 s 6.70 / 2.19 Mbit/s; still on (28,684 /
  33,362 wake-ups).
- off30 (first): INVALID - the 98 SE end did not shut down (Alt+F4 then
  the Run line) and was killed after 10 min, losing its registry write:
  A still on, B off: ping 12/30/15 ms, 1.89 / 2.39 Mbit/s (mixed).
  Redone with offredo.sh (Start, Shut Down).
- offredo.sh: also failed (the 98 SE export still showed 1 after VAL98
  IRQOFF in the restarted overlay; A killed again). Abandoned for an image:
  p98n = p98m + setv.sh 'VAL98.BAT IRQOFF' (clean ExitWindows; the export
  read back XhciPfwNhiLine 1, the USB key's lines missing from COM1).
- off30 (cell C1o: A p98n, B = C1's B overlay with NTIRQ 0): both "Ring
  interrupt: off (checked every clock tick)"; ping 20/20 13/30/15 ms;
  TBSPEED B->A 2.11, A->B 2.40 Mbit/s. (98 SE off here = IRQOFF: the
  value 0 and XhciPfwNhiLine 1, net5's bench .REG.)
- L1 latch: stopped at the handback before LATCH.BAT ran; guests 155000/25016 killed. Verdict: net7\p1-verdict.txt.

### Merge (w48-net7)

Merged into `3.4.3.0` at `a3b9c152` (2026-11-10). The merge review found
the restarts unbounded across reads (a peer notifying during every read
keeps the engine reading); that fix follows on w48-net8. The owner's
round-8 bench kit carries `0ed7ebec`'s xhcicm.sys.

### Merge (w48-ejr2)

Merged into `3.4.3.0` at `10c2e15f` (2026-11-10): XHCIINST decides the late
ask's freshness at the watch's final decision and judges only its own late
ask; test_inst's timing vectors cover a decision past the bound and the
tick wrap.

## A same-version update replaces every file whose bytes differ (2026-11-10, w48-inst)

The coordinator's task of 2026-11-10, from three findings of the day
before: on XP an update of the same version and `DriverVer` (`3.4.2.0`,
`11/08/2026`) printed "xhcicm 3.4.2.0, started", exited 3 and replaced
no driver file (net6); `XHCITR.EXE` is kept "up to date" by version
(net6); on ME `stor98.mpd` was not replaced and the drive's row read
"Reinstall (same version)" (stor3). Development host A, the pinned
emulator `asm4242-20261025-2230`, headless, QMP 47820 (XP) and 47821
(ME); every start and stop in `E:\xhci98-vm\p48\inst\pids.txt`; scripts
in `E:\xhci98-scratch\w48\inst\k3\`.

### The cause

`xhciinst\inst_core.c` `inst_versioned` and `inst_action`: a device of
this package whose driver key's `DriverVersion` equals the package's is
`INST_ACT_CURRENT`, and `inst_action_runs` runs it only with `/force`;
`group_verdicts` repairs a current device only when the group's binary
is missing or older by its version resource. `inst_tray_copy_needed`
copies XHCITR only when it is older; `inst_s98_file_update` replaces
`stor98.mpd` only when it is older. Nothing compared bytes at the same
version, so a different build of the same version was "current"
everywhere but the three 9x files `sys_9x` already places by bytes.
Windows is not the cause: a forced install replaces equal files (below).

### What Windows does at the same version, read first with the old installer

Kits `E:\xhci98-vm\p48\inst\kA` and `kB` are copies of net6's `kit6` and
`kit6c` (two `3.4.2.0` builds; every binary differs, INFs equal).

| Leg | Image | Result |
|---|---|---|
| XP, kit B plain then `/force` (`sx2`: a flattened copy of net6's `pxpk`, the image with the stale driver) | before: `fc /b` against kit B: `xhci98.sys` DIFFERS (349,680 / 352,481), `xhcihlpr.sys` DIFFERS (13,568 both), `xhcicm.sys` DIFFERS (464,803 / 494,980), `xhcinet.sys` DIFFERS, `XHCITR.EXE` DIFFERS (585,728 both), `XHCIPFW.SYS` and `XHCIMARK.EXE` SAME | `INSTALL.BAT /force`: every device "Updating ... done", exit 3, "xHCI98 USB Tree at C:\XHCI98\XHCITR.EXE is up to date."; after the restart `xhci98.sys`, `xhcihlpr.sys`, `xhcicm.sys`, `xhcinet.sys` SAME, `XHCITR.EXE` still DIFFERS (`sx2\serial-b.log`) |
| ME, kit B `/force` (`sme1`: a copy of stor3's `fme`, an earlier build installed) | before (`dir`): `xhci98.sys` 349,073 / 352,481, `xhciuas.sys` 43,879 / 43,878, `xhcicm.sys` 482,181 / 494,980, `stor98.mpd` 65,423 / 65,182, `XHCITR.EXE` 565,248 / 585,728 | exit 3 (the helper reinstalled both controllers, "Setup will replace ... xhciuas.sys / xhcicm.sys at the next start"); after the restart `xhci98.sys`, `xhciuas.sys`, `xhcicm.sys`, `xhcinet.sys` the package's; `stor98.mpd` (65,423) and `XHCITR.EXE` (565,248) not (`sme1\serial-c.log`) |

So on XP Windows' ranking does not keep an equal driver once the install
is forced (`INSTALLFLAG_FORCE`), `xhcihlpr.sys`'s `0x20` row included;
the installer never forced it. Kit A on a fresh XP base was abandoned: a
Logo dialog hidden behind a Found New Hardware window stalled it
(`sx1`, killed).

### The fix (record 23 section 5.4.1, revision 54)

A file of this package installed at the package's version whose bytes
differ from the package's is treated as an older one, on every system;
never a newer version (its refusal stands, its bytes are not compared),
never a file whose bytes cannot be read (logged, taken as the same).
2000 to 10: the group's current devices reinstalled with
`INSTALLFLAG_FORCE` (for `xhci98` its controllers), then a file check
after the passes (owed for the whole run; any queued
`PendingFileRenameOperations` entry for the file distrusted, the tool's
own rename appended last from a fresh `<name>.X00`-`.X99`; an unreadable
queue fails the file); `stor98.sys` by the file check alone and only when
no other drive shares it; never under the 5.7 trap or a refused or failed
install of its INF. 98 SE and ME: `xhci98.sys` placed as `xhciuas.sys` is
(record 26 drafts 68/69) unless the helper's passes install a controller
now; `stor98.mpd` replaced at the same release with other bytes (the
disconnect confirmation as for an older file; `/yes` leaves it). XHCITR
copied at the same version with other bytes. Commits `05d20d52`
(design, code, vectors), `8eb6e03d`, `fb05559d`, `3b309d2b` (the
rename queue), `de75433d` (9x placement not put off by an `Enum` entry
with no devnode, read on ME below), `a829731a` (the check latched, the
queue read fail-closed), `c7080f71` (the latch only for a component
still in the run), `20240493` (the 9x Stor98 notice), `5a445efe` (a
failed or refused Stor98 install keeps the check off `stor98.sys`);
reviewed and revised until approved at `5a445efe`. Host: `test_inst`
9069 checks 0 failures, `test_trset` 1975/0; `check-text` OK. Gated
`release` x86 and amd64 PASSED at `05d20d52`
(`E:\xhci98-scratch\w48\inst\logs\gated-x86-sv.log`,
`gated-amd64-sv.log`) and again at `5a445efe` (`gated-x86-sv2.log`,
`gated-amd64-sv2.log`).

### Leg kit

`E:\xhci98-vm\p48\inst\kN`: `make-package.ps1 -Arch x86` of this branch,
`ConvertTo-FamilyLayout`, tools and guest BATs (`k3\mkkit.sh`); every
binary differs from kit B and the guests' files at the same version
`3.4.2.0`; hashes in `E:\xhci98-vm\p48\inst\kN-hashes.txt`.

### XP: the stale image updated (sx3, overlay of sx2\xpbase, tb3ar; installer at 8eb6e03d)

| Step | Result |
|---|---|
| before (`CHK x1`) | `xhci98.sys` DIFFERS 349,680 / 352,481, `xhcihlpr.sys` DIFFERS, `xhcicm.sys` DIFFERS 464,803 / 495,012, `XHCIPFW.SYS` DIFFERS, `xhcinet.sys` DIFFERS, `XHCITR.EXE` DIFFERS (585,728 both) |
| `INSTALL.BAT /console /yes /nopause /thunderbolt /tbnet /stor98` (no `/force`) | rows "Replace files (same version)" for both controllers, the NHI, the tunnel ports and the network adapter, "Keep (same version)" for the root hubs; plan lines "xhci98.sys / xhcicm.sys / xhcinet.sys / xhcihlpr.sys: same version, other bytes: replaced, loaded at next start."; every device "Updating ... done", XHCIPFW.SYS placed, "xHCI98 USB Tree copied to C:\XHCI98\XHCITR.EXE.", exit 3 |
| restart, `CHK x2` | `xhci98.sys`, `xhcihlpr.sys`, `xhcicm.sys` (495,012), `xhcinet.sys`, `XHCITR.EXE` SAME; the controllers and NHI started (Results "xhci98 3.4.2.0, started", "xhcicm 3.4.2.0, started") |
| the same command again | every row "Keep (same version)", no plan line of section 5.4.1, "xHCI98 USB Tree ... is up to date."; exit 3 from `/tbnet`'s `XhciCmNet` write and the mark service's restart, as before the change (no loop) |

The file check found every file the package's after Windows' forced
installs (no "Replaced"/"Setup will replace" line), as the old-installer
reading predicted.

### ME: the stale image updated (sme1, asm4242)

The `sme1` image after the old-installer reading: kit B's `xhci98.sys`,
`xhciuas.sys`, `xhcicm.sys`, `xhcinet.sys`; stor3's `stor98.mpd`
(65,423); the older `XHCITR.EXE` (565,248). Kit N's: 352,481 / 43,878 /
495,012 / 16,972 / 65,422 / 585,728 (sizes and the kit's dates, `dir`).

| Run (installer) | Result |
|---|---|
| l1, `/yes` (`8eb6e03d`) | `xhciuas.sys`, `xhcicm.sys`, `xhcinet.sys` queued by bytes as before, XHCITR copied; plan "stor98.mpd: same version, other bytes: replaced ..." but "/yes: Stor98's driver file is not updated" (the disconnect confirmation, as for an older file); **"xhci98.sys: run Setup again after the restart to replace it."**: an old slot's `Enum` entry, planned as an install the helper never runs, counted as a helper job, every run (fixed in `de75433d`) |
| l2, the same | after the restart xhciuas/xhcicm/xhcinet/XHCITR the kit's; `xhci98.sys` and `stor98.mpd` not (as l1 said) |
| l4, no `/yes` (`a829731a`) | the controller's row "Replace files (same version)"; "placed9x: WININIT.INI [rename] ...XHCI98.SYS=...XHCI98.IN$", "Setup will replace C:\WINDOWS\SYSTEM32\DRIVERS\xhci98.sys at the next start."; the Stor98 question answered N by a stray Enter; its notice still said "an older version" (fixed in `20240493`) |
| l5 after the restart | `xhci98.sys` 352,481 with the kit's date: the package's. Then `INSTALL.BAT /console /nopause /defaults /thunderbolt /tbnet /stor98`, every question Y: exit 4 (the drives' existing steps), "shut down and start Windows again" |
| l6 after the cold start | `stor98.mpd` 65,422 with the kit's date; `xhci98.sys` and XHCITR the kit's |

### 98 SE: no regression, and net7's case (s98, overlay of a flattened copy of net6's p98k, tb3ar; installer a829731a)

| Run | Result |
|---|---|
| l1, `/yes` | before: `xhci98.sys` 349,680 against the kit's 352,481 at the same version (net7's reading), `stor98.mpd` 64,301 against 65,422, `XHCITR.EXE` 585,728 both (other dates); plan "xhci98.sys: same version, other bytes: replaced, loaded at next start." and the same for `stor98.mpd`; xhciuas/xhcicm/xhcinet queued by bytes as before; "Setup will replace C:\WINDOWS\SYSTEM32\DRIVERS\xhci98.sys at the next start." (`Placed9x` recorded); `/yes` left `stor98.mpd`; exit 4 |
| l2 after the restart | `xhci98.sys`, `xhciuas.sys`, `xhcicm.sys`, `xhcinet.sys`, `XHCITR.EXE` the kit's; the second run plans no `xhci98.sys` line (no loop), `stor98.mpd` still owed to a confirmed run; exit 4 |

Every guest stopped (pids.txt). The XP and 98 SE legs ran installers a
commit or two before the final one; the later commits change only the
NT file check's queue handling and guards (`3b309d2b` to `5a445efe`) and
the 9x notice text, which no leg's path above reached except the notice.

### After the merge (f990fbed): binaries more than one INF installs

The merge review found the NT file check holding `xhcicm.sys` only by
`xhcicm.inf`, though `xhcicmf.inf` installs the same image: with no NHI
present, a cancelled filter install or a filter-only store trap would
not have kept it. Every file audited: `xhcicm.sys` (`xhcicm.inf`,
`xhcicmf.inf`) and `xhcihlpr.sys` (`xhci98.inf`, `xhcicm.inf`) are the
two shared ones; `xhci98.sys`, `xhciuas.sys`, `stor98.sys`,
`xhcinet.sys` / `xhcinet6.sys` have one INF on their system. Commits
`1e0a76f1` (`inst_stale_infs`, `inst_stale_blocked`: a trap or a failed,
refused, cancelled or skipped install of any installing INF holds the
file; every failure and skip in `install_inf` latched), `55715651`
(`inst_hlpr_stale_ok`: `xhcihlpr.sys` kept while either group is refused;
every failed absent-UAS install or check latched), `fa381632` (section
5.5.1's older-file UAS fallback off after such a failure too); vectors
for a filter cancellation and a filter-only store trap with no NHI, both
refusal directions for the helper, the absent-UAS failure. Reviewed and
revised until approved at `fa381632`; `test_inst` 9129/0, `test_trset`
1975/0, `check-text` OK. No guest leg: the change only narrows when the
NT file check runs.

### Merges (w48-inst)

Merged into `3.4.3.0` at `f990fbed` (2026-11-10). The merge review found
that the NT file check could replace the shared xhcicm.sys after the
tunnel filter's install was refused; fixed (`1e0a76f1` to `fa381632`: the
check held off while any INF installing the same binary is trapped,
failed, refused, cancelled or skipped), merged at `9b55357a`; the
integration build of `9b55357a` passed.

## 48.36 (continued) The dock's first plug on 98 SE: 98 SE enumerates a new bridge's children one at a time (2026-11-10, w48-dock4)

Worker w48-dock4, development host A, branch `w48-dock4` from `a808dbc6`
(3.4.3.0, w48-dock3 merged), worktree `E:\xhci98-wt\w48-dock4`. QMP 47931
and 47932; QEMU PIDs in `E:\xhci98-vm\p48\dock4\pids.txt`.

### The reading (diagnostic qemu build)

The qemu `xhcicm.sys` of this tree with two new pairs of lines (kept,
record 28 section 14.4): `cm.reenum.9x.noirq.walk` (each walk whose end,
devnode count or hold differs from the last logged in the window) with a
`cm.reenum.9x.noirq.walk.node` per devnode met, and
`cm.reenum.9x.noirq.close` / `.close.hold` at the window's close.

98 SE, fresh overlay `d98a` (the q35 base, `launch-q35.ps1 -Usb4 tb3ar`,
w48-dock2's emulator), `XHCIINST /dir 98-me /console /yes /nopause
/thunderbolt /stor98` from dock3's `xfer3` kit (release `13493457`), the
diagnostic qemu `xhcicm.sys` (`3A4B0A91...`) and dock3's qemu
`xhci98.sys` copied by `QP9.BAT`, cold start, the TS3 Plus model
hot-plugged (`usb4-device,id=dock0,bus=u4.0,port=1,dialect=tb3,
dock-model=ts3plus`), the wizards answered one by one, each restart
question left 15 to 25 s and answered No (`d98a\debugcon-c-keep.log`):

```
cm.reenum.9x.noirq.walk=00000000          the plug: nothing below the port yet
cm.reenum.9x.noirq.walk=00010101          04:00 (bridge, Get_Child 0Dh, problem 1)
cm.reenum.9x.noirq.hold=01000101
cm.reenum.9x.noirq.refused=04000203
cm.reenum.9x.noirq.walk=000101FE          04:00 installed, restart question open: no child yet (FEh hold)
cm.reenum.9x.noirq.walk=00020201          answered No: 05:00 ALONE appears (problem 1)
cm.reenum.9x.noirq.refused=05000203
cm.reenum.9x.noirq.walk=000202FE          05:00 installed, its restart question open
cm.reenum.9x.noirq.walk=00030000          answered No: 06:00 (FL1100 #1, line 10) appears - nothing holds
cm.reenum.9x.noirq.refused=06000203
cm.reenum.9x.noirq.close=0210200A         closed 10 s after the last hold, 258 s from the opening
cm.reenum.9x.noirq.close.hold=010002FE
```

The walk was complete each time. After 04:00's restart question 98 SE
created ONE child devnode below it (05:00), not the five its secondary
bus carries; the next downstream port (05:08) appeared only after the
FL1100's wizard (opened at once for 06:00) had finished. So once 05:00's
child existed, no bridge read as being installed or childless and no
entry waited for its driver: the window closed during the FL1100's
wizard, before 05:08, 07:00 (FL1100 #2), 05:10, 08:00 (the ASM1142) and
the rest were enumerated. ME creates the siblings together (ME's first
plug starts all three, dock3), and a re-plug finds every devnode at once.


### The fix (record 28 sections 14.3 item 1 and 14.4)

- `11001768`: a function answering on the member's or a walked bridge's
  secondary bus (function 0, devices 0 to 31, through `PCI.VXD`) at a
  device where a complete walk placed no devnode holds the window
  (`CmNiUnenumerated`, the walk's locations kept up to `CM_WK_NI_MET`
  64), logged as the no-child hold (`FEh`; depth 0 for the member); the
  close and walk lines.
- `fa33bc19`: the walk goes below a bridge alone (or a devnode whose class
  is not read): a started controller's root hub (`USB\XHCI98_ROOT_HUB`,
  no PCI location) had left every walk incomplete, and an incomplete walk
  makes no such reading.
- `f76fb904`: only a function the window waits for holds (`CmNiAwaited`:
  a bridge, 010802h, 0C0330h; the childless-bridge reading too) - the
  I210's cancelled wizard left no devnode below 05:18 and held the
  `fa33bc19` build's window to the bound; a class read of all ones is
  "not read".
- `60535133`: README and release notes, record 28 section 14.7 (vectors,
  as read), 14.8.

Host: `test_cm_noirq` 510 checks, 0 failures (482 before). Builds
(`build-driver.cmd`, `XHCI98_SELFTEST_SKIP_OK=1`, slot gated.1): x86 `all`
of the diagnostic, of `11001768`, `fa33bc19` and `f76fb904`: BUILD + GATES
PASSED (release qemu) (`gated-x86-diag.log`, `-fix1`, `-fix2`, `-fix3`);
amd64 `release` of `f76fb904`: BUILD + GATES PASSED (release)
(`gated-amd64-fix3.log`). Review: four adversarial rounds; the first found
the root hub (fixed `fa33bc19`), the second an all-ones class read (fixed
`f76fb904`), the third (code) and fourth (whole branch with the docs)
approve, nothing material.

### Legs

Emulators: w48-dock2's (`E:\xhci98-scratch\w48\dock2\emu`) for the dock,
w48-chain's (`E:\xhci98-vm\p48\chain\bin`) for the chain. Kits
`E:\xhci98-vm\p48\dock4\xfer` (dock3's `xfer3` + the diagnostic qemu
`xhcicm.sys`), `xfer2` (`fa33bc19`, `C988BC08...`), `xfer3` (`f76fb904`,
`65749411...`); release package of `13493457` installed (`R9.BAT`), the
qemu drivers by `QP9.BAT`; 98 SE restart questions answered No (Tab,
Enter).

| system, VM | build | step | result |
|---|---|---|---|
| 98 SE `d98a` | diagnostic | fresh overlay, install, cold start, the TS3 Plus first plug | `close=0210200A` after 05:00's restart: 05:00 and 06:00 placed, 05:08 not yet created (the reading above) | the cause |
| 98 SE `d98b` | `fa33bc19` qemu | fresh overlay, the same first plug, every wizard (restart questions 20 s, the NIC's 50 s then cancelled) | held throughout (`000301FE`, `000501FE`); the reprocess held 568 s by the wizards, credited; `after=07000500`, `08000500`: both polled controllers started on the first plug; a stick on each of the three controllers enumerated at SuperSpeed; XHCITR report: the three controllers problem 0 (`d98b\serial-c-R1.txt`); the window then held by the I210's missing devnode (so `f76fb904`) | pass |
| 98 SE `d98c` | **`f76fb904`** qemu | `d98b`'s pre-plug disk, the fix3 drivers, the same first plug | `after=07000500`, `08000500` (reprocess credit 478 s), `close=021DC00A` (10 s after the last hold, the second FL1100's entry); report: the three controllers problem 0 (`d98c\serial-d-R1.txt`) | **pass** |
| ME `cme` | `f76fb904` qemu | fresh overlay (`-Usb4 adl`), install, cold start with nothing attached, the chain dock hot-plugged after the desktop (no black screen, no key), its wizards, then the first chained ASM2464 | `cm.chain.9x.number=00020801`, `reenumerate=00020800`, `hold=010002FE`, `after=06000300`, `06000500`, `close=020D200A`; F: (the chained drive): `copy` of `WIN.INI`, `fc /b` "FC: no differences encountered" (dock3's owed ME chained first plug) | **pass** |

User-visible strings (README line 160; release notes, "What 3.4.3.0
changes"): "... polling them (`XhciCmNoIrqStart`). On a dock's first plug
under 98 SE they start at the next plug." -> "... polling them
(`XhciCmNoIrqStart`), from the dock's first plug."

Not done (owed): the HP G2 and the TS3 Plus on 2000, 7 and 10 x86; the HP
dock's eject; ME's cold start with the dock attached at power-on (the
black text screen) not retaken - the hot plug after the desktop was used.
QEMU PIDs (all stopped): 84748, 186032, 173272 (`d98a`); 171104, 29028,
131836 (`d98b`); 186496, 185472 (`d98c`); 98200, 184016 (`cme`).

### Merge (w48-dock4)

Merged into `3.4.3.0` at `7f447757` (2026-11-10), reviewed, nothing
material; the integration build of `7f447757` passed.

## 48.35 (the chain, the x64 stall, ME) - S.8, S.10 and ME's standby (2026-11-10)

Task 48.35's legs after workers tbs2 and tbs3: the chain (record 22
section 11.1.12's S.8), the XP x64 / 7 x64 first shutdown after a
hibernation with a tunnelled drive (S.10), and ME's standby on q35 and
on `pc`. Branch `w48-tbs4` from `3.4.3.0` at `7f84dbf5`
(`XhciCmSleepEntry` 1 by default), no commit: the one failure read on
this package's code (ME) was not chased to a fix in this run, below.

**Build and kit.** Gated `build-driver.cmd release` x86 and `release
-amd64` of `7f84dbf5` (`XHCI98_SELFTEST_SKIP_OK=1`), each `BUILD + GATES
PASSED (release)` (`E:\xhci98-scratch\w48\tbs4\gated-x86.log`,
`gated-amd64.log`); `make-package.ps1 -Arch x86` and `-Arch amd64`
(unsigned), laid out by `ConvertTo-FamilyLayout`; `xhciinst`, `xhcitr`
and `xhcimark` built from this tree, the root files from tbs2's kit
(`INSTALL.BAT` unchanged since). x86 `xhcicm.sys` `cbcf2c2bb038cf0d...`,
`xhci98.sys` `50edb88cbf18e70e...`, `stor98.sys` `7f99b57a4578d09c...`;
amd64 `xhcicm.sys` `94339edcc3a693fd...`, `xhci98.sys`
`b557d1d15e4f2fa2...`, `stor98.sys` `f5393994fd116ecf...` (before the
in-guest signing). The amd64 catalogs made unsigned on the host with the
packager's `New-PackageCatalogFile` (tbs3's `cat64.ps1`); on 7 x64 the
kit's `SIGN7.BAT` (pci6's, WDK 7.1 amd64 `MakeCert`, `CertMgr`,
`SignTool`) made a throwaway "CN=xHCI98 leg throwaway tbs4" in the guest,
trusted it as root and publisher, and signed every `.sys` and `.cat` of
`nt-x64\` (16 "Successfully" lines). No project or owner key used.

**Vehicle.** Development host A, TCG, QMP 47840 to 47843, at most two
guests at once. q35 guests through a copy of the shared
`launch-q35.ps1` of 2026-11-10 (the ASM4242 tunnel ports with
`power_controller_present=off`, board-faithful; `-Vga` added,
`E:\xhci98-vm\p48\tbs4\tools\launch-q35.ps1`), the fixed SeaBIOS
`E:\xhci98-vm\p48\q35s3\bin\bios-256k.bin` (SHA-256 `e5dc0550...`,
checked at every start). The chain on the chain workers' emulator
`E:\xhci98-vm\p48\chain\bin\qemu-system-x86_64.exe` (`396e4de45b68e1bb...`):
at power-on the dock (`usb4-device`, `downstream-ports=1`,
`chain-power-controller=off`, NVMe) and behind it the ORICO model
(`parent=enc0,port=1,device-model=asm2464`) with a second NVMe, an
overlay of `E:\xhci98-vm\p48\tbs4\disks\nvme-b.qcow2` (the FAT test disk
with its MBR disk signature changed, `e3943d73...`, so that 7 does not
set the second disk offline). ME on `pc` (i440FX, the stock SeaBIOS)
from `vm\t37me\winme-stock-base.qcow2` with S.1's topology
(`usb4-tunnel-bridge` 00:05.0 with SHPC, `usb4-host
profile=usb4-generic` 00:04.0, the ORICO model at power-on, `qemu-xhci`
00:06.0), the pinned emulator `asm4242-20261025-2230`
(`tools\gome.ps1`, `gopc.ps1`). The leg script `xfer\ONE.CMD` (tbs3's,
every volume holding `TESTDATA.BIN` written and compared before and after,
the report's chain lines added); 98 SE / ME `ONE9.BAT`. 7 x86 standby
legs with `-Vga none` (as tbs2).

### Legs

| Leg | Guest, vehicle | `XhciCmSleepEntry` | Read | Verdict |
|---|---|---|---|---|
| S.8 a, S3 | 7 x86 (fresh overlay `ca`), q35, the chain emulator, `-Usb4 asm4242`, the dock and the ORICO behind it at power-on | 1 (the INF default) | 119 devices before and after; two volumes `D:` (the dock's NVMe) and `F:` (the chained one) on Stor98, `WAA.BIN` written and compared on both, after the resume compared again and `WAB.BIN` written and compared on both; `u4.sleep.entries 1`, `.routers 3`, `.ready 3`, `u4.sleep.entered=00000003` (the host router, the dock and the chained ASM2464 each Sleep Ready), `cm.suspend=00000004`, `cm.resume=00000001`, `cm.reenum.resumed=00000001`; `xhcimark`: "suspend notice 4: D: flushed", "F: flushed" | **pass** (the shutdown after it: see below) |
| S.8, ADL | 7 x86 (`c7`), the same chain on `-Usb4 adl` (the chain workers' profile) | 1 | the chain built (`cm.chain.*`, `u4.kid.adopted`), both NVMe functions enumerated, but both stayed at problem 28: the router reports "Port 1 (bridge 00:05.0): not active, no device", and `xhcimark` logs "Stor98: ... (service none): not below a verified tunnel port" for both; the same with `-NoXhci` (no qemu-xhci root port at 00:1C.0, which the first boot also listed as tunnel port 3, "active") | **not readable** on this profile (no volume); a finding, below |
| S.10 h, S4 | 7 x64 (q35 base, test signing on; fresh overlay `x7`), `-Usb4 asm4242 -EncAtBoot` (the ORICO at power-on), the x64 package signed in the guest, installed `/thunderbolt /stor98` (exit 3) | 1 | the ORICO volume `D:` on Stor98, `WHA.BIN` compared before and after the hibernation, `WHB.BIN` written and compared; `u4.sleep.entries 1`, `.routers 2`, `.ready 2`, `cm.reenum.hold.passed` for 0200 and 0208; "suspend notice 4: D: flushed"; **the first shutdown after the resume (`shutdown /s /t 3`) powered off in 16 s** | **pass: no stall with Stor98** |
| ME, q35 control | ME q35 base, fresh overlay, no package, no USB4 | - | Start, Shut Down offers Stand by; Stand by and `SetSuspendState` return at once (TRUE), no QMP `SUSPEND`: the guest never reaches S3 | **not readable on q35** (as 98 SE's q35 base, by another symptom) |
| ME, `pc` control | ME stock on `pc`, fresh overlay, no package, no USB4 | - | `SUSPEND`, `system_wakeup`, `WAKEUP`, the desktop back | S3 works on `pc` |
| ME a, S3 | ME on `pc` with S.1's topology and the ORICO, the package installed (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`, exit 4: the ME USB-storage notes; Stor98's wizard answered) | 1 | the ORICO volume `E:` on Stor98, `WaA.BIN` compared; `cm.usb4.sleep=00000101`; `SetSuspendState` returned TRUE at once, no `SUSPEND`, the screen went to a black text page and stayed there (10 min); quit over QMP | **fail: standby never completes** |
| ME x3 | the same overlay booted with no USB4 host (qemu-xhci only) | 1 | `SUSPEND`, `WAKEUP`, the desktop back; Shut Down worked | pass (xhci98 and the rest of the package are not the cause) |
| ME n1 | the same overlay, the USB4 host and its tunnel bridge, no enclosure | 1 | `SUSPEND` at once; after `system_wakeup` a black text page for 3+ minutes, the processor halted in a loop at `ff03a3cd`; quit | **fail: no resume** |
| ME c1, control | fresh ME overlay on `pc`, no package, the USB4 host and bridge (the NHI with no driver), no enclosure | - | `SUSPEND`, `WAKEUP`, the desktop back | pass: the hang needs `xhcicm.sys` |

**The shutdown after the 7 x86 standby legs.** On both chain overlays
(`ca`, `c7`) the shutdown asked after the leg (`SLP off`, `ExitWindowsEx`)
never powered the guest off, and 7 slept again at its two-minute
unattended timeout. Not this package: 7 x64's leg showed why - after a
resume 7 shows its lock screen ("test, Locked"), and an `ExitWindowsEx`
from the locked session reports success and does nothing; tbs2's legs
used `shutdown /s /t 3`, which works there. After unlocking (password
`test`) `shutdown /s /t 3` powered 7 x64 off in 16 s. `ONE.CMD` now uses
`shutdown /s /t 3` on 7 and later. The chain legs' first shutdown was
therefore not read; the shutdown with the chain and no sleep before it
took 6 s (`c7`, tag `dg`).

**ME: a finding on this package's code (not fixed here).** On ME (`pc`,
S.1's topology) with `xhcicm.sys` on the USB4 router: with the ORICO
attached, Stand by never reaches S3 and the machine sits on a black text
page; with no enclosure S3 is entered but the resume never returns to the
desktop. Without the router's driver (same machine, no package) and with
the package but no router, both work. The README already keeps "stop and
unplug" for ME (tbs2), so this is no regression of a promise; the cause
is unread (the release build logs nothing to the debug port; the next
step is the qemu flavour's `xhcicm.sys` on the `mep` overlay, debugcon
over a standby with no enclosure, to find where its 9x power path stops).

**The ADL chain: a finding.** On `-Usb4 adl` (the Intel ADL model with
its pre-OS connection manager, the chain workers' profile) 7 x86 never
reads tunnel port 00:05.0 active with the dock and its chain behind it,
so the mark service refuses Stor98 to both NVMe functions; on the ASM4242
host the same chain gets Stor98 on both and every leg reading above. The
chain workers read ADL only on 98 SE, ME and XP's wording, where
Stor98's binding takes another path. Unread whether it is the ADL model,
its pre-OS tunnel (`u4.preserved=00010809`, `u4.kid.adopted=00010809`)
or this driver's active-port rule; for the chain workers.

Not read, owed: S.8 at 0 and S4 (the ASM4242 chain), the chain's first
shutdown after a standby, XP x64 (S.10's second guest; 7 x64 read
instead, the in-guest certificate route), ME's cause and any ME leg past
it, S.1 on the NT targets, S.2/S.3 (the q35 native port, 7 x64 and 10
x64), S.5 (Titan Ridge), S.6 (`intel-adl`), and the bench.

Evidence: `E:\xhci98-vm\p48\tbs4\` (`ca\`, `c7\` (the chain), `x7\` (7
x64), `mep\`, `mec\`, `mepc\`, `meq\` (ME): `serial-*.log`,
`cmdline-*.txt`, `seq-*.txt`, `ev-*.txt` (QMP events), screens,
`pids.txt`; `c7\dg-report.txt` the ADL report), the scripts in `tools\`
(`go.ps1`, `gochain.ps1`, `gome.ps1`, `gomex.ps1`, `gopc.ps1`,
`inst7c.sh`, `inst64.sh`, `seq7c.sh`, `seq64.sh`, `leg9pc.sh`, `qev.py`,
`keys.py`), `xfer\ONE.CMD`, `xfer\DG.CMD`, `xfer\KIT\SIGN7.BAT`; every
QEMU start and stop in `pids-all.txt`.

### Bench round 8 (2026-11-10, image `444b2779`, w48-net8) (worker r8)

The owner's round 8 (`temp\p48-tbnet\round8\`): the P14s on 98 SE, the
Framework on Windows 11, XHCICM.SYS `444b2779` (496,964 bytes), XHCITR
`3ffb7715`'s. Each run was a restart, then `ping -n 20` and TBSPEED 30 s
each way with `-w 64` and `-w 256` (SO_SNDBUF and SO_RCVBUF on both ends),
then SAVE:
- **S1:** polled, as installed.
- **S2:** IRQON (`XhciCmRingInterrupt` 1, `XhciPfwNhiLine` 0).
- **S3:** WIN64K (MSTCP `DefaultRcvWindow` 64240), the interrupt still on.
- **S4:** IRQOFF, the 64K window kept.

`MSTCP0.REG` (saved before WIN64K) has no `DefaultRcvWindow`, so S1 and
S2 ran with 98 SE's default window. Inputs: the Framework's
`SPEED8-W.TXT`, the P14s's `SPEED8.TXT`, `S1` to `S4` (REPORT.TXT,
IPCFG.TXT) and one `W11-20261110-081401` collection (taken before S1).

#### The table (Mbit/s; "in" = Framework to P14s, "out" = the P14s sends)

The totals are TBSPEED's, and the two ends agree to 0.1 % wherever the
test ended normally.

| Run | in -w64 | in -w256 | out -w64 | out -w256 | ping avg (min-max), ms |
|---|---|---|---|---|---|
| S1 polled | 9.98 (9.5-11.7) | 10.04 (9.1-12.1) | 33.5 (14-44) | 12.9 (7-30) | 43 (10-85) / 10 (8-12) |
| S2 irq | 184.6, **stalled** | 251.5 (229-273) | 22.3 (2-30) | 96.5 (17-450) | 44 (8-96) / 50 (8-98) |
| S3 irq + 64K | 10.4, **stalled** | 13.8, **stalled** | 37.5 (30-107) | 83.0 (51-107) | 16 (3-35) / 23 (2-50) |
| S4 polled + 64K | 100.7 (92-121) | 96.9 (94-101) | 14.1 (9-25) | 14.0 (4-22) | 36 (9-99) / 52 (12-114) |

The coordinator's figures are right as totals, but three cells are not
rates:
- **S2 in -w64:** 229 to 262 every second for 22 s, then 0.
  - Framework: 713.36 MB sent, then "nothing came within 30 s" for the count.
  - P14s: 713.23 MB, then "Receiving: nothing came for 30 s".
- **S3 in -w64:** 39.06 MB in the first 1.77 s (177 Mbit/s), then 0 for 28 s.
  - The P14s: 38.92 MB in 2.26 s, then "nothing came for 30 s", and it closed the connection.
- **S3 in -w256:** 52.17 MB in 1.97 s (212 Mbit/s), then 0.
  - The P14s: 51.86 MB in 2.44 s, then the same.

So S3 did not run slowly. It ran at about S2's rate for two seconds, and
then the connection stopped dead. Each time, what never arrived was 0.13
to 0.31 MB: about one window plus one socket buffer.

TBSPEED reports no retransmissions, and 98 SE gives no TCP counters, so
this round cannot count retransmits.

#### Frame accounting

The frame counters are xhcicm's, against TBSPEED's bytes, taking 1,460
bytes a segment and 1 MB = 10^6 bytes:
- **The frames carried one segment each.** In every run, the frames out
  equal the outbound segments plus one 98 SE acknowledgement per 1.9 to
  2.0 inbound segments. The frames in equal the inbound segments plus
  Windows' acknowledgements.
  - Windows 11's MTU is 62000 (`if21-ipif.txt`) and LSO v2 is enabled, but its adapter sent 1514-byte frames at 98 SE's MSS.
  - So no multi-frame packets arrived, and the receive split (record 35 section 7.4) did not run.
- **Windows 11 acknowledges sparsely.** It sent one ACK per 15.8 (S1),
  8.2 (S2), 18.0 (S3) and 19.0 (S4) segments from 98 SE: 12 to 28 KB an
  ACK. `if21-advanced.txt` shows RSC unset.
- **Logins:** Windows' Login count (506 in S2, 514 in S3, against 8 and
  4 polled) is not a difference in behaviour.
  - `cm.net.login.id` was already 0x1FA and 0x202 when the service came up, about 4.1 s after the driver loaded.
  - Windows retries its Login every few milliseconds until it is answered. With the interrupt every retry is seen; polled, ring 0 is read every 200 ms and the rest are not.

#### S3: what the report shows

- **The interrupt was alive at SAVE.**
  - "Ring interrupt: on (17901 wake-ups)".
  - The start's self-test passed (`cm.irq.selftest=00000000`, 0 ms; S2's took 13 ms).
  - `cm.irq.armed=00001000`, `cm.net.irq.open=00000001`.
  - No `cm.irq.storm`, `.lost`, `.suspect`, `.down`, `.dpc.slow`, no `cm.net.irq.latched` and no `cm.net.link.down` in either log.
  - The counters `CmRirqLogCounters` writes appear only at a stop, so the per-cause claim counts are not in this report.
- **The few wake-ups follow from the few frames.** Wake-ups per frame
  in: S2 314,669 / 1,171,680 = 0.27; S3 17,901 / 79,415 = 0.23.
  - S3 carried only about 4 s of inbound data in all, so it has fewer wake-ups.
  - The receive path was not half-dead: the next test in the same boot (98 SE sending, Windows' ACKs coming in through the interrupt) ran at 37.5 and 83 Mbit/s.
- **No re-plug or re-login.**
  - Link changes 1 up, 0 down; logins 2 sent, 0 failed; services 1 up; discovery restarts 1 - as in S2.
  - The address changed because the MAC did (96-DD-D3-5C-56-60 in S1 and S2, 66-DD-15-D0-76-90 in S3 and S4), and 98 SE's automatic address follows the MAC.
  - This NHI has no Device Serial Number (`cm.net.dsn.none`), so `tbtIcmXdIdentity` makes the MAC at each start from the clock and ring 0's bus address (`cm.xdp.mac.random` D35C566B in S1 and S2, 15D0769C in S3 and S4).
  - The same boot path gives the same value. WIN64K changed the boot, and so the value [inf].
  - This is record 35 section 4.4's design, not a fault of this round.
- **No ring overflow or refusal shows:**
  - "0 dropped" (no bad-PDF frame, none unheld);
  - "0 refused" on transmit;
  - no `cm.net.rx.pdf`.
  - The miniport's own counters (its drop reasons, `RxNoBuffer`, `RxSplit`) are "not given by the network driver": the P14s runs 3.4.2.0's xhcinet.sys, which predates the stats callback (`8db64c87`, 3.4.3.0 only).
- **78 frames with errors**, in S3 only: 0 in S2's 1,171,680 and 0 in S4's 511,349.
  - "With errors" is the descriptor's CRC Error or Buffer Overflow bit (`CmNetrRxTake`), the two not told apart.
  - xhcinet drops each one (`NetRxFrame`), so each costs at least one segment.
  - They came only when 44-segment windows arrived at about 200 Mbit/s.

#### S3 verdict

**The interrupt did not fail. The stall is a TCP connection stopping,
and its cause is not proven.**
- **It is not the ring interrupt dying, a storm, the latch or a fallback.** The interrupt stayed armed and open, and its wake-ups per frame match S2's.
- **It is not the data ring wedging.**
  - The 1 ms poll timer runs beside the interrupt: `cmNetLink` arms it at link up whether the cause is open or not.
  - So frames left on the ring are taken within a tick, and the ring cannot hold a stall for 30 s.
  - The same boot carried the next connection at full speed.
- **It is not the ring size.** A 64240-byte window is 44 frames against `XhciCmNetRxFrames` 128, and the transmit ring was never refused.
- **It is not the 64K window alone.** S4 (polled, 64K) moved 741 MB at 100 Mbit/s with 0 errors and no stall.
- **The bench sequence did not cause it.** The address change is the per-start MAC, and S4 after it is clean.
- **The stall goes with inbound rate, which only the interrupt reaches.**
  - With the interrupt: S2 -w64 stalled after 488,000 segments at 250 Mbit/s, and S2 -w256 ran 30 s clean. S3 stalled after 27,000 and 36,000 segments at about 200 Mbit/s with the 64K window.
  - Polled: no inbound test stalled (S1 and S4, 0 of 4).
  - So the 64K window makes a stall about 35 times likelier per segment at these rates, and adds the errored frames. But S2's stall had no errored frame, so the errors are not needed for a stall.
- **The bytes that never arrived were Windows' whole outstanding data**
  (a window plus its socket buffer).
  - For 30 s and more, the P14s's TBSPEED read nothing in order, though it kept running: it printed every 1.5 s and closed at its idle limit.
  - Meanwhile, small frames kept flowing on the same link: the next test's SYN, the ACKs, the pings.
- **Candidates, in the order the data favours them** (none proven):
  1. **98 SE's MSTCP** fails to recover a loss at this rate, so Windows' retransmissions are not accepted.
  2. **The path stops carrying full-size frames while small ones pass.** The end-to-end credit is the suspect: the CRC or Overflow frames appear only with the largest bursts. But a credit leak would not explain the next inbound test, after the SPEED8 restart, starting at full rate.
  3. **xhcinet's receive pool with MSTCP holding packets** (out of order after a loss), so the hole's retransmission meets a pool indicated with `NDIS_STATUS_RESOURCES`. It is unlikely at an 8760-byte window (S2), and `RxNoBuffer` is not readable with 3.4.2.0's xhcinet.
- **Not a driver defect on this evidence, and nothing is changed.**

#### Outbound verdict (98 SE sending)

- **It is not the driver's transmit path.**
  - xhcinet copies each packet into a reservation and completes the send at once (`netSendOne`, `NetServe`).
  - `cmNetTxReserve` reaps inline, and "0 refused" in all four runs says the 128-frame transmit ring never filled.
- **The limit is 98 SE's TCP sender against Windows 11's sparse ACKs.**
  - Windows ACKs every 8 to 19 segments (above).
  - A 1998 Reno sender grows its window per ACK, not per byte acknowledged, so with stretch ACKs it grows 8 to 19 times slower, and each loss collapses it [inf].
  - S2 -w256 shows this: 160 to 450 Mbit/s for the first 6 s, then 17 to 135 for the rest.
  - Polled, every ACK also waits up to one poll, so the round trip is the tick: 14 Mbit/s flat in S4 [inf].
- **It varies from 13 to 96 Mbit/s** because it depends on when the first loss comes. `-w` does reach the send side: 256 beat 64 with the interrupt (96.5 against 22.3, 83 against 37.5), but not polled.
- **The interrupt helps outbound on average:** 22 to 97 Mbit/s against 13 to 34.

#### Ping

Isolated pings at 1 s took 2 to 114 ms in every mode, and the interrupt
did not lower them. Under load, the round trip is sub-millisecond:
250 Mbit/s through an 8760-byte window means at most 0.28 ms a window.
Linux on the same pair took 0.9 to 2.8 ms (round 7). So the wait sits
on an idle path, not in the receive ring's poll. Which side, and which
state, is not read.

#### Defaults (evidence for the owner, who decides)

- **`XhciCmRingInterrupt`: leave it 0** (as shipped) for the P14s class until the stall is explained.
  - On: inbound 25 times faster at the default window (250 against 10 Mbit/s), and outbound better on average.
  - But 3 of the 4 inbound tests with it on ended in a connection that stopped for more than 30 s, against 0 of 4 polled.
  - A stalled copy is worse for a user than a slow one.
- **`XhciPfwNhiLine`: leave it 1.** It matters only as the interrupt's precondition on 98 SE and ME (record 35 section 5.5). Nothing in this round asks for 0 without the interrupt.
- **Receive window: `DefaultRcvWindow` 64240 is the round's one clean gain.**
  - Polled, inbound went from 10 to 100 Mbit/s, steady every second, with 0 errors and no stall; outbound was unchanged.
  - It is worth a `tuning.md` line for 98 SE and ME if the owner agrees.
  - Not together with the interrupt until round 9: that pairing stalled at once in S3.
  - 98 SE ignores SO_RCVBUF for its window: `-w 256` = `-w 64` inbound in S1, S2 and S4.
- **`XhciCmNetRxFrames` stays 128.**
  - Record 35 section 5.5's trigger is Buffer Overflow or `RX_NO_BUFFER` moving.
  - Overflow cannot be told from CRC in "with errors", and `RX_NO_BUFFER` was not readable. Neither is shown, so nothing asks for 256.

#### Round 9 (what it needs)

1. **The 3.4.3.0 xhcinet.sys on the P14s with this xhcicm**, so that the
   miniport's statistics (drop reasons, `RxNoBuffer`, `RxSplit`) reach
   the report.
2. **A diagnostic xhcicm image** that logs the first CRC and the first
   Buffer Overflow frame apart (once each, as `cm.net.rx.pdf` is), with
   `cm.ms`. A second SAVE straight after a stall then dates them.
3. **On the Framework, a capture during S3's conditions** (`pktmon` on
   the USB4 P2P adapter).
   - It shows whether Windows retransmits into the stall, what 98 SE acknowledges (a zero window, SACK blocks, nothing), and the ACK spacing.
   - Plus `netstat -s` on the P14s before and after the test.
4. **During a stall** (start it with WIN64K plus IRQON), from the Framework:
   - `ping -n 10 -l 32` and `ping -n 10 -l 1472` to the P14s, and a second TBSPEED inbound.
   - If small pings pass and full-size ones do not, the path or credit leads (candidate 2). If both pass and a new connection runs, it is TCP (candidates 1 and 3).
5. **The interrupt on with `XhciCmNetE2E` 0 and the 64K window.** If the stall and the errors go, end-to-end flow control leads.
6. **The interrupt on with windows of 16 KB and 32 KB** (two more WINnnK files), to place the threshold between S2 and S3.
7. **Outbound:** one run with Windows' `TcpAckFrequency` 1 on the USB4 interface (if Windows 11 still honours it). If 98 SE's sending rises and steadies, the stretch-ACK reading holds.
8. **Ping:** `ping -n 20` from the P14s to the Framework, and from the Framework during an inbound test, to place the idle latency.

No commit. The image under test is `444b2779` (w48-net8, not merged); the
receive and send paths it exercised (`cm_net.c`, `cm_netr.c`, xhcinet's
`n5_data.c`, `net_frame.c`) are the same as on 3.4.2.0 for the miniport.

The owner's answers of the day: a round 9 to find the stall; the ring
interrupt stays 0 by default; `tuning.md` carries the 98 SE and ME receive
window as a tip.

From the roadmap (2026-11-10): the owner's answers to record 22 section 11.1's questions: Q1 (a), Q2
the flush on NT, U34 to U36 decided, Q7 (b). `XhciCmSleepEntry` was 0 until the S3 and S4 legs on 7,
8.1 and 10 x86 with the ASM4242 model passed at 1 and at 0, then 1 by default (owner, decided in
advance; merge `1b73216e`), the README's stop-and-unplug lines narrowed to that, with the device
attached at power-on and pre-boot support on (`8043a417`). The 2000 legs: hibernation passes at 1
(Enter Sleep and Sleep Ready on both routers) and at 0; standby is not readable on the 2000, XP x86
and 98 SE q35 bases (Windows refuses it without the package too). `xhcimark`'s flush (its log in
`system32\LogFiles\xhci98`): on 7 x86 the suspend notice flushed the drive at every leg; on 2000 the
query notice's flush completes and the suspend notice's at a hibernation is logged as not finished
in its bound (best effort, as designed). ME at 1 enters S3 and resumes with the drive back and
written (an earlier 'hang' was a test script); at 0 the drive is lost after the resume on ME and 98
SE, so 1 stays the default on 9x.

## The installer restarts 98 SE and ME after a Stor98 update (2026-11-10, w48-rst)

The owner's decision (2026-11-10): the installer offers a restart on 98 SE
and ME too, as on every other system. Its shutdown forms on 9x came from
the emulator (98 SE and ME guests in this project's QEMU do not survive a
warm restart), not from real hardware. Branch `w48-rst` from `3.4.3.0` at
`4fbe568b`.

**The change.** Commits `45ea7b6b` (installer, host suite, records 23 and
26, the package readme, the release notes) and `ca75e2ca` (Stor98's
README and release history):

- The window's Finish page on 98 SE and ME after a Stor98 update (and on
  98 SE after the storage drivers' copy with a UAS devnode at Code 2)
  offers "Restart Windows now" / "Restart Windows later" and calls
  `ExitWindowsEx(EWX_REBOOT, 0)`; "Shut down now" / "Shut down later"
  (`INST_PT_SHUT_NOW`/`_LATER`, ids 31 and 32 left unused), `INST_FV_SHUT`
  (value 1 left unused), the Help topic `INST_HELP_FINISH_SHUT` (the later
  topics renumbered, `INST_HELP_COUNT` 19), `inst_eng_shutdown`,
  `stor_cold`, `s98Cold`, `inst_s98_cold_start` and
  `inst_final_text_cold` are gone; `inst_final_text_shown(line,
  stepsShown)` lost its shutdown argument.
- Final lines, both front ends: "Done. Please shut down and start Windows
  again." -> "Done. Please restart Windows now."; "Not everything is done:
  follow the steps above first, then shut down and start Windows again."
  -> "... then restart Windows."; the window's "... see the steps in the
  log, then shut down and start Windows again." -> "... then restart
  Windows.". Exit codes unchanged.
- Other 9x texts: the Summary paragraph "Windows is then shut down and
  started again, not restarted." -> "Windows is then restarted."; the
  console plan "Windows is then shut down and started again." ->
  "Windows is then restarted."; "A UAS disk with Code 2 starts after
  Windows is shut down and started again." -> "... after Windows
  restarts."; the NUSB Code 2 advice "...the disk starts after Windows is
  shut down and started again." -> "...after Windows restarts."; the 9x
  steps "Shut down Windows and start it again, then run Setup again" ->
  "Restart Windows, then run Setup again" (a Stor98 job the helper could
  not do; an install cut short).
- Kept: the Summary box "Disconnect all external NVMe drives before
  Setup continues." (console `INST_Q_S98_UNUSED`). It serves Stor98's
  readme's separate precaution that nothing uses the drive's volume
  during the update, not the shutdown.
- Records: 26 draft 78 (sections 5.9, 5.10's table and pinned labels,
  6.3, 6.5.5, 7, 11's open items, 12, W5, W22); 23 revision 55 (sections
  6.4 and 7.4). `xhciinst\README.TXT`, the release notes' 3.4.3.0
  "Installer and package", `src\stor98\README.md` "Updating the driver"
  (the known issue reworded as seen in an emulator only; no longer asks
  for a shutdown) and Stor98's `releases\history.md` 3.4.3.0 entry.

**Host suite.** `test\run-host-tests.cmd`: PASSED (test_inst 9105 checks,
test_trset 1975, test_tr 1794, 0 failures). The shutdown-line and
`inst_s98_cold_start` checks removed; the help routes, labels and
paragraph counts follow the renumbering.

**Review.** Reviewed against `4fbe568b`: approved, no findings.

**Builds** (`build-driver.cmd release`, `XHCI98_SELFTEST_SKIP_OK=1`, slot
gated.3, taken and released): x86 PASSED, amd64 PASSED, at `ca75e2ca`.
`xhciinst\build.cmd x86`, `xhciinst\di\build.cmd` and `make-package.ps1
-Arch x86` ok.

**The ME leg** (development host A, the pinned emulator
`asm4242-20261025-2230`, TCG, one vCPU, 512 MB, headless, QMP
127.0.0.1:47830; `E:\xhci98-vm\p48\rst\me\`, `launch-a.cmd`, `pids.txt`).
The disk is a copy of w48-stor3's ME overlay (`p48\stor3\fme`: the q35 ME
base with this package and an earlier 3.4.3.0 working build of Stor98 in
place), the ASM4242 model with the ORICO enclosure and its NVMe at
power-on, the internal NVMe left out, a `usb-tablet` added. The kit is
this branch's `make-package.ps1 -Arch x86` staging in the family layout,
with this branch's `XHCIWIZ.EXE`, `XHCIINST.EXE`, `XHCIDI.EXE` and
`SETUP.EXE` (`stor98.mpd` `AC194E06...` against the guest's
`4E905335...`). Started pid 168512.

- `D:\KIT\SETUP.EXE` from Run: Welcome lists "Stor98 Storage Controller for
  NVMe SSDs ... Replace files (same version)" (`s4.png`); Components and
  the Thunderbolt and USB4 page as they were, Stor98 ticked.
- Summary: "If the box below is ticked, Stor98's driver file (stor98.mpd)
  is replaced with 3.4.2.0 at the next start; no drive is reinstalled.
  Windows is then restarted. If unticked, Stor98 is updated in a later
  run." (`s8.png`). Install: the box "Disconnect all external NVMe drives
  before Setup continues." with OK and Cancel (`s9.png`); OK.
- The run: "Stor98 3.4.2.0 replaces C:\WINDOWS\SYSTEM\IOSUBSYS\stor98.mpd at
  the next start; no drive is reinstalled." (`s13.png`); a Steps page for
  an old "PCI Universal Serial Bus" entry, so exit 4.
- Finish: "Not everything is done: follow the steps above first, then
  restart Windows.", the radio buttons "Restart Windows now" and "Restart
  Windows later" (later preselected, steps being left), and "Exit code 4."
  in the text (`s11.png`, `s12.png`); Help shows the restart topic
  ("Restart Windows now: Finish restarts Windows, ...", `s14.png`).
- "Restart Windows now", Finish: Windows restarted; with `-action
  reboot=shutdown` the reboot ended QEMU (pid 168512 gone), so the warm
  start itself was not watched, as expected here.

Seen in passing (not changed): the Summary's "If the box below is
ticked" sentence predates the OK/Cancel box at Install (record 26 section
5.0's flow) and no longer matches it.

### Merge (w48-rst)

Merged into `3.4.3.0` at `b1173bfe` (2026-11-10), reviewed, nothing
material; the integration build of `b1173bfe` passed. The owner's decision:
the shutdown forms on 98 SE and ME came from this project's emulator, where
a 9x guest does not survive a warm restart.

## 48.27 record 32 section 11.5.6's other 10 x86 legs and the 8.1 previews (2026-11-10, worker pci7)

Build under test: branch `w48-pci7` at `c6281b26` (the `3.4.3.0` tip, 48.27's code merged), unchanged. `src\pfw\build.cmd`, `xhciinst\di\build.cmd` and `xhciinst\build.cmd` first, then `scripts\build-driver.cmd release` (x86, slot gated.1, `XHCI98_SELFTEST_SKIP_OK=1`): **BUILD + GATES PASSED** (`E:\xhci98-scratch\w48\pci7\gb-x86.log`); `make-package.ps1 -Flavor release -Arch x86`, then `ConvertTo-FamilyLayout` to the kit `E:\xhci98-scratch\w48\pci7\k10` (`k10b`, `k10c` the same with the batches below changed or added), `XHCIINST.EXE`, `XHCIWIZ.EXE`, `SETUP.EXE` and `INSTALL.BAT` from this tree. `XHCIPFW.SYS` `7F9BB418...7CDB` (13,673 bytes; the installer's record and log read the same hash), `xhci98.sys` `F905C331...7ACC`, `xhcicm.sys` `D0EF1C72...89AD`, `XHCIINST.EXE` `17F19913...03C6`. The installer's version string is still 3.4.2.0 (the tree's).

Vehicle: development host A; pci's 4G-off launcher (a copy, QMP on TCP 47960 and 47961), emulator `asm4242-20261026-4goff`, `-Usb4 asm4242 -AsmReserve4gOff -EncAtBoot -Tablet` (the ORICO at power-on), two processors, 2 GB; a fresh overlay of the clean q35 base for every leg's guest, 10 x86 read from the guest as build 19045.2965 (`CurrentBuild` 19045, `UBR` 0xB95), 8.1 x86 as build 9600. Installs `I.BAT <tag> /console /yes /nopause /thunderbolt /nostor98` (INSTALL.BAT, its output to `C:\<tag>.log` and COM1) from an elevated console (10: Run box, `cmd`, Ctrl+Shift+Enter, Alt+Y at the consent box, which the screendump shows on this base; 8.1: Win+X, A, Alt+Y); each "Windows can't verify the publisher" box clicked by a screendump watcher. `Q.BAT` to COM1 after each step: the build, `SafeBoot\Option`, the `xhcipff` and `xhcipfw` services with their `Parameters`, `UpperFilters`/`LowerFilters` of `ACPI\PNP0A08\0`, both records (`XhciPfwFilter`, `XhciPfw`), the images in `drivers`, `HiberbootEnabled`, the last three Kernel-Boot event 27 ("boot type", 0 a full start, 1 a fast-startup start), the problem codes and every PCI function with its service, the disks, `stornvme`. Every Q read is the new one by its `==== Q start <time>` line. A cold start is a guest shutdown (`shutdown /s`) and a new QEMU process on the same overlay (QMP `system_powerdown` was ignored at the desktop on this base once, so the guest's own shutdown is used).

| Leg | Guest | Result |
|---|---|---|
| install | `a` | exit 2 ("RESTART Windows to finish"); service `xhcipff` Type 1, Start 0, ErrorControl 0, Group `PnP Filter`, ImagePath `System32\DRIVERS\xhcipff.sys`; `UpperFilters` `xhcipff` on `ACPI\PNP0A08\0` (no other root bus here); record `XhciPfwFilter` `File` `7F9BB418...7CDB`; root port 00:03.0 (`...&18`) at problem 12 before the restart. **Pass** |
| the installer's restart, then three cold starts | `a` | every start: `XhciPfwRole` 2, `XhciPfwMode` 2, `XhciPfwSkipped`/`SkippedAny` 0 (no "late" bit), `XhciPfwRootBuses` 1, `XhciPfwBuses` 0 to FFh, `XhciPfwClosed` 9, `XhciPfwVersion` "XHCIPFW.SYS 3.4.2.0", `XhciPfwHoldTicks` new each start; all 18 PCI functions at problem 0 (both root ports, the ASM4242 switch, its xHCI on `USBXHCI`, the NHI on `xhcicm`, both enclosure bridges, the NVMe on `stornvme`, RUNNING), only `ACPI\QEMU0002` at 28. Cold start 1 with a `pci_cfg_write` trace (`a\trace-c1.log`): the step's close of 00:03.0 (`@0x28 <- 0xffffffff`, `@0x2c <- 0`, `@0x24 <- 0xfff0`, `@0x28 <- 0`) at lines 444 to 447, the PCI driver's probe of 00:03.0 (`@0x20..0x30 <- 0xffffffff`) at 590 to 596: close before probe, the same lines as 47.14's start c (446 against 593). **Pass** |
| fast startup on: the installer's restart, then shut down and power on | `b` | `HON.BAT` first (`powercfg /h on`; `HiberbootEnabled` was 0 on this base, set to 1: fast startup available). Install, the installer's restart: boot type 0, step ran (HoldTicks 2CD468h, Closed 9), all at 0. Then `shutdown /s /hybrid` (the Start menu's shut down with fast startup on) and a new QEMU process: boot type **1**, `XhciPfwHoldTicks` unchanged (2CD468h: the step did not run), every function at problem 0 as the full start left them, the NVMe on `stornvme` RUNNING. **Pass** |
| fast startup on: a fresh overlay installed and shut down instead of restarted | `c` | `HON.BAT`, install (exit 2), `shutdown /s /hybrid`, power on: boot type 1, no `Parameters` under `xhcipff` (the filter never ran), root port `...&18` at **problem 12** (7 PCI functions listed: nothing behind it). Then a restart (`shutdown /r`): boot type 0, Role 2, Closed 9, all 18 at 0. **Pass** (as section 11.5.6 says) |
| the update over itself | `a` | exit 3; `xhcipfw: service Start 0, ImagePath System32\DRIVERS\xhcipff.sys` (log), the entry kept, the record unchanged; the image's slot **not** moved: the bundled bytes equal the installed image's, so `xhcipff.sys` stays (the slot moves only for other bytes, below). Restart: Closed 9, all at 0. **Pass** |
| an update between two images | `a` | after the recovery leg below (kit `k10c`: pci's `46397ba0` package beside this one as `OLD\`): this package's image in `xhcipff2.sys`; then the 48.27 build `46397ba0` (`XHCIPFW.SYS` `C1C2C511...47E6`) through `IOLD.BAT`: exit 4 ("Not everything is done", the restart pending), ImagePath moved to `xhcipff.sys`, record `File` `C1C2C511...`, the entry kept, `xhcipff2.sys` gone; restart: that image's start, Closed 9, SkippedAny 0, all 18 at 0. Then this package again: exit 3, ImagePath moved back to `xhcipff2.sys`, record `File` `7F9BB418...`, the entry kept; restart: Role 2, Closed 9, all at 0. **Pass** |
| Safe Mode | `a` | `bcdedit /set {current} safeboot minimal`, restart: the Safe Mode desktop (autologon); COM1 does not open there, so Q to `C:\qsafe.txt`, read after the normal start: `SafeBoot\Option` `OptionValue` 1; `Parameters` written for that start: `XhciPfwSkipped` 2 and `SkippedAny` 2 (the "safe mode" bit), `XhciPfwMode` 0, `XhciPfwClosed` 0, `ResultAny` 0, HoldTicks 0, `XhciPfwVersion` 3.4.2.0 (loaded, no bridge written); root port `...&18` at problem 12 there. `bcdedit /deletevalue {current} safeboot` from Safe Mode's console, restart: Closed 9 again. **Pass** |
| the uninstall, and a cold start | `a` | `/console /uninstall /yes /nopause`: exit 3; the log in order `xhcipff taken out of the UpperFilters of ACPI\PNP0A08\0` (09:26:58.448), `service Start 4` (.479), "XHCIPFW.SYS is no longer registered" (.494), `xhcipff.sys deleted` (.527): entries first, then the service and the file. Q: no `xhcipff`, no `UpperFilters`, no record, no image. Cold start: the desktop; root port at problem 12 (the windows no longer closed), as without the package. **Pass** |
| the recovery route | `a` | installed again, restart (Closed 9); `del ...\drivers\xhcipff.sys` from an elevated console (the loaded image's file deleted), restart: the start stops - "Preparing Automatic Repair", "Diagnosing your PC", then Automatic Repair "Your PC did not start correctly" (Restart, Advanced options). Advanced options, Troubleshoot, Advanced options, Command Prompt, the account `xhci` and its password: `X:\windows\system32`, the system on `C:`. The edit made with `reg.exe` (not regedit's Load Hive window, which 45.9 drove on 8 and 8.1): `reg load HKLM\S C:\Windows\System32\config\SYSTEM`, `Select\Default` 1, `ControlSet001\Enum\ACPI\PNP0A08\0` `UpperFilters` `xhcipff` set to an empty multi-string, read back empty, `reg unload`; `exit`, Continue: **the desktop**, `UpperFilters` empty, root port at problem 12 (the step gone). The installer again then puts it back (image into `xhcipff2.sys`, entry `xhcipff`), and after a restart Closed 9, all 18 at 0. **Pass** |
| 3.3.0.0's entry-role install, then this package | `d1` | 3.3.0.0's released installer does not offer `XHCIPFW.SYS` on 8 to 10 (its readme), so the entry-role install was made by hand in 3.3.0.0's names (`ENTRY.BAT`): its `XHCIPFW.SYS` (`024F3DA5...5590`, 10,038 bytes) as `drivers\xhcipfw.sys`, service `xhcipfw` (Type 1, ErrorControl 0, Group `System Reserved`, ImagePath `System32\DRIVERS\xhcipfw.sys`, Start written 4 first and 0 last, as the installer does), record `XhciPfw` `File` = that hash. A restart with it: the 3.3.0.0 image loaded and acted at entry on this 10 (`Parameters`: `XhciPfwVersion` 3.3.0.0, Mode 2, Closed 9, Skipped 0; every bridge at 0, the NHI at 28 with no driver) - a side finding, below. Then the previews (next row) and the run: "An earlier XHCIPFW.SYS: removed." first, then "XHCIPFW.SYS is no longer registered" and "XHCIPFW.SYS is in ...\xhcipff.sys and registered", exit 3; Q: no `xhcipfw` service, no `XhciPfw` record, no `xhcipfw.sys`; `xhcipff`, its entry and record placed. Restart: Role 2, Closed 9, all 18 at 0. **Pass** |
| the previews on the filter kind, 10 | `d1`..`d4` | below; **pass** |
| the previews on the filter kind, 8.1 | `e1`..`e4` | below; **pass** |
| Q27 | - | read by pci and pci5 (run-48.md, above); not repeated |

**The previews against the run.** Four previews before the run on each guest, each with its own state of the entry role's install made by `ENTRY.BAT` (no restart between it and the previews): `l` = `/console /list /thunderbolt /nostor98`, `u` = `/console /uninstall /list`, `n` = `l` with `/noxhcipfw` (the box unticked), `t` = `/console /list /usb /nostor98` (the Thunderbolt support left out: with `/usb` the console's default for it is N; the preview says "Find controllers and devices above 4 GB (XHCIPFW.SYS): comes with the Thunderbolt / USB4 support, which is not being installed"); then the run `/console /yes /nopause /thunderbolt /nostor98` and Q. S1 = the install as made; S2 = another installation's file (`xhcipfw.sys` replaced by the same bytes and one more, the record unchanged); S3 = the record unreadable (a Deny `QueryValues` ACE for Everyone on `HKLM\SOFTWARE\xhci98\XhciPfw`; `reg query` then "Access is denied"); S4 = the image the record holds unreadable (`icacls ... /deny *S-1-1-0:(R)`; `type` then "Access is denied"). The window's Summary was not driven (it is built by `inst_eng_pfw_plan`, the console's lines by `pfwsys_legacy_plan` within it).

| State | `l`, `n`, `t` (10 and 8.1 alike) | `u` | the run | after the run |
|---|---|---|---|---|
| S1 | "An earlier XHCIPFW.SYS: removed." | "(/list) An earlier XHCIPFW.SYS:" / "(/list) XHCIPFW.SYS would be removed." | "An earlier XHCIPFW.SYS: removed.", "XHCIPFW.SYS is no longer registered ...", "XHCIPFW.SYS is in ...\xhcipff.sys and registered" | entry role gone, filter placed |
| S2 | "An earlier XHCIPFW.SYS: unchanged (another installation's file)." | "... XHCIPFW.SYS is another installation's file: it would be kept." | the same line, "The service's file xhcipfw.sys was not installed by Setup: the service xhcipfw is left unchanged.", then the filter placed; exit 2 | `xhcipfw` service, image (10,039 bytes) and `XhciPfw` record kept; `xhcipff`, entry and record placed |
| S3 | "An earlier XHCIPFW.SYS: unchanged (its record cannot be read)." / "The filter XHCIPFW.SYS is then left unchanged." | "(/list) XHCIPFW.SYS's record cannot be read." / "(/list) The filter XHCIPFW.SYS would be left unchanged." | the same two lines, "XHCIPFW.SYS's record could not be read: it is left unchanged."; exit 2 | entry role as it was; no `xhcipff`, no entry, no filter record |
| S4 | "An earlier XHCIPFW.SYS: unchanged (its file cannot be read)." / "The filter XHCIPFW.SYS is then left unchanged." | "(/list) XHCIPFW.SYS cannot be read: it would be kept." / "(/list) The filter XHCIPFW.SYS would be left unchanged." | the same two lines, "The service's file xhcipfw.sys cannot be read: XHCIPFW.SYS is left unchanged."; exit 2 | entry role as it was; no `xhcipff`, no entry, no filter record |

Every preview said what its run then did, in every state, with the box ticked, unticked or the Thunderbolt support left out (the previews `n` and `t` name the earlier install's outcome before the filter's line as `l` does). **10: pass (`d1` to `d4`). 8.1 (build 9600): pass (`e1` to `e4`, the same lines in every state).** The window's Summary was not driven.

QEMU pids (all started and stopped by this worker, each start and exit in its folder's `pids.txt` under `E:\xhci98-vm\p48\pci7\`; two guests at most at once): `a` 207224, 202140, 197076, 202556, 20952, 84132; `b` 116428, 13332; `c` 205864, 143208; `d1` 182140; `d2` 78392; `d3` 199456; `d4` 177260; `e1` 152048; `e2` 203016; `e3` 196356; `e4` 79132. No VM left running (`a\pids.txt` holds two extra exit lines for 197076, written by waiters cut short). Evidence: each folder's `serial-a.log` (every Q block, install output and preview, in order), `a\trace-c1.log`, `s*.png`; kits `E:\xhci98-scratch\w48\pci7\k10`, `k10b`, `k10c`; the batches in `E:\xhci98-scratch\w48\pci7\bat\`.

No defect found in this package's code: no commit.

Side findings:
- 3.3.0.0's `XHCIPFW.SYS` in the entry role (service `xhcipfw`, group `System Reserved`, Start 0) on this 10 x86 (19045) acted at its entry at that start: Mode 2, Closed 9, no skip bit, every bridge at problem 0. Section 11.5.1's 10 row (45.9's probe never initialised, the start failed) is the probe's install with no `ImagePath`, not this; one start read, `[emu]`.
- The 10 x86 q35 base has `HiberbootEnabled` 0 and hibernation off, so fast startup is off there by default; the fast-startup legs set both first.
- The update over itself with identical bytes keeps the image's slot; section 11.5.6's "the image's slot moved" holds for an update between two images (read above).
- After the recovery route the installer, run again, wrote the image into `xhcipff2.sys` (the service still named `xhcipff.sys`, whose file was gone): exit 4, "xHCI98 PCI Bridge already had problem code 12, and still has it" until the restart.

## 48.35 (ME's standby, 98 SE on `pc`) - worker tbs5 (2026-11-10)

Task 48.35's ME finding from worker tbs4 (`run48-tbs4.md`: on ME, `pc`
machine, S.1's topology, with the package installed, "standby never
reaches S3" with the ORICO model attached and "no resume" without it)
chased. Branch `w48-tbs5` from `3.4.3.0` at `4fbe568b`
(`XhciCmSleepEntry` 1 by default). **No code change**: the hang does not
reproduce, and tbs4's "never reaches S3" is its leg script's reading
(below).

**Vehicle.** Development host A, TCG, QMP 47850 to 47853, at most two
guests at once; the host carried 7 to 9 other workers' QEMU guests
throughout (CPU about 73 % at 09:05). ME on `pc` (i440FX, stock SeaBIOS)
with S.1's topology as tbs4 ran it: `usb4-tunnel-bridge` 00:05.0 with
SHPC, `usb4-host profile=usb4-generic` 00:04.0, the ORICO model
(`device-model=asm2464`, the FAT NVMe) at power-on unless "none",
`qemu-xhci` 00:06.0; the pinned emulator `asm4242-20261025-2230`
(`E:\xhci98-vm\p48\tbs5\tools\gome.ps1`, `gopc.ps1`, copies of tbs4's).
The guest disk: tbs4's `mep` overlay (the 3.4.3.0 package as tbs4 built
and installed it, exit 4) copied to `me0\mebase.qcow2`, every leg on an
overlay of it or of a cleanly shut-down descendant (`me0p` value 0;
`me4` value 1; `me5` the qemu flavour's `xhcicm.sys` copied over
`SYSTEM32\DRIVERS`). The leg: `tools\leg9me.sh` (new): Run
`C:\T\ONE9.BAT s3 <tag>` (tbs4's), QMP `query-status` polled every 2 s
for up to 300 s for "suspended", 30 s (or 90 s), `system_wakeup`, then
`===E one` waited for 400 s (else registers and screens taken), then a
Start-menu shutdown timed.

**Why tbs4 read "standby never reaches S3".** On ME `SetSuspendState`
returns TRUE at once and S3 follows 4 to 40 s later. tbs4's
`leg9pc.sh` read the QMP status once, right after `SLP.EXE` returned,
read "running", printed "not suspended", sent no `system_wakeup`, and
waited for `===E` that a suspended guest never prints; the screendump of
a suspended guest shows its last frame, the black text page Windows
switches to before S3. Every leg below that polled reached S3, with and
without the enclosure, at 1 and at 0.

### Legs

| Leg | Guest | `XhciCmSleepEntry` | Enclosure | S3 | Resume | The drive after | Shutdown |
|---|---|---|---|---|---|---|---|
| me1 a | ME, `pc`, release (`me0p`) | 0 (`cm.usb4.sleep=00000100`) | ORICO | yes, ~40 s after `SetSuspendState` | desktop, ONE9 to its end | **not found** at t+60 | 7 s |
| me2 n | ME (`me0p`) | 0 | none | yes (woke by itself before `system_wakeup`) | desktop | - | 7 s |
| me3 a | ME (`mebase`) | 1 (`00000101`) | ORICO | yes, 8 s | desktop | `E:` back, `WaA.BIN` compared, `WaB.BIN` written and compared; `u4.sleep.entered=00000002` (host router and the ASM2464 Sleep Ready), `cm.suspend=4`, `cm.resume=1` | yes |
| me4 n | ME (`mebase`) | 1 | none | yes, 6 s | desktop; `u4.sleep.entered=00000001` | - | yes |
| me6 n | ME (`me4`) | 1 | none | yes | desktop | - | yes |
| me7 n | ME, qemu `xhcicm.sys` (`me5`) | 1 | none | yes, 4 s | desktop; debugcon `u4.sleep.entered=1` | - | yes |
| me8 a | ME, qemu `xhcicm.sys` | 1 | ORICO | yes, 6 s | desktop; `cm.resume` to `cm.reenum.resumed` in 69 ms (debugcon) | `E:` back, both compares clean | yes |
| me10 n | ME (`me4`), 90 s asleep | 1 | none | yes (woke by itself within 90 s) | desktop | - | yes |
| me9 | ME, qemu `xhcicm.sys` | 1 | none | - | - | - | boot never drew the taskbar (9 min, CPU busy in V86 BIOS code; 11 QEMU on the host, CPU 73 %); quit; no sleep leg |
| s98 k | 98 SE, `pc`, `win98-gold` + the package (release `xhcicm.sys`) | 1 | ORICO | `SetSuspendState` refused (0) while ONE9's DOS box runs (98 SE asks the DOS program, "not responding"); GUI Stand by: yes, 5 s | woke by itself before `system_wakeup`; black text page, CPU idle (`HLT` at `ff0270ed`) for 3 min, then quit | - | - |
| s98c | 98 SE `win98-gold`, **no package**, the same topology | - | ORICO (no driver) | yes, at once | desktop within 60 s | - | yes |
| s98 (z, first) | 98 SE + package, value 1 still running | 1 | ORICO | yes | desktop between 60 and 180 s after the wake | - | yes |
| s98r r2 | 98 SE + package | 1 | ORICO | yes, at once | desktop 24 s after the wake; `dir E:` answered | `copy BIG.BIN E:` **froze the UI** (CPU busy in VMM ring 0, not halted, 5+ min); quit | - |
| s98z z | 98 SE + package, qemu `xhcicm.sys` | 0 (`00000100`) | ORICO | yes, 2 s | desktop 22 s after the wake, then a "PCI standard PCI-to-PCI bridge" New Hardware wizard | `copy` to `E:`: "0 file(s) copied" | - (quit) |

**The 98 SE vehicle is confounded.** `win98-gold` carries a 2.0-era
`XHCI98.SYS` (114,704 bytes, 2026-10-03, a qemu build: it writes to the
debug port) in `SYSTEM32\DRIVERS`, and the 3.4.3.0 install (`INSTALL.BAT
/console /yes /nopause /thunderbolt /stor98`, then the New Hardware
wizards' "search for the best driver") did not replace it: the xHCI
controller ran that old binary in every 98 SE leg. `xhcicm.sys` and
`xhciuas.sys` are the package's. A qemu-flavour `xhcicm.sys` copied in
on `s98q` logged nothing at all, and that guest's shutdown hung on a
black text page; on `s98z` the same copy (with `copy /y`) logged. The
98 SE readings above need a clean 98 SE base with the package before any
conclusion.

### What this says

1. **No ME hang at 1 reproduces.** Six ME standby legs at 1 (three with
   the ORICO, two of them with debugcon) entered S3 and came back to the
   desktop, the drive back and written where attached; the sleep entry
   reads Enter Sleep and Sleep Ready on every router. tbs4's leg a was the
   script (above). tbs4's n1 (one resume that sat on a black text page,
   the processor idle at `ff03a3cd`) is unexplained; the same idle-loop
   picture came once on 98 SE (`s98 k`, after a self-wake) and resolved
   by itself in 60 to 180 s on another 98 SE leg, so "slow, then back"
   cannot be ruled out for n1 (tbs4 quit after 3 minutes).
2. **0 is worse on 9x, not better.** At 0 the domain's sleep takes the
   links down unprepared and section 7.6's rebuild brings the tunnel back
   as new hardware: ME's volume was gone at t+60 (`me1`), 98 SE's copy
   failed and a bridge wizard appeared (`s98z`). At 1 ME's volume came
   back and took writes every time.
3. **98 SE at 1**: S3 and the desktop back, but one write to the volume
   after the resume froze the guest (`s98r`). Unread whether that is
   Stor98's resume on 98 SE, the old `XHCI98.SYS` sharing the interrupt,
   or the CM; the next leg is a clean 98 SE base and the qemu flavour.
4. **The guests wake by themselves**: three legs (`me2`, `me10`, `s98
   k`) left S3 before `system_wakeup` within 20 to 90 s, at 0 and at 1,
   with and without the enclosure; the wake source is unread.

**No code change, no commit, no build beyond the qemu flavour** (gated
`build-driver.cmd qemu` x86 of `4fbe568b`, `BUILD + GATES PASSED (qemu)`,
`gated-qemu-x86.log`; qemu `xhcicm.sys` SHA-256 `351b39f8d8b33d5b...`,
used only in the guests).

**Not run** (task 3): S.8 at 0 and S4 on the chain, the chain's first
shutdown after a standby.

Evidence: `E:\xhci98-vm\p48\tbs5\` (`me0p`, `me1` to `me10`, `s98`,
`s98c`, `s98q`, `s98r`, `s98z`: `serial-*.log`, `debugcon-*.log`,
`cmdline-*.txt`, screens, `regs-*.txt`, `eip-*.txt`, `pids.txt`), every
QEMU start and stop in `pids-all.txt`; scripts `tools\gome.ps1`,
`gopc.ps1`, `leg9me.sh` (the polling leg), `sby98.sh` (98 SE GUI Stand
by, timed); `xfer\QCM\xhcicm.sys` (the qemu flavour). The first answer to
the coordinator: `first.txt` (08:28, corrected 08:41).

## 48.36 (continued) Both dock models on 2000, 7 and 10 x86; ME's cold start with a dock at power-on (2026-11-10, w48-dock5)

Worker w48-dock5, development host A, branch `w48-dock5` from `7f447757`
(3.4.3.0, w48-dock4 merged), worktree `E:\xhci98-wt\w48-dock5`; no
commit (no change made). QMP 47920 and 47921; every QEMU start and stop
in `E:\xhci98-vm\p48\dock5\pids.txt` (all stopped).

**Build and kit.** `build-driver.cmd all` at `7f447757`
(`XHCI98_SELFTEST_SKIP_OK=1`, slot gated.1): "BUILD + GATES PASSED
(release qemu)" (`E:\xhci98-scratch\w48\dock5\gated-x86.log`);
`make-package.ps1` release and qemu x86; kit `E:\xhci98-vm\p48\dock5\xfer`
(family layout, `SHA256.TXT`): `xhci98.sys` `5F7B1294...`, `xhcicm.sys`
`45C2F1E3...`, `XHCITR.EXE` `E3C310FD...`; qemu flavour in `xfer\Q`
(`xhci98.sys` `48520E68...`, `xhcicm.sys` `428928A0...`).

**Emulators.** The docks on w48-dock2's
`E:\xhci98-scratch\w48\dock2\emu\qemu-system-x86_64.exe` (`e3fc8ed3...`),
`launch-q35.ps1 -Usb4 tb3ar`, hot-plugged
`usb4-device,id=<id>,bus=u4.0,port=1,dialect=tb3,dock-model=ts3plus|hp-g2`;
ME's chain dock on w48-chain's `E:\xhci98-vm\p48\chain\bin` (`396e4de4...`),
`-Usb4 adl`, `usb4-device,id=enc0,bus=u4.0,port=1,serial=USB4EMU01,
dialect=usb4,downstream-ports=1,chain-power-controller=off,drive=nvme0` at
power-on. Sticks: `usb-storage` over read-only `fat:16` folders A, B, C
(`snapshot=on`), each with a 1 MiB `R?.BIN`; `G.CMD cmp`: `fc /b` of the
stick's file against the kit's, a copy written back and compared.

| system, VM | dock | step | result |
|---|---|---|---|
| 2000 SP4 `w2k` | - | `INSTALL /console /yes /nopause /thunderbolt` | first run exit 3 with "Code 12 until the restart" (the tunnel port's first filter install, as documented); after a cold start the second run: `xhcicm` on the Alpine Ridge NHI, the TB xHCI and the bridges started |
| 2000 | TS3 Plus | first plug | each dock bridge's Found New Hardware wizard ends "will not work until you restart your computer" and asks a restart; with No, the two FL1100s start (`rep3.txt`), the ASM1142's port 05:02 stays unenabled (Command 0000, BAR unassigned); a restart answered Yes hung in real mode (EIP `0d4f`, the base's known warm-restart hang), so Shut Down and a cold start | Windows 2000's own restart (finding 2) |
| 2000 | TS3 Plus | second plug after the cold start | the ASM1142's wizard; **all three controllers started** (`rep4.txt`), "CalDigit, Inc. TS3 Plus (Thunderbolt 3), approved"; the 82574L's wizard (no 2000 driver) cancelled | pass |
| 2000 | TS3 Plus | a stick on each controller (A FL1100 #1, B FL1100 #2, C ASM1142) | E:, F:, G:: "FC: no differences encountered" twice each | pass |
| 2000 | TS3 Plus | `XHCITR /eject id=` the dock's upstream port | every function listed; "Windows has stopped QEMU QEMU HARDDISK USB Device, ... (E:, F:, G:). The eject was confirmed to the Thunderbolt driver. It is now safe to disconnect the device." (`d-ej1.png`, `d-ej3.png`) | pass |
| 2000 | TS3 Plus | unplugged, re-plugged | three controllers started at once (`rep5.txt`); stick C on the ASM1142 compared equal | pass |
| 2000 | TS3 Plus | removed by surprise (stick mounted) | Windows' "Unsafe Removal of Device" and "Delayed Write Failed" for the stick's write-behind; the system answers, 0 Thunderbolt devices | pass |
| 2000 | HP G2 | plug | "HP Thunderbolt Dock G2 (Thunderbolt 3), approved"; "xHCI98 USB 3 Controller (Intel Titan Ridge)", its root hub and both "xHCI98 USB Hub" started; the RNDIS device listed (`rep6.txt`) | pass |
| 2000 | HP G2 | a stick on the Titan Ridge xHCI's port 2 | E:: no differences twice | pass |
| 2000 | HP G2 | **`XHCITR /eject id=` the HP dock's upstream port** | "Windows has stopped QEMU QEMU HARDDISK USB Device (E:). The eject was confirmed to the Thunderbolt driver. It is now safe to disconnect the device." (`d-hpej5.png`) | **pass** |
| 2000 | HP G2 | unplug, re-plug, surprise removal | controller and hubs started again (`rep7.txt`); after the removal the system answers (`rep8.txt`) | pass |
| 7 SP1 x86 `w7` | - | `INSTALL ... /thunderbolt` (publisher prompts clicked), exit 3, cold start | started | - |
| 7 x86 | TS3 Plus | plug (no question) | five xHCI98 controllers started: QEMU's, the Alpine Ridge's, two FL1100s, the ASM1142 (`rep1.txt`); the 82574L on Windows' own driver | pass |
| 7 x86 | TS3 Plus | a stick on each controller | the three disks came up offline (finding 3); `diskpart` `online disk`, then F:, D:, G: no differences twice each | pass |
| 7 x86 | TS3 Plus | `XHCITR /eject` (elevated), unplug, re-plug, surprise | "Windows has stopped ... (D:, F:, G:). The eject was confirmed to the Thunderbolt driver. ..." (`b-ej2.png`); re-plug: five controllers started (`rep3.txt`); after the surprise removal the system answers, 0 Thunderbolt devices (`rep4.txt`) | pass |
| 7 x86 | HP G2 | plug, stick, eject, re-plug, surprise | Titan Ridge xHCI and both hubs started, RNDIS listed (`rep5.txt`); stick D: no differences twice; "Windows has stopped QEMU QEMU HARDDISK USB Device (D:). The eject was confirmed to the Thunderbolt driver. ..." (`hp-ej2.png`); re-plug started (`rep6.txt`); surprise: the system answers | pass |
| 10 22H2 x86 `w10` | - | `INSTALL ... /thunderbolt /usb` (publisher prompts answered), exit 3, restart | desktop | - |
| 10 x86 | TS3 Plus | plug, 60 s after the desktop | **STOP: PAGE_FAULT_IN_NONPAGED_AREA** about 60 s after the plug (`b3.png`); the restart with the dock attached crashed again ("Diagnosing your PC"); a cold start without the dock boots; a second plug crashed again (its STOP screen not caught) | **fail, open** (finding 1) |
| 10 x86 | HP G2 | - | not reached | owed |

**Finding 1, 10 x86: a STOP on the TS3 Plus plug (open).** No dump was
kept (`CrashDumpEnabled` 7, but no `MEMORY.DMP`, no `Minidump`, no event
1001: each crash's restart crashed again or was cut), so the failing image
is not known: ours (`xhci98.sys` under `/usb` on the dock's three
controllers, `xhcicm.sys`) or Windows' own (10's inbox driver for the
82574L, or its audio). The same build ran the same dock on 7 x86 and 2000
with no fault. Replacing the drivers with the qemu flavour in the running
guest failed (files in use). Next: a fresh 10 x86 overlay with
`CrashDumpEnabled` 3 and `AutoReboot` 0 set and a restart before the plug,
the qemu flavour installed (debugcon), the plug; the STOP's parameters and
the image holding the faulting address read from the minidump's header
and module list (image names and bases only); then the same with
`/cmonly` (Windows' own `usbxhci` on the dock's controllers) to split ours
from the rest.

**Finding 2, 2000: a dock's first plug asks a restart per bridge.**
Windows 2000 installs the dock's PCIe switch ports ("xHCI98 Thunderbolt
PCI Bridge") with "The hardware you installed will not work until you
restart your computer"; until the restart the ASM1142's port is not
enabled (the two FL1100s start). After one restart every later plug starts
all three controllers. The host side's equivalent (the tunnel port's first
filter install) is documented; a dock's is not. Proposed known-issue line
(no dock named): "Windows 2000: the first time a Thunderbolt dock is
plugged in, Windows asks to restart for each of its PCI bridges; until
the restart some of the dock's devices do not start."

**Finding 3, 7: vvfat sticks offline.** QEMU's `fat:` disks share one MBR
signature; 7 takes a disk whose signature collides with another's offline
(2000 does not). Not the driver; future legs give each stick a distinct
image or run `online disk` first.

**ME's cold start with the dock attached at power-on (the black text
screen).** It is the 9x fatal exception family w48-boot0e chased, not a
plain hang: the black screen is what follows "An error has occurred ...
Error: 0E : 019F : BFF7xxxx ... Press any key to continue" (dock3's
`cme2\c1.png` showed `0E : 019F : BFF7B018`; this run's `me1\b-c2.png`
`0E : 019F : BFF775B4`: ring 3, KERNEL32's range), and once a key is sent
the GUI never comes up. Readings (fresh disk per trial, the first boot
after the install, watched 240 s):

| configuration | boots | 0E |
|---|---|---|
| control, no xHCI98, chain dock (adl) at power-on, first boot then cold boots (`mc1`) | 6 | 0 |
| control, no xHCI98, TS3 Plus (tb3ar) at power-on (`mc2`) | 2 | 0 |
| this tree's package + qemu drivers, chain dock at power-on (`me1` b to l, from `disk-inst.qcow2`) | 11 | 1 (b) |
| same, TS3 Plus at power-on (`me1` t1 to t3) | 3 | 0 |

The faulting boot's CM log (`me1\debugcon-b-0E.log`, qemu `xhcicm.sys`):
CM start, the tunnel up by 1.7 s, `cm.callwk.started=00000303` and
`cm.reenum.9x.version=0000045A` at 4.8 s (the CONFIGMG worker's
opening), then nothing until 119.4 s (`cm.reenum.up.poll=00000303`), the
moment the key was sent; clean boots go on within 1 to 10 s. The debugcon
file stayed empty while the blue screen stood and filled only after the
key: the screen holds the machine, so "no debugcon line before it"
(dock3) does not clear the driver. It is the same stall point as
w48-boot0e's reading of chain's faulting boot (the CM log stalled 107 s at
the first tunnel's bridge poll). Not the BIOS (the fault is in KERNEL32,
after the CM started); not shown to be the emulator's or ours: 1 in 14
with the drivers, 0 in 8 without, too few to separate. Not the XHCITR tray
(it starts after the shell). Next, if pursued: a long batch of first boots
after the install, ours against the same package with `XhciCmReenum` 0 or
without the CM, to see whether the fault follows the CM's boot-time
CONFIGMG work.

User-visible strings: none changed. QEMU PIDs (all stopped): 207480,
180684, 68964 (a mistaken launch, killed at once), 167212, 180352, 196492,
124456, 160364, 199580, 146824, 7348, 186364, 185152, 124756, 100620, and
each trial's PID in `pids.txt`.

The owner's answers of the day: the README carries the Windows 2000 note
on a dock's first plug (no dock named); the 9x first-boot fatal exception
is reopened for a long batch against controls.

## The tunnel found up at start, inferred; the learning narrowed (48.43, 2026-11-10)

Worker learn, branch `w48-learn` from `975ffe31` (worktree
`E:\xhci98-wt\w48-learn`). Evidence `E:\xhci98-vm\p48\learn\a\`,
`E:\xhci98-vm\p48\learn\b\`.

### Design

Record 22 section 7.6.2 decided (option 3, owner 2026-11-10) and a new
section 7.6.3 (the narrowed learning rule): commits `c86e9e11`,
`e471f499` (the revision after the first read: the start reading counts
only members no binding explains and trusts only switch/router-named
sources; the at-start mark is set at `cmU4BindRest` alone and ended by a
`TUNNEL_UP` taken, since the seam record is set for every tunnel found
up). Approved on the second read.

### Code

- `65e32808` the inference (`XhciTbtInfer`, `XhciTbtInferReadBack`,
  `TbtU4PcieLinkClass`, the driver half in `cm_nhi.c`), the at-start
  marks and the narrowed attribution in `XhciTbtLearn`, the status
  record naming the member for a confirmed inferred closure,
  `XhciCmInferAtStart` (rule row, both NHI INFs at 0 NOCLOBBER,
  tuning.md), host vectors.
- `395acb4a` after the first code read: the record zeroed at both start
  paths; the read-back's 2 s bound checked before a sample is taken; a
  containment ends a read-back unconfirmed (the port conflicted for the
  inference); a status vector. Approved on the second read.

Host: the full suite passed at `65e32808` (`test_tbt_topo` 873 checks
with the new `test_infer`/`test_narrowed`, `test_cm`, all suites);
`test_cm_status` 149 checks at `395acb4a`.

New log lines: `u4.infer` (port 15:8, member 7:0), `u4.infer.bdf`,
`u4.infer.refused` (port 15:8, reason 7:0, `XHCI_TBT_INFER_*`: 2 host,
3 set, 4 inactive, 5 bound, 6 conflicted, 7 not one tunnel, 8 not at
start, 9 pending, 10 router, 11 not armed, 12 unread, 13 no Link Active
reporting, 14 moved, 15 occupancy, 16 links), `u4.infer.dropped`
(port 15:8, why 7:0: 1 contained, 2 a binding, 3 not pending, 4 tunnel
down or another connection, 5 member gone, 6 set changed),
`u4.infer.recheck` (port 15:8, reason; FF a different answer),
`u4.eject.inferred` (port 31:16, bridge), `u4.infer.closed` (port 31:16,
bridge), `u4.infer.conflict` (port 15:8, read-back answer).

### Builds

Gated `release` x86 and `release -amd64` of `395acb4a` (the tree the
code review approved): "BUILD + GATES PASSED (release)" both
(`E:\xhci98-scratch\w48\learn\build-x86.log`, `build-amd64.log`), with
`XHCI98_SELFTEST_SKIP_OK=1`, slot `E:\xhci98-locks\gated.1` (made,
`owner.txt`, both removed after). The x86 `xhcicm.sys` of that build
(sha256 `5b7061bf...`) is what the legs ran.

### Legs (8.1 x86, the `asm4242` model, `-EncAtBoot -Tablet`)

Machine: copies of ejw's `w81x86` overlay (`E:\xhci98-vm\p48\ejw\a\disk.qcow2`,
kit `d0e3eea0`, Stor98 on the drive, `xhcimark` running; the second
enclosure `enc1` never plugged on it), the shared launcher (its tunnel
ports `power_controller_present=off`), 2 vCPUs, TCG; `xhcicm.sys`
replaced from the xfer disk (`CP.CMD`: copy, `fc /b` equal) and
`XhciCmInferAtStart` written into the NHI's driver key
(`Class\{4d36e97d...}\0037`), then a restart. Reports by
`XHCITR /report` typed to COM1 (`r*.txt`, `serial-*.log`).

| Guest | Setting | Step | Result |
|---|---|---|---|
| a (pid 207236) | `XhciCmInferAtStart` 0, the launcher as is | start, enc0 at power-on | no `u4.infer*` line (`r1-boot.txt`); `u4.map.none=FFFFFFF5`, `u4.preserved=00010808` |
| a | | eject 1: `XHCITR /eject id=<enc0 USP>`, OK | "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (E:). The eject was confirmed to the Thunderbolt driver. The PCIe tunnel stays up until the device is unplugged: the Thunderbolt driver does not know which of its ports the device is on. It learns that when a device is plugged in while Windows runs." (`s16.png`); `u4.eject.unmapped=00000200` - unchanged behaviour |
| a | | enc1 `device_add` on USB4 port 2 (never seen by the disk) | `u4.learn=0000020A` (SEVERAL); enc1's upstream port 06:00.0 **problem 31** (`r2.txt`). The set here has three members, 02:00.0, 02:08.0 and **02:10.0**: the model makes the DSP of the ASM4242's own xHCI hot-plug capable (`cm.reenum.topo.candidates=3`), so the start reading counted two occupied members for one at-start port and was not trusted - the rule as designed; the board's DSP 2 has no hot-plug slot (record 21's two tunnel ports) |
| b (pid 173976 then 165668) | 1, the launcher as is | start | `u4.infer.refused=0000010D`: port 1, **NO_REPORT** - 02:10.0 (the xHCI's DSP) does not report Data Link Layer Link Active (ECAM: Link Capabilities 00000400); the tunnel ports 02:00.0 and 02:08.0 do (00100400), 02:00.0 Link Active set with enc0, 02:08.0 clear (`r1-boot.txt`). The inference refused, as designed |
| b | | eject 1 | the same "stays up" line as a (`s16.png`) |
| b2 (pid 204160) | 1, `-global usb4-asm-dsp.hotplug=off` (board-faithful: only the two tunnel ports are hot-plug members) | start | `u4.infer=00000100`, `u4.infer.bdf=00000200` (port 1 is 02:00.0) (`r3.txt`) |
| b2 | | eject 2: `XHCITR /eject id=<enc0 USP>`, OK | **"Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (E:). The eject was confirmed to the Thunderbolt driver. The PCIe tunnel was closed."** / "It is now safe to disconnect the device." (`s21.png`); log `cm.reenum.confirm`, `cm.reenum.eject`, `u4.eject.bridge=00000200`, `u4.eject=00000100`, `u4.eject.inferred=00010200`, `u4.infer.closed=00010200` (`r4-eject.txt`); emulator: 03:00.0 config reads all-ones, tp0 Link Status Link Active clear (`ej2-pci.txt`); enc0's kept children destroyed (`cm.kept.gone`) |
| b2 | | enc1 `device_add` on port 2 (never seen) | `u4.learn=00000203` (LEARNED; port 1 proven down by the teardown); enc1's upstream port **problem 31** again (`r5.txt`) |
| c (pid 150076) | 0, `hotplug=off` (fresh copy of ejw's disk) | enc1 `device_add` on port 2 while enc0 (port 1, at start) stays | **`u4.learn=00000203`: port 2 learned while port 1's at-start tunnel is pending** (narrowed rule; formerly SEVERAL); enc1's upstream port problem 31 (`r1.txt`) |
| c | | `XHCITR /eject id=<enc1 USP>` | "PCI Express standard Upstream Switch Port cannot be stopped here: not started - problem code 31." (`s02.png`): port 2's eject not read (owed, behind the problem 31) |

QEMU PIDs (all stopped by ACPI powerdown, exits confirmed): 52792,
173976 (first boots), 207236 (a), 165668 (b), 204160 (b2), 150076 (c);
`pids.txt` in each folder.

### The problem 31 (enc1's upstream port)

Read three times, each time on a disk that had never seen enc1 (a, b2,
c), with the port learned (b2, c) and not (a, ejr's run b): so it is
neither the learning rule nor the whole-set re-enumeration. ejw's and
ejr's re-plugs of enc0 (a device the disk had installed) never showed
it. The log, the same each time: the tunnel port's depth-2 chain child
attaches a depth-3 child to the new upstream port's PDO at its
BusRelations (`cm.chain.attach=01030208`, before any AddDevice), the
bridge filter then finds xhcicm already there and skips
(`cm.reenum.child.decision=00000000`), the child reads a bridge
(`cm.chain.bridge=00030208`), and about 300 ms later the stack is
removed (`cm.chain.remove=00030208`) and the child kept on the PDO
(`cm.kept.logical=1003FFFF`, no `cm.kept.cb`: the PDO stays); the
device then sits at problem 31 (CM_PROB_FAILED_ADD) with no resources
and a driver key already (pci 6.3.9600; no new section in
`setupapi.dev.log`). Reading: on its first appearance the device has
no driver yet; the first stack is the PDO and the kept child; Windows
installs the inbox PCI driver and restarts the devnode, removing that
stack; 8.1's kept child (record 31 section 4.10) stays attached by
design; the PCI driver's AddDevice then attaches above an object whose
removal was processed, which the I/O manager refuses, so AddDevice
fails: problem 31. The last step is inferred (the I/O manager's refusal
to attach above a removed object), not traced. Not this task's to fix:
a finding for record 31 section 4.10 (the kept child on a device with
no function driver yet). A restart of Windows would build a fresh
stack; not read here.

### User-visible strings

None changed in code. The tools' lines (record 24 section 5.2) are
unchanged; what changes is which one shows: with
`XhciCmInferAtStart` 1 and the inference made, the eject of a device
attached at start ends "The PCIe tunnel was closed." where it ended
"The PCIe tunnel stays up until the device is unplugged: the
Thunderbolt driver does not know which of its ports the device is on.
It learns that when a device is plugged in while Windows runs." (that
line stays for 0, the default, and for a refused inference). A
read-back that does not confirm gives "The Thunderbolt driver has not
confirmed that the PCIe tunnel was closed." New: the tuning.md row for
`XhciCmInferAtStart` and the INF comment lines.

### Left

- The owner's board's Link Capabilities (bit 20 on the two tunnel
  ports) and an inferred eject on it; the default stays 0 until then.
- Port 2's eject after the narrowed learning (behind the problem 31).
- The problem 31 of a never-seen device behind a tunnel on 8.1: record
  31 section 4.10's kept child (above); a restart's effect not read.
- The emulator model: the ASM4242's xHCI downstream port is hot-plug
  capable (the board's is not); the shared launcher could pass
  `-global usb4-asm-dsp.hotplug=off` (not changed here).
- 98 SE / ME: the same hand-off code (`CmNhiU4EjectMember`); not read
  in a 9x guest.

Record 22 as read: `22a2d58d` (approved on its read).

## The narrowed learning and the inference: a tunnel's far end only (48.43, 2026-11-10)

Worker learn2, branch `w48-learn2` from `3.4.3.0` at `09ba3c3d`
(worktree `E:\xhci98-wt\w48-learn2`). Evidence `E:\xhci98-vm\p48\learn2\`.

### The hole

`src/cm/tbt_topo.c` (the start reading, `xhciTbtStartTake`): source 2
(`XHCI_TBT_SRC_SWITCH`) does not prove that what sits behind a member is
a tunnel's. On the emulator's launcher as it was, the ASM4242's own
xHCI's downstream port is hot-plug capable, so a member with the xHCI
always behind it. Port 1 preserved but its function late: the xHCI's
member alone made `occupied == 1 == at-start ports`, `StartOk` set; port
1's function then answering during port 2's attempt (on a member empty
at the start reading) was learned as port 2's - a wrong LEARNED binding
in the shared table. Live with `XhciCmInferAtStart` 0. I7 of the
inference had the same hole (the xHCI's member the one occupied member,
both links agreeing, if its port reports Link Active).

### The rule chosen

A member counts only when what answers behind it is **a tunnel's far
end**: device 0 alone, a type-1 function whose PCI Express capability
says Upstream Switch Port (`xhciTbtFarEnd`; the controller's own xHCI and
NHI are endpoints, so never). Applied:

- the start reading: an unexplained occupied member holding anything
  else makes it untrusted (old rule stands);
- the narrowed attribution: also every member active, and the gain
  itself a far end;
- I7: the one occupied member must be a far end, else reason 17
  `XHCI_TBT_INFER_SHAPE` (read inside the pass, before the closing bus
  number reads).

An unreadable shape is never a shape: at the gain `UNREAD` with the
baseline kept (`225da376`); at the start reading the reading is not
taken, which holds only a gain the narrowed rule decides, never the rule
for a port pending alone (`a0bd5ce2`); in I7 reason 12. An at-start
port's own attempt answers `SEVERAL` without moving the baseline while a
port not at start is pending, so it never consumes that port's gain
whatever the order (`cf805784`). The base rule for a port pending alone
is unchanged (it attributes one gain of any shape: a residual, outside
this task).

### Commits

- `97d87b03` the far-end rule (start reading, attribution, I7), record 22
  7.6.2/7.6.3, host vectors (the regression: xHCI member occupied, port 1
  preserved but empty, port 1 answering during port 2's attempt ->
  `SEVERAL`, no binding).
- `225da376` an unreadable far-end shape is a reading not made.
- `a0bd5ce2` an unreadable start shape holds only the narrowed attribution.
- `cf805784` an at-start port's attempt leaves another port's gain
  unconsumed.

Host: full suite PASSED at each commit; `test_tbt_topo` 873 -> 910
checks (`host1.log` .. `host4.log`). Mutation check: with
`xhciTbtFarEnd` forced to YES the new vectors fail (13 failures,
`host-mut.log`), port 2 learned 14:01.0 - the reported wrong binding.

### Review

Four reads against `09ba3c3d`: three medium findings (the three follow-up
commits above), then approved at `cf805784` ("no material findings").

`eda7c6d9` record 22 section 7.6.3's "As read" for the legs below
(docs only, after the builds).

### Builds

Gated `release` x86 and `release -amd64` of `cf805784`: "BUILD + GATES
PASSED (release)" both (`build-x86.log`, `build-amd64.log`),
`XHCI98_SELFTEST_SKIP_OK=1`, slot `E:\xhci98-locks\gated.2` (made,
owner.txt, both removed after). x86 `xhcicm.sys` sha256 `a7742754...`
(full: a774275479a50e399f95e49efd44673790a5bd182d7af22bfe09d7e52e74c6d4).
Lessons: the first x86 run failed at XHCIPFW.SYS because I had unset
NoDefaultCurrentDirectoryInExePath (cmd then runs src\pfw\nt\build.cmd
in place of the DDK's build.exe): keep it set for build-driver.cmd
(unset it only for test\run-host-tests.cmd); and never edit the tree
while a gated build runs (three runs were stopped for that, leaving
out\gated-*.lock to remove by hand).

### Legs (8.1 x86, `asm4242` model, enc0 at power-on; launcher with
`-global usb4-asm-dsp.hotplug=off` and the tunnel ports'
`power_controller_present=off`)

Fresh copies of ejw's overlay (`E:\xhci98-vm\p48\ejw\a\disk.qcow2`),
xhcicm.sys of `cf805784` copied from the xfer disk (fc equal), the
setting written in the NHI's key (`Class\{4d36e97d...}\0037`), restart.

| Guest | Setting | Step | Result |
|---|---|---|---|
| c (pid 204396, qmp 47872) | `XhciCmInferAtStart` 0 | enc1 `device_add` on USB4 port 2 while enc0 (port 1, at start) stays | **`u4.learn=00000203`**: port 2 learned while port 1's at-start tunnel is pending (`r1.txt`); enc1's USP 06:00.0 problem 31 (kept2's 8.1 issue for a never-seen enclosure, not chased) |
| c | | `XHCITR /eject id=<enc1 USP>` | "PCI Express standard Upstream Switch Port cannot be stopped here: not started - problem code 31." (`s01.png`) - port 2's eject owed behind the problem 31 |
| b2 (pid 183912, qmp 47873) | 1 | start | `u4.infer=00000100`, `u4.infer.bdf=00000200` (`r1-boot.txt`) |
| b2 | | `XHCITR /eject id=<enc0 USP>`, OK (`s03.png`) | **"Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (E:). The eject was confirmed to the Thunderbolt driver. The PCIe tunnel was closed." / "It is now safe to disconnect the device."** (`s04.png`); `cm.reenum.confirm`, `cm.reenum.eject`, `u4.eject.bridge=00000200`, `u4.eject=00000100`, `u4.eject.inferred=00010200`, `u4.infer.closed=00010200`, `cm.kept.gone=20030300` (`r2-eject.txt`) |

Both stopped by ACPI powerdown, exits confirmed; `pids.txt` in each
folder. Note: `R.CMD` (`XHCITR /report > COM1`) wrote nothing to the
serial file after the restart; `/report C:\r.txt` then `type ... >
COM1` worked.

### Left

- Port 2's eject after the narrowed learning: behind the problem 31.
- The base rule for a port pending alone attributes one gain of any
  shape (e.g. a controller's own function answering late while one
  port is pending alone): unchanged, outside this task.
- The owner's board: Link Capabilities and the far-end shape of its
  enclosures not read.

### Merges (w48-learn, w48-learn2)

w48-learn merged into `3.4.3.0` at `09ba3c3d` (2026-11-10). Its merge
review found that the narrowed learning rule could put a wrong binding in
the shared table through the host router's own integrated function, live
with `XhciCmInferAtStart` 0; fixed on w48-learn2 (a member counts only when
a tunnel's far end, an Upstream Switch Port, answers behind it), merged at
`ddf03ada`, reviewed, nothing material; the integration build of `ddf03ada`
passed. The shared q35 launchers now give the ASM4242 model's own xHCI
port hotplug=off and its tunnel ports power_controller_present=off, as the
board has.

# Run 48 - worker net8 (Thunderbolt networking, continuing net7)

## net8 takeover (2026-11-10, worker net8; 3.4.3.0 at a3b9c152)
- Worktree E:\xhci98-wt\w48-net8, branch w48-net8 from a3b9c152 (net7 merged).
- Scripts: net8\kit (net7's, net7->net8, kit7->kit8) + latch8.sh; net8\bin
  (gated.cmd, pkg.cmd, qt.cmd, kit3.py from net7; ht.cmd one host suite).
- PIDs: net8\pids.txt.

### Priority 0: merge-review finding on 0ed7ebec (coordinator) [medium]
- tbt_xdp.c 970-977: a peer sending a Changed Notification during every
  read kept the engine in PROPS (ReadOwed reasserted, tbtPropsAgain reset
  the slot and its budget each time): no READY (withdrawn directory never
  logged out), no ERROR (adopted-record cleanup bypassed). Same for IDS 3.2
  generation restarts.
- COMMIT 444b2779: PropsRestarts (reads afresh, both causes) survives
  tbtStartProps, renewed at adopt / TbtXdpPeerStart / firmware newpeer;
  after TBT_XDP_RESENDS: cm.xdp.props.spin, ERROR, block not taken, stale
  service or paths ended by tbtStartLogout. Record 35 revision 28 (3.18).
  Vectors test_changed_every_read, test_changed_every_failed_read
  (test_tbt_xdp 267/0; 11 failures without), test_xd_adopt_spin
  (test_tbt_icm 1133/0; fails without). test_u4xd 281/0, test_tbt_hw
  183/0, test_tbt_select 306/0. check-text OK.
- Review (--base a3b9c152): APPROVE, no material findings.
- 444b2779 gated x86 release (pre-step): PASSED, slot 1 (gated-x86.log;
  out\gated-x86-release.txt commit 444b2779 dirty=0). xhcicm.sys
  496,964 B, SHA-256 d0bc1e9a17f369ae0248f052e1062b3c66f52286afa37228859df8dd840fce6b
  (copy net8\rel\xhcicm.sys).
- 444b2779 gated amd64 release: PASSED (xhcicm E6461B38...F4ED).
  444b2779 gated x86 qemu: PASSED (xhcicm 22857CBC...D9DC).
- Kits: E:\xhci98-vm\p48\net8\kit8 (pkg.cmd x86 + mkkit.sh + TBSPEED,
  DEVCON, IRQON/IRQOFF.REG, NTIRQ.BAT; both xhcicm.sys = d0bc1e9a);
  kit8q = kit8 + XHCICMQ.SYS (22857CBC) + LATCH.BAT.
- BENCH KIT REFRESHED (backup net8\bench-backup): kit3.py 444b2779 495,156
  Ninth notes9.txt: 98SE\XHCICM.SYS 496,964 B d0bc1e9a...; SHA256.TXT,
  INSTDIAG.BAT size, BENCH-STEPS paragraph. XHCITR.EXE kept (3ffb7715).

## 48.38 latch leg (XP A with the qemu flavour + XhciCmQemuIrqDead 3, 98 SE B on)

### L1 (kit7q = 0ed7ebec qemu at A; p98m B, pxpm A): INVALID - 98 SE FAULT
- pre30: ping 20/20 1/16/3 ms; TBSPEED 98 SE -> XP 35.6-41.3 Mbit/s for 5 s,
  then 98 SE: "A fatal exception 0E has occurred at 0028:C19D8D03"
  (ring 0). A's report: 19,937 frames in, 0 errors, ring interrupt on (812).
- Located (memsave of B's C1980000-C19FC000): C19D8D03 lies between the
  TCPIP strings (C19CC9xx "TCPIP: Dummy Xmit called") and the MSTCP DDB
  (C19DA6B0): inside 98 SE's MSTCP/VTCP code, not in our images (the bytes
  8b542430 8b12 8bce 2bca 83f9c8 7425 match no kit .sys). Instruction
  mov ecx,[edx+20h] after a list walk (cmp of entry - 38h). B-regs.txt,
  B-spd-pre30.png in cells\L1.
- R1 (the same pair, fresh overlays): 98 SE -> XP 21.1 Mbit/s for 30 s,
  XP -> 98 SE 2.6 Mbit/s, ping 2/15/3 ms; no fault. Intermittent.
  REP98 at B: "Abort, Retry, Ignore, Fail?" (in L1 too) - not chased.

### L2 (kit8q: 444b2779 qemu xhcicm at A via LATCH.BAT 3; p98m B, pxpm A)
- A latched as designed: before LATCH "Ring interrupt: on (54 wake-ups)";
  LATCH.BAT wrote XhciCmQemuIrqDead 3 in Class\{4D36E97D...}\0038 and
  copied XHCICMQ.SYS; after the restart "Ring interrupt: off (fell back to
  checking every clock tick)", state up, logged in, cm.net.irq.open=1
  logged before the fall-back.
- B unusable: my script's Enter keys at B's desktop re-ran the Run MRU
  (SPD98 at the same APIPA address - XP's APIPA is stable per MAC) before
  A was up; then "Write fault error writing device COM1" in REP98 and no
  COM1 output for the rest (no IP, no traffic read). The Enter loop removed
  from latch8.sh; redone as L3.

### L3 (as L2, the Enter loop removed): 98 SE FAULT AGAIN
- pre30: ping 20/20 2/16/3 ms; TBSPEED 98 SE -> XP 32.5-42.0 Mbit/s for
  25 s, then "A fatal exception 0E has occurred at 0028:C19D8E73" (same
  module as L1). A: 83,927 frames in, 0 errors, on (3,409 wake-ups).
  After LATCH and A's restart, A was "connecting" (B dead): no reading.
- So 2 of 3 runs at ~40 Mbit/s faulted in MSTCP; the 21 Mbit/s run did not.
- Hypothesis: xhcinet (deserialized NDIS 5) completes sends at the
  caller's IRQL; a serialized miniport's completions come at DISPATCH
  under NDIS's lock. On 98 SE a PASSIVE completion can be preempted by the
  receive indication's DPC inside MSTCP. COMMIT (below): netSendDone
  raises to DISPATCH around every NdisMSendComplete; record 35 rev 29 7.5.
- L4: latch with only a 2 s pre test (PRE=2) so B lives to the latch.
- 7c063945: review (--base 444b2779) APPROVE, no material findings (asked to
  qualify the comment: the cause inferred; done, uncommitted until the
  legs). Gated x86 release PASSED slot 2 (gated-x86-2.log; commit 7c063945
  dirty=0; xhcinet.sys 7C5CA0AF...0B23, xhcicm.sys CAC9DA1E...8EE0).
  Kit E:\xhci98-vm\p48\net8\kit9. Image p98p = p98m + kit9 (up98.sh).

### L4 (as L3 with PRE=2): THE LATCH READ at the XP end
- pre (2 s): ping 20/20 2/17/4 ms; 98 SE -> XP 19.5, XP -> 98 SE 2.9
  Mbit/s; A "Ring interrupt: on (438 wake-ups)", 3,818 frames in.
- LATCH.BAT 3 at A (Class\...\0038), XHCICMQ.SYS copied, A restarted:
  "Ring interrupt: off (fell back to checking every clock tick)", State up,
  logged in (the link came back with the 98 SE end).
- Traffic after the latch NOT read: B's COM1 "Write fault error writing
  device COM1" / "Abort, Retry, Ignore, Fail?" in REP98 after the pre
  test (seen in L1, R1, L2 too: a 9x harness fault after a TBSPEED run;
  the waits sent Enter, which only repeats the prompt). lib.sh's 9x waits
  now send r then Enter.

### R2-R4: 7c063945 (kit9) at the 98 SE end (p98p), XP pxpm A, both on
| Cell | 98 SE -> XP | XP -> 98 SE | Result |
|---|---|---|---|
| R2 | 36.6 Mbit/s, 30 s | 2.60 | no fault; A on (7,296 wake-ups) |
| R3 | 14.5 Mbit/s, 30 s | 2.58 | no fault; A on (4,836) |
| R4 | ~15-18 Mbit/s for 17 s | - | FATAL EXCEPTION 0E at 0028:C19D99C3 (same MSTCP area) |
- So the DISPATCH_LEVEL completion did NOT cure it (provided p98p carries
  the new xhcinet.sys: up98 exit 4; checked below). The faults so far: 3 of
  6 runs with 98 SE sending at 15-40 Mbit/s under the data ring's interrupt.
- p98p read back (DIRN.BAT, IOSUBSYS): XHCINET.SYS 17,020 B (kit9's),
  XHCICM.SYS 496,964 - the new driver was in place when R4 faulted.
- 7c063945 therefore dropped from w48-net8 (reset to 444b2779; kept on
  local branch w48-net8-dispatch-try). The MSTCP fault stays OPEN.

### L5: latch with PRE=1 (B = p98p, i.e. 7c063945's xhcinet at B; A kit8q)
- pre (1 s): ping 20/20 1/18/5 ms; 98 SE -> XP 14.0, XP -> 98 SE 3.3
  Mbit/s; A on (153 wake-ups). LATCH.BAT 3, A restarted: "off (fell back
  to checking every clock tick)", up, logged in. Traffic after the latch
  again NOT read: B's REP98 never finished (no repdone, COM1 again), so
  B's SPD98 for latch30 never ran.

## Hand-over (net8)
- Branch w48-net8 = 444b2779 (P0 fix), review APPROVE, gated x86 release,
  amd64 release and x86 qemu PASSED. Bench kit = 444b2779 (Ninth).
- 48.38 latch: the fall-back read three times at the XP end (L2, L4, L5);
  traffic after it and a latch at the 98 SE end owed. Harness: 98 SE COM1
  write faults after TBSPEED runs block every later BAT (fix the 9x BATs
  to write to C:\ files and TYPE once, or close the finished DOS boxes).
- OPEN: 98 SE fatal exception 0E in MSTCP (3 of 6 runs sending 15-40
  Mbit/s under the data ring's interrupt). 7c063945 (completions at
  DISPATCH) did not cure it.
- 48.18 and 48.16 not started (prepnt8.sh drafted in net8\kit, untested).
- All my QEMUs stopped (pids.txt); no slot held; no review job.

### Bench round 9 preparation (2026-11-10) (worker r9)

Task 48.38, the data ring's interrupt. Round 8's decode is
`E:\xhci98-scratch\w48\r8\run48-r8.md`; this round builds the kit for its
"Round 9 needs" list and the owner's DefaultRcvWindow tip.

#### Branches and commits

- **w48-r9** (from 3.4.3.0 `1665b1a6`, worktree `E:\xhci98-wt\w48-r9doc`):
  - `5f097b0b` tuning.md: the TCP receive window tip for 98 SE and ME;
    release notes 3.4.3.0: a Networking line pointing at it.
  - `eecbca24` the tip keeps a value already set and its undo puts it back
    (from the doc review).
  - Review: first pass needs-attention (one medium: existing value not
    preserved), fixed in `eecbca24`; second pass approve, no findings.
- **w48-r9diag** (from w48-net8 `444b2779`, worktree
  `E:\xhci98-wt\w48-r9`; bench only, never merged):
  - `c8a834ae` (first built as `d1bdb71b` on 7c063945, kept on local
    branch w48-r9diag-7c0) xhcicm: CRC Error and Buffer Overflow frames noted apart
    (`CM_NET_DIAG_RXERR` in cm_net.c; `CmNetrDiagDue` pure in cm_netr.c,
    host vectors in test_cm_net).

#### Decision: the xhcinet.sys in the kit

REVISED (later the same day): net8 found 7c063945 did not cure the 0E
(its R4, 3 of 6 runs sending 15-40 Mbit/s) and reset w48-net8 to
444b2779. w48-r9diag was rebuilt on 444b2779 as `c8a834ae` (src/cm the
same as d1bdb71b), so the kit's xhcinet.sys is 3.4.3.0's miniport, as the
coordinator asked; against round 8 its only change is the stats callback.
STEPS.TXT warns of a fatal exception during runs 1 to 5 and says what to
do (coordinator, 2026-11-10); the kit is held until the coordinator says.
The first reasoning, kept for the record:

The coordinator asked for 3.4.3.0's xhcinet.sys. The kit's is built from
w48-r9diag, that is 3.4.3.0's miniport (src/net is identical between
444b2779 and 3.4.3.0 `1665b1a6`) plus w48-net8's `7c063945`: NDIS 5 send
completions at DISPATCH_LEVEL. net8 found that with the data ring's
interrupt on, 98 SE's MSTCP took a fatal exception 0E in two of three
30 s sends to XP, because a completion made at PASSIVE_LEVEL could be
preempted by the receive indication's DPC. Round 9 runs that exact
condition (the interrupt on, 98 SE sending ACKs at 200+ Mbit/s while
receiving, and sending in Test 2), so a kit without it risks a crash, and
the same race corrupting MSTCP's state is itself a candidate for round 8's
stall (a fourth candidate beside the three in run48-r8.md). Compared with
round 8 (3.4.2.0's xhcinet), round 9 therefore changes the miniport in two
ways: the stats callback and the completion IRQL. If the stalls go, the
completion fix is the lead; if they stay, it is excluded.

#### The diagnostic image

- Notes per kind (CRC Error, Buffer Overflow), at counts 1, 2, 4, 8 ...:
  `cm.net.rx.crc` / `cm.net.rx.bo` = count, `.ms` = CmLogClockMs, `.at` =
  RxFrames at that frame, and on the first `.lf` = length | flags << 16.
  Counted only where RxErrors counts (delivered frames), so crc + bo >=
  RxErrors (a frame with both counts in each).
- Gated build `build-driver.cmd all` x86, XHCI98_SELFTEST_SKIP_OK=1, slot
  gated.3 (taken and removed): BUILD + GATES PASSED (release, qemu);
  test_cm_net 92 checks, 0 failures; log gated-x86.log. Release hashes:
  xhcicm.sys 847aec17... (497,410 bytes), xhcinet.sys 5a6fbcd2... (17,018).
  The record says "uncommitted changes during the build" because I
  committed d1bdb71b while it ran; the tree content was the same.
- Review (base 7c063945): approve, no findings.

#### The kit (E:\xhci98-bench\p48-tbnet; staged in kit9\, scripts mk98.py,
mkw11.py, kit9.py)

98SE: INSTDIAG/RESTORE (both XHCICM.SYS in SYSTEM32\DRIVERS and XHCINET.SYS
in SYSTEM, each kept once as .ORJ), IRQON/IRQOFF (round 8's), WIN16K
(16060), WIN32K (32120), WIN64K (64240), E2E0/E2E1 (XhciCmNetE2E under
Services\USB, the machine key, which wins over the driver key), SPEED9
(netstat -s before, then TBSPEED -s -w 256), PINGF <addr>, SAVE (report,
winipcfg, netstat -s, MSTCP/USB/XHCIPFW keys exported, time).
WIN11: SPEED9.CMD -> SPEED9.PS1 (ping 20; Test 1 inbound 30 s with
background ping -l 32 and -l 1472 for 70 s; if Test 1 exits non-zero:
ping -l 32 x10, -l 1472 x10, Test 1b inbound on a new connection; Test 2
outbound; all into SPEED9-W.TXT with timestamps), CAPTURE9.CMD (elevated,
same with pktmon: filter -i <P14s>, --comp nics --pkt-size 128,
2048 MB, counters, stop, filter remove, etl2pcap), ACK1.CMD/.PS1
(TcpAckFrequency 1 on the USB4 adapter's Tcpip interface key, old value
kept in ACK-UNDO.TXT), UNDO9.CMD/.PS1 (puts it back, pktmon stop and
filter remove), COLLECT (round 8's).
-w is 256 alone: round 8 showed 98 SE ignores SO_RCVBUF inbound, and 256
beat 64 outbound.

#### Run plan (STEPS.TXT; 5 restarts: the start, runs 2 to 4 and the undo; 6 with run 6)

| Run | P14s settings | Framework | SAVE |
|---|---|---|---|
| 1 | IRQ on, 16060 | SPEED9; PINGF from the P14s | S1 |
| 2 | IRQ on, 32120 | SPEED9 | S2 |
| 3 | IRQ on, 64240 (round 8's S3) | CAPTURE9 | S3 |
| 4 | IRQ on, 64240, E2E 0 | SPEED9 | S4 |
| 5 | as 4, no restart | ACK1, cable replug, CAPTURE9, UNDO9, replug | S5 |
| 6 (opt.) | IRQ off, 64240, E2E 1 (round 8's S4, new miniport) | SPEED9 | S6 |

Reading it: 1/2/3 place the window threshold; 3 against round 8's S3 says
whether the send-completion fix (or the new miniport) changes the stall;
3's capture shows retransmissions, 98 SE's ACKs/window during a stall; the
background pings say whether small and full-size frames pass during it,
and Test 1b whether a new connection runs; 4 against 3 tests E2E; 5
against 4's outbound tests the stretch-ACK reading (5's capture proves
whether Windows honoured TcpAckFrequency); the diag notes split the
errored frames and place them (.at) against the stall; the miniport's
counters (RxNoBuffer, drops) come in every report; netstat -s
before/after gives 98 SE's retransmission counts.

#### Rebuild on 444b2779 and the dry run

- Gated `build-driver.cmd release` x86 of `c8a834ae` (slot gated.3, taken
  and removed): BUILD + GATES PASSED, test_cm_net 92/0, record commit
  c8a834ae clean. xhcicm.sys bcf74c38... (497,410), xhcinet.sys
  18d7dc09... (16,970); log gated-x86-2.log. Kit restaged (kit9.py).
- 98 SE dry run (QMP 47800, one guest; overlays g98\r9.qcow2 then a fresh
  g98\r9b.qcow2 over net7's p98n, -Usb4 tb3ar, no peer; PIDs in pids.txt,
  all stopped):
  - Boot 1: INSTDIAG (both kept as .ORJ, both copied, sizes right; a second
    INSTDIAG keeps the .ORJ files), IRQON, WIN16K (MSTCP0.REG written first,
    no DefaultRcvWindow in it), WIN32K, E2E0: all as written.
  - Boot 2 (the bench drivers loaded, normal mode, desktop): PINGF without
    and with an address, SAVE (S1: REPORT, IPCFG, NETST, TIME, MSTCP.REG
    with DefaultRcvWindow 32120, USB.REG with XhciCmRingInterrupt 1 and
    XhciCmNetE2E 0, XHCIPFW.REG with XhciPfwNhiLine 0), the report's
    "Network driver: xhcinet.sys, MTU 1500" line (the 3.4.3.0 miniport's
    stats reached the report; round 8 said "not given"), E2E1, IRQOFF,
    SAVE again (S2 shows the values back), SPEED9 (NETST9.TXT and
    SPEED9.TXT written; TBSPEED waited; Ctrl+C ended it), RESTORE (run by
    hand: both drivers back, sizes of the originals).
  - In the emulator the report said "Ring interrupt: off (checked every
    clock tick)" with IRQON; not investigated (no peer, emulator board).
  - Emulator noise, not the kit: with the 7c063945 build one boot hung in
    text mode (EIP c002fa19) and the next came up in Safe mode; the clean
    run on c8a834ae booted normally. COMMAND.COM skipped two lines of my
    harness BAT once (RESTORE then run by hand); `CALL x > COM1` does not
    redirect a called BAT's ECHOs on 98 SE.
- The WIN11 scripts were parse-checked only (PowerShell parser; pktmon and
  TBSPEED not run on the host by rule). pktmon syntax is from Windows 11's
  documented command line, unverified here: `filter add -i`, `start
  --capture --comp nics --pkt-size 128 --file-name --file-size`,
  `counters`, `stop`, `filter remove`, `etl2pcap --out`. If pktmon refuses
  to start, SPEED9.PS1 logs it and runs the tests without a capture.

#### Status

The kit is staged in E:\xhci98-scratch\w48\r9\kit9 and NOT yet copied into
E:\xhci98-bench\p48-tbnet (the coordinator may hold it for net9's 0E
result); the bench folder still holds round 8's kit (backed up in
bench-r8-backup). To hand it over: replace the bench folder's 98SE, WIN11,
STEPS.TXT and BENCH-STEPS.txt with kit9's.

### Merges (w48-net8, w48-r9)

w48-net8 (the reads made afresh bounded across reads) merged at `65642159`;
w48-r9's `tuning.md` tip for 98 SE and ME's TCP receive window merged at
`de877f8e` (2026-11-10), both reviewed, nothing material. The round-9 bench
kit is staged and held for the 98 SE fatal exception found by net8 (worker
net9).

## 48.27 The prefetchable windows above 4 GB on the 32-bit 10: the filter role (2026-11-09)

Design record 32 section 11.5.6 (`09c4f430`), reviewed and revised to convergence (three rounds); the owner's answers of 2026-11-09 in section 11.5.7 (`94edee72`): Q26 (a) offered and ticked on 10 as on 8 and 8.1, Q27 (b) the repair installation read first, Q28 (a) fast startup left alone, Q29 (b) from version 2004 (19041). Code `f7faf5ce`, `9ab5754a`: `PfwNtRole` gives 10.0 the filter role; `inst_pfw_sys` takes the build (10 from 19041 `INST_PFW_SYS_FILTER`, earlier `INST_PFW_SYS_GONE` as before); the Summary, the console's question and `/uninstall /list` preview the removal of an earlier entry-role install as `pfwsys_run_locked` makes it, and say the filter is left unchanged where that removal would fail (its record or either image unreadable); the console's lines, the two refusals, the usage line and README.TXT follow. Host tests (`test\run-host-tests.cmd`): PASSED, `test_inst` 8984 checks, `test_pfw` 545 checks, 0 failures; `xhciinst\build.cmd`: built clean.

Legs: owed (below, when run).

## 48.32 The transfers kept by a pinned containment (design, 2026-11-09)

Design record 17 section 4.13.7, in review. Owner: Q-PCR.1 D3hot not built; Q-PCR.2 the PCI Express Base Specification added by the owner, its rules cited before merge. Open for the owner: Q-PCR.3 (the drain of writes in flight at the link's fall: the same footing as the shipping proofs or not built) and Q-PCR.4 (the stop's existing all-ones fallback: ask the link route and fail closed without it). No code before those answers.

## 48.33 Every transfer staged through buffers the driver owns (design, 2026-11-09)

Design record 13 section 11.11 and record 17 section 4.13.8 (revision 16), written before code and reviewed and revised over several rounds (`46397ba0`, five rounds). The owner's questions Q-STG.1 (the client buffer's system address: a driver-owned 64 KiB mapping window recommended), Q-STG.2 (the shared pool's budget: a setting, 2 MiB, recommended), Q-STG.3 (the quarantine's bound: kept for the boot, a 64 MiB per-driver cap, recommended) and Q-STG.4 (what may stay direct: nothing, recommended) are put to the owner; no code before the answers. No build, no legs.

48.27 gated build (2026-11-09): `scripts\build-driver.cmd release` (x86) at `46397ba0` (48.27's code `f7faf5ce`..`5683edf4` plus docs): BUILD + GATES PASSED; xhci98.sys `31800DAD...B5FE`, xhciuas.sys `A9D6BA1C...BED1`, xhcicm.sys `E861F85F...DE3B`, xhcihlpr.sys `FC5DDB84...DB1C` (`out\gated-x86-release.txt`). Package `out\pkg-release-x86`. Legs owed (next run).

## 48.33 Every transfer staged through buffers the driver owns: the code (2026-11-09)

The owner's answers of 2026-11-09 (Q-STG.1 (a), Q-STG.2 (b), Q-STG.3 (a), Q-STG.4 (a)) are recorded in record 13 section 11.11.10 and record 17 section 4.13.8. Code on branch `w48-pci`: `8dd3c773` (staging: `src\hcd_stage.c`, the pure `src\xhci_stage.c` with `test\test_stage.c`, the map pump gone, the boot ledger capped at 64 MiB, `XhciStagePoolKiB` in the INFs, the INF gate and `tuning.md`), reviewed and revised in `13f208d3` and `9220ba30`; record 13 section 11.11.11 states the code as built and its departures (promotion withdrawn; a request owns its pipe from its start when longer than its pipe's reserve; x86 under NT maps through `MDL_MAPPING_CAN_FAIL`, the helper's routine service not asked; an unproven stop releases the child PDOs' held requests at the controller's stop). Host tests (`test\run-host-tests.cmd`): PASSED, `test_stage` 2103 checks, 0 failures.

48.33 reviews and builds (2026-11-09): reviewed in five areas (the pool and admission; the copies and the mapping; isochronous; the unproven stop, the kept memory and the ledger; 98/ME against NT and the gates) and revised until each returned nothing material: `13f208d3` (a copy that fails ends its request when it must, decided under the controller lock; the staging DPC and timer run with admission closed; the control slab mandatory; the give-up on the tolerance clock; isochronous packets packed from packet 0's offset, only good packets copied out), `9220ba30` (an unproven stop releases the child PDOs' held requests at the controller's stop; zero-length requests through admission; copy-outs bounded at 1 MiB a pass; no record touched once queued again), `6218eca6` (partial copies charged to the bound); after the merge of `3.4.3.0` (`295d4791`, owner-approved, no conflicts) the gated builds found two faults, fixed and reviewed: `0ec4d0a7` (the INF gate self-tests' value block lacked the new row's comment lines) and `74c59858` (`KeAcquireSpinLock` in place of the `...AtDpcLevel` forms, which the x86 import list does not admit; `HcdPoolMdlBuild` and the `MmBuildMdlForNonPagedPool` rows removed, unused once a `TransferBuffer` is copied through its own address). `scripts\build-driver.cmd all` at `74c59858`: x86 BUILD + GATES PASSED (release `xhci98.sys` `69490C6C...0067`, `xhciuas.sys` `88F870F1...C812`; qemu `xhci98.sys` `3D538ACD...ADD8C1`), amd64 BUILD + GATES PASSED (release `xhci98.sys` `A3878C99...C46C`; qemu `D1B564F7...3D07`). Packages `out\pkg-release-x86`, `out\pkg-qemu-x86`.

48.33 first leg (2026-11-09; 7 x86 q35 base, fresh overlay E:\xhci98-vm\p48\pci\s7, -smp 2, qemu-xhci behind root port rpx with a QEMU usb-storage disk and a usb-uas disk, each an overlay of the shared FAT image): `INSTALL.BAT /console /yes /nostor98` of `out\pkg-release-x86` (`74c59858`; installer 3.4.2.0 version string): xHCI98 USB 3 Controller installed, restart; `xhci98.sys` loaded `69490C6C...0067` (certutil); both disks enumerated (`USBSTOR\DISK&VEN_QEMU...` and `XHCIUAS\DISK&VEN_QEMU...`; the second taken online with diskpart, the two images sharing one disk signature); a 38,616,576-byte file copied to each disk and back and compared with `fc /b`, three rounds each: D: PASS x3, F: PASS x3; no device with a problem but ACPI\QEMU0002 (28). Then the qemu flavour's `xhci98.sys` (`3D538ACD...ADD8C1`) copied over it with `XhciQemuStageFault` 1 (every stop's quiesce reported not proven) and a restart: the controller ran (`sc query` RUNNING, both disks mounted); a QMP `device_del` of the controller was refused by Windows ("Problem Ejecting xHCI98 USB 3 Controller: This device is currently in use"), so the unproven stop was not reached on this leg. Evidence: s7\serial-leg1.log, s7\a*.png, b*.png, c*.png.

Owed for 48.33: the unproven stop with a request pending on a guest (a surprise removal, e.g. usb4-emu tb3ar `qom-set tbrp present false`, or a Device Manager disable under the fault switch), the measurement (before = `E:\xhci98-scratch\w48\pci\pkg-before-x86`, `31800DAD`, after = `out\pkg-release-x86`) on 98 SE, 2000 and 7 x64 with audio beside, the legs on 98 SE, ME, 2000, XP and 7 x64, and the 98 SE reading of `MmMapLockedPages`' failure.

48.27 Q27 leg (2026-11-09): see QUESTIONS.md "48.27 Q27 - the repair installation, READ"; Windows Setup's repair installation removed the filter's service and its UpperFilters entry, left the image and the installer's record, and Windows started normally (twice, the second a cold start); the root port went back to problem 12. Record 32 section 11.5.6's other 10 x86 legs and the 8.1 preview legs: owed.

### Merge (w48-pci)

The code of 48.27 and 48.33 reached `3.4.3.0` with w48-pci5's merge `f372fa2f` (2026-11-10); 48.32 closed as superseded by 48.33 (owner, 2026-11-10), nothing built.

## 48.1 usb-wdm-free's open items: audio, video and MIDI (worker wdma, 2026-11-09)

Worked in usb-wdm-free, branch `w48-wdma` from `main` `3f41d8c`; the
records are that repository's `docs/testing/2026-11-09-*.md`, the open
items updated in its `docs/testing/open-items.md`. Development legs in
QEMU (TCG); development host A ran at about 94% CPU with other workers'
guests, which cost some legs (said where).

- **`uac1` with the real Sound Blaster Play! 2 (`041E:323D`) on 98 SE**
  (owner, 2026-11-09: first). Claimed in `E:\xhci98-vm\devices.txt` and
  released; the host's WinUSB binding unchanged. Why nothing was audible
  on 2026-11-07: QEMU `usb-host` sends isochronous OUT in transfers of 32
  packets, and libusb's WinUSB backend refuses an OUT transfer whose
  non-final packets are short of the endpoint's maximum (192-byte packets
  here): `libusb_submit_transfer [iso] -2 [INVALID_PARAM]` for every
  playback transfer, while the guest saw its buffers complete. With
  `usb-host,...,isobsize=1` under xHCI98: install, binding, list, mixer,
  volume and mute ramp, playback 48 and 44.1 kHz, recording 48 and 44.1
  kHz (the device's input data) all PASS with no host error, three legs;
  QEMU still dropped about 3.6% of the OUT packets (`out_of_bufs`, 32 or
  64 host transfers alike). Under NUSB's UHCI companion no option carried
  both directions (default: OUT refused, recording works; `isobsize=1`:
  OUT passes, recording starves): QEMU's UHCI model through passthrough.
  Owed to the bench: listening, recording from a microphone, NUSB.
  Record `2026-11-09-uac1-play2-real.md`.
- **47.18 ALC4080 under `uac2`, the non-hardware parts.** The replay of
  the owner's capture with 0.1.9: every output and input pair, and two
  streams at once (two outputs; an output and an input; the overlap read
  in the simulator's log; under 98 SE's xHCI98 one output-and-input case
  did not overlap), PASS on 98 SE under NUSB's EHCI and xHCI98, 7
  x64 and Vista x64 under xHCI98 and Windows' EHCI. Names on Vista and 7:
  Speakers, Headphones, Speakers (S/PDIF, by design), Microphone, Line;
  the analog connector input an "Analog Connector" endpoint in state
  `0x10000001`, not in WinMM (as Windows 10's own driver). Left: the real
  board. Record `2026-11-09-alc4080-left.md`.
- **47.11 preferred device, the non-real-device legs.** 98 SE and ME keep
  the choice in force and the user's choice apart in the Sound Mapper key;
  a device absent at a start is replaced in force by a present one, and
  the user's choice comes back when it is there: the likely cause of the
  recording choice that moved after the restart (98 SE's own). ME falls
  back and returns the same way, but the values written to its key did not
  hold its choice (its panel not used). Left: real devices, ME's panel,
  2000. Record `2026-11-09-uac2-pref-98se.md`.
- **`uvc98` Full Speed under 98 SE's own UHCI.** Cause, read with the
  trace build: after a guest stall the UHCI stack refused every
  resubmitted transfer with `USBD_STATUS_BAD_START_FRAME` and the driver's
  8-failure rule ended the stream (READ 3 empty). Fix `f9f6aed` (design
  `uvc.md` 5.4a): a refusal resyncs the stream at explicit start frames
  (current + 8 + k x 16), then ASAP again; 4 resyncs with no good transfer
  between end it. Reviewed and revised until nothing material remained.
  With the fix: 98 SE's own UHCI 5/5, NUSB's UHCI companion 5/5 (0.1.9:
  0/4 and 0/5), ME's own UHCI 4/4, xHCI98 High Speed profiles all PASS. Left: the
  NUSB High Speed cells and xHCI98 Full Speed on a quieter host (both lost
  to the host today; the 0.1.9 trace control failed NUSB High Speed the
  same way, and the trace showed the new code idle on both; the loaded host is suspected). Record
  `2026-11-09-uvc98-fs-uhci.md`.
- **`usbmidi` fatal exception 0E watch.** 6 more legs of the matrix's
  `multi` leg on 98 SE under xHCI98, 16 `MIDITEST` loopbacks: no fault
  (1 in 15 legs now). Still a watch item. Record
  `2026-11-09-usbmidi-multi-watch.md`.

### Merges (usb-wdm-free, w48-wdma)

Merged into usb-wdm-free main at `78c2e88` and `30ae0a6` (2026-11-09).

## 48.1 usb-wdm-free open items, the serial and Ethernet half (2026-11-09)

Worker wdmb, development host A, usb-wdm-free branch `w48-wdmb` from
`3f41d8c` (worktree `E:\usb-wdm-free-wt\w48-wdmb`); usb-wdm-nonfree only
read. Every item is recorded in usb-wdm-free `docs/testing/open-items.md`
and the records it points at; this is the run's summary.

### Ethernet: EEE and the Advanced tab, as the makers' INFs offer them

- Design: usb-wdm-free `docs/design/ethernet.md` section 4.10 (`6adff1a`,
  reviewed and revised before code, three rounds): Realtek's 7 INF
  7.61.20 (and its XP and Vista INFs, and the Windows 10 INF of the
  RTL8157/8159) and ASIX's AX88179 and AX88772B INFs read per chip and
  system; one keyword set per package, the newest maker INF's names and
  defaults on every system; what is not offered and why; what each
  keyword does in each chip's code; the core/chip interface; the INFs;
  the legs.
- Code (`13d83ee`, review fixes `b90798d` to `9ca35a1`, six rounds):
  the shared core (`netcore.c`, identical in the four NDIS packages)
  reads each chip's keywords as text or a DWORD at `MiniportInitialize`
  (malformed or not-offered values take the default; the transmit
  checksum keywords on NT only) and hands them to the chip
  (`NET_PROBE.Settings`); a keyword the chip has no function for is
  reported back (`NET_PARAMS.Ignored`) and traced. `rtl815x`:
  `*SpeedDuplex` (forced 10/100, single-speed advertisement at 1000,
  2.5G, 5G, 10G), `*FlowControl`, `*EEE`, `EEEMaxSupportSpeed`,
  `EnableGreenEthernet`, `AdvancedEEE`, `EnableExtraPowerSaving`, EEE,
  green, EEE+ and U2P3 now off by default (Realtek's 7 default; before
  this they were on as Linux sets them). `ax88179`: `*SpeedDuplex`,
  `*FlowControl` (IEEE 802.3 Table 28B-3 resolution, pause only on a
  full-duplex autonegotiated link), `*EEE` on by default (owner,
  2026-11-09: ASIX's 7 default). `ax88772`: `*SpeedDuplex` and
  `*FlowControl` (the SR9800: speed only). `cdcnet`: `NetworkAddress`,
  sent as NCM `SetNetAddress` before the data interface opens (a resume
  that cannot restore it fails the device and the reset reports
  `NDIS_STATUS_HARD_ERRORS`). INFs regenerated (9x and NT forms; Realtek
  classes by id). Test aid `NETSET.EXE` (driver-key keywords, list, set,
  delete).
- Builds: every network package's `ndis5` free and trace and `ndis51`
  builds, import gates and host tests PASS at `867e11e`
  (`E:\xhci98-scratch\w48\wdmb\buildall2.log`; `ax88772`'s core test
  line 897, a known timing flake, failed once and passed on two reruns).
- Legs (simulated chips; usb-wdm-free `docs/testing/2026-11-09-advanced-tab.md`):
  `rtl815x` on 98 SE under xHCI98 (RTL8156B at SuperSpeed, every keyword
  and value class, 17 of 17 PASS) and under NUSB's EHCI (High Speed, the
  cases that apply, PASS), its NT form on XP (PASS); `ax88179`, `ax88772`,
  `cdcnet` (`NetworkAddress`) on 98 SE under xHCI98 PASS; the Advanced
  tab itself on 98 SE (`netdi.dll` lists the eight keywords, starred
  names included; a value changed in the dialog reached the driver key
  as `REG_SZ`).
- A real AX88179 (the owner's, passed through with QEMU `usb-host`, 98
  SE under xHCI98, the host's 10GbE port as link partner;
  `docs/testing/2026-11-09-ax88179-eee-real.md`): EEE on, link 1000 full,
  500 pings of 1472 bytes and TCP host to guest 3.89 Mbit/s with no loss
  or receive error; EEE off, the link and TCP rate the same. Once in two
  hot replugs (EEE on) the guest stopped answering after the driver had
  restarted: cause not known, open. Design 12.3's bench note closed with
  this reading.
- Open (open-items.md): `rtl815x` on 7 x64; the Advanced tab on an NT
  system; `ax88179`, `ax88772`, `cdcnet` under NUSB and on ME, 2000, XP,
  7; real chips per setting (Realtek's); EEE negotiation is not observable.

### `rtl815x` in the 9x matrix: the harness

`rtlemu.py` joins a bulk-OUT transfer QEMU's UHCI hands over in 64-byte
pieces (`ec8703a`); the matrix harness makes at most five new adapters
per fresh overlay (98 SE binds TCP/IP to the first five) and splits a
driver's shapes into such jobs. Rerun: ME's own UHCI (4 chips and 4
rebrand ids at Full Speed) and 98 SE NUSB's Full Speed shapes (4 chips),
12 of 12 PASS (ping, TCP, unplug under traffic, replug, restart), where
the matrix had 0 of 12 (`docs/testing/2026-11-09-rtl815x-9x-harness.md`).
Closed in open-items.md (`4338c40`).

### 47.27 SR9700 and SR9800, the parts without hardware

usb-wdm-free `docs/testing/2026-11-09-sr9700-sr9800-rest.md` (`ea47de1`),
on simulators. `sremu.py --composite` models the `0FE6:9702` (a CD-ROM
interface 0 with no endpoints, the Ethernet on interface 1; `7c1142b`,
`8fd3e92`, reviewed); `ax7emu.py`'s link-up logging fixed for a chip
without a PHY (`5eb6700`; it had voided the first composite, Vista and
own-UHCI runs, rerun). XP: the composite split by `usbccgp`, `MI_01`
bound to `sr9700f`, DHCP, pings, TCP on the first plug, replug and
restart PASS (`MI_00` code 10, no endpoints modelled). Vista x86: the
SR9700 and the SR9800 (also at 10 half) PASS. ME with the new exit: the
0.1.9 9x matrix's `sr9700f` cells on ME, and here the SR9800 with the
AX88772B/C shapes on ME under xHCI98 3.4.2.0, PASS. The SR9700 under 98
SE's own UHCI (no NUSB), Full Speed: ping, TCP, unplug under traffic,
replug, restart and a link drop PASS (installed by `DevicePath`; the
floppy install path met 98 SE's "Unknown Device" wizard, void). Vista
x64: in the 0.1.9 NT matrix (guest-signed images, both stacks;
owner-signed under xHCI98). Open: the owner-signed images on Vista x64
under Windows' own stack; the real SR9700 and SR9800.

### 47.19 `usbcom`, the parts without hardware

usb-wdm-free `docs/testing/2026-11-09-usbcom-rest.md`, on simulators.
The chip legs on 98 SE under xHCI98 3.4.2.0: FT-X, FT232H, FT4232H, the
six PL2303 parts, CP2105 and CP2108, every `full` run passing every check
(64 KiB echo included), the other ports' `quick` runs and the FT-X's
pull, replug and restart PASS. ME under an EHCI stack: not runnable (NUSB
does not install on ME), a bench item. The 64 KiB IN loss under NUSB:
20 echoes each with the simulator logging every request, answer and
cancellation and QEMU tracing its EHCI guest-bug events: FT-X 20 of 20,
FT232H 19 of 20; at the one loss (8160 bytes, all sent by the simulator,
no late cancellation) QEMU's trace has the leg's only "guest updated
active qTD". Reading (supported, not proven): QEMU's EHCI model
(`ehci_state_fetchqtd` -> `ehci_cancel_queue`, its write-back refused
for a changed descriptor) drops the completion of bulk-IN packets a
device has already answered. No fault found in `usbcom`, no driver
change; a packet-level reading in the emulator is owed to usb4-emu's
QEMU, and the echo on a real EHCI controller with NUSB stays owed. The
CP2105 echo repeats were void (wrong `COMTEST` flags) and the CP2108's
fourth port is beyond the harness's COM1-COM16 list.

### Merges (usb-wdm-free, w48-wdmb)

Merged into usb-wdm-free main at `24d6f3f` and `30be538` (2026-11-09).

## 48.1 (uacf) uac1's format gap against 98 SE's usbaudio (usb-wdm-free, branch w48-uacf)

- Design: usb-wdm-free docs/design/uac2.md 26.12 (048542d), reviewed and revised (3 rounds, converged):
  convert in the driver, the pin offers 16-bit PCM for PCM8, IEEE_FLOAT, A-law, mu-law; MaxPacketsOnly OUT
  padded; MaxPacketsOnly IN for synchronous/adaptive PCM and float sources by a frames-owed count; Ff fed on
  an adaptive source's synch OUT endpoint every interval (8 failures in a row end the stream). Known limit
  (owner, 2026-11-09; 648c5a3): asynchronous MaxPacketsOnly capture, and MaxPacketsOnly capture in PCM8 or
  G.711, stay not carried (null start, no hang).
- Code: 77da30c (uac1 0.1.1.0, build 2); 7095e90 (a lost MaxPacketsOnly capture transfer advances the
  cadence per packet, UacConvLostPackets; simulator jitter no longer carries into the accumulator; unpadded
  OUT packets fail the simulator verdict; tests); 824d02c and 294c891 (simulator: PCM8 0x00 trailing frames
  up to the cadence's running whole frames count as audio). Code review: 4 more rounds after the first,
  the last with nothing material. All shared changes under UAC_V1.
- Host: uac1 test.cmd PASS (565533 checks); uac1simtest.py 51 tests OK.
- Build: uac1wdm.sys x86 free at 294c891 PASS (import gate PASS), SHA-256 5e1e4eff...e306. uac2 image
  unchanged by these commits (26.11's comparison stands: x64 byte-identical, x86 thunk order only).
- Legs, 98 SE under xHCI98 (base w98k, uac1 kit), simulator profiles, record usb-wdm-free
  docs/testing/2026-11-09-uac1-formats.md (525e524, d1f4c29): every profile bound with a wave device (no null start).
  Playback headset (control), pcm8, float, mpoout, alaw, mulaw: play 48k/44.1k/ramp PASS; model 437.0 to
  442.0 Hz, level within 0.3 dB of the PCM control (-25.5 dBFS: every stream, PCM included, reaches the
  device 5.5 dB below UACWAV's -20 dBFS on 98 SE, as uac2's 98 SE records; so 26.12's absolute criterion is
  not met; where the 5.5 dB goes (KMixer suspected) is a hypothesis). Capture adsrc, adsrc-hs,
  pcm8in, floatin, mpoin, mpoin48, alaw, mulaw: rec 48k/44.1k PASS, 999 to 1003 Hz, -6.0 dBFS (-5.8 to -6.2
  for G.711). adsrc synch values 48.0/44.1 (Full Speed), 6.0/5.5125 (High Speed), none off the rate. uac2
  hs24 play and record PASS on the same base. The first alaw session failed every step (play never selected
  an alternate; rec streamed on the wire with 0/8 buffers); alaw passed every step in two later runs on
  fresh overlays: watch item. 98 SE first-plug wedges (36.41) 9 times; floatin needed a rerun.
- Legs under 98 SE's own UHCI (as time allowed): formats play/record too, but stalled streams (alaw play
  44.1k, mulaw ramp, adsrc rec 48k) and two devices WinMM did not list after a slow first plug (mpoout,
  pcm8in); mpoin48 three wedges. Not settled (driver on UHCI, QEMU's UHCI model or the loaded host).
- Owed: NUSB's EHCI legs; own-UHCI causes; synch-pipe failure injection (the simulator has no such option
  yet); unplug while streaming for the new profiles; real devices.

### Merge (usb-wdm-free, w48-uacf)

Merged into usb-wdm-free main at `c959d43` (2026-11-09).

## 48.1 (part) rtl815x at USB 2.0 High Speed: sending speed (worker r15, usb-wdm-free w48-r15)

Owner's reading (2026-11-09): HP EliteBook 8470p, right-side connector
(Windows' own EHCI, High Speed), XP x64 SP2, real RTL8157, rtl815x
0.2.1.0, iperf 2.0.8 sending: 69.3 Mb/s. Task: make USB 2.0 throughput
reasonable, sending first.

- Read (usb-wdm-free `netcore.c`, `netusb.c`, `rtlchip.c` against Linux
  `r8152.c` at `f47d2236`, facts only): transmit already filled bulk-OUT
  transfers by bytes (up to 64 frames, 32 KiB on the RTL8157), 4 in
  flight, one copy per frame, sends completed only when their transfer
  completed; receive 8 x 32 KiB bulk-IN; the chip's receive aggregation
  (640 ns, 15 us extra timer, early size, `USB_RX_BUF_TH`) exactly as
  Linux sets the RTL8157 at High Speed. Linux: 4 x 16 KiB OUT, 10 x 32
  KiB IN, frees each frame at the fill, offers the large send.
- Change `39b6d1d` (usb-wdm-free w48-r15): sends complete once their
  frame is in a submitted transfer (large sends held), 8 bulk-OUT slots,
  10 bulk-IN; shared `netusb.c` in all four Ethernet packages; tests
  (`EarlyCompletion`); `rtlemu.py` joins EHCI's descriptor-sized pieces
  of one bulk-OUT transfer (it split a 32 KiB transfer and stopped);
  design `ethernet.md` section 4.11. Reviewed: approve, no findings.
- Rig: XP x64 SP2 q35 overlay, no xHCI, `usb-ehci` (Windows' own
  `usbehci`), `rtlemu.py --chip 8157 --speed high --udp` bridged to QEMU
  user networking; TBSPEED 1.0 (x86, WOW64) in the guest against a
  TBSPEED-protocol Python server on the host. TCG; relative only.
- Numbers (3 interleaved rounds, Mb/s, means; single runs spread up to
  5x on the busy host): out with system buffers B 17.0 -> N1 22.2, out
  with 64 KiB 13.1 -> 14.8, in 61.1 -> 78.2; N1 ahead in 7 of 9 pairs.
  N1's trace: at most 8 of 8 bulk-OUT slots busy and 10 of 10 bulk-IN
  out in every round; frames per bulk-OUT transfer below 2 in both.
- Builds: rtl815x ndis5/ndis51 free/trace PASS; ax88179 ndis5,
  ax88772 and sr9700 ndis5/ndis51 free PASS; host tests of all four
  packages PASS; `rtlsimtest.py` 43 OK.
- Kit: `E:\xhci98-bench\p48-8470p\RTL815X\` (INSTALL, SPEED, RESTORE;
  section "RTL815X" in BENCH-STEPS.txt). Unsigned x64 image; XP x64 needs
  no signature.
- Owed: the owner's bench reading; 98 SE and SuperSpeed legs of this
  build; receiving at High Speed (after the reading).
- Record: usb-wdm-free `docs/testing/2026-11-09-rtl815x-usb2-throughput.md`;
  open item in `docs/testing/open-items.md`.

### Merge (usb-wdm-free, w48-r15)

Not merged; parked. The owner's bench reading (usb-wdm-free `88dccb6`, 2026-11-09): sending at High Speed is limited by XP x64's default TCP window, not the driver; the trial build left unmerged (owner).

## Issue 29: the 8470p's RTL8157, a SuperSpeed link that keeps failing (2026-11-09)

Worker r57, branch `w48-r57` from `1b4814a6`. Commits `4066a71e` (design
record 13 section 21, the fix, host vectors), `418d5c06` (a hold
release's unreadable reads counted toward the recovery), `da3fe9aa` and
`410c4d0c` (21.2: what else Linux does on Panther Point).

### The report

The owner's HP EliteBook 8470p (`8086:1E31`, subsystem `103C:179B`, BIOS
68ICF F.74), XP x64 SP2, `3.4.2.0`, `XhciIntelPortSwitch` 1 machine-wide,
everything else default. A Realtek RTL8157 (`0BDA:8157`) on either left
connector: "not recognised", XHCITR "the device failed enumeration" on
USB 2.0 port 4. On the right side (EHCI, Windows' driver) it works with
`rtl815x` at High Speed. Not tried on the 8470p under any other OS. The
same unit ran at 5 Gb/s on development host A in passthrough under the
host's own stack (`usb-wdm-free` `docs/testing/2026-11-07-rtl8157-hardware.md`).
Evidence: `temp\8470p-rtl8157\2026-11-09_173858\` (not tracked).

### The decode (record 13 section 21.1)

| Note | Meaning |
|---|---|
| `enum.port.look=040080B9`, `reset=040002A0`, `fail=04010001`, `end=040D0100` | port 4 (USB 2.0): CCS, PLS 7, CSC; the reset left CCS 0, PLS 5; cause 1 (reset), attempt 0, no retry; Failed |
| `080410B0` / `08008A83` x4, `quiet=08000008` | port 8: Empty, warm reset, PLS 6 (SS.Inactive) CCS 0, CSC PLC / CONNECT fed, CCS PED U0, CSC WRC PRC; budget of 8 bursts spent, 346 refused |
| `07008083` ... `slot=07010001`, `dev.ss.vidpid=0BDA8157`, `maxpower.ma=0220` (544), `end=070A0000` | port 7: a fresh attach (CSC only), SuperSpeed (PSIV 4, `rate=0700C350` 5000 Mbit/s), slot 1, Present |
| `link.keep=00070007`, `07B450B0` | location 7, keep refused 7 (not UAS); Bound, fed disconnect, warm reset |
| second run (slot 2), `link.keep=00070004`, `07A450B0` | refused 4 (not started); Present, dropped |
| `pnp.pdo=00071700`, `00070200` | the first PDO: IRP_MN_SURPRISE_REMOVAL (0x17), IRP_MN_REMOVE_DEVICE (0x02) |
| `070410B0` / `07008A83` x3, `quiet=07000008` | as port 8; 178 refused |
| `SsWarmResets` 270 (272 at the dump), `SsLinksGivenUp` 0 | |

No enumeration reset followed any warm reset's CONNECT: the link was in
SS.Inactive again before the 100 ms attach debounce ended
(`HCD_DEBOUNCE_MS`, `hcdPerform`).

### The diagnosis

- The link (not ours): it trains and fails to SS.Inactive within 100 ms of
  each warm reset; only a fresh plug held long enough to enumerate. The
  cause (connector, cable, power, the pairing) is not in the snapshot.
- Ours: 29-A.2's budget (`XHCI_LINK_MAX_WARM_RESETS` 3) is refilled by
  every trained reading (`XhciLinkDecide`), and each warm reset ends
  trained, so the port is warm-reset for ever (about two a second) and the
  device never reaches USB 2.0. A give-up would not reach it either: a
  device in SS.Inactive leaves it only on a warm reset or the loss of the
  host's terminations (USB 3.2 7.5.2), which the host withdraws by PED = 1
  (xHCI 1.2c 4.19.1.2.1: from any state but Powered-off).
- Port 4 (an inference; the notes give neither device nor speed): the
  device on USB 2.0 first, sent back to SuperSpeed by the enumeration's
  USB 2.0 reset (USB 3.2 10.18.1, Figure 10-26, eSS.Disabled count < 3).
- `XhciLinkKeep` 0 changes nothing here (the keep already refused, 7).
- Linux v6.12 (facts): `port_event` warm-resets at every event with no
  count; debounce 100 ms stable / 25 ms steps / 2 s; 4 init tries with a
  power cycle halfway; `hub_port_disable` on SuperSpeed sets U3. Its
  `8086:1E31` quirks (`XHCI_EP_LIMIT_QUIRK`, `XHCI_SW_BW_CHECKING`,
  `XHCI_SPURIOUS_REBOOT`, plus the Intel-wide `XHCI_LPM_SUPPORT`,
  `XHCI_INTEL_HOST`, `XHCI_AVOID_BEI`) do not concern the link; the
  compliance-mode timer covers HP Z420/Z620/Z820/Z1 only.

### The change (record 13 section 21.3)

A USB3 root port's ordinary warm resets for SS.Inactive or Compliance
(after the link keep, not CAS, not `XhciStuckLinkReset`) are counted in a
600-tick window (`XHCI_LINK_FLAP`, `HCD_PORT.Flap`); the first three are
made, the fourth sends the port to USB 2.0: a hold entry claimed under the
controller lock (`HCD_HOLD_REASON_LINK_FLAP`, kind `XHCI_HOLD_FLAP`,
`SettleGen`), then PED written on the failing sample. Released by the
companion's departure after a connect (no identity). No hold possible:
given up until a fresh attach. Notes `link.flap` (31:24 port, 23:16 event
1 counted / 2 sent / 3 given up / 4 released, 7:0 count); the hold counts
in `HoldsUnidentified`. A release whose PORTSC read is unreadable stays
owed and counts through `hcdPortUnreadable` (also 29-A.5's holds).

Review: the design converged in two rounds (the first found that PED acts
from any state, so the send moved from the warm reset's end to the failing
sample, and that the hold claim must be one lock section); the code in
two (an unbounded owed-release retry fixed in `418d5c06`), the last
"approve".

### Builds and tests

Gated build at `418d5c06` (`XHCI98_SELFTEST_SKIP_OK=1`, slot gated.3):
x86 `all` "BUILD + GATES PASSED (release qemu)", host tests "Host tests
PASSED" (`E:\xhci98-scratch\w48\r57\build-x86.log`); amd64 `release`
"BUILD + GATES PASSED (release)" (`build-amd64.log`). Release x64
`xhci98.sys` 401,920 bytes, SHA-256 `382c3693...abdbe12` (the kit's);
x86 release SHA-256 `41009844...7f5e0`. `test_link` 262 checks before the
full run. The `xhcidi` self-test reported "not run, no
xhciinst\di\xhcidi.exe" (the wrapper's `xhciinst\di\build.cmd` call
missed its directory); the gate passed regardless. Later commits
`da3fe9aa` and `410c4d0c` are docs only (record 13 section 21.2's Linux
comparison, reviewed to approve).

### Owed

- The owner's bench reading with `E:\xhci98-bench\p48-8470p\` (steps 1
  to 5, the Linux live reading optional).
- No VM leg: the emulator vehicle of 45.41 (a `link-drop` root-port patch)
  is no longer on development host A, and QEMU's xHCI has no companion
  move, so the USB 2.0 fallback cannot be read there.
- A SuperSpeed hub's downstream port refills its budget the same way
  (`hcd_sshub.c`); left for a later task.

### Round 2 (owner, 2026-11-09): XhciSsFlapFallback, hub ports, the Linux reading

- `ab30f3c9`: `XhciSsFlapFallback` (REG_DWORD, default 1, written by both
  INFs with no-overwrite; 0 = the failure past the limit is given up
  instead of sent, still bounded). Driver read in `hcd_ctl.c`
  (machine-wide `Services\USB` first, then the controller's key; note
  `link.flap.fallback`), INF gate table and count comment, gate fixtures,
  footprints, `tuning.md`, XHCITR's USB Advanced row (`tr_set.c`), the
  installer's row (`inst_core.c`) and `xhciinst\README.TXT`, `test_inst.c`,
  records 23 and 26 tables, record 13 section 21 F8.
- Same commit: SuperSpeed hub downstream ports (record 13 section 21.5).
  `HcdSsHubPortRecover` counts its BH_PORT_RESETs in the port's flap
  record; past the limit, with the switch on and the hub's USB 2.0 half
  paired (`hcdSsHubPartner`, taken out of `HcdSsHubCountPair`), the port
  goes to SS.Disabled through `HcdHubPortDisable` (its re-arm schedule
  unchanged); otherwise given up (`SsHubLinksGivenUp`) until the port
  reads empty. Note `link.flap.hub`. Pure `XhciLinkFlapDecide`; vectors in
  `test_link.c` (274 checks).
- `be6cde06`, `926ecbdb`, `8fbd4b41`: record 13 section 21.2, the owner's
  Linux live reading (Kubuntu 26.04.1, Linux 7.0.0-30,
  `temp\8470p-rtl8157\r8157-20261109-191058`): the RTL8157 "Cannot enable"
  every 4 s on the front connector (usb3-port4), SuperSpeed for 36 s then
  gone on the back one; a SanDisk 3.2 Gen1 stick in the front connector,
  during whose capture unattributed SuperSpeed descriptor timeouts on usb
  3-3 and a power cycle came before the stick at High Speed on usb 2-4.
  Conclusion, provisional: the laptop's SuperSpeed path. Open: the front
  connector's port numbers disagree between the two devices.
- Review: the code change one P1 (a missing forward declaration), then
  approve; the docs three rounds (the diagnosis made provisional, the
  stick's failures unattributed), then approve.
- Gated build at `8fbd4b41` with the tools rebuilt (an earlier run at
  `ab30f3c9` passed but its wrapper failed to rebuild xhciinst, xhcitr and
  xhcidi: `call build.cmd` without `.\`): x86 `all` "BUILD + GATES PASSED
  (release qemu)", Host tests PASSED, xhciinst, xhcitr and xhcidi
  self-tests passed (`build3-x86.log`); amd64 "BUILD + GATES PASSED
  (release)" (`build3-amd64.log`). Release x64 `xhci98.sys` 402,432 bytes,
  SHA-256 `d3ddac32...a215b239` (the kit's); x86 `4698451b...e0c8b276`.
- Kit `E:\xhci98-bench\p48-8470p\` refreshed: the reading now only
  confirms the move to USB 2.0, the High Speed path and the connector
  pairing; SAVE.CMD asks for the connector; FLAP0/FLAP1.CMD for the
  setting.

### Merge (w48-r57)

Merged into `3.4.3.0` at `e840d1df` (2026-11-09); the hub port's retry at `63128209` ("Merge (w48-r57, the hub retry)" above).

## 48.38 The 98 SE fatal exception 0E in MSTCP (2026-11-10, worker net9)

### Setup (2026-11-10)
- Worktree E:\xhci98-wt\w48-net9, branch w48-net9 from 3.4.3.0 65642159
  (net8's 444b2779 merged).
- Scripts: net9\kit (lib.sh, cell6.sh, spd6.sh, setv.sh from net8; run9.sh new),
  net9\kit\guest (9x BATs write C:\ files and TYPE them to COM1 once: UPD98,
  SPD98 [s|r|b], REP98; NT BATs net8's). Builds net9\bin\gated.cmd. PIDs net9\pids.txt.

### The cause (read, before any run)
- usb-wdm-free (the owner's sibling project, our own code) met the same fault:
  docs/testing/2026-11-07-98se-tcp.md and 2026-11-07-rtl8157-hardware.md
  there: "fatal exceptions 0E at 0028:C19243C3, C1924CB3 and C1924CC3, and
  once in VXD MSTCP(01) + 00004F5A" under sustained TCP on 98 SE, and TCP
  stalls (pool whole, every send completed). Three fixes there:
  1. every accepted send marked NDIS_STATUS_PENDING - xhcinet has it;
  2. the kernel spin lock, not NDIS.VXD's NdisAcquireSpinLock - xhcinet has it
     (USE_KLOCKS in src\net\ndis5\sources);
  3. no receive indication and no send completion from a WDM completion /
     DPC context on 98 SE and ME: queued, made by an NDIS miniport timer
     routine (NdisMSetTimer 0). After it: 0 faults, 0 stalls in 300-600 s
     both ways on 98 SE and ME (ethernet.md section 4.4 there).
- xhcinet lacks only (3): NetCbReceive (cm's service DPC, a KDPC run by
  NTKERN, from the ring interrupt's DPC or the poll timer's DPC) indicates
  straight away, and NetServe completes sends from that DPC (TxDone), from
  NetSendPackets and from the cm thread (link down) / its own worker.
  net8's 7c063945 raised IRQL around NdisMSendComplete only: the context was
  still the DPC's, so it changed nothing (R4 faulted).
- Round 8's inbound stall (connection stops, driver state whole, a new
  connection runs) is the other symptom the sibling recorded for the same
  context before its change.

### The change (uncommitted at first)
- xhcinet x86 NDIS 5.0 image, on a system without WDM 1.10 (98 SE, ME;
  IoIsWdmVersionAvailable(1, 0x10) FALSE): every receive indication, send
  completion and reset completion is queued (RxPend ring, Done list,
  ResetDone) under UpLock and made by an NDIS miniport timer's routine
  (two timers set in turn, UpArmed held until the queues are empty, as the
  sibling's f417ef2). Halt: timers cancelled, routine waited, queues
  drained. D3: routine waited before the OID completes. Reset: completes
  from the routine after the completions queued before it, gate held
  until then. NT and the x64 image: unchanged (direct calls).
- Imports: NdisMInitializeTimer, NdisSetTimer, NdisMCancelTimer (allowlist
  rows; new net98-runtime.list record cdcnet-tcp98, usb-wdm-free 427acad).
- qemu flavour only (NET_DIAG): sequence numbers of indications,
  completions and accepted sends; invariants: a send accepted twice before
  its completion (net.dg.dupsend), a completion of a send not outstanding
  (net.dg.cmpunk), a receive packet taken while out (net.dg.rxtake) or
  returned while free (net.dg.rxret), an indicated length out of 14..MaxFrame
  (net.dg.len); upcalls made inside a cm callback (net.dg.cbup), sends
  entered inside an indication (net.dg.sendind); logged every 4096 upcalls
  through the cm log (debugcon with XHCICMQ at the 98 SE end).

### Coordinator's extra item: r9's "Ring interrupt: off" on 98 SE with IRQON, no peer
- Reproduced in the emulator: p98nI = net7's p98n (IRQOFF image) + r9's IRQON.REG
  (START /W REGEDIT /S; a plain REGEDIT /S in a BAT raced the export and did not
  land the first time) + this tree's qemu xhcicm (fc811cc7), no peer, one guest
  (PIDs 16236 killed [bad BAT], 186524, 21976). Second start's debugcon:
  cm.irq.value=1, cm.irq.resource=00011101 (a line, shared, level), cm.irq.connected=3A
  (IRQ 10), cm.irq.armed=00001000, cm.irq.selftest=0. So ring 0's interrupt IS
  connected, armed and self-tested with IRQON on 98 SE.
- XHCITR's report: "State: no other computer connected" then "Ring interrupt: off
  (checked every clock tick)". That line is the network service's data-ring cause
  (tr_net.c, CM_QN_S_IRQ_STATE, record 35 section 5.5), opened only when a service is
  up (rings up after a login). With no peer it reads off by design, as net8's L3
  XP report did in state "connecting". EXPECTED, not a defect in how
  XhciCmRingInterrupt / XhciPfwNhiLine reach the driver. (Wording note for the
  owner/coordinator: the line could say "not open (no service up)" in that state.)
- To check ring 0's interrupt in a SAVE: the report's log section,
  cm.irq.connected / cm.irq.armed / cm.irq.selftest.

### Emulator legs (XP pxpm A server x 98 SE B client, usb4-xdomain, interrupt on at both ends)
Kit: run9.sh (cell, ping 20, TBSPEED 30 s 98 SE -> XP then XP -> 98 SE, -w 64,
B's VGA text watched every 5 s for a fatal exception, reports). B images = net7's
p98m (XhciPfwNhiLine 0, XhciCmRingInterrupt 1) + UPD98.BAT (XHCINET.SYS in
C:\WINDOWS\SYSTEM, where 98 SE loads it - not IOSUBSYS - and the qemu xhcicm
fc811cc7 at SYSTEM32\DRIVERS, so the cm log reaches debugcon).
- B1 (p98nB: the fix as first written, deferred packets; qemu xhcinet 0f00887c):
  no fault; 98 SE -> XP 40.0 Mbit/s 30 s, XP -> 98 SE 2.60. Diag: cbup 0 (no upcall
  inside a cm callback), bad 0, acc = cmp, ~7,400 routine runs per 58,000 indications.
- A1 (p98nA: the same image with the deferral forced off, i.e. 3.4.3.0's upcall
  context + the diag; qemu xhcinet b3eb8d96, local build only): FATAL EXCEPTION 0E
  at 0028:C19D9A33 during 98 SE -> XP (same MSTCP area as net8's C19D8D03 /
  C19D8E73 / C19D99C3). Diag at the last log before it: 41,819 indications,
  78,870 upcalls made inside a cm callback (cbup), bad 0 - no double completion,
  no unknown completion, no duplicate send, no receive packet reused while out, no
  bad length. So the driver's own state was intact; the context is the defect.
  Guests 203328, 62260 killed.

### Commits on w48-net9 (from 65642159)
- b05dcafa the deferral (packets queued) + diag + imports + record 35 rev 29.
- b011ec1f review: frames copied into a byte ring (512 KiB down to 128 KiB) so a
  burst or a 64 KiB split no longer empties the 64-packet pool before the routine;
  halt/D3 wait for the routine.
- 3c2e84cd review: per-frame power-generation tag (no pre-D3 frame after D0);
  D3 wait bound logged.
- 6d47211c review: halt completes nothing itself (waits for the routine, logged
  every 2 s); reset gate retired before NdisMResetComplete.
- Reviews: r1 needs-attention (halt direct completions; pool vs split) -> b011ec1f;
  r2 needs-attention (D3 timeout) -> 3c2e84cd; r3 needs-attention (halt fallback;
  reset gate) -> 6d47211c; r4 running.
- Host suite test_net_upq 1437 checks, 0 failures (MSVC 6.0 /Za).
- C1: image p98nC = p98m + b011ec1f-state qemu xhcinet (73f39331) + qemu xhcicm f396b033.
- Review r4 (base 65642159, head 6d47211c): APPROVE, no material findings.
- Harness: C1's first try lost keys (my 5 s QMP text watcher was a second QMP client
  while spd6 sent keys; B never started SPD98; renamed cells\C1-harness). Watcher
  removed; lib.sh waitword now reads a 9x guest's screen before sending its r/Enter and
  stops on a fatal exception (kept in fault.txt), so the fault screen is never dismissed.

### Round 8's inbound stall (analysis, not reproduced: the emulator's XP -> 98 SE path runs at 2.6 Mbit/s, round 8's stall came at 180-260 Mbit/s)
- Round 8's signature: a TCP connection stops dead for 30 s+, the driver's state whole
  (no ring wedge, no refusal, interrupt alive), small frames and a new connection pass;
  only with the interrupt (3 of 4 inbound tests), never polled (0 of 4). Round 8 ran
  3.4.2.0's xhcinet, whose upcalls are the same DPC-context calls as 3.4.3.0's.
- usb-wdm-free's 98 SE record (2026-11-07-98se-tcp.md) shows exactly that signature
  with direct upcalls from completion context ("In every stall the guest stayed up and
  the driver's trace showed its receive pool whole ... and every send completed; a new
  connection worked again"), and the same 0E faults at higher rates; both went with the
  upcalls moved to an NDIS timer. So the stall is most likely the same defect at the
  receive side [inference: the 98 TCP/IP is not read]. Round 9 with the fixed xhcinet
  is the test: if the S2/S3 stalls go, it is confirmed.
- C1 (b011ec1f-state, byte ring): no fault; 98 SE -> XP 18.6 Mbit/s 30 s (host loaded),
  XP -> 98 SE 2.60; diag cbup 0, bad 0. B's report lost to the COM1 write fault (the
  finished SPD98 window kept COM1): the 9x BATs now end with CLS so the window closes.
- Final builds of 6d47211c: gated x86 all (release + qemu) PASSED slot 3, record
  commit 6d47211c dirty=0. x86 release xhcinet.sys 20,156 B
  6a2b9ef9da45f02740de5f83a98cf5887382e2f9a48404cf3622832c58e7e7a6 (xhcicm.sys
  unchanged by this branch: 502,740 B 24d5c72e...). qemu xhcinet 4d281ca7..., xhcicm
  19939555.... Copies net9\binF\rel and \qemu.
- F1..F10: image p98nF = p98m + 6d47211c qemu xhcinet + qemu xhcicm (kit9nF).
- Gated amd64 release of 6d47211c: PASSED slot 1, record commit 6d47211c dirty=0;
  xhcinet.sys x64 94af672c231299a65242f08b70d99952f0ddaec807ff11d41ca17fc55f7f5cf8
  (NT path: direct upcalls as before; the common receive/send code was refactored into
  helpers, so the image is not byte-identical, its behaviour unchanged).
- F1..F4 (6d47211c qemu, interrupt on both ends): all clean.
  F1 98->XP 41.2 / XP->98 2.59; F2 28.3 / 2.66; F3 41.8 / 2.64; F4 29.6 / 2.59 Mbit/s.
  F1's 98 SE report: Ring interrupt on (27,939 wake-ups), 0 no-buffer drops, 0 errors;
  diag cbup 0, bad 0, acc = cmp.
- Stopped after F4 at the coordinator's "report early" (F5..F10 owed; runall.sh F
  kit9nF 5 10 resumes them). All my guests stopped (pids.txt).
- Rebased onto 3.4.3.0 (b377213d): w48-net9 = 50e46c4e, 2793bd18, 2c645ead, c2ed4c1c
  (= b05dcafa, b011ec1f, 3c2e84cd, 6d47211c); src/net, test, scripts unchanged by the
  rebase, so the 6d47211c builds' xhcinet.sys stand; a gated rebuild on c2ed4c1c for
  the record is owed if the merge wants one.

### Merge-review fix (coordinator, base b377213d)
- 54aa95c5: a D3 stays pending until the routine has made every queued completion,
  the queues stopped in the same locked step (NetUpqQuiesce), both timers cancelled;
  D0 resumes (NetUpqResume, NetUpWake); the halt shares the path. test_net_upq 1453/0
  (new testPower: completions queued, routine held off 3 s, no answer until all made).
- Review against b377213d: APPROVE, no material findings.
- Gated x86 all PASSED (slot 1, commit 54aa95c5 dirty=0); amd64 release PASSED.
  x86 release xhcinet.sys 20,092 B
  2934f038b5ca38d21d204233fc28196bff5d996f76437be0ca9ebcc62055b3e8 (binG\rel).
- G1 (54aa95c5 qemu xhcinet 4fd58a54, p98nG): clean; 98 SE -> XP 42.5 Mbit/s,
  XP -> 98 SE 2.61, 0 no-buffer drops. Guests 173752, 230368 killed.

### Merge (w48-net9)

Merged into `3.4.3.0` at `6027263d` (2026-11-10), its second merge review with nothing material.

### Bench round 9 (2026-11-10, net9's xhcinet `2934f038`, diagnostic xhcicm `c8a834ae`) (worker r9read)

The owner's round 9 (`temp\p48-tbnet\round9\`, the large files in
`E:\p48-tbnet-large\round9\`): the P14s on 98 SE, the Framework on
Windows 11, the kit `E:\xhci98-bench\p48-tbnet-r9` (STEPS.TXT,
BENCH-STEPS.txt). Images: XHCINET.SYS `2934f038...` (w48-net9 `54aa95c5`,
merged as `6027263d`: every receive indication and send completion on 98
SE from an NDIS timer's routine, record 35 section 7.8), XHCICM.SYS
`bcf74c38...` (w48-r9diag `c8a834ae`: round 8's image plus the CRC Error
and Buffer Overflow frames noted apart), XHCITR `3ffb7715`, TBSPEED
`fae73df4`. Each run: SPEED9 on the P14s, then SPEED9.CMD (or CAPTURE9.CMD)
on the Framework: `ping -n 20`; Test 1, the Framework sends 30 s (`-w 256`)
with pings of 32 and 1472 bytes once a second beside it (70 each, so they
run on through the probes and Test 1b); the stall probes and Test 1b (a
second inbound 30 s on a new connection); Test 2, the P14s sends 30 s;
then SAVE. Inputs read: SPEED9-W.TXT, SPEED9.TXT, NETST9.TXT, PINGF.TXT,
MSTCP0.REG, S1 to S6 (REPORT.TXT, NETST.TXT, MSTCP.REG, USB.REG,
XHCIPFW.REG, IPCFG.TXT), the two pktmon captures (`CAP9-...-184304`, run 3;
`CAP9-...-185307`, run 5; the .pcapng read with a header-only script,
`E:\xhci98-scratch\w48\r9read\tcpstat.py`, `outgap.py`, `ingap.py`).

The settings, each read back from that run's SAVE (MSTCP.REG, USB.REG,
XHCIPFW.REG, and the report's "Flow control" and "Ring interrupt"):

| Run | Interrupt (`XhciCmRingInterrupt` / `XhciPfwNhiLine`) | `DefaultRcvWindow` | `XhciCmNetE2E` | Framework | Restart |
|---|---|---|---|---|---|
| 1 | 1 / 0, on (240,486 wake-ups) | 16060 | 1 (end-to-end) | default | yes |
| 2 | 1 / 0, on (135,462) | 32120 | 1 | default | yes |
| 3 | 1 / 0, on (104,010) | 64240 | 1 | default, capture | yes |
| 4 | 1 / 0, on (157,245) | 64240 | 0 (none) | default | yes |
| 5 | 1 / 0, on (267,653, cumulative) | 64240 | 0 | `TcpAckFrequency` 1, cable re-plugged, capture | no (S4's boot) |
| 6 | 0 / 1, off (every clock tick) | 64240 | 1 | default | yes |

`MSTCP0.REG` (before WIN16K) has no `DefaultRcvWindow`, as in round 8.

#### The table (Mbit/s, TBSPEED's totals; both ends agree to 0.2 %)

"In" = the Framework sends (Test 1, Test 1b), "out" = the P14s sends
(Test 2). Per-second minimum and maximum in brackets. Pings in ms, as
Windows prints them ("<1" counted as 1): idle = the 20 before Test 1;
beside = the 1 s pings during Test 1 (pings 1 to 30) and Test 1b (50 to
70); probes = the pings between them (31 to 49) and the probe pings.

| Run | In, Test 1 | In, Test 1b | Out, Test 2 | Zero seconds | Ping idle avg (min-max) | Ping beside load avg (max) | Ping probes avg (max) |
|---|---|---|---|---|---|---|---|
| 1 | 328.6 (263-355) | 318.9 (255-348) | 58.2 (19-97) | none | 42 (7-99) | 1.0-1.2 (7) | 11-63 (97) |
| 2 | 302.4 (211-373) | 326.9 (144-492) | 56.5 (6-280) | none | 42 (8-89) | 1.0-2.1 (17) | 33-44 (98) |
| 3 | 290.8 (233-406) | 338.1 (228-460) | 44.3 (9-136) | none | 52 (9-97) | 1.2-1.4 (7) | 30-61 (115) |
| 4 | 434.7 (310-635) | 422.9 (283-585) | 348.0 (189-536) | none | 1 (1-1) | 1.0 (1) | 1.0 (1) |
| 5 | 308.9 (196-435) | 297.3 (162-468) | 300.8 (193-499) | none | 1 (1-2), 1 of 20 lost | 1.0-1.1 (3) | 1.0 (1) |
| 6 | 110.0 (94-142) | 101.0 (91-143) | 14.1 (9-25) | none | 33 (8-98) | 4.8-6.4 (12) | 19-52 (97) |

PINGF (the P14s pinging the Framework, after run 1): 20 of 20, 8 to 75 ms,
average 43.

Pings lost: run 4, one of 70 of each size beside the load (both in the
Test 1b part); run 5, the first of the 20 idle. Runs 1 to 3 and 6: none of
the 290 each. 98 SE's ICMP counters agree: S4 178 echo requests received
of the 180 sent, so the two lost pings were lost on the way in.

From each S report (the network section; S5 is cumulative over S4's boot,
so its own run is the difference):

| Run | Frames in | With errors | Dropped (xhcicm) | No buffer (xhcinet) | Frames out | Refused | 98 SE TCP retransmitted | Link changes |
|---|---|---|---|---|---|---|---|---|
| 1 | 1,676,023 | 0 | 0 | 0 | 1,000,618 | 0 | 0 | 1 up |
| 2 | 1,631,756 | 0 | 0 | 0 | 969,210 | 0 | 0 | 1 up |
| 3 | 1,627,752 | 0 | 0 | 0 | 934,295 | 0 | 0 | 1 up |
| 4 | 2,309,163 | 3,289 | 0 | 0 | 2,040,725 | 0 | 0 | 1 up |
| 5 (S5 - S4) | 1,649,208 | 3,762 | 0 | 0 | 1,585,638 | 0 | 0 | +1 down, +1 up (the re-plug) |
| 6 | 543,631 | 0 | 0 | 0 | 309,552 | 0 | 0 | 1 up |

Every report: "Errors: 0 sending", "Dropped sending: 1 (link down)" (one
send before the link came up, at the start), `cm.irq.selftest` passed in
runs 1 to 4, `cm.irq.armed`, no `cm.irq.storm`, `.lost`, `.down`, no
`cm.net.irq.latched`, no `cm.net.link.down` but run 5's re-plug (with
`cm.net.teardown.gone`, then the rings and the link up again). netstat:
98 SE's "Segments Retransmitted" is 0 in all six; "Received Packets
Discarded" 0; "Received Header Errors" 0.

#### SPEED9.CMD's "Test 1 did not end normally (a stall?)": a false trigger

It printed in all six runs, and the probes and Test 1b ran each time,
though every Test 1 ran its 30 s and both ends printed normal totals. The
log shows why: the line "--- Test 1 ended, exit ..." carries TBSPEED's
whole console output followed by `0`. In SPEED9.PS1, `Run-Tbs` calls
`& $tbs @a` without capturing its output, so in PowerShell that output
becomes part of the function's return value; `return $LASTEXITCODE`
appends the exit code. `$t1` is therefore an array (TBSPEED's lines, then
0), and `$t1 -ne 0` on an array returns the elements that are not 0 - the
text lines - a non-empty array, which is true. The exit code was 0 every
time. The same made TBSPEED print nothing on the Framework's screen during
the tests (its lines went into the variable; the copies in SPEED9-W.TXT
are from its own `-o`). Fix for a later kit: `& $tbs @a | Out-Host`, then
`return $LASTEXITCODE`. The extra Test 1b and probes were harmless and
gave a second inbound test and an idle-ish ping series in every run.

#### Is round 8's inbound stall gone? Yes, on this evidence

- **No stall in 10 inbound tests with the interrupt on** (runs 1 to 5,
  Test 1 and 1b, 300 s in all), nor in run 6's two polled ones. No second
  with less than 140 Mbit/s inbound with the interrupt on; 98 SE's side
  shows the same (its lowest inbound second 141 Mbit/s). Round 8: 3 of 4
  inbound tests with the interrupt on stopped dead (S2 -w64 after 22 s,
  S3 both after 2 s with the 64K window), and run 3 here is round 8's S3
  exactly (interrupt on, 64240, end-to-end on, `-w 256`).
- **Run 3's capture is clean.**
  - Windows handed 1,627,666 frames to its adapter (pktmon, upper edge);
    98 SE's ring took 1,627,752 in that boot (the difference is traffic
    before the capture). Nothing was lost and nothing was resent: 98 SE
    sent 0 duplicate ACKs and no SACK block in either inbound connection.
  - The largest gap in Windows' data was 36 ms (Test 1) and 32 ms
    (Test 1b); no gap of 0.2 s anywhere.
  - 98 SE advertised a zero window 13 times (Test 1) and 29 times
    (Test 1b) and opened it again each time within the same burst:
    TBSPEED on 98 SE reading late, not the path.
  - 98 SE ACKed every second segment (372,903 of 378,959 ACKs advanced 2
    segments), and its window was 64240 (most ACKs) down to 51,100.
- **No errored frame with end-to-end flow control on**: 0 in 5.48 million
  frames (runs 1, 2, 3, 6). Round 8's S3 had 78 with 3.4.2.0's xhcinet.
- **The pings beside the load passed at 1 ms** and the probe pings (small
  and full-size) passed in every run, so neither the path nor full-size
  frames ever stopped.
- **What changed from round 8:** the network driver alone (net9's NDIS
  timer upcalls; also the miniport's stats callback); xhcicm differs only
  by the diagnostic notes, which change nothing in the data path. So the
  stall followed the indications and completions made from the receive
  DPC, round 8's candidates 1 and 3 and r9's fourth (MSTCP's state
  corrupted by a completion preempted by the receive indication) [inf].
  The exact mechanism inside MSTCP is not read (no Microsoft binary is).

#### Fatal exception

None. The owner's return holds no note of one and no Safe mode start;
all six SAVEs were written in order (18:22, 18:29, 18:46, 18:51, 18:56,
19:01), S5's log continues S4's boot through the re-plug, and runs 1 to 5
include 98 SE sending at 44 to 348 Mbit/s with the interrupt on: the
condition that gave the 0E in 3 of 6 emulator runs before net9.

#### Outbound (98 SE sends): end-to-end flow control is the limit

- **With end-to-end flow control off, 98 SE sends at 301 to 348 Mbit/s**
  (runs 4 and 5) against 44 to 58 with it on (runs 1 to 3), the same
  boot type, window and interrupt. Nothing else changed between runs 3
  and 4.
- **It is not 98 SE's TCP window or Windows' ACK spacing.** In both
  captures, every pause of 98 SE's data of 8 ms or more (885 in run 3,
  1,013 in run 5) began with everything acknowledged by Windows (882 and
  1,004 of them with 0 bytes outstanding as Windows saw it), Windows'
  last ACK leaving within about 0.1 ms of the last segment.
  - Run 3 (on): the pauses last 5 to 150 ms, spread (median about 15 ms),
    21.6 s of the 30 s spent in pauses of 8 ms or more.
  - Run 5 (off): 10 to 12 ms almost every time, 12.3 s in pauses.
- **The extra wait with end-to-end flow control on is the idle one seen
  in the pings.** With it on, a ping on a quiet link took 0 to 100 ms
  (idle averages 33 to 52 ms; PINGF from 98 SE 8 to 75 ms) in every run,
  polled or with the interrupt; with it off, 1 ms (runs 4 and 5). Under
  inbound load, with frames flowing both ways all the time, the pings
  took 1 ms with it on too. Outbound, the way into 98 SE carries only
  Windows' sparse ACKs, so each ACK round meets that wait [inf]. Which
  end's credit grant is late is not readable from one end's capture.
  Linux on the same P14s with end-to-end flow control on its rings (round
  7, the owner's Linux run) pinged at 0.9 to 2.8 ms, so the wait is
  likely this driver's way of running end-to-end flow control on this
  link rather than the peer's or the chip's [inf].
- **The 10 to 12 ms pauses that remain with it off** are on 98 SE's
  side (Windows had ACKed everything); 98 SE resumes at about its clock
  tick. Whether it is xhcinet's timer routine under load or TBSPEED's
  thread waking is not read; an idle ping at 1 ms shows the timer path
  itself adds no tick when the system is idle.
- **Windows' ACK spacing: 9 segments an ACK, with or without
  `TcpAckFrequency` 1** (run 3: 12,499 ACKs for 114,387 segments; run 5:
  86,744 for 774,453). The capture shows why the value cannot act here:
  Windows coalesces received segments above the adapter (pktmon's upper
  edge shows up to 28 wire frames merged into one 40,880-byte receive)
  and acknowledges each coalesced block [inf]. Round 8's stretch-ACK
  reading of the outbound limit does not hold: with end-to-end flow
  control off 98 SE reaches 300+ Mbit/s against the same ACK spacing.
- **98 SE never retransmitted** (netstat, all runs), the transmit ring was
  never refused, and the frames out equal 98 SE's IP output requests plus
  ACKs: the send path loses nothing either way.

#### Effects of each setting

- **Receive window (runs 1 to 3), with the interrupt:** no effect. 16060,
  32120 and 64240 all gave 290 to 338 Mbit/s in. At a sub-millisecond
  round trip even 16 KB covers the rate. The window matters only polled
  (round 8: 10 to 100 Mbit/s; run 6 here: 110 with 64240).
- **End-to-end flow control off (run 4 against run 3):**
  - out 44 to 348 Mbit/s; in 291/338 to 435/423;
  - idle ping 52 to 1 ms;
  - **but frames into 98 SE are lost**: 3,289 in run 4 and 3,762 in run 5
    arrived with the CRC Error flag (the diagnostic notes are all
    `cm.net.rx.crc`, no `cm.net.rx.bo`: never Buffer Overflow), from the
    first second of Test 1 onwards at about one in 450 to 700 frames
    (`.at` 1272, 1274, 1276, then 8557 ... 1,333,641 in run 4; the first
    one 502 bytes long, flags 1). In run 5, Windows handed 1,652,179
    frames to its adapter and 98 SE's ring took 1,649,208 including the
    3,762 flagged, so about 2,970 more never arrived as frames: about
    0.4 % of frames into 98 SE lost in all, none the other way.
  - The TCP cost: in run 5, 98 SE sent 18,842 and 18,596 duplicate ACKs
    and 20,228 and 19,937 ACKs with SACK blocks in the two inbound tests,
    advertised a zero window 971 and 1,144 times, and Windows waited a
    retransmission timeout (0.31 s) three times. Pings: 3 lost in 270.
  - A short, flagged frame and frames missing whole are what a receive
    ring overrun looks like without credits: the host interface has no
    descriptor for part of a frame [inf]. The receive DPC and the ring
    are not the bottleneck at rest (0 no buffer, 0 dropped).
- **`TcpAckFrequency` 1 (run 5 against run 4):** no change in Windows'
  ACK spacing (above); out 348 to 301, in 435/423 to 309/297, within the
  spread of a run with a capture and a re-plug. Not a lever.
- **Polled against the interrupt (run 6 against run 3):** in 110/101
  against 291/338; out 14 against 44; pings beside load 5 to 6 ms against
  1 ms; idle the same (end-to-end flow control on in both). Run 6 equals
  round 8's S4 (101/97 in, 14 out) with net9's xhcinet: the timer upcalls
  cost nothing measurable when polled.

#### Found in passing: the other computer's MAC is shown with its bytes swapped

Every report of rounds 8 and 9 shows "Other computer's MAC:
DC-89-59-B0-40-A6"; the Framework's adapter is B0-59-89-DC-A6-40
(round 8's `adapters-all.txt` and `ipconfig.txt`; the frames in both
captures carry `b0:59:89:dc:a6:40`). Each 32-bit word is reversed:
`tbt_xdp.c`'s Login Response parse takes DW 18's most significant byte as
the first MAC byte (record 35 section 2.1: "DW 19 the most significant
four bytes"), and Windows packs the bytes the other way. The log's
`cm.net.peer.mac=59B040A6` is the same stored value. `PeerMac` reaches
only the report and the log (xhcinet does not use it; 98 SE found the
Framework's MAC through ARP, and the data frames are addressed right), so
it is a display defect. This host's own Login Response packs its MAC the
same way (`tbt_xdp.c` line 290), so Windows may read it swapped too; no
effect was seen. Not changed here.

#### Verdict

- **48.38's stall into 98 SE with the interrupt on: gone** with net9's
  xhcinet (10 of 10 inbound tests clean, run 3 = round 8's S3 lossless in
  the capture). **The 98 SE fatal exception: not seen** on hardware with
  the fix, sending at up to 348 Mbit/s.
- **The interrupt is worth it on this pair:** in 3 times polled, out 3
  times (25 times with end-to-end flow control off), ping under load 1 ms.
- **End-to-end flow control:** keeps the receive lossless and costs an
  idle wait of up to 100 ms that holds 98 SE's sending to about 50 Mbit/s;
  off, 98 SE sends at 300+ but loses about 0.4 % of the frames it
  receives. The wait is likely this driver's [inf] and is an open item.

#### Defaults and tips (evidence for the owner, who decides)

- **`XhciCmRingInterrupt` (0) and `XhciPfwNhiLine` (1): leave both for
  3.4.3.0.** The evidence now favours the interrupt on 98 SE (no stall,
  no fault, 3 times the rate), but it is one board on one system, the
  value is machine-wide (ring 0's hot-plug and every NT system too) and
  needs `XhciPfwNhiLine` 0 on 98 SE and ME, which moves the controller's
  line for every 9x install with a Thunderbolt controller. Neither has
  been read beyond this P14s; the cut is close. Offer it as a tip instead
  (below), and decide the default after 3.4.3.0 with ME and a second
  board.
- **`XhciCmNetInterrupt` stays 1** (already the default; it acts only
  where `XhciCmRingInterrupt` is 1).
- **`XhciCmNetE2E` stays 1.** Off loses frames and stalls TCP for 0.3 s
  at a time; on is lossless. Do not advertise 0 for speed until the idle
  wait is understood.
- **`XhciCmNetRxFrames` stays 128.** Section 5.5's trigger (Buffer
  Overflow or `RX_NO_BUFFER` moving) did not fire: the errored frames are
  all CRC Error, and only with end-to-end flow control off.
- **`tuning.md`:** the receive window tip stays true for the shipped
  (polled) setting. A proposed addition, for the owner to reword (draft):

  > **Thunderbolt networking speed on 98 SE and ME: the controller's
  > interrupt.** Setting `XhciCmRingInterrupt` to `1` in
  > `HKLM\System\CurrentControlSet\Services\USB` and `XhciPfwNhiLine` to
  > `0` in `HKLM\System\CurrentControlSet\Services\VxD\XHCIPFW` (both
  > DWORD), then restarting, lets the controller's interrupt report
  > network traffic at once. In the owner's test (a 98 SE computer and a
  > Windows 11 computer), data coming in went from about 100 to about
  > 300 Mbit/s and data going out from about 14 to about 50 Mbit/s. To
  > undo it, set them back to `0` and `1` and restart. With it, the
  > receive window above makes little difference.

  If the owner prefers no new tip in 3.4.3.0, the window tip's numbers
  need no change.

#### Open (after 3.4.3.0 unless the owner says otherwise)

1. The end-to-end flow control idle wait (0 to 100 ms a frame on a quiet
   link; Linux on the same P14s 0.9 to 2.8 ms): which end's credits are
   late. A round 10 could ping with the interrupt on, end-to-end on, from
   both ends at once, with a capture on the Framework, and try
   `XhciCmNetRxFrames` 256 and 512 with end-to-end flow control on and
   off (more descriptors: more credits granted at once, and fewer overrun
   frames with it off) [inf].
2. The other computer's MAC byte order in the report (above): a small
   fix in the Login Response parse (each DW's bytes in Windows' order)
   with a test vector, once a second peer's order is confirmed.
3. SPEED9.PS1's false stall trigger (bench script only).
4. The latch at a 98 SE end (48.38's other open leg), unchanged.

No commit. The images under test are `2934f038` (net9's xhcinet, merged as
`6027263d`) and `c8a834ae` (bench only, never merged).

### Merge (r9read)

Reading only. The MAC found in passing fixed by w48-macfix (below); the end-to-end idle wait left to "Planned - Left by `3.4.3.0`" (E2EIDLE.1); `XhciCmRingInterrupt` stays 0 by default, with a `tuning.md` tip (owner, 2026-11-10).

## 48.4x a switch enclosure first plugged on 8.1: problem 31 is the emulator's bus numbering, not the kept child (2026-11-10)

Worker kept2, branch `w48-kept2` from `09ba3c3d` (3.4.3.0), worktree
`E:\xhci98-wt\w48-kept2`. Evidence `E:\xhci98-vm\p48\kept2\` (one folder
a guest, `pids.txt`, `r*.txt` the guest's readings, `debugcon-*.log`
the qemu-flavour CM's log).

Task: worker learn's finding (an enclosure never seen by the disk,
hot-plugged behind a tunnel on 8.1 x86, ends at problem 31; inferred:
8.1's kept child, record 31 section 4.10, sits above a PDO whose first
stack was torn down for a driver install, and the inbox PCI driver's
AddDevice is refused). Reproduce, trace, confirm or correct, fix.

### Machine and kit

8.1 x86 q35 base (`w81x86`), fresh overlays, the shared launcher
(`-Usb4 asm4242 -Tablet`, no enclosure at power-on; tunnel ports
`power_controller_present=off`, the ASM4242 xHCI's port `hotplug=off`;
emulator `asm4242-20261025-2230`), 2 vCPUs, TCG, QMP 47760-47769.
Kit: ejw's (`d0e3eea0` tree, 48.29's kept child; label 3.4.2.0),
installed with `INSTALL.BAT /console /yes /nopause /thunderbolt
/stor98` (exit 3), then `xhcicm.sys` replaced by qemu builds of
`09ba3c3d` with throwaway diagnostics (never committed; the patch is
`E:\xhci98-scratch\w48\kept2\diag.patch`): `cm.dx.pnp` (each PnP
request a chain child gets: minor, depth, kept state, something above),
`cm.dx.add` (each AddDevice of xhcicm and the stack's top), and four
switches below. The installed overlay was kept before its first
restart (`ck-installed.qcow2`) and each variant started from a copy.
The enclosure: `usb4-device ... device-model=asm2464,drive=nvme0` on
USB4 port 1, plugged by QMP `device_add` after the desktop.

### Readings

| Guest | Variant | Result |
|---|---|---|
| a (pids 147696, 197700, 200980) | the kit's CM + diagnostics | **problem 31** on the enclosure's upstream port (`1B21:2463`), Kernel-PnP 400 (`machine.inf`, `PCI\CC_0604&DT_5`) then 411 `Problem: 0x1F Status: 0x0` 31 ms later (`r1.txt`). The depth-3 chain child on the port: QUERY_ID, CAPABILITIES, DEVICE_TEXT, QUERY_ID x4, INTERFACE, BUS_INFORMATION, RESOURCE_REQUIREMENTS, RESOURCES, DEVICE_ENUMERATED, INTERFACE x3, then **REMOVE before any AddDevice**, nothing ever above it; kept (`cm.kept.logical=1003FFFF`); one CAPABILITIES after; no AddDevice of xhcicm on that devnode (no filters on it) |
| a, after `shutdown /r` | - | upstream port problem 0; the drive configured Stor98 by the id (`Matching Device Id: PCI\XHCI98_TUNNEL&CC_010802`), E: (`r2.txt`): **a restart of Windows clears it** |
| b (168092) | the never-started child destroyed at that removal (detached, `cm.kept.final`) | **problem 31** |
| c (42872) | destroyed and a fresh child attached in its place | the fresh attach **refused** (`cm.dx.renew=0`, NULL from IoAttachDeviceToDeviceStack right after the removal); **problem 31**. A never-seen drive directly on port 2 (`usb4-device` without a switch) on the same guest: no removal before AddDevice, started; Windows configured `stornvme`, the mark service switched it to Stor98 within a minute (`mark.txt`) |
| d (182824) | no chain child below depth 2 | the bridge filter attached its own depth-1 child instead; **problem 31** |
| e (166808) | the child passes every request through until its first start | **problem 31** |
| e (191844) | spare `pcie-root-port` hp1; QEMU's generic switch upstream port (`x3130-upstream`, `104C:8232`) hot-plugged there | problem 0 |
| f (157688) | **no chain child at all** (cmChildCreate attaches nothing) | **problem 31** |
| g (200204) | fresh overlay, the **`3.4.2.0` release** (no kept child) | **problem 31**; QEMU `info pci`: the upstream port at secondary/subordinate 0 |
| h (195916) | the kit, tunnel ports with QEMU's power controller (the old launcher) | **problem 31** |
| a (200980) | the enclosure a had installed and run, plugged on a start it was absent from | **problem 31**, bus numbers 0 |
| i (45472) | the kit + diag CM, emulator `asm4242-20261027-fwpad` with `usb4-asm-usp ...,fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=16M` (tp0 buses 3-50, 16 MB) | **problem 0** for the upstream and downstream ports; the drive `stor98`, compatible ids led by `PCI\XHCI98_TUNNEL&CC_010802`, E:; the drive's kept child re-armed with the id at each of the two restarts (`cm.kept.logical`, `cm.chain.id`, `cm.kept.rearm`) |
| j (203852) | 8.1 x64, fresh overlay, the `3.4.2.0` release x64 (exit 3), the padded emulator as i | **problem 0** for the upstream and downstream ports; the drive `stor98` (3.4.2.0's class model, no id on 8.1 by that release's design) |

`info pci` on h (unpadded): `asmusp` buses 2-6, `tp0` 3-3, `tp1` 4-4,
the plugged switch's upstream port (`tp0-sw1`) secondary 0 subordinate
0: no bus left below the tunnel port for the switch's own buses.

### Cause

Not the kept child, and not this driver: the shared launcher's emulator
build (`asm4242-20261025-2230`) predates the firmware's per-port bus
padding (`usb4-asm-usp` `fw-bus-pad`, 2026-10-27, run-44 "44.45";
usb4-emu's default since 2026-10-31): SeaBIOS gives each ASM4242 tunnel
port one bus, so a switch plugged after the start has no bus numbers,
and 8.1's PCI driver fails its AddDevice (problem 31, after removing
the not-yet-configured stack). "Never seen" was incidental: an enclosure
present at boot keeps the buses it was numbered with across re-plugs
(why ejw's and ejr's re-plugs passed). Learn's step 3 (the attach
refused above the kept child) was inferred and is wrong; the attach
refused in c is above the PDO itself, after its removal.

### Fix

No driver change. Record 31 section 4.10 gains the reading and record
22 section 7.6.3 no longer names the kept child (`8c1f439f`). The
launcher (shared, not tracked) should pin a padded build and pass the
pads; proposed, not changed here. Reviewed: approved, no material findings.

Not read: 8.1 x64 with this tree's kept child on the padded emulator (no x64 build of this tree here; no driver change, so 48.29's x64 legs stand).

### User-visible strings

None.

### Merge (w48-kept2)

Merged into `3.4.3.0` at `cd13e5d8` (2026-11-10).

## Issue 32: Windows 2000 USB audio on `3.4.2.0` (worker iss32, 2026-11-10)

GitHub issue 32 (sngy844, 2026-11-09): on Windows 2000 SP4 under QEMU with
`3.4.2.0` installed by SETUP.EXE, a USB Audio Class 1.0 device installs
(`usbaudio.sys`, the WDM audio drivers) but plays nothing; DxDiag "Problem
getting extra sound info, result code = 0x88780032", DirectSound fails at
step 3 (`DirectSoundCreate`, `0x88780032`); 98 and XP play on the same
setup. **Reproduced, ours, a `3.4.2.0` regression; fixed on `w48-iss32`.**

**Setup** (development host A, the pinned emulator
`asm4242-20261025-2230`, the q35 Windows 2000 base through
`launch-q35.ps1 -Os w2k`, one processor, 512 MB, `-audiodev none`): a
control guest `c1` with no `xhci98.sys` (`-NoXhci`, `ich9-usb-uhci1` at
00:1a.0 with `usb-tablet` and `usb-audio` on it), and `x1` with QEMU's
`qemu-xhci` (`p2=4,p3=4`, behind a root port), `usb-tablet` and
`usb-audio` on it. Plays: `sndrec32 /play` of `tada.wav` (1.93 s, Windows
2000's own, and a 1.61 s one), read from the Sound Recorder's position;
DxDiag (5.00.2134.0001, DirectX 7.0) Sound tab, Test DirectSound.
VM folder `E:\xhci98-vm\p48\iss32\` (`c1\`, `x1\`, `pids.txt` in each),
scratch `E:\xhci98-scratch\w48\iss32\`.

| Guest, stack | Sound Recorder | DxDiag DirectSound |
|---|---|---|
| `c1`, Windows' own UHCI stack (USB Audio Device as `USB\Vid_46f4&Pid_0002&Rev_0000&Mi_00`, `usbaudio.sys` 5.00.2150.0001) | played to the end, 1.93 of 1.93 s (`c1\s5.png`) | played its first format, "Mono, 8-bit 22Khz, from a Software buffer. Did you hear the sound play?" (`c1\s10.png`) |
| `x1`, `3.4.2.0` (`releases\3.4.2.0`, `INSTALL.BAT /console /yes /nopause /nostor98`; the controller, the composite device and the root hub "xhci98 3.4.2.0, started", no restart asked) | "Your audio hardware cannot play files like the current file." (`x1\s6.png`) | "Failure at step 10 (Secondary buffer): HRESULT = 0x88780032 (unknown error)" (`x1\s9.png`) |
| `x1` after a shut down and cold start, same build | refused, as above (`x1\t2.png`) | - |
| `x1`, `3.3.0.0`'s debug-x86 `xhci98.sys` copied over the install (`SW.BAT`, `fc /b` equal), cold start | played to the end (`x1\play-d33.png`) | - |
| `x1`, `3.4.1.0`'s `xhci98.sys`, cold start | played to the end (`x1\play-r3410.png`) | - |
| `x1`, `3.4.3.0` at `c4c26525`, `qemu` flavour (`C626F842...B009`) | refused (`x1\play-qtip2.png`) | - |
| `x1`, `w48-iss32` `72937554`, `qemu` flavour (`949950FF...1A0D`) | played to the end (`x1\play-qfix.png`) | "DirectSound test results: All tests were successful." (`x1\ds-qfix2.png`) |

The `qemu` trace at `c4c26525` (`x1\debugcon-qtip2.log`): at each play,
`hcd: URB refused, function/length=00010000` and `URBs refused -
malformed` climbing 9, 0x11, 0x17, 0x1D - `usbaudio.sys`'s streaming
`SELECT_INTERFACE` with `UrbHeader.Length` 0, refused by `3.4.2.0`'s
header-length rule (`hcdUrbMinLength`, `src/hcd_urb.c`), the field usbport
rewrites and never reads for a select (run-26 c15's refusal, in a new
place). The fix (`72937554`): `XhciPipeUrbLengthOk`, the select exempt,
every other function as before; host vectors `test_pipe` `test_urb_length`.
With it (`x1\debugcon-qfix.log`): `select interface, URB header short,
header/length=00000010` (alternate 0, three times) and `=00000024`
(alternate 1, twice), `interface selected, number/alt=00000101`, `endpoint
mask=00000004`, an `isoch URB` line, `URBs refused - malformed=00000000`.

The 47 and 48.33 2000 legs' "cannot play files like the current file" with
QEMU's `usb-audio` (`run-47.md` section 9, the 48.33 2000 leg) was this,
not the emulated device.

Write-up: `docs/issues/30-w2k-usbaudio-select-interface-header.md`.

**Builds** (`w48-iss32` at `dcaa3bca`, slot gated.4, `XHCI98_SELFTEST_SKIP_OK=1`):
`build-driver.cmd all` x86 and `all -amd64` both "BUILD + GATES PASSED
(release qemu)", dirty 0; release `xhci98.sys` x86 `3EC78844...83EC`,
amd64 `1E832A25...A0F1`; host tests 14489 checks, 0 failures (test_pipe
with `test_urb_length`). Logs `E:\xhci98-scratch\w48\iss32\gated-x86.log`,
`gated-amd64.log`; records `out\gated-*-*.txt` in the worktree. The VM legs
ran the `qemu` flavour of `72937554` (same code as `dcaa3bca`, which adds
docs only); the released flavour on 2000 is owed to the merge's matrix.

### Merges (w48-iss32, w48-iss32b)

Merged into `3.4.3.0` at `b76b4adb` (2026-11-10), the follow-up w48-iss32b at `89715976`.

## 48.36 (continued) The 10 x86 STOP on the three-controller dock's plug (2026-11-10, w48-dock6)

Worker w48-dock6, development host A, branch `w48-dock6` from `c4c26525`
(the coordinator branch, now 3.4.3.0), worktree `E:\xhci98-wt\w48-dock6`. QMP 47922 and 47923;
QEMU PIDs in `E:\xhci98-vm\p48\dock6\w10*\pids.txt`.

Build: `build-driver.cmd all` at `c4c26525` (slot gated.4,
`XHCI98_SELFTEST_SKIP_OK=1`): "BUILD + GATES PASSED (release qemu)"
(`gated-x86.log`); `make-package.ps1` release and qemu x86; kit
`E:\xhci98-vm\p48\dock6\xfer` = the **qemu** package (family layout):
`xhci98.sys` `EACD375F...`, `xhcicm.sys` `AE87235E...`.

The installer takes only the release flavour ("A driver from another build
is in the folder"), so the kit is the release package with the qemu
`xhci98.sys` and `xhcicm.sys` in `xfer\Q`, put in place after the install
by renaming the loaded images and copying (`G.CMD qput`); a restart loads
them (debugcon lines `xhci98: ...` and `cm.*` follow).

### The STOP read (10 22H2 x86, fresh overlay `w10`)

Fresh overlay of the q35 base `w10x86`, `launch-q35.ps1 -Usb4 tb3ar`
(w48-dock2's emulator), PID 195048 (tag a) then 173380 (tag b).
`CrashDumpEnabled` 3, `AutoReboot` 0 (`G.CMD dmpset`); the page file
exists (1152 MB). Install `INSTALL /console /yes /nopause /thunderbolt
/usb` (publisher prompts answered), exit 3; the qemu drivers put in
place; restart. The TS3 Plus model hot-plugged
(`usb4-device,id=dock0,bus=u4.0,port=1,dialect=tb3,dock-model=ts3plus`).

- **Plug 1 (12:41:45):** STOP PAGE_FAULT_IN_NONPAGED_AREA at the 8th
  15-s screenshot (about 100 s), "100% complete" (`w10\p1-w8.png`); no
  "What failed" line. The dock unplugged over QMP, `system_reset`; the
  minidump `111026-33562-01.dmp` fetched over COM1 (`certutil -encode`,
  `G.CMD dmpget`) and read with `md32.py` (header, triage block, module
  list and the stack words that fall in a module; nothing disassembled).

  `BugCheck 10000050 (9DB3C0DC, 00000000, 81D9014C, 00000000)`: a read
  of `9DB3C0DC`, not present; the faulting EIP `81D9014C` is
  `ntoskrnl.exe+17F14C` (a register read routine's address, by its
  place in the stack: called from Windows' xHCI driver); ESI `9DB34000`,
  EDX `9DB3C0DC` = ESI + `80DCh`. The stack's words in a module, from the
  top: `USBXHCI.SYS+3942`, `+146B3`, `+27DD`, `+24DD`, `+5886`, then
  `Wdf01000.sys` frames and `ntoskrnl.exe`. `USBXHCI.SYS` and
  `ucx01000.sys` are loaded; `xhci98.sys` and `xhcicm.sys` are loaded
  but on no stack word. The debugcon log after the plug carries only
  `cm.*` lines (the tunnel up 0.1 s after the plug, the chain filters on
  the dock's bridges within 2 s) and **no `xhci98:` line**: no dock
  controller came to `xhci98.sys`. As designed on 8 to 10 (release
  notes, "USB 3 controllers": the INF matches every xHCI by class so
  that Windows ranks its own driver first; `/usb` moves the controllers
  present at the install), a controller that appears later, here each
  of the dock's, gets Windows' `usbxhci.sys`.
- **Plug 2 (13:03:12), to place the address:** the same STOP at the 9th
  screenshot (`p2-w9.png`). At the STOP screen `info pci` (`p2-infopci.txt`)
  and `info tlb` (`p2-tlb.txt`) were kept: FL1100 #1 (bus 6) BAR0
  `fe1fc000-fe1fffff` mapped at VA `9fbd1000`, `9fbd2000`; FL1100 #2
  (bus 7, `1ab6:1105`) BAR0 `fe0fc000-fe0fffff` mapped at `a2d0c000`;
  the ASM1142 (bus 8) BAR0 "not mapped". The dump `111026-39281-01.dmp`:
  `BugCheck 10000050 (A2D140DC, 00000000, 8159E14C, 00000000)`, EIP
  `ntoskrnl.exe+17F14C`, ESI `A2D0C000`, P1 = ESI + `80DCh`, the same
  `USBXHCI.SYS` stack offsets. ESI is the VA of FL1100 #2's BAR0, so
  **Windows' `usbxhci.sys` read offset `80DCh` of an FL1100's BAR0, which
  the model sizes at 16 KB (`4000h`)**; the real FL1100's BAR0 is 64 KB
  (usb4-emu `docs/DOCKS.md` section 2: "three 64-bit memory BARs (64 KB,
  4 KB, 4 KB)", and section 6 lists the one 16 KB BAR as a model gap).
  The offset is a fixed one past the xHCI register set the model has,
  read about 60 to 100 s after the controller started.
- A third dump, `111026-37781-01.dmp` (12:03, tag a, before anything of
  this project was installed: the fresh base with the tb3ar host and
  nothing attached): `BugCheck 0000001A (00041790, 8D93989C, 00000015,
  00000016)`, MEMORY_MANAGEMENT, only `ntoskrnl.exe` on the stack; the
  guest restarted by itself during the first UAC prompt. Not ours (no
  image of ours loaded) and not this STOP; noted as the base's.

**Verdict: not ours.** The fault is in Windows' own xHCI driver on the
dock's FL1100 model, reading 80DCh into a BAR the model makes 16 KB where
the real part has 64 KB: the emulator's gap, met only where Windows' own
`usbxhci.sys` drives the dock's controllers (8 to 10; on 2000 and 7 they
are `xhci98.sys`'s, which reads nothing past the xHCI registers, and 98 SE,
ME and XP have no inbox xHCI driver). No fix in this tree. The `/cmonly`
repeat was not needed: the controllers were already Windows' own in this
run (the dump's modules and the debugcon log). Proposed for usb4-emu: the
dock xHCI's BAR0 at the real size (FL1100 64 KB, ASM1142 and Titan Ridge
as captured or published).

- **HP G2 plug (13:18:27), Windows' driver:** the same STOP at the 9th
  screenshot (`h1-w9.png`). Dump `111026-35031-01.dmp`: `BugCheck
  10000050 (9CD9B0A4, 00000000, 8158F14C, 00000000)`, EIP
  `ntoskrnl.exe+17F14C`, stack `USBXHCI.SYS+3942`, `+14759`, `+5886`;
  `info tlb` at the STOP: the dock's Titan Ridge xHCI (`8086:15f0`, BAR0
  `fe1fc000-fe1fffff`, 16 KB) mapped at `9cd93000`, so P1 is BAR0 +
  `80A4h`. The second model meets the same gap at another offset past
  the 16 KB.

### Both docks on 10 with `xhci98.sys` on their controllers

To read the docks on 10 with this project's driver on the dock's
controllers (the arrangement `/usb` gives the controllers present at the
install), the guest's `USBXHCI` service was set to start 4 (disabled;
the guest's setting, for the test only) and `INSTALL /thunderbolt /usb`
run again with the dock attached, which moves the dock's controllers
("Installing USB xHCI Compliant Host Controller ... done"); the qemu
drivers put back (`qput`; the same-version update had replaced them),
restart.

| system, VM | dock | step | result |
|---|---|---|---|
| 10 x86 `w10` | HP G2 | attached 150 s with `USBXHCI` disabled | no STOP (`h2-end.png`) | - |
| 10 x86 | HP G2 | the installer's `/usb` re-run, restart with the dock attached | "HP Thunderbolt Dock G2 (Thunderbolt 3), approved"; "xHCI98 USB 3 Controller (Intel Titan Ridge)", its root hub, both "xHCI98 USB Hub", "RNDIS/QEMU USB Network Device" (`rep-h1.txt`) | pass |
| 10 x86 | HP G2 | stick A on the Titan Ridge xHCI's port 2 | offline at first (the vvfat signature, as dock5 found), `diskpart` `online disk`; E: "FC: no differences encountered" twice | pass |
| 10 x86 | HP G2 | `XHCITR /eject id=` the dock's upstream port | the dialog lists every function; "Windows has stopped QEMU QEMU HARDDISK USB Device (E:). The eject was confirmed to the Thunderbolt driver. It is now safe to disconnect the device." (`h-ej1.png`, serial) | pass |
| 10 x86 | HP G2 | unplugged, re-plugged | approved, controller, root hub, both hubs, RNDIS (`rep-h2.txt`) | pass |
| 10 x86 | HP G2 | removed by surprise (stick B mounted) | the system answers; "0 USB devices, 0 Thunderbolt/USB4 devices" (`rep-h3.txt`) | pass |
| 10 x86 | TS3 Plus | attached 120 s with `USBXHCI` disabled | no STOP (`t1-end.png`) | - |
| 10 x86 | TS3 Plus | the installer's `/usb` re-run ("Installing USB xHCI Compliant Host Controller ... done" three times; named FL1100, FL1100, ASM1142), restart with the dock attached | "CalDigit, Inc. TS3 Plus (Thunderbolt 3), approved"; both "xHCI98 USB 3 Controller (Fresco Logic FL1100)", "(ASMedia ASM1142)", the 82574L on Windows' own driver; every problem code 0 (`rep-t1.txt`) | pass |
| 10 x86 | TS3 Plus | a stick on each controller (A FL1100 #1, B FL1100 #2, C ASM1142, port 3; `online disk` each) | E:, F:, G:: "FC: no differences encountered" twice each | pass |
| 10 x86 | TS3 Plus | `XHCITR /eject id=` the dock's upstream port | every function listed (`t-ej1.png`); "Windows has stopped QEMU QEMU HARDDISK USB Device, ... (E:, F:, G:). The eject was confirmed to the Thunderbolt driver. It is now safe to disconnect the device." | pass |
| 10 x86 | TS3 Plus | unplugged, re-plugged | approved, the three controllers and the 82574L, every problem code 0 (`rep-t2.txt`) | pass |
| 10 x86 | TS3 Plus | removed by surprise (stick C mounted) | the system answers (`t4.png`) | pass |

So with this project's driver on the dock's controllers both docks pass
every step on 10 x86, as on 2000 and 7; the STOP comes only with Windows'
`usbxhci.sys` on the model's xHCIs, and stops when that driver cannot load.

**Not done:** the `/cmonly` install (the dock controllers were Windows'
own in the failing run already); a "fixed build" leg (nothing to fix in
this tree). No commit, no review (no change). User-visible strings: none.

**Proposed roadmap text (48.36, appended):** "partial 2026-11-10,
`runs/run-48.md`, "48.36": 10 x86's STOP on the TS3 Plus plug is not
xHCI98's: PAGE_FAULT_IN_NONPAGED_AREA in Windows' own `usbxhci.sys`
(stack `USBXHCI.SYS` > `ntoskrnl` register read), reading 80DCh into the
FL1100 model's BAR0 (80A4h into the HP G2's Titan Ridge xHCI), which the
emulator sizes at 16 KB where the real parts' BAR is 64 KB (usb4-emu
`docs/DOCKS.md` section 6's gap); 10 gives a controller that appears after
the install to its own driver, as designed. With `xhci98.sys` on the
dock's controllers both docks pass on 10 x86 (plug, a stick on each
controller, the eject, re-plug, surprise removal). For usb4-emu: the dock
xHCIs' BAR0 at the real sizes. Left: ME's cold start with the dock at
power-on."

QEMU PIDs (all stopped): 195048 (tag a), 173380 (tag b).

### Merge (dock6)

Reading only; the STOP's cause was the emulator's BAR size, fixed in `usb4-emu` w48-embar (below).

## 48.36 (continued) The dock xHCIs' real BAR0 size in the emulator: 10 x86's STOP gone (2026-11-10, w48-embar)

Worker w48-embar, development host A, usb4-emu worktree
`E:\usb4-emu-wt\w48-embar`, branch `w48-embar`. The dock models
(`dock-model=ts3plus|hp-g2`) are on usb4-emu `w48-dock` (`3bbb5c1`), not yet on
main (`f800432`), so the branch starts at `3bbb5c1` (the QEMU tree's
`w48-dock` `0d3e056`); usb4-emu main's later changes (w48-cm, w48-q35s3) are
not in it. QMP 47975 (10) and 47976 (2000); QEMU PIDs in
`E:\xhci98-vm\p48\embar\w10\pids.txt` and `w2k\pids.txt`.

**Cause (w48-dock6):** Windows 10's own `usbxhci.sys` on a dock xHCI read
BAR0 + `80DCh` (FL1100) and + `80A4h` (Titan Ridge xHCI); the model's BAR0 was
`qemu-xhci`'s 16 KB, so the read hit an unmapped page (STOP 0x50).

**Real sizes (public sources only):** FL1100 `1b73:1100` BAR0 64-bit 64 KB
(plus 4 KB BARs 2 and 4) in several linux-hardware.org `lspci -vv` probes and
a KVM pass-through write-up, as usb4-emu `DOCKS.md` 2 reads it from the
owner's capture; ASM1142 `1b21:1242` / `1b21:2142` BAR0 64-bit 32 KB (an NVIDIA
forum `lspci -vv`, a linux-hardware.org probe, Launchpad bug 1749961; MSI-X
table at 2000h); Titan Ridge DD xHCI `8086:15f0` "BAR 0 [mem
0x00000000-0x0000ffff]" (64 KB, 32-bit) in a Linux boot log (an eGPU
write-up), and the 4C host's xHCI `8086:15ec` Region 0 32-bit 64 KB (an LKML
`lspci -vv`).

**Change (usb4-emu `8844cde`, QEMU tree `2aff288`):** `qemu-xhci` (abstract
`pci-xhci`) gains `bar-size` (bytes, 0 default = unchanged 16 KB BAR0; a power
of two 16 KB..1 MB): BAR0 a container of that size, the xHCI registers (MSI-X
table and PBA at 3000h/3800h included) at 0, the rest reads zero and ignores
writes at any size. `dock-model=` sets FL1100 64 KB, ASM1142 32 KB, Titan Ridge
xHCI 64 KB. `docs/DOCKS.md` sections 1, 4, 6 and a new 7.

**Review:** the QEMU working tree and
the usb4-emu commit range `3bbb5c1..8844cde` reviewed: both "approve, no
material findings".

**Build and tests:** `src/build-qemu.sh` (MSYS2 UCRT64, the usb4-emu
toolchain), logs `E:\xhci98-scratch\w48\embar\qemu-build1.log`,
`qemu-build2.log` ("BUILD RC 0"). qtests (`E:\xhci98-scratch\w48\embar\qt\`):
usb4-dock 7/7 (new `/usb4-dock/bar-size`; each preset xHCI's BAR0 sized),
usb4-tunnel 31/31, usb4-usb3 9/9, usb4-tb3dev 13/13, usb4-icm 40/40,
usb4-asm2464 4/4 (run on the build before a comment-only amend).

**Binary snapshot:** `E:\xhci98-vm\q35-bases\bin\dock-20261110-bar0\`
(README.txt, SHA256SUMS): `qemu-system-x86_64.exe`
`33a0f360393016b24dce447b91acb64b2755b63e8b0f99151c8c79d4ba965cdb`,
`qemu-img.exe` `0e3741390cfbb26640c0c50075f78d037807b957d61e5d27c1ae0303868ba99f`.
The launcher's pin is unchanged (the legs use `-Emu`).

### Legs

| system, VM | dock | step | result |
|---|---|---|---|
| 10 22H2 x86, fresh overlay `w10` (tag a, dock2's emulator, for the install) | - | `CrashDumpEnabled` 3, `AutoReboot` 0; `INSTALL /console /yes /nopause /thunderbolt /usb` (w48-dock6's release kit of `c4c26525`: `xhci98.sys` `4CEE246D...`, `xhcicm.sys` `88B8A266...`) | exit 3; powered down |
| 10 x86 (tag b, `dock-20261110-bar0`, `launch-q35.ps1 -Usb4 tb3ar -Emu ...`) | - | XHCITR report before the plug | xhcicm on the Alpine Ridge NHI, 0 Thunderbolt devices (`rep-b0.txt`) |
| 10 x86 | TS3 Plus (`usb4-device,id=dock0,bus=u4.0,port=1,dialect=tb3,dock-model=ts3plus`) | plugged, 15-s screenshots for 270 s (dock6 stopped at 100 to 135 s) | **no STOP** (`t-w1..18.png`); "CalDigit, Inc. TS3 Plus (Thunderbolt 3), approved" (`rep-t1.txt`); `pnputil`: both FL1100s "Fresco Logic USB 3.0 eXtensible Host Controller - 1.0 (Microsoft)" and the ASM1142 "ASMedia USB 3.0 eXtensible Host Controller - 1.0 (Microsoft)", driver `usbxhci.inf`, Started (`pnp-t1.txt`) |
| 10 x86 | TS3 Plus | stick A on FL1100 #2's port 1 (Windows' driver) | "QEMU QEMU HARDDISK USB Device", Started (`pnp-t2.txt`) |
| 10 x86 | HP G2 (`dock1`, `dock-model=hp-g2`) | TS3 unplugged, HP plugged, 270 s | **no STOP** (`h-w1..18.png`); "HP Thunderbolt Dock G2 (Thunderbolt 3), approved", 3 USB devices (`rep-h1.txt`); `8086:15f0` "Intel(R) USB 3.0 eXtensible Host Controller - 1.0 (Microsoft)", `usbxhci.inf`, Started, both Generic USB Hubs Started (`pnp-h1.txt`); no `C:\Windows\Minidump` (`dmpls.txt`) |
| 10 x86 | TS3 Plus (`dock2`) | HP unplugged, TS3 re-plugged, 225 s | no STOP (the screen went dark: the power plan's display timeout, the guest idle and answering, `r-wake.png`); the three dock xHCIs Started on `usbxhci.inf` (`pnp-r1.txt`); shut down from the guest |
| 2000 SP4, a copy of w48-dock5's `w2k` overlay (xHCI98 and the dock's devices installed there with dock5's kit, `xhci98.sys` `56554677...`), on `dock-20261110-bar0` | TS3 Plus | plug | "CalDigit, Inc. TS3 Plus (Thunderbolt 3), approved"; "xHCI98 USB 3 Controller (Fresco Logic FL1100)" twice and "(ASMedia ASM1142)" (`w2k\rep1.txt`); `info pci`: BAR0 64 KB, 64 KB, 32 KB (`w2k\infopci.txt`) |
| 2000 | TS3 Plus | stick A on FL1100 #1's port 1, stick C on the ASM1142's port 1 | E:, F:: "FC: no differences encountered" twice each (pass) |

So the 10 x86 plug of either dock no longer stops with Windows' own driver on
the dock's controllers, and xHCI98 still drives the dock's controllers at
their new BAR sizes (2000).

### Merge (usb4-emu, w48-embar)

Not merged into `3.4.3.0` (an emulator change): `usb4-emu` w48-embar `8844cde` on w48-dock `3bbb5c1`, the binary `q35-bases\bin\dock-20261110-bar0`; both branches go into `usb4-emu` main with one pinned binary (owner, 2026-11-10).

## The 9x first-boot fatal exception 0E, a fair batch (worker w48-boot0e2, 2026-11-10)

Worker w48-boot0e2, development host A, branch `w48-boot0e2` from
`de877f8e` (3.4.3.0's tip), worktree `E:\xhci98-wt\w48-boot0e2`. QMP 47720
(slot 1) and 47721 (slot 2); every QEMU start and stop in
`E:\xhci98-scratch\w48\boot0e2\pids.txt`. Kit scripts in
`E:\xhci98-scratch\w48\boot0e2\kit`, VMs in `E:\xhci98-vm\p48\boot0e2`.


### Build and kit

`build-driver.cmd all` at `de877f8e` (`XHCI98_SELFTEST_SKIP_OK=1`, a gated
slot): "BUILD + GATES PASSED" (`gated-x86.log`); `make-package.ps1` release
and qemu x86 (`pkg.log`); tools (`btools.log`); kit
`E:\xhci98-vm\p48\boot0e2\xfer` (dock5's family layout, `SHA256.TXT`):
release `KIT\98-me\cm\xhcicm.sys` `0728CFAD...`, qemu `Q\xhcicm.sys`
`4C898B5C...`.

### Method

One install image per arm and system, each a fresh overlay of the q35 base
(`prep.sh`): booted with `-Usb4 adl` and nothing attached, the wizards
answered with Enter, `D:\I<o><arm>.BAT` run from Start, Run, then Shut
Down before the first boot after the install:

| arm | install | then |
|---|---|---|
| A | `INSTALL /console /yes /nopause /thunderbolt /stor98` | qemu `xhcicm.sys` copied over the release one (ME: qemu `xhci98.sys` too, as dock5) |
| B | as A | `XhciCmReenum`=0 in `HKLM\System\CurrentControlSet\Services\USB` (the machine-wide key, read before the driver key) |
| C | `INSTALL ... /stor98` (no connection manager) | (ME: qemu `xhci98.sys`) |
| D | nothing of xHCI98 | - |

Each trial (`trial.sh`): the arm's image copied afresh into the slot's
folder (and a fresh NVMe overlay), cold start with chain's dock model at
power-on (`-Usb4 adl`, `usb4-device ... dialect=usb4,downstream-ports=1,
chain-power-controller=off,drive=nvme0`, w48-chain's emulator
`E:\xhci98-vm\p48\chain\bin`, as dock5's `me1`), a screenshot every 20 s,
any text-mode screen after 40 s read from the VGA buffer for "exception"
or "error has occurred", ME watched 240 s; on a fault the screen kept, a
key sent and the CM log copied. Host load sampled before the start and at
40 s (`load.ps1`: host CPU %, other QEMU processes and their CPU, gated
slots held, compiler processes). Two slots, the arms interleaved in each
(slot 1 A B C D, slot 2 C D A B), so host load falls on all arms alike.
On ME the installer stages the release `xhci98.sys` as `XHCI98.TMP` in A and B
(WININIT renames it at the first boot), not in C; no HCD line reaches
debugcon in either (C's log is empty).

### Faults met (each with its CM log)

- `mA203` (ME, arm A, slot 2, 14:12): "Error: 0E : 019F : BFF7B018" at
  the 64 s screenshot (the address dock3's `cme2` showed). Host: CPU 80 %
  at the start, 97 % at 40 s; 4 other QEMU processes (98 %, then 63 %
  CPU); 3 gated slots held. CM log (`run2\debugcon-mA203-fault.log`;
  empty while the screen stood, as dock5 read): call workers started and
  `cm.reenum.9x.version` at 2.98 s and 3.08 s of the CM's clock,
  `cm.reenum.up.poll` at 5.0 s, `cm.brnum.9x.pci-open` at 6.0 s, then
  nothing until 79.4 s (the key). Clean A boots go on to the call workers'
  openings read back at +4.0 s from their start; here those came at
  +76.4 s. So the fault fell 3 to 5 s after the workers' start, inside the
  re-enumeration's opening seconds, as in dock5's `me1` b (after
  `version`, before `up.poll`) and chain's 98 SE boot (the same).
- Arm B's logs (`XhciCmReenum` 0, read as `cm.reenum=00000000`): the same
  four threads and the same opening at the same moment, but no
  `up.poll`, no `brnum` and no re-enumeration; the call workers'
  openings are read back at +0.2 to +1.5 s.
- `mA106` (ME, arm A, slot 1, 15:08): "Error: 0E : 019F : BFF62170".
  Host: CPU 22 % then 19 %, 2 then 1 other QEMU process (79 %, 10 %
  CPU), 4 gated slots held. CM log: call workers started and `version` at
  3.50 s and 3.54 s, then nothing until 77.5 s (`up.poll`, after the key):
  the stall of dock5's `me1` b exactly.
- `mE206` (ME, arm E = A with the pacing candidate below, slot 2, 15:12):
  "Error: 0E : 019F : BFF62170". Host: CPU 3 % then 65 %, 1 other QEMU.
  CM log: the four threads made one at a time (each started in 13 to
  54 ms), `callwk.started` at 3.67 s, then nothing until 83.0 s.

So the fault is not the host's load (one fault at 97 % host CPU, two at
3 to 22 %).

### Candidate 1 (refuted): one thread creation at a time

`CmThrSpawn` on 9x made to wait (bounded, 1 s) for each new thread's
first act before the next creation, logging the wait (`cm.cfgwk.spawn.ms`,
`cm.callwk.spawn.ms`); diff kept as `fix1-pacing.diff`, reverted. Read:
threads start in 0 to 82 ms (one wait ran to the 1 s bound, `mE205`); arm
E faulted in its third boot with the threads paced, so the burst of
creations is not the cause. Arm B, which makes the same four threads at
the same moment, has no fault either.

### 3.4.2.0 and earlier

`v3.2.0.0`, `v3.4.0.0` and `v3.4.2.0` all write `XhciCmReenum` 1, start
the call workers and run the boot-time `up.poll` (2000 ms budget, a read
each 50 ms): the same path, so not a 3.4.3.0 regression.

### tbs6's sighting

`0187:BFF9DFFF` after a 98 SE standby and a DOS box (worker tbs6) is the
standby family lessons.md records with no USB controller at all
(`016F:BFF9DFFF`), not this first-boot fault.

### Candidate 2 (refuted): no boot-time bridge poll on the CONFIGMG route

`cmPoll` skipped on the CONFIGMG route (`up.poll` read as 4, no
reading; diff `fix2-nopoll.diff`, an experiment never meant for commit,
reverted); arm G = A with that qemu `xhcicm.sys` (`1B6CD5F8...`). The poll
times out every boot on the emulator (`up.poll=00000303`, 2 s of reads
every 50 ms; record 21 section 6.5: on real hardware it normally ends at
once), so it was the A-only activity in the first windows. G faulted in
two of its first three boots: `mG210` "0E : 019F : BFF77328" (the log
stops after `cm.brnum.9x.pci-open`, before the numbering's outcome) and
`mG110` "0E : 019F : BFF62170" (stops right after `version`, before
`pci-open`). So the poll is not the cause either.

What every faulting log has in its window is the CONFIGMG worker's first
re-enumeration operation: its opening (baseline walk, tunnel-id window),
then `cmWkNumber`'s PCI route opening (`CmCfgmgPciOpen`: a HalGetBusData
read and PCI.VXD's `_PCI_Read_Config` through the dynalink) and the
bridge numbering (`_PCI_Write_Config` into the bridges the tunnel brought
up in reset state), before `Reenumerate_DevNode`. Arm B runs the opening
but not that operation (no re-enumeration), and has no fault.

### Arm H: A with `XhciCmBridgeNumbering` 0

The same install as A plus `XhciCmBridgeNumbering`=0 in the machine-wide
key (`NUMOFF.REG`; read as `cm.bridge.numbering=00000100`): the worker
re-enumerates the members without numbering the bridges below them (no
`_PCI_Write_Config`, no window programming); its PCI route still opens
later (`pci-open` after the `Reenumerate_DevNode` calls). On the
emulator the drive behind the enclosure then does not come up (the
numbering is what lets 98 SE and ME enumerate it, record 21 section
6.1.1), so H is a probe, not a remedy.

Two trials ended "gone" (`mH123`, `mA123`, 16:48-16:54): the screenshot
request failed while the host ran 10 other QEMU processes at 940 % CPU;
no CM log was kept, so they are counted as not read.

### Counts (ME, first boot after the install, chain dock at power-on, 240 s each)

| system, arm | boots read | fatal 0E | not read | host CPU at start (min-mean-max %) | other QEMU processes |
|---|---|---|---|---|---|
| me A | 25 | 2 | 1 | 22-59-94 | 1-12 |
| me B | 16 | 0 | 0 | 7-60-100 | 1-12 |
| me C | 15 | 0 | 0 | 20-60-88 | 1-10 |
| me D | 13 | 0 | 0 | 31-61-93 | 2-10 |
| me E | 6 | 1 | 0 | 3-37-56 | 1-7 |
| me G | 4 | 2 | 0 | 47-63-71 | 5-12 |
| me H | 8 | 1 | 1 | 36-52-80 | 3-10 |

Host load per boot is in `results.txt` (two samples a boot: before the
start and 40 s in). The six faults came at host CPU 3 % to 97 %, with 1
to 6 other QEMU processes: host load is not the trigger.

Faulting CM logs (6 here, plus dock5's `me1` b and chain's 98 SE boot):
every one stops inside the CONFIGMG worker's first member operation at
boot, 0 to 6 s after the worker's start, at varying points within it
(after `version`; after `pci-open`; after the first `Reenumerate_DevNode`
calls in H). The faulting addresses vary (`BFF7B018`, `BFF62170` three
times, `BFF77328`, `BFF775DD`; dock5 `BFF775B4`), all KERNEL32 ring 3.

### Verdict

Ours. The arms that run the worker's first operation (A, E, G, H) faulted
6 times in 43 read boots; the arms that do not (B: the same threads and
the same opening, no operation; C: no connection manager; D: no xHCI98)
0 times in 44. Fisher's exact test, one-sided: p = 0.012. Arm A alone, 2
in 25 against 0 in 28 (C, D), is not significant by itself (p = 0.21);
the pooled test and the timing (every faulting log, eight of them, stops
in the same few seconds of a boot lasting minutes) carry the verdict.

Not the cause, each refuted by a fault in its arm: a burst of thread
creations (E), the boot-time bridge poll (G), the bridge numbering and
its window programming (H). Left in the window: the worker's operation
itself (the opening's baseline walk and tunnel-id window run in B too, so
the parts B does not run are the operation's member walk and vanish
step, the PCI route, `Reenumerate_DevNode` and what CONFIGMG does for
the new devnodes then). Not pinned down; no fix committed.

Every release since the 9x route has this path: `v3.2.0.0`,
`v3.4.0.0` and `v3.4.2.0` write `XhciCmReenum` 1 and run the same start.
Workaround: `XhciCmReenum` 0 (arm B, 0 in 16), at the cost of the 9x
automatic re-enumeration (a device tunnelled at boot or plugged later
then needs a manual refresh).

98 SE: not run in this batch (the time went to the ME arms and the two
candidates); its images are prepared (`w98se-A` to `-D`).

### QEMU processes

Every start and stop is in `pids.txt` (113 starts); all stopped at 17:20.
One slip: the first kit script was run with dock5's default path and
replaced w48-dock5's finished `E:\xhci98-vm\p48\dock5\xfer` with this
tree's release kit (no guest scripts); dock5's own `mkkit.ps1` rebuilds
it.

### Merge (boot0e2)

Reading only, no fix: a known issue since `3.2.0.0`, workaround `XhciCmReenum` 0, in the release notes; left to "Planned - Left by `3.4.3.0`" (ME0E.2).

## A gain learned only when it is a tunnel's far end or comes with the link's evidence (48.43, 2026-11-10)

Worker learn3, branch `w48-learn3` from `ddf03ada` (branch `3.4.3.0`)
(worktree `E:\xhci98-wt\w48-learn3`). Evidence `E:\xhci98-vm\p48\learn3\`.

### The hole

`src/cm/tbt_topo.c` `XhciTbtLearn`: the rule for a port pending alone
attributed one gain of any shape. The host router's own xHCI behind a
hot-plug capable port of its switch (the emulator's model), or a device
that is no tunnel's, answering late while one port was pending alone (a
port found up at start whose function had not answered, or a hot plug)
was learned as that port's: a wrong LEARNED binding in the shared table.
The chained transaction (`src/cm/tbt_chain.c` `XhciChainTry`, record 34
section 3.12 item 4) had the same hole: a function of the parent's own
answering late on an empty chain-port candidate.

### The rule (record 22 section 7.6.4, new)

Under the rule for a port pending alone, the narrowed rule (7.6.3) and
the chained transaction, one gain counts only when `XhciTbtGainAdmits`
admits it:

1. a tunnel's far end: device 0 alone, an Upstream Switch Port (no other
   evidence needed); or
2. any other shape (a direct endpoint: the emulator's plain enclosure puts
   its NVMe directly behind the tunnel port; several devices) with the
   link's evidence: the bridge's port reading (`XhciTbtPortRead`) was DOWN
   (at least one of Link Active / Presence Detect / SHPC slots reported,
   all absent) at a reading where nothing answered behind it, nothing read
   answering since, and UP at the gain. An integrated function is wired
   behind its port, which reads present while it is there, so it never
   gets a DOWN reading.

Evidence kept per member (`XHCI_TBT_BINDS.BaseDown`: each arm, kept by
location across arms, refreshed at each attempt that moves the baseline,
cleared when the member is read occupied; an arm that fails keeps the
old baseline and bits) and per chain candidate at the arm
(`XHCI_CHAIN_BASE.Down`, the arm being right before the kid's Path
Enable). Unreadable shape or port: a reading not made (`UNREAD`, baseline
kept; chain `WAIT`). Not admitted: pending alone -> new `SHAPE`
(`u4.learn` low byte `0F`), the gain consumed, the port still pending;
narrowed rule -> `SEVERAL`; chain -> `SPOILED` with
`XHCI_CHAIN_TRY_SHAPE` (`cm.chain.txn` low byte `09`). The router-named
source is positive evidence of its own (unchanged); the inference and the
start reading keep far end only.

Costs recorded: a tunnel found up at start whose direct endpoint answers
late (no down reading) stays pending; on the emulator's i440FX SHPC
tunnel bridge a plain enclosure is no longer learned (the learning's
read-only `HalGetBusData` accessor cannot do the SHPC DWORD Select
write), so its eject keeps the tunnel up and no stor98 statement names
its member; docks (far ends) there and plain enclosures on q35 tunnel
ports keep learning. Residual: a function behind a port with no presence
reporting whose link goes down and trains again would carry the
evidence; none read on a member.

Every other place a binding is made checked: router-named (no gain),
`KNOWN`/`XhciTbtLearnBound` (existing binding), recovery and resume
(through `XhciTbtLearn`), inference and start reading (far end already),
the chain (now this rule).

### Commits

- `05bf7dd2` the rule (tbt_topo, tbt_chain), record 22 7.6.4 and pointers
  in 7.6/7.6.3, record 34 3.12 item 4, host vectors.
- `2023c3ce` an arm that fails keeps the old baseline and its evidence
  (review finding).
- `48267559` record 22 7.6.4 "As read" (docs only, after the builds).

Host: full suite PASSED at `05bf7dd2` (`host1.log`); `test_tbt_topo`
910 -> 967 checks at `2023c3ce`. Mutation (an endpoint admitted without
evidence): 15 failures, among them the reported case binding port 1 to
the xHCI's member (`host-mut.log`).

### Review

Two reads against `ddf03ada`: one medium finding (a failed re-arm lost the
evidence; fixed in `2023c3ce`), then approved ("No material findings").

### Builds

Gated `release` x86 and `release -amd64` of `2023c3ce`: "BUILD + GATES
PASSED (release)" both (`build-x86.log`, `build-amd64.log`),
`XHCI98_SELFTEST_SKIP_OK=1`, slot `E:\xhci98-locks\gated.2` (made,
owner.txt, removed after). x86 `xhcicm.sys` sha256
ca41751542505fb970612fa6bc4a2b931fbec90b4fec54f854c9562726bac1b5.

### Legs

8.1 x86: fresh copies of ejw's overlay, learn2's launcher with the
coordinator's padded emulator (`asm4242-20261027-fwpad`,
`usb4-asm-usp ... fw-bus-pad=48,fw-mem-pad=16M`,
`-global usb4-asm-dsp.hotplug=off`, tunnel ports
`power_controller_present=off`), enc0 (ASM2464) at power-on;
`xhcicm.sys` copied from the xfer disk (fc equal), the setting in the
NHI's key (`Class\{4d36e97d...}\0037`), restart. XP: a copy of the chain
worker's XP overlay, `-Usb4 adl` (chain bin, `host-model=intel-adl`), no
enclosure at power-on.

| Guest | Setting | Step | Result |
|---|---|---|---|
| c (pid 187800, qmp 47874) | `XhciCmInferAtStart` 0 | start | `u4.preserved=00010808`, `u4.map.none=FFFFFFF5` (`r1-boot.txt`) |
| c | | enc1 (ASM2464) `device_add` on USB4 port 2 while enc0 (port 1, at start) stays | **`u4.learn=00000203`** (`r2-plug.txt`); enc1's USP 33:00.0, DSP and Stor98 at **problem 0** (the padded launcher) |
| c | | `XHCITR /eject id=<enc1 USP>`, OK | **"Windows has stopped QEMU NVMe Ctrl SCSI Disk Device. The eject was confirmed to the Thunderbolt driver. The PCIe tunnel was closed." / "It is now safe to disconnect the device."** (`s11.png`); `cm.reenum.confirm=00000208`, `cm.reenum.eject=00000208`, `u4.eject.bridge=00000208`, `u4.eject=00000200` (`r3-eject.txt`) - port 2's eject after the narrowed learning, owed since learn, now read |
| b2 (pid 41604, qmp 47875) | 1 | start | `u4.infer=00000100`, `u4.infer.bdf=00000200` (`r1-boot.txt`) |
| b2 | | `XHCITR /eject id=<enc0 USP>`, OK | "... (E:). ... The PCIe tunnel was closed." (`s11.png`); `u4.eject.bridge=00000200`, `u4.eject=00000100`, `u4.eject.inferred=00010200`, `u4.infer.closed=00010200`, `cm.kept.gone=20030300` (`r2-eject.txt`) |
| adl (pid 52784, qmp 47876) | - | a plain enclosure (`usb4-device ... port=1`, no device-model: the NVMe directly at 01:00.0 behind tunnel port 00:05.0) `device_add` after the desktop; the set's third member is the platform's own xHCI root port 00:1C.0 | **`u4.learn=00000103`** on the link's evidence, tunnel port 00:05.0 (`r1-plug.txt`); Stor98 by the wizard (Continue Anyway), started, F: (`r2-stor.txt`) |
| adl | | `XHCITR /eject id=<the NVMe>`, OK | "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (F:). The eject was confirmed to the Thunderbolt driver. The PCIe tunnel was closed." (`s7.png`); `cm.reenum.confirm=00000028`, `u4.eject.bridge=00000028`, `u4.eject=00000100` (`r3-eject.txt`) |

All three stopped by ACPI powerdown, exits confirmed (`pids.txt` in each
folder). The pending-alone refusal of an integrated function appearing
late, and the unreadable shape, are host vectors only (the emulator
cannot make its xHCI answer late on a member).

### Left

- The owner's board: its enclosures' far-end shape and its tunnel ports'
  Link Active / Presence Detect reporting not read.
- The i440FX SHPC bridge's plain enclosure is no longer learned (recorded
  cost); not read in a 9x guest.

### After the merge review (2026-11-10)

The branch was rebased onto `3.4.3.0` (`b377213d`); the commits above are
now `bc1d08d49`, `38b531c6a` and `3f5cd0f7d`. The merge review's high (a
re-arm that read a member occupied and then failed kept its DOWN bit)
and four more stale-evidence paths the fresh reads found were fixed:

- `26c1e87a4` the arm clears a member's retained bit when it reads it
  occupied, whatever follows.
- `b147f23d5` an attempt that ends before the baseline moves holds the
  bit of each member it read occupied (`XHCI_TBT_BINDS.BaseHeld`), spent
  when that member reads empty; the chain marks a candidate that gained
  at a waiting try (`XHCI_CHAIN_TXN.Answered`) and spoils if it reads
  empty.
- `43d5ae5fc` a held bit spent the moment its member reads empty though
  another member fails; a device seen in a scan that then failed counts
  as occupancy (arm, attempt, chain).
- `b8c6b87d3` the chain's partial read reports the candidates scanned
  whole; an answered one read empty spoils.
- `d2e476ecc` a partial chain read that finds a candidate at another
  place than the baseline's spoils.

Each with host vectors that fail without it; `test_tbt_topo` 1004
checks. Reviews against `ddf03ada`: needs-attention three times (the
findings above), then approved ("No material findings"). Guest-visible
behaviour of the legs unchanged (the paths need read failures or a
function going quiet mid-attempt); no new legs.

Builds after the fixes: gated `release` x86 and `-amd64` of `d2e476ecc`
(on `3.4.3.0`): "BUILD + GATES PASSED (release)" both (`build3-x86.log`,
`build3-amd64.log`), slot `gated.4` (made, removed). x86 `xhcicm.sys`
sha256 fe0bac7493ff796b71469842f78e2fa77f9777f98f251a34261fcbac4b191e68.
(A first attempt, `build2-*`, was stopped part-way to fix the second
review's finding; its out\gated-*.lock directories were removed by hand.)

### The second merge review (2026-11-10)

The second merge review (base `b377213d`) found one more of the same
family (a chain partial read ignoring a baseline device lost); the class
- a reading that fails part-way treated more leniently than a whole one,
or link evidence outliving an observed occupancy - was then closed:

- `2da14d456` a partial chain read held to a whole reading's rules (device
  lost, two gains, busy or blocked gain).
- `cdbffd651` a partial chain read decides nothing: any change it saw
  spoils; depth 1 spends the evidence of a gain seen at an incomplete
  attempt that `XhciTbtGainAdmits` rejects outright, or any gain on a
  member whose scan failed.
- `073c5ad16` reaching an already answered candidate in a partial read
  spoils.
- `22655f5f7` held evidence behind a member scan that fails is spent.
- `7ff62a8ca`, `b288c9e1a` the walk's bus numbers (secondary and
  subordinate) kept and compared, whatever read fails after the walk.
- `7c81f9222` a failure of the candidate walk itself spoils (the walk never
  descends below a candidate, so a kid's Retry Status cannot fail it).

Each with host vectors that fail without it; `test_tbt_topo` 1038
checks. Recorded costs (record 22 7.6.4): a kid whose gain is first seen
by a read that fails elsewhere, or during a failed walk, is unattributed;
a direct endpoint first seen on a member whose scan failed is not
learned. Reviews against `b377213d`: needs-attention six times (the
findings above), then approved ("No material findings"). No new legs:
the paths need read failures mid-attempt.

## Gated build of the tip 7c81f9222 (after the host reboot, 2026-11-10)

- gb.cmd (slot gated.2, TEMP/TMP set to learn3\tmp): release x86 and
  release -amd64 both "BUILD + GATES PASSED (release)", EXIT 0
  (build4-x86.log, build4-amd64.log).
- x86 src\cm\objfre\i386\xhcicm.sys sha256
  d7fb5eac8ca844fe2c789c0d8acc69e0265be8c9953009737aa4767e9e5f0404
- amd64 src\cm\objfre\amd64\xhcicm.sys sha256
  1fd4191956c76e62988bac4d72496122bfa2366fc18d0c02667c435a006c11b8
- Coordinator's third merge review: [high] tbt_topo.c:1374-1379 partial
  negative evidence dropped on ERROR/GONE. Follow-up commit on top.
- Fix 12c05a886 (partial negative port evidence kept in XhciTbtGainAdmits)
  and d53db4f52 (ShpcSeen: SHPC slots read empty count even when only the
  restoring select write failed, the first review's P2). Review against
  7c81f9222: review14 needs-attention (that P2), review15 approve, "No
  material findings". test_tbt_topo 1048 checks, 0 failures.
- Gated release x86 of d53db4f52 PASSED (slot gated.3, build5-x86.log);
  xhcicm.sys sha256
  9ffe16e015685518f4902c108554ff75409e8f0198d8521e77128154f019e7f3

### Merges (w48-learn3)

Merged into `3.4.3.0` at `e144a92e` (`7c81f922`) and `aeae7d8f` (`d53db4f5`, the negative port evidence), 2026-11-10.

## 48.35 (98 SE's standby on a clean base) - worker tbs6 (2026-11-10)

Task 48.35's 98 SE question from worker tbs5 (`run48-tbs5.md`: its 98 SE
readings ran on `win98-gold`, whose 2.0-era `XHCI98.SYS` of 114,704 bytes
the install did not replace) taken again on a base with no earlier
xHCI98. Branch `w48-tbs6` from `3.4.3.0` at `ddf03ada`,
`XhciCmSleepEntry` 1 by default. **No code change, no commit**: the
failure read below is understood as far as where the drive is lost, not
fixed (the coordinator asked for an early result).

**Build and kit.** Gated `build-driver.cmd release` x86 of `ddf03ada`
(`XHCI98_SELFTEST_SKIP_OK=1`, slot `gated.4`), `BUILD + GATES PASSED
(release)` (`E:\xhci98-scratch\w48\tbs6\gated-x86.log`); `make-package.ps1
-Arch x86`, `ConvertTo-FamilyLayout`; `INSTALL.BAT`, `xhciinst`
(`XHCIINST.EXE`, `XHCIWIZ.EXE`, `XHCIDI.EXE`) and `xhcitr` from this
tree, the licence, readme and the other tools from tbs2's kit
(`E:\xhci98-scratch\w48\tbs6\kit.ps1`, `kitall.log`). `98-me\`: `xhci98.sys`
365,361 bytes (SHA-256 `ff2cb8c96ca3ee39...`), `xhciuas.sys` 43,878,
`cm\xhcicm.sys` 503,268 (`ecfb3ae114db3466...`), `cm\XHCIPFW.VXD` 11,961,
`cm\tbnet\xhcinet.sys` 16,972, `stor98\stor98.mpd` 65,422,
`stor98\S98VSD.VXD` 6,878. (The tree still stamps its tools and Stor98
`3.4.2.0`.)

**Vehicle.** Development host A, TCG, QMP 47850 and 47851, at most two
guests at once; the host carried other workers' guests throughout (CPU
99 % at 14:26, 6 QEMU processes). 98 SE on `pc` (i440FX, stock SeaBIOS),
one processor, 512 MB, S.1's topology as tbs5 ran it: `usb4-tunnel-bridge`
00:05.0 with SHPC, `usb4-host profile=usb4-generic` 00:04.0, the ORICO
model (`device-model=asm2464`, an overlay of the shared FAT NVMe) at
power-on unless "n", `qemu-xhci` 00:06.0; the pinned emulator
`asm4242-20261025-2230` (`E:\xhci98-vm\p48\tbs6\tools\go98.ps1`, over
tbs5's `gopc.ps1`).

### The clean base and the install

- **Base**: `vm\t26\win98-clean.qcow2` (read-only; `win98-gold`'s own
  backing). Overlay `s0`. First boot: the "PCI standard PCI-to-PCI bridge"
  wizard finished (Windows' `MACHINE2.INF`), the "PCI Universal Serial
  Bus" wizards cancelled. After the install a full-disk `dir XHCI*.* /s`,
  `STOR98*.*`, `S98VSD*.*` found only the files this kit put there (all
  dated 2026-11-10), so the base carried no earlier xHCI98.
- **Install** (`C:\KITS\INSTALL.BAT /console /yes /nopause /thunderbolt
  /stor98`, `s0\serial-k.log`): exit 4. On this base neither controller is
  a live devnode before its wizard has run (the installer lists only the
  base's old `BUS_00&DEV_04` record of 1B36:000D, "registry only", and
  "No Thunderbolt / USB4 controller found"), so it copies the package to
  `C:\XHCI98`, adds it to DevicePath, places `xhciuas.sys`, `xhcicm.sys`,
  `stor98.mpd`, `S98VSD.VXD`, `XHCIPFW.VXD` and prints the Device Manager
  steps. Restart with the Windows 98 SE CD attached (`D:\isos\Win98SE.iso`
  as the secondary master, `E:`): the wizards found `C:\XHCI98\XHCICM.INF`
  (xHCI98 USB4 Router), `xhci98.inf` (asked for the "xhci98 xHCI Host
  Controller Disk": `C:\XHCI98`, then the Windows CD for `usbd.sys`:
  `E:\WIN98`, the install's own step 6) and `STOR98.INF` (the tunnelled
  NVMe), each "Next" to "Finish". Cold start (`s0\serial-n.log`).
- **Every installed driver is the package's**, by size (`G9.BAT chk`, a
  full-disk `dir`): `SYSTEM32\DRIVERS\XHCI98.SYS` 365,361,
  `xhciuas.sys` 43,878, `xhcicm.sys` 503,268; `SYSTEM\XHCIPFW.VXD` 11,961;
  `SYSTEM\IOSUBSYS\stor98.mpd` 65,422, `S98VSD.VXD` 6,878 - each the kit's
  size with the kit's time stamp. (`xhcinet.sys` is not installed: `/yes`
  leaves Thunderbolt networking off.)

### The old image: does 3.4.3.0 replace the 2.0-era XHCI98.SYS? Yes

`old\oldbase.qcow2`, a copy of tbs5's `s98\disk-after-k.qcow2` (over
`win98-gold`: the old `XHCI98.SYS` 114,704 bytes, tbs5's build of
`4fbe568b` installed), overlay `old1`, the same topology and command
(`old1\serial-k.log`, the log dumped to COM1): rows "xHCI98 USB 3
Controller (Red Hat QEMU xHCI) ... driver xhci98 ...; Update", "xHCI98 USB4
Router ... Keep (same version)", "Stor98 ... Replace files (same
version)"; `xhciuas.sys` and `xhcicm.sys` queued in `WININIT.INI` by
bytes; the helper reinstalled the controller through Windows' own
installer (Windows asked for `usbd.sys` and `usbui.dll`; answered
`C:\WINDOWS\SYSTEM32\DRIVERS` and `C:\WINDOWS\SYSTEM`); Results:
"WININIT.INI: C:\WINDOWS\System32\Drivers\xhci98.sys is replaced by this
package's at the next start"; `/yes` left `stor98.mpd` ("Disconnect all
external NVMe drives" not confirmed, as designed); exit 4. After the next
start (`old1\serial-m.log`): `SYSTEM32\DRIVERS\XHCI98.SYS` 365,361,
`XHCIUAS.SYS` 43,878, `XHCICM.SYS` 503,268 - the package's; the 114,704
file is gone. **48.42's fix holds on that image.**

The shutdown after that install hung on a black text page (20+ minutes,
EIP `c002f1d7`, not halted; quit over QMP); the next start was clean.

### Legs

Leg script `tools\leg98.sh` (new): cold start, `G9.BAT pre <tag>` (the
drive holding `TESTDATA.BIN` found, `W<tag>A.BIN` written and `fc /b`
compared, an `XHCITR /report`), the DOS box closed, `tools\sby98.sh`
(tbs5's GUI Stand by, QMP polled for "suspended", `system_wakeup` after
30 s, the taskbar waited for), `POST9.BAT <tag>` (the drive found again,
`W<tag>A.BIN` compared, `W<tag>C.BIN` written and compared, an `XHCITR
/report`), then Start, Shut Down with "Shut down" selected by its radio's
pixel (98 SE remembers Stand by as the last choice), timed. The value by
`regedit` into `Services\USB` (`b0`: 0; `b2`: 1 with `XhciCmReenum` 0).

| Leg | Overlay | `XhciCmSleepEntry` | Enclosure | S3 | Resume | The drive after | Shutdown |
|---|---|---|---|---|---|---|---|
| 1a1 | `b1` | 1 (`cm.usb4.sleep=00000101`) | ORICO | yes, 2 s | desktop 20 s after the wake | **gone**: `E:` held `W1a1A.BIN` compared before; after, no drive; `u4.sleep.entered=00000002` (both routers Sleep Ready), `cm.suspend=4`, `cm.resume=1`, `cm.reenum.resumed=1`; QEMU `info pci`: bridge 00:05.0 secondary and subordinate bus 0, windows closed, IRQ 0 | 14 s |
| 1a2 | `b1` | 1 | ORICO | yes, at once | desktop 21 s after | **gone**, the same readings; bridge 00:05.0 all 0 | 20 s |
| 0a1 | `b0` | 0 (`00000100`) | ORICO | yes, 3 s | desktop 26 s after, then **the guest reset** (QEMU, `-action reboot=shutdown`, exited about 4 s later) | - | - |
| 1n1 | `b1` | 1 | none | yes, 2 s | desktop; bridge 00:05.0 restored by Windows (secondary bus 1, IRQ 10) | - | - (fatal exception 0E at `0187:BFF9DFFF` while the post step was awaited; host CPU 99 %; quit) |
| 1a3 | `b1` | 1 | ORICO | yes, 3 s | desktop 21 s after; bridge 00:05.0 all 0 again | not read: the post step's DOS box raised fatal exception 0E at `0187:BFF9DFFF` (host CPU 64 %, 5 QEMU); then a black desktop, clock stopped; quit | - |
| rA1 | `b2` | 1, **`XhciCmReenum` 0** | ORICO | yes, 2 s | desktop 21 s after; **bridge 00:05.0 restored** (secondary bus 1, IRQ 10) | not read: the same 0E when the post step started; quit | - |
| rA2 | `b2` | 1, `XhciCmReenum` 0 | ORICO | yes, at once | desktop 22 s after; bridge restored (secondary bus 1) | not read: the same 0E, the post step started 90 s after the desktop; quit (host CPU 55 %, 8 QEMU) | - |
| c0 (control) | fresh `win98-clean`, **no package** | - | ORICO (no driver) | yes, at once | desktop 22 s after; bridge 00:05.0 restored by Windows (secondary bus 1) | - | 16 s |

Every QEMU start and stop is in `E:\xhci98-vm\p48\tbs6\pids-all.txt`.
The shutdown after `b0`'s value write (no enclosure, no standby before
it) hung on a black text page (HLT at `ff0270ed`, 300+ s; quit), as the
old image's did after its install; the shutdowns after 1a1, 1a2 and the
control took 14 to 20 s. After a resume the guest drops the Shift of the
first typed `:` ("C\T\POST9.BAT" not found), so the leg types
`\T\POST9.BAT`.

### Where the drive goes (1a1, 1a2)

The connection manager's part works: Enter Sleep and Sleep Ready on both
routers, and after the resume the tunnel is rebuilt (`u4.pcie.up=00010809`,
`u4.port.state=00000105`) and section 7.6's obligation met
(`cm.reenum.resumed=1`). What is lost is the tunnel port bridge's own
configuration: after the wake its whole header reads reset (bus numbers
0, windows closed, interrupt line 0) and it stays so; the CM logs
`cm.nvlink.up.no-bus=00000028` (00:05.0 has no secondary bus),
`cm.brnum.9x.held=00002801` (the bridge's old child devnode is still in
the tree, so the 9x numbering does not run), and the re-enumeration's walk
finds nothing below it (`cm.reenum.9x.noirq.walk.node=FFFF0001` for every
node), so the NVMe devnode is left without a bus and the drive letter
goes. Windows 98 SE itself restores that bridge after a standby when the
tunnel carries nothing (leg 1n1) and when the package is absent (control
c0); with the enclosure attached and the package installed it does not.
On ME (tbs5's `me8` debugcon) the CM logs the same `no-bus` and `held`
lines at the resume, but a moment later the walk finds the NVMe
(`cm.reenum.9x.noirq.walk.node=01087F00`): ME restores the bridge, 98 SE
in this case does not.

**What turns the restore off: the 9x re-enumeration at the resume.**
With `XhciCmReenum` 0 (rA1, rA2: the same image, the enclosure attached,
`XhciCmSleepEntry` 1) Windows 98 SE restored the bridge after every
resume, as it does with no tunnel up (1n1) and with no package (c0); with
the re-enumeration on (1a1 to 1a3) it never did. The CM posts the
member's re-enumeration about 190 ms after `cm.resume` (`cm.ms` 0x2BB96
to 0x2BC7F in 1a1), while the bridge still reads reset, and its
`Reenumerate_DevNode` on that bridge then (record 21 section 6.1) is what
leaves it unconfigured on 98 SE; ME restores it regardless. Whether the
drive itself comes back with the re-enumeration off was not read: the
post step's DOS box raised the fatal 0E in both such legs (and in 1n1
and 1a3), which the owner's note says to record, not chase.

### What this says

1. **98 SE standby with a tunnelled drive does not work at 1**: S3 is
   entered with Enter Sleep and Sleep Ready on both routers and the
   desktop comes back, but the drive is lost (1a1, 1a2; 1a3 the same
   bridge reading), because the tunnel port bridge stays unconfigured.
   **At 0 the guest reset** right after the resume (0a1, once). tbs5's
   `s98r` freeze and `s98z` "0 file(s) copied" fit this; its old
   `XHCI98.SYS` was not the cause.
2. **The cause is in this package's 9x resume path**, not in the sleep
   entry: the CM's re-enumeration of the tunnel port right after the
   resume, before Windows 98 SE has restored that bridge's configuration.
   A fix to design (record 22 section 11.1.6 with record 21 section 6.1),
   not made here: on 98 SE and ME, after a resume, hold the member's
   re-enumeration until the bridge reads its secondary bus number again
   (Windows' restore), within a bound; or have the CM restore the bridge
   header it read at the suspend when the bridge reads reset after it.
   Either then needs these legs again (with the 0E answered first).
3. **Without a drive (1n1)** standby works at 1 to the desktop, the
   bridge restored; the post step met the 0E.
4. **The clean-base install**: every installed driver is the package's.
   On a base that has never seen the controllers the install ends with
   exit 4 and the wizards finish it (with the Windows CD for `usbd.sys`
   and `C:\XHCI98` for the "xhci98 xHCI Host Controller Disk"), as its
   own printed steps say.
5. **48.42's replacement holds**: the current installer replaces the
   2.0-era `XHCI98.SYS` (114,704 bytes) on tbs5's old image.
6. **The fatal 0E** (`0187:BFF9DFFF`) came in four of the six later legs,
   each time a DOS box started after a resume (host CPU 55 to 99 %),
   never before a standby; recorded for worker boot0e2's question.
7. **The shutdown hang** (black text page) came twice on this topology
   with `xhcicm.sys` loaded and no standby before it (`b0` after a
   `regedit`, `old1` after the install), never after a standby leg.

**No code change, no commit** (branch `w48-tbs6` at `ddf03ada`, no
commits of its own).

**Not run** (task 3): S.8 at 0 and S4 on the chain, the chain's first
shutdown after a standby; the 0 legs past 0a1; the no-enclosure legs past
1n1.

Evidence: `E:\xhci98-vm\p48\tbs6\` (`s0` the clean install, `old1` the
old image, `b0`, `b1`, `b2` the legs, `c0` the control: `serial-*.log`,
`cmdline-*.txt`, `pci-*.txt`, screens, `pids.txt`), every QEMU start and
stop in `pids-all.txt`; scripts `tools\go98.ps1`, `inst98.sh`,
`leg98.sh`, `setv98.sh`, `setr98.sh`, `sd98.sh` (new), `sby98.sh`
(tbs5's); `xfer\G9.BAT` (`chk` and `pre` added), `xfer\VR.REG`.

## 48.35 (the 9x resume fix: the re-enumeration waits for Windows' restore of the tunnel port) - worker tbs6 (2026-11-10)

The coordinator's go-ahead (option (a), "hold the member's re-enumeration
until the tunnel bridge reads its secondary bus again, within a bound").
Branch `w48-tbs6` reset onto `3.4.3.0` at `bec27306f` (it had no commits).

**Design** (record 21 section 6.1.3, new; record 22 section 11.1.6 item 5
and section 11.1.13): at the suspend (`CmReenumSuspend`, the CONFIGMG
route) each active member whose bridge reads a secondary bus is marked and
the worker put asleep; every end of the NHI's resume (`CmReenumResumed`,
`CmReenumResumeFailed`) wakes it and starts a 20 s bound. A marked member's
operation reads the bridge's bus numbers first and is parked (owed, not
failed, taken again at each 200 ms tick) until they are back (restored),
the bridge is unread, the controller is lost or the bound passes
(timeout: today's behaviour); logged as `cm.reenum.9x.resume-wait` and
`.ms`. A mark with no operation is resolved on its own each pass. The
member's chain requests and record 28 no-IRQ work wait with it (its time
credited); an operation already running when a suspend marks it stops at
its next admission (re-enumeration admission, numbering go callback),
owed. A fresh 9x Stor98 exposure that finds no secondary bus arms the
late scan for 30 s, so the UP is published once the bus is back.
Reviewed: design two rounds (pre-sleep marks consumed, Stor98 UP not
republished, worker blocked; then no-IRQ work), code four rounds (no-IRQ
continuation, in-flight operations, chain numbering owed), approved at
`b41f05018`.

Commits: `54886ead6` (design, code, host vectors `test_cm_ports`
`test_resume_wait`), `614d6a6df`, `edeb6475f`, `b41f05018`.

**Build: blocked by the host.** The gated `release` x86 run of the
reviewed tree (`all.cmd r1`, slot `gated.4`, started 15:54) stalled in
its host tests: `test_cm_live.exe` started 16:12:01 and never ran (0 CPU,
no image path), as did other workers' test executables started in the
same second (`test_func`, `test_membuf`); after 33 minutes the test was
killed, and the runner `cmd.exe` (230536), killed too, stays in the
process table (unkillable, apparently inside process creation), so
`build-steps.ps1` waits and the slot stays taken. No driver of this
branch is built, so the legs (three 98 SE standbys at 1 with the ORICO,
one at 0, ME once) are not run. Kit for them ready: `xfer\UPD.BAT` (the
new `xhcicm.sys` from `xfer\NEW\`, CMP98, SLPW), `tools\leg98b.sh`
(no DOS box after the resume: `CMP98.EXE` compares and writes, `XHCITR
/report` read at the next start), `tools\lme.sh` (ME, `SLPW.EXE s3/off`).

## 48.35 (the 9x resume fix, legs after the host reboot) - worker tbs6 (2026-11-10, evening)

**Builds.** Gated `release` of `b41f05018` (`all.cmd r2`,
`XHCI98_SELFTEST_SKIP_OK=1`): x86 `BUILD + GATES PASSED (release)`
(`E:\xhci98-scratch\w48\tbs6\gated-x86-r2.log`; host tests all 0 failures,
`test_cm_ports` 393 checks), amd64 likewise (`gated-amd64-r2.log`).
`xhcicm.sys` x86 504,660 bytes, SHA-256 `fff409e41d9dfa35...222d`; amd64
`b59311269c810d02...0f06`.

**Follow-up `0fbaffb78`** (on `b41f05018`, asked for at the merge review): two
suspend races closed. (1) A suspend that marked a member between its
no-IRQ pass's check and an operation's admission let the operation run
against the reset bridge, and its result was consumed: the admission
(`cmWkNiAdmit`, `CmPortsNiAdmit`) now reads the mark under the gate's lock
and holds the operation, neither made nor failed, outstanding in the
member's state; after the wait `CmNiAgain` hands it out again (same id,
clock restarted) before the pass's walk, or takes it as not made when its
attempt passed its deadline meanwhile. (2) A resume check that dropped the
lock for its bridge read could erase a later standby's mark: an epoch kept
with the marks (`CM_PORTS_RWS`) moves at each suspend and at each resume
that ends a sleep, and the check clears a mark only through `CmPortsRwClear`
with the epoch it read. Record 21 section 6.1.3 item 3 updated. Host vectors:
`test_cm_ports` `test_resume_race` (422 checks), `test_cm_noirq` (held
operation handed out again; past its deadline taken as not made; 528
checks), 0 failures. Reviewed and revised once (the replay past a
deadline), then approved. Gated `release` x86 of `0fbaffb78`: `BUILD +
GATES PASSED` (`gated-x86-r3.log`), `xhcicm.sys` 505,364 bytes,
`6e93cb19ed0b04c5...f07c`. Gated `qemu` x86, built for the debugcon leg
(`gated-qemu-x86-r3.log`): `objchk_qemu\i386\xhcicm.sys` 506,633 bytes,
`13a68c835d441b1e...99b2`.

**Vehicle.** The same as the earlier 48.35 legs: 98 SE on `pc`, one processor,
512 MB, S.1's topology with the ORICO at power-on, the pinned emulator
`asm4242-20261025-2230` (`tools\go98.ps1`), QMP 47850/47851, at most two
guests. Base `u0` = the clean install `s0` plus `b41f05018`'s
`xhcicm.sys` (`xfer\UPD.BAT`); `u1` = `s0` plus the qemu flavour of
`0fbaffb78`. `XhciCmSleepEntry` 1 (the default). The host also ran other
workers' guests (15 QEMU processes, CPU 79 % at 18:50).

| Leg | Base, overlay | Driver | S3 | After the wake | Bridge 00:05.0 | The drive |
|---|---|---|---|---|---|---|
| f1 | `u0`, `f` | `b41f05018` release | yes, 3 s | desktop 20 s after; "New Hardware Found: PCI PCI-to-PCI Bridge", "locating the software" 3 min after | secondary bus 0 from the wake to 8 min after | not read: no program started after the resume (`CMP98.EXE` never reached COM1); fatal exception 0E at `0187:BFF9DFFF` at the third try, then a black desktop; quit |
| f2 | `u0`, `g` | `b41f05018` release | yes, 1 s | desktop 20 s after; "New Hardware Found: PCI Universal Serial Bus" | 0 to 7 min after | not read: neither `XHCITR` nor `CMP98` ran; black desktop; quit |
| q1 | `u1`, `h` | `0fbaffb78` qemu (debugcon) | yes, 2 s | desktop 20 s after; "New Hardware Found: PCI Universal Serial Bus" | 0 to 8 min after | not read: fatal exception 0E at `0187:BFF9DFFF` at the second launch; quit |
| c1 (control) | `c0` (clean, no package), `c1` | none | yes, at once | desktop 20 s after; no wizard after the wake (4 min, screenshots every 30 s) | **secondary bus 1** at the first reading (24 s after the wake) and throughout | - |
| rA3 (control) | `b2` (`XhciCmReenum` 0), `r3` | `ddf03ada` release | yes, 2 s | desktop 24 s after, then the guest reset (QEMU exited, `-action reboot=shutdown`), as `0a1` did | secondary bus 1 before the standby | - |
| m1 (ME) | `me1` | `b41f05018` release | - | void: two leg scripts drove the guest (the first one's launch had failed on a port still held by the update guest); quit | - | - |

Not run for lack of time: the standby at 0, a third standby at 1, the ME leg.

**The CM's answer (q1's debugcon).** At the suspend
`cm.reenum.9x.resume-mark=00000001`; at the resume the tunnel is rebuilt
(`u4.pcie.up=00010809`), `cm.nvlink.up.no-bus=00000028`,
`cm.reenum.9x.resume-armed=00000001`. Then nothing touches the member
until `cm.reenum.9x.resume-wait=00002802`, which is **timeout**, after
`cm.reenum.9x.resume-wait.ms=00004E79` (20,089 ms): Windows had not put
the bridge's bus numbers back within the 20 s. The member's operation
then ran as before (`cm.brnum.9x.held=00002801`: its old child devnode
still in the tree, so no numbering; `cm.reenum.9x.reenumerate=00002800`;
the walk found nothing, `noirq.walk.node=FFFF0001`), and
`cm.nvlink.up.late-none=00000028`. The bridge read secondary bus 0 for the
rest of the leg (8 minutes). So the wait works as designed, but its
premise does not hold on this base: Windows 98 SE does not restore the
bridge even with the re-enumeration's operation held off for 20 s. The
re-enumeration at +190 ms was not what turned the restore off.

What `XhciCmReenum` 0 (`rA1`, `rA2`) changes besides that operation:
no re-enumeration runs at boot either, so no PCI child devnode is
made below the bridge (the drive comes through Stor98's own exposure).
Every leg in which Windows restored the bridge had no child devnode below
it: `1n1` (no tunnel), `c0` and `c1` (no package), `rA1` and `rA2` (no
re-enumeration). Every leg with one did not restore it: `1a1` to `1a3`,
`f1`, `f2`, `q1`. The working hypothesis for the next phase: on resume
98 SE does not restore a PCI-to-PCI bridge whose header was reset while
a child devnode of it stayed in the tree. The New Hardware wizards that
come up after the wake (the bridge, "PCI Universal Serial Bus") come only
with the package and the tunnel, not without them (`c1`).

Every QEMU start and stop is in `E:\xhci98-vm\p48\tbs6\pids-all.txt`;
q1's debugcon is in `E:\xhci98-scratch\w48\tbs6\debugcon-q1.log`.

### Merge (w48-tbs6)

Not merged; parked at `b41f0501` and `0fbaffb7`: on 98 SE the drive did not come back. 98 SE and ME standby with a drive attached stays a known limitation; left to "Planned - Left by `3.4.3.0`" (TBSTBY9X.1).

## 48.35 (S.1 to S.3, S.5, S.6, the chain at 0 and S4) - worker tbs7 (2026-11-10)

Branch `w48-tbs7` from `3.4.3.0` at `89715976`. No commits. No code was changed: the legs ran on the gated
builds of that commit.

**Builds.** Gated `build-driver.cmd release` x86 (`gated-x86.log`, from before the host reboot) and amd64
(`gated-amd64.log`, 2:21, `XHCI98_SELFTEST_SKIP_OK=1`). Both `BUILD + GATES PASSED (release)`, `dirty=0`.
x86 `xhcicm.sys` `40142be7...`, `xhci98.sys` `7239a4eb...`, `stor98.sys` `0124ff59...`. amd64 `xhcicm.sys`
`3b98a611...`, `xhci98.sys` `442cfed4...`, `stor98.sys` `6ff95201...`. The kits come from `make-package.ps1`
plus `ConvertTo-FamilyLayout`: x86 in `E:\xhci98-vm\p48\tbs7\xfer\KIT`; x64 in `xfer64\KIT`, with unsigned
catalogs made by MakeCat and the x64 drivers signed inside the guest with a throwaway certificate
(`SIGN7.BAT`).

Every leg runs `ONE.CMD` from `E:\xhci98-vm\p48\tbs7\xfer`. Each leg writes a file to every drive before the
sleep, reads it back and writes another after the resume (at t+40 s), and reads the device count and the
`XHCITR /report` counters. The leg then shuts down, and the shutdown is timed. Logs are in
`E:\xhci98-vm\p48\tbs7\<vm>\serial-<leg>.log`, and the sequences in `logs\legs-<vm>.log`.

| Leg | Vehicle | Value | Result |
|---|---|---|---|
| S.8 S4 | 7 x86 q35, chain emulator, `asm4242` host, dock + ORICO at power-on, no display (`c8`) | 1 | pass: Enter Sleep and Sleep Ready on 3 routers (`u4.sleep.entered=3`), hold passed and released on 0x200/0x208, both drives kept (D:, F:, files compare), 119 devices before and after, shutdown 14 s |
| S.8 S3 | same | 0 | pass: no entry, `cm.reenum.resumed=1`, both drives kept, 121/121 devices, shutdown 14 s (the guest sat about 3 min in S3, see note 1) |
| S.8 S4 | same | 0 | pass: both drives kept, 121/121 devices, shutdown 12 s |
| S.8 S3, the first shutdown after a standby (owed by tbs4) | same | 1 | pass: 3 routers entered and ready, both drives kept, 121 devices, shutdown 10 s after the leg |
| S.1 S3 | 7 x86 on `pc` (i440FX), `usb4-generic` + SHPC tunnel bridge, no pre-boot CM, ORICO at power-on, AHCI system disk, no display (`s1`) | 1 | **FAIL: the drive was gone at t+40 s.** Entry made (`u4.sleep.entered=2`, Sleep Ready 2/2), `cm.reenum.resumed=1`, then `cm.chain.remove`; volumes 1 -> 0, devices 107 -> 104. No STOP, no hang, shutdown 12 s. The file written before the standby was on the drive at the next cold boot. |
| S.1 S4 | same | 1 | pass: entry on 2 routers, drive and files kept, 109/109 devices, shutdown 14 s |
| S.1 S3 | same | 0 | **FAIL, as at 1:** no entry, `cm.reenum.resumed=1`, then `cm.chain.remove` 0x10028 and 0x10108; volumes 1 -> 0, devices 109 -> 106; shutdown 16 s. The guest sat about 4 min in S3 (note 1). |
| S.1 S4 | same | 0 | pass: drive and files kept, 109/109 devices, shutdown 12 s |
| S.3/S.7 S3 | 10 x64 q35, `asm4242` with its pre-OS CM, ORICO at power-on, virtio-vga + viogpudo (`x10`) | 1 | pass: entry on 2 routers, Sleep Ready 2, hold passed and released on 0x200/0x208, drive E: kept and written, 69/69 devices, `xhcimark` flushed E: at the suspend notice, shutdown 32 s |
| S.3 S4 | same | 1 | not read: the leg's cold start reached no command prompt in time (serial empty), and the guest was stopped at 19:47 for the cut |
| S.6 S3 | 10 x64 q35, `adl` (Intel ADL, software CM), ORICO at power-on, virtio-vga (`x10a`) | 1 | **unread, possible hang:** resumed (`--- resumed`, t+40 reached). The post-resume device listing printed the code-28 line, then `wmic diskdrive get Model,Status` had not returned 10 min later. QEMU was running and the display output was off. The guest was stopped by PID; the drive state is unread. |

Note 1: `seqg.sh` with a `setv` step lost its mode because `lib.sh`'s `waitE` reused the variable `m`. The two
S3-at-0 legs then waited for a hibernation, and each guest was woken by hand (`system_wakeup` at 19:21:17), after
3 to 4 min in S3 rather than 45 s. Fixed in `lib.sh` (`wm`).

**S.1's finding (both values).** The pre-sleep report shows the chain at the SHPC bridge's port 0x28 at
start: `cm.chain.rel=01000028`, `rel.new=1`, `attach=01010028`, `start=00010028` and `00010108`. After the
resume, at 1, the report shows `cm.reenum.hold.passed=00000028`, `cm.resume=1`, `cm.reenum.resumed=1`,
`cm.reenum.hold.released=00000028`, then `cm.chain.rel=00000028` with `rel.new=00000000`, then
`cm.chain.remove=00010028` and `00010108`. At 0 it shows the same `resumed` and the same two removals. So on
resume the connection manager read the relation at bridge port 0x28 with its top byte 00 (01 at start) and
removed the two devices of the chain under it, at both values. The entry is not the cause: the loss is
the same at 0, which is the 3.4.2.0 behaviour. The resume path on the i440FX SHPC bridge is the suspect,
since the CM finds the port's relation gone after the standby. Hibernation keeps the drive, because the cold
start enumerates it again. Not read: whether the drive comes back later than 40 s, and the full CM log.
The repeat on a fresh overlay (`s1b`, with `tools\s1late.sh`: 150 s more, then the full
`XHCITR /report` files typed to COM1) was installed but stopped before its leg when the cut was brought
forward. The full reports of these legs are in the `s1` guest at `C:\T\REP-A-POST.TXT` and
`REP-C-POST.TXT`. The fix is next-phase work (coordinator, 2026-11-10).

**Not run (left):** S.1's repeat and its late check; S.2 (no pre-boot CM, the enclosure added over QMP); S.3 on
7 x64; S.3 S4 on 10 x64; S.5 (Titan Ridge, 7 x64); S.6 S4, and S.6 S3 read to the end;
the bench.

Tool notes for the next worker: `TaskStop` on a background Bash leaves its `sh` children running, so kill
them by PID. On a CLICK install, 7 x86 needs std VGA, because the prompt is clicked from screenshots.
`SIGN7.BAT` run through `G.CMD cmd` did not run on 10 x64, so type it directly. On 10 x64 the desktop
needs more than 200 s after a cold start (use `BOOTWAIT=330`).

### Merge (tbs7)

Reading only (no commits). S.1's loss left to "Planned - Left by `3.4.3.0`" (TBSTBYNOPRE.1); S.6 being read again (TBSTBYLEGS.1).

## 48.6 (continued) XHCIPERF on 98 SE and ME: the close while a request is out (2026-11-10, w48-tools2)

Branch `w48-tools2` from `3.4.3.0` at `89715976`. Read from the code (`xhciperf\xhciperf.c`)
against record 29 sections 4.4 and 6.2, then changed.

**What 3.4.3.0 did.** Two kinds of request can be out on 98 SE and ME:

- **A worker's file request.** 9x file I/O is synchronous (`worker_sync`; `CancelIo` is
  looked up but has nothing to cancel there), so Stop and a close's stop wait for the request
  in flight. The window stays and answers; the status bar says "Waiting for the disk to finish
  the last request"; the run ends, deletes its file and the window closes once the request
  returns. A further close only repeated that line, while section 4.4 promised a text naming
  the system's own task list.
- **A Stor98 counter read or search (ASPI).** `aspi_exec` waits 2 s, then sends
  `SC_ABORT_SRB` and waits for ASPI to give the SRB and buffer back, with no bound (the
  request owns them; freeing them under ASPI is not safe). At the close the window was
  destroyed first and the process then waited in the loop after its message loop
  (`g_aspiOut`, `g_aspiThreads`), so a request ASPI never gave back left a process with no
  window, visible only in Ctrl+Alt+Del's list.

Neither request can be made cancellable by the tool (9x has no file cancel; the ASPI abort
is already sent). A bounded wait that then exits would free memory ASPI may still write, so
it was not taken. Instead (`cf854e2a`):

- a close with an ASPI request or thread out keeps the window, controls greyed, status
  "Waiting for Stor98 to answer before closing", sends no further ASPI request
  (`g_aspiClosing` from the close), and closes at the first 250 ms tick after both counts are
  0 (the timer runs until the close itself kills it, so a request counted again meanwhile is
  waited for too; if `SetTimer` fails the old path is kept); Start is refused meanwhile;
- on 98 SE and ME a further close while the close's stop waits for a worker says "Still
  waiting for the disk; Windows 98/ME cannot cancel it. Ctrl+Alt+Del ends XHCIPERF.";
- README: "On Windows 98 and ME a request sent cannot be cancelled: Stop, and closing the
  window, wait for the disk to answer it."; record 29 draft 13 (sections 4.4, 6.2).

NT is unchanged (the 2026-11-09 reading above: the close waits for every cancelled or
completed request; no ASPI there, so the new wait never arms).

**Review.** Reviewed and revised: round 1 found the deferred close latched with no timer if
`SetTimer` failed (fixed: the old path kept); round 2 no material findings.

**Builds.** Not finished: the gated `build-driver.cmd release` x86 and `-amd64` of `cf854e2a`
were started three times and stalled each time on the host-wide fault of 2026-11-10 afternoon
(freshly started processes - `rc.exe`, `test_membuf.exe`, `qemu-img.exe`, `MakeCat.exe` - sitting
at 0 CPU from creation, other workers' builds alike); stopped, slot released, owed after the
host's restart. `xhciperf\build.cmd` alone (MSVC 6 and WDK 7.1 amd64, /W3 /WX) built both
executables cleanly at the reviewed code before the stall.

**Legs.** None on 98 SE/ME: a stalled ASPI or file request has no vehicle in the emulator
(Stor98's READ BUFFER is answered from memory; a stalled block request would need
`blkdebug` suspend under a Stor98 NVMe on 98 SE). Owed: the close's wait read on 98 SE (smoke:
a `/run ... /exit` and a window close during a run, both ending normally).

## 48.17 (continued) The USB drive notice across a standby: the vehicle (2026-11-10, w48-tools2)

**What can stand by.** The q35 bases cannot: tbs3's 48.35 readings (fixed SeaBIOS) found the
98 SE q35 base refusing standby without this package ("Your computer cannot go on standby because
a device driver or program won't allow it"; its disk runs in MS-DOS compatibility mode) and the XP
q35 base "System standby ... Not Supported" (also with Cirrus). Read here on QEMU's `pc` machine
(i440FX, the pinned emulator `asm4242-20261025-2230`, stock SeaBIOS, TCG, one vCPU):

| system | leg | result |
|---|---|---|
| 98 SE (overlay of `vm\t26\win98-clean.qcow2`, no USB controller; `w98c`) | Start, Shut Down: "Stand by" offered; chosen | QMP `suspended` within 5 s; held 20 s; `system_wakeup`: the desktop back (`w98c\a1.png` to `a3.png`); Shut Down powered off. **The vehicle stands by and resumes** |
| XP SP3 (overlay of `vm\winxp.img`, its autologon state, 1024x768; `xpa`) | Start, Turn Off Computer | "Stand By" greyed (`xpa\a2.png`), as run-36's 36.28 and run-45 read (XP's display drivers in QEMU refuse S3). **XP's standby is not readable in the emulator**; its hibernation is offered on `pc` (36.28) and is the nearest reading |

**Not read** (stopped for the host restart): the 98 SE leg itself - a fresh `pc` overlay with
`qemu-xhci`, the package installed, `StorageNotice` 1, a stick plugged, Stand by with the stick
attached, wake; and XP's hibernation with a stick attached. Evidence:
`E:\xhci98-vm\p48\tools2\` (`w98c\`, `xpa\`, `pids.txt`, `tools\go98.ps1`, `tools\goxp.ps1`).

## 48.6 (continued) The timer failure; the builds and the 98 SE smoke (2026-11-10 evening, w48-tools2)

**The fix.** The merge review found that with `SetTimer` failing the close still destroyed the
window and waited with none, the case 48.6 removes. `fa5c1193`: whenever an ASPI request or
thread is out the close keeps the window, controls greyed (`g_closeWait`), whether or not the
timer was made (`g_closeTimer`); with no timer the status bar says "Waiting for Stor98 to answer;
close again later" and each further close tries the timer again; the window is destroyed only once
both counts are 0. Record 29 section 6.2 says so. Reviewed: no material findings.

**Builds** (after the host restart, `XHCI98_SELFTEST_SKIP_OK=1`):

| commit | build | result | log |
|---|---|---|---|
| `cf854e2a` | `build-driver.cmd release` x86 | BUILD + GATES PASSED, 3:02 | `gated-x86d.log` |
| `cf854e2a` | `build-driver.cmd release -amd64` | BUILD + GATES PASSED, 12:34 | `gated-amd64d.log` |
| `fa5c1193` | `build-driver.cmd release` x86 | BUILD + GATES PASSED, 2:54 | `gated-x86e.log` |
| `fa5c1193` | `build-driver.cmd release -amd64` | BUILD + GATES PASSED, 2:18 | `gated-amd64e.log` |

`XHCIPERF.EXE` `a41616e43e80ce9bd6a2911db132303d9b0abe2064acc76ad24d532762a2ed1a`, `XHCIPF64.EXE`
`62341d0ed7588cd01b90660090b152452c83d403a48e0c7ce7709384be2021fc` (`fa5c1193`).

**98 SE smoke** (`w98a`: a fresh `pc` overlay of `vm\t26\win98-clean.qcow2`, `qemu-xhci`, the x86
release package of `cf854e2a` installed with `INSTALL.BAT /console /yes /nopause`, XHCIPERF.EXE of
`fa5c1193`):

| leg | result |
|---|---|
| `XHCIPERF /run /folder C:\ /size 32 /time 5 /test seq,64,1,1 /ops both /report C:\PF.TXT /exit` | exit 0; report written (2.8 MB/s write, 2.9 read, 3 passes each; Stor98 counters "not reported by this disk's driver", C: is IDE); no `.TMP` left in C:\ (`serial-a.log`) |
| the same without `/exit`, the window closed during the run (Alt+F4) | "Stop the test and close?" (`p11.png`); Yes: the window gone within 1 s, no `.TMP` left (`p12.png`) |

The ASPI-outstanding close itself has no vehicle (a Stor98 request that ASPI does not give back);
owed as before.

## 48.17 (continued) 98 SE standby with a USB drive attached (2026-11-10 evening, w48-tools2)

System: 98 SE on QEMU `pc` (i440FX, `asm4242-20261025-2230`, stock SeaBIOS, TCG, one vCPU, 256 MB),
`qemu-xhci` (`p2=4,p3=4`), overlay `w98a` of `vm\t26\win98-clean.qcow2`; the x86 release package of
`cf854e2a` (drivers as 3.4.3.0's tip; `SHA256.TXT` in `tools2\xfer`). Install: `INSTALL.BAT /console`
exit 4 (wizards owed), the controller's wizard ran from `C:\XHCI98`; the image has no 98 SE CAB
folder, so `usbd.sys` and `usbui.dll` were skipped and copied from the 98 SE files afterwards
(`D:\WIN98`), then a cold start. `HKCU\Software\xHCI98\XHCITR\StorageNotice` = 1 (exported, `serial-a.log`).
Stick: `usb-storage` on `xh.0` port 1, a vvfat folder (`sticks\A`). Stand by from Start, Shut Down
(Alt+S, Up, Enter); held 20 to 35 s; `system_wakeup`; screendumps 5, 10, 15, 20, 30, 60 s after.

| leg | result |
|---|---|
| plug at the desktop (first plug) | "USB drive connected" / "QEMU USB HARDDRIVE is connected." during the mass-storage wizard (`s04.png`); E: mounted |
| standby with the stick attached, sample 1 | QMP `suspended` within 5 s; after the wake no xHCI98 balloon at 5, 10, 15, 20, 30 or 60 s; E: kept; Explorer opened E:\ about 10 s after the wake by itself (Windows' AutoPlay, as at a plug) (`s1-w*.png`). **Pass** |
| unplug at the desktop after it | "USB drive removed" / "QEMU QEMU HARDDISK (E:) was removed." once (`u-3.png`); nothing more at 15 s |
| replug | "USB drive connected" / "QEMU QEMU HARDDISK (E:) is connected." (`r-10.png`) |
| unplug during standby, sample 1 | resume silent at 5 to 60 s (as designed, item 6), the drive's tray icon gone (`s2-w*.png`). A console program then (`command /c dir E:\` from Run) gave fatal exception 0E at `016F:BFF9DFFF` and the shell died (`t2-e.png`, `t2-k*.png`): the known QEMU 98 SE post-standby fault (lessons, "Windows 98 SE does not resume reliably from Stand by in QEMU": a console program after a standby faults there with no USB controller at all), not this package |
| cold start, plug | "USB drive connected" (`v02.png`) |
| standby with the stick attached, sample 2 | as sample 1: no balloon at 5 to 60 s, E: kept, Explorer opens E:\ (`s3-w*.png`). **Pass** |
| unplug during standby, sample 2 | resume silent at 5 to 60 s (`s4-w*.png`); Win+E: My Computer lists A:, C:, D:, no E: (`v05.png`) |
| replug | "USB drive connected" (`v06.png`); Shut Down powered off |

So on 98 SE a standby with a USB drive attached is announced by nothing, the 15 s quiet holding;
the drive stays mounted; a drive taken away during standby is dropped silently. Note: the first
plug's balloon names the device by its USB product string ("QEMU USB HARDDRIVE"), the later ones by
the disk's name with its letter ("QEMU QEMU HARDDISK (E:)").
Evidence: `E:\xhci98-vm\p48\tools2\w98a\`, `pids.txt`.

### Merge (w48-tools2)

Merged into `3.4.3.0` at `a4addac7` (`fa5c1193`, 2026-11-10).

## 48.33 Staging off by default; the legs at 0; 98 SE's window mapping left owed (2026-11-10, worker stage)

Branch `w48-stage` from `3.4.3.0` at `89715976`, rebased onto `6027263d` at the coordinator's request. Commits: `a5f604f8` (qemu flavour only: where a staging copy reaches the client's bytes, and `XhciQemuStageFault` bit 1), `d2720005` (staging off by default: `XhciDmaStaging` written at 0, absent read as 0; owner, 2026-11-10).

**The default** (owner, 2026-11-10, passed on by the coordinator mid-run): 3.4.3.0 ships with staging off - the owner chose not to ship a speed cost not yet measured on real hardware (the emulator's 9 to 19 % on 98 SE and up to 73 % on 7 x64 above). `XhciStagingOn` now returns 1 only for a DWORD read as anything but 0; absent or a DWORD 0 is 0 (the direct route, the map pump as in 3.4.2.0, which 48.33 restored beside staging). Both INFs write `XhciDmaStaging` 0 (`0x00010003`, don't overwrite) and their comments say so; the INF gate's row (default 0 and its reason), the five footprint rows, the self-test vectors of both tables and the anchor that carries the INF text follow; tuning.md, build-and-test.md, record 13 section 11.11 ("The switch") and record 17 section 4.13.8 state the default and why. Host vectors: `test_stage` `test_switch` (absent: 0; absent ignores the value; 1, 2 and all ones: 1; 0: 0).

**The qemu flavour's reading of the copies** (`a5f604f8`; the release image unchanged): each staging copy's source counted - `stage.direct.buffer` (a URB's `TransferBuffer`), `stage.direct.mdl` (an MDL's `MappedSystemVa`), `stage.direct.low` (an address below 0x80000000, which is valid only in one process's context while a copy runs in whatever context its DPC finds) - the first addresses of each printed on 0xE9 and the counts at each stop; and `XhciQemuStageFault` bit 1, on 98 SE and ME only, sends an MDL's copy through a window even when the MDL carries a system address (the driver's own window and probe MDLs stripped of `MDL_MAPPED_TO_SYSTEM_VA` and `MDL_SOURCE_IS_NONPAGED_POOL` after `IoBuildPartialMdl`), so that `MmMapLockedPages` and `XhciQemuStageMapProbe` are reached by mass storage there. Reviewed: no findings. Not run on a guest: the coordinator dropped the 98 SE mapping investigation for this release.

**98 SE's mapping failure: owed to the next phase.** What is known (pci6's run above, and the code): on 98 SE with `XhciDmaStaging` 1, `USBSTOR.SYS` (NUSB33) hands the bus only buffers that already have a system address, so every copy goes through that address (`hcdStageDirect`) and no window is ever mapped; the controller still writes only into the driver's own buffers, so staging does protect 98 SE's mass-storage transfers - it does not fall back to direct DMA, silently or otherwise. Not read: whether every such address is a system address valid in any process context (an address in a Win32 process's private arena would be copied in the wrong context at completion; the copy-compare by COMMAND.COM passed, a Win32 copy did not run); what NTKERN's `MmMapLockedPages` does when it cannot map (NULL or a fault), which only a client passing an MDL without a system address, or bit 1 above, reaches. Next: the 98 SE leg with `XhciDmaStaging` 1, the qemu image of `a5f604f8` or later, `XhciQemuStageFault` 2 and `XhciQemuStageMapProbe` 32768, COMMAND.COM and XCOPY32 copy-compares (`k98\X.BAT`), reading the `stage.direct:` lines and the probe's outcome.

**Builds and legs: not done in this run.** Reviews of both commits converged (no findings). `check-inf.ps1` passed on both INFs by hand. The gated x86 and amd64 builds of `d2720005` did not run: each stopped at the host tests because their log files were held by orphan processes of an earlier aborted build that could not be ended during the host's stall; the host restart of 18:30 ends them. Guests prepared and shut down, each with the `3.4.2.0` package installed on a fresh overlay of its base and ready for the image swap (`E:\xhci98-vm\p48\stage\s98`, `w2k` with a BOT stick and a UAS disk, `xp` likewise, `w7` likewise, its kit copied to `C:\KIT` and signed in the guest with a throwaway certificate); kits `E:\xhci98-scratch\w48\stage\k98`, `k2k`, `kxp`, `k7` (copy-compare, hot unplug, `DE.BAT` disable and enable under a copy loop with `devcycle` / `devc64`, the 2000 stress `S.BAT`). Owed: the gated builds; the legs at 0 (copy-compare, hot unplug, disable and enable with transfers running) on 98 SE, 2000, XP and 7 x64; one leg at 1 on 7 x64; the 2000 stress; UAS and audio together on 98 SE; the bench. QEMU pids this run (each in its folder's `pids.txt`): 226908 (never initialised, stopped by its PID), 213016 (s98), 201404 (w2k), 224568 (xp), 216604 (w7); none left running.

### 48.33 after the host restart: the builds and the legs at 0 (2026-11-10, 18:10 to 19:40, worker stage)

Gated builds of `d2720005` (`XHCI98_SELFTEST_SKIP_OK=1`), log `E:\xhci98-scratch\w48\stage\gated-both-4.log`: x86 `BUILD + GATES PASSED (release qemu)`, RC 0; amd64 RC 0. SHA-256: x86 release `0e4063b72a94821b16e64abb9f94e02727b8eda5bd666b95da37cc2f9ab472bc`, x86 qemu `ef548b5e6bd6b847496cdf43f6595a66d897c3bee13eeef2d0374f55ec8711de`, amd64 release `307cbbb91a33d3df1c27775e233580bd192a7b79d52469b0ff23db7593da4596` (the copy signed in the 7 x64 guest with the throwaway certificate: `ebda1760...d61773`), amd64 qemu `dcbaee56f4349c7acf96f692719ecf7aa3c74a8ff326189743db82a476e78824`.

Legs at 0: on each guest the release image swapped in over the installed `3.4.2.0` package, whose INF writes no `XhciDmaStaging`. That is the "absent reads 0" path, and on 7 x64 `reg query` showed the value absent from the controller's key (`Class\{36FC9E60-...}\0000`). Pinned emulator `asm4242-20261025-2230`, q35 overlays, `-display none`.

| System | Step | Result |
|---|---|---|
| 98 SE (1 vCPU, 512 MB) | image | `XHCI98.SYS` 365,554 bytes (REL7) after `SW.BAT REL7`, cold start |
| | copy-compare (`C.BAT`, hot-added BOT stick, E:) | 3/3 PASS (after the USBSTOR wizard for the hot-added stick) |
| | hot unplug, disable and enable | not run: left (COM1 held by an earlier window made the first `U.BAT` stall; the second finished its 8 copies before the unplug was sent; then the cut stopped new legs) |
| 2000 SP4 (1 vCPU) | image | `SW.BAT REL7`: 365,554 bytes in place, cold start |
| | copy-compare (`C.BAT`, E: and F:) | 6/6 PASS; again 6/6 PASS after the disable and enable below |
| | hot unplug (stick removed by `device_del` during a 30-iteration copy loop to both disks) | every later iteration copied to the remaining disk; Windows' own "Unsafe Removal" and "Delayed Write Failed" boxes for the removed stick; no hang |
| | disable and enable (`devcycle disable`, then `enable` 44 s later, during a 30-iteration copy-compare loop on F:) | the disable reached the loop mid-copy (iteration 9: "The specified network resource or device is no longer available"), then "cannot find the drive" until the enable; copies and compares passed again 3 s after the enable; `disable ... ok (0)`, `enable ... ok (0)` |
| | (first boot, after the unplug and re-plug) `DE.BAT E devcycle` | no "DE devcycle exit" line reached COM1, and a later `devcon status` printed nothing for over 60 s. COM1 contention with a loop running beside it explains the missing lines but not the silent `devcon`. Not reproduced on the clean boot above (no unplug before it); the guest was stopped by QMP quit. Read again after an unplug and re-plug: left |
| XP SP3 (2 vCPU, 1 GB) | image | `SWX.BAT REL7`: FC "no differences", cold start; `sc query xhci98` RUNNING |
| | copy-compare (`C.BAT`, F: = UAS) | 3/3 PASS; again 3/3 PASS after the enable |
| | hot unplug (BOT stick D: removed by `device_del` during a 30-iteration loop to D: and F:) | D: "cannot find the drive" from the next iteration on, F: copied all 30; no hang; stick re-plugged and enumerated again (port 3) |
| | disable and enable (`devcycle`, during a copy-compare loop on F:) | disable `ok (0)` after 6.5 s with a copy in flight, which completed and compared; then "cannot find the drive"; enable `ok (0)`; `devcycle list USB` showed the root hub, both devices and the disk |
| 7 SP1 x64 (2 vCPU, 2 GB) | image | REL7 signed in the guest, `SW7.BAT REL7`, restart; `N.BAT`: RUNNING, the signed hash, controller and root hub OK, no problem code but `ACPI\QEMU0002` (28, the base's) |
| | copy-compare (`L.BAT D 3`; D: = the UAS disk, the BOT stick offline because both overlays share one disk signature) | 3/3 PASS |
| | hot unplug (UAS removed by `device_del` during a 20-iteration copy loop to D:) | from the next iteration on, "The system cannot find the file specified"; no hang. The stick's unplug did not disturb D:'s copies. The re-plugged UAS came back at 12 Mb/s with no disk (the emulator adds `usb-uas` before its LUN), so it is not counted; the re-plugged stick enumerated as D: |
| | disable and enable (`devc64`) | with a copy loop holding a file open on D:, the disable returned `FAILED (13)`. The driver accepts every query-remove (`hcd_pnp.c`, `hcd_pdo.c`), so the refusal came from above it; the loop ran 30/30 PASS. With no file open: `disable ok (0)`, `enable ok (0)`, D: back |
| | leg at 1 | `XhciDmaStaging` 1 written to `Class\{36FC9E60-...}\0000` (`G.BAT` found no key: the 3.4.2.0 install's `InfPath` is `oem2.inf`), restarted, then stopped by the cut before any step: left |

Evidence: `E:\xhci98-vm\p48\stage\{s98,w2k,xp,w7}\` (`serial-c.log`, `serial-d.log`, `serial-b.log`, `b*.png`, `c*.png`, `d*.png`, `pids.txt`); copies in `E:\xhci98-scratch\w48\stage\*-serial-*.log`. QEMU pids this part (each in its folder's `pids.txt`): 12564, 31384 (s98), 27520, 38232, 37476 (w2k), 40092, 17704 (xp), 37228 (w7); none left running.

### Merge (w48-stage)

Merged into `3.4.3.0` at `c80e4324` (`d2720005`, 2026-11-10). What the legs left went to "Planned - Left by `3.4.3.0`" (STAGELEFT.1).

### 48.33 the leg at 1 on 7 x64 (2026-11-10, 19:44 to 19:57, worker stg7)

7 SP1 x64 (2 vCPU, 2 GB), copies of worker stage's `w7-b` disk, stick and UAS overlays (3.4.2.0 installed, the release image `307cbbb9...` signed in the guest, `ebda1760...d61773`, in place; `XhciDmaStaging` 1 in `Class\{36FC9E60-...}\0000`), cold start, emulator `asm4242-20261025-2230`, `-display none`, QMP 48910. Kit `E:\xhci98-scratch\w48\stg7\k` (`A.BAT`: value, state, `XHCITR64 /dump`; `X.BAT`: disable, enable, copy-compare; stage's `L.BAT`, `N.BAT`, `devc64.exe`).

| Step | Result |
|---|---|
| value and image | `reg query`: `XhciDmaStaging REG_DWORD 0x1`; `xhci98` RUNNING, SHA-256 `ebda1760...d61773`; controller and root hub OK; only `ACPI\QEMU0002` (28, the base's) |
| staging on | `XHCITR /dump` note ring: `stage.switch=00000001`, `stage.pool.slabs=00000020`, `stage.pool.short=0`, `stage.control=1`, `stage.windows=4` |
| copy-compare (`L.BAT D 3`, D: = UAS) | 3/3 PASS |
| hot unplug (`device_del uas` during `L.BAT D 20`, after round 5) | rounds 6 to 20 FAIL at once (15 rounds in about 10 s), loop ended; no hang |
| re-plug (three ways: plain, paused, `port=2`) | `usb-uas` came back at 12 Mb/s with no disk each time (the emulator artefact), not counted |
| disable and enable (`devc64`, no file open) | `disable ... ok (0)`, `enable ... ok (0)` |
| copy-compare after the enable (`L.BAT D 3`) | 3/3 PASS; D: was the BOT stick then (`USBSTOR`, port 1: the UAS without its LUN), staging still on (`stage.switch=00000001` in a second dump) |

Evidence: `E:\xhci98-vm\p48\stg7\a\` (`serial.log`, `s1-s3.png`, `u1.png`, `x1.png`, `pids.txt`); copy `E:\xhci98-scratch\w48\stg7\stg7-serial.log`. QEMU pid 33888, shut down from inside, exited.

### Merge (stg7)

Reading only.

### 48.33: 98 SE's hot unplug and Device Manager disable at 0 (2026-11-10, 19:40 to 20:08, worker stage, the owner's request)

98 SE, REL7 (x86 release, 365,554 bytes) over `3.4.2.0`'s package, `XhciDmaStaging` absent; BOT stick hot-added (E:). The loop: kit `LP.BAT` / `LP1.BAT`, 40 iterations of copy C:->E:, copy back, FC, each stamped with the time, results to `C:\LOOP.TXT`, typed to COM1 (`TL.BAT`) after its window closed.

| Leg | Result |
|---|---|
| Hot unplug during copies (`device_del` 25 s into iteration 1) | PASS. Windows' own blue "Disk Write Error: Unable to write to disk in drive E:" box, answered with a key. Iteration 1's copies reported copied and its compare FAILED, which is expected for a write cut off by the removal. Iterations 2 to 40 failed at once (0.2 s each: "Path not found", "Invalid drive specification"). The desktop answered. Re-plugged, enumerated without a wizard: `C.BAT` 3/3 PASS |
| Device Manager "Disable in this hardware profile" during copies (OK at 19:52:08, during iteration 2's copies) | PASS. Iteration 2 finished and compared (PASS) at 19:52:34, then the removal happened. Iterations 3 to 40 failed at once. The controller showed "This device is disabled (Code 22)", and the controller, root hub and USB Mass Storage Device all carried the red X. No hang, no restart asked. Re-enabled (box cleared, OK) with no restart asked: every devnode back with no mark. `C.BAT` after it 3/3 PASS. That run was after a cold start, because a stray keystroke of mine had shut the guest down through the Shut Down dialog first |

Evidence: `E:\xhci98-vm\p48\stage\s98\serial-d.log`, `serial-e.log`, `d*.png`, `e*.png`, `pids.txt` (pids 24412, 38496; none left running); copies `E:\xhci98-scratch\w48\stage\s98-serial-{d,e}.log`.

### 48.33 the 2000 disable read again (2026-11-10, 20:00 to 20:25, worker stg2k)

2000 SP4, `XhciDmaStaging` 0, a BOT stick, copies of worker stage's 2000 disks. The earlier run in which the disable printed nothing was not a hang: the kit's `devcon.exe` is a build 2000 cannot run, so it printed nothing and did nothing.

| Leg | Result |
|---|---|
| Disable and enable after an unplug and re-plug, read twice | No hang either time; copy-compare after the enable passes |
| Disable with copies in flight | 2000 defers a disable while a file on the stick is open, so the disable landed only between copies; the leg with a copy cut off by the disable is not read (STAGELEFT.1) |

Evidence: `E:\xhci98-scratch\w48\stg2k\r1-serial-a.log`, `r2-serial-a.log`, the kit `k2k`.

### Merge (stg2k)

Reading only.

## 48.1 (continued) `uac2`, `uac1`: 26.13 as revised under ME's, 2000's and NUSB's UHCI (2026-11-10, w48-uhci)

usb-wdm-free, branch `w48-uhci` from `main` `7071de4`, worktree
`E:\usb-wdm-free-wt\w48-uhci`. Not merged, not pushed. Docs only (no
driver change).

**Build.** aud2's staged x86 free images of `dca05e0` (the code of
`a27d075`, unchanged at `7071de4`): `uac2wdm.sys` `cf243b47...`
(`Uac2Build` 28 read in every leg), `uac1wdm.sys` `568c81a5...` (build 4);
the source INFs at `dca05e0`. The same image is the 2000 driver
(`wdmx86`); on 2000 `UACCI.INF`/`UACCI1.DLL` from the 0.1.9 `w2k-xp`
folder. `UACWAV.EXE` `1cc0f1b7...` on every system.

**Kits.** 9x: `E:\xhci98-vm\p48\uhci\kit` (copy of aud3's; `uac1legs.py`
takes `--os me`; ports 48600-48614). 2000: `E:\xhci98-vm\p48\uhci\ntkit`
(copy of the 0.1.9 NT matrix kit `a347\mxnt`; `legs.py --stall N
[--inj lo,hi,glo,ghi]`, a `run_stall` mode: install, first plug, Setup's
restart as a cold start, then the rounds with the `MEDIA` class exported
after every step; the injector `ntstall.py` started by the harness on the
leg's QEMU; ports 48620-48628). Bases, read only, fresh overlays: ME
`a347\mx9x\base\men.qcow2`, 98 SE with NUSB 3.3 `a347\mx9x\base\w98n2.qcow2`
(stack `uhc`: the device on the UHCI controller, NUSB's UHCI driver), 2000
`a347\mxnt\bases\w2k-win.qcow2`. Emulator: the usb4-emu fork `cb36497` for
every leg. No Windows 95 image exists under `E:\xhci98-vm` (none with
NUSB): owed, not built.

**Legs** (rounds of play 48k 5 s, play 44.1k 3 s, rec 48k 3 s, rec 44.1k
3 s; counters exported after every step):

| Leg | Stack | Stalls | Device | Passed | Resyncs |
|---|---|---|---|---|---|
| `uac2-me-uh-d1` | ME own UHCI | none | `fs` | 23/24 | 1 play at a refusal |
| `uac2-me-uh-mi1` | ME | 150-600 ms / 2-6 s, 285 | `fs` | 23/24 | 11 plays (13) at refusals; 9 recs behind |
| `uac2-me-uh-mi3` | ME | 300-1000 / 2-5, 305 | `fs` | 23/24 | 8 plays (13); 11 recs (13) behind |
| `uac2-w2k-win-11101558` | 2000 own UHCI | none | `fs` | 24/24 | 0 |
| `uac2-w2k-win-11101647` | 2000 | 150-600 / 2-6, 130 | `fs` | 24/24 | 8 plays at refusals; 8 recs behind |
| `uac2-w98-uhc-d1` | NUSB UHCI | none | `fs` | 24/24 | 1 play, 1 rec |
| `uac2-w98-uhc-ni1` | NUSB | 150-600 / 2-6, 230 | `fs` | 24/24 | 8 plays (14); 7 recs (8) behind |
| `uac1-w98-uhc-d1` | NUSB | none | `adsrc` | 8/8 | 1 rec behind (of 16 with alaw) |
| `uac1-w98-uhc-d1` | NUSB | none | `alaw` | 16/16 | |
| `uac1-w98-uhc-ni2` | NUSB | none in the rounds (injector misfired) | `adsrc` | 8/8 | 0 |
| `uac1-w98-uhc-ni2` | NUSB | (stalled the install) | `alaw` | not installed | - |
| `uac2-w98-uhc-ni3` | NUSB | 300-1000 / 2-5, 237 | `fs` | 24/24 | 11 plays (14); 10 recs (12) behind |
| `uac1-w98-uhc-ni5` | NUSB | 150-600 / 2-6, 291 | `adsrc` | 8/8 | 13 of 16 recs (14) behind, with alaw |
| `uac1-w98-uhc-ni5` | NUSB | (as above) | `alaw` | 16/16 | 7 plays (9) at refusals |

Every stream that opened completed all its buffers; `LastStatus` 0 in
every stream; no stream ended at the driver. With stalls the empty
capture URB's rule engaged on each stack (recordings resynced behind the
bus with `DataUrbFailed` 0: ME 9/9 and 9/11, 2000 7/8, NUSB 7/7, 10/10, 12/13); plays
resynced only at refusals.

**Finding (ME, not fixed).** 3 of 18 `rec 44.1k` on ME failed `open=8`
with nothing reaching the device: `Uac2M0Stage` 30 (`NewStream`),
`Uac2M0Status` `80000011` (`STATUS_DEVICE_BUSY`, the capture direction's
one-stream gate held). `Uac2Streams` shows ME creating and closing several
capture pins per 44.1k open; in 3 of the 15 that passed the stream ran at
48 kHz (ME's own conversion). The gate is released only when the stream
object is destroyed, which a pin's final `Release` queues to a work item
(`uacstream.h`), so a pin created right after a close can meet the gate
still held: fits, not proven; not seen on 98 SE, 2000 or NUSB. A fix (wait
in `Create` for a closing stream) must drop the operation mutex the
cleanup's `Close` hook takes: not small, so reported, not changed.

**Void/harness.** 2000 `VOID-uac2-w2k-win-11101526` (Setup's restart came
before the USB device's wizard, which queues behind the base's unknown PCI
device wizard; the harness now waits until `uac2wdm` is the service),
`VOID-...-11101556` (started unpatched, killed). `uac1-w98-uhc-ni2`: the
injector's start pattern was split at its spaces by `Start-Process`, so it
stalled the `alaw` install (2 + 65 stalls) instead of the rounds; rerun as
`ni5` (dotted patterns). 98 SE's first-plug wedge: 7 times in the NUSB
legs, each recovered by the harness.

**Totals.** `uac2` `fs`: ME 69/72 (the 3 are the opens above), 2000 48/48,
NUSB 72/72; `uac1` under NUSB: `adsrc` 16/16 as found, 8/8 with stalls;
`alaw` 16/16 and 16/16.

**Record.** usb-wdm-free `docs/testing/2026-11-09-uac-uhci-stalls.md`
(new section "ME's, 2000's and NUSB's UHCI (2026-11-10)", Left, Evidence)
and its `open-items.md` entry: commits `5fda4e1` and `63569e1` on
`w48-uhci`; the reviews of the working tree before `5fda4e1`
and before `63569e1` both approve, no material findings (in the
worker's scratch). Every harness, injector, simulator and QEMU of
both kits is in their `pids.txt`; all exited, none running at 18:01.

### Merge (usb-wdm-free, w48-uhci)

Merged into usb-wdm-free main at `6a266c7` (2026-11-10); ME's 44.1 kHz recording left to "Planned - Left by `3.4.3.0`" (UAC2MEREC.1).

## 48.urblen URB header length made log-only for every function (owner, 2026-11-10)

Branch `w48-urblen` from `3.4.3.0` at `6027263d`; commit `c7e91fbb`.

- Change: `hcdSubmitUrb` (src/hcd_urb.c) no longer refuses a URB whose
  `UrbHeader.Length` is short of `hcdUrbMinLength`; it counts
  `hc->UrbsHeaderShort` (HCD_CONTROLLER, not the counter block), traces
  `hcd: URB header short, served, function/length` (qemu) and notes
  `urb.header.short` (every flavour) at each function's first, and serves.
  `UrbsMalformed` counts refusals only (matrix-hcd.psd1's
  'zero URBs refused - malformed' unaffected).
- Safety audit: the SELECT_CONFIGURATION walks bounded only by the header
  (hcdCfgSelectOnce, hcdCfgSelectFunction, hcdGhostSelect - whose
  `end - &Interface` would have gone negative and become a huge ULONG -
  and hcdCompSelect) take `XhciPipeSelectListBytes`: first entry by its
  own Length (usbport's do-while reads it whatever the header), later
  entries by the header; a first Length short of the fixed part refused
  before teardown. SELECT_INTERFACE was already bounded (iss32b). Kept as
  in 3.4.1.0: isoch packet-table check, GET_CURRENT_FRAME_NUMBER, the
  composite parent's descriptor request, static-streams checks.
- Host: test_pipe 14569 checks, 0 failures (MSVC6 x86, compiled by hand;
  the exe's exit hung on the host stall). check-text OK.
- Review: one adversarial round on the working tree, verdict approve, no
  material findings.
- Owed: gated x86 + amd64 build; legs (2000 audio sndrec32 /play, 2000
  stick copy-compare, 98 SE stick + HID mouse, the counter/log note).

### Builds and legs of `c7e91fbb` (2026-11-10, after the host reboot)

- Host suite (`test\run-host-tests.cmd`, x86 and the amd64 leg): "Host
  tests PASSED.", 97 suites, 0 failures (`test_pipe` 14569). Log
  `E:\xhci98-scratch\w48\urblen\logs\pre.log`.
- Gated `build-driver.cmd all` x86 and `all -amd64`
  (`XHCI98_SELFTEST_SKIP_OK=1`): both "BUILD + GATES PASSED (release
  qemu)", dirty 0. `xhci98.sys` x86 release `75F83B38...D77D721`, qemu
  `11EE0934...C66F590C`; amd64 release `AC1A24C7...70139714`, qemu
  `8AC6C7B0...38A5868`. Logs `logs\gated-x86.log`, `logs\gated-amd64.log`.

| Guest | Build | Step | Result |
|---|---|---|---|
| 2000 (`w2k\`, a copy of iss32b's `x1` overlay, `qemu-xhci` + `usb-tablet` + `usb-audio`) | x86 qemu `11EE0934` (SW.BAT, fc equal), cold start | `sndrec32 /play tada.wav` | played to the end, 1.61 of 1.61 s (`play-q1.png`) |
| | | stick (`usb-storage` hot-plugged, vvfat) copy + `fc /b` both ways | no differences (`cc-q1.png`) |
| | | trace (`debugcon-q1.log`) | `hcd: URB header short, served, function/length=00010000` once; `URBs served - short header` 1 to 5 (SELECT_INTERFACE alternates 0/1 at each play); `URBs refused - malformed=00000000` |
| | x86 release `75F83B38`, cold start | play | to the end, 1.61 s (`play-r1.png`) |
| | | stick copy-compare | no differences (`cc-r1.png`) |
| | | `XHCITR /snapshot` report | `urb.header.short=00010000` (`tr-r1c.png`) |
| 98 SE (`w98\`, a fresh overlay of the q35 base) | release package of `c7e91fbb` (make-package + family layout, `XHCIINST /dir 98-me /console /yes /nopause`): exit 3, cold start | USB mouse (`usb-mouse` hot-plugged, HID wizard from HIDDEV.INF) moved by `mouse_move` | cursor follows (`m5`-`m7.png`) |
| | installer re-run with the NUSB 3.3 storage files added to the kit (exit 4: the ASM4242 xHCI's wizard steps, not this change) | stick, USB Mass Storage wizard (USBSTOR.INF), copy-compare | no differences (`s5.png`) |
| | | `XHCITR /snapshot` | no `urb.` note; `log.verbosity.refused=0` |
| | x86 qemu `11EE0934` (QP9.BAT, fc equal), cold start | mouse, stick copy-compare | moves (`d2.png`); no differences (`d4.png`) |
| | | trace (`debugcon-d.log`) | `URBs refused - malformed=00000000`, `URBs served - short header=00000000` |

Evidence: `E:\xhci98-vm\p48\urblen\w2k\`, `E:\xhci98-vm\p48\urblen\w98\`.

### Merge (w48-urblen)

Merged into `3.4.3.0` at `358db5b7` (`c7e91fbb`, 2026-11-10).

## Issue 32 follow-up: the Windows 2000 playback check in the release legs, the lesson, the acceptance clause (2026-11-10, worker audleg)

Branch `w48-audleg` from `6027263d`. The owner's three decisions of
2026-11-10 after GitHub issue 32 (`docs/issues/30-w2k-usbaudio-select-interface-header.md`).

**Commits.** `3e72d848` (the check: `release-legs.psd1` `Audio`,
`run-leg.ps1`, `release-matrix.ps1`, `lib\legvm.ps1` `-Backends`,
`lib\leghost.ps1` `Add-LegAudio` / `Get-LegWavChk` / `New-LegToneWav`,
`leg\LEGNT.CMD` `:audio`, `wavchk\wavchk.c` and `build.cmd`, the
README row); `cccd1c8f` (docs: `build-and-test.md`, "The playback check
(2000; GitHub issue 32)"; `lessons.md`, "A failure on one OS with an
emulated device is checked against Windows' own driver for another
controller before the emulator is blamed"; `release-acceptance-test.md`
7.22); `59517458` (a completed play past its bound fails too); `3bead370` (the `list-current` wording, `XHNORST.VBS`, the measured runs in `build-and-test.md`).

**The check.** `w2k-console` runs QEMU with `-audiodev none` (outside the
starting point's hash); after `/list` the guest says `XHLEG WANT
plug-audio`, the host plugs `usb-audio` into `qemu-xhci` by QMP, and
`WAVCHK.EXE` (C89, MSVC 6.0) waits up to 300 s for the waveOut device
named USB, opens it (30 s of tries), plays a 2 s 22.05 kHz tone and
reports each `MMSYSERR` result and the time on COM1 (`audio-play`); then
the package's `XHCITR /report` must hold `xhci98.sys`'s counters
(`TransfersSubmitted`) and no `UrbsMalformed` (`urbs-malformed`).

**Review.** Round 1: one P2, a completed play past its bound passed
(fixed, `59517458`); round 2: no material findings. The harness fix (`3bead370`): one P2 (the restart answer could count without the box closing; fixed before the commit), then no material findings twice (the harness, then the build-and-test.md paragraph).

**Builds.** `WAVCHK.EXE` (`build.cmd`, `/W3 /WX` clean, 45056 bytes,
SHA-256 `6480db97...892a9`, subsystem 4.00, imports `KERNEL32` and
`WINMM`'s `waveOut*` alone), built at 17:41. After the host's reboot: one
gated x86 release build of `59517458` (slot 2, `XHCI98_SELFTEST_SKIP_OK=1`):
"BUILD + GATES PASSED (release)"; `xhciinst\di`, `xhciinst`, `xhcimark`,
`xhciperf` built; `make-package.ps1 -Flavor release -Arch x86` exit 0;
the kit `E:\xhci98-scratch\w48\audleg\kit\PKG` (family layout, `tools\`,
`SETUP.EXE` and `INSTALL.BAT` at the top; `kit-sums.txt`): `xhci98.sys`
`0b0008b8...`, `XHCITR.EXE` `1915b2b8...`, `XHCIINST.EXE` `9fa9b868...`,
`SETUP.EXE` `80fedc3b...` (the tree's version stamp is still 3.4.2.0
before the cut). `releases\3.4.2.0`: `xhci98.sys` `6575e65e...`, `XHCITR.EXE`
`bdc57624...`. Logs `E:\xhci98-scratch\w48\audleg\build-release.log`,
`tools.log`.

**Configuration.** `release.config.psd1` with the w2k guest alone (the
sample's values, the q35 bases' QEMU `asm4242-20261027-fwpad`, QEMU 11.1.2,
ports 48760-48768, `StickImage` = `q35-bases\shared\nvme-fat.qcow2`, a
128 MiB FAT16 image with `TESTDATA.BIN`, read from its root directory).
`make-start.ps1 -Guest w2k`: 3 Found New Hardware pages walked, cold start
to the poller 198.4 s. Runs in `E:\xhci98-vm\p48\audleg\release-matrix\runs\`.
The `-audiodev none` backend outside the start hash restored from the saved
state with no complaint (every leg's boot 1 is a restore).

**Two harness faults the first tip run showed (fixed, reviewed).**
(1) `list-current` failed on a correct install: `05d20d52` renamed the
`/list` row of a driver at the package's version from "Reinstall (same
version)" to "Keep (same version)", and `LEGNT.CMD` and `LEG9X.BAT` still
searched the old words (the slip of 46's `9ed7f787` again), so on the tip
every leg's `list-current` fails and `list-removed` passes whatever the list
says. Both scripts now accept either wording for `list-current` and refuse
either for `list-removed` (`LEG9X.BAT` not run). (2) 2000 asked "System
Settings Change ... restart your computer now?" once the audio device was
in; with the box open the install stays pending and the uninstall waited a
minute and stopped with exit 4. `leg\XHNORST.VBS` (run after `:audio`,
copied to the CD's `XHWAV\`) waits up to 120 s for the box, answers No only
right after `AppActivate` brought it forward, and counts the answer only
once the box is gone; a log on COM1, not a check.

| Run | Package | Result | `audio-play` | `urbs-malformed` | Note |
|---|---|---|---|---|---|
| `r342` | `releases\3.4.2.0` | FAIL (282.9 s), as expected | FAIL: `waveOutOpen` `MMSYSERR` 32, 30 tries | FAIL: `UrbsMalformed` 31 | before the harness fix |
| `r342b` | `releases\3.4.2.0` | FAIL (400.3 s), as expected | FAIL, WAVCHK exit 3 | FAIL | with the fix; run beside `rtip2` |
| `rtip` | kit | FAIL (277.5 s) | PASS, 2000 ms in 2113 ms | PASS (`TransfersSubmitted` 217) | `list-current` FAIL (fault 1), uninstall exit 4 (fault 2) |
| `rtip2` | kit | FAIL (385.2 s) | FAIL, WAVCHK exit 4: ended after 511 ms, position `MMSYSERR` 1 | PASS (`IsoPacketsAnswered` 430) | with the fix; beside `r342b`; every other check PASS |
| `rtip3` | kit | PASS (360.2 s) | PASS, 2103 ms | PASS | alone; no restart question |
| `rtip4` | kit | PASS (387.2 s) | PASS, 2033 ms | PASS | alone, final `XHNORST.VBS`; no restart question |

Evidence: each run's `result.json`, `serial-b*.log` (`LOG wavchk`,
`LOG xhcitr`, `LOG urbs`, `LOG norst`) and screendumps (`rtip\b2-last.png`:
the restart question).

**Open.** `rtip2`'s short play: the open, the write and the close
succeeded, the buffer came back done after 511 ms with the position
unreadable, and the driver answered 430 isochronous packets (2030 in a full
play) with no error counter raised; it ran beside another leg on TCG. Not
explained; once in four. The `XHNORST.VBS` answer itself is not yet seen
(the box came only in `rtip`, before it existed).

### Merge (w48-audleg)

Merged into `3.4.3.0` at `370d14da` (2026-11-10), with the clause 7.22 fix after it. The short play under host load left to "Planned - Left by `3.4.3.0`" (AUDSHORT.1).

## 48.16 / 48.18: worker net10 (2026-11-10, stopped by the host reboot)

- Branch `w48-net10` at `89715976` (the `3.4.3.0` tip), no change.
- Gated x86 release build of `89715976`: BUILD + GATES PASSED (`gated-x86.log`); `xhcicm.sys`
  `8D44C3BF...0DFE80EF`. Kit `E:\xhci98-vm\p48\net10\kit10`.
- No leg read. The emulator copy in `E:\xhci98-vm\p48\net\bin` hung at process creation on development
  host A (the host-wide stall the reboot clears); the five image preparations started on
  `q35-bases\bin\dock-20261110-bar0` were stopped mid-install for the reboot.
- Owed (unchanged): 48.16 Titan Ridge, a resume, the `XhciCmNet` toggle, Driver Verifier; 48.18 ME,
  2000, Vista to 10 x86, the stand-alone snapshot files. Scripts ready: `kit\c10.sh`, `TOG.BAT`, `VFY.BAT`.

## 48.16 / 48.18: worker net10, after the host reboot (2026-11-10, 18:10-19:50)

- Branch `w48-net10` fast-forwarded to `04786936` (the `3.4.3.0` tip, net9's `xhcinet.sys` in it); no change
  of its own, no review owed.
- Gated x86 release build of `04786936`: BUILD + GATES PASSED (`gated-x86-b.log`, record `dirty=0`).
  Kit `E:\xhci98-vm\p48\net10\kit11` (make-package x86, family layout, `kit\mkkit.sh`):
  `xhcinet.sys` `FA9EB1F8E4B3A960AAAFBD74752B5C7BB6E06593C0862BE1A492DDF66A33D5EC`,
  `xhcicm.sys` `2CB8240DA4604628C6645416C1013C3A666F29FD021F137BABCDB59B530B51E4`,
  `xhci98.sys` `01B07DEF351842499B02A6E1392D18023A809B88FA3AE938A878E7A4FB2158D7` (ME's XHCITR read the
  same `xhci98.sys` hash on the guest). Version stamp of these binaries and of XHCITR: 3.4.2.0.
- Emulator `q35-bases\bin\dock-20261110-bar0` (usb4-xdomain); `-Usb4 tb3ar` both ends (Alpine Ridge NHI
  `8086:15BF`, ICM), cable `usb4-xdomain` over a TCP socket. Images: XP (`xpc`) and ME (`mec`) installed by
  `INSTALL.BAT /console /yes /nopause /thunderbolt /tbnet` (exit 3 each), fresh overlays per cell.

**Cell C2x, XP (no Verifier) at A, ME at B** (`cells\C2x\`, `leg.txt`, `A\serial-A.log`, `B\serial-B.log`):

| Step | XP | ME |
|---|---|---|
| first connection | Found New Hardware "xHCI98 Thunderbolt Networking", walked (No / automatic), "ready to use" in under 1.5 min | adapter already present |
| address | `169.254.89.254` (APIPA) | `169.254.148.162` |
| ping the other end (6) | 6/6, 14-31 ms | 6/6, <10-27 ms |
| XHCITR report, networking node, with peer | "Thunderbolt networking - up", `XhciCmNet: 1 (on)`, IP, other computer's MAC, 61/31 packets, 0 errors, `xhcinet.sys, MTU 1500`, link "logged in (carries the network)", peer Kind xHCI98, 13/13 inter-domain packets, 1 login each way; `cm.net.pdo=00000001`, `cm.net.up=01000808` | "Thunderbolt networking - up", `XhciCmNet: 1 (on)`, IP, other MAC, 37/67 packets, 0 errors |
| stand-alone snapshot `XHCITR /snapshot TS` | `TS.DEV`, `TS.TXT`, `TS.BIN`, `TS.PSC` written, "Attach: TS.DEV TS.TXT"; networking section in `TS.DEV` (MAC and UUID "not included in the snapshot") | `TS.DEV` (OS "Windows ME ... 4.90, build 3000") with the networking section |
| cable out at B (QMP `xd0 plugged false`), 60 s, report | "Thunderbolt networking - no other computer connected", "Link changes: 1 up, 1 down" | the same |
| cable in, 150 s, ping and report | 6/6; "up", "Link changes: 2 up, 1 down" | 6/6; "up", "2 up, 1 down" |

Verdict: 48.18's networking node, saved report and stand-alone snapshot read on XP and ME, with a peer and
without. **pass** (emulator).

**48.16, the `XhciCmNet` toggle, XP at A of C2x with ME as the peer** (`TOG.BAT 169.254.148.162 15BF`: the
value in the machine-wide `Services\USB` key, the NHI restarted by DEVCON after each change):

| Step | Read | Verdict |
|---|---|---|
| `XhciCmNet` 0, NHI restarted, 45 s | `devcon status XHCICM\TBNET`: "No matching devices found."; no adapter in `ipconfig`; ping "Destination host unreachable" 4/4 | off: the networking device is not offered |
| 1, restarted, 90 s | "xHCI98 Thunderbolt Networking ... Driver is running."; new APIPA address `169.254.46.6`; ping 6/6, 14-33 ms | on again |
| 0/1 back to back three times (six restarts, each "Restarted"), 90 s | driver running; address `169.254.127.159`; ping 6/6 (first 458 ms, then 15 ms) | **pass** |
| value deleted | `togdone` | |

**48.16, Driver Verifier on XP** (`xpv`: `verifier /standard /driver xhcicm.sys xhcinet.sys`, query read
back: pool tracking, I/O verification, deadlock detection, DMA checking enabled, both drivers listed;
`setv-xpv.log`). Cell C1b, XP (Verifier) with ME: the first connection's Found New Hardware for "xHCI98
Thunderbolt Networking" completed in about 2.5 min (`C1b\n9.png`), the adapter came up (XP's "limited or no
connectivity" balloon, the APIPA state, `n11.png`); no bug check in about 20 minutes of running (19:15-19:35).
The legs themselves were not read: the BATs typed into Run did not start (the xfer volume is not `D:` on this
image, and typed characters were dropped while the Verifier guest was busy), and the run stopped for the cut.
**partial**: no stop, the adapter up; ping, report, toggle under Verifier not read.

**Not run (left):** 2000 (the q35 2000 base asks to restart for its PCI devices at each start, and its warm
restart stayed at a black text-mode screen twice: `w2kc`, `w2kd`; needs a cold-start-only preparation); 7 x86
and 10 x86 (installs not completed: the unsigned-driver dialog on 7's 800x600 screen was not answered by the
click script or by keyboard, 10's elevation prompt not taken; `w7c`-`w7e`, `w10c`-`w10e`); the resume on 7
x86 (`kit\s3cell.sh` and `S3.BAT` ready: `-vga none` and the q35s3 SeaBIOS `e5dc0550` through `-Extra`);
Vista, 8, 8.1 x86; Titan Ridge.

### Merge (net10)

Reading only (no commits). The leftovers left to "Planned - Left by `3.4.3.0`" (NETVIEWLEFT.1).

## 48.33 The owner's bench kit (part B of the round-9 sitting) (2026-11-10, worker kit33)

Interrupted by the host reboot of 18:30; resumed 18:40.

- Branch w48-kit33 reset to 3.4.3.0 c80e4324 (coordinator: build the kit from that tip; staging 0 by
  default, the INFs write XhciDmaStaging 0). No commits, no tracked change.
- Gated release x86 build of c80e4324: PASSED (gated-x86.log, slot gated.2); pfw and xhcidi built first;
  xhciinst\build.cmd then kit.ps1 (make-package -Arch x86, ConvertTo-FamilyLayout, 98-me only).
- The tree still carries version 3.4.2.0 (XHCI_VER_STR, DriverVer 11/08/2026,3.4.2.0): the bump is the
  cut's. Consequence for an install over 3.4.2.0: same version, other bytes. The installer
  (inst_eng.c drivers_9x, record 23 s5.4.1) places xhciuas/xhcicm/xhcinet for the next start and asks
  for a SECOND run after the restart to replace xhci98.sys (exit 4). With /yes it never updates
  stor98.mpd ("Disconnect all external NVMe drives" cannot be confirmed) - a step left on every run.
  So INST33 runs INSTALL /console /yes /nopause /force, and is run twice (a marker INST33.1 makes the
  second run's exit 4 count as done). SPEED33 records whether xhci98.sys and xhciuas.sys have the
  package's sizes (FC /B was dropped: on 9x it lists every differing byte, minutes per file).
- "/folder ." and a relative /report work for XHCIPERF under START /W on 98 SE (exit 0, P33.TXT written).
- The PROMPT $P trick (COMMAND /C of a one-line @PROMPT batch) gives INST33 its folder and drive back
  after it changes to C:\PKG343: checked on 98 SE.

### Dry run of part B (98 SE, emulator), 19:05-19:52
Guest: q35 98 SE, overlay of inst\s98\base98.qcow2 (= net6 p98k: 98 SE + tb3ar + a 3.4.2.0-numbered dev
xHCI98, no NUSB storage) + QEMU usb-uas (1.5 GiB FAT32 "UASDISK", E:) on qemu-xhci port 1 at power-on,
tb3ar topology, QMP 48330, smp 1, emulator asm4242-20261027-fwpad. Evidence: E:\xhci98-vm\p48\kit33\s98c\
(pids.txt, *.png; FAULT-h1.png).
- INST33 run 1 (from C:\STAGE, the INST33 of that moment): exit 4; xhciuas/xhcicm/xhcinet placed for the
  next start, "xhci98.sys: run Setup again after the restart", NUSB33 storage copied (cured the UAS Code 2).
- Cold start; E: (UAS) mounted; STAGE copied to E:\STAGE; marker INST33.1 made by hand (run 1 had been from
  C: with the earlier INST33); INST33 run 2 from E:\STAGE: exit 4 (Stor98 only) -> "Installed";
  INST33.TXT: xhci98 365554, xhciuas 43879, xhcicm 505061, xhcinet 20093 bytes = the package's.
  Running from the UAS drive survived the UAS disk's reinstall during the run.
- STAGE1 -> cold start -> SPEED33: S1.TXT, stage.switch=00000001, exit 0. ~3.5 min.
- STAGE0 -> cold start: FATAL EXCEPTION 0E at 019F:BFF8AB97 ("current application will be terminated",
  a ring-3 KERNEL32 address) during the start, then a black text screen; power off; Windows started in
  Safe mode by itself; Shut down; cold start normal. Not seen in the other boots of the new driver
  (f staging 0, g 1, j 0, k 0 all clean). One in five; not reproduced; cause not read.
- SPEED33: S0.TXT, stage.switch=00000000, exit 0. STAGE0 again -> cold start -> SAVE33: BACK33 with 11 files;
  Services\USB 0, both controllers' keys XhciDmaStaging 0 (the INF's).

| UAS (E:), MB/s | staging 1 (S1) | staging 0 (S0) | 1 vs 0 |
|---|---|---|---|
| seq 1 MiB write | 42.2 | 52.8 | -20% |
| seq 1 MiB read  | 92.6 | 93.7 | -1% |
| seq 64 KiB write | 42.3 | 37.4 | +13% |
| seq 64 KiB read  | 49.9 | 48.4 | +3% |
One set each, 3 passes of 8 s, 512 MiB file, depth 1, processor 100 % in every test; host shared.

### Merge (kit33)

Reading only (the bench kit, delivered 2026-11-10). The bench left to "Planned - Left by `3.4.3.0`" (STAGEBENCH.1).

## 48.x The other computer's MAC in the Thunderbolt networking report (owner, 2026-11-10)

Branch w48-macfix from 3.4.3.0 at 4d6c8b8b.

**Defect** (run48-r9read.md, "Found in passing"): every round 8 and 9
report read "Other computer's MAC: DC-89-59-B0-40-A6"; the Framework's
adapter is B0-59-89-DC-A6-40 (round 8's `ipconfig.txt`,
`adapters-all.txt`, and the frames in both captures). Each DW's bytes
were reversed.

**The on-wire order, established:**
- Linux (`drivers/net/thunderbolt/main.c`, GPL-2.0, facts only):
  `struct thunderbolt_ip_login_response` has `u32 receiver_mac[2]`;
  `tbnet_login_response` does `memcpy(reply.receiver_mac, dev_addr, 6)`
  (host order, little-endian on every Thunderbolt host), and
  `drivers/thunderbolt/ctl.c` `tb_ctl_tx` runs `cpu_to_be32_array` over
  every DW (receive: `be32_to_cpu_array`). So the DW value is
  mac0 | mac1<<8 | mac2<<16 | mac3<<24, and DW 20 mac4 | mac5<<8. Linux
  never reads the peer's field.
- This driver loads each DW big-endian (`tbt_ring.c` `tbtLoadBe32`), so
  its dw[] values are the same numbers Linux's are.
- Windows 11 (bench, rounds 8 and 9): the old parse (first byte = bits
  31:24) showed DC-89-59-B0-40-A6, i.e. DW 19 = 0xDC8959B0, DW 20 =
  0x40A6, which is exactly Linux's layout for B0-59-89-DC-A6-40.
- Record 35 section 2.1's "DW 19 the most significant four bytes" (IDS
  Table 5-6's words) had been read as "first byte in bits 31:24"; both
  real implementations put the first byte in bits 7:0. Corrected.

**Fix:** parse and builder both changed (first byte in bits 7:0 of DW 19,
fifth in bits 7:0 of DW 20). The builder change is safe: no login reads
the field (only `MacLength == 6` gates copying it into `PeerMac`, which
reaches the report, the log `cm.net.peer.mac` and the query only;
xhcinet learns the peer by ARP). xHCI98-to-xHCI98 runs the same code on
both sides: the host test's two-instance login (test_tbt_xdp, A and B to
TBT_NET_S_UP) still logs in and now checks each side's learnt MAC byte
for byte against the other's own. A 3.4.3.0 host paired with an older
xHCI98 logs in as before; only the report shows the older side's MAC
reversed per DW (as the older side shows ours).

**Host test:** vector from the bench (DW 19 0xDC8959B0, DW 20 0x40A6 ->
B0-59-89-DC-A6-40, length 6); builder vector 02-11-22-33-44-55 ->
0x33221102 / 0x5544. test_tbt_xdp: 271 checks, 0 failures.

**Review:** one adversarial round over 4d6c8b8b..6de48495: no material
findings (builder and parser inverse, the ring layer's big-endian load
supports the reading, no other packing of this MAC in tbt_icm.c,
tbt_u4cm.c or cm_net.c).

**Gated build** (commit 6de48495, XHCI98_SELFTEST_SKIP_OK=1, slot
gated.1): x86 release BUILD + GATES PASSED (11:36; test_tbt_xdp 271
checks, 0 failures), amd64 release BUILD + GATES PASSED (9:04). Logs:
E:\xhci98-scratch\w48\macfix\build-x86.log, build-x64.log.

| binary | x86 SHA-256 | amd64 SHA-256 |
|---|---|---|
| xhcicm.sys | 36DA46ACC90AE54A521DBFBCF96F9F36D3B8C0E60540B151391BA15D850E5557 | 55EF5652D69C3CC972F639B7D3DB0B2FE945E95F67D08CB50028530F562ED68A |
| xhci98.sys | 708911A543D6B5A8C197703BBC6FC3CF1BC65A2B008E95AFFFAAF15846E14978 | 3C60EE316E2BB7BD0A3D143C1D6E6578D3E3178DF49724DAF1949138BFB9758D |

No emulator leg (report-only field; the two-instance host login covers
xHCI98-to-xHCI98). Owed on the bench: the next Windows 11 pairing's
report should read B0-59-89-DC-A6-40.

### Merge (w48-macfix)

Merged into `3.4.3.0` at `4cbffbb6` (`6de48495`, 2026-11-10). The bench check (the next Windows 11 pairing shows `B0-59-89-DC-A6-40`) left to "Planned - Left by `3.4.3.0`" (PEERMAC.1).

## 48.35 (continued) S.6 read again: 10 x64 on the Intel ADL host model (2026-11-10, 19:55 to 20:40, worker s6re)

10 x64, the amd64 kit of `89715976` (release `xhcicm.sys`, so no debugcon log), virtio-vga with viogpudo, HiberbootEnabled 0; the emulator's `intel-adl` USB4 host with the software connection manager; the ORICO USB4 NVMe attached at power-on; `XhciCmSleepEntry` 1 (the INF default, no machine-wide value); S3 with the `q35s3` BIOS. A copy of worker tbs7's installed `x10a` disk. QMP 48920.

| Step | Result |
|---|---|
| Cold start | desktop; the drive E: written (PRE.BIN, 4 MB), `fc /b` no differences |
| Standby | `--- suspend 20:08:01`; QMP `suspended` 19 s later |
| Wake (`system_wakeup` 20:09:13) | `--- resumed 20:09:27`; the script's last line `===E pre 20:09:33`, then nothing |
| Checks at +30 s, +2 min, +5 min (Run box, then typed into the open elevated cmd): a file on C:, `dir E:`, compare PRE.BIN, write a new file | none produced output |
| ACPI power button at about +9 min | no shutdown within 150 s |
| State at 13.5 min | QMP `running`; both vCPUs executing (kernel and user RIPs, HLT 0); stopped by PID |

Verdict: **a hang after the wake, likely but not proven.** Against the emulator, display or BIOS: tbs7's 10 x64 leg with the same image, display, BIOS and emulator but the asm4242 host ran its script past the wake to the end (the drive written, 69/69 devices, shutdown; `E:\xhci98-vm\p48\tbs7\x10\serial-a.log`). Its screen after the wake was black too, so the black screen is the display's, not the hang. Not ruled out: whether keyboard input reaches 10 x64 after a wake at all (the asm4242 leg needed none); the power button ignored points at the guest. With tbs7's first S.6 run, where the guest ran about 40 s after the wake and stopped at `wmic diskdrive`, the likely picture is that work touching the disk stack or the USB4 drive stops after a wake on the ADL model. Not read: the input control on asm4242 (started, stopped unread), and `XhciCmSleepEntry` 0 on ADL. The ADL model has no pre-boot connection manager, so the user documents' third standby sentence (stop the drive before standby or hibernation where the firmware does not restore the connection) covers this configuration (TBSTBYLEGS.1).

Evidence: `E:\xhci98-vm\p48\s6re\` (`r1\serial-a.log`, `r1\a-*.png`, `r1\a-regs30.txt`, `r1\a-regs-late.txt`, `pids-all.txt`, `STATE.md`). QEMU pids 40632 (the leg), 41272 (the control), 34328 (a misquoted first launch, no guest work, its folder deleted); none left running.

### Merge (s6re)

Reading only.

## 48.33 (continued) The owner's bench, part B: staging on and off on the P14s (2026-11-10, 20:04 to 20:33, the owner)

98 SE on the P14s, the pre-release package of `c80e4324` (files still stamped `3.4.2.0`) installed by `INST33` twice (Setup's exit code 4 both times; after the second run `xhci98.sys` 365,554, `xhciuas.sys` 43,879, `xhcicm.sys` 505,061 and `xhcinet.sys` 20,093 bytes, the package's sizes). The UAS drive (MS______MSSU10-128GSR, FAT32, D:), `XHCIPERF` 512 MiB, unbuffered, write-through, verified, 3 passes of 8 s, best shown. `XhciDmaStaging` machine-wide; the USB Tree reports before and after each run confirm it (`stage.switch=00000001` with 32 slabs and `stage.pool.short=0`; `stage.switch=00000000` with no slabs).

| Test (MB/s) | Staging 1 | Staging 0 |
|---|---|---|
| Seq 1M write | 104.9 | 105.3 |
| Seq 1M read | 136.6 | 134.1 |
| Seq 64K write | 97.7 | 98.3 |
| Seq 64K read | 138.8 | 136.3 |

No difference above the noise: the processor is busy 98 to 100% in both runs, so the copy through the driver's buffers costs nothing measurable at this speed. With staging off, 64K read passes 2 and 3 fell to 78.2 and 76.7 (pass 1 136.3); with staging on, every pass was within 0.3 MB/s. Seen once; not read again. Every test verified; no failure, no resets, no refused notes in the reports. `XhciDmaStaging` left at 0 (`USB.REG`). Results: `temp\p14s-stage\BACK33` (untracked; `S1.TXT`, `S0.TXT`, `R1A`/`R1B`/`R0A`/`R0B`, `REPORT.TXT`, `INST33.TXT`).

### Merge (part B)

Reading only.

## 48.final The install legs from the signed downloads (2026-11-10, worker legs343)

The fresh-install legs of the owner's signed cut of `3.4.3.0` (branch
`w48-cut343` at `bf55b7f0f`), from the downloads as a user has them. The
updates over `3.4.2.0`, 2000's playback leg (`w2k-console`, WAVCHK) and the
`-nonfree` legs (43.48) are worker legs343b's and are in its own section.
Development host A, QEMU 11.1.2 (`v11.1.2-244-g4b495b9`, TCG), every leg
on a fresh overlay of its q35 base (`E:\xhci98-vm\q35-bases\`), `-smp 1`
on 98 SE, ME and 2000 and 2 on the others. Nothing was built and nothing
was signed: every x64 driver and catalog carries the owner's test signature
from the cut, and its certificate (`nt-x64\xhci98-test.cer`) went into the
guest's Root and TrustedPublisher stores, as on 47.final.

**The downloads.** `E:\xhci98-wt\w48-cut343\out\`, SHA-256 checked before
unpacking, each into its own empty folder (`E:\xhci98-vm\p48\legs343\pkg-base`,
`pkg-free`, `pkg-nonfree`). They went only onto the guests' CDs and
transfer disks:

| Zip | SHA-256 |
|---|---|
| `xhci98-3.4.3.0.zip` | `cc0ab19e8f3ddba9cde6f49b32baba6d52eefe57c7602aa67f9e565bc8a4cb77` |
| `xhci98-3.4.3.0-free.zip` | `40f45436dfab00d17e36889890c0446464bc02572e8883fb27438a026a45fd9c` |
| `xhci98-3.4.3.0-nonfree.zip` | `b15dd972f5682597c71d51208aaa0757e3c40b55c5f4b18ba16a7b7f05d708a7` |

The base download's binaries (size, first 16 hex digits of the SHA-256):
`98-me\xhci98.sys` = `nt-x86\xhci98.sys` 366,051 `3b35b4ae102266b5`;
`nt-x86\xhciuas.sys` 43,880 `be716c6500563b9d`; `nt-x64\xhci98.sys`
419,800 `77273bb0b86d6146`; `nt-x64\xhciuas.sys` 57,304
`65053ececa262a84`; `SETUP.EXE` 69,632 `a73eff5e642abdc9`;
`XHCIINST.EXE` 610,304 `98e7c679f9483c7e`; `XHCIIN64.EXE` 912,384
`68d1fc84a902b601`. Every leg read the loaded image as byte-identical to the
download's ("SAME").

**How.** 98 SE, ME, 2000, XP, Vista and 7 ran in the release matrix
(`release-matrix.ps1 -Package pkg-base -Only` the eleven fresh-install legs
of `release-legs.psd1`, `MaxVcpu 7`). Its starting points were made by
`make-start.ps1` for 98 SE, ME, XP, Vista and 7; 2000's was copied from the
playback leg's, made today with the same guest scripts, QEMU and hardware
hash. `release-legs.psd1` has no 8.1, 10 or x64 legs, so those ran with
copies of 47.final's kits: `rlx86` (8.1 and 10 x86) and `rlx64` (XP x64,
Vista x64, 7 x64, 8.1 x64, 10 x64), with the base download in place of
`3.4.2.0`. Each kit does the same: the install (`INSTALL /console /yes
/nopause`, with `/usb` on 8.1 and 10); after a cold start, the readings
(devices with codes and services, images against the download, signature,
INFs) and copies to the stick and the UAS disk; then the controller
disabled, enabled, removed and rescanned, with readings and copies after
each; the uninstall; and the readings after the next cold start. At the
peak the host ran 7 kit legs and the matrix's legs, beside legs343b's.

Results: `E:\xhci98-vm\p48\legs343\work\runs\20261110-214650\results.md`
(the matrix, 36.4 min of wall time), the 98 SE window leg run again in
`work\runs\rerun-w98se-window\`, the kits in `x86k\legs\` and
`x64k\legs\` (paths below are relative to `E:\xhci98-vm\p48\legs343\`).

| System | Leg | Verdict | Evidence |
|---|---|---|---|
| 98 SE | console (`w98se-console`) | **PASS**: install 3, list 0, uninstall 3, list-final 0; every check PASS (stick E:) | `work\runs\20261110-214650\w98se-console\` |
| 98 SE | window (`w98se-window`) | **PASS** when run again (743 s; every check PASS). The matrix's run FAILED: the guest stalled after "WANT plug-stick" (below) | `work\runs\rerun-w98se-window\`; failed run `work\runs\20261110-214650\w98se-window\` |
| ME | console (`me-console`) | **PASS**: install 3, list 0, uninstall 3, list-final 0 | `work\runs\20261110-214650\me-console\` |
| ME | window (`me-window`) | **PASS**: the same exits | `work\runs\20261110-214650\me-window\` |
| 2000 SP4 | window (`w2k-window`) | **PASS** on the evidence (installer log "Exit code 3"; every check PASS). The matrix says FAIL because the leg script misread the exit code (below) | `work\runs\20261110-214650\w2k-window\` |
| XP SP3 | console (`xp-console`) | **PASS**: install 3, list 0, uninstall 3, list-final 0 | `work\runs\20261110-214650\xp-console\` |
| XP SP3 | window (`xp-window`) | **PASS** on the evidence ("Exit code 3"; every check PASS); matrix FAIL from the same misread | `work\runs\20261110-214650\xp-window\` |
| Vista SP2 | console (`vista-console`) | **PASS** | `work\runs\20261110-214650\vista-console\` |
| Vista SP2 | window (`vista-window`) | **PASS** on the evidence ("Exit code 3"; every check PASS); matrix FAIL from the same misread | `work\runs\20261110-214650\vista-window\` |
| 7 SP1 | console (`w7-console`) | **PASS** | `work\runs\20261110-214650\w7-console\` |
| 7 SP1 | window (`w7-window`) | **PASS** on the evidence ("Exit code 3"; every check PASS); matrix FAIL from the same misread | `work\runs\20261110-214650\w7-window\` |
| 8.1 x86 | fresh, `/usb` (`w81x86-F`) | **PASS**: exit 3, "xhci98 3.4.3.0, started"; controller, root hub, hub and composite device on `xhci98`, images SAME; the UAS disk on Windows' `UASPStor` at problem 10 (no `xhciuas` on 8 to 10; as documented); stick read, written and read back after the cold start, after enable and after the rescan; disable problem 22, then running; after remove and rescan Windows gave the controller to its own `USBXHCI` and the stick passed there; uninstall exit 3; after the cold start the controller on `USBXHCI`, no xHCI98 file or service. The same as 47.final | `x86k\legs\w81x86-F\` |
| 10 22H2 x86 | fresh, `/usb` (`w10x86-F`) | **PASS**: exit 3 (the install took 30 min under host load, as 47.final noted); readings as 8.1; stick PASS after the cold start, after enable and after the rescan; disable and enable PASS; remove "Windows asks for a restart", so the tree stayed on `xhci98` through the rescan: 47.final's known not-regression (Windows' audio engine holds the endpoint open); uninstall exit 3; after the cold start the controller on `USBXHCI`, no `xhci98` or `xhcihlpr` file or service, stick PASS | `x86k\legs\w10x86-F\` |
| XP x64 SP2 | fresh (`F-xpx64`) | **PASS**: exit 3; every device Code 0 (controller, root hub, hub, composite device on `xhci98`; UAS disk on `xhciuas`; stick, audio, mice); both images SAME, RUNNING, signature "xhci98 project test signing (not trusted)" verified; copies to the stick and the UAS disk PASS after the cold start, after enable and after the rescan; disable Code 22, enable, remove and rescan ok; uninstall exit 3; after the cold start the controller Code 1 with no driver, nothing of xHCI98 left. The same as 47.final | `x64k\legs\F-xpx64\` |
| Vista SP2 x64 | fresh (`F-vx64`) | **PASS**: exit 3; as XP x64, copies PASS 6 of 6; the remove `DI_NEEDREBOOT`; uninstall exit 4 "(incomplete: run again)", then exit 3 after a cold start; at the end the controller Code 1, nothing left. The same as 47.final | `x64k\legs\F-vx64\` |
| 7 SP1 x64 | fresh (`F-w7x64`) | **PASS**: exit 3; as XP x64, copies PASS 6 of 6; disable, enable, remove, rescan ok; uninstall exit 3; at the end the controller Code 28. The same as 47.final | `x64k\legs\F-w7x64\` |
| 8.1 x64 | fresh, `/usb` (`F-w81x64`) | **PASS**: exit 3; `xhci98.sys` SAME, RUNNING, signature verified; the UAS disk on `UASPStor` Code 10 (as documented); stick PASS 3 of 3; disable, enable, remove and rescan ok, Windows then gave the controller to `USBXHCI`; uninstall exit 3; at the end the controller on `USBXHCI` Code 0. The same as 47.final | `x64k\legs\F-w81x64\` |
| 10 22H2 x64 | fresh, `/usb` (`F-w10x64`) | **PASS**: exit 3; controller, root hub, hub and composite device on `xhci98`, `xhci98.sys` SAME, RUNNING, signature verified; the UAS disk on `UASPStor` Code 10 (as documented); the stick PASS after the cold start, after enable and after the rescan (the UAS disk not mounted, as documented); disable Code 22, enable ok; the remove `DI_NEEDREBOOT`, so the tree stayed on `xhci98` through the rescan; uninstall exit 4 ("removed, restart pending", "(incomplete: run again)"), then after a cold start the second run exit 3; after the next cold start the controller on `USBXHCI` Code 0 with its tree on Windows' drivers, no xHCI98 image. The same as 47.final (104 min there, 88 min here) | `x64k\legs\F-w10x64\` |

Every PASS of the kits reads the same as 47.final's row for the same system
(`E:\xhci98-scratch\w47\rel342\run-x64.md`, `run-x86.md`), with 3.4.3.0 in
place of 3.4.2.0. No leg was Manual (every window leg of
`release-legs.psd1` has its `WindowKeys`), so no Manual leg is owed.

**The window legs on 2000, XP, Vista and 7: a misread in the leg script,
not the installer.** All four reported `XHLEG EXIT install Exit`, and the
matrix allows only 3. Every check passed, and the installer's own log, sent
on COM1 in `serial-b1.log`, ends with `[21:49:57.140] Exit code 3. The log is
C:\XHCIINST.LOG.` (XP; the same line on the other three). The leg script's
`:wrep` (`scripts\vm-matrix\leg\LEGNT.CMD`) reads the code with
`for /f "tokens=3 delims=. "` over the "Exit code " line. That was written
before every installer log line carried its `[hh:mm:ss.mmm]` stamp. With the
stamp, token 3 is the word "Exit" and the code is token 5. The console legs
read `%ERRORLEVEL%` and are not affected. 98 SE and ME parse it in
`LEG9X.BAT` and read 3 (`me-window` PASS). So this is no regression of
3.4.3.0. The fix is a change to the leg script, for a later tree; nothing was
changed in the cut tree.

**The 98 SE window leg's first run: the host's timing, not the driver.**
After the restart Windows installed the controller's root hub by itself
("New Hardware Found: xHCI98 USB 3 Root Hub", `b2-hcd-0.png`), and the poller
then read the driver and `/list` (driver-file and list-current PASS). The
host's step `wizard98-hcd` waited 300 s for the Add New Hardware wizard's
last page, which never came, and failed. Only then did it plug in the stick,
about 6 min after the guest's `XHLEG WANT plug-stick`. It walked the stick's
wizard to "Windows has finished installing the software"
(`b2-plug-stor-5.png`). After that the poller's DOS box ("XHPOLL - CHOICE")
wrote nothing more, not even the `stick-seen FAIL` its 240 s loop gives, and
the matrix ended the start after 900 s of a quiet COM1. Run again on a fresh
overlay (`run-leg.ps1 -Leg w98se-window`), the same step said "no wizard, the
poller up (the controller had its driver)", the stick was plugged at once and
the leg passed: install 3, list 0, uninstall 3, list-final 0, every check
PASS. The host ran about 20 QEMUs at the time of the first run. Why the
DOS box stayed silent after the late plug was not read further.

**10 x64's pace.** The owner asked whether 10 x64 ran faster once 10 x86
had stopped (23:02:13). It did not:

- **CPU.** Its QEMU used 2.0 CPU-seconds per second (both vCPU threads busy
  all the time, under TCG) in every 30 s sample. That held from 22:54 to
  23:02 with 10 x86 running, and from 23:02 to 23:07 with 10 x64 the only
  QEMU on the host (`E:\xhci98-scratch\w48\legs343\log\w10x64-cpu.txt`).
- **Like steps.** From a cold start to the phase script's first COM1 line
  took 3.9 min for start 4 (22:51:59 to 22:55:54, 10 x86 running). Start 5
  took 3.6 min (23:00:49 to 23:04:25; 10 x86 stopped during it). The 120 s
  settle to the readings' header took 2.1 min both times.
- **The earlier change.** The big change came when the rest of the host
  emptied. Starts 2 and 3 took 8.1 and 8.4 min to that first line, with
  about 20 QEMUs on the host. Readings A and B took 4.7 and 6.3 min then,
  against 2.3 min for readings C after the matrix and legs343b had ended
  (47.final: 2.2 and 3.2 min).

So the guest's speed followed the load on the whole host, not the one other
leg.

QEMU processes: the matrix's and `run-leg.ps1`'s in
`work\pids.txt`; the x64 kit's in `x64k\pids.txt`; the x86 kit's in
`x86k\legs\<run>\pids.txt`. Each one started was stopped or exited.

### Merge (legs343)

Reading only.

### 48.final the update, playback and non-free legs (2026-11-10, worker legs343b)

These are the post-cut install legs of the owner-signed `3.4.3.0` cut (`bf55b7f0f`) that are not fresh installs: the updates over
the published `3.4.2.0`, Windows 2000's playback leg (GitHub issue 32) and the 43.48 legs of the `-nonfree` download.
Development host A, QEMU TCG, every leg on a fresh overlay of its q35 base, nothing signed here.

**The downloads.** `out\xhci98-3.4.3.0.zip` `cc0ab19e...` and `-nonfree.zip` `b15dd972...` (SHA-256 checked), unpacked by
legs343 (`E:\xhci98-vm\p48\legs343\pkg-base`, `pkg-nonfree`; checked here file for file against the zips: 98 and 945 files,
none differing, none extra). `xhci98.sys` x86 `3b35b4ae...`, amd64 `77273bb0...`; `xhciuas.sys` x86 `be716c65...`, amd64
`65053ece...`; `XHCIINST.EXE` `98e7c679...`. The published `3.4.2.0` (`kits\k342`): `releases\3.4.2.0\` (the re-cut, with
its re-signed x64 catalogs; `nt-x64\xhci98.cat` `777d691c...`) plus the cut zip's `USBDRV\`. `xhci98.sys` x86 `6575e65e...`,
amd64 `c299d294...`. The `3.4.2.0` `-nonfree` control: the cut's zip as unpacked by rl9x (`a347\rl9x\dl\`).

**The harness.**
- WAVCHK and the 43.48 legs: `release-matrix.ps1` from a copy of the cut's `scripts\vm-matrix\` (`legs343b\tree\`, plus
  `WAVCHK.EXE` `6480db97...`, built in the audleg worktree from the same `wavchk.c`). legs343's starting points were copied in,
  emulator `asm4242-20261027-fwpad` (11.1.2).
- The update legs: copies of 47.final's kits. x64: rlx64 by way of legs343's `x64k`, the released `xhci98-test.cer` trusted as
  in 47.final, launcher pinned `asm4242-20261110-docks`. x86 XP and 7: rlx86 by way of `x86k`, `asm4242-20261025-2230`; kit
  `K341` there is `3.4.2.0` and `K343` is `3.4.3.0`. 98 SE: rl9x's hand-driven steps, `asm4242-20261025-2230`. Each kit has the
  same phases and readings as in 47.final.

| System | Leg | Verdict | Evidence |
|---|---|---|---|
| 2000 SP4 | `w2k-console` (playback, WAVCHK; pkg-base) | **PASS** | `E:\xhci98-vm\p48\legs343b\work\runs\20261110-215123\results.md` |
| 98 SE | update over 3.4.2.0 (console, both) | **PASS** | `E:\xhci98-vm\p48\legs343b\u98\w98u\` (`in-u1.txt`, `ck-u1.txt`, `in-u2.txt`, `ck-u2.txt`, `dt-u2.txt`, `dy-u2.txt`), `u98\shots\` |
| XP SP3 | update over 3.4.2.0 | **PASS** | `E:\xhci98-vm\p48\legs343b\x86u\legs\xp-U\` (`summary.txt`, `leg.log`) |
| 7 SP1 x86 | update over 3.4.2.0 | **PASS** | `E:\xhci98-vm\p48\legs343b\x86u\legs\w7x86-U\` |
| XP x64 SP2 | update over 3.4.2.0 | **PASS** | `E:\xhci98-vm\p48\legs343b\x64u\legs\U-xpx64\` (`summary.txt`) |
| 7 SP1 x64 | update over 3.4.2.0 | **PASS** | `E:\xhci98-vm\p48\legs343b\x64u\legs\U-w7x64\` |
| ME | `me-nonfree` | **PASS** (rerun); first run FAIL, stall after the uninstall | `E:\xhci98-vm\p48\legs343b\work\runs\20261110-222613\results.md`; first run `workB\runs\20261110-215123\` |
| 2000 SP4 | `w2k-nonfree` | 3 of 6 PASS (3.4.2.0: 4 of 5); not a regression (below) | `workB\runs\20261110-215123\`, `workC\runs\20261110-221014\`, `workD\runs\20261110-222543\`, `workE1..E3\runs\*\`; 3.4.2.0: `workC\runs\20261110-221613\`, `-222543\`, `workF1..F3\runs\*\` (all under `E:\xhci98-vm\p48\legs343b\`) |
| XP SP3 | `xp-nonfree` | **PASS** | `E:\xhci98-vm\p48\legs343b\workB\runs\20261110-215123\results.md` |
| 7 SP1 | `w7-nonfree` | **PASS** | the same |

**The playback leg (2000).** install=3 list=0 uninstall=3 list-final=0, 6.0 min. `WAVCHK device 0 of 1: USB Audio Device
(found after 12 s)`, `waveOutOpen MMSYSERR 0`, `position 88200 of 88200 bytes`, `played 2000 ms of audio in 2073 ms`;
`audio-play` PASS, `urbs-malformed` PASS, and stick-seen, stick-copy and list-removed PASS. `XHNORST no restart question in
120 s`.

**The updates over 3.4.2.0.** Every leg installed `3.4.2.0`, took readings and copies after a cold start, installed `3.4.3.0`
over it, then took readings and copies after the next cold start.

- XP x64 and 7 x64 (every device connected throughout):
  - On `3.4.2.0`: exit 3, every device Code 0, both images SAME AS 3.4.2.0 and RUNNING, the signature verified, `STICKMX` and
    `UASMX` copies PASS.
  - The update: the plan read "driver xhci98 3.4.2.0; Update" on all five devices (controller, root hub, hub, composite device,
    UAS disk); exit 3; all five "xhci98 3.4.3.0, started" / "xhciuas 3.4.3.0, started".
  - After the cold start: every device Code 0, both images SAME AS 3.4.3.0 and RUNNING, "Successfully verified", every device
    at `DriverVersion` 3.4.3.0, `DriverVer=11/10/2026,3.4.3.0`, both copies PASS.
- XP and 7 x86 (the bus at the reading starts only):
  - On `3.4.2.0`: exit 3, images SAME, every devnode running, both drives read, written and read back.
  - The update: exit 3; "Updating xHCI98 USB 3 Controller ... done" (7: "done, restart needed"), "Root Hub ... done", "xHCI98
    USB Attached SCSI Storage (not plugged in) ... done".
  - After the cold start: `xhci98.sys`, `xhcihlpr.sys` and `xhciuas.sys` SAME as 3.4.3.0; the controller, root hub and UAS disk
    "Keep (same version)"; every devnode running; both drives read, written and read back.
  - The hub and composite device were not plugged in during the update, so `/list` still reads "driver xhci98 3.4.2.0; Update"
    for them while they run on the new `xhci98.sys`. 47.final recorded the same over 3.4.1.0 ("for the owner to confirm").
- 98 SE:
  - On `3.4.2.0`: exit 3, the log 906 lines; after a cold start (no wizard) 12 of 12 images SAME, `/list` "Reinstall (same
    version)", `/report` "Driver version: 3.4.2.0 running".
  - The update: the rows "driver xhci98 ...; Update", exit 3, the log 1018 lines.
  - After a cold start: 12 of 12 images SAME as 3.4.3.0 (`xhcicm.sys` absent, not chosen), `/list` "Keep (same version)",
    `/report` "Driver version: 3.4.3.0 running", controller and root hub problem 0, the base's own 22 nonzero devnodes unchanged.
  - Plugged: the wizards "USB Mass Storage Device" and "xHCI98 USB Attached SCSI Storage", both finished. RL9XSTICK on E: and
    RL9XUAS on F: each read, written and read back (6 of 6 the same). 9 USB devnodes at problem 0.
  - Each shut down ended by ACPI (no shut down hang this time).

Against 47.final: every update row reads as 47.final's over 3.4.1.0. The x86 `/list` wording changed from "Reinstall (same
version)" to "Keep (same version)". No regression.

**The 43.48 legs.**
- `xp-nonfree` (6 INFs staged) and `w7-nonfree` PASS.
- `me-nonfree` first run: install exit 3, every check PASS, uninstall exit 3. Then at the start after the uninstall the guest
  reached the desktop with the poller's window open, the clock stopped at 10:00 PM and COM1 stayed silent for 900 s
  (`b3-stalled.png`). That matches the 98 SE / ME q35 stops already recorded (45.36; 47.final's ME stop after its uninstall).
  The rerun on a fresh overlay PASSED (704 of 704 files copied, the uninstall exit 3).
- `w2k-nonfree` failed 3 times in 6 runs on 3.4.3.0 and once in 5 on 3.4.2.0, read on the guests' own `setupapi.log`
  (below). Not a regression.

**w2k-nonfree, 6 runs on 3.4.3.0 against 5 on 3.4.2.0** (fresh overlays, the harness unchanged; the staging lines are the same
on both versions: 7 INFs from the 5 packages, and the bus holds only the stick):

| Package | PASS | FAIL, the restart box | FAIL, other |
|---|---|---|---|
| 3.4.3.0 | 3 (`workD`, `workE1`, `workE3`) | 2 (`workB`, `workC` 221014) | 1 (`workE2`: boot 2 stalled) |
| 3.4.2.0 | 4 (`workC` 221613 and 222543, `workF1`, `workF3`) | 1 (`workF2`) | 0 |

In each box failure, Windows 2000's "System Settings Change: Windows 2000 has finished installing new devices. You must restart
your computer ..." was open after the stick was plugged (`b2-last.png`). The uninstall therefore found "Windows is still
installing something after a minute", made no changes and ended with exit 4, and list-removed failed. `C:\WINNT\setupapi.log`
of each failed run (read from its kept disk: `qemu-img convert` to VHD, then `7z`, with no guest started; copies in
`E:\xhci98-scratch\w48\legs343b\dsk\`) names Windows' own stick installs, never a non-free INF:

- `workC` 221014 (3.4.3.0) and `workF2` (3.4.2.0), the same lines: `volume.inf` for
  `STORAGE\REMOVABLEMEDIA\7&19E23281&0&RM`, "Query-removal during install of STORAGE\REMOVABLEMEDIA\... was vetoed by
  STORAGE\RemovableMedia\... (veto type 5)", then "Device required reboot: Query remove failed (install) CfgMgr32 code: 23."
- `workB` (3.4.3.0): `disk.inf` for `USBSTOR\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\XHLEGSTICK01&0`, vetoed by the same
  `STORAGE\RemovableMedia` (veto type 5), "Device required reboot".

Veto type 5 means a handle was open on the stick's volume during its install. `LEGNT.CMD`'s stick-seen check polls `if exist
X:\TESTDATA.BIN` over D: to M: in that window, so the reading is a race between the leg and 2000's storage install, present on
both versions. LEGNT answers the box (`XHNORST.VBS`) only on Audio legs. run-46 had a w2k-nonfree first try end the same way
(exit 4 on a pending install).

`workE2` (3.4.3.0) is a different stop: at boot 2 the desktop came up and the clock ran, but nothing ever came on COM1. The
poller never started: no command window, no XHCITR tray icon either, while `C:\XHLEG\PH2.ON` was present. `setupapi.log` has
nothing at that start (`b2-stalled.png`). Seen once in eleven runs; no cause read.

**Owed / not run.** Every Manual leg of `release-legs.psd1` belongs to legs343. No playback check on XP (one-line change, not
run). No update leg on Vista, 8.1 or 10. No 7 x64 run without the certificate. No real hardware.

VMs: `E:\xhci98-vm\p48\legs343b\`. Every QEMU this worker started has exited; the PIDs are in `pids.txt`, 71 started, none
alive at the end, by a tasklist check.

### Merge (legs343b)

Reading only.
