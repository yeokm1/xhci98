# 10 - SuperSpeed storage on root ports, behind a registry switch

Written 2026-09-04 at the project owner's request, after `1.0.1.0` was cut.
It is a **proposal**. Nothing in it has been built, no boot has been taken for
it, and it has no phase or task ids yet; where the text says what a batch
would do, it is naming work, not reporting it. The evidence it rests on is the
tree as it stands, the two ABI records (`docs/usb-xhci-info/usbport-miniport-abi.md`,
`docs/usb-xhci-info/usbport-miniport-interface.md`), and the device
characterisations in `docs/contributing/test-equipment.md`.

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
`xhciPowerPorts`, both from `XhciInitController`, both before the log values
are read today. The switch must be read **before the port map is classified**,
because the port map is the one place the two modes diverge (section 4). So
the read moves ahead of `xhciBuildPortMap`, or `xhciBuildPortMap` takes the
value as an argument; either way it is one PASSIVE read at one site, and the
restore path (which skips `XhciInitController`) reuses the value already in
the extension, since usbport does not zero the extension across a restore.

### 3.4 The second value: the reset policy

A SuperSpeed port has two resets where a USB 2.0 port has one (section 5.3),
and usbhub asks for "a reset" without knowing that. Which one the miniport
writes is policy, and the owner decided (2026-09-04) that the policy is a
registry value rather than a constant:

| | |
|---|---|
| Name | `XhciSuperSpeedReset` |
| Type | `REG_DWORD`, same key as `XhciSuperSpeed` |
| `0` | **Automatic**, the default: hot reset (PR) when the link is in U0 or training, warm reset (WPR) when it is in SS.Inactive or Compliance |
| `1` | **Always warm**: every usbhub reset on a SuperSpeed port is a WPR. Slower per enumeration, one path, always recovers the link |
| Other | Refused, not clamped: 0 applied and the refusal counted, the same rule as every other value this driver reads |
| Absent | 0 |

Three rules bound it:

- **It is read only when `XhciSuperSpeed` applied as 1.** With the main
  switch off the second value is never read, so the switch-off start path
  makes exactly the registry reads it makes today. This is what keeps section
  7's invariant a statement about one divergence point rather than two.
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
- The count is the number that actually confirmed powered, as it is today
  (`PortsPowered` is "the number Phase 5's root hub is built on"), and it is
  never zero (`src/xhci_rh.c`, "NumberOfPorts must never be zero"). No change
  to that guard.

A SuperSpeed device on a connector therefore appears on a *different* hub port
from a High-Speed device on the same connector. That is also how Windows 8 and
later present an xHCI (as two root hubs), and Device Manager on the targets
shows only "Port N" either way.

## 5. What changes per device when the switch is on

### 5.1 Speed decode

`xhciSpeedClassFromKilobits` already computes a class from the Supported
Protocol PSI table in kilobits (SuperSpeed's 5 Gb/s does not fit 32 bits of
b/s, and the code says so). It needs a `XHCI_SPEED_SUPER` class for the
5,000,000 kb/s row, and the default-PSIV table (`xhciDefaultPsiv`,
`xhciDefaultSpeedClass`) needs the SuperSpeed default ID, which both functions
today deliberately omit with a comment naming this record's subject. The
`XHCI_RH_SEEN_*` first-decode bits gain a SuperSpeed member so the "first
decode of a speed" print exists for it.

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
| reads status | CCS -> connected; PED -> enabled; connected -> High Speed | same, plus: link in U3 -> suspended. Connected -> High Speed **as today**, which is the lie section 2 described |
| `SET_FEATURE(PORT_RESET)` | write PR, report `C_PORT_RESET` on PRC | decided by `XhciSuperSpeedReset` (section 3.4). Automatic: link in U0 or training, write PR (a hot reset) and report `C_PORT_RESET` on PRC; link in SS.Inactive or Compliance, write WPR (a warm reset) and report `C_PORT_RESET` on WRC. Always-warm: WPR every time. Hot and warm counted separately |
| `SET_FEATURE(PORT_SUSPEND)` | PLS = U3 with LWS | same write; the resume differs (next row) |
| `CLEAR_FEATURE(PORT_SUSPEND)` | PLS = Resume, then U0 | PLS = U0 directly (no Resume state on a SuperSpeed link) |
| `CLEAR_FEATURE(PORT_ENABLE)` | PED = 0 | PED = 0. On a USB 3.x port this puts the link in SS.Disabled, and a SuperSpeed device whose SS link is disabled falls back to its USB 2.0 connection, which appears on the companion port. Section 6.1 uses this deliberately |
| `CLEAR_FEATURE(PORT_POWER)` | PP = 0 | same |
| port event with PLC only | acknowledged, no hub change unless leaving suspend | SS.Inactive: warm reset once, automatically; if the link does not reach U0, report as disconnected-then-connected so usbhub re-enumerates. CEC: same treatment |

