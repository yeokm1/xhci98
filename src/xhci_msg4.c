/*
 * xhci_msg4.c - XhciInterruptMode 4's pure half (design record 15 section
 * 4.5; roadmap-usb4 task 49.24). See xhci_msg4.h. Host suite test_msg4.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_msg4.h"
#include "xhci_poll.h"

/* ------------------------------------------------------------------ */
/* The decision                                                        */
/* ------------------------------------------------------------------ */

static ULONG xhciMsg4Value(ULONG read)
{
    return (read == XHCI_MSG4_NOT_FOUND) ? 0UL : read;
}

/* The seven ISA IRQs a claim can ever take (msi4nt record 05 section 2.2:
 * never 0, 1, 2, 8, 9 or 12 to 15). */
static ULONG xhciMsg4IsaIrq(ULONG irq)
{
    return (irq == 3 || irq == 4 || irq == 5 || irq == 6 || irq == 7 ||
            irq == 10 || irq == 11) ? 1UL : 0UL;
}

ULONG XhciMsg4Decide(const XHCI_MSG4_IN *in, PXHCI_MSG4_CLAIM claim,
                     PULONG reason)
{
    XHCI_MSG4_CLAIM c;
    ULONG first;
    ULONG last;
    ULONG isa;
    ULONG high;

    c.Strategy = 0;
    c.Irq = 0;
    c.Pools = XHCI_MSG4_POOLS_ISA;
    c.LibPools = 1;
    c.HighFirst = 0;
    c.HighLast = 0;
    c.Budget = 0;
    if (claim != NULL) {
        *claim = c;
    }
    if (in->System != XHCI_MSG4_SYS_NT5) {
        *reason = XHCI_POLL_R_MSG4_NO_SYSTEM;
        return XHCI_POLL_DELIVERY_NONE;
    }
    if (in->Quarantined) {
        *reason = XHCI_POLL_R_MSG4_QUARANTINED;
        return XHCI_POLL_DELIVERY_NONE;
    }
    if (in->Avoided) {
        *reason = XHCI_POLL_R_MSG4_AVOIDED;
        return XHCI_POLL_DELIVERY_NONE;
    }

    c.Strategy = xhciMsg4Value(in->Strategy);
    if (c.Strategy != XHCI_MSG4_STRATEGY_CLAIM &&
        c.Strategy != XHCI_MSG4_STRATEGY_BORROW) {
        *reason = XHCI_POLL_R_MSG4_VALUE_INVALID;
        return XHCI_POLL_DELIVERY_NONE;
    }
    if (c.Strategy == XHCI_MSG4_STRATEGY_CLAIM) {
        c.Pools = in->PoolsOffered ? xhciMsg4Value(in->Pools)
                                   : XHCI_MSG4_POOLS_ISA;
        if (c.Pools != XHCI_MSG4_POOLS_ISA && c.Pools != XHCI_MSG4_POOLS_HIGH &&
            c.Pools != XHCI_MSG4_POOLS_BOTH) {
            *reason = XHCI_POLL_R_MSG4_VALUE_INVALID;
            return XHCI_POLL_DELIVERY_NONE;
        }
        isa = (c.Pools != XHCI_MSG4_POOLS_HIGH) ? 1UL : 0UL;
        high = (c.Pools != XHCI_MSG4_POOLS_ISA) ? 1UL : 0UL;
        c.LibPools = (isa ? 1UL : 0UL) | (high ? 2UL : 0UL);
        first = XHCI_MSG4_HIGH_FIRST;
        last = XHCI_MSG4_HIGH_LAST;
        if (high) {
            /* The range and the budget mean something only with the upper
             * pool: the library refuses either in a call without it, so
             * they are neither read nor passed otherwise. */
            c.HighFirst = xhciMsg4Value(in->InputFirst);
            c.HighLast = xhciMsg4Value(in->InputLast);
            c.Budget = xhciMsg4Value(in->Budget);
            if (c.HighFirst != 0 || c.HighLast != 0) {
                if (c.HighFirst < XHCI_MSG4_HIGH_FIRST ||
                    c.HighLast > XHCI_MSG4_HIGH_LAST ||
                    c.HighFirst > c.HighLast) {
                    *reason = XHCI_POLL_R_MSG4_VALUE_INVALID;
                    return XHCI_POLL_DELIVERY_NONE;
                }
                first = c.HighFirst;
                last = c.HighLast;
            }
            if (c.Budget > XHCI_MSG4_BUDGET_MAX) {
                *reason = XHCI_POLL_R_MSG4_VALUE_INVALID;
                return XHCI_POLL_DELIVERY_NONE;
            }
        }
        c.Irq = xhciMsg4Value(in->IsaIrq);
        if (c.Irq != 0 && !(isa && xhciMsg4IsaIrq(c.Irq)) &&
            !(high && c.Irq >= first && c.Irq <= last)) {
            *reason = XHCI_POLL_R_MSG4_VALUE_INVALID;
            return XHCI_POLL_DELIVERY_NONE;
        }
    } else if (!in->HaveInterrupt || in->Message ||
               (in->PinStatus == XHCI_POLL_PIN_READ && in->Pin == 0)) {
        *reason = XHCI_POLL_R_MSG4_NO_LINE;
        return XHCI_POLL_DELIVERY_NONE;
    }
    if (claim != NULL) {
        *claim = c;
    }
    *reason = XHCI_POLL_R_FORCED_MSG4;
    return XHCI_POLL_DELIVERY_MESSAGE;
}

