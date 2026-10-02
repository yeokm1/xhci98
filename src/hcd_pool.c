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
