/*
 * tbt_topo.c - finding a Thunderbolt domain's tunnel ports and reading
 * them (roadmap-usb4.md task 39-A.5; design record 21 sections 6.3 and
 * 6.5). tbt_topo.h has the contract, the register layouts and their
 * citations.
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

ULONG XhciTbtFindCapStatus(PXHCI_TBT_CFG io, ULONG bdf, ULONG capId,
                           PULONG offset)
{
    ULONG v;
    ULONG ptr;
    ULONG walked;

    if (offset != NULL) {
        *offset = 0;
    }
    if (io == NULL || io->Read == NULL || offset == NULL) {
        return XHCI_TBT_CAP_FAILED;
    }
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_STATUS, &v)) {
        return XHCI_TBT_CAP_FAILED;
    }
    if (v == XHCI_TBT_ALL_ONES) {
        return XHCI_TBT_CAP_GONE;
    }
    if (!(v & XHCI_TBT_CFG_STATUS_CAPLIST)) {
        return XHCI_TBT_CAP_ABSENT;
    }
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_CAPPTR, &v)) {
        return XHCI_TBT_CAP_FAILED;
    }
    if (v == XHCI_TBT_ALL_ONES) {
        return XHCI_TBT_CAP_GONE;
    }
    ptr = v & 0xFCUL;
    for (walked = 0; walked < XHCI_TBT_CAP_MAX_WALK; walked++) {
        if (ptr < XHCI_TBT_CAP_MIN) {
            return XHCI_TBT_CAP_ABSENT;
        }
        if (!xhciTbtRead(io, bdf, ptr, &v)) {
            return XHCI_TBT_CAP_FAILED;
        }
        if (v == XHCI_TBT_ALL_ONES) {
            return XHCI_TBT_CAP_GONE;
        }
        if ((v & 0xFFUL) == capId) {
            *offset = ptr;
            return XHCI_TBT_CAP_FOUND;
        }
        ptr = (v >> 8) & 0xFCUL;
    }
    return XHCI_TBT_CAP_FAILED;
}

ULONG XhciTbtFindCap(PXHCI_TBT_CFG io, ULONG bdf, ULONG capId,
                     PULONG offset)
{
    return XhciTbtFindCapStatus(io, bdf, capId, offset) == XHCI_TBT_CAP_FOUND
           ? 1UL : 0UL;
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
 * Walks one bus for type 1 functions. For each, the 18h dword is read; a
 * bridge whose secondary bus is `wantSecondary` ends the walk (its
 * location in *found). With `push` set, each bridge whose secondary bus is
 * above this bus and not yet visited is pushed for the search. Returns 1
 * when found.
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
            if (secondary == wantSecondary) {
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
 * The bridge whose secondary bus is `bus`. Depth first from bus 0 through
 * each bridge's secondary bus, which reads only the buses that exist;
 * then, for a bus that hierarchy does not reach, every bus not yet read.
 * XHCI_TBT_NO_BDF when there is none: `bus` is a root bus.
 */
static ULONG xhciTbtBridgeAbove(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo,
                                ULONG bus)
{
    ULONG sp = 0;
    ULONG b;
    ULONG found = XHCI_TBT_NO_BDF;

    xhciTbtZero(topo->Visited, 8);
    if (bus == 0) {
        return XHCI_TBT_NO_BDF;
    }
    xhciTbtVisit(topo, 0);
    topo->Stack[sp++] = 0;
    while (sp > 0) {
        sp--;
        b = topo->Stack[sp];
        if (xhciTbtScanBus(io, topo, b, bus, 1, &sp, &found)) {
            return found;
        }
    }
    for (b = 1; b < 256UL; b++) {
        if (xhciTbtVisited(topo, b)) {
            continue;
        }
        if (xhciTbtScanBus(io, topo, b, bus, 0, &sp, &found)) {
            return found;
        }
    }
    return XHCI_TBT_NO_BDF;
}

