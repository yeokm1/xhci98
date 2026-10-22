/*
 * xhci_usbdx.c - Windows 8's client contract served by xhci98.sys, the pure
 * half (xhci_usbdx.h; roadmap-usb4 task 43.29; design record 13 section
 * 6.13).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_usbdx.h"

typedef struct _XHCI_USBDX_GUID {
    UCHAR Bytes[16];
    ULONG Id;
} XHCI_USBDX_GUID;

/* The capability GUIDs as they lie in memory, read from the PDB-named
 * symbols of Windows 8's uaspstor.sys, usbhub3.sys and ucx01000.sys
 * (design record 13 section 6.12.3). */
static const XHCI_USBDX_GUID xhciUsbdxGuids[] = {
    { { 0x0d, 0x63, 0x5c, 0x75, 0xa0, 0xc8, 0x65, 0x47,
        0x94, 0x6f, 0x90, 0xcc, 0x70, 0x38, 0x56, 0xa4 },
      XHCI_USBDX_CAP_SELECTIVE_SUSPEND },
    { { 0x23, 0xeb, 0xce, 0xf5, 0x90, 0xad, 0x8c, 0x45,
        0x97, 0x9a, 0xd5, 0x9b, 0x3a, 0xd6, 0x88, 0x4f },
      XHCI_USBDX_CAP_CHAINED_MDLS },
    { { 0x1f, 0x1e, 0x05, 0x09, 0xc9, 0x0d, 0x6b, 0x4e,
        0x8b, 0x12, 0x96, 0x0c, 0xbd, 0x81, 0xaa, 0x8f },
      XHCI_USBDX_CAP_STATIC_STREAMS },
    { { 0x83, 0x31, 0x56, 0xf4, 0x6e, 0xd6, 0xbd, 0x42,
        0xbd, 0x53, 0x1a, 0xc7, 0xb0, 0x4c, 0xd5, 0x9d },
      XHCI_USBDX_CAP_FUNCTION_SUSPEND },
    { { 0xa6, 0x85, 0xa8, 0x81, 0x39, 0xf2, 0xa2, 0x42,
        0x83, 0x97, 0xa5, 0xd9, 0x5b, 0xea, 0x69, 0x53 },
      XHCI_USBDX_CAP_HIGH_SPEED },
    { { 0x6c, 0x77, 0x2f, 0x8a, 0xd0, 0x9b, 0x29, 0x4f,
        0x97, 0x71, 0xc7, 0xa3, 0x88, 0x46, 0xd4, 0xc7 },
      XHCI_USBDX_CAP_SUPER_SPEED },
    { { 0x91, 0x31, 0xe4, 0xcd, 0xad, 0x1f, 0x20, 0x41,
        0xa7, 0xab, 0x74, 0x09, 0x56, 0xc6, 0x6f, 0x37 },
      XHCI_USBDX_CAP_FULL_SPEED },
    { { 0x13, 0x2a, 0x6f, 0xad, 0xd1, 0xc4, 0x25, 0x49,
        0x83, 0xcc, 0x96, 0x85, 0x7a, 0x24, 0x31, 0xd9 },
      XHCI_USBDX_CAP_LOW_SPEED },
    { { 0x52, 0x6d, 0xb7, 0x09, 0x6d, 0x0a, 0x4f, 0x4e,
        0xa9, 0x11, 0xd0, 0x36, 0xd1, 0x92, 0x94, 0x97 },
      XHCI_USBDX_CAP_CLEAR_TT }
};

#define XHCI_USBDX_GUID_COUNT \
    (sizeof(xhciUsbdxGuids) / sizeof(xhciUsbdxGuids[0]))

ULONG XhciUsbdxV1Bytes(ULONG pointerBytes)
{
    if (pointerBytes == 4) {
        return XHCI_USBDX_V1_BYTES_32;
    }
    if (pointerBytes == 8) {
        return XHCI_USBDX_V1_BYTES_64;
    }
    return 0;
}

ULONG XhciUsbdxV1Check(ULONG size, ULONG version, ULONG pointerBytes,
                       ULONG deviceObjectPresent, ULONG poolTag)
{
    ULONG want;

    want = XhciUsbdxV1Bytes(pointerBytes);
    if (want == 0 || size != want || version != XHCI_USBDX_CONTRACT_602) {
        return XHCI_USBDX_NOT_OURS;
    }
    if (!deviceObjectPresent || poolTag == 0) {
        return XHCI_USBDX_INVALID;
    }
    return XHCI_USBDX_OK;
}

