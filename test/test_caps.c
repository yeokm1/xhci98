/*
 * test_caps.c - host tests for the extended-capability parser and port
 * classification (src/xhci_caps.c).
 *
 * The capability chain is a linked list the driver walks inside a memory
 * window it must not leave, on a controller it has never seen. The interesting
 * inputs are therefore the malformed ones - a NEXT pointer that leaves the
 * BAR, a group claiming ports the controller does not have, a PSI count
 * promising DWORDs past the mapping - and no real controller produces those on
 * request. Here the chain is an array, so every one of them is a test.
 *
 * The reader also counts its reads: a parser that walked out of the mapped
 * window would still return plausible answers against a large enough array, so
 * "never read outside the declared length" is checked directly rather than
 * inferred from the results.
 *
 * Per docs/contributing/design/03-host-unit-tests.md, expected values are transcribed
 * by hand from docs/usb-xhci-info/xhci-data-structures.md section 6 and the classification
 * table in docs/usb-xhci-info/xhci-programming.md - never produced by the code under test.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include "../src/xhci.h"

static int failures;
static int checks;

#define CHECK(cond, what) check_impl((cond), (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s:%d: %s\n", "test_caps.c", line, what);
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
               "test_caps.c", line, what, got, got, want, want);
    }
}

/* ------------------------------------------------------------------ */
/* A synthetic BAR0 window                                             */
/* ------------------------------------------------------------------ */

#define BAR_DWORDS 512                      /* 2 KB of register space */

static ULONG bar[BAR_DWORDS];
static ULONG barLimit;                      /* what the parser was told  */
static int barOutOfWindow;                  /* reads past that limit     */
static int barMisaligned;

static ULONG bar_read(PVOID context, ULONG byteOffset)
{
    ULONG index;

    (void)context;
    if ((byteOffset & 3) != 0) {
        barMisaligned++;
    }
    if (byteOffset >= barLimit || (barLimit - byteOffset) < 4) {
        barOutOfWindow++;
    }
    index = byteOffset / 4;
    if (index >= BAR_DWORDS) {
        return 0xFFFFFFFFUL;
    }
    return bar[index];
}

static void bar_reset(ULONG mappedBytes)
{
    ULONG i;

    for (i = 0; i < BAR_DWORDS; i++) {
        bar[i] = 0;
    }
    barLimit = mappedBytes;
    barOutOfWindow = 0;
    barMisaligned = 0;
}

/* Capability header: ID 7:0, NEXT 15:8 (in DWORDs, relative to here). */
static void put_legacy(ULONG dw, ULONG next)
{
    bar[dw] = 1UL | (next << 8) | XHCI_USBLEGSUP_BIOS_OWNED;
    bar[dw + 1] = 0;
}

static void put_debug(ULONG dw, ULONG next)
{
    bar[dw] = 10UL | (next << 8);
}

static void put_protocol(ULONG dw, ULONG next, ULONG major, ULONG minor,
                         ULONG portOffset, ULONG portCount, ULONG slotType,
                         const ULONG *psi, ULONG psic)
{
    ULONG i;

    bar[dw] = 2UL | (next << 8) | (minor << 16) | (major << 24);
    bar[dw + 1] = 0x20425355UL;     /* "USB " */
    bar[dw + 2] = portOffset | (portCount << 8) | (psic << 28);
    bar[dw + 3] = slotType;
    for (i = 0; i < psic; i++) {
        bar[dw + 4 + i] = psi[i];
    }
}

/*
 * Protocol Speed ID DWORDs: PSIV 3:0, PSIE 5:4 (0 b/s, 1 Kb/s, 2 Mb/s,
 * 3 Gb/s), PSIM 31:16. Hand-built from spec 7.2.2.1.2.
 */
#define PSI_LS 0x05DC0012UL     /* PSIV 2, Kb/s, 1500  -> Low Speed   */
#define PSI_FS 0x000C0021UL     /* PSIV 1, Mb/s, 12    -> Full Speed  */
#define PSI_HS 0x01E00023UL     /* PSIV 3, Mb/s, 480   -> High Speed  */
#define PSI_SS 0x00050034UL     /* PSIV 4, Gb/s, 5     -> SuperSpeed  */

/* ------------------------------------------------------------------ */
/* 1. The field macros the parser is built from                        */
/* ------------------------------------------------------------------ */

static void test_field_macros(void)
{
    CHECK_EQ(XHCI_XECP_ID(0x02040302UL), 2, "capability ID is bits 7:0");
    CHECK_EQ(XHCI_XECP_NEXT(0x02040302UL), 3, "NEXT is bits 15:8");
    CHECK_EQ(XHCI_PROTOCOL_MINOR(0x02040302UL), 4, "minor revision 23:16");
    CHECK_EQ(XHCI_PROTOCOL_MAJOR(0x02040302UL), 2, "major revision 31:24");

    CHECK_EQ(XHCI_PROTOCOL_PORT_OFFSET(0x30000E01UL), 1, "port offset 7:0");
    CHECK_EQ(XHCI_PROTOCOL_PORT_COUNT(0x30000E01UL), 14, "port count 15:8");
    CHECK_EQ(XHCI_PROTOCOL_PSIC(0x30000E01UL), 3, "PSIC is bits 31:28");
    CHECK_EQ(XHCI_PROTOCOL_SLOT_TYPE(0x00000009UL), 9, "slot type 4:0");

    CHECK_EQ(XHCI_PSI_PSIV(PSI_HS), 3, "PSIV 3:0");
    CHECK_EQ(XHCI_PSI_PSIE(PSI_HS), 2, "PSIE 5:4");
    CHECK_EQ(XHCI_PSI_PSIM(PSI_HS), 480, "PSIM 31:16");
    CHECK_EQ(XHCI_PSI_PSIE(PSI_LS), 1, "Low Speed is expressed in Kb/s");
    CHECK_EQ(XHCI_PSI_PSIM(PSI_LS), 1500, "1500 Kb/s");

    /* xECP is a DWORD offset; the capability list starts at BAR0 + xECP * 4. */
    CHECK_EQ(XHCI_HCCPARAMS1_XECP(0x01000284UL), 0x0100UL, "xECP 31:16");
    CHECK_EQ(XHCI_HCCPARAMS1_CSZ(0x01000284UL), 1, "CSZ is bit 2");
    CHECK_EQ(XHCI_HCCPARAMS1_PPC(0x01000284UL), 0, "PPC is bit 3");
    CHECK_EQ(XHCI_HCCPARAMS1_AC64(0x01000284UL), 0, "AC64 is bit 0");
    CHECK_EQ(XHCI_CONTEXT_SIZE_FROM_CSZ(0x01000284UL), 64, "CSZ 1 -> 64 bytes");
    CHECK_EQ(XHCI_CONTEXT_SIZE_FROM_CSZ(0x01000280UL), 32, "CSZ 0 -> 32 bytes");

    CHECK_EQ(XHCI_HCSPARAMS1_MAXSLOTS(0x0F000440UL), 0x40UL, "MaxSlots 7:0");
    CHECK_EQ(XHCI_HCSPARAMS1_MAXINTRS(0x0F000440UL), 4, "MaxIntrs 18:8");
    CHECK_EQ(XHCI_HCSPARAMS1_MAXPORTS(0x0F000440UL), 15, "MaxPorts 31:24");

    /*
     * Max Scratchpad Buffers is the split field that is easy to read
     * backwards: Hi is bits 25:21 and Lo is bits 31:27, so 34 buffers is
     * Hi = 1, Lo = 2 -> (1 << 21) | (2 << 27) = 0x10200000.
     */
    CHECK_EQ(XHCI_HCSPARAMS2_MAXSCRATCHPAD(0x10200000UL), 34,
             "34 scratchpad buffers decoded from the split field");
    CHECK_EQ(XHCI_HCSPARAMS2_MAXSCRATCHPAD(0x00000000UL), 0, "no scratchpad");
    CHECK_EQ(XHCI_HCSPARAMS2_MAXSCRATCHPAD(0xF8000000UL), 31,
             "the high five bits alone are the low half");
    CHECK_EQ(XHCI_HCSPARAMS2_MAXSCRATCHPAD(0x03E00000UL), 992,
             "and bits 25:21 alone are the high half");
    CHECK_EQ(XHCI_HCSPARAMS2_ERSTMAX(0x000000F0UL), 15, "ERST Max 7:4");
}

/* ------------------------------------------------------------------ */
/* 2. The shapes real controllers present                              */
/* ------------------------------------------------------------------ */

/*
 * The fleet's Intel PCH shape (docs/usb-xhci-info/xhci-programming.md worked example):
 * 14 USB 2.0 logical ports and 4 USB 3.0 ones for 14 physical connectors, so
 * the last four USB 2.0 ports are the companions of the four USB 3.0 ports.
 * Both fleet machines advertise a PSI table on the USB 2.0 capability.
 */
