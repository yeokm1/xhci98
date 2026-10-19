// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (c) 2026, Yeo Kheng Meng
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Parent/child request engine (PLAN 0.2), admission control (0.3) and
// per-child PRP construction (0.4).
//
// - A parent is the SRB: it owns its buffer, its fragment cursor, the count
//   of children outstanding, its first error and its cancellation state. It
//   lives in a bounded table in the device extension.
// - A child is one NVMe command. Its CID encodes the child slot and the
//   slot's generation (engine.h); the slot also records the controller
//   generation. A completion is consumed only if all three match an issued
//   child (anything else is logged as stale and dropped), so each child is
//   accounted for exactly once.
// - A parent completes exactly once (EngFinishParent): after every child it
//   issued has been quiesced (its own completion consumed) or the recovery
//   contract released it (isolation evidence, or the MD-1 gone deadline),
//   with every copy done before RequestComplete. Its table slot is freed
//   before RequestComplete and the SRB is never touched afterwards.
// - Admission: a parent waits in the table (admission order) until children,
//   PRP pages and bounce pages are free; NextRequest is withheld meanwhile.
//   A parent that has issued a child is never completed BUSY.
// - PRP construction: each child is mapped from its own buffer offset and
//   length; every mapping is validated (DWORD-aligned first entry,
//   page-aligned later entries, and a returned length that covers each
//   entry's span: EngMapCaller). A child whose mapping fails, or every
//   data child when the test tunable forces it, is bounced through the bounce
//   pages carved from the uncached extension (MD-3 (a)); bounced pages are
//   physically contiguous (validated at HwFindAdapter).
//

#include "nvme2k.h"
#include "utils.h"
#ifdef NVME2K_W9X
#include "sg9x.h"
#endif

#define ENG_ALL_ONES    0xFFFFFFFF
#define ENG_MAP_UNRESOLVED  5     // STG_ENG_MAP_FAIL: the caller's buffer could not be resolved (9x)

#ifdef NVME2K_W9X
// STG_SG9X argument (dbgport.h).
static ULONG EngSg9xStage(IN PSCSI_REQUEST_BLOCK Srb, IN ULONG Kind, IN ULONG Use, IN BOOLEAN Ok)
{
    ULONG func = Sg9xIorFunc(Srb);

    return Kind | (Use << 2) | ((ULONG)(Ok ? 1 : 0) << 4) | ((func > 0x3E ? 0x3F : func) << 8) |
           ((ULONG)Srb->Cdb[0] << 16);
}
#endif

// Linear address of byte Offset of the SRB's data, or NULL when it cannot be
// established (9x: an SG list that does not cover it, or an unknown layout;
// sg9x.h, Sg9xClassify).
static PUCHAR EngCallerVa(IN PSCSI_REQUEST_BLOCK Srb, IN ULONG Offset)
{
#ifdef NVME2K_W9X
    UCHAR shift;
    ULONG lin, run;
    const STOR98_SGD9X *sgd;

    ULONG kind = Sg9xClassify(Srb, Srb->DataBuffer, &sgd, &shift);
    BOOLEAN ok;

    ok = (BOOLEAN)(kind == SG9X_LINEAR || (kind == SG9X_SG && Sg9xResolve(sgd, shift, Offset, &lin, &run)));
#ifdef STOR98_DBGPORT
    if (kind != SG9X_LINEAR || Sg9xIorFunc(Srb) > 1) {
        DBGSTAGE(STG_SG9X, EngSg9xStage(Srb, kind, 0, ok));
    }
#endif
    if (kind != SG9X_LINEAR) {
        return ok ? (PUCHAR)(ULONG_PTR)lin : NULL;
    }
#endif
    return (PUCHAR)Srb->DataBuffer + Offset;
}

#if defined(NVME2K_W9X) && defined(STOR98_DBGPORT)
// Test builds: how the buffer of an SRB that is not READ/WRITE is laid out
// (pass-through, SMART), logged as STG_SG9X use 3. Nothing is changed.
VOID EngProbeSg9x(IN PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR shift;
    const STOR98_SGD9X *sgd;
    ULONG kind;

    if (Srb->Function != SRB_FUNCTION_EXECUTE_SCSI || Srb->DataBuffer == NULL || Srb->DataTransferLength == 0) {
        return;
    }
    switch (Srb->Cdb[0]) {
    case SCSIOP_READ6: case SCSIOP_READ: case SCSIOP_READ16:
    case SCSIOP_WRITE6: case SCSIOP_WRITE: case SCSIOP_WRITE16:
        return;
    }
    kind = Sg9xClassify(Srb, Srb->DataBuffer, &sgd, &shift);
    DBGSTAGE(STG_SG9X, EngSg9xStage(Srb, kind, 3, (BOOLEAN)(kind == SG9X_LINEAR)));
}
#endif

// Copy Bytes between the bounce pages and the SRB's data at Offset, fragment
// by fragment on 9x. ToCaller: bounce -> caller (a read), else caller ->
// bounce (a write). FALSE, with nothing copied, when the whole span cannot
// be resolved: the caller fails the parent instead of guessing an address.
static BOOLEAN EngCallerCopy(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN ULONG Offset, IN PUCHAR Bounce,
                             IN ULONG Bytes, IN BOOLEAN ToCaller)
{
#ifdef NVME2K_W9X
    UCHAR shift;
    const STOR98_SGD9X *sgd;

    ULONG kind = Sg9xClassify(Srb, Srb->DataBuffer, &sgd, &shift);
    BOOLEAN ok = TRUE;

#ifdef STOR98_DBGPORT
    // Test builds: report every Nth copy unresolved, before anything is
    // copied, to exercise the failure paths (STOR98_TEST_SG9X_FAIL).
    if (DevExt->Tun.Sg9xFailEvery != 0 && ++DevExt->Tun.Sg9xFailCount % DevExt->Tun.Sg9xFailEvery == 0) {
        DBGSTAGE(STG_SG9X, EngSg9xStage(Srb, SG9X_UNKNOWN, ToCaller ? 2 : 1, FALSE));
        return FALSE;
    }
#endif
    if (kind == SG9X_SG) {
        ok = Sg9xCopy(sgd, shift, Offset, Bounce, Bytes, ToCaller);
    } else if (kind != SG9X_LINEAR) {
        ok = FALSE;
    }
    DBGSTAGE(STG_SG9X, EngSg9xStage(Srb, kind, ToCaller ? 2 : 1, ok));
    if (kind != SG9X_LINEAR) {
        return ok;
    }
#endif
    if (ToCaller) {
        RtlCopyMemory((PUCHAR)Srb->DataBuffer + Offset, Bounce, Bytes);
    } else {
        RtlCopyMemory(Bounce, (PUCHAR)Srb->DataBuffer + Offset, Bytes);
    }
    return TRUE;
}

static ULONG EngNow(IN PHW_DEVICE_EXTENSION DevExt)
{
    return ClockNow(&DevExt->Clock);
}

static ULONG EngSafe(IN PHW_DEVICE_EXTENSION DevExt)
{
    return ClockSafeMs(&DevExt->Clock);
}

static ULONG EngMsSince(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Since)
{
    return EngNow(DevExt) - Since;
}

static BOOLEAN EngIsTagged(IN PSCSI_REQUEST_BLOCK Srb)
{
    return (BOOLEAN)((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) && Srb->QueueTag != SP_UNTAGGED);
}

//
// Completion of an SRB to the port (every path ends here)
//
VOID EngCompleteSrb(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    ULONG i;

    // An SRB that completes other than BUSY is no longer counted (0.3).
    if (SRB_STATUS(Srb->SrbStatus) != SRB_STATUS_BUSY) {
        for (i = 0; i < ENG_BUSY_TRACK; i++) {
            if (DevExt->Busy[i].Srb == Srb) {
                DevExt->Busy[i].Srb = NULL;
                DevExt->Busy[i].Count = 0;
            }
        }
    }
    DevExt->SrbsCompleted++;
    DBGSTAGE(STG_SRB_COMPLETE, ((ULONG)Srb->SrbStatus << 24) | ((ULONG)Srb->Cdb[0] << 8) | Srb->Function);
    ScsiPortNotification(RequestComplete, DevExt, Srb);
}

//
// BUSY with the per-SRB count (PLAN 0.3, Q-NT): the port re-issues a BUSY
// SRB 20 times and fails it at the 21st BUSY; the driver fails it itself
// instead of returning that 21st BUSY. Only for requests that never had a
// child issued. Returns the status used.
//
// An entry counts for the SRB it was made for only: the same address with
// another function, opcode or data buffer is an unrelated request (the
// port abandoned the old one after a BUSY and reused its memory), which
// starts from 0 rather than inherit the count.
static BOOLEAN EngBusySame(IN ENG_BUSY *b, IN PSCSI_REQUEST_BLOCK Srb)
{
    return (BOOLEAN)(b->Function == Srb->Function && b->Cdb0 == Srb->Cdb[0] && b->DataBuffer == Srb->DataBuffer);
}

