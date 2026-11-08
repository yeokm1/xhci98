# Design Record 37 - A USB Disk the BIOS Presented at Power-On, After the Takeover, on Windows 98 SE and ME

Status: draft 2, 2026-11-06, reviewed and revised (the IOS question made a
reading with its predicate named, the boot volume excluded on its own
terms, the hook's contract and residency, the EDD checks independent of
the returned size word, arming before the ownership write, or before the halt
where there is no legacy capability, the extended functions'
failure and the hardware-touching functions, identity across the session,
the P14s's register readings kept to what they show, the diagnostic's
first note recorded but not recoverable from a stopped start); draft 1,
2026-11-06, before code. Written for `roadmap-usb4.md` task 47.12 (was
USBCOMPAT.1, with the owner's P14s report of 2026-11-06), from the
readings of `runs/run-47.md`, "Boot with a USB stick attached (P14s
report)", its subsection "Earlier releases, and the other Windows", and
"47.12 The BIOS's USB disk after the takeover on 98 SE and ME: the
reading". Nothing of it is coded except section 9's diagnostic; the
choice of the rest is the owner's (section 10).

Tags: **[emu]** read in this project's guests on the pinned emulator
(QEMU 11.1.2, SeaBIOS rel-1.17.0, `qemu-xhci`); **[obs]** read on the
owner's P14s Gen 1 (Intel `8086:02ED`), the owner's readings of
2026-11-06; **[spec]** xHCI 1.2c section 4.22.1 and Table 7-4 (USB Legacy
Support); BIOS Enhanced Disk Drive Services, T13 project 1386D revision
5, section 6.8 (the final EDD 3.0 layout), and Phoenix Technologies' BIOS
Enhanced Disk Drive Specification 3.0, sections 5.2 to 5.4 (the earlier
layout and the extended read, write and verify packets); neither document
is held in this repository; **[linux]** Linux's
`include/uapi/linux/edd.h` at the commit `external/README.md` pins
(`c6859eed`), `struct edd_device_params`, the final layout only, facts
only, nothing copied; **[ddk]** the Windows 98 SE DDK's `DCB.H`, `ISP.H`,
`AEP.H`, `IOS.H` and `INT13.INC` (`docs/references/README.md`, "Windows 98
DDK"), names and their comments only; **[inf]** an inference.

## 1. The problem

On the owner's P14s Gen 1 under 98 SE, a USB stick plugged in at power-on
keeps the computer from reaching the desktop; plugged in after Windows has
started it does not [obs]. The firmware has no setting that turns its USB
storage support off [obs]. In the emulator the same boot stalls for the
firmware's USB time-out per request and then reaches the desktop [emu]:
on 98 SE 15 requests over 72 s on `3.4.1.0`, `3.4.0.0` and `3.3.0.0` and
over 71 s on `3.0.0.0` and `2.0.0.0`; on ME 20 over about 100 s on
`3.4.1.0` and 28 over 125 s on `3.4.0.0`; none on 2000, XP and 7 on
`3.4.0.0` (`runs/run-47.md`).

What the P14s's firmware does is read only from outside. After the
driver's one-second wait its BIOS Owned bit was still set (the snapshot's
`handoff.bios.held=01012201`, a start without the stick) [obs]. At the
MS-DOS prompt, with the stick and without, both controllers read
`USBLEGSUP` `00012201` and `USBLEGCTLSTS` `40002000`: at that moment, of
the SMI enables only the one on an OS ownership change (bit 13) was set
and the USB SMI enable (bit 0) was clear [obs, the XHCIQUAL probe logs of
2026-11-06]. Their values when Windows starts, whether an
ownership-change SMI is delivered at each start, and what its handler
does with a stick present, are not read. So two firmware paths stay open
on the P14s: the INT 13h requests after the takeover (the emulator's
mechanism), and the handoff's own ownership-change SMI. Section 9's
diagnostic narrows the second on a start that finishes.

## 2. What the emulator showed (`runs/run-47.md`, "47.12 ... the reading")

