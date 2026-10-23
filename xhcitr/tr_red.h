/*
 * tr_red.h - the pure half of XHCITR's red tray icon (design record 25
 * section 7.2.1, revision 18; roadmap-usb4 task 43.57): whether a
 * condition holds that is still present and that the user can put right,
 * which is first, and the tooltip that names it. No Windows call:
 * test\test_trred.c links it under /Za. C89.
 *
 * The one function that decides the colour is tr_red_state; xhcitr.c's
 * tray_red is the one place that applies it.
 */

#ifndef TR_RED_H
#define TR_RED_H

#include "tr_core.h"

/* The conditions, in the order the tooltip ranks them (section 7.2.1
 * item 4). */
#define TR_RED_NONE     0
#define TR_RED_HC       1   /* a controller stopped answering            */
#define TR_RED_WAIT     2   /* a Thunderbolt/USB4 device awaits approval */
#define TR_RED_SLOW3    3   /* a USB 3 device below USB 3 speed          */
#define TR_RED_SLOW2    4   /* a High Speed device behind a Full Speed hub */

/* An xhci98.sys controller as the tray last read its query answer:
 * Failed is XHQ_F_FAILED (src\xhci_qry.h); 0 when it could not be read. */
typedef struct _TR_REDHC {
    int Failed;
    char Name[TR_NAME_CHARS];
} TR_REDHC;

/* One device of 43.27's set, handed over (section 7.2.1 item 2): Kind
 * TR_RED_SLOW3 or TR_RED_SLOW2; any other kind is not counted. */
typedef struct _TR_REDDEV {
    int Kind;
    char Name[TR_NAME_CHARS];
} TR_REDDEV;

typedef struct _TR_RED_IN {
    const TR_SNAP *Snap;        /* the poll's; NULL or not Valid: no NHI  */
    const TR_U32 *Stage;        /* CM_ST_STAGE_* per Snap NHI (Snap->Stage
                                   in the tool), or NULL                  */
    const TR_REDHC *Hc;
    int HcCount;
    const TR_REDDEV *Dev;
    int DevCount;
} TR_RED_IN;

/* Count 0: the usual icon. Otherwise Kind and Index name the first: for
 * TR_RED_HC with Nhi 1 an NHI of Snap (its index in NhiKey), with Nhi 0
 * an entry of Hc; for TR_RED_WAIT an entry of Snap->R; for the two slow
 * kinds an entry of Dev. */
typedef struct _TR_RED {
    int Count;
    int Kind;
    int Index;
    int Nhi;
} TR_RED;

/* The state, from its inputs alone. Each NHI and each controller counts
 * once, however many of its clauses hold; each router entry and each
 * device counts once. */
void tr_red_state(const TR_RED_IN *in, TR_RED *out);

/* The tooltip's text while red: the first condition, with " and N more"
 * when others hold, at most cap - 1 characters (cap is the shell's szTip:
 * 64 below shell32 5.0, 128 from it), "xHCI98 USB Tree: " first when
 * prefix is set. The fixed words and the suffix are kept and the name is
 * cut to the room left, ending "..."; with fewer than 4 characters of
 * room a generic word stands for it. Empty when Count is 0. */
#define TR_RED_PREFIX   "xHCI98 USB Tree: "
void tr_red_tip(const TR_RED_IN *in, const TR_RED *r, int prefix,
                char *out, size_t cap);

#endif /* TR_RED_H */
