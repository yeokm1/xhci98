# Source Files

What every file in `src/` is for, in one table per kind. The authoritative description of each file is the comment block at its head; this page is the map, and it says which files the host test suite compiles (the "pure core" of [design record 03](design/03-host-unit-tests.md)) and which need the DDK. Line counts are omitted on purpose - they drift, the roles do not.

The rule that organises the whole directory: files in the pure core **decide** (they encode TRBs, compute layouts, classify ports, run state machines over caller-supplied memory) and touch no register, no DDK service and no IRQL; everything else is where those decisions become bus cycles and usbport callbacks. `src/sources` names the pure list and the reason each file is on or off it.

## Driver sources - the pure core

Each builds twice: into `xhci98.sys`, and on the build host with MSVC 6.0 under `XHCI_HOST_TEST` (`test\run-host-tests.cmd`). A file that needs `ntddk.h` does not belong here.

| File | Role | Host test |
|---|---|---|
| `xhci_mem.c` | Controller common-buffer layout, computed and checked: the resource size committed in `DriverEntry`, the offsets carved at `StartController`, and the spec's alignment and no-cross-boundary rules, all checked against each other before any of it reaches a controller ([design record 04](design/04-controller-common-buffer.md)). | `test_membuf.c` |
| `xhci_ring.c` | TRB encoding and the ring state machines (command, event and transfer rings, cycle bit, link TRBs, wrap). Rings no doorbell; the one call site that does is in the driver-only layer. | `test_ring.c` |
| `xhci_caps.c` | Extended-capability walk, port classification (which root ports this driver manages, USB 2.0 only) and speed decoding, over a caller-supplied reader so a malformed capability chain is a test rather than a crash. | `test_caps.c` |
| `xhci_port.c` | Constructing `PORTSC` writes that have only the intended effect (the register where a naive read-modify-write disables the port or discards a change bit), and deciding what a port's state means to the USB hub class. | `test_port.c` |
| `xhci_ctx.c` | Slot, Endpoint and Input Control Context encoding. A value that does not fit its field is refused, not masked. | `test_ctx.c` |
| `xhci_xfer.c` | The transfer engine: TD construction from usbport's SG list, the pending-transfer queue, and what a Transfer Event means to one usbport transfer. Speaks the usbport transfer ABI, so it includes `xhci_usbport.h`; still calls no usbport service. | `test_xfer.c`, `test_iso.c` |
| `xhci_desc.c` | The configuration-descriptor snoop (task 9-A.2): recovers an isochronous endpoint's `bInterval`, which usbport does not carry, from reply bytes the caller has already copied out. Decides nothing itself. | `test_desc.c` |
| `xhci_topo.c` | The hub topology graph (task 7b-A.1): reconstructs each device's route string, parent hub and TT from the hub-class traffic `usbhub.sys` sends through `SubmitTransfer`, since usbport hands the miniport no route ([design record 02](design/02-hub-topology-route-string.md)). | `test_topo.c` |
| `xhci_log.c` | The in-memory log ring: a bounded byte ring in the miniport extension, its producer set and per-code budget, and the flush *decision*. No file I/O anywhere; the DDK half (registry switches, IRQL check, `DbgPrint` emission) is in `xhci_dispatch.c`. Compiled into the release build deliberately ([design record 08](design/08-build-flavours-and-the-log-channel.md)). | `test_log.c` |

## Driver sources - the DDK side

These need `ntddk.h` or a usbport service, take the controller lock, or dereference BAR0. None may move into the pure list.

