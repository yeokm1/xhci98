# Handoff: release 1.0.0.1, the OS supplies usbd.sys and usbhub.sys

Written 2026-09-02 on branch `1.0.0.1`, after the SweetLow stack work
(commits 90dba24 to fba7b7e). The owner has decided the next step: the
package stops carrying any Microsoft file, the INF asks the Windows setup
engine to copy `usbd.sys` and `usbhub.sys` from the OS install source, and
that goes out as release 1.0.0.1. The release changes the install procedure
only. No driver code changes; `xhci98.sys` is rebuilt solely because its
version resource must match the INF's DriverVer.

## Why

The release download carries three Microsoft files today: `usbd98.sys`,
`usbd2k.sys` and `usbhub98.sys`, the target OS's own `usbd.sys` and Windows
98's `usbhub.sys` under media names, copied by `src/xhci98.inf` with
`COPYFLG_NO_OVERWRITE`. They exist because an xHCI-only machine never had a
USB controller Windows setup recognised, so setup never placed them, and
without `usbd.sys` the USB 2.0 root hub cannot load on either target.

Measured today on QEMU with SweetLow's stack (build-and-test, "The SweetLow
stack"; lessons, "The Windows 98 teardown bugcheck belongs to the Windows
2000-lineage usbport"):

- `usbd.sys` is required under both Windows 98 stacks. Without it the driver
  registers and starts, but the USB 2.0 Root Hub sits at Code 2 with no
  root-hub callback, because `usbhub20.sys` imports `USBD.SYS` by name.
  SweetLow's `USBDSTUB.SYS` is not a substitute and his INF does not install
  it.
- `usbhub.sys` is required under NUSB (composite devices are Code 2 without
  it; issue 03) and not under SweetLow's stack, whose usbccgp parents them
  even when `usbhub.sys` is present. Supplying it costs nothing extra.
- Windows 2000 is in the same position for `usbd.sys` (lessons,
  "usbhub20.sys bugchecks Win2000").

The files are the OS's own, so the OS's own install source is the right
place to take them from, and the three-file exception in legal-provenance
section 5 then closes.

## The mechanism

`LayoutFile=layout.inf` in `[Version]`. A CopyFiles entry whose file the
INF's own `[SourceDisksFiles]` does not name is resolved through the OS's
`layout.inf` (`C:\WINDOWS\INF\LAYOUT.INF` records `usbd.sys=5`, i.e.
`BASE5.CAB`; `%SystemRoot%\inf\layout.inf` on Windows 2000) and fetched from
the Windows source path: `C:\WINDOWS\OPTIONS\CABS` when the CABs are on disk
(OEM installs, Windows 98 QuickInstall), otherwise an "Insert Disk" prompt
for the Windows 98 CD; on Windows 2000, `Driver Cache\i386\driver.cab`,
which every install has, so no prompt. This is how Windows' own INFs work,
and it is what happened today when the HID wizard took `hidusb.sys` from
`E:\WIN98`. The driver's own file stays on disk 1 as now.

The INF edits, and nothing else in the directives:

- `[Version]`: `LayoutFile=layout.inf` after `Provider=`.
- `[SourceDisksFiles]`: remove `usbd98.sys=1`, `usbd2k.sys=1`,
  `usbhub98.sys=1`.
- `[Xhci.CopyW98]`: `usbd.sys,,,16` and `usbhub.sys,,,16`.
- `[Xhci.CopyW2K]`: `usbd.sys,,,16`.

Flag 16 stays, so an existing file is never touched, and the per-target
media names go, since each OS supplies its own build by construction.