Two facts drive the reset rows. A USB 3.x port enables itself when a device
attaches (link training sets PED without software writing PR), so usbhub's
reset arrives on an already-enabled port; a hot reset there is legal and cheap,
and it is what usbhub expects to see complete. And warm reset is the only
recovery from SS.Inactive and Compliance, states a USB 2.0 port cannot enter;
the `-0` batch reads the spec's port state machine for the exact conditions
and the record here is provisional on that reading. The existing rule that
**every** change bit is acknowledged, WRC and CEC included, already holds
(`src/xhci_port.c`, "Every change bit that was set is acknowledged"), so no
SuperSpeed port can stop reporting for want of an acknowledgement; what
changes is that WRC and CEC become inputs rather than noise.

### 5.4 Descriptor normalisation: the second half of the lie

The report layer tells usbport the *port* carries a High-Speed device. The
device's own descriptors then have to agree, because usbport and usbhub read
them literally:

| Field | SuperSpeed device says | usbport/usbhub would do | Proposed |
|---|---|---|---|
| `bMaxPacketSize0` (device descriptor byte 7) | 9, meaning 2^9 = 512 | reopen EP0 with `TotalMaxPacketSize` = 9 | **rewrite to 64** in the reply before completion |
| `bcdUSB` (bytes 2-3) | 0x0300 or 0x0310 | nothing measured; usbport patches its own root hub's to 0x0200 and reads a device's for nothing this project has found | **rewrite to 0x0200**, as cheap insurance and so a snapshot never carries a descriptor that contradicts the port's report |
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
  Speed (Table 6-12 has a SuperSpeed row; the `-0` batch reads it).
- Isochronous: **refused at the open** on a SuperSpeed slot (section 6.2).
- The TD Size arithmetic uses Max Packet Size; whether a burst changes it is a
  spec question (the TD Size definition in 4.11.2.4) for the `-0` batch, not a
  thing to assume either way.
- No Stream Context Arrays. UAS is never selected (section 6.3).

## 6. The refusals that keep the feature narrow

### 6.1 A hub on a SuperSpeed port

usbhub cannot drive a SuperSpeed hub (it asks for the USB 2.0 hub descriptor,
type 0x29, and a SuperSpeed hub answers with 0x2A). Let it try and it fails
messily and retries. So: when the device-descriptor reply on a SuperSpeed slot
carries `bDeviceClass` 9, the driver completes that transfer as a failure,
writes PED = 0 on the port (SS.Disabled), and holds the port reported as
**disconnected** until the next real CSC. Two things then happen on their own:

- The hub's USB 2.0 side connects on the companion port and is served as a
  High-Speed hub, exactly as it is today.
- The hub's SuperSpeed side is never configured, so its downstream SuperSpeed
  ports are never powered, so SuperSpeed devices behind it fall back to their
  USB 2.0 paths through the hub's High-Speed side. That is the outcome the
  current port strategy already delivers for every device; the refusal
  preserves it one tier down.

