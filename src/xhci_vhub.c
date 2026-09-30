/*
 * xhci_vhub.c - the virtual USB 2.0 hub's pure core (roadmap task 24.3,
 * design record 12). The contract, and why every function returns a verdict
 * rather than acting, is src/xhci_vhub.h's file header.
 *
 * IRQL: any, every function. Pure computation over caller-supplied state; the
 * caller owns the lock discipline, as for src/xhci_topo.c.
 */

#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_vhub.h"

/* ------------------------------------------------------------------ */
/* The switch and the ids                                              */
/* ------------------------------------------------------------------ */

static ULONG xhciVhubHexDigit(ULONG c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0xFFFFFFFFUL;
}

ULONG XhciVhubParseId(const UCHAR *buffer,
                      ULONG length,
                      ULONG isVendor,
                      USHORT *id,
                      UCHAR *encoding)
{
    ULONG width;
    ULONG count;
    ULONG terminator;
    ULONG i;
    ULONG start;
    ULONG value;
    ULONG c;

    if (id != NULL) {
        *id = 0;
    }
    if (encoding != NULL) {
        *encoding = (UCHAR)XHCI_VHUB_ENC_NONE;
    }
    if (buffer == NULL || length < 2) {
        return XHCI_VHUB_ID_NO_NUL;
    }

    width = (buffer[1] == 0 && buffer[0] != 0) ? 2U : 1U;
    /*
     * An empty string is a terminator in byte 0 in either encoding, and reads
     * as single-byte here; which one it was cannot matter, since it is refused
     * for its digit count whichever it is.
     */
    count = length / width;
    if (encoding != NULL) {
        *encoding = (UCHAR)(width == 2 ? XHCI_VHUB_ENC_UTF16 : XHCI_VHUB_ENC_BYTE);
    }

    terminator = count;
    for (i = 0; i < count; i++) {
        c = buffer[i * width];
        if (width == 2) {
            c |= (ULONG)buffer[i * width + 1] << 8;
        }
        if (c == 0) {
            terminator = i;
            break;
        }
    }
    if (terminator == count) {
        return XHCI_VHUB_ID_NO_NUL;
    }

    start = 0;
    if (terminator >= 2 && buffer[0] == '0' &&
        (buffer[width] == 'x' || buffer[width] == 'X') &&
        (width == 1 || buffer[width + 1] == 0)) {
        start = 2;
    }

    /* Every character is checked before the count, so "12G" is a bad
     * character rather than a short string: the more specific reason. */
    value = 0;
    for (i = start; i < terminator; i++) {
        c = buffer[i * width];
        if (width == 2 && buffer[i * width + 1] != 0) {
            return XHCI_VHUB_ID_CHAR;
        }
        c = xhciVhubHexDigit(c);
        if (c == 0xFFFFFFFFUL) {
            return XHCI_VHUB_ID_CHAR;
        }
        value = (value << 4) | c;
    }
    if (terminator - start != 4) {
        return XHCI_VHUB_ID_DIGITS;
    }
    if (isVendor && value == 0) {
        return XHCI_VHUB_ID_ZERO_VID;
    }
    if (id != NULL) {
        *id = (USHORT)value;
    }
    return XHCI_VHUB_ID_OK;
}

ULONG XhciVhubConfigSwitch(PXHCI_VHUB_CONFIG config,
                           ULONG status,
                           ULONG value)
{
    ULONG i;
    UCHAR *p;

    if (config == NULL) {
        return 0;
    }
    p = (UCHAR *)config;
    for (i = 0; i < sizeof(*config); i++) {
        p[i] = 0;
    }

    config->SwitchStatus = status;
    config->Applied = (UCHAR)XHCI_VHUB_MODE_OFF;
    if (status != MP_STATUS_SUCCESS) {
        return 0;
    }
    config->SwitchValue = value;
    if (value == XHCI_VHUB_MODE_OFF) {
        return 0;
    }
    if (value != XHCI_VHUB_MODE_ON_DEMAND && value != XHCI_VHUB_MODE_ALWAYS) {
        config->Refused = (UCHAR)XHCI_VHUB_WHY_SWITCH;
        return 0;
    }
    return 1;
}

ULONG XhciVhubConfigIds(PXHCI_VHUB_CONFIG config,
                        ULONG vidStatus,
                        const UCHAR *vidBuffer,
                        ULONG pidStatus,
                        const UCHAR *pidBuffer,
                        ULONG length)
{
    ULONG vidResult;
    ULONG pidResult;
    USHORT vid;
    USHORT pid;
    UCHAR vidEncoding;
    UCHAR pidEncoding;

    if (config == NULL) {
        return XHCI_VHUB_MODE_OFF;
    }
    /*
     * Only a switch read that asked for the ids gets them: a value of 1 or 2
     * that has not yet been refused or applied. Anything else is a caller
     * consulting the ids with the switch off, which 3.1 forbids.
     */
    if ((config->SwitchValue != XHCI_VHUB_MODE_ON_DEMAND &&
         config->SwitchValue != XHCI_VHUB_MODE_ALWAYS) ||
        config->SwitchStatus != MP_STATUS_SUCCESS ||
        config->Applied != XHCI_VHUB_MODE_OFF ||
        config->Refused != XHCI_VHUB_WHY_NONE ||
        config->VidResult != XHCI_VHUB_ID_UNREAD) {
        return config->Applied;
    }

    vid = 0;
    pid = 0;
    vidEncoding = (UCHAR)XHCI_VHUB_ENC_NONE;
    pidEncoding = (UCHAR)XHCI_VHUB_ENC_NONE;

    config->VidStatus = vidStatus;
    if (vidStatus != MP_STATUS_SUCCESS) {
        vidResult = XHCI_VHUB_ID_MISSING;
    } else {
        vidResult = XhciVhubParseId(vidBuffer, length, 1, &vid, &vidEncoding);
    }
    config->PidStatus = pidStatus;
    if (pidStatus != MP_STATUS_SUCCESS) {
        pidResult = XHCI_VHUB_ID_MISSING;
    } else {
        pidResult = XhciVhubParseId(pidBuffer, length, 0, &pid, &pidEncoding);
    }

    config->VidResult = (UCHAR)vidResult;
    config->PidResult = (UCHAR)pidResult;
    config->VidEncoding = vidEncoding;
    config->PidEncoding = pidEncoding;

    if (vidResult != XHCI_VHUB_ID_OK) {
        config->Refused = (UCHAR)XHCI_VHUB_WHY_VID;
        return XHCI_VHUB_MODE_OFF;
    }
    if (pidResult != XHCI_VHUB_ID_OK) {
        config->Refused = (UCHAR)XHCI_VHUB_WHY_PID;
        return XHCI_VHUB_MODE_OFF;
    }
    config->Vid = vid;
    config->Pid = pid;
    config->Applied = (UCHAR)config->SwitchValue;
    return config->Applied;
}

/* ------------------------------------------------------------------ */
/* The request table                                                   */
/* ------------------------------------------------------------------ */

/* Request types, whole (src/xhci_topo.h's rule: recipient included). */
#define XHCI_VHUB_RT_STD_IN_DEVICE      0x80U
#define XHCI_VHUB_RT_STD_IN_INTERFACE   0x81U
#define XHCI_VHUB_RT_STD_IN_ENDPOINT    0x82U
#define XHCI_VHUB_RT_STD_OUT_DEVICE     0x00U
#define XHCI_VHUB_RT_STD_OUT_INTERFACE  0x01U
#define XHCI_VHUB_RT_STD_OUT_ENDPOINT   0x02U
#define XHCI_VHUB_RT_HUB_IN             0xA0U
#define XHCI_VHUB_RT_PORT_IN            0xA3U
#define XHCI_VHUB_RT_PORT_OUT           0x23U

