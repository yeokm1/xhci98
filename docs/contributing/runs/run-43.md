# Run 43 - Release `3.2.0.0`

Phase 43 of `roadmap-usb4.md`, for release `3.2.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

## 43.11 the installer's settings pages on 98 SE, XP and 7 (2026-10-22)

43.10's layout (design record 26 section 5.4, "The layout") read on the
screen: 98 SE at 640x480, XP and 7 at 96 DPI, 7 at 120 DPI. Runtime,
development host A, QEMU, one guest at a time, 16:54 to 19:11.

**Verdict: passes on every point, on all four.** Every drop-down of every
settings page opened and closed shows its longest entry whole; the note
shows its whole text with no scroll at all; the USB settings page, the
one with four asked rows, is two parts, "(page 1 of 2)" and "(page 2 of
2)", and Back and Next move between them and to the pages either side;
a value set on a part or a page is still set after Back and Next, and
the Summary lists it.

### The vehicle

- The build: the gated build of `3.2.0.0`'s HEAD `2038250e`, on branch
  `p43-11` (`build-driver.cmd all` and `all -amd64` both passed; the
  installer's window self-test 584 checks and `test_wizlay` 199, both
  with no failure). Staged with `make-release.ps1 -Arch x86 -TestLabel
  -NoDriverPack -SkipQualtool` into a scratch directory; `XHCIWIZ.EXE`
  `16ab9fc5...`, the gated build's own file. The package still reads
  `3.1.0.0` (the version is not yet bumped).
- The guests, each on an overlay of its own over another kit's image
  (kit `E:\xhci98-vm\p43-11`, not in the tree; every screen is in its
  `shots\`):
  - 98 SE: p43-12's pristine 98 SE (no display driver: 640x480, 16
    colours), 1 vCPU, 256 MB; then p43-13's `w98-hcd310`, the same
    install with `3.1.0.0`'s drivers on the controller (see below).
  - XP SP3 32-bit: 1024x768, `LogPixels` 0x60 (96 DPI, read from the
    registry), 2 vCPUs, 512 MB.
  - 7 SP1 32-bit: Phase 38's matrix `f` overlay (`2.4.0.0` installed,
    `XhciInterruptMode` 2 on the controller from 43.13's legs), 800x600,
    2 vCPUs, 1536 MB; read at 96 DPI, then with `LogPixels` 120 written
    under `HKCU\Control Panel\Desktop` and Windows restarted (the fonts
    and the window larger on the screen).
- Every guest: a `qemu-xhci` with no device on it, so no Thunderbolt
  controller. The package was on an IDE VVFAT drive; `SETUP.EXE` was run
  inside the guest (Start, Run), elevated through UAC on 7. Keys by QMP
  `send-key`, screens by QMP `screendump`; no display client.
- Each run: on Components, Thunderbolt 3 / USB4 support and then Stor98
  ticked (the staged-only case of section 5.5, so the Thunderbolt and
  Stor98 pages are shown); on each settings page every enabled drop-down
  focused and opened (Alt+Down) and closed, a value changed with the
  arrow keys; Back and Next across the parts and pages; the Summary
  read; Back from the Summary to the USB page's first part; the wizard
  cancelled, so nothing was installed.

### Results

The pages, the same on all four but for the one 98 SE row:

| Page | Rows | Parts |
|---|---|---|
| USB settings | `XhciIntelPortSwitch` (greyed, "(Intel 7-9 series, C610 only)"), `XhciInterruptMode`; then `XhciSilentInstall` (greyed on XP and 7, "(Windows 98 SE and ME only)"; asked on 98 SE), `XhciForceBulkOnly` | two, "(page 1 of 2)" and "(page 2 of 2)" |
| Thunderbolt settings | `XhciCmMode`, `XhciCmApprove`, `XhciCmSafeEject`, each "now: not set (default 1)" | one, no count |
| Stor98's settings | `writethrough`, `pollmode` (`StorWriteThrough`, `StorPollMode` on 98 SE) | one, no count |

- **Drop-downs.** Every closed box spans the column and its choice reads
  whole; every open list is as wide as the closed box and every entry
  reads whole. The longest: "3  the driver's own USB4 connection
  manager, forced", "cacheoff - turns the drive's write cache off (fua if
  it refuses)", "not set (the built-in default is auto (here: off))",
  and on 7 "3  a message-signalled interrupt only". The "now" text sits
  on the label's line at the right, whole, wrapping inside its column
  where long (Stor98's "not set (the built-in default is auto (here:
  off))" in two lines). The greyed rows' boxes show their value whole.
- **The note.** The longest note, `XhciIntelPortSwitch`'s, is nine lines
  at 96 DPI (seven at 120) in a box of about thirteen; every note read
  whole with no scroll. Focusing a drop-down shows its row's note; a new
  part shows its first row's.
- **Parts, Back and Next.** Next on part 1 shows part 2 on the same
  page; Back on part 2 shows part 1; Next on part 2 goes to the
  Thunderbolt page; Back from the Thunderbolt page shows the USB page's
  part 2 (entered by Back, its last part). "Advanced..." from part 2
  (USB, 7 at 96 DPI and 98 SE) returned to part 2.

**Values kept.** Set, then read after Next and Back across parts and
pages, and after Back from the Summary:

| System | USB part 1 | USB part 2 | Thunderbolt | Stor98 | Summary |
|---|---|---|---|---|---|
| 98 SE (update over `3.1.0.0`) | `XhciInterruptMode` 0 to 1 | `XhciSilentInstall` 0 to 1 | `XhciCmApprove` 0 | `StorPollMode` 1 | lists `XhciInterruptMode = 1`, `XhciSilentInstall = 1` (the rest under a scroll) |
| XP, 96 DPI (fresh) | 0 to 1 | `XhciForceBulkOnly` 0 to 1 | 0 | `pollmode` 0 | `XhciInterruptMode = 1`, `XhciForceBulkOnly = 1`, `XhciCmApprove = 0` |
| 7, 96 DPI (update over `2.4.0.0`) | 2 to 3 | 0 to 1 | 0 | `pollmode` 1 | `= 3`, `= 1`, `= 0` |
| 7, 120 DPI | 2 to 3 | 0 to 1 | 0 | `pollmode` 0 | `= 3`, `= 1`, `= 0` |

Each value was in its box on every return.

- **The Advanced dialogs** (43.10's; read here once each, not a 43.11
  point): the USB one on 7 at 96 DPI and on 98 SE, `XhciFastPollFsLs`'s
  list whole, the dialog inside 640x480; the Thunderbolt one on 7 at 120
  DPI, `XhciCmLogVerbosity`'s list whole.

### Seen on the way, not 43.11's points

- **98 SE with no controller listed has no USB settings page.** On the
  pristine 98 SE, its Add New Hardware wizard for the controller
  cancelled, "What was found" says no device is present and the wizard
  goes from Components to the Thunderbolt page: a value with no
  controller stays off the pages (section 5.4). The 98 SE leg was
  therefore read on the same install with `3.1.0.0` on the controller,
  as an update.
- **7 at 120 DPI on 800x600**: the window (about 635 by 575) is taller
  than the work area; its frame below the buttons sits under the
  taskbar, the buttons whole. A screen size, not the pages' layout.
- **Components**: with the keyboard, ticking Thunderbolt and then Tab,
  Space on Stor98 left it unticked on all four; a second Space ticked
  it. Not read further (the Components page is not 43.10's).
- **`XhciCmLogVerbosity`'s value 4** is listed as a bare "4" in the
  Thunderbolt Advanced dialog (the table carries four meanings, for 0 to
  3), and the note reads "4: allowed."; its sentence is in the note's
  NOTE. A text, not the layout.
- **The cut's text checks refuse HEAD**: `make-release.ps1` stopped on
  two readme lines past 78 columns, `xhciinst\README.TXT` line 313 ("...
  it never changes it.", from the 43.34 merge) and `xhcitr\README.TXT`
  line 90 ("... On Windows 2000 without administrator", from 43.52). The
  test package was staged with both lines rewrapped in the working tree
  for that run only; neither change is committed here.

### Owed

Nothing for 43.11. The three findings above are for the owner; the two
readme lines must be rewrapped before the `3.2.0.0` cut.

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

## 43.18 - The 9x Link-Down Statement's Gate-G3 Readings on 98 SE and ME
Task 43.18 of `roadmap-usb4.md`, the third of the owner's answers of
2026-10-22 ("What 43.18 left" in the decisions table): the gate-G3
readings of the 9x link-down statement, one per system, on a trial build
in emulator guests, each system that passes admitted. Read on
2026-10-22. (A file of its own because `run-43.md` lives on another
branch; the two are joined at the merge. Record 21 section 7.2 part 2
names `run-42.md` for these readings; this sheet holds them instead.)

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

### Verdict

**Both systems pass; both are admitted** (`cm_eject.c`,
`CM_EJ_QUALIFIED_98SE` and `CM_EJ_QUALIFIED_ME` set to 1). On 98 SE under
the ICM client and on ME under the USB4 profile, at every point read, the
statement (the entry marked `DOWN_QUIESCED`) came after the emulator's
stage-2 reset and quiesce of the endpoint, the acknowledgement wait ended
after the statement, and the vanished child's `CM_Query_Remove_SubTree`
came after the wait's end. Every wait but one ended acknowledged, in 41
to 220 ms; the one that did not is described under ME, run C2: a
system-modal error screen Windows put up stopped every thread, the
connection manager's included, and the answer that came after it counted
as late. The ordering held there too.

### The vehicle

- Development host A, QEMU under TCG, headless, one vCPU per guest
  (`pentium3,-apic`), 256 MB; `usb4-emu` at `660f085` (the emulator's
  SHA-256 prefix `9dfdcf9b`), with `-device usb4-debug-port` for the
  stage codes and `-trace usb4_tp_*`, `usb4_dbg_*`, `usb4_h_*`.
- **98 SE**: the 3.0.0.0 image, the Alpine Ridge topology of the ICM
  client (`profile=tb3-alpine-ridge`, the TB3 PCI bridges, an SHPC tunnel
  bridge), updated with the published `3.1.0.0` package,
  `INSTALL /console /yes /thunderbolt /stor98`, the enclosure plugged
  (Windows' files the install asked for skipped or pointed at
  `C:\XHCI98`), then restarted.
- **ME**: the 3.1.0.0 image with the Thunderbolt driver and Stor98 of
  the joint legs, the generic USB4 topology (`profile=usb4-generic`, two
  tunnel ports), an ASM2464 USB4 enclosure.
- The NVMe drive: a 128 MiB FAT image, enclosure `enc0` hot-plugged after
  the desktop on both; a 16 MiB file of random data copied onto it as
  `E:\BIG.BIN`. Stor98: the bundled 3.1.0.0 build on both.
- **The trial build** (never committed): the `qemu` flavour of
  `xhcicm.sys` from a `git archive` of `p43-18` at `e620f8bf`, with both
  systems admitted and three stage codes written to the debug port (code
  at `5A0h`, argument at `5A4h`): `0x1C0` an entry marked
  `DOWN_QUIESCED` (argument generation, bus, slot), `0x1C1` an entry's
  acknowledgement wait ended (argument `CM_NVLINK_AW_*` << 8 | entry: 1
  acknowledged, 5 late), `0x1C2` the CONFIGMG worker's
  `CM_Query_Remove_SubTree` of the vanished child (argument the devnode).
  SHA-256 prefix `7901ed4d`, copied over the installed `xhcicm.sys`, then
  a cold restart.
- **The stage-2 event**: `usb4_tp_cutoff` ("stage 2 complete"), the
  emulator's endpoint reset and quiesce at the tunnel down. Times below
  are virtual time from it. The unplug is `device_del enc0`; a held read
  is the harness's `x-faults` rule `at=fetch op=read ... act=hold`, the
  unplug issued over QMP as soon as `x-harness-state` shows the read held
  (`E:\xhci98-vm\p43-18g3\scripts\holdunplug.py`); the re-plug leg unplugs
  as soon as `cm.nvlink.up=` reaches the debug console
  (`plugunplug.py`). The load is `copy /b E:\BIG.BIN NUL` in an MS-DOS
  Prompt, after a re-plug so that the file is not in the cache.
- The host was shared with other guests and builds (CPU near 97 % by
  midday): the times are the guests' virtual time and what they show is
  the order; their sizes are noisy.

### Results

| System, leg | 0x1C0 statement | 0x1C1 wait end | 0x1C2 query-remove | Driver's lines |
|---|---|---|---|---|
| 98 SE, idle unplug | +68 ms | +171 ms, acknowledged | +178 ms | `cm.nvlink.icm-down=000B0400`, `cm.nvlink.await=0000006E` (110 ms), query and removal 0 |
| 98 SE, unplug with the file read from the cache (no read reached the drive) | +201 ms | +257 ms, acknowledged | +267 ms | `await=00000037` (55 ms) |
| 98 SE, the 20th read held at its fetch, unplugged 3 ms after | +116 ms | +165 ms, acknowledged | +172 ms | `await=00000029` (41 ms); the query vetoed (`0x17`, the copy holds the file open) |
| 98 SE, every read held (`repeat=on`), unplugged 1.5 s after the first | +63 ms | +128 ms, acknowledged | +134 ms | `await=00000037` (55 ms); query vetoed |
| 98 SE, re-plug, unplugged 3 ms after `cm.nvlink.up` (1.34 s after the plug) | +219 ms | +372 ms, acknowledged | +378 ms | `await=00000097` (151 ms); query and removal 0 |
| ME, idle unplug | +243 ms | +458 ms, acknowledged | +463 ms | `cm.nvlink.u4-down=000B0000`, `await=000000DC` (220 ms), query and removal 0 |
| ME, the 20th read held at its fetch, unplugged 2 ms after | +271 ms | +323 ms, acknowledged | +329 ms | `await=00000036` (54 ms); query vetoed |
| ME, every read held, unplugged 1.5 s after the first | +107 ms | +216 ms, acknowledged | +222 ms | `await=0000006E` (110 ms); query vetoed |
| ME, run C2: every read held, two MS-DOS Prompts reading, unplugged 6.3 s after the first | +229 ms | +72 311 ms, late | +72 317 ms | `await=000118AE`; a system-modal "Disk Write Error" screen up from the unplug until a key was pressed (below) |
| ME, re-plug, unplugged 2 ms after `cm.nvlink.up` (3.31 s after the plug) | +222 ms | +275 ms, acknowledged | +280 ms | `await=00000037` (55 ms); query and removal 0 |

Every leg: `cm.nvlink.down` once, `cm.nvlink.retired` after the wait,
then the query; the re-plug that followed published `UP` again under a
new generation. In each leg with a held read the copy failed with
Windows' own "General failure reading drive E" or "The volume that was
removed had open files" and Windows ran on.

**The queue depth.** The held reads never went past one: an MS-DOS copy
reads one request at a time, and a second MS-DOS Prompt reading another
file on the same drive (run C2) did not add a second request at the
drive while the first was held. "A saturated queue", as record 21 section
7.2 part 2 asks for it, is not reachable this way on 9x; the most it
reached is a held request with more behind it in Windows. **Owner's
decision, 2026-10-22:** the one-request reading meets the "saturated
queue" point on 98 SE and ME.

**ME, run C2.** After the unplug Windows put up its system-modal "Disk
Write Error" screen for drive E (what it was writing was not read). While it was up, nothing ran: the statement was made
before it (+229 ms) but the 50 ms pass that ends the wait did not run
until the key was pressed, 72 s later, and then found Stor98's answer
past the 3 s bound (`CM_NVLINK_AW_LATE`). The removal still came after
the wait's end, and the ordering held. A system-modal screen stops this
driver's wait on 9x as it stops Windows; that is the systems' own
behaviour, not something the driver can shorten.

**Not a leg: the harness's surprise rule on the USB4 profile.** A rule
`act=surprise` takes the tunnel down without unplugging the enclosure's
router. On ME the USB4 connection manager saw nothing (its down comes
from the router's hot-unplug); when the enclosure was removed 2.7 minutes
later it found the function already gone and withheld the statement
(`cm.nvlink.withheld=03000002`), as the condition says. Every unplug
above is a `device_del` of the enclosure.

### What it changes

Both systems admitted in `cm_eject.c`; a host vector checks it
(`test_cm_eject`), and `test_ej9x` checks that the tools' refusal for a
drive with no Stor98 channel tells the user to shut Windows down before
unplugging it. The statement's other conditions are unchanged: the
emulator's bridge, one member under the ICM client, the known
correspondence under the USB4 profile, so on hardware no statement is
made. The admission also opens the two eject paths that needed it on 9x
(the failed branch's isolation, the USB4 profile's clean-removal teardown
with its `DOWN_QUIESCED`); those are read by 43.19's legs.

The guests' logs: `E:\xhci98-vm\p43-18g3\keep` (`trace-*.log`,
`debugcon-*.log`).

### 43.19, first legs: `XHCIINST /eject` on 98 SE and ME (2026-10-22)

After the admission's commit (`fef563af`), the same guests with that
commit's gated `qemu` build of `xhcicm.sys` (from the `scripts\build-driver.cmd all`
run that passed, SHA-256 prefix `e3697476`) and its `XHCIINST.EXE` copied
in (`XHCITR.EXE` was not: the tray had it open on 98 SE), a cold restart,
the enclosure plugged, then `XHCIINST /eject 1` and `Y`, then the unplug.
Not 43.19's whole: XHCITR's Safely Remove, the two-port legs and 42-A.14
are not read.

| System | What the tool said | The driver's lines |
|---|---|---|
| 98 SE, ICM client | "Asked the Thunderbolt driver to stop the device. Windows has stopped Stor98 Storage Controller for NVMe SSDs. ... You can now unplug the device.", exit 0 | `cm.eject.accepted`, the query (`cm.eject.call=41..`), the request (`42..`, `cm.eject.request=00000100`: Stor98's `RemoveAck`), the removal (`43..`), `cm.eject.result=080291E0`: step 8 removed, outcome 2 clean; no teardown (decision V8) |
| 98 SE, the unplug after it | - | the statement made on the tunnel down, and its wait ran to the 3 s bound (`cm.nvlink.await.end=00000400`, timeout, `await=00000BE8`): the drive's Stor98 had already been removed by the eject, so nothing could answer. Only the member's re-enumeration waited; there was no vanished child to remove |
| ME, USB4 profile | the same words, exit 0 | the same steps, then the teardown the admission opens: `u4.eject=00000100`, the tunnel taken down (`admin-down` in the emulator's trace, its stage-2 event), `cm.eject.teardown=00002801`, `cm.nvlink.down=00000001` and `cm.eject.mark=00000001` (the entry's `DOWN_QUIESCED`), the entry retired; `cm.eject.result=0802BA4A`, clean. The unplug after it logged nothing (the tunnel was already down), and a re-plug brought the drive back with a new generation (`cm.nvlink.up=02000101`) |

**Finding (98 SE, for 43.19):** after a clean eject under the ICM client,
the unplug's statement waits the full 3 s for an acknowledgement no
Stor98 can give, because the entry still reads `UP` after the eject
removed the drive's Stor98 (under the USB4 profile the teardown retires
the entry first). It delays only the member's re-enumeration, by the 3 s
acknowledgement deadline when Windows schedules the driver's 50 ms pass
on time; the deadline bounds what counts as an answer, not when the wait
is next looked at, so a stalled 9x scheduler can hold it far longer (as
run C2's 72 s shows).
**Fixed the same day on the owner's answer** (below, "The 3 s wait after
a clean eject").

**ME between the two sessions.** The ME guest, paused over QMP for about
an hour while the builds ran and then resumed, was found with a black
text-mode screen and the processor in the VMM, not answering keys; it
was quit and cold-started. Whether this is the hang task 43.49 reads (not
the same trigger: no plug cycles were running) is not read here.

### The 3 s wait after a clean eject (2026-10-22)

The owner's answer to the 98 SE finding above: fix it in 43.18. A
removal that ends clean with an entry and no teardown to follow (the
firmware connection manager, decision V8) now takes the entry from `UP`
to `IN_USE` alone after the removal (`CM_EJ_P_RETIRE`,
`CmNvlinkRetireEjected`): no `DOWN_QUIESCED`, no wait, the generation
kept. ME under the USB4 profile does not share the cause (its teardown
retires the entry first); NT does not either (Windows ejects there, and
the driver is not told that Stor98 left), and neither is changed. The
same change makes the eject's own statements take the whole
qualification: the member must read as the emulator's bridge, not only
the systems be admitted (`CmEjStatementQualified`, logged as
`cm.eject.emulator`).

**The retake, 98 SE under the ICM client.** The same guest and overlay as
the first leg, with the uncommitted tree's gated `qemu` build of
`xhcicm.sys` (`scripts\build-driver.cmd all`, x86, passed; SHA-256 prefix
`a9e38b90`) copied over the installed one and a cold restart; the host
loaded by other agents' guests and an amd64 gated build. The enclosure
plugged (`cm.nvlink.up=01000401`), then `XHCIINST /eject 1` and `Y`:
the same words, exit 0. The driver's lines: `cm.eject.emulator=00020802`
(member 02:08, read as the emulator's bridge), the query, the request and
the removal as before, `cm.eject.retire=00000001` (entry 0 retired), then
`cm.eject.result=08025107` (removed, clean). The unplug: the tunnel down,
the statement decided (`cm.nvlink.9x.decided=00000200`), and then no
`cm.nvlink.down` and no wait: the member's re-enumeration ran at once
(`cm.reenum.held.run`), where the first leg logged
`cm.nvlink.await.end=00000400` and `await=00000BE8`. A re-plug brought the
drive back with a new generation (`cm.nvlink.up=02000401`) and `dir E:\`
listed its files. Logs: `E:\xhci98-vm\p43-18g3\keep\debugcon-w98-r2.log`,
`trace-w98-r2.log`.

## 43.21 - the unexpected-removal notice, leg T.11 (2026-10-22)
Roadmap 43.21; design record 25 section 7.6 (revision 16) and leg T.11 in
its section 11.

**Verdict: done** (2026-10-22, ticked on the owner's approval). Every leg
that can be read on the emulator passes on 2000, XP, XP x64, Vista x86,
7 x86, 7 x64 and 98 SE. Two parts are recorded as not readable here, as
43.45's resumes were:

- A chain's first device unplugged. The emulator's chained routers exist
  only under the software connection manager's `usb4-generic` profile on
  q35; on the `pc` machine every NT guest uses, one tunnel-bridge level is
  reachable, so a unit behind a chained router never appears on the PCI
  side (usb4-emu `docs/XHCI98-HANDOFF.md`, "Chained PCIe tunnel"). It is
  listed for the owner's session on real hardware.
- Sleep on 2000 and XP: those images have no standby or hibernate.

One defect was found and fixed during the legs: on XP the warning balloon
never showed, because the unit's "disconnected" balloon of the same poll
replaced it at once. Commit `b03d4422` puts a due warning and the other
events of that poll in one balloon under the warning title. Every leg
after that fix ran with its XHCITR; the 2000 legs before it were retaken
for the main case.

### Build

Branch `p43-21`. Gated `build-driver all` passed for x86 and amd64 at
`b03d4422` (x86 `qemu` `xhcicm.sys` `9FE8E826...`, release `B3C59BCE...`;
amd64 `qemu` `5A766C66...`, release `77CF75FF...`; `test_tr` host vectors
pass). The x86 guests ran the `qemu`-flavour `xhcicm.sys` of `1b27ce1d`
(`B463877F...`); `b03d4422` changed no driver source, only XHCITR, its
tests and documents. XHCITR was the gated `b03d4422` build (`5B85735C...`)
on every NT leg but the first 2000 boot and the first XP boot, which found
the defect. The 98 SE guest ran `1b27ce1d`'s XHCITR: `b03d4422` changes only
a poll with a warning, which 98 SE never has.

### The vehicle

Development host A, QEMU TCG under heavy load from other guests (CPU near
97 %). The usb4-emu build copied from task 43.44's kit (`bin`, and `bin-icl`
for Ice Lake). Topology as task 43.44's: `usb4-host` with a firmware
connection manager (Titan Ridge, Alpine Ridge on 98 SE, Ice Lake on 7 x86),
the tunnel bridge, and `enc0`, the ASM2464 device router with an NVMe
namespace behind it, plugged with `device_add` and pulled with
`device_del`. NVMe through nvme98 on NT. The mark service (`XHCIMARK`,
`XHCIMK64` on x64) registered on each NT guest. Guests: 2000 (`acpi=off`,
1 vCPU), XP (1), XP x64, Vista x86, 7 x86, 7 x64 (2 each), 98 SE (1). Kit
`E:\xhci98-vm\p43-21` (`RUN-NOTES.md`, screenshots, debug-console logs).

The count is read in two places: the driver's debug-console line
`cm.reenum.unexpected=<member>` and the NHI's details in XHCITR,
"Unexpected removals: N since this start; the last on tunnel port ...".

### Results

| Case | 2000 | XP | XP x64 | Vista | 7 x86 | 7 x64 |
|---|---|---|---|---|---|---|
| Unit present at boot and never marked: unplug | "disconnected" only, not counted | same | same | same | same | same |
| Marked unit, unplug with no eject | warning, counted | warning, counted | warning, counted | warning, counted (a poll late, below) | warning, counted | warning, counted |
| Eject from XHCITR, then unplug | "disconnected" only | same | same | same | same | same |
| Driver update of the unit (nvme98 forced) | nothing | nothing | nothing | nothing | nothing | nothing |
| Disable and enable of the unit | nothing | nothing | nothing | nothing | nothing | nothing |
| Option off, unplug with no eject | no warning, counted | same | same | same | same | same |
| Option on again, unplug | warning | - | warning | - | - | - |
| Sleep and resume with the unit attached | no standby (`acpi=off`) | no S1-S4 on this image | hibernate: nothing, count unchanged | hibernate: nothing | hibernate: nothing | hibernate: nothing |

"Warning" means one balloon titled "Thunderbolt/USB4 warning" reading "A
Thunderbolt or USB4 device was removed unexpectedly." with "ASMedia 246x was
disconnected." on the next line. On 2000 the first boot, before
`b03d4422`, showed the warning and then the "disconnected" balloon; the
retake with `b03d4422` showed the single combined balloon.

On Vista the departure fell one poll before the count: "disconnected"
first, the warning alone a poll later. Section 7.6 item 4 accepts this.

A unit present at boot is counted only once it is marked: the mark
service marks it at its first plug while the service runs, and removes the
mark when the unit leaves. Section 7.6 item 1 and the release notes say so.

On 98 SE (Alpine Ridge): the tray menu and the View menu have no "Warn
when a device is removed unexpectedly" item; the NHI's details read
"Unexpected removals: not watched on this system"; an unplug gives the
tooltip "ASMedia 246x was disconnected." and nothing else.

### Not read

- A chain's first device unplugged: not readable on the emulator (above).
- Sleep on 2000 and XP: those images have no standby or hibernate.

### Findings outside 43.21

- After a hibernate and resume on 7 x86, 7 x64 and XP x64, XHCITR showed
  "The Thunderbolt/USB4 controller stopped answering." This is the
  existing finding from 43.45: the liveness gate's off state across a
  sleep. The count and the unit were unchanged.
- On 7 x86, after a resume, an eject by a standard user with an AutoPlay
  Explorer window open on the unit left the replugged device not started
  until a restart. It did not recur after a cold boot.
- On Vista and 7 x64, a guest asked to shut down at the end of its legs
  stayed at "Shutting down..." and was stopped through QEMU.

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

**Verdict: open** (Codex round 1, 2026-10-22). The x86 half is read; the
x64 half cannot be until the x64 packages carry a test-signed catalog,
which branch `p43-36s` is adding (owner's decision). Waiting on it:
Stor98 x64's install and load on 8 x64, 8.1 x64 and 10 x64, and so a
removal under Stor98 on 8.1 at all (8.1 was read on x64 only, under its own
`stornvme`). The owner may instead defer them past `3.2.0.0`.

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
neither, since the plan leaves such a drive). `test_inst`: 1805 checks,
0 failures, with the Windows 10 vectors ("Windows 10" below) after the merge of `3.2.0.0`
at `2038250e`; the gated `all` builds, x86 and amd64, passed.

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
- **SCSIport on 6.2 and 6.3 against 7's, by its interface only.** 7 x64, 8 x64 and 8.1 x64 export the
  same 50 names. Their imports differ by five names in all: 8 adds
  `KeInitializeSpinLock`, `KeLowerIrql`, `KfRaiseIrql` and
  `MmAllocateContiguousNodeMemory` and drops
  `MmAllocateContiguousMemorySpecifyCache` (the uncached extension now
  comes from the node-aware allocator); 8.1 imports what 8 does. The
  registry values the port reads are the same names on 7 x64 and 8 x64
  (`PnpInterface`, `MaximumSGList`, `NumberOfRequests`,
  `DisableTaggedQueuing`, `TimeoutValue` and the rest); 8 adds the
  `RtlQueryRegistryValuesEx` lookup, 8.1 a read of
  `Control\MiniNT` (Windows PE). That is all the static read covers:
  exports, imports and registry value names. The miniport contract itself
  was **not** compared with 7's: `ScsiPortInitialize`'s checks of
  `HW_INITIALIZATION_DATA` (size, fields, the PnP path), the
  `PORT_CONFIGURATION_INFORMATION` it fills, how it calls `HwFindAdapter`,
  `HwInitialize`, `HwStartIo`, `HwInterrupt`, `HwResetBus` and
  `HwAdapterControl`, and which PnP and power SRBs and adapter-control
  types it sends. What stands in for that read is the runtime one: on 8
  x86 (and 10 x86) SCSIport took Stor98's `HW_INITIALIZATION_DATA` and the
  INF's `PnpInterface\5`, found and started both adapters, ran I/O and
  the removal path. Unread, statically and at runtime: the x64 SCSIport
  (behind the catalog), a PnP stop and restart (Device Manager's disable
  and enable), a hibernate or sleep and resume, and
  `ScsiStopAdapter` / `ScsiRestartAdapter` on 6.2 and later.
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
no change here beyond its comments. Host vectors in `test_inst.c` hold it
through `inst_os_kind`: on 10 1507 and 22H2 Stor98 is asked with Enter
giving N, `/yes` gives N, `/stor98` installs it unasked, and a drive under
`stornvme` is installed over while a vendor's driver is left; 11 (build
22000) is not that kind. Read on 2026-10-22 after the 8 and 8.1
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

- Stor98's NT path runs on Windows 8 (32-bit) as it is, for start, I/O
  and a removal: no source change (the contract comparison and the PnP
  and power paths are unread, above).
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
  the kernel's acceptance of its SHA-1 test signature there, and a
  removal under Stor98 on 8.1: waiting on `p43-36s`'s catalog.
- SCSIport's miniport contract on 6.2 and 6.3 compared with 7's, and the
  PnP stop, restart and power paths (above).
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

## 43.49 - Windows ME Stops After Back-to-Back Mouse Plugs and Unplugs
Roadmap-usb4 task 43.49, opened by the owner on 2026-10-22 from 43.13's ME
legs. Development host A, QEMU under TCG, one virtual CPU, 256 MB, headless,
`qemu-xhci` (`p2=4,p3=4`); the host was heavily loaded by other agents'
guests throughout (the coordinator measured 97 % CPU at about 13:45), so no
timing below is a performance reading. Branch `p43-49` off `3.2.0.0`
(`77384e93`); the driver source under test is that of `dd46948c`, the build
43.13 found the stop on (the only `src\` difference between the two is
`src\silent-on.bat`).

### Method

- Driver: the `qemu` flavour of `d35829cf` (gated run, 364,084 bytes), and
  for runs `d2` and `c2` ungated rebuilds of the same flavour (c2 with one
  experimental change, below), copied over the installed `xhci98.sys` on
  overlays of the 43.13 bases (ME + `3.1.0.0`; 98 SE + `3.1.0.0` + NUSB 3.3).
  Registry as 43.13's: `XhciInterruptMode` 2, `XhciPollIdleMs` 50,
  `XhciPollFineTimer` 0, `XhciLogVerbosity` 2; cold boot after the change.
- The trace: QEMU `isa-debugcon` at port `0xE9`, the `qemu` flavour's mirror.
- The series: a mouse `m1` at root port 1 and a hub `h1` at root port 4
  (ME: and a stick at port 2), an MS-DOS Prompt open; then `usb-mouse`
  hot-plugs alternating root port 3 and hub port 4.1, each removed as soon
  as the pointer seemed to move (43.13's `plug.py`, no gap between cycles).
  A cycle that timed out was taken as a stop only when the tray clock did
  not change across a further 75 s.
- At a stop: `info registers`, and the guest's physical memory saved over
  QMP (`pmemsave`) and read offline: driver and device objects found by
  their type and size words, the HID stacks' extensions read through the
  guest's page tables (kernel space, CR3 from the register dump).
- Static reads: ME's `HIDUSB.SYS` (14,256 bytes, SHA-256 `6648b28c...2b91`),
  ME's `HIDCLASS.SYS` (25,584 bytes, `07b5d374...59cb`) and 98 SE's
  `HIDUSB.SYS` (9,296 bytes, `307cd8d8...0196`), copied off the base images
  and disassembled; addresses below are image VAs at base `0x10000`.

### Readings, Windows ME

| Run | Build | Cycles before the stop | Last PnP the PDOs saw |
|---|---|---|---|
| b | gated `qemu` | 17 | a hub-port PDO's START refused (device gone), then STOP, then the hub's relations without it, then nothing |
| c2 | `qemu` + START of a gone PDO answered success (experiment) | 4 | START succeeded, then STOP at once, then the relations without it, then nothing |
| d2 | ungated `qemu`, unmodified | 10 | START succeeded with the device still present, then STOP, then the relations without it, then nothing |

All three: the tray clock frozen at the series' last minute, no recovery, the
processor in the VMM's idle loop (`c00138c1`/`c00138c2` most samples), as 43.13
saw. The controller thread was still alive (a mouse removed afterwards was
seen: its transfers cancelled, the counters emitted), but no PnP IRP ever
reached any of the driver's devices again - neither the root hub nor the
hub asked for relations after later plugs and removals.

**Where it stops (runtime, memory read, runs c2 and d2).** The PDO the
relations had just omitted had a `hidusb.sys` FDO above it whose
minidriver extension read state 5 (removing), pending-request count 0,
and a waiter on its removal event; `hidclass.sys`'s state for the same FDO
read 6 (stopped). Every other HID stack in memory read state 2 (started).
So the REMOVE that followed the relations answer was blocked inside
`hidusb.sys`, before it was passed down to this driver, and the
configuration manager, which runs one device at a time in the system VM,
waited on it for good.

**Why (static, ME's `HIDUSB.SYS`).** `hidusb.sys` counts its requests from
0, and its removal event (a notification event, not signalled) is set when
the count goes below 0 (`0x10b00`). Its START (`0x11df9`): state 1, the
START passed down and waited for; on failure below, or of its own
descriptor and configuration requests after (`0x105b0`), state 6. Its STOP
calls the stop routine (state 3, the count decremented, the event waited
for, `0x12090`) **only when the state is 2** (`0x11e2d`), and sets state 4
after the STOP returns from below (`0x11fa6`). Its REMOVE (`0x12200`):
state 5, the count decremented only when the previous state was neither 3
nor 4, then the event waited for, then the REMOVE passed down. So a START
that failed (state 6), then a STOP (skipped, then state 4), then a REMOVE
(no decrement) waits on an event nothing will ever set. Windows ME sends
exactly that STOP after a failed START (runs b, c2, d2). This is a defect of ME's `hidusb.sys`, reached
whenever a USB HID device's start fails and its device then goes; it does
not depend on how the start failed:

- b: this driver refused the START of a PDO whose device had left
  (`hcd_pdo.c`, `hcdPdoStarted`).
- c2: with that START answered success instead (an experiment, not kept),
  `hidusb.sys`'s own descriptor requests to the departed device failed, and
  the same STOP followed.
- d2: the device left while `hidusb.sys`'s start requests were under way.

So the refusal is not the cause, and answering such a START success does
not cure it. The 2026-10-04 ME reading in `hcd_pdo.c`'s lifecycle note ("a
device pulled while its install was under way was started (refused),
stopped and left ... the shell wedged") reads as the same stop: the REMOVE
that "never came" was held in `hidusb.sys`.

**98 SE's `hidusb.sys` (static) has no such gate**: its STOP calls the stop
routine unconditionally (`0x1179b`), so the count reaches -1 and a later
REMOVE finds the event set. Its REMOVE logic is otherwise the same. And 98
SE's configuration manager, read at runtime, answers a failed START with
REMOVE, not STOP (98 SE run b: START refused, then REMOVE, and the series
went on).

### Readings, Windows 98 SE

98 SE did not stop. Two runs (a2: about 45 cycles; b: 60 cycles, with QEMU's
own `usb_xhci_*` trace) showed a different failure instead:

- The bus, not the system, failed: in a2 the controller was reset three
  times inside ten minutes, the fourth request was refused by task 35-T.6's
  window (`ctrl.window.refused`; the snapshot's verdict "LATCHED FAILED:
  the recovery window refused a fourth recovery"), and no device was seen
  after that. Windows stayed responsive throughout.
- Each reset followed a request to a device that had just been removed
  (a2: the SET_CONFIGURATION of a select; b: the first EP0 request after
  Address Device behind the hub) that never completed within
  5 s, which the driver treats as the controller's failure (`hcdCommand`'s
  `HCD_COMMAND_WAIT_MS` for a command; in b QEMU's trace shows no command
  after the Address Device, so it was the EP0 request's own wait).
- QEMU's trace (b) shows why it never completed: QEMU's `xhci_kick_epctx`
  returns without completing anything when the slot's device has been
  detached (`hcd-xhci.c`, `xhci_slot_ok`, "we must NOT continue"), so a
  transfer queued after the detach gets no event at all. Real controllers
  end such a transfer with an error, so this path is in part an emulator
  artefact; the driver's answer to it, a whole-controller reset, then spends
  the recovery window.

### The control: Windows ME on its own USB stack (2026-10-22)

The stock ME image (no `xhci98.sys`), a QEMU `piix3-usb-uhci` controller,
ME's own USB stack with the same 4.90 `HIDUSB.SYS`, one virtual CPU,
256 MB, the host still saturated. The same series: a hub at port 2, an
MS-DOS Prompt open, `usb-mouse` hot-plugs alternating port 1 and hub port
2.1, each removed as soon as the pointer moved, no gap. A `usb-mouse` was
first checked to move the pointer under ME's own stack.

- Series a: 80 cycles (13:41 to 13:47), no timeout. Series b: 80 cycles
  (13:53 to 14:01), plug-to-pointer 0.78 to 26.57 s, mean 5.57 s, no
  timeout. The tray clock advanced after each (75 s and 65 s checks).
- So ME's own stack did not stop in 160 cycles, where `xhci98.sys` stopped
  ME within 4 to 17. The defect is in ME's `hidusb.sys`, and any stack can
  reach it in principle (a START that fails, a STOP, then the REMOVE), but
  this series did not reach it on ME's own stack. Under `xhci98.sys`, ME's
  configuration manager reached a device's START many seconds after its
  PDO was reported, when at all (run b: 4 STARTs in 17 cycles, the rest
  removed before their START), so far more starts meet a device already
  gone. Why the configuration manager is that much slower here was not
  read; the log shows no request of its waiting on this driver.

Note, 2026-10-22 (task 43.54, `runs/run-43.md`): this conclusion does
not hold. With each mouse pulled a fixed 1.0 s after its plug, this driver
had the device addressed and started sooner than ME's own stack (medians
0.16 and 0.30 s against 0.58 and 0.66 s), and ME's own stack stopped the
same way, at cycles 58 and 8, with the same `hidusb.sys` state in memory.
The 160 clean cycles above came from the series' pointer detector, which
held most devices on ME's own stack past their START and, under this
driver, often fired before any START could come.

### Candidate fixes (none made)

No fix is clear and small, so none was committed. In the order the owner
may want to weigh them:

1. **ME: let `hidusb.sys`'s start succeed for a device that leaves before
   its stack has started** - answer the device and configuration
   descriptors from the PDO's own copies and a SELECT_CONFIGURATION with
   pipe handles that hold every later request until the REMOVE (the hold
   and park of design record 13 section 10.5), on Windows 9x only. Then
   STOP and REMOVE find state 2 and take the counted path. It changes the
   PDO lifecycle and the select path (record 13 sections 5.2 and 10.5); it
   needs its own design note and review.
2. **ME: never report missing a PDO whose START failed and was then
   stopped**, so ME never sends the REMOVE that blocks. That leaves a dead
   devnode until restart, needs the same device's return mapped onto it (an
   instance id may not be presented twice), and only moves the stop to the
   parent's removal (a hub pulled, a controller disabled). Not recommended.
3. **Record it as a Windows ME defect** in the release notes (rapid replug
   of a HID device on ME can stop the system; `hidusb.sys` 4.90) and keep
   the driver as it is.
4. **98 SE (and every target): a request to a departed device that never
   completes need not reset the controller** - before requesting the reset,
   check whether the slot's device has left (its root port or hub port
   reads disconnected, or a departure is pending) and complete the request
   DEVICE_GONE instead, as `hcdCfgFault` already does for a failed command
   on a departing device. Its trigger here is QEMU's; whether metal can
   reach it is unread.

Recommendation, revised after the control: 3 now, worded from the control
(the defect is ME's, but this driver reaches it where ME's own stack did
not in 160 cycles); 1 as a later task, now weighed higher; 4 as its own
task, read on metal before it is changed.

Owner, 2026-10-22: 3 taken (the release notes' "Known limitations"), 4
opened as task 43.53 with 1 noted there as the later option for ME, and
43.49 closed on this reading.

### Owed

- Why ME's configuration manager starts this driver's PDOs so much later
  than its own stack's (not read; 43.53's ME line). Read by 43.54
  (2026-10-22): it does not; see the note above.

## 43.52 two XHCITR defects from 43.45's legs: the code (2026-10-22)

43.45's legs on 7 x86 (2026-10-22, branch `p43-45`, its run sheet
`run-43.md`, its two XHCITR sections) found two XHCITR defects. This
section records their code on branch `xhcitr-fixes`. No virtual machine
was run for it; the legs are owed (below).

### The unelevated eject's tunnel line

Read from the code. Without administrator rights `ej_run`
(`xhcitr\xhcitr.c`) does not confirm its own eject (`ej_mark_confirm`
only when `ej_is_admin()`); the mark service does, at a pass that finds
the unit held for eject (problem 47), and `mk_confirm`
(`xhcimark\xhcimark.c`) writes `XhciCmEjectConfirm` under the tools'
eject mutex, `Global\XhciinstTbAsk`, waiting 1 s for it. XHCITR held
that mutex through its 10 s `ej_watch_tunnel` and the assessment after
it, so the service's confirmation could only follow the tool's answer:
43.45 read `cm.reenum.confirm` 12 to 18 s after OK. That also explains
why waiting longer alone would not have helped.

The change: `tr_eject_watch_kind` (`xhcitr\tr_core.c`) decides the
watch. An administrator's run, and the not-removable route, keep the
10 s watch and its line. A run without administrator rights on XP and
later, the service not read as stopped or absent, releases the mutex
before the watch and waits 10 s plus twice the service's idle period
(`mk_poll_ms(0)`, 10 s; `test_tr` checks the two agree), 30 s, a budget
rather than a bound the service promises; the watch ends at the
installer's quit event, as 30 s outlasts the installer's 15 s wait for
the tool to end. A tunnel still up then reads "The PCIe tunnel has not
been seen closed yet. It closes if the xHCI98 mark service confirms the
eject to the Thunderbolt driver, which the service does in the
background; an eject made as an administrator is confirmed by this tool
itself." Without administrator rights on 2000 (problem 24 is no
confirmation), or with the service stopped or absent: 10 s, then "The
PCIe tunnel stays up until the device is unplugged, because the eject
was not confirmed to the Thunderbolt driver; an eject made as an
administrator is confirmed by this tool itself." `XHCIINST` always runs
as an administrator and is unchanged.

### The balloon after a sleep

`tr_diff` raised "The Thunderbolt/USB4 controller stopped answering"
whenever an NHI's liveness left ACTIVE; `CmLiveDisarm` sets OFF ("not
monitored") at every sleep and stop. It is now raised only for ACTIVE
to a loss state (`tr_live_lost`: suspect, absent, configuration lost,
recovering, recovery failed, contained, preparing), or for the record's
stage going from active to failed, which a resume that fails leaves
with the liveness OFF (`cm_power.c`); once an NHI either way. OFF,
revoked and a value the tool does not know raise nothing. `test_tr`:
ACTIVE to OFF to ACTIVE raises nothing; ACTIVE to each loss state, and
the failed stage, raise it once.

### Builds and review

Host suites pass (`test_tr` 1002 checks). The gated builds, x86 `all`
and amd64 `all`, pass at `38fc1f45`. Reviewed and revised once; the
second reading found nothing. Commits: `650f5fc6` (the eject),
`cded3b9b` (the balloon), `38fc1f45` (the revision).

### Owed

The legs, on 7 x86 in the emulator under the USB4 profile with the
ASM2464 router, as 43.45 ran them:

- an eject from XHCITR started without administrator rights: the tunnel
  read closed within the watch (`cm.reenum.confirm` shortly after OK),
  or the new line; and the same eject as an administrator, unchanged;
- a sleep or hibernate and resume with XHCITR in the tray: no balloon;
- where the vehicle allows, a resume that fails: the balloon.

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

## 43.54 - Does Windows ME Start This Driver's Devices Late?
Roadmap-usb4 task 43.54, opened by the owner on 2026-10-22 ("start ME
investigation") from task 43.49's control (`runs/run-43.md` on branch
`p43-49`): ME's own USB stack had run 160 cycles of 43.49's plug and
unplug series without stopping. 43.49 put that down to ME's configuration
manager starting `xhci98.sys`'s device PDOs late, so that more starts ran
into a device already gone. This run measured plug-to-START on both
stacks. Development host A, QEMU under TCG, one virtual CPU, 256 MB,
headless; the host was loaded by other agents' guests throughout, so the
absolute times below are slow, and each comparison was run close in time
to the other.

### Verdict

**ME does not start this driver's devices late.** In a series that pulls
every mouse a fixed time after its plug, `xhci98.sys` had the device
addressed and the class driver's START selecting its configuration in
about half the time ME's own stack took (medians 0.16 and 0.30 s, against
0.58 and 0.66 s). **ME's own stack stopped too** in that series, at
cycles 58 and 8, with the same `hidusb.sys` state read from the guest's
memory as 43.49's stop. 43.49's 160 clean cycles came from how that
series decided when to pull, not from either stack. Nothing on this
driver's side needs a fix for this task. The release notes' known
limitation needs rewording (below).

### Method

- Guests: ME + `3.1.0.0` with the `qemu` flavour of `2f46080a` copied over
  `xhci98.sys` (`qemu-xhci`, `p2=4,p3=4`; a mouse at root port 1, a stick
  at 2, a hub at 4, an MS-DOS Prompt open), as 43.49. Registry as 43.49:
  `XhciInterruptMode` 2, `XhciPollIdleMs` 50, `XhciPollFineTimer` 0,
  `XhciLogVerbosity` 2. Two binaries: the gated build, and an ungated
  rebuild of the same source with trace lines added (each PnP IRP's PDO,
  each URB but interrupt transfers, each request held for a departed
  device and its cancel, each deferred refusal). Not kept.
- The control: stock ME (no `xhci98.sys`) on a QEMU `piix3-usb-uhci`,
  ME's own USB stack, a hub at port 2 and an MS-DOS Prompt open, as 43.49's
  control. Run c on a fresh overlay of the stock image.
- The timings: QEMU's own trace (`-trace usb_set_addr`, `usb_set_config`,
  host-stamped). `usb_set_addr` is the stack addressing the new device;
  the first `usb_set_config` after the plug is `hidusb.sys`'s START
  selecting the configuration. The same two events on both stacks. On
  `xhci98.sys` also the driver's debugcon trace, host-stamped as read.
- **The fixed series**: `usb-mouse` hot-plugged alternately at a root port
  and at a hub port (`xhci98.sys`: 3 and 4.1; UHCI: 1 and 2.1), each pulled
  exactly 1.0 s after its plug (`device_del`), the next plugged as soon as
  QEMU had dropped it; no pointer detector. After four cycles in a row
  without a START, the tray clock was checked across 75 s, and a frozen
  clock taken as a stop. At a stop the guest's memory was saved and read
  with 43.49's parser (`devs.py`).
- **43.49's series**, for the comparison: the same plugs, each pulled when
  a screen detector saw the pointer move (`plug.py`'s detector), no gap.
- **Spaced plugs**: the same plugs 6 s apart, each held until 3 s after
  its START.

### Readings

#### The fixed series (pull at 1.0 s)

| Stack | Run | Cycles | Addressed by 1.0 s (median) | START by 1.0 s (median) | Stop |
|---|---|---|---|---|---|
| `xhci98.sys`, gated `qemu` | b2 | 13 | 10 (0.17 s) | 3 (0.21 s) | at cycle 13 |
| `xhci98.sys`, `qemu` + trace lines | c | 60 | 53 (0.16 s) | 42 (0.30 s) | none in 60 |
| ME's own (UHCI) | b | 58 | 21 (0.58 s) | 18 (0.66 s) | at cycle 58 |
| ME's own (UHCI), fresh overlay | c | 8 | 3 (0.66 s) | 2 (0.99 s) | at cycle 8 |

Each of the three stops: the tray clock still across 75 s and 90 s,
typed text never echoed, the processor in the VMM's idle loop
(`c00138c1`/`c00138c2`, as 43.49's), and in the saved memory a
`hidusb.sys` minidriver extension in state 5 with its pending count 0 and
a waiter on its remove event - 43.49's signature: a STOP after a failed
START, then a REMOVE waiting for ever (b2: `c15aac3c`; UHCI b: `c145d4ec`;
UHCI c: `c153d56c`). On ME's own stack the START that preceded run b's
stop came 0.95 s after the plug, 0.05 s before the pull.

#### Spaced plugs

| Stack | START after the plug, once the device was installed |
|---|---|
| `xhci98.sys` (gated, run a, cycles 5 to 12) | 0.21 to 0.32 s |
| ME's own (UHCI, run a, cycles 6 to 12) | 0.42 to 0.50 s |

The first plugs on either stack took 4 to 14 s (ME installing the
device); the rows leave those out.

#### 43.49's series

On `xhci98.sys` (trace build, 40 cycles, no stop; the gated build's run
of the same series stopped answering at cycle 16, its clock check not
read), 13 of 40 devices were pulled before their
START, 9 of them 0.01 to 0.24 s after the plug - before this driver had
reset the port, which it does 0.16 to 0.25 s after the plug. The detector
saw a screen change that was not the new mouse's pointer, so these pulls
were the series', not the stack's. 43.49's UHCI control, run with the
same detector, had a plug-to-pointer of 0.78 to 26.57 s (mean 5.57 s): it
held most devices well past their START, and a START that has finished
before the pull does not take `hidusb.sys`'s failing path. That, not a
late START under `xhci98.sys`, is why that control did not stop in 160
cycles.

#### Where the configuration manager waits

The trace build shows ME sometimes saying nothing for 4 to 7 s after a
device leaves - no PnP request to any of this driver's devices - and then
asking the gone device's relations, cancelling its two held interrupt
reads, and sending its REMOVE (in 43.49's series 12 of 44 such answers
came more than 2 s after a plug; in the fixed series 2 of 48). The driver
had already reported the change; nothing of ME's waited on it. ME's own
stack cannot be traced at that level here, but it addressed only 21 of 58
devices within the second, against 53 of 60, which an idle configuration
manager does not explain either.

One effect of those waits is this driver's own: a port's next device is
enumerated only once ME has removed the previous device's PDO there, so
during such a wait the next device at that port is not addressed (all 7
of the 60 fixed-series cycles that were not addressed came 1.6 to 4.5 s
into such a wait). It lengthens no START that ME would otherwise have sent.

### Candidate changes

None to this driver: the START is not late, and the stop reaches ME's own
stack in the same series. 43.49's candidate 1 (let `hidusb.sys`'s start
succeed for a device that leaves first, on 9x) remains the only way to
make ME stop less often than its own stack does. It is no longer
justified by this driver reaching the defect more readily, so it weighs
less than 43.49 put it.

### The release notes

The known limitation 43.49 added (`docs/using/release-notes.md`, on branch
`p43-49`, not yet on `3.2.0.0`) says ME's own stack ran 160 cycles
without stopping and that this driver "reaches it far more readily".
These readings contradict both statements. Proposed replacement for its
last three sentences, to apply when `p43-49` is merged (owner, 2026-10-22:
accepted; applied after the merge, worded as below with the defect named as
any stack's and this driver's starts no later than ME's own stack's):

> It was seen in a virtual machine after 4 to 17 back-to-back plug and
> unplug cycles; ME's own USB stack, on a UHCI controller in the same
> virtual machine, stopped the same way after 8 and 58 cycles of a
> series that pulled each mouse one second after its plug. Plugs a few
> seconds apart, each left until the device worked, did not reach it.

### Owner, 2026-10-22

All three accepted: 43.54 closed with no driver change, the release notes'
known limitation reworded from these readings, and 43.53's ME option (43.49's
candidate 1) lowered to a low-priority later idea. `runs/run-43.md`'s 43.49
section carries a dated note pointing here.

### Owed

- Nothing for this driver. What ME's configuration manager does during
  its 4 to 7 s waits was not read (it happens on its own stack as well,
  and is ME's).

## 43.22 Windows 8 and 8.1, a target of the connection manager: what NT 6.2 and 6.3 need (2026-10-22)

Branch `p43-22`, from `3.2.0.0` at `46e7d2af`. The roadmap entry lists what
an install on NT 6.2 and 6.3 needs that 42-A.21 did not read; each item
below says what was read and how. The install, update and uninstall legs,
the emulator's tunnel legs and the NVMe legs are 43.24's; this task read
the install alone, one per system, with the published `3.1.0.0` package
(the installer as 42-A.21 coded it; `release-x64\xhcicm.sys` SHA-256
`b643972abf8f940c2fd5f07ecb1b55871a8a5e5f78433a210f718e74f2444de0`, an
embedded test signature, security directory 1,496 bytes; `release-x86`'s
`d7202c39e01450e051b65496ad66d7e94e6a537c8a601729ae0971a4465f602b`,
unsigned).

### The vehicle

Development host A, QEMU, headless, TCG: the usb4-emu QEMU
(`vm\usb4\bin-2a893ea`, SHA-256 `6a1dfcc8...bd66`, the hash every kit
checks), `-machine pc` (SeaBIOS: no Secure Boot), `-cpu
qemu64,+3dnowprefetch`, smp 2, a `qemu-xhci` with a tablet, and the
emulator's `tb3-alpine-ridge` NHI (`8086:15BF`) with its tunnel bridge
(`1B36:0001`) and xHCI - the `p43-33` kit's topology. Overlays of the
read-only pristine images of the `win8-10` set: Windows 8 Pro x86
(6.2.9200), Windows 8 Pro x64 (6.2.9200) and Windows 8.1 Pro x64
(6.3.9600, with Update); test-signing mode on in each, as built. **No
Windows 8.1 x86 image exists** (no medium; owner, 2026-10-22: 8.1 32-bit
is skipped for `3.2.0.0`, recorded as untested ground, covered by 8 x86
and 8.1 x64). A guest script (`read.ps1`, kit `E:\xhci98-vm\p43-22`) read
`SystemStartOptions`, `sc qc`, the PCI devices' problem codes and
drivers, the `UpperFilters`, and `setupapi.dev.log` onto a second disk,
read after power-off with 7-Zip; the guest ran `INSTALL /console /cmonly
/yes`, elevated, from a VVFAT drive. The host was saturated (another
agent's builds and a dozen guests): nothing here is a timing. Kit ports
48620 to 48622.

### Results

| System | `INSTALL /console /cmonly /yes` in test-signing mode | On a start with signature enforcement disabled | After |
|---|---|---|---|
| 8 x86 | **installs**: two "Windows can't verify the publisher" prompts (`xhcicm.inf`, `xhcicmf.inf`), answered; `Cm.Dev6.NTx86` and `CmF.Dev6.NTx86`; NHI on `xhcicm`, started; bridge filtered, started; `xhcimark` running; exit 3 | - | `xhcicm` `BOOT_START` ("Updated service 'xhcicm' to be boot start"); with test-signing off and on again, each start reached the desktop with the NHI and the bridge at problem 0 and `xhcicm` running |
| 8 x64 | **refused**: "xhcicm.inf could not be staged (error 0xE000022F)"; `setupapi.dev.log`: "Driver package does not contain a catalog file, and Code Integrity is in Test Signing mode" / "The third-party INF does not contain digital signature information"; "No changes were made", exit 2 | installs as on 8 x86 (the two prompts; "Driver package does not contain a catalog file, but user wants to install anyway"); `Cm.Dev6.NTamd64`, `CmF.Dev6.NTamd64`; NHI and bridge started, exit 3 | `xhcicm` `BOOT_START`; the next start, test-signing mode off, stopped in "Attempting repairs" (Automatic Repair) and stayed there through two launches, over an hour |
| 8.1 x64 | **refused**, the same error and log lines; the local copy made, exit 2 | installs as on 8 x64, exit 3 | `xhcicm` `BOOT_START`; a plain start in test-signing mode (`SystemStartOptions` `TESTSIGNING NOEXECUTE=OPTIN`) reached the desktop with the NHI on `xhcicm`, started, and the bridge filtered: the test-signed SHA-1 binary loads in test-signing mode |

`SystemStartOptions` on a start with enforcement disabled read
`TESTSIGNING NOEXECUTE=OPTIN DISABLE_INTEGRITY_CHECKS` (8 x64, 8.1 x64);
in test-signing mode `TESTSIGNING NOEXECUTE=OPTIN`; with it off
`NOEXECUTE=OPTIN`. `HKLM\SYSTEM\CurrentControlSet\Control\SecureBoot`
holds `AvailableUpdates` alone and `Confirm-SecureBootUEFI` answers "Cmdlet
not supported on this platform" on these SeaBIOS guests. The installer
named the systems "Windows 8" and "Windows 8.1" (`RtlGetVersion`), offered
the connection manager alone and left Stor98 out, as 42-A.21 coded; its
x64 note said "Test-signing mode is on: the test-signed x64 connection
manager loads." and the staging then failed. The console tool waits for
Enter at its end even with `/yes` and its output redirected; a script feeds
it `< NUL`.

### What the reading settles, item by item

- **The INFs' decorations**: right as they are. 8 and 8.1 take
  `NTx86.6.0` / `NTamd64.6.0` and the `Dev6` install sections, as the host
  vectors of `inst_models_section` say; `Signature="$CHICAGO$"` and
  `$Windows NT$` both accepted. No INF change.
- **The x64 test signature with Secure Boot off and test-signing on**: the
  binary loads (8.1 x64, a plain start; 8 x64 in the start with
  enforcement disabled), but the package does not install in test-signing
  mode: Windows 8 and 8.1 x64 refuse an INF with no catalog there. Record
  19's premise that a catalog governs only the prompt holds for Vista and
  7 and not for 8 and 8.1.
- **Boot start**: Windows 8 and 8.1 make `xhcicm` boot start through the
  bridge filter (32-bit too). On x64 that turns test-signing mode from
  "needed for the driver to load" into "needed for Windows to start". Read
  on 8 x64 only, with a hung Automatic Repair; the cause is inferred from
  the boot-start change and the test signature, not read from a boot log.
  64-bit 8.1 and 32-bit 8 read `BOOT_START` too.
- **32-bit 8 and 8.1 with Secure Boot on**: Microsoft's "Driver Signing
  Policy" page ("Signing requirements by version"; `ms.date` 2024-08-19,
  read 2026-10-22) lists "64-bit, 32-bit" for "Windows 8, Windows 8.1,
  Windows 10, versions 1507, 1511 with Secure Boot on", against "64-bit
  only, no signature required for 32-bit" for "Windows Vista, Windows 7;
  Windows 8+ with Secure Boot off". It does: so the note of record 23
  section 5.10 covers the x86 package there (owner, 2026-10-20). With
  Secure Boot off (SeaBIOS) 32-bit 8 loaded the unsigned driver with
  test-signing mode on and off. Secure Boot on is unread: no guest here
  has it.
- **The installer's 8 and 8.1 path as 42-A.21 coded it**: version, name,
  the connection manager alone, Stor98 left out: as designed. Wrong: on
  8 and 8.1 x64 it went on in test-signing mode into a staging Windows
  refuses, said nothing of why, and its note promised the driver would
  load; and it would install outside test-signing mode with a note, which
  on 8 and 8.1 x64 leaves a machine that does not start. Fixed, commit
  `6c9e49f1` and the commit that closes this task (record 23 section
  5.10, revision 30; numbered 30 because `p43-34` and `p43-27` take 27
  and `integ` and `p43-42` take 28 and 29): `inst_cm_boot_gate` refuses the connection manager on 8 and 8.1 x64 unless the package is
  test-signed and test-signing mode is confirmed, and warns when it goes
  on; `inst_stage_error_text` explains `0xE000022F` there and names the
  start without enforcement; on 32-bit 8 and 8.1 `inst_sig_note_kind`
  prints the Secure Boot note, and a Code 52 there is reported as that
  refusal. Host vectors in `test_inst.c`. The three decisions key on the
  kind `inst_os_kind` gives (`INST_OS_CMONLY`), not on 6.2 and 6.3, and
  their texts say "this Windows", so Windows 10 below build 22000, which
  `p43-34` (task 43.34) admits to that kind, takes them on the merge;
  whether 10 makes `xhcicm` boot start and refuses the catalog-less
  package in test-signing mode is 43.34's to read. The note that told 8
  and 8.1 x64 users that test-signing mode needs Secure Boot off, after
  the test-signing note, is gone: the gate's texts say it on those
  systems, and the branch is dead code otherwise (`p43-34` edits the same
  lines for 10: on the merge, take the removal).
- **8.1 with Intel's own Thunderbolt software**: not read in a guest (no
  image of it at hand; 43.24's leg, "where an image of it is at hand").
  Read from the code: a controller another driver holds is foreign and
  left (decision D3), so the NHI keeps Intel's driver; the filter still
  goes onto the bridges, where it does nothing without this driver on the
  NHI (record 21 section 3.2) - but on 8.1 x64 it makes `xhcicm` boot
  start all the same, so the gate applies. Intel's release notes for its
  Thunderbolt software 16.3.61.275 (as reposted on a vendor forum, not
  read on Intel's site) list Windows 7 32- and 64-bit, 8.1 64-bit and 10
  64-bit, Falcon Ridge and Alpine Ridge, and call it the last package for
  7 and 8.1; no 8 and no 8.1 32-bit.
- **README and the release notes**: moved from "never run" to what was
  read here (install alone), the legs still 43.24's; 8.1 32-bit in
  "Untested ground".

### Builds

| Build | Result |
|---|---|
| `xhciinst\build.cmd`, `6c9e49f1` | passed |
| host suites (`test\run-host-tests.cmd`), `6c9e49f1` | 0 failures (`test_inst` 1790 checks) |
| host suites, the closing commit (the gates keyed on the kind, Secure Boot's state) | 0 failures (`test_inst` 1798 checks) |
| host suites after merging `3.2.0.0` at `a48e8688` (Windows 10 below build 22000 now `INST_OS_CMONLY`; vectors added for 10 and 11 through every gate) | 0 failures (`test_inst` 1821 checks) |
| `xhciinst\build.cmd`, `build-driver.cmd all`, `all -amd64` after that merge | passed (`BUILD + GATES PASSED (debug release qemu)` for x86 and amd64) |
| `xhciinst\build.cmd`, `build-driver.cmd all`, `all -amd64`, `544cfcd4` | passed (`BUILD + GATES PASSED (debug release qemu)` for x86 and amd64; a first run failed the self-test's "every screen within 79 columns" on an 80-column line of the x86 Secure Boot note, rewrapped before the commit) |

### Not read

8.1 32-bit (no medium). Secure Boot on, either architecture (no UEFI
guest). The way back from Automatic Repair on 8 x64 by a start with
enforcement disabled: the guest stayed at "Attempting repairs" through
two launches (13:51 to 15:04 on the second), never reaching the menu, and
was closed; the warning's way back rests on Microsoft's documented
Startup Settings, not on a reading. Whether 7 x64 also makes `xhcicm`
boot start (the same filter; 42-A.21's 7 leg was 32-bit, and no x64 leg
has read `sc qc xhcicm`): owed, and if it does, 7 x64 has the same hazard
and the gate should cover it. Intel's own Thunderbolt software on 8.1 (no image). The update and
uninstall on 8 and 8.1, the
tunnels, eject and NVMe (43.24). The new installer in a guest: its
refusal, warning and notes rest on the host vectors and the self-test's
screens (a package with an x64 half needs the signing key, which this
session did not have).

### Secure Boot's state (owner, 2026-10-22)

The owner accepted `p43-34`'s design (record 26 section 5.3.2): the
installer reads `UEFISecureBootEnabled` (on, off, not known) and shows it
in the signature note on 8, 8.1 and 10, never changing it; with Secure
Boot on, test-signing mode never counts as confirmed and the user is told
to turn Secure Boot off first. Coded here (record 23 section 5.10,
revision 30): `inst_sb_state`, `inst_sb_applies`, `inst_sb_text`,
`inst_ts_confirmed`, keyed on the connection-manager-alone kind so 10
takes them; on 32-bit 8 and 8.1 a known state replaces the Secure Boot
note by its case. Host vectors for the three states on each system and
through the gate. The value is read on no guest here (SeaBIOS only).

The owner's answers the same day: Intel's own Thunderbolt driver on a
controller is another vendor's device and is left alone, on 8.1 and 10
alike (as the code does: the NHI is foreign and left); the per-system
"untested" warning is 43.37's.

### Owner questions

1. A signed catalog for the x64 package's INFs (record 19 decided against
   one): with a test-signed catalog, 8 and 8.1 x64 would install in
   test-signing mode with no start without enforcement. Recommendation:
   yes for `xhcicm.inf` and `xhcicmf.inf` at least, signed with the same
   test key the packaging scripts sign the x64 binaries with (no script
   here makes a catalog yet); Vista and 7 accept a catalog-less package
   now, so they lose nothing. It does not cure item 2 of record 23's
   revision 30: the boot driver still needs test-signing mode. Owner,
   2026-10-22: yes, a catalog for every x64 INF (43.36 below; record 23
   revision 33).
2. Whether 7 x64 also makes `xhcicm` boot start through the filter: if it
   does, 7 x64 on starts with enforcement disabled (F8 at every start) is
   the same hazard, and the gate should cover it. Recommendation: read it
   in 43.24's or 43.31's 7 x64 leg (`sc qc xhcicm` after the install) and
   widen the gate to Vista and 7 x64 if so. Owner, 2026-10-22: read in
   43.24's or 43.31's 7 x64 leg.
3. On a controller another driver holds (Intel's software, left alone
   as the owner decided), the filter still goes onto its bridges, which
   run Windows' own `pci`, where it does nothing, yet on 8 and 8.1 x64 it
   makes the system depend on test-signing mode. Recommendation: leave
   the filter out too where no NHI in the run is this driver's (a small
   installer change, host-testable), as a later task. Owner, 2026-10-22:
   a new task, 43.58, not this one.

## 43.36 the x64 catalog, read on Windows 10 and 8.1 x64 (2026-10-22)

The owner's decision of 2026-10-22 on 43.33's refusal (record 13 section
6.14.5): every x64 INF names a catalog of its own, made and signed by
`make-package.ps1` (design record 19 section 3a; branch `p43-36s`).
Development host A, QEMU under TCG, two virtual processors, 2048 MB,
headless, one guest at a time; the host loaded by other agents' guests
and builds throughout. Kit `E:\xhci98-vm\p43-36s` (ports 48800 to 48802),
guest scripts `xfer\auto\go.cmd` and `leg.ps1`.

**Verdict: with the catalogs, Windows 10 22H2 x64 and Windows 8.1 x64
in test-signing mode take every one of the five x64 INFs into the driver
store, after "Windows can't verify the publisher", and `xhci98.sys` and
`xhcicm.sys` run after the restart; the same package with its catalogs
removed is refused with `0xE000022F`, no prompt.**

### The package

The `release` amd64 package of this branch (gated build of `caa79889`,
the drivers' sources as on `3.2.0.0` at `2038250e`), staged by
`make-package.ps1 -Arch amd64` without the key, so the binaries were
unsigned; the five catalogs made by the packager's own
`New-PackageCatalogFile` (MakeCat, the CDF the signed path writes) and
left unsigned (`E:\xhci98-scratch\p43-36s\stage-guest.ps1`). Each read
back by `Read-CatalogFile` with its two members of the right types:
`xhci98.cat` (`xhci98.inf`, `xhci98.sys`), `xhciuas.cat`, `xhcicm.cat`,
`xhcicmf.cat` (`xhcicmf.inf`, `xhcicm.sys`) and `stor98.cat`.

**The project's key was not used**: its password is the owner's and no
agent holds it. In each guest a throwaway code-signing certificate was
made in the guest's own store (`CN=p43-36s guest throwaway (not
trusted)`, SHA-1, 2048 bits: `New-SelfSignedCertificate` on 10, WDK 7.1's
`MakeCert` on 8.1, which has no `-Type`), and the four `.sys` files and
the five `.cat` files were signed with it there (`Set-AuthenticodeSignature`
on 10, WDK 7.1's `SignTool /fd sha1` on 8.1). Test-signing mode accepts a
signature by any certificate, and the project's is trusted nowhere
either, so the reading stands for it by inference; the leg with the
owner-signed package is owed (below). Nothing was signed or installed on
the host.

### The legs (measured)

| | Windows 10 22H2 x64 (19045) | Windows 8.1 x64 (9600) |
|---|---|---|
| Starting point | `p43-33`'s prepared `w10x64` (copied), test-signing on | the pristine image plus the runner task, test-signing on (`bcdedit`: `testsigning Yes`) |
| Control: catalogs removed, `UpdateDriverForPlugAndPlayDevicesW` forced on `PCI\VEN_1B36&DEV_000D` | FALSE, `0xE000022F`, no prompt; "Driver package does not contain a catalog file, and Code Integrity is in Test Signing mode" | FALSE, `0xE000022F`, no prompt |
| `pnputil` of the five INFs with their catalogs | each after "Windows can't verify the publisher of this driver software", "Install this driver software anyway": added as `oem2.inf` to `oem6.inf`, each "Installed catalog '<name>.cat' as 'oem<n>.cat'" | the same, `oem1.inf` to `oem5.inf` |
| What the store says of each catalog | "Driver package catalog is valid", then `0x800B0109` (a root not trusted), "Driver package catalog file certificate does not belong to Trusted Root Certificates, but user wants to install anyway", "Signer Score = 0x80000000 (Unsigned)" | the same `0x800B0109` and Signer Score |
| Forced install, `xhci98.inf` on the controller | TRUE, no restart asked | TRUE |
| Forced install, `xhcicm.inf` on the NHI (`PCI\VEN_8086&DEV_15BF`, the emulator's Alpine Ridge) | TRUE | TRUE |
| After the restart | `xhci98` and `xhcicm` running; the controller, root hub and hub as `xHCI98` devnodes, HID mouse and keyboard, both Bulk-Only sticks started; the NHI "xHCI98 Thunderbolt 3 Controller" started | the same |

Not a reading of this package: QEMU's `usb-uas` at Code 10 under
`uaspstor` on both, as under Windows' own driver (record 13 section
6.14.3), so `xhciuas.sys` and `stor98.sys` were imported into the store
and never bound (no device of theirs runs here). `Win32_PnPSignedDriver`
reports the installed drivers `IsSigned False`, the analogue of Vista
and 7's "Not digitally signed" for a signer no root trusts.

Harness notes: on 10 the runner task started at logon by itself, on 8.1
it did not, and the 8.1 leg's first run, from a Run box with
Ctrl+Shift+Enter, which elevates only from 10 on, was not elevated
(every step "Access is denied"; set aside as `w81x64-cat-noelev`); the
second ran from Win+X, A. On both the first click on the publisher
prompt only activates it. The Shift key sent to wake 8.1's display
raised Sticky Keys. The output disk was not formatted on the first 8.1
run (the readings were read off the system disk).

### Owed

- The same legs with the owner-signed package (`make-package.ps1
  -TestSignPfx`, the project key): by inference identical, since the
  store reports only an untrusted root for either signer.
- Windows 8 x64 (43.22 read the refusal there; the catalog leg is not
  run); XP x64, Vista x64 and 7 x64 with a catalog'd package (the INFs
  gained one line; by inference unchanged: an untrusted signer prompts
  as an unsigned package did).
- Stor98's `DmaRemappingCompatible`: no machine here has Kernel DMA
  Protection on; the value is static (Stor98 `docs/PLAN.md`).

## 43.61 the tool readmes held to the cut's text rule on every gated build (2026-10-22)

Branch `p43-61`, from `3.2.0.0` at
`b7e58ca1`.

**Why.** `make-release.ps1`'s `Write-GeneratedText` refuses a generated
plain-text file with a line past 78 columns or a character that is not
printable ASCII (TAB allowed; `Write-AsciiFile` would turn it into `?`).
Nothing met that rule before a cut: 43.11's test cut on 2026-10-22 stopped
on one line in `xhciinst/README.TXT` (left by the 43.34 merge) and one in
`xhcitr/README.TXT` (43.52's), each having passed every gate. The two lines
are rewrapped on branch `readme-wrap`, `e547cd30`.

**What changed.** One rule, read by the cut and by the gated build:

- `scripts/package/package-common.ps1`: `Get-PlainTextFaults` (the rule and
  the cut's own messages, `$script:PlainTextColumns` = 78),
  `$script:ToolReadmeDirs` and `$script:ToolReadmeName` (the four tools
  whose `README.TXT` the cut publishes: `xhcicln`, `xhcimark`, `xhciinst`,
  `xhcitr`), `Read-ToolReadmeLines` (how the cut reads one), and, from Codex
  round 1, `Get-HistoryEmbedLines`, `Format-Wrapped` and
  `ConvertFrom-MarkdownBlocks` (how the cut renders `releases/history.md`
  into the top-level `readme.txt`), moved out of `make-release.ps1`.
- `scripts/package/make-release.ps1`: `Write-GeneratedText` calls
  `Get-PlainTextFaults`; the default tool directories come from the table;
  the four readmes are read with `Read-ToolReadmeLines`; the history with
  `Get-HistoryEmbedLines`. Behaviour and messages unchanged.
- `scripts/check-readme-text.ps1` (new): `-SelfTest` (the rule's refusals,
  the reader, the history rendering, in memory), then the four tracked
  readmes and the rendered history. Exit 1 on any fault.
- `scripts/build-driver.cmd`: `=== tool readme text ===` after the source
  charset gate, self-test first, `:readmetextfail`.
- `docs/contributing/build-and-test.md`: the script's row.

Not covered, by design: the footer the cut appends to each tool readme and
the rest of the top-level readme are `make-release.ps1`'s own template
text, met at a cut.

**Proof.** On `p43-61` at `96c44b47` (from `b7e58ca1`), the gated
`build-driver.cmd release` passed every gate before this one, then stopped
at it:

```
=== tool readme text ===
readme text self-test: OK
FAIL: xhciinst\README.TXT has 1 line(s) past 78 columns, the first being:
installer says whether it could confirm test-signing mode; it never changes it.
FAIL: xhcitr\README.TXT has 1 line(s) past 78 columns, the first being:
closed and closes if the mark service confirms the eject. On Windows 2000 ...
2 fault(s) in the readmes; make-release.ps1 would refuse the cut.

ERROR: a tool readme or the release history the release cut publishes has
...
```

The same two faults from `check-readme-text.ps1` alone (exit 1). With
`e547cd30` cherry-picked onto `96c44b47` (a scratch worktree, `d3451b2d`):
`-SelfTest` OK, then `readme text: OK (4 tool readmes and the release
history, 78 columns, printable ASCII)`, exit 0. The full gated build was not
rerun with it: the gate's success path falls through to the gates that the
failing run had already passed before it.

Batch 6 (`integ6b`, `25e7dd6a`, which carries `e547cd30`): the same rule
over its four readmes and its `releases/history.md`, read in place, finds
0 faults, and `p43-61` merges into `25e7dd6a` without conflict (`git
merge-tree`); batch 6 does not touch the functions moved to
`package-common.ps1`. So `p43-61` merges after or with `readme-wrap`, never
before it, or the gated build fails on the two lines.

Codex: round 1 found the embedded release history unchecked (fixed in
`96c44b47`); round 2 clean.
