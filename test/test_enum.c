/*
 * test_enum.c - host vectors for the enumeration state machine
 * (src\xhci_enum.c; design record 13 section 5.3; roadmap-hcd.md 26-A.9).
 *
 * The machine is pure, so every path a device can take - a clean
 * enumeration at each speed, an EP0 size that needs Evaluate Context, each
 * failing step with its slot given back, the one retry, a disconnect at
 * every stage, and the Gone wait for a PDO's remove - is driven here with no
 * controller.
 */

#include <stdio.h>
#include "../src/xhci_enum.h"
#include "test_harness.h"

static XHCI_ENUM_EVENT ev(ULONG kind, ULONG ok)
{
    XHCI_ENUM_EVENT e;

    e.Kind = kind;
    e.Ok = ok;
    e.Speed = 0;
    e.SlotId = 0;
    e.Bytes = 0;
    e.Value = 0;
    return e;
}

static ULONG step(PXHCI_ENUM_PORT p, XHCI_ENUM_EVENT e, PXHCI_ENUM_ACTION a)
{
    return XhciEnumStep(p, &e, a);
}

/* Drive a port from Empty to Present for a device of `speed` whose
 * bMaxPacketSize0 is `mps`. Returns the action count that asked for an
 * Evaluate Context. */
static ULONG drive_to_present(PXHCI_ENUM_PORT p, ULONG speed, ULONG mps)
{
    XHCI_ENUM_ACTION a;
    XHCI_ENUM_EVENT e;
    ULONG evaluated;

    evaluated = 0;
    XhciEnumReset(p);
    step(p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_DEBOUNCE, "connect asks for the debounce");
    step(p, ev(XHCI_ENUM_EV_DEBOUNCED, 1), &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_RESET, "still connected asks for the reset");
    e = ev(XHCI_ENUM_EV_RESET_DONE, 1);
    e.Speed = speed;
    step(p, e, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_ENABLE_SLOT, "reset done asks for a slot");
    CHECK_EQ(p->Speed, speed, "the reset's speed is the speed carried");
    e = ev(XHCI_ENUM_EV_COMMAND_DONE, 1);
    e.SlotId = 5;
    step(p, e, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_ADDRESS, "a slot asks for Address Device");
    CHECK_EQ(a.Mps0, XhciEnumInitialMps0(speed), "addressed at the initial size");
    step(p, ev(XHCI_ENUM_EV_COMMAND_DONE, 1), &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_GET_DEVICE, "addressed asks for 8 bytes");
    CHECK_EQ(a.Length, 8, "the first read is 8 bytes");
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 8;
    e.Value = mps;
    step(p, e, &a);
    if (a.Kind == XHCI_ENUM_ACT_EVALUATE) {
        evaluated = 1;
        CHECK_EQ(a.Mps0, mps, "Evaluate Context carries the device's size");
        step(p, ev(XHCI_ENUM_EV_COMMAND_DONE, 1), &a);
    }
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_GET_DEVICE, "then the whole descriptor");
    CHECK_EQ(a.Length, 18, "18 bytes");
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 18;
    step(p, e, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_GET_CONFIG, "then the configuration head");
    CHECK_EQ(a.Length, 9, "9 bytes");
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 9;
    e.Value = 34;
    step(p, e, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_GET_CONFIG, "then wTotalLength");
    CHECK_EQ(a.Length, 34, "34 bytes");
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 34;
    step(p, e, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_CREATE_PDO, "all of it asks for the PDO");
    CHECK_EQ(p->State, XHCI_ENUM_PRESENT, "Present");
    return evaluated;
}

static void test_clean_paths(void)
{
    XHCI_ENUM_PORT p;
    XHCI_ENUM_ACTION a;

    CHECK_EQ(drive_to_present(&p, XHCI_ENUM_SPEED_HIGH, 64), 0,
             "High Speed: no Evaluate");
    CHECK_EQ(drive_to_present(&p, XHCI_ENUM_SPEED_LOW, 8), 0,
             "Low Speed: no Evaluate");
    CHECK_EQ(drive_to_present(&p, XHCI_ENUM_SPEED_FULL, 64), 0,
             "Full Speed at 64: no Evaluate");
    CHECK_EQ(drive_to_present(&p, XHCI_ENUM_SPEED_FULL, 8), 1,
             "Full Speed at 8: Evaluate Context");
    CHECK_EQ(drive_to_present(&p, XHCI_ENUM_SPEED_FULL, 32), 1,
             "Full Speed at 32: Evaluate Context");

    step(&p, ev(XHCI_ENUM_EV_PDO_CREATED, 1), &a);
    CHECK_EQ(p.PdoExists, 1, "the PDO is recorded");
    CHECK_EQ(p.State, XHCI_ENUM_PRESENT, "still Present until started");
    step(&p, ev(XHCI_ENUM_EV_PDO_STARTED, 1), &a);
    CHECK_EQ(p.State, XHCI_ENUM_BOUND, "Bound once started");
}

