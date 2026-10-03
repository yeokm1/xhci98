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
ULONG XhciHubResetProgress(ULONG status, ULONG change)
{
    if ((status & XHCI_HUB_PORT_CONNECTION) == 0) {
        return XHCI_HUB_RESET_FAILED;
    }
    if ((change & XHCI_HUB_C_PORT_RESET) == 0 &&
        (status & XHCI_HUB_PORT_RESET) != 0) {
        return XHCI_HUB_RESET_PENDING;
    }
    if ((change & XHCI_HUB_C_PORT_RESET) == 0 &&
        (status & XHCI_HUB_PORT_ENABLE) == 0) {
        /* Neither the change nor the enable yet: the reset bit has not been
         * seen set either, so the hub may not have started it. */
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