ULONG XhciMsg4ShareValue(ULONG read)
{
    if (read == XHCI_MSG4_NOT_FOUND) {
        return XHCI_MSG4_SHARE_DEFAULT;
    }
    if (read < XHCI_MSG4_SHARE_MIN) {
        /* 1, and 0 as 1: one controller to an IRQ is no sharing. */
        return 0;
    }
    if (read > XHCI_MSG4_SHARE_MAX) {
        return XHCI_MSG4_SHARE_MAX;
    }
    return read;
}

ULONG XhciMsg4MaxMembers(ULONG share)
{
    return share;
}

/* ------------------------------------------------------------------ */
/* The veto's walk                                                     */
/* ------------------------------------------------------------------ */

static ULONG xhciMsg4Le32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

ULONG XhciMsg4ShareScan(const UCHAR *data, ULONG length, ULONG pdSize,
                        ULONG vector, PULONG interrupts)
{
    ULONG fulls;
    ULONG partials;
    ULONG off;
    ULONG f;
    ULONG p;
    ULONG extra;
    ULONG found;

    found = 0;
    if (data == NULL || (pdSize != 16 && pdSize != 20) ||
        length < XHCI_MSG4_RL_HEADER) {
        return XHCI_MSG4_SHARE_MALFORMED;
    }
    fulls = xhciMsg4Le32(data);
    if (fulls > XHCI_MSG4_MAX_FULL) {
        return XHCI_MSG4_SHARE_MALFORMED;
    }
    off = XHCI_MSG4_RL_HEADER;
    for (f = 0; f < fulls; f++) {
        if (length - off < XHCI_MSG4_FULL_HEADER) {
            return XHCI_MSG4_SHARE_MALFORMED;
        }
        partials = xhciMsg4Le32(data + off + XHCI_MSG4_FULL_COUNT);
        if (partials > XHCI_MSG4_MAX_PARTIAL) {
            return XHCI_MSG4_SHARE_MALFORMED;
        }
        off += XHCI_MSG4_FULL_HEADER;
        for (p = 0; p < partials; p++) {
            if (length - off < pdSize) {
                return XHCI_MSG4_SHARE_MALFORMED;
            }
            extra = 0;
            if (data[off + XHCI_MSG4_PD_TYPE] == XHCI_MSG4_TYPE_INTERRUPT) {
                if (interrupts != NULL) {
                    (*interrupts)++;
                }
                if (xhciMsg4Le32(data + off + XHCI_MSG4_PD_VECTOR) == vector) {
                    found = 1;
                }
            } else if (data[off + XHCI_MSG4_PD_TYPE] ==
                       XHCI_MSG4_TYPE_DEVSPECIFIC) {
                extra = xhciMsg4Le32(data + off + XHCI_MSG4_PD_DATASIZE);
            }
            off += pdSize;
            if (extra > length - off) {
                return XHCI_MSG4_SHARE_MALFORMED;
            }
            off += extra;
        }
    }
    return found ? XHCI_MSG4_SHARE_FOUND : XHCI_MSG4_SHARE_NONE;
}

static const char xhciMsg4Suffix[] = ".Translated";
#define XHCI_MSG4_SUFFIX_CHARS 11UL

ULONG XhciMsg4NameIsTranslated(const USHORT *name, ULONG chars)
{
    ULONG i;

    if (name == NULL || chars < XHCI_MSG4_SUFFIX_CHARS) {
        return 0;
    }
    name += chars - XHCI_MSG4_SUFFIX_CHARS;
    for (i = 0; i < XHCI_MSG4_SUFFIX_CHARS; i++) {
        if (name[i] != (USHORT)(UCHAR)xhciMsg4Suffix[i]) {
            return 0;
        }
    }
    return 1;
}

