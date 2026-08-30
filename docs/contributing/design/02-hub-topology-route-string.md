# Hub Topology Tracking and Route String Derivation - Design

Design doc 02. Companion to subphase 7b's hub work in
[`../roadmap.md`](../roadmap.md): tasks 7b-A.1 through 7b-A.3, the batch 7b-V
VM run and the batch 7b-M hardware run (devices behind external USB 2.0 hubs,
split by vehicle because QEMU's `usb-hub` is USB 1.1 and has no transaction
translator to test), with groundwork laid in the Phase 3/6 spikes.

## 1. The problem

xHCI's Address Device command requires software to describe where a device
sits in the hub tree, in the Slot Context
([`../../usb-xhci-info/xhci-data-structures.md`](../../usb-xhci-info/xhci-data-structures.md) section 8):

| Field | Needed for | Root-port device | Behind-hub device |
|---|---|---|---|
| Route String (DW0 `19:0`) | Path from root port, 4 bits per hub tier | 0 | hub-port path |
| Root Hub Port Number (DW1 `23:16`) | Which root port the path starts at | port index | root port of the top hub |
| Parent (TT) Hub Slot ID / Port (DW2) | Split transactions for FS/LS behind a HS hub | - | Slot ID of the TT hub + port |
| MTT (DW0 `25`), and Hub/Number of Ports/TTT on the hub's own slot | TT scheduling | - | required for correct splits |

`usbport.sys` cannot be expected to provide this. It was designed for
EHCI/OHCI/UHCI miniports, and EHCI never needs the hub-port path: on USB 2.0
the bus routes by device address token, so the only topology EHCI consumes is
the TT hub address plus port for FS/LS split transactions. What usbport
provides, confirmed on both shipping binaries by batch 7a-0 (details and
addresses in `docs/usb-xhci-info/usbport-miniport-abi.md` section 5; extracts
in `tools/{win2ksp4,nusb}-extracted/usbport-abort-disasm.txt` part 2):

- FS/LS device behind a HS hub: `USBPORT_ENDPOINT_PROPERTIES` carries `HubAddr`
  (TT hub USB address, or `0xFFFF` when the TT lookup found nothing) and
  `PortNumber` (the port on that TT hub, valid when `HubAddr != 0xFFFF`). The
  miniport resolves the address to a Slot ID through its usbport-address ->
  Slot ID map. That names which TT; it is not the whole of DW2 (see below).
- HS device behind a HS hub, or any all-High-Speed path: usbport passes nothing
  about topology, since EHCI never needed it. `HubAddr` is `0xFFFF` whenever the
  device has no TT extension. An FS/LS device below non-HS tiers below an HS
  hub does get the TT hub/port pair; what no path of any kind gets is route
  information.
- Route String: no usbport field carries it for any case. This was checked
  against the full ReactOS properties layout; no field can hold route tiers.

What the disassembly established about those fields:

- `HubAddr` names the nearest High-Speed ancestor hub, found by walking
  `DeviceHandle->HubDeviceHandle` upward in `USBPORT_GetTt`, not the immediate
  parent. `0xFFFF` means "no transaction translator", which a root-port device
  and an all-HS path both read, so it cannot be used as a "this device is on a
  root port" test.
- `PortNumber` carries the TT port, at any tier depth, when
  `HubAddr != 0xFFFF`. `USBPORT_GetTt` receives a pointer to a
  `USBPORT_CreateDevice` stack local, overwrites it on every non-High-Speed step
  of the upward walk, and `CreateDevice` then stores that mutated local as
  `DeviceHandle->PortNumber`, which `OpenPipe` copies into
  `properties.PortNumber`. The value left standing is the port on the HS TT hub
  the non-HS subtree hangs off, however deep the subtree. The local survives
  unmodified only when the device is High-Speed (no TT at all) or when its
  parent hub already is the TT hub, where the two numbers are the same.
- The qualifier is the condition to branch on. A `GetTt` that runs and fails
  leaves `HubAddr = 0xFFFF` with `PortNumber` holding the port of the last
  non-High-Speed ancestor the walk visited, or the seeded immediate-parent port
  if it visited none. Two routes reach that (walking off the top of the chain,
  and a multi-TT list miss) and they are not distinguishable from the value;
  with a single non-HS hop both give the immediate-parent port.

  Both branches
  are defensive: the TT-list construction call graph shows a successfully
  initialized hub has exactly one TT record per usable port, and a partial list
  exists only behind an initialization failure that aborts hub start, so
  neither branch is reachable through a valid tree. Read `HubAddr != 0xFFFF`
  as the test and there is no ambiguity; read `PortNumber` unconditionally and
  there is.