/* bRequest (USB 2.0 Tables 9-4 and 11-16). */
#define XHCI_VHUB_R_GET_STATUS          0x00U
#define XHCI_VHUB_R_CLEAR_FEATURE       0x01U
#define XHCI_VHUB_R_SET_FEATURE         0x03U
#define XHCI_VHUB_R_SET_ADDRESS         0x05U
#define XHCI_VHUB_R_GET_DESCRIPTOR      0x06U
#define XHCI_VHUB_R_SET_CONFIGURATION   0x09U
#define XHCI_VHUB_R_SET_INTERFACE       0x0BU
#define XHCI_VHUB_R_CLEAR_TT_BUFFER     0x08U
#define XHCI_VHUB_R_RESET_TT            0x09U
#define XHCI_VHUB_R_GET_TT_STATE        0x0AU
#define XHCI_VHUB_R_STOP_TT             0x0BU

#define XHCI_VHUB_DT_DEVICE             0x01U
#define XHCI_VHUB_DT_CONFIGURATION      0x02U
#define XHCI_VHUB_DT_STRING             0x03U
#define XHCI_VHUB_DT_INTERFACE          0x04U
#define XHCI_VHUB_DT_ENDPOINT           0x05U

#define XHCI_VHUB_HUB_CLASS             0x09U
#define XHCI_VHUB_INTERRUPT_EP          0x81U

/* The product string, 21 characters (3.3). Kept ASCII: the UTF-16 is built
 * from it, so no non-ASCII byte can reach the tree through it. */
static const char xhciVhubProduct[] = "xhci98 virtual HS hub";

static ULONG xhciVhubTruncate(const UCHAR *source,
                              ULONG sourceLength,
                              ULONG wLength,
                              UCHAR *reply)
{
    ULONG count;
    ULONG i;

    count = sourceLength < wLength ? sourceLength : wLength;
    for (i = 0; i < count; i++) {
        reply[i] = source[i];
    }
    return count;
}

static ULONG xhciVhubDeviceDescriptor(const XHCI_VHUB_IDENTITY *identity,
                                      UCHAR *d)
{
    d[0] = (UCHAR)XHCI_VHUB_DEVICE_DESC_LEN;
    d[1] = (UCHAR)XHCI_VHUB_DT_DEVICE;
    d[2] = 0x00;                        /* bcdUSB 0x0200                  */
    d[3] = 0x02;
    d[4] = (UCHAR)XHCI_VHUB_HUB_CLASS;
    d[5] = 0x00;
    d[6] = 0x01;                        /* single TT                      */
    d[7] = 64;
    d[8] = (UCHAR)(identity->Vid & 0xFF);
    d[9] = (UCHAR)(identity->Vid >> 8);
    d[10] = (UCHAR)(identity->Pid & 0xFF);
    d[11] = (UCHAR)(identity->Pid >> 8);
    d[12] = (UCHAR)(identity->BcdDevice & 0xFF);
    d[13] = (UCHAR)(identity->BcdDevice >> 8);
    d[14] = 0;                          /* no manufacturer string         */
    d[15] = 1;                          /* the product string             */
    d[16] = 0;                          /* never a serial number          */
    d[17] = 1;
    return XHCI_VHUB_DEVICE_DESC_LEN;
}

static ULONG xhciVhubConfigDescriptor(UCHAR *d)
{
    /* Configuration */
    d[0] = 9;
    d[1] = (UCHAR)XHCI_VHUB_DT_CONFIGURATION;
    d[2] = (UCHAR)XHCI_VHUB_CONFIG_DESC_LEN;
    d[3] = 0;
    d[4] = 1;                           /* one interface                  */
    d[5] = 1;                           /* bConfigurationValue            */
    d[6] = 0;
    d[7] = 0xC0;                        /* reserved one, self-powered     */
    d[8] = 0;                           /* bMaxPower                      */
    /* Interface 0: a single-TT hub's is protocol 0. */
    d[9] = 9;
    d[10] = (UCHAR)XHCI_VHUB_DT_INTERFACE;
    d[11] = 0;
    d[12] = 0;
    d[13] = 1;
    d[14] = (UCHAR)XHCI_VHUB_HUB_CLASS;
    d[15] = 0;
    d[16] = 0;
    d[17] = 0;
    /* The status-change endpoint. */
    d[18] = 7;
    d[19] = (UCHAR)XHCI_VHUB_DT_ENDPOINT;
    d[20] = (UCHAR)XHCI_VHUB_INTERRUPT_EP;
    d[21] = 0x03;                       /* interrupt                      */
    d[22] = 1;                          /* wMaxPacketSize 1               */
    d[23] = 0;
    d[24] = (UCHAR)XHCI_VHUB_INTERRUPT_INTERVAL;
    return XHCI_VHUB_CONFIG_DESC_LEN;
}

static ULONG xhciVhubHubDescriptor(const XHCI_VHUB_IDENTITY *identity,
                                   UCHAR *d)
{
    ULONG characteristics;

    /* Individual power and over-current, TTT 0 (bits 6:5 clear). */
    characteristics = XHCI_HUB_CHAR_POWER_INDIVIDUAL |
                      XHCI_HUB_CHAR_OC_INDIVIDUAL;
    d[0] = (UCHAR)XHCI_VHUB_HUB_DESC_LEN;
    d[1] = (UCHAR)XHCI_TOPO_DESC_TYPE_HUB;
    d[2] = 1;                           /* bNbrPorts                      */
    d[3] = (UCHAR)(characteristics & 0xFF);
    d[4] = (UCHAR)(characteristics >> 8);
    d[5] = identity->PowerOnToPowerGood;
    d[6] = 0;                           /* bHubContrCurrent               */
    d[7] = 0;                           /* DeviceRemovable: port 1 is     */
    d[8] = 0xFF;                        /* PortPwrCtrlMask, all ones      */
    return XHCI_VHUB_HUB_DESC_LEN;
}

static ULONG xhciVhubStringDescriptor(ULONG index, UCHAR *d)
{
    ULONG i;

    if (index == 0) {
        d[0] = (UCHAR)XHCI_VHUB_LANGID_DESC_LEN;
        d[1] = (UCHAR)XHCI_VHUB_DT_STRING;
        d[2] = (UCHAR)(XHCI_VHUB_LANGID & 0xFF);
        d[3] = (UCHAR)(XHCI_VHUB_LANGID >> 8);
        return XHCI_VHUB_LANGID_DESC_LEN;
    }
    if (index == 1) {
        d[0] = (UCHAR)XHCI_VHUB_PRODUCT_DESC_LEN;
        d[1] = (UCHAR)XHCI_VHUB_DT_STRING;
        for (i = 0; xhciVhubProduct[i] != 0; i++) {
            d[2 + i * 2] = (UCHAR)xhciVhubProduct[i];
            d[3 + i * 2] = 0;
        }
        return XHCI_VHUB_PRODUCT_DESC_LEN;
    }
    return 0;
}

static ULONG xhciVhubPortSelectorSettable(ULONG selector)
{
    return selector == XHCI_VHUB_SEL_PORT_SUSPEND ||
           selector == XHCI_VHUB_SEL_PORT_RESET ||
           selector == XHCI_VHUB_SEL_PORT_POWER;
}

