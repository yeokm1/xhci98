# Run 47 - Release `3.4.2.0`

The run sheet for release `3.4.2.0` (the 2026-11-06 audit fixes and the
queued `3.4.x` follow-ups, one phase of `roadmap-usb4.md`). This sheet
records what each task did and every reading; the roadmap's task list and
decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

## Boot with a USB stick attached (P14s report)

**The report (owner, 2026-11-06).** On the owner's ThinkPad P14s Gen 1
(Intel, xHCI only) under Windows 98 SE with `3.4.1.0`, a USB stick left
plugged in at power-on keeps the computer from reaching the desktop. The
Windows logo is turned off on that machine; the owner reads the stop as at
or after the point where the logo would be, while Windows loads drivers,
not in the BIOS. Without the stick it starts.

**Reproduced in a weaker form: a stall, not a hang.** Development host A,
the q35 Windows 98 SE base (`E:\xhci98-vm\q35-bases\w98se`, read-only)
through `launch-q35.ps1`, the pinned emulator `asm4242-20261025-2230`
(QEMU 11.1.2, SeaBIOS rel-1.17.0). Its C: is the ICH9 AHCI disk, which
98 SE runs through SeaBIOS's INT 13h (MS-DOS compatibility mode), as the
P14s's NVMe without Stor98. The published `3.4.1.0` `-nonfree` download
installed on a fresh overlay by its console tool (`XHCIINST.EXE /dir
98-me /yes /nopause /storage /thunderbolt /stor98`, exit 3), a cold start,
a stick hot-plugged once so that Windows installed `USBSTOR.INF`, then
that overlay kept as the image every leg below starts from. The stick: a
256 MB raw image with an MBR and a FAT16 partition at LBA 2048
(`usb-storage` on `qemu-xhci` port 3, SuperSpeed). Each leg took a QEMU
trace of `usb_xhci_*` and `usb_msd_*` with host timestamps. The logo is
off on this image too, so a stall shows as SeaBIOS's "Booting from Hard
Disk..." left on a text screen.

| Leg | Configuration | Result |
|---|---|---|
| L1 | stick at power-on | desktop; E: "Bootstick" (the BIOS's INT 13h disk, compatibility mode) and F: (the protected-mode disk) |
| L3 a, b | the same, traced | after xHCI98's controller reset, 15 BIOS requests of 5.1 s each: a stall of 72 s, then the desktop |
| L3 c | the same, `log int` during the stall | the requests are INT 13h from V86 code at `ffff:1e2c` (AX=0000, reset) and `ffff:1779` (AX=0201, CX=0221, DX=0082: read one sector, cylinder 2 head 0 sector 33 = LBA 2048, the partition's boot sector, BIOS disk 82h), repeated |
| L7 | the same with the `qemu` flavour of this tree (`3bafc050`) copied over the installed `xhci98.sys`, a diagnostic swap | 15 requests, 72 s; the driver's own log normal throughout (first answer after the settle in 160 ms, the stick addressed and configured, every transfer completed) |
| L4 | `USBSTOR.SYS` renamed (no protected-mode disk driver) | 35 requests, about 3 minutes on the text screen, then the desktop |
| L6 | a Full-Speed stick behind a `usb-hub` at power-on | 39 requests over 7 minutes; the desktop appeared after about 5 minutes and Explorer did not open while the requests went on |
| L5 | the stick plugged in just after SeaBIOS started the boot sector, so the BIOS never saw it | no BIOS request after the reset, the desktop at once, one letter (E:) |
| L8 | the base image without xHCI98, the stick at power-on | every BIOS request answered at once, the desktop |
| L2 | `-machine smm=off`, with the stick and without it | "While initializing device IOS: Error: An I/O subsystem driver failed to load" both ways: on q35 SeaBIOS serves the AHCI C: from V86 mode only through SMM, so this is the emulator setting and not the report |

**How a BIOS request is told from the driver's own.** Each of the driver's
Bulk-Only commands is a doorbell write to the stick's bulk OUT endpoint
(slot 1, DCI 4) followed in the same microsecond by `usb_msd_cmd_submit`.
The BIOS requests are doorbell writes to the same endpoint with no command
behind them, 5.1 s apart: SeaBIOS rings for a TRB on its own transfer ring,
which the reset controller no longer reads, then gives up after its USB
command time (5 s, plus 100 ms). A doorbell alone does not say who wrote
it; what does is the caller. The instruction pointer sampled every 250 ms
through the stall was in SeaBIOS (`f000:xxxx` in V86 mode, and its 32-bit
code under SMM, entered from V86 mode for the USB disk), and the interrupt
log names the INT 13h requests above. `qemu-xhci` has no USB Legacy Support
capability, so `xhciBiosHandoff` logs "no legacy capability" and neither
the ownership semaphore nor the SMI enables are touched here.

**The cause: Windows 98 reads the BIOS's view of the stick after the host
controller driver has taken the controller.** A stick the BIOS sees at
power-on becomes an INT 13h disk and gets an MS-DOS drive letter from
IO.SYS. During the boot, after `xhci98.sys` has started (and so after the
ownership handoff and the controller reset, xHCI 4.22.1 and 4.2), Windows
98's real-mode disk path reads that disk's boot sector through INT 13h,
with retries. The firmware's USB code no longer drives the controller, so
every request ends at the firmware's own time-out, and the computer spends
that time inside the firmware. With `USBSTOR.SYS` present it ended after 15
requests (72 s) on each of four boots, and the protected-mode disk commands
followed; without it, or behind the hub, it went on for minutes (L4, L6),
and in L6 it went on after the desktop. Which event ends it (the
protected-mode disk taking over, or DOS giving up) is not shown. With the
BIOS never seeing the stick (L5) or with no driver taking the controller
(L8) nothing waits, and nothing in the driver waits during the stall.

What the P14s's firmware does with such a request is not known: how long
its USB command time-out is, how many retries and resets it makes, and
whether it returns at all once the operating system owns the controller.
Anything from SeaBIOS's 5 s to no return turns these 15 to 39 requests into
a stall of a few minutes or a stop. QEMU has no model of a second firmware
path either: the handoff's ownership write comes before the driver clears
the SMI enables (`xhciBiosHandoff`), so where the firmware has its SMI on
an ownership change enabled, that write enters the firmware, and a handler
that does not return (while it tears down its own view of the stick, say)
would stop the computer inside the handoff, where the driver's one-second
wait cannot help. The P14s's register state and its handler's behaviour
there are not read. So the report is consistent with this
mechanism, and on the P14s it is the leading reading, not a measured one.

**Ruled out in QEMU.** The SMM wedge of QEMU 11.1.0-rc2 (lessons.md):
`SMM=0` between requests, the boot goes on, and the pinned emulator is not
that build. xHCI98's start path: the qemu-flavour log shows it finished and
idle (its real-hardware handoff is the second path above, not read). The
protected-mode disk stack (`USBSTOR.SYS`, `USBNTMAP.SYS`): the stall is
longer without it. The audit's unbounded warm reset of a port with Cold
Attach Status is a real defect of its own, but no port here was warm-reset
(QEMU's trace has one port reset of the stick's port, not warm), and it
would cost the stick, not the boot.

**Not this project's code, and what the driver could do.** The handoff and
the reset are what the specification requires of a host controller driver,
and what the firmware then does with its own disk is the firmware's. Two
mitigations, for the owner:

1. A known limitation with its workaround (drafted for the owner): plug
   USB disks in after Windows has started, or turn off the BIOS's USB
   storage support.
2. On 98 SE and ME only, an interception of INT 13h for the BIOS disks
   this controller serves, answering them with an error at once, so the
   firmware is not entered after the reset. It is a design of its own, not
   a fix: the disks must be identified before the handoff and only on
   positive evidence (EDD 3.0 function 48h names the interface and the PCI
   function on many CSMs, not on all, and a guess by drive number or size
   could fail the system disk); IOS's own INT 13h handling of disks a
   protected-mode driver has claimed must keep working, which a hook put in
   above it would break; and the VMM hook's calling convention, its
   lifetime across stop, remove and several controllers, and the context it
   runs in are all unread here. The project's service-call precedent
   (`src\cm\cm_cfgmg.c`) covers the call, not this. Even working, it
   would not give the BIOS letter back, and it does nothing for the
   handoff's own SMI.

**What would settle it on the P14s** (the owner's checklist): with the
stick in, wait 15 minutes and note whether and when the desktop comes;
Safe mode from the F8 menu (no xHCI98, so the firmware keeps the
controller); a Logged boot (`C:\BOOTLOG.TXT`, its last lines read after a
start without the stick); Step-by-step confirmation and the last line
answered; Command prompt only and `dir` of the stick's MS-DOS letter; the
BIOS's USB UEFI BIOS Support turned off, then on again; the stick plugged
in only when "Starting Windows 98" shows; the stick's make, its file system
and the port; and, from a start without the stick, an XHCITR snapshot at
log level 2 for the handoff's notes.

Raw evidence: `E:\xhci98-vm\a342\boot\L*` (command lines, QEMU traces
`qtrace-*.log`, register samples, screenshots, `debugcon-b.log` of L7),
`E:\xhci98-vm\a342\boot\STATE.md`; the counting script
`E:\xhci98-scratch\a342\boot\scripts\orphans.py`.

### Earlier releases, and the other Windows (2026-11-06)

The owner asked whether earlier releases have the same stall, and how 3.4.0.0
behaves on Windows ME, 2000, XP and 7. Same vehicle and stick as above:
the stick image on `qemu-xhci` port 3 (SuperSpeed) at power-on, a fresh
overlay of each q35 base, the release installed by its own route, the
stick hot-plugged once after the desktop (which installed the storage
driver and showed the stick worked), then a cold start with the stick
attached and a traced boot. "BIOS requests" are the counted bulk OUT
doorbells after the driver's controller reset that have no Bulk-Only
command behind them.

Windows 98 SE, one release per overlay:

| Release | Install | BIOS requests after the reset | Desktop |
|---|---|---|---|
| `3.4.1.0` | above (L3 a to c, L7) | 15 over 72 s, four boots | yes |
| `3.4.0.0` | `XHCIINST /dir 98-me /yes /nopause /storage /thunderbolt /stor98`, exit 3 | 15 over 72 s | yes, GUI at about 89 s from the start of QEMU |
| `3.3.0.0` | `XHCIINST /dir release-x86`, the same switches, exit 3 | 15 over 72 s | yes, about 89 s |
| `3.0.0.0` | `XHCIINST /dir release-x86 /yes /nopause` (exit 4, copy only), then Device Manager: the controller removed, Refresh, the wizard pointed at the package | 15 over 71 s | yes, about 90 s |
| `2.0.0.0` | Device Manager, as for `3.0.0.0`, pointed at `RELEASE-X86` | 15 over 71 s | yes, about 87 s |
| `1.2.0.0` | not completed: the owner dropped it (it needs a USB 2.0 stack; begun as the miniport copied over the one on the NUSB 3.3 image `vm\win98.img`, `pc` machine) | - | - |

For `3.0.0.0` and `2.0.0.0`, which install no storage files, the seven
NUSB 3.3 storage files were copied to where the later installer puts them
before the stick was first plugged in. Pointing the wizard at a folder
holding them instead gave "Invalid VxD dynamic link call from NTMAPHLP" at
the first plug; those two overlays were discarded. The same 98 SE overlay
with `3.4.0.0` and no stick reached the GUI at about 65 s with four guests
running. These 98 SE times are the first 640 by 480 frame (the GUI's
start), sampled every 5 s.

`3.4.0.0` on the other systems, one overlay each (ME and 2000 one
processor, XP and 7 two). ME was installed with `XHCIINST /dir 98-me /yes
/nopause /thunderbolt /stor98`, and 2000, XP and 7 with `XHCIINST /dir
nt-x86 /yes /nopause`; every install ended with exit 3. Desktop times are
the first frame, sampled every 5 s, whose Start button matches the
desktop's; four guests ran at once, so they are good to about 10 s, and a
stick boot coming in under its baseline is that noise. The "without"
column is the same overlay with no stick (ME: a clean start, not the one
after an improper shutdown, whose ScanDisk took 97 s).

| System | BIOS requests after the reset | Desktop with the stick | Without it | The stick |
|---|---|---|---|---|
| ME | 28 over 125 s | yes, 120 s | 13 s | E: (when hot-plugged) |
| 2000 | 0 | yes, 29 s | 31 s | E:, `HELLO.TXT` read |
| XP | 0 | yes, 34 s | 47 s | D:, `HELLO.TXT` read |
| 7 (32-bit) | 0 | yes, 34 s | 41 s | D:, `HELLO.TXT` read |
| 2000, a Full-Speed stick behind a hub | 0 | yes, 34 s | 31 s | E:, `HELLO.TXT` read |
| XP, the same | 0 | yes, 45 s | 47 s | not read: Windows' New Hardware wizard for the hub was open |
| 7, the same | 0 | yes, 39 s | 41 s | D:, `HELLO.TXT` read |

No STOP screen anywhere. The counting script reported one request on XP's
SuperSpeed boot, one on each of 7's two and one on L8 above; each is the
driver's own data-out doorbell following a command, not a BIOS request. One ME boot with the stick is not
counted: it came after a launch with the wrong machine profile had left
an improper shutdown, and it showed a black screen with no controller reset
in 6 minutes. Its repeat after a clean shutdown is the row above.

**Reading.** Not a regression: every 98 SE release measured that takes
the controller, back to `2.0.0.0`, stalled the same 15 requests, and ME,
the other 9x system, stalled too. On the 2000, XP and 7 boots no BIOS
request was seen after the reset, and no delay comparable to the 9x stall
was measured (their times are within the 10 s noise above).

Evidence: `E:\xhci98-vm\a342\boot\R34`, `R33`, `R30`, `R20`, `R12`, `MEx`,
`W2K`, `XP`, `W7` (command lines, `qtrace-*.log`, `poll-*` frames, shots,
`pids.txt`); the scripts `orphans.py`, `poll2.sh` and `match.py` in
`E:\xhci98-scratch\a342\boot\scripts`.

## The HCD's controller and bus fixes: build and legs (2026-11-06)

Branch `a342-hcd` at `2697fd74` (from `3bafc050`), development host A.
The controller's thread join, timer drain and bus detach waiting through
`HcdWaitEvent`; the stop holding the power gate through the halt; a START
to a started controller stopped and failed; the fail-closed map pump's
quarantine; an abort's own status for every request it ends; the device
PDO's REMOVE bookkeeping before its completion; the driver-key request
only for a PDO PnP has started; the URB length minimums; and the comment
corrections (`src/hcd_*.c`, `src/hcd.h`, `src/hlpr`). Reviewed and
revised before the build.

### Builds

- Gated x86 `all` (`release`, `qemu`): BUILD + GATES PASSED, host tests
  passed. Log `E:\xhci98-scratch\a342\hcd\build-x86.log`.
- Gated amd64 `all`: BUILD + GATES PASSED, host tests passed. Log
  `E:\xhci98-scratch\a342\hcd\build-amd64.log`.
- Package: `make-package.ps1` (x86 `release`), laid out by family with
  `ConvertTo-FamilyLayout`; the installer's x86 executables built from
  the same tree (`xhciinst\build.cmd x86`). Kit
  `E:\xhci98-scratch\a342\hcd\xfer`. `xhci98.sys` `fac2f180bc84ef6f...`,
  `xhcihlpr.sys` `8c5b6f91d0b90dd3...`, `xhciuas.sys`
  `b6b21455fdd56375...`, `XHCIINST.EXE` `e8bee22f643ec820...`.

### The legs

Fresh overlays of the q35 bases through `launch-q35.ps1`, no USB4 part,
the kit as the transfer disk; a USB stick (`usb-storage`, a 256 MB FAT16
image) added and removed over QMP; driven from the keyboard.

- **98 SE** (`E:\xhci98-vm\a342\hcd\L98`): `XHCIINST.EXE /dir 98-me /yes
  /nopause /storage` exit 3 (`s03.png`); shut down from the Start menu
  and cold-started; the stick plugged at port 3: Add New Hardware
  installed `USBSTOR.INF`, `E:\HELLO.TXT` read (`s10.png`); unplugged and
  plugged at port 4, read again (`s12.png`); Device Manager: the
  controller "working properly" (`s16.png`), disabled - controller, root
  hub and stick all disabled, no hang (`s18.png`) - and enabled, all three
  back (`s21.png`), the file read again (`s22.png`); Start menu shut down
  powered the guest off within 10 s. PASS.
- **XP SP3** (`E:\xhci98-vm\a342\hcd\LXP`): `INSTALL.BAT /console /yes
  /nopause /storage` exit 3 (`s07.png`), the wizards walked; restarted;
  the stick read as `D:` (`s12.png`); Device Manager disable of the
  controller - its children removed, no bugcheck (`s16.png`) - and
  enable, the stick mounted again with its file (`s17.png`); the stick
  surprise-removed, controller and root hub stay (`s18.png`); ACPI
  power-off within 10 s. PASS.

Not run by any leg: a START to a started controller, a quarantined
channel request (it needs real map registers and a controller that does
not stop), a SET_POWER racing a stop, and the PDO-not-yet-started window
of the driver-key request.

## The connection manager and Thunderbolt networking audit fixes: the legs (2026-11-06)

What was changed (branch `a342-cm`, `7b4de1f1` to `a13a6898`): the
connection manager's lifetime waits no longer go on after an unsatisfied
Windows 98 wait (`CmWaitEvent`, `cm_entry.c`); the NT and 9x handover
waits sleep and judge by the clock; the USB4 core runs an owed teardown
tail at resume, bounds each inter-domain pass, and reads a Thunderbolt 3
router's HopID fields at their width; the network miniports wait for the
MAC at initialisation, and the NDIS 5 image's halt, worker, D3 work and
amd64 sends were corrected. Host suites pass; gated `all` builds of x86
and amd64 pass on development host A (`a13a6898`; logs
`E:\xhci98-scratch\a342\cm\build\`).

Package: `make-package.ps1` (release, x86), laid out by
`ConvertTo-FamilyLayout`, with `releases\3.4.1.0`'s `tools\`, `SETUP.EXE`
and `INSTALL.BAT` at the root (`E:\xhci98-vm\a342\cm\kit86`). SHA-256:

- `98-me\xhci98.sys` 7e343cee38b4060a63c6f65f4831704074b29382f3af8b33211539af1a3fc4d2
- `98-me\cm\xhcicm.sys`, `nt-x86\cm\xhcicm.sys` 33ff4b7669441831cab4293b36b69f44ff15bc12541d125fa4dc9ad8752f84f3
- `98-me\cm\tbnet\xhcinet.sys` 9869b4a3474cb4920148979513db51b327037b51a797c01ecc3fbd9f498c745f

A qemu-flavour package laid out the same way is refused by the 3.4.1.0
console tool ("xhci98.sys has 1 build marker(s), the first QEMU; this
folder is RELEASE ... Exit code 1"), so both legs ran the release
flavour.

### 98 SE, `-Usb4 adl` (`E:\xhci98-vm\a342\cm\w98`, s1 to s25)

`launch-q35.ps1 -Os w98se -Usb4 adl` on a fresh overlay of the q35 base,
one virtual processor. The bridges' hardware wizards cancelled, then
`INSTALL /console /thunderbolt /tbnet /yes /nopause`: exit 3. Cold
restart; the two tunnel bridges' wizards completed; the NHI's wizard
("xHCI98 USB4 Router") found `C:\XHCI98\XHCICM.INF` with every box
unticked; at Finish the NHI started and reported the network PDO, whose
wizard ("xHCI98 Thunderbolt Networking") asked twice for the disk
(`C:\XHCI98` typed) and copied the 98 SE network files. Cold restart:
desktop with no wizard and no fault; `winipcfg` lists "xHCI98
Thunderbolt networking" with adapter address `EA-C6-81-87-4F-B0` and
autoconfiguration address 169.254.211.195 (s25): the NDIS 5.0 image
loaded and took a MAC that is not all zeros. PASS.

### 32-bit XP, `-Usb4 adl` (`E:\xhci98-vm\a342\cm\xp`, s1 to s18)

`launch-q35.ps1 -Os xp -Usb4 adl`, two virtual processors. `INSTALL
/console /thunderbolt /tbnet /yes /nopause` (unsigned prompts continued),
exit 3, restart. The adapter's wizard was cancelled by the leg's own
keystrokes (code 28 in `INSTALL /console /list`, the PDO
`XHCICM\TBNET\...` present); the same command again (a same-version
reinstall) installed it and listed "xHCI98 USB4 Router: xhcicm 3.4.1.0,
started" and "xHCI98 Thunderbolt Networking: xhcinet 3.4.1.0, started"
(s15), the devices restarted by the update. `ipconfig /all`: the
adapter, media disconnected (no peer), physical address
`EA-C6-81-87-4F-B0` (s16). `INSTALL /console /uninstall /yes /nopause`:
the adapter halted and the `xhcinet` service removed, the connection
manager and its filters removed with the rest, exit 3, no bugcheck
(s17); the restart that followed reached the desktop (s18). PASS.

Not read in a guest: a peer on the cable (frames, link up and down, D3
with a link up), Thunderbolt 3 routers, a resume, the amd64 images (no
x64 leg; the amd64 build and its import gate pass, including
`MmMapLockedPagesSpecifyCache` for `NdisQueryBufferSafe`).

## Audit 2026-11-06, UV and ST: the legs (2026-11-06)

Branch `a342-uvst` from `3bafc050`, development host A. The legs read the
Stor98 READ CAPACITY(16) fix (`a37de5f7`) through the disk class driver
on a disk above 2 TiB, and install and smoke the changed drivers
(`stor98.sys`, `stor98.mpd`, `xhciuas.sys`, `XHCIPFW.SYS`) on Windows 98
SE, XP and 7.

### Builds and packages

- Gated x86 `all` of `44d8a314`: BUILD + GATES PASSED (release qemu),
  host tests and packager self-tests (1824 checks) passed. Log
  `E:\xhci98-scratch\a342\uvst\build-x86.log`.
- Gated amd64 `all` of `44d8a314`: BUILD + GATES PASSED (release qemu).
  Log `E:\xhci98-scratch\a342\uvst\build-amd64.log`.
- The legs' package: `make-package.ps1 -Arch x86` (release), laid out by
  `ConvertTo-FamilyLayout` into `98-me\` and `nt-x86\`, beside the
  published `3.4.1.0` download's `INSTALL.BAT` and `tools\` (the installer
  is unchanged by this branch). SHA-256 in
  `E:\xhci98-scratch\a342\uvst\hashes-x86-44d8a314.txt` (`stor98.sys`
  `ec9f1d58...`, `stor98.mpd` `c4b01d30...`, `xhciuas.sys` `3e651cd5...`,
  `XHCIPFW.SYS` `afa8a7fa...`).
- `tests/scsichk` built with MSVC 6.0 (`cl /O2`) from `44d8a314`. Its
  INQUIRY page 00h check still expected three pages, so it reported one
  failure on every drive in these legs; the check is corrected in
  `42e7c550`.

### The legs

Fresh overlays of the q35 bases through `launch-q35.ps1 -Usb4 asm4242
-Enc orico -EncAtBoot`, the pinned emulator `asm4242-20261025-2230`. On
the NT guests a QEMU `usb-uas` disk (256 MiB, `scsi-hd`) on the
board's `qemu-xhci` as well. The package installed by its console tool
(`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`; 98 SE
`XHCIINST.EXE /dir 98-me /yes /nopause /thunderbolt /stor98`), a
restart (98 SE: shut down and started cold), then on the NT guests
`CHK.BAT` from the transfer disk: `diskpart` `list disk`, `wmic
diskdrive get index,model,size` and `scsichk` against each physical
drive, copied to COM1.

| Leg | System, package, NVMe disk | Result |
|---|---|---|
| c7 (control) | 7 SP1 x86; the published `3.4.1.0` download; a new sparse qcow2 of 2560 GiB (5 368 709 120 blocks of 512 bytes) | **The disk has no size**: `diskpart` "Disk 2 Online 0 B", `wmic` Size=0. `scsichk`: READ CAPACITY(10) last LBA FFFFFFFFh; READ CAPACITY(16) with 32 bytes last LBA 13FFFFFFFh, block 512; with an allocation of 12 GOOD status but the buffer untouched (CCh), with 8 the same: 2 failures. MODE SENSE's block descriptor `00 ff ff ff` |
| n7 (the fix) | 7 SP1 x86; this branch's package; the same new 2560 GiB disk; the UAS disk | **2560 GB**: `diskpart` "Disk 3 Online 2560 GB", `wmic` Size=2748773422080. `scsichk`: READ CAPACITY(16) with an allocation of 12 returns those 12 bytes (`00 00 00 01 3f ff ff ff 00 00 02 00`, then CCh), with 8 eight; MODE SENSE's block descriptor `ff ff ff ff` (`6bdf8831`). The UAS disk under `xhciuas.sys`: "Disk 2 Online 256 MB". Shutdown 12 s |
| x1 | XP SP3; this branch's package; the FAT NVMe test disk; the UAS disk | Install exit 3; Stor98 by its Found New Hardware wizard; after the restart `diskpart` lists the NVMe disk (126 MB, "QEMU NVMe Ctrl SCSI Disk Device") and the UAS disk (251 MB, "QEMU QEMU HARDDISK UAS Device"); READ CAPACITY(16) with 12 returns 12 bytes. Shutdown clean |
| s98 | 98 SE; this branch's package | Install exit 3; shut down, cold start: Device Manager shows both xHCI98 USB 3 Controllers, both root hubs and the xHCI98 USB4 Router with no problem mark; shut down clean |

Evidence: `E:\xhci98-vm\a342\uvst\c7\`, `n7\`, `x1\`, `s98\`
(`chk-*.log`, the COM1 copy; `s*.png`; `pids.txt`; `cmdline-*.txt`).

Not read here: an x64 guest (the amd64 images built and gated; a leg on
XP x64 or 7 x64 needs the in-guest throwaway signature and is owed), a
UAS device on 98 SE (needs NUSB's mass storage), and an unprivileged open
of `\Device\XHCIUAS<n>\x` on 2000 (the FILE_DEVICE_SECURE_OPEN hardening,
`a973ea8e`).

## The installer's audit fixes (2026-11-06)

Branch `a342-inst` from `3bafc050`, development host A. The 2026-11-06
audit's installer items (the console tool, the window, `SETUP.EXE`, the
helper, `INSTALL.BAT` and the installer's readme), reviewed and revised
until the last round found nothing, then built and read in QEMU. The tip
read is `f2e333f8`.

### Builds

- Gated x86 `release` and `qemu` (`build-driver.cmd all`) of `f2e333f8`:
  BUILD + GATES PASSED (36:04). `test_inst` 8662 checks, 0 failures. The
  XHCIINST self-test passed: the console's screens, the switch refusals,
  the window self-test (971 checks, 0 failures) and the new import check.
  That check holds each image's every imported function to
  `xhciinst\xhciinst-imports.allow`: XHCIINST.EXE 111, XHCIIN64.EXE 127,
  XHCIWIZ.EXE 181, XHCIWZ64.EXE 196, SETUP.EXE 50. Log
  `E:\xhci98-scratch\a342\inst\gated-x86.log`.
- No `-amd64` driver run: nothing of the drivers changed. The x64
  installer images are built by `xhciinst\build.cmd`, and they are gated
  by the same self-test.
- The legs' package: `make-package.ps1 -Arch x86` (exit 0), laid out by
  family (`ConvertTo-FamilyLayout`) into
  `E:\xhci98-scratch\a342\inst\kit\PKG`. The tools are 3.4.1.0's, with this
  branch's installer images, `XHCIDI.EXE`, `XHCITR.EXE`, `SETUP.EXE`,
  `INSTALL.BAT` and readme put in. Hashes are in
  `E:\xhci98-vm\a342\inst\hashes-f2e333f8.txt`: `SETUP.EXE` `9ec0cd57...`,
  `XHCIINST.EXE` `ed9a2445...`, `XHCIWIZ.EXE` `60469360...`, `xhci98.sys`
  `c4d2ced4...`.

### The legs

Each leg ran on a fresh overlay of a q35 base through `launch-q35.ps1`,
with the kit as the transfer disk, driven from the keyboard over QMP
(49500, 49501). Every start and stop is in
`E:\xhci98-vm\a342\inst\pids.txt`, the screenshots are under
`E:\xhci98-vm\a342\inst\g98\` and `gxp\`.

| System | Read | Result |
|---|---|---|
| 98 SE (`g98`) | `INSTALL.BAT /console /yes /nopause /thunderbolt /stor98 /s98vsd /xhcipfw` | the first wait reads "Waiting for the devices to settle ... done." (`s3.png`); the helper installed the controller; **exit code 3** (`s4.png`) |
| 98 SE, cold start | the desktop | reached, the USB Tree in the tray (`b1.png`) |
| 98 SE | `INSTALL.BAT /console /uninstall /list /nopause` | "(/list) S98VSD.VXD would be removed.", "(/list) XHCIPFW.VXD would be removed.", "No changes were made.", **exit code 0** (`b2.png`) |
| 98 SE | `XHCIINST.EXE /bogus` | "unknown argument" and the new usage: "(or XHCIINST.EXE [/dir folder] [switches])", the `/wdmfree:trust` and `/dir folder` rows, "INSTALL /console /list"; every line within 79 columns (`serial-b.log`) |
| XP (`gxp`) | `SETUP.EXE`, then Esc while the Welcome page read "Looking for devices, please wait..." | "Setup will close when it has finished looking for devices." (`g1.png`); the window closed once the reading had ended, with no run made (`g9.png`) |
| XP | `INSTALL.BAT /console /yes /nopause /thunderbolt /stor98 /xhcipfw` | the logo-testing prompts and the root hub's Found New Hardware wizard answered; **exit code 3** (`i4.png`) |
| XP | `/uninstall /list` | among the previews, "(/list) XHCIPFW.SYS would be removed." (`l1.png`) |
| XP, after a restart | `INSTALL.BAT /console /list` | the controller and its root hub on `xhci98 3.4.1.0` ("Reinstall (same version)"), the tunnel-port filter INF's port on `pci 3.4.1.0`, no problem code, "/list: nothing was changed.", **exit code 0** (`p2.png`) |

Both guests were stopped by their own PIDs: 98 SE by an ACPI power-down
(it hangs on a warm restart), XP by `stop.ps1`.

**Not read here:**

- `/restage` on Vista to 10, with a pending work file and different
  component answers, and its new refusals.
- An NT list of more than 1536 devnodes.
- An over-long Service or UpperFilters value.
- The Advanced dialog's veto.
- A pasted value with trailing text.

These rest on reading, on the review rounds and on the host tests of
`inst_dev_cap_next` and `inst_parse_dec` in `test_inst.c`.

## 47.1 The audit fixes of 2026-11-06

**The audit.** On 2026-11-06 the whole repository at `3bafc050` (the
`3.4.1.0` cut) was read in twenty-one parts: the xHCI core, the HCD's bus
and controller sides, the connection manager and Thunderbolt networking,
the UAS driver, the VxDs, `XHCIPFW.SYS` and the INFs, Stor98, the
installer, XHCITR, the other tools, the build and packaging scripts, the
host tests and the documents. It listed about 295 items: 2 High, about 55
Medium, the rest Low. Nothing was changed for it. Its work list was kept
outside the tree, as a file at the checkout's root that was never tracked,
and was deleted once this task's work was merged (owner, 2026-11-06).

**The vetting, the same day.** A second, independent reading took every
item back to the code it cited. It kept 137 as written and 126 with a
corrected severity, description or fix, left 17 to be settled by a
reading or a trace, withdrew 15 (a design choice the item had misread, a
serialisation the system already guarantees, a contract the code already
followed) and added 24 it found at the same sites. It raised to High the
unchecked waits whose early return on Windows 98 lets a teardown free what
is still in use (HCD and connection manager), an isochronous request whose
split final packet could wait for ever for an event the controller never
sends, three installer paths (an uninitialised buffer, a registry loop with
no end, a deletion reported done when the key could not be opened), a VM
script that cleared an existing directory, a Stor98 README that gave
redistributors the old package layout, and `XHCIPFW.SYS`'s version text.

**The owner's decisions taken with it (2026-11-06).** The user documents
carry no testing status (the readings moved to `target-standing.md`; past
releases' sections left as published); `XHCIPFW.SYS` acts on the 32-bit
2000 and XP (with one processor), Vista, 7, 8 and 8.1 and loads and does
nothing on 10, as its code does; `3.4.2.0` is this phase; the emulated controllers' rows left the user
tables for `target-standing.md`; Stor98's review labels were rewritten and
the wording rule covers Stor98 too.

**The work.** Nine branches from `3bafc050` (one per area, and the P14s
reading) and two documentation follow-ups (from `718e6c51` and `1067d1ae`),
each reviewed and revised to convergence before its gated build and its
legs, merged into `3.4.2.0` and each merge reviewed again for how it met the
others:

| Area | Merge | What changed |
|---|---|---|
| xHCI core | `a18d0b60` | the Cold Attach Status warm reset bounded (three resets, a give-up, then nothing); the isochronous final-packet short deferral; a configuration of its 9-byte header alone failed; the EBDA search stopped at `0xA0000`; dead miniport-era code removed or marked host-suite-only; the version header's consumer list and ranking rule |
| HCD | `116e763f` | the event waits that guard a lifetime (the 98 SE and ME thread join, the timer drain, the bus detach, the helper's IRP) through `HcdWaitEvent`; the driver-key request only for a PDO PnP has started; a START to a started controller stopped and failed; the map pump's fail-closed quarantine; the stop holding the power gate through the halt; an abort's own status for every request it ends; URB length minimums; the REMOVE bookkeeping before its completion |
| Connection manager, networking | `3aa545dc` | a completion-wait helper for every 9x lifetime wait; an owed teardown tail run at resume; each XDomain pass bounded; the handover's waits by the clock; the MAC read again after a refused query; halt, D3 and the amd64 send path; Thunderbolt 3 HopID width |
| UAS, VxDs, `XHCIPFW`, INFs, Stor98 | `c1e2e30f` | READ CAPACITY(16) answered to the allocation length; a request that needs no hardware answered after shutdown without restarting the controller; MODE SENSE, LOG SENSE, VERIFY and IDENTIFY corrected; the LUN PDO secured on NT; the abort victim pinned at admission; the VxDs' I/O gates; the redistributors' section and the 4Kn limit documented |
| XHCITR, XHCIPERF, XHCIMARK, XHCIQUAL | `3665502c` | XHCITR on 64-bit Windows hands over to XHCITR64; a symbol-level import gate; verification block sizes; DPMI failures reported as the tool's; seek and close checks |
| Scripts, gates, tests | `c4f1d939` | the transfer staging made fresh per run; the upload set's x64 signatures, certificate and file set checked; every allowlist parsed before link; the charset gate over `xhciinst\` and `xhcimark\`; a spaced path refused; wording and control-byte rules in `check-text.ps1` |
| Documents | `57e77abf`, `e4d9ed31`, `3f1ab702` | the user documents follow the `3.4.x` package; testing status moved out; the maintainer's map, roadmaps, build guide, design records and issue pages follow the HCD; Phase 47 opened |
| Installer | `9a03b9ff` | the three High paths; the NT device list grows; `/restage` identity; Cancel during the first reading; `/uninstall /list` changes nothing and says what it would keep; long filter lists never authorise a removal; a symbol-level import gate |
| The P14s report | `7688325d`, `1067d1ae` | read, not changed: the section "Boot with a USB stick attached (P14s report)" above |

The sections above carry the HCD's, the connection manager's, UAS's and
Stor98's and the installer's builds and legs. The others': the xHCI core's
gated builds, x86 and amd64, passed, and its 98 SE and XP install-and-smoke
legs passed; the tools' gated x86 build and host tests passed, no leg run;
the scripts' gated builds, x86 and amd64, and their self-tests passed, no
leg run; the documents passed `check-text.ps1` and a link check. The
combined gated build of the merged tree (`fb76d1cf`) passed, x86 and amd64. After the
work, `check-text.ps1`'s wording rules (no review tool, review round or
numbered finding in a tracked file outside the dated run sheets; no TAB or
control byte in `docs/`) were made failures (`fb76d1cf`), the tree being
clean of them, and extended to Stor98 (`3f1ab702`).

**Left.** Kept for a reading, a trace or the owner:

- The xHCI core: an error, not a short packet, on the final packet's
  earlier TRB still waits for the tail event; a replug first seen as
  Polling keeps the warm-reset give-up (the owner's policy question); an
  endpoint whose Max Packet Size is outside its speed's table is still
  refused, by design, pending a named device.
- The HCD: the device PDOs' answer to `IRP_MN_QUERY_BUS_INFORMATION` (a
  guest comparison per NT target); a descriptor request whose MDL has no
  system mapping (needs a new import with Windows 98 evidence).
- The connection manager and networking: the S4 resend after a resume from
  the hibernation file (a capture on an admitted USB4 system); whether the
  network PDO's removal can overlap a later parent START or BusRelations
  (read as serialised by PnP, no trace taken); a peer on the cable,
  Thunderbolt 3 routers, a resume and the amd64 images in a guest.
- UAS and Stor98: sense data with autosense off; a bridge that implements
  fewer than 32 bits of Prefetchable Base Upper; Stor98's software answers
  on 9x through the scatter/gather check (a trace each); an x64 guest leg of
  the changed drivers; UAS on 98 SE (with NUSB's storage); the LUN PDO
  opened by a non-administrator on 2000; Stor98's shutdown-state case in the
  emulator.
- The installer: very long HardwareID, CompatibleIDs and Driver values
  still refuse the run (a design exists); stale error numbers after the
  copy chains; unused parameters; the 98 SE `WININIT.INI` read-back and the
  DOS box's wait and `ERRORLEVEL` (a 98 SE reading each); the dead window
  resources.
- The tools: XHCIPERF's outstanding request at exit (a trace); XHCIQUAL's
  "no USB 2 ports" disqualifier (the owner's question); XHCITR's hand-over
  to XHCITR64 on an x64 guest.
- The scripts: a family rule in the upload set's check; the test-signing
  password and key-path items (need the key, the owner's session).
- The documents: eleven "to verify" rows of the SuperSpeed tables (need the
  specifications).

The owner's answers (2026-11-06): the warm-reset give-up after a replug
first seen as Polling stays (a device that trains to Enabled refills the
budget); the descriptor request whose MDL has no system mapping stays
refused (no client is known to send it); Stor98's VERIFY is task 47.22;
XHCITR without XHCITR64 on 64-bit Windows and XHCIQUAL's disqualifier are
task 47.23. The rest of this list is "Planned work", AUDLEFT.1 to
AUDLEFT.8; 47.1 is ticked.

## 47.12 The BIOS's USB disk after the takeover on 98 SE and ME: the reading (2026-11-06)

The task's reading first (roadmap-usb4 47.12, was USBCOMPAT.1, with the
owner's P14s report above): who sends INT 13h to the unit the firmware
made of a USB stick after `xhci98.sys` has taken the controller, whether
IOS can be made to stop using that unit, and how such a unit could be
named positively before the handoff. No driver code; the design is
design record 37, put to the owner.

### The vehicle

Development host A; the q35 bases through a copy of `launch-q35.ps1`
(the pinned emulator `asm4242-20261025-2230`, QEMU 11.1.2, SeaBIOS
rel-1.17.0; QMP on a named pipe, since this worker's port range lies in
the host's excluded TCP ranges). The published `3.4.1.0` `-nonfree`
download, unchanged since the run above, installed by its console tool on
a fresh overlay of each base: 98 SE `XHCIINST.EXE /dir 98-me /yes
/nopause /storage /thunderbolt /stor98`, ME the same without `/storage`;
exit 3 on both; a cold start, the boot worker's stick image (256 MB, MBR,
FAT16 at LBA 2048, `HELLO.TXT`) hot-plugged once (98 SE: the wizard
installed `USBSTOR.INF`; ME: Removable Disk E:), a shut-down, and that
overlay kept read-only as the installed image. Each leg below is an
overlay of it with a fresh overlay of the stick on `qemu-xhci` port 3
(SuperSpeed) at power-on, traced with QEMU's `-d int` from the start
(every interrupt and exception taken in protected and virtual-8086 mode,
with the registers) and the `usb_xhci_*`, `usb_msd_*` and `scsi_req_parsed`
events, host timestamps on both.

### Readings

| Leg | System | What was read |
|---|---|---|
| L1 | 98 SE | the stick at power-on, interrupts and USB events from the start to the desktop and the shut-down |
| M1 | ME | the same |
| E1 | 98 SE | the stick at power-on, the startup menu (`BootMenu=1` written to this overlay's `MSDOS.SYS`), "Command prompt only", then INT 13h functions 41h and 48h for units 80h, 81h and 82h through `DEBUG.EXE` |
| N1 | 98 SE | the clean q35 base (no xHCI98), the stick at power-on, traced as L1 |
| E2 | 98 SE | E1's overlay again, the same, function 48h with its buffer cleared and its size word 4Ah (74 bytes, the final EDD 3.0 layout) |

**Who sends the requests [emu].** On both systems every INT 13h a
virtual-8086 program issued after the takeover came from the MS-DOS
disk code in the high memory area (98 SE `ffff:1779` CHS read, `ffff:1e2c`
reset, `ffff:184a` extended read and write; ME `ffff:15b5`, `ffff:1c67`,
`ffff:1686`); the requests to the stick carried `DS` 0070h, IO.SYS's own
segment (the extended requests to C: `DS` FFFFh). None went to unit 82h
before the takeover on either system: after VMM had started, the only
INT 13h before it went to unit 80h (the AHCI C:, 67 extended reads on 98
SE; 127 extended reads and 9 extended writes on ME), and no function 08h,
15h, 41h or 48h was issued to any unit in protected or virtual-8086 mode
on either. The requests to 82h came in groups, each one MS-DOS request
with IO.SYS's retry loop (`BP` counting 5 down to 1, a reset, AH=00h,
between attempts):

| System | Takeover (controller reset) | Groups to 82h answered by the firmware | Each attempt | Then |
|---|---|---|---|---|
| 98 SE | 02:22:22.944 | 3: the partition's boot sector (CHS 2/0/33, LBA 2048), the first root directory sector (CHS 2/4/41, LBA 2308), the boot sector again; 15 reads and 15 resets from 02:22:23.613 to 02:23:40.153 | the read ended after 5.1 s with AH=0Ch and CF set; the reset at once with AH=01h | from 02:23:45.730 two more reads of 82h (the boot sector, the root directory) answered in 1 and 2 ms by the protected-mode USB disk (below) |
| ME | 02:22:26.244 | 4: the boot sector, LBA 2049 twice, the boot sector; 20 reads and 20 resets from 02:22:26.746 to 02:24:08.785 | the same | from 02:24:13.748 every request to 82h answered in 1 to 2 ms by the protected-mode disk: 20 reads and 10 writes (the first sector of each FAT copy, LBA 2052 and 2180, the root directory, LBA 2308, and data sectors from LBA 2356; the two FAT writes again at the shut-down, 02:29:41) |

Each request to 82h asked one sector in the window (`AL` 01h); the later
ones served in protected mode asked up to eight. The stick's layout, read
from its image: reserved 4 sectors, two FATs of 128, 512 root entries,
so the FATs at LBA 2052 and 2180, the root directory at 2308, the data at
2340.

Each group began inside the MS-DOS kernel (high memory, `ff39:xxxx` on
98 SE, its own stack at 00C9h), entered from protected mode: the
virtual-8086 work around it returned to VMM's breakpoint address
(`FFB17h`, as `fcb2:2ff7` and other segment:offset pairs) on a stack
VMM had given it (7700h), and the ring-0 code between those returns made
first-time VxD service calls (INT 20h) from `C037C6C9h` to `C037DAFCh`. On
98 SE the protected-mode loop around them called into virtual-8086 mode
with `BP` 2, 3 and 4 in turn (the zero-based MS-DOS drive numbers of C:,
D: and E:, the stick's letter) and `DX` 42h to 44h and C2h to C4h with
them, with MS-DOS's redirector hook (INT 2Fh AX=1120h) and its critical
sections (INT 2Ah AH=80h to 82h) at every step: a protected-mode
component walking the MS-DOS drives through MS-DOS [inf from the
registers, on 98 SE; ME's loop was not decoded as far]. Which VxD that
component is was not read (no module map was taken, and nothing of a
Microsoft image was read).

**The path a request took to the firmware [emu].** Within each attempt:
INT 13h in IO.SYS's code, then IO.SYS's own INT 13h hook in low memory
(`029a:xxxx`, which locked and unlocked the buffer through the virtual DMA
services, INT 4Bh AX=8103h and 8104h), then a VMM callback in the
real-mode chain below it (`fd41:2707`, the breakpoint address), then the
firmware (`f000:xxxx`, its USB code entered through SMM for the 5.1 s),
then a second callback on the way back (`fd55:25c7`, with the result in
the registers). The callbacks belong to a protected-mode INT 13h layer
in the chain on both sides of IO.SYS's hook [inf: IOS's, the DDK's
`Int13_*` services and `DCB_DEV_I13_IN_PROGRESS` describe it; not read
in the code].

**The unit is then served in protected mode, with no driver of this
project's doing it.** Once the groups ended, the protected-mode USB disk
(its first commands, INQUIRY
to REQUEST SENSE, at 02:22:23.479 on 98 SE and 02:22:26.647 on ME, before
the first firmware request) resumed at 02:23:43.776 and 02:24:12.388,
and from then on a virtual-8086 INT 13h for unit 82h entered a callback
at the head of the INT 13h chain (`fd5b:2567` on 98 SE, `fd59:2587` on
ME) and was carried out by ring-0 code (`C00745xxh` on 98 SE) as USB mass
storage commands of the protected-mode driver (their tags `0xc1946454`
and `0xc196c294`, not the firmware's `0x3e7`), each completed in one or
two milliseconds, writes included on ME [emu]. That IOS had by then
associated BIOS unit 82h with the protected-mode disk, as the DDK's
`ISP_ASSOCIATE_DCB` ("associates ... an int13 drive number with a
physical") and `DCB_DEV_INT13_DRIVE` describe, is an inference: no IOS
request, DCB flag or association step was observed, nor the moment the
protected-mode service began [inf]. The failing groups ended with
IO.SYS's last retry of the last request, before the disk's configuration
went on, so that service did not end them [emu]. The disk's next command
came 3.6 s after the last firmware request on 98 SE and on ME [emu]; that
it waited because the system VM was held in the firmware is [inf].

**Identification [emu].** At "Command prompt only", the firmware still
the controller's owner, `DEBUG.EXE` called INT 13h function 41h
(BX=55AAh) and 48h (a 66-byte buffer, its size word 42h) for each unit:

| Unit | Function 41h | Function 48h's answer |
|---|---|---|
| 80h (AHCI C:) | AH=30h (EDD 3.0), BX=AA55h, CX=0007h | size 1Ah (26 bytes: no DPTE, no device path); flags 0002h, 16 heads, 63 sectors, 8 388 608 sectors (82 20 00 00 / 00 00 80 00 in the dump) |
| 81h (the transfer disk) | the same | size 1Ah; flags 0002h, 1 032 192 sectors |
| 82h (the stick) | the same | size 1Ah; flags 0000h, cylinders 3FFFh, heads 0, sectors per track 0, 524 288 sectors |

To the 74-byte request of E2 (the buffer cleared, `e 200 4a 00`, then
`f 202 l 48 0`) the answers were the same: function 41h AH=30h, BX=AA55h,
CX=0007h for each unit, and function 48h a size word of 1Ah with the 26
bytes above and zeros from 1Ah to 49h, no key BEDDh, for 80h, 81h and 82h.
So this firmware names no interface and no PCI function for any of its
disks, to either request, and a unit cannot be named by its EDD path in
the emulator. What the P14s's firmware answers is not read.

### What it says

- The requests after the takeover are MS-DOS requests to the stick's
  real-mode letter, made by IO.SYS's disk code under the MS-DOS kernel,
  IO.SYS retrying each five times with a reset between [emu], for a
  protected-mode component that walks the drives during the boot [inf,
  98 SE].
- Every one of them passes a protected-mode INT 13h layer in the
  real-mode chain before the firmware [emu]; after the window the same
  kind of request never reached the firmware and was served by xHCI98's
  stack [emu], IOS's association of the unit being the inferred cause
  [inf]. The firmware's answers in the window end in an error after its
  time-out, so a request failed at once there would also fail; whether
  MS-DOS and the component above treat a quick failure as they treat a
  slow one is not read.
- No IOS setting that retires a BIOS unit's real-mode path before its
  association was found among the DDK's names (`DCB_DEV_REAL_MODE`,
  `DCB_TSD_NO_USER_INT13`, `DCB_TSD_PROTMODE_INT_13` and
  `DCB_DEV_INT13_CONFIGURED` are named, their use by another driver not
  documented there) [ddk].
- The pinned SeaBIOS answered function 48h with 26 bytes for every unit
  (E1, E2), so positive identification by the EDD 3.0 path is a reading
  owed on the P14s (design record 37, the owner's question).
- ScanDisk was not exercised; 98 SE's runs before Windows when it runs,
  while the firmware still owns the controller [inf].

**Without the driver (N1) [emu].** The clean q35 98 SE base (no xHCI98,
so the firmware keeps the controller), the stick at power-on, traced as
L1: the same drive walk (MS-DOS's INT 2Fh AX=1120h steps) began 2.23 s
after the first protected-mode interrupt (L1: 2.77 s, the takeover at
2.13 s), and read the stick's boot sector once at 2.26 s (L1: 2.80 s),
answered by the firmware at once; 6.5 s later a second caller, on the
virtual-8086 stack L1's protected-mode reads used (`0a91`), read the boot
sector and eight root directory sectors, again answered by the firmware.
So the walk is a step of the start that comes with or without the
driver, about half a second after the point where the driver takes the
controller, and the stick is read again seconds later [emu; the timing
compared across two starts].

Evidence: `E:\xhci98-vm\a347\int13\L1`, `M1` (`qlog-a.log` the raw
trace, `intsum-a.txt` one line an interrupt, `i13-a.txt` the INT 13h
census, `poll-a\` frames, `pids.txt`), `N1` (the same files), `E1` (`edd-fn41.txt`,
`edd-fn48-80-81.txt`, and E2's `edd2-fn41.txt`, `edd2-fn48.txt`: the screen text), `g98` and `gme` the installed
images, `STATE.md`; the scripts in `E:\xhci98-scratch\w47\int13\scripts`
(`intsum.py`, `i13.py`, `leg.ps1`, `qmpd.py`) and the DEBUG scripts in
`E:\xhci98-scratch\w47\int13\xfer341` (`EDD80.SCR` to `EDD82.SCR`, in E2's 4Ah form).

## 47.14 A filter in the PCI root bus's stack on the 32-bit 10: read again (2026-11-06)

PFW8.1's reading (roadmap-usb4 47.14; design record 32 sections 11.2 and
11.5): does a filter in the PCI root bus's stack act before the PCI driver
reads the bridges, at every start read, on the 32-bit 8, 8.1 and 10? For 8
and 8.1 task 45.9 read it at every start (six and five starts with the
probe, three each with `XHCIPFW.SYS`'s filter role; `runs/run-45.md`,
"45.9" and "45.9 (continued)"), and `3.4.0.0` ships that role there. On 10
45.9's probe was never initialised and the start failed; this reads 10
again.

### The vehicle

Development host A; q35, a copy of 45.9's 4G-off launcher (the emulator
`bin\asm4242-20261026-4goff`, `qemu-system-x86_64.exe` `08D49470...`; QMP
on a named pipe), the 4G-off layout with the ORICO at power-on, `qemu64`
processors, two, 2 GB; a fresh overlay of the clean q35 10 x86 base
(19041). The probe is 45.9's own image, unchanged (`pfwflt.sys`, SHA-256
`37ffe267a2814f673b446c65cdb802283728f20e948666b74b60481807790a8a`):
it marks its `DriverEntry`, each `AddDevice` and its first
`IRP_MN_START_DEVICE` in the configuration trace (three reads each of
00:00.0 at `F8h`, `F4h` and `F0h`) and on the debug console, and runs
record 32's step inside `KeIpiGenericCall` at that first start before
passing it down. Installed on the board without the USB4 model by a batch
run from an elevated prompt, two things done differently from 45.9: the
service written by `reg add` (`Type` 1, `Start` 0, `ErrorControl` 0,
`Group` `PnP Filter`, `ImagePath` `REG_EXPAND_SZ`
`System32\drivers\pfwflt.sys`; 45.9's `.reg` file had left the path to
its default), and `pfwflt` written as the `UpperFilters` of
`Enum\ACPI\PNP0A08\0` only, by a task run as SYSTEM (45.9 wrote it as the
upper and the lower filter); `powercfg /h off`. The install's own
`reg query` read the service and the one `UpperFilters` line back. Then
cold starts on the 4G-off layout with a read and write configuration
trace; the problem codes by `wmic` to COM1 at the desktop.

### Readings

| Start | Trace | `DriverEntry` | `AddDevice` | start | step's close of the root port | PCI driver's probe of the root port | Result |
|---|---|---|---|---|---|---|---|
| a | RW | 14 603 | 14 606 | 14 609 | 31 189 | 32 328 | the desktop; every bridge and the xHCI problem 0, only the NHI (`1B21:2425`) and QEMU's `fw_cfg` 28 |
| b | RW | 14 603 | 14 606 | 14 609 | 31 189 | 32 328 | the same |
| c | W | - | - | - | 446 | 593 | the same |

The debug console showed the same sequence at every start, the 8 and 8.1
sequence of "45.9" for the upper filter: "pfwflt DriverEntry"; one
`AddDevice`, for `ACPI\VEN_PNP&DEV_0A08`, attached over the PCI driver's
object; `IRP_MN_QUERY_LEGACY_BUS_INFORMATION` (18h),
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS` (0Dh) and `IRP_MN_START_DEVICE`
(0), each to the filter first; the step's line `1 12 9 0` (18 functions,
nine bridges closed, none refused); then the later requests passed down.
The ASM4242's bridges, both enclosure bridges, the ASM2464 and its NVMe
were all at problem 0 at the desktop.

Then, to tell which of the two install differences mattered, the same
overlay with `pfwflt` added as the `LowerFilters` of `ACPI\PNP0A08\0`
too (a SYSTEM task again; the batch's `reg query` read both lines back),
and one more cold start:

| Start | Trace | `DriverEntry` | `AddDevice` (lower, upper) | start | step's close of the root port | PCI driver's probe of the root port | Result |
|---|---|---|---|---|---|---|---|
| m (upper and lower) | RW | 14 603 | 14 606, 14 609 | 14 612 | 31 192 | 32 334 | the desktop; the same problem codes as a to c |

Its debug console showed 45.9's 8 and 8.1 sequence for both positions:
the lower filter attached over the ACPI driver's object, three
`IRP_MN_QUERY_INTERFACE` through it, the upper filter's `AddDevice`, then
the start to the upper filter first and the step's `1 12 9 0`. So the
lower filter was not what kept 45.9's probe from being initialised on 10;
the one difference left between the two installs is the service's
`ImagePath`, absent in 45.9's (its `.reg` line had not taken, so the
default path served) and written here [inf].

### What it says

- On 10 x86 a boot-start upper filter of the PCI root bus, named by
  `UpperFilters` alone with its service's `ImagePath` written as
  `REG_EXPAND_SZ`, is initialised, gets the root bus's `AddDevice` and its
  first start, and acts there before the PCI driver probes the root
  port's window, at every start read (three), at the same trace lines
  each time, as on 8 and 8.1 [emu]; named as both filters it did the same
  at the one start read [emu]. 45.9's 10 readings describe its install,
  not the place [inf].
- So the place record 32 section 11.5 chose for 8 and 8.1 acts in time on
  10 too [emu]; 10's role (record 32 section 11.5.2 gives it none) and
  its code stay planned work (PFW8.1), as the task says.

Evidence: `E:\xhci98-vm\a347\int13\T1` (`trace-*.log`, `debugcon-*.log`,
`serial-*.log`, `leg-*.txt`, screenshots, `pids.txt`; `T1-i.txt` the
install), the kit `E:\xhci98-scratch\w47\int13\xflt10` (`I.BAT`,
`SYS.BAT`, `L.BAT`, `SYS2.BAT`, `W.BAT`, the probe image) and the scripts
`leg10.sh`, `leg10b.sh`, `cold10.sh` and `launch4g.ps1` in
`E:\xhci98-scratch\w47\int13\scripts`.

## 47.12 The handoff's diagnostic notes: the build and a leg (2026-11-06)

Design record 37 section 9, coded at `deb72ed9` (reviewed):
`handoff.legctl.before`, `handoff.legctl.after` and
`handoff.legsup.later` in the note ring.

**Builds.** The gated `release` builds at `f8e74028` (the code at
`deb72ed9`, docs after it), x86 and amd64, `BUILD + GATES PASSED` both;
`xhci98.sys` x86 SHA-256 `aa35db727bf855ef68f443f2381d9d58adada6f7dbce6108d2cd54d5e74f3067`
(338 962 bytes), amd64 `8dd9aaa4f0c03042c0ba47de658567430eb59fd49bc4bc038dd197c283373427`.
Logs: `E:\xhci98-scratch\w47\int13\build-x86.log`, `build-amd64.log`.

**The leg (R1).** No xHCI model in the pinned emulator offers a USB
Legacy Support capability (`qemu-xhci` has none, `xhciBiosHandoff`
logging "no legacy capability"; the ASM4242 model's xHCI is built on the
same QEMU model [inf]), so the notes cannot fire there; the leg reads that
the image changes nothing else. An overlay of the 98 SE installed image
above (`3.4.1.0`), the gated x86 `xhci98.sys` copied over the installed
one from a transfer disk (a diagnostic swap; `FC /B`: "no differences
encountered"), then a cold start with the stick at power-on, traced:
15 firmware requests after the takeover, as before; the desktop; XHCITR
`/snapshot` listed `xhci98.sys` at 338 962 bytes (the published one is
337 499) and its dump's only `handoff` line was `handoff.cap=00000000`;
`F:\HELLO.TXT` read through the protected-mode letter ("stick present at
power-on"); the shut-down ended QEMU. The notes themselves are read on
hardware with a legacy capability (the bench; the P14s's snapshot).

Evidence: `E:\xhci98-vm\a347\int13\R1` (`qtrace-b.log`, the screenshots
`fc.png`, `t.png`, `h3.png`, `f.png`, `pids.txt`).

## 47.13 The platform USB4 handover: ME's fault legs and the structural row on NT (2026-11-06, w47-cm)

46.12's emulator parts (roadmap 47.13): the fault legs of record 32
section 12.8.8 on ME (98 SE's are "46.12 The 9x fault legs" in
`runs/run-46.md`), and the structural row on NT with the read path's INF
change (record 32 sections 12.8.15.12 and 12.8.15.17). The code of both
was in the tree already (3.4.1.0); this task read it. Design record 32
section 12.8.17 says what the readings settle; `usb4-emu`'s
`docs/USB4-HOSTS.md` section 9.6 and the header of
`scripts/asm-fw-ssdt.py` gained the ME reading (`usb4-emu` branch
`w47-cm`). Both reviewed and revised.

**The build.** Branch `w47-cm` at `f19492e5` (3.4.2.0's tip, no code
change): `src\pfw`, `xhciinst\di`, `xhciinst`, `xhcitr`, `xhcimark`
built, then the gated x86 `all` build: "BUILD + GATES PASSED (release
qemu)" (`E:\xhci98-scratch\w47\cm\gated-x86-all.log`). The release kit by
`make-package.ps1 -Flavor release -Arch x86` and `ConvertTo-FamilyLayout`,
the tools, `SETUP.EXE` and `INSTALL.BAT` at its root
(`E:\xhci98-scratch\w47\cm\kit`, sums `kit-sums.txt`): `xhcicm.sys`
release `ec38ba8c...` (98-me and nt-x86 the same file), qemu
`3db3323a...`; `xhcihlpr.sys` `5d11ede9...`; `XHCIINST.EXE` `616b77db...`,
`XHCIWIZ.EXE` `ae994c54...`, `XHCITR.EXE` `5765fa53...`. The stamp is
still 3.4.1.0 (the version bump is 47.final's).

**The emulator.** `usb4-emu` main `8f200d5` (the mailbox faults,
`docs/FAULTS.md` "The ASM4242 handover mailbox"), its binary
`qemu-system-x86_64.exe` `e68ad1bc...` (QEMU `18573d0`, w46-flt's,
copied); q35, TCG; the ASM4242 model with `pm-cap=on`, `fw-handover=on`,
`usb4-host fw-handover-gate=on` (the router silent until the handover)
and `acpi-sb-osc=off`; the three tables of `scripts/asm-fw-ssdt.py
--build N --osi --externals-ifzero` by `-acpitable`, at `0x7FF00000` on
ME (512 MB) and `0x9FF00000` on 10 x86 (2 GB), each set's SHA-256 in
`E:\xhci98-scratch\w47\cm\aml\`. Every leg a cold start; QMP ports 47960
to 47971 (49720 to 49739 lie in the host's excluded port range); every
start and stop in `E:\xhci98-vm\a347\cm\pids.txt`.

### ME: the emulator's tables

The q35 ME base (`42fea19a...`, which runs ACPI), a fresh overlay, no
xHCI98 driver, the model as above:

| Tables | Result |
|---|---|
| build 1 `--osi`, `External`s at the top level (the script's default) | "While initializing device CONFIGMG: Windows protection error. You need to restart your computer. System halted." (`t1\s1.png`) |
| build 1 `--osi --externals-ifzero` | the desktop (`t0\s1.png`) |

So ME's legs run on ME with ACPI, with the switch; the methods' bytes and
hashes are the same (`cm.osc.method.hash` below), and the release
flavour's lists are unchanged. 98 SE with ACPI stopped with the same
message in "46.12"; it was not tried again with the switch.

### ME: the legs

The vehicle: an overlay of the ME base with this kit installed
(`XHCIINST /dir 98-me /yes /nopause /thunderbolt`, exit 3: both
controllers on `xhci98.sys`, "xHCI98 USB4 Router (ASMedia ASM4242):
xhcicm 3.4.1.0, started"), the qemu `xhcicm.sys` copied over it and
checked (`fc /b`), one settling cold start, frozen read-only as
`E:\xhci98-vm\a347\cm\mb\disk.qcow2` (`e93aa4dd...`); each leg a fresh
overlay of it, one vCPU, 512 MB, build 1's tables. Read in each: the
qemu flavour's log on the debug console, the model's mailbox lines in
QEMU's output and `x-mb-state`, the NHI's `XhciCmStatus` record (flags:
bits 18:16 the tier, bit 19 the handover written this boot), decoded
from the `Enum\PCI` export.

| Leg | Fault | Seen | 98 SE ("46.12 The 9x fault legs") |
|---|---|---|---|
| a1 | none | the three SSDTs' ids; the five hashes `43EB7CDC`, `2FF061F8`, `CDA93C5F`, `FE35E156`, `408891DF` (build 1 `--osi`); `cm.osc.tier=2`; the contract lines; `dusb4.10=2`, `4d=1`, `4b=0`; `cm.osc.root=300h`; `cm.osc.mb.before=0`; `cm.osc.handover=100h`; `cm.osc.query=000F0100h`; the model took `0102h` and `0180h` once each; `u4.host.id=174C2428`, `u4.start=0`; flags `000A0000h`, stage active | the same |
| a1 | the NHI disabled ("This device is disabled (Code 22.)") and enabled in Device Manager | the second start: `cm.osc.mb.before=00000280h`, `cm.osc.already=00000100h`, no third command (two in QEMU's output); the router answering; "This device is working properly."; that start's record (uptime 559 s): flags `000A0000h`, stage active | the same |
| b1 | `x-mb-busy-ms=4294967295`, `x-mb-busy-reset=on` | `cm.osc.mb.before=00000100h`, `cm.osc.refused=00000107h`; no command (`commands=0`, `busy=yes`); `u4.failed=0000000Ch`; flags `00020000h` (tier 2, bit 19 clear), stage failed | the same |
| b2 | `x-mb-busy-ms=4294967295` | `cm.osc.not-accepted=00000102h`, `cm.osc.handover=FFFFFFFEh`, no query; the model "0102 held busy"; `u4.failed=0Ch`; flags `000A0000h`, stage failed | the same |
| b3 | `x-mb-busy-ms=300` | `cm.osc.accepted-ms=12Eh` for both commands, `100h`, `000F0100h`; the model "taken after 300 ms" for both; the router answering; flags `000A0000h`, stage active | the same (98 SE's counts `106h`, `F4h`) |
| c1 | `x-mb-no-reply=on` | `cm.osc.accepted-ms=0`, `cm.osc.no-reply=0`, `cm.osc.handover=FFFFFFFDh`, no query; the model "taken, no reply" once; `u4.failed=0Ch`; flags `000A0000h`, stage failed | the same |
| c1 | the NHI disabled and enabled | the second start: `cm.osc.mb.before=00000202h`, `cm.osc.already=00000100h`, no second command (`commands=1`, `unanswered=1`); `u4.failed=0Ch`; its record (uptime 418 s): flags `000A0000h`, stage failed | the same |
| d1 | none; the release `xhcicm.sys` copied over the qemu one (checked), a cold start | XHCITR's report (`XHCITR /report`): the five hashes and five forms, `cm.osc.tier=1`, `cm.osc.refused=0Dh`, `u4.failed=0Ch`, "Stage: failed"; `x-mb-state` `commands=0`; flags `00010000h` | the same |
| f1 | build 2's tables, `XhciCmAsm4242Structural` absent | the five hashes in no list (`_OSC` `2B75E85D`), the five forms build 1 `--osi`'s; `cm.osc.tier=3`, `cm.osc.refused=0Eh`; no command; `u4.failed=0Ch`; flags `00030000h`, stage failed | s1 (in "46.12 The second tier") the same |
| f1 | the same overlay, `XhciCmAsm4242Structural` 1 under `Services\USB` (`regedit /s`, read back by `regedit /e`), a cold start | `cm.osc.tier=3`, `cm.osc.root=300h`, `cm.osc.mb.before=0`, `cm.osc.handover=100h`, `cm.osc.query=000F0100h`, both commands once, `u4.host.id=174C2428`; flags `000B0000h` (tier 3, bit 19), stage active | s2 the same |
| m3 | build 3's tables | `_OSC` differs; `cm.osc.tier=1`, `cm.osc.refused=0Dh`; no command; `u4.failed=0Ch`; flags `00010000h` | m1 the same |
| r5 | build 1, the firmware data naming another root port (`fw-data-root-dev=5`) | `cm.osc.tier=2`, `cm.osc.root=00000500h`, `cm.osc.refused=0Bh`; no command; `u4.failed=0Ch`; flags `00020000h` | n2 (in "46.12") the same |
| r5 | the same overlay, `XhciCmAsm4242Handover` 0 under `Services\USB`, build 1's data as a1, a cold start | `cm.osc.off`; no command; `u4.failed=0Ch`; flags `00000000h` (tier 0) | n0 the same |
| e1 | `x-mb-busy-ms=1500` (past the driver's 1000 ms; not run on 98 SE) | `cm.osc.not-accepted=00000102h`, `cm.osc.handover=FFFFFFFEh`, no query; the model "taken after 1500 ms", its reply `100h` written and left (`cmd=00000202 reply=00000100 ... late=1 ... handover=done`); the router then answered (`u4.host.id=174C2428`, `u4.start=0`); flags `000A0000h`, stage active | not run |

A first start of each of the two `Services\USB` legs was void: the
`regedit /s` typed into the leg's console after its snapshot never ran
(the key read back empty, and the start behaved as the default); the
value was imported again and read back before the cold starts above.

So on ME each leg run ends as record 32 section 12.8.8 says, as on 98 SE.
Not run on ME: a tunnelled drive hot-plugged after the handover (the leg
image has no Stor98; 98 SE's h1 read it) and the installer's question
(98 SE's s1i; the same code on ME).
e1: the driver gives up at its bound and sends nothing more, as section
12.8.5 says; the late acceptance makes the handover anyway, and the start
goes active while the log says the handover failed. Nothing changed for
it (record 32 section 12.8.17).

Evidence: `E:\xhci98-vm\a347\cm\` `t0\`, `t1\`, `mb\`, `a1\` to `e1\`,
`f1\`, `m3\`, `r5\` (tags `b`; `d` for the `Services\USB` starts, `c` the
void ones; `debugcon-*.log`, `qemu-b.out`, `serial-b.log`: the `/dump`, the
`Enum\PCI` export and the record; `mbstate-b.txt`, `infopci-b.txt`,
`cmdline-*.txt`; `a1\dm-*.png` and `c1\dm-*.png` the Device Manager
steps; `d1\serial-b.log` the report), `pids.txt`; the record decoder
`E:\xhci98-scratch\w47\cm\st.py`.

### NT: the structural row, read on 10 x86

The q35 `w10x86` base (build 19045), one fresh overlay (`n10`) through
the legs, two vCPUs, 2 GB, a usb-tablet; no `-smbios` profile, so route E
finds the board unknown (`cm.osc.smbios.refused=0Ch`) and the read path
runs. The release kit installed from an elevated console with
`INSTALL.BAT /console /yes /nopause /thunderbolt /set
XhciCmAsm4242Reads=1` (Windows' "can't verify the publisher" answered
"Install this driver software anyway" each time; exit 3: the NHI on
`xhcicm`, the switch ports and root ports given the filter, "xhcicm: added
to the UpperFilters of PCI\VEN_1B21&DEV_2426...", "XhciCmAsm4242Reads set
to 1 for all controllers."); a cold start (the helper `xhcihlpr` then
running), the qemu `xhcicm.sys` put in place (SHA-256 checked at every
step), then the legs, each a cold start (`shutdown /s`).

| Step | Seen |
|---|---|
| after the install | the NHI's driver key (`{4D36E97D-...}\0035`): `XhciCmAsm4242Handover` 1, `XhciCmAsm4242Structural` 0, `XhciCmAsm4242Reads` 0 (the INF's `[Cm.Values]` and `[Cm.ValuesNt]`); `Services\USB`: `XhciCmAsm4242Reads` 1 |
| c: build 2, Structural 0 | `cm.osc.nt.build=4A65h`; `cm.osc.probe=0`; `cm.osc.read.route=1` (B, the routines through the helper), `cm.osc.read.uefi=C0000002h`, `cm.osc.read.scan=0`; the three SSDTs; five hashes in no list (`_OSC` `2B75E85D`), the five forms build 1 `--osi`'s (`_OSC` `9616AF17 652CB991`); `cm.osc.tier=3`; `cm.osc.read=0Eh`, `cm.osc.refused=110h`; no `cm.osc.dusb4` line (no pointer of `\CPNV` followed); no mailbox command (`commands=0`); `u4.failed=0Ch`; XHCITR "Stage: failed" |
| `/set XhciCmAsm4242Structural=1` | `INSTALL.BAT /console /yes /nopause /thunderbolt /set XhciCmAsm4242Structural=1`: "XhciCmAsm4242Structural: set to 1 by /set." with the row's note ("... On 2000 and later it counts only when unknown ASM4242 boards are set to yes. Restart Windows after changing it."), "Save the changed settings now (no device needs a new driver)", "XhciCmAsm4242Structural set to 1 for all controllers.", exit 3; `Services\USB` `XhciCmAsm4242Structural` 1, the driver key still 0; the qemu image left in place |
| d: build 2, Structural 1 (machine-wide) | `cm.osc.tier=3`, `dusb4.10=2`, `4d=1`, `4b=0`, `cm.osc.root=300h`, `cm.osc.read.refused.pages=0`, `cm.osc.members=6`, `cm.osc.route=100h`, `cm.osc.mb.before=0`, `cm.osc.handover=100h`, `cm.osc.query=000F0100h`, `u4.host.id=174C2428`, `u4.start=0`, `cm.osc.guard.clear=100h`; two commands; "Stage: active" |
| e: build 3, Structural 1 | `_OSC` hash `9BE29900`, form `B673537C 02FAE6BD`; `cm.osc.tier=1`, `cm.osc.read=0Dh`, `cm.osc.refused=110h`; no command; "Stage: failed" |
| f: build 4, Structural 1 | `_OSC` hash `2B75E85D`, the other four build 1's; `cm.osc.tier=3`; sent, `100h`, `000F0100h`, the router answering; "Stage: active" |
| g: build 4, Structural 0 (`Services\USB` set to 0 by `reg add`) | `cm.osc.tier=3`, `cm.osc.read=0Eh`, `cm.osc.refused=110h`; no command |
| the window (`SETUP.EXE`, after g) | Welcome (every device "Reinstall (same version)"), Components, the Thunderbolt and USB4 page, its Settings..., Advanced...: the row "XhciCmAsm4242Structural ... Current: 0 (all controllers)" beside "XhciCmAsm4242Reads ... Current: 1 (all controllers)", selectable, with its note; the 98 SE and ME rows below it "(Windows 98 SE and ME only)"; cancelled (`n10\w9.png`, `w10.png`) |

So on 2000 to 10 (read on 10 x86) the value is written by the NT install
at 0, `/set` takes it, the Advanced list shows it, and the driver's read
path honours it as record 32 section 12.8.15.17's table says. Route B on
the x86 image, through the helper's routines, was read here for the
first time ("46.12 on NT, the x86 route and the read path" read B on
10 x64).

**Seen, not changed.** At tier 3 with the value 0 the report says only
"Stage: failed": no line names the refusal (the status record has the
tier; XHCITR prints a handover line only for bits 22 to 25). 98 SE and ME
have the installer's question for it; on NT the row is behind Advanced.
The owner was asked whether the report should say so: yes (2026-11-06),
a status bit for that refusal alone and a report line, task 47.29.

**Harness notes.** Windows 10's first elevated console after a start
often lost its keystrokes before the UAC dialog came (TCG); the
harness's `elev.sh` waits for the dialog by a pixel. A click into the
console window put it in QuickEdit's Select mode, which held the
installer's output until Esc.

Evidence: `E:\xhci98-vm\a347\cm\n10\` (`debugcon-c.log` to `-g.log`,
`qemu-*.out`, `serial-a.log` to `-g.log`: the console installs, `S.CMD`'s
values, filters, hashes and XHCITR reports; `mbstate-c.txt`; `w*.png` the
window), `t2\` (10 x86 with build 2's tables, no driver: the desktop),
the transfer folders `xme\` and `xt10\`, `pids.txt`.

### Not run

The x64 image (record 32's table names 10 x64; the read path is the same
code, and "46.12 on NT" read B and C on x64); 2000, XP, Vista and 7 for
the row; the board.

## 47.9 Stor98's set-up guard on 98 SE and ME with memory decode off (2026-11-06)

**The defect** (owner, 2026-11-05, from the review of the whole `3.4.1.0`
code): `HwFindAdapter`'s set-up check (Stor98 PLAN 3.1, "A controller
already set up") took a function whose memory decode was off without
reading CC or CSTS, so a controller a firmware left enabled with decode
off would have been taken and reset - 46.1's hang.

**The design**, converged before the code (three rounds; the scratch file
`E:\xhci98-scratch\w47\stor\design-479.txt`, revision 3): decode is no
exemption. With decode on nothing is written, as before. With decode off,
BAR0 is reached only when it is a memory BAR at a nonzero address below
4 GiB (32-bit, or 64-bit with its upper dword 0) at the start of the
port's access range (the devnode's own memory resource, at least a page);
its page mapped; then only the command register is written: decode on
with every other bit as found, read back on before CC and CSTS are read,
and after every such write the word as found written back and read back;
a word that does not read back is a refusal. Taken only with CC.EN 0 and
CSTS.RDY 0. Weighed and refused: a missing, high or I/O BAR0 (the take
path fails those anyway, after writing them); all ones with decode turned
on (as with decode on since `3.4.1.0`; a path that starts forwarding only
between the find and `HwInitialize` would have run before - accepted, no
retry); a port range naming another BAR with decode off (BAR0's allocation
unproven; residual: Code 10 where it was taken). `DevExt`'s bus and slot
set before the config helpers are used. The sequence is one pure routine
in `pciscan.h` held by `pciscan_ut` with fake operations. The code:
`a9ffdfc2` (reviewed and revised in three rounds before the commit).

**Read first, with the `3.4.1.0` tip's kit** (`3e7a1444`'s x86 test cut,
the one `runs/run-46.md`'s delta matrix installed; a fresh overlay of the q35 98 SE base, the system disk on a
QEMU `nvme` at `00:04` that SeaBIOS boots, a second internal `nvme` at
`00:05`, the ASM4242 model with the ORICO at power-on; `XHCIINST.EXE /dir
98-me /yes /nopause /thunderbolt /stor98`, exit 3; cold start): the
tunnelled drive's `StorFind` at power-on "cmd 0107 cc 00000000 csts
00000000 taken", and after the enclosure was unplugged and plugged in
again on the desktop "cmd 0006 ... taken": on 98 SE the hot-plugged
drive reaches its find with memory decode on, so the decode-off branch is
not on the hot-plug path there.

### The legs (2026-11-06)

Development host A, QEMU TCG; the q35 bases' pinned emulator
(`asm4242-20261025-2230`, `qemu-system-x86_64.exe` SHA-256
`da60139f...0c93`) copied into the leg's folder; `launch-q35.ps1` with the
two switches of 46.1's legs (`-SysNvme`: the system disk on a QEMU `nvme`
at `00:04.0` that SeaBIOS boots; `-Nvme2`: a second internal `nvme` at
`00:05.0`; `-Nvme2Boot`: SeaBIOS sets that one up too), `-Usb4 asm4242
-EncAtBoot` (the ORICO model tunnelled at power-on) and `-device
usb4-debug-port`. QMP on a unix socket in the leg's folder (this worker's
TCP port range lies inside the host's excluded port range). Fresh
overlays of the q35 98 SE and ME bases, their bridge wizards answered.

The package: the gated `release` builds of `a9ffdfc2` ("BUILD + GATES
PASSED (release)", x86 and amd64; `stor98.mpd` SHA-256 `37b3eb48...bf00`),
cut as an x86 TEST upload set (`make-release.ps1 -Arch x86 -TestLabel`,
`xhci98-3.4.1.0.zip` SHA-256 `39048e72...6125`; the tree's version stamp
is still `3.4.1.0`), installed with `XHCIINST.EXE /dir 98-me /yes /nopause
/thunderbolt /stor98` (exit 3, both), then a shut-down and cold start;
Stor98 bound by hand by `XHCIDI` jobs (as a user accepting Windows' offer)
to the internal functions. The test image: the same source with
`STOR98_TEST_FWDECODEOFF` (9x debug-port build, SHA-256 `aee33a23...347a`),
copied over `STOR98.MPD`, then a shut-down and cold start.

98 SE: `00:04` the system disk SeaBIOS serves (C:, compatibility mode),
`00:05` not set up by SeaBIOS. ME: its IDE system disk, `00:05` served by
SeaBIOS (`-Nvme2Boot`), as in 46.1 (ME does not start from an NVMe disk
only INT 13h serves).

| Step | 98 SE | ME |
|---|---|---|
| Cold start, release image, only the tunnelled drive on Stor98 | `StorFind` "... found 5.0.0; cmd 0107 cc 00000000 csts 00000000 taken; result 1"; E: copied and compared (`fc /b` clean) | the same `StorFind`; F: copied and compared clean |
| The enclosure unplugged on the desktop and plugged in again | `StorFind` "... cmd 0006 cc 00000000 csts 00000000 taken; result 1"; copied and compared clean | the same (`cmd 0006 ... taken`); clean |
| Stor98 bound to the internal functions (98 SE `00:04` and `00:05`, ME `00:05`), cold start | the desktop, C: still the BIOS's; no `StorInterrupt 0.4.0` (left), `StorInterrupt 0.5.0` "... interrupt" (taken) and `5.0.0`; E: and F: copied and compared clean | no `StorInterrupt 0.5.0` (left), `5.0.0` taken; F: clean |
| The test image (decode turned off before the check), cold start | `00:04`: stages `0B 01000105` (injected: `0107` read back as `0105`), `0B 02000105` (the check left the word as it found it), `0B 04000107` (put back), `0A 03010101` (CC.EN 1, CSTS.RDY 1, read with decode turned on: left), `03 00000000`; `00:05` and `5.0.0`: `0A 03000000` (read, decode turned on, CC and CSTS 0: taken), result 1; the desktop from C: (the BIOS's drive kept working after the check), E: and F: clean | `00:05`: the same as 98 SE's `00:04` (`0A 03010101`, put back `0107`, NOT_FOUND); `5.0.0` taken; F: clean |
| The test image, the enclosure unplugged and plugged in again | `0B 01000004`, `0B 02000004`, `0B 04000006`, `0A 03000000`, result 1: the hot-plugged drive read with decode turned on and taken; E: clean | - |

So: a drive the BIOS set up is left whether the check finds its decode on
(the release image) or off (the test image), and with decode off the check
puts the command register back as it found it; the tunnelled drive is
taken at power-on and after a hot plug; a drive no firmware set up is
taken. On both systems the hot-plugged drive reaches its find with decode
on (`cmd 0006`).

Found on the way, not this change: on ME the BIOS-served `00:05` drive's
D: answers "General failure reading drive D" once Stor98 is bound to it
(and left), with this image and with `3.4.1.0`'s `stor98.mpd` alike
(SHA-256 `579b78e9...2116`); it fits 46.1's reading that ME does not run
a disk only INT 13h serves.

Evidence: `E:\xhci98-vm\a347\stor\` (`s98r\`, the reading with the
`3.4.1.0` tip's kit; `s98a\` and `mea\`: `serial-*.log`, `debugcon-*.log`,
`cmdline-*.txt`, the shots; `pids.txt`), the kits `xa\`, `xm\`, `xr\`.
## 47.17 NVPERF.1's emulator-decided changes (2026-11-06)

The task: those of design record 30 section 13.6's changes C1 to C5 that
task 45.10's emulator readings decide (`runs/run-45.md`, "45.10"); the
changes the bench decides (45-E.9) stay in "Planned work". Read against
45.10's readings and record 30 sections 13.4 to 13.7; no emulator run and
no code.

| Change | What 45.10 read | Decided by | Stays |
|---|---|---|---|
| C1, a child size setting on 9x | the emulator's MDTS (512 KiB) already splits every request above it; 3 to 4 commands at the drive at 2 MiB, one thread | the XG6's MDTS and the depth histogram (45-E.9's B4): the emulator's MDTS is not the drive's | planned |
| C2, one `ScsiPortGetPhysicalAddress` call per run | the aligned-against-scattered difference mixes IOS's cut of a scattered buffer (pieces below 63 KiB, one command at a time) with Stor98's per-page mapping; neither isolated nor excluded | a reading 45.10 did not take: a Stor98 counter of the mapping's time (the emulator can show relative software cost, record 30 section 13.4), or B4's refit of F and B | planned |
| C3, more `ENG_PRP_PAGES` | depth never reached 8 (at most 3 to 4 at 2 MiB) | B4's depth histogram at 8 with the drive's MDTS | planned |
| C4, a 9x maximum above 2 MiB | above 2 MiB IOS cuts requests into 63 KiB double-buffered pieces (80 to 108 MB/s against 202 to 241 at 2 MiB); whether a larger maximum lets them through whole is not shown, every run having the 2048 KiB maximum | B4's 4 and 8 MiB rows (record 30 section 13.5) | planned |
| C5, interrupt coalescing | nothing | B4's F after C1 and C2 | planned |

Two and four threads (record 30 section 13.3 item 2) showed no lever in
the emulator; that is no change of C1 to C5.

So none of C1 to C5 is decided by 45.10's readings, and nothing changes in
`src\`. C2 is the only one an emulator reading could decide; it needs a
vehicle 45.10 did not have. Read for it on the way: `engine.c`'s note at
`EngSpanOk` says the 98 SE port returns at most the length to the end of
the page (engine runs, 2026-10-15), while record 30 section 4.2 (`S98VSD.VXD`
builds the runs and `SCSIPORT.PDR` walks its list by length) records runs
of up to 2 MiB in its BG arm: which of the two `EngMapCaller` sees with
`S98VSD.VXD` loaded decides whether C2 can save any call at all, and is the
first thing such a reading would record.

## 47.24 XHCIQUAL --edd: each disk unit's EDD 3.0 device path (2026-11-06)

The owner's decision of 2026-11-06 for design record 37's Q1: an XHCIQUAL
feature first. `XHCIQUAL --edd` asks the firmware, from MS-DOS before
Windows, through INT 13h function 41h (BX=55AAh) and function 48h (a
cleared 4Ah buffer, then 42h), for each disk unit 80h to 87h, and stops
before any controller is touched; per unit it says, in Q1's terms, whether
the unit is named by a device path with key BEDDh, host bus "PCI ",
interface "USB" and the bus, device and function of an xHCI (class
0C0330). Off by default: no arguments is still the quick scan.

### The change

`650b7622` (code and tests), `b7c7e351` (documents), reviewed and
revised (the design over two rounds, the code over three, the last with
no material findings).

- `xhciqual\edd.c`, pure: `edd_run` is the whole mode, every machine
  access an injected operation (`EDD_OPS`). The environment first: INT
  2Fh 1600h and INT 21h 3306h, refused with exit 2 under Windows (an
  MS-DOS Prompt), under Windows NT's MS-DOS (true version 5.50) and when
  DPMI cannot make either call; DPMI 0400h's virtual-8086 mode a warning.
  Then per unit 41h and, when it answers (carry clear, BX=AA55h), 48h
  twice, each buffer prepared afresh (cleared, size word set, guard bytes
  to 200h); the unit and function printed and flushed before each call.
  Then the PCI reads, with the buffer freed: the xHCI scan and the
  vendor, device and class at each address a path names; `Probe safety`
  counts the writes. The decode: a 48h answer only with CF clear and AH
  00h; a path only on key BEDDh, a length of 2Ch (checksum at 49h over
  1Eh-49h) or 24h (at 41h over 1Eh-41h) that fits the buffer given, and a
  checksum that holds; the size word printed, never deciding; both trials
  judged and the better one taken, two valid ones that disagree said so.
  The device path's bytes (38h-47h, a USB serial number) are never
  printed: zero or nonzero (withheld) per half, `00` or `xx` in the dump.
  Field and bit names from T13/1386D revision 5 section 6.8, Phoenix EDD
  3.0, and Linux `include/uapi/linux/edd.h` at `c6859eed` (GPL-2.0 WITH
  Linux-syscall-note; facts only).
- `xhciqual\sysprobe.c`, `edd_dos_ops`: DPMI 0300h through a new
  `rm_int_err` (a host failure told apart from the BIOS's CF/AH), with a
  4 KiB real-mode stack of its own (SS = the block, SP = 1000h) and the
  buffer at DS:SI = block + 100h : 0 in one DPMI 0100h block, freed with
  0101h; FLAGS 0202h in the call structure.
- `xhciqual\main.c`: `--edd`, taken before the quick scan and the full run;
  beside any family selector or other mode a usage error (exit 2), with
  `--help` winning; with it only `--log`, `--log-stamp`, `--serial`,
  `--no-page`, `--no-sysinfo` and `--done-flag`. Exit 1 when done.
- Host suite `xhciqual\test\test_edd.c` (232 checks): the order through
  fakes (a refusal or no buffer makes no INT 13h and no PCI access; every
  INT 13h with the buffer allocated and prepared afresh, right after its
  own progress text was flushed; the buffer freed once, before any PCI
  read; no 48h after a failed 41h), the refusals, SeaBIOS's 1Ah answer, a
  final-layout USB path on 00:14.0 with a good and a bad checksum, the
  earlier 42h layout, a firmware writing past the 42h buffer, failed calls
  holding a valid-looking path, a keyed but invalid 4Ah answer beside a
  valid 42h one, disagreement, size words 1Eh and FFFFh, a 2Ch path in a
  42h buffer, other interfaces, classes, host buses and addresses, a
  planted serial number never printed, 79 columns, and the decode on a
  buffer that ends on a guard page.
- The QEMU matrix: case `edd_seabios` and `-PortBase` (55 cases).
- Documents: `xhciqual\README.md` ("EDD device paths (`--edd`)"),
  `xhciqual\hardware-testing.md` ("ThinkPad P14s Gen 1 - does the firmware
  name its USB stick?"), design record 01's file list, design record 37's
  Q1 pointed at `--edd` (the `DEBUG.EXE` scripts the fallback), the
  qualifier readme's option table and exit codes in `make-release.ps1`.

### Builds and tests

`xhciqual\build.cmd` at `b7c7e351` (development host A, Open Watcom):
the BAT line-ending check, the seven host suites passed (`test_edd` 232
checks, the other six as before), `XHCIQUAL.EXE` 177 822 bytes, SHA-256
`6c28ada6933dc3d093797975ad8f3caa9df94d5b2f2d26409c1862c2947412ae`
(`XHCIQUAL.MAP` `e8f925d48c5454aa4fb99b4183e8cc24bfa67faf16dd5825c3b047d9db1944cb`);
`test\check-build-exit-codes.ps1` 6 cases passed; `scripts\check-text.ps1`
OK. The QEMU matrix (`run-qemu-matrix.ps1 -PortBase 51003`, four batches,
QEMU's own SeaBIOS rel-1.17.0, `-machine pc`): 55 passed, 0 failed.

### The legs [emu]

Development host A; a fresh overlay of the q35 98 SE base each
(`E:\xhci98-vm\q35-bases\w98se`, `launch-q35.ps1`, the pinned emulator
`asm4242-20261025-2230`, QEMU 11.1.2, SeaBIOS rel-1.17.0, one vCPU, QMP
TCP 51000); the boot worker's stick image (256 MB, FAT16) as a fresh
qcow2 overlay on `qemu-xhci` port 3 (SuperSpeed) at power-on where named;
the tool from a read-only transfer disk (D:), copied to C:.

| Leg | What | Result |
|---|---|---|
| E2 a | no stick; the desktop; an MS-DOS Prompt: `D:\XHCIQUAL --edd --log C:\EDDBOX.LOG` | refused before any disk was asked: `XHCIQUAL --edd refused: Windows is running, and in its MS-DOS Prompt` / `INT 13h is Windows' own. Shut down, start the computer again, choose` / `"Command prompt only" in the Startup Menu and run it there.` / `Nothing was asked of the disks (exit 2).`; `IF ERRORLEVEL 2` true, 3 false; the log holds the same lines. `BootMenu=1` written to `MSDOS.SYS` there; ACPI power-off |
| E2 b | the stick at power-on, cold; Startup Menu 5, "Command prompt only"; `XHCIQUAL --edd --log EDD.LOG` (paged) | 80h (AHCI C:), 81h (the transfer disk), 82h (the stick): 41h AH=30h BX=AA55h CX=0007h; 48h, both buffers, CF=0 AH=00h size word 001Ah, guard unchanged, key 0000h: `unit 8xh: no device path (size word 001Ah, no key BEDDh): not named`; 83h to 87h: 41h CF=1 AH=01h BX=55AAh, `absent or unsupported, 48h not asked`; `PCI: xHCI 01:00.0 1B36:000D` after the calls; `Probe safety: PASS`; exit 1 (`IF ERRORLEVEL 1` true, 2 false); the 82h dump as `DEBUG.EXE` read it in 47.12's E2 (flags 0000h, cylinders 3FFFh, heads 0, 80000h sectors) |
| E2 b | `XHCIQUAL --edd xhci --no-page` | the usage text, exit 2 |
| E2 b | a run without `--edd`, the new build beside the base build (`d9aa8472`, `XQBASE.EXE`): `XHCIQUAL` (no arguments, redirected) and `--probe-only --no-page --log` | the quick scan: `FC: no differences encountered`; the probe logs differ in the build time and the log's own name only |
| matrix | `edd_seabios`: `-machine pc`, C: on the PIIX IDE, the stick on `qemu-xhci` at power-on | 80h: a full EDD 3.0 path in both layouts, `ATA` on PCI `00:01.1` (8086:7010, 010180), checksums PASS, while the size word says 001Eh: `unit 80h: not USB: interface "ATA", host bus "PCI " 00:01.1`; 81h (the stick): 1Ah, no path; 82h to 87h absent |

So SeaBIOS rel-1.17.0 names no USB disk by a path, in either layout, and
the mode says so cleanly; it does name an IDE disk on the PIIX (not one on
the q35 board's AHCI), with a size word of 1Eh beside a complete path -
the case record 37 section 4.1 gives for never deciding on the size word.
The P14s's answer is the owner's reading (below).

### For the owner: the P14s

1. Take `XHCIQUAL.EXE` from the build: `xhciqual\xhciqual.exe` after
   `xhciqual\build.cmd` (this run's, SHA-256 `6c28ada6...`, is
   `E:\xhci98-scratch\w47\edd\out\xhciqual.exe`). Copy it to `C:\` of the
   P14s (the internal disk, not the stick).
2. Shut down. Plug the stick in where it was for the report.
3. Power on; as Windows 98 starts hold Ctrl (or press F8) and choose 5,
   "Command prompt only" (not "Restart in MS-DOS mode"); no EMM386.
4. `XHCIQUAL --edd --log EDD.LOG`, a key at each `-- More --`. The answer
   is the last block, `EDD device path, per unit:`; a stop leaves the unit
   and function on the screen (a photo).
5. Optional control, no stick: `XHCIQUAL --edd --log EDD0.LOG`.
6. Copy `C:\EDD.LOG` (and `EDD0.LOG`) to `temp\p14sg1-boot\`.

Evidence: `E:\xhci98-vm\a347\edd\E2` (`box1.png`, `box2.png`, `e1.png` to
`e3.png`, `u1.png` to `u4.png`, `files\` with `EDD.LOG`, `EDDBOX.LOG`,
`PROBE.LOG`, `PROBEB.LOG`, `Q1.TXT`, `USAGE.TXT` and the guest's
`XHCIQUAL.EXE`, `pids.txt`), `E1` (the smoke run before the review);
`E:\xhci98-scratch\w47\edd\` (`build-final.log`, `exitcodes.log`,
`matrix-full.log`, `matrix-edd_seabios-serial.log`, `design-edd.md`).

## 47.5 Eight records on an isochronous pipe (was ISOREC.1)

**The change** (`bfddd4d4`, branch `w47-hcd`, reviewed and revised; design
record 13 section 11.9, with an amendment note at the head of 11.8): an
isochronous pipe carries `HCD_ISO_PIPE_XFERS` (8) transfer records where
every pipe carried four, allocated at `HCD_PIPE_BYTES(8)` with its request
blocks for `XferCount` records; the event budget counts each isochronous
pipe at its ring's capacity plus nine (`XHCI_ISO_EVENTS_OVER_TRBS`, held
to `HCD_ISO_PIPE_XFERS + 1` at compile time), so a page's allowance with
the 256-entry event ring is 112 TRBs, where it was 120. Every other pipe,
the embedded default pipe and a stream's pipe keep their size. A pipe's
most records out at once is noted when its budget count is returned
(`iso.recs`, and a `qemu` trace line). `target-standing.md`'s isochronous
row follows (112, 7.0 to 9.3 ms for a camera endpoint, 14 ms for small
packets).

**What the sizing says** (section 11.9, point 3): the records bind on the
pool ring (Full Speed, High Speed `bInterval` 4) for requests shorter than
15 ms - eight records hold 8 ms of 1 ms requests, 32 ms of `uac2`'s
default eight 4 ms requests; on a page (High Speed `bInterval` 1 to 3) the
allowance binds from 4 ms requests up: 46.30's case, the CX31993's 4 ms
URBs at `bInterval` 1, is three URBs on the ring with four records or
eight (112 or 120 TDs), and what lifts it is a larger event ring
(ISORING.4, still in "Planned work").

**Builds.** The gated build of `bfddd4d4`: x86 `all` passed (53:30, the
host's packager self-tests alone 27:48; `E:\xhci98-scratch\w47\hcd\gb-x86.log`,
`out\gated-x86-release.txt`: `xhci98.sys` `AD596229...9838`; `qemu`
`6862EC49...35C1`), amd64 `all` passed (`gb-amd64.log`; release
`xhci98.sys` `D75D1BFA...C8E7`, `qemu` `4ACBFD7E...ECE`). Host suites pass
(`test_pipe` 14,348 checks, the grant rows rewritten; `ht-r4.log`).

**The leg** (development host A, the pinned emulator
`asm4242-20261025-2230`, the q35 XP SP3 base, two vCPUs, TCG; the release
package of `bfddd4d4` installed by `INSTALL.BAT /console /yes /nopause`,
exit 3, then its `xhci98.sys` renamed and the `qemu` flavour copied in, a
diagnostic swap, since the installer refuses a folder whose drivers carry
another build's marker; QEMU's `usb-audio` at root port 2, a `usb-storage`
stick at 3; `E:\xhci98-vm\a347\hcd\legs\xp-iso-*-debugcon.log`):

| Ring | What was read |
|---|---|
| pool (`XhciQemuIsoRing` absent) | `iso budget counted` 71 (62 + 9) while the stream was open, 0 after; the logon sound and a `sndrec32` play: `hcd: iso pipe records, slot/DCI/peak/count` `02020308` and `02020408` - `usbaudio.sys` had at most 3 and 4 of the 8 records out; packet errors and Missed Service 0 |
| page (`XhciQemuIsoRing` 1, machine-wide under `Services\USB`) | `iso pages built` 1, `iso budget counted` 121 (allowance 112 + 9: the new grant), the largest request 10 packets in 10 TRBs, the peak 3 of 8, packet errors and Missed Service 0 |

So XP's `usbaudio.sys` (UAC 1, Full Speed) keeps three or four 10 ms
requests in flight and the change is invisible to it; the stick and
mouse traffic on the same controller ran as before. A second play's
stream (`mplay32`, `sndrec32` again) closed after a few Ring Underruns
with no packets answered, a player-side end in TCG, not counted here.
**Not read**: a client that queues more than four requests (`uac2`): the
emulator here has no `usb-redir` for `usb-wdm-free`'s UAC 2.0 simulator,
and the fork build 46.30 used was not found on this host, so the
High Speed reading stays owed, as do Windows 2000 and 98 SE.

## 47.10 A departed PDO's start on 98 SE and ME (DEPART.1's 9x part)

**The change** (`bfddd4d4`, reviewed and revised; design record 13
section 10.5.2): on 98 SE and ME a START that `hcdPdoStarted` refuses
because the device has left is answered success for a device or function
PDO with a controller, no REMOVE since it was last listed or started and
no selective-suspend quarantine (`hcdPdoGhostStart`, re-deciding under
`PdoListLock`, so a departure after `hcdPdoStarted`'s read is taken too;
URB admission reopened). The ghost PDO answers the device and
configuration descriptors at index 0 and SELECT_CONFIGURATION from its
copies (`hcdCopyAnswer`, `hcdGhostSelect` with the pure walk
`XhciPipeSelectWalk`), holds bulk, interrupt and isochronous transfers on
the PDO, and refuses everything else but ABORT_PIPE at the next tick,
ahead of the contained controller's hold. NT keeps the refusal. Pure
halves `XhciFuncGhostStart`, `XhciFuncGhostDescriptor`,
`XhciFuncGhostCopyLength`; host vectors in `test_func` (969 checks) and
`test_pipe`.

**The legs** (the same emulator; the q35 ME and 98 SE bases, one vCPU,
TCG; the release package of `bfddd4d4` installed with the console tool,
exit 3, the `qemu` `xhci98.sys` copied over it - on both the first
restart put the release file back, so it was copied again and the guest
cold-started; a `usb-hub` hot-plugged at root port 4; task 43.49's series
as 43.54 timed it: a `usb-mouse` hot-plugged alternately at root port 3
and hub port 4.1, pulled 1.0 s (ME's second series 0.8 s) after its plug,
the next plugged as soon as QEMU had dropped it;
`E:\xhci98-vm\a347\hcd\scripts\churn.py`, logs and traces in `legs\`):

| Guest | Cycles | Ghost STARTs | What each ghost read | End |
|---|---|---|---|---|
| 98 SE | 60 (HID not yet installed: the Add New Hardware wizard held the configuration manager), 80, 100 | 2 (one in boot c, one in boot d) | START answered; device descriptor; configuration descriptor (9 bytes, then all); SELECT_CONFIGURATION answered; SET_IDLE (`21 0A`) and the report descriptor (`81 06 22`) refused; ABORT_PIPE; STOP | after 100 cycles a mouse plugged and kept started at once; no controller reset in any series |
| ME | 60, 100 | 2 | the same, the report descriptor tried three times; STOP; then the relations without it and REMOVE (`minor 2`) reaching the PDO, so `hidusb.sys` passed it down | 2 ghosts then 92 more cycles, no controller reset; at cycle 100 of the second series a stop of 43.54's shape (below) |

**Read, against design record 13 section 10.5.2 point 4's hypothesis**:
on both systems `hidusb.sys`'s start sends the device descriptor, the
configuration descriptor twice and the select before anything else, all
answered from the copies; `hidclass.sys` then fails the stack's start at the report
descriptor; the configuration manager sends STOP (98 SE too, for a start
failed above the PDO, where 43.49 read REMOVE after this driver's own
refusal); on ME the REMOVE follows and is passed down to the PDO, which
43.49's stop never reached. Before the change ME stopped in 4 to 17
cycles of 43.49's series and at cycle 13 of 43.54's (its gated build; a
build with extra trace lines ran 60 without one); here two
ghost STARTs went through and ME ran 160 cycles. **The stop at cycle 160
was not a ghost**: the last mouse was started while present
(SELECT_CONFIGURATION configured), pulled during its class driver's start
- design record 13 section 10.5.2 point 6, run d2's case, which this task
leaves - and no STOP or REMOVE reached its PDO afterwards; a mouse plugged
then was queried and never started. The release notes' limitation stays
for that case. **Not read**: the stick and the split (`usb-audio`) pulls,
the 98 SE disable and enable with a device pulled while disabled, and the
Windows 2000 regression series (the NT path is unchanged by the
`IoIsWdmVersionAvailable(1, 0x10)` gate; host vectors cover the rule).

## 47.15 USB keyboard in 2000 and XP text-mode Setup on an xHCI-only machine (SETUPKBD.1's reading)

### What text mode loads for USB input (static)

Each medium's `I386\TXTSETUP.SIF` was extracted with 7-Zip into the
worker's scratch (never tracked) and read as text: `win2ksp4.ISO`'s
(335,081 bytes, SHA-256 `ECA9CDFB...6BF2`) and the XP SP3 VL CD's
(480,367 bytes, `5F754EA5...63D8`), the files design record 13 section
5.6 read.

| Section | Windows 2000 SP4 | Windows XP SP3 |
|---|---|---|
| `[Keyboard]` | `STANDARD` (i8042prt), `kbdhid` and `USB` ("USB Keyboard", both `files.kbdhid`, service `kbdhid`) | the same |
| `[Keyboard.Load]` | `STANDARD = i8042prt.sys`, `kbdhid = kbdhid.sys` | the same |
| `[SpecialFiles]` | `keyboardclass = kbdclass.sys,4`, `mouseclass = mouclass.sys,4` | the same |
| `[InputDevicesSupport.Load]` | `openhci`, `uhcd`, `usbhub`, `hidusb`, `serial`, `serenum`, `usbstor` | `usbehci`, `usbohci`, `usbuhci`, `usbhub`, `usbccgp`, `hidusb`, `serial`, `serenum`, `usbstor` |
| what those pull in | `hidclass.sys`, `hidparse.sys`, `usbd.sys` only in `[files.openhci]` and `[files.uhcd]` | the same three and `usbport.sys` in each of `[files.usbohci]`, `[files.usbuhci]`, `[files.usbehci]` |
| `[HardwareIdsDatabase]`, host side | `PCI\CC_0C0310` openhci, `PCI\CC_0C0300` uhcd, `USB\ROOT_HUB` and `USB\CLASS_09` usbhub, `USB\COMPOSITE` usbhub | adds `PCI\CC_0C0320` usbehci and `USB\ROOT_HUB20`; `USB\COMPOSITE` usbccgp |
| `[HardwareIdsDatabase]`, input side | `USB\Class_03` (and `&SubClass_01`, `&Prot_02`) hidusb; `HID_DEVICE_SYSTEM_KEYBOARD` kbdhid (keyboard class GUID); `HID_DEVICE_SYSTEM_MOUSE` mouhid | the same |
| an xHCI controller | no row binds `PCI\CC_0C0330` | none either |

So the USB keyboard path text mode builds is `hidusb` over a USB device
PDO, `kbdhid` over the HID collection and `kbdclass` above it, and it
reaches only the host controllers whose class codes the table names. On a
machine whose only USB controller is xHCI, text mode by itself has no USB
keyboard: a key reaches it only through the PS/2 port, real or emulated
by the firmware (`i8042prt` is always loaded).

### The controls: no F6 driver (runtime)

Development host A, the pinned emulator `asm4242-20261025-2230` (QEMU
11.1.2, SeaBIOS rel-1.17.0), TCG, one vCPU, 512 MB, headless; a blank 4 GB
disk and the CD on IDE, `qemu-xhci` with a `usb-kbd` and nothing else on
USB, the PS/2 controller present (NTDETECT needs it, `lessons.md`); QEMU
traced `usb_xhci_xfer_success`, `ps2_keyboard_event` and
`hid_kbd_queue_empty` (`E:\xhci98-vm\a347\hcd\tm\`,
`scripts\tm.ps1`).

| Leg | Machine | Result |
|---|---|---|
| xp-c1 | q35 | Welcome to Setup; ENTER sent: no PS/2 event at any time, no transfer on the keyboard's slot after the loader, the page unchanged |
| w2k-c2 | i440FX | the same |
| w2k-c0, c1 | q35 | not read: 2000's text mode stopped at "Setup is starting Windows 2000" (the processor at `8041c964` with interrupts off, unchanged for 20 minutes, twice; the ACPI HAL it picks on q35), so the 2000 legs ran on i440FX as 33.3's and 38.final's did |

QEMU hands every key to the USB keyboard here (SeaBIOS had used it at the
loader's prompts), and SeaBIOS serves a USB keyboard through INT 16h only
and emulates no PS/2 port. A real BIOS whose legacy USB support traps
ports 60h and 64h keeps a USB keyboard working in text mode through
`i8042prt` for as long as no driver takes the controller: then F6 is not
needed for the keyboard at all, and loading `xhci98.sys` is what ends that
emulation (its USBLEGSUP hand-off), so from then on the keyboard needs the
path below.

### With the F6 disk (runtime)

The F6 floppy held this build's `nt-x86\` `txtsetup.oem`, `xhci98.sys`
(release, `AD596229...9838`), `xhci98.inf` and `xhcihlpr.sys`; the
`usb-kbd` behind a `usb-hub` at root port 4.

| Leg | Machine | Result |
|---|---|---|
| xp-f4 | q35 | F6 through SeaBIOS's USB keyboard taken at once ("Press F6 ..."), the specify screen, S, ENTER at the disk, both options listed ("xHCI98 USB 3 Controller (32-bit Windows 2000/XP)" and "... (32-bit Windows 2000, files from CD)"), the first taken, ENTER; Welcome; ENTER moved to the licence, F8 to the partition list - every key after the kernel's start a keyboard interrupt-IN completion (`len 8`) and none a PS/2 event (0 in the whole leg) |
| w2k-f4 | i440FX | the same to the partition list, 0 PS/2 events, the keys after Welcome interrupt-IN completions |

Harness notes, each costing a run: a script that pressed F6 and polled the
first 2,000 bytes of the text buffer in one loop never got the specify
screen (xp-f1, w2k-f1, f2), where the same presses with a slower poll got
it at once (f3, f4) - the loop, not the keyboard path, was at fault; a
`usb-kbd` hot-plugged while the loader waited at the specify screen left
the loader deaf to PS/2 keys (w2k-f3, SeaBIOS's state, not read further);
QMP `input-send-event` with a `device` stopped this QEMU ("Property
'qemu-fixed-text-console.device' not found").

### Verdict, and what Intel 7-series and later offer instead

- **F6 brings `xhci98.sys` early enough.** The loader loads it with its
  own boot drivers before the kernel starts; text mode then binds Setup's
  own `hidusb`, `kbdhid` and `kbdclass` above this driver's PDOs, read
  again here on 2000 and XP, at a root port before (33.3, `lessons.md`)
  and behind a hub now, the bus's first BusRelations waiting for the
  devices present at start (design record 13 section 5.7). The limits of
  section 5.6 stand: the F6 press itself goes through the firmware's
  keyboard support; on 2000 the first GUI-mode boot needs the
  `XHCI98W2K` option (limit 7); XP's GUI mode has USB input only after
  "Installing Devices".
- **Intel 7-, 8- and 9-series and C610/X99** (design record 16): the
  switchable connectors are on EHCI until something writes `XUSB2PR`; a
  BIOS in its "Auto" or "Smart Auto" xHCI mode leaves them there at boot,
  and `XhciIntelPortSwitch` is off when absent, as in text mode after F6
  (record 16 section 5), so `xhci98.sys` loaded by F6 does not move them.
  XP's text mode drives EHCI by itself (`usbehci` in its table); 2000's
  has no EHCI driver and these chipsets no companion controllers, so on
  2000 the keyboard there is the BIOS's PS/2 emulation, or F6 with the
  BIOS set to route the ports to xHCI ("Enabled").
- **From 100-series on** (no EHCI): the BIOS's legacy USB (PS/2
  emulation, until a driver takes the controller) or F6 with `xhci98.sys`.
- **Code: none proposed.** The reading shows no gap a driver change
  closes in text mode; a design would be needed only for what is not read
  here: a real BIOS's PS/2 emulation across `xhci98.sys`'s hand-off (the
  2026-10-13 report that a USB keyboard died in 2000's text mode with F6
  was not reproduced, `lessons.md`; its leads - the firmware's emulation
  against the hand-off, a hub or KVM in the path - need the reporter's
  machine).

## 47.16 2000 and XP started from a USB disk behind xHCI (BOOTUSB.1's reading)

### What each layer asks (static)

- **Loading.** `NTLDR` and `NTDETECT.COM` read the boot disk, the kernel,
  the HAL, the `SYSTEM` hive and every boot-start driver through the
  BIOS's INT 13h, so a firmware that boots the USB disk loads all of them;
  no Windows driver runs yet. `BOOT.INI`'s ARC path is resolved against
  the boot disk's MBR signature, so the same image boots from IDE or USB
  while it is the BIOS's first disk.
- **Which drivers come.** Only `Start` 0 services, in
  `ServiceGroupOrder`'s order, with their imports. An installed system has
  `xhci98` at 3 (`src\xhci98.inf`, `[Xhci.AddService]`, `StartType=3`,
  `LoadOrderGroup=Base`) and `USBSTOR` at 3; `xhcihlpr` alone is 0.
- **Finding the device.** A boot-start driver gets its device through
  `Control\CriticalDeviceDatabase` (`Service`, `ClassGUID`), keyed by
  hardware or compatible id: this driver's `PCI\CC_0C0330` (no stock
  row), `USB\XHCI98_ROOT_HUB`, `USB\XHCI98_HUB`, `USB\XHCI98_HUB30`, and
  the disk's `USB\Class_08&SubClass_06&Prot_50` (Bulk-Only; `&Prot_62`
  for UAS and `xhciuas`); USBSTOR's and the disk class's own ids are
  stock rows.
- **The hand-off.** `xhci98.sys`'s start takes the controller from the
  BIOS (USBLEGSUP, then halt and reset, `xhci_init.c`); NT uses no INT 13h
  after the loader, unlike 98 SE and ME (`runs/run-47.md`, "Boot with a
  USB stick attached").
- **Enumerated in time.** The root hub's and each hub's first BusRelations
  waits for the ports connected at start (record 13 section 5.7,
  `XhciFirstEnumWaitMs` 5000, `XhciFirstEnumPortMs` 2000 by default).

### The legs (runtime)

The same emulator, the q35 XP SP3 (two vCPUs) and 2000 SP4 (one) bases,
TCG; this build's release package installed by the console tool (XP: the
`qemu` `xhci98.sys` swapped in, for the trace), a `usb-storage` stick
plugged once so that `USBSTOR` installed, then `USBBOOT.REG`
(`E:\xhci98-vm\a347\hcd\xn\`) imported with `regedit /s`: `xhci98` and
`USBSTOR` at `Start` 0 in group `Boot Bus Extender`, and
`CriticalDeviceDatabase` rows for `pci#cc_0c0330`,
`usb#xhci98_root_hub`, `usb#xhci98_hub`, `usb#xhci98_hub30` (service
`xhci98`) and `usb#class_08&subclass_06&prot_50` (service `USBSTOR`),
class GUID `{36FC9E60-C465-11CF-8056-444553540000}`. Then the guest shut
down and started again with its system disk moved from the IDE channel
to a `usb-storage` at root port 1, `bootindex=0`
(`scripts\mkusb.py`), SeaBIOS booting it.

| Leg | Result |
|---|---|
| XP u1, the first USB boot | the desktop in about 90 s; the disk's PDO in the root hub's first answer (187 ms); then "Windows has finished installing new devices ... restart" (the disk as a new device instance) |
| XP, the second USB boot | STOP 0x7B (`0xF7C52524, 0xC0000034, 0, 0`); the trace: the root hub started, the disk's PDO in the first answer (218 ms) and never started |
| XP from IDE after it | `USBSTOR` `Start` 3 again (the INF install of the new disk instance rewrote it), `xhci98` still 0 |
| XP u2, the rows imported again | the desktop with no prompt; C: "Fixed Drive"; `USBSTOR` 0 |
| XP u2, the page file | none on the USB system volume: `wmic pagefile` named `E:\pagefile.sys` (463 MB, on the IDE VVFAT transfer disk), and Windows warned "Low Disk Space" on E:; the IDE control boot: `C:\pagefile.sys`, 768 MB |
| XP, hibernation | `powercfg /hibernate on`: "System does not support hibernation" on the USB boot and on the IDE control alike - this guest offers no hibernation at all, so the USB disk's own answer is not read here |
| 2000 u1 | the desktop (release build, no trace) with the safe-removal icon for the disk |
| 2000 u2 | STOP 0x7B (`0xF241F84C, 0xC0000034, 0, 0`): the same reset of `USBSTOR`; the rows' second import not read on 2000 |

### What the reading says, and what a design would hold

- **It works, with every piece outside this driver's install.** Boot-start
  `xhci98` and `USBSTOR` and the `CriticalDeviceDatabase` rows are enough
  for both systems to boot from a Bulk-Only disk behind `qemu-xhci`; the
  bus's first-answer wait delivered the disk in time on every boot
  (187 to 218 ms).
- **The first boot's device install undoes it.** The disk appears as a new
  device instance at its new place, the class installer runs
  `usbstor.inf`, and its `AddService` puts `USBSTOR` back to demand start:
  the next boot is a STOP 7B. Anything that offers this has to survive
  that (rows imported again after that install, or an `INF` of this
  project's own for the disk's compatible id - a design for the owner,
  not code).
- **The page file leaves the disk.** XP did not create its page file on
  the USB system volume and used another fixed disk; on a machine with
  only the USB disk it would run with none. Hibernation and crash dumps
  need a dump driver for the boot disk, which `usbstor.sys` and
  `xhciuas.sys` do not have (static); this emulator's guests offer no
  hibernation either way, so that stays unread.
- **Owed**: UAS (`xhciuas` boot-start, `usb#class_08&subclass_06&prot_62`),
  a disk behind a hub, real hardware's BIOS (its legacy USB boot and the
  hand-off), the 2000 re-import, and what the installer should do (none of
  it is the installer's or the INFs' today; `AGENTS.md`, limit 3 of record
  13 section 5.6: "installing Windows onto a USB disk is not supported").

## 47.20 Stor98: 4 KiB-sector namespaces as 512-byte sectors (2026-11-06)

Stor98 PLAN 1.7 and its rule that 4 KiB drives must work: a namespace
formatted with 1, 2 or 4 KiB blocks is shown to every target as 512-byte
sectors, the driver translating with read-modify-write for writes that do
not cover whole native blocks.

### The design

Stor98 PLAN, "4 KiB-sector namespaces", rewritten as the design before any
code and reviewed and revised in two rounds: the units (B = 2^LBADS,
S = LBADS - 9, the exposed count NSZE << S, refused when it does not fit
64 bits); a request's plan (head and tail edges, a shared edge, the
aligned middle, which is empty for a shared edge); unaligned reads (each
edge through a general bounce page of its own, only its caller bytes
copied out); unaligned writes in two phases (every edge pre-read into one
of two edge pages, both patched, then the write-backs and the middle in
buffer order; nothing written when a pre-read or a patch fails); the edge
pages (two pages added to the bounce block, 8 to 10, the DMA block 22 to
24 pages on NT and in the 9x pool; one read-modify-write at a time, the
pair taken in the step that submits the first pre-read, so an owner has
always started; a page given back only when no parent owns it and no
child uses it, a quarantined child keeping it); the overlap rules (an
unaligned write starts only when no earlier unfinished READ/WRITE holds
one of its edge blocks; any READ/WRITE starts only when no earlier
unfinished unaligned write has an edge block in its range); failure,
cancellation, gone, MD-1, reset and stop; flush, FUA (every write child,
write-backs included; never on a pre-read) and the durability statement
(AWUPF / NAWUPF); no TRIM or UNMAP; the answers (READ CAPACITY(10) and
(16) with LBPPBE and lowest aligned LBA 0, MODE SENSE, VERIFY, the ATA
IDENTIFY counts); 9x and NT. Windows 8, 8.1 and 10 see 512-byte sectors
too (the rule has no OS exception).

### The code

Commits `571524f7` (design, code, tests), `5c073062` (review fixes: a CR
byte in the unit build's new paths, the model's phase order, the
quarantine log's edge bits) and `19482469` (the pre-reads' order
asserted). `src/rmw.h` (new), `src/engine.c`, `src/engine.h`,
`src/lifecycle.c`, `src/nvme2k_cpl.c`, `src/nvme2k_scsi.c`,
`src/capdata.h`, `src/utils.c`, `src/nsfmt.h`, `src/nvme2k.h`,
`src/dbgport.h` (stages 0x190 `STG_RMW_PREREAD`, 0x191 `STG_RMW_PATCHED`;
`STG_ENG_CAP_WAIT` reasons 6 and 7). Reviewed and revised in three rounds,
the last with nothing material.

Host tests: `tests/unit/rmw_ut.c` (new: the plan for every start offset
modulo R and every length up to 3 x R + 1 at R = 1, 2, 4 and 8; a model
of a namespace in native blocks against a 512-byte reference disk, with
the data, the medium outside the request, sentinels, the media traffic
formula and the command order; the exposed count; RC16 for PLAN's
capacity table; the overlap rules), `capdata_ut.c` (LBPPBE). The Stor98
unit suite passes (`E:\xhci98-scratch\w47\rmw\unit4.txt`).

### Builds

The gated `release` builds at `19482469`, x86 and amd64, both "BUILD +
GATES PASSED (release)" (`E:\xhci98-scratch\w47\rmw\gated-x86-release.txt`,
`gated-amd64-release.txt`): `stor98.mpd` `4AB453B0...09FA3`, x86
`stor98.sys` `DDDAC437...C270`, amd64 `stor98.sys` `D468CC42...4719`. The
x86 package by `make-package.ps1`, laid out by `ConvertTo-FamilyLayout`
(the kit `E:\xhci98-vm\a347\rmw\xk\`, `xk-sums-final.txt`).

### The legs (2026-11-06)

Development host A, QEMU TCG: the q35 bases on the pinned emulator
(`asm4242-20261025-2230`), fresh overlays, the ASM4242 layout with the
ORICO model's drive tunnelled at power-on (`launch-q35.ps1 -Usb4 asm4242
-EncAtBoot`), its NVMe given 4096-byte logical and physical blocks
(`-global nvme.logical_block_size=4096 -global
nvme.physical_block_size=4096`), QEMU's `pci_nvme_read` and
`pci_nvme_write` traced. The drive's image is the bases' 128 MiB FAT16
test disk (`shared\nvme-fat.qcow2`), its partition at sector 63 and its
clusters 2 KiB, so nearly every file system write is unaligned at the
device. The package installed by its console tool (`XHCIINST.EXE /dir
98-me /yes /nopause /thunderbolt /stor98`; on NT `INSTALL.BAT /console
/yes /nopause /thunderbolt /stor98`), then cold starts. The data checks are
`RMWCHK.EXE`, a small Win32 tool of the kit (not tracked): `c` compares
every file of one folder with the same name in another; `w` writes 300
files of pseudo-random sizes (under a sector, sector multiples and one byte
past, one byte short of 4 KiB multiples, up to 256 KiB) in random write
sizes; `p` overwrites one pseudo-random range of up to 5000 bytes inside
each file in place; `v` reads every file back and compares it with the
expected bytes, the in-place ranges included. Each phase is one batch
(`T9.BAT` on 98 SE, `T4K.BAT` on NT), every result line to COM1.

| System | Phase | Result |
|---|---|---|
| 98 SE (`ab1d4d65...`) | the drive on the existing volume: 514 DLLs of `C:\WINDOWS\SYSTEM` copied, 454 of them compared (the first RMWCHK skipped files over 320 KiB), 300 files written, verified, patched, verified | all `bad=0`; ScanDisk "did not find any errors" |
| | cold start: verify, compare | `bad=0`, `bad=0` (514 files, 75.8 MB); ScanDisk clean |
| | `format E: /Q` (FAT16, 2 KiB clusters), the same set | format complete; `bad=0` throughout; ScanDisk clean |
| | cold start: verify, compare | `bad=0`; ScanDisk clean |
| 2000 (`7fc9714e...`) | FAT: `format /FS:FAT /Q`, 135 drivers copied and compared, write, verify, patch, verify, chkdsk | `bad=0` throughout; "found no problem" |
| | cold start: FAT verify, compare, chkdsk; then `format /FS:NTFS /Q` and the same set | `bad=0`; no problem; NTFS `bad=0`, no problem |
| | cold start: NTFS verify, compare, chkdsk | `bad=0`; no problem |
| XP (`64ada103...`) | FAT: format, 746 DLLs (133 MB, the volume filled) copied and compared, chkdsk | compare `bad=0`; no problems; the 300 files did not fit ("Insufficient disk space": the copy set was then cut to the drivers folder) |
| | cold start: chkdsk; `format /FS:NTFS /Q`, 162 drivers copied and compared, write, patch, verify, chkdsk, `fsutil fsinfo ntfsinfo` | no problems; `bad=0`; "Bytes Per Sector : 512", 512-byte clusters |
| | cold start: NTFS verify, compare, chkdsk, fsutil | `bad=0`; no problems; 512 |
| 7 x86 (`95893177...`) | FAT: format, 284 drivers compared, write, verify, patch, verify, chkdsk (elevated) | `bad=0`; no problems |
| | cold start: FAT verify, compare, chkdsk; NTFS format and the same set; fsutil | `bad=0`; no problems; "Bytes Per Sector : 512", 4 KiB clusters |
| | cold start: NTFS verify, compare, chkdsk, fsutil | `bad=0`; no problems; 512 |

QEMU's traces (`trsum.py`): every NVMe read and write moved whole
4096-byte blocks, and most one-block writes came right after a one-block
read of the same block, the read-modify-write's pre-read (98 SE's format
start: 1062 one-block writes, 989 of them so; 2000's NTFS start: 3051 and
2901; XP's NTFS start: 3125 and 3016; 7's NTFS start: 1819 and 1778; the
rest are whole-block writes with no pre-read). Before it, Stor98 gave these
systems the drive's own 4 KiB sectors, which they cannot use (Stor98
README, "Known limits").

Not run: a partition at sector 2048 (aligned I/O); ME; Vista; 8 to 10
(which see 512-byte sectors now too); the usb4-emu hook cases of TESTING
(cancellation and removal between the read-modify-write's phases, a
pre-read media error, the ownership recovery sequence), which stay owed.

Seen on the way, not this task's: on 98 SE the ASM4242 bridges' "PCI
standard PCI-to-PCI bridge" wizards must be finished (Next) for the
tunnelled drive to appear; cancelled, the drive is not enumerated and the
installer sees no NVMe device. On 2000 and XP the first start after the
install shows Found New Hardware wizards for the drive's functions that find
no driver ("Skip"); Stor98 is then bound by the installer's mark service,
as the installer's text says. On this host the rmw port range
49700-49719 is inside a Windows excluded TCP range, so QMP went over a
named pipe (QEMU `-qmp pipe:`), served by a file-spool proxy
(`qmpd.py`) - QEMU's Windows pipe accepts one connection only.

Evidence: `E:\xhci98-vm\a347\rmw\` (`s98b\`, `w2a\`, `xpa\`, `w7a\`:
`serial-*.log`, `trace-*.log`, `cmdline-*.txt`, the screenshots; `pids.txt`;
the scripts `start.sh`, `ntrun.sh`, `run9.sh`, `ntcold.sh`, `ntoff.sh`,
`shut9.sh`, `shut2k.sh`, `inst7.sh`, `xpwiz.sh`, `qmpd.py`, `qmp.py`,
`trsum.py`; the kit `xk\`, `xk-sums-final.txt`); RMWCHK's source
`E:\xhci98-scratch\w47\rmw\tool\rmwchk.c`.

## 47.2, 47.3 and 47.4 The Free USB drivers notice, the Progress wait line and "Shut down now" (2026-11-06, w47-inst)

Branch `w47-inst`, from `3.4.2.0` at `f19492e5`; commit `8672e74d`.
The owner's texts, verbatim:

- 47.2: `INST_FREE_NOTICE` ends at "... can make Windows unstable."
  (the sentence "Report problems at https://github.com/yeokm1/usb-wdm-free."
  gone); the free set's carried line under it is the non-free set's,
  "Latest version: https://github.com/yeokm1/usb-wdm-free.", so the
  window's swap of that line for a cut-off notice (`oth_layout`) went; the
  Finish page's paragraph, which gave the free set's address only through
  the notice, gives it as the non-free set's ("Free USB drivers: in <dir>.
  Latest version: <address>." then the notice; record 26 section 6.7.7,
  "its address always"). `xhciinst\README.TXT`, record 26 section 6.7.4
  and the host vectors follow. The release's generated readme
  (`package-common.ps1`) keeps the whole sentence (record 26 section
  6.7.1).
- 47.3: the Progress page's wait line is "<what>." whatever the bound
  (`wiz_waiting`); the bound and the engine's time-out unchanged. Record
  26 section 5.8 follows.
- 47.4: `INST_PT_SHUT_NOW` reads "Shut down now"; the Finish page's Help
  "Shut down now: Finish shuts Windows down; start it again yourself. ...";
  record 26's Help table follows (the Help self-test quotes the label
  whole, as before).

Reviewed and revised (working-tree review: no findings). `test_inst`
8946 checks, 0 failures; `xhciinst\build.cmd` x86 and amd64 compiled.

The gated build of `e8079c88` (below) carries these. Not read in a guest:
the Free USB drivers page (needs the `-free` download), the Progress wait
line and the Finish page's "Shut down now" (98 SE or ME after a Stor98
update) - owed as window shots; the strings are held by `test_inst` and
the window's Help self-test.

## 47.7 The 98 SE uninstall's leftovers: SetupX's OEM Name records and the Enum\SCSI disks (2026-11-06, w47-inst)

Branch `w47-inst`, commit `e8079c88` (with 47.8). The `3.2.0.0` matrix's
98 SE uninstall (`E:\xhci98-vm\m32\N1\RESULT.md`, F1 and F2) left
`SetupX\INF\OEM Name`'s `"C:\WINDOWS\INF\USBSTOR.INF"="USBSTOR.INF"` and
`"C:\WINDOWS\INF\USBNTMAP.INF"="USBNTMAP.INF"`, and four disk entries
under `Enum\SCSI\QEMU____QEMU_HARDDISK___2\`
(`USBSTOR&DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+&N1STICK01&000` and the
like) whose units the uninstall had removed. The design (record 26
section 6.5.8 steps 2 and 3, 6.5.11's sentence) was reviewed in two
rounds, the second adding the `XHCIUAS\` units adoption does not reach;
the code in four (two P2s each in rounds one and two, both times a
fail-open path; round four clean).

- The INFs placed in `%windir%\INF` (the record's `Placed`) now go with
  any OEM Name value named by their path or 8.3 alias
  (`oem_name_forget`), once deleted or found already gone; a copy kept
  keeps its value. A fresh 98 SE overlay's own export shows SetupX
  keeps such a value for Windows' own INFs in `%windir%\INF` too
  (`"C:\WINDOWS\INF\MACHINE.INF"="MACHINE.INF"` and the rest).
- Before each recorded or adopted unit, and before every `XHCIUAS\`
  entry (now read with the package's devnodes, removed after the
  recorded units and before the UAS devices), its disk entries go: an
  `Enum\SCSI\<device>\<instance>` whose instance is the unit's id with
  each `\` as `&`, then its own `SCSITargetID` and `SCSILUN` (digit
  strings; missing: no match), through the helper's `remove` job. A
  list that cannot be read whole (an unreadable key or value, more than
  eight disks of one unit) keeps the unit and its record and ends the
  run with steps left (exit 4).

Host vectors: `inst_scsi_disk_of` (the two readings of the matrix, case
aside, LUN 1, another unit, another LUN, no or non-digit values, the IDE
CD-ROM's entry) and `inst_uas_unit_id`; `test_inst` 8959 checks, 0
failures.

The leg (development host A, QEMU, q35 `w98se` base, overlay
`E:\xhci98-vm\a347\inst\w98b`, the base download of the TEST cut of
`e8079c88`, `xhci98-3.4.1.0.zip` sha256 `fa1c971e...`, `XHCIINST.EXE`
`e75a1f3f...`, `98-me\xhci98.sys` `2efc4569...`; captures by `regedit /e`
over COM1, `E:\xhci98-vm\a347\inst\evidence\w98b\`):

| Step | Result |
|---|---|
| B0, a fresh overlay | no `Enum\SCSI\...HARDDISK` key; `OEM Name` holds Windows' own `%windir%\INF` INFs (`MACHINE.INF`=`MACHINE.INF` and the rest) |
| `INSTALL /console /yes /nopause /storage` | exit 3; `USBNTMAP.INF` and `USBSTOR.INF` placed (`Placed` records); "Done. Please restart Windows now." |
| cold start; a stick (`usb-storage`, serial W47STICK1) | the wizard, Next to Finish, from the placed INFs |
| a UAS disk (`usb-uas` + `scsi-hd`, `qom-set attached true`) | the wizard for "xHCI98 USB Attached SCSI Storage", installed |
| capture C2 | `Enum\SCSI\QEMU____QEMU_HARDDISK___2\USBSTOR&...&W47STICK1&000` and `...\XHCIUAS&...&27842-0000:00:1C.0:00.0-3&000`; `OEM Name` `C:\WINDOWS\INF\USBSTOR.INF`=`USBSTOR.INF` and the same for `USBNTMAP.INF` (the matrix's F1, before the uninstall) |
| both unplugged; `INSTALL /console /uninstall /yes /nopause` | exit 3; the log: "Removing QEMU QEMU HARDDISK ..." before "Removing USB Disk ..." for each of the two units (the stick's and the UAS disk's, both adopted), then the USB device, the UAS device, the root hub, the controller; "Deleted C:\WINDOWS\INF\USBSTOR.INF." and "Deleted C:\WINDOWS\INF\USBNTMAP.INF.", each followed by "Removed Windows Setup's record of it (SetupX OEM Name)." |
| cold start; capture U2 | **pass**: no key under `Enum\SCSI` but the IDE DVD's; no `OEM Name` value naming `USBSTOR.INF`, `USBNTMAP.INF`, `xhci98.inf` or `xhciuas.inf`; the controller's wizard finds no driver, as before the install |

Not read: the `XHCIUAS\` pass for a unit adoption does not reach (98 SE
with NUSB's `USB20U`, and ME); a unit of several LUNs; the window's
uninstall (the same engine).

## 47.8 usbui.dll placed by the installer on XP x64 and Server 2003 x64 (2026-11-06, w47-inst)

Commit `e8079c88`. Design record 11 decision 13's new bullet, reviewed
in two rounds (round one: append `layout.inf` with
`SetupOpenAppendInfFileA`, an OS source root, a filter on the default
callback; and the trigger narrowed to a controller on `xhci98`). The x64
engine on NT 5.2, an install run with the USB drivers in it (not the
tools alone), after the devices' installs, with
`%SystemRoot%\System32\usbui.dll` missing and a controller on `xhci98`:
the run's local `xhci98.inf`, `layout.inf` appended,
`[Xhci.CopyUI]` queued alone (`SetupQueueCopySectionA`, no overwrite)
with the system's `SourcePath` (else `DriverCachePath`, its
`%SystemRoot%` read by hand) as the root, committed through the default
callback behind a filter refusing a source in the package's folder, the
local copy or the install folder (at `SPFILENOTIFY_STARTCOPY`, and a new
path given at `NEEDMEDIA` or `COPYERROR`), both paths compared in their
full long form, one that cannot be read in it refused. Placed: "Copied
usbui.dll ..." and a change; not: said, the run not failed. The plan and
`/list` say it first. The INF, its gates and the 32-bit installer are
unchanged.

The q35 `xpx64` base: no `usbui.dll` in `System32`; `HKLM\Software\
Microsoft\Windows\CurrentVersion\Setup` holds `SourcePath` = `D:\` (the
leg's transfer disk letter on this machine), `DriverCachePath` =
`C:\WINDOWS\Driver Cache`, `ServicePackSourcePath` = `D:\`.

The leg (QEMU, q35 `xpx64` base, the Driver Signing option set to
Ignore in the overlay, since a clone without the key stages unsigned x64
packages; overlay `E:\xhci98-vm\a347\inst\x64b`; `nt-x64\` from
`ConvertTo-FamilyLayout` of the gated `out\pkg-release-amd64` with its
`cm\` taken out, since the installer refuses an x64 package whose
Thunderbolt driver has no catalog; the tools of the TEST cut,
`XHCIIN64.EXE` `d0ea38b9...`, `xhci98.sys` x64 `8628cd78...`):

| Step | Result |
|---|---|
| before | `%SystemRoot%\system32\usbui.dll`: File Not Found |
| the Found New Hardware wizard, "Install from a list or specific location", `E:\PKG\nt-x64` | "xHCI98 USB 3 Controller" installed with no prompt (`usbd.sys` from the driver cache); then "xHCI98 USB 3 Root Hub" |
| `INSTALL /console /yes /nopause` (same version: Reinstall) | the plan: "usbui.dll is missing: Setup will copy it from Windows' own files, for the controller's Advanced tab and the root hub's Power tab."; the run: "Copied usbui.dll from Windows' own files: the controller's Advanced tab and the root hub's Power tab."; no prompt; exit 3; `usbui.dll` 123,392 bytes, dated 24-Mar-05 |
| Device Manager | **pass**: the controller's Advanced tab (the bandwidth table, "System reserved 10%") and the root hub's Power tab ("8 port(s) available") (`evidence\x64b\c6.png`, `c8.png`) |

Owed: an F6 install and an nLite install followed by `SETUP.EXE`; the
window (`SETUP.EXE` itself) rather than the console; a driver cache
without `driver.cab` (the CD prompt and the filter's refusal).

## 47.6 XHCITR's times on a counter that is not invariant (2026-11-06, w47-inst)

Commit `f72de760`. `docs/contributing/snapshot-reader.md` (the transfer
size bullet) and `xhcitr\tr_snap.c`: CPUID `8000_0000h`, then
`8000_0007h` `EDX` bit 8, read once per dump unless `/nosysinfo`;
MSVC 6.0 inline assembly on x86 (the `EFLAGS` ID test, `CPUID` as bytes
`0F A2`, `EBX` saved), the compiler's `__cpuid` on x64 (declared by
hand: WDK 7.1 has no `intrin.h`). The section's new lines (owner drafts)
and `-selftest-xstat`'s two new cases, held by `snaptest.cmd`.

The gated build passed `-selftest-xstat`'s new strings (`snaptest.cmd`,
53 runs for each of `XHCITR.EXE` and `XHCITR64.EXE`). On the XP x64
guest above (`-cpu qemu64`, TCG), `XHCITR64.EXE /dump` of the TEST cut
printed under the transfer-size heading "(this processor's time-stamp
counter is not invariant: its rate changes with the processor's speed,
so every time in this section is approximate)" (`evidence\x64b\s1.png`).
Not read: a processor that reports an invariant counter, and
`/nosysinfo` in a guest (the self-test holds it).

## The installer and tools tasks: the gated builds (2026-11-06, w47-inst)

`e8079c88`: the tools rebuilt, `build-driver.cmd all` x86 BUILD + GATES
PASSED, the host suites PASSED, `build-driver.cmd all -amd64` PASSED,
`make-package.ps1 -Arch amd64` staged (unsigned); an x86 TEST cut
(`make-release.ps1 -TestLabel w47-inst`, outside the tree) made the three
downloads. `d944e7f2` (TR-3, TL-7): `xhcitr\build.cmd`, `build-driver.cmd
all` x86 PASSED (XHCITR self-test 473 checks, snapshot self-test 53 runs
each image), the host suites PASSED; XHCIQUAL's own host tests pass and
`XHCIQUAL.EXE` builds.

## 47.23 (TR-3) XHCITR.EXE on 64-bit Windows without XHCITR64.EXE (2026-11-06, w47-inst)

Commit `4a479987` (the owner's decision of 2026-11-06). Reviewed in three
rounds (round one: the warning's delivery by mode, and its saying HKLM was
written where XHCITR only reads it). The leg, on the XP x64 guest above:
`XHCITR.EXE` of `d944e7f2` (sha256 `1f2b4c75...`) alone in `C:\TR3`,
`XHCITR.EXE /report C:\TR3\R.TXT`: the warning on the console, exit 0,
`R.TXT` 34,780 bytes (`evidence\x64b\t1.png`). Not read: the window's and
the icon's message box; an elevated `/result` child (no warning, by
design).

## 47.23 (TL-7) XHCIQUAL: no USB2 protocol port a caveat (2026-11-06, w47-inst)

Commit `d944e7f2` (the owner's decision of 2026-11-06). Reviewed in three
rounds (round one: the IRQ self-test lost the caveat, a stale test, an
80-column line, the shipped readme template; round two: a controller with
no root port at all must still be disqualified, `QUICK_NO_ROOT_PORTS`).
Host tests only; no DOS reading (QEMU's `qemu-xhci` always has USB2
ports).

## 47.25 The RTL8156 and RTL8156B in usb-wdm-free's rtl815x on 98 SE to XP x64 (2026-11-06, w47-wdm3)

Task 47.25 (owner, 2026-11-06): Realtek's RTL8156 and RTL8156B 2.5 Gb/s
Ethernet in `rtl815x`, on 98 SE, ME, 2000, XP and XP x64, the makers'
drivers first. The owner has neither adapter (owner, 2026-11-06: build
emulators from public information and test against them), so every leg
is a simulator leg. The real devices are owed to the bench.

The work is in `usb-wdm-free`, branch `w47-wdm3` (from `3942e20`, 0.1.8),
commits `d628f8f` (the legs, docs), and, after the owner's answers, `52a29f2` (0BDA:8056 and staging), `8055baf` and `9556e9d` (docs). The record is
`docs/testing/2026-11-06-rtl8156.md`. It was reviewed and revised in two
review rounds, ending in "approve, no material findings".

The task found the code already there. The chip core has served the four
chip ids since task 44.70 (2026-10-27; `ethernet.md` sections 6.4.0 and
7.6): `7020` and `7030` (RTL8156, Linux `RTL_VER_10`, `11`), and `7400`
and `7410` (RTL8156B, `RTL_VER_12`, `13`).

- The INFs list `0BDA:8156`: `RTL81598.INF` for 98 SE and ME,
  `RTL815X.INF` for 2000, and `RTLXP7.INF` for XP, XP x64 and Vista.
- `rtlemu.py` has modelled all four ids since then (`--chip 8156`,
  `8156-7020`, `8156b`, `8156b-7400`).

The coverage table (`ethernet-coverage.md`, 2026-10-27) predated that
code and still marked the two chips a gap from 98 SE to Vista.

What was missing:

- legs under the released xHCI98 (the 98 SE, ME and 2000 legs were
  under 3.1.0.0);
- 98 SE under NUSB's EHCI;
- any link faster than 1 Gb/s on XP and XP x64 (Windows' EHCI only);
- the RTL8156B on XP x64.

**The makers' drivers.** Realtek's live download list was read on
2026-11-06: <https://www.realtek.com/Download/List?cate_id=585>, 103,733
bytes, SHA-256 `9a009dac...b49b63b`. It offers XP 5.23 ("FE/GBE"), Vista
6.27, 7 7.61.20, 8/8.1 8.68.20, 10 10.68.20 and 11. It offers nothing for
98 SE, ME or 2000.

The pack's Realtek INFs were read for the ids on active lines:

| Package | Realtek ids on active lines |
|---|---|
| XP 5.23 | `8050`, `8152`, `8153` |
| Vista 6.27 | `8050`, `8152`, `8153` |
| 7 7.61.20 | adds `8053`, `8056`, `8155` and `8156`; `8157`, `815A`, `8057`, `805A`, `A157` and `A15A` are on commented-out lines only |

So Realtek covers the two chips on 7 only, which the pack already
carries. Nothing new goes to `usb-wdm-nonfree`.

`0BDA:8056` (an RTL8156B id in Realtek's 7 INF) is not in Linux's
`rtl8152_table` (`r8152.c` v7.3). Owner, 2026-11-06: listed anyway, from
the maker's INF (below).

**The build.** At `3942e20` (unchanged source):

- `build.cmd ndis5 trace` and `ndis51 trace` PASS (the released source
  with the port-0xE9 trace);
- `rtl815x.sys` x86 `d38ac633...`, x64 `ece06bff...` (unsigned);
- `test.cmd` PASS (1,219,967 chip-core and 34,568 core checks);
- `host.ps1` with `rtlsimtest.py`: 30 tests OK.

The INFs are the released ones byte for byte: `RTL81598.INF`
`048b69dc...`, `RTL815X.INF` `42d166f1...`, `RTLXP7.INF` `479bf50c...`.
The simulator is `rtlemu.py` at `3942e20` (`05f14740...`).

**The rig.**

- Emulator: a copy of the usb4-emu fork build `cb36497` with `usb-redir`
  (`3e7f6bf5...`). TCG, one vCPU a guest, at most two at once, headless.
- Ports 51020 to 51029 (QMP and simulator pairs).
- Every start and stop is in `E:\xhci98-vm\a347\wdm3\pids.txt`.
- Guests:
  - 98 SE, ME, 2000, XP and XP x64: fresh overlays of the q35 bases.
  - 98 SE under NUSB: an overlay of the 98 SE + NUSB 3.3 EHCI base
    (`E:\xhci98-vm\p45\w45-serial\bases\w98-nusb-ehci.qcow2`, the
    w98-nusb-hub chain of the earlier 98 SE legs).
- xHCI98 3.4.1.0 from `releases/3.4.1.0`, by `INSTALL.BAT /console /yes
  /nopause`:
  - 98 SE and ME: exit 3, then a cold start.
  - 2000: started at once.
  - XP: exit 3, then the root hub's Found New Hardware wizard by hand
    and a restart.
  - XP x64: exit 4 first ("Windows is still installing something ...
    Nothing was changed", a Found New Hardware window open). With the
    window closed, exit 3, then the root hub wizard and a restart.
- Install: the Found New Hardware wizard at the driver's folder. 98 SE and
  ME asked for the disk and took the folder typed; 98 SE then asked for a
  restart. 2000 and XP warned about the missing signature (Yes / Continue
  Anyway).

Each leg:

1. Plug.
2. Link (the trace).
3. A DHCP lease (the simulator's log).
4. `ping -n 8 -l 1472` and `-n 4 -l 4000` to the simulated host.
5. `ping -n 20` with the unplug after 6 s, then "device gone, 0 malformed
   transfers, 0 dropped frames".

| System, host controller driver | Chip (id), USB speed | Link | Result |
|---|---|---|---|
| 98 SE, xHCI98 3.4.1.0 | RTL8156 (`7030`), Super, first install | 2500 | installed, linked; restart asked |
| 98 SE, xHCI98 | RTL8156 (`7030`), Super, cable pull | 2500, down, 2500 | PASS |
| 98 SE, xHCI98 | RTL8156B (`7410`), Super, cable pull | 2500, down, 2500 | PASS |
| 98 SE, xHCI98 | RTL8156 (`7030`), High | 1000 | PASS |
| 98 SE, xHCI98 | RTL8156 (`7020`); RTL8156B (`7400`), Super | 2500 | PASS |
| 98 SE, NUSB 3.3 EHCI | RTL8156 (`7030`), High, first install, cable pull | 1000, down, 1000 | PASS |
| 98 SE, NUSB EHCI | RTL8156B (`7410`), High | 1000 | PASS |
| ME, xHCI98 3.4.1.0 | RTL8156 (`7030`), Super, first install | 2500 | PASS |
| ME, xHCI98 | RTL8156B (`7410`), Super, cable pull | 2500, down, 2500 | PASS (unplug without traffic) |
| 2000, xHCI98 3.4.1.0 | RTL8156 (`7030`), Super, first install | 2500 | PASS |
| 2000, xHCI98 | RTL8156B (`7410`), Super, cable pull | 2500, down, 2500 | PASS |
| 2000, xHCI98 | RTL8156 (`7030`), High | 1000 | PASS |
| XP, xHCI98 3.4.1.0 | RTL8156 (`7030`), Super, first install | 2500 | PASS |
| XP, xHCI98 | RTL8156B (`7410`), Super, cable pull | 2500, down, 2500 | PASS |
| XP, xHCI98 | RTL8156 (`7030`), High | 1000 | PASS |
| XP x64, xHCI98 3.4.1.0 | RTL8156 (`7030`), Super, first install (x64 build) | 2500 | PASS |
| XP x64, xHCI98 | RTL8156B (`7410`), Super, cable pull | 2500, down, 2500 | PASS |
| XP x64, xHCI98 | RTL8156 (`7030`), High | 1000 | PASS |

Notes on the legs:

- The 2026-10-27 record found 2000 at High Speed under xHCI98 3.1.0.0
  never configuring the simulated RTL8156. Under 3.4.1.0 it configured,
  linked at 1000 and passed.
- 2000 under Windows' EHCI was not run. On the q35 2000 base the
  simulator was never enumerated on the `ich9-usb-ehci1` controller (no
  transfer after the connect), and no driver was bound to it.
- On XP x64, Found New Hardware windows for base devices with no driver
  came up ahead of the adapter's; one cancel too many took the adapter's
  window too, and a replug brought it back.
- The NUSB guest hung at "Windows is shutting down" and was ended by QMP
  `quit`.

**Coverage per system**, with whose driver serves what:

| Chip | 98 SE to Vista | 7 |
|---|---|---|
| RTL8156 and RTL8156B on `0BDA:8156` | free `rtl815x` (no maker's driver) | Realtek's, in `usb-wdm-nonfree` |
| RTL8156B on `0BDA:8056` | free `rtl815x` from 2026-11-06 (listed from the maker's INF, no device seen) | Realtek's, in `usb-wdm-nonfree` |

**Docs changed** (`d628f8f`):

- the record `docs/testing/2026-11-06-rtl8156.md`;
- `ethernet.md` section 6.4.0, a "whose driver serves what" bullet;
- `ethernet-coverage.md`: the RTL8156 rows only. These are the summary
  row, the section 3.2 RTL8156B row (its chip ids corrected to
  `7400`/`7410`; `7420` is the RTL8153D), and section 6's two RTL8156
  gap rows, which became a closed row for `0BDA:8156` and an open row
  for `0BDA:8056`;
- `hcd-matrix.md`, the `rtl815x` row;
- README's "How each driver was evaluated" footnotes (j) and (k).

**Owner, 2026-11-06: 0BDA:8056 and staging** (`52a29f2`; docs `8055baf`, `9556e9d`).

`0BDA:8056` was added under the RTL8156 name to `RTL81598.INF`,
`RTL815X.INF`, and `RTLXP7.INF`'s XP, XP x64 and Vista sections. 7 stays
Realtek's. Only the id is taken from Realtek's 7 INF, where it sits in
the RTL8156B sections. A review confirmed the chip is detected by
`MR_TX_CONFIG` (`rtlchip.c` around lines 1826-1832 and 1895-1902), never
by the product id, so the id needs no chip path.

`rtlemu.py` gained `--chip 8156b-8056` (`7410` behind `0BDA:8056`, marked
as the model's inference). `rtlsimtest.py`: 31 tests OK.

Builds at `52a29f2`: `ndis5 free`, `ndis5 trace` and `ndis51 free` PASS.

One XP leg, xHCI98 3.4.1.0 at SuperSpeed, with the trace build
(`39cb628c...`) and the new `RTLXP7.INF` (`6888a8a0...`):

- The earlier installed INF did not take the new id.  The new one
  installed it as "Realtek RTL8156 USB 2.5G Ethernet (RTL815X)".
- Trace: "device bda:8056", "version 7410", 2500 Mb/s.
- DHCP, 8/8 and 4/4 pings, unplug under traffic: PASS.

`STAGE.TXT`: `net/rtl815x` W98SE WME, W2K, WXP and WXP64 went from COPY to
STAGE, "staged on simulator legs (owner, 2026-11-06)". The Vista, 7 and
x64 Vista/7 lines stay COPY.

The gate was `release.ps1 -Check -NonFree` (usb-wdm-nonfree `1a3bdb9`).
It ran on two scratch copies of the 0.1.8 definition, one with `free/`'s
INFs and one with `src`'s. Both passed: no id staged twice on any system
across both sets.

- The released x64 images came in through `-X64Dir`, with the release's
  signer thumbprint `E6A2749B...`; uac2's x64 inputs were taken as strip:
  entries.
- The written `STAGE.TXT` equals the committed one.
- `free/`'s INFs carry the id only from the next release.

Reviews: one round on the code (approve), and two on the docs (a P3 on
section 7.6's copy-only sentence, fixed, then approve).

Not run:

- ME at High Speed;
- Vista and 7 (Vista's 2026-10-27 legs stand; 7 is Realtek's);
- throughput beyond pings;
- the RTL8156's reversed-pair path.

REAL CHECK NEEDED: the simulator and the chip core both come from
`r8152.c`, so a shared misreading would pass here. On the bench, an
RTL8156 and an RTL8156B are owed on each system:

- install and the chip id;
- 2.5 Gb/s at SuperSpeed under xHCI98, and 1 Gb/s under an EHCI driver;
- DHCP and a large copy;
- a cable pull and an unplug.

Evidence: `E:\xhci98-vm\a347\wdm3\`:

- the guests' trace and serial logs: `g98\`, `gme\`, `g2k\`, `gxp\`,
  `gx64\`, `g\n\`;
- the simulator logs `ev\*.jsonl`;
- `shots\`, `pids.txt`, `STATE.md`.

## 47.26 The ASIX AX88772B and AX88772C on 98 SE and ME: usb-wdm-free `ax88772` (2026-11-06, w47-wdm4)

**The makers' drivers first.** ASIX's driver for 98 SE and ME in
usb-wdm-nonfree (`non-free/ethernet/ax88772/w98se-me/`, `AX88772.SYS`
3.0.3.12) lists only `0B95:7720` and `0B95:772A` (its `Ax88772.inf`, read
2026-11-06). Its AX88772B/C drivers there (`ax88772b/`, 5.4.3.0 to
5.14.12.0) start at 2000. ASIX's live AX88772C page
(`https://www.asix.com.tw/en/product/USBEthernet/Fast_Ethernet/AX88772C`,
read 2026-11-06, SHA-256 of the page `9976169e...8c2`) offers 7 to 11 and
an XP-to-10 setup program; no 98, ME or 2000. So 98 SE and ME had no
driver for these chips; 2000 and later keep ASIX's. Nothing to add to
usb-wdm-nonfree.

**The design** (usb-wdm-free `docs/design/ethernet.md` section 6.8), reviewed
and revised before the code: a new package `src/net/ax88772`, `AX772F.SYS`,
on the shared NDIS core (not `ax88179`: the two chips share nothing below
the core). Facts and sequences from Linux `asix_devices.c`, `asix_common.c`
and `asix.h` at v7.3 (GPL-2.0-or-later: adapted, notices kept, NOTICE.md
rows) and the PHY rule from `drivers/net/phy/ax88796b.c`. Ids `0B95:772B`,
`0B95:7E2B`, `17EF:7203`, `2001:1A02`, on 98 SE and ME only
(`AX772F98.INF`). The receive stream runs across bulk-IN transfers, so the
chip state carries the partial frame (the package's `NET_CHIP_STATE` 2048,
overridable in its netchip.h copy) and one 2048-byte bulk-IN transfer (the
chip's `QCTCTRL` burst) is outstanding, parsed whole before any frame is
given up; an optional `RxReset` restarts the stream where data was lost,
and 4 malformed transfers in a row restart receive (RX_CTL off and on,
inferred to empty the chip's buffer). Only chip code 0x20 with the
embedded PHY is served.

**The code**: usb-wdm-free branch `w47-wdm4`, `eb14fc7` to `8b3504e`, then
the record `d8219c8`. The package's copies of `netusb.c` and `netchip.h`
add, with no effect on the other packages: the `NET_CHIP_STATE` override,
`RxReset`, a resume that ignores a D0 in D0 and reports a chip that did
not come back, a pump that stops after a failed IRP allocation, and a
100 ms wait after the worker's exit event on 98 SE and ME. The pump loop
and the unjoinable 98 SE worker are in the `ax88179` and `rtl815x` copies
too (see the coordinator notes).

**Host tests** (`8b3504e`): `test.cmd` PASS: `chiptest` 58837 checks,
`coretest` 34568; `ax7simtest.py` 12 tests OK (log
`E:\xhci98-scratch\w47\wdm4\tmp\test-8b3504e.log`).

**Builds** (`8b3504e`, `build.cmd ndis5`): free `ax772f.sys`
`06d06232...c933e`, trace `b96127f9...1c67222`, `AX772F98.INF`
`9d99d320...0b4c6`; gate PASS (copy `E:\xhci98-scratch\w47\wdm4\build-8b3504e\`).

**The simulator**: `src/net/ax88772/ax7emu.py`, usb-redir, written from the
same Linux facts (inferred items marked in its docstring): the request
set, the EEPROM MAC, the PHY, records cut at the 2 KiB burst
(`--cut random` at random even offsets), the pad word.

**Legs** (trace build `8b3504e`; stock QEMU 11.1.0, pc, TCG, 1 vCPU;
`E:\xhci98-vm\a347\wdm4\`):

| Leg | Result |
|---|---|
| 98 SE (overlay of `w98-nusb-hub`, xHCI98 3.4.1.0 from `releases/3.4.1.0`, exit 3 then cold start), xHCI98, High Speed | install from `AX772F98.INF` (disk prompt answered `A:\`), restart; "chip start: 0, version 1020, 47 requests", "rx config: wrote 88, read back 88", "link 1, 100 Mb/s full duplex"; DHCP; ping 4/4 at 32, 8/8 at 1472, 4/4 at 4000 (records across transfers); cable flap and back; 10/10 at 1000; removal under `ping -t` and replug: replies again; 0 malformed |
| 98 SE, xHCI98, AX88772C (rev 0002), `--cut random` | 128 transfers cut at random even offsets for 12 echoes; 8/8 at 1472, 4/4 at 8000; 0 malformed |
| 98 SE, NUSB 3.3 EHCI (480 Mb/s) | same device instance, no wizard; DHCP; 8/8 at 1472, 4/4 at 4000; removal under `ping -t`, replug: replies again |
| ME (overlay of `me-hcd310`, xHCI98 3.4.1.0; the installer exits 4 for a stale registry-only xHCI entry of the base, the running controller "already runs" 3.4.1.0), xHCI98 | the wizard found the INF itself, restart; bring-up as above; 8/8 at 1472, 4/4 at 8000, 3/4 at 4000 after a flap (ME queued only the last fragments behind its ARP request); removal and replug clean |

Record: usb-wdm-free `docs/testing/2026-11-06-ax88772-sim.md`.
**PASS with the simulated chip.** Owed to the bench: a real AX88772B and
AX88772C on 98 SE (xHCI98, NUSB's EHCI) and ME.

**Review**: the design two rounds, the code eight more, revised each time,
until no material finding was left.

## 47.27 The CoreChips SR9700 and SR9800: usb-wdm-free `sr9700` and the SR9800 in `ax88772` (2026-11-06, w47-wdm4)

**The makers' drivers first** (searched 2026-11-06). CoreChips' site
`http://www.corechip-sz.com/` answered (the 2026-10-27 survey could not
reach it). Its download centre (`download.asp`, `endownload.asp`, pages 1
and 2, SHA-256 of the pages in `E:\xhci98-scratch\w47\wdm4\dl\corechip-home\`)
lists SR9900 drivers (Windows 11, a mass-production tool, Android notes)
and hub and PHY data sheets only, every download behind a login. Its
SR9700 product page (`productsview.asp?id=44`) has no driver; the SR9800
is not listed. No OEM site with a driver for either chip was found;
drivers said to be for them exist only on third-party driver sites
(oemdrivers.com, driverguide.com and others), which the policy does not
allow as a source. No Windows has an in-box driver for them. So nothing
for usb-wdm-nonfree, and the free drivers aim at 98 SE to 7, x86 and x64.

**The design** (`ethernet.md` section 6.9), reviewed and revised before the
code. Facts only from `sr9800.c`, `sr9800.h`, `sr9700.c` (GPL version 2
only, no SPDX) and `sr9700.h` (GPL-2.0-only).

- The SR9800 (`0FE6:9800`) joins `ax88772` as a second chip kind chosen by
  id: its own bring-up (LED mux, PHY power-up, bulk-IN size 4 KiB at High
  Speed and 2 KiB at Full), MII without the host-enable wait, its medium
  and receive defaults and Linux's pad rule (at a multiple of maxpacket
  minus 1, nothing else). `AX772F98.INF` lists it; a new `AX772F.INF` lists only
  it on 2000 to 7 (ASIX serves the AX88772B/C there).
- The SR9700 is its own package, `src/net/sr9700`, `SR9700F.SYS`: a register
  file, 10 Mb/s half duplex, packets of a status byte, a length with the
  FCS, the frame and the FCS; transmit with a 2-byte length, bumped with a
  pad byte at a packet boundary. `SR970098.INF` (`0FE6:9700`) and
  `SR9700F.INF` (2000 to 7, also `0FE6:9702` interface 1).
- Both packages build `ndis5` (x86, 98 SE to 7) and `ndis51` (x64, XP x64
  to 7 x64).

**The code**: `e496866` to `8272e2e` (sr9700 carries ax88772's core copy),
record `09c4c47`. Host tests: ax88772 `chiptest` 58863 checks, `coretest`
PASS, `ax7simtest.py` 13 OK; sr9700 `chiptest` 50, `coretest` PASS,
`sr9simtest.py` 4 OK. Builds: `ax772f.sys` trace `b70a7c20...` (`f428b74`),
sr9700 at `8272e2e`: trace `02b84f01...`, free `2f2298fd...`, x64
`a44cc1e2...` (copies `E:\xhci98-scratch\w47\wdm4\build-f428b74\`,
`build-8272e2e\`). The x64 builds are unsigned.

**Simulators**: `ax7emu.py --chip sr9800`; `src/net/sr9700/sremu.py`.

**Legs** (the same overlays and rig as 47.26):

| Leg | Result |
|---|---|
| SR9800, 98 SE, xHCI98 3.4.1.0 | install from `AX772F98.INF`, restart; "version 109800", "rx config: wrote 188", "rx 1 x 4096", "link 1, 100 Mb/s full duplex"; DHCP; 8/8 at 1472, 4/4 at 8000; flap; 6/6 at 1000; removal under `ping -t` clean |
| SR9800, 98 SE, NUSB 3.3 EHCI | replug on EHCI: the running `ping -t` answered again; 8/8 at 1472, 4/4 at 8000; removal clean |
| SR9800, ME, xHCI98 | install, restart; DHCP; 8/8 at 1472, 4/4 at 8000; removal clean |
| SR9700, 98 SE, xHCI98 (Full Speed) | install from `SR970098.INF`, restart; "version 9700", "rx config: wrote 31", "full speed USB", "link 1, 10 Mb/s half duplex"; DHCP; 8/8 at 1472, 4/4 at 4000; flap; 6/6 at 1000; removal under `ping -t` and replug: replies again |
| SR9700, ME, xHCI98 | install, restart; DHCP; 8/8 at 1472, 4/4 at 4000; removal clean |

Record: usb-wdm-free `docs/testing/2026-11-06-sr98xx-sim.md`. **PASS on 98
SE and ME with the simulated chips.** Not run: 2000 to 7 (x86 and x64),
the `9702` composite, the SR9700 under 98 SE's other host stack (a Full
Speed device; NUSB's EHCI does not take it and this rig has no companion
controller). Owed to the bench: the real chips.

**Review**: the design one round, the code four, revised each time; the
one finding left is the shared core's: on 98 SE and ME the worker thread
cannot be joined, and halt waited 100 ms after its exit event (closed
later, below).

**Follow-up (owner, 2026-11-06)**: the core's fixes in every package,
the NT legs of the SR9800 and SR9700, and the 98 SE/ME exit.

- The shared core (`netcore.c`, `netusb.c`, `netchip.c`, `netchip.h`,
  `netusb.h`) is identical in `ax88179`, `rtl815x`, `ax88772` and
  `sr9700` (`3278ee5`, `fa9791d`, `5a10485`, `f45732b`); only the core
  files of `ax88179` and `rtl815x` changed, and their `DriverVer`:
  `ax88179` 0.1.3.0, `rtl815x` 0.2.1.0. A resume tries the chip's Resume
  three times before the adapter is reported gone.
- The 98 SE/ME exit: the worker enters `KeSetEvent` with a frame whose
  return address is `PsTerminateSystemThread`, so no instruction of the
  image runs after halt's event is set (x86 inline assembly; both
  addresses read from the import table first). The event is a block of
  its own that is never freed: one `KEVENT` and its pool header per
  worker start, until restart (about 24 KB per 1000 plugs), since
  nothing on 98 SE or ME proves `KeSetEvent` has returned. NT keeps the
  thread-object join. The highest-priority exit tried first was no such
  barrier and is gone. Residual: the exit rests on assumptions about
  NTKERN (the imports resolve to its resident code, `KeSetEvent` takes
  three stack words, `PsTerminateSystemThread` neither returns nor uses
  its caller's frame), read from the legs' absence of a fault, not
  proven; and, on NT, a thread object that could not be referenced, as
  in xHCI98's own threads.
- Names: the SR9800 constants in `ax7chip.h` and the simulator's
  register names in `sremu.py` and `ax7emu.py` are this project's own.
- Host tests PASS in all four packages; every `ndis5` and `ndis51`
  build and import gate PASS.

| Leg | Result |
|---|---|
| SR9800, 2000, xHCI98 3.4.1.0 | installed from the `DevicePath`; DHCP; 4/4 at 32, 1472 and 4000; removal under `ping -t` clean; replug: replies again |
| SR9800, XP | the wizard took `AX772F.INF`; as on 2000 |
| SR9800, 7 x86 | `pnputil -a`; 4/4 at each size; removal clean |
| SR9700, 2000, XP, 7 x86 (Full Speed) | 4/4 at each size; removal clean |
| `ax88179`, 2000 (SuperSpeed); `rtl815x` RTL8157, XP | link 1000 and 5000 Mb/s; 4/4 at each size; removal clean |
| `ax88179`, `rtl815x` (RTL8153), `ax88772` (AX88772C; SR9800 three cycles), `sr9700`, 98 SE, new exit | each plug loaded the image anew ("DriverEntry"); link; 4/4 at 1472; removal under `ping -t`: "device gone", "halted"; `ax88179` replugged into a running `ping -t`; the guest stayed up |

Records: usb-wdm-free `docs/testing/2026-11-06-sr98xx-sim.md` (the NT
legs added) and `2026-11-06-netcore-port.md`. **PASS with the simulated
chips.** Not run: every x64 system (the builds wait for the owner's
signing), Vista, ME with the new exit, the `9702` composite. The
README rows: `ax88772` and `sr9700` simulator only on 98 SE, ME, 2000, XP
and 7 (x86), Vista not evaluated; the SR9700 under 98 SE's other host
stack owed.

**Review**: revised until no finding was left; the never-freed exit
event is the accepted residual above.

**x64 (owner's signed images, 2026-11-06)**: `ax772f.sys` `d01ea838...`
and `sr9700f.sys` `4a3c2f18...` (the `f45732b` `ndis51` free builds,
test certificate `E6A2749B...`, no timestamp; Authenticode digests read
again and matching), the INFs of `f45732b`, xHCI98 3.4.1.0 `nt-x64`.

| Leg | Result |
|---|---|
| SR9800, XP x64 | wizard, "Continue Anyway"; DHCP; 4/4 at 32, 1472 and 4000; removal under `ping -t`; replug: replies again |
| SR9700, XP x64 | the same |
| SR9800 and SR9700, 7 x64 (test signing on, certificate in Root and TrustedPublisher) | **FAIL**: neither driver starts, "CM_PROB_UNSIGNED_DRIVER" and Code Integrity event 3004 ("file hash could not be found"), also after the guest clock was moved past the certificate's start |
| Vista x64 | not run |

Record: usb-wdm-free `docs/testing/2026-11-06-sr98xx-sim.md` (`6079acb`).
*Inferred* cause of the 7 x64 refusal: the test certificate is itself
signed with SHA-256, which 7's kernel Code Integrity does not verify
without Microsoft's SHA-2 code-signing update; xHCI98's SHA-1 test
certificate is accepted on the same guest. Vista x64 is expected to
refuse it the same way. Earlier Vista and 7 x64 legs signed the images
in the guest with a throwaway certificate, so no earlier leg ran an
image signed with this certificate there. Owed: a decision on the
certificate (an SHA-1-signed one for Vista and 7, or the SHA-2 update in
the bases), then the 7 x64 and Vista x64 legs.
(Since read, below: 7 x64 with the SHA-2 update; and with the owner's
SHA-1 test certificate, the plain 7 x64, Vista x64 and XP x64 legs pass,
usb-wdm-free `w47-sha1`.)

**7 x64 with SHA-2 code-signing support (owner, 2026-11-06)**: a fresh
overlay of the 7 x64 base; Microsoft Update Catalog downloads, SHA-1
equal to the catalog's digests: KB4490628 (servicing stack,
`windows6.1-kb4490628-x64.msu` `d3de52d6...`, `wusa` exit 0), then
KB4474419 v3 (`windows6.1-kb4474419-v3-x64.msu` `b5614c6c...`, exit
3010); then xHCI98 3.4.1.0, the certificate, test signing and the INFs,
a restart. The same signed images.

| Leg | Result |
|---|---|
| SR9800, 7 x64 + KB4490628 + KB4474419 | DHCP; 4/4 at 32, 1472 and 4000; removal under `ping -t`; replug: replies again |
| SR9700, the same guest | the same |
| Both, the base without the updates (the control, above) | FAIL, Code Integrity 3004 |

No Code Integrity event on the updated guest; the guest clock stayed at
2026-09-20. Measured: fail without the two updates, pass with them, on
this base. Inferred: the cause is the test certificate's own SHA-256
signature (the images' digest is SHA-1); KB3033929 not tried. Vista x64
not run; Microsoft's SHA-2 article (KB4472027) lists updates for 7 SP1,
Server 2008 R2 SP1 and Server 2008 SP2, none for Vista. Record:
usb-wdm-free `docs/testing/2026-11-06-sr98xx-sim.md` (`11e20af`,
`b141b47`); README footnote (m): "Under xHCI98. On 7 x64 the drivers
started only after Microsoft's SHA-2 code-signing update (KB4474419,
after KB4490628) was installed; without it Windows refused their
signature."

## 47.21 Thunderbolt 1, 2 and 3 on Apple Macs: the emulator's rest (2026-11-06)

Worker apple, development host A. Branch `w47-apple` (from `f19492e5`),
tree `E:\xhci98-wt\w47-apple`; `usb4-emu` branch `w47-apple` (from its
`main` `8f200d5`), tree `E:\usb4-emu-wt\w47-apple`. Continues
`runs/run-45.md` "45.21", "45.21 (code)" and "45.58": the code of record
22 section 19.9 and its host vectors were done, and the q35 legs on 7 x86
and 10 x64 had run on the Alpine, Falcon, Cactus and Light Ridge models.
Emulator only; the Mac itself stays 45-E.11, and the release notes keep
"Not claimed: Apple machines".

### The design

Record 22 section 19.10 (`5b806c14`, reviewed and revised): what the
emulator can still read of section 19, five items - (1) Titan Ridge,
which no leg had run; (2) the rest of section 19.2's systems, 7 x64, 8
x64 and 8.1 x64; (3) a device router of another generation behind a host;
(4) a tunnel the Mac's firmware built, for section 9's discovery and
cleaning on this row, which needs an emulator change; (5) the router's
generation in the status record and the tools' words (45.58's finding).
The review's one point: the mixed-generation link bonds as Linux bonds it
(the device router's link controller and both lane adapters' widths; the
host's link controller is not asked), so a generation 3 device on the
emulator's Cactus Ridge bonds - the model's lane adapters are Alpine
Ridge's image - and a real generation 1 host's widths are unread.

### The emulator (`usb4-emu` `2ca94a1`, QEMU fork `4fd5898`)

`preos-cm=on` on the `tb*-apple-*` profiles (off by default): the
machine's firmware runs Linux's software connection manager sequence on
Thunderbolt routers before the OS - the host router and each device
router at depth 1 enumerated by one write of ROUTER_CS_1 to 4 with Valid
and Notification Timeout 255, their Plug Events active, the link
controller's port-configured marks from generation 2, the lanes bonded by
the rule above, the PCIe tunnel on HopID 8 (lane HopIDs 8 and 9, the USB4
firmware's), every hot plug event it caused consumed (its
`docs/TB-SWCM-HOSTS.md` section 3a; inferred from Linux, nothing
captured). `preos-usb3` is refused there. qtest `/usb4-tbhost/preos`
(Alpine and Titan Ridge with a generation 3 device, Falcon Ridge with a
generation 2 one, Cactus Ridge with a Port Ridge and a generation 3
device, Light Ridge with a generation 3 one on port B, nothing attached,
and the option off): `usb4-tbhost-test` 13 of 13; every other usb4 suite
complete against its plan (`usb4-tunnel`'s `window-all-ones` failed once
while builds ran beside it and passed 31 of 31 twice on its own).
Reviewed and revised. Build: `E:\usb4-emu-wt\w47-apple\src\qemu\build\
qemu-system-x86_64.exe` (SHA-256 `f1e33415ac3e3e8e...`).

### The code (`f6835ba2`)

Item 5: on a `CM_ST_GEN_TBT3` entry the software connection manager read
(a host port's router, a chained router), bits 15:8 of `CM_ST_E_GEN` carry
the router's Thunderbolt generation (`TbtGen`); 0 elsewhere reads as 3,
so the record's layout and version stay and an earlier tool reads a new
record as before. `XHCIINST /devices` and XHCITR's generation and Link
lines say "Thunderbolt 1" or "Thunderbolt 2" for such a router; XHCITR
no longer says "running as Thunderbolt 2" of a Thunderbolt 1 or 2 router
behind a Falcon Ridge host. Host vectors: `test_cm_status`
`test_tbt_gen`, `test_tr` (the generation words, the Link line with and
without a width, the "running as" rule). Reviewed and revised (the first
round found the Link line still saying Thunderbolt 3).

### Builds and the kit

- The gated x86 build of `f6835ba2` (`scripts\build-driver.cmd all`, slot
  2): "BUILD + GATES PASSED (release qemu)", 50:41
  (`E:\xhci98-scratch\w47\apple\gb-x86.log`); host tests passed
  (`ht2.log`: `test_tr` 1636 checks, `test_cm_status` 143, `test_inst`
  8946, 0 failures).
- The tools built (`xhciinst`, `xhcitr`, `xhcimark` `build.cmd`, their
  self-tests passed), `make-package.ps1 -Flavor release -Arch x86`, then
  `ConvertTo-FamilyLayout` into the kit `E:\xhci98-vm\a347\apple\kit\xf\
  PKG` with `INSTALL.BAT`, `SETUP.EXE` and `tools\` beside it:
  `xhcicm.sys` `f2d71d2e5b5315fc...`, `xhci98.sys` `6187d75a20809fe5...`,
  `XHCIINST.EXE` `9878fb2ad267d889...`, `XHCITR.EXE` `b64b090c311cd47d...`,
  `stor98.sys` `48297bc9a39d1c9d...` (`hashes.txt`).

### The legs (q35; kit `E:\xhci98-vm\a347\apple\kit`, `leg.ps1`)

Each a fresh overlay of the clean q35 base, the host in its controller's
own PCIe switch (gated root port `00:08.0`, `usb4-<sw>-usp`, the NHI's
port `0.0`, tunnel ports `1.0` and `2.0`), the FAT NVMe enclosure at
power-on on port A, `INSTALL.BAT /console /yes /nopause /thunderbolt
/stor98` (7 x86: Windows' unsigned-driver prompts answered), a restart,
`R.BAT` (the PCI devnodes and codes, the volumes, `XHCIINST /devices`),
then QMP `device_del` of the enclosure, `R.BAT`, `device_add` on port B,
`R.BAT`. QMP ports 47980 to 47999 (49740 to 49759 lie in a range this
host reserves). Every start and stop in `pids.txt`.

| Leg | System, host | Port A at power-on | Unplug | Port B |
|---|---|---|---|---|
| `tr-w7x86` a | 7 x86, `tb3-apple-titan-ridge` (`usb4-tr-*` switch, NHI `8086:15EB`, subsystem `1234:106B`), a `15D3` device | install exit 3; after the restart 20 PCI devnodes Code 0; "xHCI98 TB3 Controller (Intel Titan Ridge)", "USB4 software connection manager"; "Thunderbolt 3, port 1, depth 1, approved", router `8086:15D3`, two lanes bonded, PCIe up at `02:01.0` (bridge `8086:15EA`); Stor98 started, D: ORICO | D: gone | "port 2", PCIe up at `02:02.0` (PCIe Down 9), Stor98, D: ORICO |
| `ar-w7x86` a | 7 x86, `tb3-apple-alpine-ridge` (NHI `8086:1577`, subsystem `1234:106B`), a `15D3` device | install exit 3; 20 devnodes Code 0; "xHCI98 TB3 Controller (Intel Alpine Ridge)"; "Thunderbolt 3, port 1, depth 1", two lanes bonded, PCIe up at `02:01.0`, Stor98, D: ORICO | - | (item 3 below) |

**Item 3, a device of another generation** (the `ar-w7x86` a overlay,
the enclosure kept on port A; a second enclosure with its own NVMe overlay
plugged by QMP on port B, read, unplugged; `kit\other.sh`):

| Plugged on port B | Read (`rep-ap1.txt`, `rep-ap2.txt`) | Unplugged |
|---|---|---|
| a Port Ridge router (`tb3-router-id=0x1549`, generation 1, no link controller) | 21 devnodes Code 0; "Thunderbolt 1, port 2, depth 1, approved", router `8086:1549`, "link: one lane", PCIe up at `02:02.0`, a second Stor98 controller started | its devnodes gone, port A's untouched |
| a Falcon Ridge router (`0x156d`, generation 2) | "Thunderbolt 2, port 2, depth 1", router `8086:156D`, "two lanes bonded", PCIe up at `02:02.0`, Stor98 started | the same |

No second volume is listed for the second drive: both NVMe disks are
overlays of one image, and Windows keeps one of two disks with the same
signature offline (as `runs/run-45.md` "45.13" read). The words
"Thunderbolt 1" and "Thunderbolt 2" are item 5's.

**Item 4, the firmware's tunnel** (the same overlay restarted with
`preos-cm=on`):

| Leg | `XhciCmPreOsCleanup` | Read | Verdict |
|---|---|---|---|
| p0 | 0 (as installed) | the emulator's trace: the firmware's tunnel up once at power-on, the OS taking the host interface, no tunnel transition after; 20 devnodes Code 0; "PCIe up (found at start)", two lanes bonded, Stor98 started, D: ORICO with no re-enumeration; then the unplug (surprise down) and a plug on port B: "PCIe up" at `02:02.0`, D: ORICO | preserved: pass |
| p1 | 1 (`INSTALL /set`, which reinstalled the same version, then a restart; the firmware tunnelled port B at the restart) | no tunnel transition after the OS took over; "PCIe up (found at start)" on port 2, D: ORICO | nothing cleaned: pass |
| p2, p2b | 2 (likewise; p2b relaunched with the trace of the link controller's port reset) | the firmware's tunnel down (admin), `usb4_port_dpr` adapter 3 set then clear (the link controller's Downstream Port Reset, block 1), the tunnel up again built by this driver; "PCIe up" (not found at start), "reaches the port at PCI 02:02.0", D: ORICO. **The cycle ran twice in the one boot** (the trace: down, reset, up, down, reset, up) | cleaned and rebuilt: pass; the second cleaning is a finding (below) |

A tunnel found at start is listed as "surprise removal OK: no; listed in
Safely Remove Hardware" and with no "reaches the port" line, where a
tunnel this driver built reads "yes; not listed" and names its bridge
(45.58's "found at start" read the bridge line the same way).

**Cactus Ridge** (`cr-w7x86`, 7 x86, `tb1-apple-cactus-ridge`, NHI
`8086:1547`, subsystem `2222:1111`):

| Leg | Read | Verdict |
|---|---|---|
| a (item 3): a generation 3 device (`15D3`) on port A at power-on, installed fresh | 20 devnodes Code 0; "xHCI98 TB1 Controller (Intel Cactus Ridge)"; "Thunderbolt 3, port 1, depth 1", router `8086:15D3`, **two lanes bonded** (the device's link controller says it can and the model's lane adapters list two lanes, item 3's rule), PCIe up at `02:01.0`, Stor98, D: ORICO | pass, as predicted |
| p0 (item 4): restarted with `preos-cm=on` and a Port Ridge device (`1549`) on port A, `XhciCmPreOsCleanup` 0 | the firmware's tunnel up once, no transition after the OS took over; "Thunderbolt 1, port 1, depth 1", "link: one lane", "PCIe up (found at start)", Stor98, D: ORICO | preserved, the generation 1 router identified by its DROM UID: pass |
| p2 (item 4): `INSTALL /set XhciCmPreOsCleanup=2`, a restart | after the OS took over: the tunnel down (admin) and up again, and no `usb4_port_dpr` (generation 1 has no port reset; the paths torn down alone); "PCIe up", "reaches the port at PCI 02:01.0", one lane, D: ORICO; the Port Ridge router adopted as it was (no re-enumeration) | cleaned and rebuilt: pass |

**The finding: value 2 cleaned twice on a cold start.** On `ar-w7x86` p2b
(a cold start with value 2) the cleaning ran twice in the one boot, the
drive going and coming back twice; on the warm restarts (p2 on Alpine
Ridge, p2 on Cactus Ridge) once. 45.58 read the same NHI stack stopped
and started once on cold-started 7 x86 overlays ("found at start" there).
The successor instance finds the tunnel its predecessor has just rebuilt,
matches it to the predecessor's record (record 22 section 9.4, `Own`) and,
value 2 cleaning every preserved port, cleans it again. Not this task's
own code (record 22 sections 9.3 and 9.4, any USB4 host as well). A
one-line exemption for an `Own` port was drafted
(`E:\xhci98-scratch\w47\apple\cleanup2-own-draft.diff`) and its review
found two gaps: on a version 2 host the Host Router Reset a foreign
sibling triggers still takes the exempted port down without its
teardown, and `Own` covers only the port's own tunnels, not a chained
child left half-built. So it was taken out again and is left: a design
for the owner and a planned item.

**Titan Ridge on 10 x64** (`tr-w10x64`; the x64 kit, catalogs made on the
host unsigned and every `.sys` and `.cat` signed in the guest by
`SIGN64.BAT` with a throwaway certificate, test mode on; nothing signed on
the host):

| Leg | Read | Verdict |
|---|---|---|
| a | 13 files signed and verified, install exit 3, a restart; 14 devnodes Code 0; "xHCI98 TB3 Controller (Intel Titan Ridge)"; port 1, bonded, PCIe up at `02:01.0`, Stor98, E: ORICO; unplug: E: gone; port B: PCIe up at `02:02.0`, E: ORICO on Windows' `stornvme` (a new instance takes Windows' own driver, as 45.21 and 45.58 read on 10 x64) | item 1: pass |
| p0 (item 4) | the same overlay restarted with `preos-cm=on`, the enclosure on port A: the firmware's tunnel up once, no transition after the OS took over; "PCIe up (found at start)", Stor98, E: ORICO | preserved on 10 x64: pass (on Titan Ridge, where record 22 section 19.10 named Alpine Ridge for this leg: the same generation 3 code path, one install saved) |

**Light Ridge on 7 x64** (`lr-w7x64`, item 2; the x64 kit signed in the
guest as above): install exit 3, a restart; 20 devnodes Code 0; "xHCI98
TB1 Controller (Intel Light Ridge)"; "Thunderbolt 1, port 1, depth 1",
router `8086:1549`, one lane, PCIe up at `02:01.0`, Stor98, D: ORICO;
unplug: D: gone; port B: PCIe up at `02:02.0` (PCIe Down 7), Stor98, D:
ORICO. Pass. (Windows' AutoPlay opened ORICO at the install, and the
kit's typed restart went to that window; the restart was typed again.)

**Cactus Ridge on 8 x64** (`cr-w8x64`, item 2, a Port Ridge device):
install exit 3, a restart; 14 devnodes Code 0; "xHCI98 TB1 Controller
(Intel Cactus Ridge)"; "Thunderbolt 1, port 1, depth 1", one lane, PCIe up
at `02:01.0`, Stor98, E: ORICO; unplug: E: gone; port B: PCIe up at
`02:02.0`, Stor98 (8 keeps Stor98 for the new instance, where 10 took
`stornvme`), E: ORICO. Pass.

**Falcon Ridge on 8.1 x64** (`fr-w81x64`, item 2, a Falcon Ridge device):
install exit 3, a restart; 14 devnodes Code 0; "xHCI98 TB2 Controller
(Intel Falcon Ridge)"; "Thunderbolt 2, port 1, depth 1", router
`8086:156D`, two lanes bonded, PCIe up at `02:01.0`, Stor98, E: ORICO;
unplug: E: gone; port B: PCIe up at `02:02.0` (PCIe Down 8), Stor98, E:
ORICO. Pass.

Every item of section 19.10 has legs, not all it asks for: item 1 (Titan Ridge, 7 x86 and
10 x64), item 2 (7 x64, 8 x64, 8.1 x64), item 3 (Port Ridge and Falcon
Ridge devices behind Alpine Ridge, a generation 3 device behind Cactus
Ridge), item 4 (the firmware's tunnel on Alpine Ridge, Cactus Ridge and,
for 10 x64, Titan Ridge; values 0, 1 and 2 on Alpine Ridge, 0 and 2 on
Cactus Ridge), item 5 (the words, read in every leg's `XHCIINST /devices`;
XHCITR's lines are the host vectors'). Not read, though section 19.10
names them: Cactus Ridge with value 1, and the unplug and plug again
after each cleanup value on Alpine and Cactus Ridge.


### Left in 47.21

- The Mac itself (45-E.11, in "Planned work"): which systems start, the
  NHI's ids and host interface version, the host on the PCI bus with
  nothing attached, the firmware's own tunnels at power-on and at a
  resume, a display.
- The release notes' "Not claimed: Apple machines" (the owner's, Q5): a
  draft of the wording that would replace it is in the worker's report.
- `XhciCmPreOsCleanup` 2 cleaning this driver's own tunnel again after a
  restart of the NHI's stack within one boot (above): a design for the
  owner, proposed as a planned item.
- Not read here by design: 98 SE to Vista on these hosts (section 19.2,
  Q2), DisplayPort (Q3), the 9x ids of a Light or Cactus Ridge NHI
  (section 19.3).


## 47.29 the handover refused for an unknown build said in XHCITR's report (2026-11-06)

Worker apple, the same branch and tree (the coordinator's assignment after
47.13, owner 2026-11-06). The gap 47.13 found: an ASM4242 whose firmware
is not a known build but has the same handover code (tier 3), with
`XhciCmAsm4242Structural` 0, is refused (`0Eh`; on NT `110h`,
`cm.osc.read` `0Eh`), and XHCITR's report said only "Stage: failed"; on
2000 to 10 the value is only a row under the installer's Advanced
settings.

### The design and the code

Record 32 section 12.8.11, a paragraph "Bit 26": the connection manager
sets status bit 26 (`CM_ST_F_HO_SAMEOFF`) at that refusal alone, on every
system - from its handover word's bit 10, `CM_OSCN_F_SAMEOFF`, set where
the refusal is made (98 SE and ME: `CmOscHandover`; NT: `cmOsnRead`, the
start's read path and the S4 resend's) - never from tier 3 with bit 19
clear, since a start at value 1 can still end unsent. It is cleared at
each start (`cmNhiStart`), at each NT attempt (`CmOscntHandover`, which on
98 SE and ME keeps what `CmOscHandover` just set), at each board decision
(`cmOsnBoard`) and when a served S4 resend begins (`CmOscntResume`). The
record's layout and version stay; an earlier reader reads 0 for the bit.
XHCITR's report prints the line below the stage line (`tr_ho_same_text`,
`xhcitr.c`). Host vectors: `test_cm_status` (bit 26 from bit 10; tier 3
alone sets none), `test_tr` (the line from bit 26 alone, none for tier 3
alone or no flags).

The report's line, the owner's draft for the owner to rephrase (task
46.18's rule), as XHCITR prints it:

```
  Handover not sent: this firmware is not a known build, but has the same
  handover code. To try it, set the Thunderbolt setting for such firmware
  to yes.
```

Reviewed and revised: the review found the NT attempt's reset of the
flags clearing the bit 98 SE had just set, the S4 resend not clearing it,
and the resend's earlier exits keeping an earlier attempt's bit; each
fixed.

### Builds

The gated x86 build of `c52dffc1` (`build-driver.cmd all`): "BUILD +
GATES PASSED (release qemu)", 42:56 (`E:\xhci98-scratch\w47\apple\
gb2-x86.log`); the gated amd64 build (`release -amd64`): "BUILD + GATES
PASSED (release)" (`gb2-amd64.log`); host tests passed (`ht6.log`:
`test_tr` 1639 checks, `test_cm_status` 145). `xhcicm.sys` x86 release
`33f77867...`, qemu `fa58a3f5...`, amd64 `f161a75a...`; `XHCITR.EXE`
`c007c9a9...`. The x86 package (`make-package.ps1 -Flavor release -Arch
x86`, family layout) in `E:\xhci98-vm\a347\apple\k429\xt10\PKG`.

### The leg (10 x86, the emulator's ASM4242 with build 2's tables)

`k429\` holds copies of 47.13's scripts (`osc.ps1`, its
`launch-q35.ps1`, `elev.sh`, `I.CMD`, `N.CMD`, `S.CMD`, `W.CMD`); the
tables read from `E:\xhci98-scratch\w47\cm\aml\nt-b2` (47.13's, read
only), `-FwData 0x9FF00000`, the emulator of this worker's `usb4-emu`
build (`2ca94a1`, the same ASM4242 model as 47.13's `8f200d5`). A fresh
overlay of the q35 `w10x86` base (`k429\n10`), QMP 47981.

| Step | Read |
|---|---|
| a: `I.CMD` (the package, `/thunderbolt`), Windows' publisher prompts answered | exit 3; the values in the driver key: `XhciCmAsm4242Handover` 1, `Structural` 0, `Reads` 0 |
| `W.CMD XhciCmAsm4242Reads 1` (machine-wide), a restart, `S.CMD c` | tier 1, `cm.osc.read=0Dh`, `110h`: the release flavour declines the emulator's tables, as 47.13's d1 read; no line (correctly: not this refusal) |
| b: the qemu flavour's `xhcicm.sys` of the gated build (`fa58a3f5...`) in its place (`N.CMD`, after a cold start so that the transfer disk had it), a restart, `S.CMD c` | `cm.osc.tier=3`, `cm.osc.read=0Eh`, `cm.osc.refused=110h`; XHCITR's report: "Stage: failed", then the three lines of the new message |
| `W.CMD XhciCmAsm4242Structural 1`, a restart, `S.CMD d` | `cm.osc.tier=3`, `cm.osc.handover=100h`, `cm.osc.query=000F0100h`, `u4.host.id=174C2428`; "Stage: active", no "Handover not sent" line |

Verdict: pass on 10 x86. 98 SE and ME not read: the 9x half (the bit set
in `CmOscHandover`, kept through `CmOscntHandover`) rests on the host
vectors and the review; 98 SE with these ACPI tables stops at CONFIGMG
(47.13), so it would be an ME leg, left owed. Evidence: `k429\n10\`
(`serial-a.log`, `serial-b.log`, `s-c.txt`, `s-bc.txt`, `s-bd.txt`,
`debugcon-b.log`), `k429\pids.txt` and `pids.txt`.

## 47.28 A keyboard's volume and media keys on 98 SE and ME: the reading and the design (2026-11-06, w47-wdm5)

The task: whether Windows 98 SE and ME act on a keyboard's HID Consumer
page keys (usage page 0x0C, the Consumer Control collection) under
xHCI98 3.4.1.0, and where they do nothing, an INF or a small HID filter in
usb-wdm-free, the design put to the owner first. The work is in
usb-wdm-free, branch `w47-wdm5`: `docs/design/mediakeys.md` (the design),
`docs/testing/2026-11-06-mediakeys.md` (the readings record),
`src/hid/mediakeys/kbdemu.py` (a usb-redir keyboard simulator) and
`src/hid/mediakeys/keytest.c` (`KEYTEST.EXE`, a guest reading tool). No
driver code.

Rig: the usb4-emu fork build `cb36497` (QEMU 11.1.2 with `usb-redir`,
`3e7f6bf5...`), overlays of the clean q35 bases (98 SE, ME, 2000 SP4, XP
SP3), xHCI98 3.4.1.0 as published (`xhci98.sys` `f67f2bd2...`), QEMU's
`usb-audio` for a mixer to watch. Devices: `kbdemu.py`, a boot keyboard
plus a Consumer Control collection on a second interface (`twoif`), the
same with report IDs and a System Control collection (`twoifid`), one
interface with report IDs (`oneif`), a one-bit-per-usage layout
(`bitmap`), the keyboard alone (`boot`); QEMU's `usb-kbd` and the PS/2
keyboard as controls. `KEYTEST.EXE` logs key messages, WM_APPCOMMAND,
GetAsyncKeyState of VK A6..B7, the mixer's volume and mute and the
foreground window; `/enum` lists the device nodes and their INF
sections; `/hid` reads the Consumer Control collections through hid.dll.
Kit: `E:\xhci98-vm\a347\wdm5\` (`ev\`, `shots\`, `scripts\`).

| Key | 98 SE | ME | 2000 SP4 | XP SP3 |
|---|---|---|---|---|
| Volume Up, Down, Mute | the mixer steps after a restart (`hidserv.exe`; the first-plug leg had no started audio device, so whether hidserv needs the restart is not separated) | mixer, WM_APPCOMMAND 10, 9, 8, VK AF, AE, AD, first plug | the same after a restart (VK and WM_APPCOMMAND on the first plug, the mixer still) | as ME |
| Play/Pause, Next, Previous, Stop | nothing | WM_APPCOMMAND 14, 11, 12, 13, VK B3, B0, B1, B2 | the same | the same |
| Home, Search | nothing | VK AC, AA; Internet Explorer, Search open | the same | the same |
| Mail, Calculator, My Computer, media player | nothing | VK B4, B7, B6, B5; Calculator, My Computer, Windows Media Player open (Mail: no window seen) | VK only, nothing opens | VK; Outlook Express, Calculator, My Computer, Windows Media Player 9 open |
| Keyboard page 7F, 80, 81 | nothing | nothing | not read | not read |

The four layouts read the same on 98 SE and on ME. On 98 SE and ME the
Consumer Control collection gets `HIDDEV.INF` `[HIDSystemConsumer]`
(`HID_DEVICE_UP:000C_U:0001`, which installs `hidserv.exe` through
`RunServices` and `RunOnce`, no driver), the keyboard collection
`kbdhid.vxd`; 2000 and XP use `hidserv.inf` `[HIDSystemConsumer]`.

What decides the design, all on 98 SE:

1. The PS/2 keyboard's media scan codes (E0 30, E0 22, ...) arrive as
   WM_KEYDOWN with VK FF and nothing acts on them; 98 SE has no
   WM_APPCOMMAND (Microsoft's WM_APPCOMMAND page: Windows 2000 at least,
   and "software that simulates this message on Windows systems earlier
   than Windows 2000").
2. A throwaway INF binding the collection to `kbdhid.vxd` (as
   `[HIDSystemControl]` does for system controls) installs and starts;
   `kbdhid.vxd` maps none of the consumer usages.
3. A user-mode program opens the collection (shared) and reads every
   report with blocking reads (98 SE completed no overlapped read), while
   hidserv keeps acting on the same reports.

So the gap is 98 SE's non-volume keys, and no system component consumes
them: the INF binding and the scan codes read gave nothing, and a filter
could only emit other keys chosen per program, in the kernel, under every
keyboard. Microsoft's "Keyboard Scan Code
Specification" 1.3a (March 2000), "Usage Page and Usages for Audio
Control", documents the 98 support as audio controls through
`HIDSERVE.EXE` (Volume, Mute, Bass Boost, Volume Increment and Decrement
"supported in Windows 98"). Makers' drivers: Logitech's iTouch 2.22
(still on Logitech's site, 98 SE to XP, Logitech keyboards only, no terms
readable; not carried); Microsoft offers no IntelliType for 98 SE or ME
any more.

Options put to the owner (design section 7): A, document only (98 SE
volume and mute after the first restart, other keys need the maker's
software); B, a 98 SE user-mode helper (`MKEY98.EXE`) that launches
Calculator, My Computer, the browser, Search, Mail and the media player;
C, B plus WM_APPCOMMAND to the foreground window for the transport and
browser keys; D, a HID filter or INF (not recommended: per-program key
choices in the kernel). Recommended: A now, C if the owner wants the 98 SE gap
closed for keyboards no maker covers.

Seen on the way, outside the task: 98 SE stopped once with "A fatal
exception 00 has occurred at 0028:FF09FED4 in VXD USBAUDIO(01) +
00002ED4" (98 SE's `usbaudio.sys` on QEMU's `usb-audio` under xHCI98
3.4.1.0) around an unplug and a plug on other ports; not repeated on a
fresh cold start. This QEMU build aborts on QMP `input-send-event` with a
`device` argument under `-display none`.

Owed: the owner's choice (A to D); a real keyboard with media keys on 98
SE and ME (the bench); NUSB's EHCI and Windows' UHCI not read (the gap is
above the HID class driver).

**The owner's choice (2026-11-06).** Option A: documentation only. B, C and D stay in the design, not built (usb-wdm-free `c0e56db`). The release-notes sentence (a draft): "On Windows 98 SE, a keyboard's volume and mute keys work after a restart; its other media keys need the keyboard maker's software. ME, 2000 and XP handle them." Logitech's iTouch 2.22 is listed under usb-wdm-nonfree's "Not shipped", no licence or redistribution terms being stated (usb-wdm-nonfree `3441899`).

## 47.11 swapping USB audio devices under VLC (uac2; worker wdm, 2026-11-06)

usb-wdm-free branch `w47-wdm` (from `main` `3942e20`): record
`docs/testing/2026-11-06-uac2-swap.md`, design `docs/design/uac2.md`
section 22, change `2dc6523`.

- **Rig:** XP x64 SP2 q35 overlays (`E:\xhci98-vm\a347\wdm\g1` golden,
  twins `t1`, `t2`), 2 vCPU, usb4-emu fork emulator `cb36497`; xHCI98
  3.4.1.0 released `nt-x64` (`xhci98.sys` `2be0622b...`, `INSTALL.BAT
  /console`, exit 3, cold start); `uac2` 0.1.8.26 from `free/` (`UACCI.INF`,
  `uac2wdm.inf` staged by `SetupCopyOEMInf`; `uac2wdm.sys` `9b74218f...`).
  Real devices of development host A passed through by `hostbus`/`hostaddr`:
  Sound Blaster X4 (`041E:3278`) and X1 (`041E:3286`) under `uac2`, Logi
  USB Headset (`046D:0A64`) under `usbaudio` (the control); simulated
  CX31993 with a MAX97220 (`uacemu.py --profile cx31993m`). VLC 3.0.11
  win32 (`fac5ae20...`), DirectSound and waveOut.
- **One device at a time (a1-a8, b9-b12): not reproduced.** In all 12
  swaps the new device played after VLC's Stop and Play without a VLC
  restart, `uac2` and `usbaudio` alike; right after a swap nothing played
  with either driver (VLC keeps its output on the gone device until it
  reopens it).
- **Two devices attached (d1-d3): the difference.** The Logi attached
  after the X1 became Windows' default at once; the X1 attached after the
  Logi did not. A default changed by hand was not followed by the running
  VLC at Stop and Play in either direction; a VLC restart followed it. So
  with another sound device present, a `uac2` device does not take the
  sound at install, a UAC 1.0 device does, and following a manual choice
  needs a VLC restart: the likely reading of the report.
- **Cause:** Windows' own USB audio INF writes `SetupPreferredAudioDevices`
  (1, no-clobber); `uac2wdm.inf` did not.
- **Change (`2dc6523`, `DriverVer` 11/06/2026,0.1.9.0 in `35bceb4`):** `HKR,,SetupPreferredAudioDevices,3,02,00,00,00`
  in `[AudioSupport]` of both INF copies (2: nonzero for 98 SE to XP x64,
  even so Vista's endpoint builder ignores it; design 22.3); INF gate
  `infchk.ps1` requires it (36 mutations refused); uac2 `test.cmd` PASS.
  No driver image changed.
- **The change on XP x64 (the branch's x64 INF byte for byte, `30b34655...`):**
  a new simulated device (p1), a duplex one (p2) and a two-pair one (p3)
  each became the default at install (p3: pair 0, the analog output; a
  VLC Default play streamed interface 4); the real X1 installed fresh (r1)
  became the playback and recording default, and a VLC playing on the Logi
  moved to the X1 at its next output open; control c1 (released INF) left
  the Logi the default; update u1 (installed X1 updated to the change)
  left the defaults alone. MIDI stayed the GS synth throughout.
- **Evidence:** `E:\xhci98-vm\a347\wdm\` - `sw.txt`, `dflt.txt`, `shots\`
  (`pref-p1-mmsys.png` ... `real-x1-mmsys.png`, `ctl-c1-mmsys.png`,
  `upd-2-mmsys.png`), the guests' COM1 logs, `ev\` captures, `pids.txt`.
- **98 SE (owner, 2026-11-06; simulators only, the real devices were with
  47.30):** a 98 SE guest (NUSB 3.3's EHCI + `USBHUB.SYS`, xHCI98 3.4.1.0),
  `pc`, 1 vCPU, emulator `cb36497`; `uacemu.py --profile hs24` as "UAC2 A"
  to "UAC2 D"; released INF (`D:\UOLD`) against the changed one
  (`D:\UNEW`); the KS files from the 98 SE CD's cabinets; the Multimedia
  panel read. Record: `docs/testing/2026-11-06-uac2-swap.md`, "The change
  on 98 SE".
  - Under xHCI98: B (released) left A preferred; D (changed, Have Disk)
    took playback and recording; B (released) updated to the changed INF
    kept the playback choice A (recording D became A after the next cold
    start); updating the preferred device A itself moved playback to D.
  - Under NUSB's EHCI: B (released) left A preferred; B updated to the
    changed INF kept A/A; D (new, installed at the next start with the
    changed INF) took playback and recording.
  - Control (owner, 2026-11-06), 98 SE under xHCI98, one read-only
    starting state (A then D, both released INF, A preferred), a fresh
    overlay per run: A updated with the released INF: D / D before the
    restart, A / A after a cold start; with the changed INF: the same.
    That move is 98 SE's own; the earlier run in which it lasted past the
    cold start (another starting state) stays unresolved. Each update paused about
    5 minutes at the wizard's "ready to install" page; three earlier runs
    were stopped at that page (clock still for 90 s or more), and one
    overlay stopped answering after a replug; kept in `ser\u\old\`.
  - Last reading (owner, 2026-11-06): step 6's state with D on the
    RELEASED INF (fresh overlay of the control's base): A / D chosen by
    hand -> cold start A / D; A updated to the changed INF -> D / D; cold
    start -> A / D. So D did not keep the role with the released INF,
    where in step 6 (D on the changed INF) it did (D / D after the cold
    start). A difference, one run each, with other differences in the two
    overlays' histories: the change may keep a device that got the role at
    install preferred across a later update's restart on 98 SE.
  - Repeat (owner's choice (c), 2026-11-06): steps 1 to 6 once more from a
    clean overlay (A, B, C released; D by Have Disk with the changed INF):
    D / D after D; A / D by hand; A updated to the changed INF (restart
    asked) -> A / D before the restart (read about 5 minutes after Finish)
    -> cold start A / D. **A / D: step 6's D / D did not reproduce.** The
    update reached A (registry: `MEDIA\0000`, `DriverDate` 11-6-2026,
    `SetupPreferredAudioDevices` 0). Differences from step 6 (product IDs,
    a phantom Unknown Device from a first attempt at A, B and C from
    `D:\UOLD`, the timing of the reading before the restart) recorded in
    the testing doc's new subsection. So decision (a) is not triggered.
  - 47.final release-notes line (draft, optional since it did not
    reproduce; the owner's to keep, reword or drop): "On Windows 98 SE,
    after you update the driver of the sound device chosen in Control
    Panel, Multimedia, open Multimedia (Audio tab) and check the preferred
    playback and recording devices: while the driver is replaced Windows
    may move them to another device (in our tests the choice came back at
    the next restart in four runs of five)."
  - 98 SE notes: later devices install from the one `INF\OTHER` copy, so
    the changed INF reaches an existing install only by Have Disk or
    Update Driver once; Windows sets the value to 0 after acting; updates
    ask for a restart. ME and 2000 not run (no prepared image with `uac2`).
  - Evidence: `E:\xhci98-vm\a347\wdm\ser\shots\ux-*`, `un-*`, `ue-*`,
    `ur-*`, `ser\pids.txt`.
- **Left:** the change on ME, 2000 and Vista; the usb-wdm-free release (0.1.9) and the xHCI98 pin; the owner's
  reading with a real CX31993 (the bench confirmation).

## 47.18 the Realtek ALC4080 through uac2 (worker wdm, 2026-11-06)

- **Linux read again** at master `f66469d9` (`sound/usb`, all
  GPL-2.0-or-later, facts only): one new ALC4080 entry, `0DB0:A073` with
  `QUIRK_FLAG_SKIP_IFACE_SETUP` (skips the probe-time alternate and rate
  setup); no start-up request, no jack or output selection for any
  ALC4080 id (the Realtek HD Audio verb requests stay the ALC4020 docks').
  `uac2` never does the probe-time setup (alternate 0 at start; rate and
  alternate only at stream start), so nothing beyond the class is needed
  for what Linux shows. Design `docs/design/uac2.md` 15.1b (`d14dab1`).
- **Leg, XP x64 under xHCI98 3.4.1.0, `uac2` 0.1.8.26:** the owner's
  capture replayed (`uacemu.py --profile alc4080cap --vid 0db0 --pid a073`):
  three playback and three capture devices ("ALC4080 capture - SIMULATED",
  "... 2", "... 3"); `UACWAV play` 2 ch 16-bit 48 kHz 5 s on each output
  pair: all buffers played, the simulator streaming interfaces 4, 5 and 6;
  `UACWAV rec` on each input pair: all buffers recorded, signal present
  (interfaces 1, 2, 3). Evidence `ev\alc-c4.jsonl`, `t1\serial-a.log`
  (`===AP0 c5` to `===AREND2 c5`).
- **Left:** 98 SE under NUSB's EHCI and xHCI98 and 7 x64 with the replay;
  two streams at once and the topology names on Vista and 7; the real
  board (45-E.13).

## 47.19 USB serial bridges in usbcom (worker wdm, 2026-11-06)

CH340/CH341 dropped (owner, 2026-11-06): WCH's driver, carried in usb-wdm-nonfree, covers 98 SE to XP.

- The CH340/CH341 module (design and code, both reviewed) was written and
  then removed from `w47-wdm` on the owner's decision; its two commits are
  kept as patches outside the tree. No CH340 leg counts.
- Kept: `eba39b5`, the usbcom INF gate's date mutation follows `DriverVer`.
  Since 0.1.5 the literal date it replaced is gone from `USBCOM.INF`, so
  `build.cmd w98 free|trace` failed at `main` ("Mutation did not change
  USBCOM.INF: bad date"); with the fix both flavours and the host tests
  (`acmhost` 200337 checks) pass. No driver file changed.
- Not run (owner, 2026-11-06: the time to 47.11 and 47.18): the chip legs on
  xHCI98 3.4.1.0. The 2026-10-29 legs (`docs/testing/2026-10-29-usbcom-chips.md`:
  xHCI98 3.2.0.0 and NUSB 3.3's EHCI, every simulated part, QEMU's FT232BM)
  stand.
- Makers' drivers (usb-wdm-free `docs/design/serial.md` section 2 and
  usb-wdm-nonfree's README; the makers' current pages could not be read
  from development host A this session): FTDI's VCP 1.09.06/1.0.3 (98/ME:
  FT232AM/BM/R, FT2232D) and CDM 2.06.00 (2000), in usb-wdm-nonfree for 98
  SE to 2000; no FTDI driver for FT-X, FT232H or FT4232H on 98 SE or ME
  (usbcom's modules); WCH CH341SER 98/ME to 11, in usb-wdm-nonfree for 98
  SE to XP; Silicon Labs CP210x and Prolific PL2303: their licences forbid
  passing them on ("Not shipped"); Prolific's own 2.0.0.19 for 98 and ME (older PL2303 parts) exists as an
  archived copy of Prolific's download, not carried for its EULA; no
  official 98/ME driver for CP2105, CP2108 or the newer PL2303 parts
  (usbcom's modules).
- Left: real chips (45-E.14), ME under an EHCI stack, the NUSB 64 KiB IN
  loss, the chip legs on xHCI98 3.4.1.0.

## 47.22 Stor98's VERIFY reads the medium (2026-11-06)

The task (ST-11 of the audit of 2026-11-06; owner, the same day, "doing
things properly", in two steps, the design first): until now VERIFY(10)
and VERIFY(16) refused a range past the end and a byte-compare or
protection request, and any other VERIFY succeeded without a block read,
so a surface scan found no bad block.

### The design

Reviewed and revised in four rounds before any code, written for the
engine as task 47.20 leaves it; re-read against the merged engine
(`6d7eb788`): its engine, `rmw.h`, bounce and edge pages and overlap rules
are those the design was read against, so it stood unchanged. Stor98 PLAN,
"VERIFY", holds it:

- **VERIFY(6)** (13h, a sequential-access command): CHECK CONDITION
  05/20/00, answered in software (it was GOOD).
- **VERIFY(10) and (16)**: the CDB checks as before; more than 65535
  sectors (32 MiB) refused 05/24/00 before any work. Otherwise an engine
  parent of the READ kind marked as a verify: no data buffer, nothing
  mapped or copied, the whole native blocks the range touches, its overlap
  range set as a READ's; the read counters of the counter answer leave it
  out.
- **Step 1, NVMe Verify** (opcode 0Ch, FUA, no data pointer) where the
  controller has it: Identify Controller ONCS bit 7, or a nonzero Verify
  Size Limit from CNS 06h, a new optional bring-up command (a refusal
  changes nothing; no answer fails that bring-up through the admin
  time-out, and the probe is skipped after it).
- **Step 2, reads into the bounce pages** elsewhere: FUA reads of at most
  32 KiB a child into the eight general bounce pages, nothing copied.
- **Errors** as a READ's: an unrecovered read error 03/11/00 MEDIUM ERROR,
  no Information field (a child covers many blocks).
- **Time, accepted**: the work is bounded (32 MiB), the time is not proved;
  in the polling mode a VERIFY of that size by reads can outlive a 10 s
  NT time-out and then ends through the ordinary reset path, never
  truncated to GOOD.

### The code

`fd30adde` on branch `w47-vfy` (from `a0e9eae9`), reviewed and revised in
three rounds: `vfychk.h` (the decode, the bound, the native range, the VSL
decode and arithmetic, the child sizes; `vfychk_ut`), `engine.c`
(`EngSubmitVerify`, `EngIssueVerifyChild`, the counter guard and stages),
`nvme2k_cpl.c` / `nvme2k_nvme.c` / `lifecycle.c` (ONCS, the CNS 06h probe,
`SkipIocsProbe`), `nvme2k.c` and `nvme2k_scsi.c` (the dispatch), the
test switches `STOR98_TEST_VFYREAD` and `vfyread=`, debug-port stages
`0x1A0` to `0x1A3`; Stor98 README, PLAN and TESTING.

### The legs (2026-11-06)

Development host A, QEMU TCG, the q35 bases' pinned emulator
(`asm4242-20261025-2230`), `launch-q35.ps1 -Usb4 asm4242 -EncAtBoot` (the
ORICO model tunnelled at power-on), QMP on a unix socket. The package: the
gated `release` builds of `fd30adde` ("BUILD + GATES PASSED (release)",
x86 and amd64; `stor98.mpd` SHA-256 `43e3406b...`), cut as an x86 TEST
upload set and installed by `XHCIINST.EXE /dir 98-me|nt-x86 /yes /nopause
/thunderbolt /stor98` (exit 3) on fresh overlays. Test images of the same
source: a debug-port build (stages; SHA-256 `3cd8e4b7...`) and one with
`STOR98_TEST_VFYREAD` (VERIFY by reads; `107f3351...`). QEMU's NVMe trace
(`pci_nvme_read`, `pci_nvme_verify`, `pci_nvme_err*`) where named.

**What the emulator's drive answers.** Stage `0x1A0` at every bring-up:
`5d070000` - CNS 06h answered (status 0), Verify Size Limit 7 (512 KiB),
ONCS `5Dh` with bit 7 clear. So the emulator offers NVMe Verify only
through the size limit, as upstream QEMU does, and step 1 runs there by
the CNS 06h path.

**98 SE** (`E:\xhci98-vm\a347\stor\s98v\`):

| Step | Result |
|---|---|
| ScanDisk, thorough, with write-testing | 2082 READ(10) and 6184 WRITE(10) reached Stor98, **no VERIFY**; "ScanDisk did not find any errors" |
| ScanDisk, thorough, no write-testing | 2084 READ(10), no VERIFY: ScanDisk on 98 SE scans by reads, so this change does not touch it |
| `format E: /u /autotest` (MS-DOS FORMAT in a DOS box), debug image (NVMe Verify) | 8679 VERIFY(10) reached Stor98, each 32 sectors, each one NVMe Verify child (`0x1A2` opcode 0Ch, 32 blocks), GOOD in 0 to 2 ms (`0x1A3`); FORMAT verifies 32 sectors of every 64 in runs (trace: Read/Verify LBAs `0x7F`, `0xBF`, ...) |
| The same with the read image (step 2) | each VERIFY one Read child into the bounce pages (`0x1A2` opcode 02h, 32 blocks), GOOD, 0 to 2 ms |
| The same, a read error on sector 18570 (`blkdebug` under a raw copy of the drive), read image | NVMe Unrecovered Read Error (`pci_nvme_err_aio ... status 0x281`), stage `0x8C` `04000281`, sense kept `03/11/00` (`0xD7` `0e001103`), the VERIFYs failed (`0x1A3` bit 28); FORMAT "Trying to recover allocation unit 2,133" and on: the bad area found |
| The enclosure unplugged 25 s into a FORMAT | the gone handling (stages `0x8B`, `0x46`, `0x121`, `0xCF`), no fault; the guest kept running; the enclosure reconnected ("ASMedia 246x connected"), the drive not back while the FORMAT window stayed open; the following shutdown hung and was ended by QMP |

Found on the way, not this change: MS-DOS FORMAT on that partition goes on
past its end after about a quarter of its progress, and Stor98 refuses
those READs and VERIFYs alike as LOGICAL BLOCK ADDRESS OUT OF RANGE
(05/21/00, tens of thousands of retries); the READs go through the READ
path's own range check, which 47.22 does not change, so this is FORMAT's view of
the partition, to be read separately (not compared with an older image).

**7 x86** (`E:\xhci98-vm\a347\stor\w7v\`): installed (the unsigned-driver
prompts accepted), restart, Stor98 on the tunnelled drive (D:, `StorInterrupt
5.0.0` "... interrupt"). A full NTFS format of D: ("Format complete") and
`chkdsk D: /r` ("Windows has checked the file system and found no
problems", 0 KB in bad sectors); with the trace on, a format and `chkdsk /r`
gave 955 NVMe Verify commands (922 of 256 blocks, 29 of 64, 2 of 8, 1 of
72) and 938 reads, no error.

**Not run (owed):** ME (ScanDisk and FORMAT, as 98 SE), 2000 and XP (a
full format and `chkdsk /r`), VERIFY(6) from a pass-through tool, a
VERIFY(10) of 65535 sectors in the polling mode, VERIFY on a 4 KiB
namespace in the 512-byte view (98 SE to 7), the removal leg's return of
the drive after the FORMAT window is closed, and the CNS 06h probe's failure
cases that TESTING names: a refusal, and no answer (the admin phase's time-out
and disable, the utility buffer not reused early, `SkipIocsProbe` kept for
the later bring-ups).

## 47.31 Stor98 shows a 4 KiB namespace with native 4096-byte sectors on 8, 8.1 and 10 (2026-11-06)

The task (owner, 2026-11-06, option B): on Windows 8, 8.1 and 10 a 4 KiB
namespace is shown with its native 4096-byte sectors, as Windows' own
stornvme does, so a volume made there by stornvme stays readable when Setup
puts Stor98 on the drive; 47.20's 512-byte translation kept on 98 SE to 7
and for 1 and 2 KiB namespaces everywhere.

**The design**, reviewed and revised in four rounds before the code (the
scratch file `E:\xhci98-scratch\w47\stor\design-4731.txt`, revision 4;
Stor98 PLAN, "4 KiB-sector namespaces", "Two views"): the view is fixed with
the geometry at Identify Namespace, each start; the NT build reads the
running Windows' version from KUSER_SHARED_DATA (`NtMajorVersion` 0x26C,
`NtMinorVersion` 0x270, the fixed kernel address Stor98's clock already
reads), 6.2 or later and LBADS 12 choosing the native view; the 9x build
never does. The count is gated in the chosen view's sectors. READ CAPACITY,
MODE SENSE, READ/WRITE (no edges with shift 0: no read-modify-write on the
native path), VERIFY (32 MiB in one VERIFY: 8192 sectors of 4096), the
overlap rules and the ATA IDENTIFY words follow the view. The two views are
not interchangeable, and Stor98 neither detects nor converts that.

**The code**: `8679b3c9` on `w47-vfy`, reviewed and revised in three rounds;
host tests `rmw_ut` ("view") and `vfychk_ut`. Gated `release` builds x86 and
amd64 "BUILD + GATES PASSED (release)" (`stor98.sys` x86 SHA-256
`2f1f1fcd...`), an x86 TEST upload set (`xhci98-3.4.1.0.zip` SHA-256
`50034f85...`).

### The legs (2026-11-06)

**8.1 x86** (`E:\xhci98-vm\a347\stor\w81v\`): a fresh overlay of the q35
8.1 x86 base, `-Usb4 asm4242 -EncAtBoot`, the tunnelled drive's namespace
4 KiB (`-global nvme.logical_block_size=4096 -global
nvme.physical_block_size=4096`), QEMU's `pci_nvme_read`/`pci_nvme_write`
trace on.

| Step | Result |
|---|---|
| stornvme on the drive (before the package): diskpart clean, a primary partition, NTFS quick format (V4K, T:), two files written and compared | `fsutil fsinfo ntfsinfo T:` "Bytes Per Sector : 4096", "Bytes Per Physical Sector : 4096"; `fc /b` clean; the trace: 4096-byte commands |
| The package installed (`XHCIINST.EXE /dir nt-x86 /yes /nopause /thunderbolt /stor98`, exit 3; the unsigned-driver prompts accepted), restart | `driverquery`: stor98 running; the drive's commands now carry Stor98's command identifiers (`cid 32832` ... in the trace) |
| Under Stor98: the stornvme-made files compared; two files written through Stor98 and compared | "Bytes Per Sector : 4096"; `fc /b` clean on all four; the trace: one-block reads of 4096 bytes (NLB 1 counting from 1, at native LBAs) and 256 KiB writes, no read-modify-write pattern |

**Not run (owed):** the drive back on stornvme after Stor98, 10, 7 with
the 4 KiB namespace (the 512-byte view kept), the frag=64 split, the raw
one-block write, the last LBA and one past it, and the replug, restart,
resume and reset comparisons of the design's N7.

## 47.12 The P14s's EDD reading, the XHCIQUAL guard, and the request for reports (2026-11-06, w47-edd)

The owner's decision on record 37's Q2 (2026-11-06): gather data first,
take the emulator feasibility readings, write a request for field
reports, and build no production `XHCII13.VXD` in this phase.

### The P14s reading (Q1) [obs]

`temp\p14sg1-boot\EDD.LOG` (stick in) and `EDD0.LOG` (stick out), held on
the maintainer's disk and not published, as bare-metal runs are (`.gitignore`,
`xhciqual/results/`); XHCIQUAL
3.4.1.0 `--edd` at "Command prompt only", BIOS `N2XET46W (1.36)`, DOS 7.10
"not under Windows":

| Unit | Stick in | Stick out |
|---|---|---|
| 80h | 41h AH=30h BX=AA55h CX=0005h; 48h 4Ah buffer: size word 004Ah, key BEDDh, length 2Ch, "PCI " 2E:00.0 (1C5C:174A, 010802), interface "SATA", checksum PASS; 42h buffer: size 1Eh, no key | the same |
| 81h | 41h the same; 48h 4Ah buffer: size word 004Ah, key BEDDh, length 2Ch, "PCI " 00:14.0 channel 00h (8086:02ED, 0C0330, in the xHCI scan), interface "USB", device path 38h-47h all zero, checksum 9Fh, PASS; 42h buffer: size 1Eh, no key; "named as USB on the xHCI at 00:14.0" | 41h CF=1 AH=01h: absent |
| 82h-87h | absent | absent |

So this firmware names its USB disk positively by record 37 section 4.1's
checks, to the 74-byte request only (the 66-byte request gets no path);
the device path's USB serial field is zero, so on this machine nothing
would tell two sticks apart (record 37 never decides on it). The system
NVMe disk reports interface "SATA" with a valid checksum. Unit 81h is
absent without the stick (the BIOS data area's count 2 then 1).

### XHCIQUAL under Windows (no change)

Every real-mode interrupt XHCIQUAL makes goes through `sysprobe.c`'s
`rm_int_err` (DPMI 0300h). Its callers ask INT 15h (E820), INT 21h and INT
2Fh, and `op_int13` alone asks INT 13h; `op_int13` is reached only through
`edd_dos_ops`, which only `run_edd` uses, which `main` calls only with
`--edd`. `edd_run` asks INT 2Fh 1600h and INT 21h 3306h first and returns
exit 2 under Windows' MS-DOS Prompt and Windows NT's MS-DOS before any INT
13h (read in 47.24's leg E2 a). The quick scan, `--probe-only` and the
full run make no INT 13h call, and none of the other DPMI uses (0204h and
0205h for INT 24h, 0100h and 0101h, 0400h, the PCI and memory services)
reaches the disk BIOS. So no path calls INT 13h 41h or 48h inside a
Windows DOS box, and no guard was added.

### The emulator feasibility readings: not taken

Not taken in this run: no prototype was built and no guest was started.
Record 37's readings (sections 4.3, 4.4, 5.1, 5.2 and 5.4) stay owed.

### The request for reports

`docs/using/edd-reports.md` (owner, 2026-11-06: tracked under `docs/`),
linked from the README's 98 SE and ME start-up limitation and from
`docs/README.md`: `XHCIQUAL --edd` from MS-DOS after a cold start, with
the stick present and then absent; the model, BIOS version and supplier,
the BIOS's USB setting, and whether the computer stops at start-up after
the takeover; reports wanted from many firmware families, failures as
much as successes.

## 47.21 (the cleanup legs) the firmware's tunnel on Alpine and Cactus Ridge, each value with an unplug and a plug again (2026-11-06)

Worker apple, the 47.21 kit and build as in "47.21" (`f6835ba2`, the x86
kit `E:\xhci98-vm\a347\apple\kit\xf\PKG`, `usb4-emu` `2ca94a1`), the same
two installed 7 x86 overlays (`ar-w7x86\leg-a.qcow2`,
`cr-w7x86\leg-a.qcow2`) cold-started with `preos-cm=on` and the trace of
the link controller's port reset (`usb4_port_dpr`), the enclosure on
port A at power-on. Each value set by `INSTALL /set
XhciCmPreOsCleanup=<n>` and a restart (the emulator's firmware tunnels
the enclosure again at the restart, on the port it is then on), then
`R.BAT`, then `kit\replug.sh`: QMP `device_del` of the enclosure,
`R.BAT`, `device_add` on port B, `R.BAT`. These legs complete what
"47.21" read (Alpine Ridge value 0 with its replug there, p0).

| Host | Value | At the start (`R.BAT`, the trace) | Unplug, then port B |
|---|---|---|---|
| Alpine Ridge (`c2`, cold start) | 2 | the tunnel cleaned and rebuilt **twice** (`usb4_port_dpr` adapter 1 set and clear, twice), "PCIe up", D: ORICO | D: gone; "port 2", PCIe up at `02:02.0`, D: ORICO |
| Alpine Ridge | 1 (a restart) | no tunnel transition after the OS took over; "PCIe up (found at start)", port 2, D: ORICO | D: gone; port 2 again, PCIe up at `02:02.0`, D: ORICO |
| Cactus Ridge, a Port Ridge device (`c2`, cold start) | 2 | the tunnel down and up **twice**, no port reset (generation 1); "PCIe up", one lane; Stor98's controller started but **no disk under it and no D:**, read again a minute later the same | D: absent; port B: "PCIe up" at `02:02.0`, Stor98, D: ORICO |
| Cactus Ridge | 1 (a restart) | no transition; "PCIe up (found at start)", port 2, D: ORICO | D: gone; port 2 again, PCIe up at `02:02.0`, D: ORICO |
| Cactus Ridge | 0 (a restart) | no transition; "PCIe up (found at start)", port 2, D: ORICO | D: gone; port 2 again, PCIe up at `02:02.0`, D: ORICO |

Every value read with its unplug and plug again on both hosts (Alpine
Ridge value 0 in "47.21"); 20 devnodes Code 0 each time. Values 0 and 1
pass on both. Value 2 cleans as designed, and its double cleaning on a
cold start (47.32's subject) shows on Cactus Ridge as a lost disk until
the next plug: the second cleaning removed the endpoint Stor98 had just
started on, and no disk came back under it. 47.32 (below) is the fix.
Evidence: `ar-w7x86\` and `cr-w7x86\` (`serial-c2.log`, `trace-c2.log`,
`rep-c2*.txt`, `rep-c1*.txt`, `rep-c0*.txt`, `c21.png`), `pids.txt`.

## 47.32 the pre-OS cleaning once a boot (2026-11-06)

Worker apple, branch `w47-pre2` (from `4f50ebfd`), tree
`E:\xhci98-wt\w47-pre2`. The finding of "47.21": with
`XhciCmPreOsCleanup` 2, a cold start of 7 x86 cleaned the firmware's
tunnel twice, Windows stopping and starting the NHI's stack once and the
new instance cleaning again the tunnel its predecessor had just rebuilt
(on Cactus Ridge the drive was then lost until the next plug, "47.21 (the
cleanup legs)" above).

### The design and the owner's option

Record 22 section 9.3's "Once a boot" and section 9.3.1 (`d41bf07e`,
reviewed and revised over four rounds). The start is left whole or
cleaned whole, never one port beside a cleaned one: left whole when (1)
the handed-over record (section 9.4) carries `Cleaned`, handed on only by
an instance that ran at value 2; (2) no port is HELD or owes a sweep, and
every tunnel is its own; (3) a read-only inventory of the host's adapters
of each such port and of the whole depth 1 router finds only the owned
tunnels' entries and enables, nothing pending or unreadable; (4) no
chained link or recorded child. Otherwise as before, the version 2 Host
Router Reset and a half-built child included. Bounded by the record (120
s, a surprise removal, a reboot). The owner took option (a) on
2026-11-06 (the record's flag and the four tests, no new storage).

### The code

`2940fbe8` and `bcc02f95` (reviewed and revised; the first gated x86
build failed at `cm_nhi.c`'s assertion that the counter names match the
counter block, so the counter was taken out and the log line kept):
`tbt_u4cm.h` (`TBT_U4_CARRY.Cleaned`, `TBT_U4CM.PreOsCleaned`),
`tbt_u4cm.c` (`tbtU4OnceAdapter`, `tbtU4OnceEnable`, `tbtU4OnceInventory`,
`tbtU4PreOsOnce`, `TbtU4Start`, `TbtU4GetCarry`), log `u4.preos.once` (1
left whole, else the failing test in bits 15:8 and its port in 7:4).
Host vectors `test_u4cm` `test_preos_once`: left whole after A at 2; A at
0, 2 then 1 then 2, a recorded kid, an extra entry on host lane 1, a
pending entry on the device's downstream lane each cleaned. `test_u4cm`
1950 checks, the host tests passed.

### Builds

The gated amd64 build of `bcc02f95` (`release -amd64`): "BUILD + GATES
PASSED (release)" (`E:\xhci98-scratch\w47\apple\gbp-amd64-ok.log`); the
gated x86 build (`all`): "BUILD + GATES PASSED (release qemu)"
(`gbp-x86.log`). `xhcicm.sys` x86 `d85a8517...`, amd64 `edbed247...`
(`hashes-4732.txt`). Kits: `kit\xf2\PKG` (x86 family layout and tools),
`kit\xf642\PKG` (amd64, catalogs unsigned, signed in the guest).

### The legs (7 x86, Alpine Ridge with the firmware's tunnel, a fresh overlay `ar-w7x86-p`)

| Leg | Value, start | Read | Verdict |
|---|---|---|---|
| a | 0, the install's warm restart | 20 devnodes Code 0; "PCIe up (found at start)", D: ORICO | preserved |
| p2 | 2, **a cold start** | the trace: the firmware's tunnel up, the OS took over, the tunnel down (admin), `usb4_port_dpr` adapter 1 set and clear, up again - **once**; "PCIe up (found at start)" (the restarted stack left whole what its predecessor rebuilt); XHCITR's report: `u4.carry.in=1`, `u4.preserved.own=00010001h`, `u4.preos.once=1`; D: ORICO; unplug, port B: PCIe up at `02:02.0`, D: ORICO | the double cleaning gone: pass |
| p1 | 1, a cold start | no transition after the OS took over; "PCIe up (found at start)", D: ORICO; unplug, port B, D: ORICO | regression: pass |
| p0 | 0, a cold start | the same | regression: pass |

The same overlay on the 47.21 build ("47.21 (the cleanup legs)", `c2`)
cleaned twice on its cold start.

### The legs (10 x64, Alpine Ridge with the firmware's tunnel, a fresh overlay `ar-w10x64-p`)

The x64 kit signed in the guest by `SIGN64.BAT` (12 files signed and
verified, test mode on), installed (exit 0 from the kit's wrapper; the
installer's own exit 3), value 2 set by `INSTALL /set`, a shutdown.

| Leg | Value, start | Read | Verdict |
|---|---|---|---|
| p2 | 2, a cold start | the tunnel cleaned once (down, `usb4_port_dpr` adapter 1, up); 14 devnodes Code 0; "PCIe up", Stor98, E: ORICO; XHCITR's report: `u4.carry.in=0`, `u4.preos.once=00000100h` (test 1: no record - 10 x64 did not restart the stack, so the first start cleaned as before); unplug, port B: PCIe up at `02:02.0`, E: ORICO on `stornvme` | value 2 still cleans a first start: pass |
| p1 | 1, a cold start | no transition after the OS took over; "PCIe up (found at start)", E: ORICO | regression: pass |

### Verdict

The double cleaning is gone on the cold start of 7 x86 (one cleaning,
the restarted stack left whole, `u4.preos.once=1`); a first start still
cleans at value 2 (10 x64); values 0 and 1 are unchanged (7 x86 cold
starts, 10 x64 value 1). Not read in the emulator: a version 2 host's
Host Router Reset beside a left port (the rule never leaves one; the
host vectors hold the cases that clean whole), a chained child, and
Cactus Ridge on this build. Evidence: `E:\xhci98-vm\a347\apple\
ar-w7x86-p\`, `ar-w10x64-p\` (`serial-*.log`, `trace-*.log`,
`rep-*.txt`), `pids.txt`, `hashes-4732.txt`.

## 47.22 (the legs owed)

The legs 47.22 left (`runs/run-47.md`, "47.22", "Not run (owed)"), on
the merged Stor98 of `b126cefc` (its Stor98 source is `8679b3c9`'s, so
the gated `release` package of `8679b3c9` was used: `stor98.sys` x86
SHA-256 `2f1f1fcd...`, `stor98.mpd` `69f867c1...`), branch `w47-stor2`.

**The vehicle.** Development host A, QEMU TCG, the q35 bases' pinned
emulator (`asm4242-20261025-2230`), `launch-q35.ps1 -Usb4 asm4242
-EncAtBoot` (the ORICO model tunnelled at power-on, its 128 MiB FAT test
disk), QMP on a unix socket; fresh overlays of the clean q35 bases; the
package installed by `XHCIINST.EXE /dir 98-me|nt-x86 /yes /nopause
/thunderbolt /stor98` (exit 3, then a restart). QEMU's NVMe trace
(`pci_nvme_read`, `pci_nvme_verify`, `pci_nvme_err*`) where named.
Test images of the same source, never staged: an NT x86 debug-port build
(`stor98.sys` SHA-256 `793e3e03...`; `vfyread=` and the stages) and a 9x
one (`stor98.mpd` `2259acd7...`). The guest tool `tests/vfyblk`
(`1cca08a3`, `VFYBLK.EXE` SHA-256 `b1a7f8a7...`, rebuilt after review as
`2a8e5df8...` for the later legs: the same commands).

**XP** (`E:\xhci98-vm\a347\stor2\xpv\`, `-Os xp`, two processors): the
package installed (its hardware wizards and unsigned-driver prompts
answered), a restart; `driverquery` stor98 running, the tunnelled drive
`PhysicalDrive2` ("QEMU NVMe Ctrl SCSI Disk Device"), D:.

| Step | Result |
|---|---|
| `chkdsk D: /r` on the FAT16 volume as found | "Windows has checked the file system and found no problems"; 516 NVMe Verify commands (496 of 512 blocks, 19 of 280, 1 of 216), no error |
| A full NTFS format of D: (`format D: /FS:NTFS /V:T /X`, "Verifying 127M") | "Format complete", 131040 KB; 512 NVMe Verify commands from LBA 63: the whole partition |
| `chkdsk D: /r` on the new NTFS volume | stages 1 to 5, "0 KB in bad sectors"; 512 NVMe Verify commands more (stage 5, free space) |
| `vfyblk 2 edge` | 0 failed: READ CAPACITY(10)/(16) last LBA 262143, 512 bytes, exponent 0; READ(10)/(16) of the last block GOOD, past it 05/21/00; VERIFY(10)/(16) of the last block GOOD; two blocks from the last and one past 05/21/00; a count of 0 GOOD; **VERIFY(6) 05/20/00**; BYTCHK 05/24/00; VERIFY(16) of 65536 sectors 05/24/00; the raw one-block WRITE(10) of the last block written, read back, restored; WRITE(10) past the last 05/21/00 |
| VERIFY timings, release image, interrupt (`vfyblk 2 v10/v16 ...`, pass-through time-out 10 s) | VERIFY(10) of 65535 sectors at 0 and at 65535, VERIFY(16) of 65535 at 1 and of 8192 at 0: GOOD in 0 to 31 ms each (NVMe Verify) |
| The debug-port image, `storpollmode=1` and `vfyread=1` (polling, VERIFY by reads), a restart | **a 65535-sector VERIFY outlives the 10 s time-out**: `DeviceIoControl` error 1117 after 9.2 to 13.9 s for each of the three; stage `0x1A3` (each: failed, after 635, 691 and 651 of its 1024 children, 9598 to 10014 ms), each followed by a new bring-up (`0x1A0`), the drive working after it; a VERIFY(10) of 65530 sectors at LBA 3 (`VFYBLK.EXE 2 v10 3 65530 to=60`) with a 60 s time-out GOOD in 15.5 s (`0x1A3` `04003cac`: 1024 children, 15532 ms); 8192 sectors GOOD in 5.2 s |
| The same in polling with NVMe Verify (`vfyread=` removed) | GOOD in 45 to 62 ms each (`0x1A3` `2080002d`: 128 NVMe Verify children of 512 blocks, 45 ms) |

So the polled 32 MiB VERIFY by reads takes about 15.5 s on XP in the
emulator, as PLAN "VERIFY" ("Time, accepted") predicts, and with the 10 s
time-out it ends through the port's time-out and the reset path, failed
(error 1117 to the caller, never GOOD), the drive back after the reset.
With NVMe Verify, or with an interrupt, it is far inside the time-out.

**The CNS 06h probe's failure cases** (TESTING "VERIFY reads the medium",
harness rows), XP with the NT debug-port image, usb4-emu's fault rules on
the tunnel port (`qom-set /machine/peripheral/tp0 x-faults ...`), each
taking effect at the next bring-up, made by unplugging the enclosure and
plugging it back (QMP `device_del` / `device_add`). The bring-up's admin
order is Identify Controller, CNS 06h, Identify Namespace
(`nvme2k_cpl.c`), so the probe is the second identify:

| Rule | Result |
|---|---|
| `at=complete op=identify nth=2 act=none status=0x2` (the probe refused, Invalid Field) | `0x1A0` `5d000002` (status 2, VSL 0, ONCS `5Dh`); the bring-up went on to Identify Namespace (`0x42` with its command ID 4, then `0x49`) and the disk came back (`PhysicalDrive2`); a VERIFY(10) of 64 sectors GOOD, by a read (`0x1A1` bit 16 clear, `0x1A2` opcode 02h) |
| `at=fetch op=identify nth=2 act=drop` (the probe never answered) | `0x42` for command IDs 1 and 2 (the queues) and 3 (Identify Controller), none for 7 (the probe), then BUSY for the waiting SRBs, then `0x44` reason 4 (admin time-out), `0x45` (the disable acknowledged), the adapter `failed`; the harness saw two identify commands only (`rule0=2/1`): no Identify Namespace was issued, so nothing reused the utility buffer before the disable; no disk |
| Rules cleared, the enclosure replugged | a new adapter instance (`0x01`), the probe asked again (`0x1A0` `5d070000`), the disk back: on NT a replug is a new device extension, so `SkipIocsProbe` (kept within one extension) is not seen there |

`SkipIocsProbe` across bring-ups of one device extension is not shown:
after the failed bring-up the adapter stays `failed` (`HwResetBus` there
completes what was never issued but starts no bring-up), and in the emulator every later bring-up came with a new
adapter instance (`0x01`), on NT's replug as on 98 SE's reconnect (the
removal leg below), so no second bring-up of the same extension followed.
It needs a test hook (a bring-up repeated in one extension) or a host unit
test of the admin chain; owed.

**2000** (`E:\xhci98-vm\a347\stor2\w2k\`, a fresh overlay of the q35 2000
base, one processor): the package installed (exit 3; the hardware wizards
and signature prompts answered), shut down and started cold. The drive's
FAT volume could not be formatted from the command line (2000's `format`
refused every answer to "Enter current volume label", even after `label`
had set or cleared it), so the drive was given the test disk with its
partition's boot sector cleared (an unformatted partition, E:).

| Step | Result |
|---|---|
| A full NTFS format of E: (`format E: /FS:NTFS /V:T /X`, "Verifying 127M") | "Format complete", 131040 KB; 512 NVMe Verify commands (511 of 512 blocks and one of 449, from LBA 63): the whole partition |
| `chkdsk E: /r` | stages 1 to 5, "0 KB in bad sectors"; 512 NVMe Verify commands more (free space); no NVMe error |

In an earlier session on another overlay (lost to the host's power cut
and recreated) a replug of the enclosure on 2000 left no E: volume
(`mountvol` listed C: and D: only); not investigated, and not repeated on
the fresh overlay.

**ME** (`E:\xhci98-vm\a347\stor2\me\`, a fresh overlay of the q35 ME
base, `-Os me`, one processor; the package installed with RI9.BAT, exit 3,
shut down and started cold; the drive the test disk with its boot sector's
BPB set to Windows' 64 x 32 (see "The MS-DOS FORMAT reads past the
partition's end"); QEMU's NVMe trace on):

| Step | Result |
|---|---|
| ScanDisk, thorough, E: (Windows' ScanDisk, the default thorough options) | "ScanDisk did not find any errors on this drive", "0 bytes in bad sectors"; about 4,130 NVMe reads, **no VERIFY**: ME's ScanDisk scans by reads, as 98 SE's |
| `format E: /u /autotest` (MS-DOS FORMAT in a DOS box) | 2730 VERIFY(10) of 32 sectors at stride 96 from LBA 127 to 262111, each one NVMe Verify of 32 blocks, no error; FORMAT finished; the new FAT marks no cluster bad |

**Not run (owed)**: `SkipIocsProbe` over a second bring-up of one device
extension (above).

**VERIFY on a 4 KiB namespace in the 512-byte view, 7 x86**
(`E:\xhci98-vm\a347\stor2\w7k\`, `-Os w7x86`, the tunnelled drive's
namespace 4 KiB: `-global nvme.logical_block_size=4096 -global
nvme.physical_block_size=4096`; the release package; the test disk's FAT
volume at sector 63 seen as before through the 512-byte view):

| Step | Result |
|---|---|
| `wmic diskdrive` | disk 2 "QEMU NVMe Ctrl SCSI Disk Device", 512 bytes per sector |
| `vfyblk 2 edge` | READ CAPACITY(16) last LBA 262143, 512 bytes, **exponent 3**; every check passed but the raw write, which Windows itself refused while a volume covered the block (`DeviceIoControl` error 5 before any command reached Stor98; the restore refused the same way, nothing written) |
| A full NTFS format of D: and `chkdsk D: /r` | "Format complete"; "Windows has checked the file system and found no problems", "0 KB in bad sectors"; 955 NVMe Verify commands, each over whole 4096-byte blocks: 922 of 33 blocks (a 256-sector VERIFY at an LBA not on a block boundary touches 33 blocks), 29 of 9, others of 1, 2 and 13; no error |
| `diskpart` clean of disk 2, then `vfyblk 2 edge` again | 0 failed: the raw one-block WRITE(10) of the last 512-byte sector (a read-modify-write of native block 32767) written, read back and restored; VERIFY of the last sector one NVMe Verify of block 32767 (`slba 0x7fff`, one block) |

**The drive's return after a removal during a format, 98 SE**
(`E:\xhci98-vm\a347\stor2\s98\`, the diagnostic image of "The MS-DOS
FORMAT reads past the partition's end" below, which is the debug-port
build of this source with two extra stages): `format E: /u /autotest` on a
fresh copy of the test disk; the enclosure unplugged 25 s in, after
FORMAT's 1023 VERIFYs inside the drive, while its refused VERIFYs past the
end ran. The gone handling (stages `0x8B`, `0x46` with nothing
quarantined, `0x121`, `0xCF`), no fault; FORMAT ended by itself (not in
Close Program 40 s later). The enclosure plugged back 2 minutes after the
removal: a new Stor98 instance (`0x01`), the bring-up, CNS 06h answered
(`0x1A0` `5d070000`), running (`0x40` `00000102`); `dir E:` lists the
volume (ORICO, `TESTDATA.BIN`). Earlier in the same session a replug with
no format running brought E: back the same way. So the drive comes back
once FORMAT has ended (47.22's leg had kept the FORMAT window open and
saw no drive meanwhile).

## 47.31 (the legs owed)

The legs 47.31 left (`runs/run-47.md`, "47.31", "Not run (owed)"), the
vehicle of "47.22 (the legs owed)" above, the release package of
`8679b3c9` (the Stor98 source of `b126cefc`), the tunnelled drive's
namespace 4 KiB (`-global nvme.logical_block_size=4096 -global
nvme.physical_block_size=4096`). 10 is the 32-bit 10 (no 64-bit package
in the kit), its q35 base (`-Os w10x86`, Windows 10 22H2 19045).

**10 x86, the native view** (`E:\xhci98-vm\a347\stor2\w10k\`, QEMU's
`pci_nvme_read`/`pci_nvme_write`/`pci_nvme_verify` trace):

| Step | Result |
|---|---|
| stornvme on the drive (before the package): `diskpart` clean, a 100 MB primary partition (the drive's end left unpartitioned for the raw write), NTFS quick format (V4K, T:), two files written and compared | `fsutil fsinfo ntfsinfo T:` "Bytes Per Sector : 4096", "Bytes Per Physical Sector : 4096"; `fc /b` clean; `driverquery`: stornvme running |
| The package installed (`/dir nt-x86 /yes /nopause /thunderbolt /stor98`, exit 3; the unsigned-driver prompts accepted: "/stor98: installed without asking", "Stor98 Storage Controller for NVMe SSDs: stor98 3.4.1.0, started"), a restart | `driverquery`: stor98 running, stornvme stopped |
| Under Stor98: stornvme's files compared; two files written and compared | "Bytes Per Sector : 4096"; `fc /b` clean on all four |
| `vfyblk 2 edge` | 0 failed: READ CAPACITY(10)/(16) last LBA 32767, **4096 bytes, exponent 0**; the last block read, written raw and restored (each one command of one 4096-byte block at LBA `0x7fff` in the trace, no read-modify-write), one past it 05/21/00 for READ, VERIFY and WRITE; VERIFY(16) of 8193 blocks 05/24/00 (the native view's bound, 8192) |
| Replug (QMP `device_del` / `device_add` of the enclosure), then compare and `edge` | the same: 4096 bytes, files clean, 0 failed |
| Restart, then the same | the same |
| Resume from hibernation (`shutdown /h`; Standby (S3) is not offered by this guest's display driver, `powercfg /a`), the same overlay started again, then the same | the same |
| A controller reset in place: the NT debug-port image with `storpollmode=1` and `vfyread=1`, a VERIFY(16) of 8192 blocks (32 MiB by reads, polled) with a 10 s pass-through time-out, then the same | the VERIFY ended by the port's time-out, `DeviceIoControl` error 1117 after 9.1 s (stage `0x1A3` failed after 466 of 1024 children, 9124 ms), a new bring-up (`0x1A0`); with a 60 s time-out the same VERIFY GOOD in 24.6 s; after the reset 4096 bytes, files clean, `edge` 0 failed |
| The drive back on stornvme: Stor98's guest shut down, a fresh overlay of the clean 10 x86 base (no package) started with the same drive | `driverquery`: stornvme running; the volume as E: (the letter is the system's, not the drive's), "Bytes Per Sector : 4096"; `fc /b` clean on the two files stornvme wrote and the two Stor98 wrote (`E:\xhci98-vm\a347\stor2\w10s\`) |

**7 x86, the same namespace in the 512-byte view**: "47.22 (the legs
owed)", "VERIFY on a 4 KiB namespace in the 512-byte view": READ
CAPACITY(16) 512 bytes, exponent 3; the last 512-byte LBA and one past it
in every command, and the raw one-block write of the last sector (a
read-modify-write of native block 32767).

**XP, a 512-byte namespace**: "47.22 (the legs owed)", `vfyblk 2 edge`
(last LBA 262143, 512 bytes, exponent 0).

**Not run**: 8.1 (the drive back on stornvme was read on 10 instead;
8.1 x86 had the stornvme-to-Stor98 direction in 47.31); 10 x64 (no
signed 64-bit package in this kit); the frag=64 split (a test-build
`frag=` run, not repeated here).

## The MS-DOS FORMAT reads past the partition's end

Found in 47.22's 98 SE legs (`runs/run-47.md`, "47.22"): MS-DOS FORMAT
(`format E: /u /autotest` in a DOS box) on the test partition goes past
the partition's end, Stor98 refuses those requests with 05/21/00, and
FORMAT tries again and again. Read on 98 SE (`E:\xhci98-vm\a347\stor2\s98\`,
the vehicle of "47.22 (the legs owed)"), with a test-only image of the
same source that writes the LBA and count of every request Stor98 refuses
as out of range (debug-port stages `0x1AA` and `0x1A9`; the patch script
`E:\xhci98-scratch\w47\stor2\frpatch.py`, applied for the build and
reverted, never committed; `stor98.mpd` SHA-256 `54348e39...`).

**The test partition.** The ORICO model's drive is the q35 bases' FAT
test disk (`shared\nvme-fat.qcow2`): 262144 sectors of 512 bytes (the
default 512-byte namespace, in every run here and in 47.22's), one
partition of type 06h at LBA 63, 262081 sectors, to the last sector; its
CHS fields do not match any geometry (end 16/81/1); the boot sector was
written by mtools (OEM name `MTOO4049`), its BPB saying 16 heads and 63
sectors per track, 2 KiB clusters, 65,384 of them.

**What 98 SE sees** (DEBUG scripts in the guest, `GEO*.SCR`):

- INT 13h: function 08h answers for 80h (two hard disks: the system
  disk and the transfer disk) and fails for 82h (AH 01h, carry set):
  the tunnelled drive is no BIOS unit (the firmware never sees it), so
  98 SE's geometry for it is Windows' own, not the BIOS's.
- INT 21h 440Dh/0860h on E: (the recommended and the current BPB alike):
  128 cylinders, 64 heads, 32 sectors per track, 63 hidden sectors,
  262081 sectors: Windows replaces the boot sector's 16 x 63 with its
  own 64 x 32, and 128 x 64 x 32 is exactly the 262144 sectors of
  Stor98's READ CAPACITY (last LBA 262143). Stor98 reports no geometry
  of its own on 9x; capacity and geometry agree.

| Run | Image, disk | What FORMAT did |
|---|---|---|
| 1 | 3.4.2.0 release (`69f867c1...`), the test disk | 1023 VERIFY(10) of 32 sectors, all GOOD (NVMe Verify), in the first 512 sectors of each 2048-sector cylinder (stride 64), cylinders 0 to 127; then "Trying to recover allocation unit" from 38,272, counting past 91,680 (more units than the volume has) with nothing reaching the drive; FORMAT ended by hand after 9 minutes |
| 2 | the diagnostic image, a fresh copy of the test disk | the same 1023 VERIFYs; then refused: VERIFY(10) of 32 sectors from LBA 262207 (= 128 x 2048 + 63, the 129th cylinder) to 311423, 387 LBAs (16 a cylinder, stride 32, cylinders 128 to 152), each tried twice; then READ(10) of 4 sectors (one allocation unit) for every unit from LBA 262207 on, past 395449 when ended: 774 VERIFY and 6372 READ refusals in the first 4 minutes |
| 3 | the diagnostic image, the test disk with only the BPB's heads and sectors per track changed to 64 and 32 | 2730 VERIFY(10) of 32 sectors at stride 96 from LBA 127 to 262111, all inside the drive, no refusal; FORMAT finished and wrote a new boot sector (`MSWIN4.1`, 64 x 32) |
| 4 | 3.4.1.0's release image (`e0126bd2...`), a fresh copy of the test disk | FORMAT finished in under 3 minutes with no "recover": 3.4.1.0 answered every VERIFY GOOD without a range check or a medium access (no NVMe Verify in the trace); its new boot sector kept 16 x 63 |
| 5 | the diagnostic image, a fresh copy of the test disk, FORMAT left to run | FORMAT finished after about 18 minutes. Stor98 refused 12,288 VERIFYs (LBAs 262207 to 1047071: FORMAT's pass spans four times the drive) and 101,376 READs (99,840 allocation units, LBA 262207 to 1047133). The new FAT marks **33,296 of the 65,384 clusters bad** (cluster 32090 to the last) on a drive with no bad block; runs 3 and 4 left none |
| 7 | option (a) below, a standalone free build of the change (`04d84c89...`), a fresh copy of the test disk | FORMAT finished in under 3 minutes: the same 1023 VERIFYs inside the drive (NVMe Verify), those past the end answered GOOD without a command; **0 clusters bad** |
| 8 | option (a), the gated `release` image (`stor98.mpd` SHA-256 `719ed907...`, the gated x86 `release` build of this change, "BUILD + GATES PASSED (release)"), a fresh copy of the test disk | FORMAT finished in about 2.5 minutes (started 00:46:21, its window gone by 00:48:52): 1023 VERIFYs inside the drive, the rest answered without a command; **0 clusters bad** |

**Who is to blame: FORMAT, given that boot sector.** The partition
table's LBA fields are right (63 + 262081 = 262144), and Stor98's capacity
and the geometry Windows builds on it agree (128 x 64 x 32). FORMAT's
pass depends on the boot sector's BPB: with 16 x 63 it walks cylinders of
2048 sectors (Windows' geometry) for more cylinders than there are and
addresses 512 of them, four times the drive (Windows' 64 heads against the
boot sector's 16: the drive is the first quarter of FORMAT's pass, the
"about a quarter" of 47.22's note); with 64 x 32 it stays inside
(inferred from the request patterns; FORMAT's code was not read). So a
FAT volume whose boot sector carries another geometry than Windows'
(made by mtools here; a volume made on another system with another
translation may be the same, not run) sends MS-DOS FORMAT /U past the
end. What changed in 3.4.2.0 is Stor98's answer: 3.4.1.0 answered a
VERIFY past the end GOOD and read nothing, so FORMAT finished and the
overrun was invisible; since 47.1's audit fixes (kept by 47.22) a VERIFY
past the end is refused (05/21/00) as SCSI requires, and FORMAT turns
each refused track into READs of every allocation unit in it, each
refused too, and ends with half the new volume marked bad (run 5).


**The fix (owner, 2026-11-06, option (a))**: on the 9x build a VERIFY
whose range starts at or past the end is answered GOOD with no media
access (`VFY_PAST_END`, `src/vfychk.h`; `ScsiHandleVerify`); a VERIFY
partly in range is still refused 05/21/00, READ and WRITE past the end
still are, and 2000 to 10 are unchanged (Stor98 PLAN, "VERIFY"; host
test `vfychk_ut`). FORMAT's surface check on such a volume reaches only
about an eighth of the drive (1023 VERIFYs of 32 sectors, read from the
medium); 3.4.1.0 read none of it (every VERIFY answered without the
medium). That is FORMAT's walk, given the boot sector. The half-bad result never shipped: it came with the
range check of 3.4.2.0's own audit fixes, and 3.4.1.0 answered GOOD.

## C2 the mapping time

The reading C2 waits for (design record 30 section 13.6: "One
`ScsiPortGetPhysicalAddress` call per run, not per page, in
`EngMapCaller`"; section 13.7: "a counter of the mapping's time"), owner's
go of 2026-11-06. 98 SE in the emulator only; every figure compares within
this run (record 30 section 13.4).

**The test image.** The earlier worker's patch script
(`E:\xhci98-scratch\w47\stor\c2patch.py`), retargeted to this worktree and
extended (`E:\xhci98-scratch\w47\stor2\c2patch.py`, diff
`c2patch.diff`), applied for one 9x debug-port build and reverted with
`git checkout` (never committed; `stor98.mpd` SHA-256 `cfc8fd3a...`):
at each READ/WRITE parent's completion, stage `0x1AD` (its pages and
KiB), `0x1AE` (the TSC spent in `EngMapCaller` for it, summed over its
children), `0x1AF` (the TSC from its admission to its completion), and two
counters over every `ScsiPortGetPhysicalAddress` call, `0x1AC` (calls
whose returned length reaches past the end of the page of the address
asked for, and the longest length, in pages) and `0x1AB` (calls).

**The vehicle.** As 45.10's (`runs/run-45.md`, "45.10"): a fresh overlay
of the q35 98 SE base (`E:\xhci98-vm\a347\stor2\s98c\`), the package of
"47.22 (the legs owed)" installed with `/s98vsd` (exit 3), a cold start,
the test image copied over `STOR98.MPD`, a cold start with the debug
port; XHCIPERF of `b126cefc` (SHA-256 `8a5ea21b...`) by 45.10's batch:
R1 (`seqmax`, aligned) and R3 / R4 (`sweep` 64 KiB to 8 MiB, aligned and
scattered), 48 MiB file on E:. 59,711 parents recorded. The debug-port
writes cost about 182,000 TSC each (the gap between two consecutive
stages), several inside each parent's time; the shares are given raw
and with that cost taken out ("corrected"). Times stay in TSC units: the
guest TSC's rate was not calibrated here.

| Requests (all of R1, R3 and R4, by size) | Parents | Pages each | `ScsiPortGetPhysicalAddress` calls per page | Answers past the page | Map TSC per page | Map share of the parent's time, raw / corrected |
|---|---|---|---|---|---|---|
| 4 KiB (file system) | 5406 | 1 | 1.00 | 0 % | 232,000 | 15 % / 29 % |
| 63 KiB pieces (8 MiB requests cut by IOS) | 21186 | 16.5 | 1.00 | 80 % | 29,400 | 28 % / 48 % |
| 64, 128, 256 KiB (R3 and R4) | 3069, 766, 382 | 21, 32, 64 | 0.96, 1.00, 1.00 | 0 % | 26,000, 12,300, 11,100 | 18, 20, 27 % / 30, 28, 35 % |
| 512 KiB | 6871 | 128 | 1.00 | 96.5 % | 14,800 | 42 % / 48 % |
| 1 MiB | 8357 | 256 | 1.00 | 97.4 % | 17,600 | 60 % / 69 % |
| 2 MiB | 2545 | 512 | 1.00 | 98.0 % | 17,300 | 70 % / 81 % |

What it says:

- **The mapping is a large part of a large request's time in the
  emulator**: about 42 % at 512 KiB, 60 % at 1 MiB and 70 % at 2 MiB of
  the time from admission to completion [read], 12,000 to 18,000 TSC a
  page for those sizes; under TCG every instruction costs far more than
  on hardware while the emulated drive's data moves at host memory speed,
  so the share on hardware may be smaller; it is not measured [inf].
- **98 SE's port does return runs longer than a page**: for the large
  linear buffers 96 to 98 % of the calls returned a length past the end
  of the page, up to 512 pages (2 MiB) [read]; for 64 to 256 KiB and
  the single pages none did. So `engine.c`'s comment above `EngSpanOk`
  ("The Windows 98 SE port returns at most the length to the end of the
  page (engine runs, 2026-10-15)") does not hold for these buffers
  (S98VSD loaded; whether that changes the port's answer was not read);
  the code does not depend on it (each span is checked on its own).
- **C2 can save time where the answers already cover runs**: today
  there is one call a page in every case (1.00 calls a page). Where a
  call's answer covers the rest of a physically contiguous run, C2 takes
  the following pages' entries from it and skips their calls. For the
  512 KiB to 2 MiB linear buffers that is nearly every call after a
  run's first; how many runs a child had was not counted, so the saving
  is bounded, not measured: at most the map share, about 40 to 70 % of
  such a request's time in the emulator (less the per-entry checks that
  stay), and nothing for the 64 to 256 KiB requests and single pages
  whose answers never reached past the page [inf]. On hardware the gain
  is the per-call cost times the calls skipped, which B4 (record 30
  section 13.5) refits; this reading does not give it.
- C2 stays as record 30 has it (planned, decided by its reading); the
  owner's decision is whether this emulator reading is enough to make it
  (question in the report).

## 47.27 (follow-up) A SHA-1 test certificate for usb-wdm-free's x64 images (2026-11-06 and 07, w47-sha1)

**Why.** The x64 SR9800 and SR9700 legs of `w47-wdm4` (`b141b47`,
usb-wdm-free `docs/testing/2026-11-06-sr98xx-sim.md`) found 7 x64
refusing the images signed with usb-wdm-free's test certificate
(`E6A2749B...`, since 0.1.4) unless Microsoft's SHA-2 update was
installed: that certificate is itself signed with sha256RSA. xHCI98's
own test certificate, SHA-1-signed, loads on the same base. The owner
made a SHA-1 one: `CN=usb-wdm-free test signing SHA-1 (not trusted)`,
sha1RSA, RSA 2048, 2020-01-01 to 2039-12-31, thumbprint
`7A3C6E6289EC46E6E128D87BB54E87196B824959`, file SHA-256 `095475c7...`.

**The tooling** (usb-wdm-free branch `w47-sha1`, `7143c84`, `304b205`,
`89e1eaa`, reviewed and revised): the public certificate tracked as
`release/usb-wdm-free-test-sha1.cer`, published by the release
operation as the root `usb-wdm-free-test.cer` (the name xHCI98's
packager and installer read); `release.ps1` defaults `-Thumbprint` to
it and refuses a signer certificate not signed with SHA-1, in each x64
image and in `-Certificate`; without `-Certificate` an x64 release
checks that the root certificate is the signer's. A "Next release"
Signing entry in the release notes; README, status and installer design
updated; past-release entries untouched.

**The images.** The owner signed `ax772f.sys` and `sr9700f.sys`
(`ndis51` free, `w47-wdm4` `f45732b`) with the new key:
`a2a4d69d...`, `6af89656...`. Each one is equal to its unsigned build
apart from the checksum, the security entry and the appended table.
The signer is `7A3C6E62...` (sha1RSA), the file digest is SHA-1, there
is no timestamp, and the image digests (`0F189DA8...`, `54F76B9A...`)
match.

**Legs** (stock QEMU 11.1.0, q35, xHCI98 3.4.1.0, simulated chips over
usb-redir; per chip: DHCP lease, `ping -n 4` at 32, 1472 and 4000
bytes, removal under `ping -t`, replug):

| System | SR9800 (`ax772f`) | SR9700 (`sr9700f`) |
|---|---|---|
| 7 x64 SP1, the plain base (no KB4474419, KB4490628 or KB3033929), test signing on, certificate in Root and TrustedPublisher | PASS (no prompt at `pnputil -a`; RUNNING; no Code Integrity event) | PASS |
| Vista x64 SP2, the same setup | PASS ("can't verify the publisher" at `pnputil -a`, the packages having no catalog; then RUNNING) | PASS |
| XP x64 | PASS (wizard, "Continue Anyway") | PASS |

The host lost power on 2026-11-07, after the 7 x64 legs and the first
XP x64 legs. The XP x64 and Vista x64 overlays were made again from
their bases, and every leg on them ran on the new overlays.

Evidence: `E:\xhci98-vm\a347\sha1\` (pids.txt, shots, ev, xnt64).
Record: usb-wdm-free `docs/testing/2026-11-06-sha1-cert.md`.

**For xHCI98's next release:** the `wdmfree-pin.txt` `Certificate:`
line becomes `7A3C6E6289EC46E6E128D87BB54E87196B824959`, and the
`SHA256:` line of `usb-wdm-free-test.cer` becomes
`095475C7BD9F882DA40C8932924D3E9EE7BF5430CB5FDB07C08E17BE7A441818`.
Design record 26's paragraph on the pin (near "From usb-wdm-free
`v0.1.4`") gains the new certificate. `package-common.ps1` keeps the
file name.

## Reference drivers against the usb-wdm-free simulators (agent ref, 2026-11-06/07)

usb-wdm-free branch w47-ref (w47-wdm4 b141b47 merged in at 727c127 for ax7emu/sremu; work at aaf75ba).
Record: usb-wdm-free docs/testing/2026-11-06-reference-drivers.md.

- axemu.py vs ASIX AX88179 XP 1.4.2.0: FAIL as found, PASS after six model fixes; ax179f retested PASS on
  98 SE (NUSB EHCI) and XP (Windows' EHCI).
- ax7emu.py vs ASIX AX88772B XP 5.4.3.0 and 7 5.12.12.0: both FAIL (XP driver deconfigures the device;
  7 driver ignores DHCP OFFERs, Code 43). Model fixed where the traffic was clear; ax772f retested PASS on 98 SE.
- rtlemu.py vs Realtek XP 5.23 (8152/8153/8153B), 7 7.61.20 (8156), Win10 10.68.20 (8153/8156/8156B) and
  Windows 10's own 10.5.920: one model fix (PHY byte-lane writes; RTL8153 under XP looped on resets before);
  8156B under 10.68.20 Code 10, not explained. rtl815x retested PASS on 98 SE and XP.
- xpademu.py vs x360c (XP) and Windows 10's own Xbox 360 driver: PASS; --vid/--pid added.
- No maker's driver: SR9700/SR9800 (CoreChips lists SR9900 only), PenPartner (Wacom 4.78-6 gone; PenTablet
  5.2.4-5 does not list 056A:0000).

## usb-wdm-free: inbox-driver reference runs of the class simulators (agent ref2, 2026-11-06/07)

Owner, 2026-11-06: check every emulator model against Windows' own driver
for its device class, fix the model where it diverges, rerun usb-wdm-free's
driver against each changed model. Branch `w47-ref2` of usb-wdm-free (from
main `3942e20`); record `docs/testing/2026-11-06-inbox-reference.md`.
Development legs in QEMU (TCG), stock QEMU 11.1.0 with `usb-redir` and its
`pcap=` capture, Windows' own USB stack (XP SP3 and 7 SP1 on EHCI + UHCI,
10 22H2 x64 on `qemu-xhci`); 98 SE retests on the usb4-emu fork `cb36497`.

| Model | Reference | Result | Model changed | Our driver |
|---|---|---|---|---|
| `uacemu.py` | 10 `usbaudio2.sys` | `hs24`, `clocks`, `adaptive`, `multi`, `mic`, `x1`, `uac3dual` PASS; `cx31993m`, `alc4080cap` Code 10 not settled; `x4full` Code 10 (Mixer/Selector Unit stalls) and `fs` Code 10 (3-byte FS feedback) | y: units answered; `--fs-feedback 4` (default stays 3) | 98 SE xHCI98 `x4full` play/rec PASS |
| `midiemu.py` | XP/7 `usbaudio.sys` MIDI | XP: model stopped on XP's zero padding, PASS after; 7 PASS | y: reserved CIN 0/1 skipped | 98 SE xHCI98 and XP PASS |
| `absemu.py` | XP HID, 7/10 digitizer | XP as expected; 7 touch/mtouch nothing until X/Y units | patch for `w47-wdm2` | owed when applied |
| `kbdemu.py` | XP HID + hidserv | PASS | n | stand |
| `ncmemu.py` | 10 `UsbNcm` | Code 28: 19041's `usbncm.inf` model commented out; no ECM in any Windows | n | stand |
| `acmemu.py` | 10 `usbser` class, 7 `usbser` by `USBCDC.INF` | PASS | n | stand |
| `ufiemu.py` | XP `usbstor` (as 03EE:6901) | PASS (read, full format, copy, fc) | n | stand |
| `uvcsim.py`/`uvcemu.py` | XP/10 preview, 7 bind | XP 1.0 PASS (1.1 Code 10, XP takes 1.0 only), 10 Camera PASS, 7 bind | n | stand |

Commits: `e34bd8b`, `0de729a`, `66d99ee` (reviewed and revised). Evidence:
`E:\xhci98-vm\a347\ref2\ev\`, `shots\`.

## The RTL8157 on real hardware (2026-11-06)

Worker r57. usb-wdm-free branch `w47-r57` (from `w47-wdm4` `f45732b`,
rtl815x 0.2.1.0): `9d9648e` tools, `16333c0` core and rtl815x,
`f446180` transmit queue, `d4dd81d` docs. Record: `docs/testing/2026-11-07-rtl8157-hardware.md`,
design `docs/design/ethernet.md` sections 4.7 and 6.4.2.

- **Device:** a real RTL8157 (chip `1030`, `0BDA:8157` rev 3000), bound to
  WinUSB on development host A and passed to QEMU guests by `usb-host`.
  It was cabled straight to a host 10GbE port: 5 Gb/s, link-local
  addresses. The host was not changed.
- **Guests:** fresh q35 overlays.
  - XP SP3 and 98 SE under xHCI98 3.4.1.0 as released.
  - Windows 10 x64 for Realtek's driver 10.68.20 (the owner's download).
- **Tools (new, usb-wdm-free):** `NETPERF.EXE`/`netperf.py` (TCP
  throughput, checked pattern; the host connects out) and `rtlpcap.py`
  (usbmon capture decoder).
- **The rig matters.**
  - The stock `usb-host` on Windows polls libusb every 10 ms and never
    pipelines bulk transfers, so about 85 transfers a second per
    endpoint: any driver near 17-20 Mb/s per direction. Realtek on Win10
    measured 17.2 out / 17.6 in.
  - XP's 15.2 / 2.07 Mb/s (both at once, 11 min) and the 7:1 asymmetry
    come from that cap. rtl815x's bulk-OUT transfers are small (53% one
    frame); bulk-IN is aggregated to 32 KB; ACKs share the OUT pipe.
  - A test emulator (0.25 ms poll, bulk pipelined; a scratch patch, not
    tracked) was used for everything after.
- **Reliability, before the changes:**
  - XP: link 5000; ping 1472/4000/8000 10/10 each; 11 min both ways,
    1.43 GB, 0 mismatched bytes; idle unplug/replug pass.
  - **XP unplug under traffic HANGS.** xHCI98 holds the departed
    device's transfers and 40 sends stay in the transmit slots. TCP/IP
    never unbinds, so there is no MiniportHalt: the adapter stays,
    a replug never loads, shutdown hangs.
  - **98 SE TCP out:** the send queue is corrupted, transfers stall
    (0.01-7 Mb/s) and the guest takes fatal exceptions 0E (MSTCP).
- **Fixes (owner-approved, reviewed to convergence):**
  - A: report a departure before any transfer wait (release held sends,
    Link(-1), abort).
  - B: send status PENDING (98 SE's NDIS reads it), and the core takes
    the kernel spin lock (98 SE).
  - P1: PHY power-down at halt, disable and suspend.
  - S3: receive indications in arrays of up to 16.
  - S1: 8 bulk-IN transfers.
  - S2: 32 KB / 20-frame transmit on the RTL8157/8159 only. No gain
    measured; held for the owner on branch `w47-r57-s2` (`2554299`), not
    in the release line.
  - TX queue (S5): under sustained TCP the core's 64-frame queue filled
    and sends failed (3191 in 11 min on XP). The queue now has no limit
    (packets linked through `MiniportReserved`), the pump is kicked every
    8 packets, and the paths that could hold sends forever (D3, recovery,
    init) are closed. A 128-packet receive pool was tried; no clear gain,
    not adopted.
  - The shared core is copied identically to ax88179, ax88772 and
    sr9700; host tests and builds pass for all four.
- **After:**
  - XP: unplug under traffic: PASS on every build since A, 3 cycles each
    ("0 of 9" or "0 of 13 outstanding after the abort", halt, reload,
    ping 3/3), last on `f446180`.
  - XP `f446180`, 11 min both ways at once: 4.58 GB out at 55.3 Mb/s,
    5.45 GB in at 66.0 Mb/s, 0 mismatched, 0 failed sends (queue peak
    283). Before the queue fix: 27.9/59.6 with 3191 failed sends.
  - XP: Device Manager disable/enable: PASS. The host port goes
    Disconnected while disabled.
  - 98 SE: send ran (37-70 Mb/s), but **sustained TCP in either
    direction on 98 SE still stalls and faults MSTCP with every build**
    (0E at `0028:C19243C3`/`C1924CB3`/`C1924CC3`/`C1924CA3`, and
    `C1924403` on `f446180` after 33 s). 98 SE pings 10/10 and unplug
    under ping with replug pass on `f446180`. The receive pool stays
    whole (64 free) when it happens. Open; no control NIC was possible
    (98 SE has no RTL8139 driver).
- **Speed (test emulator, XP, same boot, Mb/s out/in, means):** before
  26.6/58.2 (A+B); +S3/S1/P1 35.8/81.1; +S2 29.7/73.5; second boot
  `16333c0` 30.9/69.1 (147-184 failed sends per bench), queue fix
  28.2/69.2 (0 failed). Realtek 10.68.20 on Win10 x64: 244/339 on the
  test emulator, 17.2/17.6 on the stock rig. The cause of the gap: the
  stock rig caps everything near 17-20 Mb/s; on the test emulator
  rtl815x sends small bulk-OUT transfers (most one frame) and XP's
  TCP/IP and NDIS 5 per-packet cost bound it; Realtek's driver runs on
  Win10's stack with bulk-OUT transfers of up to 32848 bytes.
  rtl815x cannot run on Windows 8 or later, so no like-for-like.
- **Heat:** wherever the two drivers differ, rtl815x (= Linux) saves
  more power than Realtek's driver.
  - rtl815x: EEE on (100M-5G advertised), ALDPS on, EEE and 10M clock
    division on, MAC clock slow-down on.
  - Realtek 10.68.20 by default: all off (INF `*EEE` 0, GreenEthernet 0,
    AdvancedEEE 0, ExtraPowerSaving 0, *SelectiveSuspend 0; the captured
    writes agree).
  - Both: chip U1/U2 off while linked; UPS and power cut off.
  - Interrupt pipe: 7.8/s under xHCI98 (bInterval 11 = 128 ms) against
    124.5/s under Windows' xHCI.
  - The one gap was the PHY at disable: Realtek powers it down, rtl815x
    kept a 5G link. Fixed (P1).
- **Not run:** standby/resume (no S3 in the q35 machine as launched),
  Windows 2000, 98 SE disable/enable, cable flaps by renegotiation.
- **Evidence:** `E:\xhci98-vm\a347\r57\`: `ev\` (captures, bench logs,
  register dumps), `shots\`, `pids.txt`, `STATE.md`.

## 47.34 XHCITR's firmware tables: the registry's copies where Windows gives no DSDT, MSDM and SLIC never saved (2026-11-06 to 07, w47-acpi)

The owner's task of 2026-11-06, from 47.33's firmware tables: on an HP
EliteBook 8470p under XP x64, Windows' firmware-table interface listed
eleven signatures (`FACP`, four SSDTs, `SLIC` and `MSDM` among them) and
gave no `DSDT` and neither root table, so the save held no `DSDT`; and it
saved `MSDM` and `SLIC`, which hold the computer's Windows licence data.
Design: record 25 revision 28, section 21 item 10 (`54ad49eb`), reviewed
and revised. Code `147c2a28`, `14aa49f4`, `905439a9`, reviewed and revised.
Work on development host A.

### What changed

- On NT with the interface, where the set holds no good `DSDT` (whole, at
  least its header, its length field the bytes given, its checksum zero),
  the registry's copies under `HKLM\HARDWARE\ACPI` are walked as on 2000
  and 32-bit XP (`KEY_READ`, the same checks), and every copy of `DSDT`,
  `FACP`, `RSDT` and `FACS` the interface gave no good table of is added,
  "From" `registry`; a copy that fails a check is listed "not saved" with
  its reason. Where the interface gives a good `DSDT` (Vista and later)
  nothing more is read. Where it gives nothing at all, the registry's set
  is indexed and said as on 2000 and 32-bit XP.
- On every system and route, `MSDM` and `SLIC` are held as their 36-byte
  header alone before any file is named (`tr_acpi_names`), listed in
  `TABLES.TXT` with "not saved: licence data", out of the message's list of
  saved tables and counted in "<n> more are listed in TABLES.TXT, not
  saved."; on 98 SE and ME a header of `xhcicm.sys`'s is kept through the
  merge with `xhci98.sys`'s set.
- The NT index's root-entry line counts the tables saved ("<k> of them
  were saved (Windows gives each name once)"), never counting a root
  table as a child; a registry value of another signature reads "not the
  table its registry key names"; the 98 SE / ME `xhci98.sys` footer is
  rewrapped within 79 columns.
- `xhcitr\README.TXT`, "The firmware tables": the registry route on XP x64
  and the licence tables.

### The registry's layout

Microsoft's public documentation does not describe `HKLM\HARDWARE\ACPI`
(the WDK's ASL compiler page documents only `asl /loadtable`'s test
override, not this key), and the 2000 DDK and WDK 7.1 headers do not name
it. The layout rests on what Windows was read to keep (`reg query`, read
only, no package installed): `DSDT`, `FADT` and `RSDT`, each
`<OEM ID>\<OEM table ID>\<revision>` with the value `00000000`
(`REG_BINARY`, the whole table; the IDs' padding written `_`, as
`BOCHS_\BXPC____\00000001`), and `FACS` with its value itself:

| System | Keys under `HKLM\HARDWARE\ACPI` |
|---|---|
| 32-bit XP SP3, 2000 SP4 | `DSDT`, `FADT`, `RSDT`, `FACS` (run 46) |
| XP x64 SP2 (q35 base) | the same four; the `RSDT` value byte for byte the guest's memory at the root pointer's address |
| 7 x64 SP1 (q35 base) | the same four keys (not read by the tool there: the interface gives the `DSDT`) |

No `SSDT`, `MSDM` or `SLIC` key on any of them. So `FACP`, `RSDT` and
`FACS` have the same registry copies as the `DSDT` and are offered the
same way.

### Checks

`test_tracpi` 10594 checks, 0 failures (`905439a9`): the registry taken
only where the interface gave no good `DSDT` (none, too short, its length
off, a bad checksum; a good one: not read); per signature, the
interface's good `FACP` kept, a bad one with the registry's beside it,
the registry's `RSDT` beside an `XSDT`, a `FACS` under 64 bytes; a bad
checksum, a length that disagrees and another signature in the registry
refused, never named; the interface answering nothing; `MSDM` and `SLIC`
whole from the interface (each listed twice), shorter than a header, as
headers from `xhci98.sys`'s set and from `xhcicm.sys`'s answer, whole
from `xhcicm.sys` and through the merge: never named, listed "not saved:
licence data"; `msdm` and `MSDX` saved as other tables; every new index
within 79 columns; the messages.

### The legs

`XHCITR.EXE` `69dc81f7...`, `XHCITR64.EXE` `0b4a2e05...` for the NT legs,
built from `905439a9` by the first gated run (cut off by the host's power
trip after its tool step); the rerun's images, `d036d241...` and
`78243aaf...`, differ from them in the link time stamp and the build-time
string alone (8 bytes each). `XHCITR /acpi c:\<folder>` redirected, on
fresh overlays of the q35 bases with no xHCI98 package installed (NT
legs), one at a time, each shut down cleanly.

| System | Result |
|---|---|
| XP x64 SP2, `XHCITR.EXE` and `XHCITR64.EXE` | the interface gave `FACP`, `APIC`, `HPET`, `MCFG`, `WAET` and no `DSDT` or root table, as on the 8470p; `DSDT` (8042 bytes), `RSDT` and `FACS` saved from the registry, From `registry`, every Sum "ok" (`FACS` "-"); "Windows gave no DSDT; the copy it keeps in the registry (HKLM\HARDWARE\ACPI) was read." for each; the message "... FACP, APIC, HPET, MCFG, WAET, DSDT, RSDT, FACS, and TABLES.TXT listing them. DSDT, RSDT, FACS came from the copies Windows keeps in the registry."; exit 0; the `DSDT` byte for byte the guest's memory at the FADT's address (dumped from QEMU's monitor, SHA-256 `e8e0a1c1...`), both executables |
| 32-bit XP SP3 | unchanged: `DSDT` (8042), `FACP`, `RSDT`, `FACS` from the registry; the `DSDT` byte for byte the guest's memory (`60f8f292...`) |
| 2000 SP4 (the ACPI base) | unchanged: `DSDT` (7932), `FACP` (116), `RSDT`, `FACS` from the registry; the `DSDT` byte for byte the guest's memory (`e9702500...`) |
| 7 x64 SP1, `XHCITR64.EXE` | unchanged but for the root-entry line's wording: the `DSDT` (8071) and `RSDT` asked for by name from the interface, From `Windows`, no registry line; exit 0 |
| 98 SE (no ACPI on 9x's side; the released `3.4.1.0` package installed, whose `xhci98.sys` serves the tables; restart; this build's `XHCITR.EXE` `d036d241...` from the transfer disk) | unchanged: RSDP, RSDT, FACP, MCFG and the DSDT (8018) from `xhci98.sys`, APIC, HPET and WAET listed by their headers; the message as before ("... 3 more are listed in TABLES.TXT, not saved."); the footer in its new wrapping; the `DSDT` byte for byte the guest's memory (`d018fff0...`, as in run 46) |

QEMU's tables carry no `MSDM` or `SLIC`, so the rule that never saves
them rests on the host vectors above. The serial logs and memory dumps
were deleted with the overlays once read; the readings above are what
they held.

### Builds

The gated builds of `905439a9`: x86 release and qemu, BUILD + GATES PASSED
(packager self-tests 1845 checks, host tests passed, the xhcitr snapshot
self-test 53 runs each image); amd64 release, BUILD + GATES PASSED. The
first x86 run was cut off by the host's power trip of 2026-11-07 and rerun
from the start.

The owner's answers (through the coordinator, 2026-11-07): the licence
line stays "not saved: licence data"; one table not saved reads "1 more is
listed in TABLES.TXT, not saved." (`8ef8bf2d`, reviewed; `test_tracpi`
10594 checks, 0 failures). The gated builds of `8ef8bf2d`: x86 release and qemu, BUILD + GATES PASSED;
amd64 release, BUILD + GATES PASSED (`test_tracpi` 10594 checks, 0
failures, in both).

### Left

A real machine whose interface gives no DSDT (the 8470p) read on this
build, and one with `MSDM` and `SLIC`, which QEMU does not give.

## 47.30 The larger event ring and the isochronous stream's timing (was ISORING.4)

**The design** (design record 13 section 11.10, `2e556c86`, reviewed and
revised; the owner chose option 1 on 2026-11-06): one 1,024-entry event
ring segment in a common-buffer block of its own, allocated at the
controller's start (16 KiB, or 32 KiB once when the first block has no
64 KiB-safe window), the 256-entry ring in the controller block the
fall-back; the release value `XhciEventRingTrbs` (1024 default, 256 the
one-page ring); the isochronous stream's timing (Part B, from a user's
report of two undated Ring Underruns on an FL1100 under XP x64).

**The change** (`e4dafb87`, `35ee87a5` and the gate commit between them,
reviewed and revised): `HcdSvcEventRing`, `XhciEventRingWindow`, the ERST
entry's size from the ring, the isochronous grant from
`ext->EventRing.Trbs`, `XHCI_DPC_MAX_EVENTS` kept at 1024; per isochronous
pipe the longest publication gap and its own request's wait, the longest
wait, the ring's fill, the dry events; an `at` record after each
`xfer.error` and `tol.stream.halt`; the close notes `iso.gap`,
`iso.gap.lag`, `iso.lag`, `iso.dry`, `iso.close`; the snapshot HCD header's
block (792 -> 860 words); XHCITR's table and each `at` record's UTC time;
the INF rows, the INF gate's row, its self-test anchor and the footprints,
`tuning.md`'s row.

**Host suites** pass (`ht-r4.log`): `test_membuf` (the window at every
page phase), `test_ring` (a 1,024-entry lap), `test_pipe` (a pool pipe then
a page at 1,024: 254), `test_snap` (the header and the timing's pure half),
`test_tr` (the `at` decoders).

**Builds** (`35ee87a5`): x86 `all` passed (`gb-x86c.log`; release
`xhci98.sys` `9F4F29B6...8261`, qemu `B52BAC26...C8F9`); amd64 `all`
passed (`gb-amd64.log`; release `633DDD28...CF17`, qemu
`816635E0...4EF6`). Two earlier x86 runs failed: the INF gate's self-test
anchor (fixed in the gate commit) and a qemu-only field set outside its
guard (fixed in `35ee87a5`).

**The legs so far** (development host A, the pinned emulator, q35 bases,
TCG; the release package installed by `INSTALL.BAT /console /yes`):

| System | What was read |
|---|---|
| 98 SE | install exit 3; `xhci98.sys` = kit; hub, mouse, audio, BOT and UAS plugged at once: every devnode problem 0 but the BOT stick (28) and the UAS disk (2, `USBNTMAP.SYS`), this download carrying no 98 SE storage drivers; XHCITR `/dump`: `evt.ring.value=00000400`, `evt.ring=00000400` (the 16 KiB block, 1,024 entries), the table listing the audio pipe (slot 3, DCI 2, a 62-TRB pool ring); then each device unplugged 60 s apart (in progress at the hand-back) |
| XP | install exit 4 at the first run (the root hub's Found New Hardware window behind the Logo dialogs, answered), exit 3 at the retry; the controller started under `xhci98` 3.4.1.0 of this build; phase 2 (devices, disable/enable/remove/rescan, unplug, uninstall) in progress at the hand-back |

**Not read yet**: XP's device steps and snapshots; 2000, ME, XP x64 and 7
x64; the `qemu` flavour's fall-back, window and refusal routes, the HCRST
recovery, polled delivery and two controllers (kit `kitq`, `tools\qleg.py`
written, not run); QEMU `usb-audio` on a page; the High Speed `uacemu`
legs (`cx31993`, `cx31993m`) on the usb-redir build `cb36497`; the owner's
UAC 2 devices by passthrough.

## 47.30 (the legs)

Continued from the section above, on the same packages: release
`xhci98.sys` x86 `9F4F29B6...8261` and amd64 `633DDD28...CF17`, the `qemu`
flavour x86 `B52BAC26...C8F9`; development host A, TCG. `35ee87a5` (the
qemu-only default moved inside its guard) was reviewed on its own: no
material findings. XHCITR printed `evt.ring` (15:0 the TRB count, 23:16 the
route: 0 the 16 KiB block, 1 the 32 KiB route, 2 refused, 3 the test value)
and its new table at each step.

**The release flavour, device legs** (`INSTALL.BAT /console /yes`; the
q35 bases on the pinned emulator; a hub with a mouse behind it, QEMU
`usb-audio`, a Bulk-Only stick and a UAS disk; the stick and the UAS disk
read and written with a verified compare at each step):

| System | What was read |
|---|---|
| 98 SE | (above) install exit 3, `evt.ring` `00000400`; then the five devices unplugged 60 s apart, stick first, hub last: the desktop answered after each, the processor idle (`HLT`) at each register read |
| ME | install exit 3; every devnode problem 0 (hub, mouse, the audio composite, the stick, the UAS disk under `XHCIUAS`) but the base's own IDE child (10, as on 46's ME leg); both disks read and written; `evt.ring` `00000400`, the table listing the audio pipe; the five unplugged 60 s apart: ME's "Unsafe Removal of Device" each time, the desktop answering, `HLT` at the end |
| 2000 | install exit 3; every device started; both disks read and written before, after disable and enable, and after remove and rescan; `evt.ring` `00000400` at the first two, `00010400` after remove and rescan (the 32 KiB route, 1,024 entries: the restarted controller's 16 KiB block had no 64 KiB-safe window); the table each time with the audio pipe (a 62-TRB pool ring); uninstall exit 3, then no `xhci98.sys`, `/list` exit 0 |
| XP | install exit 4 then 3 (a Found New Hardware window pending, answered); the controller, root hub, stick and tablet started; the stick read and written before and after disable and enable; `evt.ring` `00000400` at both; the hub and the UAS disk Code 28, and after remove and rescan the controller Code 28, each from the leg's answering of the Found New Hardware wizard (an Enter on its first page, before a choice was made, took the privacy link and opened a browser window, and the wizard ended without a driver), so the mouse and audio behind the hub never came; uninstall exit 3, no `xhci98.sys` after. The rest of XP is read by the `qemu` session below |
| XP x64 | install exit 3 (controller, root hub, hub, the audio composite and the UAS disk under `xhciuas` started); both disks read and written before and after disable and enable; `evt.ring` `00000400`, then `00010400` after enable (the 32 KiB route); after remove and rescan the controller waited on Windows' Found New Hardware window and the uninstall ended with exit 4, "still installing", as 46's control on `3.4.0.0` did; after it the controller Code 1, no driver |
| 7 x64 | the package signed in the guest with a throwaway certificate, test signing on; install exit 3 (all five started); both disks read and written before, after disable and enable, and after remove and rescan; `evt.ring` `00000400` at each; after rescan every USB device Code 0 (controller, root hub, hub, two mice, the audio composite and `usbaudio`, the stick and the UAS disk); uninstall exit 3, the controller then Code 28 (no driver, the stock base) |

**The `qemu` flavour, XP** (one session, `kitq\QS.BAT` one step a start,
one vCPU; two `qemu-xhci` controllers, a stick on each, a mouse; the
release package installed, then the `qemu` `xhci98.sys` copied over it):

| Step | Value | Read |
|---|---|---|
| 0 | - | install exit 3, both controllers and the root hub started; the swap in place |
| 1 | none | `evt.ring` `00000400` on both controllers (`/c 0` and `/c 1`), `qemu.evtring` 0; the stick read and written |
| 2 | `XhciQemuEventRing` 2 | `evt.ring` `00010400`: the 32 KiB route, 1,024 entries; the stick read and written |
| 3 | `XhciQemuEventRing` 3 | `evt.ring` `00020100`: both requests refused, the start on the 256-entry ring; the stick read and written |
| 4 | `XhciQemuEventRing` 1 | `evt.ring` `00030100`: the in-block ring, 256 entries; the stick read and written |
| 5 | `XhciInterruptMode` 2 | delivery POLLING (2,804 poll runs), 1,024 entries; the stick read and written |
| 6 | fault 5 (`XhciQemuInject` `01050000`) | `qemu.inj.fire`, `ctrl.recover.begin`, `ctrl.recovered` 1, `HchRecoveries` 1; after the HCRST the ring still 1,024 entries and the stick read and written |
| 7 | `XhciQemuIsoRing` 1 | QEMU `usb-audio` on controller 1 (the composite and `usbaudio` through the wizard), two Sound Recorder plays: at each open `iso.page` `030200FE` (allowance 254); at each close, in order, `iso.page.free`, `iso.recs` (8 records, a peak of 3 or 4), `iso.gap`, `iso.gap.lag`, `iso.lag`, `iso.dry`, `iso.close`, each `at` printed as a UTC time of day and its age before the snapshot; each stream's last Ring Underrun as `xfer.error` (code 14) with its `at` |

Step 7's snapshots in the kit read controller 0; controller 1's was taken
by hand after the plays, the stream closed by then, so its table said "no
isochronous stream open" and the notes carry the reading.

**High Speed, `uacemu`, XP x64** (the usb4-emu build `cb36497` with
`usb-redir`, two vCPUs; `uac2` 0.1.8.26 from the free download; a
simulator on `xh.0` port 1; WinMM plays, 16-bit stereo 48 kHz, 60 s each,
under CPU load, one busy loop a processor; XHCITR64 snapshots before,
after and once during the plays; the ring by `XhciEventRingTrbs` in the
machine-wide key, a cold start each change):

| Run | Profile | Ring | Plays | Ring Underrun | Packets/s | Gaps >= 5 / 10 / 20 ms at the simulator |
|---|---|---|---|---|---|---|
| h1024 | `cx31993` | 1,024 | 5 | 6 | 7,995 | 57 / 9 / 1 |
| h256 | `cx31993` | 256 | 5 | 60 | 7,987 | 132 / 18 / 2 |
| h256m | `cx31993m` | 256 | 5 | 136 | 7,979 | 232 / 42 / 2 |
| h1024m | `cx31993m` | 1,024 | 5 | 8 | 7,995 | 136 / 14 / 1 |
| h1024x | `cx31993` | 1,024 | 2 | 2 | 7,984 | 63 / 10 / 1 |

(h256's printout counted four plays: the live snapshot's console line
crossed the second play's on COM1; the simulator's packets show five.)

The live table: on 1,024 entries the playback pipe had "a ring of 254
TRBs (its own page); 8 of 8 requests out at most" and "ring fill 32 TRBs
at least before one, 224 at most after" - seven 4 ms requests of 32 TRBs,
28 ms, the eighth held for ring room; on 256 entries "a ring of 112 TRBs
(its own page)" and "96 at most after" - three, 12 ms (both profiles; the
`cx31993m` pipe is endpoint 7, DCI 14). On 1,024 entries a play's stream
ran dry one to three times, the last at its end (`iso.dry`); on 256 entries 7
to 17 times a play (`cx31993`) and 19 to 47 (`cx31993m`). The close notes
give each play's longest publication gap: 13 to 45 ms under the load on
either ring (the simulator's own largest gap 22 to 32 ms), so a 28 ms ring
carried gaps a 12 ms ring did not. On 1,024 entries with `cx31993`
`iso.gap.lag` was the gap's own request's wait, 17.5 to 35.1 ms, measured,
the request not having waited for a record: the request that ended the
gap had sat since its claim, so the table's hint names this driver for
those gaps (the completion side delayed by the load while the ring kept
the device fed); elsewhere that wait was mostly not measured (claim and
publication on different processors, whose counters are not invariant
here, which the table says).

**The owner's devices** (usb-host passthrough, `hostbus`/`hostaddr` from
`info usbhost` at each attach, each device WinUSB-bound on the host and
logged in use and released in `devices.txt`; the same XP x64 rig, rebuilt
on a fresh overlay after the host's power trip; 3 x 60 s plays under CPU
load each, `UACWAV_NAME` set to the product name):

| Device | Speed | Ring | Ring Underrun (3 plays) | Read |
|---|---|---|---|---|
| Sound Blaster X1 (041E:3286) | High | 1,024 | 18 | under `uac2`; playback on its own page, `iso.page` allowance 254; per play the playback stream ran dry 2 to 9 times |
| Sound Blaster X1 | High | 256 | 39 | allowance 112 (`iso.page` `...0070`); playback dry 13 to 18 a play |
| Sound Blaster X4 (041E:3278) | High | 1,024 | 17 | under `uac2` (two outputs listed); allowance 254; playback dry 5 to 7 a play |
| Sound Blaster X4 | High | 256 | 182 | allowance 112; playback dry 55 to 65 a play, the longest publication gaps 28 to 53 ms |
| Logi USB Headset (046D:0A64) | Full | 1,024 | 11 | a Full Speed device, its streams on the pool ring (unchanged by this task); played |
| Logi USB Headset | Full | 256 | - | not run: the other leg using the headset held it for the 20 minutes the script waited |

Each X1 and X4 also opened a second isochronous stream on the pool ring
(a peak of two records, about 125 publications a second) which reported
65 to 101 dry events a play on both rings alike; it is not the playback
pipe this change moves. The plays ran through Sound Recorder (a 60 s
1 kHz tone at -40 dBFS on the default output): the kit's `UACWAV` names
the devices it plays by "UAC2" in their names and predates the
`UACWAV_NAME` override, while `uac2` 0.1.8 names a real device by its
product name, so the counts above are the controller's (completion code
14), with no play timings.

**Rig notes.** XHCITR's `/dump` closes with a message box, so a batch
that runs it with `start /w` waits until it is answered (the legs pressed
it from the host); `/snapshot` does not. The simulator refuses to reuse a
log file, and a shell capturing a script's output waited on the
simulator's inherited handle (both fixed in the leg scripts). Two copies
of the High Speed script ran at once for seven minutes; that run (k1024)
is void and not counted. `QS.BAT`'s `echo QS PLAYED 1>COM1` redirects
handle 1 (it printed "QS PLAYED"). The host lost power once (2026-11-07);
every leg above had ended before it but the device runs, which were rerun
from a fresh overlay.

**Left**: the Logi headset on the 256-entry ring (held by the other leg); 98 SE's
Device Manager steps and the `qemu` flavour's legs on 98 SE (the HCRST,
polled delivery, two controllers), XP's hub and UAS legs under a
corrected wizard answer, and stand-by and hibernation on any system; the
restore across the large ring and real controllers, the owner's machines.

**The no-sample `at`** (`e240cf00`, after the legs, reviewed): a close
note's `at` after `iso.gap` with no gap measured, or after `iso.dry` with
no dry event, carried 0 and XHCITR printed it as a time (seen on the High
Speed rig, `iso.dry=00000000` then `at=00000000`). The driver now never
notes a system time as 0 (one landing on it reads 1) and XHCITR prints an
`at` of 0 as "none: no such event before the close"; host vectors for both
cases (`test_snap`, `test_tr`). Gated x86 `all` and amd64 release passed,
host suites passed; release `xhci98.sys` x86 `12161683...`, amd64
`CD0E8E6B...`, `qemu` x86 `43B5BB4B...`. On the `qemu` XP session (the
step 7 disk, the rebuilt driver and XHCITR) a usb-audio play's close notes
read as before, each `at` a time; QEMU's streams always end in a Ring
Underrun after a gap, so the 0 case is read by the host vectors.

## C2 one call per run: the code and the legs (2026-11-07, w47-stor3)

The owner's decision (2026-11-06, Q1 (a)): C2 now, under the review's
conditions (every PRP checked, reuse bounded by the returned length and the
child, overflow handled, bounce and cleanup kept, host vectors, emulator
data compares on 98 SE, a gated build). Design reviewed before the code
(no DMA-boundary defect); code `609cefd3` on `w47-stor3`, reviewed and
revised in two rounds: `src/prprun.h` (`PrpRunDerive`, `PrpRunClamp`),
`EngMapNext` in `engine.c`, host test `prprun_ut`; the comment above
`EngSpanOk` rewritten to what the C2 reading showed. Gated `release`
builds x86 and amd64 "BUILD + GATES PASSED (release)" (`stor98.mpd`
SHA-256 `04695743...`, `stor98.sys` x86 `ceba827e...`).

**98 SE** (`E:\xhci98-vm\a347\stor2\s98d\`, a fresh overlay, the package
installed with `/s98vsd`, the C2 counter image: this change with the
earlier worker's test-only counters, `stor98.mpd` `41141d69...`, never
committed), in one session:

| Step | Result |
|---|---|
| Four system DLLs (MFC42, SHELL32, MSHTML, COMCTL32) copied to E: and compared (`fc /b`) | no differences |
| XHCIPERF `sweep` 64 KiB to 8 MiB with `/verify` (the verifiable pattern, the whole file checked), aligned and scattered | all 32 steps status OK (no data error) |
| XHCIPERF R1 (`seqmax`) and R3 (`sweep`), the counters | calls per page: 0.23 at 256 KiB, 0.38 at 128 KiB, 0.63 for IOS's 63 KiB pieces, 0.76 at 64 KiB, 0.97 to 1.00 at 512 KiB to 2 MiB and for single pages; answers past the page 25 to 33 % (128 and 256 KiB), 4 % (63 KiB), 0.1 to 0.2 % (512 KiB to 2 MiB), longest 256 pages |

In this session the large linear buffers were mostly not physically
contiguous (the port stated runs past the page for 0.1 to 0.2 % of the calls
at 512 KiB to 2 MiB, against 96 to 98 % in the C2 reading's session): the
buffers' physical layout varies with the session's memory, and C2 saves
calls only where the port states a run. Where it did (128 and 256 KiB,
63 KiB pieces), the calls fell to a quarter to two thirds, with the data
verified.

**10 x86** (`w10r`, the C2 gated x86 `stor98.sys` `ceba827e...` put in
place after the resume legs below, a restart): two files written and
compared, and the four files written earlier under the other image
compared: no differences. No NT counter reading was taken.

## Stor98: a resume no longer zeroes the DMA region before isolation (2026-11-07, w47-stor4)

Found by the read-only trace review of the CNS 06h latch (owner's Q2 (c),
2026-11-07): on NT, `HwFindAdapter` on a resumed or re-found device
extension called `PlatPortAcquire`, which zeroed the whole uncached region
(queues, utility buffer) before `EngInitialize` showed the controller
disabled; after a disable that was never acknowledged, a controller
returning with its old queues could DMA into memory the driver had just
reset, against Stor98 PLAN 0.10 ("trust nothing until the controller is
shown fresh"). Not a regression: the zeroing came with Stor98's entry into
this tree (`6b74d071`, 3.1.0.0) and is in 3.1.0.0 to 3.4.1.0.

**The fix** (`ee853875` on `w47-stor4`, from `b024bba1`; reviewed: no
findings): the region is zeroed on a first find only; on a resumed or
re-found extension nothing is written to it at the find, and `LcEnable`
zeroes the four queues and the utility page after RDY=0 is seen, as on
every bring-up. Stor98 PLAN's resume row says so. Gated `release` builds
x86 and amd64 "BUILD + GATES PASSED (release)" (`stor98.sys` x86
`aa7bef56...`, amd64 `b11ce323...`).

**The legs** (the package of `8679b3c9` installed on fresh overlays, then
the gated x86 `stor98.sys` put in place and the guest restarted; the
tunnelled drive's FAT volume):

| System | First start | Re-find (`tests/devcycle`: disable and enable the controller) | Resume from hibernation (`shutdown /h`, the same overlay started again) |
|---|---|---|---|
| 10 x86 (`w10r`) | stor98 running; two files written and compared (`fc /b`) | disable and enable "ok"; the files compared | the files compared, two more written and compared; stor98 running |
| 7 x86 (`w7r`) | stor98 running; two files written and compared | disable and enable "ok"; the files compared | (the guest locked at resume: the account's password typed) the files compared, two more written and compared; stor98 running |

**Not produced**: the unacknowledged-disable-then-resume case. Standby
(S3) is not offered by these guests' display drivers, so a resume is a
hibernation, which ends the emulator process: the controller comes back
from a power-on reset, never with old queues; and whether the port
hands a re-find the same device extension was not read. The emulator's
`x-rdy-clear-delay-ms=-1` (a disable never acknowledged) cannot be
carried across either.

## usb-wdm-free: uac2 Full Speed 4-byte feedback, absemu units, the cx31993m / alc4080cap Code 10 (agent wdm6, 2026-11-07)

Decisions of 2026-11-07 (coordinator, owner's overnight rule, after review):
uac2 takes Full Speed explicit feedback in 4 bytes 16.16 as well as 3
bytes 10.14; the absemu Unit/Unit Exponent patch goes onto `w47-wdm2`; a
short reading of `cx31993m` and `alc4080cap` under Windows 10's
`usbaudio2.sys`. Development legs in QEMU (TCG), simulators only; no owner
device used.

### uac2 0.1.10.0 (branch `w47-wdm6-uac` from `w47-wdm` `2d30b60`)

- `3d1a5cb`: decoded by the length received (Full Speed 3 bytes 10.14, 4
  bytes 16.16, both per frame; High Speed unchanged); 4 bytes asked where
  the feedback endpoint holds them; stride, request and allocation from
  one value; every packet checked against its stride and the slot's
  allocation before it is read, a zero-length one included; the window
  and endpoint bounds before arithmetic; a rejected value refreshes no
  deadline. Design `uac2.md` section 23. `DriverVer` 11/07/2026,0.1.10.0
  in both INFs, version resource 0.1.10.0. Reviewed and revised (one
  finding: zero-length keepalives skipped the bounds; fixed).
- Builds: `wdmx86 free` `uac2wdm.sys` `90930794...`, `wdm64 free`
  `f06bff1e...` (unsigned: needs the owner's signature). Host: `test.cmd`,
  `rtest.cmd` (98579 checks), `streamtest.cmd` pass; `ctest.cmd` and
  `mtest.cmd` cannot start their test EXE on this host
  (`NoDefaultCurrentDirectoryInExePath`; files untouched, as `rtest.cmd`
  was before its `.\` fix here).
- Legs (`docs/testing/2026-11-07-uac2-fs-feedback.md`, simulator `fs` of
  `w47-ref2` `66d99ee`, `--fb-ppm 15000`): 98 SE under xHCI98 3.4.1.0 and
  under NUSB 3.3 (on UHCI: QEMU's EHCI takes no Full Speed device), both
  feedback sizes: play 48 and 44.1 kHz, record (999 Hz tone), unplug
  during play, replug and play: PASS; the device at 44.1 kHz too; 49-frame
  packets about 71% (the feedback followed). XP SP3 under xHCI98 3.4.1.0,
  both sizes: PASS. XP's own UHCI: play FAIL with both sizes and with the
  released 0.1.8.26 too (older, not settled). Control: 0.1.8.26 on 98 SE
  xHCI98 fails the 4-byte endpoint, plays the 3-byte one.
- Open: three 98 SE ring-0 stops soon after a cold start and a first
  plug on xHCI98's port, all with an added UHCI controller; two with no
  `uac2` device; one at the first `fs` play (not established).

### absemu units (branch `w47-wdm2`)

- `792d1b7`: the patch, plus the Physical extent reset with Unit and Unit
  Exponent; `aptest.c` descriptors regenerated (93 checks pass).
  Reviewed. `988c209`: the record and a corrected comment (reviewed).
- 98 SE (absptr rebuilt, test INFs): xHCI98 3.4.1.0 and NUSB EHCI,
  `touch`, `mtouch`, `touchmouse`: every run 6/6 (first plug, two more).
- Windows 7 SP1 x86, Windows' EHCI: after the restart 7 asks for, the
  pointer follows `touch` and `mtouch` within 2 pixels with the patched
  model and with the unpatched one alike: the units are not what 7 needs;
  the restart is. (`792d1b7`'s subject says otherwise; the record and the
  comment are right.)

### cx31993m and alc4080cap under `usbaudio2.sys` (branch `w47-wdm6-ref` from `w47-ref2` `66d99ee`)

- Cause: the unanswered HID interface. Windows' HID driver stalls on its
  report descriptor and resets the whole device three times; when that
  overlaps `usbaudio2`'s start, the audio function fails (Code 10, event
  16); when it comes first (`x4full`, `x1`) audio starts.
- Model: a captured layout's HID interface answers SET_IDLE and a stand-in
  report descriptor of the declared length (`uacsimtest.py` 32 pass).
- Windows 10 22H2: `alc4080cap` Code 10 with `66d99ee`, PASS with the
  change (3 out, 2 in, play, record with the tone); `cx31993m` PASS with
  the change (no reset), and this time PASS with `66d99ee` too (the race).

## 47.33 HP EliteBook 2570p and 8470p: XP x64's Code 12 on the USB 3 controller (2026-11-06, w47-hp12)

A user's report on an HP EliteBook 2570p (2026-10-12, Phase 37's 37.4
before it was cut), reproduced by the owner on an HP EliteBook 8470p
(BIOS 68ICF F.74, 8 GB): under Windows XP x64 the xHCI98 USB 3 Controller
(Intel 7-series `8086:1E31` rev 04 at `00:14.0`) shows Code 12, "This
device cannot find enough free resources that it can use", no
conflicting device named; Windows 7 x64 (xHCI98 since `2.3.0.0`) and
Windows 10 (Microsoft's driver) start it. The 8470p's BIOS has no USB 3.0
mode setting.

### The readings

- **The owner's QUALREP** (`XHCIQUAL 3.4.1.0`, MS-DOS 6.22, before
  Windows): the xHCI's Command register `0000` (memory decode and bus
  mastering off; both EHCIs `0006`); BAR0 `D4720000` 64-bit, high half 0
  (below 4 GB, assigned); Interrupt Line 10, pin INTA; the PCH routes
  INTA to PIRQA (`D20IR` `3210`, GSI 16 in APIC mode); MSI capable, off;
  the `$PIR` table's 24 entries name no device `0x14`.
- **The owner's MADT** (saved by XHCITR `/acpi` under XP x64): one I/O
  APIC at GSI base 0, overrides for IRQ 0 (to GSI 2) and IRQ 9 (level,
  high) only. XP x64's table interface gave no DSDT, so the `_PRT` is
  not in that folder.
- **A public Linux boot log of an 8470p** (linux-hardware.org probe
  `e2a066ade3`, BIOS F.65): `pci 0000:00:14.0: enabling device (0000 ->
  0002)` (the same decode off), `can't derive routing for PCI INT A`,
  `PCI INT A: no GSI` (no `_PRT` entry for device 20; Linux's xHCI uses
  MSI), the same two MADT overrides as the owner's F.74, and root bus
  windows `BF200000-DFFFFFFF`, `F0000000-FEDFFFFF`,
  `FEE01000-FFFFFFFF`.
- **Microsoft's "Firmware Allocation of PCI Device Resources in
  Windows"** (2006): XP and Server 2003 respect a boot configuration and
  place a device below 4 GB first; they ignore a root bus's QWORD window.

So the leading guess of the brief (a 64-bit BAR above 4 GB) is out: the
BAR is below 4 GB.

### The reproduction (QEMU)

Kit `E:\xhci98-vm\a347\hp12` (`lv.ps1`, `scripts\exp.sh`, `mse.py`,
`prt.py`, `lc.py`; `pids.txt`): the q35 XP x64 SP2 base, an overlay `g0`
with `3.4.1.0` released installed (its test certificate trusted with
CertMgr; "Results: xHCI98 USB 3 Controller: xhci98 3.4.1.0, started",
exit 3), read-only, SHA-256 in `g0\disk.sha256`; each leg a fresh
overlay of `g0`; the pinned emulator `asm4242-20261025-2230`, `-smp 2 -m
2048`, `pci-hole64-size=0`, `qemu-xhci` on `pcie.0` at `00:14.0`
(`-NoXhci` plus the device), `-boot menu=on,splash-time=30000`. The
8470p's state is set while SeaBIOS waits at "Press ESC for boot menu",
after its PCI set-up and before the OS loader, the guest stopped around
each access:
- decode off: the Command register through ports `CF8`/`CFC` from QEMU's
  monitor, `0107` -> `0100` (I/O, memory and bus master cleared, SERR
  kept; the HP reads `0000`);
- no `_PRT` entry: the DSDT in RAM through QEMU's gdbstub in physical
  mode, the eight `_PRT` package entries with address `0x0014FFFF` (four
  pins, PIC and APIC tables) renamed `0x0034FFFF` (no such device), the
  checksum fixed (read back: sum 0).
Then the desktop, the Found New Hardware wizards cancelled, `R.BAT`:
`wmic` problem codes and the xHCI's `Enum\...\LogConf` values, decoded
by `lc.py`.

| Leg | Decode | `_PRT` entry | Code | BootConfig | AllocConfig |
|---|---|---|---|---|---|
| g0 (control, the install) | on | kept | 0 | memory `FEBF0000`, IRQ 10 | memory `FEBF0000`, GSI 20 |
| a1 | off | kept | 0 | none | memory `FEBFC000` (placed afresh), GSI 20 |
| a2 | on | removed | 0 | memory, IRQ 10 | memory `FEBF0000`, IRQ 10 |
| a3 | off | removed | **12** | none | none |
| a4 | off (both boots) | removed (both boots) | **12** with `/PCILOCK` in BOOT.INI | none | none |

The PCI driver's requirement list (BasicConfigVector) in a1, a3 and a4:
one list, Memory 4000 align 4000 0..`FFFFFFFFFFFFFFFF`, DevicePrivate,
Interrupt shared 0-255; in g0 and a2 the boot configuration's BAR
preferred before the same alternative. a3's Device Manager page
(`shots\a3-props1.png`): "This device cannot find enough free resources
that it can use. (Code 12)" with no conflict named, as on the 8470p.

**The cause, as reproduced:** XP's PCI driver gives a device whose
decodes are off no boot configuration (a1), so its memory is placed
afresh without trouble, but its line can then come only from the
firmware's `_PRT`. With an entry missing, the boot configuration's IRQ
(the Interrupt Line register) still serves (a2); with both, no line can
be offered, and the one configuration fails whole: Code 12. `/PCILOCK`
does not help. Windows 7, 10 and Linux take MSI and need neither. Not
excluded by the legs: an HP-specific memory reservation (the owner's
Resources tab names no conflict, and the HP's root windows leave room),
and the 2570p's firmware, not read.

### The fix

`38b36c2b` (code, vectors, design record 18 revision 14, section 16),
designed first in a scratch note, reviewed and revised (the design over
two rounds, the code with no material findings).

- `IRP_MN_FILTER_RESOURCE_REQUIREMENTS` on Windows 2000, XP and XP x64
  (an NT kernel below 6.0; 98 and ME still pass it down untouched; Vista
  and 7 unchanged): for the shape the PCI driver gives (one list; no
  message interrupt, no ConfigData, homogeneous runs), a second list is
  appended, the first with its interrupt removed
  (`XhciMsiAppendNoIntr`). The list with the line stays first and is
  taken wherever the line can be given.
- Where it cannot, the start gets the memory alone and takes polled
  delivery, record 15's mode 0 path.
- `XhciInterruptMode` 1 and 3 ask for no copy; the mode cache is now
  filled on NT 5 too. `hcdParseResources` clears the translated
  interrupt fields at entry. Filter outcome 13, named by XHCITR.

### The builds and the legs of part 1

Gated release builds of `38b36c2b`, x86 and amd64: BUILD + GATES PASSED,
host tests passed (`E:\xhci98-scratch\w47\hp12\gated-x86-release.log`,
`gated-amd64-release.log`); `xhci98.sys` x86 `8123a1fb...`, amd64
`d61d7bc7...`. Each leg: an overlay of the golden, the 8470p's state set
at the menu, `3.4.1.0` read, then the new `xhci98.sys` copied over the
installed one (`SWAP.BAT`, `fc /b` no differences), restart, the state
set again, the reading.

| Leg | System | Driver | Code | FilteredConfigVector | AllocConfig | Delivery (XHCITR snapshot) | USB stick |
|---|---|---|---|---|---|---|---|
| xa3 | 32-bit XP SP3 (golden `gx`, `3.4.1.0` installed, exit 3) | `3.4.1.0` | **12** | 1 list | none | - | - |
| xa3n | the same | `38b36c2b` x86 | 0 | 2 lists (208 bytes) | memory `FEBFC000` alone | POLLING, "automatic: no interrupt resource was granted", filter outcome 13 | read (D:) |
| xb3 | XP x64 SP2 (golden `g0`) | `3.4.1.0` | **12** | 1 list | none | - | - |
| xb3n | the same | `38b36c2b` amd64 | 0 | 2 lists | memory `FEBFC000` alone | POLLING, the same reason, outcome 13 | read (D:) |
| xb3m1 | the same, `XhciInterruptMode` 1 | `38b36c2b` amd64 | **12** | 1 list (no copy, as designed) | none | - | - |
| xc0 | XP x64, the control state (decode on, `_PRT` kept) | `38b36c2b` amd64 | 0 | 2 lists | memory `FEBF0000`, **GSI 20**: the PCI driver's own configuration still wins | - | read (D:) |

`XHCITR /snapshot` in a batch run from the console needs no `start
/wait` (the tool returns before writing); the kit's `SNAP.BAT` waits 45 s.

### Part 2: the chipset-routed GSI offer (2026-11-07)

The owner's decision of 2026-11-06, after a review of the GSI 16 option:
keep the polled fallback as the base and add, on XP x64, a configuration
constrained to the I/O APIC input the PCH routes INTA to, between the PCI
driver's own and the memory-only copy, behind `XhciIntelNoRouteIrq` (0
never, 1 an HP allowlist compiled empty until the owner's bench passes, 2
any reviewed chipset). `8c06e6e1`, designed first (a scratch note,
reviewed: the PDO's location and the controller's identity proven before
any chipset read, the clone only for a shared level-sensitive line, the
offer recorded only after a rewrite), the code reviewed over three rounds
(the last with no findings). Design record 18 revision 15 (section 16.1),
`tuning.md`, both INFs (`XhciIntelNoRouteIrq` at 1, no-overwrite) and the
INF gate.

The owner's own 8470p tables (F.74, saved under Linux) and the HP range's
latest BIOS images read for facts only: no `_PRT` entry for device 20 in
any of 68ICF/68ICE, 68IAV, 68ISB or 68IMT, in the APIC or the PIC table;
fourteen routing entries on GSI 16 (PIRQA) on the 8470p, both EHCIs among
them; the MADT overrides only IRQ 0 and 9. The firmware routes the ports
to the xHCI (`XUSB2PR`, `USB3_PSSEN`) only from `_OSC` for Windows 8 and
later [inf].

**Builds** (gated, after a power cut on the host restarted them): amd64
`all`, BUILD + GATES PASSED (release qemu), host tests passed, the INF gate
passed (`gated-amd64-all-p2.log`): release `370334fc...`, qemu
`edf94b78...`, `XHCITR64.EXE` `800af2fe...`; x86 release, BUILD + GATES
PASSED (`gated-x86-release-p2.log`): `1a22cc22...`.

**Legs** (XP x64, an overlay `p2` of `g0`, the part-2 qemu build copied
over the installed driver; each leg's values written by `SETV.BAT`, a
restart, the 8470p state at the menu, `R.BAT`, and an `XHCITR /snapshot`
typed at a Command Prompt: run from a batch, `XHCITR64.EXE /snapshot` stays
in its own window on XP x64). QEMU wires 00:14.0's INTA to GSI 20; the
qemu value `XhciQemuNoRouteGsi` stands in for the chipset read, which needs
a reviewed PCH.

| Leg | State and values | Code | Lists | Granted | Delivery (snapshot) |
|---|---|---|---|---|---|
| p2n | control (decode on, `_PRT` kept), none | 0 | 2 (no GSI offered: `noroute.decision` 5, not a reviewed xHCI) | memory, GSI 20 (the PCI driver's own) | line, `noroute.grant` 2 |
| L1 | 8470p state, GSI 20 | 0 | 3: own, GSI 20-20, memory | memory, **GSI 20** | line, outcome 14, `noroute.offered` 1, `grant` 1, `LineMoved` 0; a stick read (D:) |
| L2 | 8470p state, GSI 60 (beyond the I/O APIC) | 0 | 3 | memory alone | POLLING, "no interrupt resource was granted", `grant` 0 |
| L3 | 8470p state, GSI 20, `XhciQemuLineConnectFail` 1 | 0 | 3 | memory, GSI 20 | POLLING, reason 20 "the line granted could not be connected", `intr.line.connect.status` C0000001 |
| L4 | 8470p state, GSI 21 (no device asserts it) | 0 | 3 | memory, GSI 21 | POLLING, "the line interrupt went dead; moved to polling at run time", `LineMoved` 1 |
| L6a | 8470p state, GSI 60, `XhciInterruptMode` 1 | **12** | 2: own, GSI 60-60 (no memory copy, as designed) | none | - |
| L6b | 8470p state, GSI 20, `XhciInterruptMode` 2 | 0 | 2: own, memory (no clone) | memory alone | POLLING, "forced polling", outcome 13 |
| L6c | 8470p state, GSI 20, `XhciIntelNoRouteIrq` 0 | 0 | 2 | memory alone | POLLING, `noroute.decision` 2 (off), outcome 13 |

**L5, sharing** (the 8470p state, GSI 20, a UHCI added): at 00:10.0 the
UHCI's INTD reached GSI 23, so nothing was shared; there 60 copies of a
1 MiB file through the xHCI (line, GSI 20) each compared equal while a USB
tablet on the UHCI moved every 50 ms (2333 moves). At 00:0D.0 its INTD
reaches GSI 20 (QEMU's `_PRT` link `GSIE`), and once the UHCI had its
driver the PnP manager **did not grant** the offered GSI 20 beside it:
the xHCI took the memory-only configuration and polled (`noroute.grant`
0, "no interrupt resource was granted"), Code 0. So a refused offer falls
back safely; whether XP's arbiter shares an offered GSI with devices the
firmware routes there (on the 8470p both EHCIs on GSI 16, named directly
by the APIC `_PRT`, not through link devices as QEMU does) is for the
owner's bench. A storm from an unserviced sharer was not tried.

Not run here: a storm from an unserviced sharer (needs a debugger), 32-bit
XP and 2000 with part 2 (the offer is x64-only by design; the x86 build's
part-1 path is unchanged since `38b36c2b`'s legs), and everything on the
real chipset: the owner's 8470p checklist.

### Left

- The owner's reading on the 8470p with the part-2 build (the checklist
  in the worker's report): the polled default, the ports' routing with
  `XhciIntelPortSwitch` 0 and 1, and with `XhciIntelNoRouteIrq` 2 the
  grant, the I/O APIC's trigger and polarity, ISR delivery, busy
  sharers, cold and warm boots, disable and enable; if it passes, the
  allowlist compiled in (`XHCI_PIRQ_NOROUTE_LIST`). The reporter's 2570p.
- A Windows 2000 leg of part 1.

## wdm6's 98 SE stops after a cold start: Windows 98 SE's audio-load wedge, 36.41 (worker hang98, 2026-11-07)

**The lead.** Worker wdm6 (run 47, usb-wdm-free legs) saw three Windows 98
SE guests stop soon after a cold start and a first plug on `xhci98.sys`'s
port, all with a `piix3-usb-uhci` added beside `qemu-xhci` and `usb-ehci`:
one at the first play of the `uacemu` `fs` device, two in the Add New
Hardware wizard of an `absemu` touch device (one of them after `uacemu`
devices had played in the same boot). wdm6 sampled EIP `FF0270ED` and
`C00xxxxx` on one of them.

**The vehicle** (development host A, QEMU 11.1.0 fork `cb36497` as wdm6
ran it, `qemu-system-x86_64.exe` SHA-256 `3e7f6bf5...812d`, TCG, `pc`
machine, one `pentium3,-apic` processor, 256 MB). wdm6's command line
exactly (`E:\xhci98-vm\a347\hang98\l98.ps1`, from wdm6's `l98.ps1`):
`qemu-xhci,p2=4,p3=4` + `usb-ehci` + `piix3-usb-uhci` (or not), the
transfer folder as D:, the 98 SE CD as E:. The disk is wdm6's base,
wdm's `ser\u\us\disk.qcow2` (98 SE + NUSB 3.3 + `USBHUB.SYS` + xHCI98
`3.4.1.0` + `uac2` 0.1.8.26), read-only, under read-only overlays of this
worker's own: `prep` (the UHCI controller's driver installed by its
wizard), `prep2` (`prep` + the `uacemu --profile fs` device installed
from the 0.1.8.26 INF), `prept` (`prep2` + the 3.4.2.0 tip's `release`
`xhci98.sys`, `520098e2`, `83baef85...`, 343,327 bytes, copied over
`SYSTEM32\DRIVERS\XHCI98.SYS`), `prepq` (the same with the tip's `qemu`
flavour, `2b367a09...`, 407,236 bytes). `3.4.1.0`'s `xhci98.sys` is
`f67f2bd2...`, 337,499 bytes. One cycle (`scripts\cyc.sh`): a fresh
overlay, a cold start, 150 s to the desktop, then the first plug on
`xh.0` port 1, then a liveness probe after each step (`scripts\chk.sh`:
QEMU's CPU seconds over 10 s, eight EIP samples, Ctrl+Esc between two
screenshots). Modes: `uac` = `uacemu --profile fs --fs-feedback 3
--fb-ppm 15000` (usb-wdm-free `w47-ref2`), then `sndrec32 /play /close
TADA.WAV` twice from Start > Run; `uac` two = the same plus QEMU's
`usb-tablet` on port 2 at the same moment (the lead worker uaw reported);
`abs` = `absemu --profile touch --speed high` (`w47-wdm2`) and wdm6's
wizard walk (both its "display a list" and "search" orders). A wedge is
the Start menu not opening across the two plays' probes; on each, 12 more
EIP samples and a Ctrl+Alt+Del screen.

**Readings** (`E:\xhci98-vm\a347\hang98\ev\chk.txt`, shots in `shots\`):

| Driver | Extra UHCI | First plug after the cold start | Wedged |
|---|---|---|---|
| `3.4.1.0` `release` | yes | `uacemu fs`, then play | 2 of 3 (`c4` live; `c5`, `c6` with the tablet beside it) |
| `3.4.1.0` `release` | no | `uacemu fs`, then play | 1 of 3 (`d5`; `d4` and `d6` with the tablet live) |
| tip `release` | yes | `uacemu fs`, then play | 2 of 6 (`t5`, `t6`) |
| tip `release` | no | `uacemu fs`, then play | 2 of 4 (`tn3`, `tn4`) |
| tip `qemu` | yes | `uacemu fs`, then play | 1 of 4 (`k4`, kept) |
| `3.4.1.0` `release` | yes / no | `absemu touch`, wizard walked | 0 of 4 / 0 of 2 |

Audio arrival: `3.4.1.0` 3 of 6, the tip 5 of 14 (36.41 read 23 of 39
on `xhci98` and 14 of 30 on Windows 98 SE's own UHCI stack). In every
live run the device played (`iso_stream` in the simulator's log); `c3`
(the device's first install, a wizard) is not counted.

**Every wedge is the same state, and it is 36.41's.** On each of the
nine wedges the frame froze right after the arrival (the taskbar
clock stopped, no Run dialog, no speaker icon), the simulator saw the
same 24 requests a live run sees before its play and then nothing more
(no alternate setting, no stream), Ctrl+Alt+Del listed Explorer, Xhcitr,
Systray and **"Msgsrv32 [Not responding]"** (`d5-cad.png`, `t5-cad.png`,
`tn3-cad.png`, ...), and every EIP sample was in VMM's own code,
`C00036xx` and `C000A4D8` to `C000A933` (with `C005xxxx`, `C0001100`,
`C000CBxx` and V86 addresses once each), and QEMU busy (11.5 to 11.8 s
of CPU per probe of about 11 s, against about 1 s for a live idle guest
sitting at `FF0270ED`; run 36 called its wedge a deadlock rather than a
loop and did not measure the CPU, so here VMM is at least polling while
nothing progresses). That is run 36's "Windows 98 SE audio-load wedge"
(roadmap-hcd 36.41, `runs/run-36.md`) to the address: the same VMM
range, the same Msgsrv32, and 36.41 read it on Windows 98 SE's own UHCI
stack with `xhci98.sys` not loaded at a rate within the spread of
`xhci98`'s; it is documented as Windows 98 SE's own. Worker uaw's freeze
(EIP `C00036FF` / `C000A882`, a real X4 and a tablet plugged after a
cold start, no UHCI) is the same state. `FF0270ED`, the EIP wdm6 sampled,
is the idle loop of a live 98 SE guest here, as run 36 found.

**The kept wedge** (`k4`, the tip's `qemu` flavour, `ev\k4-e9-wedge.log`
against the live `ev\k1-e9-live.log`). The bus log is the live run's
line for line up to the audio PDO's QUERY_CAPABILITIES (minor `0x09`) and
QUERY_PNP_DEVICE_STATE (`0x14`), every transfer answered (`0x10`
submitted, `0x10` completed); a live run then takes a run of
QUERY_INTERFACE (`0x08`) from the audio stack and the play, the wedged
one asks the bus nothing more. A `usb-tablet` hot-added during the wedge
was reset, addressed, read and given its PDO by `xhci98.sys` (the bus
alive), and Windows never sent it a PnP IRP. The simulator's view agrees:
in all nine wedges and in the live runs the device saw the same 24
requests (descriptors, `SET_CONFIGURATION`, the Clock Source and Feature
Unit reads); live runs go on only when the play begins. Where the code
is: `xhci98.sys`'s own string located its image in that boot's memory
(`scripts\findimg.py`, the tree's own binary and guest memory only):
`.text` at `FF03ED40`-`FF096286` in `k4`'s boot. Of the 188 EIP samples
taken on the nine wedges none is in the `FF0xxxxx` region where the
driver sits; 186 are in `C0001100`-`C005340E` (most `C000A881`/`C000A882`,
`C00036FF`, `C0003621`), 2 are V86 (`F000:xxxx`).

**What it is not.**
- Not the added UHCI controller: without it the wedge came on `3.4.1.0`
  and on the tip alike. Routing, from QEMU's `info pci` and the `pc`
  machine's swizzle: `qemu-xhci` (00:03.0, INTA) and `usb-ehci` (00:04.0,
  INTD) both on PIRQC, `piix3-usb-uhci` (00:05.0, INTD) on PIRQD, and
  `info pci` read IRQ 11 on all three during the boot (SeaBIOS routes
  PIRQC and PIRQD both to 11; 98 SE's own routing was not read in the
  guest), so the line is shared with the EHCI in every configuration
  and with the UHCI too when it is present; 36.41's own `xhci98` base
  had no EHCI or UHCI at all and wedged as often. The sharing is not the
  trigger.
- Not new in 3.4.2.0: `3.4.1.0` and the tip both wedge, at rates these
  counts cannot tell apart (and both within 36.41's 23 of 39).
- Not the HID wizard: the `absemu` touch arrival with the wizard walked
  as wdm6 walked it, 0 of 6. wdm6's two wizard stops are not reproduced;
  one of them followed `uacemu` plays in the same boot.

**Verdict.** Not an `xhci98.sys` defect; the documented Windows 98 SE
audio-arrival limitation (36.41), on `3.4.1.0` and on the 3.4.2.0 tip,
with or without the extra UHCI. Nothing here holds the cut.

## The 98 SE usbaudio fault at unplug while playing (2026-11-06)

Worker uaw, branch `w47-uaw` from `7c995164`. A design study: no driver
code is committed. The design note is design record 13 section 18
(`e0365409`, `29d9994f`; reviewed and revised).

**Rig.** QEMU `cb36497` (the usb-redir build, SHA-256 `3e7f6bf5...`),
headless, one vCPU a guest, `-audiodev none`; QEMU's `usb-audio` (UAC 1.0,
Full Speed, `46F4:0002`) plugged and removed by QMP `device_add` /
`device_del`. Sound Recorder plays `D:\LONG.WAV` (120 s, SHA-256
`36d972b5...`) by `sndrec32 /play /close`. Each step ends with a screendump
(a full-screen fault screen counts as a fault) and, at a fault, QEMU's
`info registers`. Kit `E:\xhci98-vm\a347\uaw\` (`scripts\`, `ev\`,
`shots\`, `pids.txt`, `STATE.md`). Guests are overlays of copies of uaud's
`af1` (98 SE, q35, xHCI98 3.4.1.0, `xhci98.sys` `f67f2bd2...`), uaud's `n`
(98 SE, pc machine, NUSB 3.3 with Windows' UHCI driver) and wdm5's `me2`
(ME, q35, xHCI98 3.4.1.0); ME's own UHCI stack is the same ME image
started with no `qemu-xhci` and a `piix3-usb-uhci`. Test builds of
`xhci98.sys` (the `qemu` flavour, x86, uncommitted; a DDK build and the
import gate only, not the gated build): `80aadf6a...`, the 3.4.2.0 tree
plus a bounded port-0xE9 trace of isochronous completions, PnP minors with
their pending counts, the URBs a departed PDO receives and the frame
numbers answered; `111b8dea...` candidate 1; `fd398062...` candidate 2;
`a6298223...` and `59102812...` candidate 3 (below). Each was put in place
by `copy` and a cold start.

### 1. 98 SE: the fault is the play, not the unplug

uaud's round 1 never played (Sound Recorder: "a sound device is not
installed", `shots\old-x1-s2u1.png`), so its three passes were idle
unplugs. Round 2 was the first play, and its screenshot came 27 s after
the play started and 15 s after the unplug. Read again with no unplug:

| Leg | Stack | Steps | Result |
|---|---|---|---|
| `c0` | xHCI98 3.4.1.0 | a first plug on a new port, then play | "A fatal exception 00 has occurred at 0028:FF09FED4 in VXD USBAUDIO(01) + 00002ED4" within 12 s of the play (`c0-play1.png`) |
| `nc1b` | NUSB 3.3, Windows' UHCI | cold start, plug, play | "A fatal exception 0E has occurred at 0028:FF06A848." within 7 s (`nc1b-w1.png`); CR2 `007004A9` |

That is the Phase 9 finding (lessons.md, "Windows 98 SE cannot play USB
audio here"): 98 SE's `usbaudio.sys` on QEMU's `usb-audio` stops 98 SE once
it streams, under either stack and at the same addresses as uaud's round 2
(`FF09FED4`, `FF06A848`). A real device played on real xHCI hardware under
98 SE (batch 13-E, finding X, the miniport), so this is the guest's
limitation with that emulated device, and an unplug while playing cannot
be read on 98 SE with it. The other 98 SE plays here ended in Sound
Recorder's "Your audio hardware cannot play files like the current file"
or "Another application is playing audio" with no isochronous URB sent,
so the trace build never saw 98 SE stream and the divisor of `+2ED4` was
not read.

**The NUSB fault's module (question 1): not named.** The fault screen names
no VxD; QEMU's `info mem` shows one mapping from `FF000000` to `FF11F000`
around the address, the region the WDM images share; CR2 (`007004A9`) is a
data address, not the faulting EIP, so this is no call into a freed image.
Naming the image would need its headers, which are not read. The address
repeated exactly on two guests (uaud's `n2`, this `nc1b`).

### 2. ME: an unplug while playing stops it, on Windows' own stack too

ME's `usbaudio.sys` plays QEMU's device (the position advances,
`m1-p.png`). An unplug about 12 s into a play, then 15 s:

| Stack and build | Valid unplugs | Stops |
|---|---|---|
| xHCI98 3.4.1.0 release (`me1`, `mer1`, `r0`, `r0b`) | 20 | 5: "An error has occurred ... NTKERN(01) + 0000EECA Error: 0E : 0028 : C002EFDA", CR2 `00310044` each time |
| the trace build, hold as 3.4.2.0 (`met1`, `met2`) | 2 | 1, the same |
| ME's own UHCI stack, no xHCI98 (`u1`) | 10 | 5: "Error: 0E : 0028 : 0B70001D", CR2 `0B70001D` (a call through a bad pointer) |
| candidate 1 (`c1`, `c1b`) | 20 | 0 |
| candidate 2 (`c2`) | 20 | 0 |
| candidate 3 (`c3`, `c3x`, `c3t`) | 26 | 0 |

A trial counts when Sound Recorder's waveform is on screen at the unplug
(`scripts\playok.py`, more than 400 of its green pixels); after a fault's
cold start the next trial's keys sometimes went to the desktop instead,
and those trials are not counted. No idle unplug stopped either system
(uaud's six; every trial's own plug and unplug here).

**What the bus does at the removal (question 2)**, read from the trace
build (`ev\met1-dec.txt`, `ev\met2-dec.txt`; TSC units, about 0.35 ms
each here):

1. ME's `usbaudio.sys` aborts and resets its isochronous OUT pipe at the
   unplug and resubmits; four URBs of ten packets are on the ring.
2. About 2 s later the port's disconnect reaches the bus. Record 13
   section 10.5's teardown holds the four on the PDO (`HcdIoPark`).
3. About 0.3 s later, after the root hub's relations, ME's `usbaudio.sys`
   sends SELECT_CONFIGURATION to the departed PDO (refused at the next
   tick) and ME sends the REMOVE, the four still pending.
4. The PDO's REMOVE completes the four `USBD_STATUS_CANCELED` / 
   `STATUS_CANCELLED` one after another inside the REMOVE dispatch. In
   `met2` the trace ends at the fourth completion's marker, before the
   release's own line, and ME is at the NTKERN fault screen. In `met1` the
   same sequence survived.

What the requests carry:

| | Held at the departure, then released | Submitted after the departure |
|---|---|---|
| URB header | `DEVICE_GONE` while held, `CANCELED` (`0x00010000`) at the release | `DEVICE_GONE` |
| IRP | `STATUS_CANCELLED`, Information 0 | `STATUS_DEVICE_NOT_CONNECTED`; `STATUS_DELETE_PENDING` once the PDO closes |
| `NumberOfPackets`, `StartFrame` | as submitted (10; an ASAP start as the bus chose it) | as submitted (0 for ASAP) |
| packets | answered ones as answered; the rest `DEVICE_GONE`, `Length` 0 (OUT) | all `DEVICE_GONE`, `Length` 0 |
| `ErrorCount` | packets failed (5 or 10 here) | `NumberOfPackets` |
| `TransferBufferLength` | the bytes answered (`0x3C0` for one five packets in; 0 for one not started) | as submitted (`0x780`) |

No zero packet count, zero packet table or zero frame interval is
returned, and none of these contradicts Windows 2000 DDK `usbdi.h`
(`Length` written for IN only, `ErrorCount` the failed packets). What
Windows' UHCI stack returns was not read (no filter above the bus); that
it stops ME as often is what the control shows.

### 3. The candidates (test builds only, question 3)

- **Candidate 1, an isochronous request not held, on every target.** At
  the departure an isochronous URB completes `DEVICE_GONE` /
  `STATUS_DEVICE_NOT_CONNECTED`, the ring's at once and a waiting one at
  the next tick, as before 27-V.1. ME's `usbaudio.sys` then aborts, resets
  and resubmits, each URB refused at the next tick, until its REMOVE, and
  nothing of its stream is held at the REMOVE (`ev\c1-dec.txt`).
- **Candidate 2, the hold released at a SELECT_CONFIGURATION on the
  departed PDO.** The hold kept, `CANCELED` kept; on ME that select comes
  about 0.17 s after the departure and before the REMOVE; `usbaudio.sys`
  then aborts, resets and resubmits once (refused) and the REMOVE comes
  with nothing pending (`ev\c2-dec.txt`). It keeps the status and moves
  only the time, so its result says the time, not the status, matters.
- **Candidate 3, candidate 1 as proposed** (record 13 section 18.4): 98 SE
  and ME only, a request an abort covers still `CANCELED`, the isochronous
  test read before the record is released. Its retry loop until the REMOVE,
  measured (`ev\c3t-dec.txt`, PnP and departure events unbounded): the
  REMOVE came 2.0 s and 2.4 s after the departure (0.3 s with the hold, 0.22
  s with candidate 2), and before it ME's `usbaudio.sys` sent at least 111
  aborts, 111 resets and 1,189 isochronous URBs, each refused at the next
  tick (a lower bound: the trace's budget ran out).
- Not tried: keeping each packet's `Length` as submitted (`Length` is unused
  for OUT, and the stop is not at a field the bus writes); completing after
  the REMOVE (no IRP may outlive it).

### 4. Not applicable or not read

- usb-wdm-free's `uac2` serves UAC 2.0 only, so not QEMU's `usb-audio`. With
  the hold, 98 SE survived an unplug during `UACPLAY` (usb-wdm-free
  `docs/testing/2026-10-22-uac2-98se.md`); not read with a candidate.
- 98 SE with a candidate: not readable on this device (section 1).
- Real hardware: owed.

### 5. Owner options (record 13 section 18.6)

- **A. Not held on 9x** (candidate 3): no stop in 26; the REMOVE about 2 s
  late behind a retry loop of several hundred refused URBs a second; NT
  untouched.
- **B. Released at a departed PDO's SELECT_CONFIGURATION, isochronous
  only, 98 SE and ME** (candidate 2 narrowed, with a latch so a late
  request is not held either; not yet built): no stop in 20 in its test form; the REMOVE as prompt as today, no
  retry loop; hidclass and every non-isochronous pipe as today; a client
  that does not select before its REMOVE is left as today.
- **C. No change**, a known limitation for ME (and, unproven, 98 SE): "stop
  playback before unplugging a USB audio device". ME's own USB stack stops
  ME in the same act (5 of 10).

Recommended: B, then 98 SE on real hardware to see whether its
`usbaudio.sys` selects before the REMOVE as ME's does (if it does not,
A covers it at the retry loop's cost).

**Owed:** the owner's choice; with A or B the code (record 13 section 18.4 or 18.5),
its review, the gated build and legs: ME as here; 98 SE with a real UAC
1.0 device on real hardware; usb-wdm-free `uac2` unplug during play on 98
SE; 2000 and XP `usbaudio.sys` under B.

### 6. A real UAC 1.0 device: the Logi USB Headset (2026-11-06/07)

The owner's Logi USB Headset (`046D:0A64`, UAC 1.0, Full Speed, a composite
of audio and HID) passed through to QEMU by `usb-host` on xh.0 port 4
(`scripts\hp.sh`, every attach in `devices.txt`); unplug by QMP
`device_del` about 12 s into Sound Recorder's play; the host's power trip
of 2026-11-07 cut leg 3 once (it was rerun on a fresh overlay).

| Leg | Build | Result |
|---|---|---|
| 98 SE plays it | 3.4.1.0 | yes: the position advances (`l1-p.png`); 98 SE's own stack (NUSB 3.3, Windows' UHCI) leaves it in "Other devices" with no driver, so there is no Windows control on 98 SE |
| 1: 98 SE, unplug while playing | 3.4.1.0 | the first unplug froze the guest: its clock stopped at that minute (`l1-play2.png` to `l1-play20.png` are the same frame), so the 19 later trials never played and the "0 faults in 20" the script logged is void |
| 2: 98 SE, the trace build | baseline hold | the same freeze at the first unplug; at the departure 3 isochronous URBs were held; 98 SE's `usbaudio.sys` sent ABORT_PIPE (not SELECT_CONFIGURATION) to one function PDO, which had nothing held, and that PDO alone got its REMOVE; no PnP IRP reached any device PDO again (`ev\l2-dec.txt`) |
| 3: ME, unplug while playing | 3.4.1.0 | 4 stops in 20 valid unplugs (`trials-l3b.txt`), plus 1 in 2 before the power trip: "NTKERN(01) + 0000EECA Error: 0E : 0028 : C002EFDA", as with QEMU's device |
| 98 SE, isochronous requests not held (candidate 3, option A's form, a test build) | test | playing at 17.75 s, then at the unplug ME's retry pattern (abort, reset, three refused URBs, again) and "A fatal exception 0E has occurred at 0028:C001545A" (CR2 `00C90FA6`, `ha1t2-u.png`) |

So 98 SE does fail at an unplug while playing a real UAC 1.0 device under
xHCI98: frozen with today's hold, a fatal exception with the hold lifted
for isochronous requests. It sends no SELECT_CONFIGURATION to the departed
PDO before its REMOVE while playing (it did once at an idle unplug,
`ev\ha1-dec.txt`), so option B does not reach it. By the owner's rule the
work stopped here and is reported; option B's own legs were not run.

**Option B's state:** committed `459730a5` (code reviewed and revised to
approval, the commit reviewed with no findings), the gated build of it
passed (x86 and amd64, release and qemu; x86 release xhci98.sys
9FD1F608..., qemu 945F94FA...). Its legs (ME active and idle unplugs, root port and hub; uac2,
uvc98, composite, MIDI regressions; races; 2000 SP4 with Verifier; XP
audio removal) are not run.

### 7. Option B's legs on its gated image, and the 98 SE freeze traced (2026-11-07)

Image: `459730a5`'s gated `release` x86 `xhci98.sys` (`9fd1f608...`) with
its `xhcihlpr.sys` (`eb4cb86c...`), put in place by `copy` and a cold
start; for the trace, that tree plus a port-0xE9 trace that names the PDO
of every hold and completion (`f12047d4...`, uncommitted,
`E:\xhci98-scratch\w47\uaw\traceB.diff`).

| Leg | Result |
|---|---|
| ME, QEMU `usb-audio`, unplug 12 s into a play at a root port (`mb1`) | 0 stops in 20 valid unplugs (3.4.1.0: 5 in 20) |
| ME, idle plug, play 8 s, close, unplug (`mbi`) | 5 of 5 played, no stop |
| ME, the device behind a QEMU hub, the hub unplugged while playing (`mbh2`) | 0 stops in 8 valid unplugs (a first run, `mbh`, is void: the audio device behind the hub was not yet installed) |
| 98 SE, the Logi, idle plug, play, close, unplug (`sbj`; control `sri` on 3.4.1.0) | B: 2 of 3 played (the first play after a cold start ends in "Your audio hardware cannot play files like the current file" on either build), the shell answering after each unplug; 3.4.1.0: 3 of 3. A first B guest (`sbi`) never played ("Another application is playing audio": a Sound Recorder left open by the setup's keys), not counted |
| 98 SE, the Logi, unplug while playing (`sbh1`) | frozen, as 3.4.1.0 |
| uac2 (X1/X4), uvc98, MIDI and bulk regressions, race checks, 2000 SP4 with SMP and Verifier, XP audio removal | not run (time; iso2 held the X4) |

**The 98 SE freeze, traced** (`ev\st1-dec.txt`; the Logi is a composite:
function MI_00, interfaces 0 to 2, the audio, `usbaudio.sys`; MI_03, the
HID):

1. At the departure the bus holds 3 isochronous URBs on the audio PDO and
   2 interrupt reads on the HID PDO.
2. About 0.7 s later the HID's two reads are cancelled by their client
   (completed through the cancel routine, before any abort), hidclass
   sends ABORT_PIPE, and the HID PDO gets its REMOVE, which completes.
3. Nothing ever reaches the audio PDO again: no SELECT_CONFIGURATION, no
   ABORT_PIPE, no cancel, no PnP IRP. Its 3 isochronous URBs stay held,
   and the guest's display and clock stop.

So 98 SE's `usbaudio.sys`, handling its removal (or the removal's query)
above the bus, waits for its outstanding stream requests before it passes
anything down; the bus holds those requests until that client aborts,
cancels, selects or passes the REMOVE down. Each waits for the other. ME's
`usbaudio.sys` selects first and does not wait, which is why option B
reaches ME and not 98 SE. With the hold lifted at the departure (candidate
3) 98 SE does not deadlock but stops with "fatal exception 0E at
0028:C001545A" in the client's abort, reset and resubmit loop (section 6).

**Options for 98 SE (not coded):**

- A: release a departed device's held isochronous requests `CANCELED` once
  PnP has been told (when the root hub's relations answer omits the PDO),
  on 98 SE and ME, latched as option B latches (no request held after),
  new submissions refused at the next tick as now. It breaks the wait
  without a timer, keeps `CANCELED` (the status ME took without a fault),
  but leaves 98 SE's resubmission loop to be read: candidate 3's crash came
  in that loop with `DEVICE_GONE`, so whether `CANCELED` ends it is the
  question.
- B: as A, and a departed PDO's isochronous resubmissions are also
  completed `CANCELED` rather than refused `DEVICE_GONE` (the status a
  client treats as a stop, not an error), to keep 98 SE out of the loop.
  More change; ME's behaviour with it unread.
- C: keep option B (ME) and make 98 SE's case a known limitation: "stop
  playback before unplugging a USB audio device".

Recommended: read A first as a test build on 98 SE and ME (a few unplugs
each, the trace showing whether 98 SE's `usbaudio.sys` stops resubmitting
after `CANCELED`); if it does not settle, C. Both need the owner's word,
as A releases on an event rather than the client's own request.

### 8. Option A as a test build, and option B's further legs (2026-11-07)

**Option A (test build, uncommitted, `3e457487...`,
`E:\xhci98-scratch\w47\uaw\optA-test.diff`):** on 98 SE and ME, once a
root-hub relations answer first omits a gone PDO, that PDO (pinned by its
`Busy` count) has its departed isochronous requests completed `CANCELED`
and is latched as option B latches; on top of `459730a5` and the per-PDO
trace. Reviewed before running for DMA safety: no defect (the requests it
completes are taken only after the drain's unmap, and the latch completes
later ones at their park); one P2, a 16-PDO cap per answer that a real
build would lift.

| Leg | Result |
|---|---|
| 98 SE, the Logi, unplug while playing (after a warm-up play) | 3 of 3: "A fatal exception 0E has occurred at 0028:C001545A" (CR2 `00C90FA6`), as candidate 3 |
| ME, QEMU `usb-audio`, unplug 12 s into a play | 0 of 8 |

The trace (`ev\sa1-dec.txt`): the relations answer comes before the drain,
so the 3 isochronous URBs complete `CANCELED` at the drain; 98 SE's
`usbaudio.sys` still answers with abort, reset and resubmit (about 50
rounds, each URB refused `DEVICE_GONE`), the HID function is removed, then
`usbaudio.sys` sends SELECT_CONFIGURATION to the audio PDO and 98 SE stops
before its REMOVE. With nothing playing, the same 98 SE sends the select
and takes its REMOVE cleanly. So neither the time nor the status of the
completions saves 98 SE once a stream was running: its own teardown after
the select faults. No xHCI98 change read here keeps 98 SE up.

**Option B's further legs** (`459730a5` gated release image):

| Leg | Result |
|---|---|
| 98 SE, usb-wdm-free `uac2` with the Sound Blaster X4 (passed through), unplug while playing | 5 of 5 survived, the shell answering each time (3.4.1.0 on the same guest: 1 of 1) |
| XP SP3 (one vCPU), QEMU `usb-audio`, unplug while playing | 5 of 5 survived, Sound Recorder closing itself |
| ME: a USB keyboard unplugged and replugged while QEMU's audio plays; a tablet unplugged | playing went on (33.5 s and counting), no stop |
| ME: a USB stick (QEMU `usb-storage`) plugged, 3,000,000 bytes copied to C:, unplugged | copied, no stop |
| 2000 SP4, two vCPUs and Verifier | not run: the guest stopped before its desktop with two vCPUs |
| uvc98, MIDI, race checks, uac2 capture | not run |

**Options (unchanged in substance):**

- A: keep option B (ME: no stops in 48 across the root port, hub and A's
  form; uac2 and XP unaffected), make 98 SE's case a known limitation:
  "stop playback before unplugging a USB audio device" (98 SE freezes or
  stops at an unplug while playing a real UAC 1.0 device, under every build
  read). Recommended.
- B: as A, and also release on the relations answer (option A's form) for
  98 SE, trading a freeze for a fatal exception: not recommended.
- C: no change, the limitation for both.

### 9. Option B's owed legs, completed where the rig allowed (2026-11-07)

`459730a5` gated `release` image (`xhci98.sys` `9fd1f608...`, `xhcihlpr.sys`
`eb4cb86c...`), put in place by `copy` and a cold start.

| Leg | Result |
|---|---|
| 98 SE, usb-wdm-free `uac2`, the Sound Blaster X4 (passed through), recording in Sound Recorder, the X4 unplugged 10 s in | 3 of 3 survived, recording ending at the unplug, the shell answering (`cap-ux3b2`); the same guest on 3.4.1.0 first: 2 of 2 (`cap-ux3d`) |
| 98 SE, `uvc98` with usb-wdm-free's simulated UVC camera (`uvcsim.py` over usb-redir), `M1CAP` streaming 160x120, the camera unplugged 3 s in | 1 valid: the read ended in error 1784 and the guest went on; the 2 replugs that followed never offered the camera's filter again ("exact synthetic filter absent"), not compared with 3.4.1.0 |
| ME race checks, QEMU `usb-audio`: unplug 1 s after the play starts (`rearly`) | 4 of 4, no stop |
| ME race: Alt+F4 (the client's close: abort and deconfigure) and the unplug in the same instant (`rclose`) | 4 of 4, no stop |
| ME race: the unplug, then the client's close 0.3 s later (`rclose2c`) | 4 of 4, no stop |
| ME race: unplug while playing and replug 1 s later, before the old PDO's removal (`rreplugc`) | 4 of 4, no stop |
| Windows 2000 SP4, one vCPU, Driver Verifier on `xhci98.sys` (flags 0x1B, confirmed active by `verifier /query`) | 6 plugs and unplugs of QEMU's audio device, no bugcheck; the guest's Sound Recorder could not play the device ("cannot play files like the current file"), so no unplug while streaming was read on 2000 |
| Windows 2000 with two vCPUs | not readable: the only 2000 base (`q35-bases\w2k`, and wdm5's copy of it) has the Standard PC HAL (uniprocessor) and stalled before its desktop with `-smp 2`; the ACPI 2000 image of the p43-45 kit never finished setup |
| MIDI (bulk), on 98 SE and ME | not run: no guest with usb-wdm-free's `usbmidi` under xHCI98 was at hand; MIDI is a bulk pipe, which option B does not touch |

Seen on the way, not attributed: on a first `uac2` guest (`ux2`) the guest
froze when the X4 and a QEMU tablet were plugged together right after the
X4's install, and afterwards its recording never advanced and QEMU ended
at each unplug (twice); the clean guest above, on the same image, did
neither.

### 10. uvc98 replug on 3.4.1.0 and option B, and the frozen uac2 guest's evidence (2026-11-07)

Fresh overlays of the same base (`g\base-uvc.qcow2`, the uvcsim kit's 98 SE
guest), each with its driver copied in from the floppy and a cold start;
then 4 trials of: the simulated camera (`uvcsim.py`) plugged, `M1CAP`
streaming 160x120, the camera unplugged 3 s in, `M1CAP` closed; trials 2 to
4 are the 3 replugs (`ev\uvc2-ur1.txt`, `ev\uvc2-ub1.txt`, `M1CAP`
reports `ev\uvc2-*-m1cap-*.txt`).

| Build | Trials streaming (State 3, a READ ended 1784 by the unplug) | Camera offered again after a replug | Stops |
|---|---|---|---|
| 3.4.1.0 (`xhci98.sys` `f67f2bd2...`) | 4 of 4 | 3 of 3 | 0 |
| option B, `459730a5` (`9fd1f608...`) | 4 of 4 | 3 of 3 | 0 |

No regression. The "filter absent" of section 9 was the earlier script's:
it left each `M1CAP` window open, and the next trial's `M1CAP` found the
filter still held by the previous one.

The frozen `uac2` guest of section 9 (`ux2`): its disk was deleted in the
cleanup before it was asked for; its shots, 0xE9 logs, QEMU outputs,
launch lines, driving script and pids extract are kept in
`E:\xhci98-vm\a347\uaw\ev\ux2-freeze\` (README.txt there), for the 98 SE
ring-0 hang lead.

### 11. 98 SE: looking for a workaround after option B was held (2026-11-07)

The owner held option B (`459730a5`) and asked for a way past the 98 SE case. On 98 SE, `usbaudio.sys` waits
for its held isochronous requests and never sends SELECT_CONFIGURATION first, so B's release never fires. The
three ideas were:

- complete the held requests with an error at the departure;
- fail new isochronous submissions at once;
- delay the PDO's removal until the pipes drain.

The ideas, and what earlier legs already showed:

| Idea | Earlier legs |
|---|---|
| Complete the held isochronous requests with an error at the departure | Option A's test build (section 8: `CANCELED` at the relations answer) |
| Fail new isochronous submissions on a departed PDO at once | Candidate 3 (section 6: nothing held, every request refused `DEVICE_GONE` at the next tick) |
| Delay the removal until the pipes drain | Needs the stream to stop first, and the stream stops only when its requests complete, which is the next row |

Both earlier forms end in the same fatal exception (screens below).

So two new test builds were tried. Both are uncommitted, layered on `459730a5` plus the per-PDO trace, x86
`qemu` flavour, and gated to 98 SE (`!IoIsWdmVersionAvailable(1, 0x05)`).

- **P, "phantom"** (`E:\xhci98-scratch\w47\uaw\phantom-test.diff`, `xhci98.sys` `41f51b4d...`). A departed
  device's isochronous requests are not held: those held at the departure, and those submitted after it,
  complete SUCCESS (every packet success, no data). They complete at their own frame time through a per-PDO
  timer, as an isochronous OUT stream to an unplugged device does on the wire, where nothing answers and nothing
  fails. Abort, cancel, stop and REMOVE release them `CANCELED` as before.
- **Q2** (`phantomq2-test.diff`, `490e4ade...`). P, plus a departed PDO's unconfigure (SELECT_CONFIGURATION
  with a NULL descriptor) answered SUCCESS at the next tick, its ConfigurationHandle cleared. Any other select
  is refused `DEVICE_GONE` as before. A first form, Q (`phantomq-test.diff`), also answered RESET_PIPE and any
  select. Q was never run: its guest booted the base's own driver, see the void legs below.

Review: P had no DMA, double-completion, recursion or lifetime finding. Its three P2 findings would matter for a
real build but not on the Full Speed Logi:

- the OUT length after the drain;
- the 32-bit deadline;
- the High Speed interval.

Q's P2 (success for a non-NULL select would leave handles unset) produced Q2, which was approved.

Q2 reaches only a departed or orphaned PDO. Every caller of `hcdGoneLater` is a "device record gone" path, and a
ghost's select is answered earlier by `hcdGhostSelect`. A select on a present device, `usbaudio.sys`'s own
included, goes to the controller thread as before.

Legs: the Logi (`046D:0A64`) by `usb-host` on xh.0 port 4. Each guest:

1. A fresh overlay of `g\base-af1.qcow2`, the driver swapped in by `SW.BAT`.
2. The Logi installed by its wizard, then a cold start (`scripts\secand2.sh`, which since this run stops if
   its port is busy or the launch fails).
3. Rounds of `scripts\pt.sh`: plug; play until the position moves (up to 3 tries); unplug about 12 to 16 s
   into the play.
4. A liveness probe 20 s later: QEMU CPU seconds, 8 EIP samples, a blue check, and whether Ctrl+Esc changes
   the screen.

Every round's "playing" shot was read by eye. The driver under test is proven by the port 0xE9 trace: the
`qemu` flavour always writes one, a `release` driver never does.

| Build | Guest, round | Played at the unplug | Result | Evidence |
|---|---|---|---|---|
| B (`459730a5` release, `xferB`) | fb1, rounds 1-2 (idle unplugs: the second play failed) | no | live | `ev\pt-fb1.txt` |
| B | fb1r, round 1 | yes (`shots\fb1-1-wu.png`, play at 9.6 s in the same boot) | **frozen**: QEMU busy, EIP `C000A4D8`/`C000A700`/`C000A881`, Ctrl+Alt+Del lists "Msgsrv32 [Not responding]" (`shots\fb1r-cad.png`) | `ev\pt-fb1r.txt` |
| P | tp1, round 1 | yes | **fatal 0E at 0028:C001545A** | `ev\fault-0e\P-tp1-round1.png`, `P-tp1-e9-raw.log`, `P-tp1-e9-decoded.txt`, `P-tp1-regs.txt` |
| Q2 | tq2, round 1 | yes (8.25 s, `ev\fault-0e\Q2-tq2-round1-playing.png`) | live; the replugged device was found again | `ev\pt-tq2.txt`, `ev\tq2-dec.txt` |
| Q2 | tq2, round 2 | yes (phantom completions in the trace) | **fatal 0E at 0028:C001545A** | `ev\fault-0e\Q2-tq2-round2.png`, `Q2-tq2-e9-decoded.txt` |
| Q2 | tq3, round 1 | yes (7.75 s, `Q2-tq3-round1-playing.png`) | **fatal 0E at 0028:C001545A** | `ev\fault-0e\Q2-tq3-round1.png`, `Q2-tq3-e9-decoded.txt` |
| ME gating: Q2 on ME, QEMU `usb-audio` (`me-b\mq2`) | 10 unplugs while playing | yes, 10 of 10 | 0 stops in 10. B's select release fired in all 10 (9 with held isochronous requests). No phantom line (0x74) and no unconfigure-success line (0x60): ME runs as B | `ev\trials-mq2.txt`, `ev\debugcon-mq2b.log` (984 trace lines), `ev\mq2-dec.txt` |

Void legs (kept, not counted):

- **tq1:** meant to run Q. Its first launch failed because port 51141 was still held by the outgoing P guest,
  so the overlay booted the base's own `3.4.1.0` release driver: its E9 log is empty. That base has no audio
  driver for the Logi (the wizard never ran), so no round played. The "moved" check was fooled by the Add New
  Hardware wizard (`shots\tq1x-1-p2.png`).
- **c341:** a deliberate `3.4.1.0` control on the base without the wizard. "A sound device is not installed",
  0 plays in 4 rounds (`shots\c341-1-p2.png`).

What the traces show (P, `P-tp1-e9-decoded.txt`; Q2 the same):

1. The phantom completions kept the stream going (about 150 requests). The HID function's PDO got its REMOVE.
2. `usbaudio.sys` stopped resubmitting by itself, and its last requests completed (`UrbsPending` 0).
3. It then sent the unconfigure SELECT_CONFIGURATION to the departed audio PDO.
4. In P that select was refused `DEVICE_GONE`, and 98 SE stopped at 0028:C001545A.
5. In Q2 it was answered SUCCESS. Once (tq2 round 1) the audio PDO then got its REMOVE and the guest lived.
   Twice the guest stopped at the same address before any REMOVE.

So neither the requests' status (CANCELED, DEVICE_GONE, SUCCESS), nor their timing (at once, or at frame time
until the client stops on its own), nor the select's answer decides it. The fault follows `usbaudio.sys`'s
unconfigure in its teardown of a stream that was running. With nothing playing, the same teardown is clean
(section 8). This is the same address on every build that lets the teardown run:

| Build | Fault screen |
|---|---|
| Candidate 3 | `ev\fault-0e\cand3-ha1t2.png` |
| Option A | `optA-sa2.png`, `optA-sa3.png` |
| P | `P-tp1-round1.png` |
| Q2 | `Q2-tq2-round2.png`, `Q2-tq3-round1.png` |

The builds that hold the requests (`3.4.1.0`, B) freeze instead. The fault is inside Windows 98 SE's own code
(C001545A); the export-table rule leaves it unread.

The freeze and 36.41. B's freeze at an unplug while playing looks like run 36's audio-arrival wedge (36.41,
hang98's run-47 section):

- VMM's wait code at EIP `C000A4D8`-`C000A881`;
- QEMU busy;
- "Msgsrv32 [Not responding]".

That look marks Windows 98 SE's configuration manager blocked in a PnP operation, whichever one it is. This
freeze comes at the unplug, after a play that ran, with the trace showing three isochronous requests held on
the audio PDO (section 7). Idle unplugs in the same boot stayed live. It is not 36.41's arrival wedge, which
comes before any play.

**Options:**

1. **Keep option B as it is** (ME fixed: 0 stops in 54 unplugs while playing: 20 at a root port, 8 behind a hub, 16 in the race checks, and 10 here). Document 98 SE:
   "stop playback before unplugging a USB audio device". Without a change, 98 SE freezes at such an unplug.
   **Recommended.**
2. **B plus Q2 on 98 SE** (98 SE x86 only; ME and NT unchanged): trades the deterministic freeze for a fatal
   exception at about 2 unplugs in 3 (1 of 3 survived), on a stream that 98 SE's own stack (NUSB 3.3 and
   UHCI) cannot even give the Logi a driver for. It is about 200 lines of timer and completion code for a
   partial result. Not recommended.
3. **B plus P on 98 SE**: same trade, 0 of 1 survived. Not recommended.

Impact: x86 only (98 SE and ME exist only as x86). Options 2 and 3 are confined to 98 SE by the WDM check, and
the ME leg above confirms they leave ME as B.

All overlays are deleted (`ev\deleted-overlays.txt`); the base copies are kept. The Logi was released
(`devices.txt`), no QEMU of this kit runs, and the worktree is clean at `459730a5`. Rule slips:

- The first Q build (21 s, `quick.cmd`) ran before a gated slot was taken; later builds took one (slot 3).
- Two launches of the c341 control went to a wrong guest folder by a quoting error. Both were stopped by QMP
  before use and logged in `pids.txt`.

## 47.33 (continued): the GSI offer on 32-bit 2000 and XP, the owner's bench, the HP list (2026-11-07, w47-hp12x)

### The owner's bench

The owner read the merged build (`d3a596b7`, the `bin-merged` XP x64
binaries) on the HP EliteBook 8470p (BIOS 68ICF F.74) under XP x64,
2026-11-07: no Code 12; with `XhciIntelNoRouteIrq` 2 `noroute.decision`
13 (offered), `noroute.lpc` `1E558086`, `noroute.d20ip` 21,
`noroute.d20ir` `3210`, `noroute.gsi` `0x10`, `noroute.offered` 1,
`noroute.grant` 1, `msi.filter.outcome` 14, `intr.delivery` the line; a
SuperSpeed stick enumerated; a load with both EHCIs and the xHCI, a cold
boot and a warm restart with the stick plugged, disable and enable, all
passed. `XhciIntelPortSwitch` had to be 1 for the blue ports to reach the
xHCI. The readings are held on the maintainer's disk
(`temp\8470p-code12\irq\` and `polling\`, git-ignored).

The owner's decisions of the same day: value 1's list compiled in
(`103C:179B`, `17A7`, `17AB`, `17DF`, `176C`, and `176B` added, the 8570w),
every guard kept, not `1815` (2170p) or the ProBook 6470b/6570b (`17AB`,
an 8570p id, is also the 6570b's: the list cannot tell them apart);
`XhciIntelPortSwitch`'s default unchanged (0), the texts telling these
HPs' users to set it to 1; the 8470p added to the machines table; the offer
extended to 32-bit Windows 2000 and XP.

### The change

Designed first as design record 18 revision 16 (section 16.2, `fe11ac5a`:
what each 32-bit HAL gives, why the `PIRQx_ROUT` IRQ is not offered, what
happens when the offer is refused; the MPS HALs left unresolved), reviewed
and revised. Code `b05b9353` (reviewed, no findings):
`HcdNoRouteAddDevice` runs the read-only AddDevice run, and the qemu
flavour's `XhciQemuNoRouteGsi`, on every NT kernel below 6.0 instead of
64-bit XP alone; decision 15 ("a 32-bit Windows: no offer") retired and
reserved; `tuning.md`. Then `04dbd018` (reviewed and revised):
record 18 revision 17 (section 16.3: the bench, the list, Windows 2000
under an ACPI HAL), the list compiled in (`XHCI_PIRQ_NOROUTE_LIST` gone,
`176B` added), `test_pirq` checks for every listed board, the 2170p, another
vendor and an unlisted LPC, XHCITR's filter outcomes 14 and 15 naming
2000/XP, `tuning.md` (the list, the port switch, Windows 2000), and
`target-standing.md`'s machines table.

### The vehicle

Kit `E:\xhci98-vm\a347\hp12x` (copied and retargeted from hp12's;
`scripts\xb.sh` one cold boot with the firmware state set at SeaBIOS's
menu, `lg.sh` one leg, `chain.sh` a series on one overlay, `off.sh`;
`xferx\` with `SWAP.BAT`, `SETV.BAT` (the values in the machine-wide
`Services\USB` key by `regedit`, which 2000 also has), `SN.BAT`; every
QEMU start and stop in `pids.txt`). The 8470p's state ("hp") as in hp12's
legs: the xHCI's Command register `0107` -> `0100` and the DSDT's eight
`_PRT` entries for device 20 renamed, read back with sum 0. Each leg a cold
boot (the restart is a power-off and a new start, so the state is set
again), then `XHCITR /snapshot` typed at a Command Prompt and its `.DEV`
(Device Manager status, problem, allocated resources) and `.TXT` lines to
COM1. Systems:
- 32-bit XP SP3, ACPI Multiprocessor PC, `-smp 2`: overlay `xp1` of hp12's
  golden `gx` (`3.4.1.0` installed).
- Windows 2000 SP4, ACPI PC (`halacpi.dll`, the 8259 PIC): golden `wa0`
  (the `w2kacpi` base + `3.4.1.0`, exit 3; SHA-256 `5aa8056e...`).
- Windows 2000 SP4, ACPI Uniprocessor PC (`halaacpi.dll`, the I/O APIC):
  overlay `wp1` of `wa0`, the SP4 CD's `HALAACPI.DL_` expanded beside the
  installed HAL and `BOOT.INI` given `/HAL=halaacpi.dll`, and the base's
  boot-sector fix that clears the local APIC's global enable at every start
  (needed only after a warm reset under the PIC HAL) made a no-op (offset
  `2Ch` `F7` -> `FF`, through QEMU's `qemu-io` on the overlay). Its control
  boot gave the xHCI IRQ 20: the I/O APIC numbering.
- Windows 2000 SP4, Standard PC (no ACPI): golden `ws0` (the `w2k` base +
  `3.4.1.0`, exit 3; `31dde94a...`).
The build: `b05b9353`'s gated x86 `all` (BUILD + GATES PASSED, host tests
passed, `gated-x86-all.log`): qemu `xhci98.sys` `fdeb4e47...`, release
`ce51a126...`, `XHCITR.EXE` `1049f95e...`, the qemu `xhci98.sys` copied
over the installed one (`fc /b`: no differences). `04dbd018` changes only
value 1's list, which no qemu leg reaches (QEMU's controller is not a
reviewed one; the GSI is the qemu value's). Then `04dbd018`'s gated x86
`all` and amd64 `all` (BUILD + GATES PASSED, host tests passed;
`gated-x86-all-final.log`, `gated-amd64-all-final.log`): x86 release
`xhci98.sys` `70be654a...`, qemu `a0ae46bc...`, `XHCITR.EXE` `49a13609...`;
amd64 release `8b0eead8...`, qemu `3ca7e422...`, `XHCITR64.EXE`
`442793fc...`; read in legs r0s (32-bit XP, the release) and y0, y1s (XP
x64, the qemu build).

### The released driver in the 8470p's state (Windows 2000)

| Leg | System | State | Result |
|---|---|---|---|
| ws1 | 2000, Standard PC | hp | Code 0: memory `FFEFC000` placed afresh, **IRQ 10** shared level (the Interrupt Line register's), the line |
| wa1 | 2000, ACPI PC | hp | **STOP 0x000000A5 (0x00010003, 0x82038AD0, 0x00000014, 0x820A17D0)** at start |
| e1 | 2000, ACPI PC, no xHCI98 (the base) | hp | starts; the xHCI with no driver |
| e2 | 2000, ACPI PC | `_PRT` entry removed alone, decodes on | STOP 0xA5, the same parameters |
| wp1 d | 2000, ACPI Uniprocessor PC | hp | STOP 0xA5 (0x00010003, ..., 0x00000014, ...) |

0xA5 with parameter 1 0x10003 is Microsoft's "ACPI could not find a
mapping in the `_PRT` package for a device", parameter 3 the device
(bits 5:0, 20). Windows 2000's ACPI stops where XP's gives Code 12.

### The legs (the w47-hp12x qemu build)

| Leg | System | State and values | Code | Granted | Record | Delivery |
|---|---|---|---|---|---|---|
| x0 | 32-bit XP | control, defaults | 0 | memory `FEBF0000`, GSI 20 (the PCI driver's own) | `noroute.decision` 5 (past the location and 00:14.0's header: not a reviewed xHCI), outcome 13, grant 2 | line |
| x1 | 32-bit XP | hp, `XhciIntelNoRouteIrq` 2, no injection | 0 | memory `FEBFC000` alone | decision 5, outcome 13, grant 0 | POLLING, "no interrupt resource was granted" |
| x2s | 32-bit XP | hp, GSI 20 | 0 | memory, **GSI 20** | decision 14 (qemu), offered 1, outcome 14, **grant 1** | line |
| x3 | 32-bit XP | hp, GSI 60 | 0 | memory alone | offered 1, outcome 14, grant 0 | POLLING, "no interrupt resource was granted" |
| x4 | 32-bit XP | hp, GSI 21 (no device asserts it) | 0 | memory, GSI 21 | grant 1, `LineMoves` 1 | POLLING, "the line interrupt went dead; moved to polling at run time" |
| x5 | 32-bit XP | hp, GSI 20, `XhciQemuLineConnectFail` 1 | 0 | memory, GSI 20 | grant 1, `intr.line.connect.status` C0000001 | POLLING, "the line granted could not be connected (2000/XP), so polled" |
| x6 | 32-bit XP | hp, GSI 60, `XhciInterruptMode` 1 | **12** | none | the clone alone offered (mode 1), refused | - |
| s0 | 2000, Standard PC | hp, defaults | 0 | memory `FFEFC000`, IRQ 10 | decision 5, outcome 13, grant 2 | line |
| s1s | 2000, Standard PC | hp, GSI 20 | 0 | memory, IRQ 10 | offered 1, outcome 14 (three lists), grant 2: the PCI driver's own line first | line |
| a0 | 2000, ACPI PC | control, defaults | 0 | memory `FEBF0000`, IRQ 11 (the `_PRT` link's) | decision 5, outcome 13, grant 2 | line |
| a1 | 2000, ACPI PC | decodes off, `_PRT` kept, GSI 20 | 0 | memory `FEBFC000`, IRQ 11 | offered 1, outcome 14, grant 2 | line |
| a2 | 2000, ACPI PC | hp | - | - | the filter's trace (port E9) before the stop: `M86 L ... 0000000D` (outcome 13) | **STOP 0xA5 (0x00010003, 0x82047AD0, 0x00000014, 0x820A17D0)** |
| pz | 2000, ACPI Uniprocessor PC | control, defaults | 0 | memory `FEBF0000`, **IRQ 20** | decision 5, outcome 13, grant 2 | line |
| p1 | 2000, ACPI Uniprocessor PC | decodes off, `_PRT` kept, GSI 21 | 0 | memory, GSI 20 (the `_PRT`'s) | offered 1, outcome 14, grant 2 | line |
| p2 | 2000, ACPI Uniprocessor PC | hp | - | - | the filter's trace before the stop: outcome 13 | **STOP 0xA5 (0x00010003, 0x82069A30, 0x00000014, 0x820A17D0)** |
| x7 | 32-bit XP | hp, defaults (value 1) | 0 | memory alone | decision 5, grant 0 | POLLING |
| x8s, x10s | 32-bit XP | hp, GSI 20, a USB stick plugged after the snapshot | 0 | memory, GSI 20 | grant 1 | line; the stick at D: (`TESTDATA.BIN` listed, x10s) |
| y | 32-bit XP | hp, defaults, a stick | 0 | memory alone | - | POLLING; the stick at D:, 98 files copied to it and `xhci98.sys` compared equal (`fc /b`) |
| r0s | 32-bit XP, overlay `xp2` of `gx` | hp, defaults, **`04dbd018` release** `xhci98.sys` (`70be654a...`), a stick | 0 | memory `FEBFC000` alone | decision 5 (`noroute.subsystem` 11001AF4: QEMU's, not on the list), grant 0 | POLLING, "no interrupt resource was granted"; the stick at D: |
| y0 | XP x64, overlay `x64a` of hp12's `g0` | control, defaults, the `04dbd018` amd64 qemu build (`3ca7e422...`) | 0 | memory `FEBF0000` and the PCI driver's own line | decision 5, grant 2 | line |
| y1s | XP x64 | hp, GSI 20, a stick | 0 | memory `FEBFC000`, GSI 20 | decision 14, outcome 14, grant 1 | line; the stick at D: (`TESTDATA.BIN`) |

So: under 32-bit XP's ACPI APIC HAL the offer behaves as on XP x64 (the
grant, the refusal, the silent line moved to polling, the connect failure
polled, mode 1 at Code 12); the AddDevice run's reads (the PDO's location,
00:14.0's header) answer on 32-bit XP, on 2000 under all three HALs; under
the Standard PC HAL the PCI driver's own line wins and the offer is never
taken; on Windows 2000 under either ACPI HAL the 8470p's firmware state
stops the system with 0xA5 before any alternative of the list is tried, the
filter having run (design record 18 section 16.3). Not read: the offer's
grant under a PIC HAL (no state reaches it without the stop: with the
`_PRT` entry kept the PCI driver's own line wins, without it 2000 stops),
the MPS HALs (section 16.2, unresolved), a storm from an unserviced sharer.

### Left

- The owner's 8470p checklist's 32-bit legs, where a 32-bit XP install is
  at hand (the owner's earlier readings name XP x64, 7 x64 and 10 only).
- Windows 2000 on these HPs: under an ACPI HAL the stop above (inferred
  from the emulator; Windows 2000 has never run on them); whether a
  requirement list with no interrupt at all on 2000 would avoid it is the
  owner's question.
- The reporter's 2570p; the known-limitation line at 47.final.

## 47.38 (issue 26): a UAS flash drive's SMART reads, the stranded pause, XhciUasNoAtaPassThrough, the failure ring, XhciUasFastReset (2026-11-07, w47-uasx)

Branch `w47-uasx` from `3679c449`; development host A. The owner's choices
of 2026-11-07: options 2 (the stranded pause), 1 (`XhciUasNoAtaPassThrough`)
and 3 (diagnostics); then option 4 (`XhciUasFastReset`, a per-device value,
default 0).

### The evidence

A user's XHCITR snapshots (`3.4.1.0`, level 3) and photos, held on the
maintainer's disk: an Intel 7 Series xHCI (`8086:1E31`) desktop on Windows XP
x64 with a Fresco Logic FL1100 add-in; the drive `0781:55A9`, a 460 GB exFAT
flash drive, UAS with 4 streams. Fourteen snapshots from six boots (five on
the Intel controller, one on the FL1100). Every Intel episode: one CHECK
CONDITION, then one or more command timeouts each ended by an answered ABORT
TASK (two episodes 20 and 21, the freezes, ended by the unplug); the FL1100:
sense 5, timeouts 0, cancels 5. `strict.violation.epstate` `10030541` (Set TR
Dequeue on DCI 5, data-in, endpoint Running, polled boot) and `10020941` (DCI
9, status, five violations by the counter, the line-interrupt boot). With the
line interrupt (`XhciInterruptMode` 0) the line was healthy (watcher asserted
0, no move); those three episodes ended with the drive's own link drop and a
surprise removal. Photos: exFAT, capacity right, free 0; "Windows - Wrong
Volume" `c0000012` with the drive listed with no size; Device Manager's IRQ
view: IRQ 16 shared with EHCI 1E2D, PCIe root ports and the graphics (the GPU
swap changed nothing).

### The code

| Commit | What |
|---|---|
| `d5dc1407` | `XhciTolStopRest` (bounded Stop: restop after 1 ms when Running, or Context State Error with Stopped; at most 3; give-up faults); `hcdCfgStopBounded` (notes `cfg.stop.again`, `cfg.stop.giveup`; device marked for a device-only cycle); `hcdCfgQuiesce` faults on a give-up; `hcdCfgSetDequeueOne` stops again before a stream's Set TR Dequeue on an endpoint read Running (`cfg.deq.running`); `test_tol` `test_stop_rest` |
| `0e0488be` | `XhciUasNoAtaPassThrough` (device hardware key, INF 0 no-clobber, both INFs; gate rule `UAS-NOATA`): SRB 85h/A1h, SCSI and ATA pass-through, SMART answered locally (5/24/00; `SMART_VERSION` refused `STATUS_NOT_SUPPORTED`); `test_uas` `test_no_ata`; tuning.md |
| `a90afe96`, `2ee9a552` | the failure ring: `XHCI98_FAILURES` ('X98F') on `IOCTL_XHCI98_TELEMETRY`, kept by the HCD beside the class driver record, snapshot HCD header words 860-928 (`HEAD_WORDS` 929), XHCITR `failures:` under each record; `FailTurn`; XHCITR bounds; `test_uas` `test_fail_pack`, `test_snap` |
| `37e0352a`, `a5bdbfe9`, `8d9991c8` | issue 26 page, its index row, issue 22's link, snapshot-reader.md section, record 13 section 16.13; corrections after review; the ASM1042's Code 10 cause and workaround (47.39) |
| `ff2b7c2a` | qemu flavour: injection fault 1A (a Stop Endpoint's race: real Stop, answered Context State Error, next context read Running); `test_inj`; build-and-test.md's table |
| `17c7366d`, `8c7d82b7` | `XhciUasFastReset` (option 4): second per-device value, INF 0; at nonzero a second timeout within 120 s of an answered ABORT TASK with no progress resets the device, and a final IU's data cancel still out after 5 s asks for the reset; failure event 7; `test_uas` `test_fast_reset`; gate (60 mutations); tuning.md, record 13 section 16.14, issue 26 |

### Host vectors

`test_tol` 3461 checks, 0 failures; against `3.4.1.0`'s single Stop 12
failures (the race, the restart after a successful Stop, the endpoint that
keeps running, the "about to run" restops). `test_uas` 657 checks, 0
failures. `test_snap` 530, 0 failures. `test_inj` 1425, 0 failures. UAS INF
gate self-test 60 mutations, both INFs pass.

### Review

`d5dc1407` and `0e0488be`: no material findings. The three failure-ring
commits: failure starvation and an XHCITR offset wrap, fixed in `2ee9a552`;
then clean, six documentation corrections folded in `a5bdbfe9`. Fault 1A: a
failed real Stop must not become a race, fixed in `ff2b7c2a`, then clean.
`XhciUasFastReset`: the 5 s deadline only for a data transfer this driver
cancelled, fixed in `8c7d82b7`, then clean. `KeDelayExecutionThread` in the
restop wait: every caller at PASSIVE_LEVEL.

### Builds

| Build | Commit | Result | xhci98.sys | xhciuas.sys |
|---|---|---|---|---|
| x86 release | `ff2b7c2a` (injection popped mid-run, qemu-only) | PASS | `25223499...` | `9CBC2BFF...` |
| amd64 release + qemu | `ff2b7c2a` | PASS | release `E22331E0...`, qemu `1B252364...` | release `2FF32A8C...`, qemu `7FE10A49...` |
| amd64 release + qemu | `8c7d82b7` | PASS | release `7D10B878...`, qemu `64A8D0D0...` | release `DA62562D...`, qemu `337381F3...` |
| x86 release | `8c7d82b7` code (record `8d9991c8`, a docs commit landed mid-run) | PASS | `B090CC27...` | `A2234373...` |

### Legs

XP x64: an XP x64 SP2 guest with `3.4.1.0` (q35, read-only base under an
overlay), QEMU `usb-uas` (serial W26UAS) and `scsi-hd` on `qemu-xhci` port 3;
the branch's `xhciuas-amd64.inf` put on the device with a guest-only
`UpdateDriverForPlugAndPlayDevices` helper (the base had no `xhciuas`
staged), the branch's `.sys` copied over. 7 x64: an overlay of the q35 base
(test signing on), every `.sys` of the package signed in the guest with a
throwaway certificate, `xhci98.inf` and `xhciuas.inf` put on by the same
helper ("install anyway"). 98 SE: an overlay of a 98 SE guest with NUSB 3.3
and xHCI98 `3990e17d`, Have Disk on A:.

| Leg | System, build | Result |
|---|---|---|
| L1a | XP x64, release `ff2b7c2a`, `XhciUasNoAtaPassThrough` 0 (INF wrote it) | PASS: ATATEST as 45.52's "x-sat off": A1-A5 CHECK CONDITION 5/20/00, A6/T1-T3 Error 04h Status 51h, SMART_VERSION answered, RCV/SEND 50; snapshot `failures:` 14 CHECK CONDITION events (program, CDB 85h, 5/20/00) |
| L1b | the same, value 1 | PASS: A1-A5 5/24/00, M1 SMART_GET_VERSION error 50, the rest as before; ring "answered here, not sent" |
| L2 race | XP x64, qemu `ff2b7c2a`, fault 1A x1, the drive stopped by `XHCITR64 /eject` | PASS: `qemu.inj.stoprace=105` (slot 1, DCI 5), `cfg.stop.again=00010513` (one restop, Stopped), the stop completed with no fault and no controller reset; after a restart the drive answers ATATEST |
| L2 give-up | the same, fault 1A x4 | PASS: four races, `cfg.stop.again`/`cfg.stop.giveup=00010531` (three restops, still Running), `cfg.deq.cycle=1`: the device alone cycled and enumerated again on slot 1 (SlotsEnabled 2), no `ctrl.*` note |
| L2 via timeouts | XP x64, throttled drive (4 KB/s, 1 KB/s), PTCANCEL 8/16 wait | NOT READABLE: at 4 KB/s no command timed out; at 1 KB/s QEMU's `usb-uas` aborts at the first timeout's cancel (`dev-uas.c:708 usb_uas_cancel_io: assertion failed: canceled usb packet not found`). Control with the released `3.4.1.0` `xhci98.sys`/`xhciuas.sys`: the same assertion - an emulator limit, not this branch. A CHECK CONDITION on QEMU never leaves a data TD to cancel, so ATATEST reaches no Stop either |
| L3a | XP x64, release `8c7d82b7` (INF re-applied: both values 0, NoAta kept) | PASS: ATATEST as L1a |
| L3b | the same, `XhciUasFastReset` 1 | PASS: ATATEST as L1a; no fast-reset event, port resets 0 (the CHECK CONDITION cancels did not trip the 5 s deadline). The second-timeout reset itself is not readable here (the emulator limit above) |
| L7a | 7 x64, release `8c7d82b7`, value 0 | PASS: both values 0 in Device Parameters, `xhciuas` started; ATATEST: 5/20/00, SMART_VERSION answered |
| L7b | the same, `XhciUasNoAtaPassThrough` 1 | PASS: 5/24/00, SMART_VERSION error 50, ATA forms 04h/51h |
| L98 | 98 SE, x86 release `8c7d82b7` `xhciuas.sys` and INF from A: | PASS: `HKLM\Enum\USB\VID_46F4&PID_0003\W26UAS` holds NUSB's storage upper filter, `XhciUasNoAtaPassThrough` 0 and `XhciUasFastReset` 0 ([Uas.Dev.HW]), and `XhciUasCommand*` written by the started driver |

### Merge

Merged into `3.4.2.0` at `d76311ab` (2026-11-07); one hand resolution, the
issue index, where issue 25's and 26's rows were both kept.

### Left

- The reporter's reading on `3.4.2.0`.
- The second-timeout fast reset and the stranded-pause path through a real
  cancel: QEMU's `usb-uas` asserts on a timeout-driven cancel (with
  `3.4.1.0` too), so they need an emulator whose cancel does not assert, or
  real hardware.

## 47.39 (issue 26): the same UAS drive at Code 10 on an ASMedia ASM1042 (2026-11-07, w47-asm42)

Branch `w47-asm42` from `0f701816`; development host A.

### The report

The same user's snapshots as 47.38's (`3.4.1.0`, level 3, XP x64 SP2): the
drive `0781:55A9` (exFAT, UAS with 4 streams on the Intel 7 Series xHCI
`8086:1E31` and the FL1100) plugged into an ASMedia ASM1042 (`1B21:1042`)
add-in. Its descriptors: interface 0 alternate 0 Bulk-Only (08/06/50, bulk IN
0x81 and OUT 0x02), alternate 1 UAS (08/06/62, four pipes, 4 streams). The
ASM1042's log: `quirk.streamsoff=00010061` (Linux's `XHCI_BROKEN_STREAMS`
applied), `XportBotNoStreams` 4, and four cycles of `enum.port.end` then
`pnp.pdo=000x0204` (`IRP_MN_REMOVE_DEVICE`, listed), each followed by the port
read SS.Inactive (`enum.port.look=0xB440B0`: PLS 6, CCS 0), `link.keep`
refused with reason 4 (not started), a warm reset and a new enumeration
(`SsWarmResets` 5). Counters for the four cycles: `SelectEndpointsRequested`
16, `EndpointsOpened` 16, `TransfersSubmitted` 16 - four each.

### The cause

Four endpoints a cycle is the UAS setting (Bulk-Only has two), and four
control transfers are exactly `xhciuas`'s start (`src/uas/uas_fdo.c`
`uasFdoStart`: device descriptor, configuration header, configuration, serial
string), not `usbstor`'s. The drive has a serial number, so its PDO answers
`UniqueID` TRUE and its instance path `USB\VID_0781&PID_55A9\<serial>` is the
same on every controller (record 13 section 10.7): one devnode per drive.
Installed with `xhciuas` on the Intel controller, the entry keeps `xhciuas`
on the ASM1042, where the bus offers the drive as Bulk-Only (why 5,
`hcd_pdo.c` `hcdXportDecide`): Windows does not read the new compatible ids
(`Prot_50`) for an installed devnode. `xhciuas` selects alternate 1, its
`IOCTL_XHCI98_OPEN_STREAMS` is refused (`hcd_cfg.c` `hcdCfgStreamsOpen`,
`XHCI98_STREAMS_NO_CONTROLLER` -> `STATUS_NOT_SUPPORTED`), its start fails:
Code 10, then `REMOVE`. `usbstor.sys` never ran on the ASM1042. The link drop
after each `REMOVE` is the drive's own (no request from the bus follows the
failed start in the emulator); not reproduced, and harmless once the start
succeeds.

### The reproduction (XP x64, `3.4.1.0`)

An XP x64 SP2 guest with `3.4.1.0` under an overlay, the emulator fork at
`cb36497` (for `usb-redir`), `qemu-xhci` at 00:14.0, and a simulated drive
over `usb-redir` on port 3 at SuperSpeed with the report's descriptor set
(`0781:55A9`, Bulk-Only alternate 0 implemented over a FAT image, UAS
alternate 1 accepted but not run; serial `ASM42SIM0001`). QEMU's `usb-uas`
offers UAS alone, so it cannot stand in.

| Leg | Steps | Result |
|---|---|---|
| L1 | `XhciStreamsOff` 0; the released `xhciuas.inf` installed on the drive by its `Prot_62` id | Entry `USB\VID_0781&PID_55A9\ASM42SIM0001`, Service `xhciuas`, Capabilities `0x14` (Removable, UniqueID); the trace: SET_CONFIGURATION 1, SET_INTERFACE alternate 1 |
| L2 | `XhciStreamsOff` 2, restart, replug | **Code 10, reproduced**: Transport "Bulk-Only (the device offers UAS too; this controller cannot stream at SuperSpeed)", Driver `xhciuas`, Problem code 10, `XportBotNoStreams` 1, `SelectEndpointsRequested` 4, `EndpointsOpened` 4, `TransfersSubmitted` 4, `pnp.pdo=00030204`; the trace: alternate 1 selected, no CBW - the report's counts per cycle |
| L3 | Windows' `usbstor.inf` put on the entry by its `Prot_50` id (the uninstall-and-replug workaround's outcome) | Service `USBSTOR`, Problem code 0, alternate 0, GET_MAX_LUN, INQUIRY, READ CAPACITY, READ(10): the volume mounted |

### The change

| Commit | What |
|---|---|
| `ec39c574` | XHCITR: `tr_xport_unrunnable` (`tr_log.c`, sharing the service-name compare with `tr_xport_stale`); the drive's details add a note naming the cause and the way out; `test_xhq` vectors; record 25 section 19 item 4; release notes, "SuperSpeed and UAS" |
| `ff85e02b` | Reviewed and revised: the predicate reads the record's inputs (SuperSpeed without the controller's streams), so `XhciForceBulkOnly` on such a controller (why 4) is covered; vectors for forced Bulk-Only with and without streams and below SuperSpeed; the input flags mirrored and held equal |

No driver change.

### Review

`ec39c574`: forced Bulk-Only on a controller that cannot stream was missed,
fixed in `ff85e02b`; then no material findings.

### Builds

Gated x86 release on `ff85e02b`: BUILD + GATES PASSED. `test_xhq` 424 checks,
0 failures; XHCITR snapshot self-test 53 runs passed on `XHCITR.EXE` and
`XHCITR64.EXE`. `XHCITR64.EXE` of that build: `9c8ad053...`.

### Legs (XP x64, the new `XHCITR64.EXE`)

| Leg | Configuration | Result |
|---|---|---|
| N1 | `XhciStreamsOff` 0; QEMU `usb-uas` (W42UAS) on `xhciuas`; QEMU `usb-storage` (W42BOT) | `usb-uas`: Transport "UAS (the only transport the device offers)", `xhciuas`, Code 0, no note; `usb-storage`: "Bulk-Only (the device offers no UAS setting)", `USBSTOR`, Code 0, no note |
| N2 | `XhciStreamsOff` 2, restart, the simulated drive on its `xhciuas` entry replugged | The drive: Code 10 and the note, all eight lines as written; `usb-storage`: no note |

### The owner's decision

The options put to the owner: (1) a separate devnode for a drive the bus
offers as Bulk-Only because the controller cannot stream (a different
instance id, or `UniqueID` FALSE, for that form), at the cost of one new
entry for every such drive already installed as Bulk-Only and perhaps a new
drive letter, touching record 13 section 10.7 and the 98 SE and ME install;
(2) `xhciuas` running Bulk-Only itself on a Bulk-Only offer (CBW/CSW on
alternate 0), no devnode change but a second transport in the UAS driver;
(3) the XHCITR note and the release notes' paragraph alone.

The owner, 2026-11-07: the XHCITR note and the uninstall-and-replug
workaround in `3.4.2.0`; a real fix, its own devnode for the Bulk-Only form
or `xhciuas` running Bulk-Only, planned for later (roadmap, "Planned - Found
in Phase 47", UASBOT.1).

### Merge

Merged into `3.4.2.0` at `a38d2aa6` (2026-11-07), with no conflict.

## 47.33 (continued, 2): Windows 2000's STOP 0xA5 on the listed HPs (2026-11-07, w47-hp12y)

### The design and the diagnostic

Section 16.3's reading: in the emulator, with the 8470p's firmware state,
Windows 2000 under the ACPI PC and the ACPI Uniprocessor PC HAL stops with
STOP 0x000000A5 (0x00010003, a device object, 0x00000014, the `_PRT`
object) - "ACPI could not find a mapping in the `_PRT` package for a
device", device 20 - once the controller has a driver. The owner's
decision of 2026-11-07: design a driver fix. Design record 18 revision 18
(section 16.4, `a70e5b85`, reviewed and revised over four passes). A
diagnostic qemu build of `ae33d1b4`, not committed, whose NT 5 filter left
the memory-only copy as the list's only alternative (`xhci98.sys`
`559aceb0...`), read on development host A in the Windows 2000 guests of
section 16.2 (the 8470p's state set at SeaBIOS's menu):

| Leg | HAL | State | Result |
|---|---|---|---|
| d0 | ACPI PC | control | Code 0, IRQ 11, the line |
| d1s, d2 | ACPI PC | the 8470p's | **no STOP**: Code 0, memory alone, POLLING, grant 0; the start reached |
| d3 | ACPI PC | `_PRT` entry removed, decodes on | **STOP 0xA5**, and no IRP of the controller's in the port E9 log: the filter never ran |
| q0 | ACPI Uniprocessor PC | control | Code 0, IRQ 20, the line |
| q1s | ACPI Uniprocessor PC | the 8470p's | **no STOP**: Code 0, memory alone, POLLING |

The owner chose option C (the strip, and a limitation line for a BIOS that
turns the decodes on) in 3.4.2.0.

### The change

On branch `w47-hp12y`, rebased onto `ca0001e1`:
- `37a9f39d` (reviewed and revised): `XhciPirqW2kStrip` (`xhci_pirq.c`),
  the gate - Windows 2000 (WDM 1.10, not 1.20), the AddDevice run's
  decision `OFFERED` (every check of section 16.1; never at value 0), a
  board on value 1's list (at value 2 too); `hcdNoRouteStripDecide`
  (`hcd_ctl.c`) at the end of AddDevice's run; `XhciMsiStripNt5`
  (`xhci_msi.c`) and `hcdMsiStripNt5` (`hcd_msi86.c`): the PCI driver's one
  configuration with its interrupt runs removed, the list's only
  alternative, under every `XhciInterruptMode`; outcomes 16 and 17, named
  by XHCITR; the note `noroute.strip`; the qemu values
  `XhciQemuNoRouteListed` (QEMU's controller taken as listed where
  `XhciQemuNoRouteGsi` stands in for the run) and `XhciQemuStripFail` (1
  no memory, 2 a refused shape, 3 a refused walk); host vectors
  (`test_msi`'s strip shapes, `test_pirq`'s gate, `test_snap`'s outcomes);
  `tuning.md`; record 18 revision 19, "As built".
- `2c8e9cdd` (reviewed): where no interrupt-free list can be made, the
  filter frees the list and returns none, successfully, instead of
  failing the IRP: legs k2 and r0 below showed that a failed filter IRP
  does not keep the PnP manager from the PCI driver's list.
- `90f8ecd4` (reviewed and revised): the recovery is "Last Known Good Configuration", not Safe
  Mode (legs sm2 and lk below); `tuning.md`, record 18 section 16.4.

Builds (gated):
`2c8e9cdd` x86 `all`, BUILD + GATES PASSED, host tests passed:
release `xhci98.sys` `6a7ed33e...`, qemu
`2f08a0c9...`, `XHCITR.EXE` `2fb857bb...`; amd64 `all`, BUILD + GATES PASSED, host tests passed: release `03a455b7...`, qemu `b72b1721...`, `XHCITR64.EXE` `55f85abc...`. `37a9f39d` x86
`all` passed too (qemu `47b2d3c1...`).

### The legs

The guests of 47.33's legs, their bases rebuilt for this run: Windows 2000
under the ACPI PC HAL with `3.4.1.0` (exit 3, SHA-256 `9fb664e2...`),
Windows 2000 under the Standard PC HAL with `3.4.1.0` (`922db2ea...`), an
overlay of the first booting the SP4 CD's `halaacpi.dll` with `/HAL=` (the
ACPI Uniprocessor PC, as in section 16.2's legs); 32-bit XP and XP x64. Each leg a
cold boot with the state set at the menu, values in the machine-wide
`Services\USB` key, the qemu build swapped over the installed driver,
XHCITR `/snapshot`'s `.DEV` and notes. "Listed" is the qemu stand-in
(`XhciQemuNoRouteListed` 1 with `XhciQemuNoRouteGsi` 20).

| Leg | Build | System | State, values | Result |
|---|---|---|---|---|
| u0 | `37a9f39d` | 2000, ACPI PC | control, defaults | Code 0, IRQ 11, decision 5, outcome 13, `noroute.strip` 0, the line |
| u1 | `37a9f39d` | 2000, ACPI PC | control, value 2, GSI 20, **not listed** | Code 0, IRQ 11, outcome 14 (the offer added, the PCI driver's own line first), strip 0, the line: an unlisted 2000 controller keeps its list |
| c1s | `37a9f39d` | 2000, ACPI PC | **8470p state**, value 2, listed | **no STOP**; Code 0, memory `FEBFC000` alone, outcome 16, strip 1, offered 0, grant 0, POLLING, "no interrupt resource was granted" |
| c2 | `37a9f39d` | 2000, ACPI PC | the same, `XhciInterruptMode` 1 | no STOP; **Code 10** |
| c3 | `37a9f39d` | 2000, ACPI PC | the same, mode 3 | no STOP; **Code 10** |
| c4 | `37a9f39d` | 2000, ACPI PC | the same, mode 2 | no STOP; Code 0, outcome 16, POLLING, "forced polling" |
| k0 | `37a9f39d` | 2000, ACPI Uniprocessor PC | control | Code 0, IRQ 20, the line, strip 0 |
| k1s | `37a9f39d` | 2000, ACPI Uniprocessor PC | 8470p state, listed | **no STOP**; Code 0, memory alone, outcome 16, strip 1, grant 0, POLLING |
| k2 | `37a9f39d` | 2000, ACPI Uniprocessor PC | 8470p state, listed, allocation failure injected (the IRP failed, as first designed) | **STOP 0xA5**: the filter's trace shows outcome 17, then `QUERY_INTERFACE` IRPs: the PnP manager went on with the PCI driver's list |
| r0 | `37a9f39d` | 2000, ACPI PC | control, listed, allocation failure injected | Code 0 on the PCI driver's own line (IRQ 11) after the failed filter: the same reading where the `_PRT` routes the line |
| r1 | `37a9f39d` | 2000, ACPI PC | 8470p state, listed, **value 0** | **STOP 0xA5**, as designed (value 0 is the PCI driver's list) |
| sm2 | `37a9f39d` | 2000, ACPI PC | 8470p state, value 0, F8, **Safe Mode** | **STOP 0xA5**: Safe Mode loads the USB controllers' drivers too |
| t0 | `37a9f39d` | 2000, Standard PC | control | Code 0, IRQ 10, strip 0 |
| t1s | `37a9f39d` | 2000, Standard PC | 8470p state, listed | Code 0, memory `FFEFC000` alone, outcome 16, strip 1, POLLING: the cost on the Standard PC HAL |
| v0 | `37a9f39d` | 32-bit XP | control, listed | Code 0, GSI 20, strip 0 |
| v1s | `37a9f39d` | 32-bit XP | 8470p state, listed | Code 0, GSI 20 granted (outcome 14, grant 1), **strip 0**: XP never strips; the stick at D: (`TESTDATA.BIN`) |
| f0 | `2c8e9cdd` | 2000, ACPI PC | control, value 0 | Code 0, IRQ 11, strip 0, the line |
| f1 | `2c8e9cdd` | 2000, ACPI PC | 8470p state, value 2, listed, **allocation failure** injected | **no STOP**; Code 10; the filter's trace: outcome 17, then the start (minor 0x00) with no resources |
| f2 | `2c8e9cdd` | 2000, ACPI PC | the same, **refused shape** injected | no STOP; Code 10 |
| f3 | `2c8e9cdd` | 2000, ACPI PC | the same, **refused walk** injected | no STOP; Code 10 |
| f4 | `2c8e9cdd` | 2000, ACPI PC | 8470p state, value 0 | **STOP 0xA5**, as designed |
| lk2 | `2c8e9cdd` | 2000, ACPI PC | after f4, F8, "Last Known Good Configuration", `L` at the Hardware Profile/Configuration Recovery Menu (its text then offered `D` for the default) | **no STOP**: the desktop; the controller at Code 10, the last known good set's own injected walk refusal (f2's value), so the 0 set during f3's session was gone |
| g0 | `2c8e9cdd` | 2000, ACPI Uniprocessor PC | control, listed, allocation failure injected | Code 10: in the control state too a list that cannot be stripped is withdrawn |
| g1 | `2c8e9cdd` | 2000, ACPI Uniprocessor PC | 8470p state, allocation failure | **no STOP** (the same injection as k2, which stopped on `37a9f39d`); Code 10 |
| g2 | `2c8e9cdd` | 2000, ACPI Uniprocessor PC | 8470p state, refused shape | no STOP; Code 10 |
| g3s | `2c8e9cdd` | 2000, ACPI Uniprocessor PC | 8470p state, listed | no STOP; Code 0, outcome 16, strip 1, grant 0, POLLING |
| w0 | `2c8e9cdd` | XP x64 | control, listed | Code 0, outcome 13, strip 0, the line |
| w1s | `2c8e9cdd` | XP x64 | 8470p state, listed | Code 0, memory `FEBFC000`, GSI 20 granted (outcome 14, grant 1), **strip 0**, the line; the stick at D: (`TESTDATA.BIN`) |

**The gate widened at the merge** (`c07a6d56`, reviewed; the owner's
decision of 2026-11-07): `XhciPirqW2kStrip` stripped only where the
AddDevice run reached `XHCI_PIRQ_O_OFFERED`, so a listed board whose run
failed later (the chipset window's mapping, the LPC, RCBA, the pin,
D20IR) would have kept its interrupt and stopped again. It now strips for
every decision past the location, identity and reviewed-controller checks
(`XHCI_PIRQ_O_LPC_UNREAD` to `XHCI_PIRQ_O_OFFERED`), with `test_pirq`
vectors for each, and the qemu value `XhciQemuNoRouteListed` 2 stands in
for a listed board whose run ended at a failed mapping. Builds of
`c07a6d56`: x86 `all` BUILD + GATES PASSED, host tests passed
(release `fdcf0e50...`, qemu `366b6ee7...`);
amd64 `all` BUILD + GATES PASSED, host tests passed (release `c2a57200...`, qemu `6dfe8c71...`). A fresh Windows 2000 ACPI PC base with `3.4.1.0` (`131ab764...`):

| Leg | Build | System | State, values | Result |
|---|---|---|---|---|
| m0 | `c07a6d56` | 2000, ACPI PC | control, `XhciQemuNoRouteListed` 2 set for the next leg | Code 0, IRQ 11, decision 5, strip 0, the line |
| m1 | `c07a6d56` | 2000, ACPI PC | 8470p state, listed, the run ended at a **failed mapping** | **no STOP**; Code 0, memory alone, `noroute.decision` 10 (`MAP_FAILED`), `noroute.gsi` none, strip 1, outcome 16, offered 0, grant 0, POLLING |

On Windows 2000 the stick plugged after the snapshot was not mounted in
the legs' wait (no volume appeared); the controller's start and polled
delivery are the snapshot's.

### Left

- The owner's reading on the 8470p if a Windows 2000 install is ever made
  there (the checklist's section C); Windows 2000 has never run on these
  machines.
- Disable and enable, remove and rescan, a rebalance and a standby under
  the stripped list: not read here (no tool in the 2000 guests drives
  them by script); the gate is evaluated on every filter IRP.
- A listed board whose BIOS turns the decodes on: the stop comes before
  the filter (leg d3); the known-limitation line.

### Merge

Merged into `3.4.2.0` at `303726e3` (2026-11-07), with no conflict.

## 47.35 and 47.41: XHCITR's Settings window, /set and quick save (2026-11-07 to 2026-11-08, w47-trset)

Branch `w47-trset`; development host A. The legs ran on gated kits of this branch: `346258bc` (the
owner's hotkey change), then `cda9b0c1` and `1e1f641c` (the fixes the legs found, below). Every kit
was a gated release build, x86 and amd64 BUILD + GATES PASSED, packaged by `make-package.ps1` and
laid out as a release (81 files plus two leg batch files). The x64 binaries are unsigned. On the
x64 guests the kit was signed inside the guest with a throwaway certificate made there (MakeCert,
Root + TrustedPublisher, SignTool), with catalogs made on the host by the packager's
`New-PackageCatalogFile`. No project or owner key was used. The guests were q35 overlays, with the
output over COM1. Setup does not replace an `XHCITR.EXE` of the same version, so on guests that
already had an earlier kit of this branch the kit's XHCITR was copied over the installed one before
the window legs. Releases bump the version, so this affects development kits only.

### The owner's change before the legs (346258bc)

The quick-save hotkey defaults to Ctrl+Alt+F11 and stays off until ticked. The choices, in the
owner's order: Ctrl+Alt+F11, Ctrl+Shift+F11, Ctrl+Alt+F10, Ctrl+Alt+F9, Ctrl+F11 and
Ctrl+Alt+Shift+F11. For the two-key Ctrl+F11 the window says "On. Two keys clash with other
programs more often." A stored key that is not one of the choices reads as off. The commit
updates record 25 section 22 item 15, the README and the host vectors (test_trset 1742 checks,
0 failures). Reviewed with no findings, and the gated build passed.

### Fixes the legs found

| commit | finding | leg |
|---|---|---|
| 382f8b16 | XhciInterruptMode's note showed Setup's caution with a bare "%s"; it now reads "with 1 or 3, the controller does not start (Code 10) ..." (trs_caution_text over inst_caution_text, host vectors) | 98 SE / ME window |
| 9987cd4f | Stor98's StorWriteThrough choice read "auto - cacheoff on ME, off elsewhere (default) (default)" | XP Stor98 tab |
| cda9b0c1 | quick save's README.TXT named the snapshot "XHCITR.*"; the files are named by the snapshot run (for example 9201208A.*) | XP quick save |
| 1e1f641c | the quick-save end box (MB_ICONINFORMATION) played its own sound, so with "Sound when a quick save ends" on it sounded twice, and with it off it still sounded once; the box now shows the system icon as its own (MessageBoxIndirect, MB_USERICON, looked up at run time, so the import gate is unchanged) | 7 x64, the guest's audio captured to a WAV |
| 685129f8 | merge of 3.4.2.0 (ebcc97d6, with w47-hp12y as 303726e3) into w47-trset, so that tuning.md and the 47.40 driver code are present | - |
| 050ca4e1, 5ed313c0 | at the merge: XhciIntelNoRouteIrq 0 offered as "never: the controller is polled" with no warning, though since 47.40 it brings back STOP 0xA5 at startup under Windows 2000 on the listed HP EliteBooks. The row's 0 and its note now carry tuning.md's Windows 2000 meanings (1 and 2 poll the controller there). On Windows 2000 with a controller on the list (XhciPirqNoRouteListed over the hardware id's SUBSYS_), or with a device walk not read whole, choosing 0 asks first (No by default) in the window; /set prints the same and refuses unless /yes is given. Host vectors | Windows 2000 (below) |
| 8569e622, 2f813c73, 45f1590b, 41c22790, 9c6a52ee, bfe1896e, 6b289b86 | the owner's decisions of 2026-11-07: the icon's menu is Open, Settings..., About, Quick save (report + snapshot), Exit. Start at login, Warn when a device is removed unexpectedly, Tell me when a device can run faster and Watch Windows' own USB ports too move to the Settings window's new "xHCI98 USB Tree" tab (with the quick-save hotkey and its sound, each in force at once) and stay in the View menu. The Settings window opens with no xHCI98 device too. Quick save ends with a balloon (98 SE: the pop-up) whose click opens the folder (not on Windows 2000, whose balloons take no clicks); the sound option decides alone (NIIF_NOSOUND, MessageBeep), and a long text or a missing icon still gives a silent box. The screenshot findings: the list taller (about ten rows at 640x480), its separator "Advanced settings", a staged row "-> <new>" with the whole change in the note, rows not used on this computer "not used here", the Default column the value alone, the range text no longer painted over the value box's arrow, the hotkey note raised. Record 25 section 22 has no Help button, so none was added. Host vectors (trs_tray_item, tr_bkind_outstanding) | 98 SE, 7 x64 (below) |
| a3102daf | from the 7 x64 leg: 7's shell played no sound for the quick save's information balloon, so the balloon now always goes without its own sound and Windows' information or error sound plays once where "Sound when a quick save ends" is on (WAV: 184320 bytes on, 0 off); Apply is greyed on the tool's own tab | 7 x64 |

Each fix was reviewed with no findings left, and the gated builds passed at 346258bc, 382f8b16, cda9b0c1
and 1e1f641c. Each finding was checked again on the fixed binary: the note on 7 x64 ("with 1 or
3"), the single "(default)" on 7 x64's Stor98 tab, the README on 7 x64 ("9201208A.*  the
driver's snapshot (saved)"), and the sound on 7 x64. With the setting off a quick save wrote 0
bytes of audio; with it on, 274432 bytes. With the old box it was 180224 bytes off and 184320 on.

### Legs

| leg | 98 SE | ME | 2000 | XP (asm4242 ORICO) | XP x64 | Vista x86 | Vista x64 | 7 x64 (asm4242 ORICO) | 10 x64 |
|---|---|---|---|---|---|---|---|---|---|
| 1 install | PASS (exit 3/4: 4 from the base's registry-only ASM4242 entry) | PASS | PASS (exit 3) | PASS /thunderbolt /stor98 | PASS (in-guest signed) | PASS (exit 3; Found New Hardware windows closed first) | PASS (in-guest signed) | PASS /thunderbolt /stor98 (in-guest signed, Stor98 recorded) | PASS with /thunderbolt (CM staged, xhcicmf.inf staged; exit 4: Stor98 waits for the CM after the restart, as Setup says) |
| 2 the leg batch files | PASS | PASS | PASS (exit codes 0, 3, 0, 4, 3, 1, 1, 1, 1, 1, 1, 0, 4) | PASS (TB rows; Stor98 read-only, below) | PASS (XHCITR64) | PASS | PASS (XHCITR64) | PASS (XHCITR.EXE handing over to XHCITR64; StorPollMode=1 written) | OWED |
| 3 window | PASS (tray Settings..., change, Apply, restart question No, head "Changed, waiting for a restart", Restart now...) | PASS | PASS (Alt+V, N; restart question with the Device Manager note) | PASS (3 tabs, Applies to lists both controllers, TB change + restart question) | PASS (opens, rows) | PASS ("Apply as administrator..." + UAC Allow; replug-only change: no restart question) | - | PASS ("Apply as administrator..." with shield + UAC Yes, result box, Restart question) | - |
| 4 refusal while Setup runs | PASS (/set and window Apply: "Setup is running: try again when it has finished. Nothing was changed.") | - | - | PASS (/set, exit 1, XHCIWIZ.EXE listed) | - | - | - | - | - |
| 5 Stor98 tab | - | - | - | read-only, correctly (below) | - | - | - | PASS: writable; StorPollMode=1 by /set gave DriverParameter storpollmode=1 and Software\xhci98\Stor98\Settings storpollmode=1; 1 -> 0 by the window as administrator; after deleting Software\xhci98\Stor98 the tab reads "Stor98's settings are read-only here: Setup did not install this Stor98 ..." | - |
| 6 quick save | PASS (hotkey Ctrl+Alt+F11, tray menu, box, folder C:\XHCI98\Reports\<date> with 7 files) | PASS (Ctrl+F11 with its warning; hotkey off: no save) | - | PASS (hotkey, tray menu, 2 folders, README) | - | - | - | PASS (hotkey; documents fallback "C:\Users\test\Documents\xHCI98 Reports\..." + "(Your documents folder: ...)" once C:\XHCI98 was made read-only for users; sound on/off by the WAV) | - |
| 7 Setup regression | PASS: update, console uninstall (C:\XHCI98 kept for the Reports folder), fresh install; the USB settings head shows "You can change these settings later in xHCI98 USB Tree: View > Settings." at 640x480; INSTALL /console /yes /nopause /set XhciSilentInstall=1 merged SILENT.REG (6 SilentClasses values) and wrote the machine-wide value | - | - | PASS: update, console uninstall, fresh install; the head line at 1024x768 | - | - | - | - | - |

Notes:
- XP's Stor98 tab was read-only, and correctly so. After the console uninstall Windows kept
  Stor98 bound to the tunnelled NVMe, so the fresh install updated that Stor98 and recorded it
  as PreExisting=1 with no devnode list. The read-only state matches record 25 section 22
  item 8. Leg 5's writable half ran on 7 x64 (fresh install).
- On 7 the default ACL of C:\ gives Authenticated Users Modify on C:\XHCI98, so a quick save by
  an unelevated administrator (and by a standard user) goes to C:\XHCI98\Reports, not to the
  documents fallback. The fallback was checked by making C:\XHCI98 read-only for users.
- Audio: 7 x64 had an ich9-intel-hda with its output captured to a WAV. The other guests have
  no audio device, so MessageBeep was not heard there. 98 SE, ME and XP showed the box.
- The Alt+V, N keyboard path works where the window has the keyboard focus (2000, XP). On 7 the
  window opened from Run did not take the keys, so the View menu was clicked.
- 10 x64: test signing on, with the in-guest throwaway certificate. The first install stopped
  with exit 2: "xhcicmf.inf could not be staged (error 0xE000022F)". The host-made catalogs
  lacked xhcicmf.cat (the filter INF ships as cm\xhcicmf.inx). That is a gap in the leg kit,
  not in the package; the packager makes xhcicmf.cat for signed builds. With xhcicmf.cat made
  on the host and signed in the guest, the install staged every INF (exit 4, Stor98 waiting for
  the restart). After the restart the guest's keyboard input from QEMU stopped reaching the
  shell: Win+X opened Device Manager instead of the elevated prompt, and Run lines did nothing.
  Legs 2-6 on 10 x64 are owed. 7 x64 ran the same XHCITR64 build through every one of those
  legs.

### The XhciIntelNoRouteIrq fix and the owner's tray decisions: legs (2026-11-08)

Leg binaries: XHCITR from `6b289b86`'s gated build (98 SE install, 2000), then `a3102daf`'s XHCITR
build copied over it (7 x64, 98 SE tray); the guests are overlays of xHCI98-installed bases (2000,
7 x64) and of the q35 98 SE base. Screenshots held on the maintainer's disk.

- Windows 2000, XhciIntelNoRouteIrq: the controller (QEMU's xHCI) was given an HP EliteBook 8470p's hardware id (PCI\VEN_8086&DEV_1E31&SUBSYS_179B103C) in its Enum key by a leg fixture run as SYSTEM, so the Tree takes the computer for one on the list (the Device Manager name then reads "Intel 7 Series"). The row's 0 reads "never: the controller is polled (Windows 2000: see the note)"; choosing 0 and Apply asks the question (No by default); No stages nothing and the value stays 1. `XHCITR /set XhciIntelNoRouteIrq=0` prints the same text and "To set it anyway, add /yes. Nothing was changed." (exit 1); with /yes it sets 0 (exit 3), and /set ...=1 sets it back. PASS.
- 98 SE: the icon's menu is Open, Settings..., About, separator, Quick save (report + snapshot), separator, Exit. The Settings window: the USB tab's list shows about eleven rows with the advanced ones, "Advanced settings" as the separator, "not used here" for XhciIntelPortSwitch, the Default column the value alone, a staged change as "-> 1 - interrupt only" with "Changed here, not applied yet: 0 - automatic -> 1 - interrupt only." in the note, the value box whole with the focus in the list; the "xHCI98 USB Tree" tab with Start at login, Tell me when a device can run faster, Watch Windows' own USB ports too, the hotkey and the sound (the removal warning is NT's only), Apply greyed; Close asks "Discard the changes not applied?". Quick save from the menu: the pop-up "Quick save done", "Saved to C:\XHCI98\Reports\<date time>. Click to open the folder."; its click opened the folder in Explorer. PASS.
- 7 x64: the same menu; the tool tab; Watch Windows' own USB ports too and the hotkey turned on there, the View menu then ticked the same; Ctrl+Alt+F11 gave the balloon "Quick save done" with the folder; its click opened the folder; the sound by the WAV (a3102daf): on 184320 bytes, off 0. PASS.

### 98 SE: the VWIN32 exception after a console uninstall

During leg 7 on 98 SE, the first Shut Down after the console uninstall stopped with "A fatal
exception 0E has occurred at 0028:C029983B in VXD VWIN32(05) + 00002363" and then hung at
"Windows is shutting down". After a forced power-off the next boot was clean: Windows offered
the Add New Hardware wizard for the controller, as uninstall says it will. That guest had been
through an earlier-tree install, two /force updates, the Settings window, quick saves and
INSTALL /set XhciSilentInstall=1. The check that followed used fresh overlays of the q35 98 SE
base:

| kit | trials | steps | Shut Down after the uninstall | next boot |
|---|---|---|---|---|
| this branch (3.4.2.0 tree with 47.7) | 2 | install, restart, console uninstall, Shut Down | clean power-off, both | Add New Hardware for the controller (clean) |
| this branch | 1 | as above plus hotkey on, quick save, INSTALL /set XhciSilentInstall=1 | clean power-off | - |
| released 3.4.1.0 | 2 (2 more void: a stray key kept the uninstall from starting, no log header) | install, restart, console uninstall, Shut Down | clean power-off, both | USB HID wizard (3.4.1.0 leaves the controller driven) |

Not reproduced in 3 trials of this kit. A one-off on the first guest; not attributed to a kit.

### Owed
- 10 x64 legs 2-6 (above). Vista x64: the window was not opened (the leg batch file passed; 7 x64 and XP x64
  ran the same XHCITR64).

### Owed (bench)
- The port switchover's warning (XhciIntelPortSwitch): no Intel 7-9 series xHCI on QEMU, so the
  row is greyed there ("Intel 7-9 series, C610 only").
- The XhciIntelIrqRoute and XhciIntelNoRouteIrq rows: listed only where a controller passes
  XhciPirqReviewed; QEMU's xHCI is not Intel.
- A standard-user account (a separate login) for "Apply as administrator..." and quick save.
  Legs 3 and 6 ran as an unelevated administrator, which takes the same paths (no write rights,
  the UAC prompt), with the consent prompt in place of the credential prompt.

### Merge

Merged into `3.4.2.0` at `47239491` (2026-11-08), with no conflict.

## 47.44 Thunderbolt networking interop: Windows 11's inbox USB4 P2P adapter, Windows 10, XP and Linux, IPv4 fragmentation on receive and no E2E on the ASM4242 (2026-11-07/08, w47-tbwin)

Branch `w47-tbwin` from 3.4.2.0 `3519c133`: `32e338a1` (IPv4 fragmentation
on receive, record 35 revision 14) and `0efc141d` (no E2E on the ASM4242,
record 35 revision 15), committed with the owner's leave, their trees equal
to the patches the legs ran. Development host A. Emulator: the usb4-emu
QEMU fork `c918493` (main `5c0f918`'s export, usb4-xdomain and the Alpine
Ridge firmware model of 45-E.15), SHA-256 `53a3811a...`; each matrix cell,
its numbers and the patch set it ran are held on the maintainer's disk.

**Windows 11 (the owner's Framework).** Public sources: Windows 11 runs
its own USB4 connection manager and ships `USB4P2PNetAdapter.sys`, "USB4(TM)
P2P Network Adapter", its USB4NET implementation, on by default, also with a
Thunderbolt 3 peer (Microsoft Learn, "USB4 inter-domain connections");
Intel's `tbtp2pndisdrv` belongs to the firmware-CM stack. Read in the
emulator: Windows 11 Pro 26100 on `usb4-generic` with its inbox CM, cabled
to 98 SE on the P14s model (`tb3-alpine-ridge`): with nothing installed,
Windows bound the adapter to xHCI98's service
(`USB4\VEN_0000&DEV_00000001&PROT_USB4NET`), up 11 to 50 s after the plug,
20 Gb/s, MTU 62000, link-local address, Public profile; ping both ways
(Windows' firewall off for the inbound one), TCP both ways (98 SE to 11
75.1 Mb/s with -P 4, 11 to 98 SE 7.76 Mb/s). A 3000-byte ping from Windows
11 went unanswered: xhcinet passed up a frame within its maximum and split
only TCP. With Windows' MTU set to 1500 by hand, 3000 and 8000 bytes
answered. The Windows 11 guest bugchecked 0x3B (C0000096) at one kernel
offset every 6 to 25 minutes, with the cable in or out and before the peer
existed: a model/TCG matter, not this driver; the tests ran inside the
windows between.

**The fix, `32e338a1` (record 35 revision 14, sections 2.5 and 7.4).**
`net_frame.c` splits an oversize IPv4 datagram the TCP split does not take
into IPv4 fragments (RFC 791): the first with the whole header, the later
ones with the copied options; offsets from the original's; MF on all but
the last, which keeps the original's; the header checksum computed again;
Don't Fragment dropped and counted (`NET_SEGDROP_DF`); a malformed option
list, a fragment with MF and unaligned data, and a datagram past 65535
bytes refused. Design reviewed and revised, code reviewed (nothing
material). Host test `test_net_frame` 5163 checks, 0 failures. Gated x86
and amd64 builds of every flavour BUILD + GATES
PASSED on the tree with the change (uncommitted then); xhcinet.sys x86
`28F01E39...`, xhcinet6.sys x86 `F709A844...`. Legs, the image copied over
the installed one:
- Windows 11 (MTU 62000 again) to 98 SE: ping 3000 bytes 3/3, 8000 3/3,
  30000 2/2; UDP of 8000-byte datagrams at 20 Mb/s, 15.0 Mb/s received
  (26 % loss, the 98 SE receiver).
- Linux 7.0 (Kubuntu 26.04.1 live, stock `thunderbolt_net`) at MTU 9000 to
  98 SE and to ME (Intel host model): `-M dont -s 8000` 3/3, `-s 30000` 2/2;
  `-M do -s 8000` 0/2 (DF: dropped by design); ping 4/4 both ways, login
  and E2E paths as before.
- 98 SE to XP (cell c25).

**The ASM4242 (`0efc141d`, record 35 revision 15, section 5.3).** 98 SE on
the P14s model cabled to XP on the ASM4242 model: discovery, login, paths
and credits both ways, but the XP end's transmit ring never put a frame on
the link (link counters frame-out 0 / frame-in 42), no ping answered.
usb4-emu models the ASM4242's quirk b from a public `thunderbolt-net`
patch: a transmit ring other than ring 0 with E2E never completes a
descriptor; xhcicm enabled E2E on both rings whenever both blocks carry
`prtcstns` bit 0. The software CM now withdraws bit 0 on the ASM4242's host
router (174C:2428), as the firmware CM does on Falcon Ridge. Design and
code reviewed and revised (the log's frequency, the text aligned).
`test_u4xd` 231 checks, 0 failures (new `test_asm_no_e2e`).

**After the ASM4242 fix** (both changes; gated x86 and amd64 BUILD + GATES
PASSED on the tree with both, uncommitted then; xhcicm.sys x86
`3C835396...`, xhcinet.sys `F7060C14...`, xhcinet6.sys `963FBFBB...`):
- c13 again (c26): ping 6/6 both ways, frames both ways, no credits (E2E
  off by design); TCP 20.1 / 4.5 Mb/s.
- XP on the ASM4242 to Windows 10 on Intel (c28): 6/6, TCP 17.5 / 17.1 Mb/s.
- 98 SE to Windows 10 (c27): 6/6, 24.5 / 2.7 Mb/s, replug 6/6.
- **Open:** on the ASM4242 end a cable out and in (from either end) leaves
  the service down for good - the ASM4242 end's connection manager sends no
  inter-domain packet afterwards (its link counter ctl-out stays where it
  was while the far end's requests keep arriving), pings 0/6; the Intel end
  recovers (c11, c27). Fixed by `04df69e1` (below).

**98 SE disk prompt (the owner's decision).** After the installer's restart
98 SE installs "xHCI98 Thunderbolt Networking" and asks for the "xhci98 xHCI
Host Controller Disk" (`C:\XHCI98` answers it); ME did not ask. Four
variants were tried; only an INF whose 9x sections copy nothing, with Setup
placing `xhcinet.sys` itself, installs with no prompt. That breaks the
manual Have Disk route on 98 SE and ME; the options and a recommendation
are with the owner. Not implemented.

**Benchmark.** iperf 2.0.8 (iperf2's SourceForge project,
`iperf-2.0.8-win.zip` `11a581d9...`, `iperf.exe` `575691cf...`) loads and
runs on plain 98 SE; the same binary on XP, 10 and 11. Emulator throughput
is bounded by TCG and the host's load: relative only. A bench sheet for the
owner's P14s and Framework is with the owner.

### The ASM4242 replug (`04df69e1`, record 22 section 18.9, record 35 revision 16; 2026-11-08, w47-asmrp3)

Branch `w47-asmrp3` from `2c32ec0f`, one commit, its tree equal to the
change the legs ran (then a delta on the two above, uncommitted).

*Cause.* usb4-emu's ASM4242 quirk a (`asm-quirk-pending`, on by default,
from a public Linux patch that stops waiting on that bit): the host
router's Host Interface adapter keeps Pending Packets set on a path entry
once its Valid is cleared. At an unplug xhcicm cleared the network
service's transmit entry (Host Interface, HopID = ring 1), waited out the
500 ms drain and quarantined the entry; the quarantine lifts only on a read
of Valid 0 and Pending Packets 0, which never comes. The entry, pending
with its output at the port's lane 0 adapter, kept the lane busy in the
lane scan, so every later connection of the port was deferred (Guide 7.1's
no-path rule): the port was never classified again and nothing was sent on
the link (c26's ctl-out that stayed put). Read in the qemu flavour's CM log
on the XP end: `u4.quarantine=00050001`, then `u4.connect.deferred=00000001`
at every check. With the model's quirk off (`asm-quirk-pending=off`) the
same replug recovered (6/6 both ways). The model does what its cited source
says, so this was the driver's fault, not a modelling gap; no usb4-emu
change.

*Not the E2E change.* The qemu-flavour xhcicm.sys of `3519c133` with the
fragmentation change applied (it does not touch `src\cm`) shows the same:
no traffic before the replug (E2E, as c13), login and paths up, and at the
replug the same quarantine and deferral for good.

*Fix.* On the ASM4242's host router only (ROUTER_CS_0 174C:2428, the
identity the E2E change keys on) and on its Host Interface adapter only,
every path-entry read returns Pending Packets as 0 (`tbtU4PpStuck`/
`tbtU4PpMask` in `tbtU4RdN`), so the drain, the lane scan, the quarantine
retry and the sweeps all take an entry there as drained at Valid 0, as the
Linux patch does; every other router and adapter keeps the wait and the
quarantine. Counted (`PendingIgnored`, `u4.pending.ignored` in the counter
block) and the first one logged (`u4.asm.pp`). Host vectors
`test_asm_replug_pp` (fails 6 checks without the change) and
`test_pp_stuck_other` in `test_u4xd` (251 checks, 0 failures). Design
reviewed and revised (one wording correction); code reviewed (nothing
material), and once more after the counter-name table was added (nothing
material). Gated x86 and amd64 builds of every flavour BUILD + GATES PASSED
on the tree with all three changes (uncommitted then): x86 xhcicm.sys
`04C036E6...`, xhcinet.sys `1EA8F8D7...`, xhcinet6.sys `CE0B04F0...`; amd64
xhcicm.sys `6C2F5FD7...`, xhcinet.sys `EA4152E1...`, xhcinet6.sys
`EB4BF467...`.

*Legs (the gated x86 release images on the XP end).*
- c26r (98 SE TB3 <-> XP ASM4242): ping 6/6 both ways before, after a
  replug from the 98 SE end and after one from the XP end.
- c28r (XP ASM4242 <-> Windows 10 x86 Intel, with the `ca0001e1` drivers):
  6/6 both ways before, after a replug from XP and after one from Windows
  10.
- c11r (98 SE TB3 <-> XP Intel, regression): 6/6 both ways in all three
  rounds, E2E on.
- With the qemu-flavour image (c26's pair): `u4.asm.pp=00050001`, nothing
  quarantined, `cm.net.up` again after each replug.

The c26 and c28 replug failures are fixed in the emulator; iperf was not
rerun.

*Note.* The fix acts by default (the owner's approval of 2026-11-08), keyed
on the router's identity (as the E2E change and as Linux's patch), not
behind a registry value; the bit's behaviour comes from a mailing-list
patch and the model, not the owner's bench. A first try put the emulated
cable on a TCP port inside Windows' ephemeral range, and the reconnecting
XP end's socket connected to itself (a loop in the CM log); the cable was
moved to ports outside it.

### Merge

Merged into `3.4.2.0` at `2dce422d` (2026-11-08), with no conflict.
The replug fix merged at `cf174e8c` (2026-11-08), with no conflict.

### Left

- The owner's bench: the P14s on 98 SE to the Framework on Windows 11.
- The owner's bench reading of a replug on an ASM4242.
- The 98 SE disk prompt: fixed by `f968c677` (below, "47.44 (continued)").

## 47.44 (continued): Thunderbolt networking on 98 SE and ME with no disk prompt, and XHCITR's Windows 2000 XhciIntelNoRouteIrq=0 question (2026-11-08, w47-xfix)

Branch `w47-xfix` from 3.4.2.0 `2dce422d`: `75c410f8`, `59abc1eb`, merge
`029c560b` (3.4.2.0 `cf174e8c`; this change renumbered as record 35
revision 17, after the replug's revision 16), merge `3cbf3217` (3.4.2.0
`b02520d9`, roadmap and run sheet only). Development host A. Emulator: the
usb4-emu QEMU fork of the 47.44 legs (SHA-256 `53a3811a...`). Peer: Linux
7.0 (Kubuntu 26.04.1 live, stock `thunderbolt_net`) on usb4-generic,
169.254.50.1.

**The change.** On the owner's decision of 2026-11-08, `xhcinet.inf`'s 98 SE
and ME sections (`[Net.Dev]`, `[Net.Ndis]`) copy nothing. Setup places
`SYSTEM\xhcinet.sys` itself: `sys_9x` records it under `Placed9x`, queues it
through `WININIT.INI` over a file already there, and the uninstall withdraws
and deletes it. The INF gate's NET9X-COPY rule becomes NET9X-NOCOPY, which
also refuses Include and Needs. The record changes are record 35 revision 17
and record 26 draft 76, and the README gains a note. XHCITR's Windows 2000
question for `XhciIntelNoRouteIrq` 0 (47.35's Settings window, 47.40's
strip) now says that Last Known Good undoes the change only if Windows has
not started with it yet, as `tuning.md` does. Design, code and merge
reviewed and revised; the owner approved the new strings.

**Builds.** `029c560b`: gated x86 and amd64 release, BUILD + GATES PASSED,
dirty=0. The tools passed, and the host tests passed (`test_inst` 8961
checks, `test_trset` 1784, 0 failures). An earlier attempt at `59abc1eb`
stalled on the host's program-start hang and was stopped. `3cbf3217` adds
only roadmap and run-sheet text (text check OK).

**Legs.** These ran on a leg kit: 3.4.2.0 `3519c133`'s package (its
drivers; this branch changes no driver) with this branch's `xhcinet.inf`
and its XHCIINST, XHCIWIZ and XHCITR (`59abc1eb`, the same sources as
`029c560b` for these files). The 98 SE guests used the P14s model
(`tb3-alpine-ridge`). `xhcinet.sys` was SHA-256 `F5FA7B93...` in every
placement.

| Leg | Result |
|---|---|
| 98 SE, released 3.4.1.0, `/thunderbolt /tbnet`, restart (baseline) | "Insert Disk: xhci98 xHCI Host Controller Disk" (the defect, reproduced); `C:\XHCI98` answered it |
| 98 SE fresh, `INSTALL /console /yes /nopause /thunderbolt /tbnet` | exit 3; log "Copied xhcinet.sys to C:\WINDOWS\SYSTEM\xhcinet.sys.", Placed9x recorded. Restart: the adapter installed with **no disk prompt**, then "restart your computer now?". Cold start with the cable: APIPA 169.254.219.30, Linux to 98 SE ping 4/4. **PASS** |
| ME fresh, same | install done; restart: **no disk prompt**, "restart now?". Cold start with the cable: desktop, SYSTEM\xhcinet.sys and Placed9x present, 169.254.117.73, ME to Linux 4/4 and Linux to ME 4/4. **PASS** |
| 98 SE update over 3.4.1.0 (networking on), same command | exit 4 (steps of the base, not networking's); log "placed9x: WININIT.INI [rename] C:\WINDOWS\SYSTEM\XHCINET.SYS=C:\WINDOWS\SYSTEM\XHCINET.IN$", "Setup will replace C:\WINDOWS\SYSTEM\xhcinet.sys at the next start", recorded. Cold start: no prompt, the file is the package's (15,965 bytes), WININIT.INI empty, adapter 169.254.46.186; cabled, Linux to 98 SE 4/4. **PASS** |
| 98 SE uninstall (`INSTALL /console /uninstall /yes /nopause`), adapter loaded | exit 3; "Deleted C:\WINDOWS\SYSTEM\xhcinet.sys (placed there by Setup)." with the other placed files. **PASS** |
| 98 SE reinstall (run before the restart the uninstall asked for), then restarts | the controllers' wizards ran again: the TB3 controller from XHCICM.INF with no prompt, and the USB 3 controller from XHCI98.INF asking once for `xhci98.sys` (the known controller prompt, record 26 draft 40; not networking's). Cold start with the cable: desktop, no network prompt, SYSTEM\xhcinet.sys and Placed9x present, 169.254.89.236, 98 SE to Linux 4/4 and Linux to 98 SE 4/4. **PASS** |
| 2000, the controller given an 8470p hardware id by a leg fixture run as SYSTEM, XHCITR `59abc1eb` | `/set XhciIntelNoRouteIrq=0` printed the new text and "To set it anyway, add /yes. Nothing was changed.", exit 1. In the window, choosing 0 and Apply asked the new question; answered No. **PASS** |

Not read: a manual Have Disk install with no Setup run (the accepted
trade-off).

### Merge

Merged into `3.4.2.0` at `f968c677` (2026-11-08), with no conflict.

## The early-review fixes before the cut (2026-11-08, w47-cxfix)

Branch `w47-cxfix` from 3.4.2.0 `ba329968`, the owner's approval of each
fix; development host A.

| Commit | What |
|---|---|
| `9a1bd610` | Stor98's package readme: a 4 KiB-sector drive works on every system |
| `7a07ee3b` | docs and the INF gate's text: stale and testing-status wording fixed |
| `a1dd9ba0` | XHCITR's import gate compares function names case for case |
| `906b60ab` | XHCITR: a held setting read per controller, not from the first (`test_trset` vectors) |
| `686a196a` | no reporter's or tester's machine named in the 47.33 task line and the `2.2.0.0` and `2.3.1.0` readings |

Reviewed twice before the merge, nothing material. Gated x86 and amd64
release builds at `686a196a`: BUILD + GATES PASSED. Merged into `3.4.2.0`
at `7cf31247` (2026-11-08), with no conflict; the merge's tree equals
`686a196a`'s, so those builds stand for it. The merge reviewed, nothing
material.

## 47.45 (was UASBOT.1): the Bulk-Only form of a UAS drive gets its own devnode (2026-11-08, w47-uasbot)

Branch `w47-uasbot` from `2c32ec0f`; development host A.

### The task

Roadmap UASBOT.1, now 47.45 (from 47.39, issue 26; the owner's option 1,
2026-11-08). A drive that offers Bulk-Only and UAS, installed with
`xhciuas` on a controller that streams, kept that devnode on a controller
that cannot (the ASM1042's `XhciStreamsOff` quirk), where the bus offers it
as Bulk-Only, and stopped at Code 10. The owner's scope and gate of the
same day:
- a new identity for the Bulk-Only form only, used where the controller
  cannot stream or `XhciForceBulkOnly` is set;
- that identity is the serial plus a marker, with `UniqueID` TRUE;
- a value, on by default, whose 0 keeps today's single entry.

The owner approved the user-facing strings T1 to T10 and the README row
the same day, along with the name `XhciBulkOnlyOwnId`, the value being per
controller, the force value on a streaming controller included (Q1), and
absent reading as 1 (Q2).

### The design

The Bulk-Only choice (`XHCI_XPORT_BOT`, why 4 or 5), under the
controller's `XhciBulkOnlyOwnId`, sets `XHCI_XPORT.OwnId`. The PDO's
instance id then gains `&BOT`, after the serial id (and `&nn`) or after
the location key (and `nn`). `UniqueID` is unchanged.

Design record 13 section 10.7.3 has the rule and its reasons:
- one entry per drive, not one per parent;
- the device id is kept, so hand-listed `usbstor.inf` lines still match;
- the location form gets the marker too, for the same-port case.

The section also covers the 98 SE and ME dormant and duplicate rules, the
length (162 of 200), text-mode Setup, and drives already installed.

The value is read per controller, from the machine-wide key first, at each
plug. Absent reads as 1, and the INFs write 1 without overwriting.

The design was reviewed and revised once (XHCITR's note now depends on the
driver version, the legs include Windows 2000, the gate-off caveats are
kept, and the texts say "at SuperSpeed"), then reviewed with nothing found.

### The change

| Commit | What |
|---|---|
| `da776137` | **Driver:** `xhci_xport` `OwnId` and `XHCI_XPORT_F_BOT_OWN_ID`; `XhciFuncInstanceId`'s `bot` (`XHCI_INSTANCE_BOT`, `XHCI_INSTANCE_ID_BYTES`); `hcdInstanceQueryId`, `hcdXportFlags`; `HcdCtlBulkOnlyOwnId`.<br>**INFs and gate:** both INFs (`HKR,,XhciBulkOnlyOwnId,0x00010003,1` on every path); the INF gate's VAL row, mutation vectors and footprints.<br>**XHCITR:** the Code 10 note (own-id input clear, worded by driver version), the Settings row, the replug summary.<br>**Setup:** the Advanced row, the `/set` message, the `XhciForceBulkOnly` caution.<br>**Host vectors:** `test_xport`, `test_func`, `test_xhq`, `test_trset`, `test_inst`.<br>**Records:** 13, 23, 25, 26 and `build-and-test.md` |
| `cc798aba` | `tuning.md` (the value's row and the forcing section); issue 26's ASM1042 paragraph |
| `ebcab3c8` | The README settings-table row |
| `e9f1ec66` | INF gate self-tests: the value-rows anchor includes the new row |

The release notes' text (T4, T5, T5b, T5c) went to the notes branch. The
code review's one finding, stale text in the release notes, went there too.

### Builds

Gated x86 and amd64 release at `e9f1ec66`: BUILD + GATES PASSED for both;
x86 host tests passed, INF gate self-tests 1758 checks.
- The first gated run failed 435 INF-gate self-test checks: the mutation
  anchor `$tolRows` did not include the new INF rows. Fixed in `e9f1ec66`.
- Build steps must run with stdin from NUL. Without it, `src\pfw`'s
  `link.exe` waited at 0 CPU.

| File | SHA-256 |
|---|---|
| `out\pkg-release-x86\xhci98.sys` | `a544990e6ca344ddadcc7b9f29b4cf5f6ab6d4c8bf682e3db25690b0d4804375` |
| `out\pkg-release-x86\xhciuas.sys` | `e1dd5f6cfc76dcc6b5938eba56b904c690baab63a17a0980c94517e89535c0ac` |
| `out\pkg-release-amd64\xhci98.sys` | `290c1feaad0d2a68b2ac1addc8b3847b4fdcaa7f771ee67a2beec601d12cce35` |
| `xhcitr\XHCITR.EXE` | `bd2373f0fe09ba7aea07ab814c409a68f73db06fc70ed3666c49f03ff5f02d13` |

The build's version stamp still reads 3.4.1.0; it is bumped at the cut.

### The legs

**Setup**
- **Machine:** q35 guests under the usb-redir emulator build `cb36497`,
  with `qemu-xhci` at 00:14.0. It streams, so "can stream" is
  `XhciStreamsOff` 0 and "cannot stream" is `XhciStreamsOff` 2, set
  machine-wide with a restart.
- **Drives:**
  - Model A: a simulated drive `0781:55A9` over usb-redir with serial
    `UBSER0001`. Bulk-Only at alternate 0 is implemented over a FAT image;
    UAS at alternate 1 is accepted but not run.
  - Model B: the same drive with serial "NO SERIAL". The space is refused
    by the bus, so it gets the location form.
  - The stick: QEMU `usb-storage` (`46F4:0001`, Bulk-Only only), serial
    `UBBOT0001`.
- **Install:** XP, 7 and 2000 installed this build's x86 kit with
  `INSTALL.BAT /console /yes`. XP x64 is a `3.4.1.0` install with this
  build's amd64 `xhci98.sys` copied over, so the INF value is absent and
  reads as 1.
- **Phases:** each phase is a cold boot; a logon runner wrote the Enum keys
  (`regedit /e`), the volumes and `XHCITR /report` to COM1.
- **What showed the driver used:** the model's trace showed which driver
  started on it. `xhciuas` selects alternate 1; `usbstor` selects
  alternate 0 and sends CBWs.

| Phase | Before it | XP x64 | XP | 7 x86 |
|---|---|---|---|---|
| p1: the UAS form installed with `xhciuas` | streams on | `UBSER0001` and `5&1d4e91ea&0&4` on `xhciuas` (a guest-only install helper, Continue Anyway); the model selected alternate 1 | the same two keys on `xhciuas`, installed through the Found New Hardware wizard (the user's path; Continue Anyway) | the same two keys on `xhciuas`, installed by Windows itself from the `xhciuas.inf` Setup had staged, once its unsigned-driver dialog was answered (the install helper failed with 0xE000020B, as on 2000) |
| p2 (a): the controller cannot stream | `XhciStreamsOff` 2 | **new** `UBSER0001&BOT` and `5&1d4e91ea&0&4&BOT`, `USBSTOR`, Problem code 0, F: and G: mounted; XHCITR "Bulk-Only (the device offers UAS too; this controller cannot stream at SuperSpeed)", Instance ID `...\UBSER0001&BOT`; the plain keys keep `xhciuas`; no manual step, no Code 10 | the same: new `&BOT` keys on `USBSTOR`, Code 0, F: G: mounted | the same, and 7's "Your device is ready to use" balloon |
| p3 (b): streams on again | `XhciStreamsOff` 0 | the plain keys, `xhciuas` (alternate 1 selected again), no new key; the `&BOT` keys kept, not present | the plain keys `UBSER0001` and `5&1d4e91ea&0&4`, `xhciuas`, no new key; the `&BOT` keys kept, not present | the plain keys, `xhciuas`, no new key; the `&BOT` keys kept, not present |
| (c): the Bulk-Only-only stick | every phase | one key, `USB\VID_46F4&PID_0001\UBBOT0001`, `USBSTOR`, Code 0, throughout | the same | the same |
| (d): no serial | every phase | model B: `5&1d4e91ea&0&4` (UAS) and `5&1d4e91ea&0&4&BOT` (Bulk-Only): its own devnode for each form | the same | the same |
| p5 (e): `XhciBulkOnlyOwnId` 0 | `XhciStreamsOff` 2, `XhciBulkOnlyOwnId` 0 | today's behaviour: both drives offered as Bulk-Only under their **plain** ids (`USB\VID_0781&PID_55A9\UBSER0001`, `...\5&1D4E91EA&0&4`), driver `xhciuas`, **Problem code 10**, XHCITR's Code 10 note shown; XHCITR "XhciBulkOnlyOwnId (advanced): 0 - no: one entry everywhere (before 3.4.2.0) (machine-wide)"; inputs 0x1 (no own-id bit) | the plain ids, `xhciuas`, **Problem code 10**, XHCITR's Code 10 note (the older wording, below) | the plain ids, `xhciuas`, **Problem code 10**, XHCITR's Code 10 note (the older wording, below) |

**XHCITR's note at `XhciBulkOnlyOwnId` 0.** In p5 the note has the older
wording on every system ("...Its Device Manager entry was most likely
installed on a controller that streams..."), not the new wording that
names `XhciBulkOnlyOwnId`. The reworded note shows from driver `3.4.2.0`
on: `tr_xport_unrunnable` (`xhcitr\tr_log.c`, line 647) picks it only when
the query answers driver version `3.4.2.0` (`TR_XPORT_OWN_ID_DRIVER`) or
later, and this build still answers `3.4.1.0`; this is not a fail.
`test_xhq` holds the predicate's answer: `TR_XPORT_UNRUN_GATE` at
0x03040200 and above (and at 0x04000000), `TR_XPORT_UNRUN_OLD` at
0x030401FF and below, and none when the own-id bit is set. No host test
covers the note's text itself, which is printed in `xhcitr\xhcitr.c`, so
it is checked after the version bump. The driver half of (e) passes: the
plain ids, `xhciuas`, Code 10.

### Windows 98 SE legs

Build: the gated x86 package of `w47-uasbot` at `e9f1ec66` (`xhci98.sys`
a544990e..., `xhciuas.sys` e1dd5f6c..., `XHCITR.EXE` bd2373f0...; the file
version still reads 3.4.1.0). Machine: q35, qemu-xhci at 00:14.0, the
`cb36497` emulator with usb-redir; model A on xHCI port 3, model B on
port 4, the stick on port 1. Every boot with no USB device, the three
hot-plugged at the desktop. p3 and p5 ran on a copy of the guest taken
after p2.

| Phase | Configuration | Result |
|---|---|---|
| p1 | `XhciStreamsOff` 0 (not set), `XhciBulkOnlyOwnId` not set (1); first plug of A, B and the stick, the Add New Hardware wizard for each, power-off, cold boot, plug again | Enum `USB\VID_0781&PID_55A9\UBSER0001` (`Driver` `USB\0002`) and `...\4USB&XHCI98_ROOT_HUB&0PCI&VEN_1B36&DEV_000D&SUBSYS_11001AF4&REV_01&BUS_00&DEV_14&FUNC_00` (`USB\0003`), both "xHCI98 USB Attached SCSI Storage" (xhciuas); the driver chose UAS for both (`XportUas` 2). The stick `USB\VID_46F4&PID_0001\UBBOT0001`, USBSTOR (`USB\0004`), Code 0, volume E:. XHCITR has no Transport line for A and B: the emulator's usb-redir disconnects both when xhciuas asks for streams ("usb-redir error: peer does not support streams ... disconnecting", the model's limit), so at the report they are gone |
| p2 (a) | `XhciStreamsOff` 2 (machine-wide), cold boot, plug: the wizard for the two new devnodes (USBSTOR), power-off, cold boot, plug again | NEW Enum keys `USB\VID_0781&PID_55A9\UBSER0001&BOT` (`USB\0005`) and `...\4&BOTUSB&XHCI98_ROOT_HUB&0PCI&VEN_1B36&DEV_000D&SUBSYS_11001AF4&REV_01&BUS_00&DEV_14&FUNC_00` (`USB\0006`), "USB Mass Storage Device", compatible id `...PROT_50`. XHCITR for both: "Transport: Bulk-Only (the device offers UAS too; this controller cannot stream at SuperSpeed)", Instance ID `...\UBSER0001&BOT` and `...\4&BOTUSB&...`, Driver USBSTOR, status 0x19886ACE (started), Problem code 0. `XportBotNoStreams` 2. Volumes E:, F:, G: (the three disks). No Code 10. The plain keys unchanged (`USB\0002`, `USB\0003`, xhciuas). **PASS** |
| p3 (b) | `XhciStreamsOff` 0, cold boot, plug | No new Enum key (the same five instances as p2); the driver chose UAS again (`XportUas` 2, `set_alt` 1 in both models' logs), so the drive is back on the plain entries `UBSER0001` and `4USB&...` with xhciuas (`USB\0002`, `USB\0003`); the `&BOT` entries not used. As in p1 the emulator then drops A and B (streams), so XHCITR shows only the stick; volume E: only. **PASS** (the instance paths and drivers; the UAS run itself is not in this model) |
| (c) | every phase | The stick has one Enum instance, `USB\VID_46F4&PID_0001\UBBOT0001`, USBSTOR `USB\0004`, Code 0, in p1, p2, p3 and p5. **PASS** |
| (d) | every phase | Model B: the plain location key `4USB&XHCI98_ROOT_HUB&0PCI&...` in p1 and p3 (xhciuas), `4&BOTUSB&XHCI98_ROOT_HUB&0PCI&...` in p2 (USBSTOR, Code 0). **PASS** |
| p5 (e) | `XhciStreamsOff` 2, `XhciBulkOnlyOwnId` 0 (machine-wide), cold boot, plug | No new Enum key; XHCITR: "XhciBulkOnlyOwnId (advanced): 0 - no: one entry everywhere (before 3.4.2.0) (machine-wide)"; A and B "Transport: Bulk-Only (the device offers UAS too; ...)" on the PLAIN ids `USB\VID_0781&PID_55A9\UBSER0001` and `...\4USB&XHCI98_ROOT_HUB&...`, Driver xhciuas, status 0x19806EA6 (not started), **Problem code: 10**; volume E: only (the stick). The driver half **PASS**. XHCITR's note is the older one, as on the NT systems above (not shown with this build; it follows the version bump) |

Registry readings: `HKLM\System\CurrentControlSet\Services\USB` before p5
(`start /w regedit /e`): `XhciStreamsOff`=2, `XhciBulkOnlyOwnId`=0; after
p5's restore `XhciStreamsOff`=0 and `XhciBulkOnlyOwnId` removed. The
controller's driver key `...\Services\Class\USB\0000`: `XhciBulkOnlyOwnId`=1
(the INF's value). From a 98 DOS box `regedit` must be started with
`start /w`: without it the export can precede the import.

### Windows 2000

Build: the gated x86 package above (`xhci98.sys` `a544990e...`,
`xhciuas.sys` `e1dd5f6c...`, `XHCITR.EXE` `bd2373f0...`); the version
stamp reads 3.4.1.0. Machine: q35 2000 Professional (Standard PC HAL), the
`cb36497` usb-redir emulator, `qemu-xhci` at 00:14.0; a copy of the 2000
guest with this build installed (exit 3). Model A on xHCI port 3, model B
on port 4, the stick on port 1, all at power-on. A logon runner wrote the
Enum keys, the USB class keys (the controller's driver key with
`XhciBulkOnlyOwnId`, and each function's `DriverDesc`, `MatchingDeviceId`
and `InfPath`) and XHCITR's lines to COM1. Every phase a cold boot after
Shut Down.

**p1's install (the user's path).** On the first boot two Found New
Hardware wizards came up for "xHCI98 USB Attached SCSI Storage" (the UAS
forms of A and B). Each was taken through Welcome, Next (Search for a
suitable driver), Next (no optional locations), "Windows found a driver ...
c:\winnt\inf\oem1.inf" Next, Completing, Finish, then "You must restart"
No. No signing prompt came up (policy Ignore). A "PCI Device" wizard (an
unrelated q35 PCI function with no driver) comes up at every boot; it never
belonged to a USB device.

**Round 2, with `xhciuas` installed through the wizard:**

| Phase | Configuration | Result |
|---|---|---|
| p1 | `XhciStreamsOff` 0, boot after the wizard installs | **PASS.** `USB\VID_0781&PID_55A9\UBSER0001` and `...\4&38c5c8b0&0&4`: Service `xhciuas`, DeviceDesc "xHCI98 USB Attached SCSI Storage", class keys 0004 and 0003, `MatchingDeviceId` `usb\class_08&subclass_06&prot_62`, `InfPath` oem1.inf. The model trace shows `set_alt` 1 on both drives (xhciuas). Stick `USB\VID_46F4&PID_0001\UBBOT0001` on `USBSTOR`, Problem code 0, E:. XHCITR lists only the stick in p1 (below) |
| p2 (a) | `XhciStreamsOff` 2 | **PASS.** New `UBSER0001&BOT` and `4&38c5c8b0&0&4&BOT`, `USBSTOR` (class keys 0006 and 0005, `prot_50`, usbstor.inf), installed with no prompt. XHCITR: Instance ID `USB\VID_0781&PID_55A9\UBSER0001&BOT` and `...\4&38C5C8B0&0&4&BOT`, Driver USBSTOR, Problem code 0, "Transport: Bulk-Only (the device offers UAS too; this controller cannot stream at SuperSpeed)"; inputs 0x9, why 5. Volumes E: F: G:. No Code 10. The plain keys keep `xhciuas` (0003/0004), not present. Trace: `set_alt` 0 and 55 CBWs per drive from Windows |
| p3 (b) | `XhciStreamsOff` 0 | **PASS.** Back on the plain keys (`xhciuas`, the same instance paths as p1); the `&BOT` keys kept, not present; no new key. Trace: `set_alt` 1 on both. Volume E: (the stick) only |
| (c) | every phase | **PASS.** The stick has one key, `UBBOT0001`, `USBSTOR`, Problem code 0, E:, throughout |
| (d) | every phase | **PASS.** Model B: plain `4&38c5c8b0&0&4` in p1 and p3 (`xhciuas`), `4&38c5c8b0&0&4&BOT` in p2 (`USBSTOR`, Code 0) |
| p5 (e) | `XhciStreamsOff` 2, `XhciBulkOnlyOwnId` 0 (machine-wide) | **PASS.** Today's single entry. Both drives answer their plain ids (`USB\VID_0781&PID_55A9\UBSER0001`, `...\4&38C5C8B0&0&4`), Driver `xhciuas`, **Problem code 10**. XHCITR: "XhciBulkOnlyOwnId (advanced): 0 - no: one entry everywhere (before 3.4.2.0) (machine-wide)", inputs 0x1, Transport Bulk-Only why 5. The note shown is the older wording, as on the other systems: it follows the version bump. The `&BOT` keys kept, not present. Only E: mounted |

Registry readings: `Services\USB` after each phase's change: after p1
`XhciStreamsOff`=2; after p2 =0; after p3 =2 and `XhciBulkOnlyOwnId`=0;
after p5 `XhciStreamsOff`=0 with `XhciBulkOnlyOwnId` removed. The
controller's driver key ("xHCI98 USB 3 Controller (Red Hat QEMU xHCI)",
oem0.inf): `XhciBulkOnlyOwnId`=dword:1 (written by the INF) in every
phase, alongside the INF's `XhciStreamsOff`=1; the machine-wide value
overrode it in p5, as XHCITR's "(machine-wide)" says.

**Round 1, before `xhciuas` was on the UAS forms** (the wizards dismissed):

| Phase | Configuration | Result |
|---|---|---|
| p1 | `XhciStreamsOff` 0 | `UBSER0001` and `4&38c5c8b0&0&4` present with **no driver**. XHCITR "Transport: UAS (the device offers Bulk-Only and UAS; UAS chosen)" for both (inputs 0xB, transport 2 why 1, alternate 1). Stick `UBBOT0001` `USBSTOR` Code 0, E: |
| p2 (a) | `XhciStreamsOff` 2 | New `UBSER0001&BOT` and `4&38c5c8b0&0&4&BOT` on `USBSTOR`, Code 0, F: G: mounted; XHCITR Instance IDs `...&BOT`, "Bulk-Only (the device offers UAS too; this controller cannot stream at SuperSpeed)" |
| p3 (b) | `XhciStreamsOff` 0 | Back on the UAS forms (driverless, XHCITR "UAS chosen"); `&BOT` keys not present |
| p5 (e) | `XhciStreamsOff` 2, `XhciBulkOnlyOwnId` 0 | The plain keys, having no driver, took `USBSTOR`, Code 0; F: G: mounted: the gate's 0 gives the plain id; the Code 10 case needed round 2 |

Notes:
- **XHCITR in p1 and p3 (round 2):** with `xhciuas` bound, the dual drives
  drop out of XHCITR's report and its storage-transport table (only the
  stick is listed), as on XP's p1: the model accepts the UAS setting but
  does not run UAS. Round 1's p1 and p3 show the "UAS chosen" line; the
  deciding readings are the Enum keys and the model's `set_alt` 1.
- **p5's alternate:** the trace shows `set_alt` 1 on both drives in p5
  too, though XHCITR reports the Bulk-Only choice (why 5) for that devnode:
  `xhciuas` asks for its alternate and then fails with Code 10, as
  expected.
- `xhciuas` was installed only through the wizard on 2000.

### Notes on the vehicle

- **The 7 dialog:** the unsigned-driver dialog never takes the keyboard
  focus. It was answered with Win+T, End, Enter, then Alt+I. With Alt+I
  alone the guest only looks frozen.
- **The XP logon:** XP's "Software Installation" prompt comes back despite
  the driver-signing policy set to Ignore. Answer it with Continue Anyway.
- **Esc:** Esc must never be sent while an unsigned-driver prompt can be up,
  because it stops the installation.

### Merge

Merged into `3.4.2.0` at `1fc5e545` (2026-11-08), with no conflict. Gated x86 and amd64 release
builds of the merge: BUILD + GATES PASSED, host tests passed, INF gate
self-tests 1758 checks.
