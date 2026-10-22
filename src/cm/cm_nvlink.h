/*
 * cm_nvlink.h - the stor98 link-state channel (roadmap-usb4.md tasks 39-A.7
 * for the ICM profile and 40-A.7 for the USB4 profile; design record 21
 * section 7, design record 22 section 10). The interface is stor98's,
 * adopted by its maintainer decision MD-6 and defined in stor98cm.h, which
 * sits beside this file byte for byte as stor98 publishes it (its own
 * BSD-3-Clause notice; legal-provenance.md section 2a). This file is the
 * connection manager's side of it and adds nothing to that header.
 *
 * THE TABLE. One STOR98LS_TABLE in non-paged pool, allocated once per
 * driver load (CmNvlinkInit) and NEVER freed, not even at unload: a pointer
 * stor98 took stays valid for the life of the system. One entry per
 * tunnelled NVMe function, keyed by its bus (the tunnel bridge's secondary
 * bus) and PCI_SLOT_NUMBER. The connection manager owns every field but
 * AckGeneration, and writes an entry only inside the odd/even Sequence
 * bracket (stor98cm.h, "Entry protocol").
 *
 * RENDEZVOUS. Windows 2000 and later: the callback object
 * \Callback\Stor98LinkState (ExCreateCallback, OBJ_PERMANENT) with one
 * callback that answers STOR98LS_QUERY with the table's address; stor98
 * opens it once in HwFindAdapter (stor98cm.h's interface 2). Windows 98 SE
 * and ME: stor98cm.h's interface 3 (its rules R1 to R12), written once per
 * load at the first tunnel action, before the first UP, through the VMM
 * registry services. First the current-boot proof (R3a): the dynamic key
 * HKEY_DYN_DATA\STOR98LS9X_PROOF_KEY, created by _RegCreateDynKey with this
 * image as its provider, answering STOR98LS9X_PROOF_VALUE (REG_DWORD, the
 * table's Cookie) and STOR98LS9X_PROOF_TABLE_VALUE (REG_BINARY, a
 * STOR98LS9X_PROOF_TABLE) from this driver's own memory; an existing key
 * is another publisher's, and then nothing is published. Then the
 * locator (R3b): REG_BINARY STOR98LS9X_LOCATOR_VALUE of
 * HKEY_LOCAL_MACHINE\STOR98LS9X_LOCATOR_KEY, a STOR98LS9X_LOCATOR laid out
 * as CmNvlinkRecordEncode writes it. The locator persists across boots
 * and is only a locator; the proof exists only while this image is loaded
 * in this boot, so stor98 accepts the table only when the proof's cookie
 * and address equal the locator's and the table's (R5). The image is
 * resident for the boot on 98 SE and ME (no DriverUnload from DriverEntry,
 * R4), so the provider never goes; were it unloaded, the unload would
 * delete the proof first. The
 * order (residency, proof opened first, proof, locator) is
 * CmNvlinkPublish9x's. Every read of the proof is counted, as NT counts
 * its callback's queries: until stor98 has read it, no receiver exists
 * and nothing waits for one. The REMOVE_PENDING request below is
 * interface 3's removal transaction (R9, R11); since task 43.18 an eject
 * the install or tray tool makes is asked of this driver and runs it
 * (cm_eject.h), and a 9x tunnel down enters a statement decision whose
 * slot reading the member's call worker makes (THE OBLIGATIONS, 2).
 *
 * THE OBLIGATIONS, and where each is met:
 *  1. UP with a new link generation BEFORE the operating system can
 *     enumerate the endpoint: CmNvlinkExpose, called by the re-enumeration
 *     seam (cm_reenum.c) for a tunnel port after its tunnel-up poll and
 *     before the IoInvalidateDeviceRelations or Reenumerate_DevNode that
 *     exposes what is behind it. It reads the port's secondary bus and
 *     publishes UP for each NVMe function (class 01h, subclass 08h) that
 *     answers there, with a new LinkGeneration for a new tunnel only: an
 *     entry not UP, or one UP for another tunnel than the one the exposure
 *     names (CmNvlinkExpose's `tunnel`, task 42-A.6); an entry UP for a
 *     tunnel that stayed up keeps its generation.
 *  2. DOWN_QUIESCED only after the tunnel's PCIe path is closed and the
 *     endpoint's link-down reset has completed, read on the conditions the
 *     record qualifies (record 21 section 7.1, record 22 section 10.2):
 *     ICM profile, CmNvlinkIcmDown - the domain's tunnel-port set has one
 *     member, it is the emulator's bridge (it or the bus-0 bridge above it
 *     carries QEMU's vendor id 1B36h, and it has an SHPC capability and no
 *     PCI Express capability; its presence bit clearing after a disconnect
 *     is the documented later-than-stage-2 signal), the operating system
 *     and architecture are a pair the ordering is qualified on (Windows
 *     2000 on x86, XP x64 on amd64; not XP x86 until gate G3 runs there,
 *     not Vista or 7, whose native hot plug races the connection manager;
 *     98 SE and ME each admitted on its own reading, cm_eject.c
 *     CmEjQualified9x), and the channel's own reading of the
 *     slot came DOWN within 2 s - on 98 SE and ME asynchronously: the
 *     decision entered, the reading made through PCI.VXD by the member's
 *     call worker, the verdict at CmNvlinkDecided (CM_NVLINK_PENDING
 *     meanwhile, the member's removal held). USB4
 *     profile, CmNvlinkPortDown from the CM_U4_DOWN_HOOK - the same, plus
 *     TBT_U4_LINKDOWN.PathClosed (record 22 section 7.3 step 1 read back),
 *     the port's member known by correspondence, the reading DOWN within
 *     the time left of the 3 s, and at least CM_NVLINK_DELIVERY_MS left.
 *     On a hardware tunnel port (Link Active, Presence Detect) the
 *     endpoint's completed reset cannot be observed through a down link,
 *     so no hardware statement is made (record 21 section 7.1); the entry
 *     then goes to IN_USE alone (UP cleared, no DOWN_QUIESCED), which
 *     stor98 ignores, and its gone deadline (MD-1) applies.
 *  3. The acknowledgement before the removal, per member and never by
 *     holding the thread (task 42-A.12, record 21 section 7.2 part 3,
 *     interface 3's R6e): each entry marked DOWN_QUIESCED carries a
 *     pending wait with its mark's time (CM_NV_SHADOW.Waiting, MarkedAt),
 *     stepped by CmNvlinkStep from every pass of the thread and from the
 *     servicing point of every bounded wait inside one (CmNhiWaitService),
 *     ended by AckGeneration == LinkGeneration read twice, or
 *     CmNvlinkAckBoundMs after the mark (5 s on NT, 3 s on 9x: interface
 *     3's R12 S4), the deadline read first (CmNvlinkWaitStep). While one
 *     is pending behind a member, that member's gate is up
 *     (CmNvlinkHeld): the seam makes no removal call for it (the
 *     invalidation that reports the endpoint missing on NT,
 *     Query_Remove_SubTree and Remove_SubTree on 9x), no exposure, and on
 *     NT no release of its held D0 requests at a resume; it holds the
 *     member's work and runs it from a later pass once the gate falls,
 *     while every other member's statement, exposure and removal go on.
 *     On NT nothing waits while no receiver has ever asked for the table
 *     (no stor98 is loaded, so none can acknowledge); on 9x nothing waits
 *     where the proof and locator were never published, nor while no
 *     stor98 has read the proof. On 98 SE and ME it is reached only by a
 *     statement, which obligation 2 makes there never; the bound is
 *     interface 3's all the same.
 *  4. A tunnel re-established only to an endpoint reset since the last one
 *     (MD-2 obligation 1): by construction under the ICM (record 21 section
 *     7.1) and by the Detect gate under the USB4 profile (record 22 section
 *     10.1); nothing here.
 *
 * 9x ORDERLY REMOVAL (MD-5). A tool's eject of a unit is the transaction of
 * cm_eject.h (task 43.18): R11 O5's branches and O6 apart, on the thread,
 * its calls on the member's call worker. What follows is the domain's
 * CONFIGMG worker (cm_cfgwk.c), whose removal step (cmWkRemoveChild) wraps the
 * handshake around a removal of a function still present: Query_Remove_
 * SubTree first (the file system's quiescence and the configuration
 * manager's veto: a refusal is final, nothing is forced); then, accepted,
 * CmNvlinkWkRequest sets REMOVE_PENDING with a fresh request sequence (R9a)
 * and the worker waits, without servicing any ring, up to
 * CM_NVLINK_REMOVE_MS (R11 O4's T_ack) for stor98's answer
 * (CmNvlinkWkAnswer), ending early when its request goes stale. The
 * deadline is checked before each read, so an answer first seen after it
 * is late and changes nothing (R11), and an answer is taken only when a
 * second read finds it unchanged (R6d). Then Remove_SubTree ALWAYS follows,
 * clean answer or not, because Windows 98 cannot cancel an accepted query
 * (a device left query-removed stays quiesced), retried every
 * CM_NVLINK_RETRY_MS until CM_NVLINK_RETRY_FOR_MS after the first attempt
 * while it fails (R11 O6's bound) - the outcome is logged, and only a clean
 * answer is a clean removal; CmNvlinkWkWithdraw then clears the request.
 * MD-5's "a timeout aborts the orderly eject" therefore cannot hold after
 * an accepted query on 9x; the handshake comes as early as that rule
 * allows. Interface 3 asks more after an answer that is not CLEAN (R11 O5:
 * isolate the endpoint before Remove_SubTree) and after O6's 30 s (keep
 * retrying, no UP on that port until the removal succeeds); neither is
 * done here (roadmap-usb4.md NV9X.2, design record 21 section 7.2). The
 * children the worker removes for a tunnel down are functions that
 * vanished, which get no request (nothing to flush); a present one does.
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
#include "stor98cm.h"

/* ---- constants -------------------------------------------------------- */