ULONG XhciMsg4NameIsOwn(const USHORT *name, ULONG chars, const USHORT *own,
                        ULONG ownChars)
{
    ULONG i;

    if (own == NULL || ownChars == 0 ||
        chars != ownChars + XHCI_MSG4_SUFFIX_CHARS ||
        !XhciMsg4NameIsTranslated(name, chars)) {
        return 0;
    }
    for (i = 0; i < ownChars; i++) {
        if (name[i] != own[i]) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The refusal table                                                   */
/* ------------------------------------------------------------------ */

ULONG XhciMsg4Bdf(ULONG bus, ULONG device, ULONG function)
{
    return ((bus & 0xFFUL) << 8) | ((device & 0x1FUL) << 3) |
           (function & 0x7UL);
}

ULONG XhciMsg4Refused(const XHCI_MSG4_REFUSALS *t, ULONG known, ULONG bdf)
{
    ULONG i;

    if (t->Latched) {
        return 1;
    }
    if (!known) {
        return 0;
    }
    for (i = 0; i < t->Count && i < XHCI_MSG4_REFUSAL_SLOTS; i++) {
        if (t->Bdf[i] == bdf) {
            return 1;
        }
    }
    return 0;
}

ULONG XhciMsg4RefusalAdd(PXHCI_MSG4_REFUSALS t, ULONG known, ULONG bdf)
{
    ULONG i;

    if (!known) {
        t->Latched = 1;
        return 1;
    }
    for (i = 0; i < t->Count && i < XHCI_MSG4_REFUSAL_SLOTS; i++) {
        if (t->Bdf[i] == bdf) {
            return 1;
        }
    }
    if (t->Count >= XHCI_MSG4_REFUSAL_SLOTS) {
        t->Latched = 1;
        return 0;
    }
    t->Bdf[t->Count] = bdf;
    t->Count++;
    if (t->Count >= XHCI_MSG4_REFUSAL_SLOTS) {
        t->Latched = 1;
    }
    return 1;
}

ULONG XhciMsg4RefusalCount(const XHCI_MSG4_REFUSALS *t)
{
    return t->Count + (t->Latched ? 1UL : 0UL);
}

/* ------------------------------------------------------------------ */
/* The brake of strategy 1                                             */
/* ------------------------------------------------------------------ */

ULONG XhciMsg4StrayStep(PULONG run, ULONG claimed, ULONG pending)
{
    if (claimed || pending) {
        *run = 0;
        return 0;
    }
    if (*run >= XHCI_MSG4_STRAY_LIMIT) {
        return 0;
    }
    (*run)++;
    return (*run == XHCI_MSG4_STRAY_LIMIT) ? 1UL : 0UL;
}

/* ------------------------------------------------------------------ */
/* The stranded-event judge                                            */
/* ------------------------------------------------------------------ */

VOID XhciMsg4JudgeReset(PXHCI_MSG4_JUDGE j, ULONG claims)
{
    ULONG i;

    j->Held = 0;
    for (i = 0; i < XHCI_MSG4_STRANDED_LIMIT; i++) {
        j->Stamp[i] = 0;
    }
    j->Run = 0;
    j->Claims = claims;
    j->DeadlineValid = 0;
    j->Deadline = 0;
}

ULONG XhciMsg4JudgeStranded(PXHCI_MSG4_JUDGE j, ULONG now)
{
    ULONG i;

    j->Stranded++;
    if (j->Tripped) {
        return XHCI_MSG4_JUDGE_OK;
    }
    if (j->Held >= XHCI_MSG4_STRANDED_LIMIT) {
        for (i = 1; i < XHCI_MSG4_STRANDED_LIMIT; i++) {
            j->Stamp[i - 1] = j->Stamp[i];
        }
        j->Held = XHCI_MSG4_STRANDED_LIMIT - 1;
    }
    j->Stamp[j->Held] = now;
    j->Held++;
    if (j->Held == XHCI_MSG4_STRANDED_LIMIT &&
        now - j->Stamp[0] <= XHCI_MSG4_STRANDED_WINDOW_TICKS) {
        j->Tripped = 1;
        return XHCI_MSG4_JUDGE_TRIP;
    }
    return XHCI_MSG4_JUDGE_OK;
}

VOID XhciMsg4JudgeDrain(PXHCI_MSG4_JUDGE j, ULONG events, ULONG claims,
                        ULONG now)
{
    if (j->Tripped) {
        return;
    }
    if (claims != j->Claims) {
        j->Claims = claims;
        j->Run = 0;
        j->DeadlineValid = 0;
        return;
    }
    if (events == 0) {
        return;
    }
    if (j->Run < XHCI_MSG4_STRANDED_LIMIT) {
        j->Run++;
    }
    if (j->Run == XHCI_MSG4_STRANDED_LIMIT && !j->DeadlineValid) {
        j->DeadlineValid = 1;
        j->Deadline = now + 1;
    }
}

ULONG XhciMsg4JudgeTick(PXHCI_MSG4_JUDGE j, ULONG claims, ULONG now)
{
    if (j->Tripped) {
        return XHCI_MSG4_JUDGE_OK;
    }
    if (claims != j->Claims) {
        j->Claims = claims;
        j->Run = 0;
        j->DeadlineValid = 0;
        return XHCI_MSG4_JUDGE_OK;
    }
    if (j->DeadlineValid && (LONG)(now - j->Deadline) >= 0) {
        j->Tripped = 1;
        return XHCI_MSG4_JUDGE_TRIP;
    }
    return XHCI_MSG4_JUDGE_OK;
}

/* ------------------------------------------------------------------ */
/* A refused claim's reasons                                           */
/* ------------------------------------------------------------------ */

VOID XhciMsg4PackReasons(const UCHAR *reasons16, PULONG words4)
{
    ULONG w;
    ULONG b;

    for (w = 0; w < 4; w++) {
        words4[w] = 0;
        for (b = 0; b < 4; b++) {
            words4[w] |= (ULONG)reasons16[w * 4 + b] << (b * 8);
        }
    }
}