UCHAR EngBusyOrFail(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    ULONG i, slot = ENG_BUSY_TRACK, oldest = 0, now = EngNow(DevExt);
    ENG_BUSY *b;

    for (i = 0; i < ENG_BUSY_TRACK; i++) {
        if (DevExt->Busy[i].Srb == Srb) {
            slot = i;
            if (!EngBusySame(&DevExt->Busy[i], Srb)) {
                DevExt->Busy[i].Count = 0;
            }
            break;
        }
    }
    if (slot == ENG_BUSY_TRACK) {
        // New entry: a free one, else the least recently used.
        for (i = 0; i < ENG_BUSY_TRACK; i++) {
            if (DevExt->Busy[i].Srb == NULL) {
                slot = i;
                break;
            }
            if (now - DevExt->Busy[i].Last > now - DevExt->Busy[oldest].Last) {
                oldest = i;
            }
        }
        if (slot == ENG_BUSY_TRACK) {
            slot = oldest;
        }
        DevExt->Busy[slot].Srb = Srb;
        DevExt->Busy[slot].Count = 0;
    }
    b = &DevExt->Busy[slot];
    b->Function = Srb->Function;
    b->Cdb0 = Srb->Cdb[0];
    b->DataBuffer = Srb->DataBuffer;
    b->Last = now;
    Srb->DataTransferLength = 0;
    if (b->Count >= ENG_BUSY_LIMIT) {
        // NOT READY, LOGICAL UNIT IS IN PROCESS OF BECOMING READY
        DBGSTAGE(STG_BUSY_LIMIT, STG_SRB_ARG(Srb, DevExt->State));
        b->Srb = NULL;
        b->Count = 0;
        Srb->SrbStatus = SRB_STATUS_ERROR;
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
        ScsiSetSense(DevExt, Srb, SCSI_SENSE_NOT_READY, SCSI_ADSENSE_LUN_NOT_READY, 0x01);
        return SRB_STATUS_ERROR;
    }
    b->Count++;
    DevExt->BusyReturned++;
    DBGSTAGE(STG_BUSY, (b->Count & 0xFFF) | (((ULONG)(ULONG_PTR)Srb & 0xFFF0) << 8) | ((ULONG)Srb->Cdb[0] << 24));
    Srb->SrbStatus = SRB_STATUS_BUSY;
    return SRB_STATUS_BUSY;
}

//
// Pools carved from the uncached extension
//
static UCHAR EngPrpAlloc(IN PHW_DEVICE_EXTENSION DevExt)
{
    UCHAR i;

    for (i = 0; i < ENG_PRP_PAGES; i++) {
        if (!(DevExt->PrpListPageBitmap & (1UL << i))) {
            DevExt->PrpListPageBitmap |= (1UL << i);
            DevExt->CurrentPrpListPagesUsed++;
            if (DevExt->CurrentPrpListPagesUsed > DevExt->MaxPrpListPagesUsed) {
                DevExt->MaxPrpListPagesUsed = DevExt->CurrentPrpListPagesUsed;
            }
            return i;
        }
    }
    return ENG_NONE;
}

static VOID EngPrpFree(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR Page)
{
    if (Page < ENG_PRP_PAGES && (DevExt->PrpListPageBitmap & (1UL << Page))) {
        DevExt->PrpListPageBitmap &= ~(1UL << Page);
        DevExt->CurrentPrpListPagesUsed--;
    }
}

static PVOID EngPrpVa(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR Page)
{
    return (PVOID)((PUCHAR)DevExt->PrpListPages + (ULONG)Page * PAGE_SIZE);
}

static ULONGLONG EngPrpPa(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR Page)
{
    return (ULONGLONG)DevExt->PrpPagePa[Page].QuadPart;
}

static UCHAR EngBounceAlloc(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Count)
{
    ULONG first;
    ULONG mask;

    if (Count == 0 || Count > ENG_BOUNCE_PAGES) {
        return ENG_NONE;
    }
    mask = (Count == 32) ? 0xFFFFFFFF : ((1UL << Count) - 1);
    for (first = 0; first + Count <= ENG_BOUNCE_PAGES; first++) {
        if (!(DevExt->BounceBitmap & (mask << first))) {
            DevExt->BounceBitmap |= (mask << first);
            return (UCHAR)first;
        }
    }
    return ENG_NONE;
}

static VOID EngBounceFree(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR First, IN UCHAR Count)
{
    ULONG mask;

    if (First == ENG_NONE || Count == 0) {
        return;
    }
    mask = ((1UL << Count) - 1) << First;
    DevExt->BounceBitmap &= ~mask;
}

static PUCHAR EngBounceVa(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR Page)
{
    return (PUCHAR)DevExt->BouncePages + (ULONG)Page * PAGE_SIZE;
}

static ULONGLONG EngBouncePa(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR Page)
{
    return (ULONGLONG)DevExt->BouncePagePa[Page].QuadPart;
}

//
// Children
//
ULONG EngIssuedChildren(IN PHW_DEVICE_EXTENSION DevExt)
{
    return DevExt->ChildrenIo + DevExt->ChildrenAdmin;
}

static PENG_CHILD EngChildAlloc(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Admin, OUT PULONG Index)
{
    ULONG i;
    PENG_CHILD c;

    if (Admin) {
        if (DevExt->ChildrenAdmin >= ENG_ADMIN_CHILDREN ||
            DevExt->ChildrenAdmin + 1 >= DevExt->AdminQueue.QueueSize) {
            return NULL;
        }
    } else if (DevExt->ChildrenIo >= DevExt->ChildLimitIo) {
        return NULL;
    }
    for (i = 0; i < ENG_MAX_CHILDREN; i++) {
        c = &DevExt->Children[i];
        if (c->State == ENG_CH_FREE) {
            c->Gen = (USHORT)((c->Gen + 1) & 0x1FF);
            c->Cid = ENG_MAKE_CID(i, c->Gen);
            c->Admin = (UCHAR)Admin;
            c->Parent = ENG_NONE;
            c->PrpPage = ENG_NONE;
            c->BouncePage = ENG_NONE;
            c->BounceCount = 0;
            c->Internal = ENG_INT_NONE;
            c->Bytes = 0;
            c->BufOffset = 0;
            c->FlushGen = 0;
            *Index = i;
            return c;
        }
    }
    return NULL;
}

// Return a child's slot and memory after it was quiesced or released by
// isolation evidence (PLAN 0.10).
static VOID EngChildRelease(IN PHW_DEVICE_EXTENSION DevExt, IN PENG_CHILD c)
{
    if (c->PrpPage != ENG_NONE) {
        EngPrpFree(DevExt, c->PrpPage);
    }
    EngBounceFree(DevExt, c->BouncePage, c->BounceCount);
    if (c->State == ENG_CH_ISSUED) {
        if (c->Admin) {
            DevExt->ChildrenAdmin--;
        } else {
            DevExt->ChildrenIo--;
            if (c->Opcode == NVME_CMD_WRITE && DevExt->OutstandingWrites) {
                DevExt->OutstandingWrites--;
            }
        }
    } else if (c->State == ENG_CH_QUARANTINED) {
        if (c->PrpPage != ENG_NONE) {
            DevExt->PrpQuarantineBitmap &= ~(1UL << c->PrpPage);
        }
        if (c->BouncePage != ENG_NONE) {
            DevExt->BounceQuarantineBitmap &= ~(((1UL << c->BounceCount) - 1) << c->BouncePage);
        }
        if (DevExt->Quarantined) {
            DevExt->Quarantined--;
        }
        if (c->Admin && DevExt->QuarantinedAdmin) {
            DevExt->QuarantinedAdmin--;
        }
    }
    c->State = ENG_CH_FREE;
    c->PrpPage = ENG_NONE;
    c->BouncePage = ENG_NONE;
    c->BounceCount = 0;
    c->Parent = ENG_NONE;
}

// No isolation evidence for this child: keep its slot, CID and pages out of
// every pool for good (MD-1 gone deadline, unresponsive row). The pages stay
// set in the allocation bitmaps, so nothing can hand them out again.
static VOID EngChildQuarantine(IN PHW_DEVICE_EXTENSION DevExt, IN PENG_CHILD c)
{
    if (c->State != ENG_CH_ISSUED) {
        return;
    }
    if (c->Admin) {
        DevExt->ChildrenAdmin--;
    } else {
        DevExt->ChildrenIo--;
        if (c->Opcode == NVME_CMD_WRITE && DevExt->OutstandingWrites) {
            DevExt->OutstandingWrites--;
        }
    }
    if (c->PrpPage != ENG_NONE) {
        DevExt->PrpQuarantineBitmap |= (1UL << c->PrpPage);
    }
    if (c->BouncePage != ENG_NONE) {
        DevExt->BounceQuarantineBitmap |= (((1UL << c->BounceCount) - 1) << c->BouncePage);
    }
    if (c->Admin) {
        // Its completion can still arrive on the admin queue, which is
        // drained while any of these remain (EngProcessCompletions).
        DevExt->QuarantinedAdmin++;
    }
    if (c->Internal == ENG_INT_IDLE_FLUSH) {
        DevExt->IdleFlushOutstanding = FALSE;
    } else if (c->Internal == ENG_INT_STOP_FLUSH) {
        DevExt->StopFlushOutstanding = FALSE;
    }
    c->State = ENG_CH_QUARANTINED;
    DevExt->Quarantined++;
}

