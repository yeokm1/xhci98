/*
 * tr_red.c - the pure half of XHCITR's red tray icon (tr_red.h; design
 * record 25 section 7.2.1, revision 18). No Windows call; test\test_trred.c
 * links it under /Za. C89.
 */

#include <string.h>
#include "tr_red.h"
#include "../src/cm/cm_status.h"

/* cm_live.h's states, as tr_core.c restates them: that header cannot be
 * included by a tool. */
#define TR_LIVE_SUSPECT          2UL
#define TR_LIVE_ABSENT           3UL
#define TR_LIVE_CONFIG_LOST      4UL
#define TR_LIVE_RECOVERING       5UL
#define TR_LIVE_RECOVERY_FAILED  6UL
#define TR_LIVE_CONTAINED        7UL
#define TR_LIVE_PREPARING        9UL

int tr_red_lost(TR_U32 live)
{
    switch (live) {
    case TR_LIVE_SUSPECT:
    case TR_LIVE_ABSENT:
    case TR_LIVE_CONFIG_LOST:
    case TR_LIVE_RECOVERING:
    case TR_LIVE_RECOVERY_FAILED:
    case TR_LIVE_CONTAINED:
    case TR_LIVE_PREPARING:
        return 1;
    default:
        return 0;
    }
}

static int trRedWaiting(const TR_SROUTER *r)
{
    return r->Kind == CM_ST_KIND_ICM &&
           (r->Approval == CM_ST_APPR_PENDING ||
            r->Approval == CM_ST_APPR_UNANSWERED);
}

static void trRedHold(TR_RED *out, int kind, int index, int nhi)
{
    if (out->Count == 0) {
        out->Kind = kind;
        out->Index = index;
        out->Nhi = nhi;
    }
    out->Count++;
}

void tr_red_state(const TR_RED_IN *in, TR_RED *out)
{
    const TR_SNAP *s = (in->Snap != NULL && in->Snap->Valid) ? in->Snap : NULL;
    int i;
    int kind;

    out->Count = 0;
    out->Kind = TR_RED_NONE;
    out->Index = -1;
    out->Nhi = 0;
    if (s != NULL) {
        for (i = 0; i < s->NhiCount && i < TR_SNAP_NHI; i++) {
            if (tr_red_lost(s->Live[i]) ||
                (in->Stage != NULL && in->Stage[i] == CM_ST_STAGE_FAILED)) {
                trRedHold(out, TR_RED_HC, i, 1);
            }
        }
    }
    for (i = 0; i < in->HcCount; i++) {
        if (in->Hc[i].Failed) {
            trRedHold(out, TR_RED_HC, i, 0);
        }
    }
    if (s != NULL) {
        for (i = 0; i < s->Count && i < TR_SNAP_ROUTERS; i++) {
            if (trRedWaiting(&s->R[i])) {
                trRedHold(out, TR_RED_WAIT, i, 0);
            }
        }
    }
    for (kind = TR_RED_SLOW3; kind <= TR_RED_SLOW2; kind++) {
        for (i = 0; i < in->DevCount; i++) {
            if (in->Dev[i].Kind == kind) {
                trRedHold(out, kind, i, 0);
            }
        }
    }
}

/* What the first condition says, with "%s" where its name goes (none for
 * an NHI), the name, and the word that stands for a name with no room. */
static const char *trRedText(const TR_RED_IN *in, const TR_RED *r,
                             const char **name, const char **generic)
{
    *name = "";
    *generic = "A device";
    switch (r->Kind) {
    case TR_RED_HC:
        if (r->Nhi) {
            return "Thunderbolt/USB4 controller stopped answering";
        }
        if (r->Index >= 0 && r->Index < in->HcCount) {
            *name = in->Hc[r->Index].Name;
        }
        *generic = "A USB controller";
        return "%s stopped answering";
    case TR_RED_WAIT:
        if (in->Snap != NULL && r->Index >= 0 && r->Index < in->Snap->Count) {
            *name = in->Snap->R[r->Index].Name;
        }
        return "%s is waiting to be approved";
    case TR_RED_SLOW3:
    case TR_RED_SLOW2:
        if (r->Index >= 0 && r->Index < in->DevCount) {
            *name = in->Dev[r->Index].Name;
        }
        return r->Kind == TR_RED_SLOW3 ? "%s runs below USB 3 speed"
                                       : "%s is slowed by a USB 1.1 hub";
    default:
        return "";
    }
}

void tr_red_tip(const TR_RED_IN *in, const TR_RED *r, int prefix,
                char *out, size_t cap)
{
    const char *fmt;
    const char *name;
    const char *generic;
    const char *hole;
    char suffix[32];
    char cut[TR_NAME_CHARS];
    char line[256];
    size_t fixed;
    size_t room;
    size_t n;

    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (r->Count <= 0 || r->Kind == TR_RED_NONE) {
        return;
    }
    fmt = trRedText(in, r, &name, &generic);
    suffix[0] = '\0';
    if (r->Count > 1) {
        tr_sprintf(suffix, sizeof(suffix), " and %d more", r->Count - 1);
    }
    hole = strstr(fmt, "%s");
    fixed = strlen(fmt) - (hole != NULL ? 2 : 0) + strlen(suffix) +
            (prefix ? strlen(TR_RED_PREFIX) : 0);
    room = cap - 1 > fixed ? cap - 1 - fixed : 0;
    n = strlen(name);
    if (n >= sizeof(cut)) {
        n = sizeof(cut) - 1;
    }
    if (n == 0) {
        tr_copy(cut, generic, sizeof(cut));
    } else if (n <= room) {
        memcpy(cut, name, n);
        cut[n] = '\0';
    } else if (room >= 4) {
        memcpy(cut, name, room - 3);
        memcpy(cut + room - 3, "...", 4);
    } else {
        tr_copy(cut, generic, sizeof(cut));
    }
    line[0] = '\0';
    if (prefix) {
        tr_copy(line, TR_RED_PREFIX, sizeof(line));
    }
    n = strlen(line);
    if (hole != NULL) {
        tr_sprintf(line + n, sizeof(line) - n, fmt, cut);
    } else {
        tr_copy(line + n, fmt, sizeof(line) - n);
    }
    n = strlen(line);
    tr_copy(line + n, suffix, sizeof(line) - n);
    tr_copy(out, line, cap);
}
