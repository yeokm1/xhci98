/*
 * n5_main.c - xhcinet.sys's entry, initialisation, halt, OIDs and power
 * (design record 35 sections 7.3, 7.5 to 7.7 and 8.3; task 45.59 step
 * S6). The data path is n5_data.c, the interface glue n5_cm.c.
 *
 * IRQL: as each function says; NDIS's own contract for each handler.
 */

#include "xhcinet.h"
#include "../../xhci_version.h"

#if defined(XHCI_FLAVOUR_QEMU)
#define NET_FLAVOUR_NAME "XHCI98_FLAVOUR_QEMU"
#elif defined(XHCI_FLAVOUR_RELEASE)
#define NET_FLAVOUR_NAME "XHCI98_FLAVOUR_RELEASE"
#else
#error "no build flavour was defined. src/net/ndis5/sources derives XHCI_FLAVOUR_RELEASE or _QEMU from BUILD_ALT_DIR; build through scripts\build-driver.cmd."
#endif

static volatile const char netFlavourMarker[] = NET_FLAVOUR_NAME;
static volatile const char netVersionString[] = "xhcinet " XHCI_VER_STR;

#if defined(NDIS51_MINIPORT)
#define NET_NDIS_MAJOR  5
#define NET_NDIS_MINOR  1
#else
#define NET_NDIS_MAJOR  5
#define NET_NDIS_MINOR  0
#endif

#define NET_VENDOR_DESC "xHCI98 Thunderbolt networking"

static NDIS_HANDLE netWrapper;

/*
 * An event wait that returns only once the event is signalled: Windows
 * 98 can return from KeWaitForSingleObject unsatisfied
 * (docs/usb-xhci-info/win98-wdm.md), and every caller here frees or
 * reuses what the signaller holds. IRQL: PASSIVE_LEVEL.
 */
static VOID netWait(PVOID object)
{
    while (KeWaitForSingleObject(object, Executive, KernelMode, FALSE,
                                 NULL) != STATUS_SUCCESS) {
        NdisMSleep(1000);
    }
}

static const NDIS_OID netOids[] = {
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_PROTOCOL_OPTIONS,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,
    OID_PNP_CAPABILITIES,
    OID_PNP_QUERY_POWER,
    OID_PNP_SET_POWER
};

/*
 * Live adapters, from a successful initialisation to the start of the
 * halt: Query and Set check their context here first, so a late call
 * after a halt never touches a freed adapter (a Windows 98 SE NDIS
 * ordering the usb-wdm-free cdcnet record of 2026-10-22 reports for an
 * NDIS 5 WDM miniport there).
 */
#define NET_LIVE_MAX 8
static PNET_ADAPTER netLive[NET_LIVE_MAX];
static NDIS_SPIN_LOCK netLiveLock;

static VOID netLiveSet(PNET_ADAPTER a, PNET_ADAPTER with)
{
    ULONG i;

    NdisAcquireSpinLock(&netLiveLock);
    for (i = 0; i < NET_LIVE_MAX; i++) {
        if (netLive[i] == a) {
            netLive[i] = with;
            break;
        }
    }
    NdisReleaseSpinLock(&netLiveLock);
}

static ULONG netIsLive(PNET_ADAPTER a)
{
    ULONG i;
    ULONG live = 0;

    NdisAcquireSpinLock(&netLiveLock);
    for (i = 0; i < NET_LIVE_MAX; i++) {
        if (netLive[i] == a && a != NULL) {
            live = 1;
            break;
        }
    }
    NdisReleaseSpinLock(&netLiveLock);
    return live;
}

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

/* XhciNetMtu (section 8.3): out of range refused, the default stands,
 * and *refused is the value read (logged once the interface is held).
 * IRQL: PASSIVE_LEVEL. */
