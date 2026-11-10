# Design Record 32 - Prefetchable Windows Above 4 GB Closed Before Windows 98 SE and ME Read Them

Status: draft 18, 2026-11-09: section 11.5.7, the owner's answers to
Q26 to Q29 (a, b, a, b). Draft 17, 2026-11-09: section 11.5.6, new - 10 in the filter
role (task 48.27): 10 takes 8's and 8.1's role and installer kind, the
hazards 10 widens (fast startup, an upgrade to a later release, Secure
Boot turned on later), the previews of the entry role's removal, its
legs, and the owner's questions Q26 to Q29,
before any code. Draft 16, 2026-11-06: section 11.5.5, new - 10 read again (task
47.14): a boot-start upper filter of the root bus, installed with its
image path written as `REG_EXPAND_SZ`, acts before the PCI driver reads
the bridges at every start read, as on 8 and 8.1; 10's role and code
stay planned. Draft 15, 2026-11-03: section 12.8.16, new - the DSDT and every
SSDT read by `xhci98.sys` on the user's request for XHCITR (task 46.21,
the owner's option B). Draft 14, 2026-11-03: section 12.8.14, new - the tables
the handover's discovery read kept for XHCITR (task 46.21). Draft 13,
2026-11-03: section 8, the gate holds every LE
fixup inside its target object (record 30 section 6.1, a third
mislink of the linker, path-dependent). Draft 12, 2026-10-29: section 11.5, new - task 45.9's reading (a filter in the PCI root bus's stack
runs before the PCI driver reads the bridges at every start read on 8
and 8.1; on 10 such a filter is never initialised and the start fails)
and its design for 8 and 8.1, the owner's answers (Q10 to Q12)
included. Draft 11, 2026-10-28: `XHCIPFW.SYS` offered on the 32-bit
2000, XP, Vista and 7 only (the owner, after task 44.72's readings: from
8 on its start and the PCI driver's read of the root bus are not
ordered); on the 32-bit 8, 8.1 and 10 not offered, `/xhcipfw` refused,
and one the installer placed removed (sections 9, 11.1 to 11.4). Draft
10, 2026-10-27: "Above 4G Decoding" per system
(section 11.3), from the legs of the reviewed code (`runs/run-44.md`,
"44.49"). Draft 9, 2026-10-27: the installer's presentation (the owner):
the box the Thunderbolt 3 / USB4 box's child, under it and above Stor98,
named by its file, offered on XP too (sections 8, 9 and 11.3); the log's
names prefixed `XhciPfw` (section 6); the "late" check reads whether the
`SOFTWARE` hive is loaded, not the PCI driver's object, after 8's leg
(sections 11.1 and 11.2); XP held to one processor as 2000 is (section
11.2); "Above 4G Decoding" off for the 32-bit Vista to 10 (section 11.3);
Q9 (section 9), answered the same day: the box ticked whenever the
Thunderbolt box is, on every system. Draft 8, 2026-10-27: XP read again (section 7: it needs no step);
the owner's Q7 (the installer ticks the box itself where Windows shows
the problem, section 11.3). Draft 7, 2026-10-27: the owner's answers to Q6 (all the systems)
and Q8 (2000 held until an ACPI-HAL reading, which section 2 now
records: it supports 2000), XP's reading cited (section 7); Q7 open.
Draft 6, 2026-10-27: section 11, the step for 2000 and the
32-bit Vista, 7, 8, 8.1 and 10 (the owner's Q5), from the readings it
records, before its code. Draft 5, 2026-10-27: the xHCI controllers with a BAR above 4 GB
logged (section 4.4, the owner's), the load read on both systems (section
5.1), the init order, the log's full layout, the package and the
installer's part (section 8). Draft 4, 2026-10-27: the owner's answers
to section 9's questions. Draft 3, 2026-10-27, reviewed and revised (a window across 4 GB
left as it is and logged, not clipped; the fault guarantee bounded to a
write that takes or is ignored; the log's layout; the scan's reach; the
resume and the legs on both systems); draft 2, 2026-10-27, reviewed and
revised (every write verified
and undone on a refusal, a window across 4 GB clipped rather than closed,
a flat bus scan, the read trace in the order check, the resume, a leg with
a prefetchable BAR, Q5 as a checkpoint question); draft 1, 2026-10-27,
before code. Written for `roadmap-usb4.md`
task 44.49 (the owner, 2026-10-27; decisions table, row "44.49"),
release `3.3.0.0`, from design record 22 section 18.5 and the readings
of `runs/run-44.md`, "44.49". The code is `src\pfw\` (the VxD),
`test\test_pfw.c` (section 10.1) and the installer's part (section 8).

Tags: **[emu]** read in this project's guests on
the `usb4-emu` emulator (`runs/run-44.md`, "44.48" and "44.49");
**[spec]** the PCI-to-PCI Bridge Architecture Specification, Revision 1.2
(the bridge's Type 1 header; the document is not held in this
repository), and the PCI Express Base Specification's Type 1 header,
which keeps the same registers; **[ddk]** the Windows 98 SE DDK's
`VMM.H`, read where `docs/references/README.md` records it; **[linux]**
Linux, facts only (`drivers/pci/setup-bus.c` at its `master` of
2026-10-27, read for facts, nothing copied);
**[inf]** an inference.

## 1. The problem

On an ASM4242 with "Above 4G Decoding" off the firmware still programs the
64-bit prefetchable windows of the root port, the ASM4242's upstream port
and its two tunnel ports above 4 GB, and the host bridge's `_CRS` then
has no window above 4 GB (record 22 section 18.5). The emulator's 4G-off
layout is that case: on the `pentium3` bases its firmware programs the
root port 1_0000_0000 to 40_FFFF_FFFF (`runs/run-44.md`, "44.49")
[emu]. No firmware setting places them below 4 GB (record 22 section
18.5). Nothing behind them uses a prefetchable BAR: the ORICO's two
bridges have no prefetchable window and its NVMe's BAR is
non-prefetchable, below 4 GB [emu].

What each system does with such windows (`runs/run-44.md`, "44.48" and
"44.49"), on the emulator's firmware-faithful 4G-off layout [emu]:

| System | Result |
|---|---|
| 98 SE, ME | boot; their PCI enumerator writes only the low base and limit registers (`24h`, `26h`, as `FFF0h` and `0000h`) of five of the ASM4242 bridges and never the upper ones (`28h`, `2Ch`); the tunnel port the enclosure is on, whose firmware window has the address fields `0020h` (base) and `0030h` (limit) in its low registers (0x200000 to 0x3FFFFF in a 32-bit view, inside memory), is left so and has problem 12 on 98 SE ("cannot find any free Memory Range resources"), and nothing behind it is enumerated |
| 2000 | stops at "Starting up": `pci.sys` 5.00.2195.6655 writes the root port's window as base 0 and limit `FFFFFFFF_FFFFFFFF` and then loops in a range-list walk on a node covering that whole range (section 2) |
| XP | boots and moves the windows below 4 GB itself |
| Vista, 7, 8.1, 10 (32-bit) | boot; the ASM4242's root port has problem 12 and nothing behind it is enumerated: no ASM4242 function, no tunnel port, no drive |
| 8 (32-bit) | does not reach the Start screen (twice, fresh overlays); the low-window layout, as a control, does |
| 10 x64, option off | boots; the root port has problem 12; Windows rewrites the windows to other addresses above 4 GB |
| 10 x64, option on | every bridge, the xHCI and the NVMe problem 0 (the NVMe started), the NHI problem 28 (no driver) |

A Linux 7.0 guest on the 4G-off layout reports "can't claim; no
compatible bridge window" for the four windows and assigns its own below
4 GB: 4 MB on the root port and the upstream port, 2 MB on each tunnel
port [emu] (`runs/run-44.md`, "44.48"). The emulator's
low-window layout (the 4G-off layout with the windows below 4 GB) is
that result, and 98 SE, ME and 2000 boot
on it with every bridge, the enclosure and the drive at problem 0 [emu].

So a window that a 32-bit system cannot address must be gone from the
bridge before such a system reads it. This record is the step that does
that for 98 SE and ME. 2000, XP and the NT 6 systems are section 7.

## 2. Why 2000 stops (the reading of task 44.49's second step)

On the 4G-off layout (the q35 2000 base, Standard PC HAL, `-cpu pentium3`
with 40 address bits) [emu]: QEMU's `pci_cfg_write` trace shows SeaBIOS
programming the root port's prefetchable window as 1_0000_0000 to
40_FFFF_FFFF (`24h` base `0001h` limit `FFF1h`, `28h` `1`, `2Ch` `40h`),
then 2000's PCI driver probing the header with all-ones and writing it
back, then writing `24h` = `FFF00000h` (base `0000h`, limit `FFF0h`),
`28h` = 0 and `2Ch` = `FFFFFFFFh`: a window from 0 to
`FFFFFFFF_FFFFFFFF`. The guest then spins in `pci.sys` (`PAGE` section,
offset `974Bh` of the image, version 5.00.2195.6655) walking the back
links of a range list whose single node links to itself and covers 0 to
`FFFFFFFF_FFFFFFFF`, called from `pci.sys`'s arbitration path under the
kernel's Plug and Play manager (the return addresses on the stack); the
memory images were read with QMP `memsave` and the loop's bytes
disassembled on the host, nothing tracked (`legal-provenance.md` section
4 has the row: debugger and static). With every
prefetchable window below 4 GB (the low-window layout) 2000 boots [emu].
So 2000 stops because its PCI driver mishandles a 64-bit prefetchable
window above 4 GB, not because of anything behind the window [inf].

**Under the ACPI HAL** (the q35 2000 SP4 base with `halacpi.dll`,
uniprocessor; `-smp 1`) [emu]: the same. On the 4G-off layout 2000 stops
at "Starting up"; the PCI driver writes the root port's `24h` =
`FFF00000h`, `28h` = 0, `2Ch` = `FFFFFFFFh` as under the Standard PC HAL,
and the processor then runs at `F2009754`, `F200975C` and `F200974B`,
inside the image at `F2000000` (the same offsets, `974Bh` to `976Ch`,
of the same `pci.sys` loop). So the root bus's `_CRS` does not change
2000's handling of the window.

## 3. The registers [spec]

A bridge's prefetchable window is two 16-bit registers, Prefetchable
Memory Base (`24h`) and Prefetchable Memory Limit (`26h`), whose bits
15:4 are address bits 31:20 and whose bits 3:0 read `0h` for a 32-bit
window or `1h` for a 64-bit one, and, for a 64-bit window, Prefetchable
Base Upper 32 Bits (`28h`) and Prefetchable Limit Upper 32 Bits (`2Ch`),
address bits 63:32. The window is 1 MB aligned; the limit's address bits
19:0 are taken as ones. When the limit is below the base the bridge
forwards no prefetchable memory downstream. A device's prefetchable BAR
may be placed in the bridge's non-prefetchable range, which forwards any
memory transaction inside it, as Linux does when no prefetchable window
is available [linux: `pbus_select_window_for_type`, the comment in
`pci_need_to_release`].

A system that reads only `24h` and `26h`, as 98 SE and ME do [emu],
sees a window above 4 GB as its low halves: base 0 and limit
`FFFFFFFFh` when the registers read `0001h` and `FFF1h`, over the
system's memory [inf]. The emulator's firmware on the `pentium3` bases
programs the root port so (1_0000_0000 to 40_FFFF_FFFF, low registers
`0001h` and `FFF1h`) [emu]; 98 SE and ME then
write its low registers `FFF0h` and `0000h` and leave its upper ones, so
the window stays open from 1_FFF0_0000 to 40_000F_FFFF in 64-bit terms
while they see it closed [emu].

Linux disables a window it does not assign by writing `24h` as one dword
`0000FFF0h` and both upper registers 0, the upper limit cleared before
the others [linux: `pci_setup_bridge_mmio_pref`].

## 4. The step

### 4.1 What it does

Once, before 98 SE's or ME's configuration manager and PCI enumerator
read any bridge (section 5):

1. **Scan** every bus number 0 to 255 and device 0 to 31 through
   configuration mechanism #1 (ports `CF8h` and `CFCh`): a device whose
   function 0 reads vendor `FFFFh` is absent; bit 7 of function 0's header
   type says whether functions 1 to 7 are read; the header type is taken
   as its bits 6:0. A flat scan rather than a walk down from bus 0: it
   cannot loop, and it reaches every function configuration transactions
   reach, a bus no bridge leads to (another root bus) included. A function
   behind a bridge whose bus numbers do not route to it is not reached by
   any scan; the step acts only on what it reads, and such a subtree's
   windows are the firmware's problem as before. Every type 1 function's
   secondary and subordinate bus numbers are kept for step 4; a type 2
   (CardBus) or other header is not acted on. Nothing is written in this
   step.
2. **Classify** each type 1 function's prefetchable window by its four
   registers as read, on their address fields (the type nibbles of `24h`
   and `26h` are read-only and kept as read):
   - **32-bit** (`24h` bits 3:0 not `1h`), or **64-bit with both upper
     registers 0**: left alone.
   - **Above 4 GB**: the upper base (`28h`) not 0. The window, open or
     closed, lies wholly at or above 4 GB, and a window closed only by its
     upper halves (its low base at or below its low limit) reads as an
     open one in a 32-bit view [inf]. Closed (step 3).
   - **Across 4 GB**: the upper base 0, the upper limit (`2Ch`) not 0. Its
     part below 4 GB can hold BARs a 32-bit system uses (a firmware could
     place a frame buffer at E0000000 in a window E0000000 to
     1_FFFFFFFF). Left as it is and logged (section 6); none was seen in
     the asm4242 data or the emulator. Clipping it at 4 GB is section 9, Q3 (d).
3. **Close** each window above 4 GB to the 32-bit closed form, upper
   halves 0, base address field `FFF0h`, limit `0000h`: `28h` =
   `FFFFFFFFh` (the base now above any limit whose upper half is below
   `FFFFFFFFh`, or both at the top of the 64-bit space), `2Ch` = 0, `24h`
   = `0000FFF0h` as one dword, `28h` = 0. Each write is verified by
   reading the register back (address fields only), and the four
   registers are then checked against the invariant that the window is
   closed or lies wholly at or above 4 GB. **What this guarantees**, for a
   bridge whose registers either take the value written or keep the one
   they had (a write ignored) [inf: every state held by section 10.1's
   vectors]: every state is closed or wholly above 4 GB, the forward
   ones and the undo's alike. **The undo**: a write that does not read
   back as written, or a state that breaks the invariant, has that
   register written back to its value before the write, then the writes
   done so far undone in reverse order, each verified; the bridge is
   counted as refused. **What it does not guarantee**: a register that
   takes a third value (a bridge outside the specification) can reach any
   state, the undo's writes included; the step cannot prevent that, only
   see it. An undo that does not read back stops there; the bridge is
   counted as parked with its four registers as read, and the step goes
   on with the next bridge. Nothing is retried.
4. **Count**, before any write, in the subtree of each window step 3 is
   to close (the functions on buses from its bridge's secondary to
   subordinate number) every memory BAR whose address lies inside the
   window, reading the BARs and never sizing them (no write). These are
   the BARs the close would leave without a path, whether or not the
   close then succeeds; they are left as they are: 98 SE and ME cannot
   reach them whatever the step does [inf], and moving one needs room
   below 4 GB the step does not have (section 4.2). The count and the
   functions go to the log (section 6).

Nothing else is written: no non-prefetchable window, no I/O window, no
BAR, no command register.

### 4.2 Why close, not move

The task asks for the windows "moved below 4 GB". Closing is a move to
size zero, and it is the one this step can do safely before Windows:

- Linux gives each window a size from what its subtree needs plus a
  hot-plug minimum (2 MB a tunnel port on the 4G-off layout) and places it inside
  a free part of the host bridge's window, which it reads from the ACPI
  `_CRS` [emu; linux]. Before Windows starts there is no ACPI
  interpreter to read `_CRS`, and the free space below 4 GB is not known
  from the PCI registers alone (other bridges' windows, the firmware's
  reserved ranges, the local APIC and the flash all lie there). A guess
  that overlaps any of them corrupts more than it fixes.
- On the 4G-off layout nothing behind these windows needs prefetchable
  memory [emu], so a zero-size window loses nothing there.
- A device behind such a window that has a prefetchable BAR (none on the
  ASM4242): section 3's rule lets it be placed in the bridge's
  non-prefetchable window, and Linux does so [linux]; whether 98 SE's and
  ME's enumerator does is not read [inf], and section 10.2 item 6 reads it
  with a device that has one. A chain this project numbers on 98 SE and
  ME after a hot plug already gets its prefetchable windows closed, upper
  halves included (record 21 section 6.1.1, `src/cm/tbt_brnum.c`).
- 98 SE and ME close such windows themselves where they can (the
  `FFF0h`, `0000h` writes of section 1 [emu]); the step leaves the
  bridges in the form those systems write, with the upper halves they
  never write cleared.

### 4.3 Where it acts

On every bridge whose prefetchable window has an upper register not 0,
on any board (section 9, Q3), on an ASM4242's bridges and on any
other. A board with no such window is not written at all by this pass,
and the log says so (the interrupt-line pass of section 12.10 runs
whether or not a window qualifies). The step does not read the vendor or device id for its
decision.

### 4.4 xHCI controllers with a BAR above 4 GB: logged, never moved

The owner, 2026-10-27: the step also logs any xHCI controller whose
register BAR lies above 4 GB, and changes nothing about it. In the
scan of step 1, each type 0 function whose class code (`08h` bits 31:8)
is `0C0330h` has its BARs `10h` to `20h` read (never sized, never
written); each 64-bit memory BAR (bits 2:1 `10b`) whose upper dword is
not 0 is counted (`XhciPfwXhciHigh`) and recorded (section 6): bus, device and
function, the BAR's offset, the vendor and device ids, the address's
two dwords. 98 SE and ME cannot reach such a controller [inf], and a
BAR moved before Windows would need room below 4 GB the step does not
know (section 4.2), so the log is what tells the user, `XHCITR`'s report
or the bench why `xhci98.sys` finds no controller there. On the
emulator's q35 board SeaBIOS places every non-prefetchable BAR below 4
GB, and both xHCI models' BARs are non-prefetchable [emu], so no leg
reads a controller above 4 GB; the host vectors (section 10.1) and the
board (44-E.6) do.

## 5. Where it runs on 98 SE and ME

### 5.1 The choice: a static VxD at `Sys_Critical_Init`

A small static VxD of this project's own, in C, built with the toolchain
of design record 30 section 6 (Open Watcom 2.0 and the 98 SE DDK's
`LINK` 5.12 `/VXD`, with that section's checks), whose control procedure
does the scan and writes of section 4, and then the interrupt-line
pass of section 12.10 (task 46.11), in `Sys_Critical_Init` and nothing in any
other message but `Device_Init`'s log write (section 6). The name is
`XHCIPFW.VXD` (device name `XHCIPFW`; "prefetchable window").

- **Order** [ddk]: `VMM.H` gives `CONFIGMG_INIT_ORDER` `16400000h` and
  `PCI_INIT_ORDER` `ENUMERATOR_INIT_ORDER`, `16800000h` (ACPI's
  `16800002h`), and initialises devices of the same order in no
  guaranteed order. The VxD's `Init_Order` is `16300000h`, below
  `CONFIGMG_INIT_ORDER` and above `NTKERN_INIT_ORDER` (`16200000h`), so its
  `Sys_Critical_Init` runs before the configuration manager's and the
  PCI and ACPI enumerators' in the order VMM calls every device's
  `Sys_Critical_Init` before any `Device_Init` [inf from the DDK's init
  sequence; read in the legs by QEMU's `pci_cfg_read` and
  `pci_cfg_write` traces: between the firmware's last access and the
  step's writes no access to any bridge's `24h` to `2Ch` may appear, read
  or write, since a read could have kept the firmware's window].
- **Loading**: as a static VxD named in the registry,
  `HKLM\System\CurrentControlSet\Services\VxD\XHCIPFW`, `StaticVxD` =
  `XHCIPFW.VXD` (the file in the Windows `SYSTEM` folder), `Start` =
  binary `00`. **Read on both** (`runs/run-44.md`, "44.49", the
  qemu-flavour image, the 4G-off layout, cold starts) [emu]: 98 SE and ME
  loaded it from that key; its `Sys_Critical_Init` ran, and QEMU's
  `pci_cfg_read` and `pci_cfg_write` traces show its scan as the first
  configuration access after the firmware's last, so no access by
  Windows to any bridge came before its writes; its `Device_Init`
  registry writes returned 0 and the values were there at the desktop;
  every ASM4242 bridge, both enclosure bridges and the NVMe were then
  enumerated without problem 12 (98 SE's tunnel port at problem 0, where
  it had problem 12 without the step). So the fallback, a `device=` line
  in `SYSTEM.INI`'s `[386Enh]` section, is not used.
- **Ring 0 and ports**: at `Sys_Critical_Init` the VxD runs in ring 0
  with interrupts disabled; mechanism #1 is two `out`/`in` pairs per
  register, and no VMM service is needed for the scan.
- **What it returns**: success to VMM in every case, so that its own
  outcome never stops the start; a function that reads `FFFFFFFFh`
  mid-way is skipped and counted. A bridge outside the specification can
  still be left in a bad state (section 4.1 step 3, what it does not
  guarantee), which the log records.

### 5.2 The alternatives, read and not chosen

- **A real-mode program from `AUTOEXEC.BAT`** (or a `DEVICE=` driver in
  `CONFIG.SYS`): runs on 98 SE before `WIN.COM`. ME does not run it: a
  line appended to ME's `AUTOEXEC.BAT` was not executed at the next cold
  start (`runs/run-44.md`, "44.49") [emu]. 98 SE also skips it when the
  user boots past it. Not chosen.
- **This project's WDM drivers** (`xhci98.sys`, `xhcicm.sys`): load after
  the configuration manager has read and allocated the bridges, so too
  late [inf].

## 6. The log

At `Sys_Critical_Init` the step keeps its results in its own data; at
`Device_Init`, when VMM's registry services are available [inf], it
writes them once, as values under
`HKLM\System\CurrentControlSet\Services\VxD\XHCIPFW` (section 12.10
adds `XhciPfwLines`, `XhciPfwLineSwitch` and bits 7 to 10 of
`XhciPfwResult`). Every name
carries the project's prefix `XhciPfw` (the longest, `XhciPfwBarsInside`,
17 characters); these names, their types and layouts are what a reader
of the log (`XHCITR`'s report) takes:

- `XhciPfwResult` (`REG_DWORD`): bit 0 the scan finished, bit 1 a bridge
  read otherwise at the close than at the scan (left as it was), bit 2 a write
  was refused, bit 3 a bridge was parked, bit 4 a window across 4 GB was
  found, bit 5 a table below was full, bit 6 an xHCI controller's BAR
  above 4 GB (section 4.4);
- `XhciPfwClosed`, `XhciPfwRefused`, `XhciPfwParked`, `XhciPfwAcross`,
  `XhciPfwBarsInside`, `XhciPfwFunctions`, `XhciPfwXhciHigh`
  (`REG_DWORD`) counts; `XhciPfwBarsInside` counts the BARs of section
  4.1 step 4, before the writes, whatever their bridge's outcome;
  `XhciPfwFunctions` the functions the scan found; `XhciPfwXhciHigh` the
  BARs of section 4.4;
- `XhciPfwBridges` (`REG_BINARY`): up to 16 records of 28 bytes, one per bridge
  that step 3 was to close or that was across 4 GB: byte 0 the bus, byte
  1 the device (bits 7:3) and function (bits 2:0), byte 2 the action (1
  closed, 2 refused, 3 parked, 4 across 4 GB, left), byte 3 0; then
  before the step and after it, each as three dwords, `24h` (base and
  limit together), `28h` and `2Ch`;
- `XhciPfwBars` (`REG_BINARY`): up to 16 records of 8 bytes: the bus, the device
  and function as above, the BAR's offset, a byte 0, then the BAR's upper
  dword;
- `XhciPfwXhci` (`REG_BINARY`): up to 8 records of 16 bytes, one per BAR of
  section 4.4: the bus, the device and function, the BAR's offset, a byte
  0, then the vendor and device ids (`00h` as read), the address's upper
  dword and its lower dword (the type bits cleared);
- `XhciPfwVersion` (`REG_SZ`): the image's stamp, `XHCIPFW.VXD` and the
  release.

A scan that finds more bridges above or across 4 GB than its table
holds (64) leaves the rest as they are and sets bit 5; the records hold
the first 16.

The `qemu` flavour also writes one line per action to QEMU's debug
console (port `0E9h`); the release image writes no port but `CF8h` and
`CFCh`, which its gate checks (section 8). `XHCITR`'s report and the
bench (record 22 section 18.6) read the values; the tools' changes for
that are not part of this record.

## 7. Windows 2000, XP and the NT 6 systems

The VxD does not run on them. 32-bit XP needs no step [emu]
(`runs/run-44.md`, "44.49", XP SP3 on its q35 base, a 40-bit processor,
the 4G-off layout): at each of two cold starts with the ORICO at power-on
and at one without it, the firmware left every ASM4242 bridge's window
above 4 GB (the root port's upper registers 1 and `40h`), and XP's own
configuration writes then put each below 4 GB with its upper registers 0
(the root port `CC900000h` to `CCFFFFFFh`, the upstream port, the tunnel
ports and the enclosure's bridges inside it), and every bridge was at
problem 0; the NVMe was present at the second start (problem 1, no
driver), not yet at the first's reading, the first start after the
bridges were found. XP x64 was not read. 10 x64 works with the option
on. 2000 and the 32-bit Vista, 7, 8, 8.1 and 10 are section 11
(the owner's Q5). 32-bit XP is offered `XHCIPFW.SYS` all the same (the
owner, 2026-10-27, section 11.3), its box ticked with the Thunderbolt
box as on every system (Q9): that XP moves the windows itself was read
on the emulator only, and the step closing them first leaves the same
end state there.

## 7a. After a resume

The step runs at a start only. Whether a firmware restores the bridges'
upper registers on a resume from standby, and what 98 SE and ME then
write, is not read [inf]. If the firmware put a nonzero upper base back
under low registers 98 SE or ME had left closed (`FFF0h`, `0000h`), the
window would be closed or wholly above 4 GB again, which they do not
address. If their own enumerator had meanwhile given that bridge a
prefetchable window below 4 GB (this project's numbering of a hot-plugged
chain gives none: record 21 section 6.1.1 closes them), a restored
upper base would move that window above 4 GB and leave the devices
behind it without a path until the next start. Section 10.2 item 5
reads the registers after a resume on both systems; a step that also
runs at the resume is a later option, not this draft.

## 8. The package and the installer

- **The file**: `XHCIPFW.VXD`, at the root of every x86 flavour
  directory (release, and debug up to `3.3.0.0`, with or without Stor98; the qemu
  flavour's image in the qemu package), built by
  `scripts\build-driver.cmd` as `S98VSD.VXD` is (`src\pfw\build.cmd`),
  with a gate of record 30 section 6.4's shape (`src\pfw\check-pfw.ps1`):
  an LE image whose one entry is the DDB `XHCIPFW `, no VMM service in
  its code but the registry ones of section 6 and section 12.10's two
  reads (`src\pfw\pfw-services.allow`), the stamp `XHCIPFW.VXD <version>`, in
  the release image no port instruction outside `pfw_io.obj` and there
  only `CF8h` and `CFCh`, no write to port `0E9h`, and every LE fixup
  an internal 32-bit offset inside its target object; the build's link
  guards are record 30 section 6.1's, the third mislink's included.
  `make-package.ps1` holds it to the gate again and stages it under the
  hash it had then; `make-release.ps1` refuses a cut without it, older
  than its sources, or published other than the build's bytes.
- **The installer** (98 SE and ME here; the NT systems are section
  11.3), the owner's Q2 and his presentation of 2026-10-27: a box of its
  own on the Components page, "Close PCI windows above 4 GB
  (XHCIPFW.VXD)", the Thunderbolt 3 / USB4 box's child: under that box's
  words and above Stor98's box (the order Thunderbolt 3 / USB4,
  XHCIPFW, Stor98, `S98VSD.VXD`), indented as Stor98's is, greyed and
  unticked while the Thunderbolt box is unticked, its own state back
  when that box is ticked again; ticked by default with it; shown where
  the package carries the file. The page stays 210 dialog units high
  (640 by 480 on 98 SE with the storage row): the tools folder's line
  moved beside the Settings and Change folder buttons, right of Change
  folder (its path shortened in the middle where long), which gives the
  row its room; on a system that does not show the box the rows below
  move up into its place. The console asks the same question right after the Thunderbolt
  one and before Stor98's (default yes), and with no Thunderbolt support
  in the run leaves it out with a line saying so; `/xhcipfw` (refused
  without `/thunderbolt` or `/cmonly`, as `/s98vsd` is without Stor98)
  and `/noxhcipfw` answer it. A run without the Thunderbolt support
  leaves an installed one as it is, as a No to the Thunderbolt question
  leaves that driver. Ticked, the
  file goes to the Windows `SYSTEM` folder by record 30 section 7.3's
  table, the same planner `S98VSD.VXD` uses (its own record,
  `HKLM\Software\xhci98\XhciPfw`, values `File` and `Pending`; the
  `.TMP` beside it is `XHCIPFW.TMP`; an update of a loaded file through
  `WININIT.INI`'s `[rename]`; another program's file of that name left
  and said), and once the table leaves this installer's file in place
  (placed, queued or as it was) the static-VxD key's two values are
  written (`StaticVxD` and `Start`, section 5.1), with a restart flag
  when anything changed. Unticked, or at the uninstall: the static-VxD
  key is deleted first (with the log values in it), unless the file in
  `SYSTEM` is another installation's, then the table removes this
  installer's file. A package without the file changes nothing (an
  installed one is kept). On every other system nothing is installed.
- **Removal without the installer**: deleting the key (or starting in
  Safe mode, which loads no third-party static VxD [inf]) leaves the
  windows as the firmware left them.

## 9. The owner's questions

**Answered** (the owner, 2026-10-27; `roadmap-usb4.md`, decisions table,
row "44.49"):

- **Q1: (a)**, the static VxD from the registry's `StaticVxD` key,
  `SYSTEM.INI` `device=` the fallback; that it loads early enough and that
  its registry writes work at `Device_Init` read on 98 SE and ME before the
  rest of the code.
- **Q2: (a)**, a box of its own on 98 SE and ME, ticked by default.
- **Q3: (a)**, every bridge whose prefetchable window lies wholly above 4
  GB, whatever its ids (a window across 4 GB left and logged).
- **Q4: (a)**, closed, not sized.
- **Q5: (c)**, 2000 and the 32-bit Vista, 7, 8, 8.1 and 10 handled in
  task 44.49 too: an ACPI-HAL 2000 guest read, what can run before
  `pci.sys` on 2000 and the NT 6 x86 systems found (8's boot hang given
  its cause first), and that design written as a section of its own,
  converged before its code.
- The owner also asked, 2026-10-27, that the step log (never move) any
  xHCI controller whose BAR lies above 4 GB: section 4.4.

The questions as they were put:

- **Q1, where it runs** (section 5): (a) the static VxD at
  `Sys_Critical_Init`, loaded from the registry key; (b) the same VxD
  from `SYSTEM.INI`; (c) a real-mode program from `AUTOEXEC.BAT`, 98 SE
  only. Recommended: (a), the one route that runs on both 98 SE and ME
  before the configuration manager, with (b) as its fallback if the
  legs show (a) loading late. Either way the static-VxD loading and
  `Device_Init`'s registry writes are read on both systems before the
  rest of the code (section 5.1).
- **Q2, the installer**: (a) a box of its own on 98 SE and ME, ticked by
  default; (b) a box of its own, unticked by default; (c) no box,
  installed with the Thunderbolt and USB4 support. Recommended: (a): it
  writes nothing on a board without such a window, so the default costs
  nothing there, and a box lets a user who meets a board where it harms
  leave it out.
- **Q3, other boards** (section 4.3): (a) every bridge whose
  prefetchable window lies wholly above 4 GB, whatever its ids, closed; a
  window across 4 GB left and logged (section 4.1); (b) only the ASM4242
  (`1B21:2421`, `1B21:2423`) and the bridges above it; (c) as (a), but a
  window with a BAR inside it left as it is; (d) as (a), and a window
  across 4 GB clipped at 4 GB (its part below kept), with the resume
  question of section 7a for it read first. Recommended: (a): 98 SE and
  ME can reach nothing above 4 GB on any board, and a window left there
  is read by them as its low halves. Its risk, stated: on a board whose
  firmware put a device's prefetchable BAR above 4 GB, that device loses
  its path; 98 SE and ME could not use it there before, but whether their
  enumerator then moves it into the non-prefetchable window is not read
  (section 4.2) until section 10.2 item 6. A device hot-plugged later
  behind a tunnel port is not made worse: without the step that port's
  window is the firmware's, above 4 GB, where they cannot use it either,
  and the chain record 21 numbers gets its own prefetchable windows
  closed in any case. (c) keeps exactly the windows that misread worst;
  (b) leaves the next board with the same windows unhelped.
- **Q4, close or size** (section 4.2): (a) close (size zero); (b) a
  Linux-like size and place, which needs the free space below 4 GB the
  step cannot know before Windows. Recommended: (a).
- **Q5, 2000 and the NT 6 systems** (section 7): the task line asks for
  2000 past "Starting up" on the 4G-off layout, which this design does not
  give. (a) Amend the task line: the cause recorded (section 2), 2000 on
  real hardware left to the hardware bench (record 22 section 18.6 step
  1) and its leg on the 4G-off layout dropped from 44.49; (b) keep the checkpoint and
  read next whether an ACPI-HAL 2000 guest stops the same way (a q35 base
  with the ACPI HAL is not at hand), then whether any boot component of
  2000 can precede `pci.sys`, before designing a 2000 route; (c) as (b)
  for the 32-bit NT 6 systems too. Recommended: (a) for `3.3.0.0` with
  (b) as a later task: the 2000 base read here runs the Standard PC HAL,
  a real ASM4242 firmware would run the ACPI one, and no component of this package runs
  before `pci.sys` today.

**Answered** (the owner, 2026-10-27, on section 11):

- **Q6: (a)**, `XHCIPFW.SYS` on 2000 and the 32-bit Vista, 7, 8, 8.1 and
  10 in `3.3.0.0`. Narrowed on 2026-10-28 to 2000 to 7 (below, "2000 to
  7 only").
- **Q8: (b)**, 2000 held until an ACPI-HAL reading. That reading is
  section 2's and section 11.1's (a q35 2000 base with the ACPI HAL): the
  same stop, and the step at boot start lets it start. So 2000 is in.

- **Q7: the box ticked where the problem is seen** (the owner, after an
  opinion asked of a reviewer): ticked by default on 2000 and 8, which do
  not start with such a window; on Vista, 7, 8.1 and 10 x86 the installer
  ticks it itself when it finds a PCI bridge at problem 12
  (`CM_PROB_NORMAL_CONFLICT`) whose prefetchable window lies wholly above
  4 GB, Windows' own sign that it could not use the window; otherwise the
  box stays unticked and the user may tick it (`/xhcipfw`). Replaced by
  Q9 below.
- **The installer's presentation** (the owner, 2026-10-27, after Q7):
  the box names its file per system ("Close PCI windows above 4 GB
  (XHCIPFW.VXD)" on 98 SE and ME, "(XHCIPFW.SYS)" on the NT systems),
  and the console's question names it the same way; it is the
  Thunderbolt 3 / USB4 box's child, under it and above Stor98 (the
  order Thunderbolt 3 / USB4, XHCIPFW, Stor98, `S98VSD.VXD`), indented,
  greyed and unticked with that box unticked; the console leaves it out
  when no Thunderbolt support is in the run, and refuses `/xhcipfw`
  without the Thunderbolt part as it refuses `/s98vsd` without Stor98.
  It is offered on every 32-bit system from 2000 to 10, XP included,
  never on a 64-bit system (its default then Q7's, now Q9's). The
  log's values carry the project's prefix (section 6).

- **Q9: the box ticked whenever the Thunderbolt box is** (the owner,
  2026-10-27, replacing Q7's rule): on every 32-bit system it is offered,
  98 SE to 10 with XP, XHCIPFW is ticked by default whenever the
  Thunderbolt 3 / USB4 box is ticked; the user may untick it, and
  `/noxhcipfw` unticks it. The Thunderbolt box itself stays unticked by
  default (the rule of 2026-10-19), its locked-on cases kept. The
  owner's reason for XP: that XP moves the windows itself was read on the
  emulator only, and the step closing them first leaves the same end
  state there, while it covers a real XP that behaves otherwise. The
  question as it was put: the Thunderbolt box is unticked by default, so
  on a board where the window hides the controller a first install left
  XHCIPFW out with its parent; (a) the Thunderbolt box ticked where
  XHCIPFW's condition is seen, (b) XHCIPFW not its child, (c) as it was.
  The owner kept the Thunderbolt default and the child relation and
  made XHCIPFW's own default follow its parent; on 2000 to 10 the
  console says, when the Thunderbolt support is left out and a PCI
  bridge is at problem 12 with a window above 4 GB, to install that
  support (`/thunderbolt`) so that XHCIPFW is installed too (the page
  carries no such line: it has no row to spare on 98 SE).

- **2000 to 7 only** (the owner, 2026-10-28, after task 44.72's
  readings, `runs/run-44.md` "44.72" and "44.72 (continued)"; its option
  (b')): `XHCIPFW.SYS` is offered on the 32-bit 2000, XP, Vista and 7
  only. From 8 on the step's start and the PCI driver's read of the root
  bus are not ordered (section 11.1), so on the 32-bit 8, 8.1 and 10 it
  is no longer offered, ticked or placed, and `/xhcipfw` is refused there
  (section 11.3). 98 SE and ME keep `XHCIPFW.VXD`; no 64-bit system is
  offered either. The owner's option (c), a step in the PCI root bus's
  stack for 8, 8.1 and 10 with its timing read first, is planned work
  (`roadmap-usb4.md`, PFW8.1). Q9's rule stands on the systems still
  offered it.

## 10. Verification

### 10.1 Host vectors

The scan and the decision as a pure part over a configuration-space model
(`test\test_pfw.c`): each classification of section 4.1 step 2, the type
nibbles kept and compared apart from the address fields, a window across
4 GB left and logged; the write order of step 3 with every state checked
against its invariant for upper halves (base, limit) of (1, 0), (1, 1),
(1, 40h), (FFFFFFFFh, FFFFFFFFh) and (B8h, F7h), with low halves open,
closed and equal; each write in turn ignored and every state of its undo
checked; each write in turn reading back a third value and an undo
ignored (refused, parked, the states logged, no claim on them); the scan's bounds (an
absent function 0, a multi-function device, header types 0, 1, 2 and
with bit 7 set, a bridge with secondary bus 0, a bus no bridge leads to,
256 buses); the BAR count over a subtree's bus range; the log records
and their sizes (28, 8 and 16 bytes); the xHCI log of section 4.4 (a
controller with a BAR above 4 GB recorded, one below and another class
not, nothing written); a bridge that reads otherwise at the close than
at the scan left; the tables' limits; the emulator's nine bridges as
the VxD read them.

### 10.2 Legs (q35, the emulator's 4G-off layout, the ORICO at power-on)

1. 98 SE and ME with `XHCIPFW.VXD` installed (the qemu image by hand
   before the code was reviewed, read: section 5.1): the `pci_cfg_read` and
   `pci_cfg_write` traces show the step's accesses before any other
   access to a bridge's `24h` to `2Ch` after the firmware's, and the
   closed form on every bridge the emulator gives a nonzero upper base
   (on these bases the ASM4242's root port, its upstream port, both tunnel
   ports, the enclosure's two bridges, the xHCI's and NHI's ports, and
   the q35 board's own root port 00:1c.0 of the `qemu-xhci`, nine); every ASM4242
   function, both tunnel ports, the enclosure's bridges and the NVMe
   without problem 12; the drive's letter; the registry values.
2. The same with the `qemu` image: the debug-console lines.
3. 98 SE on the 4G-on layout (option on) and on the low-window layout, and on
   the q35 board without a USB4 model: nothing written where no window
   has upper halves (the 4G-on layout on the `pentium3` bases has such
   windows, record 22 section 18.5, and is closed like the 4G-off one).
4. Shut Down and a cold start again (the step runs at every start).
5. Standby and resume on 98 SE and on ME (section 7a): the registers
   after the resume, the drive still there.
6. A device with a 64-bit prefetchable BAR that the firmware places above
   4 GB behind a root port (a QEMU device of that kind on the q35 board),
   on 98 SE and on ME: what the enumerator does with the BAR once the step
   closed its window (problem code, the BAR's address), for Q3. A device
   with a prefetchable BAR arriving by hot plug behind a tunnel port is
   not readable on these vehicles (the enclosure models carry none); Q3
   states why the step does not make that case worse.
7. The installer on 98 SE and on ME (section 8), the release package:
   the box shown and ticked, the file in `SYSTEM`, the record and the
   static-VxD key written, the step at the next cold start (items 1 and
   4); a run with the box unticked, then the uninstall, each removing the
   key and the file.
8. The hardware bench, at 44-E.6.

## 11. 2000 and the 32-bit Vista, 7, 8, 8.1 and 10 (the owner's Q5)

### 11.1 What runs before `pci.sys` reads the bridges (read)

On every NT system the PCI driver is a boot-start driver of the group
`Boot Bus Extender`, and the I/O manager initialises the boot-start
drivers group by group in the order of
`HKLM\SYSTEM\CurrentControlSet\Control\ServiceGroupOrder`, whose first
entry is `System Reserved` (read on 2000; the same first entry on the NT
6 systems [inf]). So a boot-start driver of that group has its
`DriverEntry` called before `pci.sys`'s and before the PCI driver reads
any bridge [inf]. Read with a probe of this project's own (a boot-start
driver of `System Reserved` that marks its `DriverEntry` in QEMU's
configuration trace by three reads of 00:00.0 offset `F8h`; not tracked)
on the 4G-off layout [emu]:

- 2000 (the q35 base, Standard PC HAL): the probe's mark at trace line
  311 854, the first read of the root port's window by the PCI driver at
  311 914, its first write at 314 443. Two earlier reads of the windows
  come before the probe (lines 5 335 and 154 722, at the loader's and the
  HAL's time [inf]); neither writes them.
- 7 x86: the probe's mark at line 13 330, the first read of the root
  port's window by Windows at 13 406; before the probe only the
  firmware's accesses.

A second probe that runs this record's step (`pfw_scan.c`, through
`CF8h` and `CFCh`) at that `DriverEntry` and prints its counts to the
debug console, on each system's q35 base, installed on a start without
the USB4 model and then started cold on the 4G-off layout [emu]:

| System | Without the step (section 1) | With the step at boot start |
|---|---|---|
| 2000 (Standard PC HAL) | stops at "Starting up" | the desktop; every ASM4242 bridge, both enclosure bridges, the xHCI, the NHI and the NVMe enumerated (found new hardware) |
| 2000 (ACPI HAL) | stops at "Starting up" (section 2) | the same as under the Standard PC HAL; the probe's mark at trace line 13 107, the PCI driver's first read of the root port's window at 13 119 |
| Vista SP2 x86 | root port problem 12, nothing behind | the root port and every bridge problem 0; the xHCI and NHI problem 1 |
| 7 SP1 x86 | root port problem 12, nothing behind | every bridge 0; the NVMe, xHCI and NHI 28 (no driver) |
| 8 x86 | no Start screen | the Start screen; every bridge and the xHCI 0, the NVMe and NHI 28 |
| 8.1 x86 | root port problem 12, nothing behind | every bridge, the NVMe and the xHCI 0, the NHI 28 |
| 10 x86 (19045) | root port problem 12, nothing behind | every bridge, the NVMe (stornvme) and the xHCI 0, the NHI 28 |

The step closed nine bridges on each ("C 00000001 00000012 00000009 ..."
on the debug console). On 8 and 10 the step runs at each start only with
fast startup off (`powercfg /h off`): a fast-startup start resumes the
kernel and does not call boot-start drivers again [inf]. From 8 on a full
start is not enough: whether the step comes before the PCI driver varied
from start to start (on 8 first at one start of four, on 8.1 and 10 at
five of six each; `runs/run-44.md` "44.72" and "44.72 (continued)").
On Vista and 7 the step came first at every start read (four each). So
the boot drivers of `System Reserved` and the PCI driver's read of the
root bus are not ordered from 8 on [inf], and `XHCIPFW.SYS` is offered
on 2000 to 7 only (the owner, 2026-10-28; section 11.3).

**8's hang** (the cause asked first): with the windows above 4 GB 8's
PCI driver probes the root port's window with all-ones, writes it back
and clears the root port's AER status, and no configuration write
follows; one processor then idles in the HAL and the other runs the
kernel on its idle stack [emu], so the start waits rather than loops
[inf]. The low-window control and the step both give the Start
screen [emu], so the windows above 4 GB are what makes it wait [inf];
which wait never ends is not read.

**8's installed leg** (the release image, the 4G-off layout, installed
on the q35 board without the USB4 model, fast startup off): the first
start stopped at the spinner as without the step, every window still
above 4 GB; the next start without the USB4 model read the driver's log
`XhciPfwSkipped` 4, "late": 8's `\Driver\PCI` object already exists
when this driver's `DriverEntry` runs [emu]. That 8 reads the bridges
after it was read from the probe's first start (the table above); task
44.72 read later starts: on 8 the PCI driver probed the root bus before
this driver was called at three starts of four, the order varying with
the start, as on 8.1 and 10 less often (`runs/run-44.md` "44.72" and
"44.72 (continued)"). 2000, 7, 8.1 and 10 ran the
step under the same check [emu]. So the check reads something else
(section 11.2).

**10 x86 with the option on** (the 4G-on layout): 10 x86 reaches Windows'
recovery screen instead of the desktop, without the step and with it
alike [emu]; only the windows lie above 4 GB there [emu], so the step's
close does not cure it [emu] and its cause is not read (no error text
was taken).

### 11.2 The choice: a boot-start driver of `System Reserved`

`XHCIPFW.SYS`, an x86 kernel driver (Type 1, `Start` 0, `ErrorControl`
0, `Group` `System Reserved`, no device, no INF), built with the Windows
2000 DDK as the x86 drivers are, whose `DriverEntry` runs this record's
step (`pfw_scan.c`, the same pure half the VxD links) once and writes
section 6's values under its own service key's `Parameters` subkey,
with `XhciPfwSkipped` (bit 0 not a system it acts on, 1 Safe Mode, 2
late, 3 unheld), `XhciPfwMode` (0 nothing, 1 one processor, 2 inside
`KeIpiGenericCall`), `XhciPfwHoldTicks` and `XhciPfwTickHz` (the step's
length in the performance counter's ticks and its frequency, low
dwords), all `REG_DWORD`, written before the others.

- **Configuration accesses**: mechanism #1, `CF8h` and `CFCh`, through
  `WRITE_PORT_ULONG` and `READ_PORT_ULONG`, as the probes did. Not the
  HAL's `HalSetBusDataByOffset`: its documented contract keeps a caller
  from writing a Type 1 header's base and limit registers, the very
  registers the step writes. **Exclusion**: the address port is shared,
  so another access between a `CF8h` write and its `CFCh` access, read or
  write, would redirect one of them. The whole step (scan, closes, undo)
  therefore runs with no other configuration access possible:
  - **Vista and later**: inside one `KeIpiGenericCall`, resolved at run
    time with `MmGetSystemRoutineAddress` (a direct import would keep the
    one image from loading on 2000; the kernel exports it from Server
    2003 SP1 on, so on every system the installer offers this from Vista
    on): every processor enters the callback at `IPI_LEVEL` at once; the
    first to enter (an interlocked flag) runs the step, every other one
    spins until it sets a done flag; the service returns once all have
    left. No queue and no cleanup outlive the call. The blackout is the
    step's whole length on every processor: at least 16 384 port
    operations (one pass of 8 192 reads, each two port accesses), 32 768
    when a window is found (the second pass), and the reads and writes of
    the functions found, tens of milliseconds at
    a start before any device runs [inf]; it is measured and logged
    (`XhciPfwHoldTicks`, `XhciPfwTickHz`), and the leg reads it. Where the
    routine does not
    resolve on a multiprocessor system the step writes nothing (an
    "unheld" bit).
  - **2000 and XP** (no such service: the kernel exports it from
    Server 2003 SP1 on): the step runs only when one processor runs
    (`KeNumberProcessors` 1, a uniprocessor HAL or `/NUMPROC=1`), at
    `HIGH_LEVEL`, so no interrupt handler intervenes and no other
    processor exists to. On a multiprocessor 2000 or XP it writes nothing
    (an "unheld" bit): another processor's access between its `CF8h` and
    `CFCh` cannot be excluded there (section 9 Q8). Server 2003 (5.2) is
    not offered and gets the "not a system it acts on" bit.
  The probes closed all nine bridges and read every register back as
  written on two-processor starts of Vista to 10 with no exclusion [emu],
  which shows the accesses work, not the exclusion; the legs read the
  two-processor start. The offsets the step uses are all below `100h`,
  which mechanism #1 reaches on every PC chipset, whatever the HAL uses
  itself (ECAM included).
- **At a start only, never later**: no unload routine, so the driver
  cannot be stopped and started again; the installer writes its service
  key and never starts it; and `DriverEntry` acts only at the boot
  drivers' time, which it reads as the `SOFTWARE` hive not loaded yet
  (`ZwOpenKey` on `\Registry\Machine\SOFTWARE` answers that the name
  does not exist; success or any other failure counts as late): the
  loader brings only the `SYSTEM` hive, and the others are loaded after
  the boot drivers have been initialised [inf], which the legs read on
  each system (no "late" bit at a start). Otherwise it writes
  `XhciPfwSkipped` with a "late" bit and does nothing, so a start by
  hand after Windows assigned the windows changes nothing. That the step
  comes before the PCI driver reads the bridges rests on the group
  order and on section 11.1's readings, not on this check; the PCI
  driver's object is no sign of it (8, section 11.1). On Vista and 7 the
  order held at every start read; from 8 on it varies from start to
  start: on 8 the PCI driver probed the root bus first at three starts
  of four, on 8.1 and 10 at one of six each (`runs/run-44.md` "44.72"
  and "44.72 (continued)"). So this place holds on 2000 to 7 only, the
  systems it is offered on (section 11.3); a place for 8 to 10 is the
  first alternative below, its timing not read (planned work, PFW8.1).
- **Safe Mode and recovery**: a boot-start driver loads in Safe Mode
  whatever `SafeBoot` lists, so `DriverEntry` reads `InitSafeBootMode`
  first and, in any Safe Mode, writes `XhciPfwSkipped` with a "safe mode" bit and
  touches no register: Safe Mode then starts as without the step, as 98
  SE's and ME's do without the VxD (on 2000 it stops where it stopped
  before, section 2). The service key's `Start` set to 4 (from Safe Mode,
  the recovery console or another system's registry editor) removes it
  from the next start. `ErrorControl` 0: a failure to load stops nothing.
- **Update and removal**: the driver stays in memory until the next
  start, so the installer never stops it. The loader reads a boot-start
  image before a pending rename (`MOVEFILE_DELAY_UNTIL_REBOOT`) runs, so
  an update cannot replace the file in place for the next start: it
  writes the new image under the other of two names (`xhcipfw.sys`,
  `xhcipf2.sys`). The installer's record holds the hashes it placed:
  `File` (the image the service runs), `Pending` and `Slot` (the image
  being put in its place and the name it is written under) and `Old` (the
  image an update leaves to delete). An update names the image in use
  `Old` (an image in use that cannot be read stops it), sets `Slot` and
  `Pending` before it writes a byte (so a copy cut short in that slot is
  its own, and only there), writes and compares the new image (a file of
  that name that is not its own stops it), points the service's `ImagePath`
  at it (`Start` 4 while the service's values are written, 0 last, so a
  run cut short leaves it off, never on with a half-written service),
  sets `File` and deletes `Pending`; then it deletes each file of the two
  names that is its own but the new one, and `Old` once they are gone (at
  the next start if in use). An installed driver is a service whose image
  path is one of the two names in the drivers folder and whose image is
  one the record holds, or cannot be read (an unresolved read never
  removes it); a run that finds this release's image registered but off
  turns it on and asks for a restart. A service with no record of this
  installer's, or whose image path is not one of its two names in the
  drivers folder, is another installation's and is left as it is; a
  service key without an image path under this installer's record is its
  own creation cut short. The log's `XhciPfwVersion` names
  the image that ran. A removal (of a service whose image is one the record
  holds, or that has no image yet; 2026-10-28) sets `Start` to 4 first (so a removal cut
  short leaves the step off), deletes the service key (a key that cannot
  be deleted stays off, said, and the record is kept), then each file of
  the two names that is its own (one that cannot be read keeps the
  record, said), and the record last.
- **Fast startup, sleep and hibernation**: on 8, 8.1 and 10 a fast-startup
  start resumes the kernel and does not call boot drivers [inf]; the
  windows Windows assigned at the full start that preceded it are then
  kept by Windows itself, as after any resume [inf]. So the step runs at
  full starts only, and the driver does nothing at a resume from sleep or
  hibernation (a close then would remove a window Windows owns). Section
  11.4 reads a resume from standby and from hibernation on 7 and 10, and
  a fast-startup start on 10 (written before 8 to 10 were withdrawn,
  section 11.3; on 10 that reading now concerns PFW8.1).

The step, its invariant, its log and its limits are sections 4 and 6,
unchanged.

The alternatives, not chosen:

- **An upper filter on the PCI root bus** (`PNP0A03`, `PNP0A08`): it
  could act in its start routine before passing the start down, but
  whether the PCI driver has read the bridges by then (at `AddDevice`, or
  at an earlier query) is not read, and the step would then depend on the
  root bus's device stack on each system.
- **A step in the boot loader's chain** (`boot.ini` on 2000, the BCD on
  the NT 6 systems): runs before Windows on every system, but it changes
  how the computer starts, which an installer must not do for this.
- **The firmware's setting**: none places the windows below 4 GB (record
  22 section 18.5).

### 11.3 Where it is installed, its scope and its default (questions for the owner)

Unlike 98 SE and ME, a 32-bit NT 6 system with PAE can address device
memory above 4 GB [inf], so a window above 4 GB can be one Windows uses
when the host bridge's `_CRS` offers a window there (the firmware's option
on), and closing it would take that path from a working device; the
step runs before ACPI and cannot read `_CRS` (section 4.2). On the
emulator 10 x86 with the option on does not start, with the step or
without it (section 11.1), so no working case is at hand to read. 2000
did not start with such a window here (section 2), whether another
edition or board lets it use one is not read. Hence who decides to
close, and where, was the owner's question (section 9 Q7), answered below.

The installer offers it as a box of its own, "Close PCI windows above 4
GB (XHCIPFW.SYS)", on the 32-bit 2000, XP, Vista and 7 (the owner's Q6
and his presentation, narrowed on 2026-10-28, section 9), the
Thunderbolt 3 / USB4 box's child placed as on 98 SE and ME (section 8),
never on any 64-bit system (no 64-bit image; 10 x64 works with the
option on). Its
default (the owner's Q9, which replaced Q7's rule): ticked whenever the
Thunderbolt box is, on every system, `inst_pfw_box` the one decision;
unticked and greyed with that box unticked. With the Thunderbolt
support left out, the console's line says, where the installer reads a
PCI bridge at problem 12 (`CM_PROB_NORMAL_CONFLICT`, its compatible ids
naming class `0604h`) whose `BootConfig` holds a prefetchable memory
window starting at or above 4 GB (`inst_pfw_bootcfg_high`), to install
that support (`/thunderbolt`) so that XHCIPFW is installed too; the
readme says the same for a USB4 or Thunderbolt controller whose root
port shows "This device cannot find enough free resources" (code 12).

**The 32-bit 8, 8.1 and 10** (the owner, 2026-10-28; task 44.72; from
`3.4.0.0` 8 and 8.1 are offered again in the filter role of section
11.5, the owner, 2026-10-29, and what follows holds for 10 alone): not
offered, because there the step's start and the PCI driver's read of the
root bus are not ordered (section 11.1). `inst_pfw_sys` gives them a kind
of their own, `INST_PFW_SYS_GONE`: no box on the page and no console
question (`inst_pfw_box` shows none); `/xhcipfw` refused before any
change with one line, "REFUSED: /xhcipfw: XHCIPFW.SYS may start too late
on Windows 8, 8.1 and 10.", and `/noxhcipfw` with the line every system
not offered it gets. A run there (`inst_pfw_mode` `DESELECTED` whatever
the boxes and whether the package carries the file) removes an
`XHCIPFW.SYS` an earlier test install of `3.3.0.0` placed, by section
11.2's removal and its ownership rules: only when this installer's record
holds something (or cannot be read, which stops it with a step said);
then the service only when its image path is one of the two names and
its image is one the record holds (another installation's bytes there
leave the service, said; an image that cannot be read removes nothing),
and each file only when its hash is one the record holds. The console
says the removal where the question would be, `/list` included. With no
record the run says nothing of it, and a service named `xhcipfw` that is not this
installer's is left as it is. The uninstall removes it as on every
system. The window's Summary carries the removal's line, the box being
absent. The readme, `README.md` and the release notes say that on a board
that leaves the USB4 ports' windows above 4 GB these systems cannot use
the devices behind those ports, and that 8 may not start there at all
(section 11.1's table: 8 x86 without the step reaches no Start screen
[emu]). A step that runs early enough there is planned work
(`roadmap-usb4.md`, PFW8.1).

**"Above 4G Decoding" on** (`runs/run-44.md`, "44.56"): with the
firmware's option on, the 32-bit Vista to 10 stop at start with `0x7B` once
the host bridge's 64-bit `_CRS` window reaches 128 GB or more [emu],
before any step of this record could help, and 10 x86 shows Windows'
recovery screen with the step (section 11.1). So for the 32-bit Vista
to 10 on such a board the readme says, and on Vista and 7 the
installer's question too: set "Above 4G Decoding" off in the firmware
setup; on Vista and 7 `XHCIPFW.SYS` then closes the windows the firmware
still leaves above 4 GB. 2000, XP, 98
SE and ME with the option on were read apart (the owner, 2026-10-27;
`runs/run-44.md`, "44.49"), without this project's package and then
with it.

**"Above 4G Decoding", per system** (guidance derived from the
emulator's legs [emu], `runs/run-44.md` "44.48" and "44.49" and the
paragraph above; the readme gives the option-off line for the 32-bit
Vista to 10, the console's question for Vista and 7; the hardware bench at
44-E.6):

| System | The option | Why |
|---|---|---|
| XP (32-bit) | off | with it on, XP without the package stopped with `0xA5`, first parameter 2, at every host-bridge 64-bit window 4 GB long or longer and started below that, so the length is the rule [inf]; with `XHCIPFW.SYS` installed it stops the same on the 256 GB layout (the window is ACPI's `_CRS`, which the step does not change [inf]); off, XP moves the bridges' windows below 4 GB itself |
| 2000, ACPI HAL | off | on: `0xA5` (2, ...) as XP, with the step as without it; off: the step closes the windows and 2000 starts with every device behind the ASM4242 |
| 2000, Standard PC HAL | either, with `XHCIPFW.SYS` | without the step it stops at "Starting up" with the option on as off (section 2); with it, it starts on both layouts, every ASM4242 bridge, both enclosure bridges and the NVMe in `Enum\PCI` (this HAL does not read `_CRS` [inf]) |
| the 32-bit Vista, 7, 8, 8.1 and 10 | off | on: `0x7B` at a host window of 128 GB or more (the paragraph above), 10 x86 Windows' recovery screen with the step as without it; off: the step closes the windows; on Vista and 7 before the PCI driver at every start read (four each); from 8 on the order varied from start to start [read], the two not being ordered there [inf]: on 8 the step came first at one start of four (the whole package's first start stopped too, "44.49"), and 8 stopped at the others as without it; on 8.1 and 10 the step came first at five starts of six each, 8.1's other start stopped and showed Windows' recovery screen, 10's reached the desktop with the windows closed after the PCI driver had read them (`runs/run-44.md` "44.72" and "44.72 (continued)"; a step that runs earlier there would be a new design); so from 2026-10-28 (the owner) `XHCIPFW.SYS` is offered on Vista and 7 only, and on a board that leaves the windows above 4 GB the 32-bit 8, 8.1 and 10 cannot use the devices behind them, 8 possibly not starting at all |
| 98 SE and ME | either, with `XHCIPFW.VXD` | on these bases the firmware leaves the same windows above 4 GB on both layouts; with the step both start with every bridge at problem 0, the NVMe on Stor98; without it the tunnel port 02:00.0 is at problem 12 with nothing behind it, either way |
| 10 x64 | on | on: every bridge, the xHCI and the NVMe at problem 0, the windows used above 4 GB; off: the root port at problem 12 with nothing behind it, although 10 x64 moves the windows itself ("44.49"'s first table), and no step helps (there is no 64-bit image) |

### 11.4 Verification (when it is coded)

Host vectors: the "late" check's decision and the log's values (the
pure half's vectors are section 10.1's). Legs on
the 4G-off layout, each 32-bit system above and 2000: the installer's
box, the registry entries, a cold start, the debug-console counts (the
qemu image), the problem codes of section 11.1's table, the trace's
order (the step's writes before the PCI driver's first read of a
bridge's window); the low-window layout and the q35 board without a USB4
model: nothing written; a start with the service off (`Start` 4), then
`Start` 3 and `sc start` after the boot: nothing written, the "late" bit
(and at every start of each system: no "late" bit, 8 above all);
Safe Mode: loaded, nothing written, the "safe mode" bit; a two-processor
start of an NT 6 system: no access but the step's own between its first
and last in the trace, and the blackout's logged length; the same with
the routine's resolution failing (a test image that looks up a name that
does not exist): no configuration access, the "unheld" bit; 2000 on two
processors (where its HAL allows): no configuration access, the "unheld"
bit, and with `/NUMPROC=1` the close; on 7 and 10 a resume from
standby and from hibernation, and on 10 a fast-startup start: the windows
as Windows left them, the devices working; the uninstall removes
the service key and the file. 2000 under both HALs (the q35 bases with the
Standard PC HAL and with the ACPI HAL), and the hardware bench at 44-E.6.

For the withdrawal from 8 to 10 (section 11.3, 2026-10-28): host vectors
of `inst_pfw_sys`, `inst_pfw_box` and `inst_pfw_mode` for the three
systems (not offered, never shown or ticked, removed whatever the boxes,
removed at the uninstall) and for Vista and 7 still offered; legs of the
window and the console on 10 and 8.1 x86 (no box, `/xhcipfw` refused,
nothing placed) and on 7 x86 (the box shown and ticked with the
Thunderbolt box), `runs/run-44.md` "44.72 (the owner's decision)".

### 11.5 The 32-bit 8, 8.1 and 10: a filter in the PCI root bus's stack (task 45.9)

Added 2026-10-29 for `roadmap-usb4.md` task 45.9 (PFW8.1's reading and
design, section 11.2's first alternative), and its code in `3.4.0.0`
(the owner, 2026-10-29).

#### 11.5.1 The reading

A probe of this project's own (not tracked; `runs/run-45.md`, "45.9", has
its hash, its install and every start): a PnP filter driver registered
as **both** the upper and the lower filter of the root bus's devnode
(`ACPI\PNP0A08\0`, the q35 board's PCI Express root complex, compatible
id `*PNP0A03`), a boot-start service of group `PnP Filter`, which marks
its `DriverEntry`, each `AddDevice` and its first `IRP_MN_START_DEVICE`
in QEMU's configuration trace (three reads each of 00:00.0 at `F8h`,
`F4h` and `F0h`) and runs this record's step (`pfw_scan.c`, inside
`KeIpiGenericCall`) at that first start, before passing it down. On the
4G-off layout with the ORICO at power-on, two processors, fast startup
off, each start cold [emu]:

| System | Starts | Order read at every start | Result |
|---|---|---|---|
| 8 x86 | 6 (4 read and write traces, 2 write traces) | `DriverEntry`, the lower filter's `AddDevice`, the upper filter's `AddDevice`, the upper filter's start (the step: 9 bridges closed), then the PCI driver's probe of the root port's window | the Start screen at all six; every ASM4242 bridge and both enclosure bridges problem 0 |
| 8.1 x86 | 5 (3 read and write, 2 write) | the same, at the same trace lines as 8 | the desktop at all five; every bridge problem 0, only the NHI and QEMU's `fw_cfg` 28 |
| 10 x86 (19041 base) | 2 with the filter in group `PnP Filter`, 1 in group `Core` (fresh overlay), 1 on the board without the USB4 model | **the filter's `DriverEntry` never entered** while the root bus devnode named it; the PCI driver probed the root port all the same (trace line 44 715) | no start: Windows' recovery ("Automatic Repair", "Diagnosing your PC") and, at the second start, the stop `INACCESSIBLE_BOOT_DEVICE`; the same image, as a boot driver no devnode named (the start before the filters were written), entered `DriverEntry` |

The debug console shows the sequence the trace marks: the lower
filter's `AddDevice` attached over the ACPI driver's object; three
`IRP_MN_QUERY_INTERFACE` requests through the lower filter (the PCI
driver's own `AddDevice` asking the ACPI driver below it); the upper
filter's `AddDevice` attached over the PCI driver's object; then
`IRP_MN_QUERY_LEGACY_BUS_INFORMATION`, `IRP_MN_FILTER_RESOURCE_REQUIREMENTS`
and `IRP_MN_START_DEVICE`, each to the upper filter first [read]. Where
section 11.1's boot driver of `System Reserved` came after the PCI
driver at three starts of four on 8 and one of six on 8.1 and 10, this
filter came first at every start read, at the same trace lines each
time [read].

Why it is ordered [inf]: the PnP manager builds a devnode's whole stack
- each lower filter's, the function driver's and each upper filter's
`AddDevice` - before it sends that devnode `IRP_MN_START_DEVICE`, which
reaches the upper filter first; a boot-start filter named by the devnode
is initialised for that devnode when its stack is built; and the PCI
driver reads the bridges below the root bus only when it enumerates the
started bus. So the filter's place depends on the stack, not on the
order of the boot drivers' groups, which is what varied from 8 on.

#### 11.5.2 The design (the owner's answers of 2026-10-29: Q10 (a), Q11 (a), Q12 in `3.4.0.0`)

`XHCIPFW.SYS` keeps its one image and gains a second role, chosen in
`DriverEntry` by the system's version (`PfwNtRole`, pure, in
`pfw_ntdec.c`, with host vectors):

- **2000, XP, Vista and 7 (NT 5.0, 5.1, 6.0, 6.1)**: the entry role,
  section 11.2's step at `DriverEntry`, group `System Reserved`,
  unchanged.
- **8 and 8.1 (NT 6.2 and 6.3, 32-bit)**: the filter role, below.
- **Any other version** (10 among them, where it is not offered): neither
  role; `DriverEntry` writes `XhciPfwSkipped` with the "not a system it
  acts on" bit and returns, as a copy placed by hand on 10 did nothing
  useful in time there (section 11.1).

**The filter role.** `DriverEntry` copies its `RegistryPath` into a
static buffer (the log's path, section 11.2's bound of 300 characters;
longer, and the log is not written), points every major function at a
pass-through routine and sets `AddDevice`, and returns; it writes
nothing and touches no register. The service is an **upper filter of
every PCI root bus devnode** (hardware or compatible id `*PNP0A03` or
`*PNP0A08`).

- **`AddDevice`** creates an unnamed device object, attaches it with
  `IoAttachDeviceToDeviceStack`, copies the lower object's
  `DeviceType`, `Characteristics` and the flags `DO_BUFFERED_IO`,
  `DO_DIRECT_IO`, `DO_POWER_PAGABLE` and `DO_POWER_INRUSH`, and clears
  `DO_DEVICE_INITIALIZING`, as the probe did. **It never fails**: if the
  object cannot be created or attached it returns `STATUS_SUCCESS`
  without attaching (a filter's failed `AddDevice` would fail the root
  bus, which is the hazard below), and writes `XhciPfwSkipped` with the
  "not attached" bit (bit 4) itself, there and then, at `PASSIVE_LEVEL`,
  since a root bus it is not on sends it no start to log from. That root
  bus's bridges are then not closed; no other root bus's step reaches
  them (below).
- **A remove lock** (`IO_REMOVE_LOCK`, `IoInitializeRemoveLockEx` at
  `AddDevice`) guards every dispatch: each request acquires it before it
  is passed down and releases it after the call down returns (a failed
  acquisition completes the request with `STATUS_DELETE_PENDING`, a power
  request after `PoStartNextPowerIrp`), so a power request racing the
  removal never meets a detached object.
- **Every request** is passed down untouched (`IoSkipCurrentIrpStackLocation`
  and `IoCallDriver`); power requests through `PoStartNextPowerIrp`,
  `IoSkipCurrentIrpStackLocation` and `PoCallDriver`. `IRP_MN_REMOVE_DEVICE`
  is passed down first, then `IoReleaseRemoveLockAndWaitEx`, then the
  object detached and deleted; `IRP_MN_SURPRISE_REMOVAL` is passed down
  like any other request (the removal that follows it does the rest).
- **`IRP_MN_DEVICE_USAGE_NOTIFICATION`** (a paging, dump or hibernation
  file on a disk below this bus) follows the rule for a filter's
  `DO_POWER_PAGABLE`: before it is passed down, the flag is set on this
  object when the object above it is pageable or there is none above it;
  it is then passed down synchronously (a completion routine signals an
  event the dispatch waits on) and, on its return, the flag is cleared
  when the lower object is no longer pageable, then the request is
  completed with the lower driver's status.
- **`IRP_MN_START_DEVICE`**: at the first one each instance receives (a
  flag of that instance's), **before** it is passed down, the step runs
  over **that root bus's own bus numbers** only - the bus number range of
  the start's resources (`AllocatedResources`, the
  `CmResourceTypeBusNumber` descriptor ACPI's `_CRS` gives a root bus) -
  as section 11.2 runs it at `DriverEntry` over every bus: the same
  decision (`PfwNtDecide`: Safe Mode, "late", the processors), the same
  hold inside `KeIpiGenericCall`, the same log under the service's
  `Parameters`, with `XhciPfwMode` and the new `XhciPfwRole` (1 entry, 2
  filter, and `XhciPfwBuses`, the range scanned). On a computer with
  more than one root bus each bus's record replaces the one before it,
  so those values are the last bus's; beside them the driver keeps an
  account of every root bus recorded since it was loaded (the steps, the
  "no range" and the "not attached" records alike; `PfwNtSumAdd`, pure,
  in `pfw_ntdec.c`, with host vectors): `XhciPfwRootBuses` their count,
  `XhciPfwSkippedAny` and `XhciPfwResultAny` their skip reasons and
  result bits ORed, `XhciPfwClosedAll` the windows closed on all of them
  (task 45.9, 2026-10-30). It is called at
  `PASSIVE_LEVEL` in the PnP manager's thread, as `DriverEntry` is. A
  root bus's start comes before the PCI driver enumerates that bus, so
  each filtered root bus's bridges are closed before they are read, and
  no step ever writes a bridge of another root bus, which may already
  have been read (a root bus not filtered: one added after the installer
  ran, or one an interrupted install left without its entry, or one not
  attached). The steps of several root buses run one after the other (a
  synchronization event), the step's state being the driver's. A start
  whose resources carry no bus number range runs nothing and logs the
  "no range" bit (bit 5). Every later start of the same root bus (a
  rebalance, a stop and start) passes down with nothing run.
- **The "late" check** (section 11.2: the `SOFTWARE` hive not yet loaded)
  stays, unchanged, the gate against a start long after the boot. That a
  root bus's first start comes before that hive is loaded at every start
  of 8 and 8.1 is read on the code's first legs (the probe did not read
  it); a "late" bit at a cold start would mean the step does not run there
  and is a defect of this design, not a pass.
- **No unload routine**: the root bus's filter is never stopped while
  Windows runs.
- **Imports** added to the image: `IoCreateDevice`, `IoDeleteDevice`,
  `IoAttachDeviceToDeviceStack`, `IoDetachDevice`, `IofCallDriver`,
  `IofCompleteRequest`, `PoStartNextPowerIrp`, `PoCallDriver`,
  `IoInitializeRemoveLockEx`, `IoAcquireRemoveLockEx`,
  `IoReleaseRemoveLockEx`, `IoReleaseRemoveLockAndWaitEx`,
  `KeInitializeEvent`, `KeSetEvent`, `KeWaitForSingleObject`;
  `pfw_scan.c` gains `PfwRunBuses`, the step over a range of buses
  (`PfwRun` is it over 0 to 255, unchanged for the VxD and the entry
  role); each in the 2000, XP, Vista and
  7 x86 kernels the import gate's `hlprx86` profile reads, so the one
  image still loads on every system it is offered on.

**The installer** (`inst_pfw_sys` gains the kind `INST_PFW_SYS_FILTER`
for 6.2 and 6.3 x86; 10 keeps `INST_PFW_SYS_GONE`; the box, its default
and the console's question as on Vista and 7, `INST_PFW_SYS_SEEN`):

- **A name of its own.** The filter role is installed under a service,
  two image names and a record of its own: the service `xhcipff`, the
  images `xhcipff.sys` and `xhcipff2.sys` (the package's `XHCIPFW.SYS`
  copied under them), the record `HKLM\Software\xhci98\XhciPfwFilter`.
  `3.3.0.0`'s installer on 8 and 8.1 takes its kind as "not offered" and
  removes a service `xhcipfw` whose image its record holds, without
  touching any root bus's filters; under the entry role's names a filter
  installation would then be left named by every root bus with its
  image gone, and the next start would stop. Under names it does not
  know, that installer (or its uninstall) leaves the filter role alone.
- **Install**: section 11.2's image placement, ownership rules and
  record, under the filter role's names; the service with `Group` `PnP
  Filter` and `Start` 0; then, **last**, `xhcipff` appended to the
  `UpperFilters` of every present devnode whose `HardwareID` or
  `CompatibleIDs` holds `*PNP0A03` or `*PNP0A08`, through
  `SetupDiSetDeviceRegistryProperty` (`SPDRP_UPPERFILTERS`), the other
  filters kept in their order, a devnode already naming it left as it
  is. No record of the devnodes is kept.
- **The hazard, and the order of every change**: a root bus whose stack
  names a filter that is not loaded does not start, and with it no disk:
  Windows stops with `INACCESSIBLE_BOOT_DEVICE` (10's readings, section
  11.5.1, show it). So while any devnode may name `xhcipff`, its service
  is never set to `Start` 4 and its `ImagePath` never names an image not
  written and compared:
  - **an update** keeps the entries and the service on: the new image is
    written under the other of the two names and compared, the record's
    `Slot` and `Pending` set before its bytes as section 11.2 does, and
    only then the service's values are written, `Start` 0 throughout and
    `ImagePath` moved to the new image last; the earlier image is
    deleted (at the next start, being in use). A run cut short at any
    point leaves the service on the old image or the new, both loadable,
    and every entry in, so the windows stay closed at the next start;
  - **a removal** (the uninstall, the box unticked, the Thunderbolt
    support removed) takes `xhcipff` out of **every** devnode with a root
    bus id that names it, present or not (`DIGCF_ALLCLASSES` without
    `DIGCF_PRESENT`), **first**, once the service's image is known to be
    this installer's; then section 11.2's removal (`Start` 4, the service
    key, the files, the record). An id list or the device list that
    cannot be read, or an entry that cannot be taken out, stops it before
    the service is touched (left at `Start` 0 and loadable), a step said.
  Where the service or its image is not this installer's (section 11.2's
  ownership), nothing of it and no entry is touched; a record that
  cannot be read stops the run with a step said, as section 11.3's
  removal does.
- **One run at a time**: every change to `XHCIPFW.SYS` (both passes,
  the service, the images and the root bus entries) is made under a
  machine-wide lock of its own (`Global\XhciinstPfw`), each pass reading
  the record and the service again under it, so two installer runs never
  interleave an install's entries with another's removal.
- **The installation it replaces**: an entry-role install of this
  installer's on 8 or 8.1 (service `xhcipfw`, its record `XhciPfw`, left
  by an earlier test install of `3.3.0.0`, unordered there) is removed by
  its own names and record in a pass of its own, whatever the boxes,
  before the filter role's run; it never had a root bus entry, and any
  entry naming `xhcipfw` is taken out first all the same.
- **10** (`INST_PFW_SYS_GONE`): as section 11.3, and the removal also
  takes out first every root bus entry naming `xhcipfw`, present or not,
  an entry left stopping the removal.
- **Safe Mode** does not avoid a broken filter: the devnode names it in
  every mode. The installer's readme gives the recovery in its
  troubleshooting part: if 8 or 8.1 stops at start with
  `INACCESSIBLE_BOOT_DEVICE` after this package, start Windows' recovery
  environment, open the registry editor, load the system's `SYSTEM` hive
  (`Windows\System32\config\SYSTEM`), read `Select\Default` in it for
  the control set the next start uses (`ControlSet00n`), search that
  set's `Enum` key for `xhcipff` (Find, values and data), and remove the
  line `xhcipff` from every `UpperFilters` that has it, keeping any other
  line; do not set the `xhcipff` service to disabled while an entry is
  there, which stops the start the same way. Read on 8 x86 on
  2026-11-01 (task 45.9, `runs/run-45.md` "45.4, 45.9 and 45.50
  (closed)"): Automatic Repair offered only Restore and Cancel there,
  so the readme names the setup medium's Repair your computer as the
  way in; the registry editor's Load Hive is offered only with
  `HKEY_LOCAL_MACHINE` selected; Find from the loaded set's `Enum` key
  stops next at the `xhcipff` service's own key; an `UpperFilters` left
  with no line starts; and on a board that leaves windows above 4 GB
  a start without the filter did not finish in 20 minutes, so the
  readme also says to start with Above 4G Decoding off and run the
  installer again, which put the filter back.
- **Fast startup** on 8 and 8.1 resumes the kernel without a root bus
  start, as section 11.2 already says for the boot driver: the step does
  not run then, and the windows are those Windows assigned at the full
  start before it.

#### 11.5.3 Verification

- **Host vectors**: `PfwNtRole` for every version; `PfwRunBuses` over a
  range leaving a bridge outside it untouched; `PfwNtDecide` with the
  filter role; `inst_pfw_sys` for 6.2 and 6.3 (the new kind), `inst_pfw_box`
  and `inst_pfw_mode` for it; the multi-string append (empty, one other
  filter, already present, too long) and the root bus id test.
- **Legs** on the 4G-off layout with the ORICO at power-on, 8 and 8.1
  x86, from the package: the install (the box, the service's group, the
  entry on `ACPI\PNP0A08\0`, the record), three cold starts each (the
  trace order: the step's close before the PCI driver's probe; no "late"
  bit; the bridges at problem 0), the update over itself (the entries kept,
  the image's slot moved), the uninstall (entries first, then
  the service and the files; a cold start after it), and the recovery
  path (the entry left with the image removed by hand: the stop; the
  entry removed from the recovery environment's registry editor: the
  start); Safe Mode (loaded, nothing written, the "safe mode" bit). On 10
  x86: the box not shown, `/xhcipfw` refused, nothing placed, no entry on
  any root bus. 2000 to 7: the entry role unchanged (host vectors; the
  bench).

#### 11.5.4 The owner's questions (answered 2026-10-29)

- **Q10 (a)**: the filter role on 8 and 8.1 only; 2000 to 7 keep section
  11.2's place, which read in order at every start there; 10 not served.
- **Q11 (a)**: the image boot-critical on 8 and 8.1, accepted, with the
  order of changes and the readme's recovery above.
- **Q12**: coded in `3.4.0.0` once this section converged.

#### 11.5.5 10 read again (task 47.14, 2026-11-06)

Task 47.14 (PFW8.1's reading, the owner, 2026-11-06) read 10 x86 again
with section 11.5.1's probe image (`pfwflt.sys`, the same SHA-256
`37ffe267...0a8a`), installed differently in two ways: the service's
`ImagePath` written by `reg add` as `REG_EXPAND_SZ`
`System32\drivers\pfwflt.sys` (45.9's `.reg` file had left `ImagePath`
unwritten, so the default path served), and `pfwflt` named in the `UpperFilters` of
`ACPI\PNP0A08\0` alone, no `LowerFilters` - the one position section
11.5.2's filter role takes. Service `Type` 1, `Start` 0, `ErrorControl`
0, `Group` `PnP Filter`; fast startup off; the 4G-off layout with the
ORICO at power-on, two processors, each start cold (`runs/run-47.md`,
"47.14 A filter in the PCI root bus's stack on the 32-bit 10: read
again") [emu]:

| Start | Trace | `DriverEntry` | `AddDevice` | start | step's close of the root port | PCI driver's probe of the root port | Result |
|---|---|---|---|---|---|---|---|
| a, b | RW | 14 603 | 14 606 | 14 609 | 31 189 | 32 328 | the desktop; every bridge 0; the NHI and `fw_cfg` 28 |
| c | W | - | - | - | 446 | 593 | the same |

The debug console showed 8's and 8.1's upper-filter sequence at every
start (`IRP_MN_QUERY_LEGACY_BUS_INFORMATION`,
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS`, `IRP_MN_START_DEVICE` to the
filter first; the step closing nine bridges). So on 10 a boot-start
upper filter of the root bus is initialised and acts before the PCI
driver reads the bridges at every start read, as on 8 and 8.1 [emu], and
section 11.5.1's 10 row describes that install, not the place [inf].
Named as both filters, as 45.9 had it, with the corrected image path, a
fourth start did the same (the lower filter's `AddDevice` over the ACPI
driver's object at line 14 606, the upper filter's at 14 609, the step's
close at 31 192 before the probe at 32 334) [emu], so the lower filter
was not what stopped 45.9's starts; the difference left is the
service's `ImagePath`, absent in 45.9's install and written here [inf].
The filter role's use on 10 (section 11.5.2 gives 10 none) stays planned
work, its design and code (PFW8.1).

