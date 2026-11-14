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
 *   - the 9x resume's wait for Windows' restore of a tunnel port
 *     (section 6.1.3, task 48.35);
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

/* TRUE when `id` (CM_PORTS_ID form) is a root port id of a family whose
 * NHIs are AMD's: the ports whose filter reads _DSD for the ACPI-named
 * source (design record 22 section 7.6.1). */
BOOLEAN CmPortsAcpiPort(ULONG id);

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
 * The settle wait before a successful NT resume is published (record 22
 * section 11.1.14, task 49.11): whether to read again, given how many of
 * the `waited` members show their function (`shown`), the ms since the
 * first reading (`elapsedMs`, a difference of a free-running clock taken
 * modulo 2^32) and the bound. TRUE while some member has not shown and
 * the bound has not passed. CM_PORTS_SETTLE_MS is the bound the resume
 * uses: the PCI Express Base Specification's 1.0 s (section 6.6.1) plus
 * margin for an emulated or slow link and a coarse tick.
 */
#define CM_PORTS_SETTLE_MS          5000UL
BOOLEAN CmPortsSettleAgain(ULONG shown, ULONG waited, ULONG elapsedMs,
                           ULONG boundMs);

/*
 * A release held behind a member's stor98 gate, run once the gate falls
 * (record 22 section 11.1.14 item 7): a successful resume's release on the
 * filter route exposes first (EXPOSE) where the host interface is there;
 * behind record 22 section 7.7's retirement barrier the exposure is owed
 * (OWE: the member's missed call with its exposure, made once the barrier
 * lifts); a failed resume's release, another route, or no transport:
 * nothing (NONE), as before.
 */
#define CM_PORTS_DX_NONE            0UL
#define CM_PORTS_DX_EXPOSE          1UL
#define CM_PORTS_DX_OWE             2UL
ULONG CmPortsDeferredExpose(ULONG failed, ULONG filterRoute,
                            ULONG transport, ULONG barred);

/*
 * Record 21 section 6.1.3 (task 48.35): after a standby on Windows 98 SE
 * and ME a member's operation waits until Windows has put the tunnel port
 * bridge's bus numbers back. `buses` is the bridge's dword at 18h
 * (primary, secondary, subordinate), `read` FALSE when it could not be
 * read. At the suspend: TRUE when the member is to wait (it read, not all
 * ones, with a secondary bus). At the operation: CM_PORTS_RW_RESTORED once
 * it reads a secondary bus again, CM_PORTS_RW_UNREAD for a read refused or
 * all ones (the bridge gone or silent: no wait), CM_PORTS_RW_TIMEOUT once
 * `elapsedMs` reaches `boundMs`, else CM_PORTS_RW_WAIT.
 */
#define CM_PORTS_RW_WAIT            0UL
#define CM_PORTS_RW_RESTORED        1UL
#define CM_PORTS_RW_TIMEOUT         2UL
#define CM_PORTS_RW_UNREAD          3UL
#define CM_PORTS_RW_BOUND_MS        20000UL
BOOLEAN CmPortsResumeWaitWanted(BOOLEAN read, ULONG buses);
ULONG CmPortsResumeWait(BOOLEAN read, ULONG buses, ULONG elapsedMs,
                        ULONG boundMs);

/*
 * ... the marks' state, kept under the caller's lock: Wait the members
 * marked, Asleep nonzero between the suspend and the resume, From the
 * resume's clock, Epoch advanced by every suspend and every resume that
 * ended a sleep. A check that read the state, dropped the lock for its
 * configuration read and comes back clears its member's mark only through
 * CmPortsRwClear with the Epoch it read: a suspend or a resume in between
 * (another standby) leaves the mark it set (the check is made again).
 */
typedef struct _CM_PORTS_RWS {
    ULONG Wait;
    ULONG Asleep;
    ULONG From;
    ULONG Epoch;
} CM_PORTS_RWS;
/* The suspend: `mask` marked (earlier marks replaced), asleep when any. */
VOID CmPortsRwSuspend(CM_PORTS_RWS *s, ULONG mask);
/* A resume end at `now`: TRUE when it ended a sleep (the bound starts). */
BOOLEAN CmPortsRwResumed(CM_PORTS_RWS *s, ULONG now);
/* Member `bit`'s mark cleared when the Epoch is still `epoch` and the
 * state awake: TRUE when cleared (or not set), FALSE when kept. */
