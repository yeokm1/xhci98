/*
 * xhci_usbport.h - the usbport.sys miniport ABI, as this driver declares it.
 *
 * Source of every offset, size, signature, and constant below:
 * docs/usb-xhci-info/usbport-miniport-abi.md (transcribed from the pinned ReactOS mirror,
 * then confirmed field-for-field against the three shipping usbport.sys /
 * usbehci.sys builds - see docs/usb-xhci-info/usbport-miniport-interface.md "Target ABI
 * record"). Nothing here comes from memory, and nothing here is copied from
 * ReactOS code: this is an independently written declaration of an
 * interoperability contract.
 *
 * The binary-confirmed facts this file encodes, so a future edit knows what it
 * is allowed to move:
 *   - Registration copies 0x12C (300) bytes when 100 <= Version < 200, 0x13C
 *     (316) when 200 <= Version < 300 - the whole packet every NT 5.x and 9x
 *     usbport knows - and, on NT 6.x only, 0x1E0 (480) at 300 <= Version <
 *     310. sizeof(USBPORT_REGISTRATION_PACKET) is therefore 0x1E0 since
 *     2026-09-11 (roadmap task 22.5): the 200 tier this driver always
 *     declared, followed by the Version 300 tier read slot by slot out of
 *     the four NT 6.x binaries (docs/usb-xhci-info/usbport-miniport-abi.md,
 *     "The Version 300 tier, slot by slot"). The driver presents 300 to an
 *     NT 6.x usbport and 200 to every other, so below NT 6.x the tail past
 *     0x13C is never copied and never read.
 *   - The miniport fills 0x00-0x130 and, in the 300 tier, InterruptDpcEx at
 *     0x178 alone; usbport writes 16 service pointers at 0xE4-0x120 and, at
 *     Version >= 300, two more at 0x1B0/0x1B4, and touches no other field
 *     before copying.
 *   - USBPORT_GetHciMn returns 0x57324B30 on both primary targets and
 *     0x10000001 on the XP lineage.
 *
 * **On amd64 those first two numbers are different, and the differences are
 * measured rather than derived** (design record 11 sections 5 and 8, phase 21):
 * the packet is `0x250` / `0x230`, every offset above the `OpenEndpoint` hinge
 * at `0x28` lands on `f(X) = 0x28 + (X - 0x28) * 2`, and three support
 * structures change size - `USBPORT_RESOURCES` (`0x34` to `0x48`),
 * `USBPORT_ENDPOINT_PROPERTIES` (`0x40` to `0x48`) and
 * `USBPORT_SCATTER_GATHER_LIST` (`0x40` to `0x50`, the one number here that is
 * still the compiler's rather than a binary's). `USBPORT_GetHciMn` returns
 * `0x10000001` on NT 5.2 amd64, which is the XP value this driver already
 * accepts, so no lineage constant follows from 64-bit support.
 *
 * The NT types the real header uses are deliberately *not* pulled in here.
 * Enums become ULONG and 64-bit fields become Lo/Hi ULONG pairs, per AGENTS.md
 * ("no enums for hardware layouts", "no 64-bit arithmetic"); the substituted
 * types are all 4-byte-exact on x86 - and, where it was checked, on amd64 too -
 * and the size/offset asserts at the bottom
 * of this file are what proves that claim rather than asserting it in prose.
 * The two members that are genuinely pointer-sized, `USBPORT_RESOURCES`'s
 * `InterruptAffinity` and `USBPORT_ENDPOINT_PROPERTIES`'s `BufferVA`, are
 * declared `ULONG_PTR` for that reason and are identical on x86.
 * The substitution also lets test/test_packet.c compile this header on the
 * build host with no DDK (docs/contributing/design/03-host-unit-tests.md).
 *
 * C89 only.
 */

#ifndef XHCI_USBPORT_H
#define XHCI_USBPORT_H

#include "xhci_compat.h"

/* ------------------------------------------------------------------ */
/* Registration call                                                   */
/* ------------------------------------------------------------------ */

/*
 * Version *argument* of USBPORT_RegisterUSBPortDriver. Confirmed gate in all
 * three shipping builds: < 100 is rejected outright, >= 200 selects the full
 * 316-byte packet. Not to be confused with the packet's MiniPortVersion field
 * below - conflating the two families is the classic way to fail registration.
 */
#define USB10_MINIPORT_INTERFACE_VERSION 100
#define USB20_MINIPORT_INTERFACE_VERSION 200

/*
 * The NT 6.x tier. Vista's and Windows 7's usbport select a copy size at four
 * thresholds - 100, 200, 300 and 310 - and the miniport interrupt DPC is taken
 * from a slot that exists only at >= 300 (packet+0x178 x86 / +0x298 amd64);
 * a Version 200 miniport's DPC is never called there at all. 300 rather than
 * 310 on purpose: 310 is the tier that hands out Windows 7's 64-bit DMA
 * adapter, which this driver does not implement (design record 11 section
 * 6). Presented only when IoIsWdmVersionAvailable(6, 0) answers TRUE; every
 * NT 5.x and 9x usbport was read and tests the version at 100 and 200 only.
 */
#define USBPORT_NT6_MINIPORT_INTERFACE_VERSION 300

/*
 * What the NT 6.x InterruptDpcEx callback returns. usbport reads bits 0 and 1
 * and treats them alike: either one makes it invalidate the root-hub interrupt
 * endpoint so the hub driver polls port status. Microsoft's own usbehci
 * returns 2 after a pass that saw a port with a connect, enable or
 * overcurrent change and 1 for a transfer interrupt with pending work; this
 * driver returns 2 after a pass that consumed a Port Status Change Event and
 * 0 otherwise. Read out of USBPORT_IsrDpc and EHCI_InterruptDpcEx, static.
 */
#define USBPORT_DPC_EX_TRANSFER_WORK    0x00000001UL
#define USBPORT_DPC_EX_PORT_CHANGE      0x00000002UL

/*
 * USBPORT_GetHciMn return values. The shipping usbehci.sys of each lineage
 * probes its own value in DriverEntry and refuses to register on a mismatch.
 * Both are accepted here: 0x57324B30 covers Win98+NUSB and Win2000 SP4,
 * 0x10000001 covers the XP lineage (a small isolated accommodation for a
 * best-effort secondary target - docs/usb-xhci-info/win98-wdm.md "What about Windows XP?").
 */
#define USBPORT_HCI_MN_W2K 0x57324B30UL
#define USBPORT_HCI_MN_XP  0x10000001UL

/* Packet MiniPortVersion field */
#define USB_MINIPORT_VERSION_OHCI 0x01
#define USB_MINIPORT_VERSION_UHCI 0x02
#define USB_MINIPORT_VERSION_EHCI 0x03
#define USB_MINIPORT_VERSION_XHCI 0x04

/* Packet MiniPortFlags field */
#define USB_MINIPORT_FLAGS_INTERRUPT    0x0001
#define USB_MINIPORT_FLAGS_PORT_IO      0x0002
#define USB_MINIPORT_FLAGS_MEMORY_IO    0x0004
#define USB_MINIPORT_FLAGS_USB2         0x0010
#define USB_MINIPORT_FLAGS_DISABLE_SS   0x0020
#define USB_MINIPORT_FLAGS_NOT_LOCK_INT 0x0040
#define USB_MINIPORT_FLAGS_POLLING      0x0080
#define USB_MINIPORT_FLAGS_NO_DMA       0x0100
#define USB_MINIPORT_FLAGS_WAKE_SUPPORT 0x0200

#define TOTAL_USB11_BUS_BANDWIDTH 12000
#define TOTAL_USB20_BUS_BANDWIDTH 400000

/*
 * USBPORT_RESOURCES.ResourcesTypes - and the bit positions MOVED on NT 6.x.
 *
 * NT 5.x sets one bit for a port resource however that port is mapped. NT 6.x
 * splits it in two - an I/O-space port and a memory-mapped one - and every
 * enumerator above it moves up one bit, so the mask this driver requires is
 * 0x06 there and 0x0C here.
 *
 * **Read out of USBPORT_ParseResources in five shipping binaries on
 * 2026-09-10, static.** Each has one descriptor-scan loop (CmResourceType 1
 * port, 2 interrupt, 3 memory; stride 0x14) that records the first descriptor
 * of each type in its own register, and three branches below it that OR a
 * constant into offset 0 of the USBPORT_RESOURCES the caller passed. Which
 * branch is which is fixed twice over: by the register the scan loop filled,
 * and by the USB_MINIPORT_FLAGS_* bit each branch tests as its guard
 * (INTERRUPT 0x01, PORT_IO 0x02, MEMORY_IO 0x04 - the values above).
 *
 *   NT 5.2  winxp64    port 1 (both mappings, one site), interrupt 2, memory 4
 *   NT 6.0  vista-x64  port 1 I/O or 2 mapped,           interrupt 4, memory 8
 *   NT 6.1  win7-x64   the same
 *   NT 6.0  vista-x86  the same
 *   NT 6.1  win7-x86   the same
 *
 * RVAs, the exact commands and the instructions are in design record 11
 * section 6.2. Provenance: legal-provenance.md section 4.
 *
 * The 0x0C a Vista x64 guest reported on 2026-09-10 - with a fully populated
 * interrupt block behind it - was guessed to be 0x06 shifted one bit left. It
 * is: the inserted member is the second port bit.
 */
#define USBPORT_RESOURCES_PORT      1
#define USBPORT_RESOURCES_INTERRUPT 2
#define USBPORT_RESOURCES_MEMORY    4

/*
 * The NT 6.x assignment of the same field. Bit 0 is an I/O-space port and bit
 * 1 a memory-mapped one, where NT 5.x used bit 0 for both; interrupt and
 * memory sit one bit higher than their NT 5.x namesakes. Selected at runtime
 * in DriverEntry - src\xhci_dispatch.c, XhciResourcesRequired - and only in
 * the amd64 build, for the reason the arity branch beside it gives.
 */
#define USBPORT6_RESOURCES_PORT_IO   1
#define USBPORT6_RESOURCES_PORT_MEM  2
#define USBPORT6_RESOURCES_INTERRUPT 4
#define USBPORT6_RESOURCES_MEMORY    8

