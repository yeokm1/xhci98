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
embedded `history.md` entry into line with them. What is left is not a task:
the owner's upload of `out\xhci98-1.1.0.0.zip` (397,765 B) and the push.
Carried open past the
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

## Phase 23 - GitHub Issue 4's Open Requests, and the Interrupt Moderation Experiment

Goal: the requests of GitHub issue 4 that no release has answered, each either
fixed and read on both primary targets or recorded as a decision with its
reason; and one measurement of the owner's, whether an interrupt moderation
interval below the reset default raises throughput, which if it does becomes a
registry value whose default changes nothing.

Status: open since 2026-09-19; 23.1 started 2026-09-20 on branch `23.1`, its
host side done and its guest readings owed. Which version
carries the result is not decided: 23.3, 23.4 and 23.5 are driver code, so the
third field moves if any of them lands (`releases/README.md`). The owner's
note on the issue (2026-09-19) is that the speed work may be spread over
several releases to reduce risk, so this phase may close with 23.6 decided and
not built.

**"GitHub issue 4" is not `docs/issues/04`.** The numbers collide by accident:
`docs/issues/04-xp-restore-device-ep0-remove.md` is the XP two-handle restore
fixed in `1.0.1.0`; the GitHub issue is
`https://github.com/yeokm1/xhci98/issues/4`, "USB bus internal requests
handling (and more)", opened by LordOfMice on 2026-09-06 and still open. Its
items 3 (`usbui.dll`) and 4 (selective suspend) are already answered, in
`1.0.2.0` and `1.1.0.0`; the tasks below carry the rest and each names the
item it answers. They are ordered easiest first rather than by the issue's
numbering, and that order also keeps the reporter's advice of 2026-09-15:
the hub half of item 2 before the root-port half, and both before item 1.

- [ ] 23.1 item 5, the controller's property page. `runs/run-23.md` has the
  detail. **The line is in `[Xhci.AddReg]`** since 2026-09-20, the INF gate
  holds it there with a `PROP-*` family (seven rules, eight self-test cases),
  and the footprint has learned it. **The NT half is deferred, not refused**
  (owner, 2026-09-20): the pair it takes is known and all three NT references
  write it, but one `[Xhci.AddReg.NT]` serves four install paths and the page
  has been opened in none of their guests, so `PROP-NTHALF` refuses it until
  those readings exist. Of the three things owed before the line ships, two
  are answered `static`: "Disable USB error detection" names
  `ErrorCheckingEnabled` under
  `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Usb`, whose only consumer
  in six extracted trees is `SYSTRAY.EXE` and not any USB driver, so it can
  only quiet traffic this controller already sees; and Windows ME's
  `sysclass.dll` is the same module as Windows 98 SE's, string for string bar
  the version resource. **Still owed, and all of it guest work**: the
  before/after pair read on Windows 98 SE and Windows ME (staged at
  `vm/T231`, identical binary, one directive apart), the checkbox's registry
  write confirmed by read-back, what the Bandwidth Usage dialog charges a
  root-port device reported High Speed, and whether that dialog needs
  `usbui.dll` - which the exports say it does and the 2026-09-07 note says it
  does not. One consequence for the owner to weigh: `PROP-MISSING` now fails
  every already-published INF, `1.1.0.0`'s included, so assembling its upload
  set after this refuses where it passed before.
- [ ] 23.2 the moderation experiment. The register is IR0's IMOD: IMODI is
  bits 15:0 in 250 ns units and resets to 4000, 1 ms
  (`xhci-data-structures.md`, Table 5-39 p.392). The start never writes it
  and `xhciRestoreState` writes back what the save read, so every run to date
  has been at 4000. The hypothesis is that Bulk-Only Transport is strictly
  serial (Phase 8), so each of a command's completions can wait out a
  moderation interval before the next stage is submitted, and 1 ms per stage
  bounds mass-storage throughput from above. Measure sustained mass-storage
  read and write, and a USB Ethernet transfer, at 4000 and at several lower
  values down to 10, with an experimental build that writes IMOD in the start
  after the interrupter is programmed. **This is a real-hardware reading**:
  the E460 under Windows 98 SE, and under 32-bit Windows 7 if to hand.
  Whether QEMU's xHC models IMODI at all is read first; if it does not, a
  guest shows only that the write lands and reads back, never a rate. Read
  beside each throughput figure: interrupts per second, the ISR and DPC
  counters, and an isochronous stream playing (1,000 events a second at Full
  Speed, 8,000 at High Speed, and moderation is what absorbs them), because
  per-interrupt cost at real rates is what has bugchecked Windows 98 on bare
  metal before. Stop rule: if no value below 4000 measures faster outside
  run-to-run noise, record the numbers in `lessons.md`, leave the start not
  writing IMOD, and close 23.3 as not taken.
