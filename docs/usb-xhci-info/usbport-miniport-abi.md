# usbport.sys Miniport ABI - Source-Verified Reference (ReactOS transcription)

This is the bit-exact companion to `docs/usb-xhci-info/usbport-miniport-interface.md`. That
document explains the derivation strategy, the xHCI mapping of each callback
family, and the binary-validation procedure. This one records the ABI as it
exists in the ReactOS sources, transcribed field by field from the local
mirror, plus what was later read out of the shipping `usbport.sys` binaries.

Provenance. Every ReactOS claim below was read from `external/reactos/` at the
mirror pinned in `external/README.md` (upstream `reactos/reactos` commit
`0298e10d5d904a0230868be8f7bdf6436d589c62`). Citations are
`file:line` in that mirror. If the mirror is refreshed to a newer upstream,
re-check the cited lines.

Trust order (the same as in `docs/usb-xhci-info/usbport-miniport-interface.md`): the
NUSB-installed `usbport.sys` binary outranks ReactOS, and ReactOS outranks this
transcription. ReactOS reimplements the NT5.1/XP-era stack; the Win2000
SP4-lineage binary NUSB ships may differ, most plausibly in the tail of the
registration packet (section 3 notes which fields carry the most risk). The
Phase 3 binary-validation step still applies; this document turns it from
derivation into diffing.

License note: ReactOS is GPL. This file documents struct layouts, constants,
and observed call contracts for interoperability. Do not copy ReactOS function
bodies into `src/`.

Source method. `docs/contributing/legal-provenance.md` indexes this document by
how each non-ReactOS fact was obtained (static disassembly, runtime observation,
or both) and records what third-party material the project touches and how it
is handled. Read it before citing this file's evidence to anyone. One trap:
"binary-confirmed" below means "checked against the shipping binary", and that
check is almost always a static read of a `link -dump -disasm` listing, not a
live trace. A few facts are genuinely runtime (callback reachability, the
`HubAddr`/`PortNumber` property dumps, the `InterruptFlushes` /
`InterruptNextSofRequests` counters) and are marked as such there.

