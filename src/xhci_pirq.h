/*
 * xhci_pirq.h - the Intel PCH interrupt routing quirk, its pure half
 * (roadmap-hcd.md task 36.3; docs/issues/12-e7250-xp64-boot-enable-slot-
 * refused.md section 5.1).
 *
 * On the 7-, 8- and 9-series PCH and on Wellsburg, the xHCI's INTA reaches
 * an interrupt input through two chipset registers in the RCBA space the LPC
 * bridge's F0h names: D20IP says which pin the xHCI (device 20) drives, and
 * D20IR's IAR field (bits 2:0) routes that pin to PIRQA#-H#. A PIRQ reaches
 * I/O APIC input 16-23 in APIC mode, and an 8259 input through the LPC's
 * PIRQx_ROUT registers in PIC mode. The Dell Latitude E7250's BIOS leaves
 * D20IR routing INTA to PIRQA while its ACPI tables tell the OS the xHCI is
 * on PIRQF's IRQ 21, so the line the ISR is connected to never carries the
 * xHCI's interrupt (issue 12 section 2). The quirk derives the PIRQ that
 * delivers the IRQ the OS assigned and writes it into IAR, and nothing
 * else.
 *
 * XhciIntelIrqRoute (owner, 2026-10-08): 0 writes nothing and reports; 1,
 * the default and the meaning of a value missing, of another type or out
 * of range, writes only on the verified E7250 (xHCI 8086:9CB1, subsystem
 * 1028:062D, LPC 8086:9CC3); 2 writes on any reviewed xHCI/LPC pair. Every
 * check below holds at every value. The registers are read on a reviewed
 * xHCI only, and only the reviewed pairs are written: no unlisted Intel
 * part, no 100-series or later (PCR/ITSS behind P2SB) and no other vendor.
 *
 * Only a line delivery is repaired (owner, 2026-10-09). Under a message
 * interrupt or a poll the route carries nothing, so the run reports and
 * writes nothing; it decides again at each start and each return to D0.
 *
 * The executor is hcd_ctl.c: it supplies the xHCI's own configuration
 * space, the bus-data reads of 00:14.0 and 00:1F.0, one noncached page at
 * RCBA+3000h and the value, and records what comes back. Nothing here
 * touches a register, takes a lock or calls a kernel service.
 *
 * DDK-free: part of the pure core. C89. IRQL: any for the functions that
 * take no XHCI_PIRQ_IO; XhciPirqRun runs at its callbacks' IRQL, which with
 * hcd_ctl.c's is PASSIVE_LEVEL under no spin lock.
 */

#ifndef XHCI_PIRQ_H
#define XHCI_PIRQ_H

#include "xhci_compat.h"

/* PCI locations on bus 0: the xHCI is device 20 and the LPC bridge device
 * 31, each function 0, on every reviewed PCH. */
#define XHCI_PIRQ_XHCI_DEVICE       0x14UL
#define XHCI_PIRQ_LPC_DEVICE        0x1FUL

/* Configuration offsets, in either function's space. */
#define XHCI_PIRQ_CFG_ID            0x00UL  /* vendor 15:0, device 31:16    */
#define XHCI_PIRQ_CFG_BAR0          0x10UL
#define XHCI_PIRQ_CFG_BAR1          0x14UL
#define XHCI_PIRQ_CFG_SUBSYSTEM     0x2CUL  /* SVID 15:0, SSID 31:16        */
#define XHCI_PIRQ_CFG_INTERRUPT     0x3CUL  /* line 7:0, pin 15:8           */
#define XHCI_PIRQ_LPC_PIRQ_AD       0x60UL  /* PIRQA-D_ROUT, a byte each    */
#define XHCI_PIRQ_LPC_PIRQ_EH       0x68UL  /* PIRQE-H_ROUT, a byte each    */
#define XHCI_PIRQ_LPC_RCBA          0xF0UL  /* base 31:14, enable bit 0     */

/* How much of each header the bus-data reads take. */
#define XHCI_PIRQ_SLOT_BYTES        0x40UL
#define XHCI_PIRQ_LPC_BYTES         0x100UL

