/*
 * cm_nvlinkos.c - the driver half of the nvme98 link-state channel
 * (cm_nvlink.h; roadmap-usb4.md tasks 39-A.7 and 40-A.7): the table's one
 * allocation, the NT callback object, the Windows 98 SE and ME registry
 * record, and the seam's calls, over cm_nvlink.c's pure entry protocol.
 *
 * ONE TABLE PER DRIVER LOAD. CmNvlinkInit allocates it (CmAlloc, so in
 * cm_pool.c's one pool site) and nothing ever frees it. A reload of this
 * driver (every NHI and filter gone, then back) allocates a new one and
 * answers queries with it; an nvme98 instance bound before the reload
 * keeps polling the old one, which stays valid and frozen at its last
 * state, so it gets no statement and falls back to its gone deadline
 * (MD-1). That is the one thing a reload loses.
 *
 * SHARED BY EVERY DOMAIN. Each NHI's thread writes the entries of its own
 * tunnel ports; cmNvLock serialises the writes and the shadow below
 * (which domain marked an entry, on which bridge, when). nvme98 reads
 * without the lock, by the entry's sequence (cm_nvlink.c).
 *
 * WINDOWS 98 SE AND ME. No callback object: the rendezvous is the record
 * cm_nvlink.h describes, under HKEY_DYN_DATA, written through the VMM's
 * registry services (_RegCreateKey 14Ah, _RegSetValueEx 152h, _RegCloseKey
 * 149h: their places in the Windows 98 DDK's VMM.H service table, counted
 * from Get_VMM_Version, service 0, and checked against VMM.INC) reached by
 * the same INT 20h dynalink cm_cfgmg.c uses for CONFIGMG, from the NHI's
 * thread, a passive thread of the system VM. x86 only.
 *
 * IRQL: as cm_nvlink.h; the callback at the caller's IRQL (nvme98 calls
 * it at PASSIVE_LEVEL from HwFindAdapter), touching nothing but an
 * interlocked count and the reply.
 */

#include "cm.h"
#include "cm_cfgmg.h"
#include "cm_nvlink.h"

#if defined(_WIN64) && defined(NO_LEGACY_DRIVERS)
NTHALAPI ULONG HalGetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber,
                             ULONG SlotNumber, PVOID Buffer, ULONG Length);
#endif

/* The shadow of each entry, the connection manager's own. */
typedef struct _CM_NV_SHADOW {
    PCM_NHI Owner;      /* the domain that last published or marked it   */
    ULONG Bridge;       /* its tunnel port, XHCI_TBT_BDF                 */
    ULONG Waiting;      /* DOWN_QUIESCED set, acknowledgement awaited    */
    ULONG MarkedAt;     /* the owner's clock at the mark, ms             */
} CM_NV_SHADOW;

static PNVME98LS_TABLE cmNvTable;
static KSPIN_LOCK cmNvLock;
static CM_NV_SHADOW cmNvShadow[NVME98LS_ENTRIES];
static BOOLEAN cmNvNt;
static PCALLBACK_OBJECT cmNvObject;
static PVOID cmNvRegistration;
static LONG cmNvQueries;
static ULONG cmNvRecord;        /* 9x: the record was written            */
static ULONG cmNvRecordTried;
static ULONG cmNvRecordError;
static ULONG cmNvRemoveSeq;

/* Counters, for the log at each NHI's stop. */
static ULONG cmNvUps;
static ULONG cmNvUpsFull;
static ULONG cmNvSent;
static ULONG cmNvWithheld;
static ULONG cmNvAcked;
static ULONG cmNvAckTimeouts;
static ULONG cmNvNoReceiver;

/* ====================================================================== */
/* Configuration space and the clock                                      */
/* ====================================================================== */

/* One DWORD of `bus`/`slot` (PCI_SLOT_NUMBER) at `offset`: TRUE when read;
 * a function that does not answer reads all-ones. PASSIVE_LEVEL. */
