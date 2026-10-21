/*
 * cm_eject.c - the pure half of the Windows 98 SE and ME eject transaction
 * and the 9x statement decision (cm_eject.h; roadmap-usb4.md task 43.18;
 * design record 21 section 7.2, "The 9x half", parts 1 and 2). The driver
 * (cm_ejectos.c) calls each function under its record lock;
 * test\test_cm_eject.c runs them on the host.
 *
 * DDK-free: part of the pure core. C89, no 64-bit arithmetic. IRQL: any.
 */

#include "../xhci_compat.h"
#include "tbt_topo.h"
#include "cm_nvlink.h"
#include "cm_eject.h"

static VOID cmEjPut(UCHAR *b, ULONG at, ULONG v)
{
    b[at] = (UCHAR)(v & 0xFFUL);
    b[at + 1] = (UCHAR)((v >> 8) & 0xFFUL);
    b[at + 2] = (UCHAR)((v >> 16) & 0xFFUL);
    b[at + 3] = (UCHAR)((v >> 24) & 0xFFUL);
}

static ULONG cmEjGet(const UCHAR *b, ULONG at)
{
    return (ULONG)b[at] | ((ULONG)b[at + 1] << 8) |
           ((ULONG)b[at + 2] << 16) | ((ULONG)b[at + 3] << 24);
}

/* ---- the ask ------------------------------------------------------------ */

BOOLEAN CmEjAskDecode(const UCHAR *in, ULONG bytes, CM_EJ_ASK *ask)
{
    if (in == NULL || bytes != CM_EJ_ASK_BYTES) {
        return FALSE;
    }
    ask->Nonce = cmEjGet(in, CM_EJ_A_NONCE);
    ask->Member = cmEjGet(in, CM_EJ_A_MEMBER);
    ask->Unit = cmEjGet(in, CM_EJ_A_UNIT);
    ask->EpochLo = cmEjGet(in, CM_EJ_A_EPOCH_LO);
    ask->EpochHi = cmEjGet(in, CM_EJ_A_EPOCH_HI);
    ask->ContentGen = cmEjGet(in, CM_EJ_A_CONTENT_GEN);
    ask->Flags = cmEjGet(in, CM_EJ_A_FLAGS);
    return (BOOLEAN)(ask->Nonce != 0);
}

VOID CmEjAskEncode(UCHAR *out, const CM_EJ_ASK *ask)
{
    cmEjPut(out, CM_EJ_A_NONCE, ask->Nonce);
    cmEjPut(out, CM_EJ_A_MEMBER, ask->Member);
    cmEjPut(out, CM_EJ_A_UNIT, ask->Unit);
    cmEjPut(out, CM_EJ_A_EPOCH_LO, ask->EpochLo);
    cmEjPut(out, CM_EJ_A_EPOCH_HI, ask->EpochHi);
    cmEjPut(out, CM_EJ_A_CONTENT_GEN, ask->ContentGen);
    cmEjPut(out, CM_EJ_A_FLAGS, ask->Flags);
}

ULONG CmEjAccept(const CM_EJ_ASK *ask, const CM_EJ_CHECK *check)
{
    /* A location is bus 15:8, device 7:3, function 2:0 and nothing above.
     * The unit's bus is the one the driver resolved (CM_EJ_A_UNIT's bus 0
     * names the member's secondary bus); it is never bus 0, which no
     * tunnel port's secondary bus is. */
    if (ask->Nonce == 0 || ask->Flags != 0 ||
        (ask->Member & 0xFFFF0000UL) != 0 ||
        (ask->Unit & 0xFFFF0000UL) != 0 || ask->Unit == ask->Member) {
        return CM_EJ_WHY_BAD;
    }
    if (ask->EpochLo != check->EpochLo || ask->EpochHi != check->EpochHi ||
        (check->EpochLo == 0 && check->EpochHi == 0)) {
        return CM_EJ_WHY_EPOCH;
    }
    if (ask->ContentGen == 0 || ask->ContentGen != check->ContentGen) {
        return CM_EJ_WHY_GENERATION;
    }
    if (!check->Available) {
        return CM_EJ_WHY_UNAVAILABLE;
    }
    if (!check->Member) {
        return CM_EJ_WHY_MEMBER;
    }
    if (check->UnitId == 0 || check->UnitId == 0xFFFFFFFFUL ||
        (check->UnitId & 0xFFFFUL) == 0xFFFFUL ||
        (check->UnitId & 0xFFFFUL) == 0) {
        return CM_EJ_WHY_NOTHING;
    }
    if (!check->Below || (ask->Unit >> 8) == 0) {
        return CM_EJ_WHY_NOT_BELOW;
    }
    if (check->NoChannel) {
        /* Refused before the query: nothing irreversible is made for a
         * drive no clean stop can be asked of. */
        return CM_EJ_WHY_NO_CHANNEL;
    }
    if (check->Busy) {
        return CM_EJ_WHY_BUSY;
    }
    return 0;
}

