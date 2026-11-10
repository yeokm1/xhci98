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
// memory BAR (16 KiB above BAR0) first. So the function is the NVMe
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
// the board where this was read - read before this driver does anything
// else to it. Not proof that the BIOS will use it again; a refusal of what
// may be another owner's. On that board (owner, 2026-11-01) the BIOS served
// a tunnelled drive through INT 13h, the port called HwAdapterState(FALSE)
// during start, and the controller this driver had reset could not be
// given back: the start hung. A controller is taken only when CC and CSTS
// were read and show it disabled and not ready (CC.EN 0, CSTS.RDY 0).
// Enabled or ready, in either combination, no answer (all ones), registers
// that could not be reached, or a command register this driver changed for
// the read and could not put back, are refused. Memory decode off is no
// exemption (xhci98 3.4.2.0, its roadmap 47.9): a firmware may leave a
// controller enabled with decode off, so decode is turned on for the read
// (PciScanReadFirmware) where BAR0 is shown to be this devnode's
// (PciScanFirmwareReach), and put back.
#define PCISCAN_FW_TAKE         0   // not set up: this driver may take it
#define PCISCAN_FW_SET_UP       1   // CC.EN or CSTS.RDY set
#define PCISCAN_FW_NO_ANSWER    2   // CC or CSTS all ones
#define PCISCAN_FW_UNREADABLE   3   // the registers were not reached
#define PCISCAN_FW_NOT_RESTORED 4   // decode turned on for the read, not put back

#define PCISCAN_CMD_MEMORY      0x0002U     // PCI command: Memory Space Enable
#define PCISCAN_NVME_CC         0x14UL      // NVMe Controller Configuration
#define PCISCAN_NVME_CSTS       0x1CUL      // NVMe Controller Status
#define PCISCAN_PAGE            0x1000UL

// Whether BAR0 may be reached for the read, before anything is written:
// PCISCAN_REACH_OK, or why not. BytesRead: the header bytes read (the
// command word and BAR0/BAR1 must be among them); Cmd: the command word as
// found; Bar0, Bar1: the header's dwords at 10h and 14h; Range*: the port's
// access range. BAR0 must be a memory BAR at a nonzero address below 4 GiB
// (a 64-bit BAR's high dword 0). With decode on it already decodes there.
// With decode off it is turned on only at an address shown to be this
// devnode's: the start of the port's range (the devnode's first memory
// resource, allocated to it and no other device), at least a page long; a
// range that names another of the function's BARs leaves BAR0's allocation
// unproven (the port gives one range).
#define PCISCAN_REACH_OK            0
#define PCISCAN_REACH_SHORT         1   // the header does not hold the command word and BAR0/BAR1
#define PCISCAN_REACH_IO            2   // BAR0 is an I/O BAR
#define PCISCAN_REACH_UNASSIGNED    3   // BAR0's address is 0
#define PCISCAN_REACH_HIGH          4   // BAR0 at or above 4 GiB
#define PCISCAN_REACH_NOT_PORT      5   // decode off: BAR0 is not the port's range
#define PCISCAN_REACH_RANGE_SHORT   6   // decode off: the port's range is shorter than a page
#define PCISCAN_REACH_NO_MAP        7   // BAR0's page could not be mapped (nvme2k.c)

static __inline int PciScanFirmwareReach(unsigned long BytesRead,
                                         unsigned short Cmd,
                                         unsigned long Bar0,
                                         unsigned long Bar1,
                                         unsigned long RangeLo,
                                         unsigned long RangeHi,
                                         unsigned long RangeLen)
{
    unsigned long hi = 0;

    if (BytesRead < 0x18UL) {
        return PCISCAN_REACH_SHORT;
    }
    if ((Bar0 & 0x1UL) != 0) {
        return PCISCAN_REACH_IO;
    }
    if ((Bar0 & 0x6UL) == 0x4UL) {
        hi = Bar1;
    }
    if (hi != 0) {
        return PCISCAN_REACH_HIGH;
    }
    if ((Bar0 & 0xFFFFFFF0UL) == 0) {
        return PCISCAN_REACH_UNASSIGNED;
    }
    if ((Cmd & PCISCAN_CMD_MEMORY) == 0) {
        if (RangeHi != 0 || RangeLo != (Bar0 & 0xFFFFFFF0UL)) {
            return PCISCAN_REACH_NOT_PORT;
        }
        if (RangeLen < PCISCAN_PAGE) {
            return PCISCAN_REACH_RANGE_SHORT;
        }
    }
    return PCISCAN_REACH_OK;
}

