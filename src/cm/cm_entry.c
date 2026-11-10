/*
 * cm_entry.c - DriverEntry, AddDevice and the dispatch table of
 * xhcicm.sys (cm.h has its roles and other kinds of device object).
 *
 * The image carries the flavour marker XHCI98_FLAVOUR_* that
 * scripts\check-flavour-marker.ps1 and make-package.ps1 read out of the
 * bytes, from the define src\cm\sources derives from BUILD_ALT_DIR as
 * src\sources does for xhci98.sys, and the package version string.
 *
 * IRQL: DriverEntry, AddDevice, Unload, the PnP and power dispatch,
 * CmForwardSync, CmWaitEvent and CmWaitEventMs at PASSIVE_LEVEL;
 * CmCompleteIrp, CmZero, CmRelativeMs and the other dispatch routines at
 * <= DISPATCH_LEVEL.
 */

#include "cm.h"
#include "../xhci_version.h"
#include "cm_nvlink.h"
#include "cm_ports.h"
#include "cm_oscp.h"

#if defined(XHCI_FLAVOUR_QEMU)
#define CM_FLAVOUR_NAME "XHCI98_FLAVOUR_QEMU"
#elif defined(XHCI_FLAVOUR_RELEASE)
#define CM_FLAVOUR_NAME "XHCI98_FLAVOUR_RELEASE"
#else
#error "no build flavour was defined. src/cm/sources derives XHCI_FLAVOUR_RELEASE or _QEMU from BUILD_ALT_DIR (the debug flavour was retired with task 45.45); build through scripts\build-driver.cmd."
#endif

static volatile const char CmFlavourMarker[] = CM_FLAVOUR_NAME;
static volatile const char CmVersionString[] = "xhcicm " XHCI_VER_STR;

PDRIVER_OBJECT CmDriverObject;

/* ---- the kept child's driver-wide part (design record 31 section 4.10,
 * task 48.29): the system, the detach owner and the callback ---- */

ULONG CmKeptOn;
static FAST_IO_DISPATCH cmFastIo;
static KSPIN_LOCK cmDetachLock;

VOID CmCommonInit(PCM_COMMON common)
{
    common->Detach = CM_DETACH_NONE;
    KeInitializeEvent(&common->DetachDone, NotificationEvent, FALSE);
}

BOOLEAN CmDetachOnce(PCM_COMMON common, BOOLEAN wait)
{
    KIRQL irql;
    ULONG was;

    KeAcquireSpinLock(&cmDetachLock, &irql);
    was = common->Detach;
    if (was == CM_DETACH_NONE) {
        common->Detach = CM_DETACH_CLAIMED;
    }
    KeReleaseSpinLock(&cmDetachLock, irql);
    if (was == CM_DETACH_NONE) {
        IoDetachDevice(common->Lower);
        KeAcquireSpinLock(&cmDetachLock, &irql);
        common->Detach = CM_DETACH_DONE;
        KeReleaseSpinLock(&cmDetachLock, irql);
        KeSetEvent(&common->DetachDone, IO_NO_INCREMENT, FALSE);
        return TRUE;
    }
    if (wait) {
        (VOID)CmWaitEvent(&common->DetachDone, 0);
    }
    return FALSE;
}

/*
 * The I/O manager's notice that the object below one of this driver's
 * attached objects is being deleted (FastIoDetachDevice; set only with
 * CmKeptOn): the filters and children are cm_filt.c's; the NHI's and a
 * mailbox member's object is detached here, and its removal in progress
 * (the object below deleted inside its forwarded removal) skips its own
 * detach and deletes it. The I/O manager calls it from IoDeleteDevice,
 * at PASSIVE_LEVEL.
 */