static ULONG xhciVhubPortSelectorClearable(ULONG selector)
{
    return selector == XHCI_VHUB_SEL_PORT_ENABLE ||
           selector == XHCI_VHUB_SEL_PORT_SUSPEND ||
           selector == XHCI_VHUB_SEL_PORT_POWER ||
           (selector >= XHCI_VHUB_SEL_C_PORT_CONNECTION &&
            selector <= XHCI_VHUB_SEL_C_PORT_RESET);
}

ULONG XhciVhubRequest(const XHCI_VHUB_IDENTITY *identity,
                      const XHCI_SETUP_PACKET *setup,
                      UCHAR *reply,
                      ULONG *replyLength,
                      ULONG *arg)
{
    UCHAR built[XHCI_VHUB_REPLY_MAX];
    ULONG length;
    ULONG type;
    ULONG index;
    ULONG port;
    ULONG wValue;
    ULONG wIndex;
    ULONG wLength;

    if (replyLength != NULL) {
        *replyLength = 0;
    }
    if (arg != NULL) {
        *arg = 0;
    }
    if (identity == NULL || setup == NULL || reply == NULL ||
        replyLength == NULL || arg == NULL) {
        return XHCI_VHUB_REQ_STALL;
    }

    wValue = setup->wValue;
    wIndex = setup->wIndex;
    wLength = setup->wLength;
    length = 0;

    switch (setup->bmRequestType) {
    case XHCI_VHUB_RT_STD_IN_DEVICE:
        if (setup->bRequest == XHCI_VHUB_R_GET_STATUS) {
            built[0] = (UCHAR)XHCI_HUB_SELF_POWERED;
            built[1] = 0;
            length = 2;
            break;
        }
        if (setup->bRequest != XHCI_VHUB_R_GET_DESCRIPTOR) {
            return XHCI_VHUB_REQ_STALL;
        }
        type = wValue >> 8;
        index = wValue & 0xFF;
        if (type == XHCI_VHUB_DT_DEVICE && index == 0) {
            length = xhciVhubDeviceDescriptor(identity, built);
        } else if (type == XHCI_VHUB_DT_CONFIGURATION && index == 0) {
            length = xhciVhubConfigDescriptor(built);
        } else if (type == XHCI_VHUB_DT_STRING) {
            /* Answered whatever LANGID wIndex names (3.3). */
            length = xhciVhubStringDescriptor(index, built);
        }
        if (length == 0) {
            return XHCI_VHUB_REQ_STALL;
        }
        break;

    case XHCI_VHUB_RT_STD_IN_INTERFACE:
    case XHCI_VHUB_RT_STD_IN_ENDPOINT:
        if (setup->bRequest != XHCI_VHUB_R_GET_STATUS) {
            return XHCI_VHUB_REQ_STALL;
        }
        if (setup->bmRequestType == XHCI_VHUB_RT_STD_IN_INTERFACE
                ? wIndex != 0
                : (wIndex != 0 && wIndex != XHCI_VHUB_INTERRUPT_EP)) {
            return XHCI_VHUB_REQ_STALL;
        }
        built[0] = 0;
        built[1] = 0;
        length = 2;
        break;

    case XHCI_VHUB_RT_STD_OUT_DEVICE:
        if (setup->bRequest == XHCI_VHUB_R_SET_ADDRESS &&
            wValue >= 1 && wValue <= 127) {
            *arg = wValue;
            return XHCI_VHUB_REQ_SET_ADDRESS;
        }
        if (setup->bRequest == XHCI_VHUB_R_SET_CONFIGURATION && wValue <= 1) {
            *arg = wValue;
            return XHCI_VHUB_REQ_SET_CONFIG;
        }
        return XHCI_VHUB_REQ_STALL;

    case XHCI_VHUB_RT_STD_OUT_INTERFACE:
        if (setup->bRequest == XHCI_VHUB_R_SET_INTERFACE &&
            wValue == 0 && wIndex == 0) {
            return XHCI_VHUB_REQ_OK;
        }
        return XHCI_VHUB_REQ_STALL;

    case XHCI_VHUB_RT_STD_OUT_ENDPOINT:
        /* CLEAR_FEATURE(ENDPOINT_HALT) on EP0 or the interrupt endpoint. */
        if (setup->bRequest == XHCI_VHUB_R_CLEAR_FEATURE && wValue == 0 &&
            (wIndex == 0 || wIndex == XHCI_VHUB_INTERRUPT_EP)) {
            return XHCI_VHUB_REQ_OK;
        }
        return XHCI_VHUB_REQ_STALL;

    case XHCI_VHUB_RT_HUB_IN:
        if (setup->bRequest == XHCI_VHUB_R_GET_STATUS) {
            built[0] = 0;
            built[1] = 0;
            built[2] = 0;
            built[3] = 0;
            length = 4;
            break;
        }
        /*
         * GET_DESCRIPTOR(Hub): both shipping hub drivers send wValue 0, not
         * the 0x2900 the specification names (record 02); either is taken.
         */
        if (setup->bRequest == XHCI_VHUB_R_GET_DESCRIPTOR &&
            (wValue == 0 || wValue == (XHCI_TOPO_DESC_TYPE_HUB << 8))) {
            length = xhciVhubHubDescriptor(identity, built);
            break;
        }
        return XHCI_VHUB_REQ_STALL;

    case XHCI_VHUB_RT_PORT_IN:
        port = wIndex & 0xFF;
        if (setup->bRequest == XHCI_VHUB_R_GET_STATUS && port == 1) {
            return XHCI_VHUB_REQ_PORT_STATUS;
        }
        if (setup->bRequest == XHCI_VHUB_R_GET_TT_STATE) {
            return XHCI_VHUB_REQ_OK;
        }
        return XHCI_VHUB_REQ_STALL;

    case XHCI_VHUB_RT_PORT_OUT:
        port = wIndex & 0xFF;
        if (setup->bRequest == XHCI_VHUB_R_SET_FEATURE && port == 1 &&
            xhciVhubPortSelectorSettable(wValue)) {
            *arg = wValue;
            return XHCI_VHUB_REQ_PORT_SET;
        }
        if (setup->bRequest == XHCI_VHUB_R_CLEAR_FEATURE && port == 1 &&
            xhciVhubPortSelectorClearable(wValue)) {
            *arg = wValue;
            return XHCI_VHUB_REQ_PORT_CLEAR;
        }
        /* The xHC has no translator on a root port to clear (3.3). */
        if (setup->bRequest == XHCI_VHUB_R_CLEAR_TT_BUFFER ||
            setup->bRequest == XHCI_VHUB_R_RESET_TT ||
            setup->bRequest == XHCI_VHUB_R_STOP_TT) {
            return XHCI_VHUB_REQ_OK;
        }
        return XHCI_VHUB_REQ_STALL;

    default:
        return XHCI_VHUB_REQ_STALL;
    }

    *replyLength = xhciVhubTruncate(built, length, wLength, reply);
    return XHCI_VHUB_REQ_DATA;
}

ULONG XhciVhubStatusBytes(ULONG status,
                          ULONG change,
                          ULONG wLength,
                          UCHAR *reply)
{
    UCHAR built[4];

    if (reply == NULL) {
        return 0;
    }
    built[0] = (UCHAR)(status & 0xFF);
    built[1] = (UCHAR)((status >> 8) & 0xFF);
    built[2] = (UCHAR)(change & 0xFF);
    built[3] = (UCHAR)((change >> 8) & 0xFF);
    return xhciVhubTruncate(built, 4, wLength, reply);
}

/* ------------------------------------------------------------------ */
/* The per-port state                                                  */
/* ------------------------------------------------------------------ */

