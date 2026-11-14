/*
 * cm_snapos.c - the query device's CM_QS_IOCTL: the connection manager's
 * state snapshot (design record 21 section 8.4, design record 25 section
 * 23; roadmap-usb4 task 49.37).
 *
 * The gather reads the NHI's extension field by field beside the running
 * thread, with no lock held, under the query device's SnapUse hold: the
 * thread takes no lock for most of this state, and a gather it had to run
 * itself would miss the state of a thread stuck in a wait. The tear
 * detector says whether the thread, a log producer or the ring interrupt
 * moved during the read.
 *
 * IRQL: PASSIVE_LEVEL unless a function says otherwise.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "cm.h"
#include "cm_snapb.h"
#include "../xhci_version.h"

/* The generated directories of CM_NHI and the kernel-side structures. */
#include "cm_snapn.h"

/* XHCI_VER_CSV as four values: MSVC 6.0's preprocessor hands a macro's
   expansion on as one argument, so it cannot be spread into a macro's
   four parameters. */
static const UCHAR cmQsVersion[4] = { XHCI_VER_CSV };

/* The sum of four counters that only increase: the thread's wakes, every
   log producer call (recorded or suppressed) and the ring interrupt's
   DPCs. Any IRQL. */
static ULONG cmQsDetector(const CM_NHI *nhi)
{
    const volatile ULONG *passes = &nhi->Counters.Passes;
    const volatile ULONG *appends = &nhi->Log.Appends;
    const volatile ULONG *suppressed = &nhi->Log.Suppressed;
    const volatile LONG *dpcs = &nhi->Rirq.DpcSeq;

    return *passes + *appends + *suppressed + (ULONG)*dpcs;
}

static ULONG cmQsTables(const CM_NHI *nhi, CM_QS_TABLE *t)
{
    ULONG n = 0;
    BOOLEAN u4 = (BOOLEAN)(nhi->U4On != 0);

#define CM_QS_T(name, type, base, records)                                   \
    t[n].Name = (name);                                                      \
    t[n].Fields = CmQsF_##type;                                              \
    t[n].FieldCount = CmQsN_##type;                                          \
    t[n].Base = (const UCHAR *)(base);                                       \
    t[n].Records = (records);                                                \
    t[n].Stride = (ULONG)sizeof(type);                                       \
    n++

    CM_QS_T("nhi", CM_NHI, nhi, 1);
    CM_QS_T("cm.ctr", CM_COUNTERS, &nhi->Counters, 1);
    CM_QS_T("u4", TBT_U4CM, u4 ? &nhi->U4 : NULL, 1);
    CM_QS_T("u4.port", TBT_U4_PORT, u4 ? nhi->U4.Ports : NULL,
            TBT_U4_MAX_PORTS);
    CM_QS_T("u4.link", TBT_U4_LINK, u4 ? nhi->U4.Links : NULL,
            TBT_U4_MAX_LINKS);
    CM_QS_T("u4.kid", TBT_U4_KID, u4 ? nhi->U4.Kids : NULL, TBT_U4_MAX_KIDS);
    CM_QS_T("u4.oblig", TBT_U4_OBLIGATION, u4 ? nhi->U4.Obligations : NULL,
            TBT_U4_OBLIGATIONS);
    CM_QS_T("u4.xd", TBT_U4_XD, u4 ? nhi->U4.Xd : NULL, TBT_U4_MAX_PORTS);
    CM_QS_T("u4.ctr", TBT_U4_COUNTERS, u4 ? &nhi->U4.Counters : NULL, 1);
    CM_QS_T("icm", TBT_ICM, u4 ? NULL : &nhi->Icm, 1);
    CM_QS_T("icm.dev", TBT_ICM_DEVICE, u4 ? NULL : nhi->Icm.Devices,
            TBT_ICM_MAX_DEVICES);
    CM_QS_T("icm.ctr", TBT_ICM_COUNTERS, u4 ? NULL : &nhi->Icm.Counters, 1);
    CM_QS_T("ring", TBT_RING, &nhi->Ring, 1);
    CM_QS_T("rirq", CM_RIRQ_DRV, &nhi->Rirq, 1);
    CM_QS_T("reenum", CM_REENUM, &nhi->Reenum, 1);
    CM_QS_T("reenum.port", CM_REENUM_PORT, nhi->Reenum.Port,
            XHCI_TBT_MAX_TUNNELS);
    CM_QS_T("chain.bind", XHCI_CHAIN_BIND, nhi->Chain.Bind, XHCI_CHAIN_BINDS);
    CM_QS_T("net", CM_NET, &nhi->Net, 1);
#undef CM_QS_T
    return n;
}

/* The identity hashes' salt, once per start, from nothing any answer of
   the query device carries: the tick count, the ring block's physical
   address, the BAR's mapping and the extension's own address, mixed with
   the epoch. */
