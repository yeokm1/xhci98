/*
 * tr_res.h - the resource and command numbers of XHCITR.EXE and
 * XHCITR64.EXE, shared by xhcitr.rc and xhcitr.c.
 */

#ifndef TR_RES_H
#define TR_RES_H

#define IDI_APP                 1
#define IDR_MENU                100
#define IDR_ACCEL               101
#define IDD_ABOUT               102
#define IDC_ABOUT_TEXT          1001
#define IDC_ABOUT_ICON          1002
#define IDD_ELEVATE             103
#define IDC_ELEV_TEXT           1003

#define IDM_REFRESH             40001
#define IDM_SAVE                40002
#define IDM_EXIT                40003
#define IDM_COPY                40010
#define IDM_SELALL              40011
#define IDM_V_TB                40020
#define IDM_V_USB               40021
#define IDM_V_WINUSB            40022
#define IDM_V_EMPTY             40023
#define IDM_V_DESC              40024
#define IDM_V_BEHIND            40025
#define IDM_V_AUTO              40026
#define IDM_LOGIN               40027
#define IDM_EJECT               40030
#define IDM_PROPS               40031
#define IDM_ABOUT               40040
#define IDM_OPEN                40050
#define IDM_UNIT0               41000   /* 41000..41063: the tray's units */

#endif /* TR_RES_H */
