/*
 * tr_res.h - the resource and command numbers of XHCITR.EXE and
 * XHCITR64.EXE, shared by xhcitr.rc and xhcitr.c.
 */

#ifndef TR_RES_H
#define TR_RES_H

#define IDI_APP                 1
#define IDI_RED                 2       /* record 25 section 7.2.1 */
#define IDR_MENU                100
#define IDR_ACCEL               101
#define IDD_ABOUT               102
#define IDC_ABOUT_TEXT          1001
#define IDC_ABOUT_ICON          1002
#define IDD_ELEVATE             103
#define IDC_ELEV_TEXT           1003
#define IDD_LEVEL               104
#define IDC_LV_LIST             1004
#define IDC_LV_NOW0             1005    /* IDC_LV_NOW0 + driver             */
#define IDC_LV_GROUP0           1007    /* IDC_LV_GROUP0 + driver           */
#define IDC_LV_R0               1010    /* xhci98: 1010..1014, level 0-4    */
#define IDC_LV_R1               1020    /* xhcicm: 1020..1024               */
#define IDC_LV_NOTE             1030
#define IDD_SETTINGS            105     /* record 25 section 22 (47.35) */
#define IDR_SILENT              106     /* src\silent.reg, RCDATA      */
#define IDC_S_TAB               1101
#define IDC_S_HEAD              1102
#define IDC_S_RESTART           1103
#define IDC_S_SCOPE_L           1104
#define IDC_S_SCOPE             1105
#define IDC_S_LIST              1106
#define IDC_S_ADV               1107
#define IDC_S_LEVEL             1108
#define IDC_S_NOTE              1109
#define IDC_S_CHOICE            1110
#define IDC_S_NUM               1111
#define IDC_S_RANGE             1112
#define IDC_S_DEF               1113
#define IDC_S_HK                1114    /* the quick save's hotkey      */
#define IDC_S_HKKEY             1115
#define IDC_S_HKNOTE            1116
#define IDC_S_SND               1117    /* the quick save's sound       */
#define IDC_S_LOGIN             1118    /* the tool's own tab (owner, 2026-11-07) */
#define IDC_S_WARN              1119
#define IDC_S_FAST              1120
#define IDC_S_FASTWIN           1121
#define IDC_S_STOR              1122    /* the USB drive notice (48.17) */
#define IDC_S_FIND_L            1123    /* the settings search (49.38)  */
#define IDC_S_FIND              1124

#define IDM_REFRESH             40001
#define IDM_SAVE                40002
#define IDM_SNAPSHOT            40004
#define IDM_ACPI                40005   /* Save Firmware Tables (46.21)  */
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
#define IDM_LEVEL               40028
#define IDM_SETTINGS            40032   /* the Settings window (47.35)   */
#define IDM_QUICKSAVE           40033   /* the tray's quick save         */
#define IDM_RMWARN              40029
#define IDM_EJECT               40030
#define IDM_PROPS               40031
#define IDM_ABOUT               40040
#define IDM_OPEN                40050
#define IDM_FAST                40051   /* the notice on or off          */
#define IDM_FAST_SHOW           40052   /* the devices that can run faster */
#define IDM_RED                 40053   /* the tray menu red item */
#define IDM_FAST_WIN            40054   /* Windows' own ports watched too */
#define IDM_UNIT0               41000   /* 41000..41063: the tray's units */

#endif /* TR_RES_H */