VOID CmEjReplyEncode(UCHAR *out, ULONG nonce, ULONG reason)
{
    cmEjPut(out, CM_EJ_R_NONCE, nonce);
    cmEjPut(out, CM_EJ_R_REASON, reason);
}

/* ---- the transaction ---------------------------------------------------- */

static VOID cmEjZero(PUCHAR to, ULONG bytes)
{
    ULONG k;

    for (k = 0; k < bytes; k++) {
        to[k] = 0;
    }
}

/* A step change, written at the next chance. A terminal step stays: what
 * follows it (O6's retries, a late success) is the member's state. */
static VOID cmEjStepTo(CM_EJ_TX *t, ULONG step)
{
    if ((t->Flags & CM_EJ_F_TERMINAL) != 0 || t->Step == step) {
        return;
    }
    t->Step = step;
    t->Flags |= CM_EJ_F_DIRTY;
}

static VOID cmEjTerminal(CM_EJ_TX *t, ULONG step, ULONG outcome, ULONG nowMs)
{
    if ((t->Flags & CM_EJ_F_TERMINAL) != 0) {
        return;
    }
    t->Step = step;
    t->Outcome = outcome;
    t->TerminalAt = nowMs;
    t->Flags |= CM_EJ_F_TERMINAL | CM_EJ_F_DIRTY;
}

/* R11 O6's deadline, 30 s after the first attempt whether or not a call
 * still runs: the result blocked (it keeps failing), and the tunnel's
 * teardown owed once - kept as an obligation whoever finds the deadline
 * (a late taker or a detached loop included); a start that knows the
 * member's USB4 port makes it (CmEjNext), the firmware connection manager
 * has none to make. One transition for every place the deadline is
 * read. */
static VOID cmEjO6(CM_EJ_TX *t, ULONG nowMs)
{
    if ((t->Flags & CM_EJ_F_BLOCKED) != 0 || t->Attempts == 0 ||
        nowMs - t->FirstRemoveAt < CM_NVLINK_RETRY_FOR_MS) {
        return;
    }
    t->Flags |= CM_EJ_F_BLOCKED;
    cmEjTerminal(t, CM_EJ_STEP_BLOCKED, CM_EJ_OUT_KEEPS_FAILING, nowMs);
    if ((t->Flags & CM_EJ_F_TORN) == 0) {
        t->Flags |= CM_EJ_F_O6TEAR;
    }
}

/* Blocked with nothing authorised: the devnode stays query-removed, the
 * request stays in place, the member blocked until the unplug (V6). */
static VOID cmEjHold(CM_EJ_TX *t, ULONG nowMs)
{
    t->Flags |= CM_EJ_F_BLOCKED;
    t->Flags &= ~(CM_EJ_F_ISOLATE | CM_EJ_F_POST | CM_EJ_F_O6TEAR);
    t->Phase = CM_EJ_P_HELD;
    cmEjTerminal(t, CM_EJ_STEP_BLOCKED, CM_EJ_OUT_NO_STOP, nowMs);
}

VOID CmEjBegin(CM_EJ_TX *t, const CM_EJ_ASK *ask, ULONG unitId,
               BOOLEAN nvme, ULONG nowMs)
{
    cmEjZero((PUCHAR)t, (ULONG)sizeof(*t));
    t->Phase = CM_EJ_P_IDENT;
    t->Nonce = ask->Nonce;
    t->Bridge = ask->Member;
    t->Unit = ask->Unit;
    t->UnitId = unitId;
    t->Nvme = nvme ? 1UL : 0UL;
    t->QueryCr = CM_EJ_NO_CR;
    t->RemoveCr = CM_EJ_NO_CR;
    t->Entry = CM_EJ_NONE;
    t->Step = CM_EJ_STEP_ACCEPTED;
    t->StampAt = nowMs;
    t->Flags = CM_EJ_F_DIRTY;
}

