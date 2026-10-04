/*
 * xhci_xport.c - the mass-storage transport policy, the pure half
 * (xhci_xport.h; roadmap-hcd.md task 31-A.3).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_xport.h"

#define XHCI_XPORT_DT_CONFIG    0x02UL
#define XHCI_XPORT_DT_INTERFACE 0x04UL
#define XHCI_XPORT_CONFIG_BYTES 9UL
#define XHCI_XPORT_IFACE_BYTES  9UL
#define XHCI_XPORT_DEVICE_BYTES 18UL
#define XHCI_XPORT_DT_DEVICE    0x01UL

static ULONG xhciXportWord(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8);
}

/* wTotalLength when the header and every descriptor in it are sound, so
 * the walks below may step by bLength unchecked; else 0. */
static ULONG xhciXportTotal(const UCHAR *config, ULONG length)
{
    ULONG total;
    ULONG offset;
    ULONG bLength;

    if (length < XHCI_XPORT_CONFIG_BYTES ||
        (ULONG)config[0] < XHCI_XPORT_CONFIG_BYTES ||
        (ULONG)config[1] != XHCI_XPORT_DT_CONFIG) {
        return 0;
    }
    total = xhciXportWord(config + 2);
    if (total < XHCI_XPORT_CONFIG_BYTES || total > length ||
        (ULONG)config[0] > total) {
        return 0;
    }
    offset = (ULONG)config[0];
    while (offset < total) {
        if (total - offset < 2UL) {
            return 0;
        }
        bLength = (ULONG)config[offset];
        if (bLength < 2UL || bLength > total - offset) {
            return 0;
        }
        if ((ULONG)config[offset + 1] == XHCI_XPORT_DT_INTERFACE &&
            bLength < XHCI_XPORT_IFACE_BYTES) {
            return 0;
        }
        offset += bLength;
    }
    return total;
}

ULONG XhciXportSingleInterface(const UCHAR *config, ULONG length,
                               PULONG iface)
{
    ULONG total;
    ULONG offset;
    ULONG count;

    if (config == NULL || iface == NULL) {
        return 0;
    }
    total = xhciXportTotal(config, length);
    if (total == 0) {
        return 0;
    }
    count = 0;
    for (offset = (ULONG)config[0]; offset < total;
         offset += (ULONG)config[offset]) {
        if ((ULONG)config[offset + 1] == XHCI_XPORT_DT_INTERFACE &&
            config[offset + 3] == 0) {
            if (count++ == 0) {
                *iface = (ULONG)config[offset + 2];
            }
        }
    }
    return count == 1;
}

