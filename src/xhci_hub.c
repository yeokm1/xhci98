/*
 * xhci_hub.c - the hub class inside the bus, its pure half (xhci_hub.h;
 * roadmap-hcd.md task 27-A.1; design record 13 sections 10.1 to 10.3).
 *
 * DDK-free: part of the pure core. IRQL: any.
 */

#include "xhci.h"
#include "xhci_enum.h"
#include "xhci_hub.h"

/* IRQL: any. */
ULONG XhciHubManagedPorts(ULONG declared)
{
    return (declared > XHCI_HUB_MAX_PORTS) ? XHCI_HUB_MAX_PORTS : declared;
}

/* IRQL: any. */
ULONG XhciHubParseDescriptor(const UCHAR *data, ULONG length,
                             PXHCI_HUB_DESC out)
{
    ULONG declared;

    if (data == NULL || out == NULL) {
        return XHCI_HUB_BAD_PARAM;
    }
    out->Ports = 0;
    out->Characteristics = 0;
    out->PowerGoodMs = 0;
    out->ControllerCurrent = 0;
    out->ThinkTime = 0;
    out->Managed = 0;
    if (length < XHCI_HUB_DESC_MIN_BYTES) {
        return XHCI_HUB_MALFORMED;
    }
    declared = (ULONG)data[0];
    if (declared < XHCI_HUB_DESC_MIN_BYTES || declared > length ||
        (ULONG)data[1] != XHCI_HUB_DESC_TYPE || data[2] == 0) {
        return XHCI_HUB_MALFORMED;
    }
    out->Ports = (ULONG)data[2];
    out->Characteristics = (ULONG)data[3] | ((ULONG)data[4] << 8);
    out->PowerGoodMs = (ULONG)data[5] * 2UL;
    out->ControllerCurrent = (ULONG)data[6];
    out->ThinkTime = (out->Characteristics >> 5) & 0x3UL;
    out->Managed = XhciHubManagedPorts(out->Ports);
    return XHCI_HUB_OK;
}

/* IRQL: any. */
ULONG XhciHubStatusBytes(ULONG declared)
{
    ULONG bytes;

    bytes = (declared + 1UL + 7UL) / 8UL;
    if (bytes > XHCI_HUB_STATUS_MAX_BYTES) {
        bytes = XHCI_HUB_STATUS_MAX_BYTES;
    }
    return bytes;
}

/* IRQL: any. */
ULONG XhciHubAllBits(ULONG managed)
{
    if (managed > XHCI_HUB_MAX_PORTS) {
        managed = XHCI_HUB_MAX_PORTS;
    }
    return (1UL << (managed + 1UL)) - 1UL;
}

/* IRQL: any. */
ULONG XhciHubStatusBitmap(const UCHAR *data, ULONG bytes, ULONG managed)
{
    ULONG mask;
    ULONG bit;

    if (data == NULL) {
        return 0;
    }
    mask = 0;
    for (bit = 0; bit <= managed && bit <= XHCI_HUB_MAX_PORTS; bit++) {
        if (bit / 8UL < bytes &&
            (data[bit / 8UL] & (1U << (bit % 8UL))) != 0) {
            mask |= 1UL << bit;
        }
    }
    return mask;
}

/* IRQL: any. */
ULONG XhciHubClearSelector(ULONG changeBit)
{
    switch (changeBit) {
    case XHCI_HUB_C_PORT_CONNECTION:
        return XHCI_HUB_FEAT_C_PORT_CONNECTION;
    case XHCI_HUB_C_PORT_ENABLE:
        return XHCI_HUB_FEAT_C_PORT_ENABLE;
    case XHCI_HUB_C_PORT_SUSPEND:
        return XHCI_HUB_FEAT_C_PORT_SUSPEND;
    case XHCI_HUB_C_PORT_OVER_CURRENT:
        return XHCI_HUB_FEAT_C_PORT_OVER_CURRENT;
    case XHCI_HUB_C_PORT_RESET:
        return XHCI_HUB_FEAT_C_PORT_RESET;
    default:
        return 0;
    }
}

/* Whether the machine holds something a disconnect takes away. */
static ULONG xhciHubHolds(ULONG state)
{
    return state != XHCI_ENUM_EMPTY && state != XHCI_ENUM_FAILED &&
           state != XHCI_ENUM_GONE;
}