The USB 3.0 hub units this project holds (`05E3:0610` High-Speed halves,
`docs/contributing/test-equipment.md` row 4) are what this is measured on.

### 6.2 Isochronous endpoints

Refused at the open on a SuperSpeed slot, counted. Mult and Max ESIT Payload
are SuperSpeed-isochronous fields, usbport's per-packet stamps are a
High-Speed statement, and audio at SuperSpeed is not a target. A device that
offers both (a webcam, say) then fails to open its isochronous pipe and its
class driver reports that; the device is not made to fall back, because
nothing can make it: it trained at SuperSpeed and has no reason to drop.
The release notes name this.

### 6.3 UAS

`usbstor.sys` on every target selects alternate setting 0, which is BOT on
every unit this project holds (`test-equipment.md` rows 8 and 9, read at
SuperSpeed: BOT plus UAS, 1024-byte bulk endpoints). UAS needs a class driver
neither Windows 98 nor Windows 2000 nor XP has, so the driver never sees a
Stream request and needs no stream support. A UAS-only device, which exists
but is rare, does not work and the release notes say so.

### 6.4 Devices behind hubs

Unreachable by construction: no SuperSpeed hub is ever configured (6.1). A
SuperSpeed device behind a hub is a High-Speed device behind a High-Speed hub,
today's path.

## 7. The invariant: switch off is byte-identical, and how that is held

The claim is stronger than "the feature is disabled". It is that a build
carrying this feature, run with the value absent, produces the same port map,
the same power writes, the same hub-port numbering, the same refusals, and
the same counter readings as the release before it, on every controller.

What enforces it:

- **One divergence point.** The only code that consults the switch is the
  port-map classification (section 3.3), plus the read of the second value,
  which is skipped unless the first applied as 1 (section 3.4). Every other
  SuperSpeed path is reached through the `USB3_MANAGED` class or through a
  device record whose speed is `XHCI_SPEED_SUPER`, and neither can exist when
  the class is never assigned. A review rule for the batches: a
  `SuperSpeed.Enabled` test anywhere but the classifier and that one read is
  a defect, and `XhciSuperSpeedReset` is consulted only on a port of the
  `USB3_MANAGED` class.
- **Host tests on the classifier.** `test_caps.c` already carries port-map
  vectors; each gains a run with the switch off that must produce the exact
  table it produces today, and a run with it on that produces the new one.
  `test_port.c` gains the SuperSpeed translation rows of section 5.3 with the
  switch on, and a proof that a `USB3_COMPANION` port reaches none of them.
- **The matrix.** The Phase 10 VM matrix and the Phase 16 unattended run
  are run with the value absent and must read identically to the previous
  release's run, which is what the Phase 16 report header exists to make
  diffable. The `-V` batch adds a SuperSpeed leg beside it, never instead of
  it.
- **The header.** The mode is in every snapshot, so no reading can be
  misattributed to the wrong mode.

## 8. What can be observed where

### 8.1 Virtual machines

`qemu-xhci` is launched with `p3=0` on every guest today, and
`docs/contributing/build-and-test.md` records why: QEMU never falls a
SuperSpeed-capable device back to USB 2.0, so with SuperSpeed ports present
its `usb-storage` trains at SuperSpeed onto a port the driver does not serve.
That same property makes QEMU the SuperSpeed vehicle: a launcher with `p3=N`
and the switch on attaches `usb-storage` at SuperSpeed on all three NT guests
and the Windows 98 guest. What QEMU can and cannot show:

- Can: the whole enumeration and descriptor path, usbport's acceptance or
  refusal of 1024-byte bulk endpoints (measured, not derived, on NUSB, SP4
  and XP's usbport), the EP0 reopen being ignored, the hub refusal (with a
  `usb-hub` behind a SuperSpeed port; QEMU's hub is USB 1.1, so this measures
  the refusal mechanics and not a SuperSpeed hub), the switch-off leg.
