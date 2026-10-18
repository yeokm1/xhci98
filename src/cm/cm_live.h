/*
 * cm_live.h - the pure half of xhcicm.sys's host-interface liveness
 * monitor (design record 21 sections 5.5 and 5.6, record 22 section 3.5):
 * whether the NHI's registers can still be trusted, after a platform
 * power-off or reset that Windows did not report (BIOS Assist after the
 * last unplug, usb4-emu ICM-PROTOCOL R27/R32), and which accesses the
 * driver may make meanwhile. DDK-free and host-tested (test\test_cm_live.c);
 * cm_thr.c samples and gates, cm_nhi.c runs the actions.
 *
 * The sample is one register this driver programmed and reads back: RX
 * ring 0's size register (TBT_RX_RING(0) + TBT_RING_SIZE_REG). Its value
 * read once after the ring start is the baseline; all ones is "no answer";
 * any other value that differs from a non-zero baseline is "configuration
 * lost". Command silence alone is never absence.
 *
 * IRQL: any. Takes no lock; every function works on the caller's memory.
 * The caller serialises every call but CmLiveRevoke, which may run beside
 * the others (a PnP dispatch thread): it only sets Revoked, which every
 * other function reads first.
 */

#ifndef CM_LIVE_H
#define CM_LIVE_H

#include "../xhci_compat.h"

/* States. OFF: not monitored (before activation, inert, failed, stopped,
 * suspended) and ordinary access admitted as before this monitor existed.
 * ACTIVE: monitored, transport admitted. SUSPECT: one all-ones sample,
 * transport refused. ABSENT: three in a row. CONFIG_LOST: a readable
 * sample that differs from the baseline. RECOVERING: the driver's own
 * recovery steps, the only access then. RECOVERY_FAILED: no access, slow
 * monitoring for a new episode. CONTAINED: the USB4 profile's loss state
 * (record 22 section 7.7), in one of three substates (Sub): probing once
 * a second for access back, cooldown waiting for a new episode, or final
 * for the boot; its exit is PREPARING (the configuration header proven,
 * the preparation's two writes admitted) and then RECOVERING, which
 * commits to ACTIVE or falls back to CONTAINED. REVOKED:
 * surprise removal, stop or remove - nothing at all, terminal. */
#define CM_LIVE_OFF              0UL
#define CM_LIVE_ACTIVE           1UL
#define CM_LIVE_SUSPECT          2UL
#define CM_LIVE_ABSENT           3UL
#define CM_LIVE_CONFIG_LOST      4UL
#define CM_LIVE_RECOVERING       5UL
#define CM_LIVE_RECOVERY_FAILED  6UL
#define CM_LIVE_CONTAINED        7UL
#define CM_LIVE_REVOKED          8UL
#define CM_LIVE_PREPARING        9UL

/* CONTAINED's substates (record 22 section 7.7 part 5). */
#define CM_LIVE_SUB_PROBING      0UL
#define CM_LIVE_SUB_COOLDOWN     1UL
#define CM_LIVE_SUB_FINAL        2UL

/* What the USB4 profile's probe read (CmLiveProbe): the identity dword
 * all ones (no answer), another identity, BAR0 not as the healthy start
 * read it, or the header proven with BAR0 intact. */
#define CM_LIVE_PROBE_ONES       0UL
#define CM_LIVE_PROBE_OTHER      1UL
#define CM_LIVE_PROBE_BAR        2UL
#define CM_LIVE_PROBE_OK         3UL

/* Access classes the gate (cm_thr.c's BAR accessors) is asked about.
 * ORDINARY: everything the clients do. PROBE: the monitor's and the
 * validation's reads (never a write). RECOVERY: the recovery's own
 * reads and writes. */
#define CM_ACCESS_ORDINARY       0UL
#define CM_ACCESS_PROBE          1UL
#define CM_ACCESS_RECOVERY       2UL
/* The USB4 recovery's preparation (record 22 section 7.7 part 2): reads,
 * and the configuration writes of the Command register and the platform
 * registers alone (cm_thr.c's cmConfig enforces the offsets); no BAR
 * write. Admitted in PREPARING only. */
#define CM_ACCESS_PREPARE        3UL

/* Actions a call asks of the driver, as bits; each at most once per
 * episode where it says so. */
#define CM_LIVE_ACT_STOP_TRANSPORT 0x01UL  /* transport refused from now  */
#define CM_LIVE_ACT_RECONCILE      0x02UL  /* every device reported gone,
                                              reason "controller
                                              unavailable": once/episode  */