/* Miniport callback return values. usbport treats any nonzero StartController
 * return as failure. */
#define MP_STATUS_SUCCESS       0
#define MP_STATUS_FAILURE       1
#define MP_STATUS_NO_RESOURCES  2
#define MP_STATUS_NO_BANDWIDTH  3
#define MP_STATUS_ERROR         4
#define MP_STATUS_RESERVED1     5
#define MP_STATUS_NOT_SUPPORTED 6
#define MP_STATUS_HW_ERROR      7
#define MP_STATUS_UNSUCCESSFUL  8

#define RH_STATUS_SUCCESS      0
#define RH_STATUS_NO_CHANGES   1
#define RH_STATUS_UNSUCCESSFUL 2

/* Endpoint states (Get/SetEndpointState). There is no state 1. */
#define USBPORT_ENDPOINT_UNKNOWN 0
#define USBPORT_ENDPOINT_PAUSED  2
#define USBPORT_ENDPOINT_ACTIVE  3
#define USBPORT_ENDPOINT_REMOVE  4
#define USBPORT_ENDPOINT_CLOSED  5

/* Endpoint status (Get/SetEndpointStatus) */
#define USBPORT_ENDPOINT_RUN     0
#define USBPORT_ENDPOINT_HALT    1
#define USBPORT_ENDPOINT_CONTROL 4

/*
 * USBPORT_ENDPOINT_PROPERTIES.DeviceSpeed - the NT usbdi USB_DEVICE_SPEED enum,
 * declared here rather than pulled in from usbdi.h for the reason the header
 * comment gives (an enum becomes a ULONG and this file stays DDK-free).
 *
 * **Not this driver's XHCI_SPEED_* vocabulary, and not the PORTSC Port Speed
 * field either.** All three number the speeds differently, so a value crossing
 * between them is converted rather than assigned: usbport's 2 is High Speed
 * where PORTSC's 2 is Low Speed, which is a silent mistranslation in both
 * directions (docs/usb-xhci-info/usbport-miniport-abi.md section 4).
 */
#define UsbLowSpeed     0
#define UsbFullSpeed    1
#define UsbHighSpeed    2

/* Transfer types (USBPORT_ENDPOINT_PROPERTIES.TransferType) */
#define USBPORT_TRANSFER_TYPE_ISOCHRONOUS 0
#define USBPORT_TRANSFER_TYPE_CONTROL     1
#define USBPORT_TRANSFER_TYPE_BULK        2
#define USBPORT_TRANSFER_TYPE_INTERRUPT   3

/* UsbPortInvalidateController Type argument */
#define USBPORT_INVALIDATE_CONTROLLER_RESET           1
#define USBPORT_INVALIDATE_CONTROLLER_SURPRISE_REMOVE 2
#define USBPORT_INVALIDATE_CONTROLLER_SOFT_INTERRUPT  3

#define USBPORT_TRANSFER_DIRECTION_OUT 1
#define USBPORT_MAX_DEVICE_ADDRESS     127

typedef ULONG MPSTATUS;
typedef ULONG RHSTATUS;

/* ------------------------------------------------------------------ */
/* Support structures                                                  */
/* ------------------------------------------------------------------ */

/*
 * StartController's argument. usbport has already connected the interrupt and
 * placed the MiniPortResourcesSize common buffer at StartVA/StartPA (both
 * page-aligned) before this arrives; ResourceBase is BAR0 already mapped.
 * LegacySupport is the one OUT field.
 */
/*
 * **This structure is a different size on amd64, and the whole of the
 * difference is `InterruptAffinity`.** It is a `KAFFINITY`, which is
 * pointer-sized, so it is declared `ULONG_PTR` rather than `ULONG` - identical
 * on x86, eight bytes on amd64. Design record 11's M4 measured the amd64
 * structure at `0x48` with `StartPA` still a 4-byte `ULONG` at `0x40`, and
 * that is exactly what this declaration produces on both architectures; a
 * `ULONG` here would give `0x40` and put every field from `ShareVector` on at
 * the wrong offset.
 *
 * **The offsets in the right-hand column are x86, and the amd64 ones do not
 * move by one constant.** Each pointer-sized member takes its own alignment, so
 * the drift accumulates: identical through `InterruptLevel`, then `ShareVector`
 * / `InterruptMode` / `Reserved` are +4 (`0x18` / `0x1C` / `0x20`),
 * `ResourceBase` is +8 (`0x28`), `IoSpaceLength` +12 (`0x30`), `StartVA` +16
 * (`0x38`), and `StartPA` and everything after it +20 - `StartPA` at `0x40`,
 * which is the offset M4 measured. *(This said "8 higher from `ShareVector`
 * onward", which is true of no field: the 2026-09-16 audit's B10.)*
 *
 * **Only two of those are measured.** M4 read `sizeof` = `0x48` and `StartPA`
 * = `0x40` out of the shipping amd64 `usbport.sys`; the rest are what this
 * declaration lays out, and they are consistent with both readings rather than
 * separately confirmed. The compile-time asserts at the foot of this file pin
 * the size on each architecture, which is what makes a silent divergence in the
 * middle of the structure impossible to reach without moving the end of it.
 */
typedef struct _USBPORT_RESOURCES {
    ULONG ResourcesTypes;       /* 0x00 PORT|INTERRUPT|MEMORY bitmask       */
    ULONG HcFlavor;             /* 0x04 USB_CONTROLLER_FLAVOR enum          */
    ULONG InterruptVector;      /* 0x08                                     */
    UCHAR InterruptLevel;       /* 0x0C KIRQL                               */
    UCHAR Padded1[3];
    ULONG_PTR InterruptAffinity;/* 0x10 KAFFINITY - pointer-sized, see above */
    UCHAR ShareVector;          /* 0x14 BOOLEAN                             */
    UCHAR Padded2[3];
    ULONG InterruptMode;        /* 0x18 KINTERRUPT_MODE enum                */
    ULONG Reserved;             /* 0x1C                                     */
    PVOID ResourceBase;         /* 0x20 mapped VA of BAR0                   */
    ULONG IoSpaceLength;        /* 0x24                                     */
    ULONG_PTR StartVA;          /* 0x28 common-buffer VA                    */
    ULONG StartPA;              /* 0x2C common-buffer PA (high DWORD is 0)  */
    UCHAR LegacySupport;        /* 0x30 OUT                                 */
    UCHAR IsChirpHandled;       /* 0x31 BOOLEAN                             */
    UCHAR Reserved2;            /* 0x32                                     */
    UCHAR Reserved3;            /* 0x33                                     */
} USBPORT_RESOURCES, *PUSBPORT_RESOURCES;

/*
 * **This structure changes size on amd64 and every endpoint callback is handed
 * one**, so a wrong layout here is misread by `OpenEndpoint`,
 * `ReopenEndpoint`, `QueryEndpointRequirements` and `RebalanceEndpoint` alike,
 * silently. `BufferVA` is the reason it moves: it is the one pointer-sized
 * member, so it forces 8-byte alignment, four bytes of padding appear at
 * `0x1C`, and every field from `BufferVA` on sits 8 higher than the x86 column
 * below.
 *
 * Measured off the amd64 `usbehci.sys` on 2026-09-09, method **static**, the
 * way M4 read `USBPORT_RESOURCES` (design record 11 section 5, M7). The
 * numbers this declaration must reproduce, each read from an instruction
 * rather than inferred:
 *
 *   sizeof            0x48  OpenEndpoint copies the whole structure into its
 *                           endpoint extension as nine 8-byte moves, from
 *                           [rdx+0x00] to [rdx+0x40] inclusive, and the
 *                           extension's own fields resume at +0x58 after the
 *                           copy ends at +0x50
 *   DeviceAddress     0x00  read as a byte and masked 0x7F
 *   EndpointAddress   0x02  read as a word
 *   DeviceSpeed       0x08  compared against 2 (UsbHighSpeed), 4 bytes
 *   TransferType      0x14  switched on 0..3, 4 bytes
 *   BufferVA          0x20  read as a QWORD - this is the field that moves
 *   BufferPA          0x28  read as a DWORD, and still 32-bit (M5: the DMA
 *                           adapter is created 32-bit, so a PA fits a ULONG)
 *   BufferLength      0x2C  read as a DWORD
 *   HubAddr           0x38  read as a word and masked 0x7F
 *   PortNumber        0x3A  read as a word
 *
 * The three fields this header spells `ULONG` in place of an NT enum -
 * `DeviceSpeed`, `TransferType` and `Direction` - are the ones that had to be
 * checked for width rather than position, because `InterruptAffinity` in
 * `USBPORT_RESOURCES` is the cautionary case of a field pinned here to `ULONG`
 * that the real structure widens. Two of the three are measured above as
 * 4 bytes; `Direction` is not read by any path disassembled, and sits between
 * two measured anchors with no room to move.
 */
