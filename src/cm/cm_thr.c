/*
 * cm_thr.c - the NHI's connection-manager thread (design record 21 section
 * 4.4), its work queue, and the accessors the pure core is given
 * (TBT_HOST of tbt_ring.h, TBT_CFG of tbt_hw.h).
 *
 * ONE THREAD PER STARTED NHI, created at IRP_MN_START_DEVICE and ended at
 * the stop, surprise removal or removal that comes first, at
 * PASSIVE_LEVEL, as hcd_ctl.c's controller thread is and with its Windows
 * 98 care around the thread's handle (hcdThreadStart's comment: on NT the
 * stop waits on the thread object, on Windows 98 and ME on an event). Every
 * ring access, mailbox command, ICM command, configuration-space access of
 * the start and the stop, wait and re-enumeration call runs on it; nothing
 * else touches the rings.
 *
 * THE QUEUE is a word of work bits and three IRP slots under WorkLock
 * (one power IRP of each kind is in flight at a time, the power manager
 * serialising them per device, and one PnP request at a time). PnP
 * requests queue their work and wait on PnpDone, as PnP dispatch may;
 * power requests never wait (cm_power.c). Accepting closes when the EXIT
 * is queued: work queued before it is run before the thread leaves, and a
 * request that finds it closed takes its caller's path without the thread.
 * The thread takes every bit at each wake and runs them in cmThread's
 * order (the system IRP's work, D0, the start and its bind, D3, the stop,
 * the exit), then, while a bound client runs in D0, the idle poll.
 *
 * THE CLOCK. TBT_HOST.NowMs is a millisecond count kept here from the
 * tick count's low word (KeQueryTickCount) and the tick period
 * (KeQueryTimeIncrement), record 22 section 5's clock: monotonic, unlike
 * the system time, which can be set backwards. Tick differences are taken
 * modulo 2^32, held to CM_CLOCK_MAX_TICKS and carried in microseconds, so
 * no 64-bit arithmetic is needed and the count wraps only at 2^32 ms. It
 * is read on the thread alone; a longer gap (an idle instance) only makes
 * the clock run slow, never backwards, and every wait in the core also
 * counts its own sleeps (tbt_ring.h, TBT_DEADLINE).
 *
 * IRQL: CmThrQueue, CmPowerHold and CmPowerRelease at <= DISPATCH_LEVEL;
 * everything else PASSIVE_LEVEL - the thread, the accessors (called on it),
 * CmThrStart, CmThrStop and CmThrRunPnp (the PnP path).
 */

#include "cm.h"

/* PCI Command (PCI 3.0 6.2.2), the only configuration word written here. */
#define CM_PCI_COMMAND            0x04UL
#define CM_PCI_COMMAND_BUS_MASTER 0x0004U

/* 100 ns units per millisecond, for the relative due times. */
#define CM_CLOCK_UNITS_PER_MS 10000UL
/* The most ticks one clock read counts: 2^32 us / 15625 us at the usual
 * period's largest, well under the 32-bit product's limit for any period
 * up to 15.6 ms. */
#define CM_CLOCK_MAX_TICKS 200000UL

/* ---------------------------------------------------------------------- */
/* Configuration space                                                    */
/* ---------------------------------------------------------------------- */

