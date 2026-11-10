/*
 * cm_statos.c - the status record's publication (design record 24 section
 * 3.2): cm_status.c builds it from the connection manager's own state and
 * this file writes it to the registry, only when it changed.
 *
 * WHERE. The value XhciCmStatus (REG_BINARY) of the NHI's hardware key -
 * Device Parameters on NT, HKLM\Enum\<instance> on 98 SE and ME - through
 * the IoOpenDeviceRegistryKey and ZwSetValueKey pair xhci98.sys's door and
 * xhciuas.sys already use on every target. No volatile key: ZwCreateKey
 * would be the first import of this driver that xhci98.sys does not take,
 * with only ntkern.vxd's name table as Windows 98 SE evidence and none for
 * ME, and a missing import stops the whole driver from loading. The value
 * persists across a restart, so the start writes a first record at once.
 *
 * THE KEYS ARE OPENED IN THE START (CmStatusOpen, from cmNhiStart beside
 * CmReadValues): on 98 SE and ME IoOpenDeviceRegistryKey is a
 * configuration-manager service, which from the connection-manager thread
 * could wait for as long as the configuration manager is busy (a wizard
 * left open) and stall the ring. The thread only reads and writes values
 * on the two handles, which are registry services alone.
 *
 * THE ASK. The install tool writes a nonzero XhciCmStatusAsk (REG_DWORD)
 * to the NHI's driver key; the thread reads it at most every
 * CM_ST_ASK_MS and echoes it in the record, which is how the tool knows a
 * record is this session's and current (record 24 section 3.3).
 *
 * THE CONFIRMATION (record 21 section 7.4, revision 3; task 42-A.5). On
 * the NT targets the same read takes XhciCmEjectConfirm (cm_query.h
 * CM_QC_*) from the driver key, which the mark service writes for a unit
 * it read held for eject and an administrator's tool writes for its own
 * eject: handed to the re-enumeration's check (CmReenumConfirm), then
 * written empty, so each one is acted on once. The driver key is opened
 * with KEY_SET_VALUE beside KEY_READ on NT for that alone.
 *
 * THE 9x EJECT (record 21 section 7.2 part 1, task 43.18). On 98 SE and ME
 * the same read takes XhciCmEjectAsk (cm_ejask.h) from the driver key and
 * hands it to cm_ejectos.c whether or not it is there; the reply and the
 * result go to the hardware key beside the record (CmStatusEjectWrite).
 * The driver never writes the tool's ask.
 *
 * NO REGISTRY ACCESS AWAY FROM S0 ON 98 SE AND ME (task 46.14). Once a
 * system power IRP for a sleep or the shutdown has come back up the
 * stack (CmPowerSleeping), the thread reads and writes no value until a
 * system IRP to S0 does the same: no ask, no record, no diagnostic, no
 * eject reply. A 9x shutdown passes its system IRP on without waiting for
 * the thread (cm_power.c, task 44.47), so whatever the thread wrote after
 * it would race the rest of the shutdown, the system's last save of the
 * registry among it, in an order nothing here can see. A record not
 * written then is kept dirty and written at the first pass back in S0;
 * one not written before the power goes is lost, as the next start
 * replaces it anyway. The deferrals are counted. The NT targets are
 * unchanged.
 *
 * THE BOOT GUARD'S MARKER (record 32 section 12.8.15.13, NT only):
 * XhciCmOscPending (REG_DWORD, the send's kind) in the same hardware key,
 * written and flushed before a handover's send, deleted and flushed when
 * the start succeeded, read at the boot's first start (cm_oscnt.c). The
 * key is opened for reading too on NT for it. ZwFlushKey and
 * ZwDeleteValueKey are imported by the amd64 image alone; the x86 image,
 * which loads on 98 SE and ME too, imports neither and calls them through
 * the pointers cm_oscnt.c finds by the helper's channel
 * (CmStatusMarkerRoutines, record 32 section 12.8.15.15): until then, or
 * without them, its marker is never flushed nor deleted, and cm_oscnt.c
 * sends nothing.
 *
 * A failed write is counted and tried again at the next pass; it never
 * touches the connection manager. The import allowlists hold
 * ZwSetValueKey, RtlInitUnicodeString and KeQuerySystemTime to this file
 * and add it to the SITES of IoOpenDeviceRegistryKey,
 * RtlQueryRegistryValues and ZwClose.
 *
 * IRQL: PASSIVE_LEVEL. CmStatusOpen in the PnP start's dispatch, before the
 * thread's start work; CmStatusClose in the removal's dispatch, after the
 * stop (a start that failed before the thread ran leaves its handles to
 * it, or to the next CmStatusOpen); CmStatusQueryName in the PnP path
 * while the key is open (cm_qryos.c); the rest on the NHI's
 * connection-manager thread only, no lock held.
 */

#include "cm.h"

#define CM_ST_ASK_MS        500UL   /* the ask read at most this often     */
#define CM_ST_PATH_RETRY_MS 5000UL  /* an unknown 98 path tried again      */

