# Handoff: stop shipping usbd.sys and usbhub.sys, let the OS supply them

Written 2026-09-02 on branch `sweetlow-stack`, after the SweetLow stack work
(commits 90dba24, 5c8a6fe, 10f91c1). This is the next step the owner has
chosen for a later release, deferred so that the current release wording
can go out first. Everything below is either measured today or is a plain
consequence of the INF mechanism; the one thing not yet done is the test.

## What is wanted

The release download carries three Microsoft files today: `usbd98.sys`,
`usbd2k.sys` and `usbhub98.sys`, the target OS's own `usbd.sys` and
Windows 98's `usbhub.sys` under media names, copied by `src/xhci98.inf`
with `COPYFLG_NO_OVERWRITE`. They exist because an xHCI-only machine never
had a USB controller Windows setup recognised, so setup never placed them,
and without `usbd.sys` the USB 2.0 root hub cannot load on either target.

The owner wants the package to carry none of them. Instead the INF asks
the Windows setup engine for the files by name, and the engine fetches them
from the Windows source: `C:\WINDOWS\OPTIONS\CABS` when the CABs are on
disk (OEM installs, Windows 98 QuickInstall), otherwise the install CD, and
on Windows 2000 `Driver Cache\i386\driver.cab`, which every install has.
This is how Windows' own INFs work, and it is what happened today when the
HID wizard asked for `hidusb.sys` and took it from `E:\WIN98`.

## What today established

All of it on QEMU, on `vm\sweetlow-2a.img`, with SweetLow's stack; the
build-and-test section "The SweetLow stack" and the lessons entry "The
Windows 98 teardown bugcheck belongs to the Windows 2000-lineage usbport"
carry the detail.

- `usbd.sys` is required on Windows 98 under both stacks. Without it the
  driver registers and starts, but the USB 2.0 Root Hub sits at Code 2 and
  no root-hub callback ever arrives, because `usbhub20.sys` (NUSB's and
  SweetLow's alike) imports `USBD.SYS` by name. A hand copy of the Windows
  98 SE build and a reboot fixes it. SweetLow's `USBDSTUB.SYS` is not a
  substitute and his INF does not install it.
- `usbhub.sys` is required under NUSB (composite devices are Code 2 without
  it; issue 03) and not under SweetLow's stack, whose usbccgp parents them.
  With both files present his usbccgp still wins, so the file is inert
  there. Whatever mechanism supplies `usbd.sys` can supply `usbhub.sys` the
  same way at no extra cost.
- Windows 2000 is in the same position as Windows 98 for `usbd.sys` (the
  lessons entry "usbhub20.sys bugchecks Win2000"), so the change covers
  both targets or it leaves an odd package.

## The mechanism, and the test package that is already built

`LayoutFile=layout.inf` in `[Version]`. A CopyFiles entry whose file the
INF's own `[SourceDisksFiles]` does not name is then resolved through the
OS's `layout.inf` (`C:\WINDOWS\INF\LAYOUT.INF`, which records
`usbd.sys=5` for `BASE5.CAB`; `%SystemRoot%\inf\layout.inf` on Windows
2000) and fetched from the Windows source path. The driver's own file stays
on disk 1 as now.