/* At 1, the root-report groups the upstream view owns on a port in
 * virtual-hub mode (record 12 3.2 as amended in 24.3.2). */
#define XHCI_VHUB_UP_STATUS_V1  (XHCI_HUB_PORT_ENABLE | XHCI_HUB_PORT_RESET | \
                                 XHCI_HUB_PORT_SUSPEND)
#define XHCI_VHUB_UP_CHANGE_V1  (XHCI_HUB_C_PORT_ENABLE | XHCI_HUB_C_PORT_RESET | \
                                 XHCI_HUB_C_PORT_SUSPEND)

static VOID xhciVhubClear(PXHCI_VHUB hub)
{
    ULONG i;
    UCHAR *p;

    p = (UCHAR *)hub;
    for (i = 0; i < sizeof(*hub); i++) {
        p[i] = 0;
    }
}

/*
 * The hub's own enumeration bracket resets it twice, and the second reset
 * lands on a hub that holds EP0 at address 0 with the arm already spent - the
 * state `xhciDevEnumerationInProgress` names for a real device (record 02 open
 * question 6). That reset keeps the binding and arms nothing, once per claim
 * (`ResetSuppressed`, as `EnumResetSuppressed` is for a real record), so a hub
 * abandoned mid-enumeration cannot hold its port for ever.
 */
static ULONG xhciVhubSecondReset(const XHCI_VHUB *hub)
{
    return hub->Present && hub->Ep0Bound && !hub->HubOpenArmed &&
           hub->DevState == XHCI_VHUB_DEV_DEFAULT && hub->Address == 0 &&
           !hub->ResetSuppressed;
}

/*
 * Back to the Default state. `rearm` is a hub reset that is not the second of
 * the bracket: a new address-0 open is armed and a device on the port is a
 * connect change on port 1 again, as a real hub's reset power-cycles its ports.
 */
static VOID xhciVhubToDefault(PXHCI_VHUB hub, ULONG rearm, ULONG physStatus)
{
    hub->DevState = (UCHAR)XHCI_VHUB_DEV_DEFAULT;
    hub->Address = 0;
    hub->P1Enabled = 0;
    hub->UpSuspend = 0;
    hub->P1Suspend = 0;
    hub->UpResumeOwed = 0;
    hub->P1ResumeOwed = 0;
    hub->PhysSuspended = 0;
    if (rearm) {
        hub->HubOpenArmed = 1;
        hub->ResetSuppressed = 0;
        hub->P1Changes = (UCHAR)(((physStatus & XHCI_HUB_PORT_CONNECTION) != 0)
                                     ? XHCI_HUB_C_PORT_CONNECTION : 0);
    }
}

static ULONG xhciVhubSuspendPhys(PXHCI_VHUB hub, ULONG physStatus)
{
    if (hub->PhysSuspended || (physStatus & XHCI_HUB_PORT_ENABLE) == 0) {
        return XHCI_VHUB_DO_NONE;
    }
    hub->PhysSuspended = 1;
    return XHCI_VHUB_DO_SUSPEND;
}

/*
 * The existing disable body, unless one is already owed on the port. Asked
 * for whether or not the port still reads enabled: the body's software half -
 * the disown that gives a child's address back - runs unconditionally there
 * for the same reason, since a port that PED already left (a hardware
 * disable, a reset that timed out) has not taken its device out of the
 * address map, and a redundant PED write costs nothing.
 */
static ULONG xhciVhubDisable(ULONG disownPending, ULONG physStatus)
{
    (void)physStatus;
    if (disownPending) {
        return XHCI_VHUB_DO_NONE;
    }
    return XHCI_VHUB_DO_DISABLE;
}

ULONG XhciVhubStart(PXHCI_VHUB hub, ULONG applied, ULONG connected)
{
    if (hub == NULL) {
        return XHCI_VHUB_DO_NONE;
    }
    xhciVhubClear(hub);
    if (applied != XHCI_VHUB_MODE_ALWAYS) {
        return XHCI_VHUB_DO_NONE;
    }
    hub->Present = 1;
    hub->UpPower = 1;
    hub->P1Power = 1;
    hub->HubOpenArmed = 0;
    hub->UpChanges = (UCHAR)XHCI_HUB_C_PORT_CONNECTION;
    if (connected) {
        hub->P1Changes = (UCHAR)XHCI_HUB_C_PORT_CONNECTION;
    }
    return XHCI_VHUB_DO_ROOT_CHANGE;
}

static ULONG xhciVhubRootResetStart(PXHCI_VHUB hub,
                                    ULONG applied,
                                    ULONG generation,
                                    ULONG disownPending,
                                    ULONG physStatus)
{
    ULONG keeps;
    ULONG verdict;

    keeps = xhciVhubSecondReset(hub);
    if (keeps) {
        hub->ResetSuppressed = 1;
    }
    hub->LateEnd = 0;

    if (applied == XHCI_VHUB_MODE_ON_DEMAND) {
        hub->ResetOwner = (UCHAR)XHCI_VHUB_OWNER_ROOT;
        hub->ResetGeneration = generation;
        hub->ResetKeeps = (UCHAR)keeps;
        hub->UpResetting = 1;
        /* A port in reset is not enabled, as PED reads during one today. */
        hub->UpEnabled = 0;
        return XHCI_VHUB_DO_PHYS_RESET;
    }

    /*
     * Value 2: the reset is the hub's, and synthetic (3.8). PR is never set;
     * the root port's C_PORT_RESET is latched now, because what usbhub does
     * next is enumerate the hub, and the device is reached again only through
     * a port-1 reset, which the owed disable holds.
     */
    verdict = XHCI_VHUB_DO_ROOT_CHANGE |
              xhciVhubDisable(disownPending, physStatus);
    if (hub->ResetOwner == XHCI_VHUB_OWNER_PORT1) {
        /*
         * A port-1 reset is still running on the physical port, and the
         * disable above leaves it to run (a reset is the one operation the
         * disable body does not end). Its end, or its deadline, is then
         * nobody's: not the device's claim, which would spend the hub's
         * open armed below, and not port 1's change on a hub back in
         * Default. The generation stays, so the end is still recognised.
         */
        hub->ResetOwner = (UCHAR)XHCI_VHUB_OWNER_SUPERSEDED;
    }
    xhciVhubToDefault(hub, !keeps, physStatus);
    if (!keeps) {
        verdict |= XHCI_VHUB_DO_ARM_HUB;
    }
    hub->UpEnabled = 1;
    hub->UpChanges |= (UCHAR)XHCI_HUB_C_PORT_RESET;
    return verdict;
}

ULONG XhciVhubRootReset(PXHCI_VHUB hub,
                        ULONG applied,
                        ULONG generation,
                        ULONG disownPending,
                        ULONG physStatus)
{
    if (hub == NULL || applied == XHCI_VHUB_MODE_OFF) {
        return XHCI_VHUB_DO_PHYS_RESET;
    }
    if (applied == XHCI_VHUB_MODE_ON_DEMAND) {
        if (hub->ResetOwner != XHCI_VHUB_OWNER_NONE) {
            /*
             * A reset is running on the physical port - the root's or port
             * 1's - and the port is armed with it: the write asked for here
             * is refused as busy, as a root port's second reset is today,
             * and usbport gets the refusal. Nothing here changes hands, so
             * the running reset ends as its own, with its decision or its
             * port-1 change; the refusal's own end, carried under the
             * generation named for it, matches no reset and does nothing.
             */
            return XHCI_VHUB_DO_PHYS_RESET;
        }
        if (!hub->Present) {
            /*
             * Today's reset, unheld as today. It is still owned, because its
             * end is where the decision is taken.
             */
            hub->LateEnd = 0;
            hub->ResetOwner = (UCHAR)XHCI_VHUB_OWNER_ROOT;
            hub->ResetGeneration = generation;
            hub->ResetKeeps = 0;
            return XHCI_VHUB_DO_PHYS_RESET;
        }
        if (disownPending) {
            hub->ResetHeld = (UCHAR)XHCI_VHUB_OWNER_ROOT;
            return XHCI_VHUB_DO_RESET_HELD;
        }
    }
    return xhciVhubRootResetStart(hub, applied, generation, disownPending,
                                  physStatus);
}