That index is not row-for-row with this document. It covers the contracts
`src/` is written against, so a negative finding here ("neither shipping build
calls `CloseEndpoint`") stays in this file. When you add a finding here that
establishes a contract the driver relies on, add its row there in the same
change; for a smaller observation, state the method beside the observation and
leave the index alone.

---

## 1. Exports and the registration call

`usbport.spec` (the mirror's export list) shows exactly two exports
[usbport/usbport.spec:1-2]:

```
@ stdcall USBPORT_GetHciMn()
@ stdcall USBPORT_RegisterUSBPortDriver(ptr long ptr)
```

Observed on the real NUSB 3.3 packaged binary (5.00.2195.5652, `dumpbin
/exports`): both exports present at ordinals 2 and 3, plus a
`DllUnload` export at ordinal 1 that the ReactOS spec does not list. Nothing
in the miniport contract references `DllUnload`; the packaged `usbehci.sys`
imports only `USBPORT_GetHciMn` and `USBPORT_RegisterUSBPortDriver` (see the
Target ABI record in `docs/usb-xhci-info/usbport-miniport-interface.md` section 5).

Prototypes [usbmport.h:705-714]:

```c
ULONG    NTAPI USBPORT_GetHciMn(VOID);
NTSTATUS NTAPI USBPORT_RegisterUSBPortDriver(IN PDRIVER_OBJECT DriverObject,
                                             IN ULONG Version,
                                             IN PUSBPORT_REGISTRATION_PACKET RegistrationPacket);
```

- `USBPORT_GetHciMn()` returns the constant `USBPORT_HCI_MN = 0x10000001`
  [usbmport.h:4, usbport/usbport.c:2839-2842]. The ReactOS miniport calls it
  first thing in `DriverEntry` as a sanity probe that its import really is a
  usbport build, bailing out if the value is wrong [usbehci/usbehci.c:3621-3622].

  Binary override: ReactOS's value is XP-lineage. The shipping 2195.x binaries
  on both targets return `0x57324B30` ("W2K0"), and their `usbehci.sys` builds
  compare against that; only XP SP3 `5.1.2600.5512` returns `0x10000001`. The
  probe pattern is right; the constant is not. If `xhci98.sys` retains the
  probe, accept both known values: `0x57324B30` for the two primary targets and
  `0x10000001` for XP. This is a small, isolated compatibility accommodation
  with no effect on either primary path; unknown values still fail. Evidence:
  `tools/{nusb,win2ksp4,winxpsp3}-extracted/usbport-registration-disasm.txt`
  and the matching `usbehci-driverentry-disasm.txt`.
- The `Version` argument takes an interface version:
  `USB10_MINIPORT_INTERFACE_VERSION = 100` or
  `USB20_MINIPORT_INTERFACE_VERSION = 200` [usbmport.h:636-637]. `usbehci`
  passes `USB20_MINIPORT_INTERFACE_VERSION` [usbehci/usbehci.c:3694-3696].
  ReactOS's only validation is `Version < 100 -> STATUS_UNSUCCESSFUL`
  [usbport/usbport.c:2863-2866].

  Binary confirmation: the real binary is no stricter, and the version does one
  more thing than reject. All three binaries test `Version >= 100` (else
  `STATUS_UNSUCCESSFUL`) and then `Version >= 200`, which selects the 316-byte
  copy instead of the 300-byte one (section 3). All three shipping
  `usbehci.sys` builds pass 200.
- This is not the same constant family as the packet's `MiniPortVersion`
  field, which takes `USB_MINIPORT_VERSION_OHCI/UHCI/EHCI/XHCI = 0x01-0x04`
  [usbmport.h:526-529]; `usbehci` sets `USB_MINIPORT_VERSION_EHCI`
  [usbehci/usbehci.c:3626]. The two families are used in different places;
  conflating them is an easy way to fail registration. ReactOS already
  defines `USB_MINIPORT_VERSION_XHCI 0x04`. Whether the Win2000-era binary
  accepts an unknown `MiniPortVersion` value is a spike question; starting
  with the EHCI value (plus `USB_MINIPORT_FLAGS_USB2`) is the conservative
  first probe, since that is the combination the binary demonstrably supports.
- The interface version has behavioral consequences beyond validation: usbport
  calls `RH_ChirpRootPort` per root port only when
  `MiniPortInterface->Version >= 200` [usbport/roothub.c:1032-1042].

What registration does, in order [usbport/usbport.c:2846-2929]:

1. Rejects `Version < 100`.
2. Allocates a `USBPORT_MINIPORT_INTERFACE` from NonPagedPool, records
   `DriverObject`, the miniport's previous `DriverUnload`, and `Version`, and
   links it into a global miniport list.
3. Takes over the driver object: it sets `DriverExtension->AddDevice =
   USBPORT_AddDevice`, replaces `DriverUnload`, and points MajorFunction
   entries for CREATE, CLOSE, DEVICE_CONTROL, INTERNAL_DEVICE_CONTROL, PNP,
   POWER, and SYSTEM_CONTROL at `USBPORT_Dispatch` [usbport.c:2896-2905].
   After this the miniport never sees an IRP.
4. Writes its 16 service-function pointers into the caller's packet
   (`RegPacket->UsbPortDbgPrint = ...` etc.) [usbport.c:2907-2922], *then*
   copies the whole packet into its own `MiniPortInterface->Packet`
   [usbport.c:2924-2926].

Binary confirmation of steps 2-4, with one ordering difference. Unlike the
ReactOS order above, all three shipping binaries allocate the interface and
take over the driver object before testing `Version < 100`; they install the
service pointers and copy the packet only after that test passes. Details read
out of the disassembly:

- the interface is `ExAllocatePoolWithTag(NonPagedPool, size, 'pbsu')` and is
  fully zeroed before use;
- the driver-object takeover writes `MajorFunction[]` at DRIVER_OBJECT offsets
  0x38, 0x40, 0x70, 0x74, 0x90, 0x94, 0xA4 (= CREATE, CLOSE, DEVICE_CONTROL,
  INTERNAL_DEVICE_CONTROL, POWER, SYSTEM_CONTROL, PNP), sets
  `DriverExtension->AddDevice`, and saves the miniport's `DriverUnload` at
  interface `+0x0C` before replacing it;
- the 16 service pointers are written at packet offsets 0xE4-0x120 and no
  other packet field is touched before the copy.

Two lineage details that are not miniport-visible: the SP4 and XP builds
allocate 0x150 (336) bytes, store the `Version` argument at interface `+0x10`,
and copy the packet to `+0x14` (the ReactOS `C_ASSERT` shape); the NUSB 5652
build allocates 0x14C (332), does not retain `Version`, and copies to `+0x10`.
Since the interface is zeroed and a `Version < 200` miniport gets only the
first 300 bytes copied, that build can still gate the version-200-only tail
callbacks on their pointers being NULL. That is an inference, not a read
instruction, but it is the only consistent reading of the two facts.

One consequence worth knowing before the spike: the version rejection branch
lands after the driver-object takeover in every build. A `Version < 100`
call therefore returns `STATUS_UNSUCCESSFUL` with the caller's driver object
already hijacked (and the interface allocation leaked). Do not treat a failed
registration as "nothing happened".

Consequence of step 4: the miniport must keep its registration packet in
static storage (ReactOS declares a file-scope
`USBPORT_REGISTRATION_PACKET RegPacket;` [usbehci/usbehci.c:16]) and calls
services through it (`RegPacket.UsbPortWait(...)`). A stack-local packet would
lose the service pointers when `DriverEntry` returns.

`DriverEntry` after a successful registration does nothing else; usbport's own
`DriverEntry` (when loaded purely as an export provider) just returns success
[usbport/usbport.c:2931-2937].

## 2. Constants

### Interface versions and packet version [usbmport.h:526-529, 636-637]

| Constant | Value | Goes in |
|---|---|---|
| `USB10_MINIPORT_INTERFACE_VERSION` | 100 | `Version` argument of the register call (USB 1.1 miniports) |
| `USB20_MINIPORT_INTERFACE_VERSION` | 200 | `Version` argument (USB 2 miniports; use this) |
| `USB_MINIPORT_VERSION_OHCI` | 0x01 | Packet `MiniPortVersion` field |
| `USB_MINIPORT_VERSION_UHCI` | 0x02 | Packet `MiniPortVersion` field |
| `USB_MINIPORT_VERSION_EHCI` | 0x03 | Packet `MiniPortVersion` field (usbehci's value) |
| `USB_MINIPORT_VERSION_XHCI` | 0x04 | Packet `MiniPortVersion` field (defined by ReactOS; W2K-binary acceptance unknown) |

`xhci98.sys` declares `MiniPortVersion = 3` (EHCI), the value both primary
targets' own `usbehci.sys` declares, rather than ReactOS's XHCI 4. The field
is consumed after registration (it selects `HcFlavor`, section 9 item 4), so a
rejected value would have surfaced as an unexplained failure somewhere past the
call rather than at it; the EHCI value was the conservative first probe. The
same `DriverEntry` zeroes `DriverObject->DriverUnload` immediately before the
register call, since usbport replaces it.

### MiniPortFlags [usbmport.h:531-539]

| Flag | Value | Notes |
|---|---|---|
| `USB_MINIPORT_FLAGS_INTERRUPT` | 0x0001 | Controller uses an interrupt |
| `USB_MINIPORT_FLAGS_PORT_IO` | 0x0002 | I/O-port register space (UHCI); not xHCI |
| `USB_MINIPORT_FLAGS_MEMORY_IO` | 0x0004 | MMIO register space (xHCI: set) |
| `USB_MINIPORT_FLAGS_USB2` | 0x0010 | USB2-class miniport: enables the USB2 bandwidth budgeter, TT bookkeeping, `usbhub20` root hub |
| `USB_MINIPORT_FLAGS_DISABLE_SS` | 0x0020 | (name per ReactOS; not set by usbehci) |
| `USB_MINIPORT_FLAGS_NOT_LOCK_INT` | 0x0040 | Skip `MiniportSpinLock` around Enable/DisableInterrupts [usbport.c:568-571] |
| `USB_MINIPORT_FLAGS_POLLING` | 0x0080 | usbehci sets it alongside INTERRUPT [usbehci.c:3628-3632] |
| `USB_MINIPORT_FLAGS_NO_DMA` | 0x0100 | Not for this project |
| `USB_MINIPORT_FLAGS_WAKE_SUPPORT` | 0x0200 | ReactOS and XP usbehci set it; the two primary 2195.x miniports do not. Win2000 acts on the flag, so leave it clear until real wake behaviour exists |

ReactOS usbehci's full set is
`INTERRUPT | MEMORY_IO | USB2 | POLLING | WAKE_SUPPORT`
[usbehci/usbehci.c:3628-3632]. That records the reference implementation, not
the first-probe recommendation for the primary shipping targets; their own
miniports differ as recorded below.

Binary observation: the shipping 2195.x `usbehci.sys` builds set `0x95` =
`INTERRUPT | MEMORY_IO | USB2 | POLLING`, without `WAKE_SUPPORT`; XP's sets
`0x295`, i.e. ReactOS's set. The flag values decode cleanly against this table
in both cases, so the table itself is corroborated. `xhci98.sys` uses the
2195.x set (`0x95`): it is what both primary targets' own miniport declares,
and it avoids committing the driver to real wake behaviour on Win2000 before
any of it exists.

### Bus bandwidth [usbmport.h:541-542]

`TOTAL_USB11_BUS_BANDWIDTH = 12000`, `TOTAL_USB20_BUS_BANDWIDTH = 400000`
(bits/ms-class budget units). usbehci passes the USB2 value
[usbehci.c:3634]; usbport copies it to its per-frame bandwidth table minus a
10% reserve [usbport/pnp.c:742-762], and a registry override
(`TotalBusBandwidth`) can replace it [pnp.c:745-756].

### Transfer types [usbmport.h:6-10]

`ISOCHRONOUS 0, CONTROL 1, BULK 2, INTERRUPT 3` (field `TransferType` in
endpoint properties).

### Endpoint states [usbmport.h:12-17] (Get/SetEndpointState values)

| State | Value |
|---|---|
| `USBPORT_ENDPOINT_UNKNOWN` | 0 |
| `USBPORT_ENDPOINT_PAUSED` | 2 |
| `USBPORT_ENDPOINT_ACTIVE` | 3 |
| `USBPORT_ENDPOINT_REMOVE` | 4 |
| `USBPORT_ENDPOINT_CLOSED` | 5 |

(There is no state 1 in the header - do not invent one.)

### Endpoint status [usbmport.h:19-22] (Get/SetEndpointStatus values)

`USBPORT_ENDPOINT_RUN 0`, `USBPORT_ENDPOINT_HALT 1`,
`USBPORT_ENDPOINT_CONTROL 4`.

### Interrupt endpoint periods [usbmport.h:24-31]

`ENDPOINT_INTERRUPT_1ms/2ms/4ms/8ms/16ms/32ms = 1/2/4/8/16/32`. usbport
pre-buckets interrupt endpoints into these periods and passes the result in
`EndpointProperties.Period`.

The header's `1ms`..`32ms` names are only true for Full and Low Speed. The
binaries show the same field counting microframes on a High-Speed endpoint,
so the constant names are a speed-specific label on a speed-neutral field.
See section 5, "Periodic scheduling: what `Period` actually carries", for the
derivation, the producer/consumer addresses, and the xHCI `Interval`
conversion that follows from it.

### MPSTATUS / RHSTATUS [usbmport.h:131-146]

Miniport callbacks that return `MPSTATUS` use: `MP_STATUS_SUCCESS 0`,
`MP_STATUS_FAILURE 1`, `MP_STATUS_NO_RESOURCES 2`, `MP_STATUS_NO_BANDWIDTH 3`
(map xHCI Resource Error / Bandwidth Error / Secondary Bandwidth Error here so
usbhub degrades gracefully; not No Slots Available, which belongs to Enable
Slot),
`MP_STATUS_ERROR 4`, `MP_STATUS_RESERVED1 5`, `MP_STATUS_NOT_SUPPORTED 6`,
`MP_STATUS_HW_ERROR 7`, `MP_STATUS_UNSUCCESSFUL 8`. usbport treats any
nonzero `StartController` return as failure [usbport/pnp.c:856].
Root-hub statuses: `RH_STATUS_SUCCESS 0`, `RH_STATUS_NO_CHANGES 1`,
`RH_STATUS_UNSUCCESSFUL 2`.

The two families are not interchangeable, and the trap is `MP_STATUS_FAILURE`.
For root-hub endpoint-0 control operations whose callback result is assigned
to `MPStatus`, usbport puts that result through `USBPORT_MPStatusToRHStatus`
[roothub.c:18-31], which is
`if (MPStatus) { RHStatus = (MPStatus != MP_STATUS_FAILURE); ++RHStatus; }`.

That is five call sites in the ReactOS sources; the shipping binaries have four
machine-code callers in SP4 and three in NUSB (see the root-hub block in
section 4). Four are in `USBPORT_RootHubClassCommand`: GET_STATUS via
`RH_GetPortStatus`/`RH_GetHubStatus` [roothub.c:166], and the SET/CLEAR_FEATURE
families [roothub.c:185, 241, 287]. The fifth is in
`USBPORT_RootHubStandardCommand`, the standard device GET_STATUS handled by
`RH_GetStatus` [roothub.c:412]. That last one is the reason the scope is
"endpoint 0" rather than "class": `RH_GetStatus` is reached through the
standard path and is subject to the same mapping.

| Miniport returns | usbport sees | Effect on the endpoint-0 request |
|---|---|---|
| `MP_STATUS_SUCCESS` (0) | `RH_STATUS_SUCCESS` | completed, `USBD_STATUS_SUCCESS` |
| `MP_STATUS_FAILURE` (1) | `RH_STATUS_NO_CHANGES` | not completed at all: this worker invocation leaves the transfer queued. What happens next (URB timeout, cancellation, teardown) is outside the slice that was read; the miniport has failed to fail |
| anything else (2-8) | `RH_STATUS_UNSUCCESSFUL` | completed, `USBD_STATUS_STALL_PID` |

The completion test is `if (RHStatus != RH_STATUS_NO_CHANGES)` in the
root-hub endpoint worker [roothub.c:726-740]. So the intuitive "return
failure" is the one value that produces a hang rather than an error. **A
callback reached through one of those five sites that means "I do not support
this" must return `MP_STATUS_NOT_SUPPORTED` (6).** This table does not
describe the root-hub status-change endpoint, which tests both
`RH_GetPortStatus` and `RH_GetHubStatus` directly, the USB2
`SET_FEATURE(PORT_POWER)` helper, or the startup power/chirp paths; those
paths discard or bypass the callback result as recorded on their callback
rows in section 4.

### Misc

- `USBPORT_MAX_DEVICE_ADDRESS 127` [usbmport.h:650]; usbport allocates device
  addresses itself from a bitmap [usbport/device.c:1290-1312].
- `USBPORT_TRANSFER_DIRECTION_OUT 1` [usbmport.h:649].
- Resources type bits (for `USBPORT_RESOURCES.ResourcesTypes`):
  `PORT 1, INTERRUPT 2, MEMORY 4` [usbmport.h:39-42]. EHCI refuses to start
  unless the expected bits are present [usbehci.c:1176-1180].
- `USBPORT_INVALIDATE_CONTROLLER_RESET 1 / SURPRISE_REMOVE 2 /
  SOFT_INTERRUPT 3` - the `Type` argument of `UsbPortInvalidateController`
  [usbmport.h:489-491].

## 3. USBPORT_REGISTRATION_PACKET layout (x86)

Transcribed from [usbmport.h:544-634]. On x86 every field is 4 bytes
(`ULONG`/`SIZE_T`/function pointer), so offsets are mechanical.
`sizeof(USBPORT_REGISTRATION_PACKET) = 0x13C (316)`. The wrapper
`USBPORT_MINIPORT_INTERFACE` (usbport-internal: DriverObject 0x00,
LIST_ENTRY 0x04, DriverUnload 0x0C, Version 0x10, Packet 0x14) is
`C_ASSERT`ed at `32 + 76*sizeof(PVOID)` = 336 bytes on x86
[usbmport.h:639-647] - a useful cross-check against the binary's copy size
when disassembling `USBPORT_RegisterUSBPortDriver`.

That wrapper layout is not the same in every build, and only the wrapper
differs. The ReactOS/Win2000/XP shape above puts the packet at +0x14; NUSB's
`USBPORT.SYS` puts it at +0x10, having no `Version` field there. So an
indirect call read out of a disassembly must be converted to a packet offset
per binary: `interface+0x13C` and `interface+0x138` are the same slot 0x128
in different builds. Worked example, discriminators and evidence:
"`FlushInterrupts`: the call site the mirror does not have", below. Nothing
about the packet itself changes; it was confirmed identical across all three
builds.

Fields the miniport fills before registering ("in"), and pointers usbport
writes back ("out"):

| Offset | Field | Dir | xhci98.sys value / note |
|---|---|---|---|
| 0x00 | `MiniPortVersion` | in | `USB_MINIPORT_VERSION_EHCI` first probe (see section 1) |
| 0x04 | `MiniPortFlags` | in | usbehci's set (section 2) |
| 0x08 | `MiniPortBusBandwidth` | in | `TOTAL_USB20_BUS_BANDWIDTH` |
| 0x0C | `Reserved1` | - | sentinel-fill for the spike |
| 0x10 | `MiniPortExtensionSize` | in | `sizeof(XHCI_EXTENSION)` |
| 0x14 | `MiniPortEndpointSize` | in | `sizeof(XHCI_ENDPOINT)` |
| 0x18 | `MiniPortTransferSize` | in | `sizeof(XHCI_TRANSFER)` |
| 0x1C | `Reserved2` | - | sentinel-fill |
| 0x20 | `Reserved3` | - | sentinel-fill |
| 0x24 | `MiniPortResourcesSize` | in | Controller common-buffer block: DCBAA + cmd ring + ERST + event ring + scratchpad (delivered via `USBPORT_RESOURCES.StartVA/StartPA`) |
| 0x28 | `OpenEndpoint` | in | 26 miniport callbacks, in declaration order |
| 0x2C | `ReopenEndpoint` | in | |
| 0x30 | `QueryEndpointRequirements` | in | |
| 0x34 | `CloseEndpoint` | in | |
| 0x38 | `StartController` | in | |
| 0x3C | `StopController` | in | |
| 0x40 | `SuspendController` | in | |
| 0x44 | `ResumeController` | in | |
| 0x48 | `InterruptService` | in | |
| 0x4C | `InterruptDpc` | in | |
| 0x50 | `SubmitTransfer` | in | |
| 0x54 | `SubmitIsoTransfer` | in | |
| 0x58 | `AbortTransfer` | in | |
| 0x5C | `GetEndpointState` | in | |
| 0x60 | `SetEndpointState` | in | |
| 0x64 | `PollEndpoint` | in | |
| 0x68 | `CheckController` | in | |
| 0x6C | `Get32BitFrameNumber` | in | |
| 0x70 | `InterruptNextSOF` | in | |
| 0x74 | `EnableInterrupts` | in | |
| 0x78 | `DisableInterrupts` | in | |
| 0x7C | `PollController` | in | |
| 0x80 | `SetEndpointDataToggle` | in | |
| 0x84 | `GetEndpointStatus` | in | |
| 0x88 | `SetEndpointStatus` | in | |
| 0x8C | `ResetController` | in | |
| 0x90 | `RH_GetRootHubData` | in | 18 root-hub callbacks |
| 0x94 | `RH_GetStatus` | in | |
| 0x98 | `RH_GetPortStatus` | in | |
| 0x9C | `RH_GetHubStatus` | in | |
| 0xA0 | `RH_SetFeaturePortReset` | in | |
| 0xA4 | `RH_SetFeaturePortPower` | in | |
| 0xA8 | `RH_SetFeaturePortEnable` | in | |
| 0xAC | `RH_SetFeaturePortSuspend` | in | |
| 0xB0 | `RH_ClearFeaturePortEnable` | in | |
| 0xB4 | `RH_ClearFeaturePortPower` | in | |
| 0xB8 | `RH_ClearFeaturePortSuspend` | in | |
| 0xBC | `RH_ClearFeaturePortEnableChange` | in | |
| 0xC0 | `RH_ClearFeaturePortConnectChange` | in | |
| 0xC4 | `RH_ClearFeaturePortResetChange` | in | |
| 0xC8 | `RH_ClearFeaturePortSuspendChange` | in | |
| 0xCC | `RH_ClearFeaturePortOvercurrentChange` | in | |
| 0xD0 | `RH_DisableIrq` | in | |
| 0xD4 | `RH_EnableIrq` | in | |
| 0xD8 | `StartSendOnePacket` | in | debug single-packet path |
| 0xDC | `EndSendOnePacket` | in | |
| 0xE0 | `PassThru` | in | |
| 0xE4 | `UsbPortDbgPrint` | out | 16 usbport services, written by the register call |
| 0xE8 | `UsbPortTestDebugBreak` | out | |
| 0xEC | `UsbPortAssertFailure` | out | |
| 0xF0 | `UsbPortGetMiniportRegistryKeyValue` | out | |
| 0xF4 | `UsbPortInvalidateRootHub` | out | |
| 0xF8 | `UsbPortInvalidateEndpoint` | out | |
| 0xFC | `UsbPortCompleteTransfer` | out | |
| 0x100 | `UsbPortCompleteIsoTransfer` | out | |
| 0x104 | `UsbPortLogEntry` | out | |
| 0x108 | `UsbPortGetMappedVirtualAddress` | out | |
| 0x10C | `UsbPortRequestAsyncCallback` | out | |
| 0x110 | `UsbPortReadWriteConfigSpace` | out | |
| 0x114 | `UsbPortWait` | out | |
| 0x118 | `UsbPortInvalidateController` | out | |
| 0x11C | `UsbPortBugCheck` | out | |
| 0x120 | `UsbPortNotifyDoubleBuffer` | out | |
| 0x124 | `RebalanceEndpoint` | in | tail group - see risk note below |
| 0x128 | `FlushInterrupts` | in | |
| 0x12C | `RH_ChirpRootPort` | in | Populated only at interface Version >= 200 in both builds: registration copies 0x12C bytes below, 0x13C at or above. SP4's call site re-checks `Version` [roothub.c:1032-1042]; NUSB's does not, testing only an internal `+0x48` bit and calling the slot with no null check, so a sub-200 registration is a null call there. See the root-hub block in section 4 |
| 0x130 | `TakePortControl` | in | |
| 0x134 | `Reserved4` | - | sentinel-fill |
| 0x138 | `Reserved5` | - | sentinel-fill |

The tail group at 0x124-0x138 (`RebalanceEndpoint`..`Reserved5`) was the
highest-risk region before the binaries were read: ReactOS documents the
XP-era packet, and a 5.0.2195 binary could have ended earlier or ordered the
tail differently. The leading 0x00-0xE0 region (sizes, then miniport and
root-hub callbacks in this order) is corroborated by the whole ReactOS call
graph. Fill all five `Reserved*` fields with distinctive sentinels.

The whole table, tail included, is confirmed against the shipping binaries.
Two independent reads agree:

1. `USBPORT_RegisterUSBPortDriver` copies `0x13C` (316) bytes when
   `Version >= 200` and `0x12C` (300) otherwise. So `sizeof` is the 316 above,
   and the 16-byte difference is the last four fields (0x12C
   `RH_ChirpRootPort`, 0x130 `TakePortControl`, 0x134/0x138 `Reserved4/5`).
   That is how the binary implements the "chirp only at Version >= 200" rule
   ReactOS expresses as a field test. Identical in the NUSB 5652, SP4 6681,
   and XP SP3 5512 builds. On NUSB this copy gate is the whole of the
   protection: its chirp call site does not re-test `Version` and does not
   null-check the slot, so registering below 200 there is a null call, not a
   skipped one (section 4, root-hub block).
2. Each shipping `usbehci.sys` `DriverEntry` writes its static packet at
   every "in" offset listed above from 0x00 through 0x130 and at no other
   offset: nothing at 0x0C, 0x1C, 0x20, 0x134, 0x138 (the `Reserved` fields)
   and nothing in the 0xE4-0x120 service block. Offset 0x8C
   (`ResetController`) is the one "in" slot every shipping EHCI miniport
   leaves NULL. That confirms the slot is present but does not prove usbport
   null-checks it: EHCI-specific gating or an unexercised path could explain
   the observation. `xhci98.sys` supplies the callback, and section 4 records
   what the binaries do with it.

Observed field values in the two 2195.x `usbehci.sys` builds:
`MiniPortVersion = 3`, `MiniPortFlags = 0x95`, `MiniPortBusBandwidth =
0x61A80`, `MiniPortExtensionSize = 0x184`, `MiniPortEndpointSize = 0xA0`,
`MiniPortTransferSize = 0x34`, `MiniPortResourcesSize = 0x2C800`. XP's
differs only in the sizes and in `MiniPortFlags = 0x295`.

The reserved-sentinel advice still stands. It now guards against a
behavioural surprise (usbport reading a field ReactOS calls reserved) rather
than against a layout mismatch.

## 4. Callback signatures and observed contracts

All typedefs are `NTAPI` (stdcall) except `PUSBPORT_DBG_PRINT`, which is
cdecl varargs (its typedef carries no NTAPI) [usbmport.h:398-403]. Getting
that one wrong corrupts the stack on every debug print.

The opaque `PVOID` arguments follow one convention everywhere:

- Arg 1 (`PVOID`): the miniport device extension (`MiniPortExt`). usbport
  allocates it inside its FDO extension, directly after its own
  `USBPORT_DEVICE_EXTENSION`, and zeroes it at start-device
  [usbport/pnp.c:809-812]. The services recover the FDO by subtracting
  `sizeof(USBPORT_DEVICE_EXTENSION)` from it [usbport/roothub.c:926-927,
  usbport.c:2679-2680]; the miniport extension is identity, not a handle.
- Endpoint `PVOID`s: the miniport endpoint extension, which is the memory
  immediately after usbport's own endpoint struct; every call passes
  `Endpoint + 1` [usbport/endpoint.c:766-768, 1221-1223, 1580-1584]. usbport
  zeroes it on (re)open [endpoint.c:1232-1233].
- Transfer `PVOID`s: the miniport transfer extension
  (`Transfer->MiniportTransfer`), `MiniPortTransferSize` bytes
  [usbport/usbport.c:2635, endpoint.c:1583].

### Controller lifecycle

| Callback | Signature [usbmport.h line] | Observed contract |
|---|---|---|
| `StartController` | `MPSTATUS (ext, PUSBPORT_RESOURCES)` [176-179] | PASSIVE_LEVEL (EHCI sleeps 200 ms inside via `UsbPortWait` [usbehci.c:1263-1272]). Called from usbport's start-device path after `IoConnectInterrupt` and after the `MiniPortResourcesSize` common buffer is placed in `StartVA/StartPA` [pnp.c:788-837]. Register base arrives as `Resources->ResourceBase` (already mapped VA) [usbehci.c:1182]. Return 0 for success. Set `Resources->LegacySupport` if a BIOS-legacy capability was found - usbport records it in the registry as `DetectedLegacyBIOS` [pnp.c:839-854]. On success usbport immediately calls `EnableInterrupts` via its locked wrapper [pnp.c:875-879] and starts a 500 ms timer [pnp.c:881-882] |
| `StopController` | `VOID (ext, BOOLEAN IsDoDisableInterrupts)` [181-184] | Called with TRUE from the power-down path [power.c:194] |
| `SuspendController` | `VOID (ext)` [186-187] | Win98: minimal (no real power management). Win2000: actually invoked - implement and verify there |
| `ResumeController` | `MPSTATUS (ext)` [189-190] | |
| `InterruptService` | `BOOLEAN (ext)` [192-193] | Real DIRQL ISR body. usbport's ISR wrapper only calls it while its interrupt-enabled flags are set, and queues the DPC only on TRUE [usbport.c:1110-1142]. Claim only if USBSTS.EINT proves ownership |
| `InterruptDpc` | `VOID (ext, BOOLEAN EnableInterrupts)` [195-198] | DISPATCH_LEVEL under `MiniportInterruptsSpinLock` [usbport.c:1089-1095]. The BOOLEAN is usbport's "interrupts should be enabled" flag - re-arm controller interrupt enables per it. This is where the event ring is drained; complete transfers with `UsbPortCompleteTransfer`, report port changes with `UsbPortInvalidateRootHub` (EHCI DPC does exactly this [usbehci.c:1426-1521]) |
| `EnableInterrupts` / `DisableInterrupts` | `VOID (ext)` [248-252] | Under `MiniportSpinLock` unless `NOT_LOCK_INT` [usbport.c:553-586]. Called on success of `StartController` [pnp.c:876-878] and around the restart of a controller whose `ResumeController` failed [power.c:192, 212], and not after a successful resume, so the miniport's own resume has to restore the enables. xHCI: clear USBCMD.INTE then IMAN.IE on the way down; on the way up release `ERDP.EHB` first and then set IMAN.IE and USBCMD.INTE, acknowledging nothing (`docs/contributing/implementation-invariants.md`, "Interrupt Ordering") |
| `CheckController` | `VOID (ext)` [239-240] | Periodic health check, called from the worker thread under `MiniportSpinLock` [usbport.c:1177-1184] and from timer/root-hub paths [usbport.c:1642, roothub.c:682]. Check USBSTS.HCE/HSE here; on fatal error call `UsbPortInvalidateController(ext, USBPORT_INVALIDATE_CONTROLLER_RESET)` |
| `Get32BitFrameNumber` | `ULONG (ext)` [242-243] | Called frequently under `MiniportSpinLock` (state stamps [endpoint.c:410], iso bookkeeping [endpoint.c:1483], URB frame queries [urb.c:58]). xHCI: `MFINDEX >> 3` + software rollover extension. What this driver publishes is a delta, not the register. MFINDEX is eleven bits of frame and restarts at zero after HCRST, so an absolute reading goes backwards twice a second. usbport's post-open wait is uncapped and compares against a frame stamped before a suspend, so a reader that froze on a halted or suspended controller (Win98 idle-suspends within about half a second of every start, and MFINDEX stops on a halted xHC) would hang the enumerating thread. A controller that cannot be read is therefore answered with an increment, the safe direction since nothing is in flight on a halted xHC. The published number is also kept congruent to MFINDEX's Frame Index, because usbport stamps every isochronous packet from this callback and a Frame ID derived from a stamp is only legal if the two axes agree: the resync after a stall advances the number forward to the next value congruent to the register (at most 2,047 frames, never backwards, so monotonicity is untouched). `FrameCongruent` says when that holds and `FrameResyncSkew` measures how much axis the stall path invented |
| `InterruptNextSOF` | `VOID (ext)` [245-246] | No call site in the ReactOS mirror, but both shipping builds have two; see "`InterruptNextSOF`: what it asks for, and what happens when nothing answers" below. One argument, return value ignored, DISPATCH_LEVEL holding `MiniportSpinLock` and nothing else. Both sites belong to the endpoint state-change machine: the tail of `USBPORT_SetEndpointState`, and the walker that drains the state-change list when it cannot yet retire the head. usbport never waits on it (the same list is drained unconditionally by a self-rearming 500 ms timer DPC), so a do-nothing stub costs latency and nothing else. What the drain depends on is `Get32BitFrameNumber` advancing, not this callback |
| `PollController` | `VOID (ext)` [254-255] | No call site found in mirror (polling-mode path). Benign stub |
| `ResetController` | `VOID (ext)` [274-275] | Paired with `UsbPortInvalidateController(RESET)`. Confirmed in both shipping builds, and it is not a PASSIVE-level re-init slot: the invalidation queues a DPC, and that DPC takes `KfAcquireSpinLock` on `FdoExtension+0x288` (NUSB) / `+0x28C` (SP4), calls this slot, and releases it (NUSB `00011AC4`/`00011B36`/`00011B42`, SP4 `00011B80`/`00011BF2`/`00011BFE`). So it runs at DISPATCH_LEVEL inside one of usbport's spin locks, where `KeDelayExecutionThread` (i.e. `UsbPortWait`) is illegal. usbport does nothing after the call but release the lock and drop a busy reference, so a miniport may decline the work; it may not reinitialize here. And usbport never comes back: no timer, watchdog or transfer path in either image reaches `StopController`/`StartController`; only a PnP or power IRP does (the census two notes below). Nor does usbport ever request the reset itself; the only producer of `Type == 1` is a miniport. See the notes below |
| `FlushInterrupts` | `VOID (ext)` [515-516] | No call site in the ReactOS mirror, but all three shipping builds have one; see "`FlushInterrupts`: the call site the mirror does not have" below. Called from the device-power completion routine on the successful `PowerDeviceD0` path, before `TakePortControl` and before resume processing, holding neither miniport lock. EHCI implements it as "ack all pending status bits" [usbehci.c:3579-3593]. The xHCI miniport does nothing to the hardware, because any acknowledgement it could make is inseparable from an `ERDP` write, and this is the one caller that cannot hold the controller lock every `ERDP` writer holds (design doc 05 section 5) while the DPC can be running on another CPU |
| `TakePortControl` | `VOID (ext)` [523-524] | EHCI leaves it unimplemented [usbehci.c:3595-3600]. Companion-controller handback - N/A for xHCI. Reached from the same D0 completion as `FlushInterrupts`, gated on `MiniPortFlags & USB_MINIPORT_FLAGS_USB2` in all three builds and additionally on interface `Version >= 200` in the Win2000/XP builds |

#### `UsbPortInvalidateController(RESET)`: real in the binaries, a `FIXME` in the mirror

Static, confirmed by disassembly of the NUSB and Windows 2000 SP4 builds;
extracts in `tools/{nusb,win2ksp4}-extracted/usbport-invalidate-disasm.txt`.

ReactOS's `USBPORT_InvalidateControllerHandler` answers
`USBPORT_INVALIDATE_CONTROLLER_RESET` with
`DPRINT1("... UNIMPLEMENTED. FIXME.")` and nothing else. Both shipping builds
implement it, identically in shape: `KeInsertQueueDpc` on a DPC object
inside the `FdoExtension` (+0x580 NUSB, +0x588 SP4), then a flag bit (bit 2 of
the byte at +0x1C2 / +0x1C6), then a busy reference on the FDO. The work is
queued, not performed inline, and the miniport is not called back
synchronously.

That matters for locking, because this driver requests a reset from
`InterruptDpc`, where usbport holds `MiniportInterruptsSpinLock`. That lock is
`FdoExtension+0x480` in the NUSB build, read off the bracket around the
`InterruptDpc` call itself (`KefAcquireSpinLockAtDpcLevel` at `00029AD6`, the
callback at `00029B4A`, `KefReleaseSpinLockFromDpcLevel` at `00029B50`). The
RESET branch touches only the DPC object and a reference count under
`FdoExtension+0xA0`, so it cannot deadlock against it.

The queued DPC calls the miniport's `ResetController` at DISPATCH_LEVEL
holding a third lock (`FdoExtension+0x288` NUSB, `+0x28C` SP4), and that
decides what the callback may contain. Neither `UsbPortWait` nor any other
sleeping operation is legal there, so the reinitialization a name like
"ResetController" suggests cannot happen in it. After the call usbport only
releases the lock and drops the busy reference, so a miniport that declines
the work breaks no state machine of usbport's.

This project's miniport masks its interrupt enables, marks the controller failed, and leaves recovery to a
stop/start, the only PASSIVE-level path a miniport has. The next subsection
settles what that stop/start costs: usbport never issues one on its own, so
"leaves recovery to a stop/start" means "leaves recovery to the next PnP or
power transition".

#### After `ResetController` returns, usbport does nothing else: the `StartController`/`StopController` census

Static, both shipping builds. The commands were
`tools\MSVC600\VC98\Bin\DUMPBIN.EXE /disasm tools\nusb-extracted\USBPORT.SYS`
(5.00.2195.5652) and the same over `tools\win2ksp4-extracted\USBPORT.SYS`
(5.00.2195.6681), each redirected to a listing. Nothing was executed.

The subsection above says usbport does nothing in the reset DPC after the
call. The question a miniport's failure path depends on is whether usbport
comes back later, on a timer, from a watchdog, or from the next failed
transfer, and restarts the controller. It does not.

The method is a census rather than a search. usbport reaches every miniport
slot through the interface pointer it keeps at `FdoExtension+0x104` in both
builds, and the call is always the pair
`mov <reg>,dword ptr [<fdoext>+00000104h]` ... `call dword ptr [<reg>+disp]`,
with `disp = packetOffset + wrapperPrefix` (0x10 on NUSB, 0x14 on Win2000/XP,
the prefix section 3 records). Enumerating that pair over the whole image
yields every slot call in the binary.

Done that way, `StartController`
(packet 0x38) has three call sites per build, `StopController` (packet 0x3C)
three, and `ResetController` (packet 0x8C) one, the reset DPC of the
subsection above. There is no fourth. The census also disposes of the one
look-alike: an apparent `call dword ptr [esi+50h]` in the SP4 image
(`00027F5D`) is not on a `+0x104` base and is not `StopController`.

| Routine | NUSB | SP4 | What it is, read off what else it calls |
|---|---|---|---|
| start-device / stop-device | `Start` `00011068`; `Stop` `000104EF` in the paired teardown routine | `Start` `0001110B`; `Stop` `000104F7` | the PnP start path and its twin. The `Start` is preceded by the `rep stos` that zeroes the miniport extension, sized from packet 0x10 |
| power-down / power-up | `Stop` `0001E688` (fn `0001E532`); `Start` `0001E876` (fn `0001E7CA`) | `Stop` `0001ED56` (fn `0001EC00`); `Start` `0001EF44` (fn `0001EE98`) | the D-state pair; each `Stop` is preceded by `DisableInterrupts` (packet 0x78) |
| resume-with-restart | fn `0001EADA`: `Stop` `0001EC6F`, `Start` `0001ECD4` | fn `0001F1FE`: `Stop` `0001F387`, `Start` `0001F3EC` | `USBPORT_ResumeController`. It calls `ResumeController` (packet 0x44) at NUSB `0001EB91` / SP4 `0001F2AF` and, only if that returns nonzero, falls through to `DisableInterrupts` -> `StopController` -> zero the extension and the resources block -> `StartController` -> `EnableInterrupts`. This is the `[power.c:192, 212]` restart the mirror shows. Its whole body is skipped unless the "suspended" bit is set (NUSB `[FdoExt+0x1C1] & 1` at `0001EAE6`) |

Every one of those six is rooted in an IRP. Both builds have a single
dispatch routine (NUSB `0001134C`, SP4 `000113F4`) that switches on the
`IO_STACK_LOCATION`'s `MajorFunction` byte, decoded off its own subtraction
chain (`sub eax,ecx` with `ecx = 0`, then `dec`, `dec`, `sub 0Ch`, `dec`,
`sub 7`, `dec`, `sub 4`, giving 0x00, 0x02, 0x0E, 0x0F, 0x16, 0x17, 0x1B and
`STATUS_NOT_IMPLEMENTED` for anything else). The `IRP_MJ_PNP` (0x1B) and
`IRP_MJ_POWER` (0x16) cases each split FDO from PDO on the `'HFDO'`/`'RPDO'`
extension signature tested at entry - NUSB's cases begin at `0001143B` and
`0001146F`, SP4's at `000114E3` and `00011517`:

| | NUSB | SP4 |
|---|---|---|
| PnP FDO handler | `0001C6AE` (called at `0001144A`) | `0001CB12` (at `000114F2`) |
| PnP PDO handler | `0001CE7E` (at `00011443`) | `0001D2E2` (at `000114EB`) |
| power FDO handler | `0001D9E4` (at `0001147E`) | `0001E09C` (at `00011526`) |
| power PDO handler | `0001DB8B` (at `00011477`) | `0001E24F` (at `0001151F`) |

The direct-call chains, per routine. Every one of these edges is a
`call <absolute>`, so each is checkable by searching the listing for that one
instruction:

| Routine holding the slot call | Its direct callers, up to a handler above |
|---|---|
| stop-device, NUSB `00010306` / SP4 `00010306` | NUSB: PnP FDO at `0001C822`; also from start-device at `00011230`. SP4: PnP FDO at `0001CC86`; also from start-device at `000112D8` |
| start-device, NUSB `000106A4` / SP4 `000106EE` | NUSB: `0002D390` at `0002D3C0`, itself PnP FDO at `0001CA24`. SP4: `0002DC44` at `0002DC6F`, itself PnP FDO at `0001CE88` |
| power-down, NUSB `0001E532` / SP4 `0001EC00` | NUSB: `0001D4DC` at `0001D589` and `0001D6CC`, `0001D8DC` at `0001D9D5`, both under power FDO at `0001DB1F`/`0001DB26`. SP4: `0001DB5A` at `0001DC07`/`0001DD55` and `0001DF64` at `0001E08D`, under power FDO at `0001E1D7`/`0001E1DE` |
| power-up, NUSB `0001E7CA` / SP4 `0001EE98` | the D0 completion routine, NUSB `000153FE` at `0001548A` / SP4 `000155C0` at `00015616` - see the note below |
| resume-with-restart, NUSB `0001EADA` / SP4 `0001F1FE` | NUSB: `0001D248` at `0001D2A3` (PnP PDO at `0001CEE9`) and power PDO at `0001DE16`. SP4: `0001D6AC` at `0001D707` (PnP PDO at `0001D34D`), power PDO at `0001E4E5`, and the D0 completion at `0001562E` |

The D0 completion routine is the one entry that is not called directly, and
it is still the power path. In NUSB it is `0001D344`: `ret 14h` at `0001D4D8`
(five stack arguments), a `"PwCp"` tag written at `0001D376`, and it contains
the `FlushInterrupts` call at `0001D419` that the `FlushInterrupts` subsection
below attributes to the device-power completion routine. Its address is handed to a power
request as an immediate - `0001D861: push 1D344h`, then
`0001D871: call dword ptr ds:[0002C358h]`. It in turn reaches `000153FE` two
ways: directly through `000153AE`, and through a work stub at `0001538A` whose
address `000153AE` stores at `000153DE` (`mov dword ptr [esi+8],1538Ah`).

SP4 has one entrance NUSB does not, and it is a lever rather than a hazard:
SP4's HCD IOCTL switch `0002E8E6` (dispatch case `00011552`) reaches the
resume-with-restart through the USBUSER dispatcher `0002814C` (the same one
the "Debug / single-packet" section traces): `00029340` -> `000291FA` ->
`0001D92C` (called with `2` at `000292D1`, a controller power-transition
request) -> `00015570` -> `0001554C` -> the D0 completion `000155C0`. So on
Windows 2000/XP a user-mode USBUSER request can ask for the power transition
a stop/start rides on. NUSB's HCD IOCTL switch (`0002E00C`) does not reach
any of the six.

What this method does and does not establish. The slot-call census above is
exhaustive for the image: it is a whole-image enumeration of one instruction
pair, and the conclusion rests on it. The caller chains are exhaustive for
direct `call <absolute>` edges. Neither covers register- or memory-indirect
transfers. A transitive whole-image negative was attempted and is not
claimed: an automated closure that also followed address-shaped immediates
produced a false edge on SP4 (`000149FF: test eax,20300h`, a flag mask read
as a code address) which alone made the 500 ms timer appear to reach a
lifecycle slot. So:

- claimed: there are six slot call sites and no more; each sits in one of
  the five routines above; every direct caller chain out of them terminates at
  a PnP or power handler (plus SP4's IOCTL-driven power transition); and the
  reset DPC's own body, read instruction by instruction, calls the slot,
  releases the lock, drops the busy reference and returns, arming nothing;
- not claimed: that no indirect transfer anywhere in either image could
  reach one of those five routines. Establishing that would need every
  register/memory-indirect call, tail jump and callback registration in both
  images classified, which has not been done.

That is enough for the design question, which is not "could some path exist"
but "does usbport arrange a restart on its own". It does not: the reset DPC
arms nothing, and the five routines are entered from PnP and power
transitions that something outside usbport has to initiate.

The 500 ms timer learns nothing it could act on. It calls
`PollController` (packet 0x7C, NUSB `00014818` and `000148EC`; SP4 `000149C8`
and `00014A9D`) and `CheckController` (packet 0x68, NUSB `000148BD` in routine
`000147CE`, SP4 `00014A6E` in routine `0001497E`, both `ret 10h`).

`CheckController` is a `VOID` slot (`PHCI_CHECK_CONTROLLER` in
`src/xhci_usbport.h`), so there is no return value for usbport to act on, and
the instructions after the call confirm it does not look: NUSB
`000148C0: mov dl,byte ptr [ebp-1]`, `000148C3: lea edi,[esi+288h]`,
`000148C9: mov ecx,edi`, `000148CB: call dword ptr ds:[0002C24Ch]`, the lock
release, with `EAX` never read. SP4 is the same shape at `00014A71`, `00014A74`,
`00014A7A`, `00014A7C`.

A miniport that wants usbport to know it is sick has to say so through
`UsbPortInvalidateController`; there is no health verdict usbport collects.

Consequence for a miniport. A miniport that answers `ResetController` by
parking itself has parked itself until the next PnP or power transition. On a
machine nobody is suspending and nobody is disabling in Device Manager, that
is until the next boot. usbport will keep calling `CheckController`,
`PollController`, `Get32BitFrameNumber` and the root-hub slots forever and
will conclude nothing from any of them. **A miniport must therefore either
recover in place from `ResetController`, or not enter a state it cannot leave
from there.**

#### What makes usbport call `ResetController` at all: only the miniport itself

Static, both builds, from the same listings. The reset DPC is queued from
one place only, the `RESET` branch of `USBPORT_InvalidateController` (NUSB
`00011A0A`, SP4 `00011AB2`), a `ret 8` routine switching on its `Type`
argument with the same `dec`-chain shape:

| `Type` | Name in `src/xhci_usbport.h` | NUSB | SP4 | What it does |
|---|---|---|---|---|
| 0 | - | `00011A79` | `00011B34` | logs the string at NUSB `000119F0` - "...Miniport Raised Exception" - through the bugcheck helper |
| 1 | `USBPORT_INVALIDATE_CONTROLLER_RESET` | `00011A54` | `00011B0F` | `KeInsertQueueDpc` on the DPC at `FdoExtension+0x580` / `+0x588`; only if that returns TRUE (`00011A63: test al,al` / SP4 `00011B1E`) does it set bit 2 of the byte at `+0x1C2` / `+0x1C6` and take a busy reference. An insertion onto an already-queued DPC falls straight out, so a second request while one is pending adds nothing |
| 2 | `..._SURPRISE_REMOVE` | `00011A38` | `00011AF3` | sets bit 1 of that same byte first, then queues the DPC at `+0x560` / `+0x568`, and takes the same busy reference only on a successful insertion (`00011A4E` / SP4 `00011B09`) |
| 3 | `..._SOFT_INTERRUPT` | `00011A27` | `00011ACF` | queues a DPC (`+0x520` on NUSB; SP4 takes a different, `+0x114`-based route) |

NUSB `00011A8A` / SP4 `00011B46` is the wrapper the registration packet
publishes to the miniport: it takes `(MiniPortExt, Type)`, recovers the FDO from
`[MiniPortExt-0x7DC]` (NUSB) / `[MiniPortExt-0x844]` (SP4) and calls the routine
above.

usbport's own internal use of that routine is a single site, and it is not
`RESET`: NUSB `0001CB2A` / SP4 `0001CF8E`, in the PnP FDO handler on the
surprise-removal path, pushes 2. So nothing inside usbport ever requests a
controller reset. The only producer of `Type == 1` is a miniport calling the
published service. In this driver that is `XhciRequestControllerReset`
(`src/xhci_cmd.c`), on a command timeout, so the whole `ResetController` loop
is this driver asking usbport to call this driver back, with usbport
contributing the DPC and the lock and nothing else.

The reset DPC's own body, for completeness: it increments a reset counter in the
FDO extension (NUSB `+0x680`, SP4 `+0x6DC`), clears the bit 2 it was queued
with, takes the spin lock, writes a `"rset"` tag into usbport's own log, calls
the slot, releases the lock, drops the busy reference and returns.

#### `UsbPortRequestAsyncCallback`: what its return value is worth, and what it costs

Static, confirmed against NUSB's `000278EE` (packet slot 0x10C) and the
mirror's `USBPORT_RequestAsyncCallback` [usbport.c:2114-2164]:

- It returns 0 on success and 0 when its pool allocation fails. There is no
  value a miniport could branch on, so "every command carries a timeout" is an
  invariant a miniport can honour but cannot verify. The only checkable half is
  whether the service pointer exists at all. Check that before enqueuing, so
  a command that cannot be timed is never issued rather than left on the ring
  forever.
- It keeps no list. Each call allocates a standalone `KTIMER`/`KDPC` pair,
  `KeSetTimer`s it, and frees it in the DPC. Nothing, including usbport's own
  stop path, can cancel one. Every armed callback will fire.
- The DPC that runs it (`0002785E` in NUSB) acquires no spin lock before
  invoking the miniport callback, confirming section 7's claim for the shipping
  build as well as the mirror. It reads `FdoExtension->MiniPortExt` at DPC time,
  so a callback always receives the current extension, which is the hazard
  below.
- Unlike the mirror, the shipping build takes a busy reference on the FDO
  while the timer is armed (the same helper the RESET branch uses, under
  `FdoExtension+0xA0`).

The hazard those facts combine into: usbport calls
`RtlZeroMemory(FdoExtension->MiniPortExt, ...)` immediately before every
`StartController`, on the start-device path [pnp.c] and again on the
failed-resume restart [power.c], with no synchronization against the
uncancellable callbacks it has already handed out. So a callback armed by one
start can fire against a zeroed-then-restarted extension, and any generation
counter kept in that extension has restarted from the same value. A miniport
cannot cancel the callback from its side.

This driver keeps the token that distinguishes one start from the next in its
own image rather than in the extension, and keeps the lock that validates that
token there too. `src/xhci_cmd.c` does that with `xhciStartEpoch` and the
driver-image controller lock created once in `DriverEntry`. Only the callback
pointers are checked before the lock; the extension bracket, epoch,
generation, initialized state and failed state are validated while holding
it. This avoids both an epoch check-then-act and acquiring a lock word that
usbport has zeroed and a restart has re-created.

#### `FlushInterrupts`: the call site the mirror does not have

Static, confirmed by disassembly of all three binaries; extracts in
`tools/{nusb,win2ksp4,winxpsp3}-extracted/usbport-flushinterrupts-disasm.txt`
(host-local; `tools/` is git-ignored, like every other extract cited here).
Regenerate with `dumpbin -disasm` over each `usbport.sys` and search for
`call dword ptr [` at the offsets in the table below; the addresses there are
what makes the finding reproducible without the files.

The caller is each build's device-power completion routine, a
`PREQUEST_POWER_COMPLETE` callback, recognisable by `ret 14h` (five stack
arguments) and by its argument use: `[ebp+18h]` is the `PIO_STATUS_BLOCK` whose
`Status` is `NT_SUCCESS`-tested before anything else happens, `[ebp+10h]` is the
`POWER_STATE` compared against `1` (`PowerDeviceD0`), and `[ebp+14h]` is the FDO
whose `DeviceExtension` supplies `MiniPortExt`. On that path it calls
`FlushInterrupts(MiniPortExt)` with one argument, then the `USB2`-gated
`TakePortControl`, then resume processing, then sets a "done" flag that the same
routine tests on entry, so the call happens once per D0 transition.

| Build | Instruction | Interface offset | Packet offset |
|---|---|---|---|
| NUSB 3.3 `USBPORT.SYS` 5.00.2195.5652 | `0001D419: call dword ptr [eax+00000138h]` | 0x138 | 0x128 |
| Windows 2000 SP4 `USBPORT.SYS` 5.00.2195.6681 | `0001DA90: call dword ptr [eax+0000013Ch]` | 0x13C | 0x128 |
| Windows XP SP3 `usbport.sys` | `0001EFE8: call dword ptr [eax+0000013Ch]` | 0x13C | 0x128 |

The interface offset is not the packet offset, and it is not the same in every
build. `USBPORT_MINIPORT_INTERFACE` places the packet at +0x14 in the
Win2000/XP builds (as transcribed above from [usbmport.h:639-647]) but at
+0x10 in NUSB's, which lacks the `Version` field at 0x10. The same
registration-packet slot 0x128 is therefore called at two different interface
offsets.

Two discriminators inside the same extract settle it independently of any
assumption: the `MiniPortFlags` test a few instructions below the call reads
`[esi+18h]` in the Win2000/XP builds and `[esi+14h]` in NUSB's (packet +0x04
either way), and only the Win2000/XP builds compare an interface `Version` at
`[esi+10h]` against `0C8h` before `TakePortControl`. Argument count is the third:
slot 0x124 (`RebalanceEndpoint`, three arguments, and under a spin lock) is
what sits at the other of the two offsets in each binary.

None of this changes the registration packet, which is what the miniport
declares and which was confirmed identical across the three builds. It changes
only usbport's private wrapper, but it is the kind of difference that makes
"the same offset in both builds" an unsafe shortcut when reading a call site
out of a disassembly.

The routine acquires no spin lock at all, neither `MiniportSpinLock` nor
`MiniportInterruptsSpinLock`; `grep KfAcquireSpinLock` over any of the three
extracts returns nothing. So `FlushInterrupts` can run concurrently with
`InterruptDpc` on SMP. So `xhci98.sys` implements it as a counter and nothing
else (`XhciFlushInterrupts` in `src/xhci_evt.c`): the acknowledgement it would
otherwise make is inseparable from an `ERDP` write, and this caller cannot
hold the controller lock every `ERDP` writer holds.

Nothing is lost by that at this call site, even though something may be
pending: `IP` and `EINT` survive a mask by design (the miniport writes `IP` as
0, since it is RW1C). What the mask removes is delivery: `IE` = 0 leaves the Interrupter
"prohibited from generating interrupts" (5.5.2.1, p.391) and `INTE` = 0
disables host system interrupt generation (5.4.1, p.360), so INTx is not
asserted however long `IP` stays set.

The pending state is cleared by the resume that follows, which reinitializes through `HCRST`: "after initial
power-on or HCRST ... all of the Operational and Runtime Registers shall be at
their default values" (4.23.1, p.312), whose defaults put `IP`, `IE`, `EHB`
and `EINT` at 0, and that happens before the enables go back on.

#### `InterruptNextSOF`: what it asks for, and what happens when nothing answers

Static, confirmed by disassembly of both shipping builds; extracts in
`tools/{nusb,win2ksp4}-extracted/usbport-nextsof-disasm.txt` (host-local;
`tools/` is git-ignored). The endpoint-path census below had already
established that slot 0x70 is called by both builds. What follows is from
where, holding what, and, the question that decides whether a stub is legal,
what usbport does next.

Two call sites per build, and both are the endpoint state-change machine. But
`SetEndpointState` has four call sites, and only one of them ends here. The
other three reach the miniport directly under `MiniportSpinLock`, with a
literal state pushed (`4`, `3`, `3`), and queue nothing onto the state-change
list, so they neither stamp a frame nor need a SOF request:

| Build | `SetEndpointState` sites | of which ask for `InterruptNextSOF` |
|---|---|---|
| NUSB 3.3 | `00019BA4` (`[eax+70h]`), `0002437B` (`[ecx+70h]`, state 4), `00024614` (`[edx+70h]`, state 3), `00026FD4` (`[ecx+70h]`, state 3) | `00019BA4` only |
| Windows 2000 SP4 | `00019FB8` (`[eax+74h]`), `000249F9` (`[ecx+74h]`, state 4), `00024C92` (`[edx+74h]`, state 3), `0002765C` (`[ecx+74h]`, state 3) | `00019FB8` only |

Method note: grep for the slot across every base register, not just the one
found first. All four sites load the same wrapper pointer (`devExt+0x104`) but
into `eax`, `ecx` and `edx` in turn, so a pattern anchored on `[eax+` finds one
of four.

| Build | Site A (`USBPORT_SetEndpointState` tail) | Site B (state-change list walker) | Interface offset | Packet offset |
|---|---|---|---|---|
| NUSB 3.3 `USBPORT.SYS` 5.00.2195.5652 | `00019C6B: call dword ptr [eax+00000080h]` | `00029A3E: call dword ptr [eax+00000080h]` | 0x80 | 0x70 |
| Windows 2000 SP4 `USBPORT.SYS` 5.00.2195.6681 | `0001A082: call dword ptr [eax+00000084h]` | `0002A185: call dword ptr [eax+00000084h]` | 0x84 | 0x70 |

The two interface offsets are the same +0x14 / +0x10 wrapper difference the
`FlushInterrupts` subsection documents. The identification is corroborated
inside each extract by usbport's own four-character log tag: the entry
written immediately before either call is `rSOF`, against `setS` at the
`SetEndpointState` call a few instructions earlier and `chgS` in the walker.

The signature is `VOID (ext)`. One argument is pushed (`devExt+0x100`, the
miniport extension); the instruction after the call is the spin-lock release
in both builds and at all four sites, so nothing reads `eax`.

Locks and IRQL. Each site acquires `MiniportSpinLock` (SP4 `devExt+0x28C`,
NUSB `devExt+0x288`) with `KfAcquireSpinLock` for this call alone and
releases it immediately after, so the callback runs at DISPATCH_LEVEL under
one lock: the same one `SetEndpointState` and `Get32BitFrameNumber` are called
under, and the same one this driver's `XhciControllerLock` nests inside. The
endpoint's own locks (`Endpoint+0xD4`, `Endpoint+0xB8`) are released before
the call at both sites, and the state-change list's lock is not held either.
Site B additionally holds the walker's re-entrancy claim (`devExt+0x110`,
initialised to -1 and taken with `InterlockedIncrement`), so a miniport that
re-entered the walker from inside this callback would be refused rather than
recursing.

What the request is for. `USBPORT_SetEndpointState(Endpoint, State)` writes
the requested state to `Endpoint+0x34` (`StateNext`), stamps `Endpoint+0x38`
with `Get32BitFrameNumber()`, appends the endpoint to a state-change list
(`ExfInterlockedInsertTailList`, head `devExt+0x698` on SP4 / `devExt+0x644` on
NUSB, lock `devExt+0x2FC` / `devExt+0x2F8`) and then asks for
`InterruptNextSOF`. The walker pops that list and, per endpoint, reads
`Get32BitFrameNumber()` again and compares:

- strictly greater than the stamp (`ja`, unsigned), or the endpoint carries
  the NUKE flag: the change commits. `Endpoint+0x30` (`StateLast`) takes
  `Endpoint+0x34`, and the endpoint is handed to usbport's per-endpoint worker.
- not yet: the endpoint goes back on the head of the list
  (`ExfInterlockedInsertHeadList`), `InterruptNextSOF` is requested again, and
  the walker abandons the entire pass.

So the callback means "the frame number needs to move on; poke me when it has".

usbport does not wait on the callback, but it does block, at PASSIVE, on the
thing the callback accelerates. There is no event, no timeout, no return value
and no loop at any of the four `InterruptNextSOF` sites (two per build); the
callback is fire and forget.

But SP4 `000256DA` (single caller `00025FA1`, the
endpoint removal path; NUSB's counterpart reads `[esi+30h]` at `00025476`)
contains an uncapped 1 ms poll loop whose exit test is
`Endpoint->StateLast == Endpoint->StateNext`: SP4 read at `00025AF4`, back
edge at `00025C8F`, the only call in the body being `USBPORT_Wait`
(SP4 `00011636`, a bare `KeDelayExecutionThread`).

Those two words are equal only once the walker has committed the queued state change. The loop calls
neither the walker nor `SetEndpointState`; it cannot make progress on its own,
and there is no iteration cap. The loop flag is recomputed each pass from
whether any endpoint is still unsettled.

So a PASSIVE-level thread does block until the state-change list is drained.
`InterruptNextSOF` is the accelerator for that wait, and the stub is legal
only because the walker has a second, callback-independent driver. That makes
the timer below essential rather than a convenience: with it, the wait ends
in at most one tick; without it, and with no interrupt, this loop is an
unbounded hang at PASSIVE (a hang, not a deadline).

The walker has two callers in each build and only one of them is
interrupt-driven:

| Path | SP4 | NUSB |
|---|---|---|
| walker | `00029E32` | `000296EE` |
| interrupt DPC (registered for `devExt+0x528`) -> walker | `0002A1AE` -> `0002A304` | `00029A66` -> `00029BBA` |
| 500 ms timer DPC (registered for `devExt+0x5C8` / `+0x5C0`) -> walker | `0001497E` -> `00014AB2` | `000147CE` -> `00014901` |
| timer start (`KeInitializeDpc` + `KeSetTimer`, interval `0x1F4` = 500 ms) | `0002D812`, called once from `000112A9` | `0002CF5E`, called once from `00011201` |

Two qualifications on that timer, neither closed. "Started with the
controller" is looser than what the instructions show:

- It is armed only on a successful `StartController`. The call at SP4
  `000112A9` / NUSB `00011201` sits behind a `jl` on the return of SP4
  `00029D6C`, which is `MPStatusToNtStatus` (SP4 `00029D2E`, a pure translation
  table) applied to the miniport's status. A failed start arms no timer. That
  is very probably harmless, since a controller whose start failed should have
  no endpoints to change state, but the device-stack path has not been read.
- It can be stopped. There is one `KeCancelTimer` site per build (SP4
  `000150B1`, NUSB `00014EF9`), and the instruction above it is
  `and dword ptr [devExt+0x1C4], 0BFh`, clearing bit `0x40`, the same rearm
  flag `USBPORT_StartTimer` sets. Whether that path can run while endpoints
  still change state is open, and it is the one remaining hole in "nothing
  waits on this callback": if the timer is down, the interrupt DPC is the only
  remaining drain, and a miniport that answers `InterruptNextSOF` with nothing
  generates no interrupt to supply it.

So the verdict "a do-nothing stub is legal" rests on the timer being up
whenever endpoints exist, which is argued, not proven. Closing it means
reading the stop path's callers and the endpoint teardown ordering.

The timer DPC re-arms its own `KeSetTimer` at the end of every firing while
`devExt->Flags & 0x40` is set, and its walker call is unconditional on any path
that does not have `devExt->Flags & 0x80` (the shutting-down bit). It is the
same timer `StartController`'s row above mentions. A miniport that answers
`InterruptNextSOF` with nothing therefore delays each endpoint state change by
at most one timer tick; it cannot deadlock the machine.

The real coupling is `Get32BitFrameNumber`, and it is stronger than it looks.
The commit gate is a comparison against a stamp, so a miniport whose published
frame number stops advancing leaves the head endpoint on the list and aborts
the pass, which is head-of-line blocking for every other endpoint's state
change, on both the interrupt and the timer path, for as long as the number is
stuck. Answering `InterruptNextSOF` would not help there; only advancing the
number would. `XhciFrameNumber` (`src/xhci_init.c`) satisfies this by
construction: its stall path increments a call counter rather than repeating a
value, the same property the uncapped post-open wait needs.

The debug-console logs corroborate the four-versus-one asymmetry, and they
also show why a trace cannot prove the callback absent. `XHCI_DBG_CB` is
budgeted per site (`XHCI_DBG_CALL_LIMIT`, 4), and every debug-console log this
repository holds from batch 6-V onward, on both targets, contains the
callback. Across the 43 logs in `vm/` that carry either line, 32 show
`InterruptNextSOF` and `SetEndpointState` both saturated at 4, and 11 show
them differing (`n = 2, s = 4` in all five batch 9-V passthrough boots). Two
independently budgeted sites saturating together is not evidence of pairing,
and a divergence is evidence against it: had the callback accompanied every
state change, the two counters could never separate. A budgeted channel
cannot support a negative.

### Endpoints

| Callback | Signature | Observed contract |
|---|---|---|
| `QueryEndpointRequirements` | `VOID (ext, PUSBPORT_ENDPOINT_PROPERTIES, PUSBPORT_ENDPOINT_REQUIREMENTS)` [164-168] | DISPATCH under `MiniportSpinLock` [endpoint.c:1049-1055]. Fill `HeaderBufferSize` (per-endpoint common-buffer bytes -> the xHCI transfer-ring segment) and `MaxTransferSize`. usbport adopts the returned `MaxTransferSize` as the endpoint's transfer-size cap for bulk/interrupt [endpoint.c:1057-1061] |
| `OpenEndpoint` | `MPSTATUS (ext, PUSBPORT_ENDPOINT_PROPERTIES, PVOID epExt)` [152-156] | DISPATCH under `MiniportSpinLock` [endpoint.c:762-768]. Properties carry the common buffer allocated from `HeaderBufferSize` in `BufferVA/BufferPA/BufferLength` [endpoint.c:1063-1082]. This is the slot/endpoint pivot point (see `docs/usb-xhci-info/usbport-miniport-interface.md` section 3). Cannot sleep - command-ring waits must be event-driven, not blocking |
| `ReopenEndpoint` | `MPSTATUS (ext, props, epExt)` [158-162] | Exists in the packet, but the enumeration-time EP0 rework does not use it: `USBPORT_ReopenPipe` instead does SetEndpointState(REMOVE) -> 2 ms wait -> zero the miniport endpoint extension -> free + reallocate the common buffer -> `QueryEndpointRequirements` -> `OpenEndpoint` [endpoint.c:1194-1306]. `Packet->ReopenEndpoint` is called on the device-restore path [device.c:1877]. Binary-confirmed, with one correction: there is no `CloseEndpoint` in that sequence; see "The endpoint and transfer paths in the shipping builds" below. See "Enumeration flow" below for why this matters to slot lifetime |
| `CloseEndpoint` | `VOID (ext, epExt, BOOLEAN IsDoDisablePeriodic)` [170-174] | ReactOS calls it at DISPATCH under `MiniportSpinLock` [endpoint.c:589-603], the BOOLEAN named `IsDoDisablePeriodic` [endpoint.c:580]. Neither shipping build calls this slot at all (whole-image census below), so the rule "do not tie Disable Slot to EP0 close" is not advice, it is forced: there is no close notification to tie anything to. Keep the callback signature-correct and safe against a build that does call it |
| `GetEndpointState` | `ULONG (ext, epExt)` [223-226] | Returns a section-2 state value. Neither shipping build calls this slot at all (whole-image census below). What ReactOS polls up to 1000 x 1 ms after an open [endpoint.c:1104-1114] is `USBPORT_GetEndpointState`, usbport's *software* state, which is a different function - and in the shipping builds that poll has no retry cap. See "The endpoint and transfer paths in the shipping builds" below |
| `SetEndpointState` | `VOID (ext, epExt, ULONG state)` [228-232] | DISPATCH under `MiniportSpinLock` [endpoint.c:401-405]. After calling, usbport stamps the frame number and queues the endpoint on a state-change list, confirming the transition later via `GetEndpointState` [endpoint.c:407-424]. For xHCI, transitions are effectively immediate (Stop Endpoint / doorbell); reflect the requested state promptly or enumeration stalls. Edge-triggered on usbport's own software state, binary-confirmed: `USBPORT_SetEndpointState` compares the requested state against its recorded one and skips the miniport call when they match (SP4 `00016D8A`, NUSB `000169D2`) - so a state the miniport moved by itself is never re-announced, and an `ACTIVE` that "should" follow a pause may never arrive if usbport believes the endpoint active already. The miniport must therefore restart a self-stopped endpoint from its own state (the health poll's net), not wait for a callback |
| `PollEndpoint` | `VOID (ext, epExt)` [234-237] | Called from the endpoint worker [endpoint.c:1726]. xHCI: normally no-op |
| `SetEndpointDataToggle` | `VOID (ext, epExt, ULONG toggle)` [257-261] | Called around reset-pipe [urb.c:147]. xHCI tracks toggles in hardware: no-op |
| `GetEndpointStatus` / `SetEndpointStatus` | `ULONG (ext, epExt)` / `VOID (ext, epExt, ULONG)` [263-272] | Status values in section 2. Halted <-> Stall Error events; "set RUN" -> Reset Endpoint + Set TR Dequeue Pointer [urb.c:165 context]. Binary-confirmed live in both builds; see "The status callbacks and the NUKE path" below |
| `RebalanceEndpoint` | `VOID (ext, props, epExt)` [509-513] | Called by the USB2 budgeter when a periodic schedule shifts [usb2.c:929]. ReactOS EHCI leaves it unimplemented [usbehci.c:3570-3577]; near-no-op for xHCI (update context if interval changed) |

### Transfers

| Callback | Signature | Observed contract |
|---|---|---|
| `SubmitTransfer` | `MPSTATUS (ext, epExt, PUSBPORT_TRANSFER_PARAMETERS, transferExt, PUSBPORT_SCATTER_GATHER_LIST)` [200-206] | DISPATCH under `MiniportSpinLock` [endpoint.c:1564-1587]. Nonzero return leaves the transfer queued for retry (endpoint stays active) [endpoint.c:1589-1598]; on success usbport starts the URB timeout clock [endpoint.c:1600-1604]. Direction comes from `TransferFlags & USBD_TRANSFER_DIRECTION_IN` (usbehci tests exactly that [usbehci.c:2055]). Intercept SET_ADDRESS here (`SetupPacket` bytes are in the parameters) - see `docs/contributing/implementation-invariants.md` "Device Addressing" |
| `SubmitIsoTransfer` | `MPSTATUS (ext, epExt, params, transferExt, PVOID isoParams)` [208-214] | ReactOS's iso path is a stub (`iso.c` is 33 lines; the submit site passes NULL with a FIXME [endpoint.c:1570-1576]), so the layout could not come from there. Derived from both shipping binaries; see "Isochronous transfers" below. The declared signature is confirmed, and the fifth argument is a block usbport carves out of the transfer allocation |
| `AbortTransfer` | `VOID (ext, epExt, transferExt, PULONG CompletedLength)` [216-221] | DISPATCH under `MiniportSpinLock`; write the bytes actually transferred through arg 4 [endpoint.c:1495-1511]. Arg 3 and arg 4 do not survive the return; see "`AbortTransfer`: what survives the return" below |

Completion path: from `InterruptDpc`, call
`UsbPortCompleteTransfer(ext, epExt, TransferParameters, USBD_STATUS, transferredBytes)`
- the third argument is the same `PUSBPORT_TRANSFER_PARAMETERS` pointer that
`SubmitTransfer` received (usbport recovers its transfer record from it), as
EHCI does [usbehci.c:3041]. Keep that pointer in the miniport transfer
extension.

usbport does not zero the miniport transfer extension between transfers
(runtime observation): the extension is interior to a transfer allocation that
is freed and re-carved, so a fresh transfer can arrive carrying the previous
tenant's bytes. Every field the miniport reads on a failure or completion path
must have been written by this submit. A stale `XHCI_XFER_FLAG_ISOCH` would
send an ordinary bulk failure through the isochronous completion service with a
block pointer belonging to a transfer usbport had already freed, so
`xhciDevStampTransfer` (`src/xhci_slot.c`) clears `Flags` first.

#### Isochronous transfers

Static, read out of both shipping builds with `link -dump -disasm`; extracts
in `tools/{win2ksp4,nusb}-extracted/usbport-iso-disasm.txt`. One contract
serves both targets, with nothing to discriminate on. The agreement is at the
instruction level: the iso-parameters builder is 336 instructions differing in
3 positions, the block that consumes it on completion is 199 instructions
differing in 0, and every difference anywhere in the path is one of the three
private offsets already recorded in section 3 (the wrapper base, a spin-lock
offset, and the inline scatter/gather list's position in the transfer object).
No version gate, no `MiniPortVersion` branch, no build-specific field.

Read the wrapper base first or every offset here is wrong. `SubmitIsoTransfer`
is packet slot 0x54, and the pointer usbport keeps at `devExt+0x104` is
`packet + 0x14` on Win2000 SP4 and `packet + 0x10` on NUSB - so the call reads
`call [eax+0x68]` on one build and `call [eax+0x64]` on the other. Matching
`call [eax+0x54]` lands on `SuspendController` instead; the tell is the
argument count. Identify a slot by a callback whose signature is distinctive
(`AbortTransfer`'s OUT parameter serves) before trusting any offset
arithmetic.

The dispatch. One routine issues both submits and picks between them on
bit 5 of usbport's private transfer flags (`transfer+0x04`), not on
anything the miniport declared. Five arguments either way, in the same order,
and only the fifth differs:

```
SubmitTransfer   (ext, epExt, transferParams, transferExt, sgList)
SubmitIsoTransfer(ext, epExt, transferParams, transferExt, isoParams)
                  |     |      |               |            |
                  |     |      |               |            +-- [transfer+0x94]
                  |     |      |               +-- [transfer+0x80]
                  |     |      +-- transfer+0x4C  (TransferParameters)
                  |     +-- endpoint+0x164
                  +-- the miniport extension
```

The sg-list argument is an interior pointer (`transfer+0xC0` on SP4,
`transfer+0x98` on NUSB); the iso-parameters argument is a stored pointer,
and that difference is the whole reason the layout had to be found rather than
read off an offset.

It is never NULL at the call site, and that is derived rather than assumed.
The transfer allocator carves the iso block out of the same `ExAllocatePool`
block, sizes it only when `URB->Function == 0x0A` (`URB_FUNCTION_ISOCH_TRANSFER`),
and sets both `transfer+0x94` and the dispatch's flag bit in the same basic
block from the same condition (SP4 `0x17A0F`-`0x17AAB`). So the two cannot
disagree. A miniport that null-checks the fifth argument is checking something
this code cannot produce.

The block. `0x48 + 0x38 * NumberOfPackets` bytes, `NumberOfPackets` taken
from `URB+0x4C`. Header:

| Offset | Field | Direction | Notes |
|---|---|---|---|
| 0x00 | signature `'Isoc'` (`0x636F7349`) | in | written on every build |
| 0x04 | `NumberOfPackets` | in | `URB+0x4C` verbatim |
| 0x08 | scatter/gather element count | in | copied from the sg list's `SgElementCount`, which is at its `+0x0C` (`+0x08` there is `MappedSystemVa`, per the layout at the head of this section) |
| 0x0C | - | - | not written by usbport; the allocation is zeroed |
| 0x10 | first packet entry | | stride 0x38, `NumberOfPackets` of them |

The header is 0x10 bytes and the allocation is `0x48 + 0x38 * n`, which is one
entry's worth larger than `0x10 + 0x38 * n`. That slack is measured, not
explained - both builds compute it the same way and neither writes into it.
Do not rely on it.

The per-packet entry, offsets relative to the entry, which starts at
`isoParams + 0x10 + 0x38 * i`:

| Offset | Field | Direction | Notes |
|---|---|---|---|
| 0x00 | `Length` | in | bytes this packet asks for: the next `IsoPacket`'s `Offset` minus this one's, or `transfer+0x50 - Offset` for the last |
| 0x04 | `LengthTransferred` | out | the miniport writes it; usbport sums it across packets and writes each back into `URB->IsoPacket[i].Length` when `transfer+0x10 == 1` - a private field this pass did not identify. The natural reading is the direction, since an IN transfer is the one whose per-packet lengths the URB needs back, but that is inference and is flagged as such rather than written into the table |
| 0x08 | `FrameNumber` | in | `URB->StartFrame + (i >> 3)` on High Speed, `URB->StartFrame + i` otherwise |
| 0x0C | `MicroFrame` | in | `i & 7` on High Speed; 0 otherwise, from the zero-fill rather than from a write |
| 0x10 | `Status` | out | the miniport writes a `USBD_STATUS`; usbport copies it to `URB->IsoPacket[i].Status` |
| 0x14 | `FragmentCount` | in | 1 or 2; the builder has no path that writes anything else. Why two suffices is not derived here - do not build on a reason this pass did not establish |
| 0x18 | `Fragment[0].Length` | in | the whole packet when `FragmentCount == 1`, else `0x1000 - (PA & 0xFFF)` |
| 0x1C | - | - | not written |
| 0x20 | `Fragment[0].PhysicalAddress.LowPart` | in | |
| 0x24 | `Fragment[0].PhysicalAddress.HighPart` | in | structurally zero on a 32-bit adapter - check it, do not assume it, the same rule the sg list carries in section 5 |
| 0x28 | `Fragment[1].Length` | in | `Length - Fragment[0].Length`; 0 when `FragmentCount == 1` |
| 0x2C | - | - | not written |
| 0x30 | `Fragment[1].PhysicalAddress.LowPart` | in | |
| 0x34 | `Fragment[1].PhysicalAddress.HighPart` | in | |

Two fields are the miniport's to write, `+0x04` and `+0x10`. That comes from
the reader, not from the builder's silence: the builder zero-fills each entry
and then writes every field above except those two, and the completion path
reads those two back. Everything else is input.

The completion contract. `UsbPortCompleteIsoTransfer` is packet slot
0x100 - identified from the registration path's service-block fill (SP4
`0x27E5F` writes `0x14944`; NUSB `0x277D5` writes `0x14794`), not from a call
site, because the miniport is the only caller. It is `ret 10h`, four arguments,
matching ReactOS's declaration, and the arguments are used as:

```
UsbPortCompleteIsoTransfer(ext, epExt, transferParams, isoParams)
                                 ^^^^^                 ^^^^^^^^^
                                 unread                the same block
                                                       SubmitIsoTransfer got
```

Note the second argument is not read at all in either build. It is passed
because the declaration says so; do not infer that usbport recovers the
endpoint from it.

What usbport then does, per packet: copies `entry+0x10` into
`URB->IsoPacket[i].Status` (`URB+0x5C + 12*i`), accumulates `entry+0x04`, and
writes it back per the row above. One completion code is rewritten rather
than reported: a per-packet status of `0xC0000009` is stored to the URB, then
logged and replaced with 0. `URB->ErrorCount` (`URB+0x50`) is incremented for
every packet whose URB status is nonzero after that rewrite, so a rewritten
one is not counted as an error. The constant is recorded as measured; this file
does not name it, because `AGENTS.md` forbids naming a `USBD_STATUS` from
memory.

Three things the tables above do not cover, and how the driver handles each:

- The frame-number window usbport enforces before it will submit at all. The
  `StartFrame` comparisons in the builder are visible but were not chased to
  their policy. The driver takes its window from the spec instead (the Valid
  Frame Window of 4.11.2.5), because a Frame ID has to be legal to the xHC
  and usbport's own comparisons only decide whether it will submit.
- Which `MPSTATUS` values the return is discriminated on. The call site stores
  it as `SubmitTransfer`'s is, and a nonzero return there cannot fail a
  transfer (see "The endpoint and transfer paths in the shipping builds").
- `ASAP` handling. None is needed: a request either carries Frame IDs on every
  TD or SIA on every TD, and SIA is schedule-as-soon-as-possible.

`UsbPortCompleteIsoTransfer`'s fourth argument is a pointer, the same block
`SubmitIsoTransfer` was handed, as the callee's own per-packet dereferences at
`+0x10 + 0x38*i` show. The ReactOS-derived shape reads as a `ULONG`, which is
the same width on x86 and compiles, but it misleads a reader into thinking the
miniport hands back a count. `src/xhci_usbport.h` declares the pointer.

One thing this contract does not supply is an isochronous endpoint's service
interval: `Period` is forced to 1 and carries no meaning. The only statement
usbport makes about the rate is the `FrameNumber`/`MicroFrame` pair in the
per-packet table: one packet per microframe on High Speed, one per frame
otherwise. That is an ESIT, and it is where the Endpoint Context's Interval
comes from (`XhciBuildEndpointParams`). It is an inference from measured
arithmetic rather than a field.

The two builds do not behave the same when the open is refused (batch 6-V,
runtime, on an early build of this driver that refused every non-control
endpoint). SP4's
usbport asked for an isochronous open (`TransferType = 0`, MPS 192, `Period` 1,
`MaxTransferSize = 0x01000000`), was answered `MP_STATUS_NOT_SUPPORTED`, and
stopped; NUSB's went on to call `SubmitIsoTransfer` against that endpoint
extension, which no successful `OpenEndpoint` had produced - the older build
guarding a miniport refusal less, the same lineage shape as `USBPORT_GetTt` in
section 8. This callback therefore has to be safe against an endpoint
extension the miniport never initialised, on Win98 in particular.

#### The endpoint and transfer paths in the shipping builds

Static, both shipping builds. Extracts in
`tools/{win2ksp4,nusb}-extracted/usbport-endpoint-disasm.txt`; the SP4 one
carries the full annotation and the structure-offset table, the NUSB one
records agreement and difference. Findings:

- `USBPORT_ReopenPipe` does bypass `ReopenEndpoint`, and the sequence is one
  step shorter than the ReactOS reading. SP4 `0x247D8`-`0x24D0F`, NUSB the
  same shape at its own addresses: `SetEndpointState(REMOVE = 4)` -> a 2 ms
  `USBPORT_Wait` -> `RtlZeroMemory` over the miniport endpoint extension for
  `MiniPortEndpointSize` bytes -> free the endpoint common buffer (only if it
  has one) -> `QueryEndpointRequirements` -> reallocate the common buffer
  (only if `HeaderBufferSize` is nonzero; a failed allocation is
  `USBD_STATUS 0xC000009A`) -> `OpenEndpoint` -> and, only if `StateLast` was
  ACTIVE, `SetEndpointState(ACTIVE = 3)`. There is no `CloseEndpoint`
  call in it: ReactOS's `MiniportCloseEndpoint` at [endpoint.c:1230] has no
  counterpart in either binary. The same shape appears in
  `USBPORT_RestoreDevice`'s NUKE branch, also without a close.
- The miniport's `CloseEndpoint` and `GetEndpointState` slots are never
  invoked, anywhere, in either image. This is a whole-image result, not one
  function's extent.
  - Every `call dword ptr [reg+imm]` was histogrammed by immediate and packet
    0x34 / 0x5C are absent from both builds' sets; the only other
    indirect-call forms are the IAT, the register calls, and three
    `[ecx]`/`[ecx+4]`/`[ecx+8]` sites that are all `DMA_OPERATIONS`; and every
    register load from those two interface offsets was checked for a later
    call through the same register.
  - Independently re-derived by a producer-first audit (enumerate every read
    of the displacement, trace the base backwards, then trace the loaded
    value forwards), which also disposed of the near misses: SP4's apparent
    `+0x48` reads at `0x14301`-`0x14463`, `0x192B0`, `0x1AC80`, `0x1B407` and
    `0x1CC76`-`0x1CEB4` are numeric/flag/list fields, and NUSB's `+0x6C` read
    at `0x26CC4` is a `LIST_ENTRY.Blink` (proved by the self-link test at
    `0x26CBA`). It is still a claim about these two builds; the XP 5512 image
    was not searched.
  - `PassThru` is not a third such slot. It is called through a register in
    both builds, twice each: SP4 loads the interface at `0x28D1D`, the slot at
    `0x28D23` (`[eax+0xF4]`) and calls at `0x28D3E`, with a second site at
    `0x2914D`/`0x29153`/`0x29169`; NUSB is `0x285DB`/`0x285E1`
    (`[eax+0xF0]`)/`0x285FC` and `0x28A0B`/`0x28A11`/`0x28A27`.
  - The register-indirect form is what an immediate histogram cannot see, so
    the register-call check has to be read for every source offset, not only
    0x48 and 0x70. What drives those four sites is traced under "Debug /
    single-packet" in this section (`run-13e.md` P10): one is the
    `IOCTL_USB_USER_REQUEST` vendor escape and one is a test-mode-only
    internal probe, and the `0xF0`/`0xF4` quoted here are interface offsets
    for packet slot 0xE0, not packet offsets.
- An endpoint is destroyed with no reclamation callback and no confirmation
  handshake. The delete routine (SP4 `0x260E6`-`0x26243`, NUSB
  `0x25A68`-`0x25BC5`) unlinks the endpoint, calls `USBPORT_FreeCommonBuffer`
  on it (SP4 `0x26198`, NUSB `0x25B1A`), releases bandwidth and `ExFreePool`s
  the endpoint. So the endpoint common buffer - which under Option A is where
  the xHCI transfer ring lives - can go away with no callback at the
  reclamation itself. Say it that way rather than "the miniport is told
  nothing": a `SetEndpointState(REMOVE)` *was* delivered earlier (next
  bullet); what does not exist is any notice at the free, or any way for the
  miniport to hold it off. Anything the hardware still points at must therefore
  be torn down from the port/disconnect path, never from a close.
- How long the miniport has between its last notice and that free is not the
  same on the two paths, and neither is a handshake. The miniport's only
  notice either way is `SetEndpointState(REMOVE)`, which runs at DISPATCH under
  a usbport spin lock and so cannot wait for a Stop Endpoint command.
  `USBPORT_ReopenPipe` then gives a fixed 2 ms before it frees.
  - The delete path gives no guaranteed budget and no miniport-controlled
    gate (the elapsed time is whatever the sweep's scheduling and usbport's
    own bookkeeping produce): the REMOVE is issued from a different routine
    (SP4 `0x25E4E`, NUSB `0x257CD`, both through `USBPORT_SetEndpointState`,
    SP4 `0x19DEE`), and a later reclaim sweep (SP4 `0x25FAE`-`0x260E3`, NUSB
    `0x25930`-`0x25A64`) calls the delete routine for each candidate, which
    frees as soon as usbport's own bookkeeping allows: `Endpoint+0x0C` back
    at `-1`, and the `LIST_ENTRY` at `+0x68`/`+0x6C` with null links.
  - Read both of those carefully, because the obvious readings are wrong.
    `+0x68`/`+0x6C` are the `Flink`/`Blink` of one `LIST_ENTRY`, not two
    lists, and the gate tests them for null, not for the self-linked
    emptiness a normal empty list would show (creation self-links it at SP4
    `0x26642`, NUSB `0x25FC1`). `Endpoint+0x0C` is an interlocked,
    `-1`-biased activity counter, initialized at SP4 `0x24FB9` / NUSB
    `0x2493B`, raised by `USBPORT_ReopenPipe`'s head with
    `InterlockedIncrement` (SP4 `0x24841`, NUSB `0x241C3`) and backed out of
    on a loss; it is not a general reference count. None of those is anything
    the miniport sets, is consulted about, or can delay.
  - Design consequence, and it is an inference rather than a measurement: do
    not plan to prove quiescence to usbport (there is no channel for it), and
    plan so that the xHC never holds a pointer into an endpoint's common
    buffer that can outlive a REMOVE. That rule follows conservatively from
    "the buffer may be freed after REMOVE with no later synchronization"; the
    disassembly shows the freedom, not the rule. EP0 satisfies it because its
    ring lives in the controller common buffer
    (`docs/contributing/design/04-controller-common-buffer.md` section 3.4);
    the other endpoints are where it is a live choice.
- The "wait for ACTIVE after open" loop has no retry cap and no timeout.
  SP4's helper is `0x24D12`-`0x24EB9` (called from `USBPORT_OpenPipe` at
  `0x25635`), NUSB's is the identical loop ending at `0x2483B`: read the state,
  `cmp ..,3`, break if ACTIVE, else `USBPORT_Wait(1 ms)` and `jmp` back,
  forever. ReactOS's `for (RetryCount = 0; RetryCount < 1000; ...)` and its
  `USBD_STATUS_TIMEOUT` have no counterpart in either binary, the same
  "ReactOS added the guard the shipping code lacks" shape as `USBPORT_GetTt`
  and the `NumberOfPorts = 0` arithmetic.
  - What it polls is `USBPORT_GetEndpointState` (SP4 `0x1A096`, NUSB
    `0x19C80`, each reading `Endpoint+0x30`), usbport's own software state. On
    the ordinary-endpoint path that a post-open wait is on (the qualifier
    matters, see below) that state becomes ACTIVE only when usbport's DPC or
    its 500 ms timer finds `Get32BitFrameNumber` past the frame stamped at
    `SetEndpointState`.
  - That chain is binary-established, not inherited from the mirror:
    `USBPORT_SetEndpointState` calls the miniport slot (SP4 `0x19FB8`), stores
    the requested state at `Endpoint+0x34`, reads `Get32BitFrameNumber` (SP4
    `0x19FE7`) and stamps `Endpoint+0x38`; the state-change worker re-reads
    the frame (SP4 `0x29F76`), compares it against `+0x38` (SP4 `0x29FED`) and
    copies `+0x34` into `+0x30` (SP4 `0x2A071`). NUSB:
    `0x19BA4`/`0x19BD3`/`0x19BDF`, then `0x29832`/`0x298A6`/`0x2992A`. It is
    reached from the 500 ms timer (SP4 `0x14AB2`, NUSB `0x14901`; period
    `0x1F4` loaded at SP4 `0x1129A`, NUSB `0x111F2`) as well as from the DPC
    (SP4 `0x2A304`, NUSB `0x29BBA`).
  - Do not generalise that to all endpoint state changes:
    `USBPORT_SetEndpointState` also has immediate-update branches taken when
    `Endpoint+0x04` bit 1 or bit 3 is set (SP4 `0x19E10`-`0x19E29` and
    `0x19EFE`-`0x19F66`; NUSB `0x199FC`-`0x19A15` and `0x19AEA`-`0x19B52`)
    which set the state without the frame gate.
  - Consequence: there is no deadline to beat, there is a hang to avoid. A
    miniport whose frame number does not advance, or whose `SetEndpointState`
    is never followed by a DPC or timer pass, leaves the enumerating thread
    spinning at PASSIVE_LEVEL for the life of the boot instead of failing the
    open. The rule that `Get32BitFrameNumber` must advance rests on this.
- `SubmitTransfer`'s SG-list argument is never NULL, but is routinely
  empty. It is `&Transfer->SgList`, an interior pointer computed with `lea`
  (SP4 `Transfer+0xC0`, NUSB `Transfer+0x98`, the same offsets found in
  `USBPORT_MapTransfer`, section 5, which cross-checks this read). But
  `USBPORT_QueueActiveUrbToEndpoint` tests
  `TransferParameters.TransferBufferLength == 0` before putting the
  transfer on the map list (SP4 `0x180F2`, NUSB `0x17CDC`) and a zero-length
  transfer goes straight to the endpoint's own list, so `USBPORT_MapTransfer`
  never runs for it and `SgElementCount` stays at the 0 that
  `USBPORT_AllocateTransfer` wrote (SP4 `0x17A38`, NUSB `0x1767F`, on top of a
  `rep stos` that zeroes the whole record).
  - That is scoped to the zero-length path: on the *mapped* path the count is
    of course written (SP4 `0x13717`, NUSB `0x1356B`), so "nothing else
    writes it" is only true of the transfers that bypass mapping.
  - `Transfer+0x50` really is `TransferBufferLength` rather than an inference
    from the parameters offset: allocation copies its input `+0x18` there and
    its input `+0x14` to `Transfer+0x4C` (SP4 `0x17C92`-`0x17C9B`, NUSB
    `0x1787C`-`0x17885`).
  - Zero-length is the *common* case in enumeration (SET_ADDRESS,
    SET_CONFIGURATION and every control transfer with no data stage), so
    drive the TD from `TransferBufferLength` and only consult the SG list
    when it is nonzero; do not index element 0 unconditionally.
    `USBPORT_FlushMapTransfers` computes its map-register count from
    `TransferBufferLength - 1`, which is the other half of why that bypass
    has to exist.

The transfer-parameters pointer's lifetime: it is an interior pointer of
usbport's private transfer record at `Transfer+0x4C` in
both builds, and `UsbPortCompleteTransfer` (SP4 `0x1A4EA`, NUSB `0x1A0D4`)
recovers the record with `lea ebx,[arg3-4Ch]`. So it is stable for as long as
the record is, it is legitimate to save it in the miniport transfer extension,
and usbport itself arms the URB timeout only *after* a `SubmitTransfer` that
returned 0. What none of this bounds is the `AbortTransfer` window; the
subsection after next does.

#### The completion path is unordered against usbport's post-submit writes

Static, both builds. The completion path takes no lock that orders it behind
usbport's post-`SubmitTransfer` writes. `USBPORT_QueueDoneTransfer` (SP4 `000183C0`, NUSB `00017FA4`; log
tag `QDnT`; instruction-for-instruction parallel apart from two private
offsets, done list at FdoExt `+0x2C4`/`+0x2C0` and flush DPC at
`+0x548`/`+0x540`) unlinks the transfer from the endpoint transfer list with
a bare `RemoveEntryList` (no spin lock is acquired anywhere between the
function's entry and the unlink), then `ExfInterlockedInsertTailList`s it
onto the FDO done list and queues the flush DPC. `UsbPortCompleteTransfer`
itself (SP4 `0001A4EA`) sets the COMPLETED flag (`or byte [T+5],2`) and
stores the completed length equally bare before calling it.

The submit side, by contrast, performs its post-callback writes (`or
[esi+4],8` + the 10 s timeout arming, SP4 `000165BD`) *after* releasing the
miniport lock at `0001655C`. So nothing in usbport orders a completion this
miniport delivers against the writes usbport makes after the `SubmitTransfer`
wrapper returns. ReactOS agrees on every step. That is the reason the miniport
holds its own drain-pass epoch: on SMP, observing `SubmitDepth` reach 0 is not
evidence those writes have happened.

#### `AbortTransfer`: what survives the return

Static, both shipping builds. Extracts in
`tools/{win2ksp4,nusb}-extracted/usbport-abort-disasm.txt`; the SP4 one
carries the full annotation, the NUSB one records agreement and difference.

- usbport retains nothing. The transfer record is `ExFreePool`d in the same
  endpoint-worker pass that called `AbortTransfer`, and the miniport is not
  called again about it. `AbortTransfer` has exactly one call site in
  each image, in `USBPORT_DmaEndpointPaused` (SP4 `0x1673A`, call at
  `0x16996`; NUSB `0x16386`, call at `0x165DF`), reached only from
  `USBPORT_DmaEndpointWorker` (SP4 `0x16B1C`, NUSB `0x16764`).
  - On return the transfer is unlinked and takes one of two paths, and both
    end in `ExFreePool`. `TRANSFER_FLAG_SPLITED` (`Transfer+0x05` bit 0) goes
    to `USBPORT_CancelSplitTransfer` (SP4 `0x13F80`, free at `0x1411F`; NUSB
    `0x13DD4`, free at `0x13F73`) immediately, inside the same loop iteration
    and still under `EndpointSpinLock`.
  - Everything else is put on `Endpoint->CancelList` (`Endpoint+0x50`) and
    freed a few hundred instructions later by `USBPORT_FlushCancelList` (SP4
    `0x198A8`, NUSB `0x19494`), which the worker calls unconditionally the
    moment it drops the endpoint lock, via `USBPORT_CompleteTransfer` (SP4
    `0x185A8`, free at `0x18973`; NUSB `0x1818C`, free at `0x1855C`).
- The miniport transfer extension is interior to the freed block, so the
  free takes it: `USBPORT_AllocateTransfer` computes
  `Transfer->MiniportTransfer = (PUCHAR)Transfer + Transfer->PortTransferLength`
  (SP4 `lea eax,[edi+esi]` at `0x179F4` -> `0x179F7`; NUSB `0x1763B` ->
  `0x1763E`), and the split-transfer allocator repeats the rule (SP4
  `0x13B08`/`0x13B0E`/`0x13B13`, NUSB `0x1395C: mov eax,[edx+0Ch]` ->
  `0x13962: add eax,edx` -> `0x13967: mov [edx+80h],eax`).
  - Arg 3 of `AbortTransfer` is that pointer; arg 4 is a stack local of
    `USBPORT_DmaEndpointPaused`. Neither is valid after the return, and
    neither is the `TransferParameters` pointer at `Transfer+0x4C` that
    `SubmitTransfer` handed over.
  - Design consequence: the cancellation state machine may not hold a
    usbport-owned pointer across the Stop Endpoint / Set TR Dequeue Pointer
    chain. Whatever the miniport still needs must be copied into its own
    memory (endpoint extension or controller block) during the callback, and
    a late Transfer Event for a cancelled TD must never reach
    `UsbPortCompleteTransfer`, because usbport has already completed and
    freed it.
- `USBPORT_CompleteTransfer` releases the DMA mapping on the way past
  (`FlushAdapterBuffers` then `FreeMapRegisters`, SP4 `0x18764`/`0x187EE`,
  NUSB `0x1835E`/`0x183E8`, plus `IoFreeMdl` at SP4 `0x18800`-`0x18803` /
  NUSB `0x183FA`-`0x183FD`). So the physical addresses the miniport wrote into
  its TRBs stop belonging to that transfer at the same moment. This is the
  same hazard as on the completion path, now on the cancellation path and with
  no "answer later" available.
  The teardown is conditional on the transfer having been mapped (SP4 tests
  `Transfer+0x04` bit 1 at `0x1872B`, NUSB at `0x18325`): an unmapped or
  zero-length transfer skips the map-register branch because it holds no
  mapping, no map registers and no MDL. That narrows what the branch *does*,
  not the ownership rule - a mapped transfer's addresses are still revoked here.
- The miniport does get an earlier, asynchronous warning, but it is not a
  handshake. An endpoint reaches PAUSED because `USBPORT_DmaEndpointActive`
  (SP4 `0x16358`, NUSB `0x15FA4`) meets a transfer already flagged
  `CANCELED|ABORTED` and returns `USBPORT_ENDPOINT_PAUSED` (SP4 `0x165ED` ->
  `0x16729`); the worker then calls `USBPORT_SetEndpointState(PAUSED)` (SP4
  `0x16D97` -> `0x19DEE`, NUSB `0x169DF` -> `0x199DA`), which calls the
  miniport's `SetEndpointState` slot (SP4 `0x19FB8`, NUSB `0x19BA4`) and
  stamps the frame number. `USBPORT_DmaEndpointPaused` only runs on a later
  pass, once the frame gate described above has moved `StateLast` to PAUSED.
  - So `SetEndpointState(PAUSED)` precedes `AbortTransfer` by at least one
    frame gate and is the miniport's opportunity to start a Stop Endpoint.
  - Nothing verifies that it finished. usbport stores its requested state and
    samples its own frame counter straight after the call (SP4
    `0x19FC6`-`0x19FF6`, NUSB `0x19BB2`-`0x19BDF`) with no returned status and
    no read-back, and the timer accepts the state only once that sampled
    frame has advanced (SP4 `0x29FED`-`0x29FF0` then `0x2A06C`-`0x2A071`;
    NUSB `0x298A6`-`0x298A9` then `0x29925`-`0x2992A`). `SetEndpointState`
    runs at DISPATCH under a usbport spin lock, so it cannot wait either.
  - The bound this gives is at least one successful frame-advance gate, not
    an exact one-frame wall clock. Do not budget a fixed delay from it.
- On a NUKEd endpoint the callback is skipped outright.
  `Endpoint->Flags & ENDPOINT_FLAG_NUKE` (bit 3) short-circuits the miniport
  call (SP4 `0x16905: test byte ptr [eax+4],8` / `0x16909: jne 0x16A30`,
  bypassing the call at `0x16996`; NUSB `0x1654E`/`0x16552: jne 0x16679`,
  bypassing `0x165DF`), and the transfers are unlinked
  and completed with `USBD_STATUS_DEVICE_GONE` regardless (selected at SP4
  `0x19B00`-`0x19B18`, NUSB `0x196EC`-`0x19704`).
  Which lifetimes reach that branch cannot be read from the test site: NUKE
  is set from one controller-teardown routine (the `ENDPOINT_FLAG_NUKE` block
  below), so a disconnect does get an abort notice.
- Smaller contract facts from the same read: `CompletedLength` is a pure
  OUT (zeroed immediately before the call, SP4 `0x1691A`); for a non-ISO
  transfer it lands in `Transfer->CompletedTransferLen` (`Transfer+0x24`) and
  from there in `Urb->TransferBufferLength` (SP4 `0x1862E`-`0x18631`); for an
  ISO transfer it is discarded and an iso-flush routine runs instead
  (SP4 `0x169B7`, NUSB `0x16600`). The final URB status is usbport's choice,
  not the miniport's: `USBD_STATUS_DEVICE_GONE` (`0xC0007000`) if the endpoint
  is NUKEd or the transfer is flagged `DEVICE_GONE`, else
  `USBD_STATUS_CANCELED` (`0xC0010000`).
- There is also an ISO-only early return before any abort: if the
  transfer's last frame has not passed yet, the whole pass returns and retries
  later. The threshold is `StartFrame + NumberOfPackets + 1`, not the sum -
  both builds `inc` the computed sum before the unsigned compare (SP4
  `0x168D6: call Get32BitFrameNumber` -> `0x168EB: inc edi` ->
  `0x168EC: cmp [ebp-30h],edi` -> `0x168EF: jb`; NUSB `0x16522` ->
  `0x16534: inc edi` -> `0x16535` -> `0x16538`), so the retry condition is
  `currentFrame < StartFrame + NumberOfPackets + 1`. usbport waits one frame
  longer than the arithmetic alone suggests. A miniport whose frame number does
  not advance therefore strands ISO cancellations, the same dependency the
  post-open wait has.

#### The status callbacks and the NUKE path

Static, read out of both shipping builds with `link -dump -disasm`. The three
status callbacks are live, and the NUKE path completes transfers with no
miniport callback at all. Interface offsets are packet + `0x14` on SP4 and
packet + `0x10` on NUSB, so each slot appears at two different displacements.

- `GetEndpointStatus` (packet `0x84`) has one call site per build: SP4
  `0x1B947` (`call [eax+98h]`) inside a wrapper at `0x1B904`, NUSB `0x1B531`
  (`call [eax+94h]`) inside the wrapper at `0x1B4EE`. Both wrappers are
  instruction-for-instruction identical: a root-hub EP0
  (`Endpoint->Flags & 2`) short-circuits to status 0 without calling the
  miniport, everything else is called under `MiniportSpinLock`
  (FdoExt `+0x28C` on SP4, `+0x288` on NUSB) and the answer is cached at
  `Endpoint+0x2C`. The wrapper's own sole caller is SP4 `0x17C1D`, inside the
  URB-queueing routine at `0x17AC8`. Which reader consumes `Endpoint+0x2C`
  was not established in this pass - the offset is too common to histogram -
  so do not build on an assumed consequence of answering HALT; what is
  established is that the miniport is asked, and when.
- `SetEndpointStatus` (packet `0x88`) and `SetEndpointDataToggle` (packet
  `0x80`) have two call sites each per build. The interesting one is the
  reset-pipe URB path (SP4 `0x23CDC`, calls at `0x23EC4` and `0x23F6C`; NUSB
  `0x23844` and `0x238EC`), and it carries a precondition worth having: it
  refuses while the endpoint's own transfer list is non-empty, failing the
  URB with `0x80000400` (SP4 `0x23E88: lea eax,[esi+40h]` ->
  `0x23E8B: cmp [eax],eax` -> `0x23E8D: jne 0x23ED9` -> `push 80000400h`). The
  toggle reset is further gated on `Urb->UsbdFlags & 0x10`
  (`0x23E92`). Both then push a zeroed register as the third argument, so the
  only status the miniport is ever asked for is `USBPORT_ENDPOINT_RUN 0`.
  The second pair is on the device-restore path (SP4 `0x27742`/`0x2776D`, NUSB
  `0x270BA`/`0x270E5`).
- `ReopenEndpoint` (packet `0x2C`) is called there too.
  `USBPORT_RestoreDevice` (SP4 `0x26F30`, sole caller `0x2D809`) branches on
  `Endpoint->Flags & 8` (`ENDPOINT_FLAG_NUKE`) at SP4 `0x2743D`: NUKE clear
  takes the `ReopenEndpoint` + `SetEndpointDataToggle` + `SetEndpointStatus`
  path, NUKE set falls through to the full
  `QueryEndpointRequirements` + `OpenEndpoint` rebuild at `0x27528`/`0x2755D`.
- On a NUKEd endpoint usbport completes and frees the
  transfers itself with no miniport involvement, so no miniport-side
  arrangement can keep a DMA mapping alive. The chain is already in
  `tools/win2ksp4-extracted/usbport-abort-disasm.txt` and reads end to end:
  `0x16905: test byte ptr [eax+4],8` / `0x16909: jne 0x16A30` skips the
  `AbortTransfer` slot entirely, `USBPORT_FlushCancelList` selects
  `USBD_STATUS_DEVICE_GONE` at `0x19B00`-`0x19B11` and calls
  `USBPORT_CompleteTransfer` at `0x19B21`, which releases the map registers
  (`0x18764`, `0x187EE`), frees the MDL (`0x18803`) and `ExFreePool`s the
  record (`0x18973`). Only stopping the endpoint helps, which is what this
  driver does. This applies to controller teardown only; see the next
  subsection for what a device removal does.

#### `ENDPOINT_FLAG_NUKE` is a controller-teardown flag

Static, both builds. A device removal aborts through the miniport and then
waits for it; only controller teardown sets NUKE.

This subsection is written out at length so that it can be re-derived from the
text alone: `tools/` is git-ignored by the provenance rule in `README.md`, so
the annotated listings
(`tools/{win2ksp4,nusb}-extracted/usbport-removedevice-disasm.txt`) are a
convenience copy on whichever host last ran the pass, not the record. Two
commands reproduce everything below:

```
link -dump -disasm win2ksp4-extracted\USBPORT.SYS > sp4.asm
grep -E "or +.*,(0000000)?8h?$" sp4.asm
```

(`link.exe` from `tools\ntddk\bin` - the Win2000 DDK's own COFF dumper, same RVAs
as dumpbin.) The second command is the whole-image negative, and its result is
nine hits per build, enumerated here so the negative is checkable without
re-running it. SP4 / NUSB, in address order:

| SP4 | NUSB | What it writes |
|---|---|---|
| `0x1098A` | `0x1091A` | FdoExt flag byte |
| `0x109A3` | `0x10933` | FdoExt flag byte |
| `0x11173` | `0x110C9` | FdoExt flag byte |
| `0x1530C` | `0x1514A` | FdoExt flags |
| `0x165BD` | `0x16209` | `TRANSFER_FLAG_SUBMITED`, *Transfer*`+0x04` |
| `0x1A66C` | `0x1A256` | `or dh,8` - bit 11 of a Transfer flag word |
| `0x1BAA8` | `0x1B692` | `Endpoint->Flags \|= NUKE`, the only one |
| `0x2697C` | `0x262FA` | `DeviceHandle->Flags` (the removal mark) |
| `0x2DCBD` | `0x2D3F7` | unrelated, `+0x48` |

Only one row targets `Endpoint+0x04`. If a third build ever adds a row there,
this reading is what has to be re-taken.

- Exactly one instruction per image sets `Endpoint->Flags |= 8` - SP4
  `0x1BAA8`, NUSB `0x1B692`, both `or al,8` after a `test al,2`
  (`ENDPOINT_FLAG_ROOTHUB_EP0`) skip. See the sweep table above for why that
  is a whole-image result rather than one function's extent; the near-miss
  that misleads is `TRANSFER_FLAG_SUBMITED` at *Transfer*`+0x04` - same
  displacement, same bit, different structure.
- That instruction lives in `USBPORT_NukeAllEndpoints` (SP4 `0x1B9BA`,
  NUSB `0x1B5A4`), one argument `FdoDevice`, which walks the FDO-wide
  endpoint list (`FdoExt+0x6B8` on SP4, `+0x65C` on NUSB; link at
  `Endpoint+0x60`) and skips only `ENDPOINT_FLAG_ROOTHUB_EP0`. It is not
  given a device handle and cannot be scoped to one device.
- All three call sites per build are controller teardown, each immediately
  after the miniport's own `StopController` slot returns (SP4 `0x1ED56` ->
  `0x1ED5C` and `0x1F387` -> `0x1F38D`, NUSB `0x1E688` -> `0x1E68E` and
  `0x1EC6F` -> `0x1EC75`), and one of them is followed by the `rep stos` that
  zeroes the miniport extension (SP4 `0x1F3A8`, NUSB `0x1EC90`).
