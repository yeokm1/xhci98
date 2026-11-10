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
 * Reprocess_DevNode. Where PCI.VXD's configuration already holds the
 * memory alone and the problem is the 29 PCI.VXD set at the devnode's
 * creation (the memory-only form, record 28 section 12; read on 98 SE,
 * task 44.30), nothing is added and the devnode is reprocessed once a
 * generation. Stor98 (0.4.0.0 or later) then reads pin and a line
 * of 0 or FFh and polls. Since task 48.36 (record 28 section 14) an xHCI
 * controller bound to xhci98.sys, which polls with no interrupt resource,
 * takes the memory-only form too, and that form alone; and the window is
 * held while a bridge below the member, or a function's own driver, is
 * being installed (section 14.3).
 * This file holds everything of that which reads
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
#define CM_NOIRQ_HOLD_MS        1800000UL /* ... held while a driver is
                                           being installed, at most this
                                           long from its opening; and the
                                           held time credited, at most
                                           this much a window and an
                                           attempt (record 28 14.3, 14.6) */
#define CM_NOIRQ_TXN_MS         30000UL /* an attempt's own budget         */
#define CM_NOIRQ_DEVICES        8UL     /* devices a member                */
#define CM_NOIRQ_SKIPS          32UL    /* devnodes refused by their header,
                                           kept off the entries (record 28
                                           section 14.5)                  */
#define CM_NOIRQ_ATTEMPTS       3UL     /* configurations created a devnode,
                                           in the member's life           */
#define CM_WK_NOIRQ_MEM         9UL     /* memory descriptors copied       */
#define CM_NI_DESC_BYTES        256UL   /* one descriptor's room           */
#define CM_NI_LC_MAX            16UL    /* configurations read on one list */
#define CM_NI_READY_AFTER_MS    3000UL  /* ATTEMPTED with the problem still
                                           there this long: the one
                                           Setup_DevNode(READY) fallback  */
#define CM_NI_BLOCK_MS          2000UL  /* a call held the worker longer than
                                           this: credited (record 28 14.6) */

/* The function's kind, from its class code (record 28 section 14). */
#define CM_NI_KIND_NVME         0UL     /* 010802h, on Stor98              */
#define CM_NI_KIND_XHCI         1UL     /* 0C0330h, on xhci98.sys          */

/* Values of CONFIGMG.H (cm_cfgmg.h cites the lines). */
#define CM_NI_LCPRI_FORCECONFIG 0x0000UL
#define CM_NI_LCPRI_SUBOPTIMAL  0x5000UL
#define CM_NI_LCPRI_DISABLED    0xFFFFUL
#define CM_NI_BASIC_LOG_CONF    0x0UL
#define CM_NI_FORCED_LOG_CONF   0x4UL
#define CM_NI_RES_ALL           0UL
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
#define CM_NI_PROB_HW_DISABLED  0x1DUL  /* CM_PROB_HARDWARE_DISABLED       */

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
#define CM_NI_W_NOT_STOR98      4UL     /* not the kind's driver           */
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
                                           key) of DevLoader and, by
                                           FnKind, PortDriver (NVMe) or
                                           NTMPDriver (xHCI): Bind
                                           (CmNiBindFlagsKind)             */
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
                                           then with ResType_All, which
                                           must meet each of those and no
                                           other type                      */
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

/* CM_NI_RES.Bind (CmNiBindFlagsKind). For the xHCI kind the second value
 * is NTMPDriver and the names *NTKERN and xhci98.sys (record 28 14.2). */
#define CM_NI_BIND_DL           0x01UL  /* DevLoader read                  */
#define CM_NI_BIND_DL_IOS       0x02UL  /* ... and it is *IOS (NVMe) or
                                           *NTKERN (xHCI)                  */
#define CM_NI_BIND_PD           0x04UL  /* PortDriver (NTMPDriver) read    */
#define CM_NI_BIND_PD_STOR98    0x08UL  /* ... and it is stor98.mpd (NVMe)
                                           or xhci98.sys (xHCI)            */
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
    ULONG FnKind;           /* BINDING: CM_NI_KIND_* of the entry         */
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
    ULONG WalkHold;         /* the pass's walk met a bridge being
                               installed: its hold's log value
                               (CM_NI_HOLD_BRIDGE, record 28 14.3 item
                               1), 0 for none                            */
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
                               3 DMA, 4 every type                       */
    ULONG RdAllSeen;        /* descriptors the every-type read met        */
    ULONG RdAllIrq;         /* ... of them IRQ ones                       */
    ULONG LastStep;         /* the diagnostic's step reached              */
    ULONG LastCr;
    ULONG Pin0Gen;          /* a pin-0 function counted for gen + 1       */
    ULONG WalkSeq;          /* the walk it was last met in                */
    ULONG Credit;           /* ms of held time credited to this attempt   */
    ULONG Kind;             /* CM_NI_KIND_*, from the header (rule 3)     */
    ULONG WaitDrv;          /* PENDING, its last reading: its driver not
                               installed yet - an installation problem,
                               or the kind's pair not written (record 28
                               section 14.3 item 2)                      */
} CM_NI_DEV;

