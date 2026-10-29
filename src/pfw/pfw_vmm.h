/*
 * pfw_vmm.h - the layouts and values XHCIPFW.VXD needs from the Windows
 * 98 SE DDK, written under names of its own and cited by header and line
 * (docs\references\README.md, "Windows 98 DDK", has the members' hashes),
 * as src\vsd\vsd_vmm.h does for S98VSD.VXD (design record 30 section
 * 6.2; design record 32 section 5.1). No declaration, macro body or
 * comment of the DDK is reproduced.
 */

#ifndef PFW_VMM_H
#define PFW_VMM_H

/* VMM.H 5214-5237, struct VxD_Desc_Block, field for field. */
typedef struct PFW_DDB {
    unsigned long Next;
    unsigned short SdkVersion;
    unsigned short ReqDeviceNumber;
    unsigned char MajorVersion;
    unsigned char MinorVersion;
    unsigned short Flags;
    unsigned char Name[8];
    unsigned long InitOrder;
    unsigned long ControlProc;
    unsigned long V86ApiProc;
    unsigned long PmApiProc;
    unsigned long V86ApiCsip;
    unsigned long PmApiCsip;
    unsigned long ReferenceData;
    unsigned long ServiceTablePtr;
    unsigned long ServiceTableSize;
    unsigned long Win32ServiceTable;
    unsigned long Prev;
    unsigned long Size;
    unsigned long Reserved1;
    unsigned long Reserved2;
    unsigned long Reserved3;
} PFW_DDB;

#define PFW_DDK_VERSION         0x040AU     /* VMM.H 5206, DDK_VERSION (98)  */
#define PFW_UNDEFINED_DEVICE_ID 0x0000U     /* VMM.H 120                     */
#define PFW_DDB_PREV            0x50726576UL /* VMM.H 5232, 'Prev'           */
#define PFW_DDB_RSV1            0x52737631UL /* VMM.H 5234, 'Rsv1'           */
#define PFW_DDB_RSV2            0x52737632UL /* VMM.H 5235, 'Rsv2'           */
#define PFW_DDB_RSV3            0x52737633UL /* VMM.H 5236, 'Rsv3'           */

/* The init order: below CONFIGMG_INIT_ORDER (VMM.H 247, 16400000h) and
 * the enumerators' ENUMERATOR_INIT_ORDER (VMM.H 248, 16800000h; PCI,
 * VMM.H 251), above NTKERN_INIT_ORDER (VMM.H 245, 16200000h), which this
 * device does not need (record 32 section 5.1). */
#define PFW_INIT_ORDER          0x16300000UL

/* The control messages (VMM.H 2987 and 2994). */
#define PFW_SYS_CRITICAL_INIT   0x0000UL
#define PFW_DEVICE_INIT         0x0001UL

/* VMM's registry services, C-callable with the arguments on the stack and
 * the caller removing them (VMMREG.H 190, 201 and 347, VMM_RegCloseKey,
 * VMM_RegCreateKey and VMM_RegSetValueEx): the VMM device (1, VMM.H 121)
 * services 148h to 152h of VMM.H's table from line 1326, _RegCloseKey at
 * line 2217 (149h), _RegCreateKey at 2218 (14Ah), _RegSetValueEx at 2226
 * (152h). */
#define PFW_SVC_REGCLOSEKEY     0x00010149UL
#define PFW_SVC_REGCREATEKEY    0x0001014AUL
#define PFW_SVC_REGSETVALUEEX   0x00010152UL

#define PFW_HKEY_LOCAL_MACHINE  0x80000002UL /* VMM.H 2240                  */
#define PFW_REG_SZ              1UL         /* VMM.H 2231                    */
#define PFW_REG_BINARY          3UL         /* VMM.H 2232                    */
#define PFW_REG_DWORD           4UL         /* VMMREG.H 36                   */

#endif