/* The branches of R11 O5 once the answer (or its end) is known. */
static VOID cmEjBranch(CM_EJ_TX *t, const CM_EJ_IN *in)
{
    if (t->Answer == CM_EJ_ANS_CLEAN) {
        t->Flags |= CM_EJ_F_AUTH;
        t->Outcome = CM_EJ_OUT_CLEAN;
        t->Phase = CM_EJ_P_REMOVE;
    } else if (t->Answer == CM_EJ_ANS_ISOLATED) {
        t->Flags |= CM_EJ_F_AUTH;
        t->Outcome = CM_EJ_OUT_NOT_CLEAN;
        t->Phase = CM_EJ_P_REMOVE;
    } else if (in->U4Known && in->Qualified) {
        /* FAILED, none or late: isolation first, which only a teardown
         * read back closed, its statement and that statement's wait
         * make. */
        t->Flags |= CM_EJ_F_ISOLATE;
        t->Phase = CM_EJ_P_POPULATION;
        cmEjStepTo(t, CM_EJ_STEP_ISOLATING);
    } else {
        cmEjHold(t, in->NowMs);
    }
}

/* After a removal that succeeded and its request cleared. */
static VOID cmEjAfterRemoval(CM_EJ_TX *t, const CM_EJ_IN *in)
{
    if ((t->Flags & CM_EJ_F_BLOCKED) != 0) {
        /* O6's late success: retired, the block lifted; never clean, and
         * the terminal step already written stays. */
        t->Phase = CM_EJ_P_DONE;
        return;
    }
    if (t->Outcome == CM_EJ_OUT_NONE) {
        t->Outcome = CM_EJ_OUT_GONE;
    }
    cmEjTerminal(t, CM_EJ_STEP_REMOVED, t->Outcome, in->NowMs);
    if (t->Outcome == CM_EJ_OUT_NOT_CLEAN) {
        t->Phase = CM_EJ_P_REENUM;
    } else if ((t->Outcome == CM_EJ_OUT_CLEAN ||
                t->Outcome == CM_EJ_OUT_PLAIN) &&
               in->U4Known && (t->Flags & CM_EJ_F_TORN) == 0) {
        /* O5: the path's teardown after a clean removal, the population
         * check before it. */
        t->Flags |= CM_EJ_F_POST;
        t->Phase = CM_EJ_P_POPULATION;
    } else {
        t->Phase = CM_EJ_P_DONE;
    }
}

/* Where a teardown leads, by why it was made. */
static VOID cmEjAfterTeardown(CM_EJ_TX *t, const CM_EJ_IN *in, BOOLEAN made,
                              BOOLEAN closed)
{
    if ((t->Flags & CM_EJ_F_ISOLATE) != 0) {
        /* The isolation is the teardown read back closed and its
         * statement: without an entry there is no statement to make. */
        if (made && closed && t->Entry != CM_EJ_NONE) {
            t->Phase = CM_EJ_P_MARK;
        } else {
            cmEjHold(t, in->NowMs);
        }
    } else if ((t->Flags & CM_EJ_F_POST) != 0) {
        /* O5: DOWN_QUIESCED for the generation, no wait (no instance is
         * left), on a system qualified for the statement, and only after
         * a teardown read back closed. */
        if (made && closed && in->Qualified && t->Entry != CM_EJ_NONE) {
            t->Phase = CM_EJ_P_MARK;
        } else {
            t->Flags &= ~CM_EJ_F_POST;
            t->Phase = CM_EJ_P_DONE;
        }
    } else {
        /* O6's teardown after the deadline: DOWN_QUIESCED releases what
         * the terminal instance holds, the retries go on. */
        t->Flags &= ~CM_EJ_F_O6TEAR;
        if (made && closed && in->Qualified && t->Entry != CM_EJ_NONE) {
            t->Phase = CM_EJ_P_MARK;
            t->Flags |= CM_EJ_F_O6TEAR;
        } else {
            t->Phase = CM_EJ_P_REMOVE;
        }
    }
}

/* The unit's function left after its query was accepted: its link-down
 * reset is the isolation (A1), and the removal retires the devnode. */
static VOID cmEjVanish(CM_EJ_TX *t, const CM_EJ_IN *in)
{
    if (!in->Vanished || (t->Flags & CM_EJ_F_QUERIED) == 0 ||
        (t->Flags & (CM_EJ_F_REMOVED | CM_EJ_F_VANISHED)) != 0) {
        return;
    }
    t->Flags |= CM_EJ_F_VANISHED | CM_EJ_F_AUTH;
    t->Flags &= ~(CM_EJ_F_ISOLATE | CM_EJ_F_POST | CM_EJ_F_O6TEAR);
    if (t->Phase == CM_EJ_P_REQUEST || t->Phase == CM_EJ_P_ANSWER ||
        t->Phase == CM_EJ_P_POPULATION || t->Phase == CM_EJ_P_TEARDOWN ||
        t->Phase == CM_EJ_P_MARK || t->Phase == CM_EJ_P_ACK ||
        t->Phase == CM_EJ_P_HELD) {
        if (t->Outcome != CM_EJ_OUT_CLEAN &&
            t->Outcome != CM_EJ_OUT_NOT_CLEAN &&
            t->Outcome != CM_EJ_OUT_PLAIN) {
            t->Outcome = CM_EJ_OUT_GONE;
        }
        t->Phase = CM_EJ_P_REMOVE;
    }
}