typedef struct _USBPORT_ENDPOINT_PROPERTIES {
    USHORT DeviceAddress;             /* 0x00 */
    USHORT EndpointAddress;           /* 0x02 */
    USHORT TotalMaxPacketSize;        /* 0x04 */
    UCHAR Period;                     /* 0x06 periodic: pre-bucketed
                                       *      1/2/4/8/16/32; control and bulk
                                       *      get 0 (ABI doc's producer table,
                                       *      noted later)                    */
    UCHAR Reserved1;                  /* 0x07 */
    ULONG DeviceSpeed;                /* 0x08 USB_DEVICE_SPEED enum         */
    ULONG UsbBandwidth;               /* 0x0C */
    ULONG ScheduleOffset;             /* 0x10 */
    ULONG TransferType;               /* 0x14 */
    ULONG Direction;                  /* 0x18 */
    ULONG_PTR BufferVA;               /* 0x1C per-endpoint common buffer     */
    ULONG BufferPA;                   /* 0x20 */
    ULONG BufferLength;               /* 0x24 */
    ULONG Reserved3;                  /* 0x28 */
    ULONG MaxTransferSize;            /* 0x2C */
    USHORT HubAddr;                   /* 0x30 TT hub address, or 0xFFFF      */
    USHORT PortNumber;                /* 0x32 Port on the TT hub (any depth)
                                       * when a TT was selected. NOT a root-port
                                       * index in general - see xhci_slot.c.
                                       *
                                       * Read HubAddr first: HubAddr != 0xFFFF
                                       * is the only condition under which this
                                       * is a TT port at all, and it is exactly
                                       * that condition - no callback-visible
                                       * path pairs a non-0xFFFF HubAddr with a
                                       * non-TT port.
                                       *
                                       * On 0xFFFF this is the port of the last
                                       * non-High-Speed ancestor the TT walk
                                       * visited, or the seeded immediate-parent
                                       * port if it visited none. Several causes
                                       * produce that (lookup skipped, walk off
                                       * the top, multi-TT list miss) and they
                                       * are NOT distinguishable from the value:
                                       * with a single non-HS hop they all give
                                       * the immediate-parent port. Do not try
                                       * to tell them apart here.
                                       *
                                       * The number may well still be a real
                                       * downstream port on some hub - what a
                                       * 0xFFFF says is that no TT record was
                                       * selected, so nothing identifies WHICH
                                       * hub. That is why the pair is unusable,
                                       * not because the port is meaningless.   */
    UCHAR InterruptScheduleMask;      /* 0x34 */
    UCHAR SplitCompletionMask;        /* 0x35 */
    UCHAR TransactionPerMicroframe;   /* 0x36 */
    UCHAR Reserved4;                  /* 0x37 */
    ULONG MaxPacketSize;              /* 0x38 */
    ULONG Reserved6;                  /* 0x3C */
} USBPORT_ENDPOINT_PROPERTIES, *PUSBPORT_ENDPOINT_PROPERTIES;

/*
 * The `HubAddr` value that means "no transaction translator was selected", and
 * therefore the one test that says whether the pair above may be read as a TT
 * identity at all (batch 7a-0). Named rather than written as a literal because
 * it is a *condition* two files branch on - src/xhci_probe.c classifies it and
 * src/xhci_slot.c normalises it away - and a magic number in two places is how
 * they stop agreeing.
 */
#define USBPORT_NO_TT_HUB       0xFFFFU

typedef struct _USBPORT_ENDPOINT_REQUIREMENTS {
    ULONG HeaderBufferSize;
    ULONG MaxTransferSize;
} USBPORT_ENDPOINT_REQUIREMENTS, *PUSBPORT_ENDPOINT_REQUIREMENTS;

/* The raw 8 SETUP bytes carried in USBPORT_TRANSFER_PARAMETERS. SET_ADDRESS is
 * intercepted from here in Phase 6 (docs/contributing/implementation-invariants.md). */
typedef struct _XHCI_SETUP_PACKET {
    UCHAR bmRequestType;
    UCHAR bRequest;
    USHORT wValue;
    USHORT wIndex;
    USHORT wLength;
} XHCI_SETUP_PACKET;

typedef struct _USBPORT_TRANSFER_PARAMETERS {
    ULONG TransferFlags;           /* 0x00 bit 0 = USBD_TRANSFER_DIRECTION_IN */
    ULONG TransferBufferLength;    /* 0x04 */
    ULONG TransferCounter;         /* 0x08 */
    ULONG IsTransferSplited;       /* 0x0C BOOL */
    ULONG Reserved2;               /* 0x10 */
    XHCI_SETUP_PACKET SetupPacket; /* 0x14 */
} USBPORT_TRANSFER_PARAMETERS, *PUSBPORT_TRANSFER_PARAMETERS;

/*
 * usbport builds these through the NT DMA adapter and stores the HAL's
 * `PHYSICAL_ADDRESS` **unmasked** - the high DWORD is zero because the adapter
 * is created 32-bit (`Dma32BitAddresses = 1`, `DmaWidth = Width32Bits`), not
 * because any element writer forces it. So the address is kept as a Lo/Hi pair
 * rather than a PHYSICAL_ADDRESS, and Hi is a value to *check*, never to
 * compute with and never to assume.
 *
 * *(This said usbport built them "with the high DWORD forced to zero" until
 * a later review. That was the ReactOS reading, and
 * `docs/usb-xhci-info/usbport-miniport-abi.md` corrected it from the shipping
 * binary - `MapTransfer` returns `edx:eax` and the store is verbatim - in the
 * same paragraph that gives the miniport the check-it-never-assume rule this
 * comment already carried. The rule was right and its stated reason was not.)*
 */
typedef struct _USBPORT_SCATTER_GATHER_ELEMENT {
    ULONG SgPhysicalAddressLo;  /* 0x00 */
    ULONG SgPhysicalAddressHi;  /* 0x04 always 0 - verify, never use          */
#ifdef _WIN64
    /*
     * M8: the eight bytes between the address and the length. On x86 this is
     * one address-like private DWORD followed by the length; here the length
     * is measured at 0x10, so eight bytes separate them. Whether that is one
     * 8-byte private field or a 4-byte one plus padding was not measured -
     * the miniport must not read or write either half, so it does not matter.
     */
    ULONG Reserved1Lo;          /* 0x08 */
    ULONG Reserved1Hi;          /* 0x0C */
    ULONG SgTransferLength;     /* 0x10 */
    ULONG SgOffset;             /* 0x14 offset within the whole transfer buf  */
#else
    ULONG Reserved1;            /* 0x08 */
    ULONG SgTransferLength;     /* 0x0C */
    ULONG SgOffset;             /* 0x10 offset within the whole transfer buf  */
    ULONG Reserved2;            /* 0x14 */
#endif
} USBPORT_SCATTER_GATHER_ELEMENT, *PUSBPORT_SCATTER_GATHER_ELEMENT;

typedef struct _USBPORT_SCATTER_GATHER_LIST {
    ULONG Flags;                /* 0x00 */
    /* On amd64 a 4-byte hole follows, which the compiler supplies itself. */
    ULONG_PTR CurrentVa;        /* 0x04 / 0x08 */
    PVOID MappedSystemVa;       /* 0x08 / 0x10 */
    ULONG SgElementCount;       /* 0x0C / 0x18 */
#ifdef _WIN64
    /*
     * M8, and the whole reason this padding is written out. The array starts
     * at 0x20 on amd64 - the real element type is 8-aligned, since its first
     * member is a `PHYSICAL_ADDRESS` - while every member of the declaration
     * above is a `ULONG`, so the compiler's own alignment would put it at
     * 0x1C and shift every field the transfer path reads by four bytes.
     */
    ULONG Reserved0;            /* 0x1C */
#endif
    USBPORT_SCATTER_GATHER_ELEMENT SgElement[2];  /* 0x10 / 0x20, variable length */
} USBPORT_SCATTER_GATHER_LIST, *PUSBPORT_SCATTER_GATHER_LIST;

/*
 * RH_GetRootHubData output. PowerOnToPowerGood is in 2 ms units and is copied
 * straight into bPowerOnToPowerGood, so xHCI's 20 ms PORTSC.PP rule is 10.
 */
typedef struct _USBPORT_ROOT_HUB_DATA {
    ULONG NumberOfPorts;        /* 0x00 managed USB2 ports only */
    USHORT HubCharacteristics;  /* 0x04 */
    USHORT Padded1;             /* 0x06 */
    ULONG PowerOnToPowerGood;   /* 0x08 2 ms units */
    ULONG HubControlCurrent;    /* 0x0C */
} USBPORT_ROOT_HUB_DATA, *PUSBPORT_ROOT_HUB_DATA;

/* Standard USB hub-class port status/change, as RH_GetPortStatus reports it.
 * Phase 5 defines the bit meanings; Phase 3 only needs the width. */
typedef struct _USBPORT_PORT_STATUS_AND_CHANGE {
    USHORT PortStatus;
    USHORT PortChange;
} USBPORT_PORT_STATUS_AND_CHANGE, *PUSBPORT_PORT_STATUS_AND_CHANGE;

typedef struct _USBPORT_HUB_STATUS_AND_CHANGE {
    USHORT HubStatus;
    USHORT HubChange;
} USBPORT_HUB_STATUS_AND_CHANGE, *PUSBPORT_HUB_STATUS_AND_CHANGE;

/* usbdi.h's USBD_STATUS, declared locally so this header needs no USB DDK
 * header. Same width and signedness. */
typedef LONG XHCI_USBD_STATUS;

/* ------------------------------------------------------------------ */
/* The isochronous parameter block (task 9-0.1)                        */
/* ------------------------------------------------------------------ */

/*
 * `SubmitIsoTransfer`'s fifth argument, transcribed from
 * docs/usb-xhci-info/usbport-miniport-abi.md section 4, "Isochronous transfers (task 9-0.1)" -
 * which was read out of both shipping builds instruction by instruction rather
 * than from ReactOS, whose isoch path is a 33-line stub that defines none of it.
 *
 * usbport carves this out of the same allocation as the transfer and sizes it
 * `0x48 + 0x38 * NumberOfPackets`, which is **one entry larger** than the
 * `0x10 + 0x38 * n` this declaration covers. That slack is measured and
 * unexplained - neither build writes into it - so nothing here relies on it and
 * nothing may start to.
 *
 * **Exactly two fields per entry are the miniport's to write**:
 * `LengthTransferred` and `Status`. That is established by the *reader* - the
 * completion path copies back those two and nothing else - not by the builder
 * being silent about them. Every other field is input, and writing one would be
 * this driver editing usbport's own description of the request.
 */
