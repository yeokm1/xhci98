/*
 * xhci_inj.c - 35-T.9's fault injection, the pure half (xhci_inj.h).
 *
 * The qemu flavour alone compiles it into the driver; the debug and release
 * images carry none of it. C89, pure: IRQL any.
 */

#include "xhci_inj.h"

#if defined(XHCI_FLAVOUR_QEMU) || defined(XHCI_HOST_TEST)

VOID XhciInjTriggerStart(PXHCI_INJ_TRIGGER t, ULONG found, ULONG value)
{
    t->Seen = found ? 1UL : 0UL;
    t->Seq = found ? XHCI_INJ_GET_SEQ(value) : 0UL;
}

ULONG XhciInjTake(PXHCI_INJ_TRIGGER t, ULONG found, ULONG value)
{
    ULONG seq;
    ULONG fault;

    if (!found) {
        return XHCI_INJ_TAKE_NONE;
    }
    seq = XHCI_INJ_GET_SEQ(value);
    if (t->Seen && t->Seq == seq) {
        return XHCI_INJ_TAKE_NONE;
    }
    t->Seen = 1;
    t->Seq = seq;
    fault = XHCI_INJ_GET_FAULT(value);
    if ((fault >= XHCI_INJ_LOST_IRQ && fault <= XHCI_INJ_DEAD_NOPROOF) ||
        fault == XHCI_INJ_CLEAR) {
        return XHCI_INJ_TAKE_FIRE;
    }
    if (fault >= XHCI_INJ_TRANSACTION && fault <= XHCI_INJ_RESERVED_LAST) {
        return XHCI_INJ_TAKE_UNBUILT;
    }
    return XHCI_INJ_TAKE_UNKNOWN;
}

VOID XhciInjRegsClear(PXHCI_INJ_REGS r)
{
    r->PedPort = 0;
    r->PecHeld = 0;
    r->OcPort = 0;
    r->OcActive = 0;
    r->OcaHeld = 0;
    r->OccHeld = 0;
    r->OcPpWrites = 0;
    r->DeadLeft = 0;
    r->NoProof = 0;
}

ULONG XhciInjPortFits(const XHCI_PORT_MAP *map, ULONG port, ULONG wanted,
                      ULONG portsc)
{
    ULONG need;

    if (wanted != 0 && port != wanted) {
        return 0;
    }
    if (!XhciPortIsManaged(map, port) || XhciPortIsUsb3(map, port)) {
        return 0;
    }
    need = XHCI_PORTSC_CCS | XHCI_PORTSC_PED | XHCI_PORTSC_PP;
    if (portsc == 0xFFFFFFFFUL || (portsc & need) != need) {
        return 0;
    }
    return 1;
}

VOID XhciInjArmPec(PXHCI_INJ_REGS r, ULONG port)
{
    r->PedPort = port;
    r->PecHeld = 1;
}

VOID XhciInjArmOc(PXHCI_INJ_REGS r, ULONG port)
{
    r->OcPort = port;
    r->OcActive = 1;
    r->OcaHeld = 1;
    r->OccHeld = 1;
    r->OcPpWrites = 0;
}

VOID XhciInjReleaseOc(PXHCI_INJ_REGS r)
{
    r->OcaHeld = 0;
}

VOID XhciInjArmDead(PXHCI_INJ_REGS r, ULONG reads, ULONG noProof)
{
    r->DeadLeft = (reads == 0) ? XHCI_INJ_DEAD_FOREVER : reads;
    r->NoProof = noProof ? 1UL : 0UL;
}

ULONG XhciInjPortscRead(const XHCI_INJ_REGS *r, ULONG port, ULONG raw)
{
    ULONG value;

    value = raw;
    if (raw == 0xFFFFFFFFUL || port == 0) {
        return raw;
    }
    if (r->PecHeld && port == r->PedPort) {
        value |= XHCI_PORTSC_PEC;
    }
    if (r->OcActive && port == r->OcPort) {
        value &= ~XHCI_PORTSC_PP;
        if (r->OcaHeld) {
            value |= XHCI_PORTSC_OCA;
        }
        if (r->OccHeld) {
            value |= XHCI_PORTSC_OCC;
        }
    }
    return value;
}

ULONG XhciInjPortscWrite(PXHCI_INJ_REGS r, ULONG port, ULONG value)
{
    if (port == 0) {
        return value;
    }
    if (r->PecHeld && port == r->PedPort &&
        (value & XHCI_PORTSC_PEC) != 0) {
        r->PecHeld = 0;
        r->PedPort = 0;
    }
    if (r->OcActive && port == r->OcPort) {
        if ((value & XHCI_PORTSC_OCC) != 0) {
            r->OccHeld = 0;
        }
        if ((value & XHCI_PORTSC_PP) != 0) {
            r->OcPpWrites++;
            /* After the release the driver's repower ends the emulation:
             * the real port, which never lost power, is read from here. */
            if (!r->OcaHeld) {
                r->OcActive = 0;
                r->OccHeld = 0;
                r->OcPort = 0;
            }
        }
        /* The emulated power-off never reaches the real port: QEMU stores
         * PP as written, and a port it leaves unpowered is not the one the
         * emulation ends on. */
        value |= XHCI_PORTSC_PP;
    }
    return value;
}

ULONG XhciInjUsbsts(PXHCI_INJ_REGS r, ULONG raw)
{
    if (r->DeadLeft == 0) {
        return raw;
    }
    if (r->DeadLeft != XHCI_INJ_DEAD_FOREVER) {
        r->DeadLeft--;
    }
    return 0xFFFFFFFFUL;
}

ULONG XhciInjPciCommand(const XHCI_INJ_REGS *r, ULONG command)
{
    if (r->NoProof && command != 0xFFFFUL) {
        return command | XHCI_INJ_PCI_BME;
    }
    return command;
}

ULONG XhciInjLostCount(ULONG arg)
{
    if (arg == 0) {
        return 1;
    }
    if (arg == XHCI_INJ_ARG_FOREVER) {
        return 0xFFFFFFFFUL;
    }
    return arg;
}

#else /* debug and release: injection is not in the image */

typedef int xhciInjNotInImage;

#endif