static VOID cmStBrDiag(PCM_NHI nhi);
static VOID cmStQueryNameWrite(PCM_NHI nhi);
static VOID cmStNiDiag(PCM_NHI nhi);
static VOID cmStBqDiag(PCM_NHI nhi);

static BOOLEAN cmStNt(VOID)
{
    return IoIsWdmVersionAvailable(1, 0x10);
}

/* A write refused admission (CmRegEnter, the header's "away from S0"),
 * counted; the first one of the start logged with the system state. */
static VOID cmStDefer(PCM_NHI nhi)
{
    if (nhi->StatusPub.Deferred++ == 0) {
        CmLogNote(nhi, "cm.status.deferred.first", (ULONG)nhi->SystemPower);
    }
}

/* The system's uptime in seconds, from the tick count's low word and the
 * tick period, with no 64-bit arithmetic; it wraps with the tick count. */
static ULONG cmStUptimeS(PCM_NHI nhi)
{
    LARGE_INTEGER ticks;
    ULONG tickUs;
    ULONG q;
    ULONG r;
    ULONG a;
    ULONG c;

    tickUs = nhi->ClockTickUs;
    if (tickUs == 0) {
        tickUs = KeQueryTimeIncrement() / 10UL;
    }
    KeQueryTickCount(&ticks);
    q = ticks.LowPart / 1000000UL;
    r = ticks.LowPart % 1000000UL;
    /* ticks x tickUs / 10^6, in words, each product below 2^32: a in ms,
     * c in us, their remainders added before the last division. */
    a = r * (tickUs / 1000UL);
    c = r * (tickUs % 1000UL);
    return q * tickUs + a / 1000UL + c / 1000000UL +
           ((a % 1000UL) * 1000UL + c % 1000000UL) / 1000000UL;
}

/* A REG_DWORD, and nothing else, is taken (as cm_reg.c's). */
static NTSTATUS NTAPI cmStTakeAsk(PWSTR ValueName, ULONG ValueType,
                                  PVOID ValueData, ULONG ValueLength,
                                  PVOID Context, PVOID EntryContext)
{
    PUCHAR from;

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);
    if (ValueType != REG_DWORD || ValueLength != sizeof(ULONG)) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    from = (PUCHAR)ValueData;
    *(PULONG)EntryContext = (ULONG)from[0] | ((ULONG)from[1] << 8) |
                            ((ULONG)from[2] << 16) | ((ULONG)from[3] << 24);
    return STATUS_SUCCESS;
}

/* A REG_BINARY of CM_QC_BYTES, and nothing else, is taken. */
static NTSTATUS NTAPI cmStTakeConfirm(PWSTR ValueName, ULONG ValueType,
                                      PVOID ValueData, ULONG ValueLength,
                                      PVOID Context, PVOID EntryContext)
{
    PUCHAR from;
    PUCHAR to;
    ULONG i;

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);
    if (ValueType != REG_BINARY || ValueLength != CM_QC_BYTES) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    from = (PUCHAR)ValueData;
    to = (PUCHAR)EntryContext;
    for (i = 0; i < CM_QC_BYTES; i++) {
        to[i] = from[i];
    }
    return STATUS_SUCCESS;
}

/* NT: XhciCmEjectConfirm, checked once and written empty. */
static VOID cmStReadConfirm(PCM_NHI nhi)
{
    CM_STATUS_PUB *s;
    RTL_QUERY_REGISTRY_TABLE table[2];
    UNICODE_STRING name;
    UCHAR v[CM_QC_BYTES];
    UCHAR none;

    s = &nhi->StatusPub;
    CmZero(table, sizeof(table));
    CmZero(v, sizeof(v));
    table[0].QueryRoutine = cmStTakeConfirm;
    table[0].Flags = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND;
    table[0].Name = L"XhciCmEjectConfirm";
    table[0].EntryContext = v;
    if (!NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_HANDLE,
                                           (PWSTR)s->DrvKey, table, NULL,
                                           NULL))) {
        return;
    }
    CmReenumConfirm(nhi, v, CM_QC_BYTES);
    none = 0;
    RtlInitUnicodeString(&name, L"XhciCmEjectConfirm");
    (VOID)ZwSetValueKey((HANDLE)s->DrvKey, &name, 0, REG_BINARY,
                        (PVOID)&none, 0);
}

/* 98 SE and ME: XhciCmEjectAsk (cm_ejask.h), a REG_BINARY of
 * CM_EJ_ASK_BYTES; anything else is read as no ask. */
static NTSTATUS NTAPI cmStTakeEject(PWSTR ValueName, ULONG ValueType,
                                    PVOID ValueData, ULONG ValueLength,
                                    PVOID Context, PVOID EntryContext)
{
    PUCHAR from;
    PUCHAR to;
    ULONG i;

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);
    if (ValueType != REG_BINARY || ValueLength != CM_EJ_ASK_BYTES) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    from = (PUCHAR)ValueData;
    to = (PUCHAR)EntryContext;
    for (i = 0; i < CM_EJ_ASK_BYTES; i++) {
        to[i] = from[i];
    }
    return STATUS_SUCCESS;
}