ULONG XhciXportChoose(const UCHAR *config, ULONG length, ULONG iface,
                      ULONG flags, PXHCI_XPORT out)
{
    ULONG total;
    ULONG offset;
    ULONG alt0;                     /* offset of alternate 0, 0 if none */
    ULONG uas;                      /* offset of the first UAS setting  */
    ULONG botAt0;
    ULONG uasUsable;
    ULONG at;

    if (config == NULL || out == NULL) {
        return XHCI_XPORT_BAD_PARAM;
    }
    out->Transport = XHCI_XPORT_NONE;
    out->Why = XHCI_XPORT_WHY_NOT_UAS;
    out->Class = 0;
    out->SubClass = 0;
    out->Protocol = 0;
    out->Alternate = 0;
    out->ShortHardwareId = 1;
    total = xhciXportTotal(config, length);
    if (total == 0) {
        return XHCI_XPORT_MALFORMED;
    }

    alt0 = 0;
    uas = 0;
    for (offset = (ULONG)config[0]; offset < total;
         offset += (ULONG)config[offset]) {
        if ((ULONG)config[offset + 1] != XHCI_XPORT_DT_INTERFACE ||
            (ULONG)config[offset + 2] != iface) {
            continue;
        }
        if (config[offset + 3] == 0 && alt0 == 0) {
            alt0 = offset;
        }
        if (uas == 0 &&
            (ULONG)config[offset + 5] == XHCI_XPORT_CLASS_STORAGE &&
            (ULONG)config[offset + 6] == XHCI_XPORT_SUBCLASS_SCSI &&
            (ULONG)config[offset + 7] == XHCI_XPORT_PROT_UAS) {
            uas = offset;
        }
    }
    if (alt0 == 0) {
        return XHCI_XPORT_OK;
    }
    out->Class = (ULONG)config[alt0 + 5];
    out->SubClass = (ULONG)config[alt0 + 6];
    out->Protocol = (ULONG)config[alt0 + 7];
    if (uas == 0) {
        return XHCI_XPORT_OK;
    }

    botAt0 = out->Class == XHCI_XPORT_CLASS_STORAGE &&
             out->Protocol == XHCI_XPORT_PROT_BOT;
    /* SuperSpeed UAS runs on streams; below SuperSpeed the status
     * pipe's READ READY and WRITE READY carry it without them. */
    uasUsable = (flags & XHCI_XPORT_F_SUPERSPEED) == 0 ||
                (flags & XHCI_XPORT_F_HC_STREAMS) != 0;

    if (botAt0 && ((flags & XHCI_XPORT_F_FORCE_BOT) != 0 || !uasUsable)) {
        out->Transport = XHCI_XPORT_BOT;
        out->Why = ((flags & XHCI_XPORT_F_FORCE_BOT) != 0)
                       ? XHCI_XPORT_WHY_BOT_FORCED
                       : XHCI_XPORT_WHY_BOT_NO_STREAMS;
        return XHCI_XPORT_OK;
    }
    /* Under UAS the VID/PID hardware id stays: a device usbstor.inf
     * names by hand still goes to usbstor.sys on it, the residual case
     * roadmap 31-A.3 records rather than fights. A refused device shows
     * none, so no hand-listed line binds a transport it cannot honour. */
    if (uasUsable) {
        at = uas;
        out->Transport = XHCI_XPORT_UAS;
        if (botAt0) {
            out->Why = XHCI_XPORT_WHY_UAS;
        } else if ((flags & XHCI_XPORT_F_FORCE_BOT) != 0) {
            out->Why = XHCI_XPORT_WHY_UAS_ONLY_FORCED;
        } else {
            out->Why = XHCI_XPORT_WHY_UAS_ONLY;
        }
        out->Class = (ULONG)config[at + 5];
        out->SubClass = (ULONG)config[at + 6];
        out->Protocol = (ULONG)config[at + 7];
        out->Alternate = (ULONG)config[at + 3];
        return XHCI_XPORT_OK;
    }
    out->ShortHardwareId = 0;
    out->Transport = XHCI_XPORT_REFUSED;
    out->Why = XHCI_XPORT_WHY_NO_STREAMS;
    out->Class = 0;
    out->SubClass = 0;
    out->Protocol = 0;
    return XHCI_XPORT_OK;
}

/* The id text, counted past the capacity so the caller learns the size. */
typedef struct _XHCI_XPORT_TEXT {
    char *Out;
    ULONG Capacity;
    ULONG Used;
} XHCI_XPORT_TEXT;

static const char xhciXportHex[] = "0123456789ABCDEF";

static VOID xhciXportChar(XHCI_XPORT_TEXT *t, char c)
{
    if (t->Used < t->Capacity) {
        t->Out[t->Used] = c;
    }
    t->Used++;
}

static VOID xhciXportStr(XHCI_XPORT_TEXT *t, const char *s)
{
    while (*s != 0) {
        xhciXportChar(t, *s++);
    }
}

static VOID xhciXportHexN(XHCI_XPORT_TEXT *t, ULONG value, ULONG digits)
{
    while (digits > 0) {
        digits--;
        xhciXportChar(t, xhciXportHex[(value >> (digits * 4UL)) & 0xFUL]);
    }
}

