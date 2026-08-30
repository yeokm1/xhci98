/*
 * test_packet.c - host tests for the usbport miniport ABI declaration.
 *
 * Covers src/xhci_usbport.h, which is the shape of every conversation this
 * driver will ever have with usbport.sys. If a field of the registration
 * packet moves, registration still "succeeds" - usbport copies 316 bytes
 * either way - and the damage appears later as a callback jumping through the
 * wrong slot with arguments meant for a different function. There is no
 * diagnostic for that on either guest, so it gets caught here.
 *
 * Every expected value below is transcribed **by hand** from the offset table
 * in docs/usb-xhci-info/usbport-miniport-abi.md section 3 (the one confirmed field-for-field
 * against the three shipping binaries), not produced by the code under test.
 * The header carries its own compile-time asserts for the sizes and group
 * boundaries; this file exists to pin the fields *between* those boundaries,
 * with a failure message that names which one moved.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_usbport.h"

static int failures;
static int checks;

#define CHECK(cond, what) check_impl((cond), (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s:%d: %s\n", "test_packet.c", line, what);
    }
}

#define CHECK_EQ(got, want, what) \
    check_eq_impl((unsigned long)(got), (unsigned long)(want), (what), __LINE__)

static void check_eq_impl(unsigned long got, unsigned long want,
                          const char *what, int line)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s:%d: %s (got %lu / 0x%lX, want %lu / 0x%lX)\n",
               "test_packet.c", line, what, got, got, want, want);
    }
}

/* Offset of a registration-packet field against its hand-typed expectation. */
#define PACKET_OFFSET(field, expected) \
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, field), (expected), \
             "packet offset of " #field)

/* ------------------------------------------------------------------ */
/* 1. The registration packet, field by field                          */
/* ------------------------------------------------------------------ */

/*
 * The whole table, in declaration order, as
 * docs/usb-xhci-info/usbport-miniport-abi.md section 3 lists it. Nothing is skipped: the
 * point of a second transcription is defeated the moment it becomes a sample.
 */
static void test_packet_data_fields(void)
{
    PACKET_OFFSET(MiniPortVersion, 0x00);
    PACKET_OFFSET(MiniPortFlags, 0x04);
    PACKET_OFFSET(MiniPortBusBandwidth, 0x08);
    PACKET_OFFSET(Reserved1, 0x0C);
    PACKET_OFFSET(MiniPortExtensionSize, 0x10);
    PACKET_OFFSET(MiniPortEndpointSize, 0x14);
    PACKET_OFFSET(MiniPortTransferSize, 0x18);
    PACKET_OFFSET(Reserved2, 0x1C);
    PACKET_OFFSET(Reserved3, 0x20);
    PACKET_OFFSET(MiniPortResourcesSize, 0x24);
}

static void test_packet_miniport_callbacks(void)
{
    PACKET_OFFSET(OpenEndpoint, 0x28);
    PACKET_OFFSET(ReopenEndpoint, 0x2C);
    PACKET_OFFSET(QueryEndpointRequirements, 0x30);
    PACKET_OFFSET(CloseEndpoint, 0x34);
    PACKET_OFFSET(StartController, 0x38);
    PACKET_OFFSET(StopController, 0x3C);
    PACKET_OFFSET(SuspendController, 0x40);
    PACKET_OFFSET(ResumeController, 0x44);
    PACKET_OFFSET(InterruptService, 0x48);
    PACKET_OFFSET(InterruptDpc, 0x4C);
    PACKET_OFFSET(SubmitTransfer, 0x50);
    PACKET_OFFSET(SubmitIsoTransfer, 0x54);
    PACKET_OFFSET(AbortTransfer, 0x58);
    PACKET_OFFSET(GetEndpointState, 0x5C);
    PACKET_OFFSET(SetEndpointState, 0x60);
    PACKET_OFFSET(PollEndpoint, 0x64);
    PACKET_OFFSET(CheckController, 0x68);
    PACKET_OFFSET(Get32BitFrameNumber, 0x6C);
    PACKET_OFFSET(InterruptNextSOF, 0x70);
    PACKET_OFFSET(EnableInterrupts, 0x74);
    PACKET_OFFSET(DisableInterrupts, 0x78);
    PACKET_OFFSET(PollController, 0x7C);
    PACKET_OFFSET(SetEndpointDataToggle, 0x80);
    PACKET_OFFSET(GetEndpointStatus, 0x84);
    PACKET_OFFSET(SetEndpointStatus, 0x88);
    PACKET_OFFSET(ResetController, 0x8C);
}

