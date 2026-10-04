/*
 * xhci_func.c - composite splitting, the pure half (xhci_func.h; roadmap-hcd.md
 * task 26-A.7; design record 13 sections 10.7 to 10.9).
 *
 * DDK-free: part of the pure core.
 */

#include "xhci_func.h"

#define XHCI_FUNC_BIT(n)    (1UL << ((n) & 0x1FUL))

static ULONG xhciFuncWord(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8);
}

/* wTotalLength when the header is sound, else 0. */
static ULONG xhciFuncTotal(const UCHAR *config, ULONG length)
{
    ULONG total;

    if (length < XHCI_FUNC_CONFIG_BYTES ||
        (ULONG)config[0] < XHCI_FUNC_CONFIG_BYTES ||
        (ULONG)config[1] != XHCI_FUNC_DT_CONFIG) {
        return 0;
    }
    total = xhciFuncWord(config + 2);
    if (total < XHCI_FUNC_CONFIG_BYTES || total > length ||
        (ULONG)config[0] > total) {
        return 0;
    }
    return total;
}

/* Every descriptor past the header within wTotalLength, so the later walks
 * may step by bLength unchecked. */
static ULONG xhciFuncSound(const UCHAR *config, ULONG total)
{
    ULONG offset;
    ULONG bLength;
    ULONG bType;

    offset = (ULONG)config[0];
    while (offset < total) {
        if (total - offset < 2UL) {
            return 0;
        }
        bLength = (ULONG)config[offset];
        bType = (ULONG)config[offset + 1];
        if (bLength < 2UL || bLength > total - offset) {
            return 0;
        }
        if ((bType == XHCI_FUNC_DT_INTERFACE &&
             bLength < XHCI_FUNC_IFACE_BYTES) ||
            (bType == XHCI_FUNC_DT_IAD && bLength < XHCI_FUNC_IAD_BYTES)) {
            return 0;
        }
        offset += bLength;
    }
    return 1;
}

static ULONG xhciFuncCount(ULONG mask)
{
    ULONG n;

    n = 0;
    while (mask != 0) {
        mask &= mask - 1;
        n++;
    }
    return n;
}