/* IRQL: any. */
VOID XhciHubPortDecide(ULONG state, ULONG status, ULONG change,
                       PXHCI_HUB_PORT_DECISION out)
{
    ULONG connected;

    if (out == NULL) {
        return;
    }
    out->Clear = change & XHCI_HUB_C_PORT_MASK;
    out->Disconnect = 0;
    out->Connect = 0;
    out->OverCurrent = 0;
    out->Repower = 0;
    out->Suspended = (change & XHCI_HUB_C_PORT_SUSPEND) != 0;
    connected = (status & XHCI_HUB_PORT_CONNECTION) != 0;
    /* Handled, not initiated (xhci_hub.h): a connected port the hub reports
     * suspended is resumed before anything else is asked of it. */
    out->Resume = connected && (status & XHCI_HUB_PORT_SUSPEND) != 0;
    out->Retry = 0;
    out->GaveUp = 0;

    if ((change & XHCI_HUB_C_PORT_OVER_CURRENT) != 0) {
        out->OverCurrent = 1;
        out->Repower = (status & XHCI_HUB_PORT_POWER) == 0;
    }
    if ((change & XHCI_HUB_C_PORT_CONNECTION) != 0) {
        out->Disconnect = xhciHubHolds(state);
        out->Connect = connected;
        return;
    }
    if (!connected) {
        /* The machine ignores a disconnect it holds nothing for, so a port
         * merely confirmed empty asks for nothing (xhci_enum.c). */
        out->Disconnect = state != XHCI_ENUM_EMPTY &&
                          state != XHCI_ENUM_GONE;
        return;
    }
    if ((change & XHCI_HUB_C_PORT_ENABLE) != 0 &&
        (status & XHCI_HUB_PORT_ENABLE) == 0 && xhciHubHolds(state)) {
        out->Disconnect = 1;
        out->Connect = 1;
        return;
    }
    if (state == XHCI_ENUM_EMPTY) {
        out->Connect = 1;
    }
}

/* IRQL: any. */
ULONG XhciHubResumeBeforeReset(ULONG status)
{
    return (status & XHCI_HUB_PORT_CONNECTION) != 0 &&
           (status & XHCI_HUB_PORT_SUSPEND) != 0;
}

/* IRQL: any. */
ULONG XhciHubResumeProgress(ULONG status)
{
    if ((status & XHCI_HUB_PORT_CONNECTION) == 0) {
        return XHCI_HUB_RESUME_GONE;
    }
    if ((status & XHCI_HUB_PORT_SUSPEND) != 0) {
        return XHCI_HUB_RESUME_PENDING;
    }
    /* A clear suspend bit alone is not a resume: the port must be enabled
     * (Codex review of the Phase 27 integration, round 4, finding 3). */
    return ((status & XHCI_HUB_PORT_ENABLE) != 0) ? XHCI_HUB_RESUME_DONE
                                                  : XHCI_HUB_RESUME_DISABLED;
}

/* IRQL: any. */
ULONG XhciHubResumeSettle(ULONG progress, ULONG change)
{
    if (progress == XHCI_HUB_RESUME_DONE &&
        (change & XHCI_HUB_C_PORT_CONNECTION) != 0) {
        return XHCI_HUB_RESUME_DISABLED;
    }
    return progress;
}

/* IRQL: any. */
VOID XhciHubResumeOutcome(ULONG state, ULONG outcome, ULONG held,
                          PULONG tries, PXHCI_HUB_PORT_DECISION d)
{
    if (tries == NULL || d == NULL) {
        return;
    }
    d->Retry = 0;
    d->GaveUp = 0;
    switch (outcome) {
    case XHCI_HUB_RESUME_DONE:
        *tries = 0;
        break;
    case XHCI_HUB_RESUME_DISABLED:
        *tries = 0;
        d->Disconnect = xhciHubHolds(state);
        d->Connect = 1;
        break;
    case XHCI_HUB_RESUME_GONE:
        *tries = 0;
        d->Disconnect = xhciHubHolds(state);
        d->Connect = 0;
        break;
    default:
        if (held) {
            *tries = 0;
            d->GaveUp = 1;
            d->Disconnect = 1;
            d->Connect = 1;
            break;
        }
        (*tries)++;
        if (*tries < XHCI_HUB_RESUME_TRIES) {
            d->Retry = 1;
            d->Disconnect = 0;
            d->Connect = 0;
        } else {
            *tries = 0;
            d->GaveUp = 1;
            d->Disconnect = xhciHubHolds(state);
            d->Connect = 1;
        }
        break;
    }
}

