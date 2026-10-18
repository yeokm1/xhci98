/*
 * cm_noirq.h - a Windows 98 SE or ME function with no interrupt, started
 * (design record 28; roadmap-usb4.md task 42-A.15): the pure half.
 *
 * WHAT IT DECIDES. An NVMe function below a tunnel port whose interrupt
 * cannot be routed (its pin swizzles onto a wire the port's line is not
 * on, or the port has no line of its own) sits at a configuration problem
 * (Code 29 on the P14s) because PCI.VXD's configuration asks for an IRQ.
 * Record 28 section 4 gives it a second basic logical configuration at
 * LCPRI_SUBOPTIMAL: the enumerator's memory descriptors copied as read, no
 * IRQ descriptor, created at LCPRI_DISABLED and enabled last, then
 * Reprocess_DevNode. Stor98 (0.4.0.0 or later) then reads pin and a line
 * of 0 or FFh and polls. This file holds everything of that which reads
 * no OS state: the candidate rules (section 4 rules 1 to 5), the result
 * table, the per-device attempt state and the journal across generations,
 * the gate over the existing configurations, the descriptor copy rules,
 * the transaction's steps, the budgets, the diagnostic and the counters.
 *
 * THE INTERFACE: A STATE MACHINE OF OPERATIONS. Nothing here calls
 * CONFIGMG or PCI.VXD. The member's state hands out one operation at a
 * time (CmNiNext: a CM_NI_OP with a fresh id, its kind, devnode, location,
 * handles and buffer offsets) and takes back that operation's result
 * (CmNiTake: a CM_NI_RES with the same id); one operation is outstanding
 * per member at most. Record 28 places the calls on the member's call
 * worker behind interface 3's R6f handoff word (record 21 section 7.2's
 * 9x half, task 42-A.12); until those workers exist the 98 CONFIGMG
 * worker (cm_cfgwk.c) makes each operation itself, at once, and takes its
 * result (the interim of record 28's dated note, 2026-10-19). The machine
 * is the same for both: a result that comes back for an operation whose
 * transaction has ended meanwhile (its budget spent, the gate closed, a
 * new attempt) is taken by its id and accounted - an Add_Empty_Log_Conf
 * that succeeded journalled as an uncommitted leftover, a priority change
 * that succeeded marked committed - and never acted on (section 4,
 * "Where").
 *
 * The clock is the caller's, in ms, read on the thread that advances the
 * machine (CM_NI_CTX.Now); intervals are differences, so it may wrap.
 *
 * IRQL: any; no lock, no DDK call. Host suite: test\test_cm_noirq.c.
 */

#ifndef CM_NOIRQ_H
#define CM_NOIRQ_H

#include "tbt_brnum.h"

/* Bounds and budgets (record 28 section 4). */
#define CM_NOIRQ_WINDOW_MS      10000UL /* a member's admission window     */
#define CM_NOIRQ_TXN_MS         30000UL /* an attempt's own budget         */
#define CM_NOIRQ_DEVICES        8UL     /* devices a member                */
#define CM_NOIRQ_ATTEMPTS       3UL     /* configurations created a devnode,
                                           in the member's life           */
#define CM_WK_NOIRQ_MEM         9UL     /* memory descriptors copied       */
#define CM_NI_DESC_BYTES        256UL   /* one descriptor's room           */
#define CM_NI_LC_MAX            16UL    /* configurations read on one list */
#define CM_NI_READY_AFTER_MS    3000UL  /* ATTEMPTED with the problem still
                                           there this long: the one
                                           Setup_DevNode(READY) fallback  */

/* Values of CONFIGMG.H (cm_cfgmg.h cites the lines). */
#define CM_NI_LCPRI_FORCECONFIG 0x0000UL
#define CM_NI_LCPRI_SUBOPTIMAL  0x5000UL
#define CM_NI_LCPRI_DISABLED    0xFFFFUL
#define CM_NI_BASIC_LOG_CONF    0x0UL
#define CM_NI_FORCED_LOG_CONF   0x4UL
#define CM_NI_RES_MEM           1UL
#define CM_NI_RES_IO            2UL
#define CM_NI_RES_DMA           3UL
#define CM_NI_RES_IRQ           4UL
#define CM_NI_CR_SUCCESS        0x00UL
#define CM_NI_CR_INVALID_DEVNODE 0x05UL
#define CM_NI_CR_NO_SUCH_DEVNODE 0x0DUL
#define CM_NI_CR_NO_MORE_LOG_CONF 0x0EUL
#define CM_NI_CR_NO_MORE_RES_DES 0x0FUL
#define CM_NI_CR_FAILURE        0x13UL
#define CM_NI_DN_STARTED        0x00000008UL
#define CM_NI_DN_HAS_PROBLEM    0x00000400UL

