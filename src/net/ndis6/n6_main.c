/*
 * n6_main.c - xhcinet6.sys's entry, MiniportInitializeEx, MiniportHaltEx,
 * pause and restart, reset, the PnP event and shutdown (design record 35
 * sections 7.3, 7.5 to 7.7, 7.9 and 8.3; task 45.59 step S7). The data
 * path is n6_data.c, the OIDs and power n6_oid.c, the interface n6_cm.c.
 *
 * IRQL: as each function says; NDIS's own contract for each handler.
 */

#include "xhcinet6.h"
#include "../../xhci_version.h"

#if defined(XHCI_FLAVOUR_QEMU)
#define NET6_FLAVOUR_NAME "XHCI98_FLAVOUR_QEMU"
#elif defined(XHCI_FLAVOUR_RELEASE)
#define NET6_FLAVOUR_NAME "XHCI98_FLAVOUR_RELEASE"
#else
#error "no build flavour was defined. src/net/ndis6/sources derives XHCI_FLAVOUR_RELEASE or _QEMU from BUILD_ALT_DIR; build through scripts\build-driver.cmd."
#endif

#if NDIS_MINIPORT_MAJOR_VERSION != 6 || NDIS_MINIPORT_MINOR_VERSION != 20
#error "xhcinet6.sys is an NDIS 6.20 miniport: src/net/ndis6/sources defines NDIS620_MINIPORT."
#endif

static volatile const char net6FlavourMarker[] = NET6_FLAVOUR_NAME;
static volatile const char net6VersionString[] = "xhcinet6 " XHCI_VER_STR;

static NDIS_HANDLE net6Driver;

DRIVER_INITIALIZE DriverEntry;
static MINIPORT_UNLOAD net6Unload;
static MINIPORT_INITIALIZE net6Initialize;
static MINIPORT_HALT net6Halt;
static MINIPORT_PAUSE net6PauseHandler;
static MINIPORT_RESTART net6Restart;
static MINIPORT_RESET net6Reset;
static MINIPORT_DEVICE_PNP_EVENT_NOTIFY net6PnpEvent;
static MINIPORT_SHUTDOWN net6Shutdown;

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

/* XhciNetMtu (section 8.3): out of range refused, the default stands,
 * and *refused is the value read (logged once the interface is held).
 * IRQL: PASSIVE_LEVEL. */
static ULONG net6ReadMtu(NDIS_HANDLE handle, PULONG refused)
{
    NDIS_STRING name = NDIS_STRING_CONST("XhciNetMtu");
    NDIS_CONFIGURATION_OBJECT co;
    PNDIS_CONFIGURATION_PARAMETER p;
    NDIS_HANDLE cfg = NULL;
    NDIS_STATUS status;
    ULONG mtu = NET_MTU_DEFAULT;

    Net6Zero(&co, sizeof(co));
    co.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    co.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    co.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
    co.NdisHandle = handle;
    co.Flags = 0;
    status = NdisOpenConfigurationEx(&co, &cfg);
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
/* Initialise and halt                                                 */
/* ------------------------------------------------------------------ */

/* IRQL: PASSIVE_LEVEL. */
static VOID net6FreeAdapter(PNET6_ADAPTER a)
{
    Net6RxFree(a);
    Net6CmRelease(a);
    if (a->WorkItem != NULL) {
        NdisFreeIoWorkItem(a->WorkItem);
        a->WorkItem = NULL;
    }
    NdisFreeSpinLock(&a->Lock);
    NdisFreeMemory(a, 0, 0);
}

/* The registration attributes, first of all (NDIS 6's order).
 * IRQL: PASSIVE_LEVEL. */
static NDIS_STATUS net6SetRegistration(PNET6_ADAPTER a)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES ra;

    Net6Zero(&ra, sizeof(ra));
    ra.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    ra.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    ra.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    ra.MiniportAdapterContext = (NDIS_HANDLE)a;
    ra.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    ra.CheckForHangTimeInSeconds = 0;
    ra.InterfaceType = NdisInterfaceInternal;
    return NdisMSetMiniportAttributes(a->Handle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&ra);
}

