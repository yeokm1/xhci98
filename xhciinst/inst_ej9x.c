/*
 * inst_ej9x.c - the tools' half of the Windows 98 SE and ME eject asked of
 * xhcicm.sys (inst_ej9x.h; roadmap-usb4.md task 43.18; design record 21
 * section 7.2, "The 9x half", part 1). Pure: no Windows call.
 */

#include <stddef.h>
#include "inst_core.h"
#include "inst_ej9x.h"
#include "../src/cm/cm_status.h"
#include "../src/cm/cm_query.h"
#include "../src/cm/cm_ejask.h"

/* cm_live.h's CM_LIVE_ACTIVE (that header carries driver types). */
#define INST_EJ_LIVE_ACTIVE     1UL

static INST_U32 ejGet(const unsigned char *b, unsigned long at)
{
    return (INST_U32)b[at] | ((INST_U32)b[at + 1] << 8) |
           ((INST_U32)b[at + 2] << 16) | ((INST_U32)b[at + 3] << 24);
}

static void ejPut(unsigned char *b, unsigned long at, INST_U32 v)
{
    b[at] = (unsigned char)(v & 0xFFUL);
    b[at + 1] = (unsigned char)((v >> 8) & 0xFFUL);
    b[at + 2] = (unsigned char)((v >> 16) & 0xFFUL);
    b[at + 3] = (unsigned char)((v >> 24) & 0xFFUL);
}

int inst_ej_token(const unsigned char *answer, unsigned long bytes,
                  INST_EJ_TOKEN *tok)
{
    INST_U32 rec;
    INST_U32 version;

    tok->EpochLo = 0;
    tok->EpochHi = 0;
    tok->ContentGen = 0;
    tok->Eligible = 0;
    if (answer == NULL || bytes < CM_Q_HDR_BYTES ||
        ejGet(answer, CM_Q_O_MAGIC) != CM_Q_MAGIC) {
        return 0;
    }
    version = ejGet(answer, CM_Q_O_VERSION);
    /* cm_query.h: a reader of version 2 takes version 1 too; the record
     * is found through CM_Q_O_REC_OFFSET in both. */
    if (version != CM_Q_VERSION && version != CM_Q_VERSION_1) {
        return 0;
    }
    tok->EpochLo = ejGet(answer, CM_Q_O_EPOCH_LO);
    tok->EpochHi = ejGet(answer, CM_Q_O_EPOCH_HI);
    tok->ContentGen = ejGet(answer, CM_Q_O_CONTENT_GEN);
    if ((tok->EpochLo == 0 && tok->EpochHi == 0) || tok->ContentGen == 0) {
        return 0;
    }
    rec = ejGet(answer, CM_Q_O_REC_OFFSET);
    tok->Eligible =
        (ejGet(answer, CM_Q_O_FLAGS) & (CM_Q_F_INERT | CM_Q_F_STOPPING)) == 0 &&
        ejGet(answer, CM_Q_O_LIVE) == INST_EJ_LIVE_ACTIVE &&
        ejGet(answer, CM_Q_O_PASS) != 0 && rec >= CM_Q_HDR_BYTES &&
        rec <= bytes && bytes - rec >= CM_ST_HDR_BYTES &&
        ejGet(answer + rec, CM_ST_O_STAGE) == CM_ST_STAGE_ACTIVE;
    return 1;
}

int inst_ej_same_token(const INST_EJ_TOKEN *a, const INST_EJ_TOKEN *b)
{
    return a->EpochLo == b->EpochLo && a->EpochHi == b->EpochHi &&
           a->ContentGen == b->ContentGen && a->ContentGen != 0;
}

INST_U32 inst_ej_loc(INST_U32 bus, INST_U32 dev, INST_U32 fn)
{
    return ((bus & 0xFFUL) << 8) | ((dev & 0x1FUL) << 3) | (fn & 0x7UL);
}

