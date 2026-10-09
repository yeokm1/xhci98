/*
 * test_susp.c - host vectors for selective suspend's pure half
 * (src\xhci_susp.c; roadmap-hcd.md task 36.29, design record 14 sections
 * 3 to 6 and 3.9): the switch and capabilities; D-IRP admission; the
 * device's decision with the composite last-Dx rule, D0 during
 * SUSPENDING, epoch-stamped Dx, the latch and START's reopening; the
 * suspend and resume sequences with their outcome at every step; wake
 * classification at a root port and a USB 2.0 hub port; the wake
 * episode's claim by stamp; WAIT_WAKE; the idle notification's
 * submission, batch, claim and settlement; and, as exhaustive
 * interleavings over small models, the cancel handshake (a cancel before
 * publication, during it and after it, against a claimer) and the
 * callback context's phases against teardown's handoff (a QUEUED and a
 * RUNNING callback followed by teardown, a callback that never returns).
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci.h"
#include "../src/xhci_susp.h"
#include "test_harness.h"

#define D0 XHCI_SUSP_D0
#define D1 XHCI_SUSP_D1
#define D2 XHCI_SUSP_D2
#define D3 XHCI_SUSP_D3

/* A USB 2.0 root PORTSC: CCS, PED, PP and the link state. */
static ULONG portsc(ULONG pls)
{
    return XHCI_PORTSC_CCS | XHCI_PORTSC_PED | XHCI_PORTSC_PP |
           (pls << XHCI_PORTSC_PLS_SHIFT);
}

#define HUB_OK (XHCI_HUB_PORT_CONNECTION | XHCI_HUB_PORT_ENABLE | \
                XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_HIGH_SPEED)

static void func_init(PXHCI_SUSP_FUNC f, ULONG state)
{
    memset(f, 0, sizeof(*f));
    f->PwrState = state;
    f->PwrWanted = state;
}

/* ------------------------------------------------------------------ */

static void test_switch_and_caps(void)
{
    XHCI_SUSP_CAPS c;

    CHECK_EQ(XhciSuspSwitch(0, 1), 0, "absent: 0");
    CHECK_EQ(XhciSuspSwitch(1, 0), 0, "0");
    CHECK_EQ(XhciSuspSwitch(1, 1), 1, "1");
    CHECK_EQ(XhciSuspSwitch(1, 2), 0, "2 reads as 0");
    CHECK_EQ(XhciSuspSwitch(1, 0xFFFFFFFFUL), 0, "all ones reads as 0");

    CHECK_EQ(XhciSuspEligible(1, 0, 0), 1, "USB 2.0 non-hub");
    CHECK_EQ(XhciSuspEligible(0, 0, 0), 0, "switch off");
    CHECK_EQ(XhciSuspEligible(1, 1, 0), 0, "SuperSpeed: stage B");
    CHECK_EQ(XhciSuspEligible(1, 0, 1), 0, "hub");

    XhciSuspCaps(0, 1, 0, &c);
    CHECK_EQ(c.Changed, 0, "not eligible: today's values");
    CHECK_EQ(c.DeviceD2, 0, "not eligible: no D2");

    XhciSuspCaps(1, 1, 0, &c);
    CHECK_EQ(c.Changed, 1, "wake device");
    CHECK_EQ(c.DeviceD1, 1, "D1");
    CHECK_EQ(c.DeviceD2, 1, "D2");
    CHECK_EQ(c.WakeFromD1, 1, "wake from D1");
    CHECK_EQ(c.WakeFromD2, 1, "wake from D2");
    CHECK_EQ(c.WakeFromD3, 0, "no wake from D3");
    CHECK_EQ(c.DeviceWake, D2, "DeviceWake D2");
    CHECK_EQ(c.SystemWakeS0, 1, "SystemWake S0");

    XhciSuspCaps(1, 0, 0, &c);
    CHECK_EQ(c.Changed, 1, "no bit 5");
    CHECK_EQ(c.DeviceD1, 0, "no bit 5: no D1");
    CHECK_EQ(c.DeviceD2, 1, "no bit 5: D2");
    CHECK_EQ(c.WakeFromD2, 0, "no bit 5: no wake");
    CHECK_EQ(c.DeviceWake, 0, "no bit 5: DeviceWake unspecified");

    XhciSuspCaps(1, 1, 1, &c);
    CHECK_EQ(c.DeviceD2, 1, "9x wake off: D2");
    CHECK_EQ(c.WakeFromD2, 0, "9x wake off: no wake");
    CHECK_EQ(c.SystemWakeS0, 0, "9x wake off: no SystemWake");
}

static ULONG admit(ULONG eligible, ULONG closed, ULONG down, ULONG link,
                   ULONG sibD0, ULONG req, ULONG cur, PULONG ns)
{
    XHCI_SUSP_ADMIT_IN in;

    in.Eligible = eligible;
    in.Closed = closed;
    in.PowerDown = down;
    in.Link = link;
    in.SiblingD0 = sibD0;
    in.Requested = req;
    return XhciSuspAdmit(&in, cur, ns);
}

static void test_admit(void)
{
    ULONG ns;

    CHECK_EQ(admit(1, 1, 0, XHCI_SUSP_ACTIVE, 0, D2, D0, &ns),
             XHCI_SUSP_ADMIT_COMPLETE, "closed Dx: at once");
    CHECK_EQ(ns, D2, "closed Dx recorded");
    CHECK_EQ(admit(1, 1, 0, XHCI_SUSP_SUSPENDED, 0, D0, D2, &ns),
             XHCI_SUSP_ADMIT_COMPLETE, "closed D0: at once");
    CHECK_EQ(ns, D2, "closed D0: PwrState is the thread's");

    CHECK_EQ(admit(0, 0, 0, XHCI_SUSP_ACTIVE, 0, D2, D0, &ns),
             XHCI_SUSP_ADMIT_TODAY, "switch 0 / hub / SS: today");
    CHECK_EQ(ns, D2, "today records");

    CHECK_EQ(admit(1, 0, 0, XHCI_SUSP_ACTIVE, 1, D2, D0, &ns),
             XHCI_SUSP_ADMIT_COMPLETE, "Dx with a sibling D0: at once");
    CHECK_EQ(ns, D2, "Dx effective at once");
    CHECK_EQ(admit(1, 0, 1, XHCI_SUSP_ACTIVE, 0, D2, D0, &ns),
             XHCI_SUSP_ADMIT_COMPLETE, "Dx while PowerDown: at once");
    CHECK_EQ(admit(1, 0, 0, XHCI_SUSP_ACTIVE, 0, D2, D0, &ns),
             XHCI_SUSP_ADMIT_PEND, "the last Dx pends");
    CHECK_EQ(ns, D2, "the last Dx effective at once");
    CHECK_EQ(admit(1, 0, 0, XHCI_SUSP_SUSPENDED, 0, D3, D2, &ns),
             XHCI_SUSP_ADMIT_COMPLETE, "D2->D3 while SUSPENDED: at once");
    CHECK_EQ(ns, D3, "D3 recorded");
    CHECK_EQ(admit(1, 0, 0, XHCI_SUSP_RECOVERING, 0, D3, D2, &ns),
             XHCI_SUSP_ADMIT_COMPLETE, "Dx while RECOVERING: at once");

    CHECK_EQ(admit(1, 0, 0, XHCI_SUSP_ACTIVE, 0, D0, D2, &ns),
             XHCI_SUSP_ADMIT_COMPLETE, "D0 at ACTIVE: at once");
    CHECK_EQ(ns, D0, "D0 at ACTIVE written");
    CHECK_EQ(admit(1, 0, 0, XHCI_SUSP_SUSPENDING, 0, D0, D2, &ns),
             XHCI_SUSP_ADMIT_PEND, "D0 during SUSPENDING pends");
    CHECK_EQ(ns, D2, "pended D0 leaves PwrState");
    CHECK_EQ(admit(1, 0, 0, XHCI_SUSP_SUSPENDED, 0, D0, D2, &ns),
             XHCI_SUSP_ADMIT_PEND, "D0 while SUSPENDED pends");
    CHECK_EQ(admit(1, 0, 0, XHCI_SUSP_RESUMING, 0, D0, D2, &ns),
             XHCI_SUSP_ADMIT_PEND, "D0 while RESUMING pends");
    CHECK_EQ(admit(1, 0, 1, XHCI_SUSP_ACTIVE, 0, D0, D3, &ns),
             XHCI_SUSP_ADMIT_PEND, "D0 while PowerDown pends");

    CHECK_EQ(XhciSuspDxStale(4, 4), 0, "same epoch");
    CHECK_EQ(XhciSuspDxStale(4, 5), 1, "older epoch");
    CHECK_EQ(XhciSuspDxStale(0xFFFFFFFFUL, 0), 1, "epoch wrapped");
}

/* ------------------------------------------------------------------ */
/* The decision                                                        */
/* ------------------------------------------------------------------ */

static void test_decide_composite(void)
{
    XHCI_SUSP_LINK link;
    XHCI_SUSP_FUNC f[3];
    XHCI_SUSP_DECISION d;

    XhciSuspLinkInit(&link, 1, 7);
    CHECK_EQ(link.State, XHCI_SUSP_ACTIVE, "init ACTIVE");
    CHECK_EQ(link.Armed, 0, "init unarmed");

    /* Two functions: A went D2 while B was D0 (completed at once). */
    func_init(&f[0], D2);
    func_init(&f[1], D0);
    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "one function D0: nothing");
    CHECK_EQ(d.AllDx, 0, "not all Dx");

    /* B is the last to leave D0: its Dx pended. */
    f[1].PwrState = D2;
    f[1].PwrWanted = D2;
    f[1].DxPended = 1;
    f[1].DxEpoch = 1;
    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_SUSPEND,
             "last Dx: suspend");
    CHECK_EQ(d.AllDx, 1, "all Dx");
    CHECK_EQ(f[1].CompleteDx, 0, "the last Dx waits for the machine");
    CHECK_EQ(d.ArmIntent, 0, "no WAIT_WAKE: unarmed");

    /* Same pass, a WAIT_WAKE on a D2 function arms. */
    f[0].WaitWake = 1;
    (VOID)XhciSuspDecide(&link, f, 2, 1, 0, &d);
    CHECK_EQ(d.ArmIntent, 1, "WAIT_WAKE at D2 arms");
    /* A=D2 with WAIT_WAKE and B=D3 arms; a D3 waiter alone does not. */
    f[1].PwrState = D3;
    (VOID)XhciSuspDecide(&link, f, 2, 1, 0, &d);
    CHECK_EQ(d.ArmIntent, 1, "A D2 waiting, B D3: arms");
    f[0].WaitWake = 0;
    f[1].WaitWake = 1;
    (VOID)XhciSuspDecide(&link, f, 2, 1, 0, &d);
    CHECK_EQ(d.ArmIntent, 0, "only a D3 waiter: no intent");

    /* A sibling returned to D0 while the link stayed ACTIVE: the pended
     * Dx is no longer the last and completes. */
    func_init(&f[0], D0);
    func_init(&f[1], D2);
    f[1].DxPended = 1;
    f[1].DxEpoch = 1;
    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "sibling back to D0");
    CHECK_EQ(f[1].CompleteDx, 1, "not the last: completes");
    CHECK_EQ(d.Completions, 1, "completions flagged");

    /* A third PDO never powered down (no driver: D0) keeps it awake. */
    func_init(&f[0], D2);
    func_init(&f[1], D2);
    func_init(&f[2], D0);
    CHECK_EQ(XhciSuspDecide(&link, f, 3, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "a D0 PDO without a driver holds the device");

    CHECK_EQ(XhciSuspDecide(&link, f, 0, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "no PDOs: nothing");
}

static void test_decide_epoch_and_powerdown(void)
{
    XHCI_SUSP_LINK link;
    XHCI_SUSP_FUNC f[1];
    XHCI_SUSP_DECISION d;

    XhciSuspLinkInit(&link, 1, 1);
    func_init(&f[0], D2);
    f[0].DxPended = 1;
    f[0].DxEpoch = 3;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 4, 1, &d), XHCI_SUSP_DO_NONE,
             "PowerDown: nothing begun");
    CHECK_EQ(f[0].CompleteDx, 1, "older epoch: completed SUCCESS");

    f[0].DxEpoch = 4;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 4, 1, &d), XHCI_SUSP_DO_NONE,
             "PowerDown, current epoch: nothing");
    CHECK_EQ(f[0].CompleteDx, 0, "current epoch pends (PowerDown admits "
             "none: it pended before)");

    link.State = XHCI_SUSP_RECOVERING;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 4, 0, &d), XHCI_SUSP_DO_NONE,
             "RECOVERING: the recovery's");
    CHECK_EQ(f[0].CompleteDx, 1, "a Dx completes in every outcome");
}

