# General SuperSpeed support: the successor host controller driver `xhci98.sys`

Design record 13, for roadmap Phases 25 onward
(`docs/contributing/roadmap-hcd.md`). It was
`docs/future-plans/superspeed-hcd-reimplementation.md` from 2026-09-07, when
it left the "What SuperSpeed Support Would Require" section of
`docs/usb-xhci-info/xhci-programming.md`, until 2026-10-02, when the owner
froze the miniport at `1.2.0.0` and scheduled its successor; it moved here
that day with the proposal's text intact, and roadmap Phase 25 grew it into
the record proper on the same day. Sections 1 to 3 are the analysis the
proposal carried; section 4 holds the decisions the owner has taken; section
5 is the design (task 25.1); sections 6 to 10 are the readings it is written
against - the function-driver contract (25.2), the Windows 98 export evidence
(25.3), the `USBUSER` door and the two property tabs (25.4), the harness and
the gates (25.5), and hubs and composite devices inside the bus (25.6);
section 11 is the transfer-buffer policy (25.7) and section 12 the index of
what Phase 25 leaves open. Appendix A is the full Windows 98 export table.

**Since 2026-10-02 the successor is `xhci98.sys` and `src\` is the
successor's** (owner, the same day, after task 25.8 had first built it as
`xhci98hc.sys` in `src\hcd\`): the miniport left the tree, its last
sources are on branch `1.2.0.0` and its binaries in `releases\1.2.0.0`.
Where sections 1 to 3 say `xhci98.sys` they mean the miniport, as they did
when written.

## 1. Why it is not an extension of the miniport

SuperSpeed is not an extension of the USB 2.0 miniport path. The reused
Win2000-era `usbport.sys` has no SuperSpeed speed reporting, bandwidth model,
root-hub semantics, or USB 3.x hub support, and there is no USB 3.0-capable
`usbport.sys` for this driver model to install instead: Microsoft's
SuperSpeed stack uses the Windows 8-era UCX model (`Ucx01000.sys`,
`Usbxhci.sys` and `Usbhub3.sys`), which cannot run on Windows 98 or
Windows 2000. `docs/usb-xhci-info/win98-wdm.md`, "USB Stack Architecture and
the Integration Decision", has the stack survey behind that sentence.

A general SuperSpeed implementation would therefore first have to replace
Option A with the Option B monolithic HCD that `docs/contributing/architecture.md`
describes under "Fallback: Option B": `xhci98.sys` taking ownership of the
root-hub PDO, `IOCTL_INTERNAL_USB_*`, URB parsing, enumeration and
scheduling, on top of the same xHCI hardware layer the tree has now. Option B
was the documented fallback for the Phase 3 spike and was never needed; it
amounts to "be `usbport.sys`", and the hub contract it would sit under
(`usbhub.sys` on Windows 2000, NUSB's `usbhub20.sys` on Windows 98) would have
to be matched from the binaries the same way the miniport ABI was.

## 2. What the xHCI layer would then need

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
  U-state transitions, hubs, storage, Ethernet, and audio on real
  controllers.

## 3. The judgement, and the narrower case that was carved out

This is a separate driver-stack project with little practical benefit on the
target operating systems: High-Speed already covers the intended HID,
storage, Ethernet and audio workloads, and every USB 3.x device this project
has held falls back to its USB 2.0 path and runs at 480 Mbps. That is why a
controller exposing only USB 3.x protocol ports is refused at start
(`XHCI_CAPS_NO_MANAGED_PORTS`) rather than driven, and why the refusal is
per-controller rather than per-connector; `docs/usb-xhci-info/xhci-programming.md`
keeps both facts, since they describe the shipping driver.

One case does not need any of section 1 or 2's generality: a bulk device on a
root port, driven at SuperSpeed by the miniport and reported to
`usbport.sys` as High-Speed. That proposal is
[superspeed-storage-behind-a-switch.md](../../future-plans/superspeed-storage-behind-a-switch.md),
and its section 2 says where the line between the two lies. With the miniport
frozen it will not be built; it stays in `future-plans/` as the record of what
it would have taken, and its `-0` transcription list is what Phase 29's `29-0`
batch starts from.

## 4. Scheduled: the decisions taken on 2026-10-02

The work is scheduled. That changes its standing, not the analysis above:
sections 1 to 3 still describe what the project is and what it costs. The HCD
is the miniport's **successor**, not a project beside it: the Option A
miniport is the driver it replaces, and the miniport's work so far - the xHCI
hardware layer, the readings, the harness and the test images - is what it
builds on. The miniport is the shipping driver on every target until
`2.0.0.0` is cut, and the rule in `AGENTS.md` against re-implementing
`usbport.sys`'s role inside the miniport still holds for `src/`; the
successor is a deliberate change of architecture in a directory of its own,
not something the miniport drifts into.

The four questions the proposal left to the owner, and the four more the
roadmap put, were all answered on 2026-10-02. The roadmap's decisions table
is the index; this is the same list in the proposal's order:

- **"Stabilised" is `1.2.0.0`.** The miniport is frozen at that release:
  no further cut, and a defect reported against it is answered by the
  successor.
- **The handover is a second binary, not a switch.** The successor is chosen at
  install time through Update Driver against the same PCI class id; never a
  registry switch inside one binary, because the miniport's import of
  `USBPORT_RegisterUSBPortDriver` is a load-time gate and a combined binary
  could not load on a stock Windows 98 SE. Its name: `xhci98hc.sys` in a
  package of its own, coexisting with the miniport on disk (owner, 2026-10-02),
  until the owner decided later that day that the miniport leaves the tree and
  the successor takes `src\` and the name `xhci98.sys` - its INFs
  `xhci98.inf` and `xhci98-amd64.inf`, its package `xhci98-<version>.zip`.
  A `2.0.0.0` install therefore replaces the miniport's file rather than
  sitting beside it; going back to `1.2.0.0` is a reinstall of that package.
  The successor starts on the two
  primary targets in QEMU (Phase 26) and reaches the seven other guests in
  Phase 28.
- **USB 2.0 parity first.** Phases 26 to 28 replace usbport, the hub driver
  and the composite parent with no SuperSpeed port powered, proven against
  round 12's device matrix; SuperSpeed on root ports is Phase 29, hubs Phase
  30, streams and UAS Phase 31.
- **The behind-a-switch proposal is not built.** It stays in `future-plans/`.
- **One cut, `2.0.0.0`, after UAS** (Phase 32).
- **The hub class and composite splitting live inside the bus driver.**
  External hubs are objects of the bus, not PDOs; a composite device is split
  into per-function PDOs by the bus. The hub-to-port contract of section 1's
  last sentence is therefore never matched: there is no hub driver above the
  bus to match it for.
- **Stock Windows 98 SE is a goal**, "no host-stack dependency": the HCD
  loads, starts and binds devices with no USB 2.0 stack installed, and NUSB's
  and SweetLow's host-stack halves become inert. NUSB's `usbstor.sys` and its
  `ntmap` layer remain the mass-storage class driver on Windows 98 SE, which
  the stock OS lacks and which the HCD does not replace. Task 26-V.3 takes
  the reading; whether it is a checkpoint clause is the owner's call when
  Phase 26 opens.
- **Mass storage on Windows 98 SE stays NUSB's.** The owner chose this over
  an own storage driver, on this reading of NUSB 3.6's INFs
  (`legal-provenance.md` section 4, the NUSB 3.6 INF row; static, a text
  read): a drive letter on 9x comes from the IOS layer, and `usbntmap.inf`
  binds `usbstor.sys`'s disk objects (`USBSTOR\GenDisk` and its siblings) to
  `DevLoader=*IOS` with `PortDriver=USBMPHLP.PDR`, the USB mapping port
  driver, with `NTMAP.SYS` and `USBNTMAP.SYS` in the same copy set. NUSB's
  own install INF stamps `NTMAP.SYS` as `4.10.0.2227` and annotates it with
  hotfixes 242975 and 267304, a Windows 98 SE line, and its file list
  annotates `USBMPHLP.PDR` and `USBNTMAP.SYS` as `WinMe` - the package
  author's notes, not a reading of the files themselves. They are
  Microsoft's and this project does not redistribute them. An own Bulk-Only
  driver would still need that mapping above it; an own IOS port driver is
  a VxD project with no use on any other target. So `usbstor.sys` is the
  Bulk-Only driver on every target, the UAS driver of Phase 31 takes the
  same mapping route on Windows 98 SE, and on a stock Windows 98 SE no mass
  storage works, UAS included - a statement for the release notes, not a
  limitation to fix. Whether the 98 SE CD carries any of the mapping files
  is task 31-0's to read from the disc.
- **Pool and the HCD's own DMA buffers are allowed**, by import-allowlist
  rows with Windows 98 export evidence (task 25.3). The miniport's "allocate
  no pool" rule stands for `src/`.
- **The device names** are `xHCI98 USB 3.x eXtensible Host Controller` and
  `xHCI98 USB 3.x Root Hub` (owner, 2026-10-02; spelled xHCI98 by the owner
  2026-10-03), the INFs' device
  descriptions on every path.
- **The root hub is a devnode, and both property tabs are kept** (owner,
  2026-10-02, reversing a narrower answer given on the Codex review earlier
  that day). The controller FDO creates a root-hub PDO under a
  project-owned hardware id - never `USB\ROOT_HUB`, which the OS's own
  `usbhub.sys` claims on every NT target - and `xhci98.inf` binds it to
  `xhci98.sys` itself, so one binary is the function driver of both and
  every device PDO is a child of the root hub. The controller's Advanced
  tab: the HCD answers the `USBUSER` request set `usbui.dll` sends to the
  controller devnode, and its INFs write the two registrations the
  miniport's write today (`EnumPropPages` to `sysclass.dll` on Windows 98,
  where `usbui.dll` draws the dialogs; `EnumPropPages32` to `usbui.dll` on
  the NT paths). The root hub's Power tab: the root-hub devnode registers
  the hub property-page provider each OS's own INF registers for its root
  hub (`build-and-test.md`, the `usbui.dll` paragraph of "The files the OS
  supplies"), and answers the hub IOCTLs that page sends. `XHCISNAP`
  reaches the HCD through the controller's door. Task 25.4 reads all of it
  per target.
- **The miniport's virtual-hub values are not supported** (owner,
  2026-10-02): `XhciVirtualHSHub`, `XhciVirtualHSHubVid` and
  `XhciVirtualHSHubPid` exist to make usbport tell the truth about a
  root-port device's speed, and the HCD has no usbport to lie to; it reads
  none of them, its INFs write none, a value left behind has no effect, and
  the release notes say so.
- **SuperSpeed hubs need a Windows 2000 observation vehicle, for now** (the
  same review): Windows 2000 is a VM-only target and no QEMU device models a
  SuperSpeed hub, so Phase 30's "observed on both" has no vehicle today. The
  owner did not record an exception; the question is revisited when Phase
  30 opens, and until a vehicle exists or that changes, Phase 30 cannot
  close and Phases 31 and 32 wait on it.
- **USB Audio 2.0 is a separate repository**, a class driver of generic
  design so that it also serves machines on EHCI and vendor stacks; it needs
  nothing from this stack.

## 5. The design of the bus driver (task 25.1)

Sections 1 to 4 are the analysis and the decisions. This section and the ones
after it are the record proper: what the successor is made of, written before
a line of it beyond task 25.8's scaffold exists. Sections 6 to 10 carry the
readings it is written against (tasks 25.2 to 25.6), section 11 the
transfer-buffer policy (25.7), and section 12 what Phase 25 leaves to later
tasks. Where this section states a design and a later reading in this record
bears on it, the reading is cited by section; where it states a design that no
reading here can settle, it says which Phase 26 task settles it.

### 5.1 Source layout, and what `build.exe` accepts

**`src\` is the HCD** (owner, 2026-10-02, after the scaffold first went in as
`src\hcd\`): the usbport miniport left the tree that day and the HCD took
over the directory and the name `xhci98.sys`. The miniport's last sources are
on branch `1.2.0.0` and its binaries in `releases\1.2.0.0`; nothing in the
tree builds it any more.

What `src\` holds, by the roadmap's reuse table ("The shape of the
successor"):

| Row | Files | State after task 25.8 |
|---|---|---|
| Lifted, the pure core | `xhci_mem.c`, `xhci_ring.c`, `xhci_caps.c`, `xhci_port.c`, `xhci_ctx.c`, `xhci_topo.c`, `xhci_desc.c`, `xhci_log.c` and their headers | Built, DDK-free (design record 03), host suites in `test\` unchanged. Three include `xhci_usbport.h` for its DDK-free type declarations, and `xhci.h` still carries the miniport's extension layout, which the HCD embeds unchanged in its controller FDO (`hcd.h`); the carve down to what the HCD uses waits for 26-A.5, which replaces the transfer structures most of it describes |
| Adapted | `xhci_init.c`, `xhci_cmd.c`, `xhci_evt.c`, `xhci_pci.c` | **Built since 26-A.2** (2026-10-03), with `xhci_dbg.c`: every usbport service call replaced by `hcd_svc.h`'s (wait, configuration space, the one-shot timer, the reset request, the fail-closed DMA verdict, the controller lock). Their comments still argue in usbport's terms; `hcd_svc.h`, "READING THE KEPT FILES", is the key |
| Rewritten | `xhci_dispatch.c`, `xhci_rh.c`, `xhci_slot.c`, `xhci_xfer.c`, `xhci_vhub.c`, `xhci_probe.c` | **Deleted** from the tree with their host suites (`test_xfer`, `test_iso`, `test_vhub`, `test_init`), as are `scripts\make-usbport-lib.cmd` and `scripts\usbport-lib\`. Their headers stay until 26-A.5's carve, because `xhci.h` includes them. `xhci_xfer.c` came back in 26-A.2 holding only the completion-code table, lifted unchanged, and whole in 26-A.4 (its pure TD builder; corrected 2026-10-03) |
| New | `hcd_*.c`, `hcd.h`, `hcd_svc.h` | The scaffold of 25.8 (below) became the controller FDO in 26-A.1 and 26-A.2: `hcd_entry.c`, `hcd_pnp.c`, `hcd_power.c`, `hcd_ctl.c`, `hcd_svc.c`, `hcd_dma.c`, `hcd_dev.c`; `hcd_pool.c` waits for the first allocation |

One measurement from the first layout is kept because it constrains any
future split of `src\`. The Windows 2000 DDK's `build.exe` accepts exactly
one directory prefix in `SOURCES=`, the parent's. Measured on 2026-10-02
(task 25.8): from a sibling directory `hcd\` with `SOURCES=..\src\xhci_ring.c
...`, `build -cZ` printed

    BUILD: ...\hcd: Ignoring invalid directory prefix in SOURCES= entry: ..\src\xhci_ring.c

for every core file and then failed with `NMAKE : U1073: don't know how to
make 'objfre\i386\\xhci_mem.obj'`; from `src\hcd\` with `SOURCES=..\xhci_ring.c
...` the same files compiled and linked in all three flavours, and WDK 7.1's
`build.exe` did the same for amd64. A second binary that shares the core must
therefore live in a direct subdirectory of `src\`.

The scaffold of task 25.8 is `src\hcd_entry.c`: a `DriverEntry` that registers
nothing and returns `STATUS_SUCCESS`, carrying the flavour marker
(`XHCI98_FLAVOUR_*`, read by `scripts\check-flavour-marker.ps1`) and
`XHCI98_SCAFFOLD_DO_NOT_STAGE`, which the packager refuses and which is the
import gate's one licence for an image with no imports (section 9). Linked,
the x86 release image is 2,257 bytes and imports nothing at all: `/OPT:REF`
drops every core function, since nothing calls one yet. The amd64 image
imports one pair, `ntoskrnl.exe!KeBugCheckEx`, which WDK 7.1's `/GS` runtime
adds and which the amd64 allowlist carries for that reason alone.

The version is `src\xhci_version.h`'s, `1.99.0.0` dated `10/02/2026`: a
development number that ranks above the installed `1.2.0.0` and claims
nothing, since no HCD build is published before `2.0.0.0` and task 32.3 sets
that number there and in both INFs.
### 5.2 The object model

One driver object, `xhci98.sys`, in two PnP roles, and a tree of objects
inside the bus that the PnP manager never sees.

| Object | Kind | Created by | Parent in the PnP tree | Extension |
|---|---|---|---|---|
| Controller FDO | WDM FDO, attached over the PCI PDO | `AddDevice` for a PDO this driver did not create | the PCI bus | the controller extension: resources, BAR mapping, the interrupt object, the DMA adapter and the common buffer (section 11), the controller lock, the command engine, the event ring, the port objects of the root ports, the slot table, the log ring |
| Root-hub PDO | WDM PDO, hardware id `XHCI98\ROOT_HUB`, no compatible ids (section 8) | the controller FDO, at its first start | the controller | a small PDO extension pointing back at the controller |
| Root-hub FDO | WDM FDO, attached over the root-hub PDO | `AddDevice` again, for a PDO this driver did create - the role is decided by the PDO's own driver object | the root-hub PDO | the bus's view of the topology: the port objects, the hub objects inside the bus, the list of device and function PDOs |
| Device PDO | WDM PDO, one per non-composite device and per non-hub device the bus does not split | the root-hub FDO, when enumeration (5.3) reaches a bindable device | the root hub, wherever the device is in the USB topology | the device's slot, its configuration and pipes, its ids |
| Function PDO | WDM PDO, one per function of a split composite device (section 10) | the root-hub FDO | the root hub, beside the device PDOs | the function (its interface mask, its ids), its own filtered copy of the configuration descriptor (10.9), the group serial it shares with its siblings, and a pointer to the device record inside the bus, whose slot and pipes it shares, cleared when the device leaves (corrected 2026-10-03 from "a pointer to the parent device object inside the bus, whose pipes it shares", as 26-A.7 implements it) |
| Hub object | inside the bus, never a PDO | the root-hub FDO, when a hub enumerates (section 10) | - | its slot, its hub descriptor, its status-change pipe, its port objects |
| Port object | inside the bus | the controller for a root port, a hub object for a hub port | - | the port shadow, the reset generation, the device attached, the hold state of 29-A.5 |
| Device object (the bus's, not WDM's) | inside the bus | enumeration | - | one per addressed device, hub or not: slot id, speed, route string, TT fields, descriptors, the PDO(s) it is presented as |

Every device PDO and every function PDO is a child of the root hub, not of the
hub it is plugged into: external hubs are objects of the bus (the decisions
table), so a device behind a hub appears directly under `xHCI98 USB 3.x Root
Hub` in Device Manager, and the Power tab reports the budget the bus itself
keeps (section 8). **Superseded 2026-10-04 by task 33.4 (section 10.11):**
each hub the bus serves also has a PDO, bound to this driver as a hub FDO,
and the devices behind it are its children; the bus still runs the hub.

The driver's role at `AddDevice` is decided by whether the PDO handed in was
created by this driver object (`Pdo->DriverObject == DriverObject`), and
nothing else. A PDO of this driver's that is not a root-hub PDO never reaches
`AddDevice`: device and function PDOs are bound by the targets' class INFs to
their own function drivers (section 6), and only `XHCI98\ROOT_HUB` is bound
to this binary (section 8).

Lifetimes follow the WDM rules the targets enforce, with two of this
project's own. A device that leaves (its port reports a disconnect, its hub is
removed, or the controller stops) has its slot disabled and its pipes failed
at once, but its PDO lives until the PnP manager has been told it is missing
in a `QUERY_DEVICE_RELATIONS` answer and has sent the PDO its remove. The
device object inside the bus goes with the slot, not with the PDO: the PDO
keeps copies of the descriptors its ids are made from, and nothing of the
device object (corrected 2026-10-03 from "freed only then", which the
implementation never did; Codex review of batch (b), round 1, finding 17).
A Disable Slot the controller does not confirm is the exception: the device
object stays, quarantined in the slot table, until the controller reset that
failure requests has taken every slot. The PDO's own lifecycle, as 26-A.4
implements it (`hcd_pdo.c`): only the controller thread lists a device PDO
or moves it off the relations (the controller's stop does too, with the
thread stopped), the other unlinkings being the PDO's own deleting remove
and its parent's release, each under the PDO-list lock (5.4; corrected
after round 2 of the same review, note 7), and each relations answer marks
the PDOs it carries as reported
and the unlisted ones as reported missing, under the same lock hold as the
copy; a PDO is deleted by its own remove once reported missing, at once by
the thread when the PnP manager never saw it, or by its parent's removal when
the PnP manager has removed it already - and when its parent goes first while
it still awaits its remove, it is orphaned, forgets the controller, and
deletes itself at that remove. The root-hub PDO follows the same orphan rule
against the controller FDO. A split device's function PDOs (26-A.7,
`hcd_pdo.c`) are one group: all created before any is listed, listed in one
hold of the PDO-list lock, sharing the first one's serial; when the device
leaves they leave the relations together, and the port re-enumerates only
once the last of the group is deleted. A function PDO's own remove cancels
only its own requests, and the thread then closes its pipes, drops its
endpoints and returns its interfaces to alternate 0, its siblings' kept
(10.9). The device record still goes with the slot, not with any of the
group. And a surprise removal on
Windows 98 arrives as an out-of-sequence `IRP_MN_REMOVE_DEVICE` with no
`SURPRISE_REMOVAL` before it (`docs/usb-xhci-info/win98-wdm.md`), so every
remove handler is written to be the first PnP IRP the object sees after
start.

**The controller's orderly stop is the exception to "the controller stops,
the PDOs are reported missing"** (corrected 2026-10-04; roadmap task 33.1,
merge `a9a7577`; `docs/issues/09-me-controller-reenable-stopped-pdos.md`).
Windows 98 SE and ME disable a controller by STOPping its whole tree and
STARTing it again at the enable, expecting the same devnodes back; a new PDO
at the instance id of a stopped one wedged ME's configuration manager. So on
an orderly PnP `STOP` of the controller (`StopPreserve`), every device whose
PDOs PnP has all stopped - listed, not removed, not surprise-removed, no URB
pending - keeps them listed with no device record, **dormant**
(`HcdDevicePdoDormantAll`), and the drop that follows reports nothing gone
for it. At the restart the device's re-enumeration at the same place with
the same descriptors revives the dormant group (`hcdDormantRevive`), whose
`START` waits for that (up to 10 s, `hcdDormantWait`); a device that does
not come back, or a different one there, has the dormant group reported
missing then. Windows 2000 onward remove the children before a controller
disable, so nothing goes dormant there, and a stop of the controller alone
(a rebalance) leaves the children started, which are dropped and reported
missing as above. Hub PDOs (task 33.4, section 10.11) follow the same model:
a hub's dormant PDO is revived by the hub's re-enumeration with its serial
kept, a device behind a hub is kept dormant only when that hub's PDO is, and
a dormant group retired takes the dormant groups below it with it.

### 5.3 The enumeration state machine

One machine per port object, driven from one PASSIVE-level context per
controller (5.4), because every step after the first waits on a command or a
control transfer and the targets give a monitor-free driver no other place to
wait. The states and their exits:

| State | Entered on | Action | Exit |
|---|---|---|---|
| `Empty` | start, or a disconnect | nothing | a connect change on the port: `Debounce` |
| `Debounce` | connect | wait the USB 2.0 attach debounce (section 10 carries the value and its source) | still connected: `Reset`; disconnected: `Empty` |
| `Reset` | debounce done | root port: `PORTSC.PR`; hub port: `SET_FEATURE(PORT_RESET)` on the hub (section 10). Speed is read from the port when the reset completes | enabled: `EnableSlot`; reset failed or timed out: `Failed` |
| `EnableSlot` | port enabled | Enable Slot command | slot id: `Address`; no slot: `Failed` (counted - the controller is out of slots) |
| `Address` | slot | Input Context with the Slot Context (speed, route string, root port, TT fields from section 10) and EP0 at the speed's default packet size, Address Device with `BSR` = 0 | success: `Descriptor8`; failure: `Failed` |
| `Descriptor8` | addressed | `GET_DESCRIPTOR(DEVICE)` for 8 bytes | `bMaxPacketSize0` differs from the guess: Evaluate Context, then `Descriptor18`; else `Descriptor18` |
| `Descriptor18` | | the full device descriptor; the serial string when one is declared (29-A.5's identity read needs it) | `Configuration` |
| `Configuration` | | `GET_DESCRIPTOR(CONFIGURATION)` for 9 bytes, then for `wTotalLength` | a hub (`bDeviceClass` 9): `Hub`; otherwise `Present` |
| `Hub` | a hub's descriptors | the hub bring-up of section 10 - configure, hub descriptor, `Configure Endpoint` with the hub fields, power the ports, arm the status-change pipe | the hub's ports become port objects in `Empty`; the hub itself is never a PDO |
| `Present` | a bindable device | decide the split (section 10), create the device PDO or the function PDOs with their ids (sections 6 and 10), `IoInvalidateDeviceRelations` on the root hub | `Bound` when the PnP manager has started the PDO(s) |
| `Bound` | | the PDOs' own IRPs are served (26-A.5, 26-A.6) | a disconnect or a parent's removal: `Gone` |
| `Gone` | | slot disabled, pipes failed, PDOs reported missing at the next relations query | the PDOs' removes complete: `Empty` |
| `Failed` | any failed step | the slot disabled if one was enabled; counted per cause | `Empty` on the next connect change; one retry of `Reset` first, as the targets' own hub drivers do (section 10 has the retry rule) |

What counts as a connect change, as 26-A.3 implements it for root ports
(`hcd_enum.c`): the port's connect status change bit, not its connection
state. A port whose `PORTSC.CSC` is set gives up what it held (a disconnect)
and, when it reads connected, starts a new enumeration; a change of another
kind - the reset's own `PRC` among them - only takes a port that reads
disconnected to `Empty` and an `Empty` port that reads connected into
`Debounce`, so a `Failed` port waits for a real reconnection rather than
retrying for ever. Only the change bits the read saw are acknowledged. A
controller reset (recovery, a resume that reinitialised) or the root hub's
removal settles every port from whatever state it is in, and the PDOs are
reported missing. After a reset the device objects go without commands and
the ports are rescanned; a port waiting in `Gone` keeps waiting for the PDO
it reported, by that PDO's serial, so a later device on the port is not
enumerated before the earlier PDO is deleted. On the root hub's removal a
powered controller gives each slot back with Disable Slot, and an unpowered
one leaves the records for its first powered pass to disable; every port then
starts again from `Empty` when the root hub starts again.

Two things are deliberately missing. There is no `SET_ADDRESS` anywhere: the
bus addresses with the Address Device command, and since the bus builds the
setup packets itself, there is nothing to intercept - the miniport's
interception (`implementation-invariants.md`, "Device Addressing") has no
counterpart. And there is no speed lie: the speed the port reports is the
speed in the Slot Context and the speed every PDO reports, and the High-Speed
report the miniport made to usbport for a root-port device has no successor
(the decisions table; the matrix expectation of 26-A.10 asserts it).

The host vectors of 26-A.9 drive this machine with no hardware: the machine
is written as a pure transition function over a port object and an event, in
the core's DDK-free style, and the PASSIVE context only feeds it events and
carries out the actions it returns - the same split `xhci_vhub.c` made for the
virtual hub (design record 12).

### 5.4 The lock model, against design record 05

Design record 05 is the miniport's synchronization design, and most of its
weight is carried by something the HCD does not have: four usbport contexts,
none serialized against another, and an interrupt object usbport owns. What
changes, rule by rule:

| Design record 05 | Under the HCD |
|---|---|
| One lock, `xhciControllerLock`, in the image, created in `DriverEntry` (rule 3), because usbport zeroes the miniport extension before every start | **Per controller, in the controller extension, created at `AddDevice`.** The HCD owns its extension and nothing zeroes it underneath a holder, so the hazard rule 3 answered does not exist; one lock per controller removes the cross-controller contention the miniport accepted |
| Rule 1: innermost; no usbport service and no bounded wait under it; decide under the lock, act after dropping it | Kept, with "usbport service" read as "any call out of the driver" - `IoCallDriver`, `IoCompleteRequest`, `IoInvalidateDeviceRelations`, a completion routine. An IRP is completed after the release, never under the lock |
| Rule 2: every non-ISR read-modify-write of the flags under it | Kept |
| Rule 4 and section 4: the ISR never takes the lock, and nothing can exclude it, because `KeSynchronizeExecution` needs the `PKINTERRUPT` usbport keeps and `KeAcquireInterruptSpinLock` is XP-era | **The exclusion is now available.** The HCD connects its own interrupt (`IoConnectInterrupt`) and so holds the `PKINTERRUPT`; `KeSynchronizeExecution` has stock Windows 98 SE precedent (section 7). The ISR stays as stateless as section 4 made it - that design is still the cheaper one - and `KeSynchronizeExecution` is available for a DISPATCH-level path that must change what the ISR reads (`IMAN`, the interrupt-enable state), which section 4 had to argue around. **26-A.2 uses none**: the kept ISR and the kept enable and mask paths are the miniport's, which section 4 made safe without the exclusion, and nothing in the HCD has needed it yet |
| Section 7: the root-hub callback family, the NT 6.x PASSIVE root-hub queries with no usbport lock | Gone. Root-port state is the controller's port objects under the controller lock; the root hub's IOCTLs (section 8) read them through it |
| Section 7: transfer metadata, the deferred-completion rule (issue 7) | Kept in substance: a transfer is completed - its IRP completed - after the drain has released the lock, from a list built under it |

What is new has three layers, in this order from outermost to innermost:

1. **The enumeration context.** One PASSIVE-level context per controller runs
   every enumeration machine, every hub's status-change handling and the hub
   teardown (5.3, section 10), and is the only writer of the topology - the
   port, hub and device objects and the PDO list. Being the only writer is its
   synchronization: nothing else mutates those objects, so they need no lock
   against each other. It waits on command and transfer completions with
   `KeWaitForSingleObject`, which only a PASSIVE context may. The context is a
   system thread (`PsCreateSystemThread`, stock Windows 98 SE precedent:
   `bt829.sys`, section 7) rather than a chain of `ExQueueWorkItem` items
   (stock precedent in 24 drivers, `uhcd.sys` among them), because a thread
   can block between steps and a work item borrowed from the system pool must
   not; 26-A.2 confirms the choice on both primaries, and the same thread is
   where 26-A.8's PASSIVE-level log flusher runs.
2. **The PDO-list lock.** A spin lock (`PdoListLock`) guarding the device-PDO
   lists `QUERY_DEVICE_RELATIONS` reads and marks. Drafted in the root-hub
   FDO's extension, it sits in the controller's since 26-A.4, so that the
   controller's stop and remove reach it with no root hub present. The
   enumeration context links and unlinks the PDOs, a PDO's deleting remove
   and a parent's release unlink them too, and so the PDO list is the one
   part of the topology whose single-writer rule in layer 1 does not hold
   (Codex review of batch (b), round 2, note 7). Held only to link, unlink,
   mark or copy the lists.
3. **The controller lock**, innermost, as above.

A function driver's IRP enters at the PDO at up to DISPATCH_LEVEL and touches
the topology only through its own device object, whose lifetime 5.2 pins to
the PDO's remove. It takes the controller lock to queue a transfer and nothing
else. Cancellation uses `IoSetCancelRoutine` with the transfer list under the
controller lock and the cancel spin lock released before it is taken - the
order the miniport's own `implementation-invariants.md` would recognize as
rule 1.

The static review that design record 05 section 6 ran over the miniport is
owed again once 26-A.2 to 26-A.6 exist; it is a Phase 26 deliverable, not a
Phase 25 one.

### 5.5 The INFs and the package

Two INFs in `src\`, the files the miniport's two were:
`xhci98.inf` (undecorated Windows 98 and ME sections, `.NTx86` for Windows
2000 and 32-bit XP, `NTx86.6.0` for 32-bit Vista and Windows 7) and
`xhci98-amd64.inf` (`NTamd64` for XP x64, `NTamd64.6.0` for Vista and
Windows 7 x64). Each models section carries two models: the controller under
`PCI\CC_0C0330` and the root hub under `XHCI98\ROOT_HUB`, both bound to
`xhci98.sys` through one service, `xhci98`. The device descriptions are
the owner's names: `xHCI98 USB 3.x eXtensible Host Controller` and `xHCI98
USB 3.x Root Hub`. Both files pass the INF gate under its HCD profile
(section 9).

What they fetch from the OS through `LayoutFile`, and what they refuse to:

| File | Under the miniport | Under the HCD | Why |
|---|---|---|---|
| `usbd.sys` | every path but NT 6.x | every path but NT 6.x | The class drivers on the HCD's PDOs import its helper exports - `USBD_CreateConfigurationRequestEx`, `USBD_ParseConfigurationDescriptorEx`, `USBD_ParseDescriptors`, and the ASIX drivers' `USBD_GetUSBDIVersion` (section 6) |
| `usbui.dll` | every path but NT 6.x | every path but NT 6.x | The provider of both property tabs on every target (section 8) |
| `usbport.sys` | NT 5.x paths | **refused** (`OS-HCDREPLACED`) | The HCD replaces it |
| `usbhub.sys` | every path but NT 6.x | **refused** (`OS-HCDREPLACED`) | The HCD replaces the hub driver and the composite parent |

The registry values, the carry-over list the decisions table assigned to this
task. The miniport reads six values from its device key (`src\*.c`, the six
`L"Xhci..."` names):

| Value | Under the HCD |
|---|---|
| `XhciLogVerbosity` | Carried over, same meaning, same shipping default 0, controller only |
| `XhciLogDebugView` | Carried over, same meaning; under the HCD it also selects 26-A.8's continuous PASSIVE flusher |
| `XhciImodInterval250ns` | Carried over, same meaning and code default 4000; both INFs write 160 (40 us) on every install path since the owner's ruling of 2026-10-04 (`roadmap-hcd.md`, decisions table), 500 until then |
| `XhciVirtualHSHub`, `XhciVirtualHSHubVid`, `XhciVirtualHSHubPid` | Not read and not written (owner, 2026-10-02); the INF gate refuses them in an HCD INF (`VAL-HCDVHUB`) |
| `XhciFastPollFsLs` | New in `2.1.0.0` (task 33.8), the HCD's own: read at each controller start, written by no INF, absent or 0 is off. Lets a Low- or Full-Speed interrupt endpoint on a root port be polled faster than 1 ms; section 13 |

The root-hub sections write no value of the controller's: they carry the
loader values on Windows 98 and the hub property-page registration on every
path (section 8), which the gate's `HCD-HUBPAGE` rule holds them to.

The package is `xhci98-<version>.zip`, as the miniport's was - the published
`releases\1.2.0.0` directory is never edited (`releases/README.md`) - staged by `make-package.ps1` from 26-A.1 and published by
`make-release.ps1` from 32.3. Until 26-A.1 the packager refuses the scaffold
image by its marker (section 9).

### 5.6 Text-mode Setup: `txtsetup.oem` (task 33.3)

The question: can a machine whose keyboard or install medium sits on an xHCI
controller load `xhci98.sys` at text-mode Setup's F6 prompt of Windows 2000,
32-bit XP and XP x64, and what does Setup have to bring for it. Every reading
below is `static` unless it says otherwise; nothing here has run. The
install legs that would make any of it `runtime` are owed (the end of this
section).

**What Setup loads by itself.** Each medium's `TXTSETUP.SIF` was extracted
with 7-Zip into a scratch directory (never tracked) and read as text:

| Medium | `TXTSETUP.SIF` | `[InputDevicesSupport.Load]` | Files those entries pull in (`[files.*]`) |
|---|---|---|---|
| `win2ksp4.ISO` `I386\` | 335,081 B, SHA-256 `ECA9CDFBB5C7B455F908BF53CDBE72CADA87A2615B1E933F9559C49763CB6BF2` | `openhci`, `uhcd`, `usbhub`, `hidusb`, `serial`, `serenum`, `usbstor` | `hidclass.sys`, `hidparse.sys` and `usbd.sys` with `openhci.sys` and with `uhcd.sys`; `kbdhid` is in `[Keyboard.Load]`, `mouhid` in `[Mouse]` |
| `en_windows_xp_professional_with_service_pack_3_x86_cd_vl_x14-73974.iso` `I386\` | 480,367 B, `5F754EA59F3735CF9524E076869C2414BF39D66387A33F371CA00775F77F63D8` | `usbehci`, `usbohci`, `usbuhci`, `usbhub`, `usbccgp`, `hidusb`, `serial`, `serenum`, `usbstor` | `hid.dll`, `hidclass.sys`, `hidparse.sys`, `usbd.sys` and `usbport.sys` with each of the three host controller drivers; `kbdhid` in `[Keyboard.Load]`, `mouhid` in `[MouseDrivers.Load]` |
| `Win XP SP2 VL x64.iso` `AMD64\` | 512,828 B, `77B96776DE4DBA09A071973A661794CE98DCE5496215EF81D9193AD2BDDEAA65` | the XP list, plus `wd` | as XP |

The Windows 2000 file lists `usbport.sys` and `usbehci.sys` in
`[SourceDisksFiles]` but names neither in `[InputDevicesSupport]` or
`[HardwareIdsDatabase]`, so its text mode has no EHCI support and no
`usbport.sys` at all.

**Setup's hardware-id table.** `[HardwareIdsDatabase]` on all three binds
`PCI\CC_0C0300`, `PCI\CC_0C0310` (and on XP and XP x64 `PCI\CC_0C0320`) to
Setup's own host controller drivers, `USB\ROOT_HUB` (and `USB\ROOT_HUB20` on
XP) and `USB\CLASS_09` to `usbhub`, `USB\COMPOSITE` to `usbhub` on Windows
2000 and `usbccgp` on XP, `USB\Class_03`, `USB\Class_03&SubClass_01` and
`...&Prot_02` to `hidusb`, `HID_DEVICE_SYSTEM_KEYBOARD` to `kbdhid`,
`HID_DEVICE_SYSTEM_MOUSE` to `mouhid`, and
`USB\Class_08&SubClass_{02,05,06}&Prot_50` plus a VID/PID list to `usbstor`.
**None of them binds `PCI\CC_0C0330`**, so nothing of Setup's claims an xHCI
controller, and the table is keyed on class (compatible) ids as well as
hardware ids - Setup's own EHCI row is one.

**The `txtsetup.oem` format.** The Windows 2000 DDK's sample,
`tools\ntddk\src\setup\inf\scsi\txtsetup.oem` (1,472 B, SHA-256
`62736CB3671D26BCE67B292CA257AC6BB15A09A56AB3A0BA59F869F16A165C01`, dated
2000-07-26, not tracked), is the format reference read here: `[Disks]`
(`diskN = "description", tagfile, directory`), `[Defaults]`
(`component = ID`, its one component `scsi`), `[scsi]` (`ID =
"description"`), `[Files.scsi.ID]` (`filetype = diskN, filename[,
DriverKey]`, with the file types `driver`, `inf` and `catalog`),
`[Config.ID]` (values under the service key) and `[HardwareIds.scsi.ID]`
(`id = "deviceID", "service"`). The DDK's own format reference (the help
topic) is not in the local DDK tree; the sample's comments are what was
read. Other components (`computer`, `display`, `keyboard`, `mouse`) exist in
the format but are chosen elsewhere in Setup; F6 offers the `scsi` component,
and an F6 driver is loaded with Setup's own boot drivers, before the kernel
starts - which is early enough for a bus driver, since Setup's USB stack is
loaded the same way.

**What `xhci98.sys` needs that an INF would normally give it.** Read from
this repository's own sources: its imports are `ntoskrnl.exe` and `HAL.dll`
only (`scripts\import-gate\xhci98-imports.allow`), so it needs no file from
Setup to load; the root-hub role is decided at `AddDevice` by the PDO's
driver object, not by any INF value (`src\hcd_rh.c` header); the registry
values the INFs write (`XhciLogVerbosity`, `XhciLogDebugView`,
`XhciImodInterval250ns`) are read from the driver key with the code
defaults standing when the key or value is absent (`src\hcd_ctl.c`
`hcdReadValues`), so in text mode the log is off and the interrupt
moderation interval is the code default 4000 (1 ms) rather than the INFs'
160 - `[Config.ID]` writes under the service key, which the driver does not
read, so it cannot supply them; and the door's `SymbolicName` and device
interfaces are best effort (`src\hcd_door.c`).

**Conflicts with Setup's own USB support: none found in the reading.** The
controller id is in no Setup row. The root hub answers `XHCI98\ROOT_HUB`
and no compatible id (section 8.6), so `usbhub` cannot bind it; the
`txtsetup.oem` maps that id to `xhci98`, the same mechanism that maps
`USB\ROOT_HUB` to `usbhub` for Setup's own stack, which is what gives the
root-hub PDO its second `AddDevice` into this driver with no INF. External
hubs present no `USB\` id, so `USB\CLASS_09` never reaches `usbhub`: since
task 33.4 (section 10.11) each has a PDO under `XHCI98\HUB` or
`XHCI98\HUB30`, bound by `xhci98.inf`, and both ids are mapped here to
`xhci98` too (2026-10-04), so text mode gives a hub PDO the same second
`AddDevice` as the root hub's; the gate's `OEM-IDS` rule refuses this file
the moment the INF binds an id it does not map, so the two cannot ship out
of step. Whether text-mode Setup on Windows 2000 and XP actually starts a
hub PDO this way is unread until the F6 leg is taken with a hub. Meanwhile
the bus splits composite devices itself and presents `&MI_nn` functions
with `USB\Class_` compatible ids (section 10.7), so `USB\COMPOSITE` never
reaches `usbhub` (2000) or `usbccgp` (XP). The device PDOs' class ids then
meet Setup's `hidusb`, `kbdhid`, `mouhid` and `usbstor` rows, whose files -
and `usbd.sys`, `hidclass.sys` and `hidparse.sys` with them - come from
Setup's own source. Where a machine also has EHCI controllers, XP's text
mode drives those with its own stack beside this one; this driver does no
vendor port routing, so a port routed to an EHCI controller stays there.

**GUI-mode Setup.** The `inf` line makes text-mode Setup carry
`xhci98.inf` into the installed system, so GUI-mode Setup can install the
controller and root hub through it as for any other device, through the
same `xhci98` service the `driver` line names (the gate's `OEM-SERVICE`
rule); the INF's `StartType=3` then replaces the boot start text mode gave
the service. Whether GUI mode finds `xhci98.sys` without asking for the
floppy, and whether 32-bit XP's unsigned-driver policy during GUI mode
installs it silently, warns or skips it, is not readable from these files
and is owed to the install leg.

**`xhciuas.sys` is not on the text-mode disk, so a disk the bus selects
for UAS is lost in text mode.** The bus selects UAS whenever a device
offers a UAS alternate setting the controller can run - at SuperSpeed that
means stream support; below it, always - even beside a Bulk-Only one
(`src\xhci_xport.c`), and the one override, `XhciForceBulkOnly` in the
controller's driver key (`src\hcd_ctl.c` `HcdCtlForceBulkOnly`), is a value
text mode never writes. A dual-mode SuperSpeed device on a controller
without streams falls back to Bulk-Only and is usable. A device selected
for UAS presents the UAS class id, which no Setup row binds, so it is not
usable until GUI mode; a Bulk-Only-only device (most USB flash sticks;
QEMU's `usb-storage`) always meets Setup's own `usbstor`, and is the one
to use for a predictable result.
Carrying `xhciuas.sys` as a second `scsi` option would need its own
hardware ids and a text-mode reading of its own, and is left out. GUI-mode
Setup and the installed system install it from the package as before.

**XP x64.** The format is the same; the x64 directory carries its own file
(`src\txtsetup-amd64.oem`, staged as `txtsetup.oem`) naming the amd64
build, and NT 5.2 does not enforce kernel-mode signing, so nothing in the
reading stands between the amd64 binary and its text mode.

**Verdict, per target**, on the static evidence above:

| Target | Verdict | Why, and the limits |
|---|---|---|
| Windows 2000 SP4 | Feasible with limits | Setup carries `hidusb`, `kbdhid`, `mouhid`, `usbstor`, `usbd`, `hidclass` and `hidparse`; no Setup row claims the controller or the root hub; no `usbccgp` is needed because the bus splits composites |
| Windows XP SP3 (32-bit) | Feasible with limits | As 2000; Setup's own EHCI stack coexists on a machine that has EHCI |
| Windows XP x64 SP2 | Feasible with limits | As XP, with the amd64 build and its own file |

The limits, all of them the user's to know:

1. Pressing F6 and answering Setup's driver screens happens before any
   Windows driver runs, so the keyboard must work through the firmware's own
   USB support (legacy USB emulation or a CSM) at that point.
2. The driver disk is read as drive A: through the firmware, as text-mode
   Setup reads every F6 disk.
3. Installing Windows onto a USB disk is not supported: nothing here makes
   the installed system boot from one.
4. A disk the bus selects for UAS is not usable in text mode, even if it
   also offers Bulk-Only, since no UAS driver is loaded (above); a
   Bulk-Only-only device always is.
5. Text mode runs with interrupt moderation at the code default and the log
   off, since no INF value is written until GUI mode.
6. If the target disk needs one of Setup's own `[SCSI]` miniports, the user
   may have to add it at the same screen; whether pressing S suppresses
   Setup's own detection on these systems was not read.

**What the gate holds.** `scripts\inf-gate\check-txtsetup-oem.ps1` (run by
`build-driver.cmd` on every build, self-tested by mutation first, and by
`make-package.ps1` and `make-release.ps1` against each staged and published
directory) holds the two files to exactly the shape above: the five
sections and nothing else, balanced quotes, the `scsi` component alone, the driver at the
disk root as both the tag file and the `driver` line, the `inf` line, no
catalog and no second driver, the service the INF adds, the INF's model ids
and no `USB\` id, no Microsoft file, 8.3 names, an architecture word in each
description, the two files equal outside those descriptions, and in a
package the file at the root with every file it names beside it.

**Owed: one install leg per target**, from a floppy image in a virtual
machine with no USB host controller but `qemu-xhci`, a `usb-kbd` and a
`usb-storage` on it and the floppy on the emulated FDC: F6, S, the driver
picked, the keyboard working past the kernel start, the USB disk listed as a
target or source where the leg uses one, GUI mode completing, and the
installed system's controller and root hub on this INF. Until those pass,
every clause above stays `static` and the release notes say so.

## 6. The function-driver contract (task 25.2)

What each target's class drivers send a USB device PDO, read out of the
drivers themselves. The HCD answers these requests at its device and
function PDOs (26-A.4 to 26-A.7); nothing here is taken from the DDK headers
alone, which the Phase 25 checkpoint rules out.

**Method.** Static throughout: nothing was executed. Two subagents read the
binaries on 2026-10-02, and neither reading was re-read line by line by the
coordinator. The Windows 98 SE, NUSB and NT 5.x x86 half (25.2a) used MSVC
6.0's `dumpbin` 6.00.8447 (`-imports`, `-headers`, `-disasm`), a PowerShell
byte search for the interface GUID, and `Get-FileHash` / `VersionInfo` for
identity, with no symbols. The XP x64, Vista and Windows 7 half (25.2b) used
WDK 7.1 `link -dump -imports` / `-headers` and `cdb.exe -z` (6.12.0002.633),
which loads a raw image and executes nothing, with Microsoft's public or
private PDBs in `tools\symbols` where msdl serves them. Both halves searched
every executable section for every `0x2200xx` and `0x2204xx` immediate, for
`IRP_MJ_PNP` / `IRP_MN_QUERY_INTERFACE` stack stores, and for word stores to
a URB header's `Function` field (`[urb+2]`) with the function codes passed
to each driver's own URB-building helpers resolved at their call sites.
Every code address in this section is the image's VA at base `0x10000` as
the tool printed it, or an RVA where marked. Every constant was taken from a
header, never from memory: the IOCTL function indexes from WDK 7.1
`inc\api\usbiodef.h` (lines 48-69) with the methods of `usbioctl.h` (the
Windows 2000 DDK's `usbioctl.h` agrees for the codes it has), `FILE_DEVICE_USB`
= 0x22 (`usbiodef.h` line 171), the URB function codes from `inc\api\usb.h`,
and the interface layout from `inc\ddk\usbbusif.h` lines 252-414.

### 6.1 The constants

| IOCTL (`IOCTL_INTERNAL_USB_*`) | Value | | IOCTL | Value |
|---|---|---|---|---|
| `SUBMIT_URB` | `0x220003` | | `GET_BUS_INFO` | `0x220420` |
| `RESET_PORT` | `0x220007` | | `GET_CONTROLLER_NAME` | `0x220424` |
| `GET_ROOTHUB_PDO` | `0x22000F` | | `GET_BUSGUID_INFO` | `0x220428` |
| `GET_PORT_STATUS` | `0x220013` | | `GET_PARENT_HUB_INFO` | `0x22042C` |
| `ENABLE_PORT` | `0x220017` | | `GET_DEVICE_HANDLE` | `0x220433` |
| `GET_HUB_COUNT` | `0x22001B` | | `GET_DEVICE_HANDLE_EX` | `0x220437` |
| `CYCLE_PORT` | `0x22001F` | | `GET_TT_DEVICE_HANDLE` | `0x22043B` |
| `GET_HUB_NAME` | `0x220020` | | `GET_TOPOLOGY_ADDRESS` | `0x22043F` |
| `SUBMIT_IDLE_NOTIFICATION` | `0x220027` | | `NOTIFY_IDLE_READY`, `REQ_GLOBAL_SUSPEND`, `REQ_GLOBAL_RESUME`, `GET_DEVICE_CONFIG_INFO` | `0x220443`, `0x220447`, `0x22044B`, `0x22044F` |
| `RECORD_FAILURE` | `0x22002B` | | | |

`USB_BUS_INTERFACE_USBDI_GUID` is `{b1a96a13-3de0-4574-9b01-c08feab318d6}`
(WDK 7.1 `usbbusif.h` line 258; in-memory bytes `13 6A A9 B1 E0 3D 74 45 9B
01 C0 8F EA B3 18 D6`). The 25.2a read corrected the GUID it was briefed with
to this value, read from the header and then found as bytes in the images
(XP SP3 `usbstor.sys` at RVA `0x271C`, XP SP3 `usbaudio.sys` at RVA
`0xB3F4`, XP x64 `usbstor.sys` at file offset `0x47F0`). Its versions are
`USB_BUSIF_USBDI_VERSION_0` to `_3`, and the interface sizes follow from the
struct (an `INTERFACE` header, then `GetUSBDIVersion`, `QueryBusTime`,
`SubmitIsoOutUrb`, `QueryBusInformation`; V1 adds `IsDeviceHighSpeed`, V2
`EnumLogEntry`, V3 `QueryBusTimeEx` and `QueryControllerType`):

| Version | x86 size | x64 size | Offsets used below |
|---|---|---|---|
| V0 | `0x20` | `0x40` | `QueryBusTime` x86 +0x14, x64 +0x28; `InterfaceDereference` x86 +0x0C, x64 +0x18 |
| V1 | `0x24` | `0x48` | `IsDeviceHighSpeed` x86 +0x20, x64 +0x40 |
| V2 | `0x28` | `0x50` | |
| V3 | `0x30` | `0x60` | |

URB function names used in the tables: 0x00 `SELECT_CONFIGURATION`, 0x01
`SELECT_INTERFACE`, 0x02 `ABORT_PIPE`, 0x07 `GET_CURRENT_FRAME_NUMBER`, 0x09
`BULK_OR_INTERRUPT_TRANSFER`, 0x0A `ISOCH_TRANSFER`, 0x0B
`GET_DESCRIPTOR_FROM_DEVICE`, 0x0D `SET_FEATURE_TO_DEVICE`, 0x12
`CLEAR_FEATURE_TO_ENDPOINT`, 0x17 `VENDOR_DEVICE`, 0x1B `CLASS_INTERFACE`,
0x1C `CLASS_ENDPOINT`, 0x1E `SYNC_RESET_PIPE_AND_CLEAR_STALL`, 0x24
`GET_DESCRIPTOR_FROM_ENDPOINT`, 0x28 `GET_DESCRIPTOR_FROM_INTERFACE`, 0x2A
`GET_MS_FEATURE_DESCRIPTOR`. A 0x00 with a NULL configuration descriptor is
the unconfigure request. URB lengths as written: control, class and
descriptor requests 0x50 (x64 0x88), bulk or interrupt 0x48 (x64 0x80), pipe
requests 0x18 (x64 0x28), the unconfigure 0x3C or 0x18; isochronous lengths
are computed from the packet count.

### 6.2 The binaries read

All extracted on 2026-10-02 into git-ignored `tools\<target>-extracted\class\`
(25.2a's `hidclass.sys` for XP was already in `tools\winxpsp3-extracted\hid\`).
The NT 6.x WIM images are Vista Business SP2 (`install.wim` image 1) and
Windows 7 Professional SP1 (the single image). The installation media are
named by file name; the ISO files themselves were not hashed.

| Target | File | Version | Bytes | SHA-256 | Source medium and path |
|---|---|---|---|---|---|
| 98 SE | `hidusb.sys` | 4.10.2222 | 9,296 | `307CD8D8C1E90CA89DF1C4E23A2CB459E9E700E8C5561B7D9FD3B20711CD0196` | `Win98SE.iso` `win98\BASE5.CAB` (`LAYOUT.INF` `hidusb.sys=5,,9296`) |
| 98 SE | `usbaudio.sys` | 4.10.2222 | 40,272 | `7AF79D9BBFD6BCE0C19ADE3415CDD58F6BC1C278E4F40E41BF7E82B67122EB31` | same, `win98\DRIVER20.CAB` (`usbaudio.sys=20,,40272`) |
| 98 SE | `hidclass.sys` | 4.10.2222 | 23,520 | `2432B4D46C3D9291B49F47FC035DA182150D41CAB46D938DF5924F4603BBD9DF` | same, `win98\BASE5.CAB` |
| 98 SE (NUSB 3.3 and 3.6) | `usbstor.sys` | 4.90.3000.1 | 21,040 | `B92A6A1582287867D4950495C0A746F0F5D8831BC311061423F8645073E42D8C` | `tools\nusb-extracted\USBSTOR.SYS`, byte-identical to `tools\nusb36-extracted\usbstor.sys`: one read covers both |
| 98 SE / ME | ASIX `AX88772.SYS` | 3.0.3.12 | 18,349 | `7A712D4600B02EED49DE275AC2459DE454B4ADA467DF61608CBA09B50DF04862` | `tools\AX88772_772A_WinME98SE_v3.0.3.12\` |
| 2000 SP4 | `hidusb.sys` | 5.00.2142.1 | 13,904 | `1EC308068B86B529ADCDD454D05C4ABC84AC4D61A69EAF9696A141849F2DFC5C` | `win2ksp4.ISO` `I386\HIDUSB.SY_`, `expand.exe` |
| 2000 SP4 | `usbstor.sys` | 5.00.2195.6655 | 21,552 | `9DEF2A073FF7849C0B571AA7864A4F809209C4052D0E0E786974392A73244285` | same, `I386\USBSTOR.SY_`; identical to `I386\SP4.CAB`'s |
| 2000 SP4 | `usbaudio.sys` | 5.00.2150.1 | 68,912 | `5556161FE5336931D75A1AD8D9B658E7C816EEDFE582B36632E76558AB62ACDF` | same, `I386\DRIVER.CAB`; `SP4.CAB` carries none, and SP4's `LAYOUT.INF` line `usbaudio.sys = 1,,68912` matches this file |
| 2000 SP4 | `HIDCLASS.SYS` | 5.00.2195.6655 | 24,752 | `3BC911B579B691A9447428C2B7791938406705C30497803DF25DB9CF71846697` | same, `I386\HIDCLASS.SY_` |
| 2000 / XP | ASIX `ax88772.sys` | 3.4.3.38 | 61,696 | `30F7B50549EBE97937E1DD0C81472A002A58668821A8B952C11C69A84ACE4B0A` | `tools\AX88772A_760_772_XP2K_32bit_v3.4.3.38_WHQL\`; a KMDF 1.7 driver |
| XP SP3 | `hidusb.sys` | 5.1.2600.5512 | 10,368 | `93395FA4C26B2E82DC8B7025ED3BCF583885E5D8C5F60CD6EEAA6335D6A126EC` | `en_windows_xp_professional_with_service_pack_3_x86_cd_vl_x14-73974.iso` `I386\SP3.CAB`; identical to `I386\HIDUSB.SY_` expanded |
| XP SP3 | `usbstor.sys` | 5.1.2600.5512 | 26,368 | `ED1DC52EE45F8EAD3AEC4B1F817BB25634141CF48295494C5947DCE6CF7A9817` | same, `I386\SP3.CAB`; identical to `I386\USBSTOR.SY_` expanded |
| XP SP3 | `usbaudio.sys` | 5.1.2600.5512 | 60,032 | `226D032912D396117213FC29CD0BB5A8B2F872DD91D92F254F2F1FE392481B61` | same, `I386\SP3.CAB` |
| XP SP3 | `hidclass.sys` | 5.1.2600.5512 | 36,864 | `84A55432A7FBBD1B84FF8DD1BD84266747E4A88297BDAA84AAD12F13B848BFF2` | already in `tools\winxpsp3-extracted\hid\` |
| XP x64 SP2 | `hidusb.sys` | 5.2.3790.1830 | 18,944 | `b9ca32159cfbf658f412c77bf175bfc2e8209a32947f7c4bb251ad2a76d81759` | `Win XP SP2 VL x64.iso` `AMD64\DRIVER.CAB` (the only copy on the media) |
| XP x64 SP2 | `usbstor.sys` | 5.2.3790.3959 | 48,128 | `6bfcec240f243fa213d844d0a0a736bc96ddc57ce2ff5ab0a93a70fe5b91cdca` | same, `AMD64\USBSTOR.SY_`; byte-identical to `AMD64\SP2.CAB`'s |
| XP x64 SP2 | `usbaudio.sys` | 5.2.3790.3959 | 102,912 | `0b1c3bc0582342f633f617f4db0c5a0a11341094c26f94e4060549a3d3767a3a` | same, `AMD64\SP2.CAB` |
| Vista SP2 x86 | `hidusb.sys` | 6.0.6002.18005 | 12,800 | `91ad0758a6185b0fbbe383bdb1b457ffb850477aff8de040de9527a97d28ef62` | `en_windows_vista_sp2_x86_dvd_342266.iso` `install.wim` `1\Windows\System32\drivers\` |
| Vista SP2 x86 | `USBSTOR.SYS` | 6.0.6002.18005 | 65,536 | `201fb0fdbf423342202686dc0d8a3221b7798ae04c04a649d3441c257c733ce8` | same, `...\DriverStore\FileRepository\usbstor.inf_72a6a3e5\` |
| Vista SP2 x86 | `USBAUDIO.sys` | 6.0.6002.18005 | 73,216 | `f9ef8d0d55dabf00e79b0efe689c6662430b59093a6c7eacb2069dc70b1fdcc5` | same, `...\DriverStore\FileRepository\wdma_usb.inf_dc7189cc\` |
| Vista SP2 x64 | `hidusb.sys` | 6.0.6002.18005 | 15,872 | `bce1a241ae5cce3e1c65ccf07ecb4305c7106f2effd51f2c519eb00026b474c4` | `en_windows_vista_sp2_x64_dvd_342267.iso` `install.wim` `1\Windows\System32\drivers\` |
| Vista SP2 x64 | `USBSTOR.SYS` | 6.0.6002.18005 | 77,824 | `08cc36b33fa2281fc88671be051863aa8ca911446d24596049db77fb4cb09ea6` | same, `...\FileRepository\usbstor.inf_c0c342db\` |
| Vista SP2 x64 | `USBAUDIO.sys` | 6.0.6002.18005 | 98,944 | `dd17986cb315e9a2f34a961f3aedc034bdaf937437d74f092dee9284edbb75f4` | same, `...\FileRepository\wdma_usb.inf_7c79e8af\` |
| 7 SP1 x86 | `hidusb.sys` | 6.1.7601.17514 | 24,064 | `e208553029488a6ee2f5216cc9fe5f93e9931a94c0d0625253bb159e30642853` | `en_windows_7_professional_with_sp1_vl_build_x86_dvd_u_677896.iso` `install.wim` `Windows\System32\drivers\` |
| 7 SP1 x86 | `USBSTOR.SYS` | 6.1.7601.17514 | 76,288 | `afef764a3e5d52cdbb5074f0e87f2b5ebcdf8d9b6e8f88ee235602b80145be31` | same; size and date identical to the DriverStore copy `usbstor.inf_x86_neutral_c77d41a490bdc63d` |
| 7 SP1 x86 | `USBAUDIO.sys` | 6.1.7601.17514 | 80,768 | `72603e0a614f382af69972f0930fd168b805922599db9a7410b20cb391a9b933` | same, `...\FileRepository\wdma_usb.inf_x86_neutral_a721e4f3907a2769\` |
| 7 SP1 x64 | `hidusb.sys` | 6.1.7601.17514 | 30,208 | `fd11d5e02c32d658b28fcc35688ab66ccb5d3a0a0d74c82ae0f0b6c67b568a0f` | the x64 SP1 VL ISO (`..._x64_dvd_u_677791.iso`) `install.wim` `Windows\System32\drivers\` |
| 7 SP1 x64 | `USBSTOR.SYS` | 6.1.7601.17514 | 91,648 | `5662281c6d515423255d3c262ea368dbafc250235e535fbfa3e59d3487695439` | same |
| 7 SP1 x64 | `USBAUDIO.sys` | 6.1.7601.17514 | 109,696 | `de1cddeef2285cc8387e88acb13c000576dc8819df6dc648c988068b5c83bb15` | same, `...\FileRepository\wdma_usb.inf_amd64_neutral_7bb325bca8ea1218\` |

Read for their imports only, as references (the HCD splits composites itself,
section 10, and `usbd.sys` stays the OS's):

| Target | File | Version | Bytes | SHA-256 |
|---|---|---|---|---|
| XP x64 SP2 | `usbccgp.sys` (`AMD64\USBCCGP.SY_`, identical in `SP2.CAB`) | 5.2.3790.3959 | 42,752 | `d5e78999a26196b841aae4690588097179676f72f43e573ab173f74b8c7e1225` |
| XP x64 SP2 | `usbd.sys` (`AMD64\DRIVER.CAB`) | 5.2.3790.1830 | 7,552 | `e2c9bf115769156424b8167742704a871d264d39119087bf56692d289995f438` |
| Vista SP2 x86 | `usbccgp.sys` | 6.0.6000.16386 | 73,216 | `6b529901b0311197cb67b9d9a2ded7d79b820f66e75bef0fa912efe50f941217` |
| Vista SP2 x86 | `usbd.sys` | 6.0.6001.18000 | 5,888 | `21be97010340e1377ad94d27a307d0a0f74b53fb4688012de807f7d4b859f204` |
| Vista SP2 x64 | `usbccgp.sys` | 6.0.6000.16386 | 95,232 | `01007c33fc53bd40f4a72bfa30611e914aaaec9b755dd640ac98cb4e1b08f2f3` |
| Vista SP2 x64 | `usbd.sys` | 6.0.6001.18000 | 7,680 | `a1a2c68d1cab2aa30671f06e6d958c1a8531cc2eca08adbace0efd81566ebedd` |
| 7 SP1 x86 | `usbccgp.sys` | 6.1.7601.17514 | 75,776 | `288cac9f4ac09deb2b30c6e3a6acf8d62a75576f62f0ec159d5e1b257419e9dc` |
| 7 SP1 x86 | `usbd.sys` | 6.1.7600.16385 | 5,888 | `b4ebfed3fbb1e6d82a77f93ea3bc761152c7b0c2b1b02b898b81a92f4d1f1e8b` |
| 7 SP1 x64 | `usbccgp.sys` | 6.1.7601.17514 | 98,816 | `5d6e404fe0ab875202ca1a3e8e9d2f4368df6accfa1c872ecfaf8399cba3a485` |
| 7 SP1 x64 | `usbd.sys` | 6.1.7600.16385 | 7,936 | `ea2ce29025259e9de945ce52c80a41c33024d7c2907aa1928480ec11fc852b08` |

`usbccgp.sys` imports `USBD_ParseConfigurationDescriptorEx` and
`USBD_CreateConfigurationRequestEx` on XP x64, and
`USBD_ParseConfigurationDescriptorEx`, `USBD_ParseDescriptors` and
`WMILIB.SYS` on Vista and 7.

### 6.3 Per target: Windows 98 SE, NUSB, Windows 2000 SP4, XP SP3 and ASIX

| Binary | IOCTLs sent to the PDO | `QUERY_INTERFACE` | URB functions it writes (length) | `usbd.sys` imports | Other IRPs it originates |
|---|---|---|---|---|---|
| 98 SE `hidusb` 4.10.2222 | `SUBMIT_URB`, `GET_PORT_STATUS` (`push 220013h` 0x10887), `RESET_PORT` (0x108F8) | none | 0x00 unconfigure (0x3C, 0x11882), 0x02 (0x18), 0x09 (0x48), 0x0B, 0x1B (0x50), 0x1C (0x50), 0x1E (0x18), 0x24, 0x28 | `CreateConfigurationRequestEx`, `ParseConfigurationDescriptorEx` | none; forwards `hidclass`'s |
| 98 SE `usbaudio` 4.10.2222 | `SUBMIT_URB` only (`push 220003h` 0x1208A and six stack stores) | none | 0x00 unconfigure (0x3C), 0x01, 0x02 (0x18), 0x07 (0x14, 0x12109), 0x0A (0x6C; 0xD8), 0x0B (0x50), 0x1B (0x50), 0x1C (0x50), 0x1E (0x18) | `CreateConfigurationRequestEx`, `ParseConfigurationDescriptorEx`, `ParseDescriptors` | none found |
| NUSB `usbstor` 4.90.3000.1 (3.3 = 3.6) | `SUBMIT_URB`, `GET_PORT_STATUS` (0x11AFD), `RESET_PORT` (0x11B46) | none | 0x00 unconfigure (0x18), 0x09 (0x48), 0x0B / 0x24 / 0x28 (0x50), 0x12 (0x50), 0x1B (0x50), 0x1E (0x18); no 0x01 | `CreateConfigurationRequestEx` | `IRP_MN_SET_POWER` through `PoRequestPowerIrp` (0x10383, 0x120C1) |
| 2000 SP4 `hidusb` 5.00.2142.1 | `SUBMIT_URB`, `GET_PORT_STATUS` (0x10DE9), `RESET_PORT` (0x10EA0) | none | 0x00 unconfigure, 0x02, 0x09 (0x48), 0x0B / 0x24 / 0x28 (0x50), 0x1B (0x50), 0x1C (0x50), 0x1E (0x18) | as 98 SE's | none; forwards `hidclass`'s |
| 2000 SP4 `usbstor` 5.00.2195.6655 | `SUBMIT_URB`, `GET_PORT_STATUS` (0x11B2D), `RESET_PORT` (0x11BDA) | none | 0x00 unconfigure (0x18), 0x01 (0x139EF, length `0x24 + 0x14 * pipes`), 0x09 (0x48), 0x0B / 0x24 / 0x28 (0x50), 0x12 (0x50), 0x1B (0x50), 0x1E (0x18) | `CreateConfigurationRequestEx` | `IRP_MN_SET_POWER` (0x10397, 0x1210B) |
| 2000 SP4 `usbaudio` 5.00.2150.1 | `SUBMIT_URB`, **`GET_BUS_INFO`** (`push 220420h` 0x15546) | none | 0x00 unconfigure, 0x01, 0x02 (0x18), 0x07 (0x14), 0x0A (0x6C; computed), 0x0B (0x50), 0x1B, 0x1C (0x50), 0x1E (0x18) | `ParseConfigurationDescriptorEx`, `CreateConfigurationRequestEx`, `ParseDescriptors` | none found |
| XP SP3 `hidusb` 5.1.2600.5512 | `SUBMIT_URB`, `GET_PORT_STATUS` (0x1080D), `RESET_PORT` (0x10891), **`SUBMIT_IDLE_NOTIFICATION`** (0x1095A, on the forwarded idle IRP) | none | 0x00 unconfigure (0x3C), 0x02 (0x18), 0x09 (0x48), 0x0B / 0x24 / 0x28, 0x1B (0x50), 0x1C (0x50), 0x1E (0x18), **0x2A** (0x50, 0x11E75) | `ParseConfigurationDescriptorEx`, `CreateConfigurationRequestEx` | none; forwards `hidclass`'s |
| XP SP3 `usbstor` 5.1.2600.5512 | `SUBMIT_URB`, `GET_PORT_STATUS` (0x107CB), `RESET_PORT` (0x1087C) | **USBDI, V1, Size 0x24** (0x13FE6-0x13FFD); calls `IsDeviceHighSpeed` and stores 0 when the query fails | 0x00 unconfigure (0x18), 0x01, 0x09 (0x48), 0x0B / 0x24 / 0x28 (0x50), 0x12 (0x50), 0x1B (0x50), 0x1E (0x18) | `CreateConfigurationRequestEx` | `QUERY_INTERFACE`; `IRP_MN_SET_POWER` (0x1042F, 0x142AD) |
| XP SP3 `usbaudio` 5.1.2600.5512 | `SUBMIT_URB` only | **USBDI, V0, Size 0x20** (0x1066C-0x10689); calls only `QueryBusTime` (0x1913C, guarded by a NULL check, else `0xC00000BB`); the routine returns the query's status | 0x00 unconfigure (0x3C), 0x01, 0x02 (0x18), 0x09 (0x48), 0x0A (0x6C; 0xD8; computed), 0x0B (0x50), 0x1B, 0x1C (0x50), 0x1E (0x18); **no 0x07** | `ParseConfigurationDescriptorEx`, `CreateConfigurationRequestEx`, `ParseDescriptors` | `QUERY_INTERFACE` |
| ASIX `AX88772.SYS` 3.0.3.12 (98 SE / ME) | `SUBMIT_URB` only | none | 0x09 (0x48), 0x0B (0x50), 0x0D (0x50, `FeatureSelector` 1 at 0x119A3), 0x17 (0x50; an embedded URB at extension +0x60A0C and stack URBs) | `CreateConfigurationRequestEx`, `ParseConfigurationDescriptorEx`, `GetUSBDIVersion` | none found |
| ASIX `ax88772.sys` 3.4.3.38 (2000 / XP, KMDF 1.7) | only through `Wdf01000.sys` (below) | none in the image | its own URBs 0x09 (0x48), 0x0D (0x50), 0x17 (0x50); plus whatever its KMDF calls build | `GetUSBDIVersion` | through KMDF, unread |
| `hidclass` 98 SE 4.10.2222, 2000 SP4 5.00.2195.6655, XP SP3 5.1.2600.5512 | none | none | none | none | `IRP_MN_QUERY_CAPABILITIES` (98 SE 0x14BB8, 2000 0x14C0D, XP 0x16B09); `IRP_MN_WAIT_WAKE` and `IRP_MN_SET_POWER` through `PoRequestPowerIrp`; these reach the PDO through `hidusb` |

Three details carry weight. **Windows 2000 SP4's `usbaudio.sys` asks for
bandwidth before it streams**: the routine at 0x154E0 allocates a 0x30-byte
block, sends `GET_BUS_INFO` with `Argument1` = the block, and compares its
need against `TotalBandwidth - ConsumedBandwidth` (`[block+4] - [block+8]`,
0x155D8), failing with `0xC000009A` when it does not fit (0x155E3); a failed
IOCTL returns its own status (0x155BD). Both halves of 25.2 found this
request, the second as a cross-check of the NT 5.0 baseline, and they agree.
**Both ASIX drivers refuse to continue unless `USBD_GetUSBDIVersion` reports
`USBDI_Version >= 0x101` and `Supported_USB_Version >= 0x100`** (98 at
0x12ED2 / 0x12EDF, 2000/XP at 0x1BFC9 / 0x1BFD9); that call is answered
inside the target's own `usbd.sys` and never reaches the PDO, and what each
`usbd.sys` returns there was not read. **The 2000/XP ASIX driver is a KMDF
1.7 client** (`WDF_BIND_INFO` at 0x1B320: size 0x20, version 1.7 build 6001,
387 functions, table at 0x1B358) whose calls, mapped through WDK 7.1's KMDF
1.9 `wdffuncenum.h`, include `WdfUsbTargetDeviceCreate`, `SelectConfig`,
`IsConnectedSynchronous`, `CyclePortSynchronously`, `SendUrbSynchronously`,
`FormatRequestForUrb` and `WdfUsbTargetPipeResetSynchronously`; its 17
`push 220003h` sites are an argument to a helper that ignores it, so the
IOCTL on the wire is set by `Wdf01000.sys`, which was not read.

### 6.4 Per target: Windows XP x64, Vista and Windows 7

RVAs below are the first site found; "x86 / x64" gives both builds where they
differ.

| Binary | IOCTLs sent to the PDO | `QUERY_INTERFACE` | URB functions | `usbd.sys` imports | Other IRPs |
|---|---|---|---|---|---|
| XP x64 `hidusb` 5.2.3790.1830 | `SUBMIT_URB` (@0x1E74), `RESET_PORT` (@0x1703), `GET_PORT_STATUS` (@0x1962), `SUBMIT_IDLE_NOTIFICATION` (@0x182F) | none (no USBDI GUID bytes in the image) | 0x00 (and unconfigure), 0x02, 0x09, 0x0B, 0x28 (@0x79EB), 0x1B, 0x1C, 0x1E, 0x2A (@0x788A) | `ParseConfigurationDescriptorEx`, `CreateConfigurationRequestEx` | power IRPs passed down (`PoCallDriver`) |
| XP x64 `usbstor` 5.2.3790.3959 | `SUBMIT_URB` (@0x23CA, 12 senders), `GET_PORT_STATUS` (@0x1AB0) then `RESET_PORT` (@0x1B7F), both in `USBSTOR_ResetDeviceWorkItem` | USBDI **V1, Size 0x48** (`USBSTOR_GetBusInterface` @0xC1D4-0xC1F5); calls `IsDeviceHighSpeed` (+0x40) @0xD480 | 0x00, 0x01, 0x09, 0x0B, 0x12 (`USBSTOR_GetMaxLun`), 0x1B, 0x1E | `CreateConfigurationRequestEx` | `PoRequestPowerIrp`; `IoBuildSynchronousFsdRequest` (its own pass-through) |
| XP x64 `usbaudio` 5.2.3790.3959 | `SUBMIT_URB` only (@0x7DF4, 10 senders) | USBDI **V0, Size 0x40** (`USBAudioGetUsbBusInterface` @0x9A0-0x9C8; GUID at RVA 0x121D8); calls `QueryBusTime` (+0x28) in `GetCurrentUSBFrame` | 0x00, 0x01, 0x02, 0x09 (MIDI), 0x0A, 0x0B, 0x1B, 0x1C (`SetSampleRate`), 0x1E | `ParseConfigurationDescriptorEx`, `CreateConfigurationRequestEx`, `ParseDescriptors` | - |
| Vista SP2 `hidusb` | `SUBMIT_URB`; `RESET_PORT` (`HumResetParentPort` @0x1619 / @0x19AE); `GET_PORT_STATUS` (`HumGetPortStatus` @0x1595 / @0x18F0); `SUBMIT_IDLE_NOTIFICATION` (`HumSendIdleNotificationRequest` x86 @0x16E2; inlined in `HumInternalIoctl` x64 @0x15F5) | none | 0x00 (`HumStopDevice`), 0x02, 0x09, 0x0B and 0x28 (arguments to `HumGetDescriptorRequest`), 0x1B, 0x1C, 0x1E, 0x2A (`HumGetMsGenreDescriptor`) | `ParseConfigurationDescriptorEx`, `CreateConfigurationRequestEx` | `IoInvalidateDeviceState` (-> `QUERY_PNP_DEVICE_STATE`) |
| Vista SP2 `usbstor` | `SUBMIT_URB`; `GET_PORT_STATUS` (`USBSTOR_IsDeviceConnected` @0x1FD1 / @0x59DE); `RESET_PORT` (@0x20EF / @0x573F) | USBDI **V1**: x86 Size 0x24 (@0xBE48-0xBE5F), x64 Size 0x48 (@0x110D6-0x110EF); then `IsDeviceHighSpeed` and `InterfaceDereference` | 0x00, 0x01, 0x09, 0x0B (the x86 helper has 0x24 / 0x28 arms selected by a recipient argument every call site passes as 0; x64 folds it to 0x0B), 0x12, 0x1B, 0x1E | `CreateConfigurationRequestEx` | `PoRequestPowerIrp`, `PoRegisterDeviceForIdleDetection` |
| Vista SP2 `usbaudio` | `SUBMIT_URB` only (`USBHwSubmitUrbToUsbdSynch` @0x5865 / @0x69E1) | USBDI **V1**: x86 Size 0x24 (`USBHwGetUsbBusInterface` @0x5E13-0x5E2D), x64 Size 0x48 (inlined in `USBDeviceStart` @0x634D-0x6367); calls `QueryBusTime` and `IsDeviceHighSpeed` | 0x00, 0x01, 0x02 and 0x1E (arguments to `USBHwAbortOrResetPipe`), 0x09, 0x0A, 0x0B, 0x1B and 0x1C (arguments to `USBHwGetSetProperty`) | `ParseConfigurationDescriptorEx`, `CreateConfigurationRequestEx`, `ParseDescriptors` | - |
| 7 SP1 `hidusb` | `SUBMIT_URB`, `RESET_PORT` (@0x1AB5 / @0x25E6), `GET_PORT_STATUS` (@0x1C39 / @0x2528), `SUBMIT_IDLE_NOTIFICATION` (x86 @0x1BA0 / x64 @0x214B) | none | as Vista: 0x00, 0x02, 0x09, 0x0B, 0x28, 0x1B, 0x1C, 0x1E, 0x2A | `ParseConfigurationDescriptorEx`, `CreateConfigurationRequestEx` | `IoInvalidateDeviceState` |
| 7 SP1 `usbstor` | `SUBMIT_URB`, `GET_PORT_STATUS` (@0x219E / @0x5D6F), `RESET_PORT` (@0x22BB / @0x5AD5), **`GET_TOPOLOGY_ADDRESS`** (`USBSTOR_IsDeviceConnectedToRootHub` @0xC244 / @0x12AB2; `Argument1` a 0x24-byte local, `sizeof(USB_TOPOLOGY_ADDRESS)`; called from `USBSTOR_FdoStartDevice`) | USBDI **V1**: x86 Size 0x24 (@0xC04D / 0xC070), x64 Size 0x48 (@0x127F9 / 0x127FE); `IsDeviceHighSpeed` and `InterfaceDereference` as Vista | as Vista: 0x00, 0x01, 0x09, 0x0B, 0x12, 0x1B, 0x1E | `CreateConfigurationRequestEx` | `PoRequestPowerIrp`, `PoRegisterDeviceForIdleDetection` |
| 7 SP1 `usbaudio` | `SUBMIT_URB`, **`RECORD_FAILURE`** (`USBHwLogStartFailure` @0xDD8C / @0x146B7, through `IoBuildDeviceIoControlRequest` with `InternalDeviceIoControl` TRUE; `Argument1` a pool buffer shaped as `USB_START_FAILDATA`, `ConnectStatus` (+0x0C) = 3) | USBDI **V1**: x86 Size 0x24 (@0xD46D-0xD485), x64 Size 0x48 in `USBDeviceStart` (@0x12DB2-0x12DC4); `QueryBusTime` and `IsDeviceHighSpeed` as Vista | as Vista: 0x00, 0x01, 0x02, 0x09, 0x0A, 0x0B, 0x1B, 0x1C, 0x1E | `ParseConfigurationDescriptorEx`, `CreateConfigurationRequestEx`, `ParseDescriptors` | `PoRequestPowerIrp`, `PoRegisterDeviceForIdleDetection`; the `PoCreatePowerRequest` family (not PDO-bound) |

The `usbd.sys` import set per driver is the same on XP x64, Vista and 7 in
both architectures, and equal to Windows 2000 SP4's: no target adds a
`usbd.sys` export, and none of the three drivers imports a `usbd.sys`
function that issues an IRP itself.

### 6.5 What a device PDO must answer

The union of 6.3 and 6.4, per target. "98 SE" covers the stock drivers,
NUSB's `usbstor.sys` and the 9x ASIX driver; "2000" and "XP" the x86 builds;
the NT 6.x columns both architectures. "-" means no driver read for that
target sends it; it does not mean the target's OS never does.

| Request at the PDO | 98 SE | 2000 | XP | XP x64 | Vista | 7 | Who sends it |
|---|---|---|---|---|---|---|---|
| `SUBMIT_URB` `0x220003` | yes | yes | yes | yes | yes | yes | every function driver |
| `GET_PORT_STATUS` `0x220013` | yes | yes | yes | yes | yes | yes | `hidusb`, `usbstor` |
| `RESET_PORT` `0x220007` | yes | yes | yes | yes | yes | yes | `hidusb`, `usbstor` |
| `GET_BUS_INFO` `0x220420` (`TotalBandwidth`, `ConsumedBandwidth` read; a failure fails the routine) | - | yes | - | - | - | - | 2000 `usbaudio` only |
| `SUBMIT_IDLE_NOTIFICATION` `0x220027` | - | - | yes | yes | yes | yes | `hidusb` |
| `GET_TOPOLOGY_ADDRESS` `0x22043F` (fill a 0x24-byte `USB_TOPOLOGY_ADDRESS`) | - | - | - | - | - | yes | 7 `usbstor` |
| `RECORD_FAILURE` `0x22002B` (`Argument1` a `USB_START_FAILDATA`) | - | - | - | - | - | yes | 7 `usbaudio` |
| `CYCLE_PORT` `0x22001F` | - | inferred | inferred | - | - | - | 2000/XP ASIX through KMDF, from the function name only |
| `QUERY_INTERFACE` USBDI V0 (`QueryBusTime`) | - | - | yes (0x20) | yes (0x40) | - | - | `usbaudio` |
| `QUERY_INTERFACE` USBDI V1 (adds `IsDeviceHighSpeed`) | - | - | yes (0x24, `usbstor`) | yes (0x48, `usbstor`) | yes | yes | `usbstor`; `usbaudio` on Vista and 7 |
| USBDI members called | - | - | `QueryBusTime`, `IsDeviceHighSpeed` | same | same, and `InterfaceDereference` | same | V2 and V3 are never requested |
| `IRP_MN_QUERY_CAPABILITIES` | yes | yes | yes | not read | not read | not read | `hidclass` (98 SE to XP); the PnP manager on every target |
| `IRP_MN_WAIT_WAKE` | yes | yes | yes | not read | not read | not read | `hidclass` (98 SE to XP) |
| `IRP_MN_SET_POWER` (device power, `PoRequestPowerIrp`) | yes | yes | yes | yes | yes | yes | `hidclass`, `usbstor`; 7 `usbaudio` |
| `QUERY_PNP_DEVICE_STATE` (through `IoInvalidateDeviceState`) | - | - | import only | - | yes | yes | Vista/7 `hidusb`; XP SP3 `hidusb` imports the call, not traced |
| URB 0x00 (configure through `usbd.sys`'s `USBD_CreateConfigurationRequestEx`; and the unconfigure) | yes | yes | yes | yes | yes | yes | all |
| URB 0x01 | yes | yes | yes | yes | yes | yes | `usbaudio`; `usbstor` from 2000 on (not NUSB's) |
| URB 0x02 | yes | yes | yes | yes | yes | yes | `hidusb`, `usbaudio` |
| URB 0x07 | yes | yes | - | - | - | - | `usbaudio` to 2000; from XP it uses `QueryBusTime` instead |
| URB 0x09 | yes | yes | yes | yes | yes | yes | `hidusb`, `usbstor`, ASIX; `usbaudio` from XP (MIDI) |
| URB 0x0A | yes | yes | yes | yes | yes | yes | `usbaudio` |
| URB 0x0B | yes | yes | yes | yes | yes | yes | all but the 2000/XP ASIX driver's own code |
| URB 0x0D, 0x17 | yes | yes | yes | - | - | - | ASIX only (no NT 6.x ASIX build was read) |
| URB 0x12 | yes | yes | yes | yes | yes | yes | `usbstor` |
| URB 0x1B | yes | yes | yes | yes | yes | yes | `hidusb`, `usbstor`, `usbaudio` |
| URB 0x1C | yes | yes | yes | yes | yes | yes | `hidusb`, `usbaudio` |
| URB 0x1E | yes | yes | yes | yes | yes | yes | `hidusb`, `usbstor`, `usbaudio` |
| URB 0x24 | yes | yes | yes | - | - | - | `hidusb`, `usbstor` to XP SP3; not found in the 5.2 and 6.x builds |
| URB 0x28 | yes | yes | yes | yes | yes | yes | `hidusb`; `usbstor` to XP SP3 |
| URB 0x2A | - | - | yes | yes | yes | yes | `hidusb` (`HumGetMsGenreDescriptor`) |
| `usbd.sys` exports imported | `CreateConfigurationRequestEx`, `ParseConfigurationDescriptorEx`, `ParseDescriptors`, `GetUSBDIVersion` (ASIX) | same | same | first three | first three | first three | answered inside `usbd.sys`, never at the PDO |

**Sent by no binary read on any target:** `GET_ROOTHUB_PDO`, `ENABLE_PORT`,
`GET_HUB_COUNT`, `GET_HUB_NAME`, `GET_CONTROLLER_NAME`, `GET_BUSGUID_INFO`,
`GET_PARENT_HUB_INFO`, `GET_DEVICE_HANDLE`, `GET_DEVICE_HANDLE_EX`,
`GET_TT_DEVICE_HANDLE`, `NOTIFY_IDLE_READY`, `REQ_GLOBAL_SUSPEND` /
`_RESUME` and `GET_DEVICE_CONFIG_INFO`; `CYCLE_PORT` by none except possibly
the KMDF ASIX driver; no `QUERY_INTERFACE` for any GUID but
`USB_BUS_INTERFACE_USBDI_GUID` (the hub and USBC configuration GUIDs were
searched for as bytes and found in no image); no USBDI version above 1; and
no `CONTROL_TRANSFER(_EX)`, `GET_STATUS_*`, `GET_CONFIGURATION`,
`GET_INTERFACE`, or the 0x30 / 0x31 `SYNC_RESET_PIPE` / `SYNC_CLEAR_STALL`
codes. Roadmap tasks 26-A.6 and 28-A.1 name `GET_DEVICE_HANDLE`; no class
driver in this reading sends it.

`IoGetDeviceProperty` (98 SE `hidclass` asks property 1, the hardware ids;
XP and XP x64 `usbaudio` property 0, the device description) targets the PDO
but is answered by the PnP manager from what the PDO's `QUERY_ID` and the
INF supplied, not by an IRP to the PDO at that moment.

### 6.6 What the reading leaves open

| Open item | Binds |
|---|---|
| **The 2000/XP ASIX driver's PDO traffic.** It goes out through `Wdf01000.sys` (KMDF 1.7), which was not read: the runtime ships inside `WdfCoInstaller01007.dll` and is not in `tools\` as a loose file, so reading it needs that DLL's embedded package unpacked first. `GET_PORT_STATUS`, `CYCLE_PORT`, `RESET_PIPE`, `ABORT_PIPE` on a target stop, the select-configuration and descriptor reads `WdfUsbTargetDeviceCreate` makes, and any USBDI `QUERY_INTERFACE` and its version are inferred from KMDF function names only. The function-index mapping assumes KMDF 1.9's enum is an append-only superset of 1.7's (two spot checks agree; no 1.7 header was read). | 26-A.6 (Windows 2000, 26-V.2's ASIX row); 28-A.1 (XP) |
| **Vista's `usbstor.sys` and `usbaudio.sys` are the SP2 DriverStore packages.** On Vista they are not in `System32\drivers` of the image; the DriverStore holds RTM, SP1 and SP2 packages side by side, and only SP2's was read. An install that carries an older package was not read, nor was any edition but Business (design record 11 found `usbport.sys` identical across editions). | 28-A.1 |
| **XP x64 `hidusb.sys` was read without symbols**: msdl serves no `hidusb.pdb/42225FEEAE1B40D79F68B90D93745D471` (404 for `.pdb`, `.pd_` and `file.ptr`). Its IOCTL and URB sets come from immediates and stores alone; they match Vista's and 7's `hidusb` exactly. | 28-A.1 |
| **The URB scan is straight-line.** Both halves resolved function codes through immediates, simple register moves and each helper's call sites; a code reaching `[urb+2]` through memory, a table, a branch join or more than one level of helper would be missed, so the URB lists are what was found, not a proof of absence. Three helper arguments passed in a register were not resolved (one each in 98 SE, 2000 and XP `usbaudio`'s class helper). The branch conditions of `hidusb`'s 0x1B / 0x1C alternatives and of `usbstor`'s 0x12 in `USBSTOR_GetMaxLun` were not read, nor the `SELECT_CONFIGURATION` that `USBD_CreateConfigurationRequestEx` builds inside `usbd.sys`. | 26-A.5 |
| **How a driver tolerates a failure of the newer requests was not read**, only that the request is sent: URB 0x2A, `SUBMIT_IDLE_NOTIFICATION`, `GET_TOPOLOGY_ADDRESS`, `RECORD_FAILURE`. Nor were the callers of XP `usbaudio`'s `QUERY_INTERFACE` routine (0x105D2) and 2000 `usbaudio`'s `GET_BUS_INFO` routine (0x154E0) traced: "a failure fails start" is not established, only that the routine returns the failure. XP `usbstor` tolerates a failed query (stores 0). | 26-A.6 (`GET_BUS_INFO`, the NT 5.0 requests); 28-A.1 (XP onward) |
| PnP and power IRPs the PnP and power managers originate and the FDOs merely forward (start, `QUERY_ID`, `QUERY_CAPABILITIES`, the S-to-D sequence, remove and surprise removal) were not catalogued; the PDO answers them on every target regardless. `hidclass.sys` was read on 98 SE, 2000 and XP only. | 26-A.4 |
| `usbstor.sys` (all three NT 5.x-era builds) calls `IoBuildSynchronousFsdRequest` with a major function held in a variable (NUSB 0x13F18, 2000 0x140C0, XP 0x156C6); its target was not traced. It appears to be the class driver's own read path, not the PDO. | 26-A.5 |
| Windows ME's class drivers were not read here. | 28-A.1 |

## 7. The Windows 98 export evidence (task 25.3)

The miniport imported almost nothing because `usbport.sys` did the work; the
HCD creates device objects, connects its interrupt, allocates pool and its
own common buffer, and maps transfer buffers. Every one of those imports must
resolve on a stock Windows 98 SE, where a missing export is a load failure
and not a runtime error. This section is the evidence for them, read before
any of them is written (the Phase 25 checkpoint refuses "a Windows 98 export
assumed from Windows 2000's").

**Method.** Static: nothing Microsoft was executed. A subagent's read,
2026-10-02, at `5086e2d`, not re-read line by line by the coordinator. It
used **the import gate's own functions**, not re-implementations: a scratch
script outside the repository dot-sourced
`scripts\import-gate\evidence-common.ps1` and lifted `Initialize-Dumpbin`,
`Invoke-Dumpbin`, `Get-ImportPairs`, `Get-ExportNames`, `Get-Win2kBaseline`,
`Get-Win98Precedent`, `Get-NtkernNames` and `Test-NtkernName` out of
`check-imports.ps1` by PowerShell AST, so that script's main body never ran.

- **`w2k-export`**: the ten Windows 2000 SP4 images of
  `win2k-baselines.expected`, authenticated by length, version and SHA-256
  first (0 errors). An `ntoskrnl.exe` pair must be exported by both
  `ntoskrnl.exe` and `ntkrnlmp.exe`, a `hal.dll` pair by all eight HALs, as in
  `Test-Image`. Every candidate that is a `w2k-export` at all is one in every
  image.
- **`win98-precedent`**: the tracked `win98-evidence.list` (0 errors) **and**
  a scratch manifest in the same format holding the draft rows of Appendix A
  (0 errors: the draft rows already pass the gate's parser and identity
  check). `Get-Win98Precedent` over the union returned 414 pairs from 86
  files: 67 Microsoft WDM drivers newly staged from the Windows 98 SE CD, the
  two SE files already listed (`usbd.sys`, `usbhub.sys`), NUSB 3.3's eight
  listed files and NUSB 3.6's nine files not in 3.3.
- **`ntkern-name`**: `Get-NtkernNames` and `Test-NtkernName` (NUL-delimited,
  case-sensitive) on the authenticated `tools\win98se-extracted\ntkern.vxd`.
- **The module per pair is the one the Windows 2000 DDK link records.** The
  scaffold's `sources` (read as `src\hcd\sources`; it is `src\sources` since
  the HCD took over `src\`) is `TARGETTYPE=DRIVER` with no
  `DRIVERTYPE=WDM`, so `<ntddk.h>` is the DDK's `inc\ddk\ntddk.h`, and
  `bin\makefile.def` links `ntoskrnl.lib hal.lib wmilib.lib` in that order.
  The headers only say `dllimport`; the module is whichever library defines
  the name first, so the module column comes from `dumpbin /exports` of
  `tools\ntddk\libfre\i386\{ntoskrnl,hal,wmilib}.lib` in link order.
  (`READ_REGISTER_*` is declared `NTHALAPI` yet lives in `ntoskrnl.lib`,
  matching the shipping `USBEHCI.SYS` the miniport's allowlist (1.2.0.0)
  cited.) The macro rewrites were read from `ntddk.h`'s x86 branch:
  `IoCallDriver` -> `IofCallDriver` (line 14884), `IoCompleteRequest` ->
  `IofCompleteRequest` (14912), `Ob{Reference,Dereference}Object` -> `Obf*`
  (18338-18341); `KeAcquireSpinLock` / `KeReleaseSpinLock` -> HAL `Kf*`
  (9763-9764), the `...AtDpcLevel` / `...FromDpcLevel` pair -> **ntoskrnl**
  `Kef*` (9734-9735), `KeRaiseIrql` / `KeLowerIrql` -> HAL `Kf*`
  (9838-9839); the four `Interlocked*` are `NTKERNELAPI FASTCALL` imports and
  `InterlockedExchangeAdd` is `__inline`; `ExInterlocked*List` -> `Exf*`
  (5136-5138); `KeQueryTickCount` is an x86 macro reading the **data**
  export `KeTickCount`; `MmGetSystemAddressForMdl` -> `MmMapLockedPages`
  (12349) and the `...Safe` form -> `MmMapLockedPagesSpecifyCache`;
  `RtlCopyMemory` / `RtlZeroMemory` / `RtlFillMemory` / `RtlMoveMemory` /
  `RtlEqualMemory` -> `memcpy` / `memset` / `memset` / `memmove` / `memcmp`
  (1576-1580); `POOL_TAGGING` defines `ExAllocatePool(a,b)` as
  `ExAllocatePoolWithTag(a,b,' kdD')` (19063), does not rewrite
  `ExFreePool`, and neither header declares `ExFreePoolWithTag`.

Windows 98 SE source: `Win98SE.iso`, 656,203,776 bytes, SHA-256
`C048C31E414E3FE4FEC1CDBBA42C62452F7DF5E9F3AFD62464823DE4DEF7F584`. 7-Zip
extracted every `*.sys` of the `win98\*.CAB` chains (208 files); of the 169
that import `NTOSKRNL.EXE` or `HAL.DLL`, 97 are NDIS miniports (excluded:
they load through NDIS, so their kernel imports are not precedent of the
same kind), 3 are third-party (`cinemst2.sys`, `vdmindvd.sys`, `vbidec.sys`;
excluded) and 69 are Microsoft WDM drivers. Two of the 69 are already listed;
the other 67 were copied into the git-ignored `tools\win98se-extracted\wdm\`,
each length agreeing with its `layout.inf` / `layout1.inf` / `layout2.inf`
entry from `PRECOPY1.CAB`. NUSB 3.6 is `tools\nusb36-extracted` (installer
`tools\nusb36e.exe`, 992,768 bytes, SHA-256
`42B13CE440CC6528600A7B085226050D855C50199ECDE6CBD6F31CBF83A69621`).

**A caveat on NUSB 3.6.** NUSB 3.6 installs `wdmstub.sys` (Walter Oney
Software, 5.00.006), which adds exports to NTKERN at load. A name it carries
as a string but does not import is, by its stated purpose ("WDM stub
functions for Windows 98", its FileDescription), a name it supplies; that
inference is from the purpose, not from reading its code. A NUSB 3.6
precedent for such a name is evidence for NUSB 3.6's platform, not for stock
98 SE, and the table flags it per row. No verdict in it rests on NUSB 3.6
alone. `wdmstub.sys`'s own imports are stock evidence: it must load before it
can stub anything.

### 7.1 The answers

**DMA pairing: `IoGetDmaAdapter` and the adapter's own `DMA_OPERATIONS`
table, not `HalGetAdapter` with `HalAllocateCommonBuffer`.** Windows 98 SE's
own USB 1.1 host controller drivers, `uhcd.sys` and `openhci.sys` 4.10.2222,
both import `NTOSKRNL.EXE!IoGetDmaAdapter` (hint 4A) and reach every other
DMA service through the table it returns. Seven stock 98 SE binaries
(`uhcd.sys`, `openhci.sys`, `ohci1394.sys`, `aha894x.sys` 4.10.1998 at hint
45, `tilynx.sys`, `portcls.sys`, `stream.sys`), NUSB 3.3's `USBPORT.SYS` and
NUSB 3.6's `uhcd`, `openhci` and `OHCI1394` import it. **None of the 86
binaries imports `HalGetAdapter`, `HalAllocateCommonBuffer` or
`HalFreeCommonBuffer`**; their only Windows 98 tag is `ntkern-name`, which is
module-blind. This settles the pairing `win98-wdm.md` left unverified, and
the two routes are never mixed (that document's warning stands).

The two HCDs' disassembly (`dumpbin /disasm`, addresses as printed at image
base 0x10000; `DMA_ADAPTER.DmaOperations` at +4 and the slot order from
`ntddk.h`'s `DMA_OPERATIONS`: +4 `PutDmaAdapter`, +8 `AllocateCommonBuffer`,
+0xC `FreeCommonBuffer`, +0x10 `AllocateAdapterChannel`, +0x14
`FlushAdapterBuffers`, +0x18 `FreeAdapterChannel`, +0x1C `FreeMapRegisters`,
+0x20 `MapTransfer`):

| | `openhci.sys` 4.10.2222 | `uhcd.sys` 4.10.2222 |
|---|---|---|
| `IoGetDmaAdapter` | `0x1152B`; `DEVICE_DESCRIPTION` with `Master`, `ScatterGather`, `Dma32BitAddresses` = 1, `InterfaceType` 5 (PCIBus), `MaximumLength` 0x80000000 | `0x16D7A`; the same three flags, `InterfaceType` 5, `DmaWidth` 2, `MaximumLength` 0xFFFFFFFF |
| `AllocateCommonBuffer` (+8) | `0x1155A` (0x1000 bytes, cache-enabled), `0x10514` | `0x13487`, `0x137A1`, `0x167E5` (0x2000 bytes) |
| `FreeCommonBuffer` (+0xC) | `0x1145F` | `0x1670F`, `0x16797` |
| `PutDmaAdapter` (+4) | `0x11476` | `0x167B0` |
| `AllocateAdapterChannel` (+0x10) | `0x13CAA` | `0x12D2F` |
| `MapTransfer` (+0x20) | `0x1496E` | `0x12C66` |
| `FlushAdapterBuffers` (+0x14) | `0x12BEA` | `0x12A85` |
| `FreeMapRegisters` (+0x1C) | `0x12BFD` | `0x12AB7` |

Neither calls slot +0x18 (`FreeAdapterChannel`, which a bus master does not
need: it frees map registers instead) nor +0x2C / +0x30
(`GetScatterGatherList` / `PutScatterGatherList`). Whether Windows 98's
adapter table fills those two slots was not read.

**The `ntddk.h` trap.** With `USE_DMA_MACROS` undefined (the DDK forces it
only on `_AXP64_`), `ntddk.h` declares `HalAllocateCommonBuffer`,
`HalFreeCommonBuffer`, `IoMapTransfer`, `IoFlushAdapterBuffers`,
`IoFreeMapRegisters` and `IoFreeAdapterChannel` as `NTHALAPI` imports (lines
18120-18196), and `HalGetAdapter` as `NTHALAPI` under `!NO_LEGACY_DRIVERS`;
`wdm.h` makes the same names `__inline` wrappers over
`DmaAdapter->DmaOperations` (line 10743). Under the HCD's `ntddk.h`, writing
`HalAllocateCommonBuffer(...)` therefore produces a `HAL.dll` import even
with an `IoGetDmaAdapter` adapter. The HCD names the table slots, not the
`Hal*` functions (or defines `USE_DMA_MACROS` in its `sources`); either way
the import gate would refuse a stray HAL import.

**Transfer-buffer mapping: the map-register route through the same table.**
`AllocateAdapterChannel`, `MapTransfer` per segment, `FlushAdapterBuffers`
and `FreeMapRegisters` are what both Windows 98 SE HCDs do, and none of them
is an import. The other route has a hole: **`MmGetPhysicalAddress` is
imported by none of the 86 binaries** - not by Windows 98 SE's own stack, not
by NUSB's Windows 2000 `USBPORT.SYS` - and its only tag is `ntkern-name`, the
weakest evidence of anything the HCD would call on its transfer path. The
legacy names for the map-register route (`IoMapTransfer`,
`IoFlushAdapterBuffers`, `IoFreeMapRegisters` from `HAL.dll`,
`IoAllocateAdapterChannel` from `ntoskrnl.exe`) are likewise `w2k-export` and
`ntkern-name` only. MDLs are still needed, because a URB may carry only a
virtual `TransferBuffer`: on stock 98 SE `usbd.sys` builds that MDL, importing
`IoAllocateMdl` (hint 2E), `IoFreeMdl` and `MmBuildMdlForNonPagedPool`, and
the HCD inherits the job; the three have 12, 15 and 7 stock precedents.
`IoBuildPartialMdl`, for splitting a long transfer, has stock `1394bus.sys`
and `sbp2port.sys` and NUSB's `USBSTOR.SYS`; `MmProbeAndLockPages` /
`MmUnlockPages`, needed only if a caller hands an unlocked buffer, have stock
`ks.sys` and `kmixer.sys`. For task 25.7: on x86 with `Dma32BitAddresses` and
RAM below 4 GB, `MapTransfer` returns the physical address with no copy, so
"direct DMA from the URB's MDL" is what the map-register route gives, and
bounce buffers are what the table supplies implicitly when map registers are
real. How Windows 98's adapter implements map registers, and how many
`NumberOfMapRegisters` it grants, was not read.

**Pool: the evidence runs opposite to the miniport's compatibility-header
choice.** `ExAllocatePoolWithTag` is imported by 62 stock 98 SE drivers
(both HCDs, `usbd.sys`, `usbhub.sys`, ...) and `ExFreePool` by 61. The
untagged `ExAllocatePool` is imported by **none** of the 86 binaries
(`ntkern-name` only). `ExFreePoolWithTag` has **no Windows 98 evidence at
all**: no binary imports it, it is absent from `ntkern.vxd`'s names, the
Windows 2000 DDK headers do not declare it, and it is one of the names NUSB
3.6's `wdmstub.sys` carries but does not import. The HCD's pair is
`ExAllocatePoolWithTag` + `ExFreePool`.

### 7.2 The `ntkern-name` scan misses at scale

Nineteen candidate names that a stock 98 SE driver imports are absent from
`ntkern.vxd`'s NUL-delimited strings, among them `KeInitializeDpc`,
`KeInsertQueueDpc`, `KeSetTimer`, `KeSetTimerEx`, `KfRaiseIrql`,
`KfLowerIrql`, `KeGetCurrentIrql`, `KeQuerySystemTime` and `KeTickCount`.
This extends the import gate header's `KeGetCurrentIrql` example ("a miss is
no information"), and it shows that the `legal-provenance.md` section 4 row
"Windows 98 SE's `NTKERN.VXD` exports no `KeInsertQueueDpc` by name" is a
statement about the name table, not about the export: `uhcd.sys` and
`openhci.sys` import `NTOSKRNL.EXE!KeInsertQueueDpc` (hint 7C). The scan is
also **module-blind**: a hit does not say whether the name resolves as
`ntoskrnl.exe` or `hal.dll`, which is the trap the allowlist header warns
about. So a row whose only Windows 98 tag is `ntkern-name` is the weakest
row the HCD can carry.

### 7.3 The evidence table, in summary

205 candidate pairs were read: the brief's list (pool, device objects,
interrupts and DPCs, DMA, transfer-buffer mapping, registry, the
device-interface calls) and the extensions the reading added. Appendix A
carries the full table and the draft `win98-evidence.list` rows it rests on.
Counted by verdict:

| Verdict | Pairs | Of which `wdmstub.sys` supplies the name |
|---|---|---|
| evidenced: `w2k-export` and a stock 98 SE `win98-precedent` | 145 | - |
| evidenced: `w2k-export` and `ntkern-name` only (module-blind; weakest) | 32 | 6 |
| evidenced: a `WMILIB.SYS` pair imported by 98 SE's own `wmilib.sys` users | 2 | 2 |
| `w2k-export` only: needs a Windows 98 tag or a load test | 23 | 8 |
| no import under the Windows 2000 DDK (`PoSetDeviceBusy` is a macro, `memcmp` is in no DDK import library, `KeAcquireInterruptSpinLock` is XP-era) | 3 | - |
| **Total** | **205** | |

`memcmp` is in none of the three import libraries and is not exported by
Windows 2000 SP4's kernels, so `RtlEqualMemory` must stay an intrinsic or be
replaced by `RtlCompareMemory`. A miss in any column is never read as
"absent on 98".

### 7.4 The HCD core set

These 69 rows are what 26-A.1 adds to
`scripts\import-gate\xhci98-imports.allow` as 26-A.2's FDO comes to import
them; 26-A.1 and 26-A.2 added the rows the controller FDO imports (2026-10-03),
with this table's evidence text verbatim, and the rest wait for the tasks
that call them. Each names the strongest stock 98 SE
precedent with its hint and assumes the tier A and B evidence-list rows of
Appendix A are listed. In the file each row is flavour `all`, and its
`REQUIREMENT` is Phase 26's to decide per row (25.8's empty scaffold needs
none of them).

| `MODULE!SYMBOL` | evidence |
|---|---|
| `ntoskrnl.exe!ExAllocatePoolWithTag` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!ExAllocatePoolWithTag, hint 7) (+61 other 98 SE); ntkern-name |
| `ntoskrnl.exe!ExFreePool` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!ExFreePool, hint C) (+60 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoCreateDevice` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IoCreateDevice, hint 38) (+43 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoDeleteDevice` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IoDeleteDevice, hint 3D) (+41 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoAttachDeviceToDeviceStack` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IoAttachDeviceToDeviceStack, hint 30) (+36 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoDetachDevice` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IoDetachDevice, hint 3F) (+26 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IofCallDriver` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IofCallDriver, hint 68) (+49 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IofCompleteRequest` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!IofCompleteRequest, hint 69) (+43 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoInvalidateDeviceRelations` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!IoInvalidateDeviceRelations, hint 4F) (+3 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoInvalidateDeviceState` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!IoInvalidateDeviceState, hint 50) (+1 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoRegisterDeviceInterface` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!IoRegisterDeviceInterface, hint 54) (+9 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoSetDeviceInterfaceState` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!IoSetDeviceInterfaceState, hint 5A) (+8 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoGetDeviceProperty` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!IoGetDeviceProperty, hint 49) (+7 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoOpenDeviceRegistryKey` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!IoOpenDeviceRegistryKey, hint 53) (+18 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoAcquireCancelSpinLock` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IoAcquireCancelSpinLock, hint 29) (+9 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoReleaseCancelSpinLock` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IoReleaseCancelSpinLock, hint 57) (+12 other 98 SE); ntkern-name |
| `ntoskrnl.exe!PoCallDriver` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!PoCallDriver, hint AE) (+21 other 98 SE); ntkern-name |
| `ntoskrnl.exe!PoStartNextPowerIrp` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!PoStartNextPowerIrp, hint B6) (+25 other 98 SE); ntkern-name |
| `ntoskrnl.exe!PoRequestPowerIrp` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!PoRequestPowerIrp, hint B3) (+14 other 98 SE); ntkern-name |
| `ntoskrnl.exe!PoSetPowerState` | w2k-export; win98-precedent usbd.sys 4.10.2222 (NTOSKRNL.EXE!PoSetPowerState, hint B4) (+5 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoConnectInterrupt` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IoConnectInterrupt, hint 37) (+6 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoDisconnectInterrupt` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IoDisconnectInterrupt, hint 40) (+6 other 98 SE); ntkern-name |
| `ntoskrnl.exe!KeSynchronizeExecution` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeSynchronizeExecution, hint 92) (+7 other 98 SE); ntkern-name |
| `ntoskrnl.exe!KeInitializeDpc` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeInitializeDpc, hint 73) (+24 other 98 SE) |
| `ntoskrnl.exe!KeInsertQueueDpc` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeInsertQueueDpc, hint 7C) (+10 other 98 SE) |
| `ntoskrnl.exe!KeRemoveQueueDpc` | w2k-export; win98-precedent ks.sys 4.10.2222 (NTOSKRNL.EXE!KeRemoveQueueDpc, hint 8A) (+1 other 98 SE) |
| `ntoskrnl.exe!KeInitializeSpinLock` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeInitializeSpinLock, hint 77) (+32 other 98 SE); ntkern-name |
| `HAL.dll!KfAcquireSpinLock` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (HAL.DLL!KfAcquireSpinLock, hint 6) (+31 other 98 SE); ntkern-name |
| `HAL.dll!KfReleaseSpinLock` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (HAL.DLL!KfReleaseSpinLock, hint 9) (+31 other 98 SE); ntkern-name |
| `ntoskrnl.exe!KefAcquireSpinLockAtDpcLevel` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KefAcquireSpinLockAtDpcLevel, hint 96) (+12 other 98 SE); ntkern-name |
| `ntoskrnl.exe!KefReleaseSpinLockFromDpcLevel` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KefReleaseSpinLockFromDpcLevel, hint 97) (+12 other 98 SE); ntkern-name |
| `HAL.dll!KfRaiseIrql` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (HAL.DLL!KfRaiseIrql, hint 8) (+8 other 98 SE) |
| `HAL.dll!KfLowerIrql` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (HAL.DLL!KfLowerIrql, hint 7) (+9 other 98 SE) |
| `HAL.dll!KeGetCurrentIrql` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (HAL.DLL!KeGetCurrentIrql, hint 3) (+15 other 98 SE) |
| `ntoskrnl.exe!InterlockedIncrement` | w2k-export; win98-precedent openhci.sys 4.10.2222 (NTOSKRNL.EXE!InterlockedIncrement, hint 28) (+35 other 98 SE); ntkern-name |
| `ntoskrnl.exe!InterlockedDecrement` | w2k-export; win98-precedent openhci.sys 4.10.2222 (NTOSKRNL.EXE!InterlockedDecrement, hint 26) (+33 other 98 SE); ntkern-name |
| `ntoskrnl.exe!InterlockedExchange` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!InterlockedExchange, hint 27) (+22 other 98 SE); ntkern-name |
| `ntoskrnl.exe!InterlockedCompareExchange` | w2k-export; win98-precedent hidclass.sys 4.10.2222 (NTOSKRNL.EXE!InterlockedCompareExchange, hint 25) (+6 other 98 SE); ntkern-name |
| `ntoskrnl.exe!KeInitializeEvent` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeInitializeEvent, hint 74) (+48 other 98 SE); ntkern-name |
| `ntoskrnl.exe!KeSetEvent` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeSetEvent, hint 8E) (+42 other 98 SE); ntkern-name |
| `ntoskrnl.exe!KeClearEvent` | w2k-export; win98-precedent ks.sys 4.10.2222 (NTOSKRNL.EXE!KeClearEvent, hint 6E) (+5 other 98 SE); ntkern-name |
| `ntoskrnl.exe!KeWaitForSingleObject` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeWaitForSingleObject, hint 95) (+51 other 98 SE); ntkern-name |
| `ntoskrnl.exe!KeInitializeTimer` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeInitializeTimer, hint 78) (+20 other 98 SE) |
| `ntoskrnl.exe!KeInitializeTimerEx` | w2k-export; win98-precedent ks.sys 4.10.2222 (NTOSKRNL.EXE!KeInitializeTimerEx, hint 79) (+1 other 98 SE) |
| `ntoskrnl.exe!KeSetTimer` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeSetTimer, hint 90) (+18 other 98 SE) |
| `ntoskrnl.exe!KeSetTimerEx` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeSetTimerEx, hint 91) (+5 other 98 SE) |
| `ntoskrnl.exe!KeCancelTimer` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeCancelTimer, hint 6D) (+20 other 98 SE) |
| `ntoskrnl.exe!KeDelayExecutionThread` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeDelayExecutionThread, hint 6F) (+12 other 98 SE); ntkern-name |
| `ntoskrnl.exe!KeQuerySystemTime` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeQuerySystemTime, hint 80) (+9 other 98 SE) |
| `ntoskrnl.exe!KeQueryTimeIncrement` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!KeQueryTimeIncrement, hint 81) (+5 other 98 SE); ntkern-name |
| `HAL.dll!KeStallExecutionProcessor` | w2k-export; win98-precedent openhci.sys 4.10.2222 (HAL.DLL!KeStallExecutionProcessor, hint 5) (+15 other 98 SE); ntkern-name |
| `ntoskrnl.exe!ExQueueWorkItem` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!ExQueueWorkItem, hint 17) (+23 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoGetDmaAdapter` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IoGetDmaAdapter, hint 4A) (+6 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoAllocateMdl` | w2k-export; win98-precedent usbd.sys 4.10.2222 (NTOSKRNL.EXE!IoAllocateMdl, hint 2E) (+11 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoFreeMdl` | w2k-export; win98-precedent usbd.sys 4.10.2222 (NTOSKRNL.EXE!IoFreeMdl, hint 43) (+14 other 98 SE); ntkern-name |
| `ntoskrnl.exe!MmBuildMdlForNonPagedPool` | w2k-export; win98-precedent usbd.sys 4.10.2222 (NTOSKRNL.EXE!MmBuildMdlForNonPagedPool, hint 99) (+6 other 98 SE); ntkern-name |
| `ntoskrnl.exe!MmMapLockedPages` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!MmMapLockedPages, hint 9D) (+15 other 98 SE); ntkern-name |
| `ntoskrnl.exe!MmMapIoSpace` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!MmMapIoSpace, hint 9C) (+7 other 98 SE); ntkern-name |
| `ntoskrnl.exe!MmUnmapIoSpace` | w2k-export; win98-precedent acpi.sys 4.10.2222 (NTOSKRNL.EXE!MmUnmapIoSpace, hint A6) (+5 other 98 SE); ntkern-name |
| `ntoskrnl.exe!READ_REGISTER_ULONG` | w2k-export; win98-precedent openhci.sys 4.10.2222 (NTOSKRNL.EXE!READ_REGISTER_ULONG, hint C0) (+3 other 98 SE); ntkern-name |
| `ntoskrnl.exe!WRITE_REGISTER_ULONG` | w2k-export; win98-precedent openhci.sys 4.10.2222 (NTOSKRNL.EXE!WRITE_REGISTER_ULONG, hint ED) (+3 other 98 SE); ntkern-name |
| `ntoskrnl.exe!RtlInitUnicodeString` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!RtlInitUnicodeString, hint D7) (+47 other 98 SE); ntkern-name |
| `ntoskrnl.exe!RtlFreeUnicodeString` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!RtlFreeUnicodeString, hint D3) (+22 other 98 SE); ntkern-name |
| `ntoskrnl.exe!ZwClose` | w2k-export; win98-precedent usbhub.sys 4.10.2222 (NTOSKRNL.EXE!ZwClose, hint EF) (+29 other 98 SE); ntkern-name |
| `ntoskrnl.exe!RtlQueryRegistryValues` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!RtlQueryRegistryValues, hint DA) (+8 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoAllocateIrp` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IoAllocateIrp, hint 2D) (+30 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoFreeIrp` | w2k-export; win98-precedent uhcd.sys 4.10.2222 (NTOSKRNL.EXE!IoFreeIrp, hint 42) (+31 other 98 SE); ntkern-name |
| `ntoskrnl.exe!IoCancelIrp` | w2k-export; win98-precedent openhci.sys 4.10.2222 (NTOSKRNL.EXE!IoCancelIrp, hint 35) (+14 other 98 SE); ntkern-name |
| `ntoskrnl.exe!DbgPrint` | w2k-export; win98-precedent ks.sys 4.10.2222 (NTOSKRNL.EXE!DbgPrint, hint 1) (+1 other 98 SE); ntkern-name |

**What the working tree held at Phase 25's close** (kept as written; since
26-A.1 and 26-A.2, 2026-10-03, `[imports]` carries the rows the controller
FDO imports, and the pool pair is still denied because `hcd_pool.c` is not
linked until the first allocation). Since the owner's decision of
2026-10-02 that the HCD takes over `src\` and the name `xhci98.sys`,
`scripts\import-gate\xhci98-imports.allow` is the HCD's allowlist. It held
an empty `[imports]` (task 25.8's scaffold imports nothing) and a `[deny]`
section whose pool rows already cite this task: `ExAllocatePool` (not the HCD's spelling), `ExAllocatePoolWithTag` and
`ExFreePool` (the HCD's entry points, denied until 26-A.1 adds their rows
with the site rule), and `ExFreePoolWithTag` (denied for good). When 26-A.1
adds rows, this evidence lifts **only** `ExAllocatePoolWithTag` and
`ExFreePool`. The reading also puts the legacy DMA names and
`MmGetPhysicalAddress` in that `[deny]`: `MmGetPhysicalAddress`,
`HalGetAdapter`, `HalAllocateCommonBuffer` and `HalFreeCommonBuffer` are in
it already; `IoMapTransfer`, `IoFlushAdapterBuffers`, `IoFreeMapRegisters`
and `IoAllocateAdapterChannel` are not yet, and none of the eight is in the
amd64 file's `[deny]`. The amd64 sibling `xhci98-imports-amd64.allow` carries the one `/GS`
`KeBugCheckEx` row and denies the four pool names pending the same rows.

### 7.5 The pool rule for the HCD

This replaces the miniport's "allocate no pool" (decisions table, "Pool and
DMA"). That rule left the tree with the miniport; it remains the rule of the
1.2.0.0 sources, on their branch and under `releases\1.2.0.0`.

1. **The pair is `ExAllocatePoolWithTag` + `ExFreePool`.** Never the untagged
   `ExAllocatePool` (no Windows 98 binary imports it; `ntkern-name` only) and
   never `ExFreePoolWithTag` (no Windows 98 evidence; not declared in the
   Windows 2000 DDK; supplied by NUSB 3.6's `wdmstub.sys`). One project pool
   tag for every allocation, chosen once and recorded (a four-character
   constant in a pool header), so a leak is attributable in a debugger on
   Windows 2000. `xhci_compat.h` keeps its `#undef ExAllocatePool`, so a
   stray untagged call resolves to `ExAllocatePool`, which the allowlist
   refuses: the miniport's trip-wire, guarding the opposite pair.
2. **`NonPagedPool` only**, at `PASSIVE_LEVEL` or `DISPATCH_LEVEL`, never in
   the ISR and never in a `KeSynchronizeExecution` callback. No `PagedPool`:
   Windows 98's pager and its `DO_POWER_PAGABLE` interactions are unread.
3. **Lifetime-scoped sites, not the transfer path.** Allowed: device objects
   and their extensions (which `IoCreateDevice` allocates for the HCD),
   per-controller state at `START_DEVICE`, per-device, per-endpoint, per-hub
   and per-function-PDO records at enumeration, configuration and interface
   copies, and MDLs for URBs that arrive without one. Not allowed: an
   allocation per transfer on the submit or completion path. TDs, transfer
   records and bounce segments come from structures preallocated at
   enumeration, or from a lookaside list initialised there if 25.7 needs one
   (`ExInitializeNPagedLookasideList` and the two SList pairs have stock
   precedents in `ks.sys` and `ohci1394.sys`).
4. **Two files own every call.** A pool file (`src\hcd_pool.c`: the
   allocate and free wrappers, the tag, an outstanding-allocation counter the
   `XHCISNAP` door reports, and the only `ExAllocatePoolWithTag`,
   `ExFreePool`, `IoAllocateMdl` and `IoFreeMdl` calls) and a DMA file
   (`src\hcd_dma.c`: the only `IoGetDmaAdapter` call and the only
   `DmaOperations->` calls - `AllocateCommonBuffer`, `FreeCommonBuffer`,
   `PutDmaAdapter`, `AllocateAdapterChannel`, `MapTransfer`,
   `FlushAdapterBuffers`, `FreeMapRegisters`). The common buffer's size and
   layout stay design record 04's arithmetic.
5. **The lifted core stays pool-free.** The pure files carried over from the
   miniport (`xhci_mem.c`, `xhci_ring.c` and their siblings) allocate nothing:
   they are named in no pool row's `SITES`, so the object check below refuses
   an allocation in any of them, and no `#ifdef` may hide one from it. (The
   25.3 draft argued this from the miniport's gate, which also linked those
   files; that guard left the tree with the miniport, and the site check is
   what replaces it.)
6. **Every allocation is released** on `IRP_MN_REMOVE_DEVICE` (and on the
   Windows 98 out-of-sequence remove), and the counter reads 0 after it: a
   host vector, and a 26-A checkpoint clause.

How the gate enforces "named sites" (proposed here; **the `SITES=` field was
built in 26-A.1**, with self-tests, and per object it reads every undefined
external, not only `__imp_` thunks; over WDK 7.1's link-time-code-generation
amd64 objects, which list no symbols, it reads each object's `.c` source
instead. The `DmaOperations->` token check below is not built yet):

- **A per-row `SITES` field** (or a `[sites]` section) in the allowlist,
  naming the object files allowed to reference a pair, e.g.
  `ntoskrnl.exe!ExAllocatePoolWithTag ... SITES=hcd_pool.obj`. The gate
  checks it with its own dumper: `dumpbin /symbols` over the link's
  `obj*\i386\*.obj`, failing when an object outside `SITES` has an
  `UNDEF External` for the import thunk (`__imp__ExAllocatePoolWithTag@12`;
  `__imp_@IofCallDriver@8` for a fastcall). That holds the rule per object
  rather than per binary, without parsing C. A row without `SITES` is
  unrestricted.
- **A source check for what is not an import.** `DmaOperations->` calls
  produce no import, so the object check cannot see them; a token check
  (`AllocateCommonBuffer`, `MapTransfer`, `AllocateAdapterChannel`,
  `GetScatterGatherList`) restricted to the DMA file covers them, run beside
  the INF gate. `GetScatterGatherList` / `PutScatterGatherList` are refused
  outright until a reading or a load test shows Windows 98 fills those slots.
- **Unchanged**: `w2k-export` remains a hard requirement, and the Windows 98
  half of the rule still demands a `win98-precedent` or `ntkern-name` per
  pair. A row whose sole Windows 98 tag is `ntkern-name` is marked
  `ntkern-only` in its note and listed in the 26-A.1 load-test checklist,
  because the name scan is module-blind and misses often (7.2).

### 7.6 Other findings

- **Work items.** `IoAllocateWorkItem` / `IoQueueWorkItem` / `IoFreeWorkItem`
  are `w2k-export` with no Windows 98 tag and are among the names
  `wdmstub.sys` supplies, so the deny rows stand. `ExQueueWorkItem` has 24
  stock precedents (`uhcd.sys` among them; `usbhub.sys` at hint 17). With
  `PsCreateSystemThread` / `PsTerminateSystemThread` (stock `bt829.sys` only)
  these are the evidence for 26-A.8's PASSIVE flusher: a PASSIVE context on
  Windows 98 comes from `ExQueueWorkItem` or a system thread, never from the
  `Io*WorkItem` family.
- `IoReuseIrp`, `IoReportTargetDeviceChangeAsynchronous`,
  `MmMapLockedPagesSpecifyCache`, `MmGetSystemRoutineAddress` and
  `ExFreePoolWithTag`: no Windows 98 tag and `wdmstub`-supplied, consistent
  with the deny rows and with Oney. `IoInitializeIrp` has 10 stock
  precedents.
- The `IO_REMOVE_LOCK` family (`Io*RemoveLockEx`) and
  `KeEnterCriticalRegion` / `KeLeaveCriticalRegion` have `ntkern-name` but no
  precedent and are `wdmstub`-named, which fits Oney's "missing on 98 gold,
  present on SE". The existing deny rows are for the non-`Ex` macro names;
  if the HCD ever wants remove locks, the pairs are the `Ex` ones,
  `ntkern-name` only.
- **`KeQueryTickCount`** (the function) has no Windows 98 tag; the x86 macro
  imports the data export `ntoskrnl.exe!KeTickCount` (stock `battc.sys`).
  `KeQuerySystemTime` (10 stock precedents, both HCDs among them) or
  `KeQueryInterruptTime` (stock `acpi.sys`, `compbatt.sys`) is the evidenced
  clock.
- **PCI configuration space.** `HalGetBusData` and `HalSetBusDataByOffset`
  are `ntkern-name` only; `HalGetBusDataByOffset` and `HalSetBusData` have no
  Windows 98 tag. The import-free routes, `IRP_MN_READ_CONFIG` and
  `BUS_INTERFACE_STANDARD`, were not read for Windows 98: open.
- `DbgPrint` now has stock precedents (`ks.sys` hint 1, `usbdiag.sys`) beyond
  the `ntkern-name` the miniport's 1.2.0.0 allowlist cited for it, and
  `KeGetCurrentIrql` has 16 stock precedents (`uhcd.sys` hint 3), stronger
  than that allowlist's NUSB `NTMAP.SYS`.
- **Compiler helpers.** `_allmul`, `_aulldiv` and `_alldiv` have stock
  precedents (`ccdecode.sys`, `nabtsfec.sys`, `tosdvd.sys`); `_allshl` and
  `_aullshr` are `ntkern-name` only. `AGENTS.md`'s rule against 64-bit
  arithmetic stands for the HCD regardless.
- `IoGetAttachedDeviceReference`, `IoRegisterPlugPlayNotification`,
  `KeRemoveQueueDpc`, `InterlockedCompareExchange`, `ZwCreateKey`,
  `KeWaitForMultipleObjects` (`bt829.sys` only) and the
  `PsCreateSystemThread` pair are evidenced only by SE files outside the USB
  stack, so they need the Appendix A tier B rows to be gate-visible.

### 7.7 What the reading leaves open

| Open item | Binds |
|---|---|
| The load itself: every row the HCD keeps whose only Windows 98 tag is `ntkern-name` is a load-test item. | 26-A.1 (the checklist), 26-V.0 (the guest) |
| Whether Windows 98's `DMA_OPERATIONS` fills `GetScatterGatherList` / `PutScatterGatherList`, and how many map registers its adapter grants. | 25.7 |
| PCI configuration access on Windows 98 through `IRP_MN_READ_CONFIG` / `BUS_INTERFACE_STANDARD`. | 26-A.2 |
| Whether `IoInvalidateDeviceRelations` may be called at `DISPATCH_LEVEL` on Windows 98: this reading gives its export (stock `usbhub.sys`, hint 4F), not its IRQL contract. Section 10's hub state machine depends on it. | 27-A.1 |
| How many evidence-list rows to list: all 67 SE files, or tiers A and B only (each listed file costs one `dumpbin /imports` per gate run); and whether the NUSB 3.6 rows are listed as a tier of their own or not at all, given `wdmstub.sys`. | the owner, before 26-A.1 |

## 8. The door and the two property tabs (task 25.4)

The owner kept both property tabs (section 4): the controller's Advanced tab,
which `usbui.dll` draws from requests to the controller devnode, and the root
hub's Power tab, which it draws from hub IOCTLs to the root-hub devnode. Under
the miniport `usbport.sys` and `usbhub.sys` answered both; under the HCD one
binary answers both, and `XHCISNAP` reaches it through the same controller
door. This section is what each target's UI actually sends and how it finds
the device to send it to.

**Method.** Static: 7-Zip and `expand.exe` on the owner's install media,
`kd -z` (`tools\WinDDK71\Debuggers`, 6.12) over each target's `usbui.dll` with
Microsoft's public PDBs where msdl serves them, WDK 7.1 `link -dump` for
headers, imports and exports, and text reads of the INFs. No Microsoft binary
was executed. A subagent's read, 2026-10-02, not re-read line by line by the
coordinator. Every IOCTL and request code is from the WDK 7.1 or Windows 2000
DDK headers (`usbiodef.h`, `usbioctl.h`, `usbuser.h`), cross-checked against
the immediates in the binaries: `CTL_CODE(FILE_DEVICE_UNKNOWN = 0x22, fn,
METHOD_BUFFERED, FILE_ANY_ACCESS)` = `0x220000 | fn << 2`. PDBs exist for
five of the nine `usbui.dll` builds (XP SP3, Vista x86 and x64, 7 x86 and
x64). XP x64's (`usbui.pdb/23B4B23586964CEE8FAFD6583ACD2CAA1`) is not on the
server (`.pdb` and `.pd_` both 404), Windows 2000's points at a
`dll\usbui.dbg` the server does not have (`usbui.dbg/38439A3011000`), and 98
SE's and ME's carry no debug directory. **All three of those export their C++
methods by decorated name**, so the 98, ME and 2000 function names below are
export names, and an immediate is attributed to the nearest preceding
export: reliable for the exported methods, approximate for code between
them. XP x64's row is XP SP3's constant set found by immediate search with
export labels only.

Files read (extracted into git-ignored `tools\<target>-extracted\ui\`;
`tools\winme-extracted\` is new):

| Target | File | Source | Bytes | Version | SHA-256 |
|---|---|---|---|---|---|
| 98 SE | `usbui.dll` | `Win98SE.iso` `win98\BASE4..WIN98_74.CAB` chain | 147,456 | 4.10.2222 | `d5f43685477121542f0e8514df1c4e50c0e6e6930401a06dae8f93d08cbcaaee` |
| 98 SE | `sysclass.dll` | same chain | 27,184 | 4.10.2222 | `34534a30cc32d883f9a566482d4254410cfaa5b74909e34d0e864a40bd053b2e` |
| 98 SE | `usb.inf` | the `PRECOPY1`-`2` cabinet set | 24,922 | - | `3d9dad93cd26fd4c2d87a6b95c5d5b2231deb113a6a7ad21326f9ba8fd3622f0` |
| ME | `usbui.dll` | `Windows Me OEM Full.iso` `win9x\BASE2.CAB` | 147,456 | 4.90.3000 | `f2a4a4d7dfe84b667833837d4beb91b7672f8ca6ae0ac0e02794b9913a2228a8` |
| ME | `usb.inf` | `win9x\PRECOPY1.CAB` | 26,669 | `DriverVer` 06/08/2000 | `C7FA381948615A04...` |
| 2000 SP4 | `usbui.dll` | `win2ksp4.ISO` `I386\DRIVER.CAB` (not in `SP4.CAB`) | 59,664 | 5.00.2134.1 | `fc106598c6b2fe8a128e1753099b916662225ccf1aed5addd0e23399b4011313` |
| 2000 SP4 | `usb.inf` | `I386\USB.IN_` | 32,543 | 5.00.2195.6717 | `1b2f63a2f526c7678ecfd0f68c991e9094c5adaa6d266ca9df0ef6953049c917` |
| XP SP3 | `usbui.dll` | `en_windows_xp_professional_with_service_pack_3_x86_cd_vl_x14-73974.iso` `I386\SP3.CAB` | 74,240 | 5.1.2600.5512 | `82076e04f96b30693f61f0373e17cb8f6d9ca4a7b6ba819679435472c3c7c6a8` |
| XP SP3 | `usbport.inf`, `usb.inf` | `I386\USBPORT.IN_`, `USB.IN_` | 23,708 / 20,623 | 5.1.2600.5512 / .0 | `C1E9EB12D71A6EF4...` / `364F86B157BA808C...` |
| XP x64 SP2 | `usbui.dll` | `Win XP SP2 VL x64.iso` `AMD64\DRIVER.CAB` | 123,392 | 5.2.3790.1830 | `a6d7c2e4cb7cdff46e1101b1b902d8ca6cebc7fd27dc2e35395b4076eb1bbf55` |
| XP x64 SP2 | `usbport.inf` | `AMD64\USBPORT.IN_` | 24,632 | 5.2.3790.1830 | `D9479ABD2B09C644...` |
| Vista SP2 x86 | `usbui.dll`, `usbport.inf` | `install.wim` index 1 | 83,456 / 59,868 | 6.0.6001.18000 / 6.0.6002.18005 | `usbui.dll` `a4a068d049e4fbe446085e9f673577028e66a0fb15fa117e8ec188f0e4e29bd0` |
| Vista SP2 x64 | `usbui.dll`, `usbport.inf` | `install.wim` index 1 | 104,960 / 59,996 | 6.0.6000.16386 / 6.0.6002.18005 | `usbui.dll` `72c6016f84ca5dedb7ca91f7614af3d82e62c13665d14504faaaf7d46d892888` |
| 7 SP1 x86 | `usbui.dll`, `usbport.inf` | `install.wim` (single image) | 80,896 / 66,522 | 6.1.7600.16385 / 6.1.7601.17514 | `usbui.dll` `86bcba0928ddcd0d886a5933ff26e594943b9d4253d2860eff0792a407654402` |
| 7 SP1 x64 | `usbui.dll`, `usbport.inf` | `install.wim` (single image) | 101,376 / 66,650 | 6.1.7600.16385 / 6.1.7601.17514 | `usbui.dll` `f8e8d8214b703bf5f9ba0616c9d25d878ed99e17711291ff37cff15d161ee1e1` |
| NUSB 3.3 | `USB2.INF` | `tools\nusb-extracted` | 7,738 | `DriverVer` 09/26/2003,4.90.3000.10 | `8A583C40A063E9CF...` |
| NUSB 3.6 | `USB2.INF`, `USB.INF` | `tools\nusb36-extracted` | 14,543 / 31,279 | same / 06/08/2000 | `A9C75950F2805A1F...` / `F67BA17B96E557CA...` |
| SweetLow | `USB2.INF` | `tools\sweetlow-extracted` | 4,146 | `DriverVer` 09/26/2003,4.90.3000.10 | `A3D2B895A3A77657...` |

The `usbui.dll` sizes and versions agree row for row with
`build-and-test.md`'s "Where each target's `usbui.dll` comes from". Hashes
shown truncated are as the reading recorded them.

### 8.1 The controller's Advanced tab: what `usbui.dll` sends the controller

| Request to the controller | Code | 98 SE | ME | 2000 SP4 | XP SP3 | XP x64 | Vista x86/x64 | 7 x86/x64 | `usbui` function |
|---|---|---|---|---|---|---|---|---|---|
| `IOCTL_GET_HCD_DRIVERKEY_NAME` | `0x220424` | yes | yes | yes | yes | yes | yes | yes | `UsbItem::GetHCDDriverKeyName` (two calls: size, then data) |
| `IOCTL_USB_GET_ROOT_HUB_NAME` | `0x220408` | yes | yes | yes | yes | yes | yes | yes | `UsbItem::GetRootHubName` (two calls) |
| `IOCTL_USB_USER_REQUEST`, `UsbUserRequest` = 1 (`USBUSER_GET_CONTROLLER_INFO_0`) | `0x220438` | - | - | - | - | - | yes | yes | `UsbItem::GetControllerInfo` (x86); inlined into `UsbItem::EnumerateController` (x64) |
| any other `USBUSER` code (2 to 0xA, `0x1xxxxxxx`, `0x2xxxxxxx`) | `0x220438` | - | - | - | - | - | - | - | none |
| `HCD_GET_STATS_1/2`, `DIAGNOSTIC_MODE_ON/OFF`, `HCD_DISABLE/ENABLE_PORT` | `0x2203FC`, `0x220428`, `0x220400`, `0x220404`, ... | - | - | - | - | - | - | - | none |

**The controller door is small.** No target's `usbui.dll` sends `PASS_THRU`,
`GET_POWER_STATE_MAP`, `GET_BANDWIDTH_INFORMATION`, `GET_BUS_STATISTICS_0`,
`GET_ROOTHUB_SYMBOLIC_NAME`, `GET_USB_DRIVER_VERSION`, `GET_USB2_HW_VERSION`
or `USB_REFRESH_HCT_REG`. `PASS_THRU` (3) is `XHCISNAP`'s alone.

`USBUSER_GET_CONTROLLER_INFO_0` as Vista and 7 send it: in = out = 0x28
bytes, a `USBUSER_REQUEST_HEADER` (`UsbUserRequest` 1, `RequestBufferLength`
0x28) then `USB_CONTROLLER_INFO_0` (`PciVendorId`, `PciDeviceId`,
`PciRevision`, `NumberOfRootPorts`, `ControllerFlavor`, `HcFeatureFlags`).
Windows 7 x86 copies the six ULONGs into its item at +0x11C **whenever
`DeviceIoControl` returns TRUE, without reading `UsbUserStatusCode`**, and
`EnumerateController` ignores the function's result. The only field read
afterwards is `ControllerFlavor` (+0x12C): `UsbItem::IsController20` is
`ControllerFlavor >= 0x3E8` (`EHCI_Generic` = 1000, `usb.h`), and
`UsbItem::IsHighSpeed` applies the same test to the controller item. So the
HCD answers request 1 with status 0 and a real `USB_CONTROLLER_INFO_0`, and
reports a flavor of 1000 or above if the bandwidth view is to use USB 2.0
arithmetic; WDK 7.1's enum has nothing above `EHCI_Lucent` = 3000 and no
xHCI flavor. XP and older cannot tell a 2.0 controller this way at all: they
never send `0x220438`.

`GET_HCD_DRIVERKEY_NAME`'s answer is matched against the devnode tree by
`GetConfigMgrInfo` (`CM_Locate_DevNode`, `CM_Get_Child` / `Sibling` /
`Parent`, `CM_Get_DevNode_Registry_Property` with `CM_DRP_DRIVER` = 0x0A), so
it must be the controller devnode's driver (software) key name exactly, as
`IoGetDeviceProperty(DevicePropertyDriverKeyName)` on the PCI PDO gives it.
The same routine reads `FailReasonID` from each devnode's key (Vista and 7).

`\\.\HCD<n>` (the string `\\.\HCD` with `_itow`) is used by
`UsbItem::EnumerateAll` on 98, ME, 2000, XP and XP x64: the whole-machine
walk of the notification popups and the control-panel applet, not of the two
tabs. Vista's and 7's `usbui.dll` carry no `HCD` string; their `EnumerateAll`
enumerates `GUID_DEVINTERFACE_USB_HOST_CONTROLLER`. Windows 98's
`SYSTRAY.EXE` also carries `\\.\HCD%d` (already recorded in
`build-and-test.md`).

### 8.2 How the pages find their device

This is the load-bearing part, and it splits by generation:

| Target | Lookup | Then |
|---|---|---|
| 98 SE, ME | `SetupDiOpenDevRegKey(..., DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_ALL_ACCESS)`, then `RegQueryValueExA("SymbolicName")` (98 SE: IAT slot `0x7701B3C8` = `SetupDiOpenDevRegKey`, string at `0x77001124`) | `CreateFileA` on the name with `\??\`, `\DosDevices\` or `\\?\` rewritten to `\\.\` |
| 2000 SP4, XP SP3, XP x64 | `UsbPropertyPage::GetDeviceName`: the same `SetupDiOpenDevRegKey` (scope 1, profile 0, `DIREG_DEV`, `0xF003F`) and `RegQueryValueExW("SymbolicName")` (XP SP3 string at `0x5AF61290`) | `GetHandleForDevice`: the same prefix rewrite, `CreateFileW(..., GENERIC_WRITE 0x40000000, ...)` |
| Vista, 7 | `UsbPropertyPage::GetDeviceName(GUID_DEVINTERFACE_USB_HOST_CONTROLLER)` for the controller page, `GUID_DEVINTERFACE_USB_HUB` `{F18A0E88-C30C-11D0-8815-00A0C906BED8}` for the hub pages: `SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_PRESENT \| DIGCF_DEVICEINTERFACE = 0x12)`, keeping the interface whose `SP_DEVINFO_DATA.DevInst` is the page's devnode | the interface path to `GetHandleForDevice` |

So **98, ME, 2000, XP and XP x64 read a `SymbolicName` REG_SZ from the
devnode's hardware key; Vista and 7 need an enabled
`GUID_DEVINTERFACE_USB_HOST_CONTROLLER` or `GUID_DEVINTERFACE_USB_HUB`
interface on the devnode.** When the lookup fails the page is not created at
all - no tab, no error: on XP SP3 `PowerPage::Create` returns before
`UsbPropertyPage::Create`, and on Windows 7 x86 `BandwidthPage::Create` does
the same. The HCD therefore does **both on both devnodes**: it writes
`SymbolicName` to the controller's and the root hub's hardware keys and
registers and enables both interfaces, as `usbport.sys` and `usbhub.sys` do.
Windows 98 SE's `sysclass.dll` carries `SymbolicName` (twice) as well, so the
9x tab looks the value up before handing off to `usbui.dll`.

Windows 7 x86 `usbport.sys` 6.1.7601.17514 writes `SymbolicName` from
`USBPORT_EnableDeviceInterface` (`push offset` of the `L"SymbolicName"`
string at `0x25EC7`, PDB name), so on that build the value is the interface
link; `USBPORT_CreateLegacyFdoSymbolicLink` is the `HCD<n>` routine.
Every `usbport.sys` and `usbhub(20).sys` read - NUSB's, SweetLow's, 98 SE's
own `usbhub.sys`, 2000 SP4's, XP SP3's and 7 x86's - carries the
`SymbolicName` string and imports `IoRegisterDeviceInterface`,
`IoSetDeviceInterfaceState` and `IoOpenDeviceRegistryKey`, which is why all
three are Windows 98 exports with stock precedents in section 7.4.

### 8.3 The root hub's Power tab (and Vista's and 7's hub Advanced tab)

| Hub IOCTL | Code | 98 SE | ME | 2000 SP4 | XP SP3 | XP x64 | Vista | 7 | `usbui` function |
|---|---|---|---|---|---|---|---|---|---|
| `GET_NODE_INFORMATION` | `0x220408` | yes | yes | yes | yes | yes | yes | yes | `UsbItem::GetHubInfo` (in = out = 0x4C) |
| `GET_NODE_CONNECTION_INFORMATION` | `0x22040C` | yes | yes | yes | yes | yes | popups only | popups only | `GetConnectionInformation` (98 to XP x64); `UsbPopup::QueryContinue` (NT 5.1 on) |
| `GET_DESCRIPTOR_FROM_NODE_CONNECTION` | `0x220410` | yes | yes | yes | yes | yes | yes | yes | `GetConfigDescriptor`: one call, in = out = 0x15 bytes (a 12-byte request and the 9-byte configuration header), on a zero-filled buffer of which only `ConnectionIndex`, `wValue` 0x0200 and `wLength` 9 are written, so **`bmRequestType` and `bRequest` are 0**; anything but success with exactly 0x15 bytes returned leaves the device's power "unknown" (static, every target: one `0x220410` site per build; 98 SE 7700B590/B596, 2000 666B4696/469C, XP SP3 5AF63BD6/BDC, 7 x86 10004F03/F0A; corrected 2026-10-04 - this row said "then up to `wTotalLength`" before) |
| `GET_NODE_CONNECTION_NAME` | `0x220414` | yes | yes | yes | yes | yes | yes | yes | `GetExternalHubName` (for a connection with `DeviceIsHub`) |
| `GET_NODE_CONNECTION_DRIVERKEY_NAME` | `0x220420` | yes | yes | yes | yes | yes | yes | yes | `GetDriverKeyName` |
| `GET_HUB_CAPABILITIES` | `0x22043C` | - | - | - | yes | yes | - | - | `GetHubInfo`, after node information (4 bytes) |
| `GET_NODE_CONNECTION_ATTRIBUTES` | `0x220440` | - | - | - | yes | yes | yes | yes | `GetPortAttributes` (0x0C bytes) |
| `GET_NODE_CONNECTION_INFORMATION_EX` | `0x220448` | - | - | - | - | - | yes | yes | `GetConnectionInformation` (0x23 bytes, then 0x0B per pipe) |
| `RESET_HUB` | `0x22044C` | - | - | - | - | - | yes | yes | `HubPage::ResetHub` (the hub Advanced tab's button) |
| `GET_HUB_CAPABILITIES_EX` | `0x220450` | - | - | - | - | - | yes | yes | `GetHubInfo`, after node information (4 bytes) |
| `DIAG_IGNORE_HUBS_ON/OFF`, `HUB_CYCLE_PORT` | `0x220418`, `0x22041C`, `0x220444` | - | - | - | - | - | - | - | none |

**`0x220408` has two meanings, chosen by the device object.**
`IOCTL_USB_GET_NODE_INFORMATION` and `IOCTL_USB_GET_ROOT_HUB_NAME` are the
same value. Under Microsoft's stack they reach different drivers; under the
HCD both devnodes are one driver's, so it answers by device object: on the
root-hub FDO, node information; on the controller FDO, the root hub's name.

The providers: `USBHubPropPageProvider` adds `PowerPage` alone on 2000, XP and
XP x64, and `PowerPage` plus `HubPage` (the hub's Advanced tab) on Vista and
7. `USBControllerPropPageProvider` adds `BandwidthPage` (the controller's
Advanced tab) on every NT target. The controller page reaches the root hub
the same way on every target: `GET_ROOT_HUB_NAME` on the controller handle,
`\\.\` plus the returned name, then the hub IOCTLs.

What the Power page reads (Windows 7 x86 PDB names; field offsets checked
against the `#pragma pack(1)` structures of `usbioctl.h`):

- `USB_NODE_INFORMATION` (0x4C): `HubDescriptor.bNumberOfPorts` (item +0x1A,
  the port loop bound in `EnumerateHub`) and `HubInformation.HubIsBusPowered`
  (+0x5F). `UsbItem::PortPower` returns **500 mA when `HubIsBusPowered` is 0
  and 100 mA when it is not** - the "self-powered, 500 mA per port" text the
  guests show.
- `UsbItem::ComputePower`: a bus-powered hub costs `(ports + 1) * 100` mA,
  capped at 500 above four ports; any other device costs `bMaxPower * 2` from
  the configuration descriptor fetched with `0x220410`.
- Per connection: `ConnectionStatus`, `DeviceDescriptor`, `DeviceIsHub`
  (+0x18), and on Vista and 7 `Speed` (+0x17; `IsHighSpeed` tests `== 2`).
- The driver key per connection (`0x220420`), mapped to a devnode by
  `GetConfigMgrInfo` (`CM_DRP_DRIVER`) for the device's name and icon; it
  must be the child devnode's driver key exactly.
- On Vista and 7, `USB_HUB_CAPABILITIES_EX.CapabilityFlags`:
  `HubPage::Refresh` tests bit 3 (`HubIsMultiTt`) and `IsHighSpeed` tests
  bit 1 (`HubIsHighSpeed`), the "Hub is operating at high-speed" line.

On Vista and 7 the hub also takes WMI queries (the OS INF's
`[ROOTHUB.Dev.NT.WMI]` lines grant access to them):
`UsbItem::GetPerformanceInfo` queries `GUID_USB_WMI_DEVICE_PERF_INFO`
(`{66C1AA3C-...}`) from `EnumerateHubPorts`, `HubPage::GetHubLog` queries
`GUID_USB_WMI_HUB_DIAGNOSTICS` (`{AD0379E4-...}`) and writes a file, and the
popups use `GUID_USB_WMI_STD_DATA` (`{4E623B20-...}`, `WmiExecuteMethod`).
98's `usbui.dll` imports `wmi.dll` (`WmiNotificationRegistrationA`,
`WmiExecuteMethodA`) for its popups too.

### 8.4 The speed ceiling

WDK 7.1 `usb200.h` defines `USB_DEVICE_SPEED` as `UsbLowSpeed = 0,
UsbFullSpeed, UsbHighSpeed` - three values, no SuperSpeed - and
`USB_NODE_CONNECTION_INFORMATION_EX.Speed` is a `UCHAR` "defined in
USB200.h". The Windows 2000 DDK (`tools\ntddk\inc`, "Copyright (c) 1998-99")
has no `usb200.h` and no `_EX` structure at all; its
`USB_NODE_CONNECTION_INFORMATION` carries `BOOLEAN LowSpeed`. So the most any
target's UI can be told is High Speed (NT 6.x) or "not low speed" (98 to XP
x64), and a SuperSpeed device has to be reported as one of those. That is
why no checkpoint reads a speed from these pages.

### 8.5 The OS INFs' registrations, quoted

Root hub, NT paths - every NT target registers the same provider:

| File | Section | Line |
|---|---|---|
| 2000 SP4 `usb.inf` (5.00.2195.6717) | `[ROOTHUB2.AddReg.NT]` (for `USB\ROOT_HUB20`), `[StandardHub.AddReg.NT]` (`USB\ROOT_HUB`, `USB\CLASS_09`), `[Usb2Hub.AddReg.NT]` (`USB\HubClass`) | `HKR,,EnumPropPages32,,"usbui.dll,USBHubPropPageProvider"` |
| XP SP3 `usbport.inf` (5.1.2600.5512) | `[ROOTHUB.AddReg.NT]` (`USB\ROOT_HUB` and `USB\ROOT_HUB20` both `=ROOTHUB.Dev`) | same |
| XP SP3 `usb.inf` | `[StandardHub.AddReg.NT]` | same; `[Composite.DelReg.NT]` deletes `HKR,,EnumPropPages32` for composites |
| XP x64 `usbport.inf` (5.2.3790.1830) | `[ROOTHUB.AddReg.NT]` | same |
| Vista SP2 x86/x64 `usbport.inf` (6.0.6002.18005) | `[ROOTHUB.AddReg.NT]` | same; plus five `WMIInterface` lines in `[ROOTHUB.Dev.NT.WMI]` |
| 7 SP1 x86/x64 `usbport.inf` (6.1.7601.17514) | `[ROOTHUB.AddReg.NT]` | same, plus `HKLM,System\CurrentControlSet\Services\usbhub,BootFlags,0x00010003,4`; `[ROOTHUB.Dev.NT]` also `AddPowerSetting=USBPowerSettings`; `.WMI` as Vista |

Root hub, 9x paths - the 16-bit `sysclass.dll` page:

| File | Section | Lines |
|---|---|---|
| 98 SE `usb.inf` | `[StandardHub.Dev]` `AddReg=Hub.AddReg,Power.AddReg` (`USB\ROOT_HUB`, `USB\CLASS_09`, ...) | `[HUB.AddReg]` `HKR,,DevLoader,,*NTKERN` / `HKR,,NTMPDriver,,usbhub.sys`; `[Power.AddReg]` `HKR,,EnumPropPages,,"sysclass.dll,USBHubPropPage"` |
| ME `usb.inf` (06/08/2000) | `[StandardHub.Dev]` `AddReg=Hub.AddReg,Power.AddReg` | `[Power.AddReg]` `HKR,,EnumPropPages,,"sysclass.dll,USBHubPropPage"` (line 380) |
| NUSB 3.3 `USB2.INF` | `[ROOTHUB2]` `AddReg=Usb2Hub.AddReg` (`USB\ROOT_HUB20`), `[Usb2Hub.Dev]` (`USB\HubClass`) | `[Usb2Hub.AddReg]` `HKR,,DevLoader,,*NTKERN` / `HKR,,NTMPDriver,,usbhub20.sys` / `HKR,,EnumPropPages,,"sysclass.dll,USBHubPropPage"` |
| NUSB 3.6 `USB2.INF` | same sections | the same three lines (81-84) |
| NUSB 3.6 `USB.INF` | `[HUB.AddReg]`, the power section | `HKR,,NTMPDriver,,usbhub.sys`; `HKR,,EnumPropPages,,"sysclass.dll,USBHubPropPage"` (line 542) |
| SweetLow `USB2.INF` | `[ROOTHUB2]` / `[Usb2Hub.Dev]` | the same three lines (76-79); `[Composite.DelReg]` deletes `HKR,,EnumPropPages` |

Controller, as the OS INFs write it:

| File | Lines |
|---|---|
| 98 SE `usb.inf` OHCI / UHCI / NEC | `HKR,,EnumPropPages,,"sysclass.dll,USBControllerPropPage"` (with `NTMPDriver` and `DevLoader` through `USB.AddReg`) |
| ME `usb.inf` | the same (lines 96, 109, 120, 130) |
| NUSB 3.3 / 3.6, SweetLow `USB2.INF` `[EHCI.AddReg]` | `HKR,,DevLoader,,*NTKERN` / `HKR,,NTMPDriver,,usbehci.sys` / `HKR,,EnumPropPages,,"sysclass.dll,USBControllerPropPage"` |
| 2000 SP4 `usb.inf` `[EHCI.AddReg.NT]`, `[OpenHCD.AddReg.NT]`, `[UniversalHCD.AddReg.NT]` | `HKR,,EnumPropPages32,,"usbui.dll,USBControllerPropPageProvider"` / `HKR,,Controller,1,01` (its 9x halves vary: OHCI `"usbui.dll,USBControllerPropPageProvider"`, UHCI `"sysclass.dll,USBEnumPropPages"`) |
| XP SP3, XP x64 `usbport.inf` | `EnumPropPages32` as above on each controller section, and a 9x-style `EnumPropPages` with the same `usbui.dll` value |
| Vista, 7 `usbport.inf` `[EHCI.AddReg.NT]` and siblings | `HKR,,EnumPropPages32,,"usbui.dll,USBControllerPropPageProvider"` / `HKR,,Controller,1,01` |
| this repository's INFs, the miniport's to 1.2.0.0 and the HCD's now (`src\xhci98.inf`, `src\xhci98-amd64.inf`) | `HKR,,EnumPropPages,,"sysclass.dll,USBControllerPropPage"` (98) and `HKR,,EnumPropPages32,,"usbui.dll,USBControllerPropPageProvider"` with `HKR,,Controller,1,01` (NT): identical to the OS rows |

### 8.6 The root-hub id

Every non-VID `USB\` id any of the INFs read matches (98 SE, ME, NUSB 3.3 and
3.6, SweetLow, 2000 SP4, XP SP3, XP x64, Vista, 7) is one of `USB\ROOT_HUB`,
`USB\ROOT_HUB20`, `USB\ROOT_HUB_DBC` (98 and ME, the device bay),
`USB\HubClass`, `USB\CLASS_09`, `USB\CLASS_09&SUBCLASS_01`, `USB\COMPOSITE`,
`USB\UNKNOWN`, and NUSB's mass-storage `USB\Class_08&SubClass_0x&Prot_xx`
rows. INF matching compares whole id strings, case-insensitively, with no
prefix wildcard. Neither `usbui.dll` (all nine builds) nor `sysclass.dll`
contains `ROOT_HUB`, `RootHub` (other than the C++ name `GetRootHubName`) or
a `USB\` string, so nothing in the UI keys off the id.

**The root-hub PDO answers `BusQueryDeviceID` and `BusQueryHardwareIDs` with
`XHCI98\ROOT_HUB`, and no compatible ids.** (The reading proposed
`XHCI98HC\ROOT_HUB`, after the successor's working name; the owner's decision
of 2026-10-02 that the HCD takes the name `xhci98.sys` makes it
`XHCI98\ROOT_HUB`. A more specific
`XHCI98\ROOT_HUB&VID<vvvv>&PID<pppp>&REV<rrrr>` hardware id ahead of it, as
usbport does for its own root hubs, is optional.) A compatible id of
`USB\ROOT_HUB*`, `USB\CLASS_09*` or `USB\HubClass` would let `usbhub.sys` or
`usbhub20.sys` bind on a ranking fallback, which the decision of section 4
forbids. A private enumerator rather than a `USB\` id keeps the instance
under `Enum\XHCI98\` on every target, out of the `USB` key that usbhub and
NUSB's stack populate, and cannot collide with any OS row by construction.
Device PDOs stay `USB\VID_...` and `USB\Class_...` so the class INFs match
them (section 10). The root hub is installed from the HCD's own INF, whose
`[Version]` is `Class=USB`, `ClassGUID={36FC9E60-C465-11CF-8056-444553540000}`,
the class every OS INF above gives its root hub; on Windows 98 the 16-bit
engine needs `DevLoader=*NTKERN` and `NTMPDriver` on the root-hub devnode,
exactly as every 9x hub section above writes them.

**The working tree carries this.** `src\xhci98.inf` and
`src\xhci98-amd64.inf`, the HCD's INFs since it took over `src\`, list
`XHCI98\ROOT_HUB` as a second model beside `PCI\CC_0C0330` in every models
section, with no compatible id, and both models install the one service,
`xhci98`. The root-hub sections register
`HKR,,EnumPropPages,,"sysclass.dll,USBHubPropPage"` beside
`DevLoader=*NTKERN` and `NTMPDriver=xhci98.sys` on the 9x path, and
`HKR,,EnumPropPages32,,"usbui.dll,USBHubPropPageProvider"` on every NT path;
the controller sections keep the lines the miniport's INFs wrote,
`EnumPropPages` = `"sysclass.dll,USBControllerPropPage"` (9x) and
`EnumPropPages32` = `"usbui.dll,USBControllerPropPageProvider"` with
`Controller,1,01` (NT). The root-hub sections also copy the driver
(`CopyFiles=Xhci.CopyFiles`), which the reading's skeleton had left out; that
keeps the INF gate's rule that an `NTMPDriver` or `ServiceBinary` names a
file its own section delivers. They write no `Controller,1,01`, no
`WMIInterface` and no `AddPowerSetting`. The INF gate's `HCD-ROOTHUB` rule
refuses a models section without the root-hub model or with any
`USB\ROOT_HUB*` id on it, and its `HCD-HUBPAGE` rule refuses a root-hub
install that writes no hub page or the wrong one (section 9).

### 8.7 What the HCD must do for the two tabs

1. **Controller FDO**: register and enable
   `GUID_DEVINTERFACE_USB_HOST_CONTROLLER`
   `{3ABF6F2D-71C4-462A-8A92-1E6861E6AF27}` (WDK 7.1 `usbiodef.h`; absent from
   the Windows 2000 DDK, so defined locally); write `SymbolicName` REG_SZ to
   the controller devnode's hardware key (`IoOpenDeviceRegistryKey`,
   `PLUGPLAY_REGKEY_DEVICE`); create `\DosDevices\HCD<n>`.
2. **Root-hub devnode**: the same with `GUID_DEVINTERFACE_USB_HUB`, and
   `SymbolicName` on the root-hub devnode's hardware key.
3. **Controller FDO answers** `0x220424` and `0x220408` with usbport's
   two-call protocol (a short buffer gets `ActualLength` back with success;
   `usbui` calls twice for each), and `0x220438` request 1 (status 0, 0x28
   bytes).
4. **Root-hub FDO answers** the 8.3 table for its target set, `0x220408`
   meaning node information there. `RESET_HUB` may refuse.
5. `GET_NODE_CONNECTION_DRIVERKEY_NAME` returns the child devnode's driver
   key exactly.
6. **Device-object security**: XP, Vista and 7 open with `GENERIC_WRITE`
   (`0x40000000`), and `usbui.dll` runs in the Device Manager user's context.

### 8.8 `XHCISNAP` through the door

Today (`xhcisnap\xhcisnap.c`, `src\xhci.h`, the ABI document's "Debug /
single-packet"): `CreateFileA("\\.\HCD<n>", GENERIC_READ | GENERIC_WRITE)`,
`DeviceIoControl(0x00220438)`, in = out buffer. Layout: +0x00
`UsbUserRequest` = 3, +0x04 `UsbUserStatusCode`, +0x08
`RequestBufferLength`, +0x0C `ActualBufferLength`, +0x10 the GUID
`{34F57942-...}` as the four ULONGs `0x34F57942, 0x40662D69, 0x1482BD9C,
0x20BC448E`, +0x20 `ParameterLength` (the tool uses 0xF000), +0x24 the block:
`XHCI_SNAPSHOT_REQUEST` (`Signature` `'XSNQ'` 0x514E5358, `Region`,
`Offset`) in, `XHCI_SNAPSHOT_HEADER` (`'XSNP'` 0x504E5358, schema 5) and its
payload out.

**The wire format carries over unchanged** if the HCD's `0x220438` handler
reproduces usbport's dispatcher clause for clause, as the ABI document records
it and `XHCISNAP -probe` checks it:

| Clause | usbport behaviour | `-probe` control |
|---|---|---|
| input < 0x10 | `STATUS_BUFFER_TOO_SMALL` | - |
| `RequestBufferLength` != input length | header status 4 | "RequestBufferLength disagreeing" -> 4 |
| unknown request code | header status 2 ("table has 8 entries") | "unknown request code 15" -> 2 |
| `0x1xxxxxxx` / `0x2xxxxxxx` without the enable | header status 3 | - |
| PassThru floor 0x28 | header status 7 | "a 0x20-byte buffer" -> 7 |
| `ParameterLength` > 0x10000 | status 5 | - |
| 0x24 + `ParameterLength` > `RequestBufferLength` | status 7 | - |
| GUID not ours, or the channel shut | status 6 (exactly; usbport maps any nonzero MPSTATUS to 6) | "PassThru, our GUID" -> 6 when shut |
| answered | status 0, `ActualBufferLength` = `ParameterLength` + 0x28, `Information` = min(`RequestBufferLength`, `ActualBufferLength`) | -> 0 |
| NT status | `STATUS_SUCCESS` on every one of these | the tool reads the header, not the return |

`IRP_MJ_CREATE` and `IRP_MJ_CLOSE` on the controller FDO complete with no
work (usbport does that, with no access check; `FILE_ANY_ACCESS`). The HCD
names its FDO (usbport's is `\Device\USBFDO-<n>`) and creates
`\DosDevices\HCD<n>` at the first free `n`, because on a machine with other
usbport controllers `HCD0` and up are theirs (the tool's `-c N` already
covers that). The handler runs at PASSIVE in the HCD's own dispatch, so the
"non-paged kernel copy" clause is the HCD's to keep (the `METHOD_BUFFERED`
system buffer is non-paged already). Answering request 1 for `usbui.dll` and
request 3 for the tool from one dispatcher keeps the "8-entry table" refusal
semantics.

What does change in the tool: (a) its software-key finder, which accepts a
key on `NTMPDriver = xhci98.sys` or the miniport's `InfSection`
(`xhcisnap.c` about lines 1466-1640), learns the HCD INFs' section names (the
binary keeps the name `xhci98.sys`, so the `NTMPDriver` match stands, but a
root-hub devnode now carries the same `NTMPDriver` and must not be taken for
the controller); (b) the payload decode, because the extension bytes and region
layout are the HCD's - a schema bump, which the tool already refuses cleanly;
(c) its help text, which names usbport as the creator of `HCD<n>`.

### 8.9 Device Manager's view by connection

Not read: Device Manager (`sysdm.cpl` on 98, `devmgr.dll` on NT) was not
opened. What the reading establishes is that the node IOCTLs feed
`usbui.dll`'s own dialogs, not the Device Manager tree: the connection tree
inside the Power and Bandwidth dialogs is built by `EnumerateHub` /
`EnumerateHubPorts` from those IOCTLs and mapped to devnodes by driver key,
while the view by connection is expected to be the PnP devnode tree
(controller, root-hub PDO, device PDOs), which the HCD produces by where it
creates its PDOs.

### 8.10 What the reading leaves open

| Open item | Binds |
|---|---|
| Device Manager's view by connection on 98 and NT: that it uses only `CM_Get_Child` / `Sibling` is expected, not read. | 26-A.8 (or a guest reading in 26-V.1 / 26-V.2) |
| A sweep of each target's whole `%windir%\inf` (and NUSB's and SweetLow's full file sets) for any row matching `XHCI98\ROOT_HUB`: impossible by construction, unchecked. | 26-A.4 |
| **External hubs inside the bus against `DeviceIsHub`.** `usbui.dll` recurses into a connection with `DeviceIsHub` TRUE through `GET_NODE_CONNECTION_NAME` and `CreateFile("\\.\" + name)`. With no hub devnode the HCD either reports the devices behind a hub flat under the root hub's ports, or answers `DeviceIsHub` and serves a per-hub node through the root-hub interface link plus a suffix (`FileObject->FileName`). A design decision. **Closed 2026-10-04 by task 33.4 (10.11)**: each hub is a devnode with a door of its own, `DeviceIsHub` TRUE and its name returned | 26-A.8; 33.4 |
| **Split composite devices**: `usbui.dll` maps a connection to one devnode through `GET_NODE_CONNECTION_DRIVERKEY_NAME`. With the bus splitting a composite into per-function PDOs there is no single connection devnode; which key to return (the first function's, or a failure) and what `usbui` then shows is unread. | 26-A.8, with 26-A.7 |
| How Windows 98's engine treats a root-hub section whose `NTMPDriver` is the already-loaded controller driver, and NTKERN calling `AddDevice` a second time on that driver object (98's own `usbhub.sys` serves several devnodes, so it is expected to work). | 26-V.0 / 26-V.1 (a guest reading) |
| The `ControllerFlavor` to report on Vista and 7 (1000 or above for USB 2.0 arithmetic; nothing in WDK 7.1 names xHCI). What usbport reported for the miniport was not read. | 26-A.8 |
| Hub WMI on Vista and 7 (`GUID_USB_WMI_DEVICE_PERF_INFO`, `_HUB_DIAGNOSTICS`, the `_STD_DATA` notifications), and what `EnumerateHubPorts` does when the performance query fails. The popups (over-current, not enough power) are WMI-driven and belong to neither tab. | 26-A.8 (and 28-A.1 for Vista and 7) |
| The exact `SymbolicName` string each stack writes (interface link or `\DosDevices\` name) on 98, 2000 and XP: only Windows 7's write site was read. On 98, whether `CreateFileA("\\.\<interface link>")` resolves through NTKERN (NUSB's hub page working suggests it does) or whether the HCD should write a `\DosDevices\` name there. | 26-A.8 |
| `sysclass.dll`'s `DiagnosticMode` and `SupportNonComp` value names: purpose unread. | 26-A.8 |
| A `bcdUSB` of 0x0300 in `DeviceDescriptor`, and a SuperSpeed device's `Speed` on Vista and 7 (`UsbHighSpeed` is the ceiling): whether `UsbItem::UsbVersion` or the bandwidth arithmetic misreads a 3.x descriptor. | Phase 29 |
| Device-object security for a non-elevated Device Manager on Vista and 7 (`GENERIC_WRITE` open): a runtime question. | 28-A.1 |

## 9. The harness and the gates (task 25.5)

Every script that builds, gates, packages or measures the driver was written
for the miniport. Task 25.5 read what each of them assumes, as a plan for
adding a second binary beside the first. On 2026-10-02 the owner decided
instead that **the miniport leaves the tree and the HCD takes over `src\` and
the name `xhci98.sys`**: the INFs are `src\xhci98.inf` and
`src\xhci98-amd64.inf`, the service is `xhci98`, the package is
`xhci98-<version>.zip`, and the allowlists `scripts\import-gate\xhci98-imports.allow`
and `-amd64.allow` are the HCD's. The miniport's source files that the
roadmap classes "Rewritten" are deleted from `src\` (recoverable from the
1.2.0.0 branch and `releases\1.2.0.0`), and its "Adapted" files stay in `src\`
unbuilt until 26-A.2. The tree no longer builds the miniport. So the
second-binary machinery the plan proposed - a `-Product` switch on every
gate, an `hc` suffix on every output - is not needed: the gates have one
profile, the HCD's. What survives of the plan is what each script must learn
about a driver that is not a usbport miniport, and the harness's read route.

This section is a reading of this repository's own scripts and sources; no
binary was read for it, so it carries no `legal-provenance.md` row. A
subagent wrote the plan on 2026-10-02; its line numbers are to `5086e2d`
(branch `phase-25`), before task 25.8 and before the owner's decision, and
are kept below as such.

### 9.1 A measured fact about the DDK's `build.exe`

The Windows 2000 DDK's `build.exe` accepts a directory prefix in `SOURCES=`
only if it is the parent's, `..\`. Task 25.8 first built the scaffold from a
subdirectory, and a sibling-directory layout failed: `build.exe` printed
"Ignoring invalid directory prefix in SOURCES= entry: ..\src\xhci_ring.c" and
then failed to make the first object, while `..\xhci_ring.c` from a
subdirectory of `src\` compiled (measured 2026-10-02, task 25.8). With the HCD
in `src\` itself this no longer shapes the layout - `src\sources` names
`xhci_mem.c`, `xhci_ring.c`, `xhci_caps.c`, `xhci_port.c`, `xhci_ctx.c`,
`xhci_topo.c`, `xhci_desc.c` and `xhci_log.c` beside `hcd_entry.c` and
`xhci98.rc` - but it bounds any future split of the tree.

### 9.2 Done in task 25.8

**`scripts\build-driver.cmd`** builds `src\` in all three flavours and both
architectures and runs on each image the import gate and
`check-flavour-marker.ps1`, and the INF gate on both INFs on every build. The
image carries the `XHCI98_FLAVOUR_*` marker vocabulary and, while it is the
scaffold, `XHCI98_SCAFFOLD_DO_NOT_STAGE`.

**`scripts\import-gate\check-imports.ps1`** holds the HCD to three rules the
miniport's gate did not have:

- No `USBPORT.SYS` rows exist (`usbport-imports.expected` and the import
  library it generated are gone), and any `USBPORT.SYS` import is refused by
  name, not merely as unlisted: the HCD replaces `usbport.sys`, and a stock
  Windows 98 SE has none to resolve against.
- An image that imports nothing at all passes only while it carries
  `XHCI98_SCAFFOLD_DO_NOT_STAGE`; past 25.8 an empty import table is a
  failure. The exception is keyed on the marker, so it retires by itself when
  26-A.1 removes the marker from `hcd_entry.c`.
- The pool family is denied until 26-A.1 adds the rows of section 7.4.

The Windows 2000 baselines, the Windows 98 evidence list, the NT 5.2 amd64
baselines and the Windows 98 evidence rule for kernel and HAL rows are
unchanged: they describe the targets, not the driver, and section 7's rows
are held to them.

**`xhci98-imports.allow` and `xhci98-imports-amd64.allow`.** The x86 file has
an empty `[imports]` (the Windows 2000 DDK has no `/GS`, and the scaffold
imports nothing). The amd64 file carries one row, `ntoskrnl.exe!KeBugCheckEx`,
the `/GS` `__report_gsfailure` import that WDK 7.1's `BufferOverflowK.lib`
adds. Both `[deny]` sections deny the four pool names until 26-A.1, and carry
only the denials whose reason is a fact about a target (absent on Windows 98,
or blocking the load on Windows 2000); the miniport's "usbport owns it"
reasons do not apply to a driver that owns those things.

**`scripts\inf-gate\check-inf.ps1`** has one profile, the HCD's: the binary
`xhci98.sys`, the version header `src\xhci_version.h` and resource script
`src\xhci98.rc`, and the root-hub id `XHCI98\ROOT_HUB`. Each model line has a
role, controller or root hub, and the controller-only rules (`VAL-*`,
`PROP-*`, `OS-*`) skip the root-hub model. Four rules are the HCD's own:

| Rule | Refuses |
|---|---|
| `HCD-ROOTHUB` | a models section with no controller, a models section with no `XHCI98\ROOT_HUB` model, and any model line matching `USB\ROOT_HUB` or `USB\ROOT_HUB20`, the OS hub driver's ids for usbport's root hubs |
| `HCD-HUBPAGE` | a root-hub install that writes no hub page, or the wrong one: `EnumPropPages` = `"sysclass.dll,USBHubPropPage"` on the 9x path, `EnumPropPages32` = `"usbui.dll,USBHubPropPageProvider"` on every NT path (section 8.5) |
| `VAL-HCDVHUB` | any of the three `XhciVirtualHSHub*` values written: the HCD reads none (section 4) |
| `OS-HCDREPLACED` | `usbport.sys` or `usbhub.sys` fetched on any path: the HCD replaces both. `usbd.sys` (the leaf class drivers import its helper exports) and `usbui.dll` (both tabs) stay on the paths the miniport fetched them on |

The rules that carry over unchanged are the file rules (ANSI, CRLF, no BOM),
the Windows 98 setup-engine rules (28-character section names,
first-section-wins, dirid 12, 8.3 names), `$CHICAGO$` and the USB `ClassGUID`
(both devnodes are USB class), the path rules (exactly one `AddService`,
kernel driver, demand start, normal error control, the binary delivered by
the same section), `PATH-MFGDEC`, `PATH-NO9X`, `OS-DEFAULT`, the
selective-suspend and package rules. `scripts\inf-gate\test-inf-checks.ps1`
holds the cases: both INFs pass their own gate with no FAIL line and
`models: 4` (the controller and the root hub in each models section), and
each new rule fires on a copy broken in the one way it exists to catch (the
`hcd-*` and `vhub-written-*` cases, and the `OS-HCDREPLACED` cases, across the
two files).

**`scripts\package\make-package.ps1` refuses the scaffold.** Any image
carrying `XHCI98_SCAFFOLD_DO_NOT_STAGE` is refused first, before any gate,
with no switch that admits it (the probe build had `-FailStartArtifact` as its
one exception; the scaffold has none). `test-package.ps1` holds the case: a
stand-in carrying the scaffold marker and a correct flavour marker is
refused, the message names the build scaffold, and no output directory is
created.

### 9.3 What waits, and for which task

| Item | Waits for |
|---|---|
| `make-package.ps1` staging the HCD: the scaffold marker removed, the package built from `src\xhci98.inf` / `-amd64.inf` and `src\obj<fl>\<arch>\xhci98.sys` as before | done in 26-A.1 (2026-10-03) |
| `make-release.ps1`: the scaffold refusal in its publish loop (defence in depth; it already reads the flavour marker per binary, `Get-ImageFlavourMarker`), then the `2.0.0.0` cut publishing `xhci98-<version>.zip` and its four directories under `releases\<version>\`, beside the frozen `releases\1.2.0.0` | the refusal done in 26-A.1 (2026-10-03); 32.3 (the cut) |
| `gen-offsets.ps1` for the HCD's counter block (9.5) | 26-A.8 / 26-A.10 |
| The matrix's read route for the HCD (9.4) | 26-A.8 / 26-A.10 |
| The 26-A.10 expectation set (9.6) and `selftest.ps1` vectors for every HCD branch | 26-A.10 |

The plan's `OriginalFilename` check - refusing an image whose version
resource names the other product - distinguished two binaries that no longer
coexist; with one name it has nothing to distinguish. `check-flavour-marker.ps1` needs no change. `source-stamp.ps1`
hashes what `src\sources` names plus `src\*.h`, as it did for the miniport;
the Adapted `.c` files in `src\` that `sources` does not name are not part of
any image until 26-A.2 adds them.

### 9.4 What `scripts\vm-matrix` reads, and how it reads the HCD

**Under the miniport.** The transport is the QEMU monitor's `x/<n>wx <addr>`
against guest kernel memory, coalesced into runs of at most 32 words
(`lib\counters.ps1` `Read-Counters`, 190-277); nothing runs in the guest.
Every expectation is a delta across a row's window (`Get-CounterDelta`,
283-310), and a negative delta voids the window as a restart. The base
address is read off the port `0xE9` debug console, which only the `qemu`
flavour writes: x86 takes `a=` from `cb <Name> irql=NN a=<VA>` lines through
a kernel-address filter, amd64 the `StartController extension VA high=` /
`low=` pair (`Find-ExtensionIdentity`, 384-440). The address was usbport's
miniport extension, which usbport zeroed before every `StartController`.
Freshness is `MiniPortExtensionSize=` on the same console against the
table's `SIZEOF` (`lib\qemu.ps1` `Assert-OffsetsFresh`, 217-237;
`lib\fresh.ps1` `Get-StampProblems`, 626-657), and a reload is "more than one
VA or size in the log" (`Spans`). The table, `offsets.txt` /
`offsets-amd64.txt` and their `.labels.txt`, was generated by
`gen-offsets.ps1` from every `XHCI_DBG_VALUE_CHANGED("<label>", ext-><Field>)`
site in `src\*.c`: about 430 fields of `XHCI_EXTENSION`, `SIZEOF 104740` on
x86 at `5086e2d`. The harness itself names `transfers completed`,
`endpoints opened`, `devices addressed`, the eight refusal labels
(`lib\verdict.ps1`, about 224-233) and the three `vhub` labels, the last read
only after `cb RH_GetRootHubData` appears.

**The read route for the HCD is the same monitor read, not the door.** The
PassThru door of section 8.8 is a user-mode route a program inside the guest
takes; the matrix deliberately runs nothing in the guest. What changes is
where the address comes from and what it points at:

1. **A DDK-free counter block, not the extension.** The HCD's FDO extension
   will hold `KDPC`, `KEVENT`, `KTIMER`, device-object pointers and lock types
   the host-test build (`XHCI_HOST_TEST`, `src\xhci_compat.h`, no `ntddk.h`)
   cannot compile, and an `offsetof` program built against host stand-ins for
   those types would print a layout the kernel build does not have. So every
   counter the matrix reads lives in one struct (working name
   `XHCIHC_COUNTERS`) in a header with no `ntddk.h` dependency, embedded in the
   controller FDO's extension, with the lifted `XHCI_TOPOLOGY` inside it so
   the `Topology.*` labels keep their spelling. The root-hub FDO, the hub
   objects and the function PDOs count into the controller's block through a
   pointer: one block per controller.
2. **A debug-console identity line with address, size and start number.** The
   `qemu` flavour prints at every controller start a line carrying the
   block's VA, its `sizeof` in hex and `start=<n>` (the plan's form:
   `counters VA=<VA> size=<hex> start=<n>`), and on amd64 the VA split into
   `high=` / `low=` lines, the miniport's amd64 lesson. `start=<n>` is a
   per-load generation number, needed because a pool extension does not move
   on a stop and start the way usbport's extension contents were zeroed: a
   resource-rebalance `STOP_DEVICE` / `START_DEVICE` keeps the same FDO and
   VA, and the miniport's reload detection was "a second VA". The HCD zeroes
   the block at each controller start, so a restart inside a window still
   shows as a negative delta, and `Spans` becomes "more than one (VA, start)
   pair".
3. **No root-hub-data wait.** `Test-RootHubDataAsked` and the switch read go
   (there is no usbport to ask and no switch to read), and the report's
   `# vhub` line goes with them.

What changes in the harness with them: the identity regexes and the size
line's name (`Find-ExtensionIdentity`, `Assert-OffsetsFresh`,
`Get-StampProblems`), the offsets table the run loads, and the expectation
set (9.6). `XHCISNAP` reads windows through PassThru with no offset table and
is decoded offline; for the HCD the PassThru region names the counter block,
and an offline decode uses the HCD's offsets table keyed by the block's
`SIZEOF`. That is 26-A.8's.

### 9.5 `gen-offsets.ps1` for the HCD

| Fixed to the miniport at `5086e2d` | Line | For the HCD |
|---|---|---|
| source glob `src\*.c` | 132 | the files `src\sources` names, not the glob: the Adapted files stay in `src\` unbuilt until 26-A.2, and their `ext->` sites name `XHCI_EXTENSION` fields that no longer exist in any built struct |
| print grammar `XHCI_DBG_VALUE_CHANGED("label", ext->Field)` | 144 | the identifier before `->` becomes a parameter. The macro and the one-literal-label rule stay, so the derivation and its `-AllowRemovals` refusal (173-190) carry over unchanged |
| `#include "xhci.h"` | 200 | the counter block's DDK-free header |
| `offsetof(XHCI_EXTENSION, f)`, `sizeof(XHCI_EXTENSION)` | 202, 206 | the counter block's struct |
| `/DXHCI_HOST_TEST` | 241 | unchanged: the block's header must compile under it, which is the point of 9.4 item 1 |

`-Arch amd64` (WDK 7.1 `cl`) and the Smart App Control retry carry over.

### 9.6 The 26-A.10 expectation set

`matrix.psd1` encodes the miniport's behaviour, not a device's (roadmap
26-A.10). The plan kept the device population, the steps and the guest facts
as they are - `Groups`, each row's `Name`, `Model`, `AddArgs`, `Child`,
`NeedsNetdev` / `NeedsChardev`, `Settle`, `Steps`, `ExcludedOnTarget`,
`ExpectNoDriver`, `MayWedgeGuest` and `Pump` - and put the HCD's
expectations (its own `Always`, and per row `Expect`, `ExpectByTarget`,
`ExpectedSpeed`, `ClaimLabel`) in a set whose validator refuses a row with
no HCD decision, so every row is decided rather than inherited by omission,
and refuses `ExpectBySwitch`. The plan was written for a miniport set kept
beside the HCD's; with the miniport out of the tree its expectations describe
a binary the tree no longer builds, and whether they stay as a frozen file or
leave for the 1.2.0.0 branch is 26-A.10's. The verdict rules of design
record 06 are kept as they are (`Get-RowOutcome`'s order: ERROR, restart,
unread, INERT, refusal, PASS, NODRIVER, FAIL); what changes is the label sets
that code names (the permanent and transient refusal labels, and the
`AddressedLabel` / `ClaimedLabel` defaults).

| Expectation (label) | Where | Bound to | For the HCD |
|---|---|---|---|
| `advance devices addressed`, `advance slots enabled` | Always | xHCI | kept: the bus's own Address Device and Enable Slot |
| `zero fatal controller status`, `zero transfer events for no open endpoint`, `zero commands the engine gave up on` | Always | xHCI | kept |
| `zero interrupt mask failures` | Always | the IMAN readback inside usbport's enable/disable callbacks | kept if the HCD keeps the readback; else dropped |
| `zero endpoint opens refused - unusable buffer` / `- malformed call` | Always | **usbport**: the buffer and call shape it handed `OpenEndpoint` | dropped; replaced by the HCD's own URB refusals |
| `zero endpoint refusals - type` / `- params` / `- ring pool` | Always, refusal set | the open path, usbport-shaped | mapped to the HCD's select-configuration refusals (the same three causes, the bus's own counters) |
| `endpoint refusals - no device` | refusal set | **usbport**'s device handle | dropped: the PDO is the device |
| `endpoint refusals - not ready` (transient) | refusal set | **usbport**'s retry | dropped: the bus finishes enumeration before it reports the PDO |
| `zero endpoint configure failures`, `endpoints refused - no bandwidth` / `- no resources` | Always, refusal set | Configure Endpoint completion codes | kept, same labels |
| the nine-term open-accounting identity | Always | **usbport**'s `OpenEndpoint` accounting | dropped; a new identity over the HCD's select-configuration pipe accounting |
| `advance endpoints opened >= N` | rows; NODRIVER; readiness | a non-default endpoint opened for a function driver | kept, label unchanged, **defined as an endpoint added at a function PDO's `SELECT_CONFIGURATION` or `SELECT_INTERFACE`** - never the hub status-change pipe the bus opens for itself |
| `zero endpoint speed mismatches` (kbd/hs); the `ExpectBySwitch` speed-mismatch rows (kbd/fs, mouse/fs) | rows | **usbport's High-Speed lie** and the **virtual hub** | gone, with a comment beside each row - not `inert`, which the harness would resolve to a real counter field and read, so an absent field would be an error rather than a reading. In their place each row carries `ExpectedSpeed = 'HS' / 'FS' / 'LS'`, and the harness asserts two per-speed delta counters the HCD keeps - devices addressed at a speed (the decoded `PORTSC` speed) and slot contexts programmed at a speed - each `== 1` for the row's speed and zero for the other two, plus `zero slot speed disagreeing with port speed` in `Always`. A wrong speed is then a FAIL, never a silence |
| `iso packets answered` (inert), `iso missed service errors`, `iso packet errors` | audio row | xHCI isochronous | kept |
| `transfers submitted == transfers completed + transfers cancelled` | storage row | transfer accounting | kept |
| the topology labels (`hub descriptors folded`, `- malformed`, `nodes dropped`, `behind-hub devices addressed`, `behind-hub opens`, `behind-hub refused - too deep`, `hub slots marked`) | hub rows | the lifted topology graph | kept; the graph is fed by the bus's own hub traffic (27-A.2) |
| `topology: behind-hub refused - no record` | hub rows | **usbport** opening a device the snoop never saw | dropped: the bus creates the record before it addresses |
| `topology: TT pairs disagreeing with usbport` / `agreeing with usbport` | churn | **usbport** naming a TT; the **virtual hub** at switch 1 and 2 | gone. `TT pairs programmed` is inert because QEMU models no High-Speed hub (27-V.1) |
| the churn `'1,2'` rows `devices addressed == 10`, `behind-hub devices addressed == 9`, `behind-hub opens == 9` | churn | the **virtual hub**'s extra tier | gone. Without the virtual hub the five-tier chain is five deep in Windows' view again and the tier-5 mouse is addressed (27-V.1). Derived from the row's `Steps`, to be confirmed by the first run: `devices addressed == 11` (the first hub and ten behind-hub enumerations), `behind-hub devices addressed == 10` |
| `vhub started` / `hubs created` / `hubs dropped` | the harness's switch read | the **virtual hub** | not read |
| `transfers completed` | keep-alive liveness | transfer accounting | kept |
| the `ExpectByTarget` audio inert lines; `MayWedgeGuest` on 2a | audio row | guest facts | kept |

Two consequences the set must carry:

1. **The hub rows' NODRIVER trap.** Under the miniport `usbhub.sys` was the
   hub's function driver and opened its status-change pipe, so
   `endpoints opened` advanced on the `usb-hub/fs` row. Under the HCD no
   function driver binds an external hub (section 10), so with the
   definition above `endpoints opened` stays 0 while `devices addressed`
   advances, which `Get-RowOutcome` reads as NODRIVER. Hub rows therefore
   name `ClaimLabel = 'hubs started by the bus'` (a counter the hub class
   moves when it arms the status-change pipe), which `Get-RowOutcome`'s
   existing `ClaimedLabel` parameter takes: no change to the rule, a change
   to which counter a row names.
2. **A direct NODRIVER witness becomes possible.** The bus sees whether a PDO
   it reported ever received `IRP_MN_START_DEVICE`. A `device PDOs started`
   counter (and for composites `function PDOs created` / `function PDOs
   started`) lets a row assert the bind directly, as an additional
   expectation; design record 06's inference rule is not replaced.

## 10. Hubs and composite devices inside the bus (task 25.6)

The owner's decision (section 4; the roadmap's decisions table, "Hub class
and composite splitting") is that external hubs are **objects of the bus,
not PDOs**, and that a composite device is **split by the bus into
per-function PDOs**; there is no INF binding path for an external hub or a
composite parent. This section is the design that follows: the hub class the
bus speaks itself, the topology and removal it owns, and the ids, grouping
and routing of the function PDOs it creates. It is consumed by 26-A.4 (ids),
26-A.7 (composite splitting) and 27-A.1 to 27-A.3 (the hub class, topology
and removal).

**Sources and method.** The hub request codes and port feature selectors are
from `src\xhci_topo.h` (lines 112-161, taken from the miniport's own measured
QEMU bus trace of both Windows hub drivers, batch 7b-V0, already recorded
under design record 02) and ReactOS `usbport.h` lines 48-59 as interface
documentation. Port status bits and the hub descriptor are from WDK 7.1's
`inc\api\usb100.h` and `usb200.h`. "xHCI p.N" is a page of the xHCI 1.2c PDF
(`docs/references/README.md`). **The USB 2.0 specification is not in
`docs/references/`**, so every number cited only to it is marked **(to
transcribe)** and is read from the specification, added there with its hash
first, before 27-A.1 codes it. The id strings and the grouping rule were read
out of each target's media by subagents on 2026-10-02 - the INFs as text,
`usbhub.sys` and `usbccgp.sys` as UTF-16 string dumps, and Windows 7's and XP
SP3's `usbccgp.sys` with `kd -z` against their public PDBs - none of it re-read
line by line by the coordinator; all of it static.

### 10.1 The hub class request set

All on the hub's default pipe, issued by the bus itself.

| Request | `bmRequestType` | `bRequest` | `wValue` | `wIndex` | `wLength` | Source |
|---|---|---|---|---|---|---|
| GET_DESCRIPTOR (Hub) | `0xA0` | 6 | `0x2900` (type `0x29`, index 0); **both Windows hub drivers send `0x0000`** (design record 02, "The graph"; measured). The bus sends `0x2900` and retries once with `0x0000` on a STALL | 0 | 71 is what Windows sent; the descriptor is 7 + 2 x ceil((ports + 1) / 8) bytes (to transcribe, USB 2.0 11.23.2.1) | `xhci_topo.h` 112, 121, 143 |
| GET_STATUS (hub) | `0xA0` | 0 | 0 | 0 | 4 | `wHubStatus` / `wHubChange` bits, local power and over-current (to transcribe, USB 2.0 11.24.2.6) |
| CLEAR_FEATURE (hub) | `0x20` | 1 | C_HUB_LOCAL_POWER 0, C_HUB_OVER_CURRENT 1 (to transcribe, Table 11-17) | 0 | 0 | |
| GET_STATUS (port) | `0xA3` | 0 | 0 | port | 4 | `xhci_topo.h` 114, 118, 165 |
| SET_FEATURE (port) | `0x23` | 3 | PORT_RESET 4, PORT_SUSPEND 2, PORT_POWER 8 | port | 0 | `xhci_topo.h` 113, 120, 134-135; ReactOS `usbport.h` 50-53 |
| CLEAR_FEATURE (port) | `0x23` | 1 | PORT_ENABLE 1 (disable), PORT_SUSPEND 2 (resume), PORT_POWER 8, C_PORT_CONNECTION 16, C_PORT_ENABLE 17, C_PORT_SUSPEND 18, C_PORT_OVER_CURRENT 19, C_PORT_RESET 20 | port | 0 | ReactOS `usbport.h` 48-59; 16, 17 and 20 also in the measured trace |
| SET_INTERFACE (multi-TT) | `0x01` | 11 | alternate 1 | 0 | 0 | `xhci_topo.h` 115, 124; only on a hub with `bDeviceProtocol` 2 (`test-equipment.md` rows 3 and 4: the multi-TT `1A40:0201` carries alternate 1, protocol 2) |
| CLEAR_TT_BUFFER | `0x23` | 8 | device address, endpoint number, type and direction packed (to transcribe, USB 2.0 11.24.2.3) | the TT port, 1 on a single-TT hub (to transcribe) | 0 | needed by xHCI p.102 and p.116 (10.4) |
| RESET_TT / GET_TT_STATE / STOP_TT | `0x23` / `0xA3` / `0x23` | 9 / 10 / 11 | | | | (to transcribe, Table 11-16); not used in 2.0.0.0 |

`wPortStatus` bits (`usb200.h` 31-38): connect `0x0001`, enable `0x0002`,
suspend `0x0004`, over-current `0x0008`, reset `0x0010`, power `0x0100`,
low-speed `0x0200`, high-speed `0x0400` (neither speed bit means Full Speed);
test `0x0800` and indicator `0x1000` (to transcribe). `wPortChange`: bit 0
C_PORT_CONNECTION (design record 02, the disconnect row); bits 1 to 4
C_PORT_ENABLE, C_PORT_SUSPEND, C_PORT_OVER_CURRENT, C_PORT_RESET (to
transcribe, USB 2.0 Table 11-22). The hub descriptor (`usb100.h` 202-212,
`xhci_topo.h` 146-161): `bNbrPorts` at offset 2, `wHubCharacteristics` at 3-4
with TTT in bits 6:5 (xHCI p.85 quotes USB 2.0 Table 11-13 for it),
`bPwrOn2PwrGood` at 5 in 2 ms units (the `usb100.h` comment),
`bHubContrCurrent`, then the removable and power-mask bitmaps; the
power-switching mode (bits 1:0), compound (2), over-current mode (4:3) and
port indicator (7) bits are to transcribe from Table 11-13.

**The status-change pipe** is the hub's one interrupt IN endpoint. Its report
is a bitmap - bit 0 the hub, bit N port N, `ceil((bNbrPorts + 1) / 8)` bytes
(to transcribe, USB 2.0 11.12.4). The bus keeps exactly one transfer
outstanding on it from the moment the hub is configured until it is removed,
at the interval of the endpoint's own `bInterval` (26-A.5's rule). On
completion, for each set bit: GET_STATUS for that port or the hub, act on the
change bits, clear each with its CLEAR_FEATURE C_*, and re-arm only after
every bit of the report has been handled. A completion with a transaction
error is retried with back-off; a second consecutive failure, or a
device-not-responding completion, is the hub's removal (10.5).

**Where the hub state machine runs.** Every step is an event on a control
transfer's completion or a timer, with no blocking wait, so it runs at
`DISPATCH_LEVEL` off transfer completions, with 10.2's delays taken by a
`KTIMER` and `KDPC` per hub, rather than needing a PASSIVE context Windows 98
may not provide (the `Io*WorkItem` family is denied, section 7.6). The one PnP
call it makes is `IoInvalidateDeviceRelations`; whether Windows 98 allows it
at `DISPATCH_LEVEL` is open (10.9), and if not, that notification is the one
step handed to the bus's PASSIVE worker.

### 10.2 Port reset and enable, and its timings

**A root port** (xHCI):

1. `PORTSC.CSC` with `CCS` = 1 raises the event; clear CSC with the
   neutral-write pattern (`xhci-programming.md`, "Port Management").
2. Connect debounce, 100 ms (`TATTDB`, to transcribe, USB 2.0 7.1.7.3);
   abandon if `CCS` dropped.
3. `PR` = 1; the xHC times the reset; wait for `PRC` with `PED` = 1 (xHCI
   4.3.1, p.74-75).
4. Speed from `PORTSC` bits 13:10 (1 FS, 2 LS, 3 HS; `xhci-programming.md`,
   "Speed Encoding"): the true speed, reported as such - no High-Speed lie.
5. Reset recovery, 10 ms (`TRSTRCY`, to transcribe, USB 2.0 7.1.7.5; xHCI
   p.118: "Software shall be responsible for timing the Reset 'recovery
   interval' required by USB").
6. Enable Slot; Address Device (BSR = 0) with Route String 0 and the root port
   number; SetAddress recovery 2 ms (`TDSETADDR`, to transcribe, USB 2.0
   9.2.6.3; xHCI p.102: "Software shall be responsible for timing the
   SetAddress() 'recovery interval'").
7. Descriptors: on Full Speed the 64-byte initial EP0 size, then Evaluate
   Context to the real `bMaxPacketSize0` (the miniport's Finding 2 fix,
   `XHCI_EP0_MPS_FULL_INITIAL` 64); Low Speed 8; High Speed 64.

Port power on a root port with `PPC` = 1: 20 ms after `PP` before any state
change (xHCI 5.4.8, p.371).

**An external hub's port** (the bus's own hub):

1. Hub configured (10.3), every port powered, then wait `bPwrOn2PwrGood` x 2
   ms before reading any port.
2. Status-change bit N -> GET_STATUS(N). On C_PORT_CONNECTION:
   CLEAR_FEATURE(C_PORT_CONNECTION); if connected, debounce 100 ms, re-reading
   GET_STATUS until stable (`TATTDB`, to transcribe).
3. **Serialise**: one device between port reset and Address Device
   completion on the controller at a time. A USB 2.0 hub repeats downstream
   traffic to every enabled port, so two devices in the Default state below
   one hub would both answer the default address; a controller-wide lock is
   the simple form, and the window is tens of milliseconds.
4. SET_FEATURE(PORT_RESET). The hub times the reset (`TDRST`, 10-20 ms on a
   hub port, to transcribe, USB 2.0 7.1.7.5). Wait for C_PORT_RESET (a status
   change or a GET_STATUS poll) with a 500 ms time-out - a bus policy number,
   not a specification one - then CLEAR_FEATURE(C_PORT_RESET).
5. The enable bit must be set; speed from bits 9 and 10 (LS `0x0200`, HS
   `0x0400`, neither FS; `usb200.h` 37-38).
6. Reset recovery 10 ms, then Enable Slot and Address Device with the full
   path of 10.4, then the root port's steps 6 and 7.
7. Failure policy: three attempts (reset, address, device descriptor), then
   CLEAR_FEATURE(PORT_ENABLE), the port left disabled until its next connect
   change, and a counter. If Address Device fails with a USB Transaction
   Error behind a TT, CLEAR_TT_BUFFER first (xHCI p.102: "If an Address
   Device Command fails with USB Transaction Error and the target device is
   behind a TT, software shall issue a ClearFeature(CLEAR_TT_BUFFER) request
   to TT in the HS hub").

**Suspend and resume**, the minimum (selective suspend is not on the roadmap):
resume is signalled for at least 20 ms (`TDRSMDN`), and the xHC answers a
device-initiated resume within 1 ms (`TURSM`), both quoted by xHCI p.256 from
USB 2.0 7.1.7.7; after a port reports suspended (`PLS` = 3), wait at least 10
ms before resuming it (xHCI p.454). Resume recovery (`TRSMRCY`, 10 ms) is to
transcribe. Through an external hub: SET_FEATURE(PORT_SUSPEND) /
CLEAR_FEATURE(PORT_SUSPEND), then C_PORT_SUSPEND.

**At a SuperSpeed hub's port** (decided when Phase 27's hub-port resume met
Phase 30's SuperSpeed hub on `p28-31-int`, 2026-10-04): the same rule,
handled and never initiated, through the same code. Phase 30 had no suspend
or resume of its own - a link found in U3 under a held device was left as
it was, and only a reset noticed it (a warm one, since a hot reset cannot
start from U3) - so there was nothing to reconcile, only a gap to close.
`XhciSsHubPortDecide` now names a connected, enabled port whose link reads
U3 under a device the machine holds (`Resume`), and a `C_PORT_LINK_STATE`
with the link back in U0 under one (`Resumed`, a finished host-requested U3
exit; a remote-wake U3 exit sets no `C_PORT_LINK_STATE`, USB 3.2
10.16.2.6.2, printed p.449), after every other rule; `HcdHubPortLook` carries both out
exactly as for a USB 2.0 port - the devices below quiesced, the link asked
to U0 with SET_FEATURE(PORT_LINK_STATE) and U0 in `wIndex` 15:8, progress
read by `XhciSsHubResumeProgress` (U3 or Recovery pending; U0, U1 or U2
enabled done; anything else, the reserved link states 0xC to 0xF included,
re-enumerated; disconnected gone),
`C_PORT_LINK_STATE` cleared, the 10 ms recovery waited, and the same
outcome, retry and give-up rules - a resume whose last reading carries
`C_PORT_CONNECTION` is a replaced device and is enumerated afresh, at
either speed (`XhciHubResumeSettle`). No SuperSpeed port is resumed before a
reset: a warm reset may start from U3 (`XhciSsHubResetKind`). The quiesce
reaches streams: an endpoint with streams open is stopped when any stream
has work, and rung again per stream by its Stream ID. The request's
encoding, `C_PORT_LINK_STATE` on a host-directed U3 exit and `PORT_ENABLE`
staying set in U3 were read from USB 3.2 r1.1 10.16.2.6 and 10.16.2.10
(Codex review of the merge, printed pp.446-454); no QEMU model has a SuperSpeed
hub, so `test_sshub`'s `test_resume` vectors are the only evidence until
the bench (30-E.1).

### 10.3 The hub inside the bus

**Bring-up, in this order.** A device whose device descriptor says class
`0x09` (`usb100.h` 116) is taken by the bus and never reported as a PDO:

1. SET_CONFIGURATION (its one configuration); on `bDeviceProtocol` 2,
   SET_INTERFACE alternate 1 (multi-TT). `bDeviceProtocol` 0 is a Full-Speed
   hub, 1 single-TT, 2 multi-TT (`test-equipment.md` rows 3 and 4 read these
   values off real units).
2. GET_DESCRIPTOR(Hub).
3. **Configure Endpoint with the hub marking and the status-change endpoint
   together**: `Hub` = 1, `Number of Ports` = `bNbrPorts`, and on a High-Speed
   hub `TTT` from bits 6:5 and `MTT` = 1 if the multi-TT alternate was enabled
   (xHCI p.85: "Multi-TT (MTT) = '1' if the Multi-TT Interface of the hub has
   been enabled with a Set Interface request"). An Evaluate Context cannot set
   these fields and an Address Device clears them (`xhci-data-structures.md`,
   "Which command may set which Slot Context field"). The lifted
   `XhciTopoHubMark` derives them.
4. SET_FEATURE(PORT_POWER) on every port; wait for power-good; GET_STATUS
   every port once (on some hubs a device present at power-on raises no
   change - read, do not assume); arm the status-change pipe.
5. Only then reset any downstream port. xHCI p.74: "software shall fully
   initialize the hubs and TTs of each tier of the USB topology before
   proceeding to the next tier, starting at the Root Hub."

The topology graph (`xhci_topo.c`) is fed the bus's own requests and replies
through the same `XhciTopoObserveSetup` / `XhciTopoObserveReply` entry points
the miniport fed from its snoop (27-A.2), so design record 02's counters and
host vectors carry over. `XHCI_TOPO_NODES` 8 (`xhci_topo.h` 197), the
miniport's table of hubs, becomes a pool-backed count for a bus that owns
every hub (section 7.5).

**Depth.** The Route String has five 4-bit tiers (`xhci-data-structures.md`,
"Route String tier order"; `XHCI_TOPO_MAX_TIER` 5, `xhci_topo.h` 205), so a
device sits at most five hubs below the root hub, and a deeper path "must be
refused, not truncated: a truncated route names a different, real device".
USB 2.0 allows seven tiers counting the root hub as tier 1, so at most five
non-root hubs in a chain (to transcribe, USB 2.0 4.1.1); xHCI's glossary
defines the tier the same way (p.33: "Root Hub A (tier 1)"; p.30: "Hub Tier
One plus the number of USB links in a communication path"). The two limits
coincide: the fifth hub's devices are at tier 7 with a five-nibble route. So
the bus addresses a hub at any depth it can route but **does not configure a
hub whose children would be unroutable** (a sixth hub in a chain): it leaves
that hub addressed and unconfigured with its downstream ports unpowered, and
counts `topology: hubs refused - too deep`. No PDO exists to show a problem
code on; the counter and the log are the record. A port number above 14 on a
High-Speed or Full-Speed hub is written as 15 in its route nibble (xHCI Table
6-4 footnote 106, quoted in `xhci-data-structures.md`). Without the virtual
hub, the matrix's five-tier chain is five deep again and its tier-5 mouse is
addressed (27-V.1; section 9.5).

### 10.4 TT assignment (design record 02, carried over)

The bus knows every hub's speed and alternate setting first-hand, so the TT
is derived from the graph alone, with no usbport pair beside it to disagree
with (design record 02 Step 3 already made the graph the decider;
`TtPairsAgreed` / `TtPairsDisagreed` retire). For a device behind hubs:

| Slot Context field | Value | Rule source |
|---|---|---|
| Route String (DW0 19:0) | the hub-port path below the root port, the root-most hub's port in bits 3:0 | `xhci-data-structures.md`, "Route String tier order" |
| Root Hub Port Number (DW1 23:16) | the root port the path starts at | design record 02 section 1 |
| Speed (DW0 23:20) | the decoded `wPortStatus` speed | 10.2 |
| Parent (TT) Hub Slot ID (DW2 7:0) | the slot of the **nearest High-Speed ancestor hub**, only if the device is FS or LS; else 0 | `XhciTopoTtFor`; xHCI Table 6-6 |
| Parent (TT) Port Number (DW2 15:8) | the port **on that High-Speed hub** under which the device's subtree hangs (for `2.2.1`, behind a Full-Speed hub on High-Speed hub port 2, it is 2); FS/LS only | design record 02 Step 3 |
| MTT (DW0 25) | 1 if that High-Speed hub's multi-TT alternate is enabled; OR'd with the hub's own reason for a Full-Speed hub behind a multi-TT hub | design record 02 Step 3 |
| TTT (DW2 17:16) | 0 on every non-hub and every non-High-Speed hub; only a High-Speed hub's own slot carries it | Table 6-6, p.409, as transcribed |

Kept verbatim from design record 02: an unresolvable TT (a High-Speed ancestor
with no slot) **refuses** the Address Device rather than sending cleared
fields; an all-Full-Speed path (QEMU's `usb-hub` on a root port) has no TT
anywhere and every DW2 field is 0; a re-address re-marks a hub; a refused
marking is counted and not retried.

TT housekeeping that usbport did and the miniport never owned: when a Control
or Bulk endpoint of a FS/LS device behind a TT halts or is reset, the bus
issues CLEAR_TT_BUFFER to the TT hub as part of xHCI's reset-a-pipe sequence
(xHCI p.116: "If the device was behind a TT and it is a Control or Bulk
endpoint: Issue a ClearFeature(CLEAR_TT_BUFFER) request to the hub"), and
after an Address Device transaction error behind a TT (10.2).

### 10.5 Hub removal and subtree teardown

Triggers: the hub's upstream port reports a disconnect (a root `PORTSC` CSC
with `CCS` 0, or the parent hub's C_PORT_CONNECTION with connect 0), the
status-change pipe fails twice (10.1), or the parent disables the hub
(C_PORT_ENABLE with enable 0). The same walk serves every way a device
leaves - a root port or a parent hub reporting a disconnect, an enumeration
that fails, a CYCLE_PORT, an HCRST that took every slot, the root hub's
removal - as one teardown (`hcdSubtreeGo`, `src\hcd_enum.c`). For the
subtree rooted at the departing device, the hubs taken deepest first
(`XhciHubReleaseOrder`, checked by the host suite):

1. **Freeze the subtree.** Every hub node below and including the departing
   one stops re-arming its status-change pipe and accepts no new port work
   (Draining); an enumeration in flight in the subtree is abandoned, and the
   serialisation lock of 10.2 released. Every device in it is marked gone and
   rings no doorbell from here on: a URB submitted to it is refused
   `STATUS_DEVICE_NOT_CONNECTED` / `USBD_STATUS_DEVICE_GONE` at the next tick
   (`HcdIoRefuseLater`).
2. **Report the PDOs missing first** (`hcdReportGone`), as each device is
   frozen and before any of its URBs leaves the ring:
   `IoInvalidateDeviceRelations(BusRelations)` on the root-hub PDO's stack
   (every device PDO is a child of the root hub); the next
   `IRP_MN_QUERY_DEVICE_RELATIONS` omits the subtree's PDOs, so NT sends
   `IRP_MN_SURPRISE_REMOVAL` then `IRP_MN_REMOVE_DEVICE`, and Windows 98 sends
   `IRP_MN_REMOVE_DEVICE` alone (its out-of-sequence remove, 26-A.2). A PDO
   reported missing is deleted at its `REMOVE_DEVICE`, never before.
3. **Stop the endpoints and park the URBs - not complete them.** Leaf first,
   with the slots still enabled, each device's running endpoints are stopped
   (Stop Endpoint) and every URB IRP of the departed device - queued, on the
   ring, or waiting for a transfer record - is released from the hardware
   and **held on its PDO**, cancellable (`HcdIoPark`, `src\hcd_io.c`). A held
   IRP is completed `STATUS_CANCELLED` / `USBD_STATUS_CANCELED` only at one
   of three events (`HcdIoParkedRelease`):
   - the client cancels it;
   - the client's ABORT_PIPE covers it. Every URB IRP is stamped at dispatch
     with its PDO's 64-bit submission sequence (`HcdIoStamp`), and an
     ABORT_PIPE records its own stamp as its pipe's horizon
     (`HcdIoAbortMark`). An ABORT_PIPE on a departed PDO is answered without
     a device record. A request at or below its pipe's horizon is completed
     at once rather than held, so a request the teardown reaches after the
     abort is not stranded;
   - the PDO's stop, surprise removal or removal.
   An HCRST that took the slots stops nothing; its URBs are drained as each
   record is freed.
4. **Release the hardware leaf-first.** Disable Slot for each device in
   post-order - children before the hub they hang off, the departing hub
   last - so no live slot's Parent Hub Slot ID or TT ever names a disabled
   slot. This does not wait for step 2's IRPs: after step 1 no PDO touches its
   slot. The Disable Slot comes once the endpoints are stopped (xHCI 3.3.3:
   "issued when a device is detached from the USB").
5. **Prune the graph deepest-first** (`XhciTopoDetach` per hub as it goes,
   with whatever is left below it) and count `topology: behind-hub devices
   gone` and `topology: nodes pruned`. A departed hub's object is freed once
   every port of it has settled, and until then the port it sat on waits for
   it.

**Why report first and park (Windows 98 SE, 27-V.1, 2026-10-04;
`runs/run-27.md`, "The Windows 98 SE HID-unplug stall").** This order
replaces the one this section first gave, in which the URBs were completed
DEVICE_GONE before PnP was told. Windows 98 SE's `hidclass.sys` answers a
read failing `STATUS_DEVICE_NOT_CONNECTED`, while its device is still
started, by failing every client read and resubmitting at once (`0x110A7`,
static; `run-26.md`, "The REMOVE that never ended"). Each resubmission is
refused at the next tick, and the retries kept the guest so busy that its
configuration manager sent the REMOVE that ends the started state only 129 s
to 11 minutes after a keyboard's unplug. Reporting the PDO first did not cure
it (`26e7cb6`); a read that never fails does (`f99f184`), and the REMOVE then
came within about a second on each unplug read. A request held for a device
that stopped answering is also what the hub drivers this bus stands in for
do. Windows 2000 reads the same order cleanly (SURPRISE_REMOVAL and REMOVE
within about a second, a stick pulled mid-copy included).

A root port going down sweeps everything behind it by the same walk; a device
pulled mid-transfer behind a live hub is the one-node case. A reconnect on
the same upstream port waits until the teardown has finished: the port's
connect change is queued behind it. The miniport's measured shape (design
record 02, "Step 4 observed": a whole hub out with three live devices below
it, taken leaf-first with nothing stranded) is the expected reading for
27-V.1, now produced by the bus rather than inferred from usbhub's traffic.

### 10.6 The ids the targets' INFs match

**Matching is case-insensitive on every target.** Windows 98 SE's own
`usbhub.sys` emits `USB\Vid_nnnn&Pid_nnnn&...&Mi_nn` while its `hiddev.inf`
lists `USB\VID_04D2&PID_FF47&MI_02`, and the NT INFs mix
`USB\CLASS_09&SUBCLASS_01` with `USB\Class_08&SubClass_06&Prot_50`.
Class, subclass and protocol are always two hex digits, VID, PID and REV four,
MI two.

| Target | HID | Mass storage | Audio | `USB\COMPOSITE` bound to |
|---|---|---|---|---|
| 98 SE (stock) | `USB\Class_03` (`hiddev.inf` -> `hidusb.sys`) | none stock. NUSB's `usbstor.inf`: `USB\Class_08&SubClass_0{2,5,6}&Prot_{50,00,01}`; NUSB 3.6 adds `SubClass_04&Prot_00` and `SubClass_08&Prot_50` / `_52` | `USB\CLASS_01` (`wdma_usb.inf` -> `usbaudio.sys`) | `usbhub.sys` (stock); `usbccgp.sys` under NUSB 3.6's `USB.INF` and SweetLow's `USB2.INF` |
| 2000 SP4 | `USB\Class_03&SubClass_01`, `USB\Class_03` (`input.inf`) | `USB\Class_08&SubClass_{02,05,06}&Prot_50` | `USB\CLASS_01` | service `usbhub` |
| XP SP3, XP x64 | as 2000 | as 2000 | `USB\CLASS_01` | service `usbccgp` |
| Vista SP2, 7 SP1 | as 2000 | as 2000, plus `SubClass_08&Prot_50` and `SubClass_08&Prot_52` | `USB\CLASS_01` | service `usbccgp` |

Per-interface ids in model lines: `USB\VID_xxxx&PID_xxxx&MI_nn` on every
target, and `USB\VID_xxxx&PID_xxxx&REV_xxxx&MI_nn` from XP on
(`wdma_usb.inf`). **No stock INF on any target binds `USB\Class_02` or
`USB\Class_0A` (CDC)**; Windows 98 SE's `usbcdc.inf` binds only by VID and
PID. The hubs: `USB\CLASS_09&SUBCLASS_01` and `USB\CLASS_09` on every target;
the root hubs `USB\ROOT_HUB` (98 SE `usb.inf`), `USB\ROOT_HUB` and
`USB\ROOT_HUB20` (2000 SP4 `usb.inf`, XP onward `usbport.inf`). The Vista and
7 INFs also carry Microsoft OS descriptor ids (`USB\MS_COMP_xxx[&MS_SUBCOMP_xxx]`,
for example `USB\MS_COMP_BLUTUTH`), and Windows 7's `bth.inf` binds
`USB\Class_E0&SubClass_01&Prot_01` where XP's and Vista's bind Bluetooth by
VID/PID (Vista also `USB\MS_COMP_BLUTUTH`).

The INFs read, by target (all saved to git-ignored
`tools\<target>-extracted\inf\`; the Vista and 7 INFs are UTF-16LE on disk
and hashed as the original bytes):

| Target | Medium | INF: SHA-256 |
|---|---|---|
| 98 SE | `Win98SE.iso`, the `PRECOPY1`-`2` set (`wdma_usb.inf` from `win98\WIN98_50.CAB`) | `usb.inf` `3d9dad93cd26fd4c2d87a6b95c5d5b2231deb113a6a7ad21326f9ba8fd3622f0`; `hiddev.inf` `494d438a256ea4c183f53430cb58a4ff5d573bbb6ebdc6a8ed7945266e18f914`; `wdma_usb.inf` `a3d560915ed42ff6b3a2abbb678e0c665f58390c56fbf837a127de767539346e`; `usbcdc.inf` `c797028836914b08a7449e1fcaac3f59df59ebf22e6e8a8a02dc422a50ca038e`; `keyboard.inf` `b1f205ce8105e8ba10c41542d60d97f36f9cbca7d0a5d745d14ee336aabf129e`; `msmouse.inf` `96d8ebbc9bcb4a256a645558b6a86a3517f9d73aa1d25ed391483cc1985942c4` |
| 2000 SP4 | `win2ksp4.ISO` `I386\*.IN_` | `usb.inf` `1b2f63a2f526c7678ecfd0f68c991e9094c5adaa6d266ca9df0ef6953049c917`; `input.inf` `d16f41f4c5984a0c8ef5c818ebb01ccb1fffb20527d80b063a2a74ce93207fac`; `usbstor.inf` `ab1a11ad89b44ff0e163f57baaaa15097c62fe4a62402cf9041c2c28cb99aed9`; `wdma_usb.inf` `ca2f443a101bb42161aa9d02616d091e0176846cd8bb789d2361c90701162dbe` |
| XP SP3 | `WinXP_pro_SP3_vl_x86.iso` `I386\*.IN_` | `usb.inf` `364f86b157ba808c3aa21b8b8946783fcfb35245155c49dda0f23c809fbac418`; `usbport.inf` `c1e9eb12d71a6ef407499a52a300f77998c9ee1b124ae7d4fe5bb1ec311b26ba`; `input.inf` `43c95300df420137db9c2749d1742346c85b1ed115b77f4113fbca1fff7c55c4`; `usbstor.inf` `e243c1e8d24705cead35cf823b5aa7783f3a4d7370a88cf910ac0ce42c2ed9d8`; `wdma_usb.inf` `2197fd0179830c29ba1a83710d6d358b6089abf65f1a5562d1d541bb96216f67` |
| XP x64 SP2 | `Win XP SP2 VL x64.iso` `AMD64\*.IN_` | `usb.inf` `99de5a2fb199b483d3eff256e0a45f7a8ca2246e0ef406fb32bc95e23d1d2b7f`; `usbport.inf` `d9479abd2b09c6443106b2f608bc817b7e6db93e615aa7d2919b87d8922715a5`; `input.inf` `9929365f69252d3e7bd030d895d6a371a514429c63d35daf47d42597c370486f`; `usbstor.inf` `bac35f866284b7d28b3d746994c6474c331f011e7755f37c0b1686030f053b95`; `wdma_usb.inf` `a5ed24984189d5994ae2f85e4e8f1aa5ace44b12796b20a4f70e7964975be88d` |
| Vista SP2 x86 | `install.wim` image 1, `Windows\inf\` | `usb.inf` `d9f64e8a451838e2a1f78f0f397a987126d9b04d854228379f451bcbd40e25a7`; `usbport.inf` `3553543fb38838d83e0f74f70eda9aba13a0eee9f4549b92003eb62f631a95bd`; `input.inf` `833e2ab9c292e662d029dfab6c89bdc1fff117d3d233977e07c319cb84848257`; `usbstor.inf` `1a249a62ca1d52fd65a6a650ec4813bf1cc4c9332927e30ae35192c33cbe1823`; `wdma_usb.inf` `38321169cd9c0459dcdabef19e77ee520ce1ae5e62358135d7c435f604fd4b8f` |
| 7 SP1 x86 | `install.wim` (single image), `Windows\inf\` | `usb.inf` `571cdf50837f269699ef7b54b53550df18140da41666196c407f0e584c87e19a`; `usbport.inf` `2814a1fd14b546aedc24ae0c64bed23625cd004d5a66582429f08b872b60d808`; `input.inf` `70482096a3b9786dd44258fef6cbf1fc7389fc7fc350b5cc1ade7ef164ac1256`; `usbstor.inf` `ed1cb867a49e9b22a0ed31644fdb96947bfcf6d243681c74e0c4c771f51d194c`; `wdma_usb.inf` `a60995fc8abc2de3439d478c3ff318ebfada4fb019a5159470e9b2bf2c4bcf43`; `bth.inf` `2549d9cdcf6da8aef81cbd3e0dbb84525fad118b99d1bc3bdf15c3efe9454620` |
| 98 SE, not stock | `tools\nusb36-extracted\USB.INF`, `tools\sweetlow-extracted\USB2.INF` | `f67ba17b...197839`, `a3d2b895...ab057a` (as recorded) |

The hub drivers' and composite parents' id templates (UTF-16LE string dumps,
no disassembly):

| File | Bytes | SHA-256 | Templates |
|---|---|---|---|
| `tools\win98se-extracted\usbhub.sys` | 35,680 | `e898b75f2449eb9e5bbcb3fadf7387c9819f1c7d2c6d49bad83d8236f46afc31` | `USB\COMPOSITE`, `USB\Vid_nnnn&Pid_nnnn&Rev_nnnn&Mi_nn`, `USB\Vid_nnnn&Pid_nnnn&Mi_nn`; fragments `Class_`, `SubClass_`, `DevClass_`, `USB\MI`, `USB\UNKNOWN`, `USB\SilentClasses` |
| `tools\winxpsp3-extracted\usbhub.sys` | 59,520 | `a99c4528c4227b1e96847614745aafacd3c5f1bdfe435214dbf78740ffb300fe` | `USB\Class_nn[&SubClass_nn[&Prot_nn]]`, `USB\DevClass_nn[&SubClass_nn[&Prot_nn]]`, `USB\COMPOSITE`, the two `Vid`/`Pid` `Mi_nn` forms, `USB\MS_COMP_`, `&MS_SUBCOMP_` |
| `tools\vista-x86-extracted\usbhub.sys` | 196,096 | `0b7ded0d887a3530aa5497fdbcb69389486fb9e2b6fae3163e33713256d575ba` | `USB\VID_nnnn`, `&PID_nnnn`, `&REV_nnnn`, `USB\Class_nn`, `&SubClass_nn`, `&Prot_nn`, `USB\DevClass_00[&SubClass_00[&Prot_00]]`, `USB\COMPOSITE`, `USB\MS_COMP_n`, `&MS_SUBCOMP_n`, `USB\UNKNOWN` |
| `tools\win7-x86-extracted\usbhub.sys` | 258,560 | `ac34d36dbb5649650fcd873a792ca1387ae841d4c46781c63c0d29834f9b58e9` | as Vista, plus `usbflags\CLASS_%02X[_SUBCLASS_%02X[_PROTOCOL_%02X]]` |
| `usbccgp.sys` 5.1.2600.5585 (`xpsp_sp3_qfe.080422-1455`): `tools\nusb36-extracted\usbccgp.sys` = `tools\sweetlow-extracted\XP_SP3.QFE\USBCCGP.SYS` | 32,384 | `4c1b3e8f3f658e356a955108ff84fb5c95244cb2a9d323aa0dfaef92927c66c5` | `USB\Class_nn&SubClass_nn&Prot_nn`, `USB\Class_nn&SubClass_nn`, `USB\Class_nn`, `&MI_%02x`, `&MI_xx`, `USB\MS_COMP_`, `&MS_SUBCOMP_`; no VID/PID template, no audio string |
| `usbccgp.sys` 5.1.2600.2180 (`built by: WinDDK`): `tools\sweetlow-extracted\USBCCGP.SYS` | 27,776 | `683061afb2350ba26732354df642fe67bcc89a764cc7ebe6016f4fcd87c86457` | as 5.1.2600.5585 |

### 10.7 The ids the bus generates

`vvvv`, `pppp`, `rrrr` are `idVendor`, `idProduct` and `bcdDevice` as four
hex digits; `cc`, `ss`, `pp` two hex digits; `nn` the function's first
`bInterfaceNumber`, two hex digits. Upper case: harmless under the
case-insensitive matching above, and the form most INF lines use.

**A device PDO** (a device the bus does not split):

| Query | Strings, in order |
|---|---|
| `BusQueryDeviceID` | `USB\VID_vvvv&PID_pppp` |
| `BusQueryHardwareIDs` | `USB\VID_vvvv&PID_pppp&REV_rrrr`, `USB\VID_vvvv&PID_pppp` |
| `BusQueryCompatibleIDs` | `USB\Class_cc&SubClass_ss&Prot_pp`, `USB\Class_cc&SubClass_ss`, `USB\Class_cc`: from the interface when `bDeviceClass` is 0 and the configuration has exactly one interface, from the device descriptor otherwise - so a multi-interface device the bus does not split (10.8) reports its own triple, `00/00/00` when its class is 0, never its first interface's, which would bind a class driver to the whole device (corrected 2026-10-03 by 26-A.7 from "from the interface when `bDeviceClass` is 0"; Windows 2000's `usbaudio.sys` bound to a whole composite device bugchecks, guest leg c14, `runs/run-26.md`). Whether to add the `USB\DevClass_cc...` forms XP's `usbhub.sys` carries templates for (and Vista's and 7's `USB\DevClass_00&SubClass_00&Prot_00`) is open (10.9); no stock INF in the table above matches a `DevClass` id |
| `BusQueryInstanceID` | the serial id when the device has a usable one that no present PDO of the same VID and PID carries already, with `UniqueID` TRUE in its capabilities: the NT instance path `USB\VID_0781&PID_5567\4C530001230920108174` (what Windows 98 SE and ME make of it is for task 33.2's guest legs to read); else the location key in decimal, `UniqueID` FALSE - root port 3 is `3`, and behind hubs the Route String sits above the port (`XhciHubInstanceKey`, route `0x31` over port 2 is `12546`). The rule is "Instance ids from the serial number" below (task 33.2; until `2.1.0.0` every device had the location form) |

**A function PDO** (one per function of a split device):

| Query | Strings, in order |
|---|---|
| `BusQueryDeviceID` | `USB\VID_vvvv&PID_pppp&MI_nn` |
| `BusQueryHardwareIDs` | `USB\VID_vvvv&PID_pppp&REV_rrrr&MI_nn`, `USB\VID_vvvv&PID_pppp&MI_nn` |
| `BusQueryCompatibleIDs` | `USB\Class_cc&SubClass_ss&Prot_pp`, `USB\Class_cc&SubClass_ss`, `USB\Class_cc`: for an IAD function from the IAD's `bFunctionClass` / `bFunctionSubClass` / `bFunctionProtocol`, as Microsoft's "Support for interface collections" gives them; for any other function, a legacy audio group included, from its first interface (alternate 0). Decided 2026-10-03 (Codex review of batch (c), round 19, finding 5); it was open (10.10) |
| `BusQueryInstanceID` | with the device's serial id, that id, `&` and `nn`, `UniqueID` TRUE: `ABC123&03`; without, the location key in decimal, then `nn`: port 3's `MI_03` is `303`, `UniqueID` FALSE (task 33.2; the location form was corrected 2026-10-03 by 26-A.7 from "the parent device's instance string plus the function number") |

**Instance ids from the serial number** (roadmap task 33.2, `2.1.0.0`;
`XhciFuncSerialId`, `XhciFuncInstanceId` in `xhci_func.c`, host vectors in
`test\test_func.c`; `HcdDeviceReadSerial` in `hcd_enum.c`; the duplicate
and dormant rules in `hcd_pdo.c`). As `usbhub` does (`legal-provenance.md`
section 4, the ReactOS row; Microsoft's binary was not read for it), a
device whose serial string is usable is named by it and answers `UniqueID`
TRUE, so it keeps its devnode on any port, behind any hub; every other
device keeps the location form and `UniqueID` FALSE.

- **Read** once per enumeration, on the controller thread before the
  device's first PDO is built, into the device record (no pool site of its
  own): string descriptor 0 for the first language id (`0409h` when it
  STALLs or lists none), then string `iSerialNumber`.
- **No serial** (`iSerialNumber` 0): no request; the location form.
- **Refused**: the string arrived but is not an instance id - malformed, empty,
  or a UTF-16 unit outside `0x21`-`0x7E`, or `,` or `\`. `usbhub` refuses
  below `0x20`, above `0x7F` and `,`; this rule also refuses the space, DEL
  and the backslash, the separator of a device instance path. The location
  form, at every plug, since it is the device's own answer. No other length
  limit: 126 characters is the descriptor's own, and the longest instance
  path, `USB\VID_vvvv&PID_pppp&MI_nn\` and 126 characters and `&nn`, is 157,
  under `MAX_DEVICE_ID_LEN` (200).
- **Read failed** is not "no serial": a try that does not bring the string
  (a STALL, an error, a request not sent) is repeated, three tries in all,
  and only then is the device given the location form, counted
  (`serial.readfailed`) and traced. A device whose read fails at one plug
  and not at the next changes devnode, and it takes three failed reads in
  a row. A **timed-out** read is not retried and gives no id at all: it
  requested the controller reset, the PDO is not created, and the device is
  enumerated afresh after the reset.
- **Duplicates**: a serial id that a present PDO of the same VID and PID
  already carries, on any of this driver's controllers (on another
  controller a dormant PDO counts too, and so does a gone one its root hub
  has not yet omitted from a relations answer, since that answer is not
  ordered with this root hub's) - compared ignoring
  case, since the registry key does not tell case apart - leaves the
  newcomer on the location form (`serial.duplicate`); an instance id with
  `UniqueID` TRUE names one devnode on the whole machine. `usbhub` checks
  only its own hub's ports. A PDO already unlisted (gone, its missing
  report pending) on the same controller does not count - one relations
  answer omits it and brings the newcomer - so a device moved quickly from
  one port to another keeps its id, as under `usbhub`; moved between
  controllers faster than the old root hub's next answer, it takes the
  location form for that plug. This is the other way a
  device's instance id can change between plugs: of two units sharing a
  serial, the one enumerated second takes the location form, and which one
  that is can differ from plug to plug.
- **Composite functions** keep their `MI_nn` in the device id and add `&nn`
  to the serial id; the location form keeps its `nn` suffix. Both forms
  stay unique: two functions of one device differ in `MI_nn`, and two
  devices behind hubs differ in route or serial.
- **Dormant PDOs** (Windows 98 SE and ME, task 33.1) are matched by the
  instance id they answer, before the duplicate check: first a group named
  by the device's place whose device read the same serial id, or none (so
  a unit a duplicate left on the location form revives its own PDOs, and
  a serial-named unit never takes another unit's location group; a device
  whose every read failed this time counts as unknown, not different, and
  a group whose device's reads all failed is revived by a device that
  answers the location form at that place, since the id is the same),
  then a group named by its serial id, wherever the device comes back
  (`XhciFuncReviveByPlace`, `XhciFuncReviveBySerial`, host vectors in
  `test_func`). A revived PDO answers the id
  it had. A dormant group the newcomer did not revive is retired in the
  hold that lists the new PDOs when it is named by the newcomer's place or
  carries the serial id the newcomer keeps; one named by a serial id is
  not retired for its old place, and goes at its START's wait if its
  device does not come back. Two identical units swapped while disabled
  are taken for each other, as before.
- **Upgrading** from `2.0.0.0` gives every device with a usable serial one
  new devnode, at its first plug, because its instance id changed once.

**A storage interface that offers UAS** (roadmap task 31-A.3, `xhci_xport.c`;
a device PDO's one interface, or a function's when no IAD groups it) gets
one transport, and both tables above change for it. The compatible ids are
the chosen setting's triple - `USB\Class_08&SubClass_06&Prot_62` and its two
shorter forms under UAS, alternate 0's `Prot_50` triple under Bulk-Only -
and none at all for an interface with no transport it can run. That
refused interface shows no VID/PID-derived hardware id either: its one
hardware id is the project-owned
`USB\XHCI98_NOXPORT&VID_vvvv&PID_pppp&REV_rrrr` (`&MI_nn` on a function),
which no INF names, so no `usbstor.inf` or vendor INF line binds a storage
driver to a device whose transport cannot run; it shows with no driver.
Where it sits decides the rest (`XhciXportRefusedAt`): on a
companion-paired root port the bus asks 29-A.5's hold to send it back to
USB 2.0 (`HcdHoldRequestUsb2`, Phase 29's executor: an accepted request is
queued for the thread's next pass, the device given no PDO and left
Present until the hold's PED write and disconnect take it as an unplug,
or, if the hold is refused then, refused in place with its PDOs created
there; a refused request leaves it refused in place), and on a root port
with no companion or behind a SuperSpeed hub it is refused in place; each place is
counted. Under UAS the VID/PID hardware ids
stay, so a device a `usbstor.inf` lists by hand still binds `usbstor.sys`
on it - roadmap 31-A.3's residual case, recorded rather than fought. The
compatible-id rule rests on what the targets' `usbstor.inf` files match on (read
2026-10-04, static, a text read of the INFs hashed in 10.6 and NUSB 3.3's
and 3.6's): full class triples only - never `USB\Class_08&SubClass_06` or
`USB\Class_08`, never `Prot_62` - and, listed by hand, `USB\VID_v&PID_p`
(16 lines on 2000 SP4, 61 on XP to 7, 135 and 152 under NUSB 3.3 and 3.6)
and `USB\VID_v&PID_p&MI_nn` (6 or 7, none on 2000); never a `&REV_` form,
never a vendor id alone. Bulk-Only counts only at alternate 0, because
`usbstor.sys` selects alternate 0; the UAS driver selects its own setting.
The device id is unchanged. Whether Windows 98's configuration manager
matches an INF line against the device id as well as the hardware ids is
unread; if it does, a hand-listed `VID&PID` still wins there, roadmap
31-A.3's residual case.

Microsoft's parent forms the `MI_` suffix with the format string `&MI_%02x`
(Windows 7 SP1 and XP SP3 `usbccgp.sys`): lower-case hex, which the
case-insensitive matching makes equivalent. The string dumps alone suggest it
takes the parent's hardware ids and appends that suffix (there is no VID/PID
template in `usbccgp.sys`); that is an inference from strings. Device text:
`iProduct` for a device PDO; `iInterface` (or an IAD's `iFunction`) for a
function PDO, falling back to the device's `iProduct`.

**Device text, as implemented** (roadmap task 33.6, 2026-10-04, `2.1.0.0`;
`XhciFuncTextIndexes` and `XhciFuncText` in `xhci_func.c`, host vectors in
`test\test_func.c`; `HcdDeviceReadText` in `hcd_enum.c`; the answer in
`hcd_pdo.c`). Until `2.1.0.0` every device and function PDO answered
`DeviceTextDescription` with a fixed `USB Device`, which Windows 98 SE's
Add New Hardware wizard showed for every device (owner report).

- **Which string**: a device PDO is named by `iProduct`. A function PDO
  tries its IAD's `iFunction`, then its first interface's `iInterface`
  (alternate 0), then the device's `iProduct`, each index once; the first
  that gives a string with something to show wins. None, and the PDO
  answers `USB Device` as before.
- **Read** on the controller thread when the PDOs are built, after 33.2's
  serial read, in the device's first language id from string descriptor
  0 (`0409h` when it STALLs, lists none, or is not read whole in two
  tries) - read once per enumeration and kept for the device's other PDOs. A STALL, or a string with nothing to
  show, is final; any other failure - an error, or a descriptor that did
  not arrive whole - is tried twice in all; an index that
  gave nothing is not asked again for the next function. A read that
  times out is the serial read's case: no PDO, the device left to the
  controller reset it requested. Nothing is counted; the debug trace
  names the port and index.
- **Made fit to show**: the string ends at its first NUL unit; C0 and C1
  controls and DEL become spaces, runs of spaces one, leading and trailing
  spaces go; a surrogate that is not half of a pair, U+FFFE and U+FFFF
  become `?`; a string with nothing but `?` and spaces is refused, so the
  next index is tried. 126 characters, the descriptor's own limit, fit
  whole.
- **Windows 98 and ME fold to ASCII**: every character above U+007E, a
  surrogate pair included, becomes `?` there (decided at run time, as for
  the retire rule, by `IoIsWdmVersionAvailable(1, 0x10)` answering FALSE).
  The configuration manager keeps the description as an ANSI string made
  from the bus driver's Unicode answer, and how `ntkern` converts it - the
  system code page, or a narrowing that would turn U+0100 into a NUL and
  others into control or DBCS lead bytes - has not been read here; ASCII
  passes either unchanged. The cost is an accented or non-Latin product
  name shown with `?` on those systems. NT keeps the string as the device
  sent it.
- **Kept** in the PDO's extension (127 WCHARs, 254 bytes a PDO; no pool
  site), fixed at creation: a dormant PDO revived on Windows 98 SE or ME
  (task 33.1) answers the text it had, and the PDOs built for the
  re-enumeration that revived it are deleted with theirs.
- `DeviceTextLocationInformation` is unchanged: not answered, the IRP's
  status passed through. This record has no reading of what `usbhub`
  answers for it.

**The bus never emits `USB\COMPOSITE`.** In Microsoft's stacks the composite
device's own PDO carries it as a compatible id, and that is what brings the
parent driver: `usbhub.sys` on 98 SE and 2000, `usbccgp.sys` from XP on and on
98 SE under NUSB 3.6 or SweetLow. Under the HCD the bus is the composite
parent, so the id would bring one of those back on top of an already-split
device, against the "no INF binding path for a composite parent" decision.
Also never emitted: `USB\ROOT_HUB` and `USB\ROOT_HUB20` (the root hub is
`XHCI98\ROOT_HUB`, section 8.6, and the INF gate's `HCD-ROOTHUB` refuses
them), and `USB\CLASS_09*`, because a hub is never a PDO. `USB\MS_COMP_*` is
out of scope for 2.0.0.0.

### 10.8 When and how the bus splits a device

**When.** Microsoft's composite-parent rule, as `XhciFuncSplit`
(`xhci_func.c`) implements it: split only a device with
`bNumConfigurations` 1, two or more interfaces (counted over
`bAlternateSetting` 0 descriptors), and `bDeviceClass` 0 or
class/subclass/protocol `EF/02/01` (the IAD device class). Every other
device - several configurations, or a device class of its own, IADs or not -
is one device PDO with the whole-device ids of 10.7, which keeps the
`USB\VID_vvvv&PID_pppp` id a whole-device driver matched on; so is a device
with more than 16 functions or IADs, or an interface number of 32 or more. A
multi-interface device left whole no longer reports its first interface's
class triple (10.7).

Superseded on 2026-10-03 (owner and coordinator, after Codex review of batch
(c), round 19, finding 3): this paragraph read "Split when the active
configuration has more than one interface ... **and** `bDeviceClass` is 0,
or is `0xEF` with subclass 2 and protocol 1 ..., or the configuration carries
IADs", left any other device class unsplit "until it is read", and had the
bus split using "the first configuration", leaving the multi-configuration
rule (`ParentFindOriginalConfiguration`, `ParentFindAltConfiguration`)
unread. A device with more than one configuration is now never split.

The bus configures a split device itself, once, before any function PDO
exists (`HcdCfgParentConfigure`): SET_CONFIGURATION with its one
configuration, every interface at alternate 0, no endpoint open;
`RESET_PORT` replays it. A function's `SELECT_CONFIGURATION` never sends
SET_CONFIGURATION (10.9). A device the bus does not split is left Addressed
for its function driver to configure.

**Grouping**, in this order, over the configuration's interfaces (alternate 0
entries only); the functions are listed in the order of their first
interfaces' descriptors:

1. **IAD.** An Interface Association Descriptor (type `0x0B`, `usb200.h`
   101-114) groups `bInterfaceCount` interfaces from `bFirstInterface` into
   one function; an IAD overlapping an earlier one is ignored. Its compatible
   ids come from the IAD, and its device text from `iFunction` when set
   (10.7). The Sound Blaster X4 (`041E:3278`) is the specimen: a CDC
   function (`02/02` with `0A/00`) and the UAC 2.0 audio function, each
   IAD-grouped, and an HID interface on its own (`test-equipment.md`).
2. **The legacy rule**, only for a configuration that carries **no IAD at
   all**, read statically from Microsoft's own parent, `usbccgp.sys`
   5.1.2600.5585 and 6.1.7601.17514 (`GetFunctionInterfaceListBase`); both
   builds do the same thing. Any IAD turns the audio rule off for every
   interface, per Microsoft's grouping hierarchy ("Support for interface
   collections"), and an interface no IAD covers is then a function on its
   own (superseded on 2026-10-03, Codex review of batch (c), round 19,
   finding 6: this rule read "for interfaces no IAD covers"):
   - **Consecutive class-`0x01` interfaces whose subclass differs from the
     first form one function.** An interface of class `0x01` (Audio) starts a
     function; each following class-`0x01` interface whose
     `bInterfaceSubClass` differs from the starting interface's joins it; the
     function ends at the first interface of another class, or of class
     `0x01` with the starting interface's subclass (a second AudioControl
     starts a second function). So AudioControl `01/01` followed by
     AudioStreaming `01/02` and MIDIStreaming `01/03` is one function, named
     by the AudioControl's interface number (`&MI_%02x` of it).
   - The rule is positional: it does not read the AudioControl header's
     `baInterfaceNr` list.
   - Every other interface is a function on its own, counted at
     `bAlternateSetting` 0 only.
   - An interface of class `0x0D` (Content Security) that no IAD covers is
     skipped and belongs to no function, IADs or none in the configuration
     (`xhci_func.c`).

   The INFs agree: on 98 SE and 2000 the same vendor devices bind audio at
   `MI_00` and HID at `MI_02` (`MI_03` on NEC `0409:0203`) - `wdma_usb.inf`
   against `hiddev.inf` / `input.inf` - so interfaces 0 and 1 form one audio
   function. Every UAC 1.0 unit in `test-equipment.md` (AudioControl `01/01`,
   two AudioStreaming `01/02`, an HID `03/00`, no IAD) therefore splits into
   **two** function PDOs: if the interfaces are numbered in that order,
   `MI_00` (class `01/01/00`, matched by `wdma_usb.inf`'s `USB\CLASS_01`) and
   `MI_03` (class `03/00/00`, matched by `USB\Class_03`). `test-equipment.md`
   lists the four interfaces but not their numbers or order; an HID placed
   between the AudioControl and its streaming interfaces would, under this
   positional rule, split the audio function in two. The Low-Speed Wired
   Keyboard 600 (`045E:0750`, two HID interfaces, no IAD) splits into `MI_00`
   and `MI_01`. Guest leg c15 (2026-10-03, Windows 98 SE and 2000) split the
   C-Media `0D8C:0014` into an audio and an HID function, with no bugcheck.
3. **No CDC rule in 2.0.0.0.** Windows 7's `usbccgp.sys` has a further
   class-callback path - `GroupInterfacesByFunction` looking for an interface
   of class `0x02` subclass `0x08` (the Wireless Handset Control Model), then
   `GroupSubordinateInterfaces`, which calls `GroupSubordinateInterfacesByUFD`
   (grouping by the CDC Union Functional Descriptor) and
   `CheckForWmCdcModemFunction`, with `GetCdcFlags` - and XP SP3's build has no
   `Group*` routine at all. Under what condition Windows 7 takes that path is
   unread. Since no stock INF on any target binds a CDC class id, a CDC device
   with IADs is grouped by rule 1, and one without is split per interface as
   XP's parent does. Recorded so that a later CDC need starts from the
   reading, not from memory.

### 10.9 URB routing from a function PDO to the parent's pipes

The device - its slot, EP0, its endpoint contexts and its configuration -
belongs to one bus-internal device record; each function PDO holds a pointer
to it and the list of interface numbers it owns. Microsoft's parent has the
same shape going by its public symbol names alone
(`DispatchPdoUrbSelectConfiguration`, `DispatchPdoUrbGetDescriptorFromDevice`,
`BuildFunctionConfigurationDescriptor`, `ParentSelectConfiguration`,
`SendSelectInterfaceUrb`); the behaviour below is the HCD's own design.

| From a function PDO | What the bus does |
|---|---|
| GET_DESCRIPTOR(Device) | the real device descriptor |
| GET_DESCRIPTOR(Configuration) | a **synthesised** configuration descriptor: the real header with `wTotalLength` and `bNumInterfaces` recomputed, then only this function's interface, class-specific, endpoint and IAD descriptors, **with interface numbers unchanged** (a UAC 1.0 AudioControl header names its streaming interfaces by their real numbers). Descriptors ahead of the first interface or IAD stay out; the copy is bounded by the request's buffer, the MDL's byte count (`XhciFuncConfig`). Required, not cosmetic: Windows 2000 SP4's `usbaudio.sys` sizes its interface list by `bNumInterfaces` and dereferences an uninitialised entry for each non-audio interface, STOP 0x1E in guest leg c14 (`runs/run-26.md`, c14; `legal-provenance.md` section 4) |
| `URB_FUNCTION_SELECT_CONFIGURATION`, non-NULL | the device is already configured (10.8). Validate every `USBD_INTERFACE_INFORMATION` - its `Length`, and that its interface is this function's, else `STATUS_INVALID_PARAMETER` (Microsoft's parent carries the debug text "Pdo %x SET_CONFIGURATION Invalid Interface Information Length = %x. minimum size required = %x"). Then this function's pipes close and one Configure Endpoint drops its old endpoints and adds its new ones beside the siblings' (Context Entries = the highest DCI any function uses); only its own pipes are reopened. Per interface: SET_INTERFACE if the requested alternate differs from the current one, or if its endpoints have been opened since the device last restarted their toggles (`IfaceUsed`: re-added endpoint contexts start at DATA0, so the device's toggles must restart too); an interface still at the alternate 0 the bus's SET_CONFIGURATION left, never opened since, gets none. A SET_INTERFACE to alternate 0 STALLed by an interface with no other alternates falls back to CLEAR_FEATURE(ENDPOINT_HALT) on its non-isochronous endpoints; any other failure fails the request (round 19, finding 2). Fill pipe handles, types, packet sizes, intervals and `InterfaceHandle`; return a per-function `ConfigurationHandle`. No SET_CONFIGURATION reaches the device |
| `URB_FUNCTION_SELECT_CONFIGURATION`, NULL descriptor | unconfigure **this function only**: abort and drop its endpoints (Configure Endpoint with Drop flags); the device stays configured for its siblings |
| `URB_FUNCTION_SELECT_INTERFACE` | the interface must be this function's; SET_INTERFACE; drop the old alternate's endpoints and add the new one's in one Configure Endpoint |
| bulk, interrupt, isochronous, `ABORT_PIPE`, `SYNC_RESET_PIPE_AND_CLEAR_STALL` and the reset / clear pair | the pipe handle must be one this function's select returned (a sibling's is refused); then the device's endpoint, with 10.4's CLEAR_TT_BUFFER on a control or bulk reset behind a TT |
| a control transfer on the default pipe | the device's shared EP0, serialised across siblings. A standard or class request with an interface recipient may not name a sibling's interface in `wIndex`'s low byte; one naming no interface of the device passes to the device (`XhciFuncSetupAllowed`; corrected 2026-10-03 from "must name one of this function's interfaces", which refused `usbaudio.sys`'s class requests to `0x54` and `0x60` in guest leg c15); SET_CONFIGURATION and SET_ADDRESS in a raw control transfer are refused (the bus owns both) |
| `GET_CONFIGURATION`, `GET_INTERFACE`, `GET_CURRENT_FRAME_NUMBER`, `GET_STATUS` | answered for the device; the frame number is the controller's |
| `IOCTL_INTERNAL_USB_RESET_PORT` | resets the whole device: port reset, re-address, SET_CONFIGURATION, every function's current alternates and endpoint contexts restored, **every sibling's pipe handles kept valid**; siblings' in-flight transfers complete as cancelled. Which targets' class drivers send it is section 6.5's table |
| `IOCTL_INTERNAL_USB_CYCLE_PORT` | the whole device leaves and re-enumerates: every function PDO reported missing and recreated. The siblings share a group serial and leave together; the port re-enumerates only once the last of the group is deleted |
| `IRP_MN_REMOVE_DEVICE` on a function PDO | only this function's requests are cancelled (EP0's included); then the thread closes its pipes, drops its endpoints and returns its interfaces to alternate 0 (`HcdCfgReleaseFunction`), so a removed audio function holds no periodic bandwidth; the siblings keep theirs (round 19, finding 4). The device record stays until the slot goes (5.2) |
| `IOCTL_INTERNAL_USB_GET_PORT_STATUS`, `QUERY_INTERFACE` (`USB_BUS_INTERFACE_USBDI`) | the device's answers, identical for every function (section 6; 26-A.6) |
| power | a function's D-state is its own; the device stays D0 while any function is D0 (selective suspend is outside the roadmap, 28.3) |

Section 9.5's harness definition follows from this table: `endpoints opened`
counts endpoints added at a function's `SELECT_CONFIGURATION` or
`SELECT_INTERFACE`, and nothing the bus opens for itself.

### 10.10 What 25.6 leaves open

| Open item | Binds |
|---|---|
| Every **(to transcribe)** USB 2.0 number above: 4.1.1 tiers; 7.1.7.3 `TATTDB`; 7.1.7.5 `TDRST`, `TRSTRCY`; 7.1.7.7 `TRSMRCY`; 9.2.6.3 `TDSETADDR`; 11.12.4 the bitmap; 11.23.1-2 the hub descriptor fields and the status endpoint's `bInterval`; 11.24.2 the TT requests' `wValue`; Tables 11-13, 11-16, 11-17, 11-21 and 11-22. The specification is added to `docs/references/` with its hash first. | 27-A.1 (one transcription batch) |
| `BusQueryCompatibleIDs` for a device-class device (the `DevClass` forms, read from the hub drivers' id order statically), and the instance-id character set on Windows 98. Since task 33.2 a serial id brings any of `0x21`-`0x7E` but `,` and `\` into a Windows 98 instance id (10.7); its guest legs on Windows 98 SE and ME are where that set is first observed. | 26-A.4; 33.2 |
| **Closed 2026-10-03 by decision, not by a static read** (owner and coordinator, Codex review of batch (c), round 19, findings 3 and 5): the IAD function's compatible ids come from the IAD (10.7), and the split follows Microsoft's composite-parent rule - one configuration, two or more interfaces, device class 0 or `EF/02/01` - so no other device class and no multi-configuration device is split (10.8). It read: the IAD function's compatible ids (from the IAD or the first interface: `ParseUSBInterfaceAssociationDescriptors` / the id builders), the device-class values Microsoft splits besides 0, and the multi-configuration rule - static reads of `usbccgp.sys` and the hub drivers. The X4's IAD fields have still not been read. | 26-A.7 |
| The interface numbering and order of each UAC 1.0 unit in `test-equipment.md`, read off the units' descriptors. The bus logs each function's port and `MI_`, interface mask and class triple as it creates the PDOs (`HcdDevicePdoCreate`) for that reading. | 26-A.7 |
| Windows 2000's and stock Windows 98's own composite parent (`usbhub.sys`) grouping rule is unread. The INF evidence (audio at `MI_00`, HID at `MI_02`) agrees with the `usbccgp` rule, and since the bus does the splitting, what matters is what those targets' audio drivers accept. | 26-V.1, 26-V.2 |
| Whether `IoInvalidateDeviceRelations` may be called at `DISPATCH_LEVEL` on Windows 98 (section 7.7), which decides whether 10.1's state machine hands that one call to the PASSIVE worker. | 27-A.1 |
| Under what condition Windows 7's `usbccgp.sys` takes its CDC grouping path. | none in 2.0.0.0; recorded for a later CDC need |

### 10.11 External hubs as devnodes (task 33.4, 2026-10-04)

**The decision.** The owner, 2026-10-04: external hubs appear in Device
Manager as devnodes from release `2.1.0.0`, with the devices behind each
nested beneath it, as on every Microsoft stack. This supersedes "the hub
itself is never a PDO" wherever this record says it (5.2's object table and
the paragraph after it, 5.3's `Hub` row, 10's introduction, 10.3's
bring-up, 10.7's last paragraph) and closes 8.10's row "External hubs inside
the bus against `DeviceIsHub`". **Only the PnP presentation changes**: the
bus still runs every hub - its class requests, status-change pipe, TT
assignment, teardown and the SuperSpeed half of Phase 30 are untouched -
and no hub-class driver of any target binds anything. Written before the
code, on branch `p33-hubs`.

**Object model.** A hub's PDO is an `HCD_DEVICE_PDO` with `Hub` set, created
by the controller thread once `HcdHubStart` has brought the hub up (a hub
refused as too deep, 10.3, gets one too, with no ports to answer for). It is
bound to `xhci98.sys` by the INF (below), and `AddDevice`, decided as before
by the PDO's driver object, attaches a **hub FDO** (`HCD_HUB_FDO`, a fifth
kind) over it - the root hub's pattern again. Every PDO carries
`ParentSerial`: 0 for a child of the root hub, otherwise the `Serial` of the
hub PDO it is presented under - the nearest hub above it **that has a PDO**
(a hub whose PDO could not be created presents its devices under the next
one up, ultimately the root hub; pure rule `XhciHubPresentedParent`, host
vectors in `test_hub`). A serial rather than a pointer, so no PDO ever
dangles on its parent's deletion. The PDO lists, the lock (`PdoListLock`),
the single-writer thread and every lifecycle state of 5.2 and `hcd_pdo.c`
(listed, gone, removed, deleted, dormant, orphaned) are reused unchanged for
hub PDOs; only which relations answer carries a PDO is new.

**One devnode per half: a USB 3 hub shows as two.** The bus already models a
USB 3 hub as two hubs - the SuperSpeed half on a SuperSpeed port and the USB
2.0 half on the companion, each its own device record, slot, hub object and
ports (30-A.1) - and the devices behind each half are on that half's ports.
Two devnodes is that truth, and it is what Microsoft's own stack shows (a
"Generic SuperSpeed USB Hub" and a "Generic USB Hub" for one box); one
devnode would have to merge two hub objects whose ports, removals and node
IOCTLs are independent, for a cosmetic gain. Decided: two.

**Ids** (project-owned; `XhciHubPdoId`, `xhci_hub.c`, host vectors in
`test_hub`):

| Query | USB 2.0 / 1.1 hub, and a USB 3 hub's USB 2.0 half | A USB 3 hub's SuperSpeed half |
|---|---|---|
| `BusQueryDeviceID` | `XHCI98\HUB&VID_vvvv&PID_pppp` | `XHCI98\HUB30&VID_vvvv&PID_pppp` |
| `BusQueryHardwareIDs` | `XHCI98\HUB&VID_vvvv&PID_pppp&REV_rrrr`, `XHCI98\HUB&VID_vvvv&PID_pppp`, `XHCI98\HUB` | the same with `HUB30` |
| `BusQueryCompatibleIDs` | none | none |
| `BusQueryInstanceID` | any PDO's rule (`hcdInstanceQueryId`, task 33.2): the hub's serial id when it has a usable one, else the instance key in decimal | the same |
| Device text | `xHCI98 USB Hub` | `xHCI98 USB 3.x Hub` |

Never `USB\Class_09...`, `USB\HubClass`, `USB\USB20_HUB` / `USB30_HUB` or
`USB\VID_...&PID_...`. The hub-class matches every target's INFs carry were
swept again for this decision (2026-10-04, static, a text read of the INFs
section 10.6 hashes plus NUSB 3.3's and 3.6's `USB2.INF` and SweetLow's):
98 SE and ME `usb.inf` match `USB\CLASS_09`, `USB\CLASS_09&SUBCLASS_01`,
`USB\ROOT_HUB` and `USB\ROOT_HUB_DBC`; 2000 SP4 adds `USB\HUBCLASS` and
`USB\ROOT_HUB20`; XP, XP x64, Vista and 7 `usb.inf` the two `CLASS_09`
forms; NUSB's and SweetLow's `USB2.INF` `USB\HUBCLASS` and
`USB\ROOT_HUB20`; and XP's and later `usb.inf` also list vendor hubs by
`USB\VID_&PID_`, which is why no VID/PID id under the `USB\` enumerator is
emitted. None of the files names an `XHCI98\` id. The hardware-id form,
not a compatible id, is what the INF binds, so the match ranks as a
hardware-id match on every engine. The instance key keeps the location rule
of 10.7 as task 33.2 left it, for a hub as for any device: the serial id
with `UniqueID` TRUE when the hub has a usable serial number, else the
place with `UniqueID` FALSE (merged from `p33-serial`, 2026-10-04). On the
NT targets a PDO with `UniqueID` FALSE has its instance id prefixed with
its parent's `ParentIdPrefix`: **a serial-less device that was behind a hub
under `2.0.0.0` is a new devnode once under `2.1.0.0`**, re-installed
silently from the same class INF; Windows 98's configuration manager uses
the instance id as given (unread; the guest legs below record what it
does). Because a serial id names one devnode wherever its parent is, and
two parents' relations answers are not one answer, a serial id is not
reused while PnP may still see it present under **another** parent: a gone
PDO there not yet reported missing, or a dormant one there that this
device cannot revive, holds it, and the newcomer takes the place form for
that plug (`hcdSerialTakenLocked`; Codex review of 33.4, final round,
finding 1). Under the same parent one answer omits the old and carries
the new, as 33.2's move between root ports relies on.

**Capabilities.** `Removable` TRUE, `SurpriseRemovalOK` TRUE, `UniqueID`
TRUE exactly when the instance id is a serial id (as any PDO's), `Address` and `UINumber` the instance key. `SurpriseRemovalOK` TRUE so
the XP-onward hot-plug applet does not offer the hub itself for safe removal
while a storage device behind it keeps its own entry; the applet's rule was
not read (an open item, read on the XP guest).

**Relations.** The root hub FDO's `BusRelations` carry the listed PDOs whose
`ParentSerial` is 0; a hub FDO's carry those whose `ParentSerial` is its hub
PDO's `Serial` (`HcdDevicePdoRelations(hc, old, parent)`). The marking rules
of 5.2 apply per answer: a listed PDO carried is `Reported`; a gone PDO whose
parent is this answer's is omitted and so `MissingReported`. One rule is
added, because a hub that leaves takes its whole subtree from PnP's view at
once and its FDO answers nothing more: **a gone PDO is missing as soon as
any hub PDO above it is** - reported missing, deleted, or removed by PnP
(PnP removes a devnode's children before the devnode, so a removed parent
means each child has had its own REMOVE). Every relations answer applies
that rule to every gone PDO, whichever parent it answers for, so the ports
behind a departed hub stop waiting and a gone PDO whose REMOVE has come
reaches `RemovedPdos` and its deletion at the next answer, as any other.
The rule is applied at the departure too (`HcdDevicePdoGone`), so a port
never waits for an answer no FDO can give (Codex review of 33.4, round 1).

**Invalidation.** `IoInvalidateDeviceRelations` goes to the PDO a change
belongs under: the root-hub PDO for `ParentSerial` 0, otherwise the hub PDO,
**only when that PDO has been started by PnP** (`START` seen, no `STOP` or
`REMOVE` since). A hub PDO PnP has not started may have no devnode yet, and
invalidating it would be a fatal PnP error on NT; it needs no invalidation,
since PnP asks a hub FDO for its relations after it starts. When the parent
is not started the root hub is invalidated instead, which runs the
ancestor rule above. The hub PDO is referenced across the call.

**Ordering and lifetime.**

1. The hub PDO is created and listed after the hub's own bring-up
   (`HcdHubStart`), and the root hub (or its own parent hub) invalidated.
   Its port's machine is told it exists and has started at once, as before:
   the bus does not wait for PnP to serve a hub.
2. Devices behind it are created whenever their enumeration finishes,
   listed under the hub PDO's serial, and reported only when PnP asks the
   hub FDO - after that FDO has started.
3. A hub unplugged: section 10.5's teardown already reports the leaf PDOs
   first and the hub's own last, so children are gone before their parent.
   The hub PDO is reported missing to its parent; NT then surprise-removes
   the whole subtree and removes it leaf first, Windows 98 removes it. Each
   port below waits for its own group until the ancestor rule marks it, and
   the hub's upstream port waits for the hub PDO and for the hub object
   (`AwaitHub`), as before.
4. A hub disabled in Device Manager: presentation only. PnP removes the hub
   FDO and its children's stacks; the PDOs stay listed (the WDM rule); the
   bus keeps the hub and the devices behind it running with no client.
   Enabling it re-adds the FDO and re-reports them.
5. The controller's stop and remove, and the root hub's: unchanged.
   `HcdDevicePdoReleaseAll` settles hub PDOs and their children with the
   rest. A hub FDO reaches the controller through its PDO's `Controller`,
   inside the PDO's `Busy` count, which the release waits out before it
   orphans the PDO - the device PDOs' own guard, reused.
6. **Windows 98 SE and ME, the dormant path of task 33.1.** A hub PDO
   stopped by PnP across an orderly controller stop is kept dormant like any
   device PDO, and its children with it - but a child is kept only when its
   hub PDO is (`HcdDevicePdoDormantAll` now takes the devices tier by tier,
   root ports first). Behind a hub, a PDO the user disabled (removed by PnP
   while present, so still listed) is kept with its group and revived still
   disabled: dropped, it would be gone and waiting for an answer only its
   dormant hub's FDO could give, and its port's wait would keep that hub
   from being enumerated again; nor may it be taken for absent, since PnP
   still holds it present (Codex review of 33.4, rounds 1 and 2). A
   composite with one function disabled is kept whole the same way. The
   residue: a disabled device unplugged while the controller is off stays
   listed, disabled, until a device enumerates at its place or its hub's
   dormant group is retired. At the restart the hub re-enumerates first and revives
   its dormant PDO by 33.2's serial-or-place match, keeping its `Serial`,
   so the
   children enumerated behind it compute the same `ParentSerial` and revive
   theirs; `ParentSerial`, `Hub` and `HubUsb3` join the descriptors in the
   sameness test, so no group is revived under another parent. A dormant group retired (not revived) takes the dormant groups
   below it with it.

**The hub FDO** (`hcd_hubfdo.c`): `AddDevice` for a hub PDO; PnP passed down
like the root hub FDO's, with `BusRelations` answered as above and
`START`'s success making the door and opening its IOCTLs, which a `STOP`,
`SURPRISE_REMOVAL` or `REMOVE` closes before going down, so no open handle
keeps the PDO's `Busy` raised while its quiesce waits (round 1, finding 2);
a `REMOVE` that only tears down the
door and itself - it never detaches the bus. Power is passed down; the hub
PDO answers it as a device PDO does. A hub PDO answers no internal IOCTL
(the bus owns the hub; nothing above the hub FDO sends URBs).

**The door on a hub FDO** (section 8's table, per hub): at its start
`\DosDevices\XHCI98HUB<serial>` on the hub PDO's own name
(`\Device\XHCI98DEV<serial>`), `SymbolicName` on the hub devnode's hardware
key (the 98-to-XP x64 pages), and an enabled `GUID_DEVINTERFACE_USB_HUB` (the
Vista and 7 pages). Its `DEVICE_CONTROL` answers 8.3's set for the hub's own
ports: `GET_NODE_INFORMATION` from the hub descriptor the bus read
(`bNbrPorts`, `wHubCharacteristics`, `bPwrOn2PwrGood`, `bHubContrCurrent`;
`HubIsBusPowered` from the configuration's `bmAttributes` self-powered bit,
since the bus does not keep the hub's GET_STATUS - a SuperSpeed half's
descriptor in the same 0x29 shape, the only one the structure has), the
connection information and its `_EX` form, the descriptors, the connection's
driver key, attributes, and `GET_HUB_CAPABILITIES(_EX)` from the hub's speed
and TT (`HubIs2xCapable`, High Speed, multi-TT capable and on, never root);
`RESET_HUB` refused as on the root hub. A connection whose device is a hub
with a PDO now answers `DeviceIsHub` TRUE - on the root hub's ports and a
hub's alike - and `GET_NODE_CONNECTION_NAME` returns that hub's
`XHCI98HUB<serial>`, which `usbui.dll` opens as `\\.\` plus the name (8.3,
`GetExternalHubName`); a connection whose hub has no PDO answers as before.
**Unread**: whether Windows 98's `sysclass.dll` `USBHubPropPage` or any
`usbui.dll` sends a non-root hub anything 8.3 does not list, and whether the
Vista and 7 hub Advanced tab offers `RESET_HUB` on an external hub (refused
either way); the guest legs read the tabs.

**The INFs.** Each models section of both files carries two more models,
`%HubDesc%` under `XHCI98\HUB` and `%Hub30Desc%` under `XHCI98\HUB30`,
installed by `Hub.Dev` (98/ME: `DevLoader=*NTKERN`, `NTMPDriver=xhci98.sys`,
the hub page), `Hub.Dev.NTx86` / `Hub.Dev.NTamd64` and `Hub.Dev6.*` (the
service, the hub page). **A hub section copies nothing**: the devnode exists
only under a running `xhci98.sys`, so the binary is on disk and loaded, and
a hub plugged in months after the install must not send the setup engine
looking for the original media (Windows 98's engine and NT 5.x's both look
for a copy's source and ask for it when it is gone). The INF gate holds that
(`HCD-HUBCOPY`, over the install section and every `<install>.*` section
the engine runs beside it, with no `Include` or `Needs`; round 1, finding
3), keeps both hub models in every models section and every hub
id under `XHCI98\` (`HCD-HUB`), refuses a Microsoft hub-class id on any line,
and holds the hub sections to the hub page (`HCD-HUBPAGE`).

**What 33.4 leaves open**: the hot-plug applet's listing rule; Windows 98's
instance handling of a device re-parented under a hub; the 98 and ME
"found new hardware" behaviour for the new devnode (read on the guests);
what each page sends a non-root hub. The guest legs of 33.4 read them.

## 11. The transfer-buffer policy (task 25.7)

The roadmap put the question as direct DMA from the URB's MDL against bounce
buffers, to be decided on what section 7 found Windows 98 exports and on
design record 04's arithmetic for a common buffer the HCD now allocates
itself. Both inputs are in hand, and they decide it.

### 11.1 The adapter: `IoGetDmaAdapter` and its table, nothing else

The HCD gets one DMA adapter per controller, at `IRP_MN_START_DEVICE`, from
`IoGetDmaAdapter` on the PCI PDO, with a `DEVICE_DESCRIPTION` of `Master`,
`ScatterGather` and `Dma32BitAddresses` set and `InterfaceType` `PCIBus` -
the description Windows 98 SE's own `openhci.sys` and `uhcd.sys` pass
(section 7.1 has their call sites) - and calls every DMA service through the
adapter's `DMA_OPERATIONS` table: `AllocateCommonBuffer`,
`FreeCommonBuffer`, `AllocateAdapterChannel`, `MapTransfer`,
`FlushAdapterBuffers`, `FreeMapRegisters`, `PutDmaAdapter`. That costs one
import, `ntoskrnl.exe!IoGetDmaAdapter`, which has seven stock Windows 98 SE
precedents. The legacy pairing - `HalGetAdapter` with
`HalAllocateCommonBuffer`, or the `IoMapTransfer` family - has no Windows 98
binary precedent at all (section 7), and the two routes are never mixed on
one adapter (`docs/usb-xhci-info/win98-wdm.md`, "DMA Memory Allocation").
Under the Windows 2000 DDK's `ntddk.h`, writing `HalAllocateCommonBuffer` by
name produces a HAL import rather than a table call, so the source names the
table slots and the import gate is what proves it did.

`GetScatterGatherList` and `PutScatterGatherList` (table slots `+0x2C` and
`+0x30`) are not used: neither Windows 98 SE HCD calls them, and whether
Windows 98's adapter fills those slots was not read.

The adapter is 32-bit on both architectures. `Dma32BitAddresses` and never
`Dma64BitAddresses`, so every address the table hands back is below 4 GB and
every hardware address field keeps its high half at zero, as `AGENTS.md`
requires; on an amd64 machine with memory above 4 GB the map registers are
real and the HAL places the data below the line, which is what 11.3 relies
on.

### 11.2 The common buffer: design record 04's block, allocated by the HCD

Design record 04 sized one worst-case block because usbport allocated it at a
size committed in `DriverEntry`, before any register could be read. The HCD
allocates its own and could size it from `HCSPARAMS1`/`HCSPARAMS2`. For
Phases 26 to 28 it does not: it allocates **design record 04's layout,
unchanged - 409,600 bytes, one `AllocateCommonBuffer` call with
`CacheEnabled` = TRUE**, carved by the same `xhci_mem.c` the miniport carves
with, under the same policy limits (32 enabled slots, 64 scratchpad pages, 32
pooled transfer rings with a per-device cap of 4) and the same refusal above
them. Three reasons, in order of weight:

1. **The size is already observed granted, through the same route, on every
   target.** usbport's allocation of the miniport's common buffer is an
   `AllocateCommonBuffer` through the adapter `IoGetDmaAdapter` returned -
   NUSB's `USBPORT.SYS` imports `IoGetDmaAdapter` (section 7) - and design
   record 04's open item 1 records every start since batch 7a-V, on both
   primaries and every VM target, as an observation that 400 KiB was granted.
   A new size would be a new first observation on each of ten targets.
2. **The carve and its host suite are shared.** `test\test_membuf.c` checks
   the Table 6-1 alignment and boundary rules across every region at every
   context size; a runtime-sized layout would need those vectors rebuilt
   before it could be trusted, which is work Phase 26's parity goal does not
   need.
3. **Cacheability and ordering stay as design record 04 section 6 argued**:
   cached common buffer, coherent PCI DMA on both architectures, ordering from
   `volatile` accesses and publishing each Cycle Bit last.

What the HCD's ownership does change is where the block is allocated and
freed: at `IRP_MN_START_DEVICE` after the resources are known, at PASSIVE
level as `AllocateCommonBuffer` requires, and freed with `FreeCommonBuffer`
at stop and remove before `PutDmaAdapter`. A failed allocation fails the
start with `STATUS_INSUFFICIENT_RESOURCES`, as usbport's did.

The limits are revisited where they bind and not before. The slot limit (32)
first binds with enough hubs and devices behind them, Phase 27; the
pooled-ring limit first binds on a composite device with many endpoints or
with SuperSpeed streams, Phase 31, where Stream Context Arrays need common
buffer of their own. Either may then move to a second allocation per
controller rather than a larger first one, since the HCD can now allocate
after reading the registers.

### 11.3 Transfer buffers: the map-register route, direct in practice

Every transfer buffer reaches the controller through the adapter's map
registers: for a URB, `AllocateAdapterChannel` with the number of map
registers the transfer needs, `MapTransfer` once per physically contiguous
run in the execution routine - each run becoming one or more TRBs, split
where the xHCI 64 KB boundary rule requires (`xhci-data-structures.md`, the
TRB data-buffer rule) - and at completion `FlushAdapterBuffers`, then
`FreeMapRegisters`. Windows 98 SE's `openhci.sys` and `uhcd.sys` both do
exactly this (section 7.1), and none of it is an import.

That answers the roadmap's question without choosing between its two
options. On x86 with `Dma32BitAddresses` and the machine's memory below 4 GB -
every Windows 98 SE machine, and every 32-bit NT target without PAE above
4 GB - the HAL's map registers are not real and `MapTransfer` returns the
buffer's own physical address: the controller DMAs **directly from the URB's
pages**, with no copy. Where map registers are real (amd64 with memory above
4 GB), the HAL bounces inside `MapTransfer` and `FlushAdapterBuffers`. So the
HCD itself keeps **no bounce buffers**: bouncing, where it is needed, is the
HAL's, through the one route that both primaries' own HCDs use.

The route that was not taken: building the TRBs from `MmGetPhysicalAddress`
on each page of the MDL. It would be direct DMA too, and on the 32-bit
targets it would give the same addresses, but `MmGetPhysicalAddress` has no
Windows 98 binary precedent in any of the 86 binaries scanned - not in
Windows 98 SE's own stack and not in NUSB's `USBPORT.SYS` - and it would
bypass the HAL's bouncing where that is needed. It stays off the allowlist.

The MDL itself: a URB carries either `TransferBufferMDL` or only a virtual
`TransferBuffer`. For the second, the HCD builds an MDL with `IoAllocateMdl`
and `MmBuildMdlForNonPagedPool` - the USB driver interface's contract is that
the buffer is non-paged - and frees it with `IoFreeMdl` at completion; on
stock Windows 98 SE that job was `usbd.sys`'s, which imports all three
(section 7), and the monolithic HCD inherits it. `MmProbeAndLockPages` is not
needed for a function driver's URB and is not on the core set.

A transfer longer than the adapter's map-register grant is split into
chunks the grant can map, each chunk a TD of its own, completed as one URB.
How many map registers Windows 98's adapter grants
(`NumberOfMapRegisters`) was not read (section 7.7); 26-A.5 records the value
on both primaries at the first start and sizes the chunk from it, and the
host suite of 26-A.9 carries the split over a synthetic grant.

Isochronous transfers take the same route per packet run; the per-frame
packet layout of a USB 2.0 isochronous TD is unchanged from the miniport's
(design record 04 section 3.6 and the isochronous ABI work of Phases 8 and 9
cover the ring side, which the HCD keeps).

### 11.4 What 25.7 leaves to Phase 26

- The map-register grant on Windows 98 SE and Windows 2000 (26-A.5, a
  reading at first start).
- Whether Windows 98's `AllocateAdapterChannel` calls its execution routine
  synchronously at DISPATCH_LEVEL or later, which decides whether a URB can be
  queued and mapped in one pass (26-A.5; Windows 98 SE's `openhci.sys` call
  site in section 7.1 is the place to read it from).
- The common-buffer growth question of 11.2, in the phase that first hits a
  limit.

## 12. What Phase 25 leaves open, and where each item binds

Phase 25 read contracts and built a scaffold; it ran nothing on a guest. Every
item below is a question this record could not settle from a binary, a
header or a build, with the task that settles it. The sections above carry
the detail; this is the index.

| Item | Section | Settled by |
|---|---|---|
| The KMDF ASIX driver (3.4.3.38, Windows 2000 and XP) sends its USB traffic through `Wdf01000.sys`, which was not read; `CYCLE_PORT` and `RESET_PIPE` are inferred from function names only | 6 | 26-A.6 (read KMDF 1.7's USB target, or observe on the Windows 2000 guest) |
| Whether the class drivers tolerate a failed answer to the requests the NT 6.x line added (`GET_TOPOLOGY_ADDRESS`, `RECORD_FAILURE`, the USBDI interface) | 6 | 26-A.6 and 28-A.1 |
| URB functions passed through memory or across branches, which the straight-line scan of section 6 can miss | 6 | 26-A.5: the HCD counts every URB function it is sent, and an unexpected one is a counter, not a crash |
| Windows 98's map-register grant and whether its `AllocateAdapterChannel` runs the execution routine synchronously | 11 | 26-A.5 |
| PCI configuration-space access on Windows 98 (`IRP_MN_READ_CONFIG` or `BUS_INTERFACE_STANDARD`; the `HalGetBusData` family has no Windows 98 precedent) | 7 | **Settled in 26-A.2 (2026-10-03): `IRP_MN_READ_CONFIG` / `WRITE_CONFIG` sent to the PCI PDO**, the route NUSB's `USBPORT.SYS` and 98 SE's `uhcd.sys` take (static, `legal-provenance.md` section 4) and the HCD's (`HcdSvcConfigSpace`). Runtime, QEMU, `qemu` build: on Windows 98 SE that IRP sent to the attached lower object was refused and `GUID_BUS_INTERFACE_STANDARD` answered `STATUS_NOT_IMPLEMENTED` (`C0000002`); sent to the PDO it read the vendor and device ids and the interrupt pin, and the start completed. On Windows 2000 both targets answered |
| The PASSIVE context: a system thread (stock precedent `bt829.sys`) rather than work items, confirmed on both primaries | 5.4 | **Confirmed in 26-A.2 (2026-10-03)**: the controller thread starts with the controller on both primaries and stops cleanly across repeated disable/enable cycles - waited for on its thread object on NT and on an event on Windows 98, where the thread-object wait faulted (`runs/run-26.md`); its 100 ms health poll leaves no trace of its own yet, so that it ran is not an observation |
| `DeviceIsHub` for hubs inside the bus, the driver key returned for a split composite device, and the `ControllerFlavor` value the HCD reports to Vista and Windows 7 `usbui.dll` | 8 | 26-A.8 |
| The `SymbolicName` value and the device interfaces both devnodes must carry, so the property pages find them on every generation | 8 | 26-A.8 |
| `XHCISNAP`'s software-key finder (it matches `xhci98.sys`) and its payload schema for the HCD's extension | 8, 9 | 26-A.8 |
| `source-stamp.ps1` and staging in `make-package.ps1`; publishing in `make-release.ps1` | 9 | 26-A.1; 32.3 |
| The HCD INFs' install footprints (`-EmitFootprint`) and the self-test comparing the two INFs where they must agree | 9 | done in 25.8: both footprint files replaced by the HCD INFs' and the comparisons updated; re-emitted with every INF change |
| The matrix's read route for the HCD (the QEMU monitor reading a DDK-free counter block announced on the debug console) and the expectation-set split | 9 | 26-A.8 and 26-A.10 |
| The USB 2.0 hub and reset timings marked "to transcribe" | 10 | 27-A.1's transcription, before the hub class is written |
| Common-buffer growth beyond design record 04's limits | 11.2 | the phase that first hits a limit (27 for slots, 31 for streams) |
| The `win98-evidence.list` rows for the new precedent binaries | 7, Appendix A | added with this record; the gate re-derives the HCD's rows from them when 26-A.1 adds the rows themselves |

## 13. Polling a Low- or Full-Speed interrupt endpoint faster than 1 ms (task 33.8, 2026-10-04)

The request is issue 4's: LordOfMice, who maintains SweetLow's hidusbf,
asked on 2026-10-03 for "8000 Hz on Low Speed device as under modern Windows
NT OSes". Under the `1.x.x.x` miniport's virtual High-Speed hub his device
looked High Speed, hidusbf offered its High-Speed values 1 to 3 (8000 to
2000 Hz), and the driver gave 1000 Hz. The owner's decision of 2026-10-04
for `2.1.0.0`: see whether it can be set higher. Every fact below is tagged
with how it was obtained; nothing in this section was read out of a binary,
so it adds no `legal-provenance.md` row.

### 13.1 How hidusbf asks for more than 1000 Hz (documentation)

A Low- or Full-Speed `bInterval` is in milliseconds, 1 to 255 (Table 6-12),
so no descriptor value says "faster than 1 ms", and hidusbf does not write
one. Its own documentation, in `tools\hidusbf-extracted` (`hidusbf.zip`,
SHA-256 `bd8d1fb0...f797`, not tracked), says how it gets there:

- `README.2kHz-8kHz.ENG.TXT`: the 2-8 kHz rates need "usb 3.x", "the
  microsoft usb 3.x driver" and Windows 8, 8.1 or 10, and they **reuse the
  two slowest rates of the list**: "2kHz-4kHz driver 31 = 2000Hz 62 =
  4000Hz", "4kHz-8kHz driver 31 = 4000Hz 62 = 8000Hz".
- `README.ENG.TXT`, the entry of 2025/11/05: the variants differ only by a
  registry value, `PatchUSBXHCI`, "0 - disable patching, 1 - 1k patching,
  2 - 2k-4k patching, 3 - 4k-8k patching"; and its warnings: overclocking
  "may not work for Low Speed USB devices which controlled by non Microsoft
  USB stack", and the patching builds must not run under Memory Integrity.
  The entry of 2005/12/26 names the method: "patching the code of
  USBPORT.SYS on the fly".

So (documentation, plus one inference marked as such): the filter keeps
writing an ordinary millisecond `bInterval` into the configuration
descriptor of `URB_FUNCTION_SELECT_CONFIGURATION` - the Windows 9x filter
was read doing exactly that (static, `legal-provenance.md` section 4,
hidusbf row) - and the rate above 1000 Hz comes from **patching Microsoft's
`usbxhci.sys` in memory** so that it programs a smaller Interval for the
"31 Hz" and "62 Hz" settings. That those are `bInterval` 32 and 16 (Interval
8 and 7 by footnote 113) is inferred from the labels (1000/32 and 1000/16),
not read. How the patch computes its Interval, and whether it touches
devices the filter is not on, is not documented and was not read; this
project reads no patch code and patches nothing. Under the HCD there is no
`usbxhci.sys` and no `usbport.sys` to patch, so the HCD is the only place
the rate can come from.

### 13.2 What Linux programs (reference source)

`external/linux/xhci-mem.c` (torvalds/linux `c6859eed`), function
`xhci_get_endpoint_interval`: a Low- or Full-Speed interrupt endpoint goes
through `xhci_parse_frame_interval`, which is
`xhci_microframes_to_exponent(udev, ep, bInterval * 8, 3, 10)` - the
exponent clamped to **3..10**. The two interval quirks only lower large
values (`XHCI_LIMIT_ENDPOINT_INTERVAL_9`, AMD and one ATI part: 9 and up
become 8; `_7`, TI 0x8241, High Speed and up only: 7 and up become 6).
No path in the mirror programs a Full- or Low-Speed interrupt Interval
below 3.

### 13.3 What xHCI 1.2c says about an Interval below 3 (specification)

- Table 6-12 (p.420), FS/LS Interrupt row: bInterval 1-255, "bInterval *
  1ms", **"Endpoint Context Valid Interval range 3-10"**; footnote 113:
  "round the computed value ... down to the nearest base 2 multiple of
  bInterval * 8".
- 6.2.3.6 (p.419): "system software shall translate the bInterval field in
  the USB Endpoint Descriptor to the appropriate value for this field" and
  "Refer to Table 6-12 for the range of valid Interval values".
- Configure Endpoint, 4.6.6 (p.112): "Not all Input Endpoint Contexts
  identified by Add Context flag fields = '1' are valid" gives
  **Parameter Error**; Table 6-90 (p.467): Parameter Error is "Asserted by
  a command if a Context parameter is invalid", Bandwidth Error (8) when
  the periodic endpoints declared do not fit.
- 4.14.3 (p.244): the Interval is "treated as a throttling parameter or a
  deadline by the xHC for Interrupt endpoints"; "the xHC should consume no
  more than one TD per ESIT".

So an Interval of 0 to 2 on a FS/LS interrupt endpoint is **outside the
valid range**: a controller may refuse it with Parameter Error (it "should
check"; nothing says it must), may refuse it on bandwidth, or may accept
it and run it, and what a controller that accepts it does on the wire is
not specified. That hidusbf's 2-8 kHz rates work under Microsoft's
`usbxhci.sys` (documentation and its users' reports, not this project's
measurement) says that at least the controllers its users own accept and
run it at a root port.

### 13.4 What QEMU's xHCI does with the field (emulator source)

`external/qemu/hcd-xhci.c`, tag `v11.1.0` (fetched 2026-10-04, SHA-256
`561518AE...65CD8`): `xhci_init_epctx` takes `interval = 1u << MIN(Interval,
18)` with no check against speed or type, `xhci_configure_slot` returns no
Parameter Error for any Endpoint Context field, and `xhci_calc_intr_kick`
schedules an interrupt TD at `MAX(asap, mfindex_last + interval)`
microframes. So QEMU accepts Interval 0 on a Full-Speed endpoint and paces
it at 125 us. A QEMU reading can show that the HCD programmed the Interval
and that the emulator paces by it; it **cannot** show that a real
controller accepts it, and it can never exercise the fallback below.

### 13.5 The verdict and the design

**Feasible on some controllers only, and off by default.** Out of
specification by Table 6-12; the Configure Endpoint refusal is the
specification's own failure mode, so it can be caught and undone; whether a
given controller accepts and honours it is for the bench to read
(13.6). The risk with it off is none: nothing changes. With it on: a
controller that accepts an out-of-range Interval and then misbehaves -
polls at its own rate, starves other periodic traffic, or worse - which
no QEMU run can rule out.

- **The value**: `XhciFastPollFsLs`, a REG_DWORD on the controller's driver
  key, read at each controller start (`hcd_ctl.c`), written by no INF.
  `XhciPipeFastMode`: 1, 2 and 3 are themselves, anything else - absent, 0,
  a value of another type, 4 and up - is off. Logged as `fastpoll.value`
  and `fastpoll.mode`.
- **The mapping** (`XhciPipeFastPoll`, `xhci_pipe.c`), hidusbf's own numbers
  so a user picks the rate in the terms its Setup already uses: 1 changes
  nothing (Table 6-12 already gives a Low-Speed endpoint 1 ms); 2 maps
  Interval 8 (`bInterval` 32-63, its "31 Hz") to 2 (500 us, 2000 Hz) and
  Interval 7 (16-31, "62 Hz") to 1 (250 us, 4000 Hz); 3 maps them to 1
  (4000 Hz) and 0 (125 us, 8000 Hz). Nothing else changes: not another
  `bInterval`, not High Speed or SuperSpeed, not isochronous, not a hub's
  own status endpoint (`HcdCfgHubOpen` never asks), and **not a device
  behind a hub** (Route String nonzero), whose Full- or Low-Speed periodic
  traffic a transaction translator or a Full-Speed hub carries one frame at
  a time. The bound is the table: no Interval below 0, none from any other
  input. The client is told the `bInterval` it gave, as before. The cost
  is that a device that itself declares 16-63 ms on a root port is sped up
  too while the value is set, which is why it is a controller value a user
  sets for a purpose and not a default.
- **The refusal path** (`hcdCfgConfigureAdd`, `hcd_cfg.c`): every Configure
  Endpoint that adds pipes - `SELECT_CONFIGURATION`, `SELECT_INTERFACE`, a
  function's select, and the replays (`RESET_PORT`, the endpoint recycle) -
  goes through one helper. If it fails with Parameter, Bandwidth or
  Secondary Bandwidth Error (`XhciPipeFastRetry`) while adding a pipe the
  mapping changed, every such pipe goes back to its Table 6-12 Interval
  (`XhciPipeFastRevert`) and the command is built and issued once more - a
  refused command leaves the Output Device Context as it was (4.6.6), so the
  second command is the one the select would have issued with the value
  off. Counted in `fastpoll.fallbacks` (with a `fastpoll.fallback` record:
  slot, how many pipes, the code); an endpoint opened fast is
  `fastpoll.opened` and a `fastpoll.open` record (slot, DCI, the Table 6-12
  Interval, the one programmed). Both counts are in the counter block.
- **Host vectors**: `test\test_pipe.c`, `test_fast_poll`: the mode
  sanitising, the table at both speeds, the whole 16-63 band and its edges
  (15, 64, 1), off/1/unknown modes, behind a hub, High Speed, isochronous,
  no double application, the revert and the retry codes.

### 13.6 What is still owed, and the bench procedure

Nothing here has run on a guest or on metal. A QEMU leg was tried on
2026-10-04 (Windows ME, `winme-sl-base` overlay, `qemu` build of the
branch, harness `out\phase33\fastpoll\`, git-ignored): the controller
installed from `xhci98.inf`, and every boot after the install's restart
stopped in real mode before Windows entered protected mode (CR0 = 0x10, CS
= 0x933B, EIP fixed at 0x6173, an empty debug console - no driver code
ran), a hard reset included; not investigated further. By 13.4 a QEMU
reading could in any case show only that the Interval was programmed and
paced, never a controller's acceptance or the fallback.

The reading that matters is the owner's hardware, the ThinkPad E460 and
the P14s Gen 1, on a build of this branch:

1. A Low-Speed (or Full-Speed) mouse **on a root port**, no hub. hidusbf on
   it as 33.7 installs it: on Windows 98 SE (with NUSB 3.6's `usbd.sys`)
   or ME, `hidusbf.sys` in `SYSTEM32\DRIVERS`, `LowerFilters` =
   `"hidusbf.sys"` on the mouse's `Enum\USB` instance and a `bInterval`
   DWORD on the instance and on its `Class\HID` driver key; on XP,
   hidusbf's `Setup.exe` (the `NOPATCH` build: there is no `usbport.sys`
   or `usbxhci.sys` under the HCD to patch).
2. Turn the log on first, then restart (both switches are read at
   controller start; the install writes them 0, which records nothing):
   `XhciLogVerbosity` 2 or more, so the ring records the
   `fastpoll.value`/`.mode` notes of each start and the
   `fastpoll.open`/`.fallback` records. Read them either with DebugView
   and `XhciLogDebugView` 1 - which also delivers the counter block a
   controller stop (a disable or a shutdown) appends, its only route, but
   drains the ring continuously - or with `XHCISNAP` and
   `XhciLogDebugView` 0, which keeps the records in the ring for it but
   shows no counter block. Then `bInterval` 16 (hidusbf's "62 Hz"),
   `XhciFastPollFsLs` absent: replug, and read about 62 Hz with a mouse
   rate tool, `fastpoll.mode` 0 and no `fastpoll.open` record.
3. `XhciFastPollFsLs` = 3 (REG_DWORD) on the controller's driver key (the
   `Class\USB\nnnn` key the other `Xhci*` values are in), restart (it is
   read at controller start), replug: expect `fastpoll.mode` 3,
   `fastpoll.opened` up by one per configuration of the mouse (the counts
   are never zeroed, and a mouse attached across the restart is configured
   once at start and again at the replug, so compare before and after one
   attachment), a `fastpoll.open` record ending `0700` (Interval 7
   to 0), `fastpoll.fallbacks` unchanged, and the tool reading toward
   8000 Hz - or `fastpoll.fallbacks` up by one and 62 Hz again on a
   controller that refuses, which is itself the reading.
4. `bInterval` 32 ("31 Hz") at 3 (4000 Hz expected, record `0801`), and
   both at 2 (2000 and 4000 Hz).
5. With the value at 3, ten minutes of use: the mouse never stalls, a
   keyboard and a stick on the other ports keep working, and the same mouse
   behind a hub stays at its Table 6-12 rate (no `fastpoll.open` record).
   Then delete the value, restart, and confirm 62 Hz again.

A machine whose Windows mouse path cannot report more than it is given
(Windows 98's mouse stack may coalesce) still shows the programmed rate in
the counters; the rate tool is the second reading, not the first.

## Appendix A. The Windows 98 export evidence, pair by pair (task 25.3)

The full table section 7.3 summarises: 205 candidate pairs, each with its
`w2k-export`, `win98-precedent` and `ntkern-name` evidence as the import
gate's own functions return it. Static; a subagent's read, 2026-10-02, at
`5086e2d`, not re-read line by line by the coordinator. Section 7 gives the
method.

How to read it:

- **Pair**: the module the Windows 2000 DDK link records for the name (section
  7, "the module per pair"), and the symbol.
- **Win98 SE precedent (stock)**: how many of the 69 Microsoft WDM drivers on
  the Windows 98 SE CD import the pair, then up to three of them, preferring
  `uhcd.sys`, `openhci.sys`, `usbhub.sys` and `usbd.sys` (all 4.10.2222
  unless shown), with the hint `dumpbin /imports` prints.
- **NUSB precedent**: NUSB 3.3 (listed) and NUSB 3.6 (the draft rows below)
  counted separately; NUSB 3.6's `uhcd.sys`, `openhci.sys`, `usbd.sys` and
  `usbhub.sys` are Windows ME builds (4.90.300x). A NUSB 3.6 precedent for a
  name `wdmstub.sys` supplies is platform evidence, not stock evidence.
- **ntkern-name**: "no" means nothing (the gate's rule; section 7.2).
- **Verdict**: evidenced = `w2k-export` and at least one Windows 98 tag,
  graded by the strongest tag present; "w2k only - needs a Win98 tag or a
  load test" otherwise. A miss is never "absent on 98". Rows marked EXTENSION
  were added beyond the task's list.

| # | Pair (module per DDK link) | w2k-export | Win98 SE precedent (stock) | NUSB precedent | ntkern-name | Verdict | Note |
|---|---|---|---|---|---|---|---|
| 1 | `ntoskrnl.exe!ExAllocatePool` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | only after xhci_compat.h's #undef; ntddk.h POOL_TAGGING rewrites ExAllocatePool(a,b) to ExAllocatePoolWithTag(a,b,' kdD') |
| 2 | `ntoskrnl.exe!ExAllocatePoolWithTag` | w2k-export | 62: uhcd.sys 4.10.2222 (hint 7); openhci.sys 4.10.2222 (hint 7); usbhub.sys 4.10.2222 (hint 7); +59 more | 16: uhcd.sys 4.90.3000.1 (hint 7); openhci.sys 4.90.3000.1 (hint 7); +14 more [3.3: 7, 3.6: 9] | yes | **evidenced** (stock 98 SE precedent) | what ExAllocatePool becomes when the #undef is absent |
| 3 | `ntoskrnl.exe!ExFreePool` | w2k-export | 61: uhcd.sys 4.10.2222 (hint C); openhci.sys 4.10.2222 (hint C); usbhub.sys 4.10.2222 (hint C); +58 more | 16: uhcd.sys 4.90.3000.1 (hint C); openhci.sys 4.90.3000.1 (hint C); +14 more [3.3: 7, 3.6: 9] | yes | **evidenced** (stock 98 SE precedent) | not rewritten by POOL_TAGGING in the Win2K DDK |
| 4 | `ntoskrnl.exe!ExFreePoolWithTag` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test; wdmstub.sys supplies the name | not declared anywhere in the Win2K DDK headers |
| 5 | `ntoskrnl.exe!ExInitializeNPagedLookasideList` | w2k-export | 2: ohci1394.sys 4.10.2222 (hint F); ks.sys 4.10.2222 (hint F) | 1: OHCI1394.SYS 4.10.2228 (hint F) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 6 | `ntoskrnl.exe!ExDeleteNPagedLookasideList` | w2k-export | 2: ohci1394.sys 4.10.2222 (hint 9); ks.sys 4.10.2222 (hint 9) | 1: OHCI1394.SYS 4.10.2228 (hint 9) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 7 | `ntoskrnl.exe!ExInterlockedPopEntrySList` | w2k-export | 4: 1394bus.sys 4.10.2222 (hint 14); ohci1394.sys 4.10.2222 (hint 14); ks.sys 4.10.2222 (hint 14); +1 more | 3: 1394bus.sys 4.10.2226 (hint 14); OHCI1394.SYS 4.10.2228 (hint 14); +1 more [3.3: 0, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION; x86 FASTCALL, the body of the inline ExAllocateFromNPagedLookasideList |
| 8 | `ntoskrnl.exe!ExInterlockedPushEntrySList` | w2k-export | 3: ohci1394.sys 4.10.2222 (hint 15); ks.sys 4.10.2222 (hint 15); sbp2port.sys 4.10.2222 (hint 15) | 2: OHCI1394.SYS 4.10.2228 (hint 15); SBP2PORT.SYS 4.10.2227 (hint 15) [3.3: 0, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION; x86 FASTCALL, the body of the inline ExFreeToNPagedLookasideList |
| 9 | `ntoskrnl.exe!MmAllocateNonCachedMemory` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | EXTENSION |
| 10 | `ntoskrnl.exe!MmFreeNonCachedMemory` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | EXTENSION |
| 11 | `ntoskrnl.exe!IoCreateDevice` | w2k-export | 44: uhcd.sys 4.10.2222 (hint 38); openhci.sys 4.10.2222 (hint 38); usbhub.sys 4.10.2222 (hint 38); +41 more | 16: uhcd.sys 4.90.3000.1 (hint 38); openhci.sys 4.90.3000.1 (hint 38); +14 more [3.3: 7, 3.6: 9] | yes | **evidenced** (stock 98 SE precedent) |  |
| 12 | `ntoskrnl.exe!IoDeleteDevice` | w2k-export | 42: uhcd.sys 4.10.2222 (hint 3D); openhci.sys 4.10.2222 (hint 3D); usbhub.sys 4.10.2222 (hint 3D); +39 more | 16: uhcd.sys 4.90.3000.1 (hint 3D); openhci.sys 4.90.3000.1 (hint 3D); +14 more [3.3: 7, 3.6: 9] | yes | **evidenced** (stock 98 SE precedent) |  |
| 13 | `ntoskrnl.exe!IoAttachDeviceToDeviceStack` | w2k-export | 37: uhcd.sys 4.10.2222 (hint 30); openhci.sys 4.10.2222 (hint 30); usbhub.sys 4.10.2222 (hint 30); +34 more | 14: uhcd.sys 4.90.3000.1 (hint 30); openhci.sys 4.90.3000.1 (hint 30); +12 more [3.3: 7, 3.6: 7] | yes | **evidenced** (stock 98 SE precedent) |  |
| 14 | `ntoskrnl.exe!IoDetachDevice` | w2k-export | 27: uhcd.sys 4.10.2222 (hint 3F); openhci.sys 4.10.2222 (hint 3F); usbhub.sys 4.10.2222 (hint 3F); +24 more | 14: uhcd.sys 4.90.3000.1 (hint 3F); openhci.sys 4.90.3000.1 (hint 3F); +12 more [3.3: 7, 3.6: 7] | yes | **evidenced** (stock 98 SE precedent) |  |
| 15 | `ntoskrnl.exe!IoCreateSymbolicLink` | w2k-export | 14: usbd.sys 4.10.2222 (hint 3B); usbser.sys 4.10.2222 (hint 3B); portcls.sys 4.10.2222 (hint 3B); +11 more | 2: usbd.sys 4.90.3000.1 (hint 3B); USBPORT.SYS 5.00.2195.5652 (hint 3D) [3.3: 1, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 16 | `ntoskrnl.exe!IoDeleteSymbolicLink` | w2k-export | 14: usbd.sys 4.10.2222 (hint 3E); usbser.sys 4.10.2222 (hint 3E); ohci1394.sys 4.10.2222 (hint 3E); +11 more | 4: usbd.sys 4.90.3000.1 (hint 3E); OHCI1394.SYS 4.10.2228 (hint 3E); +2 more [3.3: 1, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 17 | `ntoskrnl.exe!IoRegisterDeviceInterface` | w2k-export | 10: usbhub.sys 4.10.2222 (hint 54); usbd.sys 4.10.2222 (hint 54); hidclass.sys 4.10.2222 (hint 54); +7 more | 6: usbhub.sys 4.90.3002.1 (hint 15C); usbd.sys 4.90.3000.1 (hint 54); +4 more [3.3: 4, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) |  |
| 18 | `ntoskrnl.exe!IoSetDeviceInterfaceState` | w2k-export | 9: usbhub.sys 4.10.2222 (hint 5A); usbd.sys 4.10.2222 (hint 5A); hidclass.sys 4.10.2222 (hint 5A); +6 more | 6: usbhub.sys 4.90.3002.1 (hint 170); usbd.sys 4.90.3000.1 (hint 5A); +4 more [3.3: 4, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) |  |
| 19 | `ntoskrnl.exe!IoGetDeviceProperty` | w2k-export | 8: usbhub.sys 4.10.2222 (hint 49); usbd.sys 4.10.2222 (hint 49); hidclass.sys 4.10.2222 (hint 49); +5 more | 4: usbhub.sys 4.90.3002.1 (hint 138); usbd.sys 4.90.3000.1 (hint 49); +2 more [3.3: 2, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) |  |
| 20 | `ntoskrnl.exe!IoInvalidateDeviceRelations` | w2k-export | 4: usbhub.sys 4.10.2222 (hint 4F); hidclass.sys 4.10.2222 (hint 4F); 1394bus.sys 4.10.2222 (hint 4F); +1 more | 4: usbhub.sys 4.90.3002.1 (hint 147); 1394bus.sys 4.10.2226 (hint 4F); +2 more [3.3: 1, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 21 | `ntoskrnl.exe!IoInvalidateDeviceState` | w2k-export | 2: usbhub.sys 4.10.2222 (hint 50); sbp2port.sys 4.10.2222 (hint 50) | 3: usbhub.sys 4.90.3002.1 (hint 148); SBP2PORT.SYS 4.10.2227 (hint 50); +1 more [3.3: 1, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) |  |
| 22 | `ntoskrnl.exe!IofCallDriver` | w2k-export | 50: uhcd.sys 4.10.2222 (hint 68); openhci.sys 4.10.2222 (hint 68); usbhub.sys 4.10.2222 (hint 68); +47 more | 16: uhcd.sys 4.90.3000.1 (hint 68); openhci.sys 4.90.3000.1 (hint 68); +14 more [3.3: 7, 3.6: 9] | yes | **evidenced** (stock 98 SE precedent) | IoCallDriver(a,b) macro |
| 23 | `ntoskrnl.exe!IofCompleteRequest` | w2k-export | 44: usbhub.sys 4.10.2222 (hint 69); usbd.sys 4.10.2222 (hint 69); hidclass.sys 4.10.2222 (hint 69); +41 more | 15: uhcd.sys 4.90.3000.1 (hint 69); openhci.sys 4.90.3000.1 (hint 69); +13 more [3.3: 6, 3.6: 9] | yes | **evidenced** (stock 98 SE precedent) | IoCompleteRequest(a,b) macro |
| 24 | `ntoskrnl.exe!IoAllocateIrp` | w2k-export | 31: uhcd.sys 4.10.2222 (hint 2D); openhci.sys 4.10.2222 (hint 2D); usbhub.sys 4.10.2222 (hint 2D); +28 more | 13: uhcd.sys 4.90.3000.1 (hint 2D); openhci.sys 4.90.3000.1 (hint 2D); +11 more [3.3: 7, 3.6: 6] | yes | **evidenced** (stock 98 SE precedent) |  |
| 25 | `ntoskrnl.exe!IoFreeIrp` | w2k-export | 32: uhcd.sys 4.10.2222 (hint 42); openhci.sys 4.10.2222 (hint 42); usbhub.sys 4.10.2222 (hint 42); +29 more | 13: uhcd.sys 4.90.3000.1 (hint 42); openhci.sys 4.90.3000.1 (hint 42); +11 more [3.3: 7, 3.6: 6] | yes | **evidenced** (stock 98 SE precedent) |  |
| 26 | `ntoskrnl.exe!IoInitializeIrp` | w2k-export | 10: usbhub.sys 4.10.2222 (hint 4D); 1394bus.sys 4.10.2222 (hint 4D); ccport.sys 4.10.2222 (hint 4D); +7 more | 5: usbhub.sys 4.90.3002.1 (hint 144); 1394bus.sys 4.10.2226 (hint 4D); +3 more [3.3: 2, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 27 | `ntoskrnl.exe!IoReuseIrp` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test; wdmstub.sys supplies the name | EXTENSION; ntddk.h only |
| 28 | `ntoskrnl.exe!IoBuildDeviceIoControlRequest` | w2k-export | 25: usbhub.sys 4.10.2222 (hint 32); usbd.sys 4.10.2222 (hint 32); usbser.sys 4.10.2222 (hint 32); +22 more | 6: usbhub.sys 4.90.3002.1 (hint 105); usbd.sys 4.90.3000.1 (hint 32); +4 more [3.3: 3, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 29 | `ntoskrnl.exe!IoBuildSynchronousFsdRequest` | w2k-export | 7: ks.sys 4.10.2222 (hint 34); stream.sys 4.10.2222 (hint 34); portcls.sys 4.10.2222 (hint 34); +4 more | 2: SBP2PORT.SYS 4.10.2227 (hint 34); USBSTOR.SYS 4.90.3000.1 (hint 107) [3.3: 1, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 30 | `ntoskrnl.exe!IoCancelIrp` | w2k-export | 15: openhci.sys 4.10.2222 (hint 35); usbhub.sys 4.10.2222 (hint 35); usbd.sys 4.10.2222 (hint 35); +12 more | 7: usbhub.sys 4.90.3002.1 (hint 10A); usbd.sys 4.90.3000.1 (hint 35); +5 more [3.3: 3, 3.6: 4] | yes | **evidenced** (stock 98 SE precedent) |  |
| 31 | `ntoskrnl.exe!IoAcquireCancelSpinLock` | w2k-export | 10: uhcd.sys 4.10.2222 (hint 29); openhci.sys 4.10.2222 (hint 29); usbhub.sys 4.10.2222 (hint 29); +7 more | 7: uhcd.sys 4.90.3000.1 (hint 29); openhci.sys 4.90.3000.1 (hint 29); +5 more [3.3: 3, 3.6: 4] | yes | **evidenced** (stock 98 SE precedent) |  |
| 32 | `ntoskrnl.exe!IoReleaseCancelSpinLock` | w2k-export | 13: uhcd.sys 4.10.2222 (hint 57); openhci.sys 4.10.2222 (hint 57); usbhub.sys 4.10.2222 (hint 57); +10 more | 8: uhcd.sys 4.90.3000.1 (hint 57); openhci.sys 4.90.3000.1 (hint 57); +6 more [3.3: 3, 3.6: 5] | yes | **evidenced** (stock 98 SE precedent) |  |
| 33 | `ntoskrnl.exe!IoGetAttachedDeviceReference` | w2k-export | 4: dbcacpi.sys 4.10.2222 (hint 119); wdmaud.sys 4.10.2222 (hint 44); wmiacpi.sys 4.10.2222 (hint 119); +1 more | - | yes | **evidenced** (stock 98 SE precedent) |  |
| 34 | `ntoskrnl.exe!ObfDereferenceObject` | w2k-export | 17: hidclass.sys 4.10.2222 (hint AC); 1394bus.sys 4.10.2222 (hint AC); ks.sys 4.10.2222 (hint AC); +14 more | 3: 1394bus.sys 4.10.2226 (hint AC); USBPORT.SYS 5.00.2195.5652 (hint B5); +1 more [3.3: 1, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | ObDereferenceObject macro |
| 35 | `ntoskrnl.exe!ObfReferenceObject` | w2k-export | 15: usbhub.sys 4.10.2222 (hint AD); usbd.sys 4.10.2222 (hint AD); hidclass.sys 4.10.2222 (hint AD); +12 more | 9: usbhub.sys 4.90.3002.1 (hint 2B5); usbd.sys 4.90.3000.1 (hint AE); +7 more [3.3: 3, 3.6: 6] | yes | **evidenced** (stock 98 SE precedent) | ObReferenceObject macro |
| 36 | `ntoskrnl.exe!IoWMIRegistrationControl` | w2k-export | 2: usbhub.sys 4.10.2222 (hint 64); wmiacpi.sys 4.10.2222 (hint 156) | 2: usbhub.sys 4.90.3002.1 (hint 188); USBHUB20.SYS 5.00.2195.6891 (hint 190) [3.3: 1, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 37 | `ntoskrnl.exe!IoOpenDeviceRegistryKey` | w2k-export | 19: usbhub.sys 4.10.2222 (hint 53); usbd.sys 4.10.2222 (hint 53); hidclass.sys 4.10.2222 (hint 53); +16 more | 10: usbhub.sys 4.90.3002.1 (hint 14F); usbd.sys 4.90.3000.1 (hint 53); +8 more [3.3: 4, 3.6: 6] | yes | **evidenced** (stock 98 SE precedent) |  |
| 38 | `ntoskrnl.exe!IoReportTargetDeviceChange` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 39 | `ntoskrnl.exe!IoReportTargetDeviceChangeAsynchronous` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test; wdmstub.sys supplies the name | ntddk.h only |
| 40 | `ntoskrnl.exe!IoIsWdmVersionAvailable` | w2k-export | 1: ohci1394.sys 4.10.2222 (hint 51) | 4: OHCI1394.SYS 4.10.2228 (hint 51); SBP2PORT.SYS 4.10.2227 (hint 51); +2 more [3.3: 1, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION; in the miniport's 1.2.0.0 allowlist |
| 41 | `ntoskrnl.exe!IoAllocateErrorLogEntry` | w2k-export | 4: stream.sys 4.10.2222 (hint 2C); kmixer.sys 4.10.2222 (hint 2C); sbp2port.sys 4.10.2222 (hint 2C); +1 more | 1: SBP2PORT.SYS 4.10.2227 (hint 2C) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 42 | `ntoskrnl.exe!IoWriteErrorLogEntry` | w2k-export | 4: stream.sys 4.10.2222 (hint 67); kmixer.sys 4.10.2222 (hint 67); sbp2port.sys 4.10.2222 (hint 67); +1 more | 1: SBP2PORT.SYS 4.10.2227 (hint 67) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 43 | `ntoskrnl.exe!IoGetDeviceObjectPointer` | w2k-export | 3: 1394bus.sys 4.10.2222 (hint 48); compbatt.sys 4.10.2222 (hint 48); nabtsfec.sys 4.10.1998 (hint 11D) | 2: 1394bus.sys 4.10.2226 (hint 48); wdmstub.sys 5.00.006 (hint 13D) [3.3: 0, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 44 | `ntoskrnl.exe!IoRegisterPlugPlayNotification` | w2k-export | 4: compbatt.sys 4.10.2222 (hint 55); redbook.sys 4.10.2222 (hint 55); sysaudio.sys 4.10.2222 (hint 55); +1 more | - | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 45 | `ntoskrnl.exe!IoUnregisterPlugPlayNotification` | w2k-export | 3: redbook.sys 4.10.2222 (hint 61); sysaudio.sys 4.10.2222 (hint 61); wdmaud.sys 4.10.2222 (hint 61) | - | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 46 | `ntoskrnl.exe!IoAcquireRemoveLockEx` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest); wdmstub.sys supplies the name | EXTENSION; IoAcquireRemoveLock macro |
| 47 | `ntoskrnl.exe!IoReleaseRemoveLockEx` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest); wdmstub.sys supplies the name | EXTENSION |
| 48 | `ntoskrnl.exe!IoInitializeRemoveLockEx` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest); wdmstub.sys supplies the name | EXTENSION |
| 49 | `WMILIB.SYS!WmiSystemControl` | n/a (not a kernel/HAL pair) | 2: usbhub.sys 4.10.2222 (hint 2); wmiacpi.sys 4.10.2222 (hint 2) | 2: usbhub.sys 4.90.3002.1 (hint 2); USBHUB20.SYS 5.00.2195.6891 (hint 2) [3.3: 1, 3.6: 1] | no (means nothing) | evidenced (Win98 SE's own WMILIB.SYS module); wdmstub.sys supplies the name | EXTENSION; wmilib.lib |
| 50 | `WMILIB.SYS!WmiCompleteRequest` | n/a (not a kernel/HAL pair) | 2: usbhub.sys 4.10.2222 (hint 0); wmiacpi.sys 4.10.2222 (hint 0) | 2: usbhub.sys 4.90.3002.1 (hint 0); USBHUB20.SYS 5.00.2195.6891 (hint 0) [3.3: 1, 3.6: 1] | no (means nothing) | evidenced (Win98 SE's own WMILIB.SYS module); wdmstub.sys supplies the name | EXTENSION; wmilib.lib |
| 51 | `ntoskrnl.exe!PoCallDriver` | w2k-export | 22: usbhub.sys 4.10.2222 (hint AE); usbd.sys 4.10.2222 (hint AE); usbser.sys 4.10.2222 (hint AE); +19 more | 10: usbhub.sys 4.90.3002.1 (hint 2BA); usbd.sys 4.90.3000.1 (hint AF); +8 more [3.3: 4, 3.6: 6] | yes | **evidenced** (stock 98 SE precedent) |  |
| 52 | `ntoskrnl.exe!PoStartNextPowerIrp` | w2k-export | 26: usbhub.sys 4.10.2222 (hint B6); usbd.sys 4.10.2222 (hint B6); hidclass.sys 4.10.2222 (hint B6); +23 more | 11: usbhub.sys 4.90.3002.1 (hint 2C4); usbd.sys 4.90.3000.1 (hint B5); +9 more [3.3: 4, 3.6: 7] | yes | **evidenced** (stock 98 SE precedent) |  |
| 53 | `ntoskrnl.exe!PoRequestPowerIrp` | w2k-export | 15: usbhub.sys 4.10.2222 (hint B3); usbd.sys 4.10.2222 (hint B3); hidclass.sys 4.10.2222 (hint B3); +12 more | 8: usbhub.sys 4.90.3002.1 (hint 2BF); usbd.sys 4.90.3000.1 (hint B2); +6 more [3.3: 3, 3.6: 5] | yes | **evidenced** (stock 98 SE precedent) |  |
| 54 | `ntoskrnl.exe!PoSetPowerState` | w2k-export | 6: usbd.sys 4.10.2222 (hint B4); 1394bus.sys 4.10.2222 (hint B4); ohci1394.sys 4.10.2222 (hint B4); +3 more | 3: usbd.sys 4.90.3000.1 (hint B3); 1394bus.sys 4.10.2226 (hint B4); +1 more [3.3: 0, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 55 | `ntoskrnl.exe!PoRegisterDeviceForIdleDetection` | w2k-export | 2: portcls.sys 4.10.2222 (hint B0); sbp2port.sys 4.10.2222 (hint B0) | 1: SBP2PORT.SYS 4.10.2227 (hint B0) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) | note only |
| 56 | `(no DDK lib)!PoSetDeviceBusy` | not in any DDK import lib; exported by:  | - | - | no (means nothing) | no import: not in ntoskrnl.lib/hal.lib/wmilib.lib (macro, intrinsic, or later-OS name) | EXTENSION; inline/macro? checked in table |
| 57 | `ntoskrnl.exe!IoConnectInterrupt` | w2k-export | 7: uhcd.sys 4.10.2222 (hint 37); openhci.sys 4.10.2222 (hint 37); ohci1394.sys 4.10.2222 (hint 37); +4 more | 4: uhcd.sys 4.90.3000.1 (hint 37); openhci.sys 4.90.3000.1 (hint 37); +2 more [3.3: 1, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 58 | `ntoskrnl.exe!IoDisconnectInterrupt` | w2k-export | 7: uhcd.sys 4.10.2222 (hint 40); openhci.sys 4.10.2222 (hint 40); ohci1394.sys 4.10.2222 (hint 40); +4 more | 4: uhcd.sys 4.90.3000.1 (hint 40); openhci.sys 4.90.3000.1 (hint 40); +2 more [3.3: 1, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 59 | `ntoskrnl.exe!KeSynchronizeExecution` | w2k-export | 8: uhcd.sys 4.10.2222 (hint 92); openhci.sys 4.10.2222 (hint 92); ohci1394.sys 4.10.2222 (hint 92); +5 more | 3: uhcd.sys 4.90.3000.1 (hint 92); openhci.sys 4.90.3000.1 (hint 92); +1 more [3.3: 0, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 60 | `ntoskrnl.exe!KeInitializeDpc` | w2k-export | 25: uhcd.sys 4.10.2222 (hint 73); openhci.sys 4.10.2222 (hint 73); usbhub.sys 4.10.2222 (hint 73); +22 more | 10: uhcd.sys 4.90.3000.1 (hint 73); openhci.sys 4.90.3000.1 (hint 73); +8 more [3.3: 3, 3.6: 7] | no (means nothing) | **evidenced** (stock 98 SE precedent) |  |
| 61 | `ntoskrnl.exe!KeInsertQueueDpc` | w2k-export | 11: uhcd.sys 4.10.2222 (hint 7C); openhci.sys 4.10.2222 (hint 7C); 1394bus.sys 4.10.2222 (hint 7C); +8 more | 5: uhcd.sys 4.90.3000.1 (hint 7C); openhci.sys 4.90.3000.1 (hint 7C); +3 more [3.3: 1, 3.6: 4] | no (means nothing) | **evidenced** (stock 98 SE precedent) |  |
| 62 | `ntoskrnl.exe!KeRemoveQueueDpc` | w2k-export | 2: ks.sys 4.10.2222 (hint 8A); portcls.sys 4.10.2222 (hint 8A) | - | no (means nothing) | **evidenced** (stock 98 SE precedent) |  |
| 63 | `ntoskrnl.exe!KeInitializeSpinLock` | w2k-export | 33: uhcd.sys 4.10.2222 (hint 77); openhci.sys 4.10.2222 (hint 77); usbhub.sys 4.10.2222 (hint 77); +30 more | 13: uhcd.sys 4.90.3000.1 (hint 77); openhci.sys 4.90.3000.1 (hint 77); +11 more [3.3: 5, 3.6: 8] | yes | **evidenced** (stock 98 SE precedent) |  |
| 64 | `HAL.dll!KfAcquireSpinLock` | w2k-export | 32: uhcd.sys 4.10.2222 (hint 6); openhci.sys 4.10.2222 (hint 6); usbhub.sys 4.10.2222 (hint 6); +29 more | 11: uhcd.sys 4.90.3000.1 (hint 6); openhci.sys 4.90.3000.1 (hint 6); +9 more [3.3: 3, 3.6: 8] | yes | **evidenced** (stock 98 SE precedent) | KeAcquireSpinLock macro |
| 65 | `HAL.dll!KfReleaseSpinLock` | w2k-export | 32: uhcd.sys 4.10.2222 (hint 9); openhci.sys 4.10.2222 (hint 9); usbhub.sys 4.10.2222 (hint 9); +29 more | 11: uhcd.sys 4.90.3000.1 (hint 9); openhci.sys 4.90.3000.1 (hint 9); +9 more [3.3: 3, 3.6: 8] | yes | **evidenced** (stock 98 SE precedent) | KeReleaseSpinLock macro |
| 66 | `ntoskrnl.exe!KefAcquireSpinLockAtDpcLevel` | w2k-export | 13: uhcd.sys 4.10.2222 (hint 96); openhci.sys 4.10.2222 (hint 96); usbhub.sys 4.10.2222 (hint 96); +10 more | 9: uhcd.sys 4.90.3000.1 (hint 96); openhci.sys 4.90.3000.1 (hint 96); +7 more [3.3: 3, 3.6: 6] | yes | **evidenced** (stock 98 SE precedent) | KeAcquireSpinLockAtDpcLevel macro (x86) |
| 67 | `ntoskrnl.exe!KefReleaseSpinLockFromDpcLevel` | w2k-export | 13: uhcd.sys 4.10.2222 (hint 97); openhci.sys 4.10.2222 (hint 97); usbhub.sys 4.10.2222 (hint 97); +10 more | 9: uhcd.sys 4.90.3000.1 (hint 97); openhci.sys 4.90.3000.1 (hint 97); +7 more [3.3: 3, 3.6: 6] | yes | **evidenced** (stock 98 SE precedent) | KeReleaseSpinLockFromDpcLevel macro (x86) |
| 68 | `ntoskrnl.exe!KeAcquireSpinLockAtDpcLevel` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | the un-macroed name, for contrast |
| 69 | `ntoskrnl.exe!KeReleaseSpinLockFromDpcLevel` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | the un-macroed name, for contrast |
| 70 | `HAL.dll!KfRaiseIrql` | w2k-export | 9: uhcd.sys 4.10.2222 (hint 8); openhci.sys 4.10.2222 (hint 8); ks.sys 4.10.2222 (hint 8); +6 more | 6: uhcd.sys 4.90.3000.1 (hint 8); openhci.sys 4.90.3000.1 (hint 8); +4 more [3.3: 3, 3.6: 3] | no (means nothing) | **evidenced** (stock 98 SE precedent) | KeRaiseIrql macro |
| 71 | `HAL.dll!KfLowerIrql` | w2k-export | 10: uhcd.sys 4.10.2222 (hint 7); openhci.sys 4.10.2222 (hint 7); ks.sys 4.10.2222 (hint 7); +7 more | 6: uhcd.sys 4.90.3000.1 (hint 7); openhci.sys 4.90.3000.1 (hint 7); +4 more [3.3: 3, 3.6: 3] | no (means nothing) | **evidenced** (stock 98 SE precedent) | KeLowerIrql macro |
| 72 | `HAL.dll!KeRaiseIrqlToDpcLevel` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | EXTENSION |
| 73 | `HAL.dll!KeGetCurrentIrql` | w2k-export | 16: uhcd.sys 4.10.2222 (hint 3); ks.sys 4.10.2222 (hint 3); portcls.sys 4.10.2222 (hint 3); +13 more | 4: uhcd.sys 4.90.3000.1 (hint 3); NTMAP.SYS 4.10.2227 (hint 3); +2 more [3.3: 2, 3.6: 2] | no (means nothing) | **evidenced** (stock 98 SE precedent) |  |
| 74 | `ntoskrnl.exe!InterlockedIncrement` | w2k-export | 36: openhci.sys 4.10.2222 (hint 28); usbhub.sys 4.10.2222 (hint 28); hidclass.sys 4.10.2222 (hint 28); +33 more | 10: openhci.sys 4.90.3000.1 (hint 28); usbhub.sys 4.90.3002.1 (hint F4); +8 more [3.3: 5, 3.6: 5] | yes | **evidenced** (stock 98 SE precedent) | x86 FASTCALL |
| 75 | `ntoskrnl.exe!InterlockedDecrement` | w2k-export | 34: openhci.sys 4.10.2222 (hint 26); usbhub.sys 4.10.2222 (hint 26); usbser.sys 4.10.2222 (hint 26); +31 more | 10: openhci.sys 4.90.3000.1 (hint 26); usbhub.sys 4.90.3002.1 (hint F1); +8 more [3.3: 5, 3.6: 5] | yes | **evidenced** (stock 98 SE precedent) | x86 FASTCALL |
| 76 | `ntoskrnl.exe!InterlockedExchange` | w2k-export | 23: uhcd.sys 4.10.2222 (hint 27); openhci.sys 4.10.2222 (hint 27); usbhub.sys 4.10.2222 (hint 27); +20 more | 9: uhcd.sys 4.90.3000.1 (hint 27); openhci.sys 4.90.3000.1 (hint 27); +7 more [3.3: 3, 3.6: 6] | yes | **evidenced** (stock 98 SE precedent) | x86 FASTCALL; also IoSetCancelRoutine via InterlockedExchangePointer |
| 77 | `ntoskrnl.exe!InterlockedCompareExchange` | w2k-export | 7: hidclass.sys 4.10.2222 (hint 25); usbser.sys 4.10.2222 (hint 25); ks.sys 4.10.2222 (hint 25); +4 more | - | yes | **evidenced** (stock 98 SE precedent) | x86 FASTCALL; also InterlockedCompareExchangePointer |
| 78 | `ntoskrnl.exe!InterlockedExchangeAdd` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | x86: __inline in wdm.h, checked anyway |
| 79 | `ntoskrnl.exe!ExfInterlockedInsertTailList` | w2k-export | 12: 1394bus.sys 4.10.2222 (hint 20); ohci1394.sys 4.10.2222 (hint 20); ks.sys 4.10.2222 (hint 20); +9 more | 4: 1394bus.sys 4.10.2226 (hint 20); OHCI1394.SYS 4.10.2228 (hint 20); +2 more [3.3: 2, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION; ExInterlockedInsertTailList macro |
| 80 | `ntoskrnl.exe!ExfInterlockedInsertHeadList` | w2k-export | 3: hidclass.sys 4.10.2222 (hint 1F); hidvkd.sys 4.10.1998 (hint 77); msdv.sys 4.10.2222 (hint 1F) | 1: USBPORT.SYS 5.00.2195.5652 (hint 1F) [3.3: 1, 3.6: 0] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION; ExInterlockedInsertHeadList macro |
| 81 | `ntoskrnl.exe!ExfInterlockedRemoveHeadList` | w2k-export | 16: 1394bus.sys 4.10.2222 (hint 23); ohci1394.sys 4.10.2222 (hint 23); ks.sys 4.10.2222 (hint 23); +13 more | 4: 1394bus.sys 4.10.2226 (hint 23); OHCI1394.SYS 4.10.2228 (hint 23); +2 more [3.3: 2, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION; ExInterlockedRemoveHeadList macro |
| 82 | `(no DDK lib)!KeAcquireInterruptSpinLock` | not in any DDK import lib; exported by:  | - | - | no (means nothing) | no import: not in ntoskrnl.lib/hal.lib/wmilib.lib (macro, intrinsic, or later-OS name) | XP-era, for contrast |
| 83 | `ntoskrnl.exe!KeInitializeEvent` | w2k-export | 49: uhcd.sys 4.10.2222 (hint 74); openhci.sys 4.10.2222 (hint 74); usbhub.sys 4.10.2222 (hint 74); +46 more | 16: uhcd.sys 4.90.3000.1 (hint 74); openhci.sys 4.90.3000.1 (hint 74); +14 more [3.3: 7, 3.6: 9] | yes | **evidenced** (stock 98 SE precedent) |  |
| 84 | `ntoskrnl.exe!KeSetEvent` | w2k-export | 43: uhcd.sys 4.10.2222 (hint 8E); openhci.sys 4.10.2222 (hint 8E); usbhub.sys 4.10.2222 (hint 8E); +40 more | 15: uhcd.sys 4.90.3000.1 (hint 8E); openhci.sys 4.90.3000.1 (hint 8E); +13 more [3.3: 6, 3.6: 9] | yes | **evidenced** (stock 98 SE precedent) |  |
| 85 | `ntoskrnl.exe!KeClearEvent` | w2k-export | 6: 1394bus.sys 4.10.2222 (hint 6E); ks.sys 4.10.2222 (hint 6E); msdv.sys 4.10.2222 (hint 6E); +3 more | 1: 1394bus.sys 4.10.2226 (hint 6E) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 86 | `ntoskrnl.exe!KeResetEvent` | w2k-export | 5: usbhub.sys 4.10.2222 (hint 8B); bt829.sys 4.10.2222 (hint 8B); msdv.sys 4.10.2222 (hint 8B); +2 more | 3: usbhub.sys 4.90.3002.1 (hint 1E9); USBHUB20.SYS 5.00.2195.6891 (hint 1F1); +1 more [3.3: 2, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 87 | `ntoskrnl.exe!KeReadStateEvent` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test |  |
| 88 | `ntoskrnl.exe!KeWaitForSingleObject` | w2k-export | 52: uhcd.sys 4.10.2222 (hint 95); openhci.sys 4.10.2222 (hint 95); usbhub.sys 4.10.2222 (hint 95); +49 more | 16: uhcd.sys 4.90.3000.1 (hint 95); openhci.sys 4.90.3000.1 (hint 95); +14 more [3.3: 7, 3.6: 9] | yes | **evidenced** (stock 98 SE precedent) |  |
| 89 | `ntoskrnl.exe!KeWaitForMultipleObjects` | w2k-export | 1: bt829.sys 4.10.2222 (hint 94) | - | yes | **evidenced** (stock 98 SE precedent) |  |
| 90 | `ntoskrnl.exe!KeInitializeTimer` | w2k-export | 21: uhcd.sys 4.10.2222 (hint 78); openhci.sys 4.10.2222 (hint 78); usbhub.sys 4.10.2222 (hint 78); +18 more | 9: uhcd.sys 4.90.3000.1 (hint 78); openhci.sys 4.90.3000.1 (hint 78); +7 more [3.3: 3, 3.6: 6] | no (means nothing) | **evidenced** (stock 98 SE precedent) |  |
| 91 | `ntoskrnl.exe!KeInitializeTimerEx` | w2k-export | 2: ks.sys 4.10.2222 (hint 79); wdmaud.sys 4.10.2222 (hint 79) | - | no (means nothing) | **evidenced** (stock 98 SE precedent) |  |
| 92 | `ntoskrnl.exe!KeSetTimer` | w2k-export | 19: uhcd.sys 4.10.2222 (hint 90); usbhub.sys 4.10.2222 (hint 90); usbd.sys 4.10.2222 (hint 90); +16 more | 8: uhcd.sys 4.90.3000.1 (hint 90); usbhub.sys 4.90.3002.1 (hint 200); +6 more [3.3: 3, 3.6: 5] | no (means nothing) | **evidenced** (stock 98 SE precedent) |  |
| 93 | `ntoskrnl.exe!KeSetTimerEx` | w2k-export | 6: uhcd.sys 4.10.2222 (hint 91); openhci.sys 4.10.2222 (hint 91); ks.sys 4.10.2222 (hint 91); +3 more | 2: uhcd.sys 4.90.3000.1 (hint 91); openhci.sys 4.90.3000.1 (hint 91) [3.3: 0, 3.6: 2] | no (means nothing) | **evidenced** (stock 98 SE precedent) |  |
| 94 | `ntoskrnl.exe!KeCancelTimer` | w2k-export | 21: uhcd.sys 4.10.2222 (hint 6D); openhci.sys 4.10.2222 (hint 6D); usbhub.sys 4.10.2222 (hint 6D); +18 more | 8: uhcd.sys 4.90.3000.1 (hint 6D); openhci.sys 4.90.3000.1 (hint 6D); +6 more [3.3: 2, 3.6: 6] | no (means nothing) | **evidenced** (stock 98 SE precedent) |  |
| 95 | `ntoskrnl.exe!KeReadStateTimer` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | EXTENSION |
| 96 | `ntoskrnl.exe!KeDelayExecutionThread` | w2k-export | 13: uhcd.sys 4.10.2222 (hint 6F); openhci.sys 4.10.2222 (hint 6F); usbhub.sys 4.10.2222 (hint 6F); +10 more | 6: uhcd.sys 4.90.3000.1 (hint 6F); openhci.sys 4.90.3000.1 (hint 6F); +4 more [3.3: 2, 3.6: 4] | yes | **evidenced** (stock 98 SE precedent) |  |
| 97 | `ntoskrnl.exe!KeQuerySystemTime` | w2k-export | 10: uhcd.sys 4.10.2222 (hint 80); openhci.sys 4.10.2222 (hint 80); usbser.sys 4.10.2222 (hint 80); +7 more | 4: uhcd.sys 4.90.3000.1 (hint 80); openhci.sys 4.90.3000.1 (hint 80); +2 more [3.3: 2, 3.6: 2] | no (means nothing) | **evidenced** (stock 98 SE precedent) |  |
| 98 | `ntoskrnl.exe!KeTickCount` | w2k-export | 1: battc.sys 4.10.2222 (hint 93) | 1: usbccgp.sys 5.1.2600.5585 (hint D4) [3.3: 0, 3.6: 1] | no (means nothing) | **evidenced** (stock 98 SE precedent) | KeQueryTickCount is an x86 macro reading this DATA export |
| 99 | `ntoskrnl.exe!KeQueryTickCount` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | the function name, for contrast |
| 100 | `ntoskrnl.exe!KeQueryTimeIncrement` | w2k-export | 6: uhcd.sys 4.10.2222 (hint 81); openhci.sys 4.10.2222 (hint 81); usbhub.sys 4.10.2222 (hint 81); +3 more | 5: uhcd.sys 4.90.3000.1 (hint 81); openhci.sys 4.90.3000.1 (hint 81); +3 more [3.3: 2, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 101 | `ntoskrnl.exe!KeQueryInterruptTime` | w2k-export | 2: acpi.sys 4.10.2222 (hint 7E); compbatt.sys 4.10.2222 (hint 7E) | 1: openhci.sys 4.90.3000.1 (hint 7E) [3.3: 0, 3.6: 1] | no (means nothing) | **evidenced** (stock 98 SE precedent) |  |
| 102 | `HAL.dll!KeQueryPerformanceCounter` | w2k-export | 11: usbser.sys 4.10.2222 (hint 4); ks.sys 4.10.2222 (hint 4); stream.sys 4.10.2222 (hint 4); +8 more | - | no (means nothing) | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 103 | `HAL.dll!KeStallExecutionProcessor` | w2k-export | 16: openhci.sys 4.10.2222 (hint 5); 1394bus.sys 4.10.2222 (hint 5); ohci1394.sys 4.10.2222 (hint 5); +13 more | 5: uhcd.sys 4.90.3000.1 (hint 5); openhci.sys 4.90.3000.1 (hint 5); +3 more [3.3: 1, 3.6: 4] | yes | **evidenced** (stock 98 SE precedent) |  |
| 104 | `ntoskrnl.exe!KeInitializeSemaphore` | w2k-export | 5: usbhub.sys 4.10.2222 (hint 76); usbd.sys 4.10.2222 (hint 76); dbclass.sys 4.10.2222 (hint 18C); +2 more | 4: usbhub.sys 4.90.3002.1 (hint 1C1); usbd.sys 4.90.3000.1 (hint 76); +2 more [3.3: 2, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 105 | `ntoskrnl.exe!KeReleaseSemaphore` | w2k-export | 6: usbhub.sys 4.10.2222 (hint 85); usbd.sys 4.10.2222 (hint 85); ks.sys 4.10.2222 (hint 85); +3 more | 4: usbhub.sys 4.90.3002.1 (hint 1E2); usbd.sys 4.90.3000.1 (hint 85); +2 more [3.3: 2, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 106 | `ntoskrnl.exe!KeInitializeMutex` | w2k-export | 17: ks.sys 4.10.2222 (hint 75); portcls.sys 4.10.2222 (hint 75); acpi.sys 4.10.2222 (hint 75); +14 more | - | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 107 | `ntoskrnl.exe!KeReleaseMutex` | w2k-export | 17: ks.sys 4.10.2222 (hint 84); portcls.sys 4.10.2222 (hint 84); acpi.sys 4.10.2222 (hint 84); +14 more | - | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 108 | `HAL.dll!ExAcquireFastMutex` | w2k-export | 8: usbhub.sys 4.10.2222 (hint 0); hidclass.sys 4.10.2222 (hint 0); ks.sys 4.10.2222 (hint 0); +5 more | 1: usbhub.sys 4.90.3002.1 (hint 0) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 109 | `HAL.dll!ExReleaseFastMutex` | w2k-export | 8: usbhub.sys 4.10.2222 (hint 1); hidclass.sys 4.10.2222 (hint 1); ks.sys 4.10.2222 (hint 1); +5 more | 1: usbhub.sys 4.90.3002.1 (hint 1) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 110 | `ntoskrnl.exe!KeEnterCriticalRegion` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest); wdmstub.sys supplies the name | EXTENSION |
| 111 | `ntoskrnl.exe!KeLeaveCriticalRegion` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest); wdmstub.sys supplies the name | EXTENSION |
| 112 | `ntoskrnl.exe!PsCreateSystemThread` | w2k-export | 1: bt829.sys 4.10.2222 (hint BA) | 1: USBPORT.SYS 5.00.2195.5652 (hint C1) [3.3: 1, 3.6: 0] | yes | **evidenced** (stock 98 SE precedent) |  |
| 113 | `ntoskrnl.exe!PsTerminateSystemThread` | w2k-export | 1: bt829.sys 4.10.2222 (hint BB) | 1: USBPORT.SYS 5.00.2195.5652 (hint C2) [3.3: 1, 3.6: 0] | yes | **evidenced** (stock 98 SE precedent) |  |
| 114 | `ntoskrnl.exe!KeSetPriorityThread` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | EXTENSION |
| 115 | `ntoskrnl.exe!KeGetCurrentThread` | w2k-export | 6: ks.sys 4.10.2222 (hint 71); ntmap.sys 4.10.2222 (hint 177); acpi.sys 4.10.2222 (hint 71); +3 more | 2: NTMAP.SYS 4.10.2227 (hint 71); USBPORT.SYS 5.00.2195.5652 (hint 78) [3.3: 2, 3.6: 0] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 116 | `ntoskrnl.exe!ObReferenceObjectByHandle` | w2k-export | 12: ks.sys 4.10.2222 (hint AA); stream.sys 4.10.2222 (hint AA); portcls.sys 4.10.2222 (hint AA); +9 more | 1: USBPORT.SYS 5.00.2195.5652 (hint B3) [3.3: 1, 3.6: 0] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 117 | `ntoskrnl.exe!ZwClose` | w2k-export | 30: usbhub.sys 4.10.2222 (hint EF); usbd.sys 4.10.2222 (hint EF); hidclass.sys 4.10.2222 (hint EF); +27 more | 11: usbhub.sys 4.90.3002.1 (hint 40A); usbd.sys 4.90.3000.1 (hint EE); +9 more [3.3: 4, 3.6: 7] | yes | **evidenced** (stock 98 SE precedent) |  |
| 118 | `ntoskrnl.exe!ExQueueWorkItem` | w2k-export | 24: uhcd.sys 4.10.2222 (hint 17); usbhub.sys 4.10.2222 (hint 17); 1394bus.sys 4.10.2222 (hint 17); +21 more | 9: uhcd.sys 4.90.3000.1 (hint 17); usbhub.sys 4.90.3002.1 (hint 68); +7 more [3.3: 3, 3.6: 6] | yes | **evidenced** (stock 98 SE precedent) | ExInitializeWorkItem is a macro |
| 119 | `ntoskrnl.exe!IoAllocateWorkItem` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test; wdmstub.sys supplies the name |  |
| 120 | `ntoskrnl.exe!IoQueueWorkItem` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test; wdmstub.sys supplies the name |  |
| 121 | `ntoskrnl.exe!IoFreeWorkItem` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test; wdmstub.sys supplies the name |  |
| 122 | `ntoskrnl.exe!KeBugCheckEx` | w2k-export | 2: hidclass.sys 4.10.2222 (hint 6C); ks.sys 4.10.2222 (hint 6C) | 3: usbccgp.sys 5.1.2600.5585 (hint A6); USBPORT.SYS 5.00.2195.5652 (hint 73); +1 more [3.3: 1, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 123 | `HAL.dll!HalGetAdapter` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 124 | `ntoskrnl.exe!IoGetDmaAdapter` | w2k-export | 7: uhcd.sys 4.10.2222 (hint 4A); openhci.sys 4.10.2222 (hint 4A); ohci1394.sys 4.10.2222 (hint 4A); +4 more | 4: uhcd.sys 4.90.3000.1 (hint 4A); openhci.sys 4.90.3000.1 (hint 4A); +2 more [3.3: 1, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 125 | `HAL.dll!HalAllocateCommonBuffer` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | HAL import under ntddk.h x86; __inline over DmaOperations under wdm.h |
| 126 | `HAL.dll!HalFreeCommonBuffer` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | as above |
| 127 | `ntoskrnl.exe!IoAllocateMdl` | w2k-export | 12: usbd.sys 4.10.2222 (hint 2E); 1394bus.sys 4.10.2222 (hint 2E); ohci1394.sys 4.10.2222 (hint 2E); +9 more | 8: usbd.sys 4.90.3000.1 (hint 2E); 1394bus.sys 4.10.2226 (hint 2E); +6 more [3.3: 4, 3.6: 4] | yes | **evidenced** (stock 98 SE precedent) |  |
| 128 | `ntoskrnl.exe!IoFreeMdl` | w2k-export | 15: usbd.sys 4.10.2222 (hint 43); 1394bus.sys 4.10.2222 (hint 43); ohci1394.sys 4.10.2222 (hint 43); +12 more | 8: usbd.sys 4.90.3000.1 (hint 43); 1394bus.sys 4.10.2226 (hint 43); +6 more [3.3: 4, 3.6: 4] | yes | **evidenced** (stock 98 SE precedent) |  |
| 129 | `ntoskrnl.exe!IoBuildPartialMdl` | w2k-export | 2: 1394bus.sys 4.10.2222 (hint 33); sbp2port.sys 4.10.2222 (hint 33) | 3: 1394bus.sys 4.10.2226 (hint 33); SBP2PORT.SYS 4.10.2227 (hint 33); +1 more [3.3: 1, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 130 | `ntoskrnl.exe!MmBuildMdlForNonPagedPool` | w2k-export | 7: usbd.sys 4.10.2222 (hint 99); 1394bus.sys 4.10.2222 (hint 99); stream.sys 4.10.2222 (hint 99); +4 more | 7: usbd.sys 4.90.3000.1 (hint 99); 1394bus.sys 4.10.2226 (hint 99); +5 more [3.3: 4, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 131 | `ntoskrnl.exe!MmProbeAndLockPages` | w2k-export | 4: ks.sys 4.10.2222 (hint 9F); dmusic.sys 4.10.2222 (hint 9F); kmixer.sys 4.10.2222 (hint 9F); +1 more | - | yes | **evidenced** (stock 98 SE precedent) |  |
| 132 | `ntoskrnl.exe!MmUnlockPages` | w2k-export | 8: ks.sys 4.10.2222 (hint A5); portcls.sys 4.10.2222 (hint A5); ntmap.sys 4.10.2222 (hint 214); +5 more | 1: NTMAP.SYS 4.10.2227 (hint A5) [3.3: 1, 3.6: 0] | yes | **evidenced** (stock 98 SE precedent) |  |
| 133 | `ntoskrnl.exe!MmGetPhysicalAddress` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 134 | `ntoskrnl.exe!MmMapLockedPages` | w2k-export | 16: uhcd.sys 4.10.2222 (hint 9D); openhci.sys 4.10.2222 (hint 9D); hidclass.sys 4.10.2222 (hint 9D); +13 more | 6: uhcd.sys 4.90.3000.1 (hint 9D); openhci.sys 4.90.3000.1 (hint 9D); +4 more [3.3: 1, 3.6: 5] | yes | **evidenced** (stock 98 SE precedent) | MmGetSystemAddressForMdl macro |
| 135 | `ntoskrnl.exe!MmMapLockedPagesSpecifyCache` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test; wdmstub.sys supplies the name | MmGetSystemAddressForMdlSafe macro |
| 136 | `ntoskrnl.exe!MmUnmapLockedPages` | w2k-export | 5: ohci1394.sys 4.10.2222 (hint A7); aha894x.sys 4.10.1998 (hint A0); kmixer.sys 4.10.2222 (hint A7); +2 more | 1: OHCI1394.SYS 4.10.2228 (hint A7) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 137 | `ntoskrnl.exe!MmMapIoSpace` | w2k-export | 8: uhcd.sys 4.10.2222 (hint 9C); openhci.sys 4.10.2222 (hint 9C); ohci1394.sys 4.10.2222 (hint 9C); +5 more | 4: uhcd.sys 4.90.3000.1 (hint 9C); openhci.sys 4.90.3000.1 (hint 9C); +2 more [3.3: 1, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 138 | `ntoskrnl.exe!MmUnmapIoSpace` | w2k-export | 6: ohci1394.sys 4.10.2222 (hint A6); stream.sys 4.10.2222 (hint A6); acpi.sys 4.10.2222 (hint A6); +3 more | 1: OHCI1394.SYS 4.10.2228 (hint A6) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 139 | `ntoskrnl.exe!MmAllocateContiguousMemory` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 140 | `ntoskrnl.exe!MmAllocateContiguousMemorySpecifyCache` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | ntddk.h only |
| 141 | `ntoskrnl.exe!MmFreeContiguousMemory` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 142 | `ntoskrnl.exe!MmIsAddressValid` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test |  |
| 143 | `ntoskrnl.exe!MmGetSystemRoutineAddress` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test; wdmstub.sys supplies the name | XP-era/98-missing, for contrast |
| 144 | `HAL.dll!IoMapTransfer` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 145 | `HAL.dll!IoFlushAdapterBuffers` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 146 | `HAL.dll!IoFreeMapRegisters` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 147 | `ntoskrnl.exe!IoAllocateAdapterChannel` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 148 | `HAL.dll!IoFreeAdapterChannel` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | EXTENSION |
| 149 | `HAL.dll!HalAllocateAdapterChannel` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | EXTENSION |
| 150 | `HAL.dll!HalTranslateBusAddress` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest); wdmstub.sys supplies the name |  |
| 151 | `HAL.dll!HalGetBusData` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | EXTENSION; PCI config by HAL |
| 152 | `HAL.dll!HalGetBusDataByOffset` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | EXTENSION |
| 153 | `HAL.dll!HalSetBusData` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | EXTENSION |
| 154 | `HAL.dll!HalSetBusDataByOffset` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | EXTENSION |
| 155 | `ntoskrnl.exe!MmLockPagableDataSection` | w2k-export | 3: usbser.sys 4.10.2222 (hint 9B); acpi.sys 4.10.2222 (hint 9B); ec.sys 4.10.1998 (hint 94) | - | no (means nothing) | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 156 | `ntoskrnl.exe!MmUnlockPagableImageSection` | w2k-export | 3: usbser.sys 4.10.2222 (hint A4); acpi.sys 4.10.2222 (hint A4); ec.sys 4.10.1998 (hint 9D) | - | no (means nothing) | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 157 | `ntoskrnl.exe!ZwOpenKey` | w2k-export | 12: hidclass.sys 4.10.2222 (hint F8); ks.sys 4.10.2222 (hint F8); stream.sys 4.10.2222 (hint F8); +9 more | 2: usbd.sys 4.90.3000.1 (hint F7); wdmstub.sys 5.00.006 (hint 439) [3.3: 0, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) |  |
| 158 | `ntoskrnl.exe!ZwQueryValueKey` | w2k-export | 24: usbd.sys 4.10.2222 (hint FD); usbser.sys 4.10.2222 (hint FD); 1394bus.sys 4.10.2222 (hint FD); +21 more | 8: usbd.sys 4.90.3000.1 (hint FC); 1394bus.sys 4.10.2226 (hint FD); +6 more [3.3: 2, 3.6: 6] | yes | **evidenced** (stock 98 SE precedent) |  |
| 159 | `ntoskrnl.exe!ZwSetValueKey` | w2k-export | 13: usbhub.sys 4.10.2222 (hint FF); usbd.sys 4.10.2222 (hint FF); stream.sys 4.10.2222 (hint FF); +10 more | 5: usbhub.sys 4.90.3002.1 (hint 458); usbd.sys 4.90.3000.1 (hint FE); +3 more [3.3: 2, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 160 | `ntoskrnl.exe!ZwCreateKey` | w2k-export | 4: ks.sys 4.10.2222 (hint F2); portcls.sys 4.10.2222 (hint F2); sonydcam.sys 4.10.2222 (hint F2); +1 more | - | yes | **evidenced** (stock 98 SE precedent) |  |
| 161 | `ntoskrnl.exe!ZwDeleteValueKey` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | EXTENSION |
| 162 | `ntoskrnl.exe!ZwEnumerateKey` | w2k-export | 4: ks.sys 4.10.2222 (hint F4); stream.sys 4.10.2222 (hint F4); portcls.sys 4.10.2222 (hint F4); +1 more | - | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 163 | `ntoskrnl.exe!RtlInitUnicodeString` | w2k-export | 48: usbhub.sys 4.10.2222 (hint D7); usbd.sys 4.10.2222 (hint D7); hidclass.sys 4.10.2222 (hint D7); +45 more | 12: usbhub.sys 4.90.3002.1 (hint 35E); usbd.sys 4.90.3000.1 (hint D6); +10 more [3.3: 5, 3.6: 7] | yes | **evidenced** (stock 98 SE precedent) |  |
| 164 | `ntoskrnl.exe!RtlCopyUnicodeString` | w2k-export | 7: ohci1394.sys 4.10.2222 (hint CB); ks.sys 4.10.2222 (hint CB); aha894x.sys 4.10.1998 (hint C4); +4 more | 1: OHCI1394.SYS 4.10.2228 (hint CB) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 165 | `ntoskrnl.exe!RtlAppendUnicodeToString` | w2k-export | 8: usbser.sys 4.10.2222 (hint C4); 1394bus.sys 4.10.2222 (hint C4); ohci1394.sys 4.10.2222 (hint C4); +5 more | 2: 1394bus.sys 4.10.2226 (hint C4); OHCI1394.SYS 4.10.2228 (hint C4) [3.3: 0, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) |  |
| 166 | `ntoskrnl.exe!RtlAppendUnicodeStringToString` | w2k-export | 10: usbd.sys 4.10.2222 (hint C3); usbser.sys 4.10.2222 (hint C3); 1394bus.sys 4.10.2222 (hint C3); +7 more | 6: usbhub.sys 4.90.3002.1 (hint 2F7); usbd.sys 4.90.3000.1 (hint C2); +4 more [3.3: 2, 3.6: 4] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 167 | `ntoskrnl.exe!RtlIntegerToUnicodeString` | w2k-export | 12: usbhub.sys 4.10.2222 (hint D8); usbd.sys 4.10.2222 (hint D8); 1394bus.sys 4.10.2222 (hint D8); +9 more | 6: usbhub.sys 4.90.3002.1 (hint 368); usbd.sys 4.90.3000.1 (hint D7); +4 more [3.3: 2, 3.6: 4] | yes | **evidenced** (stock 98 SE precedent) |  |
| 168 | `ntoskrnl.exe!RtlUnicodeStringToInteger` | w2k-export | 5: atitunep.sys 4.10.2222 (hint E2); atixbar.sys 4.10.1998 (hint D9); bt829.sys 4.10.2222 (hint E2); +2 more | - | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 169 | `ntoskrnl.exe!RtlCompareMemory` | w2k-export | 10: usbd.sys 4.10.2222 (hint C6); ntmap.sys 4.10.2222 (hint 2BF); acpi.sys 4.10.2222 (hint C6); +7 more | 5: usbhub.sys 4.90.3002.1 (hint 304); usbd.sys 4.90.3000.1 (hint C5); +3 more [3.3: 2, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) |  |
| 170 | `ntoskrnl.exe!RtlQueryRegistryValues` | w2k-export | 9: uhcd.sys 4.10.2222 (hint DA); usbhub.sys 4.10.2222 (hint DA); usbd.sys 4.10.2222 (hint DA); +6 more | 7: uhcd.sys 4.90.3000.1 (hint D9); usbhub.sys 4.90.3002.1 (hint 38E); +5 more [3.3: 3, 3.6: 4] | yes | **evidenced** (stock 98 SE precedent) |  |
| 171 | `ntoskrnl.exe!RtlWriteRegistryValue` | w2k-export | 3: usbhub.sys 4.10.2222 (hint E5); usbser.sys 4.10.2222 (hint E5); usbintel.sys 4.10.1998 (hint DC) | 1: usbhub.sys 4.90.3002.1 (hint 3C9) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 172 | `ntoskrnl.exe!RtlFreeUnicodeString` | w2k-export | 23: uhcd.sys 4.10.2222 (hint D3); openhci.sys 4.10.2222 (hint D3); usbhub.sys 4.10.2222 (hint D3); +20 more | 10: uhcd.sys 4.90.3000.1 (hint D2); openhci.sys 4.90.3000.1 (hint D2); +8 more [3.3: 4, 3.6: 6] | yes | **evidenced** (stock 98 SE precedent) |  |
| 173 | `ntoskrnl.exe!RtlInitAnsiString` | w2k-export | 14: usbser.sys 4.10.2222 (hint D5); 1394bus.sys 4.10.2222 (hint D5); stream.sys 4.10.2222 (hint D5); +11 more | 3: 1394bus.sys 4.10.2226 (hint D5); SBP2PORT.SYS 4.10.2227 (hint D5); +1 more [3.3: 1, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 174 | `ntoskrnl.exe!RtlAnsiStringToUnicodeString` | w2k-export | 14: usbser.sys 4.10.2222 (hint C2); 1394bus.sys 4.10.2222 (hint C2); stream.sys 4.10.2222 (hint C2); +11 more | 3: 1394bus.sys 4.10.2226 (hint C2); SBP2PORT.SYS 4.10.2227 (hint C2); +1 more [3.3: 1, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 175 | `ntoskrnl.exe!RtlCompareUnicodeString` | w2k-export | 2: ks.sys 4.10.2222 (hint C7); compbatt.sys 4.10.2222 (hint C7) | - | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 176 | `ntoskrnl.exe!RtlEqualUnicodeString` | w2k-export | 2: bt829.sys 4.10.2222 (hint CD); sysaudio.sys 4.10.2222 (hint CD) | 1: USBNTMAP.SYS 4.90.3000 (hint CD) [3.3: 1, 3.6: 0] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 177 | `ntoskrnl.exe!swprintf` | w2k-export | 9: usbhub.sys 4.10.2222 (hint 131); hidclass.sys 4.10.2222 (hint 131); ks.sys 4.10.2222 (hint 131); +6 more | 4: usbhub.sys 4.90.3002.1 (hint 49F); SBP2PORT.SYS 4.10.2227 (hint 131); +2 more [3.3: 1, 3.6: 3] | yes | **evidenced** (stock 98 SE precedent) | CRT export in ntoskrnl |
| 178 | `ntoskrnl.exe!_snwprintf` | w2k-export | - | 1: wdmstub.sys 5.00.006 (hint 481) [3.3: 0, 3.6: 1] | yes | **evidenced** (ntkern-name only - module-blind; weakest) | CRT export in ntoskrnl |
| 179 | `ntoskrnl.exe!sprintf` | w2k-export | 10: usbser.sys 4.10.2222 (hint 124); stream.sys 4.10.2222 (hint 124); acpi.sys 4.10.2222 (hint 124); +7 more | 2: SBP2PORT.SYS 4.10.2227 (hint 124); USBSTOR.SYS 4.90.3000.1 (hint 492) [3.3: 1, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 180 | `ntoskrnl.exe!_snprintf` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | EXTENSION |
| 181 | `ntoskrnl.exe!memcpy` | w2k-export | 2: ccport.sys 4.10.2222 (hint 11F); hidparse.sys 4.10.2222 (hint 40B) | 2: usbccgp.sys 5.1.2600.5585 (hint 196); wdmstub.sys 5.00.006 (hint 49B) [3.3: 0, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | RtlCopyMemory macro; an import only when not expanded intrinsically |
| 182 | `ntoskrnl.exe!memset` | w2k-export | 2: ccport.sys 4.10.2222 (hint 121); hidparse.sys 4.10.2222 (hint 40D) | 2: usbccgp.sys 5.1.2600.5585 (hint 198); wdmstub.sys 5.00.006 (hint 49D) [3.3: 0, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | RtlZeroMemory/RtlFillMemory macro; same |
| 183 | `ntoskrnl.exe!memmove` | w2k-export | 6: usbhub.sys 4.10.2222 (hint 120); 1394bus.sys 4.10.2222 (hint 120); ks.sys 4.10.2222 (hint 120); +3 more | 4: usbhub.sys 4.90.3002.1 (hint 48E); 1394bus.sys 4.10.2226 (hint 120); +2 more [3.3: 2, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) | RtlMoveMemory macro; never an intrinsic |
| 184 | `(no DDK lib)!memcmp` | not in any DDK import lib; exported by:  | - | - | no (means nothing) | no import: not in ntoskrnl.lib/hal.lib/wmilib.lib (macro, intrinsic, or later-OS name) | RtlEqualMemory macro; same |
| 185 | `ntoskrnl.exe!READ_REGISTER_ULONG` | w2k-export | 4: openhci.sys 4.10.2222 (hint C0); ohci1394.sys 4.10.2222 (hint C0); aha894x.sys 4.10.1998 (hint B9); +1 more | 3: openhci.sys 4.90.3000.1 (hint BF); OHCI1394.SYS 4.10.2228 (hint C0); +1 more [3.3: 1, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) |  |
| 186 | `ntoskrnl.exe!READ_REGISTER_USHORT` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 187 | `ntoskrnl.exe!READ_REGISTER_UCHAR` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 188 | `ntoskrnl.exe!WRITE_REGISTER_ULONG` | w2k-export | 4: openhci.sys 4.10.2222 (hint ED); ohci1394.sys 4.10.2222 (hint ED); aha894x.sys 4.10.1998 (hint E2); +1 more | 3: openhci.sys 4.90.3000.1 (hint EC); OHCI1394.SYS 4.10.2228 (hint ED); +1 more [3.3: 1, 3.6: 2] | yes | **evidenced** (stock 98 SE precedent) |  |
| 189 | `ntoskrnl.exe!WRITE_REGISTER_USHORT` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 190 | `ntoskrnl.exe!WRITE_REGISTER_UCHAR` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) |  |
| 191 | `ntoskrnl.exe!READ_REGISTER_BUFFER_ULONG` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | EXTENSION |
| 192 | `HAL.dll!READ_PORT_UCHAR` | w2k-export | 11: uhcd.sys 4.10.2222 (hint D); portcls.sys 4.10.2222 (hint D); acpi.sys 4.10.2222 (hint D); +8 more | 1: uhcd.sys 4.90.3000.1 (hint D) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 193 | `HAL.dll!READ_PORT_USHORT` | w2k-export | 2: uhcd.sys 4.10.2222 (hint F); acpi.sys 4.10.2222 (hint F) | 1: uhcd.sys 4.90.3000.1 (hint F) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 194 | `HAL.dll!READ_PORT_ULONG` | w2k-export | 2: uhcd.sys 4.10.2222 (hint E); acpi.sys 4.10.2222 (hint E) | 1: uhcd.sys 4.90.3000.1 (hint E) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 195 | `HAL.dll!WRITE_PORT_UCHAR` | w2k-export | 11: uhcd.sys 4.10.2222 (hint 13); portcls.sys 4.10.2222 (hint 13); acpi.sys 4.10.2222 (hint 13); +8 more | 1: uhcd.sys 4.90.3000.1 (hint 13) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 196 | `HAL.dll!WRITE_PORT_USHORT` | w2k-export | 3: uhcd.sys 4.10.2222 (hint 15); acpi.sys 4.10.2222 (hint 15); opl3sax.sys 4.10.2222 (hint 15) | 1: uhcd.sys 4.90.3000.1 (hint 15) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 197 | `HAL.dll!WRITE_PORT_ULONG` | w2k-export | 2: uhcd.sys 4.10.2222 (hint 14); acpi.sys 4.10.2222 (hint 14) | 1: uhcd.sys 4.90.3000.1 (hint 14) [3.3: 0, 3.6: 1] | yes | **evidenced** (stock 98 SE precedent) |  |
| 198 | `ntoskrnl.exe!DbgPrint` | w2k-export | 2: ks.sys 4.10.2222 (hint 1); usbdiag.sys 4.10.1998 (hint 1) | - | yes | **evidenced** (stock 98 SE precedent) |  |
| 199 | `ntoskrnl.exe!DbgBreakPoint` | w2k-export | 3: ks.sys 4.10.2222 (hint 0); stream.sys 4.10.2222 (hint 0); compbatt.sys 4.10.2222 (hint 0) | 1: USBPORT.SYS 5.00.2195.5652 (hint 0) [3.3: 1, 3.6: 0] | yes | **evidenced** (stock 98 SE precedent) | EXTENSION |
| 200 | `ntoskrnl.exe!KeBugCheck` | w2k-export | - | - | no (means nothing) | w2k only - needs a Win98 tag or a load test | EXTENSION |
| 201 | `ntoskrnl.exe!_allmul` | w2k-export | 3: ccdecode.sys 4.10.2222 (hint 3E4); nabtsfec.sys 4.10.1998 (hint 3E4); tosdvd.sys 4.10.1998 (hint 3E4) | - | yes | **evidenced** (stock 98 SE precedent) | EXTENSION; compiler helper |
| 202 | `ntoskrnl.exe!_aulldiv` | w2k-export | 3: ccdecode.sys 4.10.2222 (hint 3E8); nabtsfec.sys 4.10.1998 (hint 3E8); tosdvd.sys 4.10.1998 (hint 3E8) | - | yes | **evidenced** (stock 98 SE precedent) | EXTENSION; compiler helper |
| 203 | `ntoskrnl.exe!_alldiv` | w2k-export | 1: tosdvd.sys 4.10.1998 (hint 3E3) | - | yes | **evidenced** (stock 98 SE precedent) | EXTENSION; compiler helper |
| 204 | `ntoskrnl.exe!_allshl` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | EXTENSION; compiler helper |
| 205 | `ntoskrnl.exe!_aullshr` | w2k-export | - | - | yes | **evidenced** (ntkern-name only - module-blind; weakest) | EXTENSION; compiler helper |

### A.1 The evidence-list rows the table rests on

Rows for `scripts\import-gate\win98-evidence.list`, in the gate's format
(`RELATIVE\PATH VERSION LENGTH SHA256 note`). The reading parsed them with the
gate's own `Read-Win98EvidenceManifest` and authenticated them with
`Get-FileIdentityErrors`: 0 errors. They name files in the git-ignored
`tools\win98se-extracted\wdm\` (67 files the reading staged from `Win98SE.iso`,
656,203,776 bytes, SHA-256
`C048C31E414E3FE4FEC1CDBBA42C62452F7DF5E9F3AFD62464823DE4DEF7F584`; each
length agrees with its `layout*.inf` entry from `PRECOPY1.CAB`) and in
`tools\nusb36-extracted\`.

- **Tier A, to add**: the two Windows 98 SE USB 1.1 host controller drivers,
  the closest precedent an HCD has.
- **Tier B, to add**: the only stock precedent for a pair the HCD may need:
  `ks.sys` (`KeRemoveQueueDpc`, `ExInitializeNPagedLookasideList` /
  `ExDeleteNPagedLookasideList`, `DbgPrint`, `ZwCreateKey`,
  `KeInitializeTimerEx`, `MmProbeAndLockPages`, `ObReferenceObjectByHandle`);
  `hidclass.sys` (`InterlockedCompareExchange`, `KeBugCheckEx`);
  `ohci1394.sys` and `sbp2port.sys` (the SList pairs, `IoBuildPartialMdl`,
  `IoIsWdmVersionAvailable`, `PoRegisterDeviceForIdleDetection`,
  `MmUnmapIoSpace`, `MmUnmapLockedPages`); `acpi.sys`
  (`KeQueryInterruptTime`, `MmLockPagableDataSection`); `ccport.sys`
  (`memcpy` / `memset`, `IoBuildSynchronousFsdRequest`); `battc.sys`
  (`KeTickCount`); `bt829.sys` (`KeWaitForMultipleObjects`,
  `PsCreateSystemThread` / `PsTerminateSystemThread`); `wdmaud.sys`
  (`IoGetAttachedDeviceReference`, `IoRegisterPlugPlayNotification` /
  `IoUnregisterPlugPlayNotification`).
- **Tier C, optional**: every other staged Windows 98 SE WDM driver. With
  tiers A and B listed, the only candidate pairs left without a gate-visible
  stock precedent are `IoGetDeviceObjectPointer` (`1394bus.sys`) and the
  compiler helpers `_allmul` / `_aulldiv` / `_alldiv` (`ccdecode.sys`,
  `nabtsfec.sys`, `tosdvd.sys`), all tier C. Each listed file costs one
  `dumpbin /imports` per gate run.
- **NUSB 3.6**: the nine files not in the listed NUSB 3.3 set. The reading
  gave them no tier; whether they are listed as a tier of their own or not at
  all, given `wdmstub.sys`, is the owner's call (section 7.7).

The `ntmap.sys` row (tier C) is the SE CD's 4.10.2222, 9,360 bytes, a
different file from NUSB's listed `NTMAP.SYS` 4.10.2227.

```
[precedent]

# --- Windows 98 SE CD, the USB 1.1 host controller drivers (task 25.3) ---
# Extracted by 7-Zip from the SE CD's win98\*.CAB set (Win98SE.iso SHA-256
# C048C31E...F7F584); each length agrees with the layout*.inf entry quoted.
# The closest precedent an HCD has: IoGetDmaAdapter, IoConnectInterrupt,
# KeSynchronizeExecution, MmMapIoSpace, pool, DPCs and timers.
tools\win98se-extracted\wdm\openhci.sys        4.10.2222        23632 6E3D33A8868CE146F37EE24556692E477928E57E2B4FE5D4A865DB6200CE401D Win98 SE CD - MINI NT Universal HCD for USB; layout.inf "openhci.sys=5,,23632"
tools\win98se-extracted\wdm\uhcd.sys           4.10.2222        30448 7457D69EBC5D7ED456C8401B19C218A21FE16ADC8BC8E3D0D2C744785654554B Win98 SE CD - Universal Host Controller Driver; layout.inf "uhcd.sys=5,,30448"

# --- Windows 98 SE CD, class/bus drivers that are the only stock precedent ---
# for a pair the HCD may need (design record 13, Appendix A.1, lists which pair each covers).
tools\win98se-extracted\wdm\acpi.sys           4.10.2222        83136 3DDCACEC1D9D6580415666DC839E257E8DBE763719061952BCF665514254A338 Win98 SE CD - ACPI.SYS miniport; layout.inf "acpi.sys=5,,83136"
tools\win98se-extracted\wdm\battc.sys          4.10.2222         6432 792041B3EDF7A5CFD807DB64A474399AD47FF9F48896D33448252397AC79329B Win98 SE CD - Battery Class Driver; layout.inf "battc.sys=5,,6432"
tools\win98se-extracted\wdm\bt829.sys          4.10.2222        37776 ED37E6A6685D4EA9305FF7D5EF6E64CD7CA528406A1AF5121F3A7D319FF8E02D Win98 SE CD - WDM Video Capture Driver; layout.inf "bt829.sys=20,,37776"
tools\win98se-extracted\wdm\ccport.sys         4.10.2222        31680 0D09CE27EA50767FE9213D3EC17D70D92A1F2B0FB98CDCBF29F488B216F3667B Win98 SE CD - WDM Modem Port Driver; layout2.inf "ccport.sys=5,,31680"
tools\win98se-extracted\wdm\hidclass.sys       4.10.2222        23520 2432B4D46C3D9291B49F47FC035DA182150D41CAB46D938DF5924F4603BBD9DF Win98 SE CD - HID Class Driver; layout.inf "hidclass.sys=5,,23520"
tools\win98se-extracted\wdm\ks.sys             4.10.2222        98432 3548733A21060B88D6CAB8DF822D0F68C0B98F6B9C207A8EC63BDCA76F67A35C Win98 SE CD - Kernel CSA Library; layout.inf "ks.sys=5,,98432"
tools\win98se-extracted\wdm\ohci1394.sys       4.10.2222        35648 37F690E9135360B8082C12E8E4E9BF9A08D33D1FE877AA523C7D1BB46F30E365 Win98 SE CD - 1394 OpenHci Port Driver; layout.inf "ohci1394.sys=5,,35648"
tools\win98se-extracted\wdm\sbp2port.sys       4.10.2222        32592 14CC37722AF4690E34110A2C1371EDB3A54241F137F1C92E1E3E2BC4E2121E46 Win98 SE CD - 1394 Bus Device Driver; layout2.inf "sbp2port.sys=5,,32592"
tools\win98se-extracted\wdm\wdmaud.sys         4.10.2222        68096 014CA5DF60B6840FCFEFB8B83FCD611B5C8C6F93BE77615E759AD2AAE6B424F4 Win98 SE CD - MMSYSTEM Wave/Midi API mapper; layout.inf "wdmaud.sys=20,,68096"

# --- Windows 98 SE CD, the remaining Microsoft WDM drivers (optional) ---
tools\win98se-extracted\wdm\1394bus.sys        4.10.2222        37328 F1CCA2D7B7A07786CD002C66EA452C1D0A33BCD8774C4C5BEC2A447927128803 Win98 SE CD - 1394 Bus Device Driver; layout.inf "1394bus.sys=5,,37328"
tools\win98se-extracted\wdm\aha894x.sys        4.10.1998        33344 0152BAE2152156764F1550164E66E5EF5661D46174DCF56A41E7B9B883CF787A Win98 SE CD - Adaptec AHA-894x 1394 Port Driver (JSG076); layout.inf "aha894x.sys=19,,33344"
tools\win98se-extracted\wdm\apmbatt.sys        4.10.1998         4224 5E86469883D5EF01E4000401D7676DF267E75AFB107E667CEF6619D5242F2F8D Win98 SE CD - apmbatt.SYS miniport; layout.inf "apmbatt.sys=5,,4224"
tools\win98se-extracted\wdm\atitunep.sys       4.10.2222        11200 DD7A8E80A7A804ABF655ADF31DD28098B185F9EA29F44B7BDA7C03572BDDB502 Win98 SE CD - ATI WDM TVTuner MiniDriver; layout.inf "atitunep.sys=19,,11200"
tools\win98se-extracted\wdm\atixbar.sys        4.10.1998        12000 644D3DF33EE803EE1A3280D91C431177B9DD3564792867AEC08F3E48F79B6C9D Win98 SE CD - ATI WDM Video/Audio CrossBar MiniDriver; layout.inf "atixbar.sys=19,,12000"
tools\win98se-extracted\wdm\aztw2316.sys       4.10.2222        34400 686A11C7425DB4069F052AEA7399BC652926C684E62CAB53B973F83BE36D0CA5 Win98 SE CD - Aztech 2316 Audio driver driver; layout.inf "aztw2316.sys=19,,34400"
tools\win98se-extracted\wdm\aztw2320.sys       4.10.2222        35616 856C41CE9E9A10F0CC012902B0A13276CED2E8FBBB9B3ECCC8D3BC816F78A1EC Win98 SE CD - Aztech 2316 Audio driver driver; layout.inf "aztw2320.sys=20,,35616"
tools\win98se-extracted\wdm\ccdecode.sys       4.10.2222        11264 3622BB85F4EBEEF73C0E2F69EF17DFEDFB4A291B69A3992EE66F737385FAAAEB Win98 SE CD - WDM Codec Driver; layout.inf "ccdecode.sys=20,,11264"
tools\win98se-extracted\wdm\cmbatt.sys         4.10.2222         8928 EE8C5B9ABDBAD8E86BDEA0F15C7ABAA953EC281893E31657A53EEDC02336335E Win98 SE CD - cmbatt.SYS miniport; layout.inf "cmbatt.sys=5,,8928"
tools\win98se-extracted\wdm\compbatt.sys       4.10.2222         7808 A8440E8004F836616BB03658CA3EE7ABEF35BDE1E0AC60C7B3269295341150FF Win98 SE CD - compbatt.SYS miniport; layout.inf "compbatt.sys=5,,7808"
tools\win98se-extracted\wdm\cwbase.sys         4.10.2222         2480 B2449E7F3AB60E0006C694F528F43B0F60256F9AF1A2FC7CC1D43184EFBFB2B8 Win98 SE CD - Crystal WDM CS423XB Audio Driver; layout.inf "cwbase.sys=20,,2480"
tools\win98se-extracted\wdm\cwbmidi.sys        4.10.2222         7424 81F2C78ACB2AF9F37722D00D71E670B60D429E125A0D4C855C6C4D55C63ED13E Win98 SE CD - Crystal WDM CS423XB Audio Driver; layout.inf "cwbmidi.sys=20,,7424"
tools\win98se-extracted\wdm\cwbwdm.sys         4.10.2222        82528 BA78B091A53F9E29CA7FACEC124F0980A01042DF24F3C7E91EC8ED3DA3FE4256 Win98 SE CD - Crystal WDM CS423XB Audio Driver; layout.inf "cwbwdm.sys=20,,82528"
tools\win98se-extracted\wdm\dbcacpi.sys        4.10.2222         8096 56CDEA329C8C7D176535BE440A88AA0BBA04BB552785C5C338287B7CB24D1FD6 Win98 SE CD - MINI NT ACPI Driver; layout2.inf "dbcacpi.sys=20,,8096"
tools\win98se-extracted\wdm\dbclass.sys        4.10.2222        13488 1AACF95A6D5864D783D6273AE5E25B09A73FEF16F2AA32D5F390282AAA995D63 Win98 SE CD - MINI NT ACPI Driver; layout2.inf "dbclass.sys=5,,13488"
tools\win98se-extracted\wdm\dbcusb.sys         4.10.2222         7184 1091E82660221301E500CE1D98F1E76DFB82DFB0922CDE2069AD0B2779F48179 Win98 SE CD - MINI NT USB Driver; layout.inf "dbcusb.sys=20,,7184"
tools\win98se-extracted\wdm\dbfilter.sys       4.10.2222         2368 033FE3913E316264C8A54799925993C2D2E71A90A70E9B87B45090605829CE82 Win98 SE CD - MINI NT ACPI Driver; layout2.inf "dbfilter.sys=5,,2368"
tools\win98se-extracted\wdm\dmusic.sys         4.10.2222        60416 FE344CBA4452D08B453BE0DAB8795D182F7F2E5EF5CB2CE2318398949CECDE97 Win98 SE CD - Microsoft DMUSIC Device; layout.inf "dmusic.sys=20,,60416"
tools\win98se-extracted\wdm\ec.sys             4.10.1998         8752 00AAF791DA655C478DFDDD9F7A83D2CC5F3F19F88E6E0E1B108528F458F0B496 Win98 SE CD - ec.SYS miniport; layout.inf "ec.sys=5,,8752"
tools\win98se-extracted\wdm\ess.sys            4.10.2222        69632 50AD93971E37957D1BE8EC9DD6CE1A065491AD508F4D077385ED3DF5A52D0550 Win98 SE CD - ESS WDM Audio Driver; layout.inf "ess.sys=20,,69632"
tools\win98se-extracted\wdm\hidparse.sys       4.10.2222        44368 8EB2B9E967D3576A8AA44E946E36A3AE7CCBEB514F7805CEB77199B4CFA8F8DF Win98 SE CD - MINI NT HID PARSER; layout.inf "hidparse.sys=5,,44368"
tools\win98se-extracted\wdm\hidusb.sys         4.10.2222         9296 307CD8D8C1E90CA89DF1C4E23A2CB459E9E700E8C5561B7D9FD3B20711CD0196 Win98 SE CD - USB Miniport Driver for Input Devices; layout.inf "hidusb.sys=5,,9296"
tools\win98se-extracted\wdm\hidvkd.sys         4.10.1998         5088 43125EA15A13A999C371A6145EA1EBF67754C723CFE46B9B5C849187F89F4849 Win98 SE CD - HID Miniport Driver for legacy keyboard System Control Buttons; layout.inf "hidvkd.sys=5,,5088"
tools\win98se-extracted\wdm\kmixer.sys         4.10.2222       137120 4CAEB7D39AAB56CE3FEC0DA24A654C1567602F05756DA3AC89355B45A1A98AC0 Win98 SE CD - Kernel Mode Audio Mixer; layout.inf "kmixer.sys=20,,137120"
tools\win98se-extracted\wdm\msdv.sys           4.10.2222        51616 C18A00D2E3DE41EB9F7F13EA99C16826572AE83886C328DB05AE8A4CF45A58F6 Win98 SE CD - 1394 DVCR Driver; layout.inf "msdv.sys=20,,51616"
tools\win98se-extracted\wdm\mskssrv.sys        4.10.2222         6304 2A9F8D735C0B905FD9B19D41AFC0451F6757BB04A86041EBC713826BC4DDC404 Win98 SE CD - MS KS Server; layout.inf "mskssrv.sys=20,,6304"
tools\win98se-extracted\wdm\mspclock.sys       4.10.2222         4624 131B291054C100E24D45A8645ACACBDE7B68B85E0113AEA0AA62ECF44283B42F Win98 SE CD - MS Proxy Clock; layout.inf "mspclock.sys=20,,4624"
tools\win98se-extracted\wdm\mstee.sys          4.10.2222         4976 E4F670E37150953B0E20A2AA2573ED3B4DF8B0F93C9090B0B047AC5D6EE3A10A Win98 SE CD - WDM Tee/Communication Transform Filter; layout.inf "mstee.sys=20,,4976"
tools\win98se-extracted\wdm\nabtsfec.sys       4.10.1998        46944 FF4A8D53C7E0BAF811802A2DC49439BBD8BA159021A3295BC9B55E16091A4284 Win98 SE CD - WDM Codec Driver; layout.inf "nabtsfec.sys=20,,46944"
tools\win98se-extracted\wdm\ntmap.sys          4.10.2222         9360 6E453D9645A3B674C112A7C7770E5788F3D336E154F20B4F1808991186C53C04 Win98 SE CD - Dragon to NT I/O mapper; layout2.inf "ntmap.sys=5,,9360"
tools\win98se-extracted\wdm\opl3sax.sys        4.10.2222        83280 2ACEED7C6DEBF9430480F69C2160A7313A880BBF85F88D35E380B20F0190F90C Win98 SE CD - Yamaha OPL3-SAX Sound System (WDM); layout.inf "opl3sax.sys=20,,83280"
tools\win98se-extracted\wdm\portcls.sys        4.10.2222       165424 C219603D7BA78DED4BBCD1D9636E68F9BC435D3914AC50F7A95D6C44E2C61B1E Win98 SE CD - KERNEL PORT CLASS DRIVER; layout.inf "portcls.sys=20,,165424"
tools\win98se-extracted\wdm\redbook.sys        4.10.2222         5664 68EA6825996752CE1F777AF168B10228BFE31090AB0387EAC8AFC6A2F0970B2F Win98 SE CD - REDBOOK WDM-CSA Filter; layout.inf "redbook.sys=20,,5664"
tools\win98se-extracted\wdm\sbemul.sys         4.10.2222        36112 3D6D457D0AB825AB84A50BB5F9224F82F3844DC893D8D5BBF8CA2CF43227D539 Win98 SE CD - SBEMUL WDM-CSA Filter; layout.inf "sbemul.sys=20,,36112"
tools\win98se-extracted\wdm\scsimap.sys        4.10.1998         5184 82FCAA6DF5FCB4AD396C6D54206F7B5962DF162903B0B75CF755F3D07D164ABA Win98 SE CD - NT SCSI Port Mapper; layout.inf "scsimap.sys=20,,5184"
tools\win98se-extracted\wdm\scsiscan.sys       4.10.1998         9136 48943C005296CEC31A8A7B44FD6A59B3A2487BF5565D0C447E19B2DA6E001BBD Win98 SE CD - SCSI Scanner class driver; layout.inf "scsiscan.sys=20,,9136"
tools\win98se-extracted\wdm\sonydcam.sys       4.10.2222        21504 03CD1AAB7710BF3BD807538188139401230A7B1556CBEDF6AEA8471BF2457083 Win98 SE CD - 1394 Desktop Digital Camera Driver; layout.inf "sonydcam.sys=20,,21504"
tools\win98se-extracted\wdm\stream.sys         4.10.2222        39776 0E882AEFB3EB6B4987FE5846BD458E9943FD5D093C2CA556108E9F0D37B65983 Win98 SE CD - WDM CODEC Class Driver; layout.inf "stream.sys=20,,39776"
tools\win98se-extracted\wdm\swenum.sys         4.10.2222         3296 BE6AC9B103925C831BACD8FB6654DF5EADC57057DA5C03B31CBC393020BDBC9C Win98 SE CD - Plug and Play Software Device Enumerator; layout.inf "swenum.sys=5,,3296"
tools\win98se-extracted\wdm\swmidi.sys         4.10.2222        52656 B1D2DAE5942F1B78F44F909737C712CE68AEDE5B9B296FB1EAC09A6ACC698CC7 Win98 SE CD - SWMIDI WDM-CSA Filter; layout.inf "swmidi.sys=20,,52656"
tools\win98se-extracted\wdm\sysaudio.sys       4.10.2222        45456 DB41E1479DC63353BEFBE8541EF22C07A1A0D81E82AF5D48055ADF2A35200A0E Win98 SE CD - System Audio WDM Filter; layout.inf "sysaudio.sys=20,,45456"
tools\win98se-extracted\wdm\taishid.sys        4.10.1998        15056 A1B67A703433624C6AFBB50B804583651A8B10FDC5CB1CA8631BA735F4235653 Win98 SE CD - MINI NT USB Driver; layout.inf "taishid.sys=5,,15056"
tools\win98se-extracted\wdm\tilynx.sys         4.10.1998        25168 DA3B207F0788E939BB2BE69BC6D5465E1D89147296B29B72F064E1911FD0C5A0 Win98 SE CD - 1394 TI PCILynx Port Driver; layout.inf "tilynx.sys=20,,25168"
tools\win98se-extracted\wdm\tosdvd.sys         4.10.1998        51264 F84AB037E6B15FC79B8C3A6A4343E067B7E477F046D9569F8D97D5459CA36BF3 Win98 SE CD - Toshiba DVD minidriver; layout.inf "tosdvd.sys=20,,51264"
tools\win98se-extracted\wdm\update.sys         4.10.2222        60592 9A6D8C8D6EC1BFEF28B7E58F197DCF42C80BA137BBFAF87EB69957F79636EB06 Win98 SE CD - Update Driver; layout.inf "update.sys=5,,60592"
tools\win98se-extracted\wdm\usbaudio.sys       4.10.2222        40272 7AF79D9BBFD6BCE0C19ADE3415CDD58F6BC1C278E4F40E41BF7E82B67122EB31 Win98 SE CD - Streaming class usbaudio minidriver; layout.inf "usbaudio.sys=20,,40272"
tools\win98se-extracted\wdm\usbcamd.sys        4.10.1998        12944 7FF5820070B048D9ED7F04E47FBF1F2D2D9BB1ABFA56215477ADC997A320DD32 Win98 SE CD - Universal Serial Bus Camera Driver; layout.inf "usbcamd.sys=20,,12944"
tools\win98se-extracted\wdm\usbdiag.sys        4.10.1998         9120 E60A72255AD529F88C227CC854C6F6DF13275359B9098568E31716E6251BDD77 Win98 SE CD - MINI NT USB Driver; layout.inf "usbdiag.sys=20,,9120"
tools\win98se-extracted\wdm\usbintel.sys       4.10.1998        12416 AFB5A0C69C603563EC676F5D55AA7BEB5AFA0BC8C1B28B6A2FBE62E23C288037 Win98 SE CD - Universal Serial Bus Camera Driver; layout.inf "usbintel.sys=20,,12416"
tools\win98se-extracted\wdm\usbloop.sys        4.10.1998         9040 B7605079AA7B72C4F2E990E57A5BCDD67B397389F5FFFE60CE5F3F791F10247E Win98 SE CD - MINI NT USB Driver; layout.inf "usbloop.sys=20,,9040"
tools\win98se-extracted\wdm\usbscan.sys        4.10.1998         8944 1593856C798D3714BDBB7F6E05107896ED008778D47C56F9BE63A9B179FE23F3 Win98 SE CD - Logitech USB Scanner driver; layout1.inf "usbscan.sys=20,,8944"
tools\win98se-extracted\wdm\usbser.sys         4.10.2222        21296 171DEC3223CAC3ACDD6B9C104F4AB258F715E0FB2CE6BF3E336A6B26DC1F4237 Win98 SE CD - USB Modem Driver; layout2.inf "usbser.sys=5,,21296"
tools\win98se-extracted\wdm\wdmfs.sys          4.10.1998         4064 32E8402019D1C1D63C88AE22E14EC1195F23C5856F68AAE9EA2240F2D13AEE5F Win98 SE CD - Windows WDM File System Mapper; layout.inf "wdmfs.sys=2,,4064"
tools\win98se-extracted\wdm\wmiacpi.sys        4.10.2222         7392 1A357CACD8E792C96A542123225D2A8BCEF1F69151ADF62D3C9B61243E86D006 Win98 SE CD - wmiacpi.SYS miniport; layout2.inf "wmiacpi.sys=5,,7392"
tools\win98se-extracted\wdm\wmidrv.sys         4.10.2222        14800 E4342442E4BDEEEC130C443ECD97D66029F110263829CD09F2407EC00D96E7CE Win98 SE CD - WMI Support Library; layout.inf "wmidrv.sys=2,,14800"
tools\win98se-extracted\wdm\wmilib.sys         4.10.2222         3600 DAB20A0305D604138FBD30123B96931B63AE60DD28F942A60116BA6E1B08A5A8 Win98 SE CD - WMI Library; layout2.inf "wmilib.sys=5,,3600"

# --- NUSB 3.6 (nusb36e.exe SHA-256 42B13CE4...A69621), files not in the 3.3 rows ---
# CAUTION: NUSB 3.6 installs wdmstub.sys, which supplies missing exports at load;
# a pair these files import is NUSB 3.6 platform evidence, not stock 98 SE evidence,
# when the name is one wdmstub supplies. wdmstub.sys's own imports are stock evidence.
tools\nusb36-extracted\1394bus.sys             4.10.2226        38016 CF5A0CD3F047D143EB3EF6ECAB39CE06D35AF4EF2C8354866A51BB8A88690EA8 NUSB 3.6 - 1394 Bus Device Driver
tools\nusb36-extracted\OHCI1394.SYS            4.10.2228        37040 6FCE4658013BB62929341A75233A16B4B53AFD857DF3D5D7FF07E456FC3BB1A2 NUSB 3.6 - 1394 OpenHci Port Driver
tools\nusb36-extracted\SBP2PORT.SYS            4.10.2227        36528 127AECCA7D3F3BC9C5145235459D5B24E08BFEEF60D97A98198A8BC2D747F0BA NUSB 3.6 - 1394 Bus Device Driver
tools\nusb36-extracted\openhci.sys             4.90.3000.1      25744 71D039BB9CF160C6480325D914D3263A655B18619A982B1FB1A37E22D9CC5829 NUSB 3.6 - Open Host Controller Interface USB Driver
tools\nusb36-extracted\uhcd.sys                4.90.3000.1      34608 DE598350BD163EA87F28BFF7019C8160D134560F46B7CF74077D668DA89E91D7 NUSB 3.6 - Universal Host Controller Driver
tools\nusb36-extracted\usbccgp.sys             5.1.2600.5585    32384 4C1B3E8F3F658E356A955108FF84FB5C95244CB2A9D323AA0DFAEF92927C66C5 NUSB 3.6 - USB Common Class Generic Parent Driver
tools\nusb36-extracted\usbd.sys                4.90.3000.1      22928 E165029E43579A207D3E2A85169563B2DB8C8F16DFF41BEC2EF1A5E829AB9B25 NUSB 3.6 - Universal Serial Bus Driver
tools\nusb36-extracted\usbhub.sys              4.90.3002.1      41840 AF0249A41110AB5F1E850AEB285C9722BC4FF0C14CA2B6114E7C70289268C4E5 NUSB 3.6 - Default Hub Driver for USB
tools\nusb36-extracted\wdmstub.sys             5.00.006         12767 90B89433C76D50DDDDB32AB261B6D910DA9A51920387144C8608EC7244AE7F2A NUSB 3.6 - WDM stub functions for Windows 98
```

## Sources

- `docs/usb-xhci-info/xhci-programming.md`, "What SuperSpeed Support Would
  Require": the refusal and the USB4 paragraph that stayed behind.
- `docs/usb-xhci-info/win98-wdm.md`, "USB Stack Architecture and the
  Integration Decision": the stack survey, Option A and the Option B
  fallback.
- `docs/contributing/architecture.md`, "Fallback: Option B (monolithic HCD)".
- `AGENTS.md`, "Port Strategy (USB 2.0 vs USB 3.x)".
- `docs/contributing/roadmap-hcd.md`: the decisions table, the shape of the
  successor and Phases 25 to 32.
- `docs/contributing/test-equipment.md`: the SuperSpeed drives, the UAS
  bridge and the USB 3.0 hub the bench clauses name.