#define CM_NVLINK_NONE          0xFFFFFFFFUL

/* The acknowledgement wait, measured from the DOWN_QUIESCED write and
 * ended at once by the acknowledgement. Windows 2000 and later: stor98's
 * gone deadline (5 s in its test build, G3). Windows 98 SE and ME
 * (interface 3, R12 S4): at most stor98's gone deadline
 * (STOR98LS9X_GONE_MS, 5 s) and not below STOR98LS9X_CM_LINK_WAIT_MS
 * (2 s: two of its polls and its timer lateness). */
#define CM_NVLINK_ACK_MS        5000UL
#define CM_NVLINK_ACK_9X_MS     3000UL
/* A statement whose delivery would start with less than this left of the
 * USB4 profile's 3 s is withheld (record 22 section 10.3). */
#define CM_NVLINK_DELIVERY_MS   100UL
/* The 9x orderly removal's wait for stor98's answer (interface 3, R11 O4:
 * its 12 s acknowledgement bound, two of its polls and its lateness). */
#define CM_NVLINK_REMOVE_MS     ((ULONG)STOR98LS9X_CM_ACK_WAIT_MS)
/* Remove_SubTree after an accepted query, while it fails (R11 O6). */
#define CM_NVLINK_RETRY_MS      2000UL
#define CM_NVLINK_RETRY_FOR_MS  30000UL
/* The idle poll's ceiling while an entry is UP (record 21 section 8). */
#define CM_NVLINK_IDLE_MS       250UL
/* ... and while any acknowledgement wait is pending, of any domain,
 * whether or not an entry reads UP (interface 3's R10: acknowledgements
 * polled at least every 50 ms; record 21 section 7.2 part 0). */
#define CM_NVLINK_PEND_MS       50UL
/* Between reads of an acknowledgement. */
#define CM_NVLINK_POLL_MS       20UL

/* PCI class of an NVMe function: base 01h, subclass 08h (any interface). */
#define CM_NVLINK_CLASS_NVM     0x0108UL

/* A tunnel's identity for CmNvlinkPublishUp: the USB4 port (1 to 127) and
 * its connection generation (TBT_U4_PORT.Generation, the low 24 bits),
 * never 0; CM_NVLINK_TUNNEL_NONE for an exposure that names no new
 * tunnel. */
#define CM_NVLINK_TUNNEL_NONE   0UL
#define CM_NVLINK_TUNNEL(port, gen) \
    (0x80000000UL | (((ULONG)(port) & 0x7FUL) << 24) | \
     ((ULONG)(gen) & 0xFFFFFFUL))

