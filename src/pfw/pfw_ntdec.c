/*
 * pfw_ntdec.c - XHCIPFW.SYS's decision (pfw_ntdec.h; design record 32
 * section 11.2): the step runs on 2000 and XP only with one processor
 * (no service holds the others there), on Vista and later inside
 * KeIpiGenericCall where several run, never in Safe Mode and never after
 * the boot drivers' time.
 */

#include "pfw_ntdec.h"

int PfwNtDecide(PFW_U32 major, PFW_U32 minor, PFW_U32 nproc, int safeMode,
                int late, int haveIpi, PFW_U32 *skip)
{
    int nt5 = (major == 5 && (minor == 0 || minor == 1));  /* 2000, XP */

    *skip = 0;
    if (!nt5 && major < 6) {
        *skip = PFW_SKIP_SYSTEM;        /* NT 4, Server 2003: not offered */
        return PFW_NT_SKIP;
    }
    if (safeMode) {
        *skip |= PFW_SKIP_SAFEMODE;
    }
    if (late) {
        *skip |= PFW_SKIP_LATE;
    }
    if (*skip != 0) {
        return PFW_NT_SKIP;
    }
    if (nproc <= 1) {
        return PFW_NT_RUN_UP;
    }
    if (nt5 || !haveIpi) {
        *skip = PFW_SKIP_UNHELD;
        return PFW_NT_SKIP;
    }
    return PFW_NT_RUN_IPI;
}