/* The eject ask, read with the status ask and handed over whether or not
 * it is there (its absence after a terminal step is the tool's
 * consumption). The driver never writes the tool's value. */
static VOID cmStReadEject(PCM_NHI nhi)
{
    RTL_QUERY_REGISTRY_TABLE table[2];
    UCHAR v[CM_EJ_ASK_BYTES];
    BOOLEAN present;

    CmZero(table, sizeof(table));
    CmZero(v, sizeof(v));
    table[0].QueryRoutine = cmStTakeEject;
    table[0].Flags = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND;
    table[0].Name = L"XhciCmEjectAsk";
    table[0].EntryContext = v;
    if (!CmRegEnter(nhi)) {
        /* Nothing is known of the ask: nothing is handed over. */
        return;
    }
    present = (BOOLEAN)NT_SUCCESS(RtlQueryRegistryValues(
        RTL_REGISTRY_HANDLE, (PWSTR)nhi->StatusPub.DrvKey, table, NULL,
        NULL));
    CmRegLeave(nhi);
    CmEjectAsk(nhi, present, v, CM_EJ_ASK_BYTES);
}

BOOLEAN CmStatusEjectWrite(PCM_NHI nhi, BOOLEAN result, const UCHAR *bytes,
                           ULONG length)
{
    UNICODE_STRING name;
    BOOLEAN done;

    if (nhi->StatusPub.DevKey == NULL) {
        return FALSE;
    }
    if (!CmRegEnter(nhi)) {
        cmStDefer(nhi);
        return FALSE;
    }
    RtlInitUnicodeString(&name, result ? L"XhciCmEjectResult"
                                       : L"XhciCmEjectReply");
    done = (BOOLEAN)NT_SUCCESS(ZwSetValueKey((HANDLE)nhi->StatusPub.DevKey,
                                             &name, 0, REG_BINARY,
                                             (PVOID)bytes, length));
    CmRegLeave(nhi);
    return done;
}

static VOID cmStReadAsk(PCM_NHI nhi)
{
    CM_STATUS_PUB *s;
    RTL_QUERY_REGISTRY_TABLE table[2];
    ULONG ask;

    s = &nhi->StatusPub;
    if (s->DrvKey == NULL || nhi->ClockMs - s->AskAtMs < CM_ST_ASK_MS) {
        return;
    }
    if (!CmRegEnter(nhi)) {
        return;
    }
    s->AskAtMs = nhi->ClockMs;
    ask = 0;
    CmZero(table, sizeof(table));
    table[0].QueryRoutine = cmStTakeAsk;
    table[0].Flags = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND;
    table[0].Name = L"XhciCmStatusAsk";
    table[0].EntryContext = &ask;
    if (NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_HANDLE,
                                          (PWSTR)s->DrvKey, table, NULL,
                                          NULL))) {
        s->Echo = ask;
    }
    CmRegLeave(nhi);
    if (cmStNt()) {
        cmStReadConfirm(nhi);
    } else {
        cmStReadEject(nhi);
    }
}

/* 98 SE and ME: member i's instance path, computed once per location and
 * again every CM_ST_PATH_RETRY_MS while it is unknown, through the
 * re-enumeration's configuration reader. */
static VOID cmStPath(PCM_NHI nhi, ULONG i, PCM_STATUS_IN in)
{
    CM_STATUS_PUB *s;
    ULONG bdf;
    ULONG k;
    BOOLEAN again;

    s = &nhi->StatusPub;
    bdf = nhi->Reenum.Port[i].Bdf;
    again = (BOOLEAN)(s->PathBdf[i] != bdf + 1UL ||
                      (s->PathCount[i] == 0 &&
                       nhi->ClockMs - s->PathAtMs[i] >= CM_ST_PATH_RETRY_MS));
    if (again && !CmNhiHwAccess(nhi)) {
        /* The host interface lost, contained or revoked (record 21
         * section 5.5, record 22 section 7.7): no configuration reading
         * of the domain's buses; a path cached for this location stands,
         * any other is unknown. */
        if (s->PathBdf[i] != bdf + 1UL) {
            s->PathCount[i] = 0;
        }
        again = FALSE;
    }
    if (again) {
        s->PathBdf[i] = bdf + 1UL;
        s->PathAtMs[i] = nhi->ClockMs;
        s->PathCount[i] = 0;
        if (nhi->Reenum.Cfg.Read == NULL ||
            !CmStatusPciPath(&nhi->Reenum.Cfg, bdf, s->Path[i],
                             &s->PathCount[i], CM_ST_PATH_MAX)) {
            s->PathCount[i] = 0;
        }
    }
    in->PathCount[i] = s->PathCount[i];
    for (k = 0; k < s->PathCount[i]; k++) {
        in->Path[i][k] = s->Path[i][k];
    }
}