/* CmNvlinkPublishUp's outcome. */
#define CM_NVLINK_UP_NEW        1UL /* a fresh entry, generation 1 or the
                                       entry's next                       */
#define CM_NVLINK_UP_AGAIN      2UL /* its entry, not UP, or UP for another
                                       tunnel: generation + 1             */
#define CM_NVLINK_UP_ALREADY    3UL /* its entry is UP for this tunnel, or
                                       the exposure names none: unchanged */
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
#define CM_NVLINK_UNQUALIFIED       9UL /* the ordering is not qualified on
                                           this operating system           */
#define CM_NVLINK_LOST              10UL /* the host interface lost (record
                                           21 section 5.5): presence lost,
                                           nothing proven of the endpoint  */
#define CM_NVLINK_PENDING           11UL /* 98 SE and ME: the decision is
                                           entered, the slot reading posted
                                           (task 43.18; CmNvlinkDecided)   */

/* The 9x REMOVE_PENDING request (interface 3, R9): Flags bit 3 is the
 * request, RemoveSequence the connection manager's request sequence
 * (1..STOR98LS9X_SEQ_MASK), RemoveAck stor98's answer, the sequence it
 * answers in bits 31:8 and the outcome in 7:0. */
#define CM_NVLINK_REMOVE_PENDING    ((ULONG)STOR98LS_REMOVE_PENDING)
#define CM_NVLINK_RM_NONE           0UL /* not answered (yet)             */
#define CM_NVLINK_RM_CLEAN          ((ULONG)STOR98RM_CLEAN)
#define CM_NVLINK_RM_ISOLATED       ((ULONG)STOR98RM_ISOLATED_NOT_CLEAN)
#define CM_NVLINK_RM_FAILED         ((ULONG)STOR98RM_FAILED)
/* The removal wait's own ends (CmNvlinkRemoveStep), not stor98's: T_ack
 * spent with no answer, or an answer first seen after it (R11: logged,
 * never taken). Neither is clean. */
#define CM_NVLINK_RM_TIMEOUT        0xFEUL
#define CM_NVLINK_RM_LATE           0xFFUL

/* The link-down acknowledgement wait's step (CmNvlinkAckStep). */
#define CM_NVLINK_AW_PENDING        0UL /* still waiting                  */
#define CM_NVLINK_AW_ACKED          1UL /* acknowledged within the bound  */
#define CM_NVLINK_AW_SUPERSEDED     2UL /* no longer DOWN_QUIESCED        */
#define CM_NVLINK_AW_NO_RECEIVER    3UL /* no stor98 can answer           */
#define CM_NVLINK_AW_TIMEOUT        4UL /* the bound spent, no answer     */
#define CM_NVLINK_AW_LATE           5UL /* the bound spent, then an answer
                                           first seen (logged, not taken) */

/* CmNvlinkPublish9x's outcome: the step that stopped it, or OK. */
#define CM_NVLINK_PUB_OK            0UL
#define CM_NVLINK_PUB_RESIDENCY     1UL /* the image could not be kept    */
#define CM_NVLINK_PUB_PUBLISHER     2UL /* the proof key exists: another
                                           publisher's (R3a)              */
#define CM_NVLINK_PUB_PROOF         3UL /* _RegCreateDynKey failed        */
#define CM_NVLINK_PUB_LOCATOR       4UL /* the locator was not written    */
#define CM_NVLINK_PUB_OPEN          5UL /* the open failed otherwise than
                                           "not found": absence unproven  */

/* VMMREG.H's ERROR_FILE_NOT_FOUND: the one open result that shows the
 * proof key absent. */
#define CM_NV_ERROR_FILE_NOT_FOUND  2UL

/* The 9x locator (R3b) and current-boot proof (R3a). */
#define CM_NVLINK_REC_KEY       STOR98LS9X_LOCATOR_KEY
#define CM_NVLINK_REC_VALUE     STOR98LS9X_LOCATOR_VALUE
#define CM_NVLINK_RECORD_BYTES  ((ULONG)sizeof(STOR98LS9X_LOCATOR))
#define CM_NVLINK_RECORD_VER    ((ULONG)STOR98LS9X_LOCATOR_VERSION)
#define CM_NVLINK_PROOF_BYTES   ((ULONG)sizeof(STOR98LS9X_PROOF_TABLE))

/* VMMREG.H's struct val_context (the Windows 98 DDK): what the VMM hands a
 * dynamic key's query handler per value asked for. */
typedef struct _CM_NV_VALCTX {
    int valuelen;
    PVOID value_context;
    PVOID val_buff_ptr;
} CM_NV_VALCTX;

#define CM_NV_ERROR_MORE_DATA   234UL

/*
 * Interface 3's publication steps (R3, R4), the driver's (cm_nvlinkos.c),
 * each 0 on success but Resident: Resident answers whether the image
 * stays loaded for the boot whatever its PnP devices do; OpenProof opens
 * HKEY_DYN_DATA\STOR98LS9X_PROOF_KEY and closes it again (0: it exists,
 * CM_NV_ERROR_FILE_NOT_FOUND: absent, anything else: unknown);
 * CreateProof creates it with this image as its provider; WriteLocator
 * writes the locator.
 */
typedef struct _CM_NV_PUB9X {
    BOOLEAN (*Resident)(PVOID context);
    ULONG (*OpenProof)(PVOID context);
    ULONG (*CreateProof)(PVOID context);
    ULONG (*WriteLocator)(PVOID context);
    PVOID Context;
} CM_NV_PUB9X;

/* ---- pure ------------------------------------------------------------- */

/* Signature, version, count; every entry zero. */
VOID CmNvlinkTableInit(PSTOR98LS_TABLE t);

/* The entry of (bus, slot), or CM_NVLINK_NONE. */
ULONG CmNvlinkFind(const STOR98LS_TABLE *t, ULONG bus, ULONG slot);

