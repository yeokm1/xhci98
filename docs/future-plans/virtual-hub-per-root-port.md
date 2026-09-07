# Real speeds on root ports: a virtual USB 2.0 hub behind every USB 2.0 port

Written 2026-09-07. It is an **idea**, not a design record: nothing in it
has been built, no boot has been taken for it, and it has no phase or task
ids. Where it says what a batch would do, it is naming work, not reporting
it. The evidence it rests on is the tree as it stands, issue 6
(`docs/issues/06-full-speed-root-port-bugcheck.md`), design record 02
(`docs/contributing/design/02-hub-topology-route-string.md`) and the
root-hub contracts in `docs/usb-xhci-info/usbport-miniport-abi.md`
section 4.

Two rules frame everything below, the same two the SuperSpeed storage
proposal was written under:

1. The virtual hubs are gated behind a registry value and **off by
   default**.
2. With the value absent or 0, the driver behaves **exactly as it does
   today**. Not "equivalently": the same root-hub reports, the same
   override, the same counters, the same trace lines, the same bytes on
   the bus.

Section 3.1 says what the value is, how it is read, and what holds rule 2.

## 1. The problem it would solve

The driver reports every connected root port to `usbport.sys` as High Speed
whatever the device's decoded speed, and keeps the true speed for its own
Slot and Endpoint Contexts. Issue 6 says why: usbport applies the EHCI model,
in which a root port can only hold a High-Speed device, and when told a
root-port device is Full or Low Speed it looks up the transaction translator
that model guarantees. The root hub has none, `USBPORT_GetTt` turns the empty
list into a garbage pointer, and both shipping builds bugcheck. The override
in `XhciPortShadowReport` (`src/xhci_port.c`) is the one lever there is, and
it costs three things:

- **The interrupt interval.** usbport turns `bInterval` into the `Period` it
  hands the miniport using the speed it believes, and for a believed
  High-Speed device that is `2^(bInterval-1)` microframes capped at 32. A
  Full or Low Speed device on a root port is bucketed on those rules, the
  driver then floors it at the 1 ms the xHCI specification allows at those
  speeds, and the result is three bands: `bInterval` 1 to 4 gives 1 ms, 5
  gives 2 ms, 6 and above gives 4 ms. A stock mouse at `bInterval` 10 runs
  at 4 ms. The same mouse behind a Full-Speed hub arrives at its true speed
  and gets 8 ms, the value it asked for.
- **The isochronous cadence.** usbport stamps a believed High-Speed
  isochronous endpoint's packets one per microframe whatever the descriptor
  says, and the driver recovers the true cadence from a snooped
  `GET_DESCRIPTOR(Configuration)` (`src/xhci_desc.c`). A Full-Speed audio
  device on a root port works, but on a derived cadence with a counter
  recording that usbport and the descriptor disagreed.
- **Everything usbport shows and budgets.** Device Manager reports every
  root-port device as High Speed, and usbport budgets it against the
  High-Speed bus rather than a frame budget. The bandwidth half has no
  measurement either way.

Issue 6 section 6 lists a true-speed report as an opt-in and says why it
cannot be enabled under any shipping usbport: the fault is in usbport's list
handling, and SweetLow's rebuild has the same branch. The lever has two
positions and both have been taken. This page proposes a third: keep
reporting High Speed on every root port, and give usbport the hub its model
is looking for.

## 2. The idea

Between the root hub usbport owns and every managed USB 2.0 protocol port,
the miniport presents a USB 2.0 hub that does not exist on the bus: High
Speed, one downstream port, one transaction translator, `bDeviceProtocol`
1. The real root port becomes that hub's port 1. In usbport's view the
topology gains one tier on every port; in the xHC's view nothing changes,
because the controller keeps driving the device on its root port with a
Route String of 0 and no TT fields, as it does today.

What that buys follows from the EHCI model usbport applies. A Full or Low
Speed device behind a High-Speed hub is the case that model was written
for: `USBPORT_GetTt` walks up to the virtual hub, finds a TT record there,
and returns it. usbport then does the truthful thing at every layer:
`DeviceSpeed` is the decoded speed, `Period` is bucketed in frames (which
`XhciIntervalFromPeriod` already handles, since that is what a device
behind a real hub gets), isochronous packets are stamped per frame, the
device is budgeted against the hub's TT, and Device Manager says Full Speed.
The measurement issue 6 already took behind a Full-Speed hub is the
prediction for a device behind the virtual one: `bInterval` 10, 1 and 4
arriving as `Period` 8, 1 and 4.

