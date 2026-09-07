# SuperSpeed storage on root ports, behind a registry switch

Written 2026-09-04 at the project owner's request, after `1.0.1.0` was cut.
It is a **proposal**. Nothing in it has been built, no boot has been taken for
it, and it has no phase or task ids yet; where the text says what a batch
would do, it is naming work, not reporting it. The evidence it rests on is the
tree as it stands, the two ABI records (`docs/usb-xhci-info/usbport-miniport-abi.md`,
`docs/usb-xhci-info/usbport-miniport-interface.md`), and the device
characterisations in `docs/contributing/test-equipment.md`.

It was reviewed the day it was written and revised the same day on the
owner's instruction; section 13 lists what that review changed and why. A
second review on 2026-09-05 checked the revised record against the tree and
the 1.2c specification text and found twenty things wrong with it, five of
them in the same-day revisions; section 14 lists those and where each
correction landed, so a reader of either earlier draft can tell what moved.

Two decisions the owner has already taken frame everything below:

1. The feature is gated behind a registry value and **off by default**.
2. With the value absent or 0, the driver behaves **exactly as it does today**.
   Not "equivalently": the same port map, the same refusals, the same
   counters, the same bytes on the bus.

Everything else in this record is derived from those two, from the question
in section 1, and from what the tree already does.

## 1. The question

Can this driver carry a SuperSpeed mass-storage device without replacing the
Option A architecture, by driving the device's SuperSpeed link itself and
telling `usbport.sys` that what sits on the port is an ordinary High-Speed
USB 2.0 device?

`docs/usb-xhci-info/xhci-programming.md`, "What SuperSpeed Support Would
Require", answers no, and its reasoning is sound for the thing it describes:
*general* SuperSpeed support (hubs, isochronous, bandwidth, link power
management) needs facilities the Win2000-era `usbport.sys` does not have, and
that is Option B. What that section does not separate out is the narrower
case: a **bulk device on a root port**. Such a device needs no SuperSpeed
bandwidth model (bulk is not scheduled), no SuperSpeed hub path (there is no
hub), and no SuperSpeed root-hub semantics that the miniport does not already
own outright. The remaining gaps are all inside the miniport's own boundary,
and the miniport already lies to usbport about speed for a different reason.

So the answer proposed here is yes, for that case, with the boundary drawn
explicitly in section 6 and the things that stay out listed in section 11.

## 2. Why this is not Option B: what the tree already does

Three existing facts carry most of the weight. They are cited rather than
re-argued; each has its own home in the tree.

**The root hub already reports every connected port as High Speed.**
`src/xhci_port.c`, the block ending at `XHCI_HUB_PORT_HIGH_SPEED`, and
`docs/contributing/implementation-invariants.md`, "Root Hub Reporting": a
non-High-Speed device under the root hub sends usbport into a
transaction-translator lookup that bugchecks both shipping builds, so the
report layer says High Speed for Low and Full Speed devices and the slot and
endpoint contexts are programmed from the driver's own decoded PORTSC speed
(`shadow->Speed`), never from usbport's `DeviceSpeed`. SuperSpeed is one more
speed that report layer would say "High Speed" about, and one more value
`shadow->Speed` can hold. The mechanism exists; what changes is the set of
speeds it covers.

**The miniport already owns addressing and a per-device record.**
`SET_ADDRESS` never reaches the bus as a transfer: it is intercepted and
emulated with Address Device (`src/xhci_slot.c`, task 6-B.3), so the miniport
already holds, per device, the slot, the root port, the decoded speed, the EP0
ring and the EP0 Max Packet Size, and already survives usbport's `ReopenPipe`
of EP0 mid-enumeration (ABI record section 8). A SuperSpeed device is a device
record whose speed field says so.

**The miniport already reads the bytes of control replies before usbport sees
them.** The topology snoop (task 7b-A.1) and the configuration-descriptor snoop
(task 9-A.2, `src/xhci_desc.c`) both capture the SG list's `MappedSystemVa` at
placement and read the reply in the completion drain, under the controller
lock, before `UsbPortCompleteTransfer` hands the mapping back. Section 5.4
proposes the first *write* into that buffer this driver would make; the window
and the ownership argument are the ones those two snoops already stand on.

What Option A genuinely cannot provide, and this record does not try to
provide, is in section 11.

## 3. The switch

**Rule for both values (owner, 2026-09-04): if a value is not present, the
driver assumes its default.** `XhciSuperSpeed` absent means 0, off, today's
behaviour. `XhciSuperSpeedReset` absent means 0, automatic. Absence is not an
error, is not logged as one, and is the ordinary case rather than an edge:
a driver copied in by hand at a bench, an upgrade over an older INF, an INF
that never ran, and a registry the user has cleaned all arrive with neither
value present, and every one of them must start exactly as a fresh install
does. The INF still writes both values as 0 so a user finds them where they
look, but nothing in the driver depends on the INF having done so, and a
build must never distinguish "absent" from "present and 0" in behaviour. It
may distinguish them in a counter (section 3.2), and only there.

### 3.1 The value

| | |
|---|---|
| Name | `XhciSuperSpeed` (settled by the owner, 2026-09-04) |
| Type | `REG_DWORD` |
| Where | The controller's **driver (software) key**: the key a plain `AddReg` under an INF install section writes, and the key the two log values already live in |
| Values | `0` off, the default. `1` on. Any other value is **refused, not clamped**: the driver applies 0 and records that it refused, on the same rule the INF states for `XhciLogVerbosity` (a refusal must close the feature, never open it in some other shape) |
| Absent | 0, and not an error |
| Set by | **The user, by hand, in Registry Editor.** `XHCISNAP` does not set it and will not learn to (settled by the owner, 2026-09-04). The INF's `AddReg` writes the 0 so the value is visible where the user looks for it; the release notes carry the key path per target and the "disable/enable or reboot" rule of 3.2 |

### 3.2 How it is read

Through `UsbPortGetMiniportRegistryKeyValue` with `BOOL = TRUE`, from
`StartController`, in the same routine that reads the two log values
(`xhciLogReadValues` in `src/xhci_dispatch.c`, which `xhciLogStart` calls).
That routine is the template because every constraint on the read has already
been paid for there and is documented on it:

- PASSIVE_LEVEL only, and `StartController` is the one callback this driver
  knows is PASSIVE (ABI record, `UsbPortGetMiniportRegistryKeyValue`
  subsection).
- No new import. The `Zw*` names are denied by
  `scripts/import-gate/xhci98-imports.allow`; usbport's own service is the
  only registry channel this project may use.
- **Nothing in the read may fail a start.** The value missing, the read
  failing, and the service pointer being NULL in the packet all leave the
  value at 0 and the driver starts as today. The read cannot say why it
  failed (the service collapses every failure into one code), so, as for the
  log values, the `MPSTATUS` the service returned is kept beside the value so
  a reader can tell "read a 0 somebody set" from "found nothing".
- usbport zeroes the miniport extension before every start, so the value is
  re-read at every start and cached nowhere else. Changing it takes a
  disable/enable of the controller or a reboot, and the record says so
  wherever the value is documented.

