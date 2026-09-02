# Handoff: release 1.0.0.1 (Phase 17 observed, Phase 18 open)

Rewritten 2026-09-02 on branch `1.0.0.1`, late in the session that executed
the previous plan. The roadmap's Phase 17 and Phase 18 are the authority;
this file says where each task stands and what the next session does first.

## What was done today (commits 2256779 to 00ae686)

- Step 0 (task 17.0): `legal-provenance.md` section 5 records the decision;
  `AGENTS.md` points at it.
- Task 17.1a, observed with the owner at the console: on `vm\sweetlow-2a.img`
  reverted to `sweetlow-stack-nodriver` (SweetLow's stack, no driver, no
  `usbd.sys`, no `usbhub.sys`, no CABs on disk) the Device Manager install
  from `vm\LAYOUT` raised **Insert Disk: "Please insert the disk labeled
  'Windows 98 Second Edition CD-ROM'"**, not a prompt for the xhci98 disk.
  After the copy, a shutdown and a relaunch (`prepare-image.ps1 -Target
  2a-sweetlow -Boot -Xfer -XferAdd vm\LAYOUT`), the debugcon showed
  `USBPORT_GetHciMn=10000001`, `StartController`, `RH_GetRootHubData`,
  `RH_GetPortStatus` and the rest of the root-hub family, `-Status` read
  devices addressed 1 (the keep-alive mouse; its HID wizard then ran from
  the CD), and Device Manager showed the controller and "USB 2.0 Root Hub"
  clean. The `dir` of `SYSTEM32\DRIVERS\USB*.SYS` was not read back; the
  root hub coming up is the proof that `usbd.sys` arrived.
- Task 17.2, all of it, on the host: the INF (four directive edits, comments
  rewritten, `DriverVer=09/02/2026,1.0.0.1`), `src/xhci_version.h`, the INF
  gate's `OS-*` family and `PKG-MSFILE` with self-tests (278 checks),
  `expected-footprint.txt`, the packaging scripts without the manifest
  (`test-package.ps1` 153 checks), `usbd-sources.expected` deleted,
  `extract-usbd-sources.ps1` reduced to staging reference copies, and every
  document the plan listed. `scripts\build-driver.cmd all`: BUILD + GATES
  PASSED (debug release qemu).
- Windows ME (Phase 18's subject): harness support (`2e` prepare-only
  target, `-XferPackage`, `setup-qemu.ps1 -WinMeIso`), the static facts from
  the owner's Windows ME OEM CD in `build-and-test.md`, "Windows ME target
  VM", and the provenance note. Nothing has run on Windows ME.
- The roadmap: Phase 17 (the mechanism), Phase 18 (release 1.0.0.1 with
  Windows ME and the cut), per the owner's instruction that Windows ME
  support is part of 1.0.0.1.

## What the next session does first

1. **Task 17.1b, the NUSB leg: done** later the same evening on
   `vm\layout-2a.img` (config entry `2a-layout`, monitor 56597 + 100) with
   the real 1.0.0.1 package from `out\pkg-qemu` on `D:\`: CD prompt,
   1.0.0.1 build loaded under NUSB's usbport, root hub up, and the hot-plugged
   `usb-audio` bound as "USB Composite Device" + "USB Audio Device". A first
   attempt was contaminated by the old package (the guest was booted before
   `out\pkg-qemu` had been rebuilt, and the driver installed from it); the
   image was re-cloned with `-Clone -FreshCopy` and the leg redone.
2. **Task 17.1c, the Windows 2000 leg: done** the same evening on
   `vm\layout-2b.img`: Have Disk from `D:\` installed and started the driver
   without a reboot, root hub up, mouse bound; no disk prompt was reported
   by the owner, and the file's on-disk version was not read back.
3. Both readings are in roadmap Phase 17 and `build-and-test.md`. The two
   temporary images, their config entries, `vm\LAYOUT` and `vm\NOUSBD` were
   deleted afterwards (the standing `vm\` practice), so Phase 17 is fully
   observed and the next session starts on Phase 18.

The guests share one transfer drive (`vm\xfer-p10`), so they run one at a
time. The owner drives the guest GUI; the host side is the scripts above,
`prepare-image.ps1 -Status`, and `out\phase10\prep-<target>-debugcon.log`.
`scratchpad\guest.ps1` from this session (monitor sendkey, typed text,
screenshots to PNG) is not in the repository; the monitor echoes every
keystroke with escape codes, and the prep monitor port is the config's plus
100.

## Phase 18: Windows ME, then the cut

The owner decided that Windows ME support ships in 1.0.0.1. Everything that
needs no guest is done; what remains is the guest:

1. Install Windows ME by hand: `scripts\setup-qemu.ps1 -WinMeIso
   'D:\isos\Windows Me OEM Full.iso' -CreateDisk` writes
   `scripts\local\qemu-winme-install.cmd` and creates `vm\winme.img`. The
   ACPI HAL rule is the Windows 98 one (`setup /p j`); setup is under
   `\WIN9X` on that CD. Then a USB 2.0 stack, which the CD does not carry
   (its `layout.inf` names `uhcd.sys`, `openhci.sys`, `usbd.sys` and
   `usbhub.sys` and no `usbport.sys`, `usbehci.sys` or `usbhub20.sys`):
   Microsoft's own package is the `USB2.INF` and three drivers NUSB ships
   ("For Windows 98SE and Windows ME"), installed by right-clicking that
   INF from `tools\nusb-extracted`; SweetLow's is the other candidate.
2. Add the `2e` entry from `config.sample.psd1` to the host config with `Cd`
   pointing at the Windows ME ISO, then `prepare-image.ps1 -Target 2e -Boot
   -Xfer -XferPackage` and the driver install from `D:\`. What to read is
   in `build-and-test.md`, "Windows ME target VM": the load, the root-hub
   callbacks, which files the copy phase asked the CD for (the CD's
   `layout.inf` puts `usbd.sys` and `usbhub.sys` in `BASE2.CAB`), then HID
   and storage.
3. The tier decision is the owner's (first-class with the checkpoint tax, or
   supported-in-VM stated like Windows 2000), and it decides which documents
   name Windows ME: `AGENTS.md`, `README.md`, the release notes, the
   bug-report form, the INF header comment, and the `1.0.0.1` history entry.
   Nothing names it as supported until the observation exists.
4. Then task 18.7: `make-release.ps1` on the full flavour set and the
   acceptance test per target. **The date moves on the day of the cut**:
   `src/xhci_version.h`, the INF's `DriverVer` and the `## 1.0.0.1 - date`
   heading in `releases/history.md` all say 2026-09-02 today and must agree
   with each other and with the cut day, or `make-release.ps1` refuses.

## Open beside this

- The QEMU on this host is scoop's 11.0.0 again (`C:\Users\yeokm1\scoop\apps\qemu`);
  the 11.1-rc2 SMM wedge entry in `lessons.md` applies to that other build.
- The other Windows 98 matrix targets still run `-machine pc`; harmless on
  11.0.0.
- A full device-matrix run on the SweetLow guest, and bare metal on either
  target with the new install procedure, have not been done.
- `README.md` is LF-only; git warns it will become CRLF on the next touch.