static ULONG xhciVhubPort1ResetStart(PXHCI_VHUB hub, ULONG generation)
{
    hub->LateEnd = 0;
    hub->ResetOwner = (UCHAR)XHCI_VHUB_OWNER_PORT1;
    hub->ResetGeneration = generation;
    hub->P1Resetting = 1;
    /* The device's claim and the hub's are exclusive (3.6). */
    hub->HubOpenArmed = 0;
    return XHCI_VHUB_DO_PHYS_RESET | XHCI_VHUB_DO_ARM_DEVICE;
}

ULONG XhciVhubResetDone(PXHCI_VHUB hub,
                        ULONG applied,
                        ULONG generation,
                        ULONG timedOut,
                        ULONG speed,
                        ULONG physStatus)
{
    ULONG owner;
    ULONG decided;
    ULONG verdict;

    if (hub == NULL || applied == XHCI_VHUB_MODE_OFF ||
        hub->ResetOwner == XHCI_VHUB_OWNER_NONE ||
        hub->ResetGeneration != generation) {
        return XHCI_VHUB_DO_NONE;
    }
    owner = hub->ResetOwner;
    hub->ResetOwner = (UCHAR)XHCI_VHUB_OWNER_NONE;

    if (owner == XHCI_VHUB_OWNER_SUPERSEDED) {
        /* The end of a port-1 reset a root reset overtook, or whose hub
         * retired: the port is free again, and nothing else follows from
         * it - including from a PRC after its deadline. */
        hub->P1Resetting = 0;
        if (timedOut) {
            hub->LateEnd = 1;
        }
        return XHCI_VHUB_DO_NONE;
    }
    if (owner == XHCI_VHUB_OWNER_PORT1) {
        hub->P1Resetting = 0;
        if (timedOut) {
            hub->LateEnd = 1;
        }
        hub->P1Enabled = (UCHAR)(!timedOut && hub->P1Power &&
                                 (physStatus & XHCI_HUB_PORT_ENABLE) != 0 &&
                                 hub->DevState == XHCI_VHUB_DEV_CONFIGURED);
        hub->P1Changes |= (UCHAR)XHCI_HUB_C_PORT_RESET;
        return XHCI_VHUB_DO_PIPE;
    }
    if (applied != XHCI_VHUB_MODE_ON_DEMAND) {
        return XHCI_VHUB_DO_NONE;
    }

    hub->UpResetting = 0;
    decided = (!timedOut &&
               (speed == XHCI_SPEED_LOW || speed == XHCI_SPEED_FULL))
                  ? XHCI_VHUB_DECIDED_HUB : XHCI_VHUB_DECIDED_DIRECT;

    /*
     * A re-taken decision that changes the mode, a timeout on a hub-mode port
     * included, is a device usbhub must tear down and enumerate again: the
     * forced connect change of 3.2. Nothing is decided until the next reset.
     */
    if (hub->Decision != XHCI_VHUB_DECIDED_NONE && hub->Decision != decided) {
        verdict = XHCI_VHUB_DO_FORCE_CONNECT;
        if (hub->Present) {
            verdict |= XHCI_VHUB_DO_DROP;
        }
        xhciVhubClear(hub);
        return verdict;
    }
    hub->Decision = (UCHAR)decided;
    if (decided == XHCI_VHUB_DECIDED_DIRECT) {
        return XHCI_VHUB_DO_NONE;
    }

    if (hub->Present && hub->ResetKeeps) {
        hub->ResetKeeps = 0;
        hub->UpEnabled = 1;
        hub->UpChanges |= (UCHAR)XHCI_HUB_C_PORT_RESET;
        return XHCI_VHUB_DO_ROOT_CHANGE;
    }

    /* Created, or re-created from address 0 on a re-enumeration. */
    hub->ResetKeeps = 0;
    hub->Present = 1;
    hub->UpPower = 1;
    hub->UpEnabled = 1;
    hub->P1Power = 1;
    xhciVhubToDefault(hub, 1, physStatus);
    hub->UpChanges |= (UCHAR)XHCI_HUB_C_PORT_RESET;
    return XHCI_VHUB_DO_ROOT_CHANGE | XHCI_VHUB_DO_ARM_HUB;
}

ULONG XhciVhubDisownCollected(PXHCI_VHUB hub,
                              ULONG applied,
                              ULONG generation)
{
    ULONG held;

    if (hub == NULL) {
        return XHCI_VHUB_DO_NONE;
    }
    held = hub->ResetHeld;
    hub->ResetHeld = (UCHAR)XHCI_VHUB_OWNER_NONE;
    if (!hub->Present || applied == XHCI_VHUB_MODE_OFF) {
        return XHCI_VHUB_DO_NONE;
    }
    if (held == XHCI_VHUB_OWNER_ROOT &&
        applied == XHCI_VHUB_MODE_ON_DEMAND) {
        return xhciVhubRootResetStart(hub, applied, generation, 0, 0);
    }
    if (held == XHCI_VHUB_OWNER_PORT1 && hub->P1Power) {
        return xhciVhubPort1ResetStart(hub, generation);
    }
    return XHCI_VHUB_DO_NONE;
}

/*
 * At 1, the device behind the hub has left the port, and the hub with it
 * (task 24.3.4). The record is cleared as usbport's disable clears it (3.2),
 * because on NT 6.x that disable never comes: usbhub removes a disconnected
 * device without one (Windows 7 x86 `usbhub!UsbhPortDisconnect` 0x29967,
 * Vista x64 0x2A5D8, static), and a hub left Present keeps its address after
 * usbport has freed it - the next device given that address on another port
 * opened as this hub. A port-1 reset still running on the port stays
 * recognised, as one nobody owns, so its end claims nothing for the device
 * that has gone and latches nothing on the root port - and so does one whose
 * deadline has already passed (`LateEnd`); a root reset's end is today's, the
 * port having no hub. usbport's disable and power-off of the root port retire
 * the hub the same way, since a disable does not end a reset in flight.
 */
static ULONG xhciVhubRetire(PXHCI_VHUB hub)
{
    ULONG owner;
    ULONG generation;

    ULONG lateEnd;

    owner = hub->ResetOwner;
    generation = hub->ResetGeneration;
    lateEnd = hub->LateEnd;
    xhciVhubClear(hub);
    hub->LateEnd = (UCHAR)lateEnd;
    if (owner == XHCI_VHUB_OWNER_PORT1 ||
        owner == XHCI_VHUB_OWNER_SUPERSEDED) {
        hub->ResetOwner = (UCHAR)XHCI_VHUB_OWNER_SUPERSEDED;
        hub->ResetGeneration = generation;
    }
    return XHCI_VHUB_DO_DROP;
}

