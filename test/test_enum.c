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
#include "../src/xhci_hub.h"
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
    e.Speed = 5;
    step(&p, e, &a);
    CHECK_EQ(p.State, XHCI_ENUM_FAILED, "a speed past the machine's refused");
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

/* The speed table, the EP0 sizes each speed may claim, and the two NULLs
 * (task 26-A.9). */
static void test_sizes_and_nulls(void)
{
    static const ULONG fsGood[] = { 8, 16, 32, 64 };
    static const ULONG fsBad[] = { 0, 4, 9, 48, 128, 255 };
    XHCI_ENUM_PORT p;
    XHCI_ENUM_ACTION a;
    XHCI_ENUM_EVENT e;
    ULONG i;

    CHECK_EQ(XhciEnumInitialMps0(XHCI_ENUM_SPEED_LOW), 8, "Low Speed: 8");
    CHECK_EQ(XhciEnumInitialMps0(XHCI_ENUM_SPEED_FULL), 64, "Full Speed: 64");
    CHECK_EQ(XhciEnumInitialMps0(XHCI_ENUM_SPEED_HIGH), 64, "High Speed: 64");
    CHECK_EQ(XhciEnumInitialMps0(0), 0, "speed 0: none");
    CHECK_EQ(XhciEnumInitialMps0(XHCI_ENUM_SPEED_SUPER), 512,
             "SuperSpeed: 512 (29-A.3)");
    CHECK_EQ(XhciEnumInitialMps0(5), 0, "past SuperSpeed: none");
    CHECK_EQ(XhciEnumInitialMps0(15), 0, "a PSIV past the defaults: none");

    for (i = 0; i < sizeof(fsGood) / sizeof(fsGood[0]); i++) {
        drive_full(&p, 5, &a);
        e = full_event(5, 1);
        e.Value = fsGood[i];
        step(&p, e, &a);
        CHECK_EQ(p.State, fsGood[i] == 64 ? XHCI_ENUM_DESC18
                                           : XHCI_ENUM_EVALUATE,
                 "a Full Speed EP0 of 8, 16, 32 or 64 is taken");
        CHECK_EQ(p.Mps0, fsGood[i], "and carried");
    }
    for (i = 0; i < sizeof(fsBad) / sizeof(fsBad[0]); i++) {
        drive_full(&p, 5, &a);
        e = full_event(5, 1);
        e.Value = fsBad[i];
        step(&p, e, &a);
        CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_DESCRIPTOR,
                 "any other Full Speed EP0 size fails the descriptor");
        CHECK_EQ(a.Kind, XHCI_ENUM_ACT_DISABLE_SLOT, "giving the slot back");
    }

    /* Low Speed claiming 64. */
    XhciEnumReset(&p);
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_DEBOUNCED, 1), &a);
    e = ev(XHCI_ENUM_EV_RESET_DONE, 1);
    e.Speed = XHCI_ENUM_SPEED_LOW;
    step(&p, e, &a);
    e = ev(XHCI_ENUM_EV_COMMAND_DONE, 1);
    e.SlotId = 9;
    step(&p, e, &a);
    CHECK_EQ(a.Mps0, 8, "Low Speed is addressed at 8");
    step(&p, ev(XHCI_ENUM_EV_COMMAND_DONE, 1), &a);
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 8;
    e.Value = 64;
    step(&p, e, &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_DESCRIPTOR,
             "a Low Speed device claiming 64 fails");

    /* Enable Slot answering success with slot 0 is no slot. */
    drive_full(&p, 3, &a);
    e = full_event(3, 1);
    e.SlotId = 0;
    step(&p, e, &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_NO_SLOT, "slot 0 is no slot");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "and nothing to give back");

    e = ev(XHCI_ENUM_EV_CONNECT, 1);
    a.Kind = XHCI_ENUM_ACT_RESET;
    CHECK_EQ(XhciEnumStep(NULL, &e, &a), XHCI_ENUM_EMPTY, "no port: Empty");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "and no action");
    XhciEnumReset(&p);
    a.Kind = XHCI_ENUM_ACT_RESET;
    CHECK_EQ(XhciEnumStep(&p, NULL, &a), XHCI_ENUM_EMPTY, "no event: Empty");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "and no action");
    CHECK_EQ(p.State, XHCI_ENUM_EMPTY, "the port untouched");
}

