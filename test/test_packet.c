/*
 * test_packet.c - host tests for the usbport miniport ABI declaration.
 *
 * Covers src/xhci_usbport.h, which is the shape of every conversation this
 * driver will ever have with usbport.sys. If a field of the registration
 * packet moves, registration still "succeeds" - usbport copies 316 bytes
 * either way, or 592 on amd64, and 480 / 872 from an NT 6.x usbport offered
 * Version 300 - and the damage appears later as a callback
 * jumping through the wrong slot with arguments meant for a different
 * function. There is no
 * diagnostic for that on either guest, so it gets caught here.
 *
 * Every expected value below is transcribed **by hand** from the offset table
 * in docs/usb-xhci-info/usbport-miniport-abi.md section 3 (the one confirmed field-for-field
 * against the three shipping binaries), not produced by the code under test.
 * The header carries its own compile-time asserts for the sizes and group
 * boundaries; this file exists to pin the fields *between* those boundaries,
 * with a failure message that names which one moved.
 *
 * **So read the check count here as smaller than it looks.** About two dozen
 * of the values below - the structure sizes, and the packet offsets at 0x24,
 * 0x28, 0x38, 0x90 and the group boundaries around them - already have an
 * `XHCI_C_ASSERT` twin in `src/xhci_usbport.h`, so a violation stops the
 * COMPILE and this file never runs to report it. They are kept deliberately,
 * because the anchors are what the fields between them are measured from and
 * a reader checking one field against the ABI document should find its
 * neighbours here too - but they are restatements, not coverage, and the
 * 2026-09-07 audit's G13 counted them as such. What only exists here is the
 * per-field offsets between the anchors, which is most of section 1.
 *
 * **This suite is compiled and run twice, once per architecture** (task 21.4).
 * `run-host-tests.cmd` builds it with MSVC 6.0 for x86 and again with WDK 7.1's
 * amd64 compiler, so the `_WIN64` half of `src/xhci_usbport.h` - three
 * structures that change size, and a packet whose every callback offset moves -
 * is checked on the build host rather than only inside a driver build, which is
 * where it was checked until then. Every expectation that differs between the
 * two therefore carries both numbers, and both come from design record 11's
 * measurements: M3 for the packet's size and its short-copy boundary, M6 for
 * the callback offsets, M4 for `USBPORT_RESOURCES`, M7 for
 * `USBPORT_ENDPOINT_PROPERTIES`. The one exception is
 * `USBPORT_SCATTER_GATHER_LIST`, which was the compiler's own layout rather
 * than a reading until 2026-09-09 and is now M8, taken off the producer in the
 * amd64 `usbport.sys` after the Windows XP x64 guest of task 21.5 refused
 * every control transfer on the four-byte shift that assumption cost.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_usbport.h"
#include "test_harness.h"

/*
 * The expectation for the architecture this suite was compiled for. Both
 * columns are hand-typed from the reading that produced them; neither is
 * computed from the other, and neither is taken from the declaration under
 * test.
 */
#ifdef _WIN64
#define BY_ARCH(x86Value, amd64Value) (amd64Value)
#else
#define BY_ARCH(x86Value, amd64Value) (x86Value)
#endif

/*
 * Offset of a registration-packet field against its hand-typed expectation,
 * for the fields that do not move: the ten leading data fields are ULONGs on
 * both architectures.
 */
#define PACKET_OFFSET(field, expected) \
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, field), (expected), \
             "packet offset of " #field)

/*
 * A callback slot, whose amd64 expectation is M6's widening map applied to the
 * hand-typed x86 one: f(X) = 0x28 + (X - 0x28) * 2.
 *
 * The map is the measurement here rather than a convenience. M6 extracted
 * every pointer the amd64 usbehci.sys stores into its own packet - 50 filled
 * slots running from 0x28 to 0x238 - and every one landed on f(X) of a field
 * the x86 record names. Writing 50 amd64 numbers out by hand would be the same
 * map applied by a person, with a person's transcription errors and no more
 * evidence behind it, so it is applied here instead. What keeps that honest is
 * that the map comes from the *binary* and not from the declaration this file
 * tests, and that the anchors below are hand-typed on both architectures, so
 * the map has independently stated fixed points at both ends and at every
 * group boundary in between.
 */
