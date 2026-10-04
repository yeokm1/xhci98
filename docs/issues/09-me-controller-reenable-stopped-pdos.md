# Issue 9 - Re-enabling the controller on Windows ME with a USB mouse attached stops the shell: the HCD reported the stopped devices gone and replaced them, where Microsoft's hub driver keeps them

Status: **a known limitation of `2.0.0.0`; fixed in source for `2.1.0.0`
and not yet read on that release.** The fix is roadmap task 33.1, commit
`fa2af9b` on branch `p28-reenable`, merged at `a9a7577` (2026-10-04 20:08).
Its legs passed on the `qemu` package `out\phase28\pkg-reenable1` the same
evening (section 8): the Windows ME re-enable with a mouse attached 3 of 3,
the re-enable with a mouse and a stick, and the controller disable and
enable with a soak on Windows 98 SE and on Windows 2000. **The same legs on
the `2.1.0.0` package are owed** (section 9), and until they are read the
README's and the release notes' `TODO(33.1 legs)` markers stand and
`make-release.ps1` refuses the cut. Nothing on this page says the fix is
verified on a release.

Target affected: Windows ME, a target supported in virtual machines only,
under SweetLow's USB 2.0 stack (`AGENTS.md`, "Project Purpose"). Every
reading on this page is a QEMU reading; ME has never run on real hardware in
this project. Builds affected: the `2.0.0.0` release's source (read as Package B) and
`1ed1ba6` before it, in the two shapes section 5 records; no earlier HCD
build was read on this clause. Not the miniport
(`1.2.0.0` and earlier), which `usbport.sys` and Microsoft's hub driver
stood above. Windows 98 SE took the same wrong lifecycle and recovered from
it (section 4.4); on the NT targets a disable does not reach it, as far as
it was read (section 7).

The short version: Windows 98 SE and ME disable a controller in Device
Manager by sending `IRP_MN_QUERY_STOP_DEVICE` and `IRP_MN_STOP_DEVICE` down
the whole tree - each device PDO, the root hub, the controller - and no
`IRP_MN_REMOVE_DEVICE`; they re-enable it by starting the same devnodes
again. `2.0.0.0` treated its controller's orderly stop as the departure of
every USB device: the stop dropped each device, put its PDOs on the gone
list and reported them missing at the next relations answer. At the
re-enable the old PDOs' STARTs were refused ("device gone"), the restarted
controller enumerated the devices again, and new PDOs were created under
the same location instance ids. With only a stick attached, ME then sent
the stale PDO its REMOVE and started the new one, and so did Windows 98 SE. With a mouse attached ME sent nothing more to any
of this driver's objects - no REMOVE for the stale PDO, no relations query
for the new one, no START - and its shell stopped. ME's own Microsoft UHCI
stack re-enables with a mouse attached. The fix keeps a device whose PDOs
PnP has all stopped listed across the controller's orderly stop, with no
device behind it (dormant), and gives the device back to those same PDOs
when the restarted controller enumerates it at the same place with the same
descriptors - which is what Windows 7's `usbhub.sys` does with a PDO it is
sent a STOP for (section 4.3, static).

---

## 1. The symptom

Device Manager, the xHCI controller's Properties, "Disable in this hardware
profile" ticked, OK: the controller shows Code 22, the devices under it go,
and the guest stays healthy. The same box unticked, OK: the controller
restarts (the debug console prints a second `No Op self-test completion
code=00000001`), and then the Windows ME shell stops responding - the
taskbar clock stops and the desktop answers nothing, and no reading
recorded a recovery short of a restart. The
re-enable is not saved: on the next boot the controller is still disabled
(Code 22), and re-enabling it then, with the same devices attached, works
- on a boot where the controller never started there were no PDOs to go
stale
(`out\phase28\v1b\me\report.md`, clause 8, and its
`L3-boot-ctl-still-disabled.txt`).

What the guest looked like while hung, `out\phase28\me-reenable\r1\hang-1.txt`
(QEMU monitor, 2026-10-04 17:41:47, development host A), all **runtime**
readings of the emulated machine and of this driver's controller:

