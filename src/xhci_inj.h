/*
 * xhci_inj.h - 35-T.9's fault injection, the pure half (qemu flavour only;
 * hcd_inj.c is the driver half). Design record 17 section 5.
 *
 * QEMU raises none of the faults controller tolerance answers, so this layer
 * makes each one real where QEMU allows it and, where it does not, answers
 * the register reads the driver's own handling would meet. It never ships:
 * src/sources compiles it into the qemu flavour alone (XHCI_FLAVOUR_QEMU),
 * the host suite test\test_inj.c into the host build, and the debug and
 * release images carry none of it.
 *
 * THE TRIGGER. One REG_DWORD, XhciQemuInject, in the controller's driver key,
 * re-read by the controller thread about once a second, so a guest-side
 * script fires a fault at run time with regedit /s (or reg add):
 *
 *   bits 31:24  sequence - a command fires when this differs from the last
 *               one seen; the value present at a start is latched unfired
 *   bits 23:16  the fault, XHCI_INJ_*
 *   bits 15:8   a root port number, 0 = the first one that fits
 *   bits 7:0    the fault's argument
 *
 * Nothing is written back: the driver's log ring and the qemu trace carry
 * "qemu.inj.*" notes for every command taken, refused or ended.
 *
 * ADDING A FAULT (35-T.2, 35-T.3/4): a code below, an executor in hcd_inj.c's
 * dispatch, and the hook at the one place the faked state is met - for those,
 * the event drain (consuming the layer's Stopped events and handing an
 * injected Transfer Event to xhciHandleEvent) and the Endpoint Context read
 * (answering Halted or Error). The codes are reserved here and refused with a
 * note until their executors exist.
 *
 * C89, pure: IRQL any. The caller serializes (hcd_inj.c's InjLock).
 */

#ifndef XHCI_INJ_H
#define XHCI_INJ_H

#include "xhci_compat.h"

#if defined(XHCI_FLAVOUR_QEMU) || defined(XHCI_HOST_TEST)

#include "xhci.h"

#define XHCI_INJ_GET_SEQ(v)   ((((ULONG)(v)) >> 24) & 0xFFUL)
#define XHCI_INJ_GET_FAULT(v) ((((ULONG)(v)) >> 16) & 0xFFUL)
#define XHCI_INJ_GET_PORT(v)  ((((ULONG)(v)) >> 8) & 0xFFUL)
#define XHCI_INJ_GET_ARG(v)   (((ULONG)(v)) & 0xFFUL)
#define XHCI_INJ_VALUE(seq, fault, port, arg)                                 \
    (((((ULONG)(seq)) & 0xFFUL) << 24) | ((((ULONG)(fault)) & 0xFFUL) << 16) | \
     ((((ULONG)(port)) & 0xFFUL) << 8) | (((ULONG)(arg)) & 0xFFUL))

/* The faults built (record 17 section 5's table, the rows whose driver
 * behaviour is in the tree). */
#define XHCI_INJ_LOST_IRQ        1UL  /* arg interrupts lost; 0 one, 255 all */
#define XHCI_INJ_PED             2UL  /* real PED write, PEC answered        */
#define XHCI_INJ_OC              3UL  /* PP clear, OCA and OCC answered      */
#define XHCI_INJ_OC_RELEASE      4UL  /* OCA answered clear from now on      */
#define XHCI_INJ_HCH             5UL  /* real Run/Stop clear                 */
#define XHCI_INJ_DEAD            6UL  /* all-ones USBSTS, BME proof real     */
#define XHCI_INJ_DEAD_NOPROOF    7UL  /* the same, BME reads back set        */
/* Reserved for 35-T.2 to 35-T.4, refused until built. */
#define XHCI_INJ_TRANSACTION     8UL
#define XHCI_INJ_RESET_EP_FAIL   9UL
#define XHCI_INJ_REFUSED_CODE    10UL
#define XHCI_INJ_HALT_HALTED     11UL
#define XHCI_INJ_HALT_ERROR      12UL
#define XHCI_INJ_HALT_STALE      13UL
#define XHCI_INJ_EP_NOT_ENABLED  14UL
#define XHCI_INJ_EP0_THREAD      15UL
#define XHCI_INJ_EP0_PREPDO      16UL
#define XHCI_INJ_RACE_RING       17UL
#define XHCI_INJ_RACE_SECOND     18UL
#define XHCI_INJ_RESERVED_LAST   18UL
#define XHCI_INJ_CLEAR           0xFFUL  /* every injection ended            */