static NTSTATUS NTAPI cmConfigDone(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                                   PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    (VOID)KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*
 * IRP_MN_READ_CONFIG / IRP_MN_WRITE_CONFIG to the PDO AddDevice was given,
 * never to the attached lower object: hcd_svc.c's route
 * (HcdSvcConfigSpace), read on Windows 98 SE and Windows 2000 for the HCD's
 * Bus Master write (26-A.2), and the one Windows 98 SE's own uhcd.sys
 * takes. A read zero-fills its buffer first. IRQL: PASSIVE_LEVEL.
 */
static BOOLEAN cmConfig(PCM_NHI nhi, BOOLEAN read, PVOID buffer,
                        ULONG offset, ULONG length)
{
    PIO_STACK_LOCATION next;
    KEVENT done;
    PIRP irp;
    PUCHAR p;
    ULONG i;
    NTSTATUS status;

    if (read) {
        p = (PUCHAR)buffer;
        for (i = 0; i < length; i++) {
            p[i] = 0;
        }
    }
    irp = IoAllocateIrp(nhi->Common.Pdo->StackSize, FALSE);
    if (irp == NULL) {
        nhi->Counters.ConfigFailures++;
        return FALSE;
    }
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_PNP;
    next->MinorFunction = read ? IRP_MN_READ_CONFIG : IRP_MN_WRITE_CONFIG;
    next->Parameters.ReadWriteConfig.WhichSpace = PCI_WHICHSPACE_CONFIG;
    next->Parameters.ReadWriteConfig.Buffer = buffer;
    next->Parameters.ReadWriteConfig.Offset = offset;
    next->Parameters.ReadWriteConfig.Length = length;
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    CM_SET_COMPLETION_ALWAYS(irp, cmConfigDone, &done);
    status = IoCallDriver(nhi->Common.Pdo, irp);
    if (status == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                    NULL);
    }
    status = irp->IoStatus.Status;
    IoFreeIrp(irp);
    if (!NT_SUCCESS(status)) {
        nhi->Counters.ConfigFailures++;
        return FALSE;
    }
    return TRUE;
}

/* One aligned DW; all ones when the read is refused, which every caller
 * takes as "unreadable" (tbt_hw.c's rule for a configuration space that
 * does not answer). IRQL: PASSIVE_LEVEL. */
ULONG CmCfgRead32(PCM_NHI nhi, ULONG offset)
{
    UCHAR b[4];

    if (!cmConfig(nhi, TRUE, b, offset, 4)) {
        return 0xFFFFFFFFUL;
    }
    return (ULONG)b[0] | ((ULONG)b[1] << 8) | ((ULONG)b[2] << 16) |
           ((ULONG)b[3] << 24);
}

/* One word, so a write of the command register leaves the status
 * register's write-one-to-clear bits beside it alone. IRQL: PASSIVE_LEVEL. */
BOOLEAN CmCfgWrite16(PCM_NHI nhi, ULONG offset, USHORT value)
{
    UCHAR b[2];

    b[0] = (UCHAR)(value & 0xFF);
    b[1] = (UCHAR)((value >> 8) & 0xFF);
    return cmConfig(nhi, FALSE, b, offset, 2);
}

static ULONG cmCfgRead(PVOID context, ULONG offset)
{
    return CmCfgRead32((PCM_NHI)context, offset);
}

static VOID cmCfgWrite(PVOID context, ULONG offset, ULONG value)
{
    UCHAR b[4];

    b[0] = (UCHAR)(value & 0xFF);
    b[1] = (UCHAR)((value >> 8) & 0xFF);
    b[2] = (UCHAR)((value >> 16) & 0xFF);
    b[3] = (UCHAR)((value >> 24) & 0xFF);
    (VOID)cmConfig((PCM_NHI)context, FALSE, b, offset, 4);
}

/*
 * The command register's Bus Master bit (record 21 section 5.1 step 1),
 * through the PDO as the HCD sets its own. On sets it only where it was
 * clear and records that it did, and returns TRUE only when the bit reads
 * set afterwards; off clears it only where this driver set it, after the
 * rings are stopped. A command register that cannot be read is never
 * written: its all-ones would be written back as every other bit set.
 * IRQL: PASSIVE_LEVEL.
 */
