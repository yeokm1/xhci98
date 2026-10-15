/*
 * cm_nvlink.h - the nvme98 link-state channel (roadmap-usb4.md tasks 39-A.7
 * for the ICM profile and 40-A.7 for the USB4 profile; design record 21
 * section 7, design record 22 section 10). The interface is nvme98's,
 * adopted by its maintainer decision MD-6 and defined in nvme98cm.h, which
 * sits beside this file byte for byte as nvme98 publishes it (its own
 * BSD-3-Clause notice; legal-provenance.md section 2a). This file is the
 * connection manager's side of it and adds nothing to that header.
 *
 * THE TABLE. One NVME98LS_TABLE in non-paged pool, allocated once per
 * driver load (CmNvlinkInit) and NEVER freed, not even at unload: a pointer
 * nvme98 took stays valid for the life of the system. One entry per
 * tunnelled NVMe function, keyed by its bus (the tunnel bridge's secondary
 * bus) and PCI_SLOT_NUMBER. The connection manager owns every field but
 * AckGeneration, and writes an entry only inside the odd/even Sequence
 * bracket (nvme98cm.h, "Entry protocol").
 *
 * RENDEZVOUS. Windows 2000 and later: the callback object
 * \Callback\Nvme98LinkState (ExCreateCallback, OBJ_PERMANENT) with one
 * callback that answers NVME98LS_QUERY with the table's address; nvme98
 * opens it once in HwFindAdapter. Windows 98 SE and ME: a registry record
 * written once per boot under HKEY_DYN_DATA (memory-resident, rebuilt at
 * every boot, so a record never outlives the boot whose pool it names -
 * nvme98 PLAN MD-5, "never trust a pointer across boots"), through the VMM
 * registry services: key CM_NVLINK_DYN_KEY, REG_BINARY value
 * CM_NVLINK_DYN_VALUE, CM_NVLINK_RECORD_BYTES long, laid out as
 * CmNvlinkRecordEncode writes it. nvme98cm.h defines the NT rendezvous
 * only; the 9x record and the REMOVE_PENDING request below are this
 * driver's PROPOSAL for nvme98 to adopt into nvme98cm.h, and nothing on
 * nvme98's side reads either yet (MD-5: designed, not prototyped).
 *
 * THE OBLIGATIONS, and where each is met:
 *  1. UP with a new link generation BEFORE the operating system can
 *     enumerate the endpoint: CmNvlinkExpose, called by the re-enumeration
 *     seam (cm_reenum.c) for a tunnel port after its tunnel-up poll and
 *     before the IoInvalidateDeviceRelations or Reenumerate_DevNode that
 *     exposes what is behind it. It reads the port's secondary bus and
 *     publishes UP for each NVMe function (class 01h, subclass 08h) that
 *     answers there. An entry already UP keeps its generation (the link
 *     did not go down); one that is not UP gets LinkGeneration + 1.
 *  2. DOWN_QUIESCED only after the tunnel's PCIe path is closed and the
 *     endpoint's link-down reset has completed, read on the conditions the
 *     record qualifies (record 21 section 7.1, record 22 section 10.2):
 *     ICM profile, CmNvlinkIcmDown - the domain's tunnel-port set has one
 *     member, it is the emulator's SHPC bridge (whose presence bit clearing
 *     after a disconnect is the documented later-than-stage-2 signal), and
 *     the tunnel-down poll REACHED a DOWN reading of it. USB4 profile,
 *     CmNvlinkPortDown from the CM_U4_DOWN_HOOK - TBT_U4_LINKDOWN.PathClosed
 *     (record 22 section 7.3 step 1 read back), the port's member known by
 *     correspondence, that member an SHPC bridge, its reading DOWN within
 *     the time left of the 3 s, and at least CM_NVLINK_DELIVERY_MS left.
 *     On a hardware tunnel port (Link Active, Presence Detect) the
 *     endpoint's completed reset cannot be observed through a down link,
 *     so no hardware statement is made (record 21 section 7.1); the entry
 *     then goes to IN_USE alone (UP cleared, no DOWN_QUIESCED), which
 *     nvme98 ignores, and its gone deadline (MD-1) applies.
 *  3. The acknowledgement before the removal: CmNvlinkAwait, called by the
 *     seam before any removal call it makes for a tunnel down (the
 *     invalidation that reports the endpoint missing on NT,
 *     Query_Remove_SubTree and Remove_SubTree on 9x). It waits until every
 *     entry this domain marked DOWN_QUIESCED reads AckGeneration ==
 *     LinkGeneration, or CM_NVLINK_ACK_MS since its mark, servicing the
 *     connection manager between reads. On NT it does not wait while no
 *     receiver has ever asked for the table (no nvme98 is loaded, so none
 *     can acknowledge); on 9x it does not wait where the record was never
 *     written.
 *  4. A tunnel re-established only to an endpoint reset since the last one
 *     (MD-2 obligation 1): by construction under the ICM (record 21 section
 *     7.1) and by the Detect gate under the USB4 profile (record 22 section
 *     10.1); nothing here.
 *
 * 9x CM-CONTROLLED REMOVAL (MD-5). CmNvlinkOrderlyRemove, for a removal of
 * a PRESENT NVMe function that this driver itself starts on Windows 98 SE
 * or ME: Query_Remove_SubTree first (the file system's quiescence and the
 * configuration manager's veto: a refusal is final, nothing is forced),
 * then REMOVE_PENDING on the entry with a fresh request sequence, then a
 * wait of CM_NVLINK_REMOVE_MS for nvme98's answer to that sequence; only a
 * CLEAN answer is followed by Remove_SubTree, anything else (not clean,
 * failed, no answer) cancels the request and leaves the devnode as it is.
 * Under the ICM there is no per-device teardown (record 21 section 7.4),
 * so nothing in that profile calls it; it is the call a 9x eject of the
 * USB4 profile makes, if that half lands.
 *
 * DDK-FREE PART. The functions under "pure" below use nothing but the
 * table and their arguments; test\test_cm_nvlink.c runs them on the host
 * (XHCI_HOST_TEST). The rest is cm_nvlinkos.c.
 *
 * IRQL: the pure functions at any IRQL. The driver calls at PASSIVE_LEVEL
 * on the NHI's connection-manager thread, no lock held, unless stated;
 * CmNvlinkInit and CmNvlinkUnload from DriverEntry and the unload routine.
 */

