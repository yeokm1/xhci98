# xHCI Hardware Programming Summary

This document supplements the xHCI spec PDF (`docs/references/xHCI__Rev1.2c.pdf`, revision 1.2c). Read the spec for authoritative detail; use this doc for orientation and for driver-specific decisions. For exact bit positions, TRB/context field layouts, type codes, and alignment rules (everything needed to write `src/xhci.h`), use the companion `docs/usb-xhci-info/xhci-data-structures.md` - its tables were transcribed and verified against the spec PDF.

## Register Map (BAR0 MMIO)

```
BAR0 base
|-- Capability Registers (offset 0x00, length = CAPLENGTH)
|   |-- CAPLENGTH   [0x00, 1 byte]  - offset to Operational Registers
|   |-- HCIVERSION  [0x02, 2 bytes] - xHCI spec version (e.g., 0x0110 = 1.1)
|   |-- HCSPARAMS1  [0x04]          - number of slots, ports, interrupters
|   |-- HCSPARAMS2  [0x08]          - max scratchpad buffers, etc.
|   |-- HCSPARAMS3  [0x0C]          - exit latencies
|   |-- HCCPARAMS1  [0x10]          - capability flags (64-bit, port power, etc.)
|   |-- DBOFF       [0x14]          - doorbell array offset from BAR0
|   `-- RTSOFF      [0x18]          - runtime register set offset from BAR0
|
|-- Operational Registers (base + CAPLENGTH)
|   |-- USBCMD      [+0x00]         - RUN/STOP, HCRST, INTE, HSEE
|   |-- USBSTS      [+0x04]         - HCH, HSE, EINT, PCD, SRE, CNR, HCE
|   |-- PAGESIZE    [+0x08]         - supported page sizes
|   |-- DNCTRL      [+0x14]         - device notification control
|   |-- CRCR        [+0x18, 8 bytes]- command ring control (physical addr + RCS)
|   |-- DCBAAP      [+0x30, 8 bytes]- device context base address array pointer
|   |-- CONFIG      [+0x38]         - MaxSlotsEn
|   `-- PORTSC      [+0x400 + 0x10*(n-1)] - per-port status/control, port n is 1-based
|
|-- Runtime Registers (base + RTSOFF)
|   |-- MFINDEX     [+0x00]         - microframe index
|   `-- IR[n]       [+0x20 + 0x20*n]- interrupter registers
|       |-- IMAN    - interrupt management (IP, IE bits)
|       |-- IMOD    - interrupt moderation
|       |-- ERSTSZ  - event ring segment table size
|       |-- ERSTBA  - event ring segment table base address (8 bytes)
|       `-- ERDP    - event ring dequeue pointer (8 bytes)
|
`-- Doorbell Array (base + DBOFF)
    |-- DB[0]       - host controller doorbell (for command ring)
    `-- DB[n]       - device slot n doorbell (target in bits 7:0, stream ID in upper bits)
```

## Initialization Sequence

Follow spec section 4.2. Steps in order.

Before step 0, and before any MMIO at all, read the PCI Interrupt Pin register
(config 0x3D) and refuse a controller that reports 0. Step 13 below explains
why INTx is the only delivery path on either target; what matters here is the
position of the check. It needs no register access and its answer is final, so
running it first refuses an unusable controller without having taken it from
its firmware, halted it and reset it. The same refusal made seven steps later
leaves a machine whose xHCI is claimed and dead. A config read that fails is
not the same statement as a pin of 0: log it and continue, since PCI derives a
device's interrupt resource from that register and `usbport.sys` would not have
reported an interrupt resource for a controller without one.

Read PCI Command (config 0x04) in the same breath and require Bus Master
Enable. A controller without it does no DMA at all (no command completions, no
events, no transfers), so it would report started and then be
indistinguishable from a controller whose interrupts never route. Check it on
every start rather than only when the driver believes it cleared the bit
itself; BME can be clear for reasons the driver had no part in.