typedef struct _USBPORT_ISO_PACKET {
    ULONG Length;                   /* 0x00 in  - bytes this packet asks for  */
    ULONG LengthTransferred;        /* 0x04 OUT - the miniport writes it      */
    ULONG FrameNumber;              /* 0x08 in  - usbport's 32-bit frame      */
    ULONG MicroFrame;               /* 0x0C in  - 0-7 on HS, 0 otherwise      */
    XHCI_USBD_STATUS Status;        /* 0x10 OUT - the miniport writes it      */
    ULONG FragmentCount;            /* 0x14 in  - 1 or 2, never anything else */
    ULONG Fragment0Length;          /* 0x18 in                                */
    ULONG Reserved0;                /* 0x1C     - not written by usbport      */
    ULONG Fragment0AddressLo;       /* 0x20 in                                */
    ULONG Fragment0AddressHi;       /* 0x24 in  - check it, never assume 0    */
    ULONG Fragment1Length;          /* 0x28 in  - 0 when FragmentCount == 1   */
    ULONG Reserved1;                /* 0x2C     - not written by usbport      */
    ULONG Fragment1AddressLo;       /* 0x30 in                                */
    ULONG Fragment1AddressHi;       /* 0x34 in                                */
} USBPORT_ISO_PACKET, *PUSBPORT_ISO_PACKET;

typedef struct _USBPORT_ISO_TRANSFER {
    ULONG Signature;                /* 0x00 'Isoc', written on every build    */
    ULONG NumberOfPackets;          /* 0x04 URB+0x4C verbatim                 */
    ULONG SgElementCount;           /* 0x08 from the sg list's SgElementCount,
                                     *      which is at its +0x0C - +0x08 there
                                     *      is MappedSystemVa (corrected) */
    ULONG Reserved;                 /* 0x0C not written; the block is zeroed  */
    USBPORT_ISO_PACKET Packet[1];   /* 0x10, NumberOfPackets of them          */
} USBPORT_ISO_TRANSFER, *PUSBPORT_ISO_TRANSFER;

/*
 * `'Isoc'` little-endian, which is what the builder stores. Checked rather than
 * assumed: this is the one field in the block that says the pointer usbport
 * handed over is the structure this driver thinks it is, and every offset below
 * it is read on the strength of that.
 */
#define USBPORT_ISO_SIGNATURE   0x636F7349UL

/*
 * The `USBPORT_TRANSFER_PARAMETERS.TransferFlags` bit that says a transfer moves
 * bytes towards the host. Bit 0 is `USBD_TRANSFER_DIRECTION_IN` in the DDK's
 * usbdi.h, and it is the same bit the control and normal builders check their
 * endpoints against - written as a literal `& 1UL` at those two call sites since
 * batch 6-A, and named here because the isoch path needs it a third time.
 *
 * **It is not `USBPORT_TRANSFER_DIRECTION_OUT` above, despite both being 1.**
 * That one is ReactOS's `usbmport.h:649` constant for the *endpoint properties*
 * `Direction` field, a different field with its own numbering. Two names, one
 * value, opposite meanings - which is exactly why this one carries `FLAG` in its
 * name rather than reading as the other's twin.
 */
#define USBPORT_TRANSFER_FLAG_DIRECTION_IN 1UL

/* ------------------------------------------------------------------ */
/* Callback signatures                                                 */
/* ------------------------------------------------------------------ */

/*
 * Every one of these is NTAPI (stdcall) except PUSBPORT_DBG_PRINT, which is
 * cdecl varargs. One calling-convention error corrupts the stack on every
 * call, so the exception is spelled out where it lives rather than inherited.
 *
 * The first PVOID is always the miniport device extension - it is identity,
 * not a handle: usbport recovers its own FDO extension by subtracting from it.
 * Endpoint PVOIDs are the miniport endpoint extension, transfer PVOIDs the
 * miniport transfer extension.
 */

typedef MPSTATUS (NTAPI *PHCI_OPEN_ENDPOINT)(PVOID, PUSBPORT_ENDPOINT_PROPERTIES, PVOID);
typedef MPSTATUS (NTAPI *PHCI_REOPEN_ENDPOINT)(PVOID, PUSBPORT_ENDPOINT_PROPERTIES, PVOID);
typedef VOID (NTAPI *PHCI_QUERY_ENDPOINT_REQUIREMENTS)(PVOID, PUSBPORT_ENDPOINT_PROPERTIES, PUSBPORT_ENDPOINT_REQUIREMENTS);
/*
 * TWO parameters on the NT 6.x arm and THREE on the NT 5.x arm, and one
 * stdcall callee cannot serve both, because on x86 its `ret` cleans a fixed
 * count and the two tiers push different counts. The slot is typed with the
 * NT 6.x shape and filled with it; DriverEntry stores a cast of the
 * three-parameter callee (xhciCloseEndpointNt5, src\xhci_dispatch.c) when
 * `IoIsWdmVersionAvailable(6, 0)` is FALSE. The readings, both x86, both
 * static, each confirmed by the bugcheck the other shape produced:
 *
 * NT 6.x - Vista x86 MPf_CloseEndpoint returns at once below interface
 * version 300 (`cmp dword ptr [eax+10h],12Ch`) and above it pushes two:
 *
 *     0001f390 push edi                  ; endpointExtension
 *     0001f391 push dword ptr [esi+30Ch] ; miniportExtension
 *     0001f397 call dword ptr [eax+50h]  ; 0x50 - 0x1C = our 0x34
 *
 * Windows 7 x86 agrees. ReactOS's third parameter, IsDoDisablePeriodic
 * (endpoint.c:580), which this typedef carried until 2026-09-12, made the
 * callee `ret 0Ch` against 8 pushed, over-popping usbport's stack by four
 * and sending its epilogue's `ret` into data: bugcheck 0xD1 on the first
 * device attach, Vista x86, 2026-09-11.
 *
 * NT 5.x - Windows XP SP3 x86 MP_CloseEndpoint (5.1.2600.5512, RVA 0x1568A,
 * the only call through the slot in that image) pushes three, the third
 * being exactly ReactOS's IsDoDisablePeriodic:
 *
 *     000156c4 cmp dword ptr [edi+114h],0 ; open periodic endpoints
 *     000156d1 sete cl
 *     000156d4 push ecx                   ; IsDoDisablePeriodic
 *     000156d5 lea ecx,[esi+178h]
 *     000156db push ecx                   ; endpointExtension
 *     000156dc push dword ptr [edi+140h]  ; miniportExtension
 *     000156e2 call dword ptr [eax+48h]   ; 0x48 - 0x14 = our 0x34
 *     ...
 *     00015700 pop edi / pop esi / pop ebx / pop ebp / ret 8
 *
 * No `mov esp,ebp` before those pops, so a two-parameter callee's `ret 8`
 * leaves the third argument under every one of them: `pop edi` takes it,
 * `pop ebp` takes the caller's saved ebx, and `ret` takes the saved ebp - a
 * stack address, which the PAE kernel refuses to execute: bugcheck 0xFC on
 * the first device attach, XP SP3 x86, 2026-09-14, matched register for
 * register against the dump (issue 7 section 7.9). The caller is
 * USBPORT_InitializeDevice's EP0 close after SET_ADDRESS, by way of
 * USBPORT_PokeEndpoint. The 2026-09-12 change was measured on Vista alone;
 * XP x86 had last run on 2026-09-07, on the three-parameter callee.
 *
 * The rest of the NT 5.x arm never reaches the slot: Windows 2000 SP4, NUSB
 * and SweetLow's XP-derived 5.1.2600.2180 (the Windows ME target's stack)
 * have no `call [reg+48h]` that is a CloseEndpoint at all (NUSB's three are
 * StartController behind its 0x10 header, SweetLow's 0x44 and 0x4C are
 * QueryEndpointRequirements and StartController behind a 0x14 one). XP x64
 * takes this arm too, where the caller cleans and a third register argument
 * is inert whether or not it is passed; its MP_CloseEndpoint is inlined and
 * has not been read.
 */
typedef VOID (NTAPI *PHCI_CLOSE_ENDPOINT)(PVOID, PVOID);
typedef VOID (NTAPI *PHCI_CLOSE_ENDPOINT_NT5)(PVOID, PVOID, BOOLEAN);
typedef MPSTATUS (NTAPI *PHCI_START_CONTROLLER)(PVOID, PUSBPORT_RESOURCES);
typedef VOID (NTAPI *PHCI_STOP_CONTROLLER)(PVOID, BOOLEAN);
typedef VOID (NTAPI *PHCI_SUSPEND_CONTROLLER)(PVOID);
typedef MPSTATUS (NTAPI *PHCI_RESUME_CONTROLLER)(PVOID);
typedef BOOLEAN (NTAPI *PHCI_INTERRUPT_SERVICE)(PVOID);
/*
 * ULONG, not VOID, since task 22.5. NT 5.x declares the 0x4C slot VOID and
 * never reads eax; NT 6.x reads the 0x178 / 0x298 slot's return (bits 0 and 1,
 * USBPORT_DPC_EX_*). One function serves both slots, which is exactly what
 * Vista's and Windows 7's own usbehci does - its 0x4C function is a thunk
 * onto its InterruptDpcEx - so the widened return type is the vendor's shape
 * rather than an accommodation.
 */
typedef ULONG (NTAPI *PHCI_INTERRUPT_DPC)(PVOID, BOOLEAN);
typedef MPSTATUS (NTAPI *PHCI_SUBMIT_TRANSFER)(PVOID, PVOID, PUSBPORT_TRANSFER_PARAMETERS, PVOID, PUSBPORT_SCATTER_GATHER_LIST);
typedef MPSTATUS (NTAPI *PHCI_SUBMIT_ISO_TRANSFER)(PVOID, PVOID, PUSBPORT_TRANSFER_PARAMETERS, PVOID, PVOID);
typedef VOID (NTAPI *PHCI_ABORT_TRANSFER)(PVOID, PVOID, PVOID, PULONG);
typedef ULONG (NTAPI *PHCI_GET_ENDPOINT_STATE)(PVOID, PVOID);
typedef VOID (NTAPI *PHCI_SET_ENDPOINT_STATE)(PVOID, PVOID, ULONG);
typedef VOID (NTAPI *PHCI_POLL_ENDPOINT)(PVOID, PVOID);
typedef VOID (NTAPI *PHCI_CHECK_CONTROLLER)(PVOID);
typedef ULONG (NTAPI *PHCI_GET_32BIT_FRAME_NUMBER)(PVOID);
typedef VOID (NTAPI *PHCI_INTERRUPT_NEXT_SOF)(PVOID);
typedef VOID (NTAPI *PHCI_ENABLE_INTERRUPTS)(PVOID);
typedef VOID (NTAPI *PHCI_DISABLE_INTERRUPTS)(PVOID);
typedef VOID (NTAPI *PHCI_POLL_CONTROLLER)(PVOID);
typedef VOID (NTAPI *PHCI_SET_ENDPOINT_DATA_TOGGLE)(PVOID, PVOID, ULONG);
typedef ULONG (NTAPI *PHCI_GET_ENDPOINT_STATUS)(PVOID, PVOID);
typedef VOID (NTAPI *PHCI_SET_ENDPOINT_STATUS)(PVOID, PVOID, ULONG);
typedef VOID (NTAPI *PHCI_RESET_CONTROLLER)(PVOID);