#### 11.5.6 10 in the filter role (task 48.27, put to the owner)

Task 48.27 (was PFW8.1; the owner, 2026-11-09): the windows above 4 GB
closed on the 32-bit 10 by section 11.5.2's filter role, which section
11.5.5 read acting in time there. This section is the design, put to the
owner before any code (Q26 to Q29 below) and answered on 2026-11-09
(section 11.5.7).

**What is read, and what is not.** Read [emu]: a boot-start upper filter
of `ACPI\PNP0A08\0`, its service's `ImagePath` written, initialised and
acting before the PCI driver's probe at every start read (four, section
11.5.5), on the q35 10 x86 base (19041), fast startup off, one root bus,
with the probe image, not `XHCIPFW.SYS`. Not read on 10, each a leg below
or a question: `XHCIPFW.SYS`'s own filter role there (its version test,
its "late" check against the `SOFTWARE` hive, its log); a start with
fast startup on, Windows 10's default; an in-place upgrade to a later
release of 10; any build but the q35 base's (section 11.1's 19045
reading was the entry-time probe's, not a filter's), and none before
2004 (no such medium is at hand: the 32-bit 10 medium here is 22H2).

**The change.** No new mechanism: 10 takes 8's and 8.1's.

- **The driver**: `PfwNtRole` gives 10.0 the filter role (`PsGetVersion`
  answers 10.0 there [inf]; the image logs "not a system it acts on" on
  10 today, so the leg reads `XhciPfwRole` 2 before anything else). Every
  later version keeps "neither role". Nothing else in the image changes:
  the root bus's first start, its bus range, the hold inside
  `KeIpiGenericCall` (it resolves on 10 [inf], as on 8 and 8.1), the
  "late" check, the log and its account of root buses.