static void test_intel_shape(void)
{
    static const ULONG psi2[3] = { PSI_FS, PSI_LS, PSI_HS };
    static const ULONG psi3[1] = { PSI_SS };
    XHCI_PORT_MAP map;
    ULONG i;
    ULONG value;

    bar_reset(0x800);
    put_legacy(0x100, 2);
    put_protocol(0x102, 8, 2, 0, 1, 14, 9, psi2, 3);
    put_protocol(0x10A, 0, 3, 0, 15, 4, 10, psi3, 1);

    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 18, &map),
             XHCI_CAPS_OK, "Intel-shaped chain parsed");
    CHECK_EQ(barOutOfWindow, 0, "no read left the mapped window");
    CHECK_EQ(barMisaligned, 0, "every read was DWORD aligned");

    CHECK_EQ(map.PortCount, 18, "18 logical ports");
    CHECK_EQ(map.ProtocolCount, 2, "two protocol groups");
    CHECK_EQ(map.LegacySupportOffset, 0x400UL,
             "USBLEGSUP found at its byte offset");
    CHECK_EQ(map.DebugCapabilityOffset, 0, "no debug capability");

    CHECK_EQ(map.Protocols[0].Major, 2, "first group is USB 2");
    CHECK_EQ(map.Protocols[0].PortOffset, 1, "starting at port 1");
    CHECK_EQ(map.Protocols[0].PortCount, 14, "14 ports");
    CHECK_EQ(map.Protocols[0].SlotType, 9, "USB2 slot type");
    CHECK_EQ(map.Protocols[0].PsiCount, 3, "three PSI entries retained");
    CHECK_EQ(map.Protocols[0].Psi[2], PSI_HS, "PSI DWORDs retained verbatim");
    CHECK_EQ(map.Protocols[1].Major, 3, "second group is USB 3");
    CHECK_EQ(map.Protocols[1].SlotType, 10, "USB3 slot type");

    /* Ports 1-10 have no USB 3.0 companion; 11-14 do. */
    for (i = 1; i <= 10; i++) {
        CHECK_EQ(XhciPortClass(&map, i), XHCI_PORT_CLASS_USB2_ONLY,
                 "USB 2.0-only port");
        CHECK_EQ(map.Companion[i - 1], 0, "and no companion");
        CHECK_EQ(XhciPortIsManaged(&map, i), 1, "managed");
    }
    for (i = 11; i <= 14; i++) {
        CHECK_EQ(XhciPortClass(&map, i), XHCI_PORT_CLASS_USB2_COMPANION,
                 "USB 2.0 companion port");
        CHECK_EQ(map.Companion[i - 1], i + 4, "paired with its USB 3.0 half");
        CHECK_EQ(XhciPortIsManaged(&map, i), 1, "managed");
    }
    for (i = 15; i <= 18; i++) {
        CHECK_EQ(XhciPortClass(&map, i), XHCI_PORT_CLASS_USB3_COMPANION,
                 "USB 3.0 companion port");
        CHECK_EQ(map.Companion[i - 1], i - 4, "paired back");
        CHECK_EQ(XhciPortIsManaged(&map, i), 0,
                 "USB 3.x ports are never managed");
    }
    CHECK_EQ(map.ManagedPortCount, 14, "all 14 USB 2.0 ports are managed");

    CHECK_EQ(XhciPortSlotType(&map, 1, &value), XHCI_CAPS_OK, "slot type read");
    CHECK_EQ(value, 9, "USB2 port carries the USB2 slot type");
    CHECK_EQ(XhciPortSlotType(&map, 15, &value), XHCI_CAPS_OK, "slot type read");
    CHECK_EQ(value, 10, "USB3 port carries the USB3 slot type");
}

/*
 * QEMU's qemu-xhci: one USB 2.0 port per USB 3.0 port, no PSI tables. This is
 * the shape both target VMs actually run against, so it is not an edge case.
 */
static void test_qemu_shape(void)
{
    XHCI_PORT_MAP map;
    ULONG i;

    bar_reset(0x800);
    put_protocol(0x100, 4, 2, 0, 1, 4, 9, NULL, 0);
    put_protocol(0x104, 0, 3, 0, 5, 4, 10, NULL, 0);

    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 8, &map),
             XHCI_CAPS_OK, "QEMU-shaped chain parsed");
    CHECK_EQ(barOutOfWindow, 0, "no read left the mapped window");
    CHECK_EQ(map.LegacySupportOffset, 0,
             "QEMU exposes no legacy support capability");
    CHECK_EQ(map.Protocols[0].PsiCount, 0, "no PSI table");

    for (i = 1; i <= 4; i++) {
        CHECK_EQ(XhciPortClass(&map, i), XHCI_PORT_CLASS_USB2_COMPANION,
                 "every USB 2.0 port is a companion when the counts match");
        CHECK_EQ(map.Companion[i - 1], i + 4, "paired");
    }
    for (i = 5; i <= 8; i++) {
        CHECK_EQ(XhciPortClass(&map, i), XHCI_PORT_CLASS_USB3_COMPANION,
                 "USB 3.0 companion");
    }
    CHECK_EQ(map.ManagedPortCount, 4, "four managed ports");
}

/* A USB 2.0-only controller: nothing to pair, everything managed. */
static void test_usb2_only_shape(void)
{
    XHCI_PORT_MAP map;
    ULONG i;

    bar_reset(0x800);
    put_protocol(0x100, 0, 2, 0, 1, 4, 0, NULL, 0);

    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 4, &map),
             XHCI_CAPS_OK, "USB 2.0-only chain parsed");
    CHECK_EQ(map.ProtocolCount, 1, "one protocol group");
    for (i = 1; i <= 4; i++) {
        CHECK_EQ(XhciPortClass(&map, i), XHCI_PORT_CLASS_USB2_ONLY, "USB2 only");
        CHECK_EQ(map.Companion[i - 1], 0, "no companion");
    }
    CHECK_EQ(map.ManagedPortCount, 4, "all four managed");
}

/*
 * More USB 3.x ports than USB 2.0 ports: the surplus are orphans - physical
 * connectors with no USB 2.0 path, which this driver cannot serve at all
 * (AGENTS.md, "Port Strategy"). Naming them is the point; they must not look
 * like ordinary unmanaged ports.
 */
static void test_orphan_usb3(void)
{
    XHCI_PORT_MAP map;
    ULONG i;

    bar_reset(0x800);
    put_protocol(0x100, 4, 2, 0, 1, 2, 9, NULL, 0);
    put_protocol(0x104, 0, 3, 0, 3, 4, 10, NULL, 0);

    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 6, &map),
             XHCI_CAPS_OK, "orphan chain parsed");
    CHECK_EQ(XhciPortClass(&map, 1), XHCI_PORT_CLASS_USB2_ONLY, "port 1");
    CHECK_EQ(XhciPortClass(&map, 2), XHCI_PORT_CLASS_USB2_ONLY, "port 2");
    for (i = 3; i <= 6; i++) {
        CHECK_EQ(XhciPortClass(&map, i), XHCI_PORT_CLASS_USB3_ORPHAN,
                 "USB 3.x port with no USB 2.0 companion");
        CHECK_EQ(map.Companion[i - 1], 0, "unpaired");
        CHECK_EQ(XhciPortIsManaged(&map, i), 0, "and unmanaged");
    }
    CHECK_EQ(map.ManagedPortCount, 2, "only the two USB 2.0 ports");
}

/*
 * A well-formed chain that leaves nothing to manage: every port is USB 3.x.
 * The parser is right to accept it - it is a correct reading of a legal
 * controller - so the refusal belongs to the caller, and src/xhci_init.c makes
 * it (XHCI_CAPS_NO_MANAGED_PORTS) rather than starting a controller with no
 * port it can serve. This vector exists to pin the input that refusal keys on:
 * XHCI_CAPS_OK with a managed count of zero.
 */
static void test_no_managed_ports(void)
{
    XHCI_PORT_MAP map;
    ULONG i;

    bar_reset(0x800);
    put_protocol(0x100, 0, 3, 0, 1, 4, 10, NULL, 0);

    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 4, &map),
             XHCI_CAPS_OK, "an all-USB3 controller is a legal chain");
    CHECK_EQ(map.ManagedPortCount, 0, "and leaves this driver nothing to do");
    for (i = 1; i <= 4; i++) {
        CHECK_EQ(XhciPortClass(&map, i), XHCI_PORT_CLASS_USB3_ORPHAN,
                 "every port is an orphan");
        CHECK_EQ(XhciPortIsManaged(&map, i), 0, "unmanaged");
    }
}

/* ------------------------------------------------------------------ */
/* 3. Chains that are terminated, degenerate, or hostile               */
/* ------------------------------------------------------------------ */

