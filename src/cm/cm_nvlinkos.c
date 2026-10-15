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
 * cm_nvlink.h describes, under HKEY_LOCAL_MACHINE, written through the VMM's
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
    ULONG Id;           /* the function's ids when UP was published      */
} CM_NV_SHADOW;

static PNVME98LS_TABLE cmNvTable;
static KSPIN_LOCK cmNvLock;
static CM_NV_SHADOW cmNvShadow[NVME98LS_ENTRIES];
static BOOLEAN cmNvNt;
static BOOLEAN cmNvOsQualified; /* 2000 x86, XP x64 (record 21 7.2)      */
static PCALLBACK_OBJECT cmNvObject;
static PVOID cmNvRegistration;
static LONG cmNvQueries;
static ULONG cmNvRecord;        /* 9x: the record was written            */
static ULONG cmNvRecordTried;   /* 0 not yet, 1 being written, 2 done    */
static ULONG cmNvRecordError;
static ULONG cmNvRemoveSeq;

/* Each domain's latest connection generation per host USB4 port, from the
 * UP hook: the presence sample is offered to the core under it. */
#define CM_NV_DOMAINS 4UL

typedef struct _CM_NV_U4 {
    PCM_NHI Nhi;
    ULONG Gen[TBT_U4_MAX_PORTS + 1];
    ULONG Seen[TBT_U4_MAX_PORTS + 1];
} CM_NV_U4;

static CM_NV_U4 cmNvU4[CM_NV_DOMAINS];

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
    UCHAR buffer[256];
    ULONG need;
    ULONG got;

    if ((offset & 3UL) != 0 || offset > 252UL) {
        return FALSE;
    }
    need = offset + 4UL;
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
    CmNhiWaitService(nhi);
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

static BOOLEAN cmNvEmulator(ULONG bridge);

/* Reads `bridge` until it reads `want` (XHCI_TBT_PORT_*) or `budgetMs`
 * pass; returns the last verdict, and in `*emulator` whether the bridge
 * read as the emulator's (an SHPC capability, no PCI Express capability). */
static ULONG cmNvRead(PCM_NHI nhi, ULONG bridge, ULONG want, ULONG budgetMs,
                      BOOLEAN *emulator)
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
            verdict == XHCI_TBT_PORT_ERROR ||
            CmNvlinkLeft(start, cmNvNow(nhi), budgetMs) <
                XHCI_TBT_POLL_STEP_MS) {
            *emulator = (BOOLEAN)(port.ShpcCap != 0 && port.PcieCap == 0 &&
                                  cmNvEmulator(bridge));
            return verdict;
        }
        CmNhiWaitService(nhi);
        nhi->Host.Sleep(nhi->Host.Context, XHCI_TBT_POLL_STEP_MS);
    }
}

/*
 * Whether `bridge` is the emulator's tunnel bridge: the bridge itself, or
 * a bridge on bus 0 whose bus range holds it, has QEMU's vendor id 1B36h
 * (usb4-emu's i440FX root bridge `usb4-tunnel-bridge` is 1B36:0001; the
 * generic profile's tunnel bridge is that device itself). Together with
 * the reading's own test (an SHPC capability, no PCI Express capability)
 * this is the explicit admission of the one platform whose slot-empty
 * reading is qualified (record 21 section 7.1); no Thunderbolt or USB4
 * hardware sits below a 1B36 bridge.
 */
#define CM_NV_QEMU_VENDOR 0x1B36UL