static void test_bad_sizes(void)
{
    XHCI_ENUM_PORT p;
    XHCI_ENUM_ACTION a;
    XHCI_ENUM_EVENT e;

    XhciEnumReset(&p);
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_DEBOUNCED, 1), &a);
    e = ev(XHCI_ENUM_EV_RESET_DONE, 1);
    e.Speed = XHCI_ENUM_SPEED_HIGH;
    step(&p, e, &a);
    e = ev(XHCI_ENUM_EV_COMMAND_DONE, 1);
    e.SlotId = 2;
    step(&p, e, &a);
    step(&p, ev(XHCI_ENUM_EV_COMMAND_DONE, 1), &a);
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 8;
    e.Value = 8;
    step(&p, e, &a);
    CHECK_EQ(p.State, XHCI_ENUM_FAILED, "High Speed with a 8-byte EP0 fails");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_DISABLE_SLOT, "and gives the slot back");
    CHECK_EQ(p.SlotId, 0, "the slot is forgotten");
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_DESCRIPTOR, "a descriptor failure");

    XhciEnumReset(&p);
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_DEBOUNCED, 1), &a);
    e = ev(XHCI_ENUM_EV_RESET_DONE, 1);
    e.Speed = 4;
    step(&p, e, &a);
    CHECK_EQ(p.State, XHCI_ENUM_FAILED, "a SuperSpeed value is refused");
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_SPEED, "as a speed failure");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "with no slot to give back");
}

static void test_failures_and_retry(void)
{
    XHCI_ENUM_PORT p;
    XHCI_ENUM_ACTION a;
    XHCI_ENUM_EVENT e;

    XhciEnumReset(&p);
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_DEBOUNCED, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_RESET_DONE, 0), &a);
    CHECK_EQ(p.State, XHCI_ENUM_FAILED, "a failed reset fails");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "with nothing to give back");
    XhciEnumRetry(&p, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_RESET, "the one retry resets again");
    step(&p, ev(XHCI_ENUM_EV_RESET_DONE, 0), &a);
    XhciEnumRetry(&p, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "no second retry");
    CHECK_EQ(p.State, XHCI_ENUM_FAILED, "Failed until the next connect");
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_DEBOUNCE, "a new connect starts over");
    CHECK_EQ(p.Retries, 0, "with the retries restored");

    XhciEnumReset(&p);
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_DEBOUNCED, 1), &a);
    e = ev(XHCI_ENUM_EV_RESET_DONE, 1);
    e.Speed = XHCI_ENUM_SPEED_FULL;
    step(&p, e, &a);
    step(&p, ev(XHCI_ENUM_EV_COMMAND_DONE, 0), &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_NO_SLOT, "no slot");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "nothing to disable");

    XhciEnumReset(&p);
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_DEBOUNCED, 1), &a);
    step(&p, e, &a);
    e = ev(XHCI_ENUM_EV_COMMAND_DONE, 1);
    e.SlotId = 7;
    step(&p, e, &a);
    step(&p, ev(XHCI_ENUM_EV_COMMAND_DONE, 0), &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_ADDRESS, "Address Device failed");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_DISABLE_SLOT, "slot 7 is given back");

    step(&p, ev(XHCI_ENUM_EV_DEBOUNCED, 1), &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "an event out of place does nothing");
    CHECK_EQ(p.State, XHCI_ENUM_FAILED, "and changes nothing");
}

