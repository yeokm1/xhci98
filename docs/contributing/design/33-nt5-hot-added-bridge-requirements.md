# Design Record 33 - Windows 2000 and XP: the Requirement Lists of Hot-Added Bridges

Status: revision 3, 2026-10-27, before code, reviewed and revised: the
owner's answers to section 12's questions folded in; the NHI's role
decided before the position rule; a registered child's immediate
ancestor in `Parent`, the chain's root kept in `Bridge`; the restart
also for a bridge whose attachment is missing (read from a bridge
inventory of its own), after the bridge marks;
no list left without a window; the window arithmetic in 32-bit halves;
the 64-bit BAR rule; a replug before and after the marks' cleanup.
Revision 2, 2026-10-27, before code, reviewed and revised: the
registered attachment written by the mark service after the bridges'
installs (the filter INF's route loses to `machine.inf` on XP's ranking);
the boundary held at an active tunnel port; the registered child's role
and ancestry; a memory claim for every function; contention resolved by
deterministic budgets with one list a bridge; exact templates for the
shape test; lengths capped where a `ULONG` cannot hold them; the lower
driver's status kept; the interrupt step's type 0 restriction and 2000's
legs by HAL. Revision 1, 2026-10-27, before code. Written for
`roadmap-usb4.md` task 44.66 (the owner, 2026-10-27: 44.63's option C
for `3.3.0.0`, the limitation in the release notes; option A designed
now and coded first in the release that brings daisy chains, design
record 34), from task 44.63's reading of a hot-added switch on 2000 and
XP (`runs/run-44.md`, "44.63") and task 44.64's readings of a chain on
q35 (`runs/run-44.md` on branch `p44-chain` at `d95506fc`, "44.64 Part
A" and "44.64 Part B"). Nothing in `src\` implements it. Section 12
has the owner's answers (2026-10-27) and the questions they answer;
section 13 what is open and what is a risk.

In one paragraph: when a PCIe switch with several downstream ports - a
dock, or an enclosure with a downstream Thunderbolt or USB4 port - is
plugged in while Windows 2000 or XP runs, their PCI driver gives every
bridge of it a fixed requirement list that cannot hold more than one
downstream port, so the others end at Code 12. This record makes the
connection manager's child filter, which already sits on those bridges,
rewrite their lists in `IRP_MN_FILTER_RESOURCE_REQUIREMENTS`: the switch's
upstream port asks for what its parent window has, and each downstream
port asks for a share of the upstream port's windows, keeping only the
kinds of window that something below it needs. First it keeps that
filter in every such bridge's stack after the bridges' first-time
installs, which take it away today.

Tags:

- **[run]** this project's readings in `docs/contributing/runs/`, with
  the evidence folders they name.
- **[ddk]** the Windows 2000 DDK's headers in `tools\ntddk\inc`
  (`wdm.h`: `IO_RESOURCE_REQUIREMENTS_LIST`, `IO_RESOURCE_LIST`,
  `IO_RESOURCE_DESCRIPTOR`, the `IO_RESOURCE_*` options, the
  `CM_RESOURCE_*` flags, `IRP_MN_FILTER_RESOURCE_REQUIREMENTS` 0x0D and
  its `Parameters.FilterResourceRequirements`), read 2026-10-27.
- **[doc]** Microsoft's public DDK reference for the PnP IRPs named here
  (what a driver may do with the IRP and who owns the list), as this
  project's earlier records cite it (records 18 section 4.2 and 21 section
  6.2.1).
- **[spec]** the PCI-to-PCI Bridge Architecture Specification and the PCI
  Express Base Specification's Type 1 header and PCI Express capability,
  as records 21 and 32 cite them (not held in this repository).
- **[code]** this tree's source at `6f250605`.
- **[emu]** the `usb4-emu` emulator, as record 34 cites it: a test bed,
  not hardware.
- **[linux]** what Linux does, facts only, under `AGENTS.md`'s
  "Reference Implementations".
- **[inf]** an inference: a hypothesis the design is written to tolerate
  being wrong, and that a named leg or reading settles.

## 1. What it is for

### 1.1 The problem

Task 44.63 read it on q35 with the emulator's dock (a TI XIO3130 switch,
`104C:8232` upstream and three `104C:8233` downstream ports, behind them
an NVMe drive, an e1000e NIC and a qemu-xhci) hot-plugged behind a
tunnel port of the emulator's ASM4242 layout [run, emu]: on XP the
upstream port and the first downstream port start, the second and third
downstream ports end at problem 12 and nothing behind them starts; on
2000 the placement and the trace are the same. On 7 x86 the same dock in
the same window is placed whole. An enclosure with one downstream port is
placed on XP. Section 2 has the facts.

The release notes of `3.3.0.0` carry it as a limitation (44.63's text,
the owner's option C). This record is the fix for a later release.

### 1.2 Scope

In:

- Windows 2000 SP4, XP SP3 and XP x64 (NT 5.0, 5.1 and 5.2), both
  architectures where the target has both.
- Every PCI-to-PCI bridge below an active tunnel port (section 4.4): a
  dock's or enclosure's switch, and a chained device's switch behind
  record 34's chain port.
- The registered attachment that keeps the child filter in each such
  bridge's stack across its first-time install (section 4).
- The list rewriter, its rules and its refusals (sections 5 and 6), the
  diagnostic (section 9), the service's and the installer's parts
  (sections 4.3 and 4.5), the tests and legs (section 10).

### 1.3 Not in scope

- Vista and later: never touched (section 7.8).
- Windows 98 SE and ME: their bridges below a tunnel port are numbered
  and given windows by this project's own numbering (record 21 section
  6.1.1), not by fixed lists; section 8.5 says what is known there.
- Any write to a bridge's configuration registers on NT. Bus numbers and
  windows stay `pci.sys`'s; this record only changes what a bridge asks
  for (section 11, option B, says why).
- Bus-number shortage. A tunnel port with no spare buses (the emulator's
  q35 `tb3ar` tunnel port takes no bus reservation, so a switch
  hot-plugged there gets no buses on any system [run, 44.64 Part A]) is
  out of this record's reach: nothing below the upstream port is ever
  enumerated.
- The windows a function's own BARs ask for, and a function's
  interrupt: `pci.sys`'s, and on 2000's Standard PC HAL record 21
  section 6.2.1's, untouched (section 8.3).

## 2. The facts

### 2.1 What 2000 and XP did with the hot-added dock [run]

