/*
 * cm_nvlinkos.c - the driver half of the stor98 link-state channel
 * (cm_nvlink.h; roadmap-usb4.md tasks 39-A.7 and 40-A.7): the table's one
 * allocation, the NT callback object, the Windows 98 SE and ME registry
 * record, and the seam's calls, over cm_nvlink.c's pure entry protocol.
 *
 * ONE TABLE PER BOOT. CmNvlinkInit allocates it (CmAlloc, so in
 * cm_pool.c's one pool site) and nothing ever frees it. A reload of this
 * driver in the same boot (every NHI and filter gone, then back) finds the
 * callback object the first load made and publishes nothing (stor98cm.h,
 * "CM lifetime (NT)": no second table, and the old one is not written
 * again): a stor98 instance bound before the reload keeps polling the old
 * table, which stays valid and frozen at its last state, so it gets no
 * statement and falls back to its gone deadline (MD-1); one that binds
 * after it finds no callback answering, so no channel. That is what a
 * reload loses.
 *
 * SHARED BY EVERY DOMAIN. Each NHI's thread writes the entries of its own
 * tunnel ports; cmNvLock serialises the writes and the shadow below
 * (which domain marked an entry, on which bridge, when). stor98 reads
 * without the lock, by the entry's sequence (cm_nvlink.c).
 *
 * WINDOWS 98 SE AND ME. No callback object: the rendezvous is interface 3's
 * proof key under HKEY_DYN_DATA and locator under HKEY_LOCAL_MACHINE
 * (cm_nvlink.h), made through the VMM's registry services (_RegOpenKey
 * 148h, _RegCloseKey 149h, _RegCreateKey 14Ah, _RegDeleteKey 14Bh,
 * _RegSetValueEx 152h, _RegCreateDynKey 176h: their places in the Windows
 * 98 DDK's VMM.H service table, counted from Get_VMM_Version, service 0,
 * and checked against VMM.INC; the provider and value structures are
 * VMMREG.H's) reached by the same INT 20h dynalink cm_cfgmg.c uses for
 * CONFIGMG, from the NHI's thread, a passive thread of the system VM. The
 * proof key's query handler runs from this image whenever the key is read,
 * so the image is resident (no DriverUnload on 98 SE and ME, from
 * DriverEntry, R4) before the key is made; were it ever unloaded, the
 * unload would delete the key first. x86 only.
 *
 * IRQL: as cm_nvlink.h; the callback at the caller's IRQL (stor98 calls
 * it at PASSIVE_LEVEL from HwFindAdapter), touching nothing but an
 * interlocked count and the reply. The proof key's query handler at the
 * VMM's registry caller's level, touching only this file's proof values
 * and the caller's buffers.
 */

#include "cm.h"
#include "cm_cfgmg.h"
#include "cm_nvlink.h"

#if defined(_WIN64) && defined(NO_LEGACY_DRIVERS)
NTHALAPI ULONG HalGetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber,
                             ULONG SlotNumber, PVOID Buffer, ULONG Length);
#endif

/* The shadow of each entry, the connection manager's own (CM_NV_SHADOW,
 * cm_nvlink.h): its owner, bridge, pending acknowledgement and ids. */
static PSTOR98LS_TABLE cmNvTable;
static KSPIN_LOCK cmNvLock;
static CM_NV_SHADOW cmNvShadow[STOR98LS_ENTRIES];
/* The tunnel (CM_NVLINK_TUNNEL) each entry's UP was last published for,
 * CmNvlinkPublishUp's record (task 42-A.6); under cmNvLock. */
static ULONG cmNvTunnel[STOR98LS_ENTRIES];
static BOOLEAN cmNvNt;
static BOOLEAN cmNvOsQualified; /* 2000 x86, XP x64 (record 21 7.2)      */
static PCALLBACK_OBJECT cmNvObject;
static PVOID cmNvRegistration;
static ULONG cmNvReloaded;      /* NT: an earlier load made the object    */
static LONG cmNvQueries;
static ULONG cmNvRecord;        /* 9x: the record was written            */
static ULONG cmNvRecordTried;   /* 0 not yet, 1 being written, 2 done    */
static ULONG cmNvRecordError;
static ULONG cmNvRemoveSeq;
static LONG cmNvQueriesLogged;  /* 9x: the proof reads last logged       */
#if defined(_X86_)
static VOID cmNvDeleteProof(VOID);
#endif

/* Each domain's latest connection generation per host USB4 port, from the
 * UP hook: the presence sample is offered to the core under it. */
#define CM_NV_DOMAINS 4UL

typedef struct _CM_NV_U4 {
    PCM_NHI Nhi;
    ULONG Gen[TBT_U4_MAX_PORTS + 1];
    ULONG Seen[TBT_U4_MAX_PORTS + 1];
    /* 98 SE and ME: a member whose fresh exposure found its bus silent,
     * scanned again each pass until CM_NV_LATE_MS after the exposure (the
     * endpoint answered on 98 SE only after Reenumerate_DevNode). */
    ULONG LateBridge[XHCI_TBT_MAX_TUNNELS];
    ULONG LateFrom[XHCI_TBT_MAX_TUNNELS];
    ULONG LateOn[XHCI_TBT_MAX_TUNNELS];
    /* Task 42-A.6: the latest new tunnel (CM_NVLINK_TUNNEL) named for each
     * member's bridge, which every later exposure of that bridge publishes
     * for (cmNvTunnelOf): re-applied, it changes nothing; an exposure that
     * names none - a resync, a baseline, an owed re-enumeration, a late
     * scan, another port's tunnel up - still pays a debt an earlier
     * exposure could not (the bus silent, the member not active). */
    ULONG TunBridge[XHCI_TBT_MAX_TUNNELS];
    ULONG Tun[XHCI_TBT_MAX_TUNNELS];
    /* Task 42-A.12: CmNvlinkStep in progress on this domain's thread (a
     * step reached from inside one does nothing). */
    ULONG Stepping;
} CM_NV_U4;