static void test_chain_shapes(void)
{
    XHCI_PORT_MAP map;
    ULONG offset;

    /* No capability list at all. */
    bar_reset(0x800);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0, 0x800, 4, &map),
             XHCI_CAPS_NO_LIST, "xECP = 0 means no list");
    CHECK_EQ(map.PortCount, 4, "the map is still initialized");
    CHECK_EQ(XhciPortClass(&map, 1), XHCI_PORT_CLASS_NONE, "no port classified");
    CHECK_EQ(map.ManagedPortCount, 0, "nothing to manage");

    /* A NEXT that steps past the end of the mapping. */
    bar_reset(0x800);
    put_legacy(0x1F0, 0x10);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x1F0, 0x800, 4, &map),
             XHCI_CAPS_OUT_OF_RANGE, "NEXT leaving the BAR is refused");
    CHECK_EQ(barOutOfWindow, 0, "and the read that would have left it never ran");

    /* A capability header sitting exactly at the end. */
    bar_reset(0x404);
    put_legacy(0x100, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x404, 4, &map),
             XHCI_CAPS_OK, "a header ending exactly at the limit is in range");
    bar_reset(0x403);
    put_legacy(0x100, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x403, 4, &map),
             XHCI_CAPS_OUT_OF_RANGE, "one byte short is not");

    /* xECP pointing outside the mapping entirely. */
    bar_reset(0x400);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x400, 4, &map),
             XHCI_CAPS_OUT_OF_RANGE, "xECP past the mapping is refused");

    /*
     * All ones is the bus answering for a device that stopped decoding. A
     * chain walker that trusted it would follow a NEXT of 0xFF forever.
     */
    bar_reset(0x800);
    bar[0x100] = 0xFFFFFFFFUL;
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 4, &map),
             XHCI_CAPS_OUT_OF_RANGE, "all-ones header is refused");

    /* Unknown capability IDs are skipped, not refused. */
    bar_reset(0x800);
    bar[0x100] = 0xC0UL | (2 << 8);          /* vendor defined */
    put_debug(0x102, 2);
    put_protocol(0x104, 0, 2, 0, 1, 2, 9, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 2, &map),
             XHCI_CAPS_OK, "unknown IDs are walked past");
    CHECK_EQ(map.DebugCapabilityOffset, 0x408UL, "debug capability recorded");
    CHECK_EQ(map.ManagedPortCount, 2, "the protocol after them still parsed");

    /* Find-by-ID uses the same walk and answers before classification. */
    bar_reset(0x800);
    put_legacy(0x100, 2);
    put_protocol(0x102, 0, 2, 0, 1, 2, 9, NULL, 0);
    CHECK_EQ(XhciFindExtendedCap(bar_read, NULL, 0x100, 0x800,
                                 XHCI_XECP_ID_LEGACY, XHCI_USBLEGSUP_BYTES,
                                 &offset),
             XHCI_CAPS_OK, "USBLEGSUP found");
    CHECK_EQ(offset, 0x400UL, "at its byte offset");
    CHECK_EQ(bar[offset / 4] & XHCI_USBLEGSUP_BIOS_OWNED,
             XHCI_USBLEGSUP_BIOS_OWNED, "BIOS-owned semaphore is bit 16");
    CHECK_EQ(XhciFindExtendedCap(bar_read, NULL, 0x100, 0x800,
                                 XHCI_XECP_ID_DEBUG, 4UL, &offset),
             XHCI_CAPS_NOT_FOUND, "absent capability reports not found");
    CHECK_EQ(XhciFindExtendedCap(bar_read, NULL, 0, 0x800,
                                 XHCI_XECP_ID_LEGACY, XHCI_USBLEGSUP_BYTES,
                                 &offset),
             XHCI_CAPS_NO_LIST, "no list at all");
    CHECK_EQ(XhciFindExtendedCap(bar_read, NULL, 0x100, 0x800,
                                 XHCI_XECP_ID_LEGACY, 3UL, &offset),
             XHCI_CAPS_BAD_PARAM,
             "a span smaller than the header is a caller mistake");

    /*
     * The span check, and why the finder has one at all. A capability *header*
     * is four bytes, but USBLEGSUP is two DWORDs and the BIOS handoff both
     * reads and writes the second one. A header in the last four bytes of the
     * mapping would hand the caller an offset whose USBLEGCTLSTS write lands
     * outside the BAR - on Win2000, in whatever mapping follows it.
     *
     * Here the whole window is 0x404 bytes, so the capability at 0x400 has its
     * header inside and its body out.
     */
    bar_reset(0x404);
    put_legacy(0x100, 0);
    offset = 0xDEADBEEFUL;
    CHECK_EQ(XhciFindExtendedCap(bar_read, NULL, 0x100, 0x404,
                                 XHCI_XECP_ID_LEGACY, XHCI_USBLEGSUP_BYTES,
                                 &offset),
             XHCI_CAPS_OUT_OF_RANGE,
             "a header in the final four bytes cannot carry a two-DWORD "
             "capability");
    /* A refusal must not also publish the offset it refused. A caller that
     * ignored the status would otherwise be handed a plausible-looking one
     * and walk straight off the end of the mapping - the exact failure the
     * check above exists to prevent. */
    CHECK_EQ(offset, 0xDEADBEEFUL, "and does not write the refused offset");
    CHECK_EQ(barOutOfWindow, 0, "and the refusal itself read nothing outside");
    CHECK_EQ(XhciFindExtendedCap(bar_read, NULL, 0x100, 0x404,
                                 XHCI_XECP_ID_LEGACY, 4UL, &offset),
             XHCI_CAPS_OK, "a caller wanting only the header still gets it");

    /* Exactly the final eight bytes is the boundary on the other side: it
     * fits, so it must be found, and nothing may be read past the limit. */
    bar_reset(0x408);
    put_legacy(0x100, 0);
    CHECK_EQ(XhciFindExtendedCap(bar_read, NULL, 0x100, 0x408,
                                 XHCI_XECP_ID_LEGACY, XHCI_USBLEGSUP_BYTES,
                                 &offset),
             XHCI_CAPS_OK, "a capability occupying exactly the last 8 bytes");
    CHECK_EQ(offset, 0x400UL, "at its byte offset");
    CHECK_EQ(barOutOfWindow, 0, "with no read outside the declared window");
}

/* ------------------------------------------------------------------ */
/* 4. Protocol capabilities that do not add up                         */
/* ------------------------------------------------------------------ */

static void test_protocol_refusals(void)
{
    static const ULONG psi[3] = { PSI_FS, PSI_LS, PSI_HS };
    XHCI_PORT_MAP map;

    /* Ports are 1-based: an offset of 0 names no port. */
    bar_reset(0x800);
    put_protocol(0x100, 0, 2, 0, 0, 4, 9, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 4, &map),
             XHCI_CAPS_BAD_PORT_RANGE, "port offset 0 refused");

    /* A group running past HCSPARAMS1.MaxPorts. */
    bar_reset(0x800);
    put_protocol(0x100, 0, 2, 0, 3, 4, 9, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 4, &map),
             XHCI_CAPS_BAD_PORT_RANGE, "group past MaxPorts refused");
    bar_reset(0x800);
    put_protocol(0x100, 0, 2, 0, 1, 4, 9, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 4, &map),
             XHCI_CAPS_OK, "a group ending exactly at MaxPorts is fine");

    /* Two groups claiming the same port. */
    bar_reset(0x800);
    put_protocol(0x100, 4, 2, 0, 1, 4, 9, NULL, 0);
    put_protocol(0x104, 0, 3, 0, 4, 2, 10, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 5, &map),
             XHCI_CAPS_OVERLAPPING_PORT, "overlapping port ranges refused");

    /* A zero-length group claims nothing and is skipped. */
    bar_reset(0x800);
    put_protocol(0x100, 4, 2, 0, 1, 0, 9, NULL, 0);
    put_protocol(0x104, 0, 2, 0, 1, 2, 9, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 2, &map),
             XHCI_CAPS_OK, "zero-port group skipped");
    CHECK_EQ(map.ProtocolCount, 1, "and did not consume a protocol slot");

    /* A capability whose name string is not "USB " is not one of ours. */
    bar_reset(0x800);
    put_protocol(0x100, 4, 2, 0, 1, 2, 9, NULL, 0);
    bar[0x101] = 0x20425541UL;      /* "AUB " */
    put_protocol(0x104, 0, 2, 0, 3, 2, 9, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 4, &map),
             XHCI_CAPS_OK, "non-USB name string skipped");
    CHECK_EQ(map.ProtocolCount, 1, "one group recorded");
    CHECK_EQ(XhciPortClass(&map, 1), XHCI_PORT_CLASS_NONE, "its ports unclaimed");
    CHECK_EQ(XhciPortClass(&map, 3), XHCI_PORT_CLASS_USB2_ONLY, "the real one");

    /* An unknown major revision - USB 4, say - is not this driver's business. */
    bar_reset(0x800);
    put_protocol(0x100, 0, 4, 0, 1, 2, 9, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 2, &map),
             XHCI_CAPS_OK, "unknown major revision skipped");
    CHECK_EQ(map.ProtocolCount, 0, "no group recorded");
    CHECK_EQ(map.ManagedPortCount, 0, "and nothing managed");

    /* More protocol groups than the fixed array holds. */
    bar_reset(0x800);
    put_protocol(0x100, 4, 2, 0, 1, 1, 9, NULL, 0);
    put_protocol(0x104, 4, 2, 0, 2, 1, 9, NULL, 0);
    put_protocol(0x108, 4, 2, 0, 3, 1, 9, NULL, 0);
    put_protocol(0x10C, 4, 2, 0, 4, 1, 9, NULL, 0);
    put_protocol(0x110, 0, 2, 0, 5, 1, 9, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 5, &map),
             XHCI_CAPS_TOO_MANY_PROTOCOLS, "a fifth protocol group is refused");

    /* A PSI count promising DWORDs the mapping does not contain. */
    bar_reset(0x410);
    put_protocol(0x100, 0, 2, 0, 1, 2, 9, psi, 3);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x410, 2, &map),
             XHCI_CAPS_OUT_OF_RANGE, "PSI entries past the mapping refused");
    CHECK_EQ(barOutOfWindow, 0, "and none of them was read");

    /* A protocol header the mapping cannot even hold. */
    bar_reset(0x408);
    put_protocol(0x100, 0, 2, 0, 1, 2, 9, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x408, 2, &map),
             XHCI_CAPS_OUT_OF_RANGE, "truncated protocol capability refused");

    /* Parameter checks. */
    bar_reset(0x800);
    CHECK_EQ(XhciParseExtendedCaps(NULL, NULL, 0x100, 0x800, 4, &map),
             XHCI_CAPS_BAD_PARAM, "null reader");
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 4, NULL),
             XHCI_CAPS_BAD_PARAM, "null map");
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 0, &map),
             XHCI_CAPS_BAD_PARAM, "a controller with no ports");
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 256, &map),
             XHCI_CAPS_BAD_PARAM, "more ports than eight bits can name");
}

/* ------------------------------------------------------------------ */
/* 5. Speed decoding through the advertised table                      */
/* ------------------------------------------------------------------ */

