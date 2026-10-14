/*
 * xhci_quirk.h - Linux's xHCI controller quirks, their pure half (roadmap-hcd
 * task 38.10, design record 20 sections 4 and 6).
 *
 * Fourteen registry values, one per quirk taken, each a REG_DWORD in the
 * controller's driver key read at every start (record 20 section 6.1): 0
 * off, 1 the controllers Linux names, 2 every controller where the fix is
 * generic xHCI behaviour. On the three vendor-register quirks a 2 is read as
 * 1; absent, another type or any other number is the default (1, or 0 for
 * XhciShutdownReset). Each quirk's list is matched against the PCI vendor
 * and device dword alone (section 6.2): no revision is matched (section
 * 6.3). The fifteenth id, 1002:7316, joins XhciIntervalCap's list in
 * xhci_tol.c (section 4.10) and has no value of its own.
 *
 * Every id and every Linux flag here was read in Linux's
 * drivers/usb/host/xhci-pci.c at the commit external/README.md pins; the
 * code is this driver's own, written from the record, and copies none of
 * Linux's (record 20 section 6.6).
 *
 * What each quirk does is done where it acts - the start, the D3 path, the
 * rings, the link machine, the stream layout - and each says so beside its
 * site. This file holds the values, the match, the state a start fills and
 * the packed word the counter block and the log carry (section 6.4).
 *
 * DDK-free: part of the pure core. C89. IRQL: any; nothing here takes a lock
 * or touches a register.
 */

#ifndef XHCI_QUIRK_H
#define XHCI_QUIRK_H

#include "xhci_compat.h"

/* The quirks, in record 20 section 6.1's order: the index of every table
 * here and of the counter block's packed words (xhci_counters.h). */
#define XHCI_QK_MSI_AVOID           0UL     /* XhciMsiAvoid                 */
#define XHCI_QK_STREAMS_OFF         1UL     /* XhciStreamsOff               */
#define XHCI_QK_RESUME_REINIT       2UL     /* XhciResumeReinit             */
#define XHCI_QK_SHUT_TO_EHCI        3UL     /* XhciIntelShutdownToEhci      */
#define XHCI_QK_SHUT_RESET          4UL     /* XhciShutdownReset            */
#define XHCI_QK_HALT_DELAY          5UL     /* XhciSuspendHaltDelay         */
#define XHCI_QK_HALT_TIMEOUT        6UL     /* XhciSuspendHaltTimeout       */
#define XHCI_QK_ASM_FLOW            7UL     /* XhciAsmediaFlowControl       */
#define XHCI_QK_TI_CAP              8UL     /* XhciTiIntervalCap            */
#define XHCI_QK_STUCK_LINK          9UL     /* XhciStuckLinkReset           */
#define XHCI_QK_RING_PAD            10UL    /* XhciStreamRingPad            */
#define XHCI_QK_LINK_NOOP           11UL    /* XhciControlLinkNoOp          */
#define XHCI_QK_ISOCH_SUCCESS       12UL    /* XhciIsochSpuriousSuccess     */
#define XHCI_QK_AMD_BIT             13UL    /* XhciAmdSuspendBitClear       */
#define XHCI_QK_COUNT               14UL

/* How a value was found, the read status apart from the value. */
#define XHCI_QK_FOUND_NONE          0UL     /* absent, or no driver key     */
#define XHCI_QK_FOUND_DWORD         1UL
#define XHCI_QK_FOUND_OTHER         2UL     /* present, another type        */

/* What the value read became (the dump's "normalization"). */
#define XHCI_QK_NORM_READ           0UL     /* taken as read                */
#define XHCI_QK_NORM_ABSENT         1UL     /* the default                  */
#define XHCI_QK_NORM_TYPE           2UL     /* another type: the default    */
#define XHCI_QK_NORM_NUMBER         3UL     /* another number: the default  */
#define XHCI_QK_NORM_TWO_AS_ONE     4UL     /* a 2 on a vendor quirk: 1     */

/* The modes. */
#define XHCI_QK_MODE_OFF            0UL
#define XHCI_QK_MODE_LISTED         1UL
#define XHCI_QK_MODE_EVERY          2UL

/* A quirk's last action (outcome), and why one was skipped or failed. */
#define XHCI_QK_OUT_NONE            0UL     /* not acted on yet, or no act  */
#define XHCI_QK_OUT_DONE            1UL
#define XHCI_QK_OUT_SKIPPED         2UL
#define XHCI_QK_OUT_FAILED          3UL