/*
 * The general attributes, revision 2 (NDIS 6.20; record 35 sections 7.5
 * and 7.7): 802.3 with no offload, MtuSize the MTU, the link down until
 * the service is up, every packet filter, no wake: an NDIS_PM_CAPABILITIES
 * with every wake-up and protocol-offload capability off and its minimum
 * device states unspecified. IRQL: PASSIVE_LEVEL.
 */
static NDIS_STATUS net6SetGeneral(PNET6_ADAPTER a)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES ga;
    ULONG i;

    Net6Zero(&a->Pm, sizeof(a->Pm));
    a->Pm.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    a->Pm.Header.Revision = NDIS_PM_CAPABILITIES_REVISION_1;
    a->Pm.Header.Size = NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_1;
    a->Pm.MinMagicPacketWakeUp = NdisDeviceStateUnspecified;
    a->Pm.MinPatternWakeUp = NdisDeviceStateUnspecified;
    a->Pm.MinLinkChangeWakeUp = NdisDeviceStateUnspecified;

    Net6Zero(&ga, sizeof(ga));
    ga.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    ga.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
    ga.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
    ga.Flags = 0;
    ga.MediaType = NdisMedium802_3;
    ga.PhysicalMediumType = NdisPhysicalMediumUnspecified;
    ga.MtuSize = a->Mtu;
    Net6Bps(400000UL, &ga.MaxXmitLinkSpeed);
    ga.MaxRcvLinkSpeed = ga.MaxXmitLinkSpeed;
    ga.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    ga.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    ga.MediaConnectState = MediaConnectStateDisconnected;
    ga.MediaDuplexState = MediaDuplexStateFull;
    ga.LookaheadSize = a->Mtu;
    ga.PowerManagementCapabilities = NULL;
    ga.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                    NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                    NDIS_MAC_OPTION_NO_LOOPBACK;
    ga.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                NDIS_PACKET_TYPE_MULTICAST |
                                NDIS_PACKET_TYPE_ALL_MULTICAST |
                                NDIS_PACKET_TYPE_BROADCAST |
                                NDIS_PACKET_TYPE_PROMISCUOUS;
    ga.MaxMulticastListSize = NET6_MCAST_MAX;
    ga.MacAddressLength = 6;
    for (i = 0; i < 6; i++) {
        ga.PermanentMacAddress[i] = a->Mac[i];
        ga.CurrentMacAddress[i] = a->Mac[i];
    }
    ga.RecvScaleCapabilities = NULL;
    ga.AccessType = NET_IF_ACCESS_BROADCAST;
    ga.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    ga.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    ga.IfType = IF_TYPE_ETHERNET_CSMACD;
    ga.IfConnectorPresent = TRUE;
    ga.SupportedStatistics = NDIS_STATISTICS_XMIT_OK_SUPPORTED |
                             NDIS_STATISTICS_RCV_OK_SUPPORTED |
                             NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
                             NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
                             NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED |
                             NDIS_STATISTICS_DIRECTED_FRAMES_XMIT_SUPPORTED |
                             NDIS_STATISTICS_DIRECTED_FRAMES_RCV_SUPPORTED |
                             NDIS_STATISTICS_BYTES_RCV_SUPPORTED |
                             NDIS_STATISTICS_BYTES_XMIT_SUPPORTED |
                             NDIS_STATISTICS_RCV_DISCARDS_SUPPORTED |
                             NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED |
                             NDIS_STATISTICS_XMIT_DISCARDS_SUPPORTED;
    ga.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    ga.DataBackFillSize = 0;
    ga.ContextBackFillSize = 0;
    ga.SupportedOidList = (PNDIS_OID)Net6Oids;
    ga.SupportedOidListLength = Net6OidsSize;
    ga.AutoNegotiationFlags = 0;
    ga.PowerManagementCapabilitiesEx = &a->Pm;
    return NdisMSetMiniportAttributes(a->Handle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&ga);
}

/*
 * MiniportInitializeEx (record 35 section 7.5): the adapter, its
 * registration and general attributes, the interface, the receive pool,
 * the power work item, then Open. The adapter is left Paused: nothing is
 * indicated until MiniportRestart. IRQL: PASSIVE_LEVEL.
 */
