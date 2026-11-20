/*
 * tr_rst.c - the USB Tree's restart of a controller, the pure half
 * (tr_rst.h; roadmap-usb4 task 50.32, design record 25 section 27,
 * stages 1 and 2). No Windows call. Every string is a DRAFT for the owner.
 */

#include <stdlib.h>
#include <string.h>
#include "tr_rst.h"
#include "../xhciinst/inst_core.h"
#include "../src/xhci_qry.h"

/* ---- the start's evidence ------------------------------------------- */

void tr_rst_hist_init(TR_RST_HIST *h, const TR_XHR *base)
{
    memset(h, 0, sizeof(*h));
    if (base != NULL && tr_xhr_life_valid(base)) {
        h->BaseValid = 1;
        h->Base = *base;
    }
}

void tr_rst_hist_add(TR_RST_HIST *h, const TR_XHR *x)
{
    h->Samples++;
    if (x == NULL) {
        h->Failed++;
        return;
    }
    if (!x->LifeOk) {
        h->NoPart++;
        return;
    }
    if (!tr_xhr_life_valid(x)) {
        /* Number 0: no start has begun on this FDO; not an attempt. */
        return;
    }
    h->Valid++;
    if (h->BaseValid && !tr_xhr_life_same(&h->Base, x)) {
        h->Differed = 1;
        if ((x->Flags & XHR_F_STARTED) != 0) {
            h->SeenCompleted = 1;
        }
    }
}

int tr_rst_start(const TR_RST_HIST *h)
{
    if (!h->BaseValid) {
        return TR_RST_S4;
    }
    if (h->SeenCompleted) {
        return TR_RST_S1;
    }
    if (h->Differed) {
        return TR_RST_S2;
    }
    if (h->Valid > 0) {
        return TR_RST_S3;
    }
    return TR_RST_S4;
}

/* ---- the answer ----------------------------------------------------- */

static void line(TR_TEXT *t, const char *s)
{
    tr_put(t, s);
    tr_put(t, "\n");
}

/* 1 when line 4 says nothing at all. */
static int rstLine4Empty(const TR_RST_IN *in)
{
    return (in->F == TR_RST_NO || in->F == TR_RST_NOTREAD) &&
           (in->P == TR_RST_NO || in->P == TR_RST_NOTREAD) &&
           in->MarkEnd != TR_RST_YES && in->MarkBase != TR_RST_YES &&
           !(in->MarkEnd == TR_RST_UNKNOWN && in->MarkBase == TR_RST_UNKNOWN);
}

/* 1 when a 9x rescan answered an error (A20) or the root was not located
 * for the second (A22). */
static int rstRescanFailed(const TR_RST_IN *in)
{
    int k;

    for (k = 0; k < in->Rescans && k < 2; k++) {
        if (in->RescanCode[k] != 0) {
            return 1;
        }
    }
    return in->RootFail;
}

int tr_rst_exit(const TR_RST_IN *in)
{
    if (in->Request == TR_RST_REQ_DENIED || in->Request == TR_RST_REQ_NODEV ||
        in->Request == TR_RST_REQ_9X_VETO ||
        in->Request == TR_RST_REQ_9X_QFAIL) {
        return INST_EXIT_REFUSED;
    }
    if (in->Request != TR_RST_REQ_OK && in->Request != TR_RST_REQ_9X_REMOVED) {
        return INST_EXIT_FAILED;
    }
    if (in->Start == TR_RST_S_A11 || rstRescanFailed(in)) {
        return INST_EXIT_FAILED;
    }
    if (in->F == TR_RST_YES || in->P == TR_RST_YES ||
        in->MarkEnd == TR_RST_YES || in->MarkBase == TR_RST_YES) {
        return INST_EXIT_RESTART;
    }
    if (in->Start == TR_RST_S1 && in->Dn == TR_RST_DN_STARTED && in->XhrOk &&
        (in->Xhr.Flags & XHR_F_STARTED) != 0 && in->Xhr.Terminal == 0 &&
        rstLine4Empty(in)) {
        return INST_EXIT_DONE;
    }
    return INST_EXIT_FAILED;
}

void tr_rst_state(const TR_RST_IN *in, TR_TEXT *t)
{
    char b[200];

    switch (in->Dn) {
    case TR_RST_DN_GONE:
        line(t, "Windows does not list it now.");
        break;
    case TR_RST_DN_UNREAD:
        tr_sprintf(b, sizeof(b), "The tool could not read Windows' state of "
                   "it (code 0x%08lX).", (unsigned long)in->DnCode);
        line(t, b);
        break;
    case TR_RST_DN_STOPPED:
        tr_sprintf(b, sizeof(b), "Windows shows it stopped (problem %lu).",
                   (unsigned long)in->Problem);
        line(t, b);
        break;
    default:
        if (!in->XhrOk) {
            line(t, "Windows shows it running; the tool could not read the "
                    "driver's state.");
        } else if ((in->Xhr.Flags & XHR_F_STARTED) == 0) {
            line(t, "Windows shows it started, but the driver reports it is "
                    "not running.");
        } else if (in->Xhr.Terminal == 0) {
            line(t, "It is running.");
        } else if (in->Xhr.Terminal == 1) {
            line(t, "It is running, with a recovery of the controller "
                    "pending.");
        } else if (in->Xhr.Terminal == 2 || in->Xhr.Terminal == 3) {
            line(t, "It is stopped after repeated failures.");
        } else if (in->Xhr.Terminal == 4 || in->Xhr.Terminal == 5) {
            /* The approved status text (section 26 item 2). */
            line(t, "Closed off. This controller was closed off because it "
                    "stopped answering. Restarting it could stop Windows "
                    "responding: save your work and restart Windows.");
        } else if (in->Xhr.Terminal == 6) {
            line(t, "Stopped: its interrupts were lost. Its devices were "
                    "removed. Restart Windows.");
        } else {
            tr_sprintf(b, sizeof(b), "The driver reports a state this tool "
                       "does not know (%lu).",
                       (unsigned long)in->Xhr.Terminal);
            line(t, b);
        }
        break;
    }
}