/* Device states (section 4, "the attempt state"). */
#define CM_NI_S_FREE            0UL
#define CM_NI_S_PENDING         1UL
#define CM_NI_S_EXCLUDED        2UL
#define CM_NI_S_ACTIVE          3UL
#define CM_NI_S_ATTEMPTED       4UL
#define CM_NI_S_DONE            5UL
#define CM_NI_S_FAILED          6UL

/* Why an entry is EXCLUDED or FAILED (the result table's notes). */
#define CM_NI_W_NONE            0UL
#define CM_NI_W_OFF             1UL     /* XhciCmNoIrqStart 0              */
#define CM_NI_W_NOT_CONFIG      2UL     /* started, or another problem     */
#define CM_NI_W_HEADER          3UL     /* class, header, pin, line, I/O   */
#define CM_NI_W_NOT_STOR98      4UL
#define CM_NI_W_INTERRUPT       5UL     /* routable, or routed             */
#define CM_NI_W_CONFIG          6UL     /* the configurations' shape       */
#define CM_NI_W_LEFTOVER        7UL     /* ... one exactly at SUBOPTIMAL
                                           not the journal's               */
#define CM_NI_W_ATTEMPTS        8UL
#define CM_NI_W_NOT_IN_TIME     9UL     /* PENDING at the window's close   */
#define CM_NI_W_NO_PIN          10UL    /* Interrupt Pin 0: not a candidate,
                                           counted (section 5.4)          */
#define CM_NI_W_HANDLE          11UL    /* the handle's place changed      */
#define CM_NI_W_TIMEOUT         12UL
#define CM_NI_W_CONTROLLER      13UL    /* the loss, a member gate, the
                                           claim                           */
#define CM_NI_W_STEP            14UL    /* a step refused by CONFIGRET     */
#define CM_NI_W_ADMISSION       15UL    /* rule 2 or 4 no longer met       */
#define CM_NI_W_COUNT           16UL

/* The interrupt outcome a candidate rule used (section 4 rule 5). */
#define CM_NI_IRQ_NONE          0UL     /* not read                        */
#define CM_NI_IRQ_WRITTEN       1UL     /* ROUTED_WRITTEN                  */
#define CM_NI_IRQ_ALREADY       2UL     /* ROUTED_ALREADY                  */
#define CM_NI_IRQ_UNROUTABLE    3UL
#define CM_NI_IRQ_PORT_NO_LINE  4UL
#define CM_NI_IRQ_UNKNOWN_HELD  5UL     /* a write not held: excluded      */
#define CM_NI_IRQ_UNKNOWN_READ  6UL     /* a read failed: classified afresh */
#define CM_NI_IRQ_ROUTABLE      7UL     /* a fresh reading: the swizzle on
                                           the port's pin, its line valid  */

/* Operations (each one handed to the caller, its result taken back). */
#define CM_NI_OP_NONE           0UL
#define CM_NI_OP_HEADER         1UL     /* PCI.VXD: dwords 00h-3Ch of Bdf
                                           into Val[0..15]                 */
#define CM_NI_OP_STATUS         2UL     /* Get_DevNode_Status(Dn) and the
                                           instance id's hash              */
#define CM_NI_OP_BINDING        3UL     /* Read_Registry_Value(Dn, software
                                           key) of DevLoader, PortDriver:
                                           Bind (CmNiBindFlags)            */
#define CM_NI_OP_PORT_INTR      4UL     /* PCI.VXD: the member's 3Ch in
                                           Val[0], its id (00h) in Val[1] */
#define CM_NI_OP_LC_LIST        5UL     /* Get_First/Next_Log_Conf(Dn, LcKind)
                                           with each one's priority        */
