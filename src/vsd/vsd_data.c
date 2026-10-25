/*
 * vsd_data.c - S98VSD.VXD's data: the device descriptor block, the IOS
 * registration packet and its ILB, the counters and the stamp (design
 * record 30 sections 3.1, 3.2 and 4.9).
 *
 * In an object of its own: the 98 DDK's LINK 5.12 mislinks a reference
 * from an object's code back to data that object defined earlier
 * (section 6.1), so every data reference from vsd.c is an external.
 */

#include "vsd.h"
#include "../xhci_version.h"

/* The export the .def names (S98VSD_DDB @1), undecorated. */
#pragma aux S98VSD_DDB "*";

ILB VsdIlb = { 0 };

DRP VsdDrp = {
    "XXXXXXXX", DRP_VSD_9, (PVOID)VsdAer, (PVOID)&VsdIlb,
    "STOR98 VSD      ", 1, 0, 0
};

VSD_DDB S98VSD_DDB = {
    0, VSD_DDK_VERSION, VSD_UNDEFINED_DEVICE_ID, 1, 0, 0,
    { 'S', '9', '8', 'V', 'S', 'D', ' ', ' ' },
    VSD_UNDEFINED_INIT,
    (unsigned long)VsdControl, 0, 0, 0, 0,
    (unsigned long)&VsdDrp, 0, 0, 0,
    VSD_DDB_PREV, sizeof(VSD_DDB),
    VSD_DDB_RSV1, VSD_DDB_RSV2, VSD_DDB_RSV3
};

VSD_COUNTS VsdCounts = { 0 };

const char VsdStamp[] = "S98VSD.VXD " XHCI_VER_STR;
