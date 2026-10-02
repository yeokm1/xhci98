# General SuperSpeed support: the successor host controller driver `xhci98h.sys`

Design record 13, for roadmap Phases 25 onward
(`docs/contributing/roadmap-hcd.md`). It was
`docs/future-plans/superspeed-hcd-reimplementation.md` from 2026-09-07, when
it left the "What SuperSpeed Support Would Require" section of
`docs/usb-xhci-info/xhci-programming.md`, until 2026-10-02, when the owner
froze the miniport at `1.2.0.0` and scheduled its successor; it moved here
that day with the proposal's text intact, and roadmap task 25.1 grows it into
the record proper - the object model, the enumeration state machine, the
per-target function-driver contract, the Windows 98 export evidence, the
`USBUSER` door, the source layout and the package. Until 25.1 runs, sections
1 to 3 are the analysis the proposal carried, still true of the frozen
miniport, and section 4 holds the decisions the owner has taken. Nothing in
`src/` has been changed for it yet.

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
- **The handover is a second package.** `xhci98h.sys`, with its own INFs and
  package, chosen at install time against the same PCI class id; never a
  registry switch inside one binary, because the miniport's import of
  `USBPORT_RegisterUSBPortDriver` is a load-time gate and a combined binary
  could not load on a stock Windows 98 SE. The successor starts on the two
  primary targets in QEMU (Phase 26) and reaches the other eight in Phase 28.
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
  an own storage driver, on this reading of NUSB 3.6's package: a drive letter
  on 9x comes from the IOS layer, and `usbntmap.inf` binds `usbstor.sys`'s
  disk objects to `*IOS` through the NTMAP port driver, `NTMAPHLP.PDR` with
  `NTMAP.SYS` and `USBNTMAP.SYS`, Microsoft files of the 98 SE hotfix line
  (4.10.0.2223 to 2227, hotfixes 242975 and 267304) that this project does
  not redistribute. An own Bulk-Only driver would still need NTMAP above it;
  an own IOS port driver is a VxD project with no use on any other target.
  So `usbstor.sys` is the Bulk-Only driver on every target, the UAS driver of
  Phase 31 takes the NTMAP route on Windows 98 SE too, and on a stock
  Windows 98 SE no mass storage works, UAS included - a statement for the
  release notes, not a limitation to fix. Whether the 98 SE CD carries the
  NTMAP files is task 31-0's to read.
- **Pool and the HCD's own DMA buffers are allowed**, by import-allowlist
  rows with Windows 98 export evidence (task 25.3). The miniport's "allocate
  no pool" rule stands for `src/`.
- **The Advanced tab is kept**: the HCD answers the `USBUSER` request set
  `usbui.dll` sends, and `XHCISNAP` reaches the HCD through the same door
  (task 25.4).
- **USB Audio 2.0 is a separate repository**, a class driver of generic
  design so that it also serves machines on EHCI and vendor stacks; it needs
  nothing from this stack.

What 25.1 adds below this section, when it runs: the object model, the
enumeration state machine, the lock model against design record 05, the
per-target function-driver contract (25.2), the Windows 98 export evidence
(25.3), the door (25.4), the harness plan (25.5), the hub and composite
design (25.6) and the transfer-buffer policy (25.7).

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