#define CM_NI_OP_RD_READ        6UL     /* Get_Next_Res_Des of ResType past
                                           Lc (a configuration or a
                                           descriptor), its size, and its
                                           data into the member's Desc at
                                           Off, Len room. The source's
                                           descriptors are read one type at
                                           a time - memory, IRQ, I/O, DMA -
                                           rather than with ResType_All
                                           (read on 98 SE, 2026-10-19: a
                                           ResType_All read of PCI.VXD's
                                           configuration of an NVMe at Code
                                           29 answered no descriptor)      */
#define CM_NI_OP_ADD_EMPTY      7UL     /* Add_Empty_Log_Conf(Dn,
                                           LCPRI_DISABLED, LcKind |
                                           PRIORITY_EQUAL_LAST)            */
#define CM_NI_OP_PLACE          8UL     /* the hash of Dn's id, then Lc's
                                           place on Dn's LcKind list       */
#define CM_NI_OP_ADD_RD         9UL     /* Add_Res_Des(Lc, ResType_Mem,
                                           Desc + Off, Len)                */
#define CM_NI_OP_SET_PRI        10UL    /* Get_Set_Log_Conf_Priority(SET,
                                           Lc, Pri)                        */
#define CM_NI_OP_REPROCESS      11UL    /* Reprocess_DevNode(Dn, ASYNC)    */
#define CM_NI_OP_READY          12UL    /* Setup_DevNode(Dn, READY)        */
#define CM_NI_OP_LINE           13UL    /* PCI.VXD: 3Ch of Bdf, logged     */

/* CM_NI_RES.Bind (CmNiBindFlags). */
#define CM_NI_BIND_DL           0x01UL  /* DevLoader read                  */
#define CM_NI_BIND_DL_IOS       0x02UL  /* ... and it is *IOS              */
#define CM_NI_BIND_PD           0x04UL  /* PortDriver read                 */
#define CM_NI_BIND_PD_STOR98    0x08UL  /* ... and it is stor98.mpd        */
#define CM_NI_BIND_OK           0x0FUL

/* CM_NI_RES.Place. */
#define CM_NI_PLACE_GONE        1UL     /* Dn answers no such devnode      */
#define CM_NI_PLACE_OTHER       2UL     /* Dn answers another instance id  */
#define CM_NI_PLACE_MISSING     3UL     /* Lc not on the LcKind list       */
#define CM_NI_PLACE_AT          4UL     /* found, at PlacePri              */
#define CM_NI_PLACE_FAILED      5UL     /* a read failed (Cr)              */

/* CM_NI_CTX.Stor98: the installer's record (record 26 section 6.3). */
#define CM_NI_STOR98_OK         1UL     /* 0.4.0.0 or later                */
#define CM_NI_STOR98_NONE       2UL     /* no record                       */
#define CM_NI_STOR98_OLD        3UL     /* an older version, or not one    */
#define CM_NI_STOR98_FAILED     4UL     /* the read failed                 */

typedef struct _CM_NI_OP {
    ULONG Id;               /* nonzero                                    */
    ULONG Kind;             /* CM_NI_OP_*                                 */
    ULONG Dev;              /* the entry it is for                        */
    ULONG Txn;              /* that entry's attempt serial                */
    ULONG Dn;
    ULONG Hash;             /* the instance id's hash expected            */
    ULONG Bdf;              /* HEADER, LINE: the function; PORT_INTR: the
                               member                                    */
    ULONG Lc;               /* RD_READ, PLACE, ADD_RD, SET_PRI            */
    ULONG LcKind;           /* LC_LIST, ADD_EMPTY, PLACE                  */
    ULONG Pri;              /* SET_PRI                                    */
    ULONG Off;              /* RD_READ, ADD_RD: in the member's Desc      */
    ULONG Len;              /* RD_READ: the room; ADD_RD: the length      */
    ULONG ResType;          /* RD_READ: the resource type asked for       */
} CM_NI_OP, *PCM_NI_OP;

