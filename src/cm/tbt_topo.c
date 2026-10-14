/*
 * tbt_topo.c - finding the Thunderbolt tunnel bridge and reading its port
 * (roadmap-usb4.md task 39-A.5; design record 21 sections 6.3 and 6.5).
 * tbt_topo.h has the contract, the register layouts and their citations.
 *
 * C89, DDK-free. IRQL: as tbt_topo.h says, any for the decoders and the
 * poll machine, the accessors' for the rest (PASSIVE_LEVEL in the driver).
 */

#include "tbt_topo.h"

#define XHCI_TBT_ALL_ONES       0xFFFFFFFFUL
#define XHCI_TBT_CAP_MIN        0x40UL
#define XHCI_TBT_CAP_MAX_WALK   48UL

/* One dword; on a failed access *value is all-ones and 0 is returned. */
static ULONG xhciTbtRead(PXHCI_TBT_CFG io, ULONG bdf, ULONG offset,
                         PULONG value)
{
    *value = XHCI_TBT_ALL_ONES;
    io->Reads++;
    if (!io->Read(io->Context, bdf, offset, value)) {
        *value = XHCI_TBT_ALL_ONES;
        return 0;
    }
    return 1;
}

/*
 * 1 when a function answers at `bdf`: the access was made and the vendor
 * is neither all-ones (no function) nor zero (which some chipsets return
 * for an absent function).
 */
static ULONG xhciTbtPresent(PXHCI_TBT_CFG io, ULONG bdf, PULONG id)
{
    ULONG v;
    ULONG vendor;

    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_ID, &v)) {
        *id = XHCI_TBT_ALL_ONES;
        return 0;
    }
    *id = v;
    vendor = v & 0xFFFFUL;
    return (vendor != 0xFFFFUL && vendor != 0UL) ? 1UL : 0UL;
}

/* The Header Type byte of a present function, all-ones on a failed read. */
static ULONG xhciTbtHeader(PXHCI_TBT_CFG io, ULONG bdf)
{
    ULONG v;

    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_HEADER, &v) ||
        v == XHCI_TBT_ALL_ONES) {
        return XHCI_TBT_ALL_ONES;
    }
    return (v >> 16) & 0xFFUL;
}

static VOID xhciTbtZero(PULONG words, ULONG count)
{
    ULONG i;

    for (i = 0; i < count; i++) {
        words[i] = 0;
    }
}

/* ---- decoders ---------------------------------------------------------- */

ULONG XhciTbtPcieDownstream(ULONG pcieCaps)
{
    ULONG type = XHCI_TBT_PCIE_TYPE(pcieCaps);

    return (type == XHCI_TBT_PCIE_TYPE_ROOT ||
            type == XHCI_TBT_PCIE_TYPE_DOWN) ? 1UL : 0UL;
}

ULONG XhciTbtPcieSlotImplemented(ULONG pcieCaps)
{
    if (!XhciTbtPcieDownstream(pcieCaps)) {
        return 0;
    }
    return (pcieCaps & XHCI_TBT_PCIE_SLOT_IMPL) ? 1UL : 0UL;
}

VOID XhciTbtDecodeLink(ULONG pcieCaps, ULONG linkCap, ULONG linkStatus,
                       PULONG valid, PULONG active)
{
    *valid = 0;
    *active = 0;
    if (!XhciTbtPcieDownstream(pcieCaps) ||
        !(linkCap & XHCI_TBT_LINKCAP_DLLLARC)) {
        return;
    }
    *valid = 1;
    *active = (linkStatus & XHCI_TBT_LINKSTA_DLLLA) ? 1UL : 0UL;
}

VOID XhciTbtDecodeSlot(ULONG pcieCaps, ULONG slotStatus, PULONG valid,
                       PULONG present, PULONG changed, PULONG dllChanged)
{
    *valid = 0;
    *present = 0;
    *changed = 0;
    *dllChanged = 0;
    if (!XhciTbtPcieSlotImplemented(pcieCaps)) {
        return;
    }
    *valid = 1;
    *present = (slotStatus & XHCI_TBT_SLOTSTA_PDS) ? 1UL : 0UL;
    *changed = (slotStatus & XHCI_TBT_SLOTSTA_PDC) ? 1UL : 0UL;
    *dllChanged = (slotStatus & XHCI_TBT_SLOTSTA_DLLSC) ? 1UL : 0UL;
}

ULONG XhciTbtPcieHotPlug(ULONG pcieCaps, ULONG slotCap)
{
    if (!XhciTbtPcieSlotImplemented(pcieCaps)) {
        return 0;
    }
    return (slotCap & XHCI_TBT_SLOTCAP_HPC) ? 1UL : 0UL;
}

ULONG XhciTbtShpcSlotPresent(ULONG slotRegister)
{
    return (XHCI_TBT_SHPC_PRSNT(slotRegister) != XHCI_TBT_SHPC_PRSNT_EMPTY)
           ? 1UL : 0UL;
}