static void test_packet_roothub_callbacks(void)
{
    PACKET_OFFSET(RH_GetRootHubData, 0x90);
    PACKET_OFFSET(RH_GetStatus, 0x94);
    PACKET_OFFSET(RH_GetPortStatus, 0x98);
    PACKET_OFFSET(RH_GetHubStatus, 0x9C);
    PACKET_OFFSET(RH_SetFeaturePortReset, 0xA0);
    PACKET_OFFSET(RH_SetFeaturePortPower, 0xA4);
    PACKET_OFFSET(RH_SetFeaturePortEnable, 0xA8);
    PACKET_OFFSET(RH_SetFeaturePortSuspend, 0xAC);
    PACKET_OFFSET(RH_ClearFeaturePortEnable, 0xB0);
    PACKET_OFFSET(RH_ClearFeaturePortPower, 0xB4);
    PACKET_OFFSET(RH_ClearFeaturePortSuspend, 0xB8);
    PACKET_OFFSET(RH_ClearFeaturePortEnableChange, 0xBC);
    PACKET_OFFSET(RH_ClearFeaturePortConnectChange, 0xC0);
    PACKET_OFFSET(RH_ClearFeaturePortResetChange, 0xC4);
    PACKET_OFFSET(RH_ClearFeaturePortSuspendChange, 0xC8);
    PACKET_OFFSET(RH_ClearFeaturePortOvercurrentChange, 0xCC);
    PACKET_OFFSET(RH_DisableIrq, 0xD0);
    PACKET_OFFSET(RH_EnableIrq, 0xD4);
}

static void test_packet_service_block(void)
{
    PACKET_OFFSET(StartSendOnePacket, 0xD8);
    PACKET_OFFSET(EndSendOnePacket, 0xDC);
    PACKET_OFFSET(PassThru, 0xE0);

    /* usbport writes exactly these 16 words and nothing else before copying. */
    PACKET_OFFSET(UsbPortDbgPrint, 0xE4);
    PACKET_OFFSET(UsbPortTestDebugBreak, 0xE8);
    PACKET_OFFSET(UsbPortAssertFailure, 0xEC);
    PACKET_OFFSET(UsbPortGetMiniportRegistryKeyValue, 0xF0);
    PACKET_OFFSET(UsbPortInvalidateRootHub, 0xF4);
    PACKET_OFFSET(UsbPortInvalidateEndpoint, 0xF8);
    PACKET_OFFSET(UsbPortCompleteTransfer, 0xFC);
    PACKET_OFFSET(UsbPortCompleteIsoTransfer, 0x100);
    PACKET_OFFSET(UsbPortLogEntry, 0x104);
    PACKET_OFFSET(UsbPortGetMappedVirtualAddress, 0x108);
    PACKET_OFFSET(UsbPortRequestAsyncCallback, 0x10C);
    PACKET_OFFSET(UsbPortReadWriteConfigSpace, 0x110);
    PACKET_OFFSET(UsbPortWait, 0x114);
    PACKET_OFFSET(UsbPortInvalidateController, 0x118);
    PACKET_OFFSET(UsbPortBugCheck, 0x11C);
    PACKET_OFFSET(UsbPortNotifyDoubleBuffer, 0x120);

    /*
     * DriverEntry walks the service block as 16 consecutive words to count how
     * many usbport filled in. That walk is only meaningful while the block
     * really is contiguous and really is 16 long, so state it here rather than
     * leaving it as an assumption inside a diagnostic.
     */
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET,
                            UsbPortNotifyDoubleBuffer) -
                 XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, UsbPortDbgPrint),
             15 * 4, "service block is 16 contiguous words");
}