// The read itself, once PciScanFirmwareReach allowed it and BAR0's page is
// mapped, over three operations the caller gives (nvme2k.c: the config
// helpers and the mapped page; the host test: fakes that record each call):
// ReadCmd TRUE only when the helper obtained the whole prefix it reads
// (ReadPciConfigWord: offsets 0 to 5, the word at 04h taken from it);
// WriteCmd TRUE only when the port reported the 2 bytes written (one 16-bit
// write at 04h; Status at 06h, whose bits a write of ones would clear, is
// not touched); ReadReg a 32-bit read at an offset of BAR0.
// With decode on as found: CC and CSTS read, nothing written. With decode
// off: the command word written as found with Memory Space Enable added
// (every other bit as found) and read back; only when the write and the
// read moved their bytes and the bit reads on are CC and CSTS read (an
// endpoint that does not decode answers Unsupported Request, which SERR
// reporting may turn into a platform error). After every enable write,
// whatever it gave, the word as found is written back and read back: put
// back only when both moved their bytes and the word reads as found.
// Returns PCISCAN_READ_*; *TurnedOn TRUE when decode was turned on and the
// registers read.
#define PCISCAN_READ_OK             0   // CC and CSTS read (the command word as found)
#define PCISCAN_READ_NONE           1   // not read: decode did not turn on
#define PCISCAN_READ_NOT_RESTORED   2   // the command word not put back as found

typedef struct _PCISCAN_FW_IO {
    void *Ctx;
    int (*ReadCmd)(void *Ctx, unsigned short *Value);
    int (*WriteCmd)(void *Ctx, unsigned short Value);
    unsigned long (*ReadReg)(void *Ctx, unsigned long Offset);
} PCISCAN_FW_IO;

static __inline int PciScanReadFirmware(const PCISCAN_FW_IO *Io,
                                        unsigned short CmdFound,
                                        unsigned long *Cc,
                                        unsigned long *Csts,
                                        int *TurnedOn)
{
    unsigned short back;
    int on, restored;

    *Cc = 0;
    *Csts = 0;
    *TurnedOn = 0;
    if ((CmdFound & PCISCAN_CMD_MEMORY) != 0) {
        *Cc = Io->ReadReg(Io->Ctx, PCISCAN_NVME_CC);
        *Csts = Io->ReadReg(Io->Ctx, PCISCAN_NVME_CSTS);
        return PCISCAN_READ_OK;
    }
    back = 0;
    on = Io->WriteCmd(Io->Ctx, (unsigned short)(CmdFound | PCISCAN_CMD_MEMORY));
    if (on) {
        on = Io->ReadCmd(Io->Ctx, &back) && (back & PCISCAN_CMD_MEMORY) != 0;
    }
    if (on) {
        *Cc = Io->ReadReg(Io->Ctx, PCISCAN_NVME_CC);
        *Csts = Io->ReadReg(Io->Ctx, PCISCAN_NVME_CSTS);
        *TurnedOn = 1;
    }
    back = 0;
    restored = Io->WriteCmd(Io->Ctx, CmdFound);
    if (restored) {
        restored = Io->ReadCmd(Io->Ctx, &back) && back == CmdFound;
    }
    if (!restored) {
        return PCISCAN_READ_NOT_RESTORED;
    }
    return on ? PCISCAN_READ_OK : PCISCAN_READ_NONE;
}

// The decision on what was read: ReadHow is PCISCAN_READ_*, or
// PCISCAN_READ_NONE when PciScanFirmwareReach refused the read.
static __inline int PciScanFirmwareState(int ReadHow,
                                         unsigned long Cc,
                                         unsigned long Csts)
{
    if (ReadHow == PCISCAN_READ_NOT_RESTORED) {
        return PCISCAN_FW_NOT_RESTORED;
    }
    if (ReadHow != PCISCAN_READ_OK) {
        return PCISCAN_FW_UNREADABLE;
    }
    // All ones in either register is no answer, never "set up": the
    // user's option (PciScanFirmwareTake) overrides "set up" alone.
    if (Csts == 0xFFFFFFFFUL || Cc == 0xFFFFFFFFUL) {
        return PCISCAN_FW_NO_ANSWER;
    }
    if ((Cc & 0x1UL) != 0 || (Csts & 0x1UL) != 0) {
        return PCISCAN_FW_SET_UP;
    }
    return PCISCAN_FW_TAKE;
}