ULONG XhciVhubAbsorb(PXHCI_VHUB hub,
                     ULONG applied,
                     ULONG latched,
                     ULONG physStatus,
                     ULONG *strip)
{
    ULONG p1;

    if (strip != NULL) {
        *strip = 0;
    }
    if (hub == NULL || strip == NULL || applied == XHCI_VHUB_MODE_OFF) {
        return XHCI_VHUB_DO_NONE;
    }
    if (!hub->Present) {
        /*
         * A direct port at 1 holds nothing but its decision, and a disconnect
         * forgets that (3.2), so the next device decides afresh with no
         * forced change: a Full-Speed device following a High-Speed one on
         * the same port is the ordinary case, not a flip. A connect change
         * with the port still connected is a device that may have been
         * swapped between two readings, and is forgotten on the same terms.
         */
        if (applied == XHCI_VHUB_MODE_ON_DEMAND &&
            ((latched & XHCI_HUB_C_PORT_CONNECTION) != 0 ||
             (physStatus & XHCI_HUB_PORT_CONNECTION) == 0 ||
             (physStatus & XHCI_HUB_PORT_POWER) == 0)) {
            hub->Decision = (UCHAR)XHCI_VHUB_DECIDED_NONE;
        }
        return XHCI_VHUB_DO_NONE;
    }

    p1 = 0;
    if (applied == XHCI_VHUB_MODE_ON_DEMAND) {
        /*
         * The device gone, or swapped between two readings: the hub goes
         * with it, and the root port reports the reading as today - the
         * connect change usbhub removes the hub for included. Not waiting on
         * the change bit either: an earlier reading may have taken it.
         */
        if ((physStatus & XHCI_HUB_PORT_CONNECTION) == 0 ||
            (latched & XHCI_HUB_C_PORT_CONNECTION) != 0) {
            return xhciVhubRetire(hub);
        }
        /*
         * Connection, power and over-current stay the root port's, as today;
         * the enable, reset and suspend changes are the views' to latch. A
         * lost supply takes the upstream down with it, which is what the
         * physical port reads today.
         */
        *strip = latched & XHCI_VHUB_UP_CHANGE_V1;
        if ((physStatus & XHCI_HUB_PORT_POWER) == 0) {
            hub->UpEnabled = 0;
            hub->UpSuspend = 0;
            hub->Decision = (UCHAR)XHCI_VHUB_DECIDED_NONE;
        }
    } else {
        /* At 2 the root port reports the hub; every physical change is port
         * 1's (3.8). */
        *strip = latched & XHCI_HUB_C_PORT_MASK;
        if ((latched & XHCI_HUB_C_PORT_CONNECTION) != 0) {
            p1 |= XHCI_HUB_C_PORT_CONNECTION;
        }
    }

    if ((physStatus & XHCI_HUB_PORT_CONNECTION) == 0) {
        hub->P1Enabled = 0;
        hub->P1Suspend = 0;
        hub->PhysSuspended = 0;
    }
    if ((latched & XHCI_HUB_C_PORT_ENABLE) != 0 &&
        (physStatus & XHCI_HUB_PORT_ENABLE) == 0 && hub->P1Enabled) {
        hub->P1Enabled = 0;
        p1 |= XHCI_HUB_C_PORT_ENABLE;
    }
    if ((latched & XHCI_HUB_C_PORT_OVER_CURRENT) != 0) {
        p1 |= XHCI_HUB_C_PORT_OVER_CURRENT;
    }
    if (p1 == 0) {
        return XHCI_VHUB_DO_NONE;
    }
    hub->P1Changes |= (UCHAR)p1;
    return XHCI_VHUB_DO_PIPE;
}

VOID XhciVhubRootReport(const XHCI_VHUB *hub,
                        ULONG applied,
                        ULONG todayStatus,
                        ULONG todayChange,
                        ULONG *status,
                        ULONG *change)
{
    ULONG s;
    ULONG c;

    if (status == NULL || change == NULL) {
        return;
    }
    if (hub == NULL || applied == XHCI_VHUB_MODE_OFF || !hub->Present) {
        *status = todayStatus;
        *change = todayChange;
        return;
    }

    if (applied == XHCI_VHUB_MODE_ON_DEMAND) {
        s = todayStatus & ~XHCI_VHUB_UP_STATUS_V1;
        c = todayChange & ~XHCI_VHUB_UP_CHANGE_V1;
        c |= hub->UpChanges & XHCI_VHUB_UP_CHANGE_V1;
    } else {
        s = 0;
        c = hub->UpChanges;
        if (hub->UpPower) {
            s |= XHCI_HUB_PORT_CONNECTION | XHCI_HUB_PORT_POWER |
                 XHCI_HUB_PORT_HIGH_SPEED;
        }
    }
    if (hub->UpEnabled && (applied == XHCI_VHUB_MODE_ON_DEMAND ||
                           hub->UpPower)) {
        s |= XHCI_HUB_PORT_ENABLE;
    }
    if (hub->UpResetting) {
        s |= XHCI_HUB_PORT_RESET;
    }
    if (hub->UpSuspend) {
        s |= XHCI_HUB_PORT_SUSPEND;
    }
    *status = s;
    *change = c & XHCI_HUB_C_PORT_MASK;
}

VOID XhciVhubRootClearChange(PXHCI_VHUB hub, ULONG applied, ULONG bit)
{
    if (hub == NULL || applied == XHCI_VHUB_MODE_OFF || !hub->Present) {
        return;
    }
    hub->UpChanges &= (UCHAR)~bit;
}

/*
 * usbport has let go of the hub (3.8): the record stays, the upstream goes to
 * disabled with no address and no bindings, the held status-change transfer
 * is completed as cancelled, and the physical port leaves service.
 */
static ULONG xhciVhubAbandon(PXHCI_VHUB hub, ULONG physStatus)
{
    hub->UpEnabled = 0;
    hub->UpResetting = 0;
    hub->Ep0Bound = 0;
    hub->HubOpenArmed = 0;
    hub->ResetSuppressed = 0;
    hub->ResetKeeps = 0;
    hub->ResetHeld = (UCHAR)XHCI_VHUB_OWNER_NONE;
    xhciVhubToDefault(hub, 0, physStatus);
    hub->P1Changes = 0;
    return XHCI_VHUB_DO_CANCEL_PIPE;
}

ULONG XhciVhubRootDisable(PXHCI_VHUB hub, ULONG applied, ULONG physStatus)
{
    if (hub == NULL || applied == XHCI_VHUB_MODE_OFF) {
        return XHCI_VHUB_DO_NONE;
    }
    if (applied == XHCI_VHUB_MODE_ON_DEMAND) {
        /* usbport has let go of the port (3.2): with a hub that is the drop,
         * and on a direct port it is the decision forgotten, since the next
         * reset opens a new enumeration whichever device it finds. */
        if (!hub->Present) {
            hub->Decision = (UCHAR)XHCI_VHUB_DECIDED_NONE;
            return XHCI_VHUB_DO_NONE;
        }
        return xhciVhubRetire(hub);
    }
    if (!hub->Present) {
        return XHCI_VHUB_DO_NONE;
    }
    return xhciVhubAbandon(hub, physStatus) | xhciVhubDisable(0, physStatus);
}