static void test_packet_tail(void)
{
    PACKET_OFFSET(RebalanceEndpoint, 0x124);
    PACKET_OFFSET(FlushInterrupts, 0x128);
    PACKET_OFFSET(RH_ChirpRootPort, 0x12C);
    PACKET_OFFSET(TakePortControl, 0x130);
    PACKET_OFFSET(Reserved4, 0x134);
    PACKET_OFFSET(Reserved5, 0x138);

    CHECK_EQ(sizeof(USBPORT_REGISTRATION_PACKET), 316,
             "packet size copied at Version >= 200");

    /*
     * The 16-byte difference between the two copy sizes the binaries use must
     * be exactly the four tail fields - that is how a Version < 200 miniport
     * ends up with RH_ChirpRootPort ungated.
     */
    CHECK_EQ(sizeof(USBPORT_REGISTRATION_PACKET) -
                 XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, RH_ChirpRootPort),
             16, "the short 300-byte copy stops exactly before the tail four");
}

/* ------------------------------------------------------------------ */
/* 2. Support structures                                               */
/* ------------------------------------------------------------------ */

static void test_resources(void)
{
    CHECK_EQ(sizeof(USBPORT_RESOURCES), 52, "USBPORT_RESOURCES size");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, ResourcesTypes), 0x00,
             "resources ResourcesTypes");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, HcFlavor), 0x04,
             "resources HcFlavor");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, InterruptVector), 0x08,
             "resources InterruptVector");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, InterruptLevel), 0x0C,
             "resources InterruptLevel");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, InterruptAffinity), 0x10,
             "resources InterruptAffinity");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, ShareVector), 0x14,
             "resources ShareVector");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, InterruptMode), 0x18,
             "resources InterruptMode");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, Reserved), 0x1C,
             "resources Reserved");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, ResourceBase), 0x20,
             "resources ResourceBase (mapped BAR0)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, IoSpaceLength), 0x24,
             "resources IoSpaceLength");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, StartVA), 0x28,
             "resources StartVA (common buffer)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, StartPA), 0x2C,
             "resources StartPA (common buffer)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, LegacySupport), 0x30,
             "resources LegacySupport (the one OUT field)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, IsChirpHandled), 0x31,
             "resources IsChirpHandled");

    /* StartController dumps the struct as whole words; it has to divide. */
    CHECK_EQ(sizeof(USBPORT_RESOURCES) % 4, 0,
             "resources dumps evenly as ULONGs");
}

static void test_endpoint_properties(void)
{
    CHECK_EQ(sizeof(USBPORT_ENDPOINT_PROPERTIES), 64,
             "USBPORT_ENDPOINT_PROPERTIES size");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, DeviceAddress), 0x00,
             "properties DeviceAddress");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, EndpointAddress), 0x02,
             "properties EndpointAddress");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, TotalMaxPacketSize),
             0x04, "properties TotalMaxPacketSize (corrected EP0 MPS0)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, Period), 0x06,
             "properties Period");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, DeviceSpeed), 0x08,
             "properties DeviceSpeed");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, UsbBandwidth), 0x0C,
             "properties UsbBandwidth");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, ScheduleOffset), 0x10,
             "properties ScheduleOffset");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, TransferType), 0x14,
             "properties TransferType");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, Direction), 0x18,
             "properties Direction");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferVA), 0x1C,
             "properties BufferVA (per-endpoint common buffer)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferPA), 0x20,
             "properties BufferPA");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferLength), 0x24,
             "properties BufferLength");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, MaxTransferSize), 0x2C,
             "properties MaxTransferSize");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, HubAddr), 0x30,
             "properties HubAddr (TT hub, or 0xFFFF)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, PortNumber), 0x32,
             "properties PortNumber");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES,
                            InterruptScheduleMask), 0x34,
             "properties InterruptScheduleMask");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, SplitCompletionMask),
             0x35, "properties SplitCompletionMask");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES,
                            TransactionPerMicroframe), 0x36,
             "properties TransactionPerMicroframe");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, MaxPacketSize), 0x38,
             "properties MaxPacketSize");

    CHECK_EQ(sizeof(USBPORT_ENDPOINT_REQUIREMENTS), 8,
             "USBPORT_ENDPOINT_REQUIREMENTS size");
}