BOOLEAN CmPortsRwClear(CM_PORTS_RWS *s, ULONG bit, ULONG epoch);

/*
 * Record 21 section 6.1.4 (task 49.12): the tunnel port bridge's type 1
 * header put back from the suspend's copy, where Windows has not restored
 * it. A selected-register restore, not a whole one: the bus numbers, the
 * windows, the bridge's own BARs, cache line and latency, interrupt line
 * and bridge control, and the command last; never the id, class,
 * capability pointer, expansion ROM or anything above 3Ch.
 *
 * CmPortsHdrCopyValid: whether the sixteen dwords read at the suspend
 * (`hdr`, 00h to 3Ch; `answered` how many read) make a copy worth writing
 * back: all sixteen read, the id answering (vendor not 0000h, FFFFh or
 * 0001h), header type (0Eh bits 6:0) 1, primary < secondary <= subordinate,
 * and Secondary Bus Reset (3Ch bit 22) clear.
 * CmPortsHdrEligible: whether the bridge as read now is reset as a whole -
 * secondary and subordinate 0 and the command's I/O, memory and bus master
 * enables (bits 2:0) clear - so nothing it decodes changes while the
 * windows are written; anything else is Windows' work in progress, and
 * nothing is written.
 * CmPortsRwRestoreDue: whether a check whose reading still says
 * CM_PORTS_RW_WAIT reaches the write-back now: once per resume (`done`
 * 0), only past CM_PORTS_RW_GRACE_MS since the resume (Windows' own
 * restore first).
 * CM_PORTS_HDR_ORDER[] is the order of the writes before the command;
 * CmPortsHdrRestoreValue the dword written at `offset` (CM_PORTS_HDR_CMD
 * included) from the copy and the dword read now (`now`): status halves
 * (04h, 1Ch) and Discard Timer Status (3Ch bit 26), all write-one-to-clear,
 * written as zero; 0Ch's upper half (header type, BIST) as read now with
 * BIST Start (bit 30) zero. CmPortsHdrVerify: whether the dword read back
 * at `offset` shows the write took (the bus numbers' low 24 bits, 1Ch's
 * lower half, the windows and BARs whole, the command's bits 2:0); 0Ch
 * and 3Ch are not judged.
 */
#define CM_PORTS_RW_GRACE_MS        2000UL
#define CM_PORTS_RW_RESTORED_HERE   5UL
#define CM_PORTS_RW_RESTORE_FAILED  6UL
#define CM_PORTS_HDR_DWORDS         16UL
#define CM_PORTS_HDR_WRITES         11UL
#define CM_PORTS_HDR_CMD            0x04UL
extern const ULONG CM_PORTS_HDR_ORDER[CM_PORTS_HDR_WRITES];
BOOLEAN CmPortsHdrCopyValid(const ULONG *hdr, ULONG answered);
BOOLEAN CmPortsHdrEligible(ULONG commandNow, ULONG busesNow);
BOOLEAN CmPortsRwRestoreDue(ULONG outcome, ULONG done, ULONG elapsedMs);
ULONG CmPortsHdrRestoreValue(ULONG offset, ULONG copy, ULONG now);
BOOLEAN CmPortsHdrVerify(ULONG offset, ULONG copy, ULONG readBack);

/*
 * Record 21 section 6.1.4 item 6: the functions behind the bridge. At the
 * suspend, function 0 of each device number of the secondary bus that
 * answers with a type 0 header, up to CM_PORTS_EP_MAX, has its id, command,
 * six BARs and interrupt line copied (CM_PORTS_EP_DWORDS: 00h, 04h, 0Ch,
 * 10h to 24h, 3Ch). After the bridge's write-back, each such function that
 * reads back from reset - the same id, header type 0, its command's I/O,
 * memory and bus master enables clear and every BAR's address bits 0 -
 * has the BARs and interrupt line written back, each read back, then the
 * command. CmPortsEpCopyValid: whether a function's copy is worth writing
 * back (an answering id, header type 0). CmPortsEpReset: whether the
 * function as read now (`cmd`, `bars` the six BAR dwords) is from reset
 * (a 64-bit memory BAR's upper half, the next dword, read whole as 0).
 * CmPortsEpVerify: whether a BAR read back shows the write took (its
 * address bits, those above the type bits: 4 for memory, 2 for I/O).
 */