/* XhciInjTake's verdicts. */
#define XHCI_INJ_TAKE_NONE       0UL  /* nothing new                         */
#define XHCI_INJ_TAKE_FIRE       1UL  /* a built fault or CLEAR              */
#define XHCI_INJ_TAKE_UNBUILT    2UL  /* a reserved code: refused, noted     */
#define XHCI_INJ_TAKE_UNKNOWN    3UL  /* no such code: refused, noted        */

/* The argument that means "until CLEAR" for LOST_IRQ; for DEAD it is 0. */
#define XHCI_INJ_ARG_FOREVER     0xFFUL
#define XHCI_INJ_DEAD_FOREVER    0xFFFFFFFFUL

/* PCI Command, Bus Master Enable (xhci_hw.h's XHCI_PCI_COMMAND_BME, which
 * this pure half cannot include). */
#define XHCI_INJ_PCI_BME         0x0004UL

/* The trigger's memory: the last sequence seen, and whether one was. */
typedef struct _XHCI_INJ_TRIGGER {
    ULONG Seen;
    ULONG Seq;
} XHCI_INJ_TRIGGER, *PXHCI_INJ_TRIGGER;

/*
 * The register answers the layer gives in place of the hardware's. A port's
 * PEC is answered set from the layer's PED write until the driver's
 * acknowledgement; an over-current port reads PP clear (OCA and OCC set
 * while held) until the driver's PP write after the release, PP itself never
 * reaching the real port while emulated. USBSTS answers all-ones to the
 * containment step for DeadLeft reads (XHCI_INJ_DEAD_FOREVER: until CLEAR),
 * and Bus Master Enable reads back set once NoProof is armed.
 */
typedef struct _XHCI_INJ_REGS {
    ULONG PedPort;
    ULONG PecHeld;
    ULONG OcPort;
    ULONG OcActive;
    ULONG OcaHeld;
    ULONG OccHeld;
    ULONG OcPpWrites;
    ULONG DeadLeft;
    ULONG NoProof;
} XHCI_INJ_REGS, *PXHCI_INJ_REGS;

/* Latch the value present at a start, so it never fires: a start must not
 * replay the last command a script left in the key. `found` 0 is "no value
 * or not a DWORD". */
VOID XhciInjTriggerStart(PXHCI_INJ_TRIGGER t, ULONG found, ULONG value);

/* One read of the value: XHCI_INJ_TAKE_*. A fresh sequence is consumed
 * whatever its verdict, so a refused command is not retried. */
ULONG XhciInjTake(PXHCI_INJ_TRIGGER t, ULONG found, ULONG value);

/* Every answer off. */
VOID XhciInjRegsClear(PXHCI_INJ_REGS r);

/* A PED or over-current target: a managed USB 2.0 root port, powered, with
 * a device connected and enabled. `wanted` nonzero asks for that port only.
 * 1 fits, 0 not. */
ULONG XhciInjPortFits(const XHCI_PORT_MAP *map, ULONG port, ULONG wanted,
                      ULONG portsc);

/* The arming of each emulated fault on its port. */
VOID XhciInjArmPec(PXHCI_INJ_REGS r, ULONG port);
VOID XhciInjArmOc(PXHCI_INJ_REGS r, ULONG port);
VOID XhciInjReleaseOc(PXHCI_INJ_REGS r);
/* `reads` 0 means until CLEAR. */
VOID XhciInjArmDead(PXHCI_INJ_REGS r, ULONG reads, ULONG noProof);

/* The PORTSC a read answers. An unreadable PORTSC passes unchanged. */
ULONG XhciInjPortscRead(const XHCI_INJ_REGS *r, ULONG port, ULONG raw);

/* A PORTSC write: the acknowledgements and PP writes it carries are taken,
 * and the value the hardware is given returned. */
ULONG XhciInjPortscWrite(PXHCI_INJ_REGS r, ULONG port, ULONG value);

/* The containment step's USBSTS read: all-ones while armed, each such read
 * spending one of DeadLeft. */
ULONG XhciInjUsbsts(PXHCI_INJ_REGS r, ULONG raw);

/* The PCI Command register's read-back in the containment's proof. */
ULONG XhciInjPciCommand(const XHCI_INJ_REGS *r, ULONG command);

/* LOST_IRQ's argument as a count: 0 is one, 255 is "until CLEAR"
 * (returned as 0xFFFFFFFF). */
ULONG XhciInjLostCount(ULONG arg);

#endif /* XHCI_FLAVOUR_QEMU || XHCI_HOST_TEST */

#endif /* XHCI_INJ_H */