static BOOLEAN cmNvCfg(ULONG bus, ULONG slot, ULONG offset, PULONG value)
{
    UCHAR buffer[64];
    ULONG need;
    ULONG got;

    need = offset + 4UL;
    if (need > sizeof(buffer)) {
        return FALSE;
    }
    got = HalGetBusData(PCIConfiguration, bus, slot, buffer, need);
    if (got >= need) {
        *value = (ULONG)buffer[offset] | ((ULONG)buffer[offset + 1] << 8) |
                 ((ULONG)buffer[offset + 2] << 16) |
                 ((ULONG)buffer[offset + 3] << 24);
        return TRUE;
    }
    if (got == 2) {
        *value = 0xFFFFFFFFUL;
        return TRUE;
    }
    return FALSE;
}

static ULONG cmNvNow(PCM_NHI nhi)
{
    return nhi->Host.NowMs(nhi->Host.Context);
}

/* Between reads of a wait: the connection manager keeps decoding its
 * events (the seam's polls do the same), then a short sleep. */
static VOID cmNvPause(PCM_NHI nhi)
{
    TbtIcmService(&nhi->Icm);
    nhi->Host.Sleep(nhi->Host.Context, CM_NVLINK_POLL_MS);
}

/*
 * THE READING. The seam's poll has no SHPC writer, so on the emulator's
 * i440FX bridge it reads nothing (run-39.md, 39-A.5). The channel reads
 * the member itself: tbt_topo.c's XhciTbtPortRead over HalGetBusData and,
 * for the SHPC DWORD Select byte, CmRegConfigWrite (IRP_MN_WRITE_CONFIG to
 * the bridge's PDO, a capability offset, never the header), so on NT the
 * SHPC slot's presence is read; Windows 98 SE and ME have no writer and no
 * SHPC reading, and no statement follows there. Serialised by the thread.
 */
static ULONG cmNvCfgRead(PVOID context, ULONG bdf, ULONG offset,
                         PULONG value)
{
    UNREFERENCED_PARAMETER(context);
    return cmNvCfg(XHCI_TBT_BDF_BUS(bdf),
                   XHCI_TBT_BDF_DEV(bdf) | (XHCI_TBT_BDF_FN(bdf) << 5),
                   offset, value) ? 1UL : 0UL;
}

static ULONG cmNvCfgWrite(PVOID context, ULONG bdf, ULONG offset,
                          ULONG width, ULONG value)
{
    return CmRegConfigWrite((PCM_NHI)context, bdf, offset, width, value)
               ? 1UL : 0UL;
}

/* Reads `bridge` until it reads `want` (XHCI_TBT_PORT_*) or `budgetMs`
 * pass; returns the last verdict. */
static ULONG cmNvRead(PCM_NHI nhi, ULONG bridge, ULONG want, ULONG budgetMs)
{
    XHCI_TBT_CFG io;
    XHCI_TBT_PORT port;
    ULONG verdict;
    ULONG start;

    io.Read = cmNvCfgRead;
    io.Write = cmNvNt ? cmNvCfgWrite : NULL;
    io.Context = nhi;
    io.Reads = 0;
    io.Writes = 0;
    start = cmNvNow(nhi);
    for (;;) {
        verdict = XhciTbtPortRead(&io, bridge, &port);
        if (verdict == want || verdict == XHCI_TBT_PORT_NONE ||
            CmNvlinkLeft(start, cmNvNow(nhi), budgetMs) <
                XHCI_TBT_POLL_STEP_MS) {
            return verdict;
        }
        TbtIcmService(&nhi->Icm);
        nhi->Host.Sleep(nhi->Host.Context, XHCI_TBT_POLL_STEP_MS);
    }
}

/* The source of member `bridge` of this domain's set, or 0. */
static ULONG cmNvSource(PCM_NHI nhi, ULONG bridge)
{
    ULONG i;

    for (i = 0; i < nhi->Reenum.Topo.Tunnels; i++) {
        if (nhi->Reenum.Topo.Set[i] == bridge) {
            return nhi->Reenum.Topo.Source[i];
        }
    }
    return 0;
}