/* The PCI Express port type of the function at `bdf`, 0 when unread. */
static ULONG xhciTbtPortType(PXHCI_TBT_CFG io, ULONG bdf)
{
    ULONG off;
    ULONG v;

    if (bdf == XHCI_TBT_NO_BDF ||
        XhciTbtFindCapStatus(io, bdf, XHCI_TBT_CAP_PCIE, &off) !=
            XHCI_TBT_CAP_FOUND ||
        !xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_CAPS, &v) ||
        v == XHCI_TBT_ALL_ONES) {
        return 0;
    }
    return XHCI_TBT_PCIE_TYPE(v >> 16);
}

/* What a type 1 function carries, for the pick and the log. */
static VOID xhciTbtClassify(PXHCI_TBT_CFG io, ULONG bdf, ULONG id,
                            PXHCI_TBT_BRIDGE br)
{
    ULONG v;
    ULONG off;
    ULONG st;

    xhciTbtZero((PULONG)br, sizeof(*br) / sizeof(ULONG));
    br->Bdf = bdf;
    br->Id = id;
    xhciTbtRead(io, bdf, XHCI_TBT_CFG_BUSES, &br->Buses);
    st = XhciTbtFindCapStatus(io, bdf, XHCI_TBT_CAP_PCIE, &off);
    if (st == XHCI_TBT_CAP_FOUND) {
        if (!xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_CAPS, &v) ||
            v == XHCI_TBT_ALL_ONES) {
            br->Flags |= XHCI_TBT_BR_UNREAD;
        } else {
            br->PcieCap = off;
            br->PcieCaps = (v >> 16) & 0xFFFFUL;
            br->Flags |= XHCI_TBT_BR_PCIE;
            if (XhciTbtPcieSlotImplemented(br->PcieCaps)) {
                br->Flags |= XHCI_TBT_BR_SLOT;
                if (!xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_SLOTCAP, &v) ||
                    v == XHCI_TBT_ALL_ONES) {
                    br->Flags |= XHCI_TBT_BR_UNREAD;
                } else {
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
    } else if (st != XHCI_TBT_CAP_ABSENT) {
        br->Flags |= XHCI_TBT_BR_UNREAD;
    }
    st = XhciTbtFindCapStatus(io, bdf, XHCI_TBT_CAP_SHPC, &off);
    if (st == XHCI_TBT_CAP_FOUND) {
        br->ShpcCap = off;
        br->Flags |= XHCI_TBT_BR_SHPC;
    } else if (st != XHCI_TBT_CAP_ABSENT) {
        br->Flags |= XHCI_TBT_BR_UNREAD;
    }
    if (br->Flags & (XHCI_TBT_BR_HOTPLUG | XHCI_TBT_BR_SHPC)) {
        br->Flags |= XHCI_TBT_BR_CANDIDATE;
    }
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
    topo->Base[topo->Tunnels] = XHCI_TBT_PORT_ERROR;
    topo->Tunnels++;
    topo->BaseArmed = 0;
    return 1;
}

static VOID xhciTbtSetClear(PXHCI_TBT_TOPO topo)
{
    ULONG i;

    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        topo->Set[i] = XHCI_TBT_NO_BDF;
        topo->Source[i] = 0;
        topo->Last[i] = XHCI_TBT_PORT_ERROR;
        topo->Base[i] = XHCI_TBT_PORT_ERROR;
    }
    topo->Tunnels = 0;
    topo->Tunnel = XHCI_TBT_NO_BDF;
    topo->BaseArmed = 0;
    xhciTbtZero((PULONG)&topo->Pick, sizeof(topo->Pick) / sizeof(ULONG));
}

/*
 * 1 when the platform table verifies the candidate `br` for this domain.
 * `otherNhi` is 1 when another function on the search bus carries a
 * table entry's NhiId, which rules out the entries without a Port.
 */
static ULONG xhciTbtTableMatch(const XHCI_TBT_TOPO_IN *in,
                               const XHCI_TBT_TOPO *topo,
                               const XHCI_TBT_BRIDGE *br, ULONG otherNhi)
{
    ULONG i;
    const XHCI_TBT_TABLE_ENTRY *e;

    if (in->Table == NULL) {
        return 0;
    }
    for (i = 0; i < in->TableCount; i++) {
        e = &in->Table[i];
        if (e->PortId != br->Id ||
            (e->NhiId != 0 && e->NhiId != topo->NhiId) ||
            (e->Nhi != XHCI_TBT_NO_BDF && e->Nhi != topo->Nhi) ||
            (e->Port != XHCI_TBT_NO_BDF && e->Port != br->Bdf)) {
            continue;
        }
        if (e->Port == XHCI_TBT_NO_BDF && otherNhi) {
            continue;
        }
        return 1;
    }
    return 0;
}

