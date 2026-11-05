/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * pciscan.h - the two decisions of HwFindAdapter's NT4-ABI bus scan
 * (nvme2k.c), which 9x uses: whether a slot holds a function, and which
 * slot comes next. Pure, so the host unit tests hold them
 * (tests/unit/pciscan_ut.c); no DDK header needed.
 */

#ifndef _STOR98_PCISCAN_H_
#define _STOR98_PCISCAN_H_

// Every PCI bus number (0-255), not the first 16 the base scanned: a
// firmware numbers a hot-plug port's range far up (the emulator's asm4242
// model gives its two USB4 ports buses 19h-48h and 49h-78h), and a drive
// tunnelled there sits inside it.
#define PCISCAN_LAST_BUS    255UL
#define PCISCAN_SLOTS       256UL   // 32 devices x 8 functions per bus

// TRUE when a probe of a slot's vendor id (4 bytes from offset 0) found no
// function there (PLAN 3.4): a return of 0 (no such bus), 2 (no function)
// or anything short of the 4 bytes asked for, or a vendor id of 0000h or
// FFFFh.
static __inline int PciScanAbsent(unsigned long BytesRead,
                                  unsigned short VendorId)
{
    return BytesRead < 4UL || VendorId == 0xFFFF || VendorId == 0x0000;
}

// Moves *Bus and *Slot to the next slot to probe; FALSE once past the
// last bus (the scan is over, nothing found).
static __inline int PciScanNext(unsigned long *Bus, unsigned long *Slot)
{
    (*Slot)++;
    if (*Slot >= PCISCAN_SLOTS) {
        *Slot = 0;
        (*Bus)++;
        if (*Bus > PCISCAN_LAST_BUS) {
            return 0;
        }
    }
    return 1;
}

// 9x (PLAN 3.1): TRUE when a function is the one the port is starting.
// The port starts one devnode per load and gives one of that devnode's
// memory resources as ConfigInfo's first access range - not always BAR0:
// on a board read with an SK hynix drive, 98 SE gave the drive's second
// memory BAR (4 KB above BAR0) first. So the function is the NVMe
// function one of whose memory BARs lies at that range's start; BARs of
// different functions never share an address. No range from the port
// names no function, and nothing is taken: another NVMe controller in the
// machine (the system disk the BIOS drives, a drive another devnode owns)
// has other addresses and is never brought up. Bars holds the header's
// dwords from 10h, NumBars of them read (at most 6); a 64-bit BAR whose
// high dword was not read matches nothing.
#define PCISCAN_BARS    6UL

static __inline int PciScanIsPortFunction(int IsNvme,
                                          const unsigned long *Bars,
                                          unsigned long NumBars,
                                          unsigned long RangeLo,
                                          unsigned long RangeHi)
{
    unsigned long i, lo, hi;

    if (!IsNvme || (RangeLo == 0 && RangeHi == 0)) {
        return 0;
    }
    if (NumBars > PCISCAN_BARS) {
        NumBars = PCISCAN_BARS;
    }
    for (i = 0; i < NumBars; i++) {
        if ((Bars[i] & 0x1UL) != 0) {
            continue;                   // an I/O BAR
        }
        lo = Bars[i] & 0xFFFFFFF0UL;
        hi = 0;
        if ((Bars[i] & 0x6UL) == 0x4UL) {
            if (i + 1 >= NumBars) {
                return 0;
            }
            i++;
            hi = Bars[i];
        }
        if ((lo != 0 || hi != 0) && lo == RangeLo && hi == RangeHi) {
            return 1;
        }
    }
    return 0;
}

// 9x (PLAN 3.1, "A controller already set up"): whether the function the
// port is starting was already initialised by someone else - the BIOS, on
// the board where this was read - read before this driver writes anything.
// Not proof that the BIOS will use it again; a refusal of what may be
// another owner's. On that board (owner, 2026-11-01) the BIOS served a
// tunnelled drive through INT 13h, the port called HwAdapterState(FALSE)
// during start, and the controller this driver had reset could not be
// given back: the start hung. A controller with memory decode off cannot
// be in use and is taken; otherwise it is taken only disabled and not
// ready (CC.EN 0, CSTS.RDY 0). Enabled or ready, in either combination,
// no answer (all ones) or registers that could not be read are refused.
// ReadOk: CC and CSTS were read; Cmd: the PCI command register.
#define PCISCAN_FW_TAKE         0   // not set up: this driver may take it
#define PCISCAN_FW_SET_UP       1   // CC.EN or CSTS.RDY set
#define PCISCAN_FW_NO_ANSWER    2   // CSTS all ones
#define PCISCAN_FW_UNREADABLE   3   // decode on, registers not read

static __inline int PciScanFirmwareState(unsigned short Cmd,
                                         int ReadOk,
                                         unsigned long Cc,
                                         unsigned long Csts)
{
    if ((Cmd & 0x0002U) == 0) {
        return PCISCAN_FW_TAKE;
    }
    if (!ReadOk) {
        return PCISCAN_FW_UNREADABLE;
    }
    if (Csts == 0xFFFFFFFFUL) {
        return PCISCAN_FW_NO_ANSWER;
    }
    if ((Cc & 0x1UL) != 0 || (Csts & 0x1UL) != 0) {
        return PCISCAN_FW_SET_UP;
    }
    return PCISCAN_FW_TAKE;
}

#endif // _STOR98_PCISCAN_H_
