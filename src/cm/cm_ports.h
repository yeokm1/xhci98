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
 *   - the resume's logged bounds (section 6.3; diagnostics since task 43.24h);
 *   - design record 31's tunnel id: the compatible-id rewrite, the chain's
 *     depth and the Removable answer's gate.
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

/* The bridge ids xhcicmf.inf names that no family carries (cm_ports.c says
 * why each), for the host suite. 0 past the end. */
ULONG CmPortsUnpairedCount(VOID);
ULONG CmPortsUnpairedAt(ULONG n);

/* TRUE when `id` (CM_PORTS_ID form) is one of xhcicmf.inf's bridge models:
 * a family's switch or root port id, or an unpaired one (the host suite
 * holds the two equal both ways). AddDevice's bridge role (cm_vals.h
 * CmRoleDecide). */
BOOLEAN CmPortsFilterId(ULONG id);

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

/*
 * The other instance form Windows 98 SE gives a PCI function behind a
 * PCI-to-PCI bridge (read on the emulator's Alpine Ridge tree, 2026-10-15:
 * "083000" for 01:01.0 behind the root bridge 00:06.0, "08083000" for
 * 02:01.0 behind it, "0000083000" for 03:00.0 behind 02:00.0): the
 * function's path, two hexadecimal digits per element, leaf first - its own
 * device/function byte (device in 7:3, function in 2:0), then each parent
 * bridge's, up to the bridge on the root bus, then the root bus number.
 * CmPortsIdPath: TRUE with the elements in path[0..*count-1] when the
 * instance part is an even number (four or more) of hexadecimal digits and
 * nothing else; FALSE otherwise. At most `capacity` elements.
 */
#define CM_PORTS_PATH_MAX   16UL
BOOLEAN CmPortsIdPath(const char *id, ULONG chars, PULONG path,
                      ULONG capacity, PULONG count);
BOOLEAN CmPortsIdVenDev(const char *id, ULONG chars, PULONG id32);

/*
 * The snapshot of section 7.4: for each of the `nowCount` PDOs in now[],
 * isNew[i] is 1 when it is not among the `oldCount` of old[] and 0 when it
 * is; compared by pointer, never dereferenced. Returns how many are new.
 */
ULONG CmPortsNewPdos(PVOID const *old, ULONG oldCount, PVOID const *now,
                     ULONG nowCount, PULONG isNew);

/*
 * Whether a bridge's answer starts a new population generation, the one
 * an eject is posted against and checked at its hand-off (section 7.4,
 * task 43.70): 1 when a function newly answers (`fresh`, CmPortsNewPdos's
 * count) or the answer is longer than the snapshot, 0 for the same
 * population or a departure alone. Under native PCIe hot-plug Windows'
 * PCI driver turns the slot off as part of the eject and reports the
 * ejected function gone only after that, after a tool's own confirmation
 * was posted; a replacement is always a new function, so it still ends
 * the old generation.
 */
ULONG CmPortsNewGeneration(ULONG fresh, ULONG oldCount, ULONG nowCount);

/*
 * The log value of an eject dropped at its hand-off (task 43.70): bits
 * 0 to 3 the checks that refused it (CM_EJ_STALE_*), bits 8 to 15 the
 * bridge's generation now and 16 to 23 the eject's, each truncated to
 * its low byte.
 */
#define CM_EJ_STALE_OWNER    0x1UL  /* bridge gone or another domain's */
#define CM_EJ_STALE_SERIAL   0x2UL  /* a new publication of the bridge */
#define CM_EJ_STALE_GEN      0x4UL  /* a new population generation */
#define CM_EJ_STALE_STACK    0x8UL  /* a function behind it has a stack */
ULONG CmPortsStaleWhy(ULONG serialChanged, ULONG genChanged,
                      ULONG notReady, ULONG genNow, ULONG genEject);