static VOID cmStInput(PCM_NHI nhi, PCM_STATUS_IN in)
{
    ULONG i;
    ULONG n;
    const TBT_U4_PORT *p;

    CmZero(in, sizeof(*in));
    in->NhiId = (nhi->Vendor & 0xFFFFUL) | ((nhi->Device & 0xFFFFUL) << 16);
    in->Profile = nhi->Profile;
    in->Stage = nhi->Stage;
    in->Inert = nhi->Inert;
    in->Echo = nhi->StatusPub.Echo;
    /* Whatever the stage: an inert start reports its tier too (record 32
     * section 12.8.11). */
    in->Handover = CmOscReport(nhi);
    if (nhi->Stage == CM_STAGE_ACTIVE || nhi->Stage == CM_STAGE_FAILED) {
        if (nhi->U4On) {
            in->Cm = CM_ST_CM_USB4;
        } else if (nhi->Icm.Ring != NULL) {
            in->Cm = CM_ST_CM_ICM;
            /* The running row's generation (the AddDevice profile is
             * not read on 98 SE and ME); Maple Ridge runs the Titan Ridge
             * row but is a USB4 host, told apart by its hardware row's
             * ownership predicate (record 21 section 5.3). */
            in->IcmNoUsb3 = (ULONG)(nhi->Icm.Profile != NULL &&
                                    !(nhi->Hw != NULL &&
                                      nhi->Hw->Ownership ==
                                          TBT_HW_OWN_AUTH_DONE) &&
                                    (nhi->Icm.Profile->Generation ==
                                         TBT_ICM_GEN_FALCON_RIDGE ||
                                     nhi->Icm.Profile->Generation ==
                                         TBT_ICM_GEN_ALPINE_RIDGE ||
                                     nhi->Icm.Profile->Generation ==
                                         TBT_ICM_GEN_TITAN_RIDGE));
        }
    }
    if (!nhi->ReenumOn) {
        return;
    }
    n = nhi->Reenum.Members;
    if (n > CM_ST_MAX_PORTS) {
        n = CM_ST_MAX_PORTS;
    }
    in->PortCount = n;
    in->Blocked = CmEjectBlockedMask(nhi);
    for (i = 0; i < n; i++) {
        in->PortBdf[i] = nhi->Reenum.Port[i].Bdf;
        if (nhi->Reenum.Port[i].Flags & CM_PORT_ACTIVE) {
            in->PortActive |= 1UL << i;
        }
        if (!cmStNt()) {
            cmStPath(nhi, i, in);
        }
    }
    if (nhi->U4On && nhi->U4Infer.ClosedPort != 0) {
        /* Record 22 section 7.6.2: for the connection whose closure it
         * confirmed, never a later one on the port. */
        p = TbtU4Port(&nhi->U4, nhi->U4Infer.ClosedPort);
        if (p != NULL && p->Generation == nhi->U4Infer.ClosedGen) {
            in->InferPort = nhi->U4Infer.ClosedPort;
            in->InferBdf = nhi->U4Infer.ClosedBdf;
        }
    }
}

static NTSTATUS cmStWrite(CM_STATUS_PUB *s, const UCHAR *buf, ULONG bytes)
{
    UNICODE_STRING name;

    if (s->DevKey == NULL) {
        return STATUS_INVALID_HANDLE;
    }
    RtlInitUnicodeString(&name, L"XhciCmStatus");
    return ZwSetValueKey((HANDLE)s->DevKey, &name, 0, REG_BINARY, (PVOID)buf,
                         bytes);
}

/* The record in Built written when `always`, or when it differs from the
 * last one written or the last write failed, stamped with the next
 * generation (`always`: the start's first record, generation 0). */
static VOID cmStPublish(PCM_NHI nhi, ULONG bytes, BOOLEAN always)
{
    CM_STATUS_PUB *s;
    NTSTATUS status;
    LARGE_INTEGER now;
    ULONG i;

    s = &nhi->StatusPub;
    if (!always && s->LastBytes != 0 && !s->Dirty &&
        !CmStatusChanged(s->Built, bytes, s->Last, s->LastBytes)) {
        return;
    }
    if (!CmRegEnter(nhi)) {
        /* Away from S0 on 98 SE and ME: written once back in S0. */
        s->Dirty = 1;
        cmStDefer(nhi);
        return;
    }
    if (!always) {
        s->Generation++;
        if (s->Generation == 0) {
            s->Generation = 1;
        }
    }
    KeQuerySystemTime(&now);
    CmStatusStamp(s->Built, s->Generation, cmStUptimeS(nhi), now.LowPart,
                  (ULONG)now.HighPart);
    status = cmStWrite(s, s->Built, bytes);
    CmRegLeave(nhi);
    if (!NT_SUCCESS(status)) {
        s->Dirty = 1;
        s->Failures++;
        s->LastError = (ULONG)status;
        return;
    }
    s->Writes++;
    s->Dirty = 0;
    for (i = 0; i < bytes; i++) {
        s->Last[i] = s->Built[i];
    }
    s->LastBytes = bytes;
}