int tr_rst_answer(const TR_RST_IN *in, TR_TEXT *t)
{
    char b[400];
    const char *name = in->Name != NULL ? in->Name : "the controller";
    int lines23 = 1;

    /* Line 1, the request. */
    switch (in->Request) {
    case TR_RST_REQ_PREP:
        tr_sprintf(b, sizeof(b), "The tool could not prepare the restart "
                   "request (error 0x%08lX); nothing was asked of Windows.",
                   (unsigned long)in->Error);
        line(t, b);
        break;
    case TR_RST_REQ_NODEV:
        tr_rst_r10(b, sizeof(b), name);
        line(t, b);
        lines23 = 0;
        break;
    case TR_RST_REQ_DENIED:
        line(t, TR_RST_R1);
        break;
    case TR_RST_REQ_FAILED:
        tr_sprintf(b, sizeof(b), "Windows did not accept the restart request "
                   "(error 0x%08lX).", (unsigned long)in->Error);
        line(t, b);
        break;
    case TR_RST_REQ_9X_NOPARENT:
        tr_sprintf(b, sizeof(b), "The tool could not find the device %s is "
                   "attached to (code 0x%08lX). Nothing was removed.", name,
                   (unsigned long)in->Error);
        line(t, b);
        break;
    case TR_RST_REQ_9X_VETO:
        tr_sprintf(b, sizeof(b), "Windows refused to remove %s. A program may "
                   "be using one of its devices: close the programs using its "
                   "drives and try again.", name);
        line(t, b);
        break;
    case TR_RST_REQ_9X_QFAIL:
        tr_sprintf(b, sizeof(b), "Windows could not prepare the removal of %s "
                   "(code 0x%08lX). Nothing was removed.", name,
                   (unsigned long)in->Error);
        line(t, b);
        break;
    case TR_RST_REQ_9X_RMFAIL:
        tr_sprintf(b, sizeof(b), "Windows accepted the request but could not "
                   "remove %s (code 0x%08lX).", name, (unsigned long)in->Error);
        line(t, b);
        break;
    case TR_RST_REQ_9X_REMOVED:
        tr_sprintf(b, sizeof(b), "Windows removed %s and looked for it again.",
                   name);
        line(t, b);
        break;
    default:
        tr_sprintf(b, sizeof(b), "Windows accepted the restart request for "
                   "%s.", name);
        line(t, b);
        break;
    }
    if (in->Request == TR_RST_REQ_9X_RMFAIL ||
        in->Request == TR_RST_REQ_9X_REMOVED) {
        int k;
        for (k = 0; k < in->Rescans && k < 2; k++) {
            if (in->RescanCode[k] != 0) {
                tr_sprintf(b, sizeof(b), "Windows' search for the controller "
                           "answered code 0x%08lX.",
                           (unsigned long)in->RescanCode[k]);
                line(t, b);
            }
        }
        if (in->RootFail) {
            tr_sprintf(b, sizeof(b), "The tool could not start a second search "
                       "of the whole computer (code 0x%08lX).",
                       (unsigned long)in->RootCode);
            line(t, b);
        }
    }
    if (lines23 && in->Request != TR_RST_REQ_DENIED &&
        in->Request != TR_RST_REQ_9X_NOPARENT &&
        in->Request != TR_RST_REQ_9X_VETO &&
        in->Request != TR_RST_REQ_9X_QFAIL) {
        /* Line 2, the start: made after a failed request too, which can
         * still have changed the device; not after R1, R10, A10, A19 or
         * A21, which did nothing to it. */
        switch (in->Start) {
        case TR_RST_S_A11:
            /* Removed and not found again: no line 3 follows. */
            line(t, "The controller was removed but Windows did not find it "
                    "again. Use Refresh in Device Manager, or restart "
                    "Windows.");
            lines23 = 0;
            break;
        case TR_RST_S1:
            line(t, "It has started again.");
            break;
        case TR_RST_S2:
            line(t, "A new start of it began, but the tool did not see it "
                    "running.");
            break;
        case TR_RST_S3:
            line(t, "The tool saw no new start of it.");
            break;
        default:
            line(t, "The tool cannot tell whether it was restarted.");
            break;
        }
    }
    if (lines23) {
        tr_rst_state(in, t);
    }
    /* Line 4, a restart of the computer: each that applies. */
    if (in->F == TR_RST_YES) {
        line(t, "Windows asks for the computer to be restarted.");
    } else if (in->F == TR_RST_UNKNOWN) {
        line(t, "The tool could not read whether Windows asks for the "
                "computer to be restarted.");
    }
    if (in->P == TR_RST_YES) {
        line(t, "Windows had already asked for the computer to be restarted "
                "before this request.");
    } else if (in->P == TR_RST_UNKNOWN) {
        line(t, "The tool could not read whether Windows had asked for the "
                "computer to be restarted before this request.");
    }
    if (in->MarkEnd == TR_RST_YES) {
        tr_sprintf(b, sizeof(b), "Windows has marked %s to be finished at the "
                   "next restart of the computer.", name);
        line(t, b);
    }
    if (in->MarkBase == TR_RST_YES) {
        tr_sprintf(b, sizeof(b), "Windows had marked %s to be finished at the "
                   "next restart of the computer before this request.", name);
        line(t, b);
    }
    if (in->MarkEnd == TR_RST_UNKNOWN && in->MarkBase == TR_RST_UNKNOWN) {
        tr_sprintf(b, sizeof(b), "The tool could not read whether Windows has "
                   "marked %s to be finished at the next restart of the "
                   "computer.", name);
        line(t, b);
    }
    /* Line 5, the hint (revision 50; owner, 2026-11-19): a request Windows
     * accepted (A1, 2000 and later only) with no new start seen and
     * Windows asking for a restart (W1) or marking the controller (W5) is
     * what a stop refused in the session reads as. */
    if (in->Request == TR_RST_REQ_OK && in->Start == TR_RST_S3 &&
        (in->F == TR_RST_YES || in->MarkEnd == TR_RST_YES)) {
        line(t, TR_RST_H1);
    }
    return tr_rst_exit(in);
}