/* ---- capability walk --------------------------------------------------- */

ULONG XhciTbtFindCap(PXHCI_TBT_CFG io, ULONG bdf, ULONG capId,
                     PULONG offset)
{
    ULONG v;
    ULONG ptr;
    ULONG walked;

    if (io == NULL || io->Read == NULL || offset == NULL) {
        return 0;
    }
    *offset = 0;
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_STATUS, &v) ||
        v == XHCI_TBT_ALL_ONES || !(v & XHCI_TBT_CFG_STATUS_CAPLIST)) {
        return 0;
    }
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_CAPPTR, &v) ||
        v == XHCI_TBT_ALL_ONES) {
        return 0;
    }
    ptr = v & 0xFCUL;
    for (walked = 0; walked < XHCI_TBT_CAP_MAX_WALK; walked++) {
        if (ptr < XHCI_TBT_CAP_MIN) {
            return 0;
        }
        if (!xhciTbtRead(io, bdf, ptr, &v) || v == XHCI_TBT_ALL_ONES) {
            return 0;
        }
        if ((v & 0xFFUL) == capId) {
            *offset = ptr;
            return 1;
        }
        ptr = (v >> 8) & 0xFCUL;
    }
    return 0;
}

/* ---- discovery --------------------------------------------------------- */

static ULONG xhciTbtVisited(const XHCI_TBT_TOPO *topo, ULONG bus)
{
    return (topo->Visited[bus >> 5] >> (bus & 31UL)) & 1UL;
}

static VOID xhciTbtVisit(PXHCI_TBT_TOPO topo, ULONG bus)
{
    topo->Visited[bus >> 5] |= 1UL << (bus & 31UL);
}

/*
 * The number of functions to look at in a device whose function 0 answers
 * with header type `hdr0`: eight for a multi-function device, else one.
 */
static ULONG xhciTbtFunctionCount(ULONG hdr0)
{
    if (hdr0 == XHCI_TBT_ALL_ONES) {
        return 1;
    }
    return (hdr0 & XHCI_TBT_HDR_MF) ? 8UL : 1UL;
}

/*
 * Walks one bus for type 1 functions. For each, the
 * 18h dword is read; with `wantSecondary` below 256 a bridge whose
 * secondary bus is that number ends the walk (its location in *found).
 * With `push` set, each bridge whose secondary bus is above this bus and
 * not yet visited is pushed for the parent search. Returns 1 when found.
 */
static ULONG xhciTbtScanBus(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo,
                            ULONG bus, ULONG wantSecondary, ULONG push,
                            PULONG sp, PULONG found)
{
    ULONG dev;
    ULONG fn;
    ULONG count;
    ULONG bdf;
    ULONG id;
    ULONG hdr;
    ULONG buses;
    ULONG secondary;

    for (dev = 0; dev < 32; dev++) {
        if (!xhciTbtPresent(io, XHCI_TBT_BDF(bus, dev, 0), &id)) {
            continue;
        }
        count = xhciTbtFunctionCount(
            xhciTbtHeader(io, XHCI_TBT_BDF(bus, dev, 0)));
        for (fn = 0; fn < count; fn++) {
            bdf = XHCI_TBT_BDF(bus, dev, fn);
            if (fn != 0 && !xhciTbtPresent(io, bdf, &id)) {
                continue;
            }
            hdr = xhciTbtHeader(io, bdf);
            if (hdr == XHCI_TBT_ALL_ONES ||
                XHCI_TBT_HDR_LAYOUT(hdr) != XHCI_TBT_HDR_BRIDGE) {
                continue;
            }
            if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_BUSES, &buses) ||
                buses == XHCI_TBT_ALL_ONES) {
                continue;
            }
            secondary = (buses >> 8) & 0xFFUL;
            if (wantSecondary < 256UL && secondary == wantSecondary) {
                *found = bdf;
                return 1;
            }
            /*
             * Only downward: a bridge whose secondary is not above its own
             * bus is unconfigured (secondary 0) or misprogrammed, and
             * following it could loop.
             */
            if (push && secondary > bus && !xhciTbtVisited(topo, secondary)) {
                xhciTbtVisit(topo, secondary);
                topo->Stack[*sp] = (UCHAR)secondary;
                (*sp)++;
            }
        }
    }
    return 0;
}

/*
 * The NHI's parent: the type 1 function whose secondary bus is the NHI's
 * bus. Depth first from bus 0 through each bridge's secondary bus, which
 * reads only the buses that exist; then, for a NHI on a hierarchy bus 0
 * does not reach, every bus not yet read. XHCI_TBT_NO_BDF when there is
 * none: the NHI is on a root bus.
 */
