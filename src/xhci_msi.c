/*
 * xhci_msi.c - message-signalled interrupts, the pure half (xhci_msi.h;
 * design records 15 section 4.3 and 18).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_msi.h"
#include "xhci_poll.h"

#define XHCI_MSI_CAP_POINTER        0x34UL
#define XHCI_MSI_CAP_STATUS         0x06UL
#define XHCI_MSI_CAP_LIST_BIT       0x0010UL
#define XHCI_MSI_CAP_ID_MSI         0x05UL
#define XHCI_MSI_CAP_ID_MSIX        0x11UL
#define XHCI_MSI_CAP_WALK_MAX       48UL

static ULONG xhciMsiRd16(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8);
}

static ULONG xhciMsiRd32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static VOID xhciMsiWr32(PUCHAR p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFFUL);
    p[1] = (UCHAR)((v >> 8) & 0xFFUL);
    p[2] = (UCHAR)((v >> 16) & 0xFFUL);
    p[3] = (UCHAR)((v >> 24) & 0xFFUL);
}

ULONG XhciMsiCapRead(const UCHAR *config)
{
    ULONG ptr;
    ULONG n;
    ULONG msi;
    ULONG msix;

    msi = 0;
    msix = 0;
    if ((xhciMsiRd16(config + XHCI_MSI_CAP_STATUS) & XHCI_MSI_CAP_LIST_BIT) ==
        0) {
        return XHCI_POLL_DELIVERY_MESSAGE;
    }
    ptr = (ULONG)config[XHCI_MSI_CAP_POINTER];
    for (n = 0; n < XHCI_MSI_CAP_WALK_MAX; n++) {
        if (ptr < 0x40UL || ptr > 0xFCUL || (ptr & 3UL) != 0) {
            break;
        }
        if ((ULONG)config[ptr] == XHCI_MSI_CAP_ID_MSI) {
            if ((xhciMsiRd16(config + ptr + 2) & 0x0001UL) != 0) {
                msi = 1;
            }
        } else if ((ULONG)config[ptr] == XHCI_MSI_CAP_ID_MSIX) {
            if ((xhciMsiRd16(config + ptr + 2) & 0x8000UL) != 0) {
                msix = 1;
            }
        }
        ptr = (ULONG)config[ptr + 1];
    }
    if (msix) {
        return XHCI_POLL_DELIVERY_MSIX;
    }
    if (msi) {
        return XHCI_POLL_DELIVERY_MSI;
    }
    return XHCI_POLL_DELIVERY_MESSAGE;
}

VOID XhciMsiWalk(const UCHAR *list, PXHCI_MSI_WALK walk)
{
    ULONG remaining;
    ULONG offset;
    ULONG count;
    ULONG i;

    walk->Verdict = XHCI_MSI_WALK_OK;
    walk->ListSize = xhciMsiRd32(list);
    walk->Lists = 0;
    walk->Descriptors = 0;
    walk->Used = 0;
    walk->FailList = 0;
    if (walk->ListSize < XHCI_MSI_RRL_HEADER) {
        walk->Verdict = XHCI_MSI_WALK_SHORT_HEADER;
        return;
    }
    walk->Lists = xhciMsiRd32(list + XHCI_MSI_RRL_ALTERNATIVES);
    remaining = walk->ListSize - XHCI_MSI_RRL_HEADER;
    if (walk->Lists > remaining / XHCI_MSI_RL_HEADER) {
        walk->Verdict = XHCI_MSI_WALK_ALTERNATIVES;
        return;
    }
    offset = XHCI_MSI_RRL_HEADER;
    for (i = 0; i < walk->Lists; i++) {
        if (remaining < XHCI_MSI_RL_HEADER) {
            walk->Verdict = XHCI_MSI_WALK_LIST_HEADER;
            walk->FailList = i;
            return;
        }
        count = xhciMsiRd32(list + offset + XHCI_MSI_RL_COUNT);
        remaining -= XHCI_MSI_RL_HEADER;
        if (count > remaining / XHCI_MSI_RD_SIZE) {
            walk->Verdict = XHCI_MSI_WALK_COUNT;
            walk->FailList = i;
            return;
        }
        remaining -= count * XHCI_MSI_RD_SIZE;
        offset += XHCI_MSI_RL_HEADER + count * XHCI_MSI_RD_SIZE;
        walk->Descriptors += count;
    }
    walk->Used = offset;
}

/* Per configuration, its interrupt runs. */
#define XHCI_MSI_CFG_NONE           0UL
#define XHCI_MSI_CFG_STRIPPABLE     1UL
#define XHCI_MSI_CFG_BOUND          2UL
#define XHCI_MSI_CFG_UNSUPPORTED    3UL