ULONG CmEjNext(CM_EJ_TX *t, const CM_EJ_IN *in)
{
    ULONG guard;

    for (guard = 0; guard < 16; guard++) {
        if ((t->Flags & CM_EJ_F_ADOPT) != 0) {
            /* Nothing before the devnode is found again; a call of the
             * stopped start still outstanding comes back first (its late
             * taker feeds it). */
            if (t->Pending != 0) {
                return CM_EJ_DO_NONE;
            }
            if ((t->Flags & CM_EJ_F_QUERIED) == 0 &&
                (t->Phase == CM_EJ_P_IDENT || t->Phase == CM_EJ_P_QUERY)) {
                /* No query was accepted (none made, or refused): nothing
                 * is owed, and no lookup is needed. */
                t->Flags &= ~CM_EJ_F_ADOPT;
                t->Phase = CM_EJ_P_DONE;
                return CM_EJ_DO_NONE;
            }
            if (!in->SlotIdle ||
                (t->AdoptAt != 0 &&
                 in->NowMs - t->AdoptAt < CM_NVLINK_RETRY_MS)) {
                return CM_EJ_DO_NONE;
            }
            return CM_EJ_DO_ADOPT;
        }
        cmEjVanish(t, in);
        switch (t->Phase) {
        case CM_EJ_P_IDENT:
            if (t->Pending != 0 || !in->SlotIdle) {
                return CM_EJ_DO_NONE;
            }
            return CM_EJ_DO_FIND;
        case CM_EJ_P_QUERY:
            if (t->Pending != 0 || !in->SlotIdle) {
                return CM_EJ_DO_NONE;
            }
            return CM_EJ_DO_QUERY;
        case CM_EJ_P_REQUEST:
            return CM_EJ_DO_REQUEST;
        case CM_EJ_P_ANSWER:
            if ((t->Flags & CM_EJ_F_NOREQ) != 0) {
                /* An NVMe function with an UP entry whose request could
                 * not be published: no acknowledgement can come, so the
                 * branch of none (R11 O5: isolation first, or the
                 * block). */
                t->Answer = CM_EJ_ANS_NONE;
                cmEjStepTo(t, CM_EJ_STEP_ANSWERED);
                cmEjBranch(t, in);
                continue;
            }
            if (in->Answer == 0) {
                return CM_EJ_DO_NONE;
            }
            t->Answer = in->Answer;
            cmEjStepTo(t, CM_EJ_STEP_ANSWERED);
            cmEjBranch(t, in);
            continue;
        case CM_EJ_P_POPULATION:
            if (t->Pending != 0 || !in->SlotIdle) {
                return CM_EJ_DO_NONE;
            }
            return CM_EJ_DO_POPULATION;
        case CM_EJ_P_TEARDOWN:
            if (in->Nested) {
                return CM_EJ_DO_NONE;
            }
            return CM_EJ_DO_TEARDOWN;
        case CM_EJ_P_MARK:
            if (in->Nested) {
                return CM_EJ_DO_NONE;
            }
            return CM_EJ_DO_MARK;
        case CM_EJ_P_ACK:
            if (in->AckHeld) {
                return CM_EJ_DO_NONE;
            }
            /* Acknowledged, timed out or no receiver: the isolation the
             * teardown proved stands either way, and authorises the
             * removal (a detached worker may complete it). */
            t->Flags |= CM_EJ_F_AUTH;
            t->Outcome = CM_EJ_OUT_ISOLATED;
            t->Phase = CM_EJ_P_REMOVE;
            continue;
        case CM_EJ_P_REMOVE:
            /* R11 O6: the deadline is the clock's, a call still running
             * or not. */
            cmEjO6(t, in->NowMs);
            if (t->Pending != 0 || !in->SlotIdle) {
                return CM_EJ_DO_NONE;
            }
            if ((t->Flags & CM_EJ_F_O6TEAR) != 0 &&
                (t->Flags & CM_EJ_F_TORN) == 0 && in->U4Known) {
                t->Phase = CM_EJ_P_POPULATION;
                continue;
            }
            if (t->Attempts == 0 ||
                in->NowMs - t->LastRemoveAt >= CM_NVLINK_RETRY_MS) {
                cmEjStepTo(t, CM_EJ_STEP_REMOVING);
                return CM_EJ_DO_REMOVE;
            }
            return CM_EJ_DO_NONE;
        case CM_EJ_P_WITHDRAW:
            return CM_EJ_DO_WITHDRAW;
        case CM_EJ_P_REENUM:
            if (in->Nested) {
                return CM_EJ_DO_NONE;
            }
            return CM_EJ_DO_REENUM;
        default:
            /* FREE, HELD (waiting for the unplug), DONE. */
            return CM_EJ_DO_NONE;
        }
    }
    return CM_EJ_DO_NONE;
}