#define CM_PORTS_EP_MAX             4UL
#define CM_PORTS_EP_DWORDS          10UL
extern const ULONG CM_PORTS_EP_OFFSET[CM_PORTS_EP_DWORDS];
BOOLEAN CmPortsEpCopyValid(ULONG id, ULONG typeDword);
/* A function's whole copy (CM_PORTS_EP_DWORDS, in CM_PORTS_EP_OFFSET's
 * order) worth keeping: CmPortsEpCopyValid on its id and 0Ch, no dword
 * all ones, and its id read the same again after it (`idAgain`). */
BOOLEAN CmPortsEpSnapValid(const ULONG *ep, ULONG idAgain);
BOOLEAN CmPortsEpReset(ULONG cmd, const ULONG *bars);
BOOLEAN CmPortsEpVerify(ULONG copy, ULONG readBack);

/*
 * Record 21 section 6.1.5 (task 49.30): a switch right behind the tunnel
 * port that came back from a standby reset, under a devnode that stayed,
 * is removed and found again. Nothing below the tunnel port is written.
 *
 * The note (CM_PORTS_SUB): the bridges on the member's secondary bus that
 * read configured at a suspend, each its device and function number
 * (device in 7:3, function in 2:0) and its id. CmPortsSubNoted: whether a
 * function read at the suspend is noted (an answering id, a type 1
 * header, a secondary bus other than 0, nothing all ones).
 * CmPortsSubMerge: the note after a suspend that read `fresh` - fresh's
 * entries, then the entries the note held (pending) at another device and
 * function number, while there is room.
 *
 * The reading after the wake, per noted entry (CmPortsSubRead), from the
 * function's id, header type dword (0Ch), command and bus numbers as read
 * now (`reads`: CM_PORTS_SUB_R_*, which of the four answered) and the
 * time since the resume: CONFIGURED (the noted id, type 1, a secondary
 * bus), LOST (the noted id, type 1, reset as a whole:
 * CmPortsHdrEligible), OTHER (another id, or the noted id with another
 * header type), ABSENT (nothing answering, CM_PORTS_SUB_ABSENT_MS or more
 * after the resume), UNDECIDED (anything else: a read refused, Retry
 * Status, a field all ones, part-configured, nothing answering yet).
 * CmPortsSubVerdict over the entries: LOST when any is, else UNDECIDED
 * when any is, else SETTLED.
 *
 * CmPortsLostStep: what the member's check does with the verdict -
 * `outcome` the wait's own (CM_PORTS_RW_RESTORED by Windows or
 * CM_PORTS_RW_RESTORED_HERE), `elapsedMs` since the resume, `tried` and
 * `sinceTryMs` the last recovery attempt:
 *   CM_PORTS_LOST_END       the wait ends with its outcome, the note
 *                           emptied (settled);
 *   CM_PORTS_LOST_END_KEEP  undecided at CM_PORTS_RW_BOUND_MS: the wait
 *                           ends with its outcome, the note kept;
 *   CM_PORTS_LOST_WAIT      parked: undecided, or lost inside Windows'
 *                           grace (CM_PORTS_RW_GRACE_MS, a member Windows
 *                           restored), or lost with the last attempt less
 *                           than CM_PORTS_SUB_RETRY_MS ago;
 *   CM_PORTS_LOST_REMOVE    a recovery attempt now;
 *   CM_PORTS_LOST_KEPT      lost at the bound: the wait ends
 *                           CM_PORTS_RW_LOST_KEPT, the note kept.
 */
