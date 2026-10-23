/*
 * tr_pop.h - the pure half of XHCITR's 98 SE pop-up, drawn in the place of
 * a balloon where the shell has none (design record 25 section 16,
 * revision 17; roadmap-usb4.md task 43.55): which edge the taskbar is on,
 * where the pop-up stands and how big it is, the queue of notices waiting,
 * and the timeout.
 *
 * Pure: no Windows header, every length an int in the caller's units (the
 * window passes pixels it read from the system and measured with its own
 * font), so the host suite test\test_trpop.c checks it as the window runs
 * it, under /Za. C89. The window half is tr_popw.c.
 */

#ifndef TR_POP_H
#define TR_POP_H

/* The balloon's icon kinds, NIIF_NONE to NIIF_ERROR by number. */
#define TR_POP_NONE         0
#define TR_POP_INFO         1
#define TR_POP_WARNING      2
#define TR_POP_ERROR        3

/* What the shell sends the icon's window about a balloon after
 * NIM_SETVERSION 3 (WM_USER + 2, + 4, + 5), and the pop-up likewise. */
#define TR_POP_NIN_SHOW     0x0402
#define TR_POP_NIN_TIMEOUT  0x0404
#define TR_POP_NIN_CLICK    0x0405

/* The time tray_balloon gives the shell, which is also the shell's least. */
#define TR_POP_TIMEOUT_MS   10000UL

/* The balloon's fields, with their terminators. */
#define TR_POP_TITLE        64
#define TR_POP_TEXT         256

/* The notices that may wait while one shows. */
#define TR_POP_QMAX         8

/* The widest the text wraps at, in average characters. */
#define TR_POP_WRAP_CHARS   40

#define TR_POP_EDGE_BOTTOM  0
#define TR_POP_EDGE_TOP     1
#define TR_POP_EDGE_LEFT    2
#define TR_POP_EDGE_RIGHT   3

/* Right and Bottom are one past the last pixel, as a Windows RECT. */
typedef struct _TR_POP_RECT {
    int Left;
    int Top;
    int Right;
    int Bottom;
} TR_POP_RECT;

/* ---- where ---------------------------------------------------------------- */

/* 1 when bar (NULL none) is a rectangle the edge can be read from: not
 * empty, on the screen at least in part, and touching one of its edges. */
int tr_pop_bar_ok(const TR_POP_RECT *screen, const TR_POP_RECT *bar);

/* The taskbar's edge: from bar where tr_pop_bar_ok, else the side where the
 * work area (NULL none) falls short of the screen most, else the bottom. */
int tr_pop_edge(const TR_POP_RECT *screen, const TR_POP_RECT *work,
                const TR_POP_RECT *bar);

/* The line the pop-up stands on (*line: a y for a top or bottom edge, an x
 * for a left or right one, inside the screen) and the anchor along it the
 * stem points at (*along: an x or a y): the middle of the notification
 * area (notify, NULL none) where its middle lies in the taskbar, else the
 * taskbar's far end less margin, else the work area's. */
void tr_pop_anchor(const TR_POP_RECT *screen, const TR_POP_RECT *work,
                   const TR_POP_RECT *bar, const TR_POP_RECT *notify,
                   int edge, int margin, int *line, int *along);

/* The most room a body has beside the line: *w wide and *h high, after the
 * stem and a margin from the screen's edges; each at least 1. */
void tr_pop_room(const TR_POP_RECT *screen, int edge, int line, int stem,
                 int margin, int *w, int *h);

typedef struct _TR_POP_PLACE {
    TR_POP_RECT Win;        /* the window, screen coordinates            */
    TR_POP_RECT Body;       /* the rounded body, window coordinates      */
    int StemX[3];           /* the stem, window coordinates; [2] its tip */
    int StemY[3];
} TR_POP_PLACE;

/* The window for a body of bodyW x bodyH: beside line on the screen's
 * side, the stem's tip at along, the body reaching away from the screen's
 * near corner, then held inside the screen by margin, the stem between the
 * body's corners of radius. */
void tr_pop_place(const TR_POP_RECT *screen, int edge, int line, int along,
                  int bodyW, int bodyH, int stem, int radius, int margin,
                  TR_POP_PLACE *out);

/* ---- size ----------------------------------------------------------------- */

/* The width the text wraps at: TR_POP_WRAP_CHARS average characters, or
 * the room less the padding on both sides, whichever is less; at least 1. */
int tr_pop_wrap_w(int aveChar, int pad, int roomW);

typedef struct _TR_POP_METRICS {
    int Pad;                /* the padding inside the border             */
    int Icon;               /* the icon's side, 0 for no icon            */
    int Close;              /* the close box's side                      */
    int TitleW;             /* the title's size on one line              */
    int TitleH;
    int TextW;              /* the text's size wrapped at the wrap width */
    int TextH;
} TR_POP_METRICS;

typedef struct _TR_POP_LAYOUT {
    int W;                  /* the body */
    int H;
    TR_POP_RECT Icon;       /* in the body; empty with no icon */
    TR_POP_RECT Title;
    TR_POP_RECT Close;
    TR_POP_RECT Text;       /* empty with no text */
} TR_POP_LAYOUT;

/* The body: a title row (icon, title, close box) over the text, the
 * padding round both; never wider than wrapW plus the padding, the title
 * cut to fit (to nothing where the icon and close box fill the wrap), and
 * never higher than roomH, the text cut to fit and then the title row. */
void tr_pop_layout(const TR_POP_METRICS *m, int wrapW, int roomH,
                   TR_POP_LAYOUT *out);

/* ---- time ----------------------------------------------------------------- */

/* 1 when timeout milliseconds have passed from shown to now, two
 * GetTickCount readings; the wrap at 2^32 is taken by the unsigned
 * difference. */
int tr_pop_expired(unsigned long shown, unsigned long now,
                   unsigned long timeout);

/* ---- the queue ------------------------------------------------------------ */

typedef struct _TR_POP_NOTE {
    char Title[TR_POP_TITLE];
    char Text[TR_POP_TEXT];
    int Icon;               /* TR_POP_NONE to TR_POP_ERROR */
    unsigned int Id;        /* the caller's id, posted with this notice's
                               events (tr_pop_show); 0 from the push */
} TR_POP_NOTE;

typedef struct _TR_POP_QUEUE {
    TR_POP_NOTE Note[TR_POP_QMAX];  /* waiting, oldest first */
    int Count;
} TR_POP_QUEUE;

void tr_popq_init(TR_POP_QUEUE *q);

/* A notice joins the queue at its end, title and text cut to the balloon's
 * fields and an icon kind outside TR_POP_NONE..TR_POP_ERROR taken as
 * TR_POP_INFO. With TR_POP_QMAX waiting, the oldest waiting notice of
 * TR_POP_NONE or TR_POP_INFO is dropped first, else the oldest. Returns 1,
 * or 2 when one was dropped. */
int tr_popq_push(TR_POP_QUEUE *q, const char *title, const char *text,
                 int icon);

/* The oldest waiting notice into *out and out of the queue; 0 when none
 * waits. */
int tr_popq_pop(TR_POP_QUEUE *q, TR_POP_NOTE *out);

#endif