/*
 * Obligation 1 on (bus, slot): its entry, or a never-used one (an entry is
 * never given to another function: a stor98 instance may hold it), gets
 * Flags = IN_USE | UP with LinkGeneration + 1 - unless it is UP already
 * for the same tunnel (stor98cm.h as Stor98 revised it on 2026-10-18,
 * entry protocol step 1 and R8b: only a new tunnel increments
 * LinkGeneration, and UP published again for a tunnel that stayed up
 * keeps it; task 42-A.6). `tunnel` is the exposure's (CM_NVLINK_TUNNEL):
 * NONE for one that follows no new tunnel on the entry's member, and then
 * an entry already UP is left as it is; otherwise an entry UP is left as
 * it is when `tunnels[index]` already names that tunnel, and gets the new
 * generation when it names another (its UP outlived a tunnel nobody saw
 * go down). `tunnels` is the caller's record, one per entry, of the
 * tunnel each entry's UP was last published for, written here with every
 * new generation. An entry with a removal request outstanding is left as
 * it is whatever the tunnel (R9a). Writes `*outcome` (CM_NVLINK_UP_*);
 * returns the entry or CM_NVLINK_NONE.
 */
ULONG CmNvlinkPublishUp(PSTOR98LS_TABLE t, PULONG tunnels, ULONG bus,
                        ULONG slot, ULONG tunnel, PULONG outcome);

/*
 * CmNvlinkPublishUp with the advance said apart from the label: an entry
 * already UP is advanced only when `advance` is TRUE (and `tunnel` names a
 * tunnel other than the one recorded for it); with `advance` FALSE no
 * entry UP is advanced whatever `tunnel` names, and `tunnel` only labels
 * an entry this call publishes (a new one, or one not UP). The driver
 * passes `advance` TRUE only for an exposure that itself names the new
 * tunnel (the UP hook's, the seam's for the action's member): an exposure
 * that names none and publishes for the bridge's recorded tunnel
 * (CmNvlinkBridgeTunnel) may come after the operating system has
 * enumerated the endpoint, and across a 9x resume a bound Stor98 instance
 * may then read the new generation as its quiescence (task 42-A.6, the
 * post-resume case). CmNvlinkPublishUp is this with `advance` TRUE for
 * any tunnel named.
 */
ULONG CmNvlinkPublishUpEx(PSTOR98LS_TABLE t, PULONG tunnels, ULONG bus,
                          ULONG slot, ULONG tunnel, BOOLEAN advance,
                          PULONG outcome);

/*
 * The tunnel the exposure of member `i` carries for an action (task
 * 42-A.6): CM_NVLINK_TUNNEL(port, generation) for the member a tunnel up
 * names (`up` TRUE, `member` its index in the set, known by
 * correspondence, record 22 section 7.6); CM_NVLINK_TUNNEL_NONE for every
 * other member, for any other action (a tunnel down's, a RESYNC's) and
 * for a tunnel up whose member is not known (`member` XHCI_TBT_NO_MEMBER,
 * always so under the firmware connection manager). A function not UP
 * still gets its new generation then (CmNvlinkPublishUp); no entry
 * already UP is advanced on a guess.
 */
ULONG CmNvlinkActionTunnel(BOOLEAN up, ULONG port, ULONG generation,
                           ULONG member, ULONG i);

/*
 * The tunnel an exposure of `bridge` publishes for (task 42-A.6), from a
 * domain's record of the latest tunnel named per bridge (`count` slots of
 * `bridges` and `tunnels`, CM_NVLINK_TUNNEL_NONE in `tunnels` for a free
 * one): `named` when the exposure names one, recorded as the bridge's
 * latest; otherwise the latest named for the bridge before, or
 * CM_NVLINK_TUNNEL_NONE when none was (or no slot was free for it). Every
 * exposure of a bridge so labels what it publishes with its latest tunnel:
 * applied again it changes nothing (CmNvlinkPublishUp), and an entry an
 * exposure naming none publishes is not advanced again by the named
 * exposure of the same tunnel. An exposure that names none never advances
 * an entry already UP for it (CmNvlinkPublishUpEx's `advance` FALSE).
 */
ULONG CmNvlinkBridgeTunnel(PULONG bridges, PULONG tunnels, ULONG count,
                           ULONG bridge, ULONG named);

/*
 * Before every exposure of `bus` (after the acknowledgement wait): every
 * entry there that carries a statement (DOWN_QUIESCED, not UP) becomes
 * IN_USE alone at the same LinkGeneration, so a stor98 instance that binds
 * after the exposure - to a function the scan could not publish, or one
 * that came back between the scan and the operating system's enumeration
 * - finds no DOWN_QUIESCED, never an old statement. The generation is not
 * advanced (task 42-A.12): stor98cm.h as Stor98 revised it on 2026-10-18
 * increments LinkGeneration "by exactly one per tunnel establishment and
 * for nothing else" (R8b), so a re-plug after a statement costs the one
 * increment of its new UP; and Stor98's 9x late lookup takes a generation
 * read not UP as its floor and binds only an UP above it, which that UP
 * is. An entry already IN_USE alone carries nothing and is left. Returns
 * how many; CmNvlinkRetireEntry does one entry, TRUE when it did.
 */
ULONG CmNvlinkRetire(PSTOR98LS_TABLE t, ULONG bus);
BOOLEAN CmNvlinkRetireEntry(PSTOR98LS_TABLE t, ULONG index);
/* R8a's release of an entry whose port's bus number changed, after its
 * devnode was retired (record 22 section 7.7): IN_USE cleared (Flags 0)
 * in one R6b Sequence update, UP with it, nothing else changed; FALSE for
 * an entry not IN_USE or with a removal request outstanding (R9a). */
BOOLEAN CmNvlinkReleaseEntry(PSTOR98LS_TABLE t, ULONG index);

/* An UP entry leaves UP: Flags = IN_USE | DOWN_QUIESCED when `quiesced`,
 * IN_USE alone otherwise. FALSE (nothing written) for an entry not UP. */
BOOLEAN CmNvlinkSetDown(PSTOR98LS_TABLE t, ULONG index, BOOLEAN quiesced);

/* The next UP entry on `bus` at or after `from`, or CM_NVLINK_NONE. */
ULONG CmNvlinkNextUp(const STOR98LS_TABLE *t, ULONG bus, ULONG from);

/* AckGeneration == LinkGeneration on a DOWN_QUIESCED entry. */
BOOLEAN CmNvlinkAcked(const STOR98LS_TABLE *t, ULONG index);

/* The ICM profile's condition (record 21 section 7.1): the set's member
 * count, the member's source (XHCI_TBT_SRC_*), whether the member is the
 * emulator's bridge (QEMU's 1B36h on it or the bus-0 bridge above it, an
 * SHPC capability and no PCI Express capability; the only bridge whose
 * slot-empty reading is the documented later-than-stage-2 signal),
 * whether the operating system is one the
 * ordering is qualified on, the channel's reading's end (XHCI_TBT_POLL_*)
 * and its last verdict (XHCI_TBT_PORT_*). */
