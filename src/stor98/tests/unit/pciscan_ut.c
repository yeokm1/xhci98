/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * pciscan_ut - host unit test of src/pciscan.h, the NT4-ABI bus scan's
 * absent-slot test and its walk to the last bus (xhci98 3.3.0.0: a drive
 * on a bus above 15, behind a USB4 port the firmware numbered far up),
 * the 9x port-function match and the 9x set-up check (PLAN 3.1; xhci98
 * 3.4.2.0: decode off is read, through decode turned on and put back).
 * Built by tests\unit\build.cmd; the exit code is the number of failed
 * checks.
 */

#include <stdio.h>
#include <string.h>
#include "pciscan.h"

static int failures;

static void expect(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

/* A command register and BAR0's two registers for PciScanReadFirmware: each
 * call appended to Log (E a write with Memory Space Enable, C a write
 * without, r a command read, M a register read); WriteFails[n] and
 * ReadFails[n] make the n-th write or read move no bytes, ReadValue[n] (when
 * not 0xFFFF) is what the n-th read returns, MemoryStuck keeps the bit clear.
 * A register read with decode reading off counts in BadReads. */
typedef struct _FAKE_FW {
    PCISCAN_FW_IO Io;
    unsigned short Reg;
    unsigned long Cc, Csts;
    int WriteFails[4], ReadFails[4];
    unsigned short ReadValue[4], Written[4];
    int MemoryStuck, Writes, Reads, BadReads;
    char Log[16];
    int LogLen;
} FAKE_FW;

static void FakeNote(FAKE_FW *f, char c)
{
    if (f->LogLen < (int)sizeof(f->Log) - 1) {
        f->Log[f->LogLen++] = c;
        f->Log[f->LogLen] = 0;
    }
}

static int FakeReadCmd(void *Ctx, unsigned short *Value)
{
    FAKE_FW *f = (FAKE_FW *)Ctx;
    int n = f->Reads++;

    FakeNote(f, 'r');
    if (n < 4 && f->ReadFails[n]) {
        return 0;
    }
    *Value = (n < 4 && f->ReadValue[n] != 0xFFFF) ? f->ReadValue[n] : f->Reg;
    return 1;
}

static int FakeWriteCmd(void *Ctx, unsigned short Value)
{
    FAKE_FW *f = (FAKE_FW *)Ctx;
    int n = f->Writes++;

    FakeNote(f, (char)((Value & PCISCAN_CMD_MEMORY) ? 'E' : 'C'));
    if (n < 4) {
        f->Written[n] = Value;
        if (f->WriteFails[n]) {
            return 0;
        }
    }
    f->Reg = (unsigned short)(f->MemoryStuck ? (Value & ~PCISCAN_CMD_MEMORY) : Value);
    return 1;
}

static unsigned long FakeReadReg(void *Ctx, unsigned long Offset)
{
    FAKE_FW *f = (FAKE_FW *)Ctx;

    FakeNote(f, 'M');
    if ((f->Reg & PCISCAN_CMD_MEMORY) == 0) {
        f->BadReads++;
        return 0xFFFFFFFFUL;
    }
    return Offset == PCISCAN_NVME_CC ? f->Cc : Offset == PCISCAN_NVME_CSTS ? f->Csts : 0xFFFFFFFFUL;
}

static void FakeInit(FAKE_FW *f, unsigned short Cmd, unsigned long Cc, unsigned long Csts)
{
    int i;

    f->Io.Ctx = f;
    f->Io.ReadCmd = FakeReadCmd;
    f->Io.WriteCmd = FakeWriteCmd;
    f->Io.ReadReg = FakeReadReg;
    f->Reg = Cmd;
    f->Cc = Cc;
    f->Csts = Csts;
    for (i = 0; i < 4; i++) {
        f->WriteFails[i] = 0;
        f->ReadFails[i] = 0;
        f->ReadValue[i] = 0xFFFF;
        f->Written[i] = 0;
    }
    f->MemoryStuck = 0;
    f->Writes = 0;
    f->Reads = 0;
    f->BadReads = 0;
    f->Log[0] = 0;
    f->LogLen = 0;
}

/* TRUE when the calls were exactly Want and no register was read with
 * decode reading off. */
static int FakeLog(const FAKE_FW *f, const char *Want)
{
    return strcmp(f->Log, Want) == 0 && f->BadReads == 0;
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

    /* 9x (PLAN 3.1, "A controller already set up"): the decision on what was read */
    expect(PciScanFirmwareState(PCISCAN_READ_OK, 0, 0) == PCISCAN_FW_TAKE,
           "read, disabled and not ready (a drive plugged in): taken");
    expect(PciScanFirmwareState(PCISCAN_READ_OK, 0x00460001UL, 0x00000001UL) == PCISCAN_FW_SET_UP,
           "read, enabled and ready (the board's drive the BIOS drives): left");
    expect(PciScanFirmwareState(PCISCAN_READ_OK, 0x00000001UL, 0) == PCISCAN_FW_SET_UP,
           "read, enabled, not yet ready: left");
    expect(PciScanFirmwareState(PCISCAN_READ_OK, 0, 0x00000001UL) == PCISCAN_FW_SET_UP,
           "read, disabled, still ready (a disable in progress): left");
    expect(PciScanFirmwareState(PCISCAN_READ_OK, 0xFFFFFFFFUL, 0xFFFFFFFFUL) == PCISCAN_FW_NO_ANSWER,
           "read, no answer (all ones): left");
    expect(PciScanFirmwareState(PCISCAN_READ_NONE, 0, 0) == PCISCAN_FW_UNREADABLE,
           "registers not reached (decode on or off): left");
    expect(PciScanFirmwareState(PCISCAN_READ_NOT_RESTORED, 0, 0) == PCISCAN_FW_NOT_RESTORED,
           "command register not put back, even with CC and CSTS 0: left");

    /* Whether BAR0 may be reached for the read (decode on: 0006; off: 0004) */
    expect(PciScanFirmwareReach(0x17, 0x0006, 0xFE600004UL, 0, 0xFE600000UL, 0, 16384) == PCISCAN_REACH_SHORT,
           "a header short of BAR1: not reached");
    expect(PciScanFirmwareReach(256, 0x0006, 0x0000E001UL, 0, 0xFE600000UL, 0, 16384) == PCISCAN_REACH_IO,
           "an I/O BAR0: not reached");
    expect(PciScanFirmwareReach(256, 0x0004, 0x00000004UL, 0, 0, 0, 0) == PCISCAN_REACH_UNASSIGNED,
           "an unassigned BAR0, decode off: not reached");
    expect(PciScanFirmwareReach(256, 0x0004, 0x00000004UL, 0x00000001UL, 0, 1, 16384) == PCISCAN_REACH_HIGH,
           "a 64-bit BAR0 above 4 GiB: not reached (9x maps 32 bits)");
    expect(PciScanFirmwareReach(256, 0x0006, 0xFE600004UL, 0, 0xFE604000UL, 0, 4096) == PCISCAN_REACH_OK,
           "decode on: BAR0 reached whatever the port's range (it decodes already)");
    expect(PciScanFirmwareReach(256, 0x0004, 0xFE600004UL, 0, 0xFE600000UL, 0, 16384) == PCISCAN_REACH_OK,
           "decode off, BAR0 the port's range: reached");
    expect(PciScanFirmwareReach(256, 0x0004, 0xFE600000UL, 0, 0xFE600000UL, 0, 4096) == PCISCAN_REACH_OK,
           "decode off, a 32-bit BAR0 the port's range of one page: reached");
    expect(PciScanFirmwareReach(256, 0x0004, 0xFE600004UL, 0, 0xFE604000UL, 0, 4096) == PCISCAN_REACH_NOT_PORT,
           "decode off, the port's range another BAR: BAR0's allocation unproven, not reached");
    expect(PciScanFirmwareReach(256, 0x0004, 0xFE600004UL, 0, 0xFE600000UL, 1, 16384) == PCISCAN_REACH_NOT_PORT,
           "decode off, the port's range above 4 GiB: not BAR0, not reached");
    expect(PciScanFirmwareReach(256, 0x0004, 0xFE600004UL, 0, 0xFE600000UL, 0, 2048) == PCISCAN_REACH_RANGE_SHORT,
           "decode off, the port's range shorter than a page: not reached");

    /* The read sequence (E enable write, C write back, r command read, M register read) */
    {
        FAKE_FW f;
        unsigned long cc, csts;
        int on, how;

        FakeInit(&f, 0x0006, 0x00460001UL, 0x00000001UL);
        how = PciScanReadFirmware(&f.Io, 0x0006, &cc, &csts, &on);
        expect(how == PCISCAN_READ_OK && !on && cc == 0x00460001UL && csts == 1 && FakeLog(&f, "MM"),
               "decode on: CC and CSTS read, nothing written");

        FakeInit(&f, 0x0004, 0x00460001UL, 0x00000001UL);
        how = PciScanReadFirmware(&f.Io, 0x0004, &cc, &csts, &on);
        expect(how == PCISCAN_READ_OK && on && FakeLog(&f, "ErMMCr") && f.Reg == 0x0004 &&
               PciScanFirmwareState(how, cc, csts) == PCISCAN_FW_SET_UP,
               "decode off, enabled and ready: decode on, read, put back; left (the defect fixed)");

        FakeInit(&f, 0x0404, 0, 0);
        how = PciScanReadFirmware(&f.Io, 0x0404, &cc, &csts, &on);
        expect(how == PCISCAN_READ_OK && on && f.Written[0] == 0x0406 && f.Reg == 0x0404 &&
               PciScanFirmwareState(how, cc, csts) == PCISCAN_FW_TAKE,
               "decode off, not set up: every other bit kept; put back; taken");

        FakeInit(&f, 0x0004, 1, 1);
        f.WriteFails[0] = 1;
        how = PciScanReadFirmware(&f.Io, 0x0004, &cc, &csts, &on);
        expect(how == PCISCAN_READ_NONE && !on && FakeLog(&f, "ECr"),
               "short enable write: no register read, written back and read back, not reached");

        FakeInit(&f, 0x0004, 1, 1);
        f.ReadFails[0] = 1;
        how = PciScanReadFirmware(&f.Io, 0x0004, &cc, &csts, &on);
        expect(how == PCISCAN_READ_NONE && !on && FakeLog(&f, "ErCr"),
               "enable read-back failed: no register read, written back, not reached");

        FakeInit(&f, 0x0004, 1, 1);
        f.MemoryStuck = 1;
        how = PciScanReadFirmware(&f.Io, 0x0004, &cc, &csts, &on);
        expect(how == PCISCAN_READ_NONE && !on && FakeLog(&f, "ErCr"),
               "decode reads back off: no register read, written back, not reached");

        FakeInit(&f, 0x0004, 0, 0);
        f.WriteFails[1] = 1;
        how = PciScanReadFirmware(&f.Io, 0x0004, &cc, &csts, &on);
        expect(how == PCISCAN_READ_NOT_RESTORED && FakeLog(&f, "ErMMC") &&
               PciScanFirmwareState(how, cc, csts) == PCISCAN_FW_NOT_RESTORED,
               "short write-back: not put back, left");

        FakeInit(&f, 0x0004, 0, 0);
        f.ReadFails[1] = 1;
        how = PciScanReadFirmware(&f.Io, 0x0004, &cc, &csts, &on);
        expect(how == PCISCAN_READ_NOT_RESTORED && FakeLog(&f, "ErMMCr"),
               "write-back read failed: not put back, left");

        FakeInit(&f, 0x0004, 0, 0);
        f.ReadValue[1] = 0x0006;
        how = PciScanReadFirmware(&f.Io, 0x0004, &cc, &csts, &on);
        expect(how == PCISCAN_READ_NOT_RESTORED && FakeLog(&f, "ErMMCr"),
               "write-back reads differently: not put back, left");

        FakeInit(&f, 0x0004, 0, 0);
        f.WriteFails[0] = 1;
        f.WriteFails[1] = 1;
        how = PciScanReadFirmware(&f.Io, 0x0004, &cc, &csts, &on);
        expect(how == PCISCAN_READ_NOT_RESTORED && FakeLog(&f, "EC"),
               "short enable write and short write-back: not put back, left");
    }

    printf("%d failure(s)\n", failures);
    return failures;
}