- [ ] 23.3 the registry value, **only if 23.2 measures**. A `REG_DWORD` read
  through `UsbPortGetMiniportRegistryKeyValue` beside the two log values in
  `src/xhci_dispatch.c` (no new import), proposed name `XhciImodInterval`, in
  IMODI's own 250 ns units. The owner's rule, 2026-09-19: the default is
  4000; nothing below 10 is accepted; and 4000 is assumed when the value is
  absent or invalid. Invalid means unreadable, below 10, or above 65535
  (IMODI is 16 bits); an invalid value is replaced by 4000, not clamped to
  the nearest bound, so a mistyped 0 cannot turn moderation off. Like the log
  values, nothing in the read may fail a start. The start then writes the
  value, the save and restore pair carries it across a resume unchanged, and
  the value in force is readable from a release build (a counter and an
  `XHCISNAP` line). Both INFs write the value as 4000 on all their install
  paths, the INF gate's `VAL-*` rules and `expected-footprint.txt` learn it,
  and host vectors pin the three fallbacks and the two bounds. If 23.2 finds
  a better number, shipping it as the default is a separate decision of the
  owner's; this task ships 4000. The release notes say what the value is,
  its units, and that a low value raises the interrupt rate.
- [ ] 23.4 item 2, behind a hub: Low-Speed rates of 250 Hz and above. Reproduce
  first, with a Low-Speed mouse behind a USB 2.0 hub on Windows 98 SE under
  SweetLow's stack with HIDUSBF, and read which refusal counter moves. The
  candidate is `XhciIntervalFromPeriod` (`src/xhci_ctx.c`), which refuses a
  Low-Speed `Period` under 8 because both disassembled usbport builds (SP4,
  NUSB) floor it there; SweetLow's build is a third lineage and its Low-Speed
  bucketing has not been read, so read it (`static`, a row in
  `legal-provenance.md` section 4) before changing the rule. The xHCI side
  allows it: Table 6-12 gives Full- and Low-Speed interrupt endpoints
  Interval 3 to 10, and Interval 3 is 1 ms. The refuse-don't-repair rule of
  that function stays: a widened Low-Speed range is a derived contract, not
  a clamp.
- [ ] 23.5 item 2, on a root port: derive, then decide. On a root port
  usbport buckets `bInterval` as High Speed (`1 << min(bInterval-1, 5)`
  microframes) and the driver floors the result at 1 ms, so every request
  lands in a 1, 2 or 4 ms band (`virtual-hub-per-root-port.md` section 1).
  Read what HIDUSBF's override changes in what usbport hands `OpenEndpoint`,
  and whether any chosen rate can move an endpoint between bands; the snooped
  configuration descriptor (`src/xhci_desc.c`) already recovers a true
  `bInterval` for isochronous endpoints and is the candidate lever for
  interrupt ones, but an override applied above usbport never reaches the
  device's descriptor, so that lever may not see it. If no route exists short
  of 23.6, say so in the release notes and close this as owned by 23.6.
- [ ] 23.6 item 1, true speeds on root ports: the decision, and its first
  slice if taken. `docs/future-plans/virtual-hub-per-root-port.md` is the
  candidate and issue 6 section 8 the alternatives; the costs known since
  2026-09-19 are the Vista and Windows 7 bugcheck with a Full-Speed hub on a
  root port and the silent root-port audio device from XP on. What this task
  owes is a design record with the decision, the split into releases, and
  which slice if any this phase carries. **Never write "fixed" for issue 6
  until the High-Speed report itself is gone.**
- [ ] 23.7 the record: the release notes' known limitations brought into
  line with whatever 23.1 and 23.4 to 23.6 change, and `docs/issues/06` and
  `docs/issues/README.md` updated. Replying on the GitHub issue, and closing
  it, are the owner's and not a task.

Checkpoint (draft, tightened as 23.2 and 23.6 report): on Windows 98 SE and
Windows 2000 SP4, a Low-Speed interrupt device behind a USB 2.0 hub works at
every rate the stack in use can ask for, or the refusal that remains is
derived and published; the root-port half of item 2 and item 1 each carry a
recorded decision; the property page is in both halves of the 32-bit INF or
recorded as not taken; if 23.3 landed, the value absent, invalid and at 4000
all read the same IMOD of 4000 on both targets, a valid lower value reads
back from the register, and a start is never failed by it; every gate green
and the device matrix on both primary targets no worse than
`runs/run-22-post-release/`. Not a checkpoint: a throughput figure taken in a
guest, or the reporter's machine standing in for one of the project's.

Records: GitHub issue 4 (the thread; nothing of it is copied here beyond the
table above); `docs/issues/06-full-speed-root-port-bugcheck.md`;
`docs/future-plans/virtual-hub-per-root-port.md`;
`docs/usb-xhci-info/xhci-data-structures.md` (IMOD, Table 6-12);
`docs/usb-xhci-info/usbport-miniport-abi.md` ("Periodic scheduling: what
`Period` actually carries"); `build-and-test.md` (the INF's omitted
directives); `runs/run-23.md` (to be opened by the first task that takes a
reading).