int tr_rst_in_balloon(const char *msg)
{
    return msg != NULL && strlen(msg) < TR_RST_BALLOON_CHARS;
}

int tr_rst_refused(const char *text, int mark, const char *name, TR_TEXT *t)
{
    char b[400];

    line(t, text);
    if (mark == TR_RST_YES) {
        tr_sprintf(b, sizeof(b), "Windows has marked %s to be finished at the "
                   "next restart of the computer.",
                   name != NULL ? name : "the controller");
        line(t, b);
    }
    return INST_EXIT_REFUSED;
}

/* ---- the digest ------------------------------------------------------ */

static int rstUp(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

static int rstCmp(const void *a, const void *b)
{
    const char *x = (const char *)a;
    const char *y = (const char *)b;

    for (;; x++, y++) {
        int cx = rstUp((unsigned char)*x);
        int cy = rstUp((unsigned char)*y);
        if (cx != cy) {
            return cx < cy ? -1 : 1;
        }
        if (cx == 0) {
            return 0;
        }
    }
}

void tr_rst_digest(const char *ctrlId, char (*ids)[TR_ID_CHARS], int n,
                   char *out)
{
    char (*all)[TR_ID_CHARS];
    int i;

    all = (char (*)[TR_ID_CHARS])malloc((size_t)(n + 1) * TR_ID_CHARS);
    if (all == NULL) {
        out[0] = '\0';
        return;
    }
    if (n > 0) {
        qsort(ids, (size_t)n, TR_ID_CHARS, rstCmp);
    }
    tr_copy(all[0], ctrlId, TR_ID_CHARS);
    for (i = 0; i < n; i++) {
        memcpy(all[i + 1], ids[i], TR_ID_CHARS);
    }
    tr_res_digest((const char (*)[TR_ID_CHARS])all, n + 1, out);
    free(all);
}

/* ---- the list --------------------------------------------------------- */

/* 1 when n[i] lies below n[top]. */
static int rstBelow(const TR_RST_NODE *n, int i, int top)
{
    int p;
    int guard = 0;

    for (p = n[i].Parent; p >= 0 && guard < 64; p = n[p].Parent) {
        if (p == top) {
            return 1;
        }
        guard++;
    }
    return 0;
}

/* The device n[j] belongs to: the nearest of itself and its ancestors
 * whose parent is a hub node; -1 for none. */
static int rstOwner(const TR_RST_NODE *n, int j)
{
    int g;
    int guard = 0;

    for (g = j; g >= 0 && guard < 64; g = n[g].Parent) {
        if (n[g].Parent >= 0 && n[n[g].Parent].IsHubNode) {
            return g;
        }
        guard++;
    }
    return -1;
}

static int rstEq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (rstUp((unsigned char)*a) != rstUp((unsigned char)*b)) {
            return 0;
        }
    }
    return *a == *b;
}

