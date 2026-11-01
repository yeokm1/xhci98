/*
 * uas_mem.c - every pool, MDL and IRP-memory call xhciuas.sys makes, in one
 * object, so the import allowlist can hold those pairs to it
 * (scripts\import-gate\xhciuas-imports.allow, SITES=uas_mem.obj; the HCD's
 * rule of design record 13 section 7.5 applied to the second binary).
 *
 * IRQL: <= DISPATCH_LEVEL throughout (nonpaged pool only), but for
 * UasLockBuffer, PASSIVE_LEVEL.
 */

#include "uas.h"

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
#define UAS_POOL_NX ((POOL_TYPE)512)

static POOL_TYPE UasPoolNonPaged = NonPagedPool;

/* DriverEntry, before the first allocation. PASSIVE_LEVEL. */
VOID UasPoolInit(VOID)
{
    ULONG major;
    ULONG minor;

    if (!IoIsWdmVersionAvailable(1, 0x10)) {
        return;
    }
    major = SharedUserData->NtMajorVersion;
    minor = SharedUserData->NtMinorVersion;
    if (major > 6 || (major == 6 && minor >= 2)) {
        UasPoolNonPaged = UAS_POOL_NX;
    }
}

PVOID UasAlloc(ULONG bytes)
{
    PVOID p;

    p = ExAllocatePoolWithTag(UasPoolNonPaged, bytes, UAS_POOL_TAG);
    if (p != NULL) {
        UasZero(p, bytes);
    }
    return p;
}

VOID UasFree(PVOID p)
{
    if (p != NULL) {
        ExFreePool(p);
    }
}

/*
 * The MDL a data transfer is described by. A class driver's SRB names a
 * DataBuffer inside its IRP's MdlAddress - a virtual address that need not be
 * mapped in system space - so the transfer gets a partial MDL over the
 * caller's locked pages, as a port driver gives its adapter. An SRB with no
 * MDL behind it (this driver's own internal commands, which use nonpaged
 * pool) is described directly.
 */
PMDL UasDataMdl(PIRP request, PSCSI_REQUEST_BLOCK srb)
{
    PMDL mdl;

    mdl = IoAllocateMdl(srb->DataBuffer, srb->DataTransferLength, FALSE,
                        FALSE, NULL);
    if (mdl == NULL) {
        return NULL;
    }
    if (request != NULL && request->MdlAddress != NULL) {
        IoBuildPartialMdl(request->MdlAddress, mdl, srb->DataBuffer,
                          srb->DataTransferLength);
    } else {
        MmBuildMdlForNonPagedPool(mdl);
    }
    return mdl;
}

VOID UasFreeMdl(PMDL mdl)
{
    if (mdl != NULL) {
        IoFreeMdl(mdl);
    }
}

/*
 * Task 45.52 (design record 13 section 16.5): the caller's buffer of a
 * direct pass-through, described and locked here so the engine can build
 * its partial MDL over it as over a class driver's. PASSIVE_LEVEL, in the
 * thread that issued the request (for a UserMode one; the dispatch checks
 * that), with the request's mode: the lock raises on an address the mode
 * may not use, which is caught and returned as STATUS_INVALID_USER_BUFFER.
 * deviceWrites: data in, the pages are written.
 */
NTSTATUS UasLockBuffer(PVOID address, ULONG length, KPROCESSOR_MODE mode,
                       BOOLEAN deviceWrites, PMDL *mdl)
{
    PMDL m;
    NTSTATUS status;

    *mdl = NULL;
    m = IoAllocateMdl(address, length, FALSE, FALSE, NULL);
    if (m == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = STATUS_SUCCESS;
    __try {
        MmProbeAndLockPages(m, mode, deviceWrites ? IoWriteAccess
                                                  : IoReadAccess);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        status = STATUS_INVALID_USER_BUFFER;
    }
    if (!NT_SUCCESS(status)) {
        IoFreeMdl(m);
        return status;
    }
    *mdl = m;
    return STATUS_SUCCESS;
}

/* <= DISPATCH_LEVEL. */
VOID UasUnlockBuffer(PMDL mdl)
{
    if (mdl != NULL) {
        MmUnlockPages(mdl);
        IoFreeMdl(mdl);
    }
}

/*
 * A transfer IRP in this driver's own pool. IoReuseIrp is not on Windows 98
 * (design record 13 Appendix A row 27) and IoInitializeIrp must not be run
 * over an IoAllocateIrp IRP, whose allocation flags it would erase - so the
 * IRP's memory is ours, initialised before every use and freed with the
 * slot.
 */
PIRP UasIrpAlloc(CCHAR stackSize, PUSHORT size)
{
    PIRP irp;
    USHORT bytes;

    bytes = IoSizeOfIrp(stackSize);
    irp = (PIRP)ExAllocatePoolWithTag(UasPoolNonPaged, bytes, UAS_POOL_TAG);
    if (irp != NULL) {
        IoInitializeIrp(irp, bytes, stackSize);
    }
    *size = bytes;
    return irp;
}

VOID UasIrpReset(PIRP irp, USHORT size, CCHAR stackSize)
{
    IoInitializeIrp(irp, size, stackSize);
}