static NDIS_STATUS net6Initialize(NDIS_HANDLE handle, NDIS_HANDLE driverCtx,
                                  PNDIS_MINIPORT_INIT_PARAMETERS params)
{
    PNET6_ADAPTER a;
    PDEVICE_OBJECT pdo;
    PDEVICE_OBJECT fdo;
    PDEVICE_OBJECT lower;
    XHCI_CM_NET_INFO info;
    NDIS_STATUS status;
    ULONG asked;
    ULONG i;

    UNREFERENCED_PARAMETER(driverCtx);
    UNREFERENCED_PARAMETER(params);

    a = (PNET6_ADAPTER)NdisAllocateMemoryWithTagPriority(
        handle, sizeof(*a), NET6_TAG, NormalPoolPriority);
    if (a == NULL) {
        return NDIS_STATUS_RESOURCES;
    }
    Net6Zero(a, sizeof(*a));
    a->Handle = handle;
    a->State = NET6_PAUSED;
    NdisAllocateSpinLock(&a->Lock);
    NetTxInit(&a->Tx);

    status = net6SetRegistration(a);
    if (status != NDIS_STATUS_SUCCESS) {
        net6FreeAdapter(a);
        return status;
    }

    asked = 0;
    a->Mtu = net6ReadMtu(handle, &asked);
    a->MaxFrame = a->Mtu + NET_ETH_HDR;
    a->Lookahead = a->Mtu;

    pdo = NULL;
    fdo = NULL;
    lower = NULL;
    NdisMGetDeviceProperty(handle, &pdo, &fdo, &lower, NULL, NULL);
    if (lower == NULL) {
        net6FreeAdapter(a);
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }
    a->Lower = lower;

    status = Net6CmAcquire(a);
    if (status != NDIS_STATUS_SUCCESS) {
        net6FreeAdapter(a);
        return status;
    }
    if (asked != 0) {
        Net6CmLog(a, "net.mtu.range", asked);
    }
    Net6CmQuery(a, &info);
    for (i = 0; i < 6; i++) {
        a->Mac[i] = info.Mac[i];
    }

    status = net6SetGeneral(a);
    if (status != NDIS_STATUS_SUCCESS) {
        Net6CmLog(a, "net.attr.fail", (ULONG)status);
        net6FreeAdapter(a);
        return status;
    }

    status = Net6RxAlloc(a);
    if (status != NDIS_STATUS_SUCCESS) {
        Net6CmLog(a, "net.nomem", a->MaxFrame);
        net6FreeAdapter(a);
        return status;
    }
    a->WorkItem = NdisAllocateIoWorkItem(handle);
    if (a->WorkItem == NULL) {
        net6FreeAdapter(a);
        return NDIS_STATUS_RESOURCES;
    }

    status = Net6CmOpen(a);
    if (status != NDIS_STATUS_SUCCESS) {
        net6FreeAdapter(a);
        return status;
    }
    Net6CmLog(a, "net.init", a->Mtu);
    return NDIS_STATUS_SUCCESS;
}

/*
 * MiniportHaltEx (record 35 section 7.6): NDIS halts a paused adapter,
 * so no send is queued or in flight and every indicated list is back;
 * Close (a logout, the directory withdrawn, no callback after it), then
 * everything freed. IRQL: PASSIVE_LEVEL.
 */
static VOID net6Halt(NDIS_HANDLE ctx, NDIS_HALT_ACTION action)
{
    PNET6_ADAPTER a = (PNET6_ADAPTER)ctx;

    UNREFERENCED_PARAMETER(action);
    Net6CmClose(a);
    Net6CmLog(a, "net.halt", a->RxOk);
    net6FreeAdapter(a);
}

/* ------------------------------------------------------------------ */
/* Pause, restart, reset                                               */
/* ------------------------------------------------------------------ */

/* MiniportPause (n6_data.c's Net6Pause). IRQL: PASSIVE_LEVEL. */
static NDIS_STATUS net6PauseHandler(NDIS_HANDLE ctx,
                                    PNDIS_MINIPORT_PAUSE_PARAMETERS params)
{
    UNREFERENCED_PARAMETER(params);
    return Net6Pause((PNET6_ADAPTER)ctx);
}

/* MiniportRestart: Running, and the link state indicated as it is now,
 * which also gives the stack a link that came up while paused or during
 * the initialisation. IRQL: PASSIVE_LEVEL. */
