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

## 43.23 Stor98 on Windows 8 and 8.1, x86 and x64 (2026-10-22)

Branch `p43-23`. The owner's instruction of 2026-10-20 ("stor98 can be
adjusted to work on win 8 also since it's in this repo now"), in place of
42-A.21's "the bundled Stor98 not offered on 8 and 8.1". Static readings
from the owner's install media and runtime readings in QEMU on development
host A; Stor98's own record of the runtime legs is its `docs/TESTING.md`,
"Windows 8 and 8.1", and its `docs/PLAN.md` item 2.2.

### The installer row

43.43 (merged into `3.2.0.0` at `22ec1372`) already took 43.23's row, as
its entry says: Stor98's box shown and tickable on 8 and 8.1, unticked by
default, its case 3b warning, the bundled Stor98 placed on tunnel drives
only, and on 8.1 a tunnel drive whose service is `stornvme` installed over
with `INSTALLFLAG_FORCE` and recorded under `Software\xhci98\Stor98\Pending`
(record 26 section 5.3.1, case 3b). 43.23 adds no behaviour there. It moves
the `stornvme` test, written three times in `inst_eng.c` (the plan's
"no driver" reading, `call_flags` and the pending mark), into one pure
function, `inst_s98_over_inbox(is8, service)` in `inst_core.c`, and holds
it with host vectors in `test_inst.c`: on 8 and 8.1 `stornvme` in any case
is installed over; on 2000 to 7 it is another driver and left; Stor98
itself, a vendor's NVMe service (`secnvme`, `iaStorAC`), a longer name
(`stornvme2`), an empty service and no service are never installed over.
`call_flags` and the pending mark now test the system as the plan always
did (they had tested the service name alone, which on 2000 to 7 can reach
neither, since the plan leaves such a drive). `test_inst`: 1780 checks,
0 failures.

### Static: what SCSIport and the class driver on NT 6.2 and 6.3 are

Taken with `7z e` from `sources\install.wim` of
`en_windows_8_pro_vl_x86_dvd_917830.iso`, `Windows 8 x64.iso` (image 1),
`en_windows_8.1_with_update_x64_dvd_6051480.iso` (image 1) and, for the
comparison, `en_windows_7_professional_with_sp1_vl_build_x64_dvd_u_677791.iso`,
into a scratch directory outside the tree; read with WDK 7.1's
`link -dump -exports` / `-imports` and a strings sweep by a PowerShell
script kept outside the tree; nothing executed.

| Build | File | Version | Bytes | SHA-256 |
|---|---|---|---|---|
| 7 x64 | `scsiport.sys` | 6.1.7601.17514 | 171,392 | `8bbda37be8f842719a3324ca5120a9327fbc799d0d8596aecc97c538d2b28ba3` |
| 7 x64 | `Classpnp.sys` | 6.1.7601.17514 | 179,072 | `f0f1c47cc164b8bf4b99cc4e9c0d34d603051cd01ed369acb9a781ec9cca56aa` |
| 8 x86 | `scsiport.sys` | 6.2.9200.16384 | 146,672 | `a677380bb7cde97f7f74adc74e793565be3e71a2e1637f48bec9eef6ff7e2dec` |
| 8 x86 | `Classpnp.sys` | 6.2.9200.16384 | 299,760 | `966adacfef0037de184aa514e9136ebf085330106504c2dc023feb9066510297` |
| 8 x86 | `ntoskrnl.exe` | 6.2.9200.16384 | 5,563,120 | `fce2e5990281c3564a4f9aa684999e1c79f7531cacdd391adbe7eee323ad265f` |
| 8 x86 | `hal.dll` | 6.2.9200.16384 | 319,216 | `ce46da146250c6b0cde8fa14cdc5c85cdc19bcf64ceb9c383ecc732c12cfdd99` |
| 8 x64 | `scsiport.sys` | 6.2.9200.16384 | 172,784 | `d6515d9a5bb252b3fc77b8f20cd6becf12731c342949502057bde27d7a2111c2` |
| 8 x64 | `Classpnp.sys` | 6.2.9200.16384 | 336,624 | `0f95c7b3fd6b5485c5ede2dd50579cde8d9866cf35661a359e0d046a0ec7fe65` |
| 8 x64 | `ntoskrnl.exe` | 6.2.9200.16384 | 6,969,584 | `309d04e864809f20f23c1fbc77cd10de0d581b078b132281cdb6a1b7bdeb475a` |
| 8 x64 | `hal.dll` | 6.2.9200.16384 | 395,504 | `6b711a6c86c1564978ca65e360416918caf8991ddf9d041683588d59ff6bff7d` |
| 8.1 x64 | `scsiport.sys` | 6.3.9600.16384 | 170,848 | `b5ac8890b2d584d070f2a95118eb15b8b256b793b812d678c544f784699d901f` |
| 8.1 x64 | `Classpnp.sys` | 6.3.9600.17396 | 337,728 | `35ca4cc06d29dc068081976aceb5d5bec32d4663b7d93cb21599da3f92ef47a4` |
| 8.1 x64 | `ntoskrnl.exe` | 6.3.9600.17415 | 7,474,496 | `2676bd81b52e8e35d149d8753eb0c438f04c3bbbc11348f9f34758dbee26c843` |
| 8.1 x64 | `hal.dll` | 6.3.9600.17196 | 423,768 | `898637660689dc5cf91e3536109eb439a21f6f65bfd22e6f857fb75605fc22a8` |
| 8.1 x64 | `stornvme.sys` | 6.3.9600.16421 | 57,176 | `2ddea1568a85ad64fce5d10d348304fcd9be6e96c2313353ef70a2933306d188` |
| 8.1 x64 | `stornvme.inf` | - | 8,666 | `12257b98c43b178825dec26a6b54bcc2c0d060790aeeb15cdef29f2d6c6a2124` |

