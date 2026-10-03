/*
 * xhci_counters.h - the counter block the device matrix reads out of a live
 * guest (roadmap-hcd.md task 26-A.10; design record 13 sections 9.4 to 9.6;
 * scripts/vm-matrix/README.md, "Two expectation sets").
 *
 * The harness reads these words through the QEMU monitor by byte offset, and
 * scripts\vm-matrix\gen-offsets.ps1 measures the offsets by compiling this
 * header under XHCI_HOST_TEST, with no ntddk.h. That is why it is a header of
 * its own and DDK-free: the controller FDO's extension holds kernel objects a
 * host compile cannot lay out, and an offset measured against stand-ins for
 * them would not be the kernel build's. Everything here is a ULONG, so the
 * x86 and amd64 layouts are the same.
 *
 * Embedded in HCD_CONTROLLER (hcd.h) as Counters, zeroed at every controller
 * start, and published by the qemu flavour's identity lines and its
 * XHCI_DBG_VALUE_CHANGED("<label>", cnt-><Field>) sites (hcd_log.c). The
 * labels, not these names, are what matrix-hcd.psd1 is written in; the table
 * that joins them is generated, so a field renamed here renames there.
 *
 * Writers: the controller thread writes the enumeration and select counters
 * plainly (it alone runs those paths); the event DPC writes under the
 * controller lock; a dispatch routine, which holds neither, interlocks.
 * hcd_log.c's poll copies the two the kept controller sequence keeps in
 * XHCI_EXTENSION, which no HCD path increments.
 */

#ifndef XHCI_COUNTERS_H
#define XHCI_COUNTERS_H

#include "xhci_compat.h"

typedef struct _XHCIHC_COUNTERS {
    /* Enumeration: the bus's own Enable Slot and Address Device. The speed
     * pairs count each enumeration's successful Address Device once - the
     * port's decoded speed, and the speed the Slot Context it carried
     * decodes to - and never RESET_PORT's re-address of a kept slot. */
    ULONG SlotsEnabled;
    ULONG DevicesAddressed;
    ULONG PortSpeedHigh;
    ULONG PortSpeedFull;
    ULONG PortSpeedLow;
    ULONG SlotSpeedHigh;
    ULONG SlotSpeedFull;
    ULONG SlotSpeedLow;
    ULONG SpeedDisagreements;

    /* The bind: every non-default endpoint a SELECT_CONFIGURATION or
     * SELECT_INTERFACE names, counted once the URB names its interface, ends
     * opened or refused; the causes are counted beside the refusal. */
    ULONG EndpointsOpened;
    ULONG SelectEndpointsRequested;
    ULONG SelectEndpointsRefused;
    ULONG EndpointRefusalsType;
    ULONG EndpointRefusalsParams;
    ULONG EndpointRefusalsPool;
    ULONG EndpointConfigureFailures;
    ULONG EndpointsNoBandwidth;
    ULONG EndpointsNoResources;
    ULONG UrbsMalformed;
    /* A SELECT_CONFIGURATION or SELECT_INTERFACE answered with a failure,
     * once per request whatever its cause, so a select that fails after its
     * endpoints were opened on the controller - SET_CONFIGURATION or
     * SET_INTERFACE refused by the device - or on a malformed record is
     * still a refusal. BUFFER_TOO_SMALL is not one: it is the length probe
     * a client resizes from. */
    ULONG SelectsFailed;

    /* The controller. */
    ULONG FatalStatus;
    ULONG TransferEventsUnclaimed;
    ULONG InterruptMaskFailures;
    ULONG CommandsGivenUp;

    /* Transfers on the URB path: a TD published is later completed by an
     * event or taken off the ring cancelled. The isochronous three are the
     * transfer engine's per-queue counts, summed as events move them. */
    ULONG TransfersSubmitted;
    ULONG TransfersCompleted;
    ULONG TransfersCancelled;
    ULONG IsoPacketsAnswered;
    ULONG IsoMissedService;
    ULONG IsoPacketErrors;

    /* Hubs: Phase 27's. Published at zero until the bus serves a hub, so
     * the matrix's hub rows read a field rather than an absent label. */
    ULONG HubsStarted;
    ULONG TopoDescriptors;
    ULONG TopoDescriptorsBad;
    ULONG TopoHubSlotsMarked;
    ULONG TopoNodesDropped;
    ULONG TopoBehindHubAddressed;
    ULONG TopoBehindHubOpens;
    ULONG TopoBehindHubTooDeep;
    ULONG TopoTtProgrammed;
} XHCIHC_COUNTERS, *PXHCIHC_COUNTERS;

XHCI_C_ASSERT(xhcihc_counters_all_ulong,
              sizeof(XHCIHC_COUNTERS) == 39 * sizeof(ULONG));

#endif /* XHCI_COUNTERS_H */