static void test_decide_d0_during_suspending(void)
{
    XHCI_SUSP_LINK link;
    XHCI_SUSP_FUNC f[2];
    XHCI_SUSP_DECISION d;
    ULONG wait;
    ULONG step;

    XhciSuspLinkInit(&link, 1, 1);
    func_init(&f[0], D2);
    func_init(&f[1], D2);
    f[1].DxPended = 1;
    f[1].DxEpoch = 1;
    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_SUSPEND,
             "suspend");
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_SUSPEND, d.ArmIntent, 0,
                         1000, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_SUSPENDING, "SUSPENDING");

    /* A D0 for A arrives now: dispatch pends it. */
    f[0].PwrWanted = D0;
    f[0].D0Pended = 1;

    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1001, &wait); /* gate */
    CHECK_EQ(step, XHCI_SUSP_STEP_STOP, "unarmed: no arm step");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1002, &wait); /* stop */
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1003, &wait); /* U3 */
    CHECK_EQ(step, XHCI_SUSP_STEP_OBSERVE, "observe");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1004, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_END, "ended");
    CHECK_EQ(link.State, XHCI_SUSP_SUSPENDED, "SUSPENDED: it runs to its end");

    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_RESUME,
             "then the resume");
    CHECK_EQ(f[1].CompleteDx, 1, "the last Dx completes after the machine");
    CHECK_EQ(f[0].CompleteD0, 0, "D0 not before ACTIVE");

    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 0, 1005, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_SIGNAL, "signal");
    CHECK_EQ(wait, 9, "the dwell: 10 ms after U3 at 1004");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1014, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_U0, "root: U0 after TDRSMDN");
    CHECK_EQ(wait, XHCI_SUSP_TDRSMDN_MS, "TDRSMDN");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1034, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_VERIFY, "verify");
    CHECK_EQ(wait, XHCI_SUSP_VERIFY_MS, "verify bound");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1035, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_RECOVERY, "recovery");
    CHECK_EQ(wait, XHCI_SUSP_TRSMRCY_MS, "TRSMRCY");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1045, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_RESTART, "unarmed: restart");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1046, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_END, "end");
    CHECK_EQ(link.State, XHCI_SUSP_ACTIVE, "ACTIVE");
    CHECK_EQ(link.Outcome, XHCI_SUSP_OUT_ACTIVE, "outcome ACTIVE");

    f[1].DxPended = 0;
    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "ACTIVE again");
    CHECK_EQ(f[0].CompleteD0, 1, "the pended D0 completes at ACTIVE");
    CHECK_EQ(f[0].ToD0, 1, "and PwrState becomes D0");
    CHECK_EQ(d.AllDx, 0, "D0 counted in the same pass");
}

static void test_dwell(void)
{
    XHCI_SUSP_LINK link;
    ULONG wait;

    XhciSuspLinkInit(&link, 1, 1);
    link.State = XHCI_SUSP_SUSPENDED;
    link.U3Ms = 1000;
    (VOID)XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 0, 1020, &wait);
    CHECK_EQ(wait, 0, "dwell already out");
    link.State = XHCI_SUSP_SUSPENDED;
    (VOID)XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 0, 1000, &wait);
    CHECK_EQ(wait, 10, "dwell from U3 itself");
    link.State = XHCI_SUSP_SUSPENDED;
    link.U3Ms = 0xFFFFFFFCUL;
    (VOID)XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 0, 2, &wait);
    CHECK_EQ(wait, 4, "dwell across the clock's wrap");
    link.State = XHCI_SUSP_SUSPENDED;
    link.U3Ms = 5000;
    (VOID)XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 0, 100, &wait);
    CHECK_EQ(wait, 10, "a clock behind U3 waits the whole dwell");
}

/* Run a suspend to `failStep`, report `result` there, return the step the
 * machine answers. `arm` makes the arm step run. */
static ULONG run_suspend_to(PXHCI_SUSP_LINK link, ULONG arm, ULONG failStep,
                            ULONG result, PULONG wait)
{
    ULONG step;
    ULONG now;

    XhciSuspLinkInit(link, 1, 1);
    now = 100;
    step = XhciSuspBegin(link, XHCI_SUSP_RUN_SUSPEND, arm, 0, now, wait);
    while (step != XHCI_SUSP_STEP_END && step != failStep) {
        now++;
        step = XhciSuspAdvance(link, XHCI_SUSP_RES_OK, now, wait);
    }
    if (step == XHCI_SUSP_STEP_END) {
        return step;
    }
    return XhciSuspAdvance(link, result, now + 1, wait);
}

static void test_suspend_outcomes(void)
{
    XHCI_SUSP_LINK link;
    ULONG wait;
    ULONG step;
    ULONG s;
    static const ULONG steps[] = {
        XHCI_SUSP_STEP_GATE, XHCI_SUSP_STEP_ARM, XHCI_SUSP_STEP_STOP,
        XHCI_SUSP_STEP_U3, XHCI_SUSP_STEP_OBSERVE
    };

    /* The whole sequence, armed. */
    XhciSuspLinkInit(&link, 1, 1);
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_SUSPEND, 1, 0, 10, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_GATE, "gate first");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 11, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_ARM, "armed: the arm, while U0");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 12, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_STOP, "stop");
    CHECK_EQ(link.Armed, 1, "armed");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 13, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_U3, "U3");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 14, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_OBSERVE, "observe");
    CHECK_EQ(wait, XHCI_SUSP_OBSERVE_MS, "bounded 50 ms");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 20, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_END, "end");
    CHECK_EQ(link.State, XHCI_SUSP_SUSPENDED, "SUSPENDED");
    CHECK_EQ(link.Outcome, XHCI_SUSP_OUT_SUSPENDED, "outcome");
    CHECK_EQ(link.U3Ms, 20, "U3 time noted");
    CHECK_EQ(XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 21, &wait),
             XHCI_SUSP_STEP_END, "advance past the end stays at END");

    /* Refusals. */
    step = run_suspend_to(&link, 1, XHCI_SUSP_STEP_ARM, XHCI_SUSP_RES_STALL,
                          &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_RESTART, "arm STALL: restart");
    CHECK_EQ(wait, 0, "nothing stopped: no SP=1 wait");
    CHECK_EQ(link.Reason, XHCI_SUSP_WHY_STALL, "reason STALL");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 200, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_ACTIVE, "refused: ACTIVE");
    CHECK_EQ(link.Outcome, XHCI_SUSP_OUT_REFUSED, "outcome REFUSED");
    CHECK_EQ(link.LatchDue, 1, "a refusal latches");
    CHECK_EQ(link.Armed, 0, "a STALLed arm is not armed");

    step = run_suspend_to(&link, 1, XHCI_SUSP_STEP_ARM,
                          XHCI_SUSP_RES_NOT_SENT, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_ARM, "NOT_SENT: retried once");
    CHECK_EQ(wait, XHCI_SUSP_BUSY_RETRY_MS, "after the ring drains, 100 ms");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 300, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_STOP, "the retry succeeded");
    step = run_suspend_to(&link, 1, XHCI_SUSP_STEP_ARM,
                          XHCI_SUSP_RES_NOT_SENT, &wait);
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_NOT_SENT, 300, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_RESTART, "NOT_SENT twice: refused");
    CHECK_EQ(link.Reason, XHCI_SUSP_WHY_BUSY, "reason BUSY");

    step = run_suspend_to(&link, 1, XHCI_SUSP_STEP_ARM,
                          XHCI_SUSP_RES_FAILED, &wait);
    CHECK_EQ(link.Reason, XHCI_SUSP_WHY_FAILED, "arm failed: refused");

    step = run_suspend_to(&link, 1, XHCI_SUSP_STEP_STOP,
                          XHCI_SUSP_RES_FAILED, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_RESTART, "stop failed: restart");
    CHECK_EQ(wait, XHCI_SUSP_STOP_MIN_MS, "after SP=1's 10 ms");
    CHECK_EQ(link.Armed, 1, "a refusal after the arm keeps Armed");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 400, &wait);
    CHECK_EQ(link.Outcome, XHCI_SUSP_OUT_REFUSED, "refused");

    step = run_suspend_to(&link, 0, XHCI_SUSP_STEP_U3, XHCI_SUSP_RES_FAILED,
                          &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_RESTART, "U3 refused: restart");
    CHECK(wait <= XHCI_SUSP_STOP_MIN_MS && wait > 0,
          "U3 refused: the rest of SP=1's minimum");

    /* Uncertain outcomes: RECOVERING, the recovery owns the device. */
    step = run_suspend_to(&link, 1, XHCI_SUSP_STEP_ARM,
                          XHCI_SUSP_RES_TIMEOUT, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_RECOVERING, "arm timeout: RECOVERING");
    CHECK_EQ(link.Outcome, XHCI_SUSP_OUT_UNCERTAIN, "uncertain");
    step = run_suspend_to(&link, 0, XHCI_SUSP_STEP_STOP,
                          XHCI_SUSP_RES_TIMEOUT, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_RECOVERING, "stop timeout");
    step = run_suspend_to(&link, 0, XHCI_SUSP_STEP_U3,
                          XHCI_SUSP_RES_TIMEOUT, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_RECOVERING, "U3 request timeout");
    step = run_suspend_to(&link, 1, XHCI_SUSP_STEP_OBSERVE,
                          XHCI_SUSP_RES_TIMEOUT, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_RECOVERING, "U3 not observed");
    CHECK_EQ(link.Armed, 0, "a recovery resets the feature");
    XhciSuspRecovered(&link, 1);
    CHECK_EQ(link.State, XHCI_SUSP_ACTIVE, "recovered: ACTIVE");
    CHECK_EQ(link.LatchDue, 1, "recovered: ends as a refusal");
    step = run_suspend_to(&link, 0, XHCI_SUSP_STEP_OBSERVE,
                          XHCI_SUSP_RES_TIMEOUT, &wait);
    XhciSuspRecovered(&link, 0);
    CHECK_EQ(link.State, XHCI_SUSP_GONE, "not recovered: GONE");

    /* A disconnect at every step: GONE. */
    for (s = 0; s < sizeof(steps) / sizeof(steps[0]); s++) {
        step = run_suspend_to(&link, 1, steps[s], XHCI_SUSP_RES_DISCONNECT,
                              &wait);
        CHECK_EQ(step, XHCI_SUSP_STEP_END, "disconnect ends");
        CHECK_EQ(link.State, XHCI_SUSP_GONE, "disconnect: GONE");
        CHECK_EQ(link.Outcome, XHCI_SUSP_OUT_GONE, "outcome GONE");
    }
}