/* IRQL: any. */
ULONG XhciHubResetProgress(ULONG status, ULONG change)
{
    if ((status & XHCI_HUB_PORT_CONNECTION) == 0) {
        return XHCI_HUB_RESET_FAILED;
    }
    /* Only a fresh C_PORT_RESET with the reset bit clear says this reset
     * ended: the caller cleared any older one before starting it, and an
     * enabled port with neither may be one the hub has not reset yet (Codex
     * review of 23e7715, finding 5; USB 2.0 11.24.2.7.2.5, to transcribe). */
    if ((change & XHCI_HUB_C_PORT_RESET) == 0 ||
        (status & XHCI_HUB_PORT_RESET) != 0) {
        return XHCI_HUB_RESET_PENDING;
    }
    return ((status & XHCI_HUB_PORT_ENABLE) != 0) ? XHCI_HUB_RESET_ENABLED
                                                  : XHCI_HUB_RESET_FAILED;
}

/* IRQL: any. */
ULONG XhciHubPortSpeedClass(ULONG status)
{
    if ((status & XHCI_HUB_PORT_LOW_SPEED) != 0) {
        return XHCI_SPEED_LOW;
    }
    if ((status & XHCI_HUB_PORT_HIGH_SPEED) != 0) {
        return XHCI_SPEED_HIGH;
    }
    return XHCI_SPEED_FULL;
}

/* IRQL: any. */
ULONG XhciHubEnumSpeed(ULONG speedClass)
{
    switch (speedClass) {
    case XHCI_SPEED_LOW:
        return XHCI_ENUM_SPEED_LOW;
    case XHCI_SPEED_FULL:
        return XHCI_ENUM_SPEED_FULL;
    case XHCI_SPEED_HIGH:
        return XHCI_ENUM_SPEED_HIGH;
    case XHCI_SPEED_SUPER:
        return XHCI_ENUM_SPEED_SUPER;
    default:
        return 0;
    }
}

/* IRQL: any. */
ULONG XhciHubTierServable(ULONG tier)
{
    return tier + 1UL <= (ULONG)XHCI_TOPO_MAX_TIER;
}

/* IRQL: any. */
ULONG XhciHubWantsMultiTt(ULONG speedClass, ULONG deviceProtocol,
                          ULONG hasAlternate1)
{
    return speedClass == XHCI_SPEED_HIGH &&
           deviceProtocol == XHCI_HUB_PROTOCOL_MULTI_TT && hasAlternate1;
}

/* IRQL: any. */
ULONG XhciHubInstanceKey(ULONG rootPort, ULONG route)
{
    return ((route & 0xFFFFFUL) << 8) | (rootPort & 0xFFUL);
}

/* IRQL: any. */
ULONG XhciHubPowerWaitMs(ULONG powerGoodMs)
{
    if (powerGoodMs < 20UL) {
        return 20UL;
    }
    if (powerGoodMs > 1000UL) {
        return 1000UL;
    }
    return powerGoodMs;
}

/* IRQL: any. */
ULONG XhciHubClearTtValue(ULONG address, ULONG endpoint, ULONG type,
                          ULONG in, PULONG value)
{
    if (value == NULL) {
        return 0;
    }
    *value = 0;
    if ((type != XHCI_HUB_TT_EP_CONTROL && type != XHCI_HUB_TT_EP_BULK) ||
        address > 127UL || endpoint > 15UL) {
        return 0;
    }
    *value = endpoint | (address << 4) | (type << 11) | (in ? 0x8000UL : 0);
    return 1;
}

/* IRQL: any. */
ULONG XhciHubClearTtPort(ULONG multiTt, ULONG ttPort)
{
    return multiTt ? ttPort : 1UL;
}

