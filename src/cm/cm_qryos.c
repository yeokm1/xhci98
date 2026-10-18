/*
 * cm_qryos.c - the query device (design record 25 section 4.2): one
 * control device object per started NHI, \Device\XhciCm<n> linked as
 * \DosDevices\XHCICM<n>, answering IOCTL_XHCICM_QUERY (cm_query.h) from
 * the snapshot the connection-manager thread publishes after each pass.
 *
 * A FOURTH KIND OF DEVICE OBJECT (CM_KIND_QUERY): not in any PnP stack,
 * no lower object. cm_entry.c routes every major function by the
 * extension's Kind before anything else, so the NHI's and the filters'
 * paths are what they were. CREATE succeeds for the device itself (on NT,
 * an open with a name past the device's is refused), CLOSE and CLEANUP
 * succeed, DEVICE_CONTROL answers CM_Q_IOCTL, CM_QL_IOCTL and CM_QP_IOCTL
 * (the PCI answer below), everything else is
 * STATUS_INVALID_DEVICE_REQUEST.
 *
 * THE SNAPSHOT lives in the query device's own extension, under its own
 * spin lock, so the device holds no pointer to the NHI and outlives it
 * harmlessly while a handle is open. The thread builds the whole answer
 * in CM_NHI.QueryBuf (thread-owned) and copies it in under the lock
 * (CmQueryPublish); the IOCTL copies it out under the lock and fills in
 * the pass counter, its age (from the tick count, read before the lock)
 * and the stopping flag (CmQueryAnswer). The lock is held for the copies
 * alone: no registry, hardware or wait under it.
 *
 * LIFETIME. CmQueryOpen at the end of a start that succeeded (the PnP
 * start's system thread, so the link is global on XP and later); a new
 * device and a new epoch at every start. CmQueryStopping as a stop,
 * surprise removal or removal begins: the stopping flag, the name value
 * blanked, the link deleted. CmQueryClose once the thread has ended:
 * disconnected under the lock (every later request answered with
 * STATUS_DEVICE_REMOVED, STATUS_NO_SUCH_DEVICE on 98 SE and ME, and no
 * data), the NHI's pointer dropped, IoDeleteDevice.
 *
 * THE NAME VALUE. XhciCmQueryName (REG_SZ "XHCICM<n>") in the NHI's
 * hardware key, through cm_statos.c's handle. At the stop it is written
 * empty rather than deleted: ZwDeleteValueKey has no Windows 98 evidence
 * (design record 13 Appendix A row 161), and a reader that finds the
 * value empty, or an answer whose instance id is not the devnode's,
 * treats the NHI as having no query device.
 *
 * THE LOG (design record 25 section 4.5, revision 9). CM_QL_IOCTL answers
 * the NHI's log ring, copied - never drained - at the request under the
 * ring's own LogLock, taken inside the device's Lock (never the other
 * way: CmLogNote holds LogLock alone, CmQueryPublish the device's Lock
 * alone), and the counters' text the thread formats after each pass
 * (cmQCounters) and publishes with the answer. The device reaches the
 * ring through Log and LogLock, set at creation and cleared with
 * Connected in CmQueryClose, which runs before the NHI's extension can
 * go; they are followed only with Lock held and Connected set.
 *
 * THE PCI ANSWER (design record 25 section 4.7, revision 12). CM_QP_IOCTL
 * reads configuration space at the request, on the caller's thread at
 * PASSIVE_LEVEL, through HalGetBusData from offset 0 - the reads
 * cm_reenum.c and tbt_topo.c already make of the same functions on every
 * target - and never writes it. What to read is what the thread published
 * with its last answer (CmQueryPublish: the NHI's location and its
 * domain's tunnel-port set, under Lock), copied out under Lock; the reads
 * themselves hold no lock and follow no pointer to the NHI, so the device
 * still holds none for this request. PciGate lets one request at a time
 * build into the extension's Pci buffer. On Windows 98 SE and ME the
 * functions below a member are not read: a bus a tunnel brought up after
 * boot is not numbered by PCI.VXD (record 21 section 6.1), and a read of a
 * bus number nobody assigned is not made.
 *
 * IRQL: CmQueryInit in DriverEntry; CmQueryOpen, CmQueryStopping and
 * CmQueryClose in the PnP dispatch at PASSIVE_LEVEL; CmQueryPublish on the
 * connection-manager thread at PASSIVE_LEVEL, no lock held; CmQueryDispatch
 * at PASSIVE_LEVEL for CREATE, CLOSE, CLEANUP and DEVICE_CONTROL (the
 * caller's thread), <= DISPATCH_LEVEL for anything else.
 */

#include "cm.h"

/* Indices in use (bit n: \Device\XhciCm<n>) and the last epoch handed
 * out, under CmQueryGlobalLock. */
static KSPIN_LOCK CmQueryGlobalLock;
static ULONG CmQueryUsed;
static ULONG CmQueryLastLo;
static ULONG CmQueryLastHi;

#define CM_Q_NAME_CHARS 24

/* HalGetBusData: as hcd_ctl.c restates it for amd64 builds whose headers
 * leave it out. */