static void test_speed_decode(void)
{
    static const ULONG psi2[3] = { PSI_FS, PSI_LS, PSI_HS };
    static const ULONG psi3[1] = { PSI_SS };
    /* A controller that reorders the IDs: PSIV 1 is High Speed here. The
     * default table would call it Full Speed - which is exactly the silent
     * failure docs/contributing/implementation-invariants.md warns about. */
    static const ULONG reordered[2] = { 0x01E00021UL, 0x000C0023UL };
    XHCI_PORT_MAP map;
    ULONG speed;

    bar_reset(0x800);
    put_protocol(0x100, 8, 2, 0, 1, 4, 9, psi2, 3);
    put_protocol(0x108, 0, 3, 0, 5, 4, 10, psi3, 1);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 8, &map),
             XHCI_CAPS_OK, "chain with PSI tables parsed");

    CHECK_EQ(XhciPortSpeedClass(&map, 1, 1, &speed), XHCI_CAPS_OK, "PSIV 1");
    CHECK_EQ(speed, XHCI_SPEED_FULL, "12 Mb/s is Full Speed");
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 2, &speed), XHCI_CAPS_OK, "PSIV 2");
    CHECK_EQ(speed, XHCI_SPEED_LOW, "1500 Kb/s is Low Speed");
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 3, &speed), XHCI_CAPS_OK, "PSIV 3");
    CHECK_EQ(speed, XHCI_SPEED_HIGH, "480 Mb/s is High Speed");
    CHECK_EQ(XhciPortSpeedClass(&map, 5, 4, &speed), XHCI_CAPS_OK, "PSIV 4");
    CHECK_EQ(speed, XHCI_SPEED_SUPER, "5 Gb/s is SuperSpeed");

    /*
     * A PSIV the controller did not advertise is unknown, never a default.
     * Falling back here would invent a speed on the one controller whose
     * table differs from the defaults.
     */
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 4, &speed), XHCI_CAPS_OK,
             "PSIV absent from a non-empty table");
    CHECK_EQ(speed, XHCI_SPEED_UNKNOWN, "decodes as unknown, not as a default");
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 7, &speed), XHCI_CAPS_OK, "PSIV 7");
    CHECK_EQ(speed, XHCI_SPEED_UNKNOWN, "still unknown");

    /* Out-of-range and unclaimed ports answer "not mine". */
    speed = 0xEEEEUL;
    CHECK_EQ(XhciPortSpeedClass(&map, 0, 3, &speed), XHCI_CAPS_NOT_FOUND,
             "port 0 does not exist");
    CHECK_EQ(XhciPortSpeedClass(&map, 9, 3, &speed), XHCI_CAPS_NOT_FOUND,
             "port past MaxPorts");
    CHECK_EQ(speed, 0xEEEEUL, "a refused decode leaves the output untouched");

    /* The reordered table must win over the defaults. */
    bar_reset(0x800);
    put_protocol(0x100, 0, 2, 0, 1, 2, 9, reordered, 2);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 2, &map),
             XHCI_CAPS_OK, "reordered chain parsed");
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 1, &speed), XHCI_CAPS_OK, "PSIV 1");
    CHECK_EQ(speed, XHCI_SPEED_HIGH, "PSIV 1 is High Speed on this controller");
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 3, &speed), XHCI_CAPS_OK, "PSIV 3");
    CHECK_EQ(speed, XHCI_SPEED_FULL, "and PSIV 3 is Full Speed");

    /* PSIC = 0: the defaults apply, and only then. */
    bar_reset(0x800);
    put_protocol(0x100, 0, 2, 0, 1, 2, 9, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 2, &map),
             XHCI_CAPS_OK, "chain without PSI tables parsed");
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 1, &speed), XHCI_CAPS_OK, "default 1");
    CHECK_EQ(speed, XHCI_SPEED_FULL, "default PSIV 1 is Full Speed");
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 2, &speed), XHCI_CAPS_OK, "default 2");
    CHECK_EQ(speed, XHCI_SPEED_LOW, "default PSIV 2 is Low Speed");
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 3, &speed), XHCI_CAPS_OK, "default 3");
    CHECK_EQ(speed, XHCI_SPEED_HIGH, "default PSIV 3 is High Speed");
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 4, &speed), XHCI_CAPS_OK, "default 4");
    CHECK_EQ(speed, XHCI_SPEED_SUPER, "default PSIV 4 is SuperSpeed");
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 5, &speed), XHCI_CAPS_OK, "default 5");
    CHECK_EQ(speed, XHCI_SPEED_UNKNOWN, "there is no default PSIV 5");

    /*
     * A Gb/s entry whose mantissa cannot be normalized in 32 bits must decode
     * as unknown rather than wrap. 64-bit arithmetic is banned on this target
     * (AGENTS.md), so this is a real bound, not a theoretical one.
     */
    bar_reset(0x800);
    {
        static ULONG absurd[1];
        absurd[0] = 0x00000031UL | (65535UL << 16);   /* PSIV 1, Gb/s, 65535 */
        put_protocol(0x100, 0, 2, 0, 1, 2, 9, absurd, 1);
    }
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 2, &map),
             XHCI_CAPS_OK, "absurd PSI entry parsed");
    CHECK_EQ(XhciPortSpeedClass(&map, 1, 1, &speed), XHCI_CAPS_OK, "decoded");
    CHECK_EQ(speed, XHCI_SPEED_UNKNOWN, "65535 Gb/s is unknown, not wrapped");
}

/*
 * **The other direction, which task 7b-A.3 needs**: a device behind a hub has
 * no PORTSC to read its Protocol Speed ID from, so the ID has to be looked up
 * from the class usbport reported - against the same table the decode uses, or
 * the driver would write "3 means High Speed" into a Slot Context on the one
 * controller that reordered its IDs.
 */
static void test_speed_encode(void)
{
    static const ULONG psi2[3] = { PSI_FS, PSI_LS, PSI_HS };
    /* The reordered controller again: PSIV 1 is High Speed, PSIV 3 Full. */
    static const ULONG reordered[2] = { 0x01E00021UL, 0x000C0023UL };
    XHCI_PORT_MAP map;
    ULONG psiv;

    bar_reset(0x800);
    put_protocol(0x100, 0, 2, 0, 1, 4, 9, psi2, 3);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 4, &map),
             XHCI_CAPS_OK, "chain with a PSI table parsed");

    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_FULL, &psiv),
             XHCI_CAPS_OK, "Full Speed has an ID");
    CHECK_EQ(psiv, 1, "which is the table's, not a constant");
    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_LOW, &psiv),
             XHCI_CAPS_OK, "Low Speed has an ID");
    CHECK_EQ(psiv, 2, "PSIV 2");
    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_HIGH, &psiv),
             XHCI_CAPS_OK, "High Speed has an ID");
    CHECK_EQ(psiv, 3, "PSIV 3");

    /*
     * A speed this group does not advertise is a **refusal**, not a default -
     * the same rule as the decode direction, and the caller turns it into a
     * device that does not enumerate rather than one addressed at a speed the
     * controller never named.
     */
    psiv = 0xEEEEUL;
    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_SUPER, &psiv),
             XHCI_CAPS_NOT_FOUND, "SuperSpeed is not on this USB 2.0 group");
    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_UNKNOWN, &psiv),
             XHCI_CAPS_NOT_FOUND,
             "and 'unknown' is refused before the table is walked at all - it "
             "would otherwise match whichever entry this driver could not "
             "decode");
    CHECK_EQ(psiv, 0xEEEEUL, "a refused lookup leaves the output untouched");

    CHECK_EQ(XhciPortPsivForSpeed(&map, 0, XHCI_SPEED_FULL, &psiv),
             XHCI_CAPS_NOT_FOUND, "port 0 does not exist");
    CHECK_EQ(XhciPortPsivForSpeed(&map, 9, XHCI_SPEED_FULL, &psiv),
             XHCI_CAPS_NOT_FOUND, "port past MaxPorts");
    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_FULL, NULL),
             XHCI_CAPS_BAD_PARAM, "NULL output refused");

    /* The reordered table wins here too, which is the whole point. */
    bar_reset(0x800);
    put_protocol(0x100, 0, 2, 0, 1, 2, 9, reordered, 2);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 2, &map),
             XHCI_CAPS_OK, "reordered chain parsed");
    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_HIGH, &psiv),
             XHCI_CAPS_OK, "High Speed found");
    CHECK_EQ(psiv, 1, "as PSIV 1 on this controller");
    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_FULL, &psiv),
             XHCI_CAPS_OK, "Full Speed found");
    CHECK_EQ(psiv, 3, "as PSIV 3");

    /* PSIC = 0: the defaults, and only then. */
    bar_reset(0x800);
    put_protocol(0x100, 0, 2, 0, 1, 2, 9, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 2, &map),
             XHCI_CAPS_OK, "chain without a PSI table parsed");
    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_FULL, &psiv),
             XHCI_CAPS_OK, "default Full Speed");
    CHECK_EQ(psiv, XHCI_PSIV_FS, "PSIV 1");
    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_LOW, &psiv),
             XHCI_CAPS_OK, "default Low Speed");
    CHECK_EQ(psiv, XHCI_PSIV_LS, "PSIV 2");
    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_HIGH, &psiv),
             XHCI_CAPS_OK, "default High Speed");
    CHECK_EQ(psiv, XHCI_PSIV_HS, "PSIV 3");
    /*
     * SuperSpeed is refused even by the defaults, deliberately: USB 3.0 is out
     * of scope, so answering would let a caller build a Slot Context for a
     * device this driver has no path to. `XhciInitialMps0` refuses the same
     * speed for the same reason.
     */
    CHECK_EQ(XhciPortPsivForSpeed(&map, 1, XHCI_SPEED_SUPER, &psiv),
             XHCI_CAPS_NOT_FOUND, "SuperSpeed refused even by the defaults");
}

/* ------------------------------------------------------------------ */
/* 9. Capability-register derivation                                   */
/* ------------------------------------------------------------------ */

/*
 * XhciDeriveHcInfo turns seven registers into three register-block bases the
 * whole driver then dereferences, so the interesting inputs are the ones a
 * controller produces when it is *not* answering: an all-ones RTSOFF masks to
 * 0xFFFFFFE0, and the obvious bounds check (`offset + need <= mapped`) wraps
 * to true for it. Those cases are here, and so are the exact boundaries on
 * either side of each window check, because an off-by-one in this arithmetic
 * is a driver that reads outside its own mapping.
 *
 * A plausible mid-range controller as the baseline, transcribed by hand from
 * docs/usb-xhci-info/xhci-data-structures.md section 2: CAPLENGTH 0x20, HCIVERSION 1.0,
 * 21 ports, 32 slots, 8 interrupters, 8 scratchpad buffers, 64-byte contexts,
 * AC64 and PPC set, xECP at DWORD 0x100.
 */
#define HC_CAP_DWORD0   0x01000020UL
#define HC_HCSPARAMS1   0x15000820UL
#define HC_HCSPARAMS2   0x400000F0UL
#define HC_HCCPARAMS1   0x0100000DUL
#define HC_DBOFF        0x00003000UL
#define HC_RTSOFF       0x00002000UL
#define HC_MAPPED       0x00010000UL

/*
 * The baseline controller declares no HCCPARAMS2, which is what a 1.0 part is
 * and what this file's HC_CAP_DWORD0 says. The FSC block below drives the
 * seventh register explicitly through derive_hcc2; everything else goes through
 * this form, which passes a zero and therefore asserts nothing about it.
 */
static ULONG derive_hcc2(ULONG capDword0, ULONG hcs1, ULONG hcs2, ULONG hcc1,
                         ULONG hcc2, ULONG dboff, ULONG rtsoff, ULONG mapped,
                         PXHCI_HC_INFO info)
{
    return XhciDeriveHcInfo(capDword0, hcs1, hcs2, hcc1, hcc2, dboff, rtsoff,
                            mapped, info);
}