- A device removal takes a different route entirely.
  `USBPORT_RemoveDevice` (trace tag `REMV`, SP4 `0x26942`, NUSB `0x262C0`)
  marks `DeviceHandle->Flags |= 8` (SP4 `0x2697C`, NUSB `0x262FA`), calls
  `USBPORT_AbortAllTransfers` (SP4 `0x26ED0`, NUSB `0x26848`), and then
  spins on the device handle's reference count at PASSIVE_LEVEL,
  `USBPORT_Wait(FdoDevice, 100)` per lap (SP4 `0x269F3` -> `0x11636`, NUSB
  `0x1158E`), until it drains.
- `USBPORT_AbortAllTransfers` walks the handle's pipe list
  (`DeviceHandle+0x5C`) and sets `Endpoint->Flags |= 0x20` - not `8` -
  (SP4 `0x26EFA`, NUSB `0x26872`), runs `USBPORT_AbortEndpoint` and a flush
  per endpoint, then polls its own busy predicate with the same 100 ms wait.
  Because the endpoint is *not* NUKEd, every transfer the miniport was given
  reaches the `AbortTransfer` slot on the ordinary
  `USBPORT_DmaEndpointPaused` pass before it is moved to the cancel list and
  freed. `Transfer->Flags |= 0x80` (SP4 `0x1AD7C`) is what still makes the
  final URB status `USBD_STATUS_DEVICE_GONE` (`0x19B0D`), so a disconnect
  looks like the NUKE case from above without being one.