#define CM_NV_LATE_MS 10000UL

static CM_NV_U4 cmNvU4[CM_NV_DOMAINS];

/* Counters, for the log at each NHI's stop. */
static ULONG cmNvUps;
static ULONG cmNvUpsFull;
static ULONG cmNvSent;
static ULONG cmNvWithheld;
static ULONG cmNvAcked;
static ULONG cmNvAckTimeouts;
static ULONG cmNvAckLate;       /* of those, an answer first seen late    */
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
    if (argument1 == STOR98LS_QUERY && argument2 != NULL &&
        cmNvTable != NULL) {
        *(PSTOR98LS_TABLE *)argument2 = cmNvTable;
        InterlockedIncrement(&cmNvQueries);
    }
}

VOID CmNvlinkInit(VOID)
{
    static WCHAR name[] = STOR98LS_CALLBACK_NAME;
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
    cmNvTable = (PSTOR98LS_TABLE)CmAlloc(sizeof(STOR98LS_TABLE));
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
    /* One table per boot (stor98cm.h, "CM lifetime (NT)"): stor98 never
     * creates the object, so an object already there is an earlier load's
     * of this driver in this boot, and a reload publishes nothing - no
     * second table, and the first one is not written again. */
    status = ExCreateCallback(&cmNvObject, &attributes, FALSE, TRUE);
    if (NT_SUCCESS(status)) {
        ObDereferenceObject(cmNvObject);
        cmNvObject = NULL;
        cmNvReloaded = 1;
        return;
    }
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
#if defined(_X86_)
    /* 9x: the proof goes before the image that answers it (interface 3,
     * R4); the table stays, and receivers holding it get no further
     * statement. */
    cmNvDeleteProof();
#endif
}

#if defined(_X86_)

#define CM_NV_VMM_ID            0x0001  /* VMM.H, VMM_DEVICE_ID          */
#define CM_NV_SVC_OPEN_KEY      0x0148  /* _RegOpenKey                   */
#define CM_NV_SVC_CLOSE_KEY     0x0149  /* _RegCloseKey                  */
#define CM_NV_SVC_CREATE_KEY    0x014A  /* _RegCreateKey                 */
#define CM_NV_SVC_DELETE_KEY    0x014B  /* _RegDeleteKey                 */
#define CM_NV_SVC_SET_VALUE     0x0152  /* _RegSetValueEx                */
#define CM_NV_SVC_CREATE_DYN    0x0176  /* _RegCreateDynKey              */
#define CM_NV_HKEY_LOCAL_MACHINE 0x80000002UL
#define CM_NV_HKEY_DYN_DATA     0x80000006UL /* VMMREG.H                 */
#define CM_NV_REG_BINARY        3UL
#define CM_NV_REG_DWORD         4UL
#define CM_NV_PROVIDER_KEEPS_VALUE_LENGTH 1UL
/* cmNvWriteRecord's own errors, beside the VMM's registry codes. */
#define CM_NV_ERR_NO_THUNK      0xFFFFFFFFUL

/* VMMREG.H's struct pvalue and struct provider_info (the Windows 98 DDK). */
typedef ULONG (__cdecl *CM_NV_QUERY)(PVOID keyContext, CM_NV_VALCTX *vc,
                                     ULONG n, PVOID data, PULONG cb,
                                     ULONG reserved);

typedef struct _CM_NV_PVALUE {
    char *pv_valuename;
    ULONG pv_valuelen;
    PVOID pv_value_context;
    ULONG pv_type;
} CM_NV_PVALUE;

typedef struct _CM_NV_PROVIDER {
    CM_NV_QUERY pi_R0_1val;
    CM_NV_QUERY pi_R0_allvals;
    CM_NV_QUERY pi_R3_1val;
    CM_NV_QUERY pi_R3_allvals;
    ULONG pi_flags;
} CM_NV_PROVIDER;

/* The proof's values, answered from here for as long as the image is
 * loaded; written once, before the key exists, and never changed. */
static char cmNvProofKey[] = STOR98LS9X_PROOF_KEY;
static char cmNvProofCookieName[] = STOR98LS9X_PROOF_VALUE;
static char cmNvProofTableName[] = STOR98LS9X_PROOF_TABLE_VALUE;
static ULONG cmNvProofCookie;
static STOR98LS9X_PROOF_TABLE cmNvProofTable;
static CM_NV_PVALUE cmNvProofValues[2];
static CM_NV_PROVIDER cmNvProvider;
static ULONG cmNvProofMade;     /* the dynamic key was created           */
/* Its handle, open for as long as the key is to be answered: closing it
 * leaves the key's name with no values and no provider (98 SE, run-41),
 * so it is closed only at an unload, before the key is deleted. */
static ULONG cmNvProofHkey;