static void test_wake_at_observe(void)
{
    XHCI_SUSP_LINK link;
    ULONG wait;
    ULONG step;

    /* Root: Resume observed - suspension happened, the device wakes. */
    step = run_suspend_to(&link, 1, XHCI_SUSP_STEP_OBSERVE,
                          XHCI_SUSP_RES_WAKE, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_RESUMING, "wake at observe: RESUMING");
    CHECK_EQ(link.Kind, XHCI_SUSP_RUN_WAKE, "device-initiated");
    CHECK_EQ(link.ClaimDue, 1, "the wake event: claim now");
    CHECK_EQ(step, XHCI_SUSP_STEP_U0, "root: U0 after TDRSMDN");
    CHECK_EQ(wait, XHCI_SUSP_TDRSMDN_MS, "TDRSMDN from the PLC");
    XhciSuspWakeCount(&link, 1);
    CHECK_EQ(link.ClaimDue, 0, "claimed");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 500, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_VERIFY, "verify U0");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 501, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_RECOVERY, "TRSMRCY");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 511, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_DISARM, "armed: disarm after U0");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 512, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_RESTART, "restart");
    CHECK_EQ(link.Armed, 0, "disarmed");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 513, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_ACTIVE, "ACTIVE");
    CHECK_EQ(link.LatchDue, 0, "a wake does not latch: WokeHold holds");

    /* Evidence kept while SUSPENDING (a hub's C_PORT_SUSPEND, already
     * acknowledged) is taken once the U3 request is done, without being
     * seen again at an observation (round 1, finding 7). */
    XhciSuspLinkInit(&link, 0, 1);
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_SUSPEND, 1, 0, 0, &wait);
    CHECK_EQ(XhciSuspEvidence(&link, XHCI_SUSP_EV_WAKE), 0,
             "SUSPENDING: kept, not yet the event");
    CHECK_EQ(link.WakeKept, 1, "kept");
    while (step != XHCI_SUSP_STEP_U3) {
        step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1, &wait);
    }
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 2, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_RESUMING, "woken: RESUMING, no observation");
    CHECK_EQ(step, XHCI_SUSP_STEP_RECOVERY, "hub: the hub timed it");
    CHECK_EQ(link.ClaimDue, 1, "the event taken");

    /* A disconnect kept while SUSPENDING outranks a kept wake. */
    XhciSuspLinkInit(&link, 1, 1);
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_SUSPEND, 0, 0, 0, &wait);
    (VOID)XhciSuspEvidence(&link, XHCI_SUSP_EV_WAKE);
    (VOID)XhciSuspEvidence(&link, XHCI_SUSP_EV_DISCONNECT);
    CHECK_EQ(link.State, XHCI_SUSP_SUSPENDING, "kept for the step");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_END, "the next step ends it");
    CHECK_EQ(link.State, XHCI_SUSP_GONE, "GONE, not woken");

    /* Hub: PORT_SUSPEND clear with C_PORT_SUSPEND at the observation. */
    XhciSuspLinkInit(&link, 0, 1);
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_SUSPEND, 0, 0, 0, &wait);
    while (step != XHCI_SUSP_STEP_OBSERVE) {
        step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1, &wait);
    }
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_WAKE, 2, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_RECOVERY, "hub: the hub timed it");
    CHECK_EQ(wait, XHCI_SUSP_TRSMRCY_MS, "TRSMRCY");
    XhciSuspWakeCount(&link, 0);
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 12, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_RESTART, "unarmed: restart");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 13, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_ACTIVE, "ACTIVE");
    CHECK_EQ(link.LatchDue, 0, "an unclaimed wake does not latch");
}

static void test_resume_outcomes(void)
{
    XHCI_SUSP_LINK link;
    ULONG wait;
    ULONG step;

    /* Hub, host-initiated, armed. */
    XhciSuspLinkInit(&link, 0, 1);
    link.State = XHCI_SUSP_SUSPENDED;
    link.Armed = 1;
    link.U3Ms = 0;
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 0, 50, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_SIGNAL, "hub: ClearPortFeature");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 51, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_VERIFY, "hub: no U0 write");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 80, &wait);
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 90, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_DISARM, "disarm");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_STALL, 91, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_RESTART, "a refused disarm: the resume goes on");
    CHECK_EQ(link.Armed, 1, "and the feature may still be set");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 92, &wait);
    CHECK_EQ(link.Outcome, XHCI_SUSP_OUT_ACTIVE, "ACTIVE");
    CHECK_EQ(link.LatchDue, 0, "a host resume does not latch");

    /* A disarm timeout is uncertain. */
    XhciSuspLinkInit(&link, 1, 1);
    link.State = XHCI_SUSP_SUSPENDED;
    link.Armed = 1;
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 0, 50, &wait);
    while (step != XHCI_SUSP_STEP_DISARM) {
        step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 60, &wait);
    }
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_TIMEOUT, 61, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_RECOVERING, "disarm timeout");

    /* A resume not verified. */
    XhciSuspLinkInit(&link, 1, 1);
    link.State = XHCI_SUSP_SUSPENDED;
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 0, 50, &wait);
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 60, &wait);
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 80, &wait);
    CHECK_EQ(step, XHCI_SUSP_STEP_VERIFY, "verify");
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_TIMEOUT, 130, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_RECOVERING, "not verified: uncertain");
    XhciSuspLinkInit(&link, 1, 1);
    link.State = XHCI_SUSP_SUSPENDED;
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 0, 50, &wait);
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_FAILED, 60, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_RECOVERING, "signal failed: uncertain");
    XhciSuspLinkInit(&link, 1, 1);
    link.State = XHCI_SUSP_SUSPENDED;
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 0, 50, &wait);
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 60, &wait);
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_DISCONNECT, 70, &wait);
    CHECK_EQ(link.State, XHCI_SUSP_GONE, "unplug while resuming: GONE");
}

static void test_latch_and_rearm(void)
{
    XHCI_SUSP_LINK link;
    XHCI_SUSP_FUNC f[1];
    XHCI_SUSP_DECISION d;
    ULONG wait;
    ULONG step;

    /* A refusal: no retry until the slots change. */
    XhciSuspLinkInit(&link, 1, 1);
    func_init(&f[0], D2);
    f[0].DxPended = 1;
    f[0].DxEpoch = 1;
    f[0].WaitWake = 1;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_SUSPEND,
             "suspend");
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_SUSPEND, d.ArmIntent, 0, 0,
                         &wait);
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 1, &wait);
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_STALL, 2, &wait);
    step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 3, &wait);
    CHECK_EQ(link.Outcome, XHCI_SUSP_OUT_REFUSED, "refused");
    /* The STALL also completes the WAIT_WAKE NOT_SUPPORTED: the executor's. */
    f[0].WaitWake = 0;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "latched: no retry");
    CHECK_EQ(f[0].CompleteDx, 1, "the pended Dx completes: logical Dx");
    f[0].DxPended = 0;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "still latched");
    /* A D0 and a fresh D2 between two passes: the states read as they did,
     * but the slot's Seq moved twice (round 1, finding 3). */
    f[0].Seq += 2;
    f[0].DxPended = 1;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_SUSPEND,
             "a D0 and a Dx in between: suspend again");
    CHECK_EQ(link.Latched, 0, "latch cleared");
    /* Latched again, then the function goes D3. */
    link.LatchDue = 1;
    f[0].DxPended = 0;
    (VOID)XhciSuspDecide(&link, f, 1, 1, 0, &d);
    f[0].PwrState = D3;
    f[0].PwrWanted = D3;
    f[0].DxPended = 1;
    f[0].Seq++;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_SUSPEND,
             "slots changed: suspend again");
    /* A change while not all Dx spends the latch too. */
    link.LatchDue = 1;
    f[0].DxPended = 0;
    (VOID)XhciSuspDecide(&link, f, 1, 1, 0, &d);
    f[0].PwrState = D0;
    f[0].PwrWanted = D0;
    f[0].Seq++;
    (VOID)XhciSuspDecide(&link, f, 1, 1, 0, &d);
    CHECK_EQ(link.Latched, 0, "spent by a D0");

    /* Rearm: a WAIT_WAKE accepted while SUSPENDED unarmed. */
    XhciSuspLinkInit(&link, 1, 1);
    func_init(&f[0], D2);
    link.State = XHCI_SUSP_SUSPENDED;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "suspended, nothing wanted");
    f[0].WaitWake = 1;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_REARM,
             "unarmed but now wanted: rearm");
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 1, 100, &wait);
    while (step != XHCI_SUSP_STEP_END) {
        step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 200, &wait);
    }
    CHECK_EQ(link.State, XHCI_SUSP_ACTIVE, "resumed to arm");
    CHECK_EQ(link.Internal, 1, "an internal cycle");
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_SUSPEND,
             "every function still Dx: suspend again, armed");
    CHECK_EQ(d.ArmIntent, 1, "armed this time");
    /* Armed and a waiter cancelled: stays suspended and armed. */
    link.State = XHCI_SUSP_SUSPENDED;
    link.Armed = 1;
    f[0].WaitWake = 0;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "armed but no longer wanted: left armed");

    /* An unclaimed wake: resume, disarm, suspend again on the same pass. */
    link.WakeTaken = 0;
    CHECK_EQ(XhciSuspEvidence(&link, XHCI_SUSP_EV_WAKE), 1, "the event");
    XhciSuspWakeCount(&link, 0);
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_WAKE, 0, 1, 300, &wait);
    CHECK_EQ(link.ClaimDue, 0, "claim already counted");
    while (step != XHCI_SUSP_STEP_END) {
        if (step == XHCI_SUSP_STEP_DISARM) {
            CHECK_EQ(link.Armed, 1, "disarm runs while armed");
        }
        step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 400, &wait);
    }
    CHECK_EQ(link.Armed, 0, "disarmed");
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_SUSPEND,
             "unclaimed: suspend again");
    CHECK_EQ(d.ArmIntent, 0, "intent recomputed: none");
}