#if defined(_WIN64) && defined(NO_LEGACY_DRIVERS)
NTHALAPI ULONG HalGetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber,
                             ULONG SlotNumber, PVOID Buffer, ULONG Length);
#endif

XHCI_C_ASSERT(cm_qp_ports, CM_QP_MAX_PORTS == XHCI_TBT_MAX_TUNNELS);

XHCI_C_ASSERT(cm_ql_ring_bytes, XHCI_LOG_RING_BYTES == CM_QL_LOG_MAX);

/* The device interface class whose link name carries the NHI's instance
 * id (cmQInstance): this driver's own, registered and never enabled.
 * {E3E4EF80-9744-4323-A489-066E78AD2FA9} */
static const GUID cmQueryClass = {
    0xE3E4EF80, 0x9744, 0x4323, {0xA4, 0x89, 0x06, 0x6E, 0x78, 0xAD, 0x2F, 0xA9}
};

VOID CmQueryInit(VOID)
{
    KeInitializeSpinLock(&CmQueryGlobalLock);
    CmQueryUsed = 0;
    CmQueryLastLo = 0;
    CmQueryLastHi = 0;
}

static BOOLEAN cmQNt(VOID)
{
    return IoIsWdmVersionAvailable(1, 0x10);
}

/* "<stem><n>" into a UNICODE_STRING over `buf` (CM_Q_NAME_CHARS), n < 100. */
static VOID cmQName(PUNICODE_STRING s, PWCHAR buf, const char *stem, ULONG n)
{
    ULONG i;

    for (i = 0; stem[i] != 0 && i < CM_Q_NAME_CHARS - 3; i++) {
        buf[i] = (WCHAR)stem[i];
    }
    if (n >= 10) {
        buf[i++] = (WCHAR)(L'0' + n / 10);
    }
    buf[i++] = (WCHAR)(L'0' + n % 10);
    buf[i] = 0;
    s->Buffer = buf;
    s->Length = (USHORT)(i * sizeof(WCHAR));
    s->MaximumLength = (USHORT)((i + 1) * sizeof(WCHAR));
}

/* The next epoch: the system time, above every one handed out in this
 * boot, never 0. */
static VOID cmQEpoch(PULONG lo, PULONG hi)
{
    LARGE_INTEGER now;
    KIRQL irql;
    ULONG l;
    ULONG h;

    KeQuerySystemTime(&now);
    l = now.LowPart;
    h = (ULONG)now.HighPart;
    KeAcquireSpinLock(&CmQueryGlobalLock, &irql);
    if (h < CmQueryLastHi || (h == CmQueryLastHi && l <= CmQueryLastLo)) {
        h = CmQueryLastHi;
        l = CmQueryLastLo + 1;
        if (l == 0) {
            h++;
        }
    }
    if (l == 0 && h == 0) {
        l = 1;
    }
    CmQueryLastLo = l;
    CmQueryLastHi = h;
    KeReleaseSpinLock(&CmQueryGlobalLock, irql);
    *lo = l;
    *hi = h;
}

static BOOLEAN cmQClaim(ULONG n)
{
    KIRQL irql;
    BOOLEAN taken;

    KeAcquireSpinLock(&CmQueryGlobalLock, &irql);
    taken = (BOOLEAN)((CmQueryUsed & (1UL << n)) == 0);
    if (taken) {
        CmQueryUsed |= 1UL << n;
    }
    KeReleaseSpinLock(&CmQueryGlobalLock, irql);
    return taken;
}

static VOID cmQRelease(ULONG n)
{
    KIRQL irql;

    KeAcquireSpinLock(&CmQueryGlobalLock, &irql);
    CmQueryUsed &= ~(1UL << n);
    KeReleaseSpinLock(&CmQueryGlobalLock, irql);
}

/* The QUERY_ID IRP's completion. IRQL: <= DISPATCH_LEVEL. */
static NTSTATUS NTAPI cmQDone(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                              PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    (VOID)KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* One IRP_MN_QUERY_ID to the PDO, its answer appended to `id` as ASCII
 * from `at`; the new length, or 0 when unanswered or not printable ASCII. */
static ULONG cmQId(PCM_NHI nhi, BUS_QUERY_ID_TYPE type, char *id, ULONG at)
{
    PIO_STACK_LOCATION next;
    KEVENT done;
    PIRP irp;
    PWCHAR w;
    ULONG i;
    NTSTATUS status;

    irp = IoAllocateIrp(nhi->Common.Pdo->StackSize, FALSE);
    if (irp == NULL) {
        return 0;
    }
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_PNP;
    next->MinorFunction = IRP_MN_QUERY_ID;
    next->Parameters.QueryId.IdType = type;
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    CM_SET_COMPLETION_ALWAYS(irp, cmQDone, &done);
    status = IoCallDriver(nhi->Common.Pdo, irp);
    if (status == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                    NULL);
    }
    status = irp->IoStatus.Status;
    w = (PWCHAR)irp->IoStatus.Information;
    IoFreeIrp(irp);
    if (!NT_SUCCESS(status) || w == NULL) {
        return 0;
    }
    for (i = 0; w[i] != 0; i++) {
        if (at >= CM_Q_ID_MAX || w[i] < 0x20 || w[i] > 0x7E) {
            at = 0;
            break;
        }
        id[at++] = (char)w[i];
    }
    CmFree(w);
    return at;
}

