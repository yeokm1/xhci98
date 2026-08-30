/*
 * xhci_caps.c - extended-capability walk, port classification, speed decoding.
 *
 * Pure computation over whatever the caller's reader returns: no MMIO
 * accessors, no DDK calls, no IRQL dependencies, so it builds and runs on the
 * host under XHCI_HOST_TEST (docs/contributing/design/03-host-unit-tests.md).
 * test/test_caps.c is the regression suite; the layouts come from
 * docs/usb-xhci-info/xhci-data-structures.md section 6 and the classification rules from
 * docs/usb-xhci-info/xhci-programming.md "Port Topology Classification".
 *
 * The reader indirection is the point: a capability chain is a linked list
 * inside a memory window that a wrong NEXT pointer walks straight out of, and
 * that is a class of bug no controller reproduces on demand. Here the chain is
 * data, so the degenerate shapes (zero-length groups, a NEXT that leaves the
 * BAR, a protocol claiming ports the controller does not have) are tests.
 *
 * What this file decides is which ports the driver manages. The port strategy
 * itself is not negotiable and is stated in AGENTS.md: USB 2.0 protocol ports
 * only. USB 3.x logical ports are left unpowered and unmanaged, whether they
 * have a USB 2.0 companion or not.
 *
 * C89 only. IRQL: every function is callable at any IRQL - the reader the
 * driver passes is a register read.
 */

#include "xhci.h"

/*
 * A NEXT pointer is relative and nonzero, so offsets strictly increase and the
 * mapped-length check below already terminates the walk. This cap is the
 * belt-and-braces bound on a pathological chain in a very large BAR.
 */
#define XHCI_MAX_XECP_CAPS 256

/*
 * Read one capability header and compute where the next one starts.
 * *nextOffset = 0 means "end of list". Every read this function performs is
 * inside [0, mappedBytes).
 */
static ULONG xhciCapStep(XHCI_READ32 read,
                         PVOID context,
                         ULONG mappedBytes,
                         ULONG offset,
                         ULONG *dw0,
                         ULONG *nextOffset)
{
    ULONG header;
    ULONG next;

    if (offset > mappedBytes || (mappedBytes - offset) < 4) {
        return XHCI_CAPS_OUT_OF_RANGE;
    }
    header = read(context, offset);
    /*
     * All ones is never a capability header: it is the bus answering for a
     * device that is not decoding the access (docs/contributing/implementation-invariants.md,
     * "MMIO Sanity"). Treating it as a chain would walk 0xFF DWORDs onward.
     */
    if (header == 0xFFFFFFFFUL) {
        return XHCI_CAPS_OUT_OF_RANGE;
    }

    *dw0 = header;
    next = XHCI_XECP_NEXT(header);
    if (next == 0) {
        *nextOffset = 0;
    } else {
        *nextOffset = offset + next * 4;
    }
    return XHCI_CAPS_OK;
}

ULONG XhciFindExtendedCap(XHCI_READ32 read,
                          PVOID context,
                          ULONG xecpDwords,
                          ULONG mappedBytes,
                          ULONG capabilityId,
                          ULONG requiredBytes,
                          ULONG *byteOffset)
{
    ULONG offset;
    ULONG next;
    ULONG dw0;
    ULONG status;
    ULONG seen;

    if (read == NULL || byteOffset == NULL) {
        return XHCI_CAPS_BAD_PARAM;
    }
    /* A capability is at least its header, so a caller asking for less than
     * that has made a mistake rather than met bad hardware. */
    if (requiredBytes < 4) {
        return XHCI_CAPS_BAD_PARAM;
    }
    if (xecpDwords == 0) {
        return XHCI_CAPS_NO_LIST;
    }

    offset = xecpDwords * 4;
    for (seen = 0; seen < XHCI_MAX_XECP_CAPS; seen++) {
        status = xhciCapStep(read, context, mappedBytes, offset, &dw0, &next);
        if (status != XHCI_CAPS_OK) {
            return status;
        }
        if (XHCI_XECP_ID(dw0) == capabilityId) {
            /*
             * xhciCapStep bounded the four header bytes; the caller is about
             * to touch the whole capability. USBLEGSUP is the case that makes
             * this matter: its second DWORD, USBLEGCTLSTS, is where the SMI
             * enables are disabled, and the handoff *writes* it. A header
             * sitting in the last four bytes of the mapping would put that
             * write outside the BAR - on Win2000, into whatever mapping
             * follows it.
             *
             * The subtraction cannot wrap: xhciCapStep has already proven
             * offset <= mappedBytes and mappedBytes - offset >= 4.
             */
            if ((mappedBytes - offset) < requiredBytes) {
                return XHCI_CAPS_OUT_OF_RANGE;
            }
            *byteOffset = offset;
            return XHCI_CAPS_OK;
        }
        if (next == 0) {
            return XHCI_CAPS_NOT_FOUND;
        }
        offset = next;
    }
    return XHCI_CAPS_LOOP;
}