#ifdef _WIN64
#define PACKET_WIDENED(x86Offset) (0x28 + ((x86Offset) - 0x28) * 2)
#else
#define PACKET_WIDENED(x86Offset) (x86Offset)
#endif

#define PACKET_MAPPED_OFFSET(field, x86Expected) \
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, field), \
             PACKET_WIDENED(x86Expected), "packet offset of " #field)

/*
 * An anchor: a callback slot whose amd64 offset design record 11 states
 * literally, rather than one this file maps. The anchor set is exactly that
 * subset - M6's first filled slot (0x28), the StartController slot it names
 * (0x48), the sixteen service-block offsets it lists in full (0x1A0 through
 * 0x218), its first tail slot (0x220) and its last filled slot (0x238), and
 * M3's short-copy boundary (0x230) and packet size (0x250, which is what puts
 * the last Reserved field at 0x248). They are the boundaries
 * src/xhci_usbport.h pins, for the same reason: a mapped field is only as good
 * as the anchor it is measured from.
 */
#define PACKET_ANCHOR_OFFSET(field, x86Expected, amd64Expected) \
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, field), \
             BY_ARCH(x86Expected, amd64Expected), "packet offset of " #field)

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
    /* The hinge: the first pointer, and the same offset on both. */
    PACKET_ANCHOR_OFFSET(OpenEndpoint, 0x28, 0x28);
    PACKET_MAPPED_OFFSET(ReopenEndpoint, 0x2C);
    PACKET_MAPPED_OFFSET(QueryEndpointRequirements, 0x30);
    PACKET_MAPPED_OFFSET(CloseEndpoint, 0x34);
    PACKET_ANCHOR_OFFSET(StartController, 0x38, 0x48);
    PACKET_MAPPED_OFFSET(StopController, 0x3C);
    PACKET_MAPPED_OFFSET(SuspendController, 0x40);
    PACKET_MAPPED_OFFSET(ResumeController, 0x44);
    PACKET_MAPPED_OFFSET(InterruptService, 0x48);
    PACKET_MAPPED_OFFSET(InterruptDpc, 0x4C);
    PACKET_MAPPED_OFFSET(SubmitTransfer, 0x50);
    PACKET_MAPPED_OFFSET(SubmitIsoTransfer, 0x54);
    PACKET_MAPPED_OFFSET(AbortTransfer, 0x58);
    PACKET_MAPPED_OFFSET(GetEndpointState, 0x5C);
    PACKET_MAPPED_OFFSET(SetEndpointState, 0x60);
    PACKET_MAPPED_OFFSET(PollEndpoint, 0x64);
    PACKET_MAPPED_OFFSET(CheckController, 0x68);
    PACKET_MAPPED_OFFSET(Get32BitFrameNumber, 0x6C);
    PACKET_MAPPED_OFFSET(InterruptNextSOF, 0x70);
    PACKET_MAPPED_OFFSET(EnableInterrupts, 0x74);
    PACKET_MAPPED_OFFSET(DisableInterrupts, 0x78);
    PACKET_MAPPED_OFFSET(PollController, 0x7C);
    PACKET_MAPPED_OFFSET(SetEndpointDataToggle, 0x80);
    PACKET_MAPPED_OFFSET(GetEndpointStatus, 0x84);
    PACKET_MAPPED_OFFSET(SetEndpointStatus, 0x88);
    PACKET_MAPPED_OFFSET(ResetController, 0x8C);
}