/* The bridge's secondary bus, or CM_NVLINK_NONE. */
static ULONG cmNvSecondary(ULONG bridge)
{
    ULONG buses;
    ULONG bus;

    if (!cmNvCfg(XHCI_TBT_BDF_BUS(bridge),
                 XHCI_TBT_BDF_DEV(bridge) | (XHCI_TBT_BDF_FN(bridge) << 5),
                 XHCI_TBT_CFG_BUSES, &buses) ||
        buses == 0xFFFFFFFFUL) {
        return CM_NVLINK_NONE;
    }
    bus = (buses >> 8) & 0xFFUL;
    return bus == 0 ? CM_NVLINK_NONE : bus;
}

/* ====================================================================== */
/* The rendezvous                                                         */
/* ====================================================================== */

static VOID cmNvCallback(PVOID context, PVOID argument1, PVOID argument2)
{
    UNREFERENCED_PARAMETER(context);
    if (argument1 == NVME98LS_QUERY && argument2 != NULL &&
        cmNvTable != NULL) {
        *(PNVME98LS_TABLE *)argument2 = cmNvTable;
        InterlockedIncrement(&cmNvQueries);
    }
}

VOID CmNvlinkInit(VOID)
{
    static WCHAR name[] = NVME98LS_CALLBACK_NAME;
    UNICODE_STRING text;
    OBJECT_ATTRIBUTES attributes;
    NTSTATUS status;

    KeInitializeSpinLock(&cmNvLock);
    cmNvNt = IoIsWdmVersionAvailable(1, 0x10);
    cmNvTable = (PNVME98LS_TABLE)CmAlloc(sizeof(NVME98LS_TABLE));
    if (cmNvTable == NULL) {
        return;
    }
    CmNvlinkTableInit(cmNvTable);
    if (!cmNvNt) {
        return;
    }
    text.Buffer = name;
    text.Length = (USHORT)(sizeof(name) - sizeof(WCHAR));
    text.MaximumLength = (USHORT)sizeof(name);
    InitializeObjectAttributes(&attributes, &text,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT, NULL,
                               NULL);
    status = ExCreateCallback(&cmNvObject, &attributes, TRUE, TRUE);
    if (!NT_SUCCESS(status)) {
        cmNvObject = NULL;
        return;
    }
    cmNvRegistration = ExRegisterCallback(cmNvObject, cmNvCallback, NULL);
    if (cmNvRegistration == NULL) {
        ObDereferenceObject(cmNvObject);
        cmNvObject = NULL;
    }
}

VOID CmNvlinkUnload(VOID)
{
    /* ExUnregisterCallback returns once no call of it is in progress; the
     * object is permanent and stays for the next load to open. */
    if (cmNvRegistration != NULL) {
        ExUnregisterCallback(cmNvRegistration);
        cmNvRegistration = NULL;
    }
    if (cmNvObject != NULL) {
        ObDereferenceObject(cmNvObject);
        cmNvObject = NULL;
    }
}

#if defined(_X86_)

#define CM_NV_VMM_ID            0x0001  /* VMM.H, VMM_DEVICE_ID          */
#define CM_NV_SVC_CLOSE_KEY     0x0149  /* _RegCloseKey                  */
#define CM_NV_SVC_CREATE_KEY    0x014A  /* _RegCreateKey                 */
#define CM_NV_SVC_SET_VALUE     0x0152  /* _RegSetValueEx                */
#define CM_NV_HKEY_DYN_DATA     0x80000006UL
#define CM_NV_REG_BINARY        3UL

typedef ULONG (__cdecl *CM_NV_FN1)(ULONG a);
typedef ULONG (__cdecl *CM_NV_FN3)(ULONG a, ULONG b, ULONG c);
typedef ULONG (__cdecl *CM_NV_FN6)(ULONG a, ULONG b, ULONG c, ULONG d,
                                   ULONG e, ULONG f);

static VOID cmNvThunk(PUCHAR t, ULONG service)
{
    t[0] = 0xCD;
    t[1] = CMG_DYNALINK_INT;
    t[2] = (UCHAR)(service & 0xFFUL);
    t[3] = (UCHAR)(((service | CMG_DYNALINK_JMP) >> 8) & 0xFFUL);
    t[4] = (UCHAR)(CM_NV_VMM_ID & 0xFF);
    t[5] = (UCHAR)((CM_NV_VMM_ID >> 8) & 0xFF);
    t[6] = 0xCC;
    t[7] = 0xCC;
}