`vm\LAYOUT\` (git-ignored) already holds a test package made exactly this
way from `out\pkg-qemu`: the qemu-flavour `xhci98.sys` and the edited INF,
comments untouched.

## Step 1: prove the mechanism in the VM

1. `qemu-img snapshot -a sweetlow-stack-nodriver vm\sweetlow-2a.img`, then
   `powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-sweetlow -Boot -Xfer -XferAdd vm\LAYOUT`.
   That snapshot is SweetLow's stack with no driver, no `usbd.sys`, no
   `usbhub.sys`; the host config's `Win98Cd` points at the real ISO, so the
   CD is at E:. The target runs `pc,smm=off` (lessons, QEMU 11.1-rc2).
2. In the guest: Device Manager, the unclaimed xHCI, Update Driver, Specify
   a location, `D:\LAYOUT`. Watch the copy phase:
   - no prompt: both files came from `C:\WINDOWS\OPTIONS\CABS`;
   - "Insert Disk" naming `usbd.sys` or `usbhub.sys`: resolved through
     layout.inf, wants the CD; answer `E:\WIN98`;
   - a prompt for `usbd.sys` from the xhci98 disk: the route did not take.
3. Shut down from the Start menu, quit QEMU, relaunch with the same
   command. `dir C:\WINDOWS\SYSTEM32\DRIVERS\USB*.SYS` in a DOS box (expect
   `usbd.sys` 18,912 and `usbhub.sys` 35,680), `-Status` (endpoints opened 1
   on the keep-alive mouse), and root-hub callbacks after `StartController`
   in `out\phase10\prep-2a-sweetlow-debugcon.log`.
4. The same from `win98.img @ post-nusb` (NUSB's stack): point the target's
   `CloneFrom` there, `-Clone -FreshCopy`, install from `D:\LAYOUT`. This is
   the path that also needs `usbhub.sys`, so plug a two-interface device once
   (`-Attach audio`, once only; Windows 98's `USBAUDIO.VXD` faults on a
   second arrival) and expect "USB Composite Device" with the audio function
   beneath, not Code 2.
5. Windows 2000: the 2b guest with the driver removed, install from the same
   package, expect no prompt and `usbd.sys` 5.00.2195.6658 in
   `WINNT\SYSTEM32\DRIVERS`.

Two harness facts: the guest halts on a reboot, so every "restart" is a
shutdown plus a relaunch from the host; and QEMU's keyboard input follows
the newest keyboard, so never hot-plug a `usb-kbd` before typing.

## Step 2: the change, in order, each check green before the next

1. `src/xhci98.inf`: the four edits above. Rewrite the header paragraph on
   `usbd.sys`, the "The one file that differs per target" block and the
   `usbhub.sys` block to say what the INF now does: the OS supplies both,
   flag 16 keeps an existing file, Windows 2000 gets `usbd.sys` only. Bump
   `DriverVer` to the release date and `1.0.0.1`.
2. `src/xhci_version.h`: `1,0,0,1`, `"1.0.0.1"`, the release date. This is
   the only reason the binary is rebuilt; the version must match the INF or
   `make-package.ps1` refuses the stage. Procedure: build-and-test,
   "Versioning the driver".
3. `scripts/inf-gate/check-inf.ps1` and `test-inf-checks.ps1`: the `TGT-*`
   family (`TGT-MISSING`, `TGT-DUP`, `TGT-MEDIANAME`, `TGT-FLAGS`,
   `TGT-TARGET`, `TGT-DEST`, `TGT-SAMESRC`, `TGT-SHAREDSEC`, `TGT-DEFAULT`)
   and `W98-MEDIANAME` police the per-target copies and fail the new INF.
   Replace them with rules for the new shape: `LayoutFile=layout.inf`
   present; each install path copies `usbd.sys` with flag 16 to
   `10,System32\Drivers`; the Windows 98 path also copies `usbhub.sys` and
   the Windows 2000 path does not; no `SourceDisksFiles` entry names either;
   the package manifest lists no Microsoft file. Update
   `scripts/inf-gate/expected-footprint.txt`.
4. `scripts/package/make-package.ps1`, `make-release.ps1`,
   `usbd-sources.expected`, `extract-usbd-sources.ps1`: stop staging and
   hashing the three files; the release assembles `xhci98.sys`,
   `xhci98.inf`, the tools and the readme only.
5. `scripts/vm-matrix/prepare-image.ps1`: the fresh-target `-Xfer` comment
   says the package carries `usbd.sys`; correct it, and note that a Windows
   98 prep boot needs the CD attached (`Win98Cd`).
6. Documents. `docs/contributing/legal-provenance.md` section 5: the
   three-file exception closes; record that the decision changed and why,
   facts not verdicts. `releases/README.md`, `AGENTS.md` ("The INF and
   install media", the provenance bullet), `docs/contributing/build-and-test.md`
   ("Carrying a per-target usbd.sys"), `README.md`,
   `docs/using/release-notes.md`, `docs/using/release-acceptance-test.md`,
   and the generated readme.txt in `make-release.ps1`: install steps, the
   "after uninstall" list (which names `usbd.sys` as left behind), and the
   sentence that a "cannot find usbd.sys" prompt wants the Windows CD, not
   the driver disk.
7. `releases/history.md`: the 1.0.0.1 entry, stating that the driver code is
   unchanged and only the install procedure moved. The `1.0.0.0` directory
   is never edited.

## Step 3: cut 1.0.0.1

`scripts\package\make-release.ps1` per build-and-test, on the full flavour
set, with the INF gate, the import gate and the package self-checks green.
Then the release acceptance test on both targets with the new procedure:
Windows 98 must be run once on a guest with no `usbd.sys` and no
`usbhub.sys` and the CABs absent, so that the CD prompt is exercised, and
once with the CABs present.

## What the user sees afterwards

A Windows 98 user without the CABs on disk is asked for the Windows 98 CD
during the driver install, on an xHCI-only machine only; a machine that ever
had a USB 1.1 controller already has both files and flag 16 leaves them
alone. Windows 2000 users see nothing new.

## Also open, not part of this release

- The other Windows 98 matrix targets still run `-machine pc` and will hit
  the QEMU 11.1-rc2 SMM wedge on this host until they carry `smm=off` or
  QEMU 11.0.0 is back.
- A full device-matrix run on the SweetLow guest, and bare metal, have not
  been done.