static ULONG xhciTbtFindParent(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo,
                               ULONG nhiBus)
{
    ULONG sp = 0;
    ULONG bus;
    ULONG found = XHCI_TBT_NO_BDF;

    xhciTbtZero(topo->Visited, 8);
    if (nhiBus == 0) {
        return XHCI_TBT_NO_BDF;
    }
    xhciTbtVisit(topo, 0);
    topo->Stack[sp++] = 0;
    while (sp > 0) {
        sp--;
        bus = topo->Stack[sp];
        if (xhciTbtScanBus(io, topo, bus, nhiBus, 1, &sp, &found)) {
            return found;
        }
    }
    for (bus = 1; bus < 256UL; bus++) {
        if (xhciTbtVisited(topo, bus)) {
            continue;
        }
        if (xhciTbtScanBus(io, topo, bus, nhiBus, 0, &sp, &found)) {
            return found;
        }
    }
    return XHCI_TBT_NO_BDF;
}

/* What a type 1 function carries, for the pick and the log. */
static VOID xhciTbtClassify(PXHCI_TBT_CFG io, ULONG bdf, ULONG id,
                            PXHCI_TBT_BRIDGE br)
{
    ULONG v;
    ULONG off;

    xhciTbtZero((PULONG)br, sizeof(*br) / sizeof(ULONG));
    br->Bdf = bdf;
    br->Id = id;
    xhciTbtRead(io, bdf, XHCI_TBT_CFG_BUSES, &br->Buses);
    if (XhciTbtFindCap(io, bdf, XHCI_TBT_CAP_PCIE, &off) &&
        xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_CAPS, &v) &&
        v != XHCI_TBT_ALL_ONES) {
        br->PcieCap = off;
        br->PcieCaps = (v >> 16) & 0xFFFFUL;
        br->Flags |= XHCI_TBT_BR_PCIE;
        if (XhciTbtPcieSlotImplemented(br->PcieCaps)) {
            br->Flags |= XHCI_TBT_BR_SLOT;
            if (xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_SLOTCAP, &v) &&
                v != XHCI_TBT_ALL_ONES) {
                br->SlotCap = v;
                if (XhciTbtPcieHotPlug(br->PcieCaps, v)) {
                    br->Flags |= XHCI_TBT_BR_HOTPLUG;
                }
                if (v & XHCI_TBT_SLOTCAP_HPS) {
                    br->Flags |= XHCI_TBT_BR_SURPRISE;
                }
            }
        }
        if (XhciTbtPcieDownstream(br->PcieCaps) &&
            xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_LINKCAP, &v) &&
            v != XHCI_TBT_ALL_ONES && (v & XHCI_TBT_LINKCAP_DLLLARC)) {
            br->Flags |= XHCI_TBT_BR_LINKREPORT;
        }
    }
    if (XhciTbtFindCap(io, bdf, XHCI_TBT_CAP_SHPC, &off)) {
        br->ShpcCap = off;
        br->Flags |= XHCI_TBT_BR_SHPC;
    }
    if (br->Flags & (XHCI_TBT_BR_HOTPLUG | XHCI_TBT_BR_SHPC)) {
        br->Flags |= XHCI_TBT_BR_CANDIDATE;
    }
}

static ULONG xhciTbtListed(const XHCI_TBT_TOPO_IN *in, ULONG id)
{
    ULONG i;

    if (in->RootPortIds == NULL) {
        return 0;
    }
    for (i = 0; i < in->RootPortIdCount; i++) {
        if (in->RootPortIds[i] == id) {
            return 1;
        }
    }
    return 0;
}

/* Appends a tunnel port to the set; 0 when the set is full. */
static ULONG xhciTbtSetAdd(PXHCI_TBT_TOPO topo, const XHCI_TBT_BRIDGE *br,
                           ULONG source)
{
    if (topo->Tunnels >= XHCI_TBT_MAX_TUNNELS) {
        return 0;
    }
    if (topo->Tunnels == 0) {
        topo->Pick = *br;
        topo->Tunnel = br->Bdf;
    }
    topo->Set[topo->Tunnels] = br->Bdf;
    topo->Source[topo->Tunnels] = source;
    topo->Last[topo->Tunnels] = XHCI_TBT_PORT_ERROR;
    topo->Tunnels++;
    return 1;
}

static VOID xhciTbtSetClear(PXHCI_TBT_TOPO topo)
{
    ULONG i;

    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        topo->Set[i] = XHCI_TBT_NO_BDF;
        topo->Source[i] = 0;
        topo->Last[i] = XHCI_TBT_PORT_ERROR;
    }
    topo->Tunnels = 0;
    topo->Tunnel = XHCI_TBT_NO_BDF;
    xhciTbtZero((PULONG)&topo->Pick, sizeof(topo->Pick) / sizeof(ULONG));
}

/*
 * The type 1 siblings on topo->SearchBus, the parent excluded, and the
 * set taken from them (XhciTbtTopoFind's rule).
 */
