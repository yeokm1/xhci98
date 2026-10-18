/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * poll_ut - host unit test of the polling mode's settings and decision
 * (src/polltun.h: defaults, bounds, clamping, the meaning of 0, decimal
 * parsing, the interrupt-or-polling decision) and of the report-only PCI
 * capability walk (src/pcicap.h: Interrupt Pin/Line, MSI and MSI-X
 * presence; no capabilities, a looped list, a list ending at 0, both
 * present, a pointer past the bytes read). PLAN "Polling mode". Built by
 * tests\unit\build.cmd; the exit code is the number of failed checks.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "polltun.h"
#include "pcicap.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static POLL_SETTINGS apply(ULONG present, ULONG mode, ULONG busy, ULONG idle, ULONG spin)
{
    POLL_RAW r;
    POLL_SETTINGS s;

    r.Present = present;
    r.Mode = mode;
    r.BusyUs = busy;
    r.IdleMs = idle;
    r.SpinUs = spin;
    PollSettingsApply(&s, &r);
    return s;
}

static void test_settings(void)
{
    POLL_SETTINGS s;
    const ULONG all = POLL_CLAMP_MODE | POLL_CLAMP_BUSY | POLL_CLAMP_IDLE | POLL_CLAMP_SPIN;

    printf("settings\n");
#ifdef NVME2K_W9X
    check(POLL_SPIN_US_DEFAULT == 0, "9x: no spin by default (maintainer, 2026-10-16)");
#else
    check(POLL_SPIN_US_DEFAULT == 500, "NT: 500 us spin by default (maintainer, 2026-10-16)");
#endif
    check(POLL_BUSY_US_DEFAULT == 1000 && POLL_IDLE_MS_DEFAULT == 1000, "busy 1 ms, idle 1 s by default");
    s = apply(0, 7, 7, 7, 7);
    check(s.Mode == POLL_MODE_AUTO && s.BusyUs == POLL_BUSY_US_DEFAULT && s.IdleMs == POLL_IDLE_MS_DEFAULT &&
          s.SpinUs == POLL_SPIN_US_DEFAULT && s.Clamped == 0, "nothing present: defaults, no clamp");

    s = apply(all, 1, 2000, 500, 100);
    check(s.Mode == 1 && s.BusyUs == 2000 && s.IdleMs == 500 && s.SpinUs == 100 && s.Clamped == 0,
          "in-range values kept");

    s = apply(all, 0, 0, 0, 0);
    check(s.Mode == 0 && s.BusyUs == POLL_BUSY_US_DEFAULT && s.IdleMs == 0 && s.SpinUs == 0 && s.Clamped == 0,
          "0: mode auto, busy default, idle stops, no spin; none is a clamp");

    s = apply(POLL_CLAMP_MODE, 5, 0, 0, 0);
    check(s.Mode == 1 && s.Clamped == POLL_CLAMP_MODE, "mode above 1 clamped to 1 and recorded");

    s = apply(POLL_CLAMP_BUSY, 0, 1, 0, 0);
    check(s.BusyUs == POLL_BUSY_US_MIN && s.Clamped == POLL_CLAMP_BUSY, "busy 1 us clamped to the minimum");
    s = apply(POLL_CLAMP_BUSY, 0, 999, 0, 0);
    check(s.BusyUs == POLL_BUSY_US_MIN && s.Clamped == POLL_CLAMP_BUSY, "busy 999 us clamped to 1000");
    s = apply(POLL_CLAMP_BUSY, 0, 1000, 0, 0);
    check(s.BusyUs == 1000 && s.Clamped == 0, "busy at the minimum kept");
    s = apply(POLL_CLAMP_BUSY, 0, 100000, 0, 0);
    check(s.BusyUs == 100000 && s.Clamped == 0, "busy at the maximum kept");
    s = apply(POLL_CLAMP_BUSY, 0, 100001, 0, 0);
    check(s.BusyUs == POLL_BUSY_US_MAX && s.Clamped == POLL_CLAMP_BUSY, "busy above the maximum clamped");
    s = apply(POLL_CLAMP_BUSY, 0, 0xFFFFFFFF, 0, 0);
    check(s.BusyUs == POLL_BUSY_US_MAX && s.Clamped == POLL_CLAMP_BUSY, "busy 0xFFFFFFFF clamped");

    s = apply(POLL_CLAMP_IDLE, 0, 0, 50, 0);
    check(s.IdleMs == POLL_IDLE_MS_MIN && s.Clamped == POLL_CLAMP_IDLE, "idle 50 ms clamped to 100");
    s = apply(POLL_CLAMP_IDLE, 0, 0, 60001, 0);
    check(s.IdleMs == POLL_IDLE_MS_MAX && s.Clamped == POLL_CLAMP_IDLE, "idle above 60 s clamped");
    s = apply(POLL_CLAMP_IDLE, 0, 0, 100, 0);
    check(s.IdleMs == 100 && s.Clamped == 0, "idle at the minimum kept");

    s = apply(POLL_CLAMP_SPIN, 0, 0, 0, 501);
    check(s.SpinUs == POLL_SPIN_US_MAX && s.Clamped == POLL_CLAMP_SPIN, "spin above 500 us clamped");
    s = apply(POLL_CLAMP_SPIN, 0, 0, 0, 500);
    check(s.SpinUs == 500 && s.Clamped == 0, "spin at the maximum kept");
    check(POLL_SPIN_US_MAX <= 500, "spin bound within half the 1 ms per-callback stall budget");

    s = apply(all, 9, 5, 5, 9999);
    check(s.Clamped == all, "every clamp recorded at once");
    check(s.Mode == 1 && s.BusyUs == 1000 && s.IdleMs == 100 && s.SpinUs == 500, "every value clamped at once");
}

