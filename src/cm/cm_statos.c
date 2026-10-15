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
 * A failed write is counted and tried again at the next pass; it never
 * touches the connection manager. The import allowlists hold
 * ZwSetValueKey, RtlInitUnicodeString and KeQuerySystemTime to this file
 * and add it to the SITES of IoOpenDeviceRegistryKey,
 * RtlQueryRegistryValues and ZwClose.
 *
 * IRQL: PASSIVE_LEVEL. CmStatusOpen in the PnP start's dispatch, before the
 * thread's start work; CmStatusClose in the removal's dispatch, after the
 * stop (a start that failed before the thread ran leaves its handles to
 * it, or to the next CmStatusOpen); the rest on the NHI's
 * connection-manager thread only, no lock held.
 */

#include "cm.h"

#define CM_ST_ASK_MS        500UL   /* the ask read at most this often     */
#define CM_ST_PATH_RETRY_MS 5000UL  /* an unknown 98 path tried again      */

static BOOLEAN cmStNt(VOID)
{
    return IoIsWdmVersionAvailable(1, 0x10);
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

static VOID cmStReadAsk(PCM_NHI nhi)
{
    CM_STATUS_PUB *s;
    RTL_QUERY_REGISTRY_TABLE table[2];
    ULONG ask;

    s = &nhi->StatusPub;
    if (s->DrvKey == NULL || nhi->ClockMs - s->AskAtMs < CM_ST_ASK_MS) {
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

    CmZero(in, sizeof(*in));
    in->NhiId = (nhi->Vendor & 0xFFFFUL) | ((nhi->Device & 0xFFFFUL) << 16);
    in->Profile = nhi->Profile;
    in->Stage = nhi->Stage;
    in->Inert = nhi->Inert;
    in->Echo = nhi->StatusPub.Echo;
    if (nhi->Stage == CM_STAGE_ACTIVE || nhi->Stage == CM_STAGE_FAILED) {
        if (nhi->U4On) {
            in->Cm = CM_ST_CM_USB4;
        } else if (nhi->Icm.Ring != NULL) {
            in->Cm = CM_ST_CM_ICM;
            /* The running row's generation (the AddDevice profile is
             * not read on 98 SE and ME). */
            in->IcmNoUsb3 = (ULONG)(nhi->Icm.Profile != NULL &&
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
    for (i = 0; i < n; i++) {
        in->PortBdf[i] = nhi->Reenum.Port[i].Bdf;
        if (nhi->Reenum.Port[i].Flags & CM_PORT_ACTIVE) {
            in->PortActive |= 1UL << i;
        }
        if (!cmStNt()) {
            cmStPath(nhi, i, in);
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
    ULONG bytes;

    s = &nhi->StatusPub;
    cmStClose(s);
    CmZero(s, sizeof(*s));
    if (NT_SUCCESS(IoOpenDeviceRegistryKey(nhi->Common.Pdo,
                                           PLUGPLAY_REGKEY_DEVICE, KEY_WRITE,
                                           &key))) {
        s->DevKey = (PVOID)key;
    }
    if (NT_SUCCESS(IoOpenDeviceRegistryKey(nhi->Common.Pdo,
                                           PLUGPLAY_REGKEY_DRIVER, KEY_READ,
                                           &key))) {
        s->DrvKey = (PVOID)key;
    }
    /* The first record, at once: an earlier start's is replaced before
     * anything can take it for this one's (generation 0, no echo). */
    CmZero(&in, sizeof(in));
    /* AddDevice's ids: configuration space is read later in the start
     * (zero on 98 SE and ME, where AddDevice reads none). */
    in.NhiId = (nhi->Ids.Vendor & 0xFFFFUL) |
               ((nhi->Ids.Device & 0xFFFFUL) << 16);
    in.Stage = CM_ST_STAGE_STOPPED;
    bytes = CmStatusBuild(s->Built, &in, NULL, NULL, NULL);
    cmStPublish(nhi, bytes, TRUE);
}

VOID CmStatusClose(PCM_NHI nhi)
{
    cmStClose(&nhi->StatusPub);
}

VOID CmStatusPass(PCM_NHI nhi)
{
    CM_STATUS_PUB *s;
    CM_STATUS_IN in;
    ULONG bytes;

    s = &nhi->StatusPub;
    if (s->DevKey == NULL || nhi->Stage == CM_STAGE_STOPPED) {
        return;
    }
    if (!s->Started) {
        s->Started = 1;
        s->LastBytes = 0;
    }
    cmStReadAsk(nhi);
    cmStInput(nhi, &in);
    bytes = CmStatusBuild(s->Built, &in, &nhi->Icm, &nhi->U4, &nhi->Binds);
    cmStPublish(nhi, bytes, FALSE);
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
        bytes = CmStatusBuild(s->Built, &in, NULL, NULL, NULL);
        s->Dirty = 1;
        cmStPublish(nhi, bytes, FALSE);
    }
    CmLogNote(nhi, "cm.status.writes", s->Writes);
    CmLogNote(nhi, "cm.status.failures", s->Failures);
    if (s->Failures != 0) {
        CmLogNote(nhi, "cm.status.last-error", s->LastError);
    }
    cmStClose(s);
}