ULONG XhciFuncSplit(const UCHAR *device, const UCHAR *config, ULONG length,
                    PXHCI_FUNC_SET set)
{
    ULONG at[32];                   /* alternate-0 interface descriptors */
    ULONG iadAt[XHCI_FUNC_MAX];
    ULONG iadMask[XHCI_FUNC_MAX];
    ULONG iads;
    ULONG count;
    ULONG present;
    ULONG covered;                  /* interfaces an IAD has taken       */
    ULONG taken;
    ULONG total;
    ULONG offset;
    ULONG mask;
    ULONG first;
    ULONG iface;
    ULONG cls;
    ULONG sub;
    ULONG n;
    ULONG m;
    ULONG i;
    ULONG j;
    ULONG k;
    PXHCI_FUNC f;

    if (device == NULL || config == NULL || set == NULL) {
        return XHCI_FUNC_BAD_PARAM;
    }
    set->Count = 0;
    if ((ULONG)device[0] < XHCI_FUNC_DEVICE_BYTES ||
        (ULONG)device[1] != XHCI_FUNC_DT_DEVICE) {
        return XHCI_FUNC_MALFORMED;
    }
    total = xhciFuncTotal(config, length);
    if (total == 0 || !xhciFuncSound(config, total)) {
        return XHCI_FUNC_MALFORMED;
    }

    count = 0;
    present = 0;
    iads = 0;
    for (offset = (ULONG)config[0]; offset < total;
         offset += (ULONG)config[offset]) {
        if ((ULONG)config[offset + 1] == XHCI_FUNC_DT_IAD) {
            if (iads >= XHCI_FUNC_MAX) {
                return XHCI_FUNC_NO_SPLIT;
            }
            iadAt[iads++] = offset;
            continue;
        }
        if ((ULONG)config[offset + 1] != XHCI_FUNC_DT_INTERFACE ||
            config[offset + 3] != 0) {
            continue;
        }
        n = (ULONG)config[offset + 2];
        if (n >= 32UL) {
            return XHCI_FUNC_NO_SPLIT;
        }
        if ((present & XHCI_FUNC_BIT(n)) != 0) {
            return XHCI_FUNC_MALFORMED;
        }
        present |= XHCI_FUNC_BIT(n);
        at[count++] = offset;
    }

    /* Microsoft's composite-parent rule ("Enumeration of the composite
     * parent device"), not design record 13 section 10.8's wider one: a
     * device Windows leaves under one whole-device driver (several
     * configurations, or a class of its own, IADs or not) must keep the
     * USB\VID_v&PID_p id that driver matched on. */
    cls = (ULONG)device[4];
    if ((ULONG)device[17] != 1UL || count < 2 ||
        !(cls == 0 ||
          (cls == 0xEFUL && device[5] == 0x02 && device[6] == 0x01))) {
        return XHCI_FUNC_NO_SPLIT;
    }

    covered = 0;
    for (k = 0; k < iads; k++) {
        first = (ULONG)config[iadAt[k] + 2];
        mask = 0;
        for (j = 0; j < (ULONG)config[iadAt[k] + 3] && first + j < 32UL;
             j++) {
            mask |= XHCI_FUNC_BIT(first + j);
        }
        mask &= present;
        if ((mask & covered) != 0) {
            mask = 0;               /* overlaps an earlier IAD: ignored  */
        }
        iadMask[k] = mask;
        covered |= mask;
    }

    taken = 0;
    for (i = 0; i < count; i++) {
        n = (ULONG)config[at[i] + 2];
        if ((taken & XHCI_FUNC_BIT(n)) != 0) {
            continue;
        }
        mask = XHCI_FUNC_BIT(n);
        iface = at[i];
        k = 0;
        while (k < iads && (iadMask[k] & mask) == 0) {
            k++;
        }
        cls = (ULONG)config[at[i] + 5];
        if (k < iads) {
            mask = iadMask[k];
            first = (ULONG)config[iadAt[k] + 2];
            for (j = 0; j < count; j++) {
                if ((ULONG)config[at[j] + 2] == first) {
                    iface = at[j];
                }
            }
        } else if (cls == 0x0DUL) {
            taken |= mask;          /* Content Security: no function     */
            continue;
        } else if (cls == 0x01UL && iads == 0) {
            /* Positional, as Microsoft's parent is: baInterfaceNr is not
             * read (design record 13 section 10.8). Any IAD in the
             * configuration turns the audio rule off for every interface,
             * per Microsoft's grouping hierarchy ("Support for interface
             * collections"). */
            sub = (ULONG)config[at[i] + 6];
            for (j = i + 1; j < count; j++) {
                m = (ULONG)config[at[j] + 2];
                if ((ULONG)config[at[j] + 5] != 0x01UL ||
                    (ULONG)config[at[j] + 6] == sub) {
                    break;
                }
                mask |= XHCI_FUNC_BIT(m);
            }
        }
        if (set->Count >= XHCI_FUNC_MAX) {
            set->Count = 0;
            return XHCI_FUNC_NO_SPLIT;
        }
        f = &set->Func[set->Count++];
        f->FirstInterface = (ULONG)config[iface + 2];
        f->InterfaceMask = mask;
        f->InterfaceCount = xhciFuncCount(mask);
        f->Class = (ULONG)config[iface + 5];
        f->SubClass = (ULONG)config[iface + 6];
        f->Protocol = (ULONG)config[iface + 7];
        f->IadOffset = (k < iads) ? iadAt[k] : 0;
        f->DeviceMask = present;
        f->StringIndex = (ULONG)config[iface + 8];
        if (k < iads) {
            /* Microsoft's compatible ids for an IAD function ("Support for
             * interface collections"). */
            f->Class = (ULONG)config[iadAt[k] + 4];
            f->SubClass = (ULONG)config[iadAt[k] + 5];
            f->Protocol = (ULONG)config[iadAt[k] + 6];
            if (config[iadAt[k] + 7] != 0) {
                f->StringIndex = (ULONG)config[iadAt[k] + 7];
            }
        }
        taken |= mask;
    }
    return (set->Count != 0) ? XHCI_FUNC_OK : XHCI_FUNC_NO_SPLIT;
}