#define CM_PORTS_RW_LOST_REMOVED    7UL
#define CM_PORTS_RW_LOST_KEPT       8UL
#define CM_PORTS_SUB_MAX            8UL
#define CM_PORTS_SUB_ABSENT_MS      5000UL
#define CM_PORTS_SUB_RETRY_MS       2000UL
typedef struct _CM_PORTS_SUB {
    ULONG Count;
    ULONG DevFn[CM_PORTS_SUB_MAX];
    ULONG Id[CM_PORTS_SUB_MAX];
} CM_PORTS_SUB;
BOOLEAN CmPortsSubNoted(ULONG id, ULONG typeDword, ULONG buses);
VOID CmPortsSubMerge(CM_PORTS_SUB *note, const CM_PORTS_SUB *fresh);

#define CM_PORTS_SUB_R_ID           0x1UL
#define CM_PORTS_SUB_R_TYPE         0x2UL
#define CM_PORTS_SUB_R_CMD          0x4UL
#define CM_PORTS_SUB_R_BUSES        0x8UL
#define CM_PORTS_SUB_CONFIGURED     0UL
#define CM_PORTS_SUB_LOST           1UL
#define CM_PORTS_SUB_OTHER          2UL
#define CM_PORTS_SUB_ABSENT         3UL
#define CM_PORTS_SUB_UNDECIDED      4UL
ULONG CmPortsSubRead(ULONG notedId, ULONG reads, ULONG id, ULONG typeDword,
                     ULONG cmd, ULONG buses, ULONG elapsedMs);
#define CM_PORTS_SUBV_SETTLED       0UL
#define CM_PORTS_SUBV_LOST          1UL
#define CM_PORTS_SUBV_UNDECIDED     2UL
ULONG CmPortsSubVerdict(const ULONG *entries, ULONG count);

#define CM_PORTS_LOST_END           0UL
#define CM_PORTS_LOST_END_KEEP      1UL
#define CM_PORTS_LOST_WAIT          2UL
#define CM_PORTS_LOST_REMOVE        3UL
#define CM_PORTS_LOST_KEPT          4UL
ULONG CmPortsLostStep(ULONG outcome, ULONG verdict, ULONG elapsedMs,
                      BOOLEAN tried, ULONG sinceTryMs);

/*
 * Record 21 section 6.1.5 item 7: whether a bridge may be re-enumerated,
 * from what its secondary bus and its child devnodes read now.
 * CmPortsResetFn, one function (`reads`: CM_PORTS_SUB_R_ID, _TYPE and
 * _BUSES, which answered): ABSENT (its id all ones or vendor FFFFh), SAFE
 * (not a type 1 header, or a secondary bus other than 0), BUS0 (type 1,
 * secondary bus 0), UNKNOWN (a read needed not made or all ones, vendor
 * 0000h or 0001h). CmPortsResetChildOf, one child devnode of the bridge
 * against a BUS0 function (`id` its vendor and device, `fbdf` its place,
 * `leaf` its device and function byte; `text` its instance id, NULL when
 * not read): MATCH (that function's), NOMATCH, UNREAD (not read, or the
 * function's vendor and device with no place that parses).
 * CmPortsResetChildAdd / CmPortsResetChildEnd fold a walk of the
 * children (`complete`: read to its end): FOUND, NONE (complete, no
 * match), UNKNOWN. CmPortsResetJoin folds the functions into the bus's
 * answer: RESET (a BUS0 function with its devnode found) over UNKNOWN (a
 * function UNKNOWN, or BUS0 with its children UNKNOWN) over SAFE.
 */
#define CM_PORTS_RF_ABSENT          0UL
#define CM_PORTS_RF_SAFE            1UL
#define CM_PORTS_RF_BUS0            2UL
#define CM_PORTS_RF_UNKNOWN         3UL
ULONG CmPortsResetFn(ULONG reads, ULONG id, ULONG typeDword, ULONG buses);
#define CM_PORTS_RC_NOMATCH         0UL
#define CM_PORTS_RC_MATCH           1UL
#define CM_PORTS_RC_UNREAD          2UL
ULONG CmPortsResetChildOf(const char *text, ULONG chars, ULONG id,
                          ULONG fbdf, ULONG leaf);