static void test_transfer_structures(void)
{
    CHECK_EQ(sizeof(USBPORT_TRANSFER_PARAMETERS), 28,
             "USBPORT_TRANSFER_PARAMETERS size");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_TRANSFER_PARAMETERS, TransferFlags), 0x00,
             "transfer TransferFlags");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_TRANSFER_PARAMETERS, TransferBufferLength),
             0x04, "transfer TransferBufferLength");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_TRANSFER_PARAMETERS, TransferCounter), 0x08,
             "transfer TransferCounter");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_TRANSFER_PARAMETERS, IsTransferSplited),
             0x0C, "transfer IsTransferSplited");
    /* Where SET_ADDRESS is intercepted in Phase 6. */
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_TRANSFER_PARAMETERS, SetupPacket), 0x14,
             "transfer SetupPacket");

    CHECK_EQ(sizeof(XHCI_SETUP_PACKET), 8, "SETUP packet size");
    CHECK_EQ(XHCI_OFFSET_OF(XHCI_SETUP_PACKET, bmRequestType), 0,
             "SETUP bmRequestType");
    CHECK_EQ(XHCI_OFFSET_OF(XHCI_SETUP_PACKET, bRequest), 1, "SETUP bRequest");
    CHECK_EQ(XHCI_OFFSET_OF(XHCI_SETUP_PACKET, wValue), 2, "SETUP wValue");
    CHECK_EQ(XHCI_OFFSET_OF(XHCI_SETUP_PACKET, wIndex), 4, "SETUP wIndex");
    CHECK_EQ(XHCI_OFFSET_OF(XHCI_SETUP_PACKET, wLength), 6, "SETUP wLength");

    CHECK_EQ(sizeof(USBPORT_SCATTER_GATHER_ELEMENT), 24, "SG element size");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_ELEMENT,
                            SgPhysicalAddressLo), 0x00, "SG address low");
    /* The high DWORD is a value to check against zero, never to compute with.
     * usbport does **not** force it: it stores the HAL's PHYSICAL_ADDRESS
     * verbatim and the zero comes from the 32-bit DMA adapter contract
     * (docs/usb-xhci-info/usbport-miniport-abi.md, "The high DWORD is zero, but
     * usbport does not mask it"). This assertion is about the offset and is
     * unaffected; the sentence explaining it said "usbport forces it to zero"
     * until the post-Phase 13 review rounds, which is the reading that document corrected. */
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_ELEMENT,
                            SgPhysicalAddressHi), 0x04, "SG address high");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_ELEMENT, SgTransferLength),
             0x0C, "SG element length");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_ELEMENT, SgOffset), 0x10,
             "SG element offset within the transfer buffer");

    CHECK_EQ(sizeof(USBPORT_SCATTER_GATHER_LIST), 64,
             "SG list size with two elements");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_LIST, SgElementCount), 0x0C,
             "SG list element count");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_LIST, SgElement), 0x10,
             "SG list first element");
}

/*
 * The isochronous parameter block (task 9-0.1). Added: `xhci_usbport.h`
 * said this file pinned every field offset and this block was the exception, so
 * the one structure the driver reads at fixed offsets behind nothing but a
 * signature word had no offset coverage at all.
 *
 * The two OUT fields are the ones a wrong offset corrupts silently - the
 * miniport writes `LengthTransferred` and `Status` into usbport's own block -
 * so they are pinned beside the inputs rather than trusted to the size.
 */
