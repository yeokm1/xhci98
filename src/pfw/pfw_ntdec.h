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

/* major/minor: PsGetVersion's; nproc: the processors running; safeMode:
 * InitSafeBootMode not 0; late: not called at the boot drivers' time (the
 * SOFTWARE hive is loaded, or the test could not tell); haveIpi:
 * KeIpiGenericCall resolved. *skip gets the reasons (0 when it runs). */
int PfwNtDecide(PFW_U32 major, PFW_U32 minor, PFW_U32 nproc, int safeMode,
                int late, int haveIpi, PFW_U32 *skip);

#endif