typedef struct _CM_NI_RES {
    ULONG Id;               /* the operation's                            */
    ULONG Cr;               /* the CONFIGRET that decides (a PCI.VXD
                               operation: CM_NI_CR_FAILURE when its route
                               is not open)                              */
    ULONG Status;           /* STATUS                                     */
    ULONG Problem;
    ULONG Hash;             /* STATUS: the id's hash; 0 when not read     */
    ULONG Val[16];          /* HEADER; PORT_INTR, LINE: Val[0]            */
    ULONG Bind;             /* BINDING                                    */
    ULONG Count;            /* LC_LIST                                    */
    ULONG Cut;              /* ... more than CM_NI_LC_MAX                 */
    ULONG Lc[CM_NI_LC_MAX];
    ULONG Pri[CM_NI_LC_MAX];
    ULONG Rd;               /* RD_READ: the descriptor                    */
    ULONG Type;             /* ... its resource type                      */
    ULONG Size;             /* ... its size (data read only when <= Len)  */
    ULONG Handle;           /* ADD_EMPTY                                  */
    ULONG Place;            /* PLACE: CM_NI_PLACE_*                       */
    ULONG PlacePri;
} CM_NI_RES, *PCM_NI_RES;

/* What the caller reads for a pass. */
typedef struct _CM_NI_CTX {
    ULONG Now;              /* ms                                         */
    ULONG Switch;           /* XhciCmNoIrqStart as run with (0 or 1)      */
    ULONG Stor98;           /* CM_NI_STOR98_*                             */
    ULONG Gate;             /* nonzero while every submission may go on:
                               the worker not leaving, the claim held,
                               no loss owed (record 28, "the controller's
                               loss and the member's gates")             */
} CM_NI_CTX, *PCM_NI_CTX;

/* The numbering's interrupt outcomes for one member and generation
 * (CmNiIrqFromBrnum). */
typedef struct _CM_NI_IRQ {
    ULONG Valid;            /* the stage ran: the record stands           */
    ULONG PortNoLine;       /* XHCI_TBT_BRIRQ_PORT_NONE                   */
    ULONG Count;
    ULONG Bdf[XHCI_TBT_BRNUM_MAX_LINES];
    ULONG Out[XHCI_TBT_BRNUM_MAX_LINES];    /* CM_NI_IRQ_*                */
} CM_NI_IRQ, *PCM_NI_IRQ;

/* One journalled configuration (section 4, "the journal"). */
typedef struct _CM_NI_JENT {
    ULONG Lc;
    ULONG Kind;             /* CM_NI_BASIC_LOG_CONF (the forced form is not
                               built: section 5.2 is the leg's verdict)  */
    ULONG Gen;
    ULONG Committed;
    ULONG Gone;             /* no longer this driver's (rule 1)           */
} CM_NI_JENT;

typedef struct _CM_NI_JRN {
    ULONG Used;
    ULONG Dn;
    ULONG Hash;
    ULONG Created;          /* configurations created, the bound's count  */
    ULONG ReprocGen;        /* the generation of the last reprocess-only
                               attempt, plus one (0: none)               */
    CM_NI_JENT E[CM_NOIRQ_ATTEMPTS];
} CM_NI_JRN;

typedef struct _CM_NI_DEV {
    ULONG Used;
    ULONG Dn;
    ULONG Hash;
    ULONG Bdf;
    ULONG Rot;              /* the swizzle so far: the device numbers of
                               the bridges between the member and it     */
    ULONG State;            /* CM_NI_S_*                                  */
    ULONG Why;              /* CM_NI_W_*                                  */
    ULONG Step;             /* the next step (cm_noirq.c)                 */
    ULONG Gen;              /* the generation the state belongs to        */
    ULONG Txn;              /* bumped by every new attempt                */
    ULONG Pass;             /* the pass it was last stepped in            */
    ULONG Deadline;         /* ACTIVE, ATTEMPTED                          */
    ULONG AttemptedAt;
    ULONG ReadyTried;
    ULONG ReprocOnly;
    ULONG Pin;
    ULONG Line;
    ULONG Irq;              /* CM_NI_IRQ_* used                           */
    ULONG ProbBefore;
    ULONG Source;           /* the configuration whose descriptors are
                               copied                                    */
    ULONG Alt;              /* the configuration this attempt added       */
    ULONG AltDone;          /* ... and committed                          */
    ULONG HaveIrq;
    ULONG MemCount;
    ULONG MemOff[CM_WK_NOIRQ_MEM];
    ULONG MemLen[CM_WK_NOIRQ_MEM];
    ULONG MemAdded;
    ULONG RdAt;             /* the descriptor read past next (RD_READ)    */
    ULONG RdPhase;          /* the type being read: 0 memory, 1 IRQ, 2 I/O,
                               3 DMA                                     */
    ULONG LastStep;         /* the diagnostic's step reached              */
    ULONG LastCr;
    ULONG Pin0Gen;          /* a pin-0 function counted for gen + 1       */
    ULONG WalkSeq;          /* the walk it was last met in                */
} CM_NI_DEV;