static void test_packet_roothub_callbacks(void)
{
    PACKET_MAPPED_OFFSET(RH_GetRootHubData, 0x90);
    PACKET_MAPPED_OFFSET(RH_GetStatus, 0x94);
    PACKET_MAPPED_OFFSET(RH_GetPortStatus, 0x98);
    PACKET_MAPPED_OFFSET(RH_GetHubStatus, 0x9C);
    PACKET_MAPPED_OFFSET(RH_SetFeaturePortReset, 0xA0);
    PACKET_MAPPED_OFFSET(RH_SetFeaturePortPower, 0xA4);
    PACKET_MAPPED_OFFSET(RH_SetFeaturePortEnable, 0xA8);
    PACKET_MAPPED_OFFSET(RH_SetFeaturePortSuspend, 0xAC);
    PACKET_MAPPED_OFFSET(RH_ClearFeaturePortEnable, 0xB0);
    PACKET_MAPPED_OFFSET(RH_ClearFeaturePortPower, 0xB4);
    PACKET_MAPPED_OFFSET(RH_ClearFeaturePortSuspend, 0xB8);
    PACKET_MAPPED_OFFSET(RH_ClearFeaturePortEnableChange, 0xBC);
    PACKET_MAPPED_OFFSET(RH_ClearFeaturePortConnectChange, 0xC0);
    PACKET_MAPPED_OFFSET(RH_ClearFeaturePortResetChange, 0xC4);
    PACKET_MAPPED_OFFSET(RH_ClearFeaturePortSuspendChange, 0xC8);
    PACKET_MAPPED_OFFSET(RH_ClearFeaturePortOvercurrentChange, 0xCC);
    PACKET_MAPPED_OFFSET(RH_DisableIrq, 0xD0);
    PACKET_MAPPED_OFFSET(RH_EnableIrq, 0xD4);
}

static void test_packet_service_block(void)
{
    PACKET_MAPPED_OFFSET(StartSendOnePacket, 0xD8);
    PACKET_MAPPED_OFFSET(EndSendOnePacket, 0xDC);
    PACKET_MAPPED_OFFSET(PassThru, 0xE0);

    /*
     * usbport writes exactly these 16 words and nothing else before copying.
     * M6 read the amd64 block's sixteen offsets out of the writes themselves
     * (0x1A0 to 0x218, at RVAs 0x221FE-0x222E1), so the two ends of it are
     * anchors rather than mapped.
     */
    PACKET_ANCHOR_OFFSET(UsbPortDbgPrint, 0xE4, 0x1A0);
    PACKET_MAPPED_OFFSET(UsbPortTestDebugBreak, 0xE8);
    PACKET_MAPPED_OFFSET(UsbPortAssertFailure, 0xEC);
    PACKET_MAPPED_OFFSET(UsbPortGetMiniportRegistryKeyValue, 0xF0);
    PACKET_MAPPED_OFFSET(UsbPortInvalidateRootHub, 0xF4);
    PACKET_MAPPED_OFFSET(UsbPortInvalidateEndpoint, 0xF8);
    PACKET_MAPPED_OFFSET(UsbPortCompleteTransfer, 0xFC);
    PACKET_MAPPED_OFFSET(UsbPortCompleteIsoTransfer, 0x100);
    PACKET_MAPPED_OFFSET(UsbPortLogEntry, 0x104);
    PACKET_MAPPED_OFFSET(UsbPortGetMappedVirtualAddress, 0x108);
    PACKET_MAPPED_OFFSET(UsbPortRequestAsyncCallback, 0x10C);
    PACKET_MAPPED_OFFSET(UsbPortReadWriteConfigSpace, 0x110);
    PACKET_MAPPED_OFFSET(UsbPortWait, 0x114);
    PACKET_MAPPED_OFFSET(UsbPortInvalidateController, 0x118);
    PACKET_MAPPED_OFFSET(UsbPortBugCheck, 0x11C);
    PACKET_ANCHOR_OFFSET(UsbPortNotifyDoubleBuffer, 0x120, 0x218);

    /*
     * DriverEntry walks the service block as 16 consecutive words to count how
     * many usbport filled in. That walk is only meaningful while the block
     * really is contiguous and really is 16 long, so state it here rather than
     * leaving it as an assumption inside a diagnostic.
     */
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET,
                            UsbPortNotifyDoubleBuffer) -
                 XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, UsbPortDbgPrint),
             15 * BY_ARCH(4, 8),
             "service block is 16 contiguous pointers");
}

