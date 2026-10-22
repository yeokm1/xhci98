/*
 * tr_popw.c - XHCITR's 98 SE pop-up, the window half (design record 25
 * section 16, revision 17; roadmap-usb4.md task 43.55): where shell32.dll
 * is older than 5.0 and so shows no balloons, a small topmost window of its
 * own beside the notification area, in the system's info colours and
 * tooltip font, that never takes the focus, closes after the balloon's
 * timeout or by its close box, shows one notice at a time, and reports a
 * click to the icon's window as the shell reports a balloon's. Placement,
 * size, the queue and the timeout are tr_pop.c's (pure, host-tested).
 *
 * Every routine runs on the tray's thread (PASSIVE, user mode; the window
 * procedure on the same thread's message loop). USER32 and GDI32 calls
 * alone, each present on Windows 98 SE, so the import gate's seven modules
 * are unchanged.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#include "tr_popw.h"

#define POP_CLASS       "Xhci98TrPop"
#define POP_TIMER       1
#define POP_TICK_MS     250

static int popState;            /* 0 not tried, 1 ready, -1 refused */
static HWND popWnd;
static HWND popTray;
static UINT popMsg;
static UINT popId;
static TR_POP_QUEUE popQ;
static TR_POP_NOTE popCur;
static int popShowing;
static DWORD popShown;
static HFONT popFont;
static HFONT popBold;
static HRGN popRgn;             /* the window's shape, window coordinates */
static TR_POP_LAYOUT popLay;
static TR_POP_PLACE popPl;
static int popPress;            /* 0 none, 1 the close box, 2 the body */
static int popRadius;

static LRESULT CALLBACK pop_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp);

int tr_pop_ready(void)
{
    WNDCLASSA wc;

    if (popState != 0) {
        return popState > 0;
    }
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = pop_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = POP_CLASS;
    popState = RegisterClassA(&wc) != 0 ? 1 : -1;
    tr_popq_init(&popQ);
    return popState > 0;
}

static void pop_tell(UINT code)
{
    /* Posted, as the shell's are: the tray's answer to a click may be a
     * message box, and the next notice should not wait behind it. */
    if (popTray != NULL && IsWindow(popTray)) {
        PostMessageA(popTray, popMsg, (WPARAM)popId, (LPARAM)code);
    }
}

static void pop_fonts(void)
{
    NONCLIENTMETRICSA ncm;
    LOGFONTA lf;

    if (popFont != NULL) {
        DeleteObject(popFont);
        popFont = NULL;
    }
    if (popBold != NULL) {
        DeleteObject(popBold);
        popBold = NULL;
    }
    memset(&ncm, 0, sizeof(ncm));
    /* The size before Vista's iPaddedBorderWidth, which 98 SE checks. */
    ncm.cbSize = (UINT)(FIELD_OFFSET(NONCLIENTMETRICSA, lfMessageFont) + sizeof(LOGFONTA));
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0)) {
        lf = ncm.lfStatusFont;
        popFont = CreateFontIndirectA(&lf);
        lf.lfWeight = FW_BOLD;
        popBold = CreateFontIndirectA(&lf);
    }
    if (popFont == NULL || popBold == NULL) {
        if (popFont != NULL) {
            DeleteObject(popFont);
        }
        if (popBold != NULL) {
            DeleteObject(popBold);
        }
        popFont = NULL;
        popBold = NULL;
        if (GetObjectA(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf) != 0) {
            popFont = CreateFontIndirectA(&lf);
            lf.lfWeight = FW_BOLD;
            popBold = CreateFontIndirectA(&lf);
        }
    }
}

static void pop_get(HWND h, TR_POP_RECT *r)
{
    RECT rc;

    if (!GetWindowRect(h, &rc)) {
        rc.left = rc.top = rc.right = rc.bottom = 0;
    }
    r->Left = rc.left;
    r->Top = rc.top;
    r->Right = rc.right;
    r->Bottom = rc.bottom;
}