/* Classifies one configuration at `cfg` (its header); adds its members to
 * the counts. */
static ULONG xhciMsiConfig(const UCHAR *cfg, PXHCI_MSI_CLASSIFY c)
{
    ULONG count;
    ULONG i;
    const UCHAR *d;
    ULONG runType;
    ULONG runMsg;
    ULONG runLine;
    ULONG interruptRuns;
    ULONG messageOnlyRuns;
    ULONG unsupported;
    ULONG option;
    ULONG type;

    count = xhciMsiRd32(cfg + XHCI_MSI_RL_COUNT);
    runType = 0xFFFFFFFFUL;
    runMsg = 0;
    runLine = 0;
    interruptRuns = 0;
    messageOnlyRuns = 0;
    unsupported = 0;
    for (i = 0; i <= count; i++) {
        d = cfg + XHCI_MSI_RL_HEADER + i * XHCI_MSI_RD_SIZE;
        option = (i < count) ? (ULONG)d[XHCI_MSI_RD_OPTION] : 0UL;
        /* A run ends where the next head begins, and at the end. */
        if (i == count || (option & XHCI_MSI_OPTION_ALTERNATIVE) == 0) {
            if (runType == XHCI_MSI_TYPE_INTERRUPT) {
                interruptRuns++;
                if (runMsg != 0 && runLine == 0) {
                    messageOnlyRuns++;
                }
            }
            if (i == count) {
                break;
            }
            runType = (ULONG)d[XHCI_MSI_RD_TYPE];
            runMsg = 0;
            runLine = 0;
        } else if (runType == 0xFFFFFFFFUL) {
            /* The list opens on an alternative: no head to belong to. */
            unsupported++;
            runType = (ULONG)d[XHCI_MSI_RD_TYPE];
        }
        type = (ULONG)d[XHCI_MSI_RD_TYPE];
        if (type != runType) {
            unsupported++;
        }
        if (type == XHCI_MSI_TYPE_INTERRUPT) {
            if ((xhciMsiRd16(d + XHCI_MSI_RD_FLAGS) & XHCI_MSI_FLAG_MESSAGE) !=
                0) {
                runMsg++;
                c->MessageMembers++;
            } else {
                runLine++;
                c->LineMembers++;
            }
        } else if (type == XHCI_MSI_TYPE_DEVPRIVATE) {
            c->DevicePrivate++;
        }
    }
    c->Unsupported += unsupported;
    if (unsupported != 0) {
        return XHCI_MSI_CFG_UNSUPPORTED;
    }
    if (interruptRuns == 0) {
        return XHCI_MSI_CFG_NONE;
    }
    if (messageOnlyRuns != 0) {
        return XHCI_MSI_CFG_BOUND;
    }
    return XHCI_MSI_CFG_STRIPPABLE;
}

VOID XhciMsiClassify(const UCHAR *list, const XHCI_MSI_WALK *walk,
                     PXHCI_MSI_CLASSIFY classify)
{
    const UCHAR *cfg;
    ULONG kind;
    ULONG i;

    classify->Class = XHCI_MSI_CLASS_UNKNOWN;
    classify->Strippable = 0;
    classify->MessageBound = 0;
    classify->NoInterrupt = 0;
    classify->MessageMembers = 0;
    classify->LineMembers = 0;
    classify->DevicePrivate = 0;
    classify->Unsupported = 0;
    if (walk->Verdict != XHCI_MSI_WALK_OK) {
        classify->Class = XHCI_MSI_CLASS_INCONSISTENT;
        return;
    }
    cfg = list + XHCI_MSI_RRL_HEADER;
    for (i = 0; i < walk->Lists; i++) {
        kind = xhciMsiConfig(cfg, classify);
        if (kind == XHCI_MSI_CFG_STRIPPABLE) {
            classify->Strippable++;
        } else if (kind == XHCI_MSI_CFG_BOUND) {
            classify->MessageBound++;
        } else if (kind == XHCI_MSI_CFG_NONE) {
            classify->NoInterrupt++;
        }
        cfg += XHCI_MSI_RL_HEADER +
               xhciMsiRd32(cfg + XHCI_MSI_RL_COUNT) * XHCI_MSI_RD_SIZE;
    }
    if (classify->Unsupported != 0) {
        classify->Class = XHCI_MSI_CLASS_UNSUPPORTED;
    } else if (classify->MessageMembers == 0) {
        classify->Class = (classify->LineMembers == 0)
                              ? XHCI_MSI_CLASS_NO_INTERRUPT
                              : XHCI_MSI_CLASS_NO_MESSAGE;
    } else if (classify->Strippable != 0) {
        classify->Class = XHCI_MSI_CLASS_REPLACEABLE;
    } else {
        classify->Class = XHCI_MSI_CLASS_NO_LINE;
    }
}