static void test_disconnects(void)
{
    XHCI_ENUM_PORT p;
    XHCI_ENUM_ACTION a;
    XHCI_ENUM_EVENT e;

    XhciEnumReset(&p);
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_DEBOUNCED, 0), &a);
    CHECK_EQ(p.State, XHCI_ENUM_EMPTY, "gone during the debounce: Empty");

    XhciEnumReset(&p);
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_DISCONNECT, 1), &a);
    CHECK_EQ(p.State, XHCI_ENUM_EMPTY, "disconnect before a slot: Empty");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "nothing to undo");

    XhciEnumReset(&p);
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_DEBOUNCED, 1), &a);
    e = ev(XHCI_ENUM_EV_RESET_DONE, 1);
    e.Speed = XHCI_ENUM_SPEED_LOW;
    step(&p, e, &a);
    e = ev(XHCI_ENUM_EV_COMMAND_DONE, 1);
    e.SlotId = 3;
    step(&p, e, &a);
    step(&p, ev(XHCI_ENUM_EV_DISCONNECT, 1), &a);
    CHECK_EQ(p.State, XHCI_ENUM_EMPTY, "disconnect with a slot: Empty");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_DISABLE_SLOT, "and the slot disabled");

    drive_to_present(&p, XHCI_ENUM_SPEED_HIGH, 64);
    step(&p, ev(XHCI_ENUM_EV_PDO_CREATED, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_PDO_STARTED, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_DISCONNECT, 1), &a);
    CHECK_EQ(p.State, XHCI_ENUM_GONE, "disconnect with a PDO: Gone");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_REPORT_GONE, "the PDO is reported missing");
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    CHECK_EQ(p.State, XHCI_ENUM_GONE, "a connect waits for the remove");
    step(&p, ev(XHCI_ENUM_EV_PDO_REMOVED, 1), &a);
    CHECK_EQ(p.State, XHCI_ENUM_EMPTY, "the remove frees the port");
    CHECK_EQ(p.PdoExists, 0, "with no PDO");
}

/* The events of a whole Full Speed enumeration with an 8-byte EP0, so the
 * Evaluate Context step is in it, through to the PDO's start. */
#define FULL_SEQUENCE 12

static XHCI_ENUM_EVENT full_event(ULONG i, ULONG ok)
{
    XHCI_ENUM_EVENT e;

    switch (i) {
    case 0:  e = ev(XHCI_ENUM_EV_CONNECT, ok); break;
    case 1:  e = ev(XHCI_ENUM_EV_DEBOUNCED, ok); break;
    case 2:  e = ev(XHCI_ENUM_EV_RESET_DONE, ok);
             e.Speed = XHCI_ENUM_SPEED_FULL; break;
    case 3:  e = ev(XHCI_ENUM_EV_COMMAND_DONE, ok); e.SlotId = 4; break;
    case 4:  e = ev(XHCI_ENUM_EV_COMMAND_DONE, ok); break;
    case 5:  e = ev(XHCI_ENUM_EV_TRANSFER_DONE, ok);
             e.Bytes = 8; e.Value = 8; break;
    case 6:  e = ev(XHCI_ENUM_EV_COMMAND_DONE, ok); break;
    case 7:  e = ev(XHCI_ENUM_EV_TRANSFER_DONE, ok); e.Bytes = 18; break;
    case 8:  e = ev(XHCI_ENUM_EV_TRANSFER_DONE, ok);
             e.Bytes = 9; e.Value = 34; break;
    case 9:  e = ev(XHCI_ENUM_EV_TRANSFER_DONE, ok); e.Bytes = 34; break;
    case 10: e = ev(XHCI_ENUM_EV_PDO_CREATED, ok); break;
    default: e = ev(XHCI_ENUM_EV_PDO_STARTED, ok); break;
    }
    return e;
}

/* Feed the first `count` events of the sequence. */
static void drive_full(PXHCI_ENUM_PORT p, ULONG count, PXHCI_ENUM_ACTION a)
{
    ULONG i;

    XhciEnumReset(p);
    a->Kind = XHCI_ENUM_ACT_NONE;
    for (i = 0; i < count; i++) {
        step(p, full_event(i, 1), a);
    }
}

/* Every step's failure, its cause, and the slot given back from the step
 * that owns one on (Codex review of batch (b), round 1, finding 16). */