static VOID cmStClose(CM_STATUS_PUB *s)
{
    if (s->DevKey != NULL) {
        ZwClose((HANDLE)s->DevKey);
        s->DevKey = NULL;
    }
    if (s->DrvKey != NULL) {
        ZwClose((HANDLE)s->DrvKey);
        s->DrvKey = NULL;
    }
}

VOID CmStatusOpen(PCM_NHI nhi)
{
    CM_STATUS_PUB *s;
    CM_STATUS_IN in;
    HANDLE key;
    PVOID devKey;
    PVOID drvKey;
    ULONG bytes;
    BOOLEAN admitted;

    s = &nhi->StatusPub;
    /* Refused admission (98 SE and ME away from S0): the keys an earlier
     * stop could not close are kept, as the same devnode's, and nothing
     * is opened; the first record is then owed (cmStPublish). */
    admitted = CmRegEnter(nhi);
    devKey = s->DevKey;
    drvKey = s->DrvKey;
    if (admitted) {
        cmStClose(s);
        devKey = NULL;
        drvKey = NULL;
    }
    CmZero(s, sizeof(*s));
    s->DevKey = devKey;
    s->DrvKey = drvKey;
    /* Before the domain is listed: no child posts to it yet. */
    CmZero(nhi->BqDiag, sizeof(nhi->BqDiag));
    nhi->BqDiagSeq = 0;
    nhi->BqDiagGen = 0;
    nhi->BqDiagWritten = 0;
    KeInitializeSpinLock(&nhi->BrDiagLock);
    if (admitted) {
        if (NT_SUCCESS(IoOpenDeviceRegistryKey(
                nhi->Common.Pdo, PLUGPLAY_REGKEY_DEVICE,
                cmStNt() ? KEY_READ | KEY_WRITE : KEY_WRITE, &key))) {
            s->DevKey = (PVOID)key;
        }
        if (NT_SUCCESS(IoOpenDeviceRegistryKey(
                nhi->Common.Pdo, PLUGPLAY_REGKEY_DRIVER,
                cmStNt() ? KEY_READ | KEY_SET_VALUE : KEY_READ, &key))) {
            s->DrvKey = (PVOID)key;
        }
        CmRegLeave(nhi);
    } else {
        CmLogNote(nhi, "cm.status.open.deferred", (ULONG)nhi->SystemPower);
    }
    /* The first record, at once: an earlier start's is replaced before
     * anything can take it for this one's (generation 0, no echo). */
    CmZero(&in, sizeof(in));
    /* AddDevice's ids: configuration space is read later in the start
     * (zero on 98 SE and ME, where AddDevice reads none). */
    in.NhiId = (nhi->Ids.Vendor & 0xFFFFUL) |
               ((nhi->Ids.Device & 0xFFFFUL) << 16);
    in.Stage = CM_ST_STAGE_STOPPED;
    /* Tier 0 (the start's, not yet looked for), the boot's written mark
     * kept (record 32 section 12.8.11). */
    in.Handover = CmOscReport(nhi) & 8UL;
    bytes = CmStatusBuild(s->Built, &in, NULL, NULL, NULL);
    s->BuiltBytes = bytes;
    cmStPublish(nhi, bytes, TRUE);
}

/* ---- the boot guard's marker (record 32 section 12.8.15.13) ----------- */

#define CM_ST_MARKER L"XhciCmOscPending"

#if !defined(_AMD64_)
typedef NTSTATUS (NTAPI *CM_ST_FLUSHKEY)(HANDLE key);
typedef NTSTATUS (NTAPI *CM_ST_DELETEVALUE)(HANDLE key,
                                            PUNICODE_STRING name);

static CM_ST_FLUSHKEY cmStFlushKey;
static CM_ST_DELETEVALUE cmStDeleteValueKey;
#endif

VOID CmStatusMarkerRoutines(PVOID flushKey, PVOID deleteValueKey)
{
#if !defined(_AMD64_)
    cmStFlushKey = (CM_ST_FLUSHKEY)flushKey;
    cmStDeleteValueKey = (CM_ST_DELETEVALUE)deleteValueKey;
#else
    UNREFERENCED_PARAMETER(flushKey);
    UNREFERENCED_PARAMETER(deleteValueKey);
#endif
}

static NTSTATUS cmStFlush(HANDLE key)
{
#if defined(_AMD64_)
    return ZwFlushKey(key);
#else
    if (cmStFlushKey == NULL) {
        return STATUS_NOT_IMPLEMENTED;
    }
    return cmStFlushKey(key);
#endif
}

static NTSTATUS cmStDelete(HANDLE key, PUNICODE_STRING name)
{
#if defined(_AMD64_)
    return ZwDeleteValueKey(key, name);
#else
    if (cmStDeleteValueKey == NULL) {
        return STATUS_NOT_IMPLEMENTED;
    }
    return cmStDeleteValueKey(key, name);
#endif
}