static ULONG same_port(const XHCI_ENUM_PORT *x, const XHCI_ENUM_PORT *y)
{
    return x->State == y->State && x->Speed == y->Speed &&
           x->SlotId == y->SlotId && x->Mps0 == y->Mps0 &&
           x->ConfigLength == y->ConfigLength && x->Retries == y->Retries &&
           x->FailCause == y->FailCause && x->PdoExists == y->PdoExists;
}

/*
 * Every event that does not belong to a state, in every state of the full
 * sequence plus Gone: nothing changes and nothing is asked (xhci_enum.h, "an
 * event that does not apply to the current state changes nothing and asks
 * for nothing"). A late COMMAND_DONE or a stray TRANSFER_DONE reaching the
 * wrong state is how a machine skips a step.
 */
static void test_out_of_place_everywhere(void)
{
    XHCI_ENUM_PORT p;
    XHCI_ENUM_PORT before;
    XHCI_ENUM_ACTION a;
    XHCI_ENUM_EVENT e;
    ULONG expected;
    ULONG kind;
    ULONG bad;
    ULONG i;

    bad = 0;
    for (i = 0; i <= FULL_SEQUENCE; i++) {
        for (kind = XHCI_ENUM_EV_CONNECT; kind <= XHCI_ENUM_EV_PDO_REMOVED;
             kind++) {
            if (kind == XHCI_ENUM_EV_DISCONNECT) {
                continue;
            }
            expected = (i < FULL_SEQUENCE) ? full_event(i, 1).Kind : 0;
            if (kind == expected) {
                continue;
            }
            drive_full(&p, i, &a);
            before = p;
            e = ev(kind, 1);
            e.Speed = XHCI_ENUM_SPEED_HIGH;
            e.SlotId = 6;
            e.Bytes = 18;
            e.Value = 64;
            step(&p, e, &a);
            if (!same_port(&p, &before) || a.Kind != XHCI_ENUM_ACT_NONE) {
                printf("  after %lu events, event %lu moved the machine\n",
                       i, kind);
                bad++;
            }
        }
    }
    CHECK_EQ(bad, 0, "no out-of-place event moves any state of the sequence");

    /* Gone: only the remove moves it. */
    bad = 0;
    for (kind = XHCI_ENUM_EV_CONNECT; kind < XHCI_ENUM_EV_PDO_REMOVED;
         kind++) {
        drive_full(&p, FULL_SEQUENCE, &a);
        step(&p, ev(XHCI_ENUM_EV_DISCONNECT, 1), &a);
        before = p;
        step(&p, ev(kind, 1), &a);
        if (!same_port(&p, &before) || a.Kind != XHCI_ENUM_ACT_NONE) {
            bad++;
        }
    }
    CHECK_EQ(bad, 0, "Gone waits for the remove through every other event");
}

/* The one retry, taken from a failure that held a slot, carries a device
 * all the way to Bound; a retry asked where nothing failed does nothing. */