static void test_packet_tail(void)
{
    PACKET_ANCHOR_OFFSET(RebalanceEndpoint, 0x124, 0x220);
    PACKET_MAPPED_OFFSET(FlushInterrupts, 0x128);
    PACKET_ANCHOR_OFFSET(RH_ChirpRootPort, 0x12C, 0x230);
    /* The last slot the amd64 usbehci.sys fills, and where M6's walk stops. */
    PACKET_ANCHOR_OFFSET(TakePortControl, 0x130, 0x238);
    PACKET_MAPPED_OFFSET(Reserved4, 0x134);
    /*
     * The two canaries are ULONG_PTR, not ULONG, and this is the check that
     * says so: four-byte canaries pack at 0x240/0x244 and end the structure at
     * 0x248, eight bytes short of what usbport copies into it (design record
     * 11, M3 and M6).
     */
    PACKET_ANCHOR_OFFSET(Reserved5, 0x138, 0x248);

    /*
     * Since task 22.5 the structure continues past here into the Version 300
     * tier, so the number every NT 5.x and 9x usbport copies at Version 200 is
     * the offset of the tier's first field rather than sizeof.
     */
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, ExtraCommonBufferCount),
             BY_ARCH(316, 0x250),
             "packet bytes copied at 200 <= Version < 300");

    /*
     * The difference between the two NT 5.x copy sizes must be exactly the
     * four tail fields - that is how a Version < 200 miniport ends up with
     * RH_ChirpRootPort ungated. Four pointer-sized fields, so 16 bytes on x86
     * and 32 on amd64 (0x250 - 0x230).
     */
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, ExtraCommonBufferCount) -
                 XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, RH_ChirpRootPort),
             BY_ARCH(16, 32),
             "the short copy stops exactly before the tail four");
}

/*
 * The Version 300 tier (task 22.5), read slot by slot out of the four NT 6.x
 * usbport.sys / usbehci.sys pairs on 2026-09-11 with Microsoft's public
 * symbols loaded (docs/usb-xhci-info/usbport-miniport-abi.md, "The Version 300
 * tier, slot by slot"). Both columns are hand-typed from that table; the amd64
 * column is NOT M6's f(X) - the tier has its own map, `0x250 + (X - 0x13C)`
 * for the twelve ULONGs and `0x280 + (X - 0x16C) * 2` for the pointers - and
 * the point of typing every row is that an arithmetic slip in the declaration
 * lands on a slot usbport NULL-checks, which is silent, or on InterruptDpcEx,
 * which is the one slot the whole tier exists for.
 */