static VOID xhciXportVidPid(XHCI_XPORT_TEXT *t, const UCHAR *device)
{
    xhciXportStr(t, "USB\\VID_");
    xhciXportHexN(t, xhciXportWord(device + 8), 4);
    xhciXportStr(t, "&PID_");
    xhciXportHexN(t, xhciXportWord(device + 10), 4);
}

static VOID xhciXportMi(XHCI_XPORT_TEXT *t, ULONG mi)
{
    if (mi != XHCI_XPORT_NO_MI) {
        xhciXportStr(t, "&MI_");
        xhciXportHexN(t, mi, 2);
    }
}

ULONG XhciXportId(const UCHAR *device, const XHCI_XPORT *x, ULONG mi,
                  ULONG which, char *out, ULONG capacity, PULONG used)
{
    XHCI_XPORT_TEXT t;
    ULONG depth;

    if (device == NULL || x == NULL || used == NULL ||
        (out == NULL && capacity != 0) ||
        (mi != XHCI_XPORT_NO_MI && mi > 0xFFUL)) {
        return XHCI_XPORT_BAD_PARAM;
    }
    if ((ULONG)device[0] < XHCI_XPORT_DEVICE_BYTES ||
        (ULONG)device[1] != XHCI_XPORT_DT_DEVICE) {
        return XHCI_XPORT_MALFORMED;
    }
    t.Out = out;
    t.Capacity = capacity;
    t.Used = 0;
    switch (which) {
    case XHCI_XPORT_ID_HARDWARE:
        if (x->Transport == XHCI_XPORT_REFUSED) {
            /* No VID/PID-derived id any INF could name (xhci_xport.h). */
            xhciXportStr(&t, XHCI_XPORT_REFUSED_PREFIX);
            xhciXportStr(&t, "VID_");
            xhciXportHexN(&t, xhciXportWord(device + 8), 4);
            xhciXportStr(&t, "&PID_");
            xhciXportHexN(&t, xhciXportWord(device + 10), 4);
            xhciXportStr(&t, "&REV_");
            xhciXportHexN(&t, xhciXportWord(device + 12), 4);
            xhciXportMi(&t, mi);
            xhciXportChar(&t, 0);
            xhciXportChar(&t, 0);
            break;
        }
        xhciXportVidPid(&t, device);
        xhciXportStr(&t, "&REV_");
        xhciXportHexN(&t, xhciXportWord(device + 12), 4);
        xhciXportMi(&t, mi);
        xhciXportChar(&t, 0);
        if (x->ShortHardwareId) {
            xhciXportVidPid(&t, device);
            xhciXportMi(&t, mi);
            xhciXportChar(&t, 0);
        }
        xhciXportChar(&t, 0);
        break;
    case XHCI_XPORT_ID_COMPATIBLE:
        if (x->Transport == XHCI_XPORT_REFUSED) {
            *used = 0;
            return XHCI_XPORT_NO_IDS;
        }
        for (depth = 3; depth != 0; depth--) {
            xhciXportStr(&t, "USB\\Class_");
            xhciXportHexN(&t, x->Class, 2);
            if (depth >= 2) {
                xhciXportStr(&t, "&SubClass_");
                xhciXportHexN(&t, x->SubClass, 2);
            }
            if (depth >= 3) {
                xhciXportStr(&t, "&Prot_");
                xhciXportHexN(&t, x->Protocol, 2);
            }
            xhciXportChar(&t, 0);
        }
        xhciXportChar(&t, 0);
        break;
    default:
        return XHCI_XPORT_BAD_PARAM;
    }
    *used = t.Used;
    return (t.Used > capacity) ? XHCI_XPORT_TOO_SMALL : XHCI_XPORT_OK;
}

ULONG XhciXportRefusedAt(ULONG route, ULONG companion)
{
    if (route != 0) {
        return XHCI_XPORT_AT_BEHIND_HUB;
    }
    return (companion != 0) ? XHCI_XPORT_AT_ROOT_COMPANION
                            : XHCI_XPORT_AT_ROOT_ALONE;
}
