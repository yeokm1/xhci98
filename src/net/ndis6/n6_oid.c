/*
 * n6_oid.c - xhcinet6.sys's OID requests (design record 35 sections 7.3,
 * 7.5 and 7.7): the queries and sets an NDIS 6.20 Ethernet miniport with
 * no offload answers, and OID_PNP_SET_POWER pended to a work item that
 * closes the service at D3 and opens it at D0.
 *
 * IRQL: MiniportOidRequest at <= DISPATCH_LEVEL; the work item at
 * PASSIVE_LEVEL.
 */

#include "xhcinet6.h"

#define NET6_VENDOR_DESC "xHCI98 Thunderbolt networking"

const NDIS_OID Net6Oids[] = {
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_LINK_SPEED,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_STATISTICS,
    OID_GEN_INTERRUPT_MODERATION,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,
    OID_PNP_QUERY_POWER,
    OID_PNP_SET_POWER
};

const ULONG Net6OidsSize = sizeof(Net6Oids);

/* The receive errors: the core's drops and the split's. */
static ULONG net6RxErrors(PNET6_ADAPTER a)
{
    ULONG v = 0;
    ULONG i;

    for (i = 0; i < NET_RXDROP_REASONS; i++) {
        v += a->Rx.Drops[i];
    }
    for (i = 0; i < NET_SEGDROP_REASONS; i++) {
        v += a->SegDrops[i];
    }
    return v;
}

