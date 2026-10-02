/*
 * hcd.h - the WDM objects of xhci98.sys, the successor host controller driver
 * (design record 13 section 5.2; roadmap-hcd.md Phase 26).
 *
 * One driver object in two PnP roles. As the controller's function driver it
 * owns a controller FDO attached over the PCI PDO; as the root hub's it owns a
 * root-hub FDO attached over the root-hub PDO the controller FDO created. The
 * role is decided at AddDevice by whether this driver object created the PDO
 * (section 5.2), and every extension begins with HCD_COMMON so a dispatch
 * routine can tell which object it was handed.
 *
 * The controller FDO's extension embeds the miniport's XHCI_EXTENSION, which
 * the kept controller sequence (xhci_init.c, xhci_cmd.c, xhci_evt.c) runs on
 * unchanged; HcdControllerFromExt is the way back from it.
 *
 * DDK-dependent: this header is for the kernel files only. The pure core never
 * includes it.
 */

#ifndef HCD_H
#define HCD_H

#include "xhci_compat.h"
#include "xhci.h"
#include "xhci_usbport.h"

#define HCD_KIND_CONTROLLER_FDO 0x43464448UL /* 'HDFC' */
#define HCD_KIND_ROOTHUB_PDO    0x50524448UL /* 'HDRP' */
#define HCD_KIND_ROOTHUB_FDO    0x46524448UL /* 'HDRF' */

/* The PnP state the dispatch routines gate on. Windows 98 can deliver a
 * REMOVE as the first PnP IRP after START (no SURPRISE_REMOVAL before it;
 * win98-wdm.md), so every state may move straight to HCD_PNP_REMOVED. */
#define HCD_PNP_ADDED            0UL
#define HCD_PNP_STARTED          1UL
#define HCD_PNP_STOP_PENDING     2UL
#define HCD_PNP_STOPPED          3UL
#define HCD_PNP_REMOVE_PENDING   4UL
#define HCD_PNP_SURPRISE_REMOVED 5UL
#define HCD_PNP_REMOVED          6UL

typedef struct _HCD_COMMON {
    ULONG Kind;
    PDEVICE_OBJECT Self;
    ULONG PnpState;
    ULONG PnpStateBeforeQuery;
    DEVICE_POWER_STATE DevicePower;
    SYSTEM_POWER_STATE SystemPower;
} HCD_COMMON, *PHCD_COMMON;

/* The one-shot timer service's slots (hcd_svc.c). Four: the command
 * watchdog is the only user and arms one at a time; the rest absorb a
 * callback still in flight when the next command is armed. */
#define HCD_TIMER_SLOTS         4UL
#define HCD_TIMER_CONTEXT_BYTES 32UL

struct _HCD_CONTROLLER;

typedef struct _HCD_TIMER {
    KTIMER Timer;
    KDPC Dpc;
    struct _HCD_CONTROLLER *Controller;
    volatile ULONG Busy;
    XHCI_ASYNC_TIMER_CALLBACK *Callback;
    UCHAR Context[HCD_TIMER_CONTEXT_BYTES];
} HCD_TIMER, *PHCD_TIMER;