static ULONG netReadMtu(NDIS_HANDLE wcc, PULONG refused)
{
    NDIS_STRING name = NDIS_STRING_CONST("XhciNetMtu");
    PNDIS_CONFIGURATION_PARAMETER p;
    NDIS_HANDLE cfg;
    NDIS_STATUS status;
    ULONG mtu = NET_MTU_DEFAULT;

    NdisOpenConfiguration(&status, &cfg, wcc);
    if (status != NDIS_STATUS_SUCCESS) {
        return mtu;
    }
    NdisReadConfiguration(&status, &p, cfg, &name, NdisParameterInteger);
    if (status == NDIS_STATUS_SUCCESS &&
        p->ParameterType == NdisParameterInteger) {
        ULONG v = (ULONG)p->ParameterData.IntegerData;

        if (v >= NET_MTU_MIN && v <= NET_MTU_MAX) {
            mtu = v;
        } else {
            *refused = v;
        }
    }
    NdisCloseConfiguration(cfg);
    return mtu;
}

/* ------------------------------------------------------------------ */
/* The PASSIVE_LEVEL thread                                            */
/* ------------------------------------------------------------------ */

/* The power OIDs' work (section 7.7): Close at D3, Open at D0, then the
 * pended set completed. IRQL: PASSIVE_LEVEL. */
static VOID netWorker(PVOID ctx)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;
    ULONG req;

    for (;;) {
        netWait(&a->WorkEvent);
        NdisAcquireSpinLock(&a->TxLock);
        req = a->WorkReq;
        a->WorkReq = NET_WORK_NONE;
        if (req == NET_WORK_NONE && a->WorkStop) {
            NdisReleaseSpinLock(&a->TxLock);
            break;
        }
        KeClearEvent(&a->WorkEvent);
        NdisReleaseSpinLock(&a->TxLock);

        if (req == NET_WORK_D3) {
            NetCmClose(a);
            NdisAcquireSpinLock(&a->TxLock);
            a->LinkUp = 0;
            a->LinkGen++;
            NdisReleaseSpinLock(&a->TxLock);
            NetFlushSends(a, NET_STATUS_DOWN);
            NdisMSetInformationComplete(a->Handle, NDIS_STATUS_SUCCESS);
        } else if (req == NET_WORK_D0) {
            NDIS_STATUS status = NetCmOpen(a);

            if (status == NDIS_STATUS_SUCCESS) {
                a->PowerD3 = 0;
            }
            NdisMSetInformationComplete(a->Handle, status);
        }
    }
    (VOID)KeSetEvent(&a->WorkExited, IO_NO_INCREMENT, FALSE);
    (VOID)PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID netStopWorker(PNET_ADAPTER a);

