/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * pciscan_ut - host unit test of src/pciscan.h, the NT4-ABI bus scan's
 * absent-slot test and its walk to the last bus (xhci98 3.3.0.0: a drive
 * on a bus above 15, behind a USB4 port the firmware numbered far up).
 * Built by tests\unit\build.cmd; the exit code is the number of failed
 * checks.
 */

#include <stdio.h>
#include "pciscan.h"

static int failures;

static void expect(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

int main(void)
{
    unsigned long bus, slot, steps;
    int more;

    printf("pciscan_ut\n");
    expect(PciScanAbsent(0, 0x1B36), "0 bytes (no such bus): absent");
    expect(PciScanAbsent(2, 0xFFFF), "2 bytes (no function): absent");
    expect(PciScanAbsent(3, 0x1B36), "short of 4 bytes: absent");
    expect(PciScanAbsent(4, 0xFFFF), "vendor FFFFh: absent");
    expect(PciScanAbsent(4, 0x0000), "vendor 0000h: absent");
    expect(!PciScanAbsent(4, 0x1B36), "4 bytes, vendor 1B36h: present");
    expect(!PciScanAbsent(256, 0x1179), "256 bytes, vendor 1179h: present");

    bus = 0; slot = 254;
    more = PciScanNext(&bus, &slot);
    expect(more && bus == 0 && slot == 255, "bus 0 slot 254 -> slot 255");
    more = PciScanNext(&bus, &slot);
    expect(more && bus == 1 && slot == 0, "bus 0 slot 255 -> bus 1 slot 0");

    bus = 15; slot = 255;
    more = PciScanNext(&bus, &slot);
    expect(more && bus == 16 && slot == 0,
           "bus 15 is not the last: bus 16 is scanned");

    bus = 0x35; slot = 0;
    more = PciScanNext(&bus, &slot);
    expect(more && bus == 0x35 && slot == 1, "bus 35h scanned");

    bus = 255; slot = 254;
    more = PciScanNext(&bus, &slot);
    expect(more && bus == 255 && slot == 255, "bus 255 slot 255 scanned");
    more = PciScanNext(&bus, &slot);
    expect(!more, "past bus 255: the scan ends");

    /* HwFindAdapter's found path: an adapter at the last slot of the last
     * bus leaves no next call (Again FALSE), never a resume at bus 256 */
    bus = 255; slot = 255;
    more = PciScanNext(&bus, &slot);
    expect(!more, "found at bus 255 slot 255: no resume");
    bus = 0x35; slot = 255;
    more = PciScanNext(&bus, &slot);
    expect(more && bus == 0x36 && slot == 0,
           "found at bus 35h slot 255: resume at bus 36h slot 0");

    bus = 0; slot = 0; steps = 0;
    do {
        steps++;
    } while (PciScanNext(&bus, &slot) && steps < 70000UL);
    expect(steps == 256UL * 256UL, "a full scan visits 65536 slots");

    /* 9x (PLAN 3.1): only the function one of whose memory BARs is the
     * port's range */
    {
        static const unsigned long b32[6] = { 0xFE800000UL, 0, 0, 0, 0, 0 };
        static const unsigned long b64[6] = { 0xFE800004UL, 0, 0, 0, 0, 0 };
        static const unsigned long bpf[6] = { 0xFE80000CUL, 0, 0, 0, 0, 0 };
        static const unsigned long bhi[6] = { 0xFE800004UL, 1, 0, 0, 0, 0 };
        /* the board's drive: 64-bit BAR0 at C4000000, a second 64-bit
         * memory BAR at C4004000, which 98 SE gave as the first range */
        static const unsigned long brd[6] = { 0xC4000004UL, 0, 0, 0, 0xC4004004UL, 0 };
        static const unsigned long bio[6] = { 0xFE800004UL, 0, 0x0000C001UL, 0, 0, 0 };
        static const unsigned long bhw[6] = { 0x00000001UL, 0xFE800000UL, 0, 0, 0, 0 };
        static const unsigned long b0[6] = { 0x00000004UL, 0, 0, 0, 0, 0 };

        expect(PciScanIsPortFunction(1, b32, 6, 0xFE800000UL, 0),
               "32-bit BAR0 at the port's range: the port's function");
        expect(PciScanIsPortFunction(1, b64, 6, 0xFE800000UL, 0),
               "64-bit BAR0, high dword 0, at the port's range: taken");
        expect(PciScanIsPortFunction(1, bpf, 6, 0xFE800000UL, 0),
               "64-bit prefetchable BAR0 at the port's range: taken");
        expect(PciScanIsPortFunction(1, brd, 6, 0xC4004000UL, 0),
               "the port's range at the second memory BAR (the board): taken");
        expect(PciScanIsPortFunction(1, brd, 6, 0xC4000000UL, 0),
               "the same drive, range at BAR0: taken");
        expect(!PciScanIsPortFunction(1, brd, 6, 0xFD000000UL, 0),
               "another NVMe function (the system disk): never taken");
        expect(!PciScanIsPortFunction(1, brd, 6, 0xC4002000UL, 0),
               "inside a BAR but not at its start: not taken");
        expect(!PciScanIsPortFunction(1, brd, 4, 0xC4004000UL, 0),
               "header cut short before the second BAR: not taken");
        expect(!PciScanIsPortFunction(1, brd, 5, 0xC4004000UL, 0),
               "the second BAR's high dword unread: not taken");
        expect(!PciScanIsPortFunction(0, b32, 6, 0xFE800000UL, 0),
               "not NVMe, same address: not taken");
        expect(!PciScanIsPortFunction(1, bhi, 6, 0xFE800000UL, 0),
               "64-bit BAR0 above 4 GiB, port range below: not taken");
        expect(PciScanIsPortFunction(1, bhi, 6, 0xFE800000UL, 1),
               "64-bit BAR0 above 4 GiB, port range the same: taken");
        expect(!PciScanIsPortFunction(1, b64, 1, 0xFE800000UL, 0),
               "64-bit BAR0 with its high dword unread: not taken");
        expect(!PciScanIsPortFunction(1, bio, 6, 0x0000C000UL, 0),
               "an I/O BAR at the range's start: not taken");
        expect(PciScanIsPortFunction(1, bhw, 6, 0xFE800000UL, 0),
               "an I/O BAR0 then a memory BAR at the range: taken");
        expect(!PciScanIsPortFunction(1, b0, 6, 0, 0),
               "unassigned BARs, no port range: not taken");
        expect(!PciScanIsPortFunction(1, b32, 6, 0, 0),
               "no port range: nothing is taken");
        expect(!PciScanIsPortFunction(1, b32, 0, 0xFE800000UL, 0),
               "no BAR read: nothing is taken");
    }

    /* 9x (PLAN 3.1, "A controller already set up") */
    expect(PciScanFirmwareState(0x0006, 1, 0, 0) == PCISCAN_FW_TAKE,
           "decode on, disabled and not ready (a drive plugged in): taken");
    expect(PciScanFirmwareState(0x0006, 1, 0x00460001UL, 0x00000001UL) == PCISCAN_FW_SET_UP,
           "enabled and ready (the board's drive the BIOS drives): left");
    expect(PciScanFirmwareState(0x0006, 1, 0x00000001UL, 0) == PCISCAN_FW_SET_UP,
           "enabled, not yet ready: left");
    expect(PciScanFirmwareState(0x0006, 1, 0, 0x00000001UL) == PCISCAN_FW_SET_UP,
           "disabled, still ready (a disable in progress): left");
    expect(PciScanFirmwareState(0x0006, 1, 0xFFFFFFFFUL, 0xFFFFFFFFUL) == PCISCAN_FW_NO_ANSWER,
           "no answer (all ones): left");
    expect(PciScanFirmwareState(0x0006, 0, 0, 0) == PCISCAN_FW_UNREADABLE,
           "decode on, registers not read: left");
    expect(PciScanFirmwareState(0x0004, 0, 0, 0) == PCISCAN_FW_TAKE,
           "memory decode off (not in use): taken");
    expect(PciScanFirmwareState(0x0000, 1, 0x00000001UL, 0x00000001UL) == PCISCAN_FW_TAKE,
           "decode off: CC and CSTS are not consulted");

    printf("%d failure(s)\n", failures);
    return failures;
}