static void test_woke_hold(void)
{
    XHCI_SUSP_LINK link;
    XHCI_SUSP_FUNC f[2];
    XHCI_SUSP_DECISION d;
    ULONG wait;
    ULONG step;

    XhciSuspLinkInit(&link, 1, 1);
    func_init(&f[0], D2);
    func_init(&f[1], D2);
    f[0].WaitWake = 1;
    link.State = XHCI_SUSP_SUSPENDED;
    link.Armed = 1;
    CHECK_EQ(XhciSuspEvidence(&link, XHCI_SUSP_EV_WAKE), 1, "wake event");
    CHECK_EQ(XhciSuspEvidence(&link, XHCI_SUSP_EV_WAKE), 0,
             "one event per suspension");
    /* Claimed: A's WAIT_WAKE completed SUCCESS at once, its hold set. */
    f[0].WaitWake = 0;
    f[0].WokeHold = 1;
    XhciSuspWakeCount(&link, 1);
    CHECK_EQ(link.ClaimDue, 0, "counted");
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_WAKE, 0, 0, 0, &wait);
    CHECK_EQ(XhciSuspEvidence(&link, XHCI_SUSP_EV_WAKE), 0,
             "repeated while RESUMING: ignored");
    while (step != XHCI_SUSP_STEP_END) {
        step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 100, &wait);
    }
    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "claimed wake: no re-suspend before its D0");
    CHECK_EQ(f[0].WokeHold, 1, "held");
    /* A sibling's change does not release it (round 1, finding 2). */
    f[1].PwrState = D3;
    f[1].PwrWanted = D3;
    f[1].Seq++;
    f[1].DxPended = 1;
    f[1].DxEpoch = 1;
    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "a sibling's D3: still held");
    CHECK_EQ(f[1].CompleteDx, 1, "its Dx completes: logical Dx");
    f[1].DxPended = 0;
    /* The woken function's completion routine sends D0: at once. */
    f[0].PwrState = D0;
    f[0].PwrWanted = D0;
    f[0].Seq++;
    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "D0: active");
    CHECK_EQ(f[0].WokeHold, 0, "the hold ends at its D0");
    /* Later it idles again. */
    f[0].PwrState = D2;
    f[0].PwrWanted = D2;
    f[0].DxPended = 1;
    f[0].DxEpoch = 1;
    f[0].Seq++;
    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_SUSPEND,
             "and suspends again");

    /* A woken function closed instead: the hold ends. */
    func_init(&f[0], D2);
    f[0].WokeHold = 1;
    f[0].Closed = 1;
    func_init(&f[1], D2);
    XhciSuspLinkInit(&link, 1, 1);
    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_SUSPEND,
             "closed: hold ended");
    CHECK_EQ(f[0].WokeHold, 0, "cleared");
}

static void test_powerdown_d0(void)
{
    XHCI_SUSP_LINK link;
    XHCI_SUSP_FUNC f[1];
    XHCI_SUSP_DECISION d;

    /* A D0 pended while the controller goes down is not completed by the
     * walker that retires old-epoch Dx (round 1, finding 6). */
    XhciSuspLinkInit(&link, 1, 1);
    func_init(&f[0], D2);
    f[0].PwrWanted = D0;
    f[0].D0Pended = 1;
    (VOID)XhciSuspDecide(&link, f, 1, 2, 1, &d);
    CHECK_EQ(f[0].CompleteD0, 0, "PowerDown: D0 pends");
    CHECK_EQ(f[0].ToD0, 0, "PowerDown: PwrState stays");
    (VOID)XhciSuspDecide(&link, f, 1, 2, 0, &d);
    CHECK_EQ(f[0].CompleteD0, 1, "powered again: completed");
}

static void test_start_reopen(void)
{
    XHCI_SUSP_LINK link;
    XHCI_SUSP_FUNC f[1];
    XHCI_SUSP_DECISION d;
    ULONG wait;
    ULONG step;

    /* A function disabled (its D3 suspended the link) and enabled: START
     * writes PwrWanted D0, PwrState stays D3, no IRP. */
    XhciSuspLinkInit(&link, 1, 1);
    link.State = XHCI_SUSP_SUSPENDED;
    func_init(&f[0], D3);
    f[0].PwrWanted = D0;
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_RESUME,
             "START on a suspended device: resume");
    CHECK_EQ(f[0].CompleteD0, 0, "no IRP to complete");
    step = XhciSuspBegin(&link, XHCI_SUSP_RUN_RESUME, 0, 0, 100, &wait);
    while (step != XHCI_SUSP_STEP_END) {
        step = XhciSuspAdvance(&link, XHCI_SUSP_RES_OK, 200, &wait);
    }
    CHECK_EQ(XhciSuspDecide(&link, f, 1, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "ACTIVE");
    CHECK_EQ(f[0].ToD0, 1, "PwrState D0 at ACTIVE");
    CHECK_EQ(f[0].CompleteD0, 0, "still no IRP");
}

static void test_gone(void)
{
    XHCI_SUSP_LINK link;
    XHCI_SUSP_FUNC f[2];
    XHCI_SUSP_DECISION d;

    XhciSuspLinkInit(&link, 1, 1);
    link.State = XHCI_SUSP_SUSPENDED;
    func_init(&f[0], D2);
    f[0].PwrWanted = D0;
    f[0].D0Pended = 1;
    func_init(&f[1], D2);
    f[1].DxPended = 1;
    f[1].DxEpoch = 1;
    CHECK_EQ(XhciSuspEvidence(&link, XHCI_SUSP_EV_DISCONNECT), 0,
             "disconnect");
    CHECK_EQ(link.State, XHCI_SUSP_GONE, "GONE");
    CHECK_EQ(XhciSuspDecide(&link, f, 2, 1, 0, &d), XHCI_SUSP_DO_NONE,
             "GONE: nothing begun");
    CHECK_EQ(f[0].CompleteD0, 1, "D0 completes on GONE");
    CHECK_EQ(f[0].ToD0, 0, "never powered");
    CHECK_EQ(f[1].CompleteDx, 1, "Dx completes on GONE");

    /* A disconnect while a sequence runs is its step's to report. */
    XhciSuspLinkInit(&link, 1, 1);
    link.Step = XHCI_SUSP_STEP_STOP;
    link.State = XHCI_SUSP_SUSPENDING;
    (VOID)XhciSuspEvidence(&link, XHCI_SUSP_EV_DISCONNECT);
    CHECK_EQ(link.State, XHCI_SUSP_SUSPENDING, "left to the step");
    XhciSuspGone(&link);
    CHECK_EQ(link.State, XHCI_SUSP_GONE, "gone");
}

static void test_digest(void)
{
    XHCI_SUSP_FUNC a[2];
    XHCI_SUSP_FUNC b[2];
    ULONG da;

    func_init(&a[0], D2);
    func_init(&a[1], D3);
    memcpy(b, a, sizeof(a));
    da = XhciSuspDigest(a, 2);
    CHECK_EQ(XhciSuspDigest(b, 2), da, "same slots, same digest");
    b[0].DxPended = 1;
    b[0].DxEpoch = 9;
    CHECK_EQ(XhciSuspDigest(b, 2), da, "a pended Dx is not a change");
    b[0].PwrState = D3;
    CHECK(XhciSuspDigest(b, 2) != da, "PwrState is");
    memcpy(b, a, sizeof(a));
    b[1].WaitWake = 1;
    CHECK(XhciSuspDigest(b, 2) != da, "a WAIT_WAKE is");
    memcpy(b, a, sizeof(a));
    b[0].D0Pended = 1;
    CHECK(XhciSuspDigest(b, 2) != da, "a pended D0 is");
    b[0].D0Pended = 0;
    b[0].PwrWanted = D0;
    CHECK(XhciSuspDigest(b, 2) != da, "PwrWanted is");
    b[0] = a[1];
    b[1] = a[0];
    CHECK(XhciSuspDigest(b, 2) != da, "which PDO is which");
}

/* ------------------------------------------------------------------ */
/* Evidence                                                            */
/* ------------------------------------------------------------------ */

static void test_root_evidence(void)
{
    XHCI_SUSP_LINK link;
    ULONG plc;

    plc = XHCI_PORTSC_PLC;
    XhciSuspLinkInit(&link, 1, 5);
    link.State = XHCI_SUSP_SUSPENDED;

    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_RESUME), plc, 5),
             XHCI_SUSP_EV_WAKE, "PLC + Resume while SUSPENDED: wake");
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_RESUME),
                                  plc | XHCI_PORTSC_CSC, 5),
             XHCI_SUSP_EV_DISCONNECT, "Resume with CSC: disconnect");
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_RESUME) &
                                         ~XHCI_PORTSC_CCS, plc, 5),
             XHCI_SUSP_EV_DISCONNECT, "lost CCS");
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_RESUME) &
                                         ~XHCI_PORTSC_PED,
                                  plc | XHCI_PORTSC_PEC, 5),
             XHCI_SUSP_EV_DISCONNECT, "a port error");
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_RESUME), plc, 6),
             XHCI_SUSP_EV_STALE, "another generation: stale");
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_RESUME) &
                                         ~XHCI_PORTSC_CCS, plc, 6),
             XHCI_SUSP_EV_DISCONNECT, "a disconnect outranks the generation");
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_U3), plc, 5),
             XHCI_SUSP_EV_SUSPEND_ENTRY, "PLC + U3: suspend entry");
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_RESUME), 0, 5),
             XHCI_SUSP_EV_IGNORED, "Resume without PLC: nothing");
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_U0), plc, 5),
             XHCI_SUSP_EV_IGNORED, "U0 while SUSPENDED: stale");

    link.State = XHCI_SUSP_SUSPENDING;
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_RESUME), plc, 5),
             XHCI_SUSP_EV_WAKE, "Resume while SUSPENDING: wake");
    link.State = XHCI_SUSP_RESUMING;
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_U0), plc, 5),
             XHCI_SUSP_EV_RESUME_DONE, "U0 while RESUMING: done");
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_RESUME), plc, 5),
             XHCI_SUSP_EV_IGNORED, "Resume while RESUMING: ignored");
    link.State = XHCI_SUSP_ACTIVE;
    CHECK_EQ(XhciSuspRootEvidence(&link, portsc(XHCI_PLS_RESUME), plc, 5),
             XHCI_SUSP_EV_IGNORED, "Resume while ACTIVE: not the machine's");

    CHECK_EQ(XhciSuspObserveRoot(portsc(XHCI_PLS_U3)), XHCI_SUSP_RES_OK,
             "observe U3");
    CHECK_EQ(XhciSuspObserveRoot(portsc(XHCI_PLS_RESUME)),
             XHCI_SUSP_RES_WAKE, "observe Resume");
    CHECK_EQ(XhciSuspObserveRoot(portsc(XHCI_PLS_U0)), XHCI_SUSP_POLL_AGAIN,
             "observe U0: again");
    CHECK_EQ(XhciSuspObserveRoot(portsc(XHCI_PLS_U3) | XHCI_PORTSC_CSC),
             XHCI_SUSP_RES_DISCONNECT, "observe CSC");
    CHECK_EQ(XhciSuspObserveRoot(portsc(XHCI_PLS_U3) & ~XHCI_PORTSC_PED),
             XHCI_SUSP_RES_DISCONNECT, "observe disabled");
    CHECK_EQ(XhciSuspVerifyRoot(portsc(XHCI_PLS_U0)), XHCI_SUSP_RES_OK,
             "verify U0");
    CHECK_EQ(XhciSuspVerifyRoot(portsc(XHCI_PLS_U3)), XHCI_SUSP_POLL_AGAIN,
             "verify U3 (QEMU ignores the Resume write): again");
    CHECK_EQ(XhciSuspVerifyRoot(portsc(XHCI_PLS_RESUME)),
             XHCI_SUSP_POLL_AGAIN, "verify Resume: again");
    CHECK_EQ(XhciSuspVerifyRoot(portsc(XHCI_PLS_U0) & ~XHCI_PORTSC_CCS),
             XHCI_SUSP_RES_DISCONNECT, "verify unplugged");
}