typedef struct _HCD_CONTROLLER {
    HCD_COMMON Common;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT LowerDevice;

    /* IRPs inside the driver on this object, the remove handler's wait. The
     * IO_REMOVE_LOCK family is absent on Windows 98 SE (the allowlist's
     * [deny]), so this is the count it recommends instead. Starts at 1 and
     * the remove handler drops that bias. */
    LONG OutstandingIo;
    KEVENT RemoveEvent;

    /* Resources (hcd_ctl.c). */
    ULONG HaveMemory;
    ULONG HaveInterrupt;
    PHYSICAL_ADDRESS BarRaw;
    PHYSICAL_ADDRESS BarTranslated;
    ULONG BarLength;
    PVOID BarVa;
    ULONG IntVector;
    KIRQL IntIrql;
    KAFFINITY IntAffinity;
    KINTERRUPT_MODE IntMode;
    BOOLEAN IntShared;
    PKINTERRUPT Interrupt;
    KDPC IsrDpc;
    volatile LONG DpcsInFlight;
    volatile ULONG DpcClosed;
    ULONG ControllerStarted;
    ULONG ResumeFailures;

    /* The DMA adapter and the common buffer (hcd_dma.c). */
    PDMA_ADAPTER Dma;
    ULONG MapRegisters;
    PVOID CommonVa;
    PHYSICAL_ADDRESS CommonPa;
    ULONG CommonBytes;
    volatile ULONG CommonBufferPinned;
    ULONG CommonBuffersKept;

    /* The service layer (hcd_svc.c). */
    ULONG ConfigLastStatus;
    ULONG ConfigLastInformation;
    KSPIN_LOCK TimerLock;
    HCD_TIMER Timers[HCD_TIMER_SLOTS];
    volatile LONG TimersInFlight;
    ULONG TimersClosed;
    ULONG TimerArmsRefused;
    KEVENT TimersIdle;

    /* The controller thread (hcd_ctl.c). */
    PVOID ThreadObject;
    volatile ULONG ThreadRunning;
    KEVENT ThreadExited;
    volatile ULONG ThreadStop;
    KEVENT WorkEvent;

    /* The device layer (hcd_dev.c). */
    KEVENT CmdDoneEvent;
    volatile ULONG CmdDoneCode;
    volatile ULONG CmdDoneControl;
    volatile ULONG CmdDoneLost;
    ULONG SlotFatalEvents;
    ULONG TransferEventsUnclaimed;
    volatile ULONG PortChangeMask;
    volatile ULONG PortEvents;

    /* The controller lock (hcd_svc.h, HcdSvcControllerLock): created once at
     * AddDevice, outside Hc, which every start zeroes. */
    KSPIN_LOCK ControllerLock;

    /* The kept controller sequence's state, as the miniport's extension. */
    XHCI_EXTENSION Hc;
} HCD_CONTROLLER, *PHCD_CONTROLLER;

#define HcdControllerFromExt(ext) \
    CONTAINING_RECORD((ext), HCD_CONTROLLER, Hc)

/* IoSetCompletionRoutine without the ASSERT the checked DDK wraps it in,
 * which would import RtlAssert into the debug and qemu builds - a pair with
 * no Windows 98 evidence. Same three fields. */
#define HCD_SET_COMPLETION_ALWAYS(irp, routine, context)                     \
    do {                                                                     \
        PIO_STACK_LOCATION hcdNext_ = IoGetNextIrpStackLocation(irp);        \
        hcdNext_->CompletionRoutine = (routine);                             \
        hcdNext_->Context = (context);                                       \
        hcdNext_->Control = SL_INVOKE_ON_SUCCESS | SL_INVOKE_ON_ERROR |      \
                            SL_INVOKE_ON_CANCEL;                             \
    } while (0)

/* hcd_entry.c */
extern PDRIVER_OBJECT HcdDriverObject;
ULONG HcdIoEnter(PHCD_CONTROLLER hc);
VOID HcdIoLeave(PHCD_CONTROLLER hc);
NTSTATUS HcdPassDown(PHCD_CONTROLLER hc, PIRP irp);
NTSTATUS HcdCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information);

/* hcd_pnp.c */
NTSTATUS HcdControllerPnp(PHCD_CONTROLLER hc, PIRP irp);
NTSTATUS HcdForwardAndWait(PHCD_CONTROLLER hc, PIRP irp);

/* hcd_power.c */
NTSTATUS HcdControllerPower(PHCD_CONTROLLER hc, PIRP irp);

/* hcd_ctl.c */
NTSTATUS HcdStartController(PHCD_CONTROLLER hc, PIRP irp);
VOID HcdStopController(PHCD_CONTROLLER hc);
VOID HcdControllerInitObjects(PHCD_CONTROLLER hc);
VOID HcdThreadWake(PHCD_CONTROLLER hc);

/* hcd_dma.c */
NTSTATUS HcdDmaOpen(PHCD_CONTROLLER hc);
VOID HcdDmaClose(PHCD_CONTROLLER hc);

/* hcd_svc.c (the services themselves are in hcd_svc.h) */
VOID HcdRelativeMs(PLARGE_INTEGER due, ULONG milliseconds);
VOID HcdTimersInit(PHCD_CONTROLLER hc);
VOID HcdTimersDrain(PHCD_CONTROLLER hc);
VOID HcdTimersOpen(PHCD_CONTROLLER hc);

/* hcd_pool.c */
PVOID HcdPoolAlloc(ULONG bytes);
VOID HcdPoolFree(PVOID p);
ULONG HcdPoolOutstandingCount(VOID);

#endif /* HCD_H */
