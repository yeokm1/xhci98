# Development Roadmap - Phases 17 Onward

The second half of the roadmap: every phase after the initial release. The
first half, [`roadmap.md`](roadmap.md), is the entry point. It carries the
project-status paragraph, the batching convention and task-id rules, the phase
sequence, Phases 0-16 (everything up to the `1.0.0.0` release, the move to
specification revision 1.2c and the unattended post-release run), and the
hand-run acceptance reminder the roadmap ends on. "The roadmap" in another
document, a script or a source comment means the two files together; a phase
or task id says which one holds it, 17 and later being here.

A new phase is added at the end of this file, and its status is written into
`roadmap.md`'s "Current status" and "Phase sequence" in the same change.

---

## Phase 17 - The OS Supplies `usbd.sys` and `usbhub.sys`

Goal: the release download carries the driver's own files, the tools and the
readme, and nothing of Microsoft's. `src/xhci98.inf` asks the Windows setup
engine, through `LayoutFile=layout.inf`, to copy `usbd.sys` (both targets)
and `usbhub.sys` (Windows 98 only) from the operating system's own install
source, with `COPYFLG_NO_OVERWRITE` so a file already on the machine is never
touched. The driver code is unchanged; `xhci98.sys` is rebuilt only because
its version resource must match the INF's `DriverVer`.

Status: closed on 2026-09-02, the day it opened, every task done or observed
that evening and the cut deferred to Phase 18. The decision is the owner's,
taken after the SweetLow-stack work measured what each stack needs:
`usbd.sys` is required under every USB 2.0 stack on both targets because
`usbhub20.sys` imports it by name, `usbhub.sys` is required under NUSB's
stack and inert under SweetLow's, and both are the OS's own files. Release
`1.0.0.0` carried them on the media under per-target names; that exception
(`legal-provenance.md` section 5) was withdrawn before any upload.

Why a phase: it changes the install procedure a user follows, the packaging
scripts and gates, the provenance record, and every user-facing statement
about what the download holds. It changes no driver behaviour, which is why
its checkpoint is an install reading rather than a device reading.

Tasks:

- [x] 17.0 record the decision in `legal-provenance.md` section 5 before any
  script change, pointed at from `AGENTS.md`. Done 2026-09-02.
- [x] 17.1 prove the mechanism in the VMs, the owner at the console, all on
  2026-09-02: (a) Windows 98 under SweetLow's stack with no driver, no
  `usbd.sys`, no `usbhub.sys` and no CABs: the install raised the engine's
  own `Insert Disk` prompt naming the Windows 98 Second Edition CD-ROM, and
  after a relaunch the driver registered, `StartController` ran and the
  keep-alive mouse bound; (b) Windows 98 under NUSB 3.3's stack (a fresh
  `post-nusb` clone): the same prompt, the 1.0.0.1 build under NUSB's
  usbport (`USBPORT_GetHciMn=57324B30`), the mouse bound, and a hot-plugged
  `usb-audio` as "USB Composite Device" with "USB Audio Device" beneath, the
  `usbhub.sys` half of the route; (c) Windows 2000 SP4 (a fresh
  `phase2b-clean` clone), Have Disk: no prompt, started without a reboot,
  root hub and HID mouse bound.
- [x] 17.2 the change: the INF's four directive edits, `DriverVer` and
  `src/xhci_version.h` at `1.0.0.1`; the INF gate's `TGT-*` and `W98-*`
  families replaced by the `OS-*` rules and `PKG-MSFILE` with self-tests;
  `make-package.ps1`, `make-release.ps1` and `test-package.ps1` without the
  Microsoft files and the source manifest; every document the change
  touches, including the statement that the Windows 98 SE CD may be asked
  for; the `1.0.0.1` entry in `releases/history.md`.

Checkpoint: the package `make-package.ps1` assembles holds `xhci98.sys` and
`xhci98.inf` and no other file; the INF gate and its self-tests are green on
the new shape; and the install with the OS supplying the two files has been
observed on Windows 98 under both USB 2.0 stacks and on Windows 2000, in the
VMs, with the root hub up afterwards.

Records: `legal-provenance.md` section 5; `build-and-test.md` ("The files
the OS supplies: `usbport.sys`, `usbd.sys`, `usbhub.sys` and `usbui.dll`",
"The SweetLow stack");
`releases/history.md`.

## Phase 18 - Release `1.0.0.1`: Windows ME, and the Cut

Goal: the driver observed on a Windows ME guest with its standing stated in
every document that names the targets, and `1.0.0.1` cut carrying Phase 17's
install change together with the Windows ME support that observation
justifies.

Status: closed on 2026-09-02, the day it opened, by the owner, who decided
that morning that Windows ME support is part of `1.0.0.1`. The guest was
installed and observed under SweetLow's stack only (the owner's decision:
NUSB is a Windows 98 SE package), the tier decided as supported in virtual
machines, stated the way Windows 2000's status is, and `1.0.0.1` cut and
its install route checked from the asset on all three targets. Nothing has
run on Windows ME on real hardware.

Why a phase: Windows ME is the same 16-bit setup engine and VxD-hosted WDM
model as Windows 98 SE, so the INF's undecorated half is the half it reads,
but its CD carries no USB 2.0 stack and the import gate held no Windows ME
evidence, so the load itself was the first thing to observe, and what the
observation justifies decides how every document names the targets.

Tasks:

- [x] 18.1 install a Windows ME guest by hand (`vm\winme.img`, snapshot
  `winme-clean-install`), then SweetLow's stack from the transfer drive.
  Done 2026-09-02. Two vehicle facts, recorded in `build-and-test.md` and
  `lessons.md`: the Windows ME CD's own `FORMAT C:` never writes a sector
  under QEMU, so the format is taken from the Windows 98 SE CD's boot floppy
  with the ME CD as the second CD-ROM (`setup-qemu.ps1 -WinMeIso -Win98Iso`
  writes that launcher); and every guest-initiated restart wedges at the
  logo as Windows 98's does, LINT0 masked after the warm reset, so every
  restart is a shutdown and a cold launch. Observed first on the stock
  stack: the package installs and the controller shows Code 2 with
  `DriverEntry` never run, `usbport.sys` being absent.
- [x] 18.2 the driver through the INF (`prepare-image.ps1 -Target 2e -Boot
  -Xfer -XferPackage`): no CD asked for (the OEM Setup leaves the CABs on
  the hard disk), and after a cold start `DriverEntry`,
  `USBPORT_GetHciMn=10000001`, `USBPORT_RegisterUSBPortDriver status=0`,
  `StartController`, the `RH_*` family, and the keep-alive mouse bound.