/* Whether the descriptor at `offset` opens a run the function keeps; any
 * other descriptor inherits the run's verdict. */
static ULONG xhciFuncKeeps(const UCHAR *config, ULONG offset,
                           const XHCI_FUNC *func, ULONG keep)
{
    ULONG bType;
    ULONG n;

    bType = (ULONG)config[offset + 1];
    if (bType == XHCI_FUNC_DT_INTERFACE) {
        n = (ULONG)config[offset + 2];
        return n < 32UL && (func->InterfaceMask & XHCI_FUNC_BIT(n)) != 0;
    }
    if (bType == XHCI_FUNC_DT_IAD) {
        return func->IadOffset != 0 && offset == func->IadOffset;
    }
    return keep;
}

ULONG XhciFuncConfig(const UCHAR *config, ULONG length, const XHCI_FUNC *func,
                     UCHAR *out, ULONG capacity, PULONG total)
{
    ULONG whole;
    ULONG offset;
    ULONG size;
    ULONG keep;
    ULONG put;
    ULONG i;
    UCHAR b;

    if (config == NULL || func == NULL || total == NULL ||
        (out == NULL && capacity != 0)) {
        return XHCI_FUNC_BAD_PARAM;
    }
    whole = xhciFuncTotal(config, length);
    if (whole == 0 || !xhciFuncSound(config, whole)) {
        return XHCI_FUNC_MALFORMED;
    }
    /* Descriptors ahead of the first interface or IAD (an OTG descriptor,
     * a vendor one) belong to the configuration, not to a function. */
    size = (ULONG)config[0];
    keep = 0;
    for (offset = (ULONG)config[0]; offset < whole;
         offset += (ULONG)config[offset]) {
        keep = xhciFuncKeeps(config, offset, func, keep);
        if (keep) {
            size += (ULONG)config[offset];
        }
    }
    *total = size;

    put = 0;
    for (i = 0; i < (ULONG)config[0]; i++) {
        b = config[i];
        if (i == 2) {
            b = (UCHAR)(size & 0xFFUL);
        } else if (i == 3) {
            b = (UCHAR)((size >> 8) & 0xFFUL);
        } else if (i == 4) {
            b = (UCHAR)(func->InterfaceCount & 0xFFUL);
        }
        if (put < capacity) {
            out[put] = b;
        }
        put++;
    }
    keep = 0;
    for (offset = (ULONG)config[0]; offset < whole;
         offset += (ULONG)config[offset]) {
        keep = xhciFuncKeeps(config, offset, func, keep);
        for (i = 0; keep && i < (ULONG)config[offset]; i++) {
            if (put < capacity) {
                out[put] = config[offset + i];
            }
            put++;
        }
    }
    return XHCI_FUNC_OK;
}

ULONG XhciFuncSetupAllowed(const UCHAR *setup, ULONG interfaceMask,
                           ULONG deviceMask)
{
    ULONG type;
    ULONG n;

    if (setup == NULL) {
        return 0;
    }
    if (((ULONG)setup[0] & 0x1FUL) != 1UL) {
        return 1;                   /* not an interface recipient        */
    }
    type = ((ULONG)setup[0] >> 5) & 3UL;
    if (type != 0 && type != 1UL) {
        return 1;                   /* vendor: the function's business   */
    }
    n = (ULONG)setup[4];
    if (n >= 32UL || (deviceMask & XHCI_FUNC_BIT(n)) == 0) {
        return 1;
    }
    return (interfaceMask & XHCI_FUNC_BIT(n)) != 0;
}

/* The id text, counted past the capacity so the caller learns the size. */
typedef struct _XHCI_FUNC_TEXT {
    char *Out;
    ULONG Capacity;
    ULONG Used;
} XHCI_FUNC_TEXT;

static const char xhciFuncHex[] = "0123456789ABCDEF";

static VOID xhciFuncChar(XHCI_FUNC_TEXT *t, char c)
{
    if (t->Used < t->Capacity) {
        t->Out[t->Used] = c;
    }
    t->Used++;
}

