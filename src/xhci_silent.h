/*
 * xhci_silent.h - the Windows 98 SE and ME silent install, the pure half
 * (roadmap-hcd.md task 38.2; design record 13 section 6.11).
 *
 * A device PDO answers SilentInstall TRUE in IRP_MN_QUERY_CAPABILITIES only
 * when the controller's gate (XhciSilentInstall, a DWORD of exactly 1) is on
 * and an interface descriptor of its configuration matches an entry of
 * HKLM\System\CurrentControlSet\Services\USB\SilentClasses, read once per
 * controller start into XHCI_SILENT. Windows 98 SE and ME only; on NT the
 * bus reads neither and the table stays empty (hcd_ctl.c, hcdReadSilent).
 *
 * An entry is a 4-byte REG_BINARY, its name ignored: the control byte, then
 * bInterfaceClass, bInterfaceSubClass and bInterfaceProtocol. Control bits:
 * 0x80 the entry applies to a device the bus does not split, 0x40 to a
 * composite parent or function, 0x01 compare the subclass, 0x02 compare the
 * protocol; the class is always compared. An entry with neither 0x80 nor
 * 0x40, of another type or of another length is ignored and takes no slot;
 * past XHCI_SILENT_MAX a valid entry is dropped. Both are counted.
 *
 * The entries are kept as four UCHARs and every flag is tested as
 * (control & mask) != 0, so no flag reaches a BOOLEAN through a truncating
 * cast (6.11.5, the second trap).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_SILENT_H
#define XHCI_SILENT_H

#include "xhci_compat.h"

/* The table's cap (O5, owner 2026-10-13). */
#define XHCI_SILENT_MAX         32UL

/* REG_BINARY as ntddk.h numbers it; hcd_ctl.c checks the two agree. */
#define XHCI_SILENT_REG_BINARY  3UL

/* The control byte. */
#define XHCI_SILENT_CTL_SINGLE    0x80U  /* a device the bus does not split */
#define XHCI_SILENT_CTL_COMPOSITE 0x40U  /* a composite parent or function  */
#define XHCI_SILENT_CTL_SUBCLASS  0x01U  /* compare bInterfaceSubClass      */
#define XHCI_SILENT_CTL_PROTOCOL  0x02U  /* compare bInterfaceProtocol      */

/* XhciSilentAdd's outcome. */
#define XHCI_SILENT_KEPT        0UL
#define XHCI_SILENT_IGNORED     1UL
#define XHCI_SILENT_DROPPED     2UL

/* XhciSilentWalk's and XhciSilentPdo's reasons; the value is the silent.pdo
 * note's reason field (hcd_pdo.c, hcdSilentDecide). */
#define XHCI_SILENT_NOMATCH     0UL
#define XHCI_SILENT_MATCH       1UL
#define XHCI_SILENT_MALFORMED   2UL
#define XHCI_SILENT_OFF         3UL  /* the gate is off                     */
#define XHCI_SILENT_EMPTY       4UL  /* no entries                          */
#define XHCI_SILENT_BARRED      5UL  /* presented UAS, or refused (O2)      */

typedef struct _XHCI_SILENT {
    UCHAR Entry[XHCI_SILENT_MAX][4];
    ULONG Count;                /* entries kept, the first Count of Entry */
    ULONG Ignored;              /* values of another type, length or no
                                 * 0x80/0x40 bit                          */
    ULONG Dropped;              /* valid entries past XHCI_SILENT_MAX     */
} XHCI_SILENT, *PXHCI_SILENT;

/* The gate's rule (O6): 1 only when a DWORD was found and it is 1. */
ULONG XhciSilentGate(ULONG found, ULONG value);

/* Count, Ignored and Dropped to 0, and the entries cleared. */
VOID XhciSilentReset(PXHCI_SILENT table);

/* One registry value, as the query routine is handed it: kept, ignored or
 * dropped (XHCI_SILENT_KEPT / _IGNORED / _DROPPED), and counted. */
ULONG XhciSilentAdd(PXHCI_SILENT table, ULONG type, const UCHAR *data,
                    ULONG length);

/* The query's end: `ok` 0 (the query did not succeed) empties the table and
 * keeps Ignored and Dropped; nonzero keeps it as read. */
VOID XhciSilentEnd(PXHCI_SILENT table, ULONG ok);

/* The walk of a configuration descriptor (6.11.5): XHCI_SILENT_MATCH when
 * it was walked whole and any interface descriptor matched an entry that
 * applies to `composite` (0 or nonzero); XHCI_SILENT_NOMATCH, before any
 * byte is read, when `entries` is NULL or `count` 0; XHCI_SILENT_MALFORMED
 * when the header or any descriptor within wTotalLength is unsound. */
ULONG XhciSilentWalk(const UCHAR (*entries)[4], ULONG count,
                     const UCHAR *config, ULONG length, ULONG composite);

/* Exactly 1 when XhciSilentWalk is XHCI_SILENT_MATCH, else 0. */
ULONG XhciSilentMatch(const UCHAR (*entries)[4], ULONG count,
                      const UCHAR *config, ULONG length, ULONG composite);

/* A PDO's reason: XHCI_SILENT_OFF (gate 0), XHCI_SILENT_EMPTY (no entries),
 * XHCI_SILENT_BARRED (storageBarred nonzero, before any walk), else the
 * walk's. The PDO is silent for XHCI_SILENT_MATCH alone. */
ULONG XhciSilentPdo(ULONG gate, const UCHAR (*entries)[4], ULONG count,
                    const UCHAR *config, ULONG length, ULONG composite,
                    ULONG storageBarred);

#endif /* XHCI_SILENT_H */