- Consequence for the miniport: usbport never reclaims a transfer record
  the miniport still holds without asking for it back first. So
  `xhciDevDisown`'s unbounded wait for the port to confirm down is not a
  freed-memory window, and a "forget rather than complete" shortcut there
  is not needed. The disown/PortDisabled split keeps its reason.
- Method note: a flag TEST establishes what a branch does, never who sets
  the flag. The store site can be in another function with another
  argument, and here it reverses the answer the test site suggests.

### Root hub

Contexts and locking are callback-specific. The root-hub endpoint worker
services class commands at DISPATCH_LEVEL. ReactOS takes
`MiniportSpinLock` around `RH_GetPortStatus` and `RH_GetHubStatus`
[roothub.c:148-164], but calls the Set/Clear feature callbacks without that
lock [roothub.c:170-285]. Do not treat the root-hub family as universally
serialized; confirm the NUSB binary's behavior and protect shared miniport
state independently.

#### The root-hub paths in the shipping builds

Static, both shipping builds. Extracts in
`tools/{win2ksp4,nusb}-extracted/usbport-roothub-disasm.txt`; the SP4 one
carries the full annotation and the NUSB one records only agreement and
difference. The pass was done with `link -dump -disasm` from the Windows 2000
DDK's own `bin` directory, which is the same COFF dumper as `dumpbin` and
prints the same RVAs, so no Visual Studio install is needed to repeat it.

