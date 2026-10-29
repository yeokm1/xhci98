/*
 * pfw_scan.h - the pure half of XHCIPFW.VXD (roadmap-usb4.md task 44.49,
 * design record 32 section 4.1): the scan of every PCI function, the
 * classification of each bridge's prefetchable window, the count of the
 * BARs inside a window to be closed, and the close with every write
 * verified and undone when it is not taken.
 *
 * DDK-free and C89, so test\test_pfw.c builds it on the host with the
 * same source the VxD links (design record 03's rule). No file-scope
 * static anywhere in this half and no data defined in it: the 98 DDK's
 * LINK 5.12 mislinks Open Watcom's static-symbol records and a code
 * reference back to data its own object defined (design record 30
 * section 6.1).
 *
 * The caller supplies the two configuration accesses (the VxD's port
 * pair CF8h/CFCh, the host suite's model): PfwCfgRead and PfwCfgWrite,
 * whole dwords at a dword-aligned offset.
 */

#ifndef PFW_SCAN_H
#define PFW_SCAN_H

typedef unsigned long PFW_U32;
typedef unsigned char PFW_U8;

/* Type 1 header offsets (PCI-to-PCI Bridge Architecture Specification,
 * the Type 1 configuration header). */
#define PFW_CFG_ID          0x00UL  /* vendor in bits 15:0              */
#define PFW_CFG_HDR         0x0CUL  /* header type in bits 23:16        */
#define PFW_CFG_BUS         0x18UL  /* primary, secondary, subordinate  */
#define PFW_CFG_PF          0x24UL  /* prefetchable base and limit      */
#define PFW_CFG_PFBU        0x28UL  /* prefetchable base, upper 32 bits */
#define PFW_CFG_PFLU        0x2CUL  /* prefetchable limit, upper 32 bits */

/* The closed form written to 24h: base address field FFF0h, limit 0000h
 * (Linux's own value for a window it does not assign, [linux]
 * pci_setup_bridge_mmio_pref). */
#define PFW_PF_CLOSED       0x0000FFF0UL
#define PFW_PF_ADDR_MASK    0xFFF0FFF0UL    /* the two address fields    */

/* Bridge record actions (record 32 section 6). */
#define PFW_ACT_CLOSED      1
#define PFW_ACT_REFUSED     2
#define PFW_ACT_PARKED      3
#define PFW_ACT_ACROSS      4

/* Result bits (record 32 section 6). */
#define PFW_RES_DONE        0x01UL  /* the scan finished                  */
#define PFW_RES_CHANGED     0x02UL  /* a bridge read otherwise mid-way    */
#define PFW_RES_REFUSED     0x04UL
#define PFW_RES_PARKED      0x08UL
#define PFW_RES_ACROSS      0x10UL
#define PFW_RES_FULL        0x20UL  /* a table below was full             */
#define PFW_RES_XHCI_HIGH   0x40UL  /* an xHCI controller's BAR above 4 GB */

#define PFW_MAX_BRIDGES     16
#define PFW_MAX_BARS        16
#define PFW_MAX_XHCI        8
#define PFW_CFG_CLASS       0x08UL  /* class code in bits 31:8          */
#define PFW_CLASS_XHCI      0x0C0330UL
/* Bridges with a window above or across 4 GB the scan can hold; a board
 * with more is logged full and the rest left as they are. */
#define PFW_MAX_SUBJ        64

/* 28 bytes: bus, device and function (device in bits 7:3), the action,
 * a zero byte, then 24h, 28h and 2Ch before the step and after it. */
typedef struct PFW_BRIDGE_REC {
    PFW_U8 Bus;
    PFW_U8 DevFn;
    PFW_U8 Action;
    PFW_U8 Zero;
    PFW_U32 Before[3];
    PFW_U32 After[3];
} PFW_BRIDGE_REC;

/* 8 bytes: bus, device and function, the BAR's offset, a zero byte,
 * the BAR's upper dword. */
typedef struct PFW_BAR_REC {
    PFW_U8 Bus;
    PFW_U8 DevFn;
    PFW_U8 Offset;
    PFW_U8 Zero;
    PFW_U32 Upper;
} PFW_BAR_REC;

/* 16 bytes: an xHCI controller (class 0C0330h) whose memory BAR has a
 * nonzero upper half (the owner, 2026-10-27; record 32 section 4.4):
 * bus, device and function, the BAR's offset, a zero byte, the vendor
 * and device ids as 00h reads, the address's upper and lower dwords. */
typedef struct PFW_XHCI_REC {
    PFW_U8 Bus;
    PFW_U8 DevFn;
    PFW_U8 Offset;
    PFW_U8 Zero;
    PFW_U32 Id;
    PFW_U32 Hi;
    PFW_U32 Lo;
} PFW_XHCI_REC;

typedef struct PFW_SUBJ {
    PFW_U32 Bus;
    PFW_U32 DevFn;
    PFW_U32 Sec;            /* secondary bus number                 */
    PFW_U32 Sub;            /* subordinate bus number               */
    PFW_U32 Reg[3];         /* 24h, 28h, 2Ch as the scan read them  */
    PFW_U32 Across;         /* 1: across 4 GB, left                 */
} PFW_SUBJ;

typedef struct PFW_RESULT {
    PFW_U32 Result;
    PFW_U32 Closed;
    PFW_U32 Refused;
    PFW_U32 Parked;
    PFW_U32 Across;
    PFW_U32 BarsInside;
    PFW_U32 Functions;      /* functions the scan found            */
    PFW_U32 NBridges;       /* records in Bridges                  */
    PFW_U32 NBars;          /* records in Bars                     */
    PFW_U32 NSubj;
    PFW_U32 XhciHigh;       /* xHCI BARs above 4 GB found          */
    PFW_U32 NXhci;          /* records in Xhci                     */
    PFW_XHCI_REC Xhci[PFW_MAX_XHCI];
    PFW_BRIDGE_REC Bridges[PFW_MAX_BRIDGES];
    PFW_BAR_REC Bars[PFW_MAX_BARS];
    PFW_SUBJ Subj[PFW_MAX_SUBJ];
} PFW_RESULT;

/* Supplied by the caller. */
PFW_U32 PfwCfgRead(PFW_U32 bus, PFW_U32 devfn, PFW_U32 off);
void PfwCfgWrite(PFW_U32 bus, PFW_U32 devfn, PFW_U32 off, PFW_U32 val);

/* The whole step (record 32 section 4.1): r is zeroed by the caller. */
void PfwRun(PFW_RESULT *r);

/* The parts, for the host suite. */
int PfwClassify(PFW_U32 r24, PFW_U32 r28, PFW_U32 r2c);
#define PFW_CLASS_LEAVE     0
#define PFW_CLASS_ABOVE     1
#define PFW_CLASS_ACROSS    2
int PfwSafe(PFW_U32 r24, PFW_U32 r28, PFW_U32 r2c);
int PfwClose(PFW_U32 bus, PFW_U32 devfn, PFW_U32 *after);
void PfwXhci(PFW_RESULT *r, PFW_U32 bus, PFW_U32 devfn);

#endif