static BOOLEAN cmNvEmulator(ULONG bridge)
{
    ULONG bus;
    ULONG dev;
    ULONG fn;
    ULONG id;
    ULONG cls;
    ULONG hdr;
    ULONG buses;
    ULONG slot;

    if (cmNvCfg(XHCI_TBT_BDF_BUS(bridge),
                XHCI_TBT_BDF_DEV(bridge) | (XHCI_TBT_BDF_FN(bridge) << 5), 0,
                &id) &&
        (id & 0xFFFFUL) == CM_NV_QEMU_VENDOR) {
        return TRUE;
    }
    bus = XHCI_TBT_BDF_BUS(bridge);
    for (dev = 0; dev < 32UL; dev++) {
        for (fn = 0; fn < 8UL; fn++) {
            slot = dev | (fn << 5);
            if (!cmNvCfg(0, slot, 0, &id) || (id & 0xFFFFUL) == 0xFFFFUL) {
                if (fn == 0) {
                    break;
                }
                continue;
            }
            if ((id & 0xFFFFUL) == CM_NV_QEMU_VENDOR &&
                cmNvCfg(0, slot, 8, &cls) &&
                ((cls >> 16) & 0xFFFFUL) == 0x0604UL &&
                cmNvCfg(0, slot, XHCI_TBT_CFG_BUSES, &buses) &&
                bus >= ((buses >> 8) & 0xFFUL) &&
                bus <= ((buses >> 16) & 0xFFUL)) {
                return TRUE;
            }
            if (fn == 0 && (!cmNvCfg(0, slot, 0x0C, &hdr) ||
                            ((hdr >> 16) & XHCI_TBT_HDR_MF) == 0)) {
                break;
            }
        }
    }
    return FALSE;
}

/* Whether `bridge` is this domain's own: a member of its set that no
 * other domain claims (CM_PORT_EXCLUSIVE). Not CM_PORT_ACTIVE: on NT a
 * bridge is exclusive before its filter's first BusRelations answer (the
 * baseline) makes it active, and that answer may already report the
 * endpoint, so its UP must not wait for it. */
