# Driver Architecture

## Overview

`xhci98.sys` is a WDM kernel-mode USB host controller miniport for xHCI hardware. It does not talk to `usbhub.sys` directly. Instead it plugs in underneath Microsoft's `usbport.sys` port driver, the same way `usbehci.sys`/`usbuhci.sys`/`usbohci.sys` do, and reuses the entire generic USB stack above it.

This is the Option A (miniport) integration model. See `docs/usb-xhci-info/win98-wdm.md`, "USB Stack Architecture and the Integration Decision", for why it was chosen over a monolithic HCD (Option B), and for the go/no-go validation gate that had to pass before hardware work began.

The `usbport.sys` miniport interface (`USBPORT_REGISTRATION_PACKET` and its entry points) is not documented by Microsoft; ReactOS remains the authoritative open reference. The Phase 3 spike settled the question (checkpoint MET: one byte-identical binary registers and binds on both Windows 98 SE + NUSB and native Windows 2000 SP4), so the packet layout, entry-point names and call contracts below are pinned rather than provisional.

`docs/usb-xhci-info/usbport-miniport-abi.md` is the normative record: bit-exact layouts, signatures and observed contracts, transcribed from the local ReactOS mirror and validated against both shipping binaries. `docs/usb-xhci-info/usbport-miniport-interface.md` is the derivation guide that says where the sources are and how the validation is performed. Where this file and the ABI document differ, the ABI document is right.

## Windows USB Stack Integration

```
User mode
-----------------------------------------
Kernel mode

 [USB class drivers]      <- e.g. usbstor.sys, hidusb.sys
         |
    [usbhub.sys]          <- hub driver; enumerates downstream devices (REUSED, unchanged)
         |  (root hub PDO is created by usbport.sys)
    [usbport.sys]         <- USB port driver: root hub PDO, IOCTL_INTERNAL_USB,
         |                    URB parsing, enumeration state machine, bandwidth
         |                    (REUSED from the Win2000-derived USB 2.0 stack; ships in NUSB)
         |  (private USBPORT_REGISTRATION_PACKET miniport interface)
    [xhci98.sys]            <- THIS DRIVER - xHCI miniport registered with usbport.sys
         |
 [PCI bus driver]
         |
 [XHCI PCI hardware]
```

`usbport.sys` creates and owns the host controller FDO (the PCI bus driver provides the PDO). PnP loads the driver for a PCI device with:
- Class 0x0C (Serial Bus Controller)
- Subclass 0x03 (USB)
- Prog-IF 0x30 (xHCI)

The INF binds that device to `xhci98.sys` itself (on 9x via `DevLoader=*ntkern` + `NTMPDriver=xhci98.sys`, mirroring how the `usbehci` INF binds EHCI controllers); `usbport.sys` loads as `xhci98.sys`'s import dependency, takes over the driver object when `DriverEntry` calls `USBPORT_RegisterUSBPortDriver`, and from then on calls into the miniport through the registration packet. NUSB's own `USB2.inf` only binds EHCI (`PCI\CC_0C0320`); the xHCI binding (`PCI\CC_0C0330`) is this project's INF to author.

### What usbport.sys does for us (so we do not)

- Creates and manages the root hub PDO; loads `usbhub.sys` on it.
- Handles all `IOCTL_INTERNAL_USB_*` IRPs from `usbhub.sys`/`usbd.sys`.
- Parses URBs and drives the device enumeration state machine (default-address pipe, descriptor reads, SET_CONFIGURATION) down to miniport calls.
- Bandwidth accounting, pipe-handle bookkeeping, transfer timeouts, and the standard PnP/power IRP plumbing for the host FDO.

### What xhci98.sys (the miniport) still owns

Everything xHCI-specific. The hardware model is unchanged from a monolithic design; only the upward interface differs. The miniport translates `usbport.sys`'s generic calls into xHCI operations:

- Controller bring-up: BIOS handoff, halt/reset, scratchpad, DCBAA, command/event rings.
- Slot/endpoint lifecycle: Enable Slot, Address Device, Configure Endpoint, Disable Slot, device/input contexts - including intercepting usbport's SET_ADDRESS control transfer and emulating it with Address Device (xHCI forbids software-issued SET_ADDRESS; see the enumeration data flow below).
- Transfer encoding: usbport "submit transfer" calls -> TRBs on the right endpoint ring -> doorbell.
- Root-hub callbacks: report port count/status, perform port reset, report speed (usbport builds the hub descriptor and PDO from these).
- Interrupt handling: the miniport's ISR/DPC hooks drain the xHCI event ring and complete transfers back to usbport.

