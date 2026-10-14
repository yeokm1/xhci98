/*
 * cm.h - xhcicm.sys, the Thunderbolt 3 connection manager (design record
 * 21; roadmap-usb4.md Phase 39), a second binary beside xhci98.sys with
 * its own INFs, allowlists and INF gate, built from src\cm as xhciuas.sys
 * is built from src\uas.
 *
 * TWO ROLES IN ONE BINARY (record 21 section 3.2), told apart in AddDevice
 * by the PDO's hardware ids (cm_vals.c, CmClassifyIds):
 *
 *   [CM_NHI]     the NHI's function driver, every target (xhcicm.inf):
 *                cm_nhi.c. Owns BAR0, the rings, the ICM client and the
 *                connection-manager thread once tasks 39-A.2 to 39-A.4
 *                land; in this scaffold its start reads the registry
 *                values and touches no hardware.
 *   [CM_FILTER]  the tunnel bridge's upper filter, NT targets only
 *                (xhcicmf.inf; IoIsWdmVersionAvailable(1, 0x10) TRUE):
 *                cm_filt.c. Passes every IRP down unchanged; task 39-A.5
 *                adds the registration list the NHI asks it through.
 *
 * FILES. cm_entry.c (DriverEntry, AddDevice, the dispatch table),
 * cm_nhi.c, cm_filt.c, cm_reg.c (the one registry site), cm_log.c (the
 * shared log ring of src\xhci_log.c and its emission, decision D12),
 * cm_pool.c (the one pool site, AGENTS.md's named-sites rule), cm_vals.c
 * (pure: roles and value rules). Reserved for the later tasks, each added
 * to `sources` when it lands: tbt_ring.c and tbt_regs.h (39-A.2),
 * tbt_icm.c (39-A.3), tbt_topo.c and cm_cfgmg.h (39-A.5).
 *
 * IRQL: as each file's header says; every object this driver creates is
 * DO_POWER_PAGABLE (the filter copies its lower device's power flags).
 */

#ifndef CM_H
#define CM_H

#include "../xhci_compat.h"
#include "../xhci_log.h"
#include "cm_vals.h"

#define CM_POOL_TAG 'mChX'

/* CM_NHI.Stage. */
#define CM_STAGE_STOPPED 0  /* not started, or stopped                    */
#define CM_STAGE_INERT   1  /* started, pre-activation: no ring, protocol
                               session or connection-manager mailbox      */
#define CM_STAGE_ACTIVE  2  /* rings and session up (task 39-A.4)         */

#define CM_KIND_NHI    0x4E4D4358UL  /* 'XCMN' */
#define CM_KIND_FILTER 0x464D4358UL  /* 'XCMF' */

/* The head of both extensions: Kind is the first field, read by the
 * dispatch routines before they know which role a device object plays. */
typedef struct _CM_COMMON {
    ULONG Kind;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
} CM_COMMON, *PCM_COMMON;

typedef struct _CM_NHI {
    CM_COMMON Common;
    /* What the matched NHI's ids say (CmClassifyIds) and the profile
     * they choose (CmProfileFor). Both zero where the ids are not read
     * (Windows 98 SE and ME): task 39-A.4 reads the ids from
     * configuration space there, and confirms the profile everywhere from
     * the firmware status after start. */
    CM_IDS Ids;
    ULONG Profile;
    /* CM_INERT_* of the last start: CM_INERT_NONE once a client runs. */
    ULONG Inert;
    /* How far the start went (CM_STAGE_*). A failure is cleaned up by the
     * stage reached: before activation there is nothing to undo; after it
     * this driver's rings are stopped and its work drained before the DMA
     * memory is freed, and tunnels are preserved. The scaffold never goes
     * past CM_STAGE_INERT. */
    ULONG Stage;
    /* Platform-power changes this instance made (task 39-A.4: the
     * integrated hosts' force power and LTR), recorded apart from Stage so
     * an inert or failed instance restores only what it changed itself.
     * Always 0 in the scaffold. */
    ULONG PowerChanged;
    ULONG Started;
    ULONG Starts;
    ULONG Stops;
    /* Read once per start (cm_reg.c), at the start's first step. */
    ULONG ValuesRead;
    CM_VALUES Values;
    /* Guards Log: the connection-manager thread appends from task 39-A.4
     * on, beside the PnP path. */
    KSPIN_LOCK LogLock;
    XHCI_LOG Log;
} CM_NHI, *PCM_NHI;

typedef struct _CM_FILTER {
    CM_COMMON Common;
    ULONG Started;
    /* 1 for the attach, plus one per device-usage notification in flight;
     * removal drops the first and waits on Drained (cm_filt.c). */
    LONG Outstanding;
    KEVENT Drained;
} CM_FILTER, *PCM_FILTER;

/* cm_entry.c */
extern PDRIVER_OBJECT CmDriverObject;
NTSTATUS CmCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information);
NTSTATUS CmForwardSync(PCM_COMMON common, PIRP irp);
VOID CmZero(PVOID p, ULONG n);

/* cm_nhi.c */
NTSTATUS CmNhiAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo,
                        const CM_IDS *ids);
NTSTATUS CmNhiPnp(PCM_NHI nhi, PIRP irp);
NTSTATUS CmNhiPower(PCM_NHI nhi, PIRP irp);

/* cm_filt.c */
NTSTATUS CmFilterAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo);
NTSTATUS CmFilterPnp(PCM_FILTER filter, PIRP irp);
NTSTATUS CmFilterPower(PCM_FILTER filter, PIRP irp);

/* cm_reg.c */
VOID CmReadValues(PCM_NHI nhi);

/* cm_log.c */
VOID CmLogInit(PCM_NHI nhi);
VOID CmLogApply(PCM_NHI nhi);
VOID CmLogNote(PCM_NHI nhi, const char *label, ULONG value);
VOID CmLogEmit(PCM_NHI nhi, ULONG reason);

/* cm_pool.c */
PVOID CmAlloc(ULONG bytes);
VOID CmFree(PVOID p);

/* The completion routine of a forwarded IRP is called on success, error
 * and cancel alike; IoSetCompletionRoutine's macro spelled out, as uas.h
 * does. */
#define CM_SET_COMPLETION_ALWAYS(irp, routine, context)                      \
    do {                                                                     \
        PIO_STACK_LOCATION cmNext_ = IoGetNextIrpStackLocation(irp);         \
        cmNext_->CompletionRoutine = (routine);                              \
        cmNext_->Context = (context);                                        \
        cmNext_->Control = SL_INVOKE_ON_SUCCESS | SL_INVOKE_ON_ERROR |       \
                           SL_INVOKE_ON_CANCEL;                              \
    } while (0)

#endif /* CM_H */
