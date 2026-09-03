/*
 * xhci.h - xHCI types and the controller common-buffer layout.
 *
 * Bit positions and structure sizes come from docs/usb-xhci-info/xhci-data-structures.md
 * (transcribed and verified against the local spec PDF) - never from memory.
 * The common-buffer model and its constants are derived in
 * docs/contributing/design/04-controller-common-buffer.md.
 *
 * This file grows through Phases 3-4. It now carries the fixed common-buffer
 * layout (Phase 3 task 2), the hardware field definitions the pure core needs,
 * and the declarations of that core: the TRB/ring layer (src/xhci_ring.c), the
 * extended-capability parser and port classifier (src/xhci_caps.c), and the
 * PORTSC write builder (src/xhci_port.c). Everything declared here is pure
 * computation on caller-supplied memory - no MMIO, no DDK, no IRQL - so
 * test/ can exercise it on the build host (docs/contributing/design/03-host-unit-tests.md).
 *
 * C89 only. No bitfields, no enums for hardware layouts.
 */

#ifndef XHCI_H
#define XHCI_H

#include "xhci_compat.h"
/* The miniport extension carries the task 6-V.1 probe's state, so its types
 * have to be visible here. That header is DDK-free for the same reason this one
 * is, and it declares no function this file needs. */
#include "xhci_probe.h"
/* And the task 7b-A.1 hub topology graph, on the same terms. */
#include "xhci_topo.h"
/* ...and the task 9-A.2 configuration-descriptor snoop: every device record
 * carries the isochronous `bInterval` table it fills. */
#include "xhci_desc.h"
/* ...and the task 11-V.7 log ring, which lives in the extension for the same
 * reason everything else here does: this driver allocates no pool. */
#include "xhci_log.h"

/* ------------------------------------------------------------------ */
/* Hardware structures referenced by the layout                        */
/* ------------------------------------------------------------------ */

typedef struct _XHCI_TRB {
    ULONG Param0;   /* DW0 - parameter lo / immediate data     */
    ULONG Param1;   /* DW1 - parameter hi (always 0 for us)    */
    ULONG Status;   /* DW2 - length / interrupter / completion */
    ULONG Control;  /* DW3 - cycle, type, per-type flags       */
} XHCI_TRB, *PXHCI_TRB;

XHCI_C_ASSERT(trb_size, sizeof(XHCI_TRB) == 16);

/* Event Ring Segment Table entry (spec 6.5) */
typedef struct _XHCI_ERST_ENTRY {
    ULONG BaseLo;   /* DW0 - segment base, 64-byte aligned */
    ULONG BaseHi;   /* DW1 - always 0 (32-bit driver)      */
    ULONG Size;     /* DW2 - segment size in TRBs, bits 15:0 */
    ULONG Rsvd;     /* DW3 - RsvdZ                          */
} XHCI_ERST_ENTRY, *PXHCI_ERST_ENTRY;

XHCI_C_ASSERT(erst_entry_size, sizeof(XHCI_ERST_ENTRY) == 16);

/* ------------------------------------------------------------------ */
/* TRB fields and type codes (docs/usb-xhci-info/xhci-data-structures.md section 7)  */
/* ------------------------------------------------------------------ */

/* DW3 bits shared by every TRB. */
#define XHCI_TRB_CYCLE          0x00000001UL        /* bit 0            */
#define XHCI_TRB_TYPE_SHIFT     10
#define XHCI_TRB_TYPE_MASK      0x0000FC00UL        /* bits 15:10       */
#define XHCI_TRB_TYPE(t)        ((((ULONG)(t)) & 0x3FUL) << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_GET_TYPE(dw3)  ((((ULONG)(dw3)) >> XHCI_TRB_TYPE_SHIFT) & 0x3FUL)

/* DW3 bits by position. TC and ENT share bit 1: TC is the Link TRB's Toggle
 * Cycle, ENT is a transfer TRB's Evaluate Next TRB. Never one macro for both. */
#define XHCI_TRB_ENT            0x00000002UL        /* transfer TRBs    */
#define XHCI_TRB_LINK_TC        0x00000002UL        /* Link TRB only    */
#define XHCI_TRB_ISP            0x00000004UL
#define XHCI_TRB_NS             0x00000008UL
#define XHCI_TRB_CH             0x00000010UL
#define XHCI_TRB_IOC            0x00000020UL
#define XHCI_TRB_IDT            0x00000040UL
#define XHCI_TRB_BEI            0x00000200UL
#define XHCI_TRB_BSR            0x00000200UL        /* Address Device   */
/* Deconfigure, Configure Endpoint only (section 5's TRB table: DC `9`). Same
 * bit as BSR and BEI, and named separately for the reason stated above - three
 * commands using one macro is how a Deconfigure gets issued as a Block Set
 * Address Request. */
#define XHCI_TRB_DC             0x00000200UL        /* Configure Endpoint */
/* Transfer State Preserve, Reset Endpoint only (section 5's TRB table: TSP `9`).
 * The fourth command to use bit 9, and named separately for the same reason the
 * three above are: TSP = 1 is the Soft Retry of spec 4.6.8.1, which preserves the
 * Data Toggle, and this driver always wants the opposite. */
#define XHCI_TRB_TSP            0x00000200UL        /* Reset Endpoint     */
/* Suspend, Stop Endpoint only (section 5's TRB table: SP `23`). Its own bit
 * rather than a shared one, and this driver always writes it as 0 - a suspend
 * stop is for a controller about to enter a low-power state, which is Phase 11's
 * (it was Phase 8's before the phase split). */
#define XHCI_TRB_SP             0x00800000UL        /* Stop Endpoint      */
#define XHCI_TRB_DIR_IN         0x00010000UL

#define XHCI_TRB_SLOT_ID(s)     ((((ULONG)(s)) & 0xFFUL) << 24)
#define XHCI_TRB_GET_SLOT_ID(dw3) ((((ULONG)(dw3)) >> 24) & 0xFFUL)
#define XHCI_TRB_EP_ID(dci)     ((((ULONG)(dci)) & 0x1FUL) << 16)
#define XHCI_TRB_GET_EP_ID(dw3) ((((ULONG)(dw3)) >> 16) & 0x1FUL)
#define XHCI_TRB_SLOT_TYPE(t)   ((((ULONG)(t)) & 0x1FUL) << 16)

/*
 * Event Data (ED), bit 2 of a Transfer Event's DW3 (spec Table 6-39). Same bit
 * position as a transfer TRB's ISP, so it gets its own name for the same
 * reason TC and ENT do. When it is set, DW0/1 is *not* a ring address: it is
 * "64 bits of Event Data" the Event Data TRB carried (Table 6-37). This driver
 * places no Event Data TRBs, so an event with ED = 1 is unexpected; log it and
 * discard it, never resolve its parameter as a TRB address.
 */
#define XHCI_TRB_ED             0x00000004UL
#define XHCI_EVENT_IS_EVENT_DATA(dw3) ((((ULONG)(dw3)) & XHCI_TRB_ED) ? 1 : 0)

/*
 * DW2 of a *transfer* TRB (Normal, Setup Stage, Data Stage, Isoch). The length
 * field is 17 bits, so it holds 0 to 65536 inclusive - the one place in this
 * driver where a byte count and its mask are not the same width as the value's
 * maximum. TD Size is 5 bits and is a *packet* count, not a byte count
 * (spec 4.11.2.4).
 */
#define XHCI_TRB_LENGTH_MASK    0x0001FFFFUL        /* bits 16:0        */
#define XHCI_TRB_LENGTH_MAX     0x00010000UL        /* 64 KB, inclusive */
#define XHCI_TRB_GET_LENGTH(dw2) (((ULONG)(dw2)) & XHCI_TRB_LENGTH_MASK)
#define XHCI_TRB_TD_SIZE(n)     ((((ULONG)(n)) & 0x1FUL) << 17)
#define XHCI_TRB_GET_TD_SIZE(dw2) ((((ULONG)(dw2)) >> 17) & 0x1FUL)
#define XHCI_TRB_TD_SIZE_MAX    31
#define XHCI_TRB_INTERRUPTER(n) ((((ULONG)(n)) & 0x3FFUL) << 22)

/*
 * DW3 of an **Isoch** TRB (task 9-A.1; spec 6.4.1.3 Table 6-34, p.437).
 *
 * `TBC` is at DW3 `8:7` and **not** at DW2 `21:17`: the DW2 field is TD Size
 * while Extended TBC is disabled, which is this driver's case and the only one
 * it can be in - ETE cannot be enabled without the Large ESIT Payload
 * Capability, and nothing here writes USBCMD's enable. Naming both would be one
 * macro too many; the DW2 field keeps its TD Size name for every transfer TRB.
 *
 * Frame ID is eleven bits wide because it is compared against the eleven-bit
 * Frame Index of MFINDEX (bits `13:3`), so 2048 is the modulus of every piece of
 * arithmetic that produces one.
 */
#define XHCI_TRB_TBC_SHIFT      7
#define XHCI_TRB_TBC(n)         ((((ULONG)(n)) & 0x3UL) << XHCI_TRB_TBC_SHIFT)
#define XHCI_TRB_TBC_MAX        3
#define XHCI_TRB_TLBPC_SHIFT    16
#define XHCI_TRB_TLBPC(n)       ((((ULONG)(n)) & 0xFUL) << XHCI_TRB_TLBPC_SHIFT)
#define XHCI_TRB_TLBPC_MAX      15
#define XHCI_TRB_FRAME_ID_SHIFT 20
#define XHCI_TRB_FRAME_ID(n) \
    ((((ULONG)(n)) & XHCI_FRAME_ID_MASK) << XHCI_TRB_FRAME_ID_SHIFT)
#define XHCI_TRB_GET_FRAME_ID(dw3) \
    ((((ULONG)(dw3)) >> XHCI_TRB_FRAME_ID_SHIFT) & XHCI_FRAME_ID_MASK)
#define XHCI_TRB_SIA            0x80000000UL

/*
 * The Frame ID space, which is MFINDEX's Frame Index rather than a number of
 * this driver's choosing: "The Frame ID value is calculated as the modulus of
 * 2048, i.e. the size of the Frame Index portion of the MFINDEX register"
 * (4.11.2.5 p.199).
 *
 * The window bounds are the spec's own (4.11.2.5 p.199): a TD may not carry a
 * Frame ID past `current + 895` frames, and should not carry one before
 * `current + IST + 1`. Both are stated as arithmetic mod 2048, which is why
 * every comparison against them is a *distance* rather than a magnitude - two
 * Frame IDs either side of the wrap are not orderable by value.
 */
#define XHCI_FRAME_ID_MASK      0x000007FFUL
#define XHCI_FRAME_ID_MODULUS   0x00000800UL
#define XHCI_FRAME_ID_WINDOW_END 895UL

/* DW3 of a Setup Stage TRB: Transfer Type, bits 17:16 (Table 6-26). */
#define XHCI_TRB_TRT_SHIFT      16
#define XHCI_TRB_TRT(t)         ((((ULONG)(t)) & 0x3UL) << XHCI_TRB_TRT_SHIFT)
#define XHCI_TRB_TRT_NO_DATA    0
#define XHCI_TRB_TRT_OUT_DATA   2
#define XHCI_TRB_TRT_IN_DATA    3

/* DW2 of an event TRB. */
#define XHCI_TRB_GET_COMPLETION(dw2) ((((ULONG)(dw2)) >> 24) & 0xFFUL)
#define XHCI_TRB_GET_RESIDUAL(dw2)   (((ULONG)(dw2)) & 0x00FFFFFFUL)
/* DW0 of a Port Status Change Event: Port ID 31:24, 1-based. */
#define XHCI_TRB_GET_PORT_ID(dw0)    ((((ULONG)(dw0)) >> 24) & 0xFFUL)

/* Type codes (spec Table 6-91). Only the ones this driver produces or
 * consumes; the rest are looked up in the doc when one appears in a log. */
#define XHCI_TRB_TYPE_NORMAL            1
#define XHCI_TRB_TYPE_SETUP_STAGE       2
#define XHCI_TRB_TYPE_DATA_STAGE        3
#define XHCI_TRB_TYPE_STATUS_STAGE      4
#define XHCI_TRB_TYPE_ISOCH             5
#define XHCI_TRB_TYPE_LINK              6
#define XHCI_TRB_TYPE_EVENT_DATA        7
#define XHCI_TRB_TYPE_NOOP              8
#define XHCI_TRB_TYPE_ENABLE_SLOT       9
#define XHCI_TRB_TYPE_DISABLE_SLOT      10
#define XHCI_TRB_TYPE_ADDRESS_DEVICE    11
#define XHCI_TRB_TYPE_CONFIGURE_EP      12
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT  13
#define XHCI_TRB_TYPE_RESET_EP          14
#define XHCI_TRB_TYPE_STOP_EP           15
#define XHCI_TRB_TYPE_SET_TR_DEQUEUE    16
#define XHCI_TRB_TYPE_RESET_DEVICE      17
#define XHCI_TRB_TYPE_NOOP_COMMAND      23
/* The architected command range of Table 6-91, which is what the command ring
 * may carry (a Link TRB is placed by the ring layer, not by a submitter, and
 * Vendor Defined commands 48-63 are deliberately not accepted - this driver
 * issues none, and a vendor type ID means nothing unqualified by PCI
 * Vendor/Device). */
#define XHCI_TRB_TYPE_COMMAND_FIRST     XHCI_TRB_TYPE_ENABLE_SLOT
#define XHCI_TRB_TYPE_COMMAND_LAST      XHCI_TRB_TYPE_NOOP_COMMAND
#define XHCI_TRB_TYPE_TRANSFER_EVENT    32
#define XHCI_TRB_TYPE_COMMAND_COMPLETION 33
#define XHCI_TRB_TYPE_PORT_STATUS_CHANGE 34
#define XHCI_TRB_TYPE_BANDWIDTH_REQUEST 35
#define XHCI_TRB_TYPE_DOORBELL          36
#define XHCI_TRB_TYPE_HOST_CONTROLLER   37
#define XHCI_TRB_TYPE_DEVICE_NOTIFICATION 38
#define XHCI_TRB_TYPE_MFINDEX_WRAP      39

/*
 * The *architected* event TRB types occupy one contiguous range of Table 6-91,
 * which is what lets the interrupt path keep a per-type counter as a plain
 * array (XHCI_EXTENSION.EventCounts) instead of a lookup table.
 */
#define XHCI_EVENT_TYPE_FIRST    XHCI_TRB_TYPE_TRANSFER_EVENT
#define XHCI_EVENT_TYPE_LAST     XHCI_TRB_TYPE_MFINDEX_WRAP
#define XHCI_EVENT_TYPE_COUNT    (XHCI_EVENT_TYPE_LAST - XHCI_EVENT_TYPE_FIRST + 1)
#define XHCI_EVENT_TYPE_INDEX(t) (((ULONG)(t)) - XHCI_EVENT_TYPE_FIRST)

XHCI_C_ASSERT(event_type_range_is_eight_wide, XHCI_EVENT_TYPE_COUNT == 8);

/*
 * But that range is not all of what may legally arrive. Table 6-91 reserves
 * 40-47 and assigns **48-63 to Vendor Defined** TRB types, marked Optional in
 * its Event Ring column - so a vendor event is legal there and 4.11.6 (p.212)
 * says exactly what to do with one: "Software shall advance past and ignore
 * Vendor Defined TRBs encountered on an Event Ring."
 *
 * Advancing past and ignoring is the same code path as discarding a malformed
 * TRB, so the distinction is one of *diagnosis*, and it matters: a vendor event
 * says the controller did something this driver has no vendor knowledge of,
 * while a reserved or transfer-ring type on an event ring says either the
 * controller or this driver's idea of where the event ring lives is wrong.
 * Filing the first under the second would send a future reader hunting a
 * corruption that never happened. This project has a live example - NEC/Renesas
 * controllers carry a vendor-defined command for querying firmware version
 * (Linux xhci-pci.c, XHCI_NEC_HOST) - though this driver issues no vendor command, so
 * it should never see the completion.
 *
 * A vendor type ID means nothing on its own: "System software shall qualify all
 * Vendor Defined TRB type IDs with the Vendor ID and Device ID fields in the
 * PCI Configuration Space Header" (Table 6-91 note). XHCI_EXTENSION.
 * PciVendorDevice is that qualifier, already recorded at init.
 */
#define XHCI_TRB_TYPE_VENDOR_FIRST  48
#define XHCI_TRB_TYPE_VENDOR_LAST   63

/* Completion codes (spec Table 6-90). */
#define XHCI_CC_INVALID                 0
#define XHCI_CC_SUCCESS                 1
#define XHCI_CC_DATA_BUFFER_ERROR       2
#define XHCI_CC_BABBLE                  3
#define XHCI_CC_USB_TRANSACTION_ERROR   4
#define XHCI_CC_TRB_ERROR               5
#define XHCI_CC_STALL                   6
#define XHCI_CC_RESOURCE_ERROR          7
#define XHCI_CC_BANDWIDTH_ERROR         8
#define XHCI_CC_NO_SLOTS                9
#define XHCI_CC_INVALID_STREAM_TYPE     10
#define XHCI_CC_SLOT_NOT_ENABLED        11
#define XHCI_CC_EP_NOT_ENABLED          12
#define XHCI_CC_SHORT_PACKET            13
#define XHCI_CC_RING_UNDERRUN           14
#define XHCI_CC_RING_OVERRUN            15
#define XHCI_CC_VF_EVENT_RING_FULL      16
#define XHCI_CC_PARAMETER_ERROR         17
#define XHCI_CC_BANDWIDTH_OVERRUN       18
#define XHCI_CC_CONTEXT_STATE_ERROR     19
#define XHCI_CC_NO_PING_RESPONSE        20
#define XHCI_CC_EVENT_RING_FULL         21
#define XHCI_CC_INCOMPATIBLE_DEVICE     22
#define XHCI_CC_MISSED_SERVICE          23
#define XHCI_CC_COMMAND_RING_STOPPED    24
#define XHCI_CC_COMMAND_ABORTED         25
#define XHCI_CC_STOPPED                 26
#define XHCI_CC_STOPPED_LENGTH_INVALID  27
#define XHCI_CC_STOPPED_SHORT_PACKET    28
#define XHCI_CC_MAX_EXIT_LATENCY        29
#define XHCI_CC_ISOCH_BUFFER_OVERRUN    31
#define XHCI_CC_EVENT_LOST              32
#define XHCI_CC_UNDEFINED_ERROR         33
#define XHCI_CC_INVALID_STREAM_ID       34
#define XHCI_CC_SECONDARY_BANDWIDTH     35
#define XHCI_CC_SPLIT_TRANSACTION       36
#define XHCI_CC_VENDOR_ERROR_MIN        192
#define XHCI_CC_VENDOR_ERROR_MAX        223
#define XHCI_CC_VENDOR_INFO_MIN         224
#define XHCI_CC_VENDOR_INFO_MAX         255

/* ------------------------------------------------------------------ */
/* Register map (docs/usb-xhci-info/xhci-data-structures.md sections 2-5)            */
/* ------------------------------------------------------------------ */

/*
 * Byte offsets only - no accessors. The MMIO side lives in src/xhci_pci.c,
 * which is the only file that dereferences the BAR; keeping the offsets here
 * lets the pure derivation below (and its host tests) reason about the window
 * without either one growing a DDK dependency.
 *
 * Three bases are derived rather than fixed: the operational registers start
 * at BAR0 + CAPLENGTH, the runtime registers at BAR0 + RTSOFF, and the
 * doorbells at BAR0 + DBOFF. XhciDeriveHcInfo computes and bounds-checks all
 * three.
 */

/* Capability registers, BAR0 + 0 (spec 5.3). */
#define XHCI_CAP_CAPLENGTH      0x00UL      /* byte; HCIVERSION shares DW0 */
#define XHCI_CAP_HCSPARAMS1     0x04UL
#define XHCI_CAP_HCSPARAMS2     0x08UL
#define XHCI_CAP_HCSPARAMS3     0x0CUL
#define XHCI_CAP_HCCPARAMS1     0x10UL
#define XHCI_CAP_DBOFF          0x14UL
#define XHCI_CAP_RTSOFF         0x18UL
/*
 * HCCPARAMS2 (5.3.9, p.355). Unlike every register above it, this one is not
 * guaranteed to be *reachable*: it sits at 0x1C, so a controller whose CAPLENGTH
 * stops there has no room for it, which is a legal thing for one to do. It is
 * therefore not part of XHCI_CAP_REGISTERS_BYTES - raising the mandatory block
 * to 0x20 would refuse such a controller outright - and is read conditionally
 * instead. See XhciDeriveHcInfo for the gate.
 *
 * **It is not a 1.1 register**, and the first version of this comment said it
 * was. Appendix H.1 lists the capabilities "that were optional for xHCI 1.0
 * implementations [and] are now required in xHCI 1.1 implementations", and H.1.6
 * is FSC (p.593) - as are U3C, CTC and CIC, which are three more HCCPARAMS2
 * bits. So the register is defined at 1.0 and a 1.0 controller may legitimately
 * advertise FSC in it. Gating the read on HCIVERSION would not be caution, it
 * would be discarding a discovery bit the specification provides.
 */
#define XHCI_CAP_HCCPARAMS2     0x1CUL
/* Through RTSOFF: the last capability register every controller this driver
 * accepts is required to have, and so the smallest window and the smallest
 * CAPLENGTH that can be correct. */
#define XHCI_CAP_REGISTERS_BYTES 0x1CUL
/* What has to be mapped, and what CAPLENGTH has to reach, before the optional
 * register above may be read at all. */
#define XHCI_CAP_HCCPARAMS2_BYTES 0x20UL

#define XHCI_CAPLENGTH_OF(dw0)  (((ULONG)(dw0)) & 0xFFUL)
#define XHCI_HCIVERSION_OF(dw0) ((((ULONG)(dw0)) >> 16) & 0xFFFFUL)
#define XHCI_HCIVERSION_1_0     0x0100UL
#define XHCI_HCIVERSION_1_1     0x0110UL

/*
 * Force Save Context Capability, HCCPARAMS2 bit 2 (Table 5-16, p.355). It
 * decides whether a Save State operation is usable at all by a driver that
 * cannot issue Stop Endpoint on the suspend path: "If the Force Save Context
 * Capability (FSC = '0') is not supported, then Stop Endpoint Commands shall be
 * issued for all Idle endpoints in the Running state as well" (4.23.2, p.313).
 *
 * "Force Save Context Capability support (i.e. FSC = '1') shall be mandatory for
 * all xHCI 1.1 and xHCI 1.2 compliant xHCs" (same page). **Mandatory from 1.1 is
 * not the same as absent before it** - Appendix H.1.6 (p.593) lists FSC among
 * the capabilities "that were optional for xHCI 1.0 implementations [and] are
 * now required in xHCI 1.1" - so a 1.0 controller answers this bit honestly and
 * may well answer 1. Which way the qualified fleet answers is **not measured**:
 * XHCIQUAL prints HCCPARAMS1 and not HCCPARAMS2, so "the fleet's Intel parts have
 * FSC = 0" would be an inference from their HCIVERSION, and an inference this
 * driver has no need to make now that it reads the bit.
 */
#define XHCI_HCCPARAMS2_FSC(v)  ((((ULONG)(v)) >> 2) & 0x1UL)

/* Operational registers, BAR0 + CAPLENGTH (spec 5.4). */
#define XHCI_OP_USBCMD          0x00UL
#define XHCI_OP_USBSTS          0x04UL
#define XHCI_OP_PAGESIZE        0x08UL
#define XHCI_OP_DNCTRL          0x14UL
#define XHCI_OP_CRCR            0x18UL      /* 64-bit: +0x18 lo, +0x1C hi   */
#define XHCI_OP_DCBAAP          0x30UL      /* 64-bit: +0x30 lo, +0x34 hi   */
#define XHCI_OP_CONFIG          0x38UL
#define XHCI_OP_REGISTERS_BYTES 0x3CUL      /* through CONFIG               */
#define XHCI_OP_PORTSC_BASE     0x400UL
#define XHCI_OP_PORT_STRIDE     0x10UL
/* PORTSC of port n (1-based), as an offset from the operational base. */
#define XHCI_OP_PORTSC(n) \
    (XHCI_OP_PORTSC_BASE + ((((ULONG)(n)) - 1UL) * XHCI_OP_PORT_STRIDE))

#define XHCI_USBCMD_RS          0x00000001UL
#define XHCI_USBCMD_HCRST       0x00000002UL
#define XHCI_USBCMD_INTE        0x00000004UL
#define XHCI_USBCMD_HSEE        0x00000008UL
#define XHCI_USBCMD_LHCRST      0x00000080UL
/*
 * Controller Save/Restore State (task 6-B.6). Both are strobes rather than
 * state: "This flag always returns 0 when read" (Table 5-20, p.361), so neither
 * may be polled for completion and neither survives a read-modify-write of
 * USBCMD - which is exactly what xhciWriteUsbCmd performs. The completion
 * signal is USBSTS.SSS/RSS and the verdict is USBSTS.SRE
 * (docs/usb-xhci-info/xhci-data-structures.md section 3, "Controller Save/Restore State").
 */
#define XHCI_USBCMD_CSS         0x00000100UL
#define XHCI_USBCMD_CRS         0x00000200UL
#define XHCI_USBCMD_EWE         0x00000400UL

/*
 * USBCMD is not a register that can be written as a literal. Bits 6:4, 12 and
 * 31:17 are **RsvdP** (spec Figure 5-14 and Table 5-20, p.359-361), and RsvdP
 * means "Reserved and Preserved: Reserved for future RW implementations.
 * Software shall preserve the value read for writes to bits" (5.1.1, p.338) -
 * so every write has to carry back what the read reported there, whatever this
 * driver believes the current defaults are.
 *
 * DEFINED is the complement: every bit the specification assigns a name at
 * version 1.2 (R/S, HCRST, INTE, HSEE, LHCRST, CSS, CRS, EWE, EU3S, CME, ETE,
 * TSC_EN, VTIOE). A caller names the ones it wants set; everything else defined
 * is cleared, and everything reserved is preserved. The assertion below is what
 * keeps the two halves from drifting apart if a bit is ever added here.
 */
#define XHCI_USBCMD_RSVDP_MASK   0xFFFE1070UL
#define XHCI_USBCMD_DEFINED_MASK 0x0001EF8FUL

XHCI_C_ASSERT(usbcmd_masks_partition_the_register,
              (XHCI_USBCMD_RSVDP_MASK & XHCI_USBCMD_DEFINED_MASK) == 0 &&
              (XHCI_USBCMD_RSVDP_MASK | XHCI_USBCMD_DEFINED_MASK) ==
                  0xFFFFFFFFUL);
XHCI_C_ASSERT(usbcmd_named_bits_are_defined,
              ((XHCI_USBCMD_RS | XHCI_USBCMD_HCRST | XHCI_USBCMD_INTE |
                XHCI_USBCMD_HSEE | XHCI_USBCMD_LHCRST | XHCI_USBCMD_CSS |
                XHCI_USBCMD_CRS | XHCI_USBCMD_EWE) &
               ~XHCI_USBCMD_DEFINED_MASK) == 0);

#define XHCI_USBSTS_HCH         0x00000001UL    /* RO   */
#define XHCI_USBSTS_HSE         0x00000004UL    /* RW1C */
#define XHCI_USBSTS_EINT        0x00000008UL    /* RW1C */
#define XHCI_USBSTS_PCD         0x00000010UL    /* RW1C */
#define XHCI_USBSTS_SSS         0x00000100UL    /* RO   */
#define XHCI_USBSTS_RSS         0x00000200UL    /* RO   */
#define XHCI_USBSTS_SRE         0x00000400UL    /* RW1C */
#define XHCI_USBSTS_CNR         0x00000800UL    /* RO   */
#define XHCI_USBSTS_HCE         0x00001000UL    /* RO   */

/*
 * Every RW1C bit in USBSTS. USBSTS must never be written with a read-modify-
 * write that ORs the read value in: that acknowledges every change bit that
 * happened to be set, including ones no code has looked at yet
 * (docs/usb-xhci-info/xhci-data-structures.md section 3). Write exactly the bits to clear.
 */
#define XHCI_USBSTS_RW1C_MASK \
    (XHCI_USBSTS_HSE | XHCI_USBSTS_EINT | XHCI_USBSTS_PCD | XHCI_USBSTS_SRE)

/*
 * DNCTRL: Notification Enable N0-N15 in 15:0, **31:16 RsvdP** (Table 5-23,
 * p.366). This driver enables no notification type, so the defined half is
 * always written as zero - but a literal zero write clears the reserved half
 * too, which is the violation RsvdP exists to prevent.
 */
#define XHCI_DNCTRL_DEFINED_MASK 0x0000FFFFUL
#define XHCI_DNCTRL_RSVDP_MASK   0xFFFF0000UL

#define XHCI_CRCR_RCS           0x00000001UL
#define XHCI_CRCR_CS            0x00000002UL    /* RW1S */
#define XHCI_CRCR_CA            0x00000004UL    /* RW1S */
#define XHCI_CRCR_CRR           0x00000008UL    /* RO   */

/*
 * CRCR's low DWORD: RCS/CS/CA in 2:0, CRR (RO) in bit 3, **5:4 RsvdP**, and the
 * Command Ring Pointer in 31:6 (Table 5-24, p.367-368).
 *
 * Every one of RCS, CS, CA and the pointer "always returns '0'" when read, which
 * is what made the old literal writes look free of consequence - the enumeration
 * simply never reached bits 5:4, and those are the only bits in the register a
 * read can carry anything in. CRR is excluded from the defined mask because it
 * is read-only: naming it would put a bit into a write that has no business
 * being there.
 */
#define XHCI_CRCR_DEFINED_MASK  0x00000007UL
#define XHCI_CRCR_RSVDP_MASK    0x00000030UL
#define XHCI_CRCR_PTR_MASK      0xFFFFFFC0UL

XHCI_C_ASSERT(crcr_masks_do_not_overlap,
              (XHCI_CRCR_DEFINED_MASK & XHCI_CRCR_RSVDP_MASK) == 0 &&
              (XHCI_CRCR_DEFINED_MASK & XHCI_CRCR_PTR_MASK) == 0 &&
              (XHCI_CRCR_RSVDP_MASK & XHCI_CRCR_PTR_MASK) == 0);

/*
 * CONFIG: MaxSlotsEn 7:0, U3E bit 8, CIE bit 9, **31:10 RsvdP** (Table 5-26,
 * p.369-370). This driver sets neither U3E nor CIE, so the defined half is
 * MaxSlotsEn alone - but the write still has to carry 31:10 back.
 *
 * Revision 1.2c defined bit 10 as Software Offload Capable (SOC, RW), which
 * revision 1.2 had as RsvdP. It stays inside XHCI_CONFIG_RSVDP_MASK on
 * purpose: the mask names what a write carries back unchanged, and a bit this
 * driver never sets belongs there whether the spec calls it reserved or a
 * feature it does not use. Moving it to the defined half would only turn a
 * preserved value into a written 0.
 */
#define XHCI_CONFIG_MAXSLOTSEN_MASK 0x000000FFUL
#define XHCI_CONFIG_U3E             0x00000100UL
#define XHCI_CONFIG_CIE             0x00000200UL
#define XHCI_CONFIG_DEFINED_MASK    0x000003FFUL
#define XHCI_CONFIG_RSVDP_MASK      0xFFFFFC00UL

XHCI_C_ASSERT(config_masks_partition_the_register,
              (XHCI_CONFIG_RSVDP_MASK & XHCI_CONFIG_DEFINED_MASK) == 0 &&
              (XHCI_CONFIG_RSVDP_MASK | XHCI_CONFIG_DEFINED_MASK) ==
                  0xFFFFFFFFUL);
XHCI_C_ASSERT(config_maxslotsen_is_defined,
              (XHCI_CONFIG_MAXSLOTSEN_MASK & ~XHCI_CONFIG_DEFINED_MASK) == 0);

/* Runtime registers, BAR0 + RTSOFF (spec 5.5). */
#define XHCI_RT_MFINDEX         0x00UL
/*
 * MFINDEX is a 14-bit **microframe** counter (5.5.1, Table 5-33): bits 13:0
 * count 125 us intervals and wrap every 2,048 frames. usbport's frame number is
 * a millisecond frame count, so the conversion is a shift of three and the
 * wrap period is 2,048 - both named here because a hardcoded 0x3FFF used as a
 * frame mask is off by a factor of eight and looks plausible in a log.
 */
#define XHCI_MFINDEX_MASK       0x00003FFFUL
#define XHCI_MFINDEX_FRAME_SHIFT 3
#define XHCI_MFINDEX_FRAME_MASK 0x000007FFUL
#define XHCI_MFINDEX_FRAMES     0x00000800UL
#define XHCI_RT_IR0             0x20UL
#define XHCI_RT_IR_STRIDE       0x20UL
/* Interrupter 0 is the only one this driver uses, so the runtime window it
 * needs is MFINDEX plus one interrupter. */
#define XHCI_RT_REGISTERS_BYTES (XHCI_RT_IR0 + XHCI_RT_IR_STRIDE)

/* Offsets within one interrupter. */
#define XHCI_IR_IMAN            0x00UL
#define XHCI_IR_IMOD            0x04UL
#define XHCI_IR_ERSTSZ          0x08UL
#define XHCI_IR_ERSTBA          0x10UL      /* 64-bit */
#define XHCI_IR_ERDP            0x18UL      /* 64-bit */

/*
 * ERSTSZ: table size in 15:0, **31:16 RsvdP** (Table 5-40, p.393).
 * ERSTBA's low DWORD: **5:0 RsvdP**, base address in 31:6 (Table 5-41, p.394).
 *
 * IMOD and ERDP are deliberately absent from this block, and their absence is a
 * reading rather than an omission: IMOD is IMODI 15:0 and IMODC 31:16, both RW
 * (Table 5-39, p.392), and ERDP is DESI 2:0, EHB bit 3 and the pointer 63:4
 * (Table 5-42, p.394). Neither register has a reserved field to preserve.
 */
#define XHCI_ERSTSZ_DEFINED_MASK 0x0000FFFFUL
#define XHCI_ERSTSZ_RSVDP_MASK   0xFFFF0000UL
#define XHCI_ERSTBA_RSVDP_MASK   0x0000003FUL
#define XHCI_ERSTBA_ADDR_MASK    0xFFFFFFC0UL

XHCI_C_ASSERT(erstsz_masks_partition_the_register,
              (XHCI_ERSTSZ_RSVDP_MASK & XHCI_ERSTSZ_DEFINED_MASK) == 0 &&
              (XHCI_ERSTSZ_RSVDP_MASK | XHCI_ERSTSZ_DEFINED_MASK) ==
                  0xFFFFFFFFUL);
XHCI_C_ASSERT(erstba_masks_partition_the_low_dword,
              (XHCI_ERSTBA_RSVDP_MASK & XHCI_ERSTBA_ADDR_MASK) == 0 &&
              (XHCI_ERSTBA_RSVDP_MASK | XHCI_ERSTBA_ADDR_MASK) == 0xFFFFFFFFUL);
XHCI_C_ASSERT(dnctrl_masks_partition_the_register,
              (XHCI_DNCTRL_RSVDP_MASK & XHCI_DNCTRL_DEFINED_MASK) == 0 &&
              (XHCI_DNCTRL_RSVDP_MASK | XHCI_DNCTRL_DEFINED_MASK) ==
                  0xFFFFFFFFUL);

#define XHCI_IMAN_IP            0x00000001UL    /* RW1C */
#define XHCI_IMAN_IE            0x00000002UL
/* Bits 31:2 are RsvdP: "software shall preserve the value read for writes to
 * bits" (5.1.1). Every IMAN write in this driver is therefore a
 * read-modify-write; the one documented exception is XhciIsr's fallback when no
 * valid operand can be read at all. */
#define XHCI_IMAN_DEFINED_MASK  0x00000003UL
#define XHCI_IMAN_RSVDP_MASK    0xFFFFFFFCUL

/* Doorbells, BAR0 + DBOFF (spec 5.6): one DWORD per slot, DB[0] = command. */
#define XHCI_DB_STRIDE          4UL
#define XHCI_DB_COMMAND         0UL

/* ------------------------------------------------------------------ */
/* PORTSC (docs/usb-xhci-info/xhci-data-structures.md section 3, spec Table 5-27)    */
/* ------------------------------------------------------------------ */

#define XHCI_PORTSC_CCS         0x00000001UL    /* bit 0  RO            */
#define XHCI_PORTSC_PED         0x00000002UL    /* bit 1  RW1C - clears */
#define XHCI_PORTSC_OCA         0x00000008UL    /* bit 3  RO            */
#define XHCI_PORTSC_PR          0x00000010UL    /* bit 4  RW1S          */
#define XHCI_PORTSC_PLS_SHIFT   5
#define XHCI_PORTSC_PLS_MASK    0x000001E0UL    /* bits 8:5             */
#define XHCI_PORTSC_PP          0x00000200UL    /* bit 9  RW            */
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_SPEED_MASK  0x00003C00UL    /* bits 13:10 RO        */
#define XHCI_PORTSC_PIC_MASK    0x0000C000UL    /* bits 15:14 RW        */
#define XHCI_PORTSC_LWS         0x00010000UL    /* bit 16 RW strobe     */
#define XHCI_PORTSC_CSC         0x00020000UL    /* bit 17 RW1C          */
#define XHCI_PORTSC_PEC         0x00040000UL    /* bit 18 RW1C          */
#define XHCI_PORTSC_WRC         0x00080000UL    /* bit 19 RW1C          */
#define XHCI_PORTSC_OCC         0x00100000UL    /* bit 20 RW1C          */
#define XHCI_PORTSC_PRC         0x00200000UL    /* bit 21 RW1C          */
#define XHCI_PORTSC_PLC         0x00400000UL    /* bit 22 RW1C          */
#define XHCI_PORTSC_CEC         0x00800000UL    /* bit 23 RW1C          */
#define XHCI_PORTSC_CAS         0x01000000UL    /* bit 24 RO            */
#define XHCI_PORTSC_WCE         0x02000000UL    /* bit 25 RW            */
#define XHCI_PORTSC_WDE         0x04000000UL    /* bit 26 RW            */
#define XHCI_PORTSC_WOE         0x08000000UL    /* bit 27 RW            */
#define XHCI_PORTSC_DR          0x40000000UL    /* bit 30 RO            */
#define XHCI_PORTSC_WPR         0x80000000UL    /* bit 31 RW1S (SS)     */

#define XHCI_PORTSC_GET_PLS(v)   ((((ULONG)(v)) & XHCI_PORTSC_PLS_MASK) >> \
                                  XHCI_PORTSC_PLS_SHIFT)
#define XHCI_PORTSC_GET_SPEED(v) ((((ULONG)(v)) & XHCI_PORTSC_SPEED_MASK) >> \
                                  XHCI_PORTSC_SPEED_SHIFT)

/* Every RW1C bit in the register: writing any of them back clears a change
 * the driver may not have handled yet. */
#define XHCI_PORTSC_CHANGE_MASK \
    (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_WRC | \
     XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | XHCI_PORTSC_CEC)

/* Bits 29:28 are RsvdZ, so software writes 0 rather than preserving what it
 * read. Bit 2 was RsvdZ in revision 1.2; revision 1.2b made it Tunneled Mode
 * (TM), a read-only USB4 flag that is "RsvdZ for a USB2 protocol port"
 * (Table 5-27, p.372), which is every port this driver manages. It stays in
 * this mask: writing 0 to a read-only bit is harmless, and on the USB2 ports
 * the spec still says 0. */
#define XHCI_PORTSC_RSVDZ_MASK  0x30000004UL

/*
 * Bits a read-modify-write must never carry back. PED is RW1C - writing back
 * the 1 that says "enabled" disables the port. PR is RW1S and reads back 1
 * while a reset runs - writing that back restarts it. WPR is RW1S too but
 * "shall always return 0 when read" (5.4.8, Table 5-27) and is RsvdZ on the
 * USB2 protocol ports this driver manages, so it is stripped against a
 * *composed* value rather than a replayed read *(until the post-Phase 13 review rounds this line
 * lumped it with PR as "a reset in progress"; `xhci-data-structures.md` had
 * the right reason first)*. LWS turns the PLS field into an
 * unintended link-state write. The change bits are RW1C. RsvdZ must be 0.
 */
#define XHCI_PORTSC_UNSAFE_MASK \
    (XHCI_PORTSC_PED | XHCI_PORTSC_PR | XHCI_PORTSC_WPR | XHCI_PORTSC_LWS | \
     XHCI_PORTSC_CHANGE_MASK | XHCI_PORTSC_RSVDZ_MASK)

/*
 * The mask is defined by what it must strip; state separately what it must
 * not, so an edit that widens it fails the build instead of quietly dropping
 * port power or a wakeup enable on every read-modify-write.
 */
XHCI_C_ASSERT(portsc_unsafe_mask_preserves_state_bits,
              (XHCI_PORTSC_UNSAFE_MASK &
               (XHCI_PORTSC_CCS | XHCI_PORTSC_OCA | XHCI_PORTSC_PLS_MASK |
                XHCI_PORTSC_PP | XHCI_PORTSC_SPEED_MASK | XHCI_PORTSC_PIC_MASK |
                XHCI_PORTSC_CAS | XHCI_PORTSC_WCE | XHCI_PORTSC_WDE |
                XHCI_PORTSC_WOE | XHCI_PORTSC_DR)) == 0);
XHCI_C_ASSERT(portsc_change_mask_is_within_rw1c_range,
              (XHCI_PORTSC_CHANGE_MASK & ~0x00FE0000UL) == 0);

/*
 * Port Link State values this driver names (spec Table 5-27, p.374). Only the
 * three a USB 2.0 port passes through are here: a USB2 port is in U0 when it is
 * running, U3 when suspended, and Resume while resume signalling is on the bus.
 * PLS is only armed by a write that also carries LWS, which is what
 * XhciPortscSetLinkState exists for.
 */
#define XHCI_PLS_U0             0
#define XHCI_PLS_U3             3
#define XHCI_PLS_RESUME         15

/* Default Protocol Speed IDs (spec 7.2, used only when PSIC = 0). */
#define XHCI_PSIV_FS            1
#define XHCI_PSIV_LS            2
#define XHCI_PSIV_HS            3
#define XHCI_PSIV_SS            4

/* Decoded speed classes. The raw PSIV still goes into the Slot Context;
 * every functional decision uses one of these
 * (docs/contributing/implementation-invariants.md, "Port Speed Decoding"). */
#define XHCI_SPEED_UNKNOWN      0
#define XHCI_SPEED_LOW          1
#define XHCI_SPEED_FULL         2
#define XHCI_SPEED_HIGH         3
#define XHCI_SPEED_SUPER        4

/*
 * The sticky "this start has decoded a device at this speed" set
 * (XHCI_EXTENSION.RhSpeedsSeen). Only the three speeds a USB 2.0 hub class can
 * describe get a bit: SuperSpeed ports are not managed, and an undecodable PSIV
 * is XHCI_SPEED_UNKNOWN, which is the absence of evidence rather than evidence
 * of a speed. See the field comment for why this is a monotone set and not a
 * count or a tally.
 */
#define XHCI_RH_SEEN_LOW        0x00000001UL
#define XHCI_RH_SEEN_FULL       0x00000002UL
#define XHCI_RH_SEEN_HIGH       0x00000004UL

/* ------------------------------------------------------------------ */
/* Interrupter register fields the pure core builds values for         */
/* ------------------------------------------------------------------ */

#define XHCI_ERDP_DESI_MASK     0x00000007UL    /* bits 2:0             */
#define XHCI_ERDP_EHB           0x00000008UL    /* bit 3, RW1C          */

/* ------------------------------------------------------------------ */
/* Spec constants used by the layout (docs/usb-xhci-info/xhci-data-structures.md)    */
/* ------------------------------------------------------------------ */

#define XHCI_PAGE_SIZE              4096UL  /* selected when PAGESIZE bit 0 set */
#define XHCI_TRB_BYTES              16UL
#define XHCI_DCBAA_ENTRY_BYTES      8UL
#define XHCI_SCRATCHPAD_ENTRY_BYTES 8UL

/* Context array element counts (spec 6.2.1 / 6.2.5) */
#define XHCI_DEVICE_CONTEXT_ENTRIES 32UL    /* slot + 31 endpoints        */
#define XHCI_INPUT_CONTEXT_ENTRIES  33UL    /* control + slot + 31 endpts */

/* Both context strides must be tolerated; the layout reserves the larger. */
#define XHCI_CONTEXT_SIZE_SMALL     32UL    /* HCCPARAMS1.CSZ = 0 */
#define XHCI_CONTEXT_SIZE_LARGE     64UL    /* HCCPARAMS1.CSZ = 1 */

/* ------------------------------------------------------------------ */
/* Declared limits - the driver's policy, fixed at DriverEntry         */
/* ------------------------------------------------------------------ */

/*
 * MiniPortResourcesSize is committed in DriverEntry, before StartController
 * can read HCSPARAMS1/HCSPARAMS2. Everything below is therefore a worst-case
 * reservation, and hardware that exceeds it is refused with a named
 * diagnostic rather than silently under-served. Rationale, measured fleet
 * values, and the levers to change these two numbers are in
 * docs/contributing/design/04-controller-common-buffer.md.
 */
#define XHCI_MAX_SLOTS              32UL
#define XHCI_MAX_SCRATCHPAD         64UL

/*
 * The pooled non-EP0 transfer rings (batch 7a-A; design doc 04 section 3.6).
 *
 * Sized to equal XHCI_MAX_SLOTS. That sizing does **not** by itself guarantee
 * every slot an endpoint - eight devices at the per-device cap hold all 32, and
 * guaranteeing the arbitrary worst case would take 125 rings. What protection
 * exists comes from the admission rule in XhciPoolAcquire, and it is narrower
 * than "every slot gets one": a later endpoint never displaces a device that is
 * already live, but a device created after the pool fills may still be refused
 * its first. Design doc 04 section 3.6 has the arithmetic showing the stronger
 * property is unreachable at a pool equal to the slot count.
 *
 * The architectural maximum is 32 x 30 = 960 rings, larger than the whole common
 * buffer; this is a declared policy limit with a refusal above it, exactly like
 * the two above. The per-device cap bounds how fast one device can drain the
 * pool - it is a fairness bound, not the guarantee's mechanism.
 */
#define XHCI_MAX_POOL_RINGS         32UL
#define XHCI_MAX_DEVICE_ENDPOINTS   4UL

/* Ring sizes (Phase 4 roadmap: 64 command TRBs, one 256-TRB event segment) */
#define XHCI_CMD_RING_TRBS          64UL
#define XHCI_EVENT_RING_TRBS        256UL
#define XHCI_EP0_RING_TRBS          64UL
/* Deliberately the same as EP0's, so section 4's "a 1024-byte object at a
 * 1024-aligned offset cannot cross 64 KB" argument covers both regions with one
 * rule rather than two. */
#define XHCI_POOL_RING_TRBS         64UL

/* ------------------------------------------------------------------ */
/* Fixed region geometry                                               */
/* ------------------------------------------------------------------ */

/*
 * Every region starts on an xHC page boundary. usbport hands the miniport a
 * page-aligned StartVA *and* a page-aligned StartPA (proven against both
 * shipping binaries - see the design doc), so a region's offset satisfies the
 * spec's alignment and no-cross-boundary rules physically as well as
 * virtually. Sub-objects packed inside a region carry their own proofs, which
 * test/test_membuf.c re-checks.
 */

#define XHCI_DCBAA_RESERVED         2048UL  /* spec max DCBAA size            */
#define XHCI_SPA_RESERVED           2048UL  /* scratchpad buffer array        */
#define XHCI_CMD_RING_BYTES         (XHCI_CMD_RING_TRBS * XHCI_TRB_BYTES)
#define XHCI_ERST_RESERVED          64UL    /* one entry, 64-byte aligned     */
#define XHCI_EVENT_RING_BYTES       (XHCI_EVENT_RING_TRBS * XHCI_TRB_BYTES)
#define XHCI_INPUT_CONTEXT_RESERVED 4096UL  /* one page; 2112 B worst case    */
#define XHCI_DEVICE_CONTEXT_STRIDE  2048UL  /* worst case, CSZ = 1            */
#define XHCI_EP0_RING_STRIDE        (XHCI_EP0_RING_TRBS * XHCI_TRB_BYTES)
#define XHCI_POOL_RING_STRIDE       (XHCI_POOL_RING_TRBS * XHCI_TRB_BYTES)

#define XHCI_REGION_SMALL_OBJECTS   0UL                     /* DCBAA + SPA    */
#define XHCI_REGION_CMD_RING        (1UL * XHCI_PAGE_SIZE)  /* cmd ring, ERST */
#define XHCI_REGION_EVENT_RING      (2UL * XHCI_PAGE_SIZE)
#define XHCI_REGION_INPUT_CONTEXT   (3UL * XHCI_PAGE_SIZE)
#define XHCI_REGION_DEVICE_CONTEXTS (4UL * XHCI_PAGE_SIZE)
#define XHCI_REGION_DEVICE_CONTEXTS_BYTES \
    (XHCI_MAX_SLOTS * XHCI_DEVICE_CONTEXT_STRIDE)
#define XHCI_REGION_EP0_RINGS \
    (XHCI_REGION_DEVICE_CONTEXTS + XHCI_REGION_DEVICE_CONTEXTS_BYTES)
#define XHCI_REGION_EP0_RINGS_BYTES (XHCI_MAX_SLOTS * XHCI_EP0_RING_STRIDE)
#define XHCI_REGION_POOL_RINGS \
    (XHCI_REGION_EP0_RINGS + XHCI_REGION_EP0_RINGS_BYTES)
#define XHCI_REGION_POOL_RINGS_BYTES \
    (XHCI_MAX_POOL_RINGS * XHCI_POOL_RING_STRIDE)
#define XHCI_REGION_SCRATCHPAD_PAGES \
    (XHCI_REGION_POOL_RINGS + XHCI_REGION_POOL_RINGS_BYTES)
#define XHCI_REGION_SCRATCHPAD_PAGES_BYTES \
    (XHCI_MAX_SCRATCHPAD * XHCI_PAGE_SIZE)

/*
 * The value DriverEntry puts in USBPORT_REGISTRATION_PACKET.MiniPortResourcesSize.
 * usbport actually allocates ROUND_TO_PAGES(this + 48) - see the design doc.
 */
#define XHCI_HC_RESOURCES_SIZE \
    (XHCI_REGION_SCRATCHPAD_PAGES + XHCI_REGION_SCRATCHPAD_PAGES_BYTES)

/* Offsets within the packed regions */
#define XHCI_DCBAA_OFFSET   (XHCI_REGION_SMALL_OBJECTS)
#define XHCI_SPA_OFFSET     (XHCI_REGION_SMALL_OBJECTS + XHCI_DCBAA_RESERVED)
#define XHCI_CMD_RING_OFFSET (XHCI_REGION_CMD_RING)
#define XHCI_ERST_OFFSET    (XHCI_REGION_CMD_RING + XHCI_CMD_RING_BYTES)

/*
 * `XhciComputeLayout` re-checks the geometry at every start, and
 * test/test_membuf.c restates it against hand-written expectations. These
 * asserts are the third line: they fail the *driver* build, so a constant
 * edited without running the host suite cannot reach either guest. Only the
 * properties that must hold for any tuning of the constants above are stated
 * here - the exact offsets belong in the tests, where they are transcribed by
 * hand rather than recomputed from the same macros.
 */
XHCI_C_ASSERT(resources_page_multiple,
              XHCI_HC_RESOURCES_SIZE % XHCI_PAGE_SIZE == 0);
XHCI_C_ASSERT(small_objects_fit_one_page,
              XHCI_DCBAA_RESERVED + XHCI_SPA_RESERVED <= XHCI_PAGE_SIZE);
XHCI_C_ASSERT(dcbaa_reservation_keeps_spa_aligned,
              XHCI_DCBAA_RESERVED % 64UL == 0);
XHCI_C_ASSERT(erst_reservation_holds_one_entry,
              XHCI_ERST_RESERVED >= sizeof(XHCI_ERST_ENTRY));
XHCI_C_ASSERT(cmd_ring_has_policy_minimum,
              XHCI_CMD_RING_TRBS >= 64UL);
XHCI_C_ASSERT(cmd_ring_trbs_are_power_of_two,
              (XHCI_CMD_RING_TRBS &
                  (XHCI_CMD_RING_TRBS - 1UL)) == 0);
XHCI_C_ASSERT(cmd_ring_count_fits_packed_region,
              XHCI_CMD_RING_TRBS <=
                  (XHCI_PAGE_SIZE - XHCI_ERST_RESERVED) / XHCI_TRB_BYTES);
XHCI_C_ASSERT(cmd_ring_keeps_erst_aligned,
              XHCI_CMD_RING_BYTES % 64UL == 0);
XHCI_C_ASSERT(cmd_ring_and_erst_fit_one_page,
              XHCI_CMD_RING_BYTES + XHCI_ERST_RESERVED <= XHCI_PAGE_SIZE);
XHCI_C_ASSERT(event_ring_meets_spec_minimum,
              XHCI_EVENT_RING_TRBS >= 16UL);
XHCI_C_ASSERT(event_ring_meets_spec_maximum,
              XHCI_EVENT_RING_TRBS <= 4096UL);
XHCI_C_ASSERT(event_ring_fits_its_region,
              XHCI_EVENT_RING_BYTES <=
                  XHCI_REGION_INPUT_CONTEXT - XHCI_REGION_EVENT_RING);
XHCI_C_ASSERT(input_context_fits_its_region,
              XHCI_INPUT_CONTEXT_ENTRIES * XHCI_CONTEXT_SIZE_LARGE <=
                  XHCI_INPUT_CONTEXT_RESERVED);
XHCI_C_ASSERT(device_context_fits_its_stride,
              XHCI_DEVICE_CONTEXT_ENTRIES * XHCI_CONTEXT_SIZE_LARGE <=
                  XHCI_DEVICE_CONTEXT_STRIDE);
XHCI_C_ASSERT(dcbaa_fits_its_reservation,
              (XHCI_MAX_SLOTS + 1) * XHCI_DCBAA_ENTRY_BYTES <=
                  XHCI_DCBAA_RESERVED);
XHCI_C_ASSERT(scratchpad_array_fits_its_reservation,
              XHCI_MAX_SCRATCHPAD * XHCI_SCRATCHPAD_ENTRY_BYTES <=
                  XHCI_SPA_RESERVED);
/* Strides must divide the page/64 KB boundaries they are required not to
 * cross, or the per-slot arrays stop being uniformly safe. */
XHCI_C_ASSERT(device_context_stride_divides_page,
              XHCI_PAGE_SIZE % XHCI_DEVICE_CONTEXT_STRIDE == 0);
/*
 * usbport guarantees only page alignment for the common-buffer base. Keep
 * every EP0 segment within one page and use a power-of-two TRB count, so the
 * base and every stride are aligned to the complete segment. A larger segment
 * would need a 64 KB-aligned region or a boundary check using StartPA.
 */
/* Three is the floor, not two: the last TRB is the Link, and a producer ring
 * keeps one slot permanently empty so "enqueue == dequeue" can only mean
 * empty (spec 4.9.2.2, and XhciRingCapacity). */
XHCI_C_ASSERT(ep0_ring_has_link_and_a_usable_slot,
              XHCI_EP0_RING_TRBS >= 3UL);
XHCI_C_ASSERT(ep0_ring_trbs_are_power_of_two,
              (XHCI_EP0_RING_TRBS &
                  (XHCI_EP0_RING_TRBS - 1UL)) == 0);
XHCI_C_ASSERT(ep0_ring_fits_one_page,
              XHCI_EP0_RING_TRBS <= XHCI_PAGE_SIZE / XHCI_TRB_BYTES);
/*
 * The pooled rings inherit every EP0 rule rather than restating a weaker set:
 * same stride, same power-of-two count, same one-page bound. The last assert is
 * the one that keeps them inheritable - if the two strides ever diverge, the
 * 64 KB argument has to be made twice and this catches it at build time.
 */
XHCI_C_ASSERT(pool_ring_has_link_and_a_usable_slot,
              XHCI_POOL_RING_TRBS >= 3UL);
XHCI_C_ASSERT(pool_ring_trbs_are_power_of_two,
              (XHCI_POOL_RING_TRBS &
                  (XHCI_POOL_RING_TRBS - 1UL)) == 0);
XHCI_C_ASSERT(pool_ring_fits_one_page,
              XHCI_POOL_RING_TRBS <= XHCI_PAGE_SIZE / XHCI_TRB_BYTES);
XHCI_C_ASSERT(pool_ring_stride_matches_ep0,
              XHCI_POOL_RING_STRIDE == XHCI_EP0_RING_STRIDE);
/*
 * The pool is at least the slot count, and that is **all this proves** - the
 * name it used to carry (`pool_guarantees_one_ring_per_slot`) and the comment
 * above it both restated a guarantee design doc 04 section 3.6 has since
 * retracted. Eight devices at the per-device cap hold all 32 rings and a ninth
 * is refused its first; what protects a first ring is XhciPoolAcquire's
 * admission rule, and no relation between these two constants establishes it.
 * Kept as a floor because a pool *smaller* than the slot count would weaken even
 * that rule for no saving worth having.
 */
XHCI_C_ASSERT(pool_is_at_least_the_slot_count,
              XHCI_MAX_POOL_RINGS >= XHCI_MAX_SLOTS);
/* The per-device cap must not be able to exhaust the pool on its own. */
XHCI_C_ASSERT(device_endpoint_cap_is_below_the_pool,
              XHCI_MAX_DEVICE_ENDPOINTS < XHCI_MAX_POOL_RINGS);
/* Ring segments are capped at 64 KB by spec Table 6-1. */
XHCI_C_ASSERT(cmd_ring_segment_within_64k, XHCI_CMD_RING_BYTES <= 65536UL);
XHCI_C_ASSERT(event_ring_segment_within_64k, XHCI_EVENT_RING_BYTES <= 65536UL);
XHCI_C_ASSERT(ep0_ring_segment_within_64k, XHCI_EP0_RING_STRIDE <= 65536UL);
XHCI_C_ASSERT(pool_ring_segment_within_64k, XHCI_POOL_RING_STRIDE <= 65536UL);

/* ------------------------------------------------------------------ */
/* Layout computation (pure - no MMIO, no DDK, host-testable)          */
/* ------------------------------------------------------------------ */

typedef struct _XHCI_HC_LAYOUT {
    /* inputs, retained for the carve-time re-checks */
    ULONG ContextSize;              /* 32 or 64, from HCCPARAMS1.CSZ  */
    ULONG MaxSlotsEn;               /* what CONFIG.MaxSlotsEn will be */
    ULONG ScratchpadCount;          /* from HCSPARAMS2                */

    ULONG DcbaaOffset;
    ULONG DcbaaBytes;               /* (MaxSlotsEn + 1) * 8           */

    ULONG ScratchpadArrayOffset;
    ULONG ScratchpadArrayBytes;     /* ScratchpadCount * 8            */

    ULONG CommandRingOffset;
    ULONG CommandRingTrbs;

    ULONG ErstOffset;
    ULONG ErstEntries;

    ULONG EventRingOffset;
    ULONG EventRingTrbs;

    ULONG InputContextOffset;
    ULONG InputContextBytes;        /* 33 * ContextSize               */

    ULONG DeviceContextOffset;
    ULONG DeviceContextStride;
    ULONG DeviceContextBytes;       /* 32 * ContextSize, used portion */

    ULONG Ep0RingOffset;
    ULONG Ep0RingStride;
    ULONG Ep0RingTrbs;

    /* The pooled non-EP0 rings are indexed by pool slot, not by Slot ID: a
     * device holds between zero and XHCI_MAX_DEVICE_ENDPOINTS of them and they
     * are not contiguous per device. PoolRingCount is XHCI_MAX_POOL_RINGS and is
     * carried in the layout so the carve-time checks and the tests read the same
     * number the accessor bounds against. */
    ULONG PoolRingOffset;
    ULONG PoolRingStride;
    ULONG PoolRingTrbs;
    ULONG PoolRingCount;

    ULONG ScratchpadPageOffset;
    ULONG ScratchpadPageStride;

    ULONG TotalBytes;               /* always XHCI_HC_RESOURCES_SIZE  */
} XHCI_HC_LAYOUT, *PXHCI_HC_LAYOUT;

/* Layout / carve status codes. Nonzero values are refusal reasons that
 * StartController maps onto MP_STATUS_NO_RESOURCES.
 *
 * The offending quantity is readable from any flavour: XhciDeriveControllerInfo
 * writes hc.scratchpad, hc.maxslots, hc.contextsize and the rest into the log
 * ring at CAP_DECODE, before this carve runs, and XHCISNAP reads them back off
 * the machine. The traced (qemu) build also prints them as XHCI_DBG_VALUE
 * lines. (This comment said "a debug-build diagnostic" until the post-Phase 13 review rounds, which
 * was wrong twice over after task 13-L.1: the trace is qemu's, and the number
 * was never only in it.) */
#define XHCI_LAYOUT_OK                  0
#define XHCI_LAYOUT_BAD_CONTEXT_SIZE    1
#define XHCI_LAYOUT_BAD_PAGE_SIZE       2
#define XHCI_LAYOUT_TOO_MANY_SLOTS      3
#define XHCI_LAYOUT_TOO_MANY_SCRATCHPAD 4
#define XHCI_LAYOUT_NO_SLOTS            5
#define XHCI_LAYOUT_UNALIGNED_BASE      6
#define XHCI_LAYOUT_OVERFLOW            7
#define XHCI_LAYOUT_BAD_INDEX           8

/*
 * Compute the carve for a controller. IRQL: any (pure arithmetic).
 * hcPageSizeMask is the raw xHC PAGESIZE register value. Bit n advertises
 * support for 2^(n+12) bytes; this layout selects 4096 when bit 0 is present
 * and refuses controllers that do not advertise it.
 */
ULONG XhciComputeLayout(ULONG contextSize,
                        ULONG hwMaxSlots,
                        ULONG scratchpadCount,
                        ULONG hcPageSizeMask,
                        PXHCI_HC_LAYOUT layout);

/* Byte offset accessors. Each returns XHCI_LAYOUT_OK and writes *offset, or
 * a refusal code and leaves *offset untouched. IRQL: any. */
ULONG XhciDeviceContextOffset(const XHCI_HC_LAYOUT *layout,
                              ULONG slotId,
                              ULONG *offset);
ULONG XhciEp0RingOffset(const XHCI_HC_LAYOUT *layout,
                        ULONG slotId,
                        ULONG *offset);
/* Indexed by pool slot 0..PoolRingCount-1, unlike the two above which take a
 * 1-based Slot ID. XHCI_LAYOUT_BAD_INDEX for anything else. */
ULONG XhciPoolRingOffset(const XHCI_HC_LAYOUT *layout,
                         ULONG poolIndex,
                         ULONG *offset);

/* ------------------------------------------------------------------ */
/* The pooled-ring allocator (design doc 04 section 3.6)               */
/* ------------------------------------------------------------------ */

/*
 * Which pool rings are in use, and by which device record.
 *
 * `Owner[i]` is a device *reference* - an index into XHCI_EXTENSION.Devices plus
 * one, the same encoding XHCI_ENDPOINT.DeviceIndex uses - so the zeroed state
 * means "free" rather than "owned by device 0". Held in the controller
 * extension, under the controller lock like every other shared record.
 */
typedef struct _XHCI_RING_POOL {
    ULONG Owner[XHCI_MAX_POOL_RINGS];
    ULONG InUse;                /* rings currently allocated                 */
    /* Diagnostics. The two refusals are counted apart because they have
     * opposite meanings: an exhausted pool is a machine with more endpoints
     * than the declared limit, while a fairness refusal is this driver
     * protecting a first endpoint it has not been asked for yet. */
    ULONG AcquireFailuresEmpty;
    ULONG AcquireFailuresFairness;
    ULONG AcquireFailuresCap;
    ULONG PeakInUse;
#ifdef XHCI_FIX_NO_RING_REUSE
    /*
     * **EXPERIMENTAL, bench candidate W2 for Finding 3.** Present only under
     * the define, so no shipping flavour carries it and no shipping
     * MiniPortExtensionSize moves. A binary built with it therefore reports a
     * SIZEOF the tracked offset table does not match, and **must not be used
     * for a counter reading** - it is for the behavioural bench test only.
     *
     * Where the next scan for a free ring starts. XhciPoolAcquire is otherwise
     * first-fit, which means the ring a device gives up at teardown is handed
     * to the very next device that asks - the immediate reuse Finding 3's
     * hypothesised mechanism turns on.
     */
    ULONG NextScan;
#endif
} XHCI_RING_POOL, *PXHCI_RING_POOL;

#define XHCI_POOL_OK            0
#define XHCI_POOL_BAD_PARAM     1
#define XHCI_POOL_EMPTY         2   /* no free ring at all                   */
#define XHCI_POOL_UNFAIR        3   /* free, but reserved for first endpoints */
#define XHCI_POOL_AT_CAP        4   /* this device already holds the maximum  */

VOID XhciPoolInit(PXHCI_RING_POOL pool);

/*
 * Grant a pool ring to `deviceRef` (an index into Devices plus one).
 *
 * `ringless` is the number of *other* device records that could still open an
 * endpoint and currently hold no pool ring - the caller counts it, because only
 * the caller knows which records are live. The admission rule is design doc 04
 * section 3.6's:
 *
 *   - a device's **first** ring is granted whenever one is free;
 *   - a later ring is granted only if `free - 1 >= ringless`, which preserves
 *     the invariant `free >= ringless` and so keeps every ringless device able
 *     to obtain one.
 *
 * That rule, not the pool size, is what makes "no device is starved of its
 * first endpoint" true. Returns XHCI_POOL_OK and writes *poolIndex, or a
 * refusal code and leaves it untouched. IRQL: any (the caller holds the lock).
 */
ULONG XhciPoolAcquire(PXHCI_RING_POOL pool,
                      ULONG deviceRef,
                      ULONG ringless,
                      ULONG *poolIndex);

/* Release one ring. Refuses an index that is free, or that a different device
 * owns - both are bookkeeping errors that would otherwise hand the same ring to
 * two endpoints. IRQL: any. */
ULONG XhciPoolRelease(PXHCI_RING_POOL pool, ULONG deviceRef, ULONG poolIndex);

/* Release every ring a device holds, and answer how many that was. Used by the
 * teardown path, where the endpoint records may already be gone. IRQL: any. */
ULONG XhciPoolReleaseDevice(PXHCI_RING_POOL pool, ULONG deviceRef);

/*
 * There is deliberately **no way to disown a ring without releasing it**, and
 * that is a decision an earlier draft got wrong in an instructive way.
 *
 * A device record can be given up on a controller that was never shown to have
 * let go of its slot. Its Endpoint Contexts may still name pool rings, so
 * returning them to the free list would hand live DMA memory to the next
 * endpoint - and a sentinel "owned by nobody" marker was added to cover exactly
 * that. It was the wrong shape: the same evidence that says the rings may not be
 * recycled also says the record's *transfers* may not be completed, so the
 * record cannot be released either. `xhciDevRelease` abandons it instead, the
 * rings stay owned by a record that still exists, and the sentinel had nothing
 * left to name. `XhciPoolInit` - which only runs after HCRST - is what reclaims
 * everything either way.
 */

/* How many rings this device holds, and how many are free. IRQL: any. */
ULONG XhciPoolDeviceCount(const XHCI_RING_POOL *pool, ULONG deviceRef);
ULONG XhciPoolFree(const XHCI_RING_POOL *pool);
ULONG XhciScratchpadPageOffset(const XHCI_HC_LAYOUT *layout,
                               ULONG index,
                               ULONG *offset);

/*
 * Context strides. Every context byte offset is index * ContextSize, where
 * ContextSize is 32 or 64 from HCCPARAMS1.CSZ and is *unrelated* to 32-bit
 * DMA addressing (docs/contributing/implementation-invariants.md, "Context Stride"). These
 * return an offset from the common-buffer base so no caller ever multiplies a
 * context index by a hardcoded 32 again.
 *
 * Device Context: index 0 = Slot Context, index i = Endpoint Context for DCI i.
 * Input Context:  index 0 = Input Control Context, index 1 = Slot Context,
 *                 index i+1 = Endpoint Context for DCI i (shifted by one).
 *
 * Each returns XHCI_LAYOUT_OK and writes *offset, or a refusal code and leaves
 * *offset untouched. IRQL: any.
 */
#define XHCI_MAX_DCI 31

/*
 * Table 6-12 p.420, "Endpoint Type vs. Interval Calculation": the Endpoint
 * Context Interval values legal for each speed. FS/LS Interrupt is 3-10;
 * SuperSpeed/High-Speed Interrupt or Isoch is 0-15. Transcribed here rather than
 * open-coded at the one site that applies them, because they are a spec table
 * and this file is where spec tables live.
 */
#define XHCI_EP_INTERVAL_FSLS_MIN   3
#define XHCI_EP_INTERVAL_FSLS_MAX   10
#define XHCI_EP_INTERVAL_HS_MAX     15

/*
 * Task 9-A.1. **Isoch has its own row in the same table**, and it is not the
 * interrupt row: Full-Speed Isoch is 3-18 where FS/LS Interrupt stops at 10,
 * because `bInterval` means `2^(bInterval-1) ms` for one and `bInterval ms` for
 * the other. Reusing the interrupt bounds for isoch would refuse a legal context
 * rather than build an illegal one, which is the safe direction and still wrong.
 *
 * The two values below are the **fallback** Intervals - what an isochronous
 * endpoint is programmed with when its own `bInterval` is not known - and they
 * are read off usbport's arithmetic rather than off a descriptor. usbport forces
 * an isochronous `Period` to 1 and carries no interval at all, so the only
 * statement it makes about the service rate is how it stamps each packet's frame
 * and microframe (docs/usb-xhci-info/usbport-miniport-abi.md section 4, task 9-0.1): on High
 * Speed `StartFrame + (i >> 3)` with microframe `i & 7`, which is one packet per
 * *microframe*, and otherwise `StartFrame + i` with microframe 0, which is one
 * packet per *frame*. So the ESIT usbport is scheduling against is 125 us on HS
 * and 1 ms on FS - Interval 0 and Interval 3 - and both sit inside their row's
 * range. This is a measurement of the other side's arithmetic, not a guess at a
 * descriptor field.
 *
 * *(This block called them the "*derived*" Intervals, "derived from usbport
 * rather than from a descriptor this driver never sees", until the post-Phase 13 review rounds. Task
 * 9-A.2 built that channel: `src/xhci_desc.c` snoops the configuration
 * descriptor on EP0, and where it yields a `bInterval` the Interval really is
 * derived and these two constants are not used. They are what
 * `src/xhci_ctx.c`'s isochronous branch programs when it is not - no descriptor
 * read, no such endpoint in it, or alternates that disagree with no selection
 * to resolve them - and that branch has stated them as an assumption rather
 * than a derivation since its own review round.)*
 */
#define XHCI_EP_INTERVAL_FS_ISOCH_MIN   3
#define XHCI_EP_INTERVAL_FS_ISOCH_MAX   18
#define XHCI_EP_INTERVAL_HS_ISOCH_MAX   15
#define XHCI_EP_INTERVAL_ISOCH_HS       0
#define XHCI_EP_INTERVAL_ISOCH_FS       3

ULONG XhciSlotContextOffset(const XHCI_HC_LAYOUT *layout,
                            ULONG slotId,
                            ULONG *offset);
ULONG XhciEndpointContextOffset(const XHCI_HC_LAYOUT *layout,
                                ULONG slotId,
                                ULONG dci,
                                ULONG *offset);
ULONG XhciInputControlContextOffset(const XHCI_HC_LAYOUT *layout,
                                    ULONG *offset);
ULONG XhciInputSlotContextOffset(const XHCI_HC_LAYOUT *layout,
                                 ULONG *offset);
ULONG XhciInputEndpointContextOffset(const XHCI_HC_LAYOUT *layout,
                                     ULONG dci,
                                     ULONG *offset);

/* Confirm usbport's StartVA/StartPA are usable for this layout. IRQL: any. */
ULONG XhciCheckResourceBase(ULONG_PTR startVA, ULONG startPA);

/* What usbport will actually ask the DMA adapter for, given a packet
 * MiniPortResourcesSize of resourcesSize. IRQL: any. */
ULONG XhciCommonBufferAllocationBytes(ULONG resourcesSize);

/* usbport's private common-buffer header, appended after the miniport's
 * request before rounding to pages (read out of both shipping binaries). */
#define XHCI_USBPORT_CB_HEADER_BYTES 48UL

/* ------------------------------------------------------------------ */
/* Contexts (docs/usb-xhci-info/xhci-data-structures.md section 8, src/xhci_ctx.c)   */
/* ------------------------------------------------------------------ */

/*
 * Only the first 32 bytes of a context carry defined fields at either stride,
 * so every builder here writes exactly eight DWORDs and the stride decides only
 * where the *next* context starts (the offset accessors above own that). Eight
 * is written down rather than left implicit because a builder that wrote
 * ContextSize/4 words would clear the upper 32 reserved bytes at CSZ = 1, which
 * is a write into RsvdZ space the specification does not ask for.
 */
#define XHCI_CONTEXT_DWORDS         8UL

/* Input Control Context (spec 6.2.5.1). Bits 1:0 of the Drop word are RsvdZ. */
#define XHCI_ICC_DW_DROP            0
#define XHCI_ICC_DW_ADD             1
#define XHCI_ICC_DW_CONFIG          7
#define XHCI_ICC_FLAG(dci)          (1UL << ((dci) & 0x1FUL))
/* A0 is the Slot Context's flag and A1 is EP0's - the two Address Device sets
 * (spec 6.2.5.1 usage note), and the two an EP0-only Evaluate Context touches. */
#define XHCI_ICC_A0                 XHCI_ICC_FLAG(0)
#define XHCI_ICC_A1                 XHCI_ICC_FLAG(1)
#define XHCI_ICC_DROP_RSVDZ_MASK    0x00000003UL

/* Slot Context (spec 6.2.2, Tables 6-4..6-7). */
#define XHCI_SLOT_ROUTE_MASK        0x000FFFFFUL
#define XHCI_SLOT_SPEED_SHIFT       20
#define XHCI_SLOT_SPEED_MASK        0x00F00000UL
#define XHCI_SLOT_MTT               0x02000000UL
#define XHCI_SLOT_HUB               0x04000000UL
#define XHCI_SLOT_ENTRIES_SHIFT     27
#define XHCI_SLOT_ENTRIES_MASK      0xF8000000UL
#define XHCI_SLOT_MAX_EXIT_MASK     0x0000FFFFUL
#define XHCI_SLOT_ROOT_PORT_SHIFT   16
#define XHCI_SLOT_PORT_COUNT_SHIFT  24
#define XHCI_SLOT_TT_SLOT_SHIFT     0
#define XHCI_SLOT_TT_PORT_SHIFT     8
#define XHCI_SLOT_TTT_SHIFT         16
#define XHCI_SLOT_INTR_TARGET_SHIFT 22
/* DW3, both output-only: the address the xHC assigned, and the slot's state. */
#define XHCI_SLOT_GET_ADDRESS(dw3)  (((ULONG)(dw3)) & 0xFFUL)
#define XHCI_SLOT_GET_STATE(dw3)    ((((ULONG)(dw3)) >> 27) & 0x1FUL)
#define XHCI_SLOT_STATE_DISABLED    0   /* Disabled *or* Enabled - one encoding */
#define XHCI_SLOT_STATE_DEFAULT     1
#define XHCI_SLOT_STATE_ADDRESSED   2
#define XHCI_SLOT_STATE_CONFIGURED  3

/* Endpoint Context (spec 6.2.3, Tables 6-8..6-11). */
#define XHCI_EP_STATE_MASK          0x00000007UL
#define XHCI_EP_MULT_SHIFT          8
#define XHCI_EP_MAXPSTREAMS_SHIFT   10
#define XHCI_EP_LSA                 0x00008000UL
#define XHCI_EP_INTERVAL_SHIFT      16
#define XHCI_EP_CERR_SHIFT          1
#define XHCI_EP_TYPE_SHIFT          3
#define XHCI_EP_MAXBURST_SHIFT      8
#define XHCI_EP_MAXPACKET_SHIFT     16
#define XHCI_EP_DCS                 0x00000001UL
#define XHCI_EP_DEQUEUE_MASK        0xFFFFFFF0UL
#define XHCI_EP_AVG_TRB_MASK        0x0000FFFFUL
#define XHCI_EP_MAX_ESIT_SHIFT      16
/* Output-only: the endpoint state the xHC maintains in the Device Context. */
#define XHCI_EP_GET_STATE(dw0)      (((ULONG)(dw0)) & XHCI_EP_STATE_MASK)
#define XHCI_EP_STATE_DISABLED      0
#define XHCI_EP_STATE_RUNNING       1
#define XHCI_EP_STATE_HALTED        2
#define XHCI_EP_STATE_STOPPED       3
#define XHCI_EP_STATE_ERROR         4

/* EP Type (spec Table 6-9). */
#define XHCI_EP_TYPE_INVALID        0
#define XHCI_EP_TYPE_ISOCH_OUT      1
#define XHCI_EP_TYPE_BULK_OUT       2
#define XHCI_EP_TYPE_INTERRUPT_OUT  3
#define XHCI_EP_TYPE_CONTROL        4
#define XHCI_EP_TYPE_ISOCH_IN       5
#define XHCI_EP_TYPE_BULK_IN        6
#define XHCI_EP_TYPE_INTERRUPT_IN   7

/* Control and bulk/interrupt endpoints use CErr = 3; isoch **must** use 0
 * (Table 6-9 note), which is Phase 9's problem and not encodable by accident
 * here because the caller supplies it. */
#define XHCI_EP_CERR_DEFAULT        3
/* "Average TRB Length ... shall be greater than '0'" (Table 6-11) and the
 * specification's own worked example for the default control endpoint is 8. */
#define XHCI_EP_AVG_TRB_CONTROL     8

/*
 * EP0's Max Packet Size before the device descriptor has been read (spec 4.3).
 * LS and HS are fixed; FS is 8, 16, 32 or 64 and unknowable until the
 * descriptor arrives, so one of the four has to be assumed and then corrected
 * with Evaluate Context (task 6-B.4).
 *
 * **The FS assumption is 64, not the 8 spec 4.3 suggests, and the reason is
 * that the field describes what the CONTROLLER will accept rather than what the
 * device must send** (batch 13-E). The two directions are not
 * symmetric:
 *
 *   - declared LARGER than the device's actual value: every packet arrives
 *     short, which is legal and costs nothing;
 *   - declared SMALLER: the first packet that exceeds it is **babble**, and on
 *     a conforming controller the transfer dies there.
 *
 * Spec 4.3's "start at 8" is sound where software fetches 8 descriptor bytes
 * first - which is what Linux's usbcore does. **usbport asks for 64**, measured
 * on the 2a guest: `GET_DESCRIPTOR(DEVICE)` with `wLength` = 0x40 is
 * issued at address 0, before SET_ADDRESS and long before any correction can
 * land. A device whose `bMaxPacketSize0` is 16 or 64 answers in packets of that
 * size, into an endpoint declared as 8. This driver cannot change what usbport
 * asks for, so the assumption has to absorb it.
 *
 * That was not theory: on real xHCI silicon `041E:323D` (mps0 64) and
 * `041E:324D` (mps0 16) produced `Unknown Device` with no wizard at all, while
 * `0D8C:0014` (mps0 8) enumerated - see docs/contributing/runs/run-13e.md,
 * "Session record - bench session 1". QEMU does not reproduce it because its
 * xHC does not enforce Max Packet Size on IN transfers.
 *
 * Linux reaches the same value from the same direction: its USB core assumes 64
 * for a Full-Speed control endpoint and `xhci_check_maxpacket` corrects it after
 * the first descriptor fetch.
 *
 * **The correction now runs downward for the common case**, since a device
 * reporting 8 no longer matches the assumption. That is the intended shape and
 * the path already handles it - the trigger is any difference and 8 is a legal
 * target - but it does mean FS devices that never took the correction before
 * now take it.
 */
#define XHCI_EP0_MPS_LOW            8UL
#define XHCI_EP0_MPS_FULL_INITIAL   64UL
#define XHCI_EP0_MPS_HIGH           64UL

/* Every legal EP0 Max Packet Size, as a validation set rather than a range:
 * bMaxPacketSize0 is one of exactly these four (USB 2.0 9.6.1). */
#define XHCI_EP0_MPS_IS_LEGAL(m) \
    ((m) == 8UL || (m) == 16UL || (m) == 32UL || (m) == 64UL)

/* Context builder status codes. */
#define XHCI_CTX_OK                 0
#define XHCI_CTX_BAD_PARAM          1

/*
 * What a Slot Context says about one device. Everything but Psiv, RootHubPort
 * and ContextEntries is zero for the direct-attach devices Phase 6 addresses;
 * the fields are here rather than added later because a hub tier that arrives
 * in Phase 7b must not be a change to the encoder's shape.
 *
 * `Psiv` is the **raw** PORTSC Port Speed value, not this driver's decoded
 * XHCI_SPEED_* class. The two are different vocabularies that happen to agree
 * for the default speed IDs, and a controller with a Protocol Speed ID table
 * assigns its own values - so the slot context carries what the port reported
 * and the class decides only things like the initial EP0 Max Packet Size
 * (docs/contributing/implementation-invariants.md, "Port Speed Decoding").
 */
typedef struct _XHCI_SLOT_PARAMS {
    ULONG RouteString;      /* 0 for a device on a root port */
    ULONG Psiv;             /* raw PORTSC Port Speed field */
    ULONG RootHubPort;      /* 1-based xHCI port the path starts at */
    ULONG ContextEntries;   /* highest DCI in use; 1 for EP0 alone */
    ULONG Hub;
    ULONG NumberOfPorts;    /* only meaningful when Hub is 1 */
    ULONG MultiTt;
    ULONG MaxExitLatency;
    ULONG ParentSlotId;     /* TT hub's Slot ID, 0 when there is no TT */
    ULONG ParentPortNumber;
    ULONG TtThinkTime;
    ULONG InterrupterTarget;
} XHCI_SLOT_PARAMS, *PXHCI_SLOT_PARAMS;

typedef struct _XHCI_EP_PARAMS {
    ULONG EpType;           /* XHCI_EP_TYPE_* */
    ULONG MaxPacketSize;
    ULONG MaxBurstSize;
    ULONG Mult;
    ULONG Interval;
    ULONG ErrorCount;       /* CErr */
    ULONG DequeuePA;        /* 16-byte aligned transfer-ring base */
    ULONG Dcs;              /* the ring's current dequeue cycle state */
    ULONG AverageTrbLength; /* must be nonzero */
    ULONG MaxEsitPayload;
} XHCI_EP_PARAMS, *PXHCI_EP_PARAMS;

/*
 * Encoders. Each writes XHCI_CONTEXT_DWORDS words at `context`, which is a
 * pointer into cached common-buffer memory (volatile for the reason
 * docs/contributing/design/04-controller-common-buffer.md section 6 gives) and must be
 * the base of one context, not of the block. None of them publishes anything:
 * the command TRB that names the Input Context is what does that, and it is
 * written afterwards.
 *
 * They refuse rather than truncate. A Max Packet Size that does not fit its
 * field, a DequeuePA with low bits set, a Route String wider than 20 bits - all
 * of them are this driver's bookkeeping being wrong about a device, and a
 * silently masked value would be programmed into hardware as a plausible lie.
 *
 * IRQL: any (computation plus stores into caller-supplied memory).
 */
VOID XhciContextZero(volatile ULONG *context, ULONG dwords);
ULONG XhciBuildInputControlContext(volatile ULONG *context,
                                   ULONG addFlags,
                                   ULONG dropFlags);
ULONG XhciBuildSlotContext(volatile ULONG *context,
                           const XHCI_SLOT_PARAMS *params);
ULONG XhciBuildEndpointContext(volatile ULONG *context,
                               const XHCI_EP_PARAMS *params);

/*
 * The EP0 parameters this driver uses for a device of a given decoded speed and
 * Max Packet Size, so that the one place the control endpoint's fixed fields
 * are chosen is not restated at the Address Device and Evaluate Context sites.
 * Refuses a Max Packet Size that is not one of the four legal values.
 * IRQL: any.
 */
ULONG XhciBuildEp0Params(ULONG maxPacketSize,
                         ULONG dequeuePA,
                         ULONG dcs,
                         PXHCI_EP_PARAMS params);

/* The Max Packet Size to address a device of this decoded speed class with,
 * before its device descriptor has been read. 0 for a speed this driver does
 * not address. IRQL: any. */
ULONG XhciInitialMps0(ULONG speedClass);

/*
 * The Endpoint Context `Interval` for a periodic endpoint usbport has described
 * with `EndpointProperties.Period` and a decoded speed class.
 *
 * **usbport has already done the `bInterval` conversion, and its `Period` does
 * not mean the same thing at every speed**: it counts microframes on High Speed
 * and frames on Full/Low Speed. Both facts are binary-derived - see
 * `docs/usb-xhci-info/usbport-miniport-abi.md` section 5, "Periodic scheduling: what `Period`
 * actually carries", which proves the High-Speed unit from the 63-entry
 * schedule table in both shipping `usbehci.sys` builds. usbport hands the
 * miniport no `bInterval`, so the per-speed table in `docs/usb-xhci-info/xhci-data-structures.md`
 * section 8 is *not* this function's job; applying it here would convert twice.
 * *(This said "the miniport never sees `bInterval` at all" until the post-Phase 13 review rounds.
 * Task 9-A.2's descriptor snoop sees it - but on EP0 and for isochronous
 * endpoints only, and never through the `Period` this function is handed, so
 * the conclusion is untouched: what arrives here is already converted.)*
 *
 * xHCI's field counts microframes (period = 2^Interval * 125 us), so this is
 * `log2(Period) + (High Speed ? 0 : 3)`. The reachable results are **0-5 High
 * Speed, 3-8 Full Speed, 6-8 Low Speed** - Low Speed is not 3-8 because usbport
 * floors its `Period` at 8 upstream. That floor is *usbport's*, and this
 * function deliberately does not know about it: if it did, an LS endpoint that
 * legitimately arrived at 8 could not be told from one this code had repaired.
 *
 * Returns XHCI_CTX_OK and writes *interval, or XHCI_CTX_BAD_PARAM. It
 * **refuses rather than repairs** a Period outside the derived contract - not a
 * power of two, zero, or above 32 - because a value the derivation did not
 * predict means the contract was misread, and a repaired one would be
 * programmed into hardware as a plausible lie. IRQL: any.
 */
ULONG XhciIntervalFromPeriod(ULONG period, ULONG speedClass, ULONG *interval);

/*
 * Table 6-12's valid `Interval` range for the speed the **Slot Context** will
 * carry, applied to the log2-microframes value `XhciIntervalFromPeriod`
 * produced.
 *
 * These are two different questions and conflating them produced an Endpoint
 * Context a conforming xHC may refuse. The unit of usbport's `Period` follows
 * the speed *usbport* believes; the set of legal Interval values follows the
 * speed the *device* actually runs at, because that is what the Slot Context
 * says and what 6.2.3.2 validates against. Table 6-12 p.420: FS/LS Interrupt
 * 3-10, SS/HS Interrupt or Isoch 0-15.
 *
 * The two speeds agree everywhere except behind Phase 5 task 7's root-port
 * reporting, which tells usbport every connected port is High Speed. There a
 * Full-Speed endpoint with `bInterval = 1` reaches Interval 0 beside a
 * Full-Speed Slot Context. This **raises it to the floor and says so** through
 * `*floored` rather than refusing: the input is faithfully what usbport
 * computed, the discrepancy is downstream of this driver's own untruth, and a
 * FS/LS interrupt endpoint cannot be serviced faster than once a frame anyway -
 * so the floor is also the fastest schedule the bus can carry. An interval
 * *above* the range is still refused, because nothing in the derivation
 * produces one.
 *
 * `floored` may be NULL. Returns XHCI_CTX_OK and writes *out, or
 * XHCI_CTX_BAD_PARAM. IRQL: any.
 */
ULONG XhciIntervalForSpeed(ULONG interval,
                           ULONG deviceSpeedClass,
                           ULONG *out,
                           ULONG *floored);

/*
 * Endpoint parameters for a non-default endpoint, from what usbport supplies.
 *
 * `transferType` is a USBPORT_TRANSFER_TYPE_* value and `directionIn` is 1 for
 * an IN endpoint. `period` is only read for an interrupt endpoint - control and
 * bulk carry 0 there and isoch's is forced to 1 and means nothing (section 5
 * again), so a caller that passed it through for those types would be reading a
 * field usbport does not fill.
 *
 * **Two speeds, and they are not interchangeable.** `periodSpeedClass` is the
 * speed usbport used to bucket `Period` and to derive
 * `transactionsPerMicroframe` - it decides the unit of both. `deviceSpeedClass`
 * is the speed the Slot Context will carry, and it decides which `Interval`
 * values are legal. See XhciIntervalForSpeed for why they can differ and what
 * happens when they do; `intervalFloored` (which may be NULL) reports it.
 *
 * **`descBInterval` is task 9-A.2's channel and is read for an isochronous
 * endpoint only.** 0 means no configuration descriptor has been walked for this
 * device, or it declared nothing usable for this endpoint address, and the
 * Interval then falls back to the cadence usbport is itself scheduling to -
 * which is this driver's behaviour before task 9-A.2 and is correct exactly
 * when `bInterval` is 1. A nonzero value is converted by
 * `XhciIsochIntervalFromBInterval` and **refused rather than clamped** if it
 * cannot be. `intervalDerived` (which may be NULL) reports which of the two
 * happened, because a derived Interval makes usbport's own packet stamps
 * disagree by design and that must not read as the assumption failing.
 *
 * Refuses anything this driver does not serve rather than defaulting it.
 * IRQL: any.
 */
ULONG XhciBuildEndpointParams(ULONG transferType,
                              ULONG directionIn,
                              ULONG maxPacketSize,
                              ULONG period,
                              ULONG periodSpeedClass,
                              ULONG deviceSpeedClass,
                              ULONG transactionsPerMicroframe,
                              ULONG descBInterval,
                              ULONG dequeuePA,
                              ULONG dcs,
                              PXHCI_EP_PARAMS params,
                              PULONG intervalFloored,
                              PULONG intervalDerived);

/*
 * The Endpoint Context Interval an **isochronous** endpoint descriptor's
 * `bInterval` maps to (task 9-A.2), from the conversion table in
 * docs/usb-xhci-info/xhci-data-structures.md (spec 6.2.3.6, Table 6-12):
 *
 *   High Speed  period = 2^(bInterval-1) microframes -> `bInterval - 1`, 0-15
 *   Full Speed  period = 2^(bInterval-1) ms          -> `bInterval + 2`, 3-18
 *
 * Both ranges are exactly the image of `bInterval` 1..16, so a value outside
 * that is refused - there is no Interval it means. Low Speed is refused too,
 * and not for want of a formula: USB 2.0 section 5.6 has no isochronous
 * transfers at that speed at all, so the request describes no legal device.
 *
 * Returns XHCI_CTX_OK and writes *interval, or XHCI_CTX_BAD_PARAM. IRQL: any.
 */
ULONG XhciIsochIntervalFromBInterval(ULONG bInterval,
                                     ULONG speedClass,
                                     ULONG *interval);

/* The Device Context Index for a raw bEndpointAddress (spec 4.5.1): EP0 is 1,
 * endpoint n OUT is 2n, endpoint n IN is 2n+1. 0 if the address names no valid
 * DCI. IRQL: any. */
ULONG XhciDciFromEndpointAddress(ULONG endpointAddress);

/* Its inverse for a non-default endpoint, so the two spellings of the mapping
 * cannot drift apart. 0 for DCI 0 or 1 - the Slot Context names no endpoint and
 * EP0's two directions share one context. IRQL: any. */
ULONG XhciEndpointAddressFromDci(ULONG dci);

/* ------------------------------------------------------------------ */
/* Capability register fields (docs/usb-xhci-info/xhci-data-structures.md section 2) */
/* ------------------------------------------------------------------ */

#define XHCI_HCSPARAMS1_MAXSLOTS(v)  (((ULONG)(v)) & 0xFFUL)
#define XHCI_HCSPARAMS1_MAXINTRS(v)  ((((ULONG)(v)) >> 8) & 0x7FFUL)
#define XHCI_HCSPARAMS1_MAXPORTS(v)  ((((ULONG)(v)) >> 24) & 0xFFUL)

#define XHCI_HCSPARAMS2_IST(v)       (((ULONG)(v)) & 0xFUL)
#define XHCI_HCSPARAMS2_ERSTMAX(v)   ((((ULONG)(v)) >> 4) & 0xFUL)
#define XHCI_HCSPARAMS2_SPR(v)       ((((ULONG)(v)) >> 26) & 0x1UL)
/*
 * Max Scratchpad Buffers is split, and the *high* five bits are the lower-
 * numbered field: Hi = 25:21, Lo = 31:27. Reading it the other way round
 * yields a plausible small number and a controller that silently refuses to
 * run (docs/usb-xhci-info/xhci-data-structures.md section 2 note).
 */
#define XHCI_HCSPARAMS2_MAXSCRATCHPAD(v) \
    (((((ULONG)(v)) >> 21) & 0x1FUL) << 5 | ((((ULONG)(v)) >> 27) & 0x1FUL))

#define XHCI_HCCPARAMS1_AC64(v)      (((ULONG)(v)) & 0x1UL)
#define XHCI_HCCPARAMS1_BNC(v)       ((((ULONG)(v)) >> 1) & 0x1UL)
#define XHCI_HCCPARAMS1_CSZ(v)       ((((ULONG)(v)) >> 2) & 0x1UL)
#define XHCI_HCCPARAMS1_PPC(v)       ((((ULONG)(v)) >> 3) & 0x1UL)
#define XHCI_HCCPARAMS1_CFC(v)       ((((ULONG)(v)) >> 11) & 0x1UL)
#define XHCI_HCCPARAMS1_MAXPSA(v)    ((((ULONG)(v)) >> 12) & 0xFUL)
/* xECP is a DWORD offset from BAR0, not a byte offset. */
#define XHCI_HCCPARAMS1_XECP(v)      ((((ULONG)(v)) >> 16) & 0xFFFFUL)

/* Context stride from CSZ, as an actual byte count. */
#define XHCI_CONTEXT_SIZE_FROM_CSZ(hcc) \
    (XHCI_HCCPARAMS1_CSZ(hcc) ? XHCI_CONTEXT_SIZE_LARGE : XHCI_CONTEXT_SIZE_SMALL)

/* ------------------------------------------------------------------ */
/* Capability-register derivation (src/xhci_caps.c)                    */
/* ------------------------------------------------------------------ */

/*
 * Everything the rest of the driver needs to know about a controller before it
 * programs one, decoded from the seven capability registers and checked
 * against the length of the BAR0 mapping usbport handed over.
 *
 * The three derived bases are the reason this is a function rather than a
 * handful of macros at the call site. CAPLENGTH, RTSOFF and DBOFF are values
 * the *device* supplies: a controller that is not decoding its BAR answers
 * every read with all-ones, and an all-ones RTSOFF used as an offset lands
 * outside the mapping. So each one is bounds-checked by subtraction
 * (`offset <= mappedBytes - required`), never by `offset + required <= mapped`,
 * which wraps for exactly the values this is defending against
 * (docs/contributing/implementation-invariants.md, "MMIO Sanity").
 */
typedef struct _XHCI_HC_INFO {
    ULONG CapLength;
    ULONG HciVersion;

    /* Byte offsets from BAR0, each proven to leave room for what this driver
     * reads through it. */
    ULONG OperationalOffset;    /* == CapLength                            */
    ULONG PortscOffset;         /* OperationalOffset + 0x400               */
    ULONG RuntimeOffset;        /* RTSOFF, low 5 bits masked off           */
    ULONG DoorbellOffset;       /* DBOFF, low 2 bits masked off            */

    ULONG MaxPorts;             /* HCSPARAMS1.MaxPorts                     */
    ULONG MaxSlots;             /* HCSPARAMS1.MaxSlots - hardware's figure */
    ULONG MaxIntrs;             /* HCSPARAMS1.MaxIntrs                     */
    ULONG ScratchpadCount;      /* HCSPARAMS2 Max Scratchpad Buffers       */
    /*
     * Task 9-A.1. The isochronous scheduling threshold, **already converted to
     * frames** rather than kept in its encoded form, because every consumer
     * wants frames: it is only ever added to a Frame ID, and Frame IDs count
     * frames. HCSPARAMS2 `3:0` encodes a unit in bit 3 - clear means `2:0`
     * microframes, set means `2:0` frames (5.3.4) - and the microframe case
     * rounds **up** to a frame, which the spec requires at the one place the
     * value is used ("where IST shall be rounded up to the nearest frame
     * boundary if it is defined in microframes", 4.11.2.5 p.199).
     *
     * The extra microframe 4.14.2.1.4 p.243 requires for read latency is *not*
     * folded in here: it belongs to the reader of MFINDEX, not to the
     * controller's declared threshold, and adding it here would make a
     * round-tripped value disagree with the register.
     */
    ULONG IstFrames;

    ULONG ContextSize;          /* 32 or 64, from HCCPARAMS1.CSZ           */
    ULONG Ac64;                 /* recorded, never acted on: no 64-bit DMA */
    ULONG Ppc;                  /* HCCPARAMS1.PPC - is PORTSC.PP writable  */
    /*
     * Contiguous Frame ID Capability, HCCPARAMS1 bit 11. It decides whether an
     * explicit Frame ID may appear in any Isoch TD but the first of a data flow:
     * with CFC = 0 the spec says software "shall set SIA = '1' in all subsequent
     * TDs" (4.11.2.5 p.200), so this is a contract term rather than a
     * performance hint. Mandatory on every xHCI 1.1 and 1.2 controller, and
     * therefore expected to read 1 on real silicon and 0 on a 1.0 model.
     */
    ULONG Cfc;
    ULONG XecpDwords;           /* HCCPARAMS1.xECP, a DWORD offset         */
    /*
     * Force Save Context Capability, HCCPARAMS2 bit 2 - and the **only** field
     * in this structure that can come from a register that is not there.
     * **The gate is reachability, not version**: HCCPARAMS2 is defined at 1.0
     * and Appendix H.1.6 (p.593) lists FSC among the capabilities merely
     * *optional* there, so a 1.0 controller may advertise it and is asked. What
     * can make the register absent is a `CapLength` that stops before 20h, which
     * puts the operational registers on top of the address. This comment said
     * "HCCPARAMS2 arrived in xHCI 1.1" until audit round 4 refuted it, and the
     * version gate it described is gone; see XhciDeriveHcInfo for the two gates
     * that remain and XHCI_HCCPARAMS2_FSC for what the bit decides.
     *
     * Zero is the safe direction in both senses: it is what a controller that
     * does not implement the register would ideally answer, and it is the value
     * that makes the suspend path decline a Save State rather than take one it
     * cannot make complete.
     */
    ULONG Fsc;
} XHCI_HC_INFO, *PXHCI_HC_INFO;

/* Derivation status codes. Nonzero refuses the controller. */
#define XHCI_HC_OK                  0
#define XHCI_HC_BAD_PARAM           1
#define XHCI_HC_NOT_DECODING        2   /* an all-ones read - see below    */
#define XHCI_HC_BAD_CAPLENGTH       3
#define XHCI_HC_BAD_VERSION         4
#define XHCI_HC_BAD_RTSOFF          5
#define XHCI_HC_BAD_DBOFF           6
#define XHCI_HC_NO_PORTS            7
#define XHCI_HC_NO_SLOTS            8
#define XHCI_HC_NO_INTERRUPTERS     9
#define XHCI_HC_WINDOW_TOO_SMALL    10

/*
 * The first two capability values, checked before anything else is read.
 * `capDword0` is the DWORD at BAR0 + 0 - CAPLENGTH in bits 7:0 and HCIVERSION
 * in bits 31:16, which is why they are one read and one check.
 *
 * This exists separately from XhciDeriveHcInfo (which calls it) because the
 * order matters: "validate HCIVERSION and CAPLENGTH for plausibility
 * immediately after mapping BAR0, before any other register access". An
 * all-ones answer here means the device is not decoding at all - Memory Space
 * Enable clear, D3, a bad BAR mapping, or a removed device - and every later
 * read would be equally meaningless.
 *
 * Either output pointer may be NULL. IRQL: any (pure).
 */
ULONG XhciCheckCapDword0(ULONG capDword0,
                         ULONG mappedBytes,
                         ULONG *capLength,
                         ULONG *hciVersion);

/*
 * Decode the capability registers into XHCI_HC_INFO. `mappedBytes` is
 * usbport's IoSpaceLength for BAR0. On any nonzero return `info` is left
 * untouched, so a caller cannot half-adopt a refused controller.
 *
 * `hccparams2` is the **optional** one - optional because it may be out of
 * reach, not because of the controller's version: the caller passes whatever it
 * read (or 0 when the window is too small to read it at all) and this function
 * decides whether to believe it. Both gates
 * are here rather than at the call site so that "does this controller declare
 * FSC" has exactly one answer - and so that the host suite can ask it.
 *
 * IRQL: any (pure).
 */
ULONG XhciDeriveHcInfo(ULONG capDword0,
                       ULONG hcsparams1,
                       ULONG hcsparams2,
                       ULONG hccparams1,
                       ULONG hccparams2,
                       ULONG dboff,
                       ULONG rtsoff,
                       ULONG mappedBytes,
                       PXHCI_HC_INFO info);

/* Do two derivations describe the same controller? The init sequence derives
 * once before the BIOS handoff (it needs xECP) and again after the reset, and
 * a disagreement means the controller changed identity under it. IRQL: any. */
ULONG XhciHcInfoEqual(const XHCI_HC_INFO *a, const XHCI_HC_INFO *b);

/* ------------------------------------------------------------------ */
/* Extended capabilities (docs/usb-xhci-info/xhci-data-structures.md section 6)      */
/* ------------------------------------------------------------------ */

#define XHCI_XECP_ID(dw)             (((ULONG)(dw)) & 0xFFUL)
#define XHCI_XECP_NEXT(dw)           ((((ULONG)(dw)) >> 8) & 0xFFUL)

#define XHCI_XECP_ID_LEGACY          1
#define XHCI_XECP_ID_PROTOCOL        2
#define XHCI_XECP_ID_DEBUG           10

/* USB Legacy Support (spec 7.1.1), both semaphores in the header DWORD. */
#define XHCI_USBLEGSUP_BIOS_OWNED    0x00010000UL
#define XHCI_USBLEGSUP_OS_OWNED      0x01000000UL
/*
 * The capability is two DWORDs: USBLEGSUP itself and USBLEGCTLSTS at +4 (spec
 * 7.1.2), and the handoff reads and writes both. That is the span
 * XhciFindExtendedCap has to prove is inside the mapping, not the four header
 * bytes it would otherwise check.
 */
#define XHCI_USBLEGSUP_BYTES         8UL
#define XHCI_USBLEGCTLSTS_OFFSET     4UL
/* USBLEGCTLSTS: every SMI enable is in 15:0, and the RW1C status bits the
 * handoff acknowledges are 31:29. */
#define XHCI_USBLEGCTLSTS_SMI_ENABLES   0x0000FFFFUL
#define XHCI_USBLEGCTLSTS_SMI_STATUS    0xE0000000UL

/* Supported Protocol (spec 7.2). */
#define XHCI_PROTOCOL_MINOR(dw0)     ((((ULONG)(dw0)) >> 16) & 0xFFUL)
#define XHCI_PROTOCOL_MAJOR(dw0)     ((((ULONG)(dw0)) >> 24) & 0xFFUL)
#define XHCI_PROTOCOL_NAME_USB       0x20425355UL    /* "USB " */
#define XHCI_PROTOCOL_PORT_OFFSET(dw2) (((ULONG)(dw2)) & 0xFFUL)
#define XHCI_PROTOCOL_PORT_COUNT(dw2)  ((((ULONG)(dw2)) >> 8) & 0xFFUL)
#define XHCI_PROTOCOL_PSIC(dw2)        ((((ULONG)(dw2)) >> 28) & 0xFUL)
#define XHCI_PROTOCOL_SLOT_TYPE(dw3)   (((ULONG)(dw3)) & 0x1FUL)

/* Protocol Speed ID DWORD (spec 7.2.2.1.2). */
#define XHCI_PSI_PSIV(dw)            (((ULONG)(dw)) & 0xFUL)
#define XHCI_PSI_PSIE(dw)            ((((ULONG)(dw)) >> 4) & 0x3UL)
#define XHCI_PSI_PSIM(dw)            ((((ULONG)(dw)) >> 16) & 0xFFFFUL)

/* ------------------------------------------------------------------ */
/* Port classification (src/xhci_caps.c)                               */
/* ------------------------------------------------------------------ */

/* HCSPARAMS1.MaxPorts is eight bits, so 255 is the whole address space. */
#define XHCI_MAX_ROOT_PORTS     255
/* Two protocols is the shipping shape (one USB2 + one USB3); the slack is for
 * controllers that split a revision across capabilities. */
#define XHCI_MAX_PROTOCOLS      4
/* PSIC is four bits: retain every entry a controller can advertise
 * (docs/contributing/implementation-invariants.md, "Port Speed Decoding"). */
#define XHCI_MAX_PSI            15

#define XHCI_PORT_CLASS_NONE            0   /* named by no protocol capability */
#define XHCI_PORT_CLASS_USB2_ONLY       1   /* manage: power on, handle connects */
#define XHCI_PORT_CLASS_USB2_COMPANION  2   /* manage: the USB2 half of a connector */
#define XHCI_PORT_CLASS_USB3_COMPANION  3   /* leave unpowered and unmanaged     */
#define XHCI_PORT_CLASS_USB3_ORPHAN     4   /* out of scope - needs SuperSpeed    */
/* One past the last class, so a per-class tally can be an array. */
#define XHCI_PORT_CLASS_COUNT           5

#define XHCI_PORT_NO_COMPANION  0
#define XHCI_PORT_NO_PROTOCOL   0xFF

typedef struct _XHCI_PROTOCOL {
    ULONG Major;            /* BCD major revision: 2 = USB 2, 3 = USB 3 */
    ULONG Minor;
    ULONG PortOffset;       /* 1-based first port of the group */
    ULONG PortCount;
    ULONG SlotType;         /* Enable Slot command's Slot Type field */
    ULONG PsiCount;         /* 0 = the default speed IDs apply */
    ULONG Psi[XHCI_MAX_PSI];/* raw PSI DWORDs, retained verbatim */
} XHCI_PROTOCOL;

/*
 * Tail padding, declared rather than left to the compiler. Three UCHAR arrays
 * of 255 leave the structure three bytes short of a ULONG boundary, and three
 * bytes of *implicit* padding are three bytes a later UCHAR field could be
 * appended into with sizeof(XHCI_PORT_MAP) unchanged - which would slip past
 * the layout assertion in src/xhci_caps.c and out of XhciPortMapEqual with it.
 * Named, the bytes are initialized and compared like any other field, so
 * appending anything changes the size and fails that assertion, and a byte
 * later spent as a real field is covered from the moment it is spent.
 */
#define XHCI_PORT_MAP_RESERVED  3

typedef struct _XHCI_PORT_MAP {
    ULONG PortCount;                /* HCSPARAMS1.MaxPorts */
    ULONG ProtocolCount;
    ULONG ManagedPortCount;         /* the USB2-only and USB2-companion ports */
    ULONG LegacySupportOffset;      /* byte offset from BAR0; 0 = no such cap */
    ULONG DebugCapabilityOffset;    /* byte offset from BAR0; 0 = absent      */
    XHCI_PROTOCOL Protocols[XHCI_MAX_PROTOCOLS];
    /* All three are indexed by (port number - 1); ports are 1-based. */
    UCHAR Class[XHCI_MAX_ROOT_PORTS];
    UCHAR Protocol[XHCI_MAX_ROOT_PORTS];    /* index into Protocols[] */
    UCHAR Companion[XHCI_MAX_ROOT_PORTS];   /* paired port number, or 0 */
    UCHAR Reserved[XHCI_PORT_MAP_RESERVED];
} XHCI_PORT_MAP, *PXHCI_PORT_MAP;

/* Parser status codes. Nonzero refuses the controller. */
#define XHCI_CAPS_OK                    0
#define XHCI_CAPS_BAD_PARAM             1
#define XHCI_CAPS_NO_LIST               2   /* xECP = 0 */
#define XHCI_CAPS_OUT_OF_RANGE          3   /* a capability lies outside BAR0 */
#define XHCI_CAPS_LOOP                  4   /* NEXT does not advance */
#define XHCI_CAPS_TOO_MANY_PROTOCOLS    5
#define XHCI_CAPS_BAD_PORT_RANGE        6   /* a group names a nonexistent port */
#define XHCI_CAPS_OVERLAPPING_PORT      7   /* two groups claim the same port  */
#define XHCI_CAPS_NOT_FOUND             8
/*
 * The last three are not produced by the parser: src/xhci_init.c raises them
 * about a chain that parsed cleanly. They share this namespace so that
 * XHCI_EXTENSION.InitStatus has exactly one meaning per value at whichever of
 * the two port-map steps recorded it.
 *
 * NO_MANAGED_PORTS can come from either step. The other two can only come from
 * XHCI_INIT_STEP_PORT_MAP_RECHECK, because both are statements about a second
 * parse disagreeing with the first.
 */
#define XHCI_CAPS_NO_MANAGED_PORTS      9   /* no USB 2.0 port to serve       */
#define XHCI_CAPS_LEGACY_MOVED          10  /* USBLEGSUP is not where the
                                             * handoff found it (RECHECK)     */
#define XHCI_CAPS_TOPOLOGY_CHANGED      11  /* the re-parse disagrees with the
                                             * preflight one (RECHECK)        */
/*
 * A register the handoff must read-modify-write answered all ones, so no valid
 * operand exists. Its own code rather than one of the malformed-chain ones
 * above, because the chain parsed: this is a statement about the window having
 * stopped decoding, which is a different diagnosis from a bad descriptor.
 */
#define XHCI_CAPS_NOT_DECODING          12

/*
 * How the parser reads BAR0. A function pointer rather than a mapped pointer
 * keeps this file free of MMIO accessors: the driver passes a
 * READ_REGISTER_ULONG wrapper, the host tests pass a synthetic capability
 * chain (docs/contributing/design/03-host-unit-tests.md section 2). `byteOffset` is
 * always DWORD-aligned and always inside the length the caller declared.
 */
typedef ULONG (*XHCI_READ32)(PVOID context, ULONG byteOffset);

/*
 * Walk the extended capability list and classify every logical port.
 * `xecpDwords` is HCCPARAMS1.xECP (a DWORD offset), `mappedBytes` the length
 * of the BAR0 mapping usbport handed over, `maxPorts` HCSPARAMS1.MaxPorts.
 * IRQL: any (the reader decides; the driver's is DISPATCH-safe MMIO).
 */
ULONG XhciParseExtendedCaps(XHCI_READ32 read,
                            PVOID context,
                            ULONG xecpDwords,
                            ULONG mappedBytes,
                            ULONG maxPorts,
                            PXHCI_PORT_MAP map);

/*
 * Locate one capability by ID without classifying anything - the BIOS-handoff
 * path needs USBLEGSUP before the port map means anything. Writes the byte
 * offset from BAR0.
 *
 * `requiredBytes` is how much of the capability the caller will actually
 * touch, and a match is refused with XHCI_CAPS_OUT_OF_RANGE unless that whole
 * span is inside `mappedBytes`. It is not optional bookkeeping: the capability
 * *header* is four bytes, so without it a capability found in the last DWORD
 * of the mapping hands the caller an offset it cannot safely read past - and
 * the handoff both reads and writes the DWORD after the header. Must be at
 * least 4.
 *
 * IRQL: any.
 */
ULONG XhciFindExtendedCap(XHCI_READ32 read,
                          PVOID context,
                          ULONG xecpDwords,
                          ULONG mappedBytes,
                          ULONG capabilityId,
                          ULONG requiredBytes,
                          ULONG *byteOffset);

/*
 * Compare two parses of the same chain. The driver parses twice - once in the
 * preflight and once after the reset - and a difference means the controller's
 * memory map changed underneath it.
 *
 * Exact and field by field, deliberately not a digest: this comparison is the
 * evidence that the two maps agree, so anything that can answer "equal" for two
 * different maps answers the wrong question. Both maps are extension-owned, so
 * exactness costs nothing on the stack (src/xhci_caps.c has the full argument,
 * and the constructed collision that retired the digest lives in
 * test/test_caps.c).
 *
 * Fields rather than storage. XHCI_PORT_MAP declares its own tail padding and
 * XhciParseExtendedCaps writes every byte of the structure including it, so a
 * byte-wise comparison would in fact be correct today - but only for as long as
 * that holds, and it would be resting on a compiler's layout rather than on
 * this driver's initializer. Both properties are asserted where the comparison
 * is (XHCI_PORT_MAP_RESERVED, and the layout XHCI_C_ASSERT in xhci_caps.c).
 *
 * Returns 1 for equal, 0 for different or for a NULL argument. IRQL: any.
 */
ULONG XhciPortMapEqual(const XHCI_PORT_MAP *a, const XHCI_PORT_MAP *b);

/* Port accessors. Out-of-range port numbers answer "not ours" rather than
 * reading past the arrays. Ports are 1-based. IRQL: any. */
ULONG XhciPortClass(const XHCI_PORT_MAP *map, ULONG port);
ULONG XhciPortIsManaged(const XHCI_PORT_MAP *map, ULONG port);
ULONG XhciPortSlotType(const XHCI_PORT_MAP *map, ULONG port, ULONG *slotType);

/*
 * Decode a raw PORTSC Port Speed value against the protocol group that owns
 * the port. Falls back to the default IDs only when that group advertises no
 * PSI table; a PSIV absent from a non-empty table decodes as
 * XHCI_SPEED_UNKNOWN, never as a default. IRQL: any.
 */
ULONG XhciPortSpeedClass(const XHCI_PORT_MAP *map,
                         ULONG port,
                         ULONG psiv,
                         ULONG *speedClass);

/*
 * The other direction, which task 7b-A.3 needs: **which raw Protocol Speed ID
 * does this controller use for a device of this speed class on this port's
 * protocol group?**
 *
 * A root-port device's Slot Context Speed is read straight out of PORTSC. A
 * device *behind a hub* has no PORTSC of its own - the only statement of its
 * speed is usbport's `DeviceSpeed`, which is a class and not an ID - so the ID
 * has to be looked up in the same PSI table `XhciPortSpeedClass` decodes
 * through, or the driver would be writing "3 means High Speed" into a Slot
 * Context on the very controller that reordered its IDs
 * (docs/contributing/implementation-invariants.md, "Port Speed Decoding").
 *
 * Answers XHCI_CAPS_NOT_FOUND when the group advertises a PSI table with no
 * entry of that speed, which is a refusal rather than a default for the same
 * reason the decode direction has one. Falls back to the default IDs only when
 * the group advertises no table at all. IRQL: any.
 */
ULONG XhciPortPsivForSpeed(const XHCI_PORT_MAP *map,
                           ULONG port,
                           ULONG speedClass,
                           ULONG *psiv);

/* ------------------------------------------------------------------ */
/* PORTSC write construction (src/xhci_port.c)                         */
/* ------------------------------------------------------------------ */

/*
 * PORTSC mixes RW1C change bits with bits whose write side effects disable or
 * reset the port, so a read-modify-write is only safe through these. Each
 * takes the value just read and returns the value to write. IRQL: any.
 *
 * The first four are the primitives; the five below them are the *operations*
 * the root-hub callbacks perform, named so that no call site composes a port
 * operation out of primitives at the point of use (roadmap Phase 5 task 3).
 * That is not tidiness: "set PP" and "clear PED" look alike and one of them is
 * write-1-to-disable, so the difference between them belongs in one file with
 * a golden vector each rather than at twelve call sites.
 */
ULONG XhciPortscNeutral(ULONG portsc);
ULONG XhciPortscWith(ULONG portsc, ULONG bits);
ULONG XhciPortscClearChanges(ULONG portsc, ULONG changeBits);
ULONG XhciPortscSetLinkState(ULONG portsc, ULONG pls);

ULONG XhciPortscPower(ULONG portsc, ULONG on);
ULONG XhciPortscReset(ULONG portsc);
ULONG XhciPortscDisable(ULONG portsc);
ULONG XhciPortscSuspend(ULONG portsc);
/*
 * Resuming a USB 2.0 port is **two** writes with a timed gap, not one - see the
 * definitions. `Signal` starts resume signalling and `Done` ends it; neither is
 * a resume on its own.
 */
ULONG XhciPortscResumeSignal(ULONG portsc);
ULONG XhciPortscResumeDone(ULONG portsc);

/* ------------------------------------------------------------------ */
/* The root hub as usbport sees it (src/xhci_port.c, src/xhci_rh.c)    */
/* ------------------------------------------------------------------ */

/*
 * USB hub-class port status and change bits - the two USHORTs `RH_GetPortStatus`
 * fills, and the only vocabulary usbport and `usbhub20.sys` have for a port.
 *
 * **Provenance, because it is weaker than this file's other tables and saying
 * so is the point.** These are the USB 2.0 specification's Table 11-21
 * (wPortStatus) and Table 11-22 (wPortChange); there is no USB 2.0 PDF in
 * `docs/references/`, so unlike every xHCI constant here they were not
 * transcribed against a local copy. Two independent facts bound them, both
 * recorded in `docs/usb-xhci-info/usbport-miniport-abi.md` section 4:
 *
 *   - the status-change scan in both shipping `usbport.sys` builds reads
 *     **only** `wPortChange & 0x1F` and `wHubChange & 0x03`, which is exactly
 *     five port change bits at 4:0 and two hub change bits at 1:0 (binary
 *     confirmed);
 *   - the change selectors and the status bits share their low five positions
 *     by construction of the hub class, and ReactOS's EHCI miniport fills the
 *     same fields in the same order (`external/reactos/usbehci/roothub.c`).
 *
 * What neither bounds is the *order* inside those five, or the position of the
 * speed bits. That is the one thing in Phase 5 that a VM run confirms rather
 * than a document: a wrong order shows up as a device enumerating at the wrong
 * speed or a connect that never arrives, both visible at the checkpoint.
 */
#define XHCI_HUB_PORT_CONNECTION        0x0001UL
#define XHCI_HUB_PORT_ENABLE            0x0002UL
#define XHCI_HUB_PORT_SUSPEND           0x0004UL
#define XHCI_HUB_PORT_OVER_CURRENT      0x0008UL
#define XHCI_HUB_PORT_RESET             0x0010UL
#define XHCI_HUB_PORT_POWER             0x0100UL
#define XHCI_HUB_PORT_LOW_SPEED         0x0200UL
#define XHCI_HUB_PORT_HIGH_SPEED        0x0400UL

#define XHCI_HUB_C_PORT_CONNECTION      0x0001UL
#define XHCI_HUB_C_PORT_ENABLE          0x0002UL
#define XHCI_HUB_C_PORT_SUSPEND         0x0004UL
#define XHCI_HUB_C_PORT_OVER_CURRENT    0x0008UL
#define XHCI_HUB_C_PORT_RESET           0x0010UL

/*
 * What the status-change scan actually looks at. A change reported outside this
 * mask is invisible to usbport - it will not wake the root hub's change pipe -
 * so the latch below is deliberately confined to it.
 */
#define XHCI_HUB_C_PORT_MASK            0x001FUL

/* Hub status and change (USB 2.0 Table 11-19/11-20). Both are constant zero for
 * this root hub: xHCI reports over-current per port, and the hub's own supply
 * is the machine's. */
#define XHCI_HUB_STATUS_LOCAL_POWER_LOST 0x0001UL
#define XHCI_HUB_STATUS_OVER_CURRENT     0x0002UL
#define XHCI_HUB_C_LOCAL_POWER           0x0001UL
#define XHCI_HUB_C_OVER_CURRENT          0x0002UL
#define XHCI_HUB_C_HUB_MASK              0x0003UL

/* GET_STATUS(device) for the root hub itself: self-powered, no remote wakeup
 * armed. Reached through the *standard* command path, not the class one. */
#define XHCI_HUB_SELF_POWERED            0x0001UL

/*
 * wHubCharacteristics (USB 2.0 Table 11-13), same provenance note as above.
 * Bits 1:0 are the logical power switching mode and bits 4:3 the over-current
 * protection mode; this driver reports individual for both when the controller
 * supports port power switching, because PORTSC gives it a per-port PP and a
 * per-port OCA either way.
 */
#define XHCI_HUB_CHAR_POWER_GANGED       0x0000UL
#define XHCI_HUB_CHAR_POWER_INDIVIDUAL   0x0001UL
#define XHCI_HUB_CHAR_OC_GLOBAL          0x0000UL
#define XHCI_HUB_CHAR_OC_INDIVIDUAL      0x0008UL

/*
 * One managed port's shadow.
 *
 * `Portsc` is the last value read, kept because the *previous* link state is
 * what makes a completed resume distinguishable from any other PLC - there is
 * no "resume finished" bit, only "the link state changed", so the answer is a
 * comparison against what the port was.
 *
 * `Changes` is the latched hub-class change set, and the split between it and
 * the hardware's RW1C bits is the whole design of this shadow:
 *
 *   - a PORTSC change bit is acknowledged **as soon as it is observed**,
 *     because a change bit nobody clears suppresses the controller's next Port
 *     Status Change Event for that port (measured on QEMU: 24 hot-plug
 *     operations produced 6 events, `docs/contributing/lessons.md`,
 *     "hot-plug *operations* are not hot-plug *events*");
 *   - the hub-class change it implies is latched **here** and cleared only by
 *     the matching `RH_ClearFeaturePortXChange` callback, because that is the
 *     hub class's contract and usbport may poll long after the event.
 *
 * Losing either half loses a connect: acknowledging without latching drops the
 * change before anyone is told, latching without acknowledging silences the
 * port after the first edge.
 */
/*
 * The two asynchronous port operations (Phase 5 task 4), and the third value is
 * "neither".
 *
 * They share one mechanism - a per-port generation, an armed state that excludes
 * any other operation on that port, and an uncancellable timer carrying the
 * generation - and they do **not** share a completion rule:
 *
 *   RESET: the timer is a **deadline**. PRC is the finish line; the timer exists
 *   to diagnose and recover a completion that never arrived. Either may claim
 *   the generation, and claiming early is impossible because PRC *is* the end.
 *
 *   RESUME: the timer is a **floor**. "Software shall ensure that resume is
 *   signaled for at least 20 ms (TDRSMDN)" (4.15.2.2, p.257), so the timer alone
 *   completes it and a Port Status Change Event arriving mid-interval updates
 *   the shadow and leaves the resume alone. Letting an event claim it would end
 *   resume signalling below the minimum and leave the device asleep under a
 *   driver reporting success.
 */
#define XHCI_PORT_OP_NONE       0
#define XHCI_PORT_OP_RESET      1
#define XHCI_PORT_OP_RESUME     2

typedef struct _XHCI_PORT_SHADOW {
    ULONG Portsc;       /* the last raw PORTSC this driver read     */
    /*
     * The armed operation's token, monotonic within one start and never reused,
     * so an uncancellable timer callback is a comparison rather than a race. 0
     * is "nothing was ever armed on this port" and is never handed out.
     */
    ULONG Generation;
    UCHAR Changes;      /* latched XHCI_HUB_C_PORT_* bits           */
#ifdef XHCI_FIX_ACK_OWED
    /*
     * **EXPERIMENTAL, bench candidate W13 for Finding 3** (`run-13e.md`
     * **Finding O**). Present only under the define, so no shipping flavour
     * carries it and no shipping MiniPortExtensionSize moves.
     *
     * **A PORTSC change-bit acknowledgement this driver owes the hardware and
     * has not managed to write.** `xhciRhWritePortsc` refuses every write while
     * a Port Power change is in flight, and the acknowledgement's `ackBits` was
     * a stack local - so a refusal *dropped the debt*. An unacknowledged change
     * bit suppresses the controller's next Port Status Change Event for that
     * port (`lessons.md`, "hot-plug *operations* are not hot-plug *events*"), which takes the port out of service for
     * the life of the driver.
     *
     * **It is a debt against the HARDWARE, and that is what makes it a separate
     * field from `Changes`.** `Changes` is what `RH_GetPortStatus` reports and
     * what `CLEAR_FEATURE` clears; neither touches a register. Clearing one has
     * never implied the other, and conflating them is what would make a
     * usbhub-side clear look like a hardware acknowledgement.
     *
     * **Only the acknowledgement path may own this.** `xhciRhWritePortsc` also
     * carries reset, resume and disable writes, whose composed values are made
     * from the reading of the moment - replaying a stale one would restart a
     * reset or disable a port. So the debt is *only* the RW1C change mask, it is
     * re-composed against a **fresh** PORTSC read on every retry, and nothing
     * but the refresh's acknowledgement writes it.
     */
    ULONG PortscAckOwed;
#endif
    UCHAR Speed;        /* XHCI_SPEED_*, decoded through the PSI table */
    UCHAR Armed;        /* XHCI_PORT_OP_*, the operation in flight   */
    /* This port owes a timer arm that must be issued with the controller lock
     * released. Set by an arming site that runs inside the lock - the event
     * DPC's device-initiated resume - and drained by XhciRootHubDeferredWork. */
    UCHAR ArmPending;
    /*
     * **A disable or power-off whose device teardown is owed but not yet
     * earned**, and the review that produced it is worth the field.
     *
     * Taking a port out of service takes its device with it, but *issuing the
     * write is not the port having stopped* - the same rule `PpPending` below
     * exists for. The teardown completes queued transfers, which hands their
     * mapped scatter/gather buffers back to usbport while the TRBs naming them
     * may still be executable on a port that has not gone down yet. That is the
     * DMA-into-reclaimed-memory hazard `XhciSlotInvalidateAll`'s evidence rule
     * was written to prevent, and the first draft of the port-disable trigger
     * walked straight into it by tearing down immediately after the write.
     *
     * So the write confirms its own target bit - `PP` for a power-off, `PED`
     * for a disable - and tears down at once when it reads clear. When it does
     * not, this is set and the health poll settles it, exactly as `PpPending`
     * is settled. `DisownWantsPp` records which bit to confirm, because by then
     * the operation that armed it is gone.
     */
    UCHAR DisownPending;
    UCHAR DisownWantsPp;
    /*
     * When the armed operation's budget started running, and it exists for
     * exactly one failure: **`UsbPortRequestAsyncCallback` answers 0 on success
     * and 0 when its own pool allocation fails**, so a port can be armed with no
     * callback ever scheduled for it. Nothing else would ever disarm it, every
     * later reset and resume on that port would be refused as busy, and the
     * device on it would never enumerate - for the life of the driver.
     *
     * The command engine met the same hazard and answered it with a stamp in
     * the extension plus a copy of the generation the stamp belongs to
     * (`CommandAgeStamp`/`CommandAgeGeneration`), because two commands issued
     * between two polls would otherwise share an age. Here the age lives *in*
     * the structure that holds the operation and is disarmed by
     * `XhciPortShadowArm`, so it belongs to one operation by construction and
     * needs no second field to say which.
     *
     * **A `PollClockMs` reading rather than a poll count since task 13-R.3.5.**
     * As sixteen polls it was 0.6-1.3 s on the E460 rather than 8 s, which
     * leaves 1.2-2.6x over the 500 ms deadline it exists to sit behind instead
     * of 16x - close enough that a reset finishing late could be retired while
     * its own timer was still legitimately running. `AgeArmed` says the
     * stamp has been taken; it is what makes the answer come exactly once per
     * arming, which the poll-count form got from an equality test that a clock
     * cannot make.
     */
    ULONG AgeStamp;
    UCHAR AgeArmed;
    /*
     * **A Port Power change this driver issued and has not seen land.**
     *
     * "After modifying PP, software shall read PP and confirm that it is reached
     * its target state **before modifying it again**, undefined behavior may
     * occur if this procedure is not followed" (Table 5-27, p.375), and the
     * delay is not hypothetical: "the PP flag may be delayed in reflecting this
     * change, e.g. due to waiting for a port related state machine to complete
     * reset signaling" (footnote 91, p.375).
     *
     * The obligation outlives the callback that took it on, which is why it is
     * recorded here rather than checked once and forgotten. Every PORTSC write
     * in the root-hub family composes a *neutral* value, and a neutral value
     * carries PP exactly as it reads - so an ordinary status query refreshing a
     * port whose power-off is still in flight would write PP back as 1 and
     * cancel it. `PpWanted` is the value being waited for and `PpStamp` bounds
     * the wait - a `PollClockMs` reading, taken at the first poll that sees the
     * wait outstanding and flagged by `PpArmed` - so a port whose PP never
     * arrives cannot be blocked for ever.
     */
    UCHAR PpPending;
    UCHAR PpWanted;
    UCHAR PpArmed;
    ULONG PpStamp;
} XHCI_PORT_SHADOW;

/*
 * The root hub usbport builds a PDO for: the managed USB 2.0 ports, numbered
 * 1..PortCount in port order, and nothing else.
 *
 * The two index arrays are the explicit map roadmap Phase 5 task 2 asks for.
 * They are not an optimisation over walking the port map - they are the
 * statement that hub port *n* is one specific PORTSC and always the same one:
 * usbport's port numbers are dense and 1-based, xHCI's interleave USB 2.0 and
 * USB 3.x groups, and every arithmetic shortcut between the two ("USB2 ports
 * come first", "hub port n is PORTSC n") is false on a controller that pairs
 * them the other way round.
 *
 * `PortToHub` holds 0 for every unmanaged port, which is what makes a Port
 * Status Change Event naming a USB 3.x port a lookup that answers "not mine"
 * rather than an index into a shadow that does not describe it.
 */
#define XHCI_RH_OK              0
#define XHCI_RH_BAD_PARAM       1
#define XHCI_RH_NO_PORTS        2   /* nothing to build a root hub out of */

typedef struct _XHCI_ROOT_HUB {
    ULONG PortCount;                        /* managed ports = hub ports  */
    ULONG Status;                           /* XHCI_RH_*                  */
    UCHAR HubToPort[XHCI_MAX_ROOT_PORTS];   /* [hubPort - 1] -> xHCI port */
    UCHAR PortToHub[XHCI_MAX_ROOT_PORTS];   /* [port - 1] -> hub port, 0  */
    UCHAR Reserved[2];
    XHCI_PORT_SHADOW Ports[XHCI_MAX_ROOT_PORTS];    /* [hubPort - 1]      */
} XHCI_ROOT_HUB, *PXHCI_ROOT_HUB;

/*
 * Build the map from a port classification. Ports are visited in ascending
 * order, so hub port numbering follows PORTSC numbering; every shadow is
 * cleared, because a rebuild means the previous reading described a controller
 * this driver has since reset. IRQL: any.
 */
ULONG XhciRootHubBuild(const XHCI_PORT_MAP *map, PXHCI_ROOT_HUB rh);

/* Translations, both answering 0 for "no such port". IRQL: any. */
ULONG XhciRootHubPortOf(const XHCI_ROOT_HUB *rh, ULONG hubPort);
ULONG XhciRootHubHubPortOf(const XHCI_ROOT_HUB *rh, ULONG port);

/*
 * Fold a freshly read PORTSC into a shadow.
 *
 * `speedClass` is the decoded XHCI_SPEED_* for the value in the register's Port
 * Speed field; the caller decodes it because that needs the PSI table, which
 * belongs to the port map rather than to the port. `*ackBits` comes back as the
 * PORTSC change bits the caller must now write 1 to - every change bit that was
 * set, not only the ones that mean something to the hub class, because an
 * unacknowledged one suppresses the next event just the same.
 *
 * Returns the hub-class change bits newly latched by this call (0 when the port
 * had nothing new to say). IRQL: any.
 */
ULONG XhciPortShadowUpdate(XHCI_PORT_SHADOW *shadow,
                           ULONG portsc,
                           ULONG speedClass,
                           ULONG *ackBits);

/*
 * Report a shadow as the hub class sees it.
 *
 * Note the one deliberate untruth: a connected port is always reported High
 * Speed, because usbport's EHCI model bugchecks on a non-HS device under a root
 * hub (Phase 5 task 7; docs/usb-xhci-info/usbport-miniport-abi.md section 8). `shadow->Speed`
 * is unaffected and remains the speed the hardware must be programmed from.
 * IRQL: any.
 */
VOID XhciPortShadowReport(const XHCI_PORT_SHADOW *shadow,
                          ULONG *portStatus,
                          ULONG *portChange);

/* Clear one latched change, the only thing that ever does. Returns 1 when the
 * bit was set. IRQL: any. */
ULONG XhciPortShadowClearChange(XHCI_PORT_SHADOW *shadow, ULONG changeBit);

/*
 * Latch a hub-class change that no PORTSC bit produced.
 *
 * There is exactly one such change and it is the reset watchdog's: a reset whose
 * PRC never arrived is still a reset usbhub is waiting to be told about, and the
 * port status beside it (PED clear) is what says it failed. Masked to
 * XHCI_HUB_C_PORT_MASK for the same reason the update path is - a change
 * reported outside it is invisible to the status-change scan.
 *
 * IRQL: any.
 */
VOID XhciPortShadowLatchChange(XHCI_PORT_SHADOW *shadow, ULONG changeBits);

/*
 * Arm an asynchronous operation on one port, or refuse because another is
 * already in flight there.
 *
 * Returns the new generation, or 0 when the port already has an operation armed.
 * `operation` is an XHCI_PORT_OP_*. The generation is advanced on every arm and
 * never reused within a start, and 0 is skipped on wrap so that a zeroed shadow
 * matches no live timer context.
 *
 * IRQL: any. Caller holds the controller lock.
 */
ULONG XhciPortShadowArm(XHCI_PORT_SHADOW *shadow, ULONG operation);

/*
 * Claim the armed operation, if it is still the one the caller was armed with.
 *
 * Returns 1 having disarmed the port, or 0 when the generation has moved on, the
 * port was disarmed by something else, or a different operation is armed - the
 * three ordinary ways an uncancellable callback arrives with nothing to do.
 *
 * IRQL: any. Caller holds the controller lock.
 */
ULONG XhciPortShadowClaim(XHCI_PORT_SHADOW *shadow,
                          ULONG operation,
                          ULONG generation);

/*
 * Record that a Port Power change has been issued and not yet confirmed, so that
 * nothing writes this port's PORTSC again until it lands. `wanted` is 0 or 1.
 * IRQL: any. Caller holds the controller lock.
 */
VOID XhciPortShadowPpArm(XHCI_PORT_SHADOW *shadow, ULONG wanted);

/*
 * May this port's PORTSC be written, given a *freshly read* value?
 *
 * Answers 1 when no Port Power change is outstanding, or when this reading shows
 * the outstanding one has reached its target - in which case it also clears the
 * wait, so the confirmation the specification asks for happens exactly once and
 * on real evidence. Answers 0 while a change is still in flight.
 *
 * IRQL: any. Caller holds the controller lock.
 */
ULONG XhciPortShadowPpSettled(XHCI_PORT_SHADOW *shadow, ULONG portsc);

/*
 * Age an unconfirmed Port Power change against the health poll's clock, and say
 * whether it has now been outstanding for `limitMs` - at which point the caller
 * gives up waiting and lets the port be written again. A port whose PP never
 * arrives is broken, and refusing every operation on it for ever is worse than
 * one write made without the confirmation. Returns 1 once, having cleared the
 * wait.
 *
 * `now` is a `XHCI_EXTENSION.PollClockMs` reading. **In milliseconds since task
 * 13-R.3.5, where both of these were counts of polls** - see src/xhci_port.c.
 *
 * IRQL: any. Caller holds the controller lock.
 */
ULONG XhciPortShadowPpAge(XHCI_PORT_SHADOW *shadow, ULONG now, ULONG limitMs);

/*
 * Age the armed operation against the health poll's clock, and say whether it
 * has now been armed for `limitMs`.
 *
 * Returns 1 **once per arming**, so one lost timer produces one retirement
 * rather than one per poll from then on. An unarmed port disarms the age and
 * answers 0; the age is disarmed by the arm too, so it always describes the
 * operation currently in flight.
 *
 * IRQL: any. Caller holds the controller lock.
 */
ULONG XhciPortShadowAge(XHCI_PORT_SHADOW *shadow, ULONG now, ULONG limitMs);

/*
 * Retire whatever is armed on this port without claiming it, so that no timer
 * from before this point can act. Returns 1 when something was armed.
 *
 * The lifecycle's half of the mechanism (Phase 5 task 6): a stop, a suspend and
 * a reinitialization all pass through here, and after it every outstanding
 * callback is stale by generation and returns before touching a register.
 *
 * IRQL: any. Caller holds the controller lock.
 */
ULONG XhciPortShadowDisarm(XHCI_PORT_SHADOW *shadow);

/* ------------------------------------------------------------------ */
/* Rings and TRBs (src/xhci_ring.c)                                    */
/* ------------------------------------------------------------------ */

#define XHCI_RING_OK            0
#define XHCI_RING_BAD_PARAM     1
#define XHCI_RING_FULL          2
#define XHCI_RING_EMPTY         3
#define XHCI_RING_NOT_ON_RING   4
#define XHCI_RING_BAD_CHAIN     5   /* Chain flags do not describe one TD */
#define XHCI_RING_NOT_COMPLETE  6   /* the xHC may still own TRBs of this TD */
#define XHCI_RING_NOT_TD_HEAD   7   /* not a position execution may start at  */
#define XHCI_RING_BAD_COMPLETION 8  /* code is invalid for this ring's events */

/*
 * A producer ring (command ring, and every transfer ring from Phase 6). One
 * segment whose last TRB is a permanent Link TRB back to the start with TC
 * set, so the cycle bit toggles once per lap.
 *
 * Base is volatile because this is cached common-buffer memory: ordering rests
 * on the compiler not reordering the stores, with each TRB's Cycle Bit
 * published last (AGENTS.md; docs/contributing/design/04-controller-common-buffer.md
 * section 6). Dequeue is *software's* view of what the hardware has consumed,
 * advanced from completion events - the hardware never writes it.
 */
/*
 * What kind of work this ring carries. It changes nothing about the geometry
 * and everything about what a completion event *means*, because the spec's
 * error rules are keyed on it - so it is fixed when the ring is created rather
 * than restated at each event, where two call sites could disagree about one
 * ring.
 *
 *   - COMMAND: the command ring. A failed command reports a Completion Code
 *     and the ring carries on; there is no endpoint to halt. Only an abort or
 *     a stop (codes 24-25) hands the ring back to software.
 *   - ENDPOINT: control, bulk or interrupt. "All Transfer Ring error
 *     conditions force the state of the associated endpoint to Halted and
 *     require system software intervention to recover" (p.176).
 *   - ISOCH: isochronous, and it is the exception to that sentence. "An isoch
 *     end point never halts because there is no handshake to report a halt
 *     condition ... an isoch pipe is not halted in an error case. If an error
 *     is detected, the xHC shall continue to process the data associated with
 *     the next ESIT" (p.177), restated at 4.10.2.8 p.184: "an Isoch endpoint
 *     shall not halt due to a Data Transaction error, but instead shall
 *     advance to the next Isoch TD and attempt to execute it during the next
 *     ESIT". Resetting one of these after an error would reposition a pipe the
 *     controller is still running.
 */
#define XHCI_RING_KIND_COMMAND   0
#define XHCI_RING_KIND_ENDPOINT  1
#define XHCI_RING_KIND_ISOCH     2

typedef struct _XHCI_RING {
    volatile XHCI_TRB *Base;
    ULONG BasePA;
    ULONG Trbs;             /* TRBs in the segment, Link TRB included */
    ULONG Enqueue;          /* index of the next TRB to write         */
    ULONG Dequeue;          /* index the hardware is believed to be at */
    ULONG Cycle;            /* producer cycle state, 0 or 1           */
    ULONG Kind;             /* XHCI_RING_KIND_*                       */
} XHCI_RING, *PXHCI_RING;

/*
 * The event ring, which only the hardware writes. Ccs is the consumer cycle
 * state; an event whose Cycle Bit differs from it has not been produced yet.
 * There is one segment, so the dequeue pointer wraps rather than walking ERST
 * entries, and Ccs toggles on each wrap.
 */
typedef struct _XHCI_EVENT_RING {
    volatile XHCI_TRB *Base;
    ULONG BasePA;
    ULONG Trbs;
    ULONG Dequeue;
    ULONG Ccs;
} XHCI_EVENT_RING, *PXHCI_EVENT_RING;

/* TRB builders. Each fills a caller-owned template; the cycle bit is not the
 * builder's business - XhciRingEnqueue stamps it. IRQL: any. */
VOID XhciTrbClear(XHCI_TRB *trb);
VOID XhciTrbNoOpCommand(XHCI_TRB *trb);
VOID XhciTrbLink(XHCI_TRB *trb, ULONG segmentPA, ULONG toggleCycle, ULONG cycle);

/*
 * The four device-lifecycle commands Phase 6 issues (docs/usb-xhci-info/xhci-data-structures.md
 * section 7, "Command TRBs"). Each returns XHCI_RING_OK, or XHCI_RING_BAD_PARAM
 * having written nothing when an argument does not fit its field - a Slot ID
 * above 255, an Input Context pointer with low bits set. Truncating instead
 * would issue a command against a different slot, or point the xHC at a context
 * that is not one.
 *
 * DW0/DW1 are the Input Context Pointer where the command takes one; the high
 * DWORD is always zero (no 64-bit DMA) and is written explicitly rather than
 * left to the caller's template.
 *
 * IRQL: any.
 */
ULONG XhciTrbEnableSlot(XHCI_TRB *trb, ULONG slotType);
ULONG XhciTrbDisableSlot(XHCI_TRB *trb, ULONG slotId);
/* Reset Device (type 17): Slot ID and nothing else. It carries no Input Context
 * - "the Reset Device Command TRB (section 6.4.3.10) does not reference an Input
 * Context" (4.6.11 p.130) - so DW0/DW1 stay 0 like Disable Slot's. */
ULONG XhciTrbResetDevice(XHCI_TRB *trb, ULONG slotId);
ULONG XhciTrbAddressDevice(XHCI_TRB *trb,
                           ULONG slotId,
                           ULONG inputContextPA,
                           ULONG blockSetAddress);
ULONG XhciTrbEvaluateContext(XHCI_TRB *trb,
                             ULONG slotId,
                             ULONG inputContextPA);
/* `deconfigure` sets DC, which per spec 6.4.3.5 makes the xHC ignore the Input
 * Context pointer - so that is the one case where a 0 pointer is accepted
 * rather than refused. */
ULONG XhciTrbConfigureEndpoint(XHCI_TRB *trb,
                               ULONG slotId,
                               ULONG inputContextPA,
                               ULONG deconfigure);

/*
 * The three per-endpoint commands (batch 7a-B). Each carries a Slot ID and an
 * Endpoint ID (the DCI, field `20:16` - `docs/usb-xhci-info/xhci-data-structures.md` section
 * 5's command table), and each refuses rather than truncates for the reasons
 * stated above the builders in src/xhci_ring.c.
 *
 * `suspend` is Stop Endpoint's SP flag and `preserveState` is Reset Endpoint's
 * TSP. This driver writes both as 0 and the arguments exist so a caller has to
 * say so: SP is for a controller entering a low-power state (Phase 11's since
 * the phase split), and
 * TSP = 1 is the Soft Retry of 4.6.8.1, which deliberately does *not* reset the
 * Data Toggle - the opposite of what a reset-pipe needs.
 *
 * `dequeuePA` must be 16-byte aligned and `dcs` is the Dequeue Cycle State that
 * goes in bit 0. It is not a constant: it is "the state of the xHCI CCS flag for
 * the TRB pointed to by the TR Dequeue Pointer field" (spec 4.6.10 p.127) and
 * must come from XhciRingDequeueCycle - see "DCS is not a constant" in
 * docs/usb-xhci-info/xhci-data-structures.md.
 */
ULONG XhciTrbStopEndpoint(XHCI_TRB *trb,
                          ULONG slotId,
                          ULONG dci,
                          ULONG suspend);
ULONG XhciTrbResetEndpoint(XHCI_TRB *trb,
                           ULONG slotId,
                           ULONG dci,
                           ULONG preserveState);
ULONG XhciTrbSetTrDequeue(XHCI_TRB *trb,
                          ULONG slotId,
                          ULONG dci,
                          ULONG dequeuePA,
                          ULONG dcs);

/*
 * Ring operations. IRQL: any - all of them are computation plus stores into
 * caller-supplied common-buffer memory. None of them rings a doorbell: the
 * caller does that, once, after the TRB is published.
 */
ULONG XhciRingInit(PXHCI_RING ring,
                   volatile XHCI_TRB *base,
                   ULONG basePA,
                   ULONG trbs,
                   ULONG kind);
/*
 * Is a segment of `trbs` TRBs at `basePA` a legal ring segment - Table 6-1's
 * three rules (at most 64 KB, `alignment`-byte aligned, and not *spanning* a
 * 64 KB boundary, which is a separate rule from being 64 KB or smaller) plus
 * this project's no-64-bit-DMA one? Returns 1 when it is.
 *
 * Exported as well as used by XhciRingInit because `OpenEndpoint` has to ask it
 * of a buffer usbport allocated rather than of one this driver carved
 * (task 6-B.1). IRQL: any.
 */
ULONG XhciSegmentUsable(ULONG basePA, ULONG trbs, ULONG alignment);
ULONG XhciRingCapacity(const XHCI_RING *ring);
ULONG XhciRingFree(const XHCI_RING *ring);
ULONG XhciRingHasRoom(const XHCI_RING *ring, ULONG trbs);
ULONG XhciRingTrbPA(const XHCI_RING *ring, ULONG index);
ULONG XhciRingIndexFromPA(const XHCI_RING *ring, ULONG pa, ULONG *index);
/*
 * Enqueue a whole Transfer Descriptor - `count` TRBs the hardware must see
 * complete or not at all. Fails with XHCI_RING_FULL, having written nothing,
 * if the whole TD does not fit, and with XHCI_RING_BAD_CHAIN if the templates'
 * Chain flags do not describe one TD. The head TRB's Cycle Bit is the single
 * publishing store, written after every other TRB and after any Link TRB the
 * TD spans; the caller rings the doorbell after this returns.
 *
 * *firstTrbPA receives the **head** TRB's physical address when non-NULL. That
 * is the TD's identity - the key a caller keys its own per-TD bookkeeping on -
 * and NOT, in general, the address a completion event reports:
 *
 *   - A Command Completion Event reports the address of the command TRB (spec
 *     6.4.2.2). Commands are single-TRB TDs, so there the two coincide, and
 *     the Phase 4 command engine may compare them directly.
 *   - A Transfer Event reports "the address of the TRB that generated this
 *     event" (Table 6-37): normally the TD's last TRB via IOC, but an earlier
 *     one when a short packet with ISP or an error occurred there (4.11.3.1).
 *     Comparing that against the head would reject a legitimate completion.
 *     Resolve it with XhciRingTdBounds instead, and retire with
 *     XhciRingRetireTd.
 *   - With ED = 1 the field is not an address at all, and for an error that
 *     cannot be attributed to a TRB (Ring Overrun/Underrun) the xHC sets it to
 *     zero and "software shall treat it as invalid" (4.11.3.1). Check
 *     XHCI_EVENT_IS_EVENT_DATA first; a zero or foreign address is rejected by
 *     the resolvers below rather than mistaken for a ring position.
 */
ULONG XhciRingEnqueueTd(PXHCI_RING ring,
                        const XHCI_TRB *trbs,
                        ULONG count,
                        ULONG *firstTrbPA);
/* The single-TRB case: every command, and any transfer that fits in one TRB. */
ULONG XhciRingEnqueue(PXHCI_RING ring, const XHCI_TRB *trb, ULONG *trbPA);

/*
 * Where a group landed. The two indices are what a caller's own bookkeeping
 * keys on: an event resolves to an index, and "is this index inside the range
 * this transfer occupies" is the question that finds the owner. The PA is the
 * same identity in the hardware's units, kept because the command engine
 * already matches on it.
 */
typedef struct _XHCI_TD_GROUP_PLACEMENT {
    ULONG FirstIndex;
    ULONG FirstTrbPA;
    ULONG LastIndex;
    ULONG TrbCount;
} XHCI_TD_GROUP_PLACEMENT, *PXHCI_TD_GROUP_PLACEMENT;

/*
 * Enqueue several consecutive TDs as one publication. `tdLengths[0..tdCount-1]`
 * partitions the `count` TRBs into TDs, and the Chain flags supplied must agree
 * with that partition - set in every TRB of a TD except its last.
 *
 * XhciRingEnqueueTd is the tdCount == 1 case of this, and everything that call
 * documents holds here: XHCI_RING_FULL having written nothing, XHCI_RING_BAD_CHAIN
 * for flags that do not describe the declared TDs, the head TRB's Cycle Bit as
 * the single publishing store, and the caller ringing the doorbell afterwards.
 *
 * The reason the group exists is that a USB control transfer is **not one TD**:
 * "Control transfers require two or three TDs to define them: a Setup Stage TD
 * followed by an Status Stage TD, if a data stage is required for the transfer
 * an optional Data Stage TD will reside between" (spec 6.4.1.2, p.430).
 * Publishing those TD by TD would let the xHC begin a control transfer whose
 * Status Stage TRB does not exist yet.
 */
ULONG XhciRingEnqueueTdGroup(PXHCI_RING ring,
                             const XHCI_TRB *trbs,
                             ULONG count,
                             const ULONG *tdLengths,
                             ULONG tdCount,
                             PXHCI_TD_GROUP_PLACEMENT placement);

/*
 * The next producer index, with the Link TRB skipped rather than rested on.
 * Exported because a caller walking its own TRBs across a wrap must step the
 * way the ring does; open-coding `index + 1` is how a walk lands on the Link
 * TRB and reads a segment pointer as a transfer length.
 */
ULONG XhciRingNextIndex(const XHCI_RING *ring, ULONG index);

/*
 * Sum the TRB Transfer Length fields from `fromIndex` to `toIndex` inclusive,
 * skipping the Link TRB. This is the spec's own arithmetic for how many bytes a
 * multi-TRB TD moved: "the total number of received bytes for a Short Packet TD
 * is the sum of the TRB Transfer Length fields in all Transfer TRBs up to and
 * including the one that generated the Short Packet Event, minus the residue
 * value of the TRB Transfer Length field in the Short Packet Event" (p.175).
 *
 * It exists because "requested - residual" is **wrong for a multi-TRB TD**:
 * "For multi-TRB TDs, if ED = `0`, the TRB Transfer Length only reflects the
 * number of bytes transferred for the buffer associated with the Transfer TRB
 * pointed to by the Transfer Event, not the total bytes transferred for the TD"
 * (Table 6-39 note, p.441).
 *
 * Both ends must be outstanding TRBs of this ring and `toIndex` must be at or
 * after `fromIndex` in dequeue order, or XHCI_RING_NOT_ON_RING - a sum taken
 * across a retired TRB is a previous lap's leftovers.
 */
ULONG XhciRingSumTrbLengths(const XHCI_RING *ring,
                            ULONG fromIndex,
                            ULONG toIndex,
                            ULONG *bytes);

/*
 * Resolve any outstanding TRB index to the TD that owns it, by following the
 * Chain flags already on the ring - the same encoding the hardware uses, so no
 * side table has to be kept in step with it. Either output may be NULL.
 * Returns XHCI_RING_NOT_ON_RING for an index that is not outstanding.
 */
ULONG XhciRingTdBounds(const XHCI_RING *ring,
                       ULONG index,
                       ULONG *headIndex,
                       ULONG *tailIndex);

/*
 * What a completion event means, as **two independent facts**. They were one
 * three-valued action until review found the combination that cannot express:
 * a halting error on a control/bulk/interrupt TD's *last* TRB both relinquishes
 * that TD's TRBs and leaves the endpoint halted. Reporting only "retire" loses
 * the reset; reporting only "recover" strands the slots.
 *
 * Neither field says anything about USBD status or whether the transfer's data
 * arrived - that is the caller's business, from the completion code.
 */
typedef struct _XHCI_TD_COMPLETION {
    ULONG ReportedIndex;        /* ring index the event pointed at        */
    ULONG HeadIndex;            /* first TRB of the owning TD             */
    ULONG TailIndex;            /* last TRB of the owning TD              */
    ULONG IsTail;               /* the event named the TD's last TRB      */

    /* May software reuse this TD's TRBs now? Set only when the event named
     * the TD's last TRB, whatever the completion code says. */
    ULONG CanRetire;

    /* Does the endpoint (or command ring) need software intervention before
     * it will run again - Reset Endpoint and/or Set TR Dequeue Pointer? */
    ULONG NeedsRecovery;
} XHCI_TD_COMPLETION, *PXHCI_TD_COMPLETION;

/*
 * Classify one completion event against the ring. Pure - changes nothing.
 * `completionCode` is the event's Completion Code (DW2 bits 31:24).
 *
 * One rule decides slot ownership, and it is positional, not code-based:
 *
 *   - "Software shall not interpret an error Event as indicating that the TD
 *     that it is associated with is 'complete' (i.e. ownership of all the TRBs
 *     of the TD have been relinquished by the xHC), unless the TRB Pointer
 *     field of the error Transfer Event references the last TRB of the TD"
 *     (4.11.7 p.214). Short Packet gets the identical sentence of its own at
 *     4.10.1.1.2 p.175, and a *successful* intermediate event is covered by
 *     the reason behind both: the spec offers intermediate IOC events as a way
 *     for software to "update its Dequeue Pointer and reuse the TRBs that have
 *     been consumed by the xHC", so such an event says the xHC has passed that
 *     TRB - not that it has finished the TD.
 *
 * So `CanRetire` is positional: set when the event named the TD's last TRB.
 * The completion code and the ring's Kind decide `NeedsRecovery` instead, and
 * the two are computed independently because a tail halting error is both:
 *
 *   - Short Packet and Missed Service Error let the controller keep going by
 *     itself - it "shall advance to the first TRB of the next TD or the Enqueue
 *     Pointer, whichever is encountered first" (p.172). Mid-TD, software
 *     neither retires nor recovers: it waits, because the tail event is still
 *     coming. For a short packet the spec promises it outright ("two events
 *     shall be generated ... a second for the last TRB with the IOC flag set",
 *     p.175), and for a skipped isoch TD the resynchronization rule does ("the
 *     xHC shall not drop Events associated with TRBs ... if IOC = '1' in an
 *     Event Data or Normal TRB then it returns Missed Service Error", p.201).
 *     Both hold only because this driver sets IOC on every TD's last TRB - with
 *     control transfers as the deliberate exception, where the spec puts IOC on
 *     the Status Stage TRB alone (p.430) and the xHC "shall advance to the
 *     Status Stage TD" after a short packet (p.433), so the group's own last
 *     TRB is the tail that arrives.
 *     Retiring early instead is exactly what p.188 forbids: the xHC "may
 *     automatically advance to the next TD", yet if the event "does not point
 *     to last TRB ... software will have to wait until the next IOC flag is
 *     encountered by the endpoint before it can reclaim" the TD.
 *   - Any other error on an XHCI_RING_KIND_ENDPOINT ring halts the endpoint:
 *     "All Transfer Ring error conditions force the state of the associated
 *     endpoint to Halted and require system software intervention to recover"
 *     (p.176). That is true wherever the event landed - so a tail error sets
 *     **both** flags, and a mid-TD one sets only NeedsRecovery, because "the
 *     xHC shall stop on the TRB in error, the endpoint shall be halted, and
 *     software shall use a Set TR Dequeue Pointer Command to advance the
 *     Transfer Ring to the next TD" (p.172).
 *   - On an XHCI_RING_KIND_ISOCH ring no error *halts* anything (p.177, p.184),
 *     so an error behaves like Missed Service: defer mid-TD, retire at the tail,
 *     recover nothing. **TRB Error is the one *error* that still asks for
 *     recovery** (the stopped family below reaches every transfer-ring kind and
 *     is not an error), because it halts no endpoint of any type - 4.8.3
 *     p.149 puts a Running endpoint in the *Error* state instead, from which
 *     only a Set TR Dequeue Pointer returns it, and the exemption p.177 grants
 *     an isoch pipe is from Halted alone.
 *   - On an XHCI_RING_KIND_COMMAND ring a failed command is just a result. The
 *     ring keeps running and there is no endpoint, so an error retires its
 *     (always single-TRB) TD and asks for nothing.
 *   - Codes 24-25 stop a command ring; codes 26-28 stop a transfer ring.
 *     Software now owns that ring and chooses the next dequeue position. These
 *     codes never retire a TD. A Command Ring Stopped event's pointer is a
 *     *dequeue position*, not a completed command, so the command engine reads
 *     it directly rather than through this call.
 *   - Table 6-90 assigns many codes to only one event family. This call returns
 *     XHCI_RING_BAD_COMPLETION for a code that cannot describe this ring, and
 *     for conditions such as Ring Underrun/Overrun whose event has no valid TRB
 *     pointer. Unknown vendor information (224-255) is treated as Success;
 *     unknown vendor errors (192-223) are treated as Undefined Error.
 *
 * A deferred TD is not leaked if its tail event never arrives. Some xHCs skip
 * it: without the Contiguous Frame ID Capability one "may not generate a
 * Missed Service Error Transfer Event for every ESIT missed" (p.187), and an
 * Event Ring full condition suppresses them too. XhciRingRetireTd moves the
 * dequeue pointer *past the retired TD's tail*, not by one TD, so the next
 * event that does name a tail reclaims every deferred TD ahead of it in the
 * same step. A caller tracking TDs of its own should therefore complete
 * everything it holds below the new dequeue index, not just the TD it matched.
 *
 * Recovery has its own sequence - Reset Endpoint, then Set TR Dequeue Pointer,
 * then ring the doorbell (p.116) - and "software is responsible for cleaning up
 * any partially completed transfers" (p.118). Whatever position that command
 * programs into the hardware must be the value XhciRingDequeuePA reports, with
 * the cycle XhciRingDequeueCycle reports, or the two dequeue pointers diverge.
 * When both flags are set, retire first: the dequeue pointer then already sits
 * on the next TD, which is where the spec sentence above says to point it.
 *
 * `reportedTrbPA` must be a ring address: check XHCI_EVENT_IS_EVENT_DATA
 * first, because with ED = 1 the field is not one. A pointer that is not an
 * outstanding TRB of this ring - zero, a foreign address, the Link TRB, or a
 * TRB already retired - returns XHCI_RING_NOT_ON_RING. That is the expected
 * answer, not an error, for the trailing events one TD can legitimately
 * produce.
 */
/*
 * Is `completionCode` one a Transfer or Command Event may legally carry on a
 * ring of this Kind? Exported because the isochronous event path does its own
 * per-packet matching and never calls `XhciRingClassifyEvent`, so without this
 * it decoded through the shared decoder - which accepts Stall Error and Invalid
 * Stream ID, neither of which is legal on an isoch ring (an isoch pipe has no
 * handshake to stall with, and this driver implements no streams). The list is
 * the ring layer's, and there is one of it: a second copy in the transfer layer
 * is how the two drift.
 */
ULONG XhciRingCompletionCodeValid(ULONG kind, ULONG completionCode);

ULONG XhciRingClassifyEvent(const XHCI_RING *ring,
                            ULONG reportedTrbPA,
                            ULONG completionCode,
                            PXHCI_TD_COMPLETION completion);

/*
 * Advance the dequeue pointer past a classified TD. Refuses with
 * XHCI_RING_NOT_COMPLETE unless the classification set CanRetire, so the one
 * mistake that silently corrupts a ring - reclaiming TRBs the controller is
 * still executing - cannot be made by forgetting to check. NeedsRecovery is
 * not consulted: the two are independent, and a tail halting error needs both
 * this call and the recovery sequence.
 */
ULONG XhciRingRetireTd(PXHCI_RING ring, const XHCI_TD_COMPLETION *completion);

/*
 * Advance the dequeue pointer past a TD the **caller** knows the xHC has
 * finished with, though the event did not name the TD's last TRB. The one case
 * is a Short Packet Event mid-TD on a TD that is entirely data: the xHC "shall
 * advance to the first TRB of the next TD or the Enqueue Pointer ... whichever
 * is encountered first" (4.11.5.2 p.210), so it is no longer executing this TD,
 * and 4.10.1.1.2 p.175's second event repeats the first's length rather than
 * adding to it.
 *
 * This is a **deliberate departure** from the p.175 sentence that says software
 * "shall not interpret a Short Packet Event as indicating that the TD ... is
 * complete, unless the TRB Pointer field ... references the last TRB of the TD",
 * taken because QEMU's xHC emits one event where p.175 mandates two. It is
 * separate from XhciRingRetireTd so that the positional rule stays the default
 * and the departure is visible at every call site; the shape test that decides
 * when it applies belongs to the transfer layer, which knows a Normal TD from a
 * control transfer's Data Stage. See docs/contributing/implementation-invariants.md,
 * "Completion Matching".
 *
 * Refuses with XHCI_RING_NOT_COMPLETE for any code other than Short Packet or
 * any ring that is not XHCI_RING_KIND_ENDPOINT, and with XHCI_RING_NOT_ON_RING
 * if the classification has gone stale.
 */
ULONG XhciRingRetireAdvancedTd(PXHCI_RING ring,
                               const XHCI_TD_COMPLETION *completion,
                               ULONG completionCode);

/*
 * Task 9-A.1. Reclaim a whole isochronous **group** - every TRB up to and
 * including `tailIndex` - on the caller's word that every one of its TDs has
 * been answered.
 *
 * A third entry point rather than a widening of either above, for the same
 * reason `XhciRingRetireAdvancedTd` is a second one: the knowledge is not the
 * ring layer's. What licenses this is that the transfer layer has seen a
 * Transfer Event for every packet of the request, and "one event per packet" is
 * a property of how this driver built the group (IOC on the last TRB of each
 * Isoch TD), which the ring cannot read off the TRBs.
 *
 * It exists because the positional rule alone does not terminate on an isoch
 * pipe. Every *successful* TD's event names its own tail, so the group's last
 * event names the group's last TRB and the ordinary retire fires. But an error
 * or a Missed Service Error names "the TRB that was missed" (4.10.3.2 p.187),
 * which on a final packet split across two fragments is not the tail - and no
 * later event is coming, because the controller has already advanced to the next
 * ESIT. Waiting for a sweep by a later group is what a bulk endpoint does; an
 * isoch stream that stalls a frame behind for ever is a dead stream.
 *
 * Refuses with XHCI_RING_NOT_COMPLETE on any ring that is not
 * XHCI_RING_KIND_ISOCH, and with XHCI_RING_NOT_ON_RING if `tailIndex` is no
 * longer outstanding - the same staleness check the other two make, and for the
 * same reason: retiring on a stale position moves the dequeue pointer backwards.
 */
ULONG XhciRingRetireIsoGroup(PXHCI_RING ring, ULONG tailIndex);

/*
 * Place the dequeue pointer explicitly. This is the RECOVER path: after Reset
 * Endpoint / Stop Endpoint, software decides where execution resumes and must
 * program the same address into the Set TR Dequeue Pointer command.
 *
 * Only two kinds of position are accepted, because the dequeue pointer is what
 * every free-space and ownership calculation is measured from:
 *
 *   - the current Enqueue position, which discards all pending work and leaves
 *     the ring empty; or
 *   - an outstanding TRB that is the **first TRB of its TD**. "The xHC shall
 *     assume that the modified Dequeue Pointer references the first TRB of a
 *     TD" (spec p.172), and resuming mid-TD would also make the Chain-flag
 *     ownership walk report a head that is not one.
 *
 * Anything else is refused without moving anything: an address behind the
 * dequeue pointer would resurrect reclaimed slots as outstanding, and on an
 * empty ring any position other than Enqueue would invent outstanding work out
 * of stale TRBs - either way the free count shrinks permanently and the ring
 * eventually reports full forever.
 */
ULONG XhciRingSetDequeue(PXHCI_RING ring, ULONG dequeuePA);

/*
 * Overwrite one outstanding TRB with a No Op transfer TRB (type 8), preserving
 * the Cycle Bit the slot already carries so the position stays produced.
 *
 * This is how a cancelled TD is taken out of a ring that still has surviving
 * work behind it, and the spec offers it as the intended use: "While the
 * endpoint is stopped, software may add, delete, or otherwise rearrange TDs on
 * an associated Transfer Ring ... or to 'abort' one or more TDs by removing them
 * from the ring" (4.6.9 p.119). Each rewritten TRB becomes a single-TRB No Op TD
 * - Chain and IOC are cleared - so the xHC walks past it and generates nothing.
 *
 * **Only legal while the endpoint is not executing the ring**, which in this
 * driver means after a Stop Endpoint or Reset Endpoint command has completed.
 * That is the caller's obligation and cannot be checked here: this layer knows
 * nothing about Endpoint Contexts.
 *
 * Refuses the Link TRB and any index that is not outstanding, because both would
 * corrupt the ring rather than edit it.
 */
ULONG XhciRingNoOpAt(PXHCI_RING ring, ULONG index);

/* The physical address the dequeue pointer currently sits at - the value a Set
 * TR Dequeue Pointer command must carry so the two pointers agree. 16-byte
 * aligned, so the command's DCS and SCT bits are OR'd into the low bits. */
ULONG XhciRingDequeuePA(const XHCI_RING *ring);

/*
 * The Dequeue Cycle State to pair with that address: bit 0 of the Set TR
 * Dequeue Pointer command's New TR Dequeue Pointer field. Not a constant -
 * "this bit identifies the value of the xHC Consumer Cycle State (CCS) flag
 * for the TRB referenced by the TR Dequeue Pointer" (Table 6-67, p.455; the
 * same requirement in prose at 4.6.10 p.127). A ring that has wrapped an odd
 * number of times sits on TRBs written with the opposite cycle, so hard-coding
 * this to 0 or 1 makes the controller read a stale lap as unproduced work - it
 * would stop dead at a TD that is really there, or run off into TRBs that are
 * not.
 *
 * Derived, not stored: outstanding TRBs occupy [Dequeue, Enqueue), so exactly
 * one Link crossing separates the two pointers when Dequeue > Enqueue and none
 * otherwise. There is nothing to keep in step, and every path that moves either
 * pointer - retire, explicit placement, enqueue wrap - is correct by
 * construction. On an empty ring it is the producer cycle, which is what a
 * flush to the enqueue position needs.
 */
ULONG XhciRingDequeueCycle(const XHCI_RING *ring);

ULONG XhciEventRingInit(PXHCI_EVENT_RING ring,
                        volatile XHCI_TRB *base,
                        ULONG basePA,
                        ULONG trbs);
ULONG XhciEventRingPending(const XHCI_EVENT_RING *ring);
/* Copy the event at the dequeue pointer out and advance, or XHCI_RING_EMPTY
 * when the Cycle Bit says the hardware has produced nothing more. */
ULONG XhciEventRingDequeue(PXHCI_EVENT_RING ring, XHCI_TRB *out);
ULONG XhciEventRingDequeuePA(const XHCI_EVENT_RING *ring);
/* The value to write to IR[0].ERDP. `ehb` = 1 only on the final write after
 * the ring reads empty; an intermediate write passes 0, which *preserves*
 * Event Handler Busy because the bit is RW1C. */
ULONG XhciEventRingErdpValue(const XHCI_EVENT_RING *ring, ULONG ehb);

/* ------------------------------------------------------------------ */
/* The three extensions usbport allocates for the miniport             */
/* ------------------------------------------------------------------ */

/*
 * The miniport never allocates any of these: it declares their sizes in the
 * registration packet and usbport carves them out of its own extensions,
 * zeroing each one before first use. The first argument of every callback is
 * the device extension; endpoint and transfer callbacks carry the other two.
 *
 * Phase 3 needs only enough state to prove the callbacks arrive on the object
 * they claim to. Phase 4 grows XHCI_EXTENSION into the real controller state
 * (register base, ring cursors, command generation, port shadows); Phase 6
 * grows the endpoint and transfer extensions.
 *
 * Signature/TrailingSignature bracket the whole extension, so a callback can
 * tell "usbport handed me my extension" from "usbport handed me something
 * else, or allocated fewer bytes than MiniPortExtensionSize asked for" -
 * exactly the failure a wrong packet layout produces, and one that otherwise
 * looks like a random crash several callbacks later.
 */
#define XHCI_EXTENSION_SIGNATURE  0x58484349UL  /* 'XHCI' */
#define XHCI_EXTENSION_TRAILING   0x444E4523UL  /* '#END' */
#define XHCI_ENDPOINT_SIGNATURE   0x58484550UL  /* 'XHEP' */
#define XHCI_TRANSFER_SIGNATURE   0x58485452UL  /* 'XHTR' */

/* XHCI_EXTENSION.Flags */
#define XHCI_EXT_FLAG_STARTED     0x00000001UL
#define XHCI_EXT_FLAG_INTERRUPTS  0x00000002UL
#define XHCI_EXT_FLAG_SUSPENDED   0x00000004UL
/* Set once xhciInit has programmed DCBAAP, the command ring and the event
 * ring. Nothing may touch those structures - or the registers that point at
 * them - unless it is set (src/xhci_init.c). */
#define XHCI_EXT_FLAG_INITIALIZED 0x00000008UL
/*
 * "This driver has USBCMD.RUN set, so the xHC may be executing and writing into
 * the common buffer." Set immediately *before* the R/S write rather than after
 * the controller confirms it - the hazard begins at the write, whether or not
 * HCHalted is ever observed to clear - and cleared by the two places that clear
 * R/S, the halt step and XhciQuiesceController.
 *
 * The distinction is load-bearing because the quiesce is gated on it: a start
 * that refused during the preflight wrote nothing at all and must be left that
 * way, while a start that set R/S and then failed still has a controller to
 * stop (src/xhci_init.c).
 */
#define XHCI_EXT_FLAG_RUNNING     0x00000010UL
/*
 * "The xHC is executing and **this driver is not what started it**" - a valid
 * USBSTS read with HCHalted clear at a point where this driver has written no
 * R/S (5.4.2 p.363: HCH is 0 whenever R/S is 1).
 *
 * A second flag rather than reusing XHCI_EXT_FLAG_RUNNING, because that bit is
 * read for **two** different questions and only one of them wants this case.
 * XhciQuiesceController asks "may the xHC be executing", and the answer here is
 * yes - the common buffer is already programmed and usbport reclaims it the
 * moment a failed start is reported, so the halt and the Bus Master fallback are
 * both owed. `xhciUnpowerPorts` asks "is the power on these ports mine to take
 * away", and the answer here is **no**: this driver never wrote R/S and never
 * powered a port, so a teardown must leave them exactly as it found them.
 * Publishing RUNNING for this case answered the first question correctly and the
 * second one wrongly, which is how the port pass started writing PORTSC on a
 * controller it had claimed nothing on.
 *
 * Cleared wherever RUNNING is: whatever proves the controller stopped retires
 * both. Added by the second-reader review.
 */
#define XHCI_EXT_FLAG_HW_RUNNING  0x00000040UL
/*
 * usbport's root-hub notification gate is **open**: a port change may ask for a
 * root-hub poll. `RH_DisableIrq` closes it, `RH_EnableIrq` opens it, and it is a
 * pure software gate - it never touches `IMAN.IE`, which gates the whole
 * interrupter and would silence transfer completions with it
 * (`docs/usb-xhci-info/usbport-miniport-abi.md` section 4, the `RH_DisableIrq`/`RH_EnableIrq`
 * row: usbport's binaries establish the *lifecycle*, and the choice of
 * mechanism is this driver's).
 *
 * It starts **clear**, and that is the safe direction rather than an oversight:
 * a close is not guaranteed a matching open - the scan's early return and both
 * of its non-empty exits bypass `RH_EnableIrq` entirely - so nothing may be held
 * back waiting for one. usbport opens the gate as part of ordinary root-hub
 * startup, and until then a change simply sits latched in the shadow where the
 * next status query finds it.
 *
 * **Its reader is `XhciRootHubAnnounce`** (Phase 5 task 5): a latched change is
 * announced with `UsbPortInvalidateRootHub` only while this bit is set, and
 * `RootHubInvalidatesGated` counts the drains it suppressed. Suppressing is not
 * losing, because usbport closes the gate *by* taking a notification - so a
 * closed gate means a scan of every port is already outstanding, and that scan
 * reads this driver's shadow through `RH_GetPortStatus`.
 */
#define XHCI_EXT_FLAG_RH_IRQ      0x00000020UL
/*
 * The teardown's port-power pass is running, so **no root-hub callback may reach
 * PORTSC** (audit finding A4).
 *
 * `xhciUnpowerPorts` writes PORTSC from `XhciStopController` *before* the quiesce
 * clears `INITIALIZED`, and it writes raw - no lock, no `PpArm`/`PpSettled`,
 * outside `xhciRhWritePortsc` entirely. Root-hub admission needs only
 * `INITIALIZED`, so on SMP Win2000 a concurrent `RH_GetPortStatus` acking a
 * change bit could compose a *neutral* value from a pre-teardown read - and a
 * neutral value carries PP as it was read - re-asserting the power the pass had
 * just taken away. It is also a second unserialized PORTSC writer, against
 * design doc 05 section 7's single-writer rule.
 *
 * The start-path twin (`xhciPowerPorts`) needs nothing like this because it runs
 * with `INITIALIZED` still clear. This is that same closure, expressed for the
 * one pass that cannot borrow it: the quiesce's own gate reads `INITIALIZED`
 * too, and clearing that early would make a stop on a *suspended* controller
 * return before `XhciControllerBeginQuiesce` ever ran.
 *
 * Set and cleared under the controller lock; read by `xhciRhAdmitted`.
 */
#define XHCI_EXT_FLAG_RH_CLOSED   0x00000080UL

/*
 * Where the init sequence got to. Recorded in XHCI_EXTENSION.InitStep next to
 * the status the step returned, because most of the refusal codes are shared
 * between steps and "XHCI_HC_WINDOW_TOO_SMALL" alone does not say which window.
 * Ordered as the sequence runs (docs/usb-xhci-info/xhci-programming.md, "Initialization
 * Sequence"); a completed init leaves DONE.
 */
#define XHCI_INIT_STEP_NONE         0
#define XHCI_INIT_STEP_RESOURCES    1   /* usbport's resource packet         */
#define XHCI_INIT_STEP_INTERRUPT    2   /* PCI Interrupt Pin - the INTx gate */
#define XHCI_INIT_STEP_BUS_MASTER   3   /* PCI Command.BME - the DMA gate    */
#define XHCI_INIT_STEP_CAP_SANITY   4   /* CAPLENGTH / HCIVERSION            */
#define XHCI_INIT_STEP_CAP_DECODE   5   /* the rest of the capability set    */
/*
 * Steps 1-6 are the preflight: they read, and they refuse, but between them
 * they perform no write of any kind - no PCI configuration write, no MMIO
 * write, no ownership claim. That is a property, not a coincidence, and
 * test_init checks it directly. Everything a controller can be declined for on
 * evidence alone is decided here, so a refusal leaves the controller exactly as
 * its firmware left it rather than claimed, halted and reset.
 */
#define XHCI_INIT_STEP_PORT_MAP     6   /* xECP walk + port classification   */
#define XHCI_INIT_STEP_HANDOFF      7   /* BIOS -> OS ownership - first write */
#define XHCI_INIT_STEP_HALT         8
#define XHCI_INIT_STEP_RESET        9
#define XHCI_INIT_STEP_RECHECK      10  /* the post-reset re-derivation      */
#define XHCI_INIT_STEP_PORT_MAP_RECHECK 11 /* the post-reset re-parse        */
#define XHCI_INIT_STEP_PAGESIZE     12
#define XHCI_INIT_STEP_LAYOUT       13  /* carve the common buffer           */
#define XHCI_INIT_STEP_DCBAA        14  /* DCBAA + scratchpad + DCBAAP       */
#define XHCI_INIT_STEP_COMMAND_RING 15  /* ring init + CRCR                  */
#define XHCI_INIT_STEP_EVENT_RING   16  /* ring init + ERST + interrupter 0  */
/* Putting back a BME this driver's own quiesce took. The *gate* on bus
 * mastering is step 3, in the preflight, because refusing a controller that
 * cannot DMA needs no more than a config read; this step is the write, and a
 * write cannot be in the preflight. */
#define XHCI_INIT_STEP_BUS_MASTER_RESTORE 17
#define XHCI_INIT_STEP_RUN          18  /* USBCMD.RUN, then HCHalted clears  */
#define XHCI_INIT_STEP_PORT_POWER   19  /* PORTSC.PP per port class          */
/* The root-hub map and the first read of every managed port's PORTSC. After
 * port power, because the shadow it seeds records PP - and the port map is what
 * decides which ports the shadow has entries for. */
#define XHCI_INIT_STEP_ROOT_HUB     20
#define XHCI_INIT_STEP_NOOP         21  /* the No Op command self-test       */
#define XHCI_INIT_STEP_DONE         22

/*
 * Not a step of the sequence, and deliberately far outside its range: roadmap
 * task 12.3's failed-start artifact records this when a build made with
 * -DXHCI_FAIL_START_CONTROLLER refuses the *last* step of an otherwise complete
 * initialisation, so that a package can be staged that installs, loads, and then
 * fails inside StartController - the one recovery route this project has never
 * exercised on either target.
 *
 * A shipping build cannot produce it: the only code that writes it is inside
 * that #ifdef, and any nonempty XHCI_EXTRA_DEFINES also embeds the do-not-deploy
 * marker make-package.ps1 refuses (src/sources). So a machine reporting
 * InitStep 250 is running the artifact, which is a statement about the media it
 * was installed from and not about the controller.
 */
#define XHCI_INIT_STEP_FAIL_INJECT  250

/*
 * The one refusal reason an init step can record that belongs to neither the
 * XHCI_LAYOUT_* family nor the XHCI_RING_* one: a register whose RsvdP field
 * this driver is required to preserve could not be read for the write, so
 * nothing was written. Deliberately outside both families' ranges (each ends at
 * 8) so that `ext->InitStatus` remains readable without knowing which family the
 * step that failed reports in.
 */
#define XHCI_INIT_NO_RMW_OPERAND    64

/* ------------------------------------------------------------------ */
/* The asynchronous command engine (Phase 4 task 7, src/xhci_cmd.c)    */
/* ------------------------------------------------------------------ */

/*
 * What XhciCommandSubmit answers. Recorded in XHCI_EXTENSION.NoOpStatus for the
 * one submit the init sequence performs, so a release build can say why the
 * self-test never went out.
 */
#define XHCI_CMD_OK             0
#define XHCI_CMD_BAD_PARAM      1
#define XHCI_CMD_NOT_READY      2   /* not initialized, or not running    */
#define XHCI_CMD_BUSY           3   /* one command outstanding at a time  */
#define XHCI_CMD_RING_FULL      4
#define XHCI_CMD_BAD_TRB        5   /* not a command TRB, or a bad template */
/* No async timer service, so the command could not be timed - and an untimed
 * command sits on the ring forever, so it is refused rather than issued
 * ("Software shall be responsible for all command timeouts", 4.6, p.91). */
#define XHCI_CMD_NO_TIMER       6
/* The controller reached the end of the recovery ladder and was declared failed.
 * Terminal within this start; a stop/start is the only way back. */
#define XHCI_CMD_FAILED         7

/*
 * XHCI_EXTENSION.CommandState. ABORTING is not a variant of PENDING: while it
 * is set the doorbell must not be rung, because "if the Command doorbell is
 * rung before CRR = '0', (i.e. the ring is not fully stopped), then the
 * behavior is undefined, e.g. the Command Ring may not restart" (Table 5-24
 * note, p.368). The state only returns to IDLE when the Command Ring Stopped
 * event arrives or the abort watchdog reads CRR as clear.
 */
#define XHCI_CMD_STATE_IDLE     0
#define XHCI_CMD_STATE_PENDING  1
#define XHCI_CMD_STATE_ABORTING 2

/*
 * One usbport transfer in flight, living in the miniport transfer extension
 * usbport allocates per transfer (MiniPortTransferSize). Phase 6 batch A grew
 * it from the Phase 3 placeholder.
 *
 * `Next` is what design doc 05 section 7 requires: the event DPC classifies and
 * retires under the controller lock but cannot call UsbPortCompleteTransfer
 * there, so completed transfers are threaded onto a list, the lock is released,
 * and they are completed afterwards - through storage usbport already allocated,
 * because this driver has no private pool (AGENTS.md).
 *
 * The ring indices are the transfer's identity. A Transfer Event names "the TRB
 * that generated this event" (Table 6-37), which for a control transfer may be
 * the Setup TRB, any Data TRB, or the Status TRB, so the owner is found by
 * asking which transfer's range contains the resolved index - never by
 * comparing against a head.
 */
typedef struct _XHCI_TRANSFER {
    ULONG Signature;
    struct _XHCI_TRANSFER *Next;
    PVOID TransferParameters;   /* the transfer's identity at completion */
    /*
     * UsbPortCompleteTransfer takes the *endpoint* extension beside the
     * transfer parameters, and by the time this transfer is completed the
     * completion list has been detached from any endpoint - the drain (task
     * 6-B.5) empties a whole queue and hands the list on. So the endpoint the
     * transfer arrived on is recorded per transfer rather than looked up per
     * completion; it is usbport's own pointer, stable for the endpoint's life,
     * and it is what the completion is answered through.
     */
    PVOID EndpointExtension;
    ULONG Token;                /* per-endpoint serial, for traces and logs */

    ULONG FirstIndex;           /* ring index of the first TRB of the group  */
    ULONG LastIndex;            /* ring index of the last TRB (Status Stage) */
    ULONG TrbCount;
    ULONG DataFirstIndex;       /* first Data Stage TRB, or XHCI_XFER_NO_INDEX */
    ULONG DataTrbCount;

    ULONG RequestedLength;
    ULONG BytesTransferred;     /* latched: never recomputed from a later event */
    ULONG Flags;                /* XHCI_XFER_FLAG_*                            */
    LONG UsbdStatus;            /* latched worst status seen for this transfer */

    /*
     * Task 7b-A.1: a snooped hub-class request whose IN reply the topology
     * graph wants to read (src/xhci_topo.h). Armed by the EP0 submit path when
     * XhciTopoObserveSetup asks for the reply, and consumed - under the
     * controller lock, before `UsbPortCompleteTransfer` hands the mapped buffer
     * back - by the completion drain. `TopoReplyVa` is the SG list's
     * MappedSystemVa, which stays valid for exactly that window: usbport unmaps
     * only after the completion call. `TopoReply` is XHCI_TOPO_REPLY_NONE on
     * every transfer that is not a snooped hub request, and every site that
     * signs a transfer record initializes it - the extension is not assumed
     * zeroed per transfer.
     */
    ULONG TopoReply;            /* XHCI_TOPO_REPLY_*                           */
    ULONG TopoAddress;
    ULONG TopoPort;
    ULONG_PTR TopoReplyVa;
    /*
     * Task 9-A.2's use of the same channel: a `GET_DESCRIPTOR(Configuration)`
     * whose reply carries the isochronous `bInterval` values, or a
     * `SET_CONFIGURATION`/`SET_INTERFACE` saying what the device is now running
     * (src/xhci_desc.h). All three are carried here and applied at the
     * transfer's completion, because a request that failed changed nothing on
     * the device. Kept in fields of their own rather than sharing the three
     * above, because
     * the two snoops answer different questions of different objects - the
     * topology graph is keyed on a hub, this is keyed on the device record -
     * and one pair of fields serving both would make every future reader check
     * which kind of reply a `TopoReply` of 1 meant. `DescAction` is
     * XHCI_DESC_ACT_NONE on every transfer that carries none, and every site
     * that signs a transfer record initializes it.
     */
    ULONG DescAction;           /* XHCI_DESC_ACT_*                             */
    ULONG DescAddress;
    ULONG DescValue;            /* the selection's value, per XHCI_DESC_SNOOP  */
    ULONG DescIndex;
    ULONG_PTR DescReplyVa;
    /*
     * **Which device record this action belongs to, proved rather than
     * assumed** - the first review round's finding.
     *
     * A usbport address is recycled: a record released while this transfer sits
     * on the completion list can be re-allocated to another device that is
     * given the same address, and a lookup by address alone would then install
     * one device's descriptor readings on another. The record index plus the
     * tenancy it was allocated under names the *tenancy*, not the address, and
     * a mismatch drops the action.
     */
    ULONG DescDeviceRef;
    ULONG DescTenancy;
    /*
     * The XHCI_EXTENSION.SubmitEpoch this transfer was threaded onto the
     * completion list under (Phase 7 review, finding A7). Meaningful only
     * while the transfer is on that list; stamped by the one threading site
     * (xhciDevOweCompletion). A transfer queued outside any submit bracket is
     * stamped `epoch - 1` so every pass may deliver it.
     */
    ULONG HeldEpoch;
    /*
     * Task 9-0.2. The TRB pointer of the Short Packet Event that armed this
     * transfer's deferred completion, meaningful only while
     * `XHCI_XFER_FLAG_SHORT_DEFERRED` is set.
     *
     * It is kept rather than re-derived because the settle re-runs
     * `XhciRingClassifyEvent` on **the TRB the event named** - which is what
     * asks the ring's own chain walk whether that TRB and this record's
     * `LastIndex` belong to the same TD. Classifying from `LastIndex` instead
     * would be very nearly a tautology and would validate nothing.
     *
     * The completion code is not stored beside it: the arm site tests
     * `completionCode == XHCI_CC_SHORT_PACKET` exactly, so a second field could
     * only ever hold that one value.
     */
    ULONG ShortTrbPA;
    /*
     * Task 9-A.1, and meaningful only under `XHCI_XFER_FLAG_ISOCH`.
     *
     * `IsoParams` is usbport's own `0x48 + 0x38 * n` block - the fifth argument
     * `SubmitIsoTransfer` received - kept because it is *both* where each
     * packet's two output fields are written and the fourth argument
     * `UsbPortCompleteIsoTransfer` takes. It is not recoverable from
     * `TransferParameters`: the two are different offsets into usbport's
     * transfer object and only the miniport is handed the second.
     *
     * `IsoPacketCount` is the block's `NumberOfPackets` as it was at submission,
     * copied rather than re-read at completion time for the reason every other
     * field here is copied - a value read twice from memory another driver owns
     * is two values.
     *
     * `IsoPacketsAnswered` is what makes the request finishable when its last
     * TD's event does not name the group's last TRB, which an error or a Missed
     * Service on a two-fragment final packet produces: the positional rule alone
     * would leave the transfer queued for ever on a pipe that has moved on.
     */
    PVOID IsoParams;
    ULONG IsoPacketCount;
    ULONG IsoPacketsAnswered;
} XHCI_TRANSFER, *PXHCI_TRANSFER;

#define XHCI_XFER_NO_INDEX          0xFFFFFFFFUL

/* How many early-retired tail TRB indices one queue remembers while waiting for
 * the trailing events p.175 promises. Four rather than one because a bulk IN
 * endpoint can have several short TDs retired before any tail is serviced, and
 * the whole point of the reading is that a shortfall must be attributable - an
 * eviction is counted (`MidTdTailsDropped`) rather than silently lowering the
 * tail count. */
#define XHCI_XFER_MID_TD_TAIL       4

/* A short packet or an error has already fixed BytesTransferred; a later event
 * for the same transfer reports the same length again (p.175: the second event
 * "should be set to the same value that was reported by the initial Short
 * Packet Event") and must not add to it. */
#define XHCI_XFER_FLAG_LENGTH_FIXED 0x00000001UL
/* A non-success completion code has been seen. "If any event generated by a TD
 * reports an error, then that Completion Code overrides any Successful
 * Completion Codes that other TRBs associated with the TD may have asserted,
 * whether they come before or after the error Event" (p.214). */
#define XHCI_XFER_FLAG_FAILED       0x00000002UL
/*
 * Task 9-0.2. A Short Packet Event has ended this transfer's TD mid-TD - the
 * receive fix's departure - but the completion is **held** until a drain pass
 * finds the event ring empty, because until then the tail event 4.10.1.1.2
 * p.175 promises may still be sitting in the ring unread. See
 * `XhciXferDrainSettled` for what the delay buys and what it does not.
 *
 * While it is set the transfer is still on the queue and still owns its TRBs,
 * so an arriving tail is matched to it by the ordinary owner search and ends it
 * through the ordinary positional rule. That is the conforming controller's
 * whole path, and it is why this flag - not a counter - is the fix.
 */
#define XHCI_XFER_FLAG_SHORT_DEFERRED 0x00000004UL
/*
 * Task 9-A.1. This transfer arrived through `SubmitIsoTransfer` and is answered
 * through `UsbPortCompleteIsoTransfer` - a different service taking different
 * arguments, so the completion drain reads this flag rather than inferring the
 * kind from the endpoint. Inferring it would be a second statement of the same
 * fact that can drift: an endpoint's records are rebuilt by a reopen while a
 * transfer in the completion list keeps pointing at the old binding.
 */
#define XHCI_XFER_FLAG_ISOCH        0x00000008UL

/* ------------------------------------------------------------------ */
/* 9-A.1: the isochronous group's storage                              */
/* ------------------------------------------------------------------ */

/*
 * **A usbport isochronous request of N packets is N Isoch TDs, not one TD.**
 * "An Isoch TD defines an isochronous data transfer that will occur during a
 * single Interval" and "the xHC shall consume one Isoch TD each Interval on an
 * Isoch Transfer Ring" (spec 4.11.2.3 p.196), so the packet boundary usbport
 * hands over *is* a scheduling boundary. Building one TD out of the whole
 * request would ask the controller to move a URB's worth of audio in one service
 * interval and then have nothing for the next.
 *
 * The whole group is still published as one store, exactly as a control
 * transfer's two or three TDs are, so the xHC can never begin a request whose
 * later TDs do not exist yet.
 *
 * The caps are the endpoint ring's own capacity, not a number chosen here: a
 * request needing more TRBs than an *empty* ring holds can never be placed, and
 * has to be failed rather than retried for ever. Each packet costs at least one
 * TRB, so the packet cap follows from the TRB cap and exists to bound the per-TD
 * length array rather than to state a second policy.
 *
 * **This is a declared limit with a refusal above it**, the shape design doc 04
 * uses for the slot and scratchpad counts, and the lever if a real USB Audio
 * device asks for more is `XHCI_POOL_RING_TRBS` rather than anything here.
 * `XHCI_EXTENSION.IsoRefusalsTooLarge` is what says whether one ever does.
 */
#define XHCI_XFER_MAX_ISO_TRBS      (XHCI_POOL_RING_TRBS - 2UL)
#define XHCI_XFER_MAX_ISO_PACKETS   XHCI_XFER_MAX_ISO_TRBS

/*
 * Where a built isochronous group landed. `TdLengths` is one entry per packet,
 * in packet order, which is what makes packet *i* and TD *i* the same thing
 * everywhere else in the engine.
 */
typedef struct _XHCI_ISO_LAYOUT {
    ULONG TrbCount;
    ULONG TdCount;                  /* == the block's NumberOfPackets */
    ULONG TdLengths[XHCI_XFER_MAX_ISO_PACKETS];
    /* 1 if every TD carries an explicit Frame ID, 0 if every TD carries SIA.
     * All-or-nothing per submission: a group with a gap in its Frame IDs is how
     * the spec says to *pause* a stream (4.11.2.5 p.199), so mixing the two
     * within one request would ask for a pause nobody wanted. */
    ULONG FrameIdsUsed;
    /* 1 if usbport's per-packet frame stamps do not advance at the rate the
     * Endpoint Context this driver programmed will consume TDs. It forces SIA
     * for the request, and it is the only runtime measurement this driver has of
     * an isochronous endpoint's real service interval - see
     * `xhciXferIsoCadenceAgrees`. */
    ULONG CadenceMismatch;
} XHCI_ISO_LAYOUT, *PXHCI_ISO_LAYOUT;

/*
 * The FIFO of transfers outstanding on one endpoint's ring. In ring order,
 * because that is the order they were enqueued in and the order the xHC
 * executes them - which is what lets a retire that jumps several TDs identify
 * exactly which transfers it swept.
 */
typedef struct _XHCI_TRANSFER_QUEUE {
    PXHCI_TRANSFER Head;
    PXHCI_TRANSFER Tail;
    ULONG Count;
    ULONG NextToken;

    /* Release-build diagnosis. Each names one thing that cannot be inferred from
     * the others, which is the rule Phase 4 task 8's review paid for. */
    ULONG Submitted;
    ULONG Completed;
    ULONG ShortPackets;
    /* Transfer Events that said Success and carried a nonzero residual - the
     * spurious-success quirk (`docs/usb-xhci-info/xhci-programming.md`). A short transfer
     * either way, but counted apart from `ShortPackets` because the two say
     * opposite things about the controller in the machine. */
    ULONG ShortSuccesses;
    /* Success events on a **data** TRB with a residual of zero - the shape the
     * spec produces for an intermediate IOC, which this driver never sets (IOC
     * is the Status Stage TRB's alone, p.430, and ISP produces code 13). Zero on
     * every conforming controller. It is counted rather than ignored because
     * such an event now *fixes* the reported length, so a controller that emits
     * one and then completes the TD normally is underreported - and this is the
     * only reading that would say so. */
    ULONG IntermediateEvents;
    /* Transfers ended on a Short Packet Event that did **not** name the TD's
     * last TRB - the deliberate departure in `XhciXferDrainSettled`, taken only
     * for a TD that is entirely data. It counts the transfers this driver
     * retired *early*, and nothing more.
     *
     * **Before task 9-0.2 it did not say which controller the machine had**,
     * and an earlier revision of this comment claimed it did. The departure was
     * taken the moment the first of the two events p.175 promises arrived,
     * before the second one could possibly have been delivered, so a conforming
     * xHC moved it identically.
     *
     * Task 9-0.2 changed that by changing *when* the departure is taken - not
     * at the event, but at the end of a drain pass that found the event ring
     * empty. A conforming controller's tail is already in the ring behind the
     * short event, so it is consumed first, completes the transfer through the
     * ordinary positional rule, and this counter never moves. It is therefore
     * now a reading in its own right, and `MidTdDeferralsTailed` below is the
     * other half of the same partition. */
    ULONG MidTdShortRetires;
    /*
     * Task 9-0.2's four, and together with `MidTdShortRetires` they are the
     * mid-TD verdict now. A mid-TD short packet no longer completes the
     * transfer where it lands: it **arms a deferral** (`MidTdDeferrals`), and
     * the deferral ends one of five ways -
     *
     *   `MidTdDeferralsTailed`  the promised tail arrived while the transfer
     *                           was still queued and still owned its TRBs, so
     *                           the ordinary positional rule completed it. This
     *                           is the conforming controller's whole path, and
     *                           it costs no early retire, records no tail index
     *                           and censors nothing. Strict: p.175 requires the
     *                           second event to carry **Short Packet** on the
     *                           TD's last TRB, and only that counts here.
     *   `MidTdDeferralsTailedSpurious`
     *                           a second event did arrive on the last TRB, but
     *                           carrying **Success** rather than Short Packet.
     *                           Linux carries explicit handling for hosts that
     *                           emit exactly this after a short packet
     *                           (`xhci-ring.c`), so it is a known quirk rather
     *                           than a fault, and the transfer completes
     *                           correctly either way - `LENGTH_FIXED` already
     *                           holds the short measurement. It is counted
     *                           apart because it answers the verdict's question
     *                           ("did the controller send a second event?")
     *                           with *yes* while failing the conformance test.
     *                           Folding it into `MidTdDeferralsLost` - which is
     *                           what happened before the batch-9-0 review round
     *                           1 - made a two-event controller indistinguish-
     *                           able from a teardown, on the one measurement
     *                           this whole task exists to produce.
     *   `MidTdShortRetires`     a drain pass found the event ring empty with
     *                           the deferral still outstanding, so the tail is
     *                           not coming and the TD was retired early. This
     *                           is the one-event controller's path. **A sweep
     *                           by a later TD's retire lands here too**, and
     *                           not in `Lost`: being swept is stronger proof
     *                           the tail was never sent than the settle's,
     *                           because the drain is FIFO.
     *   `MidTdDeferralsLost`    the observation ended without answering the
     *                           question - cancelled, aborted, drained at
     *                           teardown, **or a settle whose retire the ring
     *                           refused** (a record/ring divergence tells you
     *                           nothing about whether the tail was sent).
     *                           Neither controller is implicated.
     *                           `MidTdRefusedRetires` and its `Unarmable`
     *                           companion are related but are **not** a
     *                           decomposition of this one: they also count
     *                           event-time divergences, which never armed a
     *                           deferral at all, so subtracting them can
     *                           underflow.
     *   still armed             on the queue right now, counted by walking it.
     *
     * The five partition `MidTdDeferrals` exactly, and that identity is
     * asserted rather than assumed - a row set assembled path-by-path is only
     * as complete as whoever assembled it (task 7b-A.1.0).
     *
     * **The verdict this replaces the old one with:**
     *
     *   tailed == `MidTdDeferrals`                 -> conforming, two events
     *   tailed == 0, tailedSpurious == deferrals   -> two events, wrong code
     *                                                 (the Linux quirk)
     *   `MidTdShortRetires` > 0, both tailed
     *   counters 0                                 -> one event only (QEMU's)
     *
     * and it needs none of the censoring qualifications the pre-9-0.2 reading
     * did, because a conforming controller never reaches the early-retire path
     * at all.
     */
    ULONG MidTdDeferrals;
    ULONG MidTdDeferralsTailed;
    ULONG MidTdDeferralsTailedSpurious;
    ULONG MidTdDeferralsLost;
    /*
     * The residual reading, and after task 9-0.2 it measures only the window
     * that fix could not close: a tail written by the xHC *after* a drain pass
     * read the ring empty, arriving once the early retire has already re-let
     * its TRBs. Each early retire records the tail TRB index it did not wait
     * for; a later event naming that index - unowned, because the transfer is
     * gone - increments this. So:
     *
     *   `MidTdShortTails` == `MidTdShortRetires`  -> conforming, two events
     *   `MidTdShortTails` == 0 with retires > 0   -> one event only (QEMU's xHC)
     *
     * It cannot false-positive: while an index is unowned nothing can re-let it
     * (`XhciRingHasRoom` will not pass the dequeue pointer), and once a new
     * transfer does own it the event matches that owner and never reaches the
     * unmatched path at all.
     *
     * `UnmatchedEvents` is **not** this reading and never was: it also counts
     * events for transfers that ended any other way, so it is an upper bound
     * contaminated by unrelated traffic.
     */
    ULONG MidTdShortTails;
    /* Early retires whose recorded tail index was evicted from the fixed slot
     * ring before any tail event arrived - i.e. more than XHCI_XFER_MID_TD_TAIL
     * of them outstanding at once. Without it a short `MidTdShortTails` reads as
     * "the controller withheld the tail" when it may only be slot pressure, so
     * a nonconformance verdict needs this at zero. On a one-event controller it
     * rises with `MidTdShortRetires` by construction, which is not a fault. */
    ULONG MidTdTailsDropped;
    /*
     * Observations **censored** rather than answered: a record given up because
     * its TRBs were re-let to a new TD, or still outstanding when the queue was
     * folded. The tail may well have been sent - it just can no longer be told
     * apart from a later transfer's event at the same index, so the driver
     * declines to read it either way.
     *
     * Before task 9-0.2 this was not a rare case and must not have been treated
     * as one. The repost loop a NIC runs was exactly it: retire early ->
     * complete to usbport -> usbport posts the next receive -> that TD is
     * published over the very TRBs whose tail is still outstanding. So on a
     * **conforming** controller under bulk IN load, `MidTdShortTails` could
     * legitimately read 0 with `MidTdShortRetires` large - the one-event
     * signature - and only this counter distinguished "the controller withheld
     * them" from "the driver could not look" (repo audit round 3, finding 1).
     *
     * Task 9-0.2 removes the ordinary source of that: a conforming controller's
     * tail is consumed in-band, no early retire happens and no record is made,
     * so on such a machine this should now read **zero**. Which is the check
     * that says the fix worked - `docs/contributing/roadmap.md` task 9-0.2 - and it is why
     * the verdict no longer has to be qualified by it. What remains is the
     * narrow residual: a tail written after a pass read the ring empty.
     *
     * The `MidTdShortTails` verdict therefore still needs `MidTdTailsDropped`
     * **and** this at zero. Where both cannot be had, the honest reading is
     * that the question was not answered, not that the controller failed - but
     * `MidTdDeferralsTailed` against `MidTdDeferrals` answers it without them.
     */
    ULONG MidTdTailsCensored;
    /* The recorded tail indices themselves: a compacted FIFO of `MidTdTailCount`
     * live entries, oldest first, so a record is displaced only when all of them
     * are outstanding. Fixed size - no allocation is available on this path and
     * the ring is a diagnostic, so losing the oldest is preferable to growth.
     * A record is also given up when its TRBs are re-let to a new TD. That is
     * counted apart from an eviction, in `MidTdTailsCensored` - not in nothing,
     * which is what an earlier revision did on the reasoning that the tail "can
     * no longer arrive so nothing is outstanding to be short of". It may
     * already have been sent and be sitting unprocessed in the event ring; what
     * ends is this driver's ability to tell it from the new TD's own event. */
    ULONG MidTdTailIndex[XHCI_XFER_MID_TD_TAIL];
    ULONG MidTdTailCount;
    ULONG Errors;
    /* Completions that left the endpoint not running. Halted is the usual
     * case, but a TRB Error leaves it in **Error** instead, which needs a
     * different command - see `XHCI_XFER_EVENT_RESULT.NeedsRecovery`. */
    ULONG Recoveries;
    ULONG UnmatchedEvents;  /* resolved to a ring index owned by no transfer */
    ULONG ForeignEvents;    /* did not resolve to this ring at all           */
    ULONG EventDataEvents;  /* ED = 1: this driver places no Event Data TRBs */
    ULONG BadCodes;         /* impossible for this ring, or unassigned       */
    /* Codes 26-28 handed to `XhciXferEvent`: the slot layer owns those
     * (`XhciXferQueueStopped`), and this layer refuses them rather than
     * completing the owning transfer as cancelled and sweeping the rest. */
    ULONG StoppedRefused;
    /*
     * Three ways the length arithmetic can produce something unusable, kept
     * apart because they have three different diagnoses - and because sharing
     * one counter made two of them untestable: a mutation deleting the first
     * check failed no check at all, since the third caught the same vector.
     *
     *   ResidualRejects - the controller said more bytes were not transferred
     *     than the TRBs it named could hold. A controller-side answer that
     *     cannot be true.
     *   LengthOverruns  - the bytes computed exceed the buffer usbport mapped.
     *     The TRB lengths on the ring and the transfer's own length disagree,
     *     which is this driver's bookkeeping or memory that moved under it.
     *   SumFailures     - the sum could not be taken, because the TRBs the
     *     transfer record names are no longer outstanding. The record and the
     *     ring have diverged.
     */
    ULONG ResidualRejects;
    ULONG LengthOverruns;
    ULONG SumFailures;
    ULONG ResidualIgnored;  /* nonzero residual where no length field exists */
    ULONG SweptTransfers;   /* reclaimed with no event of their own          */
    /* A transfer ended without its TRBs being reclaimed and without the ring
     * asking for recovery - a combination this engine has no path to. Counted
     * apart from Recoveries because the two have opposite diagnoses: one is the
     * hardware behaving as the spec describes, the other is this driver's model
     * of it being incomplete. */
    ULONG OrphanedGroups;
    /* The ring refused an explicit dequeue position. The software and hardware
     * pointers cannot be reconciled from here. */
    ULONG PlacementFailures;

    /*
     * Task 8-A.1's backpressure latch, and it lives on the **queue** rather than
     * on the endpoint record because EP0 has no record: the queue is the one
     * structure both kinds of endpoint have, and `xhciEpResolve` hands it back
     * for both. A parallel pair of fields - one on the record, one on the device
     * - is exactly the two-statements-of-one-fact shape this driver has already
     * paid for once.
     *
     * What it is for: a `SubmitTransfer` refused with `MP_STATUS_NO_RESOURCES`
     * is left queued by usbport and **re-offered on its 500 ms timer**
     * (`docs/contributing/implementation-invariants.md`, "Ring Full and Backpressure"). The
     * latch turns that refusal into a `UsbPortInvalidateEndpoint` as soon as
     * ring space actually appears, instead of waiting out the timer.
     *
     * **Its original motivation was measured false, and the latch is kept
     * anyway.** Batch 8-A predicted that a full bulk ring would be the ordinary
     * steady state of a device moving data as fast as it can, capping mass
     * storage at two transfers a second without this. Phase 8 then measured
     * `TransfersRefusedRingFull` and `RetriesAsked` at **0** on working storage
     * *and* working Ethernet under load, on both targets: Bulk-Only Transport is
     * strictly serial, and the available NIC keeps only a handful of receives
     * posted, so no device class this project can reach fills a 62-TRB ring
     * (`docs/contributing/lessons.md`, batch 8-V's storage and Ethernet
     * entries; the Phase 8 decision in `docs/contributing/roadmap.md`). Treat the mechanism
     * as defensive and its exercise as synthetic - a host vector is the only
     * thing that has ever driven it. What it is *not* is dead code with a live
     * justification, which is what this comment used to read as.
     *
     * `RetryFree` is the ring's free-TRB count **at the moment of the refusal**,
     * and the re-offer condition is *strictly greater* rather than "not full".
     * That is what makes the arrangement terminate: each re-offer costs a real
     * retirement, a refusal re-arms at the new higher count, and an empty ring
     * always has room for one transfer (`ep0_max_transfer_fits_the_ring` and the
     * pool ring's identical stride), so there is no value the latch can settle at
     * with work outstanding and nothing due. `RetriesAsked` is the reading that
     * says the mechanism ran; it climbing with `Submitted` frozen would be the
     * livelock, and neither can be inferred from the other.
     */
    ULONG RetryArmed;
    ULONG RetryFree;
    ULONG RetriesAsked;

    /*
     * Task 9-A.1's isochronous readings. They live on the queue beside the rest
     * for the same reason those do - it is the one structure both kinds of
     * endpoint have - and every one of them names something that cannot be
     * inferred from the others.
     *
     *   `IsoPackets`        packets submitted, so a packet-level rate is
     *                       readable without dividing transfers by an assumed
     *                       packet count.
     *   `IsoPacketsAnswered` packets given a status by an event. The difference
     *                       from `IsoPackets` on a settled endpoint is packets
     *                       the controller never reported at all, which is a
     *                       different fault from a packet reported as failed.
     *   `IsoPacketErrors`   packets whose status was not Success or a short
     *                       read. On an isoch pipe this is expected to be
     *                       nonzero under load and is not a defect; what makes
     *                       it a reading is its ratio to `IsoPackets`.
     *   `IsoMissedService`  completion code 23. "The data associated with the
     *                       TD in error shall be lost, however for the next ESIT
     *                       the xHC shall advance to the next Isoch TD"
     *                       (4.10.3.2 p.187) - the pipe resynchronizing, not an
     *                       endpoint in trouble, and counted apart so a target
     *                       log does not read it as one.
     *   `IsoGroupsAwaitingTail`
     *                       every packet of a group answered while the event
     *                       that answered the last one named an *earlier* TRB,
     *                       so the group is still queued waiting for the tail
     *                       event p.201 promises. Ordinary rather than a fault -
     *                       it is what an error or a Missed Service on a final
     *                       packet split across two fragments looks like - and it
     *                       is a **transient**: the reading that matters is a
     *                       count that rises while `Completed` does not, which is
     *                       a controller dropping the tails this driver is
     *                       waiting for. One group can be counted more than once
     *                       if several intermediate events land on it.
     */
    ULONG IsoPackets;
    ULONG IsoPacketsAnswered;
    ULONG IsoPacketErrors;
    ULONG IsoMissedService;
    ULONG IsoGroupsAwaitingTail;
} XHCI_TRANSFER_QUEUE, *PXHCI_TRANSFER_QUEUE;

/*
 * The miniport endpoint extension, and it is the one structure here whose
 * lifetime is not the miniport's to reason about: `USBPORT_ReopenPipe` **zeroes
 * every byte of it** between a `SetEndpointState(REMOVE)` and the next
 * `OpenEndpoint`, with no close callback in between, and the endpoint delete
 * path frees it with no callback at all (batch 6-0, `docs/usb-xhci-info/usbport-miniport-abi.md`
 * section 8). So nothing that has to survive an EP0 reopen may live here - which
 * is every piece of device state there is, and why XHCI_DEVICE below sits in the
 * controller extension instead.
 *
 * What is left is a back-reference plus its own validity check. `DeviceIndex` is
 * an index into XHCI_EXTENSION.Devices **plus one**, so that the zeroed state
 * usbport leaves behind names no device rather than device 0.
 */
#define XHCI_ENDPOINT_FLAG_OPEN     0x00000001UL

typedef struct _XHCI_ENDPOINT {
    ULONG Signature;
    ULONG DeviceIndex;      /* index into Devices[] + 1; 0 = bound to nothing */
    ULONG SlotId;           /* diagnostic copy - Devices[] is authoritative   */
    ULONG Dci;
    ULONG Flags;
} XHCI_ENDPOINT, *PXHCI_ENDPOINT;

/* ------------------------------------------------------------------ */
/* One addressed device (Phase 6 batch B, src/xhci_slot.c)             */
/* ------------------------------------------------------------------ */

/*
 * **The state is what the *driver* has achieved, not what the xHC's Slot
 * Context reports.** The two are related but not the same question: the Slot
 * Context's Slot State is written by hardware and readable, while this says
 * which step of the enumeration chain has been *confirmed by a completion
 * event*. Deriving one from the other would mean reading a context to decide
 * whether the command that filled it has completed.
 *
 *   RESERVED  - a record is bound to a root-hub port and owes an Enable Slot.
 *               No Slot ID, no hardware state, nothing to unwind but the record.
 *   ENABLED   - the xHC gave us a Slot ID. The device context and EP0 ring have
 *               been carved and the DCBAA entry published; Address Device is
 *               owed. Unwinding from here needs a Disable Slot.
 *   DEFAULT   - Address Device with BSR = 1 completed: the slot is in the
 *               Default state, EP0 is Running, and control transfers may flow to
 *               the device *at address 0*.
 *   ADDRESSED - Address Device with BSR = 0 completed, so the device answers on
 *               the bus address usbport chose and the address map entry is live.
 *   FAILED    - a command in the chain reported an error. Transfers are refused
 *               from here; the record is not reused until the port reports a
 *               connect change, because a device that would not address itself
 *               is not made to work by asking again immediately.
 *   GONE      - the port said the device left. Everything queued has been
 *               completed as cancelled and a Disable Slot is owed or in flight;
 *               the record returns to FREE when the xHC confirms it.
 */
#define XHCI_DEV_STATE_FREE         0
#define XHCI_DEV_STATE_RESERVED     1
#define XHCI_DEV_STATE_ENABLED      2
#define XHCI_DEV_STATE_DEFAULT      3
#define XHCI_DEV_STATE_ADDRESSED    4
#define XHCI_DEV_STATE_FAILED       5
#define XHCI_DEV_STATE_GONE         6

/*
 * The command a device record owes, or has outstanding. One field for "owed"
 * and one for "issued" rather than a single state with an in-flight bit,
 * because the two are read by different contexts for different reasons: the
 * pump asks what is owed with the lock held and the engine idle, and the
 * completion asks what was issued.
 */
#define XHCI_DEV_OP_NONE            0
#define XHCI_DEV_OP_ENABLE_SLOT     1
#define XHCI_DEV_OP_ADDRESS_BSR     2   /* Address Device, BSR = 1  */
#define XHCI_DEV_OP_ADDRESS_SET     3   /* Address Device, BSR = 0  */
#define XHCI_DEV_OP_EVALUATE_MPS    4   /* Evaluate Context, EP0 MPS0 */
#define XHCI_DEV_OP_DISABLE_SLOT    5
/*
 * Configure Endpoint for one non-default endpoint (task 7a-A.1).
 *
 * **This one is never stored in `PendingOp`**, and that is deliberate rather
 * than an inconsistency. `PendingOp` holds one op, so writing it from the
 * endpoint open would silently discard an `EVALUATE_MPS` the EP0 reopen had just
 * owed - two chains that genuinely can overlap, because usbport opens the
 * interrupt pipe while the Max Packet Size correction is still in flight. The
 * need for a Configure Endpoint is therefore **derived** from the endpoint
 * records (see `xhciDevOwedOp`), which cannot collide with anything and cannot
 * be lost.
 */
#define XHCI_DEV_OP_CONFIGURE_EP    6
/*
 * The three per-endpoint quiescence commands (batch 7a-B). Like
 * `XHCI_DEV_OP_CONFIGURE_EP` and for the same reason, **none of them is ever
 * stored in `PendingOp`**: the need is derived from the endpoint's own
 * `XHCI_EP_QUIESCE` flags, so it can neither collide with the EP0 chain's single
 * slot nor be overwritten by it.
 *
 * They outrank `PendingOp` in `xhciDevOwedOp`, which is what makes a teardown
 * legal: "before a Disable Slot ... any active endpoints ... shall be in the
 * Stopped state or Idle in the Running state, and any outstanding Transfer
 * Events shall have been received" (4.6.4 p.97). A Disable Slot sitting in
 * `PendingOp` therefore has to wait for every endpoint's Stop Endpoint.
 */
#define XHCI_DEV_OP_STOP_EP         7
#define XHCI_DEV_OP_RESET_EP        8
#define XHCI_DEV_OP_SET_DEQUEUE     9
/*
 * Task 7b-A.2: mark an external hub's own Slot Context - Hub, Number of Ports,
 * and TTT/MTT where they apply.
 *
 * **It goes out as a Configure Endpoint, and the task text's "using Evaluate
 * Context" is wrong.** Spec 6.2.2.3 p.412: "A 'valid' Input Slot Context for an
 * Evaluate Context Command requires the Interrupter Target and Max Exit Latency
 * fields to be initialized. **Only these fields shall be evaluated** when the
 * xHC receives an Evaluate Context Command that flags the Slot Context", and
 * "Only the Output Interrupter Target and Max Exit Latency fields are updated by
 * the Evaluate Context Command." The command that carries these four is the one
 * named in 6.2.2.2 p.412 instead.
 *
 * Like `XHCI_DEV_OP_CONFIGURE_EP` it is **never stored in `PendingOp`** - the
 * need is derived from the topology graph against `HubMarkDone`, so it can
 * neither displace the EP0 chain's owed step nor be displaced by it. It is
 * derived *below* `CONFIGURE_EP` deliberately: every Configure Endpoint this
 * driver issues carries A0 and fills the same four fields, so an endpoint's own
 * command marks the hub for free and this one is only reached when the
 * descriptor arrived with no endpoint work left to ride on.
 */
#define XHCI_DEV_OP_MARK_HUB        10
/*
 * Reset Device (4.6.11), the re-enumeration step the repo audit's finding A2
 * found missing.
 *
 * usbhub re-enumerates a device by resetting its port and opening EP0 at address
 * 0 again, and this driver keeps the slot across that. Getting the kept slot back
 * to Default is **not** an Address Device with BSR = 1: that command is legal
 * from the `Enabled` state and no other, so on a slot that has already been
 * addressed - which is every slot on its second enumeration - a conforming xHC
 * answers Context State Error. The list is transcribed in
 * `docs/usb-xhci-info/xhci-data-structures.md`, "Which Slot State each command
 * requires"; the comment that used to sit at the reuse branch carried it from
 * memory and had it wrong.
 *
 * Reset Device's own list is the complement - Addressed or Configured - and its
 * effect is exactly what the reuse branch wants: Slot State to Default, USB
 * Device Address to 0, Context Entries to 1, every endpoint but EP0 Disabled,
 * EP0 Running. So the branch reads the Output Slot State and owes this command
 * where BSR would have been refused, and owes *nothing* where the slot is
 * already in Default.
 *
 * Stored in `PendingOp` like the rest of the EP0 chain, because it is a step of
 * that chain and never concurrent with one.
 */
#define XHCI_DEV_OP_RESET_DEVICE    11

/* The EP0 pipe is open: usbport has an endpoint extension bound to this record
 * and may submit transfers through it. Cleared by SetEndpointState(REMOVE),
 * which is the only notice either shipping build ever gives (batch 6-0). */
#define XHCI_DEV_FLAG_EP0_OPEN      0x00000001UL
/* The address map entry for DeviceAddress is live. Its own bit rather than
 * "DeviceAddress != 0", because usbport's addresses start at 1 and a record
 * that has been addressed and then torn down still holds the number. */
#define XHCI_DEV_FLAG_ADDRESS_VALID 0x00000002UL
/*
 * The DCBAA entry for this slot points at this record's device context.
 *
 * Cleared **after** the Disable Slot completes, not before it: the xHC reaches
 * its own slot state through that entry, and "the Device Context Base Address
 * Array entry ... shall be cleared to '0' by software after the Disable Slot
 * Command completes" is the order the lifecycle needs - a null entry under a
 * slot the controller still has enabled is a pointer it may follow to zero.
 */
#define XHCI_DEV_FLAG_DCBAA_SET     0x00000004UL
/*
 * usbport has stopped believing in this device - it disabled or unpowered the
 * port and freed the USB address - but the controller has not yet been observed
 * to let the port go. The record keeps its slot, its ring and anything queued
 * until it has; what it has already given up is the address map entry and the
 * endpoint binding, which is the half that must not wait (roadmap batch 6-V).
 */
#define XHCI_DEV_FLAG_DISOWNED      0x00000008UL
/* This device's **EP0** owes a UsbPortInvalidateEndpoint call. Its own bit
 * rather than a shared pointer in the extension, so that an interrupt endpoint's
 * invalidation cannot overwrite it - see XHCI_EXTENSION.EndpointInvalidatesOwed. */
#define XHCI_DEV_FLAG_INVALIDATE_EP0 0x00000010UL
/*
 * A hub marking (task 7b-A.2) was attempted for this record and did not take.
 *
 * The need for `XHCI_DEV_OP_MARK_HUB` is *derived*, so a failure that left the
 * derivation intact would have the pump rebuild the same command for ever -
 * the shape task 7a-A.1 already paid for with `XHCI_EP_REC_FAILED`. This is
 * that stop, and it is a flag rather than a pretended success because writing
 * the wanted marking into `HubMarkDone` would be this driver claiming the xHC
 * holds something it refused. Cleared by an Address Device, which is the point
 * at which the whole Output Slot Context is rewritten and the question is open
 * again.
 */
#define XHCI_DEV_FLAG_HUB_MARK_FAILED 0x00000020UL
/*
 * The one hub-tier reset suppression this record's enumeration bracket is
 * allowed has been spent (Phase 7 review, finding A6 - task 7b-A.1.1's bound,
 * one tier down). Set when a snooped SET_FEATURE(PORT_RESET) naming this
 * record's position was judged to be usbhub's second reset of the bracket and
 * its claim re-arm suppressed; cleared when the next address-0 open at this
 * position spends a claim, which is what opens a new bracket. Without the
 * bound, a record abandoned mid-enumeration would suppress every later reset
 * and disable its position permanently.
 */
#define XHCI_DEV_FLAG_HUB_RESET_SUPPRESSED 0x00000040UL

/*
 * One hub marking, packed so that "does the xHC already hold this?" is one
 * comparison rather than four (task 7b-A.2).
 *
 * The fields stay *numbers* inside it - `Number of Ports` is a count and TTT is
 * a two-bit value, and task 7b-A.1.2's finding was that a shape bit cannot carry
 * a measurement. Bit 31 is what makes "no marking" distinguishable from "a
 * marking of all zeroes", which matters because the driver never issues a
 * command to *un*-mark a hub: a zero wanted value means "nothing to say", not
 * "say that it is not a hub".
 */
#define XHCI_HUBMARK_HUB            0x80000000UL
#define XHCI_HUBMARK_MTT            0x00010000UL
#define XHCI_HUBMARK_PORTS_SHIFT    8
#define XHCI_HUBMARK_TTT_MASK       0x00000003UL
#define XHCI_HUBMARK(hub, ports, mtt, ttt)                       \
    (((hub) != 0 ? XHCI_HUBMARK_HUB : 0UL) |                     \
     ((mtt) != 0 ? XHCI_HUBMARK_MTT : 0UL) |                     \
     (((ports) & 0xFFUL) << XHCI_HUBMARK_PORTS_SHIFT) |          \
     ((ttt) & XHCI_HUBMARK_TTT_MASK))
#define XHCI_HUBMARK_GET_PORTS(m)   (((m) >> XHCI_HUBMARK_PORTS_SHIFT) & 0xFFUL)
#define XHCI_HUBMARK_GET_TTT(m)     ((m) & XHCI_HUBMARK_TTT_MASK)

/*
 * The same marking again, laid out one field per byte, for the channel a
 * bare-metal target had when this was written: a traced build's line. *(That is
 * the `qemu` flavour since task 13-L.1, and it is not a bare-metal channel at
 * all - `qemu` is never published. What a bare-metal target has now is the
 * snapshot read, `XHCISNAP` over PassThru, since `0.0.0.6`.)* The clause that wanted it
 * - the `MTT`/`TTT` context-field readings on real translators - had no vehicle
 * and is published as a limitation; the batch and run sheet that held it were
 * deleted and the reading was never taken.
 *
 * **This is a presentation, not a second packing**, and the two must not be
 * confused: `XHCI_HUBMARK` is what the driver compares and stores, and every
 * field below is read back out of it through that macro's own accessors, so
 * the two cannot drift into describing the same hub differently.
 *
 * Why a byte per field rather than the stored word: the reader is an operator
 * in front of DebugView on a machine with no counter channel, converting hex
 * by eye, and `XHCI_HUBMARK` spends bit 31 on a flag and puts TTT hard against
 * the port count. Byte 3 is the Slot ID, byte 2 is MTT, byte 1 is the port
 * count, byte 0 is TTT - so `0x03010700` reads off as slot 3, MTT 1, 7 ports,
 * TTT 0 with no arithmetic. `XHCI_TT_TRACE_WORD` keeps bytes 3 and 2 meaning
 * the same two things, because the clause is read as a *pair* of lines and a
 * format that changed between them would be read wrong.
 *
 * The Hub bit is deliberately absent. `XhciTopoHubMark` sets it on every
 * marking it returns and returns 0 otherwise, so within a marking it carries
 * no information - what says "this is a hub's slot" is the line existing.
 *
 * `mark` is evaluated more than once; every call site passes a plain local.
 */
#define XHCI_TRACE_SLOT_SHIFT       24
#define XHCI_TRACE_MTT_SHIFT        16
#define XHCI_HUBMARK_TRACE_WORD(slotId, mark)                    \
    ((((ULONG)(slotId) & 0xFFUL) << XHCI_TRACE_SLOT_SHIFT) |     \
     ((((mark) & XHCI_HUBMARK_MTT) != 0 ? 1UL : 0UL)             \
          << XHCI_TRACE_MTT_SHIFT) |                             \
     (XHCI_HUBMARK_GET_PORTS(mark) << 8) |                       \
     XHCI_HUBMARK_GET_TTT(mark))
/*
 * And the child half of the same clause: the TT triple a Low-/Full-Speed
 * device behind a High-Speed hub carries, in the same byte order. TTT is not
 * in it because a child has none - see the trace site in `src/xhci_slot.c`,
 * where the *absence* of a hub line for the same Slot ID is what reads that
 * half of the clause.
 */
#define XHCI_TT_TRACE_WORD(slotId, mtt, parentSlotId, parentPort) \
    ((((ULONG)(slotId) & 0xFFUL) << XHCI_TRACE_SLOT_SHIFT) |      \
     (((mtt) != 0 ? 1UL : 0UL) << XHCI_TRACE_MTT_SHIFT) |         \
     (((ULONG)(parentSlotId) & 0xFFUL) << 8) |                    \
     ((ULONG)(parentPort) & 0xFFUL))

/*
 * One open non-default endpoint on a device (batch 7a-A.1).
 *
 * The ring lives in the **controller** common buffer, from the pool
 * (design doc 04 section 3.6), so it survives anything usbport does to the
 * endpoint extension - including the wipe on reopen and the free with no
 * callback at delete. `PoolIndex` is the pool entry that backs `Ring`, and the
 * two are released together or not at all.
 *
 * `State` records what a Configure Endpoint command has *done* for this DCI, not
 * that one was issued: until one has completed the xHC has no Endpoint Context
 * for the DCI and a doorbell on it would be rung at nothing.
 *
 *   PENDING     - a Configure Endpoint is owed. `xhciDevOwedOp` derives the
 *                 device's next command from the presence of a record in this
 *                 state, which is what keeps it out of the single `PendingOp`
 *                 slot the EP0 chain uses.
 *   CONFIGURING - one is in flight. Distinct from PENDING for exactly one
 *                 reason: the derived op must stop naming this record while its
 *                 command is outstanding, or the pump would issue a second.
 *   CONFIGURED  - one completed with Success. Transfers may flow.
 *   REFUSED     - the xHC declined for **bandwidth or resources** (completion
 *                 codes 7, 8 and 35). That is a scheduling answer, not a fault: the
 *                 device, its slot and EP0 are all still good, and only this
 *                 pipe is dead. Kept as a record rather than torn down so a
 *                 later `SubmitTransfer` has somewhere to answer from.
 *   FAILED      - any other completion code, or a command that could not be
 *                 encoded. Same containment: this endpoint, not this device.
 */
/* ------------------------------------------------------------------ */
/* Endpoint quiescence (batch 7a-B, src/xhci_slot.c)                   */
/* ------------------------------------------------------------------ */

/*
 * What one endpoint - EP0 or any other - owes the hardware, and what it has been
 * shown to be doing. One structure for both kinds because the machine is
 * identical: EP0's copy lives on XHCI_DEVICE and every other endpoint's on its
 * XHCI_ENDPOINT_RECORD, and `xhciEpResolve` is what hands either to the same
 * code. Two copies of this logic keyed on `dci <= 1` is exactly the shape that
 * lets a control endpoint and an interrupt endpoint disagree about what a Stop
 * Endpoint means.
 *
 * The three NEED bits are the derived command need. Each is cleared when its
 * command is *issued* and restored if the submit is refused, which is the same
 * rollback the Configure Endpoint path uses; `BUSY` is what stops the derivation
 * naming the same endpoint again while its command is outstanding.
 *
 *   NEED_STOP     - a Stop Endpoint is owed. Set by a cancellation, a REMOVE
 *                   with work still queued, a teardown, and a reprogram.
 *   NEED_RESET    - a Reset Endpoint is owed. **Only ever set for a halted
 *                   endpoint**: "The Reset Endpoint Command may only be issued
 *                   to endpoints in the Halted state" (4.6.8 p.118), and any
 *                   other state answers Context State Error.
 *   NEED_DEQUEUE  - a Set TR Dequeue Pointer is owed, at the position the ring
 *                   now reports. It follows a stop or a reset and never leads:
 *                   "This command may be executed only if the target endpoint is
 *                   in the Error or Stopped state" (4.6.10 p.126).
 *
 * The rest are state rather than debt:
 *
 *   HALTED   - a Transfer Event asked for recovery. `GetEndpointStatus` reports
 *              `USBPORT_ENDPOINT_HALT` while it is set.
 *   STOPPED  - a Stop or Reset Endpoint has *completed*, so the xHC is not
 *              executing this ring and software may rewrite TRBs on it
 *              (4.6.9 p.119). Cleared by the doorbell that restarts it.
 *   DRAIN    - when the stop completes, every transfer still queued is answered
 *              as cancelled. Set by the paths that are giving the endpoint up.
 *   RECONFIGURE - when the stop completes, the record is re-armed for a
 *              Configure Endpoint carrying a Drop flag beside the Add: the
 *              alternate-interface change of task 7a-B.1.
 *   RESTART  - ring the doorbell once the dequeue lands, because work survived.
 *   FAILED   - the chain could not be completed. Transfers are answered with an
 *              error from here rather than refused for a retry that will not
 *              come.
 */
#define XHCI_EPQ_NEED_STOP      0x00000001UL
#define XHCI_EPQ_NEED_RESET     0x00000002UL
#define XHCI_EPQ_NEED_DEQUEUE   0x00000004UL
#define XHCI_EPQ_BUSY           0x00000008UL
#define XHCI_EPQ_HALTED         0x00000010UL
#define XHCI_EPQ_STOPPED        0x00000020UL
#define XHCI_EPQ_DRAIN          0x00000040UL
#define XHCI_EPQ_RECONFIGURE    0x00000080UL
#define XHCI_EPQ_RESTART        0x00000100UL
#define XHCI_EPQ_FAILED         0x00000200UL
/*
 * usbport has paused this endpoint, so a cancellation pass is running and the
 * endpoint is **left Stopped** rather than restarted when its position lands.
 *
 * That is the one thing this driver can do about the window batch 7a-0 measured
 * and cannot close: `AbortTransfer` runs at DISPATCH under a usbport spin lock
 * and may not wait for a command, and usbport frees the transfer and releases
 * its DMA mapping a few hundred instructions after the callback returns. If the
 * endpoint were running at that moment, the aborted TD's TRBs would name pages
 * usbport had already handed back.
 *
 * PAUSED arrives at least one of usbport's own frame gates *before* the abort
 * (SP4 `0x165ED` -> `0x16729` sets the state, `0x16996` is the callback), so
 * keeping the endpoint stopped from then until usbport says ACTIVE again covers
 * the whole pass rather than racing it.
 *
 * Cleared by `SetEndpointState(ACTIVE)`, by a submission (usbport is offering
 * work again), and by the health poll's net for the case where neither arrives.
 */
#define XHCI_EPQ_PAUSED         0x00000400UL
/*
 * The ring has changed under the xHC, so the position has to be reprogrammed
 * once the endpoint stops - a transfer was removed, a queue was drained, or a
 * halted TD has to be skipped.
 *
 * **A stop that changes nothing must not reprogram anything**, and that is not
 * an optimisation. A Set TR Dequeue Pointer destroys the partial progress of a
 * TD the endpoint stopped inside: "If software issues a Set TR Dequeue Pointer
 * Command that points to a TRB that had previously been partially completed TD,
 * the xHC shall treat that TRB as the first TRB of the TD. i.e. any prior state
 * associated with a partially completed TRB is lost" (4.6.10 p.129). Left alone,
 * the endpoint resumes exactly where it stopped, because the stop wrote its own
 * position into the Endpoint Context: "if the endpoint stopped after moving the
 * first 1KB of data in a 4KB TRB, then transfer related state maintained by the
 * xHC will allow it to transfer the remaining 3KB of data when the doorbell is
 * rung" (4.6.9 p.119).
 *
 * So a `SetEndpointState(PAUSED)` pre-emption that turns out to cancel nothing
 * costs one Stop Endpoint and one doorbell, and the transfer under it is
 * untouched.
 */
#define XHCI_EPQ_REPOSITION     0x00000800UL
/*
 * The position has to be programmed even though the transfer the ring's dequeue
 * pointer names survived - the endpoint's *own* dequeue pointer is not usable.
 *
 * The distinction is between the ways an endpoint stops executing. After a
 * **Stop Endpoint** the xHC has written its own final position into the Endpoint
 * Context (4.6.9 p.119), so leaving it alone resumes the partial TD exactly.
 * Two states are not like that:
 *
 *   - after a **Reset Endpoint** the position is still the one the endpoint
 *     halted on and has to move: "software shall use a Set TR Dequeue Pointer
 *     Command to advance the Transfer Ring to the next TD" (4.10.1 p.172),
 *     which 4.6.8 p.117 makes mandatory outright for a control endpoint;
 *   - an endpoint found in the **Error** state is left there by anything else:
 *     "A TRB Error condition should cause a Running Endpoint to transition to
 *     the Error state. A Set TR Dequeue Pointer Command shall be used to
 *     transition the endpoint to the Stopped state" (4.8.3 p.149).
 *
 * Both this bit and `XHCI_EPQ_REPOSITION` are a debt against the *ring*, not
 * against a command, so they survive a failed placement and a failed command and
 * are dropped only where the debt is discharged - the branch that decides no
 * command is needed, and the Set TR Dequeue Pointer that completes with Success.
 */
#define XHCI_EPQ_FORCE_DEQUEUE  0x00001000UL
/*
 * A Set TR Dequeue Pointer is outstanding **and no new position debt has been
 * incurred since it was issued**, so its success may discharge that debt.
 *
 * Without it the completion cleared `XHCI_EPQ_REPOSITION` unconditionally, which
 * silently swallowed a debt raised while the command was in flight - an abort
 * removing a second transfer during a cancellation pass is exactly that, and the
 * pointer-divergence check cannot see it because taking a transfer off the queue
 * moves no ring pointer. The stop that followed then placed nothing, and a
 * restart could execute the newly aborted TD.
 *
 * Set when the command is issued and cleared by `xhciEpOweReposition`, which is
 * the one place any position debt is raised.
 */
#define XHCI_EPQ_DEQUEUE_ISSUED 0x00002000UL
/*
 * The ring is **not software's yet**, so no TRB on it may be rewritten - only
 * the position may be programmed.
 *
 * Set for an endpoint found in the Error state. A Stop Endpoint is what
 * "transfer[s] ownership of all the TDs on the associated Transfer Ring to
 * software" (4.11.4.8), and an endpoint in Error has not had one: "A Set TR
 * Dequeue Pointer Command shall be used to transition the endpoint to the
 * Stopped state" (4.8.3 p.149). Rewriting a cancelled TD as No Ops there would
 * be modifying TRBs the xHC may still own or have prefetched.
 *
 * The rewrite is not skipped, only deferred: the command's success clears this
 * bit and raises the position debt again, so the next pass does it with the
 * ownership the first one lacked.
 */
#define XHCI_EPQ_UNOWNED_RING   0x00004000UL
/*
 * The xHC has **no Output Endpoint Context** for this endpoint, so its doorbell
 * must not be rung and no transfer may be put on its ring.
 *
 * Set where that is read out of the hardware rather than assumed: a Stop
 * Endpoint answered with Context State Error whose Endpoint Context reports
 * Disabled. Disabled is a distinct state from Stopped in that field (Table
 * 6-8), and 4.8.3 p.150 puts the doorbell out of bounds for it: "Software
 * shall not write to the Doorbell register with the DB Target field value set
 * to an endpoint that is in the Disabled state."
 *
 * It exists because `XHCI_EPQ_STOPPED` alone does not distinguish the two.
 * That bit means "software owns the ring, nothing is executing it", which is
 * true of a Disabled endpoint - and every path that restarts one reads exactly
 * that bit. The ninth review round found the sequence: a `PAUSED` stop, an
 * `ACTIVE` arriving while it is in flight (which sets `XHCI_EPQ_RESTART`), and
 * a completion that finds the endpoint Disabled left the restart honoured
 * against an endpoint the controller does not have.
 *
 * **What clears it is whichever command gives that endpoint a context back, and
 * there is one per endpoint kind.** For a non-default endpoint it is a Configure
 * Endpoint completing with Success - every one this driver issues for a record
 * carries that record's Add Context flag, the Drop beside it being the optional
 * half. **EP0 never goes through a Configure Endpoint**; its command is the
 * Address Device, either form, which "transitions the Default Control Endpoint
 * from the Disabled to the Running state" (4.8.3 p.148). Until then a submission
 * can never succeed and is *failed* rather than refused, which is this driver's
 * rule for a refusal that cannot stop being true. The bit also goes away with
 * the device record, which is zeroed as a whole.
 *
 * **The alternate-interface reconfigure is that case, not an exception to it.**
 * `XHCI_EPQ_RECONFIGURE` is handled ahead of everything else in `xhciEpStopped`,
 * so one completion can both read Disabled and start the Drop+Add that undoes
 * it. A first draft of this bit reasoned that the reconfigure path never reached
 * the branch which sets it - it does, because the bit is raised at the call site
 * - and left the endpoint permanently unusable after a successful reprogram.
 * The submit path's own `XHCI_EP_REC_PENDING`/`CONFIGURING` retry covers the
 * window in between, so nothing is failed while the command is out.
 *
 * **The EP0 half was missed the same way**, and by the same reasoning applied to
 * a different command: the setter is the shared Disabled branch and has no
 * `Record != NULL` condition, so `xhciEpResolve` hands it `dev->Ep0Quiesce` for
 * DCI 1 like any other binding - while the clear was record-only and could never
 * reach it. **A bit on the shared quiescence needs a clear for every endpoint
 * kind that shares it**, not one for the kind it was written against.
 */
#define XHCI_EPQ_NO_CONTEXT     0x00008000UL
/*
 * The FAILED beside it was written because **the controller was unavailable**
 * - suspended or failed - not because a command for this endpoint failed
 * (Phase 7 review, B3). The distinction matters at resume: a REMOVE or PAUSE
 * delivered inside a suspend window reaches `xhciEpArmQuiesce` with `RUNNING`
 * clear, which correctly refuses to claim quiescence - but a FAILED left
 * standing across a *successful restore* (which touches no device record) is
 * a permanently dead pipe whose only recovery is a client reset-pipe that
 * non-HID clients may never send. `XhciSlotResumeSweep` clears the pair on
 * that path and re-arms what the record still says it needs; an ordinary
 * command failure never carries this bit and stays failed.
 */
#define XHCI_EPQ_UNAVAILABLE    0x00010000UL

/* Every bit that means "a command for this endpoint is owed or outstanding".
 * One name rather than the four written out at each site, because the submit
 * gate and the teardown gate must agree exactly about what "quiet" means. */
#define XHCI_EPQ_INFLIGHT       (XHCI_EPQ_NEED_STOP | XHCI_EPQ_NEED_RESET | \
                                 XHCI_EPQ_NEED_DEQUEUE | XHCI_EPQ_BUSY)

/*
 * How long an endpoint may sit Stopped with work still queued before the health
 * poll rings its doorbell anyway.
 *
 * The ordinary end of that state is `SetEndpointState(ACTIVE)` or the next
 * submission; this is the net for neither arriving, and it is a net rather than
 * a timeout because the state it ends is one *this driver* entered. One second
 * is three orders of magnitude beyond the frame gate usbport's abort pass
 * actually takes - so it can only fire after that pass is over, never inside it.
 *
 * **That margin is the whole safety argument, and until task 13-R.3.5 it was
 * spent in the wrong currency.** As `XHCI_EP_RESTART_POLLS = 2` it was two
 * *polls*, which is a second only on a host that polls twice a second; the E460
 * polls at 36-80 ms, where two polls is 72-160 ms. **That is the mildest of the
 * five and it is stated as measured rather than as alarming**: 72 ms still
 * clears a one-frame gate by about seventy, so this was a margin narrowed by an
 * order of magnitude and not one that was gone. It matters because
 * `xhciEpRestartIfStopped` clears `XHCI_EPQ_PAUSED` and rings the doorbell, so a
 * net that ever did fire inside the abort pass would restart an endpoint whose
 * TD usbport is about to free - the DMA-into-reclaimed-memory hazard that bit
 * exists to prevent, and not a hazard to leave resting on a quantity whose size
 * the host decides. See the sizing
 * rule above XHCI_COMMAND_AGE_MS in src/xhci_hw.h.
 */
#define XHCI_EP_RESTART_MS      1000UL

typedef struct _XHCI_EP_QUIESCE {
    ULONG Flags;                /* XHCI_EPQ_*                              */
    /* The PollClockMs reading at which this endpoint's unbroken run of being
     * Stopped with work queued and no command in flight began - the only state
     * it can be left in that nothing else ends - and the flag that says such a
     * run is under way. In milliseconds since task 13-R.3.5; it was a count of
     * polls, which spent a one-second margin in two milliseconds on a host that
     * polls at 1 ms. */
    ULONG StoppedStamp;
    ULONG StoppedArmed;
    /*
     * The value the outstanding Set TR Dequeue Pointer carries - the ring's
     * dequeue address with its Dequeue Cycle State in bit 0, exactly as the
     * command TRB encodes it. Kept so the completion can check the ring still
     * reports the same pair: if anything moved it while the command was in
     * flight, the two dequeue pointers have diverged and the command has to be
     * reissued rather than believed.
     */
    ULONG DequeuePA;
} XHCI_EP_QUIESCE, *PXHCI_EP_QUIESCE;

/*
 * **`CONFIGURING` is a resting state as well as a transient one, and that is
 * legal** - written down because it reads as a stuck state and is
 * not. A `CONFIGURE_EP` that the command engine gives up on reaches
 * `XhciSlotCommandLost`'s final block, which fails the **device** and leaves
 * this record at `CONFIGURING` for the rest of that device's tenancy. There is
 * no transition out of it from there and none is owed: the device is
 * `XHCI_DEV_STATE_FAILED`, so nothing new is submitted against the record, the
 * whole of it is released when the device is, and a re-enumeration builds a
 * fresh one. It is bounded and self-healing, and the read that matters is that
 * a `CONFIGURING` record on a failed device is a lost command rather than a
 * command still in flight.
 *
 * Note the asymmetry with the *refusal* path, which is deliberate rather than
 * an inconsistency: a `CONFIGURE_EP` the engine **refused** downgrades the
 * record to `FAILED`, because there the device is intact and only the record's
 * own state can end the derived need. Here the device is not intact, so there
 * is nothing for the downgrade to buy.
 */
#define XHCI_EP_REC_PENDING     0
#define XHCI_EP_REC_CONFIGURING 1
#define XHCI_EP_REC_CONFIGURED  2
#define XHCI_EP_REC_REFUSED     3
#define XHCI_EP_REC_FAILED      4

typedef struct _XHCI_ENDPOINT_RECORD {
    ULONG Dci;                  /* 0 = free; DCI 0 is the Slot Context     */
    ULONG State;                /* XHCI_EP_REC_*                           */
    /* The pool entry backing `Ring`. **Meaningful only while `Dci != 0`** -
     * pool index 0 is a real entry, so there is no spare value to mean "none"
     * and the record's own liveness is what says whether this names anything. */
    ULONG PoolIndex;
    /*
     * Everything the Endpoint Context needs, computed once at the open from
     * usbport's properties and kept because the Configure Endpoint is built
     * later, in the pump, from a record rather than from a callback argument
     * usbport owns and will free. `DequeuePA`/`Dcs` are refreshed from the ring
     * at build time rather than trusted from here - they are the two fields that
     * move.
     */
    XHCI_EP_PARAMS Params;
    ULONG InvalidateOwed;       /* owes a UsbPortInvalidateEndpoint call   */
    /*
     * The next Configure Endpoint must carry a Drop Context flag for this DCI
     * beside the Add (batch 7a-B.1's alternate-interface change). Its own field
     * rather than a `Quiesce` bit because it survives the stop that precedes it
     * and is consumed by the command *after* that: "xHC behavior is undefined if
     * the Drop Context (D) flag is '0', the Add Context (A) flag is '1', and the
     * Output Endpoint Context is not in the Disabled state" (spec 4.6.6 p.106).
     */
    ULONG DropOnConfigure;
    /*
     * The parameters a reprogram is *asking* for, held apart from `Params` until
     * its Configure Endpoint succeeds.
     *
     * They are two fields because a failed command changes nothing on the
     * hardware side - "The Output Endpoint Contexts referenced by the command
     * in the Device Context shall be unchanged" (4.6.6 p.106) - so
     * overwriting `Params` at the request would leave this driver describing an
     * endpoint the xHC does not have, with no way back to the one it does.
     *
     * **Invariant (Phase 7 review, B1/B2): equal to `Params` except while a
     * reprogram is outstanding.** Initialized beside `Params` at the open;
     * re-written by every different-parameters reopen; reconciled only by the
     * commit at the Configure Endpoint's success. "They differ" is therefore
     * the durable record of an uncommitted reprogram - it is what the
     * reset-pipe recovery reads to re-arm a RECONFIGURE a FAILED chain
     * swallowed, and what makes the reopen's comparison target the endpoint's
     * *future* rather than its past. (An earlier comment said "meaningful only
     * while `DropOnConfigure` is set", which left the field unreadable in
     * exactly the window B1 is about.)
     */
    XHCI_EP_PARAMS PendingParams;
    XHCI_EP_QUIESCE Quiesce;
    PVOID EndpointExtension;    /* usbport's, or NULL                      */
    XHCI_RING Ring;
    XHCI_TRANSFER_QUEUE Queue;
} XHCI_ENDPOINT_RECORD, *PXHCI_ENDPOINT_RECORD;

typedef struct _XHCI_DEVICE {
    ULONG State;                /* XHCI_DEV_STATE_* */
    ULONG Flags;                /* XHCI_DEV_FLAG_*  */
    ULONG SlotId;               /* 0 until Enable Slot completes */
    /*
     * The root-hub port this record sits on, 1-based, **0 for a device behind
     * an external hub** (task 7b-A.3) as well as for an unbound record.
     *
     * That double meaning is deliberate and is what keeps every existing
     * root-port path correct without a second test: `xhciDevByHubPort` is how a
     * connect change, a disable, a disown and the enumerating-port scan all find
     * "the device on this root port", and a behind-hub device is not that device
     * - the hub is. A record answering both would have a port disable tear down
     * the wrong one and the enumeration scan skip a port that is in fact free.
     * What ties a behind-hub record to a root port is `RootPort` below, which
     * every path that means "the whole subtree" uses instead.
     */
    ULONG HubPort;              /* 1-based root-hub port, 0 = behind a hub */
    ULONG RootPort;             /* the xHCI port the *path* starts at */
    /*
     * Where this device sits, which is the whole of task 7b-A.3's construction
     * half and is filled from the topology graph's claim at the address-0 open.
     *
     * `Tier` is 0 for a root-port device and 1..XHCI_TOPO_MAX_TIER below a hub;
     * `RouteString` is Slot Context DW0 `19:0`, four bits per tier with the hub
     * nearest the root hub in the low nibble (docs/usb-xhci-info/xhci-data-structures.md,
     * "Route String tier order"). A path deeper than five tiers has no
     * representable route and is refused at the open rather than truncated -
     * a truncated route names a different, *real* device.
     *
     * `ParentHubAddress`/`ParentHubPort` are the immediate parent's usbport
     * address and its downstream port. They are the record's key one tier down,
     * the way `HubPort` is on a root port: a re-enumeration behind a hub has to
     * find the record it already made, and a disconnect reported by that hub has
     * to find the record it names.
     */
    ULONG Tier;
    ULONG RouteString;
    ULONG ParentHubAddress;
    ULONG ParentHubPort;
    /*
     * What **usbport** said about this device's transaction translator at the
     * EP0 open - `HubAddr` (0 here when it read 0xFFFF, i.e. "no TT") and
     * `PortNumber` beside it (batch 7a-0, `docs/usb-xhci-info/usbport-miniport-abi.md`
     * section 5).
     *
     * Kept as a **cross-check and not as the source**, which batch 7b-V0's
     * measurement forced: a Full-Speed `usb-hub` on a root port with a
     * Full-Speed device behind it reads `HubAddr` = that hub's own address, a
     * transaction translator for a hub that has none. The pair this driver
     * programs comes from `XhciTopoTtFor`, which knows each hub's decoded speed;
     * these two are what a run compares it against, through
     * `TtPairsAgreed`/`TtPairsDisagreed`.
     */
    ULONG TtClaimAddress;
    ULONG TtClaimPort;
    ULONG Speed;                /* decoded XHCI_SPEED_* at reservation */
    ULONG Psiv;                 /* the raw PORTSC Port Speed the slot carries */
    ULONG MaxPacketSize0;       /* what the EP0 context currently holds */
    ULONG WantedMaxPacketSize0; /* what the reopen asked for, once it differs */
    ULONG DeviceAddress;        /* usbport's address - unrelated to the xHC's */
    /*
     * The address the **topology graph** still knows this device by (Phase 7
     * review, finding A4). The address-0 open clears `DeviceAddress` while the
     * graph deliberately keeps the node - positions survive the recovery
     * cycle - so a teardown landing in that window used to detach address 0,
     * which is a no-op, and the node leaked for the life of the driver (eight
     * leaks end behind-hub support until StopController). Set whenever the
     * snoop path sees the graph holding a node for this record; the detach
     * helper prefers it; the SET_ADDRESS interception migrates the node and
     * re-points it. 0 for the ordinary non-hub device that never had a node.
     */
    ULONG TopoAddress;
    /*
     * Task 9-A.2: what this device's configuration descriptor said about its
     * isochronous endpoints, snooped off EP0 (src/xhci_desc.h).
     *
     * Per device rather than per endpoint because the descriptor arrives
     * **before** any non-default endpoint is opened - usbport reads the
     * configuration, the client driver selects it, and only then do the pipes
     * open - so there is nowhere else to put it. Cleared wherever a record
     * begins a new enumeration, since a configuration descriptor describes the
     * device that answered it.
     */
    XHCI_DESC_STATE IsoDesc;
    /*
     * Which tenancy of this record slot this device is (task 9-A.2's review
     * round 1). Assigned from `XHCI_EXTENSION.DeviceTenancyNext` when the
     * record is allocated, so a record released and re-allocated to another
     * device never matches a value captured before the release. It exists for
     * the descriptor channel, whose actions are decided at a transfer's
     * placement and applied at its completion - a window a teardown can sit in.
     */
    ULONG Tenancy;

    ULONG PendingOp;            /* XHCI_DEV_OP_*, owed */
    ULONG ActiveOp;             /* XHCI_DEV_OP_*, outstanding on the ring */
    /*
     * Which endpoint the outstanding **per-endpoint** command is for - the
     * Configure Endpoint of task 7a-A.1 and the three quiescence commands of
     * batch 7a-B. One field for all four because the engine allows one command
     * at a time, so at most one of them is ever outstanding; the record pointer
     * itself must not be carried across the unlock the submit needs, and this
     * DCI is what the completion resolves it back through.
     *
     * Unread for every other op, because they are about the device rather than
     * about one of its endpoints.
     */
    ULONG EndpointOpDci;
    /*
     * Task 7b-A.2's hub marking, as XHCI_HUBMARK words.
     *
     * `HubMarkDone` is what a *completed* command programmed into the Output
     * Slot Context; `HubMarkIssued` is what the one now in flight will program,
     * captured when the Input Context was built because the record pointer may
     * not be carried across the unlock the submit needs (the same reason
     * `EndpointOpDci` exists).
     *
     * `HubMarkDone` is cleared by every Address Device that succeeds, and that
     * is not defensive: "Any Output Slot Context is 'valid' for subsequent
     * Address Device Commands because **all fields of the Output Slot Context
     * are overwritten by the xHC**" (6.2.2.1 p.412), against an Input Slot
     * Context whose validity list requires "all other fields are cleared to
     * '0'" - so a re-addressed hub is an unmarked hub. usbhub re-enumerates a
     * hub on this driver today (batch 7b-A.1.0's recovery cycle), so the path is
     * measured rather than hypothetical.
     */
    ULONG HubMarkDone;
    ULONG HubMarkIssued;
    /* The PollClockMs reading this ActiveOp's budget is measured from, and the
     * flag that says it has been taken. In milliseconds since task 13-R.3.5 -
     * it was a poll count, which on the E460 made XHCI_DEV_AGE_POLLS a 128 ms
     * budget rather than a 64 s one. */
    ULONG OpAgeStamp;
    ULONG OpAgeArmed;
    ULONG LastCompletionCode;   /* the last command completion this record saw */
    /*
     * Task 7b-A.0's progress detector, read once per health poll and cleared by
     * it. `RefusedSincePoll` is set by any transfer this record declined for
     * retry, `SubmittedSincePoll` by any transfer actually placed on one of its
     * rings, and `StallStamp` is the PollClockMs reading at which the current
     * unbroken run of polls that saw the first without the second, while no
     * command was in flight, began. `StallArmed` says such a run is in progress,
     * and any poll that is not stuck clears it - which is what keeps the measure
     * over a *consecutive* run rather than over the life of the record.
     *
     * **In milliseconds since task 13-R.3.5**, where it was a count of polls.
     * The budget batch 7b-V0 calibrated was five seconds; on the E460 the
     * ten-poll form spent it in about ten milliseconds. See PollClockMs.
     *
     * Two flags rather than one, because "refusing" alone is not a fault: a full
     * ring refuses and drains, and an endpoint quiescing refuses and resumes.
     * What distinguishes a stuck record is that **nothing moved** - so the
     * detector needs the positive observation, not just the negative one, and
     * the `ActiveOp` test keeps it from ever firing inside the command ladder
     * that would have resolved the wait properly.
     */
    ULONG RefusedSincePoll;
    ULONG SubmittedSincePoll;
    ULONG StallStamp;
    ULONG StallArmed;

    PVOID EndpointExtension;    /* usbport's EP0 endpoint extension, or NULL */
    /*
     * The intercepted SET_ADDRESS (task 6-B.3). It is a usbport transfer that
     * never reaches a transfer ring, so it is held here rather than in the EP0
     * queue - which is keyed on TRB ranges and has nothing to key this on - and
     * completed when the Address Device command answers.
     */
    PXHCI_TRANSFER PendingSetAddress;

    XHCI_RING Ep0Ring;
    XHCI_TRANSFER_QUEUE Ep0Queue;
    /*
     * EP0's quiescence state (batch 7a-B). It sits here rather than in
     * `Endpoints[]` because EP0 has no record - its ring and queue are the
     * device's - and it is the same structure the records carry so that one
     * machine serves both.
     */
    XHCI_EP_QUIESCE Ep0Quiesce;

    /*
     * Non-default endpoints (batch 7a-A.1). Fixed-size because
     * XHCI_MAX_DEVICE_ENDPOINTS is the declared per-device cap - a device asking
     * for more is refused at OpenEndpoint, so there is nothing to grow into.
     *
     * `Dci` is 0 for a free slot rather than a separate flag: DCI 0 is the Slot
     * Context and is never an endpoint, so it cannot collide with a real value
     * the way "index 0" would.
     */
    XHCI_ENDPOINT_RECORD Endpoints[XHCI_MAX_DEVICE_ENDPOINTS];
} XHCI_DEVICE, *PXHCI_DEVICE;

typedef struct _XHCI_EXTENSION {
    ULONG Signature;

    ULONG Flags;

    /* Copied from USBPORT_RESOURCES at StartController. Kept because the
     * resources pointer itself is only valid for the duration of that call. */
    ULONG ResourcesTypes;
    ULONG_PTR ResourceBase;         /* mapped BAR0 VA - no MMIO until Phase 4 */
    ULONG IoSpaceLength;
    ULONG_PTR StartVA;              /* controller common buffer               */
    ULONG StartPA;
    ULONG InterruptVector;
    ULONG InterruptLevel;
    ULONG InterruptMode;

    /* Result of carving the common buffer, from the real capability registers
     * (Phase 4 task 2; Phase 3 computed it from the declared limits alone). */
    XHCI_HC_LAYOUT Layout;
    ULONG LayoutStatus;

    /*
     * What the controller says it is, and the three derived register bases
     * every MMIO accessor in src/xhci_pci.c adds to ResourceBase. Valid only
     * once HcInfoStatus is XHCI_HC_OK.
     */
    XHCI_HC_INFO HcInfo;
    ULONG HcInfoStatus;

    /* PCI identity and the interrupt-delivery gate (config 0x00 and 0x3D).
     * InterruptPin 0 means the controller is MSI/MSI-X-only, which neither
     * target's line-based stack can ever service. */
    ULONG PciVendorDevice;
    ULONG InterruptPin;

    /* Byte offset from BAR0 of the USB Legacy Support capability, or 0 if the
     * controller has none. Also reported to usbport as LegacySupport. */
    ULONG LegacyCapOffset;

    /*
     * Which logical ports this driver manages, from the Supported Protocol
     * capabilities (Phase 4 task 3). Read-only once a start has succeeded:
     * Phase 5's port shadow, the root-hub port count, and every Enable Slot's
     * Slot Type are all derived from it. Valid only when PortMapStatus is
     * XHCI_CAPS_OK.
     *
     * This is the **post-reset** parse and nothing else. PortMapStatus
     * describes this structure alone, so it stays a refusal until
     * XHCI_INIT_STEP_PORT_MAP_RECHECK has written both: a preflight that
     * succeeded says nothing about a map that pass has not produced yet, and on
     * a restart the bytes here are still the previous start's reading.
     */
    XHCI_PORT_MAP PortMap;
    ULONG PortMapStatus;
    /*
     * The preflight parse, which is what lets an unusable topology be refused
     * before the controller has been claimed or reset. It is kept only until
     * the post-reset parse has been compared against it field for field
     * (XhciPortMapEqual); nothing downstream reads it.
     *
     * A second ~1 KB structure in the extension rather than a copy on the
     * stack: usbport allocates the extension from nonpaged pool once per
     * controller, and StartController's stack is not a place to put a kilobyte.
     */
    XHCI_PORT_MAP PreflightPortMap;

    /*
     * The root hub usbport presents, derived from PortMap once per start
     * (Phase 5 task 2). Rebuilt by every start and every resume
     * reinitialization, because the map it is derived from is.
     *
     * It is ~2.5 KB of the miniport extension, which usbport allocates from
     * nonpaged pool once per controller. Indexed by hub port rather than by
     * xHCI port so the ports that have no entry are the ones this driver does
     * not manage, and sized at XHCI_MAX_ROOT_PORTS for the same reason the port
     * map is: MaxPorts is an eight-bit field and refusing a controller for
     * having more ports than a smaller array would fit is a refusal on the
     * driver's convenience rather than on evidence.
     */
    XHCI_ROOT_HUB RootHub;

    /*
     * What the root-hub callback family has done (Phase 5 task 1). Counters
     * rather than a log, for the same reason every other block here is: a free
     * build has no trace channel, and these are the only reading of it.
     *
     * `RhPortStatusQueries` and `RhHubStatusQueries` separate the two callbacks
     * the status-change scan drives; a scan that is running at all shows here
     * even when nothing ever changes, which is what distinguishes "usbport is
     * not polling" from "usbport polls and there is nothing to report".
     * `RhInvalidPort` counts port indices outside the managed range - usbport
     * validates on the class-command path only, so this is the driver's own
     * bound and it should stay at zero.
     * `RhRefusals` counts operations answered MP_STATUS_NOT_SUPPORTED; a
     * refusal is never MP_STATUS_FAILURE, whose value usbport maps to "no
     * changes" and which would leave an endpoint-0 request queued forever.
     * `RhSpeedsSeen` is the set of speed classes this start has ever decoded -
     * XHCI_RH_SEEN_LOW/FULL/HIGH, sticky, cleared only by usbport zeroing the
     * extension before a StartController. It exists because Phase 5 task 7
     * reports every connected port to usbport as High Speed, so the decode is
     * invisible in anything usbport is told (`RH_GetPortStatus`'s trace reads
     * 0x0503 for every speed) and this is the only remaining evidence of it,
     * which the Phase 5 checkpoint requires per speed. XHCI_RH_SEEN_HIGH is the
     * negative control that says an all-HS bus overrode nothing.
     *
     * **Sticky, and that is the whole point.** Every trace macro is bounded by a
     * per-site driver-image static that no start, stop or resume resets, so a
     * witness whose value moves for reasons unrelated to the question has spent
     * its budget by the time it is asked. Four shapes were tried and three
     * failed on exactly that: a cumulative count moved on every resume; a tally
     * of what is attached *now* moved too, because a resume genuinely takes the
     * bus down (HCRST clears PP on every port, the ports are re-powered, and a
     * device is not re-detected the instant the seed reads it); and *any*
     * periodic print of this failed once a second controller existed, since a
     * macro's state is per expansion and therefore per driver image while these
     * values are per controller. Win98 idle-suspends within about half a second
     * of a start, so the churn repeats indefinitely. A **monotone** set changes
     * at most three times for the life of a start, whatever the bus does
     * afterwards, and cannot be exhausted by churn on any number of ports or
     * controllers - which is why it is traced only from its one-shot site in
     * xhciRhRefresh and printed nowhere periodically.
     */
    ULONG RhPortStatusQueries;
    ULONG RhHubStatusQueries;
    ULONG RhInvalidPort;
    ULONG RhRefusals;
    ULONG RhSpeedsSeen;
    /*
     * How many times the first-decode trace site actually fired. It must equal
     * the number of bits in `RhSpeedsSeen`, and it exists so that the *gate*
     * is testable rather than only the value: `RhSpeedsSeen` is a set, so an
     * OR is idempotent and a site that fired on every decode instead of the
     * first would leave the set identical while spending the trace budget the
     * whole design is about. The host suite compiles trace macros away, so
     * counting the firings is the only way to see that from a test.
     */
    ULONG RhFirstDecodes;
    ULONG RhPortsPowered;
    ULONG RhPortsUnpowered;
    ULONG RhPortsDisabled;
    ULONG RhPortsSuspended;
    /*
     * The two asynchronous operations (Phase 5 task 4), and each one is three
     * counters because the three outcomes have three different diagnoses.
     *
     * `RhPortsReset` / `RhPortsResumed` count the operations *started*;
     * `RhResetsCompleted` counts the PRC that finished a reset and
     * `RhResumesCompleted` the terminating U0 write that ended a resume's
     * signalling interval. A gap between a start and its completion is an
     * operation that is still in flight or one that was retired by a stop.
     *
     * `RhResetTimeouts` is a reset whose PRC never arrived within the deadline -
     * the watchdog then reports C_PORT_RESET anyway, because usbhub reads the
     * port status beside it and a port that came back not-enabled is the honest
     * answer. `RhResumesAbandoned` is a resume whose device left mid-interval,
     * where the terminating write is deliberately *not* issued.
     *
     * `RhPortsBusy` counts operations refused because that port already had one
     * armed, which usbport should never produce and which is therefore a reading
     * about usbport rather than about the hardware. `RhStaleTimers` counts
     * callbacks that claimed nothing - the ordinary shape of an uncancellable
     * timer, and expected on every stop.
     */
    ULONG RhPortsReset;
    ULONG RhResetsCompleted;
    ULONG RhResetTimeouts;
    ULONG RhPortsResumed;
    ULONG RhResumesCompleted;
    ULONG RhResumesAbandoned;
    ULONG RhPortsBusy;
    ULONG RhStaleTimers;
    /* Operations refused, or disarmed again, because there was no async timer
     * service to time them with. Its own counter rather than a share of
     * `RhRefusals` because it is a statement about usbport rather than about
     * this port: `UsbPortRequestAsyncCallback` is the only deferred-work tool
     * Option A sanctions, and without it neither asynchronous port operation can
     * be performed at all. */
    ULONG RhTimerFailures;
    /*
     * Armed operations the health poll had to retire because they had outlived
     * every legitimate timer - the one failure `UsbPortRequestAsyncCallback`
     * cannot report, since it answers 0 whether it armed a callback or failed to
     * allocate one. A nonzero value here means a port would otherwise have been
     * armed for the life of the driver, refusing every later reset.
     *
     * `RhOperationsPreempted` is the other direction: an operation still in
     * flight when usbport took the port out from under it with a power-off or a
     * disable. Its own counter rather than a share of `RhOperationsRetired`,
     * because that one is a teardown catching an operation in flight and this
     * one is ordinary hub traffic - the same reading, two diagnoses. A preempted
     * *reset* is counted here and **not** in `RhResetTimeouts`: it did not time
     * out, it was ended by the write that took its port out of service.
     *
     * `RhResetsUnconfirmed` is the case where the interrupting write was **not
     * observed** to have taken the port out of service - the read-back still
     * showed `PP` set. Two quite different things land here and neither is a
     * fault: a power-off the port has not acted on yet, which the specification
     * names as the ordinary reason PP is slow ("the PP flag may be delayed in
     * reflecting this change, e.g. due to waiting for a port related state
     * machine to complete reset signaling", footnote 91, p.375), and a *disable*,
     * which can never end a reset at all because PED is already `0` while `PR`
     * is set. Nothing is reported in either case and the reset keeps its own
     * watchdog.
     */
    ULONG RhAgeRetires;
    ULONG RhOperationsPreempted;
    ULONG RhResetsUnconfirmed;
    /*
     * An interrupting write that was **not observed to have taken effect** - a
     * `PP = 0` or a `PED` disable still in flight - so it has ended nothing and
     * whatever is armed on that port is still what will finish it. Distinct from
     * `RhResetsUnconfirmed`, which is a write that *did* take effect and simply
     * cannot end a reset.
     */
    ULONG RhPreemptsUnconfirmed;
    /*
     * Terminating writes a resume could not issue because a Port Power change
     * was in flight, so the operation was re-timed instead of being counted
     * complete. T(DRSMDN) has a floor and no ceiling, which is what makes
     * waiting the safe answer here.
     */
    ULONG RhResumeRetries;
    /*
     * PORTSC writes this driver held back because a Port Power change it had
     * issued was still in flight, and the ports it eventually gave up waiting
     * on. Both are readings about the *controller's* timing rather than about
     * this driver: a machine whose PP always lands at once leaves both at zero,
     * and a nonzero `RhPortPowerStuck` is a port whose PP never reached the
     * value it was written - at which point the wait is abandoned, because
     * refusing every operation on that port for ever is worse than one write
     * made without the confirmation.
     */
    ULONG RhPortPowerPending;
    ULONG RhPortPowerStuck;
    /* Operations retired by a stop, a suspend or a reinitialization before they
     * completed. Expected on every teardown that catches a reset in flight, and
     * the reason a later "the reset never finished" is not a defect report. */
    ULONG RhOperationsRetired;
    /* Ports the *start* found in U3 or Resume and drove back to U0 (Phase 5
     * task 6): "the xHC shall not automatically transition a root hub port from
     * the Resume or U3 state to the U0 state" (4.15, p.254). Expected to stay at
     * zero while a resume is a full reinitialization, and the counter is how
     * that expectation is checked rather than assumed. */
    ULONG RhPortsDriventoU0;
    /*
     * Times that pass was **not** run because a CSS/CRS restore had just
     * succeeded (audit finding A5). The pass's whole premise is that HCRST has
     * defaulted every port link state a moment earlier, and a successful restore
     * is exactly the path with no HCRST on it - so a port usbhub deliberately
     * suspended would still be in U3 and would be resumed, waking a device
     * nobody asked to wake and latching an unrequested `C_PORT_SUSPEND`.
     *
     * Expected 0 on every vehicle that exists today: QEMU implements CRS as "set
     * SRE" and fails every restore, so this needs bare-metal FSC >= 1. That is
     * why the counter is here - a nonzero value is the first evidence that the
     * restore path has ever been taken at all.
     */
    ULONG RhU3PassSkippedAfterRestore;
    ULONG RhChangesCleared;
    ULONG RhChirps;
    /* The notification gate's two edges. A disable with no matching enable is
     * ordinary (the scan's early return and both non-empty exits bypass the
     * enable), so these two are expected to differ - which is precisely why
     * they are two counters. */
    ULONG RhIrqGateCloses;
    ULONG RhIrqGateOpens;

    /*
     * Port Status Change Events, as the shadow saw them (Phase 5 task 2).
     * `PortEventsMapped` is the ones that named a managed port and reached the
     * shadow; `PortEventsUnmapped` the ones that named a port this driver does
     * not manage - a USB 3.x port whose PP this driver deliberately took away
     * can still report, so a nonzero count here is information rather than a
     * fault. `PortEventChanges` counts the events that latched at least one
     * hub-class change.
     *
     * `RootHubInvalidatesOwed` is the number of latched changes that nothing
     * has yet reported to usbport, and **Phase 5 task 5 is what drains it into a
     * `UsbPortInvalidateRootHub` call** - from outside the controller lock,
     * since that is a usbport service, and since the service calls
     * `RH_DisableIrq` back into this miniport, which takes that same
     * non-recursive lock.
     *
     * One call drains every owed change rather than one call each: the
     * invalidation makes usbport re-poll *all* ports, so a second one would ask
     * for a scan that the first already covers.
     *
     * `RootHubInvalidates` counts the calls, and `RootHubInvalidatesGated` the
     * drains suppressed because `XHCI_EXT_FLAG_RH_IRQ` was closed. The second is
     * not a lost announcement: usbport closes that gate when it takes a
     * notification, so a closed gate means a scan of every port is already
     * outstanding and it will read this shadow when it runs.
     *
     * **Two of the three latch sites feed it, and which two is the point.** The
     * event path and the start/resume seed both latch into a shadow nobody is
     * looking at, so a change either gets announced or is lost - and it is lost
     * for good, because both acknowledge the PORTSC bit as they go and the
     * controller does not repeat itself. `RH_GetPortStatus` is the exception: it
     * latches and reports in the same call, so it owes nothing.
     */
    ULONG PortEventsMapped;
    ULONG PortEventsUnmapped;
    ULONG PortEventChanges;
    ULONG RootHubInvalidatesOwed;
    ULONG RootHubInvalidates;
    ULONG RootHubInvalidatesGated;
#ifdef XHCI_FIX_PORT_POLL
    /* Bench candidate W10: changes a polled sweep found that no Port Status
     * Change Event had announced. Present only under the define. */
    ULONG RhPolledLatches;
#endif
#ifdef XHCI_FIX_PORT_POLL_SLOW
    /* Bench candidate W15: health polls seen since the sweep last ran. In the
     * extension rather than a driver-image static so a restart restarts the
     * cadence with everything else, and so two controllers do not share one
     * phase. Like every candidate field, this moves MiniPortExtensionSize -
     * **do not read counters from a binary built with it**. */
    ULONG RhSweepPollsSeen;
#endif
#ifdef XHCI_FIX_RH_GATE
    /*
     * **EXPERIMENTAL, bench candidate W7 for Finding 3.** Present only under the
     * define, so no shipping flavour carries it and no shipping
     * MiniPortExtensionSize moves - a binary built with it must not be used for
     * a counter reading.
     *
     * Consecutive health polls that found an announcement owed while the
     * notification gate was shut. See XhciRhGateWatchdog.
     */
    ULONG RhGateStuckPolls;
#endif

    XHCI_RING CommandRing;
    XHCI_EVENT_RING EventRing;

    /*
     * Why xhciInit refused - the failing step (XHCI_INIT_STEP_*) and the code
     * that step produced. A release build has no trace channel, so this pair
     * is the only record a crash dump or a debugger can read back.
     */
    ULONG InitStep;
    ULONG InitStatus;

    /*
     * What the port-power step did (Phase 4 task 5). Three counters rather than
     * one because they answer three different questions and only one of them is
     * a fault: how many managed USB 2.0 ports this driver drove to PP = 1, how
     * many USB 3.x ports it drove to PP = 0 - the port strategy in AGENTS.md is
     * not passive, since PP defaults to asserted on every port after HCRST
     * (spec 4.19.4, p.295) - and how many ports never reached the state they
     * were written to. The last one is per-port hardware misbehaviour, traced
     * and counted but never a refusal: one dead port is not a dead controller.
     */
    ULONG PortsPowered;
    ULONG PortsUnpowered;
    ULONG PortPowerFailures;

    /*
     * The other end of the same step (Phase 4 task 8). "Before the xHC driver
     * is unloaded, the driver should clear the Port Power (PP) flag of all Root
     * Hub ports to place them into the Disabled state and reduce port power
     * consumption" (4.19.4, p.296), and PORTSC may only be written while the
     * controller runs (5.4.8, p.370) - so this happens on the way *into* the
     * teardown, before the halt, and `TeardownCount` counts the ordered
     * teardowns that reached that point rather than the StopController calls.
     *
     * The two skip counters are the honest third and fourth answers, and
     * neither is a fault: a controller that is not running has no legal PORTSC
     * write available at all. They separate "left powered because the hardware
     * would not take it" (PortTeardownFailures) from "left powered because
     * writing would have been illegal", which look identical in the port state
     * and have opposite diagnoses - and then separate that second case again,
     * because its two causes do too:
     *
     *   PortTeardownSkippedSuspended - the stop arrived on a controller a
     *   suspend had already halted. This is the **measured ordinary shutdown**
     *   (SuspendController -> DisableInterrupts -> StopController, see
     *   docs/contributing/lessons.md), so a steadily rising count here is the expected shape
     *   and not a defect report. A release build has no trace, so without
     * its own
     *   counter this case is indistinguishable from a broken start.
     *
     *   PortTeardownSkipped - the controller was never run: a start refused in
     *   the preflight or at the run step, or a stop after a stop.
     */
    ULONG TeardownCount;
    ULONG PortsUnpoweredAtStop;
    ULONG PortTeardownFailures;
    ULONG PortTeardownSkipped;
    ULONG PortTeardownSkippedSuspended;

    /*
     * What the quiesce path could not prove. `QuiesceFailures` counts the times
     * XhciQuiesceController returned without being able to say the xHC can no
     * longer reach the common buffer - neither halted nor confirmed off the bus -
     * which is the state a later "unexplained" corruption bugcheck would want to
     * find recorded. `BusMasterCleared` says this driver took PCI Bus Master
     * Enable away as that path's last resort, so the next start knows to put it
     * back; nothing else in the driver writes PCI configuration space.
     */
    ULONG QuiesceFailures;
    ULONG BusMasterCleared;

    /*
     * `BusMasterClearRetries` counts fallback attempts that did not prove the
     * bit clear, so a machine that needed a second config cycle is separable
     * from one where the bit was simply stuck - the two look identical in
     * `QuiesceFailures` and only one of them is hardware misbehaving.
     *
     * `DmaFailClosed` counts the times that failure was answered with
     * XhciFailClosedDma on a path after which usbport reclaims the common
     * buffer. It is written *before* the bugcheck, so on the targets it is only
     * ever readable as a nonzero value from a crash dump or from the trace, and
     * `DmaFailClosedUnavailable` is the case that keeps running: no
     * UsbPortBugCheck slot, buffer reclaimed anyway. A nonzero value there is
     * the evidence a later unexplained corruption would want.
     */
    ULONG BusMasterClearRetries;
    ULONG DmaFailClosed;
    ULONG DmaFailClosedUnavailable;
    /*
     * Task 13-R.1's third outcome, and it is a separate number because it is a
     * separate finding: the failure was answered on a path where **nothing
     * reclaims the buffer** - the in-place recovery - so the bugcheck is not
     * owed and the machine keeps running with the block still this driver's.
     * Folding it into `DmaFailClosedUnavailable` would have said "the buffer was
     * reclaimed under a live bus master", which is the one thing that did not
     * happen.
     */
    ULONG DmaFailClosedDeferred;

    /*
     * The suspend/resume pair (src/xhci_init.c). The suspend masks the interrupt
     * enables and halts; the resume reinitializes, so ResumeReinits normally
     * tracks SuspendCount exactly and a gap between them is the interesting
     * signal. SuspendFailures counts suspends where the controller would neither
     * halt nor give up bus mastering - i.e. it may have entered D3 still capable
     * of DMA. On Win98 these grow steadily by design: NUSB's usbport issues
     * suspend/resume pairs repeatedly at idle, where native Win2000 usbport
     * never idle-suspended at all, so a steadily rising SuspendCount on one
     * target and a static one on the other is the expected shape.
     */
    ULONG SuspendCount;
    ULONG SuspendUsbCmd;
    ULONG SuspendFailures;      /* would neither halt nor drop bus mastering */
    ULONG ResumeReinits;
    ULONG ResumeFailures;

    /*
     * Spike counters. Written without a lock on purpose: they are diagnostics,
     * every documented call site is already serialized by one usbport lock or
     * another, and a torn count costs nothing. Nothing in the driver branches
     * on them. FrameNumber is the exception that is also functional - see
     * Get32BitFrameNumber in xhci_dispatch.c.
     */
    ULONG FrameNumber;
    ULONG InterruptCount;
    ULONG DpcCount;

    /*
     * What the interrupt path has seen (Phase 4 task 4, src/xhci_evt.c). Same
     * unlocked-diagnostic rule as the counters above, with one extra reason to
     * keep it that way: LastIsrStatus is written at DIRQL, and the ISR is the
     * one context in this driver where a lock would have to be an interrupt
     * spin lock rather than usbport's.
     *
     * Release builds have no trace channel, so these are how a post-mortem
     * answers "did a Port Status Change event ever arrive" and "did the drain
     * ever hit its bound" - the questions the Phase 4 checkpoint is about.
     */
    ULONG InterruptsClaimed;    /* of InterruptCount, the ones that were
                                 * ours - the other half of the shared-line
                                 * question InterruptCount alone cannot answer */
    ULONG LastIsrStatus;        /* USBSTS as the ISR last read it        */
    ULONG EventsTotal;          /* events consumed since the driver load */
    ULONG EventCounts[XHCI_EVENT_TYPE_COUNT];   /* by TRB type, 32..39   */
    ULONG EventsVendor;         /* type 48..63: legal, ignored (4.11.6)  */
    ULONG EventsUnknown;        /* a type the event ring cannot carry    */
    ULONG DrainBoundHits;       /* DPC passes that stopped at the bound  */
    /*
     * The subset of those whose ring was **empty anyway** - the bound and the
     * last event coincided. The drain loop tests its bound before it dequeues,
     * so such a pass leaves without ever asking the ring, and task 9-0.2's
     * settle used to be skipped on it; with the ring empty, IPE never
     * re-asserts (4.17.5 p.270) and no later DPC was guaranteed to come, which
     * stranded the deferred transfer for the life of the endpoint. The settle
     * now gates on the ring being observed empty rather than on the exit taken,
     * and this counts how often the two disagree - a number expected to be
     * small and nonzero under load, and the one that says this path is real
     * rather than theoretical.
     */
    ULONG DrainBoundEmptyHits;
    ULONG DpcsAfterFailure;     /* DPC arrivals refused by ControllerFailed */
    ULONG LastPortEventId;      /* Port ID of the last port-change event */
    ULONG LastHostControllerCode;   /* completion code of the last HCE   */

    /*
     * Host Controller Events escalate from the DPC rather than waiting for a
     * poll: Event Ring Full and Event Lost set neither USBSTS.HCE nor HSE, so
     * no register poll will ever see them, and a lost event is never resolved by
     * a later one (XhciEventDpc's caller in src/xhci_evt.c). It is the only
     * *controller-level* report of them; a TD-related Event Lost is also
     * reported per endpoint as a Transfer Event with code 32, which the transfer
     * path escalates for the same reason (audit round 7). The count is separate
     * from EventCounts because it says how many of them turned into a reset
     * request rather than how many arrived.
     */
    ULONG HostControllerEventResets;

    /*
     * usbport's three interrupt callbacks (Phase 4 task 6, src/xhci_evt.c).
     * The first two are ordinary bookkeeping; the other two each answer a
     * question nothing else in the driver can.
     *
     * InterruptFlushes is the *whole* behaviour of that callback, which touches
     * no register (see XhciFlushInterrupts). Its call site is known from the
     * shipping binaries - the D0 power completion, holding neither miniport
     * lock - so the count is the runtime confirmation of that reading: a free
     * build that has reached D0 with this still at zero would mean the
     * disassembly was misread.
     *
     * EnablesWithEventsPending counts the enables that found Event Handler
     * Busy already set, i.e. the controller had raised IP and filled the event
     * ring before usbport ever allowed an interrupt. Since task 7 that is the
     * ordinary case on *every* machine rather than only on one with a device
     * plugged in at boot: a start ends by issuing the No Op self-test, whose
     * completion arrives while interrupts are still masked. (The device-attached
     * case is still there too - port power runs while the controller is already
     * running and a present device produces its Port Status Change Event there,
     * 4.19.4 p.295.) It is counted because it is the state that makes an
     * acknowledge-on-enable fatal - see XhciEnableInterrupts.
     */
    ULONG InterruptEnables;
    ULONG InterruptDisables;
    ULONG InterruptFlushes;
    ULONG EnablesWithEventsPending;

    /*
     * InterruptNextSofRequests is the *whole* behaviour of that callback too,
     * and it exists because the trace channel was the wrong instrument for the
     * question (task 9-A.3). XHCI_DBG_CB is budgeted per site, so a debug
     * build shows the same handful of lines whether the callback fired four
     * times or forty thousand, and batch 9-V read that as "it has never fired
     * before". A release-build counter is what can answer "how often".
     *
     * **It is NOT one per endpoint state change**, which a first draft of this
     * comment claimed. usbport has four SetEndpointState call sites per build
     * and only one of them - USBPORT_SetEndpointState, the one that stamps the
     * endpoint with a frame number and queues it on the state-change list -
     * asks for this callback; the other three call the miniport directly and
     * queue nothing. So this counter is below SetEndpointStates rather than
     * equal to it, and what it is worth reading against is the *walker*: the
     * excess over the queueing state changes is usbport re-asking because it
     * still cannot retire the head endpoint, i.e. a published frame number that
     * is not moving. See xhciInterruptNextSOF and docs/usb-xhci-info/usbport-miniport-abi.md.
     */
    ULONG InterruptNextSofRequests;
    /*
     * An interrupt-enable transition that did not complete cleanly. Counted per
     * call, not per register or per retry.
     *
     * **A mask counts here only when it ran out of attempts without proving
     * delivery suppressed** - the swallowed-write case, where every operand
     * looked healthy. That is the reading with teeth: the enables may still be
     * up, so the ISR can decline a still-asserted level-triggered INTx without
     * acknowledging it, which is the shared-line livelock the mask order exists
     * to prevent. An unmask counts when the enables did not come back up. Both
     * are diagnostic only and never admission gates; CheckController traces
     * them.
     */
    ULONG InterruptMaskFailures;
    ULONG InterruptUnmaskFailures;
    /*
     * A mask that met an all-ones operand window mid-loop, so at most one of
     * the two enables was written from a validated read.
     *
     * **Split out of InterruptMaskFailures**, because the two were opposite
     * verdicts on the same hazard sharing one counter, which the repo's own
     * rule forbids in a release build. A degraded pass that still *proved*
     * delivery suppressed is a healthy outcome reached over a bad register
     * window - nothing is at risk - while a failure is the livelock hazard
     * live. `InterruptDeliverySuppressed` disambiguated them only until the
     * next lifecycle activity moved it.
     *
     * Counted per call, and independently of the outcome: a pass can be
     * degraded *and* fail, and both readings are wanted.
     */
    ULONG InterruptMaskDegraded;
    /*
     * Interrupter re-arms (XhciRearmInterrupter) that could not prove IMAN.IE
     * came back up, and the DPC escalations they caused.
     *
     * Its own pair rather than a share of the unmask counters, because since
     * task 9 this is the **only** thing that restores interrupt delivery on a
     * running controller: XhciIsr clears IE on every claimed interrupt, and
     * usbport calls EnableInterrupts once after StartController and not again.
     * So a failure here is a controller that has gone permanently silent while
     * looking healthy, which is a different diagnosis from an enable that never
     * came up in the first place - and on a release build these counters are
     * the only place either is visible.
     */
    ULONG InterruptRearmFailures;
    ULONG RearmEscalations;
    /* Refused unmasks that asked usbport for a controller reset. Distinct from
     * the failure count above because only an enable on an admitted controller
     * escalates - the same refusal during a teardown needs no rescue. */
    ULONG UnmaskEscalations;
    /* Disables that returned without proving delivery suppressed, and asked
     * usbport for a reset so the state is at least recorded. Its own counter
     * because this is the path where no ISR remains to acknowledge. */
    ULONG MaskEscalations;
    /*
     * ISR entries whose IMAN reads all answered all ones, so the acknowledge
     * was made with a literal `XHCI_IMAN_IP` instead of a RsvdP-preserving
     * read-modify-write. The documented, deliberate exception to that rule -
     * see XhciIsr - and its own counter because it is a statement about the
     * *register window*, not about an enable: any nonzero reading says an
     * interrupter window stopped decoding underneath a live interrupt.
     */
    ULONG IsrImanLiteralAcks;

    /*
     * The asynchronous command engine (Phase 4 task 7, src/xhci_cmd.c).
     *
     * **The engine needs the miniport's own lock, and it is not optional.**
     * Three contexts reach this state and no usbport lock excludes any of them
     * from the others: a submit runs under `MiniportSpinLock` (or at
     * PASSIVE_LEVEL from StartController), the completion runs in the DPC under
     * `MiniportInterruptsSpinLock`, and the uncancellable timeout runs from
     * usbport's async timer DPC holding neither (docs/usb-xhci-info/usbport-miniport-abi.md
     * section 7). The ISR is deliberately *not* one of them - it stays stateless,
     * which is what keeps this a DISPATCH-level lock rather than an interrupt
     * spin lock this driver has no way to allocate.
     *
     * **That lock is not in this structure**, and the reason is the paragraph
     * below: usbport zeroes the extension before every start, so a lock kept here
     * would be re-created underneath callbacks that cannot be cancelled. It lives
     * in the driver image instead (`xhciControllerLock` in src/xhci_cmd.c),
     * created once in DriverEntry. It also serializes the failure transition
     * against command MMIO, the event-ring drain, and interrupt enable/disable.
     *
     * Lock order: it is innermost. Nothing acquires it while holding anything
     * else of this driver's, and no usbport service is called while it is held -
     * the async-timer arm and UsbPortInvalidateController are both issued after
     * it is dropped. Roadmap Phase 4 task 9 owns writing that scope down for the
     * whole driver.
     *
     * CommandGeneration is monotonic within one start and never reused, which is
     * what makes a timeout callback that cannot be cancelled safe: it claims the
     * outstanding command only when the generation it was armed with is still
     * the current one, so a stale callback is a comparison rather than a race.
     *
     * **StartEpoch is what the generation cannot be.** usbport zeroes this whole
     * extension before every StartController, so the generation restarts from 0
     * on each one and a watchdog armed by the previous start would match the
     * first command of this one. The epoch is issued from a driver-image counter
     * usbport does not touch (xhciStartEpoch in src/xhci_cmd.c) and recorded here
     * by XhciCommandInit; every timer context carries it, and the callback
     * validates it with the extension bracket under the stable controller lock.
     * Zero is never issued, so a zeroed extension - cleared by usbport and not
     * yet started - matches no context either.
     */
    /*
     * **The controller is unrecoverable.** Set by ResetController - the callback
     * that learns the ladder has ended - and read by every path that could
     * otherwise keep touching a controller nobody is going to fix: command
     * submission, the command watchdog, the command-completion arm, the ISR, the
     * DPC, and EnableInterrupts. Deliberately *not* read by the stop, quiesce,
     * suspend or mask paths, which must keep working: proving DMA has stopped is
     * exactly what still matters about a failed controller.
     *
     * Its own word rather than a bit in Flags prevents a read-modify-write from
     * losing the transition. Atomic storage is not the synchronization: every
     * DISPATCH-level check and its following MMIO are under the driver-image
     * controller lock. The ISR cannot take that ordinary lock at DIRQL, so this
     * word is volatile for its entry gate. The failure transition masks both
     * enables before publishing the word, so an ISR may decline only after this
     * controller can no longer be asserting the shared level-triggered line.
     * usbport clears the word for the next start with the rest of the extension.
     */
    volatile ULONG ControllerFailed;

    /*
     * What the health poll found, and the latch that stops it asking twice.
     *
     * ControllerFatal is set the first time USBSTS reports HCE or HSE and is
     * what makes the escalation a *transition* rather than a repetition: HCE is
     * read-only and HSE is deliberately left unacknowledged - clearing an RW1C
     * bit would destroy the record on a path that has already decided not to
     * retry in place - so both stay set, and without this latch every 500 ms
     * poll would ask usbport to queue another reset.
     *
     * It is distinct from ControllerFailed, which is the *answer*: this says the
     * hardware reported a fatal condition, that says the ladder has ended.
     * LastCheckStatus is the raw word behind the decision, readable from a free
     * build; HealthPollsDead counts polls that read all ones, which is a window
     * that has stopped decoding and is deliberately not treated as a fatal-bit
     * report (see XhciControllerHealthPoll).
     */
    ULONG ControllerFatal;
    ULONG FatalStatusDetected;
    ULONG LastCheckStatus;
    ULONG HealthPolls;
    ULONG HealthPollsDead;
    /*
     * **Task 13-R.3.5: the health poll's own millisecond clock, and every age
     * and stall threshold in this driver is measured on it.**
     *
     * MFINDEX counts microframes and this driver's frame index counts frames,
     * one per millisecond, so a masked difference between two consecutive
     * readings of that index *is* an elapsed time in milliseconds. Advanced once
     * per poll by `XhciPollClockAdvance` (src/xhci_init.c), which is the only
     * writer; `PollClockFrame` is the previous reading and `PollClockSynced`
     * says there is one to subtract from.
     *
     * **Why this exists at all.** Every threshold here used to be a count of
     * `CheckController` polls, sized against usbport's nominal 500 ms timer and
     * defended with "a host that polls more slowly makes this fire later, never
     * sooner". **The E460 polls at 36-80 ms** - this clock measured 354,364 ms
     * against 6,461 polls - so the argument had the error direction backwards
     * and every one of those budgets was about an order of magnitude short
     * (docs/contributing/runs/run-13e.md, **Finding V**). The command backstop
     * came out at 2.3-5.1 s against the 5 s watchdog it was meant to sit 12 s
     * behind, so it pre-empted that watchdog every time and this driver had
     * been resetting controllers over commands that were merely slow.
     *
     * **Read the elapsed time as `PollClockMs - stamp`, never as a comparison
     * of two absolutes.** The clock is 32 bits and wraps after about 49 days;
     * an unsigned difference is correct across that wrap and a `<` between two
     * absolutes is not.
     *
     * **It does not advance while the poll declines**, because the poll's own
     * admission gate returns before this, and it does not advance while MFINDEX
     * does not - which is a halted controller. Both are deliberate: a command
     * can only be issued on a running controller, and the transition that stops
     * one goes through `XhciControllerBeginQuiesce`, which invalidates the
     * outstanding command and moves the generation on. So there is no state in
     * which a live watchdog is left with a frozen clock. `PollClockStalls`
     * counts the polls that could not read the axis, so a frozen `PollClockMs`
     * says which of the two it was rather than leaving it to be guessed.
     */
    ULONG PollClockMs;
    ULONG PollClockFrame;
    ULONG PollClockSynced;
    ULONG PollClockStalls;
    /*
     * Every CheckController entry, counted before any gate below it - which is
     * what HealthPolls cannot say. The health poll declines a controller that is
     * not INITIALIZED and returns *before* incrementing, and a suspend clears
     * that flag, so a frozen HealthPolls has two readings that matter to
     * opposite conclusions: "usbport stopped calling this miniport" and "usbport
     * kept calling and our own gate declined". Batch 11-V stage A hit exactly
     * that ambiguity - HealthPolls sat at 41 across a fifteen-minute idle
     * suspension - and the question it blocks is task 11-V.6's second candidate:
     * a timer-driven poll can only wake an idle-suspended controller if usbport
     * still calls the miniport while it holds it suspended.
     *
     * This is batch 7b-A.1.0's rule a second time: a row set assembled from
     * outcome counters is only as complete as whoever assembled it, and counting
     * the *entry* is what separates "passed the gate" from "never reached it".
     */
    ULONG CheckCallbacks;
    /*
     * This controller is known to be unable to deliver an interrupt. The ISR's
     * decline gates are conditional on it, because a gate is only safe to act
     * on once this controller can no longer be asserting the shared
     * level-triggered line - and a mask whose window answered all ones wrote
     * nothing, while one whose writes were swallowed changed nothing either, so
     * publishing a decline behind either would strand an asserted INTx with
     * nobody willing to acknowledge.
     *
     * **Either enable clear is enough, which is what the name says and
     * "EnablesProvablyDown" did not**: INTE is the global enable for all
     * interrupters and IE is this interrupter's, so one of them down means this
     * controller cannot assert. Set when a mask confirms one of them clear *by
     * reading it back*, and by HCRST, which clears both by definition; cleared
     * when an unmask confirms both back up. Zero is the conservative value, so
     * a zeroed extension makes the ISR prove ownership from USBSTS rather than
     * decline on trust.
     *
     * Volatile for the same reason ControllerFailed is: the ISR reads it at
     * DIRQL and cannot take the DISPATCH-level controller lock.
     */
    volatile ULONG InterruptDeliverySuppressed;
    ULONG StartEpoch;               /* 0 = zeroed and not yet started       */
    ULONG CommandState;             /* XHCI_CMD_STATE_*                     */
    ULONG CommandGeneration;        /* 0 = no command issued in this start  */
    ULONG CommandTrbPA;             /* the outstanding TRB, 0 when resolved */
    ULONG CommandType;              /* TRB type of the outstanding command  */
    ULONG LastCommandTrbPA;         /* the last event this driver matched   */
    ULONG CommandCompletionCode;    /* and that event's completion code     */
    ULONG CommandSlotId;            /* DW3 Slot ID - Enable Slot's answer   */

    /*
     * Counters, and each one is a different diagnosis rather than a tally.
     * CommandsUnmatched means an event named a TRB this driver does not have
     * outstanding, which on a one-at-a-time ring is either a duplicate event or
     * a pointer this driver's ring arithmetic does not agree with.
     * CommandsAbandoned counts commands dropped by a stop or a reinitialization,
     * which is expected on Win98's idle suspend/resume pairs and nowhere else.
     */
    ULONG CommandsIssued;
    ULONG CommandsCompleted;
    ULONG CommandsUnmatched;
    ULONG CommandsBadCompletion; /* a code Table 6-90 does not give a command */
    ULONG CommandsTimedOut;
    ULONG CommandsAborted;       /* Command Aborted events (code 25)         */
    ULONG CommandRingStops;      /* Command Ring Stopped events (code 24)    */
    /*
     * A Command Ring Stopped event naming a dequeue position this driver's ring
     * cannot hold. It is not repaired here: CRCR's Command Ring Pointer is bits
     * 63:6, so only every fourth TRB is an expressible restart position (Table
     * 5-24 note, p.368), and the software and hardware ideas of the ring have
     * diverged anyway. The engine escalates to a controller reset instead - see
     * xhciCommandRingStopped.
     */
    ULONG CommandRingDiverged;
    ULONG CommandsAbandoned;
    /*
     * Async callbacks that claimed nothing - and **two causes share it since
     * task 13-R.3.5**, which matters to anyone reading it off a bench. A
     * phase-COMMAND watchdog counts here when the command it was armed for has
     * already *completed*, which is Finding V's reading (123 == 123 == 123);
     * it also counts here when the health poll's age detector got to the same
     * command first and moved the state to ABORTING, because the phase no
     * longer matches the state. The discriminator is `CommandAgeAborts`:
     * nonzero means some of these are the second kind.
     */
    ULONG CommandStaleCallbacks;
    ULONG CommandTimerFailures;  /* submits refused for want of a timer      */
    ULONG CommandResetRequests;  /* UsbPortInvalidateController(RESET) calls */
    /*
     * How many of those requests came from a **completion code Table 6-90 calls
     * fatal** rather than from the ring's own structural divergence, and **audit
     * round 10 is why it exists**. Round 9 gave the new command-ring severity arm
     * `CommandResetRequests` to share, on the reasoning that both are this engine
     * asking for the one repair it has. That is true of the *request* and false
     * of the *diagnosis*: a controller that answered a command with Undefined
     * Error and one whose command ring stopped at a position this driver's ring
     * cannot hold need opposite investigations, and a release snapshot could not
     * tell them apart. `CommandCompletionCode` is not the discriminator either -
     * it is the last matched completion and later traffic overwrites it.
     *
     * `CommandResetRequests` keeps its meaning as the total, so nothing that read
     * it reads something else now; this is the part of the total that is a
     * *reported fault* rather than a *disagreement*.
     */
    ULONG CommandsFatal;
    /*
     * A stopped-family event (code 24 or 25) that arrived after the abort it
     * belongs to had already been resolved - normally because the abort watchdog
     * read CRR as clear and put the engine back in service first. Counted and
     * dropped rather than acted on: by then a *new* command may be outstanding,
     * and this event's dequeue position belongs to the ring as it was.
     */
    /*
     * Abort intervals spent waiting for a Command Ring Stopped event that had
     * not arrived when the watchdog found CRR already negated. The engine does
     * not recover on that reading alone - only the event ends an abort - so this
     * counts patience rather than a fault, and it is bounded by
     * XHCI_COMMAND_ABORT_WAITS before the ladder escalates.
     */
    ULONG CommandAbortWaits;
    /*
     * Abort attempts on which no Command Abort bit reached the controller: the
     * CRCR read gave no legal operand, or - the common case - CRR was already
     * '0', where CA is ignored and the pointer bits composed beside it are not
     * (see XhciWriteCrcrAbort). The engine does not treat either as a recovery:
     * the state still moves to ABORTING and rung 2 bounds it, so a nonzero value
     * here is a diagnosis rather than a branch.
     */
    ULONG CommandAbortsNotWritten;
    /*
     * The health poll's view of the outstanding command (XhciControllerHealthPoll,
     * src/xhci_cmd.c). CommandAgeStamp is the PollClockMs reading the current
     * command's budget is measured from and CommandAgeGeneration is the
     * generation that stamp belongs to - without the second field two commands
     * issued between two polls would share an age, and the later one would
     * inherit an age it never had.
     *
     * **In milliseconds since task 13-R.3.5, and it was a poll count before.**
     * See PollClockMs above for why that was wrong in the unsafe direction.
     *
     * CommandAgeEscalated is what keeps rung 2 to one request per crossing. The
     * poll-count form got that from testing the count for *equality* with the
     * threshold, which a clock cannot do - it advances by whatever the poll
     * period happened to be - so the latch is explicit. It is cleared wherever
     * the stamp is re-taken, which is every path that gives the ladder a fresh
     * interval: an idle engine, a new generation, and the abort rung itself.
     *
     * CommandAgeResets counts the escalations. A nonzero value means a command
     * went out with no watchdog behind it, which is the one thing
     * UsbPortRequestAsyncCallback's return value cannot tell this driver: it
     * answers 0 both on success and on its own pool-allocation failure.
     */
    ULONG CommandAgeStamp;
    ULONG CommandAgeGeneration;
    ULONG CommandAgeEscalated;
    /*
     * Task 13-R.2 split this detector into the two rungs it should always have
     * had, and the pair is read together. `CommandAgeAborts` counts commands the
     * poll aborted because nothing had timed them; `CommandAgeResets` counts the
     * ones that were still outstanding an interval *after* CA was written, which
     * is the specification's own trigger for HCRST (4.6.1.2, p.94).
     *
     * `CommandAgeAborts` nonzero with `CommandAgeResets` at 0 is the abort
     * working - a stall that resolved. Both nonzero is a ring that would not
     * stop. `CommandAgeResets` nonzero with `CommandAgeAborts` at 0 cannot
     * happen after batch 13-R's repair, and a build that shows it is a
     * pre-13-R one.
     */
    ULONG CommandAgeAborts;
    ULONG CommandAgeResets;
    /* Events and callbacks that arrived after the controller was declared
     * failed. Expected rather than alarming - an uncancellable timer and an
     * in-flight DPC both outlive the transition - and counted because "nothing
     * happened after the reset" and "nothing arrived" are different findings. */
    ULONG CommandsAfterFailure;
    /*
     * **Every watchdog callback that reached this driver's own extension**,
     * counted before any branch decides what it was for - added
     * because the E460 could not tell two very different failures apart
     * (run-13e.md, Finding T).
     *
     * Four commands there hung for more than thirty seconds each with a
     * 5,000 ms timer armed, and `CommandsTimedOut` stayed at **0** while
     * `cmd.timeout` appeared nowhere in 7,532 bytes of note ring. Two
     * explanations fit that exactly, and they need opposite investigations:
     * the callback **never arrived** (usbport's timer service dropped it), or
     * it **arrived far too late** (after the 32 s poll had already moved the
     * state on). Every other counter in this block is written *after* a branch
     * has decided which case it is, so none of them can separate the two.
     *
     * This one is incremented immediately after the signature and epoch
     * bracket - not before it, because writing into a structure that failed the
     * bracket is a write into somebody else's memory - and before every
     * decision. `CommandsIssued` minus this is the number of watchdogs that
     * were armed and never came back.
     */
    ULONG CommandTimeoutArrivals;
    /* A Command Completion Event whose Command TRB Pointer had RsvdZ bits 3:0
     * set. Masked off for the match - "Reserved" is not something to refuse a
     * real completion over - but counted, because a conforming controller does
     * not do it. */
    ULONG CommandsReservedBitsSet;

    /*
     * The No Op self-test the init sequence issues (XHCI_INIT_STEP_NOOP), which
     * is what the Phase 4 checkpoint reads: NoOpStatus is the submit's answer,
     * NoOpTrbPA the address it went out at, and a checkpoint pass is
     * LastCommandTrbPA == NoOpTrbPA with CommandCompletionCode == XHCI_CC_SUCCESS
     * - all four readable from a release build.
     */
    ULONG NoOpStatus;
    ULONG NoOpTrbPA;

    /*
     * A one-shot token for the traced (qemu) build's self-test completion
     * witness,
     * armed by the submit and consumed by the matching completion, so exactly
     * one line is traced per self-test *issuance*. Neither of the two obvious
     * substitutes works: the TRB address is a position on a reused ring, not an
     * identity, so Phase 6 traffic wrapping onto it would fire the witness; and
     * a "first completion of the start" counter misses the resume path
     * entirely, because usbport zeroes this extension before StartController
     * but a ResumeController reinitialisation - which runs its own self-test -
     * inherits the counters. Armed while the self-test is outstanding and
     * cleared with it, so an abandoned command cannot leave it set.
     *
     * **Armed inside the submit's own lock hold**, beside `CommandTrbPA` and
     * `NoOpTrbPA`. Arming it after the lock was dropped left an SMP window in
     * which the completion DPC on another CPU consumed the command first, so
     * the real completion went unwitnessed and the token was left armed against
     * one that had already happened.
     */
    ULONG NoOpWitnessArmed;

    /*
     * How many self-test completions have been matched to their own issuance -
     * one per start and one per resume reinitialisation, and never raised by
     * ordinary command traffic. This is the Phase 4 checkpoint's pointer-match
     * clause as a **release-build** reading, beside the traced build's own
     * line, and it is what makes the once-per-issuance policy host-testable.
     */
    ULONG NoOpWitnessFired;

    /*
     * usbport's ResetController callback, which task 7 turned from a trace into
     * a body because the command engine's recovery ladder ends in a request for
     * it. A release build showing CommandResetRequests rising while
     * ResetControllerCalls stays at zero is the diagnosis for "usbport did not
     * act on the invalidation", which is the one thing this driver cannot make
     * happen and the host suite cannot observe.
     */
    ULONG ResetControllerCalls;

    /*
     * **Task 13-R.1: what happens after that callback returns, and until
     * batch 13-R the answer was nothing at all.**
     *
     * `ResetController` masks the interrupt enables and latches
     * `ControllerFailed`, and its own trace text names the recovery it needs -
     * a stop/start. The batch 13-R census of both shipping `usbport.sys` builds
     * (docs/usb-xhci-info/usbport-miniport-abi.md, the two subsections after the
     * `UsbPortInvalidateController(RESET)` box) established that **nothing in
     * usbport arranges one**: `StopController`/`StartController` are reached
     * only from a PnP or power transition something outside usbport initiates,
     * the reset DPC arms nothing, and `CheckController` is a `VOID` slot from
     * which usbport collects no verdict. So the latch was terminal on any
     * machine nobody suspends and nobody disables in Device Manager, and a root
     * port that reached it stayed deaf until the next cold boot
     * (docs/contributing/runs/run-13e.md, Finding S).
     *
     * These fields are the owner that recovery step never had.
     * `RecoveryRequested` is set by the latch itself; the 500 ms health poll -
     * which keeps running, and is the only periodic context that survives the
     * latch - arms one `UsbPortRequestAsyncCallback` and sets `RecoveryArmed`;
     * that callback runs at DISPATCH_LEVEL holding **no** usbport lock, which
     * is where `XhciRecoverController` is legal and `ResetController` is not.
     *
     * `RecoveryAttempts` is bounded by XHCI_RECOVERY_MAX_ATTEMPTS. A controller
     * that will not come back after that is left latched, which is the honest
     * terminal state - and now a *measured* one rather than an unowned step.
     * `RecoveryLastStep`/`RecoveryLastStatus` carry the refusing
     * `XhciInitController` step out of a release build, because a recovery that
     * refuses says more about the machine than one that works.
     */
    ULONG RecoveryRequested;
    ULONG RecoveryArmed;
    ULONG RecoveryAttempts;
    ULONG RecoveryCompletions;
    ULONG RecoveryFailures;
    ULONG RecoveryStaleCallbacks;
    ULONG RecoveryLastStep;
    ULONG RecoveryLastStatus;

    /*
     * **What the attempt cap actually counts, corrected by the E460
     * (run-13e.md, Finding T).** The first version bounded `RecoveryAttempts`,
     * which is a *lifetime* total - and the bench read what that means: three
     * recoveries that all SUCCEEDED spent the entire budget, so an ordinary
     * fourth incident, retrying nothing, found nothing left and the port stayed
     * dead. `ResetControllerCalls 4` against `RecoveryAttempts 3` with
     * `RecoveryFailures 0` is that defect in three numbers.
     *
     * The cap was always meant to bound "a controller that will not come back",
     * so it bounds **consecutive failures**: this is incremented by a refusal
     * and reset to zero by a success, and it is the only field the arming
     * predicate reads. `RecoveryAttempts` stays the lifetime reading, because a
     * counter that resets cannot answer "how often has this machine needed
     * one".
     */
    ULONG RecoveryFailuresConsecutive;

    /*
     * Nonzero for exactly as long as an initialization sequence is running
     * **below PASSIVE_LEVEL** - which only the in-place recovery does. Two
     * things read it, and both are about services this driver may not call
     * there rather than about the recovery itself:
     *
     *   - `XhciWaitForBits` and `XhciDelayMs` stall instead of sleeping, because
     *     `UsbPortWait` is `KeDelayExecutionThread` (a hang on Win98, a Driver
     *     Verifier bugcheck on Win2000);
     *   - `XhciInitController` skips the three PCI configuration-space reads,
     *     because `UsbPortReadWriteConfigSpace` goes out to the bus driver and
     *     this project's contract for it is PASSIVE_LEVEL.
     *
     * Skipping those reads is a real narrowing and is recorded as one: the
     * identification and INTx and bus-master gates they feed were answered on
     * this same controller by the start that succeeded, and the recovery
     * re-runs the sequence on hardware that has not been re-enumerated since.
     */
    ULONG InitBelowPassive;

    /* ---------------------------------------------------------------- */
    /* Devices, endpoints and transfers (Phase 6 batch B)                */
    /* ---------------------------------------------------------------- */

    /*
     * One record per slot the driver may ever hold. XHCI_MAX_SLOTS is not a
     * convenience bound here: CONFIG.MaxSlotsEn is written from
     * Layout.MaxSlotsEn, which is clamped to it, so the xHC cannot hand out a
     * Slot ID this array has no room for, and running out of records is the
     * same event as running out of slots.
     *
     * Indexed by the driver's own index, **not** by Slot ID, because a record
     * exists before Enable Slot has answered - and taking the Slot ID from
     * anything other than the matching completion is the mistake task 6-B.2
     * names outright.
     */
    XHCI_DEVICE Devices[XHCI_MAX_SLOTS];

    /*
     * The pooled non-EP0 transfer rings (design doc 04 section 3.6). Shared
     * across every device rather than carved per slot, and guarded by the
     * controller lock like the records above.
     */
    XHCI_RING_POOL RingPool;

    /*
     * **The single command owner, and it is what makes completion matching
     * race-free.** The engine allows one command outstanding driver-wide, so
     * the slot layer never needs to compare TRB addresses: it records which
     * record owns the engine *before* the submit, under the controller lock,
     * and the completion - which runs under the same lock, after the engine has
     * already matched the event to its own outstanding TRB - reads it back.
     * A TRB address recorded after the submit returned would be too late: the
     * DPC can complete the command on another CPU before the submitting thread
     * gets the lock again.
     *
     * 0 means the engine is not the slot layer's; a nonzero value is an index
     * into Devices[] plus one, for the same reason XHCI_ENDPOINT.DeviceIndex is.
     */
    ULONG CommandOwner;
    ULONG CommandOwnerOp;       /* XHCI_DEV_OP_* the owner has outstanding */
    /*
     * **Which tenancy of that record reserved the engine**, captured with the
     * two fields above (audit finding A2's residue).
     *
     * `CommandOwner` is an index plus one and carries no generation, so a
     * completion arriving after usbhub gave up, reset the port and reopened EP0
     * at address 0 resolves to the *same* record under a *new* device. Without
     * this, the `ADDRESS_SET` success arm would run after the re-entry: it sets
     * `ADDRESSED`, re-asserts `XHCI_DEV_FLAG_ADDRESS_VALID` over the
     * `DeviceAddress = 0` the reopen just wrote, and emits `slot.addressed` for
     * a tenancy that no longer exists - a record carrying `ADDRESS_VALID` inside
     * the hub's own address-0 window, which finding A8's graph fallback
     * documents as impossible. The mirror case is an owed `ADDRESS_BSR` issued
     * from a slot a stale Address Device has since moved to Addressed, which
     * answers Context State Error and costs an enumeration round.
     *
     * `XHCI_DEVICE.Tenancy` was added for exactly this hazard and was read only
     * by the descriptor fold (`DescTenancy`); the command engine never consulted
     * it. `XhciSlotCommandEvent` now does - see the mismatch arm there for what
     * a stale completion does instead, which is decide nothing about the device
     * and re-derive the chain from the slot's actual state. The two
     * slot-ownership commands are exempt from the guard entirely and run their
     * ordinary arms: an `ENABLE_SLOT` adopts its Slot ID, prepares the slot and
     * owes `ADDRESS_BSR` - which is what the new tenancy needs anyway - and a
     * `DISABLE_SLOT` records the release the xHC really performed.
     */
    ULONG CommandOwnerTenancy;
    /*
     * Where the next command-issue scan starts, so a record with work cannot be
     * starved by a lower-numbered one that always has some. Enumeration is
     * serialised by usbhub, so in practice at most one record wants a command at
     * a time; the cursor costs six lines and removes the argument.
     */
    ULONG PumpCursor;
    /*
     * XhciSlotDeferredWork is running. It is not a lock - it is what stops the
     * function re-entering itself, because `UsbPortCompleteTransfer` may hand
     * usbport a completion it answers with a fresh `SubmitTransfer`, which calls
     * back in here.
     *
     * On SMP it also stops two CPUs pumping at once, at the cost of a window in
     * which work queued by one is not picked up by the other. That window is
     * closed by usbport's 500 ms `CheckController` poll, which reaches
     * XhciSlotPoll and therefore this function - so the worst case is a device
     * that waits half a second, never one that waits for ever.
     */
    ULONG DeferredBusy;
    ULONG DeferredReentries;
    /*
     * How many `SubmitTransfer` callbacks are on a stack right now, and it
     * exists because **usbport writes to the transfer record after that
     * callback returns success**:
     *
     *   00016550: call [eax+64h]     ; SubmitTransfer      (SP4 usbport.sys,
     *   000165B8: cmp  [ebp-8],eax   ; MPSTATUS == 0?       tools\win2ksp4-
     *   000165BB: jne  00016613      ; nonzero: stays queued  extracted\usbport-
     *   000165BD: or   [esi+4],8     ; success: the record    endpoint-disasm.txt)
     *   000165CD: push 2710h         ;   and arm its 10 s URB timeout
     *
     * A transfer this driver completes *inside* that call is one usbport has
     * already reclaimed by the time it reaches `000165BD`, so the timeout is
     * armed on freed memory. Measured on 2a: an unplugged HID
     * device's next read reached the "nothing behind this endpoint" gate, was
     * completed inline, and the guest died with `fatal exception 0E at
     * 0028:FF046A14` inside `hidclass.sys` one dispatch later - the same
     * address as the fault Phase 6 carried as unexplained, and the same shape
     * as batch 6-V's `refused SET_ADDRESS` crash. It is not new code: Phase 7a
     * only made it reachable on every unplug, because a HID device always has
     * a read posted when its device goes away.
     *
     * So the rule is **answer eventually, never now** - the completion stays on
     * `CompletionHead` and the next DPC or `XhciSlotPoll` delivers it, well
     * inside usbport's own 10 s timeout while the controller runs; while
     * suspended neither deliverer fires, so `XhciSuspendController` drains the
     * list before its quiesce (Phase 7 review, B4). Only the *delivery* is
     * held back: the command pump must still run on the submit stack or every
     * enumeration step would pay the 500 ms poll.
     *
     * A plain counter, not a per-CPU one: on SMP one CPU's submit may defer
     * another's completion to the poll, which is the safe direction. It is
     * incremented and decremented in the `xhciSubmitTransfer` dispatch wrapper,
     * which is the single place usbport enters this path.
     */
    ULONG SubmitDepth;
    ULONG CompletionsHeldBySubmit;
    /*
     * **The depth alone leaves an SMP window of the same class** (Phase 7
     * review, finding A7, CONFIRMED from both shipping binaries):
     * the decrement runs inside the dispatch wrapper, but usbport's
     * post-callback writes (`or [esi+4],8` + the 10 s timeout, SP4 `000165BD`)
     * run *after* the wrapper returns - and the completion path this driver
     * calls takes no lock that orders it behind them. SP4 `000183C0` / NUSB
     * `00017FA4` (`QDnT`) unlink the transfer from the endpoint list, insert
     * it on the FDO done list and queue the flush DPC **with no endpoint lock
     * held**, so on SMP a drain seeing the depth reach 0 could still deliver
     * a completion that usbport's submit path then writes through freed.
     *
     * `SubmitEpoch` closes the observable half: it advances on every bracket
     * exit, each held completion is stamped with the epoch it was queued
     * under, and the drain delivers it only from a **pass that began after
     * the bracket closed** (pass epoch snapshot > stamp). The pass that was
     * already running when the bracket closed - the one that could win the
     * race by nanoseconds - holds it for the next DPC or poll, which is
     * orders of magnitude later than the few hundred instructions the
     * post-callback writes need. A pass *starting* in that instruction window
     * remains possible and unobservable from this side of the ABI; this
     * narrows the window from "concurrent by construction" to "requires a
     * new drain to begin inside it".
     */
    ULONG SubmitEpoch;
    ULONG CompletionsHeldByPass;
    /* Leaves with no matching Enter (Phase 7 review, C2). The decrement
     * saturates so an imbalance cannot wedge delivery; this is what keeps the
     * saturation from also hiding the imbalance. Zero always. */
    ULONG SubmitUnderflows;

    /*
     * Transfers that have been retired under the lock and owe a
     * UsbPortCompleteTransfer call, which cannot be made there (design doc 05
     * section 7). Threaded through XHCI_TRANSFER.Next, oldest first, and
     * drained by XhciSlotDeferredWork after the lock is dropped.
     */
    PXHCI_TRANSFER CompletionHead;
    PXHCI_TRANSFER CompletionTail;
    ULONG CompletionsOwed;
    /*
     * The transfer a `UsbPortCompleteTransfer` call is inside right now, or
     * NULL. It is off both the endpoint queue and the completion list for the
     * duration, which is the only interval in which an abort can find it
     * nowhere - so this is what makes that interval visible to the abort path
     * (batch 7a-B, `AbortsDuringCompletion`). Written and read under the
     * controller lock; the call itself happens with the lock dropped.
     */
    PXHCI_TRANSFER CompletingTransfer;

    /*
     * Endpoints that owe a UsbPortInvalidateEndpoint call, for the same reason:
     * a transfer refused while a command chain was still running is left queued
     * by usbport, and the retry has to be *asked for* or it waits on the 500 ms
     * timer.
     *
     * **This used to be one pointer**, on the stated reasoning that "Phase 6
     * opens exactly one endpoint per device ... so a second one collapses into
     * the first". Task 7a-A.1 ends that premise: a device now has EP0 *and* up
     * to XHCI_MAX_DEVICE_ENDPOINTS interrupt pipes, and they owe invalidations
     * independently - a Configure Endpoint completing would have overwritten an
     * EP0 invalidation the Evaluate Context had just asked for, and the transfers
     * behind it would have waited on the timer with nothing saying why.
     *
     * So the debt is recorded **where the endpoint is** instead: a flag bit on
     * the device record for EP0 (XHCI_DEV_FLAG_INVALIDATE_EP0) and a word on
     * each XHCI_ENDPOINT_RECORD. Nothing can be lost, nothing needs a queue, and
     * the deferred pass finds them by the same scan it already walks. This
     * counter is what remains here.
     */
    ULONG EndpointInvalidatesOwed;

    /*
     * Which root-hub port the address-0 pipe belongs to.
     *
     * usbport's endpoint properties cannot answer this. `HubAddr` is the TT hub
     * address and reads 0xFFFF whenever there is no transaction translator, and
     * `PortNumber` is the **TT port** on a successful TT lookup - it happens to
     * hold the root port for a root-attached device only because `xhci_port.c`
     * reports every connected root port as High Speed (Phase 5 task 7), so
     * usbport skips `USBPORT_GetTt` and never overwrites the seeded value
     * (batch 7a-0 corrected, `docs/usb-xhci-info/usbport-miniport-abi.md` section 5; batch 6-V
     * had already corrected this comment's original "0xFFFF/0" to the root port
     * number). That makes the reading a consequence of this driver's own
     * reporting choice, and reading it back as a root-port index would beg the
     * question this field is being used to answer. So the
     * association is made from the one event that means "usbhub is about to
     * enumerate this port": the completion of a port reset. `EnumHubPort` is
     * the port whose reset completed most recently and `EnumSequence` counts
     * them, so a test can tell "the same port reset twice" from "the record is
     * stale".
     *
     * It is a hint that is *checked*, never trusted: the open re-reads the
     * port's shadow and refuses unless it is still connected and enabled.
     */
    ULONG EnumHubPort;
    ULONG EnumSequence;
    /*
     * **One address-0 claim per root-port reset**, and batch 7b-V0 is what
     * bought this field.
     *
     * The hint above was sticky: it named the last port whose reset completed
     * and nothing ever consumed it. A device usbport places *behind a hub*
     * produces an EP0 open at address 0 with no root-port reset anywhere near
     * it, so it read the hint, was attributed to the root port the hub itself
     * had been enumerated on, found that hub's own record there, and took the
     * re-enumeration branch - clearing the hub's address, re-entering the
     * addressing chain against its slot, and overwriting its EP0 endpoint
     * extension. That is the measured failure: not the child being
     * refused, but the *hub* being knocked out of the Addressed state and every
     * one of its transfers refused for retry from then on (`OpenRefusals` came
     * back **0**, which is what rules the refusal out as the cause).
     *
     * A genuine re-enumeration is always preceded by a fresh port reset -
     * usbhub resets the port and creates the device again - so "a reset that no
     * address-0 open has spent yet" is exactly the permission to claim a port,
     * and an open arriving without one is by construction not a root-port
     * device. Spent is a flag rather than a comparison against `EnumSequence`
     * so that "no reset has ever been seen" stays distinguishable from "the
     * last one was used": the scan below `EnumHubPort` exists for a reset that
     * was *missed*, and a counter comparison would disable it permanently.
     *
     * **The 2b run then measured what one claim per reset does not
     * cover, which is task 7b-A.1.1 and the two fields below.** The claim was
     * *unspent* going into the behind-hub attach, because usbhub's enumeration
     * of a root port issues **two** `RH_SetFeaturePortReset`, and the address-0
     * open sits between them rather than after both:
     *
     *   reset -> EP0 open at address 0 -> GET_DESCRIPTOR(device) -> **reset** ->
     *   SET_ADDRESS
     *
     * measured line for line on both targets (2a `win98-debugcon.batch7ba0-run1`
     * 615/632/676/687, 2b `win2k-debugcon.batch7ba10` 263/292/338/359). The
     * second reset is USB's own pre-SET_ADDRESS reset, so nothing will ever open
     * at address 0 behind it - and re-arming there left an entitlement lying on
     * the hub's root port for the next address-0 open *from anywhere* to spend,
     * which is exactly what a device behind that hub produces. One claim per
     * reset bounded the hijack to once per reset; it did not prevent it.
     */
    ULONG EnumClaimSpent;

    /*
     * **Task 7b-A.1.1: the reset that will not produce a device re-arms
     * nothing**, and the tension it has to resolve is that a genuine
     * re-enumeration *also* resets a port that already has a record and must
     * still be served.
     *
     * What separates them is not the port and not the record's existence - it is
     * whether the enumeration this reset belongs to has already spent its claim.
     * Two conditions, each necessary, and neither a stand-in for the other:
     *
     *   - the claim is **spent**, so some address-0 open has already been served
     *     since the last reset, and
     *   - the port being reset is **live mid-enumeration**: a record is on it,
     *     it holds EP0, and it has no valid address yet
     *     (`xhciDevEnumerationInProgress`).
     *
     * A re-enumeration's *first* reset meets neither - the record it finds is
     * either addressed, failed, disowned or gone - so it re-arms as before, and
     * the existing re-enumeration vectors are the regression net that says so.
     *
     * `EnumResetSuppressed` is the **bound** on the rule, and it exists because
     * the state above can get stuck: a record that reached Default and was then
     * abandoned with no traffic never refuses anything, so the health poll's
     * progress detector - which only advances on a refusal - never fails it, and
     * a predicate reading it alone would suppress every future reset of that
     * port for the life of the driver. At most one reset is suppressed per claim:
     * the second one re-arms whatever the record says, so the cost of being wrong
     * is one enumeration round rather than a dead port. It is cleared by every
     * re-arm, which is what makes "per claim" the unit.
     *
     * A suppressed reset leaves `EnumHubPort` alone as well, but **that half is
     * not observable and is not claimed to be**: the hint is only ever read
     * through the claim, and the claim is spent in exactly the state that
     * suppresses. Writing it there fails no check in the suite - measured, not
     * assumed - so it is consistency rather than a behaviour. What *is*
     * observable, and pinned: the reset is still counted in `EnumSequence`, which
     * counts port resets rather than enumerations.
     */
    ULONG EnumResetSuppressed;
    ULONG EnumResetsSuppressed;

    /*
     * The device layer's counters, and the split follows the rule Phase 4 task
     * 8 paid for - two causes with opposite diagnoses never share one.
     *
     * `SlotsEnabled`/`SlotsDisabled` are the xHC's slot allocations and
     * releases; a gap between them at idle is a leak. `DevicesAddressed` counts
     * completed SET_ADDRESS interceptions, `DevicesReopened` the EP0 close/
     * reopen rounds, and `Mps0Corrections` the ones that actually needed an
     * Evaluate Context - the difference is how often a Full Speed device really
     * had a non-8 bMaxPacketSize0, which is the only release-build reading
     * of task 6-B.4's path.
     *
     * `OpenRefusals` is an EP0 open this driver would not serve, and it is the
     * counter to read first when a device does not enumerate: it means the
     * association above found no port it could believe in.
     * `OpenRefusalsNoRecord` separates the sub-case where every record was in
     * use, which is a controller running at its slot limit rather than a
     * failure of the association.
     *
     * `SetAddressIntercepts` counts SET_ADDRESS transfers taken off the
     * transfer path, and it must equal `DevicesAddressed` plus the failures -
     * a SET_ADDRESS that is neither is one nobody completed, which is a hung
     * enumeration thread.
     *
     * `CommandOwnerLost` counts commands the engine gave up on (a timeout, an
     * abort, an abandon at quiesce) while a device record owned it. Expected on
     * every stop; a nonzero value while running is the diagnosis for a device
     * that never finished addressing.
     * `OpAgeRecoveries` is the other direction and the one that cannot be
     * inferred: an ActiveOp that outlived every watchdog interval, i.e. a
     * submit that reported success with nothing scheduled to time it - the same
     * failure `UsbPortRequestAsyncCallback`'s return value cannot report that
     * the command engine and the root hub each needed their own detector for.
     *
     * `TransfersRefused` counts SubmitTransfer calls declined for retry while a
     * device was not ready; `TransfersCancelled` the queued transfers completed
     * by a teardown or a REMOVE. `RemovesWithWork` is the REMOVE that arrived
     * with transfers still queued - which on the enumeration path should not
     * happen, and which leaves TRBs on a ring nothing will retire until the
     * slot is torn down (see XhciSlotEndpointRemoved).
     */
    ULONG SlotsEnabled;
    ULONG SlotsDisabled;
    ULONG DevicesAddressed;
    ULONG DevicesReopened;
    ULONG Mps0Corrections;
    /*
     * Evaluate Context completions whose wanted value had gone to zero under
     * them - a re-enumeration cleared `WantedMaxPacketSize0` while the command
     * was in flight (audit finding A3). Refused rather than committed, because
     * committing a 0 costs a whole enumeration round: `XhciBuildEp0Params`
     * refuses mps0 = 0 and the record is failed. Expected 0; nonzero says the SMP
     * window is real on this machine, which is worth knowing on its own.
     *
     * **`CommandsStaleTenancy` now shadows it on the path it was written for.**
     * Both re-enumeration branches that clear `WantedMaxPacketSize0` also take a
     * new tenancy, so the tenancy guard catches that window first and this
     * counter reads 0 where it once would have read 1. The check below is kept
     * as the narrower statement it always was - it is about the *value* being
     * asked for, not about who is asking - so read the two together: it is
     * `CommandsStaleTenancy` that measures the race now.
     */
    ULONG Mps0CorrectionsStale;
    /*
     * Device commands that stopped belonging to the record they were issued for:
     * it was re-entered while the command was in flight, so either the
     * completion arrived for a tenancy the record no longer holds, or the submit
     * was refused and the back-out found the same thing (audit finding A2's
     * residue - see `XHCI_EXTENSION.CommandOwnerTenancy`). **Two causes with the
     * same diagnosis share it deliberately** - both say the re-entry window is
     * live on this machine - which is why the print site names the shared thing
     * rather than listing them: the two are told apart here, in the header, and
     * a run that needs them apart at the counter would cost an extension field
     * and both offset tables. It is the same
     * window `Mps0CorrectionsStale` measures, one layer up, and reads the same
     * way: expected 0, and nonzero says the "completion against a port reset
     * plus reopen" race is live on this machine rather than merely arguable.
     *
     * **What it counts is the completion's *ordinary arm* being declined, not
     * the driver doing nothing.** The arm that takes these does act, and has to:
     * the command executed, so it re-reads the Output Slot Context and re-derives
     * what the chain owes. A stale `DISABLE_SLOT` is the one whose own effect is
     * still applied, and only on the two codes that prove it - Success and Slot
     * Not Enabled - where the slot is cleared, counted in `SlotsDisabled` and the
     * record restarted at `ENABLE_SLOT`; on any other code nothing is released or
     * accounted and the record is failed, because the slot may still be enabled.
     * What no arm does is apply the completion's meaning to the device now in the
     * record.
     *
     * **It counts a refused *submit* as well as a completion**, since the window
     * is the same one: the controller lock is dropped across `XhciCommandSubmit`,
     * so a re-entry can take the record before the back-out runs, and there the
     * answer is to unwind nothing at all - the command never executed.
     *
     * **`ENABLE_SLOT` is not counted**, because its completion is not declined:
     * it runs its ordinary arm whatever the tenancy says, or the slot is one
     * nothing can give back. A run whose only stale completion was an Enable Slot
     * reads 0.
     */
    ULONG CommandsStaleTenancy;
    /*
     * The re-enumeration re-entry, split by which command the kept slot needed
     * (audit finding A2 - see `XHCI_DEV_OP_RESET_DEVICE`).
     *
     * `SlotsResetToDefault` counts Reset Device commands that completed with
     * Success, i.e. slots that were Addressed or Configured when usbhub came back
     * to re-enumerate them. It is nonzero only when an **already-enumerated**
     * device is re-enumerated in place - an in-place recovery cycle of a device
     * that had reached Addressed or Configured.
     *
     * A zero is **not** a defect on any run this project has taken. Measured
     * by roadmap task 12.5, leg A': `SlotsResetToDefault` 0 with
     * `DevicesReopened` 12 and `SlotStatesUnreadable` 0 - every Output Slot
     * Context read succeeded and none found Addressed or Configured, which is
     * what a churn of **fresh** devices produces. `xhciDevReenterAtDefault` runs
     * on usbport's `ReopenPipe` of EP0 *mid-enumeration*, before the Address
     * Device that would reach Addressed, so `ADDRESS_BSR` is the legal branch and
     * no Reset Device is owed. A plain replug does not reach the reuse branch at
     * all: the disconnect tears the record down and the arrival takes a fresh
     * one. An earlier version of this comment predicted nonzero on any replug or
     * hub recovery cycle and read a zero as the slot-state read answering Enabled
     * for slots that cannot be; that prediction is what the measurement refuted,
     * not the counter and not the A2 fix.
     *
     * `SlotStatesUnreadable` counts **slot-state reads** that had to fall back
     * to `dev->State` because the Output Slot Context could not be read at all -
     * reads, not re-entries, because `xhciDevOweFromSlotState` has a second
     * caller: a device command completing for a tenancy that has been
     * re-enumerated under it re-decides the chain from a fresh read, so one
     * re-entry can contribute two. It is expected 0 either way: a record that
     * holds a Slot ID has a carve and a published DCBAA entry by construction.
     *
     * `EndpointContextsDroppedByReset` counts endpoint records that **held, or
     * had asked for, an Endpoint Context** and were put back to PENDING by a
     * re-entry - `CONFIGURED` and `CONFIGURING` and no other state. That is what
     * makes the reopen re-issue a Configure Endpoint instead of taking the
     * rebind-only path, and it is what lets this be read against
     * `EndpointsConfigured`: the gap is endpoints the xHC re-accepted after a
     * re-enumeration.
     *
     * A re-entry reconciles `REFUSED` and `FAILED` records to PENDING too - a
     * verdict about the previous enumeration's context must not outlive it - but
     * those are deliberately **not** counted here, because such a record may
     * never have had a context for the controller to drop and counting it would
     * make the comparison above meaningless (review round 2).
     */
    ULONG SlotsResetToDefault;
    ULONG SlotStatesUnreadable;
    ULONG EndpointContextsDroppedByReset;
    /*
     * Disconnects reported by a hub whose *own* record could not name its root
     * port, resolved from the topology graph instead (audit finding A8). The
     * window is the hub's address-0 re-enumeration, during which `ADDRESS_VALID`
     * is clear; before the fallback existed every such disconnect resolved to
     * root port 0 and left the departed child's slot and address standing.
     */
    ULONG HubRootPortsFromGraph;
    ULONG OpenRefusals;
    ULONG OpenRefusalsNoRecord;
    /*
     * The share of `OpenRefusals` that is an address-0 EP0 open arriving with no
     * unspent root-port reset behind it - which since batch 7b-V0 is what a
     * device *behind a hub* looks like from here. Its own counter because it is
     * the one refusal that is a topology statement rather than a resource one:
     * a nonzero reading with hubs on the bus is this driver declining to invent
     * a root port, and until task 7b-A.3 fills Route Strings it is the expected
     * outcome rather than a defect.
     */
    ULONG OpenRefusalsNoClaim;
    /*
     * **The accounting pair (task 7b-A.1.0), and it exists because a counter set
     * assembled path by path is only ever as complete as whoever assembled it.**
     *
     * The batch 7a-V 2a run read `OpenRefusals` = 0 while a behind-hub child's
     * EP0 open was certainly being refused - `ProbeEpEvents[OPEN]` said the open
     * happened and no refusal counter moved - so the run could say *that* an open
     * had gone somewhere unaccounted and not *where*. The two paths it had gone
     * through (an unusable per-endpoint buffer, a malformed call) now have their
     * own counters below, but adding those is the same act that produced the gap
     * in the first place. What closes it is the identity:
     *
     *   OpensTotal == OpensAccepted + OpenRefusals + OpenRefusalsBuffer +
     *                 OpenRefusalsMalformed + EndpointRefusalsType +
     *                 EndpointRefusalsNoDevice + EndpointRefusalsNotReady +
     *                 EndpointRefusalsParams + EndpointRefusalsPool
     *
     * `OpensTotal` counts every entry to `XhciSlotOpenEndpoint` that has an
     * extension to count in, and `OpensAccepted` every `MP_STATUS_SUCCESS` it
     * returns; the seven names on the right are every other way out of it.
     * `OpenRefusalsNoRecord` and `OpenRefusalsNoClaim` are **shares** of
     * `OpenRefusals` and are deliberately absent from the sum. A discrepancy is a
     * return path nobody counted, which is readable on a target from the printed
     * lines alone and is enforced on the host by `note_open_accounting`.
     *
     * `OpensTotal` against `ProbeEpEvents[OPEN] + ProbeEpEvents[REOPEN]` is the
     * second reading and is not the same one: the probe is called in the dispatch
     * wrapper, so a gap there is the wrapper's own bracket refusing - a NULL
     * endpoint extension, which usbport should never produce.
     *
     * `OpenRefusalsBuffer` is usbport supplying a per-endpoint common buffer this
     * driver asked none of and cannot put a ring in, and `OpenRefusalsMalformed`
     * a call with no properties or no endpoint extension. Both are permanent and
     * neither is a resource shortage, which is why neither is folded into
     * `OpenRefusals`.
     */
    ULONG OpensTotal;
    ULONG OpensAccepted;
    ULONG OpenRefusalsBuffer;
    ULONG OpenRefusalsMalformed;
    ULONG SetAddressIntercepts;
    /*
     * Task 7b-A.2, and the four are read as a group.
     *
     * `HubSlotsMarked` counts the Slot Contexts an external hub's Hub / Number
     * of Ports / TTT / MTT actually reached, by *either* route - the endpoint's
     * own Configure Endpoint carrying them, or the standalone command below. On
     * both target VMs it should equal the number of hubs on the bus once each
     * has been enumerated, and it is the only release-build reading that
     * says a behind-hub topology was described to the controller rather than
     * merely reconstructed in software.
     *
     * `HubMarkCommands` counts the standalone commands, as **attempts**: the
     * marking is derived and issued in the same deferred-work pass that folds
     * the descriptor, so on an ordinary bus this is one per hub enumeration -
     * usbhub has not opened the hub's interrupt pipe by then and there is no
     * endpoint command to ride on. Roughly equal to `HubSlotsMarked` plus
     * `HubMarksLostToAddress` is the healthy shape; a *large* excess is the
     * command engine refusing submissions, which leaves the need derived and has
     * the next pump build it again.
     *
     * `HubMarksLostToAddress` counts markings dropped because the slot was
     * re-addressed, which rewrites the whole Output Slot Context (6.2.2.1 p.411).
     * It tracks the re-enumeration cycle rather than anything going wrong.
     *
     * `HubMarkFailures` is a marking the xHC refused or this driver could not
     * encode. Each one suppresses further attempts for that record
     * (XHCI_DEV_FLAG_HUB_MARK_FAILED), so this is also the count of hubs whose
     * children the controller is scheduling without knowing about the hub.
     */
    ULONG HubSlotsMarked;
    ULONG HubMarkCommands;
    ULONG HubMarksLostToAddress;
    ULONG HubMarkFailures;
    /*
     * Task 7b-A.3, and these are the readings the subphase's **stop rule** turns
     * on: whether snooping reconstructs a path well enough to address a device
     * through it, on both shipping usbport builds.
     *
     * `BehindHubOpens` counts address-0 EP0 opens served from a topology claim
     * rather than from a root-port reset - the number of behind-hub devices this
     * driver has agreed to address. `BehindHubAddressed` is how many of them
     * reached an address, so the gap is enumerations that started one tier down
     * and did not finish. Before this task the first was structurally 0 and
     * `OpenRefusalsNoClaim` carried them all.
     *
     * The three refusals are separated because they say different things about
     * the bus: `BehindHubTooDeep` is a path past the Route String's five tiers,
     * which is the one refusal that is a *correct* answer about a legal USB
     * topology; `BehindHubNoSpeed` is a device speed this driver cannot describe
     * to this controller (no Protocol Speed ID for it, or a speed class it does
     * not address); `BehindHubNoRecord` is the record table full.
     *
     * `BehindHubGone` counts devices torn down because the hub above them
     * reported their port empty - the only channel through which a behind-hub
     * unplug is ever observed (see XHCI_TOPO_GONE).
     *
     * `TtProgrammed` counts **Address Device** Input Slot Contexts built
     * carrying a Parent (TT) Hub Slot ID and Port - two per behind-hub
     * enumeration, because the chain issues that command twice (BSR = 1, then
     * BSR = 0). `TtUnresolved` counts the ones that could not be built: a graph
     * that named an HS ancestor whose record holds no Slot ID. That is a
     * *failure*, not a
     * fallback - the record is failed rather than addressed with the fields
     * cleared, because an FS/LS device addressed as though it needed no split
     * transactions is a device the controller schedules wrongly, and design doc
     * 02 section 3's rule is that a wrongly-addressed device corrupts the
     * schedule while a cleanly-rejected one does not. **It has a reachable
     * transient cause as well as a defective one**: a hub that re-enumerates
     * gives its usbport address back, and until it has one again nothing
     * resolves the graph's TT to a Slot ID - so a child addressed in that window
     * is failed and re-enumerated behind it. A small nonzero reading beside a
     * nonzero `DevicesStalledOut` or `HubMarksLostToAddress` is that cycle; a
     * large one with the bus idle is not.
     *
     * `TtPairsAgreed`/`TtPairsDisagreed` compare what usbport said with what the
     * graph derived, on every open that carried a TT claim. **A disagreement is
     * expected on QEMU** and is the measurement task 7b-A.1.2 was instrumented
     * for one level up: usbport names the Full-Speed `usb-hub` as a transaction
     * translator (batch 7b-V0), the graph says no High-Speed ancestor exists, and
     * the graph wins. On a real High-Speed hub the two should agree, which is
     * what makes a nonzero `TtPairsDisagreed` on hardware worth reading rather
     * than a routine number.
     */
    ULONG BehindHubOpens;
    ULONG BehindHubAddressed;
    ULONG BehindHubTooDeep;
    ULONG BehindHubNoSpeed;
    ULONG BehindHubNoRecord;
    ULONG BehindHubGone;
    ULONG TtProgrammed;
    ULONG TtUnresolved;
    ULONG TtPairsAgreed;
    ULONG TtPairsDisagreed;
    ULONG CommandFailures;      /* a chain command answered a non-Success code */
    ULONG CommandOwnerLost;
    ULONG OpAgeRecoveries;
    ULONG TransfersSubmitted;
    ULONG TransfersRefused;
    /*
     * Task 8-A.1. The share of `TransfersRefused` that was **ring-full**, and the
     * re-offers the backpressure latch then asked for. They are two counters and
     * neither can be inferred from the other, which is the whole reading:
     *
     *   - a climbing `RefusedRingFull` beside a climbing `TransfersCompleted` is
     *     a bulk pipe running at the ring's capacity, which is what success looks
     *     like and is not an anomaly at any size;
     *   - a climbing `RefusedRingFull` with `RetriesAsked` frozen is the latch
     *     failing to fire, which is the throughput defect this mechanism exists
     *     to prevent - the pipe still works, on usbport's 500 ms timer;
     *   - `RetriesAsked` climbing with `TransfersSubmitted` frozen is the
     *     livelock: re-offers being asked for and nothing being placed.
     *
     * `EndpointRetriesAsked` is deliberately the controller-wide total of the
     * per-queue `RetriesAsked`, since a queue dies with its endpoint record.
     */
    ULONG TransfersRefusedRingFull;
    ULONG EndpointRetriesAsked;
    ULONG TransfersCompleted;
    ULONG TransfersCancelled;
    ULONG RemovesWithWork;
    ULONG TransferEventsForeign;    /* named a slot/DCI no record has open */
    /*
     * Transfers *failed* because the refusal that would otherwise have been
     * given could never stop being true - no record behind the endpoint, its
     * EP0 binding gone, or a terminally failed controller. Counted apart from
     * `TransfersRefused`, which is the transient answer, because the two say
     * opposite things: a refusal means "ask again", a failure means "there is
     * nothing here to ask". The batch 6-V Win2000 run is why they are two
     * counters - the driver gave the first answer where the second was true and
     * hung the thread that was waiting (roadmap batch 6-V).
     */
    ULONG TransfersFailedGone;
    /*
     * Device records failed by the health poll's progress detector: refusing
     * transfers, no command in flight, and nothing placed on a ring for
     * XHCI_DEV_STALL_MS of consecutive polls. This is the **bound** on task
     * 7b-A.0 - the net under every "refused for retry" answer this driver can
     * give, so that no refusal can be permanent whatever produced it. A nonzero
     * reading names a record that gave up; the transfers behind it are then
     * failed by the ordinary FAILED gate and counted in `TransfersFailedGone`.
     */
    ULONG DevicesStalledOut;
    /*
     * Devices torn down because the *port* was taken out of service - disabled
     * or unpowered - rather than because anything was unplugged. Its own
     * counter, not a share of `DevicesTornDown`, because the two have different
     * causes and only this one means usbport abandoned a device that is still
     * physically attached.
     */
    /*
     * The two halves of a port going out of service, counted apart because they
     * happen at different times and only one of them waits on hardware.
     * `DevicesDisownedOut` is usbport's view - the address map entry given up
     * the moment the callback arrives - and `DevicesDisabledOut` the release
     * that follows once the port is confirmed down. A gap between them at idle
     * is a port whose disable never landed.
     */
    ULONG DevicesDisownedOut;
    ULONG DevicesDisabledOut;
    /*
     * The short-transfer family, accumulated at controller level.
     *
     * `XHCI_TRANSFER_QUEUE` already counts all three per endpoint, and that is
     * where the *reasoning* about them lives (batch 6-A: three causes, three
     * counters). What the queue cannot be is a **durable** reading: it dies with
     * the device record, so an unplugged device takes its evidence with it, and
     * the checkpoint's "correct Short Packet/residual handling" clause is about
     * a whole run rather than about whichever device happens to still be
     * attached when someone looks. These are the same three events accumulated
     * as they happen, which is what a release build can report at any
     * moment.
     *
     * The split is batch 6-A's and is not cosmetic: `ShortPacketsTotal` is a
     * conforming short transfer, `ShortSuccessesTotal` is the spurious-success
     * quirk (`docs/usb-xhci-info/xhci-programming.md`) where the completion code says Success
     * and the residual says otherwise, and `IntermediateEventsTotal` is a
     * Success on a data TRB with residual 0 - which this driver gives no
     * controller a reason to emit, so a nonzero value is the only reading that
     * would say a transfer was *under*reported.
     */
    ULONG ShortPacketsTotal;
    ULONG ShortSuccessesTotal;
    ULONG IntermediateEventsTotal;
    /*
     * The fourth of that family: transfers ended on a Short Packet Event that
     * did not name the TD's last TRB. Zero means the departure documented in
     * `XhciXferDrainSettled` never fired this run.
     *
     * Task 9-0.2 made this a reading on its own, by moving the retire from the
     * event to the end of a drain pass that found the event ring empty: a
     * conforming controller's tail is already queued behind the short event, is
     * consumed first, and completes the transfer positionally - so this stays 0
     * there and `MidTdDeferralsTailedTotal` carries the count instead.
     *
     * **The primary verdict is now `MidTdDeferralsTailedTotal` against
     * `MidTdDeferralsTotal`**, which needs none of the qualifications below.
     * The older `MidTdShortTailsTotal`-against-this reading survives as the
     * measure of the one window 9-0.2 could not close, and it is a verdict only
     * while `MidTdVerdictVoided` is 0 **and** the derived outstanding term is 0
     * - see that field. (Successive audit rounds cut that qualification down
     * from "dropped is 0", which was twice too weak.)
     */
    ULONG MidTdShortRetiresTotal;
    /*
     * Task 9-0.2's partition of the deferrals, folded from the queues. See
     * `XHCI_TRANSFER_QUEUE.MidTdDeferrals` for what each means and for the
     * identity they satisfy:
     *
     *   deferrals == tailed + tailedSpurious + retired + lost + still armed
     *
     * The last term is not stored - it is the transfers carrying
     * `XHCI_XFER_FLAG_SHORT_DEFERRED` on some live queue, which
     * `XhciSlotDrainSettled` counts by walking and publishes as
     * `MidTdDeferPending`. Same reasoning as the outstanding term below: a
     * stored copy of something the state already determines can drift from it.
     */
    ULONG MidTdDeferralsTotal;
    ULONG MidTdDeferralsTailedTotal;
    ULONG MidTdDeferralsTailedSpuriousTotal;
    ULONG MidTdDeferralsLostTotal;
    /*
     * Deferrals armed and not yet resolved, across every live queue.
     *
     * It is a **hint with one exact moment**: `XhciSlotDrainSettled` recomputes
     * it by walking every live queue, so it is exact immediately after any
     * settle pass. Between passes it can only over-state - the arm path sets
     * it, and a transfer that leaves the queue armed does not clear it - and
     * over-stating costs one wasted walk that then corrects it. It cannot
     * under-state, because the only thing that arms a deferral is
     * `XhciSlotTransferEvent`, which sets it in the same fold.
     *
     * That direction is the whole reason it may be used as the gate on the
     * walk: a zero here means there is genuinely nothing to settle.
     */
    ULONG MidTdDeferPending;
    /* Drain passes that found the event ring empty, had deferrals outstanding,
     * and therefore walked the queues. Its ratio to `DpcCount` is what says
     * whether the walk is a cost worth reducing; nothing branches on it. */
    ULONG MidTdSettlePasses;
    /* Transfers settled by those passes - the walk's yield. It is not the same
     * as `MidTdShortRetiresTotal`, which the sweep also moves, so a settle pass
     * that found nothing to do is visible as passes without settles. */
    ULONG MidTdSettles;
    /*
     * Retires the ring **refused** - a record/ring divergence - after which the
     * transfer was left queued and a stop-then-drain was asked for.
     *
     * **Both routes, and the name says so deliberately.** The divergence can be
     * found at the Transfer Event or at the settle, and it is *not* a settle
     * counter: an event-time divergence never armed a deferral at all, so this
     * can rise with `MidTdDeferralsTotal` at zero. Calling it a "settle"
     * counter, which it was until the batch-9-0 review round 7, would make a
     * target log attribute an event-time disagreement to task 9-0.2's settle.
     *
     * It counts the **request**, not a completed repositioning: whether the
     * Stop Endpoint and the Set TR Dequeue Pointer that follow it actually
     * landed is the endpoint quiescence counters' business, not this one's.
     *
     * **Not subtractable from `MidTdDeferralsLostTotal`** for the same reason -
     * only a settle-time refusal is in both, so subtracting can underflow.
     *
     * Defensive: nothing in a healthy run reaches it. It exists as a counter
     * rather than as a comment because the batch-9-0 review took three rounds
     * to get this branch right - halting a healthy endpoint, then issuing a Set
     * TR Dequeue Pointer to a Running one, then missing the shape entirely -
     * and a defensive path with no number attached is one nobody can tell has
     * fired.
     */
    ULONG MidTdRefusedRetires;
    /*
     * Refused retires - from either route - on an endpoint whose quiescence
     * chain had **already failed**, where no stop-then-drain could be requested
     * at all, so they are counted apart from `MidTdRefusedRetires` rather than
     * inflating it with requests that were never made.
     *
     * Nonzero means a transfer is queued on a failed endpoint waiting for the
     * only things that can prove the xHC let its buffer go: an HCRST via
     * `XhciSlotInit`, or a Disable Slot. That is `xhciEpQuiesceFail`'s standing
     * policy, not a case this path handles specially.
     */
    ULONG MidTdRefusedRetiresUnarmable;
    /*
     * Sticky. Set when the partition
     *
     *     deferrals == tailed + tailedSpurious + retired + lost + still armed
     *
     * does not hold at the one instant it can be checked - the end of a settle
     * pass. It is not a controller fault and nothing branches on it: it says a
     * mover of one of those terms is not folding it, which makes every mid-TD
     * reading beside it unsafe. The alternative to checking is trusting that
     * whoever added a fifth path found all four sites.
     */
    ULONG MidTdDeferAccountingBroken;
    /* Trailing events that did arrive for an early-retired TD - the conformance
     * reading itself. Equal to `MidTdShortRetiresTotal` on a two-event
     * controller, 0 on QEMU's one-event xHC. */
    ULONG MidTdShortTailsTotal;
    /* Early retires whose tail index was evicted before a tail event arrived, so
     * a low tail count cannot be read as nonconformance. */
    ULONG MidTdTailsDroppedTotal;
    /* Observations censored - the record was given up because its TRBs were
     * re-let, or was still outstanding when its queue died. **Pre-9-0.2 this
     * was expected to be large on a bulk IN repost loop even on a conforming
     * controller**, which is why the verdict needs it at zero rather than
     * merely small (repo audit round 3). Since the retire moved to the end of
     * an observed-empty drain pass, a conforming controller makes no
     * early-retire record at all, so a large count now means the one-event xHC
     * or the residual post-empty-read window - something to investigate rather
     * than to expect. */
    ULONG MidTdTailsCensoredTotal;
    /*
     * Sticky: set the first time anything is dropped or censored, never
     * cleared. **This, not the two totals, is what gates the verdict**, because
     * both totals are wrapping ULONGs and "== 0" stops being proof after 2^32
     * of them - a wrap would silently re-enable a conformance claim that had
     * been invalidated hours earlier (repo audit round 4, finding 4).
     *
     * The other half of the gate is not a counter at all but the identity
     *
     *     retired == tails + dropped + censored + still outstanding
     *
     * whose last term is the records live in some queue right now. A verdict
     * taken while that term is nonzero reads a tail that simply has not arrived
     * yet as one the controller withheld, and `CheckController` polls every
     * 500 ms into exactly that window (round 4, finding 3). It is derived in
     * the print rather than stored, since the four totals already determine it.
     */
    ULONG MidTdVerdictVoided;
    /* Events that resolved to this ring but to no outstanding transfer. Folded
     * so it survives the device record, because the mid-TD departure's
     * no-double-completion argument rests on trailing events landing here - but
     * it counts every other already-ended transfer's events too, which is why it
     * is not the conformance reading. */
    ULONG UnmatchedEventsTotal;
    /*
     * The rest of `XHCI_TRANSFER_QUEUE`'s diagnostics, folded up when a queue
     * dies rather than sampled as a delta per event.
     *
     * The three above are taken as a delta across `XhciXferEvent` because only
     * that call knows which of the three an event was. These seven need no such
     * classification - they are already the queue's own totals - so the cheaper
     * shape is to add them once, on the paths that destroy the queue: a reopen
     * (`XhciXferQueueInit`), a released device record, and the slot table reset
     * a reinitialisation performs.
     *
     * Folding them at all is what makes them readable. `OrphanedGroups` is
     * documented as the only record of "a state this code did not anticipate"
     * and `PlacementFailures` as a diagnosis for `CheckController` - but the
     * endpoint-torn-down-after-a-fault case, the one most likely to need the
     * reading, was also the one that destroyed it.
     */
    ULONG OrphanedGroupsTotal;
    ULONG PlacementFailuresTotal;
    ULONG SweptTransfersTotal;
    ULONG ResidualRejectsTotal;
    ULONG LengthOverrunsTotal;
    ULONG SumFailuresTotal;
    ULONG ResidualIgnoredTotal;
    /*
     * The other seven, folded the same way and for the same reason - they had no
     * reader at all until the repo audit's finding B3.
     *
     * `ForeignEventsTotal` is the load-bearing one: `src/xhci_xfer.c` refuses to
     * escalate an event naming another slot or DCI on the reasoning that "the
     * visible failure is the counter", which was true of nothing while the
     * counter died with the device record. `EventDataEventsTotal` is an Event
     * Data event on a driver that places no Event Data TRBs, and
     * `BadCodesTotal` a completion code Table 6-90 does not give to a Transfer
     * Event - both are "the controller sent something this driver has no model
     * for", and both are counted-and-dropped, so the count is the whole report.
     *
     * `QueueSubmittedTotal` / `QueueCompletedTotal` are the per-queue TD tallies
     * and the pair is the reading: submitted climbing with completed frozen is a
     * stalled endpoint, and it is the shape a bulk device dying mid-transfer
     * makes. `QueueErrorsTotal` and `QueueRecoveriesTotal` are error-class events
     * and the retires that followed them. They carry the `Queue` prefix because
     * `Errors`, `Recoveries`, `Submitted` and `Completed` are words this
     * structure already uses for other things.
     */
    ULONG ForeignEventsTotal;
    ULONG EventDataEventsTotal;
    ULONG BadCodesTotal;
    ULONG QueueErrorsTotal;
    ULONG QueueRecoveriesTotal;
    ULONG QueueSubmittedTotal;
    ULONG QueueCompletedTotal;
    /* Stopped-class codes (26-28) handed to `XhciXferEvent` instead of
     * `XhciXferQueueStopped`. Nothing routes them that way today, so a
     * non-zero reading names a caller that has broken the contract. */
    ULONG StoppedRefusedTotal;
    ULONG DevicesTornDown;
    ULONG DevicesInvalidated;       /* dropped by a resume that could not restore */
    /*
     * Records given up on a controller that could **not** be shown to have let
     * go of them - a stop whose quiesce proved nothing, or a resume that had to
     * reinitialise a controller it could not read as halted. Their Slot IDs and
     * common-buffer blocks stay reserved and their DCBAA entries untouched until
     * a reset really releases them, so this is not a leak; it is the count of
     * times this driver declined to *claim* a release it had no evidence for.
     * Its own counter rather than a share of DevicesInvalidated, because the two
     * say opposite things about the hardware.
     */
    ULONG DevicesAbandoned;
    ULONG EndpointInvalidates;

    /*
     * The non-default endpoint family (task 7a-A.1). Split by *who said no*,
     * because on a target these are the only readings that tell an unsupported
     * device apart from an exhausted driver apart from a controller that
     * declined:
     *
     * `EndpointsOpened` is a non-default open accepted, `EndpointsReopened` one
     * that found the DCI already open and rebound it. `EndpointsConfigured` is a
     * Configure Endpoint that completed with Success - the difference between it
     * and `EndpointsOpened` is pipes usbport believes in that the xHC never
     * accepted, which is what a HID device that enumerates and then stays silent
     * looks like.
     *
     * The four refusals are this driver's own and are never merged:
     * `EndpointRefusalsType` is a transfer type this driver does not serve -
     * **isochronous, or a non-default control endpoint, and nothing else since
     * task 8-A.1 admitted bulk**. It was the expected reading for a mass-storage
     * device up to Phase 7a and is now the expected one for **USB Audio**, whose
     * ABI gate is task 9-0.1; a nonzero value therefore names a phase boundary
     * rather than a defect, but a different boundary than it used to;
     * `EndpointRefusalsNoDevice` is an open for an address no record holds;
     * `EndpointRefusalsParams` is properties the context builder would not
     * encode; `EndpointRefusalsPool` is the ring pool declining, whose own three
     * causes are counted inside XHCI_RING_POOL. `EndpointRefusalsNotReady` is
     * the only *transient* one - a controller mid-resume or a device still
     * finishing its EP0 chain - and is separated from the rest for the reason
     * the batch 6-V Win2000 run paid for: a refusal that will stop being true
     * and one that never will are opposite diagnoses, and a climbing count here
     * with `EndpointsOpened` frozen is the livelock signature.
     *
     * `EndpointSpeedMismatches` counts endpoints whose usbport-reported speed
     * differs from the port's decoded one. It is **expected to equal the number
     * of non-High-Speed endpoints opened** for as long as Phase 5 task 7 reports
     * every connected root port as High Speed - see xhciDevSpeedFromProperties
     * for what that costs. A zero here on a run with a Full-Speed HID device
     * attached would mean that untruth had been removed, not that nothing
     * happened.
     *
     * `EndpointsNoBandwidth` and `EndpointsNoResources` are completion codes
     * 8/35 and 7 - the xHC saying the schedule is full or its own resources are,
     * which usbhub is meant to degrade on rather than treat as a broken
     * controller (Table 6-90 p.466/510; the `8 and 9` this comment used to name
     * was the roadmap task text's error, corrected) - and
     * `EndpointConfigureFailures` is every other code. They are three counters
     * and not one for the reason the whole file uses: a full schedule and a
     * malformed Input Context have nothing in common but their shape.
     *
     */
    ULONG EndpointsOpened;
    ULONG EndpointsReopened;
    ULONG EndpointsConfigured;
    ULONG EndpointsReleased;
    ULONG EndpointRefusalsType;
    ULONG EndpointRefusalsNoDevice;
    ULONG EndpointRefusalsParams;
    ULONG EndpointRefusalsPool;
    ULONG EndpointRefusalsNotReady;
    ULONG EndpointSpeedMismatches;
    /*
     * Endpoints whose `Interval` had to be raised to the floor Table 6-12 gives
     * their real speed, because usbport bucketed the `Period` against the High
     * Speed this driver reports for every root port (Phase 5 task 7). Its own
     * counter beside `EndpointSpeedMismatches` because the two are not the same
     * event: every non-HS endpoint mismatches, but only one whose bucketed
     * `Period` lands under 1 ms is floored - and *that* is the subset whose
     * Endpoint Context would otherwise have been outside Table 6-12's range for
     * the speed its Slot Context carries. Neither counter says the interval
     * *matches the descriptor*; usbport's bucketing discarded that before either
     * could (see XhciIntervalForSpeed).
     */
    ULONG EndpointIntervalsFloored;
    ULONG EndpointsNoBandwidth;
    ULONG EndpointsNoResources;
    ULONG EndpointConfigureFailures;

    /*
     * The quiescence family (batch 7a-B). Each pair is one command and its
     * refusal, kept apart for the reason the rest of this block is: a command
     * the xHC declined and a command this driver never got to issue have
     * different diagnoses and only one of them is the controller's.
     *
     * `EndpointStops` / `EndpointResets` / `EndpointDequeueSets` count
     * completions that proved what the command claims - and for Stop Endpoint
     * that includes Context State Error, because the states it names ("not
     * Running") are the ones the caller wanted. `EndpointResetsNotHalted` is the
     * same code from a **Reset** Endpoint, which is a different fact: the
     * endpoint was not in the Halted state (4.6.8 p.117), so this driver's
     * `HALTED` bit was stale and the recovery converts to a Stop.
     *
     * `EndpointHalts` counts Transfer Events that left an endpoint halted and
     * `EndpointResets` the Reset Endpoint commands that cleared one; a gap
     * between them at idle is a pipe nobody reset, which for a non-default
     * endpoint means a client that never issued a reset-pipe.
     * `TransfersOnHaltedEndpoint` is a submission answered with
     * `USBD_STATUS_STALL_PID` because the pipe was still halted - the reading
     * that says a client is submitting without resetting.
     *
     * `TransfersAborted` counts `AbortTransfer` calls that found the transfer on
     * a queue and took it off; `AbortsUnmatched` the ones that did not, which is
     * either a transfer already completed by an event (a race the controller
     * lock serialises but does not prevent) or one that never reached a ring.
     * `TransfersNoOpped` counts TRBs rewritten as No Ops to take a cancelled TD
     * out of a ring with surviving work behind it.
     *
     * `EndpointDequeueRearms` is the consistency check firing: the ring's
     * dequeue moved while a Set TR Dequeue Pointer naming the old one was in
     * flight, so the command was reissued rather than believed.
     *
     * `EndpointQuiesceLost` is a quiescence command the engine gave up on and
     * `EndpointQuiesceUnavailable` one that could not be issued at all, because
     * the controller was not admitted when the endpoint was given up. They are
     * two counters for the reason the rest of this block is: the first is a
     * controller that stopped answering, the second is one that was already
     * known not to be answering, and only the first says anything went wrong.
     *
     * `EndpointQuiesceFailures` counts every call to `xhciEpQuiesceFail`, which
     * is the only place `XHCI_EPQ_FAILED` is set *without* `XHCI_EPQ_UNAVAILABLE`
     * beside it - and that is precisely the state `xhciSaveQuiesceUnproven`
     * declines a `CSS` on for the life of the driver. Task 12.1 added it because
     * the limitation that task published names a counter which has to be able to
     * say a machine is in that state, and neither of the two above can: a Stop
     * Endpoint answered with an error completion, a failed Set TR Dequeue Pointer
     * and a failed Configure Endpoint all reach `xhciEpQuiesceFail` and increment
     * neither. A limitation whose named indicator can read 0 while the condition
     * holds is worth less than no indicator, because the 0 gets believed.
     *
     * **Read it beside those two rather than as their sum, because it is neither
     * their subset nor their superset.** `EndpointQuiesceUnavailable`'s state is
     * the pair the save gate deliberately carves out, so it is not this counter's
     * business and is not counted here. `EndpointQuiesceLost` counts an abandoned
     * command whether or not the endpoint could still be resolved, and only the
     * resolved half reaches `xhciEpQuiesceFail` - so the two overlap without
     * either containing the other.
     *
     * **It counts entries, not endpoints currently stuck.** Task 7a-B.3's
     * reset-pipe clears `XHCI_EPQ_FAILED`, so a nonzero value says the state was
     * entered, not that it is held now. Nothing else clears it and nothing
     * obliges a client to send one, which is why nonzero here is the reading that
     * says this controller may have stopped taking Save States for good.
     *
     * `TeardownsWithoutStop` is a Disable Slot issued for a device with an
     * endpoint whose stop had failed - 4.6.4 p.97's precondition unmet, counted
     * rather than hidden because it is the one reading that says so.
     */
    ULONG EndpointStops;
    ULONG EndpointStopFailures;
    ULONG EndpointResets;
    ULONG EndpointResetFailures;
    ULONG EndpointResetsNotHalted;
    ULONG EndpointDequeueSets;
    ULONG EndpointDequeueFailures;
    ULONG EndpointDequeueRearms;
    ULONG EndpointPlacementFailures;
    ULONG EndpointHalts;
    /*
     * Forced Stopped Transfer Events - "every time a Transfer Ring is stopped
     * ... If a Transfer Ring is empty when a Stop Endpoint Command is issued, a
     * Stopped Transfer Event shall be generated" (4.6.9 p.122), with one stated
     * exception: "If a Transfer Ring has been Halted due to error condition when
     * a Stop Endpoint Command is received, no Stopped Transfer Event shall be
     * generated" (p.124). So it is **not** one per stop, and a run where every
     * stop met a halted ring can legitimately read zero. What a zero *does* say,
     * on a run where ordinary stops happened, is that the controller does not
     * force them - which would mean the placement is running against a ring
     * whose stop was never announced.
     */
    ULONG EndpointStoppedEvents;
    /*
     * Endpoints taken out of the Stopped state by a doorbell this driver rang
     * deliberately rather than as part of a submission - the other end of
     * `XHCI_EPQ_PAUSED`. `EndpointRestartsByPoll` is the subset the health poll
     * had to do because neither `SetEndpointState(ACTIVE)` nor a submission
     * arrived.
     *
     * **It is not expected to be zero, and the poll is not a backstop.** That
     * reading was written from the counter's name and measured wrong on all
     * three guests (1 on 2a, 3 on 2b, 4 on 2d, scaling with abort
     * count). `USBPORT_SetEndpointState` is **edge-triggered on usbport's own
     * recorded state** in both shipping builds - SP4 `00016D8A`, NUSB
     * `000169D2`, the identical `cmp eax,[ebp-10h] / je <skip the call>` - so a
     * state that does not *change* produces no callback at all. A stop this
     * driver takes on its own initiative (an abort, a quiescence) never moves
     * usbport's state, so usbport can sit ACTIVE -> ACTIVE across the whole
     * stop and say nothing. Nothing then restarts the endpoint except the next
     * submission, and a HID device's queued read is exactly the case where none
     * is coming.
     *
     * So this poll is load-bearing. What is worth alarm is a value that keeps
     * *climbing* against idle traffic - a pause that never ends - not a value
     * above zero.
     */
    ULONG EndpointRestarts;
    ULONG EndpointRestartsByPoll;
    /* Forced Stopped events that measured a partial transfer and had that length
     * latched onto it. It is the only measurement a cancelled transfer ever
     * gets, so this is what makes `AbortTransfer`'s reported byte count a
     * measurement rather than the zero the record was created with. */
    ULONG StoppedLengthsLatched;
    /*
     * Aborts that found their transfer on the **completion list** rather than on
     * an endpoint queue - a Transfer Event had already claimed it and it was
     * waiting for `UsbPortCompleteTransfer` with the controller lock dropped.
     * Taking it off that list is what stops the completion running against a
     * record usbport freed the moment the abort returned.
     */
    ULONG AbortsFromCompletionList;
    /*
     * Aborts that arrived while another context was **inside**
     * `UsbPortCompleteTransfer` for that transfer - off both the endpoint queue
     * and the completion list, with the controller lock dropped for the service
     * call. Only reachable on SMP.
     *
     * The *overlap with the call* is the part this driver cannot close: it can
     * neither recall the call nor wait for it from a callback that may not wait.
     * The part it can and does close is the interval between taking the transfer
     * off the list and naming it here, which happens in one lock hold. What is
     * left is answered with the length the transfer measured and counted, so the
     * 7a-V SMP run can say whether it happens at all.
     */
    ULONG AbortsDuringCompletion;
    /*
     * Aborts that arrived while this endpoint's Stop Endpoint was still
     * outstanding - the window `SetEndpointState(PAUSED)` narrows and no
     * miniport can close, because `AbortTransfer` runs at DISPATCH under a
     * usbport spin lock and may not wait for a command. Expected zero: usbport's
     * own frame gate puts at least one frame between the pause and the abort.
     * A nonzero reading is the measurement of how often the xHC could still
     * have been executing TRBs whose buffer usbport then unmapped.
     */
    ULONG AbortsBeforeStopped;
    ULONG EndpointReconfigures;
    /*
     * Endpoints the xHC reported **Disabled** to a Stop Endpoint and which were
     * therefore re-Added by a Configure Endpoint carrying the record's Add
     * Context flag and no Drop - the recovery for `XHCI_EPQ_NO_CONTEXT`.
     *
     * **Expected zero**, and it is the reading that says the controller dropped
     * an Endpoint Context this driver never asked it to drop: nothing here
     * issues a deconfigure or a Reset Device, so a nonzero value means either
     * the xHC lost the context by itself or this driver's picture of which
     * endpoints are configured has diverged from the controller's. The pipe is
     * restored either way - what the counter is for is that the restoration is
     * *silent* otherwise, and a device that quietly re-Adds an endpoint every
     * few seconds looks identical to one that is working.
     */
    ULONG EndpointContextRestores;
    /*
     * `SetEndpointState(REMOVE)` on an endpoint the xHC still has configured -
     * the declared limitation of task 7a-B.1, where the Endpoint Context and its
     * periodic bandwidth are held until the device's Disable Slot rather than
     * dropped, because a REMOVE cannot be told from a reopen.
     *
     * **It counts calls, not resources currently held**, and the two are not the
     * same: what is held at any moment is bounded by `XHCI_MAX_DEVICE_ENDPOINTS`
     * per device and released wholesale at teardown, so this cannot describe an
     * unbounded leak however high it climbs. What it is for is the *shape* -
     * a device repeatedly selecting alternate settings with fewer endpoints
     * drives it up without any matching reopen, and that is the case the
     * limitation would be worth revisiting for.
     */
    ULONG EndpointRemovesHeld;
    /*
     * `SetEndpointState(REMOVE)` for a default control pipe whose extension is
     * not the one the record is bound to: a superseded EP0 handle (issue 4,
     * task 19.7). XP's hub re-creates a device it is still enumerating through
     * a second usbport device handle - the port reset again, EP0 opened at
     * address 0 through a new extension, the device addressed again through it
     * - and removes the first handle's EP0 last. That REMOVE closes its own
     * extension and nothing else; the binding, the owed invalidate, the EP0
     * queue and any SET_ADDRESS in flight belong to the live handle.
     *
     * **Expected zero on Windows 98 and Windows 2000**, whose hubs have only
     * ever reopened EP0 through the same extension in any run here; nonzero
     * there is the reading that the two-handle restore is not XP's alone. On
     * XP it moves once per device the hub restores this way, and a device that
     * binds on its first attach while it moves is issue 4 handled.
     */
    ULONG Ep0RemovesSuperseded;
    ULONG EndpointQuiesceLost;
    ULONG EndpointQuiesceUnavailable;
    ULONG EndpointQuiesceFailures;
    /*
     * Devices torn down because an event carried Incompatible Device Error,
     * which Table 6-90 makes the Slot's business and not the endpoint's: the
     * code is "fatal as far as the Slot is concerned. Software shall issue a
     * Disable Slot Command to recover" (p.468). Added by audit round 8, which
     * found the code filed with the ordinary transfer errors and answered with a
     * halt recovery that leaves the slot exactly as the controller said it could
     * not use it.
     *
     * **Not expected zero, and that is the point.** It is the reading that says
     * a device on this bus is one the controller will not talk to - a
     * compatibility fault rather than a driver fault - and it is the only place
     * that shows up as itself rather than as a device that enumerates and then
     * stops answering. Counted per teardown, so a device that keeps being
     * re-enumerated by usbport and failing again drives it up once per attempt.
     */
    ULONG IncompatibleDeviceTeardowns;
    /* Pipes whose FAILED-by-unavailability a successful restore took back
     * (Phase 7 review, B3) - the pair XhciSlotResumeSweep cleared and
     * re-armed. Nonzero says a REMOVE or PAUSE really was delivered inside a
     * suspend window, which was unestablished when the sweep was written. */
    ULONG EndpointsRevivedByResume;
    ULONG TransfersOnHaltedEndpoint;
    ULONG TransfersAborted;
    ULONG AbortsUnmatched;
    ULONG TransfersNoOpped;
    ULONG TeardownsWithoutStop;
    /*
     * The three status callbacks, counted because batch 6-0 established which
     * endpoint callbacks are dead and these are **not** among them: both
     * shipping builds call all three (one `GetEndpointStatus` site, two each for
     * `SetEndpointStatus` and `SetEndpointDataToggle`). A zero here on a run
     * where a pipe stalled would say that reading is wrong on a target.
     */
    ULONG EndpointStatusQueries;
    ULONG EndpointStatusRunRequests;
    ULONG EndpointStatusOtherRequests;
    ULONG EndpointDataToggleResets;

    /*
     * The 32-bit frame number (task 6-B.1), and the three fields are what make
     * it monotone across a halt.
     *
     * `FrameNumber` is the published value and only ever increases.
     * `FrameLast` is the last MFINDEX frame observed, so the increment is a
     * *delta* rather than a recomputed absolute - MFINDEX is 11 bits of frame
     * and wraps every 2.048 s, and an absolute reading would go backwards on
     * every wrap. `FrameSynced` says whether `FrameLast` describes the running
     * controller: it is cleared whenever the number is answered without reading
     * the register, so the first read after a resume re-establishes the delta
     * base instead of counting a 2,048-frame jump.
     *
     * `FrameStalls` counts the answers given without a register read - a halted,
     * suspended, failed or undecodable controller - and it is not an error
     * count. Win98 idle-suspends within about half a second of every start, and
     * usbport's post-open wait is an *uncapped* loop that only ends when this
     * number passes a stamp (batch 6-0), so a reader that froze here would hang
     * the enumerating thread for good. `FrameReadFailures` is the strictly
     * worse sub-case, an all-ones read from a window that has stopped decoding.
     *
     * **Task 9-A.1 added a fourth property: while `FrameCongruent` is set, the
     * published number's low 11 bits *are* MFINDEX's Frame Index.** That is not
     * decoration - it is what lets an isochronous request use the frame stamps
     * usbport put on its packets as xHCI Frame IDs at all. usbport's stamps come
     * from this number and nowhere else, so if the two domains are only related
     * by an unknown offset, a Frame ID taken from a stamp names a different,
     * real frame 2,047 times out of 2,048.
     *
     * Congruence is established the same way the delta base is, at a resync, and
     * it costs one extra property of the resync: instead of adopting the new
     * `FrameLast` and publishing nothing, the number is advanced **forward** to
     * the next value congruent to it, by `(frame - FrameNumber) MOD 2048`. That
     * is at most 2,047 frames and never negative, so monotonicity - the one
     * thing usbport's uncapped wait depends on - is untouched, and a wait can
     * only be released sooner.
     *
     * The stall path clears it, because a number advancing one per *call* has no
     * relationship to the hardware's frame at all. An isochronous submission
     * taken while it is clear falls back to SIA rather than to a plausible Frame
     * ID, and `FrameNonCongruentSubmits` is what says how often that happened.
     */
    ULONG FrameLast;
    ULONG FrameSynced;
    ULONG FrameCongruent;
    /*
     * Task 9-A.1, review round 3. `FrameCongruent` says the published number and
     * MFINDEX agree in their low eleven bits; it says nothing about *when* that
     * was last established, and eleven bits cannot distinguish a gap of fifty
     * frames from fifty frames plus a whole 2,048-frame lap. `FrameSampleStale`
     * is the freshness half: set at every health poll and cleared only by a poll
     * that really read MFINDEX, so a Frame ID is claimed only within one poll
     * period of a reading. `FrameSamples` counts the readings.
     */
    ULONG FrameSampleStale;
    ULONG FrameSamples;
    ULONG FrameStalls;
    ULONG FrameReadFailures;

    /*
     * Task 9-A.1's isochronous scratch, and it is here rather than on the submit
     * path's stack because a worst-case group is a whole ring: 62 TRBs is 992
     * bytes and the per-TD length array another 248, which is not something to
     * add to a DISPATCH_LEVEL stack under usbport's own frames. AGENTS.md points
     * the same way - no private pool, so fixed software state belongs in the
     * extension usbport already allocates.
     *
     * **Shared by every isochronous endpoint, and the controller lock is what
     * makes that sound**: the build, the publish and the record all happen
     * inside one hold of it, so no second submission can be between them.
     * Nothing outside that hold may read either field.
     */
    XHCI_TRB IsoScratch[XHCI_XFER_MAX_ISO_TRBS];
    XHCI_ISO_LAYOUT IsoLayout;

    /*
     * The isochronous readings the *extension* keeps, as opposed to the
     * per-queue ones on XHCI_TRANSFER_QUEUE: these survive a device unplugging,
     * which is what makes them the run's numbers rather than one endpoint's.
     *
     *   `IsoSubmits` / `IsoPacketsSubmitted`  accepted requests and the packets
     *       in them. The ratio is the URB shape a real audio driver uses, which
     *       nothing in this project has measured and task 9-V will.
     *   `IsoSubmitsWithFrameId`  requests that went out with explicit Frame IDs
     *       rather than SIA. **Expected to read 0 on Windows 98**, where the
     *       controller idle-suspends and the published frame axis stops being
     *       congruent with MFINDEX; a nonzero reading there would be news. On
     *       Windows 2000 with a 1.1+ controller it should be most of them.
     *   `IsoRefusalsTooLarge`  the declared limit above being reached - a
     *       request needing more TRBs than the ring can hold. Nonzero says the
     *       pooled ring size is the wrong policy for real audio hardware, which
     *       is a decision to revisit rather than a defect to fix here.
     *   `IsoRefusalsMalformed`  a parameter block this driver could not encode.
     *       Expected 0; nonzero means the block layout task 9-0.1 recovered and
     *       what the running usbport builds disagree.
     *   `IsoSubmitsWrongType`  an isochronous request on a pipe that is not an
     *       isochronous endpoint. Expected 0 and counted apart from the above
     *       because it implicates the *routing*, not the request.
     *   `IsoRingUnderruns` / `IsoRingOverruns`  completion codes 14 and 15.
     *       Neither is a fault: they say the ring ran dry at an ESIT, which is
     *       what happens whenever software is late (4.10.3.1 p.185). A stream
     *       that plays cleanly still produces them at its start and stop.
     *   `IsoEventsUnattributed`  a Ring Underrun or Overrun that arrived for an
     *       endpoint with nothing queued and nothing to restart. Its own counter
     *       because it is the one shape that says the driver and the controller
     *       disagree about whether the pipe is running.
     *   `IsoDoorbellsSuppressed`  a Ring Underrun or Overrun with work queued
     *       and a slot to ring, whose restart doorbell was withheld because the
     *       quiescence machine owns the endpoint. **The opposite diagnosis to
     *       the line above and therefore not the same counter**: this is the
     *       healthy, expected case - a stale underrun racing a Stop Endpoint -
     *       and folding it into `IsoEventsUnattributed` would manufacture that
     *       counter's one alarming shape out of it.
     *   `IsoCadenceMismatches`  submissions whose per-packet frame stamps did not
     *       advance at the rate the Endpoint Context this driver programmed will
     *       consume TDs. Every one of these also forced the request to SIA, which
     *       is what it is for.
     *
     *       **Read what it can and cannot say.** A first draft of this comment
     *       called it "the one reading that can say the isochronous Interval is
     *       wrong", and the second review round refuted that: usbport stamps
     *       packets at a *fixed* indexing - `StartFrame + (i >> 3)` on High Speed,
     *       `StartFrame + i` otherwise - whatever the endpoint descriptor says,
     *       and this driver's Interval is assumed from that same fixed cadence.
     *       Both sides of the comparison therefore contain the assumption and
     *       neither contains `bInterval`, so a High-Speed endpoint with
     *       `bInterval = 4` - a common shape for USB Audio - is programmed eight
     *       times too fast and **agrees with itself here**. What the counter
     *       really detects is the *speed* disagreement: Phase 5 task 7 reports
     *       every connected root port as High Speed, so a Full-Speed device
     *       attached directly is stamped eight packets per frame against a
     *       one-per-frame ESIT. That is worth detecting and is why the gate
     *       exists, but it is not the interval question. Closing that needs
     *       `bInterval`, which the miniport can only get by snooping the
     *       configuration descriptor on EP0 - the channel task 7b-A.1 already
     *       built for hub descriptors. **Task 9-A.2 built that snoop**
     *       (src/xhci_desc.h), so an endpoint whose descriptor was read no
     *       longer contains the assumption on either side: its Interval is
     *       derived, usbport's stamps still are not, and the disagreement that
     *       follows is counted as `IsoCadenceMismatchesDerived` instead. This
     *       row therefore keeps exactly its original meaning and now covers only
     *       the endpoints still on the assumption.
     *   `IsoTrbErrorRecoveries` / `IsoTrbErrorsUnarmable`  completion code 5 on
     *       an isochronous endpoint, which is the xHC refusing a TRB this driver
     *       built - a host-side encoding fault rather than anything the device
     *       did, and the one isochronous error software must act on (4.8.3 p.149
     *       puts it in the Error state, which runs nothing). Expected **0**; any
     *       reading at all is a defect in this driver's TRB encoding, and the
     *       second row is the subset where the endpoint's chain had already been
     *       given up so no recovery could be armed.
     */
    ULONG IsoSubmits;
    ULONG IsoPacketsSubmitted;
    ULONG IsoSubmitsWithFrameId;
    ULONG IsoCadenceMismatches;
    /*
     * Task 9-A.2: the same observation on an endpoint programmed at a cadence
     * **that differs from the one usbport stamps to**, where a disagreement is
     * the expected outcome rather than a finding.
     *
     * Its own counter for the reason task 8's lifecycle review states as a rule:
     * never let two causes with opposite diagnoses share a counter in a free
     * build. A mismatch on an endpoint carrying usbport's own cadence says that
     * cadence is wrong and is news; a mismatch on one carrying the *device's*
     * says usbport is stamping packets at a rate the device did not ask for -
     * which is precisely what the derivation exists to disagree with - and the
     * request going out with SIA is the right answer, not a fallback.
     *
     * **Split by the Interval's value rather than by its provenance** (review
     * round 2): a derived Interval that comes out equal to the assumption
     * behaves exactly like the assumption, so its mismatch means what the
     * assumption's means - Phase 5 task 7's root-port speed report - and
     * charging it here would hide that fault behind the derivation.
     */
    ULONG IsoCadenceMismatchesDerived;
    ULONG IsoTrbErrorRecoveries;
    ULONG IsoTrbErrorsUnarmable;
    ULONG IsoRefusalsTooLarge;
    ULONG IsoRefusalsMalformed;
    ULONG IsoSubmitsWrongType;
    ULONG IsoRingUnderruns;
    ULONG IsoRingOverruns;
    ULONG IsoEventsUnattributed;
    ULONG IsoDoorbellsSuppressed;
    /* The per-queue isochronous counters, folded across every endpoint that has
     * lived, exactly as the mid-TD family above is folded - a queue dies with
     * its device record and the question is about the run. */
    ULONG IsoPacketsAnsweredTotal;
    ULONG IsoPacketErrorsTotal;
    ULONG IsoMissedServiceTotal;
    ULONG IsoGroupsAwaitingTailTotal;
    /* How far a resync had to advance the published number to restore
     * congruence, summed. Zero on a controller that never stalls; on Win98,
     * where every idle suspend stalls it, this is the measure of how much of the
     * frame axis the stall path invented. */
    ULONG FrameResyncSkew;

    /*
     * Task 9-A.2's configuration-descriptor snoop (src/xhci_desc.h), and the
     * block exists to answer one question a target run has never been able to
     * ask: **is an isochronous endpoint's `bInterval` actually 1?** The roadmap
     * task says in as many words that the honest outcome may be the published
     * limitation rather than the feature, and these are what decide which.
     *
     * Read in this order, because each line only means something given the one
     * above it:
     *
     *   `DescRepliesFolded`  configuration-descriptor replies that reached the
     *       walk. Zero on a bus with devices on it means the *channel* is gone -
     *       the snoop, the mapping, or the completion ordering - not that no
     *       device has a configuration. It is the sum of the **four** outcomes
     *       below - committed, refused as inactive, partial, malformed - and
     *       that identity is asserted at the end of the host suite's run rather
     *       than left as arithmetic in a comment, so a fifth exit added later
     *       fails a check instead of quietly shrinking the total. (An earlier
     *       draft named a `XhciDescCountsConsistent` function that was never
     *       written and called the partition three-way; review round 2 caught
     *       both.)
     *   `DescConfigsCommitted`  walks that covered a whole `wTotalLength`. This
     *       is the only outcome that updates a device's table.
     *   `DescConfigsPartial`  replies that stopped short. **Expected nonzero and
     *       roughly equal to the committed count**: usbport reads the header
     *       first to learn `wTotalLength` and then re-reads the whole thing, so
     *       the ordinary enumeration produces one of each.
     *   `DescConfigsMalformed`  descriptors that did not fit their own lengths.
     *       Expected 0; nonzero says either a device is lying or this walk is
     *       reading a reply that is not a configuration descriptor.
     *   `DescConfigsInactive`  a complete walk **refused** because it describes
     *       a configuration the device is not running. Expected 0, and nonzero
     *       only for a multi-configuration device whose other configuration was
     *       read after it was configured.
     *   `DescConfigsSuperseded`  a SET_CONFIGURATION selecting a configuration
     *       other than the one the committed table describes, which throws the
     *       table away. **Expected 0** for the same reason, and it is the other
     *       direction of the same guard: this one is the descriptor arriving
     *       first, `DescConfigsInactive` is the selection arriving first.
     *   `DescInterfacesSelected` / `DescInterfacesDropped`  SET_INTERFACE
     *       requests observed, and ones for an interface past the tracked set.
     *       **A dropped selection makes that interface's alternate unknown**,
     *       and since review round 3 an unknown alternate refuses a lookup
     *       rather than reading as alternate 0 - so a nonzero second row means
     *       endpoints fell back to the assumption, not that they were answered
     *       from a guess.
     *   `DescSelectionsOffAddress`  a SET_INTERFACE that completed successfully
     *       on a record which **no longer answers to the address it was sent
     *       to** - a disown clears `DeviceAddress` without advancing the
     *       tenancy, so the orphan test above passes and the record is still
     *       genuinely its own. The device's half of the selection is applied
     *       anyway (it writes that record's own state); the *graph's* half is
     *       not, because the graph is keyed on usbport's address and usbport
     *       may have given that address to something else by now. **Expected
     *       0**: it needs a port disowned with a SET_INTERFACE in flight.
     *   `DescRepliesUnmapped` / `DescRepliesOrphaned`  a snooped reply with no
     *       mapped buffer to read, and an action whose device record was gone -
     *       or had been re-allocated to a different device - by the time its
     *       transfer completed. Neither reached the walk, so neither is in
     *       `DescRepliesFolded`; they are counted apart from each other and from
     *       the outcomes above because one is a channel failure and the other is
     *       a device that left mid-enumeration, which are different diagnoses
     *       and neither may be a silence.
     *   `DescIsoEntries` / `DescIsoEntriesDropped` / `DescIsoBadInterval` /
     *   `DescIsoBadDescriptor`  what the committed configurations declared, as
     *       **declarations rather than endpoints**: one interface's alternate
     *       settings each declaring the same endpoint address are several
     *       entries, deliberately, since they may ask for different cadences.
     *       Then ones past the per-device table's size, ones whose `bInterval`
     *       is outside the range Table 6-12 can convert, and ones the bytes
     *       would not yield at all. The last three are each a reason an endpoint
     *       silently falls back to the assumption.
     *   `DescIntervalsDerived` / `DescIntervalsAssumed`  isochronous endpoint
     *       opens decided each way. **This pair is task 9-A.2's verdict**: all
     *       assumed on a bus with an audio device on it means the snoop is not
     *       reaching the endpoints that need it.
     *   `DescIntervalsUnresolved`  an isochronous open where the device *did*
     *       declare that endpoint address and the declarations could not be
     *       resolved to one cadence. Its own row because it is the one shape
     *       where falling back is known to be answering a question wrongly
     *       rather than not answering it. It is counted at the **lookup**, so it
     *       is very nearly a subset of `DescIntervalsAssumed` and not exactly
     *       one: an open the context builder then refuses for some other reason
     *       is in this row and in neither of the other two.
     *   `DescIntervalsAgreed`  derived Intervals that came out equal to what the
     *       assumption would have programmed, i.e. `bInterval = 1`. Equality
     *       with `DescIntervalsDerived` means every isochronous endpoint that
     *       was **built and derived** agreed with the assumption - a
     *       *measurement* over those endpoints, not a statement about every
     *       device: one that never binds or never opens the endpoint (the
     *       Sound Blaster X4, `bInterval` 3 and 4, run-13e.md Finding Y) is
     *       outside the comparison. *(Until a later review this said equality
     *       covered "every audio device this project can reach" and named a
     *       "publish it instead" outcome that batch 13-E has since overtaken.)*
     *   `DescIntervalsStaleAfterSelect`  an action that resolved an **already
     *       open and still bound** isochronous endpoint to a cadence other than
     *       the one it is being programmed with (its `PendingParams`, which is
     *       what a reopen would have to disagree with). Every action that can
     *       move the answer asks - a selection of either kind, and a
     *       configuration descriptor that installed a table - so it counts
     *       *observations of a mismatch* rather than endpoints or transitions,
     *       and it is read as zero-or-not rather than by magnitude. This is the
     *       reading that says which
     *       way usbport orders its pipe reopen against its `SET_INTERFACE`, an
     *       ordering this repository has not established: **0 says the question
     *       never arises** (the reopen follows, so every endpoint is opened
     *       with its alternate already known), and nonzero says an endpoint is
     *       running at the wrong cadence and a reprogram is owed. It is
     *       measured rather than repaired on purpose - the repair is a Drop+Add
     *       on a live isochronous pipe that usbport did not ask for.
     *   `DescIntervalsRefused`  a recorded `bInterval` the context builder would
     *       not convert for the endpoint's speed. Expected 0: the walk records
     *       only 1..16 and both speeds accept all of it, so the reachable cause
     *       is an isochronous endpoint on a Low-Speed device, which is not a
     *       legal USB shape at all.
     */
    ULONG DescRepliesFolded;
    ULONG DescConfigsCommitted;
    ULONG DescConfigsPartial;
    ULONG DescConfigsMalformed;
    ULONG DescConfigsInactive;
    ULONG DescConfigsSuperseded;
    ULONG DescInterfacesSelected;
    ULONG DescInterfacesDropped;
    ULONG DescSelectionsOffAddress;
    ULONG DescRepliesUnmapped;
    ULONG DescRepliesOrphaned;
    ULONG DescIsoEntries;
    ULONG DescIsoEntriesDropped;
    ULONG DescIsoBadInterval;
    ULONG DescIsoBadDescriptor;
    ULONG DescIntervalsDerived;
    ULONG DescIntervalsAssumed;
    ULONG DescIntervalsUnresolved;
    ULONG DescIntervalsAgreed;
    ULONG DescIntervalsRefused;
    ULONG DescIntervalsStaleAfterSelect;
    /* Assigned to each device record at allocation - see XHCI_DEVICE.Tenancy.
     * Never reset within a start, which is what makes a stale value stale. */
    ULONG DeviceTenancyNext;

    /*
     * Controller Save/Restore State (task 6-B.6). `SaveAttempts`/`SaveFailures`
     * and `RestoreAttempts`/`RestoreFailures` are counted apart because the two
     * operations fail for different reasons and only one of them costs the bus:
     * a save that fails means the resume must reinitialise, a restore that fails
     * means every addressed device is gone.
     *
     * `SaveRestoreTimeouts` is the SSS/RSS poll that never cleared, which is a
     * third outcome again - the controller neither succeeded nor reported an
     * error. `LastSaveRestoreStatus` is the USBSTS behind the last verdict, so a
     * release build can say which bit decided it.
     *
     * **A set SRE is the expected reading in both target VMs** and not a defect:
     * QEMU 11.0.0 implements CRS as `usbsts |= SRE` and nothing else, so every
     * restore there takes the error path (batch 6-0). The success path is
     * bare-metal-only.
     */
    ULONG SaveAttempts;
    ULONG SaveFailures;
    ULONG RestoreAttempts;
    ULONG RestoreFailures;
    ULONG SaveRestoreTimeouts;
    ULONG LastSaveRestoreStatus;
    /*
     * Events the restore received and dropped before setting R/S, discharging
     * save step 2 late (`XhciEventDiscardStale`). **A nonzero reading is not a
     * fault**: it says the suspend abandoned a command the controller had
     * already completed, which is the ordinary race the step exists for. It is
     * separate from every drain counter because those measure events that were
     * *acted on* and this one measures events deliberately not acted on - and
     * because it is the only evidence, on a machine, that the collision audit
     * round 5 found was ever live there. **Both flavours can read it**: it is in
     * `CheckController`'s dump and in the always-on stored-log counter block,
     * which audit round 6 found it was not - a counter described as the only
     * evidence has to be retrievable from the build that ships.
     *
     * **It excludes the fatal ones below**, which audit round 7 found it
     * including: an event that refuses the restore is an event acted on, and
     * counting it here as well would have a machine read the same event as a
     * fault and as harmless residue at the same time.
     */
    ULONG RestoreEventsDiscarded;
    /*
     * The events the drain above may **not** simply drop, counted where they are
     * found. Three rounds have now widened this set, each finding its material
     * inside the previous round's fix, and the shape of the mistake was the same
     * every time: an enumeration of fatal events written out by hand.
     *
     * A **Host Controller Event** is a controller-level fault reported nowhere
     * in `USBSTS` - neither Event Ring Full nor Event Lost sets `HCE` or `HSE` -
     * so a stale one is a diagnosis the save gate never excluded and the resume
     * would otherwise discard on its way to restarting the very controller that
     * raised it. **Audit round 6 found that, in round 5's own fix**: the three
     * reasons that comment gave for dropping an event cover Command Completion,
     * Transfer and Port Status Change and nothing else.
     *
     * A **Transfer Event carrying `Event Lost`** (completion code 32) is the
     * same loss reported per endpoint rather than per controller: "An Event Lost
     * Error shall be generated for the endpoint ... [and] shall halt the
     * endpoint" (4.10.1, p.173). **Audit round 7 found that, in round 6's own
     * fix**, whose prose said Event Lost is reported *only* as a Host Controller
     * Event - true of Event Ring Full, false of this.
     *
     * **Audit round 8 then found that "there are two" was itself the defect.**
     * Table 6-90 marks a third code fatal in as many words - "An Undefined Error
     * shall be treated as a fatal error by software" (33, p.469) - and hands the
     * whole vendor error range 192-223 the same reading for any code software
     * does not recognise, which is all of them here. A fourth, Incompatible
     * Device Error (22), is fatal to the *slot* and names a Disable Slot as its
     * recovery (p.468), which a successful restore would silently discard along
     * with the event. So the drain no longer carries a list of codes at all: it
     * asks `XhciXferCodeInfo`, which is the one place Table 6-90 is transcribed,
     * and a code that becomes fatal there becomes fatal here in the same edit.
     *
     * Finding any of them fails the restore, which is not an escalation to
     * usbport but a fall-through to the resume's reinitialisation - HCRST, a
     * fresh event ring and every device rebuilt - which is the stronger repair
     * of the two and one this path already has. For the slot-fatal code that
     * rebuild is also what discharges the Disable Slot: the slot does not
     * survive the reinitialisation to need one.
     */
    ULONG RestoreEventsFatal;
    /*
     * Which kind the last one was (`XHCI_RESTORE_FATAL_*`) and the completion
     * code it carried, both stored because **audit round 8 found the release
     * build unable to tell the two apart while three separate places claimed it
     * could**. `RestoreEventsFatal` is a count and says nothing about kind;
     * `LastHostControllerCode` is written only by the Host Controller Event arm,
     * so a machine reading a nonzero count beside a zero or stale code could not
     * distinguish "an Event Lost refused the restore" from "a Host Controller
     * Event refused it and the code register was never written".
     *
     * Round 7's answer had been that 32 is the code by construction. That was
     * true of the arm as it stood and stopped being true the moment the arm
     * became "any fatal completion code" - which is exactly the fragility of
     * deriving a reading from what the code currently happens to admit.
     *
     * Both are last-wins rather than accumulating: a restore that finds several
     * fails on the first one it would report, and the count beside them says how
     * many there were.
     *
     * **And "last" spans restores, not just events within one - audit round 9
     * asked which.** Neither field is cleared at the start of an attempt, so a
     * refusal's kind and code survive every later *successful* restore and a
     * snapshot taken after a good resume still carries them. That is the
     * intended reading: a refusal is worth keeping, and a resume that worked
     * afterwards does not make the one that did not uninteresting. What round 9
     * found was that nothing said so where a machine reads it, so the external
     * labels are now `restore: last fatal - ...` and `restore.lastfatal.*`
     * rather than "refused", which invited pairing them with a
     * `RestoreEventsFatal` of zero.
     */
    ULONG RestoreFatalKind;
    ULONG RestoreFatalCode;
    /*
     * The two refusals that happen **before** SaveAttempts is incremented, so
     * they are not save failures and must not be counted as any: nothing was
     * written to the controller either time.
     *
     * `SavesDeclinedNoFsc` is the controller declining to be saved at all - it
     * does not report the Force Save Context Capability, so this driver cannot
     * make the saved image complete without a Stop Endpoint pass it does not
     * have (4.23.2 p.313; see xhciSaveState). It is the *expected*, steady
     * reading on a 1.0 controller, which is two of the three qualified fleet
     * machines, and a nonzero value there means every resume reinitialises. It
     * should read zero on any 1.1 or later part, where FSC is mandatory.
     *
     * `SavesDeclinedCommandBusy` is step 2 of the same procedure - a command
     * ring that is neither Stopped nor Idle. It should read zero always, and
     * that is a statement about the code rather than a hope: the suspend's
     * quiesce abandons any outstanding command and clears R/S before a save is
     * attempted, so both of the step's alternatives already hold. The check
     * behind this counter is a guard on the restore's ring reinitialisation, not
     * a branch anything is expected to take - see xhciSaveState.
     */
    ULONG SavesDeclinedNoFsc;
    ULONG SavesDeclinedCommandBusy;
    /* The suspend left a saved state the next resume may restore from. Cleared
     * by anything that invalidates it - a failed save, a reinitialisation, a
     * restore that has been consumed - because a restore attempted without one
     * is undefined behaviour rather than a failed restore (4.23.2, p.315). */
    ULONG SavedStateValid;

    /*
     * Task 6-V.1's transfer-contract probe (src/xhci_probe.c). Instrumentation
     * only: nothing in the driver reads any of it, and its whole purpose is to
     * make a *published* build able to answer questions a trace line answers
     * only while the traced (qemu) build is running.
     *
     * `ProbeSgShape`/`ProbeEpShape` are OR-accumulated shape words - the set of
     * properties seen since this controller started - and the `...Firings`
     * beside each is what says the announcement gate worked, since a set is
     * idempotent under OR and looks the same whether it fired or not.
     *
     * The counters split the way this repo's counters always split, one cause
     * each: `ProbeSgDisordered` is elements not ascending by `SgOffset` - the
     * one thing this probe exists to measure and the one the transfer engine
     * already defends against - while `ProbeSgGapped` is a list that does not
     * tile its buffer, `ProbeSgLengthGaps` one whose lengths do not sum to
     * `TransferBufferLength`, and `ProbeSgHighDwords` an element with a
     * physical address above 4 GB. Those are four different failures with four
     * different diagnoses.
     *
     * `ProbeEpEvents[]` is the runtime witness for two of batch 6-0's
     * both-binaries negatives: a nonzero `XHCI_PROBE_EVENT_CLOSE` or
     * `..._GET_STATE` entry means a shipping build *does* call a callback this
     * driver stubs on the strength of a static read.
     *
     * `ProbeEpWithTt` and `ProbeHubClassSetups` are Phase 7b's feasibility
     * measurement (design doc 02 section 4): the first counts endpoint-property
     * blocks carrying a real transaction-translator hub address, the second
     * hub-class setup packets on a device's own default pipe. Both are expected
     * to be **zero** in a QEMU run with no hub on the bus - a root-port device
     * has no TT, and usbport answers the root hub's own class requests itself -
     * so a nonzero reading is the finding, not the pass. Batch 7b-V0 attached a
     * hub and read both nonzero on both targets.
     *
     * `ProbeTtPairs` is task 7b-A.1.2's raw reading of the same fields, and it
     * exists because `ProbeEpWithTt` and the two shape bits cannot answer the
     * question that is left. Batch 7a-0's corrected reading of `PortNumber` -
     * the port on the nearest **High-Speed ancestor**, at any depth - and the
     * superseded one - the port on the immediate parent - agree on every
     * topology shallower than two non-High-Speed tiers, and where they disagree
     * they disagree about the *number*. So the pairs are kept as numbers, one
     * entry per distinct pair, with the device addresses and speeds each was
     * seen with (see XHCI_PROBE_TT_TABLE). `ProbeTtObservations` counts the
     * entry to the fold, so `ProbeTtObservations == sum(Entry[].Observations) +
     * Dropped` is an identity the host suite enforces - the same shape as task
     * 7b-A.1.0's open accounting, and for the same reason: a table assembled
     * from the paths somebody noticed is only as complete as whoever noticed.
     */
    ULONG ProbeTransfers;
    ULONG ProbeSgShape;
    ULONG ProbeSgShapeFirings;
    ULONG ProbeSgElementsMax;
    ULONG ProbeSgDisordered;
    ULONG ProbeSgGapped;
    ULONG ProbeSgLengthGaps;
    ULONG ProbeSgHighDwords;
    ULONG ProbeSgMapped;
    ULONG ProbeSgDumps;
    ULONG ProbeSplitTransfers;
    /*
     * Two counters, because the first Win2000 run showed one of them answering
     * a question it was not asked. `ProbeClassSetups` is every class-type
     * request - any class driver's - and `ProbeHubClassSetups` only those whose
     * recipient is a device or a port, which is what design doc 02's topology
     * snooping reads. The second is the one expected to be zero in a VM.
     */
    ULONG ProbeClassSetups;
    ULONG ProbeHubClassSetups;
    ULONG ProbeEpShape;
    ULONG ProbeEpShapeFirings;
    ULONG ProbeEpWithTt;
    ULONG ProbeTtObservations;
    XHCI_PROBE_TT_TABLE ProbeTtPairs;
    ULONG ProbeEpEvents[XHCI_PROBE_EVENT_COUNT];
    XHCI_PROBE_KEYSET ProbeSetups;
    XHCI_PROBE_KEYSET ProbeEndpoints;

    /*
     * The task 7b-A.1 hub topology graph (src/xhci_topo.h): what the snooped
     * hub-class traffic has established about external hubs and where they
     * sit. Unlike the probe state above it, this is not instrumentation - task
     * 7b-A.2 marks a hub's Slot Context from it and task 7b-A.3 builds Route
     * Strings from it - but until those land nothing branches on it, and its
     * counters are the release build's reading of whether reconstruction
     * works at all, which is what 7b-A.1's own stop rule turns on. Mutated
     * only under the controller lock; zeroed by usbport before every
     * StartController like the rest of the extension, and reset explicitly at
     * stop so a stop-without-restart cannot leave a tree describing departed
     * devices.
     */
    XHCI_TOPOLOGY Topology;

    /*
     * How many transfer-error completions arrived after their completion
     * code had spent its record budget (task 11-V.9's fourth producer tier).
     * **It is the tier's own honesty check**: a nonzero reading says the ring
     * holds a sample of the errors and the counters above hold the rest, which
     * is precisely the difference between a bounded diagnostic and a trace that
     * quietly stopped being complete. Kept in the extension rather than in
     * XHCI_LOG because it is a fact about the run, and a reader takes it out of
     * a live guest by name through `offsets.txt` like the rest of the counters.
     */
    ULONG LogErrorsOverBudget;

    /*
     * The optional log ring (src/xhci_log.h): task 11-V.7's carrier and task
     * 11-V.9's producers. Off unless the driver's own software key names a file
     * or asks for the DebugView sink, and both values are read once per start -
     * so on an ordinary machine this is 16 KB of the extension that is written
     * by nothing and flushed by nothing.
     *
     * It is deliberately the **last** substantive member: a reader pulling
     * counters out of a live guest works from `scripts\local\offsets.txt`, and
     * putting a 16 KB array in the middle of the counter block would push every
     * later offset without changing anything about what those counters mean.
     *
     * Mutated only under the controller lock, like every other shared field
     * here, and zeroed by usbport before every StartController - which is why
     * both values are re-read at each start rather than cached across one.
     */
    XHCI_LOG Log;

    ULONG TrailingSignature;
} XHCI_EXTENSION, *PXHCI_EXTENSION;

/*
 * ==================================================================
 * The PassThru snapshot read channel - and it SHIPS, in every flavour
 * ==================================================================
 *
 * **What this is.** A way for a user-mode program to read this driver's own
 * miniport extension and raw PORTSC array off a running machine, through a
 * door `usbport.sys` already owns. Specified in
 * `docs\contributing\passthru-snapshot-instrument.md`; the host side is
 * `xhcisnap\`.
 *
 * **Why it is the answer to a question this driver could not otherwise
 * answer.** Windows 98 has exactly two logging families - ring 3 writes,
 * through a device object and a `\DosDevices\` link, or ring 0 writes a file -
 * and this driver is locked out of both. Family 1 is closed by Option A:
 * `USBPORT_RegisterUSBPortDriver` overwrites `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`
 * and `IRP_MJ_DEVICE_CONTROL` on our driver object before any version check,
 * so other Windows 98 drivers can log because they own a driver object and
 * this one runs inside somebody else's. Family 2 was measured shut by task
 * 11-V.7 and its imports were removed by task 13-L.2. **Both findings stand.**
 * What they miss is that this driver does not need a channel of its own,
 * because the port driver it lives inside already published one and will
 * forward through it. See
 * `docs\contributing\design\08-build-flavours-and-the-log-channel.md`,
 * sections 2 and 13.
 *
 * **The route** was read out of both shipping usbport builds -
 * `IOCTL_USB_USER_REQUEST` (0x00220438, METHOD_BUFFERED) with
 * `UsbUserRequest = 3`, on the `\DosDevices\HCD<n>` symbolic link usbport
 * always creates, ungated. Full contract, with every address, in
 * `docs\usb-xhci-info\usbport-miniport-abi.md` under "Debug / single-packet".
 * Three of its clauses shape everything below:
 *
 *   1. The `parameters` block is a NON-PAGED kernel copy usbport made of the
 *      system buffer and copies back afterwards. So there is no user address
 *      here, nothing to probe, and it is legal to fill under a spin lock at
 *      DISPATCH - which is what makes "lock, copy, return" possible at all.
 *   2. `PassThru` is entered at PASSIVE_LEVEL holding no usbport lock, so
 *      taking this driver's own controller lock is safe.
 *   3. usbport refuses `ParameterLength > 0x10000` before the miniport is ever
 *      reached. **sizeof(XHCI_EXTENSION) is larger than that**, so one call
 *      cannot carry the extension and the reader is a WINDOW over a region:
 *      the tool loops on Offset and concatenates. See TearDetector below for
 *      what that costs and how it is checked.
 *
 * **It is in every flavour, `release` included** (task 13-L.2, project owner
 * project owner), and it was `#ifdef XHCI_OBS_SNAPSHOT` until then. A debug-only
 * channel would have kept most of the defect it repairs: the user whose
 * machine misbehaves is running `release`, and telling them to install a
 * second binary before they can report anything is the same shape as telling
 * them to install one that does not load. **The flavour decides how much there
 * is to read, not whether the door exists.**
 *
 * **The door is shut until it is asked for.** `XhciLogVerbosity` in the
 * driver's own software key defaults to 0, and **rung 0 of that ladder IS the
 * shut door**: `xhciPassThru` does not engage, and returns exactly
 * `MP_STATUS_NOT_SUPPORTED`, which is the same answer a binary built without
 * any of this would give. That sameness is deliberate and it is a statement
 * about the ANSWER, not about the image - the two binaries differ by the whole
 * of `xhciPassThru`, so they are told apart by reading the installed file
 * (`fc /b`, and the `XHCI98_FLAVOUR_*` marker), never by asking the driver.
 *
 * *(A separate `XhciLogSnapshot` value held that consent until the merge, when
 * it merged into the ladder: it was a pure consent bit, the channel serves
 * everything or nothing, and consent nests inside depth. Design record 08
 * §13.2's dated amendment carries the reasoning and the security posture - of
 * which the load-bearing half is that **the enable step is the access control,
 * because this driver owns no other**: usbport hardcodes `\DosDevices\HCD<n>`,
 * completes `IRP_MJ_CREATE` with no work, and the IOCTL is `FILE_ANY_ACCESS`,
 * so Option A leaves the miniport no lever on the door itself.)*
 *
 * **The release build's import profile must not move**, which is why the copy
 * below is a byte loop rather than `RtlCopyMemory`/`memcpy`: those resolve to a
 * compiler intrinsic or an `ntoskrnl` import depending on build flags, and this
 * driver decides its import list on purpose. `scripts\build-driver.cmd` runs
 * the import gate after every link, so that is checked rather than inherited.
 *
 * **This block adds no field to XHCI_EXTENSION.** That was load-bearing while
 * the instrument had to decode against a tracked offset table taken from a
 * binary without it, and it stays true: nothing here is in the extension.
 * (Task 13-L.2 does move the layout, but through `XHCI_LOG`'s own switch
 * fields, not through this block - three arrived with the task and two left
 * again when `XhciLogSnapshot` merged into the ladder - and
 * `sizeof(XHCI_EXTENSION)` travels in every header for exactly that reason.)
 *
 * **The wire format is offset-free on purpose.** The header names sizes and
 * the payload is raw bytes; nothing here knows where a counter lives. The
 * decode is the existing host-side machinery (`scripts\local\regen-offsets.cmd`,
 * `offsets.txt`, `counters.py` / `readcounters.ps1`) against `ExtensionBytes` -
 * the same artifact the QEMU live-counter reader produces.
 *
 * **The one exception to that, and it is deliberate**: schema 2 adds a small
 * block of fields the tool can print with NO offset table at all - the flavour,
 * the verbosity tier read and applied, each switch's MPSTATUS, and the note
 * ring's location and fill. That is what makes `XHCISNAP`'s plain-text
 * companion possible, and the companion is what a stranger can paste into an
 * issue; the `.BIN` stays the attachment a maintainer decodes. It is **not** the
 * gather table design record 08 section 13.2 rejected: every one of these is a
 * derived offset or a scalar the compiler takes from the same struct, so it
 * cannot drift from the layout the way a hand-written table would.
 */

/* The one GUID this driver answers. Anything else must return exactly
 * MP_STATUS_NOT_SUPPORTED - see xhciPassThru for why "exactly". */
#define XHCI_SNAPSHOT_GUID_0    0x34F57942UL
#define XHCI_SNAPSHOT_GUID_1    0x40662D69UL
#define XHCI_SNAPSHOT_GUID_2    0x1482BD9CUL
#define XHCI_SNAPSHOT_GUID_3    0x20BC448EUL

/* 'X','S','N','Q' - what the caller puts in the block before the call, so a
 * GUID match against uninitialised memory is refused rather than answered. */
#define XHCI_SNAPSHOT_REQUEST_SIGNATURE 0x514E5358UL
/* 'X','S','N','P' - what comes back. */
#define XHCI_SNAPSHOT_SIGNATURE         0x504E5358UL
/*
 * **Schema 3, task 13-L.2 as amended.** Schema 1 was the probe-build
 * instrument's, whose header stopped at BuildFlags; schema 2 added the block
 * below; schema 3 is that block minus `SwitchStatusSnapshot`, which left with
 * the value it reported. The tool refuses any driver whose schema or header
 * size is not the one it was built against, and that refusal is correct rather
 * than unfortunate: a dump decoded against the wrong shape is a wrong reading,
 * not a failed one.
 *
 * **Bump this whenever the header CHANGES SHAPE, not only when it grows** - the
 * sentence here said "grows" until a field left, and a shrink is exactly as
 * much of a decode hazard. **Schema 2 never shipped** - the renumbering to 3
 * happened before the cut that first published any of this, so no field reading
 * in the wild was invalidated by it. `0.0.0.6` is the release schema 3 goes out
 * in, and from here a bump is a promise to a stranger's dump.
 */
#define XHCI_SNAPSHOT_SCHEMA            3UL

/* Which region a window is cut from. */
#define XHCI_SNAPSHOT_REGION_EXTENSION  0UL
#define XHCI_SNAPSHOT_REGION_PORTSC     1UL

/* Header Status bits. A window always comes back with a truthful header, so
 * every refusal below is reported here rather than through an MPSTATUS the
 * caller cannot tell apart from usbport's own. */
#define XHCI_SNAPSHOT_S_TRUNCATED       0x00000001UL /* region did not fit; loop on Offset */
#define XHCI_SNAPSHOT_S_BAD_REQUEST     0x00000002UL /* request signature wrong; nothing read */
#define XHCI_SNAPSHOT_S_BAD_EXTENSION   0x00000004UL /* not our extension; no payload */
#define XHCI_SNAPSHOT_S_BAD_REGION      0x00000008UL /* unknown Region; no payload */
#define XHCI_SNAPSHOT_S_PAST_END        0x00000010UL /* Offset >= RegionBytes; no payload */
#define XHCI_SNAPSHOT_S_NO_MMIO         0x00000020UL /* HcInfoStatus not OK; PORTSC not read */

/*
 * Header BuildFlags bits - what kind of binary produced this window.
 *
 * **There is deliberately no "the channel is disabled" bit**, here or anywhere
 * else in this format: a disabled channel never answers, so there is no header
 * for it to appear in. See the note above about the two binaries being told
 * apart by the file rather than by the reply.
 */
#define XHCI_SNAPSHOT_B_DEBUG           0x00000001UL
#define XHCI_SNAPSHOT_B_DIAGNOSTIC      0x00000002UL

/*
 * Which of the three build flavours produced this window (task 13-L.1).
 * `XHCI_SNAPSHOT_B_DEBUG` cannot say: it is `#if DBG`, which is set for BOTH
 * checked builds, and those are exactly the two that must not be confused -
 * one ships as the diagnostic download and one is never published. HOSTTEST is
 * the host suite's, so a vector cannot read a host build as a shipping one.
 */
#define XHCI_SNAPSHOT_FLAVOUR_UNKNOWN   0UL
#define XHCI_SNAPSHOT_FLAVOUR_RELEASE   1UL
#define XHCI_SNAPSHOT_FLAVOUR_DEBUG     2UL
#define XHCI_SNAPSHOT_FLAVOUR_QEMU      3UL
#define XHCI_SNAPSHOT_FLAVOUR_HOSTTEST  4UL

/*
 * One window's header, followed by PayloadBytes of raw bytes. Every field is a
 * ULONG so the layout is the same under every alignment rule MSVC 6.0 has, and
 * so a host-side reader decodes it with a single unpack.
 */
typedef struct _XHCI_SNAPSHOT_HEADER {
    ULONG Signature;        /* XHCI_SNAPSHOT_SIGNATURE                      */
    ULONG SchemaVersion;    /* XHCI_SNAPSHOT_SCHEMA                         */
    ULONG HeaderBytes;      /* sizeof(XHCI_SNAPSHOT_HEADER)                 */
    ULONG Status;           /* XHCI_SNAPSHOT_S_*                            */
    ULONG Region;           /* echoed from the request                      */
    ULONG Offset;           /* echoed from the request                      */
    ULONG RegionBytes;      /* the whole region's size, so the tool can loop */
    ULONG PayloadBytes;     /* how many bytes actually follow this header   */
    /*
     * The layout key, and it is filled even when nothing else could be: this
     * is what the host-side offset table is matched against, and a dump whose
     * ExtensionBytes does not match the table that decodes it is a WRONG
     * reading rather than a failed one.
     */
    ULONG ExtensionBytes;
    /* HcInfo.MaxPorts, or 0 if there is no MMIO. Filled for EVERY region: it
     * is a property of the controller rather than of the region asked for, and
     * a window that left it at 0 made the header only conditionally true. */
    ULONG PortCount;
    /*
     * **The window protocol's honesty check.** The extension cannot be carried
     * in one call (usbport's own 0x10000 cap), so a full dump is several
     * windows and the driver may run between them.
     *
     * **A SUM of four monotonic counters** - `CheckCallbacks`, which usbport
     * advances roughly twice a second; `DpcCount`, which the interrupt path
     * advances without going near it; and `Log.Appends` **plus**
     * `Log.Suppressed`, which between them count every producer call whether
     * or not the ladder let it record. The last pair has to be a pair: below
     * the recording rung - the cheapest level a dump can be taken at - a
     * producer bumps only `Suppressed`. It was `CheckCallbacks` alone until
     * the merge and that was far too narrow. All four only increase, so the
     * sum cannot return to an earlier value.
     *
     * **It is not a lock and it does not prevent tearing - it detects it**, and
     * it detects it in one direction: **unequal across two windows means the
     * dump IS torn.** Equal means none of those four moved, which is strong
     * evidence of coherence rather than a proof of it - there is no counter
     * behind every field, and `CheckCallbacks` is itself incremented outside
     * the controller lock, so holding that lock while reading this does not
     * serialise it.
     */
    ULONG TearDetector;
    ULONG BuildFlags;       /* XHCI_SNAPSHOT_B_*                            */

    /*
     * ---- schema 2 (task 13-L.2), amended to schema 3 ---------
     *
     * Everything below is what `XHCISNAP` can print with **no offset table**,
     * and that is the whole reason it is on the wire rather than left in the
     * `.BIN`. A user has no `offsets.txt`; a maintainer does. So the companion
     * text file is built out of these fields, and the raw extension stays the
     * attachment.
     *
     * The first requirement it meets is the one that cannot be met any other
     * way: **a reader holding a capture has to be able to tell a dump whose
     * ring recorded nothing from one that was never going to.**
     */
    ULONG Flavour;          /* XHCI_SNAPSHOT_FLAVOUR_*                      */
    /*
     * The verbosity tier - and since the snapshot-value merge this IS the switch,
     * so `Applied == 0` cannot appear in a header at all: at rung 0 the channel
     * declines and there is no header to appear in. Two fields, because they
     * differ exactly when something went wrong and that difference is the
     * diagnosis: `Read` is what the registry gave, `Applied` is what the driver
     * used. A value outside 0-4 is REFUSED rather than clamped - it falls back
     * to the default, which is off - so a dump never silently reports a tier
     * nobody asked for. **A refused value is therefore visible only as a
     * declining channel**, and `Read` is what the tool would have shown had it
     * been in range.
     */
    ULONG VerbosityRead;
    ULONG VerbosityApplied;
    /*
     * Each switch's read status, as the MPSTATUS
     * `UsbPortGetMiniportRegistryKeyValue` returned. usbport collapses "value
     * absent", "buffer too small" and "key would not open" into one code, so
     * these say "read or not" and never why - which is still the difference
     * between "the value never arrived" and "the value was zero".
     *
     * *(There were three until the merge. `SwitchStatusSnapshot` left with the
     * value it reported, and its departure is the whole of what separates
     * schema 3 from schema 2.)*
     */
    ULONG SwitchStatusVerbosity;
    ULONG SwitchStatusDebugView;
    /*
     * Nonzero once `xhciLogReadValues` has RUN, which is all it says: it is set
     * on that function's first line, before the registry service is tested.
     * **The machines that read nothing - no INF, a driver copied in by hand, a
     * packet with no registry service - all reach it and all report 1**, and
     * what separates them from a zero somebody set is the pair of
     * `SwitchStatus` fields above: SUCCESS beside a value of 0 is a real zero,
     * any other status beside 0 is nothing found. Either way the driver starts
     * normally, which is correct and must not read as a user's setting.
     * *(This field was documented as carrying that distinction itself, here and
     * in eight other places, until the post-Phase 13 review rounds.)*
     */
    ULONG SwitchRead;
    /*
     * Where the note ring is and how full it is, so the companion can print the
     * ring's text out of the `.BIN` without an offset table. Byte offsets from
     * the start of XHCI_EXTENSION, taken with XHCI_FIELD_OFFSET - the compiler
     * derives them from the same struct the payload is a copy of, so they
     * cannot drift the way a hand-written table would.
     */
    ULONG RingOffset;
    ULONG RingBytes;        /* the ring's capacity                          */
    ULONG RingHead;         /* next byte to write; the wrap point           */
    ULONG RingUsed;         /* bytes held, <= RingBytes                     */
} XHCI_SNAPSHOT_HEADER;

/*
 * What the caller writes into the block before the call. It overlays the first
 * three ULONGs of the header above, which is why the implementation reads all
 * of it before it writes any of it.
 */
typedef struct _XHCI_SNAPSHOT_REQUEST {
    ULONG Signature;        /* XHCI_SNAPSHOT_REQUEST_SIGNATURE */
    ULONG Region;           /* XHCI_SNAPSHOT_REGION_*          */
    ULONG Offset;           /* byte offset within that region  */
} XHCI_SNAPSHOT_REQUEST;



#endif /* XHCI_H */