static void test_parse(void)
{
    ULONG v = 1234;

    printf("parse\n");
    check(!PollParseDec(NULL, &v) && v == 1234, "NULL: not present, value untouched");
    check(!PollParseDec("", &v) && v == 1234, "empty: not present");
    check(!PollParseDec("abc", &v) && v == 1234, "no digit: not present");
    check(PollParseDec("2000", &v) && v == 2000, "plain decimal");
    check(PollParseDec("  15;pollidlems=3", &v) && v == 15, "leading blanks, stops at ';'");
    check(PollParseDec("0", &v) && v == 0, "zero is present");
    check(PollParseDec("4294967295", &v) && v == 0xFFFFFFFF, "32-bit maximum");
    check(PollParseDec("99999999999999", &v) && v == 0xFFFFFFFF, "overflow saturates (then clamped)");
}

static void test_decide(void)
{
    printf("decide\n");
    check(PollDecide(0, TRUE, 1, 11, TRUE, 11, 11) == POLL_WHY_INTERRUPT, "NT: pin, line, port resource: interrupt");
    check(PollDecide(1, TRUE, 1, 11, TRUE, 11, 11) == POLL_WHY_FORCED, "forced by PollMode");
    check(PollDecide(0, TRUE, 0, 11, TRUE, 11, 11) == POLL_WHY_NO_PIN, "pin 0 (MSI-only): polling");
    check(PollDecide(1, TRUE, 0, 0, TRUE, 0, 0) == POLL_WHY_NO_PIN, "natural reason wins over forced");
    check(PollDecide(0, TRUE, 1, 11, TRUE, 0, 0) == POLL_WHY_NO_PORT_IRQ, "NT: no interrupt resource: polling");
    check(PollDecide(0, TRUE, 1, 11, FALSE, 0, 0) == POLL_WHY_INTERRUPT, "9x: port values not used, line decides");
    check(PollDecide(0, TRUE, 1, 0xFF, FALSE, 0, 0) == POLL_WHY_NO_LINE, "line FFh: polling");
    check(PollDecide(0, TRUE, 1, 0, FALSE, 0, 0) == POLL_WHY_NO_LINE, "line 0: polling");
    check(PollDecide(0, TRUE, 1, 0xFF, TRUE, 16, 16) == POLL_WHY_NO_LINE, "NT: line FFh is still polling (as reported to the port)");
    check(PollDecide(0, FALSE, 1, 11, TRUE, 11, 11) == POLL_WHY_NO_CONFIG, "config unreadable: polling");
}