VOID CmQuerySnapSalt(PCM_QUERY_DEV q, const CM_NHI *nhi)
{
    LARGE_INTEGER ticks;
    ULONG mix[6];

    KeQueryTickCount(&ticks);
    mix[0] = ticks.LowPart;
    mix[1] = nhi->CommonPa.LowPart;
    mix[2] = (ULONG)(ULONG_PTR)nhi->BarVa;
    mix[3] = (ULONG)(ULONG_PTR)nhi;
    mix[4] = (ULONG)(((ULONG_PTR)nhi->CommonVa) >> 4);
    mix[5] = q->EpochLo ^ q->EpochHi;
    q->SnapSalt = CmQsHash(mix[0] ^ mix[5], (const UCHAR *)mix,
                           (ULONG)sizeof(mix)) |
                  (CmQsHash(mix[2] ^ mix[3], (const UCHAR *)mix,
                            (ULONG)sizeof(mix)) << 16);
}

VOID CmQuerySnapGone(PCM_NHI nhi)
{
    PDEVICE_OBJECT self;
    PCM_QUERY_DEV q;
    LARGE_INTEGER step;
    KIRQL irql;
    ULONG use;

    self = (PDEVICE_OBJECT)nhi->QueryDev;
    if (self == NULL) {
        return;
    }
    q = (PCM_QUERY_DEV)self->DeviceExtension;
    /* 1 ms. A gather is a few thousand word reads. */
    CmRelativeMs(&step, 1);
    for (;;) {
        KeAcquireSpinLock(&q->Lock, &irql);
        q->SnapNhi = NULL;
        use = q->SnapUse;
        KeReleaseSpinLock(&q->Lock, irql);
        if (use == 0) {
            return;
        }
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &step);
    }
}

NTSTATUS CmQuerySnap(PCM_QUERY_DEV q, PIRP irp, BOOLEAN nt)
{
    PIO_STACK_LOCATION stack;
    CM_QS_TABLE tables[24];
    CM_QS_IN in;
    PCM_NHI nhi;
    PUCHAR buf;
    KIRQL irql;
    ULONG room;
    ULONG at;
    ULONG total;
    ULONG count;
    ULONG bytes;

    stack = IoGetCurrentIrpStackLocation(irp);
    room = stack->Parameters.DeviceIoControl.OutputBufferLength;
    buf = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    if (buf == NULL) {
        room = 0;
    }
    /* The input shares the buffer: read it before anything is written. */
    at = 0;
    if (buf != NULL &&
        stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(ULONG)) {
        at = (ULONG)buf[0] | ((ULONG)buf[1] << 8) | ((ULONG)buf[2] << 16) |
             ((ULONG)buf[3] << 24);
    }
    if (room < CM_QS_HDR_BYTES) {
        return CmCompleteIrp(irp, STATUS_BUFFER_TOO_SMALL, 0);
    }
    KeAcquireSpinLock(&q->Lock, &irql);
    nhi = q->Connected ? q->SnapNhi : NULL;
    if (nhi != NULL) {
        q->SnapUse++;
    }
    CmZero(&in, sizeof(in));
    in.EpochLo = q->EpochLo;
    in.EpochHi = q->EpochHi;
    in.Salt = q->SnapSalt;
    if (q->Stopping) {
        in.Flags |= CM_QS_F_STOPPING;
    }
    KeReleaseSpinLock(&q->Lock, irql);
    if (nhi == NULL) {
        return CmCompleteIrp(irp, nt ? STATUS_DEVICE_REMOVED
                                     : STATUS_NO_SUCH_DEVICE, 0);
    }
    /* Before anything of the NHI is read, the header's words included. */
    in.DetBefore = cmQsDetector(nhi);
    if (nhi->U4On) {
        in.Flags |= CM_QS_F_U4;
    } else if (nhi->Inert == CM_INERT_NONE) {
        in.Flags |= CM_QS_F_ICM;
    }
    if (nhi->Inert != CM_INERT_NONE) {
        in.Flags |= CM_QS_F_INERT;
    }
    if (!nhi->ThreadRunning) {
        in.Flags |= CM_QS_F_NO_THREAD;
    }
    in.Driver = CM_QS_DRIVER_OF(cmQsVersion[0], cmQsVersion[1],
                                cmQsVersion[2], cmQsVersion[3]);
    in.Level = nhi->Log.Verbosity;
    in.ClockMs = nhi->ClockMs;
    in.Layout = (ULONG)sizeof(CM_NHI);
    count = cmQsTables(nhi, tables);
    bytes = CmQsBuild(buf, room, at, &in, tables, count, &total);
    CmQsSetAfter(buf, cmQsDetector(nhi));

    KeAcquireSpinLock(&q->Lock, &irql);
    q->SnapUse--;
    KeReleaseSpinLock(&q->Lock, irql);
    return CmCompleteIrp(irp, STATUS_SUCCESS, bytes);
}
