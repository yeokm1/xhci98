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
`docs/future-plans/superspeed-storage-behind-a-switch.md`, and its section 2
says where the line between the two lies. With the miniport frozen it will
not be built; it stayed in `future-plans/` as the record of what it would
have taken until that folder was removed on 2026-10-12 (it is in git
history at `00a3b9e`), and its `-0` transcription list is what Phase 29's
`29-0` batch started from.

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
- **The behind-a-switch proposal is not built.** It stayed in `future-plans/`
  until 2026-10-12, and is in git history at `00a3b9e`.
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
  2026-10-02, reversing a narrower answer given on the review earlier
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
| Function PDO | WDM PDO, one per function of a split composite device (section 10) | the root-hub FDO | the root hub, beside the device PDOs; **since 36.43 the device's composite-parent PDO (10.12)**, itself where a device PDO would be | the function (its interface mask, its ids), its own filtered copy of the configuration descriptor (10.9), the group serial it shares with its siblings, and a pointer to the device record inside the bus, whose slot and pipes it shares, cleared when the device leaves (corrected 2026-10-03 from "a pointer to the parent device object inside the bus, whose pipes it shares", as 26-A.7 implements it) |
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
**And on 2026-10-10 by task 36.43 (section 10.12):** each composite device
the bus splits has a composite-parent PDO under `XHCI98\COMPOSITE`, bound to
this driver as a composite FDO - a fourth role - with its function PDOs as
its children; the bus still splits, configures and routes.

**The controller FDO's start and stop on an Intel 7-, 8- or 9-series PCH or
C610/X99 also move the switchable connectors** (task 34.3, 2026-10-05): on
Intel device ids 1E31, 8C31, 9C31, 8CB1, 9CB1 and 8D31 the start routes them
from EHCI to the xHCI through four configuration registers above the PCI
header, before `XhciInitController`, each return to D0 routes them again,
and every stop, a refused start and a system shutdown's D3 hand them back.
Design record 16 (`16-intel-port-switchover.md`) is that design, its gate,
its switch (`XhciIntelPortSwitch`, 5.5) and its readings; it is not copied
here.

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
implementation never did; review of batch (b), round 1, finding 17).
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
   (review of batch (b), round 2, note 7). Held only to link, unlink,
   mark or copy the lists.
3. **The controller lock**, innermost, as above.

**Amended for `2.3.0.0` (roadmap-hcd 36.20).** Phase 36 adds, without
changing the three layers:

- **The event delivery** (design records 15 and 18). The ISR is one of
  three ways the event ring is reached: the line interrupt
  (`IoConnectInterrupt`, as above), a message interrupt on Vista and 7
  (`IoConnectInterruptEx` on amd64, through the `xhcimsi.sys` helper on
  x86; a message ISR of its own in `hcd_ctl.c`), or no interrupt at all
  while the poller (`hcd_poll.c`'s timer and DPC) drains the ring. The
  delivery is settled once per start. `PollLock`, a spin lock guarding the
  poller's state, nests inside the controller lock: **the controller
  lock, then `PollLock`, never the reverse**, neither held across a
  drain, and `TimerLock` never taken with `PollLock` held (record 15
  section 5.1). The line's retirement, when the watcher finds it dead,
  runs under `KeSynchronizeExecution` (record 15 section 5).
- **Selective suspend** (design record 14). Its per-device link machine
  and each function PDO's power slot are under the controller lock
  (record 14 section 4.1). The link machine's transitions are the enum
  thread's alone; the one other writer is stage B's Function Wake
  notification latch (`FwPending`, `FwPendingFar`), set from the event
  DPC under the controller lock (`HcdSuspNotificationLocked`,
  `XhciSuspFwReceive`; record 14 section 8.7);
  `PdoListLock` before the controller lock where both are needed, the
  cancel spin lock released before the controller lock is taken, as
  above. The idle notification's callbacks run on a second system thread
  per controller (record 14 section 6.1), one at a time, holding no bus
  lock; the enum thread waits on it only through a bounded, deferring
  rundown. No power or idle IRP is completed on the enum thread itself:
  each is queued to a pool of the controller's parked completion workers,
  provisioned with a grower thread at the PnP start (record 14 section
  3.12; roadmap-hcd 36.35's fix).
- **The door's descriptor reads** (36.45, section 8.11): a request pended
  on the controller's door list under its cancel-safe rules and served by
  the enum thread on the default endpoint.
- **The lost-interrupt backstop in a timer DPC** (36.55, design record 17
  section 4.1). The tolerance clock's DPC first takes the controller lock
  for the backstop's peek and releases it. Only then does it take
  `TimerLock` to count the tick and re-arm, or to refuse the re-arm once
  the timers are closed. It never holds the two together, and because the
  controller work comes before the re-arm, `HcdTimersDrain` waiting out
  the re-arm also waits out that work. It queues the event DPC through the
  ISR's admission and never drains inline. Because
  this DPC runs until the timers are drained, after the event DPC is
  closed, the release raises `DpcClosed` with an interlocked exchange
  (the ISR and the enum thread are gone by then).

**Amended for roadmap-hcd 37.3.** The shutdown latch (section 5.8) adds
one word under the controller lock, `ShutLatched`, set with `PowerDown` in
one hold before the shutdown's halt and read by every admission that
would otherwise leave a request waiting for a D0; its flush runs on the
power path under the power gate, takes its lists under the controller
lock and completes outside it, and creates no thread.

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
| `XhciFastPollFsLs` | New in `2.1.0.0` (task 33.8), the HCD's own: read at each controller start, written by no INF until task 34.1 and as 0 since, absent or 0 is off. Lets a Low- or Full-Speed interrupt endpoint on a root port be polled faster than 1 ms; section 13. Since task 36.19, 4 lets a client choose 2000 to 8000 Hz by the codes 128 to 130, section 13.7 |
| `XhciIntelPortSwitch` | New in `2.1.1.0` (task 34.3), the HCD's own: read at each start of a controller the switchover's gate passes, and on no other; written as 1 by every controller install path under task 34.1's rule until `2.2.0.0`, and as 0 since task 36.42 (`2.3.0.0`), when absent or of another type became off too; a DWORD that is not 0 turns the switchover on. Design record 16 sections 5 and 5a |

The root-hub sections write no value of the controller's: they carry the
loader values on Windows 98 and the hub property-page registration on every
path (section 8), which the gate's `HCD-HUBPAGE` rule holds them to.

Since task 34.1 (owner, 2026-10-05) both INFs write every value the HCD reads,
on every install path: the three above, `XhciFastPollFsLs`, and
`XhciForceBulkOnly` (31-A.3), `XhciFirstEnumWaitMs` and `XhciFirstEnumPortMs`
(33.3), each at the driver's own default but the interval and, since
roadmap-hcd 37.2, `XhciForceBulkOnly`: the INFs write 0 (UAS) and the driver
reads an absent value as 1 (Bulk-Only), for text-mode Setup (section 5.6; issue
15). Each is written with FLG_ADDREG_NOCLOBBER (`0x00010003`), so an install or
update writes only a missing value and keeps one the user set; a later release
therefore cannot change a default on a machine that already holds the old one.
The gate's `VAL-*` rules hold all seven, flags included, and since task 34.3 an
eighth, `XhciIntelPortSwitch`, under the same flag (at 1; at 0 since task
36.42).

**Amended for `2.3.0.0` (roadmap-hcd 36.20).** The INFs write sixteen
values under the same rule and flag: the eight above; `XhciTolerance`,
`XhciIntervalCap` and `XhciAvgTrbEsit` (Phase 35, `2.2.0.0`);
`XhciIntelIrqRoute` (36.3, at 1); `XhciMaxTransferKB` (36.22, at 1024);
and `XhciInterruptMode`, `XhciPollIdleMs` and `XhciPollActiveMs` (36.8,
at 0, 50 and 1; design record 15 section 3). Three more were read and
written by no INF, so absent was their default: `XhciWideMap` (36.24,
section 11.6; off when absent or 0, any other number on, owner 2026-10-10), `XhciSelectiveSuspend`
(36.33, design record 14 section 7; on only for a `REG_DWORD` of exactly 1) and
`XhciUsb2HubSpeedFallback` (36.49, issue 13; off only for a `REG_DWORD` 0).
**Amended 2026-10-11 (roadmap-hcd 36.51; owner decision of that day):**
every value the driver reads is written by the INFs with the driver's own
default, so the INFs write nineteen, these three at 0, 0 and 1 - each the
value that reads as an absent one does - under the same rule and flag.
`txtsetup.oem` still writes none, so F6 Setup runs at the code defaults.
**Amended by roadmap-hcd 37.2 (2026-10-12):** `XhciForceBulkOnly` is the
one value whose written number is not what an absent one reads as: the
INFs write 0 and the driver reads an absent one as 1 (above, and section
5.6).

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
Setup's own source **in text mode only**. Loaded there is not copied: XP's
`TXTSETUP.SIF` copies `hidclass.sys`, `hidparse.sys` and `usbd.sys` to the
target disk only in its own host controllers' `[files.usbohci]`,
`[files.usbuhci]` and `[files.usbehci]` sections, so after the text-mode
reboot an xHCI-only machine has `hidusb.sys` and `usbstor.sys` but not
what they import, and USB input is dead in GUI mode until "Installing
Devices" - behind XP's unsigned-driver prompts for this driver (runtime,
2026-10-04, XP SP3 and XP x64 SP2 target disks read offline,
`out\phase33\f6\`; the release notes carry it as a limitation, and no
`txtsetup.oem` mechanism reaches the Windows source: `[Disks]` names an
OEM disk and `[Files]` reads from it). Where a machine also has EHCI controllers, XP's text
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
text mode never writes. **Since roadmap-hcd 37.2 an unread value is 1**
(issue 15), so in text mode a device offering both transports is selected
for Bulk-Only and meets Setup's own `usbstor`; the INFs write 0, which
keeps UAS on an installed system. A device selected for UAS presents the
UAS class id, which no Setup row binds, so a UAS-only device is still not
usable until GUI mode; a Bulk-Only-only device (most USB flash sticks;
QEMU's `usb-storage`) always meets Setup's own `usbstor`. Folding the UAS
driver into `xhci98.sys` would answer the UAS-only case; it was planned
(UAS.1, then roadmap-hcd 38.11) and removed on 2026-10-13 (owner), so the
UAS-only case stays open.
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
4. A disk the bus selects for UAS is not usable in text mode, since no
   UAS driver is loaded (above). Since 37.2 that is a UAS-only disk: one
   that also offers Bulk-Only is selected for Bulk-Only there, as an
   unread `XhciForceBulkOnly` is 1, and a Bulk-Only-only device always
   is usable.
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

### 5.7 The first relations answer waits for the devices present at start (task 33.3, 2026-10-04)

**The reading that asked for it** (runtime, 2026-10-04, development host A,
QEMU; `out\phase33\f6\w2k*`, not tracked). At Windows 2000 SP4's text-mode
Setup, F6-loaded `xhci98.sys` started the controller and the root hub, and
no USB device was ever started: the keyboard was dead and the stick absent
from the partition screen. The qemu flavour's trace showed the root-hub
FDO's first `IRP_MN_QUERY_DEVICE_RELATIONS` (`BusRelations`) answered with
no device, because the controller thread had not yet enumerated any port;
the device PDOs came later, after the ports' change events, were queried
for their ids, capabilities, text and resources, and were never started. A
control guest on Windows 2000's own UHCI stack bound a USB keyboard and
stick in text mode, and a keyboard hot-plugged during text mode stayed dead
there too: **Windows 2000's text mode starts only what a hub's first answer
carries.** 32-bit XP and XP x64 start late PDOs, which is why their F6 legs
passed without this.

**The rule.** The first `BusRelations` answer of a hub FDO after its
`START_DEVICE` - the root hub's (`hcd_rh.c`) and each external hub's
(`hcd_hubfdo.c`, section 10.11) - waits, bounded, until the devices
connected when the bus first looked at the ports have settled, then
answers with what is listed. Settled means: every port that read connected
when the thread looked at it has its PDOs created (Present or Bound), or
has failed or been refused (Failed, a hub refused as too deep, a UAS device
refused in place with its PDOs); every hub brought up has had its ports
looked at once (its `Changed` bitmap empty); and a send-back to the USB 2.0
companion (29-A.5) asked for in the window has reached the companion's
connect, which the companion's own enumeration then settles. Addressed
alone is not settled. Every later answer is as before; a device that
misses the first answer is reported later, as before, through the
invalidation its PDO's creation makes.

**The mechanism**, against 5.3 and 5.4:

- **A generation.** `HcdEnumAttach` (the root hub's start) marks every root
  port changed and, in the same hold of the controller lock, increments
  `SettleAsked`; an external hub FDO's start asks the same way
  (`HcdEnumSettleAsk`). The FDO keeps the generation (`SettleTarget`,
  `SettlePending`).
- **The thread settles it.** `HcdEnumService` reads `SettleAsked` before the
  ports' change bits, so a pass that reads a generation has the bits the
  start set with it; the ports' machines run to rest inside the pass (5.3).
  At the end of a pass that enumerated (powered, root hub started, not
  halted), `hcdSettleCheck` declares the generation settled when nothing is
  owed or in flight: no root port change bit (the reset's own change
  included, so one more pass looks), no hub `Changed` bit, every managed
  port's machine at rest (`XhciEnumAtRest`), no SuperSpeed root link whose
  warm reset (29-A.2) is still read in progress (`PORTSC.PR`; the port's
  inspection is owed again meanwhile and feeds nothing; a reset still in
  progress past twice its own 1 s wait (a relative timer the thread alone
  arms and cancels as it leaves) is the controller's failure, its
  recovery asked for as for an unreadable port; `LinkRecovering`, rounds 2
  and 3), no
  send-back of the window in flight (`XhciEnumHoldInFlight`). A hub port
  whose `GET_STATUS` failed is owed its look again up to three times in a
  row (`HCD_HUB_LOOK_TRIES`) rather than taken as looked at (review of
  33.3, round 1, findings 2 and 3). `SettleDone` only moves forward. With
  nothing attached, the first pass after the start finds nothing in flight.
- **The waiter never holds what the thread needs.** The first answer looks
  (`HcdEnumSettleStep`) and sleeps 20 ms (`KeDelayExecutionThread`) between
  looks at PASSIVE_LEVEL, holding no spin lock, not the power gate (the
  thread takes it for every pass, `hcd_ctl.c`), and not the controller: it
  enters for each look (the root hub through `RootHubUsers`, a hub FDO
  through its PDO's `Busy`) and leaves before the sleep, so the
  controller's remove and the parent's release, which wait those counts
  out, never wait on a sleep. The thread it waits for creates PDOs and calls
  `IoInvalidateDeviceRelations` but waits on no PnP IRP, a child's start or
  removal included, so the two cannot deadlock; an invalidation of the
  same devnode during the answer is legal and only asks once more. The
  wait ends on the settle, on the root hub not started or detaching or the
  thread not running (teardown), or at the deadline.
- **The deadline is one per answer**, not renewed per port: a relative
  `KTIMER` on the waiter's stack, armed at its first look and polled with a
  zero wait, the pattern `HcdHubPortDebounce` already uses on every target,
  so a change of the system time moves no bound (review of 33.3, round
  1, finding 4). At the deadline the waiter retires its own generation,
  counts it (`SettleTimeouts`; a teardown is counted apart, `SettleAborts`,
  finding 5) and answers; the thread finishes what it was doing, and those
  devices are reported late. The time waited is traced from
  `KeQuerySystemTime` (its low word, in 100 ns units; no 64-bit arithmetic;
  task 25.3's evidenced pair, `uhcd.sys` among ten stock Windows 98 SE
  precedents, and on amd64 a macro with no import), for the trace alone.
- **A per-port budget, for fairness.** The thread enumerates the ports one
  after another, so a slow device on a lower-numbered port would spend the
  deadline of every port after it. `hcdRun` checks, between the machine's
  steps, whether its own relative timer has fired; past the budget while a
  first answer
  waits, the attempt is given up as an unplug gives it up (the slot back,
  the machine to Empty) and the port **deferred**: its look kept owed (a
  root port's in `PortChange`, a hub port's in its hub's `Changed`) and not
  taken, and not counted against the settle, until no first answer waits,
  when the port is enumerated afresh. A deferred hub port is also
  remembered by its physical path (the hub's root port and Route String and
  the port's number), so the recovery that frees the hub object and builds
  it again keeps it deferred (`SettleDeferHub`, sixteen paths; when it is
  full every hub port rebuilt before the settle is taken as deferred rather
  than one forgotten; finding 1 and round 2). A port whose
  enumeration halted the
  controller (a command or an EP0 transfer that never completed: a 5 s
  wait and a recovery) is deferred the same way, so the rescan does not
  meet it first again. Counted (`SettleDeferrals`), traced. The check sits
  between steps, so one step's own timeout (a command or a transfer, 5 s,
  `hcd_enum.c`) can carry a port past its budget: **a device that never
  answers one request costs that request's 5 s**, which is the whole
  default deadline, and a keyboard on a later port is then reported late.
  No QEMU device models a device that never answers, so that case is
  argued, not read.

**The bounds** (owner, 2026-10-04): **5 s total** per FDO's first answer
and **2 s per port**, both on the controller's driver key as REG_DWORDs in
milliseconds read at every start: `XhciFirstEnumWaitMs` (0 turns the wait
off; held to 30,000) and `XhciFirstEnumPortMs` (0 for no per-port budget;
held to the total). No INF wrote either until task 34.1; since then every
INF writes 5000 and 2000 under the don't-overwrite flag. **Text-mode Setup writes no
driver-key value, so the defaults are what F6 Setup gets**, and they are
chosen to be right on their own. Their scale is documentation, not a
binary reading: Microsoft's account of its own hub driver's enumeration
(the Microsoft USB blog, "How does USB stack enumerate a device?",
techcommunity.microsoft.com, read 2026-10-04: a 100 ms stable debounce, the
port given up if not stable within 200 ms, a 5 s timeout on each port
reset, up to three enumeration tries 500 ms apart, 10 ms reset recovery and
10 ms after SET_ADDRESS) and USB 2.0 section 9.2.6.4 (a request with no
data stage done within 50 ms, a data stage begun within 500 ms, 2 ms after
SET_ADDRESS): a normal device is ready in 0.15 to 0.3 s, one retry takes 1
to 2 s, and only a device that times out every try takes longer. Two
reference hub drivers were read for the shape, as interface documentation
only (section 3 of `legal-provenance.md`; nothing taken into `src\`):
ReactOS's `usbhub` (`drivers/usb/usbhub/pnp.c`, `USBH_FdoQueryBusRelations`,
master as fetched 2026-10-04) enumerates synchronously inside its
`BusRelations` handler, port by port, with a 5 s timeout per reset
(`USBH_SyncResetPort`), up to three device-creation tries 500 ms apart and
no overall cap; Linux's `hub.c` (`drivers/usb/core/hub.c`, the same day)
enumerates asynchronously and blocks no first report, with a 100 ms stable
debounce (in 25 ms steps, given up at 2 s) and resets of 10 to 200 ms. The
debounce, reset and recovery timings of 10.2 are the USB 2.0
specification's and stay unconfigurable.

**Decisions.**

1. **Always, not gated to NT 5.0 text mode.** Nothing in the driver can
   tell text mode from a running system before the answer, and the wait is
   what Microsoft's own hub driver does on every system; it costs one look
   when nothing is attached (20 ms measured, one step of the waiter) and
   the devices' own enumeration time when something is, which they then do
   not spend being found afterwards. Measured (below): 20 ms to 1.1 s.
2. **Every start, not only the first.** A `STOP` and `START` of a hub FDO is
   a fresh start: Windows 98 SE's and ME's controller re-enable (task 33.1)
   stops and starts the root hub, and the first answer after it waits for
   the re-enumeration that revives the dormant PDOs (5.2). The revived PDOs
   stay listed throughout and are in the answer whether revived or not,
   and a dormant PDO's own `START` wait (`hcdDormantWait`) is on another
   devnode's IRP after the answer, so the two waits never nest. `STOP` and
   `SURPRISE_REMOVAL` clear a pending wait.
3. **Only the first answer after a start.** The flag is cleared before the
   wait, so an answer that fails (pool) does not wait again, and two
   concurrent first queries, should two ever run at once, each wait
   on their own FDO's generation.
4. **A hub FDO waits on the whole bus's settle**, not only its own ports.
   The thread can state "nothing in flight anywhere" cheaply and exactly;
   a per-hub cohort would need a second bookkeeping of the same machines.
   A hub's ports are seeded for their first look in its bring-up (10.3) and
   looked at in the same pass, after its PDO is made and before PnP can
   start it (corrected after review of 33.3, round 1), so its FDO's
   first answer is one look unless the bus is busy elsewhere (0 to 31 ms
   measured). It never waits on a descendant hub FDO's
   start. Tiers add up at boot: a hub FDO starts only after its parent's
   answer, so a chain of hubs pays each tier's own settle in turn, each
   bounded by its own deadline.
5. **Devices behind a hub whose PDO could not be created** are presented
   under the next hub up (10.11) and are in that hub's answer; the
   bus-wide settle covers them whichever FDO answers.
6. **A hub FDO started after its devnode was disabled** (added 2026-10-05;
   10.11, ordering item 4) asks for the cycles of the children PnP let go
   of with the removed FDO before it asks its generation. The pass that
   reads the generation has therefore taken the cycles, in that pass or an
   earlier one. A let-go PDO is missing at once, so its port leaves Gone
   in the same pass, is marked for a look, and is enumerated before the
   pass's `hcdSettleCheck`. The first answer then carries the devices
   again, as at a fresh start, and the bounds above are unchanged: a
   device that misses the deadline is reported late through its PDO's
   invalidation, as before.

**Measured** (runtime, the `qemu` flavour's trace unless a row says
`release`, 2026-10-04 and 05, development host A, QEMU TCG;
`out\phase33\initenum\`, git-ignored). The first rows were taken on
`89a8025`, the rows marked "final" on `59e1897` (review rounds 1 and 2 in);
rounds 3 and 4 after it touch only a SuperSpeed root link still in reset,
which none of these legs has, and the keyboard-behind-a-hub F6 leg passed
again on `82902e8` (the branch's last code; 562 ms and 31 ms).

| Leg | Devices at start | Root hub's first answer | Hub FDO's first answer | Result |
|---|---|---|---|---|
| Windows 2000 SP4 text mode, F6 | keyboard, stick at root ports | 343 ms, both PDOs | - | keyboard answers at Welcome and the licence; stick listed at the partition screen |
| the same, `release`, and again final | the same | (no trace) | - | the same |
| the same, keyboard behind a `usb-hub`, and again final | keyboard behind the hub, stick at a root port | 562 ms | 31 ms | the same |
| 32-bit XP SP3 text mode, F6, `release`, two processors | keyboard, stick at root ports | (no trace) | - | the same |
| Windows 98 SE, installed (`win98-gold` overlay), cold boots | mouse, stick at root ports; mouse behind a `usb-hub` | 625 to 650 ms | 0 ms | all started, no wizard at the second cold boot |
| the same, controller disabled and enabled in Device Manager (task 33.1) | the same | 0 ms | 0 ms | every PDO kept dormant and revived; the hub FDO started before the root hub's answer was asked |
| Windows 2000 SP4, installed (`win2k-au28a` overlay), and again final | mouse, stick at root ports; mouse and stick behind a `usb-hub` | 961 to 1,081 ms | 20 ms | all started, both sticks with drive letters |
| the same guest, every device unplugged, restart | nothing | 20 ms | - | - |

No answer reached its deadline and no port was deferred. Windows 98 SE's
NTKERN took the waits with no stall: the root hub's first answer is
asked for while the controller thread enumerates and calls
`IoInvalidateDeviceRelations`, and it ended at the settle each time.
Windows 98 SE asks a hub FDO for its relations well after its start, so
its wait is nothing. One Windows 98 SE restart (Start, Shut Down,
Restart) hung after the controller's D3 with the guest in real mode, and a
`system_reset` met the same hang; a Shut Down and a cold boot were clean.
It is outside this wait (no relations answer is involved after the D3)
and was not bisected. Not read here: Windows ME, Driver Verifier, a
SuperSpeed device, and a device that never answers (QEMU models none).

### 5.8 The shutdown latch (task 37.3, 2026-10-12)

**The gap.** After the controller's D3 nothing the bus holds can end by
itself. A transfer URB admitted then is published on a ring of a controller
that is not initialized: the doorbell is dropped and the TD waits for the
resume's reinitialization to complete it (`hcd_io.c`, `HcdIoMapped`). A
command URB, `RESET_PORT` or a streams request waits on `SlowIrps` for
`HcdCfgService`, which the thread runs only on a powered controller
(`hcd_enum.c`, `HcdEnumService`). A record held on a pipe, or an IRP
waiting for a record, waits for a release only a running ring gives. At
S3 and S4 that is the design: the D0 that follows serves or completes
every one. At S5 no D0 follows, and each of them waits for good. No
observed hang rests on it - Windows ME's shutdown hang with an unopened
disk was traced to ME itself, with nothing reaching the bus after the D3
(`runs/run-36.md`) - so this is hardening of a gap `2.2.0.0` has too.

**The latch.** `hc->ShutLatched`, a word appended at the end of
`HCD_CONTROLLER`, written and read under the controller lock. It is set
when the controller leaves D0 and its system state is
`PowerSystemShutdown` (`Common.SystemPower`, written by `hcdSystemDone`
from the system IRP before it asks for the device IRP), on both routes
there: the device SET_POWER to D3, and the direct transition a failed
`PoRequestPowerIrp` leaves to the work item (`hcd_power.c`). It is set
**in the same hold of the controller lock as `PowerDown`**
(`hcdSuspendSettled`, record 14 section 4.4), before
`XhciSuspendController` halts the controller. A controller already
suspended when the shutdown reaches it takes the latch there instead,
there being no halt left to precede, by an operation that is idempotent:
at the shutdown's D3 (an earlier direct transition left it suspended in
D0, or the function is already out of D0); in the direct transition when
it was already suspended in D0; and where `hcdSystemDone` finds the
function already in the state the shutdown wants, so asks for no device
IRP at all - there the system IRP is deferred to the work item, which
latches and flushes at PASSIVE_LEVEL under the power gate and then
completes it (Codex review of this design, round 1, finding 2). The pure
decision is `XhciTolShutLatch` (`xhci_tol.c`):
only the shutdown state latches, so S1 to S4 keep today's
hold-until-resume. A D0 clears it, under the same lock, before the resume
runs, and so does a PnP start, for the lifetime it begins: no D0 follows an S5 on any target read here, and one that did must
find the bus serving again.

**Admission after the latch.** Every path that would leave a request
waiting reads the latch under the controller lock it is set under, so a
request either sees it or was queued before it and is in the flush below:

- `HcdIoSubmit` (a transfer URB): refused `USBD_STATUS_DEVICE_NOT_RESPONDING`
  through `HcdIoRefuseLater`, at the next tick, never inline (a client
  resubmitting from its completion routine would recurse) - after the
  containment's hold (35-T.6, which keeps its own rule) and after a gone
  device's `DEVICE_GONE`, which `HcdIoRefuseLater` completes and nothing
  parks.
- `HcdCfgQueue` (a command URB, `RESET_PORT`, a streams request): a URB is
  refused the same way. `RESET_PORT` and the two streams requests carry no
  URB, and `HcdIoRefuseLater`'s DPC reads `Argument1` as one, so they are
  completed inline `STATUS_UNSUCCESSFUL` - what `hcdCfgComplete` makes of
  `DEVICE_NOT_RESPONDING` for a request without a URB, so a refused one and
  a flushed one read the same. A port reset is not resubmitted from a
  completion routine, which is why the gone device's `NO_SUCH_DEVICE` is
  completed inline already.
- A promoted record's refills (`HcdIoRefillFlush`, `hcdWidePump`) are not
  gated: they continue a request admitted before the latch, on a
  controller that still runs until the halt, and the flush's drain stops
  each promoted record's machine before it flushes the refills
  (`hcdDrainOne`), so none is published after it (round 1, finding 3).
- A record already taken and launched after the latch (`hcdGo`), or mapped
  after it (`HcdIoMapped`, `hcdWideMapped`, `hcdWidePublishLocked` for a
  promoted record): never held and never published; it goes to the done
  list with `DEVICE_NOT_RESPONDING`, as a record of a gone device goes with
  `DEVICE_GONE`, and `HcdIoDeferred` completes it at the next tick
  (`!engine`, so `HcdIoRefuseLater`). This is also how a waiting IRP handed
  a record by a completion, and a record still in the map pump at the
  flush, end.

**Never `DEVICE_GONE`.** `HcdIoDeferred` parks a transfer that completes
`DEVICE_GONE` on its PDO (`HcdIoPark`) until an abort, a cancel or the
PDO's stop or removal - none of which a shutdown sends - so that status
would only move the wait. `DEVICE_NOT_RESPONDING` (`0xC0000005`) is what
a transaction that got no answer completes with, and `XhciPipeNtStatus`
maps it to `STATUS_UNSUCCESSFUL`.

**The flush, after the halt.** On the power path, at PASSIVE_LEVEL, the
power gate held - so the thread is not in a pass and no slow IRP is in its
service, and no suspend, resume or recovery runs beside it - after
`XhciSuspendController` (or at the D3 of a controller already suspended):

1. **Commands**, always once latched: every IRP on `SlowIrps`, and every
   device's IRPs selective suspend deferred (`SlowDeferred`, but those a
   cancel routine owns, which that routine completes), taken off under the
   controller lock and completed after it with `DEVICE_NOT_RESPONDING`
   (`hcdCfgComplete`; a request without a URB `STATUS_UNSUCCESSFUL`). They
   hold no DMA.
2. **Transfers**, only on the halt's positive proof: `USBSTS` read by the
   flush, on a started controller with its registers mapped and the
   function still in D0, showing `HCHalted` on a window
   that decodes (`XhciTolHaltProven`: all-ones carries the bit and proves
   nothing); the common buffer not pinned; the controller not contained
   (`Tol.Unreadable`). The quiesce's own flags are not the proof: it
   clears `RUNNING` and `HW_RUNNING` on a configuration space that reads
   all-ones too (`xhciTryClearBusMaster`), which the containment's proof
   refuses (round 1, finding 1). A function the bus already has out of D0
   cannot be read, so its transfers stay. Then every
   pipe of every device with a slot (`SlotDevice[]`) and of every port's
   device - the set `hcdDrainAll` walks - is drained with
   `DEVICE_NOT_RESPONDING` (`HcdIoDrainPipe`: waiting IRPs, held records,
   the engine's queue, a promoted record through its machine), the
   device's pipes staying open and the device not marked gone. The saved
   image is dropped (`SavedStateValid` 0) in the same step: the drained
   TDs are still on the rings, and a resume, if one ever came, must
   reinitialize rather than restore over them. Without the proof the
   transfers stay where they are, as `HcdIoDeviceDrain` keeps them on a
   pinned buffer: completing a request whose buffer the controller may
   still master is worse than a wait. A contained controller keeps 35-T.6's
   holds in full (the flush takes no command either).

The pure decision is `XhciTolShutFlush` (`xhci_tol.c`, host suite
`test_tol`). The log ring notes `power.shut`: bits 1-0 the decision
(1 commands, 2 transfers), bits 31-8 the commands completed.

**What it does not do.** It creates no thread and waits on none (roadmap
36.35's rule: on Windows 98 SE a thread's creation during the S5
transition never returned): every completion is made on the power path
itself or by the PDO's refusal timer. It sends no command and touches no
register beyond the halt the D3 already makes. It does not release
requests already parked on a PDO for a departed device (`HcdIoPark`),
whose rule is usbport's. It changes nothing at S1 to S4, at a stop or a
removal, or with the latch clear.

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

**The power rows since selective suspend (`2.3.0.0`, roadmap-hcd 36.28 to
36.40, design record 14; amended in 36.20).** With `XhciSelectiveSuspend`
0, the default, the PDO answers `QUERY_CAPABILITIES`, `WAIT_WAKE`,
`SET_POWER` and `SUBMIT_IDLE_NOTIFICATION` as before: no D1/D2 and no wake
in the capabilities, `WAIT_WAKE` refused, a D-IRP recorded and completed,
the idle IRP held and its callback never called. With 1, an eligible
non-hub device PDO (record 14 sections 5.1 and 8.2) reports D2, and
where its configuration allows remote wake also D1 and wake from both
(without it, D2 and no wake; record 14 sections 5.2 and 8.3); a Dx suspends
the device's link once its last function leaves D0 and a D0 resumes it
(record 14 sections 3 and 4); `WAIT_WAKE` arms the device (section 5);
and the idle IRP's callback is called on the bus's own callback thread,
in batches per composite device (section 6). Hub PDOs and composite-parent
PDOs keep the switch-0 answers in either case.

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

### 6.7 What the class drivers send after a failed transfer (task 35-T.0)

Design record 17 section 4.10 asks what each target's class drivers send,
and whether they resubmit, after each error completion that record's
tolerance can return; the answer sets 35-V's expectations. **Method.** Every
fact in this section is **static**: read from a disassembly listing, nothing
executed. A subagent read the binaries on 2026-10-06, and the reading was
not re-read line by line by the coordinator. The 32-bit images with MSVC
6.0's `dumpbin` 6.00.8447 `/disasm` and `/imports`, with no symbols (the IAT
slots resolved by counting from each base `/imports` prints); XP x64, Vista
and 7 with WDK 7.1 `cdb.exe -z` 6.12.0002.633 and the PDBs in
`tools\symbols` (`x` and `uf`), XP x64 `hidusb.sys` with raw `u`, having no
PDB; and a PowerShell byte search of every `hidusb`, `usbstor`, `usbaudio`
and `hidclass` image under `tools\*-extracted\` for the little-endian IOCTL
codes. The images and their hashes are section 6.2's; the Vista SP2 and 7
SP1 `usbstor.sys` and `usbaudio.sys` (x86 and x64) are extracted there too.
Constants from WDK 7.1 `usb.h` lines 353-370, `usbdi.h` line 44
(`USBD_STATUS(s)` = `s & 0x0FFFFFFF`), `usbioctl.h` lines 130-131
(`USBD_PORT_ENABLED` 1, `USBD_PORT_CONNECTED` 2) and `ntstatus.h`.
Addresses are VAs at base `0x10000` as the tool printed them.

**No class driver of any target sends `CYCLE_PORT`.** The bytes `1F 00 22
00` (`0x22001F`) occur in no `hidusb.sys`, `usbstor.sys`, `usbaudio.sys` or
`hidclass.sys` under `tools\*-extracted\` (98 SE, NUSB 3.3/3.6, 2000 SP4,
XP SP3, XP x64, Vista SP2 x86/x64, 7 SP1 x86/x64); `07 00 22 00`
(`RESET_PORT`) and `13 00 22 00` (`GET_PORT_STATUS`) occur once each in
every `hidusb` and `usbstor` and in no `usbaudio` or `hidclass`.

**`hidusb.sys`** never reads the URB status of a failed interrupt-IN read;
it decides on the IRP's NTSTATUS alone. On 98 SE (4.10.2222, read completion
`0x10A5A`, `0x10A74 jl`) any failing NTSTATUS, `0xC000009D` and
`0xC0000120` included, queues one reset work item (`0x10935`; one at a time,
by `InterlockedCompareExchange` on the extension's +0x28) and holds the read.
From 2000 on (2000 SP4 5.00.2142.1 `0x11035` / `0x11043`; XP SP3
5.1.2600.5512 `0x10AA7` / `0x10AB2`; XP x64 `0x11C09` / `0x11C10`; Vista and
7 `HumReadCompletion`, 7 x86 `0x11EE8` / `0x11F09`) `STATUS_CANCELLED`
(`0xC0000120`) and `STATUS_DEVICE_NOT_CONNECTED` (`0xC000009D`) complete
with no reset. The work item sends `GET_PORT_STATUS`; then `ABORT_PIPE` and
`RESET_PORT` - on 98 SE and 2000 only when the port reads connected and not
enabled (98 SE `0x10963` / `0x10969`), from XP on whenever it reads
connected (XP SP3 `0x109EE`, 7 x86 `0x11CCA`); then
`SYNC_RESET_PIPE_AND_CLEAR_STALL` (URB 0x1E) on the interrupt pipe while the
status so far is a success (on XP x64, Vista and 7 nothing follows a port
that reads not connected); and completes the held read with its original
error. XP SP3, Vista and 7 set the device failed (`IoInvalidateDeviceState`,
`PNP_DEVICE_FAILED`) when `RESET_PORT` returns `0xC000009C` (XP SP3
`0x10A0C`, 7 x86 `0x11D02`). No build keeps a counter. `hidclass.sys` then
re-reads after a backoff of 1 s growing by 1 s to 5 s, with no limit (98 SE
4.10.2222 `KeSetTimer` `0x110D6`, cap `0x1122C`; 2000 SP4 5.00.2195.6655
`0x11769`; XP SP3 5.1.2600.5512 `0x120FF`, DPC `0x1238C`); on `0xC000009D`
98 SE re-reads at once (`0x110A7`), 2000 stops reading (`0x11724`), XP has
no case for it. So a HID device whose reads keep failing with any other
status gets one reset episode per failed read, without limit - an inference
from the two loops, not observed.

**`usbstor.sys`** reads the URB status: `USBD_STATUS(urb) == 4`
(`STALL_PID`) is cleared with `SYNC_RESET_PIPE_AND_CLEAR_STALL` and the
Bulk-Only protocol continues, a status-stage stall at most twice per command
(XP SP3 `0x118D7 cmp ecx,2`; 2000 `0x11769`; NUSB `0x11799`; 7 x86
`0x1412D`); the CBI path resets both bulk pipes. Every other failure - not
responding, CRC, babble alike - and a command timeout (XP SP3 `IoTimer`
`0x10E9C`) completes the request `SRB_STATUS_BUS_RESET` /
`STATUS_IO_DEVICE_ERROR` (`0xC0000185`) to the class driver and queues the
reset work item, which cancels the in-flight IRP and then sends up to three
rounds of `GET_PORT_STATUS` + `RESET_PORT`, ending at the first successful
reset or at a port that reads not connected (2000 SP4 `0x11A73`, XP SP3
`0x10E14`, XP x64 `0x11BD8`, Vista x86 `0x12FE3`, Vista x64 `0x157E0`, 7 x86
`0x12DB2`, 7 x64 `0x15B77`). A reset that still fails leaves the stack
failing every new request with `STATUS_DEVICE_DOES_NOT_EXIST`
(`0xC00000C0`; XP SP3 `0x10E32`, `0x11FF6`) until it is removed. NUSB's
4.90.3000.1 build, the 98 SE one, has no loop: one `GET_PORT_STATUS` and,
if connected, one `RESET_PORT` (`0x11B46`) per episode. No build sends
`ABORT_PIPE`. On NT 6.x an attempt whose context field reads 5 (7 x86
`0x12D13`) sleeps 100 ms and counts as failed; what that field is was not
read.

**`usbaudio.sys`** sends no port request. A failed isochronous or MIDI
completion marks the pin, and its next submission sends `ABORT_PIPE`, waits
for the pin's outstanding URBs, sends `SYNC_RESET_PIPE_AND_CLEAR_STALL`,
clears the mark and goes on streaming, with no counter (XP SP3 5.1.2600.5512
completion `0x151E2`, mark `0x1520C`, reset `0x1905A`; 98 SE 4.10.2222 mark
[pin+0x80] `0x13FA1`, reset `0x12C89` / `0x13EBB`; 2000 SP4 5.00.2150.1 the
same test before `0x15380` / `0x17480`).

**At a departure** (remove paths read on Windows 7 SP1 x86 only):
`HumRemoveDevice` sends `ABORT_PIPE`, `HumStopDevice` also the unconfigure;
`USBSTOR_FdoRemoveDevice` sends no URB. No class driver sends a port request
on removal (the census), and every counter found lives in the extension of
the stack being removed, so a re-enumerated device starts each driver
afresh.

**Against record 17's location budget of three re-enumerations:** no class
driver asks for one. What they ask for is `RESET_PORT`, which the parent hub
serves: `usbstor` at most three per episode (one on NUSB's 98 SE build) and
then gives up for the life of the stack; `hidusb` one per failed read with no
bound across reads, paced by `hidclass`'s 1-5 s backoff; `usbaudio` never.

**Not read:** Windows ME (no binaries in `tools\`); XP x64 and NT 6.x
`hidclass.sys` (not in `tools\`), so their resubmit policy; `disk.sys` /
`classpnp.sys` retry counts above `usbstor`'s `SRB_STATUS_BUS_RESET`, which
set how many `usbstor` episodes one failing read can cause; how the hub
serves `RESET_PORT` and whether it re-enumerates; XP x64, Vista and 7
`usbaudio` error paths (the byte census only) and where 2000 `usbaudio` sets
its mark; the x64 `usbstor` status-stage stall limits; the remove and stop
paths before Windows 7 beyond section 6.3's URB lists.

### 6.8 What a class driver is shown after a fault (`2.2.0.0`, design record 17)

Since `2.2.0.0` the bus answers a set of controller faults itself (design
record 17 sections 4.1 to 4.6, task 35-T), and the contract a device PDO
keeps with the class drivers above it after a fault is record 17 section
4.10's, in this order of precedence:

- **An error completion the class driver already gets for the same fault,
  and only after the bus has done what it does.** A USB Transaction Error on
  a bulk or interrupt endpoint with no streams, on a device not behind a
  TT and a controller not on Linux's `XHCI_NO_SOFT_RETRY` list, is retried
  in place up to three times per TD first (record 17 section 4.2); an
  exhausted retry completes exactly as up to `2.1.1.0`,
  `USBD_STATUS_DEV_NOT_RESPONDING` with the endpoint Halted,
  so section 6.7's reactions follow from it unchanged: `hidusb`'s port
  reset episode and `hidclass`'s backoff, `usbstor`'s up to three
  `RESET_PORT`s (one on NUSB), `usbaudio`'s abort and pipe reset. A retry
  whose Reset Endpoint fails is not completed that way: the TD is left to
  `hcdCfgFault` (record 17 section 4.2, "As built"), so the class driver
  sees it end with a controller recovery while the device is present, or
  with a departure where its hub path is already gone.
- **A departure and an arrival** where the bus cannot attribute the fault to
  a transfer or the location fails under the device: a completion code
  nothing claims or a halt with no TD on a non-isochronous endpoint (record
  17 section 4.3), a USB 2.0 root port the controller disabled, an
  over-current (section 4.5), and a
  controller contained as unreadable (section 4.6). The device's PDO is
  reported missing and, where the location allows, a new one is created by
  the re-enumeration; transfers end as on an unplug. A location whose
  budget is spent still loses the device at the next fault, and is then
  held with nothing enumerated (record 17 sections 4.3 and 4.5, "As
  built"). Section 6.7's last
  reading applies: every counter a class driver keeps lives in the stack
  being removed, so the re-enumerated device starts each driver afresh, and
  no class driver's `CYCLE_PORT` (none sends one) can spend the location's
  budget.
- **Never a new status, and never `STATUS_DEVICE_NOT_CONNECTED` on a PDO
  that is still listed.** Windows 98 SE's `hidclass.sys` resubmits on that
  status at once (section 6.7), so a contained controller parks new
  requests on their PDOs, cancellable, instead of refusing them, and a
  request it cannot park completes `STATUS_CANCELLED` or
  `STATUS_DELETE_PENDING` (record 17 section 4.6).

At `XhciTolerance` `0` none of the above acts and every path is
`2.1.1.0`'s (record 17 section 4.11). What each class driver then does is
section 6.7's static reading; 35-V reads it at run time against the
injected faults.

### 6.9 An interface with more endpoint descriptors than bNumEndpoints (task 36.46, 2026-10-10)

**The report** (GitHub issue 17 item 7, SweetLow, his `usbview` readings):
a low-speed HID boot mouse, VID `073A` PID `2230`, `bcdUSB` 1.10,
`bMaxPacketSize0` 8, one configuration of 41 bytes. Its interface 0 says
`bNumEndpoints` 1 and carries two endpoint descriptors after its HID
descriptor (9 + 9 + 9 + 7 + 7 = 41): `0x81` interrupt IN and `0x01`
interrupt OUT, both 8 bytes at `bInterval` 10. Under the EHCI's companion
stack it read Current Config Value 1 with one open pipe, `0x81`; under
`xhci98` it read Current Config Value 0 and no open pipe: addressed (7) and
described, never configured.

**The one failure point**, found by running those exact bytes through every
pure path as host vectors (`test_pipe` `test_walk_surplus`, `test_func`
`test_ls_mouse_surplus`, `test_xfer` `test_build_ls_report_descriptor`):
`XhciPipeFindInterface` refused the configuration as malformed when a
second endpoint descriptor followed the one `bNumEndpoints` declared, so
`hcdCfgSelect` answered `hidusb`'s SELECT_CONFIGURATION with
`USBD_STATUS_INVALID_PARAMETER` before any Configure Endpoint or
SET_CONFIGURATION, which is exactly the reading. The other paths take the
device as Microsoft's stack does: enumeration reads the 9-byte head and the
41 bytes without parsing them; one interface is never split (section 10.8),
and as a composite's function the filtered descriptor (10.9) carries the
interface's run, surplus included, to the same walk; the endpoint rules give
the IN endpoint at Low Speed Interval 6 (`bInterval` 10 ms rounded down to
8 ms, Table 6-12's FS/LS interrupt row), Max Packet Size 8, Max ESIT Payload
8, CErr 3, and the plan adds DCI 3 alone; the never-opened OUT endpoint would
be a legal Low-Speed interrupt OUT endpoint by the same rules (DCI 2,
Interval 6); hidusbf's rewritten `bInterval` reaches the counted endpoint on
both the whole-device and the function path (section 13.7); and the 239-byte
report descriptor read over an 8-byte EP0 is one Data Stage TRB, or across
a page a Data Stage TRB chained to a Normal TRB whose TD Size counts the
8-byte packets left (14 then 0 for the tested 128/111-byte split), each
with ISP set, the short-packet handling
the control path already has.

**The rule now.** An interface's endpoints are its first `bNumEndpoints`
endpoint descriptors. Endpoint descriptors after those, up to the next
interface descriptor, are surplus: counted (`EndpointSurplus`) and
otherwise ignored, neither their numbers nor their DCIs checked, because
nothing will be programmed from them. That is usbport's rule
(`external/reactos/usbport/device.c`: `USBPORT_OpenInterface` opens
`bNumEndpoints` pipes, taking the next endpoint descriptor for each, and
`USBPORT_GetInterfaceLength` skips the rest to the next interface; reference
source, not a disassembly), and the request a class driver sends is already
sized by it, since `usbd.sys`'s `USBD_CreateConfigurationRequestEx` gives
each interface `bNumEndpoints` pipe records; counting the surplus would have
answered that request `USBD_STATUS_BUFFER_TOO_SMALL` instead. The class
driver gets the same pipe set Microsoft's stack gave it. `xhciuas.sys`'s
own walk (`UasParseConfig`, `src/uas/uas_iu.c`) keeps the same boundary:
an endpoint past `bNumEndpoints`, and the companion and Pipe Usage
descriptors after it, name no UAS pipe and make the setting no SuperSpeed
one (`test_uas`).

**What stays refused**, because usbport's tolerance there would program
garbage or the walk could not be trusted: fewer endpoint descriptors than
`bNumEndpoints` (usbport's walk would take the next interface's endpoints;
this bus answers `INVALID_PARAMETER`), a `bNumEndpoints` above 30, a counted
endpoint numbered 0 or repeating a DCI, and every structural length rule of
the whole set - an endpoint descriptor shorter than 7 bytes refuses the
configuration whether it is counted or surplus. No endpoint the xHC is
given changes: the counted endpoints go through the endpoint rules
unchanged.

**The log.** A select that takes such an interface records, in every
flavour, `cfg.ep.surplus.vidpid` (VID in the high half and PID in the low,
as `dev.ss.vidpid` has them: `073A2230` for this mouse) and
`cfg.ep.surplus` (slot, interface, alternate and the surplus count, one
byte each), so an `XHCISNAP` report shows the tolerance taken - at
`XhciLogVerbosity` 2 or above, where the note ring is on.

**Owed.** QEMU cannot present this descriptor with its stock devices, so
the legs are regression legs only (the QEMU mouse, tablet, keyboard and a
stick on 98 SE and 7 x64); the device itself is a bench clause: the owner or
SweetLow retests that mouse on `2.2.0.3`, expecting Current Config Value 1,
one open pipe `0x81`, a moving pointer and the two log records.

### 6.10 An explicit StartFrame on a controller without Contiguous Frame ID (task 36.57, 2026-10-11)

**The report** (owner, the E7250 bench, 2026-10-11): a Creative Sound
Blaster Play! 2 (VID `041E` PID `323D`, Full Speed, audio and HID) played
only hiss through `2.3.0.0` on Windows 7 x64, at 44.1 and 48 kHz alike,
from the moment playback started until it stopped. The same device played
correctly on Windows' own USB 2.0 stack, and a C-Media audio device
(`0D8C:0014`) played correctly through this bus on the same machine. The
Play 2's playback endpoint `0x01` is isochronous OUT, 200 bytes, `bInterval`
1, asynchronous, and its 9-byte audio-class endpoint descriptor names a
feedback endpoint in `bSynchAddress`: `0x83`, isochronous IN, 3 bytes (the
10.14 format), `bInterval` 1, `bRefresh` 5. Interface 2 alternate 1 carries
both. The C-Media's playback endpoint names none.

**What the snapshot showed.** `XHCISNAP` at verbosity 3 during the hiss,
the `.BIN` read against `XHCI_EXTENSION`'s layout (its amd64 size,
132,944 bytes, is the dump's `ExtensionBytes`): `IsoSubmits` 0 and
`IsoPacketsAnswered` 0 - no isochronous TD had ever been on a ring - with
`TransfersSubmitted` 42 (control and interrupt), `TransfersCancelled` 0,
`CommandsIssued` 4,420 (10 in a capture of the same machine without the
device), 1,882 completions of code 27 (Stopped - Length Invalid), and
`HcInfo.Cfc` 0: this controller (`8086:9CB1`) does not report Contiguous
Frame ID capability (HCCPARAMS1 CFC, xHCI 4.11.2.5).

**What `usbaudio.sys` sends** (static: Windows 7 SP1 `USBAUDIO.sys` x86 and
x64, symbol names from the public symbols in `tools\symbols`;
`legal-provenance.md` section 4):

- `USBType1BuildIsochUrbRequest` gives a stream's data URB an explicit
  `StartFrame` from a running frame counter, with no
  `USBD_START_ISO_TRANSFER_ASAP`, when the stream has a feedback endpoint;
  otherwise it sets `TransferFlags` to 4 (ASAP). The counter starts at the
  current frame + 2 and advances by one a packet.
- `USBType1AsyncEndpointPoll` reads the feedback endpoint: `RESET_PIPE`
  (URB function 0x1E) on the feedback pipe, then a one-packet, 3-byte
  isochronous IN URB at an explicit `StartFrame` - the current frame + the
  refresh period (2^`bRefresh` frames, 32 here) + 2 at the start. The
  read's completion routine advances that frame by the refresh period and
  queues the next poll at once (`IoQueueWorkItem`, no timer), whether the
  read succeeded or failed: what spaces the reads 32 ms apart is the bus
  completing each one at the frame it named.

So every isochronous URB of an asynchronous Type I stream carries an
explicit `StartFrame` on Windows 7, and every URB of an adaptive or
synchronous one (the C-Media, QEMU's `usb-audio`) is ASAP - which is why
no virtual machine and no other device had reached this path.

**Why it failed.** Since batch (c)'s review (round 16, finding 2, and round
17), `hcdIsoPublish` refused an explicit `StartFrame` whose packets could
not all be given xHCI Frame IDs - no CFC, no frame sample, a packet outside
the IST+1 to 895 frame window, or a cadence the Interval does not share -
as `USBD_STATUS_BAD_START_FRAME`, rather than let the engine fall back to
Start Isoch ASAP (SIA), which moves the start. Its policy then was to give
a controller without CFC no Frame ID at all, so every one of these URBs was
refused before a TRB was built: the device was put in its streaming
alternate setting and sent nothing, and its output stage hissed. The
command storm is `usbaudio.sys`'s own reading of the feedback endpoint:
each `RESET_PIPE` on the running, idle isochronous endpoint is a Stop
Endpoint (whose event is the code 27) and a Configure Endpoint that drops
and adds it (`hcd_cfg.c`'s `RESET_PIPE`, unchanged), with each refused
data URB's abort and reset beside it. The sequence is normal - one reset
a read, every 32 ms when the reads complete at their frames - but with
every read refused nothing paced it except how soon each refusal
completed. It was not the fault.

**Why plain SIA is not enough.** The feedback read paces itself by its own
completion. Run as SIA, a read completes a frame or two after it is sent;
`usbaudio.sys` then asks for the next one 32 frames further on and sends
it at once, so the requested frame runs ahead of the bus by about 30
frames a read, and within some 28 reads it is further ahead than a Frame
ID can name and `hcdIsoAdmit` refuses it - each refusal one more reset,
with the reads no longer paced at all. A read must therefore run at the
frame it names, which needs a Frame ID.

**The rule now.** An explicit `StartFrame` that `hcdIsoAdmit` accepted - no
further ahead than a Frame ID can name (895 frames) and no further behind
than usbdi's start-frame range - is never refused for want of Frame IDs:

- With CFC, as before: a Frame ID on every TD where every packet can have
  one; otherwise SIA.
- Without CFC, a request that starts a data flow carries a Frame ID on its
  first TD and SIA on the rest, which is what xHCI 4.11.2.5 (p.200) allows
  on such a controller: "software may set the Frame ID (that is, SIA =
  '0') only in the first Isoch TD of an Isoch data flow, and shall set SIA
  = '1' in all subsequent Isoch TDs of the data flow". Only the first
  packet's frame is then asked of the window, and only on an endpoint
  whose Interval is one frame or less: a longer service interval needs
  that frame on an interval boundary (4.11.2.5.1), which this driver does
  not compute, so such a request stays SIA. The feedback read starts a
  flow every time, because the `RESET_PIPE` before it gives its endpoint a
  new context; so does a data stream's first URB after its alternate
  setting is selected, once its frame is at least IST+1 frames away.
- Without CFC, a request that continues a flow goes out with SIA, "one
  Interval after the last TD the endpoint ran", which for the data pipe's
  contiguous frame counter is the frame it asked for.
- Any request whose Frame ID the window cannot name (a first frame inside
  IST+1, or behind the current frame), or with no frame sample, goes out
  with SIA.

**When a flow starts** is kept per pipe (`HCD_PIPE.IsoFlowFresh`, its rules
`XhciXferIsoFlowNext`): set when a Configure Endpoint adds the endpoint's
context (a select, or a `RESET_PIPE`'s drop and add), and when a Ring
Underrun or Overrun - the controller finding the ring empty at a service
interval, which ends the flow - is handled with nothing of the pipe's
queued; cleared by every publication. An empty queue alone is not taken
as the end of a flow: the last TD's completion can be handled before the
controller reaches the empty ring, and until then the flow is running and
a Frame ID would be one the paragraph above forbids. An Underrun handled
with something queued is either old - a request published after the
controller posted it is still queued when it is handled, since its
completion comes later on the same event ring - or a restart the doorbell
makes; it starts nothing. An abort's drained queue is not taken as one
either. Linux takes no Frame ID at all without CFC
(`external/linux/xhci-ring.c`, `xhci_queue_isoc_tx`: SIA by default, a
Frame ID only when the controller has CFC; reference source), which does
not pace a client that waits for its read to complete at the frame it
named.

What SIA does not keep, wherever it is taken, is the client's frame: a
request runs at the controller's next service after the last TD the
endpoint ran - early by as much as its start was ahead (up to 895 frames),
late by as much as it was behind, and with any gap the client left between
two requests closed. That is the cost of every SIA case above, not only of
a stream's first request. The URB's `StartFrame` is left as the client gave
it, and the per-packet `Length` and `Status` are written as for any other
request (section 6.5; for an IN packet, the bytes received).

**What stays refused:** a `StartFrame` more than 895 frames ahead, or
further behind than the range, still `BAD_START_FRAME` at admission; every
other isochronous refusal is unchanged.

**The rest of the device's path was checked and needs no change.** The
9-byte endpoint descriptors parse as 7-byte ones (`bRefresh` and
`bSynchAddress` are not read); `0x83` gets Interval 3 (1 ms), Max Packet
Size and Max ESIT Payload 3, type Isoch IN; `0x01` Interval 3 and Max ESIT
Payload 200; interface 2 alternate 1's two endpoints, with the class-specific
descriptors between them, are both opened, in order; a 3-byte packet at
offset 0 of a 3-byte buffer passes `XhciPipeIsoCheck` against Max ESIT
Payload 3, and one byte more is refused. Host vectors in `test_pipe` and
`test_td`, the latter with 300 feedback reads in a row each at its Frame ID
and the lead steady inside the window.

**The log.** In the note ring (verbosity 2 or above), the first four
first-TD Frame IDs given on a controller without CFC since the driver
loaded for the controller record `iso.fid1`, and the first four SIA
publications of an explicit `StartFrame` record `iso.sia`
(`HCD_CONTROLLER.IsoStartsFirstFrameId` and `IsoStartsSia` count them all).
Both carry the slot in bits 31:24, the DCI in 23:16, the case in 15:12 (5
a first-TD Frame ID; 2 no CFC and a flow under way, 3 no frame sample, 4 a
frame the window cannot name, a cadence the Interval does not share with
CFC, or an Interval longer than a frame without it) and the low 12 bits of `StartFrame` minus the current frame. A dump
counts a request with any Frame ID in `IsoSubmitsWithFrameId`.

**Owed.** QEMU has no asynchronous audio device with a feedback endpoint,
and its xHCI reports no CFC either, so the virtual-machine legs are
regression legs only (QEMU's `usb-audio`, adaptive and ASAP, on 98 SE and
7 x64 playing at the same timing; a stick copy). The device itself is a
bench clause, and so is the controller's honouring of a first-TD Frame ID
without CFC, which no reference driver exercises: on the E7250 the Play 2
plays a tone without hiss at 44.1 and 48 kHz, `iso.fid1` records for the
feedback endpoint's DCI 7 with a lead near 34, and Stopped events grow at
about 31 a second and commands at about twice that - one `RESET_PIPE` a
32 ms read, not faster.

**Read on the bench** (owner, 2026-10-11, the E7250, Windows 7 x64, the
build of `dad964f` installed by replacing the file): the Play 2 played
cleanly at 44.1 and 48 kHz; `iso.fid1` for DCI 7 at leads 34 and then 32,
`IsoPacketsAnswered` 13,462, the Stopped events at one a feedback read. The
feedback endpoint answered about half its reads with a USB Transaction
Error (202 in some 415), reported per packet and skipped by the class
driver, with nothing escalated; why is not established (issue 14, section
7). A listen of some 4.75 minutes stayed clean and read the same steady
half (4,441 of 8,922 reads, 49.8 %), not growing. The flow tracking of review round 2 (`IsoFlowFresh`) came after that
build; on this device it gives the same Frame IDs, since every feedback
read follows a `RESET_PIPE`'s new context.

### 6.11 The Windows 98 SE and ME silent install (task 38.2, 2026-10-13)

**Status: design approved by the owner (2026-10-13) after three Codex
review rounds, the third with no P1 or P2; code follows. O1 stays a fact
for the first 98 SE leg to read.** The rule and the
decisions are the owner's (roadmap-hcd task 38.2, 2026-10-12 and
2026-10-13, the driver's own gate of 2026-10-13 included) and are not
reopened here; what this section adds is where each part lives in the bus,
and the questions it could not settle, marked **Open** below. The
information on how Windows 98 decides a silent install, and the list
itself, are SweetLow's; the README, the release notes and the readme
template credit him for both (owner, 2026-10-13).

**Today.** Every PDO the project creates answers `SilentInstall` FALSE in
`IRP_MN_QUERY_CAPABILITIES`: the device, function, composite-parent and hub
PDOs in `hcd_pdo.c` (`hcdDeviceCapabilities`), the root-hub PDO in
`hcd_rh.c`, and `xhciuas.sys`'s LUN PDOs in `src\uas\uas_pdo.c`
(`uasCapabilities`). On Windows 98 SE a device whose class INF is the OS's
own therefore brings the New Hardware wizard at every new devnode - a known
device on another port, behind another hub - even when nothing need be asked;
the devnodes of this package's own INFs (the root hub, `XHCI98\HUB`,
`XHCI98\COMPOSITE`) were read installing with no wizard already (run-36,
the 98 SE K2 and hub rows; run-37 for ME).

#### 6.11.1 The rule

A device PDO answers `SilentInstall` TRUE when the driver's gate is on
(6.11.3) and any interface descriptor of its configuration matches an entry
of `HKLM\System\CurrentControlSet\Services\USB\SilentClasses` - the key
already in use on these systems, whose name Windows 98 SE's own `usbhub.sys`
carries (`USB\SilentClasses`, section 10.6's fragment list) - so an existing
`.reg`, and a machine that merged one, work unchanged once the gate is on.

- **An entry** is a value of that key, its name ignored, of type
  `REG_BINARY` and exactly 4 bytes: byte 0 the control byte, then
  `bInterfaceClass`, `bInterfaceSubClass` and `bInterfaceProtocol`.
- **The control byte:** `0x80` - the entry applies to a device that is not
  composite; `0x40` - to one that is; `0x01` - compare the subclass; `0x02` -
  compare the protocol. The class is always compared. Both `0x80` and `0x40`
  set applies either way.
- **Ignored entries:** one with neither `0x80` nor `0x40`; and, as this
  design reads "a 4-byte `REG_BINARY`", one of any other type (`REG_DWORD`,
  a string, `REG_NONE`) or any other length. The other control bits (`0x3C`)
  are not read. An ignored entry takes no room in the table (6.11.3) and is
  counted.
- **An interface descriptor matches an entry** when the entry applies to
  the PDO's kind (6.11.2) and its class is equal, and its subclass and
  protocol are equal where the entry's bits say to compare them. Every
  interface descriptor within the configuration's `wTotalLength` counts
  (6.11.5), alternate settings included; the configuration matches when any
  one of them matches any one entry.

**Windows 98 SE and ME only** (owner): never on Windows 2000 and later. The
test is the one the bus already uses for them,
`!IoIsWdmVersionAvailable(1, 0x10)` (WDM 1.00 on 98, 1.05 on ME, 1.10 from
2000; `hcd_pdo.c`, `hcd_door.c`). On an NT target neither the gate nor the
key is read, the table stays empty, and every PDO answers FALSE as today.

**Off unless both are there** (owner, 2026-10-12 and 2026-10-13): the gate
and the list are each off by default, and the INFs write neither. With the
gate off - its value absent, 0, any value but 1, not a DWORD, or unreadable
(6.11.3) - the `SilentClasses` key is not even opened and every PDO answers
FALSE whatever the list holds, so a list merged for another stack, or left
from one, silences nothing here. With the gate on, a missing key, an empty
one, a key the query cannot read whole (6.11.3), or a list whose every entry
is ignored, means an empty table, and nothing is silent.

#### 6.11.2 What each PDO answers

"Composite" means what the bus's own split decides (section 10.8,
`XhciFuncSplit`): the device's PDOs are a composite parent and its function
PDOs, and nothing else is composite - not a multi-interface device the bus
leaves whole (several configurations, a device class of its own, more than
16 functions), which is "not composite" and takes the `0x80` entries. In
the PDO, composite is `Composite || Function` (`hcd_pdo.c`: `Composite` is
set on the group's first PDO of a split device, `Function` on each function
PDO by `hcdPdoNew`).

| PDO | Configuration walked | Entries that apply |
|---|---|---|
| A device PDO the bus did not split | its whole configuration (`Config`) | `0x80` |
| A hub's PDO (`XHCI98\HUB`, `XHCI98\HUB30`; 10.11) | its whole configuration; a hub is never split (`bDeviceClass` 9) | `0x80` |
| The composite parent (`XHCI98\COMPOSITE`; 10.12) | the device's whole configuration | `0x40` |
| Each function PDO of a split device | its own filtered configuration (`Config` of a function, 10.9): its interfaces only | `0x40` |
| The root-hub PDO (`hcd_rh.c`) | none | always FALSE |
| `xhciuas.sys`'s LUN PDOs (`uas_pdo.c`) | none: a disk, not a USB interface | always FALSE |

So a function's answer depends on its own interfaces alone, never on its
siblings'; and the composite parent answers TRUE when any of its functions'
interfaces match a `0x40` entry, which is the rule read literally over the
whole configuration. **Decided (O3, owner 2026-10-13):** any, as written, not only when every
function does, nor never. Its devnode is this
package's own INF's and was read installing silently on 98 SE with
`SilentInstall` FALSE (run-36, K2), so its answer is expected to change
nothing there; the legs read it. The hub row is in the same position: the
hub entry is the list's, and whether it changes anything for this bus's own
hub devnode is read on the legs, not assumed.

**A storage interface under UAS.** A dual-mode drive's interface has a
Bulk-Only alternate 0 (`08/06/50`) and a UAS alternate (`08/06/62`), and the
bus presents one transport (section 10.7, `xhci_xport.h`). Under the rule as
written, the Bulk-Only alternate matches the list's storage entry even when
the PDO is presented as UAS and bound by `xhciuas.inf` - the one class the
list leaves out because this package's own INFs install it. On a machine
where `xhciuas.inf` has never been installed, that drive would then be
installed silently with no driver, the very case the readme warns against.
**Decided (O2, owner 2026-10-13):** a PDO whose transport is `XHCI_XPORT_UAS` or
`XHCI_XPORT_REFUSED` answers FALSE whatever the list says; a PDO presented
Bulk-Only (`XHCI_XPORT_BOT` or `NONE`) follows the rule. The transport read
is the PDO's final one (6.11.4): `hcdXportDecide`'s on the creation path,
the old PDO's copied one on the re-presentation path.

#### 6.11.3 Read once per controller start, into the bus's own storage

**Where.** A new static `hcdReadSilent` in `hcd_ctl.c`, called from
`hcdStartBody` right after `hcdReadValues` - after the delivery is settled
and the BAR mapped, before the interrupt is connected, the controller run
and the controller thread created. The gate and the table live in
`HCD_CONTROLLER` (the FDO extension), not in `XHCI_EXTENSION`, so
`XHCISNAP`'s dump layout is untouched; a machine with two controllers reads
both once for each. No per-device registry read and no allocation (owner).

**The gate** (owner, 2026-10-13). On 98 SE and ME only, first:
`XhciSilentInstall`, a `REG_DWORD` under the controller's driver key, read
by `hcdReadDword` - the read every other `Xhci*` value of the start takes
(`IoOpenDeviceRegistryKey(hc->Pdo, PLUGPLAY_REGKEY_DRIVER, ...)`, then
`RtlQueryRegistryValues` with `hcdTakeDword`, which refuses any type but a
4-byte `REG_DWORD`). On Windows 98 SE and ME that key is the controller's
class key, `HKLM\System\CurrentControlSet\Services\Class\USB\NNNN`, the key
`portsw.vbs` writes `XhciIntelPortSwitch` to. **Only a DWORD of exactly 1 is
on**; absent, 0, any other number, another type or a failed read is off.
The tree's switches read both ways (checked in `hcd_ctl.c`:
`XhciIntelPortSwitch` is on at any nonzero DWORD, `XhciWideMap` at anything
but 0, `XhciSelectiveSuspend` only at exactly 1); this gate takes
`XhciSelectiveSuspend`'s rule, because its on direction is the one that can
leave a device unworking with nothing said, so a stray value is taken as
off. **Decided (O6, owner 2026-10-13).** The INFs write no value: absent is off, and an INF reinstall
then cannot undo what `silent-on.bat` set. Read at every start, so
`silent-off.bat`'s 0 takes effect at the next. On NT it is never read. The
notes, in every build flavour, are `XhciLogNote` records like the start's
other values, so they reach the note ring only when `XhciLogVerbosity` is 2
or more (`XHCI_LOG_VERBOSITY_RING`; below it the ring is off, `xhci_log.c`),
the channel 38.1's `SNAPON.BAT` turns on (`XHCISNAP -verbosity 2`):
`silent.gate.value` (the DWORD read, or `0xFFFFFFFF` when none was read -
absent, another type, unreadable) and `silent.gate` (0 or 1), on 98 SE and
ME only.

**The table** is fixed-size, a pure-core structure (`XHCI_SILENT`,
`xhci_silent.h`) embedded in `HCD_CONTROLLER`: `Entry[XHCI_SILENT_MAX][4]`
(`UCHAR`), `Count`, and two counts for the log, `Ignored` and `Dropped`;
beside it the controller keeps `SilentGate` (0 or 1) and the query's
`SilentStatus`. **The cap is 32 entries**, 128 bytes of non-paged
extension: the package's list is six and SweetLow's the same six; 32 is
five times that, enough for any hand-extended list this design can foresee,
and a match over it costs at most 32 comparisons an interface descriptor,
once per PDO. **Past the cap** each further valid entry is dropped and
counted, in the order the query enumerates the values, which the bus does
not choose. Dropping can only remove silence - the wizard comes, as today -
which is the safe direction, and the note ring says it happened.
**Decided (O5, owner 2026-10-13):** 32.

**The read.** On every target first `XhciSilentReset` (`Count`, `Ignored`
and `Dropped` to 0) and `SilentGate` 0, so a gate turned off or a key
deleted between two starts takes effect at the second. Then, on 98 SE and
ME, the gate; and only with the gate on,
`RtlQueryRegistryValues(RTL_REGISTRY_SERVICES, L"USB\\SilentClasses",
table, hc, NULL)` with one table entry: a query routine
(`hcdTakeSilent`), `Name` NULL - which asks the routine for every value of
the key - and `RTL_QUERY_REGISTRY_NOEXPAND`, not `REQUIRED`. The routine
hands each value's type, data and length to the pure admission
(`XhciSilentAdd`, 6.11.7), which keeps it, ignores it or drops it, and
always returns `STATUS_SUCCESS`, so one bad value ends nothing.

**A query that does not succeed leaves an empty table.** When the query's
status is anything but `STATUS_SUCCESS`, `XhciSilentEnd(&hc->Silent, 0)`
sets `Count` to 0 and keeps `Ignored`, `Dropped` and the status for the
notes. An absent key is that failure before any value; the case the rule
closes is a query that has called the routine for some values and then
fails (a value it cannot read, a buffer it cannot size), which would
otherwise leave a partial table that silences by whichever values came
first. A list read in part is not the list, and the failure's direction is
the wizard, as today. The start's notes record the query's status, the
count kept (after `XhciSilentEnd`), the count ignored and the count dropped
(`silent.status`, `silent.entries`, `silent.ignored`, `silent.dropped`), in
every build flavour when the verbosity is 2 or more, with the gate on only:
with it off the two gate notes are all there is, and their absence on NT is
how a reading at verbosity 2 tells the three apart. Below verbosity 2 no
`silent.*` note is written anywhere, so an empty ring says nothing about the
gate.

**Imports.** Every routine the read needs is already on
`xhci98-imports.allow` with no `SITES` limit: `RtlQueryRegistryValues`
(stock 98 SE precedent, `uhcd.sys`, `usbhub.sys` and `usbd.sys` 4.10.2222;
Appendix A row 170), `IoIsWdmVersionAvailable`, and the gate's
`IoOpenDeviceRegistryKey` and `ZwClose`, which `hcdReadDword` imports
already. `RTL_REGISTRY_SERVICES` on Windows 98 is the 2a guest's runtime
reading of NUSB's `usbport.sys` taking `Services\USB\DisableSelectiveSuspend`
through `RtlQueryRegistryValues(RelativeTo = Services, L"usb", ...)`
(`lessons.md`). No allowlist row changes.

**Open (O1): the enumeration on NTKERN.** That Windows 98 SE's and ME's
`RtlQueryRegistryValues` call the routine for every value when `Name` is
NULL has not been read; every use in this tree names its value. The legs
read it (`silent.entries` 6 with the list merged and the gate on), and a
static reading of how 98 SE's `usbhub.sys` reads `USB\SilentClasses` would
be the precedent. If NTKERN does not enumerate, the routes that remain need
the owner: the NT route is `ZwOpenKey` plus `ZwEnumerateValueKey`, and
`ZwOpenKey` has stock 98 SE evidence (Appendix A row 157) but no row on the
allowlist, while `ZwEnumerateValueKey` has no Appendix A row at all - no
Windows 98 evidence recorded; or a query by name of values `"0"` to `"31"`,
which reads the package's list and SweetLow's but not the rule's "name
ignored". Neither is assumed here.

#### 6.11.4 When the list is read, against when PDOs exist

Device PDOs are created only by the controller thread, which a start
creates after the read; so every PDO of a start is created against that
start's gate and table, and no device of a start is enumerated before its
read. **The decision is taken once the PDO's fields are final, before the
PDO is published, and kept in it**: a new `HCD_DEVICE_PDO.SilentInstall`
(`ULONG`, 0 or 1), written by one helper, `hcdSilentDecide(hc, pdo)`, from
the PDO alone - its `Config` and `ConfigLength`, its kind (`Composite ||
Function`), its final `Xport.Transport` - and the controller's gate and
table, through the pure `XhciSilentPdo` (6.11.7). It is not taken in
`hcdPdoNew`: neither the composite parent's `Composite` (set by the caller
after `hcdPdoNew` returns) nor the re-presented function's transport
(copied over `hcdPdoNew`'s by the caller) is final there.

`hcdPdoNew` has two callers, and the helper is called on both paths
(checked in `hcd_pdo.c`; there is no third):

- **`HcdDevicePdoCreate`**, for every PDO of the group, after the creation
  loop - once the first PDO's `Composite` is set and every PDO's transport
  is `hcdXportDecide`'s - and before the dormant preview and the listing
  under `PdoListLock`. Nothing later in that function writes a transport
  (`hcdXportRefusal` reads `XHCI_XPORT_REFUSED`, it does not set it). A
  group that revives dormant PDOs on 98 SE or ME deletes these, and their
  answers with them.
- **`HcdDevicePdoRepresentFunctions`** (task 36.43, the replacement of
  each function PDO PnP let go with its composite parent's removed FDO),
  for each replacement, after its identity is copied from the old PDO -
  `Xport` among it - and before the `PdoListLock` hold that lists it in the
  old one's place. It is decided again rather than copied from the old
  PDO, so that one helper is the field's only writer: the same rule over
  the same filtered configuration and the same transport gives the old
  answer while the gate and table are the same, and the devnode is
  installed already, so the answer decides nothing there either way. A
  function PDO is composite. A composite parent's disable and enable in
  Device Manager on 98 SE does not reach this path (38.2's legs, 6.11.11):
  PnP took the same function PDO back and nothing was decided again.

Like the PDO's other creation fields, it is never changed after the PDO is
listed. The query copies it.

What that leaves:

- **A PDO kept across a controller stop** (dormant, revived on the next
  start, sections 5.2 and 10.11) keeps the answer it was created with,
  against the earlier start's gate and table. Its devnode is already
  installed, so the answer no longer decides anything; on 98 SE and ME a
  revived group keeps its old PDOs and deletes the new ones
  (`HcdDevicePdoCreate`), whose answers are discarded with them.
- **A list merged, changed or deleted, or the gate changed, while the
  controller runs** is seen at the next controller start: a restart, or the
  controller's disable and enable. A device plugged before that is answered
  by the old gate and table. The readme says to restart.
- A hub FDO's start reads nothing: the gate and table are the controller's.

#### 6.11.5 The match, the descriptor walk and malformed descriptors

The match is pure: `XhciSilentWalk(entries, count, config, length,
composite)` returns `XHCI_SILENT_MATCH`, `XHCI_SILENT_NOMATCH` or
`XHCI_SILENT_MALFORMED`, and `XhciSilentMatch` with the same arguments
returns exactly 1 for a match and 0 otherwise. It walks the PDO's held copy
of its configuration descriptor (`Config`, `ConfigLength`, the copy the PDO
already carries so it may outlive the device record):

- **What the buffers are.** A device PDO's `Config` is the device's
  (`hcdPdoNew` copies `dev->Config`), and that is the bytes of the second
  `GET_DESCRIPTOR(CONFIGURATION)`, asked for the `wTotalLength` the 9-byte
  head reported and cached only when exactly that many came back
  (`hcd_enum.c`, `XHCI_ENUM_ACT_GET_CONFIG`; `xhci_enum.c`, state
  `CONFIG9`). Nothing there checks the header's `bLength` or
  `bDescriptorType`, and the copy's own `wTotalLength` is the second read's,
  which a device may make differ from the head's. A function PDO's `Config`
  is `XhciFuncConfig`'s, which refuses an unsound configuration and writes
  `wTotalLength` equal to the length it builds.
- **The header first.** `length` at least 9; byte 0 (`bLength`) at least 9;
  byte 1 (`bDescriptorType`) 2; `wTotalLength` (bytes 2 and 3) at least 9,
  at least the header's `bLength`, and **at most `length`**. Otherwise
  malformed. These are the checks `xhci_func.c`'s `xhciFuncTotal` makes
  before a split, restated in `xhci_silent.c` because they are static
  there.
- **Only `wTotalLength` is walked.** From the header's `bLength` to
  `wTotalLength`; bytes of the buffer past `wTotalLength` are never read,
  so an interface descriptor there does not count.
- **Every descriptor after the header**: the walk needs two bytes at each
  offset, so a single byte left before `wTotalLength` is malformed; a
  `bLength` below 2 (no progress) or running past `wTotalLength` is
  malformed; an interface descriptor (type 4) shorter than 9, and an IAD
  (type 11) shorter than 8, are malformed, as in `xhciFuncSound`. Any other
  type is walked over unread (endpoints, class-specific descriptors).
- **An interface descriptor's** class, subclass and protocol are its bytes
  5, 6 and 7.
- **A malformed configuration answers 0**, whatever matched before the
  fault: the walk does not stop at the first match, so a configuration is
  silent only when it was walked whole and matched. Silence is the
  direction that can leave a device unworking with nothing said, so a
  configuration the bus cannot read whole is not given it. **Decided (O4, owner 2026-10-13):**
  0, not a match found before the fault.
- `config` NULL or `length` 0: 0 (malformed).

**The two traps** (roadmap 38.2), designed out where they arise:

- **An absent list is never dereferenced.** `XhciSilentWalk` returns
  `XHCI_SILENT_NOMATCH` before it reads an entry or a descriptor when
  `count` is 0, and also when `entries` is NULL whatever `count` says; the
  caller does not call it on NT or with the gate off at all. In the bus the
  table is an array in the extension, so there is no pointer to be NULL,
  but the pure function does not rely on that, and the host tests call it
  with a NULL table.
- **A flag is masked before it becomes a `BOOLEAN`.** The entries are kept
  as four `UCHAR`s, never packed into a `ULONG` whose `(BOOLEAN)` cast
  would keep the low byte - the control byte itself, nonzero for an
  ignored `0x01` entry - or lose a bit above it. Each test is written
  `(control & mask) != 0`, `XhciSilentMatch` returns exactly 0 or 1, and
  the capability is written `caps->SilentInstall = pdo->SilentInstall ?
  TRUE : FALSE`.

**The PDO's answer** is `XhciSilentPdo(gate, entries, count, config,
length, composite, storageBarred)`, `storageBarred` being the caller's
`Transport == XHCI_XPORT_UAS || Transport == XHCI_XPORT_REFUSED`. It
returns a reason: `XHCI_SILENT_OFF` (gate 0), `XHCI_SILENT_EMPTY` (no
entries), `XHCI_SILENT_BARRED` (the transport, O2, before any walk), or the
walk's result; the PDO's `SilentInstall` is 1 for `XHCI_SILENT_MATCH`
alone. With the gate on, `hcdSilentDecide` notes each PDO it decides as
`silent.pdo`, in every build flavour when the verbosity is 2 or more (the
start's notes' rule, 6.11.3; below 2 the decision is made and not noted):
`(RootPort << 24) | (first interface, or
0xFF for a whole device or a parent, << 16) | (reason << 8) | answer`, so
the bus's answer is read off the note ring with `XHCISNAP -report` (or
`SNAPGET.BAT`), not inferred from the wizard. A route behind hubs is not
in the value; the order of the notes and the `dev.*` notes beside them
place it.

#### 6.11.6 IRQL and locking

- **The read** (`hcdReadSilent`, the gate's `hcdReadDword`,
  `hcdTakeSilent`, `XhciSilentAdd`, `XhciSilentEnd`): PASSIVE_LEVEL, in the
  start body, on the PnP thread's START. It writes the gate and the table
  with no lock: the previous start's controller thread has exited (the stop
  waits for it), this start's does not exist yet, and nothing else reads
  them.
- **The decision** (`hcdSilentDecide`, from `HcdDevicePdoCreate` and
  `HcdDevicePdoRepresentFunctions`): the controller thread, PASSIVE_LEVEL,
  reading the gate and table with no lock - they are written only while no
  thread exists - and writing a PDO not yet listed. No spin lock is held for
  the walk; on both paths it runs before the `PdoListLock` hold that lists
  the PDO.
- **The query** (`hcdDeviceCapabilities`): PASSIVE_LEVEL, as every
  `IRP_MN_QUERY_CAPABILITIES`, reading the PDO's `SilentInstall` with no
  lock: it was written before the PDO was listed under `PdoListLock`, on
  both paths, the publication every other creation field relies on.
- The pure half (`xhci_silent.c`) is DDK-free, C89, IRQL any.

#### 6.11.7 The pure core and its host tests

`src\xhci_silent.c` and `xhci_silent.h` (the table `XHCI_SILENT`;
`XhciSilentGate`, `XhciSilentReset`, `XhciSilentAdd` and `XhciSilentEnd`;
`XhciSilentWalk`, `XhciSilentMatch` and `XhciSilentPdo`), with
`test\test_silent.c` in `test\run-host-tests.cmd`, as the other pure halves
are (`test_func`, `test_xport`). `XhciSilentGate(found, value)` is the
gate's rule: 1 only when a DWORD was found and it is 1. The vectors:

- **The gate:** absent, another type, 0, 2, `0xFFFFFFFF` all 0; a DWORD 1
  is 1.
- **Admission:** a 4-byte `REG_BINARY` with `0x80`, `0x40` and `0xC0` kept;
  `0x00`, `0x01`, `0x02`, `0x03` and `0x3F` ignored; `REG_DWORD` of 4
  bytes, `REG_SZ` and `REG_NONE` ignored; lengths 0, 3 and 5 ignored; NULL
  data ignored; an ignored entry takes no slot; the 33rd valid entry
  dropped and counted, the first 32 kept in order.
- **A failed enumeration:** one entry kept and one ignored, then
  `XhciSilentEnd(table, 0)`: `Count` 0, `Ignored` 1 kept, and a match
  against a configuration the kept entry matched is 0; the same with
  `XhciSilentEnd(table, 1)` keeps the entry and matches; `XhciSilentReset`
  after a full table empties it and its counts.
- **Control bits:** `0x80` with class only matches any subclass and
  protocol; `0x81` compares the subclass and not the protocol; `0x82` the
  protocol and not the subclass; `0x83` both; a class mismatch never
  matches.
- **Composite:** a `0x80` entry does not match with `composite` 1, a `0x40`
  entry does not match with 0, `0xC3` matches with either.
- **The package's six** against a Full Speed hub (`09/00/00`), a High Speed
  multi-TT hub (`09/00/02`), a SuperSpeed hub (`09/00/03`), a boot keyboard
  and a boot mouse alone and as functions, a non-boot HID (`03/00/00`, no
  match), a Bulk-Only stick alone and as a function, a CBI floppy
  (`08/04/00`, no match), and a dual-mode drive's two alternates (match on
  alternate 0).
- **Subclass and protocol:** a match on a non-zero alternate setting only;
  a match on the last interface of several.
- **The transport (O2):** `XhciSilentPdo` with `storageBarred` 1 over the
  dual-mode drive's configuration and the package's six is
  `XHCI_SILENT_BARRED` and answer 0, for the UAS case and the refused case
  alike (the caller's two transports map to the one flag, and the helper's
  mapping is read on the legs); with `storageBarred` 0 it matches. Gate 0
  is `XHCI_SILENT_OFF` with a matching table.
- **Empty and absent lists:** `count` 0 with a real table, a NULL table
  with `count` 0, and a NULL table with `count` 3 - all 0.
- **The header:** `bLength` 8; `bDescriptorType` 4 at offset 0;
  `wTotalLength` 8; `wTotalLength` below the header's `bLength` - each
  malformed, with a matching interface after it.
- **The two lengths:** `wTotalLength` greater than `length`, a matching
  interface wholly inside the buffer - malformed, 0; `length` greater than
  `wTotalLength` with the matching interface only past `wTotalLength` - no
  match, 0; `length` greater than `wTotalLength` with the match inside and
  junk past it - 1.
- **Malformed descriptors:** `bLength` 0, `bLength` 1, a descriptor
  running past `wTotalLength`, a single trailing byte before `wTotalLength`
  after a matching interface, a type-4 descriptor of 5 bytes, an IAD of 7
  bytes, a matching interface before a malformed descriptor (0), and a
  matching interface after a long class-specific descriptor (1); `config`
  NULL and `length` 0.
- **The `BOOLEAN` trap:** every `XhciSilentMatch` result is exactly 0 or 1,
  including for an entry `0x83` that matches, and an entry `0x01` (ignored)
  never yields a nonzero result.

#### 6.11.8 `tools\silent\`: the list, and the switch's scripts

The package carries the list and the scripts that set the gate in their own
directory, `tools\silent\`, under task 38.9's `tools\` (owner, 2026-10-13,
replacing the `release-x86` placement of 2026-10-12): never in a flavour
directory, never at the version root, and `Assert-ToolsLayout`'s list of
tool directories names it. Four files, sources under `src\` as
`portsw.vbs`'s are, copied once by the release cut:

| File | What it is |
|---|---|
| `SILENT.REG` | the list: six entries, `REGEDIT4` |
| `SILENT.VBS` | shows or sets `XhciSilentInstall` on every xhci98 controller, and shows the list |
| `silent-on.bat` | merges `SILENT.REG`, checks the list read back, and only then sets the gate to 1 |
| `silent-off.bat` | sets the gate to 0 and leaves the list |

**`SILENT.REG`**, under
`[HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB\SilentClasses]`:

| Value | Bytes | Control byte | Matches |
|---|---|---|---|
| `"0"` | `hex:80,09,00,00` | `0x80`: not composite; class only | any hub (`09`, every protocol: Full Speed `00`, High Speed single- and multi-TT `01`/`02`, SuperSpeed `03`); hubs are never composite |
| `"1"` | `hex:43,03,01,01` | `0x40` + `0x01` + `0x02`: composite; subclass and protocol | a boot keyboard function (`03/01/01`) |
| `"2"` | `hex:43,03,01,02` | the same | a boot mouse function (`03/01/02`) |
| `"3"` | `hex:c3,08,06,50` | `0x80` + `0x40` + `0x01` + `0x02`: alone or composite; subclass and protocol | Bulk-Only SCSI storage (`08/06/50`) |
| `"4"` | `hex:83,03,01,01` | `0x80` + `0x01` + `0x02`: not composite | a boot keyboard alone |
| `"5"` | `hex:83,03,01,02` | the same | a boot mouse alone |

The values and their names are those of SweetLow's `SilentClasses.reg`
(in `usb20_win9x.zip`, `tools/usb20_win9x.zip`, extracted to
`tools\sweetlow-extracted\SilentClasses.reg`; the file is 214 bytes,
SHA-256 `4DB099B600F0E45C82AF8F9A9E56745C6245093FCCDE574220FD610B12B523E3`,
read as text, static; `legal-provenance.md` section 4 has the row). Keeping
the same names means merging one over the other leaves six values, not
twelve: names are ignored by the rule, but a duplicate would take a table
slot. The file in `tools\silent\` is written by this project from those six
facts; it is not a copy of his file. Protocol is compared in the HID
entries so a non-boot HID device (`03/00/xx`), whose class driver may be a
vendor's, is never silent, and in the storage entry so the UAS alternate
(`08/06/62`) is not matched by it; the hub entry compares the class alone
because every hub binds the same way. **UAS is not listed** (owner):
`xhciuas.inf` is this package's own, and its first install is the
wizard's (but see O2).

**`SILENT.VBS`** is `portsw.vbs`'s pattern (`src\portsw.vbs`, roadmap-hcd
task 36.42), for the one value `XhciSilentInstall`: Windows Script Host and
VBScript, because 98 SE and ME have them and no `reg.exe`; the controller
keys found as `portsw.vbs` finds them - every `NNNN` under
`HKLM\System\CurrentControlSet\Services\Class\USB\`, listed from the section
headers of a `regedit /e` export of that key (no WMI on a stock 98), or
probed `0000` to `9999` when the export gives nothing, and kept when its
`DriverDesc` is the controller's (`xHCI98 USB 3.x eXtensible Host
Controller`) and its `NTMPDriver` is `xhci98.sys`. Arguments `show`, `1` and
`0`, or a double-click that shows and asks. Exit codes: 0 done; 1 refused
or nothing found (no controller, NT); 2 usage, as `portsw.vbs`'s; and two
of its own, **3 partial** (the DWORD written and read back on some
controllers and not on others), **4 list not verified** (`1` only:
nothing written) and **5 not verified** (a `RegWrite` that returned no error
on some controller whose `RegRead` then failed or read back another value:
that controller's value may have changed). `show` prints, for each controller, the value and what
the driver makes of it (exactly 1 on; missing, 0, another number or type
off), and the list: a `regedit /e` export of the `SilentClasses` key read
for its value lines (Script Host cannot list a key's values itself),
saying whether the six of `SILENT.REG` are there with their type and bytes
and naming every other value the key holds.

**The list check, before any gate.** `1` first runs the list check and
writes no controller's value unless it passes: the `regedit /e` export of
`SilentClasses` to a file in `%TEMP%` (the script's directory when there is
none), read back, a value line continued with `\` joined first, and each of
`"0"` to `"5"` present as `hex:` (`REG_BINARY`) with exactly its four bytes
of the table above (hex compared without regard to case). An export that
fails or leaves no file, a file that cannot be read, a missing key, a
missing one of the six, another type (`dword:`, a string, `hex(N):`) or
other bytes - each is a failed check: it names what it found, says "the
switch was not set on any controller", and exits 4. The check reads the key
as it is after the merge, not the merge's own result, so a merge that
failed over a key already holding the six passes (the list the driver will
read is the right one) and a merge that failed over a different list does
not. **Values beyond the six are reported, not refused** (the choice
here): the key may be shared with another stack's list, extended by hand
(SweetLow's stack reads the same key), and refusing would make the switch unusable beside it, and deleting them would
break that stack; so the check passes and prints, before writing, each
other value's name, type and bytes and whether the driver's admission
(6.11.7) would keep it - "the driver reads these too; a kept one silences
its class as well" - and the count kept toward the cap of 32.

**Setting** (`1` after a passed check, or `0`) writes the DWORD to each
controller's key in turn, each write trapped on its own (`On Error Resume
Next`, cleared per key) and read back with `RegRead` (a DWORD of exactly the
value written). The script keeps two records per controller, apart: whether
the `RegWrite` returned without error, and whether the `RegRead` verified
it, so each controller ends in one of three states - **set** (written and
verified), **not set** (the write itself failed, so the value is as it
was) or **unverified** (written without error, then the read-back failed
or read another value, so the value may or may not have changed). It then
prints one line per controller (its state, and the error for the last two),
and, taking the first that applies: any unverified - "UNVERIFIED: the
switch may have changed on N of M controllers", the `show` text (which may
itself fail to read those keys), "RESTART WINDOWS" withheld and in its
place the recovery (run `silent-off.bat` to set every controller back to
0, or check `XhciSilentInstall` under each controller's key in `regedit`),
exit 5; all set - the `show` text and "RESTART WINDOWS", once, as
`portsw.vbs` does since 38.6, exit 0; none set (every write failed) - "the
switch was not changed", exit 1; some set and some not - "PARTIAL: the
switch was set on N of M controllers", the `show` text, "RESTART WINDOWS"
withheld and in its place what to do (run `silent-off.bat` to set every
controller back to 0, or correct the failure and run the script again),
exit 3. "Not changed" is said only when no write succeeded; a write that
succeeded and was not verified is never reported as not set. On an NT
system it writes nothing: it says the silent install is a Windows 98 SE
and ME feature the driver never reads there, and exits 1. No elevation
path, since it writes only on 98 SE and ME.

**`silent-on.bat` and `silent-off.bat`** are `portsw-on.bat`'s shape: one
file for COMMAND.COM and cmd.exe, ASCII and CRLF, the `if
"%OS%"=="Windows_NT" goto nt` head, every line from the `run` label on
COMMAND.COM syntax, a check that `SILENT.VBS` (and for `silent-on.bat`
`SILENT.REG`) is in the current directory with the same "run it from the
download's `TOOLS\SILENT` directory" message, a `pause` at the end, and the
restart said once, by `SILENT.VBS`. On NT both say "Windows 98 SE and ME
only; nothing was changed" and end. `test-package.ps1` gates their text as
it gates `portsw-on.bat`'s.

- **`silent-on.bat`**: `cscript //nologo silent.vbs show`, and **on any
  errorlevel from it, stop**: "nothing was changed" (no controller found,
  or NT), no merge and no gate. Then `regedit /s
  silent.reg`; then `cscript //nologo silent.vbs 1`, which checks the list
  before it writes any gate (above). The batch reads its errorlevel highest
  first, as COMMAND.COM's `if errorlevel N` is "N or more": 5 - "the
  switch may have changed on some controllers and could not be read back;
  the lines above say which; run `silent-off.bat` to set it back to 0, or
  check `XhciSilentInstall` in `regedit`; restart only once it reads
  right"; 4 - "the list did not read back as `SILENT.REG`'s six; the
  switch was not changed on any controller; the list may be merged in
  part" and what the script found, and that a controller whose switch was
  already on (the `show` lines above say so) stays on over that list - "run
  `silent-off.bat` if the silent install must stop"; 3 - "the switch was
  set on some controllers and not others; the lines above say which; run
  `silent-off.bat`, or correct it and run `silent-on.bat` again"; 1 or 2 -
  "the switch was not set"; 0 - the script's own restart line stands. The
  list goes in before the gate and the gate is written only after the list
  reads back right, so a failed check cannot newly enable a gate; existing
  gate values remain unchanged, so a gate already on from an earlier run
  stays on over whatever the failed merge left, and the 4 message says so
  and points to `silent-off.bat`. `regedit /s` reports nothing and its
  errorlevel is not relied on - the check is.
- **`silent-off.bat`**: `cscript //nologo silent.vbs 0`, with the same
  descending reading of 5 ("may have changed on some controllers and could
  not be read back; run `silent-off.bat` again, or check
  `XhciSilentInstall` in `regedit`"), of 3 ("set back to 0 on some
  controllers and not others; run `silent-off.bat` again") and of 1 or 2,
  and it says that the list was
  left in place, because another USB stack may read the same key
  (SweetLow's, whose file this list's values are), and how to delete it by
  hand. `0` runs no list check: turning the switch off needs no list.

`test-package.ps1`'s gate of the two batch files' text includes the stop
after `show` and the 5, 4, 3 and 1 branches of `silent-on.bat`, in that
order.

**Names.** `SILENT.REG` and `SILENT.VBS` are 8.3. `silent-on.bat` and
`silent-off.bat` are the owner's names, 9 and 10 characters before the dot,
not 8.3 - as `portsw-on.bat` and `portsw-off.bat` are not, which run by
double-click and at a 98 SE or ME prompt under their long names. **Decided
(O7, owner 2026-10-13):** the long names stay.

**The readme section** (the readme template's, under `tools\silent\`):

- What it does: on Windows 98 SE and ME, a device of a listed class -
  hub, boot keyboard, boot mouse, Bulk-Only stick, alone or in a composite
  device - installs with no wizard; on Windows 2000 and later neither the
  switch nor the list is read, and the scripts change nothing there.
- **Two parts, both needed**: the list (`SILENT.REG`) and the driver's
  switch (`XhciSilentInstall`, off unless it is 1). `silent-on.bat` does
  both; a list merged by hand, or by another stack, does nothing while the
  switch is off.
- **Run `silent-on.bat` only once each listed class's drivers have been
  installed at least once on this machine**, by plugging such a device with
  the switch off and finishing its wizard: with no wizard, a silent install
  on 98 SE asks for a missing driver file from the CD (38.2's legs:
  `hidusb.sys` and `kbdhid.vxd` asked for, 6.11.11), so a device whose
  driver is not yet on the machine depends on the CD being at hand; the
  case with no CD was not read. The case
  read before that: a USB keyboard
  on 98 SE needs `kbdhid.vxd` from the Windows 98 SE CD (run-36, K5), and
  storage needs NUSB's `usbstor.sys` on 98 SE (ME's own on ME).
- **Restart Windows** after either script: the driver reads the switch and
  the list only when its controller starts.
- **`silent-off.bat` turns the switch off and keeps the list**, because
  another USB stack may share the key. To delete the list too, if nothing
  else uses it: in `regedit`, under
  `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB`, delete the
  `SilentClasses` key, and restart.
- UAS is not in the list, and why.
- ME: what the legs saw on ME without the list (below), as an observation.
- The credit: SweetLow, for the information on how the silent install is
  decided and for the list.

#### 6.11.9 Windows ME, and the legs

**ME is read, not assumed.** Roadmap 37.1 installed the mouse, the hub and
the stick on a stock ME with no list and no wizard; the wizard came only for
UAS (`XHCIUAS.INF`, this package's) and composite audio (run-37, 37.1). So
ME may decide silence another way, or ME's `usbhub.sys` (4.90.3002.1, not
read here for this) may not be what decides it. What ME does without the
list is therefore recorded as an observation, not asserted: on ME the legs
assert the bus's answer (the `silent.pdo` notes) and that configuration
(c) below asks no more than (a); the readme then says what was seen.

**Fresh devnodes.** Windows decides a silent install only when it makes a
devnode; a device whose devnode already exists - same parent, same
instance id, which is the serial where the device has a usable one and
then follows it across ports (section 10.7) - binds again with no wizard
and says nothing about the answer. So every pass that reads silence plugs
each device where it has no devnode: from a clean guest snapshot taken
after each class's drivers were installed once (each class's first install
through its wizard, the device's devnodes then removed in Device Manager,
which leaves the class driver's files), or, for a device with no usable
serial, at a port it has never been on. The first configuration's wizard
is the proof that the devnode was new.

**The channel on every leg.** The `silent.*` notes reach the ring only at
`XhciLogVerbosity` 2 or more (6.11.3), and the default is 0, so every
observation leg below runs 38.1's `SNAPON.BAT` (`XHCISNAP -verbosity 2`,
in `tools\xhcisnap\`) before its restart, and reads the notes after it with
`SNAPGET.BAT` (or `XHCISNAP -report`). A leg whose ring holds none of the
start's other notes either did not turn the channel on, and is run again;
it is never read as "no `silent.*` note".

**Four configurations**, on 98 SE and on ME, each after `SNAPON.BAT` and a
restart:

- **(0) the default install** (a clean install of the package: no
  `XhciSilentInstall` under any controller's key, which the INFs never
  write, and no `SilentClasses` key): `silent.gate.value` `0xFFFFFFFF`,
  `silent.gate` 0, and no `silent.status`, `silent.entries`,
  `silent.ignored`, `silent.dropped` or `silent.pdo` note - the gate is
  read as off and the list is never read, and no PDO's decision is noted
  because none is made; every PDO's `IRP_MN_QUERY_CAPABILITIES` answers
  `SilentInstall` FALSE, as before 6.11; the wizard as today. This is the
  configuration every user is in who never runs `silent-on.bat`.
- **(a) the list merged, the gate off** (`regedit /s SILENT.REG`, read
  once with no `XhciSilentInstall` and once with `silent-off.bat`'s 0):
  `silent.gate` 0, no
  `silent.entries` and no `silent.pdo` note; the wizard as today.
- **(b) the gate on, no list** (the DWORD 1 set by hand in `regedit` under
  the controller's key, on a guest with no `SilentClasses` key -
  `silent.vbs 1` refuses here, exit 4, which the leg also reads, with no
  gate written): `silent.gate` 1, `silent.status` the query's failure,
  `silent.entries` 0, every `silent.pdo` reason `XHCI_SILENT_EMPTY`,
  answer 0; the wizard as today.
- **(c) both** (`silent-on.bat`): `silent.gate` 1, `silent.entries` 6,
  `silent.ignored` 0, `silent.dropped` 0 (O1's reading); silence as below.
  Beside it, the script's refusals read once each on 98 SE: a guest whose
  `SilentClasses` holds `"0"` with other bytes and a `SILENT.REG` made
  unreadable (exit 4, every controller's value unchanged), and a guest with
  one extra well-formed value (passes, the extra named and counted).

**The devices**, each on a fresh devnode in each configuration, reading
both the wizard and the device's `silent.pdo` notes:

1. A device installed once and then on a fresh devnode (a known class on a
   new devnode): the wizard in (0), (a) and (b), none in (c), answer 1.
2. A hub: answer 1 in (c); whether it changes anything for this package's
   own `XHCI98\HUB` devnode is read.
3. A boot keyboard and a boot mouse, each alone (on 98 SE the keyboard's
   first install takes the CD's `kbdhid.vxd`, before the snapshot).
4. A composite device with a matching function and one that matches
   nothing (a boot keyboard or mouse function beside a non-boot HID
   `03/00/xx` or an audio function): in (c) the parent answers 1 (O3), the
   matching function 1 and installs silently, the other function 0 and its
   wizard comes.
5. A Bulk-Only stick: answer 1 in (c).
6. **UAS and refused, FALSE explicitly** (O2): in (c) a dual-mode drive
   presented UAS answers 0 with reason `XHCI_SILENT_BARRED` and its
   `XHCIUAS.INF` wizard comes; a device refused in place (UAS-only at
   SuperSpeed on a controller that does not stream, on a root port with no
   companion or behind a SuperSpeed hub; 31-A.3) answers 0 with the same
   reason, read where such a vehicle is at hand, and otherwise recorded as
   host-tested only.
7. **The composite parent disabled and enabled** in Device Manager in (c)
   (the path of `HcdDevicePdoRepresentFunctions`, task 36.43): the
   re-presented function PDOs' `silent.pdo` notes give the same answers as
   at their first creation, and nothing asks. (Read on 98 SE: the path was
   not taken; 6.11.11.)

Then the readme says what ME did. Proposed beside them (not the owner's
list): one Windows 2000 reading with the list merged and `XhciSilentInstall`
1 set by hand in `regedit` (`SILENT.VBS` writes nothing on NT), after
`SNAPON.BAT`, which should show the start's other notes, no `silent.*`
note, and every install as before.

#### 6.11.10 Questions and decisions

O2 to O5 were decided by the owner on 2026-10-13, each as proposed; O1 is
a fact to be read, not a choice; O6 and O7, from the gate's design,
were decided by the owner the same day, each as proposed.

| | Question | Decided / to read |
|---|---|---|
| O1 | Does NTKERN's `RtlQueryRegistryValues` (98 SE, ME) enumerate every value when `Name` is NULL? If not: `ZwOpenKey` (98 SE evidence, not allowlisted) with `ZwEnumerateValueKey` (no Windows 98 evidence recorded), or a by-name query of `"0"` to `"31"` | **Read 2026-10-13 on a 98 SE guest (runtime):** it does - with the gate on and the six-entry list merged, the notes read `silent.entries=00000006`, `silent.ignored=00000000`, `silent.dropped=00000000`, `silent.status=00000000`; no fallback is needed (`out\p38-2\legs\`) |
| O2 | A PDO presented as UAS, or refused, whose Bulk-Only alternate matches the storage entry | FALSE for `XHCI_XPORT_UAS` and `XHCI_XPORT_REFUSED` (owner) |
| O3 | The composite parent: TRUE when any function's interfaces match (the rule read literally), when all do, or never | any (literal) (owner) |
| O4 | A malformed configuration: 0 whatever matched, or keep a match found before the fault | 0 (owner) |
| O5 | The table's cap | 32, the excess dropped and counted (owner) |
| O6 | `XhciSilentInstall` other than absent, 0 and 1 | off - only a DWORD of exactly 1 is on, `XhciSelectiveSuspend`'s rule (6.11.3) (owner, 2026-10-13) |
| O7 | The batch files' names | `silent-on.bat` and `silent-off.bat`, long names as `portsw-on.bat`'s (6.11.8) (owner, 2026-10-13) |

#### 6.11.11 What the legs read (2026-10-13)

Runtime: this driver's `silent.*` notes in `XHCISNAP` dumps at verbosity 2,
the New Hardware wizard seen or not, Device Manager, and `regedit /e` of
`Enum`; the `release` package of `d8c8fa63`, QEMU's `qemu-xhci`, development
host A; evidence `out\p38-2\legs\notes.txt`, dumps and shots beside it
(git-ignored). Every device on a devnode it had never had (a fresh serial).

**Windows 98 SE** (NUSB 3.3; the storage class driver installed before (c),
the HID drivers not, see (c)):

- (0): `silent.gate.value` `0xFFFFFFFF`, `silent.gate` 0, no other note.
- (c): `silent.entries` 6, `silent.ignored` 0, `silent.dropped` 0 (O1);
  a mouse, a Bulk-Only stick, a hub and a keyboard each `silent.pdo` MATCH,
  answer 1, no wizard. On the base used, the HID drivers had not been
  installed before, and the mouse and the keyboard asked for the CD
  (`hidusb.sys`, `kbdhid.vxd`) with no wizard; the mouse then installed
  from it, and the keyboard was removed before its install was followed to
  the end. The readme's "drivers installed once first" is that. The stick
  installed (`Driver` set); the hub's devnode was left a failed install
  (no `Driver`, `ConfigFlags` 0x40), because that base's installed
  `xhci98` INF was `1.99.0.0`, older than the `XHCI98\HUB` model (added in
  `2.1.0.0`). Read again on the same base with the current INF
  (`2.3.1.0`) in `INF\OTHER`: a new hub, no wizard, MATCH, `Driver` set,
  `ConfigFlags` 0, "xHCI98 USB Hub" working, a mouse behind it silent (one
  CD prompt, `hidusb.sys`) and working; a new keyboard, no wizard, MATCH, one CD prompt (`hidusb.sys`;
  `kbdhid.vxd` was on the disk by then), "HID-compliant keyboard" working
  and typing.
- (b), the gate left at 1 from (c) and the list deleted: `silent.status`
  `0xC0000034`,
  `silent.entries` 0, a new mouse `silent.pdo` reason EMPTY, answer 0, the
  wizard came.
- (a), the list merged and the gate 0: only the two gate notes, no
  `silent.pdo` note, the wizard came for a new mouse.
- `silent-on.bat` over a list whose `"3"` read `c3,08,06,51`: refused, "The
  switch was not set on any controller", exit 4, a gate already 1 left at
  1. Over the six and three extra values (`40,01,00,00`, a DWORD,
  `01,03,00,00`): passes, names each extra and says whether the driver
  keeps it, "would keep 7"; after the restart `silent.entries` 7,
  `silent.ignored` 2. `silent-off.bat`: 0 on every controller, the list
  (extras included) left in place.
- A composite with a matching function: QEMU 11.1 has none with a boot HID
  or Bulk-Only function (each of its HID and storage devices has one
  interface; `usb-net` has two configurations and is left whole), so
  `usb-audio`, the one device the bus splits, was read with a hand-added
  `0x40` entry for class 01: parent and function each MATCH, no wizard, no
  CD; "xHCI98 USB Composite Device" and "USB Audio Device" working. A first
  plug on a base whose installed `xhci98` INF predated the
  `XHCI98\COMPOSITE` model left the parent devnode at Code 28 with nothing
  said - the readme's hazard, through a stale INF.
- The composite parent disabled and enabled: Code 22, then working; no new
  `silent.pdo` note, so `HcdDevicePdoRepresentFunctions` did not run (6.11.4).
- UAS barred: in (c) a `usb-uas` device on a SuperSpeed port `silent.pdo`
  reason BARRED, answer 0, its `XHCIUAS.INF` wizard came. Refused in place
  (`qemu-xhci p2=1,p3=4,streams=off`, every SuperSpeed port without a
  companion): BARRED, answer 0, the wizard came and found no driver. Its
  devnode's `HardwareID` value read
  `USB\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000,USB\VID_46F4&PID_0003`:
  the second entry is the device id, which this section did not set out to
  read; roadmap task NOX.1 reads whether it can bind.
- `Capabilities` in `Enum`: bit `0x20` set on the present devnodes the bus
  answered 1, clear on those it answered 0; a hot-removed devnode's value
  reads the same either way.

**Windows ME** (the package installed by the New Hardware wizard on a stock
ME):

- (0): only the two gate notes; a new mouse, stick, hub and keyboard **each
  installed with no wizard and no disk** with every PDO answering FALSE, as
  37.1 saw; a `usb-audio` device behind the hub brought the wizard.
- (c): `silent.entries` 6, `silent.ignored` 0, `silent.dropped` 0 (O1 holds
  on ME); MATCH for the stick, mouse and hub; a new mouse, no wizard. With
  the `0x40` class 01 entry added (`silent.entries` 7, `silent.ignored` 2),
  the `usb-audio` device that brought the wizard in (0) installed with no
  wizard and no disk, parent and function MATCH, both working.

So **ME does not need the list** for a keyboard, mouse, stick or hub that
is a device of its own: it installs them silently without it. Composite
HID and storage (the entries `0x43` and `0xC3`) were read on neither system:
no QEMU device offers one. The switch changes what ME does only for a class
ME would ask about, as audio showed. Not read on ME: (a), (b),
`silent-off.bat`, the script's refusals and the composite parent's disable
and enable, each read on 98 SE (of the refusals, the wrong-value one; the
unreadable `SILENT.REG` was not read). Not read on Windows 2000: the base at
hand had no `xhci98` controller.

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
  the configuration descriptor fetched with `0x220410`. At SuperSpeed
  bMaxPower is in 8 mA units (USB 3.2 Table 9-23; `xhci-data-structures.md`
  section 10.7), so the page showed a quarter of the current: 224 mA for a
  device UsbTreeView reads at 896 mA under Windows 11 (owner, P14s Gen 1,
  2026-10-05; roadmap-hcd task 34.2). Since then the door rewrites the byte
  in usbui's copy alone - request code 0, configuration 0, a SuperSpeed
  connection - to the current in 2 mA units held to 0FFh
  (`XhciDescPowerTabByte`), so the page shows the true current up to 510 mA
  and 510 mA above it. GET_DESCRIPTOR (code 6), the cached descriptor and
  the length returned are the device's own, and the bus logs each
  SuperSpeed device's exact current at enumeration (`dev.ss.vidpid`,
  `dev.ss.maxpower.ma`), which `XHCISNAP` prints.
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

**Amended by task 36.18 (2026-10-09; GitHub issue 17 item 6).** The High
Speed ceiling above is replaced, in two halves:

- **The `_EX` form's `Speed` byte** reads 3 for a SuperSpeed connection and
  4 for a SuperSpeedPlus one (the trained rate, `dev->Plus`), as Intel's USB
  3 drivers for Windows 9x report it, **on Windows 98 and ME only** - where
  `IoIsWdmVersionAvailable(1, 0x10)` is FALSE, the test `hcd_pdo.c` uses
  for them (owner, 2026-10-09). No Microsoft header defines 4, and the WDK
  7.1 headers stop at 2. Every NT target keeps 2 for both: Vista's and 7's
  `usbui!UsbItem::IsHighSpeed` tests the byte for exactly 2, and their hub
  page would list a 3 among the Full Speed devices (static,
  `legal-provenance.md` section 4, "Who reads the `_EX` `Speed` byte"); XP's
  and XP x64's pages never read it. 0 to 2 are unchanged
  (`XhciHubExSpeed`, `xhci_hub.c`).
- **`IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2`** (`0x0022045C`),
  new on every target, for NT tools: `ConnectionIndex`, `Length`,
  `SupportedUsbProtocols` (`Usb110` bit 0, `Usb200` bit 1, `Usb300` bit 2)
  and the flags (`DeviceIsOperatingAtSuperSpeedOrHigher` bit 0,
  `...SuperSpeedCapableOrHigher` bit 1, `...OperatingAtSuperSpeedPlusOrHigher`
  bit 2, `...SuperSpeedPlusCapableOrHigher` bit 3), 16 bytes, from
  Microsoft's `usbioctl.h` reference pages (documentation, read
  2026-10-09), written byte by byte like the rest of the door.
  - **The request**: an input or output buffer shorter than 16 bytes is
    `STATUS_BUFFER_TOO_SMALL`, each checked apart; an input longer than 16
    bytes, a `Length` other than 16,
    or an input `SupportedUsbProtocols` without `Usb300` (Windows 8 refuses
    that, by Microsoft's IOCTL page), is `STATUS_INVALID_PARAMETER`; a port
    out of range is refused as the `_EX` form refuses it. The answer is 16
    bytes, `ConnectionIndex` kept (`XhciHubExV2Check`, `XhciHubExV2Fill`).
  - **`SupportedUsbProtocols`, out**: the **port's** protocols, not the
    device's and not the request's: a root port by its protocol group
    (`Usb110` and `Usb200` on a USB 2.0 port, `Usb300` alone on a USB 3
    logical port, which gains nothing from its USB 2.0 companion), an
    external hub's port by the hub's speed (a SuperSpeed half's `Usb300`, a
    High-Speed hub's `Usb110` and `Usb200`, a Full-Speed hub's `Usb110`). An
    empty port has them too.
  - **The operating flags**: from the connection's negotiated speed class
    and rate.
  - **The capable flags**: from the device's BOS (its SuperSpeed and
    SuperSpeedPlus capability descriptors, `dev->BosInfo`), not from the
    speed, since a SuperSpeedPlus device may be running at 5 Gbit/s; and
    also set when the device operates at that speed, which proves it. The BOS
    is read only on the SuperSpeed enumeration path (`xhci_enum.c`), and
    **this task does not add a BOS read at High Speed**: a USB 3 device
    running at High Speed (on a USB 2.0 port, or sent back there) reports
    neither capable flag. Reading the BOS of every High-Speed device with a
    `bcdUSB` of 0x0210 or more would change every such device's enumeration
    for one flag, which is the larger risk (`XhciHubExV2Flags`).

### 8.5 The OS INFs' registrations, quoted

Root hub, NT paths - every NT target registers the same provider:

| File | Section | Line |
|---|---|---|
| 2000 SP4 `usb.inf` (5.00.2195.6717) | `[ROOTHUB2.AddReg.NT]` (for `USB\ROOT_HUB20`), `[StandardHub.AddReg.NT]` (`USB\ROOT_HUB`, `USB\CLASS_09`), `[Usb2Hub.AddReg.NT]` (`USB\HubClass`) | `HKR,,EnumPropPages32,,"usbui.dll,USBHubPropPageProvider"` |
| XP SP3 `usbport.inf` (5.1.2600.5512) | `[ROOTHUB.AddReg.NT]` (`USB\ROOT_HUB` and `USB\ROOT_HUB20` both `=ROOTHUB.Dev`) | same |
| XP SP3 `usb.inf` | `[StandardHub.AddReg.NT]` | same; `[Comp.DelReg.NT]` deletes `HKR,,EnumPropPages32` for composites |
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
| SweetLow `USB2.INF` | `[ROOTHUB2]` / `[Usb2Hub.Dev]` | the same three lines (76-79); `[Comp.DelReg]` deletes `HKR,,EnumPropPages` |

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
| **Split composite devices**: `usbui.dll` maps a connection to one devnode through `GET_NODE_CONNECTION_DRIVERKEY_NAME`. With the bus splitting a composite into per-function PDOs there is no single connection devnode; which key to return (the first function's, or a failure) and what `usbui` then shows is unread. **Closed 2026-10-10 by task 36.43 (10.12)**: every split device has a composite-parent devnode, and the connection's driver key is the parent's | 26-A.8, with 26-A.7; 36.43 |
| How Windows 98's engine treats a root-hub section whose `NTMPDriver` is the already-loaded controller driver, and NTKERN calling `AddDevice` a second time on that driver object (98's own `usbhub.sys` serves several devnodes, so it is expected to work). | 26-V.0 / 26-V.1 (a guest reading) |
| The `ControllerFlavor` to report on Vista and 7 (1000 or above for USB 2.0 arithmetic; nothing in WDK 7.1 names xHCI). What usbport reported for the miniport was not read. | 26-A.8 |
| Hub WMI on Vista and 7 (`GUID_USB_WMI_DEVICE_PERF_INFO`, `_HUB_DIAGNOSTICS`, the `_STD_DATA` notifications), and what `EnumerateHubPorts` does when the performance query fails. The popups (over-current, not enough power) are WMI-driven and belong to neither tab. | 26-A.8 (and 28-A.1 for Vista and 7) |
| The exact `SymbolicName` string each stack writes (interface link or `\DosDevices\` name) on 98, 2000 and XP: only Windows 7's write site was read. On 98, whether `CreateFileA("\\.\<interface link>")` resolves through NTKERN (NUSB's hub page working suggests it does) or whether the HCD should write a `\DosDevices\` name there. | 26-A.8 |
| `sysclass.dll`'s `DiagnosticMode` and `SupportNonComp` value names: purpose unread. | 26-A.8 |
| A `bcdUSB` of 0x0300 in `DeviceDescriptor`, and a SuperSpeed device's `Speed` on Vista and 7 (`UsbHighSpeed` is the ceiling): whether `UsbItem::UsbVersion` or the bandwidth arithmetic misreads a 3.x descriptor. | Phase 29 |
| Device-object security for a non-elevated Device Manager on Vista and 7 (`GENERIC_WRITE` open): a runtime question. | 28-A.1 |

**The USB device interface (task 36.17, 2026-10-09; GitHub issue 17 item
3).** `GUID_DEVINTERFACE_USB_DEVICE` (`{A5DCBF10-6530-11D2-901F-00C04FB951ED}`,
WDK 7.1 `usbiodef.h`) is registered on every device PDO that is not a hub,
as usbhub registers it, and not on the root hub, on which usbport
registers none (`hcd_door.c`, `HcdDoorDeviceStart` and `HcdDoorDeviceStop`).
**Corrected 2026-10-13 by task 38.7 (GitHub issue 17 item 3, reopened by
SweetLow): until then an external hub's PDO carried it too, said to be "as
usbhub registers it" with no reading behind it.** The reading
(`runs/run-38.md`, "38.7"), on Windows 2000 SP4, XP and 7 x86 with the
stock stack (UHCI, QEMU's hub on a root port with a keyboard behind it, a
mouse on another root port): `GUID_DEVINTERFACE_USB_DEVICE` on the
keyboard and the mouse only; `GUID_DEVINTERFACE_USB_HUB` on the root hub
and the external hub only; no devnode with both. The third interface of
the same list, `GUID_DEVINTERFACE_USB_HOST_CONTROLLER`, read with it: on XP
and 7 on the controller's devnode alone, as the bus registers it
(`HcdDoorControllerStart`); on 2000 on none, the stock stack registering
no such interface there. The bus still registers it on 2000 (**decided,
owner, 2026-10-13**): nothing on 2000 looks for it, and the `HCD<n>` name
stays the route there. Windows 11's stack,
read on development host A, agrees. So an external hub's PDO carries
the hub interface alone, which its FDO registers (`HcdDoorHubStart`), a
hub refused as too deep included (`HcdDoorDeviceStart` refuses every PDO
with `Hub` set). A hub devnode a release before `2.4.0.0` registered keeps
its `DeviceClasses` entry, never enabled again: an interface not linked,
as Windows leaves for any device gone.
**Superseded 2026-10-10 by task 36.43 (10.12): the interface is on the
composite parent's PDO, the device's own devnode, and on no function; the
rest of this paragraph is the rule it replaced.** A composite the bus
splits has no parent devnode, so the interface goes on
one function's PDO: the function that owns the device's first interface,
the group's first PDO, the one the door already answers the connection's
driver key from (the split-composite row above), never a hard-coded
`MI_00`. **Decided (owner, 2026-10-09):** the interface stays on that
function and follows its state - when it has no driver, fails its start or
is disabled, the device has no interface while its siblings run. Its
lifecycle follows `IoSetDeviceInterfaceState`'s contract: registered once,
at the first successful start, and enabled at every successful start (a
retained PDO started again included); disabled at a stop, a surprise
removal and a removal, never twice; the link string freed, idempotently,
in `hcdDeletePdo`, the one helper every path that destroys a device PDO
goes through (the relations answer after a removal, the retained PDO's
deletion after a disable, an orphan's deletion and Windows 98's deferred
one, the parent's release), not left to `IoDeleteDevice`. Best effort, as
the hub interface is: a failed registration is a missing interface, not a
failed device.

### 8.11 Descriptors the cache cannot answer (task 36.45)

**What is asked (owner, 2026-10-10; GitHub issue 17 item 8).** Until this
task `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION` (`0x220410`) was answered
from the descriptors the bus read at enumeration - the device descriptor and
configuration 0 - and refused everything else `STATUS_NOT_SUPPORTED`, so no
tool saw a string: usbview's Manufacturer, Product, Serial and interface
strings stayed empty where Microsoft's hubs fill them. Now a request the cache
cannot answer goes to the device as a real `GET_DESCRIPTOR` on its default
endpoint, as usbhub sends it, and the bytes come back in the request's `Data`.

**What usbhub does with it** (static, `legal-provenance.md` section 4, "How
usbhub answers `0x220410`"; XP SP3 `usbhub.sys` with its PDB, 2000 SP4
`USBHUB20.SYS` instruction for instruction the same, 98 SE `usbhub.sys`, 7
SP1 x86 `usbhub.sys` with its PDB):

| | XP SP3, 2000 SP4, 98 SE | 7 SP1 |
|---|---|---|
| Output shorter than the 12-byte request | `STATUS_BUFFER_TOO_SMALL` (input length never checked) | `STATUS_NO_SUCH_DEVICE`: its parameter check (input below 4, output below 13, the port) fails into that one status |
| `wLength` above output - 12 | `STATUS_BUFFER_TOO_SMALL` | the same |
| Port 0 or past the hub's ports, or no device on it | `STATUS_INVALID_PARAMETER` (98 SE: success, nothing returned) | `STATUS_NO_SUCH_DEVICE`; no device: success, nothing returned |
| `bmRequest`, `bRequest` | carried in a reserved URB field and ignored: URB function `GET_DESCRIPTOR_FROM_DEVICE` (0x0B), so the bus sends 80h, 06h | forced to 80h and 06h, written back into the caller's buffer |
| `wValue`, `wIndex` | passed through (`wIndex` as the language id) | passed through |
| Cache | only `wValue` 0200h with `wLength` 9 (the Power tab's request) | none while the device is addressed; device and configuration descriptor only while it is not, anything else then `STATUS_UNSUCCESSFUL` |
| Transfer | `wLength` bytes, to the child PDO, synchronous, cancelled after 5 s | the setup's own `wLength`, buffer room `OutLen - 12`, a 2 s timeout, to the bus with the device handle |
| Device state | nothing checked: an unenumerated or suspended device is sent the request | a handle only in the addressed state; a suspended child is neither checked nor woken |
| Status | the bus's, unmapped | the bus's, unmapped |
| `Information` | 12 + bytes received, even on failure | 12 + bytes received on success only |

**The HCD's answer.**

1. **Checks** (`XhciHubDescPlan`, `xhci_hub.c`, host vectors in `test_hub`):
   an input or output shorter than 12 bytes, or a `wLength` above
   `OutputBufferLength - 12`, is `STATUS_BUFFER_TOO_SMALL`; a port outside
   the node, or no device on it, `STATUS_INVALID_PARAMETER`, as XP's and
   2000's hubs and as this door already answered. The input check is kept
   where usbhub has none: `METHOD_BUFFERED` leaves the system buffer past
   the input length unwritten. `bmRequest` and `bRequest` are ignored, as
   both hub generations ignore them: the request is always `GET_DESCRIPTOR`
   (80h, 06h). The value 0 every `usbui.dll` sends (8.3) therefore needs no
   special case, and nothing the caller sent is rewritten.
2. **The cache stays first, unchanged**: `wValue` 0100h (the device
   descriptor) and 0200h (configuration 0) are answered from the bus's copy,
   with task 34.2's Power-tab rewrite for request code 0. That is a superset
   of XP's one cached request and what Windows 7 answers for a device it
   cannot address; the bytes are the device's own either way, and the Power
   tab and usbview's tree refresh send no EP0 traffic.
3. **Everything else is forwarded**: any string (index 0, the language list,
   included, with `wIndex` as the language id), another configuration
   index, the BOS, and any other type. The setup is 80h, 06h, `wValue`,
   `wIndex`, and `min(wLength, 4096)`: 4096 is the bus's EP0 scratch
   (`HCD_SCRATCH_CONTROL_BYTES`). A request above it asks the device for
   4096 bytes, so only a descriptor longer than 4096 bytes - none of the
   kinds usbview reads in practice; configuration 0, the one that grows, is
   cached whole - reads short. `wLength` 0 is sent as a transfer with no
   data stage. `Information` is 12 plus the bytes received, on success
   only.

**IRQL and the path.** The IOCTL arrives at PASSIVE_LEVEL from user mode,
inside the root-hub or hub FDO's I/O count. The transfer is not issued there:
EP0 transfers the bus makes for itself (enumeration, the serial read,
SET_CONFIGURATION, SET_INTERFACE, CLEAR_FEATURE for a client) are the
controller thread's alone (`HcdThreadControlOutcome`), because the thread owns
the scratch buffer and the device's EP0 pause. So the door **pends the IRP**,
in section 5.4's lock order:

1. Under `PdoListLock`, the PDO standing for the connection is found (the
   match `hcdDoorConnection` makes) and, while that listed PDO still names
   it, its device record is referenced - as a URB's dispatch references it
   (`hcdDeviceRef`), not through a referenced PDO, whose `Device` could
   change once the lock is dropped. A root port whose hub has no PDO
   references the port's record under the controller lock instead, unless
   it is `Gone`.
2. Under the controller lock: the queue must be open and the record not
   `Gone`, else the reference is given back and the request refused. In the
   same hold the record goes in `DriverContext[0]` and the door FDO's I/O
   count and its event in `[1]` and `[2]` (the dispatch's own node
   description is not kept), the FDO's count is raised once more, the IRP is
   marked pending, its cancel routine armed, and it is listed on the
   controller's `DoorIrps`.
3. The thread is woken. The dispatch returns `STATUS_PENDING` without
   completing the IRP.

The thread serves the list in its powered pass after the slow IRPs
(`HcdCfgService`), in order, at most four a pass and none begun once 250 ms
of the pass have gone, the controller's state looked at again before each
(a single request is bounded by its own wait, below); a pass that leaves any
wakes the thread again. Each IRP is completed from the thread when its
transfer completes, then the device reference and last the FDO's count are
given back. Completion and cancellation run at any IRQL up to
DISPATCH_LEVEL.

**Serialization with the function driver.** The forwarded request goes
through the thread's own EP0 path, not a client's queue:
`HcdThreadControlOutcome` pauses the EP0 pipe (client URBs wait at its gate),
lets the client transfers already on the ring finish and recovers a STALL
among them, then sends its own transfer and resumes the pipe. It never shares
the ring with a client transfer, and it runs in sequence with everything else
the thread does to the device - RESET_PORT, a select, a cycle - so a request
can neither race nor land inside one. The function driver's bulk, interrupt
and isochronous pipes are not touched; its own EP0 requests wait at the gate
for the length of one transfer.

**Timeout, cancellation and surprise removal.**
- *Timeout:* the thread path's own bound, 5 s to quiet EP0 and 5 s for the
  transfer (`HCD_TRANSFER_WAIT_MS`), usbhub's 5 s on XP. A client transfer
  that keeps EP0 busy past its bound fails the request (`NOT_SENT`, nothing
  sent, no recovery). A transfer the device never ends is **withdrawn**,
  as usbhub's cancel leaves the device alone, rather than meeting the rule
  of the thread's other transfers, which recover the controller because the
  scratch may still be written (design record 07): an arbitrary descriptor
  reaches firmware paths enumeration never does, and a diagnostic tool
  should not drop every device on the controller. The withdrawal spares the
  other devices when the device has no TT and every step succeeds; a device
  behind a TT, and a withdrawal that fails, still cost the controller
  recovery (below). The door's transfer
  (`HcdThreadControlDoor`) differs from the others only at its deadline,
  where `hcdEp0Withdraw` - EP0 still paused, so no client transfer is
  behind it - stops EP0 (Stop Endpoint); if EP0 then reads Stopped and the
  thread's record is the only one queued, the record leaves the engine's
  queue and the dequeue moves to the enqueue position (Set TR Dequeue). A
  stopped TD is not processed again (xHCI 4.6.9), so the scratch is free,
  and the device drops the unfinished control transfer at the next SETUP.
  A Stop Endpoint's own Transfer Event completes nothing (`hcd_dev.c`), so
  a completion seen after the stop is the device's and is answered as one.
  **A device behind a TT (a Full- or Low-Speed device behind a High-Speed
  hub, `TtSlot` set) is not withdrawn**: its stopped split transaction may
  hold the hub's TT buffer, and clearing it (`HcdHubClearTt`) is a second
  device's EP0 transfer whose failure the helper only counts, so there is
  no checked way to finish the withdrawal - such a device keeps the
  controller recovery. Any step that fails - a command, EP0 Halted or in
  Error, another record queued - falls back to the controller recovery too,
  EP0 still paused and the scratch not reused, the rule as before. Under
  XhciTolerance a cycle mark ends the wait as for any thread transfer
  (35-T.3/4).
- *How long a taken request is not cancellable:* no single deadline covers
  it, and none is claimed. It is the sum of the thread path's separate
  waits, each bounded on its own and each ending in the bus's existing
  handling when it runs out: quieting EP0 (5 s for client transfers to
  finish, plus whatever an EP0 halt found there costs to recover - its
  commands and, behind a TT, the TT clear - which runs before that
  deadline is next looked at), the transfer (5 s), and at the deadline the
  withdrawal's two commands. Every command waits up to
  `HCD_COMMAND_WAIT_MS` (5 s) for admission and again for its completion,
  and a command that does not complete is the command engine's to recover
  (the controller recovery). So a healthy device answers in milliseconds,
  a hung one costs tens of seconds of the thread under the power gate, and
  nothing is unbounded. Time spent queued is not counted; a queued request
  stays cancellable.
- *Cancellation:* while queued the IRP is cancellable (the cancel-safe queue
  pattern of `hcd_io.c`'s waiting list: the thread takes an IRP only once it
  has cleared its cancel routine, a routine already running takes the IRP
  itself, counted in `CancelsRunning`), and completes `STATUS_CANCELLED`.
  Once the thread has taken it, it is not cancellable: the transfer is
  bounded as above. A handle's `IRP_MJ_CLEANUP` on the root-hub or a hub
  FDO cancels the requests still queued through that handle (matched by
  file object, claimed by the same handshake), leaving other handles'
  alone, as the cleanup contract asks. It first marks the file object
  (`FsContext2`, the target driver's field) under the controller lock, the
  lock the pend's admission holds, so a request of that handle still in
  dispatch on another processor is refused `STATUS_CANCELLED` instead of
  being queued after the cleanup.
- *Surprise removal and unplug:* a device leaving marks its record `Gone`
  and drains it (`HcdIoDeviceDrain`), which now also completes its queued
  door IRPs `STATUS_DEVICE_NOT_CONNECTED` (`HcdDoorFlushDevice`) - before
  the pinned-buffer check, since a queued door IRP holds no DMA - so the
  thread's wait for the record's references never waits on an IRP only the
  thread completes. A transfer in flight ends with the device's
  disconnection (a transaction error) and is answered with that status. An
  FDO's REMOVE waits for its pended requests through its I/O count; a hub
  FDO's STOP or surprise removal closes its door (`DoorOpen`) to new ones.
- *The controller:* a pass that cannot serve - the controller not powered
  (D3, or suspended in D0), halted, failed or with the scratch tainted -
  completes every queued door IRP at once (`STATUS_DEVICE_NOT_READY`) rather
  than holding a diagnostic request across a power transition or a
  recovery; a pass with the scratch held for a cycle leaves them for the
  next. **The queue's rundown** is the thread's last act before it signals
  its exit and before any controller resource goes: under the controller
  lock the queue is closed (the door queues nothing on a closed queue) and
  the IRPs whose cancel routines it can clear are unlinked; the lock is
  released, those IRPs are completed, and it then waits, outside every
  lock, until the list is empty and `CancelsRunning` is 0 -
  a cancel routine the close could not pre-empt unlinks its IRP itself and
  may still be completing it, and it raises that count before it releases
  the cancel lock and drops it as its very last access to the controller.
  This holds whether or not the device records could be freed (a pinned
  common buffer keeps them, `HcdIoDeviceDrain`), so it does not rest on the
  device's free waiting the count out.

**Statuses.** A transfer that completes: `STATUS_SUCCESS`. A STALL or
another device error: `STATUS_UNSUCCESSFUL` (the URB status's NTSTATUS
through `XhciPipeNtStatus`, as a client URB reads). Not sent within the
bound, withdrawn at its deadline, or abandoned for a cycle:
`STATUS_CANCELLED`, what usbhub's timeout cancel returns. Device gone:
`STATUS_DEVICE_NOT_CONNECTED`. A queue that is closed, or a pass that
cannot serve: `STATUS_DEVICE_NOT_READY`. A forwarded request that fails
leaves the log note `door.desc.fail` (the thread outcome, `HCD_CTL_*`, in
the top byte, `wValue` in the low word), and a withdrawal
`door.ep0.withdrawn` (the slot); the counts `DoorForwarded`,
`DoorForwardFailed` and `DoorWithdrawn` are kept in the controller
extension, outside the counter block.

**Devices not addressed, being reset, suspended.** The door forwards only
for a device with a PDO (enumerated, addressed and published), or a hub the
bus serves with none (the device record, 8.3's fallback); an empty port, a
device still enumerating or one whose enumeration failed is refused as
before, `STATUS_INVALID_PARAMETER`. A device being reset (RESET_PORT) or
cycled is served after the reset, in the thread's order, or finds its
record `Gone`. **A device the bus has suspended is refused, not woken**
(`STATUS_UNSUCCESSFUL`, Windows 7's answer for a device it holds no handle
for; neither hub generation wakes a child for this IOCTL): waking would
spend the device's idle for a diagnostic read, and would have to drive the
link machine's resume (record 14 section 3.6) from outside the function's
power policy. With `XhciSelectiveSuspend` 0, the default, no device is ever
suspended. Since the merge with stage A (integration branch `p36-int3`)
the door's service tests the device's link state before the transfer
(`hcdDoorServeOne`, `HcdSuspDoorServes`, the pure `XhciSuspDoorServes`
with its vector in `test_susp`) and refuses anything but ACTIVE - a
suspending, resuming or recovering link as well as a suspended one - with
the log note `door.desc.suspended` (the link state); record 14 section 3.3 defers the
thread's other device-side EP0 requests, and a diagnostic read is refused
instead of deferred so a tool is not left waiting for a wake.

**Windows 98 and ME.** The same code serves both: `usbui.dll` and
`sysclass.dll` send only the cached configuration request (8.3), and a tool
on 9x - SweetLow's usbview opens `\\.\HCD<n>` and the hub names - waits for
a pended IRP as NT does (`DeviceIoControl` without overlap blocks until it
completes). Nothing here is version-gated.

**The composite parent (task 36.43, branch `p36-comp`).** The door resolves
a connection to the PDO standing for it (`hcdDoorConnection`) and forwards
to that PDO's device record, the USB device and not a function. Whichever
PDO 36.43 makes the connection's - the composite parent's in place of the
group's first function - names the same record, so the forwarding does not
change, and the cache's configuration copy stays the device's whole one.

**No strings cached at enumeration.** The bus reads only the serial string
(33.2), for instance ids, and keeps the characters, not the descriptor. Reading
iManufacturer, iProduct and the interface strings at every enumeration would
add EP0 requests to every device's enumeration - the step where a device is
least tolerant - for a diagnostic view, and XP's and 2000's hubs read none
there either. Every string goes to the device when it is asked for.

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
composite parent. (Since `2.1.0.0` each external hub is also a devnode
under a project-owned id, 10.11, and since task 36.43 each split composite
device has a composite-parent devnode the same way, 10.12; neither binds
an OS hub or composite driver.) This section is the design that follows: the hub class the
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

**Suspend and resume**, the minimum (selective suspend was not on the
roadmap when this was written; since `2.3.0.0` it is, off by default,
design record 14, and its suspend and resume sequences at a root port and
a USB 2.0 hub port, sections 3.4 to 3.6, apply the timings below; amended
in 36.20):
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
(review of the merge, printed pp.446-454); no QEMU model has a SuperSpeed
hub, so `test_sshub`'s `test_resume` vectors are the only evidence until
the bench (30-E.1). Since roadmap-hcd 36.38 (selective suspend's stage B,
design record 14 section 8.6), a device that selective suspend holds is the
exception: with `XhciSelectiveSuspend` 1 its port's U3 is the machine's,
neither `Resume` nor `Resumed` is acted on by the look, and a finished U3
exit is handed to the machine as evidence; with the switch at 0 no device
is held and this paragraph is unchanged.

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

**A function PDO** (one per function of a split device; since task 36.43 a
child of the device's composite-parent PDO, whose own ids are section
10.12's; the function's ids below are unchanged by it):

| Query | Strings, in order |
|---|---|
| `BusQueryDeviceID` | `USB\VID_vvvv&PID_pppp&MI_nn` |
| `BusQueryHardwareIDs` | `USB\VID_vvvv&PID_pppp&REV_rrrr&MI_nn`, `USB\VID_vvvv&PID_pppp&MI_nn` |
| `BusQueryCompatibleIDs` | `USB\Class_cc&SubClass_ss&Prot_pp`, `USB\Class_cc&SubClass_ss`, `USB\Class_cc`: for an IAD function from the IAD's `bFunctionClass` / `bFunctionSubClass` / `bFunctionProtocol`, as Microsoft's "Support for interface collections" gives them; for any other function, a legacy audio group included, from its first interface (alternate 0). Decided 2026-10-03 (review of batch (c), round 19, finding 5); it was open (10.10) |
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
  device does not come back. The place is the instance key **under the
  same parent** (`XhciFuncRetireByPlace`, host vectors in `test_func`):
  the key, `(route << 8) | root port`, names no hub, so before this rule a
  serial-less device C dormant at port 1 of a serial-named hub A, with A
  moved to another root port and a hub B carrying a device D put where A
  was, was retired by D's listing although A revived and still reported
  it, and only B's side was invalidated - C's START failed and C sat as a
  failed devnode under A (review of the 33.1-33.6 integration,
  finding 3). Now C waits out its START and goes then, its own parent
  invalidated. A serial-less hub replaced by a different hub at the same
  root port still retires the old hub's group, and its children with it.
  Two identical units swapped while disabled are taken for each other, as
  before.
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
which no INF names. Its device id, though, keeps the VID/PID form
(`USB\VID_vvvv&PID_pppp[&MI_nn]`; `hcdDeviceQueryId` routes only the
hardware and compatible ids through the transport), and so does its
instance path. So "no INF line can bind it" holds for the hardware ids
only. Read on Windows 98 SE under NUSB 3.3 (runtime, 2026-10-13, task 38.2,
a `usb-uas` refused in place): the refused devnode's `HardwareID` value
carries the device id as a second entry,
`USB\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000,USB\VID_46F4&PID_0003`,
and the wizard found no driver for it. Not read: whether an INF whose only
line is the VID/PID form binds a refused device on 98 SE or ME; whether a
device with a serial number, once its working form (High Speed, or a
controller that streams) is installed, brings that devnode's driver back
to its refused form, since both answer the same device and instance id;
and how either goes on NT. Roadmap task NOX.1, deferred, reads them
(10.7.1).
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
unread (10.7.1); if it does, a hand-listed `VID&PID` still wins there,
roadmap 31-A.3's residual case.

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
  names the port and index. **Not read** for a hub, whose PDO answers its
  fixed name (section 10.11), nor for a group that looks set to revive a
  dormant one on Windows 98 SE or ME ("Kept" below): read there,
  the text is thrown away, its time counts against the dormant `START`'s
  10 s wait, and a timeout's reset reports the device gone before its
  revival is tried - for a hub, the revived hub reported gone and its
  replacement given a new `Serial` that its dormant children can never
  match (review of the 33.1-33.6 integration, findings 1 and 2).
  "Looks set to" is a preview taken once the group is built: the
  revival's own choice (`hcdDormantChooseLocked`, which `hcdDormantRevive`
  now calls too) under `hcdSerialLock` and `PdoListLock`, changing
  nothing - by place, by serial id unless another controller carries it,
  and on the location form when the serial id is a duplicate - and the
  reads run, outside every lock, only when it finds nothing. A plain
  hint under `PdoListLock` alone (by place and by serial) was tried first
  and dropped at review: it missed a device whose earlier reads failed
  and whose serial id is now a duplicate, which revives its location
  group only after the duplicate check, so its text reads, and a timeout
  in them, still came first (review of this fix, round 1). The
  preview is still not the decision, because the locks are let go for
  the reads: the decision that counts is `hcdDormantRevive`'s, after
  them, and should a dormant group come or go in between, the new PDOs
  answer `USB Device` for that plug or the reads ran for nothing; the
  identity is the decision's either way.
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
  status passed through. XP SP3's `usbhub` answers it with the same
  `iProduct` string as the description (static, read 2026-10-05; section
  10.11, "Address"); no other target's was read.

**The bus never emits `USB\COMPOSITE`.** In Microsoft's stacks the composite
device's own PDO carries it as a compatible id, and that is what brings the
parent driver: `usbhub.sys` on 98 SE and 2000, `usbccgp.sys` from XP on and on
98 SE under NUSB 3.6 or SweetLow. Under the HCD the bus is the composite
parent, so the id would bring one of those back on top of an already-split
device, against the "no INF binding path for a composite parent" decision.
**Amended 2026-10-10 (task 36.43, section 10.12):** the bus does now present
a parent for every device it splits, under the project-owned
`XHCI98\COMPOSITE`, bound by this package's own INFs to `xhci98.sys` -
still never `USB\COMPOSITE`, for the reason above.
Also never emitted: `USB\ROOT_HUB` and `USB\ROOT_HUB20` (the root hub is
`XHCI98\ROOT_HUB`, section 8.6, and the INF gate's `HCD-ROOTHUB` refuses
them), and `USB\CLASS_09*`, because a hub is never a PDO. `USB\MS_COMP_*` is
out of scope for 2.0.0.0.

#### 10.7.1 A refused interface's device id (task NOX.1, deferred)

**Deferred.** The owner deferred roadmap task NOX.1 on 2026-10-13 until
after `2.4.0.0`. This section is the plan for that task. It has not been
reviewed, and nothing in it is implemented. The answers the owner gave the
same day are recorded below as decisions taken.

**What was read.** Task 38.2's legs (runtime, 2026-10-13; `runs/run-38.md`,
"38.2"; `out\p38-2\legs\notes.txt`, `w98\R2ENUM.txt`) used Windows 98 SE
under NUSB 3.3 with `-device qemu-xhci,p2=1,p3=4,streams=off`. A `usb-uas`
was attached at port 3, a SuperSpeed port with no USB 2.0 companion, and was
refused in place. The wizard found no driver for it. The devnode it left,
`Enum\USB\VID_46F4&PID_0003\P382R2`, read:

`"HardwareID"="USB\\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000,USB\\VID_46F4&PID_0003"`

The PDO answers only the first of those as a hardware id. The second is
its device id. `hcdDeviceQueryId` (`hcd_pdo.c`) sends a refused PDO's
hardware and compatible ids through `hcdXportQueryId`, but answers
`BusQueryDeviceID` in the ordinary form: `USB\VID_v&PID_p`, with `&MI_nn`
for a function (`hcdFunctionQueryId`). So Windows 98 SE added the device id
to the value. Which of its components does that is not read.

On every other devnode in the same export, the device id is already the
second hardware id. For a location-form instance id, 98 SE appends the
parent's path with `\` turned into `&` (`1XHCI98&ROOT_HUB&0PCI&...`). A
serial id is kept as answered.

**The two routes the task reads.**

1. **An INF line matched against the device id.** If the 9x driver search
   matches against that value, then a hand-listed `USB\VID_v&PID_p` line
   binds a refused device. NUSB 3.3's `USBSTOR.INF` (SHA-256
   `e78b5f2d2d30b039d74d670f09ba41c39bdfca058b72fd7c22eb7d55f81f0f6b`) has
   135 such lines and 6 `&MI_nn` lines (static, a text read). A vendor INF
   would do the same.
2. **The devnode its working form installed.** Take a device with a usable
   serial number. It answers the same device id and instance id whether it
   is refused, or working at High Speed or on a controller that streams.
   Once the working form is installed, the refused form comes back to that
   same devnode. That would load the stored driver with no INF search, and
   it could do so on NT too. `xhciuas.sys` fails its start with fewer than
   two streams at SuperSpeed (`uas_fdo.c`).

**The reading.** It needs no code: every release since `2.0.0.0` answers
these ids. It runs on a published `release-x86`.

- **The vehicle.** QEMU 11.1.0 under TCG, with the kit of
  `out\p38-2\kit2`; its `dev.ps1 uas` gains a serial argument. The guests
  are 98 SE under NUSB 3.3 (`vm\t26\win98-gold.qcow2`), stock ME
  (`vm\t37me\winme-stock-base.qcow2`) and Windows 2000 SP4
  (`vm\t26\win2k-gold.qcow2`). Each takes 1 vCPU and 256 MB, on its own
  overlay.
- **Every plug uses a serial never seen before**, on the refusing layout
  `p2=1,p3=4,streams=off` at port 3.
- **The probe INFs are this project's own.** Each declares `Class=USB` and
  an install section that installs nothing (on NT, `AddService=,2`). Probe
  V's one line is `USB\VID_46F4&PID_0003`. Probe C's one line is
  `USB\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000`. C is the control: it
  must bind, or V's result means nothing.
- **"Binds" means** the probe's line is chosen for the devnode: its
  `DeviceDesc`, its `Driver` value and its Device Manager name all come
  from the probe.
- **On 98 SE and ME:**
  - Offer V, then C, at the first plug's wizard.
  - Put V in `C:\WINDOWS\INF\OTHER`, delete `DRVIDX.BIN` and
    `DRVDATA.BIN`, and plug with nothing offered.
  - After each plug, export `HKLM\Enum\USB`.
- **On 2000:**
  - Read the `Enum\USB` value.
  - Offer V through Found New Hardware, and through Have Disk with
    compatible hardware only. Offer C as the control.
  - Microsoft documents NT's search as matching the hardware ids and the
    compatible ids. That is documentation; the leg checks it.
- **Route 2, on all three guests.**
  1. Plug serial S on `p2=4,p3=4` with streams on, and install
     `XHCIUAS.INF`.
  2. Relaunch the same overlay on the refusing layout and plug S again.
  3. Read whether the wizard comes back, and the devnode's name, status,
     `HardwareID` and `Driver`, and take an `XHCISNAP` dump.
- **Static.** Windows ME's `usbstor.inf`, extracted from its CD for 10.6.
  A 98 SE without NUSB is not read, because no base here lacks NUSB.

**What each outcome changes.**

- **A driver binds by either route, on any target.** The refused PDO's
  device id becomes `USB\XHCI98_NOXPORT&VID_vvvv&PID_pppp[&MI_nn]` on every
  target. That form is the second hardware id too, as on every other PDO.
  - `XhciXportId` gains the device-id form.
  - `hcdDeviceQueryId` routes `BusQueryDeviceID` to `hcdXportQueryId` when
    `Xport.Transport` is `XHCI_XPORT_REFUSED`, before its `pdo->Function`
    fallthrough.
  - The device id follows `pdo->Xport`. That holds a PDO to one identity,
    because dormant revival compares `Xport` (`hcdDormantSame`) and
    re-presentation copies it (`HcdDevicePdoRepresentFunctions`).
  - Host vectors go in `test_xport`.
  - The longest device id grows from 27 to 42 characters, and the longest
    NT instance path from 157 to 172, within 200.
  - The instance path moves within `Enum\USB`. A refused device's first
    plug after the update brings the wizard once more.
- **Nothing binds by either route.** Only the documents change. They say,
  tagged runtime, where the device id was read to be harmless.

**Decisions taken (owner, 2026-10-13).**

1. The device id is changed only if a reading shows a driver binding by
   either route. Otherwise only the documents change.
2. If it changes, it changes on every target.
3. 98 SE is read under NUSB 3.3, with stock ME and 2000 SP4 alongside. The
   record states that a 98 SE without NUSB is unread.
4. Old refused devnodes are left alone. 38.8's cleanup tool does not
   remove them. Such a key claims nothing, and when the device has a serial
   number it is the device's own working devnode.
5. 38.8's INF gate refuses any INF of this package that names
   `USB\XHCI98_NOXPORT`.
6. No `usbstor.sys` probe is run. Whether a line binds is what decides the
   change.

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

Superseded on 2026-10-03 (owner and coordinator, after review of batch
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
for its function driver to configure. **Since task 36.43 (10.12)** a split
device is presented as a composite-parent PDO under `XHCI98\COMPOSITE` with
its function PDOs beneath it, the parent first on the group's chain; when
and how the bus splits is unchanged.

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
   own (superseded on 2026-10-03, review of batch (c), round 19,
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
to it and the list of interface numbers it owns. Since task 36.43 the
functions are the children of a composite-parent devnode (10.12), which is
presentation only: it is never on the data path, answers no internal IOCTL,
and every row below is unchanged by it. Microsoft's parent has the
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
| power | a function's D-state is its own; the device stays D0 while any function is D0 (selective suspend is outside the roadmap, 28.3). **Amended 2026-10-10 (36.43, 10.12):** the device's link follows its composite parent's children, the function PDOs; the parent PDO is not a voter, and under record 14's stage A it is excluded as a hub PDO is. **Amended 2026-10-10 (36.20):** "outside the roadmap" no longer holds - selective suspend ships in `2.3.0.0` behind `XhciSelectiveSuspend`, off by default (record 14 section 7); at 0 this row stands as written, and at 1 the device's link is suspended once its last function leaves D0 (record 14 section 4.3) |

**A function's polling interval is the caller's** (2026-10-05, whole-branch
review of `2.1.0.0`, area B, incidental 2). A function's
`SELECT_CONFIGURATION` builds each endpoint from the device's own
configuration (the one 10.8's SET_CONFIGURATION selected), but takes the
`bInterval` of the descriptor the caller passed in when that descriptor's copy
of the same interface and alternate carries an endpoint whose first six
bytes (`bLength`, `bDescriptorType`, `bEndpointAddress`, `bmAttributes`,
`wMaxPacketSize`) equal the device's (`XhciPipeCallerInterval`,
`hcdCfgFunctionEndpoint`). That is the descriptor the synthesised
GET_DESCRIPTOR above returned, as the class driver received it, so a filter
below the class driver that rewrites `bInterval` (hidusbf) reaches a function
as it already reached a whole device, whose select parses the caller's
descriptor. Before this, a composite mouse's function ignored hidusbf's rate
and `XhciFastPollFsLs` (section 13.5) saw the device's own `bInterval`.
**Still the device's**: a SuperSpeed function's companion fields (Max
Burst, Mult, Max ESIT Payload, read from the device's configuration; since
task 36.19 its `bInterval` is the caller's matched byte too, section 13.7),
and a later
`SELECT_INTERFACE` on a function, which reads the device's configuration -
so a client that selects an interface after its configuration loses a
rewritten interval (no HID class driver of any target does). Host vectors:
`test\test_pipe.c`, `test_caller_interval`.

Section 9.5's harness definition follows from this table: `endpoints opened`
counts endpoints added at a function's `SELECT_CONFIGURATION` or
`SELECT_INTERFACE`, and nothing the bus opens for itself.

### 10.10 What 25.6 leaves open

| Open item | Binds |
|---|---|
| Every **(to transcribe)** USB 2.0 number above: 4.1.1 tiers; 7.1.7.3 `TATTDB`; 7.1.7.5 `TDRST`, `TRSTRCY`; 7.1.7.7 `TRSMRCY`; 9.2.6.3 `TDSETADDR`; 11.12.4 the bitmap; 11.23.1-2 the hub descriptor fields and the status endpoint's `bInterval`; 11.24.2 the TT requests' `wValue`; Tables 11-13, 11-16, 11-17, 11-21 and 11-22. The specification is added to `docs/references/` with its hash first. | 27-A.1 (one transcription batch) |
| `BusQueryCompatibleIDs` for a device-class device (the `DevClass` forms, read from the hub drivers' id order statically), and the instance-id character set on Windows 98. Since task 33.2 a serial id brings any of `0x21`-`0x7E` but `,` and `\` into a Windows 98 instance id (10.7); its guest legs on Windows 98 SE and ME are where that set is first observed. | 26-A.4; 33.2 |
| **Closed 2026-10-03 by decision, not by a static read** (owner and coordinator, review of batch (c), round 19, findings 3 and 5): the IAD function's compatible ids come from the IAD (10.7), and the split follows Microsoft's composite-parent rule - one configuration, two or more interfaces, device class 0 or `EF/02/01` - so no other device class and no multi-configuration device is split (10.8). It read: the IAD function's compatible ids (from the IAD or the first interface: `ParseUSBInterfaceAssociationDescriptors` / the id builders), the device-class values Microsoft splits besides 0, and the multi-configuration rule - static reads of `usbccgp.sys` and the hub drivers. The X4's IAD fields have still not been read. | 26-A.7 |
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
that plug (`hcdSerialTakenLocked`; review of 33.4, final round,
finding 1). Under the same parent one answer omits the old and carries
the new, as 33.2's move between root ports relies on.

**Capabilities.** `Removable` TRUE, `SurpriseRemovalOK` TRUE, `UniqueID`
TRUE exactly when the instance id is a serial id (as any PDO's), `Address` and `UINumber` its port on its parent (as any PDO's, "Address" below). `SurpriseRemovalOK` TRUE so
the XP-onward hot-plug applet does not offer the hub itself for safe removal
while a storage device behind it keeps its own entry; the applet's rule was
not read (an open item, read on the XP guest).

**Address** (2026-10-05, branch `p33-addr`; a defect of `2.1.0.0` found on
its VM leg 2c, Windows XP SP3). Every PDO answered `Address` and `UINumber`
with its instance key, `(route << 8) | root port`, which was the port only
for a device on a root port. Since 33.4 a device behind a hub is that hub's
devnode's child, and Microsoft's hub driver answers `Address` with the
device's port **on its parent hub**: XP SP3's `usbhub.sys` copies the port
number `USBH_CreateDevice` stored in the PDO extension, Windows 7's a word
of its PDO extension (static, `legal-provenance.md` section 4). A tool may
take `Address` as the connection index to ask the parent for, and hidusbf's
`Setup.exe` (2026-10-03 build) does: it reads the device's
`SPDRP_ADDRESS`, and when that is not 0 it sends the parent hub
`IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX` with it as
`ConnectionIndex` (followed by `_EX_V2` when it succeeds, replaced by the plain
form when it fails); only an `Address` of 0 makes it walk
the hub's ports by `GET_NODE_CONNECTION_DRIVERKEY_NAME` instead (static,
`legal-provenance.md` section 4). On leg 2c (XP SP3, runtime) its "Copy IDs"
showed no `BusSpeed` or `bInterval` for a Full-Speed mouse on port 1 of a hub
at root port 3, which answered 259, and it wrote High-Speed values for it;
at a root port it showed both (on Windows 98 SE and ME it found the device
another way). **The rule** (`XhciHubPdoAddress`,
`xhci_hub.c`, host vectors in `test_hub`; fixed at creation, kept by a
revived PDO with its other place fields): a device on a root port answers
the root port, unchanged; a device whose own hub has a PDO - so is the
parent it is presented under - answers the last tier of its Route String,
the downstream port of that hub, which is the connection index that hub
FDO's door serves it at (`hcdDoorConnection` matches the PDO's port
location under that parent); every function PDO of a split device answers
its device's. **A device whose own hub has no PDO** (re-parented to the
next hub up, or to the root hub) has no connection index under its
presented parent - that parent's door serves the PDO-less hub's own record
at the ancestor port, or nothing - so it keeps the instance key, a value
above any port, rather than name a port whose answer is another device.
`UINumber` answers the same number. That is a departure: XP's `usbhub`
leaves `UINumber` 0 (zeroed at creation and, as far as the static\r
reading reaches, never written) and Windows 7's sets -1. It is kept a
port because, with `DeviceTextLocationInformation` unanswered (section
10.7), `UINumber` is what NT's Device Manager shows on the Location line,
and a port reads truer than 0 or 259 there. XP's `usbhub` answers
`DeviceTextLocationInformation` with the `iProduct` string (static, the
same row); answering it is not taken here. The instance key itself, the
instance ids of 33.2 and the dormant matching of 33.1 are unchanged. The
helper's loop is written around an MSVC 6.0 code-generation fault that
made its first form answer the instance key on the guest while its host
vectors passed (`lessons.md`, the last entry). **Read on the XP SP3 guest**
(runtime, 2026-10-05, development host A, the `qemu` build of `a9dd367`,
a QEMU `usb-hub` at root port 3 and a mouse on its port 1): the bus's trace
read the mouse's answer as `Address` 1 under the hub PDO; `Setup.exe`'s
"Copy IDs" showed `BusSpeed: 1 (Full)` and `bInterval: 10`; 1000, 500 and
250 Hz set through it gave the Output Endpoint Context `Interval` 3, 4 and
5 (HMP `xp` of the controller's device context); and Device Manager's
Location line read "Location 1". Windows 7's Location text was not read.
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
never waits for an answer no FDO can give (review of 33.4, round 1).

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
   **Corrected 2026-10-05** (roadmap-hcd 33.4; the 2.1.0.0 legs 1e on
   Windows 2000 and 1g on XP, runtime, development host A, QEMU): enabling
   it re-adds the FDO, but re-reporting the same PDOs brings nothing back.
   With the hub's devnode, NT forgets its children's devnodes, and the PDO
   objects reported again to the new FDO were never queried or started; no
   port changed, so nothing re-enumerated, and only a replug recovered. So
   the hub FDO's `REMOVE` **lets go** of every child that PnP was shown and
   has removed, still listed (`HcdDevicePdoLetGo`, `ParentLetGo`, pure rule
   `XhciEnumLetGo`, host vectors in `test_enum`). No relations answer
   carries a let-go PDO again (`XhciEnumAnswerCarries`), and once its
   device leaves it is missing at once, like a PDO under a hub PDO that
   PnP has let go of. The device keeps running. The next `START` of a hub
   FDO on that PDO cycles each let-go child's port (`HcdDevicePdoRepresent`,
   `CYCLE_PORT`'s path, `HcdEnumCycle`) before it asks its settle
   generation (5.7). The old PDOs leave, missing at once, and are deleted
   by the next relations answer before any answer carries their
   successors. The devices enumerate afresh, behind a second-tier hub too,
   as new PDOs at the same instance ids: the serial id, or the place under
   the same hub devnode. PnP finds the devnodes it had, so no new devnode
   and no wizard, and the first answer carries them. A child that PnP
   never saw is not let go: the new FDO's first answer reports it. A let-go
   PDO that PnP starts again is PnP's again. Windows 98 SE `STOP`s the hub
   instead of removing it (the 33.4 leg 1c and this fix's 98 leg). That
   leaves nothing let go and changes nothing there. A controller stop never
   keeps a let-go group dormant, unlike a user-disabled PDO (item 6). PnP
   has no devnode to revive it into, and a revival could come after the
   hub's start had already asked for its cycle, and lost it. So the stop
   drops the group, missing at once, and the device comes back as new PDOs
   (review of this fix, round 1). The root hub needs nothing of this: its `REMOVE` detaches the bus,
   and its `START` marks every port changed (`HcdEnumDetach`,
   `HcdEnumAttach`).
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
   still holds it present (review of 33.4, rounds 1 and 2). A
   composite with one function disabled is kept whole the same way. The
   residue: a disabled device unplugged while the controller is off stays
   listed, disabled, until a device enumerates at its place or its hub's
   dormant group is retired. At the restart the hub re-enumerates first and revives
   its dormant PDO by 33.2's serial-or-place match, keeping its `Serial`,
   so the
   children enumerated behind it compute the same `ParentSerial` and revive
   theirs; `ParentSerial`, `Hub` and `HubUsb3` join the descriptors in the
   sameness test, so no group is revived under another parent. A dormant group retired (not revived) takes the dormant groups
   below it with it. A newcomer retires a serial-less dormant group for its
   place only under its own parent (section 10.7), and a hub's PDO reads no
   device text, so no `iProduct` read whose timeout could reset the
   controller comes before a hub's revival (33.2's serial-number read
   still does, for a hub with `iSerialNumber` set, as for any device).

**The hub FDO** (`hcd_hubfdo.c`): `AddDevice` for a hub PDO; PnP passed down
like the root hub FDO's, with `BusRelations` answered as above and
`START`'s success making the door and opening its IOCTLs, which a `STOP`,
`SURPRISE_REMOVAL` or `REMOVE` closes before going down, so no open handle
keeps the PDO's `Busy` raised while its quiesce waits (round 1, finding 2);
a `REMOVE` that only tears down the
door and itself - it never detaches the bus. Power: the hub FDO is its
stack's system-power policy owner, D3 on a sleep and D0 on the wake, and
records and reports its device states (10.12, "Power", which this code
shares since task 36.43's follow-up); the hub PDO answers power as a device
PDO does. A hub PDO answers no internal IOCTL
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

### 10.12 Composite devices as devnodes (task 36.43, 2026-10-10)

**The decision.** The owner, 2026-10-10, after SweetLow's comment on GitHub
issue 17 ("You loose 1:1 mapping between devices on USB port of the HUB and
devices in Device Manager as children of USB Hub"): every composite device
the bus splits (10.8) gets a **composite-parent devnode** of its own, with
its function PDOs as that devnode's children, so one hub port maps to one
devnode, as on every Microsoft stack, where the composite device's own PDO
carries `USB\COMPOSITE` and `usbccgp.sys` (or Windows 98's `usbhub.sys`)
presents the functions beneath it. **The pattern is 10.11's**: the parent's
PDO is presented under a project-owned id, `XHCI98\COMPOSITE`, bound by this
package's own INFs to `xhci98.sys`, which attaches a **composite FDO** over
it - this driver's fourth PnP role. **Still no binding of `usbccgp.sys` or of
Windows 98's composite parent**, and the bus still splits, configures and
routes exactly as 10.8 and 10.9 say; only the PnP presentation changes, as
it did for hubs. This supersedes, wherever this record says it, "a
composite device gets no PDO of its own" and "the bus never emits a parent"
(5.2's object table, 10's introduction, 10.7's last paragraph, 10.9's power
row, 8.10's split-composite row and its USB-device-interface paragraph), and
the decisions table's "no INF binding path for ... a composite parent"
(roadmap-hcd.md, amended the same day). Written before the code, on branch
`p36-comp`.

**What it buys, and what it costs.** One devnode per port in Device
Manager's view by connection, `usbview` and the Power tab; one driver key
for `GET_NODE_CONNECTION_DRIVERKEY_NAME` (closing 8.10's row); the USB
device interface of task 36.17 on the device rather than on one function;
a device-level devnode to disable, and where record 14's composite rule is
stated. The cost: every composite device is one devnode more, installed
from this package's INF once per device instance - with nothing to answer on
Windows 98 SE and 2000, as the hubs were (10.11), but **with Windows XP's
Found New Hardware wizard and unsigned-driver warning for each new
composite device**, as each new hub brings there; and an update from
`2.2.0.0` re-detects each composite device once (below). The release notes
say both.

**Object model.**

| Object | Kind | Created by | Parent in the PnP tree | Carries |
|---|---|---|---|---|
| Composite-parent PDO | an `HCD_DEVICE_PDO` with `Composite` set | the controller thread, in `HcdDevicePdoCreate`, for every device `XhciFuncSplit` splits, with its function PDOs | the hub PDO the device is presented under, or the root hub (`ParentSerial`, 10.11) - the place a device PDO would have | the whole configuration descriptor, the device's ids under `XHCI98\COMPOSITE`, its instance id, text and capabilities (below); the group's serial (`Group` = its own `Serial`) |
| Composite FDO | `HCD_KIND_COMPOSITE_FDO` over the parent PDO, the hub FDO's extension and code (`hcd_hubfdo.c`) | `AddDevice`, for a PDO of this driver's with `Composite` set | - | `BusRelations` of the functions; no door |
| Function PDO | unchanged (`Function` set, 10.7 to 10.9) | the thread, with the parent | the composite parent: `ParentSerial` is the parent PDO's `Serial` | unchanged; `Group` is the parent's `Serial` |

The group is the parent and its functions, in that order on the `Sibling`
chain (`dev->Pdo` names the parent): created before any is listed, listed
in one hold of `PdoListLock` and announced by one invalidation of the
parent's own parent, as 26-A.7's group was; a failure to create any of them
fails all of them, as before. A device the bus does not split is unchanged:
one device PDO, no parent. QEMU's `usb-audio`, one audio function (10.8's
legacy rule groups its AudioControl and AudioStreaming interfaces), is a
composite: one parent with one function beneath it, as `usbccgp` presents
the same device.

**Ids** (project-owned; `XhciFuncParentId`, `xhci_func.c`, host vectors in
`test_func`):

| Query | Composite-parent PDO |
|---|---|
| `BusQueryDeviceID` | `XHCI98\COMPOSITE&VID_vvvv&PID_pppp` |
| `BusQueryHardwareIDs` | `XHCI98\COMPOSITE&VID_vvvv&PID_pppp&REV_rrrr`, `XHCI98\COMPOSITE&VID_vvvv&PID_pppp`, `XHCI98\COMPOSITE` |
| `BusQueryCompatibleIDs` | none |
| `BusQueryInstanceID` | any PDO's rule (`hcdInstanceQueryId`, 33.2): the device's serial id when it has a usable one, `UniqueID` TRUE, else the location key in decimal, `UniqueID` FALSE - what the device would answer were it not split |
| Device text | its `iProduct` (33.6, read once per enumeration and kept like any PDO's), else `USB Composite Device`, the name Microsoft's own INFs give the parent; the INF's model text replaces it once installed. Each PDO's strings are picked by its own function (`pdo->Func`, none for the parent), never by its place on the chain, which the parent now heads |

Never `USB\COMPOSITE` (it would bring `usbccgp.sys` or Windows 98's
`usbhub.sys` onto an already split device, 10.7), never `USB\VID_&PID_`
(a vendor INF that lists the whole device would then bind a driver over the
parent, above functions it cannot reach), never a `USB\Class_` id. So a
vendor INF written for the whole device still matches nothing here, as
before 36.43: under Microsoft's stack such an INF outranks `usbccgp`'s
compatible-id match and takes the device whole; under this bus the device
is always split. Recorded, not changed. The function PDOs' ids are 10.7's,
unchanged - the device id `USB\VID_vvvv&PID_pppp&MI_nn`, the instance id
the serial id and `&nn` or the location key and `nn` - so the class INFs
bind them as before.

**Capabilities.** The parent: `Removable` TRUE, `UniqueID` as its instance
id, `Address` and `UINumber` its port on the parent it is presented under
(`XhciHubPdoAddress`, as a device PDO's), and **`SurpriseRemovalOK` TRUE**,
as a function and a hub answer it, so the XP-onward hot-plug applet lists
no composite device, as before 36.43 (orchestrator decision, owner to
confirm: `FALSE` would list every composite device - a keyboard with media
keys, a headset - for safe removal, a change of what users see; the parent's
devnode can still be disabled). D-states as a device PDO's. The functions:
unchanged (`SurpriseRemovalOK` TRUE, `Address` the device's port, so a
tool that asks a hub door for a function's `Address` still finds the
device there).

**The composite FDO** (`hcd_hubfdo.c`, the hub FDO's code with the role
told apart by its kind): `AddDevice` for a parent PDO; `START` passed down
first, then the in-place re-presentation below, then the settle generation
of 5.7 asked, so its first `BusRelations` answer waits for both;
`BusRelations` answered by `HcdDevicePdoRelations(hc, old, parent's
Serial)`, as a hub FDO's; `REMOVE` lets go of the children PnP has removed
(`HcdDevicePdoLetGo`, 10.11 item 4) and detaches; `STOP`,
`SURPRISE_REMOVAL` and the queries passed down. Power: the system-power
policy of "Power" below, shared with the hub FDO, and nothing requested in
S0 (record 14). **No door**: no
`\DosDevices\` name, no interface, `IRP_MJ_CREATE` and
`IRP_MJ_DEVICE_CONTROL` refused `STATUS_NOT_SUPPORTED` - a composite device
is a connection of its hub, which the hub's door already serves.

**The parent PDO** answers PnP as a device PDO does (the lifecycle of 5.2
and `hcd_pdo.c`, the dormant path, `START`'s refusal once gone) and **no
internal IOCTL** (`STATUS_NOT_SUPPORTED`, as a hub PDO): nothing above the
composite FDO sends a URB, and it answers no `USB_BUS_INTERFACE_USBDI`
query. Its `START` feeds the port's machine `PDO_STARTED` (5.3), as a device
PDO's does; the functions' starts feed it too, as before, and a second one
in `Bound` is ignored.

**URB routing** (10.9): unchanged. A function PDO's internal IOCTLs reach the
device record directly, as before; the parent is never on the data path,
which is what makes the change presentation only. PnP removes a parent's
children before it, so no function sends a URB once its parent is gone.
`RESET_PORT` from a function resets the whole device and keeps every
sibling's pipe handles, as before, the parent untouched. `CYCLE_PORT` from a
function, and a `RESET_PORT` that failed, take the whole group - the parent
with its functions - away (`HcdEnumCycle` on the group's serial, which is
now the parent's), and the device re-enumerates once the group has been
reported missing, as before.

**Relations and lifetime: 10.11's rules, with "hub PDO" read as "parent
PDO"** - a hub's or a composite's (`HcdPdoIsParent`). The ancestor rule
(`hcdAncestorGoneLocked`), invalidation of a started parent
(`hcdInvalidateFor`), the dormant tier rule and the let-go rule each find a
parent by its `Serial` whether it is a hub or a composite. In particular:

1. **The device leaves** (unplug, hub teardown, controller stop): the group
   is unlisted in one hold (`HcdDevicePdoGone`); the parent's own parent's
   next answer omits the parent, and the same answer's sweep marks the
   functions missing by the ancestor rule, so the port's wait on the group
   ends with that one answer. NT surprise-removes the parent's subtree and
   removes it leaf first; Windows 98 removes it.
2. **A function disabled** in Device Manager: unchanged (a `REMOVE` while
   listed; the PDO kept; its endpoints and alternates released, 10.9).
3. **The parent disabled** (NT): PnP removes the functions, then the
   composite FDO, then sends the parent PDO its `REMOVE` (listed, so kept:
   the WDM rule). The FDO's `REMOVE` lets go of the functions
   (`ParentLetGo`), since PnP forgets their devnodes with the parent's
   (10.11 item 4); the device stays configured with no client. Each
   function's own `REMOVE` **queued** the release of its endpoints and
   alternates (`HcdCfgReleaseFunction` records `FuncRelease`; the thread
   runs it, retrying a refused one a bounded number of times, 10.9), and
   the release-before-reconfigure order `HcdCfgService` keeps puts that
   release ahead of any later select of the same interfaces. **The enable
   re-presents the functions in place**: the parent PDO's `START` makes it
   PnP's again, and the composite FDO's `START` asks the thread
   (`HcdEnumRepresent`, by port and group serial, so a request that outlives
   its device does nothing) to build, for each let-go function, a new
   function PDO from the device record's configuration - the same function,
   ids, text, serial id, instance key, `Address`, transport and parent -
   swap it into the group's chain, list it, and unlist the old one, which
   is missing at once and, its `REMOVE` already seen, goes to `RemovedPdos`
   (`HcdDevicePdoRepresentFunctions`). The next relations answer, of any
   parent, deletes it while it builds its list - before PnP receives that
   answer, which may be the first to carry its successor - so PnP never
   meets two objects at one instance id, and finds the id it had on the
   new one: the functions come back as the devnodes they were, with no
   wizard. No re-enumeration and no reconfiguration: the device is never
   reset, and each new function selects its configuration as any class
   driver does. **Not the hub's cycle**: cycling the port, as a hub FDO's
   start does for its children, would take the parent itself - which PnP
   has just started - away and back.
   **The request's life.** Posted before the FDO's start asks its settle
   generation (5.7), it is taken by the thread's next pass, which reads
   that generation first, so a pass that can settle it has already
   re-presented; the settle may still have ended early (no wait configured,
   or its deadline), so the thread also **invalidates the parent** once
   it has listed a replacement, and PnP asks the composite FDO again
   whenever that comes. A device no longer the group's when the pass runs
   drops the request. A replacement that cannot be built (no pool, an
   `IoCreateDevice` failure) leaves its function let go and absent - traced,
   not retried - until the next start of a composite FDO on that parent (a
   disable and enable) or the device's next plug; the others are
   re-presented regardless. On Windows 98 SE and ME a disable may `STOP`
   rather than remove, as for a hub; then nothing is let go and nothing
   changes (read on the guests).
4. **A composite behind a hub whose devnode is disabled**: the hub FDO's
   `REMOVE` lets go of the parent (its direct child) and the composite FDO's
   `REMOVE`, which PnP sends as part of the same subtree, lets go of the
   functions; the hub FDO's next `START` cycles the parent's port
   (`HcdDevicePdoRepresent`, 10.11 item 4), and the whole device comes back
   as a new group under the same instance ids.
5. **Windows 98 SE and ME's dormant path** (33.1, 10.11 item 6): the group,
   parent included, is kept dormant whole when PnP has stopped every PDO of
   it; a function the user disabled (removed while listed) is kept with its
   group, as behind a hub, because its parent is the composite parent, not
   the root hub; the parent disabled under the root hub is not kept, as a
   lone device there is not. The sameness test compares the chains
   position by position; a function's `ParentSerial` is not compared,
   since it names the old parent, which revives with its `Serial`
   unchanged, so the revived functions name the revived parent. A dormant
   group retired whole - by a dormant PDO's `REMOVE` or its `START`'s wait
   (`hcdDormantRemoved`, `hcdDormantWait`), which a function can be the one
   to receive - invalidates the parent its **first PDO** is presented under
   (`hcdGroupParentLocked`), never the function's own `ParentSerial`, which
   names the composite parent just retired with it: behind a present hub
   only that hub's answer reports the parent missing and ends its port's
   wait.
6. **The controller's stop and removal**: unchanged
   (`HcdDevicePdoReleaseAll`); a composite FDO reaches the controller only
   through its PDO's `Controller` inside the PDO's `Busy`, the hub FDO's
   guard.

**Serial ids** (33.2): a serial id is held by a **group's first PDO** - a
lone device PDO, a hub PDO, or a composite parent - and the duplicate and
dormant tests compare first PDOs only (`hcdSerialTakenLocked`): a
function's `ParentSerial` names its own parent, a new one each plug, so
comparing it would take every quickly moved composite device for a
duplicate under "another parent". Functions carry the parent's serial id
with `&nn` and lose it with the parent when it is a duplicate, as before.

**The door** (section 8): a connection's devnode is the group's first PDO
(`hcdDoorConnection`), which is now the parent. So
`GET_NODE_CONNECTION_DRIVERKEY_NAME` returns the **parent's** driver key -
the one devnode `usbui.dll` and `usbview` map the port to, closing 8.10's
split-composite row; `GET_NODE_CONNECTION_INFORMATION`, its `_EX` and
`_EX_V2` forms and the descriptor requests answer for the device, as
before; `GET_NODE_CONNECTION_NAME` answers the empty name and
`DeviceIsHub` FALSE, a composite not being a hub. A parent with no driver
yet has no driver key, and the request fails as for any driverless device.

**The USB device interface** (task 36.17, 8.10): `GUID_DEVINTERFACE_USB_DEVICE`
moves to the **parent**, the device's own devnode, as `usbhub` registers it
on the composite device's PDO, and no function carries it
(`HcdDoorDeviceStart` refuses every function PDO). The owner's 2026-10-09
rule for the split device - the interface on the function that owns the
first interface, following that function's state - is superseded: the
interface follows the parent's state, so disabling one function no longer
takes it away, and disabling the parent does.

**Power.** Without selective suspend (record 14's switch at 0, the default,
and every build before stage A) nothing changes in S0: the parent PDO
records a D-IRP and completes it, as a device PDO does, and the composite
FDO requests nothing in S0, as the hub FDO does, so the parent's devnode
stays D0 while its functions' class drivers move their own PDOs; the
device's link is never suspended. (System power, which both FDOs do
handle, is the last paragraph of this part.) **With stage A (record 14), the
parent is the device's power object and the last-Dx rule is stated over
its children:** the device's link follows the parent's children - the
function PDOs - and leaves D0 only when the last of them has (record 14
section 4.3, unchanged in substance); the parent itself is **not a voter**:
it is excluded from stage A as a hub PDO is (record 14 section 5.1) - no
D1/D2 or wake in its capabilities, `WAIT_WAKE` refused, a D-IRP recorded
and completed at once, and an idle notification never reaching stage A at
all, since the parent refuses every internal IOCTL `STATUS_NOT_SUPPORTED`
(above; nothing above its FDO sends one) - so its own D0, which no
client ever moves, can never hold the device up. The composite readiness of
the idle batches (record 14 section 6.2, "as the composite parent does")
stays the bus's, over the same children.

**The parent stays D0 in S0 while stage A suspends its device (owner
decision, 2026-10-10, accepting a review's assessment of `2d0de39`).** The
composite FDO requests no D-IRP for the parent in S0; the parent's devnode
therefore reads D0 in Vista's and 7's power data while its device is
suspended, and `XHCISNAP`'s link state is the reading. The question put to
the review was whether the parent should mirror its device (Dx once every
function is Dx, D0 before a function's D0). Its findings: Microsoft's
`usbccgp` does request D2 for its own parent stack once its functions have
idled (static disassembly of XP 5.1.2600.5585, Vista 6.0.6000.16386 and 7
6.1.7601.17514 - `ParentIdleNotificationCallback`, the parent `WAIT_WAKE`,
and 7's `ParentSetD0` before a function's D0), but the general WDM
parent/child rule is an ordering constraint, not an equality one: children
stop depending on a parent before it powers down, and the parent is
available before they resume; all children at Dx permits a parent's idle
and does not compel it. Here the parent is a presentation object with no
data path: it is not a voter (`hcdSuspRead`, `hcdSuspSiblingD0`,
`hcdSuspIdleBatchLocked`, `hcdSuspPdoEligible` all pass it by), so its D0
cannot hold the link up; a function's D0 waits for an ACTIVE link and an
available controller (`XhciSuspAdmit`), which is the ordering that matters
physically; a function's `WAIT_WAKE` ends at the HCD's own function PDO and
never travels through the composite FDO; and wake is S0-only (record 14
sections 1, 5.2 to 5.4), so mirroring would add no S3/S4 wake either.
Mirroring would instead need one coordinated owner for parent D-IRPs
against function transitions, link failures, controller power epochs and
removal, `PoRequestPowerIrp` from contexts where its completion could
depend on the enum thread or `PowerGate`, and the 9x costs (the
same-state request that never delivers, PASSIVE_LEVEL only) - a second
asynchronous transition for no capability stage A needs. No P1 or P2 was
found in the parent staying D0; the two P3s are the next paragraph.
Revisit if xhci98 ever idles a hub or the controller (record 14 excludes
both): under Microsoft's stack a D0 composite stack would hold its hub up.

**System power: each parent FDO is its stack's policy owner (2026-10-10,
the two P3s of that assessment).** The hub FDO and the composite FDO
(`hcd_hubfdo.c`, shared) used to pass every power IRP down untouched,
recording nothing, and a parent PDO's `START` reported no initial D0
(`HcdSuspPdoReopen` reports only a stage A PDO) - against the
`PoSetPowerState` contract, which wants the initial D0 and every actual
transition reported per device object; and the parent PDO's capabilities
map every sleeping state to D3 (`hcdDeviceCapabilities`) while nothing in
its stack ever asked for D3. Now: a system `SET_POWER` goes down first; on
its way back up the FDO records the system state and requests, with
`PoRequestPowerIrp` on its PDO, the device state the capabilities give -
D3 for every sleeping state (hibernate and shutdown included), D0 for S0 -
and completes the system IRP only from that request's callback; a device
`SET_POWER` is recorded and reported (`PoSetPowerState`), a lower state
before it goes down and D0 once it has come back up; `START` records D0
and reports it, at the FDO and (inside `HcdSuspPdoReopen`'s report mutex,
the switch at 0 too) at a hub's or a composite's PDO. The decision is the
pure `XhciSuspParentWant` (`xhci_susp.c`; `test_susp`): no request after a
failed system IRP or before a start; on 98 and ME none for the state the
stack already has, while on NT that state is requested again, as WDM asks
of a policy owner (an S0 reasserted after a sleep that failed;
review round 1, P3 1). **Why the stack follows the capabilities and not
the reverse:** the existing D3 mapping is the conservative one - a
suspended USB device keeps no working state across a sleep, whether or not
its port keeps VBUS (it may in S1 and S3) - and it is what `usbhub`
reports for every hub and device on every target; changing the logical
parents' capabilities (to D0, which claims the device stays working, or to
unspecified, which marks the sleep state unsupported) would be a separate
design choice that fixes nothing (round 1, P3 2). Implementing the
mapping's policy owner is the WDM standard sequence, the one a
hub driver runs (`usbhub` imports `PoRequestPowerIrp` on 98 SE, Appendix A
row 53) and this driver's controller FDO already runs on every target
(`hcd_power.c`), at one request per system transition. **Order:** on NT
the power manager sends a sleep's system IRPs to children before their
parent and a wake's to the parent first; holding the system IRP until the
device IRP is done puts the parent's D3 after its children's system IRPs
and its D0 before theirs. On 98 and ME the configuration manager's order
is not read here and need not be: both parents' device states are logical
- the bus serves a hub, and a composite's functions, whatever their
parent PDO's D-state, and the physical order is kept at the controller
(`PowerGate`, `hcd_power.c`) and at the link (`XhciSuspAdmit`). **The 9x
rules** (win98-wdm.md, "Power Management"): no request for the present
state there (the pure decision, told 9x by `IoIsWdmVersionAvailable(1,
0x10)` as elsewhere in this driver), `PoSetPowerState`'s return never used, and the
system IRP completed at PASSIVE_LEVEL - from the callback when it runs
there, which it does under this driver's own PDO (every power IRP of a
parent completed inline), else from a work item. The system IRP and a D0
keep the FDO's I/O count until they complete, so a `REMOVE` waits for
them. A failed request completes the system IRP with the stack's state
unchanged (nothing physical waits on it). Not changed: the root-hub FDO
(`hcd_rh.c`, `HcdRootHubFdoPower`), which passes power down the same way
under the same D3 capabilities - left for its own task.

**What stage A's code needs** (branch `p36-susp`, not edited
here; the exact list is in the 36.43 report and the roadmap line): its
chain walks that read the slots or form a batch (`hcdSuspRead`,
`hcdSuspSiblingD0`, `hcdSuspIdleBatchLocked`) skip a `Composite` PDO; its
eligibility tests (`hcdSuspPdoEligible`, `hcdSuspPdoWasEligible`) refuse
one as they refuse a hub; its chain-length cap counts functions only (a
parent makes a chain one longer than `XHCI_FUNC_MAX`); and a function PDO
swapped out by the in-place re-presentation (item 3 above) has already had
its teardown, so it needs no settlement there, but the swap runs on the
thread under `PdoListLock` like the unlink, and a stage-A build calls the
same unlink settlement on it to keep "a PDO off a chain is settled" true.
Record 14's sections 4.3, 5.1 and 6.2 then say "function PDOs under the
composite parent" where they say "function PDOs of the group".

**Behind an external hub**: the parent is presented under the hub's PDO,
as a device PDO there would be, and the functions under the parent: a
two-level ancestor rule, which the sweep already repeats until nothing
moves. The hub's door answers the parent at the device's port.

**`XHCISNAP` and the counters.** Two counters, appended to the counter block
(`XHCIHC_COUNTERS`, after the event-delivery words; `XHCISNAP` prints them
by index until its name table catches up, as it does the event-delivery
words): `CompositeParents`, parent PDOs created, and
`CompositeRepresented`, function PDOs re-presented in place; and the same
two in the stop's counter lines, `composite.parents` and
`composite.represented`. The trace names each parent's port and function
count at creation, and each re-presentation's port and count.

**The INFs and the gates.** Each models section of both files carries one
more model, `%CompositeDesc%` (`xHCI98 USB Composite Device`) under
`XHCI98\COMPOSITE`, installed by `Comp.Dev` (98/ME: `DevLoader=*NTKERN`,
`NTMPDriver=xhci98.sys`), `Comp.Dev.NTx86` / `.NTamd64` and
`Comp.Dev6.*` (the service; a `DelReg` of `EnumPropPages32`, as XP's
own `usb.inf` writes for its composite parent). **A composite section copies
nothing**, for the hub's reason: the devnode exists only under a running
`xhci98.sys`, and a device plugged in long after the install must not send
the setup engine looking for the original media. And it registers **no
property page** (no hub Power tab: a composite is not a hub). The INF gate
holds all of it (`HCD-COMP`: the model in every models section, its id under
`XHCI98\`, no `USB\COMPOSITE` on any line; `HCD-HUBCOPY` widened to the
composite install; `HCD-COMPPAGE`: no `EnumPropPages`/`EnumPropPages32`
written by a composite section), with self-tests; the footprints are
re-emitted. **Text-mode Setup** (5.6): both `txtsetup.oem` files map
`XHCI98\COMPOSITE` to `xhci98` beside the hub ids - without it a composite
keyboard's functions would never appear under F6, the parent having no
driver there - and `OEM-IDS` already holds the file to the INF's model ids.

**Updating from `2.2.0.0`** (and from `2.1.0.0`). Each composite device
re-detects once: its parent is a new devnode everywhere, installed from
this package's INF (on XP with the wizard and the warning, above). Its
functions on NT: a function with a serial id keeps its devnode (its
instance path is unchanged and `UniqueID` TRUE, so PnP moves it under the
new parent); a function on the location form is a new devnode once, since
NT prefixes a `UniqueID` FALSE instance id with its parent's
`ParentIdPrefix` and its parent changed - re-installed silently from the
same class INF where that INF is signed, otherwise as its own driver
installs. On Windows 98 SE and ME the configuration manager uses the
instance id as given (unread for a re-parented devnode, as 10.11 left it);
the guest legs read whether the functions keep their devnodes there. The
old function devnodes stay behind as non-present devnodes on NT, as any
re-identified device's do. The release notes say: composite devices are
detected again once after the update, and work after it.

**What 36.43 leaves open**: the hot-plug applet with a parent answering
`SurpriseRemovalOK` FALSE (not taken); Windows 98's and ME's handling of the
functions' devnodes when they gain a parent, and what a disable of the
parent sends there; hidusbf's `Setup.exe` on a composite mouse, which looks
for the hub door through the device's parent (10.11, "Address") and now
meets the composite parent first - unread, and under Microsoft's stack it
meets `usbccgp`'s devnode the same way; and every reading on a real
multi-function device (QEMU has no keyboard-and-mouse composite; its
`usb-audio` is a parent with one function), which needs a passthrough
device or the bench. The guest legs of 36.43 read what QEMU can show.

### 10.13 The bus's own ids under `USB\` (task 38.9, 2026-10-13)

**Status: written before any code; the owner's decisions taken
(2026-10-13, 10.13.12; D14 open), the task kept in `2.4.0.0` with the update costs
of 10.13.4 and 10.13.5 accepted; under review.** The task and what is already decided are roadmap-hcd 38.9's (owner,
2026-10-13): the four project-owned bases move from a private enumerator to
`USB\` - `XHCI98\ROOT_HUB`, `XHCI98\HUB`, `XHCI98\HUB30` and
`XHCI98\COMPOSITE` become `USB\XHCI98_ROOT_HUB`, `USB\XHCI98_HUB`,
`USB\XHCI98_HUB30` and `USB\XHCI98_COMPOSITE`, with their `&VID_`, `&PID_`
and `&REV_` forms (GitHub issue 17 item 10, SweetLow, 2026-10-12: there is
no `XHCI98` bus); the whole task is in this phase; and the old `XHCI98\`
devnodes are removed by an opt-in tool under the release's `tools\`, its
source tracked, which removes only non-present devnodes of this project
through supported PnP routes, an install needing no run of it. The review
of the decisions (roadmap decisions table, Phase 38 row) added the
conditions this section answers: the parents' new devnodes and what they do
to the devices beneath, measured in the serial-number and the location
case (10.13.5); the private hardware ids and the empty compatible-id lists
kept (10.13.1); stock and vendor INF matches audited rather than assumed
impossible (10.13.2); both F6 files and their gates changed (10.13.7). The
choices the roadmap left open were put to the owner as D1 to D13 and are
10.13.12's decisions taken. It is a new section rather than more of 10.12 because the
move covers the root hub and the hubs as well as the composite parent;
10.11 and 10.12 keep their designs, and only their ids change.

**What it supersedes, once its code lands**: 8.6's "a private enumerator
rather than a `USB\` id keeps the instance under `Enum\XHCI98\` on every
target ... and cannot collide with any OS row by construction" - after
38.9 the root hub's devnode is under `Enum\USB`, and the guarantee rests on
the audit of 10.13.2 and the gate rule of 10.13.7, not on construction; the
id rows of 5.2's object table, 5.6's `txtsetup.oem` paragraphs, 10.7's last
paragraph, 10.11's and 10.12's id tables and INF paragraphs, and their
statement that Windows 98's configuration manager uses the instance id as
given, which 10.7.1's reading corrects for the location form (10.13.5);
and the decisions table's "Hub class and composite splitting" row. Each is amended
in the change that lands the code, as 36.43 amended its predecessors.

#### 10.13.1 The ids, old and new

| PDO | Query | Today | After 38.9 |
|---|---|---|---|
| Root hub (`hcd_rh.c`, `hcdRootHubId`, `hcdRootHubQueryId`) | `BusQueryDeviceID` | `XHCI98\ROOT_HUB` | `USB\XHCI98_ROOT_HUB` |
| | `BusQueryHardwareIDs` | `XHCI98\ROOT_HUB` | `USB\XHCI98_ROOT_HUB` |
| | `BusQueryCompatibleIDs` | none (the IRP's status passed through) | none, unchanged |
| | `BusQueryInstanceID` | `0`, `UniqueID` FALSE | unchanged |
| Hub, USB 2.0 / 1.1 or a USB 3 hub's USB 2.0 half (`xhci_hub.c`, `xhciHubPdoBase`, `XhciHubPdoId`) | `BusQueryDeviceID` | `XHCI98\HUB&VID_vvvv&PID_pppp` | `USB\XHCI98_HUB&VID_vvvv&PID_pppp` |
| | `BusQueryHardwareIDs` | `XHCI98\HUB&VID_vvvv&PID_pppp&REV_rrrr`, `XHCI98\HUB&VID_vvvv&PID_pppp`, `XHCI98\HUB` | `USB\XHCI98_HUB&VID_vvvv&PID_pppp&REV_rrrr`, `USB\XHCI98_HUB&VID_vvvv&PID_pppp`, `USB\XHCI98_HUB` |
| A USB 3 hub's SuperSpeed half | both | the same with `HUB30` | the same with `XHCI98_HUB30` |
| Composite parent (`xhci_func.c`, `XhciFuncParentId`) | `BusQueryDeviceID` | `XHCI98\COMPOSITE&VID_vvvv&PID_pppp` | `USB\XHCI98_COMPOSITE&VID_vvvv&PID_pppp` |
| | `BusQueryHardwareIDs` | `XHCI98\COMPOSITE&VID_vvvv&PID_pppp&REV_rrrr`, `...&PID_pppp`, `XHCI98\COMPOSITE` | `USB\XHCI98_COMPOSITE&VID_vvvv&PID_pppp&REV_rrrr`, `...&PID_pppp`, `USB\XHCI98_COMPOSITE` |
| Hub and composite parent | `BusQueryCompatibleIDs` | none (`BAD_PARAM`, the IRP's status passed through) | none, unchanged |
| | `BusQueryInstanceID` | `hcdInstanceQueryId`: the serial id, `UniqueID` TRUE, else the location key in decimal, `UniqueID` FALSE (10.7) | unchanged |

**Unchanged**: every device and function PDO's ids (10.7), the refused
interface's `USB\XHCI98_NOXPORT&...` hardware id and its device id (10.7,
task 31-A.3; task NOX.1, deferred, 10.7.1 and 10.13.9), every instance id and its rule, every
capability, the device texts and the INFs' model texts, the service name
`xhci98`, the door's names (`\Device\XHCI98DEV<serial>`,
`\DosDevices\XHCI98HUB<serial>`, 10.11), and the `txtsetup.oem` component
id `XHCI98` (`[Defaults] scsi = XHCI98`, `$optionId` in
`check-txtsetup-oem.ps1`), which names the F6 option, not a device. The
underscore after `XHCI98` is the roadmap's spelling and keeps each id one
`USB\` component with no second backslash, as a device id must have.

**Kept, by the review's condition**: the bare forms stay the last
hardware id (`USB\XHCI98_HUB`, `USB\XHCI98_HUB30`, `USB\XHCI98_COMPOSITE`,
and the root hub's only one), because the INFs bind those, as a
hardware-id match on every engine (10.11); and no PDO of these four kinds
answers a compatible id, so no class row (`USB\Class_09`, `USB\HUBCLASS`,
`USB\COMPOSITE`) can be reached through a ranking fallback (8.6).

**Lengths.** The prefix grows from 7 characters (`XHCI98\`) to 11
(`USB\XHCI98_`). The longest answer is the composite parent's hardware
multi-string: 48 + 39 + 21 + 1 = 109 characters with its NULs, against the
128-character buffers `hcdHubQueryId` and `hcdCompositeQueryId` hand the
pure helpers (`hcd_pdo.c`, `char text[128]`); the hub's SuperSpeed half is
97; the host vectors assert both numbers (10.13.7). The longest
**serial-form** instance path, `USB\XHCI98_COMPOSITE&VID_vvvv&PID_pppp\`
and a 126-character serial id, is 165, under `MAX_DEVICE_ID_LEN` (200),
as 10.7's longest function path (157) is. A **location-form** key on 98
SE carries its parent's path (10.13.5), and the move lengthens each
presented ancestor in it by four characters (`XHCI98&ROOT_HUB`, 15, to
`USB&XHCI98_ROOT_HUB`, 19; each hub's and the composite parent's
likewise): at most seven such ancestors (the root hub, five hub tiers, a
composite parent), so at most 28 characters more than today's key for the
same device. That bounds the change, not the key: 38.2's reading shows
one tier (the root hub's key itself carrying the controller's PCI path,
`0PCI&...`), and whether each tier's key carries its parent's whole key
- so that a chain of serial-less hubs grows by a hub's device id and key
per tier - is unread, as is what 98 SE allows for the key (a registry key
name, an instance id buffer) and what it does at the limit. That limit,
if it exists, binds today's ids too. The design therefore reads it
rather than assumes it: on 98 SE, a chain of serial-less hubs as deep as
QEMU allows (up to five tiers) with a serial-less device at its end, on
`2.3.1.0` and again after the update, each key read whole (10.13.10).
If a depth works under the old ids and fails under the new, the release
notes name the deepest serial-less chain that works on 98 SE; the bus
cannot shorten a key 98 SE builds from its parents' paths. NT's
location-form instance ids carry only the parent's `ParentIdPrefix`, not
its path, so they do not grow.

**One source for the four bases.** Today the strings are
spelled in three files (`hcd_rh.c`, `xhci_hub.c`, `xhci_func.c`) and the
INF gate's `$prod` table, with nothing holding them equal; the root hub's
is in DDK-bound code no host test reaches. The proposal: the four bases as
`#define`s in one pure header (`xhci_hub.h`, beside `XhciHubPdoId`'s),
used by all three files, a host vector over each, and the INF gate reading
that header and refusing an INF whose models name any other spelling
(`HCD-IDSRC`, 10.13.7), so the C and the INFs cannot drift apart.

#### 10.13.2 The audit: no stock or vendor INF row matches

What a match needs (static, 8.6, unchanged): every target's setup engine
compares whole id strings, case-insensitively, with no prefix or wildcard
form. So a row can match these devnodes only by naming one of the new ids
literally - which, for a row not written for this project, means naming
`USB\XHCI98_` - or by naming a compatible id the devnodes answer, and they
answer none.

**The sweep** (static, 2026-10-13: a text read of every `*.inf` under the
git-ignored `tools\`, the UTF-16LE files decoded from their byte-order
mark, the code part of each line (before its first `;`) searched for
the regular expression `"?USB\\[^,\s"]*"?`, case-insensitive, each match
taken whole as one token; a PowerShell script kept outside the tree,
nothing executed on a target). 214 INFs: the
187 under `tools\*-extracted\` (98 SE 10, ME 1, 2000 SP4 11, XP SP3 15, XP
x64 15, Vista x86 18 and x64 5, 7 x86 18 and x64 5, NUSB 3.3 8 and 3.6 9,
SweetLow 3, hidusbf 3, and the Windows 2000 DDK's installer 66), the two
ASIX AX88772 vendor packages, hidusbf's 2026-10-03 package (3), and the
samples of the Windows 2000 DDK (12), WDK 7.1 (6) and MSVC 6.0 (4). 11,361
`USB\` tokens. Results:

- **No line of any of them contains `XHCI98`**, in any case, as an id or
  otherwise (the 33.4 sweep's finding over the extracted INFs, repeated
  over the wider set).
- **No `USB\` token carries a `*` or `?`, and none is `USB\` alone.**
- **The only tokens beginning `USB\X`** are Vista's and 7's `wdma_usb.inf`
  `USB\XUSB_AUDIO_DEVICE` (with its string key `USB\XUSB.DeviceDesc`):
  Microsoft's own example of a private, non-VID name under `USB\`, as are
  98 SE's `usbcdc.inf` `USB\CPQ0011`, `USB\LUCENTUSB` and `USB\USR0021` and
  the DDK samples' `USB\BULKUSB` and `USB\ISOUSB`.
- **The non-VID `USB\` ids matched anywhere** are 8.6's list
  (`USB\ROOT_HUB`, `USB\ROOT_HUB20`, `USB\ROOT_HUB_DBC`, `USB\HUBCLASS`,
  the two `USB\CLASS_09` forms, `USB\COMPOSITE`, `USB\UNKNOWN`), SweetLow's
  `USB\COMPOSITE2`, the class triples of the class INFs (`USB\CLASS_01`,
  `03`, `06`, `07`, `08`, `0B`, `0E`, `E0`, `EF` forms), the
  `USB\MS_COMP_...` ids, the private names above, and `USB\SILENTCLASSES`
  (a key name, not a model). None is a prefix-equal or case-variant of a
  new id, and matching takes no prefixes anyway.
- **No class-wide filter** that would now meet these devnodes: no
  `UpperFilters` or `LowerFilters` line in any target's `usb.inf`,
  `usbport.inf` or `USB2.INF`, except 98 SE's and ME's on
  `[DbcHub.Dev.HW]` (`dbfilter.sys`), which is the device bay hub's own
  device key, bound by `USB\ROOT_HUB_DBC`. The devnodes' class
  (`Class=USB`, `{36FC9E60-C465-11CF-8056-444553540000}`) is the one they
  already had under `XHCI98\`, so a class filter a third party installs
  reaches them today as it will tomorrow; what changes is only that one
  keyed on the enumerator name `USB` now meets them too (unread: no such
  filter is in the swept set).
- **Text-mode Setup** (5.6, static): `[HardwareIdsDatabase]` of 2000 SP4,
  XP SP3 and XP x64 `TXTSETUP.SIF` binds `USB\ROOT_HUB` (and
  `USB\ROOT_HUB20` on XP), `USB\CLASS_09`, `USB\COMPOSITE`, the HID and
  storage class ids and a storage VID/PID list - none a new id.

**Binaries that carry id strings** (static, 2026-10-13, an ASCII and
UTF-16LE string search, no disassembly): 98 SE's `ntkern.vxd` carries no
`USB\` string at all (a string search finding nothing is weak evidence: an
id can be built from parts); 98 SE's `usbd.sys` carries `USB\ROOT_HUB`
and `USB\OTHER_ID`, the Windows 98 USB 1.1 stack's own root hub, which
`usbd.sys` presents there; NUSB 3.6's and SweetLow's `usbport.sys`
`USB\ROOT_HUB&VIDnnnn` and `USB\ROOT_HUB20&VIDnnnn`, and their
`usbhub20.sys` adds `USB\HUB_VID_nnnn` and `USB\HUBCLASS` to 10.6's
templates. Each is an id a Microsoft or SweetLow bus driver **emits** for
its own devnodes; none is `USB\XHCI98_`. Whether any of these binaries
parses another bus's ids is **not established by this search**, which
reads strings, not code.
No string of interest was found in hidusbf's binaries.

**What was not swept**, and so what the audit does not claim: a target's
whole `%windir%\inf` beyond the extracted sets (the 33.4 and 8.6 rows of
`legal-provenance.md` record the same limit), Windows Update's catalogue,
and any vendor INF on a user's machine. The argument for those is the
matching rule above - a row would have to name `USB\XHCI98_...` - and the
legs' Device Manager reading (each new devnode bound to `xhci98.sys`, no
other driver offered). **The namespace is the project's in both
directions**: `USB\XHCI98_NOXPORT` exists already (31-A.3) and must stay
unbound, so the INF gate names the four bound bases exactly and refuses
any other `USB\XHCI98_` id on any line (10.13.7), rather than admitting a
`USB\XHCI98_` prefix.

The row for this sweep joins `legal-provenance.md` section 4 beside the
33.4 one.

#### 10.13.3 Where the devnodes go, per target

**The NT targets** (2000, XP, XP x64, Vista, 7). The device id's first
component names the enumerator, so the devnodes' keys move from
`Enum\XHCI98\` to `Enum\USB\`: the root hub to
`Enum\USB\XHCI98_ROOT_HUB\<instance>`, whose instance path is the
controller's `ParentIdPrefix` and `&0` (`UniqueID` FALSE, as today); a hub
or composite parent with a serial id to
`Enum\USB\XHCI98_HUB&VID_vvvv&PID_pppp\<serial id>`; one on the location
form to its parent's `ParentIdPrefix` and the key. The `USB` enumerator key
is shared with every other USB stack's devnodes on the machine (an EHCI or
UHCI controller's under the OS's own `usbhub`); the device ids differ, so
nothing collides. Nothing in Windows' handling of that key is known to
depend on the device id's form beyond the INF match (unread for each
target's own components; the legs read Device Manager, `usbview` and the
Power tab on each).

**Windows 98 SE and ME.** The configuration manager keeps the devnode's
registry entry at `HKLM\Enum\USB\XHCI98_...\<key>`. On 98 SE the key is
the serial id as answered for a serial-form devnode, and for a
location-form one the answer followed by the parent's path with `\`
turned into `&` (runtime, 2026-10-13, 10.7.1); ME's form is unread
(10.13.5). The `USB` key holds the
devnodes of whichever other USB stack runs: the stock UHCI/OHCI one
(`usbd.sys`, `usbhub.sys`), and with NUSB or SweetLow's stack installed
their `usbhub20.sys`'s for an EHCI controller. **With and without
SweetLow's stack** the reading is the same, statically: no INF of any of
those stacks matches a new id (10.13.2); no line of 98 SE's or ME's
`usb.inf`, NUSB 3.3's `USB2.INF`, NUSB 3.6's `USB.INF` and `USB2.INF` or
either SweetLow `USB2.INF` edition writes or deletes anything under `Enum`
(a text search for `Enum` in their `AddReg` and `DelReg` lines); and the
`[ClassInstall]` of 98 SE's and ME's `usb.inf` writes only the class key
(its name and icon; ME also `SilentInstall` 1, which is why ME installs
the class silently, 6.11.9) and registers no class installer DLL. SweetLow's
`usbhub20.sys` reads `USB\SilentClasses` (a key under `Services`, 6.11),
not `Enum`. What is unread is whether 98's configuration manager, NTKERN or
the 16-bit setup engine treats `Enum\USB` entries other than by their ids
(neither `CONFIGMG` nor `setupx.dll` is in `tools\`); the 98 SE legs read
the update on a stock install and under SweetLow's stack, and ME's on its
stock stack (10.13.10).

**Text-mode Setup** (5.6). Text mode builds the root hub's and every
hub's and composite's devnode from `[HardwareIds.scsi.XHCI98]`, so both
`txtsetup.oem` files name the four new ids in place of the old, and
GUI-mode Setup installs the same devnodes from the INF the `inf` line
carries. An F6 install is always a fresh one; no `XHCI98\` devnode exists
there to be left behind.

#### 10.13.4 The update over an installed `XHCI98\` tree, per install path

**What happens on every path.** The new binary reports only the new ids.
At its first enumeration the root hub, every hub half and every composite
parent present is a **new devnode**, installed from the new INF by its
model line; its old devnode is never reported again and becomes
non-present (10.13.6). Nothing in the install itself removes or rewrites
the old one (owner, 2026-10-13: an install needs no run of the tool). The
release notes' advice to update the root hub separately so its Driver tab
shows the new version (release notes, "Updating from 2.y.y.y") does not
apply to this update: the root hub is new and installs from the new INF.

| Path | When the new ids appear | How the new devnodes install | Read |
|---|---|---|---|
| Windows 98 SE and ME (`Xhci.Dev`, `RootHub.Dev`, `Hub.Dev`, `Comp.Dev`) | at the next start: the new `xhci98.sys` cannot replace the running one, which keeps running until the restart (release notes, "Updating from 2.y.y.y") | expected silently from the new INF, as each new hub and composite devnode installs today (6.11's opening, run-36 98 SE K2 and G1, run-37 for ME); the root hub's section copies `xhci98.sys` (8.6), so at that start the engine would copy over the file the running driver is: the root-hub sections copy nothing from this release (D3) | the legs |
| Windows 2000 (`*.NTx86`) | read at once for `2.1.0.0` over `2.0.0.0` (release notes: "It runs at once, with no restart"); for this release's package, with 38.4's helper extended to 2000 and XP, to be read | no wizard, as for the hubs and composite parents (10.12, "What it buys") | the legs |
| 32-bit XP (`*.NTx86`) and XP x64 (`*.NTamd64`) | read at once for `2.1.0.0`; for this release's package, to be read, as for 2000 | **Found New Hardware and the unsigned-driver warning, once per new devnode**: the root hub (the `1.2.0.0` upgrade read "a second wizard follows for the root hub", release notes, "Upgrading from 1.2.0.0"), each hub half and each composite parent (10.12; run-36 XP K2 and G1); documented, with its input trap (D4) | the legs |
| 32-bit Vista and 7 (`*.Dev6.NTx86`) | at the restart the update asks for, for the MSI helper (task 38.6; renamed `xhcihlpr.sys` by 38.4); until it, the earlier release runs | from the package Have Disk put in the driver store; no wizard expected (unread for a new devnode of an already-staged package) | the legs |
| Vista x64 and 7 x64 (`*.Dev6.NTamd64`) | at once, in test-signing mode or an F8 boot (AGENTS.md) | as 32-bit Vista and 7 | the legs |

**The timing in the table is the earlier releases'.** Task 38.4 extends the
helper to 2000 and XP and changes what an update installs there, and on a
machine with two controllers the image stays loaded while either still
runs it; so when the new ids appear on 2000 and XP is read on this
release's package, on one and on two controllers, with the running
driver's version taken from `XHCISNAP`'s report, not from the Driver tab
(10.13.10). Nothing in this design depends on it being at once.

**Between the update and the restart on 98 SE and ME** (and on 32-bit
Vista and 7) the old binary still runs and still reports `XHCI98\` ids. A
hub or composite device plugged in for the first time in that window
makes an `XHCI98\` devnode, which on NT matches the old package's INF
(still in `%windir%\inf` or the driver store) and installs as before; on
98 SE and ME it matches only if the old INF is still in `INF\OTHER`, which
Update Driver may have replaced with the new one (unread). Without a match
it is an unknown device until the restart, after which it is a stale
`XHCI98\` devnode like the rest. The new INFs do not keep the old model
lines to cover that window (D2, a clean move); the release notes say, for
98 SE and ME, "restart before connecting any new hub or composite
device".

**Going back to `2.3.x`** after an update: the old binary reports the old
ids, and each old devnode comes back present with its driver - unless the
tool has removed it, in which case it installs afresh from the old
package's INF (on XP with the wizard). The release notes say so beside the
tool.

#### 10.13.5 The devices beneath: the serial-number and the location case

The parents' new devnodes are the condition the review raised: a child's
devnode identity on NT can depend on its parent's.

**On the NT targets** (static, the rule 10.11 and 10.12 already state; read
at runtime for the composite case on the 36.43 update legs):

- **A device or function with a serial id** answers `UniqueID` TRUE, so
  its instance path does not involve the parent: `USB\VID_vvvv&PID_pppp\<serial>`
  is the same path under the new parent, and PnP moves the devnode there.
  It keeps its devnode, its driver and every setting kept on it.
- **A device or function on the location form** answers `UniqueID` FALSE,
  and NT prefixes its instance id with its parent's `ParentIdPrefix`. A
  new parent devnode has a new `ParentIdPrefix`, so the child is a **new
  devnode once**. Because the root hub itself moves, this is **every
  serial-less device on the controller**, at a root port or behind a hub,
  not only those behind a hub or in a composite - most mice and
  keyboards. Each is installed again from its class INF: with nothing to
  answer where that INF is a signed inbox one (as at the `2.0.0.0` to
  `2.1.0.0` update, release notes, "Updating from 2.y.y.y"), and as its own
  driver installs otherwise (an unsigned vendor driver asks again on XP).
  **What is lost** is what was kept on the old devnode: a setting a tool
  or the user wrote there (hidusbf's polling rate, the release notes'
  example), a friendly name, and for a USB serial adapter the COM number,
  whose reservation the old non-present devnode may keep, giving the new
  devnode the next free number (unread here; the Ports class's handling of
  a non-present port is Windows'). The old devnode stays non-present.
  The project documents it and does nothing more (D5).
- **Composite functions** follow their own rule: a serial id with `&nn`
  keeps the devnode; the location form with `nn` gets a new one.

**On Windows 98 SE** the instance key is not the answer alone. Task 38.2's
legs read it (runtime, 2026-10-13, under NUSB 3.3; 10.7.1): a serial id is
kept as answered, but for a location-form instance id 98 SE appends the
parent's path with `\` turned into `&` (`1XHCI98&ROOT_HUB&0PCI&...`).
So on 98 SE:

- **a device or function with a serial id** is expected to keep its
  devnode and settings, its key containing nothing of the parent's;
- **a device or function on the location form** carries its parent's old
  path in its key - `XHCI98&ROOT_HUB&0` at a root port, the hub's or the
  composite parent's path behind one - which becomes
  `USB&XHCI98_ROOT_HUB&0` and the rest: a **new devnode once**, as on NT,
  and every serial-less device on the controller with it. 98 SE then runs
  the Add New Hardware wizard for each, and asked for its CD for
  `hidclass.sys` at the `2.0.0.0` to `2.1.0.0` update (release notes,
  "Updating from 2.y.y.y"), unless task 38.2's silent install is switched
  on and lists the class (6.11) - and even with it on, 38.2's legs read the
  mouse and the keyboard asking for the CD for their files (6.11.11). The serial-less keyboard and
  mouse are among these devices, so, as on XP (10.13.4), a machine whose
  only input is USB on this controller may be left with a prompt nothing
  can answer; the release notes ask for another input device independent
  of the updated driver (D4).

Windows ME's key form is unread (38.2's legs read it on 98 SE only) and is
expected to be 98 SE's; the legs read both forms on both systems, which
closes what 10.11 and 10.12 left unread there.

**Measuring both cases.** QEMU gives every emulated USB device a serial
string built from its bus path when none is set (the `1-0000:00:03.0-4`
instance ids of run-36's K2 rows), so the serial case is the guests'
default. The location case needs a device whose serial the bus refuses: a
QEMU device given a `serial` property containing a space (10.7's refusal
of `0x20`), which the bus's trace (`serial` lines) and the instance id
confirm took the location form. Each update leg (10.13.10) carries one of
each, at a root port and behind the hub, and a composite device.

#### 10.13.6 The old devnodes

**On NT** each old devnode stays as a **non-present devnode**: its key
under `Enum\XHCI98\`, its driver key under the USB class key, its
`Service` value naming `xhci98`. Nothing loads for it and nothing reports
it again unless an older binary runs (10.13.4). Device Manager hides it by
default: on every NT target here - 2000, XP, XP x64, Vista and 7 - it
shows only when `DEVMGR_SHOW_NONPRESENT_DEVICES=1` is set in the
environment Device Manager is started from and "Show hidden devices" is
chosen (Microsoft's "Viewing Hidden Devices" names the variable for
systems before Windows 8; not read on a guest here); then it shows greyed
beside the new one. The legs use that procedure before and after the
tool, on every NT target (10.13.10). The old location-form children
(10.13.5) are non-present beside it. It costs nothing at run time; what it
costs is a second "xHCI98 USB 3.x Root Hub" in the hidden view, and on
Vista and 7 possibly a reference that keeps `pnputil -d` from deleting the
old package (unread; the release notes' "Deleting the driver keeps the
older packages").

**On 98 SE and ME** an old devnode is a registry entry only: its key under
`HKLM\Enum\XHCI98\` and its driver key under the USB class key. The
configuration manager builds a devnode only for what an enumerator reports
(no `HKEY_DYN_DATA\Config Manager\Enum` entry names it), so it is inert.
Whether normal-mode Device Manager lists such an entry, and whether Safe
Mode's does, is unread here; the legs look in both modes.

**Removal** is the tool's (10.13.8), and the tool is optional: the release
notes and the readme say when to run it - after an update from `2.3.x` or
earlier and its restart - and that nothing needs it. On 98 SE and ME it
lists only in this release (10.13.8).

#### 10.13.7 Every reader of the ids

Built from a whole-tree search for `XHCI98\` (fixed string, every tracked
file, case-insensitive too) and for `ROOT_HUB`, `HUB30`, `COMPOSITE`,
`Enum\` and `NOXPORT` in `src\`, `scripts\`, `test\`, `xhcisnap\` and
`xhciqual\`, on `0976a08a`.

| Reader | What it holds | Change |
|---|---|---|
| `src\hcd_rh.c` `hcdRootHubId`, file header | the root hub's id | the new id, from the shared header |
| `src\xhci_hub.c` `xhciHubPdoBase`; `src\xhci_hub.h` (`XhciHubPdoId`'s comment) | `XHCI98\HUB`, `XHCI98\HUB30` | the new bases; the header gains the four `#define`s (10.13.1) |
| `src\xhci_func.c` `XhciFuncParentId` (two literals); `src\xhci_func.h` | `XHCI98\COMPOSITE` | the new base |
| `src\hcd.h`, `src\hcd_pdo.c`, `src\hcd_hubfdo.c` | comments naming the ids | wording |
| `src\xhci98.inf` (eight model lines: four in `[XhciModels]`, which serves 98 SE, ME, 2000 and XP, and four in `[XhciModels.NTx86.6.0]`), `src\xhci98-amd64.inf` (the same eight in its two models sections) | the model ids | the new ids; the root-hub install sections copy nothing (D3); no old model line kept (D2) |
| `src\txtsetup.oem`, `src\txtsetup-amd64.oem` | `[HardwareIds.scsi.XHCI98]` rows and the header comment | the four new ids; the component id `XHCI98` unchanged |
| `scripts\inf-gate\check-inf.ps1` | `$prod` (`RootHubId`, `HubIds`, `CompositeId`); `HCD-HUB` and `HCD-COMP` require a hub's or composite's ids "under `XHCI98\`" | the new ids; "under `XHCI98\`" becomes "exactly one of the four bases or their `&VID_&PID_[&REV_]` forms"; a new rule, **`HCD-USBRES`**: no line of any section names a `USB\XHCI98_` id other than the four bound bases (so `USB\XHCI98_NOXPORT` stays unbound, 10.13.2 and NOX.1's decision 5) and no line names an `XHCI98\` id (the clean move, D2); a new rule, **`HCD-IDSRC`**: the models' ids equal the shared header's `#define`s; `HCD-ROOTHUB`'s `^USB\\ROOT_HUB` test is unaffected, since the new id does not begin with it |
| `scripts\inf-gate\test-inf-checks.ps1` | 16 mutations built on the old ids | rewritten on the new; new mutations for `HCD-USBRES` (a `USB\XHCI98_NOXPORT` model, a `USB\XHCI98_OTHER` model) and `HCD-IDSRC`; an old `XHCI98\` id on any line refused (D2) |
| `scripts\inf-gate\check-txtsetup-oem.ps1` | `OEM-USBID` refuses **any** `USB\` id ("those belong to Setup's own drivers"); `OEM-IDS` ties the file to the INF's model ids; the self-tests at its foot | `OEM-USBID` refuses every `USB\` id except the four new bases, and names `USB\ROOT_HUB` and the class ids in its message; the self-tests rebuilt on the new ids, plus one that a `USB\XHCI98_NOXPORT` row and one that an old `XHCI98\` row is refused |
| `scripts\package\test-package.ps1` (one case) | stages a `txtsetup.oem` whose root-hub row reads `USB\ROOT_HUB`, expects a refusal | still valid under the new `OEM-USBID`; its search string becomes the new id |
| `scripts\inf-gate\expected-footprint*.txt` | no id (searched) | re-emitted, since the root hub's sections change (D3) |
| `scripts\import-gate\win98-evidence.list`, the allowlists and baselines | no id (searched) | none; the tool is user mode and outside the import gate, as `XHCISNAP` is |
| `scripts\vm-matrix\` (scripts, `lib\`, `guest\*.BAT`), `scripts\setup-qemu-*.ps1` | no reader of the ids, `Enum`, or `ROOT_HUB` (searched) | none; the 38.9 legs are driven by hand, as 38.2's were |
| `xhcisnap\xhcisnap.c` (`write_dev_devices`, `dev_consider`) | walks the devnode tree (`cfgmgr32.dll`, or `HKEY_DYN_DATA` on 98 and ME) and reports every devnode driven by `xhci98` or `xhciuas`, chosen by its service or `NTMPDriver`, with its instance id; no literal id | no change to the code: its selection is by driver, not by id; the legs compare a report before and after the update (10.13.10) |
| `xhciqual\` | no reader | none |
| `src\uas\*.inf`, `scripts\inf-gate\check-uas-inf.ps1` | no reader | none |
| `test\test_hub.c` (8 lines: `XhciHubPdoId`'s vectors) | the hub ids | the new ids; the 97-character length asserted |
| `test\test_func.c` (9 lines: `XhciFuncParentId`'s vectors) | the composite ids | the new ids; the 109-character length asserted |
| new host vectors | - | the four `#define`s spelled out, so a typo in the shared header fails a test |
| `AGENTS.md` (Architecture Overview) | `XHCI98\HUB`, `XHCI98\HUB30` | the new ids |
| design record 13 (5.2, 5.6, 8.6, 10.7, 10.11, 10.12) | 37 occurrences | amended as the opening of this section lists |
| `roadmap-hcd.md` decisions table | the hub and composite row | amended with the code |
| `docs/contributing/source-files.md` (3 lines) | the ids beside `hcd_hubfdo.c`, `hcd_pdo.c`, the `.oem` files | the new ids; the tool's source directory added |
| `docs/using/release-notes.md` | update steps; the root hub's Driver tab paragraph | a "from `2.3.x`" paragraph: each root hub, hub and composite device found again once, the devices beneath (10.13.5), the prompts the update brings on XP, XP x64 and 98 SE and the input they need - a keyboard and mouse independent of the updated driver: PS/2, or a USB controller driven by another stack, never a second controller running `xhci98.sys`, which takes the same new image and so the same prompts - before starting (D4), restart before connecting any new hub or composite device on 98 SE and ME (10.13.4), the tool and when to run it |
| the readme template in `scripts\package\make-release.ps1` | the same update steps | the same, and `tools\` gains the tool's directory |
| `legal-provenance.md` | rows 880 and 885 record the 33.4 and 8.6 sweeps | historical, unchanged; a row for 10.13.2's sweep; section 2a for the tool's executable (10.13.8) |
| `docs/README.md`, `README.md` | the tools listed | the tool and its README |
| run records, `docs/issues/10-xp-f6-gui-mode-usb-input.md`, `releases\` | what was read under the old ids | unchanged: a record keeps what it read |

#### 10.13.8 The cleanup tool

**What it is** (D6). A console program, `XHCICLN.EXE`, its source in
`xhcicln\` at the repository root beside `xhcisnap\` and `xhciqual\`,
shipped in the release as `tools\xhcicln\` (an 8.3 name, as
`tools\xhcisnap\` and `tools\xhciqual\` are) with a `README.TXT`, never in
a flavour directory. It is opt-in: nothing runs it, and an install never
needs it.

**Language, toolchain and the two builds** (D7). `XHCISNAP`'s: ANSI C89,
one source file, `/W3 /WX`, built in place by a `build.cmd` with the MSVC
6.0 in `tools\MSVC600`, so the 32-bit `XHCICLN.EXE` starts on 98 SE, ME,
2000, XP and 32-bit Vista and 7. Every configuration-manager and SetupAPI
routine is resolved at run time (`LoadLibrary` and `GetProcAddress`, as
`XHCISNAP` loads `cfgmgr32.dll`), so its import table names `KERNEL32` and
`ADVAPI32` alone and it starts where a routine is missing, saying which;
a routine missing that a removal needs makes that removal refused, never
substituted. **The 64-bit targets get a native build by design**: the same
source built a second time with WDK 7.1's amd64 compiler
(`tools\WinDDK71`, as `xhci98.sys` amd64 is), shipped beside it as
`XHCICL64.EXE`. Microsoft documents that a 32-bit process on 64-bit
Windows cannot run device installation actions -
`SetupDiCallClassInstaller` fails there with `ERROR_IN_WOW64` (Microsoft's
SetupAPI documentation; not read on a guest here) - so the design does not
rest on what one guest happens to allow: the 32-bit build, finding itself
under WOW64 (`IsWow64Process`, resolved as `XHCISNAP` resolves it), still
lists, but refuses every removal and names `XHCICL64.EXE`. The 64-bit
build is tested on XP x64, Vista x64 and 7 x64 (10.13.10). On 98 SE and
ME the tool lists only in this release (below).

**What it removes - the selection rule.** A devnode is a candidate only
when both hold:

1. its device instance id begins `XHCI98\` (case-insensitive) and its
   device id is one of the four old forms exactly - `XHCI98\ROOT_HUB`,
   `XHCI98\HUB&VID_vvvv&PID_pppp`, `XHCI98\HUB30&VID_vvvv&PID_pppp`,
   `XHCI98\COMPOSITE&VID_vvvv&PID_pppp`, four hex digits each; any other
   `XHCI98\` entry is listed as "not this project's form" and left;
2. it is **conclusively absent** (below).

**Present, absent, unknown.** Every presence test has three answers, and
only "absent" lets a removal go ahead:

- **On NT**: `CM_Locate_DevNode` with `CM_LOCATE_DEVNODE_NORMAL` on the
  instance id returning `CR_NO_SUCH_DEVNODE` (`0x0D`, the Windows 2000 DDK's
  `cfgmgr32.h`, static) is absent; `CR_SUCCESS` is present; any other
  return - access denied, invalid data, a routine missing - is unknown. The
  answer must agree with a `SetupDiGetClassDevs` set taken with no class
  GUID, the enumerator `XHCI98` and `DIGCF_ALLCLASSES | DIGCF_PRESENT`,
  itself fully enumerated (`SetupDiEnumDeviceInfo` to
  `ERROR_NO_MORE_ITEMS` and no other error, every instance id read);
  disagreement, or any failure building or walking the set, is unknown.
- **On 98 SE and ME**: the walk of `HKEY_DYN_DATA\Config Manager\Enum`
  must complete - every subkey enumerated to `ERROR_NO_MORE_ITEMS`, and
  every subkey's `HardWareKey` read whole - and then an entry no
  `HardWareKey` names is absent. That walk is `XHCISNAP`'s fallback where
  `cfgmgr32.dll` does not answer; task 38.1's legs took their readings on
  98 SE and ME through `cfgmgr32.dll` (`xhcisnap\README.md`, "Where the
  device readings come from"), so **this route is unread**: a reading on
  both systems - present, disabled and unplugged devnodes, `HardWareKey`
  whole, the `Status` bits, and what a failed or cut-short walk looks like
  - comes before the tool relies on it for anything but a listing.
  `XHCISNAP`'s walk, which skips an unreadable subkey and stops at a cap,
  is a report and is not reused for this decision: here a skipped subkey,
  a failed or truncated read, or the cap reached makes **every** candidate
  unknown.
- An unknown answer refuses that removal and is printed with its reason;
  an unknown answer in a global test (below) refuses all.

**It never touches** a present or unknown devnode; any devnode whose
device id is a current form (`USB\XHCI98_...`, present or not - a hub
unplugged today is not stale); any device or function devnode
(`USB\VID_...`), including the old location-form children of 10.13.5 (D5)
and any refused interface's old devnode (NOX.1's decision 4, 10.13.9); the
controller's devnode; any file, INF, driver-store package or service.

**The global refusals** (D10). It removes nothing - and lists what it
would have removed, saying why - when any `XHCI98\` devnode is present
(a binary before `2.4.0.0` is running: before the restart of 10.13.4, or
after going back), or when the controller set below cannot be held. Either
test answering unknown refuses as well.

**The controller set, and why no `XHCI98\` devnode can become present
during a run on NT.** Only a `xhci98.sys` before `2.4.0.0` reports an
`XHCI98\` id, and on NT every devnode bound to the `xhci98` service runs
the one loaded image; a different image loads only once every one of
those devnodes has been removed or stopped (a driver update, a rollback, a
disable of every controller). So the tool holds every controller of this
project for its run:

1. **Find them**: every present devnode whose service is `xhci98` and
   whose device id begins `PCI\` (`SetupDiGetClassDevs` with no class GUID,
   no enumerator, `DIGCF_ALLCLASSES | DIGCF_PRESENT`, fully enumerated;
   `SPDRP_SERVICE` of each), the set's creation, enumeration or any
   property read failing being unknown.
2. **Check each**: started (`DN_STARTED` from `CM_Get_DevNode_Status`),
   with a child (`CM_Get_Child`, `CM_Get_Sibling`) whose device id is
   `USB\XHCI98_ROOT_HUB`, itself started - a present root hub whose driver
   did not start proves nothing about which image runs. A project
   controller not started, or without that child, refuses.
3. **Open each one's door**: the controller writes its door's
   `\DosDevices\HCDn` name as `SymbolicName` on its own devnode's hardware
   key at every start (`HcdDoorControllerStart`, `hcd_door.c`), so the
   tool reads that value (`SetupDiOpenDevRegKey`, `DIREG_DEV`), rewrites
   the prefix to `\\.\` as the property pages do (8.2), opens it, and
   checks the handle is that controller's: the door's
   `IOCTL_GET_HCD_DRIVERKEY_NAME` (`0x220424`, 8.10) answer must equal the
   devnode's `SPDRP_DRIVER`. A missing value, a failed open or a mismatch
   refuses. The `\\.\HCDn` index is never guessed: another stack's
   controller may hold `HCD0`, and the indexes have gaps.
4. **Keep the handles open** until the tool exits, so a query-remove of a
   held controller - which a driver update, rollback or disable of it sends
   first - meets an open handle. NT's PnP manager checks for open handles
   after a query-remove and vetoes it (Microsoft's "Handling an
   IRP_MN_QUERY_REMOVE_DEVICE Request"; the veto type
   `PNP_VetoOutstandingOpen` is declared in the Windows 2000 DDK's `cfg.h`,
   static); **whether it does so for this controller on each NT target is
   unread**, and is read in the legs before the tool may remove anything
   there (10.13.10); on a target where it is not, removal is disabled for
   that OS version in the shipped tool, which then lists only.
5. **Re-check**: immediately before each removal the tool repeats the
   global tests, steps 1 and 2 (the same controllers, by instance id, all
   still started with their root hubs) and the candidate's presence test;
   immediately after, it verifies the candidate gone and steps 1 and 2
   unchanged. Any change stops the run there, with what was done printed.

`CMP_WaitNoPendingInstallEvents` (declared in WDK 7.1's `cfgmgr32.h` for
Windows 2000 and later, static) is called before the first removal, so a
run does not start in the middle of an installation; it observes, it does
not lock, and the safety rests on the held handles, not on it.

**On 98 SE and ME there is no such exclusion.** The configuration manager
does not check open handles at a query-remove there - the driver is
expected to refuse it - and this driver's door keeps no open count
(`HcdDoorCreateClose`) while its controller FDO accepts a query-remove
(`HcdControllerPnp`); and the release notes' "the new `xhci98.sys` cannot
replace the running one" is a reading of an ordinary update, not of every
remove, reinstall or rollback sequence. So on 98 SE and ME **the tool lists
only** in this release (below).

**The route on NT** (2000 to 7): candidates from `SetupDiGetClassDevs`
with no class GUID, the enumerator `XHCI98` and `DIGCF_ALLCLASSES` (no
`DIGCF_PRESENT`), each one's id from `SetupDiGetDeviceInstanceId`; the
presence test's cross-check set is the same call with `DIGCF_ALLCLASSES |
DIGCF_PRESENT`, a failed set creation or id read being unknown. Each
candidate is removed by `SetupDiCallClassInstaller(DIF_REMOVE)`, the route
Device Manager's Uninstall takes, with `SP_REMOVEDEVICE_PARAMS` set to
`DI_REMOVEDEVICE_GLOBAL` through `SetupDiSetClassInstallParams`. After each
call it reads `SetupDiGetDeviceInstallParams` and treats `DI_NEEDREBOOT` or
`DI_NEEDRESTART` as "removed, restart pending", reported apart from
"removed" and kept in the exit code even if a later removal fails. A
failed `DIF_REMOVE` - access, WOW64, a class installer or co-installer
refusing, or anything unexplained - stops at that entry and is reported;
**there is no fallback route** (in particular not `CM_Uninstall_DevNode`,
which calls no class installer and sends no query-remove, so it would
bypass the reason for the refusal). It needs an administrator on 2000 and
XP and an elevated process on Vista and 7: the executable carries a
manifest asking for elevation (`requireAdministrator`, a resource MSVC
6.0's resource compiler can embed; 98, ME, 2000 and XP ignore it), and a
refusal for want of rights is reported as such.

**The route on 98 SE and ME** (D8): **list only in this release.** The
candidates come from `HKLM\Enum\XHCI98\` (`RegEnumKeyEx` to
`ERROR_NO_MORE_ITEMS`, each key name read with a buffer sized by
`RegQueryInfoKey`; a key it cannot read whole is listed as unreadable).
The owner's route (a) - the entry located with
`CM_LOCATE_DEVNODE_PHANTOM` and removed with `CM_Uninstall_DevNode`
through 98's and ME's `cfgmgr32.dll` (declared in the Windows 2000 DDK's
`cfgmgr32.h` as `CM_Uninstall_DevNode(IN DEVNODE dnPhantom, IN ULONG
ulFlags)`, static) - is enabled only when all of these have been read on
both systems: the exports and the flag honoured for a registry-only entry
(statically from the guests' `CFGMGR32.DLL` copied to
`tools\win98se-extracted\` and `tools\winme-extracted\` with their hashes,
a `legal-provenance.md` row, then on a guest) and what it leaves behind
(the driver key); the `HKEY_DYN_DATA` walk above; and an exclusion that
holds while the tool runs - a controller removed and reinstalled, and a
driver rolled back, while the tool holds its doors, each read to leave no
older image loaded, or a guard the driver adds that is shown to refuse
them. Until then the fallback (c) applies: the tool lists the entries and
removes nothing. Old and new entries carry the same names ("xHCI98 USB 3.x
Root Hub" twice), so it names a Device Manager procedure only if the 98 SE
and ME legs find one that tells the old entry from the new by more than
its name; otherwise it says that nothing needs the entries removed
(10.13.6). Never (b), a direct registry deletion (D8).

**What it prints** (D9). With no argument it lists and removes nothing:
each candidate's instance id, its `DeviceDesc`, its driver key and why it
qualifies, then each refused, unknown or skipped entry and why. `-remove`
lists, asks once (`Y` goes on; anything else, or end of input, cancels and
removes nothing), and removes, printing each result; `-yes` is accepted
only together with `-remove`, and skips the question, for the legs'
scripts. Exit codes: 0 nothing to do, or every candidate removed (or, on 98 SE
and ME, listed); 1
refused before any removal (a global refusal, an unknown, rights, WOW64,
a missing routine, cancelled); 2 stopped part-way (a removal failed, or a
re-check changed) - the lines printed say which entries were removed, and
whether a restart is pending for any of them; 3 every candidate removed,
a restart pending. Console only, as
`XHCISNAP`'s printing half is.

**Self-test**: the selection rule's parse and classification and the
three-state decision are pure (an instance id and the lookup results in, a
verdict out), so a `-selftest` runs them over canned inputs - each old
form, each new form, case variants, a truncated hex field,
`USB\XHCI98_NOXPORT&...`, an unrelated `XHCI98\` id, a lookup error, an
incomplete walk - and `build-driver.cmd` runs it when the EXE has been
built, as it runs `XHCISNAP`'s report self-test.

**Packaging**: `make-release.ps1` stages `tools\xhcicln\` with both
executables as it stages `tools\xhcisnap\` - a freshness check that
refuses an EXE older than its source, the version stamped from
`src\xhci_version.h`, a switch to leave it out on a host that cannot build
it - and `Assert-ToolsLayout`'s list of tool directories names it; the
package self-tests cover both. `XHCICL64.EXE` is a user-mode program, not
a driver: the packager's test signing of the x64 drivers (design record 19)
does not apply to it. The executables are a third and fourth tool
executable carrying a runtime (MSVC 6.0's statically linked `LIBC`, as
`XHCISNAP`'s; the 64-bit build WDK 7.1's, as that kit's user-mode builds
link it); `legal-provenance.md` section 2a and `AGENTS.md`'s "the two tool
executables" sentence change with them.

#### 10.13.9 Task NOX.1 (38.12 until 2026-10-13)

Task 38.12 - whether a refused storage interface's VID/PID device id binds
a VID/PID-only INF, and if so moving that PDO's device id to the
`USB\XHCI98_NOXPORT&VID_vvvv&PID_pppp[&MI_nn]` form - was deferred by the
owner on 2026-10-13 to after `2.4.0.0`, as "Planned work" NOX.1; its plan
and the owner's decisions on it are 10.7.1. So in `2.4.0.0` a refused
interface keeps its ids as they are (10.7). The two tasks touch in four
places:

- **The namespace.** `USB\XHCI98_NOXPORT` and the four new bases share
  the `USB\XHCI98_` prefix. The gates name each exactly: `HCD-USBRES`
  refuses any INF of this package that names `USB\XHCI98_NOXPORT`, which
  is NOX.1's decision 5 (10.7.1), and `OEM-USBID` admits only the four
  new bases. The tool's rule 1 names the old forms exactly, and no reader
  may test the prefix alone - so NOX.1, if it moves the device id later,
  changes no 38.9 gate.
- **The devnode an earlier version made.** If NOX.1 moves the device id,
  a refused device's old devnode under `Enum\USB\VID_vvvv&PID_pppp\...`
  is left alone, and the tool does not remove it (NOX.1's decision 4: it
  claims nothing, and when the device has a serial number it is the
  device's own working devnode). The tool's selection rule therefore has
  no hardware-id clause; D11 is settled by that decision.
- **The instance id** of a refused PDO is unchanged either way.
- **Order.** 38.4's rename of `xhcimsi.sys` edits the same INF and `.oem`
  files as this task, so the two are merged in turn and the INF gate run
  on each; NOX.1, after `2.4.0.0`, builds on 38.9's gate rules.

#### 10.13.10 The legs

The roadmap's: a fresh install and an update over `2.3.1.0` on 98 SE, ME,
2000, XP, XP x64, Vista and 7 x86 and x64, with a hub and a composite
device, Device Manager clean and no stale devnode claiming a device. What
this design adds:

- **Each update leg, before**: the old tree recorded (`Enum\XHCI98\`
  keys, the children's instance ids, one setting written on a serial-less
  mouse's devnode), and the running driver's version as `XHCISNAP`'s
  report gives it.
- **Each update leg, after** the update (and the restart where one is
  needed): the running driver's version again, from `XHCISNAP`, not from
  the Driver tab; the new devnodes under `Enum\USB\XHCI98_...`, each bound
  to `xhci98.sys`, no other driver offered; each prompt each target asked;
  the identity cases of 10.13.5 - a device with a serial id and one on the
  location form, at a root port and behind the hub, and a composite
  device's function in both forms - kept or new, and where the written
  setting went; Device Manager's default view clean (no mark, no unknown
  device, one current root hub per started project controller and no old
  one present); the old entries in the hidden view (NT, with
  `DEVMGR_SHOW_NONPRESENT_DEVICES=1` set before Device Manager starts, on
  every NT target, 10.13.6) and in Safe Mode (98 SE, ME); and an
  `XHCISNAP` report, compared with the one before, naming the new root
  hub, hub and composite devnodes.
- **The input trap** (D4): on 98 SE and on XP, an update with a
  serial-less USB keyboard and mouse on this controller as the only input
  device the user has, the leg recording what can be answered and with
  what; and the release notes' instruction - a keyboard and mouse
  independent of the updated driver (PS/2, or a controller another stack
  drives) - followed once on each; and on the two-controller guest, a
  keyboard on the second `xhci98` controller recorded as no help, since
  its root hub is new too.
- **98 SE** updated on a stock install and under SweetLow's stack; and
  a chain of serial-less hubs (QEMU hubs given a refused serial) as deep
  as QEMU allows, up to five tiers, with a serial-less device at its end,
  on `2.3.1.0` and after the update, each key read whole (10.13.1,
  "Lengths").
- **A hub or composite device first plugged in** between the update and
  the restart, on 98 SE (10.13.4).
- **Two controllers**: one update leg on a two-controller guest (2000 or
  XP), the running versions read on both controllers before and after
  (10.13.4).
- **The tool**, on every target: its listing, `-remove`, the hidden view
  and `Enum` again; its global refusals forced once (run before the
  restart on 98 SE; run with the controller disabled on one NT target);
  on each NT target, whether a disable, an update and a rollback of the
  controller are vetoed while the tool holds its door open (10.13.8) - the
  result deciding whether removal is enabled there - with a stock
  controller holding `HCD0` on one guest and two project controllers on
  another; on each 64-bit target the 32-bit build's refusal under WOW64
  and `XHCICL64.EXE` removing (XP x64, Vista x64, 7 x64); on 98 SE and
  ME, the listing, and the readings route (a) waits for (the exports, the
  `HKEY_DYN_DATA` walk, and a remove, reinstall and rollback during a
  run).
- **F6** (D12): an F6 install on Windows 2000, 32-bit XP and XP x64, each
  with a hub carrying a keyboard and a mouse and a composite device, in
  stages: in text mode, the keyboard behind the hub used as the input, and
  the target disk read offline at the text-mode reboot for the composite
  parent's and the hub's devnodes under `Enum\USB\XHCI98_...` with
  `Service=xhci98` (the reading 5.6 and issue 10 made); through GUI-mode
  Setup on XP and XP x64, where USB input is known to be dead until
  "Installing Devices" has run (5.6, issue 10), an independent input
  device, recorded as used; and by the end, USB input restored and the
  composite's function bound.

**The hubs.** QEMU's only hub is a Full Speed USB 1.1 hub (task 33.4). It
takes exactly the id and code path of a USB 2.0 hub (`USB\XHCI98_HUB`;
`XhciHubPdoId` with `usb3` 0, whatever the hub's speed). No guest presents
a High Speed hub or a USB 3 hub. The USB 3 hub's two halves
(`USB\XHCI98_HUB30` for the SuperSpeed one) are read on the owner's bench
or through a passthrough after the cut (D13): before it, the SuperSpeed
half's id rests on the host vectors (the same helper with one argument
changed) and the INF gate, and the roadmap's checkpoint and the release
notes say it was not read before the release. **The High Speed USB 2.0
hub** the roadmap names is not D13's: D14 asks the owner whether QEMU's
Full Speed hub stands for it before the cut (same id, same code), the
High Speed reading coming with the USB 3 hub's - whose USB 2.0 half is a
High Speed hub under `USB\XHCI98_HUB` - after it, or a High Speed hub is
read before the cut by passthrough.

#### 10.13.11 Unread, and how it is read

| Unread | How it is read |
|---|---|
| Whether 98's configuration manager, NTKERN or `setupx.dll` treats `Enum\USB` entries other than by id (10.13.3) | the 98 SE (stock and SweetLow) and ME update legs |
| Whether a serial-form devnode keeps its devnode on 98 SE and ME, and ME's location-form key (10.13.5) | the same legs |
| Whether Update Driver replaces the old INF in 98's `INF\OTHER` (10.13.4) | the 98 SE leg's `INF\OTHER` listing before and after |
| What a new devnode of an already-staged package asks on Vista and 7 (10.13.4) | the Vista and 7 update legs |
| Whether an old non-present devnode keeps a COM number or blocks `pnputil -d` (10.13.5, 10.13.6) | not a leg of this task; the release notes say "unread" |
| Whether Device Manager lists a registry-only entry on 98 SE and ME, normal and Safe Mode (10.13.6) | the 98 SE and ME legs |
| Whether 98's and ME's `cfgmgr32.dll` export `CM_Uninstall_DevNode` and honour `CM_LOCATE_DEVNODE_PHANTOM` (10.13.8, D8 (a)) | statically from the guests' files, then on a guest |
| Whether `DIF_REMOVE` removes a non-present devnode on each NT target, its restart flags, and the 32-bit call's failure under WOW64 (10.13.8) | the tool's legs |
| Whether NT vetoes a controller's query-remove while the tool holds its door open (10.13.8) | the tool's legs on each NT target; removal disabled where it is not |
| What 98 SE allows for a location-form key two hub tiers deep (10.13.1) | the 98 SE SweetLow leg |
| When the new ids appear on 2000 and XP with 38.4's helper, on one and two controllers (10.13.4) | the update legs |
| Whether any input-free route through the XP and 98 SE prompts exists (D4) | not assumed: the release notes ask for independent input |
| A USB 3 hub's SuperSpeed half under the new id (10.13.10) | the owner's bench or a passthrough, after the cut (D13) |
| A High Speed USB 2.0 hub under the new id (10.13.10) | D14 |
| Whether 98 SE's `HKEY_DYN_DATA\Config Manager\Enum` walk is complete and reliable, and what an exclusion during a run needs there (10.13.8) | the 98 SE and ME legs, before route (a) |
| How deep a chain of serial-less hubs 98 SE takes, under the old ids and the new (10.13.1) | the 98 SE leg |

#### 10.13.12 Decisions taken (owner, 2026-10-13)

The owner kept the task in `2.4.0.0` with the update costs of 10.13.4 and
10.13.5, and took D1 to D13 as below; after a review's opinion on each, the owner
changed D4, D7 and D12 and kept the rest. The options put are kept beside
each. D14, from the review's second round, is open.

| | Question | Options put | Decided |
|---|---|---|---|
| D1 | Do the new PDOs also report the old `XHCI98\...` id as a further hardware id, for the transition? | (a) no; (b) yes, last | **(a) no**: the device id alone decides the devnode, so an extra hardware id keeps nothing; it would only let the old package's INF compete for the new devnode, and keep a dead id alive in every gate |
| D2 | Do the new INFs keep the old `XHCI98\` model lines (and `txtsetup.oem` its old rows)? | (a) no, a clean move; (b) the INFs keep them, `txtsetup.oem` not | **(a) a clean move**; the release notes say that on 98 SE and ME no new hub or composite device is connected before the restart (10.13.4) |
| D3 | Do the root hub's install sections stop copying `xhci98.sys`, as the hub and composite sections do? | (a) yes; (b) no, as today | **(a) yes**: after this update the root hub is a new devnode at a start where `xhci98.sys` is already running, and on 98 SE and ME its copy would replace the running file from wherever the update was taken from; the root hub exists only under a running `xhci98.sys`. With it: `HCD-HUBCOPY` widened to the root hub, the footprints re-emitted, a fresh-install leg on each path |
| D4 | XP and XP x64 ask Found New Hardware and the unsigned warning for the new root hub, and the USB keyboard and mouse behind it do nothing until it is answered | (a) document it; (b) a way for the driver to spare XP the root hub's new devnode - none is known, since the device id alone decides the devnode | **(a) documented, as the review advised** (owner, after its opinion): before updating, have a keyboard and mouse independent of the updated driver - PS/2, or a USB controller another stack drives; a second controller running `xhci98.sys` does not count, since the restart that loads the new image gives its root hub a new devnode too; the release notes make no claim that the driver signing option helps; the same holds on 98 SE, where the serial-less keyboard and mouse are new devnodes too (10.13.5); legs on 98 SE and XP with a serial-less USB keyboard and mouse as the only input (10.13.10) |
| D5 | The serial-less devices' new devnodes (10.13.5): the settings and COM numbers left on the old ones | (a) document it; (b) the tool also removes old children tied to an old parent by its `ParentIdPrefix` value; (c) carry the settings over | **(a) documented only**; the tool leaves every device and function devnode |
| D6 | The tool's name and place | (a) `XHCICLN.EXE`, source `xhcicln\`, shipped `tools\xhcicln\`; (b) a mode of `XHCISNAP` | **(a)** |
| D7 | The 64-bit targets | (a) a second, amd64 build `XHCICL64.EXE`; (b) the 32-bit build only | **(a), by design** (owner, after the review's opinion, replacing "only if the x64 leg shows the 32-bit build refused"): `XHCICL64.EXE` ships; the 32-bit build lists but refuses removal under WOW64; the 64-bit build tested on XP x64, Vista x64 and 7 x64 |
| D8 | The route on 98 SE and ME | (a) `CM_Locate_DevNode` with `CM_LOCATE_DEVNODE_PHANTOM` and `CM_Uninstall_DevNode`; (b) a direct registry deletion; (c) list and point at Safe Mode's Device Manager | **(a), with (c) as the fallback** where the reading or the call refuses; never (b). After the review: on 98 SE and ME the tool lists only in this release, since those systems give no exclusion against an older image loading during a run; (a) is enabled only once it, the `HKEY_DYN_DATA` walk and such an exclusion have been read on both systems, and (c) names a Device Manager procedure only if the legs find one that tells the old entry from the new by more than its name (10.13.8) |
| D9 | The switches | no argument lists; `-remove` asks then removes; `-yes` skips the question; exit codes | **as written** (10.13.8), the exit codes now 0, 1, 2 and 3 (restart pending), a pending restart reported even when a later removal fails |
| D10 | The global refusals | (a) both (an `XHCI98\` devnode present; no present `USB\XHCI98_ROOT_HUB`); (b) only the first | **(a) both**; after the review every project controller must be started, with a started `USB\XHCI98_ROOT_HUB` child and its door held open, and any test answering unknown refuses (10.13.8) |
| D11 | If the refused device id moves: does the tool also remove old refused devnodes? | (a) yes; (b) no | **left to NOX.1** (38.12 deferred), whose decision 4 is that the tool leaves them (10.13.9) |
| D12 | An F6 leg (32-bit XP, with a hub), since both `txtsetup.oem` files change | (a) add it; (b) the gates suffice | **(a), widened** (owner, after the review's opinion): F6 legs on Windows 2000, 32-bit XP and XP x64, each with the input behind the hub and a composite device bound (10.13.10) |
| D13 | The USB 3 hub the roadmap's legs name, which no guest can present | (a) host vectors and the gate, the reading on the owner's bench or a passthrough after the cut; (b) a passthrough leg before the cut | **(a), unchanged** after the review's opinion: the USB 3 hub is read after the cut, and the roadmap's checkpoint and the release notes say so (10.13.10) |
| D14 | **Open** (from the review's second round): the roadmap's legs name a USB 2.0 hub; QEMU's is Full Speed. Does QEMU's Full Speed hub stand for it before the cut? | (a) yes, the same id and code; the High Speed reading with the USB 3 hub's after the cut, whose USB 2.0 half is one; (b) a High Speed hub by passthrough before the cut | proposed **(a)**, the checkpoint and the release notes saying so with D13's |

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
`Dma64BitAddresses`, so every hardware address field keeps its high half at
zero, as `AGENTS.md` requires. The adapter does not guarantee that every
address `MapTransfer` returns is below 4 GB: with a NULL map register base a
page above 4 GB comes back as it is (11.6.1). The HCD refuses such a run
(`hcdMapExecute`, `HighPart` not 0) rather than program it; on an amd64 machine with memory above 4 GB the map registers are
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

### 11.5 The command size and its measurement (tasks 36.21 and 36.22)

Two sizes are kept apart (`roadmap-hcd.md`, "The transfer size"): the
**command size**, what one UAS command asks the device to move, which
`xhciuas.sys` advertises in its adapter descriptor and so decides how
large an SRB the storage class driver builds; and the **TD size**, how much
of one data URB the HCD puts on the ring at once, which 11.3's chunking
decides (at most 32 pages and the grant, packet-aligned when not the last).
Task 36.22 changes only the first.

- **The value.** `XhciMaxTransferKB`, a DWORD in the controller's driver
  key: 64 to 2048 in steps of 4, default 1024; absent, of another type, out
  of range or off a step reads as 1024, each noted (`xfer.max.value`,
  `xfer.max.why`, `xfer.max.bytes`). Read at every controller start
  (`hcd_ctl.c`, `hcdReadValues`; the rule is `XhciXpolValue` in
  `xhci_xstat.c`). The INFs write 1024 under 34.1's don't-overwrite rule.
- **The query.** `IOCTL_XHCI98_TRANSFER_POLICY` (`src\xhci98_xfer.h`), on
  the device PDO, pointer-free: signature, version and size in, the
  command size with the contract's range and step out, answered at once
  from what the start latched and nothing else (`hcd_urb.c`,
  `hcdPolicyIoctl`). `xhciuas.sys` sets 64 KiB, then asks at every start
  before it selects its pipes and before any LUN exists (`uas_fdo.c`,
  `uasAskPolicy`); an older bus (`STATUS_NOT_SUPPORTED`), a reply out of
  contract and a request that failed in transport each leave 64 KiB,
  counted apart, and the start goes on (`UasPolicyValue`, `uas_iu.c`,
  `test_uas`). Whatever the outcome it is noted in the UAS device's
  hardware key - on NT its `Device Parameters` - as `XhciUasCommandBytes`,
  `XhciUasCommandSource` (0 not asked, 1 the bus, 2 an older bus, 3 out of
  contract, 4 the request failed) and `XhciUasCommandStatus` (the request's
  NTSTATUS), so the note survives an older bus and a start that fails
  later (`uasNotePolicy`). The adapter descriptor's `MaximumTransferLength` and
  `MaximumPhysicalPages` (bytes / page + 1) and every pipe's
  `MaximumTransferSize` read that one stored value.
- **The device's own limit** (owner, 2026-10-09, after SweetLow's comment
  on GitHub issue 17). Before it stores the bus's answer, `xhciuas.sys`
  reads Microsoft's per-device value, `MaximumTransferLength`, a
  `REG_DWORD` in bytes under
  `HKLM\SYSTEM\CurrentControlSet\Control\usbstor\VVVVPPPP` (KB 2581464;
  `VVVV` and `PPPP` the device descriptor's `idVendor` and `idProduct` in
  uppercase hex) - the key Windows' own `usbstor.sys` reads and the
  existing tools write - through `RtlQueryRegistryValues` with a routine
  that records the type and length (`uas_fdo.c`, `uasReadLimit`). The rule
  is pure (`UasDeviceLimit`, `uas_iu.c`, `test_uas`): taken only as a
  four-byte `REG_DWORD` from 65535 to 2097152 - KB 2581464's range, 65535
  to 2097120, with its top widened to 2097152, which Uwe Sieber's tool and
  others write for 2 MiB (owner, 2026-10-10) - and only when the bus
  answered the question; then it replaces the bus's
  answer, above it or below it, for every device with that `idVendor` and
  `idProduct` - the key is per model, machine-wide, not per physical
  drive, on any controller - and the controller's `XhciMaxTransferKB`
  stays the default for every device without a valid value of its own.
  Absent, another type, out of range, or valid on a bus that did not
  answer (which keeps 64 KiB) is ignored and noted. A taken value is rounded to
  the **nearest 4 KiB, ties up**: the contract's grid (64 KiB to 2 MiB in
  4 KiB steps) is what the bus's answer obeys and what the descriptor's
  `MaximumPhysicalPages` (bytes / page + 1) is built for. Nearest moves a
  value least - never more than 2048 bytes, a tie going up - and takes
  65535 and 2097120, Microsoft's spellings of 64 KiB and 2 MiB, and the
  tools' 2097152 to exactly those (rounding down would make 2097120 into
  2 MiB less 4 KiB). It is a normalisation onto the grid, not a byte ceiling: the size
  taken can exceed the value by up to 2048 bytes. The bridge caps in use
  (64 KiB, 120 KiB = 240 sectors, 512 KiB, 1 MiB) are on the grid and do
  not move. (Until 2026-10-10 the range stopped at 2097120 and refused
  2097152; the owner widened it so a tool's 2 MiB is taken as written.)
  The note in the device's hardware key gains
  `XhciUasDeviceLimit` (the value read, 0 when absent or not four bytes)
  and `XhciUasDeviceLimitWhy` (0 absent, 1 used, 2 not a `REG_DWORD`, 3 out
  of range, 4 valid but the bus did not answer), and
  `XhciUasCommandSource` 5 says the size is the device's. On Windows 98
  and ME the value is read as well and changes nothing that reaches the
  device: `USBNTMAP.SYS` sends at most 64 KiB whatever the descriptor says
  (36.21's reading). Like the controller's value it takes effect when the
  storage stack next reads the descriptor, at the next restart.
- **Activation.** A storage class driver keeps the descriptor it read and a
  UAS stop and start keeps the LUNs it knows, so a new value takes effect
  at the next reboot. An SRB sized to an older value is carried as it is:
  neither driver cuts it short or refuses it, and the HCD chunks it.
- **What 36.21 measures** (`xhci_xstat.h`; the snapshot's HCD region,
  `XHCISNAP` "transfer size"): bulk data URBs by length and the chunks each
  became, the map grant, the gap between two chunks of one URB in two parts
  (retirement to the next publication, and the ring empty in software's
  view with the URB unfinished), and the processor time of a publication
  and a completion pass, and a next chunk's mapping. The clock is the
  processor's time-stamp counter (RDTSC, no import), each stamp carrying
  the full count and the processor's initial APIC ID (CPUID), so an
  interval is measured only within one processor's clock and one across
  two is counted apart; the start's and the snapshot's (counter, system
  time) pairs convert counts to time, approximately where processors'
  counters disagree. Hardware starvation before software handles an event
  is not measured. `xhciuas.sys` counts its commands by length and by the
  number in flight, and hands the bus a bounded record, at most once a
  second and when anything in it changed (`IOCTL_XHCI98_TELEMETRY`, copied whole into the
  bus's own storage, its stop cancelling a send still pending), so a dump
  shows both sides; the bus keys each record by the device it came from
  and keeps it, said departed, once the device leaves.
- **The qemu flavour** adds `XhciQemuMapGrant`, a constrained grant for
  the legs (`build-and-test.md`, the qemu test aids).

### 11.6 Stage 2: two chunks of one URB on the ring (task 36.24)

**Why.** With a 1 MiB command a URB is eight 128 KiB chunks, and between
two of them the ring stands empty while software retires one chunk, gives
its map registers back, plans and maps the next and publishes it. 36.21's
readings in QEMU (`out\p36-mb\xp64\notes.md`, development host A, XP x64,
UAS at SuperSpeed): the gap, retirement to publication, averaged 84 to
120 us a boundary against a 27 to 34 us publication, the mapping 9 to
22 us of it; the ring stood empty for all of it. Stage 2 (owner,
2026-10-09: taken whatever the readings) maps the whole URB once and keeps
the next chunk on the ring before the current one ends, so the controller
goes from one chunk to the next with no software between them and no
mapping at all - on NT, behind a switch that stays off until the HAL
readings point 2 names are recorded. Windows 98 SE and ME gain nothing
from it: their mapping layer sends at most 64 KiB (36.21), which is
one TD.

Review history: round 1 (thread `01a120f7`) took apart a draft that mapped
two chunk slots by two channel requests; round 2 (thread `01a12148`)
reviewed the one-mapping design and found its admission rule unevidenced,
its error path editing a ring the controller may still own, the short's
hand-off to the thread incomplete, and the failures before a TD reached
the ring outside the record's life; round 3 found that no accounting of
this driver's own registers can make a whole-URB request safe on a HAL
whose registers are real, and that the byte count, the mapping's
ownership and the cancel walk's capacity needed rules of their own. This
text answers all three: stage 2 never asks for more registers than stage
1 does.

**What changes, and what does not.**

1. **Eligibility.** A URB is pipelined - two of its TDs on the ring at
   once - only when all of these hold, judged once when its record is
   filled (`hcdFill`) and never changed afterwards:
   - a bulk pipe, not control, interrupt or isochronous;
   - more than one chunk at today's plan (`hcdPlanChunk`), so a URB that
     fits one TD is untouched;
   - the direction rule: **OUT on any bulk pipe; IN only on a stream**
     (UAS at SuperSpeed). After a Short Packet the controller goes on to
     the next TD of its ring (xHCI 4.10.1.1). On a stream the ring belongs
     to one command until `xhciuas.sys` has seen the command's status and
     every data URB of the tag has completed - its tag is not reused before
     then (`uas_xport.c`, `uasSlotIdle`) - so a successor a short leaves
     behind receives nothing; it is taken off before the URB completes
     (point 6). On a pipe without streams - Bulk-Only, and High-Speed UAS,
     which this bus cannot tell apart - the next packet after a short data
     phase may be the CSW or another command's data, which a queued
     successor would take: those IN pipes stay one chunk at a time;
   - the URB's page span - `ADDRESS_AND_SIZE_TO_SPAN_PAGES` of its MDL
     virtual address and length, computed as today's chunk span is, which
     cannot overflow for a length below 4 GiB - at most the wide entry's
     capacity (513 elements, a 2 MiB URB at any alignment) and at most the
     map grant, the per-request ceiling `AllocateAdapterChannel` allows;
     a larger URB - a device whose own limit is above the controller's
     value, a class driver that sends more - is not refused: it takes
     today's sequential path;
   - a wide entry is free (point 3).

   Eligibility only claims the entry; whether the URB is pipelined is
   settled when its first mapping runs (point 2). Everything else -
   control, interrupt, isochronous, single-chunk URBs, Bulk-Only and
   High-Speed UAS IN, a URB over the span limits, no free entry - keeps
   today's path unchanged, one chunk at a time, each mapped by its own
   channel request.
2. **One mapping per URB, and only where it costs no registers.** An
   eligible record asks the pump for exactly what stage 1 asks: today's
   first chunk (`hcdPlanChunk`, at most 32 pages). Inside that request's
   execution routine (`hcdMapExecute`) the record is **promoted** to a
   whole-URB mapping only when all three hold:
   - the HAL handed this request no map registers - `MapRegisterBase` is
     NULL, which is how a HAL says the adapter's transfers need no
     translation and `MapTransfer` returns the pages' own addresses;
   - the adapter's raw grant at this start (`hc->MapGrantRaw`, from
     `IoGetDmaAdapter`, before any `XhciQemuMapGrant` constraint) is
     exactly `0x80001`, the value every HAL read in
     11.6.1 reports for a register-free adapter of this description; and
   - promotion is **enabled**, and the controller's **register-free
     check** passed at this start (both below).

   Promoted, the routine calls `MapTransfer` over the whole URB, run by
   run, into the wide entry's list, and nothing is held for it: there are
   no registers to hold. Not promoted - a non-NULL base, the check not
   passed, any run that does not map - the routine maps the first chunk as
   today, the entry goes back, and the URB is sequential for the rest of
   its life. A promoted URB's end gives back what its request took, as
   today's end of a NULL-base chunk does (`HcdDmaUnmap`: with a NULL base
   there are no registers to free and nothing for the HAL to copy back),
   once, by the record's `MapState`, not per TD. Whether the HAL wants
   `FlushAdapterBuffers` called with a NULL base at all is the same
   question for today's chunks and is not changed here. No request ever
   asks for more registers than today's, so
   nothing stage 2 does can make a request wait that would not have waited
   at stage 1 (round 3, finding 1: never ask for a whole URB to find out
   whether it is safe).

   **Promotion is off by default (owner, 2026-10-10, for `2.2.0.3` and
   `2.3.0.0`); `XhciWideMap` 1 opts in.** It was off until the HAL
   readings were recorded and briefly on by default after them (11.6.1's
   verdict); the owner turned the default back off after a focused
   review:
   a regression is unlikely but unquantified - the HALs after the read
   service packs are unread, and the gates (a NULL base, the grant
   `0x80001`) are correlated outputs of one adapter implementation, not
   independent proofs - the failure mode would be silent data corruption,
   and the review found no defect in the gate code. Every gate below is
   kept as it is; only the absent-value default changed. Eligibility
   covers every qualifying bulk OUT transfer; bulk IN only on a stream
   (point 1), so it is not UAS-only. `MapTransfer`'s documented contract
   (`PMAP_TRANSFER`, wdm.h) bounds a mapping by the registers the request
   was given ("the number of map registers that can be set up cannot
   exceed the maximum returned"), and says nothing of a NULL base; mapping
   a whole URB under a one-chunk request is therefore outside the
   documented contract, and no runtime test can make it safe to try (round
   4, finding 1: the probe itself performs the unproven operation). So:
   - the evidence is a **recorded reading of each NT target's HAL** - the
     `MapTransfer` and `AllocateAdapterChannel` behind the adapter this
     HCD gets (the HAL's master-adapter path for a 32-bit scatter/gather
     bus master) on Windows 2000 SP4, XP x86, XP x64 / Server 2003 x64,
     Vista x86 and x64 and 7 x86 and x64, read as the project reads other
     binaries (disassembly of the shipped HAL, design record 13's evidence
     rules): that a NULL base is passed exactly when the adapter needs no
     translation, that `MapTransfer` with a NULL base consumes no
     register and does not consult the request's count, over any MDL,
     either direction and any span the contract allows. It is task 36.24's
     evidence step, done before the switch below may default on, and
     recorded in this section as a table of HAL builds (11.6.1,
     2026-10-10: on every target HAL a NULL base is pure pass-through,
     and every HAL or verifier that has real registers hands over a
     non-NULL base; the default's scope is 11.6.1's verdict);
   - the switch is `XhciWideMap`, a `DWORD` in the controller's key, read
     at every start and written by the INFs at 0 (since roadmap-hcd 36.51,
     owner, 2026-10-11; by no INF before): absent or of another
     type it is off (`HCD_WIDE_MAP_DEFAULT`, `hcd.h`, the one constant that
     sets the default; owner, 2026-10-10), 0 is off and any other number
     on. With it off every URB is sequential, exactly stage 1; the release
     notes give 1 as the way in. On, the gates above still decide each start and each
     URB: Windows 2000 or later, the adapter's grant exactly `0x80001`
     (kept apart from the qemu flavour's constrained grant,
     `hc->MapGrantRaw`), the register-free check below passed, the pool
     allocated, and a NULL base for the URB's first request. The reading
     covers the service packs in 11.6.1's table; HAL hotfixes after them
     were not read, and those runtime gates are what stands for them.
     Under Driver Verifier's DMA checks the base is never NULL (11.6.1),
     so nothing is promoted there;
   - with the switch on, the start-time **register-free check** runs as a
     regression check, never as the capability detector: at the
     controller's start, on NT only (98 and ME never send a URB that
     would be promoted, so promotion stays off there whatever the switch
     says), and only when the grant is `0x80001` (anything else leaves
     promotion off without mapping anything; `xfer.wide.check` 8), the HCD maps one 16-page non-paged buffer of its own twice
     through the pump - by a 1-register request whose routine, handed a
     NULL base, maps all 16 pages, and by an ordinary 16-register request
     - and the two lists must name the same addresses and lengths element
     for element, and both bases must be NULL. Anything else leaves
     promotion off for that start, counted and noted (`xfer.wide.check`).
     Its callbacks follow the same ownership rules as a record's (point 6,
     mapping ownership): the buffer and its MDL are freed only once both
     routines have returned and released what they were given. Its
     readings on each NT target in QEMU are 36.26 legs, and the bench's
     on an amd64 machine with memory above 4 GB, where a bounce HAL must
     leave it off.
3. **The wide-map pool.** `HCD_WIDE_ENTRIES` (8) entries, allocated with
   the controller at its start from non-paged pool (NT only, only with
   `XhciWideMap` on and the register-free check passed) and freed at its
   stop, each sized
   for the contract's largest command whatever the value in effect: a
   plain array of 513 elements (24 bytes each on both architectures,
   enforced in `xhci_usbport.h`: 12,312 bytes), the second TD's
   `XHCI_TRANSFER` and its 31-element list (the first TD's is the record's
   own, which holds 32), and the entry's own fields - about 13 KiB an entry
   and about 104 KiB for eight, `HCD_WIDE_ENTRY`'s `sizeof` asserted in
   `hcd.h` at most 14 KiB. If the allocation fails the pool
   is empty and every URB is sequential. An entry is claimed under the
   controller lock when an eligible record is filled and returned at its
   finalization (point 6), or when its first mapping does not promote; the
   entry count bounds everything stage 2 adds controller-wide (the event
   burst).
4. **Forward progress.** Because promotion holds no registers and every
   request is today's, the pump, its order and every request's size are
   exactly stage 1's: the cycle a request waiting for registers could close
   (`xhciuas.sys` posts status, data, then the command IU, `uas_xport.c`
   ~712) is no likelier than it was. Round 2's budget, latch and grant
   floor are withdrawn: none of them made a whole-URB request safe on a
   HAL with real registers (round 3, finding 1), and promotion never makes
   one.
5. **TDs from the wide list.** A TD is cut from the URB's list by the pure
   planner (`XhciXpipePlan`, `xhci_xpipe.c`): from the next byte not yet
   planned, at most **31 elements** - half the usable TRBs of every ring
   this driver has, pool rings and stream rings alike (64 TRBs, 62 usable;
   `XhciRingCapacity`), so two TDs of one URB always fit an empty ring -
   and, when it is not the URB's last, ending on a packet boundary (a
   short packet inside one URB is not allowed, USB 2.0 5.8.3), which may
   cut its last element short; the next TD starts inside that element.
   Each element is one page or part of one (the mapper cuts at page
   bounds), so one element is one data TRB and the 64 KiB rule never
   splits one. The TD's own list (at most 31 elements) is copied from the
   wide list into the TD's slot, the first and last trimmed, each element's
   offset counted from the TD's start (`XhciXpipeBuild`), and handed to
   `XhciXferSubmitNormal` as today's chunk list is. A TD of 31 page-aligned
   elements carries 124 KiB, so a 1 MiB URB is 9 TDs aligned and
   unaligned, 2 MiB 17 aligned and 18 unaligned (a TD that starts inside a
   page holds a little less; `test_xpipe`).
6. **The record's life** (under the controller lock, except the mapping,
   which precedes it, and the completion, which follows it):
   - fields: `Planned` (bytes put in TDs so far), `Committed` (bytes of TDs
     retired, in ring order, up to the boundary), `OnRing` (0, 1 or 2 TDs
     of the URB on the queue), `NoMore` (no further TD is published: set by
     every outcome below), `Sealed` and its TD (the byte boundary: no TD
     after it counts), `Truncating` (a
     successor must be taken off by the thread), `Outcome` (the status
     the URB reports and which event decided it, set once, below),
     `MapState` and the allocation it owns (below), `Seq`
     (the URB's own submission order, unchanged by how many TDs it
     takes);
   - **two TD slots**: slot 0 the record's own `Xfer`, slot 1 the wide
     entry's. Both carry the record as `TransferParameters`, which is how
     the retire path, the cancel walk and the drain find the record from
     either TD - `(PHCD_XFER)t` is replaced by `HcdIoXferOf(t)` wherever a
     TD of a pipelined record can arrive (`HcdIoRetired`,
     `hcdCfgCancelPipe`, `hcdDrainOne`). The slots alternate; a slot is
     reused only after its TD has been retired and accounted;
   - **TDs of one ring retire in ring order** - the controller ends a TD
     before it starts the next (4.10.1.1), and a stream's events come in
     its ring's order - so retirement is accounting in order, including
     when one event pass retires both TDs of the record (a short's
     deferred tail swept by its successor's event): the engine's completed
     list is in ring order and is accounted that way. A TD before the
     boundary adds what it moved to `Committed` - all of it, or a short's or
     an error's count, or a stopped TD's progress as latched by its Stopped
     event (`XhciXferQueueStopped`, as today's completion takes it,
     `hcd_io.c` ~1835) - and a TD that moved less than it asked for, or
     ended in error, becomes the boundary (`Sealed`); a TD after the
     boundary adds nothing (round 3, finding 2: the boundary is by TD
     order, not by when an outcome was decided);
   - **refill**: when a TD retires in full, `NoMore` is clear and
     `Planned < Length`, the next TD is planned and published in the
     same lock hold, after the engine's event result has been walked
     (`hcdPipeResult` collects refills and publishes them after its
     completion loop, so the engine's intrusive `Next` links are never
     rewritten while being walked), unless the pipe is paused, closed or
     its device going, or the ring refuses it (`XHCI_XFER_BUSY`: only
     another record's TD can share the ring, and its retirement retries).
     A refill held by a pause is published by the resume
     (`HcdIoPipeRelease` walks the pipe's pipelined records with
     `OnRing < 2`, `NoMore` clear, with bytes left, in `Seq` order);
     repeated refills wrap the ring as any TD does. Publication order is
     ring order, and only the record publishes its own TDs, so no other
     request lands between two of them (`Exclusive`, unchanged);
   - **an early end** - a short, or any completion the engine reports as
     not success (a halt's error, a TRB Error, and the engine's own
     verdicts such as an impossible residual turned into
     `INTERNAL_HC_ERROR`, xhci_xfer.c ~2070, which can retire a TD whose
     successor is already executing) - seals the boundary at that TD, sets
     `NoMore` and offers its status to `Outcome` (below), in the same lock
     hold. **No ring
     is edited there.** If the successor is still on the
     queue, the record is also marked `Truncating`, the pipe's
     `CancelPending` and the controller's `CancelWork` are set, and the
     thread is woken - all three before the lock is dropped, which is what
     `HcdCfgCancelService` tests (hcd_cfg.c ~3730). From then until the
     successor has left the queue, **no path rings that pipe's or that
     stream's doorbell**: a refill does not run on a `NoMore` record, and
     `hcdCfgStreamKick`, `hcdCfgCancelPipe`'s survivor doorbell and
     `HcdIoPipeRelease`'s publications all skip a pipe whose queue holds a
     `Truncating` record's TD. The thread then takes the successor off
     exactly as it takes a cancelled request's TD, by `hcdCfgCancelPipe`
     with its predicate widened from "the record's `CancelRequested`" to
     "the record's `CancelRequested`, or the TD is a `Truncating`
     record's successor": the pipe paused (for a stream, the endpoint), the
     endpoint brought to Stopped by `hcdCfgQuiesce` whatever state it is in
     - Running stopped, Halted reset with TSP 1 and its failed TD skipped by
     `hcdCfgRecoverDequeue`'s Set TR Dequeue, Error left by Set TR Dequeue -
     and only then the successor made No Ops or, at the head, skipped by
     Set TR Dequeue to the next survivor or the enqueue; the sibling
     streams' recovery runs inside the same pause, so the interval between
     a Reset Endpoint and its Set TR Dequeue rings nothing. The successor
     is retired then with an internal status that marks it truncated and
     never reaches the URB, which keeps the outcome latched at the early
     end - a short completes as success with its length under the client's
     `ShortOk` rule, an error with its status, never as cancelled. A
     successor already retired by the time the thread looks (both TDs in
     one event pass, or the device fed it) is simply not found, and its
     bytes, past the boundary, are never counted. A client cancel racing
     the early end finds the boundary sealed: the outcome stays the one
     latched first. A cancel, abort, reset or removal that comes first sets
     `NoMore` and offers its status to `Outcome`; the TDs it takes off retire in ring order
     with the progress their Stopped events latched, the first that did
     not finish sealing the boundary, so bytes the device moved before the
     stop are reported as today's cancel reports them;
   - **whose status the URB reports** (round 4, finding 3): the byte
     boundary and the status are decided apart. The status is `Outcome`,
     written once under the lock by the first of these to be latched, in
     time: a client cancel (or abort, reset, removal), an early end on the
     ring (a short, an error, an engine verdict), or a failure before the
     ring (a mapping, a publication). Later events still move the byte
     boundary - a short after a cancel seals at its TD; a predecessor's
     short or error after a refill's publication failed seals at that
     predecessor, so the length is the earlier one - but never the
     status, with one ordering rule: an early end on the ring at a TD
     **before** the position of an already-latched publication failure
     replaces that failure's status, because the device ended the transfer
     before the TD whose publication failed was due (the failure then
     describes nothing the device saw). A short so latched reports success
     with its length under the client's `ShortOk` rule, as today; a cancel
     reports cancelled with the length its stopped TDs moved, as today;
   - **a Stop** (`STOPPED`, `STOPPED_LENGTH_INVALID`,
     `STOPPED_SHORT_PACKET`) is not an end: it latches progress as today
     (`XhciXferQueueStopped`), and a TD that survives a sibling's stop or
     the cancel of another record runs on when its stream is rung again. A
     soft retry that diverts a Transaction Error retires nothing; its TD
     ends the URB only when the retry's replay retires it with the error;
   - **failures before a TD reaches the ring** - the first channel request
     refused (`MapRefusals`), a mapping that fails, a publication the ring
     refuses other than BUSY - set `NoMore`, offer the failure's status to
     `Outcome`, and seal the boundary **after the last TD already
     published**, so a TD still on the ring keeps counting when it retires: a refill that fails
     after TD A completed and with TD B running reports A's and B's bytes
     and the failure's status (round 3, finding 2);
   - **mapping ownership** (round 4, finding 2): `MapState` is one of
     *not queued* (filled, held before its mapping: today's held-unmapped
     record), *queued* (on the pump's list), *in its routine* (the HAL has
     called `hcdMapExecute`), *allocated* (the routine has returned
     keeping what it was given: the base, NULL or not, the register count
     the request asked for, and how far the mapping got - the whole URB if
     promoted, the chunk, or a part where a run failed), and *refused* (no
     allocation was ever acquired: `AllocateAdapterChannel` failed, or the
     record was cancelled before it was queued). A record whose routine
     finds it cancelled, or whose mapping fails part-way, still returns
     **allocated** with what it holds - never refused - and keeps the
     obligation to give it back. From *queued* until the routine has
     returned the record is the pump's; a cancel or removal then only
     marks it, as today's `HcdIoPipeMarkAll` and `HcdIoCancelPdo` do.
     Unmapping is by the allocation the record owns, never by `MapBase`,
     which is NULL on every promoted mapping and on every register-free
     one today: an allocated record gives back exactly the count its
     request asked for under the base it was given, after flushing the
     extent it mapped; a refused one gives back nothing;
   - **one finalizer**: every path that sets `NoMore` or lowers `OnRing` -
     retirement, a failed publication, the mapping's return, the cancel
     walk, the drain - ends with the same check under the lock:
     `MapState` is *allocated* or *refused* (or *not queued* for a record
     that never reached the pump), `OnRing` is 0, and either `Committed ==
     Length` or `NoMore`, and the record is not already finalized. The one
     that finds it true marks it finalized and puts it on the done list,
     once; no other path does. `HcdIoDeferred` then releases the
     allocation the record owns (above), returns the wide entry, and
     completes the IRP with `Committed` and `Outcome`;
   - **the cancel walk's capacity**: `hcdCfgCancelPipe` collects the TDs it
     takes off into an array sized by records; with two TDs a record it is
     sized `2 * HCD_PIPE_XFERS` (three earlier one-TD records and a
     pipelined fourth with two TDs fit the ring together, round 3, finding
     4), with a vector that aborts exactly that.
7. **Telemetry.** The 202-word HCD header is kept as it is; new counters
   are appended after it - after the two words task 36.9 takes (202 and
   203): their count at word 204 and the 24 counters from 205
   (`XHCI_XPIPE_STATS`, `xhci_xpipe.h`) - never inside `XHCI_XSTAT`, whose
   serialized form is followed by fixed-offset UAS records (old readers
   hard-code word 110): pipelined URBs (those that had two TDs outstanding
   at once, counted when the second is published), early ends with a
   successor on the queue by kind (short, error, engine verdict),
   successors taken off by the thread, refills published in a retirement
   and held by a pause or a busy ring, URBs eligible but turned away by
   the span limits or an empty pool, promotions and the reasons a first
   mapping did not promote (a non-NULL base, the check off, a run that
   did not map), the register-free check's outcome and the pool's
   size. The gap
   is sampled per adjacent pair of a URB's TDs: retirement to the next
   publication only when the next TD was not yet on the ring at the
   retirement (the sequential path and a held refill), and **overlap** -
   the next TD already on the ring when the previous retired - counted
   apart, so the readings show how often the controller had the next TD
   ready, not a negative gap. Software-empty is stamped only when the ring
   holds nothing of the URB. Neither sees hardware starvation before
   software handles the event, which stays unmeasured (36.21).

**The event burst (the roadmap's review).** The event ring has 256 entries,
controller-wide. Today no static bound keeps a burst inside it: four
records on every pipe, two on every stream pipe, each with a TD that ends
in at least one event, a short or an error adding the trailing event of
its IOC TRB, can exceed 256 on a controller with several UAS devices, and
commands, port changes and periodic traffic come on top. The drain takes
up to 1024 events a pass and publishes the dequeue pointer every 32
consumed (`xhci_evt.c`); interrupt moderation (`XhciImodInterval250ns`,
160 by the INF, the driver's own 4000 = 1 ms when the value is missing)
spaces interrupts but bounds neither their delivery nor the DPC's
scheduling, so nothing here bounds how long events accumulate. What stage
2 adds, narrowly: **one more outstanding TD for each record holding a wide
entry, at most eight controller-wide, whose terminal events are at most
two each (a completion and a short's trailing event) - 16 events of
occupancy**. It also adds work that is not occupancy: a refill published
inside a drain makes the next TD's event possible before the drain ends,
so a drain that keeps refilling can run longer (bounded by the 1024-event
pass); and an early end with a successor adds the thread's stop and
dequeue commands and their completion events, as any cancel does. The
static claim stops at the 16; whether the ring overflows, and how long a
drain runs, under a real multi-device load is a reading (36.26: storage on
two UAS devices with audio and input running, the drain's length and its
1024-event bound hits, the event-ring-full count and the error codes read
in `XHCISNAP`), not a guarantee this section makes.

**Host vectors.** `test_xpipe` (the pure `xhci_xpipe.c`): the planner over
wide lists aligned, unaligned, scattered, with a packet size that cuts an
element, at 64 KiB, 1 MiB and 2 MiB, every TD at most 31 elements, every
TD but the last packet-aligned, the TDs together covering the URB once,
each TD's own list tiling its bytes page by page; eligibility by type,
direction, stream, chunk count, span against the entry and the grant (a
device limit above the controller's value, an oversized URB, unaligned
edges), the promotion decision and the check's comparison of two lists;
and the record machine as a pure step
function over events - in-order retirement and refill, both TDs in one
pass, a short and an error and an engine verdict with the successor
queued (the boundary sealed, `NoMore`, `Truncating`, the hand-off's three flags), the
successor already retired when the thread looks, a cancel racing the
early end, a cancel taking two stopped TDs with progress, a refill held by
a pause and by a busy ring, a Stop that is not an end, the soft retry that
later succeeds and the one that ends in an error, a failed mapping, a
failed first publication and a failed refill with an earlier TD still
running (its bytes counted), a cancel and a removal at each `MapState`,
a cancel found by the routine before its first `MapTransfer` with a
non-NULL base and a mapping that fails part-way (both returning
allocated, both released), a refusal before the routine, both orders of
a cancel and an early end, a refill's publication failure followed by its
predecessor's short and by its predecessor's error, and the finalizer
firing exactly once on every path. `test_td` keeps its
mapper-to-builder sweeps and gains the wide mapper's. `test_xfer`: two TDs
of one URB on one queue retiring in order, both in one event pass, a short
on the first with the second queued, its removal and the length the URB
reports, and the cancel walk taking three one-TD records and a two-TD
record together. `test_ring`: two 31-TRB TDs filling a 64-TRB ring exactly, and the
wrap between them across repeated refills. `test_snap`: the header's new
words appended after word 203 with the old offsets unchanged, and an older
`XHCISNAP` reading a newer snapshot, as 36.21 did. The hand-off to the
cancel service, the doorbell gates and the sibling-recovery interleaving
are driver paths no pure suite reaches; they are 36.26's legs with the
qemu bulk faults (`XhciQemuInject` 14 to 17 hex: a lost completion, a stop, an error
and a short, aimed at a pipelined URB's first TD, with a sibling
stream's cancel made to run first).

#### 11.6.1 The HAL reading (task 36.24's evidence step, 2026-10-10)

Point 2 keeps promotion off until each NT target's HAL has been read. This
is that reading. **Method: static, a disassembly reading.** Every fact here
was read from a shipped binary with `kd.exe -z` (WDK 7.1 `Debuggers`; it
maps the image and executes none of it), with Microsoft's public symbol
files where they exist. The Windows 2000 SP4 HALs have none (their
`.dbg` files are not on the symbol server), so they were read from
their exported names. None of this was observed at runtime, apart from
the one corroboration marked as such. The listings stay in git-ignored
`out\p36-hal\` on development host A and are not tracked. The binaries are
in git-ignored `tools\hal-extracted\` and are identified in the table at
the end of this section. `docs/contributing/legal-provenance.md` section 4
carries the row.

**What the HCD asks for.** `HcdDmaOpen` (`hcd_dma.c`) passes
`IoGetDmaAdapter` a `DEVICE_DESCRIPTION` with `Version` 0, `Master`,
`ScatterGather` and `Dma32BitAddresses` set, `Dma64BitAddresses` clear,
`InterfaceType` `PCIBus` and `MaximumLength` `0xFFFFFFFF`. The
description is the same on x86 and amd64. All addresses below are kd
addresses. Every x86 HAL maps at `0x80010000` and the XP x64 HAL at
`0x80010000`. kd placed Vista x64's at `0x7FF3C220000` and 7 x64's at
`0x7FF346C0000`. An RVA is the address less the base.

**The NULL-base path is the same everywhere.** In all 24 HAL files read
(22 distinct images; `hal.dll` is `halmacpi.dll` on Vista and 7 x86), the three routines a promoted URB depends on behave the same way
when `MapRegisterBase` is NULL:

- **`MapTransfer` passes the address through.** `IoMapTransfer` tests
  the base first and sends a non-NULL base to `HalpMapTransfer`. With a
  NULL base it returns the MDL page's physical address plus the byte
  offset as a full 64-bit value. The page is
  `Mdl->PageFrames[(CurrentVa - Mdl->StartVa) >> 12]`.
  - It extends the run while the next frame is the previous one plus
    one, while the run stays shorter than `*Length`, and only within
    the same 2^20-page (4 GB) region. It never merges across a 4 GB
    line.
  - It writes `*Length` only when the run is shorter than asked.
  - It reads no adapter field and keeps no per-register state. It makes
    no copy, applies no clamp other than contiguity, and never looks at
    the request's register count.
  - Mapping past the request's `NumberOfMapRegisters` therefore has no
    effect.
  - It can return an address above 4 GB if such a page exists. The HCD
    already refuses that: `hcdMapExecute` fails a run whose `HighPart`
    is not 0, and a promoted mapping uses the same test.
- **`FlushAdapterBuffers` does nothing and returns TRUE** for a bus
  master with a NULL base.
- **`FreeMapRegisters` returns at once** when the base or the adapter's
  master adapter is NULL.

Addresses (`IoMapTransfer` / its helper's 4 GB test / the Flush NULL
test / the Free NULL test):

- 2000 `hal.dll`: `80012c52` / `80012ce9` / `8001310e` / `80012856`.
- XP SP3 `halmacpi.dll`: `8001570a` (NULL test at `8001570f`) /
  `HalpMapTransferHelper` `800150d4` (`and eax,0FFF00000h` at
  `800150f9`) / `800157d4` / `800147b2` (`IoFreeMapRegisters` at
  `80014792`).
- Vista SP2 x86 `hal.dll`: `80015bb2` / `HalpMapTransferHelper`
  `80015576` / `80015c78` / `80014792`.
- XP x64: `8001d1a0` / `8001d633` (Flush) / `8001c81c` (Free).
- Vista x64: RVA `0x5284` / `0x5670` / `0x49e0`.
- 7 x64: RVA `0x53e4` / `0x5790` / `0x4b20`.

How the builds compare:

- **Windows 2000.** The eight HALs are instruction-for-instruction
  identical in these routines after normalising for their export-only
  names.
- **XP SP3.** The seven HALs are identical after the addresses are
  normalised.
- **Vista and 7, x86.** Within each OS the routines differ only in
  register allocation.
- **amd64.** Vista's and 7's `IoMapTransfer` are identical. XP x64's has
  the same logic in a different compilation.

**When the base is NULL depends on the HAL.** This is where the HALs
differ.

`HalAllocateAdapterChannel` calls the execution routine with a NULL
base, and sets the adapter's `MapRegisterBase` and register count to 0,
in either of two cases:

- the request asks for no registers; or
- the adapter's "needs map registers" byte (`+0x55` on x86, `+0x125` on
  amd64) is 0.

The routine's addresses: 2000 `hal.dll` `80012a42`, with the tests at
`80012a7d` and `80012a89`. XP SP3 `halmacpi.dll` `800155c2`, with the
test at `8001560b`. Vista x86 `80015a6a`, with the tests at `80015aad`
and `80015ab3`. XP x64 `8001cca0`, with the NULL path at `8001cd82`.

The test that refuses a request larger than the adapter's
`MapRegistersPerChannel` with `STATUS_INSUFFICIENT_RESOURCES` sits only on
the needs-registers path. A NULL-base request never consults its count.

`HalGetAdapter` sets the "needs map registers" byte. For this
description it asks one question, and the answer differs by HAL:

| HAL | No map registers (NULL base) when | Otherwise |
|---|---|---|
| 2000 SP4: `hal`, `halapic`, `halmps`, `halborg`, `halacpi`, `halaacpi`, `halmacpi` | the PAE kernel is not running: `cmp byte ptr ds:[0FFDF027Dh]` (`SharedUserData` `ProcessorFeatures[PF_PAE_ENABLED]`) at `8001c0aa` in `hal.dll`, then the `Dma64BitAddresses` test at `8001c0b7` | real registers: `min(pages + 1, 16, 32-bit map buffer >> 13)`, normally 16 |
| 2000 SP4: `halsp` (Compaq SystemPro) | always: no PAE test | - |
| XP SP3: all seven | always: `HalGetAdapter` `80027754` (`halmacpi`) zeroes the count for any scatter/gather PCI master at `80027815`-`80027828`. `HalpPhysicalMemoryMayAppearAbove4GB` is written at init (`8002d600`, `8002dcf9`) and read nowhere in the image | - |
| Vista SP2 and 7 SP1 x86: `hal` (= `halmacpi`) and `halacpi` | `HalpPhysicalMemoryMayAppearAbove4GB` is 0: Vista `hal.dll` `HalGetAdapter` `80034162` reads it at `80034219`, tests it at `80034229`, and goes to the count at `8003422d` when `Dma64BitAddresses` is clear | real registers, `min(pages + 1, 16, MasterAdapter32 size >> 13)` (`8003425f`-`8003428e`) |
| XP x64, Vista SP2 x64 and 7 SP1 x64: `hal` | `HalpPhysicalMemoryMayAppearAbove4GB` is 0: XP x64 `8005cdf9` | real registers from a local scatter pool of up to 256 pages; only pages above `0xFFFFFFFF` are bounced (`HalpDmaMapScatterTransfer`, `cmp [rbp+88h],rdx` at XP x64 `80019873`) |

Notes on the table:

- **The grant.** With no registers needed, `*NumberOfMapRegisters` is
  the adapter's `MapRegistersPerChannel`, which is
  `BYTES_TO_PAGES(MaximumLength & 0x7FFFFFFF) + 1`, that is `0x80001`. It
  is the same on every HAL read. This driver's own counter reads a
  "map grant 524289 registers" on the XP x64 QEMU guest (36.21's
  serial logs, `out\p36-mb\xp64\`), so that number is a **runtime**
  corroboration for XP x64 alone.
- **Who sets the flag.** `HalpPhysicalMemoryMayAppearAbove4GB` is
  written only during initialisation. Early writers set it to 1:
  - a loader memory descriptor ending above page `0x100000`;
  - an ACPI hot-plug memory range above 4 GB (`HalpSetupAcpiPhase0`).

  `HalpDmaFinalizeDoubleBufferingDisposition`, called from
  `HalReportResourceUsage` (Vista x86 call at `8003cfa8`), then has the
  last word, and it differs by architecture:
  - **x86 (Vista, 7).** It **overwrites** the flag with the byte at the
    kernel's exported `Mm64BitPhysicalAddress` (Vista `8003b02c`-`8003b033`,
    7 `8003d02a`-`8003d031`), which can turn an earlier 1 back to 0. When
    it changes the flag from 0 to 1 it also allocates the `0x30000` map
    buffer, bugchecking `0x5C` if that fails. So on x86 the value
    `HalGetAdapter` reads after boot is the kernel's. When the kernel sets
    `Mm64BitPhysicalAddress` was not read.
  - **amd64.** It only sets the flag, and only from the `DynamicMemory`
    value above 4.

  That `HalReportResourceUsage` runs before any PnP driver starts is the
  I/O manager's documented order, not read here.
- **2000's PAE byte.** Of the four 2000 SP4 kernels, only the PAE
  kernels write `ProcessorFeatures[PF_PAE_ENABLED]`: `ntkrnlpa.exe` at
  `00551fe9` (base `0x400000`), and `ntkrpamp.exe` by the same byte
  pattern at `00556735`, not disassembled. On 2000 the test therefore
  means "the PAE kernel is running", not "memory exists above 4 GB".
- **2000's map buffer.** The 2000 HAL allocates its 32-bit map buffer
  (`0x30000`) only under PAE. If that allocation fails the count falls
  to 0 and the base is NULL even under PAE. That is the one case where a
  2000 NULL base can meet memory above 4 GB.
- **A queued request on amd64.** On the three amd64 HALs, a request
  that `IoFreeAdapterChannel` takes off the adapter's device queue gets
  a NULL base only if it asked for no registers or the adapter has no
  master adapter. It does not test the "needs" byte (XP x64 `8001c654`,
  `8001c65a`), and these adapters always have a master adapter. Such a
  request therefore receives a real, non-NULL base from the master pool.
  - This happens only when two channel requests on one adapter overlap.
    The HCD's pump never has two outstanding (section 11.3, `hcd_dma.c`).
  - If it did happen, the gate would simply not promote, and the
    non-NULL rule frees the registers as today.

**Driver Verifier.**

- **Windows 2000 SP4.** The kernels have no DMA verification.
  - `IoGetDmaAdapter` (`004d3b49`, base `0x400000`) asks the PDO's
    stack for `BUS_INTERFACE_STANDARD` (`IRP_MN_QUERY_INTERFACE`), calls
    its `GetDmaAdapter`, and falls back to `HalDispatchTable+0x3C`. It
    calls no verifier routine.
  - It rewrites the description only when the `InterfaceType` is
    `InterfaceTypeUndefined` or `PNPBus`.
  - Neither of the XP verifier's two marker constants (`0xDEADF00D`,
    `0xACEFD00D`) occurs in `ntoskrnl.exe` or `ntkrnlmp.exe`.
  - `IoAllocateAdapterChannel` (`0041f01c`) fills in the device
    object's wait block and calls the HAL with the count unchanged.
  - So the 2000 SMP Driver Verifier leg sees the HAL's own behaviour.
- **XP SP3** (`ntoskrnl.exe`; the same constants occur equally in the
  PAE and MP kernels). With DMA verification on, `VfGetDmaAdapter`
  (`0059a082`) hooks every adapter except one whose device description
  is `PCI bus` (`VfIsPCIBus`, `0046cb65`).
  - `VfAllocateAdapterChannel` (`0059b8c3`) substitutes its own callback
    and, when double buffering is on, a map register file of the
    request's size (`ViAllocateMapRegisterFile`, `0059b922`).
  - `VfAdapterCallback` (`0059aaa2`) hands the driver either that file
    (signature `0xACEFD00D`) or, for a HAL NULL base, the token
    `0xDEADF00D` (`mov ebx,0DEADF00Dh` at `0059aace`). **It never hands
    over NULL.**
  - `VfMapTransfer` (`0059bc0e`) turns the token back into NULL before
    calling the HAL (`0059bc47`), and maps a file's pages through
    `ViMapDoubleBuffer`.
  - So under XP's DMA verification promotion never fires, and the
    register-free check fails because it requires two NULL bases. Every
    URB stays sequential, exactly stage 1. A NULL base past the
    request's count is never presented to the verifier.
- **Vista SP2 and 7 SP1, x86 and x64** (`ntoskrnl.exe`, and
  `ntkrnlpa.exe` on x86, read routine by routine; within each OS the two
  x86 kernels differ only in stack layout).
  - `IoGetDmaAdapter` rewrites nothing for `PCIBus` and leaves the
    returned count alone (Vista x86 `ntkrnlpa` `00547aec`, 7 x86
    `0058594b`).
  - `IoAllocateAdapterChannel` passes the count unchanged.
  - With DMA verification on, the verifier's thunk `VfGetDmaAdapter`
    hooks the adapter (again sparing only a `PCI bus` device).
    `ViAdapterCallback` then hands the routine one of three things, and
    **never NULL**:
    - a verifier map register file (`0xACEFD00D`);
    - the token `0xDEADF00D` (`0xFFFFFFFFDEADF00D` on amd64) in place of
      a HAL NULL base: Vista x86 `mov ebx,0DEADF00Dh` at `006e7b8f`; 7
      x64 `1405240f0`;
    - the HAL's real base.
  - These kernels also count the bytes mapped on the adapter between
    flushes against the outstanding registers times 4096
    (`INCREASE_MAPPED_TRANSFER_BYTE_COUNT`, Vista x86 `004e2c9f`). The
    count runs even on the token path. Exceeding it is
    `DRIVER_VERIFIER_DMA_VIOLATION` (0xE6) with parameter 1 = 0x0D,
    which on 7 reads `0xE6(0xD, bytes mapped, limit, 0)`.
  - So a mapping past the request's registers **would** bugcheck under
    these verifiers. It is never attempted, because the routine never
    sees NULL and promotion does not happen. The register-free check
    fails as it does on XP.
  - DMA verification is on whenever flag 0x80 is set on 7. On Vista, at
    boot, it runs without double buffering (`VfVerifyMode` is
    `0xFFFFFFFF` in the image).
- **XP SP3's verifier counts bytes the same way.** `VfMapTransfer` calls
  `INCREASE_MAPPED_TRANSFER_BYTE_COUNT` at `0059bcbb`. The same
  conclusion holds: no NULL base, no promotion, no overrun.
- **The path to the HAL.** In both `pci.sys` builds read, the PCI bus
  interface's `GetDmaAdapter` does one thing for a `PCIBus` description
  before handing it up the tree with `IoGetDmaAdapter` on the parent
  bus's device: it fills in `BusNumber`.
  - XP SP3 `PciPnpGetDmaAdapter` `00015ec6` (base `0x10000`); 7 SP1 x86
    `PciBusInterface_GetDmaAdapter` `000250ae`.
  - On x86 the chain ends at the kernel's `HalDispatchTable` fallback,
    which is the HAL's `HalGetAdapter` path above.
  - The hop at the root bus (the ACPI or HAL bus driver that answers for
    the root) was **not read**, nor was the amd64 `pci.sys`. The
    `0x80001` grant read at runtime on XP x64 is the HAL formula and
    corroborates that the description arrives unchanged there.

**Per-HAL verdict: is a NULL base pure pass-through?**

| OS, file | Version | NULL base pure pass-through | When the base is NULL for this adapter |
|---|---|---|---|
| 2000 SP4 `hal`, `halapic`, `halmps`, `halborg`, `halacpi`, `halaacpi`, `halmacpi` | 5.00.2195.6655 / .6691 | **yes** | non-PAE kernel (`ntoskrnl`, `ntkrnlmp`) always; PAE kernel only if the 32-bit map buffer could not be allocated |
| 2000 SP4 `halsp` | 5.00.2195.6655 | **yes** | always |
| XP SP3 `hal`, `halapic`, `halmps`, `halsp`, `halacpi`, `halaacpi`, `halmacpi` | 5.1.2600.5512 | **yes** | always, PAE or not (pages above 4 GB, if the kernel uses any, come back unbounced and the HCD refuses them) |
| XP x64 `hal` | 5.2.3790.3959 | **yes** | no memory, hot-plug range or `DynamicMemory` above 4 GB; and a request that was not queued behind another |
| Vista SP2 x86 `hal` (= `halmacpi`), `halacpi` | 6.0.6002.18005 | **yes** | the kernel's `Mm64BitPhysicalAddress` was 0 when the HAL finalised the flag |
| Vista SP2 x64 `hal` | 6.0.6002.18005 | **yes** | as XP x64 |
| 7 SP1 x86 `hal` (= `halmacpi`), `halacpi` | 6.1.7601.17514 | **yes** | as Vista x86 |
| 7 SP1 x64 `hal` | 6.1.7601.17514 | **yes** | as XP x64 |

**The verdict.** On every NT target HAL build read, a NULL
`MapRegisterBase` means pure pass-through. Mapping a whole URB under a one-chunk request
then touches nothing the request owns. Every HAL that has real registers
for this adapter hands the routine a non-NULL base, and so does XP's DMA
verifier, so the gate point 2 already has does not promote.
Vista's and 7's verifiers behave the same way.

The condition promotion keys on is the HAL's own signal, read at the
moment it matters, so no separate probe of the OS, PAE or memory size is
needed **for the builds read**. Two things are not established by this
reading:

- **HAL builds not in the table.** A HAL hotfix after these service
  packs is a different binary. The reading says nothing about its NULL
  semantics, and neither the gate nor point 2's 16-page check can stand
  in for that (point 2, round 4).
- **The adapter's route.** The route from the PDO to `HalGetAdapter`
  is read only in part: the kernel's `IoGetDmaAdapter` on 2000, XP,
  Vista and 7, and `pci.sys` on XP SP3 and 7 SP1 x86. The root bus's
  bus driver and the amd64 `pci.sys` were not read. The `0x80001` grant
  at runtime on XP x64 matches the HAL formula but does not identify the
  `DMA_OPERATIONS` table.

One more runtime fact narrows both, and costs nothing to test. On every
HAL read, the adapter that yields a NULL base reports exactly `0x80001`
map registers for this description (`MaximumLength` `0xFFFFFFFF`), while
an adapter with real registers reports at most 16 on x86 and at most 256
on amd64.
That grant is already recorded at start (`hc->MapRegisters`). Requiring
it as well as a NULL base keeps promotion to adapters that answer the
way the read builds answer. It is still a fingerprint, not proof of an
unread build's behaviour.

So, by scope:

- **The builds read** (2000 SP4, XP SP3, XP x64, Vista SP2 and 7 SP1,
  x86 and amd64, as in the table). The evidence supports **`XhciWideMap`
  defaulting to 1**, with the gate being a NULL base and a grant of
  `0x80001`. The register-free check stays as point 2 describes it, a
  regression check.
- **Later HAL updates on those systems.** These are not covered. Whether
  the default extends to them on lineage and the fingerprint, or
  promotion stays opt-in until each is read, is the owner's call. The
  driver does not identify the HAL file it runs on, so it cannot tell a
  hotfix HAL from the one read.

Pages above 4 GB under a NULL base (XP SP3 under PAE, 2000 with a failed
map buffer) are refused by the HCD's existing `HighPart` test, and a URB
holding one fails as it does at stage 1. Promotion adds nothing there.

Changing the default is the code task's (36.24), not this record's.
`XhciWideMap` stays at 0 until that change is made and reviewed.
(36.24 made it 1; the owner returned it to 0 on 2026-10-10 - point 2 of
section 11.6 says why. Value 1 opts in on the builds read.)

**The binaries read.** Expanded on development host A:

- 2000 SP4 from `win2ksp4.ISO` `I386\SP4.CAB`, the same files as
  `tools\win2ksp4-extracted\`;
- XP SP3 from
  `en_windows_xp_professional_with_service_pack_3_x86_cd_vl_x14-73974.iso`
  `I386\SP3.CAB` (the `I386\*.DL_` copies are byte-identical);
- XP x64 from `tools\winxp64-extracted\` (expanded from
  `Win XP SP2 VL x64.iso`, design record 11);
- Vista SP2 x86 and x64 and 7 SP1 x86 and x64 from each DVD's
  `sources\install.wim`, image 1, `Windows\System32\`.

The PDB identifiers are those `kd` loaded from the public symbol server.

| File | Bytes | SHA-256 | PDB (`<GUID><age>`) |
|---|---|---|---|
| 2000 SP4 `hal.dll` 5.00.2195.6655 | 95,456 | `7c040e3792dbbbccab173e845da9a86bb8e753f2de07f007efba625a18be0d1a` | none (export names only) |
| 2000 SP4 `halaacpi.dll` 5.00.2195.6691 | 82,176 | `7e8653957708367e0ae3dc6b915ab334a8da28f3d9ff451b53d9e59081306d7b` | none (export names only) |
| 2000 SP4 `halacpi.dll` 5.00.2195.6691 | 66,848 | `3cd165f3db9e5037cf33b059b7116e4e8a2e5b9f7c73cbe31c5ff809b1867538` | none (export names only) |
| 2000 SP4 `halapic.dll` 5.00.2195.6691 | 92,224 | `48ea42c382cad8c4a8e5ed887f8280a902875aade1fd317f6b76e9e075381c27` | none (export names only) |
| 2000 SP4 `halborg.dll` 5.00.2195.6655 | 148,960 | `213b3d97696203788328a0e0374e7978d4bcac6ed02ed309c113177e7318b524` | none (export names only) |
| 2000 SP4 `halmacpi.dll` 5.00.2195.6691 | 83,040 | `869542caf5780041f2f14d621c335d016155819d191440acc028bfe51eda8ca5` | none (export names only) |
| 2000 SP4 `halmps.dll` 5.00.2195.6691 | 93,696 | `1a5d0ad6fc56a029980a634a6b3b275ce07afc0fd7816f517bf6258f6a0ca817` | none (export names only) |
| 2000 SP4 `halsp.dll` 5.00.2195.6655 | 69,088 | `553c2f6f9833aaf565a81bea0e70f7381d5dbb9eae71c7e32b9f0b11e27840d9` | none (export names only) |
| 2000 SP4 `ntoskrnl.exe` 5.00.2195.6717 | 1,719,056 | `80a8ccf2aa9dfb05408ac35d97fdb227c29f81cb329417eb1c43451f33b6271e` | none (export names only) |
| 2000 SP4 `ntkrnlmp.exe` 5.00.2195.6717 | 1,694,976 | `4b7dfd1c2ad3213eeab5d70edab865d0a9a4d60344aa15ce3d70c04eab2dd16f` | not loaded (byte search and `norm.sh` comparison only) |
| 2000 SP4 `ntkrnlpa.exe` 5.00.2195.6717 | 1,694,080 | `d2a76d8ebcb4b74e2c5c3c9e17a20d61d20d9e81046d685b9bfbe1d002379078` | not loaded (byte search and `norm.sh` comparison only) |
| 2000 SP4 `ntkrpamp.exe` 5.00.2195.6717 | 1,715,008 | `83746ddbfca2b52f89a050d1b58613ec986cb3aa23086c7f325373df0d3d702a` | not loaded (byte search and `norm.sh` comparison only) |
| XP SP3 `hal.dll` 5.1.2600.5512 | 105,344 | `77263ad2c7b56ca964f6055a21a303bf7f031794a8ab27d251d9d2888fe5cfba` | `hal.pdb/36D1EEBD32624718A02E68DABD3593DD1` |
| XP SP3 `halaacpi.dll` 5.1.2600.5512 | 131,840 | `feba3100fedbd5197c0a67512ff11153c21adad356092bf8b8e197e66152bfcb` | `halaacpi.pdb/FCC14B4D76114FE7A8E9AEA280B60E401` |
| XP SP3 `halacpi.dll` 5.1.2600.5512 | 81,152 | `a42d3849073b8c17b06122fbbcd91c9c81bda086bb60b7965a79de3a0896ed39` | `halacpi.pdb/52475CC399844839AD8ADF647D0AF71A1` |
| XP SP3 `halapic.dll` 5.1.2600.5512 | 150,528 | `e97ee5347f46842e3bbb7f662fcb0ef9e799eccfa72633c0f73c323e1364cd20` | `halapic.pdb/A2EAA6A109F64591BCC54C567E7762391` |
| XP SP3 `halmacpi.dll` 5.1.2600.5512 | 134,400 | `034f6e289d979189553f44b7cf92e4f5c7a761248d50e22c9dcde8313654b279` | `halmacpi.pdb/9875FD697ECA4BBB8A475825F6BF885E1` |
| XP SP3 `halmps.dll` 5.1.2600.5512 | 152,576 | `e5a6b1fcd1a82161e4c9915ce2489112dacce985dbb3bd20b934ce253f7db143` | `halmps.pdb/8DBE522005AF47AB8CDE9370E08A61941` |
| XP SP3 `halsp.dll` 5.1.2600.5512 | 77,696 | `979ad1195127da404ca446ead73bb3d571e7711aad61c328733005eff1aae767` | `halsp.pdb/49865CFBB8D943FDABB4C655A700E33D1` |
| XP SP3 `ntoskrnl.exe` 5.1.2600.5512 | 2,188,928 | `6005a1a1db35481612ef76856a609954990f058bfc42dfa7b0d43402bf3f3dcf` | `ntoskrnl.pdb/47A5AC97343A4A7ABF14EFD9E99337722` |
| XP SP3 `ntkrnlmp.exe` 5.1.2600.5512 | 2,145,280 | `f086434f85441b9c0245d9f17e2d41718fac810310e244839436554e852a70b8` | not loaded (byte search and `norm.sh` comparison only) |
| XP SP3 `ntkrnlpa.exe` 5.1.2600.5512 | 2,065,792 | `7968d60617e6eef479de339c6895b4479eac79971d7cea58f9fafd66cc3b5914` | not loaded (byte search and `norm.sh` comparison only) |
| XP SP3 `ntkrpamp.exe` 5.1.2600.5512 | 2,023,936 | `745f710fa3788db04e1f4370b75fee8b25bd64877fb2e4244a3c54eef061968e` | not loaded (byte search and `norm.sh` comparison only) |
| XP x64 `hal.dll` 5.2.3790.3959 | 279,040 | `7b7671166cde8cbb8af9ede79d0c4cd38208f77a5b59e7ba661d839a870a101e` | `hal.pdb/786A0CD8B0CB4761BA7DC79BD10C215C1` |
| XP x64 `ntoskrnl.exe` 5.2.3790.3959 | 4,496,896 | `0415fa7f9319bb0c57bea7d439a5b73244bb5838510631c18222fbd081c878c0` | `ntoskrnl.pdb/7362C1B15A0F49559DE8D0BFC1564A772` |
| Vista SP2 x86 `hal.dll` 6.0.6002.18005 | 177,128 | `b6d9de353b13e61eaccdc41eb73043919b7f3cb232756233f0d732071023afe8` | `halmacpi.pdb/C215292B54434B3E95D7C087721276651` |
| Vista SP2 x86 `halacpi.dll` 6.0.6002.18005 | 140,776 | `84ea2181d16c1d971d7c719603bab2f003ad611305ba9029eddd6bbd1aea275d` | `halacpi.pdb/9D9879AD5D124F2FB0871DDCA640FE0B1` |
| Vista SP2 x86 `halmacpi.dll` 6.0.6002.18005 | 177,128 | `b6d9de353b13e61eaccdc41eb73043919b7f3cb232756233f0d732071023afe8` | (byte-identical to `hal.dll`) |
| Vista SP2 x86 `ntkrnlpa.exe` 6.0.6002.18005 | 3,601,896 | `78c2a0b97f0b50db3edddb982791658c4916b227bba9bb444fbd336f30da79bc` | `ntkrpamp.pdb/109FACEC7E244C8FAC6D191457B5C7022` |
| Vista SP2 x86 `ntoskrnl.exe` 6.0.6002.18005 | 3,549,672 | `45c9cb0604b9da7ba15e8824ff4b446064ab40ffa8412679a49ec16eaf36f7fc` | `ntkrnlmp.pdb/0F9B6FF39CAB48838BFF880777AC7D0E2` |
| Vista SP2 x64 `hal.dll` 6.0.6002.18005 | 233,448 | `e47a7345a960ff3e5858c1b76e09425b5268737788eee669ce3edfd046469cc6` | `hal.pdb/5E5C6555035349D9950043A182DF64702` |
| Vista SP2 x64 `ntoskrnl.exe` 6.0.6002.18005 | 4,699,608 | `dd8c36ac87b2db30d70b0ae9879608c52f4fc195f69abaac8032376b4786685b` | `ntkrnlmp.pdb/98C6BA00FE174241BA3FDB915995EA9C2` |
| 7 SP1 x86 `hal.dll` 6.1.7601.17514 | 194,432 | `f8b007e41157452e8c2d262f37260eb662cb2099e22acffdf9d89836a6290264` | `halmacpi.pdb/AE605D6C59454802AE1D485E0B089A571` |
| 7 SP1 x86 `halacpi.dll` 6.1.7601.17514 | 137,088 | `778d2c3afd587a2750a941cd98db4c51a7a9460e056c2cd3c432a7942e9524ce` | `halacpi.pdb/91622BAE6BA748EDACB711C581C8050C1` |
| 7 SP1 x86 `halmacpi.dll` 6.1.7601.17514 | 194,432 | `f8b007e41157452e8c2d262f37260eb662cb2099e22acffdf9d89836a6290264` | (byte-identical to `hal.dll`) |
| 7 SP1 x86 `ntkrnlpa.exe` 6.1.7601.17514 | 3,966,848 | `d44e8f8420ea50401d028f4e6de4f9e9d7dfa7de0601fba25e35c2cb581304e6` | `ntkrpamp.pdb/684DA42A30CC450F81C535B4D18944B12` |
| 7 SP1 x86 `ntoskrnl.exe` 6.1.7601.17514 | 3,911,040 | `d74e389d1a3ed4c79ffc7c0af1c092102f13e67168ab66a9296cdde62a2c6cf8` | `ntkrnlmp.pdb/00625D7D36754CBEBA4533BA9A0F3FE22` |
| 7 SP1 x64 `hal.dll` 6.1.7601.17514 | 263,040 | `9c113ae7f724369077a301ac97e4d3e8313fa965674fec401efd6e89b077bdcb` | `hal.pdb/A085D08B9C5D4BFDBA48AC285BDA03F22` |
| 7 SP1 x64 `ntoskrnl.exe` 6.1.7601.17514 | 5,563,776 | `7fba129db114e8808a2ee5ae597f66176b7eb3c4077e0b5e9a3be3f74aa2e6a6` | `ntkrnlmp.pdb/3844DBB920174967BE7AA4A2C20430FA2` |
| XP SP3 `pci.sys` 5.1.2600.5512 | 68,224 | `d4e6c360a1d2fca4d17c991b834d68bf20f5111dd06b1fab8b22984804cec269` | `pci.pdb/46927BAFB8E740B6B4BAC813675A82871` |
| 7 SP1 x86 (symbol-server copy) `pci.sys` 6.1.7601.17514 | 153,984 | `1f81315664b8cbfdd569416c0ecce4c6251f34577313a0858ab46609781303b5` | `pci.pdb/2E2A912260694615A7E97AFBA3FA934E1` |

### 11.7 Stage 3: one TD a URB on stream rings (task 36.25)

**Why, and where.** Stage 2 keeps two TDs of a URB on the ring, but each
TD is at most 31 page elements (124 KiB), so a 1 MiB URB is still 9 TDs
and 2 MiB 17 or 18, each a publication, an event and a refill under the
controller lock. Stage 3 (owner, 2026-10-09: taken whatever stage 2's
readings) puts a promoted URB on the ring as **one TD**: 256 data TRBs for
1 MiB page-aligned and 257 unaligned, 512 for 2 MiB page-aligned and 513
unaligned, on rings large enough to hold it. It applies where stage 2's
promotion applies and where the large commands arrive: **the stream rings
of UAS at SuperSpeed** (the only pipes where an IN URB is eligible, section
11.6 point 1, and where 36.22's 1 MiB and 2 MiB commands go). Every other
ring keeps the size it has: the pool rings of the controller's block, 64
TRBs (design record 04's layout, untouched - so no isochronous decoupling,
no stride or pool change, no `test_membuf` change), EP0's and the command
ring's 64, and the event ring's 256 entries.

**Scope left out, and why (orchestrator decision, owner confirmed 2026-10-10).**
Bulk-Only and High-Speed UAS keep stage 2 (OUT pipelined in 31-element
TDs, IN sequential). Their rings are pool rings in the controller's block;
a large ring for one of them is an allocation of its own per pipe with the
stream block's retirement rules repeated for it (`hcdCfgPipeFree`, the
endpoint-drop reclaim, the device's going). Their IN path could take one
large TD (a single TD has no successor for a short to strand), so this is a
deferral of scope recorded in the roadmap, not a correctness limit.

**What changes.**

1. **The ring size.** When a streams endpoint is opened
   (`hcdCfgStreamsBuild`) on a start with promotion enabled
   (`hc->WideEnabled`, section 11.6 point 2), every stream's ring is
   `XHCI_BIG_RING_TRBS` = 1024 TRBs (16 KiB, 1022 usable) instead of
   `PoolRingTrbs` (64). The bus cannot tell UAS's status pipe from its data
   pipes, so the status pipe's rings grow too, which costs memory and
   nothing else. 1024 is the smallest power of two whose usable TRBs (1022)
   hold the largest eligible URB as one TD (513 elements, eligibility's span
   bound), so a promoted URB on a big ring never needs a second TD.
2. **Placement by physical address.** A transfer ring segment must not
   cross a 64 KiB boundary (`xhci-data-structures.md`, the alignment table
   in section 1, "Transfer Ring segment", boundary 64 KB, xHCI 4.9.2 and
   section 6's introduction; `XhciSegmentUsable` already refuses one that
   does, as defence in depth). A 16 KiB segment whose physical address is a
   multiple of 16 KiB lies inside one 64 KiB block. `AllocateCommonBuffer`
   promises page alignment only, so the size is computed without the
   address and the placement with it, both in the pure
   `XhciStreamLayoutPlaced` (`xhci_stream.c`):
   - **size**, address-independent: `16 KiB + streams * 16 KiB` (for 31
     streams, 512 KiB, whole pages). The Primary Stream Context Array sits
     at the block's start as today, 16-byte entries, at most 32 entries =
     512 bytes, inside the block's first page; the first ring starts at the
     first 16 KiB-aligned physical address at or past the array's end,
     which for a page-aligned base is 4, 8, 12 or 16 KiB in, so the prefix
     is at most 16 KiB.
   - **placement**, on the address granted: `RingBase = align16K(paLow +
     ArrayBytes) - paLow`, ring `i` at `RingBase + (i - 1) * 16 KiB`; refused
     (the fallback, point 6) when `paLow + size - 1` wraps 32 bits or the
     block's high part is not 0.
   `test_stream` sweeps every page-aligned base across a 64 KiB window and
   past 4 GiB's edge, 4 to 32 entries, 64 and 1024 TRBs: no ring crosses a
   64 KiB line, rings overlap neither each other nor the array, all lies
   inside the allocation, and a 64-TRB layout is today's byte for byte.
3. **One TD a URB, its list the wide list itself.** On a big ring a
   promoted URB is published as exactly one TD covering the whole URB, and
   the TD's scatter/gather list is the entry's wide list: the entry's
   array becomes a `USBPORT_SCATTER_GATHER_LIST` with room for 513
   elements (its header and `More`), whose offsets the wide mapper already
   writes from 0, so nothing is copied or trimmed and no 31-element slot
   list is used. The planner (`XhciXpipePlan`, cap 31) is not used on a
   big ring; on a 64-TRB ring - a start without the big block, or the
   fallback - stage 2 runs unchanged. The pipe records which it has
   (`pipe->BigRing`, set when its ring is built, cleared with it). With one
   TD there is no successor: no refill, no truncation, no hand-off; an
   early end is the URB's end. The machine (`XhciXpipeRec*`) runs as for
   any one-TD URB.

   **Suspension.** The wide paths follow the sequential ones on a
   suspended controller: they have no rule of their own. Task 36.28's
   attempt at one (nothing published while suspended, the settle draining
   every client request at the controller's D3) was reverted by the
   owner's decision on branch `p36-susp` (`9f20cfd`), since it did not
   change the Windows ME hang it was written for; the integration branch
   `p36-int3` took the revert over stage 2's copies of that rule as well
   (`hcdWidePump`, `hcdWidePublishLocked`, `hcdWideMapped`, and the
   pipelined-record clause of the settle that no longer exists).
4. **The engine's cap and scratch.** `XhciXferBuildNormal` refuses more
   than `XHCI_XFER_MAX_DATA_TRBS` (32) elements today, before reading any,
   and again per TRB emitted. A normal request gains `MaxDataTrbs`, the cap
   for both checks (0 meaning 32, so every existing caller - which zeroes
   the request, or must: the host suites' `normal_request_init` and
   `test_td`'s builders are changed to zero it - keeps today's behaviour);
   the output capacity stays the caller's scratch count, apart from the
   cap, since one element can still split at 64 KiB (none of the HCD's
   page elements does). Control transfers keep 32. Stage 3's publication
   passes `XHCI_XPIPE_WIDE_ELEMENTS` (513) and a scratch of 513 + 2 TRBs
   (8,240 bytes) that lives in the wide pool's allocation, beside the
   entries, used only under the controller lock (every publication holds
   it); a pool kept by a fail-closed stop keeps its scratch with it, and a
   publication with no current pool fails before the ring as any refusal.
   The SG walk (`xhciXferEmitSgList`) looks for each next element first at
   the index after the last one taken, then scans as today, so an ordered
   list costs one step an element (513 elements would otherwise be about
   130,000 comparisons under the lock); its result is unchanged for every
   list, which `test_xfer` pins on ordered, reversed and shuffled lists.
5. **TD Size, Link TRBs and the residual.** TD Size saturates at 31
   packets remaining and is 0 on the last TRB (4.11.2.4), as the builder
   does today (`test_build_td_size_saturates`); a TD that crosses the Link
   TRB carries Chain on the Link as today's publication sets it. A short
   packet's length is the inclusive prefix of the TD's TRB lengths up to
   the named TRB less its residual (`XhciRingSumTrbLengths` walks the ring
   with no TD-size bound), the tail never recomputing it. A halting error
   on a big TD retires it as today; Stop and Set TR Dequeue address the
   ring by its own geometry (`ring->Trbs`), and stream recovery still moves
   only the failed stream (section 11.6, point 6).
6. **The fallback.** If the big block cannot be allocated or placed (point
   2), the endpoint opens with today's 64-TRB layout and stage 2's TDs,
   counted (`BigRefused`); if that allocation fails too, the open fails as
   today. Nothing else changes. On Windows 98 and ME promotion is never
   enabled, so their stream rings, their allocations and their common
   buffer use are today's.
7. **Retirement.** The block keeps today's lifetime rules unchanged: it is
   the stream block, retired to the device while its endpoint is not yet
   Disabled (`StreamsRetired`, `hcdCfgRetiredReclaim`, `HcdCfgDeviceGone`);
   `HCD_STREAMS` records the size and ring size it was built with, and
   `hcdCfgStreamsFree` frees that size.
8. **The event burst.** Section 11.6's 16 is the occupancy stage 2 *added*
   (a second TD per wide entry), not the entries' total: eight entries with
   two TDs each and up to two terminal events a TD is 32 events those TDs
   can hold, and a halting error need not add a trailing event. Stage 3's
   one TD a URB removes the second TD on big rings, so its addition over
   stage 1 is none; the thread's stop and dequeue commands on a cancel are
   as before. Fewer events per byte is credible; a lower worst-case drain
   or lock hold is not claimed - a 513-TRB build and its residual walks are
   longer - and is a reading (36.26).
9. **Telemetry.** Eight counters appended to `XHCI_XPIPE_STATS` after its
   24 words: big blocks built, big blocks refused, TDs published on big
   rings, the largest TD published (data TRBs), and the counts of TDs of
   exactly 256, 257, 512 and 513 data TRBs (four words). The structure
   grows to 32 words, the count at header word 204 reads 32, the new words
   are 229 to 236 and the header becomes 237 words; nothing before moves,
   and the serializer's asserts, `test_snap` and `XHCISNAP` (which reads a
   24-word block from an older driver as before) change together. The exact counts are what
   36.26's geometry legs read to show a 257- or 513-TRB TD reached the
   ring, apart from a merely large SRB.

**Host vectors.**
- `test_stream`: `XhciStreamLayoutPlaced` as in point 2.
- `test_xfer`: `MaxDataTrbs` 0 (32), 513 accepted; three separate guards
  - a request cap of 514 refused before the list is touched, a valid cap
  with an element count above it refused with a readable header and
  inaccessible elements, and an element count within the cap whose one
  element crosses 64 KiB and expands past it (cap 1, two TRBs, scratch
  enough) refused by the per-TRB guard; a 513-TRB TD published across the Link TRB on a 1024-TRB
  ring with both cycle values; a short on its 400th TRB with the tail, the
  same with the tail absent until an empty-drain settle, and swept by a
  later TD's event (the first measured length kept); its removal by the
  cancel walk and by a drain; Set TR Dequeue past it on a big ring with
  both cycles; the SG walk's hint on ordered, reversed and shuffled lists.
- `test_td`: the wide mapper's list over 1 MiB and 2 MiB at every page
  offset and three layouts, handed as the TD's list to the engine at cap
  513: one TRB an element, 256/257/512/513 as the alignment gives.
- `test_xpipe`: the record machine over a one-TD URB - full, short, error,
  cancel at each map state, drain - firing once with no refill and no
  truncation.
- `test_snap`: the appended words. `test_iso`, `test_membuf` and
  `test_ring` pass unchanged (no pool or isochronous change).

**What the legs show (36.26).** On XP x64 and 7 x64 with promotion on, 1
MiB and 2 MiB SRBs at aligned and unaligned buffers produce TDs of exactly
256, 257, 512 and 513 TRBs (the exact counters), apart from the SRB sizes;
the fallback forced by the qemu flavour's `XhciQemuBigRefuse` and the
64-TRB behaviour read; a system standby or hibernate with a big TD
outstanding, read as the sequential path's (11.7 point 3: the wide paths
have no suspend rule of their own since the 36.28 settle was reverted),
and a pipe paused and resumed while the controller runs (held, then
published once); bulk faults 14 to 17 aimed at a big TD (the
injection's own note says which TD and its TRB count); a stream's recovery
with a sibling stream keeping its progress; streams opened and closed
across repeated starts and hot-plugs, every block granted or refused into
the fallback and the pool count back at its start value. **Low memory**:
on NT, the big blocks in a guest with little memory and promotion
enabled (XP x86 with 256 MiB), recording `Check`, `Enabled` and the big
blocks built and refused; and Windows 2000 SMP under Driver Verifier's
low-resources simulation, which has no DMA verification wrapper (11.6.1),
so promotion may be enabled there and the allocations may fail - the leg
records the same words and the configuration rather than assuming
either; on Windows 98 SE, the roadmap's low-memory machine
(a 64 MiB guest) shows its allocations unchanged by this stage - a
regression leg, since nothing here runs there. The full 36.26
compatibility and multi-device load matrix applies.

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
| `DeviceIsHub` for hubs inside the bus, the driver key returned for a split composite device, and the `ControllerFlavor` value the HCD reports to Vista and Windows 7 `usbui.dll` | 8 | 26-A.8; the hubs by 33.4 (10.11), the composite's driver key by 36.43 (10.12): the parent's |
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
  key, read at each controller start (`hcd_ctl.c`), written by no INF
  until task 34.1 and at 0 by every INF since, under the don't-overwrite
  flag.
  `XhciPipeFastMode`: 1, 2 and 3 are themselves, anything else - absent, 0,
  a value of another type, 4 and up - is off. (Since task 36.19, 4 is
  itself too, section 13.7, and 5 and up are off.) Logged as `fastpoll.value`
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
  input. The client is told the `bInterval` it gave, as before; a composite
  device's function is mapped from the `bInterval` its caller's descriptor
  gives too (section 10.9, 2026-10-05), except at a later
  `SELECT_INTERFACE`, which reads the device's own. The cost
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

### 13.7 Mode 4: the polling rate a client chooses (task 36.19, 2026-10-09)

The request is GitHub issue 17's item 2 (SweetLow, testing `2.x`): let a
client pick 2000, 4000 or 8000 Hz for one endpoint directly, rather than
through modes 2 and 3's reuse of the "31 Hz" and "62 Hz" settings, which
also speed up every device on a root port that declares 16 to 63 ms. The
owner took it into Phase 36 on 2026-10-09 as an opt-in mode of the same
value. Nothing here was read out of a binary; it adds no
`legal-provenance.md` row.

**The value.** `XhciFastPollFsLs` 4 (`XHCI_PIPE_FAST_CODES`).
`XhciPipeFastMode` passes 1 to 4 as themselves; 5 and up, 0, absent and a
value of another type stay off, as before.

**The codes.** Under mode 4, an interrupt endpoint whose `bInterval` in the
descriptor the HCD is handed is 128, 129 or 130 is programmed at Interval
2, 1 or 0: 500, 250 or 125 us, 2000, 4000 or 8000 Hz. Every other
`bInterval` is read by Table 6-12, exactly as with the value off: mode 4
does not do modes 2 and 3's mapping of Intervals 7 and 8, and modes 2 and 3
do not honour the codes. The two conventions never meet on one controller,
since the value holds one mode.

**Why an opt-in.** 128 to 255 ms are legal Full- and Low-Speed values
(Table 6-12's 1-255 row), so a device may itself declare a code: under mode
4 such a device is polled every 125 to 500 us instead of every 128 ms. And
a client that sends a code with the mode off gets what the byte legally
means, 128 ms at Full Speed, so the procedure is the mode first, then the
client (hidusbf) set to the codes. The release notes say both.

**The ceiling.** 125 us, Interval 0, at any speed. 131 and above are not
codes and read by Table 6-12; no input reaches below Interval 0.

**Scope.**
- Full and Low Speed on a root port only (Route String 0), as modes 2 and
  3: behind a hub a transaction translator or a Full-Speed hub carries the
  device's periodic traffic one frame at a time.
- High Speed and SuperSpeed (and SuperSpeedPlus) anywhere, root port or
  behind a hub, since their Interval range already reaches 125 us (Table
  6-12's HS and SS rows: Interval 0 is a valid value there). A code at
  those speeds is otherwise an out-of-range `bInterval` that the driver's
  existing clamp to Table 6-12's range takes to 16 (Interval 15, 4.096 s); the code replaces that.
- Interrupt endpoints only: never isochronous (whose interval sets its data
  rate), never bulk, never a hub's own status endpoint (`HcdCfgHubOpen`
  never asks).

**Keyed on the descriptor's byte** (`XHCI_PIPE_EP.BInterval`, as the
caller's descriptor gave it), not on the Interval Table 6-12 derives from
it, which for a code is 10 at Full and Low Speed (footnote 113's rounding of
128 to 130 ms) and 15 at High Speed and SuperSpeed after the clamp, values
an ordinary device also produces.

**Its place before the interval cap** (record 17 section 4.7). Modes 2 and 3
run after the cap and only when the cap left the Interval as it was, so on
an AMD controller (cap on) a capped endpoint is never fast-polled. Mode 4
runs first: a code's Interval (2 or less) is then never above the cap's 8,
so the cap leaves it, and an AMD controller does not silence the code. An
endpoint with no code goes through the cap as it does today.

**The fallback.** The endpoint keeps two values apart: `SpecInterval`, the
Table 6-12 Interval its descriptor gives (for the record), and
`FallbackInterval`, the Interval a refusal returns it to. Modes 2 and 3 set
the fallback to the Interval they replaced, which is today's revert
exactly. Mode 4 sets it to 3 (1 ms), not to Table 6-12's reading of the
code: that would leave a device that asked for 8000 Hz polled every 128 ms
(or 4.096 s at High Speed). 3 is inside every speed's interrupt range
(Full and Low Speed 3-10, High Speed and SuperSpeed 0-15) and at or below
the cap. The refusal path is section 13.5's, unchanged:
`hcdCfgConfigureAdd` reverts every pipe the mode changed when Configure
Endpoint fails with Parameter, Bandwidth or Secondary Bandwidth Error
(`XhciPipeFastRetry`) and issues it once more, counted in
`fastpoll.fallbacks` with a `fastpoll.fallback` record. An endpoint opened
fast is `fastpoll.opened` and a `fastpoll.open` record (slot, DCI,
`SpecInterval`, the Interval programmed): under mode 4 a Full-Speed code
reads `0A00` to `0A02`, a High-Speed or SuperSpeed one `0F00` to `0F02`.

**The replays.** `RESET_PORT` and the endpoint recycle rebuild the
Endpoint Context from the stored `pipe->Ep` (`hcdCfgConfigureAdd` with
`opening` 0), so they keep the chosen Interval, the fast flag and the
fallback without reading a descriptor again; a pipe already reverted stays
at its fallback.

**SuperSpeed composite functions.** A composite function's select takes
each endpoint from the device's own configuration (`dev->Selected`) but the
`bInterval` from the caller's matched endpoint (section 10.9,
`XhciPipeCallerInterval`). Today `hcdCfgFunctionEndpoint` (`hcd_cfg.c`)
does that only below SuperSpeed, by sending a seven-byte copy with the
caller's byte through the USB 2.0 parser, which cannot carry a companion;
at SuperSpeed and SuperSpeedPlus the caller's byte is ignored, so a
function's code would be lost there. Changed, and moved into a pure
helper the host suite runs, `XhciPipeFunctionEndpoint` (`xhci_pipe.c`),
which `hcdCfgFunctionEndpoint` calls: at those speeds the endpoint and its
companions are parsed from `dev->Selected` as now
(`XhciPipeEndpointParamsAt`, keeping Max Burst, Mult and Max ESIT
Payload), and then, when the caller's matched byte differs from the
device's, `XhciPipeRebaseInterval` replaces `BInterval` with it and derives
the ordinary Interval (and `IntervalClamped`, `SpecInterval`) from it by
the same Table 6-12 rule the parser uses for that speed and type; mode 4 is
then applied through `hcdCfgFastPoll` like every other endpoint. The
Interval is never written directly. Below SuperSpeed the seven-byte path
stays as it is. A bulk endpoint's Interval stays 0 whatever the byte.

**A composite function's later `SELECT_INTERFACE`** rebuilds from the
device's own configuration, and so loses a code, as it loses modes 2 and
3's mapping today (section 13.5). Decided (owner, 2026-10-09): accepted and
documented here and in the release notes; HID class drivers do not reselect
an interface. Revisited if a report shows it.

**What the client is told** is unchanged: the `bInterval` it gave, in
`USBD_PIPE_INFORMATION.Interval`.

**Host vectors** (`test\test_pipe.c`, `test_fast_poll` and its
neighbours): 4 accepted and 5 off; each code at Low, Full and High Speed
and SuperSpeed; 127, 131 and 255 untouched; Full Speed behind a hub
untouched and High Speed and SuperSpeed behind one applied; isochronous and
bulk untouched; modes 2 and 3 ignoring the codes and mode 4 ignoring
Intervals 7 and 8; no double application; the revert landing at 3 under
mode 4 and at the replaced Interval under modes 2 and 3; mode 4 before the
cap in `test_tol.c`'s ordering vector; `XhciPipeRebaseInterval` agreeing
with the parser at every speed and type; and an integration vector with a
split SuperSpeed device, a nontrivial companion, and a device byte that
differs from the caller's code.

**Legs.** hidusbf set to the codes on 98 SE and 7 in QEMU, read in the
`fastpoll.*` records (QEMU paces Interval 0 at 125 us, section 13.4, and
can show no refusal); mode 4 with an unpatched device reading as the
release before it. The owner's bench reads a real controller's acceptance.

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