VOID CmEjSubmitted(CM_EJ_TX *t, ULONG what, BOOLEAN ok, ULONG nowMs)
{
    if (!ok) {
        return;
    }
    t->Pending = what;
    if (what == CM_EJ_DO_REMOVE) {
        t->Attempts++;
        if (t->Attempts == 1) {
            t->FirstRemoveAt = nowMs;
        }
        t->LastRemoveAt = nowMs;
        /* A retry is visible progress: the clock field moves. */
        t->Flags |= CM_EJ_F_DIRTY;
    }
}

VOID CmEjFeed(CM_EJ_TX *t, ULONG kind, ULONG value, ULONG aux, ULONG nowMs)
{
    CM_EJ_IN in;

    cmEjZero((PUCHAR)&in, (ULONG)sizeof(in));
    in.NowMs = nowMs;
    switch (kind) {
    case CM_EJ_FED_FIND:
        if (t->Pending != CM_EJ_DO_FIND) {
            return;
        }
        t->Pending = 0;
        if (t->Phase != CM_EJ_P_IDENT) {
            return;
        }
        if (value == 0) {
            /* Not found: nothing was called. */
            t->QueryCr = CM_EJ_CR_NO_SUCH_DEVNODE;
            t->Phase = CM_EJ_P_DONE;
            cmEjTerminal(t, CM_EJ_STEP_REFUSED, CM_EJ_OUT_NONE, nowMs);
            return;
        }
        t->Devnode = value;
        t->Phase = CM_EJ_P_QUERY;
        return;
    case CM_EJ_FED_QUERY:
        if (t->Pending != CM_EJ_DO_QUERY) {
            return;
        }
        t->Pending = 0;
        t->QueryCr = value;
        if (value != CM_EJ_CR_SUCCESS) {
            /* A refusal is final; nothing else is done. */
            t->Phase = CM_EJ_P_DONE;
            cmEjTerminal(t, CM_EJ_STEP_REFUSED, CM_EJ_OUT_NONE, nowMs);
            return;
        }
        t->Flags |= CM_EJ_F_QUERIED;
        cmEjStepTo(t, CM_EJ_STEP_QUERIED);
        t->Phase = CM_EJ_P_REQUEST;
        return;
    case CM_EJ_FED_REQUEST:
        if (t->Phase != CM_EJ_P_REQUEST) {
            return;
        }
        if (value == CM_EJ_REQ_FAILED) {
            /* The unit's function has an UP entry published for it, and
             * the request was not published (its identity changed between
             * the query and now): no removal without an acknowledgement
             * or a proven isolation. */
            t->Flags |= CM_EJ_F_NOREQ;
            t->Phase = CM_EJ_P_ANSWER;
            return;
        }
        if (value == CM_EJ_NONE && t->Nvme) {
            /* An NVMe function the channel no longer names after its
             * query: never a plain removal (stor98cm.h R2, R11 O5). */
            t->Flags |= CM_EJ_F_NOREQ;
            t->Phase = CM_EJ_P_ANSWER;
            return;
        }
        if (value == CM_EJ_NONE) {
            /* No request: not an NVMe function - a dock's switch port;
             * the query authorises the removal, as record 24 section 7.1
             * made it, Windows' own removal stopping its driver. */
            t->Flags |= CM_EJ_F_AUTH;
            t->Outcome = CM_EJ_OUT_PLAIN;
            t->Phase = CM_EJ_P_REMOVE;
            return;
        }
        t->Entry = value;
        t->Sequence = aux & 0x00FFFFFFUL;
        /* T_ack from before the request's publication (R11): the caller
         * read `nowMs` before it wrote the request. */
        t->ReqAt = nowMs;
        cmEjStepTo(t, CM_EJ_STEP_REQUESTED);
        t->Phase = CM_EJ_P_ANSWER;
        return;
    case CM_EJ_FED_POPULATION:
        if (t->Pending != CM_EJ_DO_POPULATION) {
            return;
        }
        t->Pending = 0;
        t->Population = value;
        if (t->Phase != CM_EJ_P_POPULATION) {
            return;
        }
        if (value == CM_EJ_POP_PASSED) {
            t->Phase = CM_EJ_P_TEARDOWN;
            return;
        }
        /* A live sibling or an incomplete reading: no teardown, ever. */
        if ((t->Flags & CM_EJ_F_O6TEAR) != 0) {
            t->Flags |= CM_EJ_F_TORN;
        }
        in.Qualified = FALSE;
        cmEjAfterTeardown(t, &in, FALSE, FALSE);
        return;
    case CM_EJ_FED_TEARDOWN:
        if (t->Phase != CM_EJ_P_TEARDOWN) {
            return;
        }
        t->Flags |= CM_EJ_F_TORN;
        /* aux carries the qualification the driver read. */
        in.Qualified = (BOOLEAN)(aux != 0);
        cmEjAfterTeardown(t, &in, TRUE, (BOOLEAN)(value != 0));
        return;
    case CM_EJ_FED_MARK:
        if (t->Phase != CM_EJ_P_MARK) {
            return;
        }
        if ((t->Flags & CM_EJ_F_ISOLATE) != 0) {
            t->Flags &= ~CM_EJ_F_ISOLATE;
            if (value != 0) {
                t->Phase = CM_EJ_P_ACK;
            } else {
                cmEjHold(t, nowMs);
            }
        } else if ((t->Flags & CM_EJ_F_POST) != 0) {
            t->Flags &= ~CM_EJ_F_POST;
            t->Phase = CM_EJ_P_DONE;
        } else {
            t->Flags &= ~CM_EJ_F_O6TEAR;
            t->Phase = CM_EJ_P_REMOVE;
        }
        return;
    case CM_EJ_FED_REMOVE:
        if (t->Pending != CM_EJ_DO_REMOVE) {
            return;
        }
        t->Pending = 0;
        t->RemoveCr = value;
        t->Flags |= CM_EJ_F_DIRTY;
        /* R11 O6 read before the result: a result taken after the
         * deadline, whatever it says, counts as a retry's - never as
         * clean. */
        cmEjO6(t, nowMs);
        if (value != CM_EJ_CR_SUCCESS && value != CM_EJ_CR_NO_SUCH_DEVNODE) {
            return;
        }
        /* Success, or the devnode verified gone (its instance id no longer
         * on that handle): retired either way. */
        t->Flags |= CM_EJ_F_REMOVED;
        t->Flags &= ~CM_EJ_F_O6TEAR;
        if (value == CM_EJ_CR_NO_SUCH_DEVNODE &&
            (t->Flags & CM_EJ_F_BLOCKED) == 0 &&
            t->Outcome != CM_EJ_OUT_CLEAN &&
            t->Outcome != CM_EJ_OUT_NOT_CLEAN) {
            t->Outcome = CM_EJ_OUT_GONE;
        }
        if (t->Entry != CM_EJ_NONE && (t->Flags & CM_EJ_F_WITHDRAWN) == 0) {
            t->Phase = CM_EJ_P_WITHDRAW;
            return;
        }
        in.U4Known = (BOOLEAN)(aux != 0);
        cmEjAfterRemoval(t, &in);
        return;
    case CM_EJ_FED_WITHDRAW:
        if (t->Phase != CM_EJ_P_WITHDRAW) {
            return;
        }
        t->Flags |= CM_EJ_F_WITHDRAWN;
        in.U4Known = (BOOLEAN)(aux != 0);
        cmEjAfterRemoval(t, &in);
        return;
    case CM_EJ_FED_REENUM:
        if (t->Phase == CM_EJ_P_REENUM) {
            t->Phase = CM_EJ_P_DONE;
        }
        return;
    case CM_EJ_FED_ADOPT:
        if (t->Pending != CM_EJ_DO_ADOPT) {
            return;
        }
        t->Pending = 0;
        if (value == 0) {
            if (aux != 0) {
                /* Verified gone: retired by its disappearance. */
                t->Flags &= ~CM_EJ_F_ADOPT;
                t->Flags |= CM_EJ_F_REMOVED;
                t->Phase = CM_EJ_P_DONE;
            } else {
                t->AdoptAt = nowMs != 0 ? nowMs : 1;
            }
            return;
        }
        t->Flags &= ~CM_EJ_F_ADOPT;
        t->Devnode = value;
        if ((t->Flags & CM_EJ_F_QUERIED) == 0 &&
            (t->Phase == CM_EJ_P_IDENT || t->Phase == CM_EJ_P_QUERY)) {
            /* No query was made, or it was refused: nothing is owed. */
            t->Phase = CM_EJ_P_DONE;
        }
        return;
    default:
        return;
    }
}

