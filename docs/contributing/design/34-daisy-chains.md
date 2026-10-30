# Design Record 34 - Daisy Chains: a Device Behind a Device

Status: revision 12, 2026-10-30, task 45.15: section 3.10's
"Deferred" (the chained unit's eject teardown, the correspondence and
the per-tunnel keying to the next release, owner). Revision 11,
2026-10-30, task 45.15: section 6.11, Part 3 as coded. Revision 10, 2026-10-29 (task 45.15), before code, reviewed and revised:
Part 3 read again against `tbt_u4cm.c` at `3e314cde` (task 45.15): a
kid's sequence the host port's own, its unplug not nested before 45.18;
a kid's own unplug hint confirmed by evidence; which check a
notification or event from a parent serves; the parent's Disable Hot
Plug cleared after it is UP; links below a kid; adoption from a link
only when the link carries nothing else, the kid's teardown sweeping
the link's other entries; the arrays the kids grow.
Revision 9, 2026-10-30, task 45.14: section 3.3.6, the 9x
padding at the first numbering, for its first reading, reviewed and
revised before code. Revision 8, 2026-10-30, task 45.18, before code: section 3.11,
Stor98's statements for chained drives and their q35 admission, and
questions Q7 to Q11, answered by the owner the same day; reviewed and revised.
Revision 7, 2026-10-30, task 45.13: section 3.10, the chained
unit's own `Removable` answer on NT, reviewed and revised before and with
the code; section 3.9's 2000 reading. Revision 6, 2026-10-30, task 45.13:
section 3.9, the NT chain action as coded. Revision 5, 2026-10-29, task 45.17: section 4.6, the
client's approval order as coded. Revision 4, 2026-10-27, before code, reviewed
and revised: the
Thunderbolt 3 parent's LTSSM never interpreted, at the idle poll and at
every verification; the kid's state table matched to the parent's three
cases. Revision 3, 2026-10-27, before code, reviewed and revised: a
parent HELD by a resume or recovery leaves its kids untouched; the
Thunderbolt 3 parent's link read moved to the PCI side's owed action;
the q35 statement predicate fail-closed on the emulator's identity; the
owed child approvals woken by any path that establishes the parent's.
Revision 2, 2026-10-27, before code, reviewed and revised: the
statement anchors kept to today's batch rule, the q35 statement
qualification separated out, UTO by record 22's predicate, a parent that
fails but stays present tearing its kids down, the owed chain action,
slot power on a chain port (and E10), 44.63's facts held to what was
read, the cross-check's comparison stated. Revision 1, 2026-10-27,
before code. Written for `roadmap-usb4.md`
task 44.67 (the owner, 2026-10-27: one shared record in three parts, the
code in a later release), from task 44.64's sizing (`runs/run-44.md`,
"44.64 Part B: sizing", on branch `p44-chain` at `b4853a29`) and task
44.63's reading of a hot-added switch on 2000 and XP (`runs/run-44.md`,
"44.63", on branch `p44-xpdock` at `645608f5`). Nothing in `src\`
implements it. Section 12 lists the owner's questions; section 13 what is
open and what is a risk.

The three parts:

1. **The PCI side, shared** (section 3): the operating system finding a
   device that appears behind the first device's own PCIe switch after
   the first device is already up, on 98 SE and ME and on 2000 to 10.
2. **The Thunderbolt 3 firmware connection manager's chain** (section 4):
   the ICM client's changes, and the depth-2 support the `usb4-emu`
   emulator needs, specified as requirements for that repository
   (section 5).
3. **The minimal chain in this project's USB4 software connection
   manager** (section 6): two levels, PCIe only, a child connection per
   link rather than a generalised port.

Tags:

- **[spec]** the USB4 Specification 2.0 ("Spec") and the USB4 Connection
  Manager Guide 2.0 ("Guide"), as design record 22 cites them, and the
  PCI Express Base Specification's downstream-port registers as record 21
  section 6.5 cites them.
- **[emu]** the `usb4-emu` repository, read in place and never modified:
  read on its branch `asm4242-model` before the 2026-10-29 squash; the same
  text is on `usb4-emu` `main` after its merge of that work (`03d32f2`,
  QEMU fork `2e063be`): `docs/USB4-HOSTS.md` section 8
  ("Chains"), `docs/XHCI98-HANDOFF.md` (the chain paragraph),
  `docs/ICM-PROTOCOL.md` (R9, R12, R13), `docs/ICM-TITAN-RIDGE.md`,
  `README.md` and `scripts/run-cm-test.sh` (`CHAIN=1`),
  `src/qemu/hw/usb4/usb4-icm.c`, `usb4-tunnel-dsp.c` and
  `usb4-tunnel-slot.c`. What the emulator does: a test bed, not hardware.
- **[linux]** what Linux did or does: the `linux-ts3plus-orico` capture as
  `usb4-emu` records it (the Framework 13 with the CalDigit TS3 Plus on
  host adapter 3 of domain 1 as router 3 and the ORICO AAGM2-U4's
  ASM2464PD on the dock's downstream port as router 303, depth 2), and
  Linux's `drivers/thunderbolt` read for facts only under the rule of
  `AGENTS.md`, "Reference Implementations". Unconfirmed until a hardware
  capture of this project's own shows it.
- **[run]** this project's readings in `docs/contributing/runs/`.
- **[code]** this tree's source at `b24f9f8c`.
- **[inf]** an inference: a hypothesis the design is written to tolerate
  being wrong, and that a named leg or reading settles.

## 1. What it is for

### 1.1 The problem

The release notes say today, under "Which controllers": daisy chains (a
device behind a device) are not supported. Three things stand in the
way, one shared and one per connection manager:

- **The PCI side** [code], [run]. A chained device's PCIe tunnel lands on
  a downstream port of the first device's own PCIe switch, not on a
  tunnel port of the host. On NT this driver invalidates only the tunnel
  ports' bus relations (record 21 section 6.2), so on 2000 and XP, which
  have no native PCIe hot plug, nothing asks Windows to look behind that
  downstream port [inf, 44.64 Part B]. On 98 SE and ME `tbt_brnum.c`
  numbers a fresh tunnel's bridges to exactly the buses they need, closes
  the windows of empty ports and acts only on a tunnel port whose devnode
  has no child (record 21 section 6.1.1), so a device that arrives later
  behind the first device's switch finds no bus numbers and no window.
- **The firmware connection manager** [emu]. The ICM client is written
  for any depth (record 21 section 5.3; `test_tbt_icm.c`'s depth-2
  vectors), but the emulator's firmware model announces depth 1 only:
  `usb4-icm.c`'s header says "Scope: one level of device routers below
  the host router (depth 1)", its hot plug hook drops every event that is
  not from the host router ("Lane 1 events and deeper routers: nothing to
  announce"), and `get route` answers depth 1 only (`ICM-PROTOCOL.md`
  R9). Nothing here can be read until the emulator announces a chain.
- **The software connection manager** [code]. Record 22 enumerates one
  device router per host port; a plug event from a device router's
  downstream port is acknowledged and that port left locked (record 22
  section 1.3 "Out", section 7.1). Discovery of tunnels already present
  reads paths at any depth and watches discovered links (section 9.2),
  but never adopts them.

### 1.2 Scope

In:

- The PCI side on every target: 98 SE and ME, 2000, XP, XP x64, Vista,
  7, 8, 8.1 and 10, in both architectures where the target has both,
  for a device at depth 2 (section 3).
- The ICM client's share of a depth-2 device: approval order, the PCI
  actions, the marks and the eject (section 4).
- The emulator's depth-2 ICM, as requirements for `usb4-emu`, a separate
  repository the owner maintains (section 5).
- The software connection manager's minimal chain: one level below each
  host port's device router, PCIe tunnels only, the child's own
  downstream ports left locked as a depth-1 router's are today
  (section 6).
- The middle device's unplug, the adoption of chains preserved from
  before the start, Stor98's statements per tunnel, and safe removal and
  the marks at depth 2, in each part.
- Host vectors, the `usb4-emu` cross-check and the legs (section 9).

### 1.3 Not in scope

Section 11 is the "Out" list. The largest items: depth 3 and deeper; USB3,
TMU and DisplayPort at depth 2; the 2000 and XP resource-list rewrite for
a multi-port switch, which is design record 33's (section 3.6).

## 2. Facts

### 2.1 The PCI tree of a chain

- **The emulator's chain on q35** [emu] (`USB4-HOSTS.md` section 8,
  "Chains"; `usb4-tunnel-slot.c`): when the dock's own tunnel comes up,
  the tunnel port's slot builds the dock's PCIe switch, an XIO3130
  upstream port with one downstream port holding the dock's NVMe (`drive=`)
  and one gated hot-plug downstream port (`usb4-tunnel-dsp`) per
  downstream USB4 port, standing for the dock's PCIe Down adapter 7. The
  chained tunnel inserts behind that port: for `device-model=asm2464`
  the ASM2464's switch (`1B21:2463` upstream and downstream port) and the
  NVMe; for a plain `usb4-device` with no downstream ports the NVMe
  sits directly on the gated port's secondary bus. The gated port is an
  `xio3130-downstream` with native hot plug (`usb4-tunnel-dsp.c`'s
  header). Each level has its own access gate; a chained tunnel going
  down closes that port's gate only; removing the dock closes both gates
  in one call, leaf first.
- **The real TS3 Plus** [emu]: an Alpine Ridge upstream port and five
  downstream ports (`7d:00.0`, `7e:00.0` to `7e:04.0`), the chained
  device behind `7e:04.0` (`USB4-HOSTS.md` section 8, from the capture).
- **i440FX** [emu]: one tunnel-bridge level. A dock on
  `usb4-tunnel-bridge` gets its own NVMe and nothing else: no switch, no
  gated port, no chained device on the PCI side. The handoff says
  chaining a PCIe tunnel is q35-only, and that "on Windows 98 you can
  still drive the chain's routers and paths over ring 0, but the chained
  NVMe will not appear" there; that sentence is about i440FX, and the
  q35 legs of section 9 are this project's reading on q35.
- **The depth bound of the NT child chain** [code]: `CM_TUNNEL_DEPTH_MAX`
  is 8, counted from the topmost bridge a bridge filter sits on (a
  discrete controller's switch upstream port), so a drive two enclosures
  down sits at 7: the controller's switch ports (1, 2), the dock's
  upstream and downstream ports (3, 4), the enclosure's upstream and
  downstream ports (5, 6), the function (7) (record 31 section 4.3).
- **The 9x numbering's bounds** [code]: six levels of bridges and sixteen
  bridges below a member (`XHCI_TBT_BRNUM_MAX_DEPTH`,
  `XHCI_TBT_BRNUM_MAX_BRIDGES`).

### 2.2 What 2000 and XP do with a hot-added switch [run]

Task 44.63 read, on q35 with the emulator's dock hot-plugged behind a
tunnel port: XP's and 2000's PCI driver number the buses below a
hot-added bridge themselves, within the tunnel port's range (XP gave the
dock's upstream port 4 to 1Bh and its three downstream ports 5, 10h and
1Bh), and give each bridge found with its windows closed a fixed
requirement whatever is behind it: on XP 4 KB of I/O with no alternative,
memory and prefetchable windows of 64 MB down to 1 MB; on 2000 4 KB, 1 MB
and 1 MB, each with no other size. On XP the upstream port and the first
downstream port started and the second and third downstream ports were at
problem 12, nothing behind them started; on 2000 the trace and `info pci`
showed the same placement (the third downstream port closed), and Device
Manager's codes were not read. An enclosure whose switch has one
downstream port (the ORICO model) is placed on XP. A switch the firmware
configured at power-on was kept as found on XP; on 2000 that was not
read. Read on XP for the two-level chain of "44.45 (continued, 2)" too,
with the same placement. 7 x86 placed the same dock. That any hot-added
switch with two or more downstream ports, each found closed, meets the
same is 44.63's [inf], and so is that a chain on 2000 or XP meets it
whenever the first device's switch has more than one downstream port,
which the emulator's dock with a downstream USB4 port always has (one
port for its drive, one gated, `usb4-tunnel-slot.c` [emu]) and the real
TS3 Plus has (five).

### 2.3 The re-attach on 2000 and XP [run]

Record 31 section 4.3 (read 2026-10-25): on 2000 and XP each bridge's
first-time driver install below the tunnel port rebuilds that bridge's
stack and drops the child filters attached below it, and no `BusRelations`
of the parent reaches a filter of this driver afterwards, so nothing
re-attaches. A re-plug, with the bridges known, has the chain of child
filters in place. On 7 the chain was read intact.

### 2.4 The chained tunnel, as Linux built it [linux]

From the `linux-ts3plus-orico` capture (`USB4-HOSTS.md` section 8 and the
handoff's chain paragraph):

1. The ASM2464 at route `0x303`, depth 2, configured like any USB4 device
   router: ROUTER_CS_5 PTO, then CV, waiting for Configuration Ready.
2. The dock's downstream link bonded with LB on the dock's adapter 3
   (`0x37`), Target Link Width dual on adapters 3 and 4 and on both lanes
   of the chained router; the dock then reports adapter 4 unplugged
   (`0x0814803c` on 3:3, then the 3:4 unplug event); LB self-clears (the
   next read of 3:3 `0x37` gives `0x0824003c`).
3. The second tunnel 3:7 <-> 303:3 after the first, 0:9 <-> 3:6: paths
   303:1 -> 303:3 (64 credits), 3:7 -> 3:3, 303:3 -> 303:1, 3:3 -> 3:7
   (32), HopID 8 on 303:1, 3:7, 3:3 and 303:3; then Path Enable on 303:3
   and then on 3:7.
4. The TS3 Plus's event order with a router on its downstream port, at
   the first enumeration: adapters 9, 2, 10, then the downstream lanes 3
   and 4 about 90 ms later; a later re-plug put 3 and 4 before 10 [emu:
   the model always gives the first order, with no delay].

The emulator's fabric follows the HopID 8 paths hop by hop and needs the
parent's tunnel up first [emu].

### 2.5 The emulator's limits for chains [emu]

- The chained PCIe tunnel works on q35 only (above).
- The emulated dock (`dialect=tb3`) has at most one downstream USB4 port
  (`usb4-router-tb3.c`: `ndfp = MIN(ndfp, 1)`).
- The plug-event timing is not modelled (the order is, the 90 ms is not).
- Restore across a chain is not validated (`root-hotplug=restore` belongs
  to the i440FX root-bus bridge).
- Not modelled for chains: the Plug Events + 1 power-on value with a
  device on the downstream port (`0x4c05` in the capture), the dock's
  adapter 0 unplug events, DP OUT, suspend, and the capture's bouncy
  re-plug.
- The internal chain port (`usb4-tunnel-dsp`) has a Power Controller by
  default (`power-controller` true, and the slot that creates it sets no
  other value): an empty slot resets powered off, and a device plugged
  below it stays unpowered, reading all ones, until the guest turns the
  slot on, which 98 SE, ME, 2000 and XP never do (`usb4-tunnel-dsp.c`'s
  own comment). Its Link Capabilities report Data Link Layer Link Active
  Reporting (the same file).
- The firmware connection manager models (`tb3-alpine-ridge`,
  `tb3-titan-ridge`) announce depth 1 only (section 1.1); `run-cm-test.sh`
  documents `CHAIN=1` as `usb4-generic` only.

### 2.6 This tree's state at depth 2 [code]

- `tbt_icm.c`: the device table carries link, depth and route; get route
  by link and depth (AR), the walked route (FR), the route in the event
  (TR, ICL); a disconnect or a replacing announcement removes every entry
  below, deepest first, each with its own tunnel-down action
  (`test_tbt_icm.c`: route `0x0301` at depth 2, "deepest first").
- `tbt_u4cm.h`: `TBT_U4_MAX_PORTS` 4, `TBT_U4_DEPTH_MAX` 5,
  `TBT_U4_MAX_LINKS` 8 discovered links below depth 1 (`TBT_U4_LINK`:
  host port, parent route, the parent's downstream lane 0, the child's
  route, UUID and capabilities), `TBT_U4_ACT_DEEP_REMOVED`; the depth-1
  state is all in `TBT_U4_PORT` (the router, the lane fields, the TMU, the
  PCIe and USB3 tunnels, the generation, the anchor, the recovery flags).
- `cm_nhi.c` (`cmU4ToIcm`): a router removed below depth 1 re-enumerates
  the whole tunnel-port set, as a RESYNC.
- `cm_nvlinkos.c` (`cmNvScan`): Stor98's link-state table gets entries
  for NVMe functions on a member's secondary bus only. An NVMe function
  behind an enclosure's own PCIe switch gets no entry and so no statement,
  and Stor98's gone deadline (MD-1) stands for it (record 21 section 7.2).
  `CM_NVLINK_TUNNEL(port, gen)` names a tunnel by a 7-bit port and a
  24-bit generation (`cm_nvlink.h`).
- `cm_reenum.c` (`CmRegChainRelations`): a child filter on a PCI-to-PCI
  bridge below a member attaches children to the bridge's own new PDOs,
  to `CM_TUNNEL_DEPTH_MAX`, and keeps the registration list's attachment
  entry (record 31 section 4.3).

## 3. Part 1: the PCI side, shared by both connection managers

### 3.1 The chain port

A **chain port** is a PCI-to-PCI bridge below a tunnel-port member, within
the member's bus range, that a chained device's tunnel lands on: the
downstream port of the first device's switch that stands for the PCIe
Down adapter of one of its downstream USB4 ports. No router names it: the
host router's Get PCIe Downstream Entry Mapping (record 22 section 6.3)
covers the host's own adapters, and a device router has no PCI mapping
operation [spec]. So chain ports are found on the PCI side, as tunnel
ports of the switch source are (record 21 section 6.3 item 2):

- **The candidate**: a type 1 function on a bus inside a member's
  secondary..subordinate range, carrying a PCI Express capability whose
  Device/Port Type is a downstream port, with Slot Implemented and, in
  Slot Capabilities, Hot-Plug Capable. That is the emulator's gated port
  [emu] and, by the discrete controllers' own tunnel ports [obs, record 21
  section 6.3 item 2], the expected form of a Thunderbolt or USB4 device's
  downstream port [inf: the TS3 Plus's `7e:04.0` capabilities are not in
  this project's captures; the bench of section 9.5 reads them]. A dock's
  fixed downstream ports (its drive, NIC, xHCI) are not hot-plug capable
  in the emulator [emu] and are not candidates.
- **Slot power.** A candidate whose Slot Capabilities report a Power
  Controller and whose Slot Control reads power off is logged
  (`cm.chain.port-unpowered`) and acted on all the same; this driver does
  not turn the slot on. On 98 SE, ME, 2000 and XP nothing else does
  either, so a device chained there is not reached; on Vista and later
  Windows turns it on where it owns the port's hot plug [inf]. Turning the
  slot on (Power Controller Control, a write to a bridge the PCI driver
  owns, through PCI.VXD's write on 9x and the filter's
  `IRP_MN_WRITE_CONFIG` on NT) is out of the first cut (section 11) and
  waits for the bench's reading of a real dock's downstream port (section
  9.5). The legs run the emulator's chain port without one (section 9.3).
- **The walk**: pure, in `tbt_topo.c` (`XhciTbtTopoChainPorts`, new), over
  the same accessors as the discovery; depth first from the member's
  secondary bus, to `XHCI_TBT_BRNUM_MAX_DEPTH` levels and at most
  `XHCI_TBT_CHAIN_MAX` candidates per domain (4, as many as the child
  connections of section 6.2), each recorded with its location, its
  identity dword, its member and its depth below the member. A failed
  read or a function answering Retry Status makes the walk INCOMPLETE for
  that member, as
  record 21 section 6.3's discovery does, and no chain action acts on an
  INCOMPLETE member's candidates except by the whole-member fallback
  below.
- **When**: at every exposure of a member (a depth-1 tunnel up, a RESYNC,
  the baseline at start), after the PCI side has been re-enumerated and
  numbered, and again before any chain action; the set is per member and
  is emptied when the member's tunnel goes down.
- **An owed chain action.** The member's own enumeration is asynchronous
  (an NT invalidation only requests later PnP processing; the 9x worker
  runs posted work), so a chained tunnel can come up before the first
  device's switch is on the bus, or before its bridges are numbered on
  9x. A chain action that finds no candidate under its member, or an
  INCOMPLETE walk, is kept **owed** on the member with the tunnel's name
  (section 3.5) and retried at each later exposure of that member and at
  each idle tick, for up to 10 s from the tunnel up (the span of record
  21 section 7.2's late scan), then dropped and counted
  (`cm.chain.owed-expired`); on NT it is also retried when the child
  filter of a bridge below the member reports a completed `BusRelations`
  (`CmRegChainRelations`). A change of the parent's or the child's
  generation, or the member's tunnel going down, cancels it. Host vectors
  cover the delayed enumeration (section 9.1).
- **Correspondence**: which chain port a given chained tunnel lands on is
  learned as record 22 section 7.6 learns a host port's member: a new
  function 0 answering on one candidate's secondary bus while that
  chained tunnel is the only one pending under that member. Kept by
  bridge for the life of the parent's connection, withdrawn on a
  conflict, never guessed. Until it is learned a chain action acts on
  every candidate under the member, which is harmless as an invalidation
  or a re-enumeration of an unchanged bus is (record 21 section 6.3).
  With the emulator's dock and the TS3 Plus there is one candidate per
  dock [emu].

### 3.2 Windows 2000 to 10: invalidating the chain port

The vehicle is the child filter of record 21 section 7.4 and record 31
section 4.3, already attached above each bridge of the chain within
`CM_TUNNEL_DEPTH_MAX`. Its attachment entry knows the PDO it sits on and
the PDO's location (`IoGetDeviceProperty`, `DevicePropertyBusNumber` and
`DevicePropertyAddress`, the calls the bridge filter already makes for
its own registration [code]).

- **A chained tunnel up**: the NHI thread asks the child filter whose PDO
  is the chain port (by location and identity, under the registration
  list's lock) to call `IoInvalidateDeviceRelations(pdo, BusRelations)`,
  exactly as the bridge filter does for a member. `pci.sys` then
  enumerates the chained device's upstream port (or its NVMe) below that
  port, numbers and places it, and the child filter chain goes on down
  through it as at depth 1 (record 31 section 4.3).
- **A chained tunnel down** (the end device unplugged, or ejected, its
  parent still there): the same invalidation, after the statement of
  section 3.5; `pci.sys` reports the chained device missing.
- **No filter on the chain port**: the member is invalidated instead and
  `cm.chain.nofilter` counted. Whether `pci.sys` then rescans the bus
  behind a started downstream port of a started switch is not read [inf:
  an invalidation is "re-enumerate this device only" by its contract, so
  probably not]. This is the 2000 and XP first-plug case (section 2.3):
  a never-seen dock's bridges are installed for the first time and lose
  the filter, so a device chained behind such a dock in the same session
  is not found until the dock is plugged again or the system restarted.
  Record 33 designs a registered attachment on every bridge of the chain
  for 2000 and XP (task 44.66, paused); when it lands, this case closes.
- **Native hot plug** on Vista and later: where Windows owns the
  downstream port's hot plug, `pci.sys` may see the chained device's
  presence change itself [inf: on q35, whether the guest's `_OSC` hands
  native hot plug of a switch's downstream port to Windows is not read].
  The invalidation is harmless beside it; the legs read which comes
  first.

Bus numbers and windows are `pci.sys`'s on NT: this driver writes no
bridge register there (record 21 section 6.1.1, "The switch"). On 2000 and
XP the dock itself meets section 2.2's fixed requirement when it is
hot-plugged, and the chain port may be the downstream port left at Code
12, so nothing behind it can start (section 3.6).

### 3.3 Windows 98 SE and ME: bus numbers and windows for a later child

#### 3.3.1 What happens today [code]

When the first device's tunnel comes up, `cmWkNumber` (`cm_cfgwk.c`) runs
`XhciTbtNumberBridges` on the member: it numbers the first device's
switch depth first inside the member's range and narrows each bridge's
subordinate to the last bus of its subtree, so a downstream port with
nothing behind it gets secondary = subordinate; each bridge's memory
window is the size of what its subtree needs, at least 1 MiB, and closed
where nothing behind it decodes memory. PCI.VXD then takes those
registers as the bridges' boot configuration. A chained device that
arrives later sits behind a downstream port with one bus and no window,
and `cmWkNumber` will not touch the member again: its devnode has a child
now ("cm.brnum.9x.held").

Two designs give that later device buses and a window.

#### 3.3.2 Option A: padding at the first numbering (recommended)

`XhciTbtNumberBridges` gains one flag, `XHCI_TBT_BRNUM_F_PAD`: a bridge
that is a chain-port candidate (section 3.1's test, read during the
numbering's own walk) is given, beside what its subtree needs:

- **Buses**: subordinate = secondary + `P`, where `P` is the share of the
  member's range left after every other bridge is numbered, divided
  equally among the candidates, at most 8 and at least 2 (a chained
  ASM2464 needs 2 below the port: its upstream port's and its downstream
  port's secondary buses [emu]). Less than 2 available: no padding on
  that port, logged.
- **Memory**: a window inside the member's, placed as the other windows
  are, of the share of the member's memory window left after every other
  window, divided equally among the candidates, rounded down to a power of
  two, at most 64 MiB and at least 2 MiB (a chained enclosure's two
  bridges take 1 MiB each, nested [code]; a chained dock more). Less than
  2 MiB available: the port gets buses only, logged.
- I/O and prefetchable windows stay closed, as every bridge's do in
  `tbt_brnum.c` today.

A later chained device is then numbered by the same routine run on the
chain port as its member: `cmWkNumber` with the chain port as the range
and window bridge (its padded secondary..subordinate and window), and the
tunnel port as the interrupt anchor - the line rule of record 21 section
6.1.1 step 5 is computed from the tunnel port's pin and line, swizzled
through every bridge between it and the function (the anchor and the
range split into two arguments of `XhciTbtNumberBridges`). A function the
line rule cannot reach keeps design record 28's routes (the configuration
without the interrupt, or the memory-only form), as at depth 1. The chain
port's devnode must have no child, as the member's must today (the
locate of section 3.3.4). Then `Reenumerate_DevNode` of the chain port's
devnode.

The padding stays for the life of the first device's connection: a
chained device unplugged and plugged again is numbered again inside the
same range, because its bridges come back in reset state [emu: the gate
closes the port's subtree].

What A costs: buses and window space set aside on every hot-plug-capable
downstream port of every switch tunnelled on 98 SE and ME, whether a
device is ever chained there or not. A member's range on the P14s is 37
buses and about 192 MB of memory [run, record 21 section 6.1.1]: room
enough. A member with less loses the padding and is logged; nothing else
changes for the first device.

What A rests on [inf, the first leg]: that PCI.VXD keeps a bridge's
window and bus range, as its boot configuration, when nothing behind
the bridge decodes it, and does not close or narrow it at the
re-enumeration that follows the numbering. Record 21 section 6.1.1 read
that PCI.VXD takes a window already in place as a bridge's boot
configuration; it did not read an empty one. A leg on 98 SE and on ME
reads `info pci` after the first device's re-enumeration with the chain
port padded and nothing chained.

#### 3.3.3 Option B: renumbering the subtree

When a chained device arrives behind a chain port with no room, the
worker removes the first device's whole unit (`Query_Remove_SubTree`,
then `Remove_SubTree`, through the removal step of `cm_cfgwk.c` with its
`REMOVE_PENDING` handshake for each present NVMe function, record 21
section 7.2), writes every bridge below the member back to reset state
(secondary and subordinate 0, windows closed; journalled as `tbt_brnum.c`
journals), numbers the whole chain afresh, and re-enumerates the member.

What B costs: the first device's drives go away and come back while the
user did nothing to them, a query-remove vetoed by an open file leaves
the chained device unreachable (logged), and the writes reset bridges
PCI.VXD held until moments before. It is also the only route for a first
device numbered by the BIOS with no padding (present at power-on), unless
the BIOS padded its downstream ports itself [inf: unread for the TS3 Plus
on the P14s; SeaBIOS gives a switch's downstream port no reserve beyond
its subtree on q35 [inf, the leg reads it]].

**Recommendation: A in the first cut; B not coded.** A chained device
behind a first device present at power-on, with no room left by the
firmware, is then not set up on 98 SE and ME, logged
(`cm.chain.9x.no-room`) and in the release notes (attach the whole chain
at power-on, or plug the first device after the start). B can follow as
an opt-in if a bench asks for it.

#### 3.3.4 Finding the chain port's devnode on 9x

The worker walks CONFIGMG's tree from the member's devnode (record 21
section 6.1). A hot-added function's instance id is not always the
`BUS_&DEV_&FUNC_` form (the experiment saw `082800`, record 21 section
6.1), so the chain port's devnode is matched, in this order: an instance
id of that form naming the chain port's bus, device and function; else a
bridge devnode below the member whose allocated bus-number resource
(type `0043h`, the resource record 21 section 6.1.1 read on the tunnel
port) holds the chain port's secondary bus [inf: that PCI.VXD gives a
hot-added bridge such a resource, and the CONFIGMG services that read a
devnode's allocated configuration, are to be read and their numbers taken
from the Windows 98 DDK as record 21 section 6.1 took the others]. A chain
port that cannot be matched is not numbered, logged
(`cm.chain.9x.no-devnode`).

#### 3.3.5 Removal on 9x

A chained device's tunnel down runs record 21 section 6.1's removal on
the chain port's devnode: each child whose function no longer answers
goes through `Query_Remove_SubTree` and `Remove_SubTree`, then
`Reenumerate_DevNode` of the chain port. The first device's tunnel down
runs it on the member, and the walk below the member already reaches
every devnode below (record 22 section 7.1: the removal walk runs
recursively below the domain's tunnel ports), the chained device's
included.

#### 3.3.6 The padding, as designed for its first reading (task 45.14)

Section 3.3.2's Option A made exact for its first part: the padding at
the first numbering, which the leg below reads before the rest of section
3.3 (the chain port's own numbering, the devnode locate, the removal and
`DN_REMOVABLE`) is designed in detail, since all of it rests on that
reading. Where this section and section 3.3.2's first two bullets differ
(the share of buses and of memory), this section governs.

- **The flag.** `XHCI_TBT_BRNUM_F_PAD` (`0x8`) in
  `XhciTbtNumberBridges`. `cmWkNumber` (`cm_cfgwk.c`) passes it at every
  numbering of a member, with `F_WINDOWS` and `F_IRQ` as today; no
  registry value.
- **The candidate.** A bridge the numbering journals that reads, after its
  bus register is written (in a `REOPENED` numbering: once the journal has
  admitted the chain for reopening, its bus registers untouched), as
  section 3.1's candidate: a PCI Express
  capability of Device/Port Type downstream port, Slot Implemented, Slot
  Capabilities Hot-Plug Capable (`XhciTbtFindCapStatus`,
  `XhciTbtPcieHotPlug`). A failed read or a failed capability walk there
  ends the numbering with `ACCESS` (undone, or in a `REOPENED` one with
  nothing written), as any failed read does. One
  level: nothing in a candidate's own subtree is padded.
- **Buses** (a numbering that writes bus numbers, outcome `DONE`). After a
  candidate's subtree is walked, before its subordinate is narrowed: the
  buses still free in the port's range are `R` = the port's subordinate -
  the next bus to give + 1; the pad `P` = the smaller of 8 and `R` / 2,
  and the candidate's subordinate is its subtree's last bus + `P`, the
  next bus to give moved past them. `P` below 2: no bus padding for that
  candidate, counted (`PadNoBus`). Half of what is left, not an equal
  share: the walk numbers depth first in one pass and cannot know what
  later siblings need. **Never at the cost of the dock itself:** a
  numbering with bus padding that ends `NO_ROOM`, once its undo has put
  every bridge back (`UndoFailed` 0), is made again at once in the same
  call without bus padding (counted, `PadDropped`); with an undo that
  failed it is not made again. The journal records each candidate's pad
  (`Pad[k]`, 0 for none).
- **Memory** (the window stage of a `DONE` or a `REOPENED` numbering).
  The windows are computed as today (`brNeed`, the usable span between
  the port window's bottom and top as `brWindows` computes them). When at
  least one candidate qualifies - one with bus padding in this numbering
  (`DONE`), or in a `REOPENED` one a journalled candidate whose retained
  subordinate is at least its secondary + 2 (its bus registers left as
  they are) - the slack (the span - the need) is shared equally: `M` = the
  slack / the qualifying candidates, rounded down to a power of two, at
  most 64 MiB; below 2 MiB no memory padding (counted, `PadNoMem`). `M` is
  space added beside what the candidate's subtree needs: its window is
  that need + `M`, rounded up to its alignment (the 1 MiB grain, or more
  where its subtree needs more). The need is then computed again with
  those windows and checked against the span before any window-placement
  write (the sizing of BARs that `brNeed` makes is today's, restored on
  every pass); if it does not fit (an ancestor's rounding), the padded
  sizes are dropped and the unpadded need, computed again, is placed
  (counted, `PadNoMem`); a sizing refusal (`ACCESS`, `ACTIVE`, `STALE`)
  in either pass is the window stage's refusal, as today. Placement is made once, as today, and a refusal during it
  (`NO_ROOM`, `ACCESS`, `STALE`) is today's refusal: the windows undone,
  no second placement. An empty padded candidate's window is then open,
  where today it is closed. I/O and prefetchable windows stay closed.
  `NUMBERED` (a chain already numbered with a window open) is not touched.
- **Wait for an empty bus.** `F_WAIT_EMPTY` is unchanged: an empty
  candidate is a bridge with nothing behind it, so every try but the last
  returns `NOT_READY` as today, and the last pads.
- **Outcome fields** (`XHCI_TBT_BRNUM`): `Pad[]`, `Padded` (candidates
  padded with buses), `PadMem` (`M` as applied, 0 for none), `PadNoBus`,
  `PadNoMem`, `PadDropped`; logged by `cmWkNumber`
  (`cm.brnum.9x.pad`: padded, no-bus, dropped; `cm.brnum.9x.pad-mem`).
- **What it rests on** [inf]: that PCI.VXD leaves a padded empty bridge's
  bus range and memory window as the numbering wrote them through the
  re-enumeration that follows and through the dock's driver installs, and
  takes them as that bridge's configuration (record 21 section 6.1.1 read
  that it takes a window already in place as a bridge's boot
  configuration; not an empty one). The registers read unchanged is what
  the first leg can show; that PCI.VXD's allocation holds them is shown
  only when a device chained later is placed inside them (the chain
  port's own numbering, after this). If the registers are not kept,
  Option A is not available as designed and section 3.3 is revisited
  with the owner (Option B, or no 9x chain).
- **Vectors** (`test\test_tbt_brnum.c`): the emulator's dock with its
  chain port (one candidate, empty): `P` 8 inside a wide port, 2 inside a
  narrow one, none below 2; a chain port with a device behind it padded
  past its subtree; two candidates (the second gets half of what the
  first left); a dock that fits only without padding (`NO_ROOM` with it,
  made again without, `PadDropped`), and with its undo failing (not made
  again); a non-candidate (no slot, no Hot-Plug Capable, an upstream
  port) not padded; a failed capability read undone; the memory share
  (one and two candidates, a candidate with a device behind it given its
  need + `M`, rounding, the 64 MiB cap, below 2 MiB none, a padded need
  that does not fit dropped to the unpadded); a `REOPENED` chain with a
  retained padded range given its window again, one with a narrow range
  not; `NUMBERED` untouched; the undo with padding; every vector without
  the flag unchanged.
- **The leg** (98 SE and ME, q35, the emulator's Thunderbolt 3 topology
  with the tunnel port padded by its firmware model, `usb4-emu`
  `230bed5`): the dock (`downstream-ports=1`,
  `chain-power-controller=off`) hot-plugged at the desktop with the test
  build; read the log's numbering (`cm.brnum.9x.*`), then `info pci`
  after the re-enumeration and again after Windows installed the dock's
  devices: the chain port's secondary and subordinate and its memory
  window as the numbering left them, or not. That decides Option A's
  first premise. A restart after it is read apart: at a restart the
  firmware numbers the dock present at power-on itself (section 3.3.3),
  so it says nothing of PCI.VXD.
- **Read** [run, 45.14, 2026-10-30]: on 98 SE and ME the emulator's
  chain port padded 07h to 0Fh with a 64 MiB window read unchanged after
  the re-enumeration, the dock's first driver installs and in a later
  session (run-45, "45.14"): the first premise holds; that the allocation
  holds them waits for the chain port's own numbering.

### 3.4 Boot, and chains present at the start

A chain tunnelled before the operating system starts (a pre-boot or
pre-OS connection manager, or this driver's earlier instance) was laid
out by the firmware or by an earlier enumeration; the operating system
finds it at boot, and section 3's actions have nothing to do at start.
On 98 SE and ME the numbering's survey reads it numbered and writes
nothing (record 21 section 6.1.1 step 2). The chain-port walk runs at the
baseline, so a chained device unplugged later is removed through its
chain port (sections 3.2 and 3.3.5); one plugged again behind a port the
firmware left no room on meets section 3.3.3's limit on 9x.

### 3.5 Stor98's statements per tunnel

Today a statement is made per host port's member: on a tunnel down, every
entry `UP` behind that member moves to `DOWN_QUIESCED` when the
condition holds (record 21 section 7.2, record 22 section 10.2). With a
chain, one member carries two tunnels, and a chained device unplugged
alone must not state the first device's drive down.

- **Entries by landing bridge.** `cmNvScan` also scans each chain port's
  secondary bus, as it scans a member's, and the entry's shadow records
  the bridge whose secondary bus the function is on (the member or a
  chain port) and the member above it. Below an enclosure's own switch
  nothing changes: no entry, as at depth 1 (section 2.6).
- **The tunnel's name.** A chained tunnel is named
  `CM_NVLINK_TUNNEL(0x40 | child, generation)`, the child connection's
  index (section 6.2) in the port field's upper half, so it never equals
  a host port's (1 to 4). The rules of record 22 section 10 hold per
  name: only an exposure naming a new tunnel advances an entry already
  `UP`, and a chained tunnel's exposure names it to the chain port alone.
- **A chained tunnel down** states only the entries whose landing bridge
  is that tunnel's chain port, on that tunnel's own condition: its path
  closed and read back on the parent router (section 6.6), then the PCI
  reading of the chain port itself (Data Link Layer Link Active clear,
  record 21 section 6.5's reading, on the chain port) within section
  10.3's allowance. The chain port must be known by correspondence, as a
  member must (record 22 section 7.6). The emulator's chain port reports
  Data Link Layer Link Active [emu]; a real dock's is read at the bench
  (section 9.5).
- **The anchors keep today's batch rule** (`CmNvlinkService`: one sample
  per set, timestamped before its first read, matched only when every
  entry of the set answered with its own ids, so the anchor is no later
  than any recipient's last sign of presence). Two sets now: the host
  port's, every `UP` entry behind the member at both depths, which
  anchors the first device's tunnel; and each kid's, the `UP` entries
  whose landing bridge is its chain port, which anchors the chained
  tunnel. A statement is made only for entries in the set whose anchor
  it uses; an entry with no valid anchor in that set gets none (MD-1).
- **A first device's tunnel down** (the middle unplug) states every entry
  behind the member, at both depths, on the depth-1 condition: every
  packet of the chained device to host memory crosses the host router's
  lane adapter entry `hu` of the first tunnel (section 2.4's paths), so
  the host side read back closed is the path closed for both [spec,
  Spec 5.4.2 as record 22 section 10.2 reads it].
- **The qualification.** Today's admission (`CmNvlinkPortDown`, record 21
  section 7.2) takes a statement only on the emulator's SHPC bridge on
  i440FX, on the systems whose gate G3 ordering was read; a q35 tunnel
  port, a PCI Express port, is not admitted, at depth 1 either. So a
  statement on q35 needs a new admission predicate: the bridge is
  positively identified as the emulator's, fail-closed, as
  `CmNvlinkU4Verdict` requires its emulator verdict today (QEMU's vendor
  id on the port or on the bus-0 bridge whose range holds it; any read
  that fails or disagrees withholds), so a hardware port with the same
  capabilities is never admitted - on hardware no statement is made
  whatever a system's q35 qualification, until record 21 section 7.1's
  boundary is settled; the bridge read is a
  PCI Express downstream port reporting Data Link Layer Link Active, the
  reading is that bit clear (and Presence Detect clear where the slot has
  one) within the allowance, and the system is one on which G3's ordering
  check (the statement's marker later than the emulator's stage-2 reset
  and quiesce event) has been read on q35 for that bridge kind, saturated,
  for the end device's unplug and the middle device's. Until those checks
  pass on a system, MD-1 stands there, at both depths; the qualification
  is a task of its own (section 12, Q4), not part of the chain's first
  code; section 3.11 is its design (task 45.18).
- **The firmware connection manager** sends no statement on q35 today
  (record 21 section 7.2: the condition admits only the emulator's SHPC
  bridge on i440FX, and a set of exactly one member). A chain changes
  nothing there: MD-1.
- The acknowledgement wait and the 9x `REMOVE_PENDING` handshake are per
  entry already and need nothing new.

### 3.6 2000 and XP: what design record 33 owns

Two things stand between a chain and 2000 or XP that this record does not
design:

1. **The first device's switch hot-added** gets resources for one
   downstream port (section 2.2). If the chain port is not that one, it
   sits at Code 12 and nothing chained behind it starts. 44.63's option A
   (rewriting the bridges' requirement lists in the child filter) is the
   fix; it is to be designed in record 33 ("2000/XP hot-added bridge
   requirements", task 44.66).
2. **The child filter lost** at the dock's first-time bridge installs
   (section 2.3), which leaves no vehicle for section 3.2's invalidation
   in the session of the dock's first plug. Record 33's registered
   attachment on the chain's bridges is the same vehicle 44.63 says
   option A needs first.

Until record 33's work lands, a chain on 2000 and XP is expected to work
where the first device was present at power-on (its switch laid out by
the firmware, read on XP in 44.63, not on 2000 [inf]) and its bridges
installed in an earlier session, and the
release notes say so in 44.63's terms. This record's own code still runs
there; nothing in it depends on record 33 to be correct, only to be
useful on a hot-plugged dock.

### 3.7 Safe removal and the marks at depth 2

- **The chained unit.** Record 24 section 4.3's unit is a child devnode of
  a verified tunnel port. A **chained unit** is the child devnode of a
  chain port whose correspondence is known: the chained device's switch
  upstream port, or its NVMe where there is no switch. It is shown, and
  ejected, on its own, nested under the first device's unit; ejecting the
  first device's unit ejects the chained one first (leaf first).
- **NT**: the child filter on the chain port answers the chained unit
  `Removable` (`CmRegAttMayMark` admits a chain port under an active,
  exclusive member with safe eject on, as it admits the member's own
  children), and the eject's bookkeeping (`CmRegAttRemoved`, the marks
  table) reads its events. The mark service's `LowerFilters` mark
  (record 21 section 7.4 revision 3) applies to a chained unit as to any
  unit.
- **98 SE and ME**: the worker adds `DN_REMOVABLE` to the chained unit's
  devnode (`Change_DevNode_Status`, record 21 section 7.4), and the tools'
  9x eject of a chained unit goes through the worker's removal step and
  then, under the software connection manager, back to the thread for the
  chained tunnel's teardown (section 6.7).
- **XHCITR and the status record**: record 24's router entry kind 3
  ("Deeper USB4 router") gets its fields filled for a child connection
  (state, lanes, the PCIe tunnel, the chain port as "the tunnel port its
  PCIe tunnel reaches"); the ICM rows already carry link and depth.
  Record 25's leg T.11 ("the chain case is not readable on the emulator")
  becomes readable.

### 3.8 Files (Part 1)

| File | Change |
|---|---|
| `src/cm/tbt_topo.c`, `tbt_topo.h` | `XhciTbtTopoChainPorts`; chain-port correspondence learning beside `XhciTbtLearn` |
| `src/cm/tbt_brnum.c`, `tbt_brnum.h` | `XHCI_TBT_BRNUM_F_PAD`; the range bridge and the interrupt anchor as two arguments |
| `src/cm/cm_cfgwk.c` | numbering and re-enumeration of a chain port; the devnode locate; removal on a chain port; `DN_REMOVABLE` on a chained unit |
| `src/cm/cm_reenum.c`, `cm_reenum.h` | the chain action: invalidation through the chain port's child filter, the member fallback, counters |
| `src/cm/cm_ports.c`, `cm_ports.h` | `CmRegAttMayMark` for a chain port; the attachment lookup by location |
| `src/cm/cm_nvlink.c`, `cm_nvlinkos.c`, `cm_nvlink.h` | the chain port's scan, the landing bridge in the shadow, the chained tunnel's name and statement |
| `src/cm/cm_status.c`, `cm_statb.h` | kind 3 entries filled; chain counters |
| `xhcitr\` (record 25) | the chained unit nested under its parent's |
| `test\test_tbt_topo.c`, `test_tbt_brnum.c`, `test_cm_nvlink.c`, `test_cm_marks.c`, `test_cm_ports.c` | section 9.1 |

### 3.9 The NT chain action, as coded (task 45.13)

Sections 3.1 and 3.2 made exact for the NT targets' first cut. What 45.13
leaves for later is listed at the end.

- **What 7 does without it** [run, 45.13's leg r2]: on 7 x86 with the
  Thunderbolt 3 topology, the dock present at power-on and the chained
  device hot-plugged, 7 found, numbered and started the chained NVMe
  itself (the chain port `05:01.0`, a hot-plug downstream port, its
  secondary bus 7), before any chain action existed; unplug and re-plug
  of the child, and the dock's unplug with the child attached, were
  clean. So on Vista and later the action is a harmless second request
  beside native hot plug (section 3.2's last item).
- **What XP does without it** [run, 45.13's legs x0 and x1]: on XP with
  the same layout (the dock at power-on, the chained device hot-plugged)
  and the release `xhcicm.sys` of `1a2a72ca`, which has no chain action,
  XP found the chained NVMe (Stor98 installed, Code 0) and removed it at
  its unplug. [inf] The member's invalidation (`CmReenumAction`, every
  member), the only request made there, reaches the started chain
  port's bus on XP; the qemu flavour's log with the action (x1) does not
  separate the two requests. Section 3.2's [inf] ("probably not") is
  therefore not borne out on XP for this layout; 2000 is not read. The
  action stays a second request that costs one invalidation per chain
  port; whether to keep it is the owner's (run-45, "45.13").
- **What 2000 does without it** [run, 45.13's legs w0b, w0c and w2b]: on
  2000 (the Standard PC HAL) with the same layout, the release
  `xhcicm.sys` of `1a2a72ca` (no chain action), 2000 found the chained
  NVMe (its Found New Hardware wizard, Stor98 bound) and removed it at its
  unplug; with the action (the qemu flavour, w2b), the chain port's
  `BusRelations` reported the new PDO before the action's invalidation
  was made. So on all three systems read the member's invalidation (or
  7's native hot plug) is what finds it. There the chained NVMe stayed at
  Code 10: 2000 on the Standard PC HAL gives a hot-added function no
  interrupt, and the child filter's interrupt step (record 21 section
  6.2.1) declines it, the emulated Thunderbolt 3 tunnel port having no
  Interrupt Pin (`cm.chain.irq.no`, why 2); the function would also
  arrive on another pin of the tunnel port (the chain port is device 1 of
  the dock's internal bus), that section's documented limit.
- **The walk** (`XhciTbtChainPorts`, `tbt_topo.c`, pure). From one
  member's secondary bus, depth first, inside the member's secondary
  to subordinate range read from its header at the walk, at most
  `XHCI_TBT_BRNUM_MAX_DEPTH` bridge levels below the member. Every
  device 0 to 31 of a bus, functions 1 to 7 where function 0's header
  says multi-function. A function is the candidate of section 3.1 when
  it is type 1 with a PCI Express capability of Device/Port Type
  downstream port, Slot Implemented, and Slot Capabilities Hot-Plug
  Capable. A candidate is recorded with its location, its identity
  dword, its secondary bus, the member's index and its bridge depth
  below the member, and, when Slot Capabilities report a Power
  Controller and Slot Control's Power Controller Control bit reads 1
  (off), a flag (logged, `cm.chain.port-unpowered`; acted on all the
  same). The walk does not descend below a candidate (one level of chain
  ports, section 11), and descends below every other bridge whose
  secondary bus is above its own bus and inside the member's range, each
  bus at most once. `XHCI_TBT_CHAIN_MAX` (4) candidates per walk; a
  fifth sets an overflow flag (logged, `cm.chain.over`) and is not
  recorded. Outcomes: COMPLETE; INCOMPLETE when a configuration access
  fails, a vendor reads 0001h (Configuration Request Retry Status), a
  capability walk fails, or the member's own header reads all ones or
  unnumbered. No action acts on an incomplete walk's candidates except
  through the member's invalidation.
- **The action** (`cm_reenum.c`, NT filter route only). After
  `CmReenumAction`'s own handling of a `TUNNEL_UP` or a `TUNNEL_DOWN`
  whose `Depth` is 2 or more (which already invalidates every active
  member, so section 3.2's member fallback is paid there), the thread
  walks every active member this domain owns (under the firmware
  connection manager no member is named; a named member alone once Part
  3 names one). For each recorded candidate not yet done for this action
  it looks up the child filter on that candidate's PDO by location in
  the attachment list (`CM_KIND_CHILD`, listed, located at the
  candidate's bus, device and function, not surprise-removed) and, under
  the list's lock, takes that filter's removal reference (`Removing` and
  `Outstanding` under the filter's own lock, refused once its removal
  has begun; the list's lock, then the filter's, an order no path
  reverses), so the call is made before the filter has seen
  `IRP_MN_REMOVE_DEVICE`, whose completion waits for the release; then,
  without the list's lock, `IoInvalidateDeviceRelations(pdo,
  BusRelations)`, the reference released, the candidate recorded done
  (counted `cm.chain.invalidate`, logged with the action's kind and
  depth and the candidate's location). The action is done when every
  walk was complete, at least one candidate was found, and every
  candidate found is done.
- **A chained tunnel down** (section 3.2's second item), and **a
  reconciliation** (`RESYNC`: an action that did not fit the queue, a
  disconnect that matched no entry; not the containment's or the loss's,
  whose publication mode exposes nothing), are tried once, whatever the
  depth for a reconciliation, which names none: the chain ports'
  invalidation lets 2000 and XP report a chained device missing; a chain
  port found and not done is logged (`cm.chain.down.missed`, with the
  action's kind) and not owed (the member was invalidated, and a later
  up of the same device is its own action).
- **Owed.** An up not done (no candidate under any member, an
  incomplete walk, or a candidate with no child filter yet) is kept
  owed: a copy of the action, its first time, what was missing and the
  candidates already done, in one of `CM_CHAIN_OWED_MAX` (4) entries per
  domain keyed by the chained device's identity in the connection
  manager (the action's `Slot`: the firmware connection manager's table
  entry; the software one's port), never by link and depth, which two
  children on one Titan Ridge link share. A newer tunnel of the same
  device (another `Serial`) restarts the entry; a fifth distinct device
  is tried once, not kept, and counted. Each pass of the thread
  (`CmReenumService`, which runs at every exposure and every idle tick)
  tries each owed entry again, invalidating only the candidates not yet
  done, so a child filter's completed `BusRelations` is acted on at the
  next pass. An entry is cancelled when the action is no longer live
  (`CmNhiActionLive`: its table entry gone, replaced, or under the
  software connection manager its port's generation changed - the
  parent's or the child's disconnect, or a new occupancy), at a
  host-interface loss, and at the domain's stop. At 10 s from its first
  time it is dropped, checked before any retry of that pass, and counted:
  `cm.chain.nofilter` when a candidate had no child filter (2000 and XP after a never-seen dock's first-time
  bridge installs, section 2.3), else `cm.chain.owed-expired`.
- **Counters and log labels** (section 8's families): `cm.chain.ports`
  (candidates recorded, per walk), `cm.chain.invalidate`,
  `cm.chain.nofilter`, `cm.chain.owed`, `cm.chain.owed-expired`,
  `cm.chain.owed-full`, `cm.chain.incomplete`,
  `cm.chain.port-unpowered`, `cm.chain.over`, `cm.chain.cancelled`,
  `cm.chain.down.missed`. No registry value and no status-record field:
  the counters are in the log.
- **Left after 45.13's NT code** (this record's sections are their
  design): the chained unit's `Removable` and its eject through Windows'
  own applet are section 3.10's. The correspondence learning (section
  3.1; the action acts on every candidate, which section 3.1 calls
  harmless), the connection manager's per-tunnel ownership, landing
  bridges, names and generations (section 3.5 and Q4) and the status
  record's kind 3 entries (section 3.7) have no reader before Part 3's
  child connection: no statement is made for a chained drive (the owner),
  the firmware connection manager states nothing on q35, and kind 3 is a
  child connection's row; until then a chained drive is held by Stor98's
  own gone deadline, MD-1. The owner moved them to task 45.15, Part 3
  (2026-10-30; run-45, "45.13"). The 9x half is task 45.14.

### 3.10 The chained unit on NT, as designed (task 45.13)

Section 3.7's NT item made exact. What it is for: in 45.13's legs x1 and
x2 a file written to a chained drive just before its surprise unplug was
lost on XP ("Delayed Write Failed"), and the chained drive was not
offered for safe removal on its own (run-45, "45.13"). This section gives
the chained unit its own `Removable` answer, so that Windows lists it in
its Safely Remove Hardware applet and its own eject stops it before the
unplug; it does not make a surprise unplug of an unstopped chained drive
safe [inf: whether 2000 and XP cache differently for a `Removable`
ancestor is not read; the legs read the eject, then the unplug].

- **When it must be known.** Windows asks a new device's capabilities
  (`IRP_MN_QUERY_CAPABILITIES`) after the parent's `BusRelations` answer
  that reports it, before and after its start, and caches the answer; no
  later change of this driver's state reaches it (record 21 section 7.4
  says the same of a member's unit). So the chained unit's entry, and the
  fact that its parent is a chain port, must exist when the chain port's
  `BusRelations` answer that reports it goes up: they are made in that
  answer's own completion path, not on the thread.
- **The chain port's classification.** A child filter on a PCI-to-PCI
  bridge reads its `BusRelations` on the way back (`CmRegChainRelations`,
  record 31 section 4.3). There, before the answer goes up and before its
  new PDOs are attached, it classifies its own PDO (PASSIVE_LEVEL, the PnP
  dispatch; configuration reads through `cmCfgRead`, as
  `CmRegIrqWire` reads in the same context):
  1. its location known (`Located`, read at its start: a chain child
     reads its `BusRelations` only once started; not read again here);
  2. its ancestry (`Parent`, then `Bridge` for a child with none, the walk
     of `CmRegIrqWire`) reaches a filter whose location is an active member
     of a filter route's set; every bridge between them is read now by the
     same test and must read NOT: a numbered bridge that is no candidate
     (one level of chain ports, section 3.9; a bridge between them that is
     one, or reads anything else, withholds the classification);
  3. its configuration, read now: present (not all ones, not Retry
     Status), a type 1 header, the candidate of section 3.1 (a PCI Express
     capability of Device/Port Type downstream port, Slot Implemented,
     Hot-Plug Capable), and its secondary and subordinate buses valid:
     secondary above its own bus, subordinate at or above secondary, both
     inside the member's secondary + 1 .. subordinate read from the
     member's header now (so never the member's secondary bus); a dword
     of its capability reading all ones (a function that stopped
     answering, through an access that succeeds) fails it. This test is a
     pure function in `tbt_topo.c` (`XhciTbtChainPortTest`), host tested,
     with six outcomes: YES (a numbered candidate), NOT (a numbered bridge
     that is no candidate), UNNUMBERED (a bridge in the range whose buses
     fail the rule), OUTSIDE (a function outside the range, or no bridge),
     MEMBER (the member's own header unread or unnumbered), FAILED. The
     bridge itself is admitted on YES alone, a bridge between it and the
     member on NOT alone. It is stricter than the walk's candidate test of
     section 3.9, which records an unnumbered candidate for its
     invalidation.
  The ancestry is copied under the registration list's lock and the
  reads made without it; the outcome replaces the filter's earlier one
  under the lock, a positive one only while the member is still active:
  `ChainLanding` (its 16-bit location with a valid bit), `ChainSec`, and
  the member's location `ChainMember`; a failed read, or any test not
  met, clears it (logged when it changes: `cm.chain.class` with the
  secondary bus and the location, `cm.chain.class.no` with why). It is
  made again at every `BusRelations` answer of that filter; the filter
  object is the incarnation (it is deleted with its PDO). The thread walk
  of section 3.9 is unchanged and separate: it decides invalidations
  only.
- **Recovery.** A chain port whose `BusRelations` was answered before its
  member was active, or whose reads failed then, is classified again when
  a child of it asks its capabilities (`CmRegChainRecheck`, in the child
  filter's `IRP_MN_QUERY_CAPABILITIES` path before the `Removable` gate,
  not for a request this driver's own location query caused), the
  parent's removal reference held across it (`CmFilterTryHold` under the
  list's lock, as section 3.9's action takes it; a parent whose removal
  has begun is not read, and one retired meanwhile carries no landing):
  when it now is one, the marks are synced and the gate reads the chained
  unit's entry. Windows asks a device's capabilities at least before and after
  its start, so a chained unit missed at the first is answered at the
  second; one missed at both is offered with its parent's unit only, until
  it is plugged again.
- **Retirement.** A chain port's child filter seeing
  `IRP_MN_SURPRISE_REMOVAL` (`CmRegAttSurprise`), or its removal
  (`CmRegAttRemoved`), clears its classification and syncs the marks, so
  its chained entries are buried at once, not at a later sync. A landing
  filter must be listed, located and not surprise-removed to carry
  entries.
- **Locations.** As every attachment's today (record 21 section 7.4), a
  chain port's and a chained unit's location is the one read at the
  start: a rebalance that renumbers a running bridge without removing it
  is not followed. Then the chain port's test reads another location: a
  bridge that is not the candidate there clears the classification and
  buries the chained entries, so later capability requests are answered
  without `Removable`; an answer Windows already holds stays until the
  device is removed or enumerated again; nothing else changes.
- **Two filters on one chain port** (a `BusRelations` child and an
  `AddDevice` one, the mark service's): both classify alike and see the
  same answer; the sync takes the first listed as the landing's snapshot,
  and a chained entry's location is kept in every filter classified as
  that landing, so either serves.
- **The chained unit's marks entry.** The marks table (record 21 section
  7.4, revision 3; `cm_marks.c`) keeps, beside each member's own entries,
  the PDOs in the last snapshot of each child filter classified as a chain
  port under that member. Such an entry has its member's index and BDF and
  its **landing** (`Landing`: the chain port's location with a valid bit;
  0 for the member's own entries), kept through its burial. The sync of a
  member (`cmMarksSyncAll`) runs in two phases: first it buries every
  current entry of the member that is not wanted where it stands (a member
  entry whose PDO is not in the member's snapshot; a chained entry whose
  landing is no longer a listed, located, classified child filter under
  this member, or whose PDO is not in that filter's snapshot; every entry
  when the member is not active); then it makes the missing entries, the
  member's own first, then each landing's, and gives each the location the
  snapshot kept (`CmMarksLocate` on the landing's snapshot too). A PDO
  that moves from one landing to another is buried in the first phase and
  made again under its new landing in the second, in the same sync. A PDO
  already holding a current entry anywhere is not entered twice (as
  today). The chained entries count against their member's current and
  tombstone bounds. `CmRegChainRelations` keeps the per-PDO locations of
  its snapshot as `CmRegRelations` does (`SnapLocd`, `SnapBus`,
  `SnapAddr`, carried by PDO into the new snapshot), `cmMarkAtt` keeps a
  chained entry's location in its landing filter's snapshot (where
  `cmSnapKeepLoc` keeps a member entry's in the member's; in every filter
  of that landing), and the answer
  ends with `cmMarksSyncAll`, as `CmRegRelations` does - all before it
  goes up, so the entry exists when Windows asks the new PDO's
  capabilities.
- **The answer** (`IOCTL_XHCICM_MARKS`, `cm_query.h`): an entry with a
  landing carries the new flag `CM_QM_EF_CHAINED` (`0x40`, derived from
  `Landing` when the answer is built) and its landing in the entry's
  reserved dword at offset 40, now `CM_QM_E_LANDING` (the chain port's bus
  15:8, device 7:3, function 2:0; 0 without the flag). The version stays
  1: a reader that does not know the flag ignores it and the dword. The
  mark service takes as units only direct children of the member's
  devnode (`mk_check_entry`), and XHCITR and XHCIINST match an entry to a
  unit by the member and the child's bus and address; a chained entry's
  bus is its chain port's secondary bus, never the member's, so none of
  them takes a chained entry for a unit of the member.
- **The `Removable` gate** (`CmRegAttMayMark`). For an attachment whose
  PDO's current entry has a landing, the bus compared is that landing
  filter's `ChainSec` now, and the landing must still be a listed,
  located, classified child filter under the same member, which must be
  active and exclusive with safe eject on; `CmPortsMayMark` is called
  with `ChainSec` in place of the member's secondary bus. An attachment
  whose entry has no landing answers as today. So the child filter the
  chain port's `BusRelations` attached on the chained unit's PDO answers
  `IRP_MN_QUERY_CAPABILITIES` with `Removable` set on the way up, as the
  member's children's do.
- **The confirmation** (`CmMarksConfirm`) refuses an entry with a landing,
  current or a tombstone (`CM_MK_CONF_CHAINED`, 7, logged with
  `cm.reenum.confirm.refused`): no member eject is posted for a chained
  unit. Windows' own eject of the chained unit (the applet's, or a tool's
  `CM_Request_Device_Eject`) is then the whole eject: under the firmware
  connection manager no unit's eject tears a tunnel down (record 21
  section 7.4), and under the software connection manager a member eject
  would tear down the first device's tunnel; the chained tunnel's own
  teardown is Part 3's (section 6.7). A tombstone's departure kept for a
  confirmation stays member-wide, as today (a new function behind the
  member clears every tombstone's of that member).
- **Cost.** A handful of configuration reads per `BusRelations` answer of
  a child filter on a bridge (the bridge's header and PCI Express
  capability, the member's bus numbers): no thread pass, no periodic
  walk.
- **Not in this cut.** XHCITR's chained unit nested under its parent's,
  with its own eject (record 25's leg T.11; until then the tool shows the
  chained devices under the first device's unit and its eject of the
  first device's unit takes the subtree); the mark service's
  `LowerFilters` mark on a chained unit (the chain port's child filter
  attaches the chained unit at its `BusRelations`; the 2000 and XP first
  plug of a never-seen dock, which loses that filter, is record 33's);
  the 9x `DN_REMOVABLE` (task 45.14); the correspondence learning, the
  per-tunnel names and generations, and the status record's kind 3
  entries (section 3.9's list).
- **Vectors.** `test\test_tbt_topo.c` (`XhciTbtChainPortTest`): the
  emulator's chain port; a fixed downstream port, an upstream port, a
  root port, a type 0 header; Slot Implemented or Hot-Plug Capable clear;
  unnumbered, secondary at or below its own bus, subordinate below
  secondary, outside the member's range, equal to the member's secondary;
  a failed read, all ones, Retry Status, a failed capability walk.
  `test\test_cm_marks.c`: a landing synced beside the member's own,
  neither burying the other; a landing gone buries its entries; the
  member made inactive buries both kinds; a PDO moving between landings
  in one sync; the two phases freeing the member's bound before the new
  entries (a full member whose old landing's entries give way to a new
  landing's in one sync); the location of a chained entry kept through
  a burial and remade from its landing's snapshot; the answer's flag and
  landing, and 0 for a member's own entry; the confirmation of a chained
  entry, current and tombstone, refused with `CM_MK_CONF_CHAINED` and a
  member's own still accepted.
- **The log.** `cm.chain.removable`, a chained unit's gate reading when it
  changes (the verdict, whether the chain port was found, the unit's bus,
  the chain port).
- **Legs** (2000 and XP, the dock at power-on, the chained device
  hot-plugged, section 9.3 clause 5): the log's `cm.chain.class` for the
  chain port; `XHCIINST /devices` reads the chained NVMe "removable: yes";
  the Safely Remove Hardware applet lists it on its own; a file written,
  the chained unit stopped from the applet, then the child unplugged: the
  file there at the next plug; the dock's drive untouched. On 2000 the
  emulator's chained NVMe does not start (no interrupt; run-45, "45.13"),
  so 2000 reads the `Removable` answer and the eject's listing only.

- **Deferred** (owner, 2026-10-30; task 45.15). Under the software
  connection manager too, the confirmation keeps refusing a chained
  entry in `3.4.0.0`: Windows' own eject stops the chained unit and its
  tunnel stays up until the unplug, as above. Taking the confirmation to
  the kid's teardown (`TbtU4EjectKid`) is deferred to the next release
  together with the chain port's correspondence to a kid (section 3.1)
  and the per-tunnel ownership, names and generations (section 3.5, Q4):
  an eject attributed to a kid without a learnt correspondence could tear
  down another live chained tunnel. The roadmap's 45.15 line
  (`roadmap-usb4.md`) holds the plan for that release.

### 3.11 Stor98's statements for chained drives, and their q35 admission (task 45.18)

Section 3.5 made exact for the code of task 45.18 (the owner's Q4, a
leaner (a): the chain work builds the per-tunnel ownership, names and
generations; the anchors and the emission for chained drives come here,
with their q35 qualification). Nothing here is coded before 45.13's
per-tunnel keying (section 3.9, "Left in 45.13") and 45.15's kid
(section 6) are in the tree, except the depth-1 half of the admission
(item 10). Until a system passes its readings (item 8), MD-1 stands
there, at both depths, as today.

1. **What changes, and what does not.** The statement's meaning, its
   channel and Stor98's side are unchanged: an entry is a function by
   bus and slot, at any depth (`stor98cm.h`, entry protocol), and a
   chained drive is one more entry of the same table, so Stor98 needs no
   change and no interface version moves (its `AGENTS.md`: the isolation
   proof is the statement's, MD-1 the one exception). The i440FX
   admission (record 21 section 7.2, "The statement"; `CmNvlinkIcmVerdict`
   and `CmNvlinkU4Verdict` with the SHPC source) stays as it is. What is
   new is a second admission, for a landing bridge that is a PCI Express
   port on the emulator's q35 machine, at depth 1 (a member) and at depth
   2 (a chain port), under the USB4 software connection manager only
   (item 9).
2. **The cases.** Three, each a column of the qualification (item 8):
   - **D1, an end unplug at depth 1**: a member's own tunnel down, the
     entries whose landing bridge is that member, on the member's reading
     (record 22 section 10.2, as today but with the q35 admission).
   - **D2, an end unplug at depth 2**: a kid's teardown (section 6.7 step
     3), the entries whose landing bridge is that kid's chain port, on the
     chain port's reading after the parent-side read-back.
   - **M, the middle unplug**: the host port's teardown with kids under it
     (section 6.8 step 2), the entries behind the member at both depths on
     the member's reading. At depth 1 behind a dock there is no entry (the
     dock's own functions sit behind its switch, section 2.6), so in
     practice M states only the chained entries; an entry landing on the
     member itself is judged as D1. M's bridge is the **member**, the one
     that survives: items 5 and 6's reading, capability tests and current
     identity read are the member's, and M's column is the member's kind
     (item 8). The chain port is gone with the dock (it reads all ones),
     so for M it is only the bridge the entry was published on, its
     identity the one recorded at the walk; the entry's own endpoint test
     (item 6) applies as everywhere.
   An entry gets a statement only from the case its landing bridge and the
   teardown put it in, and only when that case is qualified on the running
   system and kind (item 8). Otherwise the entry is withheld by today's
   rule (record 21 section 7.2, "The statement"; `cmNvMarkEx`): an entry
   whose function no longer answers moves to `IN_USE` alone (`UP`
   cleared, no `DOWN_QUIESCED`), and one whose function still answers -
   a failed read-back with both ends present, say - keeps its `UP`, so no
   later exposure that names no new tunnel advances its generation under
   a bound Stor98 instance.
3. **The anchors** (section 3.5's two sets, made exact). `CmNvlinkService`
   samples per set, one sample per set per idle tick, timestamped before
   its first read, and matched only when every entry of the set answered
   with its own ids for its own generation: the host port's set (every
   `UP` entry behind the member, both depths), and each kid's set (the
   `UP` entries landing on that kid's chain port). D1 and M use the host
   port's anchor, D2 the kid's. Record 22 section 10.3's deadline holds per
   statement: delivered within 3 s of its set's anchor, at least 100 ms of
   it left before the table write (`CM_NVLINK_DELIVERY_MS`), else
   withheld (`CM_NVLINK_LATE`). An entry that is not in a set with a valid
   anchor gets none (MD-1). A kid's sample is taken only while the parent's
   connection is the one the kid's generation was recorded under (the
   liveness rule, section 6.2), so a sample never spans a middle unplug.
4. **The emission points.** D1: `CmNvlinkPortDown` from the DOWN hook, as
   today. D2: a new entry point beside it, called from the kid's teardown
   at section 6.7 step 3, with the host port, the kid, the parent-side
   `PathClosed` and the time left; it states only entries whose landing
   bridge is that kid's chain port, known by correspondence (section 3.1;
   an unlearned chain port states nothing). M: `CmNvlinkPortDown` for the
   host port, after section 6.8 step 1 dropped the kids (no register work,
   no statement of their own), stating the member's entries by D1's rule
   and the chained ones by M's. A chained tunnel's name (section 3.5,
   `CM_NVLINK_TUNNEL(0x40 | kid, generation)`) keys the kid's entries, so
   a D2 statement never touches the first device's entries, and a re-plug
   of the child advances only the child's. On 98 SE and ME the reading is
   entered as a decision and made on the member's call worker as at depth 1
   (record 21 section 7.2 part 2), with the chain port's location in place
   of the member's; the verdict follows at `CmNvlinkDecided`.
   **The gates.** An entry's acknowledgement wait holds the removal of
   its landing bridge and of every bridge above it within the domain:
   the shadow records the member above the landing bridge (section 3.5),
   and the member's gate (`CmNvlinkHeld` for the member) is up while any
   waiting entry's landing bridge is the member or a chain port below it,
   not only while one's bridge is the member itself, as
   `CmNvlinkWaitHeld`'s exact match has it today. So an M that follows a
   D2 still waiting holds the member's removal and exposure until that
   wait ends, although the entry, already `DOWN_QUIESCED`, is not marked
   again. The chain port's own removal requests - section 3.9's
   invalidation through the chain port's child filter at a chained tunnel
   down or a reconciliation, and on 98 SE and ME the worker's removal of
   the chain port's child - ask the chain port's gate first and are held
   and run from a later pass, as the member's are; the chain action's
   owed entry keeps them (section 3.9, "Owed") meanwhile.
5. **The q35 reading** (the condition proper). On the landing bridge,
   within the time left: a type 1 function with a PCI Express capability
   whose Device/Port Type is a root port (4) or a downstream port (6),
   Slot Implemented, Slot Capabilities Hot-Plug Capable and **no Power
   Controller**, Link Capabilities Data Link Layer Link Active Reporting
   Capable; and
   `XhciTbtPortRead` answering DOWN, which needs **both** Data Link Layer
   Link Active clear and Presence Detect State clear (its vote; MIXED,
   either one still set, withholds). Link Active alone is not enough on
   this emulator: on its PCI Express slots a down transition that is a
   link retrain (a teardown the connection manager makes with the router
   still attached: an eject, a failure with both ends present, a resume)
   clears Link Active at stage 1, before stage 2 resets the endpoint, and
   keeps Presence Detect set [emu: `docs/ARCHITECTURE.md` section 4.4,
   "Link retrain"; `usb4-tunnel-slot.c`, `ts_set_state`]. A surprise
   removal (the router unplugged) is not a retrain: stage 2 resets and
   drains the endpoint (`ts_quiesce`, then `usb4_tp_cutoff`), and stage 3
   removes it, which clears Presence Detect and Link Active [emu: the same
   bottom half, `ts_bh_work`; QEMU's PCI Express slot unplug]. So on q35
   Presence Detect clear after a disconnect is the later-than-stage-2
   signal, as the SHPC slot's presence is on i440FX (record 21 section
   7.1). That holds only while no guest command removes the slot's
   occupant: a write of Slot Control turning a populated slot's power
   off (the system's own hot plug, or an attention-button eject) marks
   the slot empty at once, Presence Detect and Link Active cleared in the
   write, while the occupant's removal waits for the bottom half and runs
   no stage 2 (`pcie.c`, `pcie_cap_slot_write_config` and
   `pcie_cap_slot_mark_empty`; `usb4-tunnel-slot.c`,
   `usb4_tunnel_slot_guest_unplug`) [emu]. Removing the Power Controller does not remove the path: the emulator
   tests the value written, not the bits Slot Control keeps, so a write
   with the power-off code reaches the unplug on a slot that has none
   (`pcie.c`, `pcie_cap_slot_write_config`, `pcie_sltctl_powered_off(val)`)
   [emu]. Nothing on the bridge can tell afterwards that it happened. So
   the admission rests on the system: only a system that writes no Slot
   Control on these ports can be qualified. This driver never writes it
   (section 3.1); 98 SE, ME, 2000, XP and XP x64 have no native PCI
   Express hot plug [inf: none of them is known to write it; every run of
   item 11 carries `-trace pcie_cap_slot_write_config` and a run with such
   a write on the stated bridge fails]; Vista and later do run native hot
   plug on these ports (section 3.9: 7 found the chained device itself),
   so their bits cannot be set until `usb4-emu` runs stage 2 before a
   guest slot-off marks the slot empty, or ignores the power-off code on
   a slot without a Power Controller (section 12, Q11). The admission
   still requires a slot without a Power Controller, so the slot's own
   power never hides a chained device on the systems that cannot turn it
   on (section 2.5): the chain port with `chain-power-controller=off`, and
   a member port given none on the command line (the property read at the
   first leg [inf]). A retrain reads
   MIXED and is withheld: an eject's teardown on q35
   therefore states nothing (the unit was removed from the system before
   it, so no Stor98 instance waits), and a both-ends failure is left to
   MD-1. A function 0 still answering on the landing bridge's secondary
   bus withholds as today.
6. **The emulator's identity, fail-closed.** All of these, each read
   successfully, or no statement:
   - **The endpoint**: the entry's function carried QEMU's vendor id
     1B36h when its `UP` was published (the scan's read of its ids, kept
     in the shadow; QEMU's NVMe controller is `1B36:0010` [emu]). This is
     the part that excludes a passed-through device. The i440FX test's
     premise, that no Thunderbolt or USB4 hardware sits below a 1B36h
     bridge (`cm_nvlinkos.c`, `cmNvEmulator`), does not hold on q35 by
     itself: a host controller passed through to a q35 guest sits below
     QEMU's `pcie-root-port` (1B36h), and its own downstream ports are
     PCI Express ports with Link Active and Presence Detect, as the
     emulator's are. On i440FX the SHPC-without-PCI-Express test is what
     excludes such a port; on q35 nothing about the port does. A function
     QEMU emulates can only sit on a bus QEMU emulates, so an emulated
     endpoint puts the whole path to it under the emulator's gates [inf:
     QEMU attaches no emulated function below an assigned device]; a
     passed-through drive carries its maker's id and is never admitted.
   - **The platform**: the landing bridge itself when it is on bus 0, or
     else a bridge on bus 0 whose secondary..subordinate range holds the
     landing bridge's bus, carries 1B36h (on q35 the `usb4-tunnel-port`
     and QEMU's `pcie-root-port` are `1B36:000C` [emu]; a `usb4-generic`
     member is itself on bus 0), read now, as `cmNvEmulator`'s two
     branches read it; and the
     landing bridge's own identity read now matches the one recorded
     when the entry's `UP` was published (the shadow's `BridgeId`, read
     by the scan that published it; an unread one is 0 and never matches).
   - **The kind**: the landing bridge's Device/Port Type, root port or
     downstream port, selects the qualification column (item 8); the
     chain port is always a downstream port (`usb4-tunnel-dsp`, an
     XIO3130 downstream port, `104C:8233` [emu]), a `usb4-generic` member
     a root port (`usb4-tunnel-port`), an `asm4242` host's member a
     downstream port of its switch. A kind with no reading has no column
     and is withheld.
   Not taken: the emulator's debug port (`usb4-debug-port`, its magic
   `U4DB` at 5A0h, `docs/HARNESS.md`): it is a harness device a leg may
   omit, its base is a property, and an I/O read at a fixed port in a
   shipping image is an access this driver makes nowhere else. On hardware
   the endpoint test alone already fails, and the record 21 section 7.1
   boundary stands: no hardware statement, whatever the qualification.
7. **The ordering on the emulator, per case.** What G3's ordering check
   (Stor98's gate G3, pass condition (2)) compares, per stated entry, in
   the host trace:
   - D1: the statement's marker later than the member's own slot's
     `usb4_tp_cutoff` for the endpoint's generation.
   - D2: later than the chain port's slot's `usb4_tp_cutoff` for the
     chained endpoint's generation.
   - M: later than the chain port's own slot's `usb4_tp_cutoff` for the
     chained endpoint's generation, as D2. The member's stage 2 also
     resets the dock's switch upstream port with a cold reset, which
     QEMU's reset tree carries to every function below it, and then drains
     all block I/O (`ts_quiesce`: `device_cold_reset`, `blk_drain_all`)
     [inf: from the emulator's code], but nothing in the trace shows that
     reset for the chained controller alone: QEMU's NVMe reset event
     (`pci_nvme_pci_reset`) names no controller, and the dock's switch
     always holds an NVMe controller of its own, `drive=` or not (an unset
     `drive=` only leaves it without a namespace; `usb4-tunnel-slot.c`,
     `ts_build_switch`). So M rests on the chain port's cutoff alone.
     Whether the chain port's bottom half runs its stage 2 before the
     member's stage 3 removes the subtree is not documented [emu: removing
     the dock closes both gates in the same call, leaf first, `docs/
     USB4-HOSTS.md` section 8; the bottom halves' order is not stated]. An
     M run whose chain-port cutoff is missing or later than the marker
     fails; if that is the emulator's order, M stays MD-1 on every system
     until `usb4-emu` guarantees the leaf's stage 2 first or traces the
     endpoint's reset with its identity (a request for that repository,
     section 13).
   In each case also: the statement **received and acknowledged** before
   that system's first removal request for the function (gate G3's pass
   condition (2) asks for receipt, not publication): Stor98's receipt
   and acknowledgement stage codes of its debug-port build - on NT
   `0x141` and `0x142` (`STG_CML_SEEN`, `STG_CML_ACK`, its `cmlink.h`), on
   98 SE and ME `0x124` and `0x125` (`STG_P9_LS_SEEN`, `STG_P9_LS_ACK`,
   its `pool9x.h`), the acknowledgement's argument showing
   `AckGeneration` written for the stated generation - and the wait's
   end `0x1C1` with an acknowledged result, not a timeout, all before the
   removal boundary below; a run whose wait
   timed out does not pass. The removal boundary: Stor98's own
   stage codes do not show it: no stop callback reaches the miniport at
   an NT surprise removal (Stor98's gate G2), and ME never calls its stop
   callback. The boundary is read where it is first seen: on NT by this
   driver's child filter on the function's own PDO (record 31 section
   4.3), at its first `IRP_MN_QUERY_REMOVE_DEVICE`,
   `IRP_MN_SURPRISE_REMOVAL` or `IRP_MN_REMOVE_DEVICE`, whichever system
   made it; on 98 SE and ME, where only this driver removes the function,
   at its worker's `CM_Query_Remove_SubTree` (task 43.18's `0x1C2`). A run
   whose function has no child filter (section 4.3's loss on 2000 and XP)
   has no reading of that clause and does not count;
   and under a saturated queue the held requests completed by the
   statement, not by the gone deadline (pass condition (3); Stor98's
   MD-1 count unchanged).
8. **The qualification table.** Compiled in, as `CM_EJ_QUALIFIED_98SE`
   and `_ME` are for i440FX (record 21 section 7.2 part 2): one bit per
   system, port kind and case - systems 98 SE, ME, 2000, XP, XP x64,
   Vista, 7, 8, 8.1 and 10, each architecture separately; kinds root port
   and downstream port; cases D1, D2 and M (D1 and M keyed by the
   member's kind, root port or downstream port; D2 by the chain port's,
   always a downstream port). Every
   bit is 0 when the code lands, and each is set by a commit that cites
   the reading that passed it (`runs/run-45.md`, "45.18"). The system is
   told apart by what already tells each one apart here: 98 SE from ME by
   a version the system answers - the VMM's (4.10 and 4.90) or
   CONFIGMG's `Get_Version`, which `cm_cfgmg.c`'s guard already calls -
   chosen at the first 98 SE and ME leg, whose reading of the exact words
   comes before that code [inf]; until then 98 SE and ME are an unknown
   system and no q35 bit applies there (`CmEjQualified9x` only returns
   the constant its caller names, and today's caller asks for both); on NT the version in the kernel's shared data
   page, as record 31's `cmChildIds` reads it (`IoIsWdmVersionAvailable`
   cannot tell Vista from 7 or 8 from 8.1), major and minor exactly, and
   `_WIN64` for the architecture; an unrecognised system has no bit. A bit is set only when every run of item 11's set
   for that system, kind and case passed; one failure leaves it 0 and is
   recorded with its trace.
9. **The firmware connection manager** keeps MD-1 on q35 (section 3.5;
   section 4.3): its condition needs a set of exactly one member, which
   the `tb3-*` profiles' q35 layouts do not have, and it has no
   path read-back. Extending this admission to it is not part of 45.18
   (section 12, Q10).
10. **Depth 1 first.** Items 5, 6 and 8's D1 column need nothing of the
    chain, and are the first code of 45.18, on NT: `CmNvlinkPortDown`
    takes a member that is not the SHPC bridge to `CmNvlinkU4VerdictQ35`
    (`cm_nvlink.c`, beside `CmNvlinkU4Verdict`), with the kind
    (`CmNvlinkQ35Kind`), the platform (`cmNvEmulator`) and the system's
    bit (`CmNvlinkQ35Qualified`) read first, so a port that cannot be
    admitted - every hardware port - costs no reading loop; only then the
    reading within the time left, and the mark tests each entry's
    endpoint and bridge identity (`cmNvMarkEx`'s q35 identity). Every bit
    is 0 in that code, so it changes nothing a user runs until a reading
    sets one. On 98 SE and ME a PCI Express reading needs no write and
    could be made on the thread, but the system is not told apart yet
    (item 8), so the 9x half of D1 - the system's identification and,
    if its leg shows the thread's read unfit, the call worker's route -
    comes with the 98 SE and ME legs. D2 and M follow with the kid.
11. **The legs** (q35 only; `usb4-emu` with `-device usb4-debug-port`,
    `-trace usb4_tp_*`, `usb4_dbg_*`, `usb4_h_*` and `pci_nvme_*`):
    `usb4-generic` with `host-model=intel-adl`; a plain NVMe router on
    port 1 (D1, kind root port); on port 2 the dock (`dialect=tb3,
    downstream-ports=1, chain-power-controller=off`) with a plain NVMe
    router behind it (D2, M). A chained `device-model=asm2464` enclosure
    gets no entry (its drive is behind its own switch) and is not a
    qualification vehicle. The build: the `qemu` flavour of `xhcicm.sys`
    with the qualification bits under test set, writing the stage codes
    of task 43.18 (`0x1C0` an entry marked `DOWN_QUIESCED`, `0x1C1` its
    wait ended, `0x1C2` the removal call; `runs/run-43.md`, "43.18") with
    the case in the argument, and `0x1C3` the child filter's first removal
    request on the function's PDO (argument the minor code), through the debug port at 5A0h - a trial
    build, never committed, as 43.18's was (section 12, Q8). Stor98 bound to each drive: by the tunnel id on 2000
    to 8, the mark service's arm on 8.1, an opt-in per drive on 10 (record
    31 section 4.7), the class match on 98 SE and ME. Per system and case,
    the runs, at gate G3's injection points (Stor98's `docs/TESTING.md`,
    "CM notification"):
    - idle unplug;
    - requests held: on NT eight reads held at their fetch (the harness's
      `x-faults` `act=hold`), on 98 SE and ME one; the router unplugged
      just after;
    - saturated: on NT the queue at Stor98's limit with every read held
      from one point on and more queued in the port (gate G3's QD 32
      form; the workload is named in the run sheet), on 98 SE and ME one
      held request (owner, 2026-10-22, `runs/run-43.md`, "43.18");
    - during bring-up: the router unplugged while Stor98's start is held
      (the harness's delayed RDY);
    - at the `RequestComplete` stage: the harness's stage rule closes the
      tunnel's gate at the request's completion step (`act=gate`; the
      harness has no router removal of its own, and its `act=surprise`
      takes the tunnel down unseen by this connection manager,
      `runs/run-43.md`, "43.18"), and the router's `device_del` is sent
      over QMP as soon as the harness shows the rule fired, as 43.18's
      held-read runs did; both times are recorded, and the ordering is
      read against the cutoff of the teardown the `device_del` starts;
    - already gone: the harness closes the gate first (`act=gate`), Stor98
      enters `gone` on all-ones, then the router is unplugged;
    - a re-plug unplugged just after `cm.nvlink.up`;
    - and, not counted for a bit but recorded: the chained unit's eject
      (MIXED, withheld, item 5), and a both-ends failure where the harness
      gives one.
    Unplugs: D1 `device_del` of the port-1 router; D2 `device_del` of the
    chained router, the dock's own drive untouched; M `device_del` of the
    dock with the child attached.
    Systems: 98 SE, ME, 2000, XP and XP x64, the systems item 5 admits;
    Vista and later are not read for a bit (the owner's Q11 (a): their
    native hot plug writes Slot Control, and Stor98's `docs/TESTING.md`
    says a q35 surprise makes them remove the controller at once), so
    their bits stay 0; one 7 x86 run of D1 may be recorded as the
    expected withholding.
12. **Host vectors** (`test_cm_nvlink.c`, with the code): the q35 branch
    admitted on each kind with its bit set and withheld with it clear;
    each identity part failing alone (the endpoint's recorded vendor not
    1B36h, no 1B36h bridge on bus 0 holding the bus, the bridge's identity
    changed, any read failing); a 1B36h landing bridge on bus 0 admitted
    by itself; a Power Controller reported, withheld; MIXED (Link Active clear, Presence Detect
    set) withheld; no slot, no Link Active reporting, a root port where a
    downstream port's column is asked, an unknown system; the i440FX
    verdicts unchanged; D2 stating only the chain port's entries, with the
    kid's anchor, and withholding on the host set's; M stating the chained
    entries on the member's reading by M's bit and the member's own by
    D1's, each withheld alone when its bit is clear; a kid's sample
    discarded across a parent's new connection; an unlearned chain port
    stating nothing.
13. **What stays MD-1 after 45.18**, whatever the readings: hardware;
    i440FX on the systems not admitted there; the firmware connection
    manager on q35; a drive behind an enclosure's or a dock's own switch;
    a both-ends failure and an eject on q35; a system, kind or case with
    no passed reading.

## 4. Part 2: the Thunderbolt 3 firmware connection manager's chain

### 4.1 What the firmware does, and what the client does

Under the firmware connection manager the firmware enumerates every
router, at any depth, and builds a chained device's PCIe tunnel through
its parent on `approve`; the client approves by policy and makes the
operating system see the result (record 21 sections 5 and 6). What a
real firmware announces for a depth-2 router is not in any capture of
this project's or `usb4-emu`'s [inf]: the client follows Linux's ICM
client, which takes a depth-2 announcement on Alpine Ridge by link and
depth with `get route`, on Falcon Ridge by the walked topology, and on
Titan Ridge and Ice Lake by the route in the event [linux, record 21
section 1.4.4].

The sequence for a device chained behind an approved dock, security level
`user`, `XhciCmApprove` 1:

1. `device connected`, link L, depth 2 (AR), or the child's route (TR,
   ICL); `approved` 0.
2. The client's table entry; the route by `get route` (link L, depth 2)
   on AR, by the topology walk on FR, from the event on TR and ICL; the
   generation read of record 21 section 5.3 at the next idle poll.
3. **Approval order (new)**: the entry is approved only when its parent
   (the entry one level up the route, or at depth - 1 on the same link
   where only link and depth are known) is approved and its tunnel up.
   A child announced while its parent is still unapproved, or whose
   parent's approve is in flight, waits in the table as "approve owed".
   The owed approvals are reconsidered whenever a parent's approval
   becomes established by any path: its own approve answered without the
   error flag, an announcement of it with `approved` 1 (a parent already
   approved arriving after its child, or a re-announcement), or an
   announcement settling an "approve outcome unknown" as approved
   (record 21 sections 5.3 and 5.5). Each reconsideration checks that
   the parent's entry is still the same occupancy (its serial) and the
   child's too; a child whose parent's occupancy changed is left to its
   own next announcement. A parent refused or failed leaves the child
   unapproved, logged. Linux's ICM client
   approves only what user space authorises through sysfs, and refuses a
   child while its parent is unauthorised before any approve is sent
   (`domain.c` `tb_domain_approve_switch`) [linux]; the firmware announces
   a parent before its child [inf, emulator requirement E5 of section 5].
4. `approve device` for the child; the firmware builds the chained tunnel
   (on the emulator synchronously, R10).
5. The tunnel-up action carries the entry's depth; at depth 2 the seam
   runs section 3's chain action on the member of the depth-1 ancestor's
   link (or the whole set where the link's member is not known), not the
   member-only re-enumeration.
6. `device disconnected` for the child: its entry dropped, the tunnel-down
   action at depth 2: the chain port's removal (sections 3.2 and 3.3.5).
7. `device disconnected` for the parent (the middle unplug): the parent's
   entry and every entry below it, deepest first, each with its own
   tunnel-down action (already so, section 2.6), and the member's removal,
   which takes the whole subtree.

An approve the firmware answers with the error flag for a child is
handled as any failed approve (record 21 section 5.5); the child is not
retried until a new announcement.

On 98 SE and ME in an NT-first cut (section 10.2) the client leaves every
entry deeper than 1 unapproved by policy, so no chained tunnel is built
where the PCI side cannot reach it, and counts it (`icm.chain.declined`).

### 4.2 The data model

None new. The table already keys an entry by link and depth (AR, FR) or
by route (TR, ICL), holds 16 entries, and removes below a departing
parent. The approval-order rule adds three fields per entry,
`ApproveOwed` ("approve owed behind the parent"), `OwedParent` (the
parent's serial when it was owed) and `Unconfirmed`, one counter, and a lookup of the
parent's entry (section 4.6).

### 4.3 The middle unplug, and Stor98

The firmware drops both tunnels at a parent's unplug [emu: the fabric's
gates close leaf first]. The client's actions run deepest first; the PCI
side's removal of the member covers both levels on NT (the dock's
upstream port reported missing takes its subtree) and on 9x (the removal
walk below the member). No statement is made under the ICM on q35 (section
3.5). The eject of a chained unit stops its drivers and leaves the tunnel
up until the user unplugs, as every eject under the firmware connection
manager does (record 21 section 7.4, decision D10).

### 4.4 Chains present at the start

After `driver ready` the firmware announces every attached router with
its `approved` and `boot` bits (`ICM-PROTOCOL.md` R12); a chain tunnelled
before the start is announced approved at depth 1 and depth 2, the client
reconciles both (record 21 section 5.3), and the PCI side has nothing to
do (section 3.4). A chained entry not re-announced within the
reconciliation's 500 ms is treated as gone, its tunnel-down action at
depth 2.

### 4.5 Files (Part 2)

| File | Change |
|---|---|
| `src/cm/tbt_icm.c`, `tbt_icm.h` | the approval order (section 4.6); the action's depth (already carried); no 9x decline (Q1 (b)) |
| `src/cm/cm_nhi.c` | the counter's label; the action's depth already reaches the seam, whose depth-2 branch is section 3's chain action |
| `test\test_tbt_icm.c` | section 9.1 |

Everything else of the chain is section 3's.

### 4.6 The client's approval order, as coded (task 45.17)

Section 4.1 step 3 and section 4.2, made exact for `tbt_icm.c`. The
emulator's side is `usb4-emu`'s requirements R35 to R41
(`ICM-PROTOCOL.md` section 15), section 5's E1 to E10.

- **The parent's entry.** For an entry at depth 2 or deeper: on a row
  that addresses by route (Titan Ridge, Ice Lake) the entry whose route
  is the child's with its last level removed (one level shorter, every
  level before equal); on a row that addresses by link and depth
  (Falcon Ridge, Alpine Ridge) the entry at depth - 1 on the same
  physical port (`tbtIcmPhyPort`, so the port's other link counts, as
  the subtree removal already takes it). A depth-1 entry's parent is the
  host router, always established. A child whose route is not yet known
  on a route row has no parent entry (its own announcement carries the
  route there, so this does not arise from the firmware).
- **Established** means the parent's entry is in use, `Approved`, not
  `ApproveUnknown`, and confirmed in this session (not `Unconfirmed`, a
  third field set with `Stale` when a driver ready marks the table and
  cleared only by an announcement of the entry, never by a driver ready
  that failed, which clears `Stale` so nothing is reconciled away: an
  entry kept across a resume, a rediscovery or a recovery is established
  only once announced again, so an old occupancy's approval never
  releases a child before the parent is seen again). An approve in flight cannot be
  seen from a child: announcements decoded during a command's wait are queued and
  handled after it (`EventsQueued`), so the child is decided with the
  parent's approve answered or timed out.
- **The order check** runs in `tbtIcmPolicy` after the policy (the hook
  or `XhciCmApprove`) said approve, and only there: a child the policy
  withholds is not owed anything. When the parent is not established the
  child is marked `ApproveOwed`, with `OwedParent` the parent entry's
  serial (0 when no parent entry exists), counted
  (`cm.icm.approve.owed`) and logged (`tbt.icm.approve-owed`, the slot
  and the parent's slot or 0xFF; `tbt.icm.approve-owed-serial`; and the
  event's words as received, `tbt.icm.approve-event`, so a later failure
  of the deferred approve, logged with the slot, has its announcement in
  the log, record 21 section 1.4.6); no approve is sent.
- **Reconsideration.** When an entry's approval becomes established -
  its approve answered without the error flag (`tbtIcmApprove`); an
  announcement leaving it `Approved` (`tbtIcmSettle` with `Approved`,
  which a new entry announced approved, a refresh settling an "approve
  outcome unknown" as approved, a refresh of a failed approve announced
  approved and a withheld entry announced approved all reach); or the
  refresh of an entry already approved that clears its `Stale` (the
  resume case, and an approved entry that needed only its route) -
  every in-use entry marked `ApproveOwed` whose parent (by the lookup
  above) is that entry is taken in table order. One meanwhile `Approved`
  or `ApproveUnknown` owes nothing and is unmarked. One `Unconfirmed`,
  one without its route on a row that asks for it (record 21 section
  5.5: no approve for an unrouted entry), or one with a newer
  announcement of its own still queued (decoded during the parent's
  wait: its refresh settles the debt with the latest key and id, so no
  approve goes out with superseded ones) is left marked for that
  announcement, and so is one whose `OwedParent` is neither 0 nor that
  entry's serial (the parent's occupancy changed since; logged
  `tbt.icm.approve-owed-stale`). Otherwise the mark is cleared and
  `tbtIcmPolicy` runs for it again (the policy is asked again, then the
  order; a refusal then consumes the debt). Each
  candidate is re-checked in use and still marked after the previous
  one's approve, because a disconnect decoded during that wait can drop
  entries. A child's own approve that succeeds reconsiders its children
  in turn, so a chain is approved top down, bounded by the depth
  (`TBT_ICM_MAX_DEPTH`).
- **A refused or failed parent.** An approve answered with the error
  flag, or withheld by the policy, leaves its owed children marked and
  logs each (`tbt.icm.approve-owed-parent`, once per such parent
  outcome); they are approved when the parent's approval is established
  later by any path above.
- **The child's own announcement.** An entry marked `ApproveOwed` is a
  recovery entry for `tbtIcmConnected`'s refresh (as `ApproveFailed`
  is), so a re-announcement runs the policy and the order again rather
  than returning at the resume case; one announced approved is settled
  approved and unmarked. A new occupancy starts unmarked.
- **Unapproved, then announced approved, in place.** An entry kept
  unapproved (withheld by the policy, or owed) that the firmware
  announces approved at the same place is settled in place, its serial
  and subtree kept, as the other recoveries are; before this task such
  an announcement replaced the entry and took its owed children with
  it. An approved entry announced unapproved is still a replacement
  (record 21 section 5.3).
- **The depth of the actions.** Every `TBT_ICM_ACTION` already carries
  the entry's `Depth` (and its link and route), and every action already
  reaches `CmReenumAction`; the depth-2 branch to section 3's chain
  action (section 4.1 step 5) belongs to that chain action's code
  (Part 1, task 45.13, `cm_reenum.c`), not to this one. Until it is in
  the tree a depth-2 `TUNNEL_UP` takes today's path, `cmReenumAll`: a
  rescan of the host tunnel ports' members, which does not reach the
  chain port's bus (section 3.2), so no chained device is guaranteed to
  be found. Depth 2 works under the firmware connection manager only
  with section 3's code.
- **No 9x decline.** The owner chose all targets at once (section 12,
  Q1 (b)), so section 4.1's NT-first decline (`icm.chain.declined`) is not
  coded.

## 5. The emulator's depth-2 firmware connection manager (work for `usb4-emu`)

`usb4-emu` is a separate repository; this project never modifies it. What
follows is the request, for its maintainer, with the acceptance checks
this project would read. The numbering continues `ICM-PROTOCOL.md`'s style
with a prefix of its own.

| Id | Requirement | Acceptance check |
|---|---|---|
| E1 | A device router plugged on a device router's downstream USB4 port, on `tb3-alpine-ridge` and `tb3-titan-ridge`, is enumerated by the firmware model (route = the parent's route plus the parent's downstream lane adapter, depth 2, ROUTER_CS_1 to 3 as `icm_plug` writes them at depth 1) and announced while a driver is ready: on AR `device connected` with link = the depth-1 ancestor's host lane adapter and depth 2; on TR the route in the event. Under security `none` it is tunnelled at once, as at depth 1 (R22) | a qtest with the dock (`dialect=tb3,downstream-ports=1`) on port 1 and a USB4 device (`device-model=asm2464` and a plain one) on the dock: two `device connected` events, the second with depth 2 (AR) or route `0x301` (TR on port 1; `0x303` where the dock is on adapter 3) |
| E2 | `get route` (R9) for link L, depth 2 answers the depth-2 router's route when one is attached there, else the error flag | qtest: `get route (L, 2)` answers the route; with nothing chained, the error flag |
| E3 | `approve device` (R10) for a depth-2 router builds the chained tunnel through the fabric's config API (the parent's PCIe Down adapter for that port, HopID 8 at both protocol adapters, as section 2.4), synchronously; it fails with the error flag while the parent's tunnel is not up | qtest: the approve's response flags 0 and the NVMe behind the gated port on q35 answers its BAR0 read; an approve of the child before the parent's: the error flag, no path written |
| E4 | `device disconnected` (R13) for a depth-2 router at its unplug, with the parent's tunnel kept; at a parent's unplug with a child attached, a disconnect for the child and then for the parent (deepest first) [inf: the real firmware's order is not captured; a property may select "parent only", which the client also handles] | qtest: `device_del` of the child gives one disconnect at depth 2 and the dock's NVMe still answers; `device_del` of the dock gives two disconnects, depth 2 first |
| E5 | `driver ready` (R12) announces every attached router in depth order, a parent before its children | qtest: a rebind with the chain attached and approved announces depth 1 then depth 2, both `approved` 1 |
| E6 | Pre-boot (R25, R34): with pre-boot on, a chain attached at power-on is tunnelled at both levels before the guest firmware scans PCI and announced with the `boot` bit | qtest: both NVMe functions present at the guest's first PCI scan; both announcements carry `boot` |
| E7 | The hot plug hook takes lane 0 events of a device router's downstream ports (today ignored) for E1 and E4 | the trace event `usb4_icm_hotplug` reads "plug"/"unplug" for the dock's adapter 3 |
| E8 | `run-cm-test.sh` accepts `CHAIN=1` with `PROFILE=tb3-alpine-ridge` and `tb3-titan-ridge` on q35 | the launcher brings up the dock with the chained device on both profiles |
| E9 | Documented in `ICM-PROTOCOL.md` and `ICM-TITAN-RIDGE.md` with the tags the repository uses, the scope line of `usb4-icm.c` updated | review of the documents |
| E10 | The internal chain port's `power-controller` settable from the command line (a `usb4-device` property passed to the `usb4-tunnel-dsp` the slot creates, or the device's global honoured), so a chain can run on systems without PCIe native hot plug | the dock with `downstream-ports=1` and the setting off: the chain port's Slot Capabilities without a Power Controller, and a chained NVMe answering its BAR0 read on 98 SE with no slot write from the guest |

**Done (task 45.16, 2026-10-29)** in `usb4-emu`'s branch `w45-emu`
(`ae28571`, `4688042`; QEMU fork `51d078a..91610eb`), as that
repository's requirements R35 to R41 (`ICM-PROTOCOL.md` section 15):
E10 as `usb4-device chain-power-controller` (the global works too), E1
to E7 in `usb4-icm.c`, E6's cold insertion in `usb4-fabric.c`, E8 in
`run-cm-test.sh` (`CHAIN=1`, `CHAIN_PC`), E9 the documents; one chained
router per host port, depth 3 not modelled. `runs/run-45.md`, "45.16",
has the acceptance checks as read.

Size, as 44.64 Part B estimated it: 300 to 500 lines in that project, two
to three days; risk medium, since the order and form of a real
firmware's depth-2 messages are not captured.

## 6. Part 3: the minimal chain in the USB4 software connection manager

### 6.1 Scope of the minimal chain

- One level below each host port's device router (depth 2), on any of
  that router's downstream USB4 ports; the depth-2 router's own
  downstream ports left locked, as depth 1's are today.
- PCIe only: no USB3 tunnel at depth 2, so no TMU at depth 2 (record 22
  section 6.6: PCIe has no accuracy requirement), and no DisplayPort;
  except a link of two version 2 routers, whose TMU record 22 section
  6.2.2 item 6 sets whatever the tunnels (section 6.11).
- The parent may be a USB4 router or a Thunderbolt 3 router (the TS3
  Plus, record 22 section 8); the child a USB4 router. A Thunderbolt 3
  child behind a parent is out (section 11): the emulator's chain puts a
  USB4 router behind the Thunderbolt 3 dock, not the reverse [emu], and
  the capture is of that order [linux].
- The approval rule as at depth 1 (`XhciCmApprove`).

### 6.2 The data model: a child connection per link

**The choice: a child connection** (the owner's lean, kept). A table of
child connections in `TBT_U4CM`, each naming its host port and its
parent's downstream lane 0 adapter, beside the depth-1 `TBT_U4_PORT`
array, which does not change. Generalising the port into a per-link
structure would touch every depth-1 sequence - the generation, the
recovery of section 7.7, the carry across sleep of section 9.4, the
anchor, the obligations - for no gain at two levels (44.64 Part B: 1,500
to 2,500 lines changed against 600 to 900 for the child form). Nothing
found in this reading argues otherwise: the depth-1 sequences that the
child needs (enumeration, bonding, Configuration Valid, the paths, the
teardown head) take a router and lane adapters, and the ones that take a
`TBT_U4_PORT` take it for the host-side adapters, which a child replaces
by its parent's. So those few functions gain a "link view" argument
(below) and depth 1 passes the host's view, with its behaviour and its
host vectors unchanged.

```
TBT_U4_KID (one per chained link; TBT_U4_MAX_KIDS 4 per domain)
  Used, Port (the host port), Index (1-based, in TBT_U4CM.Kids)
  ParentLane0          the parent's downstream lane 0 adapter
  ParentPcieDown       the parent's PCIe Down adapter paired with it
  State, Why           the states of record 22 section 7.2
  Generation           incremented at every entry to CONNECTING
  ParentGeneration     the host port's Generation when it connected
  UnplugPending, CheckPending, Unlocked, PcieFailed, Ejected, RemovalOwed
  Dev                  TBT_U4_ROUTER of the child (route, depth 2)
  Bonded, SingleLane, LaneWritten, LaneSaved[4], Lane1Disabled,
  PcSetParent, PcSetDev, LcSetParent
  Pcie                 TBT_U4_TUNNEL
  AnchorValid, AnchorUs, AnchorGeneration
  Transitional, TransitionalSinceUs