static VOID xhciResetPortMap(PXHCI_PORT_MAP map, ULONG maxPorts)
{
    ULONG i;
    ULONG j;

    map->PortCount = maxPorts;
    map->ProtocolCount = 0;
    map->ManagedPortCount = 0;
    map->LegacySupportOffset = 0;
    map->DebugCapabilityOffset = 0;

    for (i = 0; i < XHCI_MAX_PROTOCOLS; i++) {
        map->Protocols[i].Major = 0;
        map->Protocols[i].Minor = 0;
        map->Protocols[i].PortOffset = 0;
        map->Protocols[i].PortCount = 0;
        map->Protocols[i].SlotType = 0;
        map->Protocols[i].PsiCount = 0;
        for (j = 0; j < XHCI_MAX_PSI; j++) {
            map->Protocols[i].Psi[j] = 0;
        }
    }

    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        map->Class[i] = XHCI_PORT_CLASS_NONE;
        map->Protocol[i] = XHCI_PORT_NO_PROTOCOL;
        map->Companion[i] = XHCI_PORT_NO_COMPANION;
    }

    /* The declared tail padding. Written for the same reason as everything
     * above it: XhciPortMapEqual compares it, so it has to be a function of the
     * parse rather than of whatever the structure held before. */
    for (i = 0; i < XHCI_PORT_MAP_RESERVED; i++) {
        map->Reserved[i] = 0;
    }
}

/*
 * Record one Supported Protocol capability and stake its port range. Returns
 * XHCI_CAPS_OK having consumed a Protocols[] slot, or a refusal. A capability
 * this driver has no use for (an unknown major revision, a group claiming no
 * ports) is skipped without consuming a slot and without touching any port -
 * those ports simply stay unmanaged.
 */
static ULONG xhciRecordProtocol(XHCI_READ32 read,
                                PVOID context,
                                ULONG mappedBytes,
                                ULONG offset,
                                ULONG dw0,
                                PXHCI_PORT_MAP map)
{
    XHCI_PROTOCOL *proto;
    ULONG dw2;
    ULONG dw3;
    ULONG major;
    ULONG portOffset;
    ULONG portCount;
    ULONG psic;
    ULONG last;
    ULONG i;

    /* The header plus three DWORDs must be inside the mapping before any of
     * them is read; a truncated capability is a refusal, not a skip. */
    if (offset > mappedBytes || (mappedBytes - offset) < 16) {
        return XHCI_CAPS_OUT_OF_RANGE;
    }
    if (read(context, offset + 4) != XHCI_PROTOCOL_NAME_USB) {
        return XHCI_CAPS_OK;
    }

    major = XHCI_PROTOCOL_MAJOR(dw0);
    if (major != 2 && major != 3) {
        return XHCI_CAPS_OK;
    }

    dw2 = read(context, offset + 8);
    dw3 = read(context, offset + 12);
    portOffset = XHCI_PROTOCOL_PORT_OFFSET(dw2);
    portCount = XHCI_PROTOCOL_PORT_COUNT(dw2);
    psic = XHCI_PROTOCOL_PSIC(dw2);

    if (portCount == 0) {
        return XHCI_CAPS_OK;
    }
    /* Ports are 1-based, and a group must lie inside HCSPARAMS1.MaxPorts.
     * A group that does not is an inconsistent controller, and every later
     * decision - which ports to power, which PORTSC to read - is built on
     * this table. */
    if (portOffset == 0) {
        return XHCI_CAPS_BAD_PORT_RANGE;
    }
    last = portOffset + portCount - 1;
    if (last > map->PortCount || last < portOffset) {
        return XHCI_CAPS_BAD_PORT_RANGE;
    }
    for (i = portOffset; i <= last; i++) {
        if (map->Protocol[i - 1] != XHCI_PORT_NO_PROTOCOL) {
            return XHCI_CAPS_OVERLAPPING_PORT;
        }
    }

    if (map->ProtocolCount >= XHCI_MAX_PROTOCOLS) {
        return XHCI_CAPS_TOO_MANY_PROTOCOLS;
    }
    /* PSIC is four bits, so it cannot exceed the array - but the DWORDs it
     * promises still have to be inside the mapping. */
    if ((mappedBytes - offset) < (16 + psic * 4)) {
        return XHCI_CAPS_OUT_OF_RANGE;
    }

    proto = &map->Protocols[map->ProtocolCount];
    proto->Major = major;
    proto->Minor = XHCI_PROTOCOL_MINOR(dw0);
    proto->PortOffset = portOffset;
    proto->PortCount = portCount;
    proto->SlotType = XHCI_PROTOCOL_SLOT_TYPE(dw3);
    proto->PsiCount = psic;
    for (i = 0; i < psic; i++) {
        proto->Psi[i] = read(context, offset + 16 + i * 4);
    }

    for (i = portOffset; i <= last; i++) {
        map->Protocol[i - 1] = (UCHAR)map->ProtocolCount;
        map->Class[i - 1] = (major == 2) ? XHCI_PORT_CLASS_USB2_ONLY
                                         : XHCI_PORT_CLASS_USB3_ORPHAN;
    }

    map->ProtocolCount++;
    return XHCI_CAPS_OK;
}

