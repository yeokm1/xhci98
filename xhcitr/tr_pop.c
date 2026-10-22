/*
 * tr_pop.c - the pure half of XHCITR's 98 SE pop-up (design record 25
 * section 16, revision 17; roadmap-usb4.md task 43.55): the taskbar's edge,
 * the pop-up's place and size, its queue and its timeout. No Windows call:
 * test\test_trpop.c links it under /Za. C89. Every routine is called on
 * the tray's thread alone.
 */

#include "tr_pop.h"

static int pop_min(int a, int b)
{
    return a < b ? a : b;
}

static int pop_max(int a, int b)
{
    return a > b ? a : b;
}

/* v held in lo..hi; lo where the range is empty. */
static int pop_clamp(int v, int lo, int hi)
{
    if (v > hi) {
        v = hi;
    }
    if (v < lo) {
        v = lo;
    }
    return v;
}

static void pop_rect(TR_POP_RECT *r, int l, int t, int w, int h)
{
    r->Left = l;
    r->Top = t;
    r->Right = l + w;
    r->Bottom = t + h;
}

/* ---- where ---------------------------------------------------------------- */

int tr_pop_bar_ok(const TR_POP_RECT *screen, const TR_POP_RECT *bar)
{
    if (bar == 0 || bar->Right <= bar->Left || bar->Bottom <= bar->Top) {
        return 0;
    }
    if (bar->Right <= screen->Left || bar->Left >= screen->Right ||
        bar->Bottom <= screen->Top || bar->Top >= screen->Bottom) {
        return 0;
    }
    return bar->Left <= screen->Left || bar->Top <= screen->Top ||
           bar->Right >= screen->Right || bar->Bottom >= screen->Bottom;
}

int tr_pop_edge(const TR_POP_RECT *screen, const TR_POP_RECT *work,
                const TR_POP_RECT *bar)
{
    int edge = TR_POP_EDGE_BOTTOM;
    int most = 0;
    int d;

    if (tr_pop_bar_ok(screen, bar)) {
        /* Sums, not halves: the middles compared without a rounding. */
        if (bar->Right - bar->Left >= bar->Bottom - bar->Top) {
            return bar->Top + bar->Bottom < screen->Top + screen->Bottom ?
                   TR_POP_EDGE_TOP : TR_POP_EDGE_BOTTOM;
        }
        return bar->Left + bar->Right < screen->Left + screen->Right ?
               TR_POP_EDGE_LEFT : TR_POP_EDGE_RIGHT;
    }
    if (work == 0) {
        return TR_POP_EDGE_BOTTOM;
    }
    /* The first of bottom, top, left, right that falls shortest wins. */
    d = screen->Bottom - work->Bottom;
    if (d > most) {
        most = d;
        edge = TR_POP_EDGE_BOTTOM;
    }
    d = work->Top - screen->Top;
    if (d > most) {
        most = d;
        edge = TR_POP_EDGE_TOP;
    }
    d = work->Left - screen->Left;
    if (d > most) {
        most = d;
        edge = TR_POP_EDGE_LEFT;
    }
    d = screen->Right - work->Right;
    if (d > most) {
        edge = TR_POP_EDGE_RIGHT;
    }
    return edge;
}

void tr_pop_anchor(const TR_POP_RECT *screen, const TR_POP_RECT *work,
                   const TR_POP_RECT *bar, const TR_POP_RECT *notify,
                   int edge, int margin, int *line, int *along)
{
    int horiz = edge == TR_POP_EDGE_BOTTOM || edge == TR_POP_EDGE_TOP;
    const TR_POP_RECT *base;
    int haveBar = tr_pop_bar_ok(screen, bar);
    int l;
    int a;

    base = haveBar ? bar : (work != 0 ? work : screen);
    switch (edge) {
    case TR_POP_EDGE_TOP:
        l = haveBar ? bar->Bottom : base->Top;
        break;
    case TR_POP_EDGE_LEFT:
        l = haveBar ? bar->Right : base->Left;
        break;
    case TR_POP_EDGE_RIGHT:
        l = haveBar ? bar->Left : base->Right;
        break;
    default:
        l = haveBar ? bar->Top : base->Bottom;
        break;
    }
    if (horiz) {
        l = pop_clamp(l, screen->Top, screen->Bottom);
        a = pop_min(base->Right, screen->Right) - margin;
    } else {
        l = pop_clamp(l, screen->Left, screen->Right);
        a = pop_min(base->Bottom, screen->Bottom) - margin;
    }
    if (haveBar && notify != 0 && notify->Right > notify->Left &&
        notify->Bottom > notify->Top) {
        int mx = notify->Left + (notify->Right - notify->Left) / 2;
        int my = notify->Top + (notify->Bottom - notify->Top) / 2;
        if (mx >= bar->Left && mx < bar->Right &&
            my >= bar->Top && my < bar->Bottom) {
            a = horiz ? mx : my;
        }
    }
    if (horiz) {
        a = pop_clamp(a, screen->Left, screen->Right - 1);
    } else {
        a = pop_clamp(a, screen->Top, screen->Bottom - 1);
    }
    *line = l;
    *along = a;
}

