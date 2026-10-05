/*
 * xhci_psw.h - the Intel EHCI-to-xHCI port switchover, its pure half
 * (roadmap-hcd.md task 34.4).
 *
 * On the 7-, 8- and 9-series PCH (Panther Point, Lynx Point, Wildcat Point,
 * and the -LP parts of the last two), and on Wellsburg (C610/X99), each
 * switchable connector is wired to
 * both the EHCI and the xHCI controller, and four registers in the xHCI's
 * own PCI configuration space choose which one has it: USB3_PSSEN turns on
 * the connector's SuperSpeed terminations under xHCI, XUSB2PR routes its USB
 * 2.0 pair to xHCI, and USB3PRM and XUSB2PRM are the masks of what firmware
 * lets the OS change. Firmware set to "Auto", or offering no setting, leaves
 * every switchable connector on EHCI, so the xHCI starts with nothing on
 * those ports.
 *
 * The sequence is Linux's usb_enable_intel_xhci_ports (pci-quirks.c):
 * USB3_PSSEN = USB3PRM first, so a SuperSpeed device connects at SuperSpeed
 * rather than at High Speed, then XUSB2PR = XUSB2PRM, each read back. Back
 * to EHCI is its usb_disable_xhci_ports: both written 0, USB3_PSSEN first.
 *
 * The gate is the device-id list, not Linux's rule (any Intel xHCI with an
 * Intel EHCI on the bus), which needs a PCI scan the HCD does not have
 * (owner, 2026-10-05). Nor every Intel xHCI: from the 100-series on there is
 * no EHCI, and what these offsets do there is unread. The one board Linux exempts by subsystem id, a Sony
 * VAIO T-series that "is not capable of switching ports", is exempted here
 * too. The switch is the XhciIntelPortSwitch value: only an explicit 0 turns
 * it off; absent, of another type or any other number, it is on (owner,
 * 2026-10-05).
 *
 * The executor is hcd_ctl.c: it supplies the two configuration-space
 * accessors and logs what comes back. Nothing here touches a register,
 * takes a lock or calls a kernel service.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_PSW_H
#define XHCI_PSW_H

#include "xhci_compat.h"

/* PCI configuration offsets, in the xHCI function's own space. */
#define XHCI_PSW_PCI_SUBSYSTEM  0x2CUL  /* SVID 15:0, SSID 31:16            */
#define XHCI_PSW_XUSB2PR        0xD0UL  /* USB 2.0 pairs routed to xHCI     */
#define XHCI_PSW_XUSB2PRM       0xD4UL  /* which of them the OS may route   */
#define XHCI_PSW_USB3_PSSEN     0xD8UL  /* SuperSpeed terminations enabled  */
#define XHCI_PSW_USB3PRM        0xDCUL  /* which of them the OS may enable  */

/*
 * What a route or release stopped at (XHCI_PSW_STATE.Step); 0 when every
 * access was made. A route stops at a failed mask read or a failed write,
 * so the USB 2.0 pairs never move to xHCI without the SuperSpeed
 * terminations; a release attempts both writes whatever the first did.
 */
#define XHCI_PSW_DONE               0UL
#define XHCI_PSW_STEP_USB3PRM       1UL /* mask unreadable or all-ones      */
#define XHCI_PSW_STEP_PSSEN_WRITE   2UL
#define XHCI_PSW_STEP_PSSEN_READ    3UL /* read-back failed; route went on  */
#define XHCI_PSW_STEP_XUSB2PRM      4UL /* mask unreadable or all-ones      */
#define XHCI_PSW_STEP_XUSB2PR_WRITE 5UL
#define XHCI_PSW_STEP_XUSB2PR_READ  6UL /* read-back failed                 */
#define XHCI_PSW_STEP_BAD_PARAM     7UL

/* One dword of configuration space; return 1 when the access was made. */
typedef ULONG (*XHCI_PSW_READ)(PVOID context, ULONG offset, PULONG value);
typedef ULONG (*XHCI_PSW_WRITE)(PVOID context, ULONG offset, ULONG value);

typedef struct _XHCI_PSW_IO {
    XHCI_PSW_READ Read;
    XHCI_PSW_WRITE Write;
    PVOID Context;
} XHCI_PSW_IO, *PXHCI_PSW_IO;

/* What a route or release read. A field not reached is 0xFFFFFFFF. */
typedef struct _XHCI_PSW_STATE {
    ULONG Usb3Mask;     /* USB3PRM                                          */
    ULONG Usb3Now;      /* USB3_PSSEN read back                             */
    ULONG Usb2Mask;     /* XUSB2PRM                                         */
    ULONG Usb2Now;      /* XUSB2PR read back                                */
    ULONG Step;         /* XHCI_PSW_DONE or the XHCI_PSW_STEP_* it stopped  */
} XHCI_PSW_STATE, *PXHCI_PSW_STATE;

/* 1 when PCI offset 0's vendor/device dword names a controller with the
 * mux: Intel 1E31, 8C31, 9C31, 8CB1, 9CB1 or 8D31. */
ULONG XhciPswGate(ULONG vendorDevice);

/* 1 when PCI offset 0x2C's subsystem dword names a board that cannot
 * switch (Sony 104D:90A8, Linux's one exemption). */
ULONG XhciPswBoardRefused(ULONG subsystem);

/* The XhciIntelPortSwitch rule: 0 only when the value was found and is 0. */
ULONG XhciPswEnabled(ULONG found, ULONG value);

/* Every switchable connector to xHCI, SuperSpeed terminations first.
 * Returns st->Step. */
ULONG XhciPswRoute(const XHCI_PSW_IO *io, PXHCI_PSW_STATE st);

/* Every connector back to EHCI: USB3_PSSEN then XUSB2PR written 0, each
 * read back. Returns st->Step, the first access that failed. */
ULONG XhciPswRelease(const XHCI_PSW_IO *io, PXHCI_PSW_STATE st);

#endif /* XHCI_PSW_H */