#define CM_PORTS_RCL_NONE           0UL
#define CM_PORTS_RCL_FOUND          1UL
#define CM_PORTS_RCL_UNKNOWN        2UL
ULONG CmPortsResetChildAdd(ULONG acc, ULONG child);
ULONG CmPortsResetChildEnd(ULONG acc, BOOLEAN complete);
#define CM_PORTS_RU_SAFE            0UL
#define CM_PORTS_RU_RESET           1UL
#define CM_PORTS_RU_UNKNOWN         2UL
ULONG CmPortsResetJoin(ULONG verdict, ULONG fn, ULONG children);
/* CmPortsResetBare: a bridge whose secondary bus is not read (its member in
 * the no-configuration mode, or its own secondary bus 0) from its child
 * devnodes' list (`complete`, `count`): SAFE only when complete and empty,
 * else UNKNOWN. */
ULONG CmPortsResetBare(BOOLEAN complete, ULONG count);

/*
 * ... a no-IRQ operation's admission (record 28; record 21 section 6.1.3):
 * CM_PORTS_NI_CLOSED when the member's gate is shut (the operation not
 * made, a failure as before), else CM_PORTS_NI_HELD while `bit` is in the
 * marks `wait` (a suspension: not made, not failed, kept outstanding for
 * after the wait), else CM_PORTS_NI_GO.
 */
#define CM_PORTS_NI_CLOSED          0UL
#define CM_PORTS_NI_GO              1UL
#define CM_PORTS_NI_HELD            2UL
ULONG CmPortsNiAdmit(BOOLEAN open, ULONG wait, ULONG bit);

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

/*
 * Windows 98 and ME, design record 34 section 3.3.7 item 1: TRUE while a
 * chain operation on member `i`, taken at generation `taken`, may still
 * read configuration space below the member or call CONFIGMG for it: the
 * worker not leaving, the member owned (`active`), its generation now
 * (`genNow`) the one taken, and the member in none of `held`, the members
 * owing CONTROLLER_UNAVAILABLE's removal or its no-scan re-enumeration, or
 * under a retirement barrier. The worker takes `taken` before it reads the
 * gates, so a loss posted after that read still makes the operation stale:
 * every post that owes a removal or a no-scan re-enumeration also advances
 * the generation.
 */
BOOLEAN CmPortsChainCurrent(BOOLEAN leaving, ULONG active, ULONG held,
                            ULONG genNow, ULONG taken, ULONG i);

/*
 * The systems of design record 31 sections 4.8 and 4.10 (task 48.29),
 * from the kernel's shared data page's NtMajorVersion and NtMinorVersion
 * (6.3 on 8.1, 10.0 on 10, whatever the image's manifest says).
 * CmPortsTagOs: 1 where the child gives the tunnel id, NT 5.0 to 6.3
 * (2000 to 8.1). CmPortsKeptOs: 1 where the bridge's child is kept
 * attached across a removal of its device's stack (section 4.10, K1),
 * NT 6.3 alone.
 */
ULONG CmPortsTagOs(ULONG major, ULONG minor);
ULONG CmPortsKeptOs(ULONG major, ULONG minor);

/*
 * The kept child's state (design record 31 section 4.10, K3): one value
 * under the child's lock. CmPortsKeptStep gives the state after `event`
 * and, in *action, what the caller does for it (CM_KEPT_A_*, any of them
 * or none). CmPortsKeptAdmits: 1 when a dispatch may take a reference in
 * that state (every state but Detached). CmPortsKeptLast: what the release
 * that left `count` references does, in `state`, at an IRQL above
 * PASSIVE_LEVEL or not: nothing, the destruction now, or the destruction
 * handed to the child's work item.
 */
#define CM_KEPT_ATTACHED        0UL
#define CM_KEPT_STARTING        1UL
#define CM_KEPT_LOGICAL         2UL
#define CM_KEPT_DETACHED        3UL

#define CM_KEPT_E_REMOVED       0UL /* the forwarded removal returned     */
#define CM_KEPT_E_START         1UL /* a start admitted, before forwarding */
#define CM_KEPT_E_START_OK      2UL /* the start completed with success   */
#define CM_KEPT_E_START_FAIL    3UL /* the start failed below             */
#define CM_KEPT_E_CALLBACK      4UL /* FastIoDetachDevice for the object  */

