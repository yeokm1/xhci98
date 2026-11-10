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
    expect(PciScanFirmwareState(PCISCAN_READ_OK, 0xFFFFFFFFUL, 0) == PCISCAN_FW_NO_ANSWER,
           "CC alone all ones: no answer, never set up");
    expect(PciScanFirmwareState(PCISCAN_READ_OK, 0, 0xFFFFFFFFUL) == PCISCAN_FW_NO_ANSWER,
           "CSTS alone all ones: no answer");
    expect(PciScanFirmwareState(PCISCAN_READ_OK, 0xFFFFFFFFUL, 0x00000001UL) == PCISCAN_FW_NO_ANSWER,
           "CC all ones, CSTS ready: no answer, never set up");
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

    /* 9x (PLAN "Taking a controller the BIOS set up"): the user's option */
    {
        static const struct { const char *s; int want; } args[] = {
            { NULL, PCISCAN_TAKE_NONE },
            { "", PCISCAN_TAKE_NONE },
            { "stortakefirmware=1", PCISCAN_TAKE_TAKE },
            { "STORTAKEFIRMWARE=1", PCISCAN_TAKE_TAKE },
            { "StorTakeFirmware=1;", PCISCAN_TAKE_TAKE },
            { "stortakefirmware=0", PCISCAN_TAKE_LEAVE },
            { "drv-as;stortakefirmware=1", PCISCAN_TAKE_TAKE },
            { "a=1;stortakefirmware=1;b=2", PCISCAN_TAKE_TAKE },
            { "notstortakefirmware=1", PCISCAN_TAKE_NONE },
            { "x;notstortakefirmware=1", PCISCAN_TAKE_NONE },
            { " stortakefirmware=1", PCISCAN_TAKE_NONE },
            { "stortakefirmware =1", PCISCAN_TAKE_NONE },
            { "stortakefirmware= 1", PCISCAN_TAKE_NONE },
            { "stortakefirmware=", PCISCAN_TAKE_NONE },
            { "stortakefirmware=;", PCISCAN_TAKE_NONE },
            { "stortakefirmware=1x", PCISCAN_TAKE_NONE },
            { "stortakefirmware=01", PCISCAN_TAKE_NONE },
            { "stortakefirmware=10", PCISCAN_TAKE_NONE },
            { "stortakefirmware=2", PCISCAN_TAKE_NONE },
            { "stortakefirmware=1 ", PCISCAN_TAKE_NONE },
            { "stortakefirmware", PCISCAN_TAKE_NONE },
            { "stortakefirmware=1;stortakefirmware=1", PCISCAN_TAKE_TAKE },
            { "stortakefirmware=1;stortakefirmware=0", PCISCAN_TAKE_LEAVE },
            { "stortakefirmware=0;stortakefirmware=1", PCISCAN_TAKE_LEAVE },
            { "stortakefirmware=2;stortakefirmware=1", PCISCAN_TAKE_TAKE },
            { "stortakefirmware=1x;stortakefirmware=0", PCISCAN_TAKE_LEAVE },
            { ";;stortakefirmware=1", PCISCAN_TAKE_TAKE },
            { "stortakefirmware=1=1", PCISCAN_TAKE_NONE },
            { "storgonems=5000;stortakefirmware=1;storwriteidlems=10", PCISCAN_TAKE_TAKE },
            { "stortakefirmwar=1", PCISCAN_TAKE_NONE },
        };
        static const unsigned char d1[4] = { 1, 0, 0, 0 };
        static const unsigned char d0[4] = { 0, 0, 0, 0 };
        static const unsigned char d2[4] = { 2, 0, 0, 0 };
        static const unsigned char d101[4] = { 1, 1, 0, 0 };
        static const unsigned char dhi[4] = { 1, 0, 0, 1 };
        static const int states[5] = { PCISCAN_FW_TAKE, PCISCAN_FW_SET_UP, PCISCAN_FW_NO_ANSWER,
                                       PCISCAN_FW_UNREADABLE, PCISCAN_FW_NOT_RESTORED };
        static const int argv3[3] = { PCISCAN_TAKE_NONE, PCISCAN_TAKE_LEAVE, PCISCAN_TAKE_TAKE };
        static const int glob2[2] = { PCISCAN_TAKE_LEAVE, PCISCAN_TAKE_TAKE };
        char what[96];
        unsigned i;
        int a, g, st, src, take, want, wsrc, me, refused;

        for (i = 0; i < sizeof(args) / sizeof(args[0]); i++) {
            sprintf(what, "AdapterSettings \"%s\": %d", args[i].s ? args[i].s : "(none)", args[i].want);
            expect(PciScanTakeArg(args[i].s) == args[i].want, what);
        }

        expect(PciScanTakeGlobal(0, 4, d1, 4) == PCISCAN_TAKE_LEAVE, "global absent: leave");
        expect(PciScanTakeGlobal(1, 4, d1, 4) == PCISCAN_TAKE_TAKE, "global DWORD 1: take");
        expect(PciScanTakeGlobal(1, 4, d0, 4) == PCISCAN_TAKE_LEAVE, "global DWORD 0: leave");
        expect(PciScanTakeGlobal(1, 4, d2, 4) == PCISCAN_TAKE_LEAVE, "global DWORD 2: leave");
        expect(PciScanTakeGlobal(1, 4, d101, 4) == PCISCAN_TAKE_LEAVE, "global DWORD 0101h: leave");
        expect(PciScanTakeGlobal(1, 4, dhi, 4) == PCISCAN_TAKE_LEAVE, "global DWORD 01000001h: leave");
        expect(PciScanTakeGlobal(1, 4, d1, 2) == PCISCAN_TAKE_LEAVE, "global DWORD of 2 bytes: leave");
        expect(PciScanTakeGlobal(1, 1, (const unsigned char *)"1", 2) == PCISCAN_TAKE_TAKE, "global \"1\" with its zero: take");
        expect(PciScanTakeGlobal(1, 1, (const unsigned char *)"1", 1) == PCISCAN_TAKE_TAKE, "global \"1\" without its zero: take");
        expect(PciScanTakeGlobal(1, 1, (const unsigned char *)"0", 2) == PCISCAN_TAKE_LEAVE, "global \"0\": leave");
        expect(PciScanTakeGlobal(1, 1, (const unsigned char *)"01", 3) == PCISCAN_TAKE_LEAVE, "global \"01\": leave");
        expect(PciScanTakeGlobal(1, 1, (const unsigned char *)"1 ", 3) == PCISCAN_TAKE_LEAVE, "global \"1 \": leave");
        expect(PciScanTakeGlobal(1, 1, (const unsigned char *)" 1", 3) == PCISCAN_TAKE_LEAVE, "global \" 1\": leave");
        expect(PciScanTakeGlobal(1, 1, (const unsigned char *)"1x", 3) == PCISCAN_TAKE_LEAVE, "global \"1x\": leave");
        expect(PciScanTakeGlobal(1, 1, (const unsigned char *)"", 1) == PCISCAN_TAKE_LEAVE, "global \"\": leave");
        expect(PciScanTakeGlobal(1, 3, d1, 4) == PCISCAN_TAKE_LEAVE, "global REG_BINARY 1: leave");
        expect(PciScanTakeGlobal(1, 4, NULL, 4) == PCISCAN_TAKE_LEAVE, "global with no data: leave");

        /* Every firmware state against every decision and source. */
        for (st = 0; st < 5; st++) {
            for (a = 0; a < 3; a++) {
                for (g = 0; g < 2; g++) {
                    take = PciScanTakeSetting(argv3[a], glob2[g], &src);
                    want = argv3[a] != PCISCAN_TAKE_NONE ? argv3[a] : glob2[g];
                    wsrc = argv3[a] != PCISCAN_TAKE_NONE ? PCISCAN_TAKESRC_ADAPTER : PCISCAN_TAKESRC_GLOBAL;
                    sprintf(what, "state %d arg %d global %d: decision and source", states[st], argv3[a], glob2[g]);
                    expect(take == want && src == wsrc, what);
                    sprintf(what, "state %d arg %d global %d: taken only when not set up, or set up and taken",
                            states[st], argv3[a], glob2[g]);
                    expect(PciScanFirmwareTake(states[st], take) ==
                           (states[st] == PCISCAN_FW_TAKE ||
                            (states[st] == PCISCAN_FW_SET_UP && want == PCISCAN_TAKE_TAKE)), what);
                }
            }
        }
        expect(PciScanTakeSetting(PCISCAN_TAKE_LEAVE, PCISCAN_TAKE_TAKE, &src) == PCISCAN_TAKE_LEAVE &&
               src == PCISCAN_TAKESRC_ADAPTER, "the controller's 0 against a global 1: left, the controller's");
        expect(PciScanTakeSetting(PCISCAN_TAKE_TAKE, PCISCAN_TAKE_LEAVE, &src) == PCISCAN_TAKE_TAKE &&
               src == PCISCAN_TAKESRC_ADAPTER, "the controller's 1 against a global 0: taken, the controller's");
        expect(PciScanTakeSetting(PCISCAN_TAKE_NONE, PCISCAN_TAKE_LEAVE, &src) == PCISCAN_TAKE_LEAVE &&
               src == PCISCAN_TAKESRC_GLOBAL, "no setting anywhere: left");
        expect(!PciScanFirmwareTake(PCISCAN_FW_NO_ANSWER, PCISCAN_TAKE_TAKE), "no answer is never taken");
        expect(!PciScanFirmwareTake(PCISCAN_FW_SET_UP, PCISCAN_TAKE_LEAVE), "set up, option off: left");
        expect(PciScanFirmwareTake(PCISCAN_FW_SET_UP, PCISCAN_TAKE_TAKE), "set up, option on: taken");

        /* Windows Me never takes (xhci98 48.28): every decision on both
         * systems, from every source. */
        for (a = 0; a < 3; a++) {
            for (g = 0; g < 2; g++) {
                for (me = 0; me < 2; me++) {
                    take = PciScanTakeSetting(argv3[a], glob2[g], &src);
                    want = take;
                    take = PciScanTakeMe(take, me, &refused);
                    sprintf(what, "arg %d global %d me %d: Me never takes, and says when it set a take aside",
                            argv3[a], glob2[g], me);
                    expect(take == (me ? PCISCAN_TAKE_LEAVE : want) &&
                           refused == (me && want == PCISCAN_TAKE_TAKE), what);
                    sprintf(what, "arg %d global %d me %d: a set-up controller on Me is left", argv3[a], glob2[g], me);
                    expect(PciScanFirmwareTake(PCISCAN_FW_SET_UP, take) ==
                           (!me && want == PCISCAN_TAKE_TAKE), what);
                    expect(PciScanFirmwareTake(PCISCAN_FW_TAKE, take), "a controller not set up is taken on Me too");
                }
            }
        }
        refused = 7;
        expect(PciScanTakeMe(PCISCAN_TAKE_LEAVE, 1, &refused) == PCISCAN_TAKE_LEAVE && refused == 0,
               "Me, nothing to set aside: refused 0");
    }

    printf("%d failure(s)\n", failures);
    return failures;
}
