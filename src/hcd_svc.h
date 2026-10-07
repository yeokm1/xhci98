/*
 * hcd_svc.h - the services the adapted controller files call, which the
 * miniport reached through usbport's registration packet and the HCD provides
 * itself (roadmap-hcd.md task 26-A.2: "the controller sequence of
 * xhci_init.c, xhci_cmd.c and xhci_evt.c with the usbport services
 * replaced"). Implemented in hcd_svc.c.
 *
 * Each keeps the contract of the usbport service it replaces where the
 * adapted code depends on it, and says where it does not:
 *
 *   HcdSvcWaitMs        UsbPortWait. PASSIVE_LEVEL only, as before; the
 *                       callers already stall instead below PASSIVE
 *                       (ext->InitBelowPassive).
 *   HcdSvcConfigSpace   UsbPortReadWriteConfigSpace: IRP_MN_READ_CONFIG or
 *                       IRP_MN_WRITE_CONFIG to the PCI PDO, as Windows 98
 *                       SE's uhcd.sys and NUSB's usbport send them. PASSIVE_LEVEL;
 *                       refuses above it rather than blocking.
 *   HcdSvcArmTimer      UsbPortRequestAsyncCallback: the callback runs once,
 *                       at DISPATCH_LEVEL from a timer DPC, on a private copy
 *                       of the context, and cannot be cancelled by the caller
 *                       (the stale-callback rule of the command watchdog is
 *                       unchanged). Unlike usbport's, a refusal is reported:
 *                       the slots are preallocated, and when none is free the
 *                       arm is refused rather than lost. A pending arm of
 *                       the same callback is superseded, unless the new
 *                       context is the older one: that arm is dropped.
 *   HcdSvcRequestReset  UsbPortInvalidateController(RESET): the controller is
 *                       declared failed and the recovery runs from the HCD's
 *                       PASSIVE-level thread rather than a usbport DPC.
 *   HcdSvcControllerLock  the controller lock's word: per controller, created
 *                       once at AddDevice outside the zeroed extension, where
 *                       the miniport kept one lock in the driver image.
 *   HcdSvcDmaNotStopped UsbPortBugCheck. The HCD owns its common buffer, so it
 *                       does not need to crash the machine to keep a live bus
 *                       master off freed memory: the block is never freed
 *                       (design record 13 section 11.2's free is skipped and
 *                       counted). No KeBugCheckEx import.
 *
 * READING THE KEPT FILES. xhci_init.c, xhci_cmd.c, xhci_evt.c and xhci_pci.c
 * were the miniport's and their comments still speak of usbport, its
 * callbacks and its locks - in the present tense, as the design argument for
 * each step. The steps are kept, and so are the arguments; what changed is
 * who plays usbport's part. Read the comments with this key:
 *
 *   usbport / "the port driver"   the HCD's own code in hcd_*.c
 *   StartController               HcdStartController (hcd_ctl.c), at
 *                                 IRP_MN_START_DEVICE, after zeroing the
 *                                 extension as usbport did
 *   StopController                HcdStopController, at stop, surprise
 *                                 removal and remove
 *   EnableInterrupts / Disable-   XhciEnableInterrupts / XhciDisableInterrupts
 *     Interrupts                  called by HcdStartController / Stop in
 *                                 usbport's order
 *   ISR / InterruptDpc            hcdIsr / hcdIsrDpc, on the HCD's own
 *                                 IoConnectInterrupt and KDPC
 *   CheckController (500 ms)      the controller thread's health poll (100 ms)
 *   ResetController               HcdSvcRequestReset
 *   SuspendController /           IRP_MN_SET_POWER Dx / D0 (hcd_power.c)
 *     ResumeController
 *   MiniportSpinLock              the DISPATCH_LEVEL the thread raises to
 *                                 around the calls whose contract is it
 *   UsbPort* services             the HcdSvc* functions below
 *   the root hub's RH_* callbacks the device layer (hcd_dev.c); there is no
 *     and "usbport's root hub"    usbport root hub, and the HCD's own root hub
 *                                 is task 26-A.4's
 *   UsbPortCompleteTransfer /     nothing yet: the transfer path is 26-A.5's
 *     InvalidateEndpoint
 *
 * A comment that cites a usbport binary (a hint, an offset, a measured
 * ordering) is history: it records why the step exists, not a dependency.
 *
 * DDK-free declarations, so the host suites can link stubs of their own.
 */

#ifndef HCD_SVC_H
#define HCD_SVC_H

#include "xhci.h"

/* The size of the copy HcdSvcArmTimer keeps of a callback's context. The only
 * caller arms with an XHCI_COMMAND_TIMEOUT. */
#define HCD_TIMER_CONTEXT_BYTES 32UL

VOID HcdSvcWaitMs(PXHCI_EXTENSION ext, ULONG milliseconds);
MPSTATUS HcdSvcConfigSpace(PXHCI_EXTENSION ext, BOOLEAN read, PVOID buffer,
                           ULONG offset, ULONG length);
ULONG HcdSvcArmTimer(PXHCI_EXTENSION ext, ULONG milliseconds, PVOID context,
                     ULONG contextLength, XHCI_ASYNC_TIMER_CALLBACK *callback);
VOID HcdSvcRequestReset(PXHCI_EXTENSION ext);
VOID HcdSvcDmaNotStopped(PXHCI_EXTENSION ext);
PKSPIN_LOCK HcdSvcControllerLock(PXHCI_EXTENSION ext);

#if defined(XHCI_FLAVOUR_QEMU)
/* The qemu flavour's test aids, never in a published image: task 35.4's
 * speed-table override, latched by the start (hcd_ctl.c), and 35-T.9's
 * hooks (hcd_inj.c): the PORTSC filter and the doorbell on the accessors
 * in xhci_pci.c, the health poll's USBSTS (xhci_cmd.c), and the event
 * drain's swallowed Stopped event and injected Transfer Event
 * (xhci_evt.c). */
ULONG HcdSvcQemuPsiE460(PXHCI_EXTENSION ext);
ULONG HcdInjPortscRead(PXHCI_EXTENSION ext, ULONG port, ULONG value);
ULONG HcdInjPortscWrite(PXHCI_EXTENSION ext, ULONG port, ULONG value);
ULONG HcdInjHealthUsbsts(PXHCI_EXTENSION ext, ULONG usbsts);
ULONG HcdInjDoorbell(PXHCI_EXTENSION ext, ULONG slot, ULONG value);
ULONG HcdInjEventSwallow(PXHCI_EXTENSION ext, const XHCI_TRB *ev);
ULONG HcdInjEventTake(PXHCI_EXTENSION ext, XHCI_TRB *ev);
VOID HcdInjEventDone(PXHCI_EXTENSION ext);
#endif

#endif /* HCD_SVC_H */