TBT_U4_VIEW (the link view the shared sequences take)
  Up       the upstream router (the host router, or the parent)
  UpLane0  its lane 0 adapter of this link
  UpPcie   its PCIe Down adapter of this link
  Down     the downstream router (TBT_U4_PORT.Dev or TBT_U4_KID.Dev)
  Tbt3Up   the upstream router is a Thunderbolt 3 router
```

`TBT_U4_MAX_KIDS` is 4 per domain, not per port: four host ports with
one chained device each, or one dock with up to four. A discovered
depth-2 router this driver adopts (section 6.9) takes a `TBT_U4_KID`;
`TBT_U4_LINK` stays for what is discovered and not adopted (deeper links,
and depth-2 links whose state does not verify).

**The liveness rule.** A child's sequence is live while its own
generation is unchanged, its unplug is not pending, and its host port's
generation is still `ParentGeneration` with the port UP: a parent that
left ends every child's sequence at its next check (record 22 section
7.4's rule, one level up). A pending unplug of the kid itself ends its
sequence only once its removal evidence is read (section 6.7's: lane
0's class, Lock set again on a port this driver unlocked, ERR_CONN on
its route, or another router), as `tbtU4Live` tests the active port's
at depth 1 [code]: with none, the flag is cleared and the sequence goes
on, since an event's age is unbounded (record 22 section 10.3) and a
late unplug event must not cancel a healthy connect.

**Whose sequence a kid's is** (`TBT_U4CM.ActivePort`, record 22 section
7.4). A kid's connect, teardown, eject and PCIe-loss handling run as its
host port's sequence: `ActivePort` is the host port for their whole
length, as `TbtU4Eject`, `tbtU4Clean` and `tbtU4PcieLoss` set it at
depth 1 [code], so a safe point inside one never nests the parent's
teardown under it. A parent's unplug found meanwhile stays pending for
the host port's check, and ends the kid's sequence through the liveness
rule: the host port's `UnplugPending` with removal evidence (the test
`tbtU4Live` makes for the active port [code]) counts as the port not UP.
A kid's own unplug is never nested at a safe point of another sequence
in this cut; it waits for the next idle tick's kid check. Record 22
section 7.4 nests a depth-1 teardown so that its statement meets
section 10.3's 3 s; at depth 2 no statement is made before task 45.18
(section 12, Q4), so the wait loses nothing, and 45.18 decides whether a
kid's teardown nests once its statements are admitted. A kid's teardown
sets `InTeardown` as a depth-1 teardown does, so no teardown nests in
it: at most one teardown nests (record 22 section 7.4).

### 6.3 The child's state machine

The states and their meaning are record 22 section 7.2's, per child, with
three differences:

| State | Difference at depth 2 |
|---|---|
| EMPTY | also the state of every kid slot whose parent has no connection |
| CONNECTING | entered only while the parent is UP and its PCIe tunnel up (the fabric needs the parent's tunnel first, section 2.4) |
| TUNNELLING | ends with section 3's chain action, not the member's re-enumeration |
| UP, FAILED, HELD, UNSUPPORTED, TEARDOWN | as at depth 1, and by the parent's: a parent HELD by a resume or a recovery makes its kid HELD, nothing written, its records, reservations and obligation kept; a parent present whose PCIe tunnel was lost, or ejected, tears the kid down first in the orderly form; a parent with removal evidence drops the kid with no register work (section 6.8). The three cases are below |

**A parent that leaves UP but stays present**, three cases:

- **Its PCIe tunnel lost with the router still there** (record 22
  section 7.1 item 4: FAILED for PCIe, both ends read) **or its eject**:
  both routers are reachable and the kid's entries are live on them, so
  the kids are not dropped on a guess. Each kid's sequence is cancelled
  at its next check (the liveness rule), and its tunnel is torn down by
  section 6.7's orderly teardown with both ends present (record 22
  section 9.3 step 1 through the link view), what does not complete
  quarantined on the parent's and the child's adapters, before the kid's
  state is released; the PCI side's removal is the chain port's, and the
  parent's own removal follows.
- **HELD by a resume or a recovery** (record 22 sections 7.2, 7.6 and
  7.7): nothing is written. A configuration that did not verify and shows
  no removal evidence is left exactly as found, and so are its kids: each
  kid becomes HELD with its parent, its HopIDs and credits stay reserved
  and its resume obligation stays owed, and it is served by its own
  unplug, its parent's, or the parent's later classification under the
  recovery's rules (section 6.9).
- **Removal evidence of the parent** (record 22 section 7.1 items 1 to 3):
  the no-register drop of section 6.8.

### 6.4 The connect sequence at depth 2

On a check of a parent's downstream lane 0 that reads connected (section
6.5) with no kid there, while the parent is UP with its PCIe tunnel up and
a kid slot is free:

1. **Unlock** the parent's downstream lane 0 (ADP_CS_4 Lock clear,
   Guide 3.2.1) on a USB4 parent. A Thunderbolt 3 parent has no Lock to
   clear [linux: `switch.c` `tb_port_unlock` writes it only on a USB4
   router; to be read again at the code].
2. **Read** ROUTER_CS_0 to 4 at the child's route (the parent's route
   with the parent's lane adapter at depth 2); ERR_ENUM or ERR_NUA: logged,
   UNSUPPORTED; USB4 Version major 0 (a Thunderbolt 3 child): UNSUPPORTED
   in the minimal cut (section 6.1); majors 1 and 2 proceed.
3. **Enumerate**: one write of ROUTER_CS_1 to 4 (depth 2, TopologyID = the
   route, Valid, Notification Timeout 255, CMUV 1.0); Router Ready within
   500 ms, the 20 retries (record 22 section 6.4 step 4).
4. **Adapters, DROM, UUID, Get Capabilities, Buffer Allocation** as record
   22 section 6.4 steps 5 to 9, on the child.
5. **Bonding** (record 22 section 6.5) through the link view: on a USB4
   parent, its downstream lane adapters take the host's role; on a
   Thunderbolt 3 parent, Target Link Width dual on its adapters 3 and 4
   and on both of the child's lanes, then LB on the parent's lane 0
   (`0x37` of adapter 3), and the parent's lane 1 unplug event that
   follows acknowledged as bonding's (section 2.4 [linux]); the verified
   single-lane fallback as at depth 1. Then USB4 Port is Configured
   (PORT_CS_19 PC) on the child's upstream port, and on the parent's
   downstream port: PORT_CS_19 PC on a USB4 parent; on a Thunderbolt 3
   parent the link controller's SX_CTRL L1C or L2C for that port, without
   UPSTREAM [linux: `lc.c` `tb_lc_configure_port` sets UPSTREAM only for
   the upstream port; to be read again at the code].
6. **Configuration Valid**: ROUTER_CS_5 by record 22 section 6.6 step 1's
   predicates through the link view: PTO where the parent's port has a
   PCIe Down adapter and the child a PCIe Up; UTO where both USB3 adapters
   exist and the link is a USB4 link (the Guide's "must", 3.2.3, whatever
   is tunnelled; behind a Thunderbolt 3 parent, which has no USB3 Down
   adapter, never); then, with no USB3 tunnel at depth 2, the parent's
   USB3 Down adapter for that port and the child's USB3 Up adapter
   disconnected as `XhciCmUsb3Tunnel` 0 does at depth 1 (Path Enable 0
   with Valid 1, Guide 5.5.3; `tbtU4Usb3Disconnect`); then CV and
   Configuration Ready within 500 ms (record 22 section 6.6 step 3;
   section 2.4 step 1 [linux]). No TMU, but on a link of two version 2
   routers (section 6.11).
7. **The PCIe tunnel** (section 6.6), when `XhciCmApprove` is 1.
8. **The PCI side**: section 3's chain action; the kid is UP.

**Which PCIe Down adapter pairs with the parent's downstream port**: on a
USB4 parent, the Nth PCIe Down adapter in increasing adapter number
belongs to the Nth USB4 port (Spec 5.2.5; record 22 section 6.3), the
parent's downstream ports counted without its upstream port [linux:
`usb4.c` `usb4_port_index` skips the upstream port; to be read again]; on
a Thunderbolt 3 parent, the first PCIe Down adapter neither in use nor
marked unused in its DROM [linux: `tb.c` `tb_find_pcie_down` falls back
to `tb_find_unused_port` on a device router]. The emulator's dock reads
adapter 7 [emu]. No adapter: the child gets no PCIe tunnel, logged
(`u4.kid.no-pcie-down`).

### 6.5 Hot plug from the parent

Record 22 section 7.1 already acknowledges events from a device router's
downstream ports and, where section 9.2 discovered a link, queues a check
of it. The minimal chain:

- **The events**: on a Thunderbolt 3 parent its Plug Events capability is
  already active for lane adapters (record 22 section 8 item 2; bit 3 of
  Plug Events + 1 clear), so the downstream lanes' events arrive [emu: the
  dock sends 3 and 4 after it is configured]. A USB4 parent's downstream
  lane 0 adapter's ADP_CS_5 Disable Hot Plug Events is cleared [spec,
  Spec 8.2.2.1] once the parent is UP with its PCIe tunnel up and
  `XhciCmApprove` 1, at the first idle tick that finds it so, as a step
  of the kid slot's service and never inside the depth-1 connect, whose
  writes stay record 22 section 6.4 to 6.8's byte for byte. It is not
  set again: an eject leaves the parent down until its re-plug, and a
  parent's departure or reset takes the state with it; while it is still
  set, the idle poll's scan below finds the plug. A lane 0 event of the
  parent's downstream port queues a **kid check**; lane 1 events are
  counted, as at depth 1.
- **Which check a notification serves.** Today every Error Notification
  from a device router (ERR_CONN, ERR_LOCK or ERR_PLUG, its route of
  depth 1 or more) is handed to the host port its route's first level
  names, an ERR_CONN or ERR_LOCK as that port's unplug, which the host
  port's check then clears from registers (`tbtU4TakeQueues` [code]).
  The notification names the router that could not forward and its
  adapter [spec]. With kids: one whose route is a kid's parent's and
  whose adapter is that kid's `ParentLane0` or the lane above it is the
  kid's - its check queued, an ERR_CONN or ERR_LOCK its `UnplugPending`
  - and queues only a plain check of the host port, never the host
  port's unplug; one from a kid's own route or deeper serves the
  discovered links there as today and the kid's check, never an unplug
  of the kid or of the host port. A Hot Plug Event the same way: a lane
  0 event of a kid's parent's lane is the kid's check (its
  `UnplugPending` when the event reads unplug and the kid is not EMPTY),
  a lane 1 event counted. So a child's departure never reads as its
  parent's, and the parent's own removal is still found by the host
  port's check, which reads registers.
- **The idle poll** reads each kid's parent downstream lane 0 (LANE_ADP_CS_1
  and ADP_CS_4, two reads) and, for an UP kid, one PCIe adapter's
  ADP_PCIE_CS_0 (a third) and the presence sample of each Stor98 entry
  behind its chain port, as record 22 section 7.1 item 2 does per host
  port. The PCIe adapter read is the parent's PCIe Down behind a USB4
  parent, judged by record 22 section 7.1 item 4 (Path Enable clear, or
  the LTSSM in Detect, or transitional for 2 s). Behind a Thunderbolt 3
  parent the parent's LTSSM field is never interpreted (the emulator's
  adapter 7 reads `0xc6210400` with a working tunnel, `usb4-router-tb3.c`
  [emu], whose LTSSM bits would decode as a transitional state): the
  read is the child's PCIe Up adapter, a USB4 adapter whose LTSSM is the
  Spec's, judged by the same item 4 rule, plus the parent's PCIe Down Path
  Enable once every fourth tick (clear: a loss). The same split holds at
  every verification of a kid's tunnel: adoption at the start and the
  resume's verification (section 6.9) read the LTSSM of the USB4 end only
  behind a Thunderbolt 3 parent, and Path Enable at both ends. An empty
  slot under an UP parent with a downstream
  port reads that port's lane 0 class once per idle tick, so a plug the
  event did not report is found (Guide 3.1.1 allows scanning).
- **The check decides from registers**, as at depth 1: lane 0's class,
  Lock set again on a port this driver unlocked (USB4 parent), and for an
  UP kid the child's TopologyID Valid and UUID unchanged.

### 6.6 The PCIe tunnel at depth 2

Through the link view, record 22 section 6.8's four entries:

| Entry | Ingress | Output |
|---|---|---|
| downstream, segment 1 | parent's PCIe Down, HopID 8 | parent's downstream lane 0, HopID `hd` |
| downstream, segment 2 | child's lane 0, HopID `hd` | child's PCIe Up, HopID 8 |
| upstream, segment 1 | child's PCIe Up, HopID 8 | child's lane 0, HopID `hu` |
| upstream, segment 2 | parent's downstream lane 0, HopID `hu` | parent's PCIe Down, HopID 8 |

- `hd` and `hu`: the lowest free HopIDs from 8 on the parent's downstream
  lane adapter and the child's lane 0 (Linux used 8 on all four
  adapters, section 2.4 [linux]), skipping every HopID the parent's
  discovered or quarantined entries hold.
- Credits by record 22 section 6.8's accounting on the parent's lane
  adapter; a Thunderbolt 3 parent's preferences are the defaults (32 on a
  bonded lane, 16 single, 7 at its protocol adapter, record 22 section 8
  item 5). Linux's: 64 at the child's lane, 32 at the parent's (section
  2.4).
- The Detect gate (record 22 section 6.8 step 1, obligation 1 of section
  10.1): both PCIe adapters in Detect within 500 ms; where one end is a
  Thunderbolt 3 router, the USB4 end only, as at depth 1 (record 22
  section 8 item 5).
- The downstream path, then the upstream; Path Enable on the child's PCIe
  Up first, then the parent's PCIe Down (Guide 5.3.1 steps 5 and 6;
  section 2.4 [linux]).
- L0 on the parent's PCIe Down within 1 s on a USB4 parent. On a
  Thunderbolt 3 parent the core waits for no L0 [inf: whether a
  Thunderbolt 3 router's PCIe adapter reports the LTSSM is not read; the
  model's adapter 7 reads `0xc6210400` with the chained tunnel up, the
  same as adapter 6, which the emulator itself marks inferred]: the kid
  goes on to the PCI side once the Path Enables read back. There the
  chain action, owed while the chain port is not yet discoverable
  (section 3.1), polls the chain port's Data Link Layer Link Active for
  up to 2 s once the port is found, as record 21 section 6.5's tunnel-up
  poll does for a member, and acts whatever it reads; a port found but
  never reading Link Active is logged (`cm.chain.no-link`) and counted,
  and the kid stays UP with its paths, torn down at its unplug or eject.
  So a parent whose PCI side is slow to appear never fails the kid.
- A failure after any write: the orderly teardown of the entries written
  (section 6.7's head), the kid FAILED.

### 6.7 Unplug of the end device: the kid's teardown

When a kid check finds the child gone (CLd, Lock set again, ERR_CONN on
its route, or another router) with the parent still there, record 22
section 7.3's steps on the parent's side of the link (Guide 3.4.1: tear
down, on the routers that remain, every path through the removed router):

1. **The parent side of the PCIe tunnel**: the parent's PCIe Down Path
   Enable read and cleared; PATH_CS_0 Valid cleared on the parent's PCIe
   Down HopID 8 entry and its downstream lane 0's `hu` entry, Pending
   Packets polled to 0 within 500 ms, then at least 2 us; read back, and
   the LTSSM in Detect on a USB4 parent.
2. **The PCI-side reading** of the chain port (record 21 section 6.5's
   reading, on the chain port).
3. **The statement** for the entries behind the chain port (section 3.5),
   on the same conditions as record 22 section 7.3 step 3, the parent's
   read-back standing for the host's.
4. (No USB3 at depth 2.)
5. **The parent's downstream lane adapters back to their defaults**:
   Target Link Width and Lane Bonding restored, lane 1's Lane Disable
   cleared if set, USB4 Port is Configured cleared (or SX_CTRL L1C or L2C
   on a Thunderbolt 3 parent).
6. **The OS side**: the chain port's removal (sections 3.2 and 3.3.5).

Step 1 is followed by the sweep a watched link's removal makes
(`tbtU4RemoveLink` [code]): every other entry on the parent that enters
or leaves the link's lanes read, and one found Valid or with Pending
Packets invalidated or quarantined under the kid. It is a residual
pass: a link this driver built, or one adoption proved carried nothing
else, holds no such entry, and one found is closed rather than left. An
entry unread keeps the kid's removal pending, retried at the next tick,
as for a link.

What does not complete is quarantined on the parent's adapters, keyed by
the host port and the kid (`TBT_U4_QENTRY.Port` and a kid index), and
retried at idle ticks as section 7.3 says; a parent that leaves drops its
quarantine with it (`tbtU4QDropPort`).

**Below the kid.** The links discovered below a kid (depth 3, watched,
section 6.9) go with it with no register work, as `tbtU4DropLinks`
drops a port's links today [code]: their parent is the departed child.
Every route of the departed child and below it is forgotten by the
transport (`TbtCtlForget`, as `tbtU4DropLinks` does at depth 1 [code]).
The same holds in section 6.8 step 1 for the links below each dropped
kid.

**The eject of a chained unit** (section 3.7) calls the same orderly
teardown with both ends present (record 22 section 9.3 step 1 through the
link view), after the chained unit's removal, as `TbtU4Eject` does for a
port (a new entry point, `TbtU4EjectKid(port, kid)`); the kid stays down
until the child is unplugged and plugged again. Ejecting the first
device's unit with a kid up tears the kid's tunnel down first, then the
parent's.

### 6.8 Unplug of the middle device

When the host port's check finds the parent gone:

1. Every kid under that port is dropped first, leaf first, with **no
   register work**: its paths lived on the parent and the child, both gone
   (Guide 3.4.1 tears down only on the routers that remain); its
   generation is raised, a sequence of it in flight stops at its next
   check (the liveness rule, section 6.2), and its quarantine entries on
   the departed routers are dropped.
2. The host port's teardown, record 22 section 7.3, as today: the host
   side of the parent's tunnel, the reading, the statement for every
   entry behind the member - both depths (section 3.5) - then the member's
   removal and re-enumeration, which takes the whole subtree.

A kid's teardown in progress when the parent's unplug is found (a safe
point inside the kid's teardown) is abandoned at its next request: an
ERR_CONN from the departed parent ends it, and what it had not proven
drained goes with the parent. Record 22 section 7.4's nesting rule holds:
at most one teardown nests.

### 6.9 Chains preserved at the start, and across a sleep

**At the start** (record 22 section 9.2, which already walks discovered
paths through every router to depth 5): a depth-2 router whose link and
PCIe tunnel verify - the child at depth 2 with TopologyID Valid, Router
Ready, PTO, CV and Configuration Ready; the link bonded or lane 1
disconnected or disabled; the four entries of section 6.6 Valid and
chained at HopID 8 on the protocol adapters, with Path Enable at both
ends and a live LTSSM at the USB4 ends (behind a Thunderbolt 3 parent the
child's only, section 6.5) - and whose parent is UP, and **whose link
carries nothing else**, is **adopted**: a kid in state UP,
Preserved, `hd` and `hu` recorded, nothing written, nothing re-enumerated
(the operating system found it at boot). "Nothing else" is proven by
reads, every one answered: on the parent, no entry Valid or with Pending
Packets that enters the link's two lane adapters or leaves them (the set
`tbtU4RemoveLink` sweeps [code]) other than the kid's two, and no
protocol adapter's HopID 8 entry leading into the link other than the
kid's PCIe Down's; on the child, no such entry on its upstream lanes
other than the kid's two, and its USB3 Up and other protocol adapters
carrying nothing (`tbtU4ProtoEmpty`'s test [code]). A USB3 or
DisplayPort path a predecessor built there, or an entry unread, keeps
the link watched: the kid's teardown (section 6.7) closes only the
PCIe tunnel, while a watched link's removal sweeps every path through
it. A depth-2 link that does not verify stays a watched `TBT_U4_LINK`,
as today, served by its unplug. A depth-2 link with no tunnel at all
and nothing else on it (the same reads), under an UP parent, is
connected as a fresh plug (section 6.4), its router adopted as already
enumerated (its TopologyID kept). Depth 3 and deeper stay watched
links.

**From link to kid.** Section 9.2's walk (`tbtU4DiscoverLinks` [code])
still records every enumerated, unlocked depth-2 router as a
`TBT_U4_LINK` first; adoption moves a verified one into a free kid slot,
the link's UUID, capabilities and DROM answer carried and the link
freed, so a router is never both a kid and a link. With no kid slot
free the link stays watched and the fact is logged (`u4.kid.full`); a
fresh plug with no kid slot free leaves the parent's downstream port
locked, logged the same way.

**Across a sleep** (record 22 sections 7.6 and 9.4): a kid's PCIe tunnel
that is up at suspend is an obligation of its own (`TBT_U4_OBLIGATION`
gains the kid index), verified at resume after its parent's; where the
domain lost its configuration and the kid's tunnel is one this driver
built (or adopted and matched by section 9.4's record, which gains the
kids' identities), it is rebuilt after its parent's with section 6.4's
sequence and the Detect gate; otherwise the failure path of section 7.6.
System suspend with a tunnelled device attached stays unsupported in the
release that carries this (record 21 section 6.3); this keeps the kid's
state honest across one, not more.

The arrays grow by the kids: `TBT_U4_OBLIGATIONS` by `TBT_U4_MAX_KIDS`
(one PCIe obligation a kid), the obligation count that
`TbtU4ResumeBoundUs` is given with them, and `TBT_U4_CARRY` gains
`TBT_U4_MAX_KIDS` kid entries (the host port, the parent's downstream
lane 0, the child's vendor, device and UUID, and the PCIe tunnel's `hd`
and `hu`).

**The host interface lost** (record 22 section 7.7): a recovery treats
every kid as a discovered link under the recovery's own classification
of its parent - verified and kept, drained only where that
classification drains (both ends reachable and the drain proven), HELD
and untouched where it holds the parent - never rebuilt by the
recovery; a verified one is adopted again by section 6.9's start rule.
A kid rebuild by the recovery is out (section 11).

### 6.10 Files (Part 3)

| File | Change |
|---|---|
| `src/cm/tbt_u4cm.h` | `TBT_U4_KID`, `TBT_U4_VIEW`, `TBT_U4_MAX_KIDS`; kid index in the action, the link-down record, the obligation, the quarantine entry and the carry |
| `src/cm/tbt_u4cm.c` | the link view through enumeration, bonding, Configuration Valid, paths and the teardown head; the kid check, connect, teardown, eject, adoption and resume obligations; the middle unplug's kid drop |
| `src/cm/cm_nhi.c` | the kid's actions to the seam (the chain action), its UP and DOWN hooks (the chained tunnel's name, section 3.5), `TbtU4EjectKid` from the eject path |
| `src/cm/cm_eject.c`, `cm_ejectos.c` | the chained unit's eject to `TbtU4EjectKid` |
| `src/cm/cm_status.c` | kind 3 entries from the kids |
| `test\test_u4cm.c`, `u4_fakefab.c`, `u4_fakefab.h` | section 9.1 |
| `scripts\usb4-crosscheck\u4cm-crosscheck.c` | the chain case (section 9.2) |

### 6.11 Part 3 as coded (task 45.15)

Sections 6.1 to 6.10 made exact for the code (`tbt_u4cm.h`, the section
"The minimal chain" of `tbt_u4cm.c`, and `cm_nhi.c`'s seam); where this
differs from them, this section is the record.

- **The link view** is the kid's own sequences over the shared
  primitives, not a view argument threaded through the depth-1 ones:
  `tbtU4KidRouter`, `tbtU4KidBond`, `tbtU4KidConfigured`,
  `tbtU4KidConfigValid`, `tbtU4KidPcieTunnel` and `tbtU4KidTunnelDown`
  take the parent's router and downstream lane 0 in the host's place and
  call the primitives a router and an adapter already parameterise (the
  reads and writes, `tbtU4Enumerate`, `tbtU4WaitReady`, `tbtU4ReadDrom`,
  `tbtU4DiscoverAdapters`, `tbtU4RouterCaps`, `tbtU4GetCapabilities`,
  `tbtU4BufferAllocation`, `tbtU4ScanLane`, `tbtU4LaneAvail`,
  `tbtU4AllocHop`, `tbtU4WriteEntry`, `tbtU4Invalidate`, `tbtU4Disable`,
  `tbtU4LaneRd`, `tbtU4LaneRmw`, `tbtU4LcPort`). Two depth-1 functions
  were split so that a kid can share them, their behaviour unchanged:
  the TMU-Off predicate (`tbtU4LinkTmuOff`, which `tbtU4TmuOff` calls with
  the host's view) and the removed link's sweep (`tbtU4SweepLane`, which
  `tbtU4RemoveLink` calls). Every depth-1 sequence is otherwise as it was.
- **`TBT_U4_KID`** carries the fields of section 6.2 less the anchor (no
  statement is made at depth 2 before task 45.18, so no presence sample
  is taken for a kid), plus `ParentUsb3Down`, `SweepOwed` and `Ticks`.
  A free slot reads EMPTY. A kid's `Generation` is drawn from one counter
  per domain (`TBT_U4CM.KidGeneration`), so no two connections of any kid
  share one, whatever slot they take.
- **Whose work.** A kid's connect, teardown, eject, loss and resume run
  with `ActivePort` its host port and `TBT_U4CM.KidCtx` the kid, which
  keys every quarantine record they make (`TBT_U4_QENTRY.Kid`): one on
  the child lives with the kid's connection (its `Generation` the
  kid's), one on the parent with the parent's, as before. A teardown
  nested at a safe point inside a kid's work is never keyed by the kid.
- **Notifications.** Section 6.5's routing is made in two places: the
  queued note (`tbtU4KidNote`) and the request. The transport ends the
  one request outstanding with any ERR_CONN or ERR_LOCK that arrives
  meanwhile, its route not compared (`tbt_ring.c`), so a child's
  departure can end a request to its parent or to another port. A router
  reports either error for the adapter it could not forward through
  (Spec 6.4.2.3), so one that ends a request is that request's own only
  when it comes from a router above the request's route (the host
  router included) and names the next hop towards it (`tbtU4NotifyOf`);
  a request to the host router itself forwards nothing, so none is its
  own. Any other is foreign: never removal evidence for the request
  (`tbtU4Gone`), the request asked once more (`tbtU4RdN`, `tbtU4WrN`) -
  but never a router operation's launch (`ROUTER_CS_9` to `26`), which a
  second write could run twice or write over while it runs: that request
  fails as any other cut short - and
  the notification handled as its queued form would be - a kid's lane or
  route the kid's check (and unplug hint for its lane) with a plain check
  of the host port, a host lane that port's check and hint. This holds at
  depth 1 too, where such a notification was read as the requested
  router's removal (or, at a host-local request, as the active port's).
- **The idle tick** (`tbtU4KidService`, after each host port's own
  check): each kid of the port checked from the registers (its evidence:
  the parent lane's class, a USB4 parent's Lock on a lane this driver
  unlocked or found unlocked, the child's TopologyID Valid and UUID, an
  ERR_CONN on its route); an UP kid's PCIe tunnel judged by section 6.5;
  then, with the parent UP with its PCIe tunnel and `XhciCmApprove` 1, the
  parent's downstream Disable Hot Plug cleared once per connection of the
  parent (read first, written only when set), and each downstream port
  without a kid read: a watched depth-2 link there read for adoption once
  (`TBT_U4_LINK.AdoptTried`), a connected lane connected. Under a parent
  FAILED or HELD a kid is still served by its own unplug (section 6.3):
  its removal read and, on evidence, its teardown; nothing else until the
  parent is UP. A parent HELD and not yet classified (a resume's, which
  its own next check classifies before the kids are served; a
  recovery's; one behind the retirement barrier) is not written for its
  kids at all: their checks and any cleanup owed wait, the quarantine
  records the kids made included (`tbtU4KidQWait`: neither retried nor
  dropped while the parent is unclassified or holds another connection).
  A record on the child itself is retried only once the child reads as
  the same router at that tick (its UUID) and no unplug hint of the kid
  is unresolved: behind a Thunderbolt 3 parent no Lock tells a
  replacement, and the retry runs before the kid's own check. The same
  holds for a record on a USB4 router a watched link names (a kid made a
  link again by a recovery among them): retried only while that router
  reads its link's UUID.
- **Adoption** runs at that tick, the first after the parent is UP - at
  the start and after a recovery's commit alike - rather than inside
  section 9.2's walk, so the walk is unchanged. "Nothing else on the
  link" is read by `tbtU4LinkOnly` on both routers and the child's other
  protocol adapters empty. On a Thunderbolt 3 parent the adopted PCIe
  Down is the one whose HopID 8 entry leads into the link. A depth-2
  link with nothing on it is freed and connected as a fresh plug, so
  three host vectors whose depth-2 router stood on a bare link for a
  link-handling check (`test_edge_identity`'s reset under a live link,
  and `test_asm2464`'s ASM2464 at depth 2 and its depth-3 pair) were
  given a half-built path on the parent's side, which keeps
  the link watched as their subject needs; `test_links`' depth-2 re-plug
  is now connected, and kept uninitialised for `XhciCmApprove` 0.
- **The end unplug** (`tbtU4KidTeardown`): step 1 on the parent's side,
  the upcall with the kid, TUNNEL_DOWN with the kid at the first pass;
  the residual sweep (`tbtU4SweepLane`), and once the link is empty the
  parent's lanes back (`tbtU4KidLanesBack`: each change owned until a
  read-back proves it undone, as `tbtU4HostRestore` keeps the host's; the
  child's put back too while it is there), only once the link reads idle
  (the parent's two lanes with the quarantine, `tbtU4KidLanesIdle`, and
  its PCIe Down's Path Enable and HopID 8, `tbtU4AdapterFree`): a held
  entry is not a closed one. An entry unread, a held one, or a change
  not undone leaves the kid in TEARDOWN (`SweepOwed`), retried at the next
  tick; the slot is freed once nothing is owed (the links below dropped
  and every route of the child and below forgotten). A connect waits for
  the parent's two lanes to carry nothing live, pending or held
  (`tbtU4KidLanesIdle`, as `tbtU4LanesIdle` at depth 1), and a kid whose
  parent left without dropping it is dropped at the next tick.
- **The parent's PCIe loss, its eject and the kid's eject**
  (`tbtU4KidsDown`, `tbtU4KidDown`, `TbtU4EjectKid`): the kid's tunnel
  down in the orderly form with both ends, TUNNEL_DOWN with the kid, the
  kid FAILED (`Ejected` for an eject) and kept until the child's or the
  parent's unplug. The child's side is written only on the child read as
  the same router at that moment (its UUID): this runs before the kid's
  own check, and behind a Thunderbolt 3 parent no Lock tells a
  replacement; unconfirmed, the parent's side alone; the upcall only for a loss, as at depth 1 (an eject's
  removal is the operating system's own).
- **The kid's PCIe loss** behind a Thunderbolt 3 parent reads the
  parent's Path Enable every fourth idle tick the kid is UP
  (`TBT_U4_KID.Ticks`).
- **Across a sleep** (`tbtU4KidResume`, `tbtU4KidVerify`): each kid
  obligation after every port's. A parent HELD (or the resume unable to
  judge) leaves the kid HELD, its obligation kept; a parent verified as
  the same has its kid verified (reads only), a kid with removal evidence
  torn down (the next tick connects what is there), anything else HELD; a
  parent the resume rebuilt has a kid this driver built (or adopted and
  matched by the carry) rebuilt after it by section 6.4's sequence.
- **The carry** (`TBT_U4_CARRY.Kid`) records the kid's host lane, parent
  lane, vendor, device, UUID and HopIDs; an adopted kid matched to it is
  `Own`.
- **The host interface lost:** at a recovery every kid becomes a
  discovered link again before its parent is classified
  (`tbtU4KidsToLinks`), its obligation dropped; one that still verifies
  is adopted again at the first tick after the commit. One ejected or
  FAILED for PCIe stays down (`TBT_U4_LINK.KidDown`: never adopted or
  connected) until its link is removed and the child plugged again. A
  kid whose teardown is still owed stays a kid, finished or dropped with
  its parent after the commit: a departure the recovery reads drops the
  port's kids at once (`tbtU4RecoverEmpty`), and no kid is ever written
  once its port holds another connection (`ParentGeneration` against the
  port's `Generation`; dropped unwritten instead); and with too few link records free for the
  rest the attempt is refused (`RecReserveFull`), the kids kept.
- **The seam** (`cm_nhi.c`). A kid's action reaches `CmReenumAction` at
  depth 2, `Slot` the port with the kid in bits 15:8 (the chain action's
  owed key, one per kid, section 3.9), `Serial` the kid's generation;
  `CmNhiActionLive` reads the kid (its slot, generation, both routers
  present, its PCIe tunnel for an up). `CmNhiActionMember` names no
  member for it, so no member's stor98 exposure takes the kid's
  generation for its own tunnel's, and the chain action walks every
  member. A kid's action is not its port's:
  no learning (`XhciTbtLearnUp`, `LearnOpen`, `cmU4LearnTry`, the arm),
  no `U4SeamGen` (the port's eject guard), no stor98 UP; and the link-down
  upcall with a kid is logged (`u4.linkdown.kid`) and withheld, the
  port's learning and isolation word untouched, so a chained drive is held
  by stor98's own gone deadline (MD-1) until task 45.18.
- **Counters** (`TBT_U4_COUNTERS`, logged as `u4.kid.connects`,
  `u4.kid.up`, `u4.kid.adopted`, `u4.kid.torn-down`, `u4.kid.dropped`,
  `u4.kid.lost`, `u4.kid.full`) and log labels `u4.kid.connect`,
  `u4.kid.router`, `u4.kid.state`, `u4.kid.pcie.up`, `u4.kid.pcie.failed`,
  `u4.kid.no-pcie-down`, `u4.kid.version`, `u4.kid.adopted`,
  `u4.kid.fresh`, `u4.kid.watched`, `u4.kid.teardown`, `u4.kid.down`,
  `u4.kid.lost`, `u4.kid.sweep-owed`, `u4.kid.dropped`, `u4.kid.full`.
- **Version 2 routers** (record 22 section 6.2, task 45.20, through the
  link view; at the merge of both). A kid's Gen 4 link is classified as a
  port's is: symmetric, or a paired asymmetric width used as found,
  bonded with no Target Link Width, Lane Bonding or Lane Disable written
  (`TBT_U4_KID.Gen4`, `Asym`; adoption and the resume's verification
  accept the paired asymmetric width); any other Gen 4 reading leaves the
  kid FAILED (`TBT_U4_WHY_LINK`, `u4.kid.link.gen4-unusable`), nothing
  written. A chained PCIe tunnel between two version 2 routers under
  `XhciCmUsb4V2` 1 has Extended Encapsulation set and read back on both
  adapters before either Path Enable; any other has it read clear first,
  a version 2 end left set cleared, else no Path Enable
  (`u4.kid.ee.left`); the kid's teardowns clear it after the Path
  Enables, as a port's. A preserved child (not a Thunderbolt 3 one)
  under `XhciCmUsb4V2` 1 is configured only once CV reads clear: CV set
  with PTO is kept as found, CV set without PTO, or a read that fails,
  leaves the kid HELD (`TBT_U4_WHY_CV_FOUND`, `u4.kid.cv.found`,
  `u4.kid.cv.unread`), its ROUTER_CS_5 unwritten. A v2-v2 child link's
  lane 0 still training is waited for, as a port's, with no write; one
  that never reads connected leaves the kid FAILED
  (`TBT_U4_WHY_LANE_STATE`, `u4.kid.link.not-active`). On the link of a
  kid this driver enumerated, both ends version 2 routers with a TMU,
  neither reporting Time Synchronization Protocol Not Supported, under
  `XhciCmUsb4V2` 1: HiFi Enhanced Uni-Directional before CV, as at depth
  1 (Disable Time Sync set at connection, the link's TMU read Off or
  left as found, `u4.kid.tmu.inherited`; the child's time posted from the
  host's, the mode on the child's upstream and the parent's downstream
  lane 0, Disable Time Sync cleared on the child's then the parent's,
  `u4.kid.tmu.enhanced`, `TBT_U4_KID.TmuConfigured`); a failure undone,
  counted (`TmuFailures`) and logged (`u4.kid.tmu.failed`), the PCIe
  tunnel proceeding; at the kid's teardown the parent's side back to
  AdapterTimeSyncInterval 0, Enable Enhanced Uni-Directional Mode 0 and
  Disable Time Sync set, each attempted whatever the others did. The
  parent's side is the kid's debt: a write that fails
  (`u4.kid.tmu.off-failed`) keeps `TmuConfigured` and the slot TEARDOWN
  (`SweepOwed`), so nothing, of either version, connects on the link
  until the reset is retried and proven at the sweep. After a recovery a
  kid adopted again holds its link as found, its TMU included, as it
  does its lane fields. A predecessor's Enable Gen 4 Link Recovery on a depth-2 link is
  cleared at the link's discovery (`tbtU4LinkElr`), as at depth 1.
  Vectors: `test_u4v2`'s `test_kid_v2`, `test_kid_adopt_asym`,
  `test_kid_training`, `test_kid_tmu_debt`, `test_kid_cv_found`.
- **Not in this cut**: the eject paths to `TbtU4EjectKid` for a chained
  unit (deferred to the next release, section 3.10's "Deferred": the
  chained unit's own eject stops it and leaves its tunnel up, and the
  first device's unit's eject tears the kid's tunnel down first); the
  Windows 98 SE and ME worker's chain action (task 45.14).
- **The status record** (`cm_status.c`, section 3.7): each kid in use is
  a kind 3 entry (`CM_ST_KIND_USB4_LINK`) before the links': its place
  the port with depth 2 in bits 15:8, its route, its approval and state
  (with `Why`), its ids, generation (a USB4 one with its version, or
  Thunderbolt 3), lanes (bonded or single), its PCIe tunnel (up, failed,
  ejected; no USB3 or DisplayPort at depth 2), short id and names; the
  tunnel port its tunnel reaches is not named (`CM_ST_UNKNOWN`) until the
  chain port's correspondence is learnt. Vector: `test_cm_status`'s
  `test_kid`.

## 7. The roles of each target

| Target | Part 1 (PCI side) | Part 2 (ICM) | Part 3 (USB4 CM) |
|---|---|---|---|
| 98 SE, ME | padding and the chain port's numbering (section 3.3); `DN_REMOVABLE` | as NT, the actions through the worker | as NT, the actions through the worker |
| 2000, XP, XP x64 | invalidation through the chain port's filter; a hot-added multi-port dock needs record 33 (section 3.6) | yes | yes |
| Vista, 7, 8, 8.1, 10 | invalidation, beside native hot plug where Windows has it | yes | yes |

## 8. Registry values, counters and the log

No new registry value. Padding (section 3.3.2) rides on
`XhciCmBridgeNumbering` (1 by default on 98 SE and ME); at 0 nothing is
numbered or padded, as today. Chained tunnels follow `XhciCmApprove`,
`XhciCmReenum` and `XhciCmSafeEject` as depth 1 does. New counters and
log labels, each in the existing families: `cm.chain.ports` (candidates
found), `cm.chain.learned`, `cm.chain.invalidate`, `cm.chain.nofilter`,
`cm.chain.9x.pad`, `cm.chain.9x.no-room`, `cm.chain.9x.no-devnode`,
`cm.icm.approve.owed` (an approve held behind a parent, the ICM
client's counter block; with the `tbt.icm.approve-owed*` log labels),
`u4.kid.state`, `u4.kid.no-pcie-down`, `u4.kid.dropped` (the middle
unplug). `XhciCmBridgeDiag` keeps its layout and describes the latest
attempt, member or chain port, with a flag bit saying which.

## 9. Verification

### 9.1 Host vectors

- `test_tbt_topo.c`: chain-port candidates below a member (the emulator's
  dock: one fixed port with a drive, one hot-plug port; a five-port
  switch with one hot-plug port; none); an INCOMPLETE walk; the depth and
  count bounds; an owed chain action retried after a delayed enumeration,
  cancelled by a generation change, expired after 10 s; learning with one
  pending tunnel, a conflict, a parent's
  removal emptying the set.
- `test_tbt_brnum.c`: padding on the dock's layout inside the P14s's
  range and window and inside a small one (no room: buses only, then
  nothing); a later numbering on the padded port as member with the
  tunnel port as interrupt anchor (the swizzle through three bridges);
  the journal and undo with padding.
- `test_cm_nvlink.c`: an entry's landing bridge; a chained tunnel's name;
  a chained tunnel down stating only the chain port's entries; the middle
  unplug stating both; an exposure of the parent not advancing the chained
  entry; the q35 admission and the chained statements, section 3.11
  item 12.
- `test_cm_marks.c`, `test_cm_ports.c`: `CmRegAttMayMark` on a chain port
  under an active member and under an inactive one; the attachment lookup
  by location.
- `test_tbt_icm.c`: the approval order on AR and FR (link and depth) and TR
  (route): child announced before the parent's approve, during it, after
  it; the parent refused; the parent disconnected with the child's approve
  owed; the parent already approved arriving after its child; the
  parent's approve timing out and a later announcement settling it
  approved; a parent replaced (a new occupancy) while its child waits;
  the depth in the tunnel-up action; a chain re-announced after `driver
  ready` (both approved) and one not re-announced (the depth-2 tunnel
  down); a withheld parent announced approved (settled in place, the
  child kept and approved); an owed child announced approved (no approve
  sent for it later); after a resume, the child announced before its
  kept parent, and with the parent gone; an owed child re-announced
  during its parent's approve (approved once, with the newer id); a
  failed rediscovery leaving the kept parent unconfirmed. (No 9x
  decline: Q1 (b).)
- `test_u4cm.c` with `u4_fakefab` gaining a parent's downstream port: the
  connect at depth 2 behind a USB4 parent and behind a Thunderbolt 3
  parent, in sections 6.4 and 6.6's order (bonding before Configuration
  Valid, as at depth 1, where the capture configured first); the
  Thunderbolt 3 child
  refused; no PCIe Down adapter; Detect not reached; a failure in each
  step undone; the end device's unplug (the parent-side teardown, the
  statement upcall with the kid); the middle unplug with a kid UP, with a
  kid CONNECTING, with a kid's teardown at a safe point; the parent's
  PCIe tunnel lost with both routers present (the kid torn down with both
  ends, then the parent's removal); a quick unplug and
  re-plug of the child; the eject of a kid and of its parent; adoption of
  a preserved chain, a half-built one (watched), a depth-2 link with no
  tunnel (connected); the resume obligation of a kid, verified and rebuilt;
  the recovery's treatment of a kid; a parent HELD at resume with its kid
  (nothing written, the kid HELD, its obligation kept); a Thunderbolt 3
  parent whose chain port appears late (the kid UP, the chain action
  owed, then acting); a kid behind a Thunderbolt 3 parent whose PCIe Down
  reads `0xc6210400` staying UP past 2 s of idle ticks, then a genuine
  loss (the child's PCIe Up in Detect for 2 s, or the parent's Path
  Enable clear) found and torn down; a child's ERR_CONN never the host
  port's unplug; a kid's unplug found during another port's sequence
  served at the next tick; a stale unplug event during a healthy kid's
  connect not cancelling it; and the parent's unplug during a kid's
  connect ending the kid's sequence, not nested under it; a depth-3
  link below a kid dropped with it; no kid slot free; a preserved chain
  whose link also carries a USB3 or DisplayPort path kept watched, its
  unplug sweeping every path. Every depth-1
  vector unchanged. The one depth-2 vector of today that expects a
  plugged depth-2 router left uninitialised (the preserved chain's
  re-plug, "and the router left uninitialised") changes with the
  minimal chain: behind an UP parent with its PCIe tunnel up the router
  is connected; the vector is kept for a parent with no PCIe tunnel
  (`XhciCmApprove` 0).

### 9.2 The `usb4-emu` cross-check

`scripts\usb4-crosscheck\u4cm-crosscheck.c` gains `test_chain`: the core
linked with the fabric model (`CHAIN` layout: `intel-adl`, the TS3 Plus
on port 2 with `downstream-ports=1`, the ASM2464 behind it), cold and hot
plug, both tunnels up, the end device's unplug and re-plug, the dock's
unplug with the child attached. The expected sequence is sections 6.4
and 6.6's, not the capture's: compared exactly are the routes (0, 3 and
303), the adapters of each path entry (3:7, 3:3, 303:1, 303:3), HopID 8
at both protocol adapters, Path Enable on 303:3 before 3:7, and the
bonding writes on the dock's adapters 3 and 4; compared as properties
are the lane HopIDs (the lowest free from 8 per adapter, so 8 and 9 where
Linux used 8 both ways, as at depth 1), the credits (within section 6.6's
accounting), and the order of bonding against Configuration Valid (this
design bonds first, as record 22 does at depth 1). A reading recorded
per run, not a gate (record 22
section 14.1). Part 2's equivalent waits for E1 to E5.

### 9.3 The legs

On q35 only (section 2.1), each with the emulator's chain, its chain port
without a Power Controller (`-global usb4-tunnel-dsp.power-controller=off`,
section 2.5; that the global reaches a port the slot creates is read at
the first leg [inf], else requirement E10 of section 5): `intel-adl`
with `CHAIN=1`-shaped devices (the dock with `downstream-ports=1` and the
NVMe child, plain and `device-model=asm2464`) for Part 3, and, once
section 5 lands, `tb3-alpine-ridge` and `tb3-titan-ridge` with the same
chain for Part 2. Systems: 98 SE, ME, 2000, XP, 7 x86 and 10 x64; XP x64,
Vista, 8 and 8.1 where time allows (the same code paths as their
neighbours). The clauses:

1. The dock plugged at the desktop, then the child: the child's router
   enumerated (the log), its tunnel, the NVMe at Code 0 with Stor98 or
   the system's driver as record 31 ranks it, a file written and read
   back.
2. The child unplugged and plugged again (the end device); the dock's own
   drive untouched throughout.
3. The dock unplugged with the child attached (the middle device): both
   drives gone, no hang, no bugcheck, the log's order (kid dropped, the
   host-side teardown, the statement where qualified).
4. The chain attached at power-on (adoption, section 6.9 and 4.4), then
   the child unplugged and plugged again.
5. Safe removal of the chained unit from XHCITR and from the system's own
   applet on NT; the child's tunnel down after it (Part 3); the dock's
   drive still there.
6. On 98 SE and ME: `info pci` after the dock's numbering with the chain
   port padded and nothing chained (section 3.3.2's first reading), and
   after the child's numbering; the devnode locate's outcome
   (section 3.3.4).
7. On 2000 and XP: the dock hot-plugged (expect section 2.2's Code 12,
   and which downstream port gets the window), and the dock at power-on
   then the child hot-plugged (expected to work); a never-seen dock
   (expect `cm.chain.nofilter`, section 3.2).
8. On 7 and 10: whether native hot plug or the invalidation finds the
   child first (section 3.2).

The statement's qualification legs are task 45.18's, section 3.11
item 11.

What each leg records is the brief's list (system, machine type,
command line, binary hashes, each step's result, Device Manager codes,
the deciding log lines), in the run sheet the code task names.

### 9.4 The gates

Unchanged: the import gates (no new import: `IoInvalidateDeviceRelations`
and the CONFIGMG and PCI.VXD services are already used; any CONFIGMG
service section 3.3.4 adds is a dynalink, not an import), the INF gates
(no INF change), the charset gate.

### 9.5 Hardware (optional, the owner's)

- A USB4 host with an ASM4242 (software connection manager, record 22
  section 18) with the CalDigit TS3 Plus and the ORICO behind it: Part 3 and Part
  1 on a real chain, the TS3 Plus's downstream port's capabilities (section
  3.1), whether its firmware pads that port at power-on (section 3.3.3).
  With "Above 4G Decoding" off, design record 32's step runs first on 98
  SE and ME, as at depth 1.
- The ThinkPad P14s Gen 1 (Alpine Ridge, firmware connection manager)
  with the same chain: Part 2 against a real firmware, the order and form
  of its depth-2 announcements (section 4.1).
- The Framework 13 (12th generation, ICL row) with the same chain: the TR
  and ICL form (the route in the event).

## 10. Size and risk

### 10.1 Per piece

Lines of C (driver and tests apart), working days for one person who knows
the code, and the risk. From 44.64 Part B, corrected where this design
differs.

| Piece | Size | Risk | Note |
|---|---|---|---|
| Part 1: chain ports, walk and learning (`tbt_topo.c`) | 200 to 300 lines, 1 to 2 days | low | not in Part B as its own row |
| Part 1: NT invalidation through the chain port's filter | 200 to 400 lines, 2 to 3 days | medium | Part B's 300 to 600 included the learning, now above |
| Part 1: 9x padding, chain-port numbering, devnode locate, removal | 500 to 850 lines, 4 to 6 days | high | Part B: 400 to 800, 3 to 6 days; the devnode locate (section 3.3.4) is new |
| Part 1: Stor98 per tunnel | 200 to 300 lines, 2 days | high | ordering against Stor98 |
| Part 1: safe removal and marks at depth 2 | 200 to 300 lines, 2 days | medium | as Part B |
| Part 2: ICM client | 100 to 150 lines, 1 to 2 days | low | Part B: under 100, 1 day; the 9x decline and the action's depth added |
| Part 2: the emulator (in `usb4-emu`) | 300 to 500 lines there, 2 to 3 days | medium | as Part B; not this project's work |
| Part 3: the kid and the link view | 600 to 900 lines, 3 to 5 days | high | as Part B; depth 1 kept byte for byte by the view |
| Part 3: enumeration, bonding, Configuration Valid at depth 2 | 300 to 500 lines, 2 to 3 days | medium | as Part B, both parent kinds |
| Part 3: hot plug from the parent | 150 to 250 lines, 1 to 2 days | low | as Part B |
| Part 3: paths at depth 2 | 300 to 500 lines, 2 to 3 days | medium | as Part B |
| Part 3: the end and middle unplugs, the eject | 300 to 500 lines, 3 to 4 days | high | as Part B |
| Part 3: adoption and the sleep obligation | 200 to 300 lines, 1 to 2 days | medium | Part B: 200, 1 to 2 days; the obligation added |
| Host suites and the cross-check | 1,000 to 1,500 lines of tests, 4 to 5 days | low | Part B: 800 to 1,500, 3 to 5 days |
| Legs | 4 to 5 days | - | Part 2's after the emulator's work |

Total, the rows summed: about 3,250 to 5,250 lines of driver code and
1,000 to 1,500 of tests; 32 to 46 working days, six and a half to nine
weeks for one person, Part 2's legs after the emulator's work. The 9x PCI
side (four to six days) is the least certain part. This corrects 44.64
Part B's minimal figure (2,500 to 3,500 lines with the tests, three to
four weeks), which is below the sum of its own rows (about 3,500 to
6,000 lines); Part 2's client and legs add one to two weeks to the
software connection manager's chain, the emulator's share apart.

### 10.2 The NT-first option

Leaving the 9x PCI side out of the first cut (section 3.3, the 9x halves
of sections 3.5 and 3.7) saves about a week (44.64 Part B). On 98 SE and
ME the ICM client then leaves depth-2 devices unapproved (section 4.1) and
the software connection manager leaves the parent's downstream ports
locked, as today, so no chained tunnel is built where nothing can find it;
the release notes say chains are NT-only. The 9x half becomes a task of
its own, with this record's section 3.3 as its design.

## 11. Out

- Depth 3 and deeper: the child's downstream ports stay locked (Part 3);
  under the ICM a depth-3 announcement is approved by the same order rule
  but its PCI side is not designed (the chain-port walk takes one level
  of chain ports below a member).
- USB3 tunnels, the TMU and DisplayPort at depth 2.
- A Thunderbolt 3 router as the child under the software connection
  manager.
- More than `TBT_U4_MAX_KIDS` (4) chained links per domain.
- A kid rebuilt by the host interface's recovery (record 22 section 7.7).
- 9x subtree renumbering (section 3.3.3, option B).
- The 2000 and XP requirement-list rewrite and the registered attachment
  for a hot-added multi-port switch: design record 33.
- Turning on a chain port's slot power (section 3.1).
- Restore across a chain on i440FX (the emulator does not model it).
- Stor98 entries for NVMe functions below an enclosure's own switch, at
  any depth (section 2.6).
- Host-to-host links, the DMA port and inter-domain links, as at depth 1.

## 12. The owner's questions

**Answered (owner, 2026-10-27).** Q1 (b), all targets at once. Q2 (a),
Parts 1 and 3 first, Part 2 after usb4-emu's work. Q3 (a), padding. Q4 a
leaner (a): the chain work builds the connection manager's per-tunnel
ownership, names and generations only; the statement anchors and
emission for chained drives come with their q35 qualification in a later
task of their own, today's admission unchanged. Q5: E10 first, then E1
to E5 and E7, then E6 and E8, before Part 2's code. Q6: record 33's fix
(task 44.66) coded first in the same release as the chains, so chains on
2000 and XP need no interim limitation. The questions as asked follow.

- **Q1, NT-first** (section 10.2): (a) NT-first: the first cut on 2000 to
  10, 9x left as today with a release-notes line, the 9x half a later
  task; (b) all targets at once. Recommended: (a). It saves about a week
  on the least certain piece, and 9x's padding rests on a PCI.VXD
  behaviour (section 3.3.2) that is better read on a leg of its own
  before the code.
- **Q2, which part first**: (a) Parts 1 and 3 first (the emulator's
  software-connection-manager chain exists today, `CHAIN=1` and the
  `linux-ts3plus-orico` reference), Part 2's client vectors alongside, its
  legs after `usb4-emu`'s section 5 work; (b) Part 2 first, waiting on
  `usb4-emu`; (c) Part 1 alone first (the PCI side), read with the
  emulator's pre-OS chains. Recommended: (a).
- **Q3, the 9x numbering** (section 3.3): (a) padding at the first
  numbering; (b) renumbering the subtree; (c) both, B as an opt-in.
  Recommended: (a), with B left uncoded until a bench needs it.
- **Q4, Stor98 per tunnel** (section 3.5): (a) the per-tunnel keying and
  the two anchors in the chain's first code, with statements on q35 still
  withheld (MD-1) until a separate task qualifies the PCI Express
  admission per system (G3's ordering read on q35, end and middle
  unplug, saturated); (b) that qualification inside the chain work;
  (c) MD-1 at depth 2 for good. Recommended: (a): the keying is needed
  whenever statements are admitted, and the qualification is the same
  work for depth 1 on q35, so it belongs to a task of its own.
- **Q5, the emulator's work** (section 5): whether the owner takes E1 to
  E10 into `usb4-emu`, and in what order. Recommended: E10 first if the
  global does not reach the chain port (the legs on 98 SE, ME, 2000 and
  XP need it), then E1 to E5 and E7 (the hot plug path and `driver
  ready`), E6 and E8 next.
- **Q6, 2000 and XP** (section 3.6): (a) the chain code runs on 2000 and
  XP, and the release notes state 44.63's limitation for a hot-plugged
  multi-port dock until record 33's work lands; (b) chains declared
  unsupported on 2000 and XP until then. Recommended: (a): the code costs
  nothing extra there, and a chain behind a dock present at power-on is
  expected to work.


**Task 45.18's questions, section 3.11. Answered (owner, 2026-10-30):
Q7 (b), Q8 (a), Q9 (a), Q10 (a), Q11 (a), with (b) noted for
`usb4-emu`.** The questions as asked follow.

- **Q7, the emulator's identity on q35** (section 3.11 item 6): (a) the
  bridges alone, as section 3.5 first wrote it (1B36h on the landing
  bridge or a bus-0 bridge above it), which a host controller passed
  through to a q35 guest also meets; (b) (a) together with the
  endpoint's own id read at its scan (QEMU's NVMe, 1B36h) and the
  landing bridge's identity unchanged since its discovery; (c) (b) and
  the debug port's magic. Recommended: (b): it is the part that keeps a
  passed-through drive out, costs no new access, and (c) would make a
  harness device a condition of shipping behaviour.
- **Q8, the build the readings are taken on** (section 3.11 item 11):
  (a) a trial `qemu` build per reading, never committed, as task 43.18's
  (the stage codes written to the debug port, the source named by its
  commit and diff in the run sheet); (b) the stage codes committed in
  the `qemu` flavour, which needs a second qemu-only import row
  (`HAL.dll!WRITE_PORT_ULONG`) beside the port-0xE9 mirror's. Recommended:
  (a): the allowlist stays as it is, and the readings are few.
- **Q9, Vista and later on q35** (section 3.11 items 7 and 11): where
  Windows' native hot plug removes the function before the statement,
  (a) the bits stay 0 and MD-1 stands there, as on i440FX today; (b)
  the clause "before the system's first removal callback" dropped for
  those systems. Recommended: (a): that clause is gate G3's, and Stor98
  owns it.
- **Q11, Vista and later and the slot-off path** (section 3.11 item
  5): the emulator marks a slot empty at a guest's power-off write before
  any stage 2, so a statement there could precede the endpoint's reset.
  (a) Vista to 10 left at MD-1 on q35 (no bit can be set); (b) a request
  to `usb4-emu` (its own repository) that a guest slot-off run stage 2
  before it reports the slot empty, or that a slot without a Power
  Controller ignore the power-off code, and then those systems read as
  the others. Recommended: (a) for `3.4.0.0` (their native hot plug is
  expected to fail Q9's clause anyway), (b) noted for `usb4-emu`.
- **Q10, the firmware connection manager on q35** (section 3.11 item 9):
  (a) MD-1 there, as section 3.5 says; (b) a task of its own to admit it
  (a one-member condition per chain port, from the client's link and
  depth). Recommended: (a) for `3.4.0.0`.

## 13. Open, and the risks

- **The 9x padding's premise** (section 3.3.2): PCI.VXD keeping an empty
  bridge's window and range. If it does not, option B or no chain on 9x.
- **The 9x devnode locate** (section 3.3.4): the second key and its
  CONFIGMG services are unread.
- **NT invalidation without a filter** (section 3.2): whether invalidating
  the member reaches a started downstream port's bus is unread; with no
  filter on the chain port the chained device is found at the next plug
  of the dock or the next start.
- **Real firmware's depth-2 messages** (section 4.1): uncaptured; the
  approval order is written to tolerate either order.
- **The Thunderbolt 3 parent's link controller and Lock** (section 6.4
  steps 1 and 5): [linux] facts, unconfirmed by a capture of this
  project's.
- **The chain port's Link Active** on the emulator and on hardware
  (sections 3.5 and 6.6): the statement and, on a Thunderbolt 3 parent,
  the PCI side's link poll rest on it.
- **The middle unplug's ordering against Stor98** (sections 3.5 and 6.8):
  one statement covers two tunnels' entries, anchored by one sample of
  every entry behind the member; on the emulator it rests on the
  chain port's own stage 2 being traced first (section 3.11 item 7).
- **The middle unplug's trace on the emulator** (section 3.11 item 7):
  whether the chain port's stage 2 is traced before the member's stage
  3 removes the dock; if not, M waits for `usb4-emu` (a leaf-first
  guarantee, or an NVMe reset event that names its controller).
- **The q35 statement's signal** (section 3.11 item 5): Presence Detect
  clearing only after stage 2 on a surprise removal is read from the
  emulator's code and documents, not yet from a trace of this project's.
- **Slot power on a chain port** (section 3.1): a real dock's downstream
  port with a Power Controller would leave a chained device unpowered on
  98 SE, ME, 2000 and XP.
- **2000 and XP** (section 3.6): useful only with record 33's work for a
  hot-plugged dock.

## Sources

- USB4 Specification 2.0 and USB4 Connection Manager Guide 2.0
  (`docs/references/README.md`), the sections design record 22 cites and
  Spec 5.2.5, Guide 3.1, 3.1.1, 3.2.1, 3.4.1, 5.3.1 and 5.3.2.
- `usb4-emu` `main` after its merge of the asm4242 model (`03d32f2`, QEMU
  fork `2e063be`; read on branch `asm4242-model` before the 2026-10-29
  squash):
  `docs/USB4-HOSTS.md` section 8, `docs/XHCI98-HANDOFF.md`,
  `docs/ICM-PROTOCOL.md` sections 4.2, 4.4, 4.5 and 8 (R9 to R13),
  `docs/ICM-TITAN-RIDGE.md` sections 3.2 to 3.5, `README.md`,
  `scripts/run-cm-test.sh`, `src/qemu/hw/usb4/usb4-icm.c`,
  `usb4-tunnel-dsp.c`, `usb4-tunnel-slot.c`, `usb4-router-tb3.c`.
- Linux `drivers/thunderbolt` (the mirror record 22 pins), facts only:
  `switch.c` `tb_port_unlock`, `lc.c` `tb_lc_configure_port`, `usb4.c`
  `usb4_port_index`, `tb.c` `tb_find_pcie_down`, `icm.c`; and the
  `linux-ts3plus-orico` capture as `usb4-emu` records it.
- This project: design records 21 (sections 5.3, 6.1, 6.1.1, 6.2, 6.3,
  6.5, 7.2, 7.4), 22 (sections 1.3, 6.3 to 6.8, 7.1 to 7.7, 8, 9.2, 9.4,
  10, 18), 24 (sections 3.1, 4.3), 25 (leg T.11), 28 (sections 4 and 12),
  31 (section 4.3) and 32; `runs/run-44.md` "44.63" and "44.64 Part B: sizing".