ULONG XhciVhubRootPower(PXHCI_VHUB hub,
                        ULONG applied,
                        ULONG on,
                        ULONG physStatus)
{
    if (hub == NULL || applied == XHCI_VHUB_MODE_OFF) {
        return XHCI_VHUB_DO_NONE;
    }
    if (applied == XHCI_VHUB_MODE_ON_DEMAND) {
        if (!on) {
            if (hub->Present) {
                return xhciVhubRetire(hub);
            }
            /* A power-off forgets a direct port's decision, as a disable
             * does: the port is out of usbport's hands either way. */
            hub->Decision = (UCHAR)XHCI_VHUB_DECIDED_NONE;
        }
        return XHCI_VHUB_DO_NONE;
    }
    if (!on) {
        hub->UpPower = 0;
        return xhciVhubAbandon(hub, physStatus) | XHCI_VHUB_DO_POWER_OFF;
    }
    if (hub->UpPower) {
        return XHCI_VHUB_DO_POWER_ON;
    }
    hub->UpPower = 1;
    hub->UpChanges |= (UCHAR)XHCI_HUB_C_PORT_CONNECTION;
    return XHCI_VHUB_DO_POWER_ON | XHCI_VHUB_DO_ROOT_CHANGE;
}

/*
 * 3.5: each view keeps its own bit, the physical port is suspended while
 * either is set, entering suspend latches nothing, a redundant request changes
 * nothing, and a view's C_PORT_SUSPEND means that view's resume finished.
 */
ULONG XhciVhubRootSuspend(PXHCI_VHUB hub,
                          ULONG applied,
                          ULONG set,
                          ULONG physStatus)
{
    if (hub == NULL || applied == XHCI_VHUB_MODE_OFF || !hub->Present) {
        return XHCI_VHUB_DO_NONE;
    }
    if (set) {
        if (hub->UpSuspend) {
            return XHCI_VHUB_DO_NONE;
        }
        hub->UpSuspend = 1;
        return hub->P1Suspend ? XHCI_VHUB_DO_NONE
                              : xhciVhubSuspendPhys(hub, physStatus);
    }
    if (!hub->UpSuspend) {
        return XHCI_VHUB_DO_NONE;
    }
    hub->UpSuspend = 0;
    if (hub->PhysSuspended && !hub->P1Suspend) {
        hub->PhysSuspended = 0;
        hub->UpResumeOwed = 1;
        return XHCI_VHUB_DO_RESUME;
    }
    hub->UpChanges |= (UCHAR)XHCI_HUB_C_PORT_SUSPEND;
    return XHCI_VHUB_DO_ROOT_CHANGE;
}

static ULONG xhciVhubPort1Suspend(PXHCI_VHUB hub, ULONG set, ULONG physStatus)
{
    if (set) {
        /* USB 2.0 11.24.2.7.1.3: only an enabled port can be suspended. */
        if (hub->P1Suspend || !hub->P1Enabled ||
            (physStatus & XHCI_HUB_PORT_ENABLE) == 0) {
            return XHCI_VHUB_DO_NONE;
        }
        hub->P1Suspend = 1;
        return hub->UpSuspend ? XHCI_VHUB_DO_NONE
                              : xhciVhubSuspendPhys(hub, physStatus);
    }
    if (!hub->P1Suspend) {
        return XHCI_VHUB_DO_NONE;
    }
    hub->P1Suspend = 0;
    if (hub->PhysSuspended && !hub->UpSuspend) {
        hub->PhysSuspended = 0;
        hub->P1ResumeOwed = 1;
        return XHCI_VHUB_DO_RESUME;
    }
    hub->P1Changes |= (UCHAR)XHCI_HUB_C_PORT_SUSPEND;
    return XHCI_VHUB_DO_PIPE;
}

ULONG XhciVhubPort1Feature(PXHCI_VHUB hub,
                           ULONG set,
                           ULONG selector,
                           ULONG generation,
                           ULONG disownPending,
                           ULONG physStatus)
{
    if (hub == NULL || !hub->Present) {
        return XHCI_VHUB_DO_NONE;
    }

    if (set) {
        switch (selector) {
        case XHCI_VHUB_SEL_PORT_RESET:
            if (!hub->P1Power) {
                return XHCI_VHUB_DO_NONE;
            }
            if (hub->ResetOwner != XHCI_VHUB_OWNER_NONE) {
                /* A reset already runs on the port: asked for untaken, so
                 * the busy port refuses it and the caller stalls the
                 * request; the running reset keeps its owner and ends as
                 * its own (the root reset rule above). */
                return XHCI_VHUB_DO_PHYS_RESET;
            }
            if (disownPending) {
                hub->ResetHeld = (UCHAR)XHCI_VHUB_OWNER_PORT1;
                return XHCI_VHUB_DO_RESET_HELD;
            }
            return xhciVhubPort1ResetStart(hub, generation);
        case XHCI_VHUB_SEL_PORT_POWER:
            if (hub->P1Power) {
                return XHCI_VHUB_DO_NONE;
            }
            hub->P1Power = 1;
            if ((physStatus & XHCI_HUB_PORT_CONNECTION) != 0) {
                hub->P1Changes |= (UCHAR)XHCI_HUB_C_PORT_CONNECTION;
                return XHCI_VHUB_DO_PIPE;
            }
            return XHCI_VHUB_DO_NONE;
        case XHCI_VHUB_SEL_PORT_SUSPEND:
            return xhciVhubPort1Suspend(hub, 1, physStatus);
        default:
            return XHCI_VHUB_DO_NONE;
        }
    }

    switch (selector) {
    case XHCI_VHUB_SEL_PORT_POWER:
        /* Port 1's own bit, never PORTSC.PP (3.3): the disable body. */
        hub->P1Power = 0;
        /* fall through */
    case XHCI_VHUB_SEL_PORT_ENABLE:
        hub->P1Enabled = 0;
        hub->P1Suspend = 0;
        hub->P1ResumeOwed = 0;
        if (!hub->UpSuspend) {
            hub->PhysSuspended = 0;
        }
        if (hub->ResetHeld == XHCI_VHUB_OWNER_PORT1) {
            /*
             * usbhub has abandoned the reset it asked for and the owed
             * disable is still holding (task 24.3.4): started at the
             * confirmation, it would undo the abandonment. It ends here, as
             * a preempted root-port reset does - reported, since usbhub may
             * still be waiting on its change, with port 1 disabled.
             */
            hub->ResetHeld = (UCHAR)XHCI_VHUB_OWNER_NONE;
            hub->P1Changes |= (UCHAR)XHCI_HUB_C_PORT_RESET;
            return xhciVhubDisable(disownPending, physStatus) |
                   XHCI_VHUB_DO_PIPE;
        }
        return xhciVhubDisable(disownPending, physStatus);
    case XHCI_VHUB_SEL_PORT_SUSPEND:
        return xhciVhubPort1Suspend(hub, 0, physStatus);
    default:
        if (selector >= XHCI_VHUB_SEL_C_PORT_CONNECTION &&
            selector <= XHCI_VHUB_SEL_C_PORT_RESET) {
            hub->P1Changes &= (UCHAR)~(1UL << (selector -
                                        XHCI_VHUB_SEL_C_PORT_CONNECTION));
        }
        return XHCI_VHUB_DO_NONE;
    }
}