// Isolation evidence (PLAN 0.10: acknowledged disable, CM link-down): every
// issued or quarantined child is released and its parent's count dropped.
// Without evidence: every issued child is quarantined, still counted against
// its parent, so the parent stays held (unresponsive row).
VOID EngChildrenSettle(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Evidence)
{
    ULONG i;
    PENG_CHILD c;

    for (i = 0; i < ENG_MAX_CHILDREN; i++) {
        c = &DevExt->Children[i];
        if (c->State == ENG_CH_FREE) {
            continue;
        }
        if (Evidence) {
            if (c->Parent != ENG_NONE && DevExt->Parents[c->Parent].State != ENG_PA_FREE &&
                DevExt->Parents[c->Parent].Outstanding) {
                DevExt->Parents[c->Parent].Outstanding--;
            }
            if (c->Internal == ENG_INT_IDLE_FLUSH) {
                DevExt->IdleFlushOutstanding = FALSE;
            } else if (c->Internal == ENG_INT_STOP_FLUSH) {
                DevExt->StopFlushOutstanding = FALSE;
            }
            EngChildRelease(DevExt, c);
        } else if (c->State == ENG_CH_ISSUED) {
            EngChildQuarantine(DevExt, c);
        }
    }
    if (!Evidence) {
        DBGSTAGE(STG_QUARANTINE, (DevExt->Quarantined & 0xFF) | ((DevExt->PrpQuarantineBitmap & 0xFF) << 8) |
                 ((DevExt->BounceQuarantineBitmap & 0xFF) << 16));
    }
    PlatDmaSettle(DevExt, Evidence);
}

// MD-1 gone deadline for one parent: its children are quarantined and
// detached, so the parent can complete while its DMA memory stays out of use.
VOID EngQuarantineParentChildren(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Index)
{
    ULONG i;
    PENG_CHILD c;

    for (i = 0; i < ENG_MAX_CHILDREN; i++) {
        c = &DevExt->Children[i];
        if (c->Parent != Index || c->State == ENG_CH_FREE) {
            continue;
        }
        if (c->State == ENG_CH_ISSUED) {
            EngChildQuarantine(DevExt, c);
        }
        c->Parent = ENG_NONE;
    }
    DevExt->Parents[Index].Outstanding = 0;
    // The quarantined pages stay out of every pool (bitmaps above); the
    // allocation records stay OWNED (on 9x: the known gap of platform.h).
    PlatDmaSettle(DevExt, FALSE);
}

//
// Parents
//
ULONG EngFreeParents(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i, n = 0;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        if (DevExt->Parents[i].State == ENG_PA_FREE) {
            n++;
        }
    }
    return n;
}

PENG_PARENT EngParentAlloc(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR Kind)
{
    ULONG i;
    PENG_PARENT p;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State != ENG_PA_FREE) {
            continue;
        }
        RtlZeroMemory(p, sizeof(*p));
        p->Srb = Srb;
        p->Kind = Kind;
        p->State = ENG_PA_WAITING;
        p->Target = ENG_NONE;
        p->Seq = ++DevExt->ParentSeq;
        if (DevExt->ReplayAdmitTime != 0) {
            // A held SRB being replayed: the port has timed it since its
            // first admission.
            p->AdmitTime = DevExt->ReplayAdmitTime;
            p->AdmitSafe = DevExt->ReplayAdmitSafe;
            p->Flags |= ENG_PF_REPLAYED;
        } else {
            p->AdmitTime = EngNow(DevExt);
            p->AdmitSafe = EngSafe(DevExt);
        }
        if (EngIsTagged(Srb)) {
            p->Flags |= ENG_PF_TAGGED;
            if (Srb->QueueAction == SRB_ORDERED_QUEUE_TAG_REQUEST) {
                p->Flags |= ENG_PF_BARRIER;
            }
        }
        if (Srb->SrbExtension != NULL) {
            ((PNVME_SRB_EXTENSION)Srb->SrbExtension)->Parent = (UCHAR)i;
        }
        Srb->SrbStatus = SRB_STATUS_PENDING;
        DevExt->ParentsAdmitted++;
        DBGSTAGE(STG_ENG_ADMIT, i | ((ULONG)Kind << 8) | ((ULONG)Srb->Cdb[0] << 16) | ((ULONG)p->Flags << 24));
        return p;
    }
    return NULL;
}

//
// Final status of a parent (PLAN "Parent error mapping", 0.10 parent
// cancellation): a cancelled parent never completes with SUCCESS.
//
static VOID EngParentStatus(IN PHW_DEVICE_EXTENSION DevExt, IN PENG_PARENT p, IN PSCSI_REQUEST_BLOCK Srb)
{
    switch (p->Cancel) {
    case ENG_CANCEL_ABORT:
        Srb->SrbStatus = SRB_STATUS_ABORTED;
        Srb->ScsiStatus = SCSISTAT_GOOD;
        Srb->DataTransferLength = 0;
        return;
    case ENG_CANCEL_RESET:
        Srb->SrbStatus = SRB_STATUS_BUS_RESET;
        Srb->ScsiStatus = SCSISTAT_GOOD;
        Srb->DataTransferLength = 0;
        return;
    case ENG_CANCEL_GONE:
        // Terminal status (PLAN 2.2)
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        Srb->ScsiStatus = SCSISTAT_GOOD;
        Srb->DataTransferLength = 0;
        return;
    default:
        break;
    }
    if (p->Flags & ENG_PF_PRESET) {
        return;
    }
    if (p->ErrSrbStatus != 0) {
        Srb->SrbStatus = p->ErrSrbStatus;
        Srb->ScsiStatus = p->ErrScsiStatus;
        Srb->DataTransferLength = 0;
        if (p->ErrScsiStatus == SCSISTAT_CHECK_CONDITION) {
            ScsiSetSense(DevExt, Srb, p->SenseKey, p->Asc, p->Ascq);
        }
        return;
    }
    Srb->SrbStatus = SRB_STATUS_SUCCESS;
    Srb->ScsiStatus = SCSISTAT_GOOD;
    if (p->Kind == ENG_K_RW) {
        Srb->DataTransferLength = p->Bytes;
    } else if (p->Kind == ENG_K_FLUSH || p->Kind == ENG_K_SHUTDOWN) {
        Srb->DataTransferLength = 0;
    }
}

static VOID EngCompleteAbortsOf(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Target);

// Completes a parent exactly once. Every child it issued must be accounted
// for (Outstanding == 0) unless the caller is the MD-1 deadline, which has
// quarantined them.
VOID EngFinishParent(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Index)
{
    PENG_PARENT p = &DevExt->Parents[Index];
    PSCSI_REQUEST_BLOCK srb;

    if (p->State == ENG_PA_FREE || p->Srb == NULL) {
        DevExt->DoubleBlocked++;
        DBGSTAGE(STG_ENG_DOUBLE, Index | ((ULONG)p->State << 8));
        return;
    }
    srb = p->Srb;
    EngParentStatus(DevExt, p, srb);
    DBGSTAGE(STG_ENG_PARENT_DONE, Index | ((ULONG)(p->Issued & 0xFF) << 8) |
             ((ULONG)srb->SrbStatus << 16) | ((ULONG)p->Cancel << 24));
    if (SRB_STATUS(srb->SrbStatus) != SRB_STATUS_SUCCESS) {
        DBGSTAGE(STG_IO_ERROR, ((ULONG)srb->SrbStatus << 24) | p->NvmeStatus);
    }
    p->Srb = NULL;
    p->State = ENG_PA_FREE;
    DevExt->ParentsCompleted++;
    if ((DevExt->ParentsCompleted & 0xFF) == 0) {
        DBGSTAGE(STG_ENG_STATS, (DevExt->ParentsCompleted & 0xFFFF) | ((DevExt->StaleCqes & 0xFF) << 16) |
                 ((DevExt->DoubleBlocked & 0xFF) << 24));
    }
    EngCompleteSrb(DevExt, srb);
    EngCompleteAbortsOf(DevExt, Index);
}

