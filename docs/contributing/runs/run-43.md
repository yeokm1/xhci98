# Run 43 - Release `3.2.0.0`

Phase 43 of `roadmap-usb4.md`, for release `3.2.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

## 43.13 the idle poll period, 50 against 500 ms (2026-10-22)

The table `runs/run-38.md` "38.3" left as T8 (deferred by the owner on
2026-10-14, the default staying 50; read then on 2000, XP, XP x64 and
Vista only), completed on 98 SE, ME, 7 x86 and 7 x64 under mode 2.
Runtime, development host A, in three sessions on 2026-10-22, the last
ending at 11:05.

**Verdict: done, the default stays 50** (owner, 2026-10-22). On none of
the four systems did 500 give a gain that could be measured: the idle
processor load, the idle poll rate, plug-to-working and the item-5
counts are the same within noise, with the guest clock checked at about
1.0 in every window. The one counter that moves with the value is
`TmrObsReleasedCount` in the idle window on 7 (about 260 a minute at 50,
about 2018 at 500). `src/xhci98.inf` (both `XhciPollIdleMs` rows) and
`src/xhci98-amd64.inf` already write 50; nothing changes in the tree.

### The vehicle

- QEMU under TCG, headless, no display client. 98 SE and ME 1 vCPU each,
  7 x86 and 7 x64 2 vCPUs each; other agents' guests ran on the host at the
  same time (1 to 16 vCPUs in all), so the idle load is compared only
  within one boot or between boots close in time, never across sessions.
- The guests, each on an overlay of its own over a read-only copy: 98 SE
  with NUSB 3.3 and `3.1.0.0` over a pristine 98 SE image, stock ME with
  `3.1.0.0`,
  7 x86 and 7 x64 from Phase 38's matrix (the `2.4.0.0` dry cut's `f`
  overlays). The kit, its scripts and every snapshot are in
  `E:\xhci98-vm\p43-13` (not in the tree; `RUN-NOTES.md` there).
- The driver: the gated `release` binaries of `dd46948c` (`XHCI98.SYS`
  `0546d544...`, `XHCIHLPR.SYS` `0ae5f606...`, x64 `XHCI98.SYS`
  `5f578d8a...`, the hashes of `out\gated-*-release.txt`) copied over an
  earlier install, not a fresh install: the INF and the registry are the
  earlier install's (`3.1.0.0` on 98 SE and ME). The 98 SE copy was
  checked by its SHA-256 read back from the disk; the NT copies were not
  hashed again (the x64 file read 345,600 bytes, the date of the build).
- The values, under the controller's driver key (`Class\USB\0000` on 9x,
  `Class\{36FC9E60-...}\0000` on 7): `XhciInterruptMode` 2,
  `XhciPollFineTimer` 0, `XhciLogVerbosity` 2, `XhciPollIdleMs` per row.
  On 9x a cold boot after each change; on 7 the value written and the
  controller restarted with `pnpctl`. Every leg's first snapshot read
  `IntrMode` 2, `IntrDelivery` 2 and `PollIdleMsRead` equal to the row's
  value.
- Attached through every leg: a mouse at root port 1, a stick at root
  port 2 (the snapshots' copy target), a USB 2.0 hub at root port 4.

### What each column is

- **Idle load**: the QEMU process's host processor time over 60 s, as a
  percentage of one host processor, the guest at its desktop with nothing
  done (on 9x a DOS prompt open, on 7 an elevated `cmd`); four minutes, or
  as noted.
- **Plug**: plug-to-working of a second mouse, at root port 3 and at the
  hub's port 1 (4.1): `device_add`, then a pointer move of 4 pixels each
  way and a screen capture for every frame; the time to the first of two
  consecutive frames changed only inside a 64x64 box round the pointer.
  Only re-plugs of a known devnode count: a first plug installs a driver
  (98 SE's Add New Hardware Wizard for every new HID devnode, ME's silent
  install of 24 to 26 s, 7's driver install of up to 3.6 s). On ME the
  plugs were made with the DOS prompt closed and 3 s between plugs: with
  no gap a plug picks up the previous one's queued moves (times under
  0.1 s, which cannot be).
- **Item 5**: a snapshot, 60 s of mouse movement with two 4 MB copies to
  the stick at +10 and +32 s, a snapshot; the counters' deltas per host
  minute (`PollRuns`, `PollRunsWithEvents`, promotions). Kicks were 0 in
  every leg.
- **Idle window**: a snapshot, 60 s with no input, a snapshot; `PollRuns`
  and peeks per minute. Events were 0 on 9x in every window.
- **Clock**: the snapshot's guest "local time" against the host time at
  which the snapshot was typed, over each window.

### Results (`dd46948c`, mode 2, `XhciPollFineTimer` 0)

| System, value | Idle load (%) | Plug, root port 3 (s) | Plug, hub 4.1 (s) | Item 5 per minute: runs, with events, promotions | Idle window per minute: runs, peeks | Clock |
|---|---|---|---|---|---|---|
| 98 SE, 50 | 20.7, 10.2, 14.9, 20.5 (boot c); 24.3, 24.1, 24.5, 24.3 (boot f) | 0.22, 0.21, 0.20 | 0.25, 0.25, 0.22 | 14916, 894, 865 | 5851, 703 | 0.99 to 1.02; 1.000 (f) |
| 98 SE, 500 | 20.3, 20.7, 20.9, 21.8 (boot d) | 0.19, 0.21, 0.17 | 0.21, 0.46, 0.20 | 14656, 888, 861 | 5491, 700 | 0.99 to 1.02 |
| ME, 50 | 13.2, 13.7, 14.7, 13.0 (boot g) | 0.50, 0.45, 0.45, 0.33 | 0.49, 0.43, 0.51, 0.50 | 13481, 881, 857 | 6048, 706 | 0.99 to 1.000 |
| ME, 500 | 13.7, 13.9, 14.3, 15.0 (boot f) | 0.52, 0.48, 0.53, 0.52 | 0.50, 0.46, 0.33, 0.53 | 14723, 881, 864 | 6105, 710 | 0.98 to 1.005 |
| 7 x86, 50 | 12.9 (settling after the boot), 4.8, 5.4, 5.3 | 0.21, 0.24, 0.22, 0.23 | 0.24, 0.25, 0.24, 0.25 | 3841, 992, 901 | 3882, 1032 | 0.996 to 1.004 |
| 7 x86, 500 | 6.0, 4.8, 4.9, 5.5 | 0.24, 0.24, 0.21, 0.25 | 0.24, 0.26, 0.26, 0.26 | 3851, 987, 893 | 3856, 1027 | 0.996 to 1.004 |
| 7 x64, 50 | 4.9, 4.4, 3.6, 4.9 | 0.22, 0.22, 0.20, 0.21 | 0.23, 0.26, 0.24, 0.25 | 3851, 996, 898 | 3817, 1018 | 0.996 to 1.004 |
| 7 x64, 500 | 4.6, 4.3, 4.3, 4.3 | 0.21, 0.22, 0.22, 0.21 | 0.25, 0.32, 0.25, 0.24 | 3851, 988, 894 | 3851, 1023 | 0.996 to 1.004 |

The 98 SE pair is boots c (50) and d (500), close in time, with boot f's
50 idle window and load taken later under a heavier host (the load of
24 % is the host's, not the value's: boot c read 10 to 21 % at the same
value). ME's pair is boots g (50) and f (500), back to back under the
same host load. ME's first 50 boot read the same (load 17.5 to 22.9 %,
plugs 0.42 to 0.59 s, item 5 14331 runs, idle window 5952 runs and 707
peeks). On 7 each guest read 50, 500 and 50 again in one boot (the
values switched by `pnpctl`); the 50 rows are the second pass, whose
plugs met known devnodes (the first pass installed the drivers; its load
was 15.2, 4.6, 12.5, 3.5 % on x64, bursts after the boot).

**The timer's holds and releases.** With `XhciPollFineTimer` 0 there is
nothing to hold: `TmrHolds` and `TmrReleases` were absent from every 7
snapshot, and 9x has no timer helper (absent by design). The phase
observation's released-state count (`TmrObsReleasedCount`, design record
15 section 7.2.9: consecutive poll runs at the same short arm, the timer
released) is the one counter that moved:

| | Idle window per minute, 50 | 500 | Item 5 per minute, 50 | 500 |
|---|---|---|---|---|
| 7 x86 | 262 | 2018 | 3004 | 3451 |
| 7 x64 | 259 | 2018 | 3003 | 3442 |

Its meaning was not read further. A likely reading, not proven: the idle
period is also the window within which a drained event still counts as
recent (`XhciPollRecent` in `hcd_poll.c`), so at 500 the poller stays at
its short arm for longer after each mouse or hub event; the poll rate
does not change, since every arm waits for the clock tick (the 7 guests
poll about 64 times a second at both values, the 15.6 ms tick;
`PollIntervalMinMs` 15 at both). `PollIntervalMaxMs` read 62 at 50 and
15 at 500 on 7 x86, the 500 reading from the controller's restart on.

**Reading.** The poller is not paced by `XhciPollIdleMs` with a mouse, a
stick and a hub attached: 9x polls about 95 to 100 times a second
(about 11.7 peeks a second) and 7 at its tick at both values, so the idle
load cannot fall. The earlier partial reading of 2026-10-14 (`run-38.md`
"38.3", T8, `results.md` section 5) differs on Vista alone, which 43.13
does not cover: the idle load halved at 500 on Vista x86 (12.7 to 5.4 %)
and about 30 % lower on Vista x64, with no difference on XP, XP x64 and
2000.

### Not run

USB 2.0 remote wake on 98 SE and ME (owner, 2026-10-22: not needed
before the tick, since the default is unchanged). The test client
(`pmclient-mouse`) has never been installed on 9x. Remote wake was read
at both values on the x86 NT guests of 2026-10-14 (`run-38.md` "38.3",
T8); 7 x86's own wake legs (`PMI`, `PMW`, `PMS`) were not taken either,
for the same reason, and x64 has no test client.

### The ME guest's clock and the ME hang

- **The slow clock, 08:00.** ME's first boot at 500 (boot d, 07:59 to
  08:09, with the host nearly idle) read the guest clock at 0.645 of host
  time during item 5 and 0.317 during the idle window; its load (3.6 to
  7.7 %), its lower counts and its plugs of 0.56 to 0.80 s followed the
  slow guest (per guest second the idle poll rate was the same, about 88).
  Retaken at 500 (boot f) and at 50 (boot g) back to back, with the clock
  read in every window: 0.98 to 1.005, and the rows above. The slow clock
  never came back. Its cause was not read; it is most likely the hang
  below in a partial form (the system VM blocked for stretches), not
  proven.
- **The hang.** On ME a run of mouse plugs and unplugs with no gap between
  them hung the guest: the tray clock stopped, typed text never echoed,
  and every instruction-pointer sample sat in the VMM's idle loop
  (`c00138c0`, `sti`/`nop`/`nop`/`cli`, nothing to schedule) with an
  occasional V86 sample; no recovery in 3 minutes, the guest quit from
  QMP. Seen at 500 (boot e, 08:54, its first plug series) and reproduced
  at 50 (boot g, 09:59:46, after 24 good plugs, at the hub's port 1), so
  it is not the idle period's. Not tried on 98 SE. It is task 43.49; the
  register samples are in the kit (`legs\me\e\hang-regs.txt`,
  `legs\me\g\hang-regs.txt`).

### What it decides

`XhciPollIdleMs` stays 50 by default on every target (owner,
2026-10-22): 500 gave no gain that could be measured on any system read,
and only `TmrObsReleasedCount` differed. The INFs are unchanged.

## 43.14 read first: whether a VID/PID INF binds a refused interface (2026-10-22)

The reading task 43.15 waited on (was NOX.1's reading; design record 13
section 10.7.1, whose plan of 2026-10-13 it follows except where said).
A storage interface with no transport the bus can run - a UAS-only device
at SuperSpeed on a controller that does not stream - is refused: its one
hardware id is `USB\XHCI98_NOXPORT&VID_v&PID_p&REV_r`, and it has no
compatible id, but until 43.15 its device id kept the `USB\VID_v&PID_p`
form, which task 38.2 found in the 98 SE devnode's `HardwareID` value.
The question: does an INF whose only line is that VID/PID form bind it?
Runtime, development host A.

### The vehicle

- QEMU 11.1.0 under TCG, headless; each guest 1 vCPU (`pentium3,-apic`),
  256 MB, `-action reboot=shutdown`. The controller
  `qemu-xhci,p2=1,p3=4,streams=off`: port 3 is a SuperSpeed port with no
  USB 2.0 companion, and the controller reports no streams.
- The device: `usb-uas` with a `scsi-hd` (a 128 MiB FAT16 image),
  hot-plugged on port 3 after the desktop, `attached` set, each plug with a
  serial never seen before (`NOXW98V1`, ...). VID/PID `46F4:0003`, REV
  `0000`. A `usb-tablet` on port 1 for the pointer.
- The driver: the published `3.1.0.0` x86 package
  (`releases\3.1.0.0`), installed from its INF through the controller's
  Found New Hardware wizard. Every release since `2.0.0.0` answers a refused
  interface's ids the same way, so no build of this branch was needed.
- Three guests, each on its own overlay:
  - **98 SE**: installed fresh from the 98 SE CD on 2026-10-20 for task
    43.5 (no NUSB, nothing added), then `3.1.0.0`.
  - **98 SE + NUSB 3.3**: the same, then NUSB 3.3 English (`nusb33e.exe`,
    its agreement accepted in the guest, owner) and a restart.
  - **ME**: stock Windows ME (`vm\t37me\winme-stock-base.qcow2`), then
    `3.1.0.0`.
- The probes, this project's own INFs written for the reading, never in the
  tree: `Class=USB`, an install section that only writes one value
  (`NoxProbe`) under the driver key, and `.NT` sections with
  `AddService=,2`. Probe V's one line is `USB\VID_46F4&PID_0003`; probe
  C's is `USB\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000`.
- "Binds": the probe's line is chosen for the devnode - its `DeviceDesc`,
  `Mfg`, `Driver` key and Device Manager name come from the probe.

### Results

| Guest | First plug, probe V offered at the wizard | Second plug, V already installed, index deleted |
|---|---|---|
| 98 SE | Wizard for "USB Attached SCSI HBA"; at `D:\PV` it named "NOX probe V device id", `D:\PV\PROBEV.INF`, and installed it. Device Manager: "NOX probe V device id" under Universal Serial Bus controllers | No wizard, no prompt: bound to probe V |
| 98 SE + NUSB 3.3 | The same | The same; "Updating hardware information database" shown at the plug, then bound |
| ME | The same (ME's wizard: "Specify the location", the folder typed) | The same as NUSB's |

Every one of the six devnodes read the same (`regedit /e` of
`HKLM\Enum\USB\VID_46F4&PID_0003` and of `...\Services\Class\USB`, taken
off the disk after a clean shut down), for example 98 SE's second:

```
[HKEY_LOCAL_MACHINE\Enum\USB\VID_46F4&PID_0003\NOXW98V2]
"HardwareID"="USB\\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000,USB\\VID_46F4&PID_0003"
"DeviceDesc"="NOX probe V device id"
"Class"="USB"
"Driver"="USB\\0003"
"Mfg"="NOX probe"

[HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\USB\0003]
"NoxProbe"="V"
"InfPath"="NOXPRO~1.INF"
"InfSection"="Probe.Dev"
"MatchingDeviceId"="USB\\VID_46F4&PID_0003"
```

`MatchingDeviceId` is the device id, the second entry of the `HardwareID`
value; the PDO answers only the first as a hardware id. So Windows 98 SE,
with and without NUSB 3.3, and ME match INF lines against the device id the
configuration manager adds to that value, and an INF whose only line is a
refused device's `USB\VID_v&PID_p` binds it - silently, with no wizard,
once that INF is installed: the wizard had copied the probe into
`INF\OTHER` (`NOXPRO~1.INF`, "NOX probePROBEV.INF"), and with
`DRVIDX.BIN` and `DRVDATA.BIN` deleted before the second plug, the index
was rebuilt and the new device bound from it.

For the second plug the guests were shut down and relaunched cold (98 SE
excepted, which took it in the same session).

### Not read

- Probe C, the control: not offered. V bound, which answers the question C
  was there to qualify (C's line is the id the PDO answers as its hardware
  id).
- Windows 2000 SP4 and NT's matching in general (documented as hardware and
  compatible ids only): not read. Route 2 of the plan (a serial-number
  device's devnode, installed in its working form, reloading its driver in
  the refused form): not read. Neither can bind a refused device after
  43.15, whose device id is project-owned and differs from the working
  form's.
- The NUSB 3.3 `USBSTOR.INF`'s own VID/PID lines: none names `46F4:0003`,
  so they were not exercised; a listed device would match the same way.

### What it decides

By record 13 section 10.7.1's decision 1, the refused PDO's device id
moves to `USB\XHCI98_NOXPORT&VID_v&PID_p[&MI_nn]` on every target: task
43.15.

## 43.16 the legs: a refused interface under the new device id, 98 SE, ME and 2000 (2026-10-22)

NOX.1's legs (design record 13 section 10.7.1) for task 43.15's change,
on the three guests 43.14 read, each continued from the disk 43.14 left:
probe V (`USB\VID_46F4&PID_0003`) installed in `INF\OTHER` and bound to
the two refused devnodes 43.14 made; and on Windows 2000 (below).
Runtime, development host A.

### The vehicle

- As 43.14's: QEMU 11.1.0 under TCG, headless, 1 vCPU, 256 MB,
  `qemu-xhci,p2=1,p3=4,streams=off`, a `usb-uas` with a `scsi-hd` (a fresh
  overlay of the 128 MiB FAT16 image) hot-plugged on port 3 after the
  desktop, `attached` set, a serial never seen before (`NOXW98N1`,
  `NOXN98N1`, `NOXMEN1`).
- The driver: this branch's x86 release `xhci98.sys` (the gated build of
  `66840a9e`, 300,271 bytes, SHA-256
  `81E163EC145B5A99996E5E2F0456F6083A6A4247AFB0B3E786A4C309FF4E005D`),
  copied over `3.1.0.0`'s in `C:\WINDOWS\SYSTEM32\DRIVERS` from an MS-DOS
  Prompt; the INF and the rest of `3.1.0.0` unchanged. The guest shut
  down, the copy read back off the disk with the same SHA-256, then a cold
  boot.
- Guests: 98 SE, 98 SE + NUSB 3.3, ME, as in 43.14.

### Results

| Guest | The plug | The wizard pointed at `D:\PV` (probe V) | The new devnode |
|---|---|---|---|
| 98 SE | Add New Hardware Wizard for "USB Attached SCSI HBA" | "Windows was unable to locate a driver for this device"; Finish: "Unknown Device", no driver installed | `Enum\USB\XHCI98_NOXPORT&VID_46F4&PID_0003\NOXW98N1` |
| 98 SE + NUSB 3.3 | The same | The same | `...\NOXN98N1` |
| ME | The same (ME's wizard) | "Windows was unable to locate the software for this device"; Finish | `...\NOXMEN1` |

Every new devnode read the same (`regedit /e` of `HKLM\Enum\USB` and of
`...\Services\Class\USB`, taken off the disk after a clean shut down), for
example 98 SE's:

```
[HKEY_LOCAL_MACHINE\Enum\USB\XHCI98_NOXPORT&VID_46F4&PID_0003\NOXW98N1]
"HardwareID"="USB\\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000,USB\\XHCI98_NOXPORT&VID_46F4&PID_0003"
"DeviceDesc"="USB Attached SCSI HBA"
"Class"="Unknown"
"ConfigFlags"=hex:40,00,00,00
```

No `Driver` value and no new `Class\USB` key: probe V, already installed
and offered again at the wizard, did not bind, on any of the three. The
device id is the second `HardwareID` entry, as 43.14 read for the old form.
The old devnodes, `Enum\USB\VID_46F4&PID_0003\<serial>` (43.14's two per
guest), were left as they were - the same `HardwareID`, `Driver`
(`USB\0002`, `USB\0003`) and probe V's `Class\USB` keys as 43.14 read -
by decision 4; the device did not return to them.

### Windows 2000

The plan's NT target, on a fresh overlay of the project's clean Windows
2000 image (`vm\t26\win2k-clean.qcow2`), the same controller and
device:

1. `3.1.0.0` installed through the controller's Found New Hardware
   wizard from the package's INF, then a restart.
2. A refused `usb-uas` plugged under `3.1.0.0` (serial `NOXW2KO1`), and
   the wizard pointed at probe V's folder: "Windows was unable to locate a
   driver for this device"; "Skip driver installation" chosen.
3. The gated `xhci98.sys` of `66840a9e` copied over `3.1.0.0`'s in
   `C:\WINNT\system32\drivers`, the guest shut down, the copy read back off
   the disk with the same SHA-256, a cold boot.
4. The same device plugged again (the same serial): a new wizard, probe V
   offered again, "unable to locate a driver", skipped.

`regedit /e` of `HKLM\SYSTEM\CurrentControlSet\Enum\USB` and of the USB
class key, off the disk after a clean shut down:

- Step 2's devnode, `Enum\USB\VID_46F4&PID_0003\NOXW2KO1`, its
  `HardwareID` the one id the PDO answered,
  `USB\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000`: 2000 does not add the
  device id to that value as 98 SE and ME do, and probe V's
  `USB\VID_46F4&PID_0003` line did not bind the old form either.
- Step 4's devnode, `Enum\USB\XHCI98_NOXPORT&VID_46F4&PID_0003\NOXW2KO1`,
  its `HardwareID` the two NOXPORT ids, no `Driver`; the old devnode left
  beside it, not present (no `Control` key), by decision 4.
- No USB class key past the controller's and the root hub's.

### Not read

- A refused function of a composite device (`&MI_nn`): host vectors only.

## 43.45 - The `amd` host model: 7, 2000 and 98 SE

**Verdict: done** (2026-10-22, ticked on the owner's approval). Every leg
passes on 7, 2000 and 98 SE except the resume, which passes on 7 only.

- The 2000 resume is owed. The ACPI-on 2000 image did not finish: GUI-mode
  Setup looped once the product key was in (below).
- The 98 SE resume is not readable anywhere. On QEMU, the connection
  manager's half passes, and then Windows 98 SE's own post-standby fault
  (36.28 part 2) stops the guest from reading the disk. The owner's real
  machine cannot put 98 SE in Stand by either. So the release notes list
  Stand by with a USB4 or Thunderbolt disk on 98 SE as untested ground.

The owner approved the tick on 2026-10-22 with these two legs recorded
this way. Two findings in XHCITR,
neither in the connection manager: its eject box on 7 waits too short a
time when XHCITR is not elevated, and it warns "controller stopped
answering" across a hibernate. Both are described below; the code was not
changed.

**What `amd` is.** usb4-emu's `minimal` router with AMD's router identity:
vendor `0438`, device `0208` to `020B` by `host-domain`. The host interface
is still the emulator's `1B36:00F4`. So these legs read the connection
manager's vendor checks and quirk table against an AMD router identity.
They do not read AMD hardware, and they do not read the INF's match on
`1022:1668` and `1022:1669`.

**Build and package.** Branch `p43-45` at `1a85f36e`. The gated x86 `all`
build passed (`test_u4cm` 1154 checks). Release `xhcicm.sys` `E827BD54...`,
`xhci98.sys` `15E22D38...`, `stor98.sys` `571AE627...`; the `qemu`-flavour
`xhcicm.sys` `C191766A...`, which writes its log to the debug console. A
TEST cut of the x86 release package (`-TestLabel "p43-45 amd legs"`,
version 3.1.0.0) was installed with `INSTALL /console /yes /thunderbolt
/stor98` on the NT guests and with `XHCIINST` on 98 SE.

**The host and the emulator.** Development host A, QEMU TCG, 2 vCPUs per
guest on 7, 1 on 98 SE and 2000. The emulator was a copy of `vm\usb4\bin-2a893ea`.
Topology: `usb4-host` at 00:04 (`profile=usb4-generic`, `host-model=amd`,
`host-domain` 0 or 3), the SHPC tunnel bridge `tb0` at 00:05 (USB4 port 1),
and `enc0`, the ASM2464 device router (`device-model=asm2464`) with an NVMe
namespace behind it. Kit `E:\xhci98-vm\p43-45` (`RUN-NOTES.md`, shots,
debug-console logs and `usb4_*` traces per leg).

**The data check.** `CHK.BAT` on each leg: `fc /b` of the unit's
`TESTDATA.BIN` against the host's copy, then a 1 MiB `WRITE.BIN` copied to
the unit as `Wn.BIN` and compared back. Every comparison below said "no
differences encountered".

### Windows 7 x86, `host-domain` 0 (`qemu` flavour)

| Step | Reading |
|---|---|
| Identity | Debug console `u4.host.id=04380208`, `u4.quirk=02080002` (the `quirk_clx_disable` row for vendor `0438` matched), `cm.profile=5`, `cm.select.outcome=4` |
| Plug | `u4.router.up=174C2463`, `u4.pcie.up=00010809`, `cm.nvlink.up=01000101`. F: (`ORICO`) mounted under Stor98; W1 compared clean. `INSTALL /devices`: "ASMedia 246x, USB4 (version 1.0), port 1, depth 1, approved, router 174C:2463, PCIe up" |
| XHCITR | "ASMedia 246x (USB4 v1.0), approved"; Generation USB4 v1.0; Link USB4 Gen 3 x2. The `qemu` flavour leaves XHCITR's driver-log pane empty (its log goes to the debug console), so the host id was read on domain 3's release flavour |
| Eject (XHCITR, twice) | The device stopped, `u4.eject=00000100`, trace "PCIe tunnel down reason 1 (admin-down)". The tree afterwards: "PCIe tunnel: closed after an eject". XHCITR's box said "The Thunderbolt driver has not confirmed that the PCIe tunnel was closed" both times (finding 1) |
| Unplug and replug | Generation 2, F: back, W2 compared clean |
| Surprise unplug and replug | `u4.linkdown=01000209`, trace reason 3 (surprise); replug: W2 intact, W3 compared clean |
| Resume | 7 x86 offers hibernate only here. `shutdown /h` with `enc0` attached: `cm.suspend=5`, `u4.suspend.obligations=1`. Relaunched with `enc0` at power-on. The restore showed a black text screen for some minutes under TCG, then the desktop. Debug console: `cm.resume=1`, the host id and quirk read again, `u4.router.up=174C2463`, `u4.pcie.up=00010809`, `cm.reenum.resumed=1`. F: back with W1 to W3; W4 compared clean; Stor98 running; `INSTALL /devices` "approved, PCIe up". XHCITR showed the balloon "Thunderbolt/USB4 warning: The Thunderbolt/USB4 controller stopped answering" after the logon (finding 2) |
| After the resume | Eject from `XHCIINST /console /eject 1` in an elevated prompt: "Windows has stopped Stor98 Storage Controller for NVMe SSDs. The PCIe tunnel was closed." `cm.reenum.confirm` 2.2 s and `u4.eject` 2.4 s after the answer. Replug: W5 compared clean. A second XHCITR eject: the same box as above, `cm.reenum.confirm` 13.8 s after OK |
| Shutdown | `cm.shutdown=2`; `cm.reenum.ups=5`, `downs=5`, `ejects=4`, `confirms=4`, `confirms.refused=0`, `poll-timeouts=0`, `abandoned=0` |

### Windows 7 x86, `host-domain` 3 (release flavour)

The release `xhcicm.sys` copied over the `qemu` one, `enc0` attached at
power-on.

| Step | Reading |
|---|---|
| Identity | XHCITR's driver-log pane (xhcicm): `u4.host.id=0438020B`, `u4.quirk=020B0002`, `cm.select.outcome=4`, `tbt.hw.software-cm=1` |
| Unit | XHCITR "ASMedia 246x (USB4 v1.0), approved", "PCIe tunnel: up - QEMU NVMe Ctrl", the disk drive below it; W6 compared clean |
| Surprise unplug and replug | Trace reason 3, then "PCIe tunnel up", insert generation 2; W7 compared clean. XHCITR's counters: `u4.connects` 2, `u4.pcie.built` 2, `u4.pcie.torn-down` 1 |

### Windows 2000 (`win2k-xonly`, `acpi=off`), `host-domain` 0 (`qemu` flavour)

| Step | Reading |
|---|---|
| Install | `INSTALL` exit 3, restart. The plug raised Found New Hardware for "Stor98 Storage Controller for NVMe SSDs" (unsigned: Yes) |
| Identity | `u4.host.id=04380208`, `u4.quirk=02080002` |
| Plug | `u4.router.up=174C2463`, `u4.pcie.up=00010809`; F: mounted, W1 compared clean |
| Eject (XHCITR) | "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (F:). The eject was confirmed to the Thunderbolt driver. The PCIe tunnel was closed." `cm.reenum.confirm` 2.0 s and `u4.eject` 2.2 s after OK. The tree: "PCIe tunnel: closed after an eject" |
| Unplug and replug | F: back, W2 compared clean |
| Surprise unplug and replug | `u4.linkdown=0100020B`; replug: W3 compared clean |
| Resume | **Owed.** This image runs `acpi=off` and cannot sleep. The owner's decision of 2026-10-22 was to build an ACPI-on 2000 image for this leg, and to accept the leg as owed if that image had problems. It did. The unattended install (`win2ksp4.ISO`, `-machine pc`, ACPI on, `E:\xhci98-vm\p43-45\base\win2k-acpi.qcow2`) ran text mode and stopped in GUI mode at "Your Product Key". The owner then supplied the key, which is kept in the kit folder only. Setup was resumed from the disk and the key typed over QMP. Setup accepted it and installed the networking components, then went back to "Installing Devices". It ran that stage again and stopped once more with "Unattended Setup is unable to continue because a Setup parameter ... is missing or invalid": the same sequence of screens as before the key, so one clear loop. This is the known loop of 2000's GUI-mode Setup under QEMU 11.1 with ACPI on under TCG, control installs included. The install was stopped there and the leg recorded as owed, per the owner's decision |

### Windows 98 SE, `host-domain` 0 (`qemu` flavour)

The `w98-300` overlay with the tunnel bridge installed from `MACHINE2.INF`
and the NHI's own wizard cancelled; `XHCIINST` installed the package, then
a restart.

| Step | Reading |
|---|---|
| Identity | `u4.host.id=04380208`, `u4.quirk=02080002`, `cm.select.outcome=4`, `cm.reenum.9x.version=0000040B` |
| Plug | `u4.router.up=174C2463`, `u4.pcie.up=00010809`, `cm.reenum.9x.reenumerate=00002800`, `cm.nvlink.up=01000101`; F: (`ORICO`) in My Computer; W1 compared clean |
| Eject (XHCITR) | "Windows has stopped QEMU NVMe Ctrl (F:). Until you unplug it, Device Manager's Refresh, or the Thunderbolt driver looking at the port again, can start it again." Unplug: `u4.linkdown=0100010B` |
| Replug | F: back, W2 compared clean. `cm.reenum.9x.noirq.refused=01080203`: the no-IRQ remedy excluded the NVMe function at 01:01.0 for its header (it had an interrupt line), so the remedy was not needed |
| Surprise unplug and replug | `u4.linkdown=0100020B`, `cm.reenum.9x.query-remove=0`, `cm.reenum.9x.remove=0`; replug: W3 compared clean |
| Resume (Stand by) | `cm.suspend=4`, `u4.suspend.obligations=1`; QEMU `suspended`; `system_wakeup`. Connection manager: `cm.resume=1`, the host id and quirk read again, `u4.router.up=174C2463`, `u4.pcie.up=00010809`, `cm.reenum.resumed=1`, `cm.brnum.9x.held=00002801` (the bridge still had its child, so it was not numbered again), `cm.reenum.9x.reenumerate=00002800`, and seven `cm.reenum.9x.removable=0`. The trace shows the tunnel up again (insert generation 4). Then the first new program (an MS-DOS prompt from Run) stopped with "fatal exception 0E at 0187:BFF9DFFF", again on every key. This is run-36's open 36.28 part 2 defect: KERNEL32 at `BFF9DFFF` after any 98 SE standby on QEMU, with or without a USB controller in the guest |
| Resume, again | A fresh boot, `enc0` hot-plugged at the desktop, F:\ and XHCITR opened before Stand by so that no new program would be needed after the wake. After `system_wakeup` the connection manager read the same as above. Windows then raised Add New Hardware for "PCI standard PCI-to-PCI bridge" and for "Intel 82441FX Pentium(R) Pro Processor to PCI bridge" (the emulated host bridge), and closed the F:\ windows. Closing the first wizard brought up the
second, and the guest was quit there. Not attributed: Windows enumerated the system board's own bridge as new after the wake. Not read further |
| Side reading | A guest reset with `enc0` still attached, so the NVMe was present at POST: a Stand by there never resumed. QEMU went back to `running`, the screen stayed on the Shut Down dialog, and no `cm.resume` came in 4 minutes. That matches run-36's 36.28 part 2 finding that a disk attached at power-on stops a 98 SE resume. Not this task's scenario |

`cm.nvlink.up.no-bus=00000028` after both 98 SE wakes: the bridge's
secondary bus was not readable when the link came up, so no new Stor98
statement was published. On 7 the same moment logged
`cm.nvlink.up.no-channel=00000028`, and the unit worked there.

### Finding 1: XHCITR's eject box on 7, "has not confirmed that the PCIe tunnel was closed"

The cause is not TCG timing alone, and it is not a bridge mismatch. The
cause is that XHCITR does not confirm the eject when it is not elevated.

- `ej_run` (`xhcitr/xhcitr.c`) confirms an eject to the connection manager
  itself (`ej_mark_confirm`, record 21 section 7.4, decision M16) only when
  `ej_is_admin()` is true, and on Vista and 7 that means an elevated token.
  XHCITR was started from the Start menu, so it was not elevated. Its box
  carried no "The eject was confirmed to the Thunderbolt driver" line.
- Without a confirmation the connection manager holds the tunnel. The
  confirmation then comes from the mark service (`xhcimark`), which writes
  `XhciCmEjectConfirm` when its next pass finds the unit held for eject
  (problem 47). Its cadence is 1 s, or 10 s while no NHI answers
  (`mk_poll_ms`), plus a device walk that is slow under TCG.
  `cm.reenum.confirm` was logged 12 to 18 s after OK on 7, and `u4.eject`
  0.2 s after each confirmation.
- `ej_watch_tunnel` gives up 10 s after the eject returned. So it answers
  before the mark service's confirmation arrives.
- Elevated tools and administrators confirm themselves and pass. Elevated
  `XHCIINST` on 7: confirm 2.2 s, "The PCIe tunnel was closed." XHCITR on
  2000 as an administrator: confirm 2.0 s, the same line. The bridge match
  (`CM_ST_E_BRIDGE == q->PortBdf`) is the one these passing cases use, and
  the confirmations name bridge `0x28` in every case.

The fix, for a code task (not made here): when `ej_run` did not confirm
the eject itself (not elevated, or `ej_mark_confirm` returned nothing),
`ej_watch_tunnel` should not use the 10 s bound that assumes a
self-confirmation. Two ways:

- Wait for the mark service's confirmation: about its 10 s idle cadence,
  plus one device walk, plus a margin, so 30 s.
- Or say what is happening: "The Thunderbolt driver closes the PCIe tunnel
  when the eject is confirmed (the xHCI98 mark service does this in the
  background; running XHCITR as administrator confirms it at once)."

The tree already shows the result later ("PCIe tunnel: closed after an
eject"). `XHCIINST`'s watch has the same 10 s shape and would read the same
way unelevated, but it runs elevated.

### Finding 2: XHCITR's "controller stopped answering" balloon after a hibernate

`tr_diff` (`xhcitr/tr_core.c`) raises `TR_EV_STOPPED` when an NHI's
liveness goes from `CM_LIVE_ACTIVE` to any other state. A sleep disarms the
liveness gate (`CmLiveDisarm` sets `CM_LIVE_OFF`, "not monitored"). XHCITR
then sees ACTIVE followed by OFF across the hibernate and warns, although
nothing failed: `cm.resume=1`, the unit came back, and the data compared
clean.

The fix, for a code task: leave `CM_LIVE_OFF` out of the transitions that
raise `TR_EV_STOPPED`. Only a real loss should raise it: suspect, absent,
configuration lost, recovery failed, contained. A `test_tr.c` case should
cover ACTIVE to OFF to ACTIVE.

### What stays owed

- The 2000 resume, on an ACPI-on 2000 guest. One needs either an emulator
  where 2000's GUI-mode Setup does not loop with ACPI on, or real hardware.
  The partial image and its recipe are kept in the kit
  (`E:\xhci98-vm\p43-45\RUN-NOTES.md`).
- The 98 SE resume is not readable on any vehicle held: QEMU (36.28
  part 2), and the owner's machine, which cannot put 98 SE in Stand by.
  It is listed as untested ground.
- The two XHCITR fixes above.

## 43.32 the Windows 10 virtual machines (2026-10-22)

Two Windows 10 22H2 guests, x86 and x64, recorded in `build-and-test.md`,
"Windows 10 target VMs (roadmap-usb4 43.32)". Development host A; the
host loaded throughout (other agents' guests and builds; CPU near 97 % at
13:45).

**Verdict: the two TCG machines are set up; the OVMF guest with Secure
Boot on is not built, so 43.32 stays open.**

### What was done

- **The images** (2026-10-22, 11:13 to 12:40, the Windows 8, 8.1 and 10
  image session): `w10x64-pristine.qcow2` and `w10x86-pristine.qcow2`,
  10.0.19045.2965, Pro, from the owner's `Win10_22H2_English_x64v1.iso`
  and `Win10_22H2_English_x32v1.iso` by an unattended install - WinPE and
  the image copy under WHPX (owner: WHPX for the base-image installs
  only), specialize, OOBE and the first logon under TCG with the legs'
  `-machine pc -cpu qemu64,+3dnowprefetch`. `SkipMachineOOBE` and
  `SkipUserOOBE` were needed: without them 10's user OOBE stalled.
  Test-signing on, autologon, hibernation and Windows Update off, no
  network; a verify boot of each reached the desktop in under 7 minutes.
- **The prepared starting points** (12:55 to 13:38): each image booted
  once on an overlay that became `prep.qcow2` (now read-only): the
  scheduled task `xrun` (run level highest, as `xhci`) that calls
  `<transfer drive>\auto\go.cmd`, and a first reading of each guest. The
  task's logon trigger did not fire at the autologon on either guest; a
  leg starts it with `schtasks /run /tn xrun` from Win+R, which runs it
  elevated with no consent prompt.
- **The machine**: the usb4-emu QEMU (`vm\usb4\bin-2a893ea`), TCG
  `thread=multi`, `-smp 2`, 2048 MB (the host's limit for 8 to 10 guests
  that day), SeaBIOS, `qemu-xhci,p2=8,p3=4` with the tablet, two
  `usb-storage` (a USB 3 port and a USB 2.0-only port), `usb-uas`,
  `usb-audio`, `usb-hub` with `usb-kbd`, and the emulator's Thunderbolt 3
  NHI with its xHCI and tunnel bridge; QMP 48660 (x86) and 48661 (x64).

### The speed under TCG (read first, as the entry asks)

Power-on to the autologon desktop about 4.5 minutes at `-smp 2` on both
guests; a forced driver install 7 s (x64) to under 3 minutes with its
prompt (x86); `msinfo32 /report` about 10 minutes, the longest step of a
reading. **Usable for reading and install legs on TCG**; no WHPX was
needed for any leg, so the owner's choice between a slower leg and WHPX
does not arise for these. Timings are from a loaded host and are noisy.

### Not done

- **The OVMF guest with Secure Boot on** (for 43.36's refusal case).
  The usb4-emu build carries `edk2-x86_64-secure-code.fd` (needs `q35`
  with SMM) but no variable store with Microsoft's keys enrolled, and no
  `EnrollDefaultKeys` application; the keys would have to be enrolled by
  hand in the firmware's Secure Boot configuration (custom mode: a
  platform key of the project's own, Microsoft's KEK and Windows
  Production PCA 2011 certificates from Microsoft's download pages), and
  10 x64 installed fresh under UEFI, since the MBR images do not boot
  under OVMF. Not attempted in this task; an owner question in the
  report.

## 43.33 Windows 10, read first: the guest legs (2026-10-22)

The measured half of 43.33; the static reading and the conclusions are
design record 13 section 6.14 ((a) and (b)), record 21 section 3.6 ((c))
and record 23 section 5.12 ((d) and (e)). Runtime, development host A,
on 43.32's two guests, one at a time, host loaded (other agents' guests
and builds). Nothing of this project's was built: the package is the
published `3.1.0.0` (`releases\3.1.0.0`, `release-x64` and the x86
release directory), copied to the transfer drive's `pkg\`.

### The vehicle

- The guests of 43.32 (`build-and-test.md`, "Windows 10 target VMs"):
  10.0.19045.2965 Pro, legacy BIOS ("BIOS Mode Legacy", "Secure Boot
  State Unsupported", "Kernel DMA Protection Off", "Virtualization-based
  security Not enabled", `msinfo32`), test-signing on, `RtlGetVersion`
  10.0.19045, platform 2, product type 1.
- A leg overlay per guest (`w10x64-x98`, `w10x86-x98`) over the prepared
  starting point, the full device set (`tab,ss3,ss2,uas,aud,hub,nhi`),
  `-smp 2`, 2048 MB.
- The leg's script `go.cmd` (started by `schtasks /run /tn xrun`): a
  reading under Windows' own stack (`ms`); the installer's NT route by
  hand, `UpdateDriverForPlugAndPlayDevicesW` with `INSTALLFLAG_FORCE` on
  the `qemu-xhci` controller's first hardware id with the package's
  `xhci98.inf` (`force.ps1`); a reading (`x98a`); a restart and a
  reading. Each reading (`read2.ps1`): `pnputil /enum-devices /connected
  /drivers` and `/enum-drivers`, the Enum, Class, service and USB keys,
  the setup logs, the System, Kernel-PnP, USB and Code Integrity event
  logs, `bcdedit`, `msinfo32 /report`, the USB scheduled tasks, written
  to the output disk and read on the host with 7-Zip after power-off.

### x64 (13:38 to 14:33)

- `ms`: the devices of record 13 section 6.14.3's table: `qemu-xhci` and
  the emulator's xHCI under `usbxhci.inf` (`00802005`), root hubs and
  the USB 2.0 hub under `usbhub3.inf`, HID under `input.inf`, both
  `usb-storage` under `usbstor.inf`, `usb-audio` composite with
  `wdma_usb.inf` on `MI_00`, **`usb-uas` Code 10 under Windows' own
  `uaspstor.inf`**, the NHI `8086:15BF` Code 28. Explorer's details pane
  (This PC, a drive selected): F: "Connected to USB 3.0", G: no line.
- The forced install, 14:10: **refused**, `0xE000022F`, in 7 s, no
  prompt; the setup log: "Driver package does not contain a catalog
  file, and Code Integrity is in Test Signing mode", "Failed to import
  driver package into Driver Store". The controller stayed on
  `usbxhci.sys`; the guest was powered off with no restart leg.

### x86 (14:34 to 16:12)

- `ms` (14:47 to 15:05): the same table as x64's, `usb-uas` Code 10 and
  the NHI Code 28 included. (A second instance of the leg's script,
  started twice by the previous operator, took a second reading under
  Windows' own stack from 15:05 to 15:09, filed as `x98b`; it is a
  duplicate of `ms`, not a post-restart reading.)
- The forced install, 15:16 to 15:22: "Windows can't verify the
  publisher of this driver software" (Windows Security), "Install this
  driver software anyway" clicked; `UpdateDriverForPlugAndPlayDevicesW`
  returned TRUE, reboot required, 349 s with the prompt. The setup log:
  "Driver package does not contain a catalog file, but user wants to
  install anyway", "Signer Score = 0x80000000 (Unsigned)", the package
  imported as `oem2.inf`, `xhci98.sys` and `xhcihlpr.sys` copied, the
  services `xhci98` and `xhcihlpr` added, `DelService=xhcimsi`, and
  "Device required reboot: 'Reboot' in section: [Xhci.Dev6.NTx86]".
- `x98a` (15:24 to 15:29, before the restart): the controller's devnode
  "xHCI98 USB 3.x eXtensible Host Controller", `oem2.inf`, started, with
  the children of the stack it had before still listed under
  `usbhub3.inf`, `usb.inf` and `uaspstor.inf`: the restart had not yet
  happened, so nothing of `xhci98.sys` ran yet. Explorer's details pane
  at 15:41 showed F: "Connected to USB 3.0"; taken in that state, it is
  not counted for either driver.
- Restart 15:43; `x98c` (15:54 to 16:00), **`xhci98.sys` running on
  Windows 10 for the first time in this project**:

| Device | Bound | State |
|---|---|---|
| `qemu-xhci` `1B36:000D` | `oem2.inf` (`xhci98.inf`), "xHCI98 USB 3.x eXtensible Host Controller"; `usbxhci.inf` still listed as "Best Ranked" (`00802005`) beside it | started |
| its root hub | `USB\XHCI98_ROOT_HUB`, `oem2.inf` (`80FF0000`) | started |
| `usb-hub` port 8 | `USB\XHCI98_HUB&VID_0409&PID_55AA`, `oem2.inf` (`80FF0002`) | started |
| `usb-kbd` behind it, `usb-tablet` port 5 | `input.inf` (`USB\Class_03&SubClass_01`, `USB\Class_03`) | started; the pointer and keyboard worked |
| `usb-storage` `P4333SS3` (USB 3 port), `P4333SS2` (USB 2.0 port) | `usbstor.inf`, disks `disk.inf`, volumes F: and G: | started |
| `usb-audio` port 7 | `USB\XHCI98_COMPOSITE&VID_46F4&PID_0002`, `oem2.inf`; `MI_00` `wdma_usb.inf` | started |
| `usb-uas` port 2 | `uaspstor.inf` | Code 10, as under Windows' own stack |
| the emulator's xHCI `8086:15C1` | `usbxhci.inf` (untouched: the force names only `1B36:000D`) | started |
| the NHI `8086:15BF` | none | Code 28 |

  Code Integrity event 3001: "an unsigned kernel module ...
  `xhci98.sys` is loaded into the system"; System events 20003 for the
  `xhci98` service on the root hub, composite and hub. The NHI's ids,
  read from its Enum key: `HardwareID` `VEN&DEV&SUBSYS&REV`,
  `VEN&DEV&SUBSYS`, `VEN&DEV&CC_088000`, `VEN&DEV&CC_0880`;
  `CompatibleIDs` `VEN&DEV&REV`, **`VEN&DEV`**, `VEN&CC_088000`,
  `VEN&CC_0880`, `VEN`, `CC_088000`, `CC_0880` - so on 10 the bare
  `PCI\VEN_8086&DEV_15BF` that `xhcicm.inf` names is a compatible id,
  and Intel's `&CC_0880` form a hardware id (record 21 section 3.6).
- A second boot (16:03 to 16:12) for Explorer's details pane under
  `xhci98.sys`: This PC, each drive selected - F: "USB Drive", **"Connected
  to USB 3.0"**; G: "USB Drive", no line; as under Windows' own stack on
  x64. Shut down 16:12; every guest of this kit is off (pids 65764,
  41336, 25916 in the kit's `pids.txt`).

### Not read

- A restart leg on x64: the package cannot be staged there without a
  catalog (record 13 section 6.14.5).
- Device Manager's pages under `xhci98.sys`, a hot-plug, Sleep, an
  update and the uninstall back to `usbxhci.sys`: 43.37's.
- Intel's Thunderbolt driver in a guest: no image of it here; its INFs
  were read statically (record 21 section 3.6).
