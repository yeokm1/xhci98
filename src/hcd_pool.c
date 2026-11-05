/*
 * hcd_pool.c - the only pool and MDL calls in xhci98.sys (design record 13
 * section 7.5, the pool rule for the HCD).
 *
 * The pair is ExAllocatePoolWithTag + ExFreePool, never the untagged
 * ExAllocatePool and never ExFreePoolWithTag (no Windows 98 evidence for
 * either). Nonpaged pool only (HcdPoolInit's type), but for the
 * requirements list (design record 18 section 4.2), which that IRP's
 * contract puts in PagedPool. The import
 * gate's SITES= rows hold every one of
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

/*
 * The nonpaged pool type: NonPagedPoolNx (512), Windows 8's no-execute
 * nonpaged pool, which the Windows 2000 DDK and WDK 7.1 predate, from
 * NT 6.2 on, so Driver Verifier's code integrity checks, and the memory
 * integrity they test for, find no executable pool (roadmap-usb4 task
 * 45.63); NonPagedPool, the only nonpaged type they have, on Windows 98,
 * ME and 2000 to 7. NT is told by IoIsWdmVersionAvailable(1, 0x10)
 * before the kernel's shared data page is read, as cm_filt.c reads it:
 * Windows 98 and ME have no such page.
 */
#define HCD_POOL_NX ((POOL_TYPE)512)

static POOL_TYPE HcdPoolNonPaged = NonPagedPool;

/* DriverEntry, before the first allocation. PASSIVE_LEVEL. */
VOID HcdPoolInit(VOID)
{
    ULONG major;
    ULONG minor;

    if (!IoIsWdmVersionAvailable(1, 0x10)) {
        return;
    }
    major = SharedUserData->NtMajorVersion;
    minor = SharedUserData->NtMinorVersion;
    if (major > 6 || (major == 6 && minor >= 2)) {
        HcdPoolNonPaged = HCD_POOL_NX;
    }
}

PVOID HcdPoolAlloc(ULONG bytes)
{
    PVOID p;

    p = ExAllocatePoolWithTag(HcdPoolNonPaged, bytes, HCD_POOL_TAG);
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

    p = ExAllocatePoolWithTag(HcdPoolNonPaged, bytes, HCD_POOL_TAG);
    if (p != NULL) {
        (VOID)InterlockedIncrement(&HcdPoolHandedOff);
    }
    return p;
}

/*
 * The replacement IO_RESOURCE_REQUIREMENTS_LIST of IRP_MN_FILTER_RESOURCE_
 * REQUIREMENTS (design record 18 section 4.2): the one PagedPool allocation
 * in the HCD, as that IRP's contract asks, handed to the PnP manager, which
 * frees it. The owner's exception to rule 1 (2026-10-09), confined to this
 * site and that IRP. IRQL: PASSIVE_LEVEL.
 */
PVOID HcdPoolAllocRequirements(ULONG bytes)
{
    PVOID p;

    p = ExAllocatePoolWithTag(PagedPool, bytes, HCD_POOL_TAG);
    if (p != NULL) {
        (VOID)InterlockedIncrement(&HcdPoolHandedOff);
    }
    return p;
}

/*
 * A URB a Windows 8 client allocates through GUID_USBD_INTERFACE_V1's
 * allocators (task 43.29; design record 13 sections 6.13.2 and 7.5 rule 3):
 * the client's memory, allocated and freed on the client's own calls, so
 * counted apart from the driver's own allocations, which must read 0 after
 * a remove (rule 6). Tagged 'X98U', not 'X98H', so a client's leak is not
 * read as the bus's. IRQL: <= DISPATCH_LEVEL.
 */
#define HCD_POOL_CLIENT_TAG 0x55383958UL

static LONG HcdPoolClientOutstanding;

PVOID HcdPoolAllocClient(ULONG bytes)
{
    PVOID p;

    p = ExAllocatePoolWithTag(HcdPoolNonPaged, bytes, HCD_POOL_CLIENT_TAG);
    if (p != NULL) {
        (VOID)InterlockedIncrement(&HcdPoolClientOutstanding);
    }
    return p;
}

VOID HcdPoolFreeClient(PVOID p)
{
    if (p != NULL) {
        ExFreePool(p);
        (VOID)InterlockedDecrement(&HcdPoolClientOutstanding);
    }
}

/* The firmware tables request's set (task 46.21; design record 13
 * sections 7.5 and 8.13): allocated on the caller's thread when asked,
 * freed after its last part, at a release or at the controller's stop;
 * the project tag, counted with the rest. IRQL: <= DISPATCH_LEVEL. */
PVOID HcdPoolAllocAcpi(ULONG bytes)
{
    return HcdPoolAlloc(bytes);
}

VOID HcdPoolFreeAcpi(PVOID p)
{
    HcdPoolFree(p);
}

ULONG HcdPoolClientCount(VOID)
{
    return (ULONG)HcdPoolClientOutstanding;
}

/* A relations list a lower or earlier driver allocated, which this driver
 * replaces with a longer one; and a requirements list the filter
 * replaces. */
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