static HRGN pop_shape(void)
{
    const TR_POP_RECT *b = &popPl.Body;
    int d = 2 * popRadius;
    POINT pt[3];
    HRGN body;
    HRGN stem;
    int i;

    /* A round-rectangle region leaves out its right and bottom edges. */
    body = CreateRoundRectRgn(b->Left, b->Top, b->Right + 1, b->Bottom + 1, d, d);
    if (body == NULL) {
        return NULL;
    }
    for (i = 0; i < 3; i++) {
        pt[i].x = popPl.StemX[i];
        pt[i].y = popPl.StemY[i];
    }
    stem = CreatePolygonRgn(pt, 3, WINDING);
    if (stem != NULL) {
        CombineRgn(body, body, stem, RGN_OR);
        DeleteObject(stem);
    }
    return body;
}

/* Measure, lay out and place the current notice; reshape the window. */
static void pop_place(void)
{
    TR_POP_RECT screen;
    TR_POP_RECT work;
    TR_POP_RECT barR;
    TR_POP_RECT notR;
    TR_POP_RECT *barP = NULL;
    TR_POP_RECT *notP = NULL;
    TR_POP_METRICS m;
    RECT rc;
    HWND bar;
    HWND nfy;
    HDC dc;
    HGDIOBJ old;
    TEXTMETRICA tm;
    int line;
    int along;
    int edge;
    int roomW;
    int roomH;
    int wrapW;
    int lineH;
    int stem;
    int radius;
    int margin;
    HRGN shape;

    screen.Left = 0;
    screen.Top = 0;
    screen.Right = GetSystemMetrics(SM_CXSCREEN);
    screen.Bottom = GetSystemMetrics(SM_CYSCREEN);
    work = screen;
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &rc, 0)) {
        work.Left = rc.left;
        work.Top = rc.top;
        work.Right = rc.right;
        work.Bottom = rc.bottom;
    }
    bar = FindWindowA("Shell_TrayWnd", NULL);
    if (bar != NULL) {
        pop_get(bar, &barR);
        barP = &barR;
        nfy = FindWindowExA(bar, NULL, "TrayNotifyWnd", NULL);
        if (nfy != NULL) {
            pop_get(nfy, &notR);
            notP = &notR;
        }
    }

    pop_fonts();
    dc = GetDC(popWnd);
    old = SelectObject(dc, popFont != NULL ? (HGDIOBJ)popFont : GetStockObject(DEFAULT_GUI_FONT));
    memset(&tm, 0, sizeof(tm));
    GetTextMetricsA(dc, &tm);
    lineH = tm.tmHeight > 0 ? (int)tm.tmHeight : 13;
    stem = lineH;
    radius = lineH / 2 > 3 ? lineH / 2 : 3;
    popRadius = radius;
    memset(&m, 0, sizeof(m));
    m.Pad = lineH / 2 > 2 ? lineH / 2 : 2;
    margin = m.Pad;
    m.Icon = popCur.Icon != TR_POP_NONE ? GetSystemMetrics(SM_CXSMICON) : 0;
    m.Close = lineH;

    edge = tr_pop_edge(&screen, &work, barP);
    tr_pop_anchor(&screen, &work, barP, notP, edge, margin, &line, &along);
    tr_pop_room(&screen, edge, line, stem, margin, &roomW, &roomH);
    wrapW = tr_pop_wrap_w(tm.tmAveCharWidth > 0 ? (int)tm.tmAveCharWidth : 6,
                          m.Pad, roomW);

    if (popBold != NULL) {
        SelectObject(dc, popBold);
    }
    SetRect(&rc, 0, 0, wrapW, 0);
    DrawTextA(dc, popCur.Title, -1, &rc, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    m.TitleW = rc.right - rc.left;
    m.TitleH = rc.bottom - rc.top > 0 ? rc.bottom - rc.top : lineH;
    if (popFont != NULL) {
        SelectObject(dc, popFont);
    }
    if (popCur.Text[0] != '\0') {
        SetRect(&rc, 0, 0, wrapW, 0);
        DrawTextA(dc, popCur.Text, -1, &rc, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        m.TextW = rc.right - rc.left;
        m.TextH = rc.bottom - rc.top;
    }
    SelectObject(dc, old);
    ReleaseDC(popWnd, dc);

    tr_pop_layout(&m, wrapW, roomH, &popLay);
    tr_pop_place(&screen, edge, line, along, popLay.W, popLay.H, stem, radius,
                 margin, &popPl);

    SetWindowPos(popWnd, HWND_TOPMOST, popPl.Win.Left, popPl.Win.Top,
                 popPl.Win.Right - popPl.Win.Left, popPl.Win.Bottom - popPl.Win.Top,
                 SWP_NOACTIVATE);
    if (popRgn != NULL) {
        DeleteObject(popRgn);
    }
    popRgn = pop_shape();
    shape = pop_shape();
    /* The system owns the region given to SetWindowRgn; popRgn is the
     * copy the painting uses. */
    if (shape != NULL && !SetWindowRgn(popWnd, shape, TRUE)) {
        DeleteObject(shape);
    }
    InvalidateRect(popWnd, NULL, TRUE);
}

static void pop_next(void)
{
    if (!tr_popq_pop(&popQ, &popCur)) {
        return;
    }
    if (popWnd == NULL || !IsWindow(popWnd)) {
        popWnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, POP_CLASS, "",
                                 WS_POPUP, 0, 0, 1, 1, popTray, NULL,
                                 GetModuleHandleA(NULL), NULL);
        if (popWnd == NULL) {
            tr_popq_init(&popQ);
            return;
        }
    }
    popPress = 0;
    pop_place();
    ShowWindow(popWnd, SW_SHOWNOACTIVATE);
    SetWindowPos(popWnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    UpdateWindow(popWnd);
    popShowing = 1;
    popShown = GetTickCount();
    SetTimer(popWnd, POP_TIMER, POP_TICK_MS, NULL);
    pop_tell(TR_POP_NIN_SHOW);
}

static void pop_close(UINT code)
{
    if (!popShowing) {
        return;
    }
    popShowing = 0;
    popPress = 0;
    if (GetCapture() == popWnd) {
        ReleaseCapture();
    }
    KillTimer(popWnd, POP_TIMER);
    ShowWindow(popWnd, SW_HIDE);
    pop_tell(code);
    pop_next();
}

void tr_pop_show(HWND tray, UINT msg, UINT id, const char *title,
                 const char *text, int icon)
{
    if (!tr_pop_ready()) {
        return;
    }
    popTray = tray;
    popMsg = msg;
    popId = id;
    tr_popq_push(&popQ, title, text, icon);
    if (!popShowing) {
        pop_next();
    }
}

static int pop_in(const TR_POP_RECT *r, int x, int y)
{
    return x >= r->Left && x < r->Right && y >= r->Top && y < r->Bottom;
}

/* The layout's rectangle r, in the body, as window coordinates. */
static void pop_at(const TR_POP_RECT *r, RECT *out)
{
    out->left = popPl.Body.Left + r->Left;
    out->top = popPl.Body.Top + r->Top;
    out->right = popPl.Body.Left + r->Right;
    out->bottom = popPl.Body.Top + r->Bottom;
}

static void pop_paint(HWND w)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    HBRUSH bk = CreateSolidBrush(GetSysColor(COLOR_INFOBK));
    HBRUSH fg = CreateSolidBrush(GetSysColor(COLOR_INFOTEXT));
    HGDIOBJ old;
    RECT rc;
    LPCSTR name;
    UINT st;

    if (popRgn != NULL && bk != NULL && fg != NULL) {
        FillRgn(dc, popRgn, bk);
        FrameRgn(dc, popRgn, fg, 1, 1);
    }
    if (popLay.Icon.Right > popLay.Icon.Left) {
        name = popCur.Icon == TR_POP_WARNING ? IDI_WARNING :
               popCur.Icon == TR_POP_ERROR ? IDI_ERROR : IDI_INFORMATION;
        pop_at(&popLay.Icon, &rc);
        DrawIconEx(dc, rc.left, rc.top, LoadIcon(NULL, name),
                   rc.right - rc.left, rc.bottom - rc.top, 0, NULL, DI_NORMAL);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_INFOTEXT));
    old = SelectObject(dc, popBold != NULL ? (HGDIOBJ)popBold : GetStockObject(DEFAULT_GUI_FONT));
    pop_at(&popLay.Title, &rc);
    DrawTextA(dc, popCur.Title, -1, &rc,
              DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_VCENTER);
    if (popFont != NULL) {
        SelectObject(dc, popFont);
    }
    if (popLay.Text.Bottom > popLay.Text.Top) {
        pop_at(&popLay.Text, &rc);
        DrawTextA(dc, popCur.Text, -1, &rc, DT_WORDBREAK | DT_NOPREFIX);
    }
    SelectObject(dc, old);
    pop_at(&popLay.Close, &rc);
    st = DFCS_CAPTIONCLOSE | DFCS_FLAT;
    if (popPress == 1) {
        st |= DFCS_PUSHED;
    }
    DrawFrameControl(dc, &rc, DFC_CAPTION, st);
    if (bk != NULL) {
        DeleteObject(bk);
    }
    if (fg != NULL) {
        DeleteObject(fg);
    }
    EndPaint(w, &ps);
}