## Internal Components

```
xhci98.sys (usbport miniport)
|-- Miniport registration + lifecycle   xhci_dispatch.c
|   |-- DriverEntry -> USBPORT_RegisterUSBPortDriver (fills registration packet)
|   |-- StartController / StopController / SuspendController callbacks
|   `-- (PnP/power for the host FDO is handled by usbport.sys, not here)
|
|-- PCI / Hardware Layer         xhci_pci.c
|   |-- BAR0 MMIO access (resources handed in by usbport at StartController)
|   `-- PCI config access (the only file that touches PCI config space)
|
|-- Interrupt Path               xhci_evt.c
|   |-- XhciIsr        - claims the interrupt, EINT before IMAN.IP, IE forced 0
|   `-- XhciEventDpc   - bounded event-ring drain, ERDP + EHB republication
|
|-- Command Engine               xhci_cmd.c
|   |-- One command in flight, per-command generation, uncancellable watchdog
|   |-- The abort ladder (CA -> CS -> UsbPortInvalidateController(RESET))
|   `-- The No Op self-test every start issues, and every resume that
|       reinitializes rather than restoring saved state
|
|-- Controller Initialization    xhci_init.c
|   |-- BIOS handoff, reset sequence
|   |-- Scratchpad allocation
|   |-- Event ring setup (ERST), command ring setup
|   |-- Controller start (RUN bit), quiesce, suspend/resume, Save/Restore State
|   `-- The interrupt mask/unmask pair the ISR's decline gates depend on
|
|-- Capability / Layout Cores    xhci_caps.c, xhci_mem.c   (pure - no MMIO)
|   |-- Capability register and Supported Protocol parsing, window bounds
|   `-- The one fixed common-buffer carve (design doc 04) and its asserts
|
|-- Ring Manager                 xhci_ring.c   (pure core - no MMIO)
|   |-- Command ring enqueue (the caller rings the doorbell, XhciWriteDoorbell)
|   |-- Event ring dequeue and event classification (dispatch is XhciEventDpc)
|   `-- Transfer ring alloc/free/enqueue per endpoint
|
|-- Port Manager                 xhci_port.c    (pure core - no MMIO, no lock)
|   |-- USB Protocol Capability parse -> classify each logical port
|   |   (USB2-only, USB2 companion, USB3 companion, USB3 orphan)
|   `-- PORTSC word builders and the root-hub shadow's construction
|
|-- Root Hub Callbacks           xhci_rh.c
|   |-- RH_GetRootHubData / GetPortStatus / GetHubStatus, the five change clears
|   |-- Port power, enable/disable, the asynchronous reset and resume
|   |-- USB3 ports: left unpowered and unmanaged (USB 3.0 is out of scope)
|   `-- usbport's health poll (nominal 500 ms; 36-80 ms measured on the
|       E460): age retires, port-power and disown settlement
|
|-- Slot / Endpoint Manager      xhci_slot.c
|   |-- Enable Slot / Address Device (BSR + non-BSR) / Configure Endpoint / Disable Slot
|   |-- Device + input context management (context encodings in xhci_ctx.c)
|   `-- The endpoint quiescence machine (Stop / Reset / Set TR Dequeue)
|
|-- Contract Probe               xhci_probe.c
|   `-- Records the SG/endpoint shapes usbport actually hands over; changes
|       nothing the driver does
|
|-- Descriptor Snoop             xhci_desc.c
|   |-- Walks GET_DESCRIPTOR(Configuration) replies off the same EP0 reply
|   |   channel the topology graph uses, in 32-byte chunks at DISPATCH
|   |-- Keeps each isochronous endpoint's bInterval per configuration; only a
|   |   complete configuration commits, and a SET_CONFIGURATION selecting
|   |   another value discards it
|   `-- Pure core: the second consumer of the reply channel, separate fields
|       on the device record (invariants: "Endpoint Configuration")
|
|-- Hub Topology Graph           xhci_topo.c
|   |-- Snoops hub-class EP0 traffic (requests + IN replies) into a node per
|   |   external hub: position, port count, TT think time, multi-TT, connects
|   |-- Answers Route String / TT questions for devices behind hubs
|   |   (Address Device claims, Slot Context hub marking, departures)
|   `-- Pure core: no lock, no MMIO, no usbport pointer held
|
|-- Transfer Translator          xhci_xfer.c
|   |-- usbport transfer request -> TRB encoding per transfer type
|   |   |-- Control (Setup + Data + Status TRBs)
|   |   |-- Bulk / Interrupt (Normal TRBs)
|   |   `-- Isochronous (Isoch TRBs)
|   `-- Transfer completion (status mapping xHCI event code -> usbport status)
|
|-- Log Ring                     xhci_log.c, xhci_log.h   (pure core)
|   |-- The bounded in-memory log every producer appends to under the
|   |   controller lock; verbosity tiers, per-code budgets
|   `-- Read out by XHCISNAP through the snapshot channel, or handed to
|       DbgPrint once by the PASSIVE-level flush in xhci_dispatch.c
|
`-- Debug Helpers                xhci_dbg.c
    `-- The trace channel (guarded by #ifdef XHCI_DBG_TRACE, the qemu
        flavour; never #if DBG)
```