The read lands in the extension as three fields on the pattern the log block
uses: what was **read**, what was **applied**, and the **status**. All three
go into the snapshot header (`docs/contributing/passthru-snapshot-instrument.md`)
so a dump from a stranger's machine states which mode the driver was in
without anyone having to ask.

### 3.3 Where the read sits in the start path, and why that is the whole design of the gate

The port map is built by `xhciBuildPortMap` and the ports are powered by
`xhciPowerPorts`, both from `XhciInitController`. The switch must be read
**before the port map is classified**, because the port map is the one place
the two modes diverge (section 4). That order already holds: `StartController`
in `src/xhci_dispatch.c` calls `xhciLogStart`, and so `xhciLogReadValues`,
before it calls `XhciInitController`, and the comment on the call says the
values are read before anything that might want to note something. So the
switch is read at that existing site, beside the two log values, and nothing
moves; it is one PASSIVE read at one site, and the restore path (which skips
`XhciInitController`) reuses the value already in the extension, since
usbport does not zero the extension across a restore. The first draft had the
order reversed and proposed moving the read (section 14 item 1).

### 3.4 The second value: the reset policy

A SuperSpeed port has two resets where a USB 2.0 port has one (section 5.3),
and usbhub asks for "a reset" without knowing that. Which one the miniport
writes is policy, and the owner decided (2026-09-04) that the policy is a
registry value rather than a constant:

| | |
|---|---|
| Name | `XhciSuperSpeedReset` |
| Type | `REG_DWORD`, same key as `XhciSuperSpeed` |
| `0` | **Automatic**, the default: hot reset (PR) when the port is Enabled with its link in U0, warm reset (WPR) when the port is in the Error state (link SS.Inactive) or in Compliance Mode (section 5.3) |
| `1` | **Always warm**: every usbhub reset on a SuperSpeed port is a WPR. Slower per enumeration, one path, always recovers the link |
| Other | Refused, not clamped: 0 applied and the refusal counted, the rule `XhciLogVerbosity` follows (`XhciLogDebugView` is a plain zero/non-zero switch and is not the template) |
| Absent | 0 |

Three rules bound it:

- **It is read only when `XhciSuperSpeed` applied as 1.** With the main
  switch off the second value is never read, so the switch-off start path
  makes exactly one registry read more than today, the main switch itself,
  and the second value adds none. This is what keeps section 7's invariant a
  statement about one divergence point rather than two.
