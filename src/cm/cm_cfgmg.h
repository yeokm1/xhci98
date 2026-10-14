/*
 * cm_cfgmg.h - the Windows 98 / ME configuration manager (CONFIGMG.VXD)
 * as the Thunderbolt 3 client calls it from ring 0 through the VMM
 * dynalink: its VxD id, the service numbers, and the flag, status and
 * result values those calls take and return (design record 21 section 6.1,
 * roadmap-usb4.md task 39-0.3; safe eject, task 39-A.8).
 *
 * Values only. Every number below was read from the Windows 98 Second
 * Edition DDK's headers and is cited to the header and line it came from;
 * no declaration, macro body or text of those headers is reproduced, and
 * the names here are this project's own. The headers are not in this tree:
 * docs/references/README.md, "Windows 98 DDK", records the package, its
 * SHA-256 and where to fetch it, and legal-provenance.md section 4 has the
 * row for these values. The headers are members of the package's
 * INCS_DDK.CAB that its INCS_DDK.INF installs into inc\win98:
 *
 *   CONFIGMG.H  (CAB member INC_WIN98_CONFIGMG.H, 106,882 bytes, SHA-256
 *               e540ecb80a28f16f733be2280a3d99287aea1e9d3e89b473352d8ab40751d999)
 *   VMM.H       (INC_WIN98_VMM.H, 188,260 bytes, SHA-256
 *               1c8a6f7637ffa7ba97b095cc543c6bb89092bbc41fa32b5a980870e219fc6727)
 *   VMM.INC     (INC_WIN98_VMM.INC, 93,146 bytes, SHA-256
 *               713fa2d33e417ca553ce2aef64681dae26ca756ad90dc9bbd7401a044c5be072)
 *
 * A wrong service number is a call into a different VMM service, so the
 * run-time guards of record 21 section 6.1 (Get_Version's high byte 4, the
 * root devnode's id read back) stay in the driver whatever this file says.
 */
#ifndef CM_CFGMG_H
#define CM_CFGMG_H

/*
 * The dynalink. A ring-0 call is INT 20h followed by the dword
 * (VxD id << 16) | service; with bit 15 of the service word set the VMM
 * patches the six bytes into a jump to the service instead of a call.
 */
#define CMG_DYNALINK_INT        0x20        /* VMM.INC line 3355, DYNA_LINK_INT */
#define CMG_DYNALINK_JMP        0x8000      /* VMM.H line 4919, DL_Jmp_Mask (8000h); VMM.H line 1068, the C VxDJmp ORs 0x80 into the service word's high byte */
#define CMG_VXD_ID              0x0033      /* VMM.H line 175, CONFIGMG_DEVICE_ID */

/*
 * Service numbers. CONFIGMG.H's service table opens at line 532
 * (Begin_Service_Table(CONFIGMG, VxD)) and VMM.H lines 353-358 number its
 * Declare_Service entries from 0 in order, one per entry. The table, lines
 * 532-663, holds 124 entries and no conditional; its only other lines are
 * two three-line comments, "4.0 OPK2 Services" (624-626) and "4.1 Services"
 * (645-647). So service n is the entry on line 533 + n up to line 623,
 * 536 + n from 627 to 644, and 539 + n from 648. The header's own name
 * array (DEBUG_SERVICE_NAMES, lines 671 on) lists 124 names in the same
 * order, the same but for one spelling at 109 (line 783's
 * Register_For_Idle_Detection for line 648's
 * Register_DevNode_For_Idle_Detection). Get_Version returns a WORD read from AX (lines 2791-2797); every
 * other service returns a CR_ value in EAX and is C-callable (lines
 * 2775-2777).
 */