static void test_iso_block(void)
{
    CHECK_EQ(sizeof(USBPORT_ISO_PACKET), 0x38, "iso packet entry stride");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, Length), 0x00, "iso Length");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, LengthTransferred), 0x04,
             "iso LengthTransferred - the miniport writes it");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, FrameNumber), 0x08,
             "iso FrameNumber");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, MicroFrame), 0x0C,
             "iso MicroFrame");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, Status), 0x10,
             "iso Status - the miniport writes it");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, FragmentCount), 0x14,
             "iso FragmentCount");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, Fragment0Length), 0x18,
             "iso Fragment0Length");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, Fragment0AddressLo), 0x20,
             "iso Fragment0 address low");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, Fragment0AddressHi), 0x24,
             "iso Fragment0 address high - check it, never assume 0");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, Fragment1Length), 0x28,
             "iso Fragment1Length");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, Fragment1AddressLo), 0x30,
             "iso Fragment1 address low");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_PACKET, Fragment1AddressHi), 0x34,
             "iso Fragment1 address high");

    CHECK_EQ(sizeof(USBPORT_ISO_TRANSFER), 0x48,
             "iso block with one packet entry");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_TRANSFER, Signature), 0x00,
             "iso Signature");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_TRANSFER, NumberOfPackets), 0x04,
             "iso NumberOfPackets");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_TRANSFER, SgElementCount), 0x08,
             "iso SgElementCount - at 0x08 HERE, copied from the SG list's 0x0C");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ISO_TRANSFER, Packet), 0x10,
             "iso first packet entry");
    CHECK_EQ(USBPORT_ISO_SIGNATURE, 0x636F7349UL, "'Isoc' little-endian");
}

static void test_root_hub_data(void)
{
    CHECK_EQ(sizeof(USBPORT_ROOT_HUB_DATA), 16, "USBPORT_ROOT_HUB_DATA size");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ROOT_HUB_DATA, NumberOfPorts), 0x00,
             "root hub NumberOfPorts");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ROOT_HUB_DATA, HubCharacteristics), 0x04,
             "root hub HubCharacteristics");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ROOT_HUB_DATA, PowerOnToPowerGood), 0x08,
             "root hub PowerOnToPowerGood");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ROOT_HUB_DATA, HubControlCurrent), 0x0C,
             "root hub HubControlCurrent");

    CHECK_EQ(sizeof(USBPORT_PORT_STATUS_AND_CHANGE), 4, "port status size");
    CHECK_EQ(sizeof(USBPORT_HUB_STATUS_AND_CHANGE), 4, "hub status size");
}

/* ------------------------------------------------------------------ */
/* 3. Constants the registration call depends on                       */
/* ------------------------------------------------------------------ */