#define CM_KEPT_A_DEPART        0x01UL  /* record the departure (once)    */
#define CM_KEPT_A_PRESENT       0x02UL  /* presence back (KeptLogical 0)  */
#define CM_KEPT_A_ABSENT        0x04UL  /* presence away (KeptLogical 1)  */
#define CM_KEPT_A_END_DEPART    0x08UL  /* the entry brought up to date   */
#define CM_KEPT_A_DETACH        0x10UL  /* detach from the object below   */
#define CM_KEPT_A_DROP_ATTACH   0x20UL  /* release the attach's reference */
ULONG CmPortsKeptStep(ULONG state, ULONG event, PULONG action);
ULONG CmPortsKeptAdmits(ULONG state);

#define CM_KEPT_L_NONE          0UL
#define CM_KEPT_L_DESTROY       1UL
#define CM_KEPT_L_QUEUE         2UL
ULONG CmPortsKeptLast(ULONG count, ULONG state, ULONG aboveP);

/*
 * The wake power-up (design record 21 section 6.3.1, task 49.46), its pure
 * rules. An epoch is a count of system sleeps (one per sleep, whatever the
 * number of domains), 0 never one. CmPortsWpuArm: 1 when a device D0 that
 * the child filter dispatched in epoch `dispatchEpoch` and that completed
 * `failed` (with STATUS_NO_SUCH_DEVICE, the one status observed; any other
 * failure is counted, never armed on) arms its member in `epoch` (the
 * current one) whose ConsumedEpoch is `consumed`. CmPortsWpuStep: the thread's verdict
 * for one member, CM_WPU_V_*; for CM_WPU_V_SKIP the reason in *why
 * (CM_WPU_WHY_*, the cm.wakepowerup.skip note's). CmPortsWpuDue: 1 when
 * `elapsed` ms since the member's release is at least the delay.
 */
#define CM_WPU_DELAY_MS         5000UL
#define CM_WPU_WINDOW_MS        65000UL
#define CM_WPU_OVERDUE_MS       30000UL /* a request out longer: logged   */
#define CM_WPU_UNITS            4UL     /* units kept per route per filter */
#define CM_WPU_SLOTS            8UL     /* requests in flight, driver-wide */

#define CM_WPU_OWN_UNKNOWN      0UL
#define CM_WPU_OWN_STOR98       1UL
#define CM_WPU_OWN_OTHER        2UL

/* The owner from its two readings since the member's latest start, each
 * CM_WPU_OWN_*: the driver key's install section and the driver at the top
 * of the member's stack. Stor98's only when both say so; another's when
 * either says another (a contradicting reading is never overturned by the
 * other); else unknown, which fails closed. */
ULONG CmPortsWpuOwner(ULONG infSection, ULONG top);

/* The install-section reading: `text` (`chars` characters, not
 * terminated) is the driver key's InfSection. CM_WPU_OWN_STOR98 for
 * Stor98_Inst, Stor98_Inst.NTx86 or Stor98_Inst.NTamd64 (ASCII case
 * aside: Windows 7 records the undecorated name, Windows 10 the decorated
 * one), CM_WPU_OWN_OTHER for any other text. */
ULONG CmPortsWpuInfSection(const WCHAR *text, ULONG chars);

typedef struct _CM_WPU_IN {
    ULONG Epoch;            /* the current epoch                          */
    ULONG FailEpoch;        /* the member's last arm, 0 none              */
    ULONG Consumed;         /* the epoch it was last asked or skipped in  */
    ULONG Switch;           /* XhciCmWakePowerUp                          */
    ULONG Closed;           /* a removal seen on one of its filters       */
    ULONG Released;         /* its tunnel port released in this epoch,
                               the domain open and not suspended          */
    ULONG Elapsed;          /* ms since that release                      */
    ULONG Owner;            /* CM_WPU_OWN_*                               */
    ULONG Units;            /* units known and current                    */
    ULONG Slots;            /* request slots free                         */
    ULONG Outstanding;      /* a request for one of its units still out   */
} CM_WPU_IN;