#ifndef CM_NVLINK_H
#define CM_NVLINK_H

#include "../xhci_compat.h"
#include "nvme98cm.h"

/* ---- constants -------------------------------------------------------- */

#define CM_NVLINK_NONE          0xFFFFFFFFUL

/* The acknowledgement wait: nvme98's gone deadline (5 s in its test build,
 * G3), measured here from the DOWN_QUIESCED write. */
#define CM_NVLINK_ACK_MS        5000UL
/* A statement whose delivery would start with less than this left of the
 * USB4 profile's 3 s is withheld (record 22 section 10.3). */
#define CM_NVLINK_DELIVERY_MS   100UL
/* The 9x orderly removal's wait for nvme98's answer (MD-5). */
#define CM_NVLINK_REMOVE_MS     5000UL
/* The idle poll's ceiling while an entry is UP (record 21 section 8). */
#define CM_NVLINK_IDLE_MS       250UL
/* Between reads of an acknowledgement. */
#define CM_NVLINK_POLL_MS       20UL

/* PCI class of an NVMe function: base 01h, subclass 08h (any interface). */
#define CM_NVLINK_CLASS_NVM     0x0108UL

/* CmNvlinkPublishUp's outcome. */
#define CM_NVLINK_UP_NEW        1UL /* a fresh entry, generation 1 or the
                                       entry's next                       */
#define CM_NVLINK_UP_AGAIN      2UL /* its entry, not UP: generation + 1   */
#define CM_NVLINK_UP_ALREADY    3UL /* its entry is UP: unchanged          */
#define CM_NVLINK_UP_FULL       4UL /* no entry free                       */
#define CM_NVLINK_UP_BAD        5UL /* no table, or a bad argument         */

/* Why a statement was or was not made (CmNvlinkIcmVerdict,
 * CmNvlinkU4Verdict, the counters and the log). */
#define CM_NVLINK_SEND              0UL
#define CM_NVLINK_NO_SET            1UL /* the set is empty               */
#define CM_NVLINK_SEVERAL           2UL /* the set has more than one member
                                           and the port's is not known     */
#define CM_NVLINK_HARDWARE          3UL /* not the emulator's SHPC bridge:
                                           the reset is not observable     */
#define CM_NVLINK_NOT_DOWN          4UL /* the reading did not reach DOWN  */
#define CM_NVLINK_PATH_OPEN         5UL /* USB4: PathClosed not proven     */
#define CM_NVLINK_LATE              6UL /* USB4: too little of the 3 s left */
#define CM_NVLINK_NO_ENTRY          7UL /* nothing UP on that bus          */
#define CM_NVLINK_NO_CHANNEL        8UL /* no table, or no rendezvous      */

