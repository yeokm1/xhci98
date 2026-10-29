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
#define IDD_OTHER               111     /* the other USB drivers (section 6.7) */
/* Record 26 section 5.0 (draft 35): the Change folder dialog. */
#define IDD_FOLDER              112

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
#define IDC_C_FAST              1015   /* was IDC_C_TRAY_T (43.27) */
#define IDC_C_DIR               1016
#define IDC_C_BROWSE            1017
#define IDC_C_DIR_T             1018
#define IDC_C_DIR_WHY           1019
#define IDC_C_DIR_L             1020
#define IDC_C_S98               1021
#define IDC_C_S98_T             1022
#define IDC_C_UAS               1023
#define IDC_C_UAS_T             1024
#define IDC_C_STOR              1025    /* 98 SE: the USB storage drivers */
#define IDC_C_STOR_T            1026
#define IDC_C_VSD               1027    /* S98VSD.VXD, Stor98's child     */
#define IDC_C_RESTAGE           1028    /* Replace the stored build       */
#define IDC_C_DIRLINE           1029    /* the folder chosen, one line    */
#define IDC_C_SETTINGS          1033    /* Settings...                    */
#define IDC_C_FOLDER            1034    /* Change folder...               */
#define IDC_S_PART              1035    /* a tab's next part              */
#define IDC_C_PFW               1036    /* XHCIPFW.VXD or XHCIPFW.SYS     */
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
#define IDC_A_TEXT              1081
#define IDC_A_LABEL             1082
#define IDC_A_CHOICE            1083
#define IDC_A_EDIT              1084
/* The other USB drivers' page (record 26 sections 6.7.4 and 6.7.11): per
 * set (free first, then non-free, IDC_O_STRIDE apart) the copy box, its
 * sentence and the line under the row; then the packages' heading, the
 * place of their scrolling list and the details of the focused one. */
#define IDC_O_BASE              1090
#define IDC_O_STRIDE            5
#define IDC_O_BOX               0
#define IDC_O_TEXT              1
#define IDC_O_LINE              4
#define IDC_O_HEAD              1100
#define IDC_O_AREA              1101
#define IDC_O_DETAIL            1102
/* The packages' list (a child dialog): a box per package of a set, made at
 * run time, IDC_OL_BASE + 100 * set + its index. */
#define IDD_OTHERLIST           113
#define IDC_OL_BASE             1500

/* The settings rows, made at run time: a label, a choice (a combo box, or
 * an edit box for a range), a note, per row. */
#define IDC_ROW_BASE            1200
#define IDC_ROW_STRIDE          4

/* The Summary page's boxes for the Stor98 drives an update touches on
 * 98 SE and ME, made at run time. */
#define IDC_SUM_S98             1300

#endif /* XHCIWIZ_RES_H */