int tr_rst_list(const TR_RST_NODE *n, int count, int top, int bad, int bound,
                const char *ctrlId, TR_RST_LIST *l, TR_TEXT *t)
{
    int *sub;
    int *own;
    int subN = 0;
    int complete = 1;
    int unsure = bad != 0;
    int foundK = 0;
    int foundM = 0;
    int i;
    int k;

    l->Count = 0;
    l->Lines = 0;
    l->StoppedEmpty = 0;
    l->HasDisk = 0;
    l->Kbd = 0;
    l->Mouse = 0;
    l->KbdElsewhere = -1;
    l->MouseElsewhere = -1;
    l->Digest[0] = '\0';
    if (top < 0 || top >= count) {
        return TR_RST_LIST_INCOMPLETE;
    }
    sub = (int *)malloc((size_t)(count + 1) * sizeof(int));
    own = (int *)malloc((size_t)(count + 1) * sizeof(int));
    if (sub == NULL || own == NULL) {
        free(sub);
        free(own);
        return TR_RST_LIST_INCOMPLETE;
    }
    if (bound || !n[top].ReadOk || n[top].Incomplete) {
        complete = 0;
    }
    for (i = 0; i < count; i++) {
        int in = i != top && rstBelow(n, i, top);
        /* The tree's root has no registry entry of its own on NT: its
         * clear ReadOk is no failure. */
        if ((!n[i].ReadOk && n[i].Parent >= 0) || n[i].Incomplete) {
            if (in) {
                complete = 0;
            } else if (i != top) {
                /* Outside the controller: only C8's inventory is unsure. */
                unsure = 1;
            }
        }
        if (!in) {
            if (i != top && n[i].Started) {
                if (rstEq(n[i].Cls, "Keyboard")) {
                    foundK = 1;
                }
                if (rstEq(n[i].Cls, "Mouse")) {
                    foundM = 1;
                }
            }
            continue;
        }
        sub[subN++] = i;
        if (l->Count >= l->Cap) {
            complete = 0;
        } else {
            tr_copy(l->Ids[l->Count++], n[i].Id != NULL ? n[i].Id : "",
                    TR_ID_CHARS);
        }
        if (rstEq(n[i].Cls, "Keyboard")) {
            l->Kbd = 1;
        }
        if (rstEq(n[i].Cls, "Mouse")) {
            l->Mouse = 1;
        }
        if (n[i].IsDisk) {
            l->HasDisk = 1;
        }
    }
    l->KbdElsewhere = foundK ? 1 : unsure ? -1 : 0;
    l->MouseElsewhere = foundM ? 1 : unsure ? -1 : 0;
    if (!complete) {
        free(sub);
        free(own);
        return TR_RST_LIST_INCOMPLETE;
    }
    for (k = 0; k < subN; k++) {
        own[k] = rstOwner(n, sub[k]);
    }
    /* The lines: each device whose parent is a hub node of this subtree,
     * its group the devnodes it owns (itself and those below it that are
     * not devices themselves). */
    for (k = 0; k < subN; k++) {
        char letters[TR_LETTERS_BYTES];
        int kb = 0;
        int ms = 0;
        int st = 0;
        int m;
        i = sub[k];
        if (own[k] != i) {
            continue;
        }
        letters[0] = '\0';
        for (m = 0; m < subN; m++) {
            int j = sub[m];
            if (own[m] != i) {
                continue;
            }
            if (n[j].IsDisk) {
                tr_letters_add(letters, n[j].Letters != NULL ? n[j].Letters
                                                             : "");
                st = 1;
            }
            if (rstEq(n[j].Cls, "Keyboard")) {
                kb = 1;
            }
            if (rstEq(n[j].Cls, "Mouse")) {
                ms = 1;
            }
        }
        tr_printf(t, "%s", n[i].Name != NULL ? n[i].Name : "");
        if (n[i].Place != NULL && n[i].Place[0] != '\0') {
            tr_printf(t, ", %s", n[i].Place);
        }
        if (letters[0] != '\0') {
            int c;
            tr_put(t, " (");
            for (c = 0; letters[c] != '\0'; c++) {
                tr_printf(t, "%s%c:", c > 0 ? ", " : "", letters[c]);
            }
            tr_put(t, ")");
        }
        if (kb || ms || st) {
            tr_printf(t, " - %s%s%s%s%s", kb ? "keyboard" : "",
                      kb && (ms || st) ? ", " : "", ms ? "mouse" : "",
                      ms && st ? ", " : "", st ? "storage" : "");
        }
        tr_put(t, "\n");
        l->Lines++;
    }
    /* Every devnode below it that no device owns (no hub node above it),
     * but the root hub: listed by name, so that nothing in the digest goes
     * unshown. */
    for (k = 0; k < subN; k++) {
        if (own[k] < 0 && !n[sub[k]].IsRootHub) {
            tr_printf(t, "%s\n", n[sub[k]].Name != NULL ? n[sub[k]].Name : "");
            l->Lines++;
        }
    }
    /* Nothing below it but its root hub: C3. */
    l->StoppedEmpty = 1;
    for (k = 0; k < subN; k++) {
        if (!n[sub[k]].IsRootHub) {
            l->StoppedEmpty = 0;
        }
    }
    free(sub);
    free(own);
    if (t->Overflow) {
        /* A list that does not fit is never shown cut (C2). */
        return TR_RST_LIST_INCOMPLETE;
    }
    tr_rst_digest(ctrlId, l->Ids, l->Count, l->Digest);
    if (l->Digest[0] == '\0') {
        return TR_RST_LIST_INCOMPLETE;
    }
    return TR_RST_LIST_OK;
}