/*
 * The NHI's device instance id. The PnP manager's own spelling is read out
 * of a device interface's symbolic link name: registering one on the PDO
 * (this driver's own class, never enabled) returns it, and on 2000 the
 * bus's BusQueryInstanceID is only the last element ("20"), which the PnP
 * manager prefixes with its parent's unique id. Where that fails, the
 * PDO's QUERY_ID answers: the device id, a backslash, the instance id - or
 * the instance id alone where it already holds the whole path. Empty where
 * neither answers.
 */
static VOID cmQInstance(PCM_NHI nhi)
{
    char inst[CM_Q_ID_MAX];
    UNICODE_STRING link;
    ULONG n;
    ULONG k;
    ULONG i;

    nhi->QueryIdBytes = 0;
    link.Buffer = NULL;
    link.Length = 0;
    link.MaximumLength = 0;
    if (NT_SUCCESS(IoRegisterDeviceInterface(nhi->Common.Pdo, &cmQueryClass,
                                             NULL, &link)) &&
        link.Buffer != NULL) {
        nhi->QueryIdBytes = CmQueryIdFromLink((const USHORT *)link.Buffer,
                                              link.Length / sizeof(WCHAR),
                                              nhi->QueryId, CM_Q_ID_MAX);
        /* Allocated by IoRegisterDeviceInterface: cm_pool.c's ExFreePool,
         * as hcd_door.c frees the HCD's. */
        CmFree(link.Buffer);
        if (nhi->QueryIdBytes != 0) {
            return;
        }
    }
    k = cmQId(nhi, BusQueryInstanceID, inst, 0);
    if (k == 0) {
        return;
    }
    for (i = 0; i < k && inst[i] != '\\'; i++) {
    }
    if (i < k) {
        n = 0;
    } else {
        n = cmQId(nhi, BusQueryDeviceID, nhi->QueryId, 0);
        if (n == 0 || n + 1 + k > CM_Q_ID_MAX) {
            return;
        }
        nhi->QueryId[n++] = '\\';
    }
    for (i = 0; i < k; i++) {
        nhi->QueryId[n++] = inst[i];
    }
    nhi->QueryIdBytes = n;
}

static ULONG cmQBdf(PCM_NHI nhi)
{
    ULONG bus;
    ULONG address;
    ULONG bytes;

    bus = 0;
    address = 0;
    if (!NT_SUCCESS(IoGetDeviceProperty(nhi->Common.Pdo,
                                        DevicePropertyBusNumber, sizeof(bus),
                                        &bus, &bytes)) ||
        bus > 0xFFUL ||
        !NT_SUCCESS(IoGetDeviceProperty(nhi->Common.Pdo,
                                        DevicePropertyAddress,
                                        sizeof(address), &address, &bytes)) ||
        (address >> 16) > 0x1FUL || (address & 0xFFFFUL) > 7UL) {
        return CM_ST_UNKNOWN;
    }
    return XHCI_TBT_BDF(bus, address >> 16, address & 0xFFFFUL);
}

/*
 * The counters' text (record 25 section 4.5) into out (room bytes): the
 * stop dump's blocks (CmNhiCounterLines), the re-enumeration's - a 98 SE
 * or ME worker's running counts added to those its predecessors left -
 * the child filters', and the liveness state and refusals. Returns its
 * length; *cut 1 where it did not all fit. On the connection-manager
 * thread (or the PnP path with no thread), PASSIVE_LEVEL, no lock held.
 */