/* A 256-byte header: Status bit 4 per CapList, Capabilities Pointer Ptr. */
static void hdr(UCHAR *c, int CapList, UCHAR Ptr)
{
    memset(c, 0, 256);
    c[0] = 0x36; c[1] = 0x1B; c[2] = 0x10; c[3] = 0x00;
    c[S98_PCI_STATUS] = CapList ? 0x10 : 0x00;
    c[S98_PCI_CAP_PTR] = Ptr;
    c[S98_PCI_INT_LINE] = 11;
    c[S98_PCI_INT_PIN] = 1;
}

static void cap(UCHAR *c, UCHAR At, UCHAR Id, UCHAR Next)
{
    c[At] = Id;
    c[At + 1] = Next;
}

static void test_caps(void)
{
    UCHAR c[256];
    PCI_INT_INFO ii;

    printf("capabilities\n");
    hdr(c, 0, 0x40);
    cap(c, 0x40, S98_PCI_CAPID_MSI, 0);
    PciReadIntInfo(c, 256, &ii);
    check(ii.Line == 11 && ii.Pin == 1 && (ii.Flags & S98_PCI_HAS_PIN_LINE), "pin and line read");
    check(!(ii.Flags & (S98_PCI_HAS_CAPLIST | S98_PCI_MSI | S98_PCI_MSIX)) && ii.Caps == 0,
          "no capabilities: Status bit 4 clear, the pointer is not followed");

    hdr(c, 1, 0x40);
    cap(c, 0x40, 0x01, 0x50);                   /* power management */
    cap(c, 0x50, S98_PCI_CAPID_MSI, 0x60);
    cap(c, 0x60, 0x10, 0x70);                   /* PCI Express */
    cap(c, 0x70, S98_PCI_CAPID_MSIX, 0x00);
    PciReadIntInfo(c, 256, &ii);
    check((ii.Flags & S98_PCI_MSI) && (ii.Flags & S98_PCI_MSIX) && ii.Caps == 4 &&
          !(ii.Flags & (S98_PCI_CAP_LOOP | S98_PCI_CAP_SHORT)), "both present, list ends at 0");

    hdr(c, 1, 0x40);
    cap(c, 0x40, S98_PCI_CAPID_MSIX, 0x00);
    PciReadIntInfo(c, 256, &ii);
    check(!(ii.Flags & S98_PCI_MSI) && (ii.Flags & S98_PCI_MSIX) && ii.Caps == 1, "MSI-X only, ends at 0");

    hdr(c, 1, 0x40);
    cap(c, 0x40, 0x01, 0x50);
    cap(c, 0x50, S98_PCI_CAPID_MSI, 0x40);      /* back to the first: a loop */
    PciReadIntInfo(c, 256, &ii);
    check((ii.Flags & S98_PCI_CAP_LOOP) && (ii.Flags & S98_PCI_MSI) && ii.Caps == S98_PCI_CAP_MAX_WALK,
          "looped list: bounded at 48 entries, flagged");

    hdr(c, 1, 0x48);
    cap(c, 0x48, 0x01, 0x48);                   /* points at itself */
    PciReadIntInfo(c, 256, &ii);
    check((ii.Flags & S98_PCI_CAP_LOOP) && ii.Caps == S98_PCI_CAP_MAX_WALK, "self loop: bounded");

    hdr(c, 1, 0x43);                            /* low bits masked: 0x40 */
    cap(c, 0x40, S98_PCI_CAPID_MSI, 0x13);      /* next 0x10 < 40h: ends */
    PciReadIntInfo(c, 256, &ii);
    check((ii.Flags & S98_PCI_MSI) && ii.Caps == 1 && !(ii.Flags & S98_PCI_CAP_LOOP),
          "pointer low bits masked; a pointer below 40h ends the list");

    hdr(c, 1, 0x20);
    PciReadIntInfo(c, 256, &ii);
    check(ii.Caps == 0 && (ii.Flags & S98_PCI_HAS_CAPLIST), "first pointer below 40h: nothing walked");

    hdr(c, 1, 0x00);
    PciReadIntInfo(c, 256, &ii);
    check(ii.Caps == 0, "capability list bit set but pointer 0: empty");

    hdr(c, 1, 0x40);
    cap(c, 0x40, 0x01, 0xF0);
    PciReadIntInfo(c, 0x80, &ii);               /* only 128 bytes read */
    check((ii.Flags & S98_PCI_CAP_SHORT) && ii.Caps == 1, "pointer past the bytes read: stops, flagged");

    PciReadIntInfo(c, 0x30, &ii);
    check(!(ii.Flags & S98_PCI_HAS_PIN_LINE) && ii.Caps == 0 && ii.Pin == 0, "header shorter than 34h: nothing");

    hdr(c, 1, 0xFC);
    cap(c, 0xFC, S98_PCI_CAPID_MSI, 0x00);
    PciReadIntInfo(c, 256, &ii);
    check((ii.Flags & S98_PCI_MSI) && ii.Caps == 1, "capability at FCh, the last dword");
}