/* The 9x REMOVE_PENDING request: a PROPOSED extension of nvme98cm.h's
 * entry, not in that header. Flags bit 3 is the request; Reserved[0] the
 * connection manager's request sequence (never 0); Reserved[1] nvme98's
 * answer, the sequence it answers in bits 31:8 and the outcome in 7:0. */
#define CM_NVLINK_REMOVE_PENDING    0x00000008UL
#define CM_NVLINK_RM_NONE           0UL /* not answered (yet)             */
#define CM_NVLINK_RM_CLEAN          1UL /* flushed, CC.SHN, SHST, RDY 0   */
#define CM_NVLINK_RM_ISOLATED       2UL /* isolated, not clean            */
#define CM_NVLINK_RM_FAILED         3UL

/* CmNvlinkOrderlyRemove's result. */
#define CM_NVLINK_OR_REMOVED        1UL /* clean answer, Remove_SubTree ok */
#define CM_NVLINK_OR_VETOED         2UL /* Query_Remove_SubTree refused    */
#define CM_NVLINK_OR_NOT_CLEAN      3UL /* answered, not clean: cancelled  */
#define CM_NVLINK_OR_TIMEOUT        4UL /* no answer: cancelled            */
#define CM_NVLINK_OR_NO_CHANNEL     5UL /* no record or no entry: nothing
                                           asked of nvme98, nothing removed */
#define CM_NVLINK_OR_REMOVE_FAILED  6UL /* Remove_SubTree failed            */

/* The 9x record (proposal; see the file header). */
#define CM_NVLINK_DYN_KEY       "Nvme98LinkState"
#define CM_NVLINK_DYN_VALUE     "Table"
#define CM_NVLINK_RECORD_BYTES  20UL
#define CM_NVLINK_RECORD_VER    1UL

/* ---- pure ------------------------------------------------------------- */

/* Signature, version, count; every entry zero. */
VOID CmNvlinkTableInit(PNVME98LS_TABLE t);

/* The entry of (bus, slot), or CM_NVLINK_NONE. */
ULONG CmNvlinkFind(const NVME98LS_TABLE *t, ULONG bus, ULONG slot);

/*
 * Obligation 1 on (bus, slot): its entry (or a free one: never IN_USE, else
 * the least recent that is neither UP nor awaiting an acknowledgement)
 * gets Flags = IN_USE | UP, with LinkGeneration + 1 unless it was UP
 * already. Writes `*outcome` (CM_NVLINK_UP_*); returns the entry or
 * CM_NVLINK_NONE. A reassigned entry keeps counting its generation.
 */
ULONG CmNvlinkPublishUp(PNVME98LS_TABLE t, ULONG bus, ULONG slot,
                        PULONG outcome);

/* An UP entry leaves UP: Flags = IN_USE | DOWN_QUIESCED when `quiesced`,
 * IN_USE alone otherwise. FALSE (nothing written) for an entry not UP. */
BOOLEAN CmNvlinkSetDown(PNVME98LS_TABLE t, ULONG index, BOOLEAN quiesced);

/* The next UP entry on `bus` at or after `from`, or CM_NVLINK_NONE. */
ULONG CmNvlinkNextUp(const NVME98LS_TABLE *t, ULONG bus, ULONG from);

/* AckGeneration == LinkGeneration on a DOWN_QUIESCED entry. */
BOOLEAN CmNvlinkAcked(const NVME98LS_TABLE *t, ULONG index);

/* The ICM profile's condition (record 21 section 7.1): the set's member
 * count, the member's source (XHCI_TBT_SRC_*), the tunnel-down poll's end
 * (XHCI_TBT_POLL_*) and its last verdict (XHCI_TBT_PORT_*). */
ULONG CmNvlinkIcmVerdict(ULONG members, ULONG source, ULONG pollState,
                         ULONG lastVerdict);

/* The USB4 profile's condition (record 22 section 10.2): PathClosed, the
 * port's member known by correspondence, its source, its reading
 * (XHCI_TBT_PORT_*) and the time left of the 3 s when it was read. */
ULONG CmNvlinkU4Verdict(BOOLEAN pathClosed, BOOLEAN memberKnown,
                        ULONG source, ULONG reading, ULONG remainingMs);

/* Milliseconds left of `budgetMs` begun at `startMs` (a free-running
 * millisecond clock, modulo 2^32), 0 when spent. */
ULONG CmNvlinkLeft(ULONG startMs, ULONG nowMs, ULONG budgetMs);