static void test_constants(void)
{
    /* Read out of USBPORT_RegisterUSBPortDriver in all three shipping builds:
     * < 100 is rejected, >= 200 selects the 316-byte copy. */
    CHECK_EQ(USB10_MINIPORT_INTERFACE_VERSION, 100, "USB1.1 interface version");
    CHECK_EQ(USB20_MINIPORT_INTERFACE_VERSION, 200, "USB2 interface version");

    /* Not ReactOS's 0x10000001 on either primary target - that value is the
     * XP lineage's, and hard-coding it alone would abort DriverEntry on both. */
    CHECK_EQ(USBPORT_HCI_MN_W2K, 0x57324B30UL, "GetHciMn on Win98/NUSB + SP4");
    CHECK_EQ(USBPORT_HCI_MN_XP, 0x10000001UL, "GetHciMn on XP");

    CHECK_EQ(USB_MINIPORT_VERSION_EHCI, 0x03, "packet MiniPortVersion for EHCI");
    CHECK_EQ(USB_MINIPORT_VERSION_XHCI, 0x04, "packet MiniPortVersion for XHCI");

    /* The flag word both primary targets' own usbehci.sys declares. */
    CHECK_EQ(USB_MINIPORT_FLAGS_INTERRUPT | USB_MINIPORT_FLAGS_MEMORY_IO |
                 USB_MINIPORT_FLAGS_USB2 | USB_MINIPORT_FLAGS_POLLING,
             0x95, "first-probe MiniPortFlags");
    /* Setting this one would silently zero MiniPortResourcesSize and skip the
     * DMA adapter, with no diagnostic anywhere. */
    CHECK_EQ(USB_MINIPORT_FLAGS_NO_DMA, 0x0100, "NO_DMA bit position");
    CHECK_EQ(USB_MINIPORT_FLAGS_WAKE_SUPPORT, 0x0200, "WAKE_SUPPORT bit");

    CHECK_EQ(TOTAL_USB20_BUS_BANDWIDTH, 400000, "USB2 bus bandwidth");

    CHECK_EQ(MP_STATUS_SUCCESS, 0, "MP_STATUS_SUCCESS");
    CHECK_EQ(MP_STATUS_NO_RESOURCES, 2, "MP_STATUS_NO_RESOURCES");
    CHECK_EQ(MP_STATUS_NO_BANDWIDTH, 3, "MP_STATUS_NO_BANDWIDTH");
    CHECK_EQ(MP_STATUS_NOT_SUPPORTED, 6, "MP_STATUS_NOT_SUPPORTED");

    CHECK_EQ(USBPORT_RESOURCES_PORT, 1, "resource type PORT");
    CHECK_EQ(USBPORT_RESOURCES_INTERRUPT, 2, "resource type INTERRUPT");
    CHECK_EQ(USBPORT_RESOURCES_MEMORY, 4, "resource type MEMORY");

    /* There is no endpoint state 1 - do not invent one. */
    CHECK_EQ(USBPORT_ENDPOINT_PAUSED, 2, "endpoint state PAUSED");
    CHECK_EQ(USBPORT_ENDPOINT_ACTIVE, 3, "endpoint state ACTIVE");
    CHECK_EQ(USBPORT_ENDPOINT_CLOSED, 5, "endpoint state CLOSED");
}

/* ------------------------------------------------------------------ */
/* 4. The extensions usbport allocates on our behalf                   */
/* ------------------------------------------------------------------ */

static void test_extensions(void)
{
    /* usbport allocates and zeroes exactly MiniPortExtensionSize bytes, so a
     * zero size would hand every callback a pointer to usbport's own state. */
    CHECK(sizeof(XHCI_EXTENSION) > 0, "device extension is not empty");
    CHECK(sizeof(XHCI_ENDPOINT) > 0, "endpoint extension is not empty");
    CHECK(sizeof(XHCI_TRANSFER) > 0, "transfer extension is not empty");

    /*
     * The signature pair has to bracket the whole extension for the validity
     * check to mean what it claims: first word and last word.
     */
    CHECK_EQ(XHCI_OFFSET_OF(XHCI_EXTENSION, Signature), 0,
             "extension signature is the first word");
    CHECK_EQ(XHCI_OFFSET_OF(XHCI_EXTENSION, TrailingSignature),
             sizeof(XHCI_EXTENSION) - 4,
             "extension trailing signature is the last word");
    CHECK_EQ(XHCI_OFFSET_OF(XHCI_ENDPOINT, Signature), 0,
             "endpoint signature is the first word");
    CHECK_EQ(XHCI_OFFSET_OF(XHCI_TRANSFER, Signature), 0,
             "transfer signature is the first word");

    /* Distinct values, or the bracket check passes on the wrong object. */
    CHECK(XHCI_EXTENSION_SIGNATURE != XHCI_EXTENSION_TRAILING,
          "extension signatures differ from each other");
    CHECK(XHCI_EXTENSION_SIGNATURE != XHCI_ENDPOINT_SIGNATURE &&
              XHCI_EXTENSION_SIGNATURE != XHCI_TRANSFER_SIGNATURE &&
              XHCI_ENDPOINT_SIGNATURE != XHCI_TRANSFER_SIGNATURE,
          "the three extension signatures are distinct");
}

int main(void)
{
    test_packet_data_fields();
    test_packet_miniport_callbacks();
    test_packet_roothub_callbacks();
    test_packet_service_block();
    test_packet_tail();
    test_resources();
    test_endpoint_properties();
    test_transfer_structures();
    test_iso_block();
    test_root_hub_data();
    test_constants();
    test_extensions();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
