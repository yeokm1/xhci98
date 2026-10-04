# Phase 27 Record - External USB 2.0 hubs inside the bus

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 27 - External
USB 2.0 Hubs Inside the Bus", and behind 28-A.2, which the owner moved to the
start of Phase 27 (2026-10-03). The roadmap entry carries the goal, the
status, the task list and the checkpoint; this file carries what each task
did and what each reading said. Where the two disagree about a clause, the
roadmap wins.

**Written while the phase is open**, and kept as written rather than rewritten
in the past tense. A sentence saying something "is owed" describes the day it
was written; the roadmap's task list says how each task closed.

**On `out\...`, `vm\...` and `tools\...` paths in this file.** They say where
a reading was taken and what the file was called, on the host that ran it;
they are not files a clone has.

**How the guests were driven.** Every guest leg in this file ran on
development host A on 2026-10-04 under QEMU 11.1.0 (TCG; `qemu-xhci` with
eight USB 2.0 ports and no SuperSpeed ports), one guest at a time, each on a
fresh qcow2 overlay:
- **Windows 98 SE and Windows 2000 SP4** from the Phase 26 golden images
  (`vm\t26\win98-gold.qcow2`, `vm\t26\win2k-gold.qcow2`; `run-26.md`, "The
  golden images"), the build under test put over the installed driver as
  there; the matrix and soak overlays `vm\t26\win98-mx*.qcow2` and
  `win2k-mx*.qcow2`, the fix legs' `win98-p27fix*` and `win2k-p27fix*`.
- **Windows XP x64 SP2** from `vm\t27\winxp64-base.qcow2` (snapshot
  `winxp64-clean-autologon`), overlays `vm\t27\xp64-q1`, `xp64-r1` and
  `xp64-q2`, launched by `out\phase27\xp64\xp64.cmd` (TCG, `qemu64`, two
  virtual processors, 2048 MB).

The coordinating session launched and quit the QEMU processes of the GUI
legs; a GUI-driving subagent per guest drove the desktop through the QEMU
monitor (`device_add`, `device_del`, `mouse_move`, `sendkey`, `screendump`)
and wrote its notes and screenshots. The matrix and soak legs were driven by
a matrix subagent through `scripts\vm-matrix\run-matrix.ps1` and
`soak-11v.ps1`, which launch QEMU and drive the monitor themselves. The
`qemu` flavour's port-0xE9 trace went to a `-debugcon.log` beside each leg's
notes. A background shell task that runs QEMU is stopped by the agent harness
after 30 minutes unless it is given a longer limit; from the XP x64 leg's
second boot every launch was given 2 hours (`lessons.md`, "The agent
harness stops a background QEMU after 30 minutes").

**Whose read.** Each section names who read its evidence. Where it says a
subagent's read, the coordinator relayed that subagent's report and did not
re-read the evidence line by line; the docs subagent that wrote this file
checked the figures it quotes against the report files and traces named.

Opened 2026-10-04.

---

## Batch (a): 27-A.1, 27-A.2 and 27-A.4 - the hub class, topology and the host vectors

Three branches worked in parallel and merged into the integration branch
`p27-int` (`a47430a`, `f046340`, `e3d3df7`), then `fa6f199`:

- **27-A.4 first** (`74ac195`): `test_topo` grew 20 placement rows (Route
  String, tier, root port, parent and the TT triple), the depth limit, hub
  marking, removal sweeps and malformed hub descriptors, each citing design
  records 02, 12 and 13; `hub_port_vectors.h` holds 39 hub-port state-machine
  rows as data for the hub class. Three disagreements with `xhci_topo.c`
  printed as known gaps: **G1**, the multi-TT setting applied before the hub
  is known; **G2**, the node count; **G3**, `C_PORT_ENABLE` with enable 0 not
  read as a departure.
- **27-A.1 and 27-A.2** (`23e7715`): a hub is brought up by the bus and never
  gets a PDO (the Phase 26 quarantine ids go). In order: the topology attach
  and depth check; SET_CONFIGURATION; SET_INTERFACE(1) for a multi-TT hub;
  GET_DESCRIPTOR(Hub); one Configure Endpoint carrying Hub, the port count,
  TTT, MTT and the status-change endpoint; port power; a first GET_STATUS per
  port; the status-change transfer armed (polling after two failures). The
  hub's ports run the same `xhci_enum.c` machine as root ports, through hub
  class requests. A device behind a hub gets its Route String, root port,
  tier and TT fields from `xhci_topo.c`, through one Slot Context builder;
  its instance id is `(route << 8) | root port`. A departing hub takes its
  subtree down leaf-first and holds its own port until every PDO below is
  deleted. In `xhci_topo.c`: 16 nodes (G2), SET_INTERFACE on an attached node
  (G1), `C_PORT_ENABLE` with enable 0 as a departure (G3). The pure hub logic
  is `xhci_hub.c`, host suite `test_hub` (55 checks).
- **G1 to G3 closed** (`fa6f199`): after the merges the suite reported all
  four KNOWN GAP vectors (G1 twice, G2, G3) as holding, and each became a
  CHECK - `test_topo` 2202 to 2206 checks, no gap open. The `KNOWN_GAP`
  machinery stays for the next one. The counter block did not change, so the
  `offsets-hcd` tables regenerated identical.

The batch was read on guests only through 27-V.1's matrix and soak (below);
no GUI leg ran on its own build.

### 27-A.3 - removal, and Codex round 1

`b5ed0c4` made one subtree teardown, `hcdSubtreeGo` (`src\hcd_enum.c`), the
road every departure takes - a disconnect a root port or a parent hub
reports, a failed enumeration and CYCLE_PORT (`UNPLUG`); an HCRST that took
every slot (`TAKEN`); the root hub's removal (`DETACH`) - in design record 13
section 10.5's order as it then stood: freeze (hubs Draining, devices Gone),
stop endpoints and complete every URB DEVICE_GONE while the slot lives,
report PDOs and disable slots leaf-first, prune hubs deepest-first
(`XhciHubReleaseOrder`, checked by the host suite). The Windows 98 stall
below changed the order twice; section 10.5 now carries the order the code
has.

**Codex round 1** (`.claude\codex-p27-r1-result.txt`, not tracked; over
`23e7715` against `40efd31`): four MAJOR and five MINOR, every one from
reading, all taken in `b5ed0c4`:
1. (MAJOR) A port holding a device was read as quiet, so a draining hub
   could be released under it.
2. (MAJOR) An exhausted enumeration left an address-zero device enabled: a
   hub port now gets three attempts and then `CLEAR_FEATURE(PORT_ENABLE)`.
3. (MAJOR) TT recovery was missing: CLEAR_TT_BUFFER is now sent on EP0
   recovery, a bulk reset, an Address Device transaction error and the
   quiesce.
4. (MAJOR) A custom PSIV went into the Slot Context correctly and was then
   read as a speed for the endpoint rules; the decoded speed is used there.
5. (MINOR) A reset's completion now needs a fresh `C_PORT_RESET`.
6. (MINOR) A failed select behind a hub that was being unplugged counted as
   a failure; presence is read along the path first.
7. (MINOR) The debounce restarts on a change.
8. (MINOR) Hub reports had taken part of the control scratch; they have
   their own storage and the scratch is back to 4096 bytes.
9. (MINOR) The door reported an internal hub as disconnected; a served
   root-port hub is now reported (partially: devices behind hubs not yet).

The merge of `p27-a3` (`d54eef0`) met Phase 26's round-25 rule in
`hcdCfgCountSelect`: `HcdHubPathPresent` now treats a root `PORTSC` of all
ones as present - unreadable proves nothing, so the failure is counted -
while a hub that does not answer stays absent.

### Codex round 2

Codex's second round (`.claude\codex-p27-r2-result.txt`, over
`fa6f199..d54eef0` with both merge resolutions) judged round 1's findings 1
to 5, 7 and 8 fixed and 6 and 9 partial, and found one MAJOR and three MINOR,
all taken in `900dc83`:
1. (MAJOR) The teardown did not stop every doorbell: the isochronous
   underrun/overrun restart, the cancel restart, EP0 recovery and the
   thread's EP0 transfer now test Gone under the controller lock, beside the
   publish paths that already did, so an endpoint the teardown stopped stays
   stopped until its slot is disabled.
2. (MINOR) The presence check could hide a real select failure; it now
   suppresses one only on a confirmed departure (a GET_STATUS reading
   disconnected, disabled or changed, or a departing hub).
3. (MINOR) A served hub on a root port reported `DeviceIsHub` TRUE beside an
   empty GET_NODE_CONNECTION_NAME; it reports FALSE until hub traversal
   exists.
4. (MINOR) The debounce's 1.5 s give-up is an elapsed-time deadline.

Of its notes, the CLEAR_TT_BUFFER comment was corrected (a busy TT buffer is
not reused, USB 2.0 11.17.5) and the request is retried once short of a
STALL. No third round over `900dc83` is recorded; the fix rounds below read
the teardown again.

`829a4a6`, held from Phase 26 so that phase closed on the binary its clause
legs read, answers a SET_INTERFACE the device STALLs with `STALL_PID` rather
than an internal controller error (26-V.2's EP0 stall storm).

---

## 28-A.2 on Windows XP x64

The amd64 HCD readied for a guest (`dff49ae`): compile-time checks that the
isochronous block's and the SG list's overflow arrays follow their declared
tails on both architectures; `HcdDmaOpen` refuses a common buffer above 4 GB
(the controller is programmed with the low dword), the scratch buffer falling
back to NULL; stale scaffold and miniport text gone from both INF headers, the
amd64 allowlist header and `make-package.ps1`'s amd64 warning.

A GUI subagent's legs; its notes `out\phase27\xp64\q1-notes.md` (q1, r1 and
q2) and screenshots `out\phase27\xp64\shots\`, as read.

**q1** - the `qemu` flavour of the `p27-amd64` worktree at `dff49ae`
(`make-package -Flavor qemu -Arch amd64`; `xhci98.sys` 248,832 bytes, SHA-256
`2cf952e4...040a`), the transfer drive E:.
- **Install from clean.** The Found New Hardware Wizard for "Universal Serial
  Bus (USB) Controller", Have Disk `E:\`: "This driver is not digitally
  signed!", then the Logo prompt ("... has not passed Windows Logo testing
  to verify its compatibility with this version of Windows."), Continue
  Anyway; the same for "xHCI98 USB 3.x Root Hub". No CD prompt and no
  restart prompt. Both "This device is working properly."; the controller's
  files `usbd.sys` (5.2.3790.1830), `xhci98.sys` and `usbui.dll`, Driver
  Version 1.99.0.0.
- **The trace** shows a 64-bit kernel counter block (`counters VA
  high=FFFFFADF`, `low=CE2BBAE8`), `No Op self-test completion code=00000001`
  and the root hub PDO and FDO started.
- **Mouse.** `usb-mouse` at 480 Mb/s: "USB Human Interface Device" and
  "HID-compliant mouse" with no wizard, and `mouse_move` moved the pointer;
  the unplug was clean.
- **Storage.** `usb-storage` on a `fat:` drive: F:, and `fc /b E:\RAND.BIN
  F:\RAND.BIN` gave "FC: no differences encountered"; the unplug was clean.
  The trace has `QUERY_INTERFACE not answered, GUID Data1=B1A96A13`
  (`USB_BUS_INTERFACE_USBDI_GUID`), `version/size=00010048`; storage worked
  regardless.
- **Audio.** `usb-audio` (46F4:0002, 12 Mb/s): "USB Audio Device", "(Generic
  USB Audio)", "This device cannot start. (Code 10)". The trace: one
  function PDO for interfaces 0 and 1 (`interface mask=00000003`,
  `class/subclass/protocol=00010100`), SELECT_CONFIGURATION and
  SELECT_INTERFACE served, then `QUERY_INTERFACE not answered, GUID
  Data1=B1A96A13`, `version/size=00000040`, a deconfigure and REMOVE - the
  `USB_BUS_INTERFACE_USBDI` gap 28-A.1 owns (the agent's reading: consistent
  with it, not proven the sole cause).
- **The controller's Advanced tab**, verbatim in part: "Each USB controller
  has a fixed amount of bandwidth, which all attached devices must share.",
  one row `System reserved | 10%`, and "Don't tell me about USB errors"
  (clear). **The root hub's Power tab**: "The hub is self-powered.", "Total
  power available:  500 mA per port", `8 port(s) available | 0 mA`.
- **`XHCISNAP -probe`**, the 32-bit tool under WOW64: the four readings
  of c17 (`run-26.md`): `status  6
  (MINIPORT DECLINED)` ("the request reached a miniport and it DECLINED. The
  ROUTE WORKS."), `2 (invalid request code)`, `4 (invalid header parameter)`,
  `7 (buffer too small)`.
- **Disable, enable, uninstall and rescan.** Disable: "Disabling this device
  will cause it to stop functioning. Do you really want to disable it?", the
  controller red-crossed and the root hub gone, no restart prompt; the trace
  QUERY_REMOVE and REMOVE, "root hub detaching, dropping every device",
  `teardown: ports unpowered=00000008`, `quiesce: halted, USBSTS=00000001`.
  Enable: both back, a new start and a second No Op self-test passing.
  Uninstall: "Confirm Device Removal", both gone. Scan for hardware changes:
  the wizards and Logo prompts again (the package is unsigned), both back. A
  mouse after it moved the pointer.
- **Shutdown**, twice: `paused (shutdown)`, the trace ending
  `quiesce: halted`, `save: declined - the controller does not declare FSC`.
  No bugcheck in either boot.

**r1** - the `release` flavour (`make-package -Flavor release -Arch amd64`,
import gate and INF gate passed; `xhci98.sys` 124,928 bytes, SHA-256
`cab1db08...0111`), a fresh overlay. The same install route and prompts; the
controller and root hub working; the mouse moved the pointer, `fc /b` on the
stick "no differences encountered", the audio device Code 10 as on q1; all
three unplugged with no dialog or bugcheck; a clean shutdown. The trace file
is empty, as a `release` build has no trace channel.

**q2** - with 28-A.1, the `qemu` flavour of branch `p28-a1` at `fe8480b`
(`xhci98.sys` 254,464 bytes, SHA-256 `a7731448...6200`), a fresh overlay.
**28-A.1 is not in this branch**; the leg is here because it closes q1's
audio row.
- The same install from clean, both devices working.
- `usb-audio`: "USB Audio Device", "(Generic USB Audio)", "This device is
  working properly." (Code 10 on q1). The trace: `hcd: USBDI interface asked,
  version/size=00000040` (answered), SELECT_INTERFACE to alternate 1
  (`endpoint mask` 4), isochronous URBs, `iso packets answered` climbing;
  storage asked for version 1 (`version/size=00010048`).
- **Playback**: a 20 s, 48 kHz, 16-bit stereo 440 Hz `TONE20.WAV`, copied to
  C: over the stick (`fc` clean), `mplay32 /play /close`: the player's
  position read 45.30 at 1.45 s of host time and 62.60 at 18.94 s, then the
  player closed at the end - 17.49 s of position in 17.30 s: real time. Only
  the guest's position was read; the guest has no audio path out.
- Mouse, storage, disable and enable, and a second audio plug, all as
  before; a clean shutdown, no bugcheck.

---

## The Windows 98 SE HID-unplug stall, and its fix

**Found by the matrix subagent** in 27-V.1's first runs on the `p27-int`
build after Codex round 2 (`7fddbac`, by the package's build time): on
Windows 98 SE every root-port HID unplug stalled the guest for minutes. Its
traces: `out\mx27\wedge1-h98-debugcon.log` and `wedge2-`, on the overlay
`vm\t26\win98-mxb-wedge1.qcow2`; then, on `26e7cb6`'s package, a timed trace
of one keyboard unplug, `out\mx27\stall-h2-timed.txt` (seconds from the
port change), verbatim:

```
+0s xhci98: event: port status change on port=00000002
+0s xhci98: command: completion matched outstanding TRB=0FEE10C0
+0s xhci98: command: completion matched outstanding TRB=0FEE10D0
+151s xhci98: transfers cancelled=00000004
+660s xhci98: hcd: device PDO PnP minor=00000002
+660s xhci98: hcd: device PDO PnP minor=00000007
+660s xhci98: hcd: device PDO PnP minor=00000013
```

The REMOVE (minor 2) came eleven minutes after the unplug; the legs between
read 129 s to 11 min.

**The loop.** The teardown completed the device's pending interrupt reads
with `STATUS_DEVICE_NOT_CONNECTED` / `USBD_STATUS_DEVICE_GONE`. Windows 98
SE's `hidclass.sys` answers that status, while its device is still started,
by failing every client read and resubmitting at once (`0x110A7`; the state
test at `0x10C60`, static - `run-26.md`, "The REMOVE that never ended"); each
resubmission is refused DEVICE_GONE at the next tick, and the retries kept
the guest busy enough that its configuration manager did not act on the
relations change - the REMOVE that ends the started state came only minutes
later. Phase 26 had reported the PDO missing first and failed the URBs only
at the Disable Slot after it, and did not stall; 27-A.3's teardown had put
the URB completion first.

**The fix, in its iterations** (guest legs by GUI subagents on fresh golden
overlays; traces `out\phase27\fix\p27fix<n>-<os>-debugcon.log`, screenshots
`out\phase27\fix\shots\`; each leg's build was the working tree of the
commit it names, packages `out\phase27\fix\pkg-p1` to `pkg-p3`, and the
committed packages `pkg-final` to `pkg-final5`):
1. **`26e7cb6` - report first. Did not cure it.** `hcdSubtreeGo` reports each
   device's PDOs missing at the freeze (`hcdReportGone`), before the Stop
   Endpoint and the drain; the hardware order is unchanged. The stall stayed
   (the timed trace above, and the control leg `p27fixc-h98`).
2. **`f99f184` - park, do not fail.** A URB IRP whose device leaves while it
   is queued, on the ring or waiting for a record is held on its PDO,
   cancellable (`HcdIoPark`), and completed CANCELLED only when the client
   aborts a pipe or cancels it, or the PDO is stopped, surprise-removed,
   removed or deleted (`HcdIoParkedRelease`). A URB submitted after the
   device left is still refused DEVICE_GONE at the next tick. A read that
   never fails puts `hidclass.sys` in no retry state. Windows 98 SE
   (`p27fix1-h98`): a keyboard unplugged three times, a mouse and a stick -
   the REMOVE within about 1 s each, the Start menu responsive; the control
   (`26e7cb6`'s package, `p27fixc-h98`) stalled on the same repro. Windows
   2000 (`p27fix2-h2k`): SURPRISE_REMOVAL and REMOVE within about 1 s.
3. **`6dae92b` - ABORT_PIPE on a departed PDO** (Codex fix round 1, two
   MAJOR): an ABORT_PIPE on a PDO whose device had left took the generic
   gone-device refusal and released nothing; it is now answered first,
   completing that pipe's held IRPs CANCELLED. And a request the teardown
   reached only after the abort had released the list was held for good;
   every ABORT_PIPE now marks its pipe aborted on the PDO. Guest legs
   (`p27fix3-h98`, `p27fix4-h2k`): the same Windows 98 SE unplugs, the REMOVE
   within about 1 s each; Windows 2000 a keyboard, and a stick pulled
   mid-copy of a 256 MB file (3 transfers cancelled, the copy failed
   cleanly), SURPRISE_REMOVAL and REMOVE within about 1 s.
4. **`ed025d2` - abort horizons by submission sequence** (round 2, two MAJOR
   and one MINOR): a later submission had cleared an abort mark and stranded
   a request submitted before the abort. Every URB IRP is now stamped at
   dispatch with the PDO's next submission sequence (`HcdIoStamp`,
   `DriverContext[0]`); an ABORT_PIPE records its own stamp as its pipe's
   horizon (`HcdIoAbortMark`), and a request at or below it completes
   CANCELLED. The pipe comes from the record or the wait list, never
   re-parsed from a completed URB (an isochronous URB whose length the
   completion set to 0 had lost its handle). Horizons are never evicted: up
   to 32 handles per PDO, beyond which the every-pipe horizon is raised.
   Guest legs (`p27fix5-h98`, `p27fix6-h2k`): as before, plus Windows 2000's
   `usb-audio` unplugged idle - REMOVE within about 1 s.
5. **`09ed9d1`, `612f05a`, `53b43c0` - the sequence made wrap-proof**
   (rounds 3 to 5). Round 3 (one MAJOR): the 32-bit submission count wraps
   and the ordering fails both ways; `09ed9d1` ordered by signed difference
   and aged horizons 2^30 behind. Round 4 (one MAJOR): a horizon kept through
   a full lap read young again; `612f05a` made the count and every horizon
   64-bit (`XHCI_SEQ64`, a Lo/Hi pair - the tree does no 64-bit arithmetic),
   rebuilt each IRP's stamp from the low word it keeps (`XhciSeqFromStamp`)
   and compared with a 64-bit `<=` (`XhciSeqCovers`), with no aging. Round 5
   found no MAJOR, and one MINOR (the documented aliasing bound was off by
   one) and one NOTE (a request past the bound is not necessarily held until
   its PDO goes), both taken in `53b43c0`: a stamp rebuilds exactly while
   fewer than 2^32 - 1 submissions followed it, with `test_pipe` vectors at
   the boundary. No round over `53b43c0` is recorded.

The Codex rounds are `.claude\codex-p27fix-r1-result.txt` to `-r5-`, not
tracked. `18ecc83` merged the fix into `p27-int`. The last guest legs ran on
`ed025d2`'s build; the matrix's last Windows 98 SE run on `09ed9d1`'s
(below). `612f05a` and `53b43c0` change only the stamp arithmetic and its
host vectors, and no guest leg ran on them.

### Windows 2000: the audio device unplugged during playback

On `ed025d2`'s build (`p27fix6-h2k`), `usb-audio` unplugged while it played
(isochronous streaming): SURPRISE_REMOVAL, ABORT_PIPE, then **no REMOVE within
minutes**, the desktop responsive. The same repro on `26e7cb6`'s package
(`p27fixc2-h2k`) read the same; and on **`a7ddbfa`, the build Phase 26 closed
on** (`out\phase27\fix\p27fixc3-h2k-p26-debugcon.log`), the trace ends,
verbatim:

```
xhci98: event: port status change on port=00000003
xhci98: hcd: root hub FDO PnP minor=00000007
xhci98: hcd: root hub PDO PnP minor=00000007
xhci98: hcd: device PDO PnP minor=00000007
xhci98: hcd: device PDO PnP minor=00000007
xhci98: hcd: first URB of function=00000002
xhci98: command: completion matched outstanding TRB=01634090
xhci98: hcd: device PDO PnP minor=00000017
xhci98: hcd: root hub FDO PnP minor=00000007
xhci98: hcd: root hub PDO PnP minor=00000007
xhci98: transfers cancelled=00000003
```

- function 0x02, ABORT_PIPE; minor 0x17, SURPRISE_REMOVAL; no minor 2 after it.
- **The defect predates Phase 27**: it is in Phase 26's build, and neither the
  report-first change nor the parking caused or cured it.
- It is an open limitation, carried to 28.3's list.
- An idle audio unplug is clean on every build read.

---

## The behind-hub replug that made a new devnode

While the Windows 98 SE golden image was prepared for the hub rows, a mouse
replugged behind a hub raised the Add New Hardware Wizard again, as if a new
devnode had been made at a location the image had already seen. A subagent's
investigation, 2026-10-04, on overlays of the golden image
(`vm\t26\win98-inst1.qcow2`) and of the first matrix image (`win98-inst2`),
the package `out\phase27\inst\pkg0` (`11bd8bbe...a830`); traces
`out\phase27\inst\inst1-gold-debugcon.log`, `inst2-mxf-debugcon.log`,
screenshots `out\phase27\inst\shots\`; not re-read line by line by the
coordinator.

What it found:
- The HCD gives each location its own instance id, `(route << 8) | root
  port`. The image's `Enum\HID` exports (screenshots `inst1-h2`, `inst2-h7`)
  hold one `HID-compliant mouse` devnode per location, under the instance-id
  suffixes `3` (root port 3), `258` (0x102: route 1, root port 2), `770`
  (0x302) and `4610` (0x1202).
- So a location is taught once, by the wizard its first device raises, and
  a replug at a taught location reuses its devnode.
- The new devnode was the image's, not the HCD's. The prep had unplugged
  devices before the HID child of every location had finished installing,
  and a modal "Insert Disk" for the Windows 98 CD - the install source
  pointing where the CD files were not - had stopped all PnP behind it.
- The prep was redone (`prepi`, then `prepj`) with Setup's `SourcePath` set
  to where the CD files are, `"SourcePath"="E:\\WIN98\\"` under
  `HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Setup`
  (screenshot `out\mx27\shots\i-reg.png`), and every location's HID child
  installed before its unplug. That image is `h98j` below.

A location the image has not seen still raises the wizard, and the bind waits
on it. The port's enumeration waits too: a port holds its next device until
PnP has deleted the last one's PDO. That wait is per port and by design; the
other ports enumerate meanwhile (the h98f soak below).

---

## 27-V.1 - the device matrix and the soak

The matrix subagent's runs, 2026-10-04, through `run-matrix.ps1` with
`matrix-hcd.psd1` (17 rows; the `hub` group joins every Phase 27 run) and
`soak-11v.ps1 -Driver hcd`. This is its report as the coordinator summarised
it, checked by the docs subagent against the report files named. Reports:
`out\mx27\<target>\device-matrix-<target>.txt`,
`out\mx27\soak-<target>\soak-<target>.txt`.

**The packages**, matched by SHA-256 between the staging directories
(`vm\mx27*-stage`) and the packages; the commit is the one each was built
for, by build time:

| Letter | Package | `xhci98.sys` SHA-256 | Built for |
|---|---|---|---|
| b | `out\phase27\pkg-h1` | `3c9b493e...a84c` | `7fddbac`, `p27-int` after Codex round 2 |
| c | `out\phase27\pkg-h2` | `ae6cd473...cf87` | `26e7cb6`, report first |
| f | `out\phase27\fix\pkg-final` | `cd564373...03e8` | `f99f184`, parking |
| h | `out\phase27\fix\pkg-final3` | `a8792491...275c` | `ed025d2`, horizons by sequence |
| j | `out\phase27\fix\pkg-final4` | `9a3af916...7ca4` | `09ed9d1`, wrap-safe order |

The targets: `h2k<letter>` and `h98<letter>`, overlays of the golden images;
`h98j` an overlay of the re-prepared Windows 98 SE image (above).

### The matrix

| Run | Rows | Reading |
|---|---|---|
| `h2kb-full` (b) | all 17 | every row PASS, or NODRIVER where the row expects no driver (`usb-uas`, `usb-net`, `usb-serial`, `usb-braille`, `usb-ccid`, `u2f-emulated`); `usb-hub/fs` and `usb-hub/churn` PASS |
| `h2kb-hub` (b) | hub | `usb-hub/fs` PASS; `usb-hub/churn` FAIL on one expectation, `-> FAIL advance topology: behind-hub opens == 5 +10` - the expectation, not the HCD (below) |
| `h2kc`, `h2kf`, `h2kh` (c, f, h) | hid, storage, hub | every row PASS, `usb-uas/fs` NODRIVER as expected; the churn PASS |
| `h98f-full`, `h98h-full` (f, h) | all 17 | every row PASS or NODRIVER as expected (`u2f-emulated/fs` PASS, the image having taught it); `usb-tablet/hs` and `usb-wacom-tablet/fs` EXCLUDED as in Phase 26; `usb-hub/fs` PASS; `usb-hub/churn` EXCLUDED, "behind-hub device instances are not taught to this image; each raises a modal wizard that blocks the bind - prep them at their hub ports first" |
| `h98fh-hub`, `h98hh-hub` (f, h) | hub | `usb-hub/churn` FAIL: `advance hubs started by the bus == 6 +2`, `advance devices addressed == 11 +5` - the old image's modal wizard and Insert Disk, not the HCD (above) |
| `h98jh-hub` (j, re-prepared image) | hub | **both rows PASS**: `advance hubs started by the bus == 6 +6`, `advance topology: behind-hub devices addressed == 10 +10`, `advance topology: behind-hub opens == 10 +10`, `advance endpoints opened == 5 +5`, `zero topology: behind-hub refused - too deep 0`, `zero topology: TT pairs programmed 0` |

`usb-hub/churn` is the five-tier chain: a hub on root port 2, a mouse
plugged, unplugged and moved behind it, a hub with a mouse at `2.2`, and hubs
at `2.1`, `2.1.1`, `2.1.1.1` and `2.1.1.1.1` with a mouse at `2.1.1.1.1.1` -
five hubs above it, the depth USB allows, so `too deep` stays zero; then the
first hub pulled with the chain beneath it. Without the miniport's virtual
hub that tier-5 mouse is addressed, and on `h98jh` and every Windows 2000 run
it bound: five mice opened their endpoint (`endpoints opened` 5). Every speed
expectation held: the hubs and the mice behind them decoded and programmed
Full Speed (`advance port speed decoded - full speed == 11 +11`,
`zero slot speed disagreeing with port speed 0`).

The `behind-hub opens` expectation was 5 in the first runs; the reading of
10 is the five mice and the status-change pipes of the five hubs below the
first. `cd38a98` sets it to 10, adds the churn's PASS vector and three FAIL
vectors to `selftest.ps1` (467 checks), and the later runs read it.

### The soak

| Run | Classes, cycles each | Hub churn (120 attach/detach pairs, 600 ms apart, storage resident) | Verdict as written |
|---|---|---|---|
| `soak-h2kb` (b) | hid, kbd, tablet, wacom, u2f, ccid, storage, hub, hubmouse; 25 | 120 of 120 enumerated, IDE IRQ 14 7129 -> 7194, alive | PASS |
| `soak-h2kc` (c) | storage, hid, hubmouse, hub, audio; 25 | 120 of 120, IRQ 14 moved, alive | PASS |
| `soak-h2kf` (f), `soak-h2kh` (h) | storage, hid, hubmouse, hub; 10 | 120 of 120, IRQ 14 moved, alive | PASS |
| `soak-h98f` (f) | hid, kbd, storage, hub, hubmouse, u2f, ccid, audio; 25 | 120 of 120, IRQ 14 25911 frozen, alive | FAIL on the IRQ 14 clause alone |
| `soak-h98h` (h) | hid, kbd, storage, hub, u2f, ccid, audio; 25 | 0 of 120, IRQ 14 frozen | FAIL - after the audio cycles' fatal exception (below) |
| `soak-h98j` (j, re-prepared image) | hubmouse, hid; 25 | **120 of 120 enumerated, 0 refusals, in 492 s**, IRQ 14 18966 frozen, alive | FAIL on the IRQ 14 clause alone |

- **Every cycle completed on every run**, and every settled reading showed
  the same stable gap of 2 (submitted less completed less cancelled), which
  the harness itself reads as "the stage E shape - parked interrupt IN
  requests on HID and hub status-change endpoints".
- On Windows 2000 every class addressed a device on each cycle (devices
  addressed +25 or +10 per class).
- On Windows 98 SE each cycle of hid, kbd, storage and hub addressed a
  device on `h98f` and `h98h`, and of u2f and ccid on `h98h`. Each cycle of
  hubmouse and hid did on `h98j`. On `h98f` the classes after hub addressed
  almost nothing (hubmouse +1, u2f, ccid and audio +0): the behind-hub
  mouse's first plug on that untaught image raised a modal wizard, and the
  port waited on it - the per-port wait above. The churn on root port 3 went
  on enumerating meanwhile.
- **The churn on Windows 98 SE.** 120 of 120 hubs were enumerated on `h98j`
  and on `h98f`, and the guest stayed responsive. On `h98j`, after the churn,
  the matrix subagent ran a directory listing in a DOS box and it answered at
  once (screenshot `out\mx27\shots\j-alive.png`). The harness's own liveness
  check read "alive" on both: interrupts climbing, about 990 over 1500 ms.
  Under the miniport this churn wedged the guest at 12 and 18 enumerations
  (`lessons.md`, "Task 12.5's control"). It is not wedged under the HCD.
- **IDE IRQ 14.** The harness had asserted that IRQ 14 moves across the
  churn, task 12.5's wedge signature. On Windows 98 SE it stood still while
  the guest was demonstrably alive: the disk is idle during the churn, and
  the guest answered a directory listing at once afterwards. `cd38a98` turns
  the clause into a NOTE with that reason. The `h98f` and `h98j` verdicts
  above were written before that change, and their only FAIL is this
  clause.
- **Audio on Windows 98 SE.** On `h98h` the audio class addressed one device
  (+1), and the run ended at "A fatal exception 0E has occurred at
  0028:C002A3A7. The current application will be terminated."
  (`out\mx27\shots\h98h-soak-end.png`, and `soak-h98h-churn-after.png`). The
  churn after it enumerated nothing.
  - The screen names no module. The matrix subagent's reading is
    `USBAUDIO.VXD`, the known guest limitation: Windows 98 SE's
    `USBAUDIO.VXD` faults after one URB through this driver and through a
    UHCI control alike (`lessons.md`, "Windows 98 SE cannot play USB
    audio here").
  - The matrix treats that traffic as inert, which is why `usb-audio/fs`
    passes there.
  - Audio cycles on Windows 98 SE are therefore not a soak class. The `h98j`
    soak ran without them.

The soak's IRQ 14 clause and the hub-churn phase with resident storage are
`cd38a98`'s, with the soak's new classes (`kbd`, `tablet`, `wacom`, `ccid`,
`u2f`, `hub`, `hubmouse`) and `-Driver hcd`.

---

## The closing legs

Taken on development host A on 2026-10-04, after `774c194` wrote the
sections above, on the builds each part names. A GUI subagent per guest drove
the legs and a matrix subagent the soak, as before; the coordinator relayed
their reports. The docs subagent that wrote this section checked the counter
dumps, the package hashes and the REMOVE sequences it quotes against the files
named; the rest is the subagents' reading, not re-read line by line.

**The packages behind the fix rounds and the close**, extending the matrix
table above (the commit is the one each was built for):

| Package | `xhci98.sys` | Built for |
|---|---|---|
| `out\phase27\fix\pkg-final` | `cd564373...03e8` | `f99f184`, parking |
| `out\phase27\fix\pkg-final2` | `4674287b...da44` | `6dae92b`, ABORT_PIPE on a departed PDO |
| `out\phase27\fix\pkg-final3` | `a8792491...275c` | `ed025d2`, horizons by sequence |
| `out\phase27\fix\pkg-final4` | `9a3af916...7ca4` | `09ed9d1`, wrap-safe order |
| `out\phase27\fix\pkg-final5` | `73710569...a541` | `612f05a`, 64-bit count and horizons |
| `out\phase27\pkg-close` | 150,715 bytes, `8c555a8a...1020` | `981f56b`, hub-port suspend handled, not initiated |
| `out\phase27\pkg-close3` | 152,107 bytes, `2c3297f0...c529` | `6f2e1ce`, the hub-port resume |
| `out\phase27\pkg-audio98` | 152,255 bytes, `d276dfef4e1430d7089ea383de68cc38d5d2b139fdfffc54c8a07a724512c2be` | `c038326`, the removed-PDO fix |

### Full-Speed audio bound, at a root port and behind the hub

On `pkg-close` (`981f56b`). Binding, not playback, as the checkpoint clause
reads. Evidence: the counter dumps `out\mx27\k-2k-base.txt`,
`k-2k-audio-root.txt`, `k-2k-audio-hub.txt`, `k-98-audio-root.txt` and
`k-98-audio-hub.txt` (each the counter block before and after the leg's
action); the traces `k-2k-legs-debugcon.log`, `k-98-legs-debugcon.log`,
`k-98-audio-fault-debugcon.log` and `k-98-audio-hub-wedge-debugcon.log`; the
screenshot `k-98-audio-fault.png`.

- **Windows 2000 SP4, root port 2.** The composite device split as in Phase
  26; the AudioStreaming interface selected at alternate 0, then alternate 1
  when Sound Recorder (`sndrec32`) played. `endpoints opened` read 1 with the
  keep-alive mouse alone, 2 with the audio function bound and 3 after
  alternate 1; `iso packets answered` 660; the isochronous missed-service and
  packet-error counters 0.
- **Windows 2000 SP4, behind the hub** (`usb-hub` on root port 2, the audio
  device at `2.1`). The same, with `iso packets answered` 660 to 1320 (+660)
  and the isochronous error counters 0. Topology: `hubs started by the bus`
  1, `hub descriptors folded` 1, `hub slots marked` 1, `behind-hub devices
  addressed` 1, `behind-hub opens` 1; no TT pairs programmed (a Full-Speed
  hub has no TT).
- **Windows 98 SE, root port.** Bound: the function PDO at class, subclass
  and protocol 01/01/00, the AudioStreaming interface at alternate 0, and
  the volume icon in the tray. The first unplug then gave "A fatal exception
  0E has occurred at 0028:C002A3A7", the trace ending at the device PDO's
  REMOVE: the use after free below, fixed in `c038326`.
- **Windows 98 SE, behind the hub.** Installed and bound (`behind-hub devices
  addressed` 1, `endpoints opened` 1 to 2), then wedged within a minute: the
  pre-existing audio-load wedge below.

### The orderly path with a hub subtree beneath it

On `pkg-close`. A `usb-hub` on root port 2, a mouse at `2.2` and a stick at
`2.3` (`out\mx27\k-2k-sub-before.txt`, `k-98-sub-before.txt`); the `-mark`
files hold the trace's line count at each leg's start; the traces as above
and `m-2k-debugcon.log`, `m-98-debugcon.log`. The transfer identity is
submitted = completed + cancelled.

- **Windows 2000, the root hub's Disable.** QUERY_REMOVE then REMOVE for
  every device beneath it; the identity 575 = 569 + 6; Enable brought every
  device back.
- **Windows 2000, the controller's Disable and Enable.** Clean; then a 4 MB
  copy to the stick and `fc /b` clean.
- **Windows 2000, the controller's Uninstall, then Scan for hardware
  changes.** Clean; `fc /b` clean.
- **Windows 98 SE, the root hub's "Disable in this hardware profile".** STOP,
  not REMOVE; the identity 68 = 64 + 4; re-enabled, START.
- **Windows 98 SE, the controller's disable and enable.** Clean; `fc /b`
  clean.

This is 27-A.3's orderly path with a hub beneath it, which the sections above
recorded as not read.

### The Windows 98 SE audio-unplug fatal 0E, and its fix

**The cause: the device PDO used after it was freed.** `hcdPdoRemoved`
deleted a gone PDO inside its `IRP_MN_REMOVE_DEVICE`. Windows 98 SE's
configuration manager then sends that PDO two more IRPs in the same removal
pass. With a devnode that already existed, the audio stack opens the device,
the freed memory is reused before those IRPs arrive, and the guest faults.

- **The IRPs after the REMOVE.** `c038326`'s message records diagnostic
  builds that kept the PDO reading "REMOVE -> minor 7 type 0 -> minor 13" on
  every unplug and surviving 6 of 6: minor 7 is QUERY_DEVICE_RELATIONS, type
  0 BusRelations. **Minor `0x13` is `IRP_MN_QUERY_ID`**: the trace prints the
  minor in hex (SURPRISE_REMOVAL reads `00000017`), and the Windows 2000
  DDK's `wdm.h` defines `IRP_MN_QUERY_ID` as 0x13 and
  `IRP_MN_QUERY_PNP_DEVICE_STATE` as 0x14. `c038326`'s message and its
  comment in `src\hcd_pdo.c` call the second IRP QUERY_PNP_DEVICE_STATE; the
  traces read here do not bear that out, and no minor `0x14` follows a
  REMOVE in them.
- **Every removed PDO gets them**, not only audio's: the HID and hub-mouse
  legs below read REMOVE, `0x07`, `0x13` on every unplug too. Only audio
  faulted, because only there was the memory reused first.
- **Not a Phase 27 regression.** The delete inside the REMOVE is `27063e1`
  (Phase 26 batch (b)); `38635f6`'s composite split is what exposed it.
  `pkg-close`, `pkg-close3` and `pkg-final` (`f99f184`) all fault at
  0028:C002A3A7.

**The fix, `c038326`.** A gone PDO's REMOVE moves it to `hc->RemovedPdos`; it
is deleted at the next root-hub BusRelations answer (`hcdReapRemoved`) or by
`HcdDevicePdoReleaseAll`. Windows 2000 sends nothing after a REMOVE, so there
the object is only held a little longer. Codex reviewed it with no findings.
Package `out\phase27\pkg-audio98` (above).

**Verification**, Windows 98 SE under NUSB 3.3 except the Windows 2000 row,
TCG; per-leg traces, drive logs and screenshots `out\phase27\audio98\<leg>-*`:

| Leg | Result |
|---|---|
| An audio device with an existing devnode unplugged and replugged, 10 cycles on each of two guests (`fixa`, `fixb`) | 20 of 20 PASS; on each guest 10 REMOVEs, each followed by minors `0x07` and `0x13`, the PDO deleted at the next relations answer |
| The baseline, `pkg-close3`, the same leg (`close3`) | fatal 0E on cycle 1 (`close3-drive.txt`: "cycle 1 : REMOVE at line 253, lines after it 0, screen BLUESCREEN") |
| HID, 5 cycles (`fhid`) | PASS |
| A mouse behind the hub, 5 cycles on `win98-mxj` (`fhmj`) | PASS |
| Windows 2000 audio, 4 unplugs (`w2k`) | PASS: SURPRISE_REMOVAL then REMOVE, every replug started |
| A fresh install (`ffresh`), and a first install behind a hub with an idle minute (`fhub`) | Inconclusive: both met the audio-load wedge below before any unplug |
| Audio playing during an unplug | Not run: Windows 98 SE plays no USB audio in QEMU (`lessons.md`, "Windows 98 SE cannot play USB audio here, and the control is what says so") |

### The Windows 98 SE audio-load wedge

Pre-existing and above the HCD; carried to 28.3 as a limitation.
- **Symptom.** SELECT_INTERFACE to alternate 0, then QUERY_CAPABILITIES, then
  the configuration manager makes no further progress and the taskbar clock
  stops.
- **Not the HCD's.** Every IRP sent to the function PDO completed, and
  nothing was outstanding at the HCD.
- **Seen on every build tried**: Phase 26's `pkg-v` (`40efd31`), `pkg-c16`,
  `pkg-h1`, `pkg-final` and the fix build.
- **Timing-dependent.** Attaching about 40 s after boot wedged 5 of 6; a
  120 s wait after boot gave 0 of 20.
- **The miniport had it too.** The same intermittent wedge on an audio
  replug after a cold boot: `1.1.0.0` 2 of 10, `1.1.1.0` 5 of 10 over its
  three settings (`lessons.md`, "Windows 98 wedges when a USB audio device is
  replugged after a cold boot, and it is not this release's doing").

### `pkg-close3`: the regression legs for the hub-port resume

On `6f2e1ce`.
- **Windows 2000.** Hubmouse 10 of 10 PASS; a mouse and a stick behind the
  hub, `fc /b` clean.
- **Windows 98 SE.** Hubmouse PASS, but 5 of 10 replugs were missed in the
  early cycles, about 40 s after boot; a controlled rerun read 12 of 12, so
  this reads as a boot-time artefact. A mouse and a stick behind the hub,
  `fc /b` clean.

### The hub-port resume

`e0c7617` and `6f2e1ce` are read on host vectors only, Codex rounds 5 and 6
clean. No guest has exercised a hub-port resume, because the bus initiates no
suspend (the owner's ruling, 27-A.1).

### The soak, on `pkg-final4`

`soak-h98j` (hubmouse 25 of 25, hid 25 of 25, the 120-hub churn 120 of 120)
and the `h98jh` churn row PASS, as in 27-V.1 above. The IDE IRQ 14 clause is
a NOTE since `cd38a98`, because the disk is idle during the churn.

Closed 2026-10-04.