- `USBPORT_MPStatusToRHStatus` is real and has the ReactOS shape
  (SP4 `0x29D8E`-`0x29D9F`, NUSB `0x2964A`-`0x2965B`, seven instructions
  each - `xor`, `cmp`, `je`, `cmp`, `setne`, `inc`, `ret`):
  `if (mp == 0) return 0; return (mp != 1) + 1;`. So the section 2
  status-mapping table is confirmed on both primary targets:
  `MP_STATUS_FAILURE` is the one value that leaves an endpoint-0 request
  queued instead of failing it.
- The mapped call sites are four in SP4 and three in NUSB. Common to
  both: the class Set/Clear feature routing tail, the *standard* GET_STATUS
  that runs `RH_GetStatus`, and the class GET_STATUS that runs
  `RH_GetPortStatus`/`RH_GetHubStatus`. SP4 has a fourth - a hub-directed
  feature tail (`0x207FA`) that calls `RH_ClearFeaturePortOvercurrentChange`
  with `Port = 0`. A miniport must therefore tolerate a port index of 0 on
  that one callback on Win2000; it is not reachable that way on Win98.
- The class command routine validates the port itself. `wIndex` is taken
  from the SETUP packet and checked `1 <= wIndex <= bNumberOfPorts` before any
  callback runs; out of range returns `RH_STATUS_UNSUCCESSFUL` without
  calling the miniport. Class GET_STATUS additionally requires
  `wLength >= 4`. An unrecognised feature index becomes `MPStatus 5`, which
  maps to `RH_STATUS_UNSUCCESSFUL`.
- `RH_EnableIrq`/`RH_DisableIrq` sit in a notification lifecycle, not in an
  interrupt path. The observed call-site facts:
  - Each image also dispatches the status-change scan with no
    `RH_DisableIrq` beside it (SP4 `0x218FB`, NUSB `0x21289`), so "every scan
    is preceded by a disable" is not shown.
  - `USBPORT_InvalidateRootHub` calls `RH_DisableIrq`, and each image has a
    second disable site: SP4 `0x21C56` and `0x1D6C9`, NUSB `0x215E4` and
    `0x1D265`.
  - The scan calls `RH_EnableIrq` (the only such site in either image, SP4
    `0x215F4`, NUSB `0x20F82`) on exactly one exit: the one where its result
    is still `RH_STATUS_NO_CHANGES`, i.e. it found nothing and is leaving the
    interrupt transfer pending. Success (`0x215FC`) and error (`0x21611`) both
    bypass it, and so does the early return below.
  - A whole-image operand search finds `0x215F4` as the only enable site in
    SP4, and one in NUSB (`0x20F82`), so nothing else re-opens the gate: a
    disable is genuinely not guaranteed a matching enable.
  - What that does and does not establish: it establishes the call sites (an
    invalidate closes the gate, and the one enable site reopens it only when
    a scan came up empty). It does not establish what the callback body
    should contain, because usbport's binary cannot show anything about the
    miniport's own MMIO.
  - This driver's decision to implement them as a pure software gate on
    whether a port change calls `UsbPortInvalidateRootHub`, and never to
    touch `IMAN.IE`, follows from that lifecycle plus an xHCI-side argument
    (`IMAN.IE` gates the whole interrupter, so masking it here would silence
    transfer completions as well), not from the disassembly alone. Recorded
    as a decision on the callback row below, not as a binary confirmation.
- The status-change endpoint's contract, read out of `USBPORT_RootHubSCE`
  (SP4 `0x2146C`). It seeds its result with `RH_STATUS_NO_CHANGES`; requires
  a non-NULL buffer of at least `(bNumberOfPorts >> 3) + 1` bytes; zeroes the
  bitmap; then walks ports 1..`bNumberOfPorts` calling `RH_GetPortStatus`,
  where any nonzero return sets `RH_STATUS_UNSUCCESSFUL` and abandons the
  entire scan; then calls `RH_GetHubStatus` under the same rule. It tests
  only `wPortChange & 0x1F` (the five standard change bits) and only
  `wHubChange & 0x03`; bit 0 of the bitmap is the hub, bit *n* is port *n*.
  Anything a miniport reports outside those masks is invisible here.
  - There is also an early return (`0x21488`-`0x2148F`): when the build's own
    flag byte bit 1 is set (SP4 `FdoExt+0x1C6` at `0x21488`, NUSB
    `FdoExt+0x1C2` at `0x20E16`) the routine returns the seeded
    `RH_STATUS_NO_CHANGES` *before* it validates the buffer, calls any
    callback, or calls `RH_EnableIrq`.
  - So a scan can end in "no changes" with the gate left closed and no
    miniport callback reached at all. A miniport must not treat
    `RH_DisableIrq` as guaranteed to be followed by either a status query or
    a matching `RH_EnableIrq`, and must not carry state that only an
    `RH_EnableIrq` would release.
- The power/chirp startup path discards every callback return.
  `USBPORT_RH_SetFeatureUSB2PortPower` (SP4 `0x229CA`) powers each companion
  controller's ports 1..N and then the requested port on this controller,
  ignoring all returns and itself returning 0.
  `USBPORT_RootHubPowerAndChirpAllCcPorts` reads `RH_GetRootHubData` for the
  port count and powers 1..N, returns discarded throughout. Here the two
  targets diverge:
  - SP4 waits 100 ms (`0x22C55`-`0x22C58`), then gates the chirp loop
    on the interface `Version >= 200` (`0x22CBB`), then calls
    `RH_ChirpRootPort` for 1..N (`0x22CCE`). That is ReactOS's documented
    field test appearing as a real version comparison.
  - NUSB does neither. It calls power at `0x225EC` and goes straight to
    its chirp gate at `0x225F7`/`0x225FC`, calling packet slot `0x12C` at
    `0x22623`. There is no 100 ms wait and no interface `Version`
    comparison - consistent with section 3's finding that NUSB's wrapper has
    no `Version` field at all. Both loops are gated on an internal `+0x48`
    bit 1 instead.

  So a miniport must not expect the settle wait. The registration gate is
  identical in both builds; it is the call-site check that only SP4 has,
  and the difference is a hazard rather than a stylistic one. The verified
  reading:
  - Both builds zero their wrapper and copy `0x12C` bytes below interface
    Version 200, `0x13C` at or above - SP4 `0x27D9E`-`0x27DA7` (`push 54h`,
    336 bytes), compare `0x27E0F`, length `0x27E14`, `+0x10` at `0x27EBB`,
    copy to wrapper+0x14 at `0x27EC0`/`0x27EC6`; NUSB `0x27716`-`0x2771F`
    (`push 53h`, 332 bytes), compare `0x27782`, length `0x2778A`, `+0x10` at
    `0x27831`, copy to wrapper+0x10 at `0x27836`/`0x2783C`. Same gate, same
    source pointer above 200. This is section 3's 316/300 split.
  - SP4 then checks again at the call site (`Version >= 0xC8` at
    `0x22CBB`), so below 200 it simply skips the call.
  - NUSB does not. It tests only `FdoExt+0x48` bit 1 (`0x225FC`) - a flag
    set at `0x2D3D8` after successful root-hub setup, on a path with no
    `Version` test - and calls `wrapper+0x13C` at `0x22623` with no null
    check on the loaded pointer. Below Version 200 that slot is the zero
    the wrapper was initialised with.

  So `Version >= 200` is a safety requirement on the Win98 target, not
  merely the conformant choice: a miniport registering below 200 would be
  called through a null pointer during root-hub startup. `xhci98.sys` passes
  200, so this does not bite - but it is a much stronger reason to keep doing
  so than "it is what the shipping miniports declare".
- `NumberOfPorts = 0` really does ask for about 1 GB, and no guard stops
  the request. `USBPORT_RootHubCreateDevice` computes
  `maskBytes = ((NumberOfPorts - 1) >> 3) + 1` and allocates
  `2*maskBytes + 7 + 0x2B` bytes; at zero that is `0x40000032`. ReactOS's
  `ASSERT` has no counterpart in either shipping binary - both go straight
  into the arithmetic. What the instructions then show is a null check at
  `0x2E235`-`0x2E243` whose null branch builds no PDO; they cannot show the
  allocator's answer, so "a 1 GB request fails" is expectation, not proof.
  Identical arithmetic in NUSB (`0x2D939`). Do not probe this at runtime.
- `PowerOnToPowerGood` is copied straight into `bPowerOnToPowerGood` as a
  `UCHAR` (SP4 `0x2E442`), alongside `bNumberOfPorts`,
  `wHubCharacteristics` and `bHubControlCurrent` from the same structure -
  which also confirms `USBPORT_ROOT_HUB_DATA`'s layout from the code rather
  than the header. Units are 2 ms, so xHCI's 20 ms rule is the value 10.
  The removable mask is filled with `0x00` and the power mask with `0xFF`.
  (Answers section 9 open item 7.)
- Locking. A grep of each routine's own address range for the spin-lock IAT
  entries is not enough here: it cannot see a lock taken inside a helper.
  What the call graph shows, verified in both builds:
  - On the class GET_STATUS execution path and in the SCE scan, no
    spin-lock acquisition occurs in usbport-owned code, internal leaf helpers
    included. This is a completed transitive walk. Two exclusions ride on
    it: miniport callback bodies are not in
    these binaries, and GET_STATUS's *enclosing* function (from `0x2080C`)
    has other request branches that do reach the port-power lock - the
    statement is about the path, not the function.
  - The feature routing does reach a lock, on the `SET_FEATURE(PORT_POWER)`
    branch a USB2 miniport gets: `0x20638` tests `MiniPortFlags & 0x10`,
    `0x20641` calls `USBPORT_RH_SetFeatureUSB2PortPower` (`0x229CA`), whose
    `0x22A2E` calls `0x27ADA`; `0x27AED` acquires lock `0x2D120` through the
    `KfAcquireSpinLock` IAT entry `0x2CA30`, `0x27C3C` releases it through
    `0x2CA2C`, and `0x27C49` returns to `0x22A33`.
  - The `RH_SetFeaturePortPower` callbacks are invoked only *after* that
    return, at `0x22ADE` and `0x22B0A`. The helper holds the lock to build its
    companion-controller snapshot and drops it before calling either. NUSB has
    the identical ordering: `0x2004F` -> `0x20059` -> `0x222EC`, whose
    `0x22350` calls the helper at `0x27452`; acquire `0x27465`, release
    `0x275B4`, `0x275C1` returns to `0x22355`, then the callbacks at `0x22400`
    and `0x2242C`.

  So this helper releases its lock before either downstream
  `RH_SetFeaturePortPower` call. That is the whole claim, and it is
  narrower than "no root-hub callback is entered holding a lock", which the
  evidence does not reach: caller-held locking at callback entry remains
  unverified. "usbport does
  not serialize these callbacks" is likewise unsupported - all that has been
  shown is that this particular lock is not the counterexample. Protect
  root-hub state with the miniport's own interior lock regardless. Section 9
  item 9 stays open.

| Callback | Signature | Observed contract |
|---|---|---|
| `RH_GetRootHubData` | `VOID (ext, PVOID data)` [278-281] | Arg 2 is `PUSBPORT_ROOT_HUB_DATA` (section 5) [roothub.c:1013-1016]. Report only managed USB2 ports - but `NumberOfPorts` must be >= 1: ReactOS `USBPORT_RootHubCreateDevice` asserts it is nonzero before sizing the removable/power masks as `(NumberOfPorts - 1) / 8 + 1` [roothub.c:787-794]; both shipping binaries have no such assertion and go straight into the arithmetic (confirmed). At zero it unsigned-wraps to `0x20000000` mask bytes, so the descriptor allocation asks for ~1 GB. The instructions then null-check the result and build no PDO on the null branch - they cannot show what the allocator returns, so "it fails" is the expectation, not the proof. A miniport that does not yet know its port count must report a synthetic port, not zero (the Phase 3 stub reports one permanently disconnected port) |
| `RH_GetStatus` | `MPSTATUS (ext, PUSHORT)` [283-286] | Constant "self-powered hub OK". The one root-hub callback reached through the standard command path (device GET_STATUS) rather than the class one [roothub.c:397-415], and its return goes through the same `MPSTATUS` -> `RHSTATUS` mapping, so the section 2 table applies here too |
| `RH_GetPortStatus` | `MPSTATUS (ext, USHORT Port, PUSB_PORT_STATUS_AND_CHANGE)` [290-294] | `Port` is the raw hub-class `wIndex`: 1-based, validated against `bNumberOfPorts` before the call [roothub.c:124-152] - but not on the status-change-endpoint path, which walks 1..`bNumberOfPorts` itself [roothub.c:603-627]. Output is the standard USB hub port status/change bitmap (4 bytes). Return `MP_STATUS_SUCCESS` even when there is nothing to report: any nonzero return aborts that whole SCE scan with `RH_STATUS_UNSUCCESSFUL` [roothub.c:604-614], stalling the root hub's change pipe on every poll |
| `RH_GetHubStatus` | `MPSTATUS (ext, PUSB_HUB_STATUS_AND_CHANGE)` [296-299] | Constant zeros. Same two-path story as `RH_GetPortStatus` above: mapped through section 2's table on the class GET_STATUS path [roothub.c:160], but tested directly by the status-change endpoint [roothub.c:630, 657], where a nonzero return abandons the whole scan with `RH_STATUS_UNSUCCESSFUL`. Return `MP_STATUS_SUCCESS` even when reporting no changes |
| `RH_SetFeature...` / `RH_ClearFeature...` (12 entries: 4 Set + 8 Clear, packet `0xA0`-`0xCC`) | `MPSTATUS (ext, USHORT Port)` [301-359] | Port is 1-based on the ordinary class-command routing, which validates it against `bNumberOfPorts` first - but not universally: SP4's hub-directed path passes `Port = 0` to `RH_ClearFeaturePortOvercurrentChange` (`0x207EC` pushes zero, `0x207F3` calls packet `0xCC`), so that callback must tolerate 0 rather than index with it. Feature routing done by usbport from the hub SETUP packet [roothub.c:126-171ff]. Refuse an unsupported operation with `MP_STATUS_NOT_SUPPORTED`, never `MP_STATUS_FAILURE`; see the status-mapping table in section 2. Note `SET_FEATURE(PORT_POWER)` reaches a USB2 miniport through `USBPORT_RH_SetFeatureUSB2PortPower` [roothub.c:33-97], which powers every companion controller's ports first and discards the miniport's return; the root-hub startup power/chirp loop discards it too [roothub.c:989-992]. ReactOS calls these at DISPATCH_LEVEL without `MiniportSpinLock` [roothub.c:170-285], so they must not block and must use miniport-owned synchronization for shared state. A lock does appear on the USB2 port-power path, but the helper drops it before the callback: `USBPORT_RH_SetFeatureUSB2PortPower` calls a helper that acquires at SP4 `0x27AED` and releases at `0x27C3C`, returning at `0x27C49`, and only then invokes `RH_SetFeaturePortPower` at `0x22ADE`/`0x22B0A`; NUSB is identical. That is a statement about *this helper*, not about callback entry in general: caller-held locking remains unverified, so "usbport does not serialize these" stays unsupported either way - use miniport-owned synchronization. Reset: set PORTSC.PR, arm a `UsbPortRequestAsyncCallback` timeout, return, and report completion through the change bit when PRC arrives (the ReactOS EHCI miniport uses this same asynchronous shape [usbehci/roothub.c:364-394]). Because the timer cannot be cancelled, pass a reset generation and let only the matching still-armed PRC/timeout path claim completion |
| `RH_DisableIrq` / `RH_EnableIrq` | `VOID (ext)` [361-365] | Lifecycle read from the binaries: `USBPORT_InvalidateRootHub` calls `RH_DisableIrq`, and each image has a second disable site; the status-change scan calls `RH_EnableIrq` on its no-changes exit only - success, error, and the early return all bypass it - and that is the only enable site in each image, so a close is not guaranteed a matching open. Not shown, and not claimed: that every scan is preceded by a disable (each image also dispatches one with none). Per-build addresses, since these differ: SP4 enable `0x215F4`, disables `0x21C56` and `0x1D6C9`, unpaired dispatch `0x218FB`; NUSB enable `0x20F82`, disables `0x215E4` and `0x1D265`, unpaired dispatch `0x21289`. Project decision (from that lifecycle plus the xHCI argument that `IMAN.IE` gates the whole interrupter, not from the disassembly): implement as a pure software gate on whether a port change calls `UsbPortInvalidateRootHub`, hold no state that only an `RH_EnableIrq` could release, and never touch `IMAN.IE`, which would silence transfer completions too |
| `RH_ChirpRootPort` | `MPSTATUS (ext, USHORT Port)` [518-521] | Called once per port at root-hub start, after powering companion-controller ports [roothub.c:995-1042]. The return is discarded. The gating is not the same on both targets: SP4 waits 100 ms (`0x22C55`) and gates on a literal interface `Version >= 0xC8` (`0x22CBB`) before calling at `0x22CCE`, matching ReactOS; NUSB does neither - no wait, no `Version` compare (its wrapper has no `Version` field), reaching packet `0x12C` at `0x22623` gated only on an internal `+0x48` bit tested at `0x225FC`, with no null check on the slot. Do not rely on the settle wait. Both builds withhold the slot below Version 200 at *registration*, so the version rule holds either way - but on NUSB it is the only thing standing between a sub-200 miniport and a null call. Register at 200. EHCI-specific HS handshake; for xHCI return success without bus action |

`USB20_PORT_STATUS_RESERVED1_OWNED_BY_COMPANION (1 << 2)` [usbmport.h:288] -
a port-status bit usbport understands for EHCI companion routing; not used by
xHCI ports.

`RH_ClearFeaturePortEnable` and `RH_ClearFeaturePortPower` are device-gone
events, and by the time they arrive usbport has already freed the device's USB
address (batch 6-V, runtime, both targets). usbhub abandons a device by
disabling its port while it stays physically connected: on every cancelled
driver-install wizard and whenever a device's driver fails to start, which on a
driver that cannot yet serve a class is every device of that class. usbport
then returns the address to its bitmap, so the next device it enumerates may
be handed the same number.

A miniport that derives slot teardown from the connect change alone keeps the
old record, and the measured outcome is the refusal of the *next* device:
ports 1 and 2 disabled after two cancelled wizards, then a replug on port 3
answered `slot: refused SET_ADDRESS for address=00000001`, the address the
still-recorded device on port 1 held.

The refusal rule is right and stays (two records must not share an address);
the port-disable callback is the second teardown trigger, announced *after*
the write that takes the port out of service (`XhciSlotPortDisabled`,
`DevicesDisabledOut`). It is not derived from an observed `PED` = 0, because a
port under reset reads that too (Table 5-27 p.371) and a shadow-derived
trigger would tear down every device in the middle of the reset that is
enumerating it.

#### What the two USHORTs mean, and how well that is known

`RH_GetPortStatus` fills `wPortStatus` and `wPortChange`; `RH_GetHubStatus`
fills `wHubStatus` and `wHubChange`. These are USB hub-class fields, not usbport
inventions, and their provenance is weaker than everything else in this
document. It is recorded here rather than left implicit, because the port
reporting is built on it.

| `wPortStatus` | bit | | `wPortChange` | bit |
|---|---|---|---|---|
| PORT_CONNECTION | 0 | | C_PORT_CONNECTION | 0 |
| PORT_ENABLE | 1 | | C_PORT_ENABLE | 1 |
| PORT_SUSPEND | 2 | | C_PORT_SUSPEND | 2 |
| PORT_OVER_CURRENT | 3 | | C_PORT_OVER_CURRENT | 3 |
| PORT_RESET | 4 | | C_PORT_RESET | 4 |
| PORT_POWER | 8 | | | |
| PORT_LOW_SPEED | 9 | | | |
| PORT_HIGH_SPEED | 10 | | | |

`wHubStatus`: local power lost bit 0, over-current bit 1; `wHubChange` the same
two. `wHubCharacteristics` (in `USBPORT_ROOT_HUB_DATA`): logical power switching
mode bits 1:0 (00 ganged, 01 individual), compound device bit 2, over-current
protection mode bits 4:3 (00 global, 01 individual), TT think time bits 6:5.

Source: the USB 2.0 specification, Tables 11-21, 11-22, 11-19/11-20 and
11-13. There is no USB 2.0 PDF in `docs/references/`, so unlike every xHCI
table in this project these were not transcribed against a local copy. Two
independent facts bound them without confirming the ordering:

- the status-change scan in both shipping builds reads only
  `wPortChange & 0x1F` and `wHubChange & 0x03` (binary-confirmed above), which
  is exactly five port change bits at 4:0 and two hub change bits at 1:0;
- ReactOS's EHCI miniport fills the same field set
  (`external/reactos/usbehci/roothub.c` - `CurrentConnectStatus`,
  `PortEnabledDisabled`, `Suspend`, `OverCurrent`, `Reset`, `PortPower`,
  `LowSpeedDeviceAttached`, `HighSpeedDeviceAttached`), which corroborates the
  set but not the positions, since the structure it fills is not mirrored here.

What that leaves open: the order *inside* those five, and the position of
the two speed bits. Both are confirmed by the checkpoint runs rather than by a
document: a wrong order shows up as a device enumerating at the wrong speed,
or as a connect that never arrives, on a target VM. If a USB 2.0 spec PDF is
ever added to `docs/references/`, transcribe these against it and delete this
paragraph.

### Debug / single-packet

`StartSendOnePacket` / `EndSendOnePacket`:
`MPSTATUS (ext, PVOID, PVOID, PULONG, PVOID, PVOID, ULONG, USBD_STATUS *)`
[368-388]; `PassThru`: `MPSTATUS (ext, PVOID, ULONG, PVOID)` [390-395].
Debug transfer path; implement as `MP_STATUS_NOT_SUPPORTED` stubs first and
confirm usbport tolerates that in the spike.

#### What reaches `PassThru`

Static, both shipping builds, traced end to end with `dumpbin /disasm` over
`tools/nusb-extracted/USBPORT.SYS` (5.00.2195.5652, the Windows 98 binary)
and `tools/win2ksp4-extracted/USBPORT.SYS` (5.00.2195.6681). The endpoint-path
census above established that the slot is called through four
register-indirect sites; this is what drives them. Nothing was executed to
establish the static half; the runtime observations follow at the end.

The displacements quoted in that census are interface offsets, not packet
offsets. NUSB calls `[interface+0xF0]` and SP4 `[interface+0xF4]`; both are
packet offset 0xE0, through the wrapper prefix this document already
records (0x10 on NUSB, 0x14 on Win2000/XP, because NUSB's wrapper drops
`Version`). Cross-checked in the same listings: NUSB's FDO start reads
`[interface+0x14]` as `MiniPortFlags` (packet 0x04) and `[interface+0x20]` as
`MiniPortExtensionSize` (packet 0x10, added to the extension base to size the
allocation) at `0001C1B9`/`0001C1BF`; SP4 calls `InterruptNextSOF`
(packet 0x70) at `[eax+0x84]`. So the packet table in section 3 is right and
the census quotes a different base.

Site 2 is the user escape, and it is the one that matters. The chain, in
full:

| Step | NUSB | SP4 |
|---|---|---|
| `IRP_MJ_DEVICE_CONTROL` entry, splits on the extension signature (`'HFDO'` vs `'RPDO'`) | `00011340` | same shape |
| HCD IOCTL switch | `0002E00C` | `0002E8E6` |
| USBUSER dispatcher | `00027AC2` | `0002814C` |
| request jump table (8 entries, requests 1-8) | `00027E9E` | `00028528` |
| request 3 case, minimum body 0x18 | `00027CB6` | `00028340` |
| `PassThru` handler | `00028978` | `000290BA` |
| the call itself | `00028A27` | `00029169` |

The IOCTL is `0x00220438` - `CTL_CODE(FILE_DEVICE_UNKNOWN, 0x10E,
METHOD_BUFFERED, FILE_ANY_ACCESS)`, read off the switch's own subtraction
chain (`sub ecx,220400h`, then `-4`, `-4`, `-0x1C`, `-0x14`). Its four
neighbours in that chain are `0x220400`/`0x220404` (diagnostic mode on/off,
which only set and clear a flag byte in the extension) and `0x220408` /
`0x220424` (the two name queries). Anything else is
`STATUS_INVALID_DEVICE_REQUEST`. In the public DDK naming these are
`IOCTL_USB_USER_REQUEST` (function code `USB_IOCTL_INDEX + 15`) and
`USBUSER_PASS_THRU`; the numbers above are read from the binaries, not
copied from a header - the Windows 2000 DDK in `tools/ntddk` ships
`usbioctl.h`, which stops at function code 0x10C, and no `usbuser.h`.