typedef struct _CM_NI_COUNTERS {
    ULONG Candidates;       /* admitted                                   */
    ULONG Attempts;         /* configurations created                     */
    ULONG Reprocesses;      /* reprocess-only attempts                    */
    ULONG Started;          /* DONE                                       */
    ULONG Unstarted;        /* FAILED after a reprocess (timeout)         */
    ULONG Refused[CM_NI_W_COUNT];   /* EXCLUDED and FAILED, by reason     */
    ULONG LeftOver;         /* refused.left-over                          */
    ULONG Leftovers;        /* disabled configurations left (uncommitted) */
    ULONG Gone;             /* journal entries marked gone                */
    ULONG Timeouts;
    ULONG Late;             /* results taken after their attempt ended    */
    ULONG Dropped;          /* results for no operation outstanding       */
    ULONG Pin0Seen;         /* NVMe functions with Interrupt Pin 0        */
    ULONG Pin0Started;      /* ... started                                */
    ULONG Unresolved;       /* descendants whose id named no location     */
    ULONG NoRoom;           /* descendants met with every entry in use    */
} CM_NI_COUNTERS;

/* XhciCmNoIrqDiag (record 28 section 7), twelve dwords: 0 sequence, 1 the
 * member, 2 the function, 3 its pin 7:0 and line 15:8 as read, 4 the
 * interrupt outcome used (CM_NI_IRQ_*) 7:0 with the entry's state
 * (CM_NI_S_*) 15:8 and its reason (CM_NI_W_*) 23:16 - the last attempt
 * or refusal -, 5 the problem before, 6 the step
 * reached (an CM_NI_OP_* kind, 31:16) and its CONFIGRET (15:0), 7 the
 * status after, 8 the problem after, 9 configurations created 15:0 and
 * candidates 31:16, 10 refusals packed four bits a reason (reasons 1 to
 * 8, each count held at 15), 11 pin-0 functions seen 15:0 and started
 * 31:16. */
#define CM_NI_DIAG_WORDS        12UL

typedef struct _CM_NI_MEMBER {
    ULONG PortBdf;
    ULONG Gen;              /* the publication generation now             */
    ULONG HaveGen;
    ULONG WindowOpen;
    ULONG WindowFrom;
    ULONG Pass;
    ULONG WalkSeq;          /* bumped by every walk (CmNiWalkBegin)       */
    ULONG NextId;
    CM_NI_OP Out;           /* the operation outstanding (Id 0: none)     */
    ULONG Cur;              /* the entry being stepped, or CM_NOIRQ_DEVICES */
    CM_NI_IRQ Irq;          /* the numbering's record ...                 */
    ULONG IrqGen;           /* ... for this generation                    */
    CM_NI_DEV Dev[CM_NOIRQ_DEVICES];
    CM_NI_JRN Jrn[CM_NOIRQ_DEVICES];
    CM_NI_COUNTERS C;
    ULONG Diag[CM_NI_DIAG_WORDS];
    ULONG DiagSeq;
    UCHAR Desc[CM_WK_NOIRQ_MEM * CM_NI_DESC_BYTES];
} CM_NI_MEMBER, *PCM_NI_MEMBER;

/* The member's state at its first use. */
VOID CmNiInit(PCM_NI_MEMBER m, ULONG portBdf);

/*
 * The result of a Reenumerate_DevNode of the member this driver asked for
 * came back (a tunnel up, a resync, a retry): its no-IRQ window opens for
 * CM_NOIRQ_WINDOW_MS. `gen` is the member's publication generation after
 * it; a new one sets every entry not ACTIVE or ATTEMPTED back to PENDING
 * (the journal kept, rule 1); those two run to their deadlines and are
 * read again once they end.
 */