static ULONG cmQCounters(PCM_NHI nhi, UCHAR *out, ULONG room, PULONG cut)
{
    static const char *const names[] = {
        "cm.reenum.ups", "cm.reenum.downs", "cm.reenum.resyncs",
        "cm.reenum.not-done", "cm.reenum.calls", "cm.reenum.call-failures",
        "cm.reenum.abandoned", "cm.reenum.poll-timeouts",
        "cm.reenum.9x.calls", "cm.reenum.9x.call-failures",
        "cm.reenum.9x.removed", "cm.reenum.9x.vetoed", "cm.reenum.9x.marked",
        "cm.reenum.9x.mark-failures", "cm.reenum.9x.unavailable",
        "cm.reenum.9x.gone", "cm.reenum.9x.gone-dropped", "cm.reenum.ejects",
        "cm.reenum.child.attached", "cm.reenum.child.over",
        "cm.reenum.child.skipped",
        "cm.reenum.child.refused", "cm.reenum.hold.watchdogs",
        "cm.live.state", "cm.live.refused",
        "cm.log.debugview"
    };
    ULONG v[sizeof(names) / sizeof(names[0])];
    CM_CFGWK_COUNTERS wc;
    PCM_REENUM r;
    ULONG at;

    r = &nhi->Reenum;
    CmZero(&wc, sizeof(wc));
    if (r->Wk != NULL) {
        CmCfgwkCounters(r->Wk, &wc);
    }
    v[0] = r->Ups;
    v[1] = r->Downs;
    v[2] = r->Resyncs;
    v[3] = r->NotDone;
    v[4] = r->Calls;
    v[5] = r->CallFailures;
    v[6] = r->Abandoned;
    v[7] = r->PollTimeouts;
    v[8] = wc.Calls;
    v[9] = wc.CallFailures;
    v[10] = r->Removed + wc.Removed;
    v[11] = r->Vetoed + wc.Vetoed;
    v[12] = r->Marked + wc.Marked;
    v[13] = r->MarkFailures + wc.MarkFailures;
    v[14] = r->Unavailable;
    v[15] = r->GoneDone + wc.GoneDone;
    v[16] = r->GoneDropped + wc.GoneDropped;
    v[17] = r->Ejects;
    CmRegCounters(&v[18], &v[19], &v[20], &v[21], &v[22]);
    v[23] = (CmLiveEpisode(&nhi->Live) << 8) | nhi->Live.State;
    v[24] = (nhi->Live.Counters.RefusedWrites << 16) |
            (nhi->Live.Counters.RefusedReads & 0xFFFFUL);
    v[25] = nhi->Values.Value[CM_VAL_LOG_DEBUGVIEW];
    at = 0;
    *cut = 0;
    CmNhiCounterLines(nhi, out, room, &at, cut);
    (VOID)CmQueryLines(out, room, &at, names, v,
                       sizeof(names) / sizeof(names[0]), cut);
    return at;
}

/* The domain's tunnel-port set into the device's copy (record 25 section
 * 4.7). The caller holds q->Lock; nhi->Reenum is the thread's, or the
 * start's before the thread runs. */
static VOID cmQPorts(PCM_NHI nhi, PCM_QUERY_DEV q)
{
    ULONG i;
    ULONG n;

    n = 0;
    if (nhi->ReenumOn) {
        for (i = 0; i < nhi->Reenum.Members && i < XHCI_TBT_MAX_TUNNELS;
             i++) {
            q->Ports[n].Bdf = nhi->Reenum.Port[i].Bdf;
            q->Ports[n].Source = nhi->Reenum.Port[i].Source;
            q->Ports[n].Flags = nhi->Reenum.Port[i].Flags;
            n++;
        }
    }
    q->PortCount = n;
    /* Windows 98 SE and ME answer no bus number or address for the NHI's
     * PDO (cmQBdf): the topology's own reading of its location stands in
     * (read on 98 SE, record 25 revision 12). */
    if (q->NhiBdf > 0xFFFFUL && nhi->ReenumOn &&
        nhi->Reenum.Nhi != XHCI_TBT_NO_BDF) {
        q->NhiBdf = nhi->Reenum.Nhi;
    }
}

/* The answer the device holds until the thread's first publish: the
 * record's header for the stage the start left, no router. */
static VOID cmQSeed(PCM_NHI nhi, PCM_QUERY_DEV q)
{
    CM_STATUS_IN in;
    CM_QUERY_IN qi;
    UCHAR rec[CM_ST_HDR_BYTES];

    CmZero(&in, sizeof(in));
    in.NhiId = (nhi->Vendor & 0xFFFFUL) | ((nhi->Device & 0xFFFFUL) << 16);
    in.Profile = nhi->Profile;
    in.Stage = nhi->Stage;
    in.Inert = nhi->Inert;
    (VOID)CmStatusBuild(rec, &in, NULL, NULL, NULL);
    CmZero(&qi, sizeof(qi));
    qi.Record = rec;
    qi.RecordBytes = CM_ST_HDR_BYTES;
    qi.Live = nhi->Live.State;
    /* Task 42-A.7: the thread's actual wait - none (no thread: nothing
     * will change until a restart), with no timeout (an inert start), or
     * with one; between waits, the wait it goes to. The wake after the
     * device is installed publishes the flag again in any case. */
    qi.Inert = (ULONG)CmQuerySeedInert((BOOLEAN)(nhi->ThreadRunning != 0),
                                       nhi->ThreadWait,
                                       CmThrInertNext(nhi));
    qi.EpochLo = nhi->QueryEpochLo;
    qi.EpochHi = nhi->QueryEpochHi;
    qi.NhiBdf = nhi->QueryBdf;
    qi.Id = nhi->QueryId;
    qi.IdBytes = nhi->QueryIdBytes;
    q->Bytes = CmQueryBuild(q->Answer, &qi);
    (VOID)CmQueryContentGen(q->Answer, q->Bytes, NULL, 0);
}