Nothing in the xHC has to know. The xHC handles Full and Low Speed on a
root port natively, Table 6-6 conditions the TT fields on "connected
through a High-speed hub", and the driver's own topology graph already
decides the TT fields from each hub's decoded speed rather than from what
usbport names (design record 02, step 3). The virtual hub is a device that
lives entirely in the miniport, answered in software, and the only thing it
translates is what usbport is told.

## 3. What the driver would have to do

### 3.1 The switch, and what off means

| | |
|---|---|
| Name | `XhciVirtualHub` (proposed; the owner settles the name) |
| Type | `REG_DWORD` |
| Where | The controller's driver (software) key: the key a plain `AddReg` under an INF install section writes, and the key `XhciLogVerbosity` and the SuperSpeed storage proposal's `XhciSuperSpeed` live in |
| Values | `0` off, the default. `1` on, the permanent shape of section 4. Any other value is refused, not clamped: the driver applies 0 and records that it refused, on the rule the INF states for `XhciLogVerbosity`. A later on-demand shape would take the next value rather than a second switch |
| Absent | 0, and not an error |
| Set by | The user, by hand, in Registry Editor. The INF's `AddReg` writes the 0 so the value is visible where the user looks for it; `XHCISNAP` does not set it |

It is read the way the log values and the SuperSpeed storage proposal's
switch are read: through `UsbPortGetMiniportRegistryKeyValue` from
`StartController`, in the routine that already reads `XhciLogVerbosity`
(`xhciLogReadValues` in `src/xhci_dispatch.c`), because every constraint on
a registry read has been paid for there. PASSIVE_LEVEL only, since
`StartController` is the one callback this driver knows is PASSIVE. No new
import, since the `Zw*` names are denied by the import gate and usbport's
service is the only registry channel this project may use. Nothing in the
read may fail a start: the value missing, the read failing and a NULL
service pointer all leave it at 0 and the driver starts as today, with the
`MPSTATUS` kept beside the value so a reader can tell a 0 somebody set from
nothing found. usbport zeroes the miniport extension before every start, so
the value is re-read at every start and cached nowhere else; changing it
takes a disable and enable of the controller or a reboot. What was read,
what was applied and the status go into the snapshot header
(`docs/contributing/passthru-snapshot-instrument.md`), so a dump from a
stranger's machine states which mode the driver was in.

Rule 2 is held at one divergence point. With the switch applied as 0, no
virtual record is ever created, the root hub reports each managed port from
the shadow as it does now, the address map never holds an address without a
Slot ID, and the topology graph never sees a hub it has to fold out; every
path of 3.2 to 3.5 is behind a test of the applied value taken once at
start, and the only new code that runs in the off state is that test and
the read itself. Host vectors run the affected paths in both states, and
the device matrix runs with the switch off in both the value-absent and
the INF-installed setups, as the SuperSpeed storage proposal's plan does,
so that "today's driver" is a measured reading rather than a claim.

The gate is there for two reasons. The permanent shape puts N hub devnodes
in Device Manager and N enumerations into every start, a visible change on
every machine, and a user who would rather have the 4 ms mouse than the
devnodes keeps today's driver by doing nothing. And the reading section 5
rests on, that each hub driver gives the virtual hub a TT record, has not
been taken; until it has on both primary targets, the default cannot move.

### 3.2 The root hub

The root hub keeps its descriptor and its port count. What changes is what
each managed port reports: connected, enabled, powered and High Speed for
as long as the controller is in service, whether or not anything is plugged
in. A physical connect or disconnect stops being a root-port change and
becomes a change on the virtual hub's port (3.4). The Port Status Change
Event path and the start/resume seed keep every latch rule in
`docs/contributing/implementation-invariants.md`, "Root Hub Reporting";
what they announce moves from `UsbPortInvalidateRootHub` to the virtual
hub's status-change pipe.

usbhub resets a root port before it creates the device on it, so the root
hub must answer `RH_SetFeaturePortReset` for a port holding a virtual hub
without touching PORTSC: latch `C_PORT_RESET` in the shadow, announce it,
and let the enumeration proceed. The rule that a root-port reset drops a
pending hub-port claim (`XhciTopoDropPending`) has to learn that this reset
arms the virtual hub's own address-0 open and not a physical device's. A
later re-enumeration of the virtual hub by usbhub (after an error it cannot
clear, or on resume) takes the same synthetic path; a physical reset here
would drop a device that is running.

`RH_ClearFeaturePortEnable` and `RH_ClearFeaturePortPower` on such a port
mean usbport has destroyed the virtual hub's device object. The driver
tears the virtual hub down and, through it, the device behind it; the
physical port keeps its power, since the next thing usbhub does is re-create
the hub.