#define CM_LIVE_ACT_VALIDATE       0x04UL  /* run the recovery precondition
                                              (identity, BAR, revalidation)
                                              and report CmLiveValidated  */
#define CM_LIVE_ACT_RECOVER_LIGHT  0x08UL  /* driver-ready rediscovery on
                                              the retained rings          */
#define CM_LIVE_ACT_RECOVER_FULL   0x10UL  /* rings re-initialised in the
                                              kept buffer, then the bind's
                                              protocol steps              */
#define CM_LIVE_ACT_CONTAIN        0x20UL  /* the USB4 profile: contain   */
#define CM_LIVE_ACT_LOG            0x40UL  /* a transition to log         */
#define CM_LIVE_ACT_PREPARE        0x80UL  /* USB4: run the preparation,
                                              report CmLivePrepared       */
#define CM_LIVE_ACT_SNAPSHOT       0x100UL /* USB4: a new episode - the
                                              recovery snapshot again     */

/* Defaults: the idle cadence (XhciCmPollIdleMs, 200 ms) between ACTIVE
 * samples, three consecutive all-ones samples for ABSENT, one probe a
 * second outside ACTIVE. */
#define CM_LIVE_ONES_FOR_ABSENT   3UL
#define CM_LIVE_SLOW_MS           1000UL

typedef struct _CM_LIVE_COUNTERS {
    ULONG Samples;
    ULONG Suspects;         /* ACTIVE -> SUSPECT                           */
    ULONG Absents;          /* -> ABSENT                                   */
    ULONG ConfigLost;       /* -> CONFIG_LOST                              */
    ULONG Recoveries;       /* recoveries started                          */
    ULONG RecoveredOk;
    ULONG RecoveryFailed;
    ULONG Contained;
    ULONG Revokes;
    ULONG RefusedReads;     /* the gate's, counted by cm_thr.c             */
    ULONG RefusedWrites;
    ULONG LateCompletions;  /* a recovery result for another episode, or
                               after REVOKED: dropped                      */
} CM_LIVE_COUNTERS;

typedef struct _CM_LIVE {
    ULONG State;            /* CM_LIVE_*                                   */
    volatile ULONG Revoked; /* set by CmLiveRevoke; read first everywhere  */
    ULONG Usb4;             /* the USB4 profile: CONTAIN, never recover    */
    ULONG Baseline;         /* the sample's value after the ring start;
                               0 = only all-ones is detected               */
    ULONG Ones;             /* consecutive all-ones samples                */
    ULONG LastMs;           /* when the last sample was taken              */
    ULONG HaveLast;
    ULONG PeriodMs;         /* ACTIVE's cadence                            */
    ULONG InTransition;     /* a power or PnP sequence or a recovery step
                               runs: no sample is taken                    */
    ULONG Episode;          /* bumped at each loss (SUSPECT or CONFIG_LOST
                               entered from ACTIVE)                        */
    ULONG Reconciled;       /* the episode whose RECONCILE was asked       */
    ULONG Full;             /* the pending recovery is a full one          */
    ULONG Rearm;            /* after RECOVERY_FAILED: an all-ones sample
                               was seen, so a sane one may retry; the same
                               for CONTAINED's cooldown                    */
    ULONG Sub;              /* CONTAINED's substate, CM_LIVE_SUB_*         */
    CM_LIVE_COUNTERS Counters;
} CM_LIVE, *PCM_LIVE;

/* Once per start: OFF, the cadence (0 is taken as 200 ms). */
VOID CmLiveInit(PCM_LIVE live, ULONG usb4, ULONG periodMs);

/* After a bind or a resume succeeded: ACTIVE with `baseline` (the sample
 * read after the ring start; 0 detects all ones only). Refused (FALSE) for
 * an all-ones baseline, and once REVOKED or CONTAINED. */
BOOLEAN CmLiveArm(PCM_LIVE live, ULONG baseline, ULONG nowMs);

/* A stop, a suspend or a failure: OFF, keeping REVOKED and CONTAINED. */
VOID CmLiveDisarm(PCM_LIVE live);

/* A power or PnP sequence (or a recovery step) begins or ends: no sample
 * meanwhile, the consecutive count reset at both ends. */
VOID CmLiveTransition(PCM_LIVE live, BOOLEAN begin);

/* Whether a sample is due at `nowMs` (by elapsed time, so extra wakes add
 * no samples), and the longest the thread may wait before the next one
 * (0: no monitoring, wait for work). */
BOOLEAN CmLiveDue(const CM_LIVE *live, ULONG nowMs);
ULONG CmLiveWaitMs(const CM_LIVE *live);