/* ---- the run's outcome ------------------------------------------------ */

int tr_rst_child(int launched, TR_U32 err, int finished, int haveExit,
                 TR_U32 exitCode, int haveAnswer)
{
    if (!launched) {
        /* ERROR_CANCELLED: the prompt or the Run As dialog cancelled. */
        return err == 1223UL ? TR_RST_CH_A14 : TR_RST_CH_A16;
    }
    if (finished && haveAnswer) {
        return TR_RST_CH_ANSWER;
    }
    if (finished && haveExit && exitCode == (TR_U32)INST_EXIT_REFUSED) {
        return TR_RST_CH_A23;
    }
    return TR_RST_CH_A15;
}

int tr_rst_child_exit(int outcome)
{
    switch (outcome) {
    case TR_RST_CH_A14:
    case TR_RST_CH_A23:
        return INST_EXIT_REFUSED;
    default:
        return INST_EXIT_FAILED;
    }
}

/* ---- the strings ----------------------------------------------------- */

void tr_rst_c1(char *out, size_t cap, const char *name)
{
    tr_sprintf(out, cap, "Restart %s?", name);
}

void tr_rst_r9(char *out, size_t cap, const char *name)
{
    tr_sprintf(out, cap, "The devices on %s changed. Nothing was restarted. "
               "Look at the list again.", name);
}

int tr_rst_is_r9(const char *answer, const char *name)
{
    char b[400];

    tr_rst_r9(b, sizeof(b), name);
    return answer != NULL && strncmp(answer, b, strlen(b)) == 0;
}

void tr_rst_r10(char *out, size_t cap, const char *name)
{
    tr_sprintf(out, cap, "%s is no longer present; nothing was restarted.",
               name);
}

void tr_rst_r11(char *out, size_t cap, const char *name)
{
    tr_sprintf(out, cap, "The tool could not read every device on %s, so it "
               "does not restart it. Try again, or restart it from Device "
               "Manager.", name);
}

void tr_rst_a16(char *out, size_t cap, TR_U32 err)
{
    tr_sprintf(out, cap, "The tool could not start as an administrator "
               "(error %lu). Nothing was restarted.", (unsigned long)err);
}

int tr_rst_a17(char *out, size_t cap, const char *id)
{
    tr_sprintf(out, cap, "No xhci98 controller has the instance ID %s.", id);
    return INST_EXIT_FAILED;
}

/* ---- the checks ------------------------------------------------------ */

int tr_rst_check(const TR_RST_CHECK *c)
{
    if (!c->Routines) {
        return TR_RST_NO_ROUTINE;
    }
    if (c->Wow64Limited) {
        return TR_RST_WOW64;
    }
    if (c->RecState == 1 && (c->Terminal == 4 || c->Terminal == 5)) {
        return TR_RST_CONTAINED;
    }
    if (c->RecState != 1 && c->QueryFailed) {
        return TR_RST_UNKNOWN_FAILED;
    }
    if (!c->Admin && !c->Elevates) {
        return TR_RST_NEED_ADMIN;
    }
    if (c->SetupRunning) {
        return TR_RST_SETUP;
    }
    if (!c->WalkComplete) {
        return TR_RST_WALK;
    }
    return TR_RST_OK;
}

/* ---- 98 SE and ME: line 2 (item 4) ------------------------------------ */

int tr_rst_start9x(int removed, int gone, int found, int seenStarted,
                   int unsure, const TR_RST_HIST *h)
{
    if (removed && gone) {
        if (!found) {
            return unsure ? TR_RST_S4 : TR_RST_S_A11;
        }
        if (seenStarted || tr_rst_start(h) == TR_RST_S1) {
            return TR_RST_S1;
        }
        return TR_RST_S2;
    }
    return tr_rst_start(h);
}

/* ---- 98 SE and ME: the sequence (item 3) ------------------------------ */

#define RST_CR_NO_SUCH_DEVNODE  0x0000000DUL
#define RST_CR_REMOVE_VETOED    0x00000017UL