- **The installer**: `inst_pfw_sys` gives the 32-bit 10.0 the kind
  `INST_PFW_SYS_FILTER`, as 8 and 8.1 (from build 19041 under Q29 (b),
  which needs the build passed to it). `INST_PFW_SYS_GONE` then serves
  only 10 before 19041 under Q29 (b), and no system under (a), where it is
  removed with its paths (the console's line "XHCIPFW.SYS is not offered
  on Windows 10.", the refusal of `/xhcipfw` there, the pass that removed
  this installer's entry-role image whatever the boxes); under (b) its
  texts name the build ("before version 2004"). What `GONE` did is kept
  on a filter-kind 10 by the filter kind's own pass,
  "the installation it replaces" (section 11.5.2): an entry-role install
  of this installer's (service `xhcipfw`, record `XhciPfw`, only ever a
  test install of `3.3.0.0` there) is removed by its own names and record
  before the filter role's run, any root bus entry naming `xhcipfw` taken
  out first. A 10 machine updated from `3.4.x` holds no filter of this
  installer's (none was offered there), so the update places it fresh,
  under the names and record of section 11.5.2. The box, its default and
  the console's question as on 8 and 8.1 (Q26).
- **The previews read both installations, as the run does.** The run
  removes the entry role's install before the filter role's
  (`pfwsys_run_locked`), but `/list`, `/uninstall /list` and the window's
  Summary read only the role the system is offered (`pfwsys_list` and the
  plan's line). On the filter kind, 8 and 8.1 today and 10 with this
  change, a machine holding only `3.3.0.0`'s `XhciPfw` record would show
  no removal while the run removes it. So every preview on the filter
  kind reads the entry role's record and service first, read-only, by the
  same ownership rules, and says its outcome before the filter role's
  line; with the box unticked or the Thunderbolt support left out too, as
  the run removes it whatever the boxes. The outcomes are the run's two
  kinds and the preview keeps them apart: removed, or another
  installation's and kept, after which the run goes on to the filter
  role, whose line follows; or blocked - its record, or an image the
  record holds, cannot be read - after which the run stops the pass and
  leaves the filter role unchanged (`pfwsys_run_locked` skips it), so the
  preview says that the filter role is left unchanged in place of its
  line.
- **The texts** (drafts for the owner, the readme's 10 paragraph and its
  recovery paragraph): 10 joins 8 and 8.1 wherever they are named for
  the filter (the readme, `README.md` and the release notes; the
  `/xhcipfw` line of the readme's switches, "the 32-bit Vista to 10"),
  from version 2004 under Q29 (b), the earlier builds keeping today's
  "not offered" lines with the version named.

**What 10 adds to the hazards of section 11.5.2.** The one hazard of the
role stands as on 8 and 8.1: a root bus whose stack names a filter that
does not load does not start, and Windows stops with
`INACCESSIBLE_BOOT_DEVICE`; the installer's order of changes and the
readme's recovery route answer it. Three ways in are wider on 10 or not
yet stated:

- **Fast startup**, on by default on 8 to 10. A fast-startup start
  resumes the kernel and sends the root bus no start, so the step does
  not run (section 11.2); the windows are those Windows assigned at the
  full start before it, restored by Windows as at any resume [inf]. The
  installer's restart is a full start (a restart does not use fast
  startup [inf]), so the install's own restart closes the windows. A
  shut down and power on instead, after the install, resumes a kernel
  that read the windows above 4 GB, and the devices behind them stay at
  problem 12 until a restart [inf]. The readme says "restart" for it. The
  same holds on 8 and 8.1, where every reading was taken with fast
  startup off. Legs read both on 10 (below). No change to fast startup's
  setting is proposed (a power policy is not this installer's).
- **Windows Setup run over the installed system**: a feature update by
  full Setup (not by an enablement package, which installs no new
  system), an upgrade from a setup medium, or an in-place repair
  installation, on any release of 10. Whether it carries the root bus's
  `UpperFilters` entry and the `xhcipff` service, a service with no INF,
  across it is not read.
  Three outcomes: both carried (the step runs on); both dropped (the
  windows are no longer closed, the devices behind them at problem 12 as
  without the package, and the installer run again puts it back); the
  entry carried and the service dropped, or its image not copied (the
  stop above, and the readme's recovery route). The same holds for 8's
  upgrade to 8.1. A root bus driver reinstalled by Windows (a newer
  `machine.inf` through Windows Update) may drop the device's filter
  entries [inf]: the safe direction, the step no longer running. Q27.
- **Secure Boot turned on after the install.** With Secure Boot on, the
  32-bit 8 to 10 do not load a driver that is not signed (record 26
  section 5.3.3), so the root bus's filter would not load and the start
  would stop as above. The install itself is refused then, the box being
  the Thunderbolt box's child and that box refused with Secure Boot on;
  only a later change in the firmware's setup reaches it. This holds for
  8 and 8.1 as shipped. The readme's recovery paragraph gains it as a
  cause: turn Secure Boot off again, or remove the entries as it says.

"Above 4G Decoding" on is unchanged: 10 x86 shows Windows' recovery
screen with the step and without it (section 11.1), and the readme's
advice to set it off stands.

**Host vectors**: `PfwNtRole` for 10.0 (the filter role) and 10.1 and
11.0 (neither); `inst_pfw_sys` for 10.0 x64 (none) and for 10.0 x86 by
Q29: under (a) every build `INST_PFW_SYS_FILTER` and no remaining use of
`INST_PFW_SYS_GONE`; under (b) build 19040 `INST_PFW_SYS_GONE`, 19041
and 19045 `INST_PFW_SYS_FILTER`, and `GONE`'s own vectors kept (removed
whatever the boxes and whether the package carries the file, removed at
the uninstall, no box, both switches' lines); `inst_pfw_box` and
`inst_pfw_mode` for the filter kind on 10, as for 8.1.

**Legs** (q35 10 x86, the 4G-off layout with the ORICO at power-on, two
processors, from the package, each as section 11.5.3 reads 8 and 8.1):

- the install (the box ticked with the Thunderbolt box, the service's
  group, the entry on `ACPI\PNP0A08\0`, the record), then three cold
  starts with fast startup off: the trace order (the step's close before
  the PCI driver's probe), `XhciPfwRole` 2, `XhciPfwMode` 2,
  `XhciPfwSkippedAny` 0, `XhciPfwRootBuses` 1, no "late" bit, the
  bridges at problem 0;
- fast startup on: the install's restart (the step runs), then a shut
  down and power on (no step, the windows and devices as the full start
  left them, the NVMe on `stornvme`); and a fresh overlay installed and
  then shut down instead of restarted (problem 12 until a restart, then
  problem 0);
- the update over itself (entries kept, the image's slot moved), the
  uninstall (entries first, then the service and the files; a cold start
  after it), the recovery route (the image removed by hand: the stop;
  the entry removed from the recovery environment's registry editor:
  the start), Safe Mode (loaded, nothing written, the "safe mode" bit);
- an entry-role `xhcipfw` placed by `3.3.0.0`'s installer on a fresh
  overlay, then this package: removed before the filter is placed;
- the previews on the filter kind (8.1 and 10): `/list`, `/uninstall
  /list`, the box unticked, the Thunderbolt support left out, each with
  `3.3.0.0`'s entry-role install present (removed, then the filter's
  line), another installation's `xhcipfw` (kept, then the filter's
  line), an unreadable record and an unreadable image the record holds
  (blocked, the filter role said to be left unchanged), each preview
  then compared with what the run itself does;
- each of the above on the q35 10 x86 base with its build read from the
  guest and written in the run record;
- Q27's leg if the owner chooses it.

**Q26, 10's box.** (a) As on 8 and 8.1: offered, ticked whenever the
Thunderbolt 3 / USB4 box is (Q9); (b) offered, unticked by default on 10
alone; (c) not offered, as today. Recommended (a): 10 then reads as 8.1
does, and on a board that leaves the USB4 ports' windows above 4 GB the
devices behind them work instead of sitting at problem 12.

**Q27, Windows Setup run over the installed system.** (a) Accepted as
an unmeasured risk, the readme's recovery route naming it as a cause (as
8's upgrade to 8.1 is today); (b) read first on a guest: the 22H2 setup
medium run as an in-place repair installation over a 10 x86 guest with
the filter installed, then the filter entries, the service, the image it
names and this installer's record read after it, and a cold start on
the 4G-off layout; (c) the readme also asks the user to untick
XHCIPFW.SYS (or uninstall) before running Windows Setup and to run
Setup again after it. Recommended (b), then (a) or (c) by what it shows:
an enablement package (19041 to 19045) installs no new system, but full
Setup and a repair installation do, on every release, and the medium for
(b) is at hand.

**Q28, fast startup.** (a) The installer's restart, and the readme's
"restart" after an install or update, as proposed; fast startup left as
the user has it; (b) the installer also turns fast startup off on 8 to
10 when it places the filter. Recommended (a): the one start that
matters after the install is the installer's own restart, a full start,
and a fast-startup start keeps what that start assigned [inf], which
the legs read.

**Q29, which builds of 10.** (a) Every 32-bit 10 build (10240 to 19045);
(b) the builds read, 19041 and later, the installer reading the build as
`inst_os_kind` already does, with earlier builds kept as today (not
offered, an earlier test install of this installer's removed).
Recommended (b): the filter is boot-critical, an earlier build's
handling of a boot-start root bus filter is not read and no medium for
one is at hand, and a build not offered loses only the step, as today.
With (b) `INST_PFW_SYS_GONE` stays, for 10 before 19041 alone, and the
previews' paragraph above covers it unchanged.

#### 11.5.7 The owner's decisions (2026-11-09)

- **Q26 (a)**: offered on 10 as on 8 and 8.1, ticked whenever the
  Thunderbolt 3 / USB4 box is.
- **Q27 (b)**: read first. Among the legs, the first is the 22H2 x86
  medium run as an in-place repair installation over a 10 x86 guest with
  the filter installed, then the filter entries, the service, the image
  it names and this installer's record read, and a cold start on the
  4G-off layout; what it shows decides between accepting the risk (a)
  and the readme's warning (c).
- **Q28 (a)**: fast startup left as the user has it; the readme says
  restart after an install or update.
- **Q29 (b)**: 19041 (version 2004) and later only; earlier builds of
  10 keep `INST_PFW_SYS_GONE` (not offered, an earlier test install of
  this installer's removed).

## 12. The USB4 host's own functions on an IRQ 98 SE and ME cannot give (task 46.11)

Draft 3, 2026-11-03: the owner's answer to Q1 (option A, always, with
the guards of the review, and a value that turns the step off), the
step as it is to be coded in section 12.10. Draft 2, 2026-11-02, reviewed and revised (the selection narrowed to the
NHI and the downstream port that leads to it alone; tunnel ports never
touched; the readings owed before code listed); draft 1, 2026-11-02. For
`roadmap-usb4.md` task 46.11 (was BRIRQ9X.1; the owner, 2026-11-02: the
top priority after 46.1). Nothing of it is coded. Section 12.7 adds the
bench after the IRQ (the platform's USB4 handover, task 46.12).

### 12.1 What was read

On the owner's board with an ASMedia ASM4242 (45-E.5's first reading;
the board's data stays with that reading and `usb4-emu`, this section
states only what this package must change), 98 SE without ACPI [obs]:

1. The firmware's PCI IRQ routing table names the host's switch
   upstream port and no downstream port, so the downstream port in front
   of the host interface (NHI) is routed through the upstream port's pins
   to a link the firmware put on the IRQ of an ISA serial port the
   platform's PnP BIOS reports and that cannot be released (Device
   Manager: "This resource setting cannot be modified"; disabling it in
   the hardware profile leaves the IRQ held). 98 SE reports "IRQ Steering
   disabled, IRQ table has some errors", so every PCI function keeps the
   Interrupt Line the firmware wrote. The downstream port therefore sat
   at problem 12, nothing behind it was enumerated, and `xhcicm.sys`
   never loaded: no connection manager on 98 SE, no hot plug. The NHI's
   own Interrupt Line names the same IRQ.
2. A DOS program run from `AUTOEXEC.BAT` before Windows (test only,
   `out\s98-irqtest\`, not in a package) that rewrote the Interrupt Line
   of that downstream port and of the NHI to another PCI IRQ the
   firmware had routed for the host's own xHCI: the port started, the NHI
   was enumerated, `xhcicm.inf` installed by hand, the NHI started on
   that shared IRQ. Neither function's interrupt is used on 9x: a
   bridge only forwards, and `xhcicm.sys` polls (record 21 decision D3;
   ring interrupts off by default, `XhciCmRingInterrupt` 0); the
   register is a note to software and the function's interrupt wire is
   unchanged [spec].
3. Bus Master and memory decode were on, before Windows and after, on the
   upstream port, the downstream port and the NHI.

What 98 SE's `PCI.VXD` does with an Interrupt Line of `FFh` ("unknown
or not connected" [spec]) on such a function, with steering disabled,
is not read: whether it then asks no IRQ and starts the function on its
windows alone (option A below), or refuses it (problem 12 or 29, as
record 28 section 1.1 read for an NVMe function on the P14s).

### 12.2 The step

In `XHCIPFW.VXD`'s `Sys_Critical_Init` pass (section 5.1), after the
window step and before the configuration manager or `PCI.VXD` reads any
function, for each Thunderbolt or USB4 host interface (NHI) its flat scan
(section 4.1 step 1) finds - the ids `xhcicm.inf`'s models name - and
for **the one downstream port that leads to it alone**: a type 1 function
whose secondary bus is the NHI's bus, and that bus holds no function but
the NHI (every device 0 to 31 read absent but the NHI's), write its Interrupt Line (`3Ch`, one byte) as option A or B says, read it
back, and log it (section 6: the function, the line before and after,
the outcome). Nothing else is written: not its Interrupt Pin (read only
[spec]), not the command register, not any other function. A function
whose Interrupt Pin is 0 is left alone.

**Which functions, and why only these.** Their drivers on 98 SE and ME
use no interrupt: `PCI.VXD`'s bridge has no interrupt handler [inf], and
`xhcicm.sys` polls on 9x. **A tunnel port is never touched**, although it
carries the same device id (`cm_ports.c` names all four ASM4242
downstream ports `1B21:2423`): its Interrupt Line has an indirect
consumer, the 9x bridge numbering's interrupt stage, which reads the
tunnel port's line and copies it to the functions behind it whose pin
provably reaches that wire (record 21 section 6.1.1 step 5;
`src/cm/tbt_brnum.c`, `src/cm/cm_cfgwk.c`), and those drivers do
connect their interrupts. A rewritten line there would hand a hot-plugged
drive an invented IRQ (option B) or none where it had a route (option A).
The selection by bus (the port whose subtree is the NHI alone) rather
than by id is what keeps them out; a port whose secondary bus holds
anything else is left. A function whose driver connects its
interrupt (the host's xHCI under `xhci98.sys`, Stor98, any device behind
a tunnel) is never touched: a wrong line there would give its driver an
interrupt that never comes, the reason record 28 section 1.3 never
invents a line. 46.11 reverses that rule only for functions whose
interrupt nothing on 9x connects.

**Option A, `FFh` (recommended if the reading allows it).** No IRQ is
named, so none is invented and none is shared. Valid only if `PCI.VXD`
then starts both functions without an IRQ (section 12.1's open
reading); the owner's board can read it at once (`SETLN3.COM`, the same
program writing `FFh`).

**Option B, a PCI IRQ the firmware routed for a sibling.** The line the
firmware wrote for another function below the same upstream port
whose own line is not the one being replaced, preferring the host's
xHCI. What was read on the board, but a guess on another: the sibling's
IRQ can itself be one an ISA device holds, and a line shared with a
working xHCI adds one more holder to that IRQ for no use.

**When the step acts.** Only where the line would fail: that is not
knowable at `Sys_Critical_Init` (the configuration manager has not read
the PnP BIOS yet), so either always, for the functions above (option A
makes "always" harmless where `PCI.VXD` gives no IRQ to `FFh`), or only
where the line names IRQ 3 or 4, the fixed ISA serial ports' (narrower,
and misses a parallel port's 7 or a board's other fixed devices). Owner
question Q1 below.

**Where it runs.** `XHCIPFW.VXD` is installed with the Thunderbolt and
USB4 box (section 8), which 46.5 is to tick when a USB4 host is found
whose router Windows has not listed: the two tasks go together on 98 SE
and ME, since the step is what lets Windows list it. The NT systems are
untouched (their `pci.sys` routes through ACPI).

### 12.3 What it does not solve

On the same board, with the NHI started, the USB4 connection manager's
first control read of the host router is not answered (`runs/run-46.md`
when written; the diagnostic dump of commit `05ff588e`): a separate
reading, its experiments on branch `p46-asmtest`. 46.11's step makes the
connection manager load; whether it then runs on that host is that
reading's.

### 12.4 Readings owed before code

- What 98 SE's and ME's `PCI.VXD` does with `FFh` in a bridge's and in
  an NHI's Interrupt Line, steering disabled and steering enabled: the
  lines and the allocated resources after enumeration (the board's
  `SETLN3.COM` for 98 SE with steering disabled; the emulator for the
  rest).
- That the byte write comes before `PCI.VXD`'s first read of the line
  (the QEMU configuration-access trace, as section 5.1 read for the
  windows) and leaves the neighbouring bytes as they were (Interrupt
  Pin, and Min_Gnt and Max_Lat on a type 0 header or Bridge Control on a
  type 1).
- A healthy routed system (steering enabled, the line already usable):
  the step must not make it worse; whether it should then act at all is
  Q1.

### 12.5 Verification (when coded)

Host vectors for the selection (the NHI by its ids, the port by its
secondary bus holding the NHI alone, a port whose bus holds anything else
left, every tunnel port left, pin 0 left, a write that does not read back
logged and not retried). Legs on 98 SE and ME in the emulator with the ASM4242 layout
and a downstream port whose line names an IRQ a serial port holds (the
emulator's COM1) with steering off: the port and the NHI start, `xhcicm`
loads; the same with the step off reproduces problem 12; the host's
xHCI's line and every tunnel port's line are unchanged, and a drive
hot-plugged behind a tunnel port still gets its line from the bridge
numbering and runs on its interrupt; the install, an update over an
installed XHCIPFW, and the removal (section 8). Then the owner's board.

### 12.6 Questions for the owner

- **Q1.** Option A or B, by the board's `SETLN3.COM` reading; and always
  for the listed functions, or only where the line names IRQ 3 or 4.
  Recommendation: A, always, if the reading shows both functions start.
- **Q2.** Whether 46.5's installer ticks the Thunderbolt and USB4 box
  (and so XHCIPFW) when it finds such a host's switch with no router
  listed, as 46.5 proposes. Recommendation: yes.
- The owner's answer to Q1 (2026-11-03): option A, `FFh`, at every
  start, for the NHI and the downstream port in front of it alone,
  selected by topology, tunnel ports never touched, a function whose
  decode is off left, no IRQ 3 or 4 rule, not option B; and a registry
  value that turns the step off, written at 1. Section 12.10.

### 12.7 The bench after the IRQ (2026-11-02, afternoon)

With the board's two Interrupt Lines rewritten before Windows to an IRQ
it can give
(`SETLN2.COM`, test only), the router's port starts, the NHI is listed
and xhcicm loads; its USB4 start then fails (inert reason 12): ring 0's
first control read is consumed by the host interface and never answered,
and the retry is never consumed. Test images T1 (Host Interface Reset
first), T2 (T1 and the interrupt settings Linux uses), T3 (a one-dword
read first) and T4 (10-entry rings) change nothing.

Linux 7.0 on the same board (a live disc, text mode, ACPI on) finds
router 0 with the same ring 0 values, after its log shows `USB4 _OSC: OS
controls USB3+ DisplayPort+ PCIe+ XDomain+`. The firmware's tables show
what that handover does (decompiled from the board's own tables, which
stay out of this tree): the platform-wide `_OSC`, on the operating
system's request for native USB4 control, calls a method that writes a
command to a mailbox in the USB4 host's switch upstream port, in
extended configuration space: the command dword at 0x420 (busy while
bits 11:8 read 1), the reply at 0x430 (ready when bits 11:8 are not 0,
then written 0), the port put in D0 around it and its old state put
back. The command is 0x0102 with a byte of the firmware's own data area
in bits 23:16; the USB4 `_OSC` sends 0x0180, a capability query. 98 SE
never runs `_OSC`, so most likely the firmware's own connection manager
keeps the host router (inferred from the firmware source and from T5
below, not read from the chip).

Test image T5 (branch `p46-asmtest`, the ECAM page of the upstream port
and the address of the firmware's pointer from an untracked header)
sends the handover and then the query before ring 0: replies 0x100 and
0x000F0100, the router answers, the connection manager reaches active,
and a tunnelled NVMe drive hot-plugged at the desktop is enumerated,
tunnelled and started by Stor98 as a removable unit with a drive letter
(four further unplug and replug cycles clean). The product form is task
46.12; it supersedes nothing in 12.2 (the IRQ is still owed: the
hot-added drive's own function behind the tunnel holds an Interrupt
Line and is refused by the no-IRQ start; its routing is outside 12.2's
host-only step, task 46.15).

Found with it, each its own task: a connect and drop loop after a failed
first connection (46.13); the registry damaged during the bench and a
VMM page fault on a plug-in (46.14); the drive's speed (46.15); a USB
flash drive with no letter once the tunnelled drive is attached (46.16).

### 12.8 The handover made a feature (task 46.12)

Draft 11, 2026-11-03: the NT systems as decided (12.8.15): the owner's
answers to Q15 (B, then C, then D for 2000 and 32-bit XP) and Q16 (a),
the firmware table interface read on 10 x64, the mailbox excluded from
the firmware's own use; a route reading no firmware memory (E); the owner's answers to Q17 to
Q20 (2026-11-04) and the installer's part (12.8.15.10 to 12.8.15.12,
with the installer as built, 2026-11-04).
Draft 10, 2026-11-03, reviewed and revised: the board's namespace
audited (12.8.13.10). Draft 9, 2026-11-03, reviewed and revised: the NT systems under the
owner's decisions of that day (12.8.13), ending in two questions for
the owner (Q15, Q16). Draft 6, 2026-11-03: the NT systems (12.8.13), the owner's answer to
the reading that the firmware sends the handover to Windows 11 and later
alone; 12.8.2's NT statement corrected. Draft 5, 2026-11-03: the owner's
answers to Q13 and Q14 (12.8.9): the
firmware's code recognised in two tiers, its exact code (a build read:
the command sent) or its canonical form (another build with the same
handover code: sent only where the user opted in, the new value
`XhciCmAsm4242Structural`), 12.8.10; what the driver reports and what
the installer asks, 12.8.11; the log a report from another board
carries, 12.8.12. Draft 4, 2026-11-02, reviewed and revised (the three named SSDTs alone
walked, each required once and whole; the pointer fields required at the
root, the scope the methods name them in; an ambiguity never forgotten;
a region argument that is not a literal ends the walk; every physical
read a single byte or the firmware's dword field, in the firmware's own
order, each address formed without wrapping; the PCI inventory told
absent functions from missing buses; every budget fatal); draft 3,
2026-11-02, reviewed and revised (the firmware recognised by
the exact code of the five methods the handover rests on, against a
list of builds read, never by names or constants alone; no processor
register read: every physical read is one the recognised firmware's own
code makes, from addresses its own checksummed tables give; the
handover's success its observed reply exactly); draft 2, 2026-11-02,
reviewed and revised (the AML read as a structure, never searched; the
upstream port required in D0 and its power never written; the exchange's
stale and all-ones replies; the topology taken whole before anything is
written); draft 1, 2026-11-02. For `roadmap-usb4.md` task 46.12: the
replay of section 12.7 with no address built in. Tags as the record's,
and **[acpi]** the ACPI Specification (the root pointer, the description
tables and their checksums, AML's encoding, chapter 20) and the PCI
Firmware Specification (`MCFG`), neither held in this repository;
**[fw]** the board's own tables, read for section 12.7 and for this
section (copied from the board, kept outside the tree; only what this
section states of them is used).

#### 12.8.1 What the firmware does, in the terms the driver can find

[fw] One method `\_SB._OSC` (an SSDT whose OEM ID is `AMD` and OEM
table id `CPMMSOSC`) answers both the USB4 `_OSC` and the platform-wide
one. The platform-wide branch, asked without the query flag, sends the
handover through the method `\_SB.M625` of the firmware's discrete USB4
table (OEM table id `DISCUSB4`); the USB4 branch sends the capability
query through the same method, once. The method finds the mailbox the
way the driver can:

1. The firmware's data area is a `SystemMemory` operation region `\CPNV`
   (OEM table id `CPMCMN`), address and length constants. Two of its
   fields are pointers: `M620` (32 bits) to the discrete USB4 table,
   `M251` (32 bits) to a table of the platform's PCIe roots. The bytes
   are read by `\M049` (a byte at a pointer plus an offset), `\M011`
   (through `\M049`) and `\M624` (a root's bus for an index).
2. In the discrete USB4 table: byte `10h` non-zero (a host fitted; the
   firmware sends nothing otherwise), byte `12h` an index into the roots
   table, bytes `13h` and `14h` the device and function of the root port
   above the host's switch, byte `4Bh` the command's data byte, byte
   `4Dh` non-zero when the platform grants the operating system native
   USB4 control (zero: the platform-wide branch takes the grant away).
3. In the roots table, the entry at `10h + 8 x index`: byte 0 `FFh` for
   no entry, the root's bus in bytes 3 and 4.
4. The mailbox is in the switch upstream port's extended configuration
   space, through the platform's ECAM: the upstream port is device 0,
   function 0 on the root port's secondary bus; command dword at `420h`,
   reply at `430h`; the firmware puts the port in D0 around the exchange
   and its old state back after.
5. The handover is `0102h | (byte 4Bh << 16)`, the query `0180h`. The
   method waits while the command dword's bits 11:8 read 1 and then until
   the reply's bits 11:8 are not 0, 1000 times about 1 ms each, and
   writes the reply 0. The handover's reply is not read by the firmware;
   the query's is taken as success when its low byte is 0, the
   capabilities in bits 19:16. The board answered `100h` and `000F0100h`
   (section 12.7). The firmware also requires an operating system that
   names itself Windows 11 or later through `_OSI` (12.8.4).

Byte `4Dh` was not read as such (T5 logged it and required only byte
`10h`); that it is non-zero on the owner's board is inferred from Linux
being granted native USB4 control there, which the firmware's code gives
only then [inf]. The board reads it (12.8.9).

#### 12.8.2 Where it runs

In `xhcicm.sys`'s start on 98 SE and ME (draft 4 and 5; draft 6 adds the
NT systems, 12.8.13: the firmware's own `_OSC` sends the handover only to
Windows 11 and later, so an ACPI system before it gets none either), for
an NHI whose
ids are the ASM4242's (`1B21:2425`), under the USB4 profile, after BAR0
is mapped and before the platform's own steps and ring 0 (where T5 sent
it).

**Once per boot per host.** A static table of upstream ports (four
entries, keyed by the upstream port's bus, device and function as found)
reserved under a spin lock before the first mailbox write (the two reads
of 12.8.4's clean mailbox come before it and use no entry) and never
released: a restart of the NHI (a disable and enable) sends nothing
again, whatever the first attempt's outcome, a timeout included; a fifth
host is refused. The driver is resident for the boot on 98 SE and ME
(`cm_entry.c`), so the table lasts the boot. ACPI allows `_OSC` to be
evaluated more than once; sending once is this driver's choice, the
least it can do. A resume is not handled: what the firmware does across
a sleep with no ACPI operating system is unread (the owner's board).

#### 12.8.3 Finding everything, with no address built in

On the thread at PASSIVE_LEVEL. Physical memory and ECAM are read
through `MmMapIoSpace` (mapped for the read, unmapped after; each byte
loaded alone, no neighbour touched; a 32-bit field as its four bytes in
turn), configuration
space through `HalGetBusData`. Discovery has a budget of its own: at
most 64 tables, 1 MB of table bytes, 20000 AML terms a table, a path of
16 names and a nesting of 16; past any, nothing is sent.

1. **The host by its bus.** One inventory of every bus, device and
   function through `HalGetBusData` (a read of no bytes is a bus the
   HAL does not know, of two bytes an absent function, of fewer than
   asked a failure that leaves the inventory incomplete and decides
   nothing), then: the type 1 function whose secondary bus is the NHI's
   bus (the downstream port, `1B21:2423`), the one whose secondary bus is
   that port's bus (the upstream port, `1B21:2421`, which must be device
   0, function 0 of its bus), and the one whose secondary bus is the
   upstream port's bus (the root port: class `0604`, a PCI Express
   capability whose port type is a root port). Each must be the one
   function claiming that secondary bus, its subordinate bus not below
   its secondary; anything else, nothing sent.
2. **The ACPI tables in memory**, read as any ACPI operating system reads
   them. The root pointer: `RSD PTR ` on a 16-byte boundary in the first
   KB of the extended BIOS data area (its segment at physical `40Eh`)
   and then `E0000h` to `FFFFFh`, its first 20 bytes summing to zero,
   and for revision 2 or later its length (36 to 255) summing to zero
   [acpi]. The XSDT when the pointer has one below 4 GB, else (or when
   it fails) the RSDT, each of its own signature; the root table's
   length a whole number of entries, at most 64; an
   XSDT entry above 4 GB skipped; each table's header read first, its
   length at least 36 and capped (64 KB for an SSDT, 4 KB for `MCFG`),
   the whole table read into allocated memory and summing to zero before
   anything of it is used [acpi]. Every table, and every read below,
   must lie above 1 MB, below 4 GB, outside ECAM's window and below
   `FEC00000h`; a table that fails is skipped, a root pointer or root
   table that fails stops it.
3. **ECAM.** `MCFG`'s allocations for segment 0 (another segment's
   ignored; 98 SE and ME number segment 0 alone); exactly one whose bus
   range holds the three ports' buses; its base 1 MB aligned, below 4 GB,
   and the base of bus 0 whatever its first bus [acpi]. A function's
   page is `base + (bus << 20) + (device << 15) + (function << 12)`,
   checked to end below 4 GB. Read back: the upstream port's dword 0
   through ECAM equal to what `HalGetBusData` read, and its secondary bus
   the downstream port's bus; else nothing is sent.
4. **The firmware's data and code, read as AML.** The three SSDTs with
   OEM ID `AMD` and OEM table ids `CPMCMN`, `DISCUSB4` and `CPMMSOSC`
   must each be there once and are each walked as a term list from its
   root [acpi]; any other table is not read (a name defined twice in a
   firmware's namespace fails its load, so no other table can hold a
   second `\CPNV`, `\M620` or method of these names in a firmware that
   works [acpi]). `Scope` and `Device` are descended with their name
   paths (root and parent prefixes, dual and multiple name prefixes
   resolved to a canonical path); `Method` bodies, `If`, `Else`,
   `While`, `Processor`, `PowerResource`, `ThermalZone`, `IndexField`
   and `BankField` packages, and buffer and package data are skipped
   whole by their package lengths; `Name` with its data (an integer
   constant, a string - `0Dh`, its characters and a null inside the
   enclosing bounds - a buffer or a package), `External`, `Mutex`,
   `Event`, `Alias`, `OperationRegion` and `Field` are decoded; anything
   else ends the walk, and a table not walked whole stops discovery
   (never a search for the next opcode). An `OperationRegion` whose
   address or length is not a literal integer (`Zero`, `One`, `0Ah`,
   `0Bh`, `0Ch`, `0Eh` with a zero high dword) ends the walk: a name
   there may be a method call with arguments, which cannot be passed
   over safely. `\CPNV` must be in region space 0; a second declaration
   with other values makes it unusable for good. The field: a `Field`
   declared at the root (the names a `Field` declares are in its own
   scope, and the methods read `\M620` and `\M251`), whose region name
   resolves to `\CPNV`, its package length covering its own encoding and
   the rest, its list of named fields (a four-character name and a bit
   width encoded as a package length), reserved fields (`00h` and a
   width), access fields (`01h` and two operand bytes) and extended
   access fields (`03h` and three), positions summed in bits with no
   rounding, overflow checked; a connection field, anything else, or
   `M620` or `M251` twice in one list refuses the field. `M620` and
   `M251` must each be 32 bits wide, start on a byte and end inside the
   region; exactly one placement, or none. Each method of 12.8.1
   (`\_SB._OSC`, `\_SB.M625`, `\M049`, `\M011`, `\M624`) must be found
   once, by canonical path. More than eight fields over a region named
   `CPNV`, a path deeper than 16 names, a nesting deeper than 16 or more
   than 20000 terms in a table stops discovery.
5. **The firmware's code recognised**, in two tiers. Each of those five
   methods' code (its flags byte and body, as the AML holds it) is hashed
   (FNV-1a, 32 bits): the five hashes together one entry of a list of
   firmware builds whose code was read for this section is **a known
   build**. Else each of the five is parsed whole into its canonical form
   (12.8.10) and those five forms' hashes together one entry of the list
   of canonical forms read is **the same handover code** in another
   build. Else the firmware is declined. Both lists have one entry, the
   board's build of 2026-11-02 (identical in both of the board's
   captures, LAN on and off; its canonical forms computed off the tree
   from those captures); the qemu flavour alone adds the emulator's build
   1 (12.8.8) to each. A firmware that keeps the names and changes a
   register offset, a mask, a condition, the command's construction or a
   helper among the five is declined in both tiers. The hashes of both
   tiers are logged, so that a new build can be read and added.
6. **The tables, in the firmware's order.** The two pointers are read
   from `\CPNV` as the firmware reads its 32-bit fields; then single
   bytes of the discrete USB4 table, in the order the firmware's code
   reads them: `10h` (zero: not fitted, stop), `4Dh` (zero: not granted,
   stop), `4Bh`, `12h`, `13h`, `14h`; then the roots table's entry for
   that index, its byte 0 and, when it is not `FFh`, bytes 3 and 4. So
   no byte is read that the recognised firmware's own code does not read
   under an ACPI operating system, nor any neighbour of one, and each
   address is formed from the firmware's own pointer and offset without
   wrapping and held to step 2's bounds. The assumption is the
   firmware's: that its pointers name its own preserved memory; that the
   data survive a system that runs no ACPI is shown by T5, which read
   them at the desktop on 98 SE [inf].

#### 12.8.4 The match: when the command is sent

All of these, else nothing is sent, nothing written (no mailbox, no
power register) and the first that failed is logged:

- the host's three functions found by bus, unique, with their ids and
  roles (12.8.3 step 1);
- ECAM found and read back (step 3);
- `\CPNV`, `M620` and `M251` placed and the five methods found (step 4);
- the firmware's code recognised (step 5): a known build, or the same
  handover code with `XhciCmAsm4242Structural` 1 (12.8.6). The same
  handover code with the value 0 is reported (12.8.11) and the start goes
  on with nothing more read: no pointer of `\CPNV` is followed, no
  mailbox read;
- both pointers non-zero and within step 2's bounds;
- the discrete USB4 table's bytes `10h` and `4Dh` non-zero (the
  firmware's own conditions for a host and for handing it over);
- the roots table's entry for byte `12h`'s index present, its bus (both
  bytes; above `FFh` refused) with bytes `13h` and `14h` the root port
  found by bus, so that the firmware's target, that root port's
  secondary bus device 0 function 0, is the upstream port the driver
  found;
- the upstream port in D0 (its power management capability found by a
  bounded walk of 48 entries, a pointer below `40h` or not
  dword-aligned ending it; no capability: refused);
- the mailbox clean: the command dword not all ones and its bits 11:8
  not 1, the reply dword 0;
- the value of 12.8.6 not 0, and the once-per-boot entry reserved.

The firmware's `_OSI` condition is not applied: it would refuse every
system but 11, and what it gates is the hand-over itself, which this
driver asks for in that system's place, on the firmware build whose
code was read; its other conditions (a host fitted, the grant) are
applied as it applies them.

#### 12.8.5 The exchange

No power state is written: the upstream port must already be in D0. Each
wait is bounded twice, by a deadline on the thread's millisecond clock
and by an iteration count, polling every 1 ms with
`KeStallExecutionProcessor`, as T5 did:

1. The handover `0102h | (byte 4Bh << 16)` written at `420h`.
2. Accepted: the command dword's bits 11:8 no longer 1, within 1000 ms
   (all ones at any read: the device is gone, stop).
3. Answered: the reply's bits 11:8 not 0, within 1000 ms (all ones:
   stop); then the reply written 0.
4. Success: the reply exactly `100h`, the board's answer; any other
   reply is logged and ends it before the query (a later build whose
   answer differs is a new entry of 12.8.3 step 5's list, read first).
5. Only after a success: the query `0180h` the same way, its success a
   reply whose low byte is 0, as the firmware reads it.
6. Then 200 ms before the platform's steps go on, as T5 waited.

A timeout, an all-ones read or a reply that is not a success ends it,
logged with the values read; the reply register is written 0 only once
a reply came. The cost is bounded by discovery's budget and the
exchange's 4.2 s, once per boot. A host whose router then does not
answer fails its start as today (inert reason 12).

#### 12.8.6 Its value

`XhciCmAsm4242Handover` (REG_DWORD, the NHI's driver key, as every
`xhcicm` value): 1 (the default) sends it when everything matches on 98
SE and ME; 0 never, in either tier (the master switch: nothing of 12.8.3
is read). `XhciCmAsm4242Structural` (REG_DWORD, the same key): 0 (the
default) sends nothing to the same handover code in another build (the
tier recognised, logged and reported, 12.8.11); 1 sends it there as to a
known build; a known build is sent the command whatever it says. Any
other value of either is refused and its default stands. Both written
by the x86 `xhcicm.inf`'s 98 SE and ME install at their default with no
overwrite, each with its INF gate row and its `tuning.md` row (the
owner's rule of 2026-10-30). NT reads neither.

#### 12.8.7 The code's shape

- `src\cm\cm_osc.c` and `cm_osc.h` (pure): the root pointer's and a
  table's checks, the root table's entries, `MCFG`'s lookup and the ECAM
  arithmetic, the bounds, the AML walk of 12.8.3 step 4, the methods'
  hashes and the list of builds, the two tables' decode and the match,
  each failure a reason code. `src\cm\cm_oscc.c` (pure, new): 12.8.10's
  parse, its name lookup over the three tables, the canonical form and
  its list.
- `src\cm\cm_oscos.c` (OS): the physical reader, the inventory over
  `HalGetBusData`, ECAM's reads and writes, the exchange, the
  once-per-boot table, the log.
- Host vectors: the root pointer (checksums, revisions, alignment), root
  tables (entries above 4 GB, a ragged length, too many entries), MCFG
  (two segments, two allocations covering the buses, a misaligned base,
  a page past 4 GB), the AML walk (scopes and devices with every name
  prefix, a field over a `CPNV` in another scope not taken, a method or
  buffer holding a region's bytes skipped, an unknown term ending the
  table and nothing of it kept, strings, field lists with reserved,
  access and extended access elements, pointer fields off a byte, too
  wide, past the region, two disagreeing regions or placements), the
  hashes (a method changed by one byte, a method missing or twice),
  the tables' decode and each refusal of the match; the canonical form
  (12.8.10: each rewrite alone giving the same form, each near miss
  below giving another or a refusal); and, off the tree, the walk, the
  hashes and the canonical forms over the board's own tables.

#### 12.8.8 The emulator

Draft 5: `scripts/asm-fw-ssdt.py` gains `--build 1|2|3`, the five
methods with bodies of the model's own that carry the handover's logic
of 12.8.1 (written for the model, not the firmware's code): build 1 is
the qemu flavour's known build and its canonical forms its entry of the
second list; build 2 the same code laid out otherwise by the rewrites
of 12.8.10 alone (other Locals, other integer encodings, `Noop`s added,
compound assignments written as plain ones, the debug print's texts
changed), the same handover code; build 3 a near miss (one condition
changed), which neither tier accepts. Host vectors add the other near
misses: a debug call added or dropped, a method-local name renamed, a
mask, an offset, an operand order changed. Legs (98 SE without ACPI, the qemu flavour): build
1, sent; build 2 with the value 0, nothing sent and the installer asking
the question; build 2 with the value 1, sent; build 3, declined; plus
the legs of draft 4 not yet run (below). The release flavour declines
all three.

`usb4-emu`'s `usb4-asm-usp` gains the mailbox (opt-in, `fw-handover=on`)
and `usb4-host fw-handover-gate=on`, a host router that answers ring 0
only once the handover has come (as the board before T5), with a
property for each failure the legs read (a reply never given, a reply
other than `100h`, the command never accepted); the firmware data at an
address given as a property (`fw-data`), answered by the model; three
SSDTs of the emulator's own (`\CPNV` with its two fields, `DISCUSB4`
and the `_OSC` table, each with minimal methods of the five names) built
as AML by a script in `usb4-emu` and given to QEMU with `-acpitable`;
QEMU's q35 `MCFG` is the ECAM. The emulator's methods are not AMD's code
and are recognised by the qemu flavour's list alone. Legs on 98 SE and
ME on the qemu flavour: the command sent once and the router answering,
a tunnelled drive hot-plugged, a disable and enable sending nothing
again; refusals with nothing written: the value 0, the data naming
another root port, an unknown build (a method changed), the mailbox
busy; a reply never given (the start inert, no second attempt at the
next start). The release flavour declines the emulator's tables (its
list holds the board's build alone), read once.

#### 12.8.9 Questions for the owner, and the board's readings

- **Q13.** The value: a setting at all, and 1 by default.
  Recommendation: yes, both (the step writes to a firmware mailbox; a
  user whose board misbehaves with it can turn it off).
- **Q14.** Recognising the firmware by its exact code limits the feature
  to the board build read (other boards with the same AMD code may
  match; a BIOS update may not). Recommendation: accept, and add builds
  as users report them (the log names the hashes).
- The owner's answers (2026-11-03): Q13, yes, the value stays with 1
  its default. Q14: "For ASM4242, installer checks. IF match board,
  enable, otherwise gives user choice to opt-in": the two tiers of
  12.8.3 step 5, the second sent only on the user's choice (12.8.10,
  12.8.11).
- The board, LAN on and off: the log's every step (ECAM, the placements,
  the methods' hashes, bytes `10h`, `4Bh`, `4Dh`, the root port, the
  upstream port's power state, the two replies), the router answering,
  a tunnelled drive hot-plugged.
#### 12.8.10 The same handover code in another build (draft 5)

What the second tier accepts: a firmware whose five methods are, under
a short fixed list of rewrites, the code of a build read. It is no
search for constants: every term of each body is parsed, a term outside
the parser's set declines the firmware, and any other difference (an
offset, a mask, a condition, a call, a name's spelling, the order of two
statements) gives another form.

**The parse.** Each body (after the flags byte) as an AML term list
[acpi], recursively, with these terms alone: `If` (and the `Else` right
after it), `While`, `Return`, `Break`, `Noop`, `Release`, `Stall`,
`Sleep`; `Name`, `CreateDWordField` and its byte, word, bit and qword
forms, `OperationRegion`, `Field` (its elements as 12.8.3 step 4 decodes
them, a connection field refused); the expressions `Store`, `Add`,
`Subtract`, `Multiply`, `ShiftLeft`, `ShiftRight`, `And`, `Or`, `Xor`,
`Not`, `Increment`, `Decrement`, `LAnd`, `LOr`, `LNot`, `LEqual`,
`LGreater`, `LLess`, `CondRefOf`, `Acquire`; integer constants, `Ones`,
strings, `Buffer` (its size an expression, its bytes kept); `Local0` to
`Local7`, `Arg0` to `Arg6`; a target that is a name, a Local, an Arg or
none; and a name, which is a call when it names a method. Anything else
(`Package`, `Index`, `DerefOf`, `Notify`, a debug object, an `Else` with
no `If` before it, a length past its package) declines the firmware.

**Names, compared as written.** A name's token is its spelling: its root
or parent prefixes and its segments, never a path it was resolved to
(the five methods are found at the same paths in both builds, so the
same spelling starts its search from the same scope). The three tables'
declarations are used only to tell a call from a value and its argument
count: a reference is looked up as ACPI searches (a single segment from
the method's scope upward to the root, a longer or prefixed path as
written) in the three tables' roots (12.8.3 step 4's walk, here also
keeping every `Field` element's name in its scope, each `External`'s
type and argument count, each `Method`'s argument count); a `Method`, or
an `External` of type method, is a call taking that many arguments; one
declared both as a method and as anything else, or with two argument
counts, declines. A name the body declares (`Name`, a `Create...Field`,
an `OperationRegion`, a `Field`'s elements) must be one unprefixed
segment (in the method's own scope), declared once in the body, never
referenced before its declaration in the body, and not also declared in
the three tables anywhere the upward search from the method reaches
(where one declaration inside an `If` that did not run would let the
reference bind elsewhere): else declined. Such names are values, never
calls. A name the three tables do not declare, with the root prefix,
is accepted where it is not evaluated (the source of a `CondRefOf`, a
target); where it is evaluated, only the four the build read evaluates,
`\DIMS`, `\DDEP`, `\PSIG` and `\PWOC`, each taken as a value, never a
call: an assumed-value exemption, listed by name, not a proof (in the
build read `\DIMS` and `\DDEP` are read inside a test of `CondRefOf`
on each, `\PSIG` inside its own, and `\PWOC` is only written, inside
`\PSIG`'s). Any other undeclared name evaluated, and any undeclared name
without the root prefix, declines. What those names and a relative
name's binding are is not seen (below).

**The canonical form**, the parse written as tokens, with these rewrites
and no other, applied in this order:

1. A `Noop` statement dropped.
2. An integer by its value, whatever its encoding (`Zero`, `One`, the
   byte, word, dword and qword prefixes); `Ones` its own token.
3. A statement (not an expression inside another term) that is `Add`,
   `Subtract`, `Multiply`, `ShiftLeft`, `ShiftRight`, `And`, `Or`, `Xor`
   or `Not` with a target `T` written as `Store (op (...), T)`: the same
   effect (ACPI defines an operator's optional target as a store of its
   result), the first what ASL's compound assignment compiles to, the
   second a plain assignment of the expression. No other operator with
   a target is rewritten (`CondRefOf`'s target is a reference, not its
   result).
4. The first argument of a call to the firmware's debug print, spelled
   `M460` or `\M460` and found declared at the root (`\M460`, a method
   or an External of type method), when it is a string, compared as "a
   string" without its characters:
   an assumed diagnostic exemption (the build read formats a message
   with it), not a proof; the call, its other arguments and its place
   are compared.
5. Last, each Local renumbered in the order it first appears in the
   result.

Kept as they are: every name's spelling, every other string, a
`Buffer`'s bytes, a field's flags, widths and offsets, a region's space,
the method's flags byte (argument count, serialization, sync level),
every operator, the statements' order. **The comparison** is of
fingerprints: each method's form is hashed twice (FNV-1a and CRC-32,
both 32 bits), and the five pairs compared, in the order `\_SB.M625`, `\_SB._OSC`, `\M049`, `\M624`, `\M011`,
with the list's entry; the forms themselves are the firmware's code and
are not kept in the tree. An accidental match of a changed method needs
both of its 32-bit hashes to agree. Budgets: 20000 terms a method,
statements nested 16 deep and expressions 32, a form of at most 32 KB,
at most 64 distinct name spellings looked up a method (a repeated
reference takes the first lookup's answer, the term budget bounding
the rest); past one, the firmware is declined.

**What equal forms prove, and what not.** The command's construction,
the table bytes' offsets and the pointer they are read through, the
conditions (the fitted byte, the grant, the query flag, the cached
values), the mailbox's address (the ECAM base's name, the shifts, the
secondary bus read through `\M011`, `420h`), the field's access width
and its two offsets, the busy and ready masks, the reply's clearing,
the waits' bounds and the calls' order are all terms of the five forms:
equal forms give the values this driver implements (12.8.1, 12.8.5),
and a firmware whose values differ is declined. Not seen, by either
tier: the bodies of the names the five call or read but do not define
(`\M650`, the port's power state; `\M083`, the ECAM base; `\M046`, the
system's version; `\M460`, whose text rewrite 4 ignores; the names the
three tables do not declare); and the binding of a relative name, which
a nearer declaration in a table not read (the DSDT, another SSDT) would
take, the three tables being all that is walked. The driver replays
none of those helpers (12.8.5: no power register written; ECAM from
`MCFG`, read back). That a build with the same code behaves as the build
read on its own hardware is not proven by any reading, which is why the
second tier sends nothing unless the user asks (12.8.6). Every hardware
check of 12.8.4 holds in both tiers unchanged: the three functions
`1B21:2425`, `2423` and `2421` found by bus, unique; the firmware's data
naming that same upstream port; the port in D0, no power register
written; ECAM read back; the mailbox clean; once per boot, a failure
included; each wait bounded; the handover's success its reply exactly
`100h`.

#### 12.8.11 What the driver reports, and what the installer asks (draft 5)

**The driver.** The status record (design record 24 section 3,
`cm_status.h`) carries the outcome in its flags word, bits 18:16 the
tier on 98 SE and ME: 0 not looked for (not an ASM4242, an NT system,
`XhciCmAsm4242Handover` 0, or discovery stopped before the code: the
host's functions, the tables, `MCFG`), 1 the code not recognised (a
named SSDT missing or not walked, `\CPNV` not placed, neither tier), 2
a known build, 3 the same handover code; bit 19 set once the
handover's first write was made this boot. A reader that knows no such
bit reads 0 (as bits 15:8); the record's version stays 1. The order:
the start's first record (`CmStatusOpen`, before discovery) carries
tier 0, so an earlier start's tier never stands for this one; discovery
then sets the tier before ring 0, and the thread's next pass publishes
it whatever the start's outcome (an inert start included). Bit 19 is
kept with the once-per-boot entry of 12.8.2, so it survives a disable
and enable and every later record of the boot says it.

**The installer**, on 98 SE and ME alone. At its plan, for a present
devnode `xhcicm.inf` matches whose hardware id is the ASM4242 NHI's
(`PCI\VEN_1B21&DEV_2425`), it reads that devnode's record
(`XhciCmStatus` under `HKLM\Enum\<instance>`, the record's magic,
version and sizes checked, as the device view reads it). Tier 2 counts
only from a record of this boot (its system time less its uptime within
five minutes of the boot time the installer computes, `inst_st_fresh`)
of a devnode started with no problem; every such host tier 2 that way:
nothing asked, the default sends the command. Otherwise (no record, as
at a first install; an earlier boot's; the NHI not started; tier 0, 1
or 3): the question `XhciCmAsm4242Structural` is asked, on the
Thunderbolt and USB4 settings page and by the console, at its default 0,
as any asked value (record 26 section 5.4): written machine-wide under
`Services\USB` by the run, `/set XhciCmAsm4242Structural=0|1` on 98 SE
and ME and refused on NT; in the Settings sheet's Thunderbolt and USB4
tab as a 98 SE and ME row, greyed on NT. Its text says that 1 lets the
driver send the handover to firmware whose handover code it recognises
as a known build's, never to firmware it does not recognise. With no
ASM4242 host the question is not asked. The x64 and NT paths are
unchanged: the amd64 `xhcicm.inf` has no 98 SE and ME install and writes
neither value. The text is the owner's to rephrase (task 46.18's rule);
the draft is in `runs/run-46.md`.

**Bit 26, the refusal at `XhciCmAsm4242Structural` 0** (task 47.29,
owner 2026-11-06; reviewed and revised). A start that found the same
handover code in another build (tier 3) and did not send it because the
value is 0 (refusal `0Eh`; on NT `110h`, `cm.osc.read` `0Eh`) sets bit 26
of the flags word, on every system. It is set at that refusal alone and
cleared at each start (an NT start's every attempt clears it too):
tier 3 with bit 19 clear does not say it, since a start at value 1 can
still end unsent (the mailbox busy, a reply not given, the guard). A
reader that knows no such bit reads 0; the version stays 1 (the
connection manager's handover word, `CmOscReport`, carries it as bit 10,
`CM_OSCN_F_SAMEOFF`). XHCITR's report then says, below its stage line
(`tr_ho_same_text`): "Handover not sent: this firmware is not a known
build, but has the same handover code. To try it, set the Thunderbolt
setting for such firmware to yes." - the owner's draft, for the owner
to rephrase (task 46.18's rule). On 2000 to 10 the value is only a row
under the installer's Advanced settings (12.8.15.12), which nothing else
names; 98 SE and ME have the installer's question. Read on the
emulator's build 2 tables (`runs/run-47.md`, "47.29").

#### 12.8.12 What the log says, for a board not yet read (draft 5)

The connection manager's notes (`XHCITR`'s report carries them), so
that a report from another board is enough to tell which firmware build
it has and how far the match went, without asking the user:

- each named SSDT walked: `cm.osc.ssdt` (0 `CPMCMN`, 1 `DISCUSB4`, 2
  `CPMMSOSC`), then its OEM ID (`cm.osc.ssdt.oemid`, characters 1 to 4,
  and `.oemid2`, 5 and 6), its OEM table ID (`.tableid`, `.tableid2`),
  its OEM revision (`.oemrev`) and its length (`.length`), each
  characters' dword with the first character in bits 7:0; `cm.osc.ssdt.
  refused` with the index of one not walked. The DSDT is not read.
- the first tier: `cm.osc.method.hash`, five, in the order `\_SB.M625`, `\_SB._OSC`, `\M049`, `\M624`, `\M011`
  (method 0 to 4 below);
- the second tier, when the first fails: `cm.osc.form.fnv` and
  `cm.osc.form.crc` per method, or `cm.osc.form.refused` with the
  method in bits 31:24, the reason (`CM_OSCC_W_*`: 1 a term outside the
  set, 2 a length, 3 a budget, 4 the form full, 5 a name string, 6 a
  name's declaration, 7 a body's own name, 8 too many names) in bits
  23:16 and the offset in that table's AML in bits 15:0;
- `cm.osc.tier` (0 to 3, 12.8.11);
- a recognised tier's contract, the values this driver implements:
  `cm.osc.contract.mailbox` (`0420h` and `0430h`), `.commands` (`0102h`
  and `0180h`), `.masks` (bits 11:8, `100h` busy), `.bytes` and
  `.bytes2` (the discrete USB4 table's offsets `10h`, `4Dh`, `4Bh`,
  `12h`, `13h`, `14h`), `.m620` and `.m251` (the two pointers' byte
  offsets in `\CPNV`);
- then the values read and each check as before: `cm.osc.dusb4.10`,
  `.4d`, `.4b`, `cm.osc.root`, `cm.osc.mb.before`, `cm.osc.handover`,
  `cm.osc.query`, and `cm.osc.refused` with the first check that failed
  (12.8.4's order, the reasons of `cm_osc.h` and `100h` to `107h` for
  the hardware checks; `0Eh` the same handover code with
  `XhciCmAsm4242Structural` 0).

A report from a user's board may carry its maker's table ids; they stay
in the report. Host vectors use made-up ids.

#### 12.8.13 On the NT systems (task 46.12, draft 10)

Draft 10, 2026-11-03, reviewed and revised: the board's namespace
audited for a route through the documented ACPI interfaces (12.8.13.10):
none; Q15 stands. Draft 9, 2026-11-03, reviewed and revised: the owner's decisions of
2026-11-03 on draft 6's open points ("safe routes only"; once per boot
and again after S4; the same tiers and opt-in as 98 SE and ME) applied.
Two of the routes those decisions require have no documented interface
on any NT system (12.8.13.2), so the section ends in two questions for
the owner (Q15, Q16, 12.8.13.3); the design below them (12.8.13.4 to
12.8.13.8) is what options B and C would build on, to be reviewed again
with the owner's answer. Nothing of it is coded; 98 SE and ME are
unchanged. Draft 6, 2026-11-03: the NT systems, not converged.

**What the firmware's own code does there.** 12.8.2 said that an ACPI
system runs the firmware's own `_OSC`, so that only 98 SE and ME need
the handover replayed. Read again (the board's tables, 12.8.1): the
platform-wide branch sends `0102h` only when its non-query `_OSC` is
evaluated with revision 1, the host fitted (byte `10h`) and the
firmware's system version `\M046` at least `0Bh`; the USB4 branch sends
`0180h` under the same two tests. No capability bit the system asks for
takes part (bit 18, native USB4, only decides whether the grant is taken
away when byte `4Dh` is 0). `\M046` is set by `\M045` from `_OSI`: `0Bh`
for "Windows 2021", `0Ch` for "Windows 2022", lower for earlier strings,
0 for none. Microsoft's published `_OSI` table names "Windows 2021"
Windows 11 and "Windows 2020" Windows 10 version 2004, and a Windows
answers true only to its own string and the earlier ones [ms-osi]. So on
2000, XP, Vista, 7, 8, 8.1 and every release of 10 the firmware sends
neither command, whatever `_OSC` they evaluate (2000 and XP predate
`_OSC`, ACPI 3.0): the host's router stays silent there as on 98 SE
unless it answers without the handover, which is unread. The owner's
answer (2026-11-03): the handover on the NT systems too, by the same
rules, by safe routes only.

##### 12.8.13.1 The owner's rules, as this design applies them

- The mailbox only through the upstream port's own stack (configuration
  requests to its PDO), never ECAM mapped by this driver on NT.
- A physical read only where the system's documented memory map proves
  the address is firmware memory (reserved, ACPI NVS or ACPI reclaim),
  else declined and logged.
- The system's own firmware table interface where it gives what is
  needed.
- The x86 image loads on 98 SE and ME: no new static import that their
  kernels lack, and a run-time lookup only through a route the import
  gate allows.
- Once per boot, a reload of the driver included; again after a resume
  from hibernation (S4), not after standby (S3).
- The tiers and the opt-in of 12.8.6, 12.8.10 and 12.8.11; every check
  of 12.8.4.

##### 12.8.13.2 What the documented interfaces give

[ms] Microsoft's driver documentation, as read for this draft:

- **The tables.** `AuxKlibGetSystemFirmwareTable` is the documented
  kernel route, from Vista on, a routine of the static library of the
  Windows 8 and later kits (with `AuxKlibInitialize`); neither is in this
  tree's kits (the Windows 2000 DDK, WDK 7.1). It gives the first table
  of a signature alone (the board has fourteen SSDTs, and the three
  named ones are not the first), and no page promises `XSDT` or `RSDT`
  through it. The native query it is built on is not documented for
  drivers. 2000 and XP have neither.
- **The memory map.** No NT system documents a typed firmware memory
  map (reserved, NVS, reclaim) to a driver. `MmGetPhysicalMemoryRanges`
  is declared by the DDK but listed among the routines reserved for
  system use; `MmGetPhysicalMemoryRangesEx2` is documented from Windows
  10 version 2004 and gives the memory the system manages, untyped, a
  snapshot that does not hold the configuration still. `MmMapIoSpace`
  maps I/O space or locked pages; a mapping must agree in its cache type
  with every other mapping of the page, and the Driver Verifier stops a
  mapping of physical memory that is neither (bug check `C4`, `83h`).
- **Configuration space.** `IRP_MN_READ_CONFIG` and `IRP_MN_WRITE_CONFIG`
  (and `BUS_INTERFACE_STANDARD`) are documented on every NT system;
  extended configuration space (offsets 256 and up) is documented from
  Vista and Server 2008, subject to the platform's `MCFG`; on 2000 and XP
  it is not promised, and an access the route cannot make may read as
  all ones and write to no effect.
- **Power.** `IRP_MN_SET_POWER`'s transition table: a hybrid sleep is
  entered with a hibernate request (target S3, effective S4), and both
  its outcomes, a wake from S3 and a resume from the hibernation file
  after a power loss, come back with the same S0 request (target and
  effective S0); only the context's `CurrentSystemState` differs, which
  is reserved for system use. A plain hibernation and Windows 8 and
  later's fast startup leave by a hibernate request too. A system power
  request goes down each devnode's own stack: a parent bridge's filter
  is not documented to see the NHI's, and an NHI disabled across a
  hibernation sees none.

So under the rules as given the handover is declined on every NT system
at its tables; and even with a table route, "again after S4, not after
S3" cannot be told apart for a hybrid sleep, nor seen for an NHI
disabled across the hibernation.

##### 12.8.13.3 The owner's questions

**Q15, the tables and the memory.** No NT system offers a route the
rules admit to the three named SSDTs and the firmware's data.

- **Option A**: keep the rules. The handover is not sent on NT; no NT
  code of it is built. The owner's bench on 10 x64 reads whether the
  board's router answers there without it (12.8.2 as corrected); the NT
  legs of the design below are retired, and what is read on NT is the
  model's tables loading (12.8.13.9).
- **Option B**: on Windows 10 version 2004 and later only, a weaker proof
  of memory, and the documented table route. Its prerequisites, each a
  refusal when it fails: the root table (`XSDT`, else `RSDT`) and `MCFG`
  from `AuxKlibGetSystemFirmwareTable` (the Windows 8 or later kit's
  library added to the tree for the x64 build; B is limited to x64, since
  bringing that library to the x86 image, which also loads on 98 SE and
  ME, would need an NT-only integration of its own), declined when the
  interface does not give the
  root table, which is not promised; a physical read only where no byte
  of its pages lies in any range `MmGetPhysicalMemoryRangesEx2` reports,
  besides 12.8.3 step 2's bounds; the first SSDT the interface gives
  compared byte for byte with the first SSDT at the root table's address
  (a consistency check after that page is admitted, not its admission).
  What it does not show, and stays a risk: that the pages are firmware
  memory rather than a device's registers, that no other mapping of them
  has another cache type, and that the system does not take them later.
- **Option C**: B on every NT system from XP x64, Server 2003 SP1 and
  Vista on: the native firmware table query
  (`ZwQuerySystemInformation`, information class 76, the
  `SYSTEM_FIRMWARE_TABLE_INFORMATION` block WDK 7.1 declares), which is
  not documented for drivers, and before 10 version 2004
  `MmGetPhysicalMemoryRanges`, reserved for system use; on x86 both
  looked up through the x86 helper (`xhcihlpr.sys`, a channel version
  2.1 handing out `MmGetSystemRoutineAddress`). Broader, on routines
  Microsoft does not document for drivers, with B's risks.

Recommendation: **A** until the bench has read 10 x64 without the
handover. If the router is silent there, B is the narrowest route that
can send on that system, with the risks stated and the root table's
availability read first on the board (a decline if it is not given).

**The bench line A waited for** (owner, 2026-11-03): on the owner's
board, Windows 10 x64 22H2 (build 19045), CSM, Above 4G Decoding on,
`3.4.0.0`'s `xhcicm.sys` bound the NHI (problem 0) and failed its USB4
start with inert reason 12 (host router unanswered): ring 0's first
control read was submitted, its transmit reported stuck
(`tbt.ring.tx-stuck`), and no answer came. On 98 SE before the handover
the transmit completed and nothing answered, so a shared cause is
plausible, not established (12.8.15.1). The owner answered B, then C
(12.8.15).

**Q16, S4 under B or C.** Hybrid sleep (12.8.13.2) cannot be told
apart, and an NHI disabled across a hibernation does not see it. Options:
(a) treat every wake that follows a hibernate request as S4, so a hybrid
sleep woken from S3 is sent the handover again too (one send more than
the rule asks, which the firmware itself would not make after an S3);
(b) never send after a hybrid sleep's wake, so a hybrid sleep resumed
after a power loss leaves the router silent until a restart. In both an
NHI disabled across the hibernation is not sent again at its enable (its
router fails its start, logged). Recommendation: (a), since a missed
handover leaves USB4 dead until a restart, and what an extra one does
is unread but is the command the board answered before.

##### 12.8.13.4 Under B or C: where, its order, and the mailbox

**Where.** `CmOscHandover`, in `xhcicm.sys`'s start on the NT targets the
option admits, at 12.8.2's place (after BAR0 is mapped, before ring 0),
for an NHI with the ASM4242's ids under the USB4 profile; and at an S4
resume (12.8.13.6). **Order**: the host's three functions by bus
(12.8.3 step 1, `HalGetBusData`, which reads the first 256 bytes on
every NT system's HAL); the mailbox route found and checked against them
(no mailbox dword read); the served check (12.8.13.5); the tables and the
code (Q15's route, then 12.8.3 steps 4 and 5, the same pure code); the
tier decided from the code alone, and the start refused there, its tier
reported, when the code is no known build and is not the same handover
code with `XhciCmAsm4242Structural` 1, so a structural tier with the
value 0 follows no pointer and reads no firmware data (12.8.4; a host
vector holds that no data read happens then); only then the data
(12.8.3 step 6, under Q15's proof); the rest of 12.8.4's match, the
mailbox's two dwords read only now, as on 98 SE and ME; the
reservation; the exchange of 12.8.5 through the route.

**Its install.** Both filter INFs (`xhcicmf.inf`, `xhcicmf-amd64.inf`)
gain the upstream port's line, `PCI\VEN_1B21&DEV_2421&CC_0604`, with a
description of its own (`U4AsUspDesc`, the owner's to word), installed
as the downstream ports' lines are (machine.inf's `PCI_DRV` kept, the
device-specific `UpperFilters`); the installer's candidate list
(`instTbPortIds`) gains the id, so the forced install reaches the port;
the CM gate's description rule gains it. `CmPortsFilterId`'s list is
unchanged: the port is never a tunnel port or a domain member.

**Its role.** `cmAddDevice` gives that id, above the PCI driver's FDO, a
role of its own, the mailbox port (`CM_ROLE_MAILBOX`, an extension of
kind `CM_KIND_MAILBOX`; design names, not in the tree under them, the NT
route as built being section 12.8.15's), decided before `CmRoleDecide`, with a dispatch
of its own (not `cm_filt.c`'s bridge path, which publishes, reads
relations and attaches children for every bridge). Every PnP and power
request passes down unchanged but for two things. At a start that
completed below it successfully it reads its location (bus number and
address, as `cmLocation` does) and publishes an entry on the mailbox list
(four entries, a spin lock of their own); at stop, surprise removal and
removal it withdraws the entry and waits until no request of the NHI's
holds it (a count taken and dropped under the lock around each request,
an event set at zero) before it passes the request down.

**The requests.** The NHI's thread takes the entry for the upstream
port's location under the lock (none: declined, `10Ah`), sends one
`IRP_MN_READ_CONFIG` or `IRP_MN_WRITE_CONFIG` (`PCI_WHICHSPACE_CONFIG`,
4 bytes, dword-aligned, a buffer of its own) to the port's PDO, waits,
and drops the entry; each request takes the entry again, so a withdrawal
between two requests ends the step (mid-exchange: failed as a timeout
is, logged, the obligation consumed). A request succeeds only with a
success status and exactly 4 bytes transferred. First, through the
route: dword 0 and dword `18h` equal to what `HalGetBusData` read (the
route reaches the port found by bus), else `10Bh`; the power state D0
by the capability walk of 12.8.4. No power state is written, nor any
byte but the mailbox's two dwords. Whether `420h` and `430h` are
reachable is the PCI driver's: the first mailbox read failing, short or
all ones declines (`109h`). The port is the NHI's ancestor, started and
in D0 while the NHI starts. `MCFG` is read on NT only for 12.8.3 step
2's bound on physical reads; ECAM is never mapped.

##### 12.8.13.5 Under B or C: once per boot, across a reload

On NT the image stays loaded for the boot once its stor98 link-state
callback is registered (`cm_nvlinkos.c` removes `DriverUnload` before it
registers; `stor98cm.h`, "CM lifetime (NT)"), so the static table of
12.8.2 lasts the boot; a registration that failed gives the unload
routine back, and then a reload is possible. For that case, as the
owner asked, a boot marker: a permanent named callback object per
upstream port, `\Callback\Xhci98CmOsc<bdf>` (eight hexadecimal digits;
this driver its only creator), made by `ExCreateCallback` with
`OBJ_PERMANENT` as `cm_nvlinkos.c` makes its own. A permanent object
lasts until the system restarts, survives hibernation and is kept
across an unload and load; every pointer returned is dereferenced at
once, the object kept by its permanence. `ExCreateCallback` does not say
whether it created or opened, so the sequence is serialised through the
static table: under a driver-wide mutex held at PASSIVE_LEVEL (never a
spin lock across the calls), the static entry first (served: stop),
then an open with `Create` FALSE (success: served by an earlier load;
the entry marked served, stop), then the static entry reserved and the
object made with `Create` TRUE (failure: declined, `10Ch`, the entry
released, nothing written). The served check of 12.8.13.4 is the first
two alone (no reservation), before the tables. A second object,
`...<bdf>A`, made once the handover's reply was `100h`, tells a reloaded
image that the first send succeeded (the S4 rule's condition). The
wrapper lives in `cm_nvlinkos.c`, where `ExCreateCallback`'s `SITES`
admits it. A volatile registry key was the owner's example;
`ZwCreateKey` is not imported by the x86 image and has no 98 SE or ME
evidence (`cm_statos.c`), so the permanent object serves the same end.

Which reload is reachable: an NHI disabled and enabled (a fresh
extension, the image resident) is a leg; an image reload in one boot
needs the callback's registration to have failed, and is shown by host
vectors of the sequence alone.

##### 12.8.13.6 Under B or C: after S4

ACPI asks the system to evaluate `_OSC` again after S4 and leaves S1 to
S3 to the firmware's restoration of context [acpi]. The NHI's system
power dispatch (`cm_power.c` reads the request already) marks the port's
static entry "owed" when a hibernate request (`PowerSystemHibernate`)
completes successfully below it, and clears the mark when any other
sleep request completes; a failed or cancelled request changes nothing.
That is Q16's (a): a hybrid sleep's wake is owed too. The obligation is
consumed exactly once, at the NHI's next D0 work (`cm_power.c`, before
`TbtU4Resume` at its line 333, which restarts ring 0, the power
requests' order kept), only when the port's first send succeeded (the
reply `100h`, in the static entry or the second object): the whole of
12.8.13.4 again, the served check and the reservation both bypassed for
that send, the owed mark cleared at the start of that attempt, before
any table is read: a refusal before the first mailbox write, a failure
and a second D0 alike send nothing more until the next hibernation. A port never served, or
whose first send failed, is not sent at a resume. An NHI disabled across
the hibernation is not sent again (Q16).

##### 12.8.13.7 Under B or C: the values, the status and the installer

`XhciCmAsm4242Handover` (1) and `XhciCmAsm4242Structural` (0) are read
on the admitted NT targets and written by the `xhcicm.inf` file of each
at their default with no overwrite, with their INF gate rows; their
`tuning.md` rows name the systems. Bits 18:16 and 19 of 12.8.11 there
too. The installer's question of 12.8.11 on those systems (`INST_APPLY_CM`
held to them; the record read on NT from the hardware key's `Device
Parameters`, as the device view reads it; `/set` accepted there; the
Settings sheet's row not greyed there).

##### 12.8.13.8 Under B or C: the log, host vectors and legs

The log as 12.8.12, plus `cm.osc.route` (the mailbox entry's location),
`cm.osc.resume` (1 at an S4 send) and the refusals `108h` (a system the
option does not admit, or no root table) to `10Ch` above. Host vectors:
the route's checks (a short transfer, a failure, all ones, a header that
differs); the marker's sequence (served in the table, served by an
earlier load, reserved and made, made failed; the success object); the
S4 decision (hibernate then D0, sleep then D0, hibernate then sleep then
D0, a failed sleep request, two D0 in a row, never served, a first send
that failed). Legs on the emulator (the qemu flavour; 7 and 10 x64 with
the `--osi` tables, 12.8.13.9): the known build sent once and the router
answering; an NHI disabled and enabled, nothing sent; a plain S4 and
resume re-sent, and a fast startup's return; an ordinary S3 and resume
not; a hybrid sleep woken from S3 re-sent under Q16 (a), not under (b);
a hybrid sleep resumed from its file after a power loss re-sent under
(a), not under (b); an NHI disabled across a hibernation and enabled
after it not sent (its router failing its start, logged); a replay
refused before its first write (the data made to fail) and then another
D0, nothing sent at either; build 2 with the value 0 not sent and the
installer asking, with 1 sent; build 3 declined; the release flavour
declining the emulator's tables; on a system the option does not admit
(2000, XP, and under B 7), declined with `108h` and nothing written; a
10 x64 UEFI (OVMF) start where its base exists. The board, 10 x64, the
owner's bench.

##### 12.8.13.9 The emulator on NT

The model's `_OSC` (`usb4-emu` `scripts/asm-fw-ssdt.py --build`) sends
the commands to any system that evaluates it, which the board's does
not; the new `--osi` adds the board's condition (`\M045` from `_OSI
("Windows 2021")`, `\M046` at least `0Bh` before each command), so a
system before 11 that evaluates `_OSC` sends nothing. Its builds'
methods are other code, other entries of the qemu flavour's lists. The
data region must lie in a hole of q35's map (`9FF00000h` with 2 to 4 GB
of RAM): in the guest's RAM (`7FF00000h` with 2 GB) Windows 7 stopped at
boot with bug check `A5` (`1000h`, 0, `7FF00000h`, `100h`).

Whether the NT bases load the model's tables is **not settled** (read on
the q35 bases, 2026-11-03, `runs/run-46.md`, "46.12 on NT"): without
them every base booted; with them the same tables booted on one start
and stopped with bug check `A5` (`11h`, 8) on another, with one or two
virtual processors - Windows 7 x86 with build 1 `--osi` booted once and
stopped four times, XP with build 1 booted once and stopped twice,
Windows 10 x64 with build 1 `--osi` booted once (one more start not
read); 7 with build 1 without `--osi` stopped. No mailbox command reached
the model on any of these starts. Under option A nothing rests on it;
B or C needs it settled before its legs.

##### 12.8.13.10 The board's namespace, audited

2026-11-03 (task 46.12 on NT), reviewed and revised. The question: does
the board's ACPI namespace offer a route through the documented
interfaces, a method evaluated on a device's own stack
(`IOCTL_ACPI_EVAL_METHOD`, `IOCTL_ACPI_EVAL_METHOD_EX`) or an ACPI-WMI
block, that either sends the handover without the `\M046` gate or returns
what the handover needs (the command's data byte, bytes `10h` and `4Dh`,
the upstream port's identity), so that no firmware memory is read on NT?
[fw] Every table of the board was read (the DSDT and the fourteen SSDTs;
the capture holds no table loaded at run time and no table loads one),
with a call graph over every method name (an over-approximation: a name
it does not reach is not reached).

**The devices on the path.** The root port above the host, the switch's
upstream port, its four downstream ports, the xHCI and the NHI each have
a namespace node with the same set of methods: wake and sleep states
(`_S0W`, `_S3W`, `_S3D`, `_PRW`; two of them read bytes of the discrete
USB4 table), `_STA` and `_INI` (the function probed through ECAM),
`_REG`, `_DSW`, `_PS0` and `_PS3`, a power resource of bookkeeping alone,
and `_DSD` properties (hot plug in D3, external facing port, the xHCI's
ports' `usb4-host-interface`). The root port's `_PS0` and `_PS3` power the
complex on and off (platform GPIOs and the platform's PCIe library
method). No device has `_DEP` (the `_OSC` branch that would remove one
names objects no table defines) or `_OSC`. The one `_DSM` on the path is
the NHI's, under a UUID this record finds in no published document: its
function 1 sends sleep-entry notifications (S3, S4, S5) through
`\_SB.M625`, and it answers every function, 0 included, with the integer
0, no supported-function buffer.

**WMI.** Four ACPI-WMI devices (`PNP0C14`), the board maker's and the
platform's (lighting, the board maker's software interface, overclocking,
and one more). No method of any of them reaches `\_SB.M625` or the
firmware's data pointers, and none reads firmware memory at an address
the caller gives; three raise a system management interrupt, whose
handler is not readable and has no published contract. The software
interface has, besides its fixed functions, a second method block of raw
hardware access (I/O ports, model-specific registers, SMBus, the embedded
controller, and the configuration space of any bus, device, function and
offset through ECAM): it could address the mailbox, but it gives no
firmware data (so not the data byte), it is ECAM access, which 12.8.13.1
forbids this driver, and it is an interface no published document
describes.

**`\_SB.M625` and its callers.** The method takes the command and a
timeout and holds no `\M046` test: its only condition is that the
discrete USB4 table exists. It finds the mailbox as 12.8.1 says, keeps
the address for the boot, puts the upstream port in D0 by writing its
power state through ECAM and restores it after, and returns the reply. Its
callers: `\_SB._OSC` (the handover and the capability query, each gated
in the caller on byte `10h` and `\M046` at least `0Bh`; `\M046` written by
`\M045` alone, which `_OSC` alone calls); the NHI's `_DSM` above; and the
`_PS0` and `_PS3` of the upstream port, the four downstream ports, the
xHCI and the NHI, two commands each, other than the handover and the
query and described by no published specification, with no `\M046` test.

**The data.** Bytes `4Bh`, `4Dh` and `12h` to `14h` of the discrete USB4
table are returned by no method; byte `4Dh` shows only as `_OSC`'s grant,
which is withdrawn whenever `\M046` is below `0Bh`; byte `10h` only as
present or absent, through two I2C devices off the PCI path. What the
methods of the path return (the wake GPE, the deepest wake state) the
handover does not need. The upstream port's identity is the PCI topology
12.8.3 step 1 already reads.

**The interfaces** [ms-acpi]. `IOCTL_ACPI_EVAL_METHOD` is documented from
Windows 2000, `IOCTL_ACPI_EVAL_METHOD_EX` from Vista, both at IRQL below
`DISPATCH_LEVEL`, sent to the device's PDO, through its own stack; the
first evaluates a method that is an immediate child of that device's
namespace node, the second one of the device or a descendant. Acpi.sys
handles them for devices the tables describe; for a function another bus
driver enumerates it attaches a filter only where the device is described
and permanently connected, and a driver may assume neither its presence
nor its absence (a failed request is a decline). No documented request
evaluates a method outside the device's own namespace or writes a
namespace object: `\_SB.M625`, `\_SB._OSC` and the root-level byte
readers lie outside every device's namespace. ACPI-WMI blocks reach a
driver only as WMI classes.

**Verdict.** (a) No route sends the handover: its only sender is `_OSC`,
gated on the system's `_OSI` answer in the caller; the ungated sender
method lies outside every device's namespace, needs the data byte, and
writes the port's power state through ECAM, which 12.8.13.1 forbids this
driver; nothing reachable from a device's methods or a WMI method sends
it, and the WMI block that can write any configuration space does so
through ECAM, undocumented, without the data byte. (b) No route returns
the data. The interface that would have carried
either, `IOCTL_ACPI_EVAL_METHOD` (and `_EX`) on the NHI's or the upstream
port's own stack, has nothing on this board to carry. Q15's options and
recommendation stand.

**For a design under B or C.** On any ACPI system, before 11 too, the
`_PS0` and `_PS3` of the seven functions write the same mailbox whenever
the system evaluates them and the sender method finds the upstream port
(it writes nothing when the root port's secondary bus reads 0),
serialised by a mutex of the firmware's own that no driver can take. An
exchange of 12.8.13.4 may therefore meet a D-state transition of any
function on the path (a downstream port's or the xHCI's runtime D3, say):
the exchange must be kept away from those transitions or a collision
treated as a failed exchange (`109h` or the timeout of 12.8.5), and the
mailbox's clean check of 12.8.4 read immediately before the write.
Whether 2000 to 10 evaluate those methods on this board, and so send the
router these commands at boot, is not observed; nor whether the router
answers without the handover there, still the bench's reading under A.

[ms-osi]: Microsoft, "How to Identify the Windows Version in ACPI by
Using _OSI" (learn.microsoft.com, windows-hardware/drivers/acpi/
winacpi-osi), its table of `_OSI` strings.
[ms]: Microsoft, learn.microsoft.com: `AuxKlibGetSystemFirmwareTable`
(windows-hardware/drivers/ddi/aux_klib), the kernel routines reference
(its list of routines reserved for system use),
`MmGetPhysicalMemoryRangesEx2` (ddi/ntddk), `MmMapIoSpace` and
`MEMORY_CACHING_TYPE` (ddi/wdm), bug check `0xC4`, "Accessing PCI Device
Configuration Space" (drivers/pci), `IRP_MN_SET_POWER` (drivers/kernel,
its system power transition table), `SYSTEM_POWER_STATE_CONTEXT`
(ddi/wdm), `ExCreateCallback` (ddi/wdm).
[ms-acpi]: Microsoft, learn.microsoft.com: `IOCTL_ACPI_EVAL_METHOD` and
`IOCTL_ACPI_EVAL_METHOD_EX` (windows-hardware/drivers/ddi/acpiioct),
"Evaluating ACPI Control Methods" and "Evaluating ACPI Control Methods
Synchronously" (drivers/acpi), "Acpi.sys (Windows ACPI Driver)"
(drivers/kernel/acpi-driver).

#### 12.8.14 The tables kept for XHCITR (task 46.21)

So that a report from another board can carry its firmware's tables
without a Linux capture, what 12.8.3 steps 2 to 4 read at a start is
kept and served by the query device's fifth request (design record 25
section 21, `IOCTL_XHCICM_ACPI`). **No read is added, moved or repeated
for it**: every byte kept is copied from a buffer discovery had already
read and checked, at the moment discovery has it, and nothing kept
changes what discovery decides, logs or sends. A copy that cannot be
made is said so in the answer, never retried.

- **What is kept.** The root pointer as found and checked (its 20
  bytes, and from revision 2 its whole length, 36 to 255 bytes, copied
  from the BIOS area discovery read it in); the root table whole (the one discovery took:
  the XSDT, or the RSDT); for each root entry whose 36-byte header the
  `MCFG` pass read, that header; `MCFG` whole where it was read whole
  and summed to zero; each SSDT of the three OEM table ids read whole
  and summing to zero, whether or not its walk then succeeded. A table
  read whole that failed its checksum is kept as its header alone; any
  other SSDT is kept as its header (12.8.3 step 4 reads no other table
  whole). **The DSDT is not read** (12.8.12), so it is never kept: its
  address is in the FADT past the FADT's header, which discovery does
  not read either.
- **When.** Built by the start that runs discovery (the ASM4242 under
  the USB4 profile, `XhciCmAsm4242Handover` 1), whatever discovery's
  outcome; a start that stops before the tables (the host's ports not
  found) keeps none, with its state. Each start builds its own and
  frees the last; the copy moves to the query device when that device
  is created (`CmQueryOpen`) and is freed when it closes, or at the
  NHI's stop or removal when no device took it. The request copies out of it
  with no lock held, under a count of readers the device's lock guards,
  so the close frees it only once no request reads it.
- **Bounds.** At most 66 entries (the pointer, the root table, 64
  entries) and 256 KiB of bytes kept (`CM_QA_DATA_MAX`): past it a table
  is kept as its header and the answer says cut. The pool used is the
  bytes kept and the entries, once per start (nonpaged, as every
  allocation of this driver).
- **Its state** when nothing is kept: NT (none of this runs there), not
  an ASM4242 under the USB4 profile or no discovery yet, the value 0,
  the host's ports not found, no root pointer, no memory; with what is
  kept, discovery's own outcome and tier (12.8.11, 12.8.12), so the
  index the tool writes says how far the match went.
- **Host vectors** use made-up tables (`test_tracpi`); the legs read
  the emulator's tables (12.8.8), compared byte for byte with the files
  QEMU was given.
- **The DSDT and the other SSDTs** are read by `xhci98.sys` only when
  the user asks (12.8.16); this set is unchanged by it.

#### 12.8.16 The firmware's tables read on request (task 46.21, the owner's option B)

The owner, 2026-11-03: on 98 SE and ME the save holds the DSDT and every
SSDT whole, read when the user asks, by **`xhci98.sys`**, which every
machine with this package's xHCI controller driver runs, so any board's
tables can be saved. 12.8.14's copies stay as they are, `xhcicm.sys`'s,
read at a start by its discovery, and are saved beside them when present
(record 25 section 21 item 9). This section is the reading itself; the
request is design record 13 section 8.13. **A start that is never asked
reads nothing new**: nothing of it runs at a start, and 12.8.3 to 12.8.12
are unchanged. The walk is a source file of its own
(`src\xhci_acpi.c`, pure, with a physical reader the caller gives), used
by `xhci98.sys` alone; `xhcicm.sys`'s discovery is not moved into it, so
46.12's code and its reads stay as they are.

- **When and where.** On the caller's thread at `PASSIVE_LEVEL`, inside
  the request asking for the set at offset 0 when none is held. One
  reading at a time per controller (a synchronization event, taken only
  by a successful wait of at most 10 s; else busy); no spin lock held
  across a wait, a mapping or a read; a whole reading bounded by 20 s
  of the thread's clock, checked between tables (past it the tables not
  yet read are listed as not read, "timeout"; past it before the
  bootstrap pass ends, the set is refused, outcome 4).
- **Gates.** 98 SE and ME alone ("not served on this Windows" on NT);
  `XhciAcpiTables` not 0 (design record 13 section 8.13: REG_DWORD,
  default 1, written by the INF). Nothing about the controller, the
  ASM4242 or `xhcicm.sys` is required.
- **What is read**, with 12.8.3 step 2's rules: physical memory mapped
  for the read and unmapped after, a mapping never more than 64 KB
  (counted from the page it starts in: a table is read in pieces whose
  first page and length together stay within 64 KB, in order), a byte at
  a time; every table above 1 MB and below `FEC00000h`, each address
  formed without wrapping; **no address or length from the caller**:
  the input names a body offset and nothing else, and every table read
  is the root pointer, the root table it gives, a table that root table
  lists, or the DSDT its FADT names.
  1. the root pointer, as 12.8.3 step 2 finds it (the EBDA's first KB,
     then `E0000h` to `FFFFFh`, its checksums enforced), kept at its
     checked length (20, or 36 to 255 from revision 2);
  2. the root table it gives (the XSDT below 4 GB, else the RSDT), its
     header first, its length at most 36 + 8 x 64 bytes and a whole
     number of entries, its checksum enforced;
  3. a bootstrap pass: each entry's 36-byte header, held to the bounds
     above, to find `MCFG`; **every** entry must be read and classified
     (an entry of address 0, above 4 GB, outside the bounds or whose
     header cannot be read refuses the set, outcome 3: it could be the
     `MCFG`, and no window would be known); that `MCFG` read whole, its checksum enforced,
     44 bytes plus a whole number of 16-byte allocations, at most 16;
     **every** allocation, whatever its segment, the half-open window
     `[base + (first << 20), base + ((last + 1) << 20))` - the base being
     bus 0's - its base 1 MB aligned with a high dword of 0, its first
     bus not past its last, the sum not wrapping past 4 GB; no later read
     may touch a window. An `MCFG` that fails, or a second one, refuses
     the set ("the tables in memory could not be read safely"). A root
     that lists no `MCFG` declares no configuration space in memory, and
     the set is read with no window but the bounds;
  4. each entry's header again, now held to the windows too; `FACP` (at
     most 4 KB) and every `SSDT` (at most 256 KB each) whole; through
     `FACP`, the DSDT (at most 1 MB): `X_DSDT` (offset 140) from a FADT
     of at least 148 bytes when its high dword is 0 and its low dword
     not 0, else `DSDT` (offset 40) from one of at least 44 bytes;
  5. `MCFG` is kept whole too (read for its windows, step 3); any other
     entry stays its header.
- **Checked before a table is kept whole** (the reading comes after the
  firmware has run, and memory may no longer hold a table where its
  root says it is): its signature the one the entry's header gave (the
  DSDT's `DSDT`), its length that header's, at least 36 and within its
  cap, the whole read's own header the same, and its checksum zero. A
  table that fails any of these is kept as its header (or nothing) with
  the reason, and is never saved: "no longer a table at its address"
  (signature), "its length is wrong" (length), "its checksum is wrong",
  "outside the memory it may be read from" (bounds or a window), "past
  the size limit" (a cap or the 4 MB budget), "no memory", "timeout".
- **Bounds and storage.** At most 67 entries (the pointer, the root
  table, 64 root entries and the DSDT) and 4 MB of table bytes in all.
  Before any table is read whole into the body, a bounded work block
  holds what planning needs: the root pointer, the root table (at most
  548 bytes), every entry's header (67 of 36 bytes), and, each read
  whole and checked and freed once used, the `MCFG` (its windows) and
  the FADT (its DSDT address; read again for the body, with its checks);
  the DSDT's header is read last. Then every size is known and the body
  is **one allocation** of exactly that size, at `hcd_pool.c`'s named
  site (record 13 section 7.5); a table planned whole and not kept
  leaves its place zeroed with its header at its start. A table past the
  budget is kept as its header ("past the size limit").
- **Flags, from what is kept.** Cut where a table was kept as its header
  for the budget; incomplete where a FACP, an SSDT or the DSDT, or any
  entry, is not kept whole; no DSDT where none is kept whole (a FADT
  shorter than 44 bytes, no DSDT address, or its DSDT not kept).
- **No residue.** The body is freed after its last part has been
  fetched, at a release, and at the controller's stop or removal,
  whichever comes first; a reading whose controller stops meanwhile is
  freed by its reader and never published.
- **Nothing else.** No write anywhere, no AML executed or interpreted
  (the AML is copied as bytes). The release and `qemu` flavours read
  the same.

#### 12.8.15 On the NT systems as decided (draft 11)

Draft 11, 2026-11-03: the owner's answers to Q15 and Q16, the owner's
goal (the handover on every system the package serves), a reading of
what the firmware table interface gives on 10 x64 (12.8.15.2), and a
review of the plan. Not converged, and nothing of it coded: the bench
reads first whether the handover is the NT cause at all (12.8.15.9).
12.8.13.4 to 12.8.13.8 stand where this section does not replace them.

##### 12.8.15.1 The decisions, and the bench line

- **The bench** (owner, 2026-11-03): Windows 10 x64 22H2 (build 19045),
  CSM, Above 4G Decoding on, `3.4.0.0`: `xhcicm.sys` bound the ASM4242
  NHI (`1B21:2425`, problem 0) and its USB4 start failed with inert
  reason 12 (host router unanswered); ring 0's first control read went
  out and was never answered (`tbt.ring.tx-stuck`). On 98 SE before the
  handover ring 0 took the request and nothing answered it; on 10 the
  transmit itself is reported stuck. The same cause is plausible, not
  established: the test image of 12.8.15.9 reads it on the bench.
  It did (below).
- **Q15**: B first, then C, both in `3.4.1.0`; B on Windows 10 version
  2004 and later, x86 too (no kit is needed, 12.8.15.2, so the x64-only
  reason is gone); then D, for 2000 and 32-bit XP, which neither B nor C
  reaches. The goal: the handover on every system `xhci98.sys` serves.
- **Q16**: (a), every wake that follows a hibernate request treated as
  S4 and sent again.
- **The tables' source** under B (owner): the documented user-mode
  interface, no new kit (no `AuxKlibGetSystemFirmwareTable`).
- The tiers, `XhciCmAsm4242Structural` and `XhciCmAsm4242Handover`
  stay as merged; 98 SE and ME do not change.

**The test image on the bench** (owner, 2026-11-04): the same board
and system, the image of 12.8.15.9 in place of `3.4.0.0`'s
`xhcicm.sys`: the handover sent at the NHI's start (accepted within
1 ms, replied at once), reply `100h`, the query `000F0100h`, the router
answering (`u4.host.id=174C2428`), the enclosure's PCIe switch
enumerated behind the two tunnel ports and its drive found, and two
replugs survived. Before it, on the same start, the mailbox's command
dword read `00400094h` (`cm.osc.mb.before`): the firmware's own `_PS0`
command of the NHI (function `0040h`, command `0194h`, 12.8.13.10),
accepted just before the driver's write. So the handover is the NT
cause on this board, and the firmware's use of the same mailbox during
the system's start is observed, not only read from its tables: the
reading behind Q18's filters (12.8.15.7).

##### 12.8.15.2 What the interface gives, read

Read on the 10 x64 q35 bases, 2026-11-03 (`runs/run-46.md`, "46.12 on
NT, B and C", "The interface, read"), by a test program calling `EnumSystemFirmwareTables` and
`GetSystemFirmwareTable` (documented from XP x64, Server 2003 SP1 and
Vista [ms-fw]):

| Boot | `ACPI` listed | asked by name | `FIRM` |
|---|---|---|---|
| legacy BIOS boot | `MCFG`, `FACP`, `APIC`, `WAET`, `HPET` | `RSDT` given (not listed); `XSDT` and `RSD ` not | `C0000h` and `E0000h`, 128 KB each; the root pointer in the second |
| UEFI boot | the same and `BGRT` | `XSDT` given (not listed); `RSDT` and `RSD ` not | none |

With 46.21's readings on 7 and 10 (`runs/run-46.md`, "46.21"): one SSDT
of each signature, the first. So:

- **The root table** (its entries, every table's physical address) is
  given by name on both boots; its own address is not, and the root
  pointer only through `FIRM` on a BIOS boot.
- **The three named SSDTs** are not given (they are not the first), so
  they are reached only by physical reads at the root table's entries,
  which only the driver can make. User mode can therefore not decide the
  tier, nor place `\CPNV` (declared in `CPMCMN`).
- **The data byte** is in no table: it is in firmware memory `\M620`
  (a field of `\CPNV`) points to (12.8.1).

##### 12.8.15.3 What a review of the plan raised, and the answers

- **The memory proof.** That no byte of a page lies in a range
  `MmGetPhysicalMemoryRangesEx2` reports shows the system does not manage
  the page as memory; it does not show the page is firmware memory (a
  device's registers or a hole would pass). Answer: the proof is kept as
  a refusal (a page the system manages is never read) and is no longer
  called a proof of firmware memory. An address is taken only from the
  firmware's own pointer chain: the root table's entries, `\CPNV`'s
  literal address in the AML, and the two pointers the firmware's own
  `\M049` dereferences. That the system followed the same chain (that
  the root pointer found is the one the system used) is not established
  by any documented interface; that a firmware's pointer names its own
  memory is the assumption 12.8.3 step 6 states. Both stay risks.
- **The cache type.** No interface says how the system maps those pages
  (the table loader, or the interpreter's operation regions), and a
  mapping of another cache type has undefined results however short or
  read-only it is. Answer: no documented route establishes it, so it is
  not a residual this design can mitigate: it is the owner's question
  Q20, with a route that reads no firmware memory at all (E, 12.8.15.11)
  beside B, C and D.
- **The record's freshness.** A record written by user mode can name
  addresses of an earlier boot (a firmware update need not change its
  version), and a logon refresh runs after PnP starts the NHI. Answer:
  12.8.15.4 drops the persistent record for addresses: on a BIOS boot
  the driver finds the root pointer itself each boot, in the area the
  system's own `FIRM` provider serves; on a UEFI boot B declines and C
  serves. A current-boot delivery (the query door at logon, the start
  retried) is Q17.
- **The mailbox's other owners.** The firmware's `_PS0` and `_PS3` of
  seven functions use the mailbox under a mutex of its own (12.8.13.10).
  Answer: 12.8.15.7, the exchange excluded from those functions' power
  and PnP transitions by filters on their stacks, with its residuals.
- **The filter's lifetime.** Answer: 12.8.15.7 holds the entry, the
  serialisation and the power state across the whole exchange, finds the
  path by the filters' own topology, and says when the filter becomes
  active after an install.
- **Q16 (a) and idempotence.** Answer: the legs of 12.8.15.8 add fast
  startup, an aborted sleep, and an NHI disabled across S4; whether a
  second handover after a hybrid sleep's S3 wake disturbs a live tunnel
  is read on the emulator and on the bench.
- **No documented interface completes B.** Agreed (12.8.15.2).
- **B and C together.** Answer: C serves only where B's source is absent
  (a UEFI boot; no root pointer; before 10 version 2004; the
  interface absent), never after B refused for any other reason, nor
  after any mailbox access; both share one once-per-boot entry and one
  S4 obligation (12.8.13.5, 12.8.13.6).
- **Facts.** The capability bits the system asks for do not gate the
  firmware's commands (12.8.13); the 10 x64 symptom differs from 98
  SE's (12.8.15.1); a UEFI firmware may also keep a root pointer in the
  BIOS area, but the `FIRM` provider serves nothing on a UEFI boot
  (12.8.15.2). B tells the boot apart first (12.8.15.4) and scans
  nothing on a UEFI boot.

##### 12.8.15.4 B: Windows 10 version 2004 and later, x86 and x64

B, C and D read the firmware's tables and data, so each runs only for
an unknown board (12.8.15.11) and only with `XhciCmAsm4242Reads` 1
(12.8.15.10, Q20); with 0 they read nothing and send nothing.

- **Admitted**: `MmGetPhysicalMemoryRangesEx2` found by
  `MmGetSystemRoutineAddress` (x64: imported by `xhcicm.sys`, `SITES`
  one object; x86: through the helper's channel, a new minor of
  `xhcihlpr.sys`'s query handing out `MmGetSystemRoutineAddress`, since
  the x86 image also loads on 98 SE and ME and imports nothing they
  lack); its absence is "not 10 version 2004", C's case. **A UEFI
  boot** (`ExGetFirmwareEnvironmentVariable` is documented to return
  `STATUS_NOT_IMPLEMENTED` on a legacy BIOS boot; any other status is
  taken as UEFI): nothing scanned, C's case. **Windows 11** (build 22000
  and later, from `RtlGetVersion`, documented, looked up as the other
  routines are): nothing sent by any route, B, C, D or E, refused
  (`108h`) before any table, memory or mailbox access; the firmware's own
  `_OSC` sends it there, its `_OSI` condition met. A later Windows that
  answers the board's `_OSI` string is the same case.
- **The root pointer**: 12.8.3 step 2's scan of `E0000h` to `FFFFFh`
  alone (the extended BIOS data area is memory the system manages and is
  not read), each page refused when it lies in a range Ex2 reports.
  None found: B's source is absent (C's case).
- **Then 12.8.3 steps 2 to 6** as on 98 SE and ME, through the same pure
  code, every read held to step 2's bounds and refused for a page in a
  range Ex2 reports (one snapshot taken per attempt, freed after).
- **The mailbox** through the upstream port's own stack (12.8.13.4,
  12.8.15.7), never ECAM; `MCFG` read only for step 2's bound.
- **The user-mode record**: none for addresses (12.8.15.3). The
  installer and XHCITR need write nothing for B. This departs from the
  owner's choice of the user-mode interface as B's source, because that
  interface does not give the three SSDTs (12.8.15.2): Q17 asks the
  owner to confirm it.

##### 12.8.15.5 C: XP x64, Server 2003 SP1, Vista and later, before B

The native query (`ZwQuerySystemInformation`, class 76,
`SYSTEM_FIRMWARE_TABLE_INFORMATION`, which WDK 7.1 declares; not
documented for drivers): the root table by name, as the user-mode
interface it serves gives it (12.8.15.2), its entries the SSDTs'
addresses; `MCFG` by name. No root pointer is needed. Before 10 version
2004, the refusal of system-managed pages uses `MmGetPhysicalMemoryRanges`
(declared, reserved for system use). On x86 both are found through the
helper's channel. On 10 version 2004 and later C serves where B's
source is absent (a UEFI boot), with Ex2 for the refusal. The risks: B's,
and two routines Microsoft does not document for drivers.

##### 12.8.15.6 D: Windows 2000 and 32-bit XP

Neither the class 76 query (XP x64 and Server 2003 SP1 on) nor the
user-mode interface exists there. The root table: on XP the
`HKLM\HARDWARE\ACPI\RSDT` value ACPI.SYS writes (the RSDT's bytes, not
documented), read in the driver; else (2000, where the key is to be read
on the `w2kacpi` base) 12.8.15.4's scan of the BIOS area. The refusal of
system-managed pages: `MmGetPhysicalMemoryRanges`, looked up through the
helper. 2000 on the Standard PC computer type (no ACPI driver, issue 19):
the firmware's tables are touched by no ACPI interpreter, as on 98 SE;
whether the memory manager reuses the ACPI reclaim range there is not
read, so the refusal of system-managed pages stands there too. The risks:
C's, and a registry value of the system's own that no document defines.

A second AMD platform's reading with ACPI off (owner, 2026-11-04)
bears on D and on any system without ACPI: its host routers answered
and a device attached at power-on worked, the firmware's own PCIe
tunnel in place before the system started, but no port reported a hot
plug after the start, and the hosts had no INTx route (MSI-X only), so
a system that needs a line interrupt has none there.

##### 12.8.15.7 The mailbox, excluded from the firmware's own use

The firmware's `_PS0` and `_PS3` of the upstream port, the four
downstream ports, the switch's xHCI and the NHI send their own commands
through `\_SB.M625` (12.8.13.10), whenever the system moves one of them
between D0 and D3: at their starts, stops and removals, at every sleep
and resume, and at a runtime D3 (the xHCI idle under Windows' own
driver; the ports, whose `_DSD` allows hot plug in D3). The system runs
those methods in each function's own power and PnP requests, as the
ACPI filter below that function's driver. A filter above the function's
driver sees each of those requests before the ACPI filter does, so:

- filters on all seven: `xhcicmf` on the four downstream ports (as
  today) and on the upstream port (12.8.13.4), a filter on the xHCI
  (`PCI\VEN_1B21&DEV_2426`, its device-specific `UpperFilters`, the
  owner's to allow: Q18), and the NHI's own function driver. The xHCI's
  entry is written by the installer, not by an INF (decided 2026-11-04,
  on the owner's delegation): on 8 to 10 Windows' own `usbxhci` can own
  that function, which no INF of this package reaches, and an INF route
  through `xhci98.inf` would tie the controller's package to the
  connection manager's, a devnode whose `UpperFilters` names a service
  that is absent failing its start. It is not written on Windows 11
  (build 22000 and later), where nothing is sent (12.8.15.4) and the
  filter would add risk alone. The installer's part is 12.8.15.12;
- **coverage is required**: every one of the six other functions has a
  filter started on its stack and registered on the mailbox list (sized
  for seven functions of each of 12.8.2's four hosts, 28 entries, each
  keyed by its location and its host's upstream port; a full list is a
  registration refused, logged), or
  nothing is sent (refused, `10Dh`); a missing, failed or not yet active
  filter is a refusal, never an unobserved stack;
- **what the barrier tracks**: on the six, device power requests
  (`IRP_MN_SET_POWER` of type device) and start, stop, query-stop,
  query-remove and removal requests, from their arrival at the filter
  until they complete back up; not wait/wake, query-power or system power
  requests. A query-stop or query-remove that succeeds below closes
  admission for that member; its cancel reopens it only for a member
  still started; a stop withdraws the member until a start completes
  successfully below, and a removal withdraws its registration for good
  (a new start registers again). A withdrawn member is a coverage
  refusal (`10Dh`). On the NHI, the initiator, the exchange runs in its own
  start's or D0's work after the lower drivers completed it, which the
  barrier admits: it never waits on the initiating request;
- **acquisition**: an exchange starts only once none of the six has a
  tracked request in flight, waiting at most 1000 ms (else refused,
  `10Eh`); while it runs each filter queues any new tracked request and
  passes it down when the exchange ends; the upstream port's D0 is
  checked under the same hold;
- **surprise removal** of any of the seven is never queued: it closes
  admission at once, the exchange stops before its next mailbox access
  (failed, logged), the filter passes the request down, and the
  interface reference is dropped once no access holds it;
- **a timeout**: the hold is kept up to 1000 ms more while the reply
  dword is polled; a reply that comes is cleared (written 0), and the
  hold is then released either way. The firmware's sender writes its
  command without first testing the mailbox and takes the first reply
  it sees (12.8.13.10), so a reply of ours arriving later still could be
  taken as the answer to its command, and its command could overwrite
  ours: no abort is documented for the mailbox. The board's callers of
  the sender in `_PS0` and `_PS3` discard its result (12.8.13.10), so a
  stale reply changes no firmware decision there; what the switch does
  with a power notice that met our command is not known. This stays a
  residual of every route, accepted by the owner (Q21 (a), 12.8.15.11):
  the exchange is not retried after a timeout, the obligation consumed;
- the entry, the hold and the reference on the upstream port's
  configuration interface (`BUS_INTERFACE_STANDARD`, taken at its start)
  are held across the whole exchange, not per dword;
- the path is found by the filters' own topology (each port's bus and
  secondary bus through its own interface), the root port read once by
  its location, no inventory of every bus;
- an installed filter is active only once its port's stack is built
  again: at the restart the install asks for.

That the firmware uses the mailbox while the system starts its
functions is a bench reading, not only an inference (12.8.15.1: the
NHI's own `_PS0` command found in the mailbox just before the test
image's write).

Residuals, stated: that the system runs `_PS0` and `_PS3` only inside
those requests is how the power model is documented for an ACPI filter,
not a promise about every path; a held power request delays the
system's transition; the root port's own methods (power to the whole
switch, no mailbox) are not excluded (no filter on a root port); the
late reply after a timeout (above).

##### 12.8.15.8 Once per boot, S4, and the legs added

12.8.13.5 and 12.8.13.6 as drafted, Q16 (a). Legs added to 12.8.13.8:
fast startup's return; a sleep request that fails; a hybrid sleep woken
from S3 with a tunnelled device attached (the device still working
after the second handover); an NHI disabled across S4 (nothing sent,
logged); a UEFI start under B (declined, C serving); 11 (nothing sent,
`108h`, by each route). For 12.8.15.7: a member filter absent (`10Dh`);
a member's power request held past 1000 ms before the exchange
(`10Eh`); a member's D3 request arriving during the exchange (queued,
then passed); a surprise removal during the exchange (stopped before
the next access); a query-remove that succeeds (admission closed); a
timeout with a reply after it (cleared within the extra wait) and with
none, each with a member's power request queued behind it; the list
full; a member stopped and started again, and a query-remove
cancelled. For E: the profile matched (sent by default; nothing with
`XhciCmAsm4242Handover` 0); one identity field differing (unknown:
nothing with `XhciCmAsm4242Reads` 0, the read path with 1); the WMI
block absent (unknown); an S4 resume (sent again, matched again). For
the installer (12.8.15.12): the reads row shown on NT and greyed on 98
SE and ME, set to yes (written 1 machine-wide), the structural row on
NT, `/set` on NT and refused on 98 SE, an update with a saved 1 kept by
`/yes`, the uninstall removing both copies, 32-bit XP's WMI route; the
report's line for an unknown board at reads 0. All on the emulator,
its mailbox model given the faults.

##### 12.8.15.9 The bench test image (approved by the owner)

Before any of B, C or D is coded: a test image for the owner's 10 x64
board, never merged (as T5 for 98 SE, issue 21): the 98 SE send path
enabled for amd64, discovery bypassed, the ECAM base and the command's
data byte built in from an untracked header, sent at the NHI's start
before ring 0, nothing on build 22000 and later, the reply and the
router's answer in the log XHCITR's report carries. It reads whether
the handover is the NT cause. It has none of 12.8.15.7's exclusion and
writes through ECAM, which 12.8.13.1 forbids the product: a collision
with the firmware's own use of the mailbox, or a system whose
virtualization-based security restricts configuration access, can stop
it; the bench steps say so.

##### 12.8.15.10 The owner's decisions on Q17 to Q20 (2026-11-04)

- **Q17: (a).** On a UEFI boot B steps aside (logged) and C serves
  Windows 10 version 2004 and later. B's reading of the three SSDTs in
  the driver, from a root pointer it finds itself, stands.
- **Q18: (a).** A filter on the ASM4242's xHCI (`PCI\VEN_1B21&DEV_2426`),
  so all seven functions are covered (12.8.15.7).
- **Q19: on.** The NT handover is on by default in `3.4.1.0`:
  `XhciCmAsm4242Handover` 1 on NT as on 98 SE and ME, the master switch
  for every route ("we can recut if the bench says otherwise"; the bench
  is the owner's board on 10 x64).
- **The read path off by default on every NT route** (owner,
  2026-11-04): B, C and D alike, `XhciCmAsm4242Reads` 0.
- **Q20: E by default, the reads by opt-in.** A known board (12.8.15.11)
  is sent its built-in profile's command with no firmware memory read,
  on by default; an unknown board's ASM4242 goes through B, C or D's
  table and data reads only when the user opts in, a new value
  `XhciCmAsm4242Reads` (12.8.15.12), with the cache-type risk stated.
  The exact and structural tiers (12.8.3 step 5, 12.8.10) apply to that
  read path alone, `XhciCmAsm4242Structural` keeping its meaning there.

The design below follows these; 12.8.15.11's earlier form (a user-mode
sender and a deferred start) is withdrawn.

##### 12.8.15.11 E: a known board's command, no firmware memory read

**The identity** (`CmOscpMatch` (designed as `CmOscProfileMatch`), one pure function in
`src\cm\cm_oscp.c`, compiled into `xhcicm.sys` and into the installer,
to be host-tested, with the SMBIOS decoder below shared by both): a
board is known when all of these equal one profile's:

- the host's three functions as found by bus (12.8.3 step 1): the
  upstream port, the downstream port above the NHI and the NHI, their
  vendor and device ids, and the NHI's subsystem ids (its dword `2Ch`,
  where a bridge's subsystem ids sit in a capability of its own);
- the root port above them: its vendor and device ids;
- the SMBIOS baseboard (type 2) manufacturer and product strings, held
  in the profile as a 32-bit FNV-1a hash of each (no board name in the
  source), and the BIOS vendor (type 0) the same way.

The BIOS version and date are logged, not matched: a firmware update on
the same board keeps its profile. **The profile** carries the command's
data byte (the board's, read on its bench) and nothing else; the
command is `0102h | (data << 16)` and the query `0180h`, as 12.8.1.
The profiles are those read on a bench: the owner's board alone in
`3.4.1.0`.

**Where the driver gets SMBIOS**: the system's own SMBIOS data through
the documented WMI kernel interface (`IoWMIOpenBlock` and
`IoWMIQueryAllData` on the `MSSmBios_RawSMBiosTables` block,
`{8F680850-A584-11D1-BF38-00A0C9062910}` as the Windows 2000 DDK's
`wmidata.h` declares it), from XP on; looked up at run
time (x86: through the helper's channel, as B's routines). No firmware
memory is mapped. The answer is decoded with bounds:

- the buffer at most 64 KB, asked once for its size and once more
  (no other retry); each `WNODE_ALL_DATA` of the chain checked (its
  size inside the buffer, its next-offset forward and inside, its
  instance count, offsets or fixed size inside its node);
- exactly one instance in the whole chain, else refused (ambiguous);
- the instance's data: `Used20CallingMethod`, major, minor, DMI
  revision, a 32-bit length, then that many table bytes, the length
  inside the instance;
- the SMBIOS walk (`CmOscpSmbios` (designed as `CmOscSmbios`), pure, shared with the installer):
  each structure's formatted length at least 4 and inside the tables,
  every field read (type 0: vendor at `04h`, version at `05h`, date at
  `08h`; type 2: manufacturer at `04h`, product at `05h`) inside that
  structure's formatted length, else the structure is refused; its
  string set ended by a double NUL inside them; type 0 and type 2
  each exactly once (a second of either refused); a string index 0 or
  past the set refused; each string at most 64 bytes, hashed as its
  bytes with leading and trailing spaces removed (FNV-1a, 32 bits),
  case kept.

Absent, failing, malformed, ambiguous or not ready at the NHI's start:
the board is unknown (logged with the reason), and the read path's
opt-in decides. Host vectors: a known identity, each field differing, a
second instance, a second type 2, a bad string index, an unterminated
set, a length past the end, a type 0 or type 2 whose formatted length
stops short of a field read.

**What E does not check**, stated as its risk: the three SSDTs that
carry the handover's code and the firmware data the byte comes from are
never read. A board with the same identity whose firmware's handover
or data byte differs is sent the profile's command. A second AMD
platform's tables (2026-11-03) hold AMD's reference `\_SB._OSC` with
the same handover logic as the board's in other bytes (its method code
1309 bytes against the board's 1331), so the code is common to AMD's
platforms, and the profile's byte is the board's own.

**E's sequence**: first the common gates (the ASM4242's ids under the
USB4 profile, the master switch `XhciCmAsm4242Handover`, the Windows 11
refusal), before any topology, WMI or table access; then 12.8.13.4's
with the tables and data replaced by the match: the host by bus, the
match, 12.8.15.7's coverage and barrier, the once-per-boot entry
(12.8.13.5), the exchange, and the S4 obligation of 12.8.13.6 (Q16
(a)), the match made again at each send. A refusal of E for any reason
but "unknown board" sends nothing by any route; an unknown board falls
to the read path only with its opt-in. E serves every NT system from
XP on that has the WMI block; 2000 has no route but D.

**Q21, the mailbox after a timeout: (a)** (owner, 2026-11-04). The
risk of 12.8.15.7 is accepted as stated: the exchange is bounded and
never retried after a timeout; the timeout is logged (`cm.osc.timeout`,
the step and the last values read) and shown in the status record
(12.8.15.13).

**The boot guard** (owner, 2026-11-04): every NT route (E and the read
path) skips the handover once after a start that sent it and never
finished, 12.8.15.13.

##### 12.8.15.12 The installer's part (for record 26 section 5.3.5)

Written so that the installer can be built from this section alone;
98 SE and ME keep 12.8.11's question unchanged.

- **The value**: `XhciCmAsm4242Reads` (REG_DWORD; 0 the default, 1
  lets B, C or D read the firmware's tables and data on an unknown
  board; any other refused, its default standing), stored as every
  `xhcicm` value is (`cm_reg.c`): the INF's default in the NHI's driver
  key, written at 0 with no overwrite by both `xhcicm` INFs' NT installs
  with its INF gate row and `tuning.md` row; the installer's answer and
  `/set` machine-wide under `HKLM\System\CurrentControlSet\Services\USB`,
  which takes precedence over the driver key. Read on NT alone; 98 SE
  and ME neither read nor write it. `XhciCmAsm4242Handover` (1) and
  `XhciCmAsm4242Structural` (0) gain their NT writes in the same INF
  sections, with their rows.
- **When it is asked**, on 2000 and later, at the plan: for each present
  devnode `xhcicm.inf` matches whose hardware id is the ASM4242 NHI's
  (`PCI\VEN_1B21&DEV_2425`), Setup gathers the identity of 12.8.15.11
  itself (the three functions and the root port by the configuration
  manager's device tree and their hardware ids; the raw SMBIOS tables
  by `GetSystemFirmwareTable` `RSMB` from XP x64 and Vista, and on
  32-bit XP by WMI's same raw class, `MSSmBios_RawSMBiosTables` in the
  `root\WMI` namespace, its `SMBiosData` bytes) and calls the same
  `CmOscpMatch` (designed as `CmOscProfileMatch`), those bytes decoded by the shared `CmOscpSmbios` (designed as `CmOscSmbios`),
  so Setup's classification is the driver's; on 2000 neither has a
  route and every board is unknown in both:
  - the classification is shown in the report and the log (and, as
    built, the row's note and the Summary: "As built in the installer"
    below); the
    value itself is a row of the Thunderbolt settings (Advanced) list,
    `instValues` in `inst_core.c`, beside `XhciCmAsm4242Structural`'s,
    applying on NT alone (`INST_APPLY_NT_CM`, so greyed on 98 SE and ME as
    every system-specific row), its default 0, at its default for a
    known board as for any other (a known board is sent whatever it
    says). Its draft text, in that row's style: name "send the USB4
    handover to an ASM4242 board Setup does not recognise, reading the
    firmware's tables to find it"; note "a known board always gets it.
    This reads the firmware's own memory, which can stop Windows on some
    machines. Restart Windows after changing it."; choices "no: only to
    a known board" (0) and "yes" (1). The Thunderbolt and USB4 page has
    no box for it (owner, 2026-11-04).
- **The structural row on NT**: `XhciCmAsm4242Structural`'s row applies
  on NT too from this change (`INST_APPLY_CM`), its note saying that on
  2000 and later it matters only once the reads row is "yes": on NT it
  lets the read path send to a build recognised by its canonical form,
  and with reads 0 it changes nothing; on 98 SE and ME it keeps 12.8.11's
  meaning and question.
- **The report's line**: an unknown ASM4242 board left at reads 0 sets
  the status record's reason (bits 18:16 tier 1 with a new bit 22, "not
  sent: unknown board, reads off"), and XHCITR's report shows it as a
  plain line, draft: "Handover not sent: this board is not recognised.
  To try it, set the Thunderbolt setting for unknown ASM4242 boards to
  yes."
- **Console and switches**: the console asks the same question, as any
  asked value, its default the effective value; `/set
  XhciCmAsm4242Reads=0|1` accepted on 2000 and later and refused on 98
  SE and ME; `/set XhciCmAsm4242Structural=0|1` accepted on NT too; `/yes`
  keeps every effective value not given by `/set` (record 23's rule), a
  saved 1 included. The Settings sheet's Thunderbolt and USB4 tab shows
  both rows as the list shows them.
- **Uninstall** removes the value from both places: the driver key
  with the device, and the machine-wide copy under `Services\USB` by
  the uninstall's value table, which gains `XhciCmAsm4242Reads`, so a
  later install never inherits the permission.
- **The xHCI's filter entry** (12.8.15.7; decided 2026-11-04 on the
  owner's delegation), on 2000 and later, never on 98 SE and ME:
  - **Which devnodes**: every present devnode whose hardware ids hold
    `PCI\VEN_1B21&DEV_2426` (the ASM4242's xHCI), whatever driver owns
    it (Windows' `usbxhci`, `xhci98.sys` or another).
  - **When**: only when the run leaves the Thunderbolt part installed
    (the `xhcicm` service and `xhcicm.sys` in place, as the run's own
    step reads them after its install), and never on Windows 11 (build
    22000 and later), which Setup already refuses before any work
    (`inst_os_kind`). An entry an earlier run wrote on 10 that a later
    upgrade to 11 carries over stays, since Setup does nothing there: the
    driver's member role is then a filter that passes every request and
    holds none (no exchange can run on 11, 12.8.15.4).
  - **What**: `xhcicm` appended to the hardware key's `UpperFilters`
    (`SPDRP_UPPERFILTERS`), every name already there kept in its order,
    and nothing written when it is already listed: `inst_msz_append`, as
    `pfw_rootbus_filters` does for `xhcipfw` on the root buses. A value
    that cannot be read (other than absent) or would be too long is a
    failure of that devnode, said and logged, the others still done.
  - **Not a tunnel port, nor a connection-manager devnode**: the devnodes
    this entry is on are told apart by their hardware id
    (`PCI\VEN_1B21&DEV_2426`) and are kept out of the tunnel-port
    passes (`is_filter_host`, `unfilter_nt`: never reinstalled, never a
    blocked removal) and out of the connection manager's version group
    (`group_of`, `group_verdicts`): their version is their function
    driver's (Windows' own or `xhci98.sys`), never compared with this
    package's for the `xhcicm` part, whose version is the NHI's and the
    tunnel ports'.
  - **Taken out**: by the uninstall, as its first step on NT, before
    the tunnel-port pass (whose inventory is read after it), and by an
    install run that leaves the Thunderbolt part out, from every devnode
    present or not
    (`DIGCF_PRESENT` off), any entry naming `xhcicm`, the other names
    kept in order and the value deleted when it ends empty
    (`inst_msz_remove`); and always before the `xhcicm` service or image
    is removed or replaced by a run that removes them, since an entry
    naming an absent service fails that devnode's start. A devnode that
    cannot be read while taking out fails the step, so the service and
    image are then left in place, said.
  - **Restart**: a devnode changed asks for the run's restart (the
    filter attaches when the xHCI's stack is next built, 12.8.15.7), as
    the root bus entries do; the summary and the report list each
    devnode changed (`xhcicm: added to / taken out of the UpperFilters
    of <instance>` in the log).
  - **The upstream port's model line** (12.8.13.4): both filter INFs gain
    `PCI\VEN_1B21&DEV_2421&CC_0604`, and `instTbPortIds` in
    `inst_core.c` gains `1B21:2421` in the INF's order (the host test
    compares the two), in the driver's change, so the forced install
    reaches the port as it reaches the downstream ports.
- **Text**: the rows' and the report line's wording are the owner's to
  rephrase (task 46.18's rule); no "experimental" in Help.

- **As built in the installer** (2026-11-04, the owner's decisions of
  that day, which revise the bullets above where they differ):
  - **The identity** is read by `xhciinst\inst_board.c`, which links the
    driver's own `src\cm\cm_oscp.c` (`CmOscpSmbios`, `CmOscpMatch`): the
    NHI's parents by the configuration manager's `CM_Get_Parent` (the
    downstream port, the upstream port, the root port), each function's
    vendor and device from its instance id, the NHI's subsystem from its
    `SUBSYS_`, which is its dword `2Ch`. The installer compiles
    `cm_oscp.c` with `CM_OSCP_RELEASE`, so it holds the release driver's
    profiles alone, never the qemu flavour's emulator profile (the host
    suite `test_cm_oscp_rel` and the tool's `/selftest` hold it).
  - **The tables**: `GetSystemFirmwareTable` `'RSMB'` where it exists (XP
    x64, Server 2003 SP1, Vista and later), at once; on 2000 and 32-bit XP
    WMI's `MSSmBios_RawSMBiosTables` in `root\WMI` (its `SMBiosData`),
    through COM loaded at run time (nothing imported, so Setup still
    starts on 98 SE and ME), on a thread of its own, at most 10 s. Either
    is asked only with the Thunderbolt support in the run (its box
    ticked, or `/thunderbolt`) and an ASM4242 host listed (its NHI or its
    xHCI); never otherwise, and then no line is shown.
  - **When they are read**: never by a plan, whose Thunderbolt choice
    may be a default not yet shown; the window at the Thunderbolt and USB4
    page's Next (the quick read at once; WMI's with a small modal box
    counting down from 10 s with Skip meanwhile),
    the wizard going on at the answer, at 0 or at Skip; Back and Next
    again reuse the answer. The console, once its Thunderbolt question is
    answered yes, says it reads the identity and waits at most 10 s. A
    timeout, Skip, a failure or WMI absent (the class may not exist on
    2000) leave the board **not checked**, the log giving the reason:
    the driver reads by its own route (WMI's kernel interface from XP
    on, and on 32-bit NT the helper's channel, 12.8.15.15) and may still
    send to a known board, so Setup says it could not check, never that
    the board is unknown.
  - **The classes and where they are said**: known (a profile matched),
    not recognised (the tables read whole and refused by the decode or
    matched by none), not checked (a parent's id or the tables not
    read). The plan's report prints the class under the NHI, and again at
    the run's start when it differs; the log has the identity read and
    the reason; the `XhciCmAsm4242Reads` row's note in the Thunderbolt
    settings ends with it ("known: the handover is sent without this
    setting", "not recognised", "could not check it", and before the
    page's Next "checked when you press Next"); the
    Summary lists it with the components.
  - **The filter entry's devnodes before the go-ahead**: the Summary
    (install and uninstall) and the console's plan (and its uninstall
    warning) list each `PCI\VEN_1B21&DEV_2426` devnode whose
    `UpperFilters` the run will change, under "Setup will add the xhcicm
    filter to" or "take it out of", read by a pass of the same
    enumeration that writes nothing, the decision the run's own
    (`inst_cm_xhci_mode`: none on 98 SE and ME, on Windows 11 and for
    the tools alone; taken out with the support not in place after the
    run; left in place but not this run's; else added).
  - **The structural row on NT**: its note says that on 2000 and later
    it counts only when unknown ASM4242 boards are set to yes; it is
    shown on 2000 to 10 (`INST_APPLY_CM`) once both `xhcicm` INFs write
    it on every system with the read path (its own change); until then it
    keeps `INST_APPLY_9X_CM`.
  - **The line follows what the driver will do**, per system
    (2026-11-04, the coordinator on the owner's delegation, read against
    the driver of `45cf9e30`): on 2000 the driver's route E finds no WMI
    routines and reads no identity (an unknown board, 12.8.15.17), so
    Setup reads none either and says it could not check, never that
    the handover is sent; on 32-bit XP the x86 image finds
    `IoWMIOpenBlock` and `IoWMIQueryAllData` through the helper's
    channel (12.8.15.15) and sends to a known board, so Setup's WMI read
    and its "known" line stand; from XP x64 and Vista on as above.
    On every 32-bit NT system the x86 image's routines come from the
    helper `xhcihlpr.sys`, which the xHCI98 USB drivers install
    (`xhci98.inf`), so a known board is said to be sent only where the
    run installs a controller with them or the helper's service and image
    are in place at this package's version or later (the first with the
    routine service); else "known; the handover needs the xHCI98 USB
    drivers here" (`inst_board_sendable`, reviewed and revised).
    **Revised** (owner, 2026-11-04; record 18 section 15.15): the
    Thunderbolt part installs the helper too (`xhcicm.inf`), so the run
    installing an NHI with it counts as installing the helper; the line
    for the rest (an NHI the run leaves as it is, the helper absent or
    older) reads "known; the handover needs the helper xhcihlpr, not in
    place" (a draft). A run counts as installing the helper only when the
    device's INF is not held back by the staging (`cm_inf_held`: a stored
    build the run may not replace, a store not read), and a helper in place
    counts only at 3.4.1.0 or later (`inst_board_hlpr_cmp`), the first with
    the routine service: 3.4.0.0's answers minor 0 alone and shares this
    tree's version stamp until the bump, so the package's own version is
    no measure. An older helper on the disk is replaced by the run's INF, a
    newer kept (record 18 section 15.15).
  - **Texts**: drafts for the owner (task 46.18's rule).

##### 12.8.15.13 The boot guard (NT)

The owner's guard against a start that hangs or stops the system after
the handover (2026-11-04). On NT alone, every route (E, and B, C and D
under `XhciCmAsm4242Reads`); 98 SE and ME unchanged.

- **The marker**: `XhciCmOscPending` (REG_DWORD, the NHI's hardware key,
  `Device Parameters`, where the status record is written,
  `cm_statos.c`, `ZwSetValueKey`'s one site): not a value any INF
  writes, a state of the driver's own, like `XhciCmStatus`. Its value
  is the send's kind (1 a start's send, 2 an S4 resume's).
- **Before a send**: once the route's checks have passed and before
  12.8.15.7's barrier is acquired, the marker is written and its key
  flushed (`ZwFlushKey`, so a stop within seconds still finds it; never
  inside the barrier, whose hold is bounded); a write or flush that
  fails sends nothing (refused, `10Fh`, logged). The checks that the
  barrier guards (the mailbox clean, the port in D0, coverage) are made
  again under it; a refusal there leaves the marker armed and unsent,
  which the next cold boot treats as a send (one conservative skip).
- **What clears it, "the start succeeded"**: the NHI's USB4 start that
  followed the send reaching the host router (its router id read on
  ring 0, `u4.host.id`), whatever happens after; or an orderly system
  shutdown or restart after the exchange ended (the NHI's set-power to
  a system shutdown state). A clear is a delete of the value and a
  flush, made on the NHI's thread outside any barrier. A delete that
  fails leaves the marker (the next cold boot skips once more). A
  delete that succeeds whose flush fails leaves the marker's state on
  disk unknown: the driver keeps a guard latch for the rest of the boot
  (no further send by any route, the S4 resend included), writes the
  marker back and flushes again, and logs which of these succeeded; the
  record says the persistence is unknown unless that rewrite and flush
  both succeeded. A start that ends inert (the router silent
  after the handover) keeps the marker. A disable and enable within
  the boot neither reads nor clears it: the once-per-boot entry
  (12.8.13.5) already decides there, and the marker is a matter
  between boots.
- **At the next boot's first start** (the once-per-boot entry free,
  before any route's work): the marker present means the last boot's
  send never finished; the handover is skipped for this boot (logged
  `cm.osc.guard` with the marker's kind, the status record's guard bit
  set; the start goes on without it, and the once-per-boot entry is
  reserved so no restart of the NHI in this boot sends), and the
  marker is deleted and flushed before the start goes on, so the boot
  after that tries again; a delete that fails is logged and the marker
  stays (the next boot skips again); a flush that fails after the delete
  leaves its state on disk unknown (logged), which the skip of this boot
  does not depend on. A failure to read the
  marker skips too (logged), never sends. An S4 resume's send (12.8.13.6)
  writes the marker as a start's does and is cleared the same way; a
  resume finding a marker of the same boot skips that resend.
- **The status record** (record 24 section 3): bit 20 set when this
  start skipped by the guard, bit 21 when an exchange timed out this
  boot (Q21); on NT bits 18:16 also take 4, a known board's profile (E),
  and bit 19 means what it means on 98 SE and ME; a reader that knows
  none of these reads 0; the version stays 1.
- **Host vectors** (the guard's pure decision, the `CmOscg*` routines (designed as `CmOscGuard`)): no marker
  (send allowed, marker to write); a marker of each kind (skip, clear);
  a read failure (skip); a write or flush failure (no send); the router
  answering (clear); an inert start (keep); an inert start then a
  disable and enable (kept, nothing sent); an orderly system shutdown or
  restart after the exchange (clear); a delete that succeeds with a flush
  that fails (the latch: no S4 resend, the marker written back); an S4 resume's send then a hang (skip at the next
  start); two hangs in a row (skip, send, skip).
- **Legs**: the emulator's mailbox model made to stall the router after
  the reply, then the guest reset by QEMU with no shutdown (an unclean
  boot): the next boot skips and logs, the one after sends; a normal
  boot and an orderly restart clear; a disable and enable in one boot
  sends nothing and leaves the marker as it was.

##### 12.8.15.14 E's driver half, as built (2026-11-04)

`src\cm\cm_oscnt.c` (with the pure `cm_oscp.c`), reviewed and revised.
What it settles that the sections above leave open:

- **Systems.** The amd64 image finds `RtlGetVersion`, `IoWMIOpenBlock` and
  `IoWMIQueryAllData` by `MmGetSystemRoutineAddress` and imports
  `ZwFlushKey` and `ZwDeleteValueKey` for the marker (`SITES` one object
  each). The x86 image loads on 98 SE and ME and imports none of them: it
  finds all five through the helper's channel (12.8.15.15, built since),
  and refuses on NT with `108h` (`cm.osc.nt.route` the reason, status bit
  23) where the helper is absent or answers without the routine.
- **The read path.** B, C and D were not in this image: an unknown board
  with `XhciCmAsm4242Reads` 1 was refused (`110h`), with 0 reported
  (bit 22, the report's line). Built since (12.8.15.17), with
  `XhciCmAsm4242Structural`'s NT read and writes.
- **The refusals**, logged as `cm.osc.refused` beside 98's `100h` to
  `107h` (which keep their meanings: the host by bus, the port's D0, the
  mailbox not clean): `108h` the system (Windows 11, no routine route),
  `109h` the first mailbox read through the route failed or read all
  ones, `10Ah` no started upstream port member holding its interface,
  `10Bh` the route's dword 0 or `18h` differs from what the HAL read,
  `10Ch` the once-per-boot entry or object, `10Dh` coverage
  (`cm.osc.member.missing` the role and location), `10Eh` a member's
  request still in flight after 1000 ms, `10Fh` the marker not written and
  flushed (or the boot's latch), `110h` the read path, `111h` the host
  changed since the start's decision (12.8.15.16); `109h`, `10Bh` and `104h`
  come from the mailbox probe too (12.8.15.16), before any read or marker,
  and `112h` the route aliasing an extended offset or Windows 2000's gate
  (12.8.15.16), no mailbox write made.
- **The members.** The upstream port's and the xHCI's filters are objects
  of their own (`CM_KIND_MEMBER`, decided by `CmOscpMemberRole` before
  `CmRoleDecide`); a downstream port's member is its bridge filter's. A
  tracked request that arrives at `PASSIVE_LEVEL` while an exchange holds
  the members waits for its end; a device power request at
  `DISPATCH_LEVEL` is queued and sent at the release. A downstream port's
  tracked PnP requests are forwarded and waited for, so its state is the
  request's final answer. A removal waits until no request of its own is
  left in flight or queued, and (a member object) no device-usage
  notification below it, before the member is withdrawn and deleted; a
  notification after its removal began is refused. The exchange stops
  before its next mailbox access on a surprise removal of any member or of
  the NHI (its revocation).
- **Coverage is the whole switch**: the upstream port, exactly four
  functions on its secondary bus, each a downstream port, exactly one
  xHCI and one NHI behind them; a configuration read that fails, or any
  other set, is a refusal (`10Dh`, `cm.osc.members.unread`), never a
  smaller set covered. The xHCI's member exists only once the installer's
  `UpperFilters` entry (12.8.15.12) is on its devnode and its stack was
  built again: before that every board is refused `10Dh`.
- **The order of the starts.** Coverage is read at the NHI's start: a
  member whose filter has not started yet is a refusal (`10Dh`), and
  nothing retries it in that boot. On the board the xHCI (behind 14:02.0)
  is before the NHI (behind 14:03.0), so it is expected started; a system
  that starts them the other way round sends nothing (logged). A stated
  risk, read on the emulator's legs.
- **SMBIOS at the start.** The WMI block is asked at the NHI's start; one
  not yet registered is an unknown board (`cm.osc.wmi.open`), so nothing
  is sent that boot. A stated risk, read on the legs.
- **The guard** is read at a host's first start of the boot by this load
  alone; its skip holds for every later start of that boot. The marker is
  deleted only once the once-per-boot entry and its object are made, so a
  reload of the image skips too; when they cannot be made the marker is
  kept (`cm.osc.guard.kept`) and the next boot skips once more. A refusal
  after the marker was armed leaves it armed (12.8.15.13's conservative
  skip at the next boot).
- **The timeout** (Q21 (a)): neither an acceptance nor a reply in 1000 ms
  is a timeout (`cm.osc.timeout` 1 to 3, status bit 21); the reply dword is
  then polled 1000 ms more and cleared if a reply comes, never taken.
- **The status record**: tier 4, bits 20 to 22 as 12.8.15.13 says, and
  bit 23, not sent for want of the routines (the x86 image, 12.8.15.15)
  (`cm_status.h`); XHCITR's report shows a line for bit 22 and for bit
  23.
- **The qemu flavour** reads `XhciCmQemuOscNoClear` (1 leaves the marker
  as the router's answer finds it), for the guard's legs on the emulator,
  which has no stall of its own.

##### 12.8.15.15 The x86 image's routines on NT: the helper's channel

Decided 2026-11-04 on the owner's delegation, the owner's goal being the
handover on every system the package serves, 10 x86 included.

**Why the helper.** One x86 `xhcicm.sys` serves 98 SE, ME and every NT
role: `xhcicmf.inf` installs the same image as the filter
(`ServiceBinary=%12%\xhcicm.sys`, its own copy of the same bytes). Its
import gate refuses `MmGetSystemRoutineAddress` (98's NTKERN lacks it), so
neither role can import it, nor any of the routines E needs that 98 SE and
ME lack (`IoWMIOpenBlock`, `IoWMIQueryAllData`, `ZwFlushKey`,
`ZwDeleteValueKey`). The x86 helper, `xhcihlpr.sys` (record 18 sections 3
and 15), is NT-only and already hands `xhci98.sys` routine addresses
through `\Device\Xhci98Helper`'s query.

**Where the helper is.** `xhci98.inf` installs it, boot-start, from
`[Xhci.Dev.NTx86]` (2000 and 32-bit XP) and from `[Xhci.Dev6.NTx86]`, which
`NTx86.6.0` gives Vista and every later 32-bit system (7, 8, 8.1, 10):
every 32-bit NT on which a controller uses the package's `xhci98.sys`. **The
gap**: a 32-bit 8 to 10 system on which every xHCI controller stays on
Windows' own `usbxhci` has no helper; there E refuses (`108h`, logged,
status bit 23 and XHCITR's line). The helper's service description still
names 2000 to 7; it is not changed here. **Closed** (owner, 2026-11-04):
`xhcicm.inf` installs the same helper from its NT sections too, so the
Thunderbolt part alone brings it (design record 18 section 15.15).

**The channel, minor 1** (`xhci_hlpr.h`, record 18 section 15.3): the
block grows by one field at `34h`, `FindRoutine` (the helper's own
`MmGetSystemRoutineAddress`), a new service bit `XHCI_HLPR_SVC_ROUTINE`
(4), `XHCI_HLPR_MINOR` 1, the block `38h` bytes. Backward compatible both
ways, as the channel's rules already make it: a minor 0 caller (every
`xhci98.sys`) sends `34h` and the helper fills its fields for
`min(Size, 38h)` bytes, so it never sees the new field and its acceptance
ignores the new bit; a minor 1 caller (`xhcicm.sys`) given a minor 0
helper is answered `34h` bytes and no bit 4, and refuses (`108h`). The
helper sets bit 4 only when `Size` covers the field and its pointer is not
NULL. Nothing else of the channel changes: the device name, the I/O
control, the major, the validation and its injections (a qemu injection
of its own for the new service, `XHCI_HLPR_INJ_ROUTINE_OFF`).

**The caller** (`cm_oscnt.c`, x86 only): at its first NT use (the NHI's
start, `PASSIVE_LEVEL`), `IoGetDeviceObjectPointer` on the helper's name
(a new `xhcicm` x86 import with `SITES` the file, Windows 98 SE precedent
as `xhci98-imports.allow`'s row), one query of `38h` bytes asking bit 4,
the answer accepted as `xhci98.sys` accepts its own (`XhciHlprAccept`'s
length and identity tests, then bit 4 and a non-NULL pointer), the file
object dereferenced at once. The pointer then finds the same routines as
on amd64 (`RtlGetVersion`, the two WMI routines) and `ZwFlushKey` and
`ZwDeleteValueKey` for the marker, which `cm_statos.c` then calls through
pointers on x86. A helper absent, refusing or answering without bit 4:
`108h`, the result kept for the load (one query a load).

**Host tests**: the helper's answer to a minor 0 and a minor 1 block, a
minor 1 block to a helper without the routine, the caller's acceptance of
each (`test_msi.c`).

**As built** (2026-11-04, reviewed and revised): the block is
`XHCI_HLPR_QUERY1` (record 18 section 15.3, "Minor 1"); `xhci98.sys`
keeps sending the V2.0 block and declares minor 0. The query is made
once a load under the once-per-boot mutex, its block a static of
`cm_oscnt.c`; the device name is spelled out, `RtlInitUnicodeString`
being held to `cm_statos.obj`. `cm.osc.nt.route` logs the reason with
the refusal: 1 the helper's device not opened, 2 no IRP, 3 the channel
refused (the failed test in bits 15:8), 4 no routine service (its
verdict in bits 15:8), 5 a routine not found (any of the five);
`cm.osc.nt.route.status` the status. `ZwFlushKey` and `ZwDeleteValueKey`
reach `cm_statos.c` by `CmStatusMarkerRoutines`; without them the marker
is never flushed, and nothing is sent.

**One lookup for every caller** (2026-11-04, the coordinator's note: a
second x86 user of the route is coming): `CmRoutine(name)` in
`cm_oscnt.c`, declared in `cm.h`, returns any NT routine by its name
through the same lookup (amd64: the import; x86: the helper's, found
once a load under a mutex of its own, so a caller holding the
once-per-boot mutex may use it), NULL on 98 SE and ME and wherever the
route is absent. The handover's own routines are found through it.

**A lookup failed for want of memory** (2026-11-04, the ACPI-named source
of record 22 section 7.6.1 being a second caller, whose bridge filter can
ask before any NHI starts): the helper's query that found no IRP, or could
not open the helper's device for want of memory, is not kept; the next
caller asks again, so the earlier caller cannot leave the handover without
its routines for the load. The handover does not keep such a failure of its
own either: that start refuses (`108h`) and the next handover asks
again. Every other answer is kept as before.
The handover judges its routines, and copies the route's reason and status
for `cm.osc.nt.route`, under the once-per-boot mutex from its own lookup,
so another caller's later query cannot change what it logs.

##### 12.8.15.16 The start's activation deferred (2026-11-04, revised)

Decided 2026-11-04 on the owner's delegation, after the emulator's legs
(`runs/run-46.md`, "46.12 on NT, E"): at the NHI's start during the boot
the boot guard's marker is written but its key's flush fails, while the
same sequence at a restart of the NHI later in that boot flushes it; the
reading is that the NHI's driver is loaded in the kernel's PnP phase,
before the system hive is backed by its file (the status code read is in
the run record). A member filter that starts after the NHI is the same
shape: its member exists only later in the boot. Reviewed and revised: a
first form asked the PnP manager to restart the NHI; the revision defers
the start's own activation instead, so no second start, no PnP request
and no timer of its own are needed.

- **What defers.** At the NHI's start, E's sequence (12.8.15.11), its
  gates passed: the downstream or upstream port member not found yet
  (`101h`, `102h`); the board matched and the hardware key not flushable
  (a `ZwFlushKey` of it tried before anything is written); or coverage
  refused for a member absent or not started (`10Dh`, after the marker
  was armed: the marker is then deleted and its key flushed, outside the
  hold, and the send defers only when both succeed; a delete that fails
  is the refusal `10Dh` with the marker armed, and a delete whose flush
  fails is 12.8.15.13's latch and rewrite, the refusal too). The start
  then ends inert before any activation, the BAR mapped
  and nothing else taken: no ring, no DMA buffer, no platform step, no
  client (`cm.inert` 14, `CM_INERT_HO_DEFER`; `cm.osc.defer` the reason).
  Nothing is reserved; the once-per-boot entry stays free.
- **The wait.** While deferred the thread's wait is timed (the idle poll
  period, `XhciCmPollIdleMs`); at a pass due (one second by the thread's
  clock since the last try, not a count of passes) and only with the
  NHI in D0, not suspended, not sleeping, no stop pending and its
  hardware access unrevoked, the whole sequence of 12.8.15.11 runs again
  from its gates. When it ends (sent, or refused for a reason that does
  not defer), the same gates are read again (the try can block in WMI,
  the registry and the members' hold), and only then the deferral ends
  and the start's activation runs, from the platform step on, exactly as
  at a start (the selection, the USB4 client's activation and its bind);
  a stop pending then leaves it to the stop, a sleep or a gate not met
  ends it inert. The query device reports the wait as timed.
- **The bound.** 120 s by the thread's clock from the start: the deferral
  ends with no send for this start (`cm.osc.defer.expired`, status bit 24)
  and the activation runs (the router then silent, as with no handover);
  the once-per-boot entry stays free, so a later start of the NHI in the
  same boot (a disable and enable) tries again. A
  sleep or shutdown taken while deferred ends it the same way, the
  activation refused out of S0 as any start's is (`CM_INERT_POWER`), and
  a stop or removal ends it with the stop (the next start decides afresh).
- **The guard's semantics stay 12.8.15.13's**: a flush before the marker
  is written that fails defers with nothing written; a write that succeeds
  whose own flush then fails refuses (`10Fh`, the marker armed, the next
  boot's conservative skip), as before. The S4 resend (12.8.13.6) never
  defers.
- **The report**: status bit 24, deferred and expired; XHCITR's report, a
  draft for the owner: "Handover not sent: Windows took too long to
  start. Restart Windows to try again."

- **The board decided once a start** (decided 2026-11-04 on the owner's
  delegation, after the read path's B leg, `runs/run-46.md`, "46.12 on
  NT: the x86 route and the read path", whose deferred start read the
  firmware's tables and data 16 times in one boot). Every try still runs
  the sequence from its gates, but the board's decision is made at most
  once a start and kept on the NHI for that start's later tries,
  together with the host it was made for: the NHI's, the downstream
  port's, the upstream port's and the root port's addresses, and the
  PCI identity E matches on (the four functions' vendor and device ids
  and the NHI's subsystem, as `cmOsnHostByBus` reads them). A later try
  whose host, found by bus again, differs in any of these is refused for
  this start (`111h`, the host changed since the start's decision; never
  deferred, nothing decided again), so neither the match nor the read
  runs twice. In order, after the served check and the guard's read:
  1. E's match (12.8.15.11), once a start: a known profile is kept as a
     decision to send (its tier and data byte); an unknown board is kept
     as unknown. The match reads SMBIOS through WMI and no firmware
     memory, so it runs before the key's test: an unknown board with
     `XhciCmAsm4242Reads` 0 (the default) then ends as before (the
     report's line), never deferred, and the router's activation is not
     delayed for a send that cannot happen.
  2. The boot's latch (12.8.15.13): set, refused (`10Fh`) as the send
     refuses it today, before any test that could defer.
  3. The mailbox probe, passed once a start (decided 2026-11-04 on the owner's
     delegation, after the D leg on 32-bit XP): under a hold of the
     members of its own (12.8.15.7's acquisition, which also clears a
     previous exchange's abort; no tracked request in flight, so no power
     or PnP transition of a member under way), through the upstream
     port's own configuration interface (its member's
     `BUS_INTERFACE_STANDARD`, after the send's retake of it): the
     port's id at 0 and its secondary bus at `18h` as the host found by
     bus has them (else `10Bh`), the port in D0 (else `104h`), and the
     command dword at `420h` read with its whole length and not all ones
     (else `109h`), and the alias test (below; else `112h`), each a
     refusal, never deferred; then the hold is released. It defers instead (`cm.osc.defer`, nothing armed yet) when
     it cannot be read in a stable state: the upstream port's member not
     started or its interface not held (`10Ah`), the hold not had within
     its 1000 ms (`10Eh`), or a surprise removal or the NHI's revocation
     during it (`10Bh`, the next try finds the host by bus again). None of
     it writes, and the probe passed is kept. A configuration read alone:
     no firmware memory, no marker. On XP the bus interface does not
     reach extended configuration space (12.8.15.2: not promised before
     Vista), and 12.8.13.1 forbids ECAM on NT, so the send's first
     mailbox read fails there (`109h`); the probe makes that refusal come
     before the read path maps any firmware memory and before E arms the
     marker. The send's own checks under its hold (12.8.15.7) stay as
     they are, and repeat the alias test and the system gate before the
     command is written.

     *The alias test and the system gate* (decided 2026-11-04 on the
     owner's delegation, after the leg on Windows 2000): 2000's bus
     interface does not fail an access at `100h` or more, it drops the
     offset's high bits, so `420h` read the upstream port's dword `20h`
     (its memory base and limit), the probe passed, and the send wrote
     the command there, closing the port's memory window
     (`runs/run-46.md`, "46.12 on NT: the board once a start, the
     mailbox's reach"). So: (a) the system gate: on Windows 2000 (no
     `RtlGetVersion`, 12.8.15.17) no offset of `100h` or more is asked of
     a route at all, read or write (the route's own accessors refuse
     it), and the probe refuses `112h` before any read; (b) the alias
     test, on every other system, in the probe and again in the send
     before the command is written: dwords `0`, `100h`, `20h` and `420h`
     read through the route, and `100h` equal to `0` or `420h` equal to
     `20h` refuses `112h` (reads only; a coincidence on a route that does
     reach the extended space only refuses). Nothing else of the
     connection manager writes an offset of `100h` or more through a
     configuration route (the members' and the NHI's own writes are in
     the first 256 bytes; the host interface's registers are memory).

     What each system's route was read to do with `420h` (the emulator,
     q35 with an `MCFG`, the qemu flavour):

     | System | `420h` through the upstream port's interface | Outcome |
     |---|---|---|
     | Windows 2000 SP4 | aliased: dword `20h` (`FE70FA80h`) | gate, `112h`, nothing read or written; before the gate the command was written to `20h` |
     | XP SP3, 32-bit | all ones | `109h` at the probe |
     | XP x64 SP2 (build 3790) | all ones | `109h` at the probe |
     | Vista SP2 x86 | reached (`100h` an extended capability, `420h` idle) | C, sent, `100h` |
     | 7 SP1 x86 | reached | sent, `100h` (E, the earlier image) |
     | 7 SP1 x64 | reached | C, sent, `100h` |
     | 8 x86, 8.1 x86 | reached | C, sent, `100h` (with xHCI98 on a controller, so the helper is there) |
     | 10 x64 (19045) | reached | B, sent, `100h` |

     On Windows 2000 nothing is sent by the gate, whatever its route
     would read. On 32-bit XP and XP x64 nothing was sent in these
     emulator legs: their bus interfaces did not reach the upstream
     port's extended space there (the probe decides on each system, so a
     route that reaches it would send), and ECAM is not used on NT
     (12.8.13.1). Whether those systems get the handover by another route
     is the owner's question.
  4. An unknown board with `XhciCmAsm4242Reads` 1: the key's flushability
     is tested (the `ZwFlushKey` of "What defers"), and only a flushable
     key lets the read path (12.8.15.17) run; a key not flushable defers
     with nothing read. The read path's decision to send is kept (its
     tier and data byte); its refusal ends the start's sequence as
     before (`110h`, never deferred), so nothing of it is kept.
  5. A decision to send, kept or just made: the key's flushability is
     tested before the marker is written, once a try (a try that tested
     it in step 4 does not test it again); not flushable defers, nothing
     written, as before.

  The read path runs before the marker is armed (step 4 before step 5),
  so the boot guard does not cover a stop of the system during the read
  path's own mappings: a hang or a stop error there leaves no marker,
  and the next boot reads again. That is part of the read path's stated
  risk (Q20, the opt-in), not a case the guard's skip handles.

  So a deferred start reads the firmware at most once (a deferral for a
  member not found comes before step 1, one for coverage after the
  decision), and E's match runs at most once a start; a probe passed is
  kept, a deferred one runs again at the next try (reads only). The
  flush test moves from the send to the sequence (the S4 resend tests
  nothing, as before, and probes nothing: it follows a send whose
  exchange reached the mailbox). A new start of the NHI (a disable and
  enable, a stop and a start) forgets what was kept and decides afresh;
  the S4 resend decides afresh under the mutex as 12.8.15.17 says, and
  neither uses nor changes what a start kept. Host vectors (`cm_oscp.c`,
  `test_cm_oscp`): `CmOscpBoardNext`, the next step from what is kept,
  the latch and the key's test (match, latch refused, probe, test, read,
  send, defer, unknown), with a latched unflushable key refused, never
  deferred; `CmOscpProbe`, the probe's outcome from its reads (the hold
  not had, the member not usable, aborted: deferred; the route, D0, a
  short or all-ones mailbox read: refused, so no discovery and no marker
  follow; passed); and `CmOscpKept`, the kept decision used only for the
  same host, each address and each id changed alone a refusal, a read
  then a coverage deferral then a changed host refused with no second
  read, a probe deferred then passed, and a probe passed kept across a
  coverage deferral.

##### 12.8.15.17 The read path, B, C and D, as built

2026-11-04, reviewed and revised: how 12.8.15.4 to 12.8.15.6
are built in `xhcicm.sys`, both images, and what this settles that those
sections leave open. It replaces nothing they decide but the points
marked "revised".

**The common gates, split** (revised from 12.8.15.14). E's gates required
`RtlGetVersion` and the two WMI routines on every system; Windows 2000
exports none of the three, so D could never run there. From this change:

- **Required on every route**: the routine lookup itself (amd64:
  imported; x86: the helper's, 12.8.15.15) and the marker's
  `ZwFlushKey` and `ZwDeleteValueKey` (12.8.15.13); else `108h` as today.
- **The system**: `RtlGetVersion` when found (its build; 22000 and later
  refused, `108h`); when absent, the system must be Windows 2000
  (`IoIsWdmVersionAvailable(1, 0x10)` and not `(1, 0x20)`), else `108h`
  (a later system without it is not expected and is refused).
- **SMBIOS**: the WMI routines absent, or the block absent, is an unknown
  board (12.8.15.11's "absent"), not a refusal.
- **Each read route's own routines** below, required only by the route
  chosen.

**Where.** E's sequence unchanged up to the match. When the board is
unknown and `XhciCmAsm4242Reads` is 1, the read path runs in the match's
place (`cmOsnBoard`), under the once-per-boot mutex, after the served
check and the boot guard's read, before the marker is armed: it yields
the tier and, for a send, the command's data byte; the rest is E's (the
coverage and the barrier, the marker, the exchange through the upstream
port's own interface).
*Revised* (2026-11-04): the read runs only after 12.8.15.16's mailbox
probe has passed and the key has been found flushable ("The board decided
once a start"), so a system whose bus interface does not reach the
upstream port's extended configuration space (32-bit XP, read on the
emulator; never ECAM on NT, 12.8.13.1) is refused `109h` before any
firmware memory is mapped, and a deferred start reads at most once.
**At an S4 resume** (revised: `CmOscntResume`
called the match before taking the mutex) the owed mark is consumed
first, as today, then the mutex is taken and the board decided under it
(E's match or the read path, with a fresh snapshot, the tables and the
data read again), the send made, the mutex released; one acquisition, a
refusal releasing it on its way out; no deferral and no second attempt
after a refusal, and a second D0 sends nothing (the mark is gone). A
refusal of the read path sends nothing by any route: `cm.osc.refused`
`110h` (its meaning becomes "the read path declined"), `cm.osc.read` its
reason. With `XhciCmAsm4242Reads` 0 nothing changes from E.

*A stored result, considered and rejected* (owner, 2026-11-04): keeping
the read path's tier and data byte across boots, keyed by the SMBIOS
identity and the PCI ids, was weighed and is not built. A reply of `100h`
does not show that a stored data byte is still the firmware's, so a
firmware change the key does not see (a setup option, a CMOS reset, a
second BIOS bank, a restored image) could send a stale byte unnoticed.
The tables are read fresh: at most once a start, so once a boot unless
the NHI is started again (12.8.15.16), and again at an S4 resend.

**The routines of the read path** (by the lookup, none imported):
`MmGetPhysicalMemoryRangesEx2`, `MmGetPhysicalMemoryRanges`,
`ExGetFirmwareEnvironmentVariable`, `ZwQuerySystemInformation`, found once
a load beside the others. A snapshot either range routine returns is
copied into an array of the driver's own and freed with `ExFreePool`
through `cm_pool.c`'s `CmFree` (the pool pair's `SITES` stays
`cm_pool.obj`).

**Each step has three outcomes**: *obtained*; *absent* (the source does
not exist on this system: a routine not exported, a class the system
does not know, a scan of admitted memory finding no root pointer); or
*refused* (anything else: a read refused by the page check, an
allocation, a malformed answer, a checksum). Only *absent* moves to the
next route; *refused* ends the attempt (`110h`, logged), as 12.8.15.3
says ("never after B refused for any other reason").

**The route, chosen once an attempt** (`cm.osc.read.route` 1 B, 2 C,
3 D):

1. **B**, when `MmGetPhysicalMemoryRangesEx2` is found (Windows 10
   version 2004 and later), called as `Ex2(NULL, 0)` (the system
   partition, no flags). `ExGetFirmwareEnvironmentVariable` is required
   there (absent: refused): asked for the variable `Xhci98BootProbe` of
   this project's own vendor GUID with a 4-byte buffer;
   `STATUS_NOT_IMPLEMENTED` is a legacy BIOS boot, any other status a UEFI
   boot (`cm.osc.read.uefi` the status). A BIOS boot: the scan of
   `E0000h` to `FFFFFh` (each page through the refusal below; a page
   refused is *refused*, a whole admitted area with no root pointer is
   *absent*, C serves). A UEFI boot: nothing scanned, C serves (Q17 (a)).
2. **C**, on Windows 10 version 2004 and later after B's *absent*, and on
   every earlier system where `IoIsWdmVersionAvailable(1, 0x30)` is true
   (XP x64, Server 2003, Vista and later): `ZwQuerySystemInformation`
   required (absent: on those systems, refused). The snapshot by Ex2 when
   found, else `MmGetPhysicalMemoryRanges` (required). The tables by
   class 76 (`SystemFirmwareTableInformation`, provider `'ACPI'`, action
   get): the root table (`XSDT` asked first, then `RSDT`; a status of
   "not found" for a name is *absent* for that name; both absent:
   refused) and **MCFG by name**, as 12.8.15.5 says. The query's buffer
   (`SYSTEM_FIRMWARE_TABLE_INFORMATION`, 16 bytes before `TableBuffer`):
   one allocation of `16 + cap` bytes, `cap` the table's payload bound
   (the root table `24h + 8 x 64` = `224h`, MCFG `1000h`),
   `TableBufferLength` set to `cap`; a status other than success, or a
   returned `TableBufferLength` above `cap`, below 36 or unequal to the
   table's own `Length` field, or a checksum that does not sum to zero,
   is refused (a table larger than `cap` is refused, never asked
   again). The MCFG copy gives the ECAM bounds before any physical read;
   the root table copy's entries are then read physically for the
   SSDTs alone (no physical MCFG pass on C). Class 76 failing on a
   system where it must exist is refused, never D.
3. **D**, only on Windows 2000 and 32-bit XP (`IoIsWdmVersionAvailable(1,
   0x30)` false; the x86 image): `MmGetPhysicalMemoryRanges` required;
   the root pointer by B's scan. *Revised* (decided 2026-11-04 on the
   owner's delegation): 12.8.15.6 read XP's root table from
   `HKLM\HARDWARE\ACPI\RSDT`, ACPI.SYS's own undocumented copy; D scans
   on XP as on 2000, which drops that undocumented source from the
   driver (the owner's registry route was decided for XHCITR's user-mode
   save, task 46.21, which stays as it is). The scan runs only under
   `XhciCmAsm4242Reads` 1, is bounded to `E0000h` to `FFFFFh`, and a root
   pointer's checksum is checked before any length in it is trusted
   (`CmOscFindRsdp`). Its narrower coverage is stated: a firmware whose root pointer
   is only in the extended BIOS data area (which NT does not let this
   driver read) is refused on XP (*absent*: "no root pointer",
   `cm.osc.read` `1`) where the registry copy would have served; and, as
   12.8.15.3 says, that the root found is the one the system used is not
   established.

The reasons in `cm.osc.read`: 98 SE's `CM_OSC_*` (1 to 14) for the
discovery's own, and `21h` a routine of the route absent, `22h` the
snapshot not had (or more than 256 ranges before the zero pair), `23h`
the boot type not read, `24h` class 76 refused, `25h` the scan refused (a
page), `26h` no route for this system, `27h` a read of the discovery
refused by the page check, or whose mapping failed (terminal: no read is made after it, whatever
the discovery would have made of the rest).

**The page refusal.** One snapshot an attempt, taken before the first
physical read and freed on every exit of the attempt: an array of
(base, length) pairs, 64-bit each, in bytes, ended by a zero pair; at
most 256 pairs before it (a 257th: `22h`). Every physical read of the
attempt (the scan, the root table, each entry's header, MCFG on B and
D, the three SSDTs, `\CPNV`'s two pointers, the data bytes) is refused
when any byte of the pages it spans, from the first byte's page start to
the last byte's page end, lies in a range the snapshot holds, though
the bytes asked for may lie outside it. The comparisons are made on
32-bit halves with the carry written out (no 64-bit arithmetic): a range
whose base is at or above 4 GB is ignored, a range that ends at or above
4 GB is taken up to 4 GB, a read never extends past `FFFFFFFFh`. The
bounds of 12.8.3 step 2 stand as on 98 SE (1 MB to `FEC00000h`, never
ECAM) with the BIOS area admitted for the scan alone; the extended BIOS
data area and the BIOS data area are never read on NT. A read is mapped
with `MmMapIoSpace` (`MmNonCached`), each byte read alone, unmapped
after, as on 98 SE; the cache-type risk is the one the opt-in states
(Q20).

**The discovery itself** is 98 SE's, shared, not copied, split so that
nothing 98-only runs on NT. Shared, compiled on every target: the root
table checked, the MCFG pass, the three named SSDTs walked, `\CPNV`
placed, the tier from the methods' hashes, else the canonical form under
`XhciCmAsm4242Structural`, only then 12.8.3 step 6's pointers and bytes
in the firmware's order, then `CmOscDecide` against the root port found
by bus; it takes a reader (a routine and its context: 98 SE's, or NT's
with the snapshot) and optionally a root table and an MCFG already
obtained (C). 98 SE and ME only, unchanged and not reached on NT: the
root pointer from the BIOS data area and the EBDA, the inventory, the
served table, ECAM's mapping and the exchange through it, and the tables
kept for XHCITR (12.8.14; on NT XHCITR reads them itself, and the shared
part is given no capture). The status record's tier is 2 (a known build)
or 3 (the same handover code); bit 19 means what it means on 98 SE and
ME.

**`XhciCmAsm4242Structural` on NT.** Read on every system from this
change (`cm_reg.c`, read in common, `XhciCmAsm4242Reads` still NT
alone); `xhcicm.inf` moves its row from `[Cm.Values9x]` to `[Cm.Values]`
(every install path), and `xhcicm-amd64.inf` gains it in its shared
values section, both at 0 with no overwrite, with their INF gate rows and
`tuning.md` row naming NT. On NT it matters only with
`XhciCmAsm4242Reads` 1. The installer's row (`INST_APPLY_CM`, its note)
is 12.8.15.12's, built with the installer's other rows.

**Host vectors** (`cm_oscp.c`, new pure functions, `test_cm_oscp`):
- `CmOscpRangeHit`: a read inside a range; overlapping either end; a
  read outside every range whose page overlaps one; a read across a page
  boundary whose second page overlaps; touching but outside; a range at
  or above 4 GB; one ending past 4 GB; a read ending exactly at
  `FFFFFFFFh`; the zero pair ending the list; 256 pairs and a 257th.
- `CmOscpReadRoute`: Ex2 with a BIOS boot (B), with B *absent* (C), with
  B *refused* (none); Ex2 with a UEFI boot (C); no Ex2 with WDM 1.30
  (C, and C refused: none, never D); no Ex2 below 1.30 on x86 (D); the
  routines missing per route; amd64 never D.
- `CmOscpFwTable`: class 76's answer: success with a length equal to the
  table's, above `cap`, below 36, unequal to its `Length`, a bad
  checksum, a status of not found (absent), any other failure.
- The gates' split: no `RtlGetVersion` on 2000 (admitted) and on a
  system that is not 2000 (refused); the WMI routines absent (unknown).
- The resume (the sequence's decision): owed, read path refused, then a
  second D0 (nothing).

**Legs** (the emulator, the qemu flavour; build 1 to 4 tables made by
`usb4-emu`'s `asm-fw-ssdt.py --externals-ifzero`, `acpi-sb-osc=off`; no
`-smbios` profile, so E finds the board unknown):

**The emulator's tables on NT.** An NT guest's own ACPI evaluates the
loaded `\_SB._OSC`; without `--osi` the model's `_OSC` sends the
handover itself, so a leg would not show this driver's send. The legs
use `--osi` (the board's condition: nothing sent before Windows 11), and
the qemu flavour's lists (`CmOscKnownCode`, `CmOscKnownForm`) gain build
1 `--osi`'s five hashes and five forms, computed from the tables by the
pure code on the host and checked by a `test_cm_osc` fixture
(`--externals-ifzero` leaves the methods' bytes as they are; the root
walk must pass its `If (Zero)`, a vector). Build 2 `--osi` and build 4
`--osi` then have build 1 `--osi`'s forms (tier 3), build 3 `--osi` none.
The release flavour's lists do not change.

| Leg | System | Expected |
|---|---|---|
| reads 0 | 10 x64 | nothing read, E's report line, as before |
| B | 10 x64, BIOS boot, reads 1 | route 1, tier 2, sent, the router answering |
| B on x86 | 10 x86 or 7 x86 where B or C applies | the routines through the helper, tier 2, sent |
| C | 7 x64 or Vista, reads 1 | route 2, tier 2, sent |
| C on UEFI | 10 x64 UEFI (OVMF), where the base exists | route 2, no BIOS scan logged, tier 2, sent |
| D | 2000 and 32-bit XP, reads 1 | route 3, tier 2, sent where the system starts the switch's functions |
| build 2 | 10 x64, reads 1, `XhciCmAsm4242Structural` 0 then 1 | tier 3, refused with no pointer followed; then sent |
| build 3 | 10 x64, reads 1 | tier 1, refused |
| build 4 | 10 x64, reads 1, `XhciCmAsm4242Structural` 0 | its `_OSC` in no hash list, its forms build 1's: tier 3, refused |
| S4 | 10 x64, B | the resend reads the tables again and sends |

[ms-fw]: Microsoft, learn.microsoft.com: `GetSystemFirmwareTable` and
`EnumSystemFirmwareTables` (windows/win32/api/sysinfoapi), "the first
table in the list that has this name".

##### 12.8.15.18 ECAM on Windows 2000, 32-bit XP and XP x64 (task 46.25)

Draft 1, 2026-11-04. The owner's decision of 2026-11-04, after
12.8.15.16's per-system table: an exception to 12.8.13.1's first rule
for those three systems alone, so that they too get the handover. There
the mailbox's two dwords are reached through ECAM, mapped by this
driver. Every other rule of 12.8.13.1 stands on them. On every other NT
system (Vista and later, and Server 2003 x86) nothing changes, and
ECAM is never mapped. 98 SE and ME are unchanged. The owner's answers
to Q22 to Q25 (2026-11-04) are applied below.

**The systems** (owner, 2026-11-04, Q23 (a); `CmOscpEcamSystem`, pure): Windows 2000 (no
`RtlGetVersion`, the system gate of 12.8.15.17); 5.1 on the x86 image
(32-bit XP); 5.2 on the amd64 image (XP x64, and Server 2003 x64,
which is the same OS, 3790, and is not told apart). 5.2 on the x86
image (Server 2003 x86, no target) and every 6.x and later system are
excluded. On these three, `cmOsnNoExt` (12.8.15.16's gate) holds for
the upstream port's bus interface: no offset of `100h` or more is asked
of it, read or write (owner, 2026-11-04, Q24 (a): ECAM alone, never
the bus interface first). What 12.8.15.16's table read there is why: 2000's
route aliases such an offset, and XP's and XP x64's read all ones. The
first 256 bytes (the route's dwords 0 and `18h`, the power state by the
capability walk) stay on the bus interface. Only the mailbox moves.

**The opt-in** (owner, 2026-11-04, Q22 (a)): the ECAM route runs only
with `XhciCmAsm4242Reads` 1, for a known board (E) as for an unknown
one. On 2000 and 32-bit XP the ECAM base can be found only by reading
firmware memory, the cache-type risk that value states. On XP x64 it
comes through class 76, which Microsoft does not document for drivers.
And the ECAM page is itself a physical mapping of this driver's own.
With 0 nothing is read beyond the first 256 bytes and nothing is
written: an unknown board ends as before (`cm.osc.unknown`, bit 22),
and a known board is refused `113h`, with status bit 25 and a line in
XHCITR's report (owner, 2026-11-04, Q25 (b)). No new value, INF row or
installer row.

**Where the base comes from** (`cmOsnEcamFind`, once a start, kept with
the start's decision, 12.8.15.16). It runs after the latch's test and
the key's test (a key not flushable defers with nothing read, as before
the read path) and before the probe, under the once-per-boot mutex. It uses one snapshot
of the memory the system manages (`MmGetPhysicalMemoryRanges`, as C and
D take it), freed on every exit.

- **XP x64** (the amd64 image, WDM 1.30: route C's source): `MCFG` by
  name through class 76 (`cmOsnFwTable`, the cap `1000h`, the checks of
  `CmOscpFwTable`). No physical read of firmware memory.
- **2000 and 32-bit XP** (the x86 image below WDM 1.30: route D's
  source): the scan of `E0000h` to `FFFFFh` (`CmOscNtScan`), then the
  root table and the MCFG pass of the shared discovery: `CmOscNtMcfg`
  runs `cmOscDiscover` itself with its reader's `McfgOnly` set, which
  returns once the MCFG pass has given the base, before any SSDT. Every read goes through the page refusal and
  12.8.3 step 2's bounds, as D's are; no SSDT is read.
- Then `CmOscMcfg`: exactly one segment 0 allocation whose bus range
  holds the root port's bus to the downstream port's bus, its base 1 MB
  aligned and below 4 GB. The upstream port's page comes from
  `CmOscEcam`, at the bus, device and function `cmOsnHostByBus` found.
  That function is also the upstream port member's own location (its
  PDO's bus number and address, `cmOsnLocate`), which `cmOsnUse`
  requires to be equal.
- The page refused when any byte of it lies in a range of the snapshot
  (`CmOscpRangeHit`).

Its outcome is `114h` on any failure (`cm.osc.ecam` the reason: 1 a
routine absent, 2 the snapshot, 3 no root pointer, 4 the root table, 5
no `MCFG` or one refused, 6 the buses outside its allocation, 7 the
page in the snapshot, 8 the mapping failed, 9 class 76 refused, `0Ah` a
read refused by the page check). It is never deferred. **Is `MCFG`
there?** It is a firmware table (the PCI Firmware Specification),
written by the firmware whatever the system. These systems do not
promise extended configuration space through it (12.8.13.2), and
12.8.15.16's table shows they do not reach it. The board has one (12.8.1; 98 SE reads
it there), and so does q35. A firmware without one is refused (`114h`,
5).

**The page's mapping.** One 4 KB page, the upstream port's function,
`MmMapIoSpace(base, 1000h, MmNonCached)` at `PASSIVE_LEVEL`, mapped
only inside a hold of the members (12.8.15.7: the probe's, and the
send's) and unmapped (`MmUnmapIoSpace`, `PASSIVE_LEVEL`) before the
hold is released, on every exit. Each access is one aligned dword
(`READ_REGISTER_ULONG`, `WRITE_REGISTER_ULONG`). Both routines are
already imported by both images (`xhcicm-imports.allow`,
`xhcicm-imports-amd64.allow`), so no import is added. The cache type:
ECAM is device memory, outside every range the system manages (the
refusal above), so no cached mapping of RAM is aliased. The other
mappings of that page that could exist are the system's own. No
document says that the HAL or the PCI driver of these systems maps
ECAM, and their bus interfaces do not reach the extended space
(12.8.15.16), which a mapping of ECAM would. The ACPI interpreter maps a
`SystemMemory` operation region whose address lies there, as the
firmware's own sender does (12.8.13.10, the upstream port's power
state written through ECAM). The cache type it uses is not documented.
A device register region is mapped non-cached, as this driver maps its
own. On x86 and x64 a range the firmware's MTRRs make uncacheable is
uncacheable whatever the page attributes say. That this firmware's
MTRRs do so for ECAM is not read. Stated as the route's risk (Q20's,
under the same opt-in), not mitigated.

**What is verified before any write.** The probe and the send's checks
under the hold (12.8.15.16, 12.8.15.7) gain, on these systems, the ECAM
page's check (`CmOscpEcamCheck`, pure), between the route's checks and
the mailbox's read. The page's dwords 0, `08h`, `18h` and `20h` must
equal what the bus interface reads at the same offsets in the same
hold. Dword 0 must be `1B21:2421` (`CM_OSCP_ID_USP`), dword `08h`'s
class `0604h`, and dword `18h`'s secondary bus the one found by bus.
Then the alias test of 12.8.15.16 runs on the page (dwords 0, `100h`,
`20h`, `420h` read through ECAM, `CmOscpAliased`). Any difference or
alias is `115h`, the page not the port (`cm.osc.ecam.check` which
test). Then the command dword at `420h` through ECAM: all ones is
`109h`, as through a route. So the page is shown to be the same
function as the bus interface's before the mailbox is written; until the
check completes the only reads of the page are its own (dwords 0, `08h`,
`18h`, `20h`, and the alias test's `100h` and `420h`, made even when the
page then proves to be another function's: configuration reads, no
write).

**What goes through ECAM.** Writes are limited to `420h` and `430h`.
Reads are limited to `0`, `08h`, `18h`, `20h`, `100h`, `420h` and `430h`.
The accessor refuses any other offset (a host vector). Nothing else of
the connection manager uses the page, and the page is never mapped
outside a hold. Every exit of the probe and of the send (a refusal, a
timeout, an abort, a surprise removal) unmaps it before the hold is
released, and a try keeps only the base and its bus range, never a
mapping. A count of the mappings outstanding is logged at each unmap
(`cm.osc.ecam.maps`, 0 expected), so a leg reads that none is left.
On these systems an access of `100h` or more never falls back to the
bus interface: its accessor refuses it there (`cmOsnNoExt`).

**The order** (12.8.15.16's, on these systems): the gates, the host by
bus, the served check and the guard's read, the kept test, E's match,
the latch. Then, only with `XhciCmAsm4242Reads` 1: the key's test
(moved before the ECAM base on these systems, so a deferral for a key
not yet flushable comes before any firmware read, for E as for the read
path), the ECAM base (`cmOsnEcamFind`, kept: a later try of the same
start does not read again), the probe through ECAM, and for an unknown
board the read path. The read path's own `MCFG` must give the same base
and bus range as the kept one, else it is refused (`110h`, `cm.osc.read`
`28h`).

*XP x64's root table* (revised after the emulator's leg, 2026-11-04):
XP x64's firmware table provider gave `MCFG` by name and answered "not
found" for both `XSDT` and `RSDT` (class 76, `C0000225h`), so route C's
root table was absent there and the read path refused (`cm.osc.read`
`24h`) after the ECAM probe had passed. On the ECAM systems alone (so on
XP x64 alone, C's only one of them), both names absent now takes the
root pointer from the scan of `E0000h` to `FFFFFh` (`CmOscNtScan`, D's,
with the page refusal; `cm.osc.read.rootscan` its outcome), `MCFG`
still by name; a scan that finds none or is refused ends the read path
as D's does. Every other system refuses as 12.8.15.17 says. Then the send. `CmOscpBoardNext` gains the two inputs (the
system, and the base kept) and the two steps (`CM_OSCP_B_ECAM`,
`CM_OSCP_B_NOECAM`).

**The boot guard, the S4 resend, E.** The guard is unchanged
(12.8.15.13): the marker is armed before the send's hold, and the
page is mapped inside it. A stop of the system during the ECAM base's
reads leaves no marker, as the read path's do (12.8.15.16's stated
reach). The S4 resend (12.8.13.6) finds the base afresh under the
mutex, with a fresh snapshot, then sends with the checks above under
its hold. It never defers. E's profile, and its data byte, are
unchanged. On these systems E sends only through ECAM, so only under
the opt-in.

**The refusals added**, after `112h`:

- `113h`: a system whose mailbox is reached by ECAM alone, with
  `XhciCmAsm4242Reads` 0 (a known board). Nothing beyond the first 256
  bytes is read and nothing is written.
- `114h`: no ECAM page for the port (`cm.osc.ecam` the reason).
- `115h`: the ECAM page is not the port the route reads, or it aliases.

On Windows 2000 the probe's `112h` gate gives way to the ECAM route
(the bus interface still asked nothing of `100h` or more). On XP and XP
x64, `109h` keeps its meaning through ECAM.

**Host vectors** (`test_cm_oscp`): `CmOscpEcamSystem` for each system
and image (2000, 5.1 x86, 5.2 x86 excluded, 5.2 amd64, 6.0 and later
excluded); `CmOscpBoardNext` with the system and without (each earlier
vector unchanged), with reads 0 (a known board `NOECAM`, an unknown one
`UNKNOWN`), the key untested (`FLUSH`, before the base), not flushable
(`DEFER`), the base not kept (`ECAM`), kept (`PROBE`); `CmOscpEcamCheck`
with each dword differing alone, the id not the upstream port's, the
class, the secondary bus, an alias at `100h` and at `420h`;
`CmOscpEcamOffset` with each admitted read and write offset and one
neighbour of each refused; `CmOscpProbeEx` with the page's outcome
(`114h`, `115h`, its mailbox all ones, each after the route and D0);
and the order held over every input (no base before the key's test,
no probe without the base, no read or send before both). The MCFG pass
itself is `CmOscMcfg`'s, host-tested already (`test_cm_osc`);
`CmOscNtMcfg` only stops the shared discovery after it.

**Legs** (the emulator, q35 with its `MCFG`, the ASM4242 model, the
qemu flavour, `XhciCmAsm4242Reads` 1):

| Leg | System | Expected |
|---|---|---|
| D and ECAM | 2000, 32-bit XP | the base by the scan, the probe through ECAM, sent `100h`, the query `000F0100h`, the router answering, the port's memory window intact (`info pci`) |
| C and ECAM | XP x64 | the base by class 76, the same |
| reads 0 | XP | `113h` (known board) or `cm.osc.unknown` (unknown); nothing mapped |
| the page not the port | XP | the base taken a bus too high (`XhciCmQemuEcamFault` 1, the qemu flavour), the page a downstream port's: `115h`, nothing written |
| no MCFG | XP | MCFG taken as absent (`XhciCmQemuEcamFault` 2), `114h` (5), nothing written |
| a timeout | XP | the model's reply withheld (`x-mb-no-reply`), the timeout logged, never retried, `cm.osc.ecam.maps` 0 |
| S4 | XP, XP x64 | `shutdown /h` and the resume: the base found afresh (each source), sent again, `cm.osc.ecam.maps` 0 |
| a deferral | any of the three | where the start defers (the key, a member), the next try maps afresh from the kept base: one `cm.osc.ecam.base` a start |
| regression | 7 x64 or 10 x64 | the route as before, no `cm.osc.ecam` line |

**The owner's decisions** (2026-11-04): Q22 (a), `XhciCmAsm4242Reads`
1 alone, default 0, for a known board and an unknown one alike (not a
value of its own, nor on by default for a known board); Q23 (a), 5.0 and
5.1 on the x86 image and 5.2 on the amd64 image, Server 2003 x86 not
admitted (nor XP x64 told apart from Server 2003 x64); Q24 (a), ECAM
alone on those systems, the bus interface never asked `100h` or more
there and the first 256 bytes kept on it (not the bus interface first
with ECAM after an all-ones read); Q25 (b), a known board at reads 0
refused `113h` in the log, status bit 25 (record 24 section 3; free
until now, a reader that knows it not reads 0, the version stays 1)
and XHCITR's report line "Handover not sent: on this Windows it needs
the firmware reads setting set to yes."

#### 12.8.17 Read on the emulator: ME's fault legs and the structural row on NT (task 47.13)

2026-11-06, reviewed and revised. What task 47.13 read on the emulator
(`runs/run-47.md`, "47.13"); nothing of the driver, the INFs or the
installer changed.

**ME.** 12.8.8's legs on ME had waited for an ME without ACPI, since
98 SE with ACPI stopped at "While initializing device CONFIGMG: Windows
protection error" with the emulator's tables ("46.12"). ME with ACPI
(the q35 base) stops the same way on build 1 `--osi`'s tables as
`usb4-emu`'s script writes them by default, with QEMU's own `\_SB._OSC`
left out (`acpi-sb-osc=off`), and starts with `--externals-ifzero` (the
tables' `External`s inside `If (Zero)`, the methods' bytes unchanged),
the cure of Windows 7 x86's bug check `A5` (12.8.15.17). With ACPI on,
the driver found the tables, the known build (tier 2) and the mailbox
as on 98 SE without ACPI. The legs read on ME ended as on 98 SE: the
command sent once and a disable and enable sending nothing again; the
mailbox busy from the reset (`107h`) and for good (not accepted), a
hold of 300 ms, a reply never given and the start after it; the release
flavour declining the tables (tier 1); build 2 refused at 0 (`0Eh`) and
sent at 1, build 3 declined (`0Dh`), the data naming another root port
(`0Bh`) and `XhciCmAsm4242Handover` 0 ("46.12" and "46.12 The 9x
fault legs"). Not read on ME: a tunnelled drive hot-plugged after the
handover, and the installer's question. One leg more, a hold past the
driver's wait (1500 ms): the command not accepted
within 1000 ms, no query, the reply register left as the model later
wrote it (12.8.5: written 0 only once a reply came); the model then took
the command and made the handover, so the router answered and the start
went active while the log says the handover failed (`FFFFFFFEh`). The
stated rule holds (no second attempt); a firmware that answers late is
served by that chance alone.

**The structural row on NT, as built.** 12.8.15.12 said the row would
keep `INST_APPLY_9X_CM` until both `xhcicm` INFs wrote the value on every
system with the read path. Both did from 3.4.1.0 (`[Cm.Values]`, the INF
gate's every-path row), and the row has applied on 2000 to 10
(`INST_APPLY_CM`) since: on 10 x86 the install wrote
`XhciCmAsm4242Handover` 1, `XhciCmAsm4242Structural` 0 and
`XhciCmAsm4242Reads` 0 in the NHI's driver key, `/set
XhciCmAsm4242Structural=1` was taken and written under `Services\USB`
(which the driver read over the driver key), and the Thunderbolt
settings' Advanced list showed the row with the machine-wide value, not
greyed. The read path then gave 12.8.15.17's table on 10 x86 (route B,
the routines through the helper): build 2 at 0 tier 3 refused
(`cm.osc.read` `0Eh`, `110h`, no pointer of `\CPNV` followed, no
mailbox command sent; the probe of 12.8.15.16 had read the command
dword before the read path, as at every start), at 1 sent and the
router answering; build 3 tier 1 refused
(`0Dh`) at 1; build 4 tier 3, sent at 1 and refused at 0.

Not shown by the report: a start refused at tier 3 with the value 0
says only "Stage: failed" in XHCITR's report (the status record's tier
is there, no line names it), while 98 SE and ME have the installer's
question for it; the owner chose that XHCITR say so (2026-11-06,
roadmap-usb4 47.29; `runs/run-47.md`, "47.13").

### 12.9 The emulator's reading (task 46.11 step 2, 2026-11-03)

On 98 SE without ACPI in the emulator (`runs/run-46.md` "46.11"): the
q35 machine's SeaBIOS, the ASM4242 model behind the root port at 00:03,
the switch's downstream port 02:03.0 in front of the host interface
(NHI) 64:00.0, both with Interrupt Pin A. Device Manager's PCI bus page
reads "IRQ Steering Disabled. IRQ Table has some errors." - the board's
reading (section 12.1 item 1) - so every function keeps the Interrupt
Line the firmware wrote. A program run from `AUTOEXEC.BAT` (test only,
as the board's `SETLN2.COM`) wrote both functions' line before Windows:

| Line written | The downstream port | The NHI | `xhcicm` |
|---|---|---|---|
| as SeaBIOS left it (11) | started | started on IRQ 11 | runs |
| 4, the IRQ COM1 holds | problem 12, "cannot find any free Interrupt Request (IRQ) resources" | not listed | not loaded |
| `FFh` | started (no problem) | started with no IRQ (`cm.irq.resource` 0) | runs: the handover, the router answering |

So with steering disabled `PCI.VXD` starts a bridge and the NHI whose
line is `FFh` on their windows alone, with no IRQ, the line left `FFh`
(read back through QEMU after the start): option A of section 12.2
works there, and the board's problem 12 is the line naming an IRQ a
legacy device holds, reproduced. Record 28's reprocess-only form
(section 12 there) is not needed for such a bridge: `PCI.VXD` disables
only a function with no IRQ whose decode is off (record 28 section 12.1
item 1), and the firmware leaves a bridge's on (the board's, section 12.1
item 3; SeaBIOS's here). Not read: steering enabled (a routing table
`PCI.VXD` accepts), ME, a tunnel port, and the board's own `SETLN3.COM`
(`FFh`).

The step itself (section 12.2: which functions, option A or B, always
or only on IRQ 3 or 4) stays the owner's Q1 of section 12.6; it is not
coded. Recommendation, with this reading: A, always for the listed
functions (the downstream port whose secondary bus holds the NHI alone,
and the NHI), on 98 SE and ME.

### 12.10 The step as decided (task 46.11, draft 3)

In `XHCIPFW.VXD`'s `Sys_Critical_Init` pass, after the window step of
section 4.1 and from the same flat scan (every bus 0 to 255, device 0
to 31, function 0 to 7, as section 4.1 step 1 reads them), on 98 SE and
ME, at every start:

1. **The switch.** `XhciPfwNhiLine` (REG_DWORD) under the VxD's own key
   `HKLM\System\CurrentControlSet\Services\VxD\XHCIPFW` (section
   5.1), read with VMM's `_RegOpenKey`, `_RegQueryValueEx` and
   `_RegCloseKey`. A value read whole (type `REG_DWORD`, four bytes):
   0 leaves every line as it is, 1 does the step, any other value is
   refused (logged) and the default, 1, stands. The value missing (the
   query's not-found code): the default, 1. Any other failure of the
   open or the query (another code, another type or length): every line
   left as it is and the failure logged, so that a read that fails never
   overrides a 0 the user set. Registry reads at `Sys_Critical_Init` are
   what the Windows 98 DDK's own VCD sample does (its `Sys_Critical_Init`
   handler opens its VxD key and queries a value) [ddk]; the step's own
   open, query and close are read in the legs on both systems (the
   value 0 leaves the lines, 1 writes them). The gate's list of the
   VxD's services (`pfw-services.allow`) gains the two.
2. **The NHI.** A type 0 function that `xhcicm.inf`'s 98 SE and ME
   models match: its vendor and device ids one of the `Cm.Dev` rows'
   (the list held in `pfw_scan.c`), or its class code `0C0340h` (the
   USB4 host interface's, the INF's `PCI\CC_0C0340` row); its Interrupt
   Pin not 0. A host check reads the INF's `Cm.Dev` rows and holds them
   equal to the VxD's list, the class row included.
3. **Its own downstream port.** The one type 1 function whose secondary
   bus is that NHI's bus, when that bus holds no function but the NHI's
   (every device 0 to 31 there absent but the NHI's device, and that
   device's other functions absent too), its Interrupt Pin not 0; two
   type 1 functions claiming that bus, or a bus holding anything else,
   select no port. Selected by topology alone, never by its ids: a
   tunnel port carries the same id (`1B21:2423` on the ASM4242) and is
   never touched, its line being the one record 21 section 6.1.1 step 5
   (`tbt_brnum.c`'s `brIrq` and `brLines`) copies to what is plugged
   behind it. An NHI on the root bus (an integrated one) has no such
   port: the NHI alone.
4. **Decode off: left.** A selected function whose command register has
   neither memory nor I/O decode on (bits 1 and 0 clear) is left: with
   no IRQ and no decode `PCI.VXD` gives problem 29 (record 28 section
   12.1 item 1). The firmware leaves both on for these functions (the
   board's, section 12.1 item 3; SeaBIOS's, section 12.9).
5. **The write**, one byte: the dword at `3Ch` read; `FFh` written to the
   Interrupt Line alone, a byte write (the dword's address in `CF8h`, the
   byte at `CFCh`, byte lane 0), never a dword write, since on a type 1
   header the dword's upper half is Bridge Control, whose status bits a
   write of the old value could clear; then the dword read back: the
   line `FFh` and the other three bytes as before. Otherwise the old
   line is written back the same way, as a byte, and read, and the
   function is logged refused. A line already `FFh` is left and logged
   so. Nothing else is written. There is no IRQ 3 or 4 rule: every
   selected function is written whatever its line names.
6. **The log**, at `Device_Init` with the rest (section 6):
   `XhciPfwLines` (`REG_BINARY`), up to 8 records of 8 bytes, one per
   selected function: the bus, the device and function, the role (1 the
   NHI, 2 its downstream port), the outcome (1 written, 2 already `FFh`,
   3 decode off, left, 4 refused, written back, 5 refused and not
   written back, 6 left, the switch 0 or not read), the line before, the
   line after, the header type, a zero byte. `XhciPfwLineSwitch`
   (`REG_BINARY`, six dwords): the open's result code, the query's result
   code, the value's type and length as returned, the raw dword read (0
   when none), and the setting in force (0 off, 1 on, 2 left by a read
   failure). `XhciPfwResult` bit 7 a line written,
   bit 8 the switch 0, bit 9 the switch not read (a failure), bit 10 a
   line refused. The qemu flavour's debug-console lines name each.

Why every start and both functions (section 12.1 item 2; the driver's
code): on 98 SE and ME neither function's interrupt is
connected by anything (`PCI.VXD`'s bridge has no handler; `xhcicm.sys`
polls, and its ring interrupt is refused on 9x whatever
`XhciCmRingInterrupt` says), so `FFh` loses no interrupt service; the
function's wire is unchanged. With steering disabled, the function
starts on its windows alone (section 12.9). With a routing table
`PCI.VXD` uses (steering enabled), the draft held that the table's link
is used whatever the line says (record 28 section 12.1 item 1), so that
the step would change nothing there. The emulator's legs with ACPI on
(2026-11-04, `runs/run-46.md` "46.11 Steering enabled"), 98 SE and ME
alike, did not show that a routing link served these two functions:
with the step they started with no IRQ (the line `FFh` left), without it
(`XhciPfwNhiLine` 0) with IRQ 11, shared, the line's own value, which is
also what record 28 section 12.1 predicts where no link serves a
function and the line is the fallback. So with ACPI on the step takes
away an IRQ the function would have had from its line, which on 98 SE
and ME no driver uses (`xhcicm` runs either way, polling, and a
tunnelled drive hot-plugged on ME works in both); whether the step
should then leave the lines is the owner's question (section 12.6). A
case where a routing link is shown to serve the functions is not yet
read. Not proven harmless everywhere: the legs below read the cases
that are not yet read.

**The value's install.** The VxD has no INF: the installer is its
install (section 8). Where the installer writes the static-VxD key's
two values it writes `XhciPfwNhiLine` 1 when the value is missing (an
existing value kept), on every path that leaves the VxD registered,
the one that finds the key already right included, and `/set XhciPfwNhiLine=0|1` on 98 SE and ME
writes it there; the installer's self-test holds its default equal to
`pfw_scan.h`'s, and `tuning.md` gains its row. The key's deletion at an
untick or the uninstall takes it with it, as the log values. (Were an
INF wanted, `xhcicm.inf`'s 98 SE and ME install could add it with
`HKLM`; it runs only once the NHI is listed, which on the board is what
the step brings about, so the installer is the install that comes
first.)

**Host vectors** (`test\test_pfw.c`): the NHI by each listed id and not
by another; the port by its secondary bus holding the NHI alone; a port
whose bus holds a second device or a second function left; two ports
claiming the bus, none taken; a tunnel port (`1B21:2423`, the same id)
whose bus holds a bridge left; pin 0 left; decode off left; a line
already `FFh` left; a write whose neighbouring bytes read otherwise
written back; the switch 0, absent, 1 and 7; the list equal to
`xhcicm.inf`'s.

**Legs** (the emulator, the qemu flavour), each read without the step
(the package before it, or the value 0) and with it, on 98 SE and on
ME: (a) steering disabled, the downstream port's and the NHI's line put
on COM1's IRQ before Windows (test only, as section 12.9): with the step
both start with no IRQ and `xhcicm` loads; without it the port at
problem 12. (b) Steering disabled, the lines usable (SeaBIOS's): both
start with the step as without it, `xhcicm` runs. (c) Steering enabled (a
routing table `PCI.VXD` accepts): the functions get the table's IRQ,
the same with the step as without (with ACPI on, 2026-11-04: not the same,
above; a routing link serving the functions not shown).
In each: the devnodes' problems and
resources, every tunnel port's line and the host's xHCI's unchanged, a
drive hot-plugged behind a tunnel port given its line by the bridge
numbering and running; the QEMU trace showing the step's write before
`PCI.VXD`'s first read of `3Ch`. The board's reading is the owner's.

## Sources

- PCI-to-PCI Bridge Architecture Specification, Revision 1.2, the Type 1
  header's prefetchable memory registers; PCI Express Base Specification,
  the Type 1 header (not held in this repository).
- Windows 98 SE DDK, `INCS_DDK` `INC_WIN98_VMM.H`, the init-order table.
- Linux `drivers/pci/setup-bus.c` (`master`, read 2026-10-27, facts
  only): `pci_setup_bridge_mmio_pref`, `pbus_select_window_for_type`,
  `pci_need_to_release`.
- `usb4-emu`'s asm4242 data (record 22 section 18), `runs/run-44.md` "44.48" and "44.49", design records
  21 (section 6.1.1), 22 (section 18) and 30 (sections 6 and 7).