static int pop_on_close(LPARAM lp)
{
    TR_POP_RECT c = popLay.Close;
    int x = (int)(short)LOWORD(lp) - popPl.Body.Left;
    int y = (int)(short)HIWORD(lp) - popPl.Body.Top;

    return pop_in(&c, x, y);
}

static int pop_on_body(LPARAM lp)
{
    int x = (int)(short)LOWORD(lp);
    int y = (int)(short)HIWORD(lp);

    return popRgn != NULL ? PtInRegion(popRgn, x, y) != 0 : pop_in(&popPl.Body, x, y);
}

static LRESULT CALLBACK pop_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        pop_paint(w);
        return 0;
    case WM_LBUTTONDOWN:
        if (!popShowing) {
            return 0;
        }
        popPress = pop_on_close(lp) ? 1 : 2;
        SetCapture(w);
        if (popPress == 1) {
            InvalidateRect(w, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (popPress == 1) {
            if (pop_on_close(lp)) {
                pop_close(TR_POP_NIN_TIMEOUT);
                return 0;
            }
            InvalidateRect(w, NULL, FALSE);
        } else if (popPress == 2 && pop_on_body(lp) && !pop_on_close(lp)) {
            pop_close(TR_POP_NIN_CLICK);
            return 0;
        }
        popPress = 0;
        if (GetCapture() == w) {
            ReleaseCapture();
        }
        return 0;
    case WM_RBUTTONUP:
        pop_close(TR_POP_NIN_TIMEOUT);
        return 0;
    case WM_CAPTURECHANGED:
        if (popPress != 0) {
            popPress = 0;
            InvalidateRect(w, NULL, FALSE);
        }
        return 0;
    case WM_TIMER:
        if (wp == POP_TIMER && popShowing &&
            tr_pop_expired(popShown, GetTickCount(), TR_POP_TIMEOUT_MS)) {
            pop_close(TR_POP_NIN_TIMEOUT);
        }
        return 0;
    case WM_DISPLAYCHANGE:
        if (popShowing) {
            pop_place();
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (popShowing && wp == SPI_SETWORKAREA) {
            pop_place();
        }
        return 0;
    case WM_DESTROY:
        KillTimer(w, POP_TIMER);
        popShowing = 0;
        popPress = 0;
        tr_popq_init(&popQ);
        if (popRgn != NULL) {
            DeleteObject(popRgn);
            popRgn = NULL;
        }
        if (popFont != NULL) {
            DeleteObject(popFont);
            popFont = NULL;
        }
        if (popBold != NULL) {
            DeleteObject(popBold);
            popBold = NULL;
        }
        popWnd = NULL;
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}