/* The record, once per load: 0 or the VMM's registry error. */
static ULONG cmNvWriteRecord(ULONG clock)
{
    static char key[] = CM_NVLINK_DYN_KEY;
    static char value[] = CM_NVLINK_DYN_VALUE;
    UCHAR record[CM_NVLINK_RECORD_BYTES];
    PUCHAR t;
    ULONG hkey;
    ULONG error;
    ULONG cookie;

    t = (PUCHAR)CmAlloc(3 * 8);
    if (t == NULL) {
        return 0xFFFFFFFFUL;
    }
    cmNvThunk(t, CM_NV_SVC_CREATE_KEY);
    cmNvThunk(t + 8, CM_NV_SVC_SET_VALUE);
    cmNvThunk(t + 16, CM_NV_SVC_CLOSE_KEY);
    cookie = (clock * 0x9E3779B1UL) ^ (ULONG)(ULONG_PTR)cmNvTable ^
             0x4E564C4BUL;
    if (cookie == 0) {
        cookie = 1;
    }
    *(volatile ULONG *)&cmNvTable->Reserved = cookie;
    CmNvlinkRecordEncode(record, (ULONG)(ULONG_PTR)cmNvTable,
                         sizeof(NVME98LS_TABLE), cookie);
    hkey = 0;
    error = ((CM_NV_FN3)t)(CM_NV_HKEY_DYN_DATA, (ULONG)(ULONG_PTR)key,
                           (ULONG)(ULONG_PTR)&hkey);
    if (error == 0) {
        error = ((CM_NV_FN6)(t + 8))(hkey, (ULONG)(ULONG_PTR)value, 0,
                                     CM_NV_REG_BINARY,
                                     (ULONG)(ULONG_PTR)record,
                                     CM_NVLINK_RECORD_BYTES);
        (VOID)((CM_NV_FN1)(t + 16))(hkey);
    }
    CmFree(t);
    return error;
}

#endif /* _X86_ */

/* 9x: the record, written at the first tunnel action of the boot rather
 * than at the start: no registry service is called while the NHI's own
 * start is still pending in the configuration manager (the CONFIGMG
 * route's rule, cm_reenum.c). Before any UP, so before nvme98 can look. */
static VOID cmNvEnsure(PCM_NHI nhi)
{
    if (cmNvNt || cmNvTable == NULL || cmNvRecordTried) {
        return;
    }
    cmNvRecordTried = 1;
#if defined(_X86_)
    cmNvRecordError = cmNvWriteRecord(cmNvNow(nhi));
#else
    cmNvRecordError = 0xFFFFFFFEUL;
#endif
    cmNvRecord = cmNvRecordError == 0 ? 1UL : 0UL;
    CmLogNote(nhi, "cm.nvlink.9x.record",
              (cmNvRecord << 31) | (cmNvRecordError & 0xFFFFUL));
}

VOID CmNvlinkStart(PCM_NHI nhi)
{
    if (cmNvTable == NULL) {
        CmLogNote(nhi, "cm.nvlink.no-table", 0);
        return;
    }
    if (cmNvNt) {
        CmLogNote(nhi, "cm.nvlink.nt",
                  (cmNvRegistration != NULL ? 0x80000000UL : 0) |
                      ((ULONG)cmNvQueries & 0xFFFFUL));
        return;
    }
    CmLogNote(nhi, "cm.nvlink.9x", (cmNvRecord << 31) | cmNvRecordTried);
}

/* Whether a receiver can exist, so whether an acknowledgement can come. */
static BOOLEAN cmNvReceiver(VOID)
{
    if (cmNvTable == NULL) {
        return FALSE;
    }
    if (cmNvNt) {
        return (BOOLEAN)(cmNvQueries != 0);
    }
    return (BOOLEAN)(cmNvRecord != 0);
}

/* ====================================================================== */
/* The obligations                                                        */
/* ====================================================================== */

/* Obligation 1's scan of one secondary bus: UP for each NVMe function
 * answering there. Returns (functions answering << 16) | entries UP. */