| File | Role |
|---|---|
| `xhci_dispatch.c` | `DriverEntry`, the usbport registration packet, and the whole miniport callback surface - every callback the target's `usbport.sys` build can reach, signature-correct and IRQL-safe, delegating to the files below. Also home to the DDK half of the log ring and its registry switches. |
| `xhci_pci.c` | The only file that touches BAR0 or PCI config space: the MMIO accessors, config-space reads and bounded waits declared in `xhci_hw.h`. Every function is a thin wrapper so "no MMIO outside the wired-up lifecycle" is a checkable property. |
| `xhci_init.c` | The controller initialization sequence (`StartController` onward): BIOS handoff, reset, DCBAA, scratchpad, command and event rings, run, port power - with every interrupt source still masked because usbport calls `EnableInterrupts` the moment start succeeds. Also the in-place recovery path ([design record 07](design/07-controller-recovery-in-place.md)). |
| `xhci_evt.c` | The interrupt path: the miniport ISR body (proves ownership, carries nothing forward) and the event-ring drain that usbport's DPC calls, which hands each event to the command, root-hub, slot or transfer layer. |
| `xhci_cmd.c` | The asynchronous command engine every Enable Slot, Address Device, Configure Endpoint, Reset Endpoint and Set TR Dequeue Pointer goes through: completion matching, software-owned timeouts, stuck-ring recovery. Nothing may wait. Holds the miniport's own interior spin lock ([design record 05](design/05-locking-model.md)). |
| `xhci_rh.c` | The root-hub callback family usbport reaches - port count, port status, port feature set/clear, change notification enable - and the port shadow behind it. Reads and writes `PORTSC` under the controller lock, which is why it is not `xhci_port.c`. |
| `xhci_slot.c` | Devices: slots, the default control endpoint, the command chain from "a port reset finished" to "usbport has an address", `SET_ADDRESS` interception, the usbport-address to Slot ID map, endpoint open/close for every transfer type, and the hub and device positions it asks `xhci_topo.c` for. The largest file in the driver. |
| `xhci_probe.c` | The runtime transfer-contract probe (task 6-V.1): instrumentation that classifies what usbport hands over at the registration-packet surface and leaves counters behind. Nothing in the driver branches on it; it stays here rather than in the pure core because it takes the controller lock. |
| `xhci_dbg.c` | The `qemu` flavour's trace channel. Compiles to nothing without `XHCI_DBG_LIVE`, which `src/sources` defines for `chk_qemu` only, so neither published binary carries its `DbgPrint` import. |

## Headers

| File | Role | DDK-free |
|---|---|---|
| `xhci.h` | xHCI types, register and TRB bit definitions, the fixed common-buffer layout, and the declarations of the whole pure core. Bit positions are transcribed from [xhci-data-structures.md](../usb-xhci-info/xhci-data-structures.md), never from memory. | yes |
| `xhci_usbport.h` | The `usbport.sys` miniport ABI as this driver declares it - registration packet, callback signatures, transfer and endpoint parameter structures - transcribed from [usbport-miniport-abi.md](../usb-xhci-info/usbport-miniport-abi.md) and confirmed against the shipping binaries. An independent declaration of an interoperability contract, not ReactOS code. | yes (`test_packet.c` compiles it) |
| `xhci_xfer.h` | The transfer engine's interface (`xhci_xfer.c`), kept out of `xhci.h` because it needs `xhci_usbport.h`. | yes |
| `xhci_desc.h` | The descriptor snoop's interface, and the argument for why it must exist. | yes |
| `xhci_topo.h` | The topology graph's types and interface, with the measured wire constants. | yes |
| `xhci_log.h` | The log ring's contract: the verbosity ladder, the sinks, and the rule that recording is not emission. | yes |
| `xhci_probe.h` | The probe's classification and counters. | no - it takes the lock |
| `xhci_hw.h` | The driver-only side of the split: MMIO accessors, PCI config access, bounded waits. Implemented in `xhci_pci.c`. | no |
| `xhci_dbg.h` | The trace channel's macros; empty outside the `qemu` flavour. | no |
| `xhci_compat.h` | Win98/Win2000 DDK compatibility shims, chiefly undoing the Win2K DDK's `ExAllocatePool` to `ExAllocatePoolWithTag` rewrite - a policy (Option A needs no private pool), not a missing export. | no |
| `xhci_version.h` | The package version and release date, the single editable source of the version. Included by `xhci98.rc`, `xhciqual\qual.h` and `xhcisnap\xhcisnap.c`; the INF's `DriverVer` literal is checked against it by the INF gate. | yes |

## Build and packaging inputs

| File | Role |
|---|---|
| `sources` | The Windows 2000 DDK build description: target name and type, the `SOURCES` list, defines per flavour, linker flags, and the comments that record which file belongs to the pure core and why. Read it before adding a file. |
| `makefile` | The DDK build stub; it only includes `makefile.def`. Every setting lives in `sources`. |
| `xhci98.rc` | The file version resource (task 8-A.4), so a binary recovered from a user's machine can be identified. Takes its fields from `xhci_version.h`; adds no import. |
| `xhci98.inf` | The INF: one file for two setup engines. Windows 98 SE reads the undecorated sections and loads the driver through `NTKERN`; Windows 2000 SP4 reads the `.NTx86` sections and loads it as a kernel service. Both point at one `CopyFiles` section. |

## Generated, not tracked

| Path | What it is |
|---|---|
| `usbport.lib` | The import library for `usbport.sys`, generated by `scripts\make-usbport-lib.cmd` after a fresh clone. |
| `obj\`, `objchk*\`, `objfre*\` | DDK build output per flavour: `xhci98.sys`, `.pdb`, `.res` and `.obj` files. |
| `build*.log` | The DDK build engine's logs. |

The three flavours (`debug`, `release`, `qemu`) and what distinguishes their outputs are in [design record 08](design/08-build-flavours-and-the-log-channel.md); the build procedure is in [build-and-test.md](build-and-test.md).