/* The 9x record, little-endian ULONGs: signature NVME98LS_SIGNATURE,
 * CM_NVLINK_RECORD_VER, the table's linear address, its size in bytes, and
 * `cookie` (also kept in the table's Reserved field, so the reader can
 * match the two). */
VOID CmNvlinkRecordEncode(PUCHAR out, ULONG address, ULONG bytes,
                          ULONG cookie);

/* The 9x REMOVE_PENDING request on an UP entry (FALSE otherwise), its
 * answer for `sequence` (CM_NVLINK_RM_*), and its withdrawal. */
BOOLEAN CmNvlinkRemoveRequest(PNVME98LS_TABLE t, ULONG index,
                              ULONG sequence);
ULONG CmNvlinkRemoveAnswer(const NVME98LS_TABLE *t, ULONG index,
                           ULONG sequence);
VOID CmNvlinkRemoveCancel(PNVME98LS_TABLE t, ULONG index);

/* ---- the driver (cm_nvlinkos.c) --------------------------------------- */

#ifndef XHCI_HOST_TEST

struct _CM_NHI;

/* DriverEntry: the table (CmAlloc, never freed) and, on Windows 2000 and
 * later, the callback object and its callback. Never fails the load: a
 * channel that cannot be set up is logged at the first start and left
 * off (nvme98's gone deadline applies). PASSIVE_LEVEL. */
VOID CmNvlinkInit(VOID);

/* The unload routine: the callback unregistered and the object
 * dereferenced; the table stays. PASSIVE_LEVEL. */
VOID CmNvlinkUnload(VOID);

/* Each NHI start, on its thread, before the first tunnel action: Windows 98
 * SE and ME write the record once per boot (the VMM registry services, a
 * passive thread of the system VM); every target logs the channel's
 * state. */
VOID CmNvlinkStart(struct _CM_NHI *nhi);

/* Obligation 1 for tunnel port `bridge` (XHCI_TBT_BDF): before the
 * re-enumeration call that exposes its secondary bus after a tunnel up or
 * a RESYNC. Returns the number of entries UP for that bus. */
ULONG CmNvlinkExpose(struct _CM_NHI *nhi, ULONG bridge);

/* Obligation 2, ICM profile: a TBT_ICM_ACT_TUNNEL_DOWN after the seam's
 * tunnel-down poll (`pollState`, the poll's last verdict in
 * nhi->Reenum.Poll.Last), before CmNvlinkAwait and the removal. Returns
 * CM_NVLINK_SEND when DOWN_QUIESCED was set on at least one entry, else
 * why not (every UP entry of the set's buses then leaves UP). */
ULONG CmNvlinkIcmDown(struct _CM_NHI *nhi, ULONG pollState);

/* Obligation 2, USB4 profile, from the CM_U4_DOWN_HOOK: `member` the
 * port's index in nhi->Reenum.Topo.Set (XhciTbtPortMember) or
 * XHCI_TBT_NO_MEMBER, `pathClosed` TBT_U4_LINKDOWN.PathClosed,
 * `remainingMs` what is left of its 3 s (0 when no anchor). Reads the
 * member within that time. Returns as CmNvlinkIcmDown; may run nested at
 * a safe point, and never waits for the acknowledgement (CmNvlinkAwait
 * does, before the removal). */
ULONG CmNvlinkPortDown(struct _CM_NHI *nhi, ULONG member,
                       BOOLEAN pathClosed, ULONG remainingMs);

/* Obligation 3: before the removal call of a tunnel down or a RESYNC. */
VOID CmNvlinkAwait(struct _CM_NHI *nhi);

/* Windows 98 SE and ME: the CM-controlled orderly removal of the present
 * NVMe function `bdf` whose devnode is `devnode` (MD-5). Returns
 * CM_NVLINK_OR_*. */
ULONG CmNvlinkOrderlyRemove(struct _CM_NHI *nhi, ULONG devnode, ULONG bdf);

/* The idle poll period: XhciCmPollIdleMs as read, held to at most
 * CM_NVLINK_IDLE_MS while any entry reads UP (a tunnelled NVMe function is
 * there to be noticed leaving: record 21 section 7.2's budget). Any IRQL. */
ULONG CmNvlinkIdleMs(ULONG configured);

/* At the NHI's stop (CmReenumStop): this domain's entries are left as
 * they read (a tunnel the firmware keeps up stays UP) and disowned, so a
 * later start finds them by bus and slot. Counters into the log. */
VOID CmNvlinkStop(struct _CM_NHI *nhi);

#endif /* !XHCI_HOST_TEST */

#endif /* CM_NVLINK_H */
