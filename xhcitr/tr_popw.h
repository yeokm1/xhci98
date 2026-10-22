/*
 * tr_popw.h - XHCITR's 98 SE pop-up, the window half (design record 25
 * section 16, revision 17; roadmap-usb4.md task 43.55). Include after
 * <windows.h>. Every routine runs on the tray's thread, which owns the
 * window and pumps its messages.
 */

#ifndef TR_POPW_H
#define TR_POPW_H

#include "tr_pop.h"

/* 1 when the pop-up can be shown: its class is registered (at the first
 * call). */
int tr_pop_ready(void);

/* A notice in the balloon's place: shown now, or queued behind the one
 * showing (section 16 item 5). tray is the icon's window, which receives
 * msg with wParam id and lParam TR_POP_NIN_* as the shell sends a
 * balloon's (item 6), and owns the pop-up. icon is TR_POP_NONE to
 * TR_POP_ERROR. */
void tr_pop_show(HWND tray, UINT msg, UINT id, const char *title,
                 const char *text, int icon);

#endif
