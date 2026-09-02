# usbport.sys Miniport Interface - Derivation Guide

The `USBPORT_REGISTRATION_PACKET` interface between `usbport.sys` and its
host-controller miniports is undocumented by Microsoft. This doc is the
working guide for deriving it: where the authoritative sources are, what shape
to expect, how to map each callback onto xHCI operations, and how to validate
the result against the actual `usbport.sys` binary NUSB installs. It was
written for the Phase 3 spike (`docs/contributing/roadmap.md`) and has since
become the logbook of what that spike and the runs after it found.

Before using anything below: the struct, field and callback names in this
file were written down from memory of the ReactOS reimplementation. They are
a map, not a source. The source-verified transcription is
`docs/usb-xhci-info/usbport-miniport-abi.md`, transcribed field by field from
the local ReactOS mirror (`external/reactos/`, pinned commit in
`external/README.md`). It supersedes this file's from-memory details wherever
the two differ (it corrects at least the version-constant families, the
`USBPORT_GetHciMn` usage, and the `UsbPortWait` IRQL claim). Use this file for
the strategy, mapping, and validation procedure; use the ABI doc for exact
layouts, signatures, and constants. Both remain subordinate to the binary:
where ReactOS and the NUSB binary disagree, the binary wins (section 6).

---

## 1. Where the truth lives

| Source | What it gives you |
|---|---|
| ReactOS `sdk/include/reactos/drivers/usbport/usbmport.h` | The miniport-facing header: registration packet layout, callback typedefs and signatures, version constants, flags, status codes. This one file is most of the ABI. |
| ReactOS `drivers/usb/usbport/` | The port driver itself: `usbport.c` (`USBPORT_RegisterUSBPortDriver`, DriverEntry takeover), `endpoint.c` (endpoint open/state machine), `roothub.c` (how RH_* callbacks are consumed), `device.c` (`USBPORT_InitializeDevice` - the SET_ADDRESS flow to intercept), `queue.c` (transfer queuing), `iface.c`, `pnp.c`, `power.c` |
| ReactOS `drivers/usb/usbehci/` | A complete USB2 miniport against that header: `usbehci.c` (DriverEntry, packet fill, StartController and the interrupt path, all in the one file - the direct template for `src/xhci_dispatch.c`), `roothub.c` |
| ReactOS `drivers/usb/usbohci/`, `usbuhci/` | USB 1.1 miniports - secondary templates showing which parts vary by controller type |
| NUSB's installed `usbport.sys` (Win98 VM) and Win2000 SP4's native one | The binaries the ABI must match. Same Win2000 SP4 lineage; record and compare versions (roadmap Phases 2a/2b) |
| NUSB's installed `usbehci.sys` | A *real* Microsoft miniport built against that exact binary - its import table and its own registration call are ground truth |

The relevant ReactOS files are already mirrored locally under
`external/reactos/` (usbmport.h, the full `usbport/` driver, and the
`usbehci`/`usbohci`/`usbuhci` miniports) - see `external/README.md` for the
pinned upstream commit and the refresh procedure. `docs/usb-xhci-info/usbport-miniport-abi.md`
is the transcription of that mirror.

License note: ReactOS is GPL. Use it as documentation of the ABI (struct
layouts and function signatures needed for interoperability), and write this
project's code independently. Do not paste ReactOS function bodies into
`src/`.

Caveat on ReactOS fidelity: ReactOS reimplements the NT5.1/XP-era stack. The
Win2000 SP4-era `usbport.sys` that NUSB ships may have a smaller or
differently-ordered packet. That is what the binary validation in section 6
is for. Do not assume ReactOS == the 2195.x binary.

## 2. Registration flow (the shape to expect)

From ReactOS `usbehci.c` / `usbport.c`:

1. The miniport's `DriverEntry` fills a `USBPORT_REGISTRATION_PACKET` and calls
   `USBPORT_RegisterUSBPortDriver(DriverObject, Version, &RegPacket)`
   (stdcall export of `usbport.sys`).
2. `Version` is a miniport-interface version constant. ReactOS defines two
   distinct constant families (`docs/usb-xhci-info/usbport-miniport-abi.md` section 1):
   `USB10/USB20_MINIPORT_INTERFACE_VERSION` (100/200) is what the `Version`
   *argument* takes (`usbehci` passes 200), while the packet's
   `MiniPortVersion` *field* takes `USB_MINIPORT_VERSION_OHCI/UHCI/EHCI/XHCI`
   (0x01-0x04). Do not conflate them. If the argument value is wrong, the
   call fails - the return code is the first ABI probe.
3. On success, usbport takes over the driver object: it sets every
   `DriverObject->MajorFunction[]` and `DriverExtension->AddDevice` itself.
   After registration the miniport never sees an IRP; its entire surface is
   the callbacks in the packet. `DriverEntry` should do nothing else.
