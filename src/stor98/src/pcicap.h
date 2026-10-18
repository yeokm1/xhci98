// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// pcicap.h - report-only reading of the controller's interrupt hardware from
// a copy of its PCI configuration header: Interrupt Line and Pin, and whether
// an MSI or MSI-X capability is present. Stor98 uses no MSI; the result only
// goes to the interrupt report (PLAN "Polling mode", the report). Pure
// arithmetic on a buffer the caller already read in a permitted callback
// (NT: HwFindAdapter; 9x: HwFindAdapter, G9), so the host unit test
// (tests/unit/poll_ut.c) includes it unchanged.
//
// Stor98's own definitions, from the PCI Local Bus Specification: the
// Status register at 06h, whose bit 4 (Capabilities List) says that the
// Capabilities Pointer at 34h is valid (PCI 2.2); capability IDs 05h MSI
// (PCI 2.2) and 11h MSI-X (PCI 3.0). The two low bits of every pointer are
// reserved and masked; a pointer below 40h ends the list (capabilities live
// after the 64-byte header), and the walk is bounded, so a looped or
// corrupt list cannot hold the callback.
//

#ifndef _STOR98_PCICAP_H_
#define _STOR98_PCICAP_H_

#define S98_PCI_STATUS              0x06    // Status register (16 bits)
#define S98_PCI_STATUS_CAPLIST      0x0010  // bit 4: Capabilities List
#define S98_PCI_CAP_PTR             0x34    // Capabilities Pointer
#define S98_PCI_INT_LINE            0x3C    // Interrupt Line
#define S98_PCI_INT_PIN             0x3D    // Interrupt Pin (0 none, 1-4 INTA-INTD)
#define S98_PCI_CAP_FIRST           0x40    // no capability below the header
#define S98_PCI_CAP_MAX_WALK        48      // at most (256 - 64) / 4 entries
#define S98_PCI_CAPID_MSI           0x05
#define S98_PCI_CAPID_MSIX          0x11

// PCI_INT_INFO.Flags
#define S98_PCI_HAS_PIN_LINE        0x01    // Line and Pin were read (buffer long enough)
#define S98_PCI_HAS_CAPLIST         0x02    // Status bit 4 set
#define S98_PCI_MSI                 0x04    // an MSI capability is present
#define S98_PCI_MSIX                0x08    // an MSI-X capability is present
#define S98_PCI_CAP_LOOP            0x10    // the walk hit its bound (looped or corrupt list)
#define S98_PCI_CAP_SHORT           0x20    // a pointer went past the bytes read

typedef struct _PCI_INT_INFO {
    UCHAR Line;
    UCHAR Pin;
    UCHAR Flags;
    UCHAR Caps;             // capabilities visited
} PCI_INT_INFO;

static VOID PciReadIntInfo(const UCHAR *Cfg, ULONG Len, PCI_INT_INFO *Info)
{
    ULONG ptr, n;
    USHORT status;

    Info->Line = 0;
    Info->Pin = 0;
    Info->Flags = 0;
    Info->Caps = 0;
    if (Cfg == NULL) {
        return;
    }
    if (Len > S98_PCI_INT_PIN) {
        Info->Line = Cfg[S98_PCI_INT_LINE];
        Info->Pin = Cfg[S98_PCI_INT_PIN];
        Info->Flags |= S98_PCI_HAS_PIN_LINE;
    }
    if (Len <= S98_PCI_CAP_PTR) {
        return;
    }
    status = (USHORT)(Cfg[S98_PCI_STATUS] | ((USHORT)Cfg[S98_PCI_STATUS + 1] << 8));
    if (!(status & S98_PCI_STATUS_CAPLIST)) {
        return;
    }
    Info->Flags |= S98_PCI_HAS_CAPLIST;
    ptr = Cfg[S98_PCI_CAP_PTR] & 0xFC;
    for (n = 0; ptr != 0; n++) {
        if (n >= S98_PCI_CAP_MAX_WALK) {
            Info->Flags |= S98_PCI_CAP_LOOP;
            break;
        }
        if (ptr < S98_PCI_CAP_FIRST) {
            break;
        }
        if (ptr + 1 >= Len) {
            Info->Flags |= S98_PCI_CAP_SHORT;
            break;
        }
        Info->Caps++;
        if (Cfg[ptr] == S98_PCI_CAPID_MSI) {
            Info->Flags |= S98_PCI_MSI;
        } else if (Cfg[ptr] == S98_PCI_CAPID_MSIX) {
            Info->Flags |= S98_PCI_MSIX;
        }
        ptr = Cfg[ptr + 1] & 0xFC;
    }
}

#endif // _STOR98_PCICAP_H_
