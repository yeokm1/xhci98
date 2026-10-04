# Phase 28 Record - The seven other guests, the amd64 build, and the bench

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 28 - The Seven
Other Guests, the amd64 Build, and the Bench". The roadmap entry carries the
goal, the status, the task list and the checkpoint; this file carries what
each task did and what each reading said. Where the two disagree about a
clause, the roadmap wins.

**Drafted ahead of the readings, then filled.** This file was written on
2026-10-04, before the merged build of `p28-31-int` existed, so that the
checkpoint's readings only needed filling in; the readings taken on the
merged builds of that afternoon (`c0d8a51`, then Package A, `2f6030a`) are
filled in below. A cell reading `TBD(Package B)` is a clause still to be read
on the final merged build, Package B; a plain `TBD` is a fact no evidence has
been found for yet. Neither is a result. What is written as known below cites
the file it was read from.

**Written while the phase is open**, and kept as written rather than
rewritten in the past tense. A sentence saying something "is owed" describes
the day it was written; the roadmap's task list says how each task closed.

**On `out\...`, `vm\...` and `tools\...` paths in this file.** They say where
a reading was taken and what the file was called, on the host that ran it;
they are not files a clone has.

**How the guests were driven.** Every guest leg cited so far ran on
development host A under QEMU 11.1.0 (TCG; WHPX left the XP SP3 guest at
"Windows is starting up..." for over 30 minutes, `out\phase28\pre\STATUS.md`),
each on a fresh qcow2 overlay over a read-only base: `vm\t28pre\*-base.qcow2`
for the pre-read, `vm\t27\winxp64-base.qcow2` for XP x64. Installs and
disable/enable went through `pnpctl`, a scratch SetupDi tool run in an
elevated command prompt on the transfer drive; devices were QEMU's,
hot-plugged from the monitor (mouse on port 1, stick on port 2, `usb-hub` on
port 3 with a mouse at 3.1, a stick at 3.2 and audio at 3.3, audio on port
4). Vista x64 and Windows 7 x64 boot through Advanced Boot Options, "Disable
Driver Signature Enforcement", on every boot. The merged build's legs ran
the same way on development host A on 2026-10-04, between about 12:00 and
13:55, on overlays `vm\t28pre\<guest>-m1.qcow2`, through the kit in
`out\phase28\v1` (below, "The per-guest procedure"); the upgrade, stock
Windows 98 SE, SweetLow-only and Windows 2000 audio legs on overlays of
their own, named in their sections.

**Whose read.** The pre-read section is the pre-read subagent's reports as
written (`out\phase28\pre\<guest>\report.md`), not re-read line by line by the
coordinator or by the docs subagent that drafted this file. The same holds
for the merged-build sections: each relays its guest subagent's
`report.md` or notes as written; the docs subagent that filled them checked
the figures it quotes against those reports and did not re-read the traces.

Opened 2026-10-04 at Phase 27's close: branch `phase-28` from `2.0.0.0` at
`78ade37`. The work itself ran ahead on `p28-a1` and `p28-31-int`.

---

## 28-A.1 - the per-target deltas for NT 5.1 onward

Built on branch `p28-a1` and merged into `p28-31-int` (`5fdb9af`):

- **`39d6105`**: `USB_BUS_INTERFACE_USBDI` versions 0 to 3 answered, their
  layout asserted against design record 13 section 6.1's sizes and, on amd64,
  against the WDK's own structures; `GET_DEVICE_HANDLE`;
  `SUBMIT_IDLE_NOTIFICATION` held with a cancel routine and never called back
  (there is no selective-suspend policy - 28.3 below), flushed at stop and
  removal; `GET_TOPOLOGY_ADDRESS`; `RECORD_FAILURE`; URB functions 0x30
  `SYNC_RESET_PIPE` and 0x31 `SYNC_CLEAR_STALL` through a split reset
  (`XhciPipeResetParts`). `GET_MS_FEATURE_DESCRIPTOR`,
  `CONTROL_TRANSFER_EX` and the Windows 8 functions are refused as the XP and
  2000 `usbport.sys` refuse them. Host suite `test_xp_requests` in
  `test_pipe`.
- **`fe8480b`**, the Codex review's fixes: `SYNC_RESET_PIPE` takes its own
  path (`hcdCfgResetHost`) - a busy pipe answered `ERROR_BUSY`, then Reset
  Endpoint with TSP=1 and Set TR Dequeue, never `CLEAR_FEATURE(ENDPOINT_HALT)`
  or a recreated context; the held idle IRP counted until its completion
  returns, and drained at quiesce and deletion; the USBDI `BusContext` a slot
  in an image-lifetime table, a gone slot answering not-connected, released at
  PDO deletion (Windows 7's `usbaudio.sys` 6.1.7601.17514 never calls
  `InterfaceDereference`, a static read in the commit message, so retaining
  the PDO or failing QUERY_REMOVE would leak or pin every audio device);
  `QueryBusTimeEx` answers `STATUS_NOT_SUPPORTED`.
- **`196c3c3`**, at the integration: `GET_TOPOLOGY_ADDRESS` fills the hub
  chain from the Route String, since a device PDO's port is its location from
  Phase 27 on.