4. The packet is bidirectional: the miniport fills in its data fields and
   callback pointers; usbport writes *its own* service-function pointers back
   into (or alongside) the packet for the miniport to call at runtime
   (`UsbPort*` helpers, section 4). Keep the packet (or usbport's copy of it)
   reachable from the miniport extension.

Data fields to expect in the packet (names per ReactOS; exact offsets, types,
and the full 316-byte x86 layout are in `docs/usb-xhci-info/usbport-miniport-abi.md`
section 3):

| Field | Meaning | xhci98.sys value (initial) |
|---|---|---|
| `MiniPortVersion` | Controller-type constant (`USB_MINIPORT_VERSION_*`), not the interface version | `3`, what `usbehci` fills; the register call's `Version` argument is `200` (step 2 above) |
| `MiniPortFlags` | `USB_MINIPORT_FLAGS_*` OR-mask | `INTERRUPT \| MEMORY_IO \| USB2 \| POLLING` = `0x95`, what `usbehci` fills (names per usbmport.h) |
| `MiniPortBusBandwidth` | Bandwidth budget usbport uses for periodic scheduling | `TOTAL_USB20_BUS_BANDWIDTH` = 400000, what usbehci passes (confirmed in the mirror) |
| `MiniPortExtensionSize` | Bytes usbport allocates for the per-controller extension handed to every callback | `sizeof(XHCI_EXTENSION)` |
| `MiniPortEndpointSize` | Bytes per endpoint extension | `sizeof(XHCI_ENDPOINT)` |
| `MiniPortTransferSize` | Bytes per transfer extension | `sizeof(XHCI_TRANSFER)` |
| `MiniPortResourcesSize` | Bytes of common-buffer the controller needs at start (EHCI puts its periodic frame list here) | Enough for DCBAA + command ring + ERST + event ring + scratchpad (verify how the buffer is delivered in `StartController`'s resources) |

The extension pattern matters: usbport allocates all extension memory.
The miniport never allocates its own device extension; it receives a
`MiniPortExtensionSize`-byte area as the first argument of every callback and
keeps all controller state there.

## 3. Callback families and their xHCI mapping

Callback names below are the ReactOS `usbmport.h` set; the exact signatures,
IRQL/locking contracts, and observed call sites for every entry are
transcribed in `docs/usb-xhci-info/usbport-miniport-abi.md` section 4. The mapping column is
this project's design work - how each generic usbport request translates to
the xHCI hardware model documented in `docs/usb-xhci-info/xhci-programming.md` and
`docs/usb-xhci-info/xhci-data-structures.md`.

### Controller lifecycle

| Callback | xHCI implementation |
|---|---|
| `StartController` | The Phase 4 init sequence: initial BAR0 sanity reads, BIOS handoff, halt+reset, DCBAA/scratchpad/command+event rings, port topology classification, then RUN. Receives a resources structure (mapped register base, IRQ info, common-buffer block - verify layout in usbmport.h `USBPORT_RESOURCES`) |
| `StopController` | Halt controller, mask interrupter, free nothing usbport owns |
| `SuspendController` / `ResumeController` | Halt/re-run. Near-inert on Win98 (no real power management), but Win2000 SP4 calls these for real on S1-S3 and on device power IRPs - implement quiesce/restore properly and validate there, not on Win98 (`docs/usb-xhci-info/win98-wdm.md`, "Win2000 enforces what Win98 silently forgives") |
| `CheckController` | Periodic health poll from usbport - check USBSTS.HCE/HSE. It returns `VOID` (`src/xhci_usbport.h`): usbport reads no verdict from it, so a failure found here is raised through `UsbPortInvalidateController` |
| `ResetController` | Not a re-init slot, and not a wait-for-rescue slot either. usbport dispatches it from a DPC at DISPATCH_LEVEL holding one of its own spin locks (measured in both shipping builds, `docs/usb-xhci-info/usbport-miniport-abi.md`), so nothing that waits is legal here, and the init sequence waits twice. What this driver does here: mask the interrupt enables, mark the controller failed, raise a recovery request, return. Do not end at the mark: the census in the ABI doc found that usbport issues a stop/start only on a PnP or power transition something outside it initiates, that the reset DPC arms nothing, and that `CheckController` returns `VOID`, so a miniport that latches here and waits stays latched until the next cold boot. That was Finding 3 in `docs/contributing/runs/run-13e.md`; its Finding S records the latch. The request is armed by the 500 ms `CheckController` poll rather than here (arming from inside usbport's reset-DPC lock would nest usbport's timer machinery inside it), and performed by the resulting `UsbPortRequestAsyncCallback` DPC, which holds no usbport lock. usbport asks for none of this: the only producer of `UsbPortInvalidateController(RESET)` is a miniport (usbport's single internal call site passes `SURPRISE_REMOVE`), so the whole loop is the miniport calling itself back through a usbport DPC and a spin lock |
| `InterruptService` | Read USBSTS; if EINT = 0 return "not ours"; clear EINT, clear IMAN.IP, return "ours" (usbport then queues the DPC) |
| `InterruptDpc` | Drain the event ring: Transfer Events -> complete transfers (section 4), Port Status Change -> update PORTSC shadow + invalidate root hub, Command Completion -> wake command waiter |
| `EnableInterrupts` / `DisableInterrupts` | Set/clear IMAN.IE and USBCMD.INTE |
| `Get32BitFrameNumber` | `MFINDEX >> 3` (microframes -> frames) + software-extended rollover counter |
| `InterruptNextSOF` | Count the call and touch no register. Do not request an interrupt at the next frame via an MFINDEX-wrap event or a short timer: both shipping builds call it at the tail of `USBPORT_SetEndpointState` and again from the walker that drains their endpoint state-change list, read no return value and wait on nothing, and that list is drained regardless by a self-rearming 500 ms timer DPC. A do-nothing body costs one timer tick of latency per endpoint state change. What the drain needs is `Get32BitFrameNumber` advancing; a stuck number re-queues the endpoint at the head of the list and aborts the whole pass. See `docs/usb-xhci-info/usbport-miniport-abi.md`, "`InterruptNextSOF`: what it asks for, and what happens when nothing answers" |
| `PollController` | For `USB_MINIPORT_FLAGS_POLLING` mode - not planned; stub it |
| `FlushInterrupts` | Count the call and touch no register. Do not ack pending IP/EINT here: acknowledging is inseparable from an `ERDP` write (clearing IP with Event Handler Busy set silences the interrupter for good), and the disassembled call site, the D0 power completion in all three shipping builds, holds neither miniport lock, so it can run alongside `InterruptDpc`. It is therefore the one caller that could not hold the controller lock every `ERDP` writer holds (design doc 05 section 5). See `docs/usb-xhci-info/usbport-miniport-abi.md`, "`FlushInterrupts`: the call site the mirror does not have" |

### Endpoints

| Callback | xHCI implementation |
|---|---|
| `QueryEndpointRequirements` | Report per-endpoint common-buffer need: one transfer-ring segment (e.g. 64 TRBs = 1 KB, 64 KB-boundary safe). usbport allocates it and hands PA+VA to `OpenEndpoint`. This is how transfer rings get physically-contiguous memory under Option A (verify the buffer fields in the endpoint-properties struct) |
| `OpenEndpoint` | The pivot point of the whole design. Properties give device address, endpoint address, speed, max packet size, transfer type, interval. If this is EP0 for an address the miniport has never seen: Enable Slot -> Address Device(BSR=1) using speed/port from the properties, then build the EP0 context. For other endpoints: build endpoint context, Configure Endpoint. Maintain the usbport-address -> SlotID map (`docs/contributing/architecture.md` enumeration flow) |
| `ReopenEndpoint` | Same endpoint, changed properties (e.g. EP0 max packet size fixed after first descriptor read) -> Evaluate Context (EP0 MPS) or drop+add via Configure Endpoint |
| `CloseEndpoint` | Neither shipping build ever calls this (whole-image census, ABI doc section 4): not from the enumeration-time EP0 reopen, not from the endpoint delete path, nowhere in either image. Keep it signature-correct and harmless, but implement no teardown that only it would trigger: usbport frees an endpoint's common buffer and `ExFreePool`s the endpoint with no callback, so everything the xHC still points at has to be quiesced from the port/disconnect path instead |
| `GetEndpointState` / `SetEndpointState` | `GetEndpointState` is never called either. Only `SetEndpointState` is live: map usbport's states (section 2 of the ABI doc: REMOVE 4, ACTIVE 3, PAUSED 2) onto EP context state + Stop Endpoint / doorbell. "Paused" -> Stop Endpoint; "Active" -> ring doorbell. The state usbport itself polls after an open is its own software copy, advanced by its DPC/500 ms timer once `Get32BitFrameNumber` moves past the frame stamped at the `SetEndpointState` call. That poll is uncapped in both builds, so a frame number that does not advance hangs the enumerating thread rather than timing the open out |
| `PollEndpoint` | usbport calls this to have the miniport scan for completed work (EHCI walks its QH). xHCI is event-driven, so normally a no-op; useful as a stall-recovery sweep |
| `SetEndpointDataToggle` | xHCI tracks toggles in hardware (EP context). Implement as no-op; toggle reset happens implicitly via Reset Endpoint + Set TR Dequeue Pointer on the reset-pipe path |
| `GetEndpointStatus` / `SetEndpointStatus` | Report/clear halted state: on Stall Error event mark the endpoint halted; "clear" -> Reset Endpoint + Set TR Dequeue Pointer |
| `RebalanceEndpoint` | Periodic-schedule rebalancing (usbport's USB2 budgeter). xHCI does its own scheduling: accept and update the context if intervals changed (likely near-no-op; verify when usbport calls it) |

Bandwidth double-accounting. usbport budgets USB2 periodic bandwidth
itself (`MiniPortBusBandwidth`) and only opens endpoints it believes fit, but
the xHC independently admission-controls: Configure Endpoint can fail with
completion code 7 (Resource Error), 8 (Bandwidth Error) or 35 (Secondary Bandwidth Error) for an endpoint usbport
already approved, because the xHC's internal budget (which includes overheads
usbport does not model) disagrees. `OpenEndpoint` must map that command
failure to usbport's "no bandwidth" miniport status (verify the constant in
usbmport.h; ReactOS has a distinct no-bandwidth code) so `usbhub` degrades
gracefully, rather than returning a generic failure that reads as a broken
device. Do not retry the command; the xHC's answer will not change.

### Transfers

| Callback | xHCI implementation |
|---|---|
| `SubmitTransfer` | Receives transfer parameters + an SG list of physical addresses, the Option A no-bounce-buffer path in `docs/usb-xhci-info/win98-wdm.md`. The list's construction is confirmed statically in both shipping builds (DMA adapter, page-granular, 24-byte elements); its runtime contract is not; see "What Phase 3 can and cannot prove about transfer mapping" below. Control: Setup+Data+Status TRBs (Setup packet bytes live in the transfer parameters; verify field name); bulk/interrupt: Normal TRB chain with TD Size math. Ring doorbell. Intercept SET_ADDRESS here (bmRequestType 0x00, bRequest 0x05 on EP0): never queue it; issue Address Device(BSR=0), complete as success (`docs/contributing/implementation-invariants.md` "Device Addressing") |
| `SubmitIsoTransfer` | Isoch TRBs with SIA=1 initially; per-packet completion accounting (Phase 9) |
| `AbortTransfer` | Stop Endpoint -> walk ring for the transfer's TRBs -> Set TR Dequeue Pointer past them -> complete as canceled |

Completion path: from `InterruptDpc`, a Transfer Event's TRB pointer + Slot/DCI
identify the pending transfer; compute the transferred length as the sum of
the TRB lengths of the TRBs that event completes, minus the event's residual
(`XhciRingSumTrbLengths`; `docs/usb-xhci-info/xhci-data-structures.md`, "Event TRBs"), map
the completion code (`docs/usb-xhci-info/xhci-programming.md`), then hand it back via the
usbport-provided completion service (`UsbPortCompleteTransfer` /
`...IsoTransfer`; verify names), not by completing any IRP.

Do not use `requested - residual`. That is a single-TRB-TD rule, and an early
build of this driver got it wrong: a control transfer is 2-3 TDs and a data
stage can be many TRBs, so anything written against it silently reports the
wrong number on every multi-TRB transfer. See `docs/contributing/failure-diagnosis.md`, which
names the formula.

### Root hub

usbport builds the root-hub PDO and hub/port descriptors from these; the
miniport only reports and acts on PORTSC state (`docs/contributing/architecture.md`).

| Callback | xHCI implementation |
|---|---|
| `RH_GetRootHubData` | Fill the root-hub data struct: NumberOfPorts = count of managed USB2 protocol ports only, hub characteristics, and `PowerOnToPowerGood = 10` (2 ms units = 20 ms per the PORTSC.PP rule) |
| `RH_GetStatus` / `RH_GetHubStatus` | Constant "hub OK" values |
| `RH_GetPortStatus` | Translate the PORTSC shadow into USB hub port-status/change bitmask format (connect, enable, reset, over-current, speed bits + change bits). Note usbport indexes ports 1..N over the *managed* set - keep a managed-index -> physical-PORTSC-index map |
| `RH_SetFeaturePortReset` | DISPATCH_LEVEL and, in the ReactOS contract, not under `MiniportSpinLock`: under a miniport-owned lock, advance the port-reset generation and mark it armed; set PORTSC.PR = 1, arm a bounded `UsbPortRequestAsyncCallback` carrying that generation, and return immediately. The PRC event and timeout callback race to claim the same armed generation; exactly one reports reset-change, reads Port Speed, and exposes it in port status. A stale or post-stop timeout returns without touching MMIO |
| `RH_SetFeaturePortPower` / `RH_ClearFeaturePortPower` | Write PORTSC.PP and return; advertise the 20 ms power-on-to-good time through `RH_GetRootHubData` so the hub stack enforces the settle interval |
| `RH_SetFeaturePortEnable` / `RH_ClearFeaturePortEnable` | xHCI cannot force-enable a USB2 port (enable comes from reset); Clear = PORTSC.PED write-1 |
| `RH_SetFeaturePortSuspend` / `RH_ClearFeaturePortSuspend` | Both are PLS writes with LWS, and they are not symmetric. Suspend is one write of U3, and only onto a port that reports enabled (PED = `1`, PLS < `3`), 4.15.1, p.254. Resume is two writes with a mandatory 20 ms of bus signalling between them: `PLS = 15` (Resume), at least T(DRSMDN), then `PLS = 0` (U0), 4.15.2.2, p.257. Writing U0 to start is the USB 3.x branch of that same section and does nothing on a USB 2.0 port while reporting success; this project shipped that once. The interval's timer is a floor, not a deadline, so no Port Status Change Event may shorten it (`docs/contributing/implementation-invariants.md`, "Root Hub Reporting"). A device-initiated resume needs the terminating write too, armed from the PLC that announces the Resume state (4.15.2.1, p.256). Low priority on Win98, which rarely drives selective suspend; reachable on Win2000, so do not stub them in a way that misreports success |
| `RH_ClearFeaturePort*Change` | Write-1-to-clear the matching PORTSC change bit and clear it in the shadow |
| `RH_DisableIrq` / `RH_EnableIrq` | Mask/unmask the port-change interrupt source. xHCI has no separate port-IRQ mask, so these are a software gate on whether a port change calls `UsbPortInvalidateRootHub`; `IMAN.IE` is never touched, because it gates the whole interrupter (`docs/usb-xhci-info/usbport-miniport-abi.md`, the `RH_DisableIrq` rows) |

When the DPC sees a Port Status Change Event: update the shadow, then call the
usbport service that tells it to re-poll the root hub
(`UsbPortInvalidateRootHub` - verify name). usbport then calls
`RH_GetPortStatus` and drives reset/enumeration.

### Debug / misc

`StartSendOnePacket` / `EndSendOnePacket` (single-packet debug path) and
`PassThru` can be failing stubs in a first spike; usbport tolerates that.

That is spike advice only. `PassThru` is now this driver's only user-facing
diagnostic channel: it is what `XHCISNAP.EXE` reaches the miniport through, on
both targets, and it ships in every flavour from `0.0.0.6`. A failing stub
there costs a Windows 98 user the whole of what they could have sent back. The
bit-exact contract is `docs/usb-xhci-info/usbport-miniport-abi.md` section 4;
the route, the wire format and the tool are
`docs/contributing/passthru-snapshot-instrument.md`.

## 4. Services usbport provides to the miniport

Written by usbport into the registration packet (verify names/membership in
usbmport.h):

- `UsbPortInvalidateRootHub`, `UsbPortInvalidateEndpoint`,
  `UsbPortCompleteTransfer`, `UsbPortCompleteIsoTransfer`.
- `UsbPortInvalidateController`: request a controller-level reset/check.
- `UsbPortRequestAsyncCallback`: uncancellable timer callbacks that run
  without usbport's miniport locks. Use copied generations and
  miniport-owned synchronization for deferred work, since the miniport must
  not create its own threads/DPCs under usbport's synchronization model.
- `UsbPortReadWriteConfigSpace`: PCI config access for quirk detection. Use
  this, not HalGetBusData, so usbport keeps ownership.
- `UsbPortWait`: millisecond wait, implemented as a thread sleep, so
  PASSIVE_LEVEL only. Fine inside `StartController`, never inside the
  DISPATCH-level callbacks; see `docs/usb-xhci-info/usbport-miniport-abi.md`
  section 6.
- `UsbPortNotifyDoubleBuffer`, `UsbPortLogEntry`, `UsbPortBugCheck`.

Synchronization model to confirm in the spike: ReactOS uses different locks
for endpoint callbacks and `InterruptDpc`, locks root-hub status queries, but
does not lock root-hub Set/Clear feature callbacks or async timer callbacks.
Do not assume usbport serializes the whole miniport interface. Until the NUSB
binary proves a narrower contract, assume DISPATCH-level callbacks can race,
protect shared state with miniport-owned interior locks, and never block
outside a documented PASSIVE_LEVEL lifecycle callback. Let the command path
be event-loop style (issue command, service completion from `InterruptDpc`),
never a busy-wait inside a callback.

## 5. Known binary facts (cross-checks)

### Extracting the record from the NUSB package (host-side, no VM needed)

Most of the table below can be filled before any VM exists: the binaries
are inside `tools/nusb33e.exe`, which is an MS CAB self-extracting archive.
Verified contents include `USBPORT.SYS`, `USBEHCI.SYS`, `USBHUB20.SYS`,
`USB2.INF`, and `_NUSB.INF` (plus OS files - see caveat below). On Windows:

```bat
rem 7-Zip opens the SFX directly; "7z l" lists, "7z x" extracts
7z x nusb33e.exe -onusb-extracted
rem record hashes and versions
certutil -hashfile nusb-extracted\USBPORT.SYS SHA256
rem file version: Explorer Properties > Details, or PowerShell:
powershell -c "(Get-Item nusb-extracted\USBPORT.SYS).VersionInfo.FileVersion"
rem exports of usbport, imports of the real miniport (MSVC6/DDK tools)
dumpbin /exports nusb-extracted\USBPORT.SYS  > usbport-exports.txt
dumpbin /imports nusb-extracted\USBEHCI.SYS  > usbehci-imports.txt
```

(macOS/Linux equivalent for listing/extraction only: `bsdtar -xf nusb33e.exe`.)

Record the results in the table below and transcribe the EHCI sections of the
extracted `USB2.INF` into `docs/contributing/build-and-test.md` "INF-Based Installation" -
that INF is the authoritative model for `src/xhci98.inf`'s `DevLoader`/
`NTMPDriver` shape and registry keys.

Two caveats:

- The package contents are what the *installer carries*, not necessarily what
  it *installs* on a given OS - `_NUSB.INF` decides. Keep the Phase 2a
  record-from-VM step as the final confirmation that the installed
  `usbport.sys` matches the packaged one (hash both).
- The 3.3 package also carries core OS files (`USER.EXE`, `USER32.DLL`,
  `SYSTRAY.EXE`, `EXPLORER.EXE`, `IOS.VXD`), and it does install them. See
  below.

The core-file copy is unconditional, per the extracted `_NUSB.INF`. The
installer (`_START.BAT` -> `advpack.dll,LaunchINFSection _nusb.inf`) runs the
single `[DefaultInstall]`, whose `CopyFiles` list includes `explorer.exe` (to
dirid 10, `%windir%`), `user.exe`/`user32.dll`/`systray.exe`/`hotplug.dll`
(dirid 11, `%windir%\SYSTEM`), and `ios.vxd` (dirid 22, VMM32 dir). There is
no OS-version or presence condition; the only guard is copy flag `0x20`
(`COPYFLG_NO_VERSION_DIALOG`) on each entry, which skips the copy only if the
file already on disk is newer than the packaged one.

The packaged versions are Win98 SE hotfix builds (per the INF's own QFE
registry entries: `user.exe`/`user32.dll` 4.10.0.2231, `systray.exe`
4.10.0.2224, `ios.vxd` 4.10.0.2225, `explorer.exe` an IE55SP2 fix), newer than
stock 4.10.2222. So on a fresh Win98 SE install NUSB 3.3 does replace these
core files with Microsoft hotfix versions. The 3.3-vs-3.6 distinction is
narrower than it first appears: 3.3 ships MS Win98 SE QFE builds of
`user.exe`/`user32.dll`/`systray.exe` (not the WinMe-derived set 3.6 is
criticized for, and no `sysdm.cpl`), but it is not core-file-clean either.

The 3.6 package was compared file for file on 2026-09-01 (`nusb36e.exe`,
992768 bytes, SHA256
`42b13ce440cc6528600a7b085226050d855c50199ecde6cbd6f31cbf83a69621`, extracted
the same way). Its `USBPORT.SYS`, `USBEHCI.SYS` and `USBHUB20.SYS` hash equal
to the 3.3 package's, so every miniport-visible fact in the table below holds
under 3.6 unchanged. The differences sit above the miniport: 3.6 adds a WinMe
USB 1.1 stack (`OPENHCI.SYS`/`UHCD.SYS`/`USBD.SYS` 4.90.3000.1, `USBHUB.SYS`
4.90.3002.1), an XP SP3 `USBCCGP.SYS` 5.1.2600.5585 (KB945436, a composite
generic parent), `USB.INF` (OHCI/UHCI bindings only; nothing in the package
matches `CC_0C0330`), and the WinMe `SYSDM.CPL`. Its `_NUSB.INF` copies all of
those unconditionally, so on a 3.6 machine the driver package's no-overwrite
`usbd.sys`/`usbhub.sys` copies are pre-empted by the WinMe pair. One VM pass
(2026-09-01, a 2a-fresh clone) ran HID and mass storage cleanly in that
configuration; 3.3 remains the version the project installs and tests
against.

A third Windows 98 stack exists and has been examined: SweetLow's (LordOfMice
on GitHub, the author of the 2007 Windows 2000 backport NUSB carries) XP
SP2-sourced rebuild. It was raised in issue #1 as the stack free of the
controller-teardown crash. The package is his `usb20_win9x.zip`
(`http://sweetlow.orgfree.com/download/usb20_win9x.zip`, 229,139 bytes,
SHA-256 `B9C06F08...`, kept as `tools/usb20_win9x.zip`): `USBPORT.SYS`,
`USBEHCI.SYS`, `USBHUB20.SYS` and `USBCCGP.SYS`, all version 5.1.2600.2180
with the resource string "built by: WinDDK" (his notes: XP post-SP1 sources
for three of them, 2003 sources for usbehci), plus `USBDSTUB.SYS` 1.00.000
("Stubs for undecorated USBD.SYS functions", CompanyName SweetLow; his notes
call it a USBD.SYS helper for the XP SP3 QFE usbccgp on 98/SE, and his INF
does not install it), his own edit of Microsoft's 2003 `USB2.INF` (no SiS
entry, no `usbd.sys` copy line), Full and Lite INF variants (usbccgp bound
to `USB\COMPOSITE` or `USB\COMPOSITE2`), NOWMI and VIA hub variants, an XP
SP3 QFE usbccgp, and two `.reg` files. Windows 98 QuickInstall's base driver
library (`https://github.com/oerg866/win98-driver-lib-base`,
`[MBD]_sweetlow_usb2.0`) ships the same five binaries byte for byte with
Microsoft's original INF plus a `usbd.sys` copy line QuickInstall added.
Files, hashes and `dumpbin` listings are in `tools/sweetlow-extracted/`
(git-ignored; its README has the fetch record). The record below has the miniport-visible
facts; the VM observations are in `docs/contributing/build-and-test.md`, "The
SweetLow stack", and `docs/contributing/lessons.md`.

