/*
 * cm_statos.c - the status record's publication (design record 24 section
 * 3.2): cm_status.c builds it from the connection manager's own state and
 * this file writes it to the registry, only when it changed.
 *
 * The value XhciCmStatus (REG_BINARY) of the NHI's hardware key - Device
 * Parameters on NT, HKLM\Enum\<instance> on 98 SE and ME - through the
 * IoOpenDeviceRegistryKey and ZwSetValueKey pair xhci98.sys's door and
 * xhciuas.sys already use on every target. No volatile key: ZwCreateKey
 * would be the first import of this driver that xhci98.sys does not take,
 * with only ntkern.vxd's name table as Windows 98 SE evidence and none for
 * ME, and a missing import stops the whole driver from loading (record 24
 * section 3.2). The value therefore persists across a restart; the stamp
 * (uptime and system time) lets the tool tell this boot's record from an
 * earlier boot's. The first write of a start is made in the thread pass
 * that runs after the start's work, where cm_reenum.c also opens its
 * CONFIGMG route on 98 SE and ME: never inside the start itself.
 *
 * A failed write is counted and tried again at the next change; it never
 * touches the connection manager. The import allowlists hold
 * ZwSetValueKey and RtlInitUnicodeString to this file and add it to the
 * SITES of IoOpenDeviceRegistryKey, ZwClose and KeQuerySystemTime.
 *
 * IRQL: PASSIVE_LEVEL, on the NHI's connection-manager thread only
 * (cm_thr.c), no lock held.
 */

#include "cm.h"

static BOOLEAN cmStNt(VOID)
{
    return IoIsWdmVersionAvailable(1, 0x10);
}

/* The system's uptime in seconds, from the tick count's low word and the
 * tick period, with no 64-bit arithmetic; it wraps with the tick count. */
static ULONG cmStUptimeS(PCM_NHI nhi)
{
    LARGE_INTEGER ticks;
    ULONG q;
    ULONG r;

    KeQueryTickCount(&ticks);
    q = ticks.LowPart / 1000000UL;
    r = ticks.LowPart % 1000000UL;
    return q * nhi->ClockTickUs + (r * (nhi->ClockTickUs / 1000UL)) / 1000UL;
}

/* 98 SE and ME: member i's instance path, computed once per location
 * through the re-enumeration's configuration reader (a failure is kept
 * too, as unknown, until the member's location changes). */
static VOID cmStPath(PCM_NHI nhi, ULONG i, PCM_STATUS_IN in)
{
    CM_STATUS_PUB *s;
    ULONG bdf;
    ULONG k;

    s = &nhi->StatusPub;
    bdf = nhi->Reenum.Port[i].Bdf;
    if (s->PathBdf[i] != bdf + 1UL) {
        s->PathBdf[i] = bdf + 1UL;
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
    if (nhi->Stage == CM_STAGE_ACTIVE || nhi->Stage == CM_STAGE_FAILED) {
        if (nhi->U4On) {
            in->Cm = CM_ST_CM_USB4;
        } else if (nhi->Icm.Ring != NULL) {
            in->Cm = CM_ST_CM_ICM;
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

static NTSTATUS cmStWrite(PCM_NHI nhi, const UCHAR *buf, ULONG bytes)
{
    HANDLE key;
    UNICODE_STRING name;
    NTSTATUS status;

    status = IoOpenDeviceRegistryKey(nhi->Common.Pdo, PLUGPLAY_REGKEY_DEVICE,
                                     KEY_WRITE, &key);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    RtlInitUnicodeString(&name, L"XhciCmStatus");
    status = ZwSetValueKey(key, &name, 0, REG_BINARY, (PVOID)buf, bytes);
    ZwClose(key);
    return status;
}

static VOID cmStPublish(PCM_NHI nhi, ULONG bytes)
{
    CM_STATUS_PUB *s;
    NTSTATUS status;
    LARGE_INTEGER now;
    ULONG i;

    s = &nhi->StatusPub;
    if (s->LastBytes != 0 && !s->Dirty &&
        !CmStatusChanged(s->Built, bytes, s->Last, s->LastBytes)) {
        return;
    }
    s->Generation++;
    if (s->Generation == 0) {
        s->Generation = 1;
    }
    KeQuerySystemTime(&now);
    CmStatusStamp(s->Built, s->Generation, cmStUptimeS(nhi), now.LowPart,
                  (ULONG)now.HighPart);
    status = cmStWrite(nhi, s->Built, bytes);
    if (!NT_SUCCESS(status)) {
        s->Dirty = 1;
        s->Failures++;
        s->LastError = (ULONG)status;
        CmLogNote(nhi, "cm.status.fail", (ULONG)status);
        return;
    }
    s->Writes++;
    s->Dirty = 0;
    s->Published = 1;
    for (i = 0; i < bytes; i++) {
        s->Last[i] = s->Built[i];
    }
    s->LastBytes = bytes;
}

VOID CmStatusPass(PCM_NHI nhi)
{
    CM_STATUS_PUB *s;
    CM_STATUS_IN in;
    ULONG bytes;

    s = &nhi->StatusPub;
    if (nhi->Stage == CM_STAGE_STOPPED) {
        return;
    }
    if (!s->Started) {
        s->Started = 1;
        s->LastBytes = 0;
    }
    cmStInput(nhi, &in);
    bytes = CmStatusBuild(s->Built, &in, &nhi->Icm, &nhi->U4, &nhi->Binds);
    cmStPublish(nhi, bytes);
}

VOID CmStatusStopped(PCM_NHI nhi)
{
    CM_STATUS_PUB *s;
    CM_STATUS_IN in;
    ULONG bytes;

    s = &nhi->StatusPub;
    s->Started = 0;
    if (!s->Published) {
        return;
    }
    cmStInput(nhi, &in);
    in.Stage = CM_ST_STAGE_STOPPED;
    in.Cm = CM_ST_CM_NONE;
    bytes = CmStatusBuild(s->Built, &in, NULL, NULL, NULL);
    cmStPublish(nhi, bytes);
    CmLogNote(nhi, "cm.status.writes", s->Writes);
    CmLogNote(nhi, "cm.status.failures", s->Failures);
}