static VOID xhciTbtSiblings(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO_IN *in,
                            PXHCI_TBT_TOPO topo)
{
    ULONG bus = topo->SearchBus;
    ULONG dev;
    ULONG fn;
    ULONG count;
    ULONG bdf;
    ULONG id;
    ULONG hdr;
    ULONG overflow = 0;
    XHCI_TBT_BRIDGE br;

    for (dev = 0; dev < 32; dev++) {
        if (!xhciTbtPresent(io, XHCI_TBT_BDF(bus, dev, 0), &id)) {
            continue;
        }
        count = xhciTbtFunctionCount(
            xhciTbtHeader(io, XHCI_TBT_BDF(bus, dev, 0)));
        for (fn = 0; fn < count; fn++) {
            bdf = XHCI_TBT_BDF(bus, dev, fn);
            if (!xhciTbtPresent(io, bdf, &id)) {
                continue;
            }
            hdr = xhciTbtHeader(io, bdf);
            if (hdr == XHCI_TBT_ALL_ONES ||
                XHCI_TBT_HDR_LAYOUT(hdr) != XHCI_TBT_HDR_BRIDGE ||
                bdf == topo->Parent) {
                continue;
            }
            xhciTbtClassify(io, bdf, id, &br);
            if ((br.Flags & XHCI_TBT_BR_CANDIDATE) && xhciTbtListed(in, id)) {
                br.Flags |= XHCI_TBT_BR_LISTED;
            }
            if (topo->Siblings < XHCI_TBT_MAX_SIBLINGS) {
                topo->Sibling[topo->Siblings] = br;
            }
            topo->Siblings++;
            if (!(br.Flags & XHCI_TBT_BR_CANDIDATE)) {
                continue;
            }
            topo->Candidates++;
            if (!topo->RootSide) {
                if (!xhciTbtSetAdd(topo, &br, XHCI_TBT_SRC_SWITCH)) {
                    overflow = 1;
                }
            } else if (br.Flags & XHCI_TBT_BR_LISTED) {
                topo->Listed++;
                if (!xhciTbtSetAdd(topo, &br, XHCI_TBT_SRC_TABLE)) {
                    overflow = 1;
                }
            }
        }
    }

    if (topo->Candidates == 0) {
        xhciTbtSetClear(topo);
        topo->Outcome = XHCI_TBT_TOPO_NO_CANDIDATE;
    } else if (overflow) {
        xhciTbtSetClear(topo);
        topo->Outcome = XHCI_TBT_TOPO_MANY;
    } else if (topo->Tunnels == 0) {
        topo->Outcome = XHCI_TBT_TOPO_LEARN;
    } else {
        topo->Outcome = XHCI_TBT_TOPO_SET;
    }
}

ULONG XhciTbtTopoFind(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO_IN *in,
                      PXHCI_TBT_TOPO topo)
{
    ULONG id;
    ULONG hdr;
    ULONG i;
    ULONG v;
    ULONG off;
    XHCI_TBT_BRIDGE br;

    if (topo == NULL) {
        return XHCI_TBT_TOPO_BAD_PARAM;
    }
    xhciTbtZero((PULONG)topo, sizeof(*topo) / sizeof(ULONG));
    topo->Parent = XHCI_TBT_NO_BDF;
    xhciTbtSetClear(topo);
    if (in == NULL) {
        topo->Outcome = XHCI_TBT_TOPO_BAD_PARAM;
        return topo->Outcome;
    }
    topo->Nhi = in->Nhi;
    if (io == NULL || io->Read == NULL || in->Nhi > 0xFFFFUL ||
        in->Overrides > XHCI_TBT_MAX_TUNNELS) {
        topo->Outcome = XHCI_TBT_TOPO_BAD_PARAM;
        return topo->Outcome;
    }
    for (i = 0; i < in->Overrides; i++) {
        if (in->Override[i] > 0xFFFFUL) {
            topo->Outcome = XHCI_TBT_TOPO_BAD_PARAM;
            return topo->Outcome;
        }
    }
    if (!xhciTbtPresent(io, in->Nhi, &id)) {
        topo->Outcome = XHCI_TBT_TOPO_NHI_ABSENT;
        return topo->Outcome;
    }

    topo->Parent = xhciTbtFindParent(io, topo, XHCI_TBT_BDF_BUS(in->Nhi));
    topo->RootSide = 1;
    if (topo->Parent != XHCI_TBT_NO_BDF) {
        topo->SearchBus = XHCI_TBT_BDF_BUS(topo->Parent);
        if (XhciTbtFindCap(io, topo->Parent, XHCI_TBT_CAP_PCIE, &off) &&
            xhciTbtRead(io, topo->Parent, off + XHCI_TBT_PCIE_CAPS, &v) &&
            v != XHCI_TBT_ALL_ONES) {
            topo->ParentType = XHCI_TBT_PCIE_TYPE(v >> 16);
        }
        topo->RootSide = topo->ParentType == XHCI_TBT_PCIE_TYPE_ROOT
                         ? 1UL : 0UL;
    } else {
        topo->SearchBus = XHCI_TBT_BDF_BUS(in->Nhi);
    }
    xhciTbtSiblings(io, in, topo);

    if (in->Overrides == 0) {
        return topo->Outcome;
    }
    xhciTbtSetClear(topo);
    for (i = 0; i < in->Overrides; i++) {
        hdr = XHCI_TBT_ALL_ONES;
        if (xhciTbtPresent(io, in->Override[i], &id)) {
            hdr = xhciTbtHeader(io, in->Override[i]);
        }
        if (hdr == XHCI_TBT_ALL_ONES ||
            XHCI_TBT_HDR_LAYOUT(hdr) != XHCI_TBT_HDR_BRIDGE) {
            xhciTbtSetClear(topo);
            topo->Outcome = XHCI_TBT_TOPO_OVERRIDE_BAD;
            return topo->Outcome;
        }
        xhciTbtClassify(io, in->Override[i], id, &br);
        xhciTbtSetAdd(topo, &br, XHCI_TBT_SRC_OVERRIDE);
    }
    topo->Outcome = XHCI_TBT_TOPO_OVERRIDE;
    return topo->Outcome;
}