The buffer contract, all of it enforced before the miniport is reached:

```
offset  size  field
0x00    4     UsbUserRequest        == 3 for PassThru
0x04    4     UsbUserStatusCode     out; see the status list below
0x08    4     RequestBufferLength   must EQUAL the IOCTL's input length
0x0C    4     ActualBufferLength    out; bytes usbport considers valid
0x10   16     the service GUID      -> PassThru's 2nd argument
0x20    4     ParameterLength       -> PassThru's 3rd argument
0x24    N     Parameters            -> PassThru's 4th argument
```

- Input and output lengths must be equal (`00027AAE`), else
  `STATUS_INVALID_PARAMETER`. One buffer, in and out.
- Length >= 0x10 (`00027BC2`), else `STATUS_BUFFER_TOO_SMALL`.
- `RequestBufferLength` must equal the input length (`00027BE2`), else
  header status 4.
- Common per-request check (`00027A00`): `ActualBufferLength` is set to
  `0x10 + minimumBody` and compared against `RequestBufferLength`; for
  PassThru `minimumBody` is 0x18, so the floor is 0x28 bytes.
- `ParameterLength <= 0x10000` (`0002898A`), else header status 5.
- `0x24 + ParameterLength <= RequestBufferLength` (`000289A7`), else header
  status 7.
- On the way in usbport sets `ActualBufferLength = ParameterLength + 0x28`
  (`00028A08`), and on the way out it reports
  `Information = min(RequestBufferLength, ActualBufferLength)` (`00027E5B`).
  Sizing the buffer at `ParameterLength + 0x28` therefore returns the whole
  thing, and at `0x24 + ParameterLength` it returns all of `Parameters`;
  nothing is lost either way.

The buffer the miniport writes is a NON-PAGED kernel copy. usbport
`ExAllocatePoolWithTag(NonPagedPool, min(in,out), 'usbp')`s a copy of the
system buffer (`00027AFD` - the pool type is the zeroed `ebx`), hands the
miniport interior pointers into that copy, and memcpys it back afterwards
(`00027E77`). So the miniport never sees a user address, needs no probe, and
may write the block with a spin lock held at DISPATCH_LEVEL. Do not take
the pool type on trust by eye: the symbolic-link allocation earlier in the
same image passes `1` (paged) to the same import.

IRQL and locks at the callback: the dispatch entry takes the device lock
only to test the removed flag and releases it on both exits (`0001138F`
acquire, `000113A0` / `000113F6` release) before the switch runs, so
`PassThru` is entered at PASSIVE_LEVEL holding no usbport lock - it is
free to take the miniport's own lock.

Status mapping. `UsbUserStatusCode` is zeroed on entry and set from the
return: `0` leaves it `0`, anything nonzero makes it 6 (`00028A37`). The
NT status stays `STATUS_SUCCESS` in every one of these branches, so a
caller must read the header's status field and not just the
`DeviceIoControl` return. The codes reachable on this path are 3 (a gated
request), 4 (bad header length), 5 (bad parameter), 6 (miniport error) and
7 (buffer too small) - matching the public `USB_USER_ERROR_CODE` ordering.

Reachability from user mode. The HCD FDO's start path unconditionally
builds `\DosDevices\HCD<n>` - a 17-wchar template at NUSB `00011C6E`, copied
to pool and patched at `[buf+0x1E]` with `'0' + controllerIndex` (`00011CF7`)
- and `IoCreateSymbolicLink`s it onto the `\Device\USBFDO-<n>` name
(`00011D19`), setting a success bit in the extension. `IRP_MJ_CREATE` and
`IRP_MJ_CLOSE` on that FDO are completed `STATUS_SUCCESS` with no work
(`00011411` / `00011419` -> `000114B4`). So the route is `CreateFile` on
`\\.\HCD0` followed by `DeviceIoControl(0x220438, ...)`, and `FILE_ANY_ACCESS`
means no particular access mask is needed. Static only - that Windows
98's ntkern honours the `\DosDevices` link and that a Win32 `CreateFile`
reaches it is the one part of this a VM run has to confirm.

Request 3 is NOT gated. The dispatcher tests
`UsbUserRequest & 0x30000000` (`00027BF3`) and only then consults an enable
flag, failing with header status 3 when it is clear. PassThru is an ordinary
low-numbered request and skips that entirely, so
`IOCTL_USB_DIAGNOSTIC_MODE_ON` is not a prerequisite.

Site 1 is usbport's own, and it is gated. At NUSB `000285FC` / SP4
`00028D3E` usbport calls `PassThru` itself with a fixed GUID
(`{022252A1-ED5D-4E3F-976F-B2D9DB3D2BD3}`, NUSB `0002C440`) and an 8-byte
parameter block `{ port status/change (out, +0); USHORT port number (in,
+4) }`, keeping only the low 16 bits of the result. It reaches that call only
through USBUSER request `0x20000007`, one of the `0x2xxxxxxx` test-mode
family, so it cannot fire during ordinary operation.

The fallback test matters: usbport retries through `RH_GetPortStatus`
(packet 0x98) only when the return is exactly 6 (`00028603`). A miniport that answered an
unrecognised GUID with `MP_STATUS_FAILURE` would silently suppress that
fallback and hand back a zeroed port status. `xhciPassThru` already returns
6, and must keep returning exactly 6 for every GUID it does not
recognise.

What the static reading alone does not establish is that any of it works on a
running Windows 98: the symbolic link, the `CreateFile`, the IOCTL round trip
and the miniport entry are readings of the binary Windows 98 runs, not
observations of it running. The two observations below cover that.

Executed on the development host. The reading above was written entirely from
disassembly. The user-side tool (`xhcisnap/`, `-probe`) was then run on the
Windows 11 development host, and every prediction came back:

| Control | Predicted | Observed |
|---|---|---|
| `CreateFile` on `\\.\HCD0` | opens | opens |
| IOCTL `0x220438`, request 3, our GUID | reaches a miniport, which declines -> status 6 | 6 |
| request code 15 | not in the 8-entry table -> status 2 | 2 |
| `RequestBufferLength` one less than the input length | must be equal -> status 4 | 4 |
| a 0x20-byte buffer | floor is 0x28 -> status 7 | 7 |

That is the IOCTL number, the request code and its table bound, the header's
length-equality rule, the per-request size floor, the symbolic link, and the
nonzero-MPSTATUS-to-6 mapping, all behaving as read out of the 2002 binaries.

What that is not. That host runs a modern descendant of usbport, not NUSB's
5652 and not SP4's 6681, so it raises confidence in the derivation without
being an observation on either target. Windows 2000 has still not run any of
this; the same three clauses are still owed on the SP4 build.

Observed on Windows 98 itself, on the NUSB 5652 build this block was read
out of, in the 2a guest (`run-13e.md` P10, step 3). `CreateFile("\\.\HCD0")`
opens, the IOCTL round trip completes, and the four `-probe` controls return
0 / 2 / 4 / 7: a miniport that answers, an unknown request code, a
disagreeing `RequestBufferLength`, and a buffer under the 0x28 floor. The
driver's own trace carries `cb PassThru` lines, so the callback was reached
rather than inferred from a status. The windowing arithmetic held too: 87,592
extension bytes in two windows. So the symbolic link, the CreateFile, the
IOCTL and the miniport entry are all observations on the Windows 98 target,
not readings of its binary.

The same run found a limit on the route rather than on the derivation.
usbport builds its link at a fixed index from its own
controller number, with no retry and no fallback (NUSB `00011C90`; the
`'0' + controllerIndex` patch at `00011CF7`). On a machine where something
else already owns that name - Windows 98's *own* USB stack does, for a UHCI
or OHCI controller it drives - `IoCreateSymbolicLink` simply fails and no
link for the usbport controller ever appears.

Measured: with the guest's
UHCI present, `\\.\HCD0` opened but every IOCTL failed and the driver's trace
showed no `cb PassThru` at all; `HCD1` through `HCD3` did not exist.
Removing the UHCI made it work first time. The failure is silent - the driver
starts and runs perfectly and only the reading channel is missing - so any
tool using this route should check the route before trusting its absence.

## 5. Support structures (x86 layouts)

### USBPORT_RESOURCES [usbmport.h:44-65] - StartController argument

`C_ASSERT` size = 52 (0x34) bytes on x86.

| Offset | Field | Type | Meaning |
|---|---|---|---|
| 0x00 | `ResourcesTypes` | ULONG | Bitmask: PORT 1 / INTERRUPT 2 / MEMORY 4; check MEMORY+INTERRUPT before touching anything [usbehci.c:1176-1180] |
| 0x04 | `HcFlavor` | enum | `USB_CONTROLLER_FLAVOR` (NT header enum) |
| 0x08 | `InterruptVector` | ULONG | Already used by usbport for `IoConnectInterrupt` [pnp.c:788-798] |
| 0x0C | `InterruptLevel` | KIRQL + 3 pad | |
| 0x10 | `InterruptAffinity` | KAFFINITY | |
| 0x14 | `ShareVector` | BOOLEAN + 3 pad | |
| 0x18 | `InterruptMode` | enum | LevelSensitive for PCI INTx |
| 0x1C | `Reserved` | ULONG_PTR | |
| 0x20 | `ResourceBase` | PVOID | Mapped VA of BAR0 - use directly, do not map anything [usbehci.c:1182] |
| 0x24 | `IoSpaceLength` | ULONG | BAR length |
| 0x28 | `StartVA` | ULONG_PTR | VA of the `MiniPortResourcesSize` common buffer [pnp.c:826] |
| 0x2C | `StartPA` | ULONG | PA of the same buffer [pnp.c:827] |
| 0x30 | `LegacySupport` | UCHAR | OUT: set if BIOS legacy support was detected/handled; usbport records it [pnp.c:839-854] |
| 0x31 | `IsChirpHandled` | BOOLEAN | IN: EHCI branches port-power bring-up on it [usbehci.c:1263] |
| 0x32 | `Reserved2/3` | UCHAR x2 | |

The EHCI pattern for the common-buffer block - a single struct
(`EHCI_HC_RESOURCES`) whose `sizeof` is `MiniPortResourcesSize`, carved via
`FIELD_OFFSET` from `StartVA`/`StartPA` [usbehci.c:889-975, 3639] - is the
model for the xHCI block (DCBAA, command ring, ERST, event ring, scratchpad
array; scratchpad pages need their own PAGESIZE alignment, so place them last
on a page boundary or size the block generously).

The common-buffer path was read out of `USBPORT_StartDevice` and
`USBPORT_AllocateCommonBuffer` in both shipping builds (static): NUSB `5.00.2195.5652` (`USBPORT_AllocateCommonBuffer` @ VA
`0002E300`) and SP4 `5.00.2195.6681` (@ VA `0002EBDA`). The two are
instruction-for-instruction the same apart from FDO-extension field offsets.
Extracts in `tools/{nusb,win2ksp4}-extracted/usbport-commonbuffer-disasm.txt`;
the design consequences are in `docs/contributing/design/04-controller-common-buffer.md`.

- The DMA adapter is 32-bit. `StartDevice` zeroes a 40-byte
  `DEVICE_DESCRIPTION` and sets `Version = 0`, `Master = 1`,
  `ScatterGather = 1`, `Dma32BitAddresses = 1`, `InterfaceType = PCIBus`,
  `DmaWidth = Width32Bits`, `DmaSpeed = Compatible`,
  `MaximumLength = MAXULONG`, and never writes `Dma64BitAddresses`.
  Matches ReactOS `pnp.c:555-562` field for field.
- `StartPA` is `LogicalAddress.LowPart` only. `HighPart` is retained in
  usbport's private header purely for the later `FreeCommonBuffer`. The
  miniport may treat `StartPA` as a `ULONG` with an implicit zero high DWORD.
- `StartVA` and `StartPA` are both page-aligned - each is masked with
  `~(PAGE_SIZE - 1)` before publication. One offset table therefore satisfies
  the spec's alignment/no-cross-boundary rules in both address spaces.
  Verify it at `StartController` rather than assuming it.
- The allocation is `ROUND_TO_PAGES(MiniPortResourcesSize + 0x30)`, with
  usbport's 48-byte header placed at the *end* so the miniport's block starts
  on the page boundary. Corroborated by usbport's own second request in the
  same function: `0xFD0` = one page once the header is added.
  `AllocateCommonBuffer` is called with `CacheEnabled = TRUE` - the block
  is cached, and that is not the miniport's choice.
- usbport zeroes the block (`rep stos` over `BufferLength +
  LengthPadded`) before calling `StartController`.
- `MiniPortFlags` bit `0x100` (`NO_DMA`) suppresses all of it:
  `StartDevice` skips `IoGetDmaAdapter` *and* overwrites its own copy of
  `MiniPortResourcesSize` with zero. No adapter, no buffer, no diagnostic.
  `xhci98.sys`'s `0x95` does not set it; do not add it.
- Incidental confirmations of section 3's packet offsets, from the same
  routine: `MiniPortResourcesSize` at packet `0x24`, `MiniPortExtensionSize`
  at `0x10` (used as the `rep stos` count that zeroes the miniport
  extension), `StartController` at `0x38`, called as
  `StartController(MiniPortExt, Resources)`, and `Resources->StartVA` /
  `StartPA` written at `0x28` / `0x2C`.

### USBPORT_ENDPOINT_PROPERTIES [usbmport.h:67-93]

`C_ASSERT` size = 64 (0x40) bytes on x86. Filled by usbport before
QueryEndpointRequirements/OpenEndpoint [endpoint.c:885-993].

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0x00 | `DeviceAddress` | USHORT | usbport's address for the device (0 during initial EP0 open; the miniport's usbport-address -> Slot ID map keys off this) |
| 0x02 | `EndpointAddress` | USHORT | Raw `bEndpointAddress` (direction bit included) |
| 0x04 | `TotalMaxPacketSize` | USHORT | MPS x (transactions per microframe) [endpoint.c:897-898]; during enumeration this is where the corrected EP0 MPS0 appears [device.c:1365] |
| 0x06 | `Period` | UCHAR | Pre-bucketed power-of-two period 1/2/4/8/16/32, in microframes for High Speed and in frames for Full/Low Speed; 0 for control and bulk, forced to 1 for isoch. See "Periodic scheduling" below - the unit asymmetry is binary-confirmed and is an 8x error if read as one unit |
| 0x07 | `Reserved1` | UCHAR | |
| 0x08 | `DeviceSpeed` | ULONG | `USB_DEVICE_SPEED` enum: UsbLowSpeed 0, UsbFullSpeed 1, UsbHighSpeed 2 (NT usbdi enum) |
| 0x0C | `UsbBandwidth` | ULONG | usbport's budget figure |
| 0x10 | `ScheduleOffset` | ULONG | USB2 budgeter output |
| 0x14 | `TransferType` | ULONG | Section 2 values |
| 0x18 | `Direction` | ULONG | |
| 0x1C | `BufferVA` | ULONG_PTR | Per-endpoint common buffer (from `HeaderBufferSize`) [endpoint.c:1079-1081] |
| 0x20 | `BufferPA` | ULONG | 32-bit physical address of same |
| 0x24 | `BufferLength` | ULONG | |
| 0x28 | `Reserved3` | ULONG | |
| 0x2C | `MaxTransferSize` | ULONG | usbport's default per type (EP0 0x1000, control/bulk 0x10000, int 0x400, iso 0x1000000) [endpoint.c:911-941], then overwritten with the miniport's `QueryEndpointRequirements` answer for bulk/interrupt [endpoint.c:1057-1061] |
| 0x30 | `HubAddr` | USHORT | USB address of the nearest High-Speed *ancestor* hub (the TT hub), or -1 (0xFFFF) when there is no TT; binary-confirmed, see "What the two topology fields name" below. Resolve to TT hub Slot ID via the address map |
| 0x32 | `PortNumber` | USHORT | `DeviceHandle->PortNumber` [endpoint.c:909], seeded with the immediate-parent port and then overwritten by `USBPORT_GetTt` through an OUT pointer during device creation. On a successful TT lookup it is the port on the TT hub, at any tier depth; when `GetTt` is skipped it stays the immediate-parent port. A *failed* lookup gives two further readings, not distinguishable from each other by value, and both unreachable through a successfully-initialized valid tree; see "What the two topology fields name" below, which lists all four |
| 0x34 | `InterruptScheduleMask` | UCHAR | USB2 split-schedule masks (EHCI S-mask/C-mask) |
| 0x35 | `SplitCompletionMask` | UCHAR | |
| 0x36 | `TransactionPerMicroframe` | UCHAR | 1-3, from wMaxPacketSize bits 12:11 + 1 [endpoint.c:889-895] |
| 0x37 | `Reserved4` | UCHAR | |
| 0x38 | `MaxPacketSize` | ULONG | wMaxPacketSize bits 10:0 [endpoint.c:888-896] |
| 0x3C | `Reserved6` | ULONG | |

Hub-topology note for `docs/contributing/design/02-hub-topology-route-string.md`:
the properties do carry `HubAddr`/`PortNumber`, and the binaries show they
name the TT hub and its port at any tier depth, not just one.
They carry no Route String tiers, nothing that identifies the root port
under an external hub, and nothing for the hub-descriptor-derived fields
(`TTT`, `MTT`, `Hub`, `Number of Ports`) - so DW2 is *not* filled from the
properties alone. Those parts of design doc 02 stand.

Measured in batch 6-V's VM runs, on both shipping builds: `PortNumber` is
not a TT-only field, and it names the root-hub port for a device on a root
port. The raw property dumps read `HubAddr/PortNumber = 0x0001FFFF` and
`0x0002FFFF` for devices on root ports 1 and 2. `HubAddr` is `0xFFFF`,
meaning no transaction translator, as the row above says, while `PortNumber`
is 1 and 2. Reproduced on NUSB's build and on SP4's.

The explanation is this driver's own doing. The miniport reports every
connected root port as High Speed whatever the port decoded (the intentional
untruth in `src/xhci_port.c`). usbport therefore creates a
root-attached device as High-Speed and skips `GetTt` altogether at the
`0x2644B` gate, so the port local is never overwritten and the zeroed
TtExtension local gives `HubAddr` = `0xFFFF`. The walk does not run and exit
early; it does not run at all.

Were that untruth ever removed, a truthfully non-HS root-port device would
take the `GetTt` path, hit the root hub's empty `TtList`, and bugcheck inside
`OpenPipe` before any properties are filled (the fault described in
section 8). So the truthful path can never reach a device-properties or
`OpenEndpoint` callback. Root-hub status callbacks have already happened by
then, so the claim is only about the endpoint family. SP4: the faulting
`ExfInterlockedInsertTailList` is at `0x24FE0` off the check at
`0x24FC3`-`0x24FCB`, while properties do not begin until `0x250BB` and
`OpenEndpoint` is not called until `0x254B6`. NUSB: `0x24962` off
`0x24945`-`0x2494D`, properties `0x24A3D`, callback `0x24E38`.

The chain from our own `RH_GetPortStatus` answer to usbport's `[ebp+15h] & 4`
gate runs through `usbhub20.sys`, so reading only the two usbport builds
cannot close it. Derived end to end:

- This driver sets standard hub-status bit 10 (`0x0400`) for every connected
  root port (`XhciPortShadowReport` in `src/xhci_port.c`, the bit being
  `XHCI_HUB_PORT_HIGH_SPEED` in `src/xhci.h`), and `XhciRhGetPortStatus`
  (`src/xhci_rh.c`) copies that word into the callback buffer.
- usbport hands `RH_GetPortStatus` the four-byte root-port status buffer
  directly - SP4 zero at `0x213A2`, port argument `0x213A5`, callback `0x213B9`
  at interface offset `+0xAC` (packet field `+0x98` plus SP4's `+0x14`
  wrapper); NUSB `0x20D2F` / `0x20D32` / `0x20D46` at `+0xA8` (same `+0x98`
  plus NUSB's `+0x10`). That per-build offset arithmetic is the check that
  the two are the same packet field, not two different ones.
- `usbhub20.sys` requests four status bytes and passes the low word onward
  unchanged: SP4 helper `0x17B98`-`0x17BBA` (request type `0xA3` - hub-class
  port GET_STATUS), call `0x175CC`, load `0x175DB`, push `0x175E9`, call
  `0x175F0`; NUSB `0x17C58`-`0x17C7A`, `0x1768C`, `0x1769B`, `0x176A9`,
  `0x176B0`.
- The hub driver reads bit 10 as High Speed and forwards the original status:
  SP4 extraction `0x19815`-`0x19820`, push `0x19938`, usbport wrapper
  `0x19946` dispatching at `0x11C7E`-`0x11C91`; NUSB `0x198D5`-`0x198E0`,
  `0x199F8`, `0x19A06`, `0x11D58`-`0x11D6B`.
- `USBPORT_CreateDevice` receives that word and tests the high byte's bit 2 -
  the original word's bit 10 - at SP4 `0x2644B`, skipping `GetTt` at
  `0x2644F`; the same bit selects `DeviceSpeed = 2` at `0x26563`-`0x26569`.
  NUSB: seed `0x25DB8`-`0x25DBB`, test/skip `0x25DCD`-`0x25DD1`, speed
  `0x25EE2`-`0x25EE8`.

Method note: the bit is produced by this driver and consumed by usbport, but
it is relayed by a third binary, and a two-binary scope cannot show a
three-binary path. "Read both builds" is the wrong completeness test for a
claim about a value crossing three modules.

The premise that "usbport
cannot say which root port a device is on" (in `src/xhci_slot.c`'s file
header, roadmap Phase 6 batch B, and design doc 02) is wrong for this field,
though the derivation built on it (`xhciDevEnumeratingPort`: the
reset-completion hint, checked against the port shadow, refusing on
ambiguity) is correct and was proven on both targets. Whether that derivation
should be replaced by, or merely corroborated with, `PortNumber` is a design
question left open: a hint that agrees with an independent check is worth
more than either alone, and nothing measured yet says which should be
authoritative when they disagree.

#### What the two topology fields name

Static, both builds. Extracts in
`tools/{win2ksp4,nusb}-extracted/usbport-abort-disasm.txt`, part 2.

- The fill is `USBPORT_OpenPipe`, SP4 `0x250BB`-`0x250EF` / NUSB
  `0x24A41`-`0x24A71`, ReactOS's shape:
  `HubAddr = Endpoint->TtExtension ? TtExtension->DeviceAddress : 0xFFFF`,
  `PortNumber = DeviceHandle->PortNumber`.
- `HubAddr` is the nearest *High-Speed ancestor*, not the parent.
  `USBPORT_GetTt` (SP4 `0x2783A`, NUSB `0x271B2`) walks up the
  `DeviceHandle->HubDeviceHandle` chain (`DeviceHandle+0x10`) until it finds a
  handle whose `DeviceSpeed` (`+0x38`) is High-Speed, and picks a TT out of
  that hub's `TtList` (`+0x68`, count at `+0x64`). For an all-High-Speed path
  there is no TT extension and `HubAddr` reads `0xFFFF` - the same value a
  root-port device reads, so `0xFFFF` does not mean "on a root port".
- `PortNumber` is the port on the TT hub on a successful TT lookup, at any
  depth. The easy misreading is to take `GetTt`'s `OutPort` for a local of
  `GetTt`; it is a pointer to a local of `CreateDevice`, the same local that
  becomes `DeviceHandle->PortNumber`. The chain, instruction for instruction:
  1. `USBPORT_CreateDevice` seeds a stack local with the `Port` argument
     usbhub supplied - SP4 `0x26436: mov eax,[ebp+18h]` /
     `0x26439: mov [ebp-0Ch],eax`; NUSB `0x25DB8`/`0x25DBB`.
  2. It passes the address of that local as `GetTt`'s third argument - SP4
     `0x26451: lea eax,[ebp-0Ch]` / `0x26454: push eax` / `0x26459: call
     0x2783A`; NUSB `0x25DD3`/`0x25DD6`/`0x25DDB`. The second argument
     (`[ebp+10h]`) is the parent hub's handle - the same value stored to
     `DeviceHandle+0x10` at SP4 `0x26552` / NUSB `0x25ED1`.
  3. `GetTt`'s walk writes through that pointer on every non-HS iteration -
     SP4 `0x2784B: mov ecx,[ebp+10h]` / `0x2784E: mov ax,[esi+6]` /
     `0x27852: mov [ecx],ax`, then `0x27855: mov esi,[esi+10h]` and back to the
     speed test at `0x27845`; NUSB `0x271C3`/`0x271C6`/`0x271CA`/`0x271CD`.
     Each pass overwrites it with *that* handle's own port, so the value left
     standing when the loop stops at the first High-Speed ancestor is the port
     on the TT hub that the non-HS subtree hangs off.
  4. `CreateDevice` then stores the mutated local into `DeviceHandle+0x06`
     - SP4 `0x2657F: mov ax,[ebp-0Ch]` / `0x26583: mov [ebx+6],ax`; NUSB
     `0x25EFE`/`0x25F02` - immediately after the `'DevH'` signature at SP4
     `0x26579`, with the `TtExtension` from the same call landing at `+0x0C`.
  5. `USBPORT_OpenPipe` copies it to `properties.PortNumber` (`+0x2E` of the
     properties, `Endpoint+0x12E`) - SP4 `0x250EB: mov ax,[eax+6]` /
     `0x250EF`; NUSB `0x24A65`/`0x24A71`.
