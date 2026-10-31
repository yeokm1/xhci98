/*
 * cm_breq.h - the pure half of the requirement lists of hot-added bridges
 * on Windows 2000 and XP (design record 33; roadmap-usb4.md task 45.12):
 * the shape test, the claims, the rules of record 33 section 6 and the
 * writer of the new list, and the diagnostic's layout (section 9.2), kept
 * apart so the host suite (test\test_cm_breq.c) reaches them.
 *
 * The child filter (cm_filt.c) calls it on IRP_MN_FILTER_RESOURCE_
 * REQUIREMENTS for a PCI-to-PCI bridge below an active tunnel port, on NT
 * 5.x alone, with the list the PCI driver answered and a snapshot of the
 * bridge's surroundings read from configuration space (cm_reenum.c,
 * CmRegBreqSnap). The lists are read and written as bytes at the offsets
 * of cm_irq.h, which cm_filt.c asserts against the DDK's structures for
 * x86 and amd64.
 *
 * The arithmetic is 32-bit (record 33 section 6): an address is a pair of
 * ULONGs, high and low; no 64-bit type or operation appears here.
 *
 * C89, DDK-free, no allocation, no lock. IRQL: any.
 */

#ifndef CM_BREQ_H
#define CM_BREQ_H

#include "../xhci_compat.h"

/* The kinds of window, in the order of the lists' groups. */
#define CM_BREQ_K_IO            0UL
#define CM_BREQ_K_MEM           1UL
#define CM_BREQ_K_PREF          2UL
#define CM_BREQ_KINDS           3UL

#define CM_BREQ_G_IO            0x1000UL        /* the granules, g(k)     */
#define CM_BREQ_G_MEM           0x100000UL
/* The largest multiple of 1 MB a ULONG Length holds (section 6). */
#define CM_BREQ_CAP             0xFFF00000UL
/* The descriptors of one group the rewriter writes at most. */
#define CM_BREQ_STEPS_MAX       16UL
/* Siblings a downstream port's snapshot holds. */
#define CM_BREQ_SIBS_MAX        16UL

/* The bridge's role, from its PCI Express Device/Port Type (section 5.2). */
#define CM_BREQ_ROLE_UP         1UL     /* 0101b, an upstream switch port */
#define CM_BREQ_ROLE_DOWN       2UL     /* 0110b, a downstream one        */
#define CM_BREQ_ROLE_OTHER      3UL     /* anything else, or unread       */

/* The templates (section 6.1). */
#define CM_BREQ_SHAPE_NONE      0UL
#define CM_BREQ_SHAPE_XP        1UL     /* XP closed, minor version 1     */
#define CM_BREQ_SHAPE_2000      2UL     /* 2000 closed, minor version 0   */

/* The outcomes (section 9.2, dword 6 bits 15:12). */
#define CM_BREQ_OUT_LEFT        0UL     /* the rules change nothing       */
#define CM_BREQ_OUT_REWRITTEN   1UL
#define CM_BREQ_OUT_OFF         2UL     /* XhciCmBridgeReq 0              */
#define CM_BREQ_OUT_RECORD      3UL     /* XhciCmBridgeReq 2              */
#define CM_BREQ_OUT_SHAPE       4UL     /* no template matched            */
#define CM_BREQ_OUT_PARENT      5UL     /* the parent's windows unread    */
#define CM_BREQ_OUT_NOTALONE    6UL
#define CM_BREQ_OUT_NOMEM       7UL     /* the new list not allocated     */
#define CM_BREQ_OUT_LOWER       8UL     /* the lower driver's status      */
#define CM_BREQ_OUT_NOWIN       9UL     /* the rules left no window       */

/* A claim (section 5.2's table), two bits a kind in the diagnostic. */
#define CM_BREQ_C_NO            0UL
#define CM_BREQ_C_YES           1UL
#define CM_BREQ_C_UNKNOWN       2UL
#define CM_BREQ_C_GRANULE       3UL     /* memory only: exactly 1 MB      */

/* What a sibling's secondary bus holds. */
#define CM_BREQ_B_NONE          0UL     /* no function answers            */
#define CM_BREQ_B_TYPE0         1UL     /* type 0 functions, their BARs read */
#define CM_BREQ_B_TYPE1         2UL     /* a bridge: a nested switch      */
#define CM_BREQ_B_UNREAD        3UL     /* not numbered, or a read failed */