typedef ULONG (__cdecl *CM_NV_FN1)(ULONG a);
typedef ULONG (__cdecl *CM_NV_FN2)(ULONG a, ULONG b);
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

/* The proof key's query handler, all four slots (R3a). */
static ULONG __cdecl cmNvProofQuery(PVOID keyContext, CM_NV_VALCTX *vc,
                                    ULONG n, PVOID data, PULONG cb,
                                    ULONG reserved)
{
    UNREFERENCED_PARAMETER(keyContext);
    UNREFERENCED_PARAMETER(reserved);
    /* A reader of the proof: stor98 reads it once per load, in
     * HwFindAdapter, before it can answer anything (cmNvReceiver). */
    InterlockedIncrement(&cmNvQueries);
    return CmNvlinkProofQuery(vc, n, (PUCHAR)data, cb, &cmNvProofCookie,
                              &cmNvProofTable);
}

/* CmNvlinkPublish9x's steps over the VMM's registry services. */
typedef struct _CM_NV_PUBCTX {
    PUCHAR Thunks;      /* five, at the CM_NV_T_* offsets               */
    ULONG Cookie;
} CM_NV_PUBCTX;

#define CM_NV_T_CREATE_KEY  0
#define CM_NV_T_SET_VALUE   8
#define CM_NV_T_CLOSE_KEY   16
#define CM_NV_T_OPEN_KEY    24
#define CM_NV_T_CREATE_DYN  32

/* R4's residency, set in DriverEntry on 98 SE and ME (cm_entry.c): no
 * DriverUnload, so the image that answers the proof and writes the table
 * stays loaded for the boot whatever its PnP devices do. */
static BOOLEAN cmNvPubResident(PVOID context)
{
    UNREFERENCED_PARAMETER(context);
    return (BOOLEAN)(CmDriverObject != NULL &&
                     CmDriverObject->DriverUnload == NULL);
}

static ULONG cmNvPubOpenProof(PVOID context)
{
    CM_NV_PUBCTX *c;
    ULONG hkey;
    ULONG error;

    c = (CM_NV_PUBCTX *)context;
    hkey = 0;
    error = ((CM_NV_FN3)(c->Thunks + CM_NV_T_OPEN_KEY))(
        CM_NV_HKEY_DYN_DATA, (ULONG)(ULONG_PTR)cmNvProofKey,
        (ULONG)(ULONG_PTR)&hkey);
    if (error == 0) {
        (VOID)((CM_NV_FN1)(c->Thunks + CM_NV_T_CLOSE_KEY))(hkey);
    }
    return error;
}

static ULONG cmNvPubCreateProof(PVOID context)
{
    CM_NV_PUBCTX *c;
    ULONG hkey;
    ULONG error;

    c = (CM_NV_PUBCTX *)context;
    cmNvProofCookie = c->Cookie;
    cmNvProofTable.TableLinear = (ULONG)(ULONG_PTR)cmNvTable;
    cmNvProofTable.TableSize = sizeof(STOR98LS_TABLE);
    cmNvProofTable.Cookie = c->Cookie;
    cmNvProofValues[0].pv_valuename = cmNvProofCookieName;
    cmNvProofValues[0].pv_valuelen = sizeof(ULONG);
    cmNvProofValues[0].pv_value_context = &cmNvProofCookie;
    cmNvProofValues[0].pv_type = CM_NV_REG_DWORD;
    cmNvProofValues[1].pv_valuename = cmNvProofTableName;
    cmNvProofValues[1].pv_valuelen = CM_NVLINK_PROOF_BYTES;
    cmNvProofValues[1].pv_value_context = &cmNvProofTable;
    cmNvProofValues[1].pv_type = CM_NV_REG_BINARY;
    cmNvProvider.pi_R0_1val = cmNvProofQuery;
    cmNvProvider.pi_R0_allvals = cmNvProofQuery;
    cmNvProvider.pi_R3_1val = cmNvProofQuery;
    cmNvProvider.pi_R3_allvals = cmNvProofQuery;
    cmNvProvider.pi_flags = CM_NV_PROVIDER_KEEPS_VALUE_LENGTH;
    hkey = 0;
    error = ((CM_NV_FN6)(c->Thunks + CM_NV_T_CREATE_DYN))(
        (ULONG)(ULONG_PTR)cmNvProofKey, 0, (ULONG)(ULONG_PTR)&cmNvProvider,
        (ULONG)(ULONG_PTR)cmNvProofValues, 2, (ULONG)(ULONG_PTR)&hkey);
    if (error != 0) {
        return error;
    }
    cmNvProofMade = 1;
    cmNvProofHkey = hkey;
    return 0;
}

static ULONG cmNvPubWriteLocator(PVOID context)
{
    static char key[] = CM_NVLINK_REC_KEY;
    static char value[] = CM_NVLINK_REC_VALUE;
    UCHAR record[CM_NVLINK_RECORD_BYTES];
    CM_NV_PUBCTX *c;
    ULONG hkey;
    ULONG error;

    c = (CM_NV_PUBCTX *)context;
    CmNvlinkRecordEncode(record, (ULONG)(ULONG_PTR)cmNvTable,
                         sizeof(STOR98LS_TABLE), c->Cookie);
    hkey = 0;
    error = ((CM_NV_FN3)(c->Thunks + CM_NV_T_CREATE_KEY))(
        CM_NV_HKEY_LOCAL_MACHINE, (ULONG)(ULONG_PTR)key,
        (ULONG)(ULONG_PTR)&hkey);
    if (error == 0) {
        error = ((CM_NV_FN6)(c->Thunks + CM_NV_T_SET_VALUE))(
            hkey, (ULONG)(ULONG_PTR)value, 0, CM_NV_REG_BINARY,
            (ULONG)(ULONG_PTR)record, CM_NVLINK_RECORD_BYTES);
        (VOID)((CM_NV_FN1)(c->Thunks + CM_NV_T_CLOSE_KEY))(hkey);
    }
    return error;
}