void tr_pop_room(const TR_POP_RECT *screen, int edge, int line, int stem,
                 int margin, int *w, int *h)
{
    int sw = screen->Right - screen->Left - 2 * margin;
    int sh = screen->Bottom - screen->Top - 2 * margin;

    switch (edge) {
    case TR_POP_EDGE_TOP:
        *w = sw;
        *h = screen->Bottom - line - margin - stem;
        break;
    case TR_POP_EDGE_LEFT:
        *w = screen->Right - line - margin - stem;
        *h = sh;
        break;
    case TR_POP_EDGE_RIGHT:
        *w = line - screen->Left - margin - stem;
        *h = sh;
        break;
    default:
        *w = sw;
        *h = line - screen->Top - margin - stem;
        break;
    }
    *w = pop_max(*w, 1);
    *h = pop_max(*h, 1);
}

void tr_pop_place(const TR_POP_RECT *screen, int edge, int line, int along,
                  int bodyW, int bodyH, int stem, int radius, int margin,
                  TR_POP_PLACE *out)
{
    int horiz = edge == TR_POP_EDGE_BOTTOM || edge == TR_POP_EDGE_TOP;
    int w = horiz ? bodyW : bodyW + stem;
    int h = horiz ? bodyH + stem : bodyH;
    int inset = stem + radius;      /* the tip's distance from the far side */
    int x;
    int y;
    int tip;

    if (horiz) {
        x = along + inset - bodyW;
        x = pop_clamp(x, screen->Left + margin, screen->Right - margin - w);
        y = edge == TR_POP_EDGE_TOP ? line : line - h;
        y = pop_clamp(y, screen->Top, screen->Bottom - h);
        tip = pop_clamp(along - x, radius + stem, bodyW - radius);
        if (edge == TR_POP_EDGE_TOP) {
            pop_rect(&out->Body, 0, stem, bodyW, bodyH);
            out->StemY[0] = stem;
            out->StemY[1] = stem;
            out->StemY[2] = 0;
        } else {
            pop_rect(&out->Body, 0, 0, bodyW, bodyH);
            out->StemY[0] = bodyH;
            out->StemY[1] = bodyH;
            out->StemY[2] = h;
        }
        /* The near side upright, as the shell's stem: base tip-stem..tip. */
        out->StemX[0] = tip - stem;
        out->StemX[1] = tip;
        out->StemX[2] = tip;
    } else {
        y = along + inset - bodyH;
        y = pop_clamp(y, screen->Top + margin, screen->Bottom - margin - h);
        x = edge == TR_POP_EDGE_LEFT ? line : line - w;
        x = pop_clamp(x, screen->Left, screen->Right - w);
        tip = pop_clamp(along - y, radius + stem, bodyH - radius);
        if (edge == TR_POP_EDGE_LEFT) {
            pop_rect(&out->Body, stem, 0, bodyW, bodyH);
            out->StemX[0] = stem;
            out->StemX[1] = stem;
            out->StemX[2] = 0;
        } else {
            pop_rect(&out->Body, 0, 0, bodyW, bodyH);
            out->StemX[0] = bodyW;
            out->StemX[1] = bodyW;
            out->StemX[2] = w;
        }
        out->StemY[0] = tip - stem;
        out->StemY[1] = tip;
        out->StemY[2] = tip;
    }
    pop_rect(&out->Win, x, y, w, h);
}