static VOID xhciFuncStr(XHCI_FUNC_TEXT *t, const char *s)
{
    while (*s != 0) {
        xhciFuncChar(t, *s++);
    }
}

static VOID xhciFuncHexN(XHCI_FUNC_TEXT *t, ULONG value, ULONG digits)
{
    while (digits > 0) {
        digits--;
        xhciFuncChar(t, xhciFuncHex[(value >> (digits * 4UL)) & 0xFUL]);
    }
}

static VOID xhciFuncDec(XHCI_FUNC_TEXT *t, ULONG value)
{
    char digits[11];
    ULONG d;

    d = 0;
    do {
        digits[d++] = (char)('0' + (value % 10UL));
        value /= 10UL;
    } while (value != 0 && d < 11UL);
    while (d > 0) {
        xhciFuncChar(t, digits[--d]);
    }
}

static VOID xhciFuncVidPid(XHCI_FUNC_TEXT *t, const UCHAR *device)
{
    xhciFuncStr(t, "USB\\VID_");
    xhciFuncHexN(t, xhciFuncWord(device + 8), 4);
    xhciFuncStr(t, "&PID_");
    xhciFuncHexN(t, xhciFuncWord(device + 10), 4);
}

static VOID xhciFuncMi(XHCI_FUNC_TEXT *t, const XHCI_FUNC *func)
{
    xhciFuncStr(t, "&MI_");
    xhciFuncHexN(t, func->FirstInterface, 2);
}

ULONG XhciFuncId(const UCHAR *device, const XHCI_FUNC *func, ULONG port,
                 ULONG which, char *out, ULONG capacity, PULONG used)
{
    XHCI_FUNC_TEXT t;
    ULONG depth;

    if (device == NULL || func == NULL || used == NULL ||
        (out == NULL && capacity != 0)) {
        return XHCI_FUNC_BAD_PARAM;
    }
    t.Out = out;
    t.Capacity = capacity;
    t.Used = 0;
    switch (which) {
    case XHCI_FUNC_ID_DEVICE:
        xhciFuncVidPid(&t, device);
        xhciFuncMi(&t, func);
        xhciFuncChar(&t, 0);
        break;
    case XHCI_FUNC_ID_HARDWARE:
        xhciFuncVidPid(&t, device);
        xhciFuncStr(&t, "&REV_");
        xhciFuncHexN(&t, xhciFuncWord(device + 12), 4);
        xhciFuncMi(&t, func);
        xhciFuncChar(&t, 0);
        xhciFuncVidPid(&t, device);
        xhciFuncMi(&t, func);
        xhciFuncChar(&t, 0);
        xhciFuncChar(&t, 0);
        break;
    case XHCI_FUNC_ID_COMPATIBLE:
        for (depth = 3; depth != 0; depth--) {
            xhciFuncStr(&t, "USB\\Class_");
            xhciFuncHexN(&t, func->Class, 2);
            if (depth >= 2) {
                xhciFuncStr(&t, "&SubClass_");
                xhciFuncHexN(&t, func->SubClass, 2);
            }
            if (depth >= 3) {
                xhciFuncStr(&t, "&Prot_");
                xhciFuncHexN(&t, func->Protocol, 2);
            }
            xhciFuncChar(&t, 0);
        }
        xhciFuncChar(&t, 0);
        break;
    case XHCI_FUNC_ID_INSTANCE:
        /* The location form: digits and A-F only (design record 13
         * section 10.10). */
        xhciFuncDec(&t, port);
        xhciFuncHexN(&t, func->FirstInterface, 2);
        xhciFuncChar(&t, 0);
        break;
    default:
        return XHCI_FUNC_BAD_PARAM;
    }
    *used = t.Used;
    return (t.Used > capacity) ? XHCI_FUNC_TOO_SMALL : XHCI_FUNC_OK;
}

/* ----------------------------------------------------------------------- */
/* Instance ids (task 33.2)                                                 */
/* ----------------------------------------------------------------------- */

static ULONG xhciFuncSerialChar(ULONG c)
{
    return c >= 0x21UL && c <= 0x7EUL && c != (ULONG)',' &&
           c != (ULONG)'\\';
}