ULONG CmNvlinkIcmVerdict(ULONG members, ULONG source, BOOLEAN emulator,
                         BOOLEAN osQualified, ULONG pollState,
                         ULONG lastVerdict);

/* The USB4 profile's condition (record 22 section 10.2): PathClosed, the
 * port's member known by correspondence, its source, emulator and
 * qualification as above, its reading (XHCI_TBT_PORT_*) and the time left
 * of the 3 s when it was read. */
ULONG CmNvlinkU4Verdict(BOOLEAN pathClosed, BOOLEAN memberKnown,
                        ULONG source, BOOLEAN emulator, BOOLEAN osQualified,
                        ULONG reading, ULONG remainingMs);

/* The acknowledgement wait's bound: CM_NVLINK_ACK_MS on NT, else
 * CM_NVLINK_ACK_9X_MS. */
ULONG CmNvlinkAckBoundMs(BOOLEAN nt);

/* Milliseconds left of `budgetMs` begun at `startMs` (a free-running
 * millisecond clock, modulo 2^32), 0 when spent. */
ULONG CmNvlinkLeft(ULONG startMs, ULONG nowMs, ULONG budgetMs);

/* The 9x locator (STOR98LS9X_LOCATOR), little-endian ULONGs: signature
 * STOR98LS_SIGNATURE, CM_NVLINK_RECORD_VER, the table's linear address,
 * its size in bytes, and `cookie` (also the table's Cookie and the proof's
 * BootCookie, so the reader can match the three). */
VOID CmNvlinkRecordEncode(PUCHAR out, ULONG address, ULONG bytes,
                          ULONG cookie);

/*
 * The proof key's query handler (R3a), for every one of the VMM's four
 * handler slots: each of the `n` values asked for in `vc` is the one whose
 * value_context is `cookie` (REG_DWORD, 4 bytes) or `proof` (the
 * STOR98LS9X_PROOF_TABLE, 12 bytes), any other 0 bytes; the provider keeps
 * the lengths (PROVIDER_KEEPS_VALUE_LENGTH), so each valuelen is set here.
 * `data` NULL: the total length into *cb, 0 returned. `data` too short:
 * the total into *cb, CM_NV_ERROR_MORE_DATA. Otherwise the values copied
 * one after another, each val_buff_ptr set to its copy, the total into
 * *cb, 0 returned.
 */
ULONG CmNvlinkProofQuery(CM_NV_VALCTX *vc, ULONG n, PUCHAR data, PULONG cb,
                         const ULONG *cookie,
                         const STOR98LS9X_PROOF_TABLE *proof);

/* The 9x REMOVE_PENDING request on an UP entry (FALSE otherwise), its
 * answer for `sequence` on generation `gen` (CM_NVLINK_RM_*; R9c, read
 * twice, R6d), and its withdrawal. */
BOOLEAN CmNvlinkRemoveRequest(PSTOR98LS_TABLE t, ULONG index,
                              ULONG sequence);
ULONG CmNvlinkRemoveAnswer(const STOR98LS_TABLE *t, ULONG index,
                           ULONG sequence, ULONG gen);

/* One read of the removal wait, `elapsedMs` after the request: the
 * deadline first (R11), so CM_NVLINK_RM_TIMEOUT once CM_NVLINK_REMOVE_MS
 * has passed with no answer and CM_NVLINK_RM_LATE for one first seen then;
 * before it, CmNvlinkRemoveAnswer's answer (CM_NVLINK_RM_NONE: wait on). */
ULONG CmNvlinkRemoveStep(const STOR98LS_TABLE *t, ULONG index,
                         ULONG sequence, ULONG gen, ULONG elapsedMs);

/* One read of the link-down acknowledgement wait for an entry marked
 * DOWN_QUIESCED at `markedAt` (ms): CM_NVLINK_AW_*. Superseded (the entry
 * left DOWN_QUIESCED) and no receiver are decided first, then the
 * deadline (`boundMs` from the mark) before the acknowledgement is read,
 * so one first seen after it is LATE and never ACKED (R11, R12 S4); the
 * acknowledgement is read twice, unchanged (R6d). */
ULONG CmNvlinkAckStep(const STOR98LS_TABLE *t, ULONG index,
                      BOOLEAN receiver, ULONG markedAt, ULONG nowMs,
                      ULONG boundMs);

/*
 * THE PENDING WAITS (task 42-A.12; record 21 section 7.2 parts 0 and 3).
 * The connection manager's shadow of each entry (cm_nvlinkos.c keeps one
 * array of STOR98LS_ENTRIES under its table lock): the domain that last
 * published or marked it (Owner, opaque here), its tunnel port, whether a
 * link-down acknowledgement is awaited and since when, and the ids its UP
 * was published for.
 */
typedef struct _CM_NV_SHADOW {
    PVOID Owner;        /* the domain that last published or marked it   */
    ULONG Bridge;       /* its tunnel port, XHCI_TBT_BDF                 */
    ULONG Waiting;      /* DOWN_QUIESCED set, acknowledgement awaited    */
    ULONG MarkedAt;     /* the owner's clock at the mark, ms             */
    ULONG Id;           /* the function's ids when UP was published      */
} CM_NV_SHADOW;

/*
 * One step of every wait `owner` has pending: CmNvlinkAckStep on each
 * entry it marked and awaits, the deadline read before the answer; an
 * entry whose wait ended (acknowledged, superseded, no receiver, timed
 * out, late) is no longer Waiting, and (CM_NVLINK_AW_* << 8) | entry is
 * stored in ends[*count] while *count < STOR98LS_ENTRIES (ends NULL: not
 * recorded). No wait, no call: bounded by the table's size. Returns how
 * many of `owner`'s waits are still pending.
 */
ULONG CmNvlinkWaitStep(const STOR98LS_TABLE *t, CM_NV_SHADOW *shadow,
                       PVOID owner, BOOLEAN receiver, ULONG nowMs,
                       ULONG boundMs, PULONG ends, PULONG count);

/* Member `bridge`'s gate (task 42-A.12): TRUE while an entry `owner`
 * marked behind it awaits its acknowledgement. Another member's waits
 * never raise it. */
