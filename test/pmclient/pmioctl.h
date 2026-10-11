/*
 * pmioctl.h - the control interface between XHCIPM.SYS, selective
 * suspend's test client (roadmap-hcd.md task 36.34; design record 14
 * section 10, SUSPa.6), and PMCTL.EXE, its console tool. Shared by a WDM
 * driver built with the DDK and a Win32 program built with MSVC 6.0, so
 * it includes nothing and uses only fixed-size integers.
 *
 * TEST ONLY. Neither binary is ever shipped: scripts\package\make-release.ps1
 * refuses a release that carries either (the marker below, or the names).
 */

#ifndef PMIOCTL_H
#define PMIOCTL_H

/* The marker both binaries carry, which the packaging gate looks for. */
#define PM_TEST_MARKER "XHCI98_TEST_CLIENT_DO_NOT_SHIP"

/* \\.\XHCIPM0 .. \\.\XHCIPM7: one per device the client is bound to. */
#define PM_LINK_PREFIX "XHCIPM"
#define PM_MAX_DEVICES 8

/* CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900 + n, METHOD_BUFFERED,
 * FILE_ANY_ACCESS), spelled out so the tool needs no DDK header. */
#define PM_IOCTL(n) ((0x00000022UL << 16) | ((0x900UL + (n)) << 2))

#define PM_IOCTL_STATUS      PM_IOCTL(0)  /* out: PM_STATUS               */
#define PM_IOCTL_SET_POWER   PM_IOCTL(1)  /* in: ULONG D-state 1..4 (D0..D3) */
#define PM_IOCTL_WAIT_WAKE   PM_IOCTL(2)  /* request IRP_MN_WAIT_WAKE (S0)  */
#define PM_IOCTL_WW_CANCEL   PM_IOCTL(3)  /* cancel the held WAIT_WAKE      */
#define PM_IOCTL_IDLE        PM_IOCTL(4)  /* in: ULONG flags (PM_IDLE_*)    */
#define PM_IOCTL_IDLE_CANCEL PM_IOCTL(5)  /* cancel the idle IRP            */
#define PM_IOCTL_READS       PM_IOCTL(6)  /* in: ULONG 1 start, 0 stop      */
#define PM_IOCTL_RESET       PM_IOCTL(7)  /* zero the counts                */

/* PM_IOCTL_IDLE's flags: what the idle callback does, as a class driver's
 * would. Default 0: request the device's wake state (D2) and wait for it. */
#define PM_IDLE_ARM_WAKE     0x1UL  /* request WAIT_WAKE first            */
#define PM_IDLE_NO_POWER     0x2UL  /* do nothing in the callback         */
#define PM_IDLE_D3           0x4UL  /* request D3 instead of D2           */
#define PM_IDLE_AUTO_D0      0x8UL  /* on its completion, request D0      */

/* The status, every field a count or a last value. D-states as
 * DEVICE_POWER_STATE (1 = D0 .. 4 = D3). */
typedef struct _PM_STATUS {
    unsigned long Size;             /* sizeof(PM_STATUS)                  */
    unsigned long Version;          /* 1                                  */
    unsigned long DevicePower;      /* last SET_POWER that completed      */
    unsigned long SystemPower;
    unsigned long Started;
    /* D-IRPs this client requested (PoRequestPowerIrp) */
    unsigned long PowerRequests;
    unsigned long PowerCallbacks;
    long PowerLastStatus;
    unsigned long PowerLastState;
    unsigned long PowerRefused;     /* PoRequestPowerIrp itself failed    */
    /* WAIT_WAKE: requested, the requester's callback called (5.5: the 9x
     * defect is a WAIT_WAKE whose callback never comes), its status, and
     * the IRP's completion as seen passing up through the FDO */
    unsigned long WakeRequests;
    unsigned long WakeCallbacks;
    long WakeLastStatus;
    unsigned long WakeIrpCompletions;
    long WakeIrpLastStatus;
    unsigned long WakeHeld;         /* one is outstanding now             */
    /* the idle notification (XP onward) */
    unsigned long IdleSubmits;
    unsigned long IdleCallbacks;
    unsigned long IdleCompletions;
    long IdleLastStatus;
    unsigned long IdleHeld;
    unsigned long IdleCallbackPower; /* the D-state the callback asked     */
    long IdleCallbackPowerStatus;    /* and what its D-IRP returned        */
    /* the interrupt IN read loop */
    unsigned long ReadsOn;
    unsigned long ReadsCompleted;
    unsigned long ReadsFailed;
    long ReadLastStatus;
    unsigned long ReadLastUsbd;
    /* the device */
    unsigned long Configured;
    unsigned long ConfigAttributes;  /* bmAttributes of configuration 1   */
    unsigned long InterruptIn;       /* its address, 0 when none          */
    long ConfigStatus;
} PM_STATUS;

#endif /* PMIOCTL_H */