static VOID xhciMsiCopy(PUCHAR to, const UCHAR *from, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        to[i] = from[i];
    }
}

/* Is descriptor `d` a message interrupt? */
static ULONG xhciMsiIsMessage(const UCHAR *d)
{
    return ((ULONG)d[XHCI_MSI_RD_TYPE] == XHCI_MSI_TYPE_INTERRUPT &&
            (xhciMsiRd16(d + XHCI_MSI_RD_FLAGS) & XHCI_MSI_FLAG_MESSAGE) != 0)
               ? 1UL
               : 0UL;
}

/*
 * Writes (or, `out` NULL, sizes) the rewritten list: mode 0 the strip
 * (STRIPPABLE configurations, message members removed), mode 1 the no-line
 * rewrite (configurations holding a message, line members of mixed runs
 * removed). A removed head's run is headed by its next kept member.
 */
static ULONG xhciMsiRewrite(const UCHAR *list, const XHCI_MSI_WALK *walk,
                            ULONG mode, PUCHAR out, ULONG capacity)
{
    XHCI_MSI_CLASSIFY scratch;
    const UCHAR *cfg;
    const UCHAR *d;
    ULONG count;
    ULONG i;
    ULONG j;
    ULONG size;
    ULONG lists;
    ULONG cfgOut;
    ULONG kept;
    ULONG keepCfg;
    ULONG keep;
    ULONG pendingHead;
    ULONG runHasMsg;
    ULONG runHasLine;
    ULONG k;
    const UCHAR *e;
    ULONG option;

    size = XHCI_MSI_RRL_HEADER;
    lists = 0;
    if (out != NULL) {
        if (capacity < XHCI_MSI_RRL_HEADER) {
            return 0;
        }
        xhciMsiCopy(out, list, XHCI_MSI_RRL_HEADER);
    }
    cfg = list + XHCI_MSI_RRL_HEADER;
    for (i = 0; i < walk->Lists; i++) {
        count = xhciMsiRd32(cfg + XHCI_MSI_RL_COUNT);
        scratch.MessageMembers = 0;
        scratch.LineMembers = 0;
        scratch.DevicePrivate = 0;
        scratch.Unsupported = 0;
        keepCfg = 0;
        if (mode == 0) {
            keepCfg = (xhciMsiConfig(cfg, &scratch) ==
                       XHCI_MSI_CFG_STRIPPABLE) ? 1UL : 0UL;
        } else {
            /* The rewrite keeps a configuration with at least one
             * interrupt run holding a message; one whose interrupt runs
             * are all line-only goes. */
            (VOID)xhciMsiConfig(cfg, &scratch);
            keepCfg = (scratch.Unsupported == 0 &&
                       scratch.MessageMembers != 0) ? 1UL : 0UL;
        }
        if (keepCfg) {
            cfgOut = size;
            size += XHCI_MSI_RL_HEADER;
            if (out != NULL) {
                if (size > capacity) {
                    return 0;
                }
                xhciMsiCopy(out + cfgOut, cfg, XHCI_MSI_RL_HEADER);
            }
            kept = 0;
            pendingHead = 0;
            runHasMsg = 0;
            runHasLine = 0;
            for (j = 0; j < count; j++) {
                d = cfg + XHCI_MSI_RL_HEADER + j * XHCI_MSI_RD_SIZE;
                option = (ULONG)d[XHCI_MSI_RD_OPTION];
                if ((option & XHCI_MSI_OPTION_ALTERNATIVE) == 0) {
                    /* A new run: what it holds, for the rewrite. */
                    pendingHead = 0;
                    runHasMsg = 0;
                    runHasLine = 0;
                    for (k = j; k < count; k++) {
                        e = cfg + XHCI_MSI_RL_HEADER + k * XHCI_MSI_RD_SIZE;
                        if (k != j && ((ULONG)e[XHCI_MSI_RD_OPTION] &
                                       XHCI_MSI_OPTION_ALTERNATIVE) == 0) {
                            break;
                        }
                        if ((ULONG)e[XHCI_MSI_RD_TYPE] ==
                            XHCI_MSI_TYPE_INTERRUPT) {
                            if (xhciMsiIsMessage(e)) {
                                runHasMsg = 1;
                            } else {
                                runHasLine = 1;
                            }
                        }
                    }
                }
                if (mode == 0) {
                    keep = xhciMsiIsMessage(d) ? 0UL : 1UL;
                } else {
                    keep = (runHasMsg && runHasLine && !xhciMsiIsMessage(d) &&
                            (ULONG)d[XHCI_MSI_RD_TYPE] ==
                                XHCI_MSI_TYPE_INTERRUPT)
                               ? 0UL
                               : 1UL;
                }
                if (!keep) {
                    if ((option & XHCI_MSI_OPTION_ALTERNATIVE) == 0) {
                        pendingHead = 1;
                    }
                    continue;
                }
                if (out != NULL) {
                    if (size + XHCI_MSI_RD_SIZE > capacity) {
                        return 0;
                    }
                    xhciMsiCopy(out + size, d, XHCI_MSI_RD_SIZE);
                    if (pendingHead &&
                        (option & XHCI_MSI_OPTION_ALTERNATIVE) != 0) {
                        out[size + XHCI_MSI_RD_OPTION] =
                            (UCHAR)(option & ~XHCI_MSI_OPTION_ALTERNATIVE);
                    }
                }
                pendingHead = 0;
                size += XHCI_MSI_RD_SIZE;
                kept++;
            }
            if (out != NULL) {
                xhciMsiWr32(out + cfgOut + XHCI_MSI_RL_COUNT, kept);
            }
            lists++;
        }
        cfg += XHCI_MSI_RL_HEADER + count * XHCI_MSI_RD_SIZE;
    }
    if (lists == 0) {
        return 0;
    }
    if (out != NULL) {
        xhciMsiWr32(out, size);
        xhciMsiWr32(out + XHCI_MSI_RRL_ALTERNATIVES, lists);
    }
    return size;
}