BOOLEAN CmNvlinkWaitHeld(const CM_NV_SHADOW *shadow, PVOID owner,
                         ULONG bridge);

/* Any wait pending, of any domain (the 50 ms cadence, R10). */
BOOLEAN CmNvlinkWaitAny(const CM_NV_SHADOW *shadow);

/*
 * THE SEAM'S HOLD (task 42-A.12; record 21 section 7.2 part 3). A member
 * whose gate is up has its work held in CM_REENUM_PORT.Held (cm_reenum.c)
 * and run by the first pass that finds the gate down; another member's
 * gate never holds it. The decisions are pure so that the host vectors
 * drive them across passes.
 */
#define CM_HELD_REENUM      0x01UL  /* its exposure and re-enumeration     */
#define CM_HELD_VANISH      0x02UL  /* ... removing vanished children     */
#define CM_HELD_FRESH       0x04UL  /* ... a fresh exposure               */
#define CM_HELD_RELEASE     0x08UL  /* NT: its held D0 requests' release  */
#define CM_HELD_FAIL        0x10UL  /* ... on the failure path            */
#define CM_HELD_LOSS        0x20UL  /* NT: the loss's own call (removal)  */

/* CmNvlinkHoldRun's answer, bits. */
#define CM_HELD_DO_RELEASE  0x01UL  /* release its held D0 requests       */
#define CM_HELD_DO_FAIL     0x02UL  /* ... on the failure path            */
#define CM_HELD_DO_REENUM   0x04UL  /* expose and call, *vanish, *fresh    */
#define CM_HELD_DO_OWE      0x08UL  /* not active: owed (Missed) instead  */
#define CM_HELD_DO_LOSS     0x10UL  /* the loss's call: removal, no UP    */

/*
 * An action's re-enumeration of a member (cmReenumAll): `gateUp` - held,
 * collapsed into *held with what was held before (a removal of vanished
 * children or a fresh exposure asked by either is kept), TRUE returned;
 * down - any re-enumeration held before folded into *vanish and *fresh and
 * taken out of *held, and an owed skipped change (*missed) paid by the same
 * call (*vanish and *fresh set, *missed cleared), FALSE returned (the
 * caller makes the call). Down with the host interface lost (`live`
 * FALSE): FALSE, the call made alone, *held and *missed kept for the pass
 * that finds the transport back (CmNvlinkHoldRun). Up with it lost: the
 * loss's call alone is held (CM_HELD_LOSS), apart from the ordinary work;
 * any later call made with the gate down pays it (*vanish set).
 */
BOOLEAN CmNvlinkHoldReenum(PULONG held, BOOLEAN gateUp, BOOLEAN live,
                           BOOLEAN *missed, BOOLEAN *vanish, BOOLEAN *fresh);

/*
 * The servicing point of back-to-back busy waits (cm_thr.c cmStall): TRUE,
 * and *lastUs advanced to `nowUs`, once CM_NVLINK_PEND_MS have passed on
 * the microsecond clock (TBT_HOST.ClockUs, unsigned difference) since the
 * last step it records; a sleep's step records its own time there.
 * Any IRQL.
 */
BOOLEAN CmNvlinkServiceDue(PULONG lastUs, ULONG nowUs);

/*
 * A pass's decision for a member's held work: nothing while its gate is
 * up. Down: a held resume release is returned (and taken out of *held)
 * whatever the transport; the re-enumeration is kept while the host
 * interface is lost (`live` FALSE: no bridge is read and nothing posted
 * until the transport is back), else taken out and returned when
 * `reenumOn` (XhciCmReenum not 0) and the member is `active`, owed instead
 * (CM_HELD_DO_OWE) when it is only `exclusive`. A member that also owes a
 * skipped change (*missed) is paid by the same call: *vanish and *fresh
 * set and *missed cleared, so one dispatch pays both. A held loss call
 * (CM_HELD_LOSS) waits for the gate alone: CM_HELD_DO_LOSS for an active
 * member (a removal of vanished children, no exposure), folded instead
 * into a held re-enumeration that runs in the same pass.
 */
ULONG CmNvlinkHoldRun(PULONG held, BOOLEAN gateUp, BOOLEAN live,
                      BOOLEAN reenumOn, BOOLEAN active, BOOLEAN exclusive,
                      BOOLEAN *missed, BOOLEAN *vanish, BOOLEAN *fresh);

/* The idle poll period from XhciCmPollIdleMs as read: held to at most
 * CM_NVLINK_PEND_MS while `pending`, else to CM_NVLINK_IDLE_MS while `up`
 * (an entry reads UP). */
ULONG CmNvlinkIdleFor(ULONG configured, BOOLEAN up, BOOLEAN pending);

/* Interface 3's publication in R3's and R4's order: the image resident,
 * the proof key opened first (an existing one is another publisher's, and
 * one whose absence the open did not show may be: nothing is published
 * either way), the proof created, then the locator. The table and its
 * Cookie are the caller's, filled before. Returns CM_NVLINK_PUB_*, and the
 * failed step's own code in *error (0 for OK, RESIDENCY and PUBLISHER). A
 * proof made with no locator after it leaves no channel (stor98 needs
 * both) and stays until the unload. */
ULONG CmNvlinkPublish9x(const CM_NV_PUB9X *ops, PULONG error);

/* Remove_SubTree failing after an accepted query (R11 O6): TRUE while
 * `elapsedMs` since the first attempt is short of CM_NVLINK_RETRY_FOR_MS,
 * so the attempts run every CM_NVLINK_RETRY_MS from 0 to 30 s. */
BOOLEAN CmNvlinkRetryAgain(ULONG elapsedMs);
VOID CmNvlinkRemoveCancel(PSTOR98LS_TABLE t, ULONG index);

/* ---- the driver (cm_nvlinkos.c) --------------------------------------- */

#ifndef XHCI_HOST_TEST

struct _CM_NHI;
struct _TBT_U4_ACTION;
struct _TBT_U4_LINKDOWN;

/* DriverEntry: the table (CmAlloc, never freed) and, on Windows 2000 and
 * later, the callback object and its callback. Never fails the load: a
 * channel that cannot be set up is logged at the first start and left
 * off (stor98's gone deadline applies). PASSIVE_LEVEL. */
