# Design Record 34 - Daisy Chains: a Device Behind a Device

Status: revision 4, 2026-10-27, before code, reviewed and revised: the
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
  branch `asm4242-model` at `bf130a8` (its `main` at `e0346a4` is an
  ancestor), QEMU fork at `fa428b4`: `docs/USB4-HOSTS.md` section 8
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
  code.
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
   approves only what user space authorises through sysfs, in the order
   user space chooses [linux]; the firmware announces a parent before its
   child [inf, emulator requirement E5 of section 5].
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
parent. The approval-order rule adds one bit per entry ("approve owed
behind the parent") and a lookup of the parent's entry.

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
| `src/cm/tbt_icm.c`, `tbt_icm.h` | the approval order; the action's depth; the 9x decline in an NT-first cut |
| `src/cm/cm_nhi.c` | the ICM action's depth to the seam (the chain action at depth 2) |
| `test\test_tbt_icm.c` | section 9.1 |

Everything else of the chain is section 3's.

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

Size, as 44.64 Part B estimated it: 300 to 500 lines in that project, two
to three days; risk medium, since the order and form of a real
firmware's depth-2 messages are not captured.

## 6. Part 3: the minimal chain in the USB4 software connection manager

### 6.1 Scope of the minimal chain

- One level below each host port's device router (depth 2), on any of
  that router's downstream USB4 ports; the depth-2 router's own
  downstream ports left locked, as depth 1's are today.
- PCIe only: no USB3 tunnel at depth 2, so no TMU at depth 2 (record 22
  section 6.6: PCIe has no accuracy requirement), and no DisplayPort.
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
7.4's rule, one level up).

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
   section 2.4 step 1 [linux]). No TMU.
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
  lane 0 adapter's ADP_CS_5 Disable Hot Plug Events is cleared at the
  parent's configuration [spec, Spec 8.2.2.1]. A lane 0 event of the parent's
  downstream port queues a **kid check**; lane 1 events are counted, as
  at depth 1.
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

What does not complete is quarantined on the parent's adapters, keyed by
the host port and the kid (`TBT_U4_QENTRY.Port` and a kid index), and
retried at idle ticks as section 7.3 says; a parent that leaves drops its
quarantine with it (`tbtU4QDropPort`).

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
child's only, section 6.5) - and whose parent is UP, is **adopted**: a
kid in state UP,
Preserved, `hd` and `hu` recorded, nothing written, nothing re-enumerated
(the operating system found it at boot). A depth-2 link that does not
verify stays a watched `TBT_U4_LINK`, as today, served by its unplug. A
depth-2 link with no tunnel at all, under an UP parent, is connected as a
fresh plug (section 6.4), its router adopted as already enumerated (its
TopologyID kept). Depth 3 and deeper stay watched links.

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
`icm.chain.order` (an approve held behind a parent), `icm.chain.declined`,
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
  entry.
- `test_cm_marks.c`, `test_cm_ports.c`: `CmRegAttMayMark` on a chain port
  under an active member and under an inactive one; the attachment lookup
  by location.
- `test_tbt_icm.c`: the approval order on AR (link and depth), FR and TR
  (route): child announced before the parent's approve, during it, after
  it; the parent refused; the parent disconnected with the child's approve
  owed; the parent already approved arriving after its child; the
  parent's approve timing out and a later announcement settling it
  approved; a parent replaced (a new occupancy) while its child waits;
  the depth in the tunnel-up action; the 9x decline in an NT-first
  cut; a chain re-announced after `driver ready` (both approved) and one
  not re-announced (the depth-2 tunnel down).
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
  Enable clear) found and torn down. Every depth-1 vector unchanged.

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
  every entry behind the member.
- **Slot power on a chain port** (section 3.1): a real dock's downstream
  port with a Power Controller would leave a chained device unpowered on
  98 SE, ME, 2000 and XP.
- **2000 and XP** (section 3.6): useful only with record 33's work for a
  hot-plugged dock.

## Sources

- USB4 Specification 2.0 and USB4 Connection Manager Guide 2.0
  (`docs/references/README.md`), the sections design record 22 cites and
  Spec 5.2.5, Guide 3.1, 3.1.1, 3.2.1, 3.4.1, 5.3.1 and 5.3.2.
- `usb4-emu`, branch `asm4242-model` at `bf130a8` (QEMU fork `fa428b4`):
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