// An ABORT SRB waiting for its target completes once the target has
// (PLAN 0.10 abort: every child the target issued is accounted for).
static VOID EngCompleteAbortsOf(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Target)
{
    ULONG i;
    PENG_PARENT q;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        q = &DevExt->Parents[i];
        if (q->State == ENG_PA_PARKED && q->Kind == ENG_K_ABORT && q->Target == Target) {
            q->Target = ENG_NONE;
            EngFinishParent(DevExt, i);
        }
    }
}

// Complete a parent that never issued a child with a given status (BUSY,
// fast-fail, ...): no DMA can reach its buffer.
VOID EngFailUnstarted(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Index, IN UCHAR Cancel)
{
    PENG_PARENT p = &DevExt->Parents[Index];

    p->Cancel = Cancel;
    EngFinishParent(DevExt, Index);
}

// Records the first error of a parent; later errors do not change it.
static VOID EngParentError(IN PENG_PARENT p, IN UCHAR SrbStatus, IN UCHAR Key, IN UCHAR Asc, IN UCHAR Ascq,
                           IN USHORT NvmeStatus)
{
    if (p->ErrSrbStatus != 0) {
        return;
    }
    p->ErrSrbStatus = SrbStatus;
    p->ErrScsiStatus = (UCHAR)(Key != 0 ? SCSISTAT_CHECK_CONDITION : SCSISTAT_GOOD);
    p->SenseKey = Key;
    p->Asc = Asc;
    p->Ascq = Ascq;
    p->NvmeStatus = NvmeStatus;
}

// NVMe status (SCT << 8 | SC) to the parent error mapping
static VOID EngMapNvmeError(IN PENG_PARENT p, IN USHORT Status)
{
    UCHAR sct = (UCHAR)((Status >> 8) & 0x7);
    UCHAR sc = (UCHAR)(Status & 0xFF);

    if (sct == 2 && sc == NVME_SC_READ_ERROR) {
        EngParentError(p, SRB_STATUS_ERROR, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00, Status);
    } else if (sct == 2 && sc == NVME_SC_WRITE_FAULT) {
        EngParentError(p, SRB_STATUS_ERROR, SCSI_SENSE_MEDIUM_ERROR, 0x03, 0x00, Status);
    } else if (sct == 0 && sc == NVME_SC_LBA_RANGE) {
        EngParentError(p, SRB_STATUS_ERROR, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00, Status);
    } else if (sct == 0 && sc == NVME_SC_INVALID_OPCODE) {
        EngParentError(p, SRB_STATUS_ERROR, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00, Status);
    } else if (sct == 0 && sc == NVME_SC_INVALID_FIELD) {
        EngParentError(p, SRB_STATUS_ERROR, SCSI_SENSE_ILLEGAL_REQUEST, 0x24, 0x00, Status);
    } else {
        EngParentError(p, SRB_STATUS_ERROR, SCSI_SENSE_HARDWARE_ERROR, 0x00, 0x00, Status);
    }
}

//
// Command submission
//
static BOOLEAN EngSubmitChild(IN PHW_DEVICE_EXTENSION DevExt, IN PENG_CHILD c, IN PNVME_COMMAND Cmd)
{
    BOOLEAN ok;

    Cmd->CDW0.Fields.CommandId = c->Cid;
    Cmd->CDW0.Fields.Opcode = c->Opcode;
    c->CtrlGen = DevExt->CtrlGen;
    c->IssueTime = EngNow(DevExt);
    c->State = ENG_CH_ISSUED;
    if (c->Admin) {
        DevExt->ChildrenAdmin++;
        ok = NvmeSubmitAdminCommand(DevExt, Cmd);
    } else {
        DevExt->ChildrenIo++;
        if (c->Opcode == NVME_CMD_WRITE) {
            // Conservative dirty tracking: dirty from submission (0.10).
            if (DevExt->WriteGen == DevExt->CleanGen) {
                DBGSTAGE(STG_DIRTY, DevExt->WriteGen + 1);
            }
            DevExt->WriteGen++;
            DevExt->OutstandingWrites++;
            DevExt->LastWriteTime = c->IssueTime;
        }
        ok = NvmeSubmitIoCommand(DevExt, Cmd);
    }
    if (!ok) {
        // Queue full: cannot happen while children stay below the queue
        // size; the child was never visible to the controller.
        if (c->Opcode == NVME_CMD_WRITE && !c->Admin) {
            DevExt->WriteGen--;
        }
        EngChildRelease(DevExt, c);
        return FALSE;
    }
    DevExt->ChildrenIssued++;
    DBGSTAGE(STG_ENG_CHILD_SUBMIT, c->Cid | ((ULONG)c->Parent << 16) |
             ((ULONG)((c->BouncePage != ENG_NONE ? 1 : 0) | (c->Admin ? 2 : 0)) << 24));
    return TRUE;
}

//
// PRP construction for a caller buffer (PLAN 0.4). Returns 0 on success,
// a STG_ENG_MAP_FAIL reason if the mapping cannot be used (the caller then
// bounces), or 0xFF if a PRP list page is needed and none is free.
//
// Translation contract. Each PRP entry describes one span: entry 1 from the
// buffer's offset to the end of its page (or to the end of the child), each
// later entry one whole page (or the child's last partial page). Every span
// is translated on its own, starting at its own address, so physical
// contiguity is needed only inside one page, which a page always has. The
// length the port returns is the physically contiguous length from that
// address; it must cover the whole span, or the mapping is refused (reason
// 4) and the child bounced: a shorter length means the port could not vouch
// for the span. NT ports return the contiguous run (at least to the end of
// the page). The Windows 98 SE port returns at most the length to the end
// of the page (engine runs, 2026-10-15), which by construction covers each
// span; were a 9x port ever to return less, the check bounces the child
// rather than trust it.
//
static BOOLEAN EngSpanOk(IN PHYSICAL_ADDRESS Pa, IN ULONG Len, IN ULONG Need)
{
    return (BOOLEAN)(Pa.QuadPart != 0 && Len >= Need);
}

static ULONG EngMapCaller(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN PENG_CHILD c,
                          IN PNVME_COMMAND Cmd)
{
    PUCHAR va = (PUCHAR)Srb->DataBuffer + c->BufOffset;
    PUCHAR callerVa;
    PHYSICAL_ADDRESS pa;
    ULONG len, need, offsetInPage, firstBytes, pages, i;
    PULONGLONG list;

    len = c->Bytes;
    pa = ScsiPortGetPhysicalAddress(DevExt, Srb, va, &len);
    if (len == 0) {
        return 1;
    }
    if (pa.QuadPart == 0) {
        return 3;
    }
    // PRP entry 1: DWORD aligned (bits 1:0 zero); its page offset is the
    // buffer's page offset.
    callerVa = EngCallerVa(Srb, c->BufOffset);
    if (callerVa == NULL || (pa.LowPart & 3) != 0 ||
        (pa.LowPart & (PAGE_SIZE - 1)) != ((ULONG)(ULONG_PTR)callerVa & (PAGE_SIZE - 1))) {
        return 2;
    }
    offsetInPage = pa.LowPart & (PAGE_SIZE - 1);
    firstBytes = PAGE_SIZE - offsetInPage;
    need = (c->Bytes < firstBytes) ? c->Bytes : firstBytes;
    if (!EngSpanOk(pa, len, need)) {
        return 4;
    }
    Cmd->PRP1 = (ULONGLONG)pa.QuadPart;
    if (c->Bytes <= firstBytes) {
        Cmd->PRP2 = 0;
        return 0;
    }
    pages = (offsetInPage + c->Bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 2) {
        need = c->Bytes - firstBytes;
        len = need;
        pa = ScsiPortGetPhysicalAddress(DevExt, Srb, va + firstBytes, &len);
        if (len == 0) {
            return 1;
        }
        if ((pa.LowPart & (PAGE_SIZE - 1)) != 0) {
            return 2;
        }
        if (!EngSpanOk(pa, len, need)) {
            return 4;
        }
        Cmd->PRP2 = (ULONGLONG)pa.QuadPart;
        return 0;
    }
    // PRP list: pages - 1 entries, at most one list page (children are at
    // most STOR98_MAX_TRANSFER, 64 pages on NT and 128 on 9x, below
    // STOR98_PRP_PER_PAGE: checked at compile time in nvme2k.h; the
    // StorMaxTransferKB / maxtransferkb= setting only lowers it).
    if (pages - 1 > STOR98_PRP_PER_PAGE) {
        return 2;
    }
    c->PrpPage = EngPrpAlloc(DevExt);
    if (c->PrpPage == ENG_NONE) {
        return 0xFF;
    }
    list = (PULONGLONG)EngPrpVa(DevExt, c->PrpPage);
    for (i = 1; i < pages; i++) {
        ULONG off = firstBytes + (i - 1) * PAGE_SIZE;
        ULONG why = 0;

        need = c->Bytes - off;
        if (need > PAGE_SIZE) {
            need = PAGE_SIZE;
        }
        len = c->Bytes - off;
        pa = ScsiPortGetPhysicalAddress(DevExt, Srb, va + off, &len);
        if (len == 0) {
            why = 1;
        } else if ((pa.LowPart & (PAGE_SIZE - 1)) != 0) {
            why = 2;
        } else if (!EngSpanOk(pa, len, need)) {
            why = 4;
        }
        if (why != 0) {
            EngPrpFree(DevExt, c->PrpPage);
            c->PrpPage = ENG_NONE;
            return why;
        }
        list[i - 1] = (ULONGLONG)pa.QuadPart;
    }
    Cmd->PRP2 = EngPrpPa(DevExt, c->PrpPage);
    return 0;
}