void tr_rst_seq9x(const TR_RST_OPS9X *ops, const char *id, TR_RST_IN *in,
                  TR_RST_HIST *h)
{
    TR_RST_SEEN s;
    TR_U32 dn = 0;
    TR_U32 parent = 0;
    TR_U32 root = 0;
    TR_U32 after = 0;
    TR_U32 cr;
    int gone;

    memset(&s, 0, sizeof(s));
    cr = ops->Locate(ops->Ctx, &dn, id);
    if (cr != 0) {
        in->Error = cr;
        in->Request = cr == RST_CR_NO_SUCH_DEVNODE ? TR_RST_REQ_NODEV
                                                   : TR_RST_REQ_9X_NOPARENT;
        return;
    }
    /* The parent first: the controller's handle is not valid once it is
     * removed. */
    cr = ops->Parent(ops->Ctx, &parent, dn);
    if (cr != 0) {
        in->Error = cr;
        in->Request = TR_RST_REQ_9X_NOPARENT;
        return;
    }
    cr = ops->QueryRemove(ops->Ctx, dn);
    if (cr != 0) {
        in->Error = cr;
        in->Request = cr == RST_CR_REMOVE_VETOED ? TR_RST_REQ_9X_VETO
                                                 : TR_RST_REQ_9X_QFAIL;
        return;
    }
    cr = ops->Remove(ops->Ctx, dn);
    if (cr != 0) {
        /* A12: the parent's rescan alone, once; line 2 from the lifetime
         * samples. */
        in->Error = cr;
        in->Request = TR_RST_REQ_9X_RMFAIL;
        in->RescanCode[0] = ops->Reenumerate(ops->Ctx, parent);
        in->Rescans = 1;
        ops->Settle(ops->Ctx, h, 0, &s);
        in->Start = tr_rst_start9x(0, 0, s.Found, s.Seen, s.Unsure, h);
        return;
    }
    in->Request = TR_RST_REQ_9X_REMOVED;
    gone = ops->Locate(ops->Ctx, &after, id) == RST_CR_NO_SUCH_DEVNODE;
    in->RescanCode[0] = ops->Reenumerate(ops->Ctx, parent);
    in->Rescans = 1;
    ops->Settle(ops->Ctx, h, gone, &s);
    if (!s.Found || in->RescanCode[0] != 0) {
        cr = ops->Locate(ops->Ctx, &root, NULL);
        if (cr != 0) {
            in->RootFail = 1;
            in->RootCode = cr;
        } else {
            in->RescanCode[1] = ops->Reenumerate(ops->Ctx, root);
            in->Rescans = 2;
            ops->Settle(ops->Ctx, h, gone, &s);
        }
    }
    in->Start = tr_rst_start9x(1, gone, s.Found, s.Seen, s.Unsure, h);
}

int tr_rst_offered(int nt, int recState, TR_U32 terminal)
{
    return nt || !(recState == 1 && (terminal == 2 || terminal == 3));
}

static int rstPrefix(const char *s, const char *prefix);

int tr_rst_is_hubnode(const char *id, const char *service, int isHubId)
{
    if (rstPrefix(id, "USB\\XHCI98_ROOT_HUB")) {
        return 1;
    }
    return rstEq(service, "xhci98") &&
           (isHubId || rstPrefix(id, "USB\\XHCI98_HUB"));
}

/* ---- 98 SE and ME: item 7's rules (stage 2) --------------------------- */

/* 1 when s holds word as a field of its own: at its start or after a
 * backslash or an ampersand, and ended by either or by its end (so
 * SubClass_08 is not Class_08); without case. */
static int rstIdField(const char *s, const char *word)
{
    size_t wl = strlen(word);
    const char *p;

    for (p = s; *p != '\0'; p++) {
        size_t k;
        if (p != s && p[-1] != '\\' && p[-1] != '&') {
            continue;
        }
        for (k = 0; k < wl; k++) {
            if (p[k] == '\0' || rstUp((unsigned char)p[k]) !=
                                    rstUp((unsigned char)word[k])) {
                break;
            }
        }
        if (k == wl && (p[wl] == '\0' || p[wl] == '&' || p[wl] == '\\')) {
            return 1;
        }
    }
    return 0;
}

/* 1 when any string of the NUL-separated list of n bytes has the field. */
static int rstIdsField(const char *ids, size_t n, const char *word)
{
    size_t i = 0;

    if (ids == NULL) {
        return 0;
    }
    while (i < n && ids[i] != '\0') {
        size_t len = 0;
        while (i + len < n && ids[i + len] != '\0') {
            len++;
        }
        if (i + len >= n) {
            break;      /* no terminator inside the n bytes */
        }
        if (rstIdField(ids + i, word)) {
            return 1;
        }
        i += len + 1;
    }
    return 0;
}

/* 1 when s begins with the prefix, without case. */
static int rstPrefix(const char *s, const char *prefix)
{
    if (s == NULL) {
        return 0;
    }
    for (; *prefix != '\0'; s++, prefix++) {
        if (rstUp((unsigned char)*s) != rstUp((unsigned char)*prefix)) {
            return 0;
        }
    }
    return 1;
}

/* 1 when the service, up to any extension, is name; without case. */
static int rstService(const char *svc, const char *name)
{
    size_t k;

    if (svc == NULL) {
        return 0;
    }
    for (k = 0; name[k] != '\0'; k++) {
        if (rstUp((unsigned char)svc[k]) != rstUp((unsigned char)name[k])) {
            return 0;
        }
    }
    return svc[k] == '\0' || svc[k] == '.';
}

int tr_rst_node_storage(const TR_RST_NODE *n)
{
    if (rstIdField(n->Id != NULL ? n->Id : "", "Class_08") ||
        rstIdsField(n->Hw, n->HwBytes, "Class_08") ||
        rstIdsField(n->Compat, n->CompatBytes, "Class_08")) {
        return 1;
    }
    if (rstEq(n->Cls, "DiskDrive") || rstEq(n->Cls, "CDROM") ||
        rstEq(n->Cls, "hdc") || rstEq(n->Cls, "SCSIAdapter")) {
        return 1;
    }
    if (rstPrefix(n->Id, "USBSTOR\\") || rstPrefix(n->Id, "SCSI\\")) {
        return 1;
    }
    return rstService(n->Service, "USBSTOR") ||
           rstService(n->Service, "xhciuas") ||
           rstService(n->Service, "USBNTMAP");
}