/* A hub's distance below `top`, or XHCI_HUB_DETACHED when `top` is not on
 * its chain within `count` steps. IRQL: any. */
static ULONG xhciHubDepthBelow(const ULONG *parent, ULONG count, ULONG top,
                               ULONG hub)
{
    ULONG depth;
    ULONG at;

    at = hub;
    for (depth = 0; depth < count; depth++) {
        if (at == top) {
            return depth;
        }
        if (parent[at] == XHCI_HUB_DETACHED ||
            parent[at] == XHCI_HUB_NO_PARENT || parent[at] >= count) {
            return XHCI_HUB_DETACHED;
        }
        at = parent[at];
    }
    return XHCI_HUB_DETACHED;
}

/* IRQL: any. */
ULONG XhciHubReleaseOrder(const ULONG *parent, ULONG count, ULONG top,
                          PULONG order)
{
    ULONG written;
    ULONG d;
    ULONG i;

    if (parent == NULL || order == NULL || top >= count ||
        parent[top] == XHCI_HUB_DETACHED) {
        return 0;
    }
    written = 0;
    /* Deepest first: a hub's children are one deeper than it, so each is
     * written before it. */
    for (d = count; d > 0; d--) {
        for (i = 0; i < count; i++) {
            if (xhciHubDepthBelow(parent, count, top, i) == d - 1UL) {
                order[written++] = i;
            }
        }
    }
    return written;
}

/* IRQL: any. */
ULONG XhciHubReportHas(const UCHAR *data, ULONG bytes, ULONG bit)
{
    if (data == NULL || bit / 8UL >= bytes) {
        return 0;
    }
    return (data[bit / 8UL] & (1U << (bit % 8UL))) != 0;
}

/* ----------------------------------------------------------------------- */
/* A hub as a devnode (task 33.4; design record 13 section 10.11)            */
/* ----------------------------------------------------------------------- */

typedef struct _XHCI_HUBPDO_TEXT {
    char *Out;
    ULONG Capacity;
    ULONG Used;
} XHCI_HUBPDO_TEXT;

static VOID xhciHubPdoChar(XHCI_HUBPDO_TEXT *t, char c)
{
    if (t->Used < t->Capacity) {
        t->Out[t->Used] = c;
    }
    t->Used++;
}

static VOID xhciHubPdoStr(XHCI_HUBPDO_TEXT *t, const char *s)
{
    while (*s != 0) {
        xhciHubPdoChar(t, *s++);
    }
}

static VOID xhciHubPdoHex(XHCI_HUBPDO_TEXT *t, ULONG value, ULONG digits)
{
    static const char hex[] = "0123456789ABCDEF";

    while (digits > 0) {
        digits--;
        xhciHubPdoChar(t, hex[(value >> (digits * 4UL)) & 0xFUL]);
    }
}

static VOID xhciHubPdoDec(XHCI_HUBPDO_TEXT *t, ULONG value)
{
    char digits[11];
    ULONG d;

    d = 0;
    do {
        digits[d++] = (char)('0' + (value % 10UL));
        value /= 10UL;
    } while (value != 0 && d < 11UL);
    while (d > 0) {
        xhciHubPdoChar(t, digits[--d]);
    }
}

static VOID xhciHubPdoBase(XHCI_HUBPDO_TEXT *t, ULONG usb3)
{
    xhciHubPdoStr(t, usb3 ? "XHCI98\\HUB30" : "XHCI98\\HUB");
}

static VOID xhciHubPdoVidPid(XHCI_HUBPDO_TEXT *t, const UCHAR *device,
                             ULONG usb3)
{
    xhciHubPdoBase(t, usb3);
    xhciHubPdoStr(t, "&VID_");
    xhciHubPdoHex(t, (ULONG)device[8] | ((ULONG)device[9] << 8), 4);
    xhciHubPdoStr(t, "&PID_");
    xhciHubPdoHex(t, (ULONG)device[10] | ((ULONG)device[11] << 8), 4);
}