static ULONG cmNvScan(PCM_NHI nhi, ULONG bridge, ULONG bus)
{
    ULONG dev;
    ULONG fn;
    ULONG id;
    ULONG cls;
    ULONG hdr;
    ULONG slot;
    ULONG index;
    ULONG outcome;
    ULONG count;
    ULONG answering;
    KIRQL irql;

    count = 0;
    answering = 0;
    for (dev = 0; dev < 32UL; dev++) {
        for (fn = 0; fn < 8UL; fn++) {
            slot = dev | (fn << 5);
            if (!cmNvCfg(bus, slot, 0, &id) || id == 0xFFFFFFFFUL ||
                (id & 0xFFFFUL) == 0xFFFFUL) {
                if (fn == 0) {
                    break;
                }
                continue;
            }
            answering++;
            if (cmNvCfg(bus, slot, 8, &cls) &&
                ((cls >> 16) & 0xFFFFUL) == CM_NVLINK_CLASS_NVM) {
                outcome = CM_NVLINK_UP_BAD;
                KeAcquireSpinLock(&cmNvLock, &irql);
                index = CmNvlinkPublishUp(cmNvTable, bus, slot, &outcome);
                if (index != CM_NVLINK_NONE) {
                    cmNvShadow[index].Owner = nhi;
                    cmNvShadow[index].Bridge = bridge;
                    cmNvShadow[index].Waiting = 0;
                    count++;
                    if (outcome != CM_NVLINK_UP_ALREADY) {
                        cmNvUps++;
                    }
                } else {
                    cmNvUpsFull++;
                }
                KeReleaseSpinLock(&cmNvLock, irql);
                if (outcome != CM_NVLINK_UP_ALREADY) {
                    CmLogNote(nhi, "cm.nvlink.up",
                              (outcome << 24) | ((bus & 0xFFUL) << 8) | slot);
                }
            }
            if (fn == 0) {
                if (!cmNvCfg(bus, slot, 0x0C, &hdr) ||
                    ((hdr >> 16) & XHCI_TBT_HDR_MF) == 0) {
                    break;
                }
            }
        }
    }
    return (answering << 16) | count;
}

/* How long a card the SHPC slot reports present may take to answer its
 * configuration cycle before the scan gives up on it. */
#define CM_NV_ANSWER_MS 1000UL

ULONG CmNvlinkExpose(PCM_NHI nhi, ULONG bridge)
{
    ULONG bus;
    ULONG r;
    ULONG start;

    if (cmNvTable == NULL) {
        return 0;
    }
    cmNvEnsure(nhi);
    bus = cmNvSecondary(bridge);
    if (bus == CM_NVLINK_NONE) {
        CmLogNote(nhi, "cm.nvlink.up.no-bus", bridge);
        return 0;
    }
    r = cmNvScan(nhi, bridge, bus);
    if ((r >> 16) == 0 && cmNvSource(nhi, bridge) == XHCI_TBT_SRC_SHPC &&
        cmNvRead(nhi, bridge, XHCI_TBT_PORT_UP, 0) == XHCI_TBT_PORT_UP) {
        /* A card is present and nothing answers yet (Configuration Request
         * Retry, or the link still training): the UP must precede the
         * enumeration, so wait for it here, briefly. */
        start = cmNvNow(nhi);
        while ((r >> 16) == 0 &&
               CmNvlinkLeft(start, cmNvNow(nhi), CM_NV_ANSWER_MS) != 0) {
            TbtIcmService(&nhi->Icm);
            nhi->Host.Sleep(nhi->Host.Context, XHCI_TBT_POLL_STEP_MS);
            r = cmNvScan(nhi, bridge, bus);
        }
        CmLogNote(nhi, "cm.nvlink.up.waited",
                  ((cmNvNow(nhi) - start) << 8) | (r >> 16));
    }
    return r & 0xFFFFUL;
}

/*
 * Every UP entry behind `bridge` (by the shadow, or by its secondary bus
 * read now) leaves UP: with DOWN_QUIESCED when `verdict` is SEND; else
 * withheld, and then only an entry whose function no longer answers (one
 * still answering keeps its UP: its link is not shown down). Returns how
 * many were marked DOWN_QUIESCED.
 */
