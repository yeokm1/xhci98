/*
 * xhci_psw.h - the Intel EHCI-to-xHCI port switchover, its pure half
 * (roadmap-hcd.md task 34.3).
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
 * to EHCI is its usb_disable_xhci_ports: written 0, USB3_PSSEN first -
 * here only the registers a route of this lifetime wrote (task 35.5).
 *
 * The gate is the device-id list, not Linux's rule (any Intel xHCI with an
 * Intel EHCI on the bus), which needs a PCI scan the HCD does not have
 * (owner, 2026-10-05). Nor every Intel xHCI: from the 100-series on there is
 * no EHCI, and what these offsets do there is unread. The one board Linux exempts by subsystem id, a Sony
 * VAIO T-series that "is not capable of switching ports", is exempted here
 * too. The switch is the XhciIntelPortSwitch value: only an explicit 0 turns
 * it off; absent, of another type or any other number, it is on (owner,
 * 2026-10-05). An exact 2 also routes on an Intel controller the list does
 * not name, at the user's own risk (task 35.5, design record 16 section 4a).
 *
 * The lifetime (record 16 section 7a) is here too: the start's decision,
 * and which of the two routing registers a route of this started lifetime
 * wrote, so a release writes those and no other.
 *
 * The executor is hcd_ctl.c: it supplies the configuration-space accessors
 * and the value reader, and logs what comes back. Nothing here touches a
 * register, takes a lock or calls a kernel service.
 *
 * DDK-free: part of the pure core. C89. IRQL: any for the functions that
 * take no XHCI_PSW_IO; those that do run at their callbacks' IRQL, which
 * with hcd_ctl.c's is PASSIVE_LEVEL under no spin lock.
 */

#ifndef XHCI_PSW_H
#define XHCI_PSW_H

#include "xhci_compat.h"

/* PCI configuration offsets, in the xHCI function's own space. */
#define XHCI_PSW_PCI_ID         0x00UL  /* vendor 15:0, device 31:16        */
#define XHCI_PSW_PCI_SUBSYSTEM  0x2CUL  /* SVID 15:0, SSID 31:16            */
#define XHCI_PSW_XUSB2PR        0xD0UL  /* USB 2.0 pairs routed to xHCI     */
#define XHCI_PSW_XUSB2PRM       0xD4UL  /* which of them the OS may route   */
#define XHCI_PSW_USB3_PSSEN     0xD8UL  /* SuperSpeed terminations enabled  */
#define XHCI_PSW_USB3PRM        0xDCUL  /* which of them the OS may enable  */

/*
 * What a route or release stopped at (XHCI_PSW_STATE.Step); 0 when every
 * access was made. A route stops at a failed mask read or a failed write,
 * so the USB 2.0 pairs never move to xHCI without the SuperSpeed
 * terminations; a release attempts each write its set selects, whatever
 * an earlier one did.
 */
#define XHCI_PSW_DONE               0UL
#define XHCI_PSW_STEP_USB3PRM       1UL /* mask unreadable or all-ones      */
#define XHCI_PSW_STEP_PSSEN_WRITE   2UL
#define XHCI_PSW_STEP_PSSEN_READ    3UL /* read-back failed; route went on  */
#define XHCI_PSW_STEP_XUSB2PRM      4UL /* mask unreadable or all-ones      */
#define XHCI_PSW_STEP_XUSB2PR_WRITE 5UL
#define XHCI_PSW_STEP_XUSB2PR_READ  6UL /* read-back failed                 */
#define XHCI_PSW_STEP_BAD_PARAM     7UL

/* Which routing registers a route's write was accepted on (Written). */
#define XHCI_PSW_WROTE_PSSEN        0x1UL   /* USB3_PSSEN, D8h              */
#define XHCI_PSW_WROTE_XUSB2PR      0x2UL   /* XUSB2PR, D0h                 */

/* XhciPswMode. */
#define XHCI_PSW_MODE_OFF           0UL
#define XHCI_PSW_MODE_GATED         1UL     /* a listed controller          */
#define XHCI_PSW_MODE_BYPASS        2UL     /* unlisted Intel, value 2      */

/* What ended the start's decision (XHCI_PSW_LIFE.Decision). */
#define XHCI_PSW_DECIDE_NONE        0UL     /* not run, or a bad parameter  */
#define XHCI_PSW_DECIDE_ID_UNREAD   1UL     /* PCI offset 0 unreadable      */
#define XHCI_PSW_DECIDE_NOT_INTEL   2UL     /* no value read                */
#define XHCI_PSW_DECIDE_OFF         3UL     /* XhciPswMode said off         */
#define XHCI_PSW_DECIDE_BOARD_UNREAD 4UL    /* subsystem id unreadable      */
#define XHCI_PSW_DECIDE_BOARD_REFUSED 5UL   /* the Sony board               */
#define XHCI_PSW_DECIDE_ON          6UL     /* routed                       */