typedef VOID (NTAPI *PHCI_RH_GET_ROOT_HUB_DATA)(PVOID, PVOID);
typedef MPSTATUS (NTAPI *PHCI_RH_GET_STATUS)(PVOID, PUSHORT);
typedef MPSTATUS (NTAPI *PHCI_RH_GET_PORT_STATUS)(PVOID, USHORT, PUSBPORT_PORT_STATUS_AND_CHANGE);
typedef MPSTATUS (NTAPI *PHCI_RH_GET_HUB_STATUS)(PVOID, PUSBPORT_HUB_STATUS_AND_CHANGE);
typedef MPSTATUS (NTAPI *PHCI_RH_PORT_OPERATION)(PVOID, USHORT);
typedef VOID (NTAPI *PHCI_RH_DISABLE_IRQ)(PVOID);
typedef VOID (NTAPI *PHCI_RH_ENABLE_IRQ)(PVOID);

typedef MPSTATUS (NTAPI *PHCI_SEND_ONE_PACKET)(PVOID, PVOID, PVOID, PULONG, PVOID, PVOID, ULONG, XHCI_USBD_STATUS *);
typedef MPSTATUS (NTAPI *PHCI_PASS_THRU)(PVOID, PVOID, ULONG, PVOID);

typedef VOID (NTAPI *PHCI_REBALANCE_ENDPOINT)(PVOID, PUSBPORT_ENDPOINT_PROPERTIES, PVOID);
typedef VOID (NTAPI *PHCI_FLUSH_INTERRUPTS)(PVOID);
typedef VOID (NTAPI *PHCI_TAKE_PORT_CONTROL)(PVOID);

/* Services usbport writes back into the packet. */
typedef ULONG (*PUSBPORT_DBG_PRINT)(PVOID, ULONG, PCHAR, ...);  /* cdecl! */
typedef ULONG (NTAPI *PUSBPORT_TEST_DEBUG_BREAK)(PVOID);
typedef ULONG (NTAPI *PUSBPORT_ASSERT_FAILURE)(PVOID, PVOID, PVOID, ULONG, PCHAR);
/*
 * **Arguments 4 and 6 are `SIZE_T`, not `ULONG`, and on amd64 that is a
 * difference rather than a spelling.** `usbmport.h` lines 416-423 and the
 * abi record's argument-by-argument table both say `SIZE_T`: argument 4 is the
 * value name's byte length and argument 6 the number of bytes copied, and the
 * callee uses them as `arg4 + arg6 + 0x18` for its allocation and as the
 * unconditional copy length.
 *
 * On x86 the two widths are the same and this declaration was harmless. On
 * amd64 argument 6 is the second *stack* argument: a caller typed `ULONG`
 * stores four bytes into an eight-byte home slot and a callee compiled for
 * `SIZE_T` reads all eight, so the copy length is four bytes of the caller's
 * intent and four bytes of whatever the loader left there. `ULONG_PTR` is the
 * width on both - identical to `ULONG` on x86, so the 32-bit binary does not
 * move - and the call sites cast rather than passing `sizeof`, which is
 * already `SIZE_T`-typed but says so nowhere a reader can see.
 *
 * **Unmeasured on the tier it matters on.** No run has read the log channel on
 * the XP x64 guest, and there is no static read of that build's thunk behind
 * this: it is taken from the declaration and the x86 reading, so
 * `legal-provenance.md` gains no row for it. What the narrowing could have
 * produced there is either both switches reading 0 - so `XhciLogVerbosity`
 * could never open the snapshot channel on x64 - or an oversized copy into a
 * four-byte local. The same widening is applied to the two other services
 * whose declarations carry a `SIZE_T`, where it is inert: argument 4 of
 * `UsbPortRequestAsyncCallback` is register-passed on amd64, and
 * `UsbPortNotifyDoubleBuffer` is not called by this driver at all.
 */
typedef MPSTATUS (NTAPI *PUSBPORT_GET_MINIPORT_REGISTRY_KEY_VALUE)(PVOID, ULONG, PVOID, ULONG_PTR, PVOID, ULONG_PTR);
typedef ULONG (NTAPI *PUSBPORT_INVALIDATE_ROOT_HUB)(PVOID);
typedef ULONG (NTAPI *PUSBPORT_INVALIDATE_ENDPOINT)(PVOID, PVOID);
typedef VOID (NTAPI *PUSBPORT_COMPLETE_TRANSFER)(PVOID, PVOID, PVOID, XHCI_USBD_STATUS, ULONG);
/*
 * The fourth argument is a **pointer** - the same `USBPORT_ISO_TRANSFER` block
 * `SubmitIsoTransfer` was handed - and it was declared `ULONG` here until task
 * 9-A.1, from the ReactOS-derived shape. Task 9-0.1 read the callee out of both
 * shipping builds (`ret 10h`, four arguments, the block dereferenced per packet
 * at `+0x10 + 0x38*i`), so this is measured rather than inferred. The width is
 * the same on x86 and nothing miscompiled; what a `ULONG` cost was every reader
 * of this line believing the miniport hands back a count.
 *
 * The second argument (`epExt`) is passed because the declaration says so and is
 * **not read at all** in either build - do not infer that usbport recovers the
 * endpoint from it.
 */
typedef ULONG (NTAPI *PUSBPORT_COMPLETE_ISO_TRANSFER)(PVOID, PVOID, PVOID, PVOID);
typedef ULONG (NTAPI *PUSBPORT_LOG_ENTRY)(PVOID, ULONG, ULONG, ULONG, ULONG, ULONG);
typedef PVOID (NTAPI *PUSBPORT_GET_MAPPED_VIRTUAL_ADDRESS)(ULONG, PVOID, PVOID);
typedef VOID (NTAPI XHCI_ASYNC_TIMER_CALLBACK)(PVOID, PVOID);
typedef ULONG (NTAPI *PUSBPORT_REQUEST_ASYNC_CALLBACK)(PVOID, ULONG, PVOID, ULONG_PTR, XHCI_ASYNC_TIMER_CALLBACK *);
typedef MPSTATUS (NTAPI *PUSBPORT_READ_WRITE_CONFIG_SPACE)(PVOID, BOOLEAN, PVOID, ULONG, ULONG);
typedef LONG (NTAPI *PUSBPORT_WAIT)(PVOID, ULONG);
typedef ULONG (NTAPI *PUSBPORT_INVALIDATE_CONTROLLER)(PVOID, ULONG);
typedef VOID (NTAPI *PUSBPORT_BUG_CHECK)(PVOID);
typedef ULONG (NTAPI *PUSBPORT_NOTIFY_DOUBLE_BUFFER)(PVOID, PVOID, PVOID, ULONG_PTR);

/* ------------------------------------------------------------------ */
/* USBPORT_REGISTRATION_PACKET                                         */
/* ------------------------------------------------------------------ */