/* IRQL: PASSIVE_LEVEL. */
static NDIS_STATUS netStartWorker(PNET_ADAPTER a)
{
    HANDLE h;
    NTSTATUS status;

    KeInitializeEvent(&a->WorkEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&a->WorkExited, NotificationEvent, FALSE);
    a->WorkThread = NULL;
    status = PsCreateSystemThread(&h, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  netWorker, a);
    if (!NT_SUCCESS(status)) {
        return NDIS_STATUS_RESOURCES;
    }
    /* The join differs by target, as src\cm\cm_thr.c's: the thread
     * object on NT (WDM 1.10 and later), whose signal alone says the
     * thread has left the image; the exit event on Windows 98 and ME,
     * where a wait on a thread object faults (win98-wdm.md). */
    status = STATUS_SUCCESS;
    if (IoIsWdmVersionAvailable(1, 0x10)) {
        status = ObReferenceObjectByHandle(h, THREAD_ALL_ACCESS, NULL,
                                           KernelMode, &a->WorkThread,
                                           NULL);
        if (!NT_SUCCESS(status)) {
            a->WorkThread = NULL;
        }
    }
    (VOID)ZwClose(h);
    if (!NT_SUCCESS(status)) {
        /* NT without the object: no join that proves the thread has left
         * the image, so the initialisation fails; the thread is told to
         * leave, its event waited for, and its last instructions given
         * 100 ms before the image can be unloaded. */
        netStopWorker(a);
        NdisMSleep(100000);
        return NDIS_STATUS_RESOURCES;
    }
    return NDIS_STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL. */
static VOID netStopWorker(PNET_ADAPTER a)
{
    NdisAcquireSpinLock(&a->TxLock);
    a->WorkStop = 1;
    NdisReleaseSpinLock(&a->TxLock);
    (VOID)KeSetEvent(&a->WorkEvent, IO_NO_INCREMENT, FALSE);
    if (a->WorkThread != NULL) {
        netWait(a->WorkThread);
        ObDereferenceObject(a->WorkThread);
        a->WorkThread = NULL;
    } else {
        netWait(&a->WorkExited);
    }
}

/* ------------------------------------------------------------------ */
/* Initialise and halt                                                 */
/* ------------------------------------------------------------------ */

static VOID netFreeAdapter(PNET_ADAPTER a)
{
    NetRxFree(a);
    NetCmRelease(a);
    NdisFreeSpinLock(&a->RxLock);
    NdisFreeSpinLock(&a->TxLock);
    NdisFreeMemory(a, sizeof(*a), 0);
}

/* MiniportInitialize. IRQL: PASSIVE_LEVEL. */
static NDIS_STATUS netInitialize(PNDIS_STATUS openError, PUINT medium,
                                 PNDIS_MEDIUM media, UINT mediaCount,
                                 NDIS_HANDLE handle, NDIS_HANDLE wcc)
{
    NDIS_PHYSICAL_ADDRESS high = NDIS_PHYSICAL_ADDRESS_CONST(-1, -1);
    PNET_ADAPTER a;
    PDEVICE_OBJECT pdo;
    PDEVICE_OBJECT fdo;
    PDEVICE_OBJECT lower;
    NET_CM_INFO info;
    NDIS_STATUS status;
    UINT i;
    ULONG asked;

    *openError = NDIS_STATUS_SUCCESS;
    for (i = 0; i < mediaCount; i++) {
        if (media[i] == NdisMedium802_3) {
            break;
        }
    }
    if (i == mediaCount) {
        return NDIS_STATUS_UNSUPPORTED_MEDIA;
    }
    *medium = i;

    status = NdisAllocateMemory((PVOID *)&a, sizeof(*a), 0, high);
    if (status != NDIS_STATUS_SUCCESS) {
        return NDIS_STATUS_RESOURCES;
    }
    NetZero(a, sizeof(*a));
    a->Handle = handle;
    NdisAllocateSpinLock(&a->TxLock);
    NdisAllocateSpinLock(&a->RxLock);
    KeInitializeEvent(&a->RxDrained, NotificationEvent, FALSE);
    NetTxInit(&a->Tx);
    a->Lookahead = NET_MTU_DEFAULT;

    asked = 0;
    a->Mtu = netReadMtu(wcc, &asked);
    a->MaxFrame = a->Mtu + NET_ETH_HDR;

    NdisMSetAttributesEx(handle, a, 0,
                         NDIS_ATTRIBUTE_DESERIALIZE
#if defined(NDIS51_MINIPORT)
                         | NDIS_ATTRIBUTE_SURPRISE_REMOVE_OK
#endif
                         , NdisInterfaceInternal);

    pdo = NULL;
    fdo = NULL;
    lower = NULL;
    NdisMGetDeviceProperty(handle, &pdo, &fdo, &lower, NULL, NULL);
    if (lower == NULL) {
        netFreeAdapter(a);
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }
    a->Lower = lower;

    status = NetCmAcquire(a);
    if (status != NDIS_STATUS_SUCCESS) {
        netFreeAdapter(a);
        return status;
    }
    if (asked != 0) {
        NetCmLog(a, "net.mtu.range", asked);
    }
    NetCmQuery(a, &info);
    for (i = 0; i < 6; i++) {
        a->Mac[i] = info.Mac[i];
    }

    status = NetRxAlloc(a);
    if (status != NDIS_STATUS_SUCCESS) {
        NetCmLog(a, "net.nomem", a->MaxFrame);
        netFreeAdapter(a);
        return status;
    }
    status = netStartWorker(a);
    if (status != NDIS_STATUS_SUCCESS) {
        netFreeAdapter(a);
        return status;
    }

    status = NetCmOpen(a);
    if (status != NDIS_STATUS_SUCCESS) {
        netStopWorker(a);
        netFreeAdapter(a);
        return status;
    }
    netLiveSet(NULL, a);
    NetCmLog(a, "net.init", a->Mtu);
    return NDIS_STATUS_SUCCESS;
}

/*
 * MiniportHalt (section 7.6): Close (a logout, the directory withdrawn,
 * no callback after it), every queued send failed, the sender waited
 * out, every indicated packet back, then everything freed.
 * IRQL: PASSIVE_LEVEL.
 */
static VOID netHalt(NDIS_HANDLE ctx)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;
    ULONG busy;
    ULONG out;

    netLiveSet(a, NULL);
    NdisAcquireSpinLock(&a->TxLock);
    a->Halting = 1;
    NdisReleaseSpinLock(&a->TxLock);

    netStopWorker(a);
    NetCmClose(a);
    a->LinkUp = 0;
    NetFlushSends(a, NDIS_STATUS_FAILURE);
    for (;;) {
        NdisAcquireSpinLock(&a->TxLock);
        busy = a->TxServing | a->TxActive;
        NdisReleaseSpinLock(&a->TxLock);
        if (!busy) {
            break;
        }
        NdisMSleep(1000);
    }
    NetFlushSends(a, NDIS_STATUS_FAILURE);
    for (;;) {
        NdisAcquireSpinLock(&a->TxLock);
        busy = a->TxActive;
        NdisReleaseSpinLock(&a->TxLock);
        if (!busy) {
            break;
        }
        NdisMSleep(1000);
    }

    NdisAcquireSpinLock(&a->RxLock);
    out = a->RxOut;
    NdisReleaseSpinLock(&a->RxLock);
    if (out != 0) {
        netWait(&a->RxDrained);
        /* The last return set the event under RxLock: taking it once
         * more waits that return out. */
        NdisAcquireSpinLock(&a->RxLock);
        NdisReleaseSpinLock(&a->RxLock);
    }
    netFreeAdapter(a);
}