#define CM_WPU_V_IDLE           0UL     /* not armed, or done this epoch   */
#define CM_WPU_V_WAIT           1UL     /* armed, look again next pass     */
#define CM_WPU_V_ASK            2UL
#define CM_WPU_V_SKIP           3UL     /* consumed without a request      */
#define CM_WPU_V_NONE           4UL     /* consumed: no unit known in time */

#define CM_WPU_WHY_REMOVED      1UL
#define CM_WPU_WHY_OFF          3UL
#define CM_WPU_WHY_BUSY         4UL
#define CM_WPU_WHY_NOT_STOR98   5UL
#define CM_WPU_WHY_OUTSTANDING  6UL     /* never a second request at once */
#define CM_WPU_WHY_CLOSED       7UL     /* closed between admission and send */

ULONG CmPortsWpuArm(ULONG dispatchEpoch, ULONG epoch, ULONG consumed,
                    ULONG failed);
ULONG CmPortsWpuDue(ULONG elapsed);
ULONG CmPortsWpuStep(const CM_WPU_IN *in, PULONG why);

/*
 * The wake power-up's gate (design record 21 section 6.3.1): when an ask
 * may be admitted at all. CM_WPU_SYS is the system's (one for the driver),
 * CM_WPU_DOM a domain's (one per NHI); both under the driver's gate lock.
 * The system part: Epoch counts system sleeps (one per sleep, at the first
 * SET below S0, Claim cleared by the SET to S0); Halt is set at every
 * QUERY or SET below S0 and HaltSeq counts those events. The domain part:
 * StopLatch is set at the NHI's query-stop, query-remove, stop, surprise
 * removal and removal and cleared only by that NHI's own cancel or start;
 * PowerDown at the NHI's own device power-down, cleared only by its own
 * accepted release; ResumeEpoch and ResumeHaltSeq are taken when the NHI's
 * resume work begins. A release is accepted (and stamps the epoch) only
 * when no sleep and no halt event came since that resume began and the
 * domain is not latched; an accepted release clears Halt and the domain's
 * PowerDown, never another domain's latch. DownSeq counts the device
 * power-downs any filter or NHI of the driver received (CmPortsWpuSysDown);
 * a domain's release records it (RelDownSeq) and a later one closes every
 * domain until that domain's own next release; HaltSeq likewise
 * (RelHaltSeq), so a sleep query after a domain's release keeps it closed
 * whatever another domain's later release does. CmPortsWpuDomOpen: 1 when
 * an ask behind a port released in `relEpoch` may be admitted now.
 */
typedef struct _CM_WPU_SYS {
    ULONG Epoch;
    ULONG Claim;
    ULONG Halt;
    ULONG HaltSeq;
    ULONG DownSeq;
} CM_WPU_SYS;

typedef struct _CM_WPU_DOM {
    ULONG StopLatch;
    ULONG PowerDown;
    ULONG ResumeEpoch;
    ULONG ResumeHaltSeq;
    ULONG Resuming;         /* a resume began and its release is owed     */
    ULONG RelDownSeq;       /* DownSeq at the accepted release            */
    ULONG RelHaltSeq;       /* HaltSeq at the accepted release            */
} CM_WPU_DOM;

VOID CmPortsWpuSysPower(CM_WPU_SYS *s, ULONG isSet, ULONG toSleep);
VOID CmPortsWpuSysDown(CM_WPU_SYS *s);
VOID CmPortsWpuDomStop(CM_WPU_DOM *d);
VOID CmPortsWpuDomCancel(CM_WPU_DOM *d);
VOID CmPortsWpuDomStart(CM_WPU_DOM *d);
VOID CmPortsWpuDomPowerDown(CM_WPU_DOM *d);
VOID CmPortsWpuDomResumeBegin(const CM_WPU_SYS *s, CM_WPU_DOM *d);
ULONG CmPortsWpuDomRelease(CM_WPU_SYS *s, CM_WPU_DOM *d);
ULONG CmPortsWpuDomOpen(const CM_WPU_SYS *s, const CM_WPU_DOM *d,
                        ULONG relEpoch);

#endif /* CM_PORTS_H */
