/*
 * xhci_msg4.h - XhciInterruptMode 4's pure half (design record 15 section
 * 4.5; roadmap-usb4 task 49.24): the mode-4 decision and its values, the
 * walk of one CM_RESOURCE_LIST from the PnP manager's resource map that
 * finds another device's interrupt on this controller's vector (the veto
 * of strategy 1), the image's refusal table, the stray-entry brake of
 * strategy 1, the stranded-event judge, and the packing of a refused
 * claim's reasons.
 *
 * The kernel halves are hcd_msg4.c (the lifetime) and hcd_msgnt.c
 * (msi4nt, 2000 to XP x64).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_MSG4_H
#define XHCI_MSG4_H

#include "xhci_compat.h"

/* ------------------------------------------------------------------ */
/* The decision (section 4.5.2)                                        */
/* ------------------------------------------------------------------ */

/* The system, in the three classes mode 4 tells apart. */
#define XHCI_MSG4_SYS_9X                0UL     /* 98 SE, ME: none, 4 as 0 */
#define XHCI_MSG4_SYS_NT5               1UL     /* 2000, XP, XP x64: msi4nt */
#define XHCI_MSG4_SYS_NT6               2UL     /* Vista and later: none   */

/* XhciMsiStrategy: where the vector comes from on NT 5. No value ever
 * turns into the other (owner, 2026-11-10). */
#define XHCI_MSG4_STRATEGY_CLAIM        0UL     /* a free ISA IRQ claimed  */
#define XHCI_MSG4_STRATEGY_BORROW       1UL     /* the controller's line   */

/* XhciMsiPools (owner, 2026-11-11): which pools a claim may take its
 * interrupt from, under strategy 0, on 32-bit 2000 and XP only. */
#define XHCI_MSG4_POOLS_ISA             0UL     /* the ISA IRQs, default   */
#define XHCI_MSG4_POOLS_HIGH            1UL     /* I/O APIC inputs above 23 */
#define XHCI_MSG4_POOLS_BOTH            2UL     /* both, the ISA IRQs first */

/* The upper inputs' range (msi4nt api.md, "What a client can set"). */
#define XHCI_MSG4_HIGH_FIRST            24UL
#define XHCI_MSG4_HIGH_LAST             255UL
#define XHCI_MSG4_BUDGET_MAX            32UL

/* XhciMsiShare (the owner, 2026-11-13): the most controllers on one
 * claimed IRQ - 1 no sharing, 2 to 16 that many; absent is 8, sharing on
 * by default. */
#define XHCI_MSG4_SHARE_MIN             2UL
#define XHCI_MSG4_SHARE_MAX             16UL
#define XHCI_MSG4_SHARE_DEFAULT         8UL

/* A value not found, as xhci_poll.h's XHCI_POLL_NOT_FOUND. */
#define XHCI_MSG4_NOT_FOUND             0xFFFFFFFFUL

/*
 * The decision's inputs. Each value as read, XHCI_MSG4_NOT_FOUND when
 * absent (an absent value is its default, 0). `PoolsOffered` is 0 where
 * the upper pool is not offered (the amd64 image: XP x64, owner
 * 2026-11-11), so the pool values are taken as their defaults there;
 * `Quarantined` that the image's refusal table holds this function (or
 * its latch is set).
 */
typedef struct _XHCI_MSG4_IN {
    ULONG System;           /* XHCI_MSG4_SYS_*                              */
    ULONG Quarantined;
    ULONG Avoided;          /* XhciMsiAvoid applies                         */
    ULONG Strategy;
    ULONG IsaIrq;
    ULONG PoolsOffered;
    ULONG Pools;
    ULONG InputFirst;
    ULONG InputLast;
    ULONG Budget;
    ULONG HaveInterrupt;    /* an interrupt resource was given              */
    ULONG Message;          /* and it is a message one                      */
    ULONG PinStatus;        /* XHCI_POLL_PIN_*                              */
    ULONG Pin;
} XHCI_MSG4_IN, *PXHCI_MSG4_IN;

/* What a claim would be asked for, from the same values once valid. */
typedef struct _XHCI_MSG4_CLAIM {
    ULONG Strategy;         /* 0 or 1                                       */
    ULONG Irq;              /* forced IRQ or input, 0 the library's choice  */
    ULONG Pools;            /* XHCI_MSG4_POOLS_*                            */
    ULONG LibPools;         /* the library's Pools: 1 ISA, 2 HIGH, 3 both   */
    ULONG HighFirst;        /* 0 and 0: the library's 24..255               */
    ULONG HighLast;
    ULONG Budget;           /* 0: the library's 16                          */
} XHCI_MSG4_CLAIM, *PXHCI_MSG4_CLAIM;

