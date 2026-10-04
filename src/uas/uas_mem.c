/*
 * uas_mem.c - every pool, MDL and IRP-memory call xhciuas.sys makes, in one
 * object, so the import allowlist can hold those pairs to it
 * (scripts\import-gate\xhciuas-imports.allow, SITES=uas_mem.obj; the HCD's
 * rule of design record 13 section 7.5 applied to the second binary).
 *
 * IRQL: <= DISPATCH_LEVEL throughout (nonpaged pool only).
 */

#include "uas.h"

PVOID UasAlloc(ULONG bytes)
{
    PVOID p;

    p = ExAllocatePoolWithTag(NonPagedPool, bytes, UAS_POOL_TAG);
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
    irp = (PIRP)ExAllocatePoolWithTag(NonPagedPool, bytes, UAS_POOL_TAG);
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
