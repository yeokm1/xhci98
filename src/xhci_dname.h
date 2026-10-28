/*
 * xhci_dname.h - the controller's \DosDevices name, the pure half
 * (roadmap-usb4 task 44.81; design record 13 section 8.8).
 *
 * Each controller FDO has one \DosDevices name for the user-mode door: on
 * Windows 98 SE and ME \DosDevices\HCD<n>, as usbport and Windows 98's own
 * USB drivers name theirs, and on every NT target (2000 to 10)
 * \DosDevices\XHCI98HC<k>, a name of this project's own. On Windows 2000 a
 * create that collides with an existing \DosDevices link leaves a pointer
 * reference on the EXISTING link, which keeps the device object it names
 * (and so the driver image) until Windows restarts; usbport creates
 * \DosDevices\HCD<its index> once, at the index its \Device\USBFDO-<n>
 * took, so a controller of this driver that held that HCD<n> lost its
 * unload (runs/run-44.md, "44.81"). Nothing else creates XHCI98HC<k>.
 *
 * What lives here: which form a system takes, whether it also gets the
 * HCD<m> alias, the driver-wide record of
 * the numbers its controllers hold (one bit each), what a probe's status
 * means, and whether a delete's status gives the number back. hcd_door.c
 * takes the lock around the record and makes the calls.
 *
 * DDK-free: part of the pure core. C89. IRQL: any; nothing here takes a
 * lock.
 */

#ifndef XHCI_DNAME_H
#define XHCI_DNAME_H

#include "xhci_compat.h"

/* The numbers tried, 0 to XHCI_DNAME_MAX - 1, and the record's words. */
#define XHCI_DNAME_MAX              64UL
#define XHCI_DNAME_WORDS            (XHCI_DNAME_MAX / 32UL)

/* The alias's numbers: 9 down to 0. usbui.dll's EnumerateAll on 2000 and
 * XP opens \\.\HCD0 to \\.\HCD9 and passes over one that does not
 * open; usbport's AddDevice takes the lowest free \Device\USBFDO-<i> and
 * creates \DosDevices\HCD<i> once, at that i, with no retry (static
 * readings, runs/run-44.md "44.81"). From the top, the alias meets a
 * usbport controller only when more than ten USB host controllers run at
 * once. */
#define XHCI_DNAME_ALIAS_TOP        9UL

/* The two forms. */
#define XHCI_DNAME_FORM_HCD         0UL     /* \DosDevices\HCD<n>: 98, ME */
#define XHCI_DNAME_FORM_PROJECT     1UL     /* \DosDevices\XHCI98HC<k>    */

/* NTSTATUS values the decisions read (ntstatus.h). */
#define XHCI_DNAME_ST_SUCCESS       0x00000000UL
#define XHCI_DNAME_ST_NAME_NOT_FOUND 0xC0000034UL
#define XHCI_DNAME_ST_PATH_NOT_FOUND 0xC000003AUL

/* The form for a system: `nt` is IoIsWdmVersionAvailable(1, 0x10). */
ULONG XhciDnameForm(ULONG nt);

/* Whether a system also gets the alias \DosDevices\HCD<m>, a link to the
 * XHCI98HC<k> link rather than to the FDO: 2000, XP and XP x64, whose
 * usbui.dll walks \\.\HCD<n> for its pop-ups and control-panel walk
 * (design record 13 section 8.1); not 98 SE and ME, whose door is HCD<n>
 * itself, nor Vista and later, whose usbui.dll enumerates the host
 * controller interface. `nt` as above, `nt6` IoIsWdmVersionAvailable(6,
 * 0). */
ULONG XhciDnameAlias(ULONG nt, ULONG nt6);

/* Every number free. */
VOID XhciDnameInit(ULONG *taken);

/* The first n from `from` on that the record does not hold, marked held;
 * XHCI_DNAME_MAX when none is left. */
ULONG XhciDnameClaim(ULONG *taken, ULONG from);

/* The highest n at or below `top` that the record does not hold, marked
 * held; XHCI_DNAME_MAX when none is left. A `top` past the record starts
 * at its last number. */
ULONG XhciDnameClaimDown(ULONG *taken, ULONG top);

/* Gives n back; a number out of range is ignored. */
VOID XhciDnameRelease(ULONG *taken, ULONG n);

/* 1 when the record holds n. */
ULONG XhciDnameHeld(const ULONG *taken, ULONG n);

/* A probe's open of the name: 1 only when it says nothing is there (the
 * name, or a directory on its path, not found); success and every other
 * failure (a sharing or access refusal, a device that is going) mean
 * something holds the name, and it is never created over. */
ULONG XhciDnameProbeFree(ULONG status);

/* After IoDeleteSymbolicLink of a name this driver made: 1 when the
 * number can be given back - the delete succeeded, or the name is already
 * gone; 0 for any other failure, where the name may still be there and
 * the number stays held until the driver unloads, so no later start
 * collides with it. */
ULONG XhciDnameDeleteReleases(ULONG status);

#endif /* XHCI_DNAME_H */