// 9x (PLAN "Taking a controller the BIOS set up", xhci98 3.4.3.0): the
// user's option to take a controller the check above found set up. Off by
// default; it overrides PCISCAN_FW_SET_UP alone.
//
// PciScanTakeArg: the controller's own setting in the string the 9x port
// hands HwFindAdapter (the devnode's AdapterSettings). Fields are separated
// by ';'; a field is the key only at the string's start or after a ';',
// compared case-insensitively, and its value is exactly '0' or '1' ended by
// ';' or the string's end. A 0 in any such field leaves (the safe answer to
// a contradiction); else a 1 in any takes; else none.
#define PCISCAN_TAKE_NONE       (-1)    // no setting of its own
#define PCISCAN_TAKE_LEAVE      0
#define PCISCAN_TAKE_TAKE       1

#define PCISCAN_TAKE_KEY        "stortakefirmware="

static __inline int PciScanTakeArg(const char *s)
{
    const char *k;
    const char *p;
    int sawTake = 0;
    int c;

    if (s == 0) {
        return PCISCAN_TAKE_NONE;
    }
    p = s;
    for (;;) {
        // p is a field's start.
        k = PCISCAN_TAKE_KEY;
        while (*k != 0) {
            c = (unsigned char)*p;
            if (c >= 'A' && c <= 'Z') {
                c += 'a' - 'A';
            }
            if (c != (unsigned char)*k) {
                break;
            }
            p++;
            k++;
        }
        if (*k == 0 && (p[0] == '0' || p[0] == '1') && (p[1] == ';' || p[1] == 0)) {
            if (p[0] == '0') {
                return PCISCAN_TAKE_LEAVE;
            }
            sawTake = 1;
        }
        // On to the next field.
        while (*p != 0 && *p != ';') {
            p++;
        }
        if (*p == 0) {
            break;
        }
        p++;
    }
    return sawTake ? PCISCAN_TAKE_TAKE : PCISCAN_TAKE_NONE;
}

// PciScanTakeGlobal: StorTakeFirmware under Services\stor98 as read
// (Present FALSE when the value was not read): a REG_DWORD (4) of exactly
// 1, or a REG_SZ (1) that is exactly "1" (with or without its terminating
// zero in Size), takes; anything else leaves.
static __inline int PciScanTakeGlobal(int Present,
                                      unsigned long Type,
                                      const unsigned char *Data,
                                      unsigned long Size)
{
    if (!Present || Data == 0) {
        return PCISCAN_TAKE_LEAVE;
    }
    if (Type == 4UL) {
        return (Size == 4UL && Data[0] == 1 && Data[1] == 0 && Data[2] == 0 && Data[3] == 0)
               ? PCISCAN_TAKE_TAKE : PCISCAN_TAKE_LEAVE;
    }
    if (Type == 1UL) {
        return (Data[0] == '1' && (Size == 1UL || (Size == 2UL && Data[1] == 0)))
               ? PCISCAN_TAKE_TAKE : PCISCAN_TAKE_LEAVE;
    }
    return PCISCAN_TAKE_LEAVE;
}

// PciScanTakeSetting: the decision and its source. The controller's own
// setting when it has one (its 0 leaves against a global 1, its 1 takes
// against a global 0), else the global value's.
#define PCISCAN_TAKESRC_ADAPTER 1   // the controller's stortakefirmware=
#define PCISCAN_TAKESRC_GLOBAL  2   // StorTakeFirmware

static __inline int PciScanTakeSetting(int Arg, int Global, int *Source)
{
    if (Arg == PCISCAN_TAKE_LEAVE || Arg == PCISCAN_TAKE_TAKE) {
        *Source = PCISCAN_TAKESRC_ADAPTER;
        return Arg;
    }
    *Source = PCISCAN_TAKESRC_GLOBAL;
    return Global == PCISCAN_TAKE_TAKE ? PCISCAN_TAKE_TAKE : PCISCAN_TAKE_LEAVE;
}

// PciScanTakeMe: the decision on this system. Windows Me never takes a
// controller someone else set up (xhci98's owner, 2026-11-09, its roadmap
// 48.28: a take there hung Me's start after the desktop came, in the BIOS's
// own code), whatever the settings say. Me: 1 on Windows Me. *Refused is 1
// when a take decision was set aside for that, else 0.
static __inline int PciScanTakeMe(int Take, int Me, int *Refused)
{
    *Refused = (Take == PCISCAN_TAKE_TAKE && Me) ? 1 : 0;
    return *Refused ? PCISCAN_TAKE_LEAVE : Take;
}

// PciScanFirmwareTake: whether the find goes on with this function: one not
// set up, or one set up that the user's decision takes. Never no answer,
// unreadable or not restored.
static __inline int PciScanFirmwareTake(int State, int Take)
{
    if (State == PCISCAN_FW_TAKE) {
        return 1;
    }
    return State == PCISCAN_FW_SET_UP && Take == PCISCAN_TAKE_TAKE;
}

#endif // _STOR98_PCISCAN_H_