// PRPs for a bounced child: one entry per bounce page, each translated at
// HwInitialize, so the pages need not be physically contiguous.
// FALSE: no PRP list page free.
static BOOLEAN EngMapBounce(IN PHW_DEVICE_EXTENSION DevExt, IN PENG_CHILD c, IN PNVME_COMMAND Cmd)
{
    ULONG i;
    PULONGLONG list;

    Cmd->PRP1 = EngBouncePa(DevExt, c->BouncePage);
    if (c->BounceCount == 1) {
        Cmd->PRP2 = 0;
    } else if (c->BounceCount == 2) {
        Cmd->PRP2 = EngBouncePa(DevExt, (UCHAR)(c->BouncePage + 1));
    } else {
        c->PrpPage = EngPrpAlloc(DevExt);
        if (c->PrpPage == ENG_NONE) {
            return FALSE;
        }
        list = (PULONGLONG)EngPrpVa(DevExt, c->PrpPage);
        for (i = 1; i < c->BounceCount; i++) {
            list[i - 1] = EngBouncePa(DevExt, (UCHAR)(c->BouncePage + i));
        }
        Cmd->PRP2 = EngPrpPa(DevExt, c->PrpPage);
    }
    return TRUE;
}

// Capacity wait reasons (STG_ENG_CAP_WAIT)
#define ENG_WAIT_NONE       0
#define ENG_WAIT_PARENTS    1
#define ENG_WAIT_CHILDREN   2
#define ENG_WAIT_PRP        3
#define ENG_WAIT_BOUNCE     4
#define ENG_WAIT_BARRIER    5

// Issues the next child of a READ/WRITE parent. Returns ENG_WAIT_NONE if a
// child was issued (or the parent failed), or the capacity it waits for.
static ULONG EngIssueRwChild(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Index)
{
    PENG_PARENT p = &DevExt->Parents[Index];
    PSCSI_REQUEST_BLOCK srb = p->Srb;
    PENG_CHILD c;
    ULONG ci, bytes, blocks, bs = DevExt->NamespaceBlockSize, why;
    NVME_COMMAND cmd;
    BOOLEAN bounce = (BOOLEAN)(DevExt->Tun.ForceBounce != 0);

    c = EngChildAlloc(DevExt, FALSE, &ci);
    if (c == NULL) {
        return ENG_WAIT_CHILDREN;
    }
    bytes = p->BlocksLeft * bs;
    if (p->BlocksLeft > DevExt->MaxChildBytes / bs) {
        bytes = (DevExt->MaxChildBytes / bs) * bs;
    }
    c->Parent = (UCHAR)Index;
    c->Opcode = (UCHAR)((p->Flags & ENG_PF_WRITE) ? NVME_CMD_WRITE : NVME_CMD_READ);
    c->BufOffset = p->NextOffset;
    c->Bytes = bytes;

    RtlZeroMemory(&cmd, sizeof(cmd));
    cmd.NSID = 1;
    cmd.CDW0.Fields.Flags = NVME_CMD_PRP;

    if (!bounce) {
        why = EngMapCaller(DevExt, srb, c, &cmd);
        if (why == 0xFF) {
            c->State = ENG_CH_FREE;
            return ENG_WAIT_PRP;
        }
        if (why != 0) {
            DBGSTAGE(STG_ENG_MAP_FAIL, why | (Index << 8));
            bounce = TRUE;
        }
    }
    if (bounce) {
        ULONG maxBytes = ENG_BOUNCE_PAGES * PAGE_SIZE;
        ULONG pages;

        if (bytes > maxBytes) {
            bytes = (maxBytes / bs) * bs;
            c->Bytes = bytes;
        }
        pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
        c->BouncePage = EngBounceAlloc(DevExt, pages);
        if (c->BouncePage == ENG_NONE) {
            c->State = ENG_CH_FREE;
            return ENG_WAIT_BOUNCE;
        }
        c->BounceCount = (UCHAR)pages;
        if (!EngMapBounce(DevExt, c, &cmd)) {
            EngBounceFree(DevExt, c->BouncePage, c->BounceCount);
            c->BouncePage = ENG_NONE;
            c->State = ENG_CH_FREE;
            return ENG_WAIT_PRP;
        }
        if (c->Opcode == NVME_CMD_WRITE &&
            !EngCallerCopy(DevExt, srb, c->BufOffset, EngBounceVa(DevExt, c->BouncePage), bytes, FALSE)) {
            // The caller's buffer cannot be resolved (9x): fail the parent;
            // children already issued still quiesce first (the dispatcher
            // finishes it once none is outstanding).
            if (c->PrpPage != ENG_NONE) {
                EngPrpFree(DevExt, c->PrpPage);
                c->PrpPage = ENG_NONE;
            }
            EngBounceFree(DevExt, c->BouncePage, c->BounceCount);
            c->BouncePage = ENG_NONE;
            c->BounceCount = 0;
            c->State = ENG_CH_FREE;
            DBGSTAGE(STG_ENG_MAP_FAIL, ENG_MAP_UNRESOLVED | (Index << 8));
            EngParentError(p, SRB_STATUS_ERROR, SCSI_SENSE_HARDWARE_ERROR, 0x44, 0x00, 0);
            p->BlocksLeft = 0;
            return ENG_WAIT_NONE;
        }
        DBGSTAGE(STG_ENG_BOUNCE, c->Cid | (pages << 16) | ((ULONG)(DevExt->Tun.ForceBounce != 0) << 24));
    }

    blocks = bytes / bs;
    cmd.CDW10 = (ULONG)p->NextLba;
    cmd.CDW11 = (ULONG)(p->NextLba >> 32);
    cmd.CDW12 = blocks - 1;
    if (c->Opcode == NVME_CMD_WRITE && (DevExt->WtFua || (p->Flags & ENG_PF_FUA))) {
        // Write-through by FUA (PLAN D3), or a write the OS sent with FUA
        // (the caching page reports a write cache, so the class driver
        // marks write-through I/O that way): durable when it completes
        // (NVM Express Base 1.4c 6.15, CDW12 bit 30), on every child.
        cmd.CDW12 |= (1UL << 30);
        DevExt->WtFuaWrites++;
    } else if (c->Opcode != NVME_CMD_WRITE && (p->Flags & ENG_PF_FUA)) {
        // A READ the OS sent with FUA: read from the medium, not from a
        // cache (NVM Express Read CDW12 bit 30), on every child.
        cmd.CDW12 |= (1UL << 30);
    }
    if (!EngSubmitChild(DevExt, c, &cmd)) {
        return ENG_WAIT_CHILDREN;
    }
    p->Flags |= ENG_PF_STARTED;
    p->Outstanding++;
    p->Issued++;
    p->NextLba += blocks;
    p->NextOffset += bytes;
    p->BlocksLeft -= blocks;
    return ENG_WAIT_NONE;
}

// A Flush child, for a parent (Parent != ENG_NONE) or internal.
static ULONG EngIssueFlushChild(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Parent, IN UCHAR Internal)
{
    PENG_CHILD c;
    ULONG ci;
    NVME_COMMAND cmd;

    c = EngChildAlloc(DevExt, FALSE, &ci);
    if (c == NULL) {
        return ENG_WAIT_CHILDREN;
    }
    c->Parent = (UCHAR)Parent;
    c->Opcode = NVME_CMD_FLUSH;
    c->Internal = Internal;
    // The Flush covers the writes completed before it is submitted. With
    // writes still outstanding it is conservatively taken to cover none of
    // the new ones (0.10 durability: an ordered Flush).
    c->FlushGen = (DevExt->OutstandingWrites == 0) ? DevExt->WriteGen : DevExt->CleanGen;
    RtlZeroMemory(&cmd, sizeof(cmd));
    cmd.NSID = 1;
    if (!EngSubmitChild(DevExt, c, &cmd)) {
        return ENG_WAIT_CHILDREN;
    }
    if (Parent != ENG_NONE) {
        PENG_PARENT p = &DevExt->Parents[Parent];

        p->Flags |= ENG_PF_STARTED;
        p->Outstanding++;
        p->Issued++;
    }
    return ENG_WAIT_NONE;
}

// Internal Flush (idle flush, MD-7 (a); stop path). TRUE if submitted.
BOOLEAN EngSubmitInternalFlush(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR Internal)
{
    return (BOOLEAN)(EngIssueFlushChild(DevExt, ENG_NONE, Internal) == ENG_WAIT_NONE);
}