```
EIP=c0013740 ... CPL=0 ... HLT=0          (5 s later EIP=c0013518)
IRQ  0: 166107  -> 169331 five seconds later
IRQ 11: 45      -> 45                     (the xHC's line, unchanged)
info usb: Device 0.1, Port 1, Speed 480 Mb/s, Product QEMU USB Mouse
USBCMD 0x00000005   USBSTS 0x00000000
PORTSC1 0x00000e03  (connected, enabled, powered, High Speed)
IMAN 0x00000002     IMOD 0x000000a0
```

The virtual CPU was running in ring 0 and the timer was counting; the
controller was running, with the mouse's port enabled; the controller's own
interrupt line had not counted in five seconds. The Package B reading of the
same clause adds that the PS/2 mouse still moved the
pointer while the shell was wedged and that the PIC showed no interrupt
pending or in service (`out\phase28\v1b\me\report.md`). None of this names
where ME is waiting; section 4.2 says what is and is not known.

Which devices make it hang, as recorded:

| Attached at the re-enable | Build | Result | Evidence |
|---|---|---|---|
| A HID mouse | Package B, `qemu` flavour (three runs) | hang, 3 of 3 | `out\phase28\me-reenable\r1\` to `r3\` |
| A HID mouse and a stick | Package B, `qemu` flavour (28-V.1 clause 8) | hang | `out\phase28\v1b\me\report.md`, `debugcon-m1-2.log` |
| A HID mouse | Package B, `release` flavour (install leg clause 6) | hang, over 9 minutes | `out\phase28\release\report-pkgB.md` |
| A HID mouse | `1ed1ba6` (the 28-V.1 pre-read) | hang, in the earlier shape of section 5 | `out\phase28\pre\me\report-rerun.md`, "Other clauses" |
| A stick only | Package B, `qemu` flavour | the re-enable completes, the stick works | `out\phase28\me-reenable\r4\` |
| A stick only | the `2.0.0.0` release asset (install leg clause 6) | the re-enable completes | `out\phase32\asset\me\` (screenshots; the stick-only choice is written only in the session note `.claude\memory\owner-rulings-28-31-2026-10-04.md`, ~19:35) |
| Nothing | Package B, `qemu` flavour | the re-enable completes | `out\phase28\me-reenable\r5\`; `out\phase28\release\report-pkgB.md` |

Package B is `p28-31-int` at `417199e` (`runs/run-28.md`); its `qemu` x86
`xhci98.sys` is SHA-256 `10f1d58c...d134`, the file staged as
`out\phase28\me-reenable\xfer\xhci98.sys`. `git diff 417199e 4038447 -- src`
touches only the version stamps (`xhci_version.h` and the four INFs'
`DriverVer`), so the three hangs out of three in `r1` to `r3` are the
`2.0.0.0` source's, which is what roadmap task 33.1's "3 of 3 on
`2.0.0.0`" means. They are not readings of the released binary itself.

**No keyboard was ever attached in any of these runs.** The `2.0.0.0`
release notes and the README said "a USB mouse or keyboard"; the keyboard is
an extension from the mouse to the other HID device a user is likely to
have, not a measurement (section 9).

## 2. How it was reproduced

All on development host A, 2026-10-04, in QEMU under TCG on one virtual
CPU, the ME guest under SweetLow's stack (`vm\t28pre\winme-sl-base.qcow2`,
read-only, with a fresh overlay per run), `qemu-xhci` with `p2=8,p3=0`, and
the `qemu` flavour's port-`0xE9` debug console written to a file
(`out\phase28\me-reenable\kit\launch.ps1`).

1. Boot, wait for the desktop, attach `usb-mouse` on the monitor (and, for
   the stick legs, `usb-storage` with a FAT image holding `RAND.BIN`).
2. Device Manager, the controller's Properties, tick "Disable in this
   hardware profile", OK. Wait 30 s.
3. Open the controller's Properties again, untick the box, OK.
4. Watch the clock and the Start menu for two minutes; if the shell has
   stopped, take the QEMU monitor readings (`info registers`, `info irq`
   twice five seconds apart, `info usb`, the xHC's operational and runtime
   registers) and the debug console's last 60 lines.

The control, the same procedure under ME's own Microsoft UHCI stack with no
xHCI controller and no `xhci98.sys` (`out\phase28\me-control\enable\`,
screenshots `e1-*` to `e4-*`): three re-enables with a mouse attached, each
followed by the pointer moving and the Start menu opening, and one with
nothing attached. That is what made the hang this driver's (owner,
2026-10-04 ~17:30; roadmap-hcd.md decisions table, "Windows ME: the
controller re-enable"). It is a screenshot reading of the desktop; no trace
of what ME's own hub driver was sent exists, because nothing of this
project's runs underneath that stack.

## 3. What the traces show

The IRPs this driver logged in `out\phase28\me-reenable\r1\debugcon-2.log`
(runtime: this driver's own `hcd: ... PnP minor=` lines; `r2` and `r3` read
the same). Minor codes from the Windows 2000 DDK's `wdm.h`: 0 START, 2
REMOVE, 4 STOP, 5 QUERY_STOP, 7 QUERY_DEVICE_RELATIONS, 9
QUERY_CAPABILITIES, 0x0D FILTER_RESOURCE_REQUIREMENTS, 0x13 QUERY_ID, 0x14
QUERY_PNP_DEVICE_STATE.

The disable (lines 251 to 281):

```
device PDO PnP minor=00000005      QUERY_STOP: the mouse's PDO first
root hub FDO / PDO minor=00000005
controller PnP minor=00000005
device PDO PnP minor=00000004      STOP: the same order
root hub FDO / PDO minor=00000004
controller PnP minor=00000004
hcd: stop controller
teardown: ports unpowered=00000008
quiesce: halted, USBSTS=00000001
```

No REMOVE reaches any object. The enable (lines 287 to 393):

```
controller PnP minor=0000000D, 00000000      the controller restarts
root hub FDO / PDO minor=00000000            the root hub restarts
device PDO PnP minor=00000009
device PDO PnP minor=00000000                START on the mouse's old PDO
hcd: START refused, device gone, port=00000001
device PDO PnP minor=00000004                ME stops it again
controller PnP minor=00000007                relations
root hub FDO / PDO minor=00000013 (x2), 00000007
hcd: port reset, ... enable slot, address device
hcd: device descriptor, idVendor/idProduct=06270001
hcd: device PDOs created, port/count=00010001
hcd: device enumerated at location=00000001
```

and then nothing: no further PnP minor to any object of this driver, for as
long as the guest ran.

The stick-only run, `out\phase28\me-reenable\r4\debugcon-2.log` lines 324 to
380, goes the same way up to the new PDO, and then does not stop:

```
device PDO PnP minor=00000000
hcd: START refused, device gone, port=00000002
device PDO PnP minor=00000004
... relations, the port re-enumerated (idVendor/idProduct=46F40001) ...
hcd: device PDOs created, port/count=00020001
device PDO PnP minor=00000002                REMOVE: the stale PDO
root hub FDO / PDO minor=00000007            relations again
device PDO PnP minor=00000009, 13, 13, 0B, 15, 0D, 00   the new PDO started
```

Windows 98 SE on a Phase 28 build before the fix behaves like ME with a
stick: the STARTs refused, the restarted controller creates new
PDOs, and each stale PDO gets its REMOVE
(`out\phase28\primaries\r-98-gui2-debugcon.log`, about lines 960 to 1040).

So the difference the traces establish is one IRP. **After the relations
answer that reported the stopped, refused PDO missing, ME sent that PDO its
REMOVE when it was a stick's and never sent it when it was a mouse's.**

## 4. The cause

### 4.1 This driver's side, read from its own source

What `2.0.0.0` does, as a static reading of this project's own code (the
read-only diagnosis of 2026-10-04 ~17:43 on `e0cc62f`, whose `src` is
`417199e`'s, kept at `out\phase28\me-reenable\codex-diagnosis.md`, checked
against the tree):

- The controller FDO's `IRP_MN_STOP_DEVICE` arm (`src/hcd_pnp.c`) calls
  `HcdStopController`, whose body stops the controller thread and calls
  `HcdEnumDrop` (`src/hcd_enum.c`) - the drop a controller reset's
  invalidation shares.
- `HcdEnumDrop` settles every port as departed: each device's PDOs are
  unlisted onto `GonePdos` by `HcdDevicePdoGone` (`src/hcd_pdo.c`), the
  device record goes with its slot, and the next relations answer reports
  those PDOs missing. Design record 13 section 5.2 states this as the rule
  ("removed, or the controller stops" - the PDO lives until PnP has been
  told it is missing).
- At the restart a START on an unlisted PDO is refused,
  `STATUS_UNSUCCESSFUL`, with the line `START refused, device gone`
  (`791f9f8`, section 5).
- The restarted controller's rescan enumerates each device again, and
  `HcdDevicePdoCreate` always allocates new PDOs. Their instance id is the
  location (root port and route), so they carry the instance ids the old
  PDOs had.

Nothing in that path asks who stopped the controller. A PnP STOP that ME
sends with every child already stopped, expecting the children back, was
handled exactly like a controller that had lost its devices.

### 4.2 Windows ME's side: what is established and what is not

Established, **runtime**, this driver's trace on ME under SweetLow's stack:

- ME's controller disable is QUERY_STOP then STOP, device PDOs first, then
  the root hub, then the controller, and no REMOVE (section 3). The enable
  is a START of the controller, the root hub and the same device PDOs, in
  that order, before the restarted controller has enumerated anything.
- A refused START is followed by a STOP of the same PDO.
- When the PDO reported missing is a stick's, ME sends it a REMOVE and then
  starts the new PDO. When it is a mouse's, ME sends no IRP of any kind to
  this driver's objects again, and the shell stops.
- ME's own UHCI stack re-enables with a mouse attached (screenshots only,
  section 2).

Not established: **where ME's configuration manager is waiting, and why a
mouse makes the difference.** No ME component was disassembled and no
debugger was attached to the hung guest. The QEMU readings say the machine
was not dead and that this driver had not been sent anything; they do not
say what ME was doing. That a stale HID devnode reported missing while
stopped, with a new devnode under the same instance id beside it, is what
ME cannot get past is an inference from the one-IRP difference of section 3
and from the fix's result (section 8), not a reading of ME. The fix's own
source comment states it as the observation it is: "A new PDO beside a
stopped one at the same instance id wedged ME's configuration manager".

### 4.3 What Microsoft's hub driver does with a stopped PDO

The model the fix follows was read from Windows 7 SP1 x86 `usbhub.sys`
6.1.7601.17514 (258,560 bytes, SHA-256 `ac34d36d...58e9`), **static**:
`cdb.exe -z` over the image with its public PDB, nothing executed, the
listing `out\post-release\issue5-ssflag\static\static-hub\dis-win7-x86.txt`
(git-ignored, taken for issue 5's post-release work; base 0x10000):

- `UsbhPdoPnp_StopDevice` (`0x3B40E`) sets the PDO's software PnP state to
  `0x67` (`push 67h` / `call SET_PDO_SWPNPSTATE` at `0x3B461`), closes the
  device's configuration (`UsbhCloseDeviceConfiguration`, `0x3B473`),
  unlinks the PDO from its device handle (`UsbhUnlinkPdoDeviceHandle`,
  `0x3B49B`) and completes the IRP with success. It does not delete the
  PDO or take it off the hub's list; `UsbhPdoRemoveCleanup` is called from
  the surprise-removal handler next to it, not from this one.
- `UsbhPdoPnp_StartDevice` reads that state back (`cmp eax,67h` at
  `0x1B165`, and `0x66` at `0x1B170`) and for a stopped PDO calls
  `UsbhSyncResetDeviceInternal` (`0x1B1B5`) on the same PDO: the device is
  reset underneath the PDO that PnP already has. On a failure it asks
  `Usb_Disconnected` (`0x1B1D4`) whether the status means the device has
  gone.

So a STOP to a USB device's PDO is not its departure in Microsoft's hub
driver: the PDO stays, and the START that follows brings the hardware back
under it. That is the contract the fix takes. Two limits on it: it is the
Windows 7 hub that was read, not ME's own `usbhub.sys` or SweetLow's, and
the hub never sees a controller stop as such - it sees its own FDO stopped
by the same tree-wide STOP, which is the case the HCD's root hub and device
PDOs are in on ME.

### 4.4 Why Windows 98 SE survived the same lifecycle

Windows 98 SE disables the controller the same way, STOPs the whole tree
and STARTs it again, and before the fix took the same refused STARTs and
the same new PDOs (section 3). Its configuration manager then REMOVEs each stale
PDO and starts the new one, so the devices came back - as new devnodes at
the same instance ids rather than the old ones restarted. No branch in the
HCD separated Windows 98 SE from ME here; the operating systems did.

## 5. The wrong turns

The hang was first read in the 28-V.1 pre-read on `1ed1ba6` (2026-10-04
morning, `out\phase28\pre\me\report-rerun.md`): with a mouse attached, the
stale PDO got a START, then a STOP, the mouse was never enumerated again
(`PORTSC1` `0x00000EE1`), and the shell wedged. It was filed with the
gone-PDO family of the same day - PDOs a pull or a Device Manager remove had
left behind - and sent to branch `p28-pdoleak` (`runs/run-28.md`, "The
device-pull fixes").

Two of that branch's fixes changed the shape of this hang without touching
its cause:

- **`791f9f8`** made a START on a departed or orphaned PDO fail
  `STATUS_UNSUCCESSFUL`, as Windows 2000's `USBHUB20.SYS` 5.00.2195.6655
  fails one (`0x142E8`, static; `runs/run-28.md`). For a device pulled
  during its install that is right. For a PDO that the controller's stop
  had only called departed, it turned the restart's START into a refusal.
- **`7f1b4e0`**, the owner's option 1, freed a gone PDO's port once a
  relations answer had reported it missing, so the port enumerates a new
  device with a new PDO "under the same location instance id, while the old
  PDO waits for its REMOVE ... as usbport's children do". On `1ed1ba6` the
  port had waited for the stale PDO's REMOVE and the mouse never came back;
  from `7f1b4e0` on the mouse was enumerated again and given a new PDO
  beside the stale one - which is the state section 4.2 says ME cannot get
  past.

Package B carried both, and its 28-V.1 clause 8 failed the new way (stale
STARTs refused, new PDOs created, ME silent), which is what showed that the
family's fixes were not this issue's. `runs/run-28.md`'s device-pull section
still lists "the ME controller disable and enable" as to be read on Package
B (`TBD(Package B)`); that reading is the clause 8 FAIL of the same file.

Between them the two shapes bracket the problem. Keep the stale PDO's port
until its REMOVE, as `1ed1ba6` did, and the mouse never returns; free the
port, as `7f1b4e0` does, and the mouse returns under a new PDO that ME does
not get past. Neither is right while the stop is treated as a departure.

## 6. The fix, and why it is the usbhub model

Commit `fa2af9b` (`src/hcd.h`, `src/hcd_pnp.c`, `src/hcd_enum.c`,
`src/hcd_pdo.c`; 289 lines added, none removed), written first as `5b554f4`
on `e0cc62f` and rebased onto `main` with its `src` unchanged but for the
version stamps, reviewed by Codex and found clean in one round, merged into
the release branch at `a9a7577`:

- **The controller's orderly PnP STOP marks itself.** The FDO's
  `IRP_MN_STOP_DEVICE` arm sets `StopPreserve` around `HcdStopController`.
  A REMOVE, a surprise removal, a reset's invalidation and a failed start
  keep the destructive drop.
- **Stopped devices go dormant, not gone.** In `HcdEnumDrop`, with
  `StopPreserve` set, `HcdDevicePdoDormantAll` walks the devices. A device
  whose every PDO (a split device's whole group) is listed, in PnP state
  STOPPED, not surprise-removed, not removed and with no URB pending keeps
  its PDOs listed with no device and `Dormant` set, and lets go of them; the
  drop that follows then finds the device detached, reports nothing gone and
  leaves its port `Empty` for the restart's rescan. The slot, the rings and
  the pipes are torn down as before: nothing of the hardware state survives
  the stop, only the PnP objects. The line is `hcd: stopped PDOs kept
  dormant, port=`.
- **The re-enumerated device is given back to its PDOs.** In
  `HcdDevicePdoCreate`, before anything is listed, `hcdDormantRevive` looks
  for a dormant group at the same instance key and compares it with the PDOs
  just built, position by position: function, interface mask, configuration
  length and bytes (as filtered), speed, the device descriptor, and the
  transport decision. If every one matches, the dormant PDOs take the device
  (its record, its port object, its root port and route) and the new ones
  are deleted unlisted, so the relations do not change. The line is `hcd:
  stopped PDOs revived, port=`. A dormant group at that place that does not
  match is reported gone in the same lock hold that lists the new PDOs.
- **A START on a dormant PDO waits for that.** Windows 98 SE and ME start
  the device PDOs right after the root hub, before the restarted
  controller's thread has rescanned, so the START wakes the thread and waits
  up to 10 s (`HCD_DORMANT_WAIT_MS`), in 50 ms steps with no lock held and
  not on a relations query, for the revival. If none comes - the device was
  unplugged while the controller was disabled, or another device is there -
  the group is reported gone and the START refused as before, with the line
  `hcd: dormant PDO not revived, reported gone, port=`.
- **A REMOVE on a dormant PDO** reports its group gone first, then takes
  the ordinary gone-PDO REMOVE path.

Why this and not something else: section 4.3. A STOP sent to a USB
device's PDO keeps the PDO in Microsoft's hub driver and the START after it
brings the device back under the same PDO; the HCD now does the same with
the PDOs its root hub owns, and the devnodes ME stopped are the devnodes it
starts. The new-PDO-beside-a-stopped-one state, which only this driver ever
created, no longer arises from a disable and enable.

What it does not do, by design and stated in the code:

- **Identity is the place and the descriptors, not the serial number.**
  Two identical units with no serial string, swapped between ports while
  the controller was disabled, are taken for each other. The instance id
  in `2.0.0.0` is location-only (`UniqueID` FALSE), which is the release
  notes' known difference and roadmap task 33.2's subject; design record 13
  section 10.7's id table says the serial string where a device has one,
  which is not what `2.0.0.0` built. *(Since task 33.2, branch
  `p33-serial`: a device with a usable serial answers it as its instance
  id, and the revive matches a dormant group by the instance id it
  answers - first a group named by the device's place, then one named by
  its serial id wherever the device comes back - with the same descriptor
  comparison; a revived PDO keeps the id it had. Identical units without a
  serial id, or sharing one, swapped while disabled are still taken for
  each other. Design record 13 section 10.7, "Instance ids from the serial
  number".)*
- **A PnP stop of the controller alone** - a resource rebalance, with its
  children still started - finds no device whose PDOs are all stopped, so
  every device is dropped and reported gone as before. That path was not
  exercised.

## 7. What the fix changes on Windows 98 SE and the NT targets

**Windows 98 SE.** The same disable and enable now keeps the devnodes: the
mouse and the stick go dormant at the stop and are revived at the enable,
where `2.0.0.0` gave both new PDOs and Windows 98 SE removed the old ones
(section 4.4). So a re-enable no longer removes and re-creates a devnode per
device: the devnodes are stopped and started again. Read on one Windows 98 SE
guest on the fix's package (section 8), whose USB 2.0 stack the record does
not name; not read on both of the Windows 98 SE install legs.

**Windows 2000.** Its controller disable never sends the controller a STOP:
on the fix's package the trace reads REMOVE to each device PDO and then
`controller PnP minor=00000002`, REMOVE, before `hcd: stop controller`
(`out\phase27\audio98\re2k-debugcon.log`, lines 505 to 530; **runtime**),
and no `kept dormant` line appears. `StopPreserve` is set only in the STOP
arm, so the new code is not entered and the disable is what it was.

**Windows XP, XP x64, Vista and 7.** Not read on the fix's package. On the
Phase 28 builds before it, their controller disable reaches the controller
as REMOVE and never as STOP: `controller PnP minor=00000002` five times and
`00000004` never across the five-cycle legs of Vista and Windows 7 in both
architectures, and one controller REMOVE and no STOP on XP SP3 and XP x64
(`out\phase28\v1\<guest>\debugcon-m1-1.log`; **runtime**, a count of this
driver's lines). On that reading the dormant path is unreachable there for a disable, and the only STOP it
could see is a rebalance with started children, which keeps the old drop
(section 6). The amd64 build carries the same source; no amd64 binary was
in `pkg-reenable1`.

What the fix adds that every target carries: a `Surprised` flag on each
device PDO (set at `IRP_MN_SURPRISE_REMOVAL`, read only by the dormant
test), and the `Dormant` checks at START and REMOVE, which are false unless
a dormant group exists.

## 8. The readings that passed, on the `qemu` package `out\phase28\pkg-reenable1`

The package: `xhci98.sys` 191,935 bytes, SHA-256
`72f86bbe...a665`, with `xhciuas.sys` and the two INFs, x86 only, `qemu`
flavour. The same file was staged for the ME leg as
`out\phase28\pdoleak\me\xfer\xhci98.sys` (same hash). The file is stamped
17:59 and `5b554f4` 18:05; no record names the commit the binary was built
from, so the link between the two rests on the session that built both. All
on development host A, 2026-10-04, one virtual CPU under TCG.

**Windows ME, a mouse, three cycles, then a mouse and a stick** (18:11 to
18:48; `out\phase28\pdoleak\me\debugcon-re1-2.log`, screenshots
`out\phase28\pdoleak\me\shots\re1-*`, `c2-*`, `c3-*`, `c4-*`). Each cycle
reads the disable's QUERY_STOP and STOP, `stopped PDOs kept dormant,
port=00000001`, the restart, the mouse's START held while the port resets
and the device is addressed (`idVendor/idProduct=06270001`), `stopped PDOs
revived, port=00000001`, and then the class driver's SELECT_CONFIGURATION
on the same PDO (`hcd: select, ConfigurationDescriptor=C1400750`, `endpoints
opened=00000001`). No `START refused`, no new PDO, the shell live after
each. The fourth cycle, with the stick added at port 2
(`46F40001`), keeps and revives both (lines 861 to 965), and the stick read
back after it (`c4-fc.png`). Three of three with a mouse, where `2.0.0.0`'s
source hung three of three.

**Windows 98 SE, a mouse and a stick, then a soak** (18:49 to 18:59; trace
`out\phase27\audio98\re98-debugcon.log`, driver
`out\phase27\audio98\re98-drive.txt`, screenshots
`out\phase28\pdoleak\re98-*.png`). The disable: QUERY_STOP and STOP to both
device PDOs, the root hub and the controller (lines 262 to 291), both kept
dormant (298, 299); the enable: both revived (398, 410). `fc /b` of a file
copied to the stick and back reads "no differences encountered" on `F:`
(`re98-fc.png`; the same window also shows an earlier `dir E:\RAND.BIN`
answered "Not ready reading drive E", and the record does not say which
drive `E:` was). Then 10 unplug and replug cycles, each with its REMOVE
seen and the desktop on screen; the trace holds the REMOVEs of the mouse
(`0627`) and of the stick (`46F4`). The guest's desktop carries
`nusb33e.exe`; the record does not otherwise name its stack.

**Windows 2000, a mouse and a stick, then a soak** (18:56 to 19:11;
`out\phase27\audio98\re2k-debugcon.log`, `re2k-drive.txt`,
`out\phase28\pdoleak\re2k-*.png`). The disable by REMOVE (section 7), the
enable with the devices back, `fc /b` clean on `F:` (`re2k-fc.png`), then
10 unplug and replug cycles, each with its REMOVE and the desktop on screen.
Nothing went dormant, as expected.

Not among these legs: a keyboard; a device unplugged, or a different
device plugged, while the controller was disabled (the 10 s wait and the
mismatch path); a composite device split into function PDOs across a
re-enable; a device behind a hub; both Windows 98 SE USB 2.0 stacks by name;
XP, XP x64, Vista and 7; the `release` flavour; and the amd64 build.

## 9. What is still owed

- **The same legs on the `2.1.0.0` package** (roadmap task 33.1): the ME
  re-enable with a mouse, three cycles, and with a mouse and a stick; the
  Windows 98 SE and Windows 2000 disable and enable with a soak each. Until
  they are read, `2.1.0.0`'s README and release notes keep their
  `TODO(33.1 legs)` markers, and the release-acceptance test's step 7.9 (the
  re-enable with a mouse attached) has not been run against a release.
  Task 33.4 adds the same re-enable legs with a device behind a hub, once
  hubs are devnodes of their own.
- **The keyboard.** No run attached one. The `2.0.0.0` wording ("a USB
  mouse or keyboard") is untested on the keyboard side in both directions:
  that `2.0.0.0` hangs with one, and that the fix holds with one.
- **The cases section 8 lists as not taken**, the unplug-while-disabled
  path first, since it is the one that makes a START wait 10 s.
- **ME's side of the mechanism** (section 4.2): unread. The fix does not
  depend on it.
- **The records the fix made stale.** Design record 13 section 5.2 still
  says a controller stop reports every PDO missing; `src/hcd_pdo.c`'s
  lifetime comment carries the dormant state, the design record does not
  yet. `runs/run-28.md`'s device-pull section still lists the ME controller
  disable and enable as `TBD(Package B)`.

## Sources

- `docs/contributing/roadmap-hcd.md`: the decisions table, "Windows ME: the
  controller re-enable, and a pull during an install"; Phase 28, tasks
  28-V.1 and 28.3; Phase 33, task 33.1.
- `docs/contributing/runs/run-28.md`: "The reading on the merged build"
  (clause 8 and the Windows ME paragraph), "The ten install legs on the
  `release` flavour" (ME clause 6), "The device-pull fixes" (`791f9f8`,
  `7f1b4e0`).
- `src/hcd_pnp.c` (`IRP_MN_STOP_DEVICE` arm), `src/hcd_enum.c`
  (`HcdEnumDrop`), `src/hcd_pdo.c` (the lifetime comment,
  `HcdDevicePdoDormantAll`, `hcdDormantRevive`, `hcdDormantWait`,
  `hcdDormantRemoved`, `HcdDevicePdoCreate`, `HcdDevicePdoPnp`),
  `src/hcd.h` (`Dormant`, `Surprised`, `StopPreserve`); commits `fa2af9b`
  and `a9a7577`.
- `docs/contributing/design/13-superspeed-hcd.md` section 5.2 (the PDO
  lifecycle as `2.0.0.0` built it) and section 10.7's id table.
- `docs/contributing/legal-provenance.md` section 4: the rows for Windows 7
  `usbhub.sys`'s PDO stop and start (static) and for the controller-disable
  IRP order on Windows 98 SE, ME and 2000 (runtime).
- `docs/using/release-notes.md`, `README.md`,
  `docs/using/release-acceptance-test.md` step 7.9, and commit `49a98e8`
  (the limitation's text in `2.0.0.0` and its removal for `2.1.0.0`).

Evidence, git-ignored, all on development host A:

- `out\phase28\me-reenable\` - the reproduction: `kit\launch.ps1`,
  `xfer\xhci98.sys` (Package B `qemu` x86), `r1\` to `r5\` (debug consoles,
  screenshots, and for `r1` to `r3` `hang-1.txt` and
  `hang-1-debugcon-tail60.txt`), and `codex-diagnosis.md` (the read-only
  diagnosis on `e0cc62f`).
- `out\phase28\me-control\enable\` - the control under ME's own UHCI stack.
- `out\phase28\v1b\me\report.md` - Package B's ME leg, clause 8, with
  `debugcon-m1-2.log`, `c8-evidence-1.txt`, `c8-evidence-2.txt` and
  `L3-boot-ctl-still-disabled.txt`.
- `out\phase28\release\report-pkgB.md` - the `release`-flavour legs, ME
  clause 6.
- `out\phase28\pre\me\report-rerun.md` - the pre-read on `1ed1ba6`.
- `out\phase28\primaries\r-98-gui2-debugcon.log` - Windows 98 SE removing
  the stale PDOs on the pre-fix build.
- `out\phase28\v1\<guest>\debugcon-m1-1.log` - the NT guests' controller
  disable arriving as REMOVE.
- `out\phase28\pkg-reenable1\` - the fix's package; `out\phase28\pdoleak\me\`
  (`debugcon-re1-2.log`, `shots\`) the ME legs on it;
  `out\phase27\audio98\re98-*` and `re2k-*` with
  `out\phase28\pdoleak\re98-*.png` and `re2k-*.png` the Windows 98 SE and
  Windows 2000 legs.
- `out\phase32\asset\me\` - the `2.0.0.0` asset leg on ME, whose clause 6
  was taken with a stick only; roadmap task 32.3 records the ten asset legs
  as passing every clause without saying so, and only the session note
  `.claude\memory\owner-rulings-28-31-2026-10-04.md` does.
- `out\post-release\issue5-ssflag\static\static-hub\dis-win7-x86.txt` - the
  Windows 7 `usbhub.sys` listing section 4.3 reads.