static ULONG derive(ULONG capDword0, ULONG hcs1, ULONG hcs2, ULONG hcc1,
                    ULONG dboff, ULONG rtsoff, ULONG mapped,
                    PXHCI_HC_INFO info)
{
    return derive_hcc2(capDword0, hcs1, hcs2, hcc1, 0, dboff, rtsoff,
                       mapped, info);
}

/* The baseline, with one input replaced. */
#define DERIVE_BASE(info) \
    derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1, \
           HC_DBOFF, HC_RTSOFF, HC_MAPPED, (info))

static void test_hc_info(void)
{
    XHCI_HC_INFO info;
    XHCI_HC_INFO other;
    ULONG capLength;
    ULONG version;

    /* --- the two-value sanity gate, on its own ------------------------- */
    capLength = 0;
    version = 0;
    CHECK_EQ(XhciCheckCapDword0(HC_CAP_DWORD0, HC_MAPPED, &capLength, &version),
             XHCI_HC_OK, "a plausible CAPLENGTH/HCIVERSION dword");
    CHECK_EQ(capLength, 0x20, "CAPLENGTH is bits 7:0");
    CHECK_EQ(version, 0x0100, "HCIVERSION is bits 31:16");
    CHECK_EQ(XhciCheckCapDword0(HC_CAP_DWORD0, HC_MAPPED, NULL, NULL),
             XHCI_HC_OK, "both outputs are optional");

    CHECK_EQ(XhciCheckCapDword0(0xFFFFFFFFUL, HC_MAPPED, NULL, NULL),
             XHCI_HC_NOT_DECODING, "all ones is a device that is not decoding");
    CHECK_EQ(XhciCheckCapDword0(0x01000000UL, HC_MAPPED, NULL, NULL),
             XHCI_HC_BAD_CAPLENGTH, "CAPLENGTH 0 puts the op regs at BAR0");
    CHECK_EQ(XhciCheckCapDword0(0x010000FFUL, HC_MAPPED, NULL, NULL),
             XHCI_HC_BAD_CAPLENGTH, "CAPLENGTH 0xFF is the byte-wide all-ones");
    CHECK_EQ(XhciCheckCapDword0(0x0100001BUL, HC_MAPPED, NULL, NULL),
             XHCI_HC_BAD_CAPLENGTH,
             "CAPLENGTH below 0x1C overlaps RTSOFF, the last cap register");
    CHECK_EQ(XhciCheckCapDword0(0x0100001CUL, HC_MAPPED, NULL, NULL),
             XHCI_HC_OK, "0x1C is the smallest CAPLENGTH that does not");
    CHECK_EQ(XhciCheckCapDword0(0x00FF0020UL, HC_MAPPED, NULL, NULL),
             XHCI_HC_BAD_VERSION, "HCIVERSION below 1.0 is refused");
    CHECK_EQ(XhciCheckCapDword0(0xFFFF0020UL, HC_MAPPED, NULL, NULL),
             XHCI_HC_BAD_VERSION, "an all-ones HCIVERSION half is refused");
    CHECK_EQ(XhciCheckCapDword0(HC_CAP_DWORD0, 0x1B, NULL, NULL),
             XHCI_HC_WINDOW_TOO_SMALL,
             "a window too small to hold the capability registers");
    CHECK_EQ(XhciCheckCapDword0(HC_CAP_DWORD0, 0x1C, NULL, NULL),
             XHCI_HC_OK, "0x1C bytes is exactly enough for them");

    /* --- the full derivation ------------------------------------------- */
    CHECK_EQ(DERIVE_BASE(&info), XHCI_HC_OK, "baseline controller decodes");
    CHECK_EQ(info.CapLength, 0x20, "CapLength");
    CHECK_EQ(info.HciVersion, 0x0100, "HciVersion");
    CHECK_EQ(info.OperationalOffset, 0x20, "operational base is CAPLENGTH");
    CHECK_EQ(info.PortscOffset, 0x420, "PORTSC is operational + 0x400");
    CHECK_EQ(info.RuntimeOffset, 0x2000, "RuntimeOffset");
    CHECK_EQ(info.DoorbellOffset, 0x3000, "DoorbellOffset");
    CHECK_EQ(info.MaxPorts, 21, "MaxPorts");
    CHECK_EQ(info.MaxSlots, 32, "MaxSlots");
    CHECK_EQ(info.MaxIntrs, 8, "MaxIntrs");
    CHECK_EQ(info.ScratchpadCount, 8, "ScratchpadCount");
    CHECK_EQ(info.ContextSize, 64, "CSZ = 1 means 64-byte contexts");
    CHECK_EQ(info.Ac64, 1, "AC64 recorded");
    CHECK_EQ(info.Ppc, 1, "PPC recorded");
    CHECK_EQ(info.XecpDwords, 0x100, "xECP recorded as a DWORD offset");
    CHECK_EQ(info.Cfc, 0,
             "the baseline declares no Contiguous Frame ID Capability, which "
             "is what an xHCI 1.0 controller looks like");
    CHECK_EQ(info.IstFrames, 0, "and an IST of zero");

    /*
     * Task 9-A.1's two fields, and the IST one has a **unit selector in bit 3**
     * rather than a four-bit magnitude: "If bit [3] of IST is cleared to '0',
     * software can add a TRB no later than IST[2:0] Microframes ... If bit [3]
     * of IST is set to '1', software can add a TRB no later than IST[2:0]
     * Frames" (5.3.4). Reading the field as a plain number turns a threshold of
     * seven *microframes* - under one frame - into seven frames, and the Valid
     * Frame Window's start bound moves with it.
     *
     * The conversion rounds **up** to a whole frame, which the one place the
     * value is used requires: "where IST shall be rounded up to the nearest
     * frame boundary if it is defined in microframes" (4.11.2.5 p.199).
     */
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1,
                    (HC_HCSPARAMS2 & ~0xFUL) | 0x1UL, HC_HCCPARAMS1,
                    HC_DBOFF, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_OK, "IST = 1 microframe decodes");
    CHECK_EQ(info.IstFrames, 1, "and rounds up to one whole frame");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1,
                    (HC_HCSPARAMS2 & ~0xFUL) | 0x7UL, HC_HCCPARAMS1,
                    HC_DBOFF, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_OK, "IST = 7 microframes decodes");
    CHECK_EQ(info.IstFrames, 1,
             "and is still one frame - seven microframes is under a frame, not "
             "seven of them");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1,
                    (HC_HCSPARAMS2 & ~0xFUL) | 0x8UL, HC_HCCPARAMS1,
                    HC_DBOFF, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_OK, "bit 3 set with a magnitude of 0");
    CHECK_EQ(info.IstFrames, 0, "is zero frames, not eight microframes");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1,
                    (HC_HCSPARAMS2 & ~0xFUL) | 0xFUL, HC_HCCPARAMS1,
                    HC_DBOFF, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_OK, "the largest IST decodes");
    CHECK_EQ(info.IstFrames, 7,
             "as seven frames - the magnitude is bits 2:0, never 3:0");

    /* CFC is HCCPARAMS1 bit 11 (Table 5-13), and it decides whether an explicit
     * Frame ID may appear in any Isoch TD but the first of a data flow. */
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2,
                    HC_HCCPARAMS1 | 0x800UL, HC_DBOFF, HC_RTSOFF, HC_MAPPED,
                    &info),
             XHCI_HC_OK, "a controller declaring CFC decodes");
    CHECK_EQ(info.Cfc, 1, "with the capability recorded");

    /* CSZ = 0 is the other stride, and nothing else moves with it. */
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2,
                    HC_HCCPARAMS1 & ~0x4UL, HC_DBOFF, HC_RTSOFF, HC_MAPPED,
                    &info),
             XHCI_HC_OK, "CSZ = 0 decodes");
    CHECK_EQ(info.ContextSize, 32, "CSZ = 0 means 32-byte contexts");

    /* --- all-ones on each register separately -------------------------- */
    CHECK_EQ(derive(0xFFFFFFFFUL, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    HC_DBOFF, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_NOT_DECODING, "all-ones DW0");
    /*
     * The three that matter most: an all-ones HCSPARAMS1 decodes to 255 ports
     * and 255 slots, which are legal values, so only an explicit check can
     * tell a dead bus from a very large controller.
     */
    CHECK_EQ(derive(HC_CAP_DWORD0, 0xFFFFFFFFUL, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    HC_DBOFF, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_NOT_DECODING, "all-ones HCSPARAMS1");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, 0xFFFFFFFFUL, HC_HCCPARAMS1,
                    HC_DBOFF, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_NOT_DECODING, "all-ones HCSPARAMS2");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, 0xFFFFFFFFUL,
                    HC_DBOFF, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_NOT_DECODING, "all-ones HCCPARAMS1");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    0xFFFFFFFFUL, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_NOT_DECODING, "all-ones DBOFF");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    HC_DBOFF, 0xFFFFFFFFUL, HC_MAPPED, &info),
             XHCI_HC_NOT_DECODING, "all-ones RTSOFF");

    /* --- a controller with nothing to offer ---------------------------- */
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1 & 0x00FFFFFFUL, HC_HCSPARAMS2,
                    HC_HCCPARAMS1, HC_DBOFF, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_NO_PORTS, "MaxPorts 0");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1 & ~0xFFUL, HC_HCSPARAMS2,
                    HC_HCCPARAMS1, HC_DBOFF, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_NO_SLOTS, "MaxSlots 0");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1 & ~0x0007FF00UL, HC_HCSPARAMS2,
                    HC_HCCPARAMS1, HC_DBOFF, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_NO_INTERRUPTERS, "MaxIntrs 0 - no interrupter 0 to use");

    /* --- the RsvdZ bits in RTSOFF and DBOFF are not part of the offset -- */
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    HC_DBOFF | 3UL, HC_RTSOFF | 0x1FUL, HC_MAPPED, &info),
             XHCI_HC_OK, "the low bits of DBOFF/RTSOFF are RsvdZ, not offset");
    CHECK_EQ(info.RuntimeOffset, 0x2000, "RTSOFF masks off its low 5 bits");
    CHECK_EQ(info.DoorbellOffset, 0x3000, "DBOFF masks off its low 2 bits");

    /* --- offsets that land on the capability registers ------------------ */
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    HC_DBOFF, 0, HC_MAPPED, &info),
             XHCI_HC_BAD_RTSOFF, "RTSOFF 0 overlaps the capability registers");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    0, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_BAD_DBOFF, "DBOFF 0 overlaps the capability registers");

    /*
     * The wrap cases, and the whole reason the bounds are two subtractions.
     * These values are not all-ones - they are what all-ones *becomes* after
     * the RsvdZ mask - and `offset + need <= mapped` is true for both of them.
     */
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    HC_DBOFF, 0xFFFFFFE0UL, HC_MAPPED, &info),
             XHCI_HC_BAD_RTSOFF, "a runtime offset that would wrap is refused");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    0xFFFFFFFCUL, HC_RTSOFF, HC_MAPPED, &info),
             XHCI_HC_BAD_DBOFF, "a doorbell offset that would wrap is refused");

    /* --- exact window boundaries ---------------------------------------- */

    /*
     * The runtime block needs MFINDEX plus interrupter 0: 0x40 bytes. With
     * RTSOFF at 0x2000 the window has to reach 0x2040.
     */
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    0x1000, HC_RTSOFF, 0x2040, &info),
             XHCI_HC_OK, "0x2040 is exactly enough for RTSOFF 0x2000");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    0x1000, HC_RTSOFF, 0x203F, &info),
             XHCI_HC_BAD_RTSOFF, "one byte short of the interrupter is not");

    /*
     * The doorbell array is (MaxSlots + 1) DWORDs - DB[0] is the command
     * doorbell - so 32 slots need 0x84 bytes from DBOFF.
     */
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    HC_DBOFF, HC_RTSOFF, 0x3084, &info),
             XHCI_HC_OK, "0x3084 holds 33 doorbells at DBOFF 0x3000");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    HC_DBOFF, HC_RTSOFF, 0x3083, &info),
             XHCI_HC_BAD_DBOFF, "one byte short of the last doorbell is not");

    /*
     * The PORTSC array. With CAPLENGTH 0x20 and a 4 KB window there is room
     * for (0x1000 - 0x20 - 0x400) / 0x10 = 190 ports, so 190 fits and 191 does
     * not. Both use a runtime and doorbell block that sit below PORTSC, which
     * is unusual but legal and keeps this one check the only one failing.
     */
    CHECK_EQ(derive(HC_CAP_DWORD0, 0xBE000102UL, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    0x700, 0x600, 0x1000, &info),
             XHCI_HC_OK, "190 ports fit a 4 KB window");
    CHECK_EQ(info.MaxPorts, 190, "and are decoded");
    CHECK_EQ(derive(HC_CAP_DWORD0, 0xBF000102UL, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    0x700, 0x600, 0x1000, &info),
             XHCI_HC_WINDOW_TOO_SMALL, "191 do not");

    /* A window that stops before the fixed operational registers, before
     * PORTSC enters into it at all. */
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2, HC_HCCPARAMS1,
                    HC_DBOFF, HC_RTSOFF, 0x5B, &info),
             XHCI_HC_WINDOW_TOO_SMALL, "no room for CONFIG at 0x20 + 0x38");

    /* --- a refusal must not half-write the caller's structure ----------- */
    {
        ULONG i;
        ULONG *words;

        CHECK_EQ(DERIVE_BASE(&info), XHCI_HC_OK, "start from a good decode");
        words = (ULONG *)&other;
        for (i = 0; i < sizeof(XHCI_HC_INFO) / sizeof(ULONG); i++) {
            words[i] = 0xA5A5A5A5UL;
        }
        CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2,
                        HC_HCCPARAMS1, HC_DBOFF, 0, HC_MAPPED, &other),
                 XHCI_HC_BAD_RTSOFF, "refused late, after most fields decoded");
        for (i = 0; i < sizeof(XHCI_HC_INFO) / sizeof(ULONG); i++) {
            CHECK_EQ(words[i], 0xA5A5A5A5UL,
                     "a refused derivation leaves the output untouched");
        }
        CHECK_EQ(XhciDeriveHcInfo(HC_CAP_DWORD0, HC_HCSPARAMS1, HC_HCSPARAMS2,
                                  HC_HCCPARAMS1, 0, HC_DBOFF, HC_RTSOFF,
                                  HC_MAPPED, NULL),
                 XHCI_HC_BAD_PARAM, "a NULL output is refused, not written");
    }

    /* --- FSC, and the two gates on believing HCCPARAMS2 ------------------ */
    {
        /* CAPLENGTH 0x20 with HCIVERSION 1.10 and 1.00 respectively. */
        ULONG cap11 = 0x01100020UL;
        ULONG cap10 = HC_CAP_DWORD0;
        /* CAPLENGTH 0x1C - the register is outside the controller's own
         * declared capability block, whatever the version says. */
        ULONG cap11Short = 0x0110001CUL;
        ULONG fscSet = 0x00000004UL;    /* HCCPARAMS2 bit 2 */

        CHECK_EQ(derive_hcc2(cap11, HC_HCSPARAMS1, HC_HCSPARAMS2,
                             HC_HCCPARAMS1, fscSet, HC_DBOFF, HC_RTSOFF,
                             HC_MAPPED, &info),
                 XHCI_HC_OK, "a 1.1 controller derives");
        CHECK_EQ(info.Fsc, 1UL, "and its HCCPARAMS2 bit 2 is FSC");

        CHECK_EQ(derive_hcc2(cap11, HC_HCSPARAMS1, HC_HCSPARAMS2,
                             HC_HCCPARAMS1, ~fscSet, HC_DBOFF, HC_RTSOFF,
                             HC_MAPPED, &info),
                 XHCI_HC_OK, "every other HCCPARAMS2 bit set");
        CHECK_EQ(info.Fsc, 0UL, "and none of them is FSC");

        /*
         * **There is no version gate, and that is a correction rather than an
         * omission.** A first version of this required HCIVERSION >= 1.10 on the
         * reasoning that HCCPARAMS2 arrived in xHCI 1.1. Appendix H.1 says
         * otherwise: it lists the capabilities "that were optional for xHCI 1.0
         * implementations [and] are now required in xHCI 1.1 implementations",
         * and H.1.6 is FSC (p.593) - as are U3C, CTC and CIC, three more bits of
         * the same register. So a 1.0 controller answers this bit honestly, and
         * forcing it to 0 would discard a discovery bit and make every resume on
         * that hardware reinitialise for nothing.
         */
        CHECK_EQ(derive_hcc2(cap10, HC_HCSPARAMS1, HC_HCSPARAMS2,
                             HC_HCCPARAMS1, fscSet, HC_DBOFF, HC_RTSOFF,
                             HC_MAPPED, &info),
                 XHCI_HC_OK, "a 1.0 controller derives");
        CHECK_EQ(info.Fsc, 1UL,
                 "and its HCCPARAMS2 bit 2 is believed - FSC was optional at "
                 "1.0, not absent");

        CHECK_EQ(derive_hcc2(cap10, HC_HCSPARAMS1, HC_HCSPARAMS2,
                             HC_HCCPARAMS1, ~fscSet, HC_DBOFF, HC_RTSOFF,
                             HC_MAPPED, &info),
                 XHCI_HC_OK, "a 1.0 controller that clears the bit");
        CHECK_EQ(info.Fsc, 0UL, "declares no FSC, and that is the reading that "
                                "makes the suspend decline");

        /* The gate that survives, and it is about reach rather than version:
         * a controller whose own CAPLENGTH stops at 0x1C has put its operational
         * registers on top of this address, so there is no HCCPARAMS2 to read. */
        CHECK_EQ(derive_hcc2(cap11Short, HC_HCSPARAMS1, HC_HCSPARAMS2,
                             HC_HCCPARAMS1, fscSet, HC_DBOFF, HC_RTSOFF,
                             HC_MAPPED, &info),
                 XHCI_HC_OK, "CAPLENGTH 0x1C is still a legal controller");
        CHECK_EQ(info.Fsc, 0UL,
                 "but its capability block stops before HCCPARAMS2, so FSC is 0");

        CHECK_EQ(derive_hcc2(cap11, HC_HCSPARAMS1, HC_HCSPARAMS2,
                             HC_HCCPARAMS1, 0xFFFFFFFFUL, HC_DBOFF, HC_RTSOFF,
                             HC_MAPPED, &info),
                 XHCI_HC_OK, "an all-ones HCCPARAMS2 does not refuse the "
                             "controller - the register is optional");
        CHECK_EQ(info.Fsc, 0UL,
                 "but it is not data either, and bit 2 of it is a 1");

        CHECK_EQ(derive_hcc2(cap11, HC_HCSPARAMS1, HC_HCSPARAMS2,
                             HC_HCCPARAMS1, fscSet, HC_DBOFF, HC_RTSOFF,
                             0x1CUL, &info),
                 XHCI_HC_WINDOW_TOO_SMALL,
                 "a window that cannot hold HCCPARAMS2 is judged on the "
                 "mandatory registers, which need more than 0x1C here");
    }

    /* --- the post-reset comparison -------------------------------------- */
    CHECK_EQ(DERIVE_BASE(&info), XHCI_HC_OK, "first derivation");
    CHECK_EQ(DERIVE_BASE(&other), XHCI_HC_OK, "second derivation");
    CHECK_EQ(XhciHcInfoEqual(&info, &other), 1,
             "the same controller twice compares equal");
    CHECK_EQ(derive(HC_CAP_DWORD0, HC_HCSPARAMS1 - 1UL, HC_HCSPARAMS2,
                    HC_HCCPARAMS1, HC_DBOFF, HC_RTSOFF, HC_MAPPED, &other),
             XHCI_HC_OK, "one slot fewer still decodes");
    CHECK_EQ(XhciHcInfoEqual(&info, &other), 0,
             "a single changed field compares unequal");
    CHECK_EQ(XhciHcInfoEqual(&info, NULL), 0, "NULL is never equal");
    CHECK_EQ(XhciHcInfoEqual(NULL, &info), 0, "in either position");
}