/*
 * Pair USB 3.x ports with their USB 2.0 companions.
 *
 * The convention (docs/usb-xhci-info/xhci-programming.md): within a USB 2.0 group of P ports
 * starting at A and a USB 3.x group of Q ports starting at B, USB 3.x port
 * B + k is the same physical connector as USB 2.0 port A + P - Q + k. It is a
 * convention, not a spec guarantee, and getting it wrong costs nothing here:
 * both halves of a pair are treated identically by this driver anyway - the
 * USB 2.0 one is managed, the USB 3.x one is not. The pairing exists so a
 * USB 3.x port with no USB 2.0 path can be named as such (an orphan, a
 * connector this driver cannot serve at all) rather than looking like an
 * ordinary unmanaged port.
 */
static VOID xhciPairCompanions(PXHCI_PORT_MAP map)
{
    ULONG i;
    ULONG j;
    ULONG k;

    for (i = 0; i < map->ProtocolCount; i++) {
        if (map->Protocols[i].Major != 3) {
            continue;
        }
        for (j = 0; j < map->ProtocolCount; j++) {
            if (map->Protocols[j].Major != 2) {
                continue;
            }
            if (map->Protocols[j].PortCount < map->Protocols[i].PortCount) {
                continue;
            }
            for (k = 0; k < map->Protocols[i].PortCount; k++) {
                ULONG ss;
                ULONG hs;

                ss = map->Protocols[i].PortOffset + k;
                hs = map->Protocols[j].PortOffset +
                     map->Protocols[j].PortCount -
                     map->Protocols[i].PortCount + k;

                /* A second USB 3.x group aimed at the same tail must not
                 * steal an already-paired USB 2.0 port; its ports stay
                 * orphans instead. */
                if (map->Companion[hs - 1] != XHCI_PORT_NO_COMPANION) {
                    continue;
                }
                map->Companion[ss - 1] = (UCHAR)hs;
                map->Companion[hs - 1] = (UCHAR)ss;
                map->Class[ss - 1] = XHCI_PORT_CLASS_USB3_COMPANION;
                map->Class[hs - 1] = XHCI_PORT_CLASS_USB2_COMPANION;
            }
            break;
        }
    }
}

ULONG XhciParseExtendedCaps(XHCI_READ32 read,
                            PVOID context,
                            ULONG xecpDwords,
                            ULONG mappedBytes,
                            ULONG maxPorts,
                            PXHCI_PORT_MAP map)
{
    ULONG offset;
    ULONG next;
    ULONG dw0;
    ULONG status;
    ULONG seen;
    ULONG i;

    if (read == NULL || map == NULL) {
        return XHCI_CAPS_BAD_PARAM;
    }
    if (maxPorts == 0 || maxPorts > XHCI_MAX_ROOT_PORTS) {
        return XHCI_CAPS_BAD_PARAM;
    }

    xhciResetPortMap(map, maxPorts);

    if (xecpDwords == 0) {
        return XHCI_CAPS_NO_LIST;
    }

    offset = xecpDwords * 4;
    for (seen = 0; seen < XHCI_MAX_XECP_CAPS; seen++) {
        status = xhciCapStep(read, context, mappedBytes, offset, &dw0, &next);
        if (status != XHCI_CAPS_OK) {
            return status;
        }

        switch (XHCI_XECP_ID(dw0)) {
        case XHCI_XECP_ID_LEGACY:
            if (map->LegacySupportOffset == 0) {
                map->LegacySupportOffset = offset;
            }
            break;
        case XHCI_XECP_ID_PROTOCOL:
            status = xhciRecordProtocol(read, context, mappedBytes, offset,
                                        dw0, map);
            if (status != XHCI_CAPS_OK) {
                return status;
            }
            break;
        case XHCI_XECP_ID_DEBUG:
            if (map->DebugCapabilityOffset == 0) {
                map->DebugCapabilityOffset = offset;
            }
            break;
        default:
            break;
        }

        if (next == 0) {
            break;
        }
        offset = next;
    }
    if (seen >= XHCI_MAX_XECP_CAPS) {
        return XHCI_CAPS_LOOP;
    }

    xhciPairCompanions(map);

    for (i = 0; i < map->PortCount; i++) {
        if (map->Class[i] == XHCI_PORT_CLASS_USB2_ONLY ||
            map->Class[i] == XHCI_PORT_CLASS_USB2_COMPANION) {
            map->ManagedPortCount++;
        }
    }

    return XHCI_CAPS_OK;
}