/*
 * 1 when learning may add to this domain's set: nothing was verified by
 * discovery or the override, and there is room.
 */
static ULONG xhciTbtLearnable(const XHCI_TBT_TOPO *topo)
{
    ULONG i;

    if (topo->Outcome != XHCI_TBT_TOPO_LEARN &&
        topo->Outcome != XHCI_TBT_TOPO_SET) {
        return 0;
    }
    if (topo->Tunnels >= XHCI_TBT_MAX_TUNNELS) {
        return 0;
    }
    for (i = 0; i < topo->Tunnels; i++) {
        if (topo->Source[i] != XHCI_TBT_SRC_LEARNED) {
            return 0;
        }
    }
    return 1;
}

static ULONG xhciTbtInSet(const XHCI_TBT_TOPO *topo, ULONG bdf)
{
    ULONG i;

    for (i = 0; i < topo->Tunnels; i++) {
        if (topo->Set[i] == bdf) {
            return 1;
        }
    }
    return 0;
}

static ULONG xhciTbtKept(const XHCI_TBT_TOPO *topo)
{
    return topo->Siblings < XHCI_TBT_MAX_SIBLINGS ? topo->Siblings
                                                  : XHCI_TBT_MAX_SIBLINGS;
}

VOID XhciTbtLearnArm(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo)
{
    ULONG i;
    XHCI_TBT_PORT port;

    if (topo == NULL) {
        return;
    }
    topo->LearnArmed = 0;
    if (io == NULL || io->Read == NULL || !xhciTbtLearnable(topo)) {
        return;
    }
    for (i = 0; i < xhciTbtKept(topo); i++) {
        topo->Before[i] = XHCI_TBT_PORT_ERROR;
        if (topo->Sibling[i].Flags & XHCI_TBT_BR_CANDIDATE) {
            topo->Before[i] = XhciTbtPortRead(io, topo->Sibling[i].Bdf,
                                              &port);
        }
    }
    topo->LearnArmed = 1;
}

ULONG XhciTbtLearn(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo)
{
    ULONG i;
    ULONG found = XHCI_TBT_MAX_SIBLINGS;
    ULONG rose = 0;
    XHCI_TBT_PORT port;

    if (topo == NULL) {
        return 0;
    }
    if (io == NULL || io->Read == NULL || !topo->LearnArmed ||
        !xhciTbtLearnable(topo)) {
        topo->LearnArmed = 0;
        return 0;
    }
    topo->LearnArmed = 0;
    for (i = 0; i < xhciTbtKept(topo); i++) {
        if (!(topo->Sibling[i].Flags & XHCI_TBT_BR_CANDIDATE) ||
            xhciTbtInSet(topo, topo->Sibling[i].Bdf)) {
            continue;
        }
        if (XhciTbtPortRead(io, topo->Sibling[i].Bdf, &port) ==
                XHCI_TBT_PORT_UP &&
            topo->Before[i] != XHCI_TBT_PORT_UP) {
            rose++;
            found = i;
        }
    }
    if (rose != 1) {
        return 0;
    }
    xhciTbtSetAdd(topo, &topo->Sibling[found], XHCI_TBT_SRC_LEARNED);
    topo->Outcome = XHCI_TBT_TOPO_SET;
    return 1;
}

static ULONG xhciTbtHex(WCHAR c)
{
    if (c >= '0' && c <= '9') {
        return (ULONG)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (ULONG)(c - 'a') + 10UL;
    }
    if (c >= 'A' && c <= 'F') {
        return (ULONG)(c - 'A') + 10UL;
    }
    return 16;
}

/*
 * One to `most` hex digits from text[*i] on; 1 and the number in *value,
 * or 0 when there is no digit.
 */