Compared with a monolithic HCD, three pieces shrink or move into `usbport.sys`. There is no `xhci_dispatch.c` IOCTL_INTERNAL_USB handler (usbport owns it). There is no root hub PDO creation or hub-descriptor construction (usbport owns that too; only the port callbacks remain, in `xhci_rh.c`, while `xhci_port.c` stays a pure core with no MMIO and no lock). And there is no URB-function dispatch table: `xhci_xfer.c` receives already-parsed transfer requests.

## Data Flow: Transfer Submission

```
usbhub.sys / class driver submits URB -> usbport.sys
    |
    v
usbport.sys parses URB, resolves pipe handle, calls miniport SubmitTransfer callback
    |
    v
xhci_xfer.c - translate the transfer request into one or more TRBs (IOC on last)
    |
    v
xhci_ring.c - enqueue TRBs onto the endpoint's transfer ring
    |
    v
xhci hardware - write doorbell -> controller processes TRBs
    |
    v (async; hardware fires interrupt when done)
    |
xhci_evt.c XhciIsr - reads USBSTS.EINT, acknowledges it and IMAN.IP, forces
                     IMAN.IE to 0, returns TRUE so usbport queues the DPC
    |
    v
xhci_evt.c XhciEventDpc - bounded drain, then one ERDP write with EHB = 1
    |
    v
xhci_ring.c - dequeue Transfer Event TRBs from the event ring (xhci_evt.c dispatches them)
    |
    v
xhci_xfer.c - map completion status, hand the finished transfer back to usbport.sys,
              which completes the URB/IRP up the stack
```

One declared limit sits on this path. An isochronous request is N Isoch TDs,
one per packet, published as one store. A request needing more TRBs than an
empty pooled ring holds (62, so 62 single-fragment packets) can never be
placed, so it is failed rather than retried for ever; `IsoRefusalsTooLarge`
counts the refusals and `XHCI_POOL_RING_TRBS` is the lever. Real USB Audio
URBs on Windows 2000 arrive as 10 packets per submit (batch 9-V), so the cap
is comfortable.

## Data Flow: Device Enumeration

```
usbport.sys polls the root hub (its own PDO) for port status changes - and it
polls only when the miniport asks it to, through UsbPortInvalidateRootHub
    |
    v
xhci_rh.c root-hub callback - refreshes the shadow from PORTSC, acknowledges the
hardware change bits so the port keeps reporting, and returns the hub-class
status and the changes latched since the last clear-feature
    |
    v  (on connect detected)
usbport.sys requests a port reset
    |
    v
xhci_rh.c - writes PR to PORTSC and returns; the PRC arrives as a Port Status
Change Event and reports enabled + speed (asynchronous by construction - the
callback runs at DISPATCH_LEVEL and may not wait)
    |
    v
usbport.sys runs the enumeration state machine: it opens the default control pipe
at address 0, then sends SET_ADDRESS as an ordinary EP0 control transfer
    |
    v
xhci_slot.c - on the first EP0 open for a port's device: Enable Slot ->
              Address Device (BSR = 1, slot reaches Default state). Neither can
              be waited for, so the open returns success before the slot exists
              and transfers are refused for retry until the chain completes.
              Which root port the address-0 pipe is on cannot be asked of
              usbport (its endpoint properties carry the transaction
              translator), so it is derived from the completion of a port reset
              and checked against the port's own shadow
    |
    v
xhci_slot.c - intercepts the SET_ADDRESS setup packet in SubmitTransfer (xHCI
              forbids putting it on a transfer ring, spec section 4.5.4.1) and
              issues Address Device (BSR = 0). The xHC assigns the address on the
              bus, and the original transfer is completed back to usbport as
              success when the command answers; the miniport keeps a
              usbport-address -> Slot ID map from then on
    |
    v
usbport.sys reads the device descriptor, learns the real bMaxPacketSize0, and
rebuilds EP0: SetEndpointState(REMOVE) -> QueryEndpointRequirements ->
OpenEndpoint at the new address. There is no close callback anywhere in it
    |
    v
xhci_slot.c - resolves the new address through the map to the slot that already
              exists, leaves the transfer ring where it is (the xHC still
              points into it) and issues Evaluate Context for the corrected
              packet size, refusing control traffic until it lands
    |
    v
xhci_xfer.c - remaining control transfers (GET_DESCRIPTOR, SET_CONFIGURATION, ...)
              proceed as TRBs; xhci_slot.c issues Configure Endpoint as pipes open
              (Phase 7a)
    |
    v
xhci_rh.c   - the port reports a connect change, in either direction, and
              xhci_slot.c tears down: stops every endpoint, completes queued
              work as cancelled from the stops' completions, issues Disable
              Slot, and clears the map entry. Two other triggers cause the
              same teardown: a port disable (usbhub abandoning a device with
              nothing unplugged; see the disown/teardown split) and, for a
              device behind an external hub, the hub's own GET_STATUS(port)
              reply reporting the port empty or its connect-change bit set,
              snooped by xhci_topo.c. That reply is the only channel a
              behind-hub departure has
```