typedef struct _USBPORT_REGISTRATION_PACKET {
    /* Data fields the miniport declares */
    ULONG MiniPortVersion;                /* 0x00 */
    ULONG MiniPortFlags;                  /* 0x04 */
    ULONG MiniPortBusBandwidth;           /* 0x08 */
    ULONG Reserved1;                      /* 0x0C canary */
    ULONG MiniPortExtensionSize;          /* 0x10 */
    ULONG MiniPortEndpointSize;           /* 0x14 */
    ULONG MiniPortTransferSize;           /* 0x18 */
    ULONG Reserved2;                      /* 0x1C canary */
    ULONG Reserved3;                      /* 0x20 canary */
    ULONG MiniPortResourcesSize;          /* 0x24 */

    /* Miniport callbacks */
    PHCI_OPEN_ENDPOINT OpenEndpoint;                            /* 0x28 */
    PHCI_REOPEN_ENDPOINT ReopenEndpoint;                        /* 0x2C */
    PHCI_QUERY_ENDPOINT_REQUIREMENTS QueryEndpointRequirements; /* 0x30 */
    PHCI_CLOSE_ENDPOINT CloseEndpoint;                          /* 0x34 */
    PHCI_START_CONTROLLER StartController;                      /* 0x38 */
    PHCI_STOP_CONTROLLER StopController;                        /* 0x3C */
    PHCI_SUSPEND_CONTROLLER SuspendController;                  /* 0x40 */
    PHCI_RESUME_CONTROLLER ResumeController;                    /* 0x44 */
    PHCI_INTERRUPT_SERVICE InterruptService;                    /* 0x48 */
    PHCI_INTERRUPT_DPC InterruptDpc;                            /* 0x4C */
    PHCI_SUBMIT_TRANSFER SubmitTransfer;                        /* 0x50 */
    PHCI_SUBMIT_ISO_TRANSFER SubmitIsoTransfer;                 /* 0x54 */
    PHCI_ABORT_TRANSFER AbortTransfer;                          /* 0x58 */
    PHCI_GET_ENDPOINT_STATE GetEndpointState;                   /* 0x5C */
    PHCI_SET_ENDPOINT_STATE SetEndpointState;                   /* 0x60 */
    PHCI_POLL_ENDPOINT PollEndpoint;                            /* 0x64 */
    PHCI_CHECK_CONTROLLER CheckController;                      /* 0x68 */
    PHCI_GET_32BIT_FRAME_NUMBER Get32BitFrameNumber;            /* 0x6C */
    PHCI_INTERRUPT_NEXT_SOF InterruptNextSOF;                   /* 0x70 */
    PHCI_ENABLE_INTERRUPTS EnableInterrupts;                    /* 0x74 */
    PHCI_DISABLE_INTERRUPTS DisableInterrupts;                  /* 0x78 */
    PHCI_POLL_CONTROLLER PollController;                        /* 0x7C */
    PHCI_SET_ENDPOINT_DATA_TOGGLE SetEndpointDataToggle;        /* 0x80 */
    PHCI_GET_ENDPOINT_STATUS GetEndpointStatus;                 /* 0x84 */
    PHCI_SET_ENDPOINT_STATUS SetEndpointStatus;                 /* 0x88 */
    PHCI_RESET_CONTROLLER ResetController;                      /* 0x8C */

    /* Root-hub callbacks */
    PHCI_RH_GET_ROOT_HUB_DATA RH_GetRootHubData;                /* 0x90 */
    PHCI_RH_GET_STATUS RH_GetStatus;                            /* 0x94 */
    PHCI_RH_GET_PORT_STATUS RH_GetPortStatus;                   /* 0x98 */
    PHCI_RH_GET_HUB_STATUS RH_GetHubStatus;                     /* 0x9C */
    PHCI_RH_PORT_OPERATION RH_SetFeaturePortReset;              /* 0xA0 */
    PHCI_RH_PORT_OPERATION RH_SetFeaturePortPower;              /* 0xA4 */
    PHCI_RH_PORT_OPERATION RH_SetFeaturePortEnable;             /* 0xA8 */
    PHCI_RH_PORT_OPERATION RH_SetFeaturePortSuspend;            /* 0xAC */
    PHCI_RH_PORT_OPERATION RH_ClearFeaturePortEnable;           /* 0xB0 */
    PHCI_RH_PORT_OPERATION RH_ClearFeaturePortPower;            /* 0xB4 */
    PHCI_RH_PORT_OPERATION RH_ClearFeaturePortSuspend;          /* 0xB8 */
    PHCI_RH_PORT_OPERATION RH_ClearFeaturePortEnableChange;     /* 0xBC */
    PHCI_RH_PORT_OPERATION RH_ClearFeaturePortConnectChange;    /* 0xC0 */
    PHCI_RH_PORT_OPERATION RH_ClearFeaturePortResetChange;      /* 0xC4 */
    PHCI_RH_PORT_OPERATION RH_ClearFeaturePortSuspendChange;    /* 0xC8 */
    PHCI_RH_PORT_OPERATION RH_ClearFeaturePortOvercurrentChange;/* 0xCC */
    PHCI_RH_DISABLE_IRQ RH_DisableIrq;                          /* 0xD0 */
    PHCI_RH_ENABLE_IRQ RH_EnableIrq;                            /* 0xD4 */

    /* Debug single-packet path */
    PHCI_SEND_ONE_PACKET StartSendOnePacket;                    /* 0xD8 */
    PHCI_SEND_ONE_PACKET EndSendOnePacket;                      /* 0xDC */
    PHCI_PASS_THRU PassThru;                                    /* 0xE0 */

    /* OUT: the 16 services usbport writes here before copying the packet */
    PUSBPORT_DBG_PRINT UsbPortDbgPrint;                             /* 0xE4  */
    PUSBPORT_TEST_DEBUG_BREAK UsbPortTestDebugBreak;                /* 0xE8  */
    PUSBPORT_ASSERT_FAILURE UsbPortAssertFailure;                   /* 0xEC  */
    PUSBPORT_GET_MINIPORT_REGISTRY_KEY_VALUE
        UsbPortGetMiniportRegistryKeyValue;                         /* 0xF0  */
    PUSBPORT_INVALIDATE_ROOT_HUB UsbPortInvalidateRootHub;          /* 0xF4  */
    PUSBPORT_INVALIDATE_ENDPOINT UsbPortInvalidateEndpoint;         /* 0xF8  */
    PUSBPORT_COMPLETE_TRANSFER UsbPortCompleteTransfer;             /* 0xFC  */
    PUSBPORT_COMPLETE_ISO_TRANSFER UsbPortCompleteIsoTransfer;      /* 0x100 */
    PUSBPORT_LOG_ENTRY UsbPortLogEntry;                             /* 0x104 */
    PUSBPORT_GET_MAPPED_VIRTUAL_ADDRESS UsbPortGetMappedVirtualAddress; /* 0x108 */
    PUSBPORT_REQUEST_ASYNC_CALLBACK UsbPortRequestAsyncCallback;    /* 0x10C */
    PUSBPORT_READ_WRITE_CONFIG_SPACE UsbPortReadWriteConfigSpace;   /* 0x110 */
    PUSBPORT_WAIT UsbPortWait;                                      /* 0x114 */
    PUSBPORT_INVALIDATE_CONTROLLER UsbPortInvalidateController;     /* 0x118 */
    PUSBPORT_BUG_CHECK UsbPortBugCheck;                             /* 0x11C */
    PUSBPORT_NOTIFY_DOUBLE_BUFFER UsbPortNotifyDoubleBuffer;        /* 0x120 */

    /* Tail group - present only when the Version argument is >= 200 */
    PHCI_REBALANCE_ENDPOINT RebalanceEndpoint;   /* 0x124 */
    PHCI_FLUSH_INTERRUPTS FlushInterrupts;       /* 0x128 */
    PHCI_RH_PORT_OPERATION RH_ChirpRootPort;     /* 0x12C */
    PHCI_TAKE_PORT_CONTROL TakePortControl;      /* 0x130 */
    /*
     * Pointer-sized rather than `ULONG`, and that is a measured requirement on
     * amd64 rather than tidiness. Registration copies `0x250` bytes there
     * (design record 11, M3), and the callback before these two ends at
     * `0x240`; two 4-byte canaries would pack into `0x240`/`0x244` and end the
     * structure at `0x248`, so usbport would copy eight bytes past the end of
     * `XhciRegPacket` - a static global - into fields it believes are part of
     * the packet. M6 read the same `0x250` from the other side and records
     * that widening these two or adding explicit tail padding are equally
     * valid, since both are reserved and this driver reads neither. On x86
     * `ULONG_PTR` is four bytes, so the x86 layout is exactly as it was.
     */
    ULONG_PTR Reserved4;                         /* 0x134 canary */
    ULONG_PTR Reserved5;                         /* 0x138 canary */

    /*
     * The Version 300 tier - copied only by an NT 6.x usbport, and only when
     * this driver presented USBPORT_NT6_MINIPORT_INTERFACE_VERSION. Read slot
     * by slot on 2026-09-11 (roadmap task 22.5; usbport-miniport-abi.md, "The
     * Version 300 tier, slot by slot"; legal-provenance.md section 4): twelve
     * ULONGs, then 29 pointer slots, and the arithmetic closes on both
     * architectures - x86 0x13C + 48 = 0x16C, + 116 = 0x1E0; amd64 0x250 +
     * 48 = 0x280, + 232 = 0x368, the two copy lengths registration uses.
     *
     * Every pointer slot is NULL-checked by the usbport wrapper that reads it,
     * and the twelve ULONGs are a count of extra common buffers (zero: none),
     * their sizes, and three context sizes behind a MiniPortFlags bit this
     * driver does not set. So everything here is left zero except
     * InterruptDpcEx, which is the slot the whole tier is declared for. The
     * unfilled callback slots are declared PVOID rather than with a signature:
     * naming a signature this driver never implements would document a
     * contract nobody here has exercised, and the ABI record carries what
     * usbport passes for anyone who later needs one. The two OUT fields are
     * services usbport writes at >= 300; nothing here calls them. Offsets in
     * the comments are x86 / amd64.
     */
    ULONG ExtraCommonBufferCount;                /* 0x13C / 0x250 - IN, 0 */
    ULONG ExtraCommonBufferSize[8];              /* 0x140 / 0x254 - IN, 0 */
    ULONG HsbControllerContextSize;              /* 0x160 / 0x274 - IN, 0 */
    ULONG HsbTtContextSize;                      /* 0x164 / 0x278 - IN, 0 */
    ULONG HsbEndpointContextSize;                /* 0x168 / 0x27C - IN, 0 */
    PVOID ReleasePortControl;                    /* 0x16C / 0x280 */
    PVOID ReadCfgFlag;                           /* 0x170 / 0x288 */
    PVOID SetWakeOnConnect;                      /* 0x174 / 0x290 */
    PHCI_INTERRUPT_DPC InterruptDpcEx;           /* 0x178 / 0x298 - FILLED */
    PVOID NotifyTransferQueueState;              /* 0x17C / 0x2A0 */
    PVOID CheckHwSync;                           /* 0x180 / 0x2A8 */
    PVOID UsbxInitHsbTransactionTranslator;      /* 0x184 / 0x2B0 */
    PVOID UsbxInitHsbController;                 /* 0x188 / 0x2B8 */
    PVOID UsbxInitHsbEndpoint;                   /* 0x18C / 0x2C0 */
    PVOID UsbxAllocateBandwidth;                 /* 0x190 / 0x2C8 */
    PVOID UsbxFreeBandwidth;                     /* 0x194 / 0x2D0 */
    PVOID Unreferenced198;                       /* 0x198 / 0x2D8 - no reader */
    PVOID UsbxPokeEndpoint;                      /* 0x19C / 0x2E0 */
    PVOID UsbxOpenEndpoint;                      /* 0x1A0 / 0x2E8 */
    PVOID UsbxQueryBandwidthData;                /* 0x1A4 / 0x2F0 */
    PVOID UsbxQueryTtBandwidthData;              /* 0x1A8 / 0x2F8 */
    PVOID UsbxQueryEpBandwidthData;              /* 0x1AC / 0x300 */
    PVOID UsbPortRequestAsyncCallbackEx;         /* 0x1B0 / 0x308 - OUT */
    PVOID UsbPortCancelAsyncCallback;            /* 0x1B4 / 0x310 - OUT */
    PVOID Unreferenced1B8;                       /* 0x1B8 / 0x318 - no reader */
    PVOID Unreferenced1BC;                       /* 0x1BC / 0x320 - no reader */
    PVOID Unreferenced1C0;                       /* 0x1C0 / 0x328 - no reader */
    PVOID CreateDeviceData;                      /* 0x1C4 / 0x330 */
    PVOID DeleteDeviceData;                      /* 0x1C8 / 0x338 */
    PVOID DbgFreeEndpoint;                       /* 0x1CC / 0x340 */
    PVOID Unreferenced1D0;                       /* 0x1D0 / 0x348 - no reader */
    PVOID Unreferenced1D4;                       /* 0x1D4 / 0x350 - no reader */
    PVOID HaltController;                        /* 0x1D8 / 0x358 */
    PVOID Get32BitMicroFrameNumber;              /* 0x1DC / 0x360 */
} USBPORT_REGISTRATION_PACKET, *PUSBPORT_REGISTRATION_PACKET;