BOOLEAN CmEjGate(const CM_EJ_TX *t)
{
    return (BOOLEAN)(t->Phase != CM_EJ_P_FREE && t->Phase != CM_EJ_P_DONE);
}

BOOLEAN CmEjBusy(const CM_EJ_TX *t)
{
    if ((t->Flags & CM_EJ_F_ADOPT) != 0) {
        return TRUE;
    }
    return (BOOLEAN)(t->Phase != CM_EJ_P_FREE && t->Phase != CM_EJ_P_DONE &&
                     t->Phase != CM_EJ_P_HELD);
}

VOID CmEjResultEncode(CM_EJ_TX *t, UCHAR *out, ULONG nowMs)
{
    ULONG answer;

    cmEjZero(out, CM_EJ_RESULT_BYTES);
    cmEjPut(out, CM_EJ_S_NONCE, t->Nonce);
    cmEjPut(out, CM_EJ_S_ACCEPTED, CM_EJ_ACCEPTED);
    cmEjPut(out, CM_EJ_S_STEP, t->Step);
    cmEjPut(out, CM_EJ_S_QUERY_CR, t->QueryCr);
    cmEjPut(out, CM_EJ_S_REMOVE_CR, t->RemoveCr);
    answer = 0;
    if (t->Entry != CM_EJ_NONE) {
        answer = (t->Sequence << 8) |
                 (t->Answer != 0 ? (t->Answer & 0xFFUL) : CM_EJ_ANS_NONE);
    }
    cmEjPut(out, CM_EJ_S_ANSWER, answer);
    cmEjPut(out, CM_EJ_S_OUTCOME, t->Outcome);
    cmEjPut(out, CM_EJ_S_CLOCK_MS, nowMs);
    t->StampAt = nowMs;
    t->Flags &= ~CM_EJ_F_DIRTY;
}