The key difference from a monolithic design: the enumeration orchestration (when to address, which descriptors to read, configuration selection) lives in `usbport.sys`. The miniport only executes the xHCI-specific steps when asked.

## Memory Layout

Controller-owned DMA memory is one fixed, worst-case common-buffer block that
`usbport.sys` allocates for the size the miniport declares
(`MiniPortResourcesSize` = `XHCI_HC_RESOURCES_SIZE`, 409,600 bytes, pinned by
`test/test_membuf.c`). The miniport carves it once, in `xhci_mem.c`, and owns
its internal layout; it never calls a HAL common-buffer allocator itself.
`docs/contributing/design/04-controller-common-buffer.md` derives every row:

| Offset | Size | Contents |
|---|---|---|
| `0x00000` | 4096 | DCBAA (2048 reserved, 264 used at 32 slots); Scratchpad Buffer Array at `+0x800` (2048 reserved, 512 used at 64 buffers) |
| `0x01000` | 4096 | Command Ring (64 TRBs); ERST at `+0x400` (one 16-byte entry) |
| `0x02000` | 4096 | Event Ring segment 0 (256 TRBs) |
| `0x03000` | 4096 | The one shared Input Context (2112 bytes worst case at CSZ = 1) |
| `0x04000` | 65536 | 32 Device Contexts, stride 2048 |
| `0x14000` | 32768 | 32 EP0 transfer rings, stride 1024 (64 TRBs) |
| `0x1C000` | 32768 | 32 pooled non-EP0 transfer rings, stride 1024 (`XhciPoolAcquire`, `src/xhci_mem.c`) |
| `0x24000` | 262144 | 64 scratchpad pages, stride 4096 |
| `0x64000` | | total 409,600 bytes (400 KiB) |

The limits are declared policy with a refusal above them: `MaxSlotsEn` is
clamped to 32, and a controller asking for more than 64 scratchpad buffers is
refused at start. `ContextSize` is 32 bytes when `HCCPARAMS1.CSZ = 0` and 64
when it is 1, and that stride is used for input, slot, endpoint and device
contexts. Objects a slot owns (its Device Context, its EP0 ring) live in this
block rather than in an endpoint common buffer, because usbport frees the
endpoint buffer and zeroes the miniport endpoint extension on every
`ReopenPipe`, which happens to EP0 mid-enumeration.

`usbport.sys` owns URB payload mapping and hands the miniport scatter/gather
physical addresses at transfer submission, so there are no bounce buffers.
Confirmed statically in both shipping builds: the elements are produced by
usbport's own NT DMA adapter and are page-granular, so an N-byte transfer
needs at most `ceil(N / 4096) + 1` data TRBs.

## IRQ and DPC Model