static void test_hub_evidence(void)
{
    XHCI_SUSP_LINK link;
    ULONG cs;

    cs = XHCI_HUB_C_PORT_SUSPEND;
    XhciSuspLinkInit(&link, 0, 3);
    link.State = XHCI_SUSP_SUSPENDED;
    CHECK_EQ(XhciSuspHubEvidence(&link, HUB_OK, cs, 3), XHCI_SUSP_EV_WAKE,
             "C_PORT_SUSPEND, suspend clear, SUSPENDED: a remote wake");
    CHECK_EQ(XhciSuspHubEvidence(&link, HUB_OK | XHCI_HUB_PORT_SUSPEND, cs,
                                 3),
             XHCI_SUSP_EV_IGNORED, "C_PORT_SUSPEND with suspend set");
    CHECK_EQ(XhciSuspHubEvidence(&link, HUB_OK,
                                 cs | XHCI_HUB_C_PORT_CONNECTION, 3),
             XHCI_SUSP_EV_DISCONNECT, "connection change: disconnect");
    CHECK_EQ(XhciSuspHubEvidence(&link, HUB_OK & ~XHCI_HUB_PORT_ENABLE,
                                 cs | XHCI_HUB_C_PORT_ENABLE, 3),
             XHCI_SUSP_EV_DISCONNECT, "disabled by the hub");
    CHECK_EQ(XhciSuspHubEvidence(&link, HUB_OK & ~XHCI_HUB_PORT_CONNECTION,
                                 0, 3),
             XHCI_SUSP_EV_DISCONNECT, "not connected");
    CHECK_EQ(XhciSuspHubEvidence(&link, HUB_OK, cs, 4),
             XHCI_SUSP_EV_STALE, "another generation");
    link.State = XHCI_SUSP_RESUMING;
    CHECK_EQ(XhciSuspHubEvidence(&link, HUB_OK, cs, 3),
             XHCI_SUSP_EV_RESUME_DONE, "C_PORT_SUSPEND while RESUMING");
    link.State = XHCI_SUSP_SUSPENDING;
    CHECK_EQ(XhciSuspHubEvidence(&link, HUB_OK | XHCI_HUB_PORT_SUSPEND, 0,
                                 3),
             XHCI_SUSP_EV_SUSPEND_ENTRY, "suspend set while SUSPENDING");
    CHECK_EQ(XhciSuspHubEvidence(&link, HUB_OK, cs, 3), XHCI_SUSP_EV_WAKE,
             "a wake that finished while SUSPENDING");
    link.State = XHCI_SUSP_ACTIVE;
    CHECK_EQ(XhciSuspHubEvidence(&link, HUB_OK, cs, 3),
             XHCI_SUSP_EV_IGNORED, "ACTIVE: today's handling");

    CHECK_EQ(XhciSuspObserveHub(HUB_OK | XHCI_HUB_PORT_SUSPEND, 0),
             XHCI_SUSP_RES_OK, "observe PORT_SUSPEND");
    CHECK_EQ(XhciSuspObserveHub(HUB_OK, cs), XHCI_SUSP_RES_WAKE,
             "observe clear with C_PORT_SUSPEND: woke");
    CHECK_EQ(XhciSuspObserveHub(HUB_OK, 0), XHCI_SUSP_POLL_AGAIN,
             "observe clear, no change: again");
    CHECK_EQ(XhciSuspObserveHub(HUB_OK, XHCI_HUB_C_PORT_CONNECTION),
             XHCI_SUSP_RES_DISCONNECT, "observe connection change");
    CHECK_EQ(XhciSuspVerifyHub(HUB_OK, cs), XHCI_SUSP_RES_OK,
             "verify with C_PORT_SUSPEND");
    CHECK_EQ(XhciSuspVerifyHub(HUB_OK, 0), XHCI_SUSP_RES_OK,
             "verify by a poll that reads it clear");
    CHECK_EQ(XhciSuspVerifyHub(HUB_OK | XHCI_HUB_PORT_SUSPEND, 0),
             XHCI_SUSP_POLL_AGAIN, "verify still suspended");
    CHECK_EQ(XhciSuspVerifyHub(HUB_OK & ~XHCI_HUB_PORT_ENABLE, 0),
             XHCI_SUSP_RES_DISCONNECT, "verify disabled");
}

static void test_wake_claims(void)
{
    CHECK_EQ(XhciSuspWakeClaims(4, 5, D2), 1, "stamped before: claimed");
    CHECK_EQ(XhciSuspWakeClaims(5, 5, D2), 0, "stamped at: after the event");
    CHECK_EQ(XhciSuspWakeClaims(6, 5, D2), 0, "stamped after");
    CHECK_EQ(XhciSuspWakeClaims(4, 5, D1), 1, "D1");
    CHECK_EQ(XhciSuspWakeClaims(4, 5, D3), 0, "D3 contributes none");
    CHECK_EQ(XhciSuspWakeClaims(4, 5, D0), 0, "D0 not waiting");
    CHECK_EQ(XhciSuspWakeClaims(0xFFFFFFF0UL, 5, D2), 1,
             "across the wrap: claimed");
    CHECK_EQ(XhciSuspWakeClaims(5, 0xFFFFFFF0UL, D2), 0,
             "across the wrap: after");
}

/* ------------------------------------------------------------------ */
/* WAIT_WAKE and the idle notification                                 */
/* ------------------------------------------------------------------ */

static ULONG ww(ULONG rel, ULONG el, ULONG rw, ULONG off9x, ULONG closed,
                ULONG sys, ULONG pwr, ULONG held)
{
    XHCI_SUSP_WW_IN in;

    in.Released = rel;
    in.Eligible = el;
    in.RemoteWake = rw;
    in.WakeOff9x = off9x;
    in.Closed = closed;
    in.SystemState = sys;
    in.PwrState = pwr;
    in.Held = held;
    return XhciSuspWaitWakeAdmit(&in);
}

static void test_wait_wake(void)
{
    CHECK_EQ(ww(0, 1, 1, 0, 0, 1, D0, 0), XHCI_SUSP_ST_PENDING, "accepted");
    CHECK_EQ(ww(0, 1, 1, 0, 0, 1, D2, 0), XHCI_SUSP_ST_PENDING,
             "accepted while D2");
    CHECK_EQ(ww(1, 1, 1, 0, 0, 1, D0, 0), XHCI_SUSP_ST_NO_DEVICE,
             "controller released");
    CHECK_EQ(ww(0, 0, 1, 0, 0, 1, D0, 0), XHCI_SUSP_ST_NOT_SUPPORTED,
             "not eligible");
    CHECK_EQ(ww(0, 1, 0, 0, 0, 1, D0, 0), XHCI_SUSP_ST_NOT_SUPPORTED,
             "no remote-wake bit");
    CHECK_EQ(ww(0, 1, 1, 1, 0, 1, D0, 0), XHCI_SUSP_ST_NOT_SUPPORTED,
             "9x wake off");
    CHECK_EQ(ww(0, 1, 1, 0, 1, 1, D0, 0), XHCI_SUSP_ST_NOT_SUPPORTED,
             "PwrClosed");
    CHECK_EQ(ww(0, 1, 1, 0, 0, 2, D0, 0), XHCI_SUSP_ST_INVALID_STATE,
             "deeper than SystemWake");
    CHECK_EQ(ww(0, 1, 1, 0, 0, 1, D3, 0), XHCI_SUSP_ST_INVALID_STATE,
             "PwrState D3");
    CHECK_EQ(ww(0, 1, 1, 0, 0, 1, D0, 1), XHCI_SUSP_ST_BUSY,
             "a second on the PDO");
}

static void test_idle_submit_and_batch(void)
{
    XHCI_SUSP_IDLE_FUNC f[3];

    CHECK_EQ(XhciSuspIdleSubmit(0, 1, 0, 0), XHCI_SUSP_ST_PENDING, "held");
    CHECK_EQ(XhciSuspIdleSubmit(0, 0, 0, 0), XHCI_SUSP_ST_TODAY,
             "not eligible: held as today");
    CHECK_EQ(XhciSuspIdleSubmit(1, 1, 0, 0), XHCI_SUSP_ST_NO_DEVICE,
             "released");
    CHECK_EQ(XhciSuspIdleSubmit(0, 1, 1, 0), XHCI_SUSP_ST_NO_DEVICE,
             "closed");
    CHECK_EQ(XhciSuspIdleSubmit(0, 1, 0, 1), XHCI_SUSP_ST_BUSY,
             "previous IRP or entry still owned");
    CHECK_EQ(XhciSuspIdleSubmit(0, 0, 0, 1), XHCI_SUSP_ST_BUSY,
             "today's BUSY too");

    memset(f, 0, sizeof(f));
    f[0].Started = 1;
    f[0].IdleState = XHCI_SUSP_IDLE_HELD;
    f[0].PwrState = D0;
    f[1] = f[0];
    CHECK_EQ(XhciSuspIdleBatchReady(f, 2, XHCI_SUSP_ACTIVE, 1), 1,
             "every function held, D0: a batch");
    CHECK_EQ(XhciSuspIdleBatchReady(f, 2, XHCI_SUSP_SUSPENDED, 1), 0,
             "link not ACTIVE");
    CHECK_EQ(XhciSuspIdleBatchReady(f, 2, XHCI_SUSP_ACTIVE, 0), 0,
             "not S0");
    f[1].IdleState = XHCI_SUSP_IDLE_NONE;
    CHECK_EQ(XhciSuspIdleBatchReady(f, 2, XHCI_SUSP_ACTIVE, 1), 0,
             "a sibling has not submitted: held");
    f[1].Started = 0;
    f[1].PwrState = D3;
    CHECK_EQ(XhciSuspIdleBatchReady(f, 2, XHCI_SUSP_ACTIVE, 1), 1,
             "an unstarted sibling in D3 is not of the group");
    f[1].PwrState = D0;
    CHECK_EQ(XhciSuspIdleBatchReady(f, 2, XHCI_SUSP_ACTIVE, 1), 0,
             "an unstarted sibling at D0 holds the device: no batch "
             "(round 1, finding 1)");
    f[1].Started = 1;
    f[1].Closed = 1;
    CHECK_EQ(XhciSuspIdleBatchReady(f, 2, XHCI_SUSP_ACTIVE, 1), 0,
             "a closed sibling at D0 likewise");
    f[1].PwrState = D2;
    CHECK_EQ(XhciSuspIdleBatchReady(f, 2, XHCI_SUSP_ACTIVE, 1), 1,
             "a closed sibling in D2 is not of the group");
    f[1] = f[0];
    f[1].PwrState = D2;
    CHECK_EQ(XhciSuspIdleBatchReady(f, 2, XHCI_SUSP_ACTIVE, 1), 0,
             "a sibling not D0");
    f[1] = f[0];
    f[1].IdleState = XHCI_SUSP_IDLE_QUEUED;
    CHECK_EQ(XhciSuspIdleBatchReady(f, 2, XHCI_SUSP_ACTIVE, 1), 0,
             "already queued: not a new batch");
    f[0].Started = 0;
    f[1].Started = 0;
    CHECK_EQ(XhciSuspIdleBatchReady(f, 2, XHCI_SUSP_ACTIVE, 1), 0,
             "nothing started");
    CHECK_EQ(XhciSuspIdleBatchReady(f, 0, XHCI_SUSP_ACTIVE, 1), 0,
             "no PDOs");
}