VOID CmNvlinkInit(VOID);

/* The unload routine: the callback unregistered and the object
 * dereferenced; the table stays. PASSIVE_LEVEL. */
VOID CmNvlinkUnload(VOID);

/* Each NHI start, on its thread, before the first tunnel action: Windows 98
 * SE and ME write the record once per boot (the VMM registry services, a
 * passive thread of the system VM); every target logs the channel's
 * state. */
VOID CmNvlinkStart(struct _CM_NHI *nhi);

/* Obligation 1 for tunnel port `bridge` (XHCI_TBT_BDF): before every
 * re-enumeration call that exposes its secondary bus. Every exposure
 * first retires the statements on that bus (and on every entry last
 * published or marked for `bridge`, should the bus not read). `fresh` TRUE
 * after a tunnel up, a RESYNC or an owed re-enumeration: a silent bus is
 * waited for (on NT where the SHPC slot reads a card present) and, on 98
 * SE and ME, scanned again on later passes (CmNvlinkService); FALSE on a
 * tunnel down's. `tunnel` (CM_NVLINK_TUNNEL) names the new tunnel on
 * `bridge` this exposure follows, or is CM_NVLINK_TUNNEL_NONE: a function
 * found not UP gets a new generation either way, one UP only when it was
 * published for another tunnel than `tunnel` names (CmNvlinkPublishUp,
 * task 42-A.6). Windows 98 SE and ME: no UP while the record is not
 * written (the channel is unavailable to that exposure). Only functions on
 * the secondary bus itself are seen: an NVMe function behind an
 * enclosure's own PCIe switch is not, gets no entry and so no statement
 * (stor98's gone deadline stands); nor does one that has not answered by
 * the end of the scan. Returns the number of entries UP for that bus. */
ULONG CmNvlinkExpose(struct _CM_NHI *nhi, ULONG bridge, BOOLEAN fresh,
                     ULONG tunnel);

/* Obligation 2, ICM profile: a TBT_ICM_ACT_TUNNEL_DOWN after the seam's
 * tunnel-down poll (`pollState`, the poll's last verdict in
 * nhi->Reenum.Poll.Last), before the removal, which its pending wait
 * then holds per member (obligation 3). Returns
 * CM_NVLINK_SEND when DOWN_QUIESCED was set on at least one entry, else
 * why not (every UP entry of the set's buses then leaves UP). */
ULONG CmNvlinkIcmDown(struct _CM_NHI *nhi, ULONG pollState);

/* The host interface lost (a TUNNEL_DOWN of why TBT_ICM_WHY_UNAVAILABLE,
 * NT): no statement - each UP entry of the set's buses is treated as for a
 * verdict that withholds it (CM_NVLINK_LOST: UP cleared only where its
 * function reads absent, never DOWN_QUIESCED), so stor98's own gone
 * deadline applies. Not called on 98 SE and ME, whose loss reads no bridge
 * (cm_reenum.c CmReenumAction). */
VOID CmNvlinkLost(struct _CM_NHI *nhi);

/* Record 22 section 7.7's recovery (task 42-A.10). CmNvlinkUpCount: how
 * many entries behind `bridge` read UP (by the shadow, or by its
 * secondary bus read now; NT only, as CmNvlinkLost). CmNvlinkRetireMember:
 * publication mode retire for `bridge`, once its endpoint's devnode was
 * seen retired - each UP entry there whose function reads absent (its
 * identifier read, all ones) leaves UP for IN_USE alone, its generation
 * unchanged: no DOWN_QUIESCED, no increment, nothing published. */
ULONG CmNvlinkUpCount(struct _CM_NHI *nhi, ULONG bridge);
VOID CmNvlinkRetireMember(struct _CM_NHI *nhi, ULONG bridge);
/* R8a, after the retirement of the devnodes of member `bridge`, whose
 * secondary bus changed from `bus`: every entry recorded for that member
 * on the old bus (its shadow names the bridge, or names none while no
 * member of the set has `bus` as its secondary bus now) has IN_USE
 * cleared through CmNvlinkReleaseEntry - UP or not, since a function
 * answering at the old address now is another member's or nobody's - and
 * its shadow forgotten; the rebuild's exposure on the new bus takes a
 * free entry. Returns how many such entries are left (a removal request
 * outstanding, an acknowledgement awaited): called again later. */
ULONG CmNvlinkReleaseBus(struct _CM_NHI *nhi, ULONG bridge, ULONG bus);

/* The host interface lost, every operating system (cm_reenum.c
 * CmReenumLossBegin): the deferred scans of buses an exposure found silent
 * are dropped, with no access; a later exposure arms its own. */
VOID CmNvlinkDropLate(struct _CM_NHI *nhi);

/* Obligation 2, USB4 profile, from the CM_U4_DOWN_HOOK: `member` the
 * port's index in nhi->Reenum.Topo.Set (XhciTbtPortMember) or
 * XHCI_TBT_NO_MEMBER, `pathClosed` TBT_U4_LINKDOWN.PathClosed,
 * `remainingMs` what is left of its 3 s (0 when no anchor). Reads the
 * member within that time. Returns as CmNvlinkIcmDown; may run nested at
 * a safe point, and never waits for the acknowledgement (its pending
 * wait holds the member's removal, obligation 3). */
ULONG CmNvlinkPortDown(struct _CM_NHI *nhi, ULONG member,
                       BOOLEAN pathClosed, ULONG remainingMs);

/*
 * Obligation 3 (task 42-A.12). CmNvlinkStep steps every acknowledgement
 * wait of this domain (CmNvlinkWaitStep on the NHI's clock) and logs each
 * end: from every pass of the thread (CmReenumService) and from the
 * servicing point of every bounded wait inside one (CmNhiWaitService:
 * cmPoll's loop, the channel's reading and scan waits, the ICM client's
 * command wait). Non-reentrant (a step reached from inside a step does
 * nothing), no wait, no call: it only advances states and writes the log.
 * CmNvlinkHeld steps first and answers member `bridge`'s gate: TRUE while
 * a wait of this domain is pending behind it, and then the seam makes no
 * removal call, no exposure (CmNvlinkExpose refuses one too) and no
 * release of held D0 requests for that member, and runs them from a pass
 * after the gate falls; nothing of another member's waits on it.
 * CmNvlinkPending: any wait of any domain pending (the thread's cadence).
 */