static BOOLEAN cmNvExclusive(PCM_NHI nhi, ULONG bridge)
{
    ULONG i;

    for (i = 0; i < nhi->Reenum.Members; i++) {
        if (nhi->Reenum.Port[i].Bdf == bridge &&
            (nhi->Reenum.Port[i].Flags & CM_PORT_EXCLUSIVE) != 0) {
            return TRUE;
        }
    }
    return FALSE;
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
    /* The OS and architecture pairs gate G3 qualified: Windows 2000 (x86:
     * WDM 1.10, not 1.20) and XP x64 (amd64: below WDM 6.0, Vista). XP
     * x86 (1.20), Vista and 7 are not admitted until their own
     * qualification exists (record 21 section 7.2). */
#if defined(_WIN64)
    cmNvOsQualified = (BOOLEAN)(cmNvNt && !IoIsWdmVersionAvailable(6, 0));
#else
    cmNvOsQualified = (BOOLEAN)(cmNvNt && !IoIsWdmVersionAvailable(1, 0x20));
#endif
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
#define CM_NV_HKEY_LOCAL_MACHINE 0x80000002UL
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
    static char key[] = CM_NVLINK_REC_KEY;
    static char value[] = CM_NVLINK_REC_VALUE;
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
    error = ((CM_NV_FN3)t)(CM_NV_HKEY_LOCAL_MACHINE, (ULONG)(ULONG_PTR)key,
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
static BOOLEAN cmNvEnsure(PCM_NHI nhi)
{
    KIRQL irql;
    ULONG mine;
    ULONG start;

    if (cmNvTable == NULL) {
        return FALSE;
    }
    if (cmNvNt) {
        return TRUE;
    }
    KeAcquireSpinLock(&cmNvLock, &irql);
    mine = cmNvRecordTried == 0 ? 1UL : 0UL;
    if (mine) {
        cmNvRecordTried = 1;
    }
    KeReleaseSpinLock(&cmNvLock, irql);
    if (!mine) {
        /* Another domain's thread is writing it: wait for it, within a
         * bound; still unwritten after that, this exposure publishes no UP
         * (the channel is unavailable to it) and the next one asks again. */
        start = cmNvNow(nhi);
        while (cmNvRecordTried != 2 &&
               CmNvlinkLeft(start, cmNvNow(nhi), 2000UL) != 0) {
            nhi->Host.Sleep(nhi->Host.Context, CM_NVLINK_POLL_MS);
        }
        return (BOOLEAN)(cmNvRecordTried == 2 && cmNvRecord != 0);
    }
#if defined(_X86_)
    cmNvRecordError = cmNvWriteRecord(cmNvNow(nhi));
#else
    cmNvRecordError = 0xFFFFFFFEUL;
#endif
    cmNvRecord = cmNvRecordError == 0 ? 1UL : 0UL;
    cmNvRecordTried = 2;
    CmLogNote(nhi, "cm.nvlink.9x.record",
              (cmNvRecord << 31) | (cmNvRecordError & 0xFFFFUL));
    return (BOOLEAN)(cmNvRecord != 0);
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
static ULONG cmNvScan(PCM_NHI nhi, ULONG bridge, ULONG bus, BOOLEAN fresh)
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
                index = CmNvlinkFind(cmNvTable, bus, slot);
                if (index != CM_NVLINK_NONE &&
                    cmNvShadow[index].Owner != NULL &&
                    cmNvShadow[index].Owner != nhi &&
                    cmNvShadow[index].Waiting) {
                    /* Another domain's statement is awaited there. */
                    index = CM_NVLINK_NONE;
                } else {
                    index = CmNvlinkPublishUp(cmNvTable, bus, slot, fresh,
                                              &outcome);
                }
                if (index != CM_NVLINK_NONE) {
                    cmNvShadow[index].Owner = nhi;
                    cmNvShadow[index].Bridge = bridge;
                    cmNvShadow[index].Waiting = 0;
                    cmNvShadow[index].Id = id;
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

ULONG CmNvlinkExpose(PCM_NHI nhi, ULONG bridge, BOOLEAN fresh)
{
    ULONG bus;
    ULONG r;
    ULONG start;
    ULONG retired;
    ULONG i;
    BOOLEAN emulator;
    BOOLEAN ready;
    KIRQL irql;

    if (cmNvTable == NULL) {
        return 0;
    }
    /* Only a bridge this domain owns exclusively: one another domain also
     * claims is ambiguous for both, and its entries are not ours. */
    if (!cmNvExclusive(nhi, bridge)) {
        CmLogNote(nhi, "cm.nvlink.up.not-member", bridge);
        return 0;
    }
    /* A statement this domain made is acknowledged (or timed out) before
     * any exposure can retire it or start a removal: a USB4 DOWN hook may
     * have marked one ahead of an UP action in the same batch. */
    CmNvlinkAwait(nhi);
    ready = cmNvEnsure(nhi);
    bus = cmNvSecondary(bridge);
    /* Every exposure retires the statements behind the bridge first, by
     * its bus and by the entries last published or marked for it. */
    retired = 0;
    KeAcquireSpinLock(&cmNvLock, &irql);
    for (i = 0; i < NVME98LS_ENTRIES; i++) {
        if (cmNvShadow[i].Owner != NULL && cmNvShadow[i].Owner != nhi &&
            cmNvShadow[i].Waiting) {
            /* Another domain's statement, still awaited: never ours. */
            continue;
        }
        if ((cmNvShadow[i].Bridge == bridge && cmNvShadow[i].Owner != NULL) ||
            (bus != CM_NVLINK_NONE &&
             (cmNvTable->Entry[i].Flags & NVME98LS_IN_USE) != 0 &&
             cmNvTable->Entry[i].BusNumber == bus)) {
            if (CmNvlinkRetireEntry(cmNvTable, i)) {
                retired++;
            }
        }
    }
    KeReleaseSpinLock(&cmNvLock, irql);
    if (retired != 0) {
        CmLogNote(nhi, "cm.nvlink.retired", (bridge << 8) | retired);
    }
    if (bus == CM_NVLINK_NONE) {
        CmLogNote(nhi, "cm.nvlink.up.no-bus", bridge);
        return 0;
    }
    if (!ready) {
        CmLogNote(nhi, "cm.nvlink.up.no-channel", bridge);
        return 0;
    }
    r = cmNvScan(nhi, bridge, bus, fresh);
    if ((r >> 16) == 0 && fresh &&
        (!cmNvNt ||
         (cmNvSource(nhi, bridge) == XHCI_TBT_SRC_SHPC &&
          cmNvRead(nhi, bridge, XHCI_TBT_PORT_UP, 0, &emulator) ==
              XHCI_TBT_PORT_UP))) {
        /* A card is present and nothing answers yet (Configuration Request
         * Retry, or the link still training): the UP must precede the
         * enumeration, so wait for it here, briefly. On NT the SHPC slot
         * says whether a card is there; on 98 SE and ME nothing reads it,
         * so a fresh exposure of a silent bus always waits the bound. */
        start = cmNvNow(nhi);
        while ((r >> 16) == 0 &&
               CmNvlinkLeft(start, cmNvNow(nhi), CM_NV_ANSWER_MS) != 0) {
            CmNhiWaitService(nhi);
            nhi->Host.Sleep(nhi->Host.Context, XHCI_TBT_POLL_STEP_MS);
            r = cmNvScan(nhi, bridge, bus, fresh);
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

    if (!cmNvExclusive(nhi, bridge)) {
        return 0;
    }
    now = cmNvNow(nhi);
    bus = cmNvSecondary(bridge);
    marked = 0;
    seen = 0;
    for (i = 0; i < NVME98LS_ENTRIES; i++) {
        if ((cmNvTable->Entry[i].Flags & NVME98LS_UP) == 0 ||
            (cmNvShadow[i].Bridge != bridge &&
             cmNvTable->Entry[i].BusNumber != bus) ||
            (cmNvShadow[i].Owner != NULL && cmNvShadow[i].Owner != nhi)) {
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
    BOOLEAN emulator;

    r = &nhi->Reenum;
    emulator = FALSE;
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
                           XHCI_TBT_POLL_BUDGET_MS, &emulator);
        state = reading == XHCI_TBT_PORT_DOWN ? XHCI_TBT_POLL_REACHED
                                              : XHCI_TBT_POLL_TIMEOUT;
    }
    verdict = CmNvlinkIcmVerdict(r->Topo.Tunnels,
                                 r->Topo.Tunnels != 0 ? r->Topo.Source[0] : 0,
                                 emulator, cmNvOsQualified, state, reading);
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
    BOOLEAN emulator;

    r = &nhi->Reenum;
    emulator = FALSE;
    if (cmNvTable == NULL) {
        return CM_NVLINK_NO_CHANNEL;
    }
    if (member == XHCI_TBT_NO_MEMBER || member >= r->Topo.Tunnels) {
        verdict = CmNvlinkU4Verdict(pathClosed, FALSE, 0, FALSE, FALSE, 0,
                                    remainingMs);
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
                           remainingMs - CM_NVLINK_DELIVERY_MS, &emulator);
        left = CmNvlinkLeft(start, cmNvNow(nhi), remainingMs);
    }
    verdict = CmNvlinkU4Verdict(pathClosed, TRUE, r->Topo.Source[member],
                                emulator, cmNvOsQualified, reading, left);
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
    ULONG rounds;
    KIRQL irql;

    if (cmNvTable == NULL) {
        return;
    }
    pending = 0;
    KeAcquireSpinLock(&cmNvLock, &irql);
    for (i = 0; i < NVME98LS_ENTRIES; i++) {
        if (cmNvShadow[i].Owner == nhi && cmNvShadow[i].Waiting) {
            pending++;
        }
    }
    KeReleaseSpinLock(&cmNvLock, irql);
    if (pending == 0) {
        return;
    }
    start = cmNvNow(nhi);
    rounds = 0;
    for (;;) {
        pending = 0;
        now = cmNvNow(nhi);
        KeAcquireSpinLock(&cmNvLock, &irql);
        for (i = 0; i < NVME98LS_ENTRIES; i++) {
            if (cmNvShadow[i].Owner != nhi || !cmNvShadow[i].Waiting) {
                continue;
            }
            rounds |= 0x80000000UL;
            if (CmNvlinkAcked(cmNvTable, i)) {
                cmNvShadow[i].Waiting = 0;
                cmNvAcked++;
            } else if ((cmNvTable->Entry[i].Flags &
                        NVME98LS_DOWN_QUIESCED) == 0) {
                /* Superseded by a later UP or retirement. */
                cmNvShadow[i].Waiting = 0;
            } else if (!cmNvReceiver()) {
                /* No nvme98 has the table: no acknowledgement can come. */
                cmNvShadow[i].Waiting = 0;
                cmNvNoReceiver++;
            } else if (CmNvlinkLeft(cmNvShadow[i].MarkedAt, now,
                                    CmNvlinkAckBoundMs(cmNvNt)) == 0) {
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
        rounds++;
        cmNvPause(nhi);
    }
    if (rounds != 0) {
        CmLogNote(nhi, "cm.nvlink.await", cmNvNow(nhi) - start);
    }
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
    for (i = 0; i < CM_NV_DOMAINS; i++) {
        if (cmNvU4[i].Nhi == nhi) {
            cmNvU4[i].Nhi = NULL;
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

/* ====================================================================== */
/* The USB4 profile (record 22 section 10; task 40-A.7)                   */
/* ====================================================================== */


/* This domain's slot, claimed (and initialised before it is published)
 * under cmNvLock when `create`; only the owning domain's thread uses the
 * slot's fields afterwards. */
static CM_NV_U4 *cmNvU4Of(PCM_NHI nhi, BOOLEAN create)
{
    ULONG i;
    ULONG k;
    CM_NV_U4 *u;
    KIRQL irql;

    u = NULL;
    KeAcquireSpinLock(&cmNvLock, &irql);
    for (i = 0; i < CM_NV_DOMAINS && u == NULL; i++) {
        if (cmNvU4[i].Nhi == nhi) {
            u = &cmNvU4[i];
        }
    }
    for (i = 0; i < CM_NV_DOMAINS && u == NULL && create; i++) {
        if (cmNvU4[i].Nhi == NULL) {
            for (k = 0; k <= TBT_U4_MAX_PORTS; k++) {
                cmNvU4[i].Gen[k] = 0;
                cmNvU4[i].Seen[k] = 0;
            }
            cmNvU4[i].Nhi = nhi;
            u = &cmNvU4[i];
        }
    }
    KeReleaseSpinLock(&cmNvLock, irql);
    return u;
}

static ULONG cmNvU4Bridge(PCM_NHI nhi, ULONG port);

VOID CmNvlinkU4Up(PVOID context, const TBT_U4_ACTION *action)
{
    PCM_NHI nhi;
    CM_NV_U4 *u;
    ULONG bridge;
    ULONG i;

    nhi = (PCM_NHI)context;
    u = cmNvU4Of(nhi, TRUE);
    if (u != NULL && action->Port >= 1 && action->Port <= TBT_U4_MAX_PORTS) {
        u->Gen[action->Port] = action->Generation;
        u->Seen[action->Port] = 1;
    }
    CmLogNote(nhi, "cm.nvlink.u4-up",
              ((action->Port & 0xFFUL) << 16) |
                  (action->Generation & 0xFFFFUL));
    /* The new establishment's UP now: this hook runs before anything can
     * expose the endpoint, also on the resume path, before held D0
     * requests are released. The seam's later fresh exposure counts the
     * generation once more; a newer generation with no link-down between
     * is harmless to the reader rule (a DOWN_QUIESCED of a generation at
     * or above the one an instance recorded is still about its link). */
    bridge = XHCI_TBT_NO_BDF;
    if (action->Port >= 1 && action->Port <= XHCI_TBT_MAX_PORTS) {
        bridge = cmNvU4Bridge(nhi, action->Port);
    }
    if (bridge != XHCI_TBT_NO_BDF) {
        (VOID)CmNvlinkExpose(nhi, bridge, TRUE);
    } else {
        /* The port's bridge is not known: every member of the set, as the
         * seam's own exposure of this change will do. */
        for (i = 0; i < nhi->Reenum.Topo.Tunnels; i++) {
            (VOID)CmNvlinkExpose(nhi, nhi->Reenum.Topo.Set[i], TRUE);
        }
    }
}

ULONG CmNvlinkU4Down(PVOID context, const TBT_U4_LINKDOWN *info)
{
    PCM_NHI nhi;
    ULONG member;
    ULONG r;

    nhi = (PCM_NHI)context;
    member = XHCI_TBT_NO_MEMBER;
    if (info->Port >= 1 && info->Port <= XHCI_TBT_MAX_PORTS) {
        member = XhciTbtPortMember(&nhi->Reenum.Topo, &nhi->Binds,
                                   info->Port);
    }
    r = CmNvlinkPortDown(nhi, member, info->PathClosed,
                         info->AnchorValid ? info->RemainingUs / 1000UL
                                           : 0);
    return r == CM_NVLINK_SEND ? TBT_U4_STMT_SENT : TBT_U4_STMT_WITHHELD;
}

/* The tunnel port of USB4 port `port`'s member, or XHCI_TBT_NO_BDF. */
static ULONG cmNvU4Bridge(PCM_NHI nhi, ULONG port)
{
    ULONG member;

    member = XhciTbtPortMember(&nhi->Reenum.Topo, &nhi->Binds, port);
    if (member == XHCI_TBT_NO_MEMBER || member >= nhi->Reenum.Topo.Tunnels) {
        return XHCI_TBT_NO_BDF;
    }
    return nhi->Reenum.Topo.Set[member];
}

/*
 * Record 22 section 10.3's anchor: once per pass, for each port known by
 * correspondence with UP entries of this domain behind it, one sample
 * timestamped on the core's clock before its first read, matched only
 * when every such entry's function answered with the ids its UP was
 * published for.
 */
VOID CmNvlinkService(PCM_NHI nhi)
{
    CM_NV_U4 *u;
    ULONG port;
    ULONG bridge;
    ULONG i;
    ULONG id;
    ULONG issued;
    ULONG entries;
    BOOLEAN matched;

    if (cmNvTable == NULL || !nhi->U4On) {
        return;
    }
    u = cmNvU4Of(nhi, FALSE);
    if (u == NULL) {
        return;
    }
    for (port = 1; port <= TBT_U4_MAX_PORTS; port++) {
        if (!u->Seen[port]) {
            continue;
        }
        bridge = cmNvU4Bridge(nhi, port);
        if (bridge == XHCI_TBT_NO_BDF) {
            continue;
        }
        /* One sample per port, timestamped at its first read and matched
         * only when every UP entry behind the port answered with its own
         * ids: the port's anchor is then no later than any recipient's
         * last sign of presence. */
        entries = 0;
        matched = TRUE;
        issued = TbtClockUs(&nhi->Host);
        for (i = 0; i < NVME98LS_ENTRIES; i++) {
            if (cmNvShadow[i].Owner != nhi || cmNvShadow[i].Bridge != bridge ||
                (cmNvTable->Entry[i].Flags & NVME98LS_UP) == 0) {
                continue;
            }
            entries++;
            if (!cmNvCfg(cmNvTable->Entry[i].BusNumber,
                         cmNvTable->Entry[i].SlotNumber, 0, &id) ||
                id != cmNvShadow[i].Id) {
                matched = FALSE;
            }
        }
        if (entries != 0) {
            TbtU4PresenceSample(&nhi->U4, port, u->Gen[port], issued,
                                matched);
        }
    }
}
