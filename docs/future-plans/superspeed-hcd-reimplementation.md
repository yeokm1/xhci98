# General SuperSpeed support: the Option B host controller driver

Status: not planned. This page records what general USB 3.x SuperSpeed
support would take, so that the question does not have to be re-derived each
time it is asked. It was the "What SuperSpeed Support Would Require" section
of `docs/usb-xhci-info/xhci-programming.md` until 2026-09-07; that section
now keeps only the refusal the driver makes today and points here. Nothing
on this page has a phase or task id, and nothing on it has been started.

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
[superspeed-storage-behind-a-switch.md](superspeed-storage-behind-a-switch.md),
and its section 2 says where the line between the two lies. Should that
proposal ever be built, this page still describes what it does not cover:
SuperSpeed hubs, isochronous endpoints, link power management, and the
bandwidth model.

## Sources

- `docs/usb-xhci-info/xhci-programming.md`, "What SuperSpeed Support Would
  Require": the refusal and the USB4 paragraph that stayed behind.
- `docs/usb-xhci-info/win98-wdm.md`, "USB Stack Architecture and the
  Integration Decision": the stack survey, Option A and the Option B
  fallback.
- `docs/contributing/architecture.md`, "Fallback: Option B (monolithic HCD)".
- `AGENTS.md`, "Port Strategy (USB 2.0 vs USB 3.x)".