/* N unknown (an upstream port whose secondary bus is not readable). */
#define CM_BREQ_N_UNKNOWN       0xFFUL

/* A three-way reading. */
#define CM_BREQ_NO              0UL
#define CM_BREQ_YES             1UL
#define CM_BREQ_UNKNOWN         2UL

/* One function on the upstream port's secondary bus (a downstream port's
 * sibling, itself included), in device and function order. */
typedef struct _CM_BREQ_SIB {
    ULONG Dsp;          /* it answers as a downstream switch port        */
    ULONG Numbered;     /* its secondary bus numbered                    */
    ULONG HotPlug;      /* Slot Implemented and Hot-Plug Capable         */
    ULONG Behind;       /* CM_BREQ_B_*                                   */
    ULONG AnyIo;        /* B_TYPE0: some I/O BAR                         */
    ULONG AnyPref;      /* B_TYPE0: some prefetchable memory BAR         */
} CM_BREQ_SIB, *PCM_BREQ_SIB;

/* The snapshot (section 5.2): what the filter read at the IRP. */
typedef struct _CM_BREQ_SNAP {
    ULONG Role;         /* CM_BREQ_ROLE_*                                */
    ULONG ParentRead;   /* the six parent registers below were read      */
    /* The parent's 1Ch, 20h, 24h, 28h, 2Ch and 30h, as read. */
    ULONG Par[6];
    ULONG Alone;        /* CM_BREQ_NO, _YES or _UNKNOWN                  */
    /* An upstream port: its secondary bus numbered, and N (the downstream
     * ports on it), CM_BREQ_N_UNKNOWN where unread. */
    ULONG SecNumbered;
    ULONG N;
    /* A downstream port: the functions on its own bus, read whole
     * (SibsRead), and its index among them. */
    ULONG SibsRead;
    ULONG Sibs;
    ULONG Index;
    CM_BREQ_SIB Sib[CM_BREQ_SIBS_MAX];
} CM_BREQ_SNAP, *PCM_BREQ_SNAP;

/* The rules' decision for one bridge. */
typedef struct _CM_BREQ_PLAN {
    ULONG Outcome;      /* CM_BREQ_OUT_*: LEFT, REWRITTEN, SHAPE, PARENT,
                           NOTALONE or NOWIN                             */
    ULONG Shape;
    ULONG Role;
    ULONG Dropped;      /* bit k: the group of kind k dropped            */
    ULONG Unmet;        /* bit k: a "yes" claim of kind k not met        */
    ULONG Claims;       /* this port's, two bits a kind (I/O 1:0)        */
    ULONG Flags;        /* the diagnostic's dword 7 bits 19:16 >> 16      */
    ULONG Count;        /* N, or the siblings                            */
    ULONG Index;
    ULONG P[CM_BREQ_KINDS];         /* the parent's windows, capped      */
    ULONG OsMin[CM_BREQ_KINDS];     /* the OS list's smallest lengths    */
    ULONG OsPref[CM_BREQ_KINDS];    /* the OS list's first lengths       */
    ULONG Keep[CM_BREQ_KINDS];      /* the group is in the new list      */
    ULONG Sized[CM_BREQ_KINDS];     /* its sizes are the rules'          */
    ULONG Steps[CM_BREQ_KINDS];
    ULONG Len[CM_BREQ_KINDS][CM_BREQ_STEPS_MAX];
    ULONG Bytes;        /* the new list's size, Outcome REWRITTEN        */
    ULONG Tail;         /* 1: the OS list ends with the bridge's interrupt,
                           kept as the new list's last descriptor        */
} CM_BREQ_PLAN, *PCM_BREQ_PLAN;

/*
 * The shape test (section 6.1): CM_BREQ_SHAPE_XP or _2000 when `list` (an
 * IO_RESOURCE_REQUIREMENTS_LIST, NULL allowed) matches that template field
 * by field, alone or followed by exactly one interrupt descriptor of the
 * form section 6.1 gives (a bridge whose Interrupt Pin is set), and
 * `minor` is its system's NT minor version (1 XP, 0 2000);
 * CM_BREQ_SHAPE_NONE for anything else, XP x64's lists (minor 2) included.
 * The header's interface type, bus and slot are not compared.
 */
ULONG CmBreqShape(const UCHAR *list, ULONG minor);