static ULONG cmNvMark(PCM_NHI nhi, ULONG bridge, ULONG verdict)
{
    ULONG bus;
    ULONG i;
    ULONG id;
    ULONG marked;
    ULONG seen;
    BOOLEAN absent;
    ULONG now;
    KIRQL irql;

    now = cmNvNow(nhi);
    bus = cmNvSecondary(bridge);
    marked = 0;
    seen = 0;
    for (i = 0; i < NVME98LS_ENTRIES; i++) {
        if ((cmNvTable->Entry[i].Flags & NVME98LS_UP) == 0 ||
            (cmNvShadow[i].Bridge != bridge &&
             cmNvTable->Entry[i].BusNumber != bus)) {
            continue;
        }
        seen++;
        absent = TRUE;
        if (verdict != CM_NVLINK_SEND &&
            cmNvCfg(cmNvTable->Entry[i].BusNumber,
                    cmNvTable->Entry[i].SlotNumber, 0, &id) &&
            id != 0xFFFFFFFFUL && (id & 0xFFFFUL) != 0xFFFFUL) {
            absent = FALSE;
        }
        KeAcquireSpinLock(&cmNvLock, &irql);
        if (verdict == CM_NVLINK_SEND) {
            if (CmNvlinkSetDown(cmNvTable, i, TRUE)) {
                cmNvShadow[i].Owner = nhi;
                cmNvShadow[i].Bridge = bridge;
                cmNvShadow[i].Waiting = 1;
                cmNvShadow[i].MarkedAt = now;
                marked++;
            }
        } else if (absent) {
            (VOID)CmNvlinkSetDown(cmNvTable, i, FALSE);
        }
        KeReleaseSpinLock(&cmNvLock, irql);
        CmLogNote(nhi, verdict == CM_NVLINK_SEND ? "cm.nvlink.down"
                                                 : "cm.nvlink.withheld",
                  (verdict << 24) | (i << 16) |
                      (cmNvTable->Entry[i].LinkGeneration & 0xFFFFUL));
    }
    if (seen == 0) {
        return 0;
    }
    if (verdict == CM_NVLINK_SEND && marked != 0) {
        cmNvSent++;
    } else {
        cmNvWithheld++;
    }
    return marked;
}

ULONG CmNvlinkIcmDown(PCM_NHI nhi, ULONG pollState)
{
    PCM_REENUM r;
    ULONG verdict;
    ULONG reading;
    ULONG state;
    ULONG i;

    r = &nhi->Reenum;
    if (cmNvTable == NULL) {
        return CM_NVLINK_NO_CHANNEL;
    }
    /* The channel's own reading of the one SHPC member (the seam's poll
     * reads no SHPC slot): the slot's presence clearing after a device
     * disconnected is the emulator's later-than-stage-2 signal. */
    reading = XHCI_TBT_PORT_ERROR;
    state = XHCI_TBT_POLL_NO_READING;
    if (r->Topo.Tunnels == 1 && r->Topo.Source[0] == XHCI_TBT_SRC_SHPC) {
        reading = cmNvRead(nhi, r->Topo.Set[0], XHCI_TBT_PORT_DOWN,
                           XHCI_TBT_POLL_BUDGET_MS);
        state = reading == XHCI_TBT_PORT_DOWN ? XHCI_TBT_POLL_REACHED
                                              : XHCI_TBT_POLL_TIMEOUT;
    }
    verdict = CmNvlinkIcmVerdict(r->Topo.Tunnels,
                                 r->Topo.Tunnels != 0 ? r->Topo.Source[0] : 0,
                                 state, reading);
    CmLogNote(nhi, "cm.nvlink.icm-down",
              (verdict << 16) | (pollState << 8) | reading);
    if (verdict == CM_NVLINK_NO_SET) {
        return verdict;
    }
    for (i = 0; i < r->Topo.Tunnels; i++) {
        if (cmNvMark(nhi, r->Topo.Set[i], verdict) != 0) {
            return CM_NVLINK_SEND;
        }
    }
    return verdict == CM_NVLINK_SEND ? CM_NVLINK_NO_ENTRY : verdict;
}