#define CMG_SVC_GET_VERSION             0   /* line 533, _CONFIGMG_Get_Version */
#define CMG_SVC_LOCATE_DEVNODE          2   /* line 535, _CONFIGMG_Locate_DevNode */
#define CMG_SVC_GET_PARENT              3   /* line 536, _CONFIGMG_Get_Parent */
#define CMG_SVC_GET_CHILD               4   /* line 537, _CONFIGMG_Get_Child */
#define CMG_SVC_GET_SIBLING             5   /* line 538, _CONFIGMG_Get_Sibling */
#define CMG_SVC_GET_DEVICE_ID_SIZE      6   /* line 539, _CONFIGMG_Get_Device_ID_Size */
#define CMG_SVC_GET_DEVICE_ID           7   /* line 540, _CONFIGMG_Get_Device_ID */
#define CMG_SVC_QUERY_REMOVE_SUBTREE    12  /* line 545, _CONFIGMG_Query_Remove_SubTree */
#define CMG_SVC_REMOVE_SUBTREE          13  /* line 546, _CONFIGMG_Remove_SubTree */
#define CMG_SVC_REGISTER_DEVICE_DRIVER  14  /* line 547, _CONFIGMG_Register_Device_Driver */
#define CMG_SVC_LOCK                    22  /* line 555, _CONFIGMG_Lock */
#define CMG_SVC_UNLOCK                  23  /* line 556, _CONFIGMG_Unlock */
#define CMG_SVC_REENUMERATE_DEVNODE     51  /* line 584, _CONFIGMG_Reenumerate_DevNode */
#define CMG_SVC_SETUP_DEVNODE           52  /* line 585, _CONFIGMG_Setup_DevNode */
#define CMG_SVC_GET_DEVNODE_STATUS      54  /* line 587, _CONFIGMG_Get_DevNode_Status */
#define CMG_SVC_DISABLE_DEVNODE         64  /* line 597, _CONFIGMG_Disable_DevNode */
#define CMG_SVC_ENABLE_DEVNODE          65  /* line 598, _CONFIGMG_Enable_DevNode */
#define CMG_SVC_BROADCAST_DEVICE_CHANGE 88  /* line 621, _CONFIGMG_Broadcast_Device_Change_Message */
#define CMG_SVC_CHANGE_DEVNODE_STATUS   91  /* line 627, _CONFIGMG_Change_DevNode_Status, first of the "4.0 OPK2" group */
#define CMG_SVC_SET_DEVNODE_PROBLEM     122 /* line 661, _CONFIGMG_Set_DevNode_Problem, in the "4.1 Services" group */

/*
 * Get_Version. CONFIGMG.H lines 27 and 37 define CONFIGMG_VERSION as
 * 0x0400 under WIN40COMPAT and 0x040A otherwise; the guard reads only the
 * high byte of AX.
 */
#define CMG_VERSION_MAJOR       0x04        /* CONFIGMG.H lines 27, 37, high byte of CONFIGMG_VERSION */

/* Results (CONFIGMG.H lines 937-990). */
#define CMG_CR_SUCCESS              0x00    /* line 937, CR_SUCCESS */
#define CMG_CR_INVALID_POINTER      0x03    /* line 940, CR_INVALID_POINTER */
#define CMG_CR_INVALID_FLAG         0x04    /* line 941, CR_INVALID_FLAG */
#define CMG_CR_INVALID_DEVNODE      0x05    /* line 942, CR_INVALID_DEVNODE */
#define CMG_CR_NO_SUCH_DEVNODE      0x0D    /* line 950, CR_NO_SUCH_DEVNODE */
#define CMG_CR_FAILURE              0x13    /* line 956, CR_FAILURE */
#define CMG_CR_NOT_A_GOOD_TIME      0x16    /* line 959, CR_NOT_A_GOOD_TIME */
#define CMG_CR_REMOVE_VETOED        0x17    /* line 960, CR_REMOVE_VETOED */
#define CMG_CR_BUFFER_SMALL         0x1A    /* line 963, CR_BUFFER_SMALL */
#define CMG_CR_DEVICE_NOT_THERE     0x24    /* line 973, CR_DEVICE_NOT_THERE */
#define CMG_CR_NOT_DISABLEABLE      0x28    /* line 977, CR_NOT_DISABLEABLE */
#define CMG_CR_QUERY_VETOED         0x2A    /* line 979, CR_QUERY_VETOED */