INST_U32 inst_ej_unit_loc(const char *id)
{
    INST_U32 bdf;
    unsigned char path[8];
    int n;

    if (id == NULL) {
        return INST_EJ_NO_LOC;
    }
    if (inst_id_location(id, &bdf)) {
        return bdf & 0xFFFFUL;
    }
    if (inst_id_path(id, path, &n, (int)sizeof(path)) && n >= 2) {
        return (INST_U32)path[0];
    }
    return INST_EJ_NO_LOC;
}

void inst_ej_ask(unsigned char *out, INST_U32 nonce, INST_U32 member,
                 INST_U32 unit, const INST_EJ_TOKEN *tok)
{
    ejPut(out, CM_EJ_A_NONCE, nonce);
    ejPut(out, CM_EJ_A_MEMBER, member);
    ejPut(out, CM_EJ_A_UNIT, unit);
    ejPut(out, CM_EJ_A_EPOCH_LO, tok->EpochLo);
    ejPut(out, CM_EJ_A_EPOCH_HI, tok->EpochHi);
    ejPut(out, CM_EJ_A_CONTENT_GEN, tok->ContentGen);
    ejPut(out, CM_EJ_A_FLAGS, 0);
}

INST_U32 inst_ej_nonce(INST_U32 seed, INST_U32 last)
{
    INST_U32 n;

    n = (seed * 2654435761UL) ^ 0x45A5C3E1UL;
    if (n == last) {
        n++;
    }
    if (n == 0) {
        n = 1;
        if (n == last) {
            n = 2;
        }
    }
    return n;
}

int inst_ej_result(const unsigned char *b, unsigned long bytes,
                   INST_U32 nonce, INST_EJ_RESULT *r)
{
    if (b == NULL || bytes != CM_EJ_RESULT_BYTES || nonce == 0 ||
        ejGet(b, CM_EJ_S_NONCE) != nonce ||
        ejGet(b, CM_EJ_S_ACCEPTED) != CM_EJ_ACCEPTED) {
        return 0;
    }
    r->Step = ejGet(b, CM_EJ_S_STEP);
    r->QueryCr = ejGet(b, CM_EJ_S_QUERY_CR);
    r->RemoveCr = ejGet(b, CM_EJ_S_REMOVE_CR);
    r->Answer = ejGet(b, CM_EJ_S_ANSWER);
    r->Outcome = ejGet(b, CM_EJ_S_OUTCOME);
    r->Clock = ejGet(b, CM_EJ_S_CLOCK_MS);
    return 1;
}

INST_U32 inst_ej_reply(const unsigned char *b, unsigned long bytes,
                       INST_U32 nonce)
{
    if (b == NULL || bytes != CM_EJ_REPLY_BYTES || nonce == 0 ||
        ejGet(b, CM_EJ_R_NONCE) != nonce) {
        return 0;
    }
    return ejGet(b, CM_EJ_R_REASON);
}

int inst_ej_terminal(INST_U32 step)
{
    return step == CM_EJ_STEP_REFUSED || step == CM_EJ_STEP_REMOVED ||
           step == CM_EJ_STEP_BLOCKED;
}

void inst_ej_wait_init(INST_EJ_WAIT *w, INST_U32 now)
{
    w->State = 0;
    w->AskedAt = now;
    w->DeletedAt = 0;
    w->AcceptedAt = 0;
    w->Clock = 0;
    w->ClockAt = now;
}