/* One dword of configuration space; return 1 when the access was made. */
typedef ULONG (*XHCI_PSW_READ)(PVOID context, ULONG offset, PULONG value);
typedef ULONG (*XHCI_PSW_WRITE)(PVOID context, ULONG offset, ULONG value);
/* XhciIntelPortSwitch; return 1 when it was found as a DWORD. */
typedef ULONG (*XHCI_PSW_VALUE)(PVOID context, PULONG value);

typedef struct _XHCI_PSW_IO {
    XHCI_PSW_READ Read;
    XHCI_PSW_WRITE Write;
    XHCI_PSW_VALUE Value;   /* the start's decision only                    */
    PVOID Context;
} XHCI_PSW_IO, *PXHCI_PSW_IO;

/* What a route or release read. A field not reached is 0xFFFFFFFF. */
typedef struct _XHCI_PSW_STATE {
    ULONG Usb3Mask;     /* USB3PRM                                          */
    ULONG Usb3Now;      /* USB3_PSSEN read back                             */
    ULONG Usb2Mask;     /* XUSB2PRM                                         */
    ULONG Usb2Now;      /* XUSB2PR read back                                */
    ULONG Step;         /* XHCI_PSW_DONE or the XHCI_PSW_STEP_* it stopped  */
    ULONG Written;      /* XHCI_PSW_WROTE_* this route's writes accepted    */
} XHCI_PSW_STATE, *PXHCI_PSW_STATE;

/*
 * One started lifetime (record 16 section 7a). Written accumulates every
 * route's accepted writes and is emptied only with On, by a start's
 * decision and by XhciPswLifeEnd. The rest is what the decision read, for
 * the log; Id and Subsystem are 0xFFFFFFFF where not read.
 */
typedef struct _XHCI_PSW_LIFE {
    ULONG On;
    ULONG Mode;         /* XHCI_PSW_MODE_*                                  */
    ULONG Written;      /* XHCI_PSW_WROTE_*                                 */
    ULONG Decision;     /* XHCI_PSW_DECIDE_*                                */
    ULONG Id;           /* PCI offset 0                                     */
    ULONG Found;        /* XhciIntelPortSwitch found as a DWORD             */
    ULONG Value;        /* its value, 0 when not found                      */
    ULONG Subsystem;    /* PCI offset 0x2C                                  */
} XHCI_PSW_LIFE, *PXHCI_PSW_LIFE;

/* 1 when PCI offset 0's vendor/device dword names a controller with the
 * mux: Intel 1E31, 8C31, 9C31, 8CB1, 9CB1 or 8D31. */
ULONG XhciPswGate(ULONG vendorDevice);

/* 1 when the vendor is Intel: the only controllers the value is read on. */
ULONG XhciPswIntel(ULONG vendorDevice);

/* 1 when PCI offset 0x2C's subsystem dword names a board that cannot
 * switch (Sony 104D:90A8, Linux's one exemption). */
ULONG XhciPswBoardRefused(ULONG subsystem);

/* The XhciIntelPortSwitch rule: on a listed controller GATED unless the
 * value was found and is 0; on another Intel controller BYPASS only when
 * it was found and is exactly 2; on any other vendor OFF. */
ULONG XhciPswMode(ULONG vendorDevice, ULONG found, ULONG value);

/* Every switchable connector to xHCI, SuperSpeed terminations first.
 * Returns st->Step; st->Written says which writes were accepted. */
ULONG XhciPswRoute(const XHCI_PSW_IO *io, PXHCI_PSW_STATE st);

/* The registers in written back to EHCI: USB3_PSSEN then XUSB2PR written
 * 0, each read back; a register not in written is not touched. Returns
 * st->Step, the first access that failed. */
ULONG XhciPswRelease(const XHCI_PSW_IO *io, ULONG written,
                     PXHCI_PSW_STATE st);

/* The start: clear the lifetime, decide, and route when on. Returns 1 when
 * the route ran; the decision's own reads do not count. */
ULONG XhciPswLifeStart(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                       PXHCI_PSW_STATE st);

/* A return to D0: route again when On. Returns 1 when the route ran. */
ULONG XhciPswLifeResume(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                        PXHCI_PSW_STATE st);

/* A shutdown's D3: release Written, keeping On and Written. Returns 1 when
 * Written was not empty. */
ULONG XhciPswLifeRelease(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                         PXHCI_PSW_STATE st);

/* The stop and a refused start: release Written, then clear On and
 * Written. Returns 1 when Written was not empty. */
ULONG XhciPswLifeEnd(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                     PXHCI_PSW_STATE st);

#endif /* XHCI_PSW_H */