VOID CmNiOpenWindow(PCM_NI_MEMBER m, ULONG gen, ULONG now);

/* The numbering's record for generation `gen` (its interrupt stage's
 * per-function outcomes; irq->Valid 0 when the stage did not run). */
VOID CmNiSetIrq(PCM_NI_MEMBER m, ULONG gen, const CM_NI_IRQ *irq);
/* ... built from a numbering's result: the stage's outcomes kept only for
 * a numbering that left its chain (DONE or REOPENED) with the stage run. */
VOID CmNiIrqFromBrnum(const XHCI_TBT_BRNUM *r, PCM_NI_IRQ irq);

/* A walk begins; one that ended normally (`complete`, every sibling chain
 * read to its end) forgets the entries it did not meet that have no
 * attempt in progress. */
VOID CmNiWalkBegin(PCM_NI_MEMBER m);
VOID CmNiWalkDone(PCM_NI_MEMBER m, ULONG complete);

/* Nonzero while an attempt is in progress (an entry ACTIVE or ATTEMPTED,
 * or an operation outstanding): this driver's own re-enumerations of the
 * member queue behind it (record 28 section 4, "The transaction"). */
ULONG CmNiInTransaction(const CM_NI_MEMBER *m);

/* Nonzero while the member wants passes: its window open, an entry
 * ACTIVE or ATTEMPTED, or an operation outstanding. */
ULONG CmNiBusy(const CM_NI_MEMBER *m);
/* Nonzero while the walk should run (the window open). */
ULONG CmNiWindow(const CM_NI_MEMBER *m);

/*
 * The walk met a descendant of the member: devnode `dn`, its instance
 * id's hash, its location and the swizzle of the bridges above it (the
 * sum of their device numbers). Inside the window only; a handle met
 * with another hash is a new device (the old entry and its journal
 * dropped); no room past CM_NOIRQ_DEVICES.
 */
VOID CmNiSeen(PCM_NI_MEMBER m, ULONG dn, ULONG hash, ULONG bdf, ULONG rot);
/* The walk met a descendant whose id named no location. */
VOID CmNiUnresolved(PCM_NI_MEMBER m);

/* A pass begins: the window's close (PENDING entries EXCLUDED, "not read
 * in time"), the deadlines (ACTIVE and ATTEMPTED entries past theirs
 * FAILED, "timeout"), every entry to be stepped once. */
VOID CmNiPass(PCM_NI_MEMBER m, const CM_NI_CTX *ctx);

/* The next operation of this pass: 1 with *op filled (its Id new), 0 when
 * the pass has nothing more to submit or one is outstanding. */
ULONG CmNiNext(PCM_NI_MEMBER m, const CM_NI_CTX *ctx, PCM_NI_OP op);

/* The result of the operation outstanding (res->Id its id); any other id
 * is dropped and counted. */
VOID CmNiTake(PCM_NI_MEMBER m, const CM_NI_CTX *ctx, const CM_NI_RES *res);

/* The pure readings the caller's operations need. */
/* DevLoader and PortDriver as read (found nonzero with its text, `n`
 * bytes at most): CM_NI_BIND_* (case not significant). */
ULONG CmNiBindFlags(ULONG dlFound, const char *dl, ULONG dlBytes,
                    ULONG pdFound, const char *pd, ULONG pdBytes);
/* The installer's Stor98 Version, a REG_SZ "a.b.c.d": CM_NI_STOR98_OK at
 * 0.4.0.0 or later, CM_NI_STOR98_OLD otherwise or unparsed. */
ULONG CmNiStor98Version(const char *text, ULONG bytes);
/* A fresh classification of a function's interrupt from reads alone (rule
 * 5): its pin and device, the swizzle of the bridges above it, and the
 * port's 3Ch: CM_NI_IRQ_PORT_NO_LINE, CM_NI_IRQ_UNROUTABLE or
 * CM_NI_IRQ_ROUTABLE. */
ULONG CmNiClassify(ULONG pin, ULONG dev, ULONG rot, ULONG portIntr);

/* The diagnostic's words now (Diag[0] the sequence, bumped by every
 * change). */
VOID CmNiDiag(const CM_NI_MEMBER *m, PULONG diag);

#endif /* CM_NOIRQ_H */
