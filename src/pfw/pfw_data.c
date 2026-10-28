/*
 * pfw_data.c - XHCIPFW.VXD's data: the device descriptor block, the
 * result of the step and the stamp (design record 32 sections 5.1 and 6).
 *
 * In an object of its own: the 98 DDK's LINK 5.12 mislinks a reference
 * from an object's code back to data that object defined earlier (design
 * record 30 section 6.1), so every data reference from the code is an
 * external.
 */

#include "pfw.h"
#include "../xhci_version.h"

/* The export the .def names (XHCIPFW_DDB @1), undecorated. */
#pragma aux XHCIPFW_DDB "*";

PFW_DDB XHCIPFW_DDB = {
    0, PFW_DDK_VERSION, PFW_UNDEFINED_DEVICE_ID, 1, 0, 0,
    { 'X', 'H', 'C', 'I', 'P', 'F', 'W', ' ' },
    PFW_INIT_ORDER,
    (unsigned long)PfwControl, 0, 0, 0, 0,
    0, 0, 0, 0,
    PFW_DDB_PREV, sizeof(PFW_DDB),
    PFW_DDB_RSV1, PFW_DDB_RSV2, PFW_DDB_RSV3
};

PFW_RESULT PfwRes = { 0 };

unsigned long PfwLogRc = 0;

const char PfwStamp[] = "XHCIPFW.VXD " XHCI_VER_STR;