/* Device ids (CONFIGMG.H line 482). */
#define CMG_MAX_DEVICE_ID_LEN       200     /* line 482, MAX_DEVICE_ID_LEN */

/* Flags of the calls above (CONFIGMG.H). */
#define CMG_QUERY_REMOVE_UI_OK      0x00    /* line 1180, CM_QUERY_REMOVE_UI_OK */
#define CMG_QUERY_REMOVE_UI_NOT_OK  0x01    /* line 1181, CM_QUERY_REMOVE_UI_NOT_OK */
#define CMG_REMOVE_UI_OK            0x00    /* line 1184, CM_REMOVE_UI_OK */
#define CMG_REMOVE_UI_NOT_OK        0x01    /* line 1185, CM_REMOVE_UI_NOT_OK */
#define CMG_REGISTER_DRIVER_REMOVABLE 0x02  /* line 1156, CM_REGISTER_DEVICE_DRIVER_REMOVABLE */
#define CMG_SETUP_DEVNODE_READY     0x00    /* line 1188, CM_SETUP_DEVNODE_READY */
#define CMG_DISABLE_POLITE          0x00    /* line 1232, CM_DISABLE_POLITE */
#define CMG_DISABLE_ABSOLUTE        0x01    /* line 1233, CM_DISABLE_ABSOLUTE */
#define CMG_REENUMERATE_NORMAL      0x00    /* line 1256, CM_REENUMERATE_NORMAL */
#define CMG_REENUMERATE_SYNCHRONOUS 0x01    /* line 1257, CM_REENUMERATE_SYNCHRONOUS */
#define CMG_CHANGE_STATUS_REMOVE_FLAGS 0x00 /* line 1283, CM_CHANGE_DEVNODE_STATUS_REMOVE_FLAGS */
#define CMG_CHANGE_STATUS_ADD_FLAGS 0x01    /* line 1284, CM_CHANGE_DEVNODE_STATUS_ADD_FLAGS */

/*
 * Devnode status bits, as Get_DevNode_Status returns them (CONFIGMG.H
 * lines 2036-2084). Change_DevNode_Status may set or clear only the bits of
 * DN_CHANGEABLE_FLAGS (line 2086, 0x79BB62E0), which holds DN_REMOVABLE
 * and DN_DISABLEABLE.
 */
#define CMG_DN_DRIVER_LOADED        0x00000002UL    /* line 2048, DN_DRIVER_LOADED */
#define CMG_DN_STARTED              0x00000008UL    /* line 2050, DN_STARTED */
#define CMG_DN_NEED_TO_ENUM         0x00000020UL    /* line 2052, DN_NEED_TO_ENUM */
#define CMG_DN_HAS_PROBLEM          0x00000400UL    /* line 2057, DN_HAS_PROBLEM */
#define CMG_DN_DISABLEABLE          0x00002000UL    /* line 2060, DN_DISABLEABLE */
#define CMG_DN_REMOVABLE            0x00004000UL    /* line 2061, DN_REMOVABLE */
#define CMG_DN_WILL_BE_REMOVED      0x00040000UL    /* line 2065, DN_WILL_BE_REMOVED */
#define CMG_DN_NT_DRIVER            0x01000000UL    /* line 2074, DN_NT_DRIVER */
#define CMG_DN_CHANGEABLE_FLAGS     0x79BB62E0UL    /* line 2086, DN_CHANGEABLE_FLAGS */

/* Problem numbers, for Get_DevNode_Status and Set_DevNode_Problem. */
#define CMG_PROB_NOT_CONFIGURED     0x01    /* line 1048, CM_PROB_NOT_CONFIGURED */
#define CMG_PROB_FAILED_START       0x0A    /* line 1057, CM_PROB_FAILED_START */
#define CMG_PROB_WILL_BE_REMOVED    0x15    /* line 1068, CM_PROB_WILL_BE_REMOVED */
#define CMG_PROB_DISABLED           0x16    /* line 1069, CM_PROB_DISABLED */
#define CMG_PROB_DEVICE_NOT_THERE   0x18    /* line 1071, CM_PROB_DEVICE_NOT_THERE */

#endif /* CM_CFGMG_H */