/*
 * Interface 3's publication, once per load (R3, R4): the table's Cookie,
 * then CmNvlinkPublish9x - residency, the proof key opened first (refused
 * when it exists: another publisher's), the proof created, the locator.
 * Returns 0 when published, else CM_NVLINK_PUB_* << 16 | the failed
 * step's code's low 16 bits, or CM_NV_ERR_NO_THUNK.
 */
static ULONG cmNvWriteRecord(ULONG clock)
{
    CM_NV_PUBCTX c;
    CM_NV_PUB9X ops;
    ULONG step;
    ULONG error;

    c.Thunks = (PUCHAR)CmAlloc(5 * 8);
    if (c.Thunks == NULL) {
        return CM_NV_ERR_NO_THUNK;
    }
    cmNvThunk(c.Thunks + CM_NV_T_CREATE_KEY, CM_NV_SVC_CREATE_KEY);
    cmNvThunk(c.Thunks + CM_NV_T_SET_VALUE, CM_NV_SVC_SET_VALUE);
    cmNvThunk(c.Thunks + CM_NV_T_CLOSE_KEY, CM_NV_SVC_CLOSE_KEY);
    cmNvThunk(c.Thunks + CM_NV_T_OPEN_KEY, CM_NV_SVC_OPEN_KEY);
    cmNvThunk(c.Thunks + CM_NV_T_CREATE_DYN, CM_NV_SVC_CREATE_DYN);
    c.Cookie = (clock * 0x9E3779B1UL) ^ (ULONG)(ULONG_PTR)cmNvTable ^
               0x5339384CUL;
    if (c.Cookie == 0) {
        c.Cookie = 1;
    }
    *(volatile ULONG *)&cmNvTable->Cookie = c.Cookie;
    ops.Resident = cmNvPubResident;
    ops.OpenProof = cmNvPubOpenProof;
    ops.CreateProof = cmNvPubCreateProof;
    ops.WriteLocator = cmNvPubWriteLocator;
    ops.Context = &c;
    step = CmNvlinkPublish9x(&ops, &error);
    CmFree(c.Thunks);
    return step == CM_NVLINK_PUB_OK ? 0 : (step << 16) | (error & 0xFFFFUL);
}

/* The proof key deleted (R4), when this load made it. PASSIVE_LEVEL. */
static VOID cmNvDeleteProof(VOID)
{
    PUCHAR t;

    if (cmNvProofMade == 0) {
        return;
    }
    t = (PUCHAR)CmAlloc(16);
    if (t == NULL) {
        return;
    }
    cmNvThunk(t, CM_NV_SVC_CLOSE_KEY);
    cmNvThunk(t + 8, CM_NV_SVC_DELETE_KEY);
    if (cmNvProofHkey != 0) {
        (VOID)((CM_NV_FN1)t)(cmNvProofHkey);
        cmNvProofHkey = 0;
    }
    if (((CM_NV_FN2)(t + 8))(CM_NV_HKEY_DYN_DATA,
                             (ULONG)(ULONG_PTR)cmNvProofKey) == 0) {
        cmNvProofMade = 0;
    }
    CmFree(t);
}

#endif /* _X86_ */

/* 9x: the record, written at the first tunnel action of the boot rather
 * than at the start: no registry service is called while the NHI's own
 * start is still pending in the configuration manager (the CONFIGMG
 * route's rule, cm_reenum.c). Before any UP, so before stor98 can look. */