static void test_timer(void)
{
    const ULONG watch = 100000, cm = 100000;

    printf("timer selection\n");
    /* EngTimerEnsure */
    check(PollEnsurePeriod(watch, TRUE, TRUE, 10000) == 10000, "submission while polling: watchdog tick -> busy period");
    check(PollEnsurePeriod(watch, TRUE, FALSE, 10000) == watch, "dirty idle while polling: watchdog tick kept");
    check(PollEnsurePeriod(cm, TRUE, FALSE, 10000) == cm, "CM-only 100 ms with nothing outstanding: kept");
    check(PollEnsurePeriod(100000 * 10, TRUE, TRUE, 10000) == 1000000, "a 1 s idle period is never shortened");
    check(PollEnsurePeriod(10000, TRUE, TRUE, 20000) == 10000, "a request shorter than busy (bring-up 10 ms) kept");
    check(PollEnsurePeriod(watch, FALSE, TRUE, 10000) == watch, "interrupt mode: unchanged");
    /* LcWantedPeriodEngine, running */
    check(PollRunningPeriod(TRUE, FALSE, TRUE, TRUE, 10000, 1000, watch) == 10000, "busy, polling: busy period");
    check(PollRunningPeriod(TRUE, FALSE, FALSE, FALSE, 10000, 1000, watch) == watch, "busy, interrupt: watchdog");
    check(PollRunningPeriod(FALSE, TRUE, TRUE, TRUE, 10000, 1000, watch) == watch, "dirty idle, polling: watchdog (idle flush)");
    check(PollRunningPeriod(FALSE, FALSE, TRUE, TRUE, 10000, 1000, watch) == 1000000, "clean idle, polling: PollIdleMs");
    check(PollRunningPeriod(FALSE, FALSE, TRUE, TRUE, 10000, 0, watch) == 0, "clean idle, PollIdleMs 0: timer stops");
    check(PollRunningPeriod(FALSE, FALSE, TRUE, FALSE, 10000, 1000, watch) == 0, "storm polling only, idle: timer stops");
    check(PollRunningPeriod(FALSE, FALSE, TRUE, TRUE, 10000, POLL_IDLE_MS_MAX, watch) == 60000000UL, "60 s idle: no overflow");
    /* LcWantedPeriod */
    check(PollCombine(0, cm) == cm, "engine idle (0) + CM poll: CM period");
    check(PollCombine(1000000, cm) == cm, "1 s idle + CM 100 ms: CM wins");
    check(PollCombine(10000, cm) == 10000, "busy 10 ms + CM 100 ms: busy wins");
    check(PollCombine(10000, 0) == 10000 && PollCombine(0, 0) == 0, "no CM channel: engine period");
}

int main(void)
{
    test_timer();
    test_settings();
    test_parse();
    test_decide();
    test_caps();
    printf("%s: %d failure(s)\n", failures ? "FAILED" : "passed", failures);
    return failures;
}