ULONG XhciFuncSerialId(const UCHAR *desc, ULONG bytes, char *out,
                       ULONG capacity)
{
    ULONG length;
    ULONG count;
    ULONG c;
    ULONG i;

    if (desc == NULL || out == NULL || capacity < XHCI_SERIAL_ID_BYTES) {
        return XHCI_FUNC_BAD_PARAM;
    }
    out[0] = 0;
    if (bytes < 2 || desc[1] != 3) {
        return XHCI_FUNC_MALFORMED;
    }
    length = desc[0];
    if (length < 2 || length > bytes) {
        return XHCI_FUNC_MALFORMED;
    }
    count = (length - 2UL) / 2UL;
    if (count == 0) {
        return XHCI_FUNC_BAD_SERIAL;
    }
    for (i = 0; i < count; i++) {
        c = (ULONG)desc[2 + i * 2] | ((ULONG)desc[3 + i * 2] << 8);
        if (!xhciFuncSerialChar(c)) {
            out[0] = 0;
            return XHCI_FUNC_BAD_SERIAL;
        }
        out[i] = (char)c;
    }
    out[count] = 0;
    return XHCI_FUNC_OK;
}

static ULONG xhciFuncUpper(ULONG c)
{
    return (c >= (ULONG)'a' && c <= (ULONG)'z') ? c - 0x20UL : c;
}

ULONG XhciFuncSerialSame(const char *a, const char *b)
{
    ULONG i;

    if (a == NULL || b == NULL || a[0] == 0 || b[0] == 0) {
        return 0;
    }
    for (i = 0; a[i] != 0 || b[i] != 0; i++) {
        if (xhciFuncUpper((ULONG)(UCHAR)a[i]) !=
            xhciFuncUpper((ULONG)(UCHAR)b[i])) {
            return 0;
        }
    }
    return 1;
}

static ULONG xhciFuncExact(const char *a, const char *b)
{
    ULONG i;

    for (i = 0; i < XHCI_SERIAL_ID_BYTES; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == 0) {
            break;
        }
    }
    return 1;
}

ULONG XhciFuncReviveByPlace(const char *oldSerial, const char *oldRead,
                            ULONG oldUnread, const char *newRead,
                            ULONG newUnread, ULONG newLocation)
{
    if (oldSerial == NULL || oldRead == NULL || newRead == NULL ||
        oldSerial[0] != 0) {
        return 0;
    }
    return newUnread || (oldUnread && newLocation) ||
           xhciFuncExact(oldRead, newRead);
}

ULONG XhciFuncReviveBySerial(const char *oldSerial, const char *newSerial)
{
    if (oldSerial == NULL || newSerial == NULL || newSerial[0] == 0) {
        return 0;
    }
    return xhciFuncExact(oldSerial, newSerial);
}

ULONG XhciFuncInstanceId(const char *serial, ULONG location, ULONG mi,
                         char *out, ULONG capacity, PULONG used)
{
    XHCI_FUNC_TEXT t;
    ULONG i;

    if (used == NULL || (out == NULL && capacity != 0) ||
        (mi != XHCI_INSTANCE_NO_MI && mi > 0xFFUL)) {
        return XHCI_FUNC_BAD_PARAM;
    }
    t.Out = out;
    t.Capacity = capacity;
    t.Used = 0;
    if (serial != NULL && serial[0] != 0) {
        for (i = 0; serial[i] != 0 && i < XHCI_SERIAL_ID_CHARS; i++) {
            xhciFuncChar(&t, serial[i]);
        }
        if (mi != XHCI_INSTANCE_NO_MI) {
            xhciFuncChar(&t, '&');
            xhciFuncHexN(&t, mi, 2);
        }
    } else {
        xhciFuncDec(&t, location);
        if (mi != XHCI_INSTANCE_NO_MI) {
            xhciFuncHexN(&t, mi, 2);
        }
    }
    xhciFuncChar(&t, 0);
    *used = t.Used;
    return (t.Used > capacity) ? XHCI_FUNC_TOO_SMALL : XHCI_FUNC_OK;
}