/* PIRQx_ROUT: bit 7 IRQEN (0 routes the PIRQ to the 8259), bits 3:0 the
 * ISA IRQ. */
#define XHCI_PIRQ_ROUT_IRQEN        0x80UL
#define XHCI_PIRQ_ROUT_IRQ          0x0FUL

/* RCBA, and the one page of it the run maps. */
#define XHCI_PIRQ_RCBA_ENABLE       0x00000001UL
#define XHCI_PIRQ_RCBA_BASE         0xFFFFC000UL
#define XHCI_PIRQ_WINDOW            0x3000UL
#define XHCI_PIRQ_WINDOW_BYTES      0x1000UL
#define XHCI_PIRQ_D20IP             0x128UL /* RCBA+3128h, 32-bit           */
#define XHCI_PIRQ_D20IR             0x160UL /* RCBA+3160h, 16-bit           */
#define XHCI_PIRQ_D20IP_PIN         0x0000000FUL
#define XHCI_PIRQ_IAR               0x0007U /* D20IR 2:0, INTA's PIRQ       */
#define XHCI_PIRQ_PIN_INTA          1UL

#define XHCI_PIRQ_UNREAD            0xFFFFFFFFUL

/* XhciIntelIrqRoute's meanings. */
#define XHCI_PIRQ_MODE_OFF          0UL     /* report only                  */
#define XHCI_PIRQ_MODE_VERIFIED     1UL     /* the E7250 only               */
#define XHCI_PIRQ_MODE_REVIEWED     2UL     /* any reviewed pair            */

/* How the controller's events are delivered this start. */
#define XHCI_PIRQ_DELIVERY_NONE     0UL     /* no interrupt resource        */
#define XHCI_PIRQ_DELIVERY_LINE     1UL
#define XHCI_PIRQ_DELIVERY_MESSAGE  2UL     /* MSI or MSI-X                 */
#define XHCI_PIRQ_DELIVERY_POLL     3UL

/* The path the raw IRQ's number names: 16-23 the PIRQs wired direct to
 * I/O APIC inputs 16-23, below 16 a PIRQx_ROUT byte steering a PIRQ to it.
 * The number names a path, not the HAL's mode: an I/O APIC HAL can hand
 * out an IRQ below 16 too. The names are kept. */
#define XHCI_PIRQ_IRQ_NONE          0UL
#define XHCI_PIRQ_IRQ_APIC          1UL     /* 16-23: the direct path       */
#define XHCI_PIRQ_IRQ_PIC           2UL     /* below 16: PIRQx_ROUT steered */

/* When a run was made (XHCI_PIRQ_STATE.When). */
#define XHCI_PIRQ_WHEN_NONE         0UL
#define XHCI_PIRQ_WHEN_START        1UL
#define XHCI_PIRQ_WHEN_RESUME       2UL

/*
 * What ended a run (XHCI_PIRQ_STATE.Decision), in the order it checks.
 * Each one before WROTE writes nothing; the run reads no further than the
 * check that ended it, so a field past it stays XHCI_PIRQ_UNREAD.
 */