ULONG XhciUsbdxCapBlockBytes(ULONG pointerBytes)
{
    if (pointerBytes == 4) {
        return XHCI_USBDX_CAP_BYTES_32;
    }
    if (pointerBytes == 8) {
        return XHCI_USBDX_CAP_BYTES_64;
    }
    return 0;
}

ULONG XhciUsbdxCapBlockCheck(ULONG version, ULONG size, ULONG pointerBytes,
                             ULONG contextPresent, ULONG outLength,
                             ULONG outPresent)
{
    ULONG want;

    want = XhciUsbdxCapBlockBytes(pointerBytes);
    if (want == 0 || size != want || version != XHCI_USBDX_CAP_VERSION ||
        !contextPresent) {
        return XHCI_USBDX_INVALID;
    }
    if ((outLength != 0) != (outPresent != 0)) {
        return XHCI_USBDX_INVALID;
    }
    return XHCI_USBDX_OK;
}

ULONG XhciUsbdxCapabilityId(const UCHAR *guid)
{
    ULONG i;
    ULONG b;

    if (guid == NULL) {
        return XHCI_USBDX_CAP_UNKNOWN;
    }
    for (i = 0; i < XHCI_USBDX_GUID_COUNT; i++) {
        for (b = 0; b < 16; b++) {
            if (guid[b] != xhciUsbdxGuids[i].Bytes[b]) {
                break;
            }
        }
        if (b == 16) {
            return xhciUsbdxGuids[i].Id;
        }
    }
    return XHCI_USBDX_CAP_UNKNOWN;
}

ULONG XhciUsbdxCapability(ULONG id, ULONG speedClass, ULONG maxStreams,
                          ULONG outLength, PULONG count, PULONG resultBytes)
{
    *count = 0;
    *resultBytes = 0;
    if (id == XHCI_USBDX_CAP_STATIC_STREAMS) {
        if (outLength != 2) {
            return XHCI_USBDX_INVALID;
        }
        if (speedClass < XHCI_SPEED_SUPER || maxStreams == 0) {
            return XHCI_USBDX_NOT_SUPPORTED;
        }
        *count = maxStreams;
        *resultBytes = 2;
        return XHCI_USBDX_OK;
    }
    if (outLength != 0) {
        return XHCI_USBDX_INVALID;
    }
    switch (id) {
    case XHCI_USBDX_CAP_SELECTIVE_SUSPEND:
        return XHCI_USBDX_OK;
    case XHCI_USBDX_CAP_HIGH_SPEED:
        return speedClass >= XHCI_SPEED_HIGH ? XHCI_USBDX_OK
                                             : XHCI_USBDX_NOT_SUPPORTED;
    case XHCI_USBDX_CAP_SUPER_SPEED:
        return speedClass >= XHCI_SPEED_SUPER ? XHCI_USBDX_OK
                                              : XHCI_USBDX_NOT_SUPPORTED;
    default:
        return XHCI_USBDX_NOT_SUPPORTED;
    }
}

ULONG XhciUsbdxMaxStreams(ULONG hcEntries)
{
    if (hcEntries < 4) {
        return 0;
    }
    if (hcEntries - 1 < XHCI_USBDX_MAX_STREAMS) {
        return hcEntries - 1;
    }
    return XHCI_USBDX_MAX_STREAMS;
}

ULONG XhciUsbdxIsochBytes(ULONG packets, ULONG isochBytes, ULONG packetBytes,
                          PULONG bytes)
{
    if (packets == 0 || packetBytes == 0 ||
        isochBytes > XHCI_USBDX_MAX_URB_BYTES ||
        packets > (XHCI_USBDX_MAX_URB_BYTES - isochBytes) / packetBytes) {
        return XHCI_USBDX_INVALID;
    }
    *bytes = isochBytes + packets * packetBytes;
    return XHCI_USBDX_OK;
}

ULONG XhciUsbdxInterfaceBytes(ULONG pipes, ULONG ifInfoBytes,
                              ULONG pipeInfoBytes)
{
    if (pipes > 1) {
        return ifInfoBytes + (pipes - 1) * pipeInfoBytes;
    }
    return ifInfoBytes;
}

