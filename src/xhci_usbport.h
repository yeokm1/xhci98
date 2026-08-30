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
 *   - sizeof(USBPORT_REGISTRATION_PACKET) = 0x13C (316). Registration copies
 *     exactly that many bytes when the Version argument is >= 200, and 0x12C
 *     (300) when 100 <= Version < 200.
 *   - The miniport fills 0x00-0x130; usbport writes 16 service pointers at
 *     0xE4-0x120 and touches no other field before copying.
 *   - USBPORT_GetHciMn returns 0x57324B30 on both primary targets and
 *     0x10000001 on the XP lineage.
 *
 * The NT types the real header uses are deliberately *not* pulled in here.
 * Enums become ULONG and 64-bit fields become Lo/Hi ULONG pairs, per AGENTS.md
 * ("no enums for hardware layouts", "no 64-bit arithmetic"); the substituted
 * types are all 4-byte-exact on x86, and the size/offset asserts at the bottom
 * of this file are what proves that claim rather than asserting it in prose.
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

/* USBPORT_RESOURCES.ResourcesTypes */
#define USBPORT_RESOURCES_PORT      1
#define USBPORT_RESOURCES_INTERRUPT 2
#define USBPORT_RESOURCES_MEMORY    4

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
typedef struct _USBPORT_RESOURCES {
    ULONG ResourcesTypes;       /* 0x00 PORT|INTERRUPT|MEMORY bitmask       */
    ULONG HcFlavor;             /* 0x04 USB_CONTROLLER_FLAVOR enum          */
    ULONG InterruptVector;      /* 0x08                                     */
    UCHAR InterruptLevel;       /* 0x0C KIRQL                               */
    UCHAR Padded1[3];
    ULONG InterruptAffinity;    /* 0x10 KAFFINITY                           */
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
    ULONG Reserved1;            /* 0x08 */
    ULONG SgTransferLength;     /* 0x0C */
    ULONG SgOffset;             /* 0x10 offset within the whole transfer buf  */
    ULONG Reserved2;            /* 0x14 */
} USBPORT_SCATTER_GATHER_ELEMENT, *PUSBPORT_SCATTER_GATHER_ELEMENT;

typedef struct _USBPORT_SCATTER_GATHER_LIST {
    ULONG Flags;                /* 0x00 */
    ULONG_PTR CurrentVa;        /* 0x04 */
    PVOID MappedSystemVa;       /* 0x08 */
    ULONG SgElementCount;       /* 0x0C */
    USBPORT_SCATTER_GATHER_ELEMENT SgElement[2];  /* 0x10, variable length */
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
typedef VOID (NTAPI *PHCI_CLOSE_ENDPOINT)(PVOID, PVOID, BOOLEAN);
typedef MPSTATUS (NTAPI *PHCI_START_CONTROLLER)(PVOID, PUSBPORT_RESOURCES);
typedef VOID (NTAPI *PHCI_STOP_CONTROLLER)(PVOID, BOOLEAN);
typedef VOID (NTAPI *PHCI_SUSPEND_CONTROLLER)(PVOID);
typedef MPSTATUS (NTAPI *PHCI_RESUME_CONTROLLER)(PVOID);
typedef BOOLEAN (NTAPI *PHCI_INTERRUPT_SERVICE)(PVOID);
typedef VOID (NTAPI *PHCI_INTERRUPT_DPC)(PVOID, BOOLEAN);
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
typedef MPSTATUS (NTAPI *PUSBPORT_GET_MINIPORT_REGISTRY_KEY_VALUE)(PVOID, ULONG, PVOID, ULONG, PVOID, ULONG);
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
typedef ULONG (NTAPI *PUSBPORT_REQUEST_ASYNC_CALLBACK)(PVOID, ULONG, PVOID, ULONG, XHCI_ASYNC_TIMER_CALLBACK *);
typedef MPSTATUS (NTAPI *PUSBPORT_READ_WRITE_CONFIG_SPACE)(PVOID, BOOLEAN, PVOID, ULONG, ULONG);
typedef LONG (NTAPI *PUSBPORT_WAIT)(PVOID, ULONG);
typedef ULONG (NTAPI *PUSBPORT_INVALIDATE_CONTROLLER)(PVOID, ULONG);
typedef VOID (NTAPI *PUSBPORT_BUG_CHECK)(PVOID);
typedef ULONG (NTAPI *PUSBPORT_NOTIFY_DOUBLE_BUFFER)(PVOID, PVOID, PVOID, ULONG);

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
    ULONG Reserved4;                             /* 0x134 canary */
    ULONG Reserved5;                             /* 0x138 canary */
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
#define XHCI_OFFSET_OF(type, field) ((ULONG)&(((type *)0)->field))

XHCI_C_ASSERT(resources_size, sizeof(USBPORT_RESOURCES) == 52);
XHCI_C_ASSERT(endpoint_properties_size,
              sizeof(USBPORT_ENDPOINT_PROPERTIES) == 64);
XHCI_C_ASSERT(endpoint_requirements_size,
              sizeof(USBPORT_ENDPOINT_REQUIREMENTS) == 8);
XHCI_C_ASSERT(setup_packet_size, sizeof(XHCI_SETUP_PACKET) == 8);
XHCI_C_ASSERT(transfer_parameters_size,
              sizeof(USBPORT_TRANSFER_PARAMETERS) == 28);
XHCI_C_ASSERT(sg_element_size,
              sizeof(USBPORT_SCATTER_GATHER_ELEMENT) == 24);
XHCI_C_ASSERT(sg_list_size, sizeof(USBPORT_SCATTER_GATHER_LIST) == 64);
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

/* The number registration copies at Version >= 200. */
XHCI_C_ASSERT(packet_size, sizeof(USBPORT_REGISTRATION_PACKET) == 0x13C);
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

/* ------------------------------------------------------------------ */
/* The two usbport.sys exports (linked through src/usbport.lib)        */
/* ------------------------------------------------------------------ */

#ifndef XHCI_HOST_TEST
ULONG NTAPI USBPORT_GetHciMn(VOID);

NTSTATUS NTAPI USBPORT_RegisterUSBPortDriver(
    IN PDRIVER_OBJECT DriverObject,
    IN ULONG Version,
    IN PUSBPORT_REGISTRATION_PACKET RegistrationPacket);
#endif

#endif /* XHCI_USBPORT_H */