// Get Log Page into a PRP page (the page is the data buffer).
static ULONG EngIssueLogChild(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Index)
{
    PENG_PARENT p = &DevExt->Parents[Index];
    PENG_CHILD c;
    ULONG ci;
    NVME_COMMAND cmd;
    UCHAR page;

    c = EngChildAlloc(DevExt, TRUE, &ci);
    if (c == NULL) {
        return ENG_WAIT_CHILDREN;
    }
    page = EngPrpAlloc(DevExt);
    if (page == ENG_NONE) {
        c->State = ENG_CH_FREE;
        return ENG_WAIT_PRP;
    }
    RtlZeroMemory(EngPrpVa(DevExt, page), PAGE_SIZE);
    c->Parent = (UCHAR)Index;
    c->Opcode = NVME_ADMIN_GET_LOG_PAGE;
    c->PrpPage = page;
    c->Bytes = 512;
    RtlZeroMemory(&cmd, sizeof(cmd));
    cmd.NSID = 0xFFFFFFFF;
    cmd.PRP1 = EngPrpPa(DevExt, page);
    // NUMDL (31:16) = dwords - 1 for 512 bytes, LID (7:0)
    cmd.CDW10 = (ULONG)p->LogId | (((512 / 4) - 1) << 16);
    if (!EngSubmitChild(DevExt, c, &cmd)) {
        return ENG_WAIT_CHILDREN;
    }
    p->Flags |= ENG_PF_STARTED;
    p->Outstanding++;
    p->Issued++;
    return ENG_WAIT_NONE;
}

//
// Dispatch (0.3): parents in admission order; strict FIFO, so a parent that
// waits for capacity holds back the ones behind it. ORDERED and SHUTDOWN
// parents are barriers: they start only when every earlier parent has
// completed, and every later parent waits for them. A FLUSH waits for every
// earlier write (EngEarlierWrites).
//
static BOOLEAN EngEarlierBusy(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Seq, IN BOOLEAN BarriersOnly)
{
    ULONG i;
    PENG_PARENT q;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        q = &DevExt->Parents[i];
        if (q->State == ENG_PA_FREE || q->State == ENG_PA_PARKED) {
            continue;
        }
        if ((LONG)(q->Seq - Seq) >= 0) {
            continue;
        }
        if (!BarriersOnly || (q->Flags & ENG_PF_BARRIER)) {
            return TRUE;
        }
    }
    return FALSE;
}

// A FLUSH (SYNCHRONIZE CACHE, SRB_FUNCTION_FLUSH) is a barrier for the
// writes before it whatever its queue tag (PLAN 0.11 class 1: "NVMe Flush
// after the earlier writes"; NVMe Flush covers only commands completed
// before it): its Flush is submitted only when every earlier write parent,
// with all of its children, has completed. Later parents wait behind it in
// admission order.
static BOOLEAN EngEarlierWrites(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Seq)
{
    ULONG i;
    PENG_PARENT q;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        q = &DevExt->Parents[i];
        if (q->State == ENG_PA_FREE || q->State == ENG_PA_PARKED || !(q->Flags & ENG_PF_WRITE)) {
            continue;
        }
        if ((LONG)(q->Seq - Seq) < 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static LONG EngNextWaiting(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;
    LONG best = -1;
    PENG_PARENT p;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State != ENG_PA_WAITING) {
            continue;
        }
        if (best < 0 || (LONG)(p->Seq - DevExt->Parents[best].Seq) < 0) {
            best = (LONG)i;
        }
    }
    return best;
}

BOOLEAN EngParentsWaiting(IN PHW_DEVICE_EXTENSION DevExt)
{
    return (BOOLEAN)(EngNextWaiting(DevExt) >= 0);
}

//
// SoftwareOnly (review round 3, N3): dispatch only the leading run of held
// requests that complete in software without starting hardware work
// (ScsiCompletesInSoftware), in order, and stop at the first other one. The
// timer runs this before its hold check, so a request such as INQUIRY held
// through a bring-up that has just reached `running` is answered rather than
// completed BUSY; anything that would start DMA is dispatched only after the
// hold check, so one whose hold bound has passed still gets BUSY before any
// child is issued (hold-then-BUSY, PLAN 0.3).
//
static VOID EngDispatchEx(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN SoftwareOnly)
{
    LONG idx;
    PENG_PARENT p;
    ULONG why = ENG_WAIT_NONE;
    PSCSI_REQUEST_BLOCK srb;

    if (DevExt->InDispatch) {
        return;
    }
    DevExt->InDispatch = TRUE;
    // A 9x removal transaction past its drain dispatches nothing more
    // (platform.h, PlatCmRemoveGate): undispatched work stays frozen.
    while (DevExt->State == ENG_ST_RUNNING && !(PlatCmRemoveGate(DevExt) & PLAT_RM_GATE_DISPATCH)) {
        idx = EngNextWaiting(DevExt);
        if (idx < 0) {
            break;
        }
        p = &DevExt->Parents[idx];
        if (SoftwareOnly && (p->Kind != ENG_K_HELD || p->Srb == NULL || !ScsiCompletesInSoftware(p->Srb))) {
            break;
        }
        if (EngEarlierBusy(DevExt, p->Seq, (BOOLEAN)!(p->Flags & ENG_PF_BARRIER))) {
            why = ENG_WAIT_BARRIER;
            break;
        }
        switch (p->Kind) {
        case ENG_K_HELD:
            // Held while not running: replay through the normal dispatch.
            // The slot is freed first; the replay may admit it again with
            // the original admission time.
            srb = p->Srb;
            DBGSTAGE(STG_SRB_DISPATCH_HELD, (EngMsSince(DevExt, p->AdmitTime) << 8) | srb->Cdb[0]);
            DevExt->ReplayAdmitTime = p->AdmitTime;
            DevExt->ReplayAdmitSafe = p->AdmitSafe;
            if (DevExt->ReplayAdmitTime == 0) {
                DevExt->ReplayAdmitTime = 1;
            }
            p->Srb = NULL;
            p->State = ENG_PA_FREE;
            DevExt->ParentsCompleted++;     // the held parent ends; the replay may admit a new one
            ScsiDispatch(DevExt, srb);
            DevExt->ReplayAdmitTime = 0;
            continue;
        case ENG_K_RW:
            while (p->State == ENG_PA_WAITING && p->BlocksLeft > 0 && p->ErrSrbStatus == 0 &&
                   p->Cancel == ENG_CANCEL_NONE) {
                why = EngIssueRwChild(DevExt, (ULONG)idx);
                if (why != ENG_WAIT_NONE) {
                    break;
                }
            }
            if (why != ENG_WAIT_NONE) {
                break;
            }
            if (p->ErrSrbStatus != 0 && p->Outstanding == 0) {
                // Failed before any child was outstanding (EngIssueRwChild).
                EngFinishParent(DevExt, (ULONG)idx);
                continue;
            }
            p->State = ENG_PA_ISSUED;
            continue;
        case ENG_K_FLUSH:
            if (EngEarlierWrites(DevExt, p->Seq)) {
                why = ENG_WAIT_BARRIER;
                break;
            }
            why = EngIssueFlushChild(DevExt, (ULONG)idx, ENG_INT_NONE);
            if (why == ENG_WAIT_NONE) {
                p->State = ENG_PA_ISSUED;
            }
            break;
        case ENG_K_SHUTDOWN:
            why = EngIssueFlushChild(DevExt, (ULONG)idx, ENG_INT_NONE);
            if (why == ENG_WAIT_NONE) {
                p->State = ENG_PA_ISSUED;
            }
            break;
        case ENG_K_LOG:
            why = EngIssueLogChild(DevExt, (ULONG)idx);
            if (why == ENG_WAIT_NONE) {
                p->State = ENG_PA_ISSUED;
            }
            break;
        default:
            p->State = ENG_PA_ISSUED;
            break;
        }
        if (why != ENG_WAIT_NONE) {
            break;
        }
        // A barrier that just started holds back everything after it.
        if (p->Flags & ENG_PF_BARRIER) {
            break;
        }
    }
    if (why != ENG_WAIT_NONE && !DevExt->CapWaitLogged) {
        DevExt->CapWaitLogged = TRUE;
        DBGSTAGE(STG_ENG_CAP_WAIT, why | ((ULONG)(ENG_MAX_PARENTS - EngFreeParents(DevExt)) << 8));
    }
    DevExt->InDispatch = FALSE;
}

VOID EngDispatch(IN PHW_DEVICE_EXTENSION DevExt)
{
    EngDispatchEx(DevExt, FALSE);
}

