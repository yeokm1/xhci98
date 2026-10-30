/*
 * pfw_ntdec.h - XHCIPFW.SYS's decision whether to run the step at this
 * start, and how (design record 32 section 11.2). DDK-free and C89, so
 * test\test_pfw.c checks it on the host with the same source the driver
 * links.
 */

#ifndef PFW_NTDEC_H
#define PFW_NTDEC_H

#include "pfw_scan.h"

/* What DriverEntry does. */
#define PFW_NT_SKIP         0   /* nothing; *skip says why            */
#define PFW_NT_RUN_UP       1   /* one processor: the step at HIGH_LEVEL */
#define PFW_NT_RUN_IPI      2   /* inside KeIpiGenericCall              */

/* Why nothing was done: the service key's Parameters value Skipped. */
#define PFW_SKIP_SYSTEM     0x01UL  /* not 2000, XP nor Vista or later */
#define PFW_SKIP_SAFEMODE   0x02UL  /* a Safe Mode start               */
#define PFW_SKIP_LATE       0x04UL  /* not at the boot drivers' time   */
#define PFW_SKIP_UNHELD     0x08UL  /* several processors and no way to
                                       hold them (2000 or XP, or the
                                       routine not found)               */
#define PFW_SKIP_UNATTACHED 0x10UL  /* the filter role: a root bus it
                                       is named on could not be attached,
                                       so that bus starts unscanned
                                       (record 32 section 11.5.2)       */
#define PFW_SKIP_NORANGE    0x20UL  /* the filter role: the root bus's
                                       start carries no bus numbers, so
                                       its range is not known; nothing
                                       scanned                          */

/* The role DriverEntry takes (record 32 section 11.5.2): the step at
 * DriverEntry on 2000, XP, Vista and 7; an upper filter of the PCI root
 * bus, the step at its first start, on 8 and 8.1; neither elsewhere. */
#define PFW_ROLE_NONE       0
#define PFW_ROLE_ENTRY      1
#define PFW_ROLE_FILTER     2
int PfwNtRole(PFW_U32 major, PFW_U32 minor);

/* The filter role's account of every root bus it recorded since it was
 * loaded (record 32 section 11.5.2): each bus's record replaces the one
 * before it, so these are kept beside the last bus's values. */
typedef struct PFW_NT_SUM {
    PFW_U32 RootBuses;      /* root buses recorded: run, skipped or not
                               attached                                 */
    PFW_U32 SkippedAny;     /* every bus's skip reasons, ORed           */
    PFW_U32 ResultAny;      /* every bus's result bits, ORed            */
    PFW_U32 ClosedAll;      /* windows closed on every bus, summed      */
} PFW_NT_SUM;
void PfwNtSumAdd(PFW_NT_SUM *sum, PFW_U32 skip, PFW_U32 result,
                 PFW_U32 closed);

/* major/minor: PsGetVersion's; nproc: the processors running; safeMode:
 * InitSafeBootMode not 0; late: not called at the boot drivers' time (the
 * SOFTWARE hive is loaded, or the test could not tell); haveIpi:
 * KeIpiGenericCall resolved. *skip gets the reasons (0 when it runs). */
int PfwNtDecide(PFW_U32 major, PFW_U32 minor, PFW_U32 nproc, int safeMode,
                int late, int haveIpi, PFW_U32 *skip);

#endif