- [x] 18.3 HID, mass storage and one composite device: the mouse at boot,
  `-Attach storage` bound with no wizard, a hot-plugged `usb-audio` bound as
  "Composite Device" (Windows ME's own `usbccgp` parent) with "USB Audio
  Device" under Sound; no refusal counter moved.
- [x] 18.4 the tier, decided by the owner: supported in virtual machines,
  under SweetLow's stack only, no checkpoint tax; stated in `AGENTS.md`,
  `README.md`, the release notes, both issue forms, the INF header comment,
  the generated `readme.txt` and the acceptance test (rows 4.5, 7.7, 7.8).
- [x] 18.5 first-class only: not applicable.
- [x] 18.6 the `1.0.0.1` history entry carries the Windows ME line; the cut
  fell on the date the three fields already carried, so none moved.
- [x] 18.7 cut `1.0.0.1` with `make-release.ps1`, every gate green
  (re-cut the same day for readme wording, before any upload):
  `releases/1.0.0.1/` and `out\xhci98-1.0.0.1.zip` (245,067 B), the two
  files per flavour, the two tools with their readmes and NOTICEs, `LICENSE`
  and `readme.txt`, nothing else. The published `xhci98.sys` differs from
  `1.0.0.0`'s only in timestamps, checksum and version resource (22 bytes),
  so the release changes the install route, and that was run from the asset
  the same night, the owner at the console: Windows 98 SE (a fresh
  `post-nusb` clone, no CABs) asked for `usbd.sys` with the CD prompt and
  came up with the root hub clean and the mouse enumerated; Windows 2000
  SP4 (a fresh `phase2b-clean` clone) asked for nothing, root hub and mouse
  working; Windows ME (`winme-clean-install`, SweetLow's stack first) asked
  for nothing, controller, root hub and HID clean. The Windows 98 run with
  the CABs present was not made (no image carries them), and the nine-step
  acceptance test is the post-release reminder `roadmap.md` ends on, taken
  from the published download.

Checkpoint: a Windows ME guest boots the driver, the root hub comes up, a HID
device and a mass-storage device work, and the tier is stated; and the asset
`make-release.ps1` assembles for `1.0.0.1` holds `xhci98.sys`, `xhci98.inf`,
the two tools and the readmes and no other file, with the install route
checked on each target from that asset.

Records: `build-and-test.md` ("Windows ME target VM"); `lessons.md`
("Windows ME on QEMU"); `scripts/vm-matrix/README.md`; `releases/history.md`.
## Phase 19 - Release `1.0.1.0`: Windows XP, and the NT Install Fixes

Goal: the driver observed on a 32-bit Windows XP guest with its standing
stated in every document that names the targets, and `1.0.1.0` cut carrying
the two INF changes that guest showed an xHCI-only NT machine needs (the
operating system supplying `usbport.sys`, `usbd.sys` and `usbhub.sys` on the
NT install path, and that path disabling usbport's idle suspend as the
Windows 98 path already does), together with one driver change, issue 4's
identity check on the EP0 REMOVE path.

Status: closed on 2026-09-04, its second day, by the owner. It opened on
2026-09-03 as `1.0.0.2`, when the owner asked whether XP could be supported
and installed the guest by hand that afternoon, and was renumbered to
`1.0.1.0` the same night because task 19.7 makes it a driver code change
(the third field moves for code, the fourth for install media and
documents). XP is supported in virtual machines, stated the way Windows 2000
and Windows ME are, and `1.0.1.0` was cut and its install route read from
the asset on all five targets. Nothing has run on XP on real hardware; the
upload and the push are the owner's.

Why a phase: `win98-wdm.md` had kept XP best-effort for cost, and the first
guest settled the runtime side in an afternoon: the driver registered and
ran under XP's own usbport once that file was on the disk. What the guest
also showed were two install-time gaps that belong to the NT path, not to
XP. An NT install that never saw a USB controller has no `usbport.sys`
(both targets' `layout.inf` give it, `usbhub.sys`, `usbehci.sys` and
`usbd.sys` the disposition Setup does not copy, so the file reaches the disk
only when a controller's install pulls it from the driver cache), and on an
xHCI-only machine the package installed but could not load, Code 39; every
Windows 2000 vehicle in this project had carried an EHCI that hid it. And
XP's usbport idles a controller with nothing attached about thirty seconds
after start, after which a hot-plugged device is invisible, the reading the
SweetLow record had predicted for an XP-lineage usbport without
`DisableSelectiveSuspend`. Fixing both changes the INF, its gate, the
release notes and every statement about what the OS supplies, and needs an
install reading on an xHCI-only guest of each NT target, which no existing
image could give.

Tasks:

- [x] 19.0 the XP guest recorded before anything changed: `build-and-test.md`
  "Windows XP target VM" (WHPX, the two launchers from
  `scripts\setup-qemu-winxp.ps1`, the Code 39 reading, the suspend reading),
  `lessons.md` with the `layout.inf` disposition table, `win98-wdm.md`, and
  the SweetLow table's suspend row. Done 2026-09-03.
- [x] 19.1 the INF, NT path, file placement: `[Xhci.CopyNT]` copies
  `usbport.sys`, `usbd.sys` and `usbhub.sys` through `LayoutFile` (the
  owner's instruction); in the gate `OS-ONWIN2K` retired, `OS-MISSING`
  extended to the three, `OS-ONWIN98` keeping `usbport.sys` off the Windows
  98 path (its `layout.inf` cannot resolve it), `OS-NEVER` for `usbhub20.sys`
  on no path (the owner's decision: Windows 2000's own `USB.INF` places it
  when usbport creates the root hub PDO). Landed 2026-09-03.
- [x] 19.2 the INF, NT path, idle suspend: `Services\USB\DisableSelectiveSuspend`
  written from `[Xhci.Dev.NTx86]` and `[DefaultInstall.NTx86]` as the 9x
  path has done since `1.0.0.0`, the gate requiring it once per route on
  both targets; `HcDisableSelectiveSuspend` considered and not taken (under
  NUSB the per-controller value alone still idled the controller - a single
  2026-08-13 boot that did not reproduce: on 2026-09-16 the value alone
  stopped the idle in two boots under NUSB and two under SweetLow's stack, and
  the binaries agree; issue 5 section 4). Read
  first with the value hand-set, then from the 19.4 package install: no
  `SuspendController` in two minutes idle, and a mouse hot-plugged after
  them bound.
- [x] 19.3 the XP readings on the qemu flavour (run `p194`): the Device
  Manager door sequence (disable, enable, remove, rescan) clean, where
  Windows 98 under NUSB bugchecks; `usb-storage` formatted, written and read
  back; `usb-audio` bound as a composite with its isochronous endpoint
  opened from the descriptor. Both bound on their second attach only: on
  the first, XP re-created the device through a second usbport device
  handle and removed the first handle's EP0 last, which the REMOVE path
  read as the live pipe closing
  (`docs/issues/04-xp-restore-device-ep0-remove.md`). The default 4+4 port
  layout had put the SuperSpeed-capable disk on an unmanaged USB3 port, so
  the launcher defaults to `p3=0` since.
- [x] 19.4 the fix observed on XP: `winxp-clean-install`, the launcher with
  no EHCI, Have Disk from the transfer drive: no CD prompt, no reboot,
  `usbport.sys` and `usbhub.sys` placed from `sp3.cab` beside XP's
  `usbd.sys` stub, the driver loaded on that boot, "USB Root Hub" installed
  by XP's own `usbport.inf`, the value under `Services\USB`, no
  `SuspendController` in two minutes, and the hot-plugged mouse bound.
- [x] 19.5 the fix observed on Windows 2000: a fresh SP4 install with no USB
  controller (`vm\win2k-xonly.img`, `win2k-xonly-clean-install`, the new
  `2b-fresh` base; `scripts\setup-qemu-win2k-xonly.ps1`). Its disk, read
  from the snapshot without a boot: `usbcamd.sys` and `usbintel.sys` and no
  other `usb*.sys`, not even `usbd.sys`; the four stack files in `sp4.cab`.
  The package install with the xHCI alone (run `p195`): no CD prompt, no
  reboot, `usbport.sys`, `usbd.sys` and `usbhub.sys` from the cab, "USB 2.0
  Root Hub" up through the OS's own `USB.INF` placing `usbhub20.sys`, a
  mouse bound, every refusal counter at zero.
- [x] 19.6 the tier, decided by the owner on 2026-09-03 night: supported in
  virtual machines, stated the way Windows 2000 and Windows ME are, in
  `AGENTS.md`, `README.md`, the release notes, the INF header comment, the
  `readme.txt` template, the acceptance test (rows 4.6 and 7.9 to 7.12),
  `win98-wdm.md` and `build-and-test.md`.
- [x] 19.7 issue 4, the one driver change (the owner's decision of 2026-09-03
  evening, reversing the afternoon's): in `XhciSlotSetEndpointState`'s
  REMOVE branch for the default pipe, a REMOVE whose extension is neither
  NULL nor the one the record is bound to names a superseded handle, so it
  closes that extension alone and counts `Ep0RemovesSuperseded`, leaving
  the binding, the owed invalidate, the EP0 queue and any pending
  SET_ADDRESS to the live handle. Two host vectors model the `p194` order
  and failed on the old path exactly as the run did. The closing
  reading (run `i4b`, a clean-snapshot reinstall): the restore recurred on
  the first attach of both `usb-storage` and `usb-audio`, the counter moved
  to 1 and 2, and both bound with every failure counter at zero. Both
  primary targets on the same binary (`run-matrix.ps1` on 2a and 2b, the
  Windows 98 door sequence on the SweetLow guest; `out\phase10\matrix-19.7-*.txt`
  and `prep-2a-sweetlow-debugcon-19.7-door.log`): the counter at zero
  throughout, nothing changed. What the runs left is a correlation: every
  first attach that took the restore path was also the first-ever install
  of that class driver.
- [x] 19.8 the primary targets unchanged: `run-matrix.ps1 -PostRelease` on
  freshly re-taken 2a and 2b clones, 2b from the xHCI-only base: `2b-fresh`
  PASS, 17 rows, 6 NODRIVER expected, 0 against, the report identical to
  Phase 16's outside its header; `2a-fresh` PASS, 17 rows, 5 NODRIVER
  expected, 3 not reached (the declared exclusions), 0 against, identical
  to Phase 16's except that the `usb-audio/fs` replug leg passed where
  Phase 16 read the Insert Disk failure the release notes carry, one better
  reading recorded and not promoted. A first run had read that row ERROR on
  both targets because `device_add usb-audio` with no `audiodev=` opened
  QEMU's default host backend and stalled the monitor; the run declares
  `-audiodev none` since. Reports in
  `docs\contributing\runs\run-19-post-release\`.
- [x] 19.9 the cut, 2026-09-04 (re-cut the same morning
  for the readme's opening paragraph, before any upload): the date in
  `src\xhci_version.h`, the INF's `DriverVer`, the history heading and the
  release notes; `build-driver.cmd all` and both tools rebuilt after the
  header, every gate green, `make-release.ps1` exit 0: `releases\1.0.1.0\`
  and `out\xhci98-1.0.1.0.zip` (244,237 B, thirteen files, no Microsoft
  file). The install route from that asset the same morning, the owner at
  the console: Windows 98 SE under NUSB (a fresh `post-nusb` clone) asked
  for `usbd.sys` with the CD prompt and came up with the root hub clean and
  the mouse bound; Windows 98 SE under SweetLow (over the installed driver)
  and Windows ME asked for nothing, controller, root hub and HID clean;
  Windows 2000 (a fresh clone of the xHCI-only base) asked for nothing,
  root hub and mouse working; XP (`winxp-clean-install`) asked for nothing,
  a mouse attached after the install bound with no Refresh, and a
  mass-storage device bound on its first-ever attach, disk and volume in
  turn. Screens in `out\post-release\1.0.1.0\asset-legs\`.

Checkpoint: on an XP guest that has never had another USB controller, the
`1.0.1.0` package installs from the asset, the driver loads on the first
boot with `usbport.sys` supplied by the OS from its own cache, the root hub
comes up, a HID device hot-plugged after the idle window binds, and a
mass-storage device works on its first attach (issue 4); the same install
reading on a Windows 2000 SP4 guest that has never had another USB
controller; the three 9x-family install routes unchanged from the asset;
the tier stated; and the asset holds `xhci98.sys`, `xhci98.inf`, the two
tools and the readmes and no other file.

Records: `build-and-test.md` ("Windows XP target VM", "Windows 2000
xHCI-only VM", "The files the OS supplies"); `lessons.md`; `win98-wdm.md`
("What about Windows XP?"); `usbport-miniport-interface.md` ("The SweetLow
rebuild"); `docs/issues/04-xp-restore-device-ep0-remove.md`;
`scripts/inf-gate/`; `releases/history.md`;
`docs/contributing/runs/run-19-post-release/`.

## Phase 20 - Release `1.0.2.0`: The 2026-09-05 Audit Fixes

Goal: every finding of the 2026-09-05 repository audit (no
critical defect; nineteen findings F1-F19 and six documentation groups D1-D6)
either fixed with the regression vector that pins it or recorded as an owner
decision with its reason; the gates green; both targets' post-release matrix
no worse than Phase 19's; and the result cut as `1.0.2.0`.

Status: closed on 2026-09-07 on the cut and its guest readings. It opened on
2026-09-05 on branch `phase-20`, renamed to `1.0.2.0` on 2026-09-06, the
third field moving because the phase carries driver code changes. Every gate
is green, the version is cut, the install route from the published download
reads clean on four targets, and the confirming matrix on this release's own
driver matches 20.8's reports but for one transfer count. One checkpoint
clause is met on the development machine rather than in a guest: no guest has
been made to produce the state this release's new control-endpoint refusal
guards, so the guests read its other half, that a legitimate reopen is not
refused. The acceptance test from the download, the upload and the push
remain, and are the owner's alone.

The install gains one file: the OS supplies `usbui.dll` as well, by the same
`LayoutFile` route as the three drivers but to dirid 11 rather than their
dirid 10, the INF gate holding it there. Windows 2000's `USB.INF` and Windows XP's `usbport.inf` already name
that file as the root hub's property-page provider, so on an xHCI-only
machine the reference dangled and the Power tab was silently absent.
Measured in both NT guests (`build-and-test.md`), and the acceptance test
takes the tab at step 4.7.

Why a phase: the findings interact - F3 and F9 are one verdict rule from two
sides, F1 and F8 both table ownership, F6, F7, F18 and D1 the same shipped
statements - so a piecemeal fix on a release branch would repeat their drift.

Tasks, in the audit's revised order, all closed.
[`runs/run-20.md`](runs/run-20.md) is the record: what each task changed, the
vectors behind it, and every reading.

| Task | Subject |
|---|---|
| 20.0 | the matrix verdict (F3, F9, F11) |
| 20.1 | the packaging guards (F4, F14, F15) |
| 20.2 | endpoint and device-table ownership (F1, F8) |
| 20.3 | recovery delivery loss (F2) |
| 20.4 | the shipped statements (F6, F7, F18, D1) |
| 20.5 | the register and tool items (F5, F10, F12, F13, F16, F17) |
| 20.6 | the smaller items and D2-D6 |
| 20.7 | the gates and the guest readings; F19 was found and fixed here |
| 20.8 | the Windows 98 audio replug row, read with that target run alone |
| 20.9 | the cut, `usbui.dll`, and the readings on the published asset |

Checkpoint: every finding closed with a cited commit and regression vector or
recorded as an owner decision with its reason; every gate and self-test
green; the post-release matrix on both primary targets no worse than the
Phase 19 reports; the Windows 2000 SMP recovery and XP lifecycle readings
taken for 20.2; and the version cut. Not a checkpoint: a host test standing
in for a guest.

Records: `runs/run-20.md`; design records 05, 06 and 07; `build-and-test.md`;
`lessons.md`; `runs/run-20-post-release/`; `releases/history.md`.

## Phase 21 - 64-bit Targets: Windows XP x64 and Server 2003 x64, then Vista x64 and Windows 7 x64

Goal: whether this driver can be an Option A miniport on 64-bit Windows,
settled from the shipping binaries first and guests second; if it can, one
amd64 `xhci98.sys` observed on Windows XP x64 and then on Vista x64 and
Windows 7 x64, with its standing stated in every document that names the
targets.

Status: closed on 2026-09-16 on the Vista x64 and Windows 7 x64 tier record.
It opened on 2026-09-08. The Windows XP x64 checkpoint passed on 2026-09-09:
an amd64 binary built with WDK 7.1 as `WNET`, in its own package with its own
INF, passed every clause on an xHCI-only XP Professional x64 SP2 guest, on the
`qemu` build and then the `release` flavour; the x86 binaries were held
byte-identical while the amd64 build was added. It cost one defect, a
`USBPORT_SCATTER_GATHER_LIST` offset the compiler had guessed four bytes wrong,
now measurement M8. The Vista x64 and Windows 7 x64 leg found that no Version
200 miniport can run on NT 6.x in either architecture (M9 to M11): the
registration takes a fourth argument, the resource-type bits moved, and the
interrupt DPC is called through a Version 300 slot. The Version 300 path that
answers it is Phase 22's task 22.5, and the four NT 6.x guests there are the
evidence this phase closed on.

Both tiers are supported in virtual machines only and have never run on real
hardware. Server 2003 x64 rests on its NT 5.2.3790 identity with XP x64, not a
boot of its own. The package is not signed, so Vista x64 and Windows 7 x64
load the driver only on a boot with driver signature enforcement disabled from
the F8 menu, chosen again at every start. Their `release` flavour, and Windows
7 x64's first install through the committed INF, were task 22.10's sixth and
seventh install legs, read on 2026-09-18, and the `1.1.0.0` cut that first
published the 64-bit package is Phase 22's.

Why a phase: a target is not a build. The static pass settles the ABI before
any code is written, a guest settles the runtime the static pass cannot, and
the gates, the INF and the packager all have to learn a second architecture
before anything ships. XP x64 and Server 2003 x64 are one target, and `WNET`
is the only route to it; Vista x64 and Windows 7 x64 were a second leg because
both enforce kernel-mode code signing and stage packages through a driver
store.

Tasks, all closed. [`runs/run-21.md`](runs/run-21.md) is the record: what each
task did and every reading.

| Task | Subject |
|---|---|
| 21.1 | the static ABI pass on NT 5.2 amd64: M1 to M6, all pass |
| 21.2 | the x64 build path: WDK 7.1, all three flavours, no diagnostic |
| 21.3 | the gates: the amd64 import gate, the import library, the second INF, both packagers |
| 21.4 | the code the pass implies: the `_WIN64` packet, M7's `USBPORT_ENDPOINT_PROPERTIES`, `USBPORT_RESOURCES`, host tests for amd64 |
| 21.5 | the Windows XP x64 guest: every checkpoint clause on both flavours, and M8 |
| 21.6 | the record: the XP x64 and Server 2003 x64 tier in every document that names the targets |
| 21.7 | the six measurements on Vista x64 and Windows 7 x64, all pass, and the `i386` halves for task 22.1 |
| 21.8 | the Vista x64 and Windows 7 x64 guests: TCG, F8 as the only route, the file-queue abort and the `Xhci.Dev6` install path, M9 to M11, and the tier |

Checkpoint (the Windows XP x64 guest), passed 2026-09-09: the static pass
transcribed, eight measurements; the gates green on an amd64 binary; on an
xHCI-only guest the package installed with no prompt, the driver registered
and started and passed its No Op self-test, the root-hub callbacks answered, a
HID mouse, a mass-storage device and a composite audio device bound, and the
Device Manager disable, enable, remove and rescan sequence survived. Task 21.8
was not a checkpoint clause. Not a checkpoint: a build that links, or a static
reading standing in for a guest.

Records: `runs/run-21.md`; `design/11-x64-targets.md`;
`usb-xhci-info/usbport-miniport-abi.md`; `legal-provenance.md` section 4;
`build-and-test.md`.

## Phase 22 - Vista and Windows 7, and Release `1.1.0.0`

Goal: whether this driver installs, loads and works on Windows Vista and
Windows 7, settled from the shipping `usbport.sys` first and guests second,
with its standing stated in every document that names the targets; and the
tree cut as `1.1.0.0`, the first release to carry a 64-bit package.

Status: closed on 2026-09-19 on the cut, its nine install legs and the
post-release readings, with 22.12 (d) re-scoped by the owner. It opened on
2026-09-08, the cut added on 2026-09-09. The static pass found nothing
against the 32-bit binary, but Phase 21's M9 to M11 found that no Version
200 miniport can run on NT 6.x, so task 22.5 made both shipping binaries
present `Version = 300` to an NT 6.x `usbport.sys` and `200` to everything
else. Its guests found three defects, each fixed and re-read on every
target: the Windows 7 enable arrest (issue 7), a Windows XP x64 SMP bugcheck
(issue 8), and XP's three-argument `CloseEndpoint`. Vista and Windows 7 have
been a VM-supported tier in both architectures since 2026-09-16. `1.1.0.0`
was cut on 2026-09-18 and re-cut eight times, every binary byte-identical;
the sixth and seventh re-cuts recorded three known limitations found after
the cut: the Vista/7 bugcheck with a Full-Speed hub on a root port,
XP-onward silence on a root-port audio device, and the Windows 7
controller-disable hang seen on the E460; the eighth brought the readme's
embedded `history.md` entry into line with them. What was left was not a
task: the owner's upload of `out\xhci98-1.1.0.0.zip` (397,765 B) and the push,
and the owner uploaded it on 2026-09-20 (Phase 23's status). Carried open past the
phase: the disable hang's cause, the Sound Blaster Play! 2's Code 10 on
Windows 7, and the split isochronous packet 22.12 (d) did not exercise.

The x64 half is exactly what tasks 21.5 and 22.5 observed: three guests,
never real hardware, with Vista x64 and Windows 7 x64 loading the driver
only on an F8 boot. Of the x86 half, 32-bit Windows 7 ran once on real
hardware after the cut (the E460, 2026-09-19).

Why a phase: the 32-bit question asked nothing of the toolchain, only guests
and readings, which is a different shape of work from Phase 21's. The cut
lands here because Phase 21 closed on task 21.8's guests, and a cut should
not wait on a leg whose answer changes nothing it publishes.

Tasks, all closed. [`runs/run-22.md`](runs/run-22.md) is the record: what
each task did and every reading, box by box.

| Task | Subject |
|---|---|
| 22.1 | the static ABI pass on 6.0 and 6.1 x86: all six pass |
| 22.2 | the imports: all ten pairs resolve on both systems |
| 22.3 | the install path off clean guests: every OS-supplied file already on disk |
| 22.4 | the Vista x86 and Windows 7 x86 guests, which do not share an accelerator |
| 22.5 | Version 300 on NT 6.x, both architectures: issues 7 and 8, the `Xhci.Dev6` INF path, `xhcisnap`, Windows 2000's restart prompt, right-click Install |
| 22.6 | the record: the tier in both architectures, nothing said of issue 7 |
| 22.7 | the charset gate on tracked source |
| 22.8 | what a cut needs that no gate supplies: the date 2026-09-18 in `xhci_version.h` and both INFs, the history entry, the release notes, README's Install section, the issue forms |
| 22.9 | the post-release matrix on fresh Windows 98, 2000, XP x64 and Windows 7 x86 clones, taken after the cut by the owner's decision: run 2026-09-19; the Full-Speed-hub bugcheck (all four NT 6 targets) and the root-port audio silence (XP onward) became known limitations, and the Windows 7 real-hardware session on the E460 added the disable hang as a third |
| 22.10 | the cut, 2026-09-18, four flavour directories and the asset; nine install legs read from it - the four x86 legs with full device clauses, the amd64 package on XP x64, Vista x64 and Windows 7 x64, the x86 package on Vista x86 and Windows 7 x86, the unsigned-driver prompt recorded - and the asset's 17 files exactly what the packager staged; no finding, no re-cut for it |
| 22.11 | issue 5's mechanism replaced by `USB_MINIPORT_FLAGS_DISABLE_SS`, read on all ten targets 2026-09-17 |
| 22.12 | the guest readings the 2026-09-17 audit fixes owe: (a) the 16-pointer service block on XP x64 and Vista x64; (b) the folded port shadow on Vista x86, gating the cut, re-read on the `release` flavour on leg 8; (c) the CNR/HCRST refusal on the Windows 2000 SMP guest under a gdbstub; (d) re-scoped by the owner to the played streams, Windows 2000 in QEMU and Windows 98 on the E460, since no matrix audio row plays and nothing after XP plays on a root port - the split isochronous packet and `SweptTransfers` were not exercised |

Checkpoint, the guest half: on each of Vista x86 and Windows 7 x86 the
package installed, the driver registered, started and passed its No Op
self-test, the root-hub callbacks answered, the three devices bound, and the
disable, enable, remove and rescan sequence survived. Taken on 2026-09-13 on
the `qemu` build from a staged INF (issue 7 section 7.5), all but the
unsigned-driver prompt, which 22.10 took. The cut half: every gate green on
both architectures and four flavour directories cut; all nine install legs
read from the asset; the asset holding exactly what the packager staged, with
no Microsoft file; and the prose no gate reaches bumped. Not a checkpoint: a
`qemu` reading standing in for the published `release` binary, the
acceptance test, or the upload. The post-release matrix (22.9) was a clause
of this half until 2026-09-18, when the owner put it after the cut.

Records: `runs/run-22.md` (22.12's readings go there too);
`design/11-x64-targets.md`; `usb-xhci-info/usbport-miniport-abi.md`; issues
05, 07 and 08; `build-and-test.md`; `lessons.md`; `releases/history.md`;
`runs/run-22-post-release/` (written by 22.9).

## Phase 23 - Controller Property Page and Interrupt Moderation

Goal: GitHub issue 4's request for the controller's property page, fixed and
read on both primary targets; one measurement of the owner's, whether an
interrupt moderation interval below the reset default raises throughput, which
if it does becomes a registry value; and the result cut as `1.1.1.0`. The
issue's polling rates and true speeds on root ports are Phase 24's: the
Phase 24 that first carried them was removed by the owner on 2026-09-22, and
a second was added on 2026-09-24, after this phase closed.

Status: closed on 2026-09-24 on the re-cut, its ten install legs, the audio
gate and the post-release matrix. It opened on 2026-09-19 on branch
`phase-23`, renamed to `1.1.1.0` on 2026-09-22, the third field moving
because 23.4 is driver code. The property page's two halves were taken in
one day, 2026-09-20, on three 9x stacks and seven NT guests, and both INFs
carry the line. The moderation experiment measured: on the P14s Gen 1 under
Windows 98 SE, mass-storage reads ran 88% faster at 50 us than at the
hardware's 1 ms, so the value shipped as `XhciImodInterval250ns`, written by
the INF as 500 (125 us), with the driver substituting 4000 when the value is
absent or invalid. `1.1.1.0` was cut on 2026-09-22 and re-cut twice, the
drivers' code and data byte-identical each time: the same night for the
owner's edit to the history entry, and on 2026-09-24 after the audio gate,
which passed on the owner's ruling with a limitation: the stream never dropped
out, but ATTO's large reads stuttered by ear at 500 where the controls at
4000 did not. Carried open past the phase: the stutter's cause; an
intermittent Windows 98 shell wedge when a USB audio device is replugged
after a cold boot, present on `1.1.0.0` too and at every moderation value,
so not this release's; and the polling-rate and true-speed requests, which
keep the GitHub issue open and are Phase 24's. What was left was not a task: the owner's upload
of `out\xhci98-1.1.1.0.zip` (377,889 B) and the push.

"GitHub issue 4" is not `docs/issues/04`. The numbers collide by accident:
`docs/issues/04-xp-restore-device-ep0-remove.md` is the XP two-handle restore
fixed in `1.0.1.0`; the GitHub issue is
`https://github.com/yeokm1/xhci98/issues/4`, "USB bus internal requests
handling (and more)", opened by LordOfMice on 2026-09-06 and still open. Its
`usbui.dll` and selective-suspend requests were answered in `1.0.2.0` and
`1.1.0.0`, this phase answers the property page, and polling rates and true
speeds are Phase 24's. Replying on the issue, and closing it, are the
owner's and not a task.

Why a phase: the owner's note on the issue (2026-09-19) was that the speed
work may be spread over several releases to reduce risk, and that is the
split taken: this release carries the property page and the moderation value
and nothing else of the issue. The two ship together because 23.4 changes the
driver's start, which moves the third field of the version and makes the
release a cut of its own, with the install legs and post-release matrix every
cut carries.

Tasks, all closed. [`runs/run-23.md`](runs/run-23.md) is the record: what
each task did and every reading, and the phase narrative as this entry
carried it while the phase was open.

| Task | Subject |
|---|---|
| 23.1 | the 9x half of the controller's property page: the line in `[Xhci.AddReg]`, the `PROP-*` gate family, the static readings of `sysclass.dll` and `usbui.dll`, and three guest legs on 2026-09-20 (Windows 98 SE under NUSB and under SweetLow, Windows ME); issue 6 visible in the Bandwidth dialog on all three, and `usbui.dll` shown to supply both dialogs |
| 23.2 | the NT half, the same day: `EnumPropPages32` and `Controller` in `[Xhci.AddReg.NT]`, the Advanced tab on seven NT guests across all four install sections, `PROP-NTHALF` inverted into `PROP-MISSING`; a by-hand `EnumPropPages32` on an installed devnode is inert on NT 6.x |
| 23.3 | the moderation experiment, the P14s Gen 1 under Windows 98 SE, 2026-09-22: 8 MB reads 17.6 MB/s at 4000, 29.8 at 1000, 33.1 at 200 and at 160; QEMU does not model IMODI, so a real-hardware reading; the stop rule did not fire |
| 23.4 | the registry value `XhciImodInterval250ns`: the INF writes 500, the driver substitutes 4000 for an absent, unreadable or out-of-range value (10 to 4000), `XHCISNAP` moves to schema 4, the experimental build retired; guest readings on Windows 98 SE and Windows 2000, 2026-09-22 |
| 23.5 | the audio test, read first on the 23.3 build: a Full-Speed root-port stream through a full ATTO pass at 4000, 1000 and 500 with no dropout and every isochronous error counter 0; the gate proper became 23.9 |
| 23.6 | the record: the release notes' two "Not in this release" sections become this release's, the moderation section written as README's "Tuning" is, issue 6 and the issues index saying polling rates and true speeds are not answered here |
| 23.7 | what the cut needs that no gate supplies: `1.1.1.0` dated 2026-09-22 in `xhci_version.h` and both INFs, the history entry, the release notes' version, the issue forms, and the `readme.txt` template's schema-4 paragraph |
| 23.8 | the cut, 2026-09-22, re-cut the same night for the owner's history edit; all ten install vehicles read from the asset by 2026-09-23, the Windows 98 SE and Windows 2000 legs reading 500 back from the register; one finding, the Windows 98 audio replug wedge, not this release's, so no re-cut for it |
| 23.9 | 23.5's gate proper on the cut's own `release` binary, the P14s, 2026-09-23: no dropout at 500, but a stutter on ATTO's large reads that the controls did not show; the owner ruled it a limitation, kept 500 and re-cut `1.1.1.0` dated 2026-09-24 |
| 23.10 | the post-release matrix on fresh Windows 98 SE and Windows 2000 clones, 2026-09-23, run before 23.9 by the owner's choice: both pass, Windows 98 SE one row better than `1.1.0.0` (22.9's audio replug failure did not recur) and Windows 2000 identical; XP x64 and Windows 7 read as 22.9 |

Checkpoint: the property page in both halves of the 32-bit INF; 23.4 landed
(23.3 measured): the value absent, invalid and at 4000 all read the same IMOD
of 4000 on both targets, the INF's 500 and any other valid value read back
from the register, a start never failed by it, and 23.5's audio test passed
at 500 on bare-metal Windows 98 on the cut's own `release` binary; every gate
green on both architectures, `1.1.1.0` cut, and its install legs read from
the asset; and the device matrix on both primary targets no worse than
`runs/run-22-post-release/`. Not a checkpoint: a throughput figure taken in
a guest, the reporter's machine standing in for one of the project's, any
polling-rate work or a decision on true speeds (Phase 24's), the
acceptance test, or the upload.

Records: GitHub issue 4 (the thread; nothing of it is copied here beyond the
table above); `docs/issues/06-full-speed-root-port-bugcheck.md`;
`docs/future-plans/virtual-hub-per-root-port.md`;
`docs/usb-xhci-info/xhci-data-structures.md` (IMOD, Table 5-39);
`docs/usb-xhci-info/usbport-miniport-abi.md` ("Periodic scheduling: what
`Period` actually carries"); `build-and-test.md` (the INF's omitted
directives); `lessons.md`; `runs/run-23.md`; `releases/history.md`;
`runs/run-23-post-release/` (written after the cut).

## Phase 24 - GitHub Issue 4's Remaining Requests: Polling Rates and True Speeds

Goal: what GitHub issue 4 still asks for after `1.1.1.0`, taken in the order
that costs least to build and to read. Two of its five items are open, and
one of them has two halves: a Low-Speed device behind a hub refuses the
polling rates the reporter's stack offers it (item 2, the reporter's Code 10
at 250 Hz and above); a Full or Low Speed device on a root port polls in the
1, 2 and 4 ms bands (item 2's other half, issue 6 section 5); and every
root-port device is reported High Speed (item 1, issue 6 itself). Items 3
(`usbui.dll`, `1.0.2.0`), 4 (idle suspend, `1.1.0.0`'s miniport flag) and
5 (the property page, `1.1.1.0`) are answered and stay closed. The reporter's
last pointer, "ADuM3160 / 4160" for the USB 1.1 hub this project has never
held, is a reading the phase takes once the part is to hand.

Status: **open**, added on 2026-09-24 at the owner's request. **24.1's
Low-Speed clause is read** (`runs/run-24.md`), the same day it started.
SweetLow's usbport rebuild has no Low-Speed floor where SP4, NUSB and XP SP3
have one, so it sends `Period` 4, 2 and 1 for a Low-Speed mouse behind a hub
at 250, 500 and 1000 Hz, and the driver refused them; his own
`README.ENG.TXT` says the same thing in words. The refusal is gone, the host
vectors are in, and `ep.open.ival` puts the programmed Interval in the ring.
QEMU presents no Low-Speed peripheral, so the device is a **real one passed
through** with `usb-host` (a 1.5 Mb/s mouse bound to WinUSB), behind a
`usb-hub`, on Windows 98 SE under SweetLow's stack, driven with his own
`Setup.exe`: all three rates now open, at Interval 5, 4 and 3 - 4, 2 and
1 ms - none floored, 24 of 24 endpoint opens accepted, no refusal of any
kind, and no Code 10. **The no-regression reading is taken on all nine other
guests** (2026-09-24/25): 230 endpoint opens across the ten, 230 accepted,
not one refusal and not one interval floored, on both usbport lineages and
both architectures. Vista and Windows 7, in both architectures, are read on a
**root port** rather than behind the hub, because behind the hub is the
topology issue 6 section 6.2 bugchecks them in - confirmed here against the
`1.1.1.0` binary as well, so it is that known limitation and not this task.
**24.1 is read out.** It is the second Phase 24: the first, split out of Phase 23 on
2026-09-22 with these same three subjects as 24.1 to 24.3, was removed the
same day before any task ran, and `runs/run-23.md` names its ids as the
record of that day. The ids below keep those three meanings, so nothing that
cites them changes sense; 24.4 and 24.5 are new.

"GitHub issue 4" is not `docs/issues/04`; Phase 23's entry has the
collision. Replying on the issue, and closing it, are the owner's and not a
task. The two items carried open past Phase 23 that are not the issue's -
the 500 stutter's cause and the Windows 98 audio replug wedge - are not this
phase's either.

Why a phase, and why this order: the owner's note on the issue (2026-09-19)
was that the speed work may be spread over several releases to reduce risk,
and the reporter's advice (2026-09-14) was Low-Speed devices behind a hub
first, then root ports, "as I expected different complexity". The tasks are
in order of how cheaply each can be built and read: one bound in one
function with a virtual-machine reproduction, then a decision that may need
no code, then a decision on the largest change the driver has asked for,
then the reporter's hub pointer, which waits on a part the owner does not
yet hold (2026-09-24) and so sits second last. The phase ends on the cut of
whatever 24.1 and 24.2 change; if 24.3 is taken, what it builds is a phase
of its own, since the proposal's batches are a phase's worth and the
release-per-risk rule says so.

Tasks. `runs/run-24.md` is the record once a task runs.

- [x] **24.1 - Low-Speed polling rates behind a hub.** *(read out 2026-09-25; `runs/run-24.md`)*

  Reproduce the reporter's Code 10 first, in the reporter's own
  configuration. The reporter is SweetLow, who runs his own stack on Windows
  98 and not on Windows 2000, so the reproduction is a Low-Speed mouse behind
  a hub, his hidusbf setting 250, 500 and 1000 Hz, under his stack on Windows
  98 SE. That guest is where the task is developed.

  Once the change holds there, the same hidusbf reading is repeated on every
  other guest the project holds: Windows 98 SE under NUSB 3.3, Windows ME,
  Windows 2000, XP x86 and x64, and Vista and 7 in both architectures (the
  x64 guests taking hidusbf's signed build or test signing). Each takes the
  mouse behind the hub at the same three rates. The point is to show the
  change affects none of them and to record what each system's usbport
  passes down.

  The candidate is `XhciIntervalFromPeriod` in `src/xhci_ctx.c`, which
  refuses a Low-Speed `Period` under 8 on the strength of the SP4 and NUSB
  floors alone. xHCI Table 6-12 allows Interval 3 to 10 (1 ms to 128 ms) for
  a Low-Speed interrupt endpoint, so the bound is this driver's, not the
  hardware's.

  If confirmed: accept `Period` 1, 2 and 4 at Low Speed, host vectors for
  the new rows, `XHCISNAP` showing the programmed Interval, the three rates
  read back on Windows 98 SE under SweetLow's stack, and the same hidusbf
  reading on every other guest held. A `Period` that no shipping usbport can
  send stays refused.

  Where it is read: host tests; then, on the SweetLow guest, a `usb-hub`
  with a **real Low-Speed mouse passed through by `usb-host`** - QEMU models
  no Low-Speed peripheral, so the `usb-mouse` this line named until
  2026-09-24 is Full Speed and cannot exercise the arm; then hidusbf on every
  other guest held for the no-regression read, where the Full-Speed
  `usb-mouse` is the right device because that read is a regression read and
  not a Low-Speed one; the E460 with the Low-Speed mouse behind a USB 2.0 hub
  if a metal reading is wanted.

- [x] **24.2 - Polling rates on a root port.** *(decided 2026-09-25: owned by 24.3, no narrower change; `runs/run-24.md`)*

  A decision, with its reason, whether they close as owned by 24.3. The
  bands are usbport's bucketing on the speed it was told (issue 6 section
  5), `bInterval` is gone before the miniport sees it, and the invariants
  forbid reconstructing it. If no narrower change exists, this task records
  why and points the reporter at 24.3; if one does, it is measured with
  hidusbf on a root port as section 5's reading was.

  Where it is read: a reading of the tree and the ABI document; a boot only
  if a change is found.

- [ ] **24.3 - True speeds on root ports.**

  The owner's decision on `docs/future-plans/virtual-hub-per-root-port.md` -
  taken, refused or deferred, with the reason recorded on issue 6 section 8.
  Taken means the page becomes a numbered design record, its section 5
  measurements become the checkpoint of a new phase, and its section 9
  decisions are answered (name, permanent or on demand, ids, default). Not
  taken means issue 6 section 9's first item says so and why.

  The reporter offered no fix to take. His remarks on the thread - that the
  High-Speed report is "only default behaviour" because usbport and usbhub
  handle slower devices on UHCI and OHCI root hubs (2026-09-07), and "EHCI
  with right USBPORT, of course" (2026-09-19) - are answered by issue 6
  section 4 (those miniports never declare the USB2 flag, and dropping it
  loses High Speed on Windows 98) and by the rule that a usbport other than
  the target's own is not this project's to ship. This paragraph said until
  2026-09-25 that he had proposed a patched usbport; the thread does not say
  so (`runs/run-24.md`, 24.3).

  An experiment was taken before the decision, at the owner's request
  (2026-09-25, `runs/run-24.md`, 24.3): an uncommitted build reporting the
  true speed, on Windows 98 SE under SweetLow's stack, with a Full-Speed
  mouse on a root port. It took the fatal exception at `0028:C002F70E` in
  `NTKERN`, NUSB's address from Phase 5, so a truthful root-hub report is now
  measured fatal on both Windows 98 lineages and on Windows 2000. It is
  evidence for the decision, not the decision.

  Where it is read: a decision; no boot (the experiment above was one, and
  is not this task's reading).

- [ ] **24.4 - The reporter's pointer for the missing USB 1.1 hub.**

  Once a part is to hand: read whether a board built on an Analog Devices
  ADuM3160 or ADuM4160 USB isolator enumerates as a hub or is transparent to
  the host, and what it does to a High-Speed device behind it. If it
  presents as a hub, it is the first way to put issue 6 section 6's topology
  on metal (`test-equipment.md` gains its row); if it is transparent, the
  pointer closes on that reading. The datasheet can be read before the part
  arrives, and the phase does not wait on it.

  Where it is read: the datasheet, then the part on the E460.

- [ ] **24.5 - The record and the cut.**

  The record and the cut of what 24.1 and 24.2 changed, as `1.1.2.0` if 24.1
  landed driver code (the third field moves): issue 6 section 5's bands and
  section 9's first item, the release notes' High Speed entry,
  `releases/history.md`, `xhci_version.h` and both INFs, the install legs
  from the asset, and the post-release matrix on both primary targets read
  against `runs/run-23-post-release/`. If 24.1 changed nothing, the phase
  closes on the two decisions with no cut.

  Where it is read: the cut's gates; the ten install vehicles; the matrix.

Checkpoint: 24.1 either refuted (the Code 10 reproduced and traced to
something other than the bound, recorded) or landed and read: a Low-Speed
device behind a hub polling at 250, 500 and 1000 Hz on Windows 98 SE under
SweetLow's stack, with the Interval read from the snapshot and no Code 10;
the same hidusbf reading taken and recorded on every other guest the project
holds, none of them worse for the change; and the device matrix on both
primary targets no worse than `runs/run-23-post-release/`; 24.2 and 24.3 each a recorded decision with its
reason, on issue 6; 24.4 a recorded reading, or recorded as waiting on the
part if the phase closes first; and, if driver code changed, `1.1.2.0` cut
with its install legs read from the asset. Not a checkpoint: a root-port
polling rate outside the bands, a truthful root-port speed report, anything
the virtual hub would measure, a metal reading of 24.1, the part itself, the
acceptance test, the upload, or the reply on the issue.

Records: GitHub issue 4 (the thread; the reporter's Code 10 reading of
2026-09-12, his order of 2026-09-14 and his pointer of 2026-09-19);
`docs/issues/06-full-speed-root-port-bugcheck.md` sections 5, 8 and 9;
`docs/future-plans/virtual-hub-per-root-port.md`;
`docs/usb-xhci-info/usbport-miniport-abi.md` ("Periodic scheduling: what
`Period` actually carries"); `docs/usb-xhci-info/xhci-data-structures.md`
(Table 6-12); `src/xhci_ctx.c` (`XhciIntervalFromPeriod`,
`XhciIntervalForSpeed`); `docs/contributing/implementation-invariants.md`
("Root Hub Reporting"); `docs/contributing/test-equipment.md`;
`runs/run-24.md` (written by the first task that runs).