1. **Who sends the requests.** IO.SYS's disk code in the high memory
   area, under the MS-DOS kernel; each MS-DOS request is five attempts
   with an INT 13h reset between them; on 98 SE three requests to the
   stick's letter (the partition's boot sector, the root directory, the
   boot sector), on ME four [emu]. On 98 SE they came from a protected-mode
   component walking the MS-DOS drives just after the takeover [inf from
   the registers]; which VxD is not read. No INT 13h went to the stick's
   unit, and no parameter function (08h, 15h, 41h, 48h) to any unit,
   between VMM's start and the takeover [emu].
2. **The path.** Every attempt passes IO.SYS's own INT 13h hook, then a
   protected-mode callback in the real-mode chain below it, then the
   firmware, and a second callback on the way back [emu]; the DDK names
   the layer (`INT13.INC`'s `Int13_*` services, `DCB.H`'s
   `DCB_DEV_I13_IN_PROGRESS`, "set when an int 13 in progress, in
   iosint13.asm") [ddk; its code not read].
3. **The unit is served in protected mode afterwards.** After the
   window's requests had failed, the protected-mode USB disk's
   configuration went on (3.6 s after the last firmware request on both
   systems), and from then on every INT 13h for the stick's unit entered a
   callback at the head of the chain and was carried out by the
   protected-mode disk in one or two milliseconds, writes included on ME
   [emu]. That IOS had associated BIOS unit 82h with that disk (`ISP.H`:
   `ISP_ASSOCIATE_DCB` "associates ... an int13 drive number with a
   physical"; `DCB_DEV_INT13_DRIVE`) is the inferred mechanism; no IOS
   request, flag or moment of association was observed [inf]. The stall
   is the window between the takeover and that service; the service did
   not end it (the last failing request ended first) [emu].
4. **The firmware's answers in the window** were all errors after its
   time-out (a read: AH=0Ch after 5.1 s; a reset: AH=01h at once) [emu],
   so MS-DOS and the component above it already go on with a failed
   request; whether they treat a quick failure as they treat a slow one is
   not read.
5. **Identification.** The pinned SeaBIOS answers INT 13h function 41h
   with EDD 3.0 for every unit, and function 48h, asked with a 66-byte and
   with a cleared 74-byte buffer, with a 26-byte table (no DPTE, no device
   path) for every unit, the stick's included [emu]. No unit can be named
   by its EDD path there.

## 3. What any mitigation must keep

- **Only a unit this driver's controller serves, and never a unit the
  system needs from the firmware.** The boot disk and any other disk the
  firmware serves through another controller (the q35 base's own C: is
  one: the AHCI disk through SeaBIOS's INT 13h) must never see a changed
  answer. A correctly attributed USB disk can itself hold the boot or
  system volume (Windows started from that stick in MS-DOS compatibility
  mode); before the protected-mode service exists such a unit has only
  the firmware, so it must never be failed either (section 4.3). Nothing
  may decide a unit by drive number, geometry, capacity or contents (a
  cloned disk has the same MBR and size); a drive number may only ever
  exclude.
- **IOS's own service.** Requests IOS serves from the protected-mode disk
  must reach it unchanged (section 2 item 3); a hook that answered them
  would turn a working letter into a failing one.
- **The firmware's parameter answers.** Functions 08h, 15h, 41h and 48h
  answer from the firmware's own tables on SeaBIOS [inf]; none was issued
  after the takeover in the emulator [emu], so whether any firmware waits
  on the controller for one is not read. IOS and MS-DOS may rely on them:
  left to the firmware unless a reading shows one waits (section 5.1).
- **Nothing before the takeover.** Real-mode MS-DOS, the boot sector,
  IO.SYS's letters and anything before this driver's takeover of that
  controller are the firmware's and work [emu: L8 of the run above].
- **Nothing on NT.** 2000, XP and 7 showed no request in the window
  [emu]; the 64-bit package and the NT paths are untouched.
- **No claim beyond the stall.** It gives no BIOS letter back, it leaves
  the same volume under two letters (98 SE's E: and F:, USBCOMPAT.1's
  first half), it does nothing for the handoff's own ownership-change SMI
  (section 1), and it does nothing where the firmware names no path.

## 4. Naming the unit before the takeover

### 4.1 The EDD 3.0 device path [spec, linux]

Function 41h (BX=55AAh) answers BX=AA55h with the version in AH (30h for
3.0) and the EDD support bit (CX bit 2) set. Function 48h is asked with
DS:SI at a buffer of 4Ah bytes, cleared to zero for each unit (so no
earlier unit's path can be read for this one), its first word 4Ah. The
returned size word is not used to decide: T13/1386D revision 5 has the
firmware return 1Eh there for any buffer of 30 bytes or more, and the
path's own key, length and checksum say whether a path was written. A
unit is named only when all of these hold, else it is not:

- function 41h: carry clear, BX AA55h, AH at least 30h, CX bit 2;
- function 48h: carry clear, AH 0;
- at 1Eh the key BEDDh; at 20h the path's length, either 2Ch (the
  final layout: checksum at 49h, the bytes 1Eh to 49h summing to zero
  modulo 256) or 24h (Phoenix's earlier layout: checksum at 41h over 1Eh
  to 41h); no other length;
- at 24h the host bus "PCI " and at 28h the interface "USB" padded with
  spaces to eight bytes;
- at 30h the PCI bus, device and function of a function whose class code
  is 0C0330h (an xHCI) and which this driver takes (section 5.3).

The device path's USB part (a serial number in the final layout) is
recorded, never used to decide: two sticks of one serial on one
controller are both on that controller.

### 4.2 Rejected ways of naming it

Drive number (the order of units changes with what is plugged in),
geometry and capacity (alike across disks), the MBR or its signature (a
stick cloned from the system disk matches the system disk), the
firmware's own device contexts before the reset (they say which devices
it drives, never which INT 13h unit each became), and the time-out itself
(a request that fails slowly after the takeover: a disk elsewhere can
fail slowly too, and the first such request has already cost the wait).

### 4.3 The units never armed

Whatever its path says, a unit is never armed when it holds the boot
drive, the drive holding the Windows directory or the drive holding the
swap file, nor when it is unit 80h (an exclusion by number, never an
inclusion). How a drive letter is mapped to its INT 13h unit at that time
is a reading owed: IOS's `ISP_GET_DCB` for the letter and the physical
DCB's `DCB_BDD_Int_13h_Number` [ddk], or MS-DOS's own drive map
information; until one is read on both systems no unit is armed.

### 4.4 When

Before this driver's takeover of that controller, while the firmware
still serves its disks. On 98 SE and ME the earliest place this project
already uses is a static VxD (`XHCIPFW.VXD`, record 32 section 5.1, loaded
from `HKLM\System\CurrentControlSet\Services\VxD\<name>`, `StaticVxD`):
INT 13h through VMM's `Exec_Int` at its `Device_Init`, in the system VM,
before the configuration manager starts the controller [inf; owed: that
`Device_Init` comes before the controller's start on both systems, and
that `Exec_Int` there reaches the firmware's INT 13h through the same
chain as above]. Each unit from 80h up to the count at 0040:0075h is
asked once; the answers are kept, nothing else is done there.

### 4.5 The emulator

The pinned SeaBIOS names no path (section 2 item 5), so on it the
identification never names a unit and nothing changes. A leg of the hook
itself needs a `qemu`-flavour value that names a unit by number for the
test (exempt from the registry rule as a `qemu`-only value, never read by
a `release` image), and the identification needs a firmware that fills
the path, which only hardware offers here.

## 5. The interception

### 5.1 The answer

For an armed unit (sections 4.3 and 5.3), a request of a function that
reaches the device is answered at once, never passed to the firmware:

- the transfer and positioning functions 02h, 03h, 04h, 05h, 0Ch, 0Dh,
  10h and 11h, and the reset 00h: carry set, AH=80h (time-out), AL=0;
- the extended functions 42h, 43h, 44h and 47h: carry set, AH=80h, and the
  packet's block count (the word at offset 2) set to zero, but only after
  the packet is checked to lie whole in the VM's memory below 1 MB plus
  64 KB with its size byte at least 10h; a packet that fails the check is
  answered AH=01h with no write to it;
- the functions that move the medium or send commands, 45h, 46h, 49h, 4Eh
  and 50h: carry set, AH=80h;
- function 01h (status of the last operation): carry set, AH=80h, when
  the last request to that unit was one this layer answered; else passed
  on.

Every other function (08h, 15h, 41h and 48h among them) goes to the
firmware, until a reading on the P14s shows one of them waits on the
controller (Q1). In the window that gives MS-DOS a failed request, as it
gets today, without the wait [emu for today's failure]; AH=80h is not
the firmware's own 0Ch and 01h, so how IO.SYS's retry loop and the
drive-walking component treat it is a reading owed before code.

### 5.2 Where

Three places were weighed:

- **(a) A VMM hook of the virtual-8086 INT 13h** (`Hook_V86_Int_Chain`)
  in the VxD, which sees every INT 13h before the real-mode chain and so
  also the requests IOS serves in protected mode (section 2 item 3). It
  may answer only when no protected-mode device will serve the request,
  and the DDK names no such test: `IOS.H`'s `IOS_Find_Int13_Drive` ("ios
  service to return BDD for int 13 drive") returns a BlockDev descriptor,
  and `DCB.H` names flags for an INT 13h drive (`DCB_DEV_INT13_DRIVE`,
  `DCB_DEV_INT13_CONFIGURED`), for one the real-mode mapper controls
  (`DCB_DEV_RMM`, `DCB_DEV_RMM_INT13`), for one that must stay real mode
  (`DCB_DEV_REAL_MODE`) and for one going away (`DCB_DEV_REMOVAL_PENDING`)
  [ddk]. The predicate to read: a protected-mode physical DCB with
  `DCB_DEV_INT13_DRIVE`, `DCB_BDD_Int_13h_Number` the unit, and none of
  `DCB_DEV_RMM`, `DCB_DEV_REAL_MODE` and `DCB_DEV_REMOVAL_PENDING` - held
  against the trace, request by request, on both systems, from the first
  request the head-of-chain callback serves; and whether the services
  that read it (`ISP_GET_FRST_NXT_DCB` or `IOS_Find_Int13_Drive`) may be
  called from a V86 interrupt hook at all. Until both are read, option
  (a) has no step-aside test.
- **(b) The point where IO.SYS calls the firmware.** MS-DOS's own
  interface for the handler its disk code calls (INT 2Fh AH=13h, "set
  disk interrupt handler") would put a VxD callback between IO.SYS's hook
  and the callback below it (section 2 item 2). Requests IOS served were
  not seen to pass there [emu], so it might need no question to IOS; but
  that rests on the order of three layers read only from interrupt
  traces, and on IOS tolerating a change to that chain after its own
  start (IOS judges the INT 13h chain at its start to choose
  compatibility mode, `IOS.INI`'s safe list [inf]). A chain-placement
  experiment, owed: the order on both systems, and IOS's behaviour with a
  handler added there after its start.
- **(c) An IOS layer on the unit's real-mode DCB** (a VSD as
  `S98VSD.VXD`, record 30): deprioritised. No IOS request on a DCB was
  observed in the window's path [emu], but no observation excludes that
  IOS turns such an INT 13h into a request on a real-mode DCB before the
  firmware; whether a layer there would see them is not read.

Option (a) is the one the design carries (section 10), its step-aside
test a reading owed; (b) is the fallback if that test cannot be had.

### 5.3 The hook's contract

- **Return**: the request's own result goes in the saved client
  registers (client carry, client AH, AL, the packet); the hook's own
  return carry says only whether the request is consumed (clear) or
  passed on down the chain (set), as VMM's V86 interrupt chain defines.
  On every pass-on path the client state is untouched.
- **What it may do**: compare the unit, read its own armed table and the
  step-aside test's answer, write the client registers and, for the
  extended functions, the checked packet. It never calls the firmware,
  never allocates, never blocks, never waits, and never re-enters IOS
  unless that reentry is read safe (section 5.2 (a)).
- **Residency**: its code and every datum it reads sit in the VxD's
  locked segments; nothing it reaches is paged.

### 5.4 Arming

A unit is armed only once its controller is being taken by this driver.
`xhci98.sys`, on 98 SE and ME only, tells the VxD, and has its answer,
before the first step of its takeover that can end the firmware's
service: before it writes OS Owned where the controller has a USB Legacy
Support capability (the firmware may stop serving its disks at that
write, and the handoff's wait may yield), and before the halt where it
has none (QEMU's has none) or the semaphore needs no write. The link is
a private system control message through VMM's `Directed_Sys_Control` to the VxD's
DDB (`Get_DDB` by name), as `src/cm/cm_cfgmg.c` already calls VxD services
from the WDM driver, carrying a protocol version and the controller's
bus, device and function; the VxD answers with its version and the units
it armed, and `xhci98.sys` notes both. A message the VxD does not answer,
or an unknown version, arms nothing (the stall stays, as today), and is
noted. Owed: those calls from the WDM start context on both systems, and
that the bus, device and function `xhci98.sys` reads for itself are the
firmware's numbers (98 SE and ME keep the firmware's bus numbers [inf]).

The VxD reading the controller's own `USBLEGSUP` at each request instead
was weighed: it needs no link, but QEMU's controller has no such
capability, and OS Owned set says only that some driver took it.

### 5.5 Lifetime and identity

- **The session**: the bus, device and function the firmware reported
  name the controller it served before Windows; the firmware does not
  serve it again once reset (a stopped, removed or resumed controller
  included [inf]), so an armed unit stays armed until Windows ends. A
  controller that later appears at the same numbers (a removal and
  another device) changes nothing: the armed units are the firmware's
  boot-time units, which no firmware serves any more.
- **Several controllers**: each named unit is armed by its own
  controller's message.
- **Shut-down**: requests during IOS's teardown are answered as any other;
  the VxD has no unload. "Restart in MS-DOS mode" leaves real-mode MS-DOS
  with a firmware that no longer serves the stick, so MS-DOS mode would
  stall on that letter as it does today (the known limitation says so).
- **Calls that bypass the hook**: a program calling a saved firmware
  vector directly (a far call, not an INT 13h) is not seen and meets the
  firmware's time-out as today.

## 6. Packaging

A new image (`XHCII13.VXD`, a name of this project's own, on
`XHCIPFW.VXD`'s pattern: Open Watcom and the DDK's linker, record 30
section 6; its gate as `src/pfw/check-pfw.ps1`; the registry `StaticVxD`
key; installed and removed by the installer alone, 98 SE and ME only,
under the controller's install, its box and default the owner's). Folding
it into `XHCIPFW.VXD` was weighed: that image is installed only with the
Thunderbolt support, so the stick's fix would depend on an unrelated box;
a separate image keeps each one's install and recovery its own. Folding
the hook into `xhci98.sys` itself (VMM services by dynalink, a register
thunk for the hook) needs no new file, but puts virtual-8086 hook code
and its lifetime into the one HCD image of every 32-bit target and ties
the hook to the controller's stop and removal; not recommended.

## 7. Risks

- A named unit that is not this controller's (a firmware filling the
  path wrongly) gets failed requests: limited to the functions of section
  5.1, to after that controller's takeover and to units section 4.3 does
  not exclude; the checksum, key, interface and class checks bound it;
  still the reason the path is the only name.
- The component that walks the drives, or IO.SYS, may treat a fast
  failure differently from a slow one (a retry loop of its own, an MS-DOS
  critical error shown): owed (section 5.1).
- The step-aside test (section 5.2 (a)) may disagree with IOS's service:
  a request in between would be failed though IOS could serve it, or
  reach the firmware: owed.
- The hook runs in every INT 13h of the boot (the C: of a compatibility
  mode disk included): a fault in it reaches the system disk; hence
  section 5.3's contract and its host vectors.
- A firmware that never returns from the ownership-change SMI (section 1)
  stops the start inside the handoff; nothing here changes that.

## 8. The alternatives rejected

- **Deferring the takeover** until the drive walk has passed: no signal
  marks its end, the protected-mode disk needs the controller, and a
  request after the deferred takeover meets the same window. Without
  the driver the walk came at about the same point of the start, and
  another caller read the stick 6.5 s later [emu, the run section's
  N1]: passing the first walk does not mean the firmware's accesses
  have ended (no deferred takeover was read).
- **Clearing the SMI enables before writing OS Owned**: may suppress the
  firmware's own release (section 1); not a change for every machine.
- **Naming by contents or size** (section 4.2).

## 9. The diagnostic

Independent of the rest, the handoff records what the P14s's readings
miss, as three notes in the note ring, every flavour, with no change to
what the handoff writes:

- `handoff.legctl.before`: `USBLEGCTLSTS` read after `USBLEGSUP` has been
  validated and before OS Owned is written (the firmware's SMI enables
  and status at the OS's handoff);
- `handoff.legctl.after`: `USBLEGCTLSTS` read back after the driver's
  write of it;
- `handoff.legsup.later`: `USBLEGSUP` read again once the root hub has
  been initialised (`XhciRootHubInit` succeeded), whatever its value, for
  a controller with the capability; all ones says the window stopped
  decoding rather than that BIOS Owned is set.

On a start that finishes, the snapshot then says whether the enables at
the handoff were the MS-DOS prompt's and whether BIOS Owned was released
later. On a start that stops inside the firmware the first note is
appended (it comes before the ownership write) but cannot be read back:
the snapshot is taken from a running Windows.

## 10. Recommendation and the owner's questions

**Recommendation.** Keep the known limitation and its workaround in the
release notes now; section 9's diagnostic in `3.4.2.0`. Read on the P14s
first whether its firmware names the stick's controller in function 48h
(Q1). If it does, that justifies taking the readings section 5 owes
(sections 4.3, 4.4, 5.1, 5.2 and 5.4) in the emulator with the `qemu`
test value, and then, if they hold, building section 5 as option (a) in a
new static VxD armed by `xhci98.sys` (Q2 (B)); it is not yet an approval
of the interception as specified here. If the firmware names no path,
this design gives no ground to intercept, and the limitation stays
(Q2 (A)).

- **Q1 (the reading).** At "Command prompt only" on the P14s, the stick
  in: INT 13h functions 41h and 48h (a cleared 4Ah buffer, then 42h) for
  each hard-disk unit. The owner's answer (2026-11-06): an XHCIQUAL feature
  first, `XHCIQUAL --edd` (roadmap-usb4 task 47.24; `xhciqual/README.md`,
  "EDD device paths (`--edd`)", and `xhciqual/hardware-testing.md`,
  "ThinkPad P14s Gen 1 - does the firmware name its USB stick?"), which
  applies section 4.1's checks and says per unit whether it is named;
  `DEBUG.EXE` with the run section's scripts stays the fallback.
- **Q2 (the design).** (A) the known limitation only; (B) section 5 in a
  new static VxD, `XHCII13.VXD`, armed by `xhci98.sys`, 98 SE and ME, its
  installer box and default the owner's; (C) the same inside `xhci98.sys`
  (no new file); (D) naming by the time-out after the takeover.
  Recommended: (B) investigated if Q1 shows the path, else (A); (C) and
  (D) not.
- **Q3 (the diagnostic).** Section 9 in `3.4.2.0`. Recommended: yes
  (coded with this record, `runs/run-47.md`).

## 11. Verification (when coded)

Host vectors of the pure half: the EDD table's checks (each field wrong
in turn, both layouts, a stale buffer, the checksum), the function table
of section 5.1, the extended packet's check, the exclusions of section
4.3 and the arming by bus, device and function. Legs on 98 SE and ME (the
q35 bases, the stick at power-on, behind a hub, two sticks, a stick on a
second controller left to the firmware): with the `qemu` test value
naming the unit, the trace shows no firmware request in the window and
the protected-mode service afterwards, the boot without the stall, both
letters readable, a write and a read back on each; the same without the
value: nothing changed; the system disk through INT 13h (the q35 base's
C:) untouched in every leg; a start from a stick holding Windows: never
armed; NT untouched (the image not installed). On the P14s (the bench):
the stick at power-on, the start time, the snapshot's notes.