/*
 * Whether a child filter goes on a PDO the snapshot found new (section 7.4
 * step 1, revision 2). `chain[0..chainCount-1]` is the driver object of
 * each device object already attached above the PDO, lowest first, as the
 * caller walked it (at most CM_CHILD_CHAIN_MAX + 1 entries, so a count
 * above CM_CHILD_CHAIN_MAX means "more than the walk allows");
 * `busDriver` is the PDO's own driver object, `self` this driver's and
 * `prevTruncated` the previous snapshot's SnapTruncated. Compared by
 * pointer, never dereferenced.
 *
 * CM_CHILD_ATTACH: nothing is attached, the case of revision 1.
 * CM_CHILD_ATTACH_OVER: only bus filters are attached - objects a driver
 * below the bridge filter put on the new PDO while the BusRelations answer
 * was completing (on an ACPI machine, ACPI's filter on a PCI-to-PCI
 * bridge). The PnP manager builds a new device's stack only after that
 * answer reaches it, so none of them can be the function driver; the
 * checks below hold that reasoning to what can be seen: no object of the
 * PDO's own driver (pci.sys, which is also a bridge's function driver),
 * none of this driver's, none without a driver, a short chain, and a
 * snapshot that was not truncated (beyond CM_SNAP_MAX "new" is not
 * reliable, and the PDO may carry a built stack).
 * CM_CHILD_SKIP: anything else; nothing is attached (STATUS_DEVICE_BUSY).
 */
#define CM_CHILD_SKIP           0UL
#define CM_CHILD_ATTACH         1UL
#define CM_CHILD_ATTACH_OVER    2UL
#define CM_CHILD_WALK           3UL
#define CM_CHILD_KEEP           4UL
#define CM_CHILD_CHAIN_MAX      4UL
ULONG CmPortsChildAttach(ULONG prevTruncated, PVOID const *chain,
                         ULONG chainCount, PVOID busDriver, PVOID self);

/*
 * The step before it, which decides whether the caller may walk the chain
 * at all. For a PDO new by a whole snapshot (`isNew` 1): CM_CHILD_ATTACH
 * when nothing is attached (`hasAttached` 0), CM_CHILD_SKIP when something
 * is and the previous snapshot was truncated (a PDO past CM_SNAP_MAX reads
 * as new on every answer and may carry a built stack, whose objects
 * another driver can detach and delete at any time, so they are not
 * dereferenced), CM_CHILD_WALK otherwise: walk the chain, then
 * CmPortsChildAttach.
 *
 * For a PDO already in the snapshot (`isNew` 0), the re-attach of design
 * record 31 section 4.3: CM_CHILD_ATTACH when no attachment of this
 * driver is on it any more (`hasOurs` 0) and nothing at all is attached
 * (`hasAttached` 0) - a device whose stack was taken down and not yet
 * built again, as the first-time install of a bridge below a tunnel port
 * does on Windows 2000 and XP, so the child goes back below the stack the
 * PnP manager builds next. CM_CHILD_KEEP otherwise: nothing is done and
 * nothing counted, and a built stack's objects are never walked.
 */
ULONG CmPortsChildFirst(ULONG prevTruncated, ULONG hasAttached, ULONG isNew,
                        ULONG hasOurs);