static ULONG xhciTbtHexField(PCWSTR text, ULONG chars, PULONG i, ULONG most,
                             PULONG value)
{
    ULONG n = 0;
    ULONG d;

    *value = 0;
    while (*i < chars && n < most) {
        d = xhciTbtHex(text[*i]);
        if (d > 15) {
            break;
        }
        *value = (*value << 4) | d;
        (*i)++;
        n++;
    }
    return n != 0 ? 1UL : 0UL;
}

ULONG XhciTbtParseBdfList(PCWSTR text, ULONG chars, PULONG list,
                          ULONG capacity)
{
    ULONG i = 0;
    ULONG n;
    ULONG count = 0;
    ULONG bus;
    ULONG dev;
    ULONG fn;

    if (text == NULL || list == NULL || capacity == 0) {
        return 0;
    }
    for (n = 0; n < chars; n++) {
        if (text[n] == 0) {
            break;
        }
    }
    chars = n;
    for (;;) {
        if (count >= capacity || count >= XHCI_TBT_MAX_TUNNELS) {
            return 0;
        }
        if (!xhciTbtHexField(text, chars, &i, 2, &bus) ||
            i >= chars || text[i] != ':') {
            return 0;
        }
        i++;
        if (!xhciTbtHexField(text, chars, &i, 2, &dev) || dev > 0x1FUL ||
            i >= chars || text[i] != '.') {
            return 0;
        }
        i++;
        if (!xhciTbtHexField(text, chars, &i, 1, &fn) || fn > 7UL) {
            return 0;
        }
        list[count++] = XHCI_TBT_BDF(bus, dev, fn);
        if (i == chars) {
            return count;
        }
        if (text[i] != ',') {
            return 0;
        }
        i++;
    }
}

/* ---- the port reading -------------------------------------------------- */

/*
 * The SHPC slots of the bridge at `bdf`, whose capability is at `cap`.
 * Returns XHCI_TBT_PORT_UP/DOWN as a reading, NONE when there are no
 * slots, GONE on an all-ones dword, ERROR on a failed access. The DWORD
 * Select byte found there is written back whatever happened.
 */
static ULONG xhciTbtShpcRead(PXHCI_TBT_CFG io, ULONG bdf, ULONG cap,
                             PXHCI_TBT_PORT port)
{
    ULONG v;
    ULONG saved;
    ULONG slots;
    ULONG n;
    ULONG result = XHCI_TBT_PORT_NONE;

    if (!xhciTbtRead(io, bdf, cap, &v)) {
        return XHCI_TBT_PORT_ERROR;
    }
    if (v == XHCI_TBT_ALL_ONES) {
        return XHCI_TBT_PORT_GONE;
    }
    saved = (v >> 16) & 0xFFUL;

    io->Writes++;
    if (!io->Write(io->Context, bdf, cap + XHCI_TBT_SHPC_SELECT, 1,
                   XHCI_TBT_SHPC_SLOTCFG)) {
        result = XHCI_TBT_PORT_ERROR;
        goto restore;
    }
    if (!xhciTbtRead(io, bdf, cap + XHCI_TBT_SHPC_DATA, &v)) {
        result = XHCI_TBT_PORT_ERROR;
        goto restore;
    }
    if (v == XHCI_TBT_ALL_ONES) {
        result = XHCI_TBT_PORT_GONE;
        goto restore;
    }
    slots = XHCI_TBT_SHPC_NSI(v);
    port->ShpcSlots = slots;
    for (n = 0; n < slots; n++) {
        io->Writes++;
        if (!io->Write(io->Context, bdf, cap + XHCI_TBT_SHPC_SELECT, 1,
                       XHCI_TBT_SHPC_SLOT0 + n)) {
            result = XHCI_TBT_PORT_ERROR;
            goto restore;
        }
        if (!xhciTbtRead(io, bdf, cap + XHCI_TBT_SHPC_DATA, &v)) {
            result = XHCI_TBT_PORT_ERROR;
            goto restore;
        }
        if (v == XHCI_TBT_ALL_ONES) {
            result = XHCI_TBT_PORT_GONE;
            goto restore;
        }
        if (XhciTbtShpcSlotPresent(v)) {
            if (port->ShpcPresent == 0) {
                port->ShpcFirst = n + 1;
            }
            port->ShpcPresent++;
        }
    }
    if (slots != 0) {
        port->ShpcValid = 1;
        result = port->ShpcPresent != 0 ? XHCI_TBT_PORT_UP
                                        : XHCI_TBT_PORT_DOWN;
    }

restore:
    io->Writes++;
    io->Write(io->Context, bdf, cap + XHCI_TBT_SHPC_SELECT, 1, saved);
    return result;
}

/* Folds one valid reading (1 up, 0 down) into the running tally. */
static VOID xhciTbtVote(ULONG up, PULONG ups, PULONG downs)
{
    if (up) {
        (*ups)++;
    } else {
        (*downs)++;
    }
}