/*
 * The mode-4 decision (section 4.5.2's first table), the first row that
 * applies: any system but NT 5 22; the refusal table 27; XhciMsiAvoid 24; on
 * NT 5 a strategy, pool, range, budget or forced IRQ outside its set 30;
 * strategy 1 with no line to borrow (no interrupt resource, a message
 * resource, or the pin read 0) 23; otherwise XHCI_POLL_DELIVERY_MESSAGE
 * (5) with reason 21. NONE is 0. `claim` (may be NULL) receives the claim's settings when the
 * answer is a message on NT 5.
 */
ULONG XhciMsg4Decide(const XHCI_MSG4_IN *in, PXHCI_MSG4_CLAIM claim,
                     PULONG reason);

/* XhciMsiShare as it runs, 0 for no sharing: absent 8; 1 no sharing, and
 * 0, which is not offered, as 1; 2 to 16 as set; above 16 as 16. */
ULONG XhciMsg4ShareValue(ULONG read);

/* The member cap a claim passes: the share value, 0 when sharing is off
 * (the library's own default then; 1 it refuses, and no claim is joined
 * without MSGIRQ_ISA_FLAG_SHARE). */
ULONG XhciMsg4MaxMembers(ULONG share);

/* ------------------------------------------------------------------ */
/* The veto's resource-map walk (section 4.5.3, strategy 1)            */
/* ------------------------------------------------------------------ */

/* The layout the walk reads, the DDK's (hcd_msgnt.c asserts each against
 * the DDK it is built with): CM_RESOURCE_LIST's Count, then its full
 * descriptors from offset 4; a full descriptor's InterfaceType,
 * BusNumber, Version, Revision and Count, its partial descriptors from
 * offset 16; a partial descriptor's Type at 0, its interrupt Vector at 8,
 * a device-specific descriptor's DataSize at 4 with that many bytes after
 * the descriptor. A partial descriptor is 16 bytes on x86 and 20 on amd64,
 * the caller's to say: the registry holds the writing kernel's layout. */
#define XHCI_MSG4_RL_HEADER             4UL
#define XHCI_MSG4_FULL_HEADER           16UL
#define XHCI_MSG4_FULL_COUNT            12UL
#define XHCI_MSG4_PD_TYPE               0UL
#define XHCI_MSG4_PD_VECTOR             8UL
#define XHCI_MSG4_PD_DATASIZE           4UL
#define XHCI_MSG4_TYPE_INTERRUPT        2UL     /* CmResourceTypeInterrupt  */
#define XHCI_MSG4_TYPE_DEVSPECIFIC      5UL     /* ...DeviceSpecific        */
/* Bounds no PnP list comes near: a list beyond them is malformed. */
#define XHCI_MSG4_MAX_FULL              64UL
#define XHCI_MSG4_MAX_PARTIAL           256UL

/* XhciMsg4ShareScan's answers. */
#define XHCI_MSG4_SHARE_NONE            0UL     /* no interrupt on it       */
#define XHCI_MSG4_SHARE_FOUND           1UL     /* an interrupt on it       */
#define XHCI_MSG4_SHARE_MALFORMED       2UL     /* the walk could not finish */

/*
 * One list, `length` bytes of it, its partial descriptors `pdSize` bytes
 * each (16 or 20; anything else is malformed): FOUND when an interrupt
 * descriptor's Vector is `vector`, else NONE, or MALFORMED when a count
 * or a size would run past `length` or past the bounds above. Interrupt
 * descriptors seen are added to *interrupts. Reads only. IRQL: any.
 */
ULONG XhciMsg4ShareScan(const UCHAR *data, ULONG length, ULONG pdSize,
                        ULONG vector, PULONG interrupts);

/*
 * Whether `name` (`chars` WCHARs, no terminator needed) is `own` followed
 * by ".Translated": the controller's own entry in the map. And whether it
 * ends in ".Translated" at all. IRQL: any.
 */
ULONG XhciMsg4NameIsOwn(const USHORT *name, ULONG chars, const USHORT *own,
                        ULONG ownChars);
ULONG XhciMsg4NameIsTranslated(const USHORT *name, ULONG chars);

/* ------------------------------------------------------------------ */
/* The refusal table (section 4.5.4)                                   */
/* ------------------------------------------------------------------ */

/*
 * Functions whose message a teardown could not prove off this boot, or
 * whose claim was kept, by bus 15:8, device 7:3, function 2:0, under the
 * caller's lock. A full table, or a function whose location could not be
 * read, sets the latch, which refuses every start on these systems for
 * the boot. The table outlives every device object: the image is kept
 * with it.
 */
#define XHCI_MSG4_REFUSAL_SLOTS         8UL

typedef struct _XHCI_MSG4_REFUSALS {
    ULONG Count;
    ULONG Latched;
    ULONG Bdf[XHCI_MSG4_REFUSAL_SLOTS];
} XHCI_MSG4_REFUSALS, *PXHCI_MSG4_REFUSALS;