ULONG XhciUsbdxSelectLayout(const ULONG *pipes, ULONG count,
                            ULONG headerBytes, ULONG ifInfoBytes,
                            ULONG pipeInfoBytes, PULONG offsets,
                            PULONG total)
{
    ULONG at;
    ULONG i;

    if (count == 0 || count > XHCI_USBDX_MAX_INTERFACES ||
        headerBytes > XHCI_USBDX_MAX_URB_BYTES ||
        ifInfoBytes > XHCI_USBDX_MAX_URB_BYTES ||
        pipeInfoBytes > XHCI_USBDX_MAX_URB_BYTES) {
        return XHCI_USBDX_INVALID;
    }
    at = headerBytes;
    for (i = 0; i < count; i++) {
        if (pipes[i] > XHCI_USBDX_MAX_ENDPOINTS) {
            return XHCI_USBDX_INVALID;
        }
        /* Each step adds at most 0xFFFF + 29 * 0xFFFF, so `at` cannot wrap
         * before the bound below stops it. */
        at += XhciUsbdxInterfaceBytes(pipes[i], ifInfoBytes, pipeInfoBytes);
        if (at > XHCI_USBDX_MAX_URB_BYTES) {
            return XHCI_USBDX_INVALID;
        }
    }
    at = headerBytes;
    for (i = 0; i < count; i++) {
        offsets[i] = at;
        at += XhciUsbdxInterfaceBytes(pipes[i], ifInfoBytes, pipeInfoBytes);
    }
    *total = at;
    return XHCI_USBDX_OK;
}

static VOID xhciUsbdxPut16(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFFUL);
    p[1] = (UCHAR)((v >> 8) & 0xFFUL);
}

static VOID xhciUsbdxPut32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFFUL);
    p[1] = (UCHAR)((v >> 8) & 0xFFUL);
    p[2] = (UCHAR)((v >> 16) & 0xFFUL);
    p[3] = (UCHAR)((v >> 24) & 0xFFUL);
}

/* The structure sizes and offsets for a function and width; 0 when either
 * is unknown. */
static ULONG xhciUsbdxShape(ULONG function, ULONG pointerBytes,
                            PULONG header, PULONG ifInfo, PULONG pipe,
                            PULONG pipesAt, PULONG pipe0At, PULONG maxXferAt)
{
    if (pointerBytes == 4) {
        *ifInfo = XHCI_USBDX_IFINFO_BYTES_32;
        *pipe = XHCI_USBDX_PIPE_BYTES_32;
        *pipesAt = XHCI_USBDX_IFINFO_PIPES_32;
        *pipe0At = XHCI_USBDX_IFINFO_PIPE0_32;
        *maxXferAt = XHCI_USBDX_PIPE_MAXXFER_32;
        *header = function == XHCI_USBDX_URB_SELECT_CONFIG
                      ? XHCI_USBDX_SELCFG_IFACE_32
                      : XHCI_USBDX_SELIF_IFACE_32;
    } else if (pointerBytes == 8) {
        *ifInfo = XHCI_USBDX_IFINFO_BYTES_64;
        *pipe = XHCI_USBDX_PIPE_BYTES_64;
        *pipesAt = XHCI_USBDX_IFINFO_PIPES_64;
        *pipe0At = XHCI_USBDX_IFINFO_PIPE0_64;
        *maxXferAt = XHCI_USBDX_PIPE_MAXXFER_64;
        *header = function == XHCI_USBDX_URB_SELECT_CONFIG
                      ? XHCI_USBDX_SELCFG_IFACE_64
                      : XHCI_USBDX_SELIF_IFACE_64;
    } else {
        return 0;
    }
    return function == XHCI_USBDX_URB_SELECT_CONFIG ||
           function == XHCI_USBDX_URB_SELECT_IFACE;
}

ULONG XhciUsbdxSelectBytes(ULONG function, ULONG pointerBytes,
                           const ULONG *pipes, ULONG count, PULONG bytes)
{
    ULONG offsets[XHCI_USBDX_MAX_INTERFACES];
    ULONG header;
    ULONG ifInfo;
    ULONG pipe;
    ULONG pipesAt;
    ULONG pipe0At;
    ULONG maxXferAt;

    if (!xhciUsbdxShape(function, pointerBytes, &header, &ifInfo, &pipe,
                        &pipesAt, &pipe0At, &maxXferAt) ||
        (function == XHCI_USBDX_URB_SELECT_IFACE && count != 1)) {
        return XHCI_USBDX_INVALID;
    }
    return XhciUsbdxSelectLayout(pipes, count, header, ifInfo, pipe, offsets,
                                 bytes);
}