ULONG XhciTbtPortRead(PXHCI_TBT_CFG io, ULONG bdf, PXHCI_TBT_PORT port)
{
    ULONG id;
    ULONG v;
    ULONG off;
    ULONG ups = 0;
    ULONG downs = 0;
    ULONG shpc;

    if (port == NULL) {
        return XHCI_TBT_PORT_ERROR;
    }
    xhciTbtZero((PULONG)port, sizeof(*port) / sizeof(ULONG));
    if (io == NULL || io->Read == NULL || bdf > 0xFFFFUL) {
        port->Verdict = XHCI_TBT_PORT_ERROR;
        return port->Verdict;
    }
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_ID, &id)) {
        port->Verdict = XHCI_TBT_PORT_ERROR;
        return port->Verdict;
    }
    port->Id = id;
    if ((id & 0xFFFFUL) == 0xFFFFUL || (id & 0xFFFFUL) == 0UL) {
        port->Verdict = XHCI_TBT_PORT_GONE;
        return port->Verdict;
    }

    if (XhciTbtFindCap(io, bdf, XHCI_TBT_CAP_PCIE, &off)) {
        if (!xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_CAPS, &v)) {
            goto error;
        }
        if (v == XHCI_TBT_ALL_ONES) {
            goto gone;
        }
        port->PcieCap = off;
        port->PcieCaps = (v >> 16) & 0xFFFFUL;
        if (XhciTbtPcieDownstream(port->PcieCaps)) {
            if (!xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_LINKCAP,
                             &port->LinkCap) ||
                !xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_LINKSTA, &v)) {
                goto error;
            }
            if (port->LinkCap == XHCI_TBT_ALL_ONES ||
                v == XHCI_TBT_ALL_ONES) {
                goto gone;
            }
            port->LinkStatus = (v >> 16) & 0xFFFFUL;
            XhciTbtDecodeLink(port->PcieCaps, port->LinkCap, port->LinkStatus,
                              &port->LinkValid, &port->LinkActive);
            if (port->LinkValid) {
                xhciTbtVote(port->LinkActive, &ups, &downs);
            }
        }
        if (XhciTbtPcieSlotImplemented(port->PcieCaps)) {
            if (!xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_SLOTSTA, &v)) {
                goto error;
            }
            if (v == XHCI_TBT_ALL_ONES) {
                goto gone;
            }
            port->SlotStatus = (v >> 16) & 0xFFFFUL;
            XhciTbtDecodeSlot(port->PcieCaps, port->SlotStatus,
                              &port->SlotValid, &port->Presence,
                              &port->PresenceChanged, &port->DllChanged);
            if (port->SlotValid) {
                xhciTbtVote(port->Presence, &ups, &downs);
            }
        }
    }

    if (XhciTbtFindCap(io, bdf, XHCI_TBT_CAP_SHPC, &off)) {
        port->ShpcCap = off;
        if (io->Write != NULL) {
            shpc = xhciTbtShpcRead(io, bdf, off, port);
            if (shpc == XHCI_TBT_PORT_ERROR) {
                goto error;
            }
            if (shpc == XHCI_TBT_PORT_GONE) {
                goto gone;
            }
            if (port->ShpcValid) {
                xhciTbtVote(shpc == XHCI_TBT_PORT_UP ? 1UL : 0UL,
                            &ups, &downs);
            }
        }
    }

    if (ups == 0 && downs == 0) {
        port->Verdict = XHCI_TBT_PORT_NONE;
    } else if (downs == 0) {
        port->Verdict = XHCI_TBT_PORT_UP;
    } else if (ups == 0) {
        port->Verdict = XHCI_TBT_PORT_DOWN;
    } else {
        port->Verdict = XHCI_TBT_PORT_MIXED;
    }
    return port->Verdict;

error:
    port->Verdict = XHCI_TBT_PORT_ERROR;
    return port->Verdict;

gone:
    port->Verdict = XHCI_TBT_PORT_GONE;
    return port->Verdict;
}

ULONG XhciTbtSetVerdict(const ULONG *verdicts, ULONG count, ULONG want)
{
    ULONG i;
    ULONG up = 0;
    ULONG down = 0;
    ULONG gone = 0;
    ULONG none = 0;
    ULONG error = 0;

    if (verdicts == NULL || count == 0) {
        return XHCI_TBT_PORT_NONE;
    }
    for (i = 0; i < count; i++) {
        if (verdicts[i] == XHCI_TBT_PORT_UP) {
            up++;
        } else if (verdicts[i] == XHCI_TBT_PORT_DOWN) {
            down++;
        } else if (verdicts[i] == XHCI_TBT_PORT_GONE) {
            gone++;
        } else if (verdicts[i] == XHCI_TBT_PORT_NONE) {
            none++;
        } else if (verdicts[i] == XHCI_TBT_PORT_ERROR) {
            error++;
        }
    }
    if (want == XHCI_TBT_WANT_UP && up != 0) {
        return XHCI_TBT_PORT_UP;
    }
    if (want == XHCI_TBT_WANT_DOWN && down + gone + none == count) {
        if (down != 0) {
            return XHCI_TBT_PORT_DOWN;
        }
        if (gone != 0) {
            return XHCI_TBT_PORT_GONE;
        }
    }
    if (error != 0) {
        return XHCI_TBT_PORT_ERROR;
    }
    if (none == count) {
        return XHCI_TBT_PORT_NONE;
    }
    return XHCI_TBT_PORT_MIXED;
}