static NDIS_STATUS net6Restart(NDIS_HANDLE ctx,
                               PNDIS_MINIPORT_RESTART_PARAMETERS params)
{
    PNET6_ADAPTER a = (PNET6_ADAPTER)ctx;

    UNREFERENCED_PARAMETER(params);
    NdisAcquireSpinLock(&a->Lock);
    a->State = NET6_RUNNING;
    a->Restarted = 1;
    NdisReleaseSpinLock(&a->Lock);
    Net6IndicateLink(a);
    Net6CmLog(a, "net.restart", a->LinkUp);
    return NDIS_STATUS_SUCCESS;
}

/* MiniportResetEx: nothing below is reset (record 35 section 7.5); an
 * NDIS 6.20 reset carries no duty over pending sends, whose queue only a
 * pause drains. IRQL: <= DISPATCH_LEVEL. */
static NDIS_STATUS net6Reset(NDIS_HANDLE ctx, PBOOLEAN addressingReset)
{
    PNET6_ADAPTER a = (PNET6_ADAPTER)ctx;

    *addressingReset = FALSE;
    Net6CmLog(a, "net.reset", 0);
    return NDIS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* PnP and shutdown                                                    */
/* ------------------------------------------------------------------ */

/* MiniportDevicePnPEventNotify: a surprise removal ends sends at once
 * (record 35 section 7.9); the halt follows. IRQL: PASSIVE_LEVEL. */
static VOID net6PnpEvent(NDIS_HANDLE ctx, PNET_DEVICE_PNP_EVENT event)
{
    PNET6_ADAPTER a = (PNET6_ADAPTER)ctx;

    if (event->DevicePnPEvent == NdisDevicePnPEventSurpriseRemoved) {
        NdisAcquireSpinLock(&a->Lock);
        a->Removed = 1;
        NdisReleaseSpinLock(&a->Lock);
        Net6LinkDown(a);
        Net6CmLog(a, "net.removed", 0);
    }
}

/* MiniportShutdownEx: the connection manager ends the service at its own
 * shutdown; nothing here touches hardware. IRQL: any. */
static VOID net6Shutdown(NDIS_HANDLE ctx, NDIS_SHUTDOWN_ACTION action)
{
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(action);
}

/* ------------------------------------------------------------------ */
/* Entry                                                               */
/* ------------------------------------------------------------------ */

/* IRQL: PASSIVE_LEVEL. */
static VOID net6Unload(PDRIVER_OBJECT driver)
{
    UNREFERENCED_PARAMETER(driver);
    NdisMDeregisterMiniportDriver(net6Driver);
}

/* IRQL: PASSIVE_LEVEL. */
NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING path)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS ch;
    NDIS_STATUS status;

    if (net6FlavourMarker[0] == 0 || net6VersionString[0] == 0) {
        return STATUS_UNSUCCESSFUL;
    }
    Net6Zero(&ch, sizeof(ch));
    ch.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    ch.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    ch.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    ch.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    ch.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    /* Informational to NDIS; the file version is the package's
     * (XHCI_VER_STR). */
    ch.MajorDriverVersion = 1;
    ch.MinorDriverVersion = 0;
    ch.Flags = 0;
    ch.SetOptionsHandler = NULL;
    ch.InitializeHandlerEx = net6Initialize;
    ch.HaltHandlerEx = net6Halt;
    ch.UnloadHandler = net6Unload;
    ch.PauseHandler = net6PauseHandler;
    ch.RestartHandler = net6Restart;
    ch.OidRequestHandler = Net6OidRequest;
    ch.SendNetBufferListsHandler = Net6SendNbls;
    ch.ReturnNetBufferListsHandler = Net6ReturnNbls;
    ch.CancelSendHandler = Net6CancelSend;
    ch.CheckForHangHandlerEx = NULL;
    ch.ResetHandlerEx = net6Reset;
    ch.DevicePnPEventNotifyHandler = net6PnpEvent;
    ch.ShutdownHandlerEx = net6Shutdown;
    ch.CancelOidRequestHandler = Net6CancelOid;
    ch.DirectOidRequestHandler = NULL;
    ch.CancelDirectOidRequestHandler = NULL;
    status = NdisMRegisterMiniportDriver(driver, path, NULL, &ch,
                                         &net6Driver);
    if (status != NDIS_STATUS_SUCCESS) {
        return STATUS_UNSUCCESSFUL;
    }
    return STATUS_SUCCESS;
}
