/*
 * vsd.h - STOR98.VXD, shared by vsd.c (code) and vsd_data.c (data); design
 * record 30. The DDK's block-device headers are the staged copies of
 * stage-ddk.ps1 (obj\inc\), compiled with -zp1: they mirror the packed
 * MASM layouts.
 */

#ifndef VSD_H
#define VSD_H

#include <basedef.h>
#include <sgd.h>
#include <blockdev.h>
#include <ior.h>
#include <iop.h>
#include <dcb.h>
#include <isp.h>
#include <aep.h>
#include <ilb.h>
#include <drp.h>
#include "vsd_vmm.h"
#include "vsd_list.h"

/* The layer's area in every IOP of a claimed DCB (section 3.4): sized
 * for VSD_MAX_XFER whatever the DCB's maximum, which section 3.3 holds
 * at or below it. */
typedef struct VSD_AREA {
    ULONG SaveSgd;              /* IOR_sgd_lin_phys as it came  */
    ULONG SaveFlags;            /* IOR_flags as it came         */
    UCHAR SaveNum;              /* IOR_num_sgds as it came      */
    UCHAR Pad[7];
    VSD_RUN List[VSD_ROOM_RUNS];
    VSD_U32 Scratch[VSD_SCRATCH];
} VSD_AREA;

/* Counters (section 4.9); the release build keeps them for a debugger. */
typedef struct VSD_COUNTS {
    ULONG Built;                /* lists built                     */
    ULONG Prebuilt;             /* passed with IORF_PHYS_SGDS       */
    ULONG NoData;               /* passed with no data              */
    ULONG Refused[VSD_R_KINDS]; /* by VSD_R_ value                  */
    ULONG SrbNoData;            /* SRB data on a no-data request    */
    ULONG NoSrb;                /* data and no SRB                  */
    ULONG CbFull;               /* the callback table full          */
    ULONG MaxRuns;
    ULONG MaxBytes;
    ULONG Claimed;
    ULONG Left[VSD_C_KINDS];    /* DCBs left, by VSD_C_ value       */
    ULONG PhysLeft;             /* phys_sgd still demanded after us */
    ULONG InsertFailed;
    ULONG Registered;           /* IOS_Register succeeded           */
    ULONG Stamp;                /* VsdStamp's address: keeps it linked */
} VSD_COUNTS;

void __cdecl VsdAer(AEP *aep);
void __cdecl VsdRequest(IOP *iop);
void __cdecl VsdCallback(IOP *iop);
void __declspec(naked) VsdControl(void);

extern ILB VsdIlb;
extern DRP VsdDrp;
extern VSD_DDB S98VSD_DDB;
extern VSD_COUNTS VsdCounts;
extern const char VsdStamp[];

#endif