static void claim_base(PXHCI_SUSP_CLAIM_IN in)
{
    in->IdleState = XHCI_SUSP_IDLE_QUEUED;
    in->EntryBatch = 3;
    in->BatchCur = 3;
    in->Started = 1;
    in->Closed = 0;
    in->PwrState = D0;
    in->SystemS0 = 1;
    in->EntryEpoch = 2;
    in->Epoch = 2;
    in->SlotFull = 1;
    in->Owned = 1;
}

static void test_idle_claim(void)
{
    XHCI_SUSP_CLAIM_IN in;

    claim_base(&in);
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_RUN, "owned, eligible");
    in.SlotFull = 0;
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_LET_GO,
             "slot empty (cancelled while QUEUED)");
    claim_base(&in);
    in.Owned = 0;
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_LET_GO,
             "the cancel routine owns it");

    claim_base(&in);
    in.BatchCur = 4;
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_SKIP, "batch moved on");
    claim_base(&in);
    in.BatchCur = 0;
    in.EntryBatch = 0;
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_SKIP,
             "unlinked: batch 0 never matches");
    claim_base(&in);
    in.Started = 0;
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_SKIP, "not started");
    claim_base(&in);
    in.Closed = 1;
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_SKIP, "closed");
    claim_base(&in);
    in.PwrState = D2;
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_SKIP, "not D0");
    claim_base(&in);
    in.SystemS0 = 0;
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_SKIP, "not S0");
    claim_base(&in);
    in.Epoch = 3;
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_SKIP, "another epoch");
    claim_base(&in);
    in.IdleState = XHCI_SUSP_IDLE_HELD;
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_SKIP, "not QUEUED");
}

static void test_idle_settle(void)
{
    ULONG s;
    static const ULONG states[] = {
        XHCI_SUSP_IDLE_HELD, XHCI_SUSP_IDLE_QUEUED, XHCI_SUSP_IDLE_RETURNED
    };

    for (s = 0; s < 3; s++) {
        CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_CANCEL, states[s], D0),
                 XHCI_SUSP_ST_CANCELLED, "cancel");
        CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_GONE, states[s], D0),
                 XHCI_SUSP_ST_CANCELLED, "teardown / GONE");
        CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_SYSTEM, states[s], D0),
                 XHCI_SUSP_ST_CANCELLED, "a system IRP not S0");
        CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_D3, states[s], D3),
                 XHCI_SUSP_ST_POWER_INVALID, "D3 at acceptance");
        CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_SKIP, states[s], D0),
                 XHCI_SUSP_ST_CANCELLED, "skip");
    }
    /* HELD is reached by the first four and the skip only. */
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_D0, XHCI_SUSP_IDLE_HELD, D0),
             XHCI_SUSP_ST_NONE, "HELD: D0 does not settle it");
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_EPISODE, XHCI_SUSP_IDLE_HELD,
                                D2),
             XHCI_SUSP_ST_NONE, "HELD: the episode does not");
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_D0, XHCI_SUSP_IDLE_RETURNED,
                                D0),
             XHCI_SUSP_ST_SUCCESS, "Dx then D0 at ACTIVE");
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_D0, XHCI_SUSP_IDLE_QUEUED, D0),
             XHCI_SUSP_ST_SUCCESS, "QUEUED, own D0");
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_EPISODE,
                                XHCI_SUSP_IDLE_RETURNED, D2),
             XHCI_SUSP_ST_SUCCESS, "episode ended, RETURNED, D2");
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_EPISODE,
                                XHCI_SUSP_IDLE_RETURNED, D3),
             XHCI_SUSP_ST_NONE, "episode, D3: settled by D3 instead");
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_EPISODE,
                                XHCI_SUSP_IDLE_RETURNED, D0),
             XHCI_SUSP_ST_NONE, "episode, still D0: callback never powered "
             "down");
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_EPISODE, XHCI_SUSP_IDLE_QUEUED,
                                D2),
             XHCI_SUSP_ST_NONE, "episode, QUEUED");
    /* RUNNING: claimed only after the callback returns. */
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_CANCEL, XHCI_SUSP_IDLE_RUNNING,
                                D0),
             XHCI_SUSP_ST_PENDING, "RUNNING cancel: after the return");
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_GONE, XHCI_SUSP_IDLE_RUNNING,
                                D2),
             XHCI_SUSP_ST_PENDING, "RUNNING teardown: after the return");
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_CANCEL, XHCI_SUSP_IDLE_NONE,
                                D0),
             XHCI_SUSP_ST_NONE, "nothing held");

    CHECK_EQ(XhciSuspEpisodeEnds(XHCI_SUSP_EPI_CANCEL), 1, "cancel ends");
    CHECK_EQ(XhciSuspEpisodeEnds(XHCI_SUSP_EPI_OWN_D0), 1, "own D0 ends");
    CHECK_EQ(XhciSuspEpisodeEnds(XHCI_SUSP_EPI_SKIP), 1, "skip ends");
    CHECK_EQ(XhciSuspEpisodeEnds(XHCI_SUSP_EPI_REFUSED), 1, "refusal ends");
    CHECK_EQ(XhciSuspEpisodeEnds(XHCI_SUSP_EPI_SYSTEM), 1, "system ends");
    CHECK_EQ(XhciSuspEpisodeEnds(XHCI_SUSP_EPI_GONE), 1, "GONE ends");
    CHECK_EQ(XhciSuspEpisodeEnds(XHCI_SUSP_EPI_FUNC_D3), 0,
             "a function's D3 does not");
    CHECK_EQ(XhciSuspEpisodeEnds(XHCI_SUSP_EPI_INTERNAL), 0,
             "an internal cycle does not");
}

/* ------------------------------------------------------------------ */
/* The cancel handshake, every interleaving (4.2)                      */
/* ------------------------------------------------------------------ */

/*
 * Three actors over one IRP and one slot. The submitter publishes under
 * the controller lock in two steps (fill and set the routine; then the
 * irp->Cancel test); IoCancelIrp sets Cancel and swaps the routine out
 * with no controller lock, and the routine it finds then takes the lock;
 * a claimer swaps the routine out under the lock. Each schedule must
 * complete the IRP at most once, and exactly once whenever it was
 * cancelled or claimed; the slot must be empty once it is complete.
 * The claimer's second form is the callback thread's (6.4): it owns the
 * IRP while its callback RUNS, the slot still naming it and no routine
 * set, and then re-publishes it under the lock in the submitter's two
 * steps, the irp->Cancel test included - a cancel that came while it ran
 * must complete it then (Codex review of 36.29, round 1, note).
 */
typedef struct _HS {
    int sub;        /* 0 not yet, 1 slot filled + routine set, 2 done    */
    int can;        /* 0 not yet, 1 swapped (routine pending), 2 done    */
    int clm;        /* 0 not yet, 1 done; RUNNING form: 2 owned, 3
                     * routine set again (lock held), 4 done              */
    int lock;       /* held between a publication's two steps            */
    int routine;    /* the IRP's cancel routine is set                   */
    int cancel;     /* irp->Cancel                                       */
    int slot;
    int completed;
    int doCancel;
    int doClaim;    /* 1 claim and complete; 2 the RUNNING form          */
} HS;

static int hs_checks_failed;

static void hs_check_final(const HS *h)
{
    int want;

    want = (h->doCancel || h->doClaim == 1) ? 1 : 0;
    if (h->completed > 1) {
        hs_checks_failed++;
    }
    if (want && h->completed != 1) {
        hs_checks_failed++;
    }
    if (!want && (h->completed != 0 || !h->slot || !h->routine)) {
        hs_checks_failed++;
    }
    if (h->completed && h->slot) {
        hs_checks_failed++;
    }
}

static void hs_run(HS h)
{
    int moved;
    HS n;

    moved = 0;
    /* Submitter. */
    if (h.sub == 0) {
        n = h;
        n.lock = 1;
        n.slot = 1;
        n.routine = 1;
        n.sub = 1;
        hs_run(n);
        moved = 1;
    } else if (h.sub == 1) {
        int cleared;
        ULONG r;

        n = h;
        cleared = 0;
        if (n.cancel) {
            cleared = n.routine;
            n.routine = 0;
        }
        r = XhciSuspPublish((ULONG)n.cancel, (ULONG)cleared);
        if (r == XHCI_SUSP_PUB_TAKE_BACK) {
            n.slot = 0;
            n.completed++;
        }
        n.lock = 0;
        n.sub = 2;
        hs_run(n);
        moved = 1;
    }
    /* IoCancelIrp, at any time once asked (before publication too). */
    if (h.doCancel && h.can == 0) {
        n = h;
        n.cancel = 1;
        if (n.routine) {
            n.routine = 0;
            n.can = 1;
        } else {
            n.can = 2;
        }
        hs_run(n);
        moved = 1;
    } else if (h.can == 1 && !h.lock) {
        n = h;
        if (n.slot) {
            n.slot = 0;
        }
        n.completed++;
        n.can = 2;
        hs_run(n);
        moved = 1;
    }
    /* A claimer, under the lock, once the IRP could be in the slot. */
    if (h.doClaim && h.clm == 0 && !h.lock && h.sub == 2) {
        int cleared;

        n = h;
        cleared = 0;
        if (n.slot) {
            cleared = n.routine;
            n.routine = 0;
        }
        if (XhciSuspClaim((ULONG)n.slot, (ULONG)cleared)) {
            if (n.doClaim == 1) {
                n.slot = 0;
                n.completed++;
                n.clm = 1;
            } else {
                n.clm = 2;      /* RUNNING: owned, the slot kept */
            }
        } else {
            n.clm = (n.doClaim == 1) ? 1 : 4;
        }
        hs_run(n);
        moved = 1;
    } else if (h.clm == 2 && !h.lock) {
        n = h;
        n.lock = 1;
        n.routine = 1;
        n.clm = 3;
        hs_run(n);
        moved = 1;
    } else if (h.clm == 3) {
        int cleared;
        ULONG r;

        n = h;
        cleared = 0;
        if (n.cancel) {
            cleared = n.routine;
            n.routine = 0;
        }
        r = XhciSuspPublish((ULONG)n.cancel, (ULONG)cleared);
        if (r == XHCI_SUSP_PUB_TAKE_BACK) {
            n.slot = 0;
            n.completed++;
        }
        n.lock = 0;
        n.clm = 4;
        hs_run(n);
        moved = 1;
    }
    if (!moved) {
        hs_check_final(&h);
    }
}

