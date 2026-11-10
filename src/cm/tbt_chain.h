/*
 * tbt_chain.h - which chain port a chained tunnel lands on, and which
 * chained tunnel owns it: the structural and occupancy bindings, learnt in
 * a transaction around the kid's own Path Enable (design record 34
 * section 3.12, roadmap-usb4.md task 48.30). The pure half.
 *
 * THE BINDINGS. A structural binding (S) ties a parent's link - its host
 * port, the port's connection generation, the parent router's UUID and
 * the parent's PCIe Down adapter of the link - to one chain port: its
 * location, its secondary and subordinate buses, the member it sits under
 * and, once a filter is seen on it, that filter's incarnation. An
 * occupancy binding (O) ties the kid on S's link - its slot, generation,
 * route and UUID - to S. Each S has an epoch drawn from a counter of the
 * domain that never repeats in its life. S and O are the evidence the
 * chained unit's eject rests on: a kid with no O has no chain port as far
 * as this driver knows.
 *
 * THE TRANSACTION. Opened (XhciChainArm) immediately before a kid's
 * first Path Enable, against a baseline read twice, back to back, the
 * same both times: the chain-port walk under the port's member complete,
 * every candidate numbered, and each candidate's secondary bus with the
 * devices whose function 0 answers. Then, at the kid's tunnel up and at
 * each pass while open (XhciChainTry), the same reads once: exactly one
 * candidate that answered nothing at the baseline gaining, nothing else
 * changed, no other kid unsettled, and what it gained a tunnel's far end
 * or its port read down at the arm and up now (XhciTbtGainAdmits, record
 * 22 section 7.6.4), learns S (or keeps it, when it names that
 * candidate) and O. A kid whose tunnel came up and is neither
 * attributed nor resolved is unsettled; while one is, no other kid's gain
 * is attributed, and a fresh kid's connect waits (XhciChainMayConnect)
 * up to XHCI_CHAIN_TXN_MS from that kid's Path Enable.
 *
 * C89, DDK-free: part of the pure core, no kernel service, lock or
 * allocation. The reads go through the caller's XHCI_TBT_CFG. IRQL: the
 * functions that take an XHCI_TBT_CFG at its accessors' (PASSIVE_LEVEL on
 * the connection-manager thread in the driver), the rest any. The caller
 * serialises every call on one XHCI_CHAIN. Host suite:
 * test\test_tbt_chain.c.
 */

#ifndef TBT_CHAIN_H
#define TBT_CHAIN_H

#include "tbt_topo.h"

#define XHCI_CHAIN_KIDS         4UL     /* TBT_U4_MAX_KIDS                  */
#define XHCI_CHAIN_BINDS        4UL     /* one per parent link              */
#define XHCI_CHAIN_TXN_MS       10000UL /* record 34 section 3.1's span     */

/* A candidate's buses, as the baseline keeps them: secondary 7:0,
 * subordinate 15:8. */
#define XHCI_CHAIN_BUSES(sec, sub) \
    (((ULONG)(sec) & 0xFFUL) | (((ULONG)(sub) & 0xFFUL) << 8))

/* One reading of the chain ports under a member. */
typedef struct _XHCI_CHAIN_BASE {
    ULONG Count;
    ULONG Bdf[XHCI_TBT_CHAIN_MAX];
    ULONG Buses[XHCI_TBT_CHAIN_MAX];
    ULONG Seen[XHCI_TBT_CHAIN_MAX];     /* bit d: device d's function 0   */
    ULONG Down;         /* bit i: Bdf[i]'s port read DOWN with nothing
                           behind it (XhciTbtPortDown), at the arm alone */
    ULONG PartDev;      /* UNREAD alone: candidate Count's devices 0 to
                           PartDev - 1 were read before the failure      */
    ULONG Walk;         /* the walk's candidates, every Bdf[] filled; 0
                           when the walk itself did not complete         */
    ULONG Scanned;      /* the candidates whose occupancy scan began     */
} XHCI_CHAIN_BASE, *PXHCI_CHAIN_BASE;

/* XhciChainRead's return. */
#define XHCI_CHAIN_READ_OK      0UL
#define XHCI_CHAIN_READ_UNREAD  1UL     /* a failed read, or Retry Status   */
#define XHCI_CHAIN_READ_WALK    2UL     /* the walk incomplete or over      */
#define XHCI_CHAIN_READ_NUMBER  3UL     /* a candidate not numbered inside
                                           the member's range               */
#define XHCI_CHAIN_READ_NONE    4UL     /* no candidate                     */

/*
 * The chain ports under the member at `memberBdf` (index `member`) and,
 * for each, which devices answer function 0 on its secondary bus. Reads
 * only. Anything but OK leaves *out not to be compared, but for UNREAD's
 * partial record: out->Walk and every out->Bdf[] and out->Buses[] (as the
 * walk read them, and the reading refuses NUMBER where a candidate's buses
 * then read otherwise) when the walk completed, out->Scanned candidates
 * whose occupancy scan began, out->Count of them completed, and the
 * devices read until the failure (out->PartDev of the one after them).
 */