VOID XhciVhubPort1Report(const XHCI_VHUB *hub,
                         ULONG physStatus,
                         ULONG speed,
                         ULONG *status,
                         ULONG *change)
{
    ULONG s;

    if (status == NULL || change == NULL) {
        return;
    }
    *status = 0;
    *change = 0;
    if (hub == NULL || !hub->Present) {
        return;
    }
    *change = hub->P1Changes & XHCI_HUB_C_PORT_MASK;
    if (!hub->P1Power) {
        return;
    }

    s = XHCI_HUB_PORT_POWER;
    if ((physStatus & XHCI_HUB_PORT_CONNECTION) != 0) {
        s |= XHCI_HUB_PORT_CONNECTION;
        /* The decoded speed, not the root port's override; neither bit is
         * Full Speed. */
        if (speed == XHCI_SPEED_LOW) {
            s |= XHCI_HUB_PORT_LOW_SPEED;
        } else if (speed == XHCI_SPEED_HIGH) {
            s |= XHCI_HUB_PORT_HIGH_SPEED;
        }
    }
    /*
     * Never enabled without a connection (USB 2.0 11.24.2.7.1). Windows 7's
     * usbhub sends a first-reset change reading enabled and not connected to
     * UsbhHardErrorReset1BadEnable, which hard-resets this hub; not connected
     * and not enabled drops only the device [static; the round-8 x86 dump's
     * exception history repeats that path, debugger] (task 24.3.4 round 9,
     * legal-provenance.md section 4).
     */
    if (hub->P1Enabled && (s & XHCI_HUB_PORT_CONNECTION) != 0 &&
        (physStatus & XHCI_HUB_PORT_ENABLE) != 0 &&
        hub->DevState == XHCI_VHUB_DEV_CONFIGURED) {
        s |= XHCI_HUB_PORT_ENABLE;
    }
    if (hub->P1Resetting) {
        s |= XHCI_HUB_PORT_RESET;
    }
    if ((physStatus & XHCI_HUB_PORT_OVER_CURRENT) != 0) {
        s |= XHCI_HUB_PORT_OVER_CURRENT;
    }
    if (hub->P1Suspend) {
        s |= XHCI_HUB_PORT_SUSPEND;
    }
    *status = s;
}

ULONG XhciVhubResumeDone(PXHCI_VHUB hub)
{
    ULONG verdict;

    if (hub == NULL) {
        return XHCI_VHUB_DO_NONE;
    }
    verdict = XHCI_VHUB_DO_NONE;
    if (hub->UpResumeOwed) {
        hub->UpResumeOwed = 0;
        hub->UpChanges |= (UCHAR)XHCI_HUB_C_PORT_SUSPEND;
        verdict |= XHCI_VHUB_DO_ROOT_CHANGE;
    }
    if (hub->P1ResumeOwed) {
        hub->P1ResumeOwed = 0;
        hub->P1Changes |= (UCHAR)XHCI_HUB_C_PORT_SUSPEND;
        verdict |= XHCI_VHUB_DO_PIPE;
    }
    return verdict;
}

ULONG XhciVhubRemoteWake(PXHCI_VHUB hub)
{
    ULONG verdict;

    if (hub == NULL || !hub->Present) {
        return XHCI_VHUB_DO_NONE;
    }
    verdict = XhciVhubResumeDone(hub);
    hub->PhysSuspended = 0;
    if (hub->UpSuspend) {
        hub->UpSuspend = 0;
        hub->UpChanges |= (UCHAR)XHCI_HUB_C_PORT_SUSPEND;
        verdict |= XHCI_VHUB_DO_ROOT_CHANGE;
    }
    if (hub->P1Suspend) {
        hub->P1Suspend = 0;
        hub->P1Changes |= (UCHAR)XHCI_HUB_C_PORT_SUSPEND;
        verdict |= XHCI_VHUB_DO_PIPE;
    }
    return verdict;
}

ULONG XhciVhubClaimOpen(PXHCI_VHUB hub)
{
    if (hub == NULL || !hub->Present || !hub->HubOpenArmed) {
        return 0;
    }
    hub->HubOpenArmed = 0;
    hub->Ep0Bound = 1;
    return 1;
}

VOID XhciVhubEp0Closed(PXHCI_VHUB hub)
{
    if (hub != NULL) {
        hub->Ep0Bound = 0;
    }
}

VOID XhciVhubSetAddress(PXHCI_VHUB hub, ULONG address)
{
    if (hub == NULL || !hub->Present || address == 0 || address > 127) {
        return;
    }
    hub->Address = (UCHAR)address;
    hub->DevState = (UCHAR)XHCI_VHUB_DEV_ADDRESSED;
}

VOID XhciVhubSetConfig(PXHCI_VHUB hub, ULONG value)
{
    if (hub == NULL || !hub->Present || hub->Address == 0) {
        return;
    }
    if (value == 1) {
        hub->DevState = (UCHAR)XHCI_VHUB_DEV_CONFIGURED;
        return;
    }
    hub->DevState = (UCHAR)XHCI_VHUB_DEV_ADDRESSED;
    hub->P1Enabled = 0;
}

/*
 * The rebuilt shadow times no reset any more, so the record owns none either:
 * left owned, a reset would take the next one on the port for its own - that
 * reset's end deciding nothing and its C_PORT_RESET stripped (task 24.3.4).
 * A hub-less record is included, since a direct port's root reset at 1 and a
 * retired hub's port-1 reset are both owned. After HCRST no PRC can follow;
 * after a restore the registers survived, and a port-1 or nobody's reset may
 * still end - or already have, its PRC pending for the seed - so it becomes a
 * late end rather than being forgotten.
 */
static VOID xhciVhubEndResets(PXHCI_VHUB hub, ULONG restored)
{
    ULONG owner;

    owner = hub->ResetOwner;
    hub->ResetOwner = (UCHAR)XHCI_VHUB_OWNER_NONE;
    hub->ResetHeld = (UCHAR)XHCI_VHUB_OWNER_NONE;
    hub->UpResetting = 0;
    hub->P1Resetting = 0;
    if (!restored) {
        hub->LateEnd = 0;
    } else if (owner == XHCI_VHUB_OWNER_PORT1 ||
               owner == XHCI_VHUB_OWNER_SUPERSEDED) {
        hub->LateEnd = 1;
    }
}

ULONG XhciVhubReinit(PXHCI_VHUB hub,
                     ULONG applied,
                     ULONG deviceLost,
                     ULONG restored)
{
    if (hub == NULL || applied == XHCI_VHUB_MODE_OFF) {
        return XHCI_VHUB_DO_NONE;
    }
    /*
     * A reinitialisation ends every physical operation in flight, at either
     * value: the root hub is rebuilt with its shadows cleared, so a reset
     * held for a disown confirmation (3.3) has nothing left to wait on and
     * would otherwise be held for ever. At 2 the hub's address, bindings and
     * held transfer are software and stay (3.8); at 1 the rest is today's.
     */
    xhciVhubEndResets(hub, restored);
    if (!hub->Present) {
        return XHCI_VHUB_DO_NONE;
    }
    if (!deviceLost || applied != XHCI_VHUB_MODE_ALWAYS) {
        return XHCI_VHUB_DO_NONE;
    }
    hub->P1Enabled = 0;
    hub->P1Suspend = 0;
    hub->P1ResumeOwed = 0;
    hub->PhysSuspended = 0;
    hub->P1Changes |= (UCHAR)XHCI_HUB_C_PORT_CONNECTION;
    return XHCI_VHUB_DO_PIPE;
}

ULONG XhciVhubPipeByte(const XHCI_VHUB *hub)
{
    if (hub == NULL || !hub->Present ||
        hub->DevState != XHCI_VHUB_DEV_CONFIGURED ||
        (hub->P1Changes & XHCI_HUB_C_PORT_MASK) == 0) {
        return 0;
    }
    return 0x02;
}

ULONG XhciVhubFindAddress(const XHCI_VHUB *hubs, ULONG count, ULONG address)
{
    ULONG i;

    if (hubs == NULL || address == 0) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (hubs[i].Present && hubs[i].Address == address) {
            return i + 1;
        }
    }
    return 0;
}