static void test_handshake(void)
{
    HS h;
    int c;
    int k;

    CHECK_EQ(XhciSuspPublish(0, 0), XHCI_SUSP_PUB_PUBLISHED, "no cancel");
    CHECK_EQ(XhciSuspPublish(1, 1), XHCI_SUSP_PUB_TAKE_BACK,
             "cancel before publication");
    CHECK_EQ(XhciSuspPublish(1, 0), XHCI_SUSP_PUB_ROUTINE,
             "the routine is running");
    CHECK_EQ(XhciSuspClaim(1, 1), 1, "claimer owns");
    CHECK_EQ(XhciSuspClaim(1, 0), 0, "routine owns");
    CHECK_EQ(XhciSuspClaim(0, 0), 0, "slot empty");

    hs_checks_failed = 0;
    for (c = 0; c < 2; c++) {
        for (k = 0; k < 3; k++) {
            memset(&h, 0, sizeof(h));
            h.doCancel = c;
            h.doClaim = k;
            hs_run(h);
        }
    }
    CHECK_EQ(hs_checks_failed, 0,
             "every interleaving of publish, cancel and claim: one owner");
}

/* ------------------------------------------------------------------ */
/* The callback context's phases against teardown (4.4)               */
/* ------------------------------------------------------------------ */

/* The callback thread's program counter. */
#define T_TRY_CLAIM   0  /* CAS IDLE -> CLAIMING                          */
#define T_DEQUEUE     1  /* CLAIMING: dequeue and claim (controller)     */
#define T_CALL        2  /* IN_CALLBACK: in the client's callback        */
#define T_TRY_RET     3  /* CAS IN_CALLBACK -> RETURNING                  */
#define T_BOOK        4  /* RETURNING: bookkeeping (controller)          */
#define T_TRY_IDLE    5  /* CAS RETURNING -> IDLE                          */
#define T_ABANDONED   6  /* returned into ABANDONED: touches nothing      */
#define T_EXITED      7

/* Teardown's. */
#define D_DETACH      0  /* (1): detach unclaimed entries (controller)    */
#define D_OBSERVE     1  /* read the phase, choose by XhciSuspHandoff    */
#define D_WAIT        2  /* (4): wait for the phase read to change        */
#define D_CAS         3  /* try the chosen compare-exchange               */
#define D_FINAL       4  /* won: (1) once more (XhciSuspHandoff's note)   */
#define D_JOIN        5  /* EXITING won: join the thread                  */
#define D_DONE        6

typedef struct _PH {
    int phase;
    int t;
    int d;
    int queue;      /* unclaimed entries                                 */
    int held;       /* the thread holds a claimed entry                  */
    int dfrom;      /* the phase teardown read, or its CAS's from        */
    int dto;
    int returns;    /* the callback may return                           */
} PH;

static int ph_violations;
static int ph_finals;

/* Teardown has won: the thread may touch neither the controller nor the
 * queue from here on. */
static int ph_won(const PH *p)
{
    return p->d == D_FINAL || p->d == D_JOIN || p->d == D_DONE;
}

static int ph_cas(PH *p, int actor, int from, int to)
{
    if (!XhciSuspPhaseAllowed((ULONG)actor, (ULONG)from, (ULONG)to)) {
        ph_violations++;
        return 0;
    }
    if (p->phase != from) {
        return 0;
    }
    p->phase = to;
    return 1;
}

static void ph_run(PH p);

/* One step of the thread; `choice` picks among its branches. Returns 0
 * when that branch does not exist or the thread cannot move. */
static int ph_thread(PH p, int choice)
{
    switch (p.t) {
    case T_TRY_CLAIM:
        if (choice != 0) {
            return 0;
        }
        if (ph_cas(&p, XHCI_SUSP_ACTOR_THREAD, XHCI_SUSP_PH_IDLE,
                   XHCI_SUSP_PH_CLAIMING)) {
            p.t = T_DEQUEUE;
        } else if (p.phase == XHCI_SUSP_PH_EXITING) {
            p.t = T_EXITED;
        } else {
            return 0;
        }
        break;
    case T_DEQUEUE:
        if (ph_won(&p)) {
            ph_violations++;    /* controller touched after teardown won */
        }
        if (choice == 0 && p.queue > 0) {
            /* An entry claimed: its callback is called. */
            p.queue--;
            p.held = 1;
            (VOID)ph_cas(&p, XHCI_SUSP_ACTOR_THREAD, XHCI_SUSP_PH_CLAIMING,
                         XHCI_SUSP_PH_IN_CALLBACK);
            p.t = T_CALL;
        } else if (choice == 1 && p.queue > 0) {
            /* An entry whose slot was empty or cancel-owned: let go. */
            p.queue--;
            (VOID)ph_cas(&p, XHCI_SUSP_ACTOR_THREAD, XHCI_SUSP_PH_CLAIMING,
                         XHCI_SUSP_PH_IDLE);
            p.t = T_TRY_CLAIM;
        } else if (choice == 0 && p.queue == 0) {
            (VOID)ph_cas(&p, XHCI_SUSP_ACTOR_THREAD, XHCI_SUSP_PH_CLAIMING,
                         XHCI_SUSP_PH_IDLE);
            p.t = T_TRY_CLAIM;
        } else {
            return 0;
        }
        break;
    case T_CALL:
        if (choice != 0 || !p.returns) {
            return 0;
        }
        p.t = T_TRY_RET;
        break;
    case T_TRY_RET:
        if (choice != 0) {
            return 0;
        }
        if (ph_cas(&p, XHCI_SUSP_ACTOR_THREAD, XHCI_SUSP_PH_IN_CALLBACK,
                   XHCI_SUSP_PH_RETURNING)) {
            p.t = T_BOOK;
        } else {
            p.t = T_ABANDONED;
        }
        break;
    case T_BOOK:
        if (ph_won(&p)) {
            ph_violations++;
        }
        if (choice == 0) {
            p.held = 0;         /* settled and let go                    */
        } else if (choice == 1) {
            p.held = 0;         /* put back as unclaimed                 */
            p.queue++;
        } else {
            return 0;
        }
        p.t = T_TRY_IDLE;
        break;
    case T_TRY_IDLE:
        if (choice != 0) {
            return 0;
        }
        (VOID)ph_cas(&p, XHCI_SUSP_ACTOR_THREAD, XHCI_SUSP_PH_RETURNING,
                     XHCI_SUSP_PH_IDLE);
        p.t = T_TRY_CLAIM;
        break;
    case T_ABANDONED:
        if (choice != 0) {
            return 0;
        }
        /* It completes the IRP it owns and frees the context: nothing
         * of the controller. */
        p.held = 0;
        p.t = T_EXITED;
        break;
    default:
        return 0;
    }
    ph_run(p);
    return 1;
}

static int ph_teardown(PH p)
{
    ULONG h;

    switch (p.d) {
    case D_DETACH:
        p.queue = 0;
        p.d = D_OBSERVE;
        break;
    case D_OBSERVE:
        h = XhciSuspHandoff((ULONG)p.phase);
        if (h == XHCI_SUSP_HAND_EXIT) {
            p.dfrom = XHCI_SUSP_PH_IDLE;
            p.dto = XHCI_SUSP_PH_EXITING;
            p.d = D_CAS;
        } else if (h == XHCI_SUSP_HAND_ABANDON) {
            p.dfrom = XHCI_SUSP_PH_IN_CALLBACK;
            p.dto = XHCI_SUSP_PH_ABANDONED;
            p.d = D_CAS;
        } else if (h == XHCI_SUSP_HAND_WAIT) {
            p.dfrom = p.phase;
            p.dto = 0;
            p.d = D_WAIT;
        } else {
            ph_violations++;
            return 0;
        }
        break;
    case D_WAIT:
        if (p.phase == p.dfrom) {
            return 0;
        }
        p.d = D_DETACH;
        break;
    case D_CAS:
        if (ph_cas(&p, XHCI_SUSP_ACTOR_TEARDOWN, p.dfrom, p.dto)) {
            if (p.dto == XHCI_SUSP_PH_EXITING && p.held) {
                ph_violations++;    /* a claimed entry left behind */
            }
            p.d = D_FINAL;
        } else {
            p.d = D_DETACH;
        }
        break;
    case D_FINAL:
        p.queue = 0;
        p.d = (p.dto == XHCI_SUSP_PH_EXITING) ? D_JOIN : D_DONE;
        break;
    case D_JOIN:
        if (p.t != T_EXITED) {
            return 0;
        }
        p.d = D_DONE;
        break;
    default:
        return 0;
    }
    ph_run(p);
    return 1;
}

/* The model's states are few: each is explored once. */
static unsigned char ph_seen[1UL << 20];

static unsigned long ph_key(const PH *p)
{
    return (unsigned long)p->phase | ((unsigned long)p->t << 3) |
           ((unsigned long)p->d << 6) | ((unsigned long)p->queue << 9) |
           ((unsigned long)p->held << 11) | ((unsigned long)p->dfrom << 12) |
           ((unsigned long)p->dto << 15) | ((unsigned long)p->returns << 18);
}

static void ph_run(PH p)
{
    unsigned long key;
    int c;

    key = ph_key(&p);
    if (ph_seen[key]) {
        return;
    }
    ph_seen[key] = 1;
    if (p.d == D_DONE) {
        ph_finals++;
        /* Nothing left queued, and the thread can add nothing now. */
        if (p.queue != 0) {
            ph_violations++;
        }
    }
    for (c = 0; c < 2; c++) {
        (VOID)ph_thread(p, c);
    }
    (VOID)ph_teardown(p);
}

/* Teardown alone, the thread frozen where it is: 1 when teardown
 * finishes, -1 when it waits on a phase that cannot block (CLAIMING,
 * RETURNING), 0 when it is stuck otherwise. */
static int ph_solo(PH p)
{
    int i;
    ULONG h;

    for (i = 0; i < 20; i++) {
        switch (p.d) {
        case D_DONE:
            return 1;
        case D_JOIN:
            return (p.t == T_EXITED || p.t == T_TRY_CLAIM ||
                    p.t == T_CALL) ? 1 : 0;
        case D_DETACH:
        case D_FINAL:
            p.queue = 0;
            p.d = (p.d == D_DETACH) ? D_OBSERVE
                  : (p.dto == XHCI_SUSP_PH_EXITING) ? D_JOIN : D_DONE;
            break;
        case D_OBSERVE:
            h = XhciSuspHandoff((ULONG)p.phase);
            if (h == XHCI_SUSP_HAND_WAIT) {
                return -1;
            }
            if (h == XHCI_SUSP_HAND_DONE) {
                return 0;
            }
            p.dfrom = (h == XHCI_SUSP_HAND_EXIT) ? XHCI_SUSP_PH_IDLE
                                                 : XHCI_SUSP_PH_IN_CALLBACK;
            p.dto = (h == XHCI_SUSP_HAND_EXIT) ? XHCI_SUSP_PH_EXITING
                                               : XHCI_SUSP_PH_ABANDONED;
            p.d = D_CAS;
            break;
        case D_CAS:
            if (p.phase == p.dfrom) {
                p.phase = p.dto;
                p.d = D_FINAL;
            } else {
                p.d = D_DETACH;
            }
            break;
        default:
            return 0;
        }
    }
    return 0;
}

