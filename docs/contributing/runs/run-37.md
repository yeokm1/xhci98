# Phase 37 Record

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 37". The
roadmap entry carries the goal, the status, the task list and the
checkpoint; this file carries what each task did and what each reading
said. Where the two disagree about a clause, the roadmap wins.

**On `out\...` and `vm\...` paths in this file.** They say where a reading
was kept on the host that took it; they are not files a clone has.

## 37.1 - Windows ME on its stock USB stack, no SweetLow

**Verdict: PASS** (2026-10-12). Windows ME needs no USB 2.0 stack under the
host controller driver.

**The vehicle.** A development host with QEMU 11.1.0-rc2, TCG, `pc`,
`pentium3`, one processor, 256 MB, `qemu-xhci,p2=4,p3=4`. The base is the
`winme-clean-install` snapshot of `vm\winme.img` (the stock ME install of
2026-09-02, from before SweetLow's stack was added), copied out read-only to
`vm\t37me\winme-stock-base.qcow2`; the leg ran on its own overlay. Kit
`out\p37-me\kit` (the Phase 36 matrix kit, retargeted); readings, shots and
captures in `out\p37-me\legs\me\` (`notes.txt` is the full list).

**The package** is the `2.3.0.0` asset, `xhci98-2.3.0.0.zip`, SHA-256
`025EA20C95C6D90097720C45B8D266C9B4019B454F422C0724E5BBDF1A1A1EE3` (the hash
recorded at the cut), `release-x86`.

**What was read:**

| Step | Result |
| --- | --- |
| Install | Found New Hardware "PCI Universal Serial Bus", Specify a location `D:\`, "xHCI98 USB 3.x eXtensible Host Controller"; no file asked for; restart requested and taken. The root hub installed silently |
| Controller | `XHCISNAP` (empty bus): driver `2.3.0.0` running, Windows ME 4.90.3000, 8 ports |
| HID mouse | `usb-mouse`, root port, High Speed: installed silently, the pointer moves through it |
| Bulk-Only stick | `usb-storage`, root port, SuperSpeed: ME's own "USB Mass Storage Device", a drive letter, `fc /b` of a 4 MB file clean |
| UAS drive | `usb-uas`, root port, SuperSpeed: the wizard once, `XHCIUAS.INF` from the package, no restart; a drive letter, `fc /b` clean |
| Hub | `usb-hub`: installed silently |
| Composite audio | `usb-audio` behind the hub: the wizard once, "USB Audio Device" with files from ME's own CABs. The sound device appears about a minute after Finish. `TONE.WAV` (20 s, 1 kHz) played whole twice, captured by QEMU's WAV backend: the tone is one unbroken 20.01 s block in both captures (the silent runs `audchk.ps1` lists fall between ME's own system sounds, not inside the tone) |
| Slots | five, all configured (`XHCISNAP`); four `xfer.error` notes, slot 5 DCI 2 completion code 14 (Ring Underrun on the audio's isochronous OUT at stream start and stop), as on the Phase 36 Vista, 7 and XP audio legs |
| Disable, enable | Device Manager, live: Code 22 and every child disabled, no restart asked; enabled, everything back |
| Remove, rescan | Remove: every USB, HID, sound and storage entry gone, no hang. Refresh: everything reinstalled with no question |
| After the sequence | both disks `fc /b` clean; five slots configured, containment none |
| Shut Down | with every device attached: off within 60 s |

**Not read:** an update from an earlier xhci98 on a stock ME, and a UAS
drive on a machine where no Bulk-Only stick was ever plugged (the README's
advice to plug an ordinary stick first stands unread either way).

**A tool note.** The kit's `PNPCTL.EXE` is an NT build (subsystem version
5.1), which ME refuses to start; Device Manager carried the lifecycle steps.

## 37.4 - An event ring check after the re-arm

**Verdict: PASS** (2026-10-12). Under QEMU's MSI-X the backstop no longer
serves the events the interrupter's IE toggle strands; under MSI and the line
the check queues nothing.

**The change.** `f808e22`: `hcdRearmPeek` (`src/hcd_ctl.c`) after an
interrupt-delivered drain, the `HCD_DRAIN_FROM_PEEK` marker so the drain it
queues does not peek again, `RearmPeekPending` and `RearmPeekDrains` before
`TrailingPad`; design record 17 section 4.1, "Amended for 37.4". Codex,
three rounds: round 1 found the first draft's re-peeking drain could chain
DPCs at DISPATCH_LEVEL under sustained completions (fixed by the marker),
round 2 a reversed race in the design text, round 3 no defects. Host tests
2027 checks, 0 failures; the qemu flavour built and gated, x86 and amd64.

**The vehicle.** A development host, QEMU 11.1.0 (installed that day from
the stable `qemu-w64-setup-20260811.exe`, SHA-512 checked; the rc2 build of
37.1 was replaced), TCG, `qemu64`, two processors, 2048 MB,
`qemu-xhci,p2=4,p3=4` with `msi`/`msix` set per leg. Bases
`vm\t28pre\win7-x64-base.qcow2` and `vista-x64-base.qcow2`, each leg on its
own overlay. Kit `out\p37-rrc\kit` (the Phase 36 matrix kit, retargeted);
readings in `out\p37-rrc\legs\<guest>\notes.txt`. Devices: the Bulk-Only
stick on port 1 at boot, a raw UAS disk on port 3 and `usb-audio` on port 4
hot-added. The new build is the qemu flavour of `f808e22`, unsigned, so each
boot took F8's "Disable Driver Signature Enforcement"; the control is the
`2.3.0.0` `release-x64`.

**How the counters were read.** `XHCISNAP` does not decode the two new
words, so they were read from its `.BIN`, the raw extension image: the two
words before `TrailingPad` and the trailing signature. A start zeroes them,
so a snapshot after a controller restart covers only that start.

**What was read** (load: TONE.WAV three times, the stick copied and compared,
the UAS disk written 64 MiB in 1 MiB blocks and verified four times):

| Guest | Build | Delivery | BackstopDrains | Peek pending / drains | TONE | UAS VERIFY | Result |
|---|---|---|---|---|---|---|---|
| 7 x64 | 37.4 | MSI-X | 0 | 487 / 226 | 20.26, 20.31 s (first play 59.5 s, cold); Ring Underrun 1-3 | 97.3-101.1 MB/s | PASS |
| 7 x64 | 2.3.0.0 | MSI-X | 172 | - | 24.87, 27.47, 26.51 s; Ring Underrun 9-42 | 3.6-4.1 MB/s | control |
| 7 x64 | 37.4 | MSI | 0 | 869 / 0 | 21.34, 20.27, 20.36 s | 109.0-115.3 MB/s | PASS |
| 7 x64 | 37.4 | line | 0 | 815 / 0 | 21.09, 20.26, 20.32 s | 109.7-115.8 MB/s | PASS |
| Vista x64 | 37.4 | MSI-X | 0 | 1681 / 204 | capture: 3 plays in 62.76 s, 12 gaps of 20.7 ms | 94.4-103.6 MB/s | PASS |
| Vista x64 | 2.3.0.0 | MSI-X | 263 | - | capture: 3 plays in 63.14 s, 70 gaps (one 990.7 ms) | 4.2-5.3 MB/s | control |
| Vista x64 | 37.4 | MSI | 0 | 1811 / 0 | capture: 7 gaps, two of them the pauses between plays | 110.3-113.5 MB/s | PASS |
| Vista x64 | 37.4 | line | 0 | 1562 / 0 | capture: 9 gaps, two of them the pauses between plays | 89.7-102.5 MB/s | PASS |

Every stick compare was clean and every VERIFY read 0 sectors bad.

**What it says.**

- QEMU 11.1.0 still drops the MSI-X message raised across the IE toggle:
  the `2.3.0.0` control counts 172 and 263 backstop drains, as Phase 36 read
  on 11.1.0-rc2 (`runs/run-36.md`, "MSI-X BackstopDrains (NT6)"), and with
  them its UAS reads run at about 4 MB/s and its audio gaps.
- With 37.4 the same load counts no backstop drain; the peek queued 226 and
  204 drains in their place, and the UAS reads and the tone match MSI's.
- Under MSI and the line the peek saw events at the dequeue but queued
  nothing: the interrupt had already queued the drain each time, which is
  the cost record 17 section 4.1 accepts for a delivering interrupter.

**Harness notes, not the driver.** Vista has no PowerShell, so its tones
were played by a WMPlayer.OCX script and read from QEMU's WAV capture (the
guest's clock runs slow under TCG). Disk numbers move between boots; one
XFERTEST aimed at the stick was refused (error 5) and was rerun on the UAS
disk. Vista's autoplay window on the stick vetoed one controller disable.
The repository's untracked `xhcisnap\XHCISNAP.EXE` was a stale `2.2.0.0`
build and failed the build's self-test gate until rebuilt.

**Not read:** a real MSI-X controller (37.14 kept that bench item; it is
BF.6 in "Planned work" since 2026-10-12), and Windows 7 and Vista x86,
whose MSI-X goes through `xhcimsi.sys`.


## The roadmap entry before it was condensed (2026-10-12)

On 2026-10-12 the owner had Phase 37's roadmap entry cut to one line a
task (`roadmap-hcd.md`, "Phase 37"), as the earlier phases' are. The entry
as it stood then follows verbatim, so the decisions, the reviewed design
facts and the evidence pointers inside the task text stay on record.

**Phase 37 - Release `2.3.1.0`: Windows ME Without SweetLow's Stack and the X230's XP x64 Setup Stop**

Goal: Windows ME read under the HCD with no USB 2.0 stack installed; the
X230's STOP 0x7B in XP x64 text-mode setup explained and fixed where the
driver is at fault.

Status: open. Opened 2026-10-12 (owner), the day Phase 36 closed; its
release is `2.3.1.0`, on branch `2.3.1.0` (owner, 2026-10-12; named
`next-release` until then). It opened with every
item that was "Planned work" when Phase 36 closed and three field reports
(the Dell Latitude E7250, the Lenovo ThinkPad X230 and the HP EliteBook
2570p). The same day it was cut back and renumbered (owner): the 2570p's
report and the Linux quirk population were removed; every other open task
except the X230's setup stop moved to "Planned work" at the end of this
file, under the placeholder id it had before or a new one (the field
reports' batch files last, so the release goes out sooner); and the tasks
left were numbered in order. The decisions table's Phase 37 row maps the
old numbers to the new. 37.3, 37.4 and 37.5 are done, with legs owed.

Why a phase: the Windows ME reading may change a documented requirement,
and the X230's report is on a machine nobody here holds.

- [x] 37.1 Windows ME on its stock USB stack, no SweetLow (owner, 2026-10-12). Every Windows ME reading so far, under the miniport and under the HCD, was taken under SweetLow's USB 2.0 stack (release notes, "Requirements"); the miniport needed it for `usbport.sys`, which stock ME lacks (`AGENTS.md`, the Windows ME paragraph: Code 2 on a stock ME). The HCD replaces the port and hub drivers and runs on a stock Windows 98 SE, so a stock ME is expected to work, but has not been read. Leg: `vm/winme.img` from `winme-clean-install`, the `2.3.0.0` package from the asset, and the SweetLow leg's reading: the controller and root hub started, a hub, a HID mouse, a composite audio device, a Bulk-Only stick on ME's own `USBSTOR`, a UAS drive, and disable, enable, remove and rescan. If it passes, the release notes ("Requirements", "Windows 98 SE and ME: the stock install, and storage", the update route) and `AGENTS.md` say ME needs no USB 2.0 stack; if not, the requirement is stated as measured. **PASSED 2026-10-12** (`runs/run-37.md`): every clause on the stock ME guest from the asset, the tone played whole in two WAV captures, Shut Down clean; the release notes, `README.md`, `AGENTS.md`, the acceptance test and the readme template now say ME needs no USB 2.0 stack. Not read: an update from an earlier xhci98 on a stock ME
- [ ] 37.2 Lenovo ThinkPad X230: a bugcheck while installing Windows XP x64 from the ISO; the driver installed on the finished XP x64 system works (a user report, owner's summary, 2026-10-12). The fault is in the setup path only. The reporter's photo (2026-10-12), with the driver integrated into the ISO: `STOP 0x0000007B (0xFFFFFADF338393C0, 0xFFFFFFFFC0000034, 0, 0)`, INACCESSIBLE_BOOT_DEVICE with STATUS_OBJECT_NAME_NOT_FOUND - the boot volume's device was not found, the symptom of a disk-controller driver missing (an AHCI SATA controller XP x64 has no driver for) rather than of a USB driver failing. Awaiting: the setup stage it stops at (text mode, or the first boot after it), the BIOS SATA mode (AHCI or Compatibility) for both installs, whether the same ISO without xhci98 installs in the same BIOS setting, how the driver was integrated (tool, F6 or `txtsetup.sif`), and the driver version. **Reproduced 2026-10-12 on the owner's E7250 with the reporter's ISO** (the QEMU kit in git-ignored `out\p37-3\`): an nLite build whose `xhci98.sys` is `2.3.0.0` `release-x64` byte for byte, integrated as `txtsetup.oem` describes, booted from a WinSetupFromUSB stick whose `TXTSETUP.SIF` makes the USB drivers boot drivers and names the stick the boot device. The stop is at text-mode start and is the stick, not the AHCI disk: with "Enable USB 3.0 Controller" off (stick on EHCI) text mode starts, and with it on and xhci98 taken out of setup's list the same 0x7B comes, so the stick is on xHCI. Under QEMU the same boot through xhci98 with a Bulk-Only `usb-storage` stick reaches the partition screen. **Cause: a port reset the firmware left running.** A diagnostic build that printed the bus's state from a bugcheck callback onto the 0x7B screen (a throwaway, never in the tree) read: the BIOS handed over cleanly (USBLEGSUP OS-owned), only the start's No Op was ever issued, the root hub's first answer went out empty after 125 ms, and root port 13 was FAILED with `XHCI_ENUM_FAIL_RESET` after its one retry while its PORTSC read connected, enabled, U0, SuperSpeed. The port had PR set at the driver's first look (the installed system's `XHCISNAP` shows the same PR, ended there before the reset step); `XhciLinkDecide` refuses a reset while one is running, so `hcdUsb3ResetWrite` returned 0 at once, both attempts failed inside one debounce, and the boot stick was never presented. QEMU never shows it: its resets end at once and SeaBIOS leaves none running. Fixes: `hcdAwaitRunningReset` waits a running reset out, within the warm reset's budget, and acknowledges its PRC and WRC before the driver's own reset (log note `enum.port.prwait`); and, needed next for this stick, an unread `XhciForceBulkOnly` is 1 (`HcdCtlForceBulkOnly`, `dfb56bb4`) - 31-A.3 gives a SuperSpeed device offering UAS only UAS's ids, the installed system ran this stick under UAS (four endpoints), and text mode has `usbstor` for Bulk-Only ids alone and no `xhciuas.sys`; every INF path writes 0, so installed systems keep UAS. **Read 2026-10-12 on the E7250** (both fixes, with the diagnostic screen): text mode reached the partition screen with the USB 3.0 controller on. **Read to the desktop 2026-10-12** on the E7250 (owner): the `release-x64` build of `2072d222` on the stick, the same settings, Setup through text mode and GUI mode to the desktop, Device Manager with no device marked. Owed: the X230 reporter's reading.
- [x] 37.3 (was S5L.1) the S5 shutdown latch (orchestrator, 2026-10-11; owner confirmed 2026-10-11), so no transfer, command or port request can wait forever after the controller's D3 at shutdown. Hardening, not tied to an observed hang: Phase 36's matrix traced Windows ME's Shut Down hang with an unopened USB disk to ME itself (the same hang on ME's own UHCI + USBSTOR stack), but the code has the gap (`2.2.0.0` too): after the D3 a transfer URB waits on the halted ring for a resume (`hcd_io.c`, HcdIoMapped), command URBs and RESET_PORT wait on HcdCfgService, skipped while unpowered (`hcd_enum.c`), and at S5 no resume comes. The latch: at a controller Dx whose system state is PowerSystemShutdown, including the direct-transition path, set it before the halt; after it, refuse new transfer and command URBs with `USBD_STATUS_DEVICE_NOT_RESPONDING` through HcdIoRefuseLater (never DEVICE_GONE, which HcdIoDeferred parks), complete RESET_PORT inline (its IRP carries no URB, and the refusal DPC reads Argument1 as one), and after the halt flush what is queued with the same status. S3 and S4 keep hold-until-resume. A full Codex loop under record 13 section 5's lock model, IRQL tags, stage A PowerDown and 36.35's no-thread-creation rule. Legs: shutdown with devices on ME, 98 SE (the 36.35 case), 2000, XP and 7 x64. Implementable part done 2026-10-12 (`232f9fef`..`e5b24e9f`; design record 13 section 5.8, which owns it, with a sentence in record 14 section 4.4): `ShutLatched` (appended at the end of `HCD_CONTROLLER`) set with `PowerDown` before the halt, also where the shutdown finds the controller already down (`HCD_POWER_WORK_SHUT`); a D0 or a PnP start clears it; the transfer flush only on `USBSTS` HCH read on a started, mapped controller in D0; pure `XhciTolShutLatch` and `XhciTolShutFlush` with host vectors in `test_tol`; log note `power.shut`. Codex thread `01a12fdc`: round 1 (design) two P1 and one P3, round 2 one P1, one P2 and one P3, round 3 nothing. Built x86 all and amd64 all with every gate. Smoke leg: Windows 98 SE `release`, a boot-present stick and a bound HID mouse, Start > Shut Down powered off twice (development host A, QEMU 11.1.0). Legs owed to the matrix: shutdown with devices on ME, 98 SE (with the selective-suspend client, the 36.35 case), 2000, XP and 7 x64
- [x] 37.4 (was RRC.1) an event ring check after the re-arm, so an event stranded while IMAN.IE was clear is served without waiting for the 100 ms backstop. Phase 36's matrix found QEMU's MSI-X loses that interrupt (Vista and 7 under `msix=on`: `BackstopDrains` 3 to 288 a session, 0 under MSI, the line and polling; `xhci_intr_update` raises before it marks the vector used again, and `msix_notify` returns on an unused vector before its mask and PBA check). Codex (2026-10-11) read no lost-interrupt window on compliant hardware, so it is a QEMU defect and `2.3.0.0` shipped unchanged (orchestrator, owner to confirm). The check: after the DPC's re-arm, read the next event's cycle bit and, if an event is pending, queue one more bounded drain through the existing admission and accounting (`hcd_ctl.c`), never a loop under the controller lock; IE stays 0 in the ISR, so record 05's DIRQL exception is untouched; Codex's preferred option over re-arming before the final ERDP write or a blind second ERDP write. Legs: 7 x64 and Vista x64 under MSI-X (`BackstopDrains` 0, TONE and UAS timings against MSI), and the line and MSI rows unchanged. **PASSED 2026-10-12** (`f808e22`, Codex clean at round 3; `runs/run-37.md`): on QEMU 11.1.0 under MSI-X, `BackstopDrains` 0 on 7 x64 and Vista x64 with the peek queuing 226 and 204 drains, UAS VERIFY about 100 MB/s and the tone as under MSI, against the `2.3.0.0` control's 172 and 263 backstop drains and about 4 MB/s on the same load; under MSI and the line, `BackstopDrains` 0 and the peek queued nothing. Not read: a real MSI-X controller (BF.6), 7 and Vista x86
- [x] 37.5 the Intel port switchover's scripts in a directory of their own (owner, 2026-10-12). Since `2.3.0.0` a cut publishes `portsw.vbs`, `portsw-on.bat` and `portsw-off.bat` at the root of each of the four flavour directories (`$pswPublishable`, `make-release.ps1`; staged by `make-package.ps1`, tasks 36.42 and 36.56), four byte-identical copies beside the INFs, though the script is architecture- and flavour-neutral and the setup engine never reads it. They move to one `portsw\` directory at the version root, beside `xhciqual\` and `xhcisnap\`, and leave the flavour directories. The work: `make-package.ps1` stops staging them, `make-release.ps1` publishes them once into `portsw\` and its acceptance of a published version keeps accepting the old layout (a version before `2.3.0.0` has none, `2.3.0.0` has them per flavour; published releases are not rewritten), `test-package.ps1` and the INF gate follow wherever they check them, and the readme template (the contents list, section 3's file table and section 9's "beside the INF in each RELEASE and DEBUG directory") says where they now are; the user documents (`README.md`, `docs/using/release-notes.md`, `docs/using/release-acceptance-test.md`) and the contributor ones (`build-and-test.md`, `source-files.md`, design record 16) the same. The scripts' logic does not change: `portsw.vbs` finds the controllers through the registry, not the directory it runs from, and the batch files look for it beside themselves (`cd /d "%~dp0"` under cmd.exe; under COMMAND.COM on 98 SE and ME, the current directory, so the user must be in `portsw\`). Only their words do: the batch files' "not found" message sends the user to "the package directory that holds portsw.vbs", and their comments say it is beside the INF. FR.2's batches, if they ship as files beside the INF, take the same question. Legs: a local cut's tree read (one `portsw\`, no copies in the flavour directories, the readme's paths true), the package and release tests, and `portsw-on.bat`, `portsw-off.bat` and `portsw.vbs show` run from `portsw\` on 98 SE, ME, 2000, XP, XP x64 and 7. **Implemented 2026-10-12, VM legs owed** (`bc003002`, review fix `92a20bb1`; Codex clean at round 2; record 16 revision 6): `make-release.ps1` publishes the three once from `src\` into `portsw\` (`Publish-PortSwitchScripts`) and holds the staged tree to it before the publish swap (`Assert-PortSwitchLayout`); the upload assembly accepts a tree without `portsw\` only in the layout its version was published with (none before `2.3.0.0`, per flavour in `2.3.0.0`; `-UploadSetOnly` of the real `2.3.0.0` read); `make-package.ps1` stages none and loses `-PswScriptPath`, so a development package carries none either (a leg copies `portsw\` from a cut or the files from `src\`); no `readme.txt` in `portsw\`. Read: a local x86 cut's tree (one `portsw\`, no copies in the flavour directories; the readme's contents, section 3 and section 9 true, 78 columns), the package and INF gate self-tests, both builds. Owed: the scripts run from `portsw\` on 98 SE, ME, 2000, XP, XP x64 and 7, and the two-architecture cut's tree at 37.final
- [ ] 37.final the builds, cut and check of the release this phase ends in, its version the owner's to choose

Checkpoint: 37.1 read on a stock Windows ME and the documents saying what
it found; 37.2 fixed, read on the E7250 and explained to the reporter;
37.3's and 37.5's owed legs read; the bench work in "Planned work" still listed under "Untested
ground" in the README and the release notes; and the install legs read
from the asset.

Records: `runs/run-36.md` ("The owner's bench, 2026-10-11", and "Moved
from the roadmap at the phase's close"); design records 13, 14, 15 and 17;
GitHub issue 17; `docs/using/release-notes.md`.