VOID CmNvlinkStep(struct _CM_NHI *nhi);
BOOLEAN CmNvlinkHeld(struct _CM_NHI *nhi, ULONG bridge);
BOOLEAN CmNvlinkPending(VOID);
/* Task 43.18: member `bridge`'s acknowledgement waits alone (CmNvlinkHeld
 * without the 9x eject's gate), for the eject's isolation branch.
 * CmNvlinkMarkEntry: DOWN_QUIESCED on entry `index` this domain published
 * behind `bridge`, with its acknowledgement wait when `wait`; TRUE when
 * written. CmNvlinkDecided: a 9x statement decision's end (cm_ejectos.c) -
 * the verdict from its reading, then the statement made or withheld as
 * CmNvlinkIcmDown and CmNvlinkPortDown make it. On the thread. */
BOOLEAN CmNvlinkAckHeld(struct _CM_NHI *nhi, ULONG bridge);
BOOLEAN CmNvlinkMarkEntry(struct _CM_NHI *nhi, ULONG bridge, ULONG index,
                          BOOLEAN wait);
/* After a clean 9x eject with no teardown (the firmware connection
 * manager): entry `index` this domain published behind `bridge` leaves UP
 * for IN_USE alone - no DOWN_QUIESCED, no wait, generation unchanged - as
 * its Stor98 instance was removed with the devnode; TRUE when written. */
BOOLEAN CmNvlinkRetireEjected(struct _CM_NHI *nhi, ULONG bridge,
                              ULONG index);
/* Whether member `bridge` reads as the emulator's bridge, as the
 * statement's condition reads it (an SHPC capability, no PCI Express
 * capability, and QEMU's vendor id on it or the bus-0 bridge above it):
 * one reading, configuration reads alone. */
BOOLEAN CmNvlinkEmulatorMember(struct _CM_NHI *nhi, ULONG bridge);
struct _CM_EJ_DECISION;
VOID CmNvlinkDecided(struct _CM_NHI *nhi,
                     const struct _CM_EJ_DECISION *d);

/* Windows 98 SE and ME, the CONFIGMG worker's removal step (see the file
 * header). CmNvlinkWkCandidate: whether entry `index` is one domain
 * `owner` published UP behind tunnel port `bridge` for ids `id` (vendor
 * 15:0, device 31:16) with no request outstanding, and its bus, slot and
 * generation - the worker matches that function's path against the
 * devnode's instance id BEFORE its query, so the request names the very
 * devnode the query accepted. CmNvlinkWkRequest: REMOVE_PENDING with a
 * fresh sequence (written to `*sequence`) on that entry, only while the
 * function still answers with `id` and owner, UP, generation `gen` and the
 * absence of another request are unchanged; returns the entry, or
 * CM_NVLINK_NONE.
 * CmNvlinkWkAnswer: CmNvlinkRemoveStep for that sequence on generation
 * `gen`, `elapsedMs` after the request (CM_NVLINK_RM_*).
 * CmNvlinkWkWithdraw: that request cleared, and only that one. Any thread
 * at <= DISPATCH_LEVEL but CmNvlinkWkRequest
 * (PASSIVE_LEVEL: a configuration read); no ring is serviced and nothing
 * waits. */
BOOLEAN CmNvlinkWkCandidate(struct _CM_NHI *owner, ULONG bridge, ULONG id,
                            ULONG index, PULONG bus, PULONG slot,
                            PULONG gen);
ULONG CmNvlinkWkRequest(struct _CM_NHI *owner, ULONG index, ULONG gen,
                        ULONG id, PULONG sequence);
ULONG CmNvlinkWkAnswer(ULONG index, ULONG sequence, ULONG gen,
                       ULONG elapsedMs);
VOID CmNvlinkWkWithdraw(ULONG index, ULONG sequence);

/* The idle poll period: XhciCmPollIdleMs as read, held to at most
 * CM_NVLINK_PEND_MS while any acknowledgement wait is pending (R10), else
 * to CM_NVLINK_IDLE_MS while any entry reads UP (a tunnelled NVMe function
 * is there to be noticed leaving: record 21 section 7.2's budget). Any
 * IRQL. */
ULONG CmNvlinkIdleMs(ULONG configured);

/*
 * The USB4 profile (record 22 section 10), registered by the driver half
 * with CmNhiSetU4Hooks where the selection admits that profile, with the
 * NHI as the context. CmNvlinkU4Up (CM_U4_UP_HOOK) records the port's
 * connection generation for the presence sample and publishes the UP
 * itself (CmNvlinkExpose of the port's bridge with the port's tunnel, or
 * of every member with none when the correspondence is not known), before
 * anything - the seam's
 * re-enumeration, or the release of held D0 requests at a resume - can
 * expose the endpoint. CmNvlinkU4Down (CM_U4_DOWN_HOOK) maps the port to
 * its member by correspondence and calls CmNvlinkPortDown; it returns
 * TBT_U4_STMT_SENT when a DOWN_QUIESCED was written, else
 * TBT_U4_STMT_WITHHELD. While the port's member's gate is up (an
 * acknowledgement pending behind it: a re-plug inside the wait), the up
 * hook records the tunnel and publishes nothing: the seam's exposure of
 * the member, held behind the same gate, publishes for it once the wait
 * has ended (R8b's order, DOWN_QUIESCED acknowledged or timed out before
 * the next UP). CmNvlinkService, once per pass of the thread
 * (CmReenumService), offers the core one presence sample per port known
 * by correspondence that has UP entries (TbtU4PresenceSample, the anchor
 * of section 10.3), timestamped at its first read and matched only when
 * every entry answered with its own ids; nothing outside that profile.
 */
VOID CmNvlinkU4Up(PVOID context, const struct _TBT_U4_ACTION *action);
ULONG CmNvlinkU4Down(PVOID context, const struct _TBT_U4_LINKDOWN *info);
VOID CmNvlinkService(struct _CM_NHI *nhi);

/* At the NHI's stop (CmReenumStop): this domain's entries are left as
 * they read (a tunnel the firmware keeps up stays UP) and disowned, so a
 * later start finds them by bus and slot. Counters into the log. */
VOID CmNvlinkStop(struct _CM_NHI *nhi);

#endif /* !XHCI_HOST_TEST */

#endif /* CM_NVLINK_H */