Refuse rather than repair unless the driver's own quiesce cleared it: configuration space
belongs to the bus driver, and the one exception is undoing your own act. A
Command register that cannot be read refuses as well. Unlike the Interrupt Pin,
no resource bit independently witnesses bus mastering, so an unreadable
register leaves you knowing nothing about the bit in question (see
`docs/contributing/implementation-invariants.md`, "Starting and Stopping the
Controller").

0. MMIO sanity before side effects: immediately after BAR0 is mapped, read
   `CAPLENGTH` and `HCIVERSION`. Abort on all-ones reads, zero CAPLENGTH, or an
   implausible version before touching operational registers. All-ones means
   the device is not decoding the access (Memory Space Enable clear, D3, bad
   BAR mapping, or removed device).
1. BIOS handoff: walk the xECP chain for the Legacy Support Capability
   (ID = 0x01). Set bit 24 (OS Owned Semaphore). Poll until bit 16 (BIOS Owned
   Semaphore) clears, with ~1 s timeout. Disable all SMI enables in DW1. See
   "BIOS handoff - required on all controllers" at the end of this document for
   the full procedure. Do this before any operational register access beyond
   the initial capability-register sanity reads.
2. Halt the controller: clear USBCMD.RUN. Poll USBSTS.HCH = 1.
3. Reset: set USBCMD.HCRST = 1. Poll USBCMD.HCRST = 0 and USBSTS.CNR = 0.
4. Check HCIVERSION: must be >= 0x0100. Bail if not. In practice re-derive
   the whole capability set here and compare it field for field with the
   pre-handoff decode. The question this step exists to answer is whether the
   far end of the mapping is still the same controller, and six extra reads
   answer it completely instead of partially.

   Follow it with the other half of the same question: re-parse the
   extended-capability chain and overwrite the preflight port map with the
   result. The registers cannot show a list that changed underneath an `xECP`
   value that did not, and everything downstream (port power, the root-hub port
   count, Slot Type) reads that map, so it must describe the controller as it
   is now rather than as it was before the reset. Re-run every refusal against
   the new parse, require USBLEGSUP at the offset the handoff used, and require
   the parse to agree with the preflight one: Supported Protocol capabilities
   are read-only hardware description and HCRST does not change them, so a
   difference is an anomaly, not an update.

   The two refusals that re-parse can raise belong to the driver rather than to
   the parser, and `src/xhci_init.c` codes them in the same `XHCI_CAPS_*` space
   so `InitStatus` has one meaning per value at its step: `XHCI_CAPS_LEGACY_MOVED`
   (the capability this driver wrote to is not where it wrote) and
   `XHCI_CAPS_TOPOLOGY_CHANGED` (the topology it is about to act on is not the
   one it validated). The third driver-raised code in that space,
   `XHCI_CAPS_NO_MANAGED_PORTS`, is the preflight's (step 12 below), where the
   port map is `XHCI_INIT_STEP_PORT_MAP` = 6.

   Compare the two field for field, and keep both maps in the miniport
   extension so exactness costs no stack. A digest is the wrong tool here: this
   comparison is the evidence the maps agree, so accepting on equal hashes is a
   bet against a collision, and for a word-at-a-time FNV fold the collisions
   are constructible, not rare (each step is invertible, so changing one PSI
   DWORD and solving for the next reproduces the state).
5. Read capabilities:
   - `HCSPARAMS1` -> max ports, slots, interrupters
   - `HCSPARAMS2` -> max scratchpad buffer pages (SP_MAX field)
   - `HCCPARAMS1` -> CSZ (context size: 32 or 64 bytes), AC64 (64-bit addressing)
   - For our driver: always use 32-bit mode regardless of AC64.
   - Set `ContextSize` to 64 bytes when CSZ = 1, otherwise 32 bytes. This stride applies to every slot, endpoint, input-control, and device context. It is independent of 32-bit DMA addressing.
6. Set MaxSlotsEn: write `CONFIG.MaxSlotsEn` = `min(HCSPARAMS1.MaxSlots, XHCI_MAX_SLOTS)`, where the fixed common-buffer policy currently sets `XHCI_MAX_SLOTS` to 32. Writing a value greater than the hardware's `MaxSlots` is undefined; enabling more than the declared cap would let the controller return Slot IDs for which the driver reserved no Device Context or EP0 ring. CONFIG bits `31:10` are RsvdP (Table 5-26, p.370), so this is a read-modify-write and not `CONFIG = MaxSlotsEn`.
7. Set up the Device Context Base Address Array (DCBAA):
   - Carve and zero `(MaxSlotsEn + 1)` entries x 8 bytes from the fixed controller common buffer (33 entries at the declared cap; upper 32 bits always 0 in our 32-bit driver). The region reserves the spec maximum for stable offsets, but only the enabled entries are used.
   - Entry 0 points to the scratchpad buffer array. Entries 1 through `MaxSlotsEn` point to device contexts.
   - Write physical address to `DCBAAP`.
8. Set up scratchpad buffers (if SP_MAX > 0). Nothing is allocated here: under Option A everything is carved from the fixed common buffer usbport already supplied (`docs/contributing/design/04-controller-common-buffer.md`).
   - Refuse the controller first if `SP_MAX > XHCI_MAX_SCRATCHPAD` (currently 64): a short array is a controller that DMAs into memory the driver does not own. Return `MP_STATUS_NO_RESOURCES` naming the required count.
   - Carve the array of SP_MAX physical addresses (8 bytes each) and the SP_MAX buffer pages, each page-aligned. Buffer size is the selected xHC page size, so also refuse a controller whose `PAGESIZE` does not advertise 4 KB (bit 0); it is a capability bitmap, not a byte count.
   - Set array entries to page physical addresses. Set DCBAA[0] to array physical address.
9. Set up the Command Ring:
   - Carve and zero the fixed 64-TRB, power-of-two ring from the controller common buffer.
   - Last TRB must be a Link TRB pointing back to start (TC bit set).
   - Write physical address | RCS (ring cycle state = 1) to `CRCR`, preserving
     bits `5:4`, which are RsvdP (Table 5-24, p.367). A composed
     `base | RCS` clears them; that HCRST has just run is not a licence, for the
     same reason it is not one for USBCMD.
10. Set up the Event Ring. Register order matters (spec 4.9.4): ERSTSZ and ERDP
   must be programmed before ERSTBA, because the ERSTBA write starts the
   Event Ring State Machine and latches the table.
   - Carve and zero the fixed 256-TRB event-ring segment from the controller common buffer.
   - Initialize the one reserved ERST entry with the segment physical address and size.
   - Write 1 to `IR[0].ERSTSZ`, preserving bits `31:16` (RsvdP, Table 5-40 p.393).
   - Write event ring segment physical address (with EHB=0, DESI=0) to `IR[0].ERDP`. ERDP has no reserved field, so this one is a plain write.
   - Write ERST physical address to `IR[0].ERSTBA` last (this write enables the event ring), preserving bits `5:0` (RsvdP, Table 5-41 p.394).
11. Prepare interrupter: leave `IR[0].IMAN.IE` and `USBCMD.INTE` masked until the OS interrupt object is connected.
12. Parse Extended Capabilities: walk the xECP list starting at `HCCPARAMS1.xECP` to find USB Protocol Capability structures. Classify each logical port into USB2-only / USB2-companion / USB3-companion / USB3-orphan. See "Port Topology Classification" below.

    This parse runs twice, and neither time is here. The first parse goes after
    step 5's capability decode and before step 1: it reads the capability chain
    and nothing else, and its refusals are final, so running it there declines
    a controller this driver cannot serve while that controller is still as its
    firmware left it (not claimed, not halted, not reset). Same argument as the
    INTx gate above, and together the two make everything ahead of the handoff
    a preflight that performs no write of any kind, which `test_init` checks
    refusal by refusal (`docs/contributing/design/03-host-unit-tests.md`). The
    second parse is step 4's, described above, and it is the authoritative one:
    the preflight map is a pre-reset reading and nothing downstream may be left
    holding it.

    Every nonzero parser status refuses, including "no capability list".
    Unlike the Legacy Support capability, whose absence just means there is no
    firmware to take the controller from, an absent Supported Protocol
    capability means the controller has not said which of its ports carry
    USB 2.0, and guessing would mean powering SuperSpeed ports. Refuse a
    controller with zero managed USB 2.0 ports for the same reason plus one
    more: `usbport.sys` sizes its root-hub removable/power masks from the
    reported port count and asks for roughly 1 GB of nonpaged pool at zero
    (`docs/usb-xhci-info/usbport-miniport-abi.md` section 9).
13. Connect the interrupt: under Option A, `usbport.sys` owns the interrupt object and the miniport never calls `IoConnectInterrupt`. Ensure the miniport ISR/DPC callbacks are registered with `usbport.sys` before unmasking controller interrupts. (Under the Option B fallback, `IRP_MN_START_DEVICE` calls `IoConnectInterrupt` with the translated interrupt resource first.)
14. Enable interrupts: set `IR[0].IMAN.IE` = 1, then set `USBCMD.INTE` = 1.

    Under Option A this is not part of the start sequence at all, and its real
    position is after step 16: `usbport.sys` calls the miniport's
    `EnableInterrupts` once `StartController` has returned success, and that
    callback is the only place these two bits are set. The specification's own
    list puts INTE before IE (4.2, p.70); the order above is the mirror of the
    mask, and neither can deliver an interrupt until both are set.

    Release Event Handler Busy first. By the time this runs, steps 15 and 16
    have made the controller report the devices already attached, so the
    interrupter is normally holding `IMAN.IP` = 1, `USBSTS.EINT` = 1 and
    `ERDP.EHB` = 1 with the enables still masked. Write `ERDP` with EHB = 1 at
    the unmoved dequeue pointer before setting either enable; it consumes no
    event, and without it the held interrupt is suppressed. Acknowledge nothing
    here: clearing IP while EHB is set makes IP unsettable for good (4.17.5,
    p.270), and clearing EINT while IP stays set makes the ISR decline its own
    controller's interrupt on a line that "remains asserted until the device
    driver clears the Interrupt Pending (IP) flag" (4.17.3, p.268). See
    `docs/contributing/implementation-invariants.md`, "Interrupt Ordering".
15. Start controller: set `USBCMD.RUN` = 1. Poll `USBSTS.HCH` = 0.

    Check `USBSTS.HCH` = 1 first: "software shall not write a `1` to this flag
    unless the xHC is in the Halted state ... doing so may yield undefined
    results" (5.4.1, p.359). Write the R/S bit alone rather than ORing it into
    the read value: HCRST reset every other field of USBCMD, and two of them
    (INTE, HSEE) are the ones that must stay clear here. The poll is not a
    timing allowance. HCH "is a `0` whenever the Run/Stop (R/S) bit is a `1`"
    (5.4.2, p.363), so it answers "did the controller take the write" and
    answers it at once. Record that R/S was written before writing it, so a
    start that then fails its confirmation still knows it has a controller to
    stop.
16. Power up ports: for each USB2 port managed by this driver, ensure `PORTSC.PP` = 1, at either value of HCCPARAMS1.PPC, which says only whether the controller has power switches: "software cannot change the state of the port unless Port Power (PP) is asserted (`1`), regardless of the Port Power Control (PPC) capability" (5.4.8, p.371). PPC gates the 20 ms settle delay, not the write.

    This step is after step 15 and cannot be moved: "software shall ensure that
    the xHC is running (HCHalted (HCH) = `0`) before attempting to write to this
    register" (5.4.8, p.371). It is also where the connects appear. A port with
    a device already attached asserts PSCEG as HCH transitions to `0`,
    "generating a respective Port Status Change Event" (4.19.4, p.296).

    It must also take power off the USB 3.x ports. PP defaults to asserted on
    every port after HCRST (4.19.4, p.295), so the port strategy's "leave
    USB 3.x ports unpowered" is a write, not an omission. See
    `docs/contributing/implementation-invariants.md`, "PORTSC Writes", for that
    rule, the ordering it imposes (assert before deassert, because VBus is the
    OR of a connector's two ports), the 20 ms power-stable delay a `0` to `1`
    transition owes, and the read-back that confirms it.

## Extended Capability: USB Protocol

Walk from `HCCPARAMS1.xECP` (units of DWORDs from BAR0). Each capability has:
- Byte 0: ID (0x02 = USB Protocol)
- Byte 1: NEXT (DWORD offset to next cap, 0 = end)
- Byte 2: Minor version
- Byte 3: Major version (2 = USB 2.0, 3 = USB 3.x)
- DWORD 1: Name string (4 ASCII bytes, "USB ")
- DWORD 2 bits[7:0]:   CompatiblePortOffset (1-based index of first port in this group)
- DWORD 2 bits[15:8]:  CompatiblePortCount  (number of contiguous ports in this group)
- DWORD 2 bits[27:16]: Protocol Defined
- DWORD 2 bits[31:28]: Protocol Speed ID Count (PSIC, xHCI 1.1+)
- DWORD 3 bits[4:0]:   Protocol Slot Type (the Enable Slot command's Slot Type)
- DWORD 4.. :          PSIC x Protocol Speed ID dwords (present only if PSIC > 0)

PSIC > 0 means this capability defines its own speed IDs, and the default
`1 = FS, 2 = LS, 3 = HS, 4 = SS` mapping is then an assumption rather than a
fact: a PORTSC Port Speed value must be looked up in that table (PSIV `3:0`,
PSIE `5:4` as b/s/Kb/s/Mb/s/Gb/s, PSIM `31:16`; spec 7.2.2.1.2). Both fleet
Intel machines advertise a table (PSIC 3 on the USB2 capability), so this path
is live on real hardware. The defaults happen to agree there, so a wrong
assumption would go unnoticed.

Parse all capabilities first, then classify ports. A typical Intel 7-series controller example:
```
USB 2.0 Protocol: ports 1-14  (10 USB-2.0-only + 4 companion USB 2.0 paths)
USB 3.0 Protocol: ports 15-18 (4 companion USB 3.0 paths for the same physical connectors)
```

## Port Topology Classification

After parsing all USB Protocol Capability structures, classify every logical port number:

```
port_usb2[i] = TRUE if logical port i appears in any USB 2.0 capability range
port_usb3[i] = TRUE if logical port i appears in any USB 3.x capability range
```

Then for each physical connector, find its logical port pair. The pairing convention used by most Intel/AMD XHCI controllers: the n-th USB 3.x port (within the USB 3.x capability range) corresponds to the last-N USB 2.0 ports (within the USB 2.0 capability range), where N = USB 3.x PortCount. Concretely:

```
USB 2.0 capability: ports A .. A+P-1     (P total USB 2.0 logical ports)
USB 3.x capability: ports B .. B+Q-1     (Q total USB 3.x logical ports)

Companion mapping (P >= Q):
  USB 2.0 port (A + P - Q + k)  <->  USB 3.x port (B + k)   for k = 0 .. Q-1
  USB 2.0 ports A .. A+P-Q-1 have no USB 3.x companion (USB-2.0-only physical ports)
```

This mapping is a convention, not guaranteed by the spec. Verify against the chipset datasheet or ACPI _UPC/_PLD objects for precise physical-to-logical mapping if needed.

Port categories after classification:

| Category | Condition | Action |
|---|---|---|
| USB 2.0-only | `port_usb2[i]` and no USB 3.x companion | Manage: power on, handle connects |
| USB 2.0 companion | `port_usb2[i]` with a USB 3.x companion | Manage: power on, handle connects |
| USB 3.x companion | `port_usb3[i]` with a USB 2.0 companion | Leave unmanaged: do NOT power on |
| USB 3.x orphan | `port_usb3[i]` and no USB 2.0 companion | Out of scope - would require SuperSpeed; left unpowered/unmanaged |

Signaling note: a USB 3.x logical port uses SuperSpeed electrical signaling (SSTX/SSRX differential pairs). A USB 2.0 logical port uses USB 2.0 signaling (D+/D- differential pairs). These are completely different physical paths in the cable and connector. There is no mechanism to make a USB 3.x logical port communicate at USB 2.0 speeds; the SS path simply cannot carry USB 2.0 signals. The USB 2.0 companion port is the correct path for USB 2.0 and USB 2.0-fallback operation.

USB 3.x capable devices on USB 2.0 companion ports: if the USB 3.x logical companion port is left unpowered (PORTSC.PP = 0), a USB 3.x capable device plugged into that physical connector will fail SS link training on the SS path and fall back to its D+/D- USB 2.0 path, connecting to our managed USB 2.0 companion port at High-Speed. This is the intended behavior (USB 3.0 is out of scope; HS fallback is the supported path).

## What SuperSpeed Support Would Require

SuperSpeed is not an extension of the USB 2.0 miniport path. The reused
Win2000-era `usbport.sys` has no SuperSpeed speed reporting, bandwidth model,
root-hub semantics, or USB 3.x hub support. A SuperSpeed implementation would
first have to replace Option A with the Option B monolithic HCD described in
`docs/usb-xhci-info/win98-wdm.md`, taking ownership of the root-hub PDO,
`IOCTL_INTERNAL_USB_*`, URB parsing, enumeration, and scheduling.

Only after that USB 2.0 replacement worked would the xHCI layer gain the
SuperSpeed-specific paths:

- Power USB 3.x ports and implement link-state transitions, U0/U1/U2/U3,
  warm reset, link training, and compliance-mode recovery.
- Parse BOS, SuperSpeed Device Capability, and SuperSpeed Endpoint Companion
  descriptors; use the 512-byte EP0 maximum packet size.
- Program Max Burst, Mult, and Max ESIT Payload, account for burst transfers,
  and add Stream Context Arrays if UAS bulk streams are supported.
- Implement USB 3.x hub descriptors and port state. SuperSpeed hubs have no
  transaction translators, but still require Route Strings; the NT5 hub
  drivers cannot provide this path, so a USB 3.x-aware hub driver would also
  be required unless support stopped at root-port devices.
- Add a SuperSpeed bandwidth model and validate link training, warm reset,
  U-state transitions, hubs, storage, Ethernet, and audio on real controllers.

This is a separate driver-stack project with little practical benefit on the
target operating systems; High-Speed already covers the intended HID, storage,
Ethernet, and audio workloads. A controller exposing only USB 3.x protocol
ports is therefore refused at start (`XHCI_CAPS_NO_MANAGED_PORTS`) rather than
driven.

That refusal is a per-controller condition, not a per-connector one. USB4
tunnels USB 3.x, DisplayPort and PCIe but not USB 2.0: a USB4/Thunderbolt
connector's D+/D- wires are not tunneled and still terminate at a USB 2.0
protocol port of some xHCI on the machine. What such a machine does change is
which xHCI. The USB 2.0 side of a Type-C connector is commonly a port of a
different PCI function from the one carrying that connector's SuperSpeed side,
so an all-SuperSpeed controller means the driver belongs on a different
controller, not that the connector can never be served.

## Command Ring Operation

The command ring is a circular ring of 16-byte TRBs (Transfer Request Blocks). The last TRB is a Link TRB that wraps back to the start.

Enqueue a command:
1. Write TRB fields (spec table 6-x for the specific command type).
2. Set the Cycle Bit (C) to the current producer cycle state (PCS).
3. Advance enqueue pointer. If it lands on a Link TRB: toggle PCS, advance past it.
4. Ring doorbell `DB[0]` with value 0.

Completion: the hardware writes a Command Completion Event TRB to the event ring. Match by comparing the event's Command TRB Pointer field to the physical address of the submitted command TRB.

Key commands needed:
- `Enable Slot` (TRB type 9) - returns a Slot ID
- `Disable Slot` (TRB type 10)
- `Address Device` (TRB type 11) - with Input Context pointer, BSR flag
- `Configure Endpoint` (TRB type 12) - with Input Context pointer
- `Evaluate Context` (TRB type 13) - update device context without full configure. It is not a general-purpose context update: flagging the Slot Context evaluates the Interrupter Target and Max Exit Latency and nothing else (6.2.2.3 p.412), and flagging EP0 evaluates its Max Packet Size. Every other Slot Context field needs a Configure Endpoint; see `docs/usb-xhci-info/xhci-data-structures.md`, "Which command may set which Slot Context field"
- `Reset Endpoint` (TRB type 14) - after stall/error
- `Stop Endpoint` (TRB type 15)
- `Set TR Dequeue Pointer` (TRB type 16) - after reset endpoint
- `No Op Command` (TRB type 23) - for testing

SET_ADDRESS is a command, not a transfer. The xHC assigns USB addresses itself via `Address Device` (BSR = 0): it sends SET_ADDRESS on the bus with an address it chooses. Software must never encode SET_ADDRESS as a Setup TRB on a transfer ring. The xHC blocks it: no Setup transaction goes on the bus and the TRB completes with a TRB Error (spec section 4.5.4.1; the Address Device command itself is spec section 4.6.5). This matters under Option A because `usbport.sys` sends SET_ADDRESS as an ordinary EP0 control transfer: the miniport intercepts that setup packet and emulates it with Address Device (see `docs/contributing/architecture.md`, "Data Flow: Device Enumeration").

## Command Ring Discipline, Timeout, and Abort

Driver policy, built on spec sections 4.6.1.1 (Stopping the Command Ring) and
4.6.1.2 (Aborting a Command), both verified against the spec PDF.

One outstanding command at a time. The usbport callbacks that need commands
run at DISPATCH_LEVEL under usbport's lock and cannot block
(`docs/usb-xhci-info/usbport-miniport-abi.md` section 7), so command issue is
asynchronous: enqueue the TRB, ring `DB[0]`, return; service the Command
Completion Event from the DPC. Serializing to one in-flight command keeps
completion matching trivial (still verify the event's Command TRB Pointer
against the issued TRB's physical address) and makes command-ring-full
unreachable with a 64-TRB ring.

Time every command. The spec's own implementation note (4.6.1.2) says
software should time all xHCI commands; ~5 seconds is its "assume larger
problems" threshold. Under Option A the sanctioned timer is
`UsbPortRequestAsyncCallback` (no private DPCs/threads). It is uncancellable
and its callback runs without usbport's miniport locks: keep the armed/current
command generation in miniport-owned state, pass the issued generation in the
copied timer context, and let completion/timeout atomically claim only the
matching generation. Ignore stale and post-stop callbacks before touching
MMIO. Address Device is the canonical command that can legitimately hang. It
waits for the device to accept SET_ADDRESS on the bus, which is outside the
xHC's control (spec 4.6.1.2 implementation note).

Timeout recovery ladder, in order; stop at the first rung that works. As
implemented in `src/xhci_cmd.c`:

1. Abort: write CRCR.CA = 1 (RW1S), from a read, and only while CRR = 1.

   RCS, CS, CA and the pointer field all read back as `0` (Table 5-24,
   p.367-368), which is what makes a bare `CRCR = CA` literal look complete.
   That enumeration is accurate and it stops one field short, in a way that
   costs twice:

   - CRCR bits `5:4` are RsvdP (p.367), and RsvdP means "software shall
     preserve the value read for writes to bits" (p.338). They are not in the
     read-as-zero list, and they are the only bits of this register a read can
     carry anything in. So the operand is `(read & 0x30) | CA`.
   - The pointer zeros are not always ignored. "Writes to this field are
     ignored when Command Ring Running (CRR) = `1`" (p.368) is true, and CRR = 1
     is the only state in which CA does anything (p.367), but the two halves of
     this register are ignored in opposite states. On a stopped ring, "If
     the CRCR is written while the Command Ring is stopped (CRR = `0`), the value
     of this field shall be used to fetch the first Command TRB the next time the
     Host Controller Doorbell register is written" (p.368). So a `CA` write
     composed with a zero pointer, on a ring that has already stopped, repoints
     the command ring at physical address 0 while achieving nothing, since CA
     is ignored there.

     The read that supplies the RsvdP operand also supplies CRR, so the write
     is simply not made when CRR = 0. That loses nothing and
     the state machine is unchanged: the engine still moves to its aborting
     state and rung 2 below still bounds it.

   Only the low DWORD is written; the high half is pointer bits.
   Expected events: a Command Completion Event with code 25 (Command Aborted)
   for the stuck command (this event may be absent if the xHC happened to
   be between commands, 4.6.1.2 p.93), then a Command Completion Event with
   code 24 (Command Ring Stopped) whose TRB Pointer holds the current dequeue
   pointer. Complete the stuck operation as failed toward usbport.
2. Adopt the reported dequeue pointer and restart. The abort has already
   advanced it past the aborted command ("Advance the Command Ring Dequeue
   Pointer to point to the next Command TRB", 4.6.1.2 p.93), so the aborted
   TRB is not re-executed and software's job is only to move its own dequeue
   pointer to match. Nothing else is needed ("Command Ring execution shall
   restart at the current Dequeue Pointer value", 4.6.1.1 p.93), and the next
   command's `DB[0]` write is the restart. The doorbell must not be rung
   before the Command Ring Stopped event arrives: "if the Command doorbell is
   rung before CRR = `0`, (i.e. the ring is not fully stopped), then the
   behavior is undefined, e.g. the Command Ring may not restart" (p.368).

   Do not try to reposition CRCR when the reported pointer is one the
   software ring cannot hold. It is legal to rewrite CRCR here (the ring is
   stopped) and it does not help: the Command Ring Pointer is bits 63:6, "so the
   low order 6 bits of the Command Ring Pointer shall always be `0`" (p.368), so
   only every fourth TRB of a 16-byte-TRB ring is an addressable restart
   position and an arbitrary enqueue pointer is not one; and restarting at the
   ring's base instead would run over TRBs from earlier laps whose Cycle Bits
   still read as produced. A pointer the ring cannot hold means the software and
   hardware ideas of the ring have diverged. Go straight to rung 3.
3. Reset: if CRR does not clear within ~5 s of setting CA, the spec's
   guidance is HCRST and full reinitialization (4.6.1.2 note, p.94). Same
   answer for a divergence at rung 2, and for a ring that negates CRR but never
   posts its Command Ring Stopped event within a bounded number of further
   intervals. Under Option A that means calling
   `UsbPortInvalidateController(ext, USBPORT_INVALIDATE_CONTROLLER_RESET)`
   with the miniport's interior lock released.

   The reinitialization cannot happen in the `ResetController` callback:
   usbport dispatches it from a DPC holding one of its own spin locks, so the
   callback runs at DISPATCH_LEVEL where the init sequence's waits are illegal
   (`docs/usb-xhci-info/usbport-miniport-abi.md`). The engine therefore stays
   out of service after this rung; recovery is a stop/start. That is the honest
   terminal state for a command ring that will not report itself stopped, and
   it is the reason the ladder has no fourth rung.

CRCR handling reminders (`docs/usb-xhci-info/xhci-data-structures.md` section 3): write the
pointer field only while CRR = 0, and keep a software copy of the ring pointer;
the pointer bits read back as 0.

The No Op Command is this driver's start-up self-test. It "can be issued by
software to exercise the TRB Ring mechanism of the xHC without affecting any xHC
or USB Device state" (4.6.2, p.94), so the last step of `StartController` issues
one and lets the ordinary interrupt path complete it. That is the only thing
short of a real transfer that exercises the command ring, `DB[0]`, the event
ring, the ISR and the DPC as one path, and it is what the Phase 4 checkpoint
reads. Its completion cannot gate the start, since interrupts are still masked
at that point and usbport enables them afterwards, so the submit's status and
the matched TRB pointer are recorded in the miniport extension where a release
build can report them.

## Fatal Controller Errors (HCE, HSE)

Verified against the spec PDF (USBSTS definitions and spec 4.24.1 note):

- USBSTS.HCE (bit 12, RO): internal xHC error; the controller ceases all
  activity. The only recovery is HCRST plus full reinitialization. The spec
  explicitly tells software to "implement an algorithm for checking the HCE
  flag if the xHC is not responding"; the natural place is the
  `CheckController` callback, which usbport already invokes periodically from
  its worker thread and timer (`docs/usb-xhci-info/usbport-miniport-abi.md` section 4).
- USBSTS.HSE (bit 2, RW1C): serious system-level error (PCI parity error,
  Master Abort, Target Abort). The xHC clears RUN itself. The spec says both
  the xHC and software must assume system integrity is compromised. Treat it
  as fatal: log loudly, stop submitting work, report controller failure.
- On either flag: stop issuing commands/transfers, complete pending work as
  failed, and request recovery via
  `UsbPortInvalidateController(USBPORT_INVALIDATE_CONTROLLER_RESET)`; do the
  actual halt/HCRST/reinit inside the `ResetController` callback.
- Leave `USBCMD.HSEE` = 0 (policy): HSEE turns HSE into out-of-band error
  signaling (SERR#), which neither target handles usefully (Win98 not at all;
  Win2000 would surface it as an unhelpful machine-check-flavoured bugcheck
  rather than a diagnosable driver error).

## Event Ring Operation

The event ring is written by hardware. The driver reads from it.

Dequeue an event:
1. Read TRB at `ERDP` (current dequeue pointer).
2. If TRB Cycle Bit != Consumer Cycle State (CCS), ring is empty - stop.
3. Process event based on TRB Type field.
4. Advance dequeue pointer. If it reaches end of segment, wrap to start (and advance ERST segment index if using multiple segments - with 1 segment, just wrap).
5. During a long burst, publish dequeue progress to `IR[0].ERDP`
   periodically with EHB left set - EHB is RW1C, so write bit 3 as 0 to
   preserve it; writing 1 would clear it mid-drain. After the Cycle Bit
   indicates empty, write the final dequeue pointer with `EHB` = 1 to clear
   Event Handler Busy.
6. Toggle CCS when dequeue pointer wraps.

Event types:
- Type 32: Transfer Event - URB completion
- Type 33: Command Completion Event - command done
- Type 34: Port Status Change Event - port state changed
- Type 35: Bandwidth Request Event
- Type 36: Doorbell Event
- Type 37: Host Controller Event
- Type 38: Device Notification Event
- Type 39: MFINDEX Wrap Event

## Transfer Ring Operation

Each active endpoint has its own transfer ring (same circular TRB structure as command ring).

Enqueue a transfer:
1. Build TRB chain:
   - Control: Setup TRB (type 2) + Data TRB (type 3, if data stage) + Status TRB (type 4)
   - Bulk/Interrupt: one or more Normal TRBs (type 1), chained with CH bit if spanning multiple
   - Isochronous: Isoch TRB (type 5)
   - A single TRB's data buffer must not span a 64 KB physical boundary; split buffer fragments at 64 KB boundaries into chained TRBs (spec 6.4.1 note; see `docs/usb-xhci-info/xhci-data-structures.md` "Transfer TRBs")
2. Set IOC (Interrupt On Completion) on the last TRB of the transfer.
3. Write cycle bit to each TRB, but write the first TRB last, so hardware doesn't start partially-written transfers. This makes enqueue a whole-TD operation: `XhciRingEnqueueTd` writes the head TRB with the inverse cycle, fills the rest, and publishes the head with one final store. If the TD spans the wrap-back Link TRB, that Link TRB's Chain bit must be set for the crossing (and cleared again on a crossing between TDs); see `docs/usb-xhci-info/xhci-data-structures.md`, "TD composition and Link placement".
4. Ring doorbell: write `DB[slotId]` with target = `endpoint_dci` in bits 7:0. Leave the stream ID bits zero unless streams are explicitly enabled.

Endpoint DCI (Device Context Index):
- EP0 (Default Control Pipe): DCI = 1
- Other endpoints: DCI = (endpoint_number x 2) + (direction: IN=1, OUT=0)

Transfer completion: hardware writes a Transfer Event TRB to the event ring. Slot ID and Endpoint ID identify the ring; the TRB Pointer identifies the work. That pointer is the TRB that generated the event (the TD's last TRB on IOC, but an earlier one on a short packet with ISP or an error), so resolve it to a ring index and then to the owning TD (`XhciRingTdBounds`) rather than comparing it against the TD's head.

Then decide, do not assume. `XhciRingClassifyEvent` returns two independent facts: `CanRetire` says the event named the TD's last TRB, while `NeedsRecovery` says this ring will not run again until software intervenes. A tail halting error sets both. An intermediate Success, Short Packet, or Missed Service event sets neither: the controller is still advancing, but software cannot reclaim the whole TD yet.

Codes 24-25 stop only the command ring; codes 26-28 stop only transfer rings. `XhciRingClassifyEvent` rejects a completion code from the wrong event family before either flag is derived, and treats vendor information codes 224-255 as Success as Table 6-90 requires. When both flags are set, retire first, then program Set TR Dequeue Pointer from `XhciRingDequeuePA` and its `XhciRingDequeueCycle`.

`XhciRingSetDequeue` is the other way the dequeue pointer can move, and it is narrow on purpose: the enqueue position (discarding everything pending) or an outstanding TRB that is the first of its TD. The controller "shall assume that the modified Dequeue Pointer references the first TRB of a TD", and a backwards move would turn already-reclaimed slots back into outstanding ones, a ring that quietly fills up and never empties again.

See `docs/usb-xhci-info/xhci-data-structures.md`, "What the TRB Pointer actually points at", and `docs/contributing/implementation-invariants.md`, "Completion Matching", including the three pointer values that are not ring addresses at all (ED = 1, zero, and a foreign address).

Ring-full backpressure: a ring is full when advancing the Enqueue Pointer
would make it equal the Dequeue Pointer ("Enqueue Pointer + 1 = Dequeue
Pointer", spec 4.9 and 4.9.2.2). Account for the Link TRB when computing the
next position, and track the hardware dequeue position from Transfer Events.
If a whole TD does not fit, enqueue nothing and return a nonzero (busy)
MPSTATUS from the SubmitTransfer callback: usbport keeps the transfer queued
and resubmits it later (`docs/usb-xhci-info/usbport-miniport-abi.md` section 4, Transfers),
which gives clean backpressure. Never write a partial TD and never overwrite
TRBs the hardware has not consumed.

## Slot and Device Context

Input Context (for Address Device / Configure Endpoint commands):
- Entry 0: Input Control Context - A-flags and D-flags bitmask for which contexts to update
- Entry 1: Slot Context
- Entries 2-32: Endpoint Contexts (EP0 = entry 2, DCI = entry index - 1)

The entry numbers above are logical context indexes. The byte offset is `index * ContextSize`, where `ContextSize` is 32 or 64 bytes from `HCCPARAMS1.CSZ`. Do not hardcode 32-byte strides in context helpers.

Slot Context fields (spec Table 6-7):
- `Route String`: 0 for root-hub-attached devices; nonzero for devices behind downstream hubs
- `Speed`: 3 = HighSpeed, 1 = FullSpeed, 2 = LowSpeed (USB2 enum; same default Protocol Speed ID encoding as PORTSC Port Speed)
- `Context Entries`: number of valid endpoint contexts (>= 1 for EP0 only phase)
- `Root Hub Port Number`: 1-based root port number for the device path
- `TT Hub Slot ID` / `TT Port Number`: required for Full-Speed or Low-Speed devices behind a High-Speed hub transaction translator

Endpoint Context fields (spec Table 6-9):
- `EP Type`: 4 = Control bidirectional, 2/6 = Bulk OUT/IN, 3/7 = Interrupt OUT/IN, 1/5 = Isoch OUT/IN
- `Max Packet Size`: from USB endpoint descriptor
- `Max Burst Size`: 0 for USB 2.0 (no burst)
- `TR Dequeue Pointer`: physical address of transfer ring start | DCS (dequeue cycle state = 1). The 1 is only correct here because the ring is *fresh*; a later Set TR Dequeue Pointer command must carry the ring's current cycle from `XhciRingDequeueCycle`, not a constant (`docs/usb-xhci-info/xhci-data-structures.md`, "DCS is not a constant")
- `Interval`: for interrupt/isochronous endpoints (log2 of polling interval in 125us units)
- `Average TRB Length`: hint to hardware; set to 8 for control EP0 (spec-recommended value)

## Downstream Hub Addressing

The root-hub-only path can use Route String = 0. Once external hubs work, every device context must describe the full path from the xHCI root port:

- `Root Hub Port Number` stays the physical xHCI root port where the path begins.
- `Route String` encodes the downstream hub port path, four bits per hub tier.
- Devices below High-Speed hubs need transaction translator information when they operate at Full Speed or Low Speed.
- Test both single-TT and multi-TT High-Speed hubs. They exercise different TT scheduling and split-transaction behavior even when all managed xHCI ports are USB 2.0 protocol ports.

`usbhub.sys` owns hub enumeration, but `xhci98.sys` still has to program route and TT fields correctly when USBD submits URBs for devices behind those hubs.

The hub's own slot has to be described too, and by a Configure Endpoint. `Hub`, `Number of Ports`, and (only on a High-Speed hub) `TTT` and `MTT` are the fields 6.2.2.2 p.412 names, and none of them can be programmed by an Evaluate Context. An `Address Device` clears them, so a re-enumerated hub has to be marked again. The driver does this from the snooped hub descriptor; the per-command field table is in `docs/usb-xhci-info/xhci-data-structures.md`.

## Port Management (PORTSC)

Key PORTSC bits per port:

| Bit | Name | Description |
|---|---|---|
| 0 | CCS | Current Connect Status |
| 1 | PED | Port Enabled/Disabled |
| 4 | PR | Port Reset (write 1 to reset) |
| 5-8 | PLS | Port Link State |
| 9 | PP | Port Power (write 1 to power) |
| 10-13 | SPEED | Port speed after reset (USB2: 1=FS, 2=LS, 3=HS) |
| 17 | CSC | Connect Status Change (write 1 to clear) |
| 18 | PEC | Port Enable Change (write 1 to clear) |
| 21 | PRC | Port Reset Change (write 1 to clear) |
| 22 | PLC | Port Link State Change (write 1 to clear) |

PORTSC mixes RW1C change bits (bits 17-23) with bits that have dangerous write side effects. Writing 1 to `PED` (bit 1) disables the port, and `PR` (bit 4) and `WPR` (bit 31) are write-1-to-reset. **A naive read-modify-write that preserves an already-set `PED` bit will silently disable the port.** Build a "neutral" value that keeps the read-only and link-state bits but clears `PED`, `PR`, `WPR`, `LWS`, every RW1C change bit and the RsvdZ bits (2 and 29:28, which 5.1.1 says software writes as 0), then OR in only the bit you intend to change. The production mask is `XHCI_PORTSC_UNSAFE_MASK` in `src/xhci.h`, with a compile-time assertion that it strips nothing else; the pattern below is that mask written out.

Safe PORTSC write pattern:
```c
/* Keep RO/state bits; clear PED, PR, WPR, LWS, all RW1C change bits and the
   RsvdZ bits so the write has no side effects.  LWS must be cleared too:
   writing with LWS = 1 turns the PLS field into an unintended link-state write.
   WPR is RW1S like PR but "shall always return 0 when read" (5.4.8, Table
   5-27), so a fresh read never carries it: clearing it protects a composed or
   stale value, and keeps bit 31 zero on USB2 protocol ports, where it is
   RsvdZ. */
#define PORTSC_RSVDZ_MASK    0x30000004UL   /* bits 29:28 and 2 */
#define PORTSC_PRESERVE_MASK (~(PORTSC_PED | PORTSC_PR | PORTSC_WPR | PORTSC_LWS | \
                                PORTSC_CHANGE_BITS | PORTSC_RSVDZ_MASK))

ULONG portsc = READ_REGISTER_ULONG(&regs->PORTSC[portIndex]);
portsc &= PORTSC_PRESERVE_MASK;  /* drop PED, PR, WPR, LWS, RW1C and RsvdZ bits */
portsc |= PORTSC_PP;             /* set only the desired bit */
WRITE_REGISTER_ULONG(&regs->PORTSC[portIndex], portsc);
```

## Speed Encoding (USB2 ports)

After a USB2 port reset completes (PRC set), read `PORTSC.SPEED`:
- 1 = Full Speed (12 Mbps)
- 2 = Low Speed (1.5 Mbps)
- 3 = High Speed (480 Mbps)

Map to Windows `USB_DEVICE_SPEED`:
- FS -> `UsbFullSpeed`
- LS -> `UsbLowSpeed`
- HS -> `UsbHighSpeed`

This mapping feeds the driver's internal port shadow and its Slot Contexts. It
is not what the root-hub reporting layer tells `usbport.sys`. Every connected
managed root port is reported to usbport as High Speed, and
`XHCI_HUB_PORT_LOW_SPEED` is never set at all; see
`docs/contributing/implementation-invariants.md`, "Root Hub Reporting". That
works around `USBPORT_GetTt`, which `CONTAINING_RECORD`s an empty `TtList` into
`0xFFFFFFEC` on the Full-Speed root-port path and bugchecks both primary
targets deterministically. A reporting layer written or reviewed from the
mapping above alone reintroduces that bugcheck; the invariants document is
normative for what is reported, this section for what is decoded.

## Completion Status Mapping

Map xHCI Transfer Event completion codes (spec section 6.4.5; the codes this driver handles are tabled in `docs/usb-xhci-info/xhci-data-structures.md` "Completion Codes") to `USBD_STATUS`. Implemented by `XhciXferCodeInfo` (`src/xhci_xfer.c`), pinned by `test/test_xfer.c`.

The vocabulary is the Windows 2000 DDK's `usbdi.h`. `USBD_STATUS_DATA_BUFFER_ERROR`, `USBD_STATUS_BABBLE_DETECTED` and `USBD_STATUS_XACT_ERROR`, the three names ReactOS's `usbehci` returns, do not exist in `C:\NTDDK\inc\usbdi.h` (checked, not recalled); they are later WDK additions. That header is the one this driver builds against and the one both targets' USB stacks were built from, so its set is the ceiling. The era-appropriate equivalents are the ones ReactOS's uhci miniport uses, which is the closest precedent available for a miniport shipping against this header.

`USBD_STATUS_DEVICE_GONE` is a fourth name in the same trap. It was proposed for a transfer whose device record is already gone and declined because that name is also absent from the Windows 2000 DDK's `usbdi.h`. usbport's own code has the value internally (`usbport-miniport-abi.md` section 4), but the miniport completes such a transfer with `USBD_STATUS_CANCELED`, the status it already answers for a `GONE` record.

| xHCI Code | xHCI Name | USBD_STATUS | Value | Why |
|---|---|---|---|---|
| 1 | Success | `USBD_STATUS_SUCCESS` | 0x00000000 | |
| 13 | Short Packet | `USBD_STATUS_SUCCESS` (with actual length) | 0x00000000 | see below |
| 224-255 | Vendor Defined Info | `USBD_STATUS_SUCCESS` | 0x00000000 | Table 6-90: unknown means Success |
| 6 | Stall Error | `USBD_STATUS_STALL_PID` | 0xC0000004 | |
| 4 | USB Transaction Error | `USBD_STATUS_DEV_NOT_RESPONDING` | 0xC0000005 | no handshake, CRC, or timeout - a device that did not answer (UHCI maps timeout/CRC the same way) |
| 20 | No Ping Response | `USBD_STATUS_DEV_NOT_RESPONDING` | 0xC0000005 | |
| 36 | Split Transaction Error | `USBD_STATUS_DEV_NOT_RESPONDING` | 0xC0000005 | |
| 2 | Data Buffer Error | `USBD_STATUS_DATA_OVERRUN` | 0xC0000008 | the xHC's own data path over/under-ran |
| 3 | Babble Detected | `USBD_STATUS_BUFFER_OVERRUN` | 0xC000000C | UHCI maps babble+stalled here |
| 5 | TRB Error | `USBD_STATUS_INTERNAL_HC_ERROR` | 0x80000800 | this driver built a TRB the xHC could not act on |
| 22 | Incompatible Device | `USBD_STATUS_INTERNAL_HC_ERROR` | 0x80000800 | |
| 32 | Event Lost | `USBD_STATUS_INTERNAL_HC_ERROR` | 0x80000800 | and escalates: a dropped event is not a late one |
| 33 | Undefined Error | `USBD_STATUS_INTERNAL_HC_ERROR` | 0x80000800 | |
| 34 | Invalid Stream ID | `USBD_STATUS_INTERNAL_HC_ERROR` | 0x80000800 | this driver opens no streams |
| 192-223 | Vendor Defined Error | `USBD_STATUS_INTERNAL_HC_ERROR` | 0x80000800 | Table 6-90: unknown means Undefined Error |
| 26/27/28 | Stopped / Length Invalid / Short Packet | `USBD_STATUS_CANCELED` | 0x00010000 | software stopped the ring with Stop Endpoint |

Everything else is refused by this decoder, not mapped: code 0, the isoch-only codes (18, 23, 31), the command-ring families (7-11, 17, 19, 24, 25, 29, 35), the three whose event carries no valid TRB pointer (12, Endpoint Not Enabled, a Transfer Event whose TRB Pointer is zero per 4.7 p.143; 14; 15), and Event Ring Full (21), which arrives as a Host Controller Event and is escalated from the DPC rather than attributed to a transfer. An unknown or impossible code completes nothing and retires nothing; the visible failure is a counter plus usbport's own URB timeout, because treating it as success is the one option that loses data silently.

The isoch-only codes have a decoder of their own, `XhciXferIsoCodeInfo` (`src/xhci_xfer.c`), where Missed Service becomes `USBD_STATUS_NOT_ACCESSED` with the residual explicitly not read as bytes and Isoch Buffer Overrun becomes `USBD_STATUS_BUFFER_OVERRUN`; `test/test_iso.c` pins each. Two decoders share one code space and disagree on purpose: Short Packet is a success in the shared table and a `DATA_UNDERRUN` in the isochronous one. Read the table above as the non-isochronous path's.

Short Packet (code 13) is a successful completion; the device sent less data than requested. Complete with SUCCESS and the actual bytes received. Whether a short transfer is an error depends on `USBD_SHORT_TRANSFER_OK`, which lives in an URB the miniport never sees, and usbport sets it on its own enumeration requests. Compute the actual length with the multi-TRB rule in `docs/usb-xhci-info/xhci-data-structures.md`, "Event TRBs", not `requested - residual`.

The mapping above is only half the answer. The completion code decides the `USBD_STATUS`, the length field decides the byte count, and where they disagree the length field wins. So any event that measured a byte count fixes the reported length: a Success carrying a nonzero residual (the spurious-success quirk below), and equally a Success carrying a zero residual on a non-final TRB, which measures the bytes moved so far. Only a transfer where nothing measured anything may have its terminal Success read as "the whole request completed".

Getting this backwards is a silent overreport rather than a visible failure, so the ambiguous case resolves toward the underreport: a short descriptor is retried, an unwritten buffer tail is not noticed. See `docs/contributing/implementation-invariants.md`, "Transfer Buffers".

## Firmware Handoff, and the Controller Deviations This Driver Acts On

There is no per-controller quirk table in this driver, and there is not going to be one. A catalogue of the field-accumulated deviations of NEC/Renesas, ASMedia, Fresco Logic, VIA Labs, Etron and AMD silicon would describe none of what this project does: it identifies no controller by VID/DID, uploads no firmware, runs no compliance-mode timer and applies no PLL fix. A workaround that is not implemented cannot be got wrong, and a catalogue of the ones that might have been reads, after a year, as a list of things the driver does. What follows is what the code actually does.

| What | Why it is here rather than in a quirk table |
|---|---|
| BIOS handoff | Spec-mandated on every controller (4.22.1), so not a quirk at all. Implemented in `src/xhci_init.c` |
| Never set BEI | Applied unconditionally, so no controller has to be identified for it to be applied |
| Residual-length computation | The correct rule for every controller; the spurious-success silicon is only why it is never short-circuited |
| `XUSB2PR` routing | Not handled: the driver reads neither register, by decision. It is here as a diagnosis, because its symptom is a controller that looks dead |

Where to look when a real controller misbehaves anyway: Linux `drivers/usb/host/xhci.h` (the `XHCI_*` quirk flags, with the comments explaining what broke), `xhci-pci.c` (VID/DID -> flag for every known controller), and `pci-quirks.c` (`quirk_usb_handoff_xhci`, `usb_enable_intel_xhci_ports`). Those are years of field evidence, and per `docs/contributing/failure-diagnosis.md`'s trust order they outrank anything this repository would write down about someone else's silicon. Individual Intel/AMD chipset datasheets are largely under NDA; the Linux source has already extracted what matters from them.

The Phase 0 qualifier is the one place a VID/DID table still exists, in `xhciqual/quirks.c`, and it reports rather than works around. `QF_PME_STUCK` is the example that matters, because Windows 2000 issues real D-state transitions and acts on PME, which makes a stuck wake latch a genuine work item on that target rather than a Win98 footnote. That table's authority is Linux `xhci-pci.c`, directly.

### BIOS handoff - required on all controllers

Source: xHCI spec 4.22.1 (Pre-OS to OS Handoff Synchronization) and 7.1 (the USB Legacy Support Capability itself); Linux `pci-quirks.c` `quirk_usb_handoff_xhci()`.

Before touching any operational register, claim ownership from the BIOS/UEFI firmware. Failing to do this is the commonest reason a controller appears to initialise and then silently misbehaves. The Legacy Support Capability has Extended Capability ID = 0x01; find it by walking the xECP chain.

```
DW0 bit[16]   BIOS Owned Semaphore (read-only from the OS side)
DW0 bit[24]   OS Owned Semaphore (write 1 to claim)
DW1           USBLEGCTLSTS - SMI enable bits (clear all to disable firmware SMI)
```

1. Walk the xECP chain looking for capability ID = 0x01.
2. Not found: the BIOS has already handed off, or the controller has no firmware. Skip.
3. Found: set bit 24 (OS Owned) of DW0.
4. Poll until bit 16 (BIOS Owned) clears. Time out after ~1 second.
5. If BIOS Owned never clears, record it and proceed anyway; some firmware simply ignores the handoff and the controller still works.
6. Having claimed ownership, clear every enable bit in USBLEGCTLSTS (DW1). Leaving SMI enabled lets the firmware intercept USB interrupts and double-handle events.

This happens before the halt+reset sequence; it is step 1 of "Initialization Sequence" above. One bounds-check trap is worth stating with it: the write in step 6 touches the capability's second DWORD, so a window check sized to the 4-byte capability header passes and then writes outside the mapping.

### Never set BEI

The Block Event Interrupt bit in an Isochronous TRB suppresses the interrupt for every TRB of a chain but the last. Some Intel PCH revisions mishandle it and miss that final interrupt entirely, so the transfer never completes.

The scope is every Intel xHCI controller, not one generation. Linux sets the flag on the vendor alone (`if (pdev->vendor == PCI_VENDOR_ID_INTEL) { ... xhci->quirks |= XHCI_AVOID_BEI; }` in `xhci-pci.c`), with the Panther Point-specific block immediately below it setting other flags and not touching BEI.

This driver never sets BEI and sets IOC on every isochronous TRB instead: more interrupts, safe on all hardware, and unconditional, so no controller has to be identified. The bit's position is in `docs/usb-xhci-info/xhci-data-structures.md`.

### Spurious success - why length never comes from the completion code

Some NEC uPD720200 revisions, Fresco Logic FL1000, and early VIA Labs VL800 revisions return Transfer Event completion code 1 (Success) for transfers that delivered fewer bytes than requested, with the length field still correct. Linux carried this as `XHCI_TRUST_TX_LENGTH` before making it every controller's default (so look in git history, not in current `xhci.h`).

The rule is therefore universal rather than conditional, and it is the one stated at the end of "Completion Status Mapping" above. The transferred length is the sum of the TRB lengths of the TRBs that event completes, minus the event's residual (`XhciRingSumTrbLengths`); any event that measured a byte count fixes the length, a Success included; and only a transfer no event ever measured may read a terminal Success as "everything moved". It is not `requested_length - residual`: that is a single-TRB-TD rule, wrong for any control transfer's multi-TRB data stage. The normative statements are `docs/contributing/implementation-invariants.md` ("Completion Matching"), the "Event TRBs" section of `docs/usb-xhci-info/xhci-data-structures.md`, and `docs/contributing/failure-diagnosis.md`.

### `XUSB2PR` - the Intel 7/8-series port mux this driver leaves alone

> **Nothing in this section has been measured by this project.** No machine in
> this fleet has ever had an Intel 7/8-series port mux, so every register
> value, BIOS behaviour and symptom below is derived from the two sources named
> underneath and has never been read off silicon here.

Phase 13 owned the driver-side observation and Phase 4 task 10 the register
reading; both are published as unreachable limitations. Treat the whole
section as a prediction to check, not as a record.

Applies to 7-series and 8-series PCH only, the generations carrying both EHCI and xHCI. Skylake and later Intel have no EHCI, the ports are hardwired to xHCI, and these registers do not exist; nor do they on modern AMD. Source: Linux `pci-quirks.c` `usb_enable_intel_xhci_ports()`; Intel 7-series PCH datasheet vol. 2 (xHCI config registers).

Every switchable USB2 port is muxed between the EHCI and xHCI controllers by PCI config registers on the xHCI PCI function (typically bus 0, device 20h, function 0):

| Config offset | Register | Meaning |
|---|---|---|
| 0xD0 | `XUSB2PR` | USB 2.0 Port Routing: bit n = 1 routes switchable USB2 port n to xHCI; 0 routes it to EHCI |
| 0xD4 | `XUSB2PRM` | Mask (RO): which USB2 ports are switchable at all |
| 0xD8 | `USB3_PSSEN` | SuperSpeed enable: bit n = 1 enables SS termination on USB3 port n |
| 0xDC | `USB3PRM` | Mask (RO): which SS ports can be enabled |

The trap: with BIOS "xHCI mode" set to Auto/Smart-Auto, the common default, the firmware routes USB2 ports to EHCI at boot. The xHCI controller then initialises perfectly (handoff, reset, rings and interrupts all pass) and no port ever shows a connect, because the physical ports are wired to the other controller. It looks like a dead driver, and it is row 7 of `docs/contributing/failure-diagnosis.md`.

Expected register values, by firmware mode (a prediction, not a reading). Firmware of this era typically exposes the routing as a setting named something like "USB 3.0 Mode". What each value should produce, from the datasheet and from what Linux writes:

| Firmware mode | `XUSB2PR` | `XUSB2PRM` | `USB3_PSSEN` | `USB3PRM` | Expected effect |
|---|---|---|---|---|---|
| xHCI disabled | - | - | - | - | the xHCI PCI function is absent from config space entirely |
| route ports to xHCI | `= XUSB2PRM` | mask | `= USB3PRM` | mask | every switchable USB2 port on xHCI |
| Auto / Smart-Auto | `00000000` | mask | `00000000` | mask | the trap: every switchable port left on EHCI, SS termination off |

So `Auto` means "the OS driver does the switchover", and a driver that never writes the register (this one) would get the deceptive symptom: ports power, xhciqual C1-C4 pass, and no connect event ever arrives. Whether a given firmware behaves this way, and which physical connectors are inside the switchable set at all, is a per-machine fact this project has never been able to check.

What Linux does at probe is `USB3_PSSEN = USB3PRM; XUSB2PR = XUSB2PRM`: claim every switchable port. This driver does neither. The decision is recorded at the end of Phase 4 in `docs/contributing/roadmap.md`, and nothing in `src/` reads or writes either register.

The reason is the development workflow on a machine that has them: routing a port to xHCI removes it from EHCI, and on such a machine EHCI is the Windows 98 file-transfer path. A firmware setting that routes ports to xHCI writes the same blanket value Linux does, so it claims every switchable port too; any file-transfer path that survives it does so because those connectors are outside the switchable set, which is a thing to measure on the machine rather than assume. Prefer the firmware setting, or xhciqual test C7, over a driver-side write.

What C7 is (Phase 4 task 10, withdrawn as unreachable with the machine that could have run it): `xhciqual/bringup.c` reads all four registers, gated on `QF_XUSB2PR` in `xhciqual/quirks.c` for device IDs 1E31, 8C31 and 8CB1, and under `--set-intel-ports` writes `XUSB2PR = XUSB2PRM`. It leaves that write in place on exit, since the routing is what the test is for, so expect to restore the previous state by cold boot. It has executed only against `xhciqual/test/test_mmiodiag.c`.

A blanket `XUSB2PR = XUSB2PRM` is the wrong default for this project for the reason above: on a machine that has EHCI, EHCI is the Windows 98 file-transfer safety net. Leave `USB3_PSSEN` alone: SS termination is useless here (USB 3.x ports stay unpowered) and disabled termination is what guarantees SS-capable devices fall back to their USB2 pins. Undoing the routing at teardown is polite and optional; the BIOS reprograms it at reboot.

If a bench session is ever losing real time to this diagnosis, add the read half only: read `XUSB2PR` and `XUSB2PRM` through the usbport config-space service (`docs/usb-xhci-info/usbport-miniport-interface.md`), record them in the extension, and report "USB2 ports are routed to EHCI" rather than letting a correctly-initialised controller look dead. Phase 13 owned the driver observed against the routing; it is published as unreachable, for want of any machine with the register.