#define XHCI_QK_WHY_NONE            0UL
#define XHCI_QK_WHY_NOT_STARTED     1UL     /* no started lifetime or BAR   */
#define XHCI_QK_WHY_NOT_D0          2UL
#define XHCI_QK_WHY_UNREADABLE      3UL     /* window not decoding          */
#define XHCI_QK_WHY_ALL_ONES        4UL     /* a read of all ones           */
#define XHCI_QK_WHY_WINDOW_SMALL    5UL     /* the BAR does not reach it    */
#define XHCI_QK_WHY_NOT_HALTED      6UL     /* HCH not read set             */
#define XHCI_QK_WHY_RESET_REFUSED   7UL     /* xhciReset's rules refused    */
#define XHCI_QK_WHY_WRITE_FAILED    8UL     /* a configuration access failed */
#define XHCI_QK_WHY_READBACK        9UL     /* read back other than written */
#define XHCI_QK_WHY_BUSY            10UL    /* the write bit never cleared  */
#define XHCI_QK_WHY_READ_FF         11UL    /* the control byte read FFh    */
#define XHCI_QK_WHY_ID_UNREAD       12UL    /* Q1: no identity ever read    */
#define XHCI_QK_WHY_ID_KEPT         13UL    /* Q1: the identity kept        */

/*
 * One packed word per quirk, for the counter block (words 199 to 212) and
 * the start's log note (`quirk.<name>`):
 *
 *   bits 1:0    the mode in effect (XHCI_QK_MODE_*)
 *   bits 4:2    the normalization (XHCI_QK_NORM_*)
 *   bit 5       this controller's id is on the quirk's list
 *   bit 6       the quirk applies here (the effective policy)
 *   bit 7       overridden (XhciMsiAvoid under XhciInterruptMode 3)
 *   bits 11:8   the last outcome (XHCI_QK_OUT_*)
 *   bits 15:12  its reason (XHCI_QK_WHY_*)
 *   bits 31:16  the value as read, low 16 bits; FFFFh when none was read
 */
#define XHCI_QK_PACK_MODE(w)        ((w) & 0x3UL)
#define XHCI_QK_PACK_NORM(w)        (((w) >> 2) & 0x7UL)
#define XHCI_QK_PACK_MATCHED        0x00000020UL
#define XHCI_QK_PACK_EFFECTIVE      0x00000040UL
#define XHCI_QK_PACK_OVERRIDDEN     0x00000080UL
#define XHCI_QK_PACK_OUT(w)         (((w) >> 8) & 0xFUL)
#define XHCI_QK_PACK_WHY(w)         (((w) >> 12) & 0xFUL)
#define XHCI_QK_PACK_RAW(w)         (((w) >> 16) & 0xFFFFUL)

/* Not a value read: the raw field of an absent or mistyped value. */
#define XHCI_QK_RAW_NONE            0xFFFFFFFFUL

/*
 * The start's reading and the lifetime's counts, in XHCI_EXTENSION (zeroed
 * with it at every start). The executors in the HCD and the core read
 * XhciQuirkOn; the counts are the counter block's words 215 onward, copied
 * by the thread's poll (hcd_log.c, HcdCountersPoll). An even number of
 * ULONGs, so the extension's amd64 parity (TrailingPad) is unchanged.
 */
typedef struct _XHCI_QUIRK_STATE {
    ULONG Raw[XHCI_QK_COUNT];       /* as read, XHCI_QK_RAW_NONE if not   */
    ULONG Mode[XHCI_QK_COUNT];
    ULONG Norm[XHCI_QK_COUNT];
    ULONG Matched[XHCI_QK_COUNT];
    ULONG On[XHCI_QK_COUNT];        /* the quirk applies here             */
    ULONG Overridden[XHCI_QK_COUNT];
    ULONG Outcome[XHCI_QK_COUNT];
    ULONG Why[XHCI_QK_COUNT];
    ULONG Identity;                 /* the PCI id the lists were matched to */
    ULONG PciRevision;              /* configuration byte 08h, or ~0      */
    /* The counts (record 20 section 6.4). */
    ULONG MsiRefused;               /* Q1: a message refused or stripped  */
    ULONG HaltsLong;                /* Q8: suspend halts past 32 ms       */
    ULONG HaltDelays;               /* Q7: delays before the halt         */
    ULONG SavesSkipped;             /* Q3: suspends that did not save     */
    ULONG StuckLinkResets;          /* Q12: warm resets of a stuck port   */
    ULONG PaddedBlocks;             /* Q13: stream blocks padded          */
    ULONG LinkNoOps;                /* Q14a: No Op TRBs placed            */
    ULONG IsochDropped;             /* Q14c: Successes dropped            */
    ULONG TiCapped;                 /* Q10: interrupt endpoints capped    */
    ULONG AsmWrites;                /* Q9: write sequences completed      */
    ULONG AsmGiveUps;               /* Q9: sequences given up             */
    ULONG AmdBitClears;             /* Q16: writes made                   */
    ULONG AmdBitSkips;              /* Q16: suspends it was skipped at    */
    ULONG ShutEhciWrites;           /* Q4: shutdowns both registers wrote */
    ULONG ShutResets;               /* Q5: HCRSTs at shutdown             */
    ULONG ShutFlushes;              /* shutdown flushes acknowledged, drained */
    ULONG ShutFlushSinkOff;         /* ...acknowledged with the sink off  */
    ULONG ShutFlushUnacked;         /* ...not acknowledged in the bound   */
    ULONG Restores;                 /* resumes that restored (Q12's input) */
    ULONG Reserved;                 /* keeps the count even               */
} XHCI_QUIRK_STATE, *PXHCI_QUIRK_STATE;