static BOOLEAN cmNvEnsure(PCM_NHI nhi)
{
    KIRQL irql;
    ULONG mine;
    ULONG start;

    if (cmNvTable == NULL) {
        return FALSE;
    }
    if (cmNvNt) {
        /* NT: only with the callback registered (not on a reload in the
         * same boot, nor where registration failed). */
        return (BOOLEAN)(cmNvRegistration != NULL);
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
    /* Bit 31 published; else bits 18:16 the step that stopped it
     * (CM_NVLINK_PUB_*) and 15:0 its code, or all ones for no thunk. */
    CmLogNote(nhi, "cm.nvlink.9x.record",
              (cmNvRecord << 31) | (cmNvRecordError & 0x7FFFFFFFUL));
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
                      (cmNvReloaded != 0 ? 0x40000000UL : 0) |
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
    /* 9x: published, and the proof read at least once - a stor98 that
     * never read it holds no table and can answer nothing. */
    return (BOOLEAN)(cmNvRecord != 0 && cmNvQueries != 0);
}

/* ====================================================================== */
/* The obligations                                                        */
/* ====================================================================== */

static CM_NV_U4 *cmNvU4Of(PCM_NHI nhi, BOOLEAN create);

/* 98 SE and ME: `bridge`'s bus was silent at a fresh exposure; scan it
 * again each pass for CM_NV_LATE_MS (CmNvlinkService), its UP still owed;
 * `arm` FALSE: a function answered, the scan is no longer needed. The UP
 * so published follows the operating system's enumeration: a stor98
 * instance bound before it finds its entry later, and the retirement
 * before the exposure kept any old statement away from it. Whether an
 * entry found UP gets a new generation is the bridge's tunnel's matter
 * (cmNvTunnelOf), not the late scan's. */
static VOID cmNvLateArm(PCM_NHI nhi, ULONG bridge, BOOLEAN arm)
{
    CM_NV_U4 *u;
    ULONG k;

    u = cmNvU4Of(nhi, TRUE);
    if (u == NULL) {
        return;
    }
    for (k = 0; k < XHCI_TBT_MAX_TUNNELS; k++) {
        if (u->LateOn[k] && u->LateBridge[k] == bridge) {
            break;
        }
    }
    if (!arm) {
        if (k != XHCI_TBT_MAX_TUNNELS) {
            u->LateOn[k] = 0;
        }
        return;
    }
    if (k == XHCI_TBT_MAX_TUNNELS) {
        for (k = 0; k < XHCI_TBT_MAX_TUNNELS && u->LateOn[k]; k++) {
        }
    }
    if (k == XHCI_TBT_MAX_TUNNELS) {
        return;
    }
    u->LateBridge[k] = bridge;
    u->LateFrom[k] = cmNvNow(nhi);
    u->LateOn[k] = 1;
}

/* The tunnel an exposure of `bridge` publishes for (task 42-A.6): `named`,
 * recorded as the bridge's latest, when the exposure names one; otherwise
 * the latest one named for the bridge before (CM_NVLINK_TUNNEL_NONE when
 * none ever was, or the table of tunnels is full). */
static ULONG cmNvTunnelOf(PCM_NHI nhi, ULONG bridge, ULONG named)
{
    CM_NV_U4 *u;

    u = cmNvU4Of(nhi, TRUE);
    if (u == NULL) {
        return named;
    }
    return CmNvlinkBridgeTunnel(u->TunBridge, u->Tun, XHCI_TBT_MAX_TUNNELS,
                                bridge, named);
}

/* Obligation 1's scan of one secondary bus: UP for each NVMe function
 * answering there. Returns (functions answering << 16) | entries UP. */
static ULONG cmNvScan(PCM_NHI nhi, ULONG bridge, ULONG bus, ULONG tunnel)
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
                    index = CmNvlinkPublishUp(cmNvTable, cmNvTunnel, bus,
                                              slot, tunnel,
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

ULONG CmNvlinkExpose(PCM_NHI nhi, ULONG bridge, BOOLEAN fresh, ULONG tunnel)
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
    /* The tunnel named, recorded first, even where nothing is published
     * now (the bus unread, the channel not ready, the gate up): a later
     * exposure of the bridge publishes for it. */
    tunnel = cmNvTunnelOf(nhi, bridge, tunnel);
    /* A statement this domain made behind this member is acknowledged
     * (or timed out) before any exposure can retire it or start a
     * removal: no UP while its gate is up (R8b's order; task 42-A.12).
     * The seam holds the member's call behind the same gate and exposes
     * it again once the gate falls; nothing waits here. */
    if (CmNvlinkHeld(nhi, bridge)) {
        CmLogNote(nhi, "cm.nvlink.up.held", bridge);
        return 0;
    }
    ready = cmNvEnsure(nhi);
    bus = cmNvSecondary(bridge);
    /* Every exposure retires the statements behind the bridge first, by
     * its bus and by the entries last published or marked for it. */
    retired = 0;
    KeAcquireSpinLock(&cmNvLock, &irql);
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        if (cmNvShadow[i].Owner != NULL && cmNvShadow[i].Waiting) {
            /* A statement still awaited - another domain's, or one of
             * this domain's behind another member on the same bus: an
             * exposure never ends a wait (record 21 section 7.2 part 3). */
            continue;
        }
        if ((cmNvShadow[i].Bridge == bridge && cmNvShadow[i].Owner != NULL) ||
            (bus != CM_NVLINK_NONE &&
             (cmNvTable->Entry[i].Flags & STOR98LS_IN_USE) != 0 &&
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
    r = cmNvScan(nhi, bridge, bus, tunnel);
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
            r = cmNvScan(nhi, bridge, bus, tunnel);
        }
        CmLogNote(nhi, "cm.nvlink.up.waited",
                  ((cmNvNow(nhi) - start) << 8) | (r >> 16));
    }
    if (fresh && !cmNvNt) {
        /* Silent: scanned again on later passes until it answers.
         * Answered: no later scan; a generation still owed to the bridge's
         * tunnel is paid by its next exposure (cmNvTunnelOf). */
        cmNvLateArm(nhi, bridge, (BOOLEAN)((r >> 16) == 0));
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
static ULONG cmNvMarkEx(PCM_NHI nhi, ULONG bridge, ULONG verdict,
                        BOOLEAN confirmed);

static ULONG cmNvMark(PCM_NHI nhi, ULONG bridge, ULONG verdict)
{
    return cmNvMarkEx(nhi, bridge, verdict, FALSE);
}

/* cmNvMark; with `confirmed`, a withheld entry leaves UP only when its
 * function's identifier was read and read all-ones (a failed read proves
 * nothing). */
static ULONG cmNvMarkEx(PCM_NHI nhi, ULONG bridge, ULONG verdict,
                        BOOLEAN confirmed)
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
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        if ((cmNvTable->Entry[i].Flags & STOR98LS_UP) == 0 ||
            (cmNvShadow[i].Bridge != bridge &&
             cmNvTable->Entry[i].BusNumber != bus) ||
            (cmNvShadow[i].Owner != NULL && cmNvShadow[i].Owner != nhi)) {
            continue;
        }
        seen++;
        absent = TRUE;
        if (verdict != CM_NVLINK_SEND) {
            if (!cmNvCfg(cmNvTable->Entry[i].BusNumber,
                         cmNvTable->Entry[i].SlotNumber, 0, &id)) {
                absent = (BOOLEAN)!confirmed;
            } else if (id != 0xFFFFFFFFUL && (id & 0xFFFFUL) != 0xFFFFUL) {
                absent = FALSE;
            }
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

VOID CmNvlinkDropLate(PCM_NHI nhi)
{
    CM_NV_U4 *u;
    ULONG i;
    ULONG dropped;

    if (cmNvTable == NULL) {
        return;
    }
    u = cmNvU4Of(nhi, FALSE);
    if (u == NULL) {
        return;
    }
    dropped = 0;
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (u->LateOn[i]) {
            u->LateOn[i] = 0;
            dropped++;
        }
    }
    if (dropped != 0) {
        CmLogNote(nhi, "cm.nvlink.late.dropped", dropped);
    }
}

VOID CmNvlinkLost(PCM_NHI nhi)
{
    PCM_REENUM r;
    ULONG i;

    r = &nhi->Reenum;
    if (cmNvTable == NULL) {
        return;
    }
    CmLogNote(nhi, "cm.nvlink.lost", r->Topo.Tunnels);
    for (i = 0; i < r->Topo.Tunnels; i++) {
        (VOID)cmNvMark(nhi, r->Topo.Set[i], CM_NVLINK_LOST);
    }
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

VOID CmNvlinkStep(PCM_NHI nhi)
{
    CM_NV_U4 *u;
    ULONG i;
    ULONG k;
    ULONG n;
    ULONG now;
    ULONG step;
    ULONG ends[STOR98LS_ENTRIES];
    ULONG took[STOR98LS_ENTRIES];
    KIRQL irql;

    if (cmNvTable == NULL) {
        return;
    }
    /* Not a slot of its own: a step after the NHI's stop (a sleep of
     * its teardown) must not claim one back. */
    u = cmNvU4Of(nhi, FALSE);
    if (u != NULL) {
        if (u->Stepping) {
            return;
        }
        u->Stepping = 1;
    }
    n = 0;
    now = cmNvNow(nhi);
    KeAcquireSpinLock(&cmNvLock, &irql);
    (VOID)CmNvlinkWaitStep(cmNvTable, cmNvShadow, nhi, cmNvReceiver(), now,
                           CmNvlinkAckBoundMs(cmNvNt), ends, &n);
    for (k = 0; k < n; k++) {
        i = ends[k] & 0xFFUL;
        step = ends[k] >> 8;
        took[k] = now - cmNvShadow[i].MarkedAt;
        if (step == CM_NVLINK_AW_ACKED) {
            cmNvAcked++;
        } else if (step == CM_NVLINK_AW_NO_RECEIVER) {
            cmNvNoReceiver++;
        } else if (step == CM_NVLINK_AW_TIMEOUT) {
            cmNvAckTimeouts++;
        } else if (step == CM_NVLINK_AW_LATE) {
            cmNvAckTimeouts++;
            cmNvAckLate++;
        }
    }
    KeReleaseSpinLock(&cmNvLock, irql);
    /* How each entry's wait ended (CM_NVLINK_AW_* << 8 | entry) and how
     * long it ran from its mark, ms. */
    for (k = 0; k < n; k++) {
        CmLogNote(nhi, "cm.nvlink.await.end", ends[k]);
        CmLogNote(nhi, "cm.nvlink.await", took[k]);
    }
    if (u != NULL) {
        u->Stepping = 0;
    }
}

BOOLEAN CmNvlinkHeld(PCM_NHI nhi, ULONG bridge)
{
    BOOLEAN held;
    KIRQL irql;

    if (cmNvTable == NULL) {
        return FALSE;
    }
    CmNvlinkStep(nhi);
    KeAcquireSpinLock(&cmNvLock, &irql);
    held = CmNvlinkWaitHeld(cmNvShadow, nhi, bridge);
    KeReleaseSpinLock(&cmNvLock, irql);
    return held;
}

BOOLEAN CmNvlinkPending(VOID)
{
    BOOLEAN pending;
    KIRQL irql;

    if (cmNvTable == NULL) {
        return FALSE;
    }
    KeAcquireSpinLock(&cmNvLock, &irql);
    pending = CmNvlinkWaitAny(cmNvShadow);
    KeReleaseSpinLock(&cmNvLock, irql);
    return pending;
}

/*
 * The 9x orderly-removal handshake (MD-5), as the CONFIGMG worker's
 * removal step uses it (cm_cfgwk.c, cmWkRemoveChild): no CONFIGMG call
 * and no ring service here - the worker makes the CONFIGMG calls and does
 * the waiting. Only for a function still present behind its bridge whose
 * entry reads UP and was published for the id the devnode names.
 */
BOOLEAN CmNvlinkWkCandidate(PCM_NHI owner, ULONG bridge, ULONG id,
                            ULONG index, PULONG bus, PULONG slot, PULONG gen)
{
    BOOLEAN ok;
    KIRQL irql;

    if (cmNvTable == NULL || cmNvNt || !cmNvReceiver() || owner == NULL ||
        index >= STOR98LS_ENTRIES) {
        return FALSE;
    }
    KeAcquireSpinLock(&cmNvLock, &irql);
    ok = (BOOLEAN)(cmNvShadow[index].Owner == owner &&
                   cmNvShadow[index].Bridge == bridge &&
                   cmNvShadow[index].Id == id &&
                   (cmNvTable->Entry[index].Flags &
                    (STOR98LS_UP | CM_NVLINK_REMOVE_PENDING)) == STOR98LS_UP);
    if (ok) {
        *bus = cmNvTable->Entry[index].BusNumber;
        *slot = cmNvTable->Entry[index].SlotNumber;
        *gen = cmNvTable->Entry[index].LinkGeneration;
    }
    KeReleaseSpinLock(&cmNvLock, irql);
    return ok;
}

ULONG CmNvlinkWkRequest(PCM_NHI owner, ULONG index, ULONG gen, ULONG id,
                        PULONG sequence)
{
    ULONG read;
    ULONG result;
    KIRQL irql;

    *sequence = 0;
    if (cmNvTable == NULL || cmNvNt || !cmNvReceiver() || owner == NULL ||
        index >= STOR98LS_ENTRIES) {
        return CM_NVLINK_NONE;
    }
    /* The function still answers with that id (outside the lock). */
    if (!cmNvCfg(cmNvTable->Entry[index].BusNumber,
                 cmNvTable->Entry[index].SlotNumber, 0, &read) ||
        read != id) {
        return CM_NVLINK_NONE;
    }
    /* The identity the worker established before its query, unchanged:
     * same owner, still UP at the same generation, no other request. */
    result = CM_NVLINK_NONE;
    KeAcquireSpinLock(&cmNvLock, &irql);
    if (cmNvShadow[index].Owner == owner && cmNvShadow[index].Id == id &&
        (cmNvTable->Entry[index].Flags &
         (STOR98LS_UP | CM_NVLINK_REMOVE_PENDING)) == STOR98LS_UP &&
        cmNvTable->Entry[index].LinkGeneration == gen) {
        cmNvRemoveSeq = (cmNvRemoveSeq + 1) & 0x00FFFFFFUL;
        if (cmNvRemoveSeq == 0) {
            cmNvRemoveSeq = 1;
        }
        if (CmNvlinkRemoveRequest(cmNvTable, index, cmNvRemoveSeq)) {
            *sequence = cmNvRemoveSeq;
            result = index;
        }
    }
    KeReleaseSpinLock(&cmNvLock, irql);
    return result;
}

ULONG CmNvlinkWkAnswer(ULONG index, ULONG sequence, ULONG gen,
                       ULONG elapsedMs)
{
    return CmNvlinkRemoveStep(cmNvTable, index, sequence, gen, elapsedMs);
}

VOID CmNvlinkWkWithdraw(ULONG index, ULONG sequence)
{
    KIRQL irql;

    if (cmNvTable == NULL || index >= STOR98LS_ENTRIES || sequence == 0) {
        return;
    }
    /* Only this request: another worker's later one is left alone. */
    KeAcquireSpinLock(&cmNvLock, &irql);
    if ((cmNvTable->Entry[index].Flags & CM_NVLINK_REMOVE_PENDING) != 0 &&
        cmNvTable->Entry[index].RemoveSequence == sequence) {
        CmNvlinkRemoveCancel(cmNvTable, index);
    }
    KeReleaseSpinLock(&cmNvLock, irql);
}

VOID CmNvlinkStop(PCM_NHI nhi)
{
    ULONG i;
    KIRQL irql;

    if (cmNvTable == NULL) {
        return;
    }
    KeAcquireSpinLock(&cmNvLock, &irql);
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        if (cmNvShadow[i].Owner == nhi) {
            cmNvShadow[i].Owner = NULL;
            cmNvShadow[i].Waiting = 0;
            /* A later start numbers its connections afresh: none of its
             * tunnels may be read as one recorded here. */
            cmNvTunnel[i] = CM_NVLINK_TUNNEL_NONE;
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
    CmLogNote(nhi, "cm.nvlink.ack-late", cmNvAckLate);
    CmLogNote(nhi, "cm.nvlink.no-receiver", cmNvNoReceiver);
    CmLogNote(nhi, "cm.nvlink.queries", (ULONG)cmNvQueries);
}

ULONG CmNvlinkIdleMs(ULONG configured)
{
    ULONG i;
    BOOLEAN up;

    if (configured <= CM_NVLINK_PEND_MS || cmNvTable == NULL) {
        return configured;
    }
    up = FALSE;
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        if ((cmNvTable->Entry[i].Flags & STOR98LS_UP) != 0) {
            up = TRUE;
        }
    }
    /* Read without the lock (any IRQL): a wait marked just after is
     * stepped on the next pass all the same. */
    return CmNvlinkIdleFor(configured, up, CmNvlinkWaitAny(cmNvShadow));
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
            for (k = 0; k < XHCI_TBT_MAX_TUNNELS; k++) {
                cmNvU4[i].LateOn[k] = 0;
                cmNvU4[i].Tun[k] = CM_NVLINK_TUNNEL_NONE;
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
     * requests are released. It names the tunnel (port, connection
     * generation), so the seam's later exposure of the same tunnel finds
     * the entry UP for it and leaves its generation as it is (stor98cm.h
     * R8b: only a new tunnel increments it; task 42-A.6). */
    bridge = XHCI_TBT_NO_BDF;
    if (action->Port >= 1 && action->Port <= XHCI_TBT_MAX_PORTS) {
        bridge = cmNvU4Bridge(nhi, action->Port);
    }
    if (bridge != XHCI_TBT_NO_BDF) {
        (VOID)CmNvlinkExpose(nhi, bridge, TRUE,
                             CM_NVLINK_TUNNEL(action->Port,
                                              action->Generation));
    } else {
        /* The port's bridge is not known: every member of the set, as the
         * seam's own exposure of this change will do, naming no tunnel -
         * a function not UP gets its generation, and no member's entry
         * already UP is advanced for a tunnel that may not be its own. */
        for (i = 0; i < nhi->Reenum.Topo.Tunnels; i++) {
            (VOID)CmNvlinkExpose(nhi, nhi->Reenum.Topo.Set[i], TRUE,
                                 CM_NVLINK_TUNNEL_NONE);
        }
    }
}

/* Whether a USB4 port other than `port` with a live PCIe tunnel
 * corresponds to member `k` (named or learned): that member's tunnel is
 * that port's. */
static BOOLEAN cmNvOtherPortLive(PCM_NHI nhi, ULONG port, ULONG k)
{
    const TBT_U4_PORT *p;
    ULONG q;

    for (q = 1; q <= nhi->U4.PortCount && q <= XHCI_TBT_MAX_PORTS; q++) {
        if (q == port) {
            continue;
        }
        p = TbtU4Port(&nhi->U4, q);
        if (p != NULL && p->Pcie.Up &&
            XhciTbtPortMember(&nhi->Reenum.Topo, &nhi->Binds, q) == k) {
            return TRUE;
        }
    }
    return FALSE;
}

ULONG CmNvlinkU4Down(PVOID context, const TBT_U4_LINKDOWN *info)
{
    PCM_NHI nhi;
    ULONG member;
    ULONG r;
    ULONG k;

    nhi = (PCM_NHI)context;
    member = XHCI_TBT_NO_MEMBER;
    if (info->Port >= 1 && info->Port <= XHCI_TBT_MAX_PORTS) {
        member = XhciTbtPortMember(&nhi->Reenum.Topo, &nhi->Binds,
                                   info->Port);
    }
    if (cmNvTable != NULL &&
        (member == XHCI_TBT_NO_MEMBER || member >= nhi->Reenum.Topo.Tunnels)) {
        /* The port's member is not known, so nothing is stated; but an
         * entry whose function is read absent leaves UP (IN_USE alone),
         * so the tunnel that follows on that bus publishes a new
         * generation even where no tunnel can be named for it (task
         * 42-A.6). Not on a member another port with a live PCIe tunnel
         * corresponds to (its tunnel is that port's, unchanged), and only
         * on a read that shows the function gone: a failed read, or a
         * function still answering, keeps its UP. */
        for (k = 0; k < nhi->Reenum.Topo.Tunnels; k++) {
            if (!cmNvOtherPortLive(nhi, info->Port, k)) {
                (VOID)cmNvMarkEx(nhi, nhi->Reenum.Topo.Set[k],
                                 CM_NVLINK_SEVERAL, TRUE);
            }
        }
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
    ULONG bus;
    BOOLEAN matched;

    if (cmNvTable == NULL) {
        return;
    }
    if (!cmNvNt && cmNvQueries != cmNvQueriesLogged) {
        /* Each new read of the proof (stor98's HwFindAdapter reads it once
         * per load): the evidence a receiver holds the table. */
        cmNvQueriesLogged = cmNvQueries;
        CmLogNote(nhi, "cm.nvlink.9x.proof-read", (ULONG)cmNvQueriesLogged);
    }
    u = cmNvU4Of(nhi, FALSE);
    if (u == NULL) {
        return;
    }
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS && !cmNvNt; i++) {
        if (!u->LateOn[i]) {
            continue;
        }
        bus = cmNvSecondary(u->LateBridge[i]);
        if (bus != CM_NVLINK_NONE && cmNvExclusive(nhi, u->LateBridge[i]) &&
            (cmNvScan(nhi, u->LateBridge[i], bus,
                      cmNvTunnelOf(nhi, u->LateBridge[i],
                                   CM_NVLINK_TUNNEL_NONE)) >> 16) != 0) {
            u->LateOn[i] = 0;
            CmLogNote(nhi, "cm.nvlink.up.late",
                      ((cmNvNow(nhi) - u->LateFrom[i]) << 8) | i);
        } else if (cmNvNow(nhi) - u->LateFrom[i] >= CM_NV_LATE_MS) {
            u->LateOn[i] = 0;
            CmLogNote(nhi, "cm.nvlink.up.late-none", u->LateBridge[i]);
        }
    }
    if (!nhi->U4On) {
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
        for (i = 0; i < STOR98LS_ENTRIES; i++) {
            if (cmNvShadow[i].Owner != nhi || cmNvShadow[i].Bridge != bridge ||
                (cmNvTable->Entry[i].Flags & STOR98LS_UP) == 0) {
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