static void test_phases(void)
{
    PH p;
    int q;
    int r;
    int tpc;
    static const int frozen[] = { T_TRY_CLAIM, T_CALL };
    static const int phaseOf[] = { XHCI_SUSP_PH_IDLE,
                                   XHCI_SUSP_PH_IN_CALLBACK };

    CHECK_EQ(XhciSuspPhaseAllowed(XHCI_SUSP_ACTOR_THREAD, XHCI_SUSP_PH_IDLE,
                                  XHCI_SUSP_PH_CLAIMING), 1, "thread claims");
    CHECK_EQ(XhciSuspPhaseAllowed(XHCI_SUSP_ACTOR_THREAD,
                                  XHCI_SUSP_PH_IN_CALLBACK,
                                  XHCI_SUSP_PH_IDLE), 0,
             "no shortcut out of a callback");
    CHECK_EQ(XhciSuspPhaseAllowed(XHCI_SUSP_ACTOR_TEARDOWN,
                                  XHCI_SUSP_PH_CLAIMING,
                                  XHCI_SUSP_PH_EXITING), 0,
             "teardown cannot win against CLAIMING");
    CHECK_EQ(XhciSuspPhaseAllowed(XHCI_SUSP_ACTOR_TEARDOWN,
                                  XHCI_SUSP_PH_RETURNING,
                                  XHCI_SUSP_PH_ABANDONED), 0,
             "nor against RETURNING");
    CHECK_EQ(XhciSuspPhaseAllowed(XHCI_SUSP_ACTOR_THREAD,
                                  XHCI_SUSP_PH_ABANDONED,
                                  XHCI_SUSP_PH_RETURNING), 0,
             "an abandoned thread cannot return into the controller");
    CHECK_EQ(XhciSuspHandoff(XHCI_SUSP_PH_IDLE), XHCI_SUSP_HAND_EXIT, "IDLE");
    CHECK_EQ(XhciSuspHandoff(XHCI_SUSP_PH_IN_CALLBACK),
             XHCI_SUSP_HAND_ABANDON, "IN_CALLBACK");
    CHECK_EQ(XhciSuspHandoff(XHCI_SUSP_PH_CLAIMING), XHCI_SUSP_HAND_WAIT,
             "CLAIMING");
    CHECK_EQ(XhciSuspHandoff(XHCI_SUSP_PH_RETURNING), XHCI_SUSP_HAND_WAIT,
             "RETURNING");
    CHECK_EQ(XhciSuspHandoff(XHCI_SUSP_PH_EXITING), XHCI_SUSP_HAND_DONE,
             "EXITING");

    /* Every interleaving: entries queued (one behind another), a
     * callback that returns or never does, an entry let go empty or put
     * back as unclaimed after its callback, against teardown with its
     * wait-and-retry and its last (1). */
    ph_violations = 0;
    ph_finals = 0;
    memset(ph_seen, 0, sizeof(ph_seen));
    for (q = 0; q <= 2; q++) {
        for (r = 0; r <= 1; r++) {
            memset(&p, 0, sizeof(p));
            p.phase = XHCI_SUSP_PH_IDLE;
            p.queue = q;
            p.returns = r;
            ph_run(p);
        }
    }
    CHECK_EQ(ph_violations, 0,
             "no controller or queue access after teardown won, no entry "
             "left held or queued, only allowed compare-exchanges");
    CHECK(ph_finals > 0, "teardown finishes in some schedules");

    /* A callback that never returns, and an idle thread: teardown alone
     * finishes (abandon, or exit and join). */
    for (tpc = 0; tpc < 2; tpc++) {
        memset(&p, 0, sizeof(p));
        p.t = frozen[tpc];
        p.phase = phaseOf[tpc];
        p.held = (tpc == 1) ? 1 : 0;
        p.queue = 1;
        CHECK_EQ(ph_solo(p), 1, "teardown alone finishes");
    }
    memset(&p, 0, sizeof(p));
    p.t = T_DEQUEUE;
    p.phase = XHCI_SUSP_PH_CLAIMING;
    CHECK_EQ(ph_solo(p), -1, "CLAIMING: teardown waits (never blocks)");
}

/* ------------------------------------------------------------------ */
/* Finalization (4.4 (b) finish, (e))                                  */
/* ------------------------------------------------------------------ */

static ULONG fin2(ULONG thread, ULONG exited, ULONG urbs, ULONG cb,
                  ULONG power, ULONG halted, ULONG pinned)
{
    XHCI_SUSP_FIN_IN in;

    in.CallerIsThread = thread;
    in.ThreadExited = exited;
    in.UrbsPending = urbs;
    in.CallbackRunning = cb;
    in.PowerPending = power;
    in.DmaHalted = halted;
    in.Pinned = pinned;
    return XhciSuspFinalize(&in);
}

static ULONG fin(ULONG thread, ULONG exited, ULONG urbs, ULONG cb,
                 ULONG halted)
{
    return fin2(thread, exited, urbs, cb, 0, halted, 0);
}

static void test_finalize(void)
{
    CHECK_EQ(fin(0, 0, 0, 0, 0), XHCI_SUSP_FIN_NOW, "nothing owed");
    CHECK_EQ(fin(1, 0, 1, 0, 0), XHCI_SUSP_FIN_DEFER,
             "unlinked with URBs on rings, the thread as caller: deferred");
    CHECK_EQ(fin(1, 0, 0, 1, 0), XHCI_SUSP_FIN_DEFER,
             "a callback not returned, the thread as caller: deferred");
    CHECK_EQ(fin(0, 0, 1, 0, 0), XHCI_SUSP_FIN_WAIT,
             "a PnP caller, the thread alive: waits");
    CHECK_EQ(fin(0, 1, 1, 0, 1), XHCI_SUSP_FIN_WAIT,
             "after the thread exits, DMA halted: waits");
    CHECK_EQ(fin(0, 1, 1, 0, 0), XHCI_SUSP_FIN_QUARANTINE,
             "after the thread exits, no proof: quarantine");
    CHECK_EQ(fin(0, 1, 0, 1, 0), XHCI_SUSP_FIN_QUARANTINE,
             "a callback waiting on cancelled I/O, no proof: quarantine");
    CHECK_EQ(fin(0, 1, 0, 0, 0), XHCI_SUSP_FIN_NOW,
             "after exit, nothing owed");
    /* A WAIT_WAKE whose cancel routine owns it, its completion not yet
     * returned (round 1, finding 4). */
    CHECK_EQ(fin2(0, 0, 0, 0, 1, 0, 0), XHCI_SUSP_FIN_WAIT,
             "a power completion still running: waits");
    CHECK_EQ(fin2(1, 0, 0, 0, 1, 0, 0), XHCI_SUSP_FIN_DEFER,
             "...or the thread defers");
    /* Containment pinned the buffer with the thread alive (round 1,
     * finding 5). */
    CHECK_EQ(fin2(0, 0, 1, 0, 0, 0, 1), XHCI_SUSP_FIN_QUARANTINE,
             "pinned, thread alive, URBs pinned: quarantine");
    CHECK_EQ(fin2(1, 0, 0, 1, 0, 0, 1), XHCI_SUSP_FIN_QUARANTINE,
             "pinned, the thread as caller: quarantine, not deferred "
             "forever");
    CHECK_EQ(fin2(0, 0, 0, 0, 0, 0, 1), XHCI_SUSP_FIN_NOW,
             "pinned, nothing owed: finalized");
}

/* ------------------------------------------------------------------ */
/* An idle episode end to end: QUEUED cancel, then teardown            */
/* ------------------------------------------------------------------ */

static void test_episode_interleavings(void)
{
    XHCI_SUSP_CLAIM_IN in;

    /* QUEUED, cancelled: the routine completed it; the callback thread
     * reaching the entry finds the slot empty and lets go. */
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_CANCEL, XHCI_SUSP_IDLE_QUEUED,
                                D0),
             XHCI_SUSP_ST_CANCELLED, "QUEUED cancel");
    CHECK_EQ(XhciSuspEpisodeEnds(XHCI_SUSP_EPI_CANCEL), 1, "episode ends");
    claim_base(&in);
    in.SlotFull = 0;
    CHECK_EQ(XhciSuspIdleClaim(&in), XHCI_SUSP_CLAIM_LET_GO,
             "the entry is let go");
    /* Then teardown: nothing left in the slot; the rundown finishes. */
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_GONE, XHCI_SUSP_IDLE_NONE, D0),
             XHCI_SUSP_ST_NONE, "teardown finds nothing");
    CHECK_EQ(fin(0, 0, 0, 0, 1), XHCI_SUSP_FIN_NOW, "finalized");

    /* RUNNING, cancelled: nothing runs until the callback returns; then
     * the re-publication's Cancel test completes it. Teardown meanwhile
     * waits for the rundown (or, with no DMA proof after the thread
     * exited, quarantines). */
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_CANCEL,
                                XHCI_SUSP_IDLE_RUNNING, D2),
             XHCI_SUSP_ST_PENDING, "RUNNING cancel waits for the return");
    CHECK_EQ(XhciSuspPublish(1, 1), XHCI_SUSP_PUB_TAKE_BACK,
             "re-published with Cancel set: completed CANCELLED");
    CHECK_EQ(fin(0, 0, 0, 1, 0), XHCI_SUSP_FIN_WAIT,
             "teardown waits for the callback");
    CHECK_EQ(fin(1, 0, 0, 1, 0), XHCI_SUSP_FIN_DEFER,
             "the enum thread defers instead");
    CHECK_EQ(fin(0, 1, 0, 1, 0), XHCI_SUSP_FIN_QUARANTINE,
             "after exit, no proof: quarantine");

    /* D3 and a system transition with an idle IRP still HELD. */
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_D3, XHCI_SUSP_IDLE_HELD, D3),
             XHCI_SUSP_ST_POWER_INVALID, "HELD, D3");
    CHECK_EQ(XhciSuspIdleSettle(XHCI_SUSP_SET_SYSTEM, XHCI_SUSP_IDLE_HELD,
                                D0),
             XHCI_SUSP_ST_CANCELLED, "HELD, system to S3");
}

int main(void)
{
    test_switch_and_caps();
    test_admit();
    test_decide_composite();
    test_decide_epoch_and_powerdown();
    test_decide_d0_during_suspending();
    test_dwell();
    test_suspend_outcomes();
    test_wake_at_observe();
    test_resume_outcomes();
    test_latch_and_rearm();
    test_woke_hold();
    test_powerdown_d0();
    test_start_reopen();
    test_gone();
    test_digest();
    test_root_evidence();
    test_hub_evidence();
    test_wake_claims();
    test_wait_wake();
    test_idle_submit_and_batch();
    test_idle_claim();
    test_idle_settle();
    test_handshake();
    test_phases();
    test_finalize();
    test_episode_interleavings();

    printf("test_susp: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