- That is not the same as DW2 being covered. Slot Context DW2 is four fields
  (`docs/usb-xhci-info/xhci-data-structures.md` section 8): `Parent (TT) Hub
  Slot ID` `7:0`, `Parent (TT) Port Number` `15:8`, `TTT` `17:16`, `Interrupter
  Target` `31:22`. `PortNumber` gives the second directly. `HubAddr` gives the
  first only after the miniport resolves a USB address to a Slot ID through its
  own map, and only if that hub already holds a slot. `TTT` comes from the hub
  descriptor's `wHubCharacteristics` bits 6:5 and usbport never supplies it.
  Nor does it supply the hub's own slot context (`Hub` = 1 (DW0 `26`), `Number
  of Ports` (DW1 `31:24`), `MTT` (DW0 `25`)), which the table above lists as
  required for correct splits. So FS/LS-behind-HS does not work from usbport
  data alone; snooping is required for it.
- usbport has the full parent chain (`DeviceHandle+0x10`, each handle carrying
  its own port at `+0x06`) and exposes none of it: no miniport callback takes a
  `PUSBPORT_DEVICE_HANDLE`, and the 0x40 bytes of `USBPORT_ENDPOINT_PROPERTIES`
  are fully accounted for. The four values the miniport ever sees are
  `DeviceAddress`, `DeviceSpeed`, `HubAddr`, `PortNumber`.
- Section 2's snooping channel is real: the root hub's EP0 is diverted to
  usbport's own root-hub worker and never reaches `OpenEndpoint`, while every
  other endpoint, including an external hub's EP0, goes through
  `SubmitTransfer`, whose `TransferParameters+0x14` carries the raw SETUP
  bytes. So `GET_DESCRIPTOR(Hub)` and `SET_FEATURE(PORT_RESET)` on an external
  hub are observable.

Net effect on section 3's decision order: step 1 ("prefer usbport-provided
data") is the primary source for the TT's identity (which hub, which port on
it) at every depth. Step 2's snooping graph is required for the Route String
tiers, the Root Hub Port Number of a behind-hub device, and hub identification
(`Hub`, `Number of Ports`, `TTT`, `MTT`); since `TTT` is itself part of DW2,
not even DW2 is finished without it.

How wrong can we afford to be? On USB 2.0 the xHC does not route packets by
Route String (that is a SuperSpeed mechanism), so some controllers may
tolerate Route String = 0 for behind-hub HS devices. But the spec requires it,
the xHC uses the topology for TT/bandwidth bookkeeping, and FS/LS-behind-HS
will not work without correct TT fields and the Hub/MTT flags on the hub's
slot. Build it correctly; do not rely on controller leniency.

## 2. What the miniport can see

Under Option A the miniport never parses URBs, but it does see, on every hub's
EP0, the raw setup packets of the hub class requests that `usbhub20.sys`
issues through usbport, the same visibility that makes the SET_ADDRESS
interception work ([`../architecture.md`](../architecture.md), enumeration
flow). That is enough to reconstruct the tree passively:

| Snooped EP0 traffic (hub class) | What it reveals |
|---|---|
| GET_DESCRIPTOR(Hub): bmRequestType 0xA0, bRequest 6, wValue 0x0000, wLength 71 | The target device is a hub; the IN payload carries `bNbrPorts` and `wHubCharacteristics` (TTT bits) |
| SET_FEATURE(PORT_RESET): bmRequestType 0x23, bRequest 3, wValue 4, wIndex = port | Hub X is resetting port Y, so the next new device enumerates at hub X port Y |
| SET_FEATURE(PORT_POWER) / GET_STATUS(port) | Port lifecycle corroboration (optional) |

The first row's `wValue` is the one field a graph must not match on. Both
shipping hub drivers ask with `wValue = 0x0000` and `wLength = 71`, measured on
Win98+NUSB and Windows 2000 SP4 from the miniport's setup-key ring and from
QEMU's own `usb_hub_control` trace independently. The USB 2.0 hub-class
specification says a caller should send `0x2900`; neither shipping caller
does. Match `bmRequestType == 0xA0 && bRequest == 6` and take the descriptor
type from the reply.

Reconstruction sketch:

1. When a device on slot S receives GET_DESCRIPTOR(Hub): mark S as a hub;
   from the returned payload record `bNbrPorts` and TT think time; issue a
   Configure Endpoint updating S's Slot Context with Hub = 1, Number of Ports,
   TTT (and MTT once known; see open question 3).

   The command must be Configure Endpoint, not Evaluate Context. Spec 6.2.2.3
   p.412: "A 'valid' Input Slot Context for an Evaluate Context Command
   requires the Interrupter Target and Max Exit Latency fields to be
   initialized. Only these fields shall be evaluated when the xHC receives an
   Evaluate Context Command that flags the Slot Context", and "Only the Output
   Interrupter Target and Max Exit Latency fields are updated by the Evaluate
   Context Command."

   An Evaluate Context carrying Hub / Number of Ports / TTT / MTT does not
   fail; it ignores them, which is the worst available outcome.
   6.2.2.2 p.412 is where those four are named, and it is the Configure
   Endpoint's section. The full per-command table is in
   `../../usb-xhci-info/xhci-data-structures.md`, "Which command may set which
   Slot Context field".
2. When hub S receives SET_FEATURE(PORT_RESET) on port Y: record `(S, Y)` as
   the pending enumeration parent.
3. When the next EP0 open for USB address 0 arrives (the Phase 6 Enable Slot /
   Address Device BSR = 1 path): the new device's parent is `(S, Y)`. Derive:
   - Route String = parent's Route String with Y inserted at the next 4-bit
     tier (Y clamped to 15 per spec routing rules);
   - Root Hub Port Number = parent's Root Hub Port Number;
   - TT fields (only if the new device is FS/LS and the nearest ancestor hub
     is HS): TT Hub Slot ID = slot of that hub, TT Port = port on it. Both are
     available from the endpoint properties at any depth when the lookup
     succeeded (`HubAddr` through the address -> Slot ID map, `PortNumber`
     directly). `HubAddr != 0xFFFF` is that condition; a `0xFFFF` reading means
     no TT record was selected, the number may still be a real downstream port
     but nothing says on which hub, and the snoop is the only source. Section 4
     records why the implementation ended up trusting the graph first and the
     properties as the cross-check.
   - The child's own `MTT` (DW0 `25`), which the properties do not carry.
     `docs/usb-xhci-info/xhci-data-structures.md` section 8 scopes MTT as "is
     (or hangs off) a multi-TT hub interface", and spec 6.2.2.1's validity list
     for Address Device names MTT among the fields an FS/LS-behind-HS child
     must carry, so a downstream FS/LS device needs it as well as the hub does.
     It must come from step 1's snoop of that hub.
   - `TTT` is not a child field. Table 6-6 p.409 sets TTT only when `Hub = 1`
     and `Speed = High-Speed`, and states that otherwise "this field shall be
     '0'". It belongs on the hub's own Slot Context (step 1), and writing it on
     the child would contradict both that rule and 6.2.2.1's "all other fields
     are cleared to '0'".
4. On Disable Slot / device removal, prune the subtree state.

Enumeration is serialized above us (usbhub resets one port and addresses one
device at a time; section 4 confirms it), so a single pending-parent slot
suffices. Guard it with a timeout and fall back to "unknown parent": fail the
open loudly rather than addressing with a fabricated path.

## 3. Decision order

1. Prefer usbport-provided data. Where the endpoint-open properties carry hub
   address/port, use that as the primary source and keep snooping only for
   what is missing (Route String tiers, hub flags).
2. Snoop to fill the gaps per section 2. The GET_DESCRIPTOR(Hub) and
   PORT_RESET intercepts are passive (observe and complete normally); unlike
   SET_ADDRESS nothing is emulated, so the risk is bounded.
3. If neither works (usbport provides nothing and snooping proves
   unreliable), down-scope: support root-port devices only, and complete
   behind-hub endpoint opens with a clean failure. That keeps Phases 6-8
   shippable for direct-attach devices while the problem is revisited.
   Wrongly-addressed devices corrupt the schedule; cleanly-rejected ones do
   not.

## 4. What Phases 3 and 6 must log to settle this

This section asked three questions of the target stacks: whether an external
hub's EP0 traffic reaches `SubmitTransfer` with usable SETUP bytes; whether
usbhub/usbport serializes enumeration (one PORT_RESET -> one address-0 open,
never interleaved); and whether any endpoint-open/`SubmitTransfer` parameter
block for a device behind a hub carries hub address, port, or speed-of-parent
(the "log everything `StartController` and friends pass" habit from
[`../../usb-xhci-info/usbport-miniport-interface.md`](../../usb-xhci-info/usbport-miniport-interface.md),
extended to endpoint opens).

One QEMU caveat applies throughout: `usb-hub` is USB 1.1 (Full-Speed). It exercises Route String tiers and FS-hub paths but
cannot emulate a High-Speed hub, so the TT fields need `usb-host` passthrough
of a physical HS hub or real hardware ([`../build-and-test.md`](../build-and-test.md)
"QEMU coverage limits").

### Batch 7b-V0: the measurements

Run on both target stacks, and the readings agree field-for-field. Evidence in
`vm\win98-{debugcon,qemu-trace}.batch7bv0*`,
`vm\win2k-{debugcon,qemu-trace}.batch7bv0*`, the two
`*-batch7bv0-run1-counters.txt`, and `vm\win2k-batch7bv0-verifier.png` (the
files were discarded on 2026-08-30; the readings below are the record). Setup:
the Phase 7a checkpoint binary (`built Aug  5 2026 10:00:32`, extension
`0xB160`), a QEMU `usb-hub` on a root port and a `usb-mouse` on hub port 1.

An external hub's EP0 traffic does reach `SubmitTransfer` with usable SETUP
bytes, and the hub-recipient split is real: `probe class setups (any
recipient)` 26 against `probe hub-class setups` 25 on both stacks, the one
difference being the HID `SET_IDLE` (`0x21`, interface recipient). The snoop
channel section 2 assumes is confirmed at runtime on both builds, not only
from the disassembly.

`GET_DESCRIPTOR(Hub)` is issued with `wValue = 0x0000`, not `0x2900`, on both
stacks, at `wLength = 71`. A snooping graph keyed on `0x2900` would miss every
hub on every target we ship to. Confirmed twice over per target: the
miniport's own setup-key ring reads `0xA0060000`, and QEMU's independent
`usb_hub_control dev 2, req 0xa006, value 0, index 0, length 71` says the same
from the hardware side.

`SET_FEATURE(PORT_RESET)` is observable and correctly ordered, and the whole
port lifecycle with it: per-port `SET_FEATURE(PORT_POWER)` at hub start, then
on attach `CLEAR_FEATURE(C_PORT_CONNECTION)` -> `SET_FEATURE(PORT_RESET)` ->
`CLEAR_FEATURE(C_PORT_RESET)` -> `CLEAR_FEATURE(C_PORT_ENABLE)`. Enumeration is
serialized as section 2 assumes. The miniport's own probe cannot tell one port
feature from another, though: its setup key drops the low half of `wValue`,
which is where the feature selector lives, so `PORT_POWER` (8) and
`PORT_RESET` (4) share the key `0x23030000`. The key ring proves a port feature
was set; only the raw eight bytes in the qemu-build trace, or QEMU's
`usb_hub_*` events, say which. A snooping graph must read `wValue` in full.

The behind-hub properties confirm the field half at runtime, on both stacks
and identically: for the Full-Speed mouse on hub port 1, the block at `+0x30`
reads `0x00010002` (`HubAddr = 2`, the hub's own USB address, and
`PortNumber = 1`, the port on it) against `0x0001FFFF` / `0x0002FFFF` for the
two root-port devices. `DeviceSpeed` is `UsbFullSpeed` for the child and
`UsbHighSpeed` for the hub, so the Phase 5 task 7 override reaches the hub's
own handle and does not cascade to a device behind it.

A two-tier discriminator was unreachable on this build. A second `usb-hub`
chained behind the first, on a clean 2b boot with no other device on the bus,
is itself a Full-Speed device behind a hub and fails where the mouse did: hub1
alone gives `slots enabled` 1 / `endpoints opened` 1 / 25 hub-class setups,
and after chaining hub2 both counters are still 1 while `transfers refused for
retry` starts climbing. Its properties do arrive, and carry the same shape one
tier down: `+0x30` = `0x00010001`, `HubAddr = 1` (hub1's address),
`PortNumber = 1` (the port on it).

The behind-hub device did not enumerate on either target. No slot was enabled
for it (`slots enabled` stays 2, `endpoints opened` stays 2) and the driver
answered usbport's transfers with the requeue-and-retry status instead of a
clean failure.

The costs differed sharply, and the difference is the most important safety
reading of the phase. On 2a Win98 `transfers refused for retry` reached 1,803
and the guest ended dead: clock stopped, mouse pointer still tracking but no
click accepted, no bugcheck and no blue screen. On 2b Windows 2000 it stopped
at 17 and the machine stayed responsive, under a Driver Verifier armed with
Special pool, Force IRQL checking, Pool tracking and I/O verification, with no
bugcheck.

On 2b the damage was confined to the PnP device tree, with a control:
`Device Manager` produced nothing at all when
clicked (no window, no error, 0.0 s of guest CPU in 10 s) while `eventvwr.msc`,
an MMC snap-in that does not walk the device tree, opened normally on the same
desktop. On 2a the same stuck enumeration took the whole desktop with it,
which is what a target with no preemption does with the identical defect. The
root cause and fix are open question 5 below: the refused transfers were the
hub's, not the child's.

Phase 5 task 7's recorded residual did not reproduce and is refuted as
written. That entry says a hub with no transaction translator on a root port
"still bugchecks, one level down, as soon as an FS/LS device goes into it",
through `USBPORT_GetTt` returning `0xFFFFFFEC` from an empty `TtList`. QEMU's
`usb-hub` is USB 1.1 and has no TT, and no fault occurred on either target.

The measurement is that `HubAddr` came back as 2, which is the documented "the
lookup succeeded" reading, so a TT record existed for a hub that physically
has none. One reading of why, not confirmed against the binaries, is that the
override marks the hub High-Speed, a believed-HS hub gets
`USBPORT_Initialize20Hub`, and the same untruth that creates the hazard
removes it (open question 6).

What is settled either way is that the operating limitation as stated is
wrong: the topology is reachable, and what it costs is an unenumerable device,
not a bugcheck.

### Reading the TT pairs in a release build

Everything above that concerns `HubAddr`/`PortNumber` was read from a debug
build's per-observation dump. The measurement that separates the two readings
of `PortNumber` (the port on the nearest HS ancestor versus the immediate
parent's port) needs a two-tier FS/LS path, and the run that produces it is
long; `XHCI_DBG_VALUE_CHANGED`'s per-site budget is exhausted by ordinary HID
traffic in seconds.

So the miniport extension carries `XHCI_PROBE_TT_TABLE`: twelve distinct
`(HubAddr, PortNumber)` pairs as numbers, each with its observation count,
the set of device addresses it was seen with, and the OR of the speeds.
`0xFFFF` pairs are recorded too, so a run in which no transaction translator
is ever named is a measurement rather than an empty table.

`Dropped` counts observations rather than distinct pairs, and
`ProbeTtObservations == sum(rows) + Dropped` is enforced by the host suite and
re-checked against the guest by `scripts\local\readcounters.ps1`, which also
decodes the rows.

Three choices in that table matter. Rows are keyed on the full
`(HubAddr << 16) | PortNumber` value; keying on `HubAddr` alone fails 11
checks, because it collapses "the same TT hub on another port" into one row,
which is the distinction being measured. The addresses are a set rather than
a first-seen witness because every candidate for this reading arrives at
`DeviceAddress` 0 (a device that never enumerates has no other address), and
the speeds separate a hub's own opens from the FS/LS child behind it. And
`Dropped` counts observations, not pairs: a full table meeting one pair twice
has lost two readings.

### The graph (task 7b-A.1)

`src/xhci_topo.c` implements sections 2 and 3 of this document: a graph of
external hubs keyed by usbport device address, fed by the snooped hub-class
traffic and by the reply bytes of the two requests that carry numbers. What
scopes a node's life is the record teardown chain (every
teardown/disown/release detaches through the record's graph key, which
survives the address-0 window) plus the SET_ADDRESS-time migration that
re-keys a node to a re-assigned address and prunes whatever stale node sat
under it. What it learns, and from which of the two channels:

| Fact | Source | Notes |
|---|---|---|
| "this device is a hub" | any hub-class port request (`0x23`/`0xA3`) | Positive evidence needing no descriptor: only a hub has ports, and usbport answers the root hub's class traffic itself through the RH_ callbacks. `GET_DESCRIPTOR(Hub)` promotes too. |
| `bNbrPorts`, `wHubCharacteristics` (and so `TTT`) | the `GET_DESCRIPTOR(Hub)` reply | Read at completion through the SG list's `MappedSystemVa`, before `UsbPortCompleteTransfer`, the only window usbport keeps that mapping. |
| hub port count, lower bound | `SET_FEATURE(PORT_POWER)` high-water mark | Kept only until a descriptor supplies the real number. usbhub powers every port at hub start, so on the measured bus the two agree; that is a cross-check to make on a target, not a reason to trust the bound. |
| hub speed | this driver's own device record | usbport's `DeviceSpeed` at the hub's EP0 open. |
| parent hub + port of the next device | `SET_FEATURE(PORT_RESET)` | Step 2 of section 2. Armed by the reset, spent by the next address-0 open. |
| single-TT vs multi-TT | `SET_INTERFACE` on a device already known to be a hub | Follows the request rather than latching, per Table 6-4's "currently enabled alternate setting". |
| disconnects | the `GET_STATUS(port)` reply | A 1 -> 0 transition of `wPortStatus` bit 0, or `wPortStatus` bit 0 still set with `wPortChange`'s C_PORT_CONNECTION bit set, which is a disconnect+reconnect between usbhub polls that never shows the graph an empty port. The request alone cannot tell a connect from a disconnect (`CLEAR_FEATURE(C_PORT_CONNECTION)` is sent for both), so this half needs the reply channel. |

The reply channel has a second consumer, and it is not this graph. Phase 9
task 9-A.2 snoops `GET_DESCRIPTOR(Configuration)` on the same EP0 path, read
through the same `MappedSystemVa` in the same pre-`UsbPortCompleteTransfer`
window, to recover an isochronous endpoint's `bInterval`, the one number
usbport discards outright (`src/xhci_desc.h`). It is a separate file, a
separate snoop and separate fields on the transfer record: this graph is keyed
on a hub and answers where devices sit; that one is keyed on a device record
and answers how often an endpoint wants servicing. What they share is the
channel, so anything that breaks the channel breaks both. A change to the
placement-time snoop or to the completion-drain fold has two callers to check.

Two constraints from batch 7b-V0 are built in: `GET_DESCRIPTOR(Hub)` is
matched on `bmRequestType`/`bRequest` only, because both shipping hub drivers
send `wValue = 0x0000`; and `wValue` is read whole for the port features,
because the selector lives in the low half that the miniport's own probe key
drops.

A request is snooped when it is placed, not when it is submitted. usbport
requeues a refused transfer and sends it again, so counting at submission
would report one `SET_FEATURE(PORT_RESET)` as several, and the graph's own "a
second reset arrived before the first was claimed" counter would then report a
serialization violation that never happened.

The graph stores hubs only, never leaves. A leaf's position is already held by
its own `XHCI_DEVICE` record, and putting every device in the graph as well
would be two statements of one fact, and would spend the table on the HID
keyboards that can never be anybody's parent.

Route String tier order is corroborated, not transcribed, and the write-up
lives in `docs/usb-xhci-info/xhci-data-structures.md`, "Route String tier
order": Table 6-4 defers the format to USB3 section 8.9, which is not in
`docs/references/`, so the nibble order comes from two mirrored implementations
agreeing. The two things the local PDF does settle (the root port is not in
the route, and a hub wider than 14 ports clamps to 15) are quoted there.

### Step 1: marking a hub's own Slot Context (task 7b-A.2)

A hub's own Slot Context is marked from the graph (`Hub`, `Number of Ports`,
and `TTT`/`MTT` where the device's decoded speed allows them), and the command
is a Configure Endpoint, for the reason given in section 2 step 1.

- The trigger is the descriptor reply, not the endpoint open. The marking is
  derived and issued in the same deferred-work pass that folds
  `GET_DESCRIPTOR(Hub)`'s bytes. Waiting for the hub's interrupt pipe to be
  opened would leave it undescribed across the `SET_CONFIGURATION` and the
  port-power sweep usbhub does in between.
- Every Configure Endpoint carries the same four fields, endpoint ones
  included, because A0 is mandatory on that command and a Slot Context built
  without them would clear a marking rather than leave it. Opening the hub's
  interrupt pipe is the next thing usbhub does after reading the descriptor,
  so that clearing command would arrive within milliseconds of the marking. It
  also means an endpoint open arriving before the descriptor costs no extra
  command.
- The need is derived, never stored. It is the wanted marking (read from the
  graph and the device's speed) compared against what a completed command
  programmed, so a `SET_INTERFACE` moving MTT re-arms it on its own and a
  marking already held is never reissued. What a completion records is what
  its command carried, not a fresh derivation; otherwise a `SET_INTERFACE`
  arriving while one is in flight would be recorded as programmed and never
  sent.
- A re-address drops it, since Address Device overwrites the whole Output Slot
  Context from an Input one whose Hub must be 0 (6.2.2.1 p.411). usbport
  re-enumerates hubs on this driver today, so this fires on every recovery
  cycle and has its own counter.
- A refusal gives up the marking and nothing else. The device keeps its
  address and its endpoints; a per-record flag stops the derived need retrying
  for ever, and `HubMarkFailures` is the reading that says the xHC is
  scheduling a hub's downstream traffic without knowing it is a hub.
- The graph's address key is not on its own enough to decide who gets marked.
  A record that re-enumerates gives its usbport address back with no
  disconnect, and none of the detach sites runs on that path, so a node can
  outlive the address naming its device, and a later device given that
  address would be programmed as a hub. The derivation therefore also requires
  the node's root port to be the record's. Detaching instead was rejected on
  evidence: the 7b-A.0 2a trace reads one `req 0xa006` across five
  `usb_xhci_slot_address` events, so a re-enumeration cannot be assumed to
  re-read the hub descriptor.
- What a run should read: `topology: hub slots marked` equal to the number of
  hubs enumerated, `topology: hub marking commands` about the same (the
  marking fires at the fold, before usbhub has opened anything to ride on),
  `topology: hub marking failures` 0. On QEMU's Full-Speed `usb-hub` the
  marking is `Hub = 1`, `Number of Ports = 8`, `TTT = 0`, `MTT = 0`; Table 6-6
  p.410 forbids a TT think time on anything that is not a High-Speed hub,
  whatever `wHubCharacteristics` declared.
- `TTT`/`MTT` follow the decoded speed class, so the rule lives in the pure
  core (`XhciTopoHubMark`) rather than in the context builder; which PSIV
  means High Speed is a property of the controller's PSI table. The builder
  refuses the half it can answer (a TTT on a non-hub, and a hub with `Number
  of Ports` 0) and has no MTT refusal: Table 6-4 allows MTT on a Full-Speed
  device behind a multi-TT parent, and the encoder cannot see the parent. A
  hub descriptor claiming no ports is not marked and gets its own counter
  (`topology: hub descriptors with no ports`) rather than silence, since it is
  self-consistent and nothing else would report it. Likewise a hub identified
  by port traffic alone is not marked from the power sweep's high-water mark,
  which is a lower bound where Table 6-5 wants `bNbrPorts`.

### Step 3: addressing a device behind a hub (task 7b-A.3)

A device behind a hub is given a slot, a Route String, a root port and, where
one applies, a TT triple. The part of step 3 that had to change is the third
bullet:

- "Prefer the properties and use the snoop as the cross-check" is inverted for
  the TT, on the strength of batch 7b-V0's measurement. It read `HubAddr` = 2
  for a Full-Speed device behind a Full-Speed `usb-hub`: usbport named a
  transaction translator for a hub that physically has none (open question 6).
  `HubAddr != 0xFFFF` is therefore a test of whether usbport selected a TT
  record, not of whether a transaction translator exists, and Table 6-6 p.409
  conditions both DW2 fields on "connected through a High-speed hub".

  So the
  graph decides, being the only thing that knows each hub's decoded speed, and
  usbport's pair is kept beside it and compared. `XhciTopoTtFor` walks to the
  nearest High-Speed ancestor and returns the port on it, which is the same
  value `USBPORT_GetTt` computes; on a real High-Speed hub the two should
  agree, and `TtPairsAgreed`/`TtPairsDisagreed` says whether they did.
- An unresolvable TT fails the device. If the graph names a High-Speed
  ancestor whose record holds no Slot ID, the Address Device is refused rather
  than sent with the fields cleared: section 3's own rule, applied to the one
  case where clearing them looks harmless and is not.
- The child's MTT follows the enabled alternate setting, not the hub's
  capability. This reads Table 6-4 (whose child clause omits the "enabled by
  software" qualifier its hub clause carries) the way Linux does. A Full-Speed
  hub behind a multi-TT High-Speed hub carries MTT for both reasons, so the hub
  marking ORs the bit rather than assigning it.
- Step 4's prune has a device-record half. A behind-hub device has no root-hub
  callback to leave through: the hub's own `GET_STATUS(port)` reply is the only
  statement that it has gone, so the fold reports the pair and the driver
  tears the record down, and with it any subtree below, keyed on the graph
  rather than on record liveness so that the fifteen-second re-enumeration
  cycle does not look like a departure. A root port going down sweeps
  everything behind it, and a disown gives every address below it back.
- A behind-hub record must not hold a root-hub port. `HubPort` is 0 for one
  and `RootPort` carries the path's origin. `xhciDevByHubPort` is how a connect
  change, a port disable, a disown and the enumerating-port scan all find "the
  device on this root port", and one tier down that device is the hub; a
  record answering both would have usbhub's re-enumeration of the hub take the
  child apart. The mutation that sets `HubPort` fails 16 checks.
- The two enumeration entitlements are mutually exclusive, not ranked. A
  hub-port reset (reported by the snoop) spends the root-port claim, and a
  root-port reset drops the pending hub claim (`XhciTopoDropPending`), so at
  most one is ever armed and the open needs no rule for which to believe. That
  is valid because enumeration is serialized above this driver (section 2,
  measured), so the older bracket is always the abandoned one.
- `Topology.MaxTier` counts hubs only, so it reads 1 while a leaf record sits
  at tier 2. That is correct, not a defect.

Observed on both targets, and the stop rule closed affirmatively (roadmap
batch 7b-A's verdict boxes). A device two tiers behind external hubs
enumerated on 2a and again on 2b: `BehindHubOpens` = `Claims` = `Addressed` =
3 on each, against 7b-V0's 1,803 refusals and a dead guest. QEMU's own
`usb_xhci_slot_address slotid 3, port 2.1` confirms the reconstructed Route
String from the hardware side, and 2b's trace resolves every slot to its true
port including `slotid 5 -> 2.2.1`. A second 2b boot rebuilt the graph
identically, so the reconstruction is deterministic rather than
order-dependent. 2b ran under Driver Verifier with Force IRQL checking. No
Option A hub limitation is owed; that was the question the stop rule was there
to decide.

Two readings from the same run settle earlier questions. The two-tier
measurement refutes the "immediate parent's port" reading of `PortNumber` by
the value, and the hub marking of step 1 matched its prediction
field-for-field on both hubs.

The two-tier Full-Speed device at `2.2.1` reads `tier 2, route 0x00012, root
port 6, behind hub address 4 port 1; usbport claimed TT hub 2 port 2`. Its
immediate parent is hub2 (address 4) on hub2's port 1; usbport says `HubAddr`
2 / `PortNumber` 2, the port on hub1 (the hub `HubAddr` names), which the
nearest-HS-ancestor reading predicts.

Pair row `[3]` carries `TT hub address 2, port 2 on it` with addresses `0,4,5` (hub2
and its child sharing one pair, the discriminator the table was built for) at
`ProbeTtObservations` 30 with 0 dropped and the on-guest identity holding.
Reproduced on SP4.

### Step 4 observed (task 7b-V.1, identically on 2a and 2b)

Remove the child: `topology: behind-hub devices gone` 0 -> 1, the record
released, the event arriving inside a `GET_STATUS(port)` reply, since a
behind-hub departure has no callback. Reinsert it on the same port: a new
claim/open/address (2/2/2), not a survivor, with the xHC re-decoding
`port 2.1`. Move it to another hub port: the route is rebuilt (`0x00001` ->
`0x00003`, parent pair -> `hub address 2 port 3`) and QEMU's own
`usb_xhci_slot_address slotid 3, port 2.3` confirms the new route against the
hardware rather than against this driver's counters.

Whole hub out with three active devices below it: `gone` 2 -> 5, both hub
nodes pruned, nothing left but the root-port keep-alive, and `transfers
refused for retry` 0 on both targets.

The rule the clause was written for: a device moved between hub ports keeps
its usbport address, so "it still works" is not evidence the route was
rebuilt. Read the record's tier, route and parent pair as numbers.

## 5. Open questions

1. Exact usbport endpoint-properties layout: closed by batch 7a-0. The layout
   is `docs/usb-xhci-info/usbport-miniport-abi.md` section 5, confirmed
   field-for-field on both builds, and the topology fields are the four named
   in section 1. Nothing about it differs between NUSB and SP4.
2. Whether usbport re-opens endpoints (`ReopenEndpoint`) when a device moves
   or re-enumerates behind a hub, and whether the map survives that.
3. Multi-TT hubs: MTT is selected by SET_INTERFACE (alternate setting 1 on the
   hub). The graph snoops that request and follows it rather than latching
   (section 4, graph table). A multi-TT hub has not been measured on a target.
4. Whether any controller this project meets rejects Route String = 0 for a
   behind-hub HS device. Worth one experiment on real silicon once hubs work,
   purely to know how much slack exists (do not design for it).
5. Why the behind-hub endpoint open produced no slot: closed by tasks 7b-A.0
   and 7b-A.1.1. The 7b-V0 failure was not a refusal of the child's open. The
   enumerating-port hint was set by every root-port reset completion and
   nothing consumed it, so the child's address-0 open, which follows a reset
   of the hub's downstream port (invisible to this driver), read the stale
   hint and was attributed to the root port the hub had been enumerated on.

   It found the hub's own record there and took the re-enumeration branch:
   address cleared, `ADDRESS_VALID` dropped, Max Packet Size reset, and the
   addressing chain re-entered with BSR = 1 against the hub's slot.

   The 1,803
   refusals that killed the 2a guest were the hub's transfers being turned
   away from that point on, not the child's. That is also why `SlotsEnabled`
   stayed at 2 and nothing in the endpoint-refusal counters moved.

   The 2b Windows 2000 run measured the hijack directly: `OpenRefusalsBuffer`
   = 0 and `OpenRefusalsMalformed` = 0, opens going 6 -> 7 with that one open
   accepted, `SlotsEnabled` unchanged at 2, no third record allocated, and the
   hub's record going `ADDRESSED / address 2 / Flags 7` -> `DEFAULT / address
   0 / Flags 4` in the same step. Earlier attempts to read the path off the
   refusal counters failed because no refusal counter can name an accepted
   open; passing a gate and never reaching it look identical from the refusal
   side. Every return from `XhciSlotOpenEndpoint` now increments exactly one
   counter, the entry itself is counted in `OpensTotal`, and the identity
   between them is enforced by the host suite and printed on a target.

   The hijacked hub's end state, from a live dump of the device records: `State
   = DEFAULT`, `DeviceAddress = 0`, `Flags = DCBAA_SET` only (EP0 unbound),
   nothing owed and nothing in flight, with every hub-class transfer refused.
   The progress detector fails that record and usbport re-enumerates the hub,
   roughly every 15 s.

   A build that took the `EP0_OPEN` gate out of the
   detector's scope left the hub stuck permanently instead, and left running
   it reproduced the original blocker outright: 1,229 refusals with `records
   failed - no progress` 0, then a dead guest in the 7b-V0 shape (clock frozen,
   refusals frozen because nothing was calling the driver any more, ~16% host
   CPU, no bugcheck). So the ~15 s cycle is a recovery, and that gate's
   coverage is what keeps a Windows 98 guest alive while a behind-hub device
   is attached.

   The fix is one claim per root-port reset (`EnumClaimSpent`): a reset arms
   the entitlement, one address-0 open spends it, and an open arriving with
   none is refused and counted in `OpenRefusalsNoClaim`. A genuine
   re-enumeration is always preceded by a fresh reset, so it is unaffected.
   Two host vectors cover it, and deleting the gate reproduces the field
   failure field-for-field.

   One claim per reset alone bounds the hijack to once per reset rather than
   preventing it, because usbhub issues two `RH_SetFeaturePortReset` per root
   port. The open sits between the two resets, on both targets, line for line
   (`win98-debugcon.batch7ba0-run1` 615 / 632 / 676 / 687 and
   `win2k-debugcon.batch7ba10` 263 / 292 / 338 / 359):

   ```text
   RH_SetFeaturePortReset -> OpenEndpoint (address 0) ->
   GET_DESCRIPTOR(device) -> RH_SetFeaturePortReset -> SET_ADDRESS
   ```

   That is USB's ordinary enumeration bracket, whose second reset is the one
   before SET_ADDRESS. The second reset is a reset inside an enumeration whose
   claim has already been spent, and that is what the rule is keyed on: the
   claim is spent and the port being reset still carries a live, unaddressed
   record holding EP0 (`xhciDevEnumerationInProgress`). A genuine
   re-enumeration also resets a port that has produced a device, and that is
   resolved by the record's state rather than by the port's history: on a
   re-enumeration the record is addressed, failed, disowned or torn down, and
   the reset arms as before. Two pre-existing re-enumeration vectors and a
   pre-existing disown vector fail when the respective term is deleted.

   At most one reset is suppressed per claim (`EnumResetSuppressed`), because
   a record left mid-enumeration by an abandonment that produces no refusal is
   never failed by the progress detector (which only advances on a refusal),
   so an unbounded rule would trade a bounded hijack for a permanently dead
   root port. The cost of the rule being wrong is one enumeration round.

   One ordering matters with it: `xhciRhRefresh` announces the connect change
   before the reset. When a replug latches both, the record the reset would
   read belongs to the device that has just left, and reading it would deny
   the incoming device the claim it needs.
6. Whether `USBPORT_Initialize20Hub` really runs for a believed-High-Speed 1.1
   hub. This is the reading offered for why `GetTt` did not fault in batch
   7b-V0, not confirmed against either binary. The consequence is closed (task
   7b-A.3); the mechanism is still open, and the two are worth keeping apart.

   What the driver does no longer depends on the answer: the TT triple is
   derived from the topology graph, which carries each hub's speed as this
   driver decoded it, so a Full-Speed hub's children get no TT whatever
   usbport says about them. What remains unanswered is why usbport says it,
   and the driver measures the disagreement (`TtPairsDisagreed`) instead of
   resolving it.

   A run in which that counter is zero with a Full-Speed hub on
   the bus would refute the 7b-V0 measurement this entry rests on, which is a
   second reason both pairs are kept rather than one being thrown away.
7. Whether promoting a device to hub on the GET_DESCRIPTOR request is worth
   the node it spends. The `XHCI_TOPO_RT_HUB_DESC` arm of
   `XhciTopoObserveSetup` calls `xhciTopoPromote` before any reply proves
   hubness (`src/xhci_topo.c`), so a non-hub that receives a class-recipient
   `GET_DESCRIPTOR` permanently spends one of the eight nodes, and once all
   eight are spent, `xhciTopoNodeAdd` refuses every real hub thereafter
   (`Dropped++`).

   This is by design per section 2's decision table: the request is the only
   channel that arrives before the numbers are needed, and the promotion sits
   above the request test so a `CLEAR_FEATURE` counts as evidence too.

   The larger leak in this area (a torn-down hub leaking its
   node forever) is fixed by `TopoAddress` + `XhciTopoMigrate`. What survives
   is the narrower case: a node spent on a device that never answers as a hub,
   reclaimed by nothing short of `StopController`. Worth revisiting only if a
   real bus is measured spending nodes this way.