ULONG CmNvlinkPortDown(PCM_NHI nhi, ULONG member, BOOLEAN pathClosed,
                       ULONG remainingMs)
{
    PCM_REENUM r;
    ULONG verdict;
    ULONG bridge;
    ULONG reading;
    ULONG start;
    ULONG left;

    r = &nhi->Reenum;
    if (cmNvTable == NULL) {
        return CM_NVLINK_NO_CHANNEL;
    }
    if (member == XHCI_TBT_NO_MEMBER || member >= r->Topo.Tunnels) {
        verdict = CmNvlinkU4Verdict(pathClosed, FALSE, 0, 0, remainingMs);
        CmLogNote(nhi, "cm.nvlink.u4-down", verdict << 16);
        cmNvWithheld++;
        return verdict;
    }
    bridge = r->Topo.Set[member];
    reading = XHCI_TBT_PORT_ERROR;
    left = remainingMs;
    if (pathClosed && r->Topo.Source[member] == XHCI_TBT_SRC_SHPC &&
        remainingMs > CM_NVLINK_DELIVERY_MS) {
        /* Within what is left of the 3 s, the delivery bound kept back. */
        start = cmNvNow(nhi);
        reading = cmNvRead(nhi, bridge, XHCI_TBT_PORT_DOWN,
                           remainingMs - CM_NVLINK_DELIVERY_MS);
        left = CmNvlinkLeft(start, cmNvNow(nhi), remainingMs);
    }
    verdict = CmNvlinkU4Verdict(pathClosed, TRUE, r->Topo.Source[member],
                                reading, left);
    CmLogNote(nhi, "cm.nvlink.u4-down", (verdict << 16) | (left << 4) |
                                            (reading & 0xFUL));
    if (cmNvMark(nhi, bridge, verdict) != 0) {
        return CM_NVLINK_SEND;
    }
    return verdict == CM_NVLINK_SEND ? CM_NVLINK_NO_ENTRY : verdict;
}

VOID CmNvlinkAwait(PCM_NHI nhi)
{
    ULONG i;
    ULONG pending;
    ULONG start;
    ULONG now;
    KIRQL irql;

    if (cmNvTable == NULL) {
        return;
    }
    start = cmNvNow(nhi);
    for (;;) {
        pending = 0;
        now = cmNvNow(nhi);
        KeAcquireSpinLock(&cmNvLock, &irql);
        for (i = 0; i < NVME98LS_ENTRIES; i++) {
            if (cmNvShadow[i].Owner != nhi || !cmNvShadow[i].Waiting) {
                continue;
            }
            if (CmNvlinkAcked(cmNvTable, i)) {
                cmNvShadow[i].Waiting = 0;
                cmNvAcked++;
            } else if (!cmNvReceiver()) {
                /* No nvme98 has the table: no acknowledgement can come. */
                cmNvShadow[i].Waiting = 0;
                cmNvNoReceiver++;
            } else if (CmNvlinkLeft(cmNvShadow[i].MarkedAt, now,
                                    CM_NVLINK_ACK_MS) == 0) {
                cmNvShadow[i].Waiting = 0;
                cmNvAckTimeouts++;
            } else {
                pending++;
            }
        }
        KeReleaseSpinLock(&cmNvLock, irql);
        if (pending == 0) {
            break;
        }
        cmNvPause(nhi);
    }
    CmLogNote(nhi, "cm.nvlink.await", cmNvNow(nhi) - start);
}