XHCI_C_ASSERT(xhci_quirk_state_even,
              (sizeof(XHCI_QUIRK_STATE) % (2 * sizeof(ULONG))) == 0);

/* The value's name, its Linux flag and its short log label, for the log
 * and any reader that prints them. NULL past XHCI_QK_COUNT. */
const char *XhciQuirkValueName(ULONG quirk);
const char *XhciQuirkLinuxFlag(ULONG quirk);
const char *XhciQuirkLabel(ULONG quirk);

/* The default (1, or 0 for XhciShutdownReset), and whether a 2 is offered
 * (the generic quirks) or read as 1 (the vendor-register ones). */
ULONG XhciQuirkDefault(ULONG quirk);
ULONG XhciQuirkTwoOffered(ULONG quirk);

/* The mode a value read gives (XHCI_QK_FOUND_* as found), and through
 * `norm` what became of it. An unknown quirk is off. */
ULONG XhciQuirkMode(ULONG quirk, ULONG found, ULONG value, PULONG norm);

/* 1 when `vendorDevice` (PCI offset 0: device 31:16, vendor 15:0) is on the
 * quirk's list. VIA (1106) is vendor-wide for XhciResumeReinit. */
ULONG XhciQuirkMatches(ULONG quirk, ULONG vendorDevice);

/* Whether the quirk applies: every controller at 2, the listed ones at 1. */
ULONG XhciQuirkEffective(ULONG mode, ULONG matched);

/* The start: every quirk reset to its default and unread. */
VOID XhciQuirkInit(PXHCI_QUIRK_STATE st);

/* One value as read: its mode and normalization; the match not yet made. */
VOID XhciQuirkSetValue(PXHCI_QUIRK_STATE st, ULONG quirk, ULONG found,
                       ULONG value);

/* The match against `vendorDevice`, and the effective policy, for every
 * quirk but XhciMsiAvoid, which the start decides before the BAR is mapped
 * (XhciQuirkMsiAvoidDecide) and sets with XhciQuirkSetEffective. */
VOID XhciQuirkMatchAll(PXHCI_QUIRK_STATE st, ULONG vendorDevice);
VOID XhciQuirkSetEffective(PXHCI_QUIRK_STATE st, ULONG quirk, ULONG matched,
                           ULONG on, ULONG overridden);

/* 1 when the quirk applies on this controller. */
ULONG XhciQuirkOn(const XHCI_QUIRK_STATE *st, ULONG quirk);

/* The last action and why. */
VOID XhciQuirkOutcome(PXHCI_QUIRK_STATE st, ULONG quirk, ULONG outcome,
                      ULONG why);

/* The packed word above. */
ULONG XhciQuirkPacked(const XHCI_QUIRK_STATE *st, ULONG quirk);

/*
 * Q1 (record 20 section 4.1): whether the start avoids a message.
 * `mode` is XhciMsiAvoid's; `identityKnown` whether an identity was ever
 * read for this FDO, `vendorDevice` that identity. 0 off; 2 always; 1 on a
 * listed controller, and on an unknown one too - a wrongly avoided message
 * costs the line or polling, a wrongly taken one could cost a dead
 * controller.
 */
ULONG XhciQuirkMsiAvoidDecide(ULONG mode, ULONG identityKnown,
                              ULONG vendorDevice);

/* Q2 (section 4.2): HCCPARAMS1 as the stream consumers read it - MaxPSASize
 * (bits 15:12) cleared when XhciStreamsOff applies, so both the transport
 * decision and OPEN_STREAMS read a controller with no streams. */
ULONG XhciQuirkStreamsHcc1(ULONG streamsOff, ULONG hccparams1);

#endif /* XHCI_QUIRK_H */