/* Any type is a marker; a REG_DWORD gives its kind. */
static NTSTATUS NTAPI cmStTakeMarker(PWSTR ValueName, ULONG ValueType,
                                     PVOID ValueData, ULONG ValueLength,
                                     PVOID Context, PVOID EntryContext)
{
    PUCHAR from;

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);
    *(PULONG)EntryContext = 0xFFUL;
    if (ValueType == REG_DWORD && ValueLength == sizeof(ULONG)) {
        from = (PUCHAR)ValueData;
        *(PULONG)EntryContext = (ULONG)from[0] | ((ULONG)from[1] << 8) |
                                ((ULONG)from[2] << 16) |
                                ((ULONG)from[3] << 24);
    }
    return STATUS_SUCCESS;
}

ULONG CmStatusMarkerRead(PCM_NHI nhi, PULONG kind)
{
    RTL_QUERY_REGISTRY_TABLE table[2];
    NTSTATUS status;
    ULONG v;

    *kind = 0;
    if (nhi->StatusPub.DevKey == NULL || !cmStNt()) {
        return 2UL;     /* CM_OSCG_M_UNREADABLE */
    }
    v = 0;
    CmZero(table, sizeof(table));
    table[0].QueryRoutine = cmStTakeMarker;
    table[0].Flags = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND;
    table[0].Name = CM_ST_MARKER;
    table[0].EntryContext = &v;
    status = RtlQueryRegistryValues(RTL_REGISTRY_HANDLE,
                                    (PWSTR)nhi->StatusPub.DevKey, table, NULL,
                                    NULL);
    if (status == STATUS_OBJECT_NAME_NOT_FOUND) {
        return 0UL;     /* CM_OSCG_M_ABSENT */
    }
    if (!NT_SUCCESS(status)) {
        CmLogNote(nhi, "cm.osc.guard.read", (ULONG)status);
        return 2UL;
    }
    *kind = v;
    return 1UL;         /* CM_OSCG_M_PRESENT */
}

VOID CmStatusMarkerWrite(PCM_NHI nhi, ULONG kind, BOOLEAN *written,
                         BOOLEAN *flushed)
{
    UNICODE_STRING name;
    NTSTATUS status;

    *written = FALSE;
    *flushed = FALSE;
    if (nhi->StatusPub.DevKey == NULL || !cmStNt()) {
        return;
    }
    RtlInitUnicodeString(&name, CM_ST_MARKER);
    status = ZwSetValueKey((HANDLE)nhi->StatusPub.DevKey, &name, 0,
                           REG_DWORD, &kind, sizeof(kind));
    *written = (BOOLEAN)NT_SUCCESS(status);
    if (!*written) {
        CmLogNote(nhi, "cm.osc.guard.set", (ULONG)status);
    }
    if (*written) {
        status = cmStFlush((HANDLE)nhi->StatusPub.DevKey);
        *flushed = (BOOLEAN)NT_SUCCESS(status);
        if (!*flushed) {
            CmLogNote(nhi, "cm.osc.guard.flush", (ULONG)status);
        }
    }
}

BOOLEAN CmStatusMarkerFlushable(PCM_NHI nhi)
{
    NTSTATUS status;

    if (nhi->StatusPub.DevKey == NULL || !cmStNt()) {
        return FALSE;
    }
    status = cmStFlush((HANDLE)nhi->StatusPub.DevKey);
    if (!NT_SUCCESS(status)) {
        CmLogNote(nhi, "cm.osc.guard.preflush", (ULONG)status);
        return FALSE;
    }
    return TRUE;
}

VOID CmStatusMarkerDelete(PCM_NHI nhi, BOOLEAN *deleted, BOOLEAN *flushed)
{
    *deleted = FALSE;
    *flushed = FALSE;
    if (nhi->StatusPub.DevKey == NULL || !cmStNt()) {
        return;
    }
    {
        UNICODE_STRING name;

        RtlInitUnicodeString(&name, CM_ST_MARKER);
        *deleted = (BOOLEAN)NT_SUCCESS(
            cmStDelete((HANDLE)nhi->StatusPub.DevKey, &name));
        if (*deleted) {
            *flushed = (BOOLEAN)NT_SUCCESS(
                cmStFlush((HANDLE)nhi->StatusPub.DevKey));
        }
    }
}

/* The removal's close: the last point the handles can be closed, so they
 * are closed whether or not admission is open (98 SE and ME send no
 * removal at a shutdown). */
VOID CmStatusClose(PCM_NHI nhi)
{
    BOOLEAN admitted;

    admitted = CmRegEnter(nhi);
    cmStClose(&nhi->StatusPub);
    if (admitted) {
        CmRegLeave(nhi);
    }
}