#define XHCI_PIRQ_D_NONE            0UL     /* not run                      */
#define XHCI_PIRQ_D_BAD_PARAM       1UL
#define XHCI_PIRQ_D_OWN_UNREAD      2UL     /* the xHCI's own config        */
#define XHCI_PIRQ_D_NOT_REVIEWED    3UL     /* not a reviewed xHCI: no read */
#define XHCI_PIRQ_D_SLOT_UNREAD     4UL     /* 00:14.0 not read             */
#define XHCI_PIRQ_D_NOT_AT_SLOT     5UL     /* 00:14.0 is not this function */
#define XHCI_PIRQ_D_LPC_UNREAD      6UL     /* 00:1F.0 not read             */
#define XHCI_PIRQ_D_LPC_UNLISTED    7UL     /* not this xHCI's LPC ids      */
#define XHCI_PIRQ_D_RCBA_INVALID    8UL     /* disabled, zero or all-ones   */
#define XHCI_PIRQ_D_MAP_FAILED      9UL
#define XHCI_PIRQ_D_PIN             10UL    /* D20IP or the pin not INTA    */
#define XHCI_PIRQ_D_D20IR_UNREAD    11UL
#define XHCI_PIRQ_D_MESSAGE         12UL    /* a message interrupt: report  */
#define XHCI_PIRQ_D_POLL            13UL    /* polled: report               */
#define XHCI_PIRQ_D_NO_INTERRUPT    14UL    /* no interrupt resource        */
#define XHCI_PIRQ_D_IRQ_AMBIGUOUS   15UL    /* not one raw line, or L != V  */
#define XHCI_PIRQ_D_IRQ_UNROUTABLE  16UL    /* in neither range             */
#define XHCI_PIRQ_D_PIC_NO_PIRQ     17UL    /* no routed PIRQ names the IRQ */
#define XHCI_PIRQ_D_ROUTED          18UL    /* IAR already delivers it      */
#define XHCI_PIRQ_D_STEERED_NT      19UL    /* below 16 on NT: report       */
#define XHCI_PIRQ_D_OFF             20UL    /* value 0: would have written  */
#define XHCI_PIRQ_D_NOT_ADMITTED    21UL    /* value 1, not the E7250       */
#define XHCI_PIRQ_D_WROTE           22UL    /* written and read back        */
#define XHCI_PIRQ_D_WRITE_FAILED    23UL    /* write or read-back failed    */
#define XHCI_PIRQ_D_COUNT           24UL

/* The xHCI's own configuration space, one dword; return 1 when read. */
typedef ULONG (*XHCI_PIRQ_OWN)(PVOID context, ULONG offset, PULONG value);
/* Bus 0, the device and function given, from offset 0 for length bytes;
 * return the bytes read, as HalGetBusData does. */
typedef ULONG (*XHCI_PIRQ_SLOT)(PVOID context, ULONG device, ULONG function,
                                PUCHAR buffer, ULONG length);
/* One noncached page at the physical address given (below 4 GB, page
 * aligned); return 1 when mapped. The accessors take offsets in it, and
 * Unmap ends it. */
typedef ULONG (*XHCI_PIRQ_MAP)(PVOID context, ULONG physical);
typedef ULONG (*XHCI_PIRQ_READ32)(PVOID context, ULONG offset, PULONG value);
typedef ULONG (*XHCI_PIRQ_READ16)(PVOID context, ULONG offset,
                                  PUSHORT value);
typedef ULONG (*XHCI_PIRQ_WRITE16)(PVOID context, ULONG offset,
                                   USHORT value);
typedef VOID (*XHCI_PIRQ_UNMAP)(PVOID context);

typedef struct _XHCI_PIRQ_IO {
    XHCI_PIRQ_OWN Own;
    XHCI_PIRQ_SLOT Slot;
    XHCI_PIRQ_MAP Map;
    XHCI_PIRQ_READ32 Read32;
    XHCI_PIRQ_READ16 Read16;
    XHCI_PIRQ_WRITE16 Write16;
    XHCI_PIRQ_UNMAP Unmap;
    PVOID Context;
    /* 1 where an IRQ below 16 is the 8259 path for certain, Windows 98
     * and ME, so the PIRQx_ROUT-steered target may be written; 0 on 2000,
     * XP, Vista and 7, where an I/O APIC HAL can hand out a low IRQ too
     * and which route it uses is not proven: such a run reports
     * XHCI_PIRQ_D_STEERED_NT (owner, 2026-10-09). */
    ULONG Steerable;
} XHCI_PIRQ_IO, *PXHCI_PIRQ_IO;

/* The raw interrupt resource, as the start's parser kept it: how many
 * interrupt descriptors the raw list holds, and the first one's fields.
 * The translated vector and IRQL are never used. Under
 * CM_RESOURCE_INTERRUPT_MESSAGE the first word is a reserved USHORT then
 * MessageCount, so Level holds the count in 31:16; the run reports a
 * message delivery before it reads Level. */
