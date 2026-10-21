/*
 * cm_statb.h - the builder of the status record (cm_status.h; design
 * record 24 section 3): pure, DDK-free, host-tested by
 * test\test_cm_status.c. cm_statos.c publishes what it builds.
 *
 * IRQL: any; no lock, no allocation. The caller passes a snapshot it owns
 * (the connection-manager thread reads its own state).
 */

#ifndef CM_STATB_H
#define CM_STATB_H

#include "tbt_icm.h"
#include "tbt_u4cm.h"
#include "tbt_topo.h"
#include "cm_status.h"

/* The domain's side of the record: what is not in TBT_ICM or TBT_U4CM. */
typedef struct _CM_STATUS_IN {
    ULONG NhiId;            /* vendor 15:0, device 31:16                   */
    ULONG Profile;          /* CM_PROFILE_*                                */
    ULONG Stage;            /* CM_ST_STAGE_*                               */
    ULONG Inert;            /* CM_INERT_*                                  */
    ULONG Cm;               /* CM_ST_CM_*                                  */
    ULONG PortCount;        /* the tunnel-port set, up to CM_ST_MAX_PORTS  */
    ULONG PortBdf[CM_ST_MAX_PORTS];
    ULONG PortActive;       /* bit n: an active member                     */
    ULONG PathCount[CM_ST_MAX_PORTS];   /* 0 where unknown                 */
    ULONG Path[CM_ST_MAX_PORTS][CM_ST_PATH_MAX];
    ULONG Echo;             /* the ask last read (record 24 section 3.3)   */
    ULONG IcmNoUsb3;        /* a Thunderbolt 3 ICM host (Falcon, Alpine or
                               Titan Ridge, not Maple Ridge): no USB3
                               tunnel exists                              */
} CM_STATUS_IN, *PCM_STATUS_IN;

/*
 * The record into buf (at least CM_ST_MAX_BYTES), from `in` and whichever
 * connection manager runs: `icm` under CM_ST_CM_ICM, `u4` (with `binds`,
 * the port-to-bridge correspondence, or NULL) under CM_ST_CM_USB4; both
 * NULL otherwise. The stamp is left 0 (CmStatusStamp).
 * Returns the record's length in bytes.
 */
ULONG CmStatusBuild(UCHAR *buf, const CM_STATUS_IN *in, const TBT_ICM *icm,
                    const TBT_U4CM *u4, const XHCI_TBT_BINDS *binds);

/* TRUE when two records differ in anything but the stamp. */
BOOLEAN CmStatusChanged(const UCHAR *a, ULONG aBytes, const UCHAR *b,
                        ULONG bBytes);

/* The stamp: the generation, the system's uptime in seconds and the
 * system time as KeQuerySystemTime returned it (two words, no arithmetic):
 * the tool tells this boot's record from an earlier boot's by them. */
VOID CmStatusStamp(UCHAR *buf, ULONG generation, ULONG uptimeS,
                   ULONG systemLo, ULONG systemHi);

/* The Windows 98 instance path of the function at `bdf` through `cfg`'s
 * reader: up to `room` elements, leaf first, then the root bus. FALSE
 * where it cannot be read whole or is longer. PASSIVE_LEVEL where the
 * reader needs it. */
BOOLEAN CmStatusPciPath(const XHCI_TBT_CFG *cfg, ULONG bdf, PULONG path,
                        PULONG count, ULONG room);

/* The publication's state, one per NHI (CM_NHI.StatusPub, cm_statos.c);
 * touched by the connection-manager thread alone. */
/* XhciCmBridgeDiag (record 21 section 6.1.1): the bus numbering's last
 * attempt, dwords: 0 sequence, 1 port bdf, 2 outcome, 3 seen (functions
 * 23:16, bridges 15:8, tries 7:0), 4 the port's 18h, 5 window outcome,
 * 6 the port's 20h, 7 window need, 8 where, 9 undone (failed 31:16),
 * 10-11 the first two bridges' bdf, 12-13 their 18h as left, 14-15 their
 * 20h as left, 16 flags (bit 0 held by a child devnode, bit 1 PCI.VXD's
 * guard refused, bit 2 the port read up), 17 the guard's reading, 18 the
 * interrupt lines (outcome 7:0, the port's line 15:8, its pin 23:16),
 * 19 their counts (functions 7:0, set 15:8, kept 23:16, other pin 31:24). */
#define CM_BRDIAG_WORDS 20

/* XhciCmNoIrqDiag (design record 28 section 7, task 42-A.15): the last
 * no-interrupt attempt of any member, twelve dwords as cm_noirq.h's
 * CM_NI_DIAG_WORDS lays them out (0 the sequence). */
#define CM_NIDIAG_WORDS 12

typedef struct _CM_STATUS_PUB {
    PVOID DevKey;           /* the hardware key, opened in the start      */
    PVOID DrvKey;           /* the driver key, for the ask                */
    ULONG Echo;             /* the ask last read                          */
    ULONG AskAtMs;          /* when it was read                           */
    ULONG Started;          /* a pass has seen the instance started       */
    ULONG Published;        /* a record was written in this load          */
    ULONG Generation;
    ULONG LastBytes;        /* 0: nothing written since the start         */
    ULONG BuiltBytes;       /* Built's length, 0 before the first build:
                               the query device's record (cm_qryos.c)     */
    ULONG Writes;
    ULONG Failures;
    ULONG LastError;        /* the last failed write's NTSTATUS           */
    ULONG Dirty;            /* the last write failed: try again           */
    ULONG PathBdf[CM_ST_MAX_PORTS];     /* 98: the paths computed, for   */
    ULONG PathCount[CM_ST_MAX_PORTS];   /* these members (0: unknown)    */
    ULONG PathAtMs[CM_ST_MAX_PORTS];    /* when an unknown one was tried */
    ULONG Path[CM_ST_MAX_PORTS][CM_ST_PATH_MAX];
    UCHAR Last[CM_ST_MAX_BYTES];
    UCHAR Built[CM_ST_MAX_BYTES];
} CM_STATUS_PUB;

#endif /* CM_STATB_H */