/*
 * ---- The tunnel id (design record 31 sections 3 and 4) ----
 *
 * A child filter of this driver rewrites the BusQueryCompatibleIDs answer
 * of an NVMe-class PCI function (PCI\CC_010802 among its compatible ids)
 * so that CM_TUNNEL_ID is the first compatible id; the hardware ids are
 * never touched. CmPortsTunnelIds reads one answer, a MULTI_SZ of 16-bit
 * characters (`list`, NULL for none), and:
 *
 *   CM_TID_LEAVE    no PCI\CC_010802 among the strings: left as it came;
 *   CM_TID_PRESENT  CM_TUNNEL_ID already among them: left (idempotent);
 *   CM_TID_OVER     no terminating empty string within CM_TID_MAX_CHARS
 *                   characters (4 KiB): left, and the caller counts it;
 *   CM_TID_TAG      to be rewritten: *chars is the new list's length in
 *                   characters, CM_TID_ID_CHARS more than the old one's
 *                   (both terminators included), and when `out` is not
 *                   NULL and `outChars` holds *chars the new list - the id,
 *                   then the old strings in their order, then the empty
 *                   string - is written there.
 *
 * Otherwise *chars is the old list's length (0 for NULL or OVER). *kind
 * gets CM_TID_KIND_NVME when PCI\CC_010802 is among the strings and
 * CM_TID_KIND_BRIDGE when PCI\CC_0604 is (a PCI-to-PCI bridge, the
 * chain's link of section 4.3). Strings are compared whole, ASCII case
 * ignored.
 */
#define CM_TUNNEL_ID            "PCI\\XHCI98_TUNNEL&CC_010802"
#define CM_TID_ID_CHARS         28UL    /* CM_TUNNEL_ID and its NUL      */
#define CM_TID_MAX_CHARS        2048UL  /* 4 KiB of WCHARs                */
#define CM_TID_LEAVE            0UL
#define CM_TID_TAG              1UL
#define CM_TID_PRESENT          2UL
#define CM_TID_OVER             3UL
#define CM_TID_KIND_NVME        0x1UL
#define CM_TID_KIND_BRIDGE      0x2UL
ULONG CmPortsTunnelIds(const USHORT *list, USHORT *out, ULONG outChars,
                       PULONG chars, PULONG kind);

/*
 * The chain's depth (record 31 section 4.3): the topmost bridge filter is
 * depth 0 and each child one more than the filter whose BusRelations
 * attached it. On a discrete controller that is its switch's upstream
 * port, two levels above the tunnel port (read on 7, 2026-10-25). A child
 * that is a PCI-to-PCI bridge reads its own BusRelations and attaches
 * children below it while its depth is below CM_TUNNEL_DEPTH_MAX: the
 * controller's two switch levels, a dock's upstream and downstream ports,
 * an enclosure's upstream and downstream ports and the function at depth
 * 7, with one to spare. 1 when a child at `depth` that is a bridge
 * (`isBridge`) does, 0 otherwise.
 */
#define CM_TUNNEL_DEPTH_MAX     8UL
ULONG CmPortsChainReads(ULONG depth, ULONG isBridge);

/*
 * The Removable answer's gate (record 21 section 7.4; record 31 section
 * 4.1), for one member of a listed domain on the filter route, against an
 * attachment located on bus `locBus` (`located`): CM_MM_YES when the
 * member is active and exclusive, its secondary bus is known and is that
 * bus, the marks table has an entry for the attachment's PDO (`inMarks`)
 * and the domain's XhciCmSafeEject is on; CM_MM_SEEN for all of that but
 * safe eject; CM_MM_NO otherwise. Every kind of attachment answers by it:
 * since record 31 a child attached at BusRelations exists on every
 * filtered bridge, not only on an active one with safe eject on.
 */
#define CM_MM_NO                0UL
#define CM_MM_SEEN              1UL
#define CM_MM_YES               2UL
ULONG CmPortsMayMark(ULONG located, ULONG locBus, ULONG active,
                     ULONG exclusive, ULONG secBusValid, ULONG secBus,
                     ULONG inMarks, ULONG safeEject);

/*
 * Whether a function behind a bridge is without a device stack, for the
 * eject's eligibility (section 7.4): 1 when nothing is attached to its PDO
 * (`hasAttached` 0), or when the only object of this driver's on it is the
 * child filter the bridge's BusRelations answer put there (`ownBusChild`,
 * no AddDevice-made one: `ownAddDev` 0) and nothing is attached above that
 * child (`aboveChild` 0). The second is a function the PnP manager found
 * no driver for (Code 28, read on 7 for an NVMe drive): its stack was
 * never built, so this driver's own child and any bus filter below it
 * stay on the PDO, and no function driver would see the tunnel go. A
 * function driver or an upper filter always attaches above the child. 0
 * otherwise, and when no child of this driver's is on it at all (another
 * driver's objects are not read).
 */