ULONG XhciChainRead(PXHCI_TBT_CFG io, ULONG memberBdf, ULONG member,
                    PXHCI_CHAIN_BASE out);

/* What the caller knows of a kid when it arms or tries (the kid's own and
 * its parent's, read from the connection manager on its thread). */
typedef struct _XHCI_CHAIN_KID {
    ULONG Port;             /* the host port, 1-based                     */
    ULONG Kid;              /* the kid's slot, 1-based                    */
    ULONG KidGen;
    ULONG ParentGen;        /* the host port's connection generation      */
    ULONG ParentUuid[2];
    ULONG ParentPcieDown;
    ULONG KidRoute[2];
    ULONG KidUuid[2];
    ULONG Member;           /* the port's member by correspondence        */
    ULONG MemberBdf;
} XHCI_CHAIN_KID, *PXHCI_CHAIN_KID;

typedef struct _XHCI_CHAIN_BIND {
    ULONG Used;
    /* S */
    ULONG Port;
    ULONG ParentGen;
    ULONG ParentUuid[2];
    ULONG ParentPcieDown;
    ULONG Member;
    ULONG MemberBdf;
    ULONG Bdf;              /* the chain port                             */
    ULONG Buses;            /* XHCI_CHAIN_BUSES                           */
    ULONG Epoch;
    ULONG Inc;              /* its filter's incarnation, 0 none seen yet  */
    /* O */
    ULONG Occupied;
    ULONG Kid;
    ULONG KidGen;
    ULONG KidRoute[2];
    ULONG KidUuid[2];
    ULONG ArmSeq;           /* the learning transaction's arm stamp       */
    ULONG LearnSeq;         /* the caller's stamp at LEARNED, 0 before    */
} XHCI_CHAIN_BIND, *PXHCI_CHAIN_BIND;

typedef struct _XHCI_CHAIN_TXN {
    ULONG Open;
    XHCI_CHAIN_KID K;
    XHCI_CHAIN_BASE Base;
    ULONG OpenMs;
    ULONG ArmSeq;           /* the caller's stamp at the arm (record 34
                               section 3.12 item 5)                       */
    ULONG Answered;         /* bit i: Base.Bdf[i] read gaining at a try
                               that waited: empty again spoils it         */
} XHCI_CHAIN_TXN, *PXHCI_CHAIN_TXN;

typedef struct _XHCI_CHAIN_UNSET {
    ULONG Used;
    ULONG Kid;
    ULONG KidGen;
    ULONG SinceMs;          /* its Path Enable                            */
} XHCI_CHAIN_UNSET;

typedef struct _XHCI_CHAIN {
    XHCI_CHAIN_TXN Txn;
    XHCI_CHAIN_BIND Bind[XHCI_CHAIN_BINDS];
    XHCI_CHAIN_UNSET Unset[XHCI_CHAIN_KIDS];
    ULONG NextEpoch;
    /* counters */
    ULONG Armed;
    ULONG NotArmed;
    ULONG Learned;
    ULONG Spoiled;
    ULONG Expired;
    ULONG Conflicts;
    ULONG Cancelled;
    ULONG Withdrawn;
    ULONG Held;             /* MAY_CONNECT refusals                       */
} XHCI_CHAIN, *PXHCI_CHAIN;

/* Nothing bound, nothing open, nothing unsettled; the epochs start again
 * only at a new domain (NextEpoch is kept when `keepEpoch`). */
VOID XhciChainInit(PXHCI_CHAIN c, BOOLEAN keepEpoch);

/*
 * 1 while a fresh kid's connect may start: no transaction open, and no
 * kid unsettled, each for less than XHCI_CHAIN_TXN_MS since its opening or
 * its Path Enable; else 0 (counted).
 */
ULONG XhciChainMayConnect(PXHCI_CHAIN c, ULONG nowMs);

/*
 * The arm, immediately before kid k's first Path Enable: the kid becomes
 * unsettled (whatever else happens), and, when no transaction is open, no
 * other kid is unsettled, nothing else may carry an unowned live path
 * (`blocked`: the caller's reading of the connection manager's other kid
 * slots in use without an occupancy and its watched links, record 34
 * section 3.12 item 3), k->Member is known and the baseline reads OK
 * twice and the same, the transaction opens. Returns XHCI_CHAIN_ARM_*.
 */
#define XHCI_CHAIN_ARM_OPEN     0UL
#define XHCI_CHAIN_ARM_BUSY     1UL     /* a transaction open already       */
#define XHCI_CHAIN_ARM_OTHER    2UL     /* another kid unsettled            */
#define XHCI_CHAIN_ARM_MEMBER   3UL     /* no member known                  */
#define XHCI_CHAIN_ARM_READ     4UL     /* the baseline not read OK          */
#define XHCI_CHAIN_ARM_UNSTABLE 5UL     /* the two readings differ          */
#define XHCI_CHAIN_ARM_PARAM    6UL
ULONG XhciChainArm(PXHCI_CHAIN c, PXHCI_TBT_CFG io, const XHCI_CHAIN_KID *k,
                   BOOLEAN blocked, ULONG nowMs, ULONG armSeq,
                   PULONG readWhy);