static void test_retry_to_bound(void)
{
    XHCI_ENUM_PORT p;
    XHCI_ENUM_ACTION a;
    ULONG i;

    drive_full(&p, 7, &a);
    step(&p, full_event(7, 0), &a);
    CHECK_EQ(p.State, XHCI_ENUM_FAILED, "the 18-byte read failed");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_DISABLE_SLOT, "the slot given back");
    XhciEnumRetry(&p, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_RESET, "the retry resets");
    CHECK_EQ(p.Retries, 1, "and is counted");
    for (i = 2; i < FULL_SEQUENCE; i++) {
        step(&p, full_event(i, 1), &a);
    }
    CHECK_EQ(p.State, XHCI_ENUM_BOUND, "the retried device is bound");
    CHECK_EQ(p.Mps0, 8, "at the size its own descriptor gave");
    CHECK_EQ(p.SlotId, 4, "on its new slot");
    CHECK_EQ(p.Retries, 1, "the retry still counted while it stays");
    XhciEnumRetry(&p, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "no retry where nothing failed");
    CHECK_EQ(p.State, XHCI_ENUM_BOUND, "Bound stays Bound");

    drive_full(&p, 4, &a);
    XhciEnumRetry(&p, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "no retry mid-sequence either");
    CHECK_EQ(p.State, XHCI_ENUM_ADDRESS, "the step in flight is kept");

    /* A PDO that could not be created: Failed with the slot back, and a
     * disconnect from there has nothing left to undo. */
    drive_full(&p, 10, &a);
    step(&p, ev(XHCI_ENUM_EV_PDO_CREATED, 0), &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_PDO, "PDO creation failed");
    CHECK_EQ(p.PdoExists, 0, "no PDO recorded");
    step(&p, ev(XHCI_ENUM_EV_DISCONNECT, 1), &a);
    CHECK_EQ(p.State, XHCI_ENUM_EMPTY, "a disconnect from Failed: Empty");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "the slot was already given back");
}

/* A hub's port is given XHCI_HUB_PORT_ATTEMPTS attempts in all (27-A.3;
 * design record 13 section 10.2 step 7), a root port the machine's own one
 * retry: the limit is the caller's, the count the port's. */
static void test_hub_port_attempts(void)
{
    XHCI_ENUM_PORT p;
    XHCI_ENUM_ACTION a;
    ULONG limit;

    limit = XHCI_HUB_PORT_ATTEMPTS - 1UL;
    drive_full(&p, 7, &a);
    step(&p, full_event(7, 0), &a);
    CHECK_EQ(p.State, XHCI_ENUM_FAILED, "first attempt failed");
    XhciEnumRetryUpTo(&p, limit, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_RESET, "second attempt");
    step(&p, ev(XHCI_ENUM_EV_RESET_DONE, 0), &a);
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_RESET, "its reset failed");
    XhciEnumRetry(&p, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "a root port would stop here");
    XhciEnumRetryUpTo(&p, limit, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_RESET, "a hub port tries a third time");
    CHECK_EQ(p.Retries, 2, "two retries counted");
    step(&p, ev(XHCI_ENUM_EV_RESET_DONE, 0), &a);
    XhciEnumRetryUpTo(&p, limit, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_NONE, "given up after the third");
    CHECK_EQ(p.State, XHCI_ENUM_FAILED, "Failed until a new connect");
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_DEBOUNCE, "a connect starts afresh");
    CHECK_EQ(p.Retries, 0, "with its attempts restored");
}

/*
 * SuperSpeed (task 29-A.3): EP0 at 512, bMaxPacketSize0 the exponent 9, and
 * the BOS read between the device descriptor and the configuration - which
 * does not fail the enumeration when it fails.
 */
static void ss_to_desc18(PXHCI_ENUM_PORT p, PXHCI_ENUM_ACTION a)
{
    XHCI_ENUM_EVENT e;

    XhciEnumReset(p);
    step(p, ev(XHCI_ENUM_EV_CONNECT, 1), a);
    step(p, ev(XHCI_ENUM_EV_DEBOUNCED, 1), a);
    e = ev(XHCI_ENUM_EV_RESET_DONE, 1);
    e.Speed = XHCI_ENUM_SPEED_SUPER;
    step(p, e, a);
    e = ev(XHCI_ENUM_EV_COMMAND_DONE, 1);
    e.SlotId = 3;
    step(p, e, a);
    CHECK_EQ(a->Kind, XHCI_ENUM_ACT_ADDRESS, "SS: Address Device");
    CHECK_EQ(a->Mps0, 512, "at EP0 512");
    step(p, ev(XHCI_ENUM_EV_COMMAND_DONE, 1), a);
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 8;
    e.Value = 9;
    step(p, e, a);
    CHECK_EQ(a->Kind, XHCI_ENUM_ACT_GET_DEVICE,
             "bMaxPacketSize0 09h is 512: no Evaluate Context");
    CHECK_EQ(a->Length, 18, "the whole descriptor");
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 18;
    step(p, e, a);
}