ULONG CmPortsStackless(ULONG hasAttached, ULONG ownBusChild, ULONG ownAddDev,
                       ULONG aboveChild);

/*
 * The resume's bound (section 6.3), logged as a diagnostic since task
 * 43.24h (below): the NHI's own resume bound
 * plus CM_PORTS_HOLD_EXTRA_MS. The resume is every synchronous stage of
 * the client's bring-up: the platform's readiness (CM_PORTS_HOLD_PLATFORM_MS,
 * the integrated hosts' force power and firmware-ready wait with the NVM
 * authentication poll, tbt_hw.h), driver ready's `readyMs` per attempt over
 * 1 + `readyRetries` attempts, the host-router poll's `hostPollTries` x
 * `hostPollMs`, the reconciliation's `reconcileMs`, and for each of
 * `devices` re-announced devices one ordinary command (the route query)
 * and one approve, each over 1 + `retries` attempts of `cmdMs` and
 * `approveMs`. Held to CM_PORTS_HOLD_MAX_MS, which is not arithmetic
 * saturation but the operating system's own limit: a device power IRP
 * retained much longer meets the power-IRP watchdog (Microsoft's bug check
 * 0x9F, DRIVER_POWER_STATE_FAILURE, about ten minutes), so the hold ends
 * at five minutes whatever the client's configured timeouts would allow.
 * Since task 43.24h no power IRP is retained (record 21 section 6.3: a
 * retained bridge D0 deadlocked the NT resume), so this bound and the cap
 * are diagnostics: cm_reenum.c logs them at the suspend and nothing waits
 * on either.
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
#define CM_PORTS_HOLD_MAX_MS        300000UL    /* 5 min: the OS's limit */
ULONG CmPortsHoldBound(const CM_PORTS_RESUME *resume);

/*
 * The five-minute cap, ms, logged for either profile: always
 * CM_PORTS_HOLD_MAX_MS. `clientMs` is the client's own resume bound (the
 * USB4 client's, or 0 when none is known); *over is set TRUE when that
 * bound plus CM_PORTS_HOLD_EXTRA_MS would pass the cap. A diagnostic since
 * task 43.24h: no power IRP waits on it (record 21 section 6.3).
 */
ULONG CmPortsHoldWatchdog(ULONG clientMs, BOOLEAN *over);

/*
 * The release of a member at the NHI's resume, stop or removal (record 21
 * section 6.3, task 43.24h): a member still marked by this NHI has its
 * mark cleared (CM_PORTS_REL_CLEAR) and, where re-enumeration is on, its
 * bridge invalidated (CM_PORTS_REL_INVALIDATE), whether the resume
 * succeeded or failed: the bridge's D0 went down at once, so the devices
 * behind it may have resumed against an absent tunnel. A member no longer
 * marked by it is left alone. The resume's outcome is not an input: only
 * the log names it.
 */
#define CM_PORTS_REL_CLEAR          0x1UL
#define CM_PORTS_REL_INVALIDATE     0x2UL
ULONG CmPortsRelease(ULONG markedByThis, ULONG reenumOn);

/*
 * Windows 98 and ME: a member's known children (devnode handles) kept only
 * while present among `present`, in order; returns the new count.
 * CONFIGMG reuses a freed devnode's handle, so an identity must be
 * forgotten as soon as its devnode leaves, however it left (the worker's
 * removal, the user's, Windows' own), or a re-plug given the same handle
 * is taken for the old child and never marked DN_REMOVABLE (record 21
 * section 7.4).
 */
ULONG CmPortsKeepPresent(PULONG known, ULONG count, const ULONG *present,
                         ULONG presentCount);

#endif /* CM_PORTS_H */