/* IRQL: any. */
ULONG XhciHubPdoId(const UCHAR *device, ULONG usb3, ULONG instanceKey,
                   ULONG which, char *out, ULONG capacity, PULONG used)
{
    XHCI_HUBPDO_TEXT t;

    if (device == NULL || used == NULL || (out == NULL && capacity != 0)) {
        return XHCI_HUBPDO_BAD_PARAM;
    }
    t.Out = out;
    t.Capacity = capacity;
    t.Used = 0;
    switch (which) {
    case XHCI_HUBPDO_ID_DEVICE:
        xhciHubPdoVidPid(&t, device, usb3);
        xhciHubPdoChar(&t, 0);
        break;
    case XHCI_HUBPDO_ID_HARDWARE:
        xhciHubPdoVidPid(&t, device, usb3);
        xhciHubPdoStr(&t, "&REV_");
        xhciHubPdoHex(&t, (ULONG)device[12] | ((ULONG)device[13] << 8), 4);
        xhciHubPdoChar(&t, 0);
        xhciHubPdoVidPid(&t, device, usb3);
        xhciHubPdoChar(&t, 0);
        xhciHubPdoBase(&t, usb3);
        xhciHubPdoChar(&t, 0);
        xhciHubPdoChar(&t, 0);
        break;
    case XHCI_HUBPDO_ID_INSTANCE:
        xhciHubPdoDec(&t, instanceKey);
        xhciHubPdoChar(&t, 0);
        break;
    default:
        /* No compatible id, by design (section 10.11). */
        *used = 0;
        return XHCI_HUBPDO_BAD_PARAM;
    }
    *used = t.Used;
    return (t.Used > capacity) ? XHCI_HUBPDO_TOO_SMALL : XHCI_HUBPDO_OK;
}

/* IRQL: any. */
ULONG XhciHubPresentedParent(const ULONG *parent, const ULONG *serial,
                             ULONG count, ULONG start)
{
    ULONG at;
    ULONG steps;

    if (parent == NULL || serial == NULL) {
        return 0;
    }
    at = start;
    for (steps = 0; steps < count; steps++) {
        if (at >= count) {
            return 0;
        }
        if (serial[at] != 0) {
            return serial[at];
        }
        at = parent[at];
    }
    return 0;
}

/* IRQL: any. */
ULONG XhciHubPortLocation(ULONG rootPorts, ULONG perHub, ULONG index,
                          ULONG n)
{
    if (n == 0 || n > perHub) {
        return 0;
    }
    return rootPorts + index * perHub + n;
}

/* IRQL: any. */
VOID XhciHubNodeInfo(const XHCI_HUB_DESC *desc, ULONG busPowered,
                     UCHAR *out)
{
    ULONG ports;
    ULONG maskBytes;
    ULONG chars;
    ULONG i;

    if (out == NULL) {
        return;
    }
    for (i = 0; i < XHCI_HUB_NODE_INFO_BYTES; i++) {
        out[i] = 0;
    }
    ports = (desc != NULL) ? desc->Ports : 0;
    if (ports > 255UL) {
        ports = 255UL;
    }
    chars = (desc != NULL) ? desc->Characteristics : 0;
    maskBytes = (ports + 1UL + 7UL) / 8UL;
    /* NodeType UsbHub (0) in bytes 0 to 3, then USB_HUB_DESCRIPTOR. */
    out[4] = (UCHAR)(7UL + 2UL * maskBytes);
    out[5] = (UCHAR)XHCI_HUB_DESC_TYPE;
    out[6] = (UCHAR)ports;
    out[7] = (UCHAR)(chars & 0xFFUL);
    out[8] = (UCHAR)((chars >> 8) & 0xFFUL);
    out[9] = (UCHAR)(((desc != NULL) ? desc->PowerGoodMs / 2UL : 0) & 0xFFUL);
    out[10] = (UCHAR)(((desc != NULL) ? desc->ControllerCurrent : 0) & 0xFFUL);
    out[75] = (UCHAR)(busPowered ? 1 : 0);
}

/* IRQL: any. */
ULONG XhciHubCapsEx(ULONG speedClass, ULONG multiTtCapable, ULONG multiTtOn)
{
    ULONG flags;

    flags = 0;
    if (speedClass == XHCI_SPEED_HIGH) {
        flags |= 0x3UL;
        if (multiTtCapable) {
            flags |= 0x4UL;
            if (multiTtOn) {
                flags |= 0x8UL;
            }
        }
    }
    return flags;
}