From 44.63 (evidence `E:\xhci98-vm\p44\p44-xpdock\`, decoded lists in
`xp\reqs-a.txt` and `w2k\reqs-w.txt`, traces `xp\trace-a.log` from line
1123 and `w2k\trace-w.log` from line 926):

1. XP's and 2000's PCI driver numbered the buses below the hot-added
   switch themselves, within the tunnel port's range (XP: the upstream
   port 4 to 1Bh, its downstream ports 5, 10h and 1Bh).
2. Each of the four bridges, found with its windows closed, got the same
   requirement list whatever was behind it:
   - **XP**: one alternative list of 18 descriptors; an I/O descriptor of
     4 KB, alignment 4 KB, range 0 to FFFFh, `Option` 0 and no
     alternative; a memory descriptor of 64 MB, alignment 1 MB, range 0
     to FFFFFFFFh, followed by `IO_RESOURCE_ALTERNATIVE` descriptors of
     32, 16, 8, 4, 2 and 1 MB; a prefetchable memory group of the same
     ladder with `CM_RESOURCE_MEMORY_PREFETCHABLE` and the range 0 to
     FFFFFFFFFFFFFFFFh; each window group followed by a device-private
     descriptor of the PCI driver's, data `(1, 2, 0)` after the I/O
     group, `(1, 3, 0)` after the memory group, `(1, 4, 0)` after the
     prefetchable one; `ListSize` 268h. I/O `ShareDisposition` 1 (device
     exclusive), flags `B1h` (`CM_RESOURCE_PORT_IO`, `_16_BIT_DECODE`,
     `_POSITIVE_DECODE`, `_WINDOW_DECODE` [ddk]); memory
     `ShareDisposition` 0, flags 0, prefetchable flags 4.
   - **2000**: the same three groups in the same order, each a single
     descriptor with `Option` 0 and no alternative: 4 KB of I/O, 1 MB of
     memory and 1 MB prefetchable, alignments 4 KB and 1 MB,
     `ShareDisposition` 1 for all three, with the same private
     descriptors; `ListSize` E8h.
3. `FilteredConfigVector` equalled `BasicConfigVector` on every bridge:
   no filter changed a list. On the upstream port `xhcicm` was a lower
   filter (the child filter of record 31 section 4.3).
4. The upstream port was placed at the top of the tunnel port's windows
   (XP: I/O `3000h` 4 KB, memory `E2000000h` 64 MB, prefetchable
   `CD200000h` 2 MB, out of the tunnel port's 12 KB, 384 MB and 2 MB).
   One downstream port then took the upstream port's whole windows; the
   PnP manager's attempts to place a second stopped the first. The third
   was never given windows.
5. A bridge the firmware configured at power-on was kept as found:
   SeaBIOS gave each downstream port windows of its own, XP kept them and
   placed the dock (44.45's `rf` leg).

### 2.2 A bridge with an earlier assignment [run]

From 44.64 Part A (evidence `E:\xhci98-vm\p44\p44-chain\xp\serial-b.log`,
decoded 2026-10-27 with 44.63's decoder): a chain first laid out by the
firmware at power-on, then unplugged and plugged in again while XP ran,
came back with the same instance ids, and XP's lists were:

- the upstream port: memory `IO_RESOURCE_PREFERRED` 4 MB at its earlier
  window `FE000000h` (alignment 1, the range exactly that window), then an
  alternative of 4 MB anywhere (alignment 4 MB), then the private
  `(1, 3, 0)`; prefetchable the same at `FD000000h` with `(1, 4, 0)`;
- each downstream port: 2 MB and 2 MB the same way, its earlier window
  first;
- **no I/O descriptor and no `(1, 2, 0)` on any of the three**;
- `FilteredConfigVector` equal to `BasicConfigVector`, `AllocConfig` each
  at its earlier window; both downstream ports started, no problem 12.

2000's configured shape, read in 44.63 on the bridges it had placed, is
the same form: the current window with `IO_RESOURCE_PREFERRED`, then the
same size anywhere with `IO_RESOURCE_ALTERNATIVE`, then the private
descriptor.

Two things follow:

- **XP's PCI driver itself omits a closed window's group, its private
  descriptor with it, and starts that bridge with the window closed.** So
  a list with a group removed is a shape that driver produces and accepts
  [run]. That it accepts one for a bridge it first found closed is not
  read [inf: section 10.2, leg R1].
- **The fixed list is met only by a bridge found closed with no earlier
  assignment.** Where the windows came back from is not settled: the
  emulator's tunnel slot gates a re-plugged switch rather than resetting
  it (record 34 section 2.1 [emu]), so the earlier windows may have been
  read from the switch's own registers, not remembered by XP [inf]. A
  real switch that lost power comes back from reset with its windows as
  its reset leaves them [spec], so a re-plug on hardware is expected to
  meet the closed shape again [inf]. The failing case is therefore a
  never-seen switch (or one reset by its unplug) hot-added on a port with
  spare buses: 44.63 on the ASM4242 board layout.

"Is there an earlier assignment to reuse?" No, not one this record can
rely on: on hardware there is none in the registers, and XP's
`BasicConfigVector` of the bridges it had placed in 44.63 still held the
closed shape after their installs [run], so it keeps none in a form a
filter could read. What there is to **imitate** is the shape: the
configured list's omission of a window nothing needs, and its ladder of
one preferred size and alternatives. Section 6 builds on both.

### 2.3 The list, as the registry records it [ddk, run]

`LogConf\BasicConfigVector` is the `IO_RESOURCE_REQUIREMENTS_LIST` the
PCI driver answered to `IRP_MN_QUERY_RESOURCE_REQUIREMENTS`;
`Control\FilteredConfigVector` the list after the stack's
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS`; `Control\AllocConfig` the
`CM_RESOURCE_LIST` the arbiters gave [run: the three values read on XP
and 2000 in 44.63]. The layout [ddk, `wdm.h`]:

- `IO_RESOURCE_REQUIREMENTS_LIST`: `ListSize` (ULONG, the whole list in
  bytes), then three ULONGs that `wdm.h` names `DoNotUse1` to `DoNotUse3`
  (`ntddk.h`'s `InterfaceType`, `BusNumber`, `SlotNumber`; the vectors
  read 5, `PCIBus`, and the bridge's bus and slot), `Reserved[3]`,
  `AlternativeLists` (ULONG), then the first `IO_RESOURCE_LIST` at byte
  32.
- `IO_RESOURCE_LIST`: `Version`, `Revision` (USHORTs; the vectors read 1
  and 1), `Count` (ULONG), then `Count` descriptors.
- `IO_RESOURCE_DESCRIPTOR`, 32 bytes on x86 and amd64: `Option`, `Type`,
  `ShareDisposition`, `Spare1` (UCHARs), `Flags`, `Spare2` (USHORTs), then
  a 24-byte union: for a port or memory range `Length`, `Alignment`
  (ULONGs), `MinimumAddress`, `MaximumAddress` (`PHYSICAL_ADDRESS`); for
  a device-private descriptor `Data[3]`. `Length` is a ULONG, so one
  descriptor cannot ask for 4 GB or more.
- `Option` [ddk]: 0 for a required descriptor, `IO_RESOURCE_PREFERRED`
  (01h), `IO_RESOURCE_DEFAULT` (02h), `IO_RESOURCE_ALTERNATIVE` (08h). A
  descriptor with `IO_RESOURCE_ALTERNATIVE` is an alternative to the
  nearest descriptor before it that is not one; together they are one
  resource with a choice of forms, and the arbiter takes the first that
  fits [doc].
- `Type` [ddk]: `CmResourceTypePort` 1, `CmResourceTypeInterrupt` 2,
  `CmResourceTypeMemory` 3, `CmResourceTypeDevicePrivate` 129 (81h).
- **Alternative lists**: `AlternativeLists` complete configurations in
  order of preference [doc]. This record writes one list per bridge
  (section 6.3), so how 2000's and XP's assignment of a batch falls back
  between lists does not matter to it.
- The device-private descriptors are the PCI driver's own and are not
  documented. Read in every list of 44.63 and 44.64: one follows each
  window group, `Data` `(1, 2, 0)` for the I/O window, `(1, 3, 0)` for
  memory, `(1, 4, 0)` for prefetchable, and a group absent from a
  configured list has its private descriptor absent too [run]. This
  record treats the pair as one unit and never invents, changes or
  reorders a private descriptor (section 6.1).

The existing pure code reads the same layout as bytes: `cm_irq.h`'s
offsets (`CM_IRQ_REQ_HDR` 32, `CM_IRQ_REQ_ALTS_AT` 28, `CM_IRQ_IOL_HDR`
8, `CM_IRQ_IOD_SIZE` 32), asserted against the DDK's structures in
`cm_filt.c` for x86 and amd64 [code].

### 2.4 Which IRP, and when [doc, run, code]

- `IRP_MN_QUERY_RESOURCE_REQUIREMENTS` goes to the device's stack when
  the PnP manager first processes a new devnode; the PCI driver as bus
  driver answers it with a list it allocates; the PnP manager records it
  as `BasicConfigVector` [doc, run].
- `IRP_MN_FILTER_RESOURCE_REQUIREMENTS` (0x0D) goes to the top of the
  device's stack once it is built, before `IRP_MN_START_DEVICE`, and again
  whenever the PnP manager assigns the device's resources afresh (a
  rebalance, a restart after a stop) [doc]. `IoStatus.Information`
  carries the list on the way down,
  `Parameters.FilterResourceRequirements.IoResourceRequirementList` the
  original [ddk]; a driver that changes it allocates a new list from
  paged pool, frees the one in `IoStatus.Information` and completes with
  the new one there [doc]. The result is `FilteredConfigVector` [run].
- The child filter already handles the IRP for one case: on 2000 with
  the Standard PC HAL (`CmIrqGate`) it adds an interrupt to a function's
  list (`cmChildReqFilter`, record 21 section 6.2.1, task 44.53), only
  when the lower driver returned success or `STATUS_NOT_SUPPORTED` with a
  list, the list replaced from paged pool (`cmIrqReplace`,
  `CmAllocPaged`) [code].
- **The order against the bus numbering, read in the traces** (XP's
  `trace-a.log` and 2000's `trace-w.log`, the same order on both): the
  upstream port's bus numbers were written (secondary 4, subordinate 4)
  well before its start (the window writes and Command 7); the downstream
  ports' secondary buses were written (5, 10h, 1Bh, each with its
  subordinate equal, and the upstream port's subordinate widened to 1Bh)
  after the upstream port's start and before the first downstream port's
  start; the first read of bus 4 came after the upstream port's start
  [run]. So, at the moment each bridge's requirements are filtered:
  - for the **upstream port**, its own secondary bus is numbered and
    reachable, so its downstream ports can be read and counted, but
    their secondary buses are 0, so nothing behind them can be read;
  - for a **downstream port**, its own secondary bus and its siblings'
    are numbered, so the functions directly behind each sibling can be
    read (their headers and BAR type bits), but not sized (sizing a BAR
    writes it, and the PCI driver owns it).
  That the filter IRP comes after those bus writes is the [inf] the
  traces suggest and do not show (the trace has no IRPs); leg R1 reads it
  (the filter logs the bridge's bus-number register when it sees the
  IRP), and section 13.2 has the fallback.

### 2.5 How 2000 and XP differ [run]

| | 2000 | XP |
|---|---|---|
| Closed bridge, I/O | 4 KB, required | 4 KB, required |
| Closed bridge, memory | 1 MB, required | 64 MB preferred, alternatives down to 1 MB |
| Closed bridge, prefetchable | 1 MB, required | 64 MB preferred, alternatives down to 1 MB |
| Memory `ShareDisposition` | 1 | 0 |
| A bridge it placed, after its install | the current window preferred, the same size as the alternative (44.63) | the closed list still recorded (44.63) |
| A bridge found configured | kept (2000's chain at power-on started, 44.64) | kept (44.63, 44.64) |
| The filter INF against `machine.inf` for a bridge | ranked unforced (record 21 decision D2) | the unsigned INF ranks `8005h` against `machine.inf`'s signed `2004h`, and `machine.inf` is kept unless the install is forced (record 21 section 3.3, decision D2) |

So on 2000 the memory windows fail as well as the I/O (a 1 MB upstream
window cannot hold two 1 MB downstream windows); on XP memory has a
ladder and it is I/O, and a small prefetchable parent window, that fail.

XP x64 (NT 5.2) is not read: its PCI driver is Server 2003's, not XP's
[inf: the same family; its closed list may differ, section 7.7].

### 2.6 What Windows 7 does, and Linux [run, linux]

7 x86 placed the same dock in the same window, a window per downstream
port and the 4 KB of I/O to the NIC's port alone (44.45 continued, 2)
[run]; that it sizes a hot-added bridge from what is behind it is read
from that outcome only [inf]. Linux, for a hot-plug bridge, distributes
the space its parent has left among the bridges below it and sizes each
window from what is behind it (`drivers/pci/setup-bus.c`, the
distribution of available resources to hot-plug bridges) [linux]; this
record's rules are of the same kind but cannot size from below
(section 2.4).

## 3. Design overview

Three pieces, in the order they are needed:

1. **The registered attachment** (section 4). On 2000 and XP each
   bridge's first-time install rebuilds its stack and the kernel-attached
   child filter is lost (record 31 section 4.3, read 2026-10-25). The
   mark service (record 21 section 7.4 revision 3) writes a device
   `LowerFilters` value naming `xhcicm` on every bridge below a unit once
   their installs are done, and restarts the unit once where a bridge
   below it is at problem 12 or runs without this driver's attachment,
   so the bridges start again with the filter registered.
2. **The rewriter** (sections 5 and 6): a pure function over the list
   the PCI driver answered and a snapshot of the bridge's surroundings
   read from configuration space, which returns a new list or "leave it".
3. **The glue** in the child filter's `IRP_MN_FILTER_RESOURCE_REQUIREMENTS`
   on a bridge (section 5.1), the version gate, the switch and the
   diagnostic (section 9).

## 4. The prerequisite: a registered attachment on every bridge below a tunnel port

### 4.1 What is lost, and when [run, code]

- The bridge filter (role 2) is a registered `UpperFilters` filter of
  every bridge `xhcicmf.inf` names; it attaches a child filter to each new
  PDO in that bridge's `BusRelations` answer, before the answer is
  completed; a child on a bridge does the same for that bridge's
  `BusRelations` (`cmChainRelations`, `CmRegChainRelations`), to
  `CM_TUNNEL_DEPTH_MAX` (record 31 sections 4.1 and 4.3).
- The first enumeration of a never-seen switch runs its bridges from the
  Critical Device Database (`pci#cc_0604`, the PCI driver) with the
  kernel-attached children in place: the chain tags the NVMe function at
  depth 3 on its first enumeration (record 31 section 4.3, read on 2000
  and XP). So **this record's rewrite already acts at that first start**
  [inf: the lists of 44.63 were filtered with a child on the upstream
  port, `xhcicm` as its lower filter].
- Each bridge's first-time install (`machine.inf`) then rebuilds its stack
  from the registry, the kernel-attached child is gone, and no
  `BusRelations` of the parent reaches a filter of this driver
  afterwards, so nothing re-attaches (record 31 section 4.3) [run]. The
  rebuilt stack's start is filtered again with no child: on XP the list
  is again the closed one (section 2.5), and the dock is back at 44.63's
  placement.
- On a re-plug of a known switch its bridges are not installed again and
  the chain is intact (record 31 section 4.3) [run], so only the session
  of a never-seen switch's first plug needs more.
- Windows' driver installation deletes a device's own `LowerFilters`
  value (record 21 section 7.4 revision 3, read on XP in 42-V.2), and an
  install of a boot-critical class copies the device's filter values into
  the Critical Device Database entry of the id it matched, so a filter
  value present on a bridge at its install would reach every later
  bridge (record 21 section 7.4, read on 2000, XP and 7 x86 in 42-A.24)
  [run].

So the attachment must be registered after each bridge's first-time
install, on every bridge of the switch, and on nothing else.

### 4.2 The choices

- **R1. A tunnel bridge id and the filter INF.** The child would add an
  id of this project's own, first among a bridge's compatible ids, and
  the filter INF would match it with the PCI driver's install and a
  `LowerFilters` value, so the first-time install itself registers the
  filter. Not chosen: on XP the unsigned filter INF ranks `8005h` against
  `machine.inf`'s signed `2004h` and Windows keeps `machine.inf` unless
  the install is forced (record 21 section 3.3, decision D2), so the
  first-time install would still be `machine.inf`'s and drop the filter;
  forcing it needs a per-device install of an unsigned driver by
  something with a user present; on 2000 it would cost one Found New
  Hardware wizard and signing prompt per bridge; and the install would
  copy the INF's filter value into a Critical Device Database entry
  (section 4.1).
- **R2. Device `LowerFilters` written by the mark service after the
  installs (chosen; the owner's answer to Q1).** The service already writes the
  value on the unit - for a dock or a switch enclosure, its switch's
  upstream port - and restarts a unit on NT 5.x under a gate (record 21
  section 7.4 revision 3; record 31 section 6.2). Extended to every
  bridge below a unit, written only once that bridge's install is done,
  and one restart of the unit where a bridge below it is at problem 12
  or runs without this driver's attachment (section 4.3). Silent: no INF of this project's is installed on the
  bridges, `machine.inf` stays their driver, and the value is written
  after the install that would have deleted or copied it.
- **R3. A class filter** (`LowerFilters` on the System class key). Every
  System-class device in the machine - the HAL's, the PCI bus's, every
  root port and bridge - would load `xhcicm` and call its `AddDevice` at
  boot, which would have to decline all but a few. A fault there makes
  the machine unbootable. Rejected.
- **R4. Re-attaching after the loss**: asking the parent's
  `BusRelations` again when a child sees its device's non-surprise
  removal. A filter can be attached to a PDO only before the PnP manager
  builds that PDO's stack; the rebuilt stack is already built when the
  parent's answer comes back, so an attach then lands above the PCI
  driver's FDO, not below it. Rejected.
- **R0. No registered attachment**: the rewrite acts at the Critical
  Device Database start of a never-seen switch and at every re-plug of a
  known one (the kernel chain), and not between the first installs and
  the next re-plug. What remains where R2 cannot act: safe eject off
  (section 12, Q6), or a restart refused.

### 4.3 The mark service's part (R2)

Record 21 section 7.4 revision 3's service, on 2000, XP and XP x64 only:

- **Which devices.** Every PCI-to-PCI bridge in a unit's subtree (record
  24 section 4.3's unit: the device at the top of an active tunnel port's
  secondary bus), the unit itself when it is a bridge, to the depth
  bound, read through the service's existing walk of the unit's subtree
  (`CM_Get_Child`, `CM_Get_Sibling`) and identified as a bridge by its
  compatible id `PCI\CC_0604`. Never the tunnel port, a root port, a
  host's own switch port, or any bridge outside such a subtree.
- **When the value is written.** A bridge whose devnode has a driver
  (its `Driver` value present), no installation pending
  (`CONFIGFLAG_REINSTALL` and `CONFIGFLAG_FINISH_INSTALL` clear), and no
  `xhcicm` in its `LowerFilters`: the service appends `xhcicm` (the
  other names kept), as it writes the unit's mark, and records it as a
  **bridge mark** (its kind beside the unit marks; the same records, the
  same reconciliation). A bridge with an install pending is left for a
  later poll.
- **The restart.** When every bridge below the unit has its bridge mark
  and at least one of them **needs** the restart, the service restarts
  the unit once, through the documented restart of one device
  (`SetupDiCallClassInstaller(DIF_PROPERTYCHANGE)` with
  `DICS_PROPCHANGE`) its other restarts use (record 21 section 7.4
  revision 3), so every bridge below starts again with the filter
  registered and its list rewritten. A bridge needs it when it is at
  problem 12, or when it is started and **its attachment is missing**
  by the bridge inventory below. Problem 12 alone is not enough: a bridge
  that kept the windows its first start gave it (2000's configured
  shape, section 2.5) is placed, yet its rebuilt stack has no filter,
  and a value written after its start changes only the registry - a
  chain port so left has no filter in the session (record 34 section
  3.2). The gate: the existing restart gate of record 31 section 6.2,
  widened for this purpose (the owner's answer to Q1) to allow started
  functions behind the unit that are not storage (a dock's xHCI or
  Ethernet) and to refuse when a disk or volume below the unit is
  started; the budget, once per arrival of the unit, shared with the
  other restarts (a restart made for one reason serves the others). A
  refused restart is logged with the release notes' re-plug sentence.
- **The bridge inventory** (the attachment test). Not the unit's marks
  entries: those are sized for a member's immediate children
  (`CM_QM_MAX_CURRENT`, 32 a member), and `mk_restart_wanted` also asks
  for a restart when an attachment has not answered `Removable`
  (`MK_EF_REMOVABLE`), which a child on a nested bridge never does
  (`CmPortsMayMark` admits only a device directly on the member's
  secondary bus [code]). So the query device's marks answer (record 25
  section 4) gains a separate list, beside the entries and tombstones
  and never mixed with them: for each active member, the location (bus,
  device and function) of every child filter of this driver that is
  attached, located, not removed, and on a bridge below that member
  (section 4.4's ancestry), whichever way it came (kernel-attached or
  registered); at most `CM_QB_MAX_BRIDGES` (16) a member, with a
  **complete** flag set only when every such attachment was listed and
  located. The unit's own predicate is unchanged. A separate predicate,
  `mk_bridge_attached`, answers for one bridge devnode below the unit,
  by its location:
  - **attached**: its location is in the member's list;
  - **missing**: the list is complete and its location is not in it -
    no filter of this driver is on that bridge;
  - **unknown**: the list is incomplete, the answer could not be read,
    the bridge's location could not be read, or the driver is one
    without the list (an older build). Unknown never asks for a
    restart and never spends the budget; the poll looks again.
- **The bridge marks first.** On 2000, XP and XP x64 the unit's other
  restarts (record 21 section 7.4 revision 3's and record 31 section
  6.2's) wait while a bridge below the unit has its install pending or
  its bridge mark not yet written, so the one restart of the shared
  budget is made with every bridge mark in place and serves them all.
  The wait is bounded: `MK_BRIDGE_WAIT`, 60 seconds from the unit's
  arrival as the service first saw it. Its expiry lifts only this
  barrier (the bridge marks all written): every other condition of a
  restart still holds at the moment of the call and defers it, without
  spending the budget, while it fails - no install pending on any
  devnode below the unit (as `mk_restart` and record 31 section 6.2
  require), every state read, no disk or volume below started, and the
  unit's identity checked afresh. A bridge marked after an expired wait
  gets no restart of its own in that arrival, and the service logs it
  with the re-plug sentence.
- **Its life.** A bridge mark is removed when the bridge leaves (the
  service's tombstones), when the unit is no longer behind an active
  member, and by the uninstall and every cleanup that removes unit marks
  (record 23 section 9.2's step 0, `mk_remove_safe_off` with safe eject
  off): bridge marks are the service's like unit marks, never an INF's,
  so no cleanup rule needs an exemption. With `XhciCmSafeEject` 0 the
  service is not installed and there are no bridge marks; this record's
  first-plug session then falls back to R0, "unplug and plug in again"
  (the owner's answer to Q6).
- **The Critical Device Database.** A bridge mark is written after the
  bridge's install, so that install copies nothing. A later reinstall of
  a marked bridge (a forced `machine.inf` update) copies the value into
  `pci#cc_0604`, which the service already takes out at every poll
  (record 21 section 7.4) [run].
- **Reconciliation.** The service's inventory keeps an unrecorded
  device-level `xhcicm` mark only on a unit behind an active member and
  removes it elsewhere; it gains "or a bridge in such a unit's subtree"
  and records the mark as a bridge mark.

### 4.4 Scope: only bridges below an active tunnel port

The rewrite acts in a child filter, which can sit on bridges that are
not below a tunnel port: record 31 section 4.1 attaches children below a
discrete controller's filtered upstream switch port, to the host's own
downstream switch ports, its NHI and its xHCI. So "below a bridge
filter" is not the boundary. The boundary is the **active member**: a
bridge filter whose port the NHI verified and assigned as a tunnel port
(`cmRegIsActiveMember`, as `CmRegIrqWire` uses it [code]).

- **How a child knows.** It walks its ancestry under `cmRegLock`
  (`Parent`, else `Bridge` for a child with no `Parent`, as `CmRegIrqWire`
  walks it [code]) and acts
  only when it meets an active member **above** its own device, within
  `CM_TUNNEL_DEPTH_MAX` levels. A host's downstream switch port is the
  member itself, not below one, and is never rewritten; its kernel child
  meets the host's upstream switch port's filter, which is no member.
  A host vector of the walk covers a discrete controller's topology
  (section 10.1).
- **A registered child's ancestry.** `CmRegAttLink` associates a child
  made by `AddDevice` with the filter whose last snapshot reported its
  PDO, but searches only the published bridge filters (`cmRegFilters`)
  [code], so a registered downstream port below a child-filtered upstream
  port would stay unassociated. It gains a second search, when the first
  finds none, over the snapshots of the child filters on bridges
  (kernel-attached or registered; each keeps one of its bridge's
  `BusRelations` answer, record 31 section 4.3), on the attachment list
  under the same lock. The two fields keep their meanings [code]:
  - the found child becomes the registered child's **`Parent`** (its
    immediate ancestor, as a chain child's `Parent` is the filter whose
    `BusRelations` attached it), and its depth is that child's depth
    plus one;
  - its **`Bridge`** stays the chain's root, the tunnel-port bridge
    filter: the found child's `cmTopOf`, as `cmChildCreate` names a chain
    child's (`top = cmTopOf(parent)`). `cmTopOf` returns a child's
    `Bridge` without walking, and `cmChildCreate` and the chain log's
    domain (`cmLogDom(cmTopOf(f))`) rely on it naming the root, so the
    `ChildLive` count goes to the root as for any chain child.
  The ancestry walk then goes through `Parent` as `CmRegIrqWire`'s does.
  `Parent` outlives the registered child (a device's children are
  removed before it), and the found child's removal clears the
  association of any registered child that names it. A child not
  associated by its first filter IRP acts as a pass-through
  (`cm.breq.nobridge`).
- **A registered child's role.** `cmAddDevice` decides roles by the
  PDO's hardware ids, and `CmRoleDecide` makes any id the filter INF lists
  a bridge filter [code]. A dock's switch can carry such an id (the TS3
  Plus's switch is an Alpine Ridge, `8086:15D3`, record 21). The order:
  1. **The NHI first, by its ids, unchanged**: `CmRoleDecide`'s
     `CM_ROLE_NHI` (an id of the NHI table or the USB4 host class) stands
     whatever the stack holds. The NHI's own `AddDevice` finds no function
     driver above its PDO, because this driver is that function driver;
     a position rule applied to it would make it a child and no
     connection manager would start.
  2. **Then position, for a bridge only**: where `CmRoleDecide` answers
     `CM_ROLE_FILTER` (a class 0604 device the filter INF lists), an
     `AddDevice` that finds no PCI driver's device above the PDO (the PDO
     is the top, or the top is one of this driver's) is a lower filter,
     the child role; one that finds the PCI driver's FDO above it is the
     upper filter, the bridge role (and `cmBridgeOnStack`'s existing
     check applies to it as now).
  3. Every other answer (`CM_ROLE_CHILD`) is the child, as now.
  Host vectors cover an INF-listed bridge in both orders and the NHI with
  nothing above its PDO (section 10.1).
- **Which systems.** NT 5.0, 5.1 and 5.2, read once at the driver's start
  from `SharedUserData` (`NtMajorVersion` 5), as `CmReadIrqGate` reads
  it [code]; never on Vista and later. The service's part runs on 2000,
  XP and XP x64 only.
- A child that is not below an active member - the inert mode
  (`XhciCmMode` 0), a firmware-approved tunnel the NHI did not verify, a
  bridge outside any tunnel - passes every request this record adds
  through.

### 4.5 The installer (records 23 and 26)

- **Install and update.** Nothing new is installed: the service and the
  filter are already the Thunderbolt component's (record 23 section 5;
  record 26 section 6.1). No INF changes.
- **The window's texts.** None new. The release notes' 2000 and XP
  paragraph changes from 44.63's limitation to Q1's text (section 12).
- **Uninstall.** Record 23 section 9.2's step 0 already takes `xhcicm`
  out of every device's `LowerFilters` and verifies it before the service
  and the filter are deleted; bridge marks are among them. A bridge left
  naming a deleted `xhcicm` would fail its next start (Code 39), which
  that verification prevents; leg R12 reads it.
- **`XHCICLN`** lists such values as it lists the unit marks (record 21
  section 7.4), unchanged. (Historical: XHCICLN was dropped in `3.3.0.0`.)

### 4.6 What it also closes

- Record 31 section 4.4's first case: the first plug of a never-seen
  switch enclosure on 2000 and XP arrives untagged, because the bridges'
  installs drop the chain. The bridge marks' restart re-creates the NVMe
  through registered children, so it is tagged [inf: leg R5]; record 31
  section 6.2's own restart then finds nothing to do.
- Record 34 section 3.2's "no filter on the chain port" case on 2000 and
  XP: after the restart a chain port of a never-seen dock keeps a filter
  in the session of its first plug.

## 5. The rewrite: where it runs and what it reads

### 5.1 The handler

The child filter, on `IRP_MN_FILTER_RESOURCE_REQUIREMENTS`, for a PDO it
knows to be a PCI-to-PCI bridge (`cmChainKindOf` is `CM_CHAIN_BRIDGE`)
below an active member (section 4.4), on NT 5.x, with the switch on
(section 9.1), and only in the lowest of this driver's filters in the
stack (section 6.1):

1. forwards the IRP and waits (`CmForwardSync`), as `cmChildReqFilter`
   does, and reads the list afresh from `IoStatus.Information` (a driver
   below may have replaced it);
2. goes on only when the status is a success or `STATUS_NOT_SUPPORTED`
   and the list is not null, the guard `cmChildReqFilter` has; any other
   result is completed as it came;
3. builds the snapshot of section 5.2 through `cmCfgRead`
   (`HalGetBusData`, PASSIVE_LEVEL, reads only);
4. calls the pure rewriter (`CmBreqRewrite`, `cm_breq.c`) with the list,
   the snapshot and the system's minor version; it returns "leave" with a
   reason, or the size of the new list;
5. on a new list: allocates it from paged pool (`CmAllocPaged`), has the
   rewriter write it, frees the old one (`CmFree`), puts the new one in
   `IoStatus.Information` and completes with `STATUS_SUCCESS`; an
   allocation failure completes with the old list and the lower status
   (`cm.breq.nomem`);
6. records the attempt (section 9.2).

The start that follows is passed through untouched: the PCI driver
reads its windows from the assignment through its private descriptors,
and the filter records only the start's status (section 9.2).

### 5.2 The snapshot

Read at the filter IRP, each read a dword through `cmCfgRead`; any read
failure, a function answering Configuration Request Retry Status, or a
header that changed between two reads makes the item **unknown**, never
absent:

- **This bridge**: its location (`CmRegAttLocate`), its header type, its
  PCI Express capability's Device/Port Type (upstream switch port 0101b,
  downstream switch port 0110b; anything else - a root port, a PCI
  Express-to-PCI bridge, no capability - is **other**) [spec], its Slot
  Capabilities (Hot-Plug Capable, for a downstream port with Slot
  Implemented) [spec], and its bus-number register.
- **Its parent's windows**: the bridge whose secondary bus is this
  bridge's bus - the next filter up the ancestry walk of section 4.4,
  whose location is known - read from its I/O, memory and prefetchable
  base and limit registers (1Ch with 30h, 20h, 24h with 28h and 2Ch)
  [spec], each address kept as two `ULONG`s, high and low (section 6's
  arithmetic); a window is open when its base is not above its limit,
  compared high first, then low. These are what the PCI driver programmed at the parent's
  start, which precedes the child's enumeration [run, section 2.4]. Read
  from the registers rather than from the parent's `IRP_MN_START_DEVICE`
  resources because every parent has registers, including one whose
  filter was not in its stack at its start; a window that is not open
  counts as 0 of that kind.
- **Alone**: whether this bridge is the only function on its parent's
  secondary bus (functions 0 to 7 of devices 0 to 31 read; for a PCI
  Express downstream port only device 0 can answer [spec]).
- **For an upstream port**: whether its own secondary bus is numbered
  (secondary not 0 and not FFh, subordinate not below secondary), and if
  so how many of its functions are downstream ports, N.
- **For a downstream port**: the same for each **sibling** (every
  downstream port on its own bus, itself included, in device and function
  order): numbered or not, hot-plug capable or not, and the functions on
  the sibling's secondary bus: none, type 1 (a bridge: a nested switch),
  or type 0 with the type bits of each BAR. Those bits are read-only
  [spec], so they read the same before a BAR is assigned: bit 0 set is
  an I/O BAR; a memory BAR with bit 3 set is prefetchable. The BARs are
  walked by the header's own bounds [spec]: a type 0 function's six
  (10h to 24h); a type 1 function's two (10h and 14h) are not read, its
  claim being the table's nested-switch row. A memory BAR's bits 2:1
  give its width: 00b (and the old below-1 MB 01b) one dword; 10b a
  64-bit BAR, whose next dword is its upper address and is **skipped**,
  never read as a BAR of its own (an assigned upper dword can have bit 0
  or bit 3 set and would invent an I/O or prefetchable claim); 11b
  (reserved), or a 64-bit BAR in the last slot with no dword after it,
  makes that function's claims unknown. A memory BAR
  that is not prefetchable and not yet assigned can read 0, as an
  unimplemented BAR does (the e1000e's BAR0 and BAR1 read 0 before their
  sizing, `xp\trace-a.log` lines 29882 and 29883 [run]), so a 0 is never
  read as "no memory".

From this, for each sibling and each kind of window, a **claim**:

| Behind the sibling | I/O | Memory | Prefetchable |
|---|---|---|---|
| nothing, not hot-plug capable | no | granule only | no |
| nothing, hot-plug capable (a chain port, record 34 section 3.1) | unknown | yes | yes |
| type 0 functions | yes if any BAR has bit 0 set | yes, always | yes if any memory BAR has bit 3 set |
| a type 1 function (a nested switch) | unknown | yes | yes |
| not readable (bus not numbered, read failed) | unknown | yes | yes |

Memory is claimed for every function because a 0 cannot be told from an
absent BAR without writing it; a memory claim costs a share of a window,
never a failure. "Granule only" keeps one 1 MB memory window on an empty
port rather than none (the owner's answer to Q3): a bridge answered with
no window at all is a list neither system was read producing for a
bridge [inf], and section 6.1 keeps the rewriter from ever writing one. A
prefetchable claim counts as a memory claim instead when the upstream
port has no prefetchable window (the PCI driver may then place such a
BAR in the memory window [inf; Linux does, `setup-bus.c`, linux]).

## 6. The rules

Symbols: for each kind of window k (I/O, memory, prefetchable) the
**granule** g(k) is 4 KB for I/O and 1 MB for memory and prefetchable
(a bridge window's alignment and step [spec]); floor_g(x) rounds down to
a multiple of g(k). P(k) is the parent's window of kind k, capped at
`CAP` = FFF00000h (4095 MB, the largest multiple of 1 MB a `ULONG`
`Length` holds) for memory and prefetchable. W(k) is the upstream port's
window, capped the same way (for a downstream port the parent is the
upstream port, so W = P there). "The OS list" is the list the PCI driver
answered.

**The arithmetic is 32-bit** (`AGENTS.md`'s rule against 64-bit
arithmetic in the shared driver, record 21): no 64-bit integer type or
operation in `cm_breq.c` or the snapshot's code. An address is a pair of
`ULONG`s, high and low (the high half 0 for the memory window and for a
prefetchable window that does not decode 64 bits).

- **Open**: base not above limit, compared as (high, low): the high
  halves first, the low halves only when the highs are equal.
- **Length** of an open memory or prefetchable window: D = limit - base,
  taken as a pair with a borrow (low = limit.low - base.low, borrow when
  limit.low < base.low; high = limit.high - base.high - borrow). The
  limit's low 20 bits are FFFFFh and the base's 0 [spec], so D is the
  length less one and never overflows. Then P = `CAP` when D.high is not
  0 or D.low is FFEFFFFFh or more, else D.low + 1. The `+ 1` is made
  only below that bound, so it cannot wrap.
- **I/O**: base and limit with their upper 16 bits (30h and 32h); a
  window that is not wholly below 10000h (the templates' maximum FFFFh)
  makes the parent unread (`cm.breq.parent`); otherwise the length is
  limit - base + 1 in a `ULONG`, at most 10000h.
- **Budgets** work on the capped lengths, in `ULONG`s: every sum is of
  grants and shares that are each at most W(k), taken in an order that
  stops before the sum exceeds W(k), so no sum passes `CAP`; a
  subtraction is made only where the subtrahend is known not larger
  (W(k) less the granted granules), and saturates at 0 otherwise; a
  share is a `ULONG` division by the claimant count, then floor_g.
- Vectors (section 10.1) cover the bounds: D.low FFEFFFFFh (exactly
  `CAP`), D.low FFFFFFFFh (4 GB, capped), a window crossing 4 GB (base
  high 0, limit high 1), base FFF00000h with limit FFFFFFFFh (1 MB at the
  top of the 32-bit space), 64 GB, a base above the limit only in the
  high half, and an I/O window reaching 10000h.

### 6.1 The shape test (both roles)

The rewriter acts only on a list that matches one of two **templates**
exactly, field by field, read from section 2.1:

- **XP closed** (minor version 1): `ListSize` 268h; `AlternativeLists`
  1; `Version` 1, `Revision` 1, `Count` 18; in this order: the I/O group
  (one descriptor: `Option` 0, `Type` 1, `ShareDisposition` 1, `Flags`
  B1h, `Length` 1000h, `Alignment` 1000h, minimum 0, maximum FFFFh), the
  private `(1, 2, 0)`; the memory group (`Option` 0 then six of 08h,
  `Type` 3, `ShareDisposition` 0, `Flags` 0, lengths 64, 32, 16, 8, 4, 2
  and 1 MB, `Alignment` 100000h, minimum 0, maximum FFFFFFFFh), the
  private `(1, 3, 0)`; the prefetchable group (the same with `Flags` 4
  and maximum FFFFFFFFFFFFFFFFh), the private `(1, 4, 0)`; every private
  descriptor `Option` 0, `ShareDisposition` and `Flags` as read in the
  captures.
- **2000 closed** (minor version 0): `ListSize` E8h; `AlternativeLists`
  1; `Count` 6; the same order, each group one descriptor with `Option`
  0: I/O exactly as XP's (`reqs-w.txt` reads the same fields), memory
  `Length` 100000h, `Alignment` 100000h, `ShareDisposition` 1, `Flags` 0,
  minimum 0, maximum FFFFFFFFh, prefetchable the same with `Flags` 4 and
  maximum FFFFFFFFFFFFFFFFh; the same privates.

The header's interface type, bus and slot fields are not compared. A
template is tied to its system: an XP list on 2000 or the reverse is
left. Anything else is left as it came and counted by reason
(`cm.breq.shape`): a configured list, an interrupt or any other
descriptor, two alternative lists, a field that differs, an oversize
list. XP x64's closed list is not read; until leg R4 reads it and adds
its template, every XP x64 list is left (section 7.7).

A stack can hold two filters of this driver (a kernel-attached child and
a registered one, record 21 section 7.4 revision 3), and the rewriter's
output can resemble its input. So only the **lowest** of this driver's
filters in a stack acts: a filter that finds another of this driver's
device objects between itself and the PDO passes the IRP through. The
rules are deterministic over the same snapshot, so a second rewrite
would give the same list; the rule keeps one attempt record per IRP.

The rewriter **keeps** from the OS list, in each group it keeps: the
`Type`, `ShareDisposition`, `Flags` and `MaximumAddress` of the group's
first descriptor (copied into every descriptor it writes for that group)
and the private descriptor after it, unchanged and in its place. It
**drops** a group whole, its private descriptor with it, as the PCI
driver's own configured shape does (section 2.2). It **replaces** a
group's sizes: the first descriptor `Option` 0 with the preferred length,
then `IO_RESOURCE_ALTERNATIVE` descriptors with the ladder's smaller
lengths, each with `Alignment` g(k) and `MinimumAddress` 0. It writes one
alternative list, never an `IO_RESOURCE_PREFERRED` descriptor (that is
the configured shape's mark of a fixed window). Every length it writes
is at least g(k) and at most P(k).

**No list without a window.** Where the rules of sections 6.2 to 6.4
would leave a bridge's list with no window group at all - whichever rule
dropped the groups: a parent with no window of any kind, or a downstream
port the budget granted nothing (one memory megabyte, two memory
claimants, no I/O or prefetchable claim) - the rewriter **leaves that
bridge's OS list unchanged** (outcome 9, "no window"; `cm.breq.nowin`).
A bridge started with no window is not read on either system (section
13.2), so this record never writes such a list; a reading first (a leg
that starts a closed bridge from a list with no group) would be needed
before it could, and none is planned. The cost: that port asks as today
and its siblings' grants were computed without it, so the arbiter may
place it instead of a granted sibling - at worst today's outcome, one
port placed of a window too small for two [inf: the arbiter's order,
not read]. Its OS list is not reduced by rule 6.3.1 either: a required
group its parent lacks makes it fail rather than compete. The sum
invariant of section 6.3.4 is over the ports whose lists are written.

### 6.2 The upstream port

Applies to a bridge whose Device/Port Type is an upstream switch port.

1. **A kind the parent lacks is dropped** (P(k) = 0): the OS list's
   required window of that kind could not be had in any case, and the
   port starts without it. This holds for any N, also N unknown and N = 1.
2. **N = 0 or N = 1**: nothing else changes. One downstream port is
   placed by the OS list today (the ORICO model on XP [run]); the single
   enclosure keeps exactly today's list (section 6.5, example 2).
3. **Not alone on its parent's bus**: nothing else changes (counted,
   `cm.breq.notalone`); a tunnel port, and a chain port, hold one device.
4. **N >= 2 or N unknown, alone**: for each kind the parent has, the
   preferred length is P(k), the whole parent window, and the ladder
   halves it (floor_g at each step) down to and including max(2 x g(k),
   the OS list's smallest length of that kind), and ends with the OS
   list's smallest length if that is smaller; at most 16 descriptors a
   group. I/O steps down by 4 KB from P(I/O) instead of halving, to 4 KB,
   at most 16 entries.

Why the whole parent window: the upstream port is the only thing on its
parent's bus, nothing else can use what it leaves, and its downstream
ports' needs are not readable when its list is filtered (section 2.4).
N unknown (its bus not yet numbered at the IRP, the [inf] of section 2.4
failing) is treated as N >= 2: a switch that answers as an upstream
port with an unreadable bus is assumed to need room. The ladder lets it
start in less when the arbiter cannot give the whole (another claim on
the parent after a rebalance), down to what the OS list would have
asked, so the rewrite never leaves the upstream port worse placed than
the OS list would.

### 6.3 A downstream port

Applies to a bridge whose Device/Port Type is a downstream switch port.
The rewriter computes the claims of **every** sibling from the same
snapshot, so each sibling's filter, run in turn, reaches the same
division; it then writes this port's part. Every port gets one list.

1. **A kind the upstream port lacks is dropped** (W(k) = 0), for any
   number of siblings. A claim that is "yes" there is an **unmet need**,
   counted and recorded (`cm.breq.unmet`); the function behind meets its
   own Code 12 rather than the port.
2. **One sibling** (the single enclosure): nothing else changes; its
   list is today's.
3. **Two or more siblings**, per kind, in three steps:
   - **The order**: claimants of the kind sorted "yes" first, then
     "granule only" (memory), then "unknown", each class in sibling
     order.
   - **The budget**: walking that order, each claimant is **granted** one
     granule while the granted granules fit in W(k); a claimant not
     granted drops the group (an unmet need when its claim was "yes"),
     unless that leaves its list with no window (section 6.1, "No list
     without a window").
     So when W(k) is short the "yes" claimants are served first and an
     empty chain port cannot take a NIC's I/O.
   - **The shares** (memory and prefetchable): the space left, W(k) less
     the granted granules, is divided equally among the granted
     claimants that are not "granule only", floor_g; each such claimant's
     preferred length is its granule plus its share, and its ladder
     halves that down to its granule. A "granule only" claimant asks
     exactly 1 MB. I/O claimants ask exactly 4 KB, no ladder.
4. The sum of the preferred lengths over the siblings is at most W(k)
   for each kind, and every length at most W(k) (section 7.1): each
   grant fits by construction, and the shares divide only what is left.

### 6.4 Bridges that are neither

A bridge whose Device/Port Type is neither (a PCI Express-to-PCI bridge
in a dock, a root port, a bridge with no PCI Express capability): the OS
list is left as it came except rule 6.2.1 (a kind the parent lacks is
dropped). Counted by role. Where such a bridge sits on an upstream
port's bus beside downstream ports, the downstream ports' budget cannot
account for it, so the rules of section 6.3 apply only where every
function on the upstream port's bus is a downstream port; otherwise
every bridge there keeps its OS list but for rule 6.2.1.

### 6.5 Worked examples

All on XP unless said; 2000's are the same with 2000's OS list (section
2.5) as the input and its smallest lengths (4 KB, 1 MB, 1 MB) as the
ladders' last entries.

**Example 1: 44.63's dock** (the ASM4242 layout [run]). The tunnel
port's windows: I/O 12 KB (`1000h`-`3FFFh`), memory 384 MB
(`CE000000h`-`E5FFFFFFh`), prefetchable 2 MB (`CD200000h`-`CD3FFFFFh`).
Below it the switch's upstream port `03:00.0`, alone; on its bus 4 three
downstream ports, N = 3.

- Upstream port: I/O preferred 12 KB, alternatives 8 KB and 4 KB; memory
  preferred 384 MB, alternatives 192, 96, 48, 24, 12, 6, 3 and 2 MB, then
  1 MB (the OS list's smallest); prefetchable preferred 2 MB, alternative
  1 MB. Expected placement: the tunnel port's three windows whole.
- Downstream ports, with W = 12 KB, 384 MB, 2 MB; the functions behind
  each read at the filter [emu: QEMU's `info pci` of 44.63's leg, every
  BAR non-prefetchable]:
  - `04:00.0`, the NVMe (one 64-bit memory BAR): I/O no, memory yes,
    prefetchable no;
  - `04:01.0`, the e1000e (three 32-bit memory BARs and one I/O BAR): I/O
    yes, memory yes, prefetchable no;
  - `04:02.0`, the qemu-xhci (one 64-bit memory BAR): I/O no, memory yes,
    prefetchable no.
- Results: memory: three claimants, three granules granted, the 381 MB
  left divided, 127 MB each, so each port prefers 128 MB with the ladder
  64, 32, 16, 8, 4, 2, 1 MB; prefetchable: no claimant, the group dropped
  on all three; I/O: one claimant (`04:01.0`), 4 KB. Sum of preferred
  memory 384 MB = W. Expected: all three ports Code 0, the NIC's I/O BAR
  placed, the NVMe and the xHCI started (their drivers as section 10.2's
  legs say).
- On 2000 the upstream port's rewrite is the one that matters: its OS
  list asks 1 MB of memory, which cannot hold three 1 MB windows; the
  rewritten one asks 384 MB.

**Example 2: a single enclosure** (the ORICO model: an upstream port, one
downstream port, the NVMe). Upstream port: N = 1, rule 6.2.2, the OS
list unchanged (but for a kind its parent lacks). Downstream port: one
sibling, rule 6.3.2, unchanged. Expected: today's placement, byte for
byte the same lists (`FilteredConfigVector` = `BasicConfigVector`), the
regression leg R9.

**Example 3: a chain of two enclosures** (the emulator's
`usb4-device ... downstream-ports=1` as the first, a second enclosure on
its downstream USB4 port; record 34 section 2.1 [emu]). The first
enclosure's switch: upstream port U1, downstream ports `D1a` (its NVMe)
and `D1b` (the gated hot-plug port the chained tunnel lands on), N = 2.
With the tunnel port of example 1:

- U1: as example 1's upstream port (memory down to 2 MB, then 1 MB).
- `D1a`: NVMe, memory yes, I/O and prefetchable no. `D1b`: empty and
  hot-plug capable while the chained device is not yet there: I/O
  unknown, memory and prefetchable yes. Memory: two granules granted,
  382 MB left, 191 MB each, so each prefers 192 MB; prefetchable: `D1b`
  alone, 1 MB granted plus the 1 MB left, 2 MB; I/O: `D1b` alone
  ("unknown"), granted 4 KB.
- The chained enclosure arrives later behind `D1b` (record 34 section
  3.2's invalidation): its switch's upstream port U2 is alone on `D1b`'s
  bus and its N is 1, so its list is the OS list: on XP 4 KB of I/O
  (inside `D1b`'s 4 KB), memory 64 MB (inside 192 MB), prefetchable down
  to 1 MB (inside 2 MB). Its downstream port, one sibling, the OS list
  too. On 2000 the same fits (4 KB, 1 MB, 1 MB). Where the chained device
  is a plain enclosure with its NVMe directly on `D1b`'s bus, there is no
  bridge to rewrite.
- Plugged together (the chain present at the plug): `D1b`'s secondary
  bus then holds U2, a type 1 function: unknown I/O, memory and
  prefetchable yes; the same division.
- The emulator's chain port has a Power Controller that 2000 and XP
  never turn on (record 34 section 2.5); the legs run with
  `-global usb4-tunnel-dsp.power-controller=off` as 44.45 did.

**Example 4: a port with nothing behind it**. A two-port switch (as
example 3's) where the second downstream port is empty and not hot-plug
capable: it asks exactly 1 MB of memory and nothing else; the first is
granted its granule plus the rest, W(mem) less 1 MB, and every other
kind it claims. A hot-plug-capable empty port is example 3's `D1b`.

**Example 5: short windows.** The tunnel port has 4 KB of I/O and 2 MB
prefetchable, and the dock's three ports are a NIC (I/O yes), an empty
chain port (I/O unknown, prefetchable yes) and a function with a
prefetchable BAR (prefetchable yes). I/O: the NIC first ("yes"), granted
the one 4 KB; the chain port not granted (unknown, no unmet need).
Prefetchable: two "yes" claimants, the chain port and the third port,
in the same class and so taken in sibling order, the chain port first;
2 MB, both granted 1 MB, nothing left. Sums: I/O 4 KB, prefetchable 2
MB, within W. (With 1 MB prefetchable the chain port, first in sibling
order, would be granted it and the third port's need would be unmet:
the class decides, then the order, as section 6.3 says.)

**Example 6: the parent has no I/O window.** A tunnel port the firmware
gave no I/O (a board short of I/O space): the OS list's required 4 KB of
I/O fails today even for a single enclosure. Rule 6.2.1 drops it on the
upstream port, rule 6.3.1 on the downstream ports; the NVMe needs none.
An e1000e behind such a port keeps an unmet I/O need and is the one
device not started.

## 7. Safety

### 7.1 Never more than the parent has

Every length written is at most the parent window of its kind (capped,
section 6), and the preferred lengths of the downstream ports sum to at
most the upstream port's window (section 6.3.4); the host vectors check
both over every case and over a seeded sweep of window sizes and claims
(section 10.1). The ladders end at the OS list's own smallest length or
the granule, so a placement the OS list could have made remains
possible. A parent window that cannot be read makes the rewriter leave
the list (`cm.breq.parent`). A parent window of 4 GB or more is asked
for as `CAP`, the rest of it left unused.

### 7.2 When the arbiter still fails

The bridge gets Code 12, as today. The filter does not retry, invalidate
or rewrite again by itself; the next filter IRP for that bridge (a
rebalance, a re-plug, a restart) runs the rules afresh on what is there
then. The attempt record shows the start's status (section 9.2), so a
failed rewrite is visible in a registry export. The switch's value 0
(section 9.1) puts today's behaviour back without an uninstall.

### 7.3 A later device that needs I/O

A device that appears later behind a port that took no I/O window (a
chained dock with a NIC behind example 5's chain port, or behind a port
that read empty) has an unmet I/O need: its function's I/O BAR is not
placed and that function, not the port, shows Code 12. The port's own
list is filtered again only when the PnP manager assigns its resources
again (a rebalance); the rules then see the new function and claim I/O,
and whether the PnP manager of 2000 and XP rebalances a started bridge
for a new child is not read [inf]. Unplugging and plugging in the whole
switch again re-runs everything with the new device present. The
release notes say so (section 12, Q1's text).

### 7.4 A rebalance

A rebalance stops the bridge and filters its list again [doc]. 2000's
PCI driver then answers a placed bridge in the configured shape (section
2.5): not a template, left as it is, and the configured shape keeps the
window size the rewrite gave. XP's answers the closed shape again
(section 2.5): rewritten again by the same deterministic rules from the
same snapshot, so to the same sizes. Either way a rebalance does not
shrink a rewritten bridge back to the fixed list.

### 7.5 Unplug and plug in again

The bridges' devnodes are removed by the surprise removal; the plug
creates them again with the same instance ids [run, 44.64], with their
installed driver (no install again) and the kernel chain (record 31
section 4.3). What else they have depends on the service:

- **An immediate replug**, before the service's poll has seen the
  bridges leave: their bridge marks are still there, so each stack has
  both of this driver's children, the kernel-attached one and the
  registered one, and the lowest acts (section 6.1).
- **A later replug**, after a poll removed the marks of the bridges that
  left (section 4.3, "Its life"): the kernel chain alone. That is enough
  for the rewrite, since the kernel children are in place at the
  bridges' starts, as at a never-seen switch's first start (section
  4.1). The service writes the bridge marks again at its next poll; the
  bridge inventory lists the kernel children's attachments, so no
  bridge needs the restart (section 4.3) and none is made.

A switch reset by its unplug comes back closed and is rewritten; the
emulator's switch may come back with its earlier windows (section 2.2)
and is then left alone, its lists the configured shape. Leg R7 covers
both replugs and records which shape arrived.

### 7.6 Standby and hibernation

No filter IRP is expected at a resume: the PCI driver restores each
bridge's configuration as it assigned it, and the arbiters' assignments
do not change [inf: the PCI driver's resume path, not read]. Where the
tunnel does not come back at the resume, the switch's devnodes are
removed and a later re-approval is a plug (section 7.5). A resume from
hibernation on these systems is the same. A cold start with the dock
attached is the firmware's layout (section 7.9). Leg R11 reads standby
and hibernation on XP with the dock placed by the rewrite.

### 7.7 XP x64

NT 5.2, the same driver family and the same code: the amd64 build of
`xhcicm.sys` and the amd64 service. Its PCI driver's lists for a
hot-added bridge are not read, and the shape test has no template for
it, so until leg R4 reads its closed list (and the template is added
with that reading as its source) every XP x64 list is left: XP x64 then
gets the registered attachment and the diagnostic, and today's
placement. The structures are the same size on amd64
(`IO_RESOURCE_DESCRIPTOR` 32 bytes, asserted in `cm_filt.c` for both
builds today [code]).

### 7.8 Vista and later: not touched

The rewrite runs only on NT 5.x, and the service's bridge marks are
written only on 2000, XP and XP x64. On Vista and later the child filter
passes every filter IRP of a bridge through, as today. Why: 7 x86 places
the same dock itself (section 2.6); its PCI driver's lists are a
different design that this record has not read; and a rewrite there
could only make a working placement worse. The owner's rule for these
targets (`AGENTS.md`: accommodate where small and low-risk, never at a
primary target's expense) points the same way. Leg R8 shows 7 unchanged.

### 7.9 A cold start

A switch attached at power-on is laid out by the firmware where the
firmware tunnels it before the hand-off (44.63, 44.64 [run]); its lists
are the configured shape and are left as they are. A switch whose tunnel
this driver builds after the start (a software connection manager host
with no pre-boot tunnel) is a hot add and is rewritten. Leg R6.

## 8. Interaction with existing parts

### 8.1 XHCIPFW (record 32, task 44.49)

`XHCIPFW` closes prefetchable windows that lie wholly above 4 GB before
the PCI driver reads them, on 98 SE and ME (the VxD) and, as designed in
record 32 section 11, on 2000 and the 32-bit NT 6 systems (a boot-start
driver). On 2000 a tunnel port so closed may have no prefetchable window
when a switch is hot-added below it: P(prefetchable) = 0, so rules 6.2.1
and 6.3.1 drop the prefetchable group that 2000's OS list requires and
that would otherwise fail; a prefetchable claim then counts as memory
(section 5.2). Nothing of this record writes a register, and `XHCIPFW`
runs before the PCI driver, so the two never act on the same thing at
the same time. XP is outside record 32's scope; a prefetchable parent
window above 4 GB on XP is read as any parent window is (64-bit base and
limit), and the template's maximum `FFFFFFFFFFFFFFFFh` is kept [inf: not
read].

### 8.2 `XhciCmBridgeDiag`

`XhciCmBridgeDiag` is the 9x bus numbering's last attempt (record 21
section 6.1.1; `cm_statb.h`, `CM_BRDIAG_WORDS` 20), written only on 98
SE and ME. This record's diagnostic is a separate value beside it in the
same key, in the same style (section 9.2), so the two never share a
layout or a sequence. On 98 SE and ME the numbering is the analogue of
this record: it places a hot-added chain's memory windows itself from
what is behind each bridge (section 8.5).

### 8.3 The no-interrupt start (record 28) and the 2000 interrupt (record 21 section 6.2.1)

Record 28 is 98 SE and ME only and does not meet this record. Its NT
relative, the interrupt the child gives a function on 2000's Standard PC
HAL (task 44.53), acts on type 0 functions only: `CmRegIrqWire` refuses a
type 1 header [code], so it never adds an interrupt to a bridge and never
meets a rewritten list; the shape test refuses a list with an interrupt
in any case. The two are independent: the rewrite places the bridges,
and whether a function behind them gets an interrupt on that HAL is
44.53's rule (the function's pin, swizzled through each bridge, must
arrive on the tunnel port's own pin). In example 1 the NIC behind the
second downstream port (device 1) swizzles to another pin than the
port's [inf: the swizzle rule, read in 44.53's code], so on 2000's
Standard PC HAL its bridge is placed and it has no interrupt; leg R3b
says which outcome is expected where.

### 8.4 Stor98's tunnel-drives-only rule (record 31)

Unchanged: the NVMe id is given by the same child on the same
structural test. Record 31's first-plug gap on 2000 and XP closes
(section 4.6).

### 8.5 Windows 98 SE and ME

What is known [code]: the connection manager's numbering
(`XhciTbtNumberBridges`, `XHCI_TBT_BRNUM_F_WINDOWS`) numbers a fresh
tunnel's bridges, sizes each bridge's memory window from the memory BARs
behind it (at least 1 MiB), places it inside the tunnel port's window,
and **closes every I/O and prefetchable window**; `PCI.VXD` takes those
registers as the bridges' boot configuration. So the fixed 4 KB I/O list
of 2000 and XP does not arise there, and a downstream port gets a window
sized for what is behind it. What follows from the closed I/O windows
is that a function with an I/O BAR behind a hot-added switch (44.63's
e1000e) gets no I/O on 98 SE and ME [inf], and whether its 9x driver
needs it is the device's. A multi-port dock hot-plugged on 98 SE or ME
on q35 with this numbering has not been read: **a reading owed, not a
claim** (section 10.2, leg R13). Record 34 section 3.3 designs the
chain port's padding there.

### 8.6 Design record 34 (daisy chains)

Record 34 depends on this record for 2000 and XP (its section 3.6): the
first device's switch must place the chain port, and the chain port must
keep a filter in the first plug's session. Here: the chain port is a
hot-plug-capable empty port that claims memory, prefetchable and
(unknown) I/O (section 5.2, example 3), so it gets windows for a device
that arrives later; the bridge marks (section 4.3) keep its filter after
the restart; and the chained device's own switch is rewritten by the
same rules (example 3). The owner's order (2026-10-27): this record's
code first, in the release that brings daisy chains.

## 9. Switch, diagnostic, log and counters

### 9.1 `XhciCmBridgeReq`

`REG_DWORD`, read as every `XhciCm*` value is read from `3.1.0.0` (task
42-A.17): the machine-wide key `HKLM\System\CurrentControlSet\Services\USB`
first, and where absent there the NHI's driver key; once at the NHI's
start, and taken by a child from the NHI of the active member its
ancestry walk reaches. 1 (the default; absent in both places reads as
1): section 6's rewrite on NT 5.x, and the service's bridge marks on
2000, XP and XP x64. 0: neither; lists pass untouched, attempts still
recorded with outcome "off". 2: every attempt computed and recorded, no
list replaced, no bridge mark (a reading mode for the bench). Any other
value is refused and 1 stands. Never read on Vista and later or on 98 SE
and ME. Not written by the INFs; the installer never asks for it (record
23 section 8.1's never-asked set) and the window offers it under
"Advanced..." as a 2000 and XP row (record 26 section 5.5). The service
reads it as it reads `XhciCmSafeEject`.

### 9.2 `XhciCmBridgeReqDiag`

`REG_BINARY` in the NHI's hardware key beside `XhciCmBridgeDiag` and
`XhciCmNoIrqDiag`, written by the NHI's thread when its sequence changed,
from a copy the filter posts under `BrDiagLock` (the handler runs in the
PnP manager's thread and does not write the registry itself). 68 dwords:
a header of 4 and a ring of the last 8 attempts, 8 dwords each, so a
dock's four bridges and a chained switch fit together:

| Dword | Meaning |
|---|---|
| 0 | the last attempt's sequence (0: none since the start) |
| 1 | rewrites made (15:0), lists left by rule (31:16), each saturating |
| 2 | starts seen after a rewrite: succeeded (15:0), failed (31:16) |
| 3 | allocation failures (15:0), children with no active member above (31:16) |
| 4 + 8n | entry n: its sequence (0: empty) |
| 5 + 8n | the bridge's location (bus 15:8, device and function 7:0) and its tunnel port's (31:16, the same packing) |
| 6 + 8n | role 3:0 (1 upstream, 2 downstream, 3 other), shape 7:4 (0 no template, 1 XP closed, 2 2000 closed), system 11:8 (the minor version), outcome 15:12 (0 left, 1 rewritten, 2 off, 3 record only, 4 shape refused, 5 parent unread, 6 not alone, 7 no memory, 8 lower status, 9 no window), groups dropped 18:16 (bit 16 I/O, 17 memory, 18 prefetchable), unmet needs 21:19 (the same order), the start 23:22 (0 not seen, 1 started, 2 failed), reserved 31:24 (0) |
| 7 + 8n | N or the sibling count 7:0, this port's index 15:8, flags 19:16 (bit 16 alone, 17 secondary numbered, 18 hot-plug capable, 19 a nested bridge behind), this port's claims 25:20 (2 bits a kind, I/O 21:20, memory 23:22, prefetchable 25:24: 0 no, 1 yes, 2 unknown, 3 granule only), reserved 31:26 (0) |
| 8 + 8n | the parent's I/O window in bytes |
| 9 + 8n | the parent's memory (15:0) and prefetchable (31:16) windows in MiB, saturating at FFFFh |
| 10 + 8n | the I/O written in 4 KB units (7:0), the OS list's I/O in 4 KB units (15:8), the times this bridge was rewritten since the start (23:16, saturating), reserved 31:24 (0) |
| 11 + 8n | the preferred memory (15:0) and prefetchable (31:16) written, in MiB |

A release build's outcome on a user's machine is thus readable from a
registry export, as record 21 section 6.1.1's and record 28 section 7's
are; XHCITR (record 25) can show it later beside the tunnel it belongs
to: the record's layout goes in `cm_statb.h` with the other two, and the
query device's answer (record 25 section 4) may carry it in a later
version. Not in this record's code. The service's bridge marks are in
its own records and log, as its unit marks are.

### 9.3 The log and the counters

- The driver's log (level 4 for each attempt, level 2 for a failure):
  `cm.breq` with the entry's dwords 5 to 7, `cm.breq.list` with each
  written length, `cm.breq.start` with the start's status.
- Counters (record 21 section 8's `cm.*` set): `cm.breq.seen`,
  `cm.breq.rewritten`, `cm.breq.dropped.io`, `.mem`, `.pref`,
  `cm.breq.unmet`, `cm.breq.shape`, `cm.breq.parent`,
  `cm.breq.notalone`, `cm.breq.nobridge`, `cm.breq.nomem`,
  `cm.breq.nowin`,
  `cm.breq.lower`, `cm.breq.start.ok`, `cm.breq.start.fail`.
- The service's log: a bridge mark written, a bridge's install still
  pending, the restart made or refused (with the reason and the re-plug
  sentence).

## 10. Verification

### 10.1 Host vectors (pure C, as the other host suites)

`cm_breq.c` and `cm_breq.h` are DDK-free C89 with the byte offsets of
`cm_irq.h` (shared, not duplicated), no allocation and no lock; the
caller passes the list's bytes, the snapshot and the room.
`test\test_cm_breq.c`:

- **The templates** on the lists captured here: XP's closed list
  (44.63, `reqs-a.txt`, as bytes), 2000's closed list (`reqs-w.txt`),
  XP's configured lists (44.64, `serial-b.log`), 2000's configured
  lists, the tunnel port's list with an interrupt: closed recognised,
  each on its own system only; configured and the rest left; and a
  rejection made from each closed list by one change for **every field**
  the template compares (each length, alignment, minimum, maximum,
  share, flag, option, the private data, the order of groups, `Count`,
  `ListSize`, `AlternativeLists`, `Version`, an extra interrupt).
- **The examples** of section 6.5, inputs as snapshots and the OS lists
  as captured, the outputs compared byte for byte with expected lists
  written out in the test, on both systems' inputs.
- **The rules' edges**: N unknown, 0, 1, 2 and 8; not alone; every
  parent window 0 in turn; the budget's order (a "yes" after an
  "unknown" in sibling order still served first); short I/O with two,
  three and four claimants; short prefetchable; an empty hot-plug port;
  a nested bridge; an "other" bridge among the siblings; a read failure
  (unknown); a memory BAR reading 0 (memory claimed; the e1000e's
  captured BARs); an assigned 64-bit memory BAR whose upper dword has
  bit 0 set, and one whose upper dword has bit 3 set (no I/O or
  prefetchable claim from either), a 64-bit BAR in the last slot and a
  reserved width 11b (unknown); a prefetchable claim moved to memory;
  the ladder's bounds and its 16-entry limit; section 6's arithmetic
  bounds (exactly `CAP`, 4 GB, 4 GB plus 1 MB, 64 GB, a window crossing 4
  GB, 1 MB at the top of the 32-bit space, the high-half-only comparison,
  an I/O window reaching 10000h: capped, no wrap); no list without a
  window (1 MB of memory, two memory claimants and no other claim: the
  second port's OS list left, outcome 9; a parent with no window of any
  kind: left).
- **The invariants**, over a seeded sweep (a fixed seed, a few thousand
  cases of window sizes, N and claims): every length a multiple of its
  granule, at least the granule, at most the parent window and `CAP`;
  the siblings' preferred lengths summing to at most W per kind; every
  private descriptor of the output present in the input, in the same
  order; one alternative list; the output's `ListSize` exact; the
  rewriter never writing past the room it was given.
- **The boundary** (the registration and ports suites): the ancestry
  walk over a discrete controller's topology (an upstream switch port's
  filter, its downstream switch ports as members, the NHI and xHCI) - no
  rewrite on the host's ports or functions, a rewrite on a dock below a
  member; a registered child associated through a chain child's
  snapshot, its `Parent` that child and its `Bridge` the root (so
  `cmTopOf` and the chain log's domain name the tunnel-port bridge
  filter); the role by position with an INF-listed id in both orders;
  the NHI's `AddDevice` with nothing above its PDO (the NHI role, not
  the child).
- **The service** (its own host suite): bridge marks written only after
  the install (pending flags, no driver), only below a unit of an active
  member; the restart's gate (a started disk refuses, a started xHCI or
  NIC does not); the restart's trigger (a bridge at problem 12; a placed
  bridge whose attachment is missing, such as a chain port; no restart
  when every bridge has its attachment, the later replug of section
  7.5); `mk_bridge_attached` (a nested bridge attached with `Answered`
  0: attached, no restart; a complete list without it: missing; a list
  of `CM_QB_MAX_BRIDGES` entries flagged incomplete, an unreadable
  answer, an unread location: unknown, no restart, the budget kept); the
  other restarts waiting for the bridge marks, and after
  `MK_BRIDGE_WAIT` going ahead only when the rest of the gate holds (the
  deadline past with an install still pending: no restart call, the
  budget kept); bridge marks removed by every cleanup that
  removes unit marks, safe eject off included.

### 10.2 The legs

All on q35 with the ASM4242 layout of 44.63 (emulator build
`asm4242-20261027-boardint` or later; the root port's reservations and
`usb4-asm-usp`'s pads as 44.45 set them), or with the chain attached at
power-on by `preboot=on` and `-EncAtBoot` where a leg says so: the
emulator's q35 `tb3ar` tunnel port takes no bus reservation and is not
usable for a hot add (section 1.3). Chains with
`-global usb4-tunnel-dsp.power-controller=off`. 2000 runs on its
Standard PC HAL (`acpi=off`) unless a leg says otherwise. Each leg
records the `BasicConfigVector`, `FilteredConfigVector` and
`AllocConfig` of every bridge (decoded), `XhciCmBridgeReqDiag`, the
service's log, the Device Manager codes and the trace's window writes.

Before the code is committed (on a test build, read and recorded first):

- **R1. The order and the dropped window** (XP and 2000): the filter IRP
  arrives after the bridge's bus numbers are written (the bus-number
  register logged at the IRP); a closed bridge started from a list with
  its I/O group removed starts, with its I/O window left closed.
- **R5. The registered attachment** (XP and 2000): the first plug of a
  never-seen dock - the bridges' installs (`machine.inf`, silent), the
  service's bridge marks written after them, no Critical Device Database
  entry changed, the restart, the installed stacks with `xhcicm` below
  the PCI driver after it, the NVMe behind tagged (section 4.6); which
  bridges were at problem 12 and which were placed without an
  attachment before the restart (section 4.3's trigger); the
  restart refused with a started disk below (a drive already on Stor98),
  the message, and a re-plug placing the dock.

Then, on the reviewed build:

- **R3. The dock hot-plugged** on XP: all three downstream ports Code 0,
  the NIC's I/O placed, the NVMe on Stor98, the xHCI on xHCI98 (the NIC
  at problem 28, no XP driver in the guest, as 44.45 read); the
  diagnostic's four entries as example 1.
- **R3b.** The same on 2000 on the Standard PC HAL: all three ports Code
  0; the functions behind as 44.53's interrupt rule allows (the NVMe and
  the xHCI where their pins arrive on the tunnel port's pin, the NIC
  without an interrupt, section 8.3), each function's code recorded
  against that expectation. On an ACPI HAL where a q35 ACPI base boots
  (record 32 section 10.2's note), the same with every function's
  interrupt routed.
- **R4.** XP x64 (the amd64 package, test-signed inside the guest as
  task 43.36 did, or unsigned; XP x64 loads either): the closed list
  read (the template's source), the bridge marks; then, with the
  template added, R3 on XP x64.
- **R6. A cold start** with the dock attached (`preboot=on`) on XP and
  2000: the lists in the configured shape, left; all ports placed.
- **R7. Unplug and plug in again** on XP and 2000, hot, twice: once
  within the service's poll interval (the bridge marks still there) and
  once after a poll has removed them (the kernel chain alone, the marks
  written again, no restart): placed again each time; which shape
  arrived, and which of this driver's filters acted, recorded.
- **R8. Windows 7** x86 (and x64): the dock and the single enclosure
  unchanged from today: `FilteredConfigVector` equal to
  `BasicConfigVector`, no diagnostic entries, no bridge marks.
- **R9. The single enclosure** (the ORICO model) on XP and 2000, hot and
  at a cold start: lists unchanged, the drive working (the regression
  leg).
- **R10. The two-level chain** (example 3) on XP and 2000 where the
  emulator announces it (under the USB4 software connection manager,
  record 34 section 2.5), both drives.
- **R11. Standby and hibernation** on XP with the dock placed by the
  rewrite: the devices working after each resume.
- **R12. The uninstall** on XP and 2000: no `LowerFilters` naming
  `xhcicm` anywhere, the dock's bridges Code 0 after a restart; and an
  update install over `3.3.0.0` with a known dock (its bridges get their
  marks at the service's next poll; a re-plug placed).
- **R13. 98 SE and ME** (the reading owed by section 8.5, no code of this
  record): the dock hot-plugged, each downstream port's windows and each
  function's state recorded.
- **The switch**: `XhciCmBridgeReq` 0 (lists as today, outcome "off", no
  bridge marks) and 2 (computed, recorded, not replaced) on XP.

### 10.3 Hardware (optional, the owner's)

On the hardware bench, where XP runs there, a real dock or a chained
enclosure hot-plugged: the bridges' `BasicConfigVector` (the shape of a
real switch's closed list, and the real downstream ports' Slot
Capabilities), then the leg R3 outcome. 2000 has never run on real
hardware (`AGENTS.md`); no 2000 bench leg is planned.

## 11. Alternatives

- **B. Program the downstream ports' windows** before the PCI driver
  reads them, in the child filter's completion of the upstream port's
  start, so the PCI driver finds them configured as at power-on (44.63's
  option B). Not chosen: it writes the configuration of devices the PCI
  driver owns, on NT, where this project writes no bridge register; the
  downstream ports' secondary buses are not numbered at that point
  (section 2.4), so what each needs cannot be read and I/O goes to none,
  leaving a NIC's I/O BAR unplaced; on XP the 2 MB prefetchable window of
  44.63's tunnel port cannot give three ports 1 MB each, and on 2000 the
  upstream port's fixed 1 MB windows cannot be split at all, so B cannot
  place the dock without A's rewrite of the upstream port; a later
  rebalance of the upstream port would leave the programmed windows
  outside its new ones; and the PCI driver's keeping of a configured
  bridge at a hot add is read only at power-on and after a re-plug on
  the emulator (section 2.2).
- **C. Documentation only** (44.63's option C): the limitation in the
  release notes, with the power-on workaround on XP. The owner's choice
  for `3.3.0.0`; this record replaces it in the release that brings
  daisy chains, where a chain on 2000 and XP needs the first device's
  switch placed whole (record 34 section 3.6).
- The registered attachment's own alternatives are section 4.2's.

## 12. The owner's questions

**Answered (owner, 2026-10-27), each the recommendation.** Q1 (a): R2,
the unit restarted once after the bridges' installs unless a disk or
volume below it is in use; the restart's trigger is a bridge below the
unit at problem 12 **or a bridge whose attachment is missing** (a placed
bridge, a chain port among them, whose rebuilt stack has no filter of
this driver), made once the bridge marks are written (section 4.3), with
Q1's release notes paragraph. Q2: `XhciCmBridgeReq` defaults to 1
(section 9.1). Q3: an empty port that is not hot-plug capable keeps 1 MB
of memory (section 5.2), and no list is written without a window
(section 6.1). Q4: on XP x64 the attachment and the diagnostic now, the
rewrite once leg R4 has read its closed list and added the template
(section 7.7). Q5: equal shares (section 6.3). Q6: with safe eject off,
no bridge marks; the first plug's session falls back to "unplug and plug
in again" (section 4.3, "Its life"). The questions as they were put:

- **Q1. The registered attachment and its restart** (sections 4.2 and
  4.3). (a) R2 with the restart gate widened: the unit restarted once
  after the bridges' installs unless a disk or volume below it is
  started (a dock's xHCI or NIC restarted with it). (b) R2 with record
  31 section 6.2's gate as it is: the restart refused whenever anything
  below the unit runs, so on most docks "unplug and plug in again". (c)
  R0: no registered attachment; the rewrite at the first start and at
  every re-plug. **Recommendation: (a)**, with the release notes'
  paragraph: "Windows 2000 and XP: the first time a dock or an enclosure
  with a switch is plugged in, its devices may stop and start again
  once, a few seconds after it is plugged in, while xHCI98 sets up its
  PCI Express ports. If a drive behind it is already in use, unplug it
  safely and plug it in again instead. A device added later behind a
  port that has no I/O range (a network adapter behind a chained dock)
  is placed after the dock is unplugged and plugged in again."
- **Q2. The default of `XhciCmBridgeReq`**: 1 (on) or 0 (opt-in) for the
  release that brings it. **Recommendation: 1**: it touches only the
  closed lists that fail today, and a single enclosure keeps its list.
- **Q3. An empty port that is not hot-plug capable**: 1 MB of memory
  (the design) or no window at all. **Recommendation: 1 MB**, until a leg
  reads a bridge answered with no window.
- **Q4. XP x64**: the attachment and the diagnostic now, the rewrite once
  R4 has read its closed list and added the template (the design), or
  XP x64 left out entirely. **Recommendation: the design.**
- **Q5. The division**: equal shares among the granted claimants (the
  design) or weighted toward a hot-plug chain port. **Recommendation:
  equal**, as record 34 section 3.3.2's 9x padding divides.
- **Q6. Safe eject off**: the bridge marks are the mark service's, so
  with `XhciCmSafeEject` 0 (no service) the first-plug session falls back
  to R0. Keep that, or install the service for bridge marks alone when
  safe eject is off. **Recommendation: keep**; the re-plug covers it, and
  the setting's note says so.

## 13. Size, risk and what is open

### 13.1 Per piece

| Piece | Size | Risk |
|---|---|---|
| `cm_breq.c` and `cm_breq.h`: the templates, the claims, the rules, the writer | 350 to 500 lines | medium: the PCI driver's private descriptors are undocumented and kept by position |
| `test\test_cm_breq.c` with the captured lists and the sweep | 450 to 650 lines | low |
| `cm_filt.c` glue: the handler, the snapshot reads, the lowest-filter rule, the start's status | 150 to 250 lines | medium |
| the boundary and the registered child: `cm_reenum.c`'s association through chain snapshots, `cm_entry.c` and `cm_vals.c`'s role by position, and their vectors | 150 to 250 lines | medium: the role decision is on every `AddDevice` |
| the mark service: bridge marks, the restart's widened gate, the cleanups, and their vectors | 250 to 400 lines | medium-high: a restart takes a dock's devices down once; it must never fire with a disk in use |
| `XhciCmBridgeReq`, `XhciCmBridgeReqDiag`, the log and counters | 100 to 150 lines | low |
| the readings R1 and R5 on a test build | 2 days | - |
| the legs R3 to R13 | 3 to 4 days | - |

Total: about 1,450 to 2,200 lines with the tests, 10 to 14 working days
for one person who knows the code; **medium-high** overall, because two
facts the design stands on are read only after code exists (R1, R5), and
a wrong one changes a rule rather than a detail. XHCITR's view of the
diagnostic (section 9.2) is not counted.

### 13.2 Open, and the risks

- **The filter IRP's timing** against the bus numbering (section 2.4):
  if the upstream port's list is filtered before its secondary bus is
  numbered, N is unknown and rule 6.2.4 still gives it the parent's
  whole windows; if a downstream port's is filtered before its siblings'
  buses are numbered, every claim is "unknown" and the division is equal
  shares with I/O granted in sibling order, which can miss a NIC's I/O
  (an unmet need, not a Code 12 of the port).
- **A bridge started from a list without a window** it was found closed
  with (R1): read only as a shape the PCI driver produces for a
  configured bridge.
- **The private descriptors** are kept by position and pairing, read on
  every list here; a system or service pack whose closed list differs by
  any field is left alone by the templates.
- **The restart** of section 4.3: its gate, its timing against the
  bridges' installs (`MK_BRIDGE_WAIT`), its trigger's reading of the
  bridge inventory, and a restart Windows vetoes (left, and the re-plug
  sentence logged).
- **Rebalance on 2000 and XP** for a device added later behind a started
  port (section 7.3): not read.
- **98 SE and ME with a multi-port dock** (section 8.5): not read.
- **A real dock's switch**: its downstream ports' Slot Capabilities and
  its closed lists are not in this project's captures (section 10.3).

## 14. Files

| File | Change |
|---|---|
| `src/cm/cm_breq.c`, `cm_breq.h` (new) | the rewriter |
| `src/cm/cm_filt.c` | the bridge case of the filter IRP, the snapshot, the lowest-filter rule, the start's status |
| `src/cm/cm_reenum.c`, `cm_reenum.h` | the association through child filters' snapshots (`Parent` the immediate ancestor, `Bridge` the root), the active-member walk for the rewrite |
| `src/cm/cm_query.h`, `cm_query.c` | the bridge inventory in the marks answer (`CM_QB_MAX_BRIDGES`, the complete flag) |
| `src/cm/cm_entry.c`, `cm_vals.c`, `cm_vals.h` | the role of a bridge's `LowerFilters` registration by position, after the NHI's |
| `src/cm/cm_reg.c` | the NT 5.x gate, `XhciCmBridgeReq` |
| `src/cm/cm_statb.h`, `cm_statos.c`, `cm.h` | `XhciCmBridgeReqDiag` |
| `src/cm/sources`, `makefile` | `cm_breq.c` |
| `xhcimark\` | bridge marks, the restart and its trigger (problem 12 or a missing attachment, `mk_bridge_attached`), the other restarts' wait (`MK_BRIDGE_WAIT`), the reconciliation |
| `xhciinst\` (records 23 and 26) | the cleanups' bridge marks, the "Advanced..." row |
| `test\test_cm_breq.c` (new), the registration and ports suites, the mark service's suite | section 10.1 |
| `docs/using/release-notes.md` | 44.63's limitation replaced by Q1's text |

## Sources

- The Windows 2000 DDK's `inc\wdm.h` (`IO_RESOURCE_REQUIREMENTS_LIST`,
  `IO_RESOURCE_LIST`, `IO_RESOURCE_DESCRIPTOR`, `IO_RESOURCE_PREFERRED`,
  `_DEFAULT`, `_ALTERNATIVE`, `CmResourceTypeDevicePrivate`,
  `CM_RESOURCE_PORT_*`, `CM_RESOURCE_MEMORY_PREFETCHABLE`,
  `IRP_MN_FILTER_RESOURCE_REQUIREMENTS`), read 2026-10-27; Microsoft's
  public DDK reference for `IRP_MN_QUERY_RESOURCE_REQUIREMENTS`,
  `IRP_MN_FILTER_RESOURCE_REQUIREMENTS` and `IO_RESOURCE_REQUIREMENTS_LIST`.
- PCI-to-PCI Bridge Architecture Specification and PCI Express Base
  Specification: the Type 1 header's bus-number, base and limit
  registers, the BARs' read-only type bits, the PCI Express capability's
  Device/Port Type and Slot Capabilities (not held in this repository).
- `runs/run-44.md`, "44.63" (evidence `E:\xhci98-vm\p44\p44-xpdock\`,
  the decoder in `E:\xhci98-scratch\p44-p44-xpdock\tools\`), "44.45
  (continued, 2)", and on branch `p44-chain` at `d95506fc` "44.64 Part A"
  and "44.64 Part B" (evidence `E:\xhci98-vm\p44\p44-chain\`).
- Design records 21 (sections 3.3 and decision D2, 6.1.1, 6.2.1, 7.4 and
  its revision 3, 8), 23 (sections 5 and 9.2), 24 (section 4.3), 25, 26,
  28, 31 (sections 3.4, 4.1 to 4.4, 6.2), 32 and 34 (sections 2, 3.1,
  3.2, 3.3 and 3.6).
- `src\cm\cm_filt.c`, `cm_irq.h`, `cm_reenum.c` (`CmRegAttLink`,
  `CmRegIrqWire`, `cmCfgRead`), `cm_entry.c`, `cm_vals.c`
  (`CmRoleDecide`), `cm_reg.c`, `cm_statb.h`, `tbt_brnum.h`,
  `xhcicmf.inf` at `6f250605`.
- Linux `drivers/pci/setup-bus.c` (facts only): the distribution of a
  hot-plug bridge's available space among the bridges below it.