ULONG XhciMsiStrip(const UCHAR *list, const XHCI_MSI_WALK *walk,
                   const XHCI_MSI_CLASSIFY *classify, PUCHAR out,
                   ULONG capacity)
{
    if (walk->Verdict != XHCI_MSI_WALK_OK ||
        classify->Class != XHCI_MSI_CLASS_REPLACEABLE) {
        return 0;
    }
    return xhciMsiRewrite(list, walk, 0, out, capacity);
}

ULONG XhciMsiInjNoLine(const UCHAR *list, const XHCI_MSI_WALK *walk,
                       PUCHAR out, ULONG capacity)
{
    XHCI_MSI_CLASSIFY c;

    if (walk->Verdict != XHCI_MSI_WALK_OK) {
        return 0;
    }
    XhciMsiClassify(list, walk, &c);
    /* Nothing to rewrite without a message, and nothing more once no line
     * is offered. */
    if (c.Class != XHCI_MSI_CLASS_REPLACEABLE) {
        return 0;
    }
    return xhciMsiRewrite(list, walk, 1, out, capacity);
}

ULONG XhciMsiOutcome(ULONG listClass)
{
    switch (listClass) {
    case XHCI_MSI_CLASS_NO_INTERRUPT:
        return XHCI_MSI_OUT_NO_INTERRUPT;
    case XHCI_MSI_CLASS_NO_MESSAGE:
        return XHCI_MSI_OUT_NO_MESSAGE;
    case XHCI_MSI_CLASS_REPLACEABLE:
        return XHCI_MSI_OUT_REPLACED;
    case XHCI_MSI_CLASS_NO_LINE:
        return XHCI_MSI_OUT_NO_LINE;
    case XHCI_MSI_CLASS_UNSUPPORTED:
        return XHCI_MSI_OUT_UNSUPPORTED;
    default:
        return XHCI_MSI_OUT_INCONSISTENT;
    }
}

VOID XhciMsiFilterInit(PXHCI_MSI_FILTER_RECORD record)
{
    record->Sequence = 0;
    record->Class = XHCI_MSI_CLASS_UNKNOWN;
    record->Outcome = XHCI_MSI_OUT_NONE;
}

VOID XhciMsiFilterBegin(PXHCI_MSI_FILTER_RECORD record)
{
    record->Sequence++;
    record->Class = XHCI_MSI_CLASS_UNKNOWN;
    record->Outcome = XHCI_MSI_OUT_NONE;
}