BOOLEAN CmNhiSetBusMaster(PCM_NHI nhi, BOOLEAN on)
{
    ULONG v;
    USHORT cmd;

    v = CmCfgRead32(nhi, CM_PCI_COMMAND);
    if (v == 0xFFFFFFFFUL) {
        CmLogNote(nhi, "cm.bus-master.unreadable", on ? 1UL : 0UL);
        return FALSE;
    }
    cmd = (USHORT)(v & 0xFFFF);
    if (on) {
        if ((cmd & CM_PCI_COMMAND_BUS_MASTER) == 0) {
            if (!CmCfgWrite16(nhi, CM_PCI_COMMAND,
                              (USHORT)(cmd | CM_PCI_COMMAND_BUS_MASTER))) {
                return FALSE;
            }
            nhi->BusMasterSetHere = 1;
            v = CmCfgRead32(nhi, CM_PCI_COMMAND);
            if (v == 0xFFFFFFFFUL || (v & CM_PCI_COMMAND_BUS_MASTER) == 0) {
                return FALSE;
            }
        }
        CmLogNote(nhi, "cm.bus-master", 1);
        return TRUE;
    }
    if (nhi->BusMasterSetHere && (cmd & CM_PCI_COMMAND_BUS_MASTER) != 0 &&
        CmCfgWrite16(nhi, CM_PCI_COMMAND,
                     (USHORT)(cmd & ~CM_PCI_COMMAND_BUS_MASTER))) {
        nhi->BusMasterSetHere = 0;
        CmLogNote(nhi, "cm.bus-master", 0);
    }
    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* TBT_HOST                                                               */
/* ---------------------------------------------------------------------- */

/* BAR0, one DW; all ones past the mapping, as a window that does not
 * decode reads. IRQL: PASSIVE_LEVEL (the thread). */
static ULONG cmBarRead(PVOID context, ULONG offset)
{
    PCM_NHI nhi;

    nhi = (PCM_NHI)context;
    if (nhi->BarVa == NULL || offset > nhi->BarLength - 4 || (offset & 3)) {
        return 0xFFFFFFFFUL;
    }
    return READ_REGISTER_ULONG((PULONG)(nhi->BarVa + offset));
}

static VOID cmBarWrite(PVOID context, ULONG offset, ULONG value)
{
    PCM_NHI nhi;

    nhi = (PCM_NHI)context;
    if (nhi->BarVa == NULL || offset > nhi->BarLength - 4 || (offset & 3)) {
        return;
    }
    WRITE_REGISTER_ULONG((PULONG)(nhi->BarVa + offset), value);
}

static VOID cmStall(PVOID context, ULONG microseconds)
{
    UNREFERENCED_PARAMETER(context);
    KeStallExecutionProcessor(microseconds);
}

/* A relative due time of `ms` milliseconds in 100 ns units, negative, from
 * 32-bit arithmetic (hcd_svc.c's HcdRelativeMs). */
static VOID cmRelativeMs(PLARGE_INTEGER due, ULONG ms)
{
    ULONG ticks;

    if (ms > 400000UL) {
        ms = 400000UL;
    }
    ticks = ms * CM_CLOCK_UNITS_PER_MS;
    due->LowPart = 0UL - ticks;
    due->HighPart = (ticks == 0) ? 0 : -1;
}

static VOID cmSleep(PVOID context, ULONG milliseconds)
{
    LARGE_INTEGER due;

    UNREFERENCED_PARAMETER(context);
    cmRelativeMs(&due, milliseconds);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
}

static ULONG cmNowMs(PVOID context)
{
    PCM_NHI nhi;
    LARGE_INTEGER ticks;
    ULONG delta;

    nhi = (PCM_NHI)context;
    KeQueryTickCount(&ticks);
    delta = ticks.LowPart - nhi->ClockLast;
    nhi->ClockLast = ticks.LowPart;
    if (delta > CM_CLOCK_MAX_TICKS) {
        /* So delta times the period stays inside 32 bits; a gap this long
         * (over an hour at 15.6 ms ticks) only makes the clock run slow. */
        delta = CM_CLOCK_MAX_TICKS;
    }
    nhi->ClockRest += delta * nhi->ClockTickUs;
    nhi->ClockMs += nhi->ClockRest / 1000UL;
    nhi->ClockRest %= 1000UL;
    return nhi->ClockMs;
}

static VOID cmHostLog(PVOID context, const char *label, ULONG value)
{
    CmLogNote((PCM_NHI)context, label, value);
}

/* The accessors, bound to this NHI. IRQL: PASSIVE_LEVEL. */
VOID CmThrAccessors(PCM_NHI nhi)
{
    LARGE_INTEGER ticks;

    nhi->Host.Context = nhi;
    nhi->Host.Read32 = cmBarRead;
    nhi->Host.Write32 = cmBarWrite;
    nhi->Host.Stall = cmStall;
    nhi->Host.Sleep = cmSleep;
    nhi->Host.NowMs = cmNowMs;
    nhi->Host.Log = cmHostLog;
    nhi->Cfg.Context = nhi;
    nhi->Cfg.Read32 = cmCfgRead;
    nhi->Cfg.Write32 = cmCfgWrite;
    /* The tick period in microseconds, once (record 22 section 5's clock:
     * KeQueryTimeIncrement in 100 ns units). At least 1, so the clock
     * always moves. */
    nhi->ClockTickUs = KeQueryTimeIncrement() / 10UL;
    if (nhi->ClockTickUs == 0) {
        nhi->ClockTickUs = 1;
    }
    KeQueryTickCount(&ticks);
    nhi->ClockLast = ticks.LowPart;
    nhi->ClockRest = 0;
}

/* ---------------------------------------------------------------------- */
/* The queue                                                              */
/* ---------------------------------------------------------------------- */

/* Once, at AddDevice. IRQL: PASSIVE_LEVEL. */
VOID CmThrInit(PCM_NHI nhi)
{
    KeInitializeSpinLock(&nhi->WorkLock);
    KeInitializeEvent(&nhi->WorkEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&nhi->PnpDone, SynchronizationEvent, FALSE);
    KeInitializeEvent(&nhi->ThreadExited, NotificationEvent, FALSE);
    KeInitializeEvent(&nhi->PowerIdle, NotificationEvent, TRUE);
    nhi->Accepting = 0;
    nhi->PowerOutstanding = 0;
}

/*
 * Queue `work` (one CM_WORK_* bit), with the power IRP it carries where it
 * carries one. FALSE when the thread is not accepting (not started, or its
 * EXIT already queued): the caller then goes on without it. An EXIT closes
 * the queue. IRQL: <= DISPATCH_LEVEL.
 */
BOOLEAN CmThrQueue(PCM_NHI nhi, ULONG work, PIRP irp)
{
    KIRQL irql;
    BOOLEAN taken;

    taken = FALSE;
    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    if (nhi->Accepting) {
        if (work == CM_WORK_SYSTEM || work == CM_WORK_DIRECT) {
            nhi->SystemIrp = irp;
        } else if (work == CM_WORK_D3) {
            nhi->D3Irp = irp;
        } else if (work == CM_WORK_D0) {
            nhi->D0Irp = irp;
        } else if (work == CM_WORK_EXIT) {
            nhi->Accepting = 0;
        }
        nhi->WorkBits |= work;
        taken = TRUE;
    }
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    if (taken) {
        (VOID)KeSetEvent(&nhi->WorkEvent, IO_NO_INCREMENT, FALSE);
    }
    return taken;
}

/* A PnP request's work, waited for: the start's selection (the thread
 * signals PnpDone before the bind) or the stop. FALSE from CmThrQueue
 * cannot happen here, the PnP path being the one that opens and closes
 * the queue. IRQL: PASSIVE_LEVEL. */
VOID CmThrRunPnp(PCM_NHI nhi, ULONG work)
{
    if (!CmThrQueue(nhi, work, NULL)) {
        return;
    }
    (VOID)KeWaitForSingleObject(&nhi->PnpDone, Executive, KernelMode, FALSE,
                                NULL);
}

/* Power IRPs this driver holds (pended, or a device IRP requested for a
 * system IRP): a removal closes admission (Removing), then waits until
 * none is left (cm_nhi.c, CmPowerDrain). FALSE once Removing is set: the
 * caller then passes the IRP down touching nothing of the extension's
 * after it. The release is each holder's last access to the extension.
 * IRQL: <= DISPATCH_LEVEL. */
BOOLEAN CmPowerHold(PCM_NHI nhi)
{
    KIRQL irql;
    BOOLEAN held;

    held = FALSE;
    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    if (!nhi->Removing) {
        if (nhi->PowerOutstanding++ == 0) {
            KeClearEvent(&nhi->PowerIdle);
        }
        held = TRUE;
    }
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    return held;
}

VOID CmPowerRelease(PCM_NHI nhi)
{
    KIRQL irql;

    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    if (--nhi->PowerOutstanding == 0) {
        (VOID)KeSetEvent(&nhi->PowerIdle, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&nhi->WorkLock, irql);
}

/* The removal's half: admission closed, every held IRP waited for, then
 * the lock taken once more, so the last release has left it before the
 * extension is freed. IRQL: PASSIVE_LEVEL. */
VOID CmPowerDrain(PCM_NHI nhi)
{
    KIRQL irql;

    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    nhi->Removing = 1;
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    (VOID)KeWaitForSingleObject(&nhi->PowerIdle, Executive, KernelMode,
                                FALSE, NULL);
    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    KeReleaseSpinLock(&nhi->WorkLock, irql);
}

/* ---------------------------------------------------------------------- */
/* The thread                                                             */
/* ---------------------------------------------------------------------- */

/* A client to poll: bound, in D0, not failed. */
static BOOLEAN cmPolling(PCM_NHI nhi)
{
    return (BOOLEAN)(nhi->Stage == CM_STAGE_ACTIVE && nhi->Icm.Bound &&
                     nhi->DevicePower == PowerDeviceD0 && !nhi->Suspended);
}

static VOID NTAPI cmThread(PVOID context)
{
    PCM_NHI nhi;
    LARGE_INTEGER due;
    KIRQL irql;
    ULONG bits;
    PIRP systemIrp;
    PIRP d3Irp;
    PIRP d0Irp;

    nhi = (PCM_NHI)context;
    for (;;) {
        if (cmPolling(nhi)) {
            cmRelativeMs(&due, nhi->Values.Value[CM_VAL_POLL_IDLE_MS]);
            (VOID)KeWaitForSingleObject(&nhi->WorkEvent, Executive,
                                        KernelMode, FALSE, &due);
        } else {
            (VOID)KeWaitForSingleObject(&nhi->WorkEvent, Executive,
                                        KernelMode, FALSE, NULL);
        }
        nhi->Counters.Passes++;
        (VOID)cmNowMs(nhi);

        KeAcquireSpinLock(&nhi->WorkLock, &irql);
        bits = nhi->WorkBits;
        nhi->WorkBits = 0;
        systemIrp = nhi->SystemIrp;
        d3Irp = nhi->D3Irp;
        d0Irp = nhi->D0Irp;
        nhi->SystemIrp = NULL;
        nhi->D3Irp = NULL;
        nhi->D0Irp = NULL;
        KeReleaseSpinLock(&nhi->WorkLock, irql);

        if (bits & CM_WORK_DIRECT) {
            CmPowerDirectWork(nhi, systemIrp);
        } else if (bits & CM_WORK_SYSTEM) {
            CmPowerSystemWork(nhi, systemIrp);
        }
        if (bits & CM_WORK_D0) {
            CmPowerD0Work(nhi, d0Irp);
        }
        /* The start before a D3 queued beside it, so that the D3 finds the
         * client the start made and quiesces it; the start itself refuses
         * to activate anything out of D0 (CmNhiStartWork). */
        if (bits & CM_WORK_START) {
            CmNhiStartWork(nhi);
            (VOID)KeSetEvent(&nhi->PnpDone, IO_NO_INCREMENT, FALSE);
            if (nhi->BindOwed) {
                nhi->BindOwed = 0;
                CmNhiBind(nhi);
            }
        }
        if (bits & CM_WORK_D3) {
            CmPowerD3Work(nhi, d3Irp);
        }
        if (bits & CM_WORK_STOP) {
            CmNhiStopWork(nhi);
            (VOID)KeSetEvent(&nhi->PnpDone, IO_NO_INCREMENT, FALSE);
        }
        if (bits & CM_WORK_EXIT) {
            break;
        }
        if (cmPolling(nhi)) {
            nhi->Counters.Polls++;
            CmNhiPollPass(nhi);
        }
        /* Once per pass, after the actions (cm_reenum.h): the seam's own
         * work, eject notifications and the filters' baselines. */
        if (nhi->ReenumOn) {
            CmReenumService(nhi);
        }
        /* The checked flavours' emission (decision D12), from here at
         * PASSIVE_LEVEL after each pass that recorded something; the
         * release flavour's CmLogEmit does nothing. */
        CmLogEmit(nhi, XHCI_LOG_REASON_PERIODIC);
    }
    (VOID)KeSetEvent(&nhi->ThreadExited, IO_NO_INCREMENT, FALSE);
    (VOID)PsTerminateSystemThread(STATUS_SUCCESS);
}

/*
 * The thread, created and its queue opened. How the stop waits for it
 * differs by target and follows hcd_ctl.c's hcdThreadStart, each half the
 * one measured there: the thread object on NT (WDM 1.10 and later), whose
 * signal alone says the thread has left the image a removal unloads; the
 * event on Windows 98 and ME, where a wait on the thread object faulted.
 * IRQL: PASSIVE_LEVEL.
 */
NTSTATUS CmThrStart(PCM_NHI nhi)
{
    HANDLE handle;
    NTSTATUS status;
    KIRQL irql;

    nhi->ThreadRunning = 0;
    nhi->ThreadObject = NULL;
    nhi->WorkBits = 0;
    KeClearEvent(&nhi->ThreadExited);
    status = PsCreateSystemThread(&handle, THREAD_ALL_ACCESS, NULL, NULL,
                                  NULL, cmThread, nhi);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    /* Opened only once the thread exists: work accepted is work run. */
    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    nhi->Accepting = 1;
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    status = STATUS_SUCCESS;
    if (IoIsWdmVersionAvailable(1, 0x10)) {
        /* A NULL object type: PsThreadType is a data import. */
        status = ObReferenceObjectByHandle(handle, THREAD_ALL_ACCESS, NULL,
                                           KernelMode, &nhi->ThreadObject,
                                           NULL);
    }
    (VOID)ZwClose(handle);
    if (!NT_SUCCESS(status)) {
        /* hcd_ctl.c's residual, kept: without the object a removal's
         * unload cannot be joined, so the start does not proceed; the
         * thread is told to leave and its event waited for. */
        nhi->ThreadReferenceFailures++;
        nhi->ThreadObject = NULL;
        (VOID)CmThrQueue(nhi, CM_WORK_EXIT, NULL);
        (VOID)KeWaitForSingleObject(&nhi->ThreadExited, Executive,
                                    KernelMode, FALSE, NULL);
        return status;
    }
    nhi->ThreadRunning = 1;
    return STATUS_SUCCESS;
}

/* The queue closed and the thread joined; what was queued before the EXIT
 * runs first. IRQL: PASSIVE_LEVEL. */
VOID CmThrStop(PCM_NHI nhi)
{
    if (!nhi->ThreadRunning) {
        return;
    }
    (VOID)CmThrQueue(nhi, CM_WORK_EXIT, NULL);
    if (nhi->ThreadObject != NULL) {
        (VOID)KeWaitForSingleObject(nhi->ThreadObject, Executive, KernelMode,
                                    FALSE, NULL);
        ObDereferenceObject(nhi->ThreadObject);
        nhi->ThreadObject = NULL;
    } else {
        (VOID)KeWaitForSingleObject(&nhi->ThreadExited, Executive,
                                    KernelMode, FALSE, NULL);
    }
    nhi->ThreadRunning = 0;
}