VOID CmStatusPass(PCM_NHI nhi)
{
    CM_STATUS_PUB *s;
    CM_STATUS_IN in;
    ULONG bytes;
    ULONG i;
    ULONG k;

    s = &nhi->StatusPub;
    if (nhi->Stage == CM_STAGE_STOPPED) {
        return;
    }
    if (s->DevKey != NULL && !s->Started) {
        s->Started = 1;
        s->LastBytes = 0;
    }
    /* The driver key's values (XhciCmEjectConfirm, XhciCmEjectAsk) are
     * read whether or not the device key opened: a device key that did
     * not open must not turn the eject's confirmation off. */
    cmStReadAsk(nhi);
    /* Built whether or not the key opened: the query device frames it
     * (cm_qryos.c, design record 25 section 4). */
    cmStInput(nhi, &in);
    if (nhi->U4On) {
        /* Record 34 section 3.12 item 9: each kid's chain port while its
         * occupancy stands. */
        for (i = 0; i < 4 && i < XHCI_CHAIN_KIDS; i++) {
            k = XhciChainOfKid(&nhi->Chain, i + 1UL, 0);
            if (k < XHCI_CHAIN_BINDS) {
                in.KidBridge[i] = 0x10000UL | (nhi->Chain.Bind[k].Bdf & 0xFFFFUL);
            }
        }
    }
    bytes = CmStatusBuild(s->Built, &in, &nhi->Icm, &nhi->U4, &nhi->Binds);
    s->BuiltBytes = bytes;
    if (s->DevKey == NULL) {
        return;
    }
    cmStPublish(nhi, bytes, FALSE);
    cmStQueryNameWrite(nhi);
    cmStBrDiag(nhi);
}

/* XhciCmQueryName: the query device's name, or empty once it is gone
 * (cm_qryos.c: ZwDeleteValueKey has no Windows 98 evidence). Best
 * effort, as the record. The latest name asked for is kept, and one not
 * yet written (refused admission away from S0, or a failed write) is
 * written by the next pass, or by the stop before the keys close. */
static VOID cmStQueryNameWrite(PCM_NHI nhi)
{
    CM_STATUS_PUB *s;
    UNICODE_STRING value;
    WCHAR copy[CM_ST_QNAME_CHARS];
    ULONG chars;
    ULONG seq;
    ULONG written;
    ULONG i;
    KIRQL irql;
    BOOLEAN done;

    s = &nhi->StatusPub;
    if (s->DevKey == NULL) {
        return;
    }
    /* The PnP dispatch sets the name and the thread writes it: both under
     * BrDiagLock, the copy written after it is released. */
    KeAcquireSpinLock(&nhi->BrDiagLock, &irql);
    seq = s->QNameSeq;
    written = s->QNameWritten;
    chars = s->QNameChars;
    for (i = 0; i <= chars; i++) {
        copy[i] = s->QName[i];
    }
    KeReleaseSpinLock(&nhi->BrDiagLock, irql);
    if (seq == written) {
        return;
    }
    if (!CmRegEnter(nhi)) {
        cmStDefer(nhi);
        return;
    }
    RtlInitUnicodeString(&value, L"XhciCmQueryName");
    done = (BOOLEAN)NT_SUCCESS(ZwSetValueKey((HANDLE)s->DevKey, &value, 0,
                                             REG_SZ, (PVOID)copy,
                                             (chars + 1UL) * sizeof(WCHAR)));
    CmRegLeave(nhi);
    if (done) {
        KeAcquireSpinLock(&nhi->BrDiagLock, &irql);
        s->QNameWritten = seq;
        KeReleaseSpinLock(&nhi->BrDiagLock, irql);
    }
}

VOID CmStatusQueryName(PCM_NHI nhi, PCUNICODE_STRING name)
{
    CM_STATUS_PUB *s;
    ULONG n;
    ULONG i;
    KIRQL irql;

    s = &nhi->StatusPub;
    n = (ULONG)name->Length / sizeof(WCHAR);
    if (n >= CM_ST_QNAME_CHARS) {
        n = CM_ST_QNAME_CHARS - 1UL;
    }
    KeAcquireSpinLock(&nhi->BrDiagLock, &irql);
    for (i = 0; i < n; i++) {
        s->QName[i] = name->Buffer[i];
    }
    s->QName[n] = 0;
    s->QNameChars = n;
    s->QNameSeq++;
    if (s->QNameSeq == s->QNameWritten) {
        s->QNameSeq++;
    }
    KeReleaseSpinLock(&nhi->BrDiagLock, irql);
    cmStQueryNameWrite(nhi);
}

/* XhciCmBridgeDiag, when the worker has a newer attempt to show: best
 * effort, as the record. The copy is taken under BrDiagLock, the lock the
 * worker writes it under, and written after the lock is released. */