//
// Admission: true if the port may send another request (NextRequest).
//
static BOOLEAN EngCanAccept(IN PHW_DEVICE_EXTENSION DevExt)
{
    if (DevExt->State == ENG_ST_STOPPED || DevExt->State == ENG_ST_STOPPING) {
        return FALSE;
    }
    return (BOOLEAN)(EngFreeParents(DevExt) > 0 && !EngParentsWaiting(DevExt));
}

//
// Readiness notifications (0.3). Two separate debts, each paid once its rule
// allows it:
//
// - NextRequest (NextOwed): the adapter can take another request. Withheld
//   while EngCanAccept is false; given when capacity returns.
// - NextLuRequest (NextLuOwed): LU 0 can take another request. Owed after a
//   tagged SRB (admitted or completed): without it the port keeps the LU's
//   other tagged requests until some unrelated completion (seen in gone).
//   It is never given while an untagged SRB of the LU is still active in
//   the engine (admitted, held or parked, not yet completed): an untagged
//   request owns its LU until it completes, and the port starts the LU's
//   next request itself at that completion (SCSI Verifier checks this). Nor
//   is it given while NextRequest is withheld.
//
static BOOLEAN EngUntaggedActive(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;
    PENG_PARENT p;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State != ENG_PA_FREE && p->Srb != NULL && !(p->Flags & ENG_PF_TAGGED)) {
            return TRUE;
        }
    }
    return FALSE;
}

static VOID EngNotifyReady(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Deferred)
{
    if (DevExt->NextOwed) {
        if (!EngCanAccept(DevExt)) {
            return;
        }
        DevExt->NextOwed = FALSE;
        if (Deferred) {
            DevExt->CapWaitLogged = FALSE;
            DBGSTAGE(STG_ENG_CAP_RESUME, EngFreeParents(DevExt) |
                     ((DevExt->ChildLimitIo - DevExt->ChildrenIo) << 8));
        }
        ScsiPortNotification(NextRequest, DevExt, NULL);
    }
    if (DevExt->NextLuOwed) {
        if (EngUntaggedActive(DevExt)) {
            return;
        }
        DevExt->NextLuOwed = FALSE;
        ScsiPortNotification(NextLuRequest, DevExt, 0, 0, 0);
    }
}

// Dispatch what can go, then pay whatever readiness notification is owed
// and now allowed.
VOID EngKick(IN PHW_DEVICE_EXTENSION DevExt)
{
    EngDispatch(DevExt);
    EngNotifyReady(DevExt, TRUE);
}

// The timer's dispatch before its hold check: software-only replays (N3).
VOID EngKickSoftware(IN PHW_DEVICE_EXTENSION DevExt)
{
    EngDispatchEx(DevExt, TRUE);
    EngNotifyReady(DevExt, TRUE);
}

// End of HwStartIo: give or withhold the readiness notifications. Tagged:
// the SRB was tagged; worked out by the caller before handling it, since a
// completed SRB is not read again.
VOID EngStartIoTail(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Tagged)
{
    if (DevExt->State == ENG_ST_STOPPED || DevExt->State == ENG_ST_STOPPING) {
        return;
    }
    DevExt->NextOwed = TRUE;
    if (Tagged) {
        DevExt->NextLuOwed = TRUE;
    }
    EngNotifyReady(DevExt, FALSE);
}

//
// Request submission from the SCSI handlers. Each returns TRUE; the SRB is
// either admitted (SrbStatus PENDING) or completed.
//
static BOOLEAN EngAdmitOrBusy(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR Kind,
                              OUT PENG_PARENT *Parent)
{
    *Parent = EngParentAlloc(DevExt, Srb, Kind);
    if (*Parent == NULL) {
        // No parent slot: only possible if the port sent a request while
        // NextRequest was withheld. Nothing was issued, so BUSY is safe.
        EngBusyOrFail(DevExt, Srb);
        EngCompleteSrb(DevExt, Srb);
        return FALSE;
    }
    return TRUE;
}