The project installs NUSB 3.3 in full anyway. It is the environment end users
will run, the replacements are MS SE hotfix builds, and a pre-install VM
snapshot covers rollback (`docs/contributing/build-and-test.md`).

### Target ABI record

This record was filled before any Phase 3 source was written; the
registration-packet layout is tied to these exact binaries.

The third column is best-effort and static-only. XP is a secondary target with
no VM and no checkpoint. Small, isolated compatibility accommodations are allowed when
they do not weaken either primary target (`docs/usb-xhci-info/win98-wdm.md`, "What about
Windows XP?"); the column exists because packet-format compatibility is the
first fact needed to judge whether such accommodations are feasible.

| Item | Win98+NUSB target | Win2000 SP4 target | Windows XP SP3 (static only, non-gating) |
|---|---|---|---|
| Stack package | NUSB 3.3 English (`nusb33e.exe`) | Windows 2000 SP4 or KB319973. Recorded from the SP4-integrated install ISO (`win2ksp4.ISO`, volume `ZRMPFPP_EN`); binaries + dumps kept in `tools/win2ksp4-extracted/` | Retail XP Pro SP3 OEM ISO (`D:\isos\Win XP Pro SP3 OEM original.iso`); `I386\USBPORT.SY_` + `USBEHCI.SY_` extracted with `7z e` then `expand` into `tools/winxpsp3-extracted/` |
| Package SHA256 | `f1f30a800cf6013eb6f18db775d88e777ddc3924622e6560edd2dcd7b53698d3` (`nusb33e.exe`, 774144 bytes, copy in `tools/`) | - (files taken from `I386\USBPORT.SY_` etc. on the install ISO; the guest's installed copies match these sizes byte-for-byte) | - (ISO not hashed; it is not a project input beyond these two files) |
| `usbport.sys` version | `5.00.2195.5652` (Microsoft, "USB 1.1 & 2.0 Port Driver"). Installed value = package value: NUSB's `_NUSB.INF` `[DefaultInstall]` places `USBPORT.SYS` via a verbatim `CopyFiles` (no patching), so the installed `C:\WINDOWS\SYSTEM32\DRIVERS\USBPORT.SYS` is byte-identical to the package binary. | `5.00.2195.6681` (Microsoft, "USB 1.1 & 2.0 Port Driver"), a different build from the NUSB one, as expected: SP4 final vs the 5652 build NUSB back-ported | `5.1.2600.5512 (xpsp.080413-2108)`, a different lineage, not just a different build |
| `usbport.sys` size/timestamp | 135920 bytes; CAB file date 2002-11-07 10:42:16; PE link timestamp `3CC59E03` = 2002-04-24 (package = installed, verbatim copy) | 138288 bytes; ISO file date 2003-06-20; in-guest file date 2003-06-19 12:05; PE link timestamp `3E6CB39A` = 2003-03-10 | 143872 bytes (expanded) |
| `usbport.sys` SHA256 | `EEC79B5A4CCE9C40A7D2484AA48F0FBEE7745FC447093D840BBD51A28C3F9B55` (package = installed, verbatim copy). Optional in-VM spot-check: `certutil -hashfile C:\WINDOWS\SYSTEM32\DRIVERS\USBPORT.SYS SHA256` should equal this. | `22388C0E5ECB8840027559B1B1FBE0D7AF9F1C87741A222B3D55772ADA91B53F` (expanded from `I386\USBPORT.SY_`) | `2B269372E5B39B03089F781CC69AE519D1C840A80ADBE15EA3787FBCDE97F1A8` |
| `dumpbin /exports` saved? | Yes - `tools/nusb-extracted/usbport-exports.txt` (package = installed, verbatim copy). 3 exports: `DllUnload` (ord 1), `USBPORT_GetHciMn` (ord 2), `USBPORT_RegisterUSBPortDriver` (ord 3) | Yes - `tools/win2ksp4-extracted/usbport-exports.txt`. The same 3 exports at the same ordinals (`DllUnload` 1, `USBPORT_GetHciMn` 2, `USBPORT_RegisterUSBPortDriver` 3) | Yes - `tools/winxpsp3-extracted/usbport-exports.txt`. Same 3 exports at the same ordinals |
| `usbehci.sys` imports saved? | Yes - `tools/nusb-extracted/usbehci-imports.txt` (from package; NUSB's `usbehci.sys` is version `5.00.2195.6882`, SHA256 `E25024B003328562F89D45D96A40E3750418331358B17882EB793D5857507750`). Imports: `NTOSKRNL.EXE` (KeQuerySystemTime, WRITE_REGISTER_ULONG, READ_REGISTER_ULONG), `HAL.DLL` (KeStallExecutionProcessor), `USBPORT.SYS` (USBPORT_GetHciMn, USBPORT_RegisterUSBPortDriver) | Yes - `tools/win2ksp4-extracted/usbehci-imports.txt` (SP4 `usbehci.sys` is `5.00.2195.6709`, 19728 bytes, SHA256 `0F3980FA26DCB46A8F7DA1618C3385734346043EDB261BA77FA87D488DE1707E`). Imports: `NTOSKRNL.EXE` (KeQuerySystemTime only), `HAL.DLL` (KeStallExecutionProcessor), `USBPORT.SYS` (both exports) - same module profile, one fewer ntoskrnl import than the 9x build | Yes - `tools/winxpsp3-extracted/usbehci-imports.txt` (XP `usbehci.sys` `5.1.2600.5512`, 30208 bytes, SHA256 `90EBA8BAF45932B453D905EDF2BDDDF3A432BFD50B9F7DF58CDEAE98D11C2E2F`). Same three modules, but a wider ntoskrnl set (adds `ExAllocatePoolWithTag`, `ExFreePool`, `KeBugCheckEx`, `KeGetCurrentThread`, `KeInitializeSpinLock`, `KeTickCount`) and `Kf{Acquire,Release}SpinLock` from HAL; an XP-era miniport does more of its own work |
| Confirmed miniport version constant | Confirmed (`tools/nusb-extracted/usbport-registration-disasm.txt`). `USBPORT_RegisterUSBPortDriver` compares the `Version` argument against two immediates only: `cmp [esp+14h],64h` -> `jae`, i.e. `Version < 100` returns `STATUS_UNSUCCESSFUL` (0xC0000001), and `cmp [esp+14h],0C8h`, i.e. `Version >= 200` selects the long packet (next row). No other value is examined; there is no exact-match version constant. The shipping `usbehci.sys` passes 200 (0xC8); `xhci98.sys` does the same. | Confirmed, identical in the SP4 build (`tools/win2ksp4-extracted/usbport-registration-disasm.txt`): same `>= 100` gate, same `>= 200` size selector, its `usbehci.sys` also passes 200. | Confirmed (`tools/winxpsp3-extracted/usbport-registration-disasm.txt`): same `>= 100` gate and `>= 200` size selector; XP's `usbehci.sys` also passes 200. |
| Confirmed `USBPORT_GetHciMn` return value | `0x57324B30` ("W2K0" as bytes `30 4B 32 57`), a one-instruction `mov eax,57324B30h; ret`. This is not ReactOS's `USBPORT_HCI_MN = 0x10000001`; see the bullet below. NUSB's `usbehci.sys` `DriverEntry` opens with `call USBPORT_GetHciMn; cmp eax,57324B30h; jne -> return STATUS_UNSUCCESSFUL`. | `0x57324B30`, byte-identical instruction; SP4's `usbehci.sys` checks the same value. | `0x10000001`: the XP lineage uses ReactOS's value, and XP's `usbehci.sys` checks against `0x10000001`. The optional probe may accept this second known value as a small, isolated compatibility accommodation; see the bullet below. |
| Confirmed registration packet size | Confirmed: 316 bytes (0x13C) when `Version >= 200`, 300 bytes (0x12C) when `100 <= Version < 200`. The routine loads `ecx = 12Ch`, adds `10h` iff `Version >= 0C8h`, then `rep movs` that many bytes out of the caller's packet. Matches ReactOS's `sizeof(USBPORT_REGISTRATION_PACKET) = 0x13C`, and the 16-byte delta is the four tail fields `RH_ChirpRootPort`/`TakePortControl`/`Reserved4`/`Reserved5` at 0x12C-0x138, which is also how this build implements ReactOS's "chirp only when Version >= 200" rule. | Confirmed identical: same 0x12C / 0x13C pair, same selector. The two builds differ only in usbport's own private wrapper struct, which is not miniport-visible: NUSB allocates 0x14C (332) bytes and copies the packet to `+0x10` (it does not retain the `Version` argument), while SP4 allocates 0x150 (336) and copies to `+0x14` after storing `Version` at `+0x10`; the 336-byte/`+0x14` form is what ReactOS's `USBPORT_MINIPORT_INTERFACE` `C_ASSERT` describes. | Confirmed identical (0x12C / 0x13C, interface 0x150, packet at `+0x14`, `Version` stored at `+0x10`). The packet format is the same across all three lineages, so packet format does not rule XP out of Option A. It proves nothing about XP callback contracts or runtime behaviour. |
| Confirmed early field offsets touched | Confirmed: before the copy, the routine writes exactly 16 service pointers into the caller's packet at 0xE4, 0xE8, 0xEC, 0xF0, 0xF4, 0xF8, 0xFC, 0x100, 0x104, 0x108, 0x10C, 0x110, 0x114, 0x118, 0x11C, 0x120 and touches no other packet field. That pins the whole in/out boundary: everything below 0xE4 must be laid out as section 3 of `docs/usb-xhci-info/usbport-miniport-abi.md` says, or the service pointers land in the wrong slots. Independently corroborated by the shipping `usbehci.sys` `DriverEntry`, which writes every ReactOS "in" offset from 0x00 to 0x130 and nothing at 0x0C/0x1C/0x20/0x134/0x138 or in the 0xE4-0x120 service block (`tools/nusb-extracted/usbehci-driverentry-disasm.txt`). | Confirmed identical offsets in both the register routine and SP4's `usbehci.sys`. | Confirmed identical offsets in both. |
| Confirmed common-buffer / DMA path | Confirmed (`tools/nusb-extracted/usbport-commonbuffer-disasm.txt`). `USBPORT_StartDevice` fills a 40-byte `DEVICE_DESCRIPTION` with `Dma32BitAddresses = 1`, `DmaWidth = Width32Bits`, `Master`/`ScatterGather = 1`, `InterfaceType = PCIBus`, `MaximumLength = MAXULONG`, and never writes `Dma64BitAddresses`. `USBPORT_AllocateCommonBuffer` (VA `0002E300`) allocates `ROUND_TO_PAGES(MiniPortResourcesSize + 0x30)` with `CacheEnabled = TRUE`, publishes `StartVA`/`StartPA` both masked to page alignment, derives `StartPA` from `LogicalAddress.LowPart` only, and zeroes the block before `StartController`. `MiniPortFlags` bit `0x100` (`NO_DMA`) would skip the adapter and zero `MiniPortResourcesSize`. | Confirmed identical (`tools/win2ksp4-extracted/usbport-commonbuffer-disasm.txt`; `USBPORT_AllocateCommonBuffer` @ VA `0002EBDA`). The two routines are instruction-for-instruction the same apart from FDO-extension field offsets, so one fixed layout serves both targets. | Not read: non-gating, and the 2195.x pair already settles both primary targets. |
| Confirmed transfer SG-mapping path | Confirmed (`tools/nusb-extracted/usbport-sglist-disasm.txt`). `USBPORT_MapTransfer` @ VA `0001856A` is the `AllocateAdapterChannel` execution routine and builds every SG element through `FdoExt->DmaAdapter (+0x510) -> DmaOperations (+0x04) -> MapTransfer (ops+0x20)`. Elements are page-granular (`0x1000 - (PA.LowPart & 0xFFF)`, clamped), 24 bytes, from list offset 0x10. The returned `HighPart` is stored unmasked; it is zero because the adapter is 32-bit and because usbport advances only the LowPart without carry. | Confirmed identical (`tools/win2ksp4-extracted/usbport-sglist-disasm.txt`; `USBPORT_MapTransfer` @ VA `00018980`). 540 instructions each, differing in exactly three private structure offsets (adapter ptr `+0x510`/`+0x518`, FDO list head `+0x1C8`/`+0x1CC`, SG list at `Transfer+0x98`/`+0xC0`). | Not read: non-gating. |
| `usbport.def` / `usbport.lib` prepared? | Yes, and reproducible: generation lives in tracked sources (`scripts/usbport-lib/`) driven by `scripts/make-usbport-lib.cmd`, which writes `src/usbport.lib` for `TARGETLIBS`. Still the stdcall stub-DLL method, not plain `lib /def:`; see `docs/contributing/build-and-test.md` "Build Files". The tracked manifest records the two exact imports verified against the package binary, so a fresh checkout needs no ignored reference binary; supplying one adds an independent exact-name export check. Package = installed (verbatim `CopyFiles`); the package binary re-hashed to the SHA256 in the row above. | Same lib serves this target: export names and ordinals are identical (row above, re-dumped), and the lib's content derives from the stubs, not from the reference binary. `make-usbport-lib.cmd tools\win2ksp4-extracted\USBPORT.SYS` performs the optional binary check and produces the same symbol set. | Same lib would serve XP too: identical export names and ordinals. Non-gating; no XP build is attempted. |

#### The SweetLow rebuild (Windows 98, XP lineage)

A fourth `usbport.sys` the driver has now run under, kept in its own table
because it is a Windows 98 stack of the XP lineage and so fits neither
column above. Method per row: static means read from the `dumpbin` listings in
`tools/sweetlow-extracted/`; runtime means observed through this driver's own
trace in the `2a-sweetlow` guest on 2026-09-02.

| Item | SweetLow rebuild |
|---|---|
| Stack package | `usb20_win9x.zip` from SweetLow's own site (see the paragraph above); the same binaries are in `oerg866/win98-driver-lib-base` `[MBD]_sweetlow_usb2.0` at commit `5ef7f88e`, which is where they were first fetched on 2026-09-02; files in `tools/sweetlow-extracted/` with the README recording URLs and hashes |
| `usbport.sys` version | `5.1.2600.2180 built by: WinDDK`, CompanyName "Windows (R) 2000 DDK provider", "USB 1.1 & 2.0 Port Driver": XP SP2's source level rebuilt with the DDK, not Microsoft's shipping binary (XP SP2's own is 143,872 bytes) |
| `usbport.sys` size / SHA256 | 134,912 bytes; `8A3C9F1B568CB25CF5DD9AF3AF9E5C3400DE24BD087CAA3E4E3345588F5CFB56` |
| Companions | `USBEHCI.SYS` 20,224 B `7BE8F4AD...`, `USBHUB20.SYS` 50,560 B `01A83E76...`, `USBCCGP.SYS` 27,776 B `683061AF...`, all 5.1.2600.2180 WinDDK builds; `USBDSTUB.SYS` 5,376 B `DCF7E861...`; `USB2.INF` 4,470 B `305F6133...` (full hashes in the README) |
| `dumpbin /exports` | `tools/sweetlow-extracted/usbport-exports.txt`: the same 3 exports at the same ordinals (`DllUnload` 1, `USBPORT_GetHciMn` 2, `USBPORT_RegisterUSBPortDriver` 3). Static |
| `usbport.sys` imports | `NTOSKRNL.EXE` only (`usbport-imports.txt`); no `USBD.SYS` import, so nothing in the port driver itself needs the stub. `usbhub20.sys` imports `ntoskrnl.exe`, `WMILIB.SYS` and `USBD.SYS` (`_USBD_CreateConfigurationRequestEx@8` and friends, decorated); `USBDSTUB.SYS` imports the same three decorated `USBD.SYS` names plus PnP/power entry points and exports nothing. Static |
| `usbehci.sys` imports | `usbehci-imports.txt`: `NTOSKRNL.EXE` (KeQuerySystemTime, READ_REGISTER_ULONG, WRITE_REGISTER_ULONG), `HAL.DLL` (KeStallExecutionProcessor), `USBPORT.SYS` (both exports); the NUSB 9x profile exactly, not the wider XP SP3 one. Static |
| Miniport version constant | `usbport-registration-disasm.txt`, `USBPORT_RegisterUSBPortDriver` @ VA `00027632`: `cmp eax,64h` / `jae` (below 100 returns `0xC0000001`), then `cmp eax,0C8h` selecting the packet length. The same two immediates, no exact-match constant. Static; runtime corroborated by `USBPORT_RegisterUSBPortDriver status=00000000` with `Version = 200` |
| `USBPORT_GetHciMn` | VA `000271E6`: `mov eax,10000001h; ret`. The XP lineage's value, as `src/xhci_usbport.h` already accepts. Both: the trace reads `USBPORT_GetHciMn=10000001` |
| Registration packet size | `mov ecx,12Ch`, `add ecx,10h` when `Version >= 0C8h`, then `rep movs`: 300 / 316 bytes. Both: `packet size=0000013C` in the trace |
| Wrapper layout | Allocates 0x150 with `push 150h` (pool tag `usbp`), zeroes 0x54 dwords, stores `Version` at `+0x10`, copies the packet to `+0x14`: the Win2000 SP4 / XP form, not NUSB's `+0x10` form. Static |
| Service pointers written before the copy | The 16 slots 0xE4..0x120, and nothing else in the packet (`mov dword ptr [esi+E4h]` .. `[esi+120h]` in the listing). Static |
| Registry value names present | `DisableSelectiveSuspend`, `UsbBIOSx`, `DisableCcDetect` in one UTF-16 cluster with `usb` (offsets 0xD0A..0xD56), the same Services\USB query table shape NUSB's build has at 0x1D52..0x1D9E; `HcDisableSelectiveSuspend` separately; no `EnIdleEndpointSupport` (the XP SP3 binary has it, this SP2-level build does not). Both: with the value deleted the stack idle-suspended the controller shortly after start and a later hot-plug was invisible; with it present neither happened (`docs/contributing/build-and-test.md`, "The SweetLow stack") |
| What it needs beside it | `usbd.sys`: required, `usbhub20.sys` imports it by name and the root hub is Code 2 without it (runtime, 2026-09-02); `USBDSTUB.SYS` is not a substitute and his INF does not install it. `usbhub.sys`: not required, composites are parented by his `usbccgp.sys` via the Full INF's `USB\COMPOSITE` binding (runtime, a two-interface `usb-audio`), and it still is when `usbhub.sys` is present, so the package's copy is inert under his stack. `docs/contributing/build-and-test.md`, "The SweetLow stack" |
| Common buffer, SG mapping | Not read. The driver's runtime behaviour under it (below) is the only evidence, and it is consistent with the 32-bit page-granular path both 2195.x builds have |
| Runtime, `2a-sweetlow` guest, 2026-09-02 | Registration, `StartController` and the No Op self-test clean; boot-attached HS mouse bound; hot-plugged HS `usb-storage` addressed (SET_ADDRESS interception), bulk pair opened, mounted as a removable disk; then Device Manager disable, re-enable, Remove and Refresh-plus-reinstall each completed: `DisableInterrupts`, `StopController(TRUE)`, eight ports unpowered, halted at `USBSTS=1`, `StartController` on the same extension after re-enable, a fresh `DriverEntry` after reinstall. QEMU only, `pc,smm=off` (see lessons), no matrix run, no bare metal |

- MS `usbport.sys` exports two symbols relevant here:
  `USBPORT_RegisterUSBPortDriver` and `USBPORT_GetHciMn`. Confirmed by
  `dumpbin /exports` on the packaged NUSB binary: both are present, plus a
  third export `DllUnload` (ordinal 1). `DllUnload` is not part of the
  miniport contract and nothing imports it, but the `.def` written for the
  import library should be derived from the saved dump, not from a two-name
  assumption. The import library in `docs/contributing/build-and-test.md`
  is generated from that list.
- `USBPORT_GetHciMn` does not return `0x10000001` on either shipping
  target. Both 2195.x binaries return `0x57324B30` ("W2K0"); only the XP
  `5.1.2600.x` binary returns `0x10000001`, which is the value ReactOS
  transcribed. Each in-box `usbehci.sys` hard-codes its own lineage's
  constant and returns `STATUS_UNSUCCESSFUL` on mismatch. So the
  ReactOS-style probe is still the right shape for `xhci98.sys`
  `DriverEntry`: it is the cheapest proof that the import really resolved to
  a known usbport lineage. If retained, the probe accepts `0x57324B30` for
  both primary targets and `0x10000001` for XP, rejecting unknown values.
  The second comparison is a small, isolated compatibility accommodation
  with no effect on either primary path. Hard-coding ReactOS's single value
  would abort `DriverEntry` on both primary targets.
- NUSB's `usbehci.sys` imports from three modules, not two: besides
  `ntoskrnl.exe` and `usbport.sys` it imports `KeStallExecutionProcessor`
  from `HAL.DLL` (`dumpbin /imports` on the packaged binary). So a HAL
  import for busy-wait stalls is part of the real miniport profile. If
  `xhci98.sys` pulls in HAL or other imports for things usbport provides
  (mapping, interrupts, common buffer), something is being done the
  non-miniport way, but `KeStallExecutionProcessor` itself is fine.
- Community-pinned flaky threshold: usbport builds newer than 5.0.2195.5652
  misbehave on 9x (`docs/usb-xhci-info/win98-wdm.md`). The packaged NUSB 3.3
  binary is `5.00.2195.5652`, at the threshold and not above it, so the
  package passes this sanity check. The Win2000 target runs
  `5.00.2195.6681`, well above the threshold, which is fine there: the
  threshold is a 9x-compatibility claim, not an ABI claim.
- Comparing the two targets: they differ in build but agree
  everywhere the miniport contract is visible from outside. Same export set
  and ordinals, and the same `usbehci.sys` import profile. That is the
  strongest static evidence available that one `xhci98.sys` can load on
  both. One trap: `0x10000001` does appear in both binaries, but not in
  `USBPORT_RegisterUSBPortDriver`. It sits in usbport's USBUSER request
  dispatcher, where it is `USBUSER_OP_SEND_ONE_PACKET`
  (`USBUSER_OP_MASK_DEVONLY_API | 1`, the `usbuser.h` code behind the
  `StartSendOnePacket`/`EndSendOnePacket` debug path), reached through a
  jump table over request codes 1-8. The genuine version gate is the
  `>= 100` / `>= 200` pair recorded in the table above, and the packet size
  is 316 bytes, identical in both builds.
- Ground truth from the shipping miniports. Both
  `usbehci.sys` builds fill their static packet with: `MiniPortVersion = 3`
  (`USB_MINIPORT_VERSION_EHCI`), `MiniPortBusBandwidth = 0x61A80` (400000),
  `MiniPortExtensionSize = 0x184`, `MiniPortEndpointSize = 0xA0`,
  `MiniPortTransferSize = 0x34`, `MiniPortResourcesSize = 0x2C800` (182272),
  and pass `Version = 200`.
  - `MiniPortFlags = 0x95` = `INTERRUPT | MEMORY_IO | USB2 | POLLING`,
    without `WAKE_SUPPORT` (0x200), which ReactOS sets and which XP's
    `usbehci.sys` does set (`0x295`).
  - Offset 0x8C (`ResetController`) is left NULL by every shipping
    `usbehci.sys`; that confirms the field position, but does not establish
    whether usbport null-checks it, gates it for EHCI, or never reaches that
    path. Keep the call contract open and supply the callback in
    `xhci98.sys`.
  - `DriverEntry` also zeroes `DriverObject->DriverUnload` immediately
    before registering.
  - These are the numbers `src/xhci_dispatch.c` should be shaped against;
    the size values themselves are EHCI's, not ours.

### Observed callback sequence (Win98, Phase 3 task 8)

Measured on the Phase 2a VM (Win98 SE + NUSB 3.3, `qemu-xhci`), from
the Win98 launcher's `vm\win98-debugcon.log` of that session (the trace itself
was not kept; the table is the record). This is the first *live* record in this
file; every row above it is static. The `a=` argument is the miniport extension pointer.

| Stage | Calls, in order | IRQL |
|---|---|---|
| Registration | `USBPORT_GetHciMn` -> `USBPORT_RegisterUSBPortDriver` = `STATUS_SUCCESS`, 16 service pointers written at 0xE4-0x120 | PASSIVE |
| Bind | `StartController` (resources: `StartVA` and `StartPA` page-aligned, `MiniPortResourcesSize` honoured in full) | 0 |
| | `EnableInterrupts`, after `StartController` returns, as the ABI doc predicts | 2 |
| | `CheckController` / `PollController`, repeating | 2 |
| Root hub | `RH_GetRootHubData` | 0 |
| | `RH_GetPortStatus` | 0, later 2 |
| | `RH_GetStatus` | 2 |
| | `RH_SetFeaturePortPower` | 2 |
| | `RH_ClearFeaturePortConnectChange` | 2 |
| | `RH_GetHubStatus` + `RH_EnableIrq`, paired and repeating | 2 |
| Idle | `SuspendController` -> `ResumeController` pairs, repeating | 0 |
| Shutdown | `SuspendController` -> `DisableInterrupts` -> `StopController(TRUE)` | 0 / 2 / 0 |
| Disable | ... `RH_ClearFeaturePortEnable` -> `RH_DisableIrq`, then the VM bugchecks | 2 / 0 |

Notes that change nothing in the contract but are worth having on record:

- `StopController`'s `IsDoDisableInterrupts` argument was TRUE in the
  shutdown path, matching `power.c:194` in the mirror.
- The endpoint and transfer families were never reached, as designed: the
  synthetic root-hub port never reports a connection, and usbport services the
  root hub's own endpoint internally.
- `RH_ChirpRootPort`, `ResetController`, `PassThru`, `RebalanceEndpoint`,
  `StartSendOnePacket`/`EndSendOnePacket`, `InterruptNextSOF` and
  `TakePortControl` were not called in any run. Their slots stay filled;
  an unreached slot proves nothing about its offset.
- The disable/re-enable bugcheck is not a miniport fault: the shipping
  `usbehci.sys` bugchecks at the identical address on the same VM. See
  `docs/contributing/lessons.md`, "Option A works on Win98". The Win2000 run
  below settles it: the same binary walks past the point Win98 dies at.

### Observed callback sequence (Win2000 SP4, Phase 3 task 9)

Measured on the Phase 2b VM (Win2000 SP4, native `usbport.sys`
5.00.2195.6681, Standard-PC HAL, `qemu-xhci` at PCI 0:4.0, IRQ 11, BAR0
`FEBF0000-FEBF3FFF`), from the task 9 debugcon trace (`win2k-debugcon.task9.log`,
not kept; the table is the record). The binary is the
byte-identical debug `xhci98.sys` task 8 ran on Win98
(SHA-256 `4593D236094B39D0FD5D6887314248F82807F4DB8A16B5C2FEA0500797713BE3`),
installed through the same INF's `.NTx86` path.

| | Win98 SE + NUSB (task 8) | Win2000 SP4 (task 9) |
|---|---|---|
| `USBPORT_GetHciMn` | `57324B30` | `57324B30` |
| Packet accepted | 316 B (0x13C) at `Version 200` | same |
| `USBPORT_RegisterUSBPortDriver` | `STATUS_SUCCESS` | `STATUS_SUCCESS` |
| Service pointers written at 0xE4-0x120 | 16 | 16 |
| Canary / slot / ESP checks | no `ABI-SUSPECT` | no `ABI-SUSPECT` |
| `StartController` IRQL | 0 | 0 |
| `StartVA` / `StartPA` | `D2EEA000` / `0FF00000` | `815B3000` / `015B3000`, and `81549000` / `01549000` on a later bind |
| Common buffer granted | 376,832 B (the size declared at the time; see below) | 376,832 B |
| Bind sequence | `StartController` -> `EnableInterrupts` -> `CheckController`/`PollController` -> `RH_GetRootHubData` -> `RH_GetPortStatus` -> `RH_GetStatus` -> `RH_SetFeaturePortPower` -> `RH_ClearFeaturePortConnectChange` -> `RH_GetHubStatus`/`RH_EnableIrq` | identical, same IRQLs |
| Teardown (disable, uninstall) | `RH_ClearFeaturePortEnable` -> `RH_DisableIrq`, then bugcheck | `RH_ClearFeaturePortEnable`(2) -> `RH_DisableIrq`(0) -> `DisableInterrupts`(2) -> `StopController(TRUE)`(0), no crash |
| Shutdown | `SuspendController` -> `DisableInterrupts` -> `StopController(TRUE)` | same, plus a trailing second `DisableInterrupts`(2) |
| Idle | repeating `SuspendController`/`ResumeController` pairs | none observed in ~1 h |

The common-buffer row is the size the driver declared when this was measured.
It now declares `MiniPortResourcesSize` = 409,600 B (design doc 04 section
3.6), and the allocation has not been re-observed at that size on either
target.

Where the two targets differ, and what each difference means:

- Win2000 unloads the image on disable and reloads it on enable. One boot
  produced three complete `DriverEntry` -> `USBPORT_GetHciMn` ->
  `USBPORT_RegisterUSBPortDriver` cycles (install, re-enable, re-detect after
  uninstall), each returning `STATUS_SUCCESS` with the same 16 service
  pointers. A second registration by the same image is accepted; nothing in
  `DriverEntry` may assume it runs once per boot.
- The common buffer is not at a fixed address. Two binds got `815B3000`,
  the third `81549000`. On Win2000 `StartVA - StartPA` is exactly `0x80000000`
  (the kernel identity map); on Win98 the two were unrelated. Only page
  alignment and the sub-4 GB bound may be relied on; both held everywhere.
- `RH_DisableIrq` arrives at IRQL 0 on the disable path here while its
  neighbours arrive at 2, so that slot must not assume DISPATCH_LEVEL.
- No idle `SuspendController`/`ResumeController` cycling. Native usbport did
  not idle-suspend this controller at all in the observation window, so Win98's
  repeating pairs are an NUSB/Win98 behaviour, not a contract.

`USBPORT_RESOURCES` confirmed live (ABI doc section 9, item 4). The raw 52
bytes `StartController` received decode field-for-field against section 5 of
`docs/usb-xhci-info/usbport-miniport-abi.md`:

| Offset | Field | Value | Reading |
|---|---|---|---|
| 0x00 | `ResourcesTypes` | `00000006` | MEMORY + INTERRUPT, no PORT |
| 0x04 | `HcFlavor` | `000003E8` | 1000 = EHCI generic, assigned from our declared `MiniPortVersion = 3` |
| 0x08 | `InterruptVector` | `0000003B` | 59, the Standard-PC HAL's vector for IRQ 11 |
| 0x0C | `InterruptLevel` | `00000010` | 16 |
| 0x10 | `InterruptAffinity` | `00000001` | uniprocessor VM |
| 0x14 | `ShareVector` | `00000001` | TRUE - PCI INTx is shared |
| 0x18 | `InterruptMode` | `00000000` | LevelSensitive |
| 0x20 | `ResourceBase` | `BF3D5000` | mapped BAR0 VA (`BF305000`/`BF76F000` on later binds) |
| 0x24 | `IoSpaceLength` | `00004000` | 16 KB, matching Device Manager's `FEBF0000-FEBF3FFF` |
| 0x28 / 0x2C | `StartVA` / `StartPA` | see table above | page-aligned, below 4 GB |
| 0x30 | `LegacySupport` / `IsChirpHandled` | `00000000` | neither set |

Registry cross-check, which Win98 could not supply (no EHCI on that VM): the
service key the `.NTx86` path creates is value-for-value what Microsoft's own
miniport uses - `Type=1`, `Start=3`, `ErrorControl=1`, `Group=Base`,
`ImagePath=system32\DRIVERS\xhci98.sys` - differing from `usbehci` only in the
system-assigned `Tag` (17 vs 15). The devnode carries `Service=xhci98`,
`Class=USB`, `ClassGUID={36FC9E60-C465-11CF-8056-444553540000}`,
`DeviceDesc=xHCI USB 2.0 Host Controller (xhci98)`.

Still unreached here, as on Win98: the endpoint and transfer families,
`RH_ChirpRootPort`, `ResetController`, `PassThru`, `RebalanceEndpoint`,
`StartSendOnePacket`/`EndSendOnePacket`, `InterruptNextSOF` and
`TakePortControl`. `InterruptService` was never entered either - the trace's
ISR and DPC counters stayed at zero across every bind, which is expected while
the driver never enables an interrupt source and returns FALSE from the shared
line.

### What Phase 3 can and cannot prove about transfer mapping

Phase 3 never reached `SubmitTransfer` on either target. The spike reports one
synthetic, permanently disconnected root-hub port, so no external device is
ever enumerated and no endpoint beyond the root hub's internally-serviced EP0
is ever opened (both observed sequences above record the transfer family as
unreached). Everything below is therefore static, read out of the two shipping
binaries, with the boundary between proven and open drawn explicitly.

Proven statically, both builds, and safe to design against:

| Fact | Where |
|---|---|
| The SG elements come from the NT DMA adapter: `FdoExt->DmaAdapter` -> `Adapter->DmaOperations` (`+0x04`) -> `MapTransfer` (`ops+0x20`), called with the full six-argument NT signature from `USBPORT_MapTransfer`, the `AllocateAdapterChannel` execution routine | `docs/usb-xhci-info/usbport-miniport-abi.md` section 5, `USBPORT_SCATTER_GATHER_LIST`; `tools/{nusb,win2ksp4}-extracted/usbport-sglist-disasm.txt` |
| There is no second producer: `DMA_OPERATIONS+0x20` is reached from exactly one place in each image, and the 24-byte-strided element accesses are this writer, the split-transfer builder, and three readers - no `MmGetPhysicalAddress` path | same |
| Elements are page-granular - `0x1000 - (PA.LowPart & 0xFFF)`, clamped to the remaining length - so worst case is `ceil(N / 4096) + 1` elements | same |
| The header/element layout the miniport will index (header 0x10 bytes, elements 24 bytes from 0x10) | same |
| The high DWORD is expected to be zero because the adapter is 32-bit. usbport stores the HAL's returned high DWORD unmasked; its LowPart-only advance and split-child copy paths rely on the adapter contract rather than enforcing it | same, plus section 5's `DEVICE_DESCRIPTION` block |
| The two builds' mapping code is the same 540 instructions apart from three private structure offsets, so one implementation serves both targets | same |

Not proven by the static read:

- Element ordering. `SgOffset` is written per element and is the element's
  byte offset within the whole transfer buffer. The disassembly shows it
  increasing monotonically within one map round, but a transfer that needs
  several `AllocateAdapterChannel` rounds, and any bounce/map-register mapping
  (which `SgList->Flags` bit 0 flags), are not settled by reading the code.
  Order TRBs by `SgOffset`; do not assume list order equals buffer order.
- Anything else about `SubmitTransfer`: which fields of
  `USBPORT_TRANSFER_PARAMETERS` are populated for each transfer type, split/TT
  behaviour, completion accounting, and the iso variant. The registration-only
  spike says nothing about any of it. (The arguments as passed are settled:
  the call site was read out of both builds,
  `docs/usb-xhci-info/usbport-miniport-abi.md` section 4.)
- Whether every transfer arrives with an SG list at all. Answered statically,
  and the answer is both halves: the list pointer is
  `&Transfer->SgList`, computed with `lea`, so every transfer arrives with one
  and it is never NULL, but a transfer whose `TransferBufferLength` is 0 never
  reaches `USBPORT_MapTransfer`, so it arrives with `SgElementCount = 0`. That
  is the normal case for zero-length and control-status stages, not an
  anomaly to log and move on from.

The obligation this leaves: whenever `SubmitTransfer` is reached, log the
`SgElementCount`, and each element's `SgPhysicalAddress` (both DWORDs),
`SgTransferLength` and `SgOffset`, for a multi-page transfer on both targets,
and add the observation to the Target ABI record above. Until a multi-page
log exists, `xhci98.sys` must check the high DWORD is zero rather than assume
it (`docs/contributing/implementation-invariants.md`, "DMA Addresses").

### The probe that discharges it, and how to read its log

`src/xhci_probe.c` implements the obligation, wired into the
registration-packet wrappers in `src/xhci_dispatch.c`. It is instrumentation:
nothing in the driver branches on anything it produces, and every classification
it makes is also compiled into the release build, where the counters in
`XHCI_EXTENSION` are the reading and the trace lines do not exist.

Three shapes of line, and all three come from the `qemu` flavour alone:
build it with `scripts\build-driver.cmd qemu` and run it in the emulator. The
shipping `debug` build prints nothing as it runs, so pointing this section at
it yields no log at all. What a published build gives instead is the counters
below, read back with `XHCISNAP`:

| Line | Carries |
|---|---|
| `cb probe.xfer irql=.. a=<seq> b=<shape> c=<new bits>` | one `SubmitTransfer` whose shape or setup packet had not been seen since this `StartController`. `irql` is the callback's, the other half of the ordering question |
| `probe.xfer params+..`, `probe.xfer sg head+..`, `probe.xfer sg elem+..` | the raw `USBPORT_TRANSFER_PARAMETERS` (7 words, so the 8 setup bytes are in it), the SG list header (`Flags`, `CurrentVa`, `MappedSystemVa`, `SgElementCount`), and up to 8 elements at 6 words each, the dump this section asks for, printed rather than summarised so the record above can be updated from the log itself |
| `cb probe.ep ...` + `probe.ep props+..` | one endpoint-family callback and the whole 64-byte `USBPORT_ENDPOINT_PROPERTIES` block, which is design doc 02 section 4's "does any field carry hub address, port or parent speed" |

`MappedSystemVa` is populated, and the hub topology graph depends on it. The `probe.xfer
sg head` line's third word is the SG list's `MappedSystemVa`, and on both targets
it is non-NULL and equal to `CurrentVa` for a single-element list - `81624C08` on
Windows 2000 SP4 and `C14F2250` on Win98+NUSB, both for the 71-byte
`GET_DESCRIPTOR(Hub)` (batch 7b-V.0 run 1 debugcon traces on both targets, since
discarded). That is the channel the hub topology
graph reads a reply's bytes through, at completion and before
`UsbPortCompleteTransfer`; usbport unmaps only after that call returns, so the
window is the callback's own.

The announcement gate is semantic, not budgeted, and that is what makes the
log readable rather than absent. Every `XHCI_DBG_*` macro caps at a per-site
driver-image static that no start, stop or resume resets, so a per-transfer line
would be silent long before an operator plugged anything in. The probe announces
only a *property* it has not seen since the current start - a shape bit, or a new
`(bmRequestType, bRequest, wValue>>8)` key - so a bus moving a million transfers
prints exactly as much as one moving thirty, and a log spanning several starts
re-announces once per start because usbport zeroes the extension before each.

What to read out of a run, and the expected values. The `CheckController`
block prints all of these every 500 ms, change-gated:

| Counter | Expected in a QEMU run | What a different value means |
|---|---|---|
| `probe transfer shape` | `SG_EMPTY`, `SG_SINGLE`, `SG_TILES`, `SG_UNMAPPED`, both direction bits. `SG_MULTI` and `SG_ASCENDING` quite possibly absent | see the note below; their absence is a statement about the traffic, not about the probe |
| `probe max SG elements` | 0 or 1 on the enumeration path | this is the counter that says whether the ordering question was askable in the run at all |
| `probe SG lists out of SgOffset order` | 0 | nonzero settles the open question the other way: ordering by `SgOffset` stops being defensive and becomes required. But read it beside `probe max SG elements`: a zero here with a max of 1 means nothing was measured |
| `probe SG elements above 4 GB` | 0 | nonzero means the adapter's declared width is not enforced at the element and the driver's high-DWORD check is doing real work |
| `probe bounce-mapped transfers` | 0 or nonzero, both informative | `SgList->Flags` bit 0; the case the disassembly could not settle ordering for, so a nonzero count beside a zero disorder count is itself the answer |
| `probe class setups (any recipient)` | nonzero as soon as any class driver binds | not a topology reading at all; the first Win2000 run read eleven of these from the USB audio class driver on a bus with no hub. It is here as the denominator for the row below |
| `probe hub-class setups` | 0, for a measured reason | only class requests whose recipient is a device or a port, which is what design doc 02's snooping reads (`0xA0` GET_DESCRIPTOR(Hub), `0x23` SET_FEATURE(PORT_RESET)). QEMU's only hub model is USB 1.1, which is the topology usbport's own `GetTt` defect bugchecks on, so no hub can be on the bus to produce one. The recipient split matters: a counter that tests the request type alone counts every class driver's traffic, which the first Win2000 device binding a driver shows within seconds. Phase 7b's feasibility gate gets a negative from the VMs and owes the positive to hardware |
| `probe endpoints with a TT` | 0 | no device on a root port has a transaction translator |
| `probe CloseEndpoint calls`, `probe GetEndpointState calls` | 0 | nonzero refutes the both-binaries negative in the ABI doc, and the two callbacks that were stubbed on the strength of it need implementations |
| `probe setup keys dropped` | 0 for one or two devices | the key set holds 16; a nonzero value says the log is a sample rather than the whole traffic, which is a caveat on the reading and not a defect |

The ordering question may come back unmeasured, and that is a legitimate
result to record rather than a failed run. Enumeration traffic goes through
the default control pipe, which usbport caps at `0x1000`, and every
descriptor an enumeration reads is far smaller than a page, so a single
scatter/gather element is the expected shape and a multi-element list may never
occur.

The probe therefore refuses to answer the ordering question for a one-element
list at all. It sets neither `SG_ASCENDING` nor `SG_DISORDERED`, because one
element trivially ascends and a bit set on that basis would put "ordering
confirmed" into the shape word of a run that never saw two elements.
`probe max SG elements` is the counter to read first: while it is 1, `probe SG
lists out of SgOffset order` being 0 says nothing.

The question becomes askable only with bulk traffic (Phase 8), where buffers are big enough
to be mapped in pieces, and this probe stays in the binary for that. Interrupt
traffic does not get there: an 8-byte HID report gives usbport's mapper
nothing to split, and `probe max SG elements` read 1 on all three guests in
batch 7a-V.

## 6. Validation procedure (Phase 3 spike, step by step)

1. In the Phase 2a VM, record `usbport.sys` version; copy the binary out.
2. `dumpbin /exports usbport.sys` -> write `usbport.def` -> build `usbport.lib` via the stdcall stub-DLL method (details in `docs/contributing/build-and-test.md` "Build Files"). Verified: plain `lib /def:` emits cdecl-style `_Name` symbols that cannot resolve the `NTAPI` (`_Name@N`) references a correctly-prototyped miniport generates - the lib must come from `link /DLL` over stdcall stubs with a plain-name `.def`.
3. `dumpbin /imports usbehci.sys` -> confirm the real miniport's import set (expect the two usbport exports).
4. From ReactOS `usbmport.h`, transcribe the packet struct and version constant into `src/xhci_usbport.h` (independently written, section 1 license note). Done: that header declares the packet, the support structures, and every callback signature, with C89 compile-time asserts for the sizes and the group boundaries; `test/test_packet.c` re-transcribes the offset table by hand and checks each field. The NT enums and `PHYSICAL_ADDRESS` are substituted with ULONG / Lo-Hi pairs per `AGENTS.md`, which is also what lets the header compile on the build host with no DDK.
5. Confirm the transcription against the binaries before building anything (`docs/contributing/roadmap.md` Phase 3, task 1). `dumpbin /disasm` `USBPORT_RegisterUSBPortDriver` in both shipping builds and read out: the accepted version constant, the packet size the routine copies, and the first field offsets it touches; confirm `USBPORT_GetHciMn`'s returned constant while there.

   Fill the "Confirmed" rows of the Target ABI record above; this has to happen before step 6, since a wrong size or a shifted early field otherwise surfaces as an unexplained registration failure or garbage callback arguments. Do the XP `5.1.2600.x` binary in the same pass if one is available (best-effort third column, blocks nothing; the values must be read out rather than diffed, since XP is a different build lineage).

   Result: all four "Confirmed" rows are filled, the XP column was obtained, and the extracted routines are kept as `tools/{nusb,win2ksp4,winxpsp3}-extracted/usbport-registration-disasm.txt` plus the corresponding `usbehci-driverentry-disasm.txt`. Two corrections came out of it, the `USBPORT_GetHciMn` magic and the `0x10000001` misattribution, both recorded above.
6. Build the stub: `DriverEntry` fills sizes + version + flags, points every callback at a logging stub that prints its name and first arguments, calls `USBPORT_RegisterUSBPortDriver`, logs the return NTSTATUS. Done, in `src/xhci_dispatch.c`; both debug and release DDK builds link. The trace goes to DbgPrint and to the QEMU port-0xE9 console at once (`src/xhci_dbg.c`), each site prints its first few calls and then goes quiet, and no callback touches MMIO in this phase.

   The stub also verifies the registration itself: all five `Reserved` fields carry the canaries `0xC0DE0001`-`0xC0DE0005`, and after the call the driver re-checks them, counts the 16 service pointers usbport must have written at 0xE4-0x120, confirms its own callback slots are unchanged, and under `XHCI_DBG_TRACE` (the `qemu` flavour) compares ESP either side of the call (a calling-convention mismatch is the failure the stdcall stub library exists to prevent). Any of those failing logs `ABI-SUSPECT` but still returns `STATUS_SUCCESS`: usbport has already linked this driver object into its global miniport list, so failing `DriverEntry` at that point would unload the image out from under live pointers and turn a diagnosable mismatch into an unrelated crash.

   Three of the spike's stubs are shaped by usbport rather than inert, and the shape is worth keeping on record. It reports one synthetic, permanently disconnected port, because usbport cannot represent a zero-port root hub (its descriptor path subtracts one from `NumberOfPorts` while sizing the removable/power masks, `usbport-miniport-abi.md` section 9), and usbport never calls `OpenEndpoint` for the root hub's own endpoint, which it services internally (`endpoint.c:1026-1084`), so the endpoint and transfer slots stay unreachable but are still filled, since a NULL slot and a wrong layout are indistinguishable once usbport jumps through one.

   `InterruptService` always returns FALSE, because interrupts are never enabled and anything on the shared line belongs to another device (on the 2b VM, EHCI). `Get32BitFrameNumber` returns an ever-increasing counter rather than a constant, because usbport waits for that number to advance before confirming some state transitions.
7. Install via the INF on Win98+NUSB. Observe:
   - Registration returns non-success -> version constant or packet size wrong. Try the other version constant(s) before concluding mismatch.
   - Registration succeeds but callbacks arrive with garbage arguments or in impossible order -> field-order mismatch past the early offsets step 5 confirmed. Go back to the disassembly and walk further into the routine; diff the offsets it reads against the ReactOS ordering.
   - `StartController` arrives with plausible resources -> Option A is real; log everything it passes (this pins the resources-struct layout).
8. Repeat on the Win2000 SP4 VM to separate NUSB-adaptation failures from miniport bugs (`docs/contributing/roadmap.md` Phase 3, task 8).
9. Record every confirmed offset/signature in this file, replacing the
   "verify" marks; this doc is the ABI logbook after the spike, and those
   confirmed facts become the contract `src/xhci_dispatch.c` is written
   against. If the ABI cannot be matched after the disassembly step, invoke
   the Option B fallback decision (`docs/usb-xhci-info/win98-wdm.md`).

## 7. Spike exit criteria recap

Go: stub registers, controller binds in Device Manager, lifecycle callbacks
logged on Win98+NUSB. No-go: registration or callback ABI cannot be matched
even after binary disassembly -> switch to Option B (monolithic HCD,
`docs/contributing/architecture.md` "Fallback"). Either way, update this doc with what was
learned before writing further code.

## 8. Defects measured in the stack this driver plugs into

Option A reuses a shipping stack, so some of what a user meets through
`xhci98.sys` is not in `xhci98.sys`. This is the index of the ones this
project has measured, with where each full record lives; every row states how
the attribution was established, because "it is not ours" is a claim that needs
a control and not a feeling. Nothing here is a defect report against NUSB or
Microsoft. Each is a behaviour of the shipped binaries that this driver has
to live with, and two of the five this driver already works around.

| Defect | Where it bites | How it was attributed elsewhere | Full record |
|---|---|---|---|
| The controller teardown faults on Win98 + NUSB: stopping a running USB host controller bugchecks the machine (`fatal exception 0E at 0028:C00312EE`), which takes disable, uninstall, in-place upgrade and rollback with it. The uninstall does not commit; the upgrade commits its file copy and loses its registry phase. | Win98 SE + NUSB 3.3, VM. Never reproduced or excluded on real hardware. | Reproduced at the identical address with Microsoft's own `usbehci.sys` on the same guest, and on a build of ours asking 4 KB instead of 372 KB of controller memory. Win2000 SP4 runs the byte-identical binary through all four transitions cleanly. | `lessons.md`, "Option A works on Win98" and "The same binary passes on Win2000 SP4"; `run-11v.md` stage B (which operations reach it, and the no-crash uninstall route); `build-and-test.md` "Do not disable the controller in Device Manager"; user-facing in `docs/using/release-notes.md` limitation 2 |
| `USBPORT_GetTt` `CONTAINING_RECORD`s an empty `TtList` on the `TtCount <= 1` branch, yielding `0xFFFFFFEC`, which survives `OpenPipe`'s null check and faults in `ExfInterlockedInsertTailList`. Reachable whenever a non-High-Speed device sits on a root port. | Both shipping builds (NUSB and SP4 native), so it is the lineage and not the back-port. | Read out of both binaries; ReactOS has the guard the shipping code lacks, the same shape as the uncapped post-open ACTIVE wait and the `NumberOfPorts = 0` arithmetic. | `usbport-miniport-abi.md` section 8 and its `USBPORT_ENDPOINT_PROPERTIES` notes; `lessons.md`, the Full-Speed bugcheck entries. Worked around by `USB_MINIPORT_FLAGS_USB2` plus the root-port speed report in `src/xhci_port.c` |
| Win98's `USBAUDIO.VXD` faults after one 10 ms buffer from QEMU's emulated audio device, so USB Audio playback does not work in that vehicle. It is not a target-wide defect: on the E460 a physical UAC 1.0 device played clean under Windows 98, directly and behind a multi-TT hub. | Win98 SE, VM, five runs, three failure shapes; refuted as a property of the target on real silicon. | It faulted at the same address through a completely different controller driven by Win98's own USB 1.1 stack, with this driver idle-suspended and every isochronous counter at 0: an exoneration on the same target rather than a cross-OS differential. | `lessons.md`, "Batch 9-V"; `docs/using/release-notes.md` limitation 3 |
| NUSB's `usbport.sys` submits to an isochronous pipe whose open the miniport refused. SP4's usbport asked for an isochronous open (`TransferType = 0`, MPS 192, `Period` 1, `MaxTransferSize = 0x01000000`), was refused `MP_STATUS_NOT_SUPPORTED`, and stopped; NUSB's went on to call `SubmitIsoTransfer` on that endpoint extension, one no successful `OpenEndpoint` had produced. Same family as the `USBPORT_GetTt` row: the older build guards a miniport refusal less. | Win98 SE + NUSB, VM (batch 6-V, on an early build of this driver, which refused every non-control endpoint). | The same binary on Win2000 SP4 received the identical open request and no submit after the refusal, so the difference is the usbport lineage and not the miniport. Every isochronous callback must therefore be safe against an endpoint extension it never initialised. The fatal `0E` at `0028:FF046A14` that followed on 2a was not this defect: batch 7a-V traced it to this driver's own inline completion inside `SubmitTransfer` against usbport's post-return write (`usbport-miniport-abi.md` section 4, "The completion path is unordered against usbport's post-submit writes"). | `usbport-miniport-abi.md` "Isochronous transfers"; batches 6-V and 7a-V |
| Windows 98's composite parent `usbhub.sys` is never placed on an xHCI-only machine, so a multi-interface device enumerates and then has nothing to bind (`USB Composite Device`, `Code 2`: matched, and the file was not there). Windows 98 ships `USB.INF` on every install but copies the USB driver files only when Setup detects a USB controller it recognises. NUSB is not the cause: it ships no composite parent, but Windows 98 already has one and simply did not install it. | Win98 SE on real hardware (ThinkPad E460), and reproduced in the 2a guest by renaming the file away. | Three independent directions: a ThinkPad X61 running the same Win98 SE and the same NUSB 3.3 with no xHCI binds every one of the same units, because Setup found its UHCI controller; supplying the one file on the E460 makes them bind there; and removing it from a guest that had it reproduces `Code 2` on a device that had bound for months. A root-port control excludes topology, and the driver's own side reads clean in the same run (`devices addressed 1`, the configuration descriptor read). | `lessons.md`, the composite-parent entry; `run-13e.md` "Finding 1 - RESOLVED"; roadmap task 13-E.1's result box. Fixed: the package carries `usbhub.sys` on the Windows 98 path (`src/xhci98.inf`), so `1.0.0.0` has no limitation for it |

Do not add a row from memory or from a single symptom. The bar is the one
the rows above meet: either the same failure reproduced without this driver in
the path, or the defect read out of the shipping binary at a named address. A
symptom this driver merely sits under belongs in `failure-diagnosis.md` until
it has one of those.