static VOID cmFastIoDetach(PDEVICE_OBJECT source, PDEVICE_OBJECT target)
{
    PCM_COMMON common;

    if (source == NULL || source->DriverObject != CmDriverObject) {
        return;
    }
    common = (PCM_COMMON)source->DeviceExtension;
    if (common->Kind == CM_KIND_FILTER || common->Kind == CM_KIND_CHILD) {
        CmFilterDetachCb((PCM_FILTER)common, target);
        return;
    }
    if ((common->Kind == CM_KIND_NHI || common->Kind == CM_KIND_MEMBER) &&
        common->Lower == target) {
        /* Detached when this returns, whoever claimed it (K6). */
        (VOID)CmDetachOnce(common, TRUE);
    }
}

/* The hardware id list IoGetDeviceProperty returns for a PCI function:
 * four or five ids of about 50 characters each. A longer list is read as
 * no list (CM_ROLE_NONE). */
#define CM_ID_CHARS 512

VOID CmZero(PVOID p, ULONG n)
{
    PUCHAR b;
    ULONG i;

    b = (PUCHAR)p;
    for (i = 0; i < n; i++) {
        b[i] = 0;
    }
}

NTSTATUS CmCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS cmSignal(PDEVICE_OBJECT device, PIRP irp, PVOID context)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(irp);
    KeSetEvent((PKEVENT)context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* The IRP passed down and waited for; the caller completes it. */
NTSTATUS CmForwardSync(PCM_COMMON common, PIRP irp)
{
    KEVENT done;

    KeInitializeEvent(&done, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(irp);
    CM_SET_COMPLETION_ALWAYS(irp, cmSignal, &done);
    (VOID)IoCallDriver(common->Lower, irp);
    (VOID)CmWaitEvent(&done, 0);
    return irp->IoStatus.Status;
}

/* hcd_svc.c's HcdRelativeMs, for this image. */
VOID CmRelativeMs(PLARGE_INTEGER due, ULONG ms)
{
    ULONG ticks;

    if (ms > 400000UL) {
        ms = 400000UL;
    }
    ticks = ms * CM_CLOCK_UNITS_PER_MS;
    due->LowPart = 0UL - ticks;
    due->HighPart = (ticks == 0) ? 0 : -1;
}

/*
 * hcd_svc.c's HcdWaitEvent, for this image. Windows 98 can end
 * KeWaitForSingleObject with an undocumented 0xFFFFFFFF and the event
 * unsignalled (win98-wdm.md, "Win98/98 SE status"); a caller that went on
 * from there would leave a stack event for a completion routine to set in
 * a dead frame, free an IRP still below it, free or delete what a thread,
 * a DPC or a dispatch still uses, or enter a gate another party holds. A
 * 1 ms delay follows each unsatisfied return, so a wait that keeps ending
 * at once yields the processor rather than spinning.
 *
 * `bound` 0 waits until the event is satisfied: for an event a completion
 * routine, a leaving thread or a draining count is owed to set, any return
 * before that is worse than waiting on. A gate passes CM_GATE_WAIT_BOUND,
 * because win98-wdm.md knows no remedy for the nested-block case and warns
 * against an unbounded retry there, which would wedge the gate's every
 * later caller too; after `bound` unsatisfied returns in a row the wait is
 * abandoned, and the caller, which does not hold the gate then, neither
 * enters nor releases it.
 */
ULONG CmWaitEvent(PKEVENT event, ULONG bound)
{
    LARGE_INTEGER due;
    ULONG missed;

    missed = 0;
    while (KeWaitForSingleObject(event, Executive, KernelMode, FALSE,
                                 NULL) != STATUS_SUCCESS) {
        missed++;
        if (bound != 0 && missed >= bound) {
            break;
        }
        CmRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    return missed;
}

/*
 * A timed wait whose time is the clock's, not the count of its returns:
 * an unsatisfied return before the time is up (Windows 98's, above) is
 * followed by a 1 ms delay and a wait for what is left, so it neither
 * ends the wait early nor spins.
 */
BOOLEAN CmWaitEventMs(PKEVENT event, ULONG ms)
{
    LARGE_INTEGER due;
    LARGE_INTEGER from;
    LARGE_INTEGER now;
    ULONG tickUs;
    ULONG ticks;
    ULONG spent;
    ULONG left;
    NTSTATUS status;

    if (ms > 400000UL) {
        ms = 400000UL;
    }
    tickUs = KeQueryTimeIncrement() / 10UL;
    if (tickUs == 0) {
        tickUs = 1;
    }
    KeQueryTickCount(&from);
    for (;;) {
        KeQueryTickCount(&now);
        ticks = now.LowPart - from.LowPart;
        if (ticks >= 0xFFFFFFFFUL / tickUs) {
            spent = ms;
        } else {
            spent = ticks * tickUs / 1000UL;
        }
        left = (spent < ms) ? ms - spent : 0;
        CmRelativeMs(&due, left);
        status = KeWaitForSingleObject(event, Executive, KernelMode, FALSE,
                                       &due);
        if (status == STATUS_SUCCESS) {
            return TRUE;
        }
        if (status == STATUS_TIMEOUT || left == 0) {
            return FALSE;
        }
        CmRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
}

/* TRUE when a bridge filter of this driver is already on the PDO's stack:
 * a bridge the filter INF names and whose LowerFilters value the mark
 * service wrote calls AddDevice twice, once as a lower and once as an
 * upper filter. Until task 45.12 the first object served both (decision
 * M15); since then the lower call is the child (CmRolePlace, design record
 * 33 section 4.4) and this guards the upper one against a second bridge
 * filter. The stack is being built by the PnP manager, which is this
 * call's caller, so it holds still. PASSIVE_LEVEL. */
static BOOLEAN cmBridgeOnStack(PDEVICE_OBJECT pdo)
{
    PDEVICE_OBJECT walk;
    ULONG n;

    n = 0;
    for (walk = pdo->AttachedDevice; walk != NULL && n < 16;
         walk = walk->AttachedDevice) {
        if (walk->DriverObject == CmDriverObject &&
            ((PCM_COMMON)walk->DeviceExtension)->Kind == CM_KIND_FILTER) {
            return TRUE;
        }
        n++;
    }
    return FALSE;
}

/* TRUE when a device of the PCI driver (the PDO's own driver, which is a
 * bridge's function driver too) is already above the PDO: this AddDevice
 * is an upper filter's, made after the FDO (design record 33 section 4.4).
 * The stack is being built by the PnP manager, this call's caller, so it
 * holds still. PASSIVE_LEVEL. */
static BOOLEAN cmPciAbove(PDEVICE_OBJECT pdo)
{
    PDEVICE_OBJECT walk;
    ULONG n;

    n = 0;
    for (walk = pdo->AttachedDevice; walk != NULL && n < 16;
         walk = walk->AttachedDevice) {
        if (walk->DriverObject == pdo->DriverObject) {
            return TRUE;
        }
        n++;
    }
    return FALSE;
}

/*
 * Which role this PDO is (record 21 section 3.2; since task 42-A.5,
 * section 7.4 revision 3's CmRoleDecide). Windows 98 SE and ME
 * (IoIsWdmVersionAvailable(1, 0x10) FALSE) have the NHI role alone: no
 * INF of this driver that their engine reads names a bridge, no mark
 * service runs there, and a WDM driver on a bridge devnode there breaks
 * the bridge (section 6.1), so the ids are not read. On the NT targets the
 * ids decide, and every PDO that is neither an NHI nor a filter INF's
 * bridge is the LowerFilters child, which never fails AddDevice. Since
 * task 45.12 a filter INF's bridge is the bridge filter only above the
 * PCI driver's FDO (its UpperFilters) and the child below it (its
 * LowerFilters bridge mark), CmRolePlace.
 */
static NTSTATUS cmAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo)
{
    WCHAR ids[CM_ID_CHARS];
    ULONG bytes;
    CM_IDS idsSeen;
    NTSTATUS status;
    ULONG role;
    ULONG member;
    ULONG pciAbove;

    CmZero(&idsSeen, sizeof(idsSeen));
    if (!IoIsWdmVersionAvailable(1, 0x10)) {
        idsSeen.Role = CM_ROLE_NHI;
        return CmNhiAddDevice(driver, pdo, &idsSeen);
    }
    bytes = 0;
    status = IoGetDeviceProperty(pdo, DevicePropertyHardwareID,
                                 sizeof(ids) - 2 * sizeof(WCHAR), ids,
                                 &bytes);
    if (NT_SUCCESS(status) && bytes <= sizeof(ids) - 2 * sizeof(WCHAR)) {
        /* Two terminators past what was returned, so a list the bus did
         * not end with an empty string still ends here. */
        ids[bytes / sizeof(WCHAR)] = 0;
        ids[bytes / sizeof(WCHAR) + 1] = 0;
        (VOID)CmClassifyIds(ids, bytes / sizeof(WCHAR) + 2, &idsSeen);
    }
    pciAbove = (ULONG)cmPciAbove(pdo);
    /* Record 32 section 12.8.15.7: the ASM4242's upstream port above the
     * PCI driver and its xHCI above its function driver are the mailbox's
     * members, objects of their own (cm_oscnt.c); its downstream ports are
     * bridge filters with a member half. */
    member = CmOscpMemberRole(idsSeen.Vendor, idsSeen.Device,
                              (BOOLEAN)(idsSeen.Role == CM_ROLE_FILTER),
                              (BOOLEAN)(pciAbove != 0),
                              (BOOLEAN)(pdo->AttachedDevice != NULL));
    if (member == CM_OSCP_R_USP || member == CM_OSCP_R_XHCI) {
        return CmOscmAddDevice(driver, pdo, member);
    }
    role = CmRoleDecide(&idsSeen,
                        (ULONG)CmPortsFilterId(CM_PORTS_ID(idsSeen.Vendor,
                                                           idsSeen.Device)));
    /* Since task 45.12 (design record 33 section 4.4): a listed bridge's
     * LowerFilters registration, made before the PCI driver's FDO, is the
     * child; the NHI's answer stands whatever is above its PDO. */
    role = CmRolePlace(role, pciAbove, (ULONG)(CmBreqGate != 0));
    if (role == CM_ROLE_FILTER) {
        if (cmBridgeOnStack(pdo)) {
            return STATUS_SUCCESS;
        }
        return CmFilterAddDevice(driver, pdo,
                                 CM_PORTS_ID(idsSeen.Vendor, idsSeen.Device),
                                 member == CM_OSCP_R_DSP ? member : 0);
    }
    if (role == CM_ROLE_NHI) {
        return CmNhiAddDevice(driver, pdo, &idsSeen);
    }
    return CmChildAddDevice(driver, pdo);
}

static VOID cmUnload(PDRIVER_OBJECT driver)
{
    UNREFERENCED_PARAMETER(driver);
    /* Installed on NT alone (DriverEntry), where no CONFIGMG worker runs,
     * and CmCfgwkStart clears it where one starts: the drain is for a
     * driver that never started one (cm_cfgwk.c), and finds none. */
    CmCfgwkDrain();
    CmNvlinkUnload();
}

/* Every dispatch routine tells the query device (CM_KIND_QUERY,
 * cm_qryos.c) from the PnP roles first: it has no lower object. */
static NTSTATUS cmDispatchPnp(PDEVICE_OBJECT device, PIRP irp)
{
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_QUERY) {
        return CmQueryDispatch(device, irp);
    }
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_NETPDO) {
        return CmNetPdoPnp((PCM_NETPDO)device->DeviceExtension, irp);
    }
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_NHI) {
        return CmNhiPnp((PCM_NHI)device->DeviceExtension, irp);
    }
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_MEMBER) {
        return CmOscmPnp((PCM_MEMBER)device->DeviceExtension, irp);
    }
    if (((PCM_FILTER)device->DeviceExtension)->Kept) {
        return CmKeptDispatch((PCM_FILTER)device->DeviceExtension, irp);
    }
    return CmFilterPnp((PCM_FILTER)device->DeviceExtension, irp);
}