/* 1 when `id` is some table entry's NhiId. */
static ULONG xhciTbtTableNhi(const XHCI_TBT_TOPO_IN *in, ULONG id)
{
    ULONG i;

    if (in->Table == NULL) {
        return 0;
    }
    for (i = 0; i < in->TableCount; i++) {
        if (in->Table[i].NhiId != 0 && in->Table[i].NhiId == id) {
            return 1;
        }
    }
    return 0;
}

/*
 * The functions on topo->SearchBus: another domain's NHI (a function other
 * than this NHI carrying a table entry's NhiId) and the type 1 siblings,
 * the parent excluded, with the candidates among them. Fills the log and
 * the candidate list; the set is taken by xhciTbtTake.
 */
static VOID xhciTbtSiblings(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO_IN *in,
                            PXHCI_TBT_TOPO topo, PULONG otherNhi)
{
    ULONG bus = topo->SearchBus;
    ULONG dev;
    ULONG fn;
    ULONG count;
    ULONG bdf;
    ULONG id;
    ULONG hdr;
    XHCI_TBT_BRIDGE br;

    *otherNhi = 0;
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
            if (bdf != topo->Nhi && xhciTbtTableNhi(in, id)) {
                *otherNhi = 1;
            }
            hdr = xhciTbtHeader(io, bdf);
            if (hdr == XHCI_TBT_ALL_ONES ||
                XHCI_TBT_HDR_LAYOUT(hdr) != XHCI_TBT_HDR_BRIDGE ||
                bdf == topo->Parent) {
                continue;
            }
            xhciTbtClassify(io, bdf, id, &br);
            if (br.Flags & XHCI_TBT_BR_UNREAD) {
                topo->Unread++;
            }
            if (topo->Siblings < XHCI_TBT_MAX_SIBLINGS) {
                topo->Sibling[topo->Siblings] = br;
            }
            topo->Siblings++;
            if (!(br.Flags & XHCI_TBT_BR_CANDIDATE)) {
                continue;
            }
            if (topo->Candidates < XHCI_TBT_MAX_CANDIDATES) {
                topo->Cand[topo->Candidates] = bdf;
            }
            topo->Candidates++;
        }
    }
}