VOID CmQueryOpen(PCM_NHI nhi)
{
    WCHAR devBuf[CM_Q_NAME_CHARS];
    WCHAR linkBuf[CM_Q_NAME_CHARS];
    WCHAR value[CM_Q_NAME_CHARS];
    UNICODE_STRING dev;
    UNICODE_STRING link;
    UNICODE_STRING v;
    PDEVICE_OBJECT self;
    PCM_QUERY_DEV q;
    ULONG n;
    ULONG i;
    NTSTATUS status;

    if (nhi->QueryDev != NULL) {
        return;
    }
    cmQEpoch(&nhi->QueryEpochLo, &nhi->QueryEpochHi);
    nhi->QueryBdf = cmQBdf(nhi);
    cmQInstance(nhi);
    for (n = 0; n < CM_Q_MAX_DEVICES; n++) {
        if (!cmQClaim(n)) {
            continue;
        }
        cmQName(&dev, devBuf, "\\Device\\XhciCm", n);
        status = IoCreateDevice(CmDriverObject, sizeof(CM_QUERY_DEV), &dev,
                                FILE_DEVICE_UNKNOWN,
                                cmQNt() ? FILE_DEVICE_SECURE_OPEN : 0, FALSE,
                                &self);
        if (!NT_SUCCESS(status)) {
            cmQRelease(n);
            continue;
        }
        cmQName(&link, linkBuf, "\\DosDevices\\XHCICM", n);
        if (!NT_SUCCESS(IoCreateSymbolicLink(&link, &dev))) {
            IoDeleteDevice(self);
            cmQRelease(n);
            continue;
        }
        q = (PCM_QUERY_DEV)self->DeviceExtension;
        CmZero(q, sizeof(CM_QUERY_DEV));
        q->Common.Kind = CM_KIND_QUERY;
        q->Common.Self = self;
        KeInitializeSpinLock(&q->Lock);
        q->Index = n;
        q->Connected = 1;
        q->Log = &nhi->Log;
        q->LogLock = &nhi->LogLock;
        q->EpochLo = nhi->QueryEpochLo;
        q->EpochHi = nhi->QueryEpochHi;
        q->NhiBdf = nhi->QueryBdf;
        KeInitializeEvent(&q->PciGate, SynchronizationEvent, TRUE);
        cmQPorts(nhi, q);
        cmQSeed(nhi, q);
        /* No thread, no pass: the counters as the start left them, once. */
        if (!nhi->ThreadRunning) {
            q->CtrBytes = cmQCounters(nhi, q->Ctr, CM_QL_CTR_MAX, &q->CtrCut);
        }
        /* The thread's previous answer, before it can see the device. */
        for (i = 0; i < q->Bytes; i++) {
            nhi->QueryLast[i] = q->Answer[i];
        }
        nhi->QueryLastBytes = q->Bytes;
        self->Flags &= ~DO_DEVICE_INITIALIZING;
        nhi->QueryIndex = n;
        nhi->QueryDev = self;
        cmQName(&v, value, "XHCICM", n);
        CmStatusQueryName(nhi, &v);
        CmLogNote(nhi, "cm.query.device", n);
        /* A thread that already waits with no timeout (an inert start)
         * would not publish until something else woke it: one pass now,
         * which publishes the real state and the real inert flag. */
        if (nhi->ThreadRunning) {
            (VOID)KeSetEvent(&nhi->WorkEvent, IO_NO_INCREMENT, FALSE);
        }
        return;
    }
    CmLogNote(nhi, "cm.query.device", 0xFFFFFFFFUL);
}

VOID CmQueryStopping(PCM_NHI nhi)
{
    WCHAR linkBuf[CM_Q_NAME_CHARS];
    WCHAR empty[1];
    UNICODE_STRING link;
    UNICODE_STRING v;
    PCM_QUERY_DEV q;
    KIRQL irql;

    if (nhi->QueryDev == NULL || nhi->QueryUnlinked) {
        return;
    }
    q = (PCM_QUERY_DEV)nhi->QueryDev->DeviceExtension;
    KeAcquireSpinLock(&q->Lock, &irql);
    if (!q->Stopping) {
        q->Stopping = 1;
        /* The flag changes the answer: its content generation moves. */
        (VOID)CmQueryBumpGen(q->Answer, q->Bytes);
    }
    KeReleaseSpinLock(&q->Lock, irql);
    empty[0] = 0;
    v.Buffer = empty;
    v.Length = 0;
    v.MaximumLength = sizeof(empty);
    CmStatusQueryName(nhi, &v);
    cmQName(&link, linkBuf, "\\DosDevices\\XHCICM", nhi->QueryIndex);
    (VOID)IoDeleteSymbolicLink(&link);
    nhi->QueryUnlinked = 1;
}

VOID CmQueryClose(PCM_NHI nhi)
{
    PDEVICE_OBJECT self;
    PCM_QUERY_DEV q;
    KIRQL irql;

    if (nhi->QueryDev == NULL) {
        return;
    }
    CmQueryStopping(nhi);
    self = (PDEVICE_OBJECT)nhi->QueryDev;
    q = (PCM_QUERY_DEV)self->DeviceExtension;
    KeAcquireSpinLock(&q->Lock, &irql);
    q->Connected = 0;
    q->Log = NULL;
    q->LogLock = NULL;
    KeReleaseSpinLock(&q->Lock, irql);
    nhi->QueryDev = NULL;
    nhi->QueryUnlinked = 0;
    cmQRelease(nhi->QueryIndex);
    IoDeleteDevice(self);
}