/* ------------------------------------------------------------------ */
/* 10. The port-map comparison                                         */
/* ------------------------------------------------------------------ */

/*
 * The driver parses the chain twice - once before it claims the controller,
 * once after the reset - and requires the two to agree (src/xhci_init.c).
 * XhciPortMapEqual is what "agree" means, so it has two properties to hold.
 *
 * It must be a *function of the parse*: two parses of the same chain compare
 * equal, whatever was in the structures before. And it must separate any
 * difference the driver would otherwise act on - including the ones a
 * comparison written by hand tends to omit, which is why a single PSI DWORD and
 * a slot type are checked by name.
 *
 * The last vector is the reason this is a comparison and not the 32-bit digest
 * it replaced; see test_digest_collision below.
 */
static void test_port_map_equal(void)
{
    static const ULONG psi2[3] = { PSI_FS, PSI_LS, PSI_HS };
    static const ULONG psi2b[3] = { PSI_FS, PSI_HS, PSI_HS };
    XHCI_PORT_MAP a;
    XHCI_PORT_MAP b;
    ULONG i;
    ULONG j;

    bar_reset(0x800);
    put_protocol(0x100, 8, 2, 0, 1, 4, 9, psi2, 3);
    put_protocol(0x108, 0, 3, 0, 5, 4, 10, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 8, &a),
             XHCI_CAPS_OK, "chain parsed");
    CHECK_EQ(XhciPortMapEqual(&a, &a), 1, "a map equals itself");

    /* Same chain, a structure that held something else first: the answer is a
     * function of the parse, not of the memory it landed in. Every field the
     * comparison reads has to have been written by the parse - including the
     * protocol slots and ports it never reaches. */
    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        b.Class[i] = 0xEE;
        b.Protocol[i] = 0xEE;
        b.Companion[i] = 0xEE;
    }
    for (i = 0; i < XHCI_MAX_PROTOCOLS; i++) {
        b.Protocols[i].SlotType = 0xEEUL;
        for (j = 0; j < XHCI_MAX_PSI; j++) {
            b.Protocols[i].Psi[j] = 0xEEEEEEEEUL;
        }
    }
    for (i = 0; i < XHCI_PORT_MAP_RESERVED; i++) {
        b.Reserved[i] = 0xEE;
    }
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 8, &b),
             XHCI_CAPS_OK, "same chain parsed again");
    CHECK_EQ(XhciPortMapEqual(&a, &b), 1, "two parses of one chain are equal");

    /*
     * The declared tail padding is part of that: it is compared, so the parse
     * has to write it, and a byte of it later spent on a real field is covered
     * from the start rather than from whenever someone revisits the comparison.
     * (The bytes are named for exactly that reason - three bytes of implicit
     * padding would take three appended UCHAR fields without sizeof moving,
     * which is what the layout assertion in src/xhci_caps.c watches.)
     */
    for (i = 0; i < XHCI_PORT_MAP_RESERVED; i++) {
        CHECK_EQ(b.Reserved[i], 0, "the parse writes the tail padding");
        b.Reserved[i] = 1;
        CHECK_EQ(XhciPortMapEqual(&a, &b), 0, "which the comparison reads");
        b.Reserved[i] = 0;
    }
    CHECK_EQ(XhciPortMapEqual(&a, &b), 1, "and restoring it restores equality");

    /* One PSI DWORD, with every port class, range and slot type identical.
     * This is the case the comparison exists for. */
    bar_reset(0x800);
    put_protocol(0x100, 8, 2, 0, 1, 4, 9, psi2b, 3);
    put_protocol(0x108, 0, 3, 0, 5, 4, 10, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 8, &b),
             XHCI_CAPS_OK, "chain with one PSI entry changed parsed");
    CHECK_EQ(b.ManagedPortCount, a.ManagedPortCount, "same managed count");
    CHECK_EQ(XhciPortMapEqual(&a, &b), 0, "but not equal");

    /* A slot type, which nothing else in the map reflects. */
    bar_reset(0x800);
    put_protocol(0x100, 8, 2, 0, 1, 4, 7, psi2, 3);
    put_protocol(0x108, 0, 3, 0, 5, 4, 10, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 8, &b),
             XHCI_CAPS_OK, "chain with a changed slot type parsed");
    CHECK_EQ(XhciPortMapEqual(&a, &b), 0, "a slot type separates two maps");

    /* A port range, which changes the classification too. */
    bar_reset(0x800);
    put_protocol(0x100, 8, 2, 0, 1, 3, 9, psi2, 3);
    put_protocol(0x108, 0, 3, 0, 5, 4, 10, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 8, &b),
             XHCI_CAPS_OK, "chain with a changed port range parsed");
    CHECK_EQ(XhciPortMapEqual(&a, &b), 0, "so does a port range");

    CHECK_EQ(XhciPortMapEqual(&a, NULL), 0, "NULL is never equal");
    CHECK_EQ(XhciPortMapEqual(NULL, &a), 0, "in either position");
    CHECK_EQ(XhciPortMapEqual(NULL, NULL), 0, "not even to another NULL");
}