#if defined(NDIS51_MINIPORT)
/* MiniportShutdown: the connection manager ends the service at its own
 * shutdown; nothing here touches hardware. IRQL: any. */
static VOID netShutdown(NDIS_HANDLE ctx)
{
    UNREFERENCED_PARAMETER(ctx);
}

/* MiniportPnPEventNotify: a surprise removal fails the queued sends at
 * once (the halt follows). IRQL: PASSIVE_LEVEL. */
static VOID netPnpEvent(NDIS_HANDLE ctx, NDIS_DEVICE_PNP_EVENT event,
                        PVOID buffer, ULONG length)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;

    UNREFERENCED_PARAMETER(buffer);
    UNREFERENCED_PARAMETER(length);
    if (event == NdisDevicePnPEventSurpriseRemoved) {
        NdisAcquireSpinLock(&a->TxLock);
        a->LinkUp = 0;
        a->LinkGen++;
        NdisReleaseSpinLock(&a->TxLock);
        NetServe(a);
    }
}
#endif

/* ------------------------------------------------------------------ */
/* OIDs                                                                */
/* ------------------------------------------------------------------ */

/* MiniportQueryInformation. IRQL: <= DISPATCH_LEVEL. */
static NDIS_STATUS netQuery(NDIS_HANDLE ctx, NDIS_OID oid, PVOID buf,
                            ULONG len, PULONG written, PULONG needed)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;
    NDIS_PNP_CAPABILITIES caps;
    const VOID *src;
    ULONG size = sizeof(ULONG);
    ULONG v = 0;
    USHORT version;
    NDIS_MEDIUM medium = NdisMedium802_3;
    NET_CM_INFO info;

    *written = 0;
    *needed = 0;
    if (!netIsLive(a)) {
        return NDIS_STATUS_FAILURE;
    }
    src = &v;

    switch (oid) {
    case OID_GEN_SUPPORTED_LIST:
        src = netOids;
        size = sizeof(netOids);
        break;
    case OID_GEN_HARDWARE_STATUS:
        v = NdisHardwareStatusReady;
        break;
    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
        src = &medium;
        size = sizeof(medium);
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
    case OID_GEN_LINK_SPEED:
        /* In units of 100 bit/s; 10 Gbit/s when unknown (section 7.5),
         * which is 100,000,000 units, within a ULONG. */
        NetCmQuery(a, &info);
        v = info.RateMbps;
        if (v == 0 || v > 400000UL) {
            v = 10000UL;
        }
        v *= 10000UL;
        break;
    case OID_GEN_TRANSMIT_BUFFER_SPACE:
        v = a->MaxFrame * 32UL;
        break;
    case OID_GEN_RECEIVE_BUFFER_SPACE:
        v = a->MaxFrame * a->RxPackets;
        break;
    case OID_GEN_VENDOR_ID:
        v = ((ULONG)a->Mac[0] << 16) | ((ULONG)a->Mac[1] << 8) |
            (ULONG)a->Mac[2];
        break;
    case OID_GEN_VENDOR_DESCRIPTION:
        src = NET_VENDOR_DESC;
        size = sizeof(NET_VENDOR_DESC);
        break;
    case OID_GEN_VENDOR_DRIVER_VERSION:
        v = 0x00010000UL;
        break;
    case OID_GEN_DRIVER_VERSION:
        version = (USHORT)((NET_NDIS_MAJOR << 8) | NET_NDIS_MINOR);
        src = &version;
        size = sizeof(version);
        break;
    case OID_GEN_CURRENT_PACKET_FILTER:
        v = a->Filter;
        break;
    case OID_GEN_MAC_OPTIONS:
        v = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
            NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
            NDIS_MAC_OPTION_NO_LOOPBACK;
        break;
    case OID_GEN_MEDIA_CONNECT_STATUS:
        v = a->LinkUp ? NdisMediaStateConnected : NdisMediaStateDisconnected;
        break;
    case OID_GEN_MAXIMUM_SEND_PACKETS:
        v = 16;
        break;
    case OID_GEN_XMIT_OK:
        v = a->TxOk;
        break;
    case OID_GEN_RCV_OK:
        v = a->RxOk;
        break;
    case OID_GEN_XMIT_ERROR:
        v = a->TxErrors + a->TxDrops[NET_TXDROP_SHORT] +
            a->TxDrops[NET_TXDROP_LONG] + a->TxDrops[NET_TXDROP_PEER];
        break;
    case OID_GEN_RCV_ERROR:
        {
            ULONG i;

            for (i = 0; i < NET_RXDROP_REASONS; i++) {
                v += a->Rx.Drops[i];
            }
            for (i = 0; i < NET_SEGDROP_REASONS; i++) {
                v += a->SegDrops[i];
            }
        }
        break;
    case OID_GEN_RCV_NO_BUFFER:
        v = a->RxNoBuffer;
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
        v = NET_MCAST_MAX;
        break;
    case OID_802_3_RCV_ERROR_ALIGNMENT:
    case OID_802_3_XMIT_ONE_COLLISION:
    case OID_802_3_XMIT_MORE_COLLISIONS:
        v = 0;
        break;
    case OID_PNP_CAPABILITIES:
        NetZero(&caps, sizeof(caps));
        caps.WakeUpCapabilities.MinMagicPacketWakeUp =
            NdisDeviceStateUnspecified;
        caps.WakeUpCapabilities.MinPatternWakeUp =
            NdisDeviceStateUnspecified;
        caps.WakeUpCapabilities.MinLinkChangeWakeUp =
            NdisDeviceStateUnspecified;
        src = &caps;
        size = sizeof(caps);
        break;
    case OID_PNP_QUERY_POWER:
        size = 0;
        break;
    default:
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (size > len) {
        *needed = size;
        return NDIS_STATUS_INVALID_LENGTH;
    }
    NetCopy((PUCHAR)buf, (const UCHAR *)src, size);
    *written = size;
    return NDIS_STATUS_SUCCESS;
}

