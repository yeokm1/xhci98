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
     * once per request, so a select that fails after its
     * endpoints were opened on the controller - SET_CONFIGURATION or
     * SET_INTERFACE refused by the device - or on a malformed record is
     * still a refusal. BUFFER_TOO_SMALL is not one: it is the length probe
     * a client resizes from. Nor is a failure on a device proven to have
     * left its port (hcd_cfg.c, hcdCfgCountSelect): a select racing an
     * unplug is not a refusal. */
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

    /* SuperSpeed: Phase 29's. Appended, so every earlier offset stands.
     * The speed pair counts as the High/Full/Low ones do (the port's
     * decoded speed and the Slot Context's) for the SuperSpeed class;
     * SuperSpeedPlus counts the addressed devices whose trained rate is
     * above Gen 1x1 (29-A.1, "counted by rate"). Then the link (29-A.2):
     * warm resets written (driver policy and recovery), hot resets the xHC
     * itself carried out warm (WRC seen after a PR), links given up after
     * the warm-reset budget - the device left to its USB 2.0 companion,
     * 29-A.5's passive fallback - and SuperSpeed-capable devices (a BOS with
     * a SuperSpeed USB Device Capability) enumerated on a USB 2.0 companion
     * port, which is that fallback seen from the other side. A BOS read for
     * it on the USB 2.0 path was withdrawn (Codex review of Phase 29, round
     * 2, finding 1): it counts instead the held device itself enumerating on
     * its companion, matched by the identity 29-A.5's hold read on the
     * SuperSpeed port. Then the BOS reads that
     * failed, and the SuperSpeedPlus isochronous endpoints refused because
     * their payload does not fit the Endpoint Context (29-A.6). */
    ULONG PortSpeedSuper;
    ULONG SlotSpeedSuper;
    ULONG PortSpeedSuperPlus;
    ULONG SsWarmResets;
    ULONG SsResetsConverted;
    ULONG SsLinksGivenUp;
    ULONG SsDevicesOnUsb2;
    ULONG SsBosMissing;
    ULONG SsEndpointsEsitRefused;

    /* 29-A.5's hold, appended: holds begun by kind - identified and
     * companion-paired (releasable), unidentified (no serial, no descriptor,
     * a failed identity read: until the next start), orphan - then holds
     * released, companion visits by some other device, send-back requests
     * refused, and holds dropped by a controller reset that took the port
     * out of SS.Disabled. */
    ULONG HoldsPaired;
    ULONG HoldsUnidentified;
    ULONG HoldsOrphan;
    ULONG HoldsReleased;
    ULONG HoldCompanionOthers;
    ULONG HoldRequestsRefused;
    ULONG HoldsDropped;

    /* SuperSpeed hubs: Phase 30's (30-A.1), appended after the hold. The
     * SuperSpeed halves configured; hub pairs that look like the two halves
     * of one unit (a counter and nothing more); SET_HUB_DEPTH refused (the
     * hub not served); warm resets of SuperSpeed hub ports, and links given
     * up after the per-port budget - the device left to the hub's USB 2.0
     * half; C_PORT_CONFIG_ERROR seen; and devices behind a SuperSpeedPlus
     * hub given a SuperSpeedPlus Protocol Speed ID from the extended port
     * status, or given SuperSpeed's because none matched the rate. */
    ULONG SsHubsStarted;
    ULONG SsHubPairs;
    ULONG SsHubDepthRefused;
    ULONG SsHubWarmResets;
    ULONG SsHubLinksGivenUp;
    ULONG SsHubConfigErrors;
    ULONG SsHubDevicesPlus;
    ULONG SsHubRateUnmatched;
} XHCIHC_COUNTERS, *PXHCIHC_COUNTERS;

XHCI_C_ASSERT(xhcihc_counters_all_ulong,
              sizeof(XHCIHC_COUNTERS) == 63 * sizeof(ULONG));

#endif /* XHCI_COUNTERS_H */