/* ------------------------------------------------------------------ */
/* Layout asserts                                                      */
/* ------------------------------------------------------------------ */

/*
 * These fail the *driver* build, so a substituted type of the wrong width or
 * an accidental reorder can never reach a guest. They pin the sizes and the
 * group boundaries the disassembly actually confirmed; test/test_packet.c
 * pins the individual field offsets from a separately hand-typed table, so
 * a reorder inside a group is caught there with a diagnostic that names the
 * field instead of a line number.
 *
 * *(This said test_packet.c pinned **every** field offset, until round 11
 * pointed out that the isochronous block had neither a size assert here nor a
 * single offset there. It has both now. The claim is still not "every field of
 * every structure" - the ordinary support structures are pinned by size and by
 * group boundary - so it is written as what it is.)*
 */
/*
 * The inner `ULONG_PTR` cast is what keeps this usable on amd64: the address
 * is pointer-sized there, and going straight to `ULONG` is a pointer
 * truncation the compiler reports (C4311) - which `/WX` makes an error in the
 * free build. The value is an offset and genuinely fits; the cast says so
 * once, here, rather than at every use. On x86 both casts are identities.
 */
#define XHCI_OFFSET_OF(type, field) ((ULONG)(ULONG_PTR)&(((type *)0)->field))

/*
 * **Three of these structures are a different size on amd64, and the numbers
 * below are measured on both architectures rather than derived from one
 * another.** That distinction is the whole point: a size assert written from
 * the compiler's own layout asserts the compiler against itself and cannot
 * fail, and the registration packet is the case that proves it - declared the
 * obvious way under `_WIN64` it comes out `0x248` while usbport copies
 * `0x250`, an eight-byte overrun of a static global that no self-consistent
 * assert could ever have caught. The amd64 numbers come from design record 11:
 * M3 for the packet size and its short-copy boundary, M4 for
 * `USBPORT_RESOURCES`, M6 for the callback offsets, and M7 for
 * `USBPORT_ENDPOINT_PROPERTIES`.
 */
#ifdef _WIN64
XHCI_C_ASSERT(resources_size, sizeof(USBPORT_RESOURCES) == 0x48);
XHCI_C_ASSERT(endpoint_properties_size,
              sizeof(USBPORT_ENDPOINT_PROPERTIES) == 0x48);
#else
XHCI_C_ASSERT(resources_size, sizeof(USBPORT_RESOURCES) == 52);
XHCI_C_ASSERT(endpoint_properties_size,
              sizeof(USBPORT_ENDPOINT_PROPERTIES) == 64);
#endif

/*
 * The individual fields M7 read out of the amd64 `usbehci.sys`, and their x86
 * counterparts from the same record. `BufferVA` is the field that moves and
 * the reason the structure changes size at all, so it is pinned on both sides
 * along with the two fields either side of it and the TT pair at the tail -
 * the ones an off-by-eight would corrupt first.
 */
#ifdef _WIN64
XHCI_C_ASSERT(ep_props_device_speed_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, DeviceSpeed) == 0x08);
XHCI_C_ASSERT(ep_props_transfer_type_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, TransferType) == 0x14);
XHCI_C_ASSERT(ep_props_buffer_va_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferVA) == 0x20);
XHCI_C_ASSERT(ep_props_buffer_pa_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferPA) == 0x28);
XHCI_C_ASSERT(ep_props_buffer_length_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferLength) == 0x2C);
XHCI_C_ASSERT(ep_props_hub_addr_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, HubAddr) == 0x38);
XHCI_C_ASSERT(ep_props_port_number_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, PortNumber) == 0x3A);
XHCI_C_ASSERT(resources_start_pa_offset,
              XHCI_OFFSET_OF(USBPORT_RESOURCES, StartPA) == 0x40);
#else
XHCI_C_ASSERT(ep_props_device_speed_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, DeviceSpeed) == 0x08);
XHCI_C_ASSERT(ep_props_transfer_type_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, TransferType) == 0x14);
XHCI_C_ASSERT(ep_props_buffer_va_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferVA) == 0x1C);
XHCI_C_ASSERT(ep_props_buffer_pa_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferPA) == 0x20);
XHCI_C_ASSERT(ep_props_buffer_length_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferLength) == 0x24);
XHCI_C_ASSERT(ep_props_hub_addr_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, HubAddr) == 0x30);
XHCI_C_ASSERT(ep_props_port_number_offset,
              XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, PortNumber) == 0x32);
XHCI_C_ASSERT(resources_start_pa_offset,
              XHCI_OFFSET_OF(USBPORT_RESOURCES, StartPA) == 0x2C);
#endif

XHCI_C_ASSERT(endpoint_requirements_size,
              sizeof(USBPORT_ENDPOINT_REQUIREMENTS) == 8);
XHCI_C_ASSERT(setup_packet_size, sizeof(XHCI_SETUP_PACKET) == 8);
XHCI_C_ASSERT(transfer_parameters_size,
              sizeof(USBPORT_TRANSFER_PARAMETERS) == 28);
XHCI_C_ASSERT(sg_element_size,
              sizeof(USBPORT_SCATTER_GATHER_ELEMENT) == 24);
/*
 * **M8, and it is the reading that closes the last assumption on this page.**
 * These were the compiler's layout until 2026-09-09, written as `0x50` so that
 * the day someone read the real structure the assert would either confirm it
 * or fire. It fired - not as an assert, but on a Windows XP x64 guest, where
 * every control transfer came back `XHCI_XFER_SG_HIGH_ADDRESS` because the
 * element array was being read four bytes low. `sizeof` was right and
 * `SgElement[]` was not: the real element type is 8-aligned (its first member
 * is a `PHYSICAL_ADDRESS`), so the array starts at `0x20`, while a
 * declaration made of `ULONG`s aligns to 4 and put it at `0x1C`.
 *
 * Read from the producer in NT 5.2 amd64 `usbport.sys` 5.2.3790.3959 at RVA
 * `0xF468`: `lea rdi,[rsi+118h]` (the list, inside usbport's private transfer
 * record), `mov dword ptr [rdi],r12d` (Flags), `mov qword ptr [rdi+8],rcx`
 * (CurrentVa), `mov qword ptr [rdi+10h],rax` (MappedSystemVa),
 * `mov dword ptr [rdi+18h],r12d` and `inc dword ptr [rdi+18h]`
 * (SgElementCount), `lea rbx,[rdi+20h]` (SgElement[0]), `add rbx,18h` (the
 * stride), `mov qword ptr [rbx],rax` (the address), `mov dword ptr
 * [rbx-8],r8d` after that advance (the length, element `+0x10`) and
 * `mov dword ptr [rbx+14h],r11d` (the offset). Method `static`; design record
 * 11 section 5 M8 and `usbport-miniport-abi.md` carry it in full.
 */
#ifdef _WIN64
XHCI_C_ASSERT(sg_list_size, sizeof(USBPORT_SCATTER_GATHER_LIST) == 0x50);
XHCI_C_ASSERT(sg_list_element_offset,
              XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_LIST, SgElement) == 0x20);
XHCI_C_ASSERT(sg_list_count_offset,
              XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_LIST, SgElementCount) == 0x18);
XHCI_C_ASSERT(sg_element_length_offset,
              XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_ELEMENT,
                             SgTransferLength) == 0x10);
XHCI_C_ASSERT(sg_element_offset_offset,
              XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_ELEMENT, SgOffset) == 0x14);
#else
XHCI_C_ASSERT(sg_list_count_offset,
              XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_LIST, SgElementCount) == 0x0C);
XHCI_C_ASSERT(sg_element_length_offset,
              XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_ELEMENT,
                             SgTransferLength) == 0x0C);
XHCI_C_ASSERT(sg_element_offset_offset,
              XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_ELEMENT, SgOffset) == 0x10);
XHCI_C_ASSERT(sg_list_size, sizeof(USBPORT_SCATTER_GATHER_LIST) == 64);
XHCI_C_ASSERT(sg_list_element_offset,
              XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_LIST, SgElement) == 0x10);
#endif
/*
 * The isochronous block had neither of these until the post-Phase 13 review rounds, and the comment
 * above claimed test/test_packet.c pinned every field offset while that file did
 * not mention either structure. It is the block this driver reads at fixed
 * offsets on the strength of a signature word, so an accidental reorder is
 * exactly the failure the signature cannot catch. 0x38 is the measured entry
 * stride and 0x48 is **this declaration** - a 0x10 header plus one entry, which
 * is the shape the miniport indexes. usbport's own allocation is
 * `0x48 + 0x38 * NumberOfPackets`, so for one packet it is 0x80: an entry's
 * worth of measured, unexplained slack beyond header-plus-entries, which
 * nothing here relies on. *(Corrected, the day it was written: this
 * note called 0x48 "usbport's own allocation for one packet" and in the same
 * breath called it one entry larger than the declaration, which cannot both be
 * true of one number.)*
 */
XHCI_C_ASSERT(iso_packet_size, sizeof(USBPORT_ISO_PACKET) == 0x38);
XHCI_C_ASSERT(iso_transfer_size, sizeof(USBPORT_ISO_TRANSFER) == 0x48);
XHCI_C_ASSERT(root_hub_data_size, sizeof(USBPORT_ROOT_HUB_DATA) == 16);
XHCI_C_ASSERT(port_status_size,
              sizeof(USBPORT_PORT_STATUS_AND_CHANGE) == 4);