static void test_packet_version_300_tier(void)
{
    CHECK_EQ(USBPORT_NT6_MINIPORT_INTERFACE_VERSION, 300,
             "the NT 6.x interface version presented");
    CHECK_EQ(USBPORT_DPC_EX_PORT_CHANGE, 2, "InterruptDpcEx port-change bit");
    CHECK_EQ(USBPORT_DPC_EX_TRANSFER_WORK, 1, "InterruptDpcEx transfer bit");

    /* The twelve ULONGs: count, eight sizes, three context sizes. */
    PACKET_ANCHOR_OFFSET(ExtraCommonBufferCount, 0x13C, 0x250);
    PACKET_ANCHOR_OFFSET(ExtraCommonBufferSize, 0x140, 0x254);
    CHECK_EQ(sizeof(((USBPORT_REGISTRATION_PACKET *)0)->ExtraCommonBufferSize),
             8 * 4, "eight ULONG sizes, one per extra common buffer");
    PACKET_ANCHOR_OFFSET(HsbControllerContextSize, 0x160, 0x274);
    PACKET_ANCHOR_OFFSET(HsbTtContextSize, 0x164, 0x278);
    PACKET_ANCHOR_OFFSET(HsbEndpointContextSize, 0x168, 0x27C);

    /* The 29 pointer slots, in order. */
    PACKET_ANCHOR_OFFSET(ReleasePortControl, 0x16C, 0x280);
    PACKET_ANCHOR_OFFSET(ReadCfgFlag, 0x170, 0x288);
    PACKET_ANCHOR_OFFSET(SetWakeOnConnect, 0x174, 0x290);
    PACKET_ANCHOR_OFFSET(InterruptDpcEx, 0x178, 0x298);
    PACKET_ANCHOR_OFFSET(NotifyTransferQueueState, 0x17C, 0x2A0);
    PACKET_ANCHOR_OFFSET(CheckHwSync, 0x180, 0x2A8);
    PACKET_ANCHOR_OFFSET(UsbxInitHsbTransactionTranslator, 0x184, 0x2B0);
    PACKET_ANCHOR_OFFSET(UsbxInitHsbController, 0x188, 0x2B8);
    PACKET_ANCHOR_OFFSET(UsbxInitHsbEndpoint, 0x18C, 0x2C0);
    PACKET_ANCHOR_OFFSET(UsbxAllocateBandwidth, 0x190, 0x2C8);
    PACKET_ANCHOR_OFFSET(UsbxFreeBandwidth, 0x194, 0x2D0);
    PACKET_ANCHOR_OFFSET(Unreferenced198, 0x198, 0x2D8);
    PACKET_ANCHOR_OFFSET(UsbxPokeEndpoint, 0x19C, 0x2E0);
    PACKET_ANCHOR_OFFSET(UsbxOpenEndpoint, 0x1A0, 0x2E8);
    PACKET_ANCHOR_OFFSET(UsbxQueryBandwidthData, 0x1A4, 0x2F0);
    PACKET_ANCHOR_OFFSET(UsbxQueryTtBandwidthData, 0x1A8, 0x2F8);
    PACKET_ANCHOR_OFFSET(UsbxQueryEpBandwidthData, 0x1AC, 0x300);
    PACKET_ANCHOR_OFFSET(UsbPortRequestAsyncCallbackEx, 0x1B0, 0x308);
    PACKET_ANCHOR_OFFSET(UsbPortCancelAsyncCallback, 0x1B4, 0x310);
    PACKET_ANCHOR_OFFSET(Unreferenced1B8, 0x1B8, 0x318);
    PACKET_ANCHOR_OFFSET(Unreferenced1BC, 0x1BC, 0x320);
    PACKET_ANCHOR_OFFSET(Unreferenced1C0, 0x1C0, 0x328);
    PACKET_ANCHOR_OFFSET(CreateDeviceData, 0x1C4, 0x330);
    PACKET_ANCHOR_OFFSET(DeleteDeviceData, 0x1C8, 0x338);
    PACKET_ANCHOR_OFFSET(DbgFreeEndpoint, 0x1CC, 0x340);
    PACKET_ANCHOR_OFFSET(Unreferenced1D0, 0x1D0, 0x348);
    PACKET_ANCHOR_OFFSET(Unreferenced1D4, 0x1D4, 0x350);
    PACKET_ANCHOR_OFFSET(HaltController, 0x1D8, 0x358);
    PACKET_ANCHOR_OFFSET(Get32BitMicroFrameNumber, 0x1DC, 0x360);

    /* What an NT 6.x usbport copies at 300 <= Version < 310. */
    CHECK_EQ(sizeof(USBPORT_REGISTRATION_PACKET), BY_ARCH(0x1E0, 0x368),
             "packet bytes copied at Version 300");

    /* The two maps, stated as arithmetic on the declaration. */
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, ReleasePortControl) -
                 XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, ExtraCommonBufferCount),
             12 * 4, "twelve ULONGs before the first 300-tier pointer");
    CHECK_EQ(sizeof(USBPORT_REGISTRATION_PACKET) -
                 XHCI_OFFSET_OF(USBPORT_REGISTRATION_PACKET, ReleasePortControl),
             29 * BY_ARCH(4, 8), "29 pointer slots to the end of the tier");
}

/* ------------------------------------------------------------------ */
/* 2. Support structures                                               */
/* ------------------------------------------------------------------ */