static void test_each_step_fails(void)
{
    static const ULONG cause[FULL_SEQUENCE] = {
        0, 0, XHCI_ENUM_FAIL_RESET, XHCI_ENUM_FAIL_NO_SLOT,
        XHCI_ENUM_FAIL_ADDRESS, XHCI_ENUM_FAIL_DESCRIPTOR,
        XHCI_ENUM_FAIL_ADDRESS, XHCI_ENUM_FAIL_DESCRIPTOR,
        XHCI_ENUM_FAIL_CONFIG, XHCI_ENUM_FAIL_CONFIG, XHCI_ENUM_FAIL_PDO, 0
    };
    XHCI_ENUM_PORT p;
    XHCI_ENUM_ACTION a;
    ULONG i;

    for (i = 2; i < FULL_SEQUENCE - 1; i++) {
        drive_full(&p, i, &a);
        step(&p, full_event(i, 0), &a);
        CHECK_EQ(p.State, XHCI_ENUM_FAILED, "a failed step fails the port");
        CHECK_EQ(p.FailCause, cause[i], "with that step's cause");
        CHECK_EQ(a.Kind, i >= 4 ? XHCI_ENUM_ACT_DISABLE_SLOT
                                : XHCI_ENUM_ACT_NONE,
                 "the slot given back exactly when one was held");
        CHECK_EQ(p.SlotId, 0, "and forgotten");
        CHECK_EQ(p.PdoExists, 0, "with no PDO");
    }
}

/* Short reads that report success (round 1, finding 16): each is refused. */
static void test_short_reads(void)
{
    XHCI_ENUM_PORT p;
    XHCI_ENUM_ACTION a;
    XHCI_ENUM_EVENT e;

    drive_full(&p, 5, &a);
    e = full_event(5, 1);
    e.Bytes = 7;
    step(&p, e, &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_DESCRIPTOR, "7 of 8 bytes");

    drive_full(&p, 7, &a);
    e = full_event(7, 1);
    e.Bytes = 17;
    step(&p, e, &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_DESCRIPTOR, "17 of 18 bytes");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_DISABLE_SLOT, "the slot given back");

    drive_full(&p, 8, &a);
    e = full_event(8, 1);
    e.Bytes = 8;
    step(&p, e, &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_CONFIG, "8 of 9 configuration bytes");

    drive_full(&p, 8, &a);
    e = full_event(8, 1);
    e.Value = 8;
    step(&p, e, &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_CONFIG,
             "a wTotalLength shorter than its own header");

    drive_full(&p, 9, &a);
    e = full_event(9, 1);
    e.Bytes = 33;
    step(&p, e, &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_CONFIG, "33 of 34 bytes");
    CHECK_EQ(p.State, XHCI_ENUM_FAILED, "is no Present");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_DISABLE_SLOT, "the slot given back");

    drive_full(&p, 9, &a);
    e = full_event(9, 1);
    e.Bytes = 35;
    step(&p, e, &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_CONFIG, "35 of 34 bytes");
}

/* A disconnect after every step of the sequence (round 1, finding 16). */
static void test_disconnect_every_stage(void)
{
    XHCI_ENUM_PORT p;
    XHCI_ENUM_ACTION a;
    ULONG held;
    ULONG pdo;
    ULONG i;

    for (i = 1; i <= FULL_SEQUENCE; i++) {
        drive_full(&p, i, &a);
        held = p.SlotId != 0;
        pdo = p.PdoExists;
        step(&p, ev(XHCI_ENUM_EV_DISCONNECT, 1), &a);
        if (pdo) {
            CHECK_EQ(p.State, XHCI_ENUM_GONE, "with a PDO: Gone");
            CHECK_EQ(a.Kind, XHCI_ENUM_ACT_REPORT_GONE, "reported missing");
        } else {
            CHECK_EQ(p.State, XHCI_ENUM_EMPTY, "without a PDO: Empty");
            CHECK_EQ(a.Kind, held ? XHCI_ENUM_ACT_DISABLE_SLOT
                                  : XHCI_ENUM_ACT_NONE,
                     "the slot given back exactly when one was held");
        }
        CHECK_EQ(p.SlotId, 0, "no slot is kept");
        CHECK_EQ(held, i >= 4 && i <= FULL_SEQUENCE, "slot held from step 4");
    }
}

int main(void)
{
    test_clean_paths();
    test_bad_sizes();
    test_failures_and_retry();
    test_disconnects();
    test_each_step_fails();
    test_short_reads();
    test_disconnect_every_stage();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