- Both targets are line-based, not MSI: Win98/NUSB `usbport.sys` with the 9x PCI driver, and Win2000's native `usbport.sys` with the NT5 PCI driver (the NT kernel gained MSI only in Vista). The driver relies on the controller's PCI INTx# interrupt (shared or dedicated); Interrupter 0 asserts INTx# whenever MSI/MSI-X is not enabled (xHCI spec section 4.17.3).
- INTx is optional in xHCI (section 4.17.3: "PCI Interrupt Pins are optional"). A controller may be MSI/MSI-X-only and report PCI Interrupt Pin (config 0x3D) = 0, in which case neither target's line-based stack gets interrupts. Treat Interrupt Pin != 0 as a prerequisite; most real PCH/discrete xHCI controllers satisfy it. On PCIe the "pin" is emulated via in-band Assert/Deassert_INTx messages routed to a legacy IRQ, which is transparent to both. Where the routing of that assertion differs (PIC on Win98 and Win2000's `hal.dll`/`halacpi.dll`, IOAPIC on Win2000's uniprocessor and multiprocessor APIC HALs), `usbport.sys` and the HAL absorb it; the miniport sees the same ISR callback either way.
- The miniport does not call `IoConnectInterrupt` itself. `usbport.sys` owns the interrupt object and dispatches to the miniport's ISR callback.
- Miniport ISR callback: checks `USBSTS.EINT`; if set, acknowledges `USBSTS.EINT` and then `IR[0].IMAN.IP`, in that order, and returns `TRUE`. Both bits are RW1C; clearing IP first can lose an interrupt, and leaving IP set holds the level-triggered INTx line asserted into an interrupt storm. usbport queues the miniport DPC; the miniport does not request one. Bit-exact rules are in `docs/usb-xhci-info/xhci-data-structures.md` (ISR/DPC rules) and `docs/contributing/implementation-invariants.md` ("Event Ring Draining").
- Miniport DPC callback: drains the event ring until the Cycle Bit says empty, publishing `ERDP` periodically with EHB left set and writing the final dequeue pointer with EHB = 1 once empty; hands completions to usbport. Runs at DISPATCH_LEVEL.
- Ring operations in the DPC/completion path and in the submission path are serialized by the miniport's own spinlock, `xhciControllerLock`. It lives in the driver image rather than in the miniport extension because usbport zeroes that extension before every `StartController`. usbport's own `MiniportSpinLock` and `MiniportInterruptsSpinLock` do not exclude these paths from each other and are not relied on. The lock order, the DIRQL exception for the ISR, and a per-entry-point table are in `docs/contributing/design/05-locking-model.md`.

## INF and Installation

The device's driver is the miniport itself (`xhci98.sys`). On Win98 `usbport.sys` has no service of its own; it loads as an import dependency because the miniport calls its export `USBPORT_RegisterUSBPortDriver` at `DriverEntry`. The import dependency holds on both targets, but the two setup engines express it differently, and one INF must carry both paths:

- Win98 does it the 9x way: `DevLoader=*ntkern` + `NTMPDriver=xhci98.sys`, the same shape NUSB's `USB2.inf` uses for `usbehci.sys`; ntkern resolves the miniport's `usbport.sys` imports at load. The registry layout is mirrored from the installed NUSB EHCI device and was confirmed during the spike.
- Win2000 uses a `.NTx86`-decorated service install (`AddService`/`ServiceBinary`). Here `usbport.sys` does have its own service, a native OS one that already exists; the miniport still reaches it as an import dependency rather than by creating it.

The section shapes, the dirid-12 trap, and the parser limits that constrain the shared INF are in `docs/contributing/build-and-test.md`, "The INF must carry both install paths".

- PnP hardware ID: `PCI\CC_0C0330` (USB class, xHCI prog-IF); add specific vendor/device IDs as higher-priority entries.
- Prerequisite on the target machine: the Win2000-derived USB 2.0 stack (`usbport.sys` + `usbhub20.sys`). On Win98 it ships in NUSB 3.3 and is already present on most retro Win98 SE installs; on Win2000 the same stack is native in SP4 (or KB319973) and NUSB must not be installed.
- The root hub PDO that `usbport.sys` creates for a USB2-flagged miniport should match `USB\ROOT_HUB20`, which NUSB's existing INF binds to `usbhub20.sys`. Confirm the exact ID against the EHCI root hub during the spike.

## Fallback: Option B (monolithic HCD)

Had the Phase 3 spike shown that the miniport ABI could not be matched against the shipping `usbport.sys` (version mismatch, undocumented behavior we could not reproduce), the fallback was a monolithic HCD: `xhci98.sys` re-implements `usbport.sys`'s role itself (creates the root hub PDO, handles `IOCTL_INTERNAL_USB_*`, parses URBs, and runs enumeration) on top of the same xHCI hardware layer described above. This is substantially more work (it amounts to "be usbport.sys") and remains the documented contingency, not the plan.
