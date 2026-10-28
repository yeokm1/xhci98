# Design Record 32 - Prefetchable Windows Above 4 GB Closed Before Windows 98 SE and ME Read Them

Status: draft 11, 2026-10-28: `XHCIPFW.SYS` offered on the 32-bit
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
other. A board with no such window is not written at all, and the log
says so. The step does not read the vendor or device id for its
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
does the scan and writes of section 4 in `Sys_Critical_Init` and nothing in any
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
`HKLM\System\CurrentControlSet\Services\VxD\XHCIPFW`. Every name
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
  directory (release and debug, with or without Stor98; the qemu
  flavour's image in the qemu package), built by
  `scripts\build-driver.cmd` as `S98VSD.VXD` is (`src\pfw\build.cmd`),
  with a gate of record 30 section 6.4's shape (`src\pfw\check-pfw.ps1`):
  an LE image whose one entry is the DDB `XHCIPFW `, no VMM service in
  its code but the registry ones of section 6
  (`src\pfw\pfw-services.allow`), the stamp `XHCIPFW.VXD <version>`, in
  the release image no port instruction outside `pfw_io.obj` and there
  only `CF8h` and `CFCh`, and no write to port `0E9h`.
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

**The 32-bit 8, 8.1 and 10** (the owner, 2026-10-28; task 44.72): not
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
