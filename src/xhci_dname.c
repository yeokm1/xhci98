/*
 * xhci_dname.c - the controller's \DosDevices name, the pure half
 * (xhci_dname.h; roadmap-usb4 task 44.81; design record 13 section 8.8).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_dname.h"

ULONG XhciDnameForm(ULONG nt)
{
    return nt ? XHCI_DNAME_FORM_PROJECT : XHCI_DNAME_FORM_HCD;
}

ULONG XhciDnameAlias(ULONG nt, ULONG nt6)
{
    return (nt && !nt6) ? 1UL : 0UL;
}

VOID XhciDnameInit(ULONG *taken)
{
    ULONG i;

    for (i = 0; i < XHCI_DNAME_WORDS; i++) {
        taken[i] = 0;
    }
}

ULONG XhciDnameClaim(ULONG *taken, ULONG from)
{
    ULONG n;
    ULONG bit;

    for (n = from; n < XHCI_DNAME_MAX; n++) {
        bit = 1UL << (n % 32UL);
        if ((taken[n / 32UL] & bit) == 0) {
            taken[n / 32UL] |= bit;
            return n;
        }
    }
    return XHCI_DNAME_MAX;
}

ULONG XhciDnameClaimDown(ULONG *taken, ULONG top)
{
    ULONG n;
    ULONG bit;

    if (top >= XHCI_DNAME_MAX) {
        top = XHCI_DNAME_MAX - 1;
    }
    n = top + 1;
    while (n > 0) {
        n--;
        bit = 1UL << (n % 32UL);
        if ((taken[n / 32UL] & bit) == 0) {
            taken[n / 32UL] |= bit;
            return n;
        }
    }
    return XHCI_DNAME_MAX;
}

VOID XhciDnameRelease(ULONG *taken, ULONG n)
{
    if (n >= XHCI_DNAME_MAX) {
        return;
    }
    taken[n / 32UL] &= ~(1UL << (n % 32UL));
}

ULONG XhciDnameHeld(const ULONG *taken, ULONG n)
{
    if (n >= XHCI_DNAME_MAX) {
        return 0;
    }
    return (taken[n / 32UL] >> (n % 32UL)) & 1UL;
}

ULONG XhciDnameProbeFree(ULONG status)
{
    return (status == XHCI_DNAME_ST_NAME_NOT_FOUND ||
            status == XHCI_DNAME_ST_PATH_NOT_FOUND) ? 1UL : 0UL;
}

ULONG XhciDnameDeleteReleases(ULONG status)
{
    return (status == XHCI_DNAME_ST_SUCCESS ||
            status == XHCI_DNAME_ST_NAME_NOT_FOUND) ? 1UL : 0UL;
}