VOID CmQueryPublish(PCM_NHI nhi, BOOLEAN inert)
{
    PDEVICE_OBJECT self;
    PCM_QUERY_DEV q;
    CM_QUERY_IN qi;
    LARGE_INTEGER ticks;
    KIRQL irql;
    ULONG bytes;
    ULONG n;
    ULONG i;

    self = (PDEVICE_OBJECT)nhi->QueryDev;
    if (self == NULL || nhi->StatusPub.BuiltBytes == 0) {
        return;
    }
    n = CmQueryExt(nhi->QueryExt, CM_ST_MAX_ROUTERS, nhi->StatusPub.Built,
                   nhi->StatusPub.BuiltBytes, &nhi->Icm,
                   nhi->U4On ? &nhi->U4 : NULL);
    CmZero(&qi, sizeof(qi));
    qi.Record = nhi->StatusPub.Built;
    qi.RecordBytes = nhi->StatusPub.BuiltBytes;
    qi.Ext = nhi->QueryExt;
    qi.ExtCount = n;
    qi.Live = nhi->Live.State;
    qi.Inert = (ULONG)inert;
    qi.EpochLo = nhi->QueryEpochLo;
    qi.EpochHi = nhi->QueryEpochHi;
    qi.NhiBdf = nhi->QueryBdf;
    qi.Id = nhi->QueryId;
    qi.IdBytes = nhi->QueryIdBytes;
    bytes = CmQueryBuild(nhi->QueryBuf, &qi);
    /* The record as CmStatusBuild makes it: the registry write's stamp,
     * which cm_statos.c puts in Built when it writes, is cleared. */
    if (bytes >= CM_Q_HDR_BYTES + CM_ST_HDR_BYTES) {
        CmStatusStamp(nhi->QueryBuf + CM_Q_HDR_BYTES, 0, 0, 0, 0);
    }
    (VOID)CmQueryContentGen(nhi->QueryBuf, bytes, nhi->QueryLast,
                            nhi->QueryLastBytes);
    nhi->QueryCtrBytes = cmQCounters(nhi, nhi->QueryCtr, CM_QL_CTR_MAX,
                                     &nhi->QueryCtrCut);
    /* The bytes, the counter and the publish time together. */
    q = (PCM_QUERY_DEV)self->DeviceExtension;
    KeAcquireSpinLock(&q->Lock, &irql);
    for (i = 0; i < bytes; i++) {
        q->Answer[i] = nhi->QueryBuf[i];
    }
    q->Bytes = bytes;
    for (i = 0; i < nhi->QueryCtrBytes; i++) {
        q->Ctr[i] = nhi->QueryCtr[i];
    }
    q->CtrBytes = nhi->QueryCtrBytes;
    q->CtrCut = nhi->QueryCtrCut;
    cmQPorts(nhi, q);
    /* Once stopping, every answer stays one generation past the thread's
     * own sequence (CmQueryStopping's bump), so it never goes back. */
    if (q->Stopping) {
        (VOID)CmQueryBumpGen(q->Answer, bytes);
    }
    KeQueryTickCount(&ticks);
    q->PassTick = ticks.LowPart;
    q->Pass++;
    if (q->Pass == 0) {
        q->Pass = 1;
    }
    KeReleaseSpinLock(&q->Lock, irql);
    for (i = 0; i < bytes; i++) {
        nhi->QueryLast[i] = nhi->QueryBuf[i];
    }
    nhi->QueryLastBytes = bytes;
}

/* IRP_MJ_DEVICE_CONTROL's CM_Q_IOCTL. */
static NTSTATUS cmQAnswer(PCM_QUERY_DEV q, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    LARGE_INTEGER ticks;
    ULONG incr;
    ULONG room;
    ULONG info;
    ULONG rc;
    ULONG pass;
    ULONG tick;
    KIRQL irql;
    PUCHAR out;

    stack = IoGetCurrentIrpStackLocation(irp);
    room = stack->Parameters.DeviceIoControl.OutputBufferLength;
    out = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    if (out == NULL) {
        room = 0;
    }
    info = 0;
    pass = 0;
    tick = 0;
    KeAcquireSpinLock(&q->Lock, &irql);
    if (!q->Connected) {
        rc = 0xFFFFFFFFUL;
    } else {
        pass = q->Pass;
        tick = q->PassTick;
        rc = CmQueryAnswer(out, room, q->Answer, q->Bytes, pass, CM_Q_UNKNOWN,
                           (BOOLEAN)(q->Stopping != 0), &info);
    }
    KeReleaseSpinLock(&q->Lock, irql);
    /* The clock read after the copy: never before the publication the
     * answer holds. */
    if (rc == CM_Q_ANS_WHOLE || rc == CM_Q_ANS_HEADER) {
        incr = KeQueryTimeIncrement();
        KeQueryTickCount(&ticks);
        CmQuerySetAge(out, info, pass, CmQueryAgeMs(ticks.LowPart, tick, incr));
    }
    if (rc == 0xFFFFFFFFUL) {
        return CmCompleteIrp(irp, cmQNt() ? STATUS_DEVICE_REMOVED
                                          : STATUS_NO_SUCH_DEVICE, 0);
    }
    if (rc == CM_Q_ANS_TOO_SMALL) {
        return CmCompleteIrp(irp, STATUS_BUFFER_TOO_SMALL, 0);
    }
    return CmCompleteIrp(irp, STATUS_SUCCESS, info);
}