BOOLEAN CmEjResultDue(const CM_EJ_TX *t, ULONG nowMs)
{
    if (t->Phase == CM_EJ_P_FREE || (t->Flags & CM_EJ_F_CONSUMED) != 0) {
        return FALSE;
    }
    if ((t->Flags & CM_EJ_F_DIRTY) != 0) {
        return TRUE;
    }
    /* A pending wait is re-stamped, so the tool's progress clock moves. */
    return (BOOLEAN)((t->Flags & CM_EJ_F_TERMINAL) == 0 &&
                     nowMs - t->StampAt >= CM_EJ_RESTAMP_MS);
}

BOOLEAN CmEjConsume(CM_EJ_TX *t, BOOLEAN askPresent, ULONG nowMs)
{
    if (t->Phase == CM_EJ_P_FREE ||
        (t->Flags & (CM_EJ_F_TERMINAL | CM_EJ_F_CONSUMED)) !=
            CM_EJ_F_TERMINAL ||
        (t->Flags & CM_EJ_F_DIRTY) != 0) {
        return FALSE;
    }
    if (askPresent && nowMs - t->TerminalAt < CM_EJ_CONSUME_MS) {
        return FALSE;
    }
    t->Flags |= CM_EJ_F_CONSUMED;
    return TRUE;
}

BOOLEAN CmEjSpent(const CM_EJ_TX *t)
{
    return (BOOLEAN)(t->Phase == CM_EJ_P_DONE && t->Pending == 0 &&
                     (t->Flags & CM_EJ_F_CONSUMED) != 0);
}

VOID CmEjAdopt(CM_EJ_TX *t, ULONG nowMs)
{
    (VOID)nowMs;
    if (t->Phase == CM_EJ_P_FREE || t->Phase == CM_EJ_P_DONE) {
        return;
    }
    t->Flags |= CM_EJ_F_ADOPT | CM_EJ_F_CONSUMED;
    t->Flags &= ~CM_EJ_F_DIRTY;
    t->AdoptAt = 0;
    /* A teardown, mark or re-enumeration of the stopped start is not
     * carried over: the new start's port and entries are its own. */
    if (t->Phase == CM_EJ_P_TEARDOWN || t->Phase == CM_EJ_P_MARK ||
        t->Phase == CM_EJ_P_ACK || t->Phase == CM_EJ_P_POPULATION) {
        if ((t->Flags & CM_EJ_F_ISOLATE) != 0 ||
            t->Phase == CM_EJ_P_ACK) {
            /* An isolation in progress, its acknowledgement wait
             * included, is not carried over: the block. */
            t->Flags &= ~CM_EJ_F_ISOLATE;
            t->Flags |= CM_EJ_F_BLOCKED;
            t->Phase = CM_EJ_P_HELD;
        } else if ((t->Flags & CM_EJ_F_POST) != 0) {
            t->Flags &= ~CM_EJ_F_POST;
            t->Phase = CM_EJ_P_DONE;
        } else {
            /* O6's teardown: still owed unless it was made (TORN); the
             * adopting start makes it when it knows the port. */
            t->Phase = CM_EJ_P_REMOVE;
        }
    } else if (t->Phase == CM_EJ_P_REENUM) {
        t->Phase = CM_EJ_P_DONE;
    }
}