ULONG CmNvlinkOrderlyRemove(PCM_NHI nhi, ULONG devnode, ULONG bdf)
{
    ULONG index;
    ULONG cr;
    ULONG seq;
    ULONG answer;
    ULONG start;
    ULONG result;
    KIRQL irql;

    index = CM_NVLINK_NONE;
    if (cmNvTable != NULL && !cmNvNt && cmNvRecord != 0) {
        index = CmNvlinkFind(cmNvTable, XHCI_TBT_BDF_BUS(bdf),
                             XHCI_TBT_BDF_DEV(bdf) |
                                 (XHCI_TBT_BDF_FN(bdf) << 5));
    }
    if (index == CM_NVLINK_NONE ||
        (cmNvTable->Entry[index].Flags & NVME98LS_UP) == 0) {
        CmLogNote(nhi, "cm.nvlink.remove", CM_NVLINK_OR_NO_CHANNEL);
        return CM_NVLINK_OR_NO_CHANNEL;
    }
    /* The file system's quiescence and the veto first: a refusal is
     * final, nothing is forced (MD-5; the usb4-emu handoff's rule). */
    cr = CmCfgmgQueryRemove(&nhi->Reenum.Cfgmg, devnode);
    if (cr != CMG_CR_SUCCESS) {
        CmLogNote(nhi, "cm.nvlink.remove", (cr << 8) | CM_NVLINK_OR_VETOED);
        return CM_NVLINK_OR_VETOED;
    }
    KeAcquireSpinLock(&cmNvLock, &irql);
    cmNvRemoveSeq = (cmNvRemoveSeq + 1) & 0x00FFFFFFUL;
    if (cmNvRemoveSeq == 0) {
        cmNvRemoveSeq = 1;
    }
    seq = cmNvRemoveSeq;
    (VOID)CmNvlinkRemoveRequest(cmNvTable, index, seq);
    KeReleaseSpinLock(&cmNvLock, irql);
    start = cmNvNow(nhi);
    answer = CM_NVLINK_RM_NONE;
    for (;;) {
        answer = CmNvlinkRemoveAnswer(cmNvTable, index, seq);
        if (answer != CM_NVLINK_RM_NONE ||
            CmNvlinkLeft(start, cmNvNow(nhi), CM_NVLINK_REMOVE_MS) == 0) {
            break;
        }
        cmNvPause(nhi);
    }
    if (answer == CM_NVLINK_RM_CLEAN) {
        cr = CmCfgmgRemove(&nhi->Reenum.Cfgmg, devnode);
        result = cr == CMG_CR_SUCCESS ? CM_NVLINK_OR_REMOVED
                                      : CM_NVLINK_OR_REMOVE_FAILED;
    } else {
        result = answer == CM_NVLINK_RM_NONE ? CM_NVLINK_OR_TIMEOUT
                                             : CM_NVLINK_OR_NOT_CLEAN;
        cr = 0;
    }
    KeAcquireSpinLock(&cmNvLock, &irql);
    CmNvlinkRemoveCancel(cmNvTable, index);
    KeReleaseSpinLock(&cmNvLock, irql);
    CmLogNote(nhi, "cm.nvlink.remove", (answer << 16) | (cr << 8) | result);
    return result;
}

VOID CmNvlinkStop(PCM_NHI nhi)
{
    ULONG i;
    KIRQL irql;

    if (cmNvTable == NULL) {
        return;
    }
    KeAcquireSpinLock(&cmNvLock, &irql);
    for (i = 0; i < NVME98LS_ENTRIES; i++) {
        if (cmNvShadow[i].Owner == nhi) {
            cmNvShadow[i].Owner = NULL;
            cmNvShadow[i].Waiting = 0;
        }
    }
    KeReleaseSpinLock(&cmNvLock, irql);
    CmLogNote(nhi, "cm.nvlink.ups", cmNvUps);
    CmLogNote(nhi, "cm.nvlink.full", cmNvUpsFull);
    CmLogNote(nhi, "cm.nvlink.sent", cmNvSent);
    CmLogNote(nhi, "cm.nvlink.withheld", cmNvWithheld);
    CmLogNote(nhi, "cm.nvlink.acked", cmNvAcked);
    CmLogNote(nhi, "cm.nvlink.ack-timeouts", cmNvAckTimeouts);
    CmLogNote(nhi, "cm.nvlink.no-receiver", cmNvNoReceiver);
    CmLogNote(nhi, "cm.nvlink.queries", (ULONG)cmNvQueries);
}

ULONG CmNvlinkIdleMs(ULONG configured)
{
    ULONG i;

    if (configured <= CM_NVLINK_IDLE_MS || cmNvTable == NULL) {
        return configured;
    }
    for (i = 0; i < NVME98LS_ENTRIES; i++) {
        if ((cmNvTable->Entry[i].Flags & NVME98LS_UP) != 0) {
            return CM_NVLINK_IDLE_MS;
        }
    }
    return configured;
}