ULONG XhciUsbdxSelectFill(UCHAR *urb, ULONG urbBytes, ULONG function,
                          ULONG pointerBytes, const ULONG *pipes,
                          const UCHAR *numbers, const UCHAR *alternates,
                          ULONG count, ULONG maxTransfer, PULONG offsets)
{
    ULONG header;
    ULONG ifInfo;
    ULONG pipe;
    ULONG pipesAt;
    ULONG pipe0At;
    ULONG maxXferAt;
    ULONG total;
    ULONG at;
    ULONG i;
    ULONG e;

    if (!xhciUsbdxShape(function, pointerBytes, &header, &ifInfo, &pipe,
                        &pipesAt, &pipe0At, &maxXferAt) ||
        (function == XHCI_USBDX_URB_SELECT_IFACE && count != 1) ||
        XhciUsbdxSelectLayout(pipes, count, header, ifInfo, pipe, offsets,
                              &total) != XHCI_USBDX_OK ||
        total != urbBytes) {
        return XHCI_USBDX_INVALID;
    }
    xhciUsbdxPut16(urb, total);
    xhciUsbdxPut16(urb + 2, function);
    for (i = 0; i < count; i++) {
        at = offsets[i];
        xhciUsbdxPut16(urb + at,
                       XhciUsbdxInterfaceBytes(pipes[i], ifInfo, pipe));
        urb[at + 2] = numbers[i];
        urb[at + 3] = alternates[i];
        xhciUsbdxPut32(urb + at + pipesAt, pipes[i]);
        for (e = 0; e < pipes[i]; e++) {
            xhciUsbdxPut32(urb + at + pipe0At + e * pipe + maxXferAt,
                           maxTransfer);
        }
    }
    return XHCI_USBDX_OK;
}

ULONG XhciUsbdxOpenCheck(ULONG length, ULONG structBytes, ULONG infoVersion,
                         ULONG infoSize, ULONG infoBytes, ULONG streams,
                         ULONG arrayPresent)
{
    if (length < structBytes || infoVersion != XHCI_USBDX_STREAM_INFO_VERSION ||
        infoSize != infoBytes || streams == 0 ||
        streams > XHCI_USBDX_MAX_STREAMS || !arrayPresent) {
        return XHCI_USBDX_USBD_INVALID_PARAMETER;
    }
    return XHCI_USBDX_USBD_SUCCESS;
}

ULONG XhciUsbdxCloseCheck(ULONG length, ULONG pipeRequestBytes)
{
    if (length < pipeRequestBytes) {
        return XHCI_USBDX_USBD_INVALID_PARAMETER;
    }
    return XHCI_USBDX_USBD_SUCCESS;
}

ULONG XhciUsbdxGrantCheck(ULONG requested, ULONG planned, ULONG exact)
{
    if (planned == 0 || planned > requested) {
        return XHCI_USBDX_INVALID;
    }
    if (exact && planned != requested) {
        return XHCI_USBDX_INVALID;
    }
    return XHCI_USBDX_OK;
}

ULONG XhciUsbdxStreamsUsbd(ULONG answer, ULONG gone)
{
    switch (answer) {
    case XHCI_USBDX_SA_SUCCESS:
        return XHCI_USBDX_USBD_SUCCESS;
    case XHCI_USBDX_SA_NO_CONTROLLER:
    case XHCI_USBDX_SA_NO_ENDPOINT:
        return XHCI_USBDX_USBD_NOT_SUPPORTED;
    case XHCI_USBDX_SA_INVALID_PIPE:
        return XHCI_USBDX_USBD_INVALID_PIPE;
    case XHCI_USBDX_SA_NO_RESOURCES:
        return XHCI_USBDX_USBD_NO_RESOURCES;
    case XHCI_USBDX_SA_FAILED:
        return gone ? XHCI_USBDX_USBD_DEVICE_GONE
                    : XHCI_USBDX_USBD_INTERNAL_HC_ERROR;
    default:
        return XHCI_USBDX_USBD_INVALID_PARAMETER;
    }
}

ULONG XhciUsbdxIoctlAlias(ULONG code)
{
    if (code == XHCI_USBDX_IOCTL_RESET_ALIAS) {
        return XHCI_USBDX_IOCTL_RESET_PORT;
    }
    return code;
}