**On a guest.** Windows XP x64 SP2, the `qemu` flavour of `p28-a1` at
`fe8480b` (`xhci98.sys` 254,464 bytes, SHA-256 `a7731448...6200`): the
composite `usb-audio` device that showed Code 10 without 28-A.1 read "This
device is working properly.", the trace `hcd: USBDI interface asked,
version/size=00000040` answered, and a 20 s 440 Hz tone played in real time,
17.49 s of player position in 17.30 s of host time (`runs/run-27.md`, "28-A.2
on Windows XP x64", leg q2; notes `out\phase27\xp64\q1-notes.md`). Only the
guest's position was read; the guest has no audio path out.

**Still open for 28-A.1:**
- Windows ME under SweetLow's stack with its own `usbccgp.sys` left unused:
  the ME pre-read row read "NOT CHECKED - no composite device plugged"; the
  ME rerun on `1ed1ba6` read it PASS - the HCD split `usb-audio` itself, only
  `MI_00` devnodes appeared, and ME's `usbccgp.sys` was not used
  (`out\phase28\pre\me\report-rerun.md`, "Other clauses"). That is a reading
  on the pre-read build; the official one is the ME leg on Package B:
  TBD(Package B).
- **The unanswered QUERY_INTERFACE GUIDs: left unanswered for `2.0.0.0`.**
  Every NT guest logs `QUERY_INTERFACE not answered` once per driver load for
  GUID Data1 `496B8280` and `4747B320`, and Windows 7 once more for
  `70211B0E`; nothing failed for any of them. A static reading identified
  all three and who asks (`out\phase28\qi-guids.txt`, 2026-10-04: a
  byte-pattern search of each GUID in the Microsoft binaries under `tools\`
  and their import names, no disassembly, nothing executed - **static**):
  - `496B8280` is `GUID_BUS_INTERFACE_STANDARD`, asked of the audio function
    PDO before START by `ks.sys`'s AVStream device object on behalf of
    `usbaudio.sys` (version/size `00010020` on x86, `00010040` on amd64).
    `usbaudio.sys` imports neither `KsDeviceGetBusData` nor
    `KsDeviceSetBusData`, and neither `usbhub.sys` nor `usbccgp.sys` of any
    version carries the GUID, so on a stock stack the same query is refused
    too. A USB PDO has no configuration space or DMA adapter of its own to
    offer through it.
  - `4747B320` is `BUSID_SoftwareDeviceEnumerator` (`BUS_INTERFACE_SWENUM`),
    asked of the same PDO after START by `ks.sys` to learn whether it is a
    software-bus child; refusing it is the correct answer for a hardware PDO,
    and answering it would be wrong.
  - `70211B0E` is `GUID_PNP_LOCATION_INTERFACE`, asked by Windows 7's PnP
    manager of the UAS device PDO before START, at a device's first install
    (version/size `00010014` on x86, `00010028` on amd64; `w7x86` and `w7x64`
    traces). Refusing it leaves the device's "Location paths" property empty;
    Vista's own `usbport.sys` and `usbhub.sys` never answer it. It may be
    answered in a later release, on the root hub PDO and the device PDOs
    together.

  The trace prints each Data1 only once per driver load (a static table of
  eight in `hcdQueryInterfaceTrace`), so "once" hides the repeats.

## 28-A.2 - the amd64 build

Moved to the start of Phase 27 by the owner's decision of 2026-10-03 and
recorded there: `dff49ae` readied the amd64 HCD for a guest, and Windows XP
x64 SP2 passed the clauses on the `qemu` flavour (q1) and on the `release`
flavour (r1, `xhci98.sys` 124,928 bytes, SHA-256 `cab1db08...0111`)
(`runs/run-27.md`, "28-A.2 on Windows XP x64"). With 28-A.1 the same guest
bound and played audio (q2, above). `3c2bd27` added `xhciuas.sys` for amd64
(its own allowlist and `xhciuas-amd64.inf`), which is Phase 31's
(`runs/run-31.md`).

---

## 28-V.1 - the seven other guests

The clause: ME, XP SP3, XP x64, Vista x86 and x64, 7 x86 and x64, each the
same clauses as 26-V and 27-V, Vista and 7 at four virtual processors, Vista
x64 and Windows 7 x64 on an F8 boot. With the three Windows 98 SE and Windows
2000 legs of Phases 26 and 27 these are the ten install legs.

### The pre-read on `1ed1ba6` (not the reading)

Taken on development host A on 2026-10-04 on `p28-31-int` at `1ed1ba6`,
`qemu` flavour (packages `out\phase28\pre\pkg-1ed1ba6\x86`, `xhci98.sys`
SHA-256 `d7c5c281...68a5`, and `...\amd64`, `d7ae21fd...6a9d`; full hashes in
`SHA256SUMS.txt` beside them), and paused at about 10:20 on the
coordinator's instruction so that every guest is read once, on the merged
build. It is an early look for target-specific defects, not 28-V.1's reading.
Evidence: `out\phase28\pre\STATUS.md` and `out\phase28\pre\<guest>\report.md`,
screenshots and traces beside each report.

| Guest | Install | HID | Storage `fc` | Unplug/replug | Hub + devices behind | Audio (bind) | Root hub dis/en | Controller dis/en | Shutdown | Soak x10 |
|---|---|---|---|---|---|---|---|---|---|---|
| Windows 7 x86 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| XP SP3 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | not taken |
| XP x64 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Windows 7 x64 (F8) | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | not taken |
| ME (SweetLow) | PASS | PASS | PASS | PASS | PASS | not taken | PASS | not taken | PASS | not taken |
| Vista x86 | not taken | | | | | | | | | |
| Vista x64 | not taken | | | | | | | | | |

- **No HCD defect** on any guest taken: no bugcheck, no hang, no `not served`
  or `refused at dispatch` line, no nonzero fatal or gave-up counter.
- **Audio** is binding only, at a root port and behind the hub; no playback
  was attempted. The ME audio unplug and audio-behind-hub rows were deferred
  for the Windows 98 family's removal defect then being fixed on
  `p28-pdoleak`.
- **The Windows 7 x86 controller disable** with devices attached: Code 22,
  the root hub gone, then enable with a second No Op self-test and the
  devices back; "the 1.2.0.0 Win7 disable hang did not reproduce in QEMU"
  (`out\phase28\pre\w7x86\report.md`). The miniport's VMs did not show it
  either (release notes, "Known limitations"), so this says nothing about the
  E460; see 28.3.
- **Soak**: Windows 7 x86 and XP x64, hid, hubmouse and storage 10 cycles
  each, 30 of 30, +10 addressed per class, the settled gap 2 stable
  (`w7x86\soak2\soak-w7x86.txt`, `xp64\soak2\soak-xp64.txt`).
- **Vehicle notes, not HCD defects** (`STATUS.md`): `soak-11v.ps1` read an
  empty bus as an incomplete reply (a keep-alive keyboard on port 8 cured it)
  and had no `-Arch` (it read XP x64's counters as 32-bit; patched locally);
  the packager self-test's repository-root refusal fails on a long worktree
  path; Windows ME's warm restart wedges at its logo, so it is relaunched
  cold; XP SP3 under WHPX hung at boot. The harness changes are on branch
  `p28-v1pre` (`2527746`), merged into `p28-31-int` at `43da000`.
- ME showed one "Unknown Device" under Other devices: the rerun identified it
  as `ACPI\*PNP0103` (the HPET), present in the base image before `xhci98`
  was installed - not this driver's (`out\phase28\pre\me\report-rerun.md`).

### The per-guest procedure

Written down at the owner's request (2026-10-04) as the procedure every
28-V.1 guest leg follows, from the brief the guest subagents worked to
(`out\phase28\v1\guest-brief.md`) and the vehicle notes their reports
added. `docs/contributing/build-and-test.md` points here.

**The vehicle.** One QEMU guest per leg, on a fresh overlay over a read-only
base (`vm\t28pre\<guest>-m1.qcow2`, which `launch.ps1 -Tag m1` creates on
first use). The kit is `out\phase28\v1`:
- `launch.ps1 -Guest <g> -Tag m1 [-Smp <s>] [-Xhci 'p2=4,p3=4']` runs QEMU in
  the foreground, so it is started as a background command; each launch
  writes a new `<g>\debugcon-m1-<n>.log`. The default controller is
  `qemu-xhci,p2=8,p3=0`. A background command that runs QEMU is given a
  2-hour limit, so every launch is finished - guest shut down, monitor quit -
  inside 2 hours.
- `g.ps1 <g> m "<monitor command>"`, `t "<text>"` (typed through the guests'
  US-Dvorak keymap), `k "<sendkey names>"`, `s <name>` (a screenshot to
  `<g>\shots\<name>.png`).
- `step.ps1 <g> <step>`: the canned clause steps - `devs` (mouse on port 1,
  stick on port 2, `s`, `f <drive>`), `unplug`, `hub` (mouse and stick
  replugged at root ports, `usb-hub` on port 3 with a mouse at 3.1 and a stick
  at 3.2), `hubfc`, `audio` (`usb-audio` on port 4 and at 3.3), `audiooff`
  (the audio unplugged, then the hub pulled with everything beneath), `dh`,
  `eh`, `dc`, `ec`, `ssstor`/`ssstoroff` (`usb-storage` at a given port and
  id), `uas`/`uasoff` (`usb-uas` with a `scsi-hd` child, then `qom-set ...
  attached true`).
- Batch files on the transfer drive: `i` (installs `xhci98.inf` on
  `PCI\CC_0C0330`), `s` (status list), `f <drive>` (copies `RAND.BIN` to the
  drive and back as `R2.BIN`, `fc /b` both), `dh`, `eh`, `dc`, `ec`, `adv`
  (the controller's property sheet), `pwr` (the root hub's), `u` (installs
  `xhciuas.inf` on `USB\Class_08&SubClass_06&Prot_62`), `su` (lists the UAS,
  `USBSTOR\`, `XHCIUAS\` and `SCSI\` devnodes), `pnpctl
  list|disable|enable|remove|rescan <instance substring>`, `XHCISNAP`.
- Before every launch: more than 4 GB of host memory free, and the leg's
  monitor port not already listening. A guest is shut down from inside
  Windows, then `quit` on its own monitor port; never by killing QEMU by
  process name, since other legs' guests run on the same host.

**The clauses**, all on the leg's fresh overlay.

Launch L1, `p2=8,p3=0`:
1. **Install**: `i`. Every prompt recorded verbatim (screenshots) and
   answered: the unsigned-publisher box, install anyway; XP's Logo box,
   Continue Anyway; XP's New Hardware wizards for the root hub, let finish.
   Pass: controller and root hub started, problem 0 (`s`); the trace has
   `No Op self-test completion code=00000001` and no refusal. A restart
   request is recorded, and the IMOD value if the trace prints one.
2. **HID**: `step devs`, part 1 - the mouse bound, and `mouse_move` moves the
   pointer (a screenshot before and after).
3. **Storage `fc /b`, read and write**: `devs` runs `f <letter>`; both `fc`
   read "no differences encountered". The stick's letter is found first;
   XP may still be installing "Disk drive" at first, so wait and rerun.
4. **Unplug and replug**: `unplug`, then `hub` replugs the mouse and stick at
   root ports; `hubfc` runs `f` on both sticks.
5. **`usb-hub` with a mouse and a stick behind it**: `hub` and `hubfc` - the
   behind-hub mouse moves the pointer, the behind-hub stick's `fc` is clean.
   The hub pull is in `audiooff`.
6. **`usb-audio` at a root port and behind the hub**: `audio` - both audio
   functions started, problem 0 (`pnpctl list VID_46F4`; on XP wait out the
   New Hardware installs, up to a few minutes, and re-read). Then
   `audiooff`: the audio unplugged, then the hub pulled with the mouse,
   stick and audio beneath - all gone, no dialog, no hang.
7. **Root hub disable and enable**, a mouse attached at a root port: `dh`,
   `eh`; the devices back and the mouse moving.
8. **Controller disable and enable**, a mouse attached: `dc`, `ec`; a second
   No Op self-test in the trace, the mouse moving. On Vista and Windows 7,
   **five** `dc`/`ec` cycles (the NT 6.x tier's standing reading), the mouse
   moving after each; a hang over 10 minutes is a FAIL.
9. **Advanced and Power tabs**, mouse and stick attached: `adv` - the
   controller's Advanced tab, recorded verbatim (the bandwidth list, the
   "System reserved" row); `pwr` - the root hub's Power tab ("hub is self
   powered", total power, the device list). Every tab visited is
   screenshotted; an absent tab is recorded as a reading, not fixed.
10. **`XHCISNAP`**: `XHCISNAP -probe` (the four status lines), `XHCISNAP
    -verbosity 2`; after clause 11's restart, `XHCISNAP -probe` again (the
    channel live), `XHCISNAP -o C:\SNAP`, `dir C:\SNAP.*`, `find "coherence"
    C:\SNAP.TXT`. On x64 it runs under WOW64.
11. **Restart and shutdown**: first a restart from inside Windows (`shutdown
    -r -t 0`; on Vista x64 and 7 x64, F8 and "Disable Driver Signature
    Enforcement" again), back working (mouse moving, `s` clean) - clause 10's
    second half is taken here. Later, at the end of the launch, `shutdown -s
    -t 0` with a mouse and a stick attached: QEMU reaches `paused
    (shutdown)` (`info status`), no bugcheck, the trace ending with
    `quiesce: halted` and `save: declined ...`, quoted. Then `quit`. A launch
    nearing 2 hours is shut down and relaunched instead of restarted in
    place, and that is recorded.
12. **The soak**: 10 cycles each of `hid`, `hubmouse` and `storage`, on a
    booted guest at the desktop with **no devices attached**, as its own
    background command while QEMU runs:
    `scripts\vm-matrix\soak-11v.ps1 -Monitor <port> -DebugconLog <the current
    launch's log> -Target <g> -Driver hcd -Classes hid,hubmouse,storage
    -Cycles 10 -OutDir out\phase28\v1\<g>\soak`, adding `-Arch amd64` on an
    x64 guest. Pass: 30 of 30, each class +10 addressed, no FAIL line, and the
    guest alive afterwards.
13. **`usb-uas` at High Speed** (still `p2=8,p3=0`): `step uas -Port 5 -Id
    uh` (the attach shows `attached false`, then true). A Found New Hardware
    wizard may come up (cancel it, or let it fail); then `u` installs
    `xhciuas` (prompts recorded; the unsigned box, install anyway). Pass: the
    UAS devnode (`VID_46F4&PID_0003`) started, problem 0, service `xhciuas`
    (`su`); `info usb` speed 480; a volume appears; `f <letter>` clean. Then
    `step uasoff -Id uh`: gone, no dialog, no hang, no bugcheck. Then the
    clause 11 shutdown and `quit`.

Launch L3, `-Xhci 'p2=4,p3=4'`, the same overlay (F8 again on Vista x64 and 7
x64). Each QEMU port 1 to 4 is then a USB 2 and USB 3 pair, so a model that
can do SuperSpeed attaches at 5000 Mb/s:

14. **`usb-storage` at SuperSpeed**: `step ssstor -Port 1 -Id ss1 <letter>`
    (the letter found first, then `f <letter>` rerun): `info usb` shows 5000
    Mb/s; bound to the OS's `usbstor.sys` (a `USBSTOR\` devnode, problem 0);
    `fc /b` clean; the trace's transport line, if any. Then `step ssstoroff
    -Id ss1`.
15. **`usb-uas` at SuperSpeed**: `step uas -Port 2 -Id us` - bound to
    `xhciuas` (installed in 13; `u` again only if not), 5000 Mb/s, `fc /b`
    clean, then `step uasoff -Id us`, a clean unplug; the trace's `storage
    transport` line quoted. Then a clean shutdown and `quit`.

The x64 guests run the amd64 `xhciuas.sys`, which had never run anywhere
before these legs, so any failure there is a finding.

**After each launch** the trace is searched for `not served`, `refused`,
`fatal`, `gave up`, `given up`, `bugcheck` and `assert`; a nonzero counter of
those kinds is a finding. `QUERY_INTERFACE not answered` for `4747B320` and
`496B8280` once per driver load is a known observation (28-A.1 above).

**On a defect** (a bugcheck, a hang, a wrong binding, a failed `fc`, a
refusal in the trace, a clause the driver fails): the evidence first (the
bugcheck screen's code and parameters, `info usb`, `info status`, `info
registers` if wedged, the trace's tail, the exact steps), written to
`out\phase28\v1\<g>\DEFECT-<n>.md` with a hypothesis; the guest left safe
(quit through the monitor if wedged, otherwise shut down cleanly); the leg
stops there and goes back to the coordinator. A vehicle problem (QEMU, the
harness, the keyboard, a tool) is not a defect: it is worked around and
recorded as a vehicle note.

**Vehicle notes** the legs added (each guest's `report.md`):
- **The soak comes first in a fresh launch.** `soak-11v.ps1` refuses a trace
  that spans more than one driver load ("already spans more than one driver
  load or binary ... Restart the guest so the soak measures one continuous
  load"), so the soak cannot follow clause 8, clause 11's restart or a driver
  update in the same launch: shut down, relaunch, and run it before anything
  else (`vx86` report, clause 12; `xp` and `xp64` vehicle notes).
- **F8 on every boot of Vista x64 and 7 x64**: Advanced Boot Options,
  "Disable Driver Signature Enforcement", at every launch and every restart;
  the package is not signed.
- **Windows 7 and the `usb-uas` disk.** Every QEMU `vvfat` disk carries the
  same MBR signature (`BE1AFDFA`), so Windows 7 takes the `usb-uas` disk
  offline ("Signature Collision") because the transfer drive has it too:
  `diskpart`, `online disk`, then the round trip (`w7x86` and `w7x64`
  reports).
- **An update with the same `DriverVer` does not replace the binary on
  Vista and Windows 7.** `pnpctl update` with an INF identical to the
  installed one returns OK and leaves the old `xhci98.sys` in
  `System32\drivers`, and a new UAS device instance re-copies the old
  `xhciuas.sys` from the driver store. Remove the staged INFs first
  (`pnputil -f -d oem<N>.inf` for `xhci98` and for `xhciuas`) and install
  again, or copy both binaries over `System32\drivers` by hand; then `fc /b`
  each against the package, restart, and `fc /b` `xhciuas.sys` again after
  every UAS attach (`vx86`, `vx64`, `w7x86`, `w7x64` reports). XP did
  replace the file.
- The unsigned-driver box on Vista and Windows 7 does not take keyboard
  focus: answer it with a mouse click (`mouse_move`, `mouse_button`). On
  Vista a Program Compatibility Assistant box follows each unsigned install.
- The guests' keymap is US-Dvorak: on Windows 7 UAC's Yes is `alt-t`; on XP
  Continue Anyway is `alt-i`, Next `alt-l`, "No, not this time" `alt-k`.
- XP's AutoPlay and its per-instance Found New Hardware wizard take focus;
  XP gives a UAS device at a new port a new devnode, so the wizard and the
  `xhciuas` install recur per port. On XP x64 Win+R does nothing: `ctrl-esc`,
  then `r`.
- The transfer drive is `E:` on the Vista, Windows 7 and XP guests (`D:` is
  the CD-ROM). `g.ps1` cannot type `|`, and types capitals as lower case on
  Windows 7.
- The guest clock runs slow under TCG (five `dc`/`ec` cycles of about 116 s
  each showed as about a minute on the Vista x86 clock).
- `quit` on the monitor never returns a `(qemu)` prompt; QEMU exits 0.

### The reading on the merged build, `qemu` flavour

Taken by six guest subagents in parallel on development host A, 2026-10-04,
about 12:00 to 13:55 (XP SP3 and XP x64 at two virtual processors under TCG,
Vista and Windows 7 at four under multi-threaded TCG; 1024 MB on XP SP3,
2048 MB on the rest). Evidence `out\phase28\v1\<guest>\report.md`, with the
traces `debugcon-m1-<n>.log` and screenshots `shots\` beside each, and
`out\phase28\v1\STATUS.md`.

Two builds, because Package A appeared while the first launches ran:
- **`c0d8a51`**, the first merged build (`out\merged\pkg-c0d8a51\qemu`):
  x86 `xhci98.sys` SHA-256 `8e14104f...f2fd`, `xhciuas.sys` `6b62378f...d3d3`;
  amd64 `xhci98.sys` `df42fe9e...b696`, `xhciuas.sys` `1cd8c41a...d689`
  (full hashes in `out\phase28\v1\STATUS.md`). Clauses 1 to 11 on every
  guest; clause 13 on XP x64 and, first, on Vista x86 and Windows 7 x86.
- **Package A, `2f6030a`** (`out\merged\pkg-2f6030a\qemu`, its
  `SHA256SUMS.txt` checked): x86 `xhci98.sys` `8736aaa3...9d00` (188,038
  bytes), `xhciuas.sys` `2d0d5b49...3a38`; amd64 `xhci98.sys`
  `114b1340...b884`, `xhciuas.sys` `09b244e6...1f55`. It changes only the
  Windows 98 and ME removal paths and the hub resume against `c0d8a51` (the
  coordinator's statement, relayed in the `vx86` report), and its INFs are
  `c0d8a51`'s; clauses 1 to 11 therefore stand on `c0d8a51`. Clauses 13 to
  15 on Package A everywhere (XP x64's 13 on `c0d8a51`), after the binaries
  were proved in place (the same-`DriverVer` vehicle note above).

The soak, clause 12, is read once, on Package B, by the coordinator's
instruction; the readings taken before that instruction are noted below
the table and are not the clause's reading.

| # | Clause | ME (SweetLow) | XP SP3 | XP x64 | Vista x86 | Vista x64 (F8) | 7 x86 | 7 x64 (F8) |
|---|---|---|---|---|---|---|---|---|
| 1 | Install from clean, prompts as recorded | TBD(Package B) | PASS | PASS | PASS | PASS | PASS | PASS |
| 2 | HID mouse on a root port | TBD(Package B) | PASS | PASS | PASS | PASS | PASS | PASS |
| 3 | Storage, `fc /b` read and write | TBD(Package B) | PASS | PASS | PASS | PASS | PASS | PASS |
| 4 | Unplug and replug | TBD(Package B) | PASS | PASS | PASS | PASS | PASS | PASS |
| 5 | `usb-hub` with a mouse and a stick behind it | TBD(Package B) | PASS | PASS | PASS | PASS | PASS | PASS |
| 6 | Audio bound at a root port and behind the hub; audio unplugged; hub pulled with devices beneath | TBD(Package B) | PASS | PASS | PASS | PASS | PASS | PASS |
| 7 | Root hub disable and enable | TBD(Package B) | PASS | PASS | PASS | PASS | PASS | PASS |
| 8 | Controller disable and enable (five cycles on Vista and 7) | TBD(Package B) | PASS | PASS | PASS, 5 cycles | PASS, 5 cycles | PASS, 5 cycles | PASS, 5 cycles |
| 9 | Advanced tab and Power tab | TBD(Package B) | PASS (note) | NOTE | PASS | PASS | PASS | NOTE |
| 10 | `XHCISNAP` | TBD(Package B) | PASS | PASS | PASS | PASS | PASS | PASS |
| 11 | Restart and shutdown | TBD(Package B) | PASS | PASS | PASS | PASS | PASS | PASS |
| 12 | Soak, 10 cycles per class | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) |
| 13 | `usb-uas` at High Speed | TBD(Package B) | PASS (A) | PASS (`c0d8a51`) | PASS (`c0d8a51` and A) | PASS (A) | PASS (A) | PASS (A) |
| 14 | `usb-storage` at SuperSpeed | TBD(Package B) | PASS (A) | PASS (A) | PASS (A) | PASS (A) | PASS (A) | PASS (A) |
| 15 | `usb-uas` at SuperSpeed | TBD(Package B) | PASS (A) | PASS (A) | PASS (A) | PASS (A) | PASS (A) | PASS (A) |
| - | 28-A.1: ME's own `usbccgp.sys` left unused | TBD(Package B) | n/a | n/a | n/a | n/a | n/a | n/a |

**No HCD or `xhciuas` defect on any of the six guests**: no bugcheck, no
hang, no `not served` or `refused` line, every refusal, fatal and gave-up
counter 0 in every trace, and no `DEFECT-<n>.md` written. What the cells
rest on, guest by guest:
- **Install.** XP SP3 and XP x64: the Logo boxes, Continue Anyway, and the
  root hub's wizard; no CD prompt, no restart. Vista and Windows 7: the
  unsigned-publisher box ("Windows can't verify the publisher of this driver
  software"), "Install this driver software anyway"; on Vista a Found New
  Hardware box at boot and a Program Compatibility Assistant box after; no
  restart request. Every guest: controller and root hub started, problem 0,
  `No Op self-test completion code=00000001`. This build prints no literal
  `DriverEntry` line and no IMOD line (`vx86` report, clause 1).
- **Hub.** The QEMU hub at 12 Mb/s, the mouse at 3.1 moving the pointer, the
  stick at 3.2 clean; the hub has no `USB\` devnode of its own, since the
  HCD owns the topology (`behind hub, root port/route=03000001..03000003`,
  `TT pairs programmed=00000000` in the Vista x86 trace).
- **Audio.** Both functions started, problem 0; Vista and Windows 7 name the
  function "USB Device" (service `usbaudio`), not "USB Audio Device". The
  hub pull with the mouse, stick and audio beneath left nothing behind, no
  dialog, no hang.
- **Controller disable and enable.** Vista x86: five cycles of 116 to 117 s
  each, `teardown: ports unpowered=00000008` and `quiesce: halted,
  USBSTS=00000001` five times, and six No Op self-tests by the end. Vista
  x64 about 117 s a cycle, Windows 7 x86 about 120 s, Windows 7 x64 about
  113 s; none hung.
- **Tabs.** XP SP3 and XP x64: the Advanced tab's list has the one row
  "System reserved 11%", and the Power tab lists the devices with power
  required "Unknown". Vista and Windows 7: "System reserved 20%" (the mouse
  and stick are not listed), "Tell me if my device can perform faster"
  checked; the root hub's Power tab "The hub is self-powered.", "Total power
  available: 500 mA per port", the devices "Unknown"; its Advanced tab "Hub
  is operating at high-speed". The NOTE cells are that reading: the device
  rows and power figures are not filled in.
- **`XHCISNAP`.** Before the restart, `-probe`'s PassThru line reads status
  6 ("MINIPORT DECLINED"); after `-verbosity 2` and the restart, status 0,
  "the channel is live"; `-o C:\SNAP` writes `.BIN`, `.PSC` and `.TXT`
  (Vista x86: 105,192, 32 and 2,286 bytes, "schema 5", "coherence: tear
  detector 2612, unchanged across every window."). Under WOW64 on the x64
  guests.
- **Shutdown.** `paused (shutdown)` with a mouse and a stick attached, the
  trace ending `quiesce: halted, USBSTS=00000001` / `SuspendController:
  halted, USBCMD=00000000` / `save: declined - the controller does not
  declare FSC, HCIVERSION=00000100`.
- **UAS at High Speed.** 480 Mb/s, "xHCI98 USB Attached SCSI Storage"
  started with service `xhciuas`, the LUN `XHCIUAS\DISK&VEN_QEMU&PROD_QEMU_HARDDISK...`
  ("QEMU QEMU HARDDISK UAS Device") started, `fc /b` clean both ways,
  `hcd: storage transport, port/transport/why/alternate=00052200`, and a
  clean surprise removal. The first install of `xhciuas.inf` raised the same
  unsigned-publisher box on Vista and Windows 7, and no restart. XP x64's
  leg was the first run of the amd64 `xhciuas.sys` anywhere.
- **SuperSpeed storage.** `Port 1, Speed 5000 Mb/s, QEMU USB MSD`, `port speed
  decoded - superspeed=00000001`, bound to the OS's `usbstor.sys`, `fc /b`
  clean, a clean unplug. No `storage transport` line is printed for a
  Bulk-Only device, at either speed; the line appears for UAS only.
- **UAS at SuperSpeed.** 5000 Mb/s, `xhciuas` bound with no reinstall, `fc
  /b` clean, `storage transport ...=00022200`, a clean unplug - on XP x64
  the first SuperSpeed run of the amd64 `xhciuas.sys`.

**The soak before Package B** (not the clause's reading): XP SP3 on Package A,
`hid`, `hubmouse` and `storage` 10 of 10 each, the settled gap 0, "IDENTITY
EXACT", every refusal, fatal and gave-up counter 0
(`out\phase28\v1\xp\soak\soak-xp.txt`); XP x64 on `c0d8a51`, a fresh boot
with `-Arch amd64`, the same (`out\phase28\v1\xp64\soak\soak-xp64.txt`).
Vista x86's and Windows 7 x86's attempts on `c0d8a51` were refused at the
harness's preflight because their trace spanned more than one driver load
(`vx86\soak\soak-vx86-L1-aborted.txt`, `w7x86\soak-L1-aborted\`); Vista x64
and Windows 7 x64 took none.

**Windows ME** is read on Package B, by its own leg: TBD(Package B). Its
pre-read and rerun on `1ed1ba6` are above and in
`out\phase28\pre\me\report-rerun.md`.

### The ten install legs on the `release` flavour

The checkpoint reads the ten install legs on the `qemu` build and then on the
`release` flavour. The kit is ready (`out\phase28\release`: `launch.ps1
-Leg`, `stage.ps1 -Pkg out\merged\pkg-<commit>\release`, nine clauses in
three launches, fresh overlays `vm\t28rel\<leg>-rel`), so the same-`DriverVer`
trap cannot bite. Release package: TBD(Package B) (commit, hashes).

| Install leg | `release` package installed | Controller and root hub working | HID, storage `fc`, audio bound | Controller disable and enable | Shutdown |
|---|---|---|---|---|---|
| Windows 98 SE, NUSB 3.3 | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) |
| Windows 98 SE, SweetLow | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) |
| Windows 2000 SP4 | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) |
| Windows ME, SweetLow | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) |
| XP SP3 | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) |
| XP x64 | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) |
| Vista x86 | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) |
| Vista x64 (F8) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) |
| 7 x86 | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) |
| 7 x64 (F8) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) | TBD(Package B) |

### Windows 98 SE with no USB 2.0 stack, and with SweetLow's alone

Two Windows 98 SE legs for the README's prerequisites, both on development
host A, 2026-10-04, one virtual processor under TCG, from the read-only 26-V.3
stock base `vm\t26\win98-stock-base.qcow2` (NUSB 3.3 uninstalled from the
NUSB image, `runs/run-26.md`, "26-V.3").

**Stock, no USB 2.0 stack**, on Package A (`out\merged\pkg-2f6030a\qemu\x86`,
`xhci98.sys` `8736aaa3...9d00`), overlay `vm\t28rel\stock98-a.qcow2`,
13:13 to 13:40 (`out\phase28\stock98\notes.txt`, screenshots `shots\`):
- The controller from the Add New Hardware Wizard, Search, `D:\`: the CD
  asked for, then "The file 'usbd.sys' on Windows 98 Second Edition CD-ROM
  cannot be found." (given `E:\WIN98`), then the restart. On the second
  boot the root hub installed with no prompt. Both "This device is working
  properly."; `No Op self-test completion code=00000001`; every refusal,
  fatal and gave-up counter 0. Nothing else was asked for: no USB 2.0 stack
  is present and none is needed.
- A mouse at a root port (the CD for `hidclass.sys`, once) and a mouse behind
  `usb-hub` moved the pointer: PASS.
- A stick: "Unknown Device", Code 28, no drive letter - NODRIVER, as 26-V.3
  read and as expected.
- The unplugs and the shutdown clean.

**SweetLow's stack alone**, on the `qemu` flavour of `1ed1ba6`
(`out\phase28\pre\pkg-1ed1ba6\x86`, `xhci98.sys` `d7c5c281...68a5`,
`xhciuas.sys` `17be6a5f...69fe`), overlay `vm\t26\win98-sl28a.qcow2`, 11:40
to 13:17, `qemu-xhci,p2=4,p3=4` (`out\phase28\sweetlow-only\report.md`):
- An inventory first (`dir /s /b` over C:, and the registry exported and
  searched): no NUSB stack or storage file present; `NTMAP.SYS` alone, the
  QFE copy 26-V.3 left on purpose.
- SweetLow's `USB2.INF` installed with no prompt; the controller's install
  then asked exactly what the stock one does (the CD, `usbd.sys`, the
  restart). A mouse at a root port and one behind a hub: PASS.
- **Storage needs NUSB's mass-storage files.** A Bulk-Only stick: Code 28, no
  driver. The UAS device: `xhciuas.inf` installs, asks for a restart, and
  the device stays at Code 2 ("The NTKERN.VXD device loader(s) for this
  device could not load the device driver.") across a restart, because the
  upper filter the INF names, `USBNTMAP.SYS`, is not on the machine: NTKERN
  fails the whole devnode rather than skip the missing filter, and sends
  the PDO a REMOVE while the device is present. This is the first runtime
  reading of a named but missing upper filter.
- **NUSB 3.3's five storage files alone are enough** (`USBSTOR.INF`,
  `USBSTOR.SYS`, `USBNTMAP.INF`, `USBNTMAP.SYS`, `USBMPHLP.PDR`, from
  `tools\nusb-extracted`), without NUSB's USB 2.0 stack: given to the wizard
  as a folder, both the stick and the UAS device got a drive letter and a
  clean `fc /b` round trip, with no CD prompt and no restart. One step is
  not obvious: on the first storage device Windows binds the disk child to
  its own "Disk drive" before it has seen `USBNTMAP.INF`, so Update Driver on
  that "Disk drive", pointed at the same folder ("USB Disk"), then a replug.
  The UAS devnode that had been Code 2 started after the next cold boot with
  no change to `xhciuas.inf`.
- NUSB's own installer cannot install the storage half alone: `_NUSB.INF` has
  one `[DefaultInstall]` that copies the whole stack (static, text read).
- **An HCD defect found here and fixed since**: a root port stayed held after
  a Code 2 device was unplugged, because Windows 98 SE sends no second
  REMOVE to a devnode it has already removed. That is `cc9da33`, below.

### The upgrade from `1.2.0.0`

Five guests on development host A, 2026-10-04, fresh overlays
`vm\t28upg\*.qcow2` over bases with no 1.x driver, one virtual processor
under TCG (Windows 7 two), each guest first given `1.2.0.0`'s
`releases\1.2.0.0\release-x86` (`xhci98.sys` `2D53B5F4...DDEB35`) and then
upgraded to the HCD's `qemu` package of `1ed1ba6`
(`out\phase28\pre\pkg-1ed1ba6\x86`, `xhci98.sys` `d7c5c281...68a5`,
`DriverVer` 10/02/2026,1.99.0.0). Report `out\phase28\upgrade\report.md`
(the agent's text, saved by the coordinating session); traces, screenshots
and transfer folders in `out\phase28\upgrade\<guest>\`.

| OS | The route that worked | Prompts | Restarts | Result |
|---|---|---|---|---|
| 98 SE, NUSB 3.3 | **Not in place.** `ren` `XHCI98.SYS` to `XHCI98.SAV`, a cold boot (the controller at Code 2), then Update Driver, "Display a list", Have Disk. Rename, then Remove, Refresh and Search with a location, also works | The CD for files, then `usbd.sys` (the CD's `WIN98` folder), then the restart; each USB device detected once more, HID asking for the CD (`hidclass.sys`) | 1, plus the cold boot after the rename | PASS by that route. In place: fatal exception 0E |
| 98 SE, SweetLow | In place (Update Driver, Have Disk); the new driver runs only after a cold boot, which Windows does not ask for | None at the update; each device detected once more after the boot (the CD for `hidclass.sys`) | 1 cold boot, the user's | PASS after the boot; a bad state until then (finding 2) |
| 2000 SP4 | In place: Update Driver, "Display a list of the known drivers", Have Disk, the first of three models | None at Finish; after the first restart a "must restart" prompt | 0 to run; 1 deferred | PASS |
| XP SP3 | In place: Update Driver, "No, not this time", "Install from a list or specific location", "Don't search", Have Disk | The Logo box, Continue Anyway; a second wizard for the root hub (automatic, Continue Anyway) | 0 | PASS |
| 7 x86 | In place: Update Driver Software, Browse, "Let me pick", Have Disk | Unverified publisher, install anyway | 0 | PASS; typing the folder into the search box does not upgrade (finding 3) |

After the upgrade, on every guest: the new controller and root hub names;
the mouse, a stick copy with `fc /b` clean, unplug and replug, `usb-hub` on
port 3 with a mouse at 3.1, a clean restart and shutdown (`quiesce: halted`,
fatal controller status and gave-up 0), and no leftover devnode with a
warning mark.

**The NUSB stop crash, on this path: carried, unchanged.** An in-place
Update Driver from `1.2.0.0` to the HCD on Windows 98 SE under NUSB, with a
mouse and a stick attached: "A fatal exception 0E has occurred at
0028:C00312EE" - the address `1.2.0.0`'s readme records - then "RUNDLL32
caused a general protection fault in module USER.EXE at 0010:00001288", the
taskbar gone (`n98\shots\CRASH-inplace-0E.png`). The HCD's trace is empty
throughout: the crash is NUSB's `usbport.sys` stopping the running
`1.2.0.0` miniport, before any HCD code runs, and the HCD cannot prevent it.
What changed from `1.2.0.0`'s description: after Ctrl+Alt+Del, Shut Down and a
cold boot, the machine came up on the HCD rather than keeping the old
version. The route that avoids it is the rename and cold boot above, which
no longer lets NUSB's `usbport.sys` stop a running miniport. Disabling the
HCD itself under NUSB was not re-measured here; Phases 26 and 27 read it
clean (28.3 below).

**Findings:**
1. **Windows 2000: no "Unplug or Eject Hardware" icon** for a USB stick under
   the HCD (present under `1.2.0.0`); pulling the stick raised "Unsafe Removal
   of Device" (`w2k\shots\p1`). The device PDO reported `SurpriseRemovalOK`
   TRUE. Fixed by `9001ebd` and `8993884` (below): on a Windows 2000 guest
   with a `p28-pdoleak` build carrying `9001ebd`, the icon was back, "Unplug or
   Eject Hardware" listed "USB Mass Storage Device at Location 3", and Stop
   gave "The 'USB Mass Storage Device' device can now be safely removed from
   the system." (`out\phase28\pdoleak\w2k-stick.png`, `w2k-hotplug.png`,
   `w2k-stop3.png`, about 15:11 to 15:14). On Package B: TBD(Package B).
   Windows 98 and XP had the icon; Windows 7 not checked.
2. **Windows 98 SE under SweetLow: the in-place update needs a cold boot.**
   Twice, on fresh chains: no crash and no prompt, but the HCD does not run
   until the next boot (its trace empty, Device Manager showing the new
   controller name above SweetLow's old root hub); USB dead and dialog
   keyboard input lost after Finish, until the boot. The hypothesis is
   NTKERN reusing the already-loaded `xhci98.sys` image, the module name
   being the same; not chased. The README tells the user to restart at once.
3. **Windows 7: "Browse" alone does not upgrade.** Pointing the search at
   the HCD's folder answered "The best driver software for your device is
   already installed", twice; only "Let me pick" and Have Disk upgraded.
   Hypothesis: both packages' `DriverVer` carry the date 10/02/2026. To be
   read again at the cut.
4. **Windows 2000's Have Disk lists all three models** (controller, root hub,
   UAS storage); the right one is first and selected. Windows 98, XP and 7
   list only the controller.
5. Small: Windows 98 shows the HCD's date as 10-4-2026 (the file's date)
   against `DriverVer` 10/02; the renamed `XHCI98.SAV` stays in
   `SYSTEM32\DRIVERS`.

The README's upgrade steps are this report's proposed text, in the README's
voice.

### The Windows 2000 audio unplug during playback

Phase 27 carried a limitation: on Windows 2000, an audio device unplugged
while it played got SURPRISE_REMOVAL and then no REMOVE within minutes
(`runs/run-27.md`). Read again on development host A, 2026-10-04, 11:42 to
12:05, on the `qemu` flavour of `1ed1ba6` (`xhci98.sys` `d7c5c281...68a5`),
a Windows 2000 SP4 guest on a new overlay `vm\t26\win2k-au28a.qcow2` over the
matrix's `win2k-mxh`, one virtual processor, `-audiodev none`, a 120 s
440 Hz WAV played by `sndrec32` (one leg `mplay32`)
(`out\phase28\w2k-audio-unplug\report.md`, trace `debugcon-b1.log`,
`ts-b1.log`):
- 7 of 7 unplugs got SURPRISE_REMOVAL (`0x17`) and then REMOVE (`0x02`)
  within about a second - three at a root port while playing, one behind a
  `usb-hub`, one with the hub itself pulled under the playing device, one
  stopped (the control), one under Media Player; the player still open and
  still holding the file in every case.
- Every replug re-enumerated and played; nothing hung; the iso missed-service
  and packet error counters and every refusal counter read 0; the shutdown
  clean.
- The difference from Phase 27's reading is not bisected; the report's
  hypothesis is the departed-URB handling that changed after `ed025d2`
  (`09ed9d1`, `612f05a` and the Phase 28 merges).

Verdict: **gone** (28.3 below).

---

## 28-E.1 - the bench

**Waits for the combined bench session (owner, 2026-10-03).** Nothing of it
has been read. Its parts, as the roadmap lists them:
- the E460 on Windows 98 SE and on 32-bit Windows 7, and the second Windows
  98 SE machine, through `run-13e.md`'s stage list: install, HID, storage,
  Ethernet, audio played and heard on a root port and behind a hub (Phase
  27's playback clause), five hot-plugs, and the controller disable that hung
  Windows 7 under the miniport - waits for the combined bench session (owner,
  2026-10-03);
- Phase 27's High-Speed hub clauses: the hub rig at positions H1 to H4 with
  the single-TT and multi-TT units of `test-equipment.md`, Low and Full Speed
  devices behind each, and the Full-Speed-hub-behind-High-Speed-hub clause if
  a specimen is held, otherwise untested ground - waits for the combined bench
  session (owner, 2026-10-03);
- Phase 27's Low-Speed mouse at `bInterval` 10 polled every 8 ms on a root
  port and behind a hub, moved to this bench by the owner's decision of
  2026-10-04 because no QEMU model is Low Speed (its host vectors are
  `test_hub`'s `test_low_speed_mouse`) - waits for the combined bench session
  (owner, 2026-10-03).

Two rulings of 2026-10-04 change what the session reads (`roadmap-hcd.md`,
decisions table):
- **Windows 7 is not benched** ("We won't bench Win 7", owner, about 14:05):
  the E460's 32-bit Windows 7 half of 28-E.1, 29-E.1, 30-E.1 and 31-E.1 is
  dropped, so Windows 7 is a virtual-machine target only for `2.0.0.0`, and
  the miniport's Windows 7 disable hang is not re-measured on metal (28.3).
- **The Full-Speed hub clause is taken**, with a USB 2.0 hub held at Full
  Speed by an ADuM full/low-speed isolator in front of it, in place of a
  USB 1.1 hub (owner, about 12:50, superseding the plan to buy one).

---

## 28.3 - the limitations of `1.2.0.0` under the HCD

Each limitation is written down as gone, carried or new. By the owner's
ruling of 2026-10-04 a survivor is carried in the release notes and does not
block the cut (`roadmap-hcd.md`, decisions table). The list as read on
2026-10-04:

| Limitation | Under `1.2.0.0` | What has been read under the HCD | Verdict |
|---|---|---|---|
| The NUSB stop crash | Stopping a running controller on Windows 98 under NUSB 3.3 crashes (`fatal exception 0E at 0028:C00312EE`, the same with Microsoft's `usbehci.sys`); it is NUSB's `usbport.sys`, which the HCD replaces | Stopping the HCD itself: in QEMU, Windows 98 SE under NUSB 3.3, the controller's disable, enable, remove and rescan clean on `a7ddbfa` (`runs/run-26.md`, "The Windows 98 door sequence"); disable and enable clean with a hub subtree beneath it on `981f56b` (`runs/run-27.md`, "The orderly path with a hub subtree beneath it"). Upgrading from `1.2.0.0` in place: the same fatal 0E at `0028:C00312EE`, the HCD's trace empty, so NUSB's `usbport.sys` stopping the running miniport before any HCD code runs; a cold boot then comes up on the HCD; the rename-and-cold-boot route avoids it (`out\phase28\upgrade\report.md`; "The upgrade from `1.2.0.0`" above). Physical machine: none | **Carried, on the upgrade path only**: gone for the HCD's own disable, enable and removal; the upgrade over a running `1.2.0.0` under NUSB still crashes, and the README gives the rename-and-cold-boot route |
| The idle that never sleeps | The controller never idles; the driver tells Windows so as it registers | The bus initiates no suspend: the owner's decision of 2026-10-04 on 27-A.1 ("handle, not initiate", `runs/run-27.md`, "The hub-port resume"); `SUBMIT_IDLE_NOTIFICATION` is held and never called back (28-A.1) | **Carried**, by the owner's ruling of 2026-10-04 (`roadmap-hcd.md`, decisions table, "The idle power policy (28.3)"): `2.0.0.0` never initiates selective suspend, of a device or a hub port, and handles one it is asked for or a hub reports |
| The Windows 98 churn wedge | Plugging and unplugging a device every 0.6 s for minutes froze Windows 98 (the miniport's own defect, at 12 and 18 enumerations) | In QEMU, Windows 98 SE under NUSB 3.3: the 120-hub churn with storage resident enumerated 120 of 120 and the guest stayed responsive, on `f99f184` (`soak-h98f`) and `09ed9d1` (`soak-h98j`) (`runs/run-27.md`, "The soak"). On the final build: TBD(Package B soak) | TBD(Package B soak) |
| The Windows 7 disable hang | On the E460, 32-bit Windows 7, the first controller disable never finished (2026-09-19); VMs did not show it | Not reproduced under the HCD in QEMU: the pre-read on Windows 7 x86 (above), then five controller disable and enable cycles each on Windows 7 x86 and x64 on `c0d8a51`, about 113 to 120 s a cycle, none hung (`out\phase28\v1\w7x86\report.md`, `w7x64\report.md`, clause 8). Windows 7 is not benched (owner, 2026-10-04, about 14:05), so no metal re-measure follows | **Gone**: removed from the `2.0.0.0` limitations by the owner's ruling (2026-10-04, about 14:10) - a `1.2.0.0` miniport and usbport issue, not reproduced under the HCD, with no metal caveat |
| The Windows 98 audio-load wedge | The miniport had it: an intermittent wedge on an audio replug after a cold boot (`1.1.0.0` 2 of 10, `1.1.1.0` 5 of 10; `lessons.md`) | Seen on every HCD build tried, timing-dependent (5 of 6 at about 40 s after boot, 0 of 20 after 120 s), every IRP to the function PDO completed and nothing outstanding at the HCD (`runs/run-27.md`, "The Windows 98 SE audio-load wedge") | Carried: pre-existing, above the HCD |
| Windows 2000: an audio device unplugged during playback gets no REMOVE | Not on the `1.2.0.0` list | Phase 27: SURPRISE_REMOVAL and ABORT_PIPE, then no REMOVE within minutes, on `ed025d2`, `26e7cb6` and Phase 26's `a7ddbfa` (`runs/run-27.md`). On `1ed1ba6`: 7 of 7 unplugs during playback got REMOVE within about 1 s, root port, behind a hub and with the hub pulled, with Sound Recorder and Media Player (`out\phase28\w2k-audio-unplug\report.md`; "The Windows 2000 audio unplug during playback" above) | **Gone** |
| Windows ME: a UAS drive as the first USB storage device | Not on the `1.2.0.0` list (`1.2.0.0` had no UAS driver) | On a fresh ME install whose first storage device is a UAS drive, the drive shows Code 2: ME copies its own `USBNTMAP.SYS` and `USBMPHLP.PDR` only when its first ordinary stick installs, and `xhciuas.inf` names `USBNTMAP.SYS` as its upper filter (`out\phase28\pre\me\report-rerun.md`; `runs/run-31.md`, "Windows ME: a UAS drive first") | **New, carried** (owner, 2026-10-04, option C). The recovery - an ordinary stick once, then the UAS drive replugged - is unmeasured: TODO(28-V.1, ME on Package B) |

---

## The device-pull fixes

Phase 27's Windows 98 removed-PDO fix and seven commits of Phase 28 changed
what happens when a device leaves at an awkward moment. Each was found on a
guest, fixed on branch `p28-pdoleak` (`c038326` on `p27-int`), reviewed by
Codex, and merged into `p28-31-int` (`aafede0`, then `417199e`):

- **`c038326`** (Phase 27): a gone PDO that receives its REMOVE moves to
  `RemovedPdos` and is deleted at the next root-hub BusRelations answer,
  because Windows 98 SE sends a removed PDO a relations query and one more
  IRP after its REMOVE in the same pass; deleting it inside the REMOVE gave
  fatal exception 0E at `0028:C002A3A7` (`runs/run-27.md`).
- **`cc9da33`**: Windows 98 SE removes a devnode with no driver, or one
  Device Manager removed, while its device is still present, and sends no
  second REMOVE when the device then leaves; the PDO stayed on `GonePdos`
  and its root port was never enumerated again (the SweetLow-only leg's
  Code 2 UAS device, a cancelled audio wizard behind a hub, and 31-V.2's
  StoreJet after a Device Manager Remove). The relations answer now moves
  every gone PDO whose REMOVE has come to `RemovedPdos`. It also corrects
  `c038326`'s comment: the IRP after the post-REMOVE relations query is
  minor `0x13`, `IRP_MN_QUERY_ID`.
- **`2f6030a`** (Codex, the merge's round 2, MAJOR): the 32-entry retired
  list fell back to deleting an orphan inside its REMOVE when full; it is
  now an intrusive list that never fills. Package A is this commit.
- **`791f9f8`**: two Windows ME pulls during a mouse's install, on a healthy
  guest (`out\phase28\pre\me\report-rerun.md`, `r3-t1` to `r3-t5`). Pulled
  before START: the PDO was started SUCCESS anyway, stopped and never
  removed, its port held and the shell wedged - a START on an orphan or on a
  departed PDO now fails `STATUS_UNSUCCESSFUL`, as Windows 2000's hub driver
  fails one (`USBHUB20.SYS` 5.00.2195.6655, `0x142E8`, static). Pulled during
  SELECT_CONFIGURATION: the failed Configure or Stop Endpoint requested a
  controller reset, twice, dropping every device - a command failing on a
  device proven to have left no longer resets the controller.
- **`8a1a0c4`** (Codex review of the above): a suppressed reset makes the
  device's teardown certain - a record already gone is left to the teardown
  under way, otherwise the device's port is cycled.
- **`7f1b4e0`**, the owner's option 1: refusing the START did not bring the
  REMOVE on ME (on `8a1a0c4`'s package: "START refused, device gone", minor
  4, then nothing for 300 s and the port never re-enumerated). A gone PDO
  now holds its port only until a relations answer has reported it missing;
  the port then enumerates a new device with a new PDO under the same
  location instance id, while the old PDO waits for its REMOVE or the
  controller's release, as usbport's children do. The pull-during-install
  trial's traces are in `out\phase28\pdoleak\`: on Windows 98 SE,
  `w98-t5.txt`, `w98-t5b.txt` and `w98-t5-ctlA.txt` (14:27 to 14:58: no
  REMOVE within 300 s and the port not re-enumerated, which is what option 1
  stops depending on); on Windows 2000, `w2k-t5.txt` (15:18: a mouse pulled
  at minor `0x0D` got its REMOVE at once and the port re-enumerated). Which
  build each trial ran is not written beside the traces: TBD.
- **`9001ebd`**: device PDOs report `SurpriseRemovalOK` FALSE, as Windows
  2000's `usbhub.sys` does, so the hot-plug applet lists a stick again (the
  upgrade report's finding 1, above). Removable stays TRUE; HID and other
  function drivers that tolerate a pull set TRUE on the way up themselves.
- **`8993884`** (Codex review of `9001ebd`): a split function of a composite
  device keeps `SurpriseRemovalOK` TRUE, since stopping one function does
  not make the shared connector safe to pull while its siblings run.

The Windows 2000 tray icon was read on a `p28-pdoleak` build carrying
`9001ebd` (`out\phase28\pdoleak\w2k-*.png`, about 15:06 to 15:18). The
package is not named beside the screenshots; `out\phase28\pkg-pdoleak4`,
staged at 14:55, after `9001ebd` and before `8993884`, is the candidate -
TBD. `8993884` changes only split function PDOs, so the stick's reading does
not depend on it. The ME
controller re-enable that left a stale PDO (`report-rerun.md`, "Other
clauses") was sent to the same branch. Every one of these is read again on
Package B: the Windows 98 and ME pulls and replugs, the Windows 2000 icon,
and the ME controller disable and enable - TBD(Package B).


### A Windows 98 port change "lost after idle": withdrawn

A reading during the day suggested that Windows 98 guests stopped seeing
port changes after 2 to 3 minutes idle (`PORTSC` `0xEE1`, IRQ 11 quiet). It
was investigated on 2026-10-04 and withdrawn; it is not a defect and not a
28.3 item. Fifteen idle tries across five builds - the final `8993884` with
`XhciImodInterval250ns` at 500 and at 4000, Package A, `c038326`, and
Windows 2000 - all detected the attach and the pull in under 25 s, with the
registers clean (`USBSTS` 0, `IMAN` `0x2`, `ERDP`'s EHB clear). The earlier
reading had two causes, neither the driver's:
- the trace's `event: port status change` line is printed by
  `XHCI_DBG_VALUE_LIMITED` (`src\xhci_evt.c`, line 170), which stops after 32
  lines a driver load, so in a long trace later port events are silent
  however many occur;
- Windows 98 holds a gone PDO's port while its modal Add New Hardware wizard
  is open, by design: the configuration manager sends no relations query
  until the wizard is dismissed, so nothing re-enumerates there in the
  meantime.

Evidence: `out\phase28\idle\` and `out\phase27\audio98\<tag>-debugcon.log`.
`lessons.md` carries the two harness traps the investigation met.

---

## The interrupt moderation default: 160

**The ruling.** The HCD's INFs write `XhciImodInterval250ns` = 160 (40 us) on
every install path of both packages (owner, 2026-10-04, about 10:10), where
the miniport's wrote 500 (125 us) from `1.1.1.0`. The code default is
unchanged: `XHCI_IMOD_INTERVAL_DEFAULT` stays 4000 (1 ms), what an absent,
unreadable or out-of-range value runs at - "INF only", the owner confirmed.

**The evidence**, the owner's ATTO Disk Benchmark readings on the ThinkPad
P14s Gen 1, Windows 98 SE with NUSB, the MSSU10 under `xhciuas.sys` at
SuperSpeed, I/O "Neither" (queue depth 1), the informal release package of
`1ed1ba6` (`out\informal-p14s-uas98`), relayed to the coordinator on
2026-10-04 (no copy of the screenshots is held under `out\`):

| `XhciImodInterval250ns` | 0.5 KB write/read | 64 KB write/read | 8 MB write/read |
|---|---|---|---|
| 500 (125 us, the INF's value until the ruling) | 2.89 MB/s (one figure) | 181/182 MB/s | 181/181 MB/s |
| 160 (40 us) | 2.84 MB/s (one figure) | 203/209 MB/s | 211/221 MB/s |
| 40 (10 us) | 2.84/2.87 MB/s | 208/214 MB/s | 217/225 MB/s |

- At 8 MB, 160 reads 17 to 22% above 500; 40 adds only 2 to 3% over 160,
  near noise, for up to four times the interrupt rate.
- Small commands do not move with the value (about 175 us a command at
  0.5 KB under all three), so that overhead is elsewhere; unmeasured.
- QEMU models no interrupt moderation, so no guest reading can stand in for
  this (`lessons.md`; task 23.2).

**The change** is `2823958` on branch `p28-imod`, merged into `p28-31-int`
at `c8deaed` and so in `c0d8a51`: both INFs (the amd64 file already wrote the
value on both of its paths, through one shared `Xhci.AddReg.NT`), `src\xhci.h`'s
comment, the INF gate's expected value 160 on both files with 500 a refused
wrong value on the 9x, NT and amd64 paths (643 checks), the footprints,
`scripts\bench\IMOD98.BAT` (160, `0x000000a0`, on its ladder) and the bench
README, and forward notes in the release notes, README, acceptance test and
`build-and-test.md`.

**The Full-Speed audio re-check at 160**, before the change (the commit
message's reading; no report file beside it): a Windows 2000 guest from the
golden `mxh` overlay, the `qemu` flavour, development host A, Update Driver
from the `p28-imod` package; the interrupter's IMOD register read `0x1F4`
before and `0xA0` after; QEMU `usb-audio` on a root port played a 120 s WAV
through `sndrec32` to its end - iso packets answered 0 to 120,030, iso
missed-service errors 0, iso packet errors 0, transfers 12,017 of 12,017.
That shows the value reaches the controller and a stream runs under it, not
what 160 costs an audio stream on metal: TODO(bench), Full-Speed audio
playback while a drive is read at full speed.

---

## Codex

The Phases 28-31 integration was reviewed in nine rounds over `p28-31-int`
(`.claude\codex-p2831-r1-result.txt` to `-r9-`, not tracked): rounds 1 to 7
taken in `81d3942`, `2943dfe`, `3c56beb`, `bf4612c`, `c0f9ad6`, `d05990a`
and `28e9254`; round 8 over `28e9254` found no MAJOR or MINOR; round 9 over
the `p27-fix` merge `239d23a` found no MAJOR or MINOR and one NOTE. 28-A.1's
own review is in `fe8480b`. A review of `1ed1ba6` (an INF and gate change)
and of the merged build: TBD.

Closed: TBD.
