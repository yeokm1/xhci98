/*
 * test_cm_pwrr.c - the system SET_POWER's route (src/cm/cm_pwrr.c, task
 * 43.65): a failed or unstarted pass, the state already held, the
 * reinitialisation of a client suspended in D0, a request on NT and on 9x
 * for a sleep, and 9x's shutdown detached from the thread (task 44.47;
 * taken directly from 43.65) while NT's requests D3; and (task 44.43) 9x's
 * shutdown quiescence touching no configuration space.
 */

#include <stdio.h>
#include "../src/cm/cm_pwrr.h"
#include "test_harness.h"

#define S_SLEEP1 2UL
#define S_HIBER  5UL

int main(void)
{
    ULONG want;

    CHECK_EQ(CmPowerRoute(FALSE, TRUE, CM_PWR_S_SHUTDOWN, CM_PWR_D0, FALSE,
                          FALSE, &want), CM_PWR_PASS, "failed below: pass");
    CHECK_EQ(want, CM_PWR_D3, "shutdown wants D3");
    CHECK_EQ(CmPowerRoute(TRUE, FALSE, CM_PWR_S_SHUTDOWN, CM_PWR_D0, FALSE,
                          TRUE, &want), CM_PWR_PASS, "not started: pass");

    /* 43.65: Windows 98 SE and ME shut down with no device IRP; 44.47:
     * and the system IRP goes on without waiting for the thread. */
    CHECK_EQ(CmPowerRoute(TRUE, TRUE, CM_PWR_S_SHUTDOWN, CM_PWR_D0, FALSE,
                          FALSE, &want), CM_PWR_DETACH,
             "9x shutdown: detached");
    CHECK_EQ(want, CM_PWR_D3, "9x shutdown: the quiescence of D3");
    CHECK_EQ(CmPowerRoute(TRUE, TRUE, CM_PWR_S_SHUTDOWN, CM_PWR_D0, FALSE,
                          TRUE, &want), CM_PWR_REQUEST, "NT shutdown: D3 asked");
    CHECK_EQ(CmPowerRoute(TRUE, TRUE, CM_PWR_S_SHUTDOWN, CM_PWR_D0, TRUE,
                          FALSE, &want), CM_PWR_DETACH,
             "9x shutdown, client suspended in D0: detached, its unbind owed");
    CHECK_EQ(CmPowerRoute(TRUE, TRUE, CM_PWR_S_SHUTDOWN, CM_PWR_D3, FALSE,
                          FALSE, &want), CM_PWR_PASS,
             "9x shutdown already in D3: pass");

    /* Sleeps are as before on both. */
    CHECK_EQ(CmPowerRoute(TRUE, TRUE, S_SLEEP1, CM_PWR_D0, FALSE, FALSE,
                          &want), CM_PWR_REQUEST, "9x standby: D3 asked");
    CHECK_EQ(CmPowerRoute(TRUE, TRUE, S_HIBER, CM_PWR_D0, FALSE, TRUE,
                          &want), CM_PWR_REQUEST, "NT hibernate: D3 asked");
    CHECK_EQ(CmPowerRoute(TRUE, TRUE, CM_PWR_S_WORKING, CM_PWR_D3, FALSE,
                          FALSE, &want), CM_PWR_REQUEST, "9x wake: D0 asked");
    CHECK_EQ(want, CM_PWR_D0, "working wants D0");
    CHECK_EQ(CmPowerRoute(TRUE, TRUE, CM_PWR_S_WORKING, CM_PWR_D0, TRUE,
                          TRUE, &want), CM_PWR_DIRECT,
             "suspended in D0, back to working: reinitialised directly");
    CHECK_EQ(CmPowerRoute(TRUE, TRUE, CM_PWR_S_WORKING, CM_PWR_D0, FALSE,
                          FALSE, &want), CM_PWR_PASS, "already D0: pass");
    CHECK_EQ(CmPowerRoute(TRUE, TRUE, CM_PWR_S_WORKING, CM_PWR_D0, FALSE,
                          FALSE, NULL), CM_PWR_PASS, "NULL want");

    /* 44.43: the 9x shutdown quiescence reaches no configuration space. */
    CHECK(!CmPowerShutdownConfig(CM_PWR_S_SHUTDOWN, FALSE),
          "9x shutdown: no configuration IRP");
    CHECK(CmPowerShutdownConfig(CM_PWR_S_SHUTDOWN, TRUE),
          "NT shutdown: the Bus Master bit and force power as before");
    CHECK(CmPowerShutdownConfig(S_SLEEP1, FALSE) &&
          CmPowerShutdownConfig(S_HIBER, FALSE) &&
          CmPowerShutdownConfig(CM_PWR_S_WORKING, FALSE),
          "9x, any other state: as before");

    printf("%d checks, %d failures\n", checks, failures);
    return failures;
}