/* One sample read at `nowMs`; returns CM_LIVE_ACT_* bits. */
ULONG CmLiveSample(PCM_LIVE live, ULONG nowMs, ULONG value);

/* The precondition's result after CM_LIVE_ACT_VALIDATE: TRUE -> RECOVERING
 * and RECOVER_LIGHT or RECOVER_FULL returned; FALSE -> RECOVERY_FAILED,
 * with RECONCILE if this episode had none yet. */
ULONG CmLiveValidated(PCM_LIVE live, BOOLEAN ok);

/* The recovery's result for `episode` (CmLiveEpisode when it began):
 * success with a final sample equal to the baseline (any readable value
 * where it was 0) -> ACTIVE (TRUE returned: publish); a failure, an
 * all-ones or a differing final sample -> RECOVERY_FAILED (then ask
 * CmLiveReconcileOwed). A result for another episode, or after REVOKED,
 * is dropped (FALSE). The caller serialises this call against
 * CmLiveRevoke so a publication cannot follow a revocation. */
BOOLEAN CmLiveRecovered(PCM_LIVE live, ULONG episode, BOOLEAN ok,
                        ULONG baseline, ULONG nowMs);

/* Surprise removal, stop or remove: REVOKED, terminal. Safe beside the
 * other calls (see the header). */
VOID CmLiveRevoke(PCM_LIVE live);

/* The gate: whether an access of `cls` (CM_ACCESS_*) is admitted now. */
BOOLEAN CmLiveAdmits(const CM_LIVE *live, ULONG cls);

/* Whether ordinary transport and lifecycle work may run (OFF or ACTIVE,
 * not revoked). The exit paths ask this before any register access. */
BOOLEAN CmLiveTransport(const CM_LIVE *live);

/* The current episode, for CmLiveRecovered. */
ULONG CmLiveEpisode(const CM_LIVE *live);

/* After a failed recovery: RECONCILE if this episode had none yet, so the
 * old generations are retired; 0 otherwise. */
ULONG CmLiveReconcileOwed(PCM_LIVE live);

/*
 * The USB4 profile's recovery (record 22 section 7.7 part 5's table; the
 * caller serialises them as the others).
 *
 * CmLiveContain: CONTAINED (probing) entered by the driver for a loss the
 * monitor did not record (a bind, a resume, a surprise removal's
 * containment): the episode bumped, the first probe a second away.
 * Nothing once REVOKED or already CONTAINED.
 *
 * CmLiveProbe: one probe at `nowMs` in CONTAINED (CM_LIVE_PROBE_*): from
 * probing, OK -> PREPARING (ACT_PREPARE), BAR -> cooldown, ONES or OTHER
 * stay; from cooldown, ONES sets Rearm and a readable probe with Rearm
 * set is a new episode -> probing (ACT_SNAPSHOT); final stays.
 *
 * CmLivePrepared: the preparation's result from PREPARING: TRUE ->
 * RECOVERING (ACT_RECOVER_FULL); FALSE -> CONTAINED (cooldown), Rearm
 * set where `ones` (a read came back all ones).
 *
 * CmLiveU4Recovered: the recovery's end for `episode`: with `ok` and the
 * fresh `sample` equal to the baseline, ACTIVE (TRUE: commit); `final`
 * -> CONTAINED (final); anything else -> CONTAINED (cooldown), Rearm set
 * where `ones` or the sample read all ones. A result for another episode
 * or after REVOKED is dropped (FALSE), the state unchanged.
 *
 * CmLiveU4Interrupt: a system suspend (D3) during PREPARING or
 * RECOVERING: CONTAINED (cooldown) with Rearm set, so the probe at the
 * resume (CmLiveU4Wake: due at once, and with Rearm set a new episode
 * in probing at once, ACT_SNAPSHOT) takes the probing row once the
 * header is proven: no second outage is asked of a power-cancelled
 * attempt.
 */
VOID CmLiveContain(PCM_LIVE live, ULONG nowMs);
ULONG CmLiveProbe(PCM_LIVE live, ULONG nowMs, ULONG result);
ULONG CmLivePrepared(PCM_LIVE live, BOOLEAN ok, BOOLEAN ones);
BOOLEAN CmLiveU4Recovered(PCM_LIVE live, ULONG episode, BOOLEAN ok,
                          BOOLEAN final, BOOLEAN ones, ULONG sample,
                          ULONG nowMs);
VOID CmLiveU4Interrupt(PCM_LIVE live);
ULONG CmLiveU4Wake(PCM_LIVE live);

#endif /* CM_LIVE_H */