/* MiniportSetInformation. IRQL: <= DISPATCH_LEVEL. */
static NDIS_STATUS netSet(NDIS_HANDLE ctx, NDIS_OID oid, PVOID buf,
                          ULONG len, PULONG read, PULONG needed)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;
    ULONG v;

    *read = 0;
    *needed = 0;
    if (!netIsLive(a)) {
        return NDIS_STATUS_FAILURE;
    }

    switch (oid) {
    case OID_802_3_MULTICAST_LIST:
        if (len % 6 != 0) {
            return NDIS_STATUS_INVALID_LENGTH;
        }
        if (len > NET_MCAST_MAX * 6) {
            return NDIS_STATUS_MULTICAST_FULL;
        }
        NetCopy((PUCHAR)a->Mcast, (const UCHAR *)buf, len);
        a->McCount = len / 6;
        *read = len;
        return NDIS_STATUS_SUCCESS;
    case OID_PNP_QUERY_POWER:
        return NDIS_STATUS_SUCCESS;
    default:
        break;
    }

    if (len < sizeof(ULONG)) {
        *needed = sizeof(ULONG);
        return NDIS_STATUS_INVALID_LENGTH;
    }
    v = *(ULONG UNALIGNED *)buf;
    *read = sizeof(ULONG);

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
    case OID_GEN_PROTOCOL_OPTIONS:
        return NDIS_STATUS_SUCCESS;
    case OID_PNP_SET_POWER:
        NdisAcquireSpinLock(&a->TxLock);
        if ((NDIS_DEVICE_POWER_STATE)v == NdisDeviceStateD0) {
            a->WorkReq = NET_WORK_D0;
        } else {
            a->PowerD3 = 1;
            a->WorkReq = NET_WORK_D3;
        }
        NdisReleaseSpinLock(&a->TxLock);
        (VOID)KeSetEvent(&a->WorkEvent, IO_NO_INCREMENT, FALSE);
        return NDIS_STATUS_PENDING;
    default:
        *read = 0;
        return NDIS_STATUS_NOT_SUPPORTED;
    }
}