int tr_rst_cfg_storage(const unsigned char *cfg, size_t n)
{
    size_t i;
    int found = 0;
    unsigned int ifaces = 0;

    if (cfg == NULL || n < 9 || cfg[0] < 9 || cfg[1] != 2 ||
        ((size_t)cfg[2] | ((size_t)cfg[3] << 8)) != n) {
        return -1;
    }
    for (i = 0; i < n; i += cfg[i]) {
        if (n - i < 2 || cfg[i] < 2 || (size_t)cfg[i] > n - i) {
            return -1;
        }
        if (cfg[i + 1] == 4) {
            if (cfg[i] < 9) {
                return -1;
            }
            if (cfg[i + 3] == 0) {
                ifaces++;
            }
            if (cfg[i + 5] == 0x08) {
                found = 1;
            }
        }
    }
    if (ifaces != cfg[4]) {
        return -1;
    }
    return found;
}

/* The drive letter of a path X:\...; 0 where it is not one. *net is set
 * for a path beginning \\ (a network installation). */
static int rstPathLetter(const char *p, int *net)
{
    int c;

    *net = 0;
    if (p == NULL) {
        return 0;
    }
    if (p[0] == '\\' && p[1] == '\\') {
        *net = 1;
        return 0;
    }
    c = rstUp((unsigned char)p[0]);
    if (c >= 'A' && c <= 'Z' && p[1] == ':' && p[2] == '\\') {
        return c;
    }
    return 0;
}

static int rstHasLetter(const char *letters, int L)
{
    if (letters == NULL) {
        return 0;
    }
    for (; *letters != '\0'; letters++) {
        if (rstUp((unsigned char)*letters) == L) {
            return 1;
        }
    }
    return 0;
}

/* R13, naming the drive ("drive C:", or where no one letter is at fault
 * "the Windows drive"). */
static void rstR13(char *why, size_t cap, const char *what)
{
    tr_sprintf(why, cap, "The tool could not tell which controller %s is "
               "on, so it does not restart this controller. Restart Windows "
               "instead.", what);
}

static void rstR16(char *why, size_t cap, const char *name)
{
    tr_sprintf(why, cap, "The tool could not read what is plugged into %s, "
               "so it does not restart it. Try again, or restart Windows.",
               name);
}

/* The drive rule (item 7 steps 1 to 3): 0, 13, 5 or 18, the text in why;
 * *network where a network letter or path exists (for step 4). */
static int rstDrives(const TR_RST_DRIVES *d, const TR_RST_NODE *n, int count,
                     int top, int bad, int bound, const char *name,
                     char *why, size_t cap, int *network)
{
    int winNet;
    int sysNet;
    int win = rstPathLetter(d->WinDir, &winNet);
    int sys = rstPathLetter(d->SysDir, &sysNet);
    int r13 = 0;
    int r5 = 0;
    int r18 = 0;
    int i;
    int L;

    *network = winNet || sysNet;
    /* The faults that are no one letter's: R13 naming no letter. */
    if ((win == 0 && !winNet) || (sys == 0 && !sysNet) || d->Mask == 0 ||
        bound || bad != 0) {
        rstR13(why, cap, "the Windows drive");
        return 13;
    }
    for (i = 0; i < count; i++) {
        if (n[i].Parent < 0) {
            continue;   /* the tree's root has no Enum key of its own */
        }
        if (!n[i].ReadOk || n[i].Incomplete ||
            n[i].LetState == TR_RST_LET_FAILED) {
            rstR13(why, cap, "the Windows drive");
            return 13;
        }
    }
    for (L = 'A'; L <= 'Z'; L++) {
        unsigned int type;
        int claims = 0;
        int claimant = -1;
        if ((d->Mask & (1UL << (L - 'A'))) == 0) {
            continue;
        }
        type = d->Type[L - 'A'];
        if (type == 4) {
            *network = 1;   /* DRIVE_REMOTE */
            continue;
        }
        if (type != 2 && type != 3 && type != 5 && type != 6) {
            if (r13 == 0) {
                r13 = L;    /* DRIVE_UNKNOWN, DRIVE_NO_ROOT_DIR */
            }
            continue;
        }
        for (i = 0; i < count; i++) {
            if (n[i].LetState == TR_RST_LET_READ &&
                rstHasLetter(n[i].DrvLetters, L)) {
                claims++;
                claimant = i;
            }
        }
        if (claims == 0 && (L == 'A' || L == 'B') && type == 2) {
            /* A floppy letter, A: or B: answered DRIVE_REMOVABLE, that no
             * devnode claims: passed over (owner, 2026-11-19, revision
             * 49; the risk accepted is item 7's). */
            continue;
        }
        if (claims != 1) {
            /* Unclaimed (owner, 2026-11-19) or claimed twice. */
            if (r13 == 0) {
                r13 = L;
            }
            continue;
        }
        if (claimant != top && rstBelow(n, claimant, top)) {
            if (L == win || L == sys) {
                if (r5 == 0) {
                    r5 = L;
                }
            } else if (r18 == 0) {
                r18 = L;
            }
        }
    }
    if (r13 != 0) {
        char what[16];
        tr_sprintf(what, sizeof(what), "drive %c:", r13);
        rstR13(why, cap, what);
        return 13;
    }
    if (r5 != 0) {
        tr_sprintf(why, cap, "Windows uses drive %c: on this controller (%s), "
                   "so the controller cannot be restarted while Windows runs. "
                   "Restart Windows instead.", r5,
                   r5 == win ? "the Windows folder" : "the system folder");
        return 5;
    }
    if (r18 != 0) {
        tr_sprintf(why, cap, "Drive %c: is on %s, so the controller is not "
                   "restarted. Unplug the drive's device, and then restart "
                   "the controller.", r18, name);
        return 18;
    }
    return 0;
}