ULONG XhciMsg4Bdf(ULONG bus, ULONG device, ULONG function);
/* 1 when `bdf` is refused: in the table, or the latch set. `known` 0 says
 * the caller could not read the location, which only the latch answers. */
ULONG XhciMsg4Refused(const XHCI_MSG4_REFUSALS *t, ULONG known, ULONG bdf);
/* Enters `bdf` (`known` 0: the location never read, the latch set); the
 * entry that fills the table sets the latch. Returns 0 when the table was
 * full already and nothing was entered. */
ULONG XhciMsg4RefusalAdd(PXHCI_MSG4_REFUSALS t, ULONG known, ULONG bdf);
/* Functions in the table, the latch counted as one more. */
ULONG XhciMsg4RefusalCount(const XHCI_MSG4_REFUSALS *t);

/* ------------------------------------------------------------------ */
/* The brake of strategy 1 (section 4.5.6)                             */
/* ------------------------------------------------------------------ */

/*
 * Under a message IMAN.IP clears itself when the message write completes
 * (xHCI 1.2c 4.17.5), so an entry is judged by the event ring: one the
 * ISR did not claim (USBSTS.EINT clear) and that finds no event at the
 * ring's dequeue position found nothing of this controller's - an
 * interrupt on the borrowed line from elsewhere. `*run` counts such
 * entries in a row and any other entry ends the run; 1 when this entry
 * makes the run XHCI_MSG4_STRAY_LIMIT, once. Under the interrupt's own
 * lock (the ISR). IRQL: any.
 */
#define XHCI_MSG4_STRAY_LIMIT           256UL

ULONG XhciMsg4StrayStep(PULONG run, ULONG claimed, ULONG pending);

/* ------------------------------------------------------------------ */
/* The stranded-event judge (section 4.5.6)                            */
/* ------------------------------------------------------------------ */

/*
 * Eight stranded events within 60 s fail the controller (owner,
 * 2026-11-11, decision 11); constants, not values. The window is in the
 * tolerance clock's ticks (XHCI_TOL_TICK_MS, 100 ms).
 */
#define XHCI_MSG4_STRANDED_LIMIT        8UL
#define XHCI_MSG4_STRANDED_WINDOW_TICKS 600UL

/* The judge's verdicts. */
#define XHCI_MSG4_JUDGE_OK              0UL
#define XHCI_MSG4_JUDGE_TRIP            1UL

typedef struct _XHCI_MSG4_JUDGE {
    ULONG Stranded;         /* stranded events this lifetime (Msg4Stranded) */
    ULONG Held;             /* stamps held, 0..XHCI_MSG4_STRANDED_LIMIT     */
    ULONG Stamp[XHCI_MSG4_STRANDED_LIMIT];  /* oldest first                 */
    ULONG Run;              /* unclaimed drains with events, in a row       */
    ULONG Claims;           /* the routine's claimed entries at the last look */
    ULONG DeadlineValid;
    ULONG Deadline;         /* fixed when the run reaches the limit         */
    ULONG Tripped;          /* latched once                                 */
} XHCI_MSG4_JUDGE, *PXHCI_MSG4_JUDGE;

/* A fresh count and window: each start, each in-place recovery, each
 * successful re-arm at D0, each reconnect at a cancel. `claims` the
 * routine's claimed entries now. */
VOID XhciMsg4JudgeReset(PXHCI_MSG4_JUDGE j, ULONG claims);

/* A backstop drain under a connected mode 4 with the flag set, at
 * tolerance tick `now`: counted; TRIP when it is the eighth within the
 * window, whatever was claimed between them. Once tripped, OK. */
ULONG XhciMsg4JudgeStranded(PXHCI_MSG4_JUDGE j, ULONG now);

/* A drain that took `events` events (0 judges nothing) with the routine's
 * claimed entries at `claims`, at tick `now`: a moved count ends the run
 * and its deadline; else the run grows, and the drain that brings it to
 * XHCI_MSG4_STRANDED_LIMIT fixes the deadline one tick ahead, which later
 * drains do not move. */
VOID XhciMsg4JudgeDrain(PXHCI_MSG4_JUDGE j, ULONG events, ULONG claims,
                        ULONG now);

/* Each tolerance tick: a moved claim count ends the run and its deadline;
 * TRIP when the deadline is reached with the run unbroken. Once tripped,
 * OK. */
ULONG XhciMsg4JudgeTick(PXHCI_MSG4_JUDGE j, ULONG claims, ULONG now);

/* ------------------------------------------------------------------ */
/* A refused claim's reasons (section 4.5.9)                           */
/* ------------------------------------------------------------------ */

/* The picker's reason for each IRQ 0 to 15, a byte each, into four
 * words: IRQ 4n in bits 7:0 of word n. */
VOID XhciMsg4PackReasons(const UCHAR *reasons16, PULONG words4);

#endif /* XHCI_MSG4_H */