ULONG XhciTbtSetRead(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo, ULONG want)
{
    ULONG i;
    XHCI_TBT_PORT port;

    if (topo == NULL || topo->Tunnels == 0) {
        return XHCI_TBT_PORT_NONE;
    }
    for (i = 0; i < topo->Tunnels; i++) {
        topo->Last[i] = XhciTbtPortRead(io, topo->Set[i], &port);
    }
    return XhciTbtSetVerdict(topo->Last, topo->Tunnels, want);
}

/* ---- the bounded poll -------------------------------------------------- */

ULONG XhciTbtPollBegin(PXHCI_TBT_POLL poll, ULONG want, ULONG nowMs,
                       ULONG budgetMs)
{
    if (poll == NULL) {
        return XHCI_TBT_POLL_BAD_PARAM;
    }
    xhciTbtZero((PULONG)poll, sizeof(*poll) / sizeof(ULONG));
    poll->Want = want;
    poll->Start = nowMs;
    poll->Budget = budgetMs != 0 ? budgetMs : XHCI_TBT_POLL_BUDGET_MS;
    poll->Last = XHCI_TBT_PORT_ERROR;
    if (want != XHCI_TBT_WANT_UP && want != XHCI_TBT_WANT_DOWN) {
        poll->State = XHCI_TBT_POLL_BAD_PARAM;
        return poll->State;
    }
    poll->State = XHCI_TBT_POLL_RUNNING;
    return poll->State;
}

static ULONG xhciTbtPollEnd(PXHCI_TBT_POLL poll, ULONG state, ULONG nowMs)
{
    poll->State = state;
    poll->Elapsed = nowMs - poll->Start;
    return state;
}

ULONG XhciTbtPollStep(PXHCI_TBT_POLL poll, ULONG verdict, ULONG nowMs)
{
    if (poll == NULL) {
        return XHCI_TBT_POLL_BAD_PARAM;
    }
    if (poll->State != XHCI_TBT_POLL_RUNNING) {
        return poll->State;
    }
    poll->Steps++;
    poll->Last = verdict;
    if (verdict == XHCI_TBT_PORT_NONE) {
        return xhciTbtPollEnd(poll, XHCI_TBT_POLL_NO_READING, nowMs);
    }
    if (poll->Want == XHCI_TBT_WANT_UP && verdict == XHCI_TBT_PORT_UP) {
        return xhciTbtPollEnd(poll, XHCI_TBT_POLL_REACHED, nowMs);
    }
    if (poll->Want == XHCI_TBT_WANT_DOWN) {
        if (verdict == XHCI_TBT_PORT_DOWN) {
            return xhciTbtPollEnd(poll, XHCI_TBT_POLL_REACHED, nowMs);
        }
        if (verdict == XHCI_TBT_PORT_GONE) {
            return xhciTbtPollEnd(poll, XHCI_TBT_POLL_GONE, nowMs);
        }
    }
    if (nowMs - poll->Start >= poll->Budget) {
        return xhciTbtPollEnd(poll, XHCI_TBT_POLL_TIMEOUT, nowMs);
    }
    return poll->State;
}

ULONG XhciTbtPollPort(PXHCI_TBT_CFG io, ULONG bdf, PXHCI_TBT_POLL poll,
                      PXHCI_TBT_PORT port, ULONG nowMs)
{
    if (poll == NULL || port == NULL) {
        return XHCI_TBT_POLL_BAD_PARAM;
    }
    if (poll->State != XHCI_TBT_POLL_RUNNING) {
        return poll->State;
    }
    return XhciTbtPollStep(poll, XhciTbtPortRead(io, bdf, port), nowMs);
}

ULONG XhciTbtPollAbandon(PXHCI_TBT_POLL poll, ULONG nowMs)
{
    if (poll == NULL) {
        return XHCI_TBT_POLL_BAD_PARAM;
    }
    if (poll->State != XHCI_TBT_POLL_RUNNING) {
        return poll->State;
    }
    return xhciTbtPollEnd(poll, XHCI_TBT_POLL_ABANDONED, nowMs);
}

ULONG XhciTbtPollWait(const XHCI_TBT_POLL *poll, ULONG nowMs)
{
    ULONG elapsed;
    ULONG left;

    if (poll == NULL || poll->State != XHCI_TBT_POLL_RUNNING) {
        return 0;
    }
    elapsed = nowMs - poll->Start;
    if (elapsed >= poll->Budget) {
        return 0;
    }
    left = poll->Budget - elapsed;
    return left < XHCI_TBT_POLL_STEP_MS ? left : XHCI_TBT_POLL_STEP_MS;
}