/* ------------------------------------------------------------------ */
/* 10b. Why it is a comparison and not a digest                        */
/* ------------------------------------------------------------------ */

/*
 * The check above used to compare a 32-bit FNV-1a fold over the map's fields,
 * one ULONG at a time, on the argument that both values were computed by this
 * driver moments apart so collision resistance was not being relied on. That
 * argument was wrong: accepting two maps as equal *because* their folds match
 * relies on nothing else.
 *
 * The fold is trivially collidable, and not by luck. Each step is
 * hash = (hash ^ word) * prime with an odd prime, so it is invertible: for any
 * two words x and x2 at one field, choosing the next field's y2 as
 * y ^ step(h, x) ^ step(h, x2) makes the state after both fields identical, and
 * every later step therefore agrees to the last bit. Two adjacent PSI DWORDs
 * are exactly such a pair - the parser retains them verbatim and nothing else
 * in the map reflects them - so a controller whose speed table changed across
 * the reset could have presented a fold-identical map.
 *
 * The fold is reproduced here as the adversary. It is not the driver's code any
 * more; what these checks pin is that the constructed pair really is
 * indistinguishable to a word-at-a-time digest of that shape, and that
 * XhciPortMapEqual separates it anyway.
 */
#define FNV_OFFSET_BASIS    2166136261UL
#define FNV_PRIME           16777619UL

static ULONG fold_step(ULONG hash, ULONG value)
{
    hash ^= value;
    hash *= FNV_PRIME;
    return hash;
}

/* The fold over every field ahead of Protocols[0].Psi[0], in the order the
 * retired digest visited them: the five header words, then that protocol's six
 * scalars. Both maps below are identical up to there, so this is the state the
 * two PSI DWORDs are folded into. */
static ULONG fold_before_first_psi(const XHCI_PORT_MAP *map)
{
    ULONG hash;

    hash = FNV_OFFSET_BASIS;
    hash = fold_step(hash, map->PortCount);
    hash = fold_step(hash, map->ProtocolCount);
    hash = fold_step(hash, map->ManagedPortCount);
    hash = fold_step(hash, map->LegacySupportOffset);
    hash = fold_step(hash, map->DebugCapabilityOffset);
    hash = fold_step(hash, map->Protocols[0].Major);
    hash = fold_step(hash, map->Protocols[0].Minor);
    hash = fold_step(hash, map->Protocols[0].PortOffset);
    hash = fold_step(hash, map->Protocols[0].PortCount);
    hash = fold_step(hash, map->Protocols[0].SlotType);
    hash = fold_step(hash, map->Protocols[0].PsiCount);
    return hash;
}

static void test_digest_collision(void)
{
    ULONG psiA[3];
    ULONG psiB[3];
    XHCI_PORT_MAP a;
    XHCI_PORT_MAP b;
    ULONG prefix;

    psiA[0] = PSI_FS;
    psiA[1] = PSI_LS;
    psiA[2] = PSI_HS;

    bar_reset(0x800);
    put_protocol(0x100, 8, 2, 0, 1, 4, 9, psiA, 3);
    put_protocol(0x108, 0, 3, 0, 5, 4, 10, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 8, &a),
             XHCI_CAPS_OK, "the first speed table parsed");

    /* Solve for the second DWORD, given a first one that is simply a different
     * speed the same controller could legitimately have advertised. */
    prefix = fold_before_first_psi(&a);
    psiB[0] = PSI_HS;
    psiB[1] = psiA[1] ^ fold_step(prefix, psiA[0]) ^ fold_step(prefix, psiB[0]);
    psiB[2] = psiA[2];

    CHECK(psiB[0] != psiA[0], "the constructed table really differs");
    CHECK(psiB[1] != psiA[1], "in both DWORDs");
    CHECK_EQ(fold_step(fold_step(prefix, psiB[0]), psiB[1]),
             fold_step(fold_step(prefix, psiA[0]), psiA[1]),
             "and the fold state after them is identical, so every later "
             "field folds the same and the digests match");

    bar_reset(0x800);
    put_protocol(0x100, 8, 2, 0, 1, 4, 9, psiB, 3);
    put_protocol(0x108, 0, 3, 0, 5, 4, 10, NULL, 0);
    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x100, 0x800, 8, &b),
             XHCI_CAPS_OK, "the colliding speed table parsed");

    CHECK_EQ(b.Protocols[0].Psi[0], psiB[0], "the map holds the new DWORDs");
    CHECK_EQ(b.Protocols[0].Psi[1], psiB[1], "both of them");
    CHECK_EQ(b.ManagedPortCount, a.ManagedPortCount,
             "nothing else about the topology moved");
    CHECK_EQ(fold_before_first_psi(&b), prefix,
             "and neither did anything the fold reaches before them");

    CHECK_EQ(XhciPortMapEqual(&a, &b), 0,
             "a digest-identical pair is still two different maps");
}

/* ------------------------------------------------------------------ */
/* 11. Phase 0 replay: the two qualified fleet controllers             */
/* ------------------------------------------------------------------ */