static void test_superspeed(void)
{
    XHCI_ENUM_PORT p;
    XHCI_ENUM_ACTION a;
    XHCI_ENUM_EVENT e;

    ss_to_desc18(&p, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_GET_BOS, "SS: the BOS head next");
    CHECK_EQ(a.Length, 5, "5 bytes");
    CHECK_EQ(p.State, XHCI_ENUM_BOS5, "Bos5");
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 5;
    e.Value = 22;
    step(&p, e, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_GET_BOS, "then wTotalLength of it");
    CHECK_EQ(a.Length, 22, "22 bytes");
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 22;
    step(&p, e, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_GET_CONFIG, "then the configuration");
    CHECK_EQ(a.Length, 9, "its head");
    CHECK_EQ(p.BosMissing, 0, "with the BOS read");
    CHECK_EQ(p.BosLength, 22, "and its length kept");

    /* A BOS read that stalls does not fail the enumeration. */
    ss_to_desc18(&p, &a);
    step(&p, ev(XHCI_ENUM_EV_TRANSFER_DONE, 0), &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_GET_CONFIG, "a failed BOS head: go on");
    CHECK_EQ(p.BosMissing, 1, "BosMissing says so");
    CHECK_EQ(p.State, XHCI_ENUM_CONFIG9, "Config9");

    /* Nor does a short whole read. */
    ss_to_desc18(&p, &a);
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 5;
    e.Value = 22;
    step(&p, e, &a);
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 10;
    step(&p, e, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_GET_CONFIG, "a short BOS: go on");
    CHECK_EQ(p.BosMissing, 1, "BosMissing");

    /* A wTotalLength below the header is no BOS. */
    ss_to_desc18(&p, &a);
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 5;
    e.Value = 4;
    step(&p, e, &a);
    CHECK_EQ(p.BosMissing, 1, "wTotalLength 4: missing");

    /* A disconnect mid-BOS gives the slot back. */
    ss_to_desc18(&p, &a);
    step(&p, ev(XHCI_ENUM_EV_DISCONNECT, 1), &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_DISABLE_SLOT, "slot given back");
    CHECK_EQ(p.State, XHCI_ENUM_EMPTY, "Empty");

    /* bMaxPacketSize0 anything but 09h at SuperSpeed is a bad descriptor -
     * 64 included, a USB 2.0 size. */
    XhciEnumReset(&p);
    step(&p, ev(XHCI_ENUM_EV_CONNECT, 1), &a);
    step(&p, ev(XHCI_ENUM_EV_DEBOUNCED, 1), &a);
    e = ev(XHCI_ENUM_EV_RESET_DONE, 1);
    e.Speed = XHCI_ENUM_SPEED_SUPER;
    step(&p, e, &a);
    e = ev(XHCI_ENUM_EV_COMMAND_DONE, 1);
    e.SlotId = 3;
    step(&p, e, &a);
    step(&p, ev(XHCI_ENUM_EV_COMMAND_DONE, 1), &a);
    e = ev(XHCI_ENUM_EV_TRANSFER_DONE, 1);
    e.Bytes = 8;
    e.Value = 64;
    step(&p, e, &a);
    CHECK_EQ(p.State, XHCI_ENUM_FAILED, "64 at SuperSpeed is refused");
    CHECK_EQ(p.FailCause, XHCI_ENUM_FAIL_DESCRIPTOR, "a descriptor failure");

    /* A USB 2.0 device never sees a BOS read. */
    CHECK_EQ(drive_to_present(&p, XHCI_ENUM_SPEED_HIGH, 64), 0,
             "High Speed: no BOS");
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
    test_sizes_and_nulls();
    test_out_of_place_everywhere();
    test_retry_to_bound();
    test_hub_port_attempts();
    test_superspeed();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