typedef struct _XHCI_PIRQ_IRQ {
    ULONG Count;
    ULONG Level;
    ULONG Vector;
    ULONG Flags;        /* CM_RESOURCE_INTERRUPT_*                          */
    ULONG Share;        /* CM_SHARE_DISPOSITION                             */
} XHCI_PIRQ_IRQ, *PXHCI_PIRQ_IRQ;

/*
 * The record (all ULONGs, copied whole into the snapshot's HCD region and
 * named there by the snapshot reader, xhcitr\tr_snap.c, in this order). The
 * three counts are never
 * zeroed; Mode and the value are the start's (XhciPirqLatch); the rest is
 * the last run's, XHCI_PIRQ_UNREAD where it did not read so far.
 */
typedef struct _XHCI_PIRQ_STATE {
    ULONG Runs;
    ULONG Writes;       /* D20IR writes issued                              */
    ULONG WriteFailures;/* write or read-back refused, or read-back differs */
    ULONG Mode;         /* XHCI_PIRQ_MODE_*                                 */
    ULONG ValueFound;   /* XhciIntelIrqRoute found as a DWORD               */
    ULONG Value;        /* its value, XHCI_PIRQ_UNREAD when not found       */
    ULONG When;         /* XHCI_PIRQ_WHEN_*                                 */
    ULONG Delivery;     /* XHCI_PIRQ_DELIVERY_*                             */
    ULONG Decision;     /* XHCI_PIRQ_D_*                                    */
    ULONG XhciId;       /* own config 00h                                   */
    ULONG XhciSubsystem;/* own config 2Ch                                   */
    ULONG XhciPin;      /* own config 3Dh                                   */
    ULONG SlotId;       /* 00:14.0's 00h through the bus-data path          */
    ULONG LpcId;        /* 00:1F.0's 00h                                    */
    ULONG Rcba;         /* 00:1F.0's F0h                                    */
    ULONG PirqAd;       /* 00:1F.0's 60h-63h                                */
    ULONG PirqEh;       /* 00:1F.0's 68h-6Bh                                */
    ULONG IrqCount;     /* the raw resource, as given                       */
    ULONG IrqLevel;
    ULONG IrqVector;
    ULONG IrqFlags;
    ULONG IrqShare;
    ULONG IrqMode;      /* XHCI_PIRQ_IRQ_*                                  */
    ULONG D20ip;
    ULONG D20ir;        /* before any write                                 */
    ULONG Target;       /* the PIRQ, 0-7                                    */
    ULONG D20irAfter;   /* read back after the write                        */
} XHCI_PIRQ_STATE, *PXHCI_PIRQ_STATE;

#define XHCI_PIRQ_STATE_WORDS       27UL

XHCI_C_ASSERT(xhci_pirq_state_words,
              sizeof(XHCI_PIRQ_STATE) == XHCI_PIRQ_STATE_WORDS * sizeof(ULONG));

/* XhciIntelIrqRoute: 0, 1 or 2 when found as such; 1 otherwise. */
ULONG XhciPirqMode(ULONG found, ULONG value);

/* 1 when PCI offset 0 names a reviewed xHCI: Intel 1E31, 8C31, 9C31,
 * 8CB1, 9CB1 or 8D31. */
ULONG XhciPirqReviewed(ULONG xhciId);

/* 1 when the LPC's offset 0 is one of the reviewed xHCI's own LPC ids. */
ULONG XhciPirqPairListed(ULONG xhciId, ULONG lpcId);

/* 1 for the verified E7250: 8086:9CB1, subsystem 1028:062D, LPC 8086:9CC3. */
ULONG XhciPirqVerified(ULONG xhciId, ULONG subsystem, ULONG lpcId);