- **Stor98's imports are all exported.** Every NT line of
  `scripts\import-gate\stor98-imports.allow` (x86 against 8 x86, amd64
  against 8 x64 and 8.1 x64): the 19 x86 names (11 `SCSIPORT.SYS`, 8
  `NTOSKRNL.EXE`) and the 18 amd64 names (9 and 9) are all present in
  those builds' export tables. 8 x86's `scsiport.sys` exports 47 names, 8 x64's and 8.1 x64's 50.
- **SCSIport on 6.2 and 6.3 is 7's.** 7 x64, 8 x64 and 8.1 x64 export the
  same 50 names. Their imports differ by five names in all: 8 adds
  `KeInitializeSpinLock`, `KeLowerIrql`, `KfRaiseIrql` and
  `MmAllocateContiguousNodeMemory` and drops
  `MmAllocateContiguousMemorySpecifyCache` (the uncached extension now
  comes from the node-aware allocator); 8.1 imports what 8 does. The
  registry values the port reads are the same names on 7 x64 and 8 x64
  (`PnpInterface`, `MaximumSGList`, `NumberOfRequests`,
  `DisableTaggedQueuing`, `TimeoutValue` and the rest); 8 adds the
  `RtlQueryRegistryValuesEx` lookup, 8.1 a read of
  `Control\MiniNT` (Windows PE). Nothing read asks more of the miniport
  than 7 did: `HwInitializationData`, the callbacks and the INF's
  `PnpInterface\5` are taken as Stor98's NT path gives them, which the
  runtime leg below then showed.
- **The class driver.** `Classpnp.sys` exports the same 60 names on 7 x64
  and 8 x64; 8.1 adds `ClassModeSelect` and `ClassModeSenseEx`. Its retry
  policy was not read statically; the runtime legs read it.