static ULONG net6TxErrors(PNET6_ADAPTER a)
{
    return a->TxErrors + a->TxDrops[NET_TXDROP_SHORT] +
           a->TxDrops[NET_TXDROP_LONG] + a->TxDrops[NET_TXDROP_PEER];
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

static NDIS_STATUS net6Query(PNET6_ADAPTER a, PNDIS_OID_REQUEST r)
{
    NDIS_OID oid = r->DATA.QUERY_INFORMATION.Oid;
    PVOID buf = r->DATA.QUERY_INFORMATION.InformationBuffer;
    ULONG len = r->DATA.QUERY_INFORMATION.InformationBufferLength;
    NDIS_STATISTICS_INFO st;
    NDIS_INTERRUPT_MODERATION_PARAMETERS im;
    XHCI_CM_NET_INFO info;
    const VOID *src;
    ULONG size = sizeof(ULONG);
    ULONG v = 0;
    NDIS_MEDIUM medium = NdisMedium802_3;

    r->DATA.QUERY_INFORMATION.BytesWritten = 0;
    r->DATA.QUERY_INFORMATION.BytesNeeded = 0;
    src = &v;

    switch (oid) {
    case OID_GEN_SUPPORTED_LIST:
        src = Net6Oids;
        size = Net6OidsSize;
        break;
    case OID_GEN_HARDWARE_STATUS:
        v = NdisHardwareStatusReady;
        break;
    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
        src = &medium;
        size = sizeof(medium);
        break;
    case OID_GEN_PHYSICAL_MEDIUM:
        v = NdisPhysicalMediumUnspecified;
        break;
    case OID_GEN_MAXIMUM_LOOKAHEAD:
    case OID_GEN_MAXIMUM_FRAME_SIZE:
        v = a->Mtu;
        break;
    case OID_GEN_CURRENT_LOOKAHEAD:
        v = a->Lookahead;
        break;
    case OID_GEN_MAXIMUM_TOTAL_SIZE:
    case OID_GEN_TRANSMIT_BLOCK_SIZE:
    case OID_GEN_RECEIVE_BLOCK_SIZE:
        v = a->MaxFrame;
        break;
    case OID_GEN_TRANSMIT_BUFFER_SPACE:
        v = a->MaxFrame * 32UL;
        break;
    case OID_GEN_RECEIVE_BUFFER_SPACE:
        v = a->MaxFrame * a->RxCount;
        break;
    case OID_GEN_LINK_SPEED:
        /* In units of 100 bit/s; 10 Gbit/s when unknown (section 7.5):
         * NetCmIfRateMbps caps the rate at 400000, so * 10000 fits. */
        Net6CmQuery(a, &info);
        v = NetCmIfRateMbps(&info);
        if (v == 0) {
            v = 10000UL;
        }
        v *= 10000UL;
        break;
    case OID_GEN_MEDIA_CONNECT_STATUS:
        v = a->LinkUp ? NdisMediaStateConnected : NdisMediaStateDisconnected;
        break;
    case OID_GEN_VENDOR_ID:
        v = ((ULONG)a->Mac[0] << 16) | ((ULONG)a->Mac[1] << 8) |
            (ULONG)a->Mac[2];
        break;
    case OID_GEN_VENDOR_DESCRIPTION:
        src = NET6_VENDOR_DESC;
        size = sizeof(NET6_VENDOR_DESC);
        break;
    case OID_GEN_VENDOR_DRIVER_VERSION:
        v = 0x00010000UL;
        break;
    case OID_GEN_DRIVER_VERSION:
        v = (NDIS_MINIPORT_MAJOR_VERSION << 8) | NDIS_MINIPORT_MINOR_VERSION;
        size = sizeof(USHORT);
        break;
    case OID_GEN_MAC_OPTIONS:
        v = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
            NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
            NDIS_MAC_OPTION_NO_LOOPBACK;
        break;
    case OID_GEN_MAXIMUM_SEND_PACKETS:
        v = 16;
        break;
    case OID_GEN_CURRENT_PACKET_FILTER:
        v = a->Filter;
        break;
    case OID_GEN_XMIT_OK:
        v = a->TxOk;
        break;
    case OID_GEN_RCV_OK:
        v = a->RxOk;
        break;
    case OID_GEN_XMIT_ERROR:
        v = net6TxErrors(a);
        break;
    case OID_GEN_RCV_ERROR:
        v = net6RxErrors(a);
        break;
    case OID_GEN_RCV_NO_BUFFER:
        v = a->RxNoBuffer;
        break;
    case OID_GEN_STATISTICS:
        Net6Zero(&st, sizeof(st));
        st.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        st.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
        st.Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
        st.SupportedStatistics =
            NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
            NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
            NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
            NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
            NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
            NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
            NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR |
            NDIS_STATISTICS_FLAGS_VALID_XMIT_DISCARDS;
        /* ULONG counters widened, not summed in 64 bits. */
        st.ifInDiscards = a->RxNoBuffer + a->RxNotUp;
        st.ifInErrors = net6RxErrors(a);
        st.ifHCInOctets = a->RxBytes;
        st.ifHCInUcastPkts = a->RxOk;
        st.ifHCOutOctets = a->TxBytes;
        st.ifHCOutUcastPkts = a->TxOk;
        st.ifOutErrors = net6TxErrors(a);
        st.ifOutDiscards = a->TxDown;
        src = &st;
        size = sizeof(st);
        break;
    case OID_GEN_INTERRUPT_MODERATION:
        Net6Zero(&im, sizeof(im));
        im.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        im.Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
        im.Header.Size = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
        im.Flags = 0;
        im.InterruptModeration = NdisInterruptModerationNotSupported;
        src = &im;
        size = sizeof(im);
        break;
    case OID_802_3_PERMANENT_ADDRESS:
    case OID_802_3_CURRENT_ADDRESS:
        src = a->Mac;
        size = 6;
        break;
    case OID_802_3_MULTICAST_LIST:
        src = a->Mcast;
        size = a->McCount * 6;
        break;
    case OID_802_3_MAXIMUM_LIST_SIZE:
        v = NET6_MCAST_MAX;
        break;
    case OID_802_3_RCV_ERROR_ALIGNMENT:
    case OID_802_3_XMIT_ONE_COLLISION:
    case OID_802_3_XMIT_MORE_COLLISIONS:
        v = 0;
        break;
    case OID_PNP_QUERY_POWER:
        size = 0;
        break;
    default:
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (size > len) {
        r->DATA.QUERY_INFORMATION.BytesNeeded = size;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }
    Net6Copy((PUCHAR)buf, (const UCHAR *)src, size);
    r->DATA.QUERY_INFORMATION.BytesWritten = size;
    return NDIS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Power (record 35 section 7.7)                                       */
/* ------------------------------------------------------------------ */

/* The pended OID_PNP_SET_POWER: Close at D3 (the link down), Open at D0
 * (the connection manager logs in again when the peer is there), then
 * the request completed. NDIS has paused the adapter before a D3 and
 * sends no other request until this one completes. IRQL: PASSIVE_LEVEL. */
static VOID net6PowerWork(PVOID ctx, NDIS_HANDLE workItem)
{
    PNET6_ADAPTER a = (PNET6_ADAPTER)ctx;
    PNDIS_OID_REQUEST r;
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    ULONG req;

    UNREFERENCED_PARAMETER(workItem);

    NdisAcquireSpinLock(&a->Lock);
    req = a->WorkReq;
    a->WorkReq = NET6_WORK_NONE;
    r = a->PowerOid;
    a->PowerOid = NULL;
    NdisReleaseSpinLock(&a->Lock);

    if (req == NET6_WORK_D3) {
        Net6CmClose(a);
        Net6LinkDown(a);
        Net6IndicateLink(a);
        Net6CmLog(a, "net.power.d3", 0);
    } else if (req == NET6_WORK_D0) {
        status = Net6CmOpen(a);
        if (status == NDIS_STATUS_SUCCESS) {
            NdisAcquireSpinLock(&a->Lock);
            a->PowerD3 = 0;
            NdisReleaseSpinLock(&a->Lock);
        }
        Net6CmLog(a, "net.power.d0", (ULONG)status);
    }
    if (r != NULL) {
        NdisMOidRequestComplete(a->Handle, r, status);
    }
}

/* ------------------------------------------------------------------ */
/* Sets                                                                */
/* ------------------------------------------------------------------ */

static NDIS_STATUS net6Set(PNET6_ADAPTER a, PNDIS_OID_REQUEST r)
{
    NDIS_OID oid = r->DATA.SET_INFORMATION.Oid;
    PVOID buf = r->DATA.SET_INFORMATION.InformationBuffer;
    ULONG len = r->DATA.SET_INFORMATION.InformationBufferLength;
    ULONG v;

    r->DATA.SET_INFORMATION.BytesRead = 0;
    r->DATA.SET_INFORMATION.BytesNeeded = 0;

    switch (oid) {
    case OID_802_3_MULTICAST_LIST:
        if (len % 6 != 0) {
            return NDIS_STATUS_INVALID_LENGTH;
        }
        if (len > NET6_MCAST_MAX * 6) {
            return NDIS_STATUS_MULTICAST_FULL;
        }
        Net6Copy((PUCHAR)a->Mcast, (const UCHAR *)buf, len);
        a->McCount = len / 6;
        r->DATA.SET_INFORMATION.BytesRead = len;
        return NDIS_STATUS_SUCCESS;
    case OID_PNP_QUERY_POWER:
        return NDIS_STATUS_SUCCESS;
    case OID_GEN_INTERRUPT_MODERATION:
        return NDIS_STATUS_INVALID_DATA;
    default:
        break;
    }

    if (len < sizeof(ULONG)) {
        r->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
        return NDIS_STATUS_INVALID_LENGTH;
    }
    v = *(ULONG UNALIGNED *)buf;
    r->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);

    switch (oid) {
    case OID_GEN_CURRENT_PACKET_FILTER:
        /* Every filter accepted: a point-to-point link passes up every
         * frame the peer sends, and the stack filters (section 7.5). */
        a->Filter = v;
        return NDIS_STATUS_SUCCESS;
    case OID_GEN_CURRENT_LOOKAHEAD:
        if (v > a->Mtu) {
            return NDIS_STATUS_INVALID_DATA;
        }
        a->Lookahead = v;
        return NDIS_STATUS_SUCCESS;
    case OID_PNP_SET_POWER:
        NdisAcquireSpinLock(&a->Lock);
        if (a->PowerOid != NULL) {
            NdisReleaseSpinLock(&a->Lock);
            return NDIS_STATUS_FAILURE;
        }
        if ((NDIS_DEVICE_POWER_STATE)v == NdisDeviceStateD0) {
            a->WorkReq = NET6_WORK_D0;
        } else {
            a->PowerD3 = 1;
            a->WorkReq = NET6_WORK_D3;
        }
        a->PowerOid = r;
        NdisReleaseSpinLock(&a->Lock);
        NdisQueueIoWorkItem(a->WorkItem, net6PowerWork, a);
        return NDIS_STATUS_PENDING;
    default:
        r->DATA.SET_INFORMATION.BytesRead = 0;
        return NDIS_STATUS_NOT_SUPPORTED;
    }
}

/* MiniportOidRequest. IRQL: <= DISPATCH_LEVEL. */
NDIS_STATUS Net6OidRequest(NDIS_HANDLE ctx, PNDIS_OID_REQUEST r)
{
    PNET6_ADAPTER a = (PNET6_ADAPTER)ctx;

    switch (r->RequestType) {
    case NdisRequestQueryInformation:
    case NdisRequestQueryStatistics:
        return net6Query(a, r);
    case NdisRequestSetInformation:
        return net6Set(a, r);
    default:
        return NDIS_STATUS_NOT_SUPPORTED;
    }
}

/* MiniportCancelOidRequest: the one request this miniport pends, the
 * power set, is already on its work item and completes there.
 * IRQL: <= DISPATCH_LEVEL. */
VOID Net6CancelOid(NDIS_HANDLE ctx, PVOID requestId)
{
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(requestId);
}