int inst_ej_wait_step(INST_EJ_WAIT *w, INST_U32 now, INST_U32 reason,
                      const INST_EJ_RESULT *res)
{
    if (res != NULL) {
        if (w->State != 2) {
            /* Accepted, however late it was seen: every state with the
             * ask's nonce implies acceptance. */
            w->State = 2;
            w->AcceptedAt = now;
            w->Clock = res->Clock;
            w->ClockAt = now;
        } else if (res->Clock != w->Clock) {
            w->Clock = res->Clock;
            w->ClockAt = now;
        }
        if (inst_ej_terminal(res->Step)) {
            return INST_EJ_W_TERMINAL;
        }
        if (now - w->AcceptedAt >= CM_EJ_BUDGET_MS ||
            now - w->ClockAt >= CM_EJ_PROGRESS_MS) {
            return INST_EJ_W_UNCERTAIN;
        }
        return INST_EJ_W_WAIT;
    }
    if (w->State == 2) {
        /* A result seen and then no longer read: no end is known. */
        if (now - w->AcceptedAt >= CM_EJ_BUDGET_MS ||
            now - w->ClockAt >= CM_EJ_PROGRESS_MS) {
            return INST_EJ_W_UNCERTAIN;
        }
        return INST_EJ_W_WAIT;
    }
    if (reason != 0) {
        return INST_EJ_W_REJECTED;
    }
    if (w->State == 0) {
        if (now - w->AskedAt >= CM_EJ_ANSWER_MS) {
            /* Neither answer: the driver may have taken the ask and write
             * late, so the ask goes (a deleted ask is never acted on) and
             * a result is still waited for. */
            w->State = 1;
            w->DeletedAt = now;
            return INST_EJ_W_DELETE;
        }
        return INST_EJ_W_WAIT;
    }
    if (now - w->DeletedAt >= CM_EJ_AFTER_MS) {
        return INST_EJ_W_UNCERTAIN;
    }
    return INST_EJ_W_WAIT;
}

int inst_ej_rejected(INST_U32 reason, const char **text)
{
    switch (reason) {
    case CM_EJ_WHY_EPOCH:
    case CM_EJ_WHY_GENERATION:
        *text = "The device list changed; nothing was ejected.";
        return INST_EJ_CHANGED;
    case CM_EJ_WHY_MEMBER:
    case CM_EJ_WHY_NOT_BELOW:
        *text = "The Thunderbolt driver does not hold that port now; "
                "nothing was ejected.";
        return INST_EJ_CHANGED;
    case CM_EJ_WHY_NOTHING:
        *text = "Nothing answers at that device's place now; nothing was "
                "ejected.";
        return INST_EJ_CHANGED;
    case CM_EJ_WHY_BUSY:
        *text = "The Thunderbolt driver is still busy with an earlier "
                "removal on that port.\nTry again later.";
        return INST_EJ_CHANGED;
    case CM_EJ_WHY_UNAVAILABLE:
        *text = "The Thunderbolt driver cannot remove devices now.";
        return INST_EJ_UNSUPPORTED;
    case CM_EJ_WHY_NO_CHANNEL:
        *text = "The drive cannot be asked to stop cleanly (Stor98 is not "
                "running it), so it is not\nejected from here. Shut down "
                "Windows before you unplug it.";
        return INST_EJ_UNSUPPORTED;
    default:
        *text = "The Thunderbolt driver refused the request.";
        return INST_EJ_CHANGED;
    }
}

int inst_ej_outcome(const INST_EJ_RESULT *r, const char **text,
                    int *notClean)
{
    *notClean = 0;
    if (r->Step == CM_EJ_STEP_REFUSED) {
        return inst_eject9x_query(r->QueryCr, text);
    }
    if (r->Step == CM_EJ_STEP_REMOVED) {
        *text = "";
        *notClean = r->Outcome == CM_EJ_OUT_NOT_CLEAN ||
                    r->Outcome == CM_EJ_OUT_ISOLATED;
        return INST_EJ_DONE;
    }
    if (r->Outcome == CM_EJ_OUT_KEEPS_FAILING) {
        return inst_eject9x_remove(r->RemoveCr != 0 ? r->RemoveCr : 1UL,
                                   text);
    }
    *text = "Blocked: the drive did not stop; shut down Windows before you "
            "unplug it.";
    return INST_EJ_FAILED;
}

const char *inst_ej_uncertain_text(void)
{
    return "The Thunderbolt driver did not answer. Look at the device in the "
           "xHCI98 USB\nTree before you unplug it.";
}

const char *inst_ej_not_clean_text(void)
{
    return "The drive did not confirm a clean stop.";
}