/*
 * A field added to XHCI_PORT_MAP is a field the comparison below would silently
 * stop covering. XhciHcInfoEqual avoids that trap by being a word loop, which
 * this structure's UCHAR arrays rule out, so the substitute is a layout
 * assertion: adding, removing or resizing anything fails the build here, and
 * the only way to satisfy it again is to come to that function first.
 *
 * The sum is exact, with no rounding up to the next ULONG, and that is the
 * point: the structure declares its own tail padding (XHCI_PORT_MAP_RESERVED),
 * so there is no slack for a new UCHAR field to be appended into without
 * changing sizeof. A rounded bound would have had three bytes of it.
 */
XHCI_C_ASSERT(port_map_is_fully_compared,
              sizeof(XHCI_PORT_MAP) ==
                  5 * sizeof(ULONG) +
                  XHCI_MAX_PROTOCOLS * sizeof(XHCI_PROTOCOL) +
                  3 * XHCI_MAX_ROOT_PORTS + XHCI_PORT_MAP_RESERVED);

XHCI_C_ASSERT(protocol_is_fully_compared,
              sizeof(XHCI_PROTOCOL) == (6 + XHCI_MAX_PSI) * sizeof(ULONG));

/*
 * Do two parses of the same capability chain describe the same controller?
 *
 * The driver builds the port map twice - once in the preflight, once after the
 * reset - and the second must agree with the first, for the same reason the
 * capability registers must (src/xhci_init.c). This function is the *evidence*
 * for that agreement, which is why it compares every field rather than a
 * summary of them. A 32-bit digest of roughly a kilobyte of parsed data can
 * only ever say the two maps might be equal, and accepting on it is a bet
 * against a collision. The word-at-a-time FNV fold this replaced lost that bet
 * to construction, not to chance: each step is invertible, so changing one PSI
 * DWORD and solving for the next reproduces any fold state exactly
 * (test/test_caps.c builds that pair and checks this function rejects it).
 *
 * Exactness costs no stack: both maps are extension-owned
 * (XHCI_EXTENSION.PortMap and .PreflightPortMap), which is what the digest was
 * avoiding.
 *
 * Field by field, never over the structure's storage. Every field below is
 * written by xhciResetPortMap - the protocol slots and ports a parse never
 * reaches, which is why the loops run to the array bounds rather than to
 * ProtocolCount and PortCount, and the declared tail padding, which is compared
 * so that a byte later spent on a real field is covered before anyone remembers
 * to come here. Between that and XHCI_PORT_MAP_RESERVED there is no
 * uninitialized byte left for a byte-wise comparison to read, so what rules one
 * out now is not correctness but where the guarantee would come from: memcmp
 * would rest the answer on a compiler's layout of the structure, and would be
 * an import this driver has not decided to have (the reason xhci_dispatch.c
 * zeroes the registration packet by hand).
 *
 * Returns 1 for equal, 0 for different or for a NULL argument.
 */
ULONG XhciPortMapEqual(const XHCI_PORT_MAP *a, const XHCI_PORT_MAP *b)
{
    const XHCI_PROTOCOL *protoA;
    const XHCI_PROTOCOL *protoB;
    ULONG i;
    ULONG j;

    if (a == NULL || b == NULL) {
        return 0;
    }

    if (a->PortCount != b->PortCount ||
        a->ProtocolCount != b->ProtocolCount ||
        a->ManagedPortCount != b->ManagedPortCount ||
        a->LegacySupportOffset != b->LegacySupportOffset ||
        a->DebugCapabilityOffset != b->DebugCapabilityOffset) {
        return 0;
    }

    for (i = 0; i < XHCI_MAX_PROTOCOLS; i++) {
        protoA = &a->Protocols[i];
        protoB = &b->Protocols[i];
        if (protoA->Major != protoB->Major ||
            protoA->Minor != protoB->Minor ||
            protoA->PortOffset != protoB->PortOffset ||
            protoA->PortCount != protoB->PortCount ||
            protoA->SlotType != protoB->SlotType ||
            protoA->PsiCount != protoB->PsiCount) {
            return 0;
        }
        for (j = 0; j < XHCI_MAX_PSI; j++) {
            if (protoA->Psi[j] != protoB->Psi[j]) {
                return 0;
            }
        }
    }

    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        if (a->Class[i] != b->Class[i] ||
            a->Protocol[i] != b->Protocol[i] ||
            a->Companion[i] != b->Companion[i]) {
            return 0;
        }
    }

    for (i = 0; i < XHCI_PORT_MAP_RESERVED; i++) {
        if (a->Reserved[i] != b->Reserved[i]) {
            return 0;
        }
    }

    return 1;
}

ULONG XhciPortClass(const XHCI_PORT_MAP *map, ULONG port)
{
    if (port == 0 || port > map->PortCount || port > XHCI_MAX_ROOT_PORTS) {
        return XHCI_PORT_CLASS_NONE;
    }
    return map->Class[port - 1];
}

