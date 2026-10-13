/*
 * xhci_silent.c - the Windows 98 SE and ME silent install, the pure half
 * (xhci_silent.h; roadmap-hcd.md task 38.2, design record 13 section 6.11).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_silent.h"

#define XHCI_SILENT_DT_CONFIG     0x02UL
#define XHCI_SILENT_DT_INTERFACE  0x04UL
#define XHCI_SILENT_DT_IAD        0x0BUL
#define XHCI_SILENT_CONFIG_BYTES  9UL
#define XHCI_SILENT_IFACE_BYTES   9UL
#define XHCI_SILENT_IAD_BYTES     8UL

ULONG XhciSilentGate(ULONG found, ULONG value)
{
    return (found != 0 && value == 1) ? 1UL : 0UL;
}

VOID XhciSilentReset(PXHCI_SILENT table)
{
    ULONG i;
    ULONG j;

    if (table == NULL) {
        return;
    }
    for (i = 0; i < XHCI_SILENT_MAX; i++) {
        for (j = 0; j < 4; j++) {
            table->Entry[i][j] = 0;
        }
    }
    table->Count = 0;
    table->Ignored = 0;
    table->Dropped = 0;
}

ULONG XhciSilentAdd(PXHCI_SILENT table, ULONG type, const UCHAR *data,
                    ULONG length)
{
    ULONG j;

    if (table == NULL) {
        return XHCI_SILENT_IGNORED;
    }
    if (type != XHCI_SILENT_REG_BINARY || length != 4 || data == NULL ||
        (data[0] & (XHCI_SILENT_CTL_SINGLE | XHCI_SILENT_CTL_COMPOSITE)) ==
            0) {
        table->Ignored++;
        return XHCI_SILENT_IGNORED;
    }
    if (table->Count >= XHCI_SILENT_MAX) {
        table->Dropped++;
        return XHCI_SILENT_DROPPED;
    }
    for (j = 0; j < 4; j++) {
        table->Entry[table->Count][j] = data[j];
    }
    table->Count++;
    return XHCI_SILENT_KEPT;
}

VOID XhciSilentEnd(PXHCI_SILENT table, ULONG ok)
{
    if (table != NULL && ok == 0) {
        /* A list read in part is not the list (6.11.3). */
        table->Count = 0;
    }
}

/* One interface's triple against one entry, for a PDO of `composite`'s
 * kind. Exactly 0 or 1. */
static ULONG xhciSilentEntry(const UCHAR *entry, ULONG composite,
                             ULONG cls, ULONG sub, ULONG proto)
{
    ULONG control;

    control = (ULONG)entry[0];
    if (composite != 0) {
        if ((control & XHCI_SILENT_CTL_COMPOSITE) == 0) {
            return 0;
        }
    } else if ((control & XHCI_SILENT_CTL_SINGLE) == 0) {
        return 0;
    }
    if (cls != (ULONG)entry[1]) {
        return 0;
    }
    if ((control & XHCI_SILENT_CTL_SUBCLASS) != 0 && sub != (ULONG)entry[2]) {
        return 0;
    }
    if ((control & XHCI_SILENT_CTL_PROTOCOL) != 0 &&
        proto != (ULONG)entry[3]) {
        return 0;
    }
    return 1;
}

ULONG XhciSilentWalk(const UCHAR (*entries)[4], ULONG count,
                     const UCHAR *config, ULONG length, ULONG composite)
{
    ULONG total;
    ULONG offset;
    ULONG bLength;
    ULONG type;
    ULONG matched;
    ULONG i;

    /* The first trap: an absent list is never dereferenced. */
    if (entries == NULL || count == 0) {
        return XHCI_SILENT_NOMATCH;
    }
    if (config == NULL || length < XHCI_SILENT_CONFIG_BYTES ||
        (ULONG)config[0] < XHCI_SILENT_CONFIG_BYTES ||
        (ULONG)config[1] != XHCI_SILENT_DT_CONFIG) {
        return XHCI_SILENT_MALFORMED;
    }
    total = (ULONG)config[2] | ((ULONG)config[3] << 8);
    if (total < XHCI_SILENT_CONFIG_BYTES || total < (ULONG)config[0] ||
        total > length) {
        return XHCI_SILENT_MALFORMED;
    }
    matched = 0;
    offset = (ULONG)config[0];
    while (offset < total) {
        if (total - offset < 2) {
            return XHCI_SILENT_MALFORMED;
        }
        bLength = (ULONG)config[offset];
        type = (ULONG)config[offset + 1];
        if (bLength < 2 || bLength > total - offset) {
            return XHCI_SILENT_MALFORMED;
        }
        if (type == XHCI_SILENT_DT_INTERFACE) {
            if (bLength < XHCI_SILENT_IFACE_BYTES) {
                return XHCI_SILENT_MALFORMED;
            }
            for (i = 0; i < count && matched == 0; i++) {
                matched = xhciSilentEntry(entries[i], composite,
                                          (ULONG)config[offset + 5],
                                          (ULONG)config[offset + 6],
                                          (ULONG)config[offset + 7]);
            }
        } else if (type == XHCI_SILENT_DT_IAD &&
                   bLength < XHCI_SILENT_IAD_BYTES) {
            return XHCI_SILENT_MALFORMED;
        }
        offset += bLength;
    }
    /* Walked whole: a match before a fault never reaches here (O4). */
    return (matched != 0) ? XHCI_SILENT_MATCH : XHCI_SILENT_NOMATCH;
}

ULONG XhciSilentMatch(const UCHAR (*entries)[4], ULONG count,
                      const UCHAR *config, ULONG length, ULONG composite)
{
    return (XhciSilentWalk(entries, count, config, length, composite) ==
            XHCI_SILENT_MATCH)
               ? 1UL
               : 0UL;
}

ULONG XhciSilentPdo(ULONG gate, const UCHAR (*entries)[4], ULONG count,
                    const UCHAR *config, ULONG length, ULONG composite,
                    ULONG storageBarred)
{
    if (gate == 0) {
        return XHCI_SILENT_OFF;
    }
    if (entries == NULL || count == 0) {
        return XHCI_SILENT_EMPTY;
    }
    if (storageBarred != 0) {
        return XHCI_SILENT_BARRED;
    }
    return XhciSilentWalk(entries, count, config, length, composite);
}