/*
 * The chipset-routed GSI offer (task 47.33; design record 18 sections
 * 16.1 and 16.2, on 2000, XP and XP x64).
 * On a controller whose firmware gives Windows 2000/XP no interrupt for it
 * (decodes off at boot and no _PRT entry), the NT 5 requirements filter may
 * offer, before its memory-only configuration, the I/O APIC input the PCH
 * really routes the xHCI's INTA to: D20IR's IAR names PIRQA-H, which are
 * inputs 16-23 in APIC mode. Read-only: no register is written, and the
 * I/O APIC is Windows' to program for whatever it grants.
 *
 * XhciIntelNoRouteIrq (owner, 2026-11-06): 0 never offers; 1, the default
 * and the meaning of a value missing, of another type or out of range,
 * offers on value 1's allowlist alone (HP 7-series boards, read on the
 * owner's 8470p), each also a reviewed pair; 2 on any reviewed xHCI/LPC
 * pair.
 */
#define XHCI_PIRQ_NR_OFF            0UL     /* never offer                  */
#define XHCI_PIRQ_NR_LISTED         1UL     /* the listed subsystems        */
#define XHCI_PIRQ_NR_REVIEWED       2UL     /* any reviewed pair            */

/* An APIC-mode PIRQ's I/O APIC input: PIRQA is 16. */
#define XHCI_PIRQ_GSI_BASE          16UL
#define XHCI_PIRQ_NO_GSI            0xFFFFFFFFUL

/* What ended an offer's run (XHCI_PIRQ_OFFER.Decision), in check order. */
#define XHCI_PIRQ_O_NONE            0UL     /* not run                      */
#define XHCI_PIRQ_O_BAD_PARAM       1UL
#define XHCI_PIRQ_O_OFF             2UL     /* the value 0                  */
#define XHCI_PIRQ_O_NOT_SLOT        3UL     /* the list is not 0:14.0's     */
#define XHCI_PIRQ_O_OWN_UNREAD      4UL     /* 00:14.0's header not read    */
#define XHCI_PIRQ_O_NOT_REVIEWED    5UL
#define XHCI_PIRQ_O_NOT_ADMITTED    6UL     /* value 1, subsystem unlisted  */
#define XHCI_PIRQ_O_LPC_UNREAD      7UL
#define XHCI_PIRQ_O_LPC_UNLISTED    8UL
#define XHCI_PIRQ_O_RCBA_INVALID    9UL
#define XHCI_PIRQ_O_MAP_FAILED      10UL
#define XHCI_PIRQ_O_PIN             11UL    /* own pin or D20IP not INTA    */
#define XHCI_PIRQ_O_D20IR_UNREAD    12UL
#define XHCI_PIRQ_O_OFFERED         13UL    /* Gsi is the offer             */
#define XHCI_PIRQ_O_QEMU            14UL    /* the qemu flavour's injection */
/* 15, "a 32-bit Windows: no offer", is no longer set (design record 18
 * section 16.2): reserved, so a snapshot of an earlier release reads as
 * it did. */
#define XHCI_PIRQ_O_NOT_X64         15UL
#define XHCI_PIRQ_O_NOT_AT_SLOT     16UL    /* 00:14.0 is not this function */

/* The offer's record (all ULONGs; the start writes it to the notes). */
typedef struct _XHCI_PIRQ_OFFER {
    ULONG Decision;     /* XHCI_PIRQ_O_*                                    */
    ULONG Mode;         /* XHCI_PIRQ_NR_*                                   */
    ULONG XhciId;       /* 00:14.0's 00h                                    */
    ULONG Subsystem;    /* 00:14.0's 2Ch                                    */
    ULONG Pin;          /* 00:14.0's 3Dh                                    */
    ULONG LpcId;        /* 00:1F.0's 00h                                    */
    ULONG Rcba;         /* 00:1F.0's F0h                                    */
    ULONG D20ip;
    ULONG D20ir;
    ULONG Pirq;         /* IAR, 0-7 (PIRQA-H)                               */
    ULONG Gsi;          /* XHCI_PIRQ_NO_GSI unless offered                  */
} XHCI_PIRQ_OFFER, *PXHCI_PIRQ_OFFER;