/* IRP_MJ_DEVICE_CONTROL's CM_QL_IOCTL (record 25 section 4.5): the ring
 * copied under LogLock inside the device's Lock, straight into the system
 * buffer, at most CM_QL_MAX_BYTES; nothing of the ring is changed. */
static NTSTATUS cmQLog(PCM_QUERY_DEV q, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    LARGE_INTEGER ticks;
    CM_QLOG_IN in;
    PXHCI_LOG log;
    ULONG incr;
    ULONG room;
    ULONG info;
    ULONG rc;
    ULONG pass;
    ULONG tick;
    KIRQL irql;
    KIRQL logIrql;
    PUCHAR out;

    stack = IoGetCurrentIrpStackLocation(irp);
    room = stack->Parameters.DeviceIoControl.OutputBufferLength;
    out = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    if (out == NULL) {
        room = 0;
    }
    info = 0;
    pass = 0;
    tick = 0;
    rc = 0xFFFFFFFFUL;
    CmZero(&in, sizeof(in));
    KeAcquireSpinLock(&q->Lock, &irql);
    if (q->Connected && q->Log != NULL && q->LogLock != NULL) {
        pass = q->Pass;
        tick = q->PassTick;
        in.Flags = q->Stopping ? CM_QL_F_STOPPING : 0;
        if (q->Bytes >= CM_Q_HDR_BYTES &&
            ((ULONG)q->Answer[CM_Q_O_FLAGS] & CM_Q_F_INERT) != 0) {
            in.Flags |= CM_QL_F_INERT;
        }
        in.EpochLo = q->EpochLo;
        in.EpochHi = q->EpochHi;
        in.Ctr = q->Ctr;
        in.CtrBytes = q->CtrBytes;
        in.CtrCut = q->CtrCut;
        in.Pass = pass;
        KeAcquireSpinLock(q->LogLock, &logIrql);
        log = q->Log;
        if (log->Enabled) {
            in.Flags |= CM_QL_F_RECORDING;
        }
        if (log->VerbosityRefused) {
            in.Flags |= CM_QL_F_REFUSED;
        }
        if (log->DebugViewEnabled) {
            in.Flags |= CM_QL_F_EMITTED;
        }
        in.Verbosity = log->Verbosity;
        in.VerbosityRead = log->VerbosityRead;
        in.Ring = log->Ring;
        in.RingBytes = XHCI_LOG_RING_BYTES;
        in.Head = log->Head;
        in.Used = log->Used;
        in.Dropped = log->BytesDropped;
        in.Appends = log->Appends;
        rc = CmQueryLogAnswer(out, room, &in, &info);
        KeReleaseSpinLock(q->LogLock, logIrql);
    }
    KeReleaseSpinLock(&q->Lock, irql);
    if (rc == CM_Q_ANS_WHOLE || rc == CM_Q_ANS_HEADER) {
        incr = KeQueryTimeIncrement();
        KeQueryTickCount(&ticks);
        CmQueryLogSetAge(out, info, pass,
                         CmQueryAgeMs(ticks.LowPart, tick, incr));
    }
    if (rc == 0xFFFFFFFFUL) {
        return CmCompleteIrp(irp, cmQNt() ? STATUS_DEVICE_REMOVED
                                          : STATUS_NO_SUCH_DEVICE, 0);
    }
    if (rc == CM_Q_ANS_TOO_SMALL) {
        return CmCompleteIrp(irp, STATUS_BUFFER_TOO_SMALL, 0);
    }
    return CmCompleteIrp(irp, STATUS_SUCCESS, info);
}

/* One prefix of bdf's configuration space through HalGetBusData from
 * offset 0, the form cm_reenum.c's reader uses: a function that does not
 * answer is reported by the HAL as 2 bytes (the vendor id it read); a bus
 * it does not know is a refused read. IRQL: PASSIVE_LEVEL. */
static ULONG cmQpPrefix(PVOID context, ULONG bdf, UCHAR *buf, ULONG bytes)
{
    UNREFERENCED_PARAMETER(context);
    return HalGetBusData(PCIConfiguration, XHCI_TBT_BDF_BUS(bdf),
                         XHCI_TBT_BDF_DEV(bdf) |
                             (XHCI_TBT_BDF_FN(bdf) << 5),
                         buf, bytes);
}

/* The PCI answer's reader (cm_qryb.h CM_QP_READ), one DWORD at a time
 * (CmQpciReadByDword): Windows 98 SE's HAL answers the prefixes the
 * topology reads and not one 256-byte read (read on 98 SE, record 25
 * revision 12). `context` is the request's CM_QP_CFG_BYTES scratch.
 * IRQL: PASSIVE_LEVEL. */