typedef struct _CM_NI_COUNTERS {
    ULONG Candidates;       /* admitted                                   */
    ULONG Attempts;         /* configurations created                     */
    ULONG Reprocesses;      /* reprocess-only attempts                    */
    ULONG MemOnly;          /* ... of them the memory-only form           */
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
    ULONG Held;             /* windows held (record 28 section 14.3)      */
    ULONG Credited;         /* calls whose held time was credited (14.6)  */
} CM_NI_COUNTERS;

/* XhciCmNoIrqDiag (record 28 section 7), twelve dwords: 0 sequence, 1 the
 * member, 2 the function, 3 its pin 7:0 and line 15:8 as read, 4 the
 * interrupt outcome used (CM_NI_IRQ_*) 7:0 with the entry's state
 * (CM_NI_S_*) 15:8, its reason (CM_NI_W_*) 23:16 and its kind
 * (CM_NI_KIND_*) 31:24 - the last attempt or refusal -, 5 the problem
 * before, 6 the step
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
    ULONG WindowFrom;       /* the window's opening (moved by a credit)   */
    ULONG HaveHold;         /* a pass of this window held it ...          */
    ULONG HoldAt;           /* ... the last such pass (record 28 14.3)    */
    ULONG HeldThis;         /* held since it opened (counted, logged)     */
    ULONG OutAt;            /* the clock when Out was handed out          */
    ULONG Credit;           /* ms of held time credited to the window     */
    ULONG LogHold;          /* a hold's line owed (CmNiLogNext) ...       */
    ULONG LogHoldV;         /* ... its value                              */
    ULONG LogCredit;        /* a credit's line owed ...                   */
    ULONG LogCreditV;       /* ... its value                              */
    ULONG HoldV;            /* the last hold's source (CM_NI_HOLD_*)      */
    ULONG LogClose;         /* the close's lines owed ...                 */
    ULONG LogCloseV;        /* ... its reason and times ...               */
    ULONG LogCloseHoldV;    /* ... and the last hold's source             */
    ULONG Pass;
    ULONG WalkSeq;          /* bumped by every walk (CmNiWalkBegin)       */
    ULONG NextId;
    CM_NI_OP Out;           /* the operation outstanding (Id 0: none)     */
    ULONG Cur;              /* the entry being stepped, or CM_NOIRQ_DEVICES */
    CM_NI_IRQ Irq;          /* the numbering's record ...                 */
    ULONG IrqGen;           /* ... for this generation                    */
    CM_NI_DEV Dev[CM_NOIRQ_DEVICES];
    CM_NI_JRN Jrn[CM_NOIRQ_DEVICES];
    ULONG SkipDn[CM_NOIRQ_SKIPS];       /* the refused list: handle (0 a
                                           free line) ...                */
    ULONG SkipHash[CM_NOIRQ_SKIPS];     /* ... and the id's hash         */
    ULONG SkipSeq[CM_NOIRQ_SKIPS];      /* ... and the walk last meeting it */
    ULONG SkipNext;                     /* the line written next         */
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
 * CM_NOIRQ_WINDOW_MS, held past that while a driver is being installed
 * (to CM_NOIRQ_HOLD_MS at most, record 28 section 14.3). `gen` is the
 * member's publication generation after
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
 * Record 28 section 14.3: nonzero for a devnode status and problem that
 * say its driver is still being installed - DN_HAS_PROBLEM with
 * CM_PROB_NOT_CONFIGURED (1), CM_PROB_DEVLOADER_NOT_FOUND (8),
 * CM_PROB_REINSTALL (18) or CM_PROB_FAILED_INSTALL (28). A pass whose walk
 * met a bridge being installed (CM_NI_CTX.WalkHold), or with an entry
 * PENDING for its driver (CM_NI_DEV.WaitDrv), holds the window: it closes
 * CM_NOIRQ_WINDOW_MS after the last such pass, never later than
 * CM_NOIRQ_HOLD_MS after its opening.
 */