### 3.3 The virtual hub as a device

usbport creates the virtual hub the way it creates any device: EP0 open at
address 0, `GET_DESCRIPTOR(Device)`, `SET_ADDRESS`, the descriptors again at
the new address, `SET_CONFIGURATION`, then the hub class. The driver
intercepts `SET_ADDRESS` today and issues Address Device; for the virtual
hub it records the address in the usbport-address map against a record
that has no Slot ID and no ring, and every later open and transfer keyed on
that address is served in software. Each request is a table row, and the
table is the whole device:

| Request | Answer |
|---|---|
| `GET_DESCRIPTOR(Device)` | `bcdUSB` 0x0200, class 9, subclass 0, `bDeviceProtocol` 1 (single TT), `bMaxPacketSize0` 64, a vendor and product id of the project's choosing, `bcdDevice` carrying the driver version, no string indices |
| `GET_DESCRIPTOR(Configuration)` | one configuration, one interface of class 9 with one interrupt IN endpoint (`bInterval` 12, `wMaxPacketSize` 1), `bmAttributes` self-powered, `bMaxPower` 0 |
| `GET_DESCRIPTOR(String)` | a stall. No string index is advertised, so a caller asking is off the descriptor; the language table (index 0) can be answered if a shipping hub driver turns out to ask for it unprompted |
| `GET_DESCRIPTOR(Hub)` | `bNbrPorts` 1, `wHubCharacteristics` individual port power and over-current, `TTT` 0, `bPwrOn2PwrGood` from the root hub's own value, `bHubContrCurrent` 0, port 1 removable |
| `SET_CONFIGURATION`, `SET_INTERFACE` (alt 0), `GET_STATUS` (device, interface, endpoint), `CLEAR_FEATURE(ENDPOINT_HALT)` | success, no data |
| `GET_HUB_STATUS` | zero |
| `GET_PORT_STATUS(1)` | the real port's shadow reported truthfully: the connect, enable, suspend, over-current, reset and power bits as today, and the Low-Speed or High-Speed bit from the decoded speed, neither for Full Speed |
| `SET_PORT_FEATURE(1, PORT_RESET)` | the existing reset path: PORTSC.PR, the asynchronous timeout, a reset generation, `C_PORT_RESET` on completion, reported through 3.4 |
| `SET_PORT_FEATURE(1, PORT_POWER)`, `PORT_SUSPEND` | the existing `RH_SetFeature...` bodies, called for the underlying port |
| `CLEAR_PORT_FEATURE(1, C_PORT_*)` | clear the shadow's change bit, as `RH_ClearFeaturePortXChange` does |
| `CLEAR_PORT_FEATURE(1, PORT_ENABLE)`, `PORT_POWER`, `PORT_SUSPEND` | the existing disown and disable bodies (`XhciSlotPortDisowned` and the confirmed half), unchanged |
| `CLEAR_TT_BUFFER`, `RESET_TT`, `GET_TT_STATE`, `STOP_TT` | success. The xHC has no translator on a root port to clear |
| anything else | a stall, counted |

Two rules from the tree apply to how the answers are delivered. A transfer
cannot be completed inside `SubmitTransfer`, because usbport writes to the
transfer record after that callback returns (design record 05, section 7:
the submit bracket is a lifetime rule, and `SubmitEpoch` is what defers a
completion out of it); a synthetic completion therefore threads onto the
same deferred-completion list a hardware completion uses and drains from
the same place, with the reply bytes written through the scatter/gather
list's `MappedSystemVa` before `UsbPortCompleteTransfer`, the one window in
which usbport keeps that mapping. And a refusal that can never stop being
true must fail the transfer rather than return nonzero, so an unknown
request is completed with a stall status, never left queued for retry.

The two EP0 snoops share this channel. The hub-topology graph
(`src/xhci_topo.c`) learns hubs from the hub-class traffic it sees at
placement and from the reply bytes it reads at completion; the descriptor
snoop (`src/xhci_desc.c`) reads configuration descriptors the same way. A
virtual hub's traffic must either go through both folds and be recognised
there, or be diverted before them. Going through is the smaller change and
the graph then sees the virtual hub as a hub, which 3.5 has to correct.

### 3.4 The status-change pipe

usbhub opens the hub's interrupt IN pipe and keeps one transfer pending on
it. The driver holds that transfer on the virtual device's queue and
completes it with a one-byte bitmap (bit 1 set) when the underlying port
latches a change; between changes the transfer stays pending, which is what
a real hub's pipe does. The three latch sites keep their obligation: a
change latched with no transfer pending is held in the shadow until the
next transfer arrives and completed into it at once. The controller has
still acknowledged the PORTSC bit the moment it was read, so nothing here
loosens the "latch until asked" half of the rule.