static ULONG cmQpRead(PVOID context, ULONG bdf, UCHAR *buf, ULONG bytes,
                      PULONG valid)
{
    return CmQpciReadByDword(cmQpPrefix, NULL, bdf, buf, bytes,
                             (UCHAR *)context, valid);
}

/* IRP_MJ_DEVICE_CONTROL's CM_QP_IOCTL (record 25 section 4.7): what to
 * read copied under Lock, the reads made with no lock held into Pci under
 * PciGate, the answer copied out with CM_Q_IOCTL's sizing rules. */
static NTSTATUS cmQPci(PCM_QUERY_DEV q, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    CM_QPCI_IN in;
    ULONG room;
    ULONG info;
    ULONG rc;
    ULONG bytes;
    ULONG i;
    ULONG connected;
    KIRQL irql;
    PUCHAR out;

    stack = IoGetCurrentIrpStackLocation(irp);
    room = stack->Parameters.DeviceIoControl.OutputBufferLength;
    out = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    if (out == NULL) {
        room = 0;
    }
    if (room < CM_QP_HDR_BYTES) {
        return CmCompleteIrp(irp, STATUS_BUFFER_TOO_SMALL, 0);
    }
    CmZero(&in, sizeof(in));
    /* The gate first: what to read is taken after any request before this
     * one, and the device is checked again after the reads, so an answer
     * built across a stop is never returned. */
    (VOID)KeWaitForSingleObject(&q->PciGate, Executive, KernelMode, FALSE,
                                NULL);
    KeAcquireSpinLock(&q->Lock, &irql);
    connected = q->Connected;
    if (connected) {
        in.Flags = q->Stopping ? CM_QP_F_STOPPING : 0;
        in.EpochLo = q->EpochLo;
        in.EpochHi = q->EpochHi;
        in.NhiBdf = q->NhiBdf;
        in.PortCount = q->PortCount;
        for (i = 0; i < q->PortCount && i < CM_QP_MAX_PORTS; i++) {
            in.Ports[i] = q->Ports[i];
        }
    }
    KeReleaseSpinLock(&q->Lock, irql);
    rc = CM_Q_ANS_TOO_SMALL;
    info = 0;
    if (connected) {
        in.ReadBelow = (ULONG)cmQNt();
        in.Read = cmQpRead;
        in.Context = q->PciScratch;
        bytes = CmQpciBuild(q->Pci, CM_QP_MAX_BYTES, &in);
        KeAcquireSpinLock(&q->Lock, &irql);
        connected = q->Connected;
        if (connected && q->Stopping && bytes >= CM_QP_HDR_BYTES) {
            q->Pci[CM_QP_O_FLAGS] |= (UCHAR)CM_QP_F_STOPPING;
        }
        KeReleaseSpinLock(&q->Lock, irql);
        if (connected) {
            rc = CmQpciAnswer(out, room, q->Pci, bytes, &info);
        }
    }
    (VOID)KeSetEvent(&q->PciGate, IO_NO_INCREMENT, FALSE);
    if (!connected) {
        return CmCompleteIrp(irp, cmQNt() ? STATUS_DEVICE_REMOVED
                                          : STATUS_NO_SUCH_DEVICE, 0);
    }
    if (rc == CM_Q_ANS_TOO_SMALL) {
        return CmCompleteIrp(irp, STATUS_BUFFER_TOO_SMALL, 0);
    }
    return CmCompleteIrp(irp, STATUS_SUCCESS, info);
}

NTSTATUS CmQueryDispatch(PDEVICE_OBJECT device, PIRP irp)
{
    PCM_QUERY_DEV q;
    PIO_STACK_LOCATION stack;
    PFILE_OBJECT fo;

    q = (PCM_QUERY_DEV)device->DeviceExtension;
    stack = IoGetCurrentIrpStackLocation(irp);
    switch (stack->MajorFunction) {
    case IRP_MJ_CREATE:
        /* \\.\XHCICM<n> itself; on NT a name past it is refused. 98 SE
         * and ME are not held to it: what NTKERN leaves in FileName for a
         * \\.\ open is not read. */
        fo = stack->FileObject;
        if (cmQNt() && fo != NULL && fo->FileName.Length != 0) {
            return CmCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
        }
        return CmCompleteIrp(irp, STATUS_SUCCESS, 0);
    case IRP_MJ_CLOSE:
    case IRP_MJ_CLEANUP:
        return CmCompleteIrp(irp, STATUS_SUCCESS, 0);
    case IRP_MJ_DEVICE_CONTROL:
        if (stack->Parameters.DeviceIoControl.IoControlCode == CM_Q_IOCTL) {
            return cmQAnswer(q, irp);
        }
        if (stack->Parameters.DeviceIoControl.IoControlCode == CM_QL_IOCTL) {
            return cmQLog(q, irp);
        }
        if (stack->Parameters.DeviceIoControl.IoControlCode == CM_QP_IOCTL) {
            return cmQPci(q, irp);
        }
        break;
    case IRP_MJ_POWER:
        PoStartNextPowerIrp(irp);
        break;
    default:
        break;
    }
    return CmCompleteIrp(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
}