BOOLEAN EngSubmitReadWrite(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    ULONGLONG lba = 0;
    ULONG blocks = 0;
    BOOLEAN isWrite = FALSE;
    BOOLEAN fua = FALSE;
    ULONG bs = DevExt->NamespaceBlockSize;
    ULONGLONG bytes;
    PENG_PARENT p;

    switch (cdb->CDB10.OperationCode) {
    case SCSIOP_READ6:
    case SCSIOP_WRITE6:
        lba = ((ULONG)(cdb->CDB6READWRITE.LogicalBlockMsb1 & 0x1F) << 16) |
              ((ULONG)(cdb->CDB6READWRITE.LogicalBlockMsb0) << 8) |
              ((ULONG)(cdb->CDB6READWRITE.LogicalBlockLsb));
        blocks = cdb->CDB6READWRITE.TransferBlocks;
        if (blocks == 0) {
            blocks = 256;   // READ/WRITE(6): 0 means 256 blocks (1.2)
        }
        isWrite = (BOOLEAN)(cdb->CDB6READWRITE.OperationCode == SCSIOP_WRITE6);
        break;
    case SCSIOP_READ:
    case SCSIOP_WRITE:
        lba = BSWAP(*((PULONG)&cdb->CDB10.LogicalBlockByte0));
        blocks = ((ULONG)cdb->CDB10.TransferBlocksMsb << 8) | ((ULONG)cdb->CDB10.TransferBlocksLsb);
        isWrite = (BOOLEAN)(cdb->CDB10.OperationCode == SCSIOP_WRITE);
        fua = (BOOLEAN)((Srb->Cdb[1] & 0x08) != 0);
        break;
#define cdb16 ((PCDB16)cdb)
    case SCSIOP_READ16:
    case SCSIOP_WRITE16:
        BSWAPCopy((PULARGE_INTEGER)&lba, (PULARGE_INTEGER)cdb16->LogicalBlock);
        blocks = BSWAP(*((PULONG)cdb16->TransferLength));
        isWrite = (BOOLEAN)(cdb16->OperationCode == SCSIOP_WRITE16);
        fua = (BOOLEAN)((Srb->Cdb[1] & 0x08) != 0);
        break;
#undef cdb16
    }

    // (10)/(16) with a count of 0 transfer nothing (1.2, 1.3).
    if (blocks == 0) {
        Srb->DataTransferLength = 0;
        return ScsiSuccess(DevExt, Srb);
    }
    // Checked arithmetic and namespace bounds (1.3).
    bytes = (ULONGLONG)blocks * bs;
    if (bytes > Srb->DataTransferLength || bs == 0) {
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
        ScsiSetSense(DevExt, Srb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
        return ScsiError(DevExt, Srb, SRB_STATUS_ERROR);
    }
    if (lba >= DevExt->NamespaceSizeInBlocks || (ULONGLONG)blocks > DevExt->NamespaceSizeInBlocks - lba) {
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
        ScsiSetSense(DevExt, Srb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_ILLEGAL_BLOCK, 0);
        return ScsiError(DevExt, Srb, SRB_STATUS_ERROR);
    }

    if (!EngAdmitOrBusy(DevExt, Srb, ENG_K_RW, &p)) {
        return TRUE;
    }
    if (isWrite) {
        p->Flags |= ENG_PF_WRITE;
    }
    // FUA (byte 1 bit 3 of READ/WRITE(10)/(16); the 6-byte forms have
    // none). MODE SENSE reports a write cache and DPOFUA, so the class
    // driver sets it on write-through I/O, and the OS takes the write as
    // durable when it completes; on a READ it asks for the data from the
    // medium. Every child of this parent carries NVMe FUA (EngIssueRwChild).
    if (fua) {
        p->Flags |= ENG_PF_FUA;
    }
    p->NextLba = lba;
    p->BlocksLeft = blocks;
    p->NextOffset = 0;
    p->Bytes = (ULONG)bytes;
    EngDispatch(DevExt);
    EngTimerEnsure(DevExt, ENG_TICK_WATCH_US);
    return TRUE;
}

BOOLEAN EngSubmitFlush(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PENG_PARENT p;
    UCHAR kind = (UCHAR)(Srb->Function == SRB_FUNCTION_SHUTDOWN ? ENG_K_SHUTDOWN : ENG_K_FLUSH);

#ifdef STOR98_DBGPORT
    // Test builds (shnflush=1): SRB_FUNCTION_FLUSH runs the restartable
    // shutdown, so a user-mode FlushFileBuffers can drive that path.
    if (Srb->Function == SRB_FUNCTION_FLUSH && DevExt->Tun.ShnFlush) {
        kind = ENG_K_SHUTDOWN;
    }
#endif

    if (!EngAdmitOrBusy(DevExt, Srb, kind, &p)) {
        return TRUE;
    }
    if (kind == ENG_K_SHUTDOWN) {
        // Class 2 shutdown notification: after every earlier request.
        p->Flags |= ENG_PF_BARRIER;
    }
    EngDispatch(DevExt);
    EngTimerEnsure(DevExt, ENG_TICK_WATCH_US);
    return TRUE;
}

BOOLEAN EngSubmitLogPage(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR LogId)
{
    PENG_PARENT p;

    if (!EngAdmitOrBusy(DevExt, Srb, ENG_K_LOG, &p)) {
        return TRUE;
    }
    p->LogId = LogId;
    EngDispatch(DevExt);
    EngTimerEnsure(DevExt, ENG_TICK_WATCH_US);
    return TRUE;
}

// Admits any SRB as held (not running: starting, re-enabling, resetting,
// shutdown in progress). No child is issued for it until it is replayed.
BOOLEAN EngHold(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PENG_PARENT p = EngParentAlloc(DevExt, Srb, ENG_K_HELD);

    if (p == NULL) {
        DBGSTAGE(STG_SRB_BUSY_STARTING, STG_SRB_ARG(Srb, DevExt->State));
        EngBusyOrFail(DevExt, Srb);
        EngCompleteSrb(DevExt, Srb);
        return TRUE;
    }
    DBGSTAGE(STG_SRB_HELD, (STG_SRB_ARG(Srb, DevExt->State) & 0x00FFFFFF) | ((ULONG)(Srb->TimeOutValue & 0xFF) << 24));
    return TRUE;
}

//
// Child completion (exactly once)
//
VOID EngChildCompleted(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Admin, IN USHORT Cid, IN USHORT Status, IN ULONG Dw0)
{
    ULONG idx = ENG_CID_INDEX(Cid);
    PENG_CHILD c = &DevExt->Children[idx];
    PENG_PARENT p = NULL;
    ULONG reason = 0;
    BOOLEAN ok = (BOOLEAN)((Status & 0x7FF) == 0);
    BOOLEAN copyFailed;

    UNREFERENCED_PARAMETER(Dw0);

    if (!(Cid & ENG_CID_FLAG)) {
        reason = 4;
    } else if (c->Gen != ENG_CID_GEN(Cid) || (BOOLEAN)c->Admin != Admin) {
        reason = 2;
    } else if (c->State == ENG_CH_QUARANTINED) {
        // A late completion (unresponsive row: the controller recovered):
        // its own completion consumed is command-level quiescence (0.10),
        // so its memory may return to the pools. A child whose parent was
        // completed by the MD-1 deadline has no parent any more; one whose
        // parent is still held (failed reset) is accounted for below like
        // any completion, so the parent's count drops and it can complete.
        DBGSTAGE(STG_ENG_STALE_CQE, Cid | (5UL << 16) | ((ULONG)c->Parent << 24));
        DevExt->LateCqes++;
        if (c->Parent == ENG_NONE || DevExt->Parents[c->Parent].State == ENG_PA_FREE) {
            EngChildRelease(DevExt, c);
            return;
        }
    } else if (c->State != ENG_CH_ISSUED) {
        reason = 1;
    } else if (c->CtrlGen != DevExt->CtrlGen) {
        reason = 3;
    }
    if (reason != 0) {
        DevExt->StaleCqes++;
        DBGSTAGE(STG_ENG_STALE_CQE, Cid | (reason << 16));
        return;
    }
    DBGSTAGE(STG_ENG_CHILD_DONE, Cid | ((ULONG)(Status & 0xFF) << 16) | ((ULONG)c->Parent << 24));

    if (c->Parent != ENG_NONE) {
        p = &DevExt->Parents[c->Parent];
    }

    // Data copies first, while everything is still owned.
    copyFailed = FALSE;
    if (p != NULL && p->Srb != NULL && ok && c->BouncePage != ENG_NONE && c->Opcode == NVME_CMD_READ &&
        !EngCallerCopy(DevExt, p->Srb, c->BufOffset, EngBounceVa(DevExt, c->BouncePage), c->Bytes, TRUE)) {
        DBGSTAGE(STG_ENG_MAP_FAIL, ENG_MAP_UNRESOLVED | ((ULONG)c->Parent << 8));
        copyFailed = TRUE;
    }
    if (p != NULL && p->Srb != NULL && p->Kind == ENG_K_LOG && c->PrpPage != ENG_NONE &&
        p->Cancel == ENG_CANCEL_NONE) {
        NvmeCompleteLogPage(DevExt, p->Srb, (USHORT)(Status & 0xFF), EngPrpVa(DevExt, c->PrpPage));
        p->Flags |= ENG_PF_PRESET;
    }

    // Durability tracking (0.10): a successful ordered Flush.
    if (c->Opcode == NVME_CMD_FLUSH && !c->Admin) {
        if (ok && (LONG)(c->FlushGen - DevExt->CleanGen) > 0) {
            DevExt->CleanGen = c->FlushGen;
            if (DevExt->CleanGen == DevExt->WriteGen) {
                DBGSTAGE(STG_CLEAN, DevExt->CleanGen);
            }
        }
        if (c->Internal == ENG_INT_IDLE_FLUSH) {
            DevExt->IdleFlushOutstanding = FALSE;
        } else if (c->Internal == ENG_INT_STOP_FLUSH) {
            DevExt->StopFlushOutstanding = FALSE;
            DevExt->StopFlushStatus = Status;
        }
    }

    EngChildRelease(DevExt, c);

    if (p == NULL || p->State == ENG_PA_FREE) {
        return;
    }
    p->Outstanding--;
    if (copyFailed && p->Kind != ENG_K_SHUTDOWN) {
        EngParentError(p, SRB_STATUS_ERROR, SCSI_SENSE_HARDWARE_ERROR, 0x44, 0x00, 0);
        p->BlocksLeft = 0;
    }
    if (!ok && !(p->Flags & ENG_PF_PRESET)) {
        if (p->Kind == ENG_K_SHUTDOWN) {
            // The shutdown proceeds; the failed Flush is logged and the
            // adapter stays dirty.
            DBGSTAGE(STG_IO_ERROR, ((ULONG)SRB_STATUS_ERROR << 24) | Status);
        } else {
            EngMapNvmeError(p, Status);
            // First error wins: no new children (parent error mapping).
            p->BlocksLeft = 0;
        }
    }
    if (p->Outstanding != 0) {
        return;
    }
    if (p->Kind == ENG_K_SHUTDOWN) {
        // Flush to CC.SHN only from `running` (PLAN 0.11 class 2). A
        // recovery that took over meanwhile (reset, quarantine, gone) is
        // authoritative: its state stands, the shutdown work it suppressed
        // is not resumed, and the SHUTDOWN SRB completes with the
        // recovery's status (review round 2: a late Flush used to switch a
        // resetting adapter to `shutdown`, abandoning the reset and
        // stranding its parked reset SRB).
        if (p->Cancel == ENG_CANCEL_NONE && DevExt->State == ENG_ST_RUNNING) {
            EngShutdownFlushed(DevExt, (ULONG)(p - DevExt->Parents), Status);
            return;
        }
        if (p->Cancel == ENG_CANCEL_NONE) {
            p->Cancel = (UCHAR)(DevExt->State == ENG_ST_GONE ? ENG_CANCEL_GONE : ENG_CANCEL_RESET);
        }
        DBGSTAGE(STG_SHN_SUPPRESSED, (ULONG)(p - DevExt->Parents) | ((ULONG)p->Cancel << 8) |
                 ((ULONG)DevExt->State << 16));
        EngFinishParent(DevExt, (ULONG)(p - DevExt->Parents));
        return;
    }
    if (p->BlocksLeft != 0 && p->Cancel == ENG_CANCEL_NONE && p->ErrSrbStatus == 0) {
        // More fragments to dispatch.
        p->State = ENG_PA_WAITING;
        return;
    }
    EngFinishParent(DevExt, (ULONG)(p - DevExt->Parents));
}

//
// Cancellation (PLAN 0.10 "Parent cancellation", H1): stop dispatch at once;
// what was issued still quiesces before the parent completes. A parent all
// of whose work was already dispatched is not marked cancelled.
//
VOID EngCancelParent(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Index, IN UCHAR Cause)
{
    PENG_PARENT p = &DevExt->Parents[Index];

    if (p->State == ENG_PA_FREE) {
        return;
    }
    // Required work still to come: fragments not dispatched, a held SRB,
    // or a SHUTDOWN whose CC.SHN step has not been taken.
    if (p->State == ENG_PA_WAITING || p->BlocksLeft != 0 || p->Kind == ENG_K_HELD ||
        (p->Kind == ENG_K_SHUTDOWN && p->State != ENG_PA_SHN)) {
        p->Cancel = Cause;
        p->BlocksLeft = 0;
    }
    if (p->State == ENG_PA_WAITING) {
        p->State = ENG_PA_ISSUED;
    }
    DBGSTAGE(STG_ENG_CANCEL, Index | ((ULONG)Cause << 8) | ((ULONG)p->Outstanding << 16));
    if (p->Outstanding == 0) {
        if (p->Cancel == ENG_CANCEL_NONE) {
            p->Cancel = Cause;
        }
        EngFinishParent(DevExt, Index);
    }
}