static VOID cmStBrDiag(PCM_NHI nhi)
{
    UNICODE_STRING name;
    ULONG copy[CM_BRDIAG_WORDS];
    ULONG seq;
    ULONG i;
    KIRQL irql;

    cmStNiDiag(nhi);
    cmStBqDiag(nhi);
    if (nhi->StatusPub.DevKey == NULL) {
        return;
    }
    KeAcquireSpinLock(&nhi->BrDiagLock, &irql);
    for (i = 0; i < CM_BRDIAG_WORDS; i++) {
        copy[i] = nhi->BrDiag[i];
    }
    KeReleaseSpinLock(&nhi->BrDiagLock, irql);
    seq = copy[0];
    if (seq == 0 || seq == nhi->BrDiagWritten) {
        return;
    }
    if (!CmRegEnter(nhi)) {
        return;
    }
    RtlInitUnicodeString(&name, L"XhciCmBridgeDiag");
    if (NT_SUCCESS(ZwSetValueKey((HANDLE)nhi->StatusPub.DevKey, &name, 0,
                                 REG_BINARY, (PVOID)copy, sizeof(copy)))) {
        nhi->BrDiagWritten = seq;
    }
    CmRegLeave(nhi);
}

/* XhciCmBridgeReqDiag (design record 33 section 9.2), the same way, when
 * its generation changed (a start seen after a rewrite changes the ring
 * but not its last sequence). NT 5.x alone: nothing posts elsewhere. */
static VOID cmStBqDiag(PCM_NHI nhi)
{
    UNICODE_STRING name;
    ULONG copy[CM_BQDIAG_WORDS];
    ULONG gen;
    ULONG i;
    KIRQL irql;

    if (nhi->StatusPub.DevKey == NULL) {
        return;
    }
    KeAcquireSpinLock(&nhi->BrDiagLock, &irql);
    for (i = 0; i < CM_BQDIAG_WORDS; i++) {
        copy[i] = nhi->BqDiag[i];
    }
    gen = nhi->BqDiagGen;
    KeReleaseSpinLock(&nhi->BrDiagLock, irql);
    if (gen == 0 || gen == nhi->BqDiagWritten) {
        return;
    }
    if (!CmRegEnter(nhi)) {
        return;
    }
    RtlInitUnicodeString(&name, L"XhciCmBridgeReqDiag");
    if (NT_SUCCESS(ZwSetValueKey((HANDLE)nhi->StatusPub.DevKey, &name, 0,
                                 REG_BINARY, (PVOID)copy, sizeof(copy)))) {
        nhi->BqDiagWritten = gen;
    }
    CmRegLeave(nhi);
}

/* XhciCmNoIrqDiag (design record 28 section 7), the same way. */
static VOID cmStNiDiag(PCM_NHI nhi)
{
    UNICODE_STRING name;
    ULONG copy[CM_NIDIAG_WORDS];
    ULONG seq;
    ULONG i;
    KIRQL irql;

    if (nhi->StatusPub.DevKey == NULL) {
        return;
    }
    KeAcquireSpinLock(&nhi->BrDiagLock, &irql);
    for (i = 0; i < CM_NIDIAG_WORDS; i++) {
        copy[i] = nhi->NiDiag[i];
    }
    KeReleaseSpinLock(&nhi->BrDiagLock, irql);
    seq = copy[0];
    if (seq == 0 || seq == nhi->NiDiagWritten) {
        return;
    }
    if (!CmRegEnter(nhi)) {
        return;
    }
    RtlInitUnicodeString(&name, L"XhciCmNoIrqDiag");
    if (NT_SUCCESS(ZwSetValueKey((HANDLE)nhi->StatusPub.DevKey, &name, 0,
                                 REG_BINARY, (PVOID)copy, sizeof(copy)))) {
        nhi->NiDiagWritten = seq;
    }
    CmRegLeave(nhi);
}

VOID CmStatusStopped(PCM_NHI nhi)
{
    CM_STATUS_PUB *s;
    CM_STATUS_IN in;
    ULONG bytes;

    s = &nhi->StatusPub;
    s->Started = 0;
    if (s->DevKey != NULL) {
        CmZero(&in, sizeof(in));
        in.NhiId = (nhi->Vendor & 0xFFFFUL) |
                   ((nhi->Device & 0xFFFFUL) << 16);
        in.Stage = CM_ST_STAGE_STOPPED;
        in.Profile = nhi->Profile;
        in.Inert = nhi->Inert;
        in.Handover = CmOscReport(nhi);
        bytes = CmStatusBuild(s->Built, &in, NULL, NULL, NULL);
        s->BuiltBytes = bytes;
        s->Dirty = 1;
        cmStPublish(nhi, bytes, FALSE);
    }
    CmLogNote(nhi, "cm.status.writes", s->Writes);
    CmLogNote(nhi, "cm.status.failures", s->Failures);
    if (s->Failures != 0) {
        CmLogNote(nhi, "cm.status.last-error", s->LastError);
    }
    cmStQueryNameWrite(nhi);
    if (s->Deferred != 0) {
        CmLogNote(nhi, "cm.status.deferred", s->Deferred);
    }
    if (nhi->RegCloseTimeouts != 0) {
        CmLogNote(nhi, "cm.reg.close-timeouts", nhi->RegCloseTimeouts);
    }
    /* Refused admission: the handles are kept for the next start's open
     * or the removal's close (CmStatusClose). */
    if (CmRegEnter(nhi)) {
        cmStClose(s);
        CmRegLeave(nhi);
    } else {
        cmStDefer(nhi);
    }
}
