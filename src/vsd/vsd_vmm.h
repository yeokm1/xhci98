/*
 * vsd_vmm.h - the layouts and values STOR98.VXD needs that no Windows 98
 * SE DDK header gives it in a form Open Watcom compiles (design record 30
 * section 6.2): VMM.H assumes Microsoft's compiler, and SRB.H drags in the
 * NT port's types. Each is written under a name of the layer's own and
 * cited by header and line (docs\references\README.md, "Windows 98 DDK",
 * for the members' hashes), as src\cm\cm_cfgmg.h cites CONFIGMG.H. No
 * declaration, macro body or comment of the DDK is reproduced.
 */

#ifndef VSD_VMM_H
#define VSD_VMM_H

/* VMM.H 5214-5237, struct VxD_Desc_Block: the device descriptor block,
 * field for field (each ULONG or USHORT the header's). */
typedef struct VSD_DDB {
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
} VSD_DDB;

#define VSD_DDK_VERSION         0x040AU     /* VMM.H 5206, DDK_VERSION (98)  */
#define VSD_UNDEFINED_DEVICE_ID 0x0000U     /* VMM.H 120                     */
#define VSD_UNDEFINED_INIT      0x80000000UL /* VMM.H 287, UNDEFINED_INIT_ORDER */
#define VSD_DDB_PREV            0x50726576UL /* VMM.H 5232, 'Prev'           */
#define VSD_DDB_RSV1            0x52737631UL /* VMM.H 5234, 'Rsv1'           */
#define VSD_DDB_RSV2            0x52737632UL /* VMM.H 5235, 'Rsv2'           */
#define VSD_DDB_RSV3            0x52737633UL /* VMM.H 5236, 'Rsv3'           */

#define VSD_SYS_DYNAMIC_INIT    0x001BUL    /* VMM.H 3219                    */
#define VSD_SYS_DYNAMIC_EXIT    0x001CUL    /* VMM.H 3220                    */

/* A VxD service is called with int 20h and a dword: the device id in the
 * high word, the service's ordinal in the low (VMM.H's VMMCall/VxDCall).
 * _CopyPageTable is VMM (id 1, VMM.H 121) service 0x61 (the 98th entry of
 * VMM.H's service table from line 1326, at line 1521); IOS_Register is
 * the IOS device's (BLOCKDEV_DEVICE_ID 0x10, VMM.H 136) service 7 (IOS.H,
 * its service table from line 45, at line 162). */
#define VSD_SVC_COPYPAGETABLE   0x00010061UL
#define VSD_SVC_IOS_REGISTER    0x00100007UL

/* SRB.H 321-343, SCSI_REQUEST_BLOCK: the two fields record 30 section 4.3
 * reads, by offset. */
#define VSD_SRB_DATA_LENGTH     0x10        /* DataTransferLength, line 334  */
#define VSD_SRB_DATA_BUFFER     0x18        /* DataBuffer, line 336          */

#endif
