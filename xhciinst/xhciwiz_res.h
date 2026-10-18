/*
 * xhciwiz_res.h - the resource ids of XHCIWIZ.EXE and XHCIWZ64.EXE, the
 * installer's window (design record 26 sections 4 and 5): the icon, the
 * wizard's page templates and their controls, the Advanced dialog.
 */

#ifndef XHCIWIZ_RES_H
#define XHCIWIZ_RES_H

#define IDI_WIZ                 1

/* The pages, in the owner's order (record 26 section 5), and the
 * uninstall's welcome (section 9.3 step 1). */
#define IDD_WELCOME             101
#define IDD_FOUND               102
#define IDD_COMPONENTS          103
#define IDD_SETTINGS            104
#define IDD_SUMMARY             105
#define IDD_PROGRESS            106
#define IDD_STEPS               107
#define IDD_FINISH              108
#define IDD_UWELCOME            109
#define IDD_ADVANCED            110

/* The controls. */
#define IDC_TITLE               1001
#define IDC_TEXT                1002
#define IDC_SYSTEM              1003
#define IDC_LOG                 1004
#define IDC_STATUS              1005
#define IDC_LIST                1006
#define IDC_DETAIL              1007
#define IDC_C_DRIVERS           1010
#define IDC_C_DRIVERS_T         1011
#define IDC_C_TB                1012
#define IDC_C_TB_T              1013
#define IDC_C_TRAY              1014
#define IDC_C_TRAY_T            1015
#define IDC_C_DIR               1016
#define IDC_C_BROWSE            1017
#define IDC_C_DIR_T             1018
#define IDC_C_DIR_WHY           1019
#define IDC_C_DIR_L             1020
#define IDC_S_HEAD              1030
#define IDC_S_ADVANCED          1031
#define IDC_S_NOTE              1032
#define IDC_PROGRESSBAR         1040
#define IDC_STEPLIST            1041
#define IDC_SAVE                1050
#define IDC_DEVMGR              1051
#define IDC_F_LAST              1060
#define IDC_F_NOW               1061
#define IDC_F_LATER             1062
#define IDC_U_AGREE             1070
#define IDC_A_HEAD              1080

/* The settings rows, made at run time: a label, a choice (a combo box, or
 * an edit box for a range), a note, per row. */
#define IDC_ROW_BASE            1200
#define IDC_ROW_STRIDE          4

#endif /* XHCIWIZ_RES_H */