XHCI_C_ASSERT(hub_status_size, sizeof(USBPORT_HUB_STATUS_AND_CHANGE) == 4);

/*
 * The packet's own numbers. On amd64 every offset at or below the hinge is
 * unchanged and every offset above it lands on M6's widening map,
 * `f(X) = 0x28 + (X - 0x28) * 2` - ten leading `ULONG` data fields that do not
 * widen, followed by function pointers that all do. The two the map is worth
 * checking by hand are the hinge itself (`OpenEndpoint`, `0x28` on both) and
 * the field below it (`MiniPortResourcesSize`, `0x24` on both).
 *
 * `packet_size` and `packet_last_reserved` are the pair that catch the
 * eight-byte shortfall: M3 measured usbport copying `0x250`, and a declaration
 * whose two trailing canaries stayed 4 bytes ends at `0x248`.
 */
/*
 * Three tiers, three boundaries, on each architecture. `packet_size` is what
 * an NT 6.x usbport copies at Version 300; `packet_nt5_copy_boundary` is what
 * every NT 5.x and 9x usbport copies at Version 200 and is the offset of the
 * first 300-tier field, which is why that field and not `sizeof` carries the
 * old number; `packet_short_copy_boundary` is the Version < 200 length. The
 * 300-tier anchors are the slots the reading named as load-bearing: the
 * first pointer, InterruptDpcEx, the first OUT service and the last slot.
 */
#ifdef _WIN64
XHCI_C_ASSERT(packet_size, sizeof(USBPORT_REGISTRATION_PACKET) == 0x368);
XHCI_C_ASSERT(packet_nt5_copy_boundary,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, ExtraCommonBufferCount)
                  == 0x250);
XHCI_C_ASSERT(packet_first_nt6_pointer,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, ReleasePortControl)
                  == 0x280);
XHCI_C_ASSERT(packet_interrupt_dpc_ex,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, InterruptDpcEx)
                  == 0x298);
XHCI_C_ASSERT(packet_nt6_service_block,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET,
                             UsbPortRequestAsyncCallbackEx) == 0x308);
XHCI_C_ASSERT(packet_last_nt6_slot,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET,
                             Get32BitMicroFrameNumber) == 0x360);
XHCI_C_ASSERT(packet_short_copy_boundary,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, RH_ChirpRootPort)
                  == 0x230);
XHCI_C_ASSERT(packet_resources_size_offset,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, MiniPortResourcesSize)
                  == 0x24);
XHCI_C_ASSERT(packet_first_callback_offset,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, OpenEndpoint) == 0x28);
XHCI_C_ASSERT(packet_start_controller_offset,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, StartController) == 0x48);
XHCI_C_ASSERT(packet_first_roothub_offset,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, RH_GetRootHubData) == 0xF8);
XHCI_C_ASSERT(packet_send_one_packet_offset,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, StartSendOnePacket) == 0x188);
XHCI_C_ASSERT(packet_service_block_start,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, UsbPortDbgPrint) == 0x1A0);
XHCI_C_ASSERT(packet_service_block_end,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, UsbPortNotifyDoubleBuffer)
                  == 0x218);
XHCI_C_ASSERT(packet_tail_group_start,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, RebalanceEndpoint) == 0x220);
XHCI_C_ASSERT(packet_last_reserved,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, Reserved5) == 0x248);
#else
/* The number an NT 6.x usbport copies at Version 300. */
XHCI_C_ASSERT(packet_size, sizeof(USBPORT_REGISTRATION_PACKET) == 0x1E0);
/* The number every NT 5.x and 9x usbport copies at Version 200. */
XHCI_C_ASSERT(packet_nt5_copy_boundary,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, ExtraCommonBufferCount)
                  == 0x13C);
XHCI_C_ASSERT(packet_first_nt6_pointer,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, ReleasePortControl)
                  == 0x16C);
XHCI_C_ASSERT(packet_interrupt_dpc_ex,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, InterruptDpcEx)
                  == 0x178);
XHCI_C_ASSERT(packet_nt6_service_block,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET,
                             UsbPortRequestAsyncCallbackEx) == 0x1B0);
XHCI_C_ASSERT(packet_last_nt6_slot,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET,
                             Get32BitMicroFrameNumber) == 0x1DC);
/* ...and the boundary that makes the short copy exactly the tail group. */
XHCI_C_ASSERT(packet_short_copy_boundary,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, RH_ChirpRootPort)
                  == 0x12C);

XHCI_C_ASSERT(packet_resources_size_offset,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, MiniPortResourcesSize)
                  == 0x24);
XHCI_C_ASSERT(packet_first_callback_offset,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, OpenEndpoint) == 0x28);
XHCI_C_ASSERT(packet_start_controller_offset,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, StartController) == 0x38);
XHCI_C_ASSERT(packet_first_roothub_offset,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, RH_GetRootHubData) == 0x90);
XHCI_C_ASSERT(packet_send_one_packet_offset,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, StartSendOnePacket) == 0xD8);
/* The in/out boundary: the first and last words usbport writes back. */
XHCI_C_ASSERT(packet_service_block_start,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, UsbPortDbgPrint) == 0xE4);
XHCI_C_ASSERT(packet_service_block_end,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, UsbPortNotifyDoubleBuffer)
                  == 0x120);
XHCI_C_ASSERT(packet_tail_group_start,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, RebalanceEndpoint) == 0x124);
XHCI_C_ASSERT(packet_last_reserved,
              XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, Reserved5) == 0x138);
#endif

/* ------------------------------------------------------------------ */
/* The two usbport.sys exports (linked through src/usbport.lib)        */
/* ------------------------------------------------------------------ */

#ifndef XHCI_HOST_TEST
ULONG NTAPI USBPORT_GetHciMn(VOID);

/*
 * FOUR arguments on NT 6.x, three on NT 5.x, and the fourth is DriverEntry's
 * RegistryPath. Read out of the shipping binaries; the boundary is the same
 * on both architectures:
 *
 *   Win2000 SP4 / NUSB / XP SP3 (x86)      ret 0Ch                 three
 *   XP x64 / Server 2003 x64 (amd64)       no r9 read before call  three
 *   Vista x86 / Windows 7 x86              ret 10h                 four
 *   Vista x64 / Windows 7 x64              mov r14,r9 / mov r12,r9 four
 *
 * The identity of the fourth is taken from Microsoft's own call site rather
 * than inferred from its shape: Vista x64's usbehci.sys DriverEntry does
 * `mov rdi,rdx` in its prologue - rdx being DriverEntry's RegistryPath - and
 * `mov r9,rdi` immediately before the call. usbport then dereferences it as a
 * UNICODE_STRING (`movzx r8d,word ptr [r14]` = Length, `mov rdx,qword ptr
 * [r14+8]` = Buffer) and copies the buffer into an allocation of its own.
 *
 * Passing it unconditionally is correct on all four amd64 targets and needs
 * no version test. The x64 convention is caller-cleaned and the 32-byte
 * shadow space for four register arguments is allocated either way, so no
 * arity mismatch can unbalance the stack; and NT 5.2's usbport reaches its
 * first call without reading r9, which - being volatile - it can never
 * recover afterwards. A three-parameter function compiled from source cannot
 * observe a fourth argument.
 *
 * MEASURED, not deduced. A three-argument call bugchecks Vista x64 with
 * 0x7E / STATUS_ACCESS_VIOLATION inside USBPORT!memmove, reached from
 * USBPORT_RegisterUSBPortDriver+0x46a, r9 holding whatever nt!IopLoadDriver
 * last left there - on the guest of 2026-09-10 an address inside ntoskrnl's
 * own image, whose instruction bytes read back as Length=0x9000 and
 * Buffer=0x9090... (roadmap task 21.8).
 *
 * ON x86 THE ARITY MUST MATCH EXACTLY, AND SINCE TASK 22.5 IT DOES ON BOTH
 * SIDES OF THE BOUNDARY. stdcall is callee-cleaned: a three-argument call
 * into a `ret 10h` callee leaves the stack four bytes wrong on return, and
 * a four-argument call into a `ret 0Ch` one leaves it four bytes the other
 * way, on Windows 98, Windows 2000 and XP - the primary targets. What makes
 * a runtime choice possible without a second import stub is that
 * src\usbport.lib binds the import BY NAME: `_USBPORT_RegisterUSBPortDriver@12`
 * is only the linker-side name of the one IAT slot, and the loader resolves
 * that slot against usbport's plain export `USBPORT_RegisterUSBPortDriver`
 * whatever the callee's arity. So DriverEntry calls through that slot with
 * the prototype below on NT 5.x and 9x, and through XHCI_REGISTER_USBPORT_NT6
 * - a cast of the same address - on NT 6.x, and the compiler emits the push
 * count and the post-call stack expectation of the prototype it was given.
 * `XHCI_CHECK_STACK_DELTA` in the qemu flavour measures esp across the call
 * and reports an imbalance, which is the net under both arms.
 */
NTSTATUS NTAPI USBPORT_RegisterUSBPortDriver(
    IN PDRIVER_OBJECT DriverObject,
    IN ULONG Version,
    IN PUSBPORT_REGISTRATION_PACKET RegistrationPacket);

/*
 * The NT 6.x form of the same export, called through a cast because the
 * import library carries one symbol and this is the same entry point with a
 * fourth argument. On amd64 the cast changes nothing about the stack (x64 is
 * caller-cleaned with a fixed shadow area); on x86 it is what makes the
 * emitted call a four-argument stdcall call, `push` count and post-call
 * expectation both - see the block above. Both architectures since task
 * 22.5; it was amd64-only from task 21.8 until then.
 */
typedef NTSTATUS (NTAPI *XHCI_REGISTER_USBPORT_NT6)(
    IN PDRIVER_OBJECT DriverObject,
    IN ULONG Version,
    IN PUSBPORT_REGISTRATION_PACKET RegistrationPacket,
    IN PUNICODE_STRING RegistryPath);
#endif

#endif /* XHCI_USBPORT_H */