ULONG CmNiInstalling(ULONG status, ULONG problem);
/* ... and the walk's whole reading of a descendant: its status read
 * (`statusCr`, `status`, `problem`) saying so, or its hardware key's
 * Driver value read as absent (`driverCr` CR_NO_SUCH_VALUE: Windows has
 * installed no driver for it yet - a new devnode before its wizard, whose
 * status shows no problem yet). A read that failed otherwise says
 * nothing. */
#define CM_NI_CR_NO_SUCH_VALUE  0x25UL
ULONG CmNiWalkInstalling(ULONG statusCr, ULONG status, ULONG problem,
                         ULONG driverCr);
/* Nonzero for a function's dword at 08h (class code 31:8) that names a
 * PCI-to-PCI bridge (class 06h, subclass 04h): the walk reads the
 * installation of these alone (record 28 section 14.3 item 1). */
ULONG CmNiIsBridge(ULONG classRev);

/* A device answering on secondary bus `bus` (`present`, function 0's
 * answer a bit per device) at which the walk placed none of its `metN`
 * devnodes (`met`, their locations, bus 15:8 and devfn 7:0): Windows has
 * not enumerated it yet, so the window holds (record 28 section 14.3
 * item 1, 98 SE's one-at-a-time enumeration). 0 for bus 0, or when the
 * walk met more than `metMax` and kept them not whole. */
ULONG CmNiUnenumerated(ULONG bus, ULONG present, const ULONG *met,
                       ULONG metN, ULONG metMax);

/* A function whose devnode the window waits for when it answers with none
 * (CmNiUnenumerated's `present`): a PCI-to-PCI bridge, which brings more
 * devnodes, or a class this record starts (010802h, 0C0330h); not another
 * class (a network function with no driver for the system), nor a read
 * that failed (all ones). */
ULONG CmNiAwaited(ULONG classRev);

/* The hold's log value (cm.reenum.9x.noirq.hold): its source in 31:24 -
 * a bridge the walk met (its depth below the member 15:8, its problem 7:0,
 * FFh for no Driver value or FEh for a function behind it with no child
 * devnode yet), or an entry waiting for its driver (its location 15:0) -
 * record 28 section 14.4. */
#define CM_NI_HOLD_BRIDGE       0x01000000UL
#define CM_NI_HOLD_ENTRY        0x02000000UL
#define CM_NI_HOLD_NO_DRIVER    0xFFUL
#define CM_NI_HOLD_NO_CHILD     0xFEUL

/* A call outside the operations (the walk) held the worker `held` ms:
 * credited to the window and the attempts as an operation's is (record
 * 28 section 14.6), when above CM_NI_BLOCK_MS. */
VOID CmNiCredit(PCM_NI_MEMBER m, ULONG held, ULONG now);

/* A line the caller owes the log: 1 with *what (CM_NI_LOG_*) and *value,
 * each line once; 0 when none is owed. Read after CmNiPass, CmNiTake and
 * CmNiCredit (record 28 section 14.4). */
#define CM_NI_LOG_HOLD          1UL     /* cm.reenum.9x.noirq.hold         */
#define CM_NI_LOG_CREDIT        2UL     /* cm.reenum.9x.noirq.credit: the
                                           call's held ms                  */
#define CM_NI_LOG_CLOSE         3UL     /* cm.reenum.9x.noirq.close: why
                                           31:24 (CM_NI_CLOSE_*), seconds
                                           from the opening 23:12 and from
                                           the last hold (or the opening)
                                           11:0, each held at FFFh         */
#define CM_NI_LOG_CLOSE_HOLD    4UL     /* cm.reenum.9x.noirq.close.hold:
                                           the last hold's source
                                           (CM_NI_HOLD_*), 0 for none      */
#define CM_NI_CLOSE_UNHELD      1UL     /* 10 s from the opening, no hold  */
#define CM_NI_CLOSE_AFTER_HOLD  2UL     /* 10 s from the last hold         */
#define CM_NI_CLOSE_BOUND       3UL     /* CM_NOIRQ_HOLD_MS reached        */
ULONG CmNiLogNext(PCM_NI_MEMBER m, PULONG what, PULONG value);

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
/* ... for a kind (CM_NI_KIND_*): the xHCI kind's DevLoader and NTMPDriver
 * against *NTKERN and xhci98.sys. */
ULONG CmNiBindFlagsKind(ULONG kind, ULONG dlFound, const char *dl,
                        ULONG dlBytes, ULONG pdFound, const char *pd,
                        ULONG pdBytes);
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