VOID XhciMsiFilterPublish(PXHCI_MSI_FILTER_RECORD record, ULONG listClass,
                          ULONG outcome)
{
    record->Class = listClass;
    record->Outcome = outcome;
}

VOID XhciMsi86BindInit(PXHCI_MSI86_BIND bind)
{
    bind->State = XHCI_MSI86_STATE_UNSEEN;
    bind->Held = 0;
    bind->Looks = 0;
}

ULONG XhciMsi86BindStep(PXHCI_MSI86_BIND bind, ULONG event)
{
    ULONG act;

    act = 0;
    switch (event) {
    case XHCI_MSI86_EV_FILTER:
    case XHCI_MSI86_EV_START:
        if (bind->State != XHCI_MSI86_STATE_BOUND &&
            bind->State != XHCI_MSI86_STATE_REMOVED) {
            bind->Looks++;
            act = XHCI_MSI86_ACT_LOOK;
        }
        break;
    case XHCI_MSI86_EV_REMOVE:
        if (bind->Held) {
            act = XHCI_MSI86_ACT_DROP;
        }
        bind->Held = 0;
        bind->State = XHCI_MSI86_STATE_REMOVED;
        break;
    case XHCI_MSI86_EV_LOOK_ABSENT:
        if (bind->State != XHCI_MSI86_STATE_BOUND &&
            bind->State != XHCI_MSI86_STATE_REMOVED) {
            bind->State = XHCI_MSI86_STATE_ABSENT;
        }
        break;
    case XHCI_MSI86_EV_LOOK_FAILED:
        act = XHCI_MSI86_ACT_DROP_LOOK;
        if (bind->State != XHCI_MSI86_STATE_BOUND &&
            bind->State != XHCI_MSI86_STATE_REMOVED) {
            bind->State = XHCI_MSI86_STATE_ABSENT;
        }
        break;
    case XHCI_MSI86_EV_LOOK_REFUSED:
        act = XHCI_MSI86_ACT_DROP_LOOK;
        if (bind->State != XHCI_MSI86_STATE_BOUND &&
            bind->State != XHCI_MSI86_STATE_REMOVED) {
            bind->State = XHCI_MSI86_STATE_REFUSED;
        }
        break;
    case XHCI_MSI86_EV_LOOK_ACCEPTED:
        if (bind->State == XHCI_MSI86_STATE_BOUND ||
            bind->State == XHCI_MSI86_STATE_REMOVED) {
            act = XHCI_MSI86_ACT_DROP_LOOK;
        } else {
            bind->State = XHCI_MSI86_STATE_BOUND;
            bind->Held = 1;
        }
        break;
    default:
        /* STOP and SURPRISE ask for nothing. */
        break;
    }
    return act;
}

static ULONG xhciMsiTraceHex(char *line, ULONG len, ULONG value,
                             ULONG digits)
{
    static const char hex[] = "0123456789ABCDEF";
    ULONG i;

    for (i = 0; i < digits; i++) {
        line[len + i] =
            hex[(value >> (4 * (digits - 1 - i))) & 0xFUL];
    }
    return len + digits;
}

ULONG XhciMsiTraceFormat(char *line, char kind, ULONG fdo, ULONG sequence,
                         ULONG number, const ULONG *fields, ULONG count)
{
    ULONG len;
    ULONG i;
    ULONG sum;

    if (count > XHCI_MSI_TRACE_FIELDS) {
        count = XHCI_MSI_TRACE_FIELDS;
    }
    line[0] = 'M';
    line[1] = '8';
    line[2] = '6';
    line[3] = ' ';
    line[4] = kind;
    line[5] = ' ';
    len = xhciMsiTraceHex(line, 6, fdo & 0xFFFFUL, 4);
    line[len++] = ' ';
    len = xhciMsiTraceHex(line, len, sequence & 0xFFFFUL, 4);
    line[len++] = ' ';
    len = xhciMsiTraceHex(line, len, number & 0xFFFFUL, 4);
    for (i = 0; i < count; i++) {
        line[len++] = ' ';
        len = xhciMsiTraceHex(line, len, fields[i], 8);
    }
    sum = 0;
    for (i = 0; i < len; i++) {
        sum += (ULONG)(UCHAR)line[i];
    }
    line[len++] = ' ';
    line[len++] = 'c';
    len = xhciMsiTraceHex(line, len, sum & 0xFFUL, 2);
    line[len] = '\0';
    return len;
}