- The exact rule, case by case. `GetTt` is called only if the
  controller's USB2 flag is set (SP4 `0x26445: test byte ptr [eax+18h],10h`,
  NUSB `0x25DC7` at `[eax+14h]`) and the new device is not High-Speed (SP4
  `0x2644B: test byte ptr [ebp+15h],4` / `0x2644F: jne`, NUSB
  `0x25DCD`/`0x25DD1` - the same bit that sets `DeviceSpeed = 2` at SP4
  `0x26569`). So:
  1. `GetTt` skipped (High-Speed device, or non-USB2 controller) - the
     local keeps the immediate-parent port, and the `TtExtension` local zeroed
     at function entry (SP4 `0x262F1: xor esi,esi` -> `0x26300: mov
     [ebp-10h],esi`) survives, so `HubAddr` reads `0xFFFF`.
  2. `GetTt` runs and selects a TT - the walk has overwritten the local
     once per non-HS ancestor, so `PortNumber` is the port on the TT hub, at
     any depth. If the parent hub is itself the HS TT hub the loop exits on the
     first `cmp [esi+38h],2` (SP4 `0x27845`, NUSB `0x271BD`) with no write, and
     the immediate-parent port *is* the TT port, so the two agree.
  3. `GetTt` runs and returns no TT, by either of two routes. `HubAddr`
     reads `0xFFFF` in both.
     - Walk off the top of the parent chain (SP4 `0x2785A: je 0x2789D`,
       NUSB `0x271D2`), returning `ebx = 0`, zeroed at SP4 `0x27843` / NUSB
       `0x271BB`. `PortNumber` holds the port of the last non-HS ancestor
       visited.
     - Multi-TT list miss (SP4 `0x2786C`-`0x27890`, NUSB
       `0x271E4`-`0x27208`): a High-Speed ancestor *is* found but no list entry
       matches. If the immediate parent is itself that HS hub, the loop exits
       on the first `cmp [esi+38h],2` (SP4 `0x27845` -> `0x2785E`, NUSB
       `0x271BD` -> `0x271D6`) with no OUT write at all, leaving the
       *seeded immediate-parent* port. Deeper, it leaves the last non-HS
       ancestor's port - the same category as walk-off.

     So the two routes do not have distinguishable `PortNumber` values.
     Both reduce to "the last non-HS ancestor visited, or the seed if there
     was none", and with a single non-HS hop that value is the
     immediate-parent port. Do not use `PortNumber` to tell the failures
     apart; it cannot.
  4. Malformed/no-TT, outside both NULL returns: `TtCount <= 1` over an
     empty list `CONTAINING_RECORD`s to `0xFFFFFFEC` (SP4 `0x27861` /
     `0x27892`-`0x2789A`, NUSB `0x271D9` / `0x2720A`-`0x27212`). That is the
     fault section 8 describes, and it never reaches a properties-bearing callback
     because `OpenPipe` faults on it first.

  Is case 3 reachable? The walk-off-top route is not, because the walk meets
  the High-Speed-marked root-hub handle (SP4 `0x2E1C1`-`0x2E1D9`, NUSB
  `0x2D8E7`-`0x2D8FF`).

  The list-miss route needs the call graph, and it closes too: `USBPORT_Initialize20Hub` (SP4 `0x26246`-`0x262E3`, NUSB
  `0x25BC8`-`0x25C65`) creates TT records numbered `1..TtCount` (SP4
  `0x262C0`-`0x262D7`, NUSB `0x25C42`-`0x25C59`) through the sole TT-creator
  call (SP4 `0x262C8` -> `0x278FE`, NUSB `0x25C4A` -> `0x27276`), a partial
  allocation returns failure (SP4 `0x262CD`-`0x262D2`), and `usbhub20`
  aborts hub start on that failure (SP4 `0x1724B`-`0x17254`, NUSB
  `0x1730B`-`0x17314`) having supplied the count as `1` or the descriptor's
  `bNbrPorts` (SP4 `0x11C98`-`0x11CCF`, NUSB `0x11D72`-`0x11DA9`). Restore
  moves the whole list and copies the count (SP4 `0x27342`-`0x27382`).

  So a successfully initialized hub has exactly one TT record per usable
  port (one shared record when single-TT, or `1..bNbrPorts` when multi-TT),
  and a partial list exists only behind an initialization failure that
  aborts the hub. Both failure branches exist defensively, and neither is
  reachable through the successfully-initialized valid-tree call graph. Case
  3 is still not something to rely on being absent, but it is not "unproven"
  either. A negative claim needs the call graph, not one function's extent.

  There is exactly one `GetTt` call site (SP4 `0x26459`, NUSB `0x25DDB`), so no
  alternate caller contract narrows any of this.
- What is still not provided: the *root* port under an external hub. For a
  device behind a hub, nothing in the properties says which root port that hub
  hangs off, and `PortNumber` now demonstrably does not - it names a port on
  the TT hub. xHCI's Root Hub Port Number still needs the snooping graph.
- usbport has the whole parent chain and never exposes it. The chain is
  `DeviceHandle+0x10` with each handle's own `PortNumber` at `+0x06`; no
  miniport callback in the packet is passed a `PUSBPORT_DEVICE_HANDLE`, and
  the 0x40 bytes of `USBPORT_ENDPOINT_PROPERTIES` are fully accounted for
  above. So the four values the miniport gets are `DeviceAddress`,
  `DeviceSpeed`, `HubAddr` and `PortNumber` - no route tiers, no parent
  address on an all-HS path, no "this device is a hub".
- What *is* observable is the snooping channel design doc 02 section 2
  assumes. `USBPORT_OpenPipe` installs one of two endpoint workers as a
  function pointer at `Endpoint+0x3C`: root-hub EP0
  (`ENDPOINT_FLAG_ROOTHUB_EP0`, `Endpoint->Flags` bit 1) gets the root-hub
  worker and never reaches `OpenEndpoint` at all (SP4 `0x25341`/`0x25359`,
  NUSB `0x24CC3`/`0x24CDB`), while every successfully opened non-root
  endpoint gets `OpenEndpoint` and `USBPORT_DmaEndpointWorker` (SP4
  `0x254B6`/`0x254DF`, NUSB `0x24E38`/`0x24E61`). So hub-class requests to an
  external hub's EP0 do arrive at `SubmitTransfer` with their raw SETUP
  bytes at `TransferParameters+0x14`, and the root hub's own class requests
  never do.
  - Two qualifications, neither of which changes the conclusion. The non-root
    path can fail before the callback (SP4 `0x25439`-`0x2544F`, NUSB
    `0x24DBB`-`0x24DD1`), hence "successfully opened" above.
  - The `OpenEndpoint` slot has three call sites per image, not the one cited
    here: SP4 `0x24B7A`, `0x254B6`, `0x2755D`; NUSB `0x244FC`, `0x24E38`,
    `0x26ED5`. The two extra sites are the child/default-pipe reopen and the
    ordinary-device restore paths (reached from SP4 `0x26C55`/`0x2D809`, NUSB
    `0x265CD`/`0x2CF55`); neither constructs the root-hub endpoint, so the
    root-EP0 diversion holds across all three.

Consequence for design doc 02, stated here because it is an ABI fact, and
stated narrowly: what the properties settle is which TT a device is behind,
at every depth, when the lookup succeeded. `HubAddr` names the TT hub and
`PortNumber` names the port on it.

The qualifier is the condition to test, not a caveat to skim: `HubAddr !=
0xFFFF` means "a TT record was selected", and it is the only reading under
which the pair names a transaction translator. When `HubAddr` is `0xFFFF` the
port field still holds something (the last visited non-HS ancestor's port, or
the seed; see the cases above), and on a multi-TT miss that number is
genuinely the found HS hub's downstream port. What is missing is not the
port's meaning but the identity of the hub it belongs to. The pair is
unusable; the number is not nonsense, only unattributed.

It does not make the FS/LS-behind-HS case work from usbport data alone, and
this paragraph must not be read that way:

- `HubAddr` is a USB address, while Slot Context DW2 `7:0` wants the TT hub's
  Slot ID - the miniport still resolves it through its own
  usbport-address -> Slot ID map, and the TT hub must already hold a slot.
- DW2 `17:16` (TTT) is not provided at all. It comes from the hub
  descriptor's `wHubCharacteristics` bits 6:5
  (`docs/usb-xhci-info/xhci-data-structures.md` section 8), which only snooping supplies.
- The hub's own Slot Context still needs `Hub` = 1 (DW0 `26`),
  `Number of Ports` (DW1 `31:24`) and `MTT` (DW0 `25`) before splits are
  scheduled correctly, and usbport carries none of them.
- Route String (DW0 `19:0`) and Root Hub Port Number (DW1 `23:16`) for a
  behind-hub device remain unprovided, as before.

So under the "prefer usbport-provided data" rule the properties are the
primary source for the TT identity at any depth, and the snooping graph is
still required for everything else, including the rest of DW2.

### Periodic scheduling: what `Period` actually carries

Static, from both `usbport.sys` builds and both shipping `usbehci.sys`
builds.

The miniport never sees `bInterval`. usbport decodes the endpoint
descriptor itself and delivers a single pre-bucketed `Period`, so the per-speed
`bInterval` conversion of `docs/usb-xhci-info/xhci-data-structures.md` section 8
is already applied by the time `OpenEndpoint` is called. Do not read that table
as this driver's job.

The producer is `USBPORT_OpenPipe`, SP4 `0x251D3`-`0x2524F` / NUSB
`0x24B55`-`0x24BD1`. The two builds are byte-identical here except for branch
target addresses. The endpoint properties block is at `Endpoint+0xFC`, so
`Period` is `Endpoint+0x102`. With `bInterval` at `[edx+0x0A]` (the endpoint
descriptor's own byte, `bmAttributes` at `+7` and `wMaxPacketSize` at `+8`
confirming the alignment):

| Transfer type | `Period` delivered |
|---|---|
| Control (1), Bulk (2) | 0 - the bucketing is gated on `TransferType == INTERRUPT` (SP4 `0x251DA`) and the field was cleared just above it |
| Isochronous (0) | 1, unconditionally (SP4 `0x25248`). It carries no interval information, so an isoch interval must come from elsewhere. The per-transfer iso parameter block (section 4) does not supply it either: it carries a frame and microframe per packet rather than an interval. The miniport gets the ESIT interval through a different channel: it snoops the device's own `GET_DESCRIPTOR(Configuration)` reply on EP0 and reads `bInterval` out of the endpoint descriptor (`src/xhci_desc.h`). usbport still hands over no interval, but the Endpoint Context no longer has to assume one |
| Interrupt (3) | the bucketed value below |

For an interrupt endpoint, with `DeviceSpeed` read from `DeviceHandle+0x38`
into properties `+0x08`:

- High Speed: `raw = 1 << min(bInterval - 1, 5)` (SP4 `0x251EC`-`0x251FD`).
- Full/Low Speed: `raw = bInterval` unchanged.
- Low Speed only: a `raw` below 8 is raised to 8 (SP4 `0x25216`).
- Then `raw` is rounded down to a power of two by shifting 32 rightwards
  until a bit matches (SP4 `0x25223`-`0x25231`), and anything `>= 32` stays 32.

So `Period` is a power of two in 1..32 - but its unit is not the same for all
speeds. For High Speed it counts microframes; for Full and Low Speed it
counts frames. That asymmetry is the whole trap: a driver that treats it as
one unit is wrong by 8x for one speed class or the other, in the direction that
silently over- or under-polls rather than failing.

The consumer proves the High-Speed half outright. `usbehci.sys` decodes
`Period` at SP4 `0x1134B` / `0x117E7` by counting trailing zero bits - i.e.
`log2(Period)` - and uses it to index a periodic-tree base table
`{0, 1, 3, 7, 15, 31, 31, 31}`, the classic balanced tree. On the High-Speed
path (SP4 `0x1138C`, reached when `DeviceSpeed == 2`) that base plus
`ScheduleOffset` indexes a 63-entry, 3-byte-stride schedule table at VA
`0x14220`, whose entries are `(period, frame-selector, S-mask)`. Decoding all 63:

| `Period` | entries | frame selectors | bits set in S-mask | actual service interval |
|---|---|---|---|---|
| 1 | 1 | `00` | 8 | every microframe - 125 us |
| 2 | 2 | `00` | 4 | 250 us |
| 4 | 4 | `00` | 2 | 500 us |
| 8 | 8 | `00` | 1 | 1 ms - once per frame |
| 16 | 16 | `01`,`02` | 1 | 2 ms |
| 32 | 32 | `03`,`04`,`05`,`06` | 1 | 4 ms |

The slot count at each level equals the `Period` value, and the S-mask bit
count halves as `Period` doubles until it reaches one bit at `Period` = 8. A
frame holds 8 microframes, so a table in which `Period` = 8 is "once per
frame" is a table whose unit is the microframe. That is a structural proof,
not an inference.

The Full/Low-Speed path (SP4 `0x1137F`) takes no entry in that table at all - it
zeroes the pointer - because a device behind a TT is scheduled by split
transactions whose masks come from usbport's own USB2 budgeter rather than from
this geometry. `InterruptScheduleMask`/`SplitCompletionMask` (`+0x34`/`+0x35`)
carry those, and usbehci writes them into the EHCI QH S-mask/C-mask fields at
SP4 `0x103B1`-`0x103C3`.

Two details that are easy to misread. usbehci does not select different base
tables by speed: the image table at `0x142E0` and the stack-built one hold
identical bytes (`00 01 03 07 0F 1F 1F 1F`, verified in both builds). And the
branch reads backwards at first sight: `setne` on `DeviceSpeed == 2` means
the `je` at SP4 `0x1137D` is taken for High Speed, so HS is the path using
`ScheduleOffset` and the `0x14220` structure while non-HS uses the image
copy.

`Period` is never 0 for an endpoint that is periodically scheduled, and that
is provable from the consumer rather than assumed. usbehci's decode is
`test al,1 / inc / shr al,1 / jmp` with no iteration bound: on a zero `Period`
it spins forever. Two sites in each build have that shape - SP4
`0x1135C`-`0x11363` and `0x117EC`-`0x117F3`. (The load at `0x11CAE` reads
`Period` into a 4-bit field and is not a decoder.) A shipping miniport that
would hang is the strongest
available evidence that the producer cannot deliver 0 on the paths that reach it
- and the producer table above agrees, since only control and bulk get 0 and
neither is periodically scheduled.

What a malformed descriptor produces, recorded because it is the one input
the bucketing does not normalise into the contract: a `bInterval` of 0 gives
`Period` = 1 on High Speed (the decrement at SP4 `0x251EC` is skipped) and
leaves the preloaded `Period` = 32 on Full/Low Speed. Both are inside the
power-of-two contract, so neither is distinguishable downstream from a device
that asked for them - the miniport cannot detect this case and should not try.

What this means for the xHCI Endpoint Context `Interval` field (period =
`2^Interval` x 125 us, so the field counts microframes):

```
Interval = log2(Period) + (DeviceSpeed == UsbHighSpeed ? 0 : 3)
```

That lands on the values `docs/usb-xhci-info/xhci-data-structures.md` section 8's
conversion table prescribes, because usbport's bucketing *is* that conversion:
HS gives `log2(Period) = min(bInterval - 1, 5)`, which is the table's
`bInterval - 1` under usbport's own clamp, and FS/LS gives
`log2(Period) = floor(log2(bInterval))`, which is the table's
`floor(log2(bInterval)) + 3` once the `+ 3` above is applied.

The reachable ranges are therefore `Interval` 0-5 High Speed, 3-8 Full Speed,
and 6-8 Low Speed. Low Speed is not 3-8, because usbport floors its `Period`
at 8 before the miniport ever sees it. That is a far smaller space than "every
LS/FS/HS bucket" suggests, and the clamps are usbport's, so a device asking
for a 125 us HS period gets one, while one asking for faster than 32
microframes is already slowed down before the miniport sees it.

The two clamps are usbport's policy, not the hardware's, so they must not be
re-applied or "corrected" here: xHCI's `Interval` field is 8 bits and would
accept far more. The miniport's job is to translate what it is given, and to
refuse - not silently repair - a `Period` outside the derived contract, because
a value this section did not predict means the contract was misread.

The speed the conversion uses is usbport's, not the port's. usbport bucketed
`Period` under its own belief about the device's speed, and the unit follows
from that belief, so this driver converts with the speed usbport holds. The
speed override makes that belief High Speed for every connected root port
(section 8, "The transaction-translator lookup"). A Full- or Low-Speed
interrupt endpoint on a root port is therefore scheduled on the *microframe*
reading: more often than its `bInterval` asked for, never less, which costs
periodic bandwidth and works. The Slot Context's Speed field still carries the
port's real decode, which is what tells the xHC how to talk to the device.

Two counters make the gap readable. `EndpointSpeedMismatches` and
`EndpointIntervalsFloored` are expected nonzero with any Full- or Low-Speed
device attached (measured `EndpointSpeedMismatches` 1 in batch 7b-A), and both
are 0 on an all-High-Speed bus. QEMU's HID devices enumerate at 480 Mb/s, so a
QEMU-only run reads 0, correctly. A zero with an FS/LS device present means
the untruth has been removed, not that nothing happened.

### USBPORT_ENDPOINT_REQUIREMENTS [usbmport.h:126-129]

`{ ULONG HeaderBufferSize; ULONG MaxTransferSize; }` - 8 bytes. EHCI returns
per-type constants [usbehci.c:620-645ff]; xHCI: one transfer-ring segment
(64 KB-boundary-safe) + chosen cap.

### USBPORT_TRANSFER_PARAMETERS [usbmport.h:95-104]

`C_ASSERT` size = 28 (0x1C).

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0x00 | `TransferFlags` | ULONG | URB-style: test `USBD_TRANSFER_DIRECTION_IN` (0x01) for direction [usbehci.c:2055] |
| 0x04 | `TransferBufferLength` | ULONG | Requested bytes |
| 0x08 | `TransferCounter` | ULONG | usbport bookkeeping id |
| 0x0C | `IsTransferSplited` | BOOL | TT split bookkeeping (usbport splits FS/LS transfers) |
| 0x10 | `Reserved2` | ULONG | |
| 0x14 | `SetupPacket` | 8 bytes | `USB_DEFAULT_PIPE_SETUP_PACKET` - the raw SETUP bytes for control transfers; this is where SET_ADDRESS (bmRequestType 0x00, bRequest 0x05) is intercepted, and where hub-class requests are snooped for design doc 02 |

This pointer is the transfer's identity at completion (section 4, Transfers).
Binary-confirmed: `SubmitTransfer` is passed
`&Transfer->TransferParameters` at `Transfer+0x4C` in both builds, and
`UsbPortCompleteTransfer` recovers the record with `lea ebx,[arg3-4Ch]` - so the
28 bytes above are embedded in usbport's record, not copied, and the offsets in
this table are the *live* fields usbport reads back.

### USBPORT_SCATTER_GATHER_LIST / _ELEMENT [usbmport.h:106-124]

Element (`C_ASSERT` 24 bytes): `PHYSICAL_ADDRESS SgPhysicalAddress` (0x00,
8 bytes), `Reserved1` (0x08), `SgTransferLength` (0x0C), `SgOffset` (0x10),
`Reserved2` (0x14).