ULONG XhciPortIsManaged(const XHCI_PORT_MAP *map, ULONG port)
{
    ULONG portClass;

    portClass = XhciPortClass(map, port);
    return (portClass == XHCI_PORT_CLASS_USB2_ONLY ||
            portClass == XHCI_PORT_CLASS_USB2_COMPANION) ? 1 : 0;
}

ULONG XhciPortSlotType(const XHCI_PORT_MAP *map, ULONG port, ULONG *slotType)
{
    if (slotType == NULL) {
        return XHCI_CAPS_BAD_PARAM;
    }
    if (port == 0 || port > map->PortCount || port > XHCI_MAX_ROOT_PORTS) {
        return XHCI_CAPS_NOT_FOUND;
    }
    if (map->Protocol[port - 1] == XHCI_PORT_NO_PROTOCOL) {
        return XHCI_CAPS_NOT_FOUND;
    }
    *slotType = map->Protocols[map->Protocol[port - 1]].SlotType;
    return XHCI_CAPS_OK;
}

/*
 * Normalize one PSI entry to kilobits per second. Kb/s rather than b/s
 * because SuperSpeed's 5 Gb/s does not fit in 32 bits of b/s, and 64-bit
 * arithmetic is banned (the compiler helpers may not exist on Win98's
 * kernel). Returns 0 for an entry that cannot be represented, which decodes
 * as an unknown speed rather than a wrong one.
 */
static ULONG xhciPsiKilobits(ULONG psi)
{
    ULONG psim;

    psim = XHCI_PSI_PSIM(psi);
    switch (XHCI_PSI_PSIE(psi)) {
    case 0:
        return psim / 1000;         /* bits/s   */
    case 1:
        return psim;                /* Kb/s     */
    case 2:
        return psim * 1000;         /* Mb/s     */
    default:
        if (psim > 4294UL) {        /* Gb/s     */
            return 0;
        }
        return psim * 1000000UL;
    }
}

static ULONG xhciSpeedClassFromKilobits(ULONG kbps)
{
    if (kbps == 1500UL) {
        return XHCI_SPEED_LOW;
    }
    if (kbps == 12000UL) {
        return XHCI_SPEED_FULL;
    }
    if (kbps == 480000UL) {
        return XHCI_SPEED_HIGH;
    }
    if (kbps >= 5000000UL) {
        return XHCI_SPEED_SUPER;
    }
    return XHCI_SPEED_UNKNOWN;
}

static ULONG xhciDefaultSpeedClass(ULONG psiv)
{
    switch (psiv) {
    case XHCI_PSIV_FS:
        return XHCI_SPEED_FULL;
    case XHCI_PSIV_LS:
        return XHCI_SPEED_LOW;
    case XHCI_PSIV_HS:
        return XHCI_SPEED_HIGH;
    case XHCI_PSIV_SS:
        return XHCI_SPEED_SUPER;
    default:
        return XHCI_SPEED_UNKNOWN;
    }
}

ULONG XhciPortSpeedClass(const XHCI_PORT_MAP *map,
                         ULONG port,
                         ULONG psiv,
                         ULONG *speedClass)
{
    const XHCI_PROTOCOL *proto;
    ULONG i;

    if (speedClass == NULL) {
        return XHCI_CAPS_BAD_PARAM;
    }
    if (port == 0 || port > map->PortCount || port > XHCI_MAX_ROOT_PORTS) {
        return XHCI_CAPS_NOT_FOUND;
    }
    if (map->Protocol[port - 1] == XHCI_PORT_NO_PROTOCOL) {
        return XHCI_CAPS_NOT_FOUND;
    }

    proto = &map->Protocols[map->Protocol[port - 1]];
    if (proto->PsiCount == 0) {
        *speedClass = xhciDefaultSpeedClass(psiv);
        return XHCI_CAPS_OK;
    }

    /*
     * The table replaces the defaults; it does not extend them. A PSIV the
     * controller did not advertise is unknown, and a driver that fell back to
     * "3 means High Speed" here would be guessing on exactly the controller
     * that reordered its IDs (docs/contributing/implementation-invariants.md, "Port Speed
     * Decoding").
     */
    for (i = 0; i < proto->PsiCount; i++) {
        if (XHCI_PSI_PSIV(proto->Psi[i]) == psiv) {
            *speedClass = xhciSpeedClassFromKilobits(
                xhciPsiKilobits(proto->Psi[i]));
            return XHCI_CAPS_OK;
        }
    }

    *speedClass = XHCI_SPEED_UNKNOWN;
    return XHCI_CAPS_OK;
}

static ULONG xhciDefaultPsiv(ULONG speedClass)
{
    switch (speedClass) {
    case XHCI_SPEED_FULL:
        return XHCI_PSIV_FS;
    case XHCI_SPEED_LOW:
        return XHCI_PSIV_LS;
    case XHCI_SPEED_HIGH:
        return XHCI_PSIV_HS;
    default:
        /*
         * SuperSpeed is deliberately absent rather than mapped to
         * XHCI_PSIV_SS: USB 3.0 is out of scope, a SuperSpeed port is left
         * unpowered by the port strategy, and answering here would let a
         * caller build a Slot Context for a device this driver has no path
         * to. XhciInitialMps0 refuses the same speed for the same reason.
         */
        return 0;
    }
}