/* ---- size ----------------------------------------------------------------- */

int tr_pop_wrap_w(int aveChar, int pad, int roomW)
{
    return pop_max(pop_min(TR_POP_WRAP_CHARS * aveChar, roomW - 2 * pad), 1);
}

void tr_pop_layout(const TR_POP_METRICS *m, int wrapW, int roomH,
                   TR_POP_LAYOUT *out)
{
    int pad = m->Pad;
    int gap = pop_max(pad / 2, 1);
    int iconPart = m->Icon > 0 ? m->Icon + gap : 0;
    int rowH = pop_max(pop_max(m->TitleH, m->Icon), m->Close);
    int fixed = iconPart + gap + m->Close;
    int titleW = pop_max(pop_min(m->TitleW, wrapW - fixed), 0);
    int textW = pop_min(m->TextW, wrapW);
    /* Never wider than the wrap: a room too narrow for the icon and the
     * close box loses the title, not the screen's edge. */
    int inner = pop_max(pop_min(pop_max(fixed + titleW, textW), wrapW), 1);
    int textTop = pad + rowH + gap;
    int textH = m->TextH > 0 ? m->TextH : 0;
    int least = pad + rowH + pad;
    int w = inner + 2 * pad;
    int h = textH > 0 ? textTop + textH + pad : least;

    /* Never higher than the room: the text is cut first, then the title
     * row itself (a room lower than one row is a degenerate screen). */
    if (h > roomH) {
        h = pop_max(roomH, 1);
        textH = pop_max(h - textTop - pad, 0);
        if (textH == 0) {
            h = pop_min(least, h);
        }
    }
    out->W = w;
    out->H = h;
    if (m->Icon > 0) {
        pop_rect(&out->Icon, pad, pad + (rowH - m->Icon) / 2, m->Icon, m->Icon);
    } else {
        pop_rect(&out->Icon, pad, pad, 0, 0);
    }
    pop_rect(&out->Title, pad + iconPart, pad + (rowH - m->TitleH) / 2,
             titleW, m->TitleH);
    pop_rect(&out->Close, w - pad - m->Close, pad + (rowH - m->Close) / 2,
             m->Close, m->Close);
    pop_rect(&out->Text, pad, textTop, textH > 0 ? inner : 0, textH);
}

/* ---- time ----------------------------------------------------------------- */

int tr_pop_expired(unsigned long shown, unsigned long now,
                   unsigned long timeout)
{
    return ((now - shown) & 0xFFFFFFFFUL) >= timeout;
}

/* ---- the queue ------------------------------------------------------------ */

static void pop_copy(char *dst, int cap, const char *src)
{
    int i = 0;

    if (src != 0) {
        while (i < cap - 1 && src[i] != '\0') {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

void tr_popq_init(TR_POP_QUEUE *q)
{
    q->Count = 0;
}

static void pop_drop(TR_POP_QUEUE *q, int at)
{
    int i;

    for (i = at; i + 1 < q->Count; i++) {
        q->Note[i] = q->Note[i + 1];
    }
    q->Count--;
}

int tr_popq_push(TR_POP_QUEUE *q, const char *title, const char *text,
                 int icon)
{
    int dropped = 0;
    TR_POP_NOTE *n;

    if (q->Count >= TR_POP_QMAX) {
        int i;
        int at = 0;
        for (i = 0; i < q->Count; i++) {
            if (q->Note[i].Icon == TR_POP_NONE || q->Note[i].Icon == TR_POP_INFO) {
                at = i;
                break;
            }
        }
        pop_drop(q, at);
        dropped = 1;
    }
    n = &q->Note[q->Count];
    pop_copy(n->Title, TR_POP_TITLE, title);
    pop_copy(n->Text, TR_POP_TEXT, text);
    n->Icon = (icon >= TR_POP_NONE && icon <= TR_POP_ERROR) ? icon : TR_POP_INFO;
    q->Count++;
    return dropped ? 2 : 1;
}

int tr_popq_pop(TR_POP_QUEUE *q, TR_POP_NOTE *out)
{
    if (q->Count <= 0) {
        return 0;
    }
    *out = q->Note[0];
    pop_drop(q, 0);
    return 1;
}