/*
 * Three of these change size on amd64 and the rest do not, and which is which
 * is a fact about the declarations rather than a convention: a structure whose
 * every member is a ULONG or narrower is laid out identically by both
 * compilers, so USBPORT_TRANSFER_PARAMETERS, XHCI_SETUP_PACKET,
 * USBPORT_ENDPOINT_REQUIREMENTS, the two isochronous blocks and
 * USBPORT_ROOT_HUB_DATA carry one number each below.
 * USBPORT_SCATTER_GATHER_ELEMENT is the trap in that rule and cost task 21.5
 * a guest: its size is 24 on both, so it looks like one of them, but the real
 * amd64 element has eight bytes between the address and the length rather
 * than four, and the fields after the address move (M8).
 * The three that move are the three with a pointer-sized member:
 * USBPORT_RESOURCES (InterruptAffinity), USBPORT_ENDPOINT_PROPERTIES
 * (BufferVA) and USBPORT_SCATTER_GATHER_LIST (CurrentVa and MappedSystemVa).
 */

/*
 * The amd64 column is M4's table, every row of it read off the amd64
 * usbehci.sys's StartController: InterruptAffinity is a KAFFINITY and widens,
 * which is the whole of the eight-byte difference, and StartPA does NOT widen -
 * it is loaded as a dword at 0x40, so the common-buffer physical address is a
 * ULONG on both architectures and nothing after it shifts.
 */
static void test_resources(void)
{
    CHECK_EQ(sizeof(USBPORT_RESOURCES), BY_ARCH(52, 0x48),
             "USBPORT_RESOURCES size");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, ResourcesTypes),
             BY_ARCH(0x00, 0x00), "resources ResourcesTypes");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, HcFlavor),
             BY_ARCH(0x04, 0x04), "resources HcFlavor");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, InterruptVector),
             BY_ARCH(0x08, 0x08), "resources InterruptVector");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, InterruptLevel),
             BY_ARCH(0x0C, 0x0C), "resources InterruptLevel");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, InterruptAffinity),
             BY_ARCH(0x10, 0x10), "resources InterruptAffinity");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, ShareVector),
             BY_ARCH(0x14, 0x18), "resources ShareVector");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, InterruptMode),
             BY_ARCH(0x18, 0x1C), "resources InterruptMode");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, Reserved),
             BY_ARCH(0x1C, 0x20), "resources Reserved");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, ResourceBase),
             BY_ARCH(0x20, 0x28), "resources ResourceBase (mapped BAR0)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, IoSpaceLength),
             BY_ARCH(0x24, 0x30), "resources IoSpaceLength");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, StartVA),
             BY_ARCH(0x28, 0x38), "resources StartVA (common buffer)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, StartPA),
             BY_ARCH(0x2C, 0x40),
             "resources StartPA (common buffer) - 4 bytes on both");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, LegacySupport),
             BY_ARCH(0x30, 0x44), "resources LegacySupport (the one OUT field)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_RESOURCES, IsChirpHandled),
             BY_ARCH(0x31, 0x45), "resources IsChirpHandled");

    /* StartController dumps the struct as whole words; it has to divide. */
    CHECK_EQ(sizeof(USBPORT_RESOURCES) % 4, 0,
             "resources dumps evenly as ULONGs");
}

/*
 * The amd64 column is M7, read off the amd64 usbehci.sys on 2026-09-09 after
 * the compile scout found that this structure changes size and that none of
 * M1-M6 had covered it. BufferVA is the one member that widens; four bytes of
 * padding appear at 0x1C to align it, so every field below it keeps its x86
 * offset and every field from it on sits exactly 8 higher - confirmed at both
 * ends rather than at one point. Six of these offsets and the size were each
 * read from an instruction; the rest follow from the two anchors that bracket
 * them.
 */