static NTSTATUS cmDispatchPower(PDEVICE_OBJECT device, PIRP irp)
{
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_QUERY) {
        return CmQueryDispatch(device, irp);
    }
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_NETPDO) {
        return CmNetPdoPower((PCM_NETPDO)device->DeviceExtension, irp);
    }
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_NHI) {
        return CmNhiPower((PCM_NHI)device->DeviceExtension, irp);
    }
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_MEMBER) {
        return CmOscmPower((PCM_MEMBER)device->DeviceExtension, irp);
    }
    if (((PCM_FILTER)device->DeviceExtension)->Kept) {
        return CmKeptDispatch((PCM_FILTER)device->DeviceExtension, irp);
    }
    return CmFilterPower((PCM_FILTER)device->DeviceExtension, irp);
}

/* Every other request, on any PnP role, goes down unchanged: the NHI's
 * interface is the query device's, and the filters add nothing to their
 * device.
 * The PnP and power dispatch send both filter kinds (CM_KIND_FILTER and
 * CM_KIND_CHILD) to cm_filt.c. */
static NTSTATUS cmDispatchOther(PDEVICE_OBJECT device, PIRP irp)
{
    PCM_COMMON common;

    common = (PCM_COMMON)device->DeviceExtension;
    if (common->Kind == CM_KIND_QUERY) {
        return CmQueryDispatch(device, irp);
    }
    if (common->Kind == CM_KIND_NETPDO) {
        /* The network PDO has no lower object (cm_net.c). */
        return CmNetPdoOther((PCM_NETPDO)common, irp);
    }
    if (common->Kind == CM_KIND_CHILD && ((PCM_FILTER)common)->Kept) {
        return CmKeptDispatch((PCM_FILTER)common, irp);
    }
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(common->Lower, irp);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    ULONG i;

    UNREFERENCED_PARAMETER(RegistryPath);
    /* Read once, so the linker keeps both strings in the image. */
    if (CmFlavourMarker[0] == 0 || CmVersionString[0] == 0) {
        return STATUS_UNSUCCESSFUL;
    }
    CmDriverObject = DriverObject;
    KeInitializeSpinLock(&cmDetachLock);
    CmPoolInit();
    /* The unload routine is set before CmNvlinkInit: on NT, the load that
     * publishes the link-state table takes the routine away there, before
     * its callback can answer (task 45.31), and nothing after gives it
     * back. Windows 98 SE and ME: resident for the boot, whatever the PnP
     * devices do (stor98cm.h R4: the link-state channel's proof key is
     * answered from this image, cm_nvlinkos.c), as a CONFIGMG worker's
     * start made it before. */
    DriverObject->DriverUnload =
        IoIsWdmVersionAvailable(1, 0x10) ? cmUnload : NULL;
    CmRegInit();
    CmNvlinkInit();
    CmOscInit();
    CmOscntInit();
    CmEjectInit();
    CmLogClockInit();
    CmQueryInit();
    CmNhiCarryInit();
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = cmDispatchOther;
    }
    DriverObject->MajorFunction[IRP_MJ_PNP] = cmDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = cmDispatchPower;
    DriverObject->DriverExtension->AddDevice = cmAddDevice;
    if (IoIsWdmVersionAvailable(1, 0x10)) {
        /* Design record 21 section 6.2.1's gate, read once (cm_reg.c), and
         * design record 33's. */
        CmReadIrqGate();
        CmReadBreqGate();
        /* Design record 31 section 4.10 (K1, K7): the kept child on 8.1
         * alone, its callback through this table only, and the image
         * resident for the boot, so no callback or work item of a kept
         * child runs in an unloaded image. */
        CmKeptOn = CmPortsKeptOs(SharedUserData->NtMajorVersion,
                                 SharedUserData->NtMinorVersion);
        if (CmKeptOn) {
            CmZero(&cmFastIo, sizeof(cmFastIo));
            cmFastIo.SizeOfFastIoDispatch = sizeof(cmFastIo);
            cmFastIo.FastIoDetachDevice = cmFastIoDetach;
            DriverObject->FastIoDispatch = &cmFastIo;
            DriverObject->DriverUnload = NULL;
        }
    }
    return STATUS_SUCCESS;
}