`vm\LAYOUT\` (git-ignored) holds the test package: the qemu-flavour
`xhci98.sys` and an INF derived from `out\pkg-qemu\xhci98.inf` by exactly
these edits:

- `[Version]`: `LayoutFile=layout.inf` added after `Provider=`.
- `[SourceDisksFiles]`: the `usbd98.sys=1`, `usbd2k.sys=1` and
  `usbhub98.sys=1` lines removed.
- `[Xhci.CopyW98]`: `usbd.sys,,,16` and `usbhub.sys,,,16`.
- `[Xhci.CopyW2K]`: `usbd.sys,,,16`.

Nothing else changed; the comments still describe the old scheme.

## The test

1. `powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-sweetlow -Boot -Xfer -XferAdd vm\LAYOUT`
   after `qemu-img snapshot -a sweetlow-stack-nodriver vm\sweetlow-2a.img`
   (SweetLow's stack, no driver, no `usbd.sys`, no `usbhub.sys`; the host
   config's `Win98Cd` now points at the real ISO, so the CD is at E:). The
   target runs `pc,smm=off`; see the lessons entry on QEMU 11.1-rc2.
2. In the guest: Device Manager, the unclaimed xHCI, Update Driver, Specify
   a location, `D:\LAYOUT`. Watch the copy phase:
   - no prompt: both files came from `C:\WINDOWS\OPTIONS\CABS`;
   - "Insert Disk" naming `usbd.sys` or `usbhub.sys`: resolved through
     layout.inf, wants the CD, answer `E:\WIN98`;
   - a prompt for `usbd.sys` from the xhci98 disk: the route did not take.
3. Shut down from the Start menu, quit QEMU, relaunch with the same
   command. Then `dir C:\WINDOWS\SYSTEM32\DRIVERS\USB*.SYS` in a DOS box
   (expect `usbd.sys` 18,912 and `usbhub.sys` 35,680), `-Status` (endpoints
   opened 1 on the keep-alive mouse), and the trace at
   `out\phase10\prep-2a-sweetlow-debugcon.log` (root-hub callbacks after
   `StartController`).
4. Repeat from `win98.img @ post-nusb` (NUSB's stack) by pointing the
   target's `CloneFrom` there and re-cloning with `-Clone -FreshCopy`; the
   NUSB path is the one that also needs `usbhub.sys`, so plug a
   two-interface device once (the `audio` spec; once only, its
   `USBAUDIO.VXD` faults on a second arrival) and expect "USB Composite
   Device" with the audio function beneath, not Code 2.
5. Windows 2000: the 2b guest with the driver removed, install from the
   same package, expect no prompt and `usbd.sys` 5.00.2195.6658 in
   `WINNT\SYSTEM32\DRIVERS`.

The guest halts on a reboot instead of restarting; every "reboot" is a
shutdown plus a relaunch from the host. Keyboard input follows the newest
QEMU keyboard, so never hot-plug a `usb-kbd` before typing.

## If the test passes, what changes

In order, each with its check green before the next:

1. `src/xhci98.inf`: the edits above, for real, and the comment block
   "The one file that differs per target" and the usbhub block rewritten to
   say what the INF now does and why.
2. `scripts/inf-gate/check-inf.ps1` and `test-inf-checks.ps1`: the `TGT-*`
   family (`TGT-MISSING`, `TGT-DUP`, `TGT-MEDIANAME`, `TGT-FLAGS`,
   `TGT-TARGET`, `TGT-DEST`, `TGT-SAMESRC`, `TGT-SHAREDSEC`, `TGT-DEFAULT`)
   and `W98-MEDIANAME` police the per-target copies and will fail the new
   INF. Replace them with rules for the new shape: `LayoutFile` present,
   each path copies `usbd.sys` with flag 16 to `10,System32\Drivers`, the
   Windows 98 path also copies `usbhub.sys`, no `SourceDisksFiles` entry
   names either, and the package manifest lists no Microsoft file.
   `scripts/inf-gate/expected-footprint.txt` changes with it.
3. `scripts/package/make-package.ps1`, `make-release.ps1`,
   `usbd-sources.expected`, `extract-usbd-sources.ps1`: stop staging and
   hashing the three files; the release assembles `xhci98.sys`,
   `xhci98.inf`, the tools and the readme only.
4. `scripts/vm-matrix/prepare-image.ps1`: the fresh-target `-Xfer` staging
   comment says the package carries `usbd.sys`; it no longer will, and the
   CD must be attached for a Windows 98 prep boot (it is, via `Win98Cd`).
5. Documents: `docs/contributing/legal-provenance.md` section 5 (the
   three-file exception closes; record that the decision changed and why,
   facts not verdicts), `releases/README.md`, `AGENTS.md` ("The INF and
   install media", the provenance bullet about the three files),
   `docs/contributing/build-and-test.md` ("Carrying a per-target
   usbd.sys"), the generated readme.txt in `make-release.ps1` (install
   steps, the "after uninstall" list, the CD sentence), `README.md`,
   `docs/using/release-notes.md`, `docs/using/release-acceptance-test.md`.
6. Version bump per `build-and-test.md` "Versioning the driver"; the
   `1.0.0.0` directory under `releases/` is never edited.

## What the user sees afterwards

A Windows 98 user without the CABs on disk is asked for the Windows 98 CD
during the driver install, on an xHCI-only machine only; a machine that
ever had a USB 1.1 controller already has both files and flag 16 leaves
them alone. Windows 2000 users see nothing new. The readme has to say that
a "cannot find usbd.sys" prompt wants the Windows CD, not the driver disk.

## Also open, not part of this step

- The one-sentence usbd.sys note for the SweetLow install option in
  `README.md` and the readme.txt ("install this driver after the stack;
  its package carries the usbd.sys the stack needs and does not include")
  becomes moot if this step lands, since the OS supplies the file either
  way.
- The other Windows 98 matrix targets still run `-machine pc` and will hit
  the QEMU 11.1-rc2 SMM wedge on this host until they carry `smm=off` or
  QEMU 11.0.0 is back.
- A full device-matrix run on the SweetLow guest, and bare metal, have not
  been done.