/* XhciIntelNoRouteIrq: 0, 1 or 2 when found as such; 1 otherwise. */
ULONG XhciPirqNoRouteMode(ULONG found, ULONG value);

/* 1 for an xHCI (00:14.0's 00h) and subsystem (2Ch) value 1's allowlist
 * admits: Intel 1E31 (7 series) with HP (103C) boards 179B, 17A7, 17AB,
 * 17DF, 176C and 176B. */
ULONG XhciPirqNoRouteListed(ULONG xhciId, ULONG subsystem);

/*
 * Windows 2000's strip (task 47.33; design record 18 section 16.4): 1 when
 * the filter must take every interrupt out of the controller's list - on
 * Windows 2000 (`isW2k`), where the offer's run passed its identity
 * guards (`decision` XHCI_PIRQ_O_LPC_UNREAD to XHCI_PIRQ_O_OFFERED: the
 * location, 00:14.0's identity the controller's own, a reviewed controller;
 * whatever the route's reads then found, a failed mapping included; never
 * at value 0, which ends the run as XHCI_PIRQ_O_OFF) and the board is on
 * value 1's list (`listed`, at value 2 too). Windows 2000's ACPI stops with 0xA5 on these boards' firmware for
 * a list carrying the line; XP and XP x64 are never stripped.
 */
ULONG XhciPirqW2kStrip(ULONG isW2k, ULONG decision, ULONG listed);

/* Clears the record: decision NONE, every read field XHCI_PIRQ_UNREAD, no
 * GSI. */
VOID XhciPirqOfferClear(PXHCI_PIRQ_OFFER st);

/*
 * The offer's run for the controller the PDO places at PCI bus `bus`,
 * `address` (DevicePropertyAddress: device 31:16, function 15:0), whose own
 * configuration space gave `ownId` (00h) and `ownSubsystem` (2Ch). The
 * location must be 0, 00:14.0, and 00:14.0's header, read through
 * io->Slot, must carry the same two dwords; then 00:1F.0, RCBA+3000h's
 * D20IP and D20IR, the page unmapped. io->Own and io->Write16 are never
 * called. Returns the decision; st->Gsi holds the GSI when OFFERED.
 */
ULONG XhciPirqOffer(const XHCI_PIRQ_IO *io, ULONG bus, ULONG address,
                    ULONG ownId, ULONG ownSubsystem, ULONG mode,
                    PXHCI_PIRQ_OFFER st);

/* 1 for an IRQ PIRQx_ROUT's field can name: 3-7, 9-12, 14 and 15. */
ULONG XhciPirqPicIrq(ULONG irq);

/*
 * The PIRQ that delivers raw IRQ irq: in APIC mode (16-23) irq - 16; in
 * PIC mode the PIRQ whose PIRQx_ROUT is routed and names irq - the current
 * IAR when it is one of them, else the lowest. Sets *irqMode. Returns
 * XHCI_PIRQ_D_NONE with *target set, or XHCI_PIRQ_D_IRQ_UNROUTABLE or
 * XHCI_PIRQ_D_PIC_NO_PIRQ.
 */
ULONG XhciPirqTarget(ULONG irq, ULONG pirqAd, ULONG pirqEh, ULONG current,
                     PULONG irqMode, PULONG target);

/* The start of every start attempt, before anything can refuse it: the
 * value and the last run's fields cleared, Mode XHCI_PIRQ_UNREAD until the
 * latch, the counts kept. A start refused before the routing step then
 * leaves no earlier start's run in the record. */
VOID XhciPirqReset(PXHCI_PIRQ_STATE st);

/* The start: the value's meaning latched, the last run's fields cleared,
 * the counts kept. */
VOID XhciPirqLatch(PXHCI_PIRQ_STATE st, ULONG found, ULONG value);

/* One run, at a start or a return to D0. Returns st->Decision. */
ULONG XhciPirqRun(const XHCI_PIRQ_IO *io, ULONG when, ULONG delivery,
                  const XHCI_PIRQ_IRQ *irq, PXHCI_PIRQ_STATE st);

#endif /* XHCI_PIRQ_H */
