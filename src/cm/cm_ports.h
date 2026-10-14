/*
 * cm_ports.h - the pure half of cm_reenum.c (roadmap-usb4.md tasks 39-A.5,
 * driver half, and 39-A.8; design record 21 sections 3.3, 6.1, 6.3 and
 * 7.4): what the re-enumeration and safe eject decide without touching a
 * kernel service, kept apart so the host suite (test\test_cm_ports.c)
 * reaches it.
 *
 *   - the families of record 21 section 3.3: from an NHI's id, its family's
 *     NHI ids, discrete switch port ids and integrated tunnel root port
 *     ids, the three lists tbt_topo.c's discovery is given;
 *   - Windows 98's device ids (section 6.1): the bus, device and function
 *     of a PCI devnode's instance part, and the vendor and device of its
 *     hardware part;
 *   - the bridge filter's snapshot (section 7.4): which PDOs of a
 *     BusRelations answer were not in the previous one;
 *   - the resume hold's watchdog bound (section 6.3).
 *
 * C89, DDK-free, no allocation, no lock. IRQL: any.
 */

#ifndef CM_PORTS_H
#define CM_PORTS_H

#include "../xhci_compat.h"

/* A packed PCI id, as tbt_topo.h takes it: vendor 15:0, device 31:16. */
#define CM_PORTS_ID(ven, dev)   ((((ULONG)(dev) & 0xFFFFUL) << 16) | \
                                 ((ULONG)(ven) & 0xFFFFUL))

typedef struct _CM_FAMILY {
    const char *Name;
    const ULONG *NhiIds;
    ULONG NhiIdCount;
    const ULONG *SwitchPortIds;     /* discrete families               */
    ULONG SwitchPortIdCount;
    const ULONG *RootPortIds;       /* integrated families             */
    ULONG RootPortIdCount;
} CM_FAMILY, *PCM_FAMILY;

/*
 * The family whose NHI list holds `nhiId` (CM_PORTS_ID form), or NULL
 * for an id record 21 section 3.3 does not list. A NULL family still
 * lets the discovery find an override's or an SHPC bridge's set.
 */
const CM_FAMILY *CmPortsFamily(ULONG nhiId);

/* How many families the table holds, and the n-th, for the host suite. */
ULONG CmPortsFamilyCount(VOID);
const CM_FAMILY *CmPortsFamilyAt(ULONG n);

/*
 * A Windows 98 PCI device id, "PCI\VEN_vvvv&DEV_dddd...\BUS_bb&DEV_dd&
 * FUNC_ff" (ANSI, at most `chars` characters, ending at the first NUL):
 *
 * CmPortsIdLocation: TRUE with *bdf (XHCI_TBT_BDF form: bus 15:8, device
 * 7:3, function 2:0) when the instance part is the BUS_&DEV_&FUNC_ form,
 * each field one or two hexadecimal digits, device up to 1F and function
 * up to 7. FALSE for anything else, among them the other instance forms
 * a hot-added function can take ("082800" in the experiment), which are
 * never matched by location.
 *
 * CmPortsIdVenDev: TRUE with *id (CM_PORTS_ID form) when the id starts
 * "PCI\VEN_" with four hexadecimal digits, then "&DEV_" with four. Case
 * is ignored throughout.
 */
BOOLEAN CmPortsIdLocation(const char *id, ULONG chars, PULONG bdf);
BOOLEAN CmPortsIdVenDev(const char *id, ULONG chars, PULONG id32);

/*
 * The snapshot of section 7.4: for each of the `nowCount` PDOs in now[],
 * isNew[i] is 1 when it is not among the `oldCount` of old[] and 0 when it
 * is; compared by pointer, never dereferenced. Returns how many are new.
 */
ULONG CmPortsNewPdos(PVOID const *old, ULONG oldCount, PVOID const *now,
                     ULONG nowCount, PULONG isNew);

/*
 * The resume hold's watchdog (section 6.3): the NHI's own resume bound
 * plus CM_PORTS_HOLD_EXTRA_MS. The resume is every synchronous stage of
 * the client's bring-up: the platform's readiness (CM_PORTS_HOLD_PLATFORM_MS,
 * the integrated hosts' force power and firmware-ready wait with the NVM
 * authentication poll, tbt_hw.h), driver ready's `readyMs` per attempt over
 * 1 + `readyRetries` attempts, the host-router poll's `hostPollTries` x
 * `hostPollMs`, the reconciliation's `reconcileMs`, and for each of
 * `devices` re-announced devices one ordinary command (the route query)
 * and one approve, each over 1 + `retries` attempts of `cmdMs` and
 * `approveMs`. Held to CM_PORTS_HOLD_MAX_MS, so no timeout can overflow;
 * the cap is far above any bound the client's own limits allow (sixteen
 * devices at the longest timeouts come to about two hours).
 */
typedef struct _CM_PORTS_RESUME {
    ULONG ReadyMs;
    ULONG ReadyRetries;
    ULONG HostPollTries;
    ULONG HostPollMs;
    ULONG ReconcileMs;
    ULONG Devices;
    ULONG CmdMs;
    ULONG ApproveMs;
    ULONG Retries;
} CM_PORTS_RESUME;

#define CM_PORTS_HOLD_EXTRA_MS      10000UL
#define CM_PORTS_HOLD_PLATFORM_MS   5000UL
#define CM_PORTS_HOLD_MAX_MS        86400000UL  /* a day: saturation only */
ULONG CmPortsHoldBound(const CM_PORTS_RESUME *resume);

#endif /* CM_PORTS_H */