ULONG XhciPortPsivForSpeed(const XHCI_PORT_MAP *map,
                           ULONG port,
                           ULONG speedClass,
                           ULONG *psiv)
{
    const XHCI_PROTOCOL *proto;
    ULONG value;
    ULONG i;

    if (psiv == NULL) {
        return XHCI_CAPS_BAD_PARAM;
    }
    /*
     * A speed this driver serves, asked **first**, because the table walk below
     * compares decoded classes and XHCI_SPEED_UNKNOWN is one of them: a caller
     * passing "unknown" would otherwise be handed the Protocol Speed ID of
     * whichever entry this driver could not decode.
     */
    value = xhciDefaultPsiv(speedClass);
    if (value == 0) {
        return XHCI_CAPS_NOT_FOUND;
    }
    if (port == 0 || port > map->PortCount || port > XHCI_MAX_ROOT_PORTS) {
        return XHCI_CAPS_NOT_FOUND;
    }
    if (map->Protocol[port - 1] == XHCI_PORT_NO_PROTOCOL) {
        return XHCI_CAPS_NOT_FOUND;
    }

    proto = &map->Protocols[map->Protocol[port - 1]];
    if (proto->PsiCount == 0) {
        *psiv = value;
        return XHCI_CAPS_OK;
    }

    /*
     * The table replaces the defaults in this direction too. A controller that
     * advertises a PSI table and no entry of this speed is one this driver
     * cannot describe a device of that speed to, and the honest answer is a
     * refusal - the caller fails the record, which is a yellow bang rather than
     * a device addressed at a speed the controller never named.
     */
    for (i = 0; i < proto->PsiCount; i++) {
        if (xhciSpeedClassFromKilobits(xhciPsiKilobits(proto->Psi[i])) ==
            speedClass) {
            *psiv = XHCI_PSI_PSIV(proto->Psi[i]);
            return XHCI_CAPS_OK;
        }
    }

    return XHCI_CAPS_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/* Capability-register derivation                                      */
/* ------------------------------------------------------------------ */

/*
 * Does a window of `mappedBytes` hold `need` bytes starting at `offset`?
 *
 * Written as two subtractions on purpose. The natural form,
 * `offset + need <= mappedBytes`, wraps to true for exactly the inputs this is
 * defending against: an undecoded RTSOFF reads back 0xFFFFFFE0 after masking,
 * and 0xFFFFFFE0 + 0x40 is 0x20 (docs/contributing/implementation-invariants.md, "MMIO
 * Sanity").
 */
static ULONG xhciWindowHolds(ULONG mappedBytes, ULONG offset, ULONG need)
{
    if (mappedBytes < need) {
        return 0;
    }
    return (offset <= (mappedBytes - need)) ? 1 : 0;
}

/*
 * XHCI_HC_INFO is all ULONGs, which is what lets both of these be loops. A
 * struct assignment would do the same thing, but MSVC 6.0 may emit a memcpy
 * call for it, and this driver decides its import list deliberately rather
 * than by codegen accident (the same reason xhci_dispatch.c zeroes the
 * registration packet by hand).
 */
XHCI_C_ASSERT(hc_info_is_whole_words,
              sizeof(XHCI_HC_INFO) % sizeof(ULONG) == 0);

static VOID xhciCopyHcInfo(PXHCI_HC_INFO dst, const XHCI_HC_INFO *src)
{
    ULONG *out;
    const ULONG *in;
    ULONG i;

    out = (ULONG *)dst;
    in = (const ULONG *)src;
    for (i = 0; i < sizeof(XHCI_HC_INFO) / sizeof(ULONG); i++) {
        out[i] = in[i];
    }
}

ULONG XhciCheckCapDword0(ULONG capDword0,
                         ULONG mappedBytes,
                         ULONG *capLength,
                         ULONG *hciVersion)
{
    ULONG length;
    ULONG version;

    /* Before believing anything read out of the window, the window has to be
     * big enough to hold the registers being read from it. */
    if (mappedBytes < XHCI_CAP_REGISTERS_BYTES) {
        return XHCI_HC_WINDOW_TOO_SMALL;
    }
    if (capDword0 == 0xFFFFFFFFUL) {
        return XHCI_HC_NOT_DECODING;
    }

    length = XHCI_CAPLENGTH_OF(capDword0);
    version = XHCI_HCIVERSION_OF(capDword0);

    /*
     * CAPLENGTH is where the operational registers start. Zero would place
     * them on top of the capability registers, and anything below
     * XHCI_CAP_REGISTERS_BYTES would overlap the ones this driver reads - so
     * the lower bound is the size of what is above it, not an arbitrary
     * minimum. 0xFF is the byte-wide form of a device that is not decoding.
     */
    if (length < XHCI_CAP_REGISTERS_BYTES || length == 0xFFUL) {
        return XHCI_HC_BAD_CAPLENGTH;
    }
    if (version == 0xFFFFUL || version < XHCI_HCIVERSION_1_0) {
        return XHCI_HC_BAD_VERSION;
    }

    if (capLength != NULL) {
        *capLength = length;
    }
    if (hciVersion != NULL) {
        *hciVersion = version;
    }
    return XHCI_HC_OK;
}

ULONG XhciDeriveHcInfo(ULONG capDword0,
                       ULONG hcsparams1,
                       ULONG hcsparams2,
                       ULONG hccparams1,
                       ULONG hccparams2,
                       ULONG dboff,
                       ULONG rtsoff,
                       ULONG mappedBytes,
                       PXHCI_HC_INFO info)
{
    XHCI_HC_INFO derived;
    ULONG status;
    ULONG runtime;
    ULONG doorbell;
    ULONG available;

    if (info == NULL) {
        return XHCI_HC_BAD_PARAM;
    }

    status = XhciCheckCapDword0(capDword0, mappedBytes,
                                &derived.CapLength, &derived.HciVersion);
    if (status != XHCI_HC_OK) {
        return status;
    }

    /*
     * Each of these is checked for all-ones separately rather than trusting
     * the field decode: HCSPARAMS1 of 0xFFFFFFFF decodes to 255 ports and 255
     * slots, which are legal values, so the field extraction cannot tell a
     * dead bus from a very large controller.
     */
    if (hcsparams1 == 0xFFFFFFFFUL || hcsparams2 == 0xFFFFFFFFUL ||
        hccparams1 == 0xFFFFFFFFUL) {
        return XHCI_HC_NOT_DECODING;
    }

    derived.MaxPorts = XHCI_HCSPARAMS1_MAXPORTS(hcsparams1);
    derived.MaxSlots = XHCI_HCSPARAMS1_MAXSLOTS(hcsparams1);
    derived.MaxIntrs = XHCI_HCSPARAMS1_MAXINTRS(hcsparams1);
    derived.ScratchpadCount = XHCI_HCSPARAMS2_MAXSCRATCHPAD(hcsparams2);
    /*
     * Task 9-A.1. Converted here rather than stored raw, so that there is one
     * place in the driver that knows bit 3 is a *unit selector* and not part of
     * the magnitude. "If bit [3] of IST is cleared to '0', software can add a
     * TRB no later than IST[2:0] Microframes before that TRB is scheduled to be
     * executed. If bit [3] of IST is set to '1', software can add a TRB no later
     * than IST[2:0] Frames" (5.3.4), and the Frame ID window rounds the
     * microframe case up to a whole frame (4.11.2.5 p.199). Reading the field as
     * a plain number would take a threshold of 7 microframes - under one frame -
     * and treat it as seven.
     */
    {
        ULONG ist;

        ist = XHCI_HCSPARAMS2_IST(hcsparams2);
        if ((ist & 0x8UL) != 0) {
            derived.IstFrames = ist & 0x7UL;
        } else {
            /* Round up: 0 microframes is 0 frames, 1-8 is 1. The encoded field
             * cannot exceed 7 here, so the addition cannot overflow. */
            derived.IstFrames = ((ist & 0x7UL) + 7UL) / 8UL;
        }
    }

    derived.ContextSize = XHCI_CONTEXT_SIZE_FROM_CSZ(hccparams1);
    derived.Ac64 = XHCI_HCCPARAMS1_AC64(hccparams1);
    derived.Ppc = XHCI_HCCPARAMS1_PPC(hccparams1);
    derived.Cfc = XHCI_HCCPARAMS1_CFC(hccparams1);
    derived.XecpDwords = XHCI_HCCPARAMS1_XECP(hccparams1);

    /*
     * FSC, and the gate is about **reach**, not about version.
     *
     * A first version of this also required HCIVERSION >= 1.10, on the reasoning
     * that HCCPARAMS2 arrived in xHCI 1.1 and that BAR0 + 1Ch therefore means
     * nothing on a 1.0 part. That reasoning is wrong, and the specification says
     * so in one line: Appendix H.1 lists the capabilities "that were optional for
     * xHCI 1.0 implementations [and] are now required in xHCI 1.1
     * implementations", and H.1.6 is the Force Save Context Capability (p.593) -
     * as are U3C (H.1.4), CTC (H.1.7) and CIC (H.1.8), three more bits of this
     * same register. The register is defined at 1.0 and a 1.0 controller may
     * legitimately advertise FSC. A version gate would not be conservatism; it
     * would force `FSC = 0` on hardware that just told us otherwise, and the cost
     * of a wrong `0` is that every resume reinitialises the bus.
     *
     * What remains is the ordinary reachability question. The register is outside
     * XHCI_CAP_REGISTERS_BYTES, so nothing above has proved it is there:
     *
     *   `CapLength` is the controller's own statement of where its capability
     *   block ends. One that stops at 1Ch puts the operational registers on top
     *   of this address, so there is no HCCPARAMS2 to read at all.
     *
     *   `mappedBytes` is the second line, against the window usbport handed over
     *   rather than against what the controller claims.
     *
     * An all-ones read is not treated as data, for the reason every other read in
     * this function is not: it is what an undecoding window answers, and bit 2 of
     * it is a 1 - which is the one direction of this decision that loses data.
     */
    derived.Fsc = 0;
    if (derived.CapLength >= XHCI_CAP_HCCPARAMS2_BYTES &&
        mappedBytes >= XHCI_CAP_HCCPARAMS2_BYTES &&
        hccparams2 != 0xFFFFFFFFUL) {
        derived.Fsc = XHCI_HCCPARAMS2_FSC(hccparams2);
    }

    if (derived.MaxPorts == 0) {
        return XHCI_HC_NO_PORTS;
    }
    if (derived.MaxSlots == 0) {
        return XHCI_HC_NO_SLOTS;
    }
    /* Interrupter 0 is the one this driver programs; a controller reporting
     * none has no event ring to give it. */
    if (derived.MaxIntrs == 0) {
        return XHCI_HC_NO_INTERRUPTERS;
    }

    derived.OperationalOffset = derived.CapLength;
    derived.PortscOffset = derived.OperationalOffset + XHCI_OP_PORTSC_BASE;

    /*
     * The operational window has to hold the fixed registers *and* the PORTSC
     * array, which is sized by MaxPorts. Stepwise so no term can overflow:
     * `available` is what lies above the operational base, PORTSC starts a
     * fixed distance into it, and what remains has to divide into MaxPorts
     * registers.
     */
    if (!xhciWindowHolds(mappedBytes, derived.OperationalOffset,
                         XHCI_OP_REGISTERS_BYTES)) {
        return XHCI_HC_WINDOW_TOO_SMALL;
    }
    available = mappedBytes - derived.OperationalOffset;
    if (available < XHCI_OP_PORTSC_BASE) {
        return XHCI_HC_WINDOW_TOO_SMALL;
    }
    if (((available - XHCI_OP_PORTSC_BASE) / XHCI_OP_PORT_STRIDE) <
        derived.MaxPorts) {
        return XHCI_HC_WINDOW_TOO_SMALL;
    }

    /* RTSOFF's low five bits and DBOFF's low two are RsvdZ, not part of the
     * offset (docs/usb-xhci-info/xhci-data-structures.md section 2). */
    if (rtsoff == 0xFFFFFFFFUL || dboff == 0xFFFFFFFFUL) {
        return XHCI_HC_NOT_DECODING;
    }
    runtime = rtsoff & ~0x1FUL;
    doorbell = dboff & ~0x3UL;

    /*
     * Zero - or any offset inside the capability registers - would put the
     * runtime or doorbell block on top of registers this driver reads. That is
     * a refusal rather than a clamp: there is no reading of the spec under
     * which a controller means it.
     *
     * The bound is the capability block, deliberately not CAPLENGTH. Only the
     * capability registers have a fixed position; where a controller puts the
     * runtime and doorbell blocks relative to its operational registers is its
     * own business, and refusing an unusual-but-legal arrangement would be
     * worse than the overlap it would catch.
     */
    if (runtime < XHCI_CAP_REGISTERS_BYTES ||
        !xhciWindowHolds(mappedBytes, runtime, XHCI_RT_REGISTERS_BYTES)) {
        return XHCI_HC_BAD_RTSOFF;
    }
    derived.RuntimeOffset = runtime;

    /* The doorbell array is one DWORD per slot plus DB[0] for the command
     * ring, sized by the hardware's MaxSlots rather than by MaxSlotsEn: the
     * array exists at its full size whatever this driver enables. */
    if (doorbell < XHCI_CAP_REGISTERS_BYTES ||
        !xhciWindowHolds(mappedBytes, doorbell,
                         (derived.MaxSlots + 1UL) * XHCI_DB_STRIDE)) {
        return XHCI_HC_BAD_DBOFF;
    }
    derived.DoorbellOffset = doorbell;

    xhciCopyHcInfo(info, &derived);
    return XHCI_HC_OK;
}

ULONG XhciHcInfoEqual(const XHCI_HC_INFO *a, const XHCI_HC_INFO *b)
{
    const ULONG *wordsA;
    const ULONG *wordsB;
    ULONG i;

    if (a == NULL || b == NULL) {
        return 0;
    }
    /*
     * Field-by-field by iteration rather than by name: the structure is all
     * ULONGs, and a comparison written out by hand is one a later field
     * addition silently falls out of.
     */
    wordsA = (const ULONG *)a;
    wordsB = (const ULONG *)b;
    for (i = 0; i < sizeof(XHCI_HC_INFO) / sizeof(ULONG); i++) {
        if (wordsA[i] != wordsB[i]) {
            return 0;
        }
    }
    return 1;
}