static void test_endpoint_properties(void)
{
    CHECK_EQ(sizeof(USBPORT_ENDPOINT_PROPERTIES), BY_ARCH(64, 0x48),
             "USBPORT_ENDPOINT_PROPERTIES size");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, DeviceAddress),
             BY_ARCH(0x00, 0x00), "properties DeviceAddress");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, EndpointAddress),
             BY_ARCH(0x02, 0x02), "properties EndpointAddress");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, TotalMaxPacketSize),
             BY_ARCH(0x04, 0x04),
             "properties TotalMaxPacketSize (corrected EP0 MPS0)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, Period),
             BY_ARCH(0x06, 0x06), "properties Period");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, DeviceSpeed),
             BY_ARCH(0x08, 0x08), "properties DeviceSpeed");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, UsbBandwidth),
             BY_ARCH(0x0C, 0x0C), "properties UsbBandwidth");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, ScheduleOffset),
             BY_ARCH(0x10, 0x10), "properties ScheduleOffset");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, TransferType),
             BY_ARCH(0x14, 0x14), "properties TransferType");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, Direction),
             BY_ARCH(0x18, 0x18), "properties Direction");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferVA),
             BY_ARCH(0x1C, 0x20),
             "properties BufferVA (per-endpoint common buffer) - the one that moves");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferPA),
             BY_ARCH(0x20, 0x28), "properties BufferPA - 4 bytes on both");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, BufferLength),
             BY_ARCH(0x24, 0x2C), "properties BufferLength");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, MaxTransferSize),
             BY_ARCH(0x2C, 0x34), "properties MaxTransferSize");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, HubAddr),
             BY_ARCH(0x30, 0x38), "properties HubAddr (TT hub, or 0xFFFF)");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, PortNumber),
             BY_ARCH(0x32, 0x3A), "properties PortNumber");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES,
                            InterruptScheduleMask), BY_ARCH(0x34, 0x3C),
             "properties InterruptScheduleMask");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, SplitCompletionMask),
             BY_ARCH(0x35, 0x3D), "properties SplitCompletionMask");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES,
                            TransactionPerMicroframe), BY_ARCH(0x36, 0x3E),
             "properties TransactionPerMicroframe");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_ENDPOINT_PROPERTIES, MaxPacketSize),
             BY_ARCH(0x38, 0x40), "properties MaxPacketSize");

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
    /*
     * These two move on amd64 and the element's size does not, which is why
     * the size alone could never have caught the defect below: eight bytes
     * separate the address from the length there, against four here.
     */
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_ELEMENT, SgTransferLength),
             BY_ARCH(0x0C, 0x10), "SG element length");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_ELEMENT, SgOffset),
             BY_ARCH(0x10, 0x14),
             "SG element offset within the transfer buffer");

    /*
     * **M8, 2026-09-09, and it is the row that was wrong.** The amd64 column
     * was the compiler's layout rather than a reading until the Windows XP x64
     * guest of roadmap task 21.5 refused every control transfer with
     * XHCI_XFER_SG_HIGH_ADDRESS: sizeof was right at 0x50 and SgElement[] was
     * not, because the real element type is 8-aligned - its first member is a
     * PHYSICAL_ADDRESS - so the array starts at 0x20, while a declaration made
     * of ULONGs aligns to 4 and put it at 0x1C. Read off the producer in NT
     * 5.2 amd64 usbport.sys at RVA 0xF468 (`lea rdi,[rsi+118h]` ...
     * `lea rbx,[rdi+20h]`, `add rbx,18h`); design record 11 section 5 M8.
     */
    CHECK_EQ(sizeof(USBPORT_SCATTER_GATHER_LIST), BY_ARCH(64, 0x50),
             "SG list size with two elements");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_LIST, SgElementCount),
             BY_ARCH(0x0C, 0x18), "SG list element count");
    CHECK_EQ(XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_LIST, SgElement),
             BY_ARCH(0x10, 0x20), "SG list first element");
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
    /*
     * *(Three checks that each `sizeof` is greater than zero stood here. A C
     * structure with at least one member cannot have a size of zero, so all
     * three were true by construction and could not have failed - the
     * 2026-09-07 audit's G13. The property they were reaching for is that
     * usbport allocates and zeroes exactly `MiniPortExtensionSize` bytes, and
     * that is not a fact about `sizeof` at all: it is about what DriverEntry
     * publishes and what the callbacks then find, which `test_init.c` drives
     * against a model that allocates exactly the published size and poisons
     * everything either side of it.)*
     *
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
    test_packet_version_300_tier();
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