- **It decides only what usbhub's `SET_FEATURE(PORT_RESET)` becomes.** The
  automatic warm reset the driver issues on its own when a port event reports
  SS.Inactive (the last row of section 5.3's table) is link recovery, not a
  usbhub reset, and happens under either value.
- It goes into the snapshot header beside the main switch, as read, applied
  and status, so a bench reading of a reset problem names the policy it was
  taken under.

There is no "always hot" value. A hot reset cannot recover SS.Inactive or
Compliance, so that setting would be a port that never comes back with no
counter to say why; a diagnostic that wants it can be a build-time define in
the `qemu` flavour, not a shipped knob.

The gate is then a **classification**, not a set of `if` statements scattered
through the driver. With the switch off, every USB 3.x port keeps the class it
has today (`XHCI_PORT_CLASS_USB3_COMPANION` or `_USB3_ORPHAN`) and nothing
downstream can tell the feature exists, because every SuperSpeed path below is
reached only through a port class that is never assigned. With the switch on,
those ports get a new class, and the paths that exist for that class run.
Section 7 states this as the invariant and says how it is tested.

## 4. What changes at start when the switch is on

### 4.1 Port classification and power

| | Today | Switch on |
|---|---|---|
| USB 3.x port with a USB 2.0 companion | `USB3_COMPANION`: unpowered, unmanaged | New class `USB3_MANAGED`: powered, managed |
| USB 3.x port with no companion | `USB3_ORPHAN`: unpowered, unmanaged, out of scope | `USB3_MANAGED` likewise |
| USB 2.0 ports | managed, unchanged | managed, **unchanged** |
| Controller with no USB 2.0 port | refused, `XHCI_CAPS_NO_MANAGED_PORTS` | **accepted** (owner's decision 3, 2026-09-04): its SuperSpeed ports are managed and it serves SuperSpeed devices only, since a High-Speed device on such a connector appears on whichever other xHCI its USB 2.0 wires reach. Counted (`ss.all-superspeed controller`) and named in the release notes. The refusal stays when the switch is off |

The companion pairing convention (`xhciPairCompanions`) is kept as it is: it
exists to *name* connectors and it still does. What it no longer decides is
power.

The port-power pass (`xhciPowerPorts`) has an ordering argument written on it:
assert the USB 2.0 halves first and confirm them before deasserting the USB 3.x
halves, because the two halves of one connector OR their PP outputs and a
deassert-first order drops VBus. With the switch on there is no deassertion
pass at start at all, so that argument is unexercised rather than violated.
`PortsUnpowered` then reads 0 on a controller with SuperSpeed ports, which
today is documented as "devices are training onto ports nothing services"; the
counter's reading needs the mode beside it, which is why section 3.2 puts the
mode in the header.

### 4.2 Root hub port numbering

The root hub reports `ManagedPortCount` ports to usbport. With the switch on
that count grows by the number of USB 3.x ports. Two rules:

- **USB 2.0 ports keep the hub-port numbers they have today**, and the USB 3.x
  ports are appended after them. A user who turns the switch on must not see
  their existing devices move to different port numbers, and a bug report
  taken in one mode must be readable against the other.
- The count is the number of ports the classifier marked managed, as it is
  today: the hub-port table is built by walking the port map and counting
  every port for which `XhciPortIsManaged` is true (`src/xhci_port.c`), and
  `src/xhci_rh.c` reports that count; power confirmation does not enter it.
  `PortsPowered` is a counter beside it, not its source, and the comment on
  `PortsPowered` in `src/xhci_init.c` that calls it "the number Phase 5's root
  hub is built on" is stale and is corrected in the `-A` batch. The count is
  never zero (`src/xhci_rh.c`, "NumberOfPorts must never be zero"). No
  change to that guard.

A SuperSpeed device on a connector therefore appears on a *different* hub port
from a High-Speed device on the same connector. Device Manager on the targets
shows only "Port N" either way.

## 5. What changes per device when the switch is on

### 5.1 Speed decode

`XHCI_SPEED_SUPER` already exists, and the decode already produces it:
`xhciSpeedClassFromKilobits` (`src/xhci_caps.c`) returns it for a PSI row of
5,000,000 kb/s or more (kilobits because SuperSpeed's 5 Gb/s does not fit 32
bits of b/s, and the code says so), `xhciDefaultSpeedClass` returns it for
the default SuperSpeed PSIV, and `test/test_caps.c` already checks both. So
with the switch off a device record *can* carry `XHCI_SPEED_SUPER` today;
what stops it going anywhere is that the port it would sit on is never
managed, and two refusals downstream of the decode: `xhciDefaultPsiv` returns
0 for the class, so no Slot Context can be built from it, and
`XhciInitialMps0` returns 0 (section 5.2), both with a comment naming this
record's subject. What the switch changes is those two refusals, and it
changes them **only for a device on a `USB3_MANAGED` port**: the speed class
alone is not the admission, the port class is (section 7). The first draft
said the class had to be added (section 14 item 2). The `XHCI_RH_SEEN_*`
first-decode bits gain a SuperSpeed member so the "first decode of a speed"
print exists for it.

Every value transcribed here (the default PSIV, the Slot Context speed
encoding, the PORTSC link-state encodings in 5.3) comes from
`docs/usb-xhci-info/xhci-data-structures.md` after transcription from the
spec PDF, per AGENTS.md. This record names the fields and does not state their
bit values.

### 5.2 Slot and EP0

- Slot Context speed = the SuperSpeed PSIV, Route String 0 (root port),
  everything else as for a root-port High-Speed device.
- EP0 Max Packet Size = 512. `XhciInitialMps0` returns 0 for SuperSpeed today
  so that an attempt to address one refuses at the caller; it returns 512 for
  a SuperSpeed slot when the class exists.
- **usbport's EP0 reopen is ignored on a SuperSpeed slot.** After the device
  descriptor is read, usbport reopens EP0 with `TotalMaxPacketSize` set to the
  descriptor's `bMaxPacketSize0` byte taken literally (ABI record section 8,
  step 3). Section 5.4 makes that byte read 64. Whatever it reads, the slot's
  EP0 stays at 512 and no Evaluate Context is issued; the reopen is counted
  (`ss.mps0 ignored`) rather than acted on. `XHCI_EP0_MPS_IS_LEGAL`, the
  four-value set, is not widened: it guards the USB 2.0 path and 512 is never
  legal there.

### 5.3 Port behaviour on a SuperSpeed port, as reported to usbhub

usbhub drives the port through the USB 2.0 hub-class port features and
reads USB 2.0 port status bits; the miniport translates. The translation
table, each row a decision this record proposes and the `-0` batch confirms
against the spec text:

| usbhub does | On a USB 2.0 port today | On a SuperSpeed port |
|---|---|---|
| reads status | CCS -> connected; PED -> enabled; connected -> High Speed | same, plus: link in U3 -> suspended, and PED is **masked to 0 until the first `C_PORT_RESET` of the connection has been delivered** (5.3.1). Connected -> High Speed **as today**, which is the lie section 2 described |
| `SET_FEATURE(PORT_RESET)` | write PR, report `C_PORT_RESET` on PRC | decided by `XhciSuperSpeedReset` (section 3.4). Automatic: port Enabled with link in U0, write PR (a hot reset); port in the Error state (link SS.Inactive) or Compliance Mode, write WPR (a warm reset). Always-warm: WPR every time. `C_PORT_RESET` is reported on PRC, which the spec sets on every exit from the Reset state, hot or warm; WRC is set beside it for a warm reset, and also when a hot reset the driver asked for was turned into a warm one by the link (4.19.1.2.5), which is counted so the policy's readings can be told apart. Hot and warm counted separately |
| `SET_FEATURE(PORT_SUSPEND)` | PLS = U3 with LWS | same write; the resume differs (next row) |
| `CLEAR_FEATURE(PORT_SUSPEND)` | PLS = Resume, then U0 | PLS = U0 with LWS directly: software does not resume a SuperSpeed link by writing Resume, the xHC runs the LFPS handshake from that U0 write (4.19.1.2.13.3). The port's own Resume substate still exists, for a device-initiated wake, and is reported as leaving suspend as today |
| `CLEAR_FEATURE(PORT_ENABLE)` | PED = 0 | PED = 0. On a USB 3.x port this puts the link in SS.Disabled, and a SuperSpeed device whose SS link is disabled falls back to its USB 2.0 connection, which appears on the companion port. Section 6.1 uses this deliberately |
| `CLEAR_FEATURE(PORT_POWER)` | PP = 0 | same |
| port event with PLC only | acknowledged, no hub change unless leaving suspend | SS.Inactive: warm reset once, automatically; if the link does not reach U0, report as disconnected-then-connected so usbhub re-enumerates. CEC: same treatment |

Three facts drive the reset rows, each read from the 1.2c text
(`docs/references/spec-1.2c-dump.txt`) for this revision of the record. A
USB 3.x port enables itself when a device attaches: link training sets PED
without software writing PR, and CCS is asserted on the Polling to Enabled
transition (Table 5-27, the footnote on CCS), so usbhub never sees a
connected port whose link is still training; its reset arrives on an
already-enabled port in U0, where a hot reset is legal and cheap and is what
usbhub expects to see complete. Warm reset is the only recovery from
SS.Inactive and Compliance Mode, states a USB 2.0 port cannot enter. And a PR
or WPR write moves the port to the Reset state from **any** state except
Disconnected, Powered-off and Disabled (4.19.1.2.5), so there is no
"Polling" case for the policy to decide: a port in Polling is still in the
Disconnected state with CCS clear, usbhub has nothing to reset, and a PR
written there is ignored by definition. The first draft's Polling row rested
on CCS being set before training completed, which the footnote says it is
not, and is withdrawn (section 14 item 5). The `-0` batch transcribes the
USB3 port state machine, Figure 4-27 in 1.2c (Figure 4-26 is the USB2 Enabled
substate diagram), for the exact conditions, and the record here is
provisional on that reading. The existing rule that **every** change bit is
acknowledged, WRC and CEC included, already holds (`src/xhci_port.c`, "Every
change bit that was set is acknowledged"), so no SuperSpeed port can stop
reporting for want of an acknowledgement; what changes is that WRC and CEC
become inputs rather than noise.

#### 5.3.1 The enabled-before-reset mask

On a USB 2.0 port PED becomes 1 only when the reset usbhub asked for
completes, and usbhub's enumeration sequence is written against that order:
connect, debounce, reset, enabled. On a USB 3.x port CCS and PED arrive
together at connect, so an unmasked report would show usbhub an enabled port
it has not reset. Whether usbhub tolerates that is unmeasured (8.3, item 7),
and the proposal does not depend on the answer: the report layer masks PED to
0 on a `USB3_MANAGED` port from the connection's CSC until the driver has
latched a `C_PORT_RESET` for that connection, and reports the hardware bit
from then on. The port then looks to usbhub exactly as a USB 2.0 port does,
and the reset that lifts the mask is the one usbhub asked for. The mask is
report-only: the shadow keeps the hardware PED, the suspend derivation in
`src/xhci_port.c` that reads PED keeps reading it, and a PEC on a masked port
still latches `C_PORT_ENABLE`. A new CSC re-arms the mask. Masked reads are
counted (`ss.ped masked`) so a port usbhub never reset can be told from one it
reset and lost.

### 5.4 Descriptor normalisation: the second half of the lie

The report layer tells usbport the *port* carries a High-Speed device. The
device's own descriptors then have to agree, because usbport and usbhub read
them literally:

| Field | SuperSpeed device says | usbport/usbhub would do | Proposed |
|---|---|---|---|
| `bMaxPacketSize0` (device descriptor byte 7) | 9, meaning 2^9 = 512 | reopen EP0 with `TotalMaxPacketSize` = 9 | **rewrite to 64** in the reply before completion |
| `bcdUSB` (bytes 2-3) | 0x0300, 0x0310 or 0x0320 (the held units read 0x0320 on both flash drives and 0x0300 on the bridge, `test-equipment.md`) | nothing measured; usbport patches its own root hub's to 0x0200 and reads a device's for nothing this project has found | **rewrite to 0x0200**, as cheap insurance and so a snapshot never carries a descriptor that contradicts the port's report |
| bulk `wMaxPacketSize` (configuration descriptor) | 1024 | passes bits 10:0 = 1024 into `MaxPacketSize` at the endpoint open; whether `OpenEndpoint` or `QueryEndpointRequirements` refuses 1024 for bulk is **unmeasured** in all three usbport builds | leave the descriptor alone; the endpoint open on a SuperSpeed slot ignores the value and programs 1024 (bulk), 512 (control), and the smaller of usbport's value and 1024 (interrupt). If the `-0` reading finds usbport refuses 1024, fall back to rewriting the field to 512 |
| SuperSpeed Endpoint Companion descriptors (type 0x30, six bytes, one after each endpoint descriptor) | present | `USBD_ParseConfigurationDescriptorEx` and usbstor's walk skip unknown types by `bLength` | leave in place; **read `bMaxBurst` from them** for the Endpoint Context's Max Burst Size (section 5.5). A lost reading means burst 0, which works and is slow |
| `DEVICE_QUALIFIER`, `OTHER_SPEED_CONFIGURATION`, BOS | STALL, STALL, present | usbhub on a port reported High Speed asks for none of these; a STALL is a tolerated answer | not intercepted |

The rewrite is done in the completion drain, under the controller lock, on
`MappedSystemVa`, before `UsbPortCompleteTransfer`: the window both snoops
already read in. It is done only for a transfer on a SuperSpeed slot whose
setup packet was `GET_DESCRIPTOR(DEVICE)`, that completed successfully, and
that transferred at least 8 bytes (the byte is at offset 7; usbport's first
read asks for 64 and a device may answer 8 or 18). Both rewrites are counted,
and a device whose descriptor already said 64 and 0x0200 is counted
separately, because that would be a High-Speed device on a SuperSpeed port,
which the port class says cannot happen and a counter should be able to
contradict.

**This is the first byte this driver writes into a buffer usbport owns.** The
argument that it is sound: the buffer is the transfer's own data buffer, the
xHC has just DMA'd into it under this driver's TRBs, usbport has not looked at
it (it cannot until the completion it has not received), and the SG list's
mapping is still live. The `-0` batch confirms nothing in usbport's completion
path re-reads the buffer from a second mapping, which the two read-only snoops
did not need to establish.

### 5.5 Endpoint contexts and transfers

- Bulk: Max Packet Size 1024, Max Burst Size = `bMaxBurst` from the companion
  descriptor when the snoop recorded it, else 0. The configuration-descriptor
  walk in `src/xhci_desc.c` is extended to record a per-endpoint burst the
  way it records a per-endpoint isochronous `bInterval`, under the same rules
  (commit only a complete configuration; a `SET_CONFIGURATION` naming another
  configuration discards it).
- Control: 512, burst 0.
- Interrupt: allowed, up to 1024, `bInterval` encoding the same as High
  Speed (Table 6-12 has a SuperSpeed row; the `-0` batch reads it). Max ESIT
  Payload is a **periodic** field, not an isochronous one
  (`xhci-data-structures.md` already says "Periodic only"), so a SuperSpeed
  interrupt endpoint needs it too, and for a SuperSpeed endpoint the spec
  takes it from the companion descriptor's `wBytesPerInterval` (4.14.2)
  rather than from Max Packet Size times burst. The descriptor walk records
  that field beside `bMaxBurst`; a lost reading falls back to Max Packet Size
  times (Max Burst + 1), the formula the code uses today for High Speed,
  which over-states rather than under-states the payload.
- Isochronous: **never opened on a SuperSpeed slot.** A configuration that
  carries one sends the device back to USB 2.0 at the configuration-descriptor
  read (section 6.3); an open that reaches the miniport anyway is refused and
  counted, as a backstop.
- The TD Size arithmetic uses Max Packet Size; whether a burst changes it is a
  spec question (the TD Size definition in 4.11.2.4) for the `-0` batch, not a
  thing to assume either way.
- No Stream Context Arrays. UAS is never selected (section 6.4).

## 6. The refusals that keep the feature narrow

### 6.1 The one fallback mechanism: sending a device back to USB 2.0

Every refusal below that ends with the device working at High Speed uses one
mechanism, defined here once and triggered from two places (6.2 and 6.3).
When the driver decides, from a descriptor reply on a SuperSpeed slot, that it
will not serve the device at SuperSpeed, it:

1. completes that transfer as a failure, so usbhub's enumeration of the port
   stops where it is;
2. writes PED on the USB 3.x port (`XhciPortscDisable`, the RW1C write
   `src/xhci_port.c` already owns and documents), which puts the port in the
   Disabled state and its link in SS.Disabled, with the port's SuperSpeed
   terminations withdrawn;
3. reports the port to usbhub as **disconnected** and keeps it so (the
   **held** state), acknowledging every change bit the port raises meanwhile,
   as the shadow already does for all of them;
4. lets the slot go on the ordinary teardown path when usbhub removes the
   device it now sees as gone.

The device does the rest. A SuperSpeed device whose link partner has withdrawn
its terminations gives up the SuperSpeed path and connects on its USB 2.0
wires, which terminate at the connector's companion port, where it enumerates
as the High-Speed device it presents itself as at that speed
(`test-equipment.md` records that the two USB 3.0 flash drives present
differently at the two speeds). That is the outcome today's port strategy
produces for every device by never powering the USB 3.x half; the mechanism
reproduces it per device, after the fact.

**The hold must outlast the device's stay on USB 2.0.** The USB 3.0
specification's link state machine has an upstream port in SS.Disabled retry
its SuperSpeed connection when it receives a USB 2.0 bus reset, and usbhub
resets the companion port during the enumeration the fallback just caused,
and again on every error recovery after that. Each of those resets makes the
device look for the SuperSpeed terminations again; if the driver has restored
them by then, the device leaves the companion port for the SuperSpeed one, is
refused again, and ping-pongs between the two buses. So a held port is
released only when the device has gone, and "gone" is decided per port class:

- **Companion-paired port** (the connector's USB 2.0 port is on this
  controller, by the pairing convention): the pairing is a **convention, not
  a spec guarantee** (`xhciPairCompanions` says so, and today nothing depends
  on its being right), so the release rule may not rest on it alone. It rests
  on evidence instead: the hold is released on the companion port's
  disconnect CSC **only if that companion port reported a connect CSC after
  the hold began**, which is the fallen-back device arriving where the
  convention said it would, and is the one observation that shows the pairing
  is physical for this device. If no such connect is seen, because the device
  was unplugged during the fallback or because the convention paired the
  wrong ports, the hold is treated as an orphan hold (next bullet), and the
  two outcomes are counted apart (`ss.held paired`, `ss.held unpaired`). A
  disconnect on a companion port that never connected releases nothing, so a
  connector the convention mis-paired cannot release another connector's
  hold. The release re-arms the USB 3.x port so it can detect the next
  device: a PORTSC write with PLS = RxDetect and LWS, which is the Disabled
  state's exit to Disconnected (4.19.1.2.1). It is not a warm reset: PR and
  WPR do not act on a port in the Disabled state (4.19.1.2.5), and the first
  draft's "WPR out of Disabled" was wrong (section 14 item 7). Nor is it a PP
  cycle, so no VBus argument is needed; the `-0` batch transcribes the exit
  and its prerequisites.
- **Orphan port** (the USB 2.0 wires reach a different xHCI), **and a paired
  port whose companion never connected**: the driver never sees the device
  leave, and a port in the Disabled state has no detection of its own to fall
  back on, since the terminations it would detect a device with are the ones
  it withdrew. The hold lasts until the controller's next start. A supported
  device plugged into that connector later falls back to the other xHCI's USB
  2.0 port (or, on a paired connector, to this controller's companion port),
  which is today's behaviour for that connector, so the failure mode is "no
  SuperSpeed on that connector until a disable/enable or reboot", counted
  (`ss.held orphan`, `ss.held unpaired`) and named in the release notes.
  Re-arming a held port on a timer is refused for the ping-pong reason above:
  the driver cannot tell whether the device is still there on the other
  controller, or still on its way to the companion port. The first draft had
  no exit for the paired port whose companion never connected (section 14
  item 9).

Whether the Disabled state raises CSC on a physical disconnect is a spec
question the `-0` batch transcribes from Figure 4-27 rather than assumes; the
paired-port rule does not need the answer, and the orphan rule is written on
the pessimistic reading. Linux's xHCI driver refuses usbcore's request to
disable a SuperSpeed port outright, which is one more reason to treat the PED
write as a deliberate one-way step with a defined exit and not as an ordinary
port feature. The held state, its release rule with the connect-evidence
condition, the unpaired case and the counters are `test_port` vectors in the
`-C` batch, including the vector where the companion disconnects without ever
having connected and the hold must stay.

### 6.2 A hub on a SuperSpeed port

usbhub cannot drive a SuperSpeed hub (it asks for the USB 2.0 hub descriptor,
type 0x29, and a SuperSpeed hub answers with 0x2A). Let it try and it fails
messily and retries. So: when the device-descriptor reply on a SuperSpeed slot
carries `bDeviceClass` 9, the driver sends the device back to USB 2.0 (6.1)
from that reply. A SuperSpeed hub always reports class 9 in its device
descriptor, so the decision needs nothing past byte 4 of the first reply, the
same reply 5.4 already reads. Two things then happen on their own:

- The hub's USB 2.0 side connects on the companion port and is served as a
  High-Speed hub, exactly as it is today.
- The hub's SuperSpeed side is never configured, so its downstream SuperSpeed
  ports are never powered, so SuperSpeed devices behind it fall back to their
  USB 2.0 paths through the hub's High-Speed side. That is the outcome the
  current port strategy already delivers for every device; the refusal
  preserves it one tier down.

The USB 3.0 hub units this project holds (`05E3:0610` High-Speed halves,
`docs/contributing/test-equipment.md` row 4) are what this is measured on.

### 6.3 Isochronous endpoints

Mult is a SuperSpeed-isochronous field (Max ESIT Payload is periodic, and
5.5 handles it for interrupt endpoints), usbport's per-packet stamps are a
High-Speed statement, and audio or video at SuperSpeed is not a target. The first draft of this record refused the isochronous open
and left the device at SuperSpeed with only its other pipes, on the grounds
that nothing could make it fall back; 6.1 is exactly the thing that can, and
it is cheaper to use it than to ship a webcam that half-works. So: when the
configuration-descriptor walk in `src/xhci_desc.c` commits a complete
configuration on a SuperSpeed slot that contains an isochronous endpoint, the
driver sends the device back to USB 2.0 (6.1) from that reply. The walk
already commits only complete configurations, and usbhub reads the
configuration descriptor before it issues `SET_CONFIGURATION`, so no class
driver has loaded when the decision is taken. The device then enumerates on
the companion port at High Speed and its isochronous pipes work there as they
do today.

An isochronous open that reaches the miniport on a SuperSpeed slot anyway is
refused and counted, as a backstop; it is a defect indicator, not a path.

The release notes name this: with the switch on, a SuperSpeed device that
carries an isochronous endpoint runs at High Speed, as it does with the switch
off.

### 6.4 UAS

`usbstor.sys` on every target selects alternate setting 0, which is BOT on
every unit this project holds (`test-equipment.md`, read at SuperSpeed: the
`090C:2320` drive and the `174C:5106` bridge present BOT plus UAS, the
`0781:55AB` drive BOT only, all with 1024-byte bulk endpoints). UAS needs a class driver
neither Windows 98 nor Windows 2000 nor XP has, so the driver never sees a
Stream request and needs no stream support. A UAS-only device, which exists
but is rare, does not work and the release notes say so. It is not sent back
to USB 2.0, because it is UAS-only there too and nothing is gained.

### 6.5 Devices behind hubs

Unreachable by construction: no SuperSpeed hub is ever configured (6.2). A
SuperSpeed device behind a hub is a High-Speed device behind a High-Speed hub,
today's path.

## 7. The invariant: switch off is today's behaviour, and how that is held

The claim is stronger than "the feature is disabled". It is that a build
carrying this feature, run with the value absent or 0, produces the same port
map, the same power writes, the same hub-port numbering, the same refusals,
and the same readings in every counter the previous release has, on every
controller. What it does **not** claim, because it cannot: the start path
makes one registry read the previous release did not make (the main switch,
section 3.4), the snapshot header carries fields the previous release's
header does not (section 3.2), and an INF install writes two values the
previous INF did not. Those are the additions, they are the whole list, and a
diff of two matrix reports must expect exactly them and nothing else. The
first draft said "byte-identical" and "exactly the registry reads it makes
today", and neither survives the additions (section 14 item 3).

What enforces it:

- **One divergence point.** The only code that consults the switch is the
  port-map classification (section 3.3), plus the read of the second value,
  which is skipped unless the first applied as 1 (section 3.4). Every other
  SuperSpeed path is reached through the `USB3_MANAGED` class: either a port
  of that class, or a device record whose root port is of that class. A
  device record's speed alone is **not** the key, because `XHCI_SPEED_SUPER`
  already exists and the decode already produces it with the switch off
  (section 5.1); a path keyed on the speed alone could change an existing
  refusal. A review rule for the batches: a `SuperSpeed.Enabled` test
  anywhere but the classifier and that one read is a defect, a SuperSpeed
  behaviour keyed on `XHCI_SPEED_SUPER` without the port class is a defect,
  and `XhciSuperSpeedReset` is consulted only on a port of the
  `USB3_MANAGED` class.
- **Host tests on the classifier.** `test_caps.c` already carries port-map
  vectors; each gains a run with the switch off that must produce the exact
  table it produces today, and a run with it on that produces the new one.
  `test_port.c` gains the SuperSpeed translation rows of section 5.3 with the
  switch on, and a proof that a `USB3_COMPANION` port reaches none of them.
- **The matrix.** The Phase 10 VM matrix and the Phase 16 unattended run
  are run in two switch-off setups, because the INF writing 0 and the value
  being absent are different registries: once from a driver copied in by
  hand with neither value present, and once from the package's INF install
  with both present as 0. Both must read identically to the previous
  release's run in every field it produced, with the header additions above
  as the only expected difference, which is what the Phase 16 report header
  exists to make diffable. The `-V` batch adds a SuperSpeed leg beside them,
  never instead of them.
- **The header.** The mode is in every snapshot, so no reading can be
  misattributed to the wrong mode.

## 8. What can be observed where

### 8.1 Virtual machines

The matrix runner (`scripts/vm-matrix/run-matrix.ps1`), the image
preparation and soak scripts beside it, and the XP and Windows 2000
xHCI-only launcher generators (`scripts/setup-qemu-winxp.ps1`,
`scripts/setup-qemu-win2k-xonly.ps1`) launch `qemu-xhci` with `p3=0`, and
`docs/contributing/build-and-test.md` records why: QEMU never falls a
SuperSpeed-capable device back to USB 2.0, so with SuperSpeed ports present
its `usb-storage` trains at SuperSpeed onto a port the driver does not serve.
The older generators (`scripts/setup-qemu.ps1`, `setup-qemu-win2k.ps1`,
`setup-qemu-win2k-smp.ps1`) default to a plain `qemu-xhci` and take `p3=0`
only through their `-XhciDevice` argument, so "every guest" is a statement
about how the launchers are run, not about the committed defaults; the `-V`
batch names the launchers it uses (section 14 item 14). That same QEMU
property makes it the SuperSpeed vehicle: a launcher with `p3=N` and the
switch on attaches `usb-storage` at SuperSpeed on all three NT guests and the
Windows 98 guest. What QEMU can and cannot show:

- Can: the whole enumeration and descriptor path, usbport's acceptance or
  refusal of 1024-byte bulk endpoints (measured, not derived, on NUSB, SP4
  and XP's usbport), the EP0 reopen being ignored, the PED mask (5.3.1), and
  the switch-off leg.
- Cannot, for want of a device: **neither refusal trigger of section 6.**
  QEMU has no SuperSpeed hub model and no SuperSpeed isochronous model; its
  `usb-hub` and `usb-audio` are Full-Speed devices, and QEMU attaches a Low,
  Full or High-Speed device to one of its USB 2.0 ports whatever `p3` says,
  so neither ever sits on a SuperSpeed slot and neither can carry the device
  descriptor or configuration descriptor that fires 6.2 or 6.3. The first
  draft named those two models for the first half of the refusals and was
  wrong (section 14 item 11). The refusal mechanics, the failed transfer, the
  PED write, the held port and the slot teardown, are `test_desc` and
  `test_port` vectors in the `-C` batch; if the `-V` batch wants a VM reading
  of them it needs a `qemu`-flavour build-time define that fires the trigger
  on the storage device, and any such reading is labelled synthetic.
- Cannot, by construction: the **second half of any refusal**. QEMU never
  reconnects a device on USB 2.0 after its SuperSpeed link is disabled, so no
  VM run shows the device reappearing on the companion port, or the held
  port's release on the companion's disconnect. That half is bench-only
  (8.2), and a green `-V` run must not be read as covering it. Nor: link training, warm reset,
  SS.Inactive, Compliance, U3 on a real PHY, Max Packet Size enforcement on
  IN transfers (QEMU's xHC does not enforce it, `src/xhci.h` on
  `XHCI_EP0_MPS_FULL_INITIAL`), or throughput that means anything.

### 8.2 Bench

The storage devices are already held and characterised at SuperSpeed on a
modern host (`docs/contributing/test-equipment.md`): the `090C:2320` and
`0781:55AB` flash drives and the `174C:5106` ASMedia bridge, all with
1024-byte bulk endpoints on a SuperSpeed root port; the `090C:2320` drive and
the bridge present BOT plus UAS, the `0781:55AB` drive BOT only, and the two
flash drives report `bcdUSB` 0x0320 and the bridge 0x0300. The bench machine has to
expose a SuperSpeed root port on the xHCI the driver is installed on; the
E460 does, and the `test-equipment.md` rig positions already name a root
connector (position D). The one reading the acceptance test cannot skip is a
file round trip at position D with the switch on, on Windows 98 SE metal,
against the same round trip with the switch off (which is today's High-Speed
fallback and is already a recorded reading). The readings only the bench can
take are the second halves of section 6: the `05E3:0610` hub at position D
refused on the SuperSpeed port and served on the companion (6.2), the held
port staying held across usbhub's resets of the companion, and its release
and re-arm on unplug (6.1). **The isochronous refusal (6.3) has no specimen
yet**: every USB Audio unit in `test-equipment.md` is Full Speed or High
Speed, so none of them can appear on a SuperSpeed slot and none can fire the
trigger. A SuperSpeed device carrying an isochronous endpoint (a USB 3.0
webcam or capture device) is a prerequisite of the `-E` row for 6.3, and
until one is acquired that half of 6.3 is unobserved on every vehicle and the
release notes say so (section 14 item 12).

### 8.3 What stays unmeasured until a batch measures it

Listed so the record cannot be read as claiming them:

1. usbport's `OpenEndpoint`/`QueryEndpointRequirements` treatment of a bulk
   `MaxPacketSize` of 1024, in NUSB, SP4 and XP. Decides section 5.4's
   configuration-descriptor row.
2. usbhub's device-descriptor validation, if any, on `bMaxPacketSize0` and
   `bcdUSB` before the miniport's rewrite could matter. Decides whether the
   rewrite is necessary or merely tidy.
3. Whether any usbport completion path re-reads the data buffer through a
   mapping other than the SG list's (section 5.4's soundness argument).
4. That `UsbPortGetMiniportRegistryKeyValue` is populated in XP's usbport as
   it is in NUSB and SP4 (the ABI record read those two).
5. Hot versus warm reset behaviour on real silicon, and how long usbhub
   waits for `C_PORT_RESET` against a link that retrains.
6. That a device sent back to USB 2.0 (6.1) reconnects on the companion port
   on real silicon, stays there across usbhub's resets while the hold lasts,
   and what the Disabled state does on a physical disconnect (whether CSC is
   raised). The paired-port rule is written not to need the answer; the
   reading confirms the orphan rule's pessimism or relaxes it.
7. Whether usbhub tolerates a port that reports enabled before it has reset
   it. The mask of 5.3.1 makes the answer moot for the shipped path; the
   reading says whether the mask is necessary or merely tidy, the same status
   as item 2.

## 9. Work, as batches

On the roadmap's batching convention. A phase number is not assigned here;
the version it would ship in has a code change and so moves the third field
(`1.0.2.0` on the numbering Phase 19 states).

| Batch | Where confirmed | What |
|---|---|---|
| `-0` | Static | Spec transcription into `xhci-data-structures.md`: SuperSpeed PSIV default, Slot Context speed, PORTSC WPR/WRC/CEC and the PLS encodings for U0/U3/Polling/Inactive/Compliance, the USB3 port state machine of Figure 4-27 (the Disabled state's exit to Disconnected by the PLS = RxDetect write with LWS and its prerequisites, whether CSC is raised in Disabled on a physical disconnect, the states PR and WPR act from and the hot-to-warm conversion of 4.19.1.2.5 with its WRC reporting, the Error and Compliance Mode states the automatic policy keys on, the CCS assertion point in Table 5-27), the USB 3.0 specification's SS.Disabled exit rule for an upstream port that has fallen back, Endpoint Context Max Burst Size and Max ESIT Payload from the companion descriptor (4.14.2), TD Size against burst, Table 6-12's SuperSpeed row. Binary reads for 8.3 items 1-4 and 7, recorded the way the ABI record records its other findings, with addresses |
| `-A` | Host | The two values: read, refusal, the second read gated on the first, header fields, INF `AddReg` on both install paths with the comment block. The classifier, the new port class, and the all-SuperSpeed controller accepted under the switch. `test_caps` switch-off identity vectors (run twice: value absent, and value present as 0) and switch-on vectors; the stale `PortsPowered` comment in `src/xhci_init.c` corrected (section 4.2) |
| `-B` | Host | Speed class, default PSIV, `XhciInitialMps0`, Slot Context, EP0 reopen ignored, endpoint MPS/burst policy. `test_ctx` vectors |
| `-C` | Host | Descriptor rewrite in the completion drain, `bMaxBurst` in the descriptor walk, the send-back mechanism of 6.1 with its held state and the two release rules, the hub trigger (6.2) and the isochronous trigger (6.3), the isochronous-open backstop, the PED mask of 5.3.1. `test_desc` and `test_port` vectors |
| `-V` | VM | `p3=N` launchers for the four guests with the value set, named per guest (8.1); storage enumerated and round-tripped at SuperSpeed on each; the switch-off matrix reading identically to `1.0.1.0`'s in every field the previous release produced (section 7), in both the value-absent and the INF-installed setups. No refusal trigger can fire in QEMU (8.1); a synthetic reading of the mechanics is optional and labelled |
| `-E` (or the machine's initial) | Bench | Position D round trip, switch on and off, Windows 98 SE; the second half of the hub refusal (the reappearance on the companion port, the hold across usbhub's resets, the release on unplug); the isochronous refusal in both halves once a SuperSpeed isochronous specimen is held (8.2), recorded as unobserved until then; warm reset and SS.Inactive observed if they occur; counters read through `XHCISNAP` |

Checkpoint: the bench round trip with the switch on, and the switch-off
matrix diffing clean against the previous release. Neither alone.

## 10. Documentation this would change

Each of these states the opposite of what the switch does, and each would say
"off by default, and then exactly today's behaviour" in its own terms:

- `AGENTS.md`: the "USB scope" row, "Port Strategy (USB 2.0 vs USB 3.x)", and
  the two "Do not" rules on SuperSpeed and USB 3.x ports.
- `docs/usb-xhci-info/xhci-programming.md`: "What SuperSpeed Support Would
  Require" keeps its argument and gains the boundary this record draws; the
  port topology table's orphan row.
- `docs/using/release-notes.md`: "It is not USB 3.0" becomes "It is not USB
  3.0 unless you turn it on, and then it is USB 3.0 for bulk storage on root
  ports only", with the 6.x refusals as known limitations.
- `src/xhci98.inf`: both values on both install paths and their comment
  block, beside the two log values.
- `docs/contributing/implementation-invariants.md`, "Root Hub Reporting":
  the High-Speed report now covers a fourth speed.
- `docs/usb-xhci-info/win98-wdm.md`, "USB Stack Architecture and the
  Integration Decision": the Option B paragraph's "SuperSpeed would require
  this full Option B stack first".
- `docs/contributing/architecture.md`: the component tree's "USB3 ports: left
  unpowered and unmanaged" line.
- `README.md` and `docs/README.md`: one line each.

## 11. What this does not do

Option B's list, restated as what stays out with the switch on:

- No SuperSpeed hubs, and no SuperSpeed devices behind hubs.
- No UAS, no streams.
- No SuperSpeed isochronous, so no USB Audio or video at SuperSpeed: a
  device that carries an isochronous endpoint is sent back to USB 2.0 and
  runs at High Speed (6.3).
- No SuperSpeed on an orphan connector after a device has been sent back
  from it, until the next start (6.1).
- No U1/U2 link power management; the link sits in U0 or U3.
- No USB 3.1 Gen 2 or later speeds as anything other than "SuperSpeed"; the
  speed class is one class.
- No correct speed shown to the user: Device Manager says High-Speed, because
  that is what usbport was told.
- Nothing for USB4/Thunderbolt beyond what the existing port strategy says.

## 12. Decisions taken

All five were put to the owner and settled on 2026-09-04. None remain open.

1. **The value name** is `XhciSuperSpeed`.
2. **`XHCISNAP` does not set it.** The user sets it by hand in Registry
   Editor; that is the documented and only route (section 3.1).
3. **The all-SuperSpeed controller is accepted when the switch is on.** It
   serves SuperSpeed devices and nothing else, since a High-Speed device on
   such a connector appears on whichever other xHCI its USB 2.0 wires reach;
   it is counted and the release notes say what it can and cannot serve. The
   refusal stays when the switch is off, which section 7 requires anyway
   (section 4.1).
4. **Endpoint filter, not class filter.** The design gates on transfer type
   (bulk and interrupt served; an isochronous endpoint sends the device back
   to USB 2.0, section 6.3) and not on interface class. A class filter
   would need the interface descriptor, would buy nothing the isochronous
   trigger does not, and would exclude a SuperSpeed Ethernet adapter that
   otherwise works for free. Mass storage is the validation target and the
   release-notes claim, not the code's filter.
5. **The reset policy is a second registry value**, `XhciSuperSpeedReset`,
   automatic by default and always-warm on request, read only when the main
   switch applied (section 3.4). The `-0` batch still reads the port state
   machine before the automatic policy's conditions are written into code;
   the value chooses between policies, it does not replace the reading.

## 13. Revisions from the 2026-09-04 review

The first draft was reviewed the day it was written and the owner instructed
the review's five findings to be applied. None of them changes the two
framing decisions or the five in section 12; each is listed so a reader of the
first draft can tell what moved and why.

1. **The fallback is one mechanism, used twice.** The draft's isochronous
   section said nothing could make such a device fall back, while its hub
   section disabled a hub's SuperSpeed port to do exactly that. The mechanism
   is now defined once (6.1) and triggered from the device descriptor for
   hubs (6.2) and from the configuration descriptor for isochronous endpoints
   (6.3); the isochronous open refusal is demoted to a backstop, and 5.5, 8.3,
   9, 11 and decision 4 follow.
2. **The held port has a defined exit.** The draft held the port "until the
   next real CSC", which a USB 3.x port in the Disabled state may never raise,
   and did not consider that a fallen-back device retries SuperSpeed on every
   USB 2.0 reset it receives. 6.1 now releases a paired port on the
   companion's disconnect, holds an orphan port to the next start, and the
   `-0` batch transcribes the USB3 port state machine and the SS.Disabled
   exit rule. (Written as "Figure 4-26" in that revision; it is Figure 4-27 in
   1.2c, section 14 item 6.)
3. **Polling is a warm-reset case.** The draft wrote a hot reset for a link
   "in U0 or training"; a hot reset is a U0 operation, so the automatic policy
   was changed to write WPR from Polling (5.3). *Superseded on 2026-09-05*:
   the 1.2c text puts CCS on the Polling to Enabled transition, so usbhub
   never sees a port in Polling and the case does not arise; 3.4 and 5.3 now
   key the policy on the Enabled, Error and Compliance Mode states. This item
   also left 3.4's table saying "U0 or training" while changing 5.3. Both are
   section 14 item 5.
4. **PED is masked until the first reset.** A SuperSpeed port reports enabled
   at connect, an order usbhub's sequence is not written against; 5.3.1 masks
   it until the first `C_PORT_RESET`, and 8.3 item 7 records that usbhub's
   tolerance is unmeasured.
5. **QEMU shows half of every refusal.** 8.1 now says the reappearance on the
   companion port is bench-only, and the `-V` and `-E` rows say which half
   each observes.

## 14. Revisions from the 2026-09-05 review

A second review, the day after the first, read the revised record against
the tree, the launcher scripts, `test-equipment.md` and the 1.2c text in
`docs/references/spec-1.2c-dump.txt`, and returned twenty findings. All
twenty were verified against the source they name before being applied, and
all twenty were accepted. None changes the two framing decisions or the five
in section 12; two (items 8 and 9) change the design of the held port, one
(item 5) withdraws a same-day revision, and three (items 11, 12 and 14)
shrink what the record had claimed could be observed. Two more findings were
about other documents and are recorded at the end.

1. **The start order was backwards** (3.3). The record said the port map was
   built before the log values were read and proposed moving the read;
   `StartController` reads the log values before it calls
   `XhciInitController`, so the switch is read at the existing site and
   nothing moves.
2. **`XHCI_SPEED_SUPER` already exists** (5.1, 7). The record said the class
   had to be added and that both default-PSIV functions omitted it;
   `xhciSpeedClassFromKilobits` and `xhciDefaultSpeedClass` both produce it
   today and `test_caps` checks that they do. Only `xhciDefaultPsiv` and
   `XhciInitialMps0` refuse it. Section 7's rule that a SuperSpeed path is
   reached "through a device record whose speed is `XHCI_SPEED_SUPER`" was
   therefore unsafe, since such a record can exist with the switch off; the
   port class is now the key and the speed alone is named as a defect.
3. **"Byte-identical" and "exactly the registry reads it makes today" were
   both false** (3.4, 7, and the index entry). The main switch is one read
   the previous release does not make, the header gains fields, and the INF
   writes two values. Section 7 now lists those three as the whole set of
   additions, and the matrix is run in both the value-absent and the
   INF-installed setups, since the record had proposed testing "absent" with
   an INF that makes the value present.
4. **The hub-port count is not the powered count** (4.2). The table is built
   from `XhciPortIsManaged`, not from `PortsPowered`, and the comment on
   `PortsPowered` that the record quoted is itself stale; its correction is
   `-A` work.
5. **The Polling case does not exist, and 3.4 still said "U0 or training"**
   (3.4, 5.3, 13). Table 5-27's footnote asserts CCS for a USB3 port on the
   Polling to Enabled transition, so usbhub never sees a port in Polling;
   4.19.1.2.5 lets PR or WPR act from any state but Disconnected, Powered-off
   and Disabled and describes the hot-to-warm conversion with WRC set. The
   policy now keys on the Enabled, Error and Compliance Mode states, the
   reset row says what PRC and WRC report, and 3.4's table matches 5.3.
6. **Figure 4-26 is the USB2 diagram** (5.3, 6.1, 9, 13). In 1.2c the USB3
   Root Hub Port State Machine is Figure 4-27; every reference is corrected.
7. **WPR is not the exit from Disabled** (6.1). 4.19.1.2.5 excludes the
   Disabled state from reset entry; 4.19.1.2.1 exits it to Disconnected by a
   PLS = RxDetect write with LWS. The re-arm is that write, and the PP-cycle
   alternative with its VBus argument is dropped.
8. **The pairing convention is not a correctness basis** (6.1).
   `xhciPairCompanions` calls it a convention that costs nothing when wrong,
   which stops being true once a release depends on it. The release now
   requires a connect CSC on the companion after the hold began, so a
   mis-paired connector cannot release another connector's hold.
9. **A paired port whose companion never connects had no exit** (6.1). A
   device unplugged during the fallback leaves a hold with no companion
   disconnect to release it. It is now the unpaired case, held to the next
   start like an orphan and counted apart.
10. **Max ESIT Payload is periodic, not isochronous** (5.5, 6.3). The
    record's own transcription says "Periodic only", and 4.14.2 takes the
    SuperSpeed value from the companion descriptor's `wBytesPerInterval`.
    The interrupt bullet in 5.5 now specifies it and the `-0` row reads it.
11. **QEMU's `usb-hub` and `usb-audio` cannot fire either trigger** (8.1, 9).
    Both are Full-Speed models and QEMU attaches them to USB 2.0 ports
    whatever `p3` says, so the "first half" the `-V` row promised for 6.2 and
    6.3 was unobservable. The refusal mechanics are host vectors; a VM
    reading needs a synthetic trigger and is labelled as such.
12. **No held audio unit can fire the isochronous trigger** (8.2, 9). All six
    are Full or High Speed. A SuperSpeed isochronous specimen is a
    prerequisite of the `-E` row for 6.3, recorded as unobserved until held.
13. **`0781:55AB` is BOT only, and `bcdUSB` reads 0x0320** (5.4, 6.4, 8.2).
    The record had all three storage units as BOT plus UAS and listed only
    0x0300 and 0x0310 as what a device says; `test-equipment.md` has the
    per-device readings and they are now transcribed.
14. **`p3=0` is not on every committed launcher** (8.1). The matrix scripts
    and the XP and Windows 2000 xHCI-only generators pass it; the Windows 98,
    Windows 2000 and SMP generators default to a plain `qemu-xhci`. The
    record now says which, and the `-V` row names its launchers.
15. **"No Resume state on a SuperSpeed link" conflated two state machines**
    (5.3). Software resumes by writing U0 with LWS, but 4.19.1.2.13.3 defines
    a port Resume substate for a device-initiated wake; the row says both.
16. **The refusal rule is `XhciLogVerbosity`'s, not every value's** (3.4).
    `XhciLogDebugView` is applied as zero/non-zero and refuses nothing.
17. **Windows 8 does not present an xHCI as two root hubs** (4.2). The
    two-hub picture is an external USB 3.0 hub's two halves under one root
    hub; the comparison is removed.
18. **Section 10 missed two documents.** `win98-wdm.md`'s Option B paragraph
    and `architecture.md`'s component tree both state the unconditional rule;
    both are added.

Two findings were about other documents and were applied outside this record
in the same commit: `docs/contributing/roadmap.md`'s status paragraph still
said Phase 19 was open and named two cut releases where there are three, and
`docs/usb-xhci-info/win98-wdm.md`'s Option A paragraph still said the
package supplied `usbhub.sys` and a per-target `usbd.sys`, which
`src/xhci98.inf` and AGENTS.md contradict (the OS supplies both through
`LayoutFile`, and the media carries no Microsoft file).