List header (`C_ASSERT` 64 bytes with 2 elements): `Flags` (0x00),
`CurrentVa` (0x04), `MappedSystemVa` (0x08), `SgElementCount` (0x0C),
`SgElement[]` (0x10, variable length - the list is the last member of
usbport's transfer record [usbport/usbport.h:270-271]).

`SgElementCount == 0` is a legal, common input, and the pointer is never
NULL (binary-confirmed; see "The endpoint and transfer paths in the shipping
builds" in section 4). The
list is passed as an interior pointer of the transfer record, so it always
exists; but a transfer whose `TransferBufferLength` is 0 is never mapped, so the
header is still the zeroed one `USBPORT_AllocateTransfer` produced. Read the
count, never assume element 0 exists, and let `TransferBufferLength` decide
whether there is a data stage at all.

How usbport builds it [usbport/usbport.c:2341-2438]: through the NT DMA
adapter (`DmaOperations->MapTransfer` with map registers) and splitting
elements at 4 KB page boundaries
(`ElementLength = PAGE_SIZE - (PhAddress & (PAGE_SIZE-1))`). Consequences for
the miniport:

- Elements are bus addresses (possibly map-register/bounce addresses on some
  platforms) - program them into TRBs as-is; never re-derive from VAs.
- A page-granular element can never cross a 64 KB boundary, so per-element TRB
  encoding is inherently 64 KB-safe. Keep the 64 KB split logic anyway
  (`docs/usb-xhci-info/xhci-data-structures.md` "Transfer TRBs") - it is cheap and protects
  against a binary that produces larger elements.
- `SgOffset` is the element's byte offset within the whole transfer buffer -
  use it to order TRBs; do not assume list order == buffer order without
  checking it in the spike log.

The list builder was read out of both shipping builds (static): NUSB
`5.00.2195.5652` (`USBPORT_MapTransfer` @ VA `0001856A`) and SP4
`5.00.2195.6681` (@ VA `00018980`). Both are 540 instructions that differ in
three operands, all private
structure offsets: the FDO extension's DMA adapter pointer (`+0x510` vs
`+0x518`), an FDO list head (`+0x1C8` vs `+0x1CC`), and the SG list's offset
inside usbport's private transfer record (`Transfer+0x98` vs `Transfer+0xC0`).
The split-transfer SG builder (307 instructions) differs only in that same
transfer-record offset. Extracts in
`tools/{nusb,win2ksp4}-extracted/usbport-sglist-disasm.txt`.

- It really is the NT DMA adapter, and there is no second producer.
  `USBPORT_MapTransfer` is the `AllocateAdapterChannel` execution routine
  (`ret 10h`; `DeviceObject, Irp, MapRegisterBase, Context`). It loads
  `Adapter = FdoExt->DmaAdapter`, `ops = Adapter->DmaOperations` (adapter
  `+0x04`), and calls `[ops+0x20]` = `MapTransfer` with the full six-argument
  NT signature (`DmaAdapter, Mdl, MapRegisterBase, CurrentVa, &Length,
  WriteToDevice`). Scanning each image for 24-byte-strided element accesses
  (`lea eax,[eax+eax*2]` then `*8`) finds five sites: this one writer, the
  split builder below, and three readers. No `MmGetPhysicalAddress` path and
  no second adapter call - `DMA_OPERATIONS+0x20` is reached from exactly one
  place in the whole image.
- The 4 KB split is in the binary, not just in ReactOS:
  `mov ebx,1000h; and eax,0FFFh; sub ebx,eax`, then clamped to the remaining
  mapped length. Worst-case element count for an N-byte transfer is therefore
  `ceil(N / 4096) + 1`. (Answers section 9 open item 8.)
- The layout is confirmed from the code, independently of the headers:
  header 0x10 bytes (`Flags` 0x00, `CurrentVa` 0x04, `MappedSystemVa` 0x08,
  `SgElementCount` 0x0C), elements 24 bytes starting at 0x10 (indexed
  `lea eax,[eax+eax*2]` then `*8`), and a separate element-lookup helper walks
  the array with `add edx,18h`.
- The high DWORD is zero, but usbport does not mask it (ReactOS reads as if
  it forced `HighPart = 0`; the binary does not). `MapTransfer`
  returns a `PHYSICAL_ADDRESS` in `edx:eax` and the binary
  stores `edx` into `SgElement[i].SgPhysicalAddress.HighPart` verbatim. It is
  expected to be zero because the adapter is created
  `Dma32BitAddresses = 1` / `DmaWidth = Width32Bits` with
  `Dma64BitAddresses` never written (section 5), so the HAL cannot return a
  PA above 4 GB.
  - The paths after `MapTransfer` rely on that adapter contract rather than
    adding enforcement: within one mapped chunk usbport advances only the
    LowPart (`add [ebp-40h],ebx`) and never carries into the HighPart, so a
    chunk straddling the 4 GB line would corrupt usbport's own addresses; and
    the split builder overwrites only the child element's LowPart after the
    child allocation has been zeroed and then copied from the parent, so its
    unwritten HighPart is inherited from that parent record, not from
    zero-initialization.
  - Rule for the miniport: read the low DWORD, and check the high DWORD is
    zero rather than assume it. The check is free and the zero is inherited
    from the adapter contract, not enforced by usbport's element writers.
- `Reserved1` (element +0x08) is usbport-private and address-like.
  `MapTransfer` never writes it, but the split builder propagates it
  (`child = parent->Reserved1 + splitOffset`) just as it does the LowPart.
  The miniport must not read or write it.
- `SgList->Flags` bit 0 is set when `MapTransfer` returns the same base
  physical address for two successive map rounds - i.e. a map-register/bounce
  mapping. Informational only; the miniport programs the elements either way.

### USBPORT_ROOT_HUB_DATA [usbmport.h:695-703]

`C_ASSERT` 16 bytes: `NumberOfPorts` (ULONG, 0x00),
`HubCharacteristics` (USHORT union, 0x04 - USB2 form has PowerControlMode,
OverCurrent bits, TtThinkTime, PortIndicators [usbmport.h:668-680]),
`Padded1` (0x06), `PowerOnToPowerGood` (ULONG, 0x08 - units are 2 ms and
usbport copies the value directly into `bPowerOnToPowerGood`
[roothub.c:883], so use 10 for xHCI's 20 ms PORTSC.PP rule),
`HubControlCurrent` (ULONG, 0x0C).

## 6. usbport service functions (what the miniport may call)

First argument is always the miniport extension (not a device object).
Written into the packet at registration (section 1).

| Service | Signature [usbmport.h line] | Contract |
|---|---|---|
| `UsbPortDbgPrint` | `ULONG (ext, ULONG level, PCH fmt, ...)` [398-403] | cdecl varargs - the one non-stdcall entry |
| `UsbPortTestDebugBreak` | `ULONG (ext)` [405-406] | |
| `UsbPortAssertFailure` | `ULONG (ext, PVOID, PVOID, ULONG, PCHAR)` [408-414] | |
| `UsbPortGetMiniportRegistryKeyValue` | `MPSTATUS (ext, BOOL, PCWSTR, SIZE_T, PVOID, SIZE_T)` [416-423] | Read miniport registry parameters. Binary-confirmed in both shipping builds, argument by argument; see the `UsbPortGetMiniportRegistryKeyValue` subsection below |
| `UsbPortInvalidateRootHub` | `ULONG (ext)` [425-426] | Tell usbport port status changed; it will re-poll via `RH_GetPortStatus` [roothub.c:916-956]. Call from the DPC on Port Status Change events (EHCI does [usbehci.c:1521, 3422-3433]). It re-enters the miniport before it does anything else: `RH_DisableIrq(MiniPortExt)` is its first act [roothub.c:941], so a miniport that holds its own lock across this call deadlocks on it if that callback takes the same lock - which is a hang rather than a lock-order warning, and is the reason this driver's announcement is decided under the lock and issued after releasing it. What follows is `USBPORT_InvalidateEndpointHandler` on the root hub's EP0, whose `ENDPOINT_FLAG_ROOTHUB_EP0` forces `INVALIDATE_ENDPOINT_WORKER_THREAD` [endpoint.c:1382-1425], i.e. `EndpointListSpinLock` plus a `KeSetEvent` - not `MiniportSpinLock`, so calling it from a callback that runs under that lock is safe on the mirror's evidence. Mirror-derived: the re-entry and the worker-thread path have not been re-read out of the shipping binaries, unlike the section 4 root-hub block |
| `UsbPortInvalidateEndpoint` | `ULONG (ext, epExt-or-NULL)` [428-431] | Kick usbport's endpoint worker; NULL = all endpoints (EHCI DPC passes NULL [usbehci.c:1515]) |
| `UsbPortCompleteTransfer` | `VOID (ext, epExt, transferParams, USBD_STATUS, ULONG bytes)` [433-439] | Complete a non-iso transfer (section 4) |
| `UsbPortCompleteIsoTransfer` | `ULONG (ext, epExt, transferParams, PVOID isoParams)` [441-446] | Packet slot 0x100, `ret 10h`. The declaration is confirmed; the fourth argument is the same iso-parameters block `SubmitIsoTransfer` was given, and `epExt` is not read at all. Layout and completion contract: section 4, "Isochronous transfers" |
| `UsbPortLogEntry` | `ULONG (ext, ULONG, ULONG, ULONG, ULONG, ULONG)` [448-455] | Event log ring |
| `UsbPortGetMappedVirtualAddress` | `PVOID (ULONG pa, PVOID ext, PVOID epExt)` [457-461] | PA is the first argument (unique among services). Translates a physical address inside an endpoint's common buffer back to its VA (EHCI resolves hardware TD pointers this way [usbehci.c:2525, 3067]) |
| `UsbPortRequestAsyncCallback` | `ULONG (ext, ULONG ms, PVOID ctx, SIZE_T ctxLen, ASYNC_TIMER_CALLBACK *)` [468-474] | One-shot timer callback; callback receives `(MiniportExtension, CallBackContext)` [463-466]. ReactOS invokes it directly from `USBPORT_AsyncTimerDpc` without `MiniportSpinLock` or `MiniportInterruptsSpinLock`, and exposes no cancellation service [usbport.c:2089-2164]. Use a copied generation/context plus miniport-owned synchronization; stale or post-stop callbacks must do nothing. This is the sanctioned deferred-work tool (no private DPCs/threads) for command/reset watchdogs |
| `UsbPortReadWriteConfigSpace` | `MPSTATUS (ext, BOOLEAN IsRead, PVOID buf, ULONG offset, ULONG len)` [476-482] | PCI config access; TRUE = read (EHCI reads FLADJ with TRUE [usbehci.c:1195-1200]). Use for VID/DID quirk lookup and Intel `XUSB2PR` switchover |
| `UsbPortWait` | `NTSTATUS (ext, ULONG ms)` [484-487] | Implemented as `KeDelayExecutionThread` [usbport.c:545-551], so PASSIVE_LEVEL only: safe inside `StartController`, never inside DISPATCH-level callbacks (Open/Submit/DPC...). `ResetController` is one of those: usbport calls it from a DPC holding a spin lock, so it is not a PASSIVE context and nothing that waits may be reached from it |
| `UsbPortInvalidateController` | `ULONG (ext, ULONG type)` [493-496] | Types in section 2; RESET requests recovery (EHCI uses it on fatal error [usbehci.c:1426, 2925]) |
| `UsbPortBugCheck` | `VOID (ext)` [498-499] | Last resort, and verified in both shipping builds rather than taken from the mirror: SP4 VA `0x11C2E` and NUSB VA `0x11B72` are the same five instructions - `KeBugCheckEx(0xD2, 0, 0, 0, 0)` then `ret 4` - reached through IAT index 18 (thunk `0x2CA80`) and 17 (thunk `0x2C29C`) of each image's `NTOSKRNL.EXE` import table. Extracts in `tools/{nusb,win2ksp4}-extracted/usbport-bugcheck-disasm.txt`. Two consequences for callers: all four bugcheck parameters are hard zeros, so whatever diagnosis is wanted must be in the extension and the trace before the call; and the extension argument is pushed and never read. This is the fail-closed path for a DMA teardown that can prove nothing (`docs/contributing/implementation-invariants.md`, "DMA Teardown"), and it costs the miniport no import of its own |
| `UsbPortNotifyDoubleBuffer` | `ULONG (ext, transferParams, PVOID, SIZE_T)` [501-506] | EHCI double-buffer bookkeeping; expected unused by xHCI |

### There is no `Set` counterpart, and that is a design constraint (task 13-L.0)

Static. The table above is the whole service list: 16 entries, packet
offsets `0xE4`-`0x120`, and section 1 confirms usbport writes those 16 and
touches no other packet field. One of them is a registry entry and it is a
read. There is no `UsbPortSetMiniportRegistryKeyValue`
at any packet offset, in any of the three shipping builds.

So the asymmetry is real and permanent: a miniport reads its own software key
with no import of its own, and a miniport that wants to *write* one has to
carry an import that clears the gate's Win98 half on its own evidence. That is
not a gap to be worked around by hunting for a hidden slot - the 16 are
enumerated and the list is closed.

Batch 13-L considered a registry write as a way to give a Windows 98 user
something to attach to a bug report, and the first question was whether
usbport already offered one. It does not, so a write would have to be an
import of the miniport's own, which is part of why that candidate was not
taken. `docs/contributing/design/08-build-flavours-and-the-log-channel.md`
section 10 records the rest, including why `RtlWriteRegistryValue` rather
than `ZwSetValueKey` would have been the import (the handle problem, and
Win98 SE's own `usbhub.sys` calling it at VA `0x102D6`). The read/no-write
asymmetry is a fact about the ABI either way, so it belongs here.

### `UsbPortGetMiniportRegistryKeyValue` - read out of both binaries (task 11-V.7)

Static, from both shipping `usbport.sys` builds *and* both
shipping `usbehci.sys` builds. Extracts:
`tools/{nusb,win2ksp4}-extracted/usbport-registrykey-disasm.txt` and
`usbehci-registrykey-disasm.txt`, each carrying the addresses to search for so
a fresh clone can redo it with `link -dump -disasm` and `-imports`.

The mirror's transcription (`MPSTATUS (ext, BOOL, PCWSTR, SIZE_T, PVOID,
SIZE_T)`) holds argument for argument. What was unknown - what the `BOOL`
selects, whether the `PCWSTR` is a value name, and whether the slot is
populated in NUSB as well as SP4 - is answered below, along with three
further constraints on how it may be used.

The slot is populated in both builds, unconditionally, in the same
registration block that writes the other 15 services: SP4 `00027E37` writes
`0002D88E`, NUSB `000277AD` writes `0002CFDA`. Both entries are a 15-instruction
thunk ending `ret 18h` - six stack arguments, confirming the arity.

The thunk's whole job is to prepend two pointers taken from usbport's own
extension by negative offset from the miniport extension (SP4 `[ext-0x844]`
and `[ext-0x774]`; NUSB `[ext-0x7DC]` and `[ext-0x70C]` - private offsets
differ per build as usual, the shape does not). The second of the two is the
PDO, which is what `IoOpenDeviceRegistryKey` requires; the first is passed
and never read.

| Miniport argument | What the binary does with it |
|---|---|
| 1 `ext` | Consumed by the thunk to reach usbport's extension; never passed on |
| 2 `BOOL` | `cmp byte ptr [..],0` selecting `IoOpenDeviceRegistryKey`'s `DevInstKeyType`: FALSE -> 1 (`PLUGPLAY_REGKEY_DEVICE`, the hardware key), TRUE -> 2 (`PLUGPLAY_REGKEY_DRIVER`, the software/driver key). `DesiredAccess` is the literal `0x1F0000` in both builds |
| 3 `PCWSTR` | Yes, it is the value name. `RtlInitUnicodeString(&local, arg3)` then `ZwQueryValueKey(key, &local, KeyValueFullInformation, ...)` |
| 4 `SIZE_T` | The byte length of that name, including the terminating NUL (usbehci passes `0x2C` for a 21-character name). Used only to size the `KEY_VALUE_FULL_INFORMATION` allocation - `arg4 + arg6 + 0x18` |
| 5 `PVOID` | Destination. On success `arg6` bytes are copied from `info + info->DataOffset` (the `[ebx+8]` read is what identifies the information class as Full, not Partial) |
| 6 `SIZE_T` | The number of bytes copied. Unconditional - it is not clamped to `DataLength`, so ask for exactly what the value holds |

The return value carries no diagnosis at all, and this is the finding that
constrains any caller. The thunk ends `push eax; call <converter>`, and the
converter is four instructions in both builds - `neg eax / sbb eax,eax / and
eax,8` - i.e. `MPSTATUS = (ntStatus == STATUS_SUCCESS) ? MP_STATUS_SUCCESS :
MP_STATUS_UNSUCCESSFUL`. Not `NT_SUCCESS`: any warning or informational
status also maps to 8. So "value absent", "key would not open", "buffer too
small" and "pool allocation failed" are one indistinguishable code, and a
caller may only conclude *present and read* or *not*.

It is PASSIVE_LEVEL only. The reader calls `IoOpenDeviceRegistryKey` and
`ZwQueryValueKey`, and allocates its scratch buffer from PagedPool (`push
1` as the pool type, tag `'usbp'`). Every one of those is a PASSIVE-level
requirement. Do not read the sequence "NUSB has no cache, SP4 sometimes has
one" (below) as a licence to call it higher: the miniport cannot see which
branch it will get.

The two builds differ by one layer, and it changes nothing for a caller.
NUSB's thunk calls the reader directly. SP4's calls a dispatcher (`0001D85A`)
that branches on a bit in usbport's device extension: one way is the same
reader plus a table insert, the other serves from an internal table and returns
`STATUS_OBJECT_NAME_NOT_FOUND` or `STATUS_BUFFER_TOO_SMALL` without touching
the registry. The reader itself is instruction-for-instruction identical
across the two images - all 64 instructions, differing only in relocated
branch targets and IAT slots, each of which resolves to the same imported name.

The shipping miniport's own use is the template, and it is in
`StartController`. Each `usbehci.sys` references its packet's `0xF0` slot
exactly once, through a five-argument wrapper that reads a 4-byte value with
`BOOL = TRUE` and, if the result is nonzero, ORs a flag into its miniport
extension. Its one caller passes `L"EnIdleEndpointSupport"` and sits inside
`StartController` (NUSB `00013B9E` within `00013AF8`; SP4 `00013B58` within
`00013AB2`), which is the PASSIVE context the reader requires - so the IRQL
rule above is observed in a shipping caller, not only derived from the imports.
`BOOL = TRUE` also means the shipping precedent reads the software (driver)
key, which is what a plain `AddReg` under an INF's install section writes.

The miniport needs no import of its own for any of this. The `Zw*` calls,
the pool allocation and the string work are all inside `usbport.sys`. That is
the property that makes this the only registry channel this project may use -
`scripts/import-gate/xhci98-imports.allow` denies the `Zw*` names outright.

## 7. Locking, IRQL, and threading summary

These are the ABI *facts*. What this driver does about them - the single
innermost driver-image lock, its order against the two below, the DIRQL
exception, the static review of every entry point, and where the driver's own
state goes - is derived from this table in
`docs/contributing/design/05-locking-model.md`.

| Context | Facts |
|---|---|
| `MiniportSpinLock` | usbport's lock around endpoint open/close/state/submit/abort, `CheckController`, `Get32BitFrameNumber`, Enable/DisableInterrupts, and the root-hub status-query callbacks. The root-hub Set/Clear feature callbacks are a documented exception in ReactOS: they run at DISPATCH_LEVEL without this lock. Evidence: [endpoint.c:762-783, 1049-1055, 1218-1247, 1495-1502, 1564-1587; usbport.c:568-585, 1177-1184; roothub.c:148-164, 170-285] |
| `MiniportInterruptsSpinLock` | Separate lock held (at DPC level) around `InterruptDpc` [usbport.c:1089-1095]. Implication: `InterruptDpc` can run concurrently with a `MiniportSpinLock`-holding callback - the miniport needs its own interior lock for structures shared between the DPC path and the submit path (ring enqueue/dequeue state) |
| Async timer DPC | `UsbPortRequestAsyncCallback` callbacks run at DISPATCH_LEVEL without either usbport miniport lock [usbport.c:2089-2109], confirmed in the NUSB binary at `0002785E`. They can race `InterruptDpc` and callback paths on SMP, and can run stale after the operation completed - including after a stop and a restart, since nothing can cancel one and usbport zeroes the miniport extension before every start. Validate a token that does *not* live in that extension before touching anything, then generation and lifecycle state under the miniport's own lock; see "`UsbPortRequestAsyncCallback`: what its return value is worth" above |
| ISR | `InterruptService` runs at DIRQL from usbport's connected ISR; gated by usbport's enable flags [usbport.c:1110-1142] |
| PASSIVE contexts | `StartController` (can `UsbPortWait`), `StopController`/power paths, and the worker thread that invokes `CheckController` (but under `MiniportSpinLock`, i.e. raised to DISPATCH at the call itself) |
| Worker/timer | usbport runs a worker thread and a 500 ms timer [pnp.c:881-882]; state-change confirmation is frame-number based [endpoint.c:407-424] |
| Registration packet | Keep it global; it is both the callback table usbport copied and the service table the miniport calls through |

## 8. Enumeration flow facts the miniport must survive

From `USBPORT_InitializeDevice` [device.c:1316-1414] and
`USBPORT_ReopenPipe` [endpoint.c:1194-1306]:

1. usbport allocates the device address itself (bitmap, counts up from 1)
   [device.c:1339].
2. It sends SET_ADDRESS as a plain EP0 control transfer
   (`bRequest = USB_REQUEST_SET_ADDRESS`, wValue = address) through
   `SubmitTransfer` [device.c:1342-1353]. The miniport intercepts (Address
   Device BSR = 0) and completes success - invariant doc.
3. It then updates EP0's properties (`DeviceAddress` = new address,
   `TotalMaxPacketSize` = real MPS0) and calls `USBPORT_ReopenPipe`
   [device.c:1362-1368], which tears EP0's private state down and rebuilds
   it. It does not do that through `CloseEndpoint`. What happens is
   `SetEndpointState(REMOVE)` -> a 2 ms wait -> the endpoint extension and the
   common buffer are destroyed -> `QueryEndpointRequirements` -> `OpenEndpoint`.
   So the miniport will see, for the same physical device: EP0 open at address
   0 -> transfers -> REMOVE -> EP0 open at address N, with no close
   callback anywhere in between. The slot must survive that, and the reopened
   EP0 (usbport address N, never seen before) must resolve to the existing
   slot - key the map by port/topology at open time, not only by address.
   - Two consequences of the reopen sequence (row `ReopenEndpoint` in
     section 4) decide the memory model: the miniport endpoint extension is
     zeroed, so the address -> Slot ID map cannot live there; and the
     endpoint's common buffer is freed and separately reallocated, so its
     address must be re-read from the properties at every open and nothing may
     cache it. The slot's Device Context and EP0 transfer ring therefore live
     in the controller common buffer instead
     (`docs/contributing/design/04-controller-common-buffer.md` section 3.4).
   - The binaries show a free followed by a fresh allocation request; they do
     not guarantee the allocator returns a different address, so treat "it
     may move" as the rule rather than "it will".
4. A 10 ms settle wait follows, then GET_DESCRIPTOR traffic resumes
   [device.c:1373-1388].
5. usbport reopens a pipe *after* its `SET_INTERFACE`, not before (batch
   9-V, runtime, including a device with three alternate settings), so an
   endpoint is always opened with its alternate already selected and a
   descriptor-derived isochronous interval read at open time is the one in
   force. `DescIntervalsStaleAfterSelect` counts the opposite ordering and read 0,
   so no repair for that ordering is needed.

### The transaction-translator lookup, and why `USB_MINIPORT_FLAGS_USB2` must be set

Binary-confirmed in both shipping builds. This is not a ReactOS-derived rule:
it was read out of the
instructions after a Full-Speed device on a managed root port bugchecked both
primary targets. Addresses are SP4 unless noted; NUSB is the same logic at its
own addresses.

`USBPORT_CreateDevice` (SP4 `0x26445`, the function from `0x26400`) gates a
transaction-translator lookup on two conditions:

```
0x26445  test byte ptr [eax+18h],10h   ; MiniPortFlags & USB_MINIPORT_FLAGS_USB2
0x26449  je   0x26461                  ; not USB2 -> no lookup at all
0x2644B  test byte ptr [ebp+15h],4     ; PortStatus & USB_PORT_STATUS_HIGH_SPEED
0x2644F  jne  0x26461                  ; High Speed -> no lookup
0x26459  call 0x2783A                  ; USBPORT_GetTt
0x2645E  mov  [ebp-10h],eax            ; -> DeviceHandle->TtExtension
```

`USBPORT_GetTt` (SP4 `0x2783A`, NUSB `0x271B2`) has no empty-list guard on its
single-TT branch. It walks up `HubDeviceHandle` until it finds a device handle
whose `DeviceSpeed` is `UsbHighSpeed` (returning NULL only if the walk runs off
the top), then:

```
0x27861  cmp  dword ptr [esi+64h],1    ; TtCount <= 1 ?   (0 takes this branch)
0x2786A  jbe  0x27892
0x27892  sub  eax,ecx                  ; eax = TtList.Flink
0x27894  neg  eax
0x27896  sbb  eax,eax                  ; IsListEmpty folded to a mask
0x27898  and  eax,edx                  ; empty list -> eax = 0
0x2789A  lea  ebx,[eax-14h]            ; CONTAINING_RECORD, UNCONDITIONALLY
```

An empty `TtList` therefore yields `0xFFFFFFEC`, not NULL. The *multi*-TT
branch at `0x27874` does test for empty and returns NULL; ReactOS's
`if (!TtCount) return NULL;` and `if (IsListEmpty(...)) return NULL;`
[device.c:938-946] have no counterpart in either shipping binary - the same
"ReactOS added the guard the shipping code lacks" shape as the
`NumberOfPorts = 0` arithmetic in section 4.

`USBPORT_OpenPipe` (SP4 `0x24EBC`) then null-checks `TtExtension` at `0x24FC6`,
which `0xFFFFFFEC` passes, and inserts at `TtExtension + 0xC`:

```
0x24FDD  add  ecx,0Ch                  ; 0xFFFFFFEC + 0xC = 0xFFFFFFF8
0x24FE0  call ExfInterlockedInsertTailList
```

`ExfInterlockedInsertTailList` (`ntoskrnl.exe` RVA `0x6B0`, loaded
`0x804006B0`) reads `ListHead->Blink` at its third instruction, faulting on
`0xFFFFFFFC` with interrupts already off from its own `cli` - which is the
whole of `STOP 0x0000000A (0xFFFFFFFC, 0xFF, 0x00000000, 0x804006B2)`.

Two facts make this reachable for every non-HS device on a root port, not
just an unlucky one:

- `USBPORT_RootHubCreateDevice` sets the root hub's *own* device handle to
  `UsbHighSpeed` iff the miniport declared `USB_MINIPORT_FLAGS_USB2`
  (SP4 `0x2E1C1`-`0x2E1D9`: `test byte ptr [edi+18h],10h` selecting `2` or `1`
  into handle offset `0x38`). So the upward walk stops at the root hub
  immediately and never reaches the safe NULL exit.
- usbport's root-hub device descriptor template (SP4 RVA `0x1CD60`:
  `12 01 00 01 09 01 00 08 ...`) has `bDeviceProtocol = 0` - a hub with no
  transaction translator - and only `bcdUSB` is patched (to `0x0200`, SP4
  `0x2E2F9`) when the USB2 flag is set. So no hub driver ever calls
  `USBPORT_Initialize20Hub` for the root hub, and its `TtCount`/`TtList` stay
  0/empty for the life of the controller.

On genuine EHCI hardware the combination is unreachable: a Full or Low Speed
device on an EHCI root port is released to a companion controller, so usbport
is never asked to create a non-HS device whose parent is the EHCI root hub.

`MiniPortVersion` is not a lever on these builds. ReactOS switches the
root-hub descriptor type on it [roothub.c:863-876]; both shipping builds instead
`movs` a fixed 7-byte template (SP4 RVA `0x1CD98`: `09 29 ...`, i.e.
`bDescriptorType = 0x29`) and contain no version branch there at all. Declaring
`USB_MINIPORT_VERSION_XHCI` would change nothing about this path.

Consequence for any miniport whose root ports can carry Full or Low Speed
devices directly (which is every xHCI controller, and no EHCI one): with
`USB_MINIPORT_FLAGS_USB2` set, the only way to keep usbport out of `GetTt` is
for `RH_GetPortStatus` never to report a connected device without
`PORT_HIGH_SPEED`. Note what that does not cover: a device behind a
*USB 1.1 hub* plugged into a root port is reported non-HS by that hub, and the
walk then stops at the 1.1 hub's own handle - which the same lie has marked
`UsbHighSpeed`, with `TtCount = 0` - so the identical fault returns one level
down. A genuine USB 2.0 hub is unaffected, because it really is High Speed and
its hub driver does give it a TT.

That last prediction was refuted by the run (batch 7b-V0, runtime, both
targets): QEMU's hub is USB 1.1 with no TT, an FS device entered it on a root
port, and neither target bugchecked. `HubAddr` came back as the *success*
reading, so usbport had built a TT record for a hub that has none.

The mechanism offered for that (the speed override marks the 1.1 hub High
Speed, so `USBPORT_Initialize20Hub` runs for it and gives it a TT) is a
reading, not a measurement, and it is open question 6 of
`docs/contributing/design/02-hub-topology-route-string.md`; if it holds, every TT
field filled for a device behind a 1.1 hub describes a translator that does not
exist. The prediction was a claim about a disassembly, and the run is what
tests it. The operating limitation "do not plug a USB 1.1 hub into a root
port" therefore rests on the open question, not on a measured fault.

The other direction - dropping `USB_MINIPORT_FLAGS_USB2` - removes the crash
class entirely and loses High Speed on Windows 98, and the loss is structural
rather than a matter of degree (static). Without the flag the
root hub PDO is `USB\ROOT_HUB`, which NUSB's `USB2.INF` does not claim (it claims
only `USB\ROOT_HUB20`), so Windows 98 binds its own 1.1 `usbhub.sys`. That
binary was disassembled: it extracts `(PortStatus >> 9) & 1` - the Low-Speed bit
- at `0x146C4` and passes that boolean, not the port-status word, into
device creation at `0x162EB`; a whole-image search finds zero references to
bit 10 (the one `0x400` hit is an unrelated `sub ecx,400h`) against two
Low-Speed extractions. The High-Speed bit would be discarded by the hub driver
before usbport ever saw it. The compensating virtue is real: with the flag
clear, `USBPORT_CreateDevice`'s `je` at `0x26449` above means `GetTt` is never
called in any topology. The trade is recorded here so it is not silently
re-decided.

## 9. What still requires the NUSB binary

This transcription pins the ReactOS side. The spike (procedure in
`docs/usb-xhci-info/usbport-miniport-interface.md` section 6) must still confirm against the
Win2000 SP4-lineage binary:

1. Accepted `Version` argument values. Answered: `>= 100` is accepted,
   `>= 200` selects the full packet; all shipping miniports pass 200. Still
   open: whether `MiniPortVersion = 0x04` (XHCI) is tolerated in the packet
   field or must masquerade as EHCI (0x03). That value is only consumed
   later, so it stays a runtime spike question.
2. Actual packet size the binary copies. Answered: 0x13C (316) at Version
   >= 200, 0x12C (300) below, matching the C_ASSERT above. The 336-byte
   interface C_ASSERT matches the SP4/XP builds; NUSB's 5652 allocates 332
   because it does not retain `Version`. Neither is miniport-visible.
3. The tail group (offsets 0x124-0x138), presence and order. Answered:
   present and in this order; the shipping `usbehci.sys` fills 0x124-0x130
   and leaves 0x134/0x138 alone.
4. `USBPORT_RESOURCES` size/layout (52/0x34 expected) as passed to
   `StartController`. Answered (runtime, live on Win2000 SP4; the same 52
   bytes were logged on Win98). Every field lands
   where section 5 says: `ResourcesTypes = 6` (MEMORY+INTERRUPT),
   `HcFlavor = 1000` (EHCI generic - usbport derives it from the declared
   `MiniPortVersion`, so a miniport does not choose its own flavor),
   `InterruptVector`/`Level`/`Affinity`/`ShareVector`/`InterruptMode` at
   0x08-0x18, `ResourceBase` + `IoSpaceLength` at 0x20/0x24 matching the BAR
   Device Manager reports, `StartVA`/`StartPA` at 0x28/0x2C, and
   `LegacySupport`/`IsChirpHandled` both zero at 0x30. Decode table in
   `docs/usb-xhci-info/usbport-miniport-interface.md`, "Observed callback sequence (Win2000
   SP4, Phase 3 task 9)".
5. Whether `InterruptNextSOF` / `PollController` / `FlushInterrupts` are ever
   called (unobserved in the ReactOS mirror). Answered, all three.
   - The endpoint-path census in section 4 answered the general form of the
     question for the whole packet: of the "in" slots, two are never called
     by either build, `CloseEndpoint` (0x34) and `GetEndpointState` (0x5C).
     `PassThru` (0xE0) is called, via a register in both builds; see that
     census for the method and its limits, and "What reaches `PassThru`" for
     what drives those sites (the vendor escape `IOCTL_USB_USER_REQUEST` and
     one test-mode-only internal probe).
   - `PollController` is called on both targets, repeatedly, from the bind
     onward (Phase 3 traces).
   - `FlushInterrupts` was answered by disassembly: all three builds call it
     from the D0 power completion, holding no lock; see "`FlushInterrupts`:
     the call site the mirror does not have". The Phase 3 traces never
     reached it because they covered only start/stop/idle-suspend, which is a
     statement about the traces, not the binaries.
   - `InterruptNextSOF` was answered by disassembly the same way: two call
     sites per build, both in the endpoint state-change machine, `VOID (ext)`
     at DISPATCH under `MiniportSpinLock`, and nothing waits on it; see
     "`InterruptNextSOF`: what it asks for, and what happens when nothing
     answers" in section 4.
   - `XHCI_EXTENSION.InterruptFlushes` and `InterruptNextSofRequests` confirm
     both readings at runtime from a release build.
6. The iso parameter structures (ReactOS has none). Closed, derived from both
   shipping binaries; section 4, "Isochronous transfers".
7. Whether the NUSB binary also copies `PowerOnToPowerGood` directly into
   the hub descriptor (ReactOS does); expected units are 2 ms, so xHCI's
   20 ms requirement is encoded as 10. Answered (static, SP4
   `0x2E442` with the same descriptor build in NUSB): it is copied directly
   and truncated to a `UCHAR`, alongside `bNumberOfPorts`,
   `wHubCharacteristics` and `bHubControlCurrent`. Encode 20 ms as 10. See
   the root-hub block in section 4.
8. Whether the binary's SG elements are page-granular like ReactOS's
   (affects worst-case TRB count per transfer). Answered (static, both
   builds): yes.
   - `ElementLength = 0x1000 - (PA.LowPart & 0xFFF)` clamped to the remaining
     mapped length, so worst case is `ceil(N / 4096) + 1` elements for an
     N-byte transfer. The same pass confirmed the DMA-adapter path and
     settled the `HighPart` handling; see the binary read under
     `USBPORT_SCATTER_GATHER_LIST / _ELEMENT` in section 5.
   - Still not answered by a static pass, and not claimed: runtime element
     ordering versus `SgOffset` (see
     `docs/usb-xhci-info/usbport-miniport-interface.md`, "What Phase 3 can and
     cannot prove about transfer mapping"). Ordering versus `SgOffset` remains
     a runtime question, because it cannot be read out of the code.
   - The endpoint census closed the rest of the `SubmitTransfer` precondition
     question statically: the list pointer is never NULL,
     `SgElementCount == 0` is legal and common, and the transfer-parameters
     pointer is an interior pointer of the transfer record (section 4).
9. Whether NUSB repeats ReactOS's split root-hub locking contract: status
   queries under `MiniportSpinLock`, Set/Clear feature callbacks unlocked.
   Still open.
   - What is traced in both builds: on the class GET_STATUS execution path
     and in the SCE scan there is no spin-lock acquisition in usbport-owned
     code, leaf helpers included (a completed transitive walk; GET_STATUS's
     enclosing function does have other branches that reach the port-power
     lock, so this is about the path).
   - The feature routing reaches a lock on the `SET_FEATURE(PORT_POWER)`
     branch, which that helper releases before either of its own downstream
     `RH_SetFeaturePortPower` calls.
   - None of that reaches the question. The untraced half is whether a caller
     holds `MiniportSpinLock` at callback entry, so ReactOS's split contract
     is neither confirmed nor refuted. The miniport is written not to depend
     on the answer.
10. The async-callback contract: callback IRQL/locks, whether pending timers
    are cancelled or drained during stop/remove, and whether the return value
    reports allocation/scheduling failure. ReactOS runs an untracked timer
    DPC, exposes no cancellation service, and returns 0 on both success and
    allocation failure. The NUSB build agrees on every point; see
    "`UsbPortRequestAsyncCallback`: what its return value is worth" in
    section 4.
11. The two root-hub rules in sections 2 and 4: the endpoint-0 control
    `MPSTATUS` -> `RHSTATUS` mapping that turns `MP_STATUS_FAILURE` into "no
    changes", and the `NumberOfPorts >= 1` requirement. Answered (static,
    both builds). Both rules hold. `USBPORT_MPStatusToRHStatus` is seven
    instructions with the ReactOS semantics at SP4 `0x29D8E` / NUSB `0x2964A`, and
    `USBPORT_RootHubCreateDevice` computes `((NumberOfPorts - 1) >> 3) + 1`
    mask bytes with no guard in front of it, so a zero port count asks for
    `0x40000032` bytes and no root-hub PDO is built. The same pass settled the
    status-change endpoint's hard-error rule, the `RH_EnableIrq`/`RH_DisableIrq`
    latch semantics, the discarded returns on the power/chirp startup path, and
    open item 7; see the root-hub block in section 4. The runtime-probe warning
    stands: do not feed `NumberOfPorts = 0` to a live usbport.
12. What `usbhub20.sys` does with a Full-Speed device that the root hub has
    announced as High Speed (the speed override, section 8). Its
    port extension records Low Speed as `8` and High Speed as `0x800000` at
    `+0x1C` (SP4 `0x198AA`-`0x198E6`); the consumer of `0x800000` is untraced
    in either build. The runs since - Full-Speed devices on root ports on both
    targets, batch 7b-V0's 1.1 hub - have shown no consequence, which is a
    measurement of those vehicles and not of the binary. Static, open.