The pipe's `Period` is whatever usbport buckets `bInterval` 12 into; it is
never programmed anywhere, since there is no endpoint. `AbortTransfer` on
this pipe must find the held transfer wherever it is, queue or completion
list, as design record 05 already requires.

### 3.5 The topology graph

The virtual hub is a hub in usbport's view and must not be one in the
xHC's. Four rules in the graph and the device records change meaning:

- A device behind virtual hub port 1 on root port N is a root-port device:
  Route String 0, Root Hub Port Number N, `HubPort` 0, `RootPort` N, and
  no TT fields. The rule that a behind-hub record must not hold a root-hub
  port holds, because the virtual hub is folded out before the record is
  built.
- `SET_FEATURE(PORT_RESET)` on a virtual hub arms the root-port
  enumeration claim, not a hub-port claim. The two claims stay mutually
  exclusive.
- `XhciTopoTtFor` must return "no TT" for a device whose nearest High-Speed
  ancestor is a virtual hub. usbport's `HubAddr`/`PortNumber` will name the
  virtual hub for every Full and Low Speed root-port device, so the
  comparison that feeds `TtPairsAgreed`/`TtPairsDisagreed` has to know the
  virtual address, or every such device counts as a disagreement.
- A device behind a real hub behind a virtual hub is one tier behind a real
  hub, and its TT fields come from that hub as they do today. A Full-Speed
  hub plugged into a root port gets, in usbport's view, a TT from the
  virtual hub above it; the xHC needs none, which is already what the graph
  derives for a Full-Speed hub on a root port.

Device removal changes route. Today a root-port device leaves through
`RH_ClearFeaturePortEnable` after usbhub sees the disconnect on the root
hub; with the virtual hub it leaves through the hub path, the
`GET_STATUS(port)` reply fold that already tears down a behind-hub device
and its subtree. That path has to reach the same disown bookkeeping the
root-port path does, since the physical port really is being given up.

### 3.6 What stays as it is

Everything below the usbport-facing surface. Port speed decoding, the
Slot Context and EP0 programming from the decoded speed, the interval floor
at Table 6-12's minimum, the reset generations, the disown and disable
split, the failure counters, the log channel, the PORTSC watchdog and the
recovery latch are untouched. The High-Speed override in
`XhciPortShadowReport` stays where it is, since the root port still reports
High Speed; what it now describes is the virtual hub.

## 4. Two shapes, and which to build

**Permanent (the shape this page proposes).** Every managed port carries a
virtual hub from start to stop, plugged or not. One state machine per port,
no decision point, and a High-Speed device is simply a High-Speed device
behind a High-Speed hub, which needs no TT and costs nothing. The price is
one "Generic USB Hub" devnode per managed port in Device Manager, one USB
address per port out of the 127 usbport hands out, and usbhub enumerating N
hubs at every start and resume.

**On demand.** The root port reports a bare connect, and only if the
decoded speed after the reset is Full or Low does the driver report the
connect as a High-Speed hub and materialise one. High-Speed devices stay
where they are and Device Manager stays clean. The cost is a second
enumeration shape on every port, a decision that must be made between the
reset's completion and usbhub's next `RH_GetPortStatus`, and a hub that
appears and disappears with the device, so that every removal, disown and
resume path has two cases. It is the smaller visible change and the larger
state change.

Build the permanent shape first, as value 1 of the switch in 3.1. The
on-demand shape can be a later value of the same switch if the devnodes
turn out to matter.

## 5. What has to be measured before it is trusted

- **That usbhub creates a TT record for the virtual hub on both shipping
  builds.** The whole idea rests on `USBPORT_GetTt` finding a non-empty
  list. Batch 7b-V0 measured that a believed High-Speed hub with
  `bDeviceProtocol` 0 got one (design record 02, open question 6), and a
  hub declaring protocol 1 is the case the hub driver was written for, but
  why either holds is unconfirmed against the binaries. The probe is the
  one the tree already has: `HubAddr != 0xFFFF` on the first Full-Speed
  device's endpoint properties, read before the device is opened, on NUSB's
  build, SP4's, SweetLow's and XP's.
- **What a one-port hub does to each hub driver.** `bNbrPorts` 1 is legal
  and unusual. usbhub20, Windows 2000's `usbhub.sys`, Windows ME's and XP's
  each need one boot with the switch on.