/* Adds one device line to R12's list, once. */
static void rstListAdd(TR_TEXT *t, const char *nm, const char *place)
{
    char line[300];

    if (place != NULL && place[0] != '\0') {
        tr_sprintf(line, sizeof(line), "%s, %s", nm != NULL ? nm : "", place);
    } else {
        tr_sprintf(line, sizeof(line), "%s", nm != NULL ? nm : "");
    }
    if (t->Len > 0 && strstr(t->Buf, line) != NULL) {
        return;
    }
    if (t->Len > 0) {
        tr_put(t, "; ");
    }
    tr_put(t, line);
}

/* The device a devnode of the walk belongs to: its owner (rstOwner), or
 * itself where no hub node is above it. */
static int rstGroup(const TR_RST_NODE *n, int count, int i)
{
    int g;

    if (i < 0 || i >= count) {
        return -1;
    }
    g = rstOwner(n, i);
    return g >= 0 ? g : i;
}

int tr_rst_rules9x(const TR_RST_DRIVES *d, const TR_RST_NODE *n, int count,
                   int top, int bad, int bound, const TR_RST_PORT *p, int np,
                   int pbound, const char *name, char *why, size_t cap,
                   TR_LEAD_FN lead)
{
    char *mark;
    char list[1600];
    char big[2048];
    TR_TEXT t;
    int network = 0;
    int unread = pbound;
    int r;
    int i;

    why[0] = '\0';
    if (top < 0 || top >= count) {
        rstR13(why, cap, "the Windows drive");
        return 13;
    }
    r = rstDrives(d, n, count, top, bad, bound, name, why, cap, &network);
    if (r != 0) {
        return r;
    }
    /* The storage rule: every devnode of the subtree, then every port. */
    mark = (char *)calloc((size_t)count + 1, 1);
    if (mark == NULL) {
        rstR16(why, cap, name);
        return 16;
    }
    tr_text_init(&t, list, sizeof(list));
    for (i = 0; i < count; i++) {
        int g;
        if (i == top || !rstBelow(n, i, top) || !tr_rst_node_storage(&n[i])) {
            continue;
        }
        g = rstGroup(n, count, i);
        if (!mark[g]) {
            mark[g] = 1;
            rstListAdd(&t, n[g].Name, n[g].Place);
        }
    }
    for (i = 0; i < np; i++) {
        int st = 0;
        int g;
        if (!p[i].Read) {
            unread = 1;
            continue;
        }
        if (!p[i].Connected) {
            continue;
        }
        if (p[i].IsHub && !p[i].HubRead) {
            unread = 1;
        }
        if (p[i].Dev == NULL || p[i].Dev[0] != 18 || p[i].Dev[1] != 1) {
            unread = 1;
        } else if (p[i].Dev[4] == 0x08) {
            st = 1;
        }
        switch (tr_rst_cfg_storage(p[i].Cfg, p[i].CfgN)) {
        case 1:
            st = 1;
            break;
        case -1:
            unread = 1;
            break;
        default:
            break;
        }
        if (!st) {
            continue;
        }
        g = rstGroup(n, count, p[i].Node);
        if (g >= 0) {
            if (!mark[g]) {
                mark[g] = 1;
                rstListAdd(&t, n[g].Name, n[g].Place);
            }
        } else {
            rstListAdd(&t, p[i].Name, NULL);
        }
    }
    free(mark);
    if (t.Len > 0) {
        tr_sprintf(big, sizeof(big), "USB storage is connected to %s: %s. "
                   "Unplug it, and then restart the controller.", name, list);
        tr_copy_lead(why, big, cap, lead);
        return 12;
    }
    if (unread) {
        rstR16(why, cap, name);
        return 16;
    }
    /* The network (step 4). */
    if (network) {
        for (i = 0; i < count; i++) {
            if (i != top && rstBelow(n, i, top) && rstEq(n[i].Cls, "Net")) {
                tr_sprintf(why, cap, "A network adapter on %s may be carrying "
                           "a network drive Windows uses, so the controller "
                           "is not restarted. Disconnect the network drives "
                           "first, or restart Windows.", name);
                return 15;
            }
        }
    }
    return 0;
}