/* The set from the candidates, by the switch rule or the table. */
static VOID xhciTbtTake(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO_IN *in,
                        PXHCI_TBT_TOPO topo, ULONG otherNhi)
{
    ULONG i;
    ULONG id;
    ULONG overflow = 0;
    XHCI_TBT_BRIDGE br;

    for (i = 0; i < topo->Candidates; i++) {
        if (i >= XHCI_TBT_MAX_CANDIDATES) {
            /* A candidate not kept cannot be taken: the set would be
             * partial without saying so. */
            overflow = 1;
            break;
        }
        if (!xhciTbtPresent(io, topo->Cand[i], &id)) {
            continue;
        }
        xhciTbtClassify(io, topo->Cand[i], id, &br);
        if (!(br.Flags & XHCI_TBT_BR_CANDIDATE)) {
            continue;
        }
        if (topo->Switch) {
            if (!xhciTbtSetAdd(topo, &br, XHCI_TBT_SRC_SWITCH)) {
                overflow = 1;
            }
            continue;
        }
        if (xhciTbtTableMatch(in, topo, &br, 0)) {
            if (otherNhi && !xhciTbtTableMatch(in, topo, &br, 1)) {
                topo->Ambiguous = 1;
                continue;
            }
            topo->Listed++;
            br.Flags |= XHCI_TBT_BR_LISTED;
            if (!xhciTbtSetAdd(topo, &br, XHCI_TBT_SRC_TABLE)) {
                overflow = 1;
            }
        }
    }

    if (topo->Candidates == 0) {
        xhciTbtSetClear(topo);
        topo->Outcome = XHCI_TBT_TOPO_NO_CANDIDATE;
    } else if (overflow && (topo->Switch || topo->Tunnels != 0)) {
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
    ULONG otherNhi;
    XHCI_TBT_BRIDGE br;

    if (topo == NULL) {
        return XHCI_TBT_TOPO_BAD_PARAM;
    }
    xhciTbtZero((PULONG)topo, sizeof(*topo) / sizeof(ULONG));
    topo->Parent = XHCI_TBT_NO_BDF;
    topo->Upstream = XHCI_TBT_NO_BDF;
    xhciTbtSetClear(topo);
    if (in == NULL) {
        topo->Outcome = XHCI_TBT_TOPO_BAD_PARAM;
        return topo->Outcome;
    }
    topo->Nhi = in->Nhi;
    if (io == NULL || io->Read == NULL || in->Nhi > 0xFFFFUL ||
        in->Overrides > XHCI_TBT_MAX_TUNNELS ||
        (in->Table == NULL && in->TableCount != 0)) {
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
    topo->NhiId = id;

    topo->Parent = xhciTbtBridgeAbove(io, topo, XHCI_TBT_BDF_BUS(in->Nhi));
    if (topo->Parent != XHCI_TBT_NO_BDF) {
        topo->SearchBus = XHCI_TBT_BDF_BUS(topo->Parent);
        topo->ParentType = xhciTbtPortType(io, topo->Parent);
        topo->Upstream = xhciTbtBridgeAbove(io, topo, topo->SearchBus);
        topo->UpstreamType = xhciTbtPortType(io, topo->Upstream);
        topo->Switch = (topo->ParentType == XHCI_TBT_PCIE_TYPE_DOWN &&
                        topo->UpstreamType == XHCI_TBT_PCIE_TYPE_UP)
                       ? 1UL : 0UL;
    } else {
        topo->SearchBus = XHCI_TBT_BDF_BUS(in->Nhi);
    }
    xhciTbtSiblings(io, in, topo, &otherNhi);
    xhciTbtTake(io, in, topo, otherNhi);

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

/* ---- learning ---------------------------------------------------------- */

/*
 * 1 when learning may add to this domain's set: nothing was verified by
 * discovery, the table or the override, every candidate is kept, every
 * capability walk succeeded, and there is room.
 */
static ULONG xhciTbtLearnable(const XHCI_TBT_TOPO *topo)
{
    ULONG i;

    if (topo->Outcome != XHCI_TBT_TOPO_LEARN &&
        topo->Outcome != XHCI_TBT_TOPO_SET) {
        return 0;
    }
    if (topo->Candidates > XHCI_TBT_MAX_CANDIDATES || topo->Unread != 0 ||
        topo->Tunnels >= XHCI_TBT_MAX_TUNNELS) {
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
    for (i = 0; i < topo->Candidates; i++) {
        topo->Before[i] = XhciTbtPortRead(io, topo->Cand[i], &port);
        if (topo->Before[i] == XHCI_TBT_PORT_ERROR) {
            return;
        }
    }
    topo->LearnArmed = 1;
}

ULONG XhciTbtLearn(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo)
{
    ULONG i;
    ULONG now;
    ULONG found = XHCI_TBT_NO_BDF;
    ULONG rose = 0;
    ULONG id;
    XHCI_TBT_PORT port;
    XHCI_TBT_BRIDGE br;

    if (topo == NULL) {
        return 0;
    }
    if (io == NULL || io->Read == NULL || !topo->LearnArmed ||
        !xhciTbtLearnable(topo)) {
        topo->LearnArmed = 0;
        return 0;
    }
    topo->LearnArmed = 0;
    for (i = 0; i < topo->Candidates; i++) {
        now = XhciTbtPortRead(io, topo->Cand[i], &port);
        if (now == XHCI_TBT_PORT_ERROR) {
            return 0;
        }
        if (xhciTbtInSet(topo, topo->Cand[i])) {
            continue;
        }
        if (topo->Before[i] == XHCI_TBT_PORT_DOWN && now == XHCI_TBT_PORT_UP) {
            rose++;
            found = topo->Cand[i];
        }
    }
    if (rose != 1 || !xhciTbtPresent(io, found, &id)) {
        return 0;
    }
    xhciTbtClassify(io, found, id, &br);
    xhciTbtSetAdd(topo, &br, XHCI_TBT_SRC_LEARNED);
    topo->Outcome = XHCI_TBT_TOPO_SET;
    return 1;
}

/* ---- the override's text ----------------------------------------------- */

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
 * Select byte found there is written back whatever happened, and a
 * failure to write it back is ERROR.
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
    if (!io->Write(io->Context, bdf, cap + XHCI_TBT_SHPC_SELECT, 1, saved) &&
        result != XHCI_TBT_PORT_GONE) {
        port->ShpcValid = 0;
        result = XHCI_TBT_PORT_ERROR;
    }
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

/* XHCI_TBT_CAP_* to the port verdict a failed walk means, 0 otherwise. */
static ULONG xhciTbtWalkVerdict(ULONG st)
{
    if (st == XHCI_TBT_CAP_FAILED) {
        return XHCI_TBT_PORT_ERROR;
    }
    if (st == XHCI_TBT_CAP_GONE) {
        return XHCI_TBT_PORT_GONE;
    }
    return 0xFFFFFFFFUL;
}

ULONG XhciTbtPortRead(PXHCI_TBT_CFG io, ULONG bdf, PXHCI_TBT_PORT port)
{
    ULONG id;
    ULONG v;
    ULONG off;
    ULONG st;
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

    st = XhciTbtFindCapStatus(io, bdf, XHCI_TBT_CAP_PCIE, &off);
    if (xhciTbtWalkVerdict(st) != 0xFFFFFFFFUL) {
        port->Verdict = xhciTbtWalkVerdict(st);
        return port->Verdict;
    }
    if (st == XHCI_TBT_CAP_FOUND) {
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

    st = XhciTbtFindCapStatus(io, bdf, XHCI_TBT_CAP_SHPC, &off);
    if (xhciTbtWalkVerdict(st) != 0xFFFFFFFFUL) {
        port->Verdict = xhciTbtWalkVerdict(st);
        return port->Verdict;
    }
    if (st == XHCI_TBT_CAP_FOUND) {
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

ULONG XhciTbtSetVerdict(const ULONG *verdicts, const ULONG *bases,
                        ULONG count, ULONG want)
{
    ULONG i;
    ULONG up = 0;
    ULONG down = 0;
    ULONG gone = 0;
    ULONG none = 0;
    ULONG error = 0;
    ULONG progress = 0;

    if (verdicts == NULL || count == 0) {
        return XHCI_TBT_PORT_NONE;
    }
    for (i = 0; i < count; i++) {
        if (verdicts[i] == XHCI_TBT_PORT_UP) {
            up++;
            if (bases != NULL && (bases[i] == XHCI_TBT_PORT_DOWN ||
                                  bases[i] == XHCI_TBT_PORT_MIXED ||
                                  bases[i] == XHCI_TBT_PORT_GONE)) {
                progress++;
            }
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
    if (want == XHCI_TBT_WANT_UP && (progress != 0 || up == count)) {
        return XHCI_TBT_PORT_UP;
    }
    if (want == XHCI_TBT_WANT_DOWN && down + gone == count) {
        return gone != 0 ? XHCI_TBT_PORT_GONE : XHCI_TBT_PORT_DOWN;
    }
    if (error != 0) {
        return XHCI_TBT_PORT_ERROR;
    }
    if (none == count) {
        return XHCI_TBT_PORT_NONE;
    }
    return XHCI_TBT_PORT_MIXED;
}

VOID XhciTbtSetArm(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo)
{
    ULONG i;
    XHCI_TBT_PORT port;

    if (topo == NULL) {
        return;
    }
    for (i = 0; i < topo->Tunnels; i++) {
        topo->Base[i] = XhciTbtPortRead(io, topo->Set[i], &port);
    }
    topo->BaseArmed = topo->Tunnels != 0 ? 1UL : 0UL;
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
    return XhciTbtSetVerdict(topo->Last, topo->BaseArmed ? topo->Base : NULL,
                             topo->Tunnels, want);
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
