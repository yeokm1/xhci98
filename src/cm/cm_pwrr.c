/*
 * cm_pwrr.c - the system SET_POWER's route (cm_pwrr.h; task 43.65).
 * DDK-free and host-tested (test\test_cm_pwrr.c). IRQL: any.
 */

#include "cm_pwrr.h"

ULONG CmPowerRoute(BOOLEAN ok, BOOLEAN started, ULONG systemState,
                   ULONG devicePower, BOOLEAN suspended, BOOLEAN nt,
                   ULONG *want)
{
    ULONG w;

    w = (systemState == CM_PWR_S_WORKING) ? CM_PWR_D0 : CM_PWR_D3;
    if (want != NULL) {
        *want = w;
    }
    if (!ok || !started) {
        return CM_PWR_PASS;
    }
    if (w == devicePower) {
        /* An earlier direct transition left the client suspended in D0:
         * no device IRP is owed, only its reinitialisation. */
        return (w == CM_PWR_D0 && suspended) ? CM_PWR_DIRECT : CM_PWR_PASS;
    }
    if (!nt && systemState == CM_PWR_S_SHUTDOWN) {
        return CM_PWR_DETACH;
    }
    return CM_PWR_REQUEST;
}

BOOLEAN CmPowerShutdownConfig(ULONG systemState, BOOLEAN nt)
{
    return (BOOLEAN)(nt || systemState != CM_PWR_S_SHUTDOWN);
}