/*
 * For a list CmBreqShape matched: 1 when it ends with the interrupt
 * descriptor, 0 when it is the template alone (or `list` is NULL).
 */
ULONG CmBreqTail(const UCHAR *list);

/*
 * The parent's windows (section 6's arithmetic) from its 1Ch, 20h, 24h,
 * 28h, 2Ch and 30h: P[k] in bytes, 0 for a window that is not open, the
 * memory and prefetchable ones capped at CM_BREQ_CAP. FALSE when the I/O
 * window is open and not wholly below 10000h (the parent unread).
 */
BOOLEAN CmBreqWindows(const ULONG *par, PULONG p);

/*
 * One type 0 function's BARs (10h to 24h, `bars[6]`, as read): TRUE with
 * *io and *pref (some I/O BAR, some prefetchable memory BAR) when every
 * BAR's type reads; a 64-bit BAR's upper dword is skipped. FALSE (the
 * function's claims unknown) for a reserved width 11b or a 64-bit BAR in
 * the last slot.
 */
BOOLEAN CmBreqBars(const ULONG *bars, PULONG io, PULONG pref);

/*
 * The claims of sibling `sib` for each kind (section 5.2's table), into
 * c[3]; `prefWin` whether the upstream port has a prefetchable window
 * (a prefetchable claim counts as memory where it has none).
 */
VOID CmBreqClaims(const CM_BREQ_SIB *sib, ULONG prefWin, PULONG c);

/*
 * The rules (sections 6.1 to 6.4) for the bridge `s` describes, whose
 * list the PCI driver answered as `list`, on NT minor version `minor`.
 * Returns p->Outcome; p is filled whatever it is.
 */
ULONG CmBreqPlan(const UCHAR *list, ULONG minor, const CM_BREQ_SNAP *s,
                 PCM_BREQ_PLAN p);

/*
 * The new list (section 6.1, what the rewriter keeps, drops and replaces)
 * for a plan whose outcome is CM_BREQ_OUT_REWRITTEN, from the same `list`,
 * into `out` of `room` bytes. Returns p->Bytes, or 0 (nothing written)
 * when the plan is another outcome, the list is not its shape, or `room`
 * is less than p->Bytes.
 */
ULONG CmBreqWrite(const UCHAR *list, const CM_BREQ_PLAN *p, UCHAR *out,
                  ULONG room);

/*
 * XhciCmBridgeReqDiag (section 9.2): 68 dwords, a header of 4 and a ring
 * of the last 8 attempts of 8 dwords each.
 */
#define CM_BQDIAG_WORDS         68UL
#define CM_BQDIAG_ENTRIES       8UL
#define CM_BQDIAG_ENTRY_WORDS   8UL
#define CM_BQDIAG_HDR           4UL

/* The start's state in an entry (dword 6 bits 23:22). */
#define CM_BREQ_START_NONE      0UL
#define CM_BREQ_START_OK        1UL
#define CM_BREQ_START_FAILED    2UL

/*
 * One attempt's entry dwords 1 to 7 (the sequence is the ring's) into
 * e[7]: `bdf` and `portBdf` the bridge's and its tunnel port's locations
 * (bus 15:8, device and function 7:0), `minor` the system, `outcome` the
 * attempt's (the plan's, or OFF, RECORD, NOMEM or LOWER), `times` the
 * times this bridge was rewritten since the start.
 */
VOID CmBreqEntry(const CM_BREQ_PLAN *p, ULONG bdf, ULONG portBdf,
                 ULONG minor, ULONG outcome, ULONG times, PULONG e);

/*
 * The ring: entry e[7] added as the newest with sequence `seq` (not 0),
 * dword 0 set to it, the header's counts updated by its outcome (dword 1
 * rewrites 15:0 and lists left by rule 31:16; dword 3 allocation failures
 * 15:0), each saturating at FFFFh.
 */
VOID CmBreqDiagAdd(PULONG d, ULONG seq, const ULONG *e);

/* A start after a rewrite: the newest entry of `bdf` whose outcome is
 * REWRITTEN and whose start is not yet seen takes it, and dword 2's
 * count; TRUE when one did. */
BOOLEAN CmBreqDiagStart(PULONG d, ULONG bdf, BOOLEAN ok);

/* A child with no active member above it (dword 3 bits 31:16). */
VOID CmBreqDiagNoBridge(PULONG d);

#endif /* CM_BREQ_H */