- **Idle suspend.** usbport idle-suspends a quiet controller (issue 5). N
  permanently present hubs with pending interrupt transfers may keep it
  from ever going idle, or may not count; either reading changes what the
  package's `DisableSelectiveSuspend` is for.
- **Resume.** After a resume the virtual hubs already exist and usbhub does
  not rescan the root hub. A device plugged during the suspend has to be
  reported through the hub's pipe from the seed, and whether usbhub re-arms
  that pipe before or after the seed runs decides whether the change is
  held or lost.
- **Boot time.** N extra enumerations at every start, on a Windows 98
  machine that already boots slowly under NUSB.
- **The interval.** The measurement issue 6 took with a `bInterval`
  override tool, repeated on a root port with the switch on: `bInterval`
  10 should arrive as `Period` 8 and program Interval 6.
- **The bandwidth half** stays unmeasured until a root port is loaded with
  enough Full-Speed periodic traffic to reach usbport's TT budget.

## 6. What QEMU can and cannot show

`qemu-xhci` with `usb-kbd,usb_version=1` (Full Speed), `usb-mouse` (Low
Speed) and `usb-audio` (Full Speed) on root ports reproduces every reading in
section 5 except boot time, on all four guests. The device matrix's Full
and Low Speed rows run unchanged with the switch on and are the regression
half. What QEMU cannot show is a real Windows 98 machine's hub driver
against N hubs at boot, and that is the E460's reading: the Low-Speed mouse
and the Full-Speed audio device from `docs/contributing/test-equipment.md`
on root ports, with the interval read from the snapshot instrument.

## 7. What it does not do

- High-Speed devices see no change.
- The 1 ms floor stays, since the xHCI specification allows nothing less at
  Full and Low Speed. What goes away is the bucketing above it.
- usbport still rounds a Full or Low Speed `bInterval` down to a power of
  two in frames, so `bInterval` 10 gives 8 ms, as it does behind a real hub.
- It does not remove the override. Under a usbport that guarded the empty
  list (issue 6 section 6) a truthful root port would be simpler than a
  virtual hub, but no such usbport exists for these targets.
- It does not interact with the SuperSpeed storage proposal except
  by composition: a SuperSpeed device reported as High-Speed sits behind
  the virtual hub as a High-Speed device, which needs no TT.

## 8. Batches, if it is taken up

- `-0`: the request table of 3.3 as host vectors (`test/`), fed the
  setup packets both shipping hub drivers send, from the measurements in
  design record 02 (`wValue` 0 on `GET_DESCRIPTOR(Hub)`, `wLength` 71); the
  graph fold of 3.5 as vectors over the existing topology tests; the
  switch of 3.1, its refusal of unknown values, its snapshot header
  fields, and the off-state vectors that hold rule 2.
- `-A`: the virtual device record, the synthetic completion path, the
  status-change pipe, the root-hub changes of 3.2.
- `-V`: section 6 on all four guests, switch off then on, the device matrix
  in both states.
- `-E`: the E460 reading.

## 9. Decisions the owner would take

1. Whether it is wanted at all, against the cost in section 4: N hub
   devnodes for an interval a stock mouse feels as 4 ms instead of 8 ms.
2. Permanent or on demand, if section 4's recommendation is not taken.
3. The virtual hub's vendor and product id and whether it carries strings.
4. The switch's name, `XhciVirtualHub` being the proposal.
5. Whether the switch defaults to on in a later release once section 5 is
   read on both primary targets, or stays an opt-in like the SuperSpeed
   storage proposal's.

## Sources

- `docs/issues/06-full-speed-root-port-bugcheck.md`: the bugcheck, the
  override, the three bands, the behind-hub measurement, and the opt-in
  that cannot be taken.
- `docs/usb-xhci-info/usbport-miniport-abi.md` section 4 (the root-hub
  callbacks and their contracts), section 5 ("Periodic scheduling: what
  `Period` actually carries") and section 8 ("The transaction-translator
  lookup").
- `docs/contributing/design/02-hub-topology-route-string.md`: what the
  miniport sees of hub-class traffic, the graph, step 3's TT derivation,
  and open question 6.
- `docs/contributing/design/05-locking-model.md` section 7: the submit
  bracket and the deferred-completion list.
- `docs/contributing/implementation-invariants.md`: "Root Hub Reporting",
  "Device Addressing", "Hub Paths".
- `docs/issues/05-idle-suspend-and-disableselectivesuspend.md`: the idle
  suspend the virtual hubs may or may not hold off.
- `src/xhci_port.c` (`XhciPortShadowReport`), `src/xhci_topo.c`,
  `src/xhci_ctx.c` (`XhciIntervalFromPeriod`).
