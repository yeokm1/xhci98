/*
 * hcd_pool.c - the only pool and MDL calls in xhci98.sys (design record 13
 * section 7.5, the pool rule for the HCD).
 *
 * The pair is ExAllocatePoolWithTag + ExFreePool, never the untagged
 * ExAllocatePool and never ExFreePoolWithTag (no Windows 98 evidence for
 * either). NonPagedPool only. The import gate's SITES= rows hold every one of
 * these imports to this object file, so a call anywhere else fails the
 * build. The outstanding count is what XHCISNAP's door reports and what reads
 * 0 after the controller's remove (rule 6).
 *
 * IRQL: <= DISPATCH_LEVEL, never in the ISR or a KeSynchronizeExecution
 * callback (rule 2).
 */

#include "hcd.h"

/* 'X98H' in a pool dump on Windows 2000: the bytes are stored low first. */
#define HCD_POOL_TAG 0x48383958UL

static LONG HcdPoolOutstanding;

PVOID HcdPoolAlloc(ULONG bytes)
{
    PVOID p;

    p = ExAllocatePoolWithTag(NonPagedPool, bytes, HCD_POOL_TAG);
    if (p != NULL) {
        (VOID)InterlockedIncrement(&HcdPoolOutstanding);
    }
    return p;
}

VOID HcdPoolFree(PVOID p)
{
    if (p != NULL) {
        ExFreePool(p);
        (VOID)InterlockedDecrement(&HcdPoolOutstanding);
    }
}

ULONG HcdPoolOutstandingCount(VOID)
{
    return (ULONG)HcdPoolOutstanding;
}

/*
 * Pool handed to the PnP manager, which frees it itself with ExFreePool: the
 * id strings and device text of IRP_MN_QUERY_ID / QUERY_DEVICE_TEXT and the
 * DEVICE_RELATIONS of QUERY_DEVICE_RELATIONS. Counted apart from the
 * driver's own allocations, which must read 0 after a remove (rule 6) -
 * these leave the driver's ownership the moment they are returned.
 */
static LONG HcdPoolHandedOff;

PVOID HcdPoolAllocHandedOff(ULONG bytes)
{
    PVOID p;

    p = ExAllocatePoolWithTag(NonPagedPool, bytes, HCD_POOL_TAG);
    if (p != NULL) {
        (VOID)InterlockedIncrement(&HcdPoolHandedOff);
    }
    return p;
}

/* A relations list a lower or earlier driver allocated, which this driver
 * replaces with a longer one. */
VOID HcdPoolFreeForeign(PVOID p)
{
    if (p != NULL) {
        ExFreePool(p);
    }
}

/*
 * The MDL for a URB that carries only a virtual TransferBuffer (design record
 * 13 section 11.3): the USB driver interface promises the buffer is
 * non-paged, so MmBuildMdlForNonPagedPool describes it with no probe and no
 * lock. Counted with the allocations, since it is this driver's until
 * HcdPoolMdlFree. NULL when the system has none. IRQL: <= DISPATCH_LEVEL.
 */
PMDL HcdPoolMdlBuild(PVOID va, ULONG bytes)
{
    PMDL mdl;

    mdl = IoAllocateMdl(va, bytes, FALSE, FALSE, NULL);
    if (mdl != NULL) {
        MmBuildMdlForNonPagedPool(mdl);
        (VOID)InterlockedIncrement(&HcdPoolOutstanding);
    }
    return mdl;
}

VOID HcdPoolMdlFree(PMDL mdl)
{
    if (mdl != NULL) {
        IoFreeMdl(mdl);
        (VOID)InterlockedDecrement(&HcdPoolOutstanding);
    }
}
