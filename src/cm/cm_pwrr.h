/*
 * cm_pwrr.h - which way the device half of a system SET_POWER goes, the
 * pure half of cm_power.c's completion routine (design record 21 section
 * 4.4; task 43.65, 2026-10-23). DDK-free and host-tested
 * (test\test_cm_pwrr.c).
 *
 *   CM_PWR_PASS     no device transition of this driver's: the system IRP
 *                   goes on at once;
 *   CM_PWR_REQUEST  PoRequestPowerIrp for the matching device state, the
 *                   system IRP completed from its callback;
 *   CM_PWR_DIRECT   the thread's direct transition (the quiescence, or a
 *                   client suspended in D0 reinitialised) with no device
 *                   IRP, then the system IRP.
 *
 * Windows 98 SE and ME at a shutdown (task 43.65): DIRECT, never REQUEST.
 * There the device IRP's D3, passed down from the connection-manager
 * thread after the quiescence, did not return from the bus below on three
 * of five first shutdowns after a wizard or the installer had just started
 * the driver (ME, QEMU, 2026-10-23), and the machine hung with VMM idle;
 * a shutdown needs no device state of this driver's, and the quiescence
 * runs the same on either route.
 *
 * States are the WDK's numbers (SYSTEM_POWER_STATE, DEVICE_POWER_STATE),
 * checked against them in cm_power.c. IRQL: any.
 */
#ifndef CM_PWRR_H
#define CM_PWRR_H
#include "../xhci_compat.h"

#define CM_PWR_PASS     0UL
#define CM_PWR_REQUEST  1UL
#define CM_PWR_DIRECT   2UL

#define CM_PWR_S_WORKING    1UL     /* PowerSystemWorking  */
#define CM_PWR_S_SHUTDOWN   6UL     /* PowerSystemShutdown */
#define CM_PWR_D0           1UL     /* PowerDeviceD0       */
#define CM_PWR_D3           4UL     /* PowerDeviceD3       */

/* ok: the system IRP succeeded below; started: the instance is started;
 * systemState: the state it carries; devicePower: the state recorded;
 * suspended: a client suspended in D0 by an earlier direct transition;
 * nt: IoIsWdmVersionAvailable(1, 0x10). *want gets the device state the
 * system state matches (D0 for working, D3 otherwise). */
ULONG CmPowerRoute(BOOLEAN ok, BOOLEAN started, ULONG systemState,
                   ULONG devicePower, BOOLEAN suspended, BOOLEAN nt,
                   ULONG *want);

#endif