- Cannot: link training, warm reset, SS.Inactive, Compliance, U3 on a real
  PHY, Max Packet Size enforcement on IN transfers (QEMU's xHC does not
  enforce it, `src/xhci.h` on `XHCI_EP0_MPS_FULL_INITIAL`), or throughput
  that means anything.

### 8.2 Bench

The devices are already held and characterised at SuperSpeed on a modern host
(`docs/contributing/test-equipment.md`): `090C:2320` and `0781:55AB` flash
drives and the `174C:5106` ASMedia bridge, all presenting BOT plus UAS with
1024-byte bulk endpoints on a SuperSpeed root port. The bench machine has to
expose a SuperSpeed root port on the xHCI the driver is installed on; the
E460 does, and the `test-equipment.md` rig positions already name a root
connector (position D). The one reading the acceptance test cannot skip is a
file round trip at position D with the switch on, on Windows 98 SE metal,
against the same round trip with the switch off (which is today's High-Speed
fallback and is already a recorded reading).

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
6. Whether a SuperSpeed device whose isochronous open is refused (6.2) leaves
   its bulk endpoints usable.

## 9. Work, as batches

On the roadmap's batching convention. A phase number is not assigned here;
the version it would ship in has a code change and so moves the third field
(`1.0.2.0` on the numbering Phase 19 states).

| Batch | Where confirmed | What |
|---|---|---|
| `-0` | Static | Spec transcription into `xhci-data-structures.md`: SuperSpeed PSIV default, Slot Context speed, PORTSC WPR/WRC/CEC and the PLS encodings for U0/U3/Inactive/Compliance, Endpoint Context Max Burst Size, TD Size against burst, Table 6-12's SuperSpeed row. Binary reads for 8.3 items 1-4, recorded the way the ABI record records its other findings, with addresses |
| `-A` | Host | The two values: read, refusal, the second read gated on the first, header fields, INF `AddReg` on both install paths with the comment block. The classifier, the new port class, and the all-SuperSpeed controller accepted under the switch. `test_caps` switch-off identity vectors and switch-on vectors |
| `-B` | Host | Speed class, default PSIV, `XhciInitialMps0`, Slot Context, EP0 reopen ignored, endpoint MPS/burst policy. `test_ctx` vectors |
| `-C` | Host | Descriptor rewrite in the completion drain, `bMaxBurst` in the descriptor walk, the hub refusal and the held-disconnected port state, the isochronous refusal. `test_desc` and `test_port` vectors |
| `-V` | VM | `p3=N` launchers for the four guests with the value set; storage enumerated and round-tripped at SuperSpeed on each; the hub-refusal mechanics; the switch-off matrix identical to `1.0.1.0`'s |
| `-E` (or the machine's initial) | Bench | Position D round trip, switch on and off, Windows 98 SE; warm reset and SS.Inactive observed if they occur; counters read through `XHCISNAP` |

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
- `README.md` and `docs/README.md`: one line each.

## 11. What this does not do

Option B's list, restated as what stays out with the switch on:

- No SuperSpeed hubs, and no SuperSpeed devices behind hubs.
- No UAS, no streams.
- No SuperSpeed isochronous, so no USB Audio or video at SuperSpeed.
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
   (bulk and interrupt allowed, isochronous refused) and not on interface
   class. A class filter would need the interface descriptor, would buy
   nothing the isochronous refusal does not, and would exclude a SuperSpeed
   Ethernet adapter that otherwise works for free. Mass storage is the
   validation target and the release-notes claim, not the code's filter.
5. **The reset policy is a second registry value**, `XhciSuperSpeedReset`,
   automatic by default and always-warm on request, read only when the main
   switch applied (section 3.4). The `-0` batch still reads the port state
   machine before the automatic policy's conditions are written into code;
   the value chooses between policies, it does not replace the reading.