- **8.1's own NVMe driver.** `stornvme.inf` (`DriverVer=06/21/2006,
  6.3.9600.16421`, `Provider=%MSFT%`) binds `PCI\CC_010802` in its
  `NVME.NTamd64` and `NVME.NTx86` models, as a boot-start Storport
  miniport with `ExcludeFromSelect = *`; Stor98's INFs match the same
  compatible id, so Windows' ranking (a signed in-box INF over an unsigned
  one) gives every NVMe controller to `stornvme`, which the 8.1 guest
  showed at first boot. A tunnel drive the installer forces onto Stor98
  keeps its devnode, and with it Stor98, for as long as Windows keeps that
  devnode; a drive of another model (other PCI ids) gets a new devnode and
  `stornvme`, until the installer runs again (Q2 below).

### Runtime: the vehicle

usb4-emu's QEMU `bin-2a893ea` (SHA-256 `6a1dfcc8...bd66`), xhci98's
pristine images `w8x86`, `w8x64` and `w81x64` (`E:\xhci98-vm\win8-10`)
through overlays, `-machine pc`, TCG, `-cpu qemu64,+3dnowprefetch`,
2 vCPUs, 2048 MB (the coordinator's resource rule of the day), one guest
at a time from 13:20; kit `E:\xhci98-vm\p43-23`, QMP 48640-48642. Each
guest: a plain QEMU `nvme` on the root bus (a blank 1 GiB disk, serial
`S98W8INT1`), and a `usb4-tunnel-bridge` (SHPC, `x-force-tunnel=on`,
`nvme-drive=`, serial `S98W8TUN1`) with the FAT test image
`vm\u4-asm\base\nvme-fat.qcow2` behind it; a `qemu-xhci` with a
`usb-tablet` for the pointer. No connection manager in the guest (8 and
8.1's connection-manager path is 43.22's): the tunnel up at power-on,
down with `qom-set /machine/peripheral/tb0 tunnel surprise`. **Windows 8
does not take the bridge's SHPC**: the emulator's trace shows the
surprise removal signalled and never acknowledged, and the controller's
devnode stayed started; so each removal below is a link loss with no
notice from Windows, Stor98's all-ones detection and its gone deadline
alone. The install: `UpdateDriverForPlugAndPlayDevices(NULL,
"PCI\CC_010802", <inf>, INSTALLFLAG_FORCE)` from a PowerShell script in
the guest, the installer's own call, its "Windows can't verify the
publisher" prompt answered "Install this driver software anyway". The
guest's scripts wrote `wmic`, `sc`, `reg` and `wevtutil` output to COM1;
the debug-port builds' stage lines went to `isa-debugcon` with
`usb4-debug-port`. Host load: the coordinator's note of the day (CPU
97 %), so timings are to the second.

### Runtime: results

| Leg | Driver | Result |
|---|---|---|
| 8 x86, before | - | Both controllers "PCI Device", problem 28, no service: Windows 8 has no NVMe driver |
| 8 x86, install | this branch's free x86 `stor98.sys` (`2fd7005b...29bfd`) and `stor98-x86.inf` | `ok=True` after the prompt; both controllers "Stor98 Storage Controller for NVMe SSDs", service `stor98` running, demand-start, `SCSI Miniport`, `Parameters\PnpInterface\5` = 1; both disks present (serials `S98W8INT1`, `S98W8TUN1`), the tunnel volume F:; `StorInterrupt` "pin 1 line 10 msi no msix yes port 10/10 interrupt" and "pin 1 line 5 ... interrupt" |
| 8 x86, unplug during writes | the same | a 4 MB copy looped onto F:: the 181st failed with "The system cannot find the drive specified"; disk event 153 ("... was retried") three times for each of the four lazy writes; the disk and volume gone, the controller started |
| 8 x86, with stage codes | the `STOR98_DBGPORT=1` free x86 build (`5c6de2f3...d245`) | gone entered with no command issued; each WRITE fast-failed `SRB_STATUS_NO_DEVICE` (0x08) at 0, 1.3 and 4.0 s (the same four, classpnp's retries, event 153 each time); the gone deadline at 5.0 s, `BusChangeDetected`, the next INQUIRY failed `NO_DEVICE`, the disk removed; the copy failed at about 5.4 s |
| 8 x86, `SRB_STATUS_SELECTION_TIMEOUT` | a test build with Stor98's two terminal sites set to 0x0A, never committed (`71fcde8f...05f1`), installed over the first by a copy of the INF with its `DriverVer` raised in the guest | the same shape: 0x0A fast-fails from 0.4 s, 38 event 153 entries, fastfat event 141 and the "Windows - Delayed Write Failed" popup (event 26) at about 4.7 s, the deadline and the disk's removal at 5.0 s, the copy failed at about 5.6 s |
| 8 x64, install | `releases\3.1.0.0\release-x64\stor98.sys` and `stor98.inf` (test-signed; Stor98's source unchanged since `v3.1.0.0`) | **refused**: `0xE000022F` (`ERROR_NO_CATALOG_FOR_OEM_INF`); `setupapi.dev.log`: "Driver package does not contain a catalog file, and Code Integrity is in Test Signing mode", "Failed to import driver package into Driver Store", a NULL driver installed. `bcdedit`: `testsigning Yes` |
| 8.1 x64, before | - | `stornvme` on both controllers ("Standard NVM Express Controller"), the tunnel volume F: |
| 8.1 x64, forced install over `stornvme` | the same `3.1.0.0` x64 pair | the same `0xE000022F` and the same `setupapi` lines |
| 8.1 x64, unplug during writes under `stornvme` | Windows' own | the copy in flight blocked; `stornvme` events 129 ("Reset to device ... was issued") and 11 ("controller error") every 35 s, the disk never removed; with the tunnel brought back after 4.6 min the copy completed with success 23 s later |

### Windows 10 (after 43.34)

43.34 (branch `p43-34`, finished, not yet merged here) makes Windows 10
below build 22000 the same kind of system as 8 and 8.1 (`INST_OS_CMONLY`),
and with it Stor98's row: its case 3b warning already reads "Windows 8,
8.1 or 10". `inst_s98_over_inbox` is called with that kind, so a tunnel
drive under 10's own `stornvme` is installed over, forced, as on 8.1, with
no change here beyond its comments. Read on 2026-10-22 after the 8 and 8.1
legs, on xhci98's pristine `w10x86` and `w10x64` images (Windows 10 Pro
22H2, 19045.2965), the same vehicle and guest scripts, QMP 48643 and 48644,
one guest at a time.

Static, from the same two images' `Windows` trees, read with `7z e`
through the powered-off qcow2 (the images were installed from the owner's
media; nothing executed); the export and import comparison against 8's by
WDK 7.1's `link -dump`:

| Build | File | Version | Bytes | SHA-256 |
|---|---|---|---|---|
| 10 x86 | `scsiport.sys` | 10.0.19041.1288 | 145,232 | `fcefde990f6ca548a1f6f1f606bb2d5e9a2c01d33fc8ebf78e043c78f26d4484` |
| 10 x86 | `Classpnp.sys` | 10.0.19041.2673 | 385,072 | `8e5c4bef0939f4db8cce846dea5235f0c2cd1a91c5cda6777971e58d44240ea4` |
| 10 x86 | `ntoskrnl.exe` | 10.0.19041.2965 | 7,204,688 | `f57145f91a3b6527afd0b386151c4c769b52f18c00ceb2cc7a5d405b931a2827` |
| 10 x86 | `stornvme.sys` | 10.0.19041.2075 | 126,816 | `87ec028b500bd24b4c4cc61c53f7562a3628a4333f2ab69feb2dce169b318e6d` |
| 10 x86 | `stornvme.inf` | - | 18,144 | `9ec0a307119914983e32429ee73bd7bc2cb08883822270a80ed590ee340b174f` |
| 10 x64 | `scsiport.sys` | 10.0.19041.1288 | 188,232 | `2a72c9c5cb02e03ced6a6f52000d92485bf2453374cc47e32541b5def1144b2f` |
| 10 x64 | `Classpnp.sys` | 10.0.19041.2673 | 452,656 | `5f3b2144c8b118a2666abcfc4ee30d4900fee48719ddfbb2b9c6ae8e9a5c4de9` |
| 10 x64 | `ntoskrnl.exe` | 10.0.19041.2965 | 10,865,536 | `eace65a06bab6b67461ed29783cde9367c3ddd2a31b2594c59d22d77460d4f4b` |
| 10 x64 | `stornvme.inf` | - | 18,152 | `2729d9ef07aecd2036f55ea51009ccc26f766537859dd1ea6c6b34d6bb5cfab6` |

- Every NT import of Stor98 (the 19 x86 and 18 amd64 lines of
  `stor98-imports.allow`) is exported by 10's `scsiport.sys` and
  `ntoskrnl.exe`. `scsiport.sys` exports exactly 8's names (47 x86, 50
  x64); its imports add `IoGetCurrentProcess`, `IoGetRequestorProcess`,
  `KeFlushIoBuffers`, `KeStackAttachProcess` and `KeUnstackDetachProcess`
  and drop `NlsMbCodePageTag` (and `_allmul` on x86). `Classpnp.sys` adds
  `ClassModeSenseTranslate` and `DllInitialize` to 8.1's names.
- `stornvme.inf` (`DriverVer=06/21/2006,10.0.19041.2075`) binds
  `PCI\CC_010802` with `ExcludeFromSelect = *`, as 8.1's does.

| Leg | Driver | Result |
|---|---|---|
| 10 x86, before | - | `stornvme` on both controllers, the tunnel volume F: |
| 10 x86, forced install over `stornvme` | the free x86 of the 8 legs (`2fd7005b...29bfd`) | the unsigned-driver prompt, "Install this driver software anyway"; `ok=True`; both controllers Stor98, `stor98` running, `stornvme` stopped, both disks and F: present, `PnpInterface\5` = 1 |
| 10 x86, unplug during writes | the same | the 56th copy failed about 5.9 s after the `qom-set` ("The system cannot find the path specified"); fastfat event 50 with the "Delayed Write Failed" popup (event 26) at about 2.4, 3.8 and 5.1 s, 37 disk event 51 (paging-operation errors) at 5.1 s, disk event 157 ("Disk 2 has been surprise removed") at 5.4 s; no event 153; the controller started; the SHPC removal again never acknowledged (`awaiting_ack=1`); the disk did not come back when the tunnel did |
| 10 x64, install | `releases\3.1.0.0\release-x64` | **refused**: `0xE000022F`, `setupapi.dev.log` "Driver package does not contain a catalog file, and Code Integrity is in Test Signing mode", "Failed to import driver package into Driver Store. Error = 0xE000022F" |

Decided: Stor98's x86 NT path runs on Windows 10 22H2 as it is, over 10's
own `stornvme` by the installer's forced call, and its docs and INF
comments now say 2000 to 10; the x64 package's refusal is the same as on
8 and 8.1 (Q1 covers 10 x64 too). Owed on 10: PLAN 2.2's terminal-status
question (only `NO_DEVICE` ran, with no stage-code build, and 10 logged no
retry event, so its retry count is unread); 10 builds other than 22H2;
the connection manager's legs on 10 (43.37).

### What it decides

- Stor98's NT path runs on Windows 8 (32-bit) as it is: no source change.
  Its INFs need no new decoration; their comments, Stor98's `AGENTS.md`,
  `README.md`, `docs/TESTING.md`, `docs/PLAN.md` 2.2 and the package
  readme now say 2000 to 8.1.
- PLAN 2.2's question for 8: neither `NO_DEVICE` nor `SELECTION_TIMEOUT`
  ends classpnp's retries there (2000 and XP x64: both did). What ends
  them is the disk's removal after `BusChangeDetected`, and Stor98's
  fast-fail makes each retry cost nothing. `NO_DEVICE` stays the default.
- **The x64 package cannot be installed on 8 x64 or 8.1 x64**, Stor98's
  nor, by the same rule, any of this package's x64 INFs: none names a
  `CatalogFile` (record 19: "no catalog"). That is 43.22's to settle for
  `xhcicm`, and an owner decision (Q1).

### Not read

- Windows 8.1 x86: no medium (owner, 2026-10-22: skipped for `3.2.0.0`,
  untested ground, covered by 8 x86 and 8.1 x64; the release notes'
  untested row).
- Stor98 x64 running on 8 and 8.1 (refused at the driver store), and so
  the kernel's acceptance of its SHA-1 test signature there.
- A removal Windows is told of (pci.sys removing the controller), and
  xhci98's link-down statement on 8 and 8.1: 43.24, with the installer's
  own run there.

### Questions for the owner

1. **A catalog for the x64 package.** Windows 8 x64, 8.1 x64 and 10 x64
   refuse every catalog-less package in test-signing mode, so Stor98, and by the
   same rule `xhcicm.inf` and the USB drivers' x64 INFs, cannot install
   there as the package stands. Recommendation: the packager makes one
   catalog per x64 INF (WDK 7.1's `inf2cat` or `makecat`) and signs it with
   the same test key at the same step, each x64 INF gaining
   `CatalogFile=`; record 19 amended; read on 7 x64 again (a catalog the
   test certificate signs must not change what 7 x64 does) and on 8 x64.
   Until then the release notes say the x64 drivers do not install on 8,
   8.1 and 10.
2. **A tunnel drive first plugged after the install, on 8.1 and 10.** It gets
   `stornvme` (Windows' ranking), not Stor98, until the installer runs
   again; on 2000 to 7 the staged `stor98.inf` takes it by itself.
   Recommendation: accept it and say so in the release notes and the
   installer's warning text, rather than raise Stor98's rank.