/*
 * The shapes above are hand-built to exercise the parser. These two are the
 * opposite: every number below is transcribed from a bare-metal XHCIQUAL v0.9
 * report (the XEMPTY.LOG under each machine's xhciqual/results directory,
 * the Phase 0 runs), and the expected port map is the one that tool - an
 * independently written classifier, running on the silicon itself - printed.
 * Agreement is the only check this project can make
 * against real controllers without a bench session, which is what roadmap
 * Phase 4 task 3 asks for and design doc 03 open question 2 left open.
 *
 * What the reports do *not* carry is the raw register words. v0.9 prints a
 * decoded fact sheet, so:
 *
 *   - The capability registers below are *reconstructed* from that sheet. The
 *     check they make is that a real controller's parameters are accepted and
 *     round-trip - notably 34 scratchpad buffers through the split field - not
 *     that the field positions are right; the hand-transcribed vectors in
 *     test_field_macros are what check those.
 *   - xECP, DBOFF, RTSOFF, the BAR length and HCSPARAMS2's other fields were
 *     not recorded at all, so they are synthetic and nothing asserts them.
 *   - PSI *counts* were recorded; the PSI DWORDs were not. The tables below are
 *     filled with plausible entries purely so the counts are reachable, and no
 *     check reads them. A true speed-decode replay needs a xhciqual change to
 *     dump the raw capability DWORDs, and then another bare-metal run.
 *
 * Both machines present the same topology, which is itself worth pinning: 18
 * logical ports, USB 2.0 on 1-12 and USB 3.x on 13-18, so the six USB 3.x ports
 * pair with the *last* six USB 2.0 ports and ports 1-6 are USB-2.0-only. That
 * is the convention in docs/usb-xhci-info/xhci-programming.md "Port Topology Classification"
 * producing, on two chipset generations, exactly what the hardware reports.
 */

/* Both reports: MaxSlots 64, MaxIntrs 8, MaxPorts 18. */
#define FLEET_HCSPARAMS1    0x12000840UL
/* Both reports: Scratchpad 34 = Hi 1 (bits 25:21) | Lo 2 (bits 31:27). The
 * IST and ERST Max fields were not recorded and are left zero. */
#define FLEET_HCSPARAMS2    0x10200000UL
/* Both reports: ContextSize 32 (CSZ = 0), AC64 = 1, PPC = 0. xECP is synthetic;
 * it places the chain at byte 0x600, clear of the PORTSC array that CAPLENGTH
 * 128 and 18 ports put at 0x480-0x59F, where the vectors below build it. */
#define FLEET_HCCPARAMS1    0x01800001UL
/* Not recorded: a 64 KB BAR window with the runtime and doorbell blocks in
 * their conventional places. */
#define FLEET_DBOFF         0x00003000UL
#define FLEET_RTSOFF        0x00002000UL
#define FLEET_MAPPED        0x00010000UL

static void check_fleet_hc_info(ULONG capDword0, const char *what)
{
    XHCI_HC_INFO info;

    CHECK_EQ(derive(capDword0, FLEET_HCSPARAMS1, FLEET_HCSPARAMS2,
                    FLEET_HCCPARAMS1, FLEET_DBOFF, FLEET_RTSOFF, FLEET_MAPPED,
                    &info),
             XHCI_HC_OK, what);
    CHECK_EQ(info.CapLength, 128, "CAPLENGTH 128");
    CHECK_EQ(info.MaxSlots, 64, "MaxSlots 64");
    CHECK_EQ(info.MaxIntrs, 8, "MaxIntrs 8");
    CHECK_EQ(info.MaxPorts, 18, "MaxPorts 18");
    CHECK_EQ(info.ScratchpadCount, 34, "34 scratchpad buffers");
    CHECK_EQ(info.ContextSize, 32, "32-byte contexts (CSZ 0)");
    CHECK_EQ(info.Ac64, 1, "AC64 set, and ignored by this driver");
    CHECK_EQ(info.Ppc, 0, "no port power control");
}

/*
 * The port map both reports printed, port by port. Written as a table rather
 * than as loops because that is how the tool printed it, and a transcription
 * is worth more here than a restatement of the pairing rule the parser already
 * implements.
 */
static void check_fleet_port_map(const XHCI_PORT_MAP *map)
{
    ULONG i;

    CHECK_EQ(map->PortCount, 18, "18 logical ports");
    CHECK_EQ(map->ProtocolCount, 2, "two protocol groups");
    CHECK_EQ(map->Protocols[0].Major, 2, "group 1 is USB 2");
    CHECK_EQ(map->Protocols[0].PortOffset, 1, "USB 2.0 ports start at 1");
    CHECK_EQ(map->Protocols[0].PortCount, 12, "12 USB 2.0 ports");
    CHECK_EQ(map->Protocols[0].SlotType, 0, "USB2 slot type 0");
    CHECK_EQ(map->Protocols[1].Major, 3, "group 2 is USB 3");
    CHECK_EQ(map->Protocols[1].PortOffset, 13, "USB 3.x ports start at 13");
    CHECK_EQ(map->Protocols[1].PortCount, 6, "6 USB 3.x ports");
    CHECK_EQ(map->Protocols[1].SlotType, 0, "USB3 slot type 0");

    for (i = 1; i <= 6; i++) {
        CHECK_EQ(XhciPortClass(map, i), XHCI_PORT_CLASS_USB2_ONLY,
                 "ports 1-6: USB2-only (managed)");
        CHECK_EQ(map->Companion[i - 1], 0, "and unpaired");
        CHECK_EQ(XhciPortIsManaged(map, i), 1, "managed");
    }
    for (i = 7; i <= 12; i++) {
        CHECK_EQ(XhciPortClass(map, i), XHCI_PORT_CLASS_USB2_COMPANION,
                 "ports 7-12: USB2 companion (managed)");
        CHECK_EQ(map->Companion[i - 1], i + 6, "paired with 13-18");
        CHECK_EQ(XhciPortIsManaged(map, i), 1, "managed");
    }
    for (i = 13; i <= 18; i++) {
        CHECK_EQ(XhciPortClass(map, i), XHCI_PORT_CLASS_USB3_COMPANION,
                 "ports 13-18: USB3 companion (unmanaged)");
        CHECK_EQ(map->Companion[i - 1], i - 6, "paired back with 7-12");
        CHECK_EQ(XhciPortIsManaged(map, i), 0, "unmanaged");
    }
    /* The report's FACT line: usb2ports=12. */
    CHECK_EQ(map->ManagedPortCount, 12, "12 managed USB 2.0 ports");
}

/*
 * ThinkPad E460, Intel 100-series PCH 8086:9D2F rev 21, HCIVERSION 1.00.
 * xhciqual/results/e460-2026-07-25/XEMPTY.LOG.
 */
static void test_replay_e460(void)
{
    static const ULONG psi2[3] = { PSI_FS, PSI_LS, PSI_HS };
    static const ULONG psi3[3] = { PSI_SS, PSI_SS, PSI_SS };
    XHCI_PORT_MAP map;

    check_fleet_hc_info(0x01000080UL, "E460 capability registers decode");

    bar_reset(FLEET_MAPPED);
    /* legsup=1 in the FACT line: the capability is present. */
    put_legacy(0x180, 2);
    put_protocol(0x182, 8, 2, 0, 1, 12, 0, psi2, 3);
    put_protocol(0x18A, 0, 3, 0, 13, 6, 0, psi3, 3);

    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x180, FLEET_MAPPED, 18,
                                   &map),
             XHCI_CAPS_OK, "E460 chain parsed");
    CHECK_EQ(barOutOfWindow, 0, "no read left the mapped window");
    CHECK_EQ(map.LegacySupportOffset, 0x600UL, "USBLEGSUP present");
    CHECK_EQ(map.Protocols[0].Minor, 0, "USB 2.0");
    CHECK_EQ(map.Protocols[0].PsiCount, 3, "USB2 capability advertises PSIC 3");
    CHECK_EQ(map.Protocols[1].Minor, 0, "USB 3.0");
    CHECK_EQ(map.Protocols[1].PsiCount, 3, "USB3 capability advertises PSIC 3");
    check_fleet_port_map(&map);
}

/*
 * ThinkPad P14s Gen 1, Comet Lake 8086:02ED rev 00, HCIVERSION 1.10 - and a
 * USB *3.1* group with eight PSI entries, which is the largest table this
 * project has seen on real hardware and the reason XHCI_MAX_PSI keeps all 15.
 * xhciqual/results/p14s-gen1-2026-07-25/XEMPTY.LOG.
 */
static void test_replay_p14s(void)
{
    static const ULONG psi2[3] = { PSI_FS, PSI_LS, PSI_HS };
    static const ULONG psi3[8] = { PSI_SS, PSI_SS, PSI_SS, PSI_SS,
                                   PSI_SS, PSI_SS, PSI_SS, PSI_SS };
    XHCI_PORT_MAP map;

    check_fleet_hc_info(0x01100080UL, "P14s capability registers decode");

    bar_reset(FLEET_MAPPED);
    put_legacy(0x180, 2);
    put_protocol(0x182, 8, 2, 0, 1, 12, 0, psi2, 3);
    put_protocol(0x18A, 0, 3, 1, 13, 6, 0, psi3, 8);

    CHECK_EQ(XhciParseExtendedCaps(bar_read, NULL, 0x180, FLEET_MAPPED, 18,
                                   &map),
             XHCI_CAPS_OK, "P14s chain parsed");
    CHECK_EQ(barOutOfWindow, 0, "no read left the mapped window");
    CHECK_EQ(map.LegacySupportOffset, 0x600UL, "USBLEGSUP present");
    CHECK_EQ(map.Protocols[0].PsiCount, 3, "USB2 capability advertises PSIC 3");
    CHECK_EQ(map.Protocols[1].Minor, 1, "USB 3.1");
    CHECK_EQ(map.Protocols[1].PsiCount, 8, "USB3.1 capability advertises PSIC 8");
    check_fleet_port_map(&map);
}

int main(void)
{
    test_field_macros();
    test_intel_shape();
    test_qemu_shape();
    test_usb2_only_shape();
    test_orphan_usb3();
    test_no_managed_ports();
    test_chain_shapes();
    test_protocol_refusals();
    test_speed_decode();
    test_speed_encode();
    test_hc_info();
    test_port_map_equal();
    test_digest_collision();
    test_replay_e460();
    test_replay_p14s();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