/*
 * One attribution try of the open transaction. `live` is the caller's
 * reading that the transaction's kid still holds its generation with its
 * tunnel up, its parent the same connection, and the domain running;
 * `blocked` as XhciChainArm's, read again now.
 * Returns XHCI_CHAIN_TRY_*; on LEARNED *bind is the binding's index.
 */
#define XHCI_CHAIN_TRY_NONE     0UL     /* no transaction open              */
#define XHCI_CHAIN_TRY_WAIT     1UL
#define XHCI_CHAIN_TRY_LEARNED  2UL
#define XHCI_CHAIN_TRY_SPOILED  3UL
#define XHCI_CHAIN_TRY_SEVERAL  4UL     /* spoiled: another kid unsettled,
                                           or the gain on a busy port      */
#define XHCI_CHAIN_TRY_CONFLICT 5UL     /* spoiled: S named another port    */
#define XHCI_CHAIN_TRY_CANCELLED 6UL
#define XHCI_CHAIN_TRY_EXPIRED  7UL
#define XHCI_CHAIN_TRY_FULL     8UL     /* spoiled: no binding free         */
#define XHCI_CHAIN_TRY_SHAPE    9UL     /* spoiled: what B gained is no
                                           tunnel's far end and its port
                                           was not read down at the arm and
                                           up now (record 22 section 7.6.4) */
ULONG XhciChainTry(PXHCI_CHAIN c, PXHCI_TBT_CFG io, BOOLEAN live,
                   BOOLEAN blocked, ULONG nowMs, PULONG bind);

/*
 * A kid resolved: its slot no longer holds generation `kidGen` (or any,
 * kidGen 0), or its path proven closed for it. It stops being unsettled,
 * an open transaction of it is cancelled, its O withdrawn. Returns 1 when
 * an O was withdrawn.
 */
ULONG XhciChainKidGone(PXHCI_CHAIN c, ULONG kid, ULONG kidGen);

/*
 * Each pass, per kid slot: `used` and `gen` as the slot reads now. A kid
 * unsettled or occupying under another generation, or a slot no longer
 * used, is resolved (XhciChainKidGone). Returns 1 when an O was
 * withdrawn.
 */
ULONG XhciChainKidCheck(PXHCI_CHAIN c, ULONG kid, BOOLEAN used, ULONG gen);

/*
 * Each pass, per host port: `up` (the port UP with its router present)
 * and its generation now. Every S of the port under another generation,
 * or with the port not up, is withdrawn with its O, and an open
 * transaction of the port cancelled. Returns the S withdrawn, as a mask
 * of binding indices.
 */
ULONG XhciChainPortCheck(PXHCI_CHAIN c, ULONG port, BOOLEAN up, ULONG gen);

/* Binding `i` withdrawn (S and O). */
VOID XhciChainWithdraw(PXHCI_CHAIN c, ULONG i);

/* Every binding withdrawn, the transaction cancelled, nothing unsettled
 * (the domain's stop, a loss or a recovery). */
VOID XhciChainWithdrawAll(PXHCI_CHAIN c);

/* The binding whose O is kid `kid` of generation `kidGen` (kidGen 0: any),
 * or XHCI_CHAIN_BINDS. */
ULONG XhciChainOfKid(const XHCI_CHAIN *c, ULONG kid, ULONG kidGen);

/* The binding of the chain port at `bdf` under the member at
 * `memberBdf`, or XHCI_CHAIN_BINDS. */
ULONG XhciChainOfPort(const XHCI_CHAIN *c, ULONG memberBdf, ULONG bdf);

/*
 * A filter of incarnation `inc` seen classified on binding i's chain
 * port: the first fixes S's incarnation (1); the same again 1; another
 * withdraws the binding (0).
 */
ULONG XhciChainIncarnation(PXHCI_CHAIN c, ULONG i, ULONG inc);

/*
 * The occupancy token of binding i: tok[0] the valid bit, the host port
 * in 7:0 and the kid in 15:8; tok[1] the kid's generation; tok[2] S's
 * epoch. All 0 when the binding holds no O.
 */
#define XHCI_CHAIN_TOK_VALID    0x80000000UL
#define XHCI_CHAIN_TOK_WORDS    3UL
#define XHCI_CHAIN_TOK_PORT(t)  ((t)[0] & 0xFFUL)
#define XHCI_CHAIN_TOK_KID(t)   (((t)[0] >> 8) & 0xFFUL)
VOID XhciChainToken(const XHCI_CHAIN *c, ULONG i, PULONG tok);

/* 1 when tok names binding i's O as it stands now. */
ULONG XhciChainTokenLive(const XHCI_CHAIN *c, ULONG i, const ULONG *tok);

#endif /* TBT_CHAIN_H */