BOOLEAN CmEjOwesRemoval(const CM_EJ_TX *t)
{
    return (BOOLEAN)((t->Flags & (CM_EJ_F_AUTH | CM_EJ_F_REMOVED)) ==
                         CM_EJ_F_AUTH &&
                     t->Phase == CM_EJ_P_REMOVE);
}

/* ---- the statement decision ---------------------------------------------- */

VOID CmEjDecideBegin(CM_EJ_DECISION *d, ULONG kind, ULONG member,
                     ULONG bridge, ULONG serial, ULONG nowMs,
                     ULONG budgetMs)
{
    d->State = CM_EJ_D_POSTED;
    d->Kind = kind;
    d->Member = member;
    d->Bridge = bridge;
    d->Serial = serial;
    d->BeganAt = nowMs;
    d->DeadlineAt = nowMs + budgetMs;
    d->Reading = XHCI_TBT_PORT_ERROR;
    d->Emulator = 0;
    d->ReadAt = 0;
    d->DecidedAt = 0;
    d->PathClosed = FALSE;
}

ULONG CmEjDecideLeft(const CM_EJ_DECISION *d, ULONG nowMs)
{
    ULONG used;
    ULONG budget;

    used = nowMs - d->BeganAt;
    budget = d->DeadlineAt - d->BeganAt;
    return used >= budget ? 0 : budget - used;
}

BOOLEAN CmEjDecideTake(CM_EJ_DECISION *d, ULONG serial, ULONG reading,
                       BOOLEAN emulator, ULONG readAt, ULONG nowMs)
{
    if (d->State != CM_EJ_D_POSTED || serial != d->Serial) {
        return FALSE;
    }
    d->State = CM_EJ_D_DECIDED;
    d->ReadAt = readAt;
    d->DecidedAt = nowMs;
    /* Both the read and its taking inside the deadline: a reading taken
     * late is not published late. */
    if (readAt - d->BeganAt >= d->DeadlineAt - d->BeganAt ||
        nowMs - d->BeganAt >= d->DeadlineAt - d->BeganAt) {
        /* Late: logged by the caller, never a reading. */
        d->Reading = XHCI_TBT_PORT_ERROR;
        d->Emulator = 0;
        return TRUE;
    }
    d->Reading = reading;
    d->Emulator = emulator ? 1UL : 0UL;
    return TRUE;
}

BOOLEAN CmEjDecideExpire(CM_EJ_DECISION *d, ULONG nowMs)
{
    if (d->State != CM_EJ_D_POSTED || CmEjDecideLeft(d, nowMs) != 0) {
        return FALSE;
    }
    d->State = CM_EJ_D_DECIDED;
    d->Reading = XHCI_TBT_PORT_ERROR;
    d->Emulator = 0;
    d->ReadAt = nowMs;
    d->DecidedAt = nowMs;
    return TRUE;
}

BOOLEAN CmEjByteWrite(CM_EJ_READ32 read, CM_EJ_WRITE32 write, PVOID context,
                      ULONG bdf, ULONG offset, ULONG width, ULONG value)
{
    ULONG v;
    ULONG shift;
    ULONG back;

    if (width == 4) {
        if ((offset & 3UL) != 0 || !write(context, bdf, offset, value) ||
            !read(context, bdf, offset, &back)) {
            return FALSE;
        }
        return (BOOLEAN)(back == value);
    }
    if (width != 1) {
        return FALSE;
    }
    shift = (offset & 3UL) * 8UL;
    if (!read(context, bdf, offset & ~3UL, &v) || v == 0xFFFFFFFFUL) {
        return FALSE;
    }
    v = (v & ~(0xFFUL << shift)) | ((value & 0xFFUL) << shift);
    if (!write(context, bdf, offset & ~3UL, v) ||
        !read(context, bdf, offset & ~3UL, &back)) {
        return FALSE;
    }
    return (BOOLEAN)(((back >> shift) & 0xFFUL) == (value & 0xFFUL));
}

/* Neither system's gate-G3 reading is in the run record yet (decision V4):
 * a system is admitted here once it is. */
#define CM_EJ_QUALIFIED_98SE    0
#define CM_EJ_QUALIFIED_ME      0

BOOLEAN CmEjQualified9x(BOOLEAN me)
{
    return (BOOLEAN)(me ? CM_EJ_QUALIFIED_ME != 0
                        : CM_EJ_QUALIFIED_98SE != 0);
}
