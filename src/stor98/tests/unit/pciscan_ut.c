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

    printf("%d failure(s)\n", failures);
    return failures;
}