/* ------------------------------------------------------------------ */
/* Entry                                                               */
/* ------------------------------------------------------------------ */

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING path);

/* IRQL: PASSIVE_LEVEL. */
NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING path)
{
    NDIS_MINIPORT_CHARACTERISTICS chars;
    NDIS_STATUS status;

    if (netFlavourMarker[0] == 0 || netVersionString[0] == 0) {
        return STATUS_UNSUCCESSFUL;
    }
    NdisAllocateSpinLock(&netLiveLock);
    NdisMInitializeWrapper(&netWrapper, driver, path, NULL);
    NetZero(&chars, sizeof(chars));
    chars.MajorNdisVersion = NET_NDIS_MAJOR;
    chars.MinorNdisVersion = NET_NDIS_MINOR;
    chars.InitializeHandler = netInitialize;
    chars.HaltHandler = netHalt;
    chars.QueryInformationHandler = netQuery;
    chars.SetInformationHandler = netSet;
    chars.ResetHandler = NetReset;
    chars.SendPacketsHandler = NetSendPackets;
    chars.ReturnPacketHandler = NetReturnPacket;
#if defined(NDIS51_MINIPORT)
    chars.AdapterShutdownHandler = netShutdown;
    chars.PnPEventNotifyHandler = netPnpEvent;
#endif
    status = NdisMRegisterMiniport(netWrapper, &chars, sizeof(chars));
    if (status != NDIS_STATUS_SUCCESS) {
        NdisTerminateWrapper(netWrapper, NULL);
        return STATUS_UNSUCCESSFUL;
    }
    return STATUS_SUCCESS;
}
