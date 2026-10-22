/*
 * inst_ej9x.h - the tools' half of the Windows 98 SE and ME eject asked of
 * xhcicm.sys (design record 21 section 7.2, "The 9x half", part 1; record
 * 24 section 7.1, revision 6; record 25 section 6.2 step 6; roadmap-usb4.md
 * task 43.18). XHCIINST.EXE (/eject) and XHCITR.EXE (Safely Remove) both
 * use it: the ask's bytes, the query device's freshness token, the reply
 * and the result read back, the wait's clocks, and what each answer means
 * for the exit code and the text. Pure: no Windows call; the host suite is
 * test\test_ej9x.c. The layouts are src\cm\cm_ejask.h's.
 */

#ifndef INST_EJ9X_H
#define INST_EJ9X_H

#include "inst_core.h"

/* The query device's answer, as far as an ask needs it: the start epoch
 * and the content generation (record 25 section 4.3), and whether the
 * answer is one an eject may be made under (stage active, liveness active,
 * not inert, not stopping). 1 when the header checks. */
typedef struct _INST_EJ_TOKEN {
    INST_U32 EpochLo;
    INST_U32 EpochHi;
    INST_U32 ContentGen;
    int Eligible;
} INST_EJ_TOKEN;

int inst_ej_token(const unsigned char *answer, unsigned long bytes,
                  INST_EJ_TOKEN *tok);
/* Two tokens of the same start and content: the unit was chosen under a
 * topology the driver still publishes. */
int inst_ej_same_token(const INST_EJ_TOKEN *a, const INST_EJ_TOKEN *b);

/* A location word: bus 15:8, device 7:3, function 2:0. */
INST_U32 inst_ej_loc(INST_U32 bus, INST_U32 dev, INST_U32 fn);
/* A unit's location from its 98 instance id: the BUS_&DEV_&FUNC_ form
 * whole, the path form's leaf devfn with bus 0 ("the member's secondary
 * bus", which the driver reads); INST_EJ_NO_LOC when neither parses. */
#define INST_EJ_NO_LOC 0xFFFFFFFFUL
INST_U32 inst_ej_unit_loc(const char *id);

/* The ask's CM_EJ_ASK_BYTES. */
void inst_ej_ask(unsigned char *out, INST_U32 nonce, INST_U32 member,
                 INST_U32 unit, const INST_EJ_TOKEN *tok);

/* A nonzero nonce that differs from `last`, from a seed the caller varies
 * (the tick count). */
INST_U32 inst_ej_nonce(INST_U32 seed, INST_U32 last);

/* The result read back: 1 when it is CM_EJ_RESULT_BYTES long, names
 * `nonce` and carries the acceptance word. */
typedef struct _INST_EJ_RESULT {
    INST_U32 Step;
    INST_U32 QueryCr;
    INST_U32 RemoveCr;
    INST_U32 Answer;
    INST_U32 Outcome;
    INST_U32 Clock;
} INST_EJ_RESULT;

int inst_ej_result(const unsigned char *b, unsigned long bytes,
                   INST_U32 nonce, INST_EJ_RESULT *r);
/* The reply read back: its reason (CM_EJ_WHY_*) when it is
 * CM_EJ_REPLY_BYTES long and names `nonce`, else 0. */
INST_U32 inst_ej_reply(const unsigned char *b, unsigned long bytes,
                       INST_U32 nonce);
/* Whether a step is terminal: refused, removed, blocked. */
int inst_ej_terminal(INST_U32 step);

/*
 * The wait, polled every CM_EJ_POLL_MS: inst_ej_wait_init at the ask's
 * write, then inst_ej_wait_step at each poll with what was read (`reason`
 * the reply's, 0 for none; `res` NULL for no result naming the nonce).
 */
#define INST_EJ_W_WAIT          0   /* poll again                            */
#define INST_EJ_W_DELETE        1   /* delete the ask now, then poll again   */
#define INST_EJ_W_REJECTED      2   /* a reply: nothing was called           */
#define INST_EJ_W_TERMINAL      3   /* a terminal step                       */
#define INST_EJ_W_UNCERTAIN     4   /* no answer, or no end within the
                                       budget, or no progress                */

typedef struct _INST_EJ_WAIT {
    int State;              /* 0 asked, 1 deleted unanswered, 2 accepted  */
    INST_U32 AskedAt;
    INST_U32 DeletedAt;
    INST_U32 AcceptedAt;
    INST_U32 Clock;         /* the result's clock field last seen          */
    INST_U32 ClockAt;       /* ... and when it last moved                  */
} INST_EJ_WAIT;

void inst_ej_wait_init(INST_EJ_WAIT *w, INST_U32 now);
int inst_ej_wait_step(INST_EJ_WAIT *w, INST_U32 now, INST_U32 reason,
                      const INST_EJ_RESULT *res);

/*
 * What an end means. A rejection (the reply's reason): exit 1, "No changes
 * were made", the reason as text. A terminal result: REFUSED by
 * inst_eject9x_query's table on its CONFIGRET; REMOVED is an ejection
 * (INST_EJ_DONE) whose *notClean says the drive did not confirm a clean
 * stop (not clean, after isolation); BLOCKED is INST_EJ_FAILED with the
 * text for its cause. The uncertain end has its own text.
 */
int inst_ej_rejected(INST_U32 reason, const char **text);
int inst_ej_outcome(const INST_EJ_RESULT *r, const char **text,
                    int *notClean);
const char *inst_ej_uncertain_text(void);
/* The line a not-clean removal adds to the assessment. */
const char *inst_ej_not_clean_text(void);

#endif /* INST_EJ9X_H */
