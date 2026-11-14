/*
 * tbt_topo.c - finding a Thunderbolt domain's tunnel ports and reading
 * them (roadmap-usb4.md tasks 39-A.5 and 40-A.4; design record 21
 * sections 6.3 and 6.5, design record 22 section 7.6). tbt_topo.h has the
 * contract, the register layouts and their citations.
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
        topo->Pick.Flags |= XHCI_TBT_BR_MEMBER;
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
    topo->Named = 0;
    topo->AcpiNamed = 0;
    topo->Tunnel = XHCI_TBT_NO_BDF;
    topo->BaseArmed = 0;
    xhciTbtZero((PULONG)&topo->Pick, sizeof(topo->Pick) / sizeof(ULONG));
}

static ULONG xhciTbtInList(const ULONG *list, ULONG count, ULONG id)
{
    ULONG i;

    if (list == NULL) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (list[i] == id) {
            return 1;
        }
    }
    return 0;
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

/*
 * 0 when no function answers at `bdf`, 1 when one does (its id in *id),
 * 2 when the access failed: the enumeration is then incomplete.
 */
static ULONG xhciTbtProbe(PXHCI_TBT_CFG io, ULONG bdf, PULONG id)
{
    ULONG v;

    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_ID, &v)) {
        *id = XHCI_TBT_ALL_ONES;
        return 2;
    }
    *id = v;
    return ((v & 0xFFFFUL) != 0xFFFFUL && (v & 0xFFFFUL) != 0UL) ? 1UL : 0UL;
}

/*
 * The source that makes `br` a member, 0 for none, by sources 2 to 4 of
 * XhciTbtTopoFind. A listed root port beside a root-bus NHI with another
 * NHI of the family is flagged AMBIGUOUS and gets none.
 */
static ULONG xhciTbtSource(const XHCI_TBT_TOPO_IN *in, PXHCI_TBT_TOPO topo,
                           PXHCI_TBT_BRIDGE br)
{
    if (topo->Parent != XHCI_TBT_NO_BDF) {
        if (xhciTbtInList(in->SwitchPortIds, in->SwitchPortIdCount,
                          br->Id)) {
            br->Flags |= XHCI_TBT_BR_LISTED | XHCI_TBT_BR_CANDIDATE;
            if (br->Flags & XHCI_TBT_BR_HOTPLUG) {
                return XHCI_TBT_SRC_SWITCH;
            }
        }
    } else if (xhciTbtInList(in->RootPortIds, in->RootPortIdCount, br->Id)) {
        br->Flags |= XHCI_TBT_BR_LISTED | XHCI_TBT_BR_CANDIDATE;
        /* Exactly one NHI of the family, and this one is it: a family
         * list without this NHI's id proves nothing (fails closed). */
        if (topo->FamilyNhis == 1 &&
            xhciTbtInList(in->FamilyNhiIds, in->FamilyNhiIdCount,
                          topo->NhiId)) {
            return XHCI_TBT_SRC_ROOTPORT;
        }
        br->Flags |= XHCI_TBT_BR_AMBIGUOUS;
        return 0;
    }
    if (br->Flags & XHCI_TBT_BR_SHPC) {
        return XHCI_TBT_SRC_SHPC;
    }
    return 0;
}

/*
 * One pass over the functions of topo->SearchBus. Pass 0 counts the
 * family's NHIs (a root bus only; every function's identity read, so an
 * unread one counts in Unread). Pass 1 reads the type 1 siblings, the
 * parent excluded: the log, the candidates, and the set by sources 2 to
 * 4. Every function not read completely - identity, header (so whether
 * it is multi-function), capability list - counts in topo->Unread.
 */
static VOID xhciTbtWalkBus(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO_IN *in,
                           PXHCI_TBT_TOPO topo, ULONG pass, PULONG overflow)
{
    ULONG bus = topo->SearchBus;
    ULONG dev;
    ULONG fn;
    ULONG count;
    ULONG bdf;
    ULONG id;
    ULONG hdr;
    ULONG probe;
    ULONG source;
    XHCI_TBT_BRIDGE br;

    for (dev = 0; dev < 32; dev++) {
        probe = xhciTbtProbe(io, XHCI_TBT_BDF(bus, dev, 0), &id);
        if (probe == 2) {
            topo->Unread++;
        }
        if (probe != 1) {
            continue;
        }
        hdr = xhciTbtHeader(io, XHCI_TBT_BDF(bus, dev, 0));
        if (hdr == XHCI_TBT_ALL_ONES) {
            /* Not even whether it has other functions is known. */
            topo->Unread++;
            continue;
        }
        count = xhciTbtFunctionCount(hdr);
        for (fn = 0; fn < count; fn++) {
            bdf = XHCI_TBT_BDF(bus, dev, fn);
            probe = xhciTbtProbe(io, bdf, &id);
            if (probe == 2) {
                topo->Unread++;
            }
            if (probe != 1) {
                continue;
            }
            if (pass == 0) {
                if (xhciTbtInList(in->FamilyNhiIds, in->FamilyNhiIdCount,
                                  id)) {
                    topo->FamilyNhis++;
                }
                continue;
            }
            hdr = xhciTbtHeader(io, bdf);
            if (hdr == XHCI_TBT_ALL_ONES) {
                topo->Unread++;
                continue;
            }
            if (XHCI_TBT_HDR_LAYOUT(hdr) != XHCI_TBT_HDR_BRIDGE ||
                bdf == topo->Parent) {
                continue;
            }
            xhciTbtClassify(io, bdf, id, &br);
            if (br.Flags & XHCI_TBT_BR_UNREAD) {
                topo->Unread++;
            }
            source = xhciTbtSource(in, topo, &br);
            if (br.Flags & XHCI_TBT_BR_AMBIGUOUS) {
                topo->Ambiguous++;
            }
            if (source != 0 && !(br.Flags & XHCI_TBT_BR_UNREAD) &&
                !xhciTbtSetAdd(topo, &br, source)) {
                *overflow = 1;
            }
            if (topo->Siblings < XHCI_TBT_MAX_SIBLINGS) {
                topo->Sibling[topo->Siblings] = br;
            }
            topo->Siblings++;
            if (br.Flags & XHCI_TBT_BR_CANDIDATE) {
                if (topo->Candidates < XHCI_TBT_MAX_CANDIDATES) {
                    topo->Cand[topo->Candidates] = bdf;
                }
                topo->Candidates++;
            }
        }
    }
}

/*
 * Source 1, the override: the whole set. An entry with no function, a
 * function that is not a bridge, or a repeat is dropped; an entry that
 * cannot be read (a failed access, or a present function whose header
 * reads all-ones) makes the outcome INCOMPLETE with an empty set, since
 * dropping it would lose a port the user named.
 */
static VOID xhciTbtOverride(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO_IN *in,
                            PXHCI_TBT_TOPO topo)
{
    ULONG i;
    ULONG id;
    ULONG hdr;
    ULONG probe;
    XHCI_TBT_BRIDGE br;

    xhciTbtSetClear(topo);
    for (i = 0; i < in->Overrides; i++) {
        probe = xhciTbtProbe(io, in->Override[i], &id);
        if (probe == 2) {
            goto unread;
        }
        if (probe == 0 || xhciTbtInSet(topo, in->Override[i])) {
            topo->Dropped++;
            continue;
        }
        hdr = xhciTbtHeader(io, in->Override[i]);
        if (hdr == XHCI_TBT_ALL_ONES) {
            goto unread;
        }
        if (XHCI_TBT_HDR_LAYOUT(hdr) != XHCI_TBT_HDR_BRIDGE) {
            topo->Dropped++;
            continue;
        }
        xhciTbtClassify(io, in->Override[i], id, &br);
        xhciTbtSetAdd(topo, &br, XHCI_TBT_SRC_OVERRIDE);
    }
    topo->Outcome = topo->Tunnels != 0 ? XHCI_TBT_TOPO_OVERRIDE
                                       : XHCI_TBT_TOPO_OVERRIDE_BAD;
    return;

unread:
    topo->Unread++;
    xhciTbtSetClear(topo);
    topo->Outcome = XHCI_TBT_TOPO_INCOMPLETE;
}

/* The log's MEMBER flags, from the final set. */
static VOID xhciTbtMarkMembers(PXHCI_TBT_TOPO topo)
{
    ULONG i;
    ULONG kept;

    kept = topo->Siblings < XHCI_TBT_MAX_SIBLINGS ? topo->Siblings
                                                  : XHCI_TBT_MAX_SIBLINGS;
    for (i = 0; i < kept; i++) {
        topo->Sibling[i].Flags &= ~XHCI_TBT_BR_MEMBER;
        if (xhciTbtInSet(topo, topo->Sibling[i].Bdf)) {
            topo->Sibling[i].Flags |= XHCI_TBT_BR_MEMBER;
        }
    }
    if (topo->Tunnels != 0) {
        topo->Pick.Flags |= XHCI_TBT_BR_MEMBER;
    }
}

ULONG XhciTbtTopoFind(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO_IN *in,
                      PXHCI_TBT_TOPO topo)
{
    ULONG id;
    ULONG i;
    ULONG overflow = 0;

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
    topo->NhiId = id;

    topo->Parent = xhciTbtBridgeAbove(io, topo, XHCI_TBT_BDF_BUS(in->Nhi));
    if (topo->Parent != XHCI_TBT_NO_BDF) {
        topo->SearchBus = XHCI_TBT_BDF_BUS(topo->Parent);
        topo->ParentType = xhciTbtPortType(io, topo->Parent);
    } else {
        topo->SearchBus = XHCI_TBT_BDF_BUS(in->Nhi);
        xhciTbtWalkBus(io, in, topo, 0, &overflow);
    }
    xhciTbtWalkBus(io, in, topo, 1, &overflow);

    if (topo->Unread != 0) {
        xhciTbtSetClear(topo);
        topo->Outcome = XHCI_TBT_TOPO_INCOMPLETE;
    } else if (overflow) {
        xhciTbtSetClear(topo);
        topo->Outcome = XHCI_TBT_TOPO_MANY;
    } else if (topo->Tunnels != 0) {
        topo->Outcome = XHCI_TBT_TOPO_SET;
    } else if (topo->Ambiguous != 0) {
        topo->Outcome = XHCI_TBT_TOPO_AMBIGUOUS;
    } else if (topo->Candidates != 0) {
        topo->Outcome = XHCI_TBT_TOPO_UNLISTED;
    } else {
        topo->Outcome = XHCI_TBT_TOPO_NO_CANDIDATE;
    }

    if (in->Overrides != 0) {
        xhciTbtOverride(io, in, topo);
    }
    topo->FindOutcome = topo->Outcome;
    xhciTbtMarkMembers(topo);
    return topo->Outcome;
}

ULONG XhciTbtExclusive(const ULONG *set, ULONG count, const ULONG *others,
                       ULONG otherCount, PULONG active)
{
    ULONG i;
    ULONG off = 0;

    if (set == NULL || active == NULL) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        active[i] = 1;
        if (xhciTbtInList(others, otherCount, set[i])) {
            active[i] = 0;
            off++;
        }
    }
    return off;
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

/* ---- the USB4 profile's correspondence --------------------------------- */

static ULONG xhciTbtPortBit(ULONG port)
{
    return 1UL << (port - 1);
}

/* The member index of `bdf` in the set, or XHCI_TBT_NO_MEMBER. */
static ULONG xhciTbtMemberOf(const XHCI_TBT_TOPO *topo, ULONG bdf)
{
    ULONG i;

    for (i = 0; i < topo->Tunnels && i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (topo->Set[i] == bdf) {
            return i;
        }
    }
    return XHCI_TBT_NO_MEMBER;
}

/* The binding of `port`, named or learned, or XHCI_TBT_NO_MEMBER. */
static ULONG xhciTbtBindOfPort(const XHCI_TBT_BINDS *binds, ULONG port)
{
    ULONG i;

    for (i = 0; i < binds->Count && i < XHCI_TBT_MAX_BINDS; i++) {
        if (binds->Port[i] == port &&
            (binds->How[i] == XHCI_TBT_CORR_NAMED ||
             binds->How[i] == XHCI_TBT_CORR_LEARNED)) {
            return i;
        }
    }
    return XHCI_TBT_NO_MEMBER;
}

/* The entry of `bdf`, of any kind, or XHCI_TBT_NO_MEMBER. */
static ULONG xhciTbtBindOfBdf(const XHCI_TBT_BINDS *binds, ULONG bdf)
{
    ULONG i;

    for (i = 0; i < binds->Count && i < XHCI_TBT_MAX_BINDS; i++) {
        if (binds->Bdf[i] == bdf) {
            return i;
        }
    }
    return XHCI_TBT_NO_MEMBER;
}

/*
 * A new entry; with no room left the table overflows, and from then on no
 * port is known (XhciTbtPortMember answers none).
 */
static ULONG xhciTbtBindNew(PXHCI_TBT_BINDS binds, ULONG port, ULONG bdf,
                            ULONG how)
{
    ULONG i;

    if (binds->Count >= XHCI_TBT_MAX_BINDS) {
        binds->Overflow = 1;
        return XHCI_TBT_NO_MEMBER;
    }
    i = binds->Count++;
    binds->Port[i] = port;
    binds->Bdf[i] = bdf;
    binds->How[i] = how;
    return i;
}

/*
 * Two claims disagree about `port` and `bdf`: the port, the bridge and
 * whatever either was bound to are known no longer, for the instance.
 */
static ULONG xhciTbtBindConflict(PXHCI_TBT_BINDS binds, ULONG port,
                                 ULONG bdf)
{
    ULONG e;

    e = xhciTbtBindOfPort(binds, port);
    if (e != XHCI_TBT_NO_MEMBER) {
        binds->How[e] = XHCI_TBT_CORR_CONFLICT;
        binds->Port[e] = 0;
    }
    e = xhciTbtBindOfBdf(binds, bdf);
    if (e != XHCI_TBT_NO_MEMBER) {
        if (binds->Port[e] != 0) {
            binds->PortConflict |= xhciTbtPortBit(binds->Port[e]);
        }
        binds->How[e] = XHCI_TBT_CORR_CONFLICT;
        binds->Port[e] = 0;
    } else {
        xhciTbtBindNew(binds, 0, bdf, XHCI_TBT_CORR_CONFLICT);
    }
    binds->PortConflict |= xhciTbtPortBit(port);
    return XHCI_TBT_MAP_CONFLICT;
}

/*
 * Claims `port` for the bridge at `bdf` with `how`: `fresh` for a new
 * binding, KNOWN for the same binding again (by either kind), CONFLICT
 * when the port is conflicted or bound to another bridge, or the bridge
 * conflicted or bound to another port, or the table cannot hold it.
 */
static ULONG xhciTbtClaim(PXHCI_TBT_BINDS binds, ULONG port, ULONG bdf,
                          ULONG how, ULONG fresh)
{
    ULONG p;
    ULONG b;

    if (binds->Overflow) {
        return XHCI_TBT_MAP_CONFLICT;
    }
    if (binds->PortConflict & xhciTbtPortBit(port)) {
        return xhciTbtBindConflict(binds, port, bdf);
    }
    p = xhciTbtBindOfPort(binds, port);
    if (p != XHCI_TBT_NO_MEMBER && binds->Bdf[p] == bdf) {
        return XHCI_TBT_MAP_KNOWN;
    }
    b = xhciTbtBindOfBdf(binds, bdf);
    if (p != XHCI_TBT_NO_MEMBER || b != XHCI_TBT_NO_MEMBER) {
        return xhciTbtBindConflict(binds, port, bdf);
    }
    if (xhciTbtBindNew(binds, port, bdf, how) == XHCI_TBT_NO_MEMBER) {
        return XHCI_TBT_MAP_CONFLICT;
    }
    return fresh;
}

VOID XhciTbtBindsInit(PXHCI_TBT_BINDS binds)
{
    if (binds != NULL) {
        xhciTbtZero((PULONG)binds, sizeof(*binds) / sizeof(ULONG));
    }
}

/*
 * The discovery cannot stand as it is: an empty set with `outcome`, as
 * record 21 section 6.3 makes an incomplete or overfull discovery.
 */
static VOID xhciTbtSetVoid(PXHCI_TBT_TOPO topo, ULONG outcome)
{
    xhciTbtSetClear(topo);
    topo->Outcome = outcome;
    xhciTbtMarkMembers(topo);
}

/*
 * The router-named and ACPI-named sources' common rules (tbt_topo.h): with
 * `binds` NULL (ACPI-named) nothing is bound and the member joins
 * unbound, with source `source`.
 */
static ULONG xhciTbtTopoAdd(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo,
                            PXHCI_TBT_BINDS binds, ULONG port, ULONG bdf,
                            ULONG source)
{
    ULONG member;
    ULONG probe;
    ULONG id;
    ULONG hdr;
    ULONG claim;
    XHCI_TBT_BRIDGE br;

    if (topo->Outcome != XHCI_TBT_TOPO_SET &&
        topo->Outcome != XHCI_TBT_TOPO_OVERRIDE &&
        topo->Outcome != XHCI_TBT_TOPO_NO_CANDIDATE &&
        topo->Outcome != XHCI_TBT_TOPO_UNLISTED &&
        topo->Outcome != XHCI_TBT_TOPO_AMBIGUOUS) {
        return XHCI_TBT_MAP_REFUSED;
    }
    if (bdf == topo->Parent || bdf == topo->Nhi) {
        return XHCI_TBT_MAP_REFUSED;
    }
    member = xhciTbtMemberOf(topo, bdf);
    if (member != XHCI_TBT_NO_MEMBER) {
        /* Whatever source put it there stays; the naming binds it. */
        if (binds == NULL) {
            return XHCI_TBT_MAP_KNOWN;
        }
        claim = xhciTbtClaim(binds, port, bdf, XHCI_TBT_CORR_NAMED,
                             XHCI_TBT_MAP_KNOWN);
        return claim;
    }
    if (topo->Outcome == XHCI_TBT_TOPO_OVERRIDE) {
        /* The override is the whole set. */
        return XHCI_TBT_MAP_REFUSED;
    }

    probe = xhciTbtProbe(io, bdf, &id);
    if (probe == 0) {
        return XHCI_TBT_MAP_NO_BRIDGE;
    }
    hdr = probe == 1 ? xhciTbtHeader(io, bdf) : XHCI_TBT_ALL_ONES;
    if (hdr == XHCI_TBT_ALL_ONES) {
        /* A port the router names that cannot be read: the set might lack
         * it, so it stands as none until a discovery completes. */
        topo->Unread++;
        xhciTbtSetVoid(topo, XHCI_TBT_TOPO_INCOMPLETE);
        return XHCI_TBT_MAP_UNREAD;
    }
    if (XHCI_TBT_HDR_LAYOUT(hdr) != XHCI_TBT_HDR_BRIDGE) {
        return XHCI_TBT_MAP_NO_BRIDGE;
    }

    /* A claim that disagrees with one already made binds nothing, but
     * the router still names the bridge as a tunnel port: it joins the set
     * all the same, its port unknown (the whole set acts for it). */
    claim = binds == NULL ? XHCI_TBT_MAP_ADDED
                          : xhciTbtClaim(binds, port, bdf,
                                         XHCI_TBT_CORR_NAMED,
                                         XHCI_TBT_MAP_ADDED);
    if (topo->Tunnels >= XHCI_TBT_MAX_TUNNELS) {
        xhciTbtSetVoid(topo, XHCI_TBT_TOPO_MANY);
        return XHCI_TBT_MAP_FULL;
    }

    /* Taken whatever its capability list reads, as an override entry is:
     * only its port readings depend on the capabilities. */
    xhciTbtClassify(io, bdf, id, &br);
    xhciTbtSetAdd(topo, &br, source);
    if (source == XHCI_TBT_SRC_ACPI) {
        topo->AcpiNamed++;
    } else {
        topo->Named++;
    }
    topo->Outcome = XHCI_TBT_TOPO_SET;
    xhciTbtMarkMembers(topo);
    return claim == XHCI_TBT_MAP_CONFLICT ? XHCI_TBT_MAP_CONFLICT
                                          : XHCI_TBT_MAP_ADDED;
}

ULONG XhciTbtTopoAddNamed(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo,
                          PXHCI_TBT_BINDS binds, ULONG port, ULONG bdf)
{
    if (io == NULL || io->Read == NULL || topo == NULL || binds == NULL ||
        port == 0 || port > XHCI_TBT_MAX_PORTS || bdf > 0xFFFFUL) {
        return XHCI_TBT_MAP_BAD_PARAM;
    }
    return xhciTbtTopoAdd(io, topo, binds, port, bdf, XHCI_TBT_SRC_ROUTER);
}

ULONG XhciTbtTopoAddAcpi(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo, ULONG bdf)
{
    if (io == NULL || io->Read == NULL || topo == NULL || bdf > 0xFFFFUL) {
        return XHCI_TBT_MAP_BAD_PARAM;
    }
    return xhciTbtTopoAdd(io, topo, NULL, 0, bdf, XHCI_TBT_SRC_ACPI);
}

/*
 * One member's occupancy: its secondary bus in *bus and, in *seen, bit d
 * set for each device d whose function 0 answers there. 0 when it could
 * not be read (a failed access, bus numbers all-ones, or no secondary bus
 * above the bridge's own), else 1.
 */
static ULONG xhciTbtOccupancy(PXHCI_TBT_CFG io, ULONG bdf, PULONG bus,
                              PULONG seen)
{
    ULONG buses;
    ULONG secondary;
    ULONG dev;
    ULONG id;
    ULONG probe;

    *bus = 0;
    *seen = 0;
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_BUSES, &buses) ||
        buses == XHCI_TBT_ALL_ONES) {
        return 0;
    }
    secondary = (buses >> 8) & 0xFFUL;
    if (secondary <= XHCI_TBT_BDF_BUS(bdf)) {
        /* Unconfigured (0) or misprogrammed: no bus to look at. */
        return 0;
    }
    *bus = secondary;
    for (dev = 0; dev < 32; dev++) {
        probe = xhciTbtProbe(io, XHCI_TBT_BDF(secondary, dev, 0), &id);
        if (probe == 2) {
            return 0;
        }
        if (probe == 1) {
            *seen |= 1UL << dev;
        }
    }
    return 1;
}

/* 1 when the baseline was taken over the set as it is now. */
static ULONG xhciTbtBaseFits(const XHCI_TBT_TOPO *topo,
                             const XHCI_TBT_BINDS *binds)
{
    ULONG i;

    if (!binds->Armed || binds->BaseCount != topo->Tunnels) {
        return 0;
    }
    for (i = 0; i < topo->Tunnels && i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (binds->BaseBdf[i] != topo->Set[i]) {
            return 0;
        }
    }
    return 1;
}

static ULONG xhciTbtStartTake(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                              PXHCI_TBT_BINDS binds, const ULONG *bus,
                              const ULONG *seen);

ULONG XhciTbtLearnArm(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                      PXHCI_TBT_BINDS binds)
{
    ULONG i;
    ULONG k;
    ULONG bus;
    ULONG seen;
    ULONG down;
    ULONG oldCount;
    ULONG oldDown;
    ULONG oldHeld;
    ULONG ok;
    ULONG oldBdf[XHCI_TBT_MAX_TUNNELS];
    ULONG newBus[XHCI_TBT_MAX_TUNNELS];
    ULONG newSeen[XHCI_TBT_MAX_TUNNELS];

    if (binds == NULL) {
        return XHCI_TBT_MAP_BAD_PARAM;
    }
    binds->Armed = 0;
    if (io == NULL || io->Read == NULL || topo == NULL ||
        topo->Tunnels == 0 || topo->Tunnels > XHCI_TBT_MAX_TUNNELS) {
        return XHCI_TBT_MAP_BAD_PARAM;
    }
    oldCount = binds->BaseCount;
    oldDown = binds->BaseDown;
    oldHeld = binds->BaseHeld;
    for (i = 0; i < oldCount && i < XHCI_TBT_MAX_TUNNELS; i++) {
        oldBdf[i] = binds->BaseBdf[i];
    }
    /* Read whole before the baseline is kept: an arm that fails leaves
     * the old baseline and its link evidence as they were (not armed), so
     * the next arm still finds that evidence by location - but what it
     * did read of a member counts at once, whatever happens after: a
     * device seen answering there (even in a scan that then failed)
     * spends the member's evidence, and so does reading empty a member
     * whose evidence an incomplete attempt held. */
    for (i = 0; i < topo->Tunnels; i++) {
        ok = xhciTbtOccupancy(io, topo->Set[i], &bus, &seen);
        for (k = 0; k < oldCount && k < XHCI_TBT_MAX_TUNNELS; k++) {
            /* Held evidence is spent by a scan that failed too: what the
             * failure hid may be the loss of the occupancy it holds. */
            if (oldBdf[k] == topo->Set[i] &&
                (seen != 0 || (oldHeld & (1UL << k)) != 0)) {
                oldDown &= ~(1UL << k);
                oldHeld &= ~(1UL << k);
            }
        }
        if (!ok) {
            binds->BaseDown = oldDown;
            binds->BaseHeld = oldHeld;
            return XHCI_TBT_MAP_UNREAD;
        }
        newBus[i] = bus;
        newSeen[i] = seen;
    }
    for (i = 0; i < topo->Tunnels; i++) {
        binds->BaseBdf[i] = topo->Set[i];
        binds->BaseBus[i] = newBus[i];
        binds->BaseSeen[i] = newSeen[i];
    }
    binds->BaseCount = topo->Tunnels;
    /* The link evidence: a member empty now keeps what an earlier baseline
     * read of it, or reads its port now. */
    down = 0;
    for (i = 0; i < topo->Tunnels; i++) {
        if (binds->BaseSeen[i] != 0) {
            continue;
        }
        /* A bit held by an occupancy no complete reading followed is
         * spent here: the member is empty again with no DOWN read since. */
        for (k = 0; k < oldCount && k < XHCI_TBT_MAX_TUNNELS; k++) {
            if (oldBdf[k] == topo->Set[i] && (oldDown & (1UL << k)) != 0 &&
                (oldHeld & (1UL << k)) == 0) {
                down |= 1UL << i;
            }
        }
        if ((down & (1UL << i)) == 0 && XhciTbtPortDown(io, topo->Set[i])) {
            down |= 1UL << i;
        }
    }
    binds->BaseDown = down;
    binds->BaseHeld = 0;
    binds->Armed = 1;
    /* A shape not read leaves the start reading to the next reading. */
    (VOID)xhciTbtStartTake(io, topo, binds, binds->BaseBus, binds->BaseSeen);
    return 0;
}

ULONG XhciTbtShpcSet(const XHCI_TBT_TOPO *topo)
{
    ULONG i;

    if (topo == NULL || topo->Tunnels == 0 ||
        topo->Tunnels > XHCI_TBT_MAX_TUNNELS) {
        return 0;
    }
    for (i = 0; i < topo->Tunnels; i++) {
        if (topo->Source[i] != XHCI_TBT_SRC_SHPC) {
            return 0;
        }
    }
    return 1;
}

ULONG XhciTbtLearnOwed(const XHCI_TBT_TOPO *topo, const XHCI_TBT_BINDS *binds)
{
    ULONG i;
    ULONG owed = 0;

    if (topo == NULL || binds == NULL || topo->Tunnels == 0 ||
        topo->Tunnels > XHCI_TBT_MAX_TUNNELS ||
        !xhciTbtBaseFits(topo, binds)) {
        return 0;
    }
    for (i = 0; i < topo->Tunnels; i++) {
        if (binds->BaseSeen[i] == 0 && (binds->BaseDown & (1UL << i)) == 0) {
            owed |= 1UL << i;
        }
    }
    return owed;
}

ULONG XhciTbtLearnEvidence(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                           PXHCI_TBT_BINDS binds)
{
    ULONG i;
    ULONG bus;
    ULONG seen;
    ULONG owed;

    owed = XhciTbtLearnOwed(topo, binds);
    if (io == NULL || io->Read == NULL) {
        return owed;
    }
    /* Bits are set only, by the arm's own condition (the port DOWN at a
     * reading where nothing answers behind it); the baseline, its armed
     * mark and every other bit stand whatever a read here does, so a
     * failure costs the next attempt nothing (record 22 section 7.6.5). */
    for (i = 0; i < topo->Tunnels && i < XHCI_TBT_MAX_TUNNELS; i++) {
        if ((owed & (1UL << i)) == 0) {
            continue;
        }
        if (xhciTbtOccupancy(io, topo->Set[i], &bus, &seen) &&
            bus == binds->BaseBus[i] && seen == 0 &&
            XhciTbtPortDown(io, topo->Set[i])) {
            binds->BaseDown |= 1UL << i;
            owed &= ~(1UL << i);
        }
    }
    return owed;
}

ULONG XhciTbtEvidenceDue(ULONG owed, ULONG pending, ULONG armMs, ULONG nowMs,
                         const ULONG *record, const ULONG *recordAt,
                         ULONG count)
{
    ULONG i;

    if (owed == 0 || pending != 0) {
        return 0;
    }
    /* Unsigned elapsed time: a wrap of the clock is harmless. */
    if ((ULONG)(nowMs - armMs) < XHCI_TBT_POLL_BUDGET_MS) {
        return 1;
    }
    if (record == NULL || recordAt == NULL) {
        return 0;
    }
    for (i = 0; i < count && i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (record[i] != recordAt[i]) {
            return 1;
        }
    }
    return 0;
}

VOID XhciTbtLearnUp(PXHCI_TBT_BINDS binds, ULONG port)
{
    if (binds == NULL || port == 0 || port > XHCI_TBT_MAX_PORTS ||
        xhciTbtBindOfPort(binds, port) != XHCI_TBT_NO_MEMBER) {
        return;
    }
    /* A conflicted port's tunnel is recorded too: it can never be learned,
     * but its device could still be taken for another port's. */
    binds->Pending |= xhciTbtPortBit(port);
}

/* 1 when a gain on the bridge at `bdf` is its own bound port's. */
static ULONG xhciTbtExplained(const XHCI_TBT_BINDS *binds, ULONG bdf)
{
    ULONG e = xhciTbtBindOfBdf(binds, bdf);

    return (e != XHCI_TBT_NO_MEMBER &&
            (binds->How[e] == XHCI_TBT_CORR_NAMED ||
             binds->How[e] == XHCI_TBT_CORR_LEARNED)) ? 1UL : 0UL;
}

/* The at-start mark of `port` ended (record 22 section 7.6.3); with none
 * left the start reading is forgotten. */
static VOID xhciTbtAtStartEnd(PXHCI_TBT_BINDS binds, ULONG port)
{
    binds->AtStart &= ~xhciTbtPortBit(port);
    if (binds->AtStart == 0) {
        binds->StartTaken = 0;
        binds->StartOk = 0;
    }
}

VOID XhciTbtLearnDown(PXHCI_TBT_BINDS binds, ULONG port)
{
    if (binds == NULL || port == 0 || port > XHCI_TBT_MAX_PORTS) {
        return;
    }
    binds->Pending &= ~xhciTbtPortBit(port);
    xhciTbtAtStartEnd(binds, port);
}

VOID XhciTbtLearnAtStart(PXHCI_TBT_BINDS binds, ULONG port)
{
    ULONG bit;

    if (binds == NULL || port == 0 || port > XHCI_TBT_MAX_PORTS ||
        xhciTbtBindOfPort(binds, port) != XHCI_TBT_NO_MEMBER) {
        return;
    }
    bit = xhciTbtPortBit(port);
    if (!(binds->Pending & bit) || (binds->AtStart & bit)) {
        return;
    }
    binds->AtStart |= bit;
    /* A port newly marked: the next full reading is the start reading. */
    binds->StartTaken = 0;
    binds->StartOk = 0;
}

VOID XhciTbtLearnNotAtStart(PXHCI_TBT_BINDS binds, ULONG port)
{
    if (binds == NULL || port == 0 || port > XHCI_TBT_MAX_PORTS) {
        return;
    }
    xhciTbtAtStartEnd(binds, port);
}

static ULONG xhciTbtBits(ULONG v)
{
    ULONG n = 0;

    while (v != 0) {
        v &= v - 1;
        n++;
    }
    return n;
}

/* xhciTbtFarEnd's answers, XhciTbtGainAdmits' own. */
#define XHCI_TBT_FAR_NO         XHCI_TBT_GAIN_NO
#define XHCI_TBT_FAR_YES        XHCI_TBT_GAIN_YES
#define XHCI_TBT_FAR_UNREAD     XHCI_TBT_GAIN_UNREAD

/*
 * Whether what answers behind a member (its secondary bus `bus` and the
 * devices `seen` there, as xhciTbtOccupancy gives them) has the shape of
 * a tunnel's far end as record 22 section 7.6.3 admits it: device 0
 * alone, a PCI-to-PCI bridge whose PCI Express capability says Upstream
 * Switch Port (YES). A host controller's own integrated functions behind
 * its switch's downstream ports (its xHCI, its NHI) are endpoints, so
 * they never have it. NO for any other shape; UNREAD when a read failed
 * or came back all-ones, which proves no shape either way.
 */
static ULONG xhciTbtFarEnd(PXHCI_TBT_CFG io, ULONG bus, ULONG seen)
{
    ULONG bdf;
    ULONG v;
    ULONG off;
    ULONG st;

    if (seen != 1UL) {
        return XHCI_TBT_FAR_NO;
    }
    bdf = XHCI_TBT_BDF(bus, 0, 0);
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_HEADER, &v) ||
        v == XHCI_TBT_ALL_ONES) {
        return XHCI_TBT_FAR_UNREAD;
    }
    if (XHCI_TBT_HDR_LAYOUT((v >> 16) & 0xFFUL) != XHCI_TBT_HDR_BRIDGE) {
        return XHCI_TBT_FAR_NO;
    }
    st = XhciTbtFindCapStatus(io, bdf, XHCI_TBT_CAP_PCIE, &off);
    if (st == XHCI_TBT_CAP_ABSENT) {
        return XHCI_TBT_FAR_NO;
    }
    if (st != XHCI_TBT_CAP_FOUND ||
        !xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_CAPS, &v) ||
        v == XHCI_TBT_ALL_ONES) {
        return XHCI_TBT_FAR_UNREAD;
    }
    return XHCI_TBT_PCIE_TYPE(v >> 16) == XHCI_TBT_PCIE_TYPE_UP
               ? XHCI_TBT_FAR_YES
               : XHCI_TBT_FAR_NO;
}

/*
 * Whether a port reading that ended ERROR or GONE had already read, whole,
 * a signal that says absent (Link Active where reported, Presence Detect
 * State where the slot is implemented, SHPC slots all empty): a complete
 * reading would then be DOWN or MIXED, never UP, so the failure hides no
 * UP (record 22 section 7.6.4). Every SHPC slot read empty counts even
 * when only the DWORD Select's restoring write then failed.
 */
static ULONG xhciTbtPortRulesOutUp(const XHCI_TBT_PORT *p)
{
    return (p->LinkValid && !p->LinkActive) ||
           (p->SlotValid && !p->Presence) ||
           (p->ShpcSeen && p->ShpcPresent == 0);
}

ULONG XhciTbtPortDown(PXHCI_TBT_CFG io, ULONG bdf)
{
    XHCI_TBT_PORT p;

    return XhciTbtPortRead(io, bdf, &p) == XHCI_TBT_PORT_DOWN ? 1UL : 0UL;
}

ULONG XhciTbtGainAdmits(PXHCI_TBT_CFG io, ULONG bdf, ULONG bus, ULONG seen,
                        ULONG wasDown)
{
    XHCI_TBT_PORT p;
    ULONG shape;

    if (io == NULL || io->Read == NULL || seen == 0) {
        return XHCI_TBT_GAIN_NO;
    }
    shape = xhciTbtFarEnd(io, bus, seen);
    if (shape != XHCI_TBT_FAR_NO) {
        return shape;
    }
    /* Any other shape (an endpoint directly behind the port, several
     * devices) only on the link's evidence: the port seen down with
     * nothing behind it, up now. */
    if (!wasDown) {
        return XHCI_TBT_GAIN_NO;
    }
    switch (XhciTbtPortRead(io, bdf, &p)) {
    case XHCI_TBT_PORT_UP:
        return XHCI_TBT_GAIN_YES;
    case XHCI_TBT_PORT_ERROR:
    case XHCI_TBT_PORT_GONE:
        /* A reading that fails part-way is never more lenient than a
         * whole one: a signal it read absent before failing rejects. */
        return xhciTbtPortRulesOutUp(&p) ? XHCI_TBT_GAIN_NO
                                         : XHCI_TBT_GAIN_UNREAD;
    default:
        return XHCI_TBT_GAIN_NO;
    }
}

/*
 * The start reading (record 22 section 7.6.3), from a full reading of the
 * set (`bus` and `seen`, per member), when at-start marks exist and none
 * was taken since the last one was added. Trusted only where a function
 * behind any member can be a tunnel's of this domain alone (source SWITCH
 * or ROUTER), every pending port is an at-start port, every occupied
 * member no binding explains holds a tunnel's far end (xhciTbtFarEnd: a
 * member holding anything else, such as the controller's own xHCI behind
 * a hot-plug capable port, is not a tunnel's), and those members are
 * exactly as many as the at-start ports: then each at-start tunnel's
 * function is on a distinct one of them. Returns 0, the reading not
 * taken, when a shape could not be read; else 1 (taken now or before).
 */
static ULONG xhciTbtStartTake(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                              PXHCI_TBT_BINDS binds, const ULONG *bus,
                              const ULONG *seen)
{
    ULONG i;
    ULONG shape;
    ULONG occ = 0;
    ULONG occupied = 0;
    ULONG trusted = 1;

    if (binds->AtStart == 0 || binds->StartTaken) {
        return 1;
    }
    for (i = 0; i < topo->Tunnels && i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (topo->Source[i] != XHCI_TBT_SRC_SWITCH &&
            topo->Source[i] != XHCI_TBT_SRC_ROUTER) {
            trusted = 0;
        }
        if (seen[i] != 0) {
            occ |= 1UL << i;
            if (!xhciTbtExplained(binds, topo->Set[i])) {
                occupied++;
                shape = xhciTbtFarEnd(io, bus[i], seen[i]);
                if (shape == XHCI_TBT_FAR_UNREAD) {
                    return 0;
                }
                if (shape != XHCI_TBT_FAR_YES) {
                    trusted = 0;
                }
            }
        }
    }
    for (i = 0; i < topo->Tunnels && i < XHCI_TBT_MAX_TUNNELS; i++) {
        binds->StartBdf[i] = topo->Set[i];
    }
    binds->StartCount = i;
    binds->StartOcc = occ;
    if (topo->Outcome != XHCI_TBT_TOPO_SET ||
        binds->Pending != binds->AtStart ||
        occupied != xhciTbtBits(binds->AtStart)) {
        trusted = 0;
    }
    binds->StartOk = trusted;
    binds->StartTaken = 1;
    return 1;
}

/*
 * The narrowed attribution (record 22 section 7.6.3): FAR_YES when the
 * gain on member `member`, the one gain of the reading (`bus` and `seen`
 * as read now), may be port `bit`'s although other ports are pending: the
 * start reading trusted, every other pending port an at-start port and
 * this one not, every member active (a member another domain claims
 * could hold that domain's tunnel's function), the member one of the
 * start reading's and empty then, and what it gained admitted by
 * XhciTbtGainAdmits (a tunnel's far end, or the link's evidence). FAR_UNREAD
 * when only that shape or port reading could not be read; else FAR_NO.
 */
static ULONG xhciTbtStartAdmits(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                                const XHCI_TBT_BINDS *binds, ULONG bit,
                                ULONG member, const ULONG *bus,
                                const ULONG *seen, const ULONG *active)
{
    ULONG i;

    if (!binds->StartTaken || !binds->StartOk ||
        (binds->AtStart & bit) != 0 ||
        ((binds->Pending & ~bit) & ~binds->AtStart) != 0) {
        return XHCI_TBT_FAR_NO;
    }
    for (i = 0; active != NULL && i < topo->Tunnels; i++) {
        if (!active[i]) {
            return XHCI_TBT_FAR_NO;
        }
    }
    for (i = 0; i < binds->StartCount && i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (binds->StartBdf[i] == topo->Set[member]) {
            if ((binds->StartOcc & (1UL << i)) != 0) {
                return XHCI_TBT_FAR_NO;
            }
            return XhciTbtGainAdmits(io, topo->Set[member], bus[member],
                                     seen[member],
                                     (binds->BaseDown >> member) & 1UL);
        }
    }
    return XHCI_TBT_FAR_NO;
}

/*
 * An attempt that ends before the baseline moves decides no gain, but a
 * gain it saw that XhciTbtGainAdmits rejects outright (no far end, no
 * link evidence: its port not UP now) is rejected as a whole reading
 * would reject it: the member's evidence is spent, so a link coming up
 * later never admits a function already seen answering without it. The
 * members read are 0 to `upto` - 1, and `partial` the one whose scan
 * failed (its devices partly read: any gain there spends its evidence),
 * or XHCI_TBT_NO_MEMBER.
 */
static VOID xhciTbtGainReject(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                              PXHCI_TBT_BINDS binds, const ULONG *bus,
                              const ULONG *seen, ULONG upto, ULONG partial)
{
    ULONG i;
    ULONG bit;

    for (i = 0; i < upto && i < XHCI_TBT_MAX_TUNNELS; i++) {
        bit = 1UL << i;
        if ((seen[i] & ~binds->BaseSeen[i]) == 0 ||
            xhciTbtExplained(binds, topo->Set[i])) {
            continue;
        }
        if (i == partial ||
            XhciTbtGainAdmits(io, topo->Set[i], bus[i], seen[i],
                              (binds->BaseDown & bit) != 0 ? 1UL : 0UL) ==
                XHCI_TBT_GAIN_NO) {
            binds->BaseDown &= ~bit;
            binds->BaseHeld &= ~bit;
        }
    }
}

ULONG XhciTbtLearn(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                   PXHCI_TBT_BINDS binds, ULONG port, const ULONG *active)
{
    ULONG i;
    ULONG bit;
    ULONG bus[XHCI_TBT_MAX_TUNNELS];
    ULONG seen[XHCI_TBT_MAX_TUNNELS];
    ULONG gained = 0;
    ULONG ok;
    ULONG occ = 0;
    ULONG member = XHCI_TBT_NO_MEMBER;
    ULONG admit;
    ULONG taken;
    ULONG result;

    if (io == NULL || io->Read == NULL || topo == NULL || binds == NULL ||
        port == 0 || port > XHCI_TBT_MAX_PORTS ||
        topo->Tunnels > XHCI_TBT_MAX_TUNNELS) {
        return XHCI_TBT_MAP_BAD_PARAM;
    }
    bit = xhciTbtPortBit(port);
    if (xhciTbtBindOfPort(binds, port) != XHCI_TBT_NO_MEMBER) {
        binds->Pending &= ~bit;
        xhciTbtAtStartEnd(binds, port);
        return XHCI_TBT_MAP_KNOWN;
    }
    if (binds->Overflow || (binds->PortConflict & bit)) {
        return XHCI_TBT_MAP_CONFLICT;
    }
    if (!(binds->Pending & bit)) {
        return XHCI_TBT_MAP_NOT_UP;
    }
    if (topo->Tunnels == 0) {
        binds->Armed = 0;
        return XHCI_TBT_MAP_NOT_ARMED;
    }
    if (!xhciTbtBaseFits(topo, binds)) {
        /* No baseline, or one over another set: take one now, which
         * cannot tell what this tunnel brought. */
        XhciTbtLearnArm(io, topo, binds);
        return XHCI_TBT_MAP_NOT_ARMED;
    }
    /* Every attempt that ends before the baseline moves holds the evidence
     * of each member it read occupied (BaseHeld), a device seen in a scan
     * that then failed included: the bit then stands only while that
     * member stays occupied, and a member read empty while its bit is
     * held spends it at once, whatever happens after. */
    for (i = 0; i < topo->Tunnels; i++) {
        ok = xhciTbtOccupancy(io, topo->Set[i], &bus[i], &seen[i]);
        if (seen[i] != 0) {
            occ |= 1UL << i;
        }
        if ((binds->BaseHeld & (1UL << i)) != 0 &&
            (seen[i] == 0 || !ok || bus[i] != binds->BaseBus[i])) {
            /* Held evidence read empty, or behind a scan that failed (what
             * it hid may be that loss), or a member moved: spent. */
            binds->BaseDown &= ~(1UL << i);
            binds->BaseHeld &= ~(1UL << i);
        }
        if (!ok || bus[i] != binds->BaseBus[i]) {
            binds->BaseHeld |= occ;
            xhciTbtGainReject(io, topo, binds, bus, seen, i + 1, i);
            return XHCI_TBT_MAP_UNREAD;
        }
    }
    taken = xhciTbtStartTake(io, topo, binds, bus, seen);
    for (i = 0; i < topo->Tunnels; i++) {
        if ((seen[i] & ~binds->BaseSeen[i]) &&
            !xhciTbtExplained(binds, topo->Set[i])) {
            gained++;
            member = i;
        }
    }
    /* An at-start port is never given a gain while another port is
     * pending (the narrowed rule admits only a port not at start), so its
     * attempt leaves a gain unconsumed while a port not at start is
     * pending: that port's own attempt decides it. */
    if (gained != 0 && (binds->AtStart & bit) != 0 &&
        (binds->Pending & ~binds->AtStart) != 0) {
        binds->BaseHeld |= occ;
        xhciTbtGainReject(io, topo, binds, bus, seen, topo->Tunnels,
                          XHCI_TBT_NO_MEMBER);
        return XHCI_TBT_MAP_SEVERAL;
    }
    /* What one gain is (record 22 section 7.6.4), under either rule: a
     * tunnel's far end, or another shape with the link's evidence. A shape
     * or a port that could not be read is a reading not made: the baseline
     * stays, so the gain is seen again at the next attempt. */
    admit = XHCI_TBT_GAIN_YES;
    if (gained == 1 && binds->Pending != bit) {
        if (!taken) {
            binds->BaseHeld |= occ;
            xhciTbtGainReject(io, topo, binds, bus, seen, topo->Tunnels,
                              XHCI_TBT_NO_MEMBER);
            return XHCI_TBT_MAP_UNREAD;
        }
        admit = xhciTbtStartAdmits(io, topo, binds, bit, member, bus, seen,
                                   active);
    } else if (gained == 1) {
        admit = XhciTbtGainAdmits(io, topo->Set[member], bus[member],
                                  seen[member],
                                  (binds->BaseDown >> member) & 1UL);
    }
    if (admit == XHCI_TBT_GAIN_UNREAD) {
        binds->BaseHeld |= occ;
        xhciTbtGainReject(io, topo, binds, bus, seen, topo->Tunnels,
                          XHCI_TBT_NO_MEMBER);
        return XHCI_TBT_MAP_UNREAD;
    }
    for (i = 0; i < topo->Tunnels; i++) {
        binds->BaseSeen[i] = seen[i];
        if (seen[i] != 0 || (binds->BaseHeld & (1UL << i)) != 0) {
            /* Answering now, or answered at an attempt not completed and
             * empty again: spent either way. */
            binds->BaseDown &= ~(1UL << i);
        }
        if (seen[i] == 0 && (binds->BaseDown & (1UL << i)) == 0 &&
            XhciTbtPortDown(io, topo->Set[i])) {
            binds->BaseDown |= 1UL << i;
        }
    }
    binds->BaseHeld = 0;
    if (gained == 0) {
        return XHCI_TBT_MAP_NOT_GAINED;
    }
    if (gained > 1 || (admit != XHCI_TBT_GAIN_YES && binds->Pending != bit)) {
        /* Another tunnel not yet attributed could explain the gain. */
        return XHCI_TBT_MAP_SEVERAL;
    }
    if (admit != XHCI_TBT_GAIN_YES) {
        /* Pending alone, but what answers is no tunnel's far end and no
         * link came up for it: a function of the controller's own, or of
         * a device that is no tunnel's, answering late is never bound. */
        return XHCI_TBT_MAP_SHAPE;
    }
    /* Only a binding resolves the tunnel: an inactive or conflicting
     * attribution leaves it pending until it is proven down. */
    if (active != NULL && !active[member]) {
        return XHCI_TBT_MAP_INACTIVE;
    }
    result = xhciTbtClaim(binds, port, topo->Set[member],
                          XHCI_TBT_CORR_LEARNED, XHCI_TBT_MAP_LEARNED);
    if (result == XHCI_TBT_MAP_LEARNED) {
        binds->Pending &= ~bit;
        xhciTbtAtStartEnd(binds, port);
    }
    return result;
}

ULONG XhciTbtPortMember(const XHCI_TBT_TOPO *topo,
                        const XHCI_TBT_BINDS *binds, ULONG port)
{
    ULONG e;

    if (topo == NULL || binds == NULL || port == 0 ||
        port > XHCI_TBT_MAX_PORTS || binds->Overflow ||
        (binds->PortConflict & xhciTbtPortBit(port))) {
        return XHCI_TBT_NO_MEMBER;
    }
    e = xhciTbtBindOfPort(binds, port);
    if (e == XHCI_TBT_NO_MEMBER) {
        return XHCI_TBT_NO_MEMBER;
    }
    return xhciTbtMemberOf(topo, binds->Bdf[e]);
}

/* ---- the learning's window --------------------------------------------- */

static ULONG xhciTbtClockPort(const XHCI_TBT_LEARN_CLOCK *clk, ULONG port)
{
    return (clk != NULL && port != 0 && port <= XHCI_TBT_MAX_PORTS) ? 1UL
                                                                    : 0UL;
}

VOID XhciTbtLearnOpen(PXHCI_TBT_LEARN_CLOCK clk, ULONG port, ULONG nowMs,
                      ULONG tag)
{
    ULONG bit;

    if (!xhciTbtClockPort(clk, port)) {
        return;
    }
    bit = xhciTbtPortBit(port);
    clk->Open |= bit;
    clk->Closed &= ~bit;
    clk->Since[port - 1] = nowMs;
    clk->Next[port - 1] = nowMs;
    clk->Gap[port - 1] = 0;
    clk->Tag[port - 1] = tag;
}

ULONG XhciTbtLearnDue(const XHCI_TBT_LEARN_CLOCK *clk, ULONG port,
                      ULONG nowMs)
{
    ULONG bit;

    if (!xhciTbtClockPort(clk, port)) {
        return XHCI_TBT_LEARN_CLOSED;
    }
    bit = xhciTbtPortBit(port);
    if (clk->Closed & bit) {
        return XHCI_TBT_LEARN_CLOSED;
    }
    if (clk->Tried & bit) {
        return XHCI_TBT_LEARN_WAIT;     /* once a pass */
    }
    if (!(clk->Open & bit)) {
        return XHCI_TBT_LEARN_TRY;
    }
    if (nowMs - clk->Since[port - 1] >= XHCI_TBT_LEARN_WINDOW_MS) {
        return XHCI_TBT_LEARN_OVER;
    }
    /* Unsigned: the time since the window opened against the time the
     * next try is due after it, both below the window. */
    return (nowMs - clk->Since[port - 1] >=
            clk->Next[port - 1] - clk->Since[port - 1])
               ? XHCI_TBT_LEARN_TRY
               : XHCI_TBT_LEARN_WAIT;
}

VOID XhciTbtLearnMissed(PXHCI_TBT_LEARN_CLOCK clk, ULONG port, ULONG nowMs,
                        ULONG periodMs)
{
    ULONG gap;

    if (!xhciTbtClockPort(clk, port)) {
        return;
    }
    clk->Tried |= xhciTbtPortBit(port);
    if (!(clk->Open & xhciTbtPortBit(port))) {
        return;
    }
    if (nowMs - clk->Since[port - 1] < XHCI_TBT_LEARN_EAGER_MS) {
        /* The first second: again at the next pass. */
        clk->Gap[port - 1] = 0;
        clk->Next[port - 1] = nowMs;
        return;
    }
    gap = clk->Gap[port - 1];
    if (gap == 0) {
        gap = periodMs != 0 ? periodMs : 1UL;
    } else if (gap >= XHCI_TBT_LEARN_GAP_MAX_MS / 2UL) {
        gap = XHCI_TBT_LEARN_GAP_MAX_MS;
    } else {
        gap *= 2UL;
    }
    if (gap > XHCI_TBT_LEARN_GAP_MAX_MS) {
        gap = XHCI_TBT_LEARN_GAP_MAX_MS;
    }
    clk->Gap[port - 1] = gap;
    clk->Next[port - 1] = nowMs + gap;
}

VOID XhciTbtLearnClose(PXHCI_TBT_LEARN_CLOCK clk, ULONG port)
{
    ULONG bit;

    if (!xhciTbtClockPort(clk, port)) {
        return;
    }
    bit = xhciTbtPortBit(port);
    clk->Open &= ~bit;
    clk->Closed |= bit;
}

VOID XhciTbtLearnEnd(PXHCI_TBT_LEARN_CLOCK clk, ULONG port)
{
    ULONG bit;

    if (!xhciTbtClockPort(clk, port)) {
        return;
    }
    bit = xhciTbtPortBit(port);
    clk->Open &= ~bit;
    clk->Closed &= ~bit;
}

VOID XhciTbtLearnPass(PXHCI_TBT_LEARN_CLOCK clk)
{
    if (clk != NULL) {
        clk->Tried = 0;
    }
}

ULONG XhciTbtLearnBound(PXHCI_TBT_BINDS binds, ULONG port)
{
    if (binds == NULL || port == 0 || port > XHCI_TBT_MAX_PORTS ||
        xhciTbtBindOfPort(binds, port) == XHCI_TBT_NO_MEMBER) {
        return 0;
    }
    binds->Pending &= ~xhciTbtPortBit(port);
    xhciTbtAtStartEnd(binds, port);
    return 1;
}

/* ---- a tunnel found up at start: its port inferred --------------------- */

/*
 * One member's link as I7 reads it: 1 with *active, 0 with *why (UNREAD
 * or NO_REPORT).
 */
static ULONG xhciTbtInferLink(PXHCI_TBT_CFG io, ULONG bdf, PULONG active,
                              PULONG why)
{
    ULONG cap;
    ULONG st;
    ULONG caps;
    ULONG linkCap;
    ULONG linkSta;
    ULONG valid;

    *active = 0;
    st = XhciTbtFindCapStatus(io, bdf, XHCI_TBT_CAP_PCIE, &cap);
    if (st != XHCI_TBT_CAP_FOUND) {
        *why = st == XHCI_TBT_CAP_ABSENT ? XHCI_TBT_INFER_NO_REPORT
                                         : XHCI_TBT_INFER_UNREAD;
        return 0;
    }
    if (!xhciTbtRead(io, bdf, cap + XHCI_TBT_PCIE_CAPS, &caps) ||
        caps == XHCI_TBT_ALL_ONES ||
        !xhciTbtRead(io, bdf, cap + XHCI_TBT_PCIE_LINKCAP, &linkCap) ||
        linkCap == XHCI_TBT_ALL_ONES ||
        !xhciTbtRead(io, bdf, cap + XHCI_TBT_PCIE_LINKSTA, &linkSta) ||
        linkSta == XHCI_TBT_ALL_ONES) {
        *why = XHCI_TBT_INFER_UNREAD;
        return 0;
    }
    XhciTbtDecodeLink(caps >> 16, linkCap, linkSta >> 16, &valid, active);
    if (!valid) {
        *why = XHCI_TBT_INFER_NO_REPORT;
        return 0;
    }
    return 1;
}

ULONG XhciTbtInfer(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                   const XHCI_TBT_BINDS *binds, const XHCI_TBT_INFER_IN *in,
                   PXHCI_TBT_INFER_OUT out)
{
    ULONG i;
    ULONG n;
    ULONG up;
    ULONG port;
    ULONG want;
    ULONG why;
    ULONG bus;
    ULONG member;
    ULONG occupied;
    ULONG first[XHCI_TBT_MAX_TUNNELS];
    ULONG again;
    ULONG seen[XHCI_TBT_MAX_TUNNELS];
    ULONG link[XHCI_TBT_MAX_TUNNELS];
    ULONG farEnd[XHCI_TBT_MAX_TUNNELS];

    if (out != NULL) {
        xhciTbtZero((PULONG)out, sizeof(*out) / sizeof(ULONG));
    }
    if (io == NULL || io->Read == NULL || topo == NULL || binds == NULL ||
        in == NULL || out == NULL || in->Ports == 0 ||
        in->Ports > XHCI_TBT_MAX_PORTS) {
        return XHCI_TBT_INFER_BAD_PARAM;
    }
    if (!in->Usb4Host) {
        return XHCI_TBT_INFER_HOST;
    }
    n = topo->Tunnels;
    if (topo->Outcome != XHCI_TBT_TOPO_SET ||
        topo->FindOutcome != XHCI_TBT_TOPO_SET || n == 0 ||
        n > XHCI_TBT_MAX_TUNNELS) {
        return XHCI_TBT_INFER_SET;
    }
    for (i = 0; i < n; i++) {
        if (topo->Source[i] != XHCI_TBT_SRC_SWITCH) {
            return XHCI_TBT_INFER_SET;
        }
    }
    for (i = 0; i < n; i++) {
        if (in->Active != NULL && !in->Active[i]) {
            return XHCI_TBT_INFER_INACTIVE;
        }
    }
    if (binds->Count != 0 || binds->Overflow || binds->PortConflict) {
        return XHCI_TBT_INFER_BOUND;
    }
    up = in->UpMask;
    if (in->Ports < 32) {
        up &= (1UL << in->Ports) - 1;
    }
    if (up == 0 || (up & (up - 1)) != 0) {
        return XHCI_TBT_INFER_NOT_ONE;
    }
    if (in->Conflict & up) {
        return XHCI_TBT_INFER_CONFLICTED;
    }
    if (!(binds->AtStart & up)) {
        return XHCI_TBT_INFER_NOT_AT_START;
    }
    if (binds->Pending != up) {
        return XHCI_TBT_INFER_PENDING;
    }
    port = 0;
    for (i = 1; i <= in->Ports; i++) {
        if (up == xhciTbtPortBit(i)) {
            port = i;
        }
        want = up == xhciTbtPortBit(i) ? XHCI_TBT_RLINK_UP
                                       : XHCI_TBT_RLINK_DOWN;
        if (in->RouterLink[i - 1] != want) {
            return XHCI_TBT_INFER_ROUTER;
        }
    }
    if (!xhciTbtBaseFits(topo, binds)) {
        return XHCI_TBT_INFER_NOT_ARMED;
    }
    /* One pass: the bus numbers, each member's link, occupancy and the
     * shape of what answers there, the bus numbers again. */
    for (i = 0; i < n; i++) {
        if (!xhciTbtRead(io, topo->Set[i], XHCI_TBT_CFG_BUSES, &first[i]) ||
            first[i] == XHCI_TBT_ALL_ONES) {
            return XHCI_TBT_INFER_UNREAD;
        }
    }
    for (i = 0; i < n; i++) {
        if (!xhciTbtInferLink(io, topo->Set[i], &link[i], &why)) {
            return why;
        }
        if (!xhciTbtOccupancy(io, topo->Set[i], &bus, &seen[i])) {
            return XHCI_TBT_INFER_UNREAD;
        }
        if (bus != ((first[i] >> 8) & 0xFFUL) || bus != binds->BaseBus[i]) {
            return XHCI_TBT_INFER_MOVED;
        }
        farEnd[i] = seen[i] != 0 ? xhciTbtFarEnd(io, bus, seen[i])
                                 : XHCI_TBT_FAR_NO;
        if (farEnd[i] == XHCI_TBT_FAR_UNREAD) {
            return XHCI_TBT_INFER_UNREAD;
        }
    }
    for (i = 0; i < n; i++) {
        if (!xhciTbtRead(io, topo->Set[i], XHCI_TBT_CFG_BUSES, &again) ||
            again == XHCI_TBT_ALL_ONES) {
            return XHCI_TBT_INFER_UNREAD;
        }
        if (again != first[i]) {
            return XHCI_TBT_INFER_MOVED;
        }
    }
    occupied = 0;
    member = XHCI_TBT_NO_MEMBER;
    for (i = 0; i < n; i++) {
        if (seen[i] != 0) {
            occupied++;
            member = i;
        }
    }
    if (occupied != 1) {
        return XHCI_TBT_INFER_OCCUPANCY;
    }
    if (farEnd[member] != XHCI_TBT_FAR_YES) {
        return XHCI_TBT_INFER_SHAPE;
    }
    for (i = 0; i < n; i++) {
        if ((seen[i] != 0) != (link[i] != 0)) {
            return XHCI_TBT_INFER_LINKS;
        }
    }
    out->Port = port;
    out->Member = member;
    out->Bdf = topo->Set[member];
    out->Buses = first[member];
    return XHCI_TBT_INFER_MADE;
}

ULONG XhciTbtInferReadBack(PXHCI_TBT_CFG io, ULONG bdf, ULONG buses)
{
    ULONG v;
    ULONG active;
    ULONG why;
    ULONG bus;
    ULONG seen;

    if (io == NULL || io->Read == NULL) {
        return XHCI_TBT_RB_UNREAD;
    }
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_BUSES, &v) || v != buses ||
        v == XHCI_TBT_ALL_ONES) {
        return XHCI_TBT_RB_UNREAD;
    }
    if (!xhciTbtInferLink(io, bdf, &active, &why) ||
        !xhciTbtOccupancy(io, bdf, &bus, &seen) ||
        bus != ((buses >> 8) & 0xFFUL)) {
        return XHCI_TBT_RB_UNREAD;
    }
    return (active || seen != 0) ? XHCI_TBT_RB_OPEN : XHCI_TBT_RB_CLOSED;
}

ULONG XhciTbtFunctionRead(PXHCI_TBT_CFG io, ULONG bdf)
{
    ULONG v;
    ULONG primary;
    ULONG secondary;
    ULONG subordinate;
    ULONG vendor;
    ULONG dev;
    ULONG retry;

    if (io == NULL || io->Read == NULL || bdf > 0xFFFFUL) {
        return XHCI_TBT_FN_UNREAD;
    }
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_BUSES, &v) ||
        v == XHCI_TBT_ALL_ONES) {
        return XHCI_TBT_FN_UNREAD;
    }
    primary = v & 0xFFUL;
    secondary = (v >> 8) & 0xFFUL;
    subordinate = (v >> 16) & 0xFFUL;
    if (secondary == 0 || secondary == 0xFFUL || secondary <= primary ||
        subordinate < secondary) {
        return XHCI_TBT_FN_UNREAD;
    }
    /* Function 0 of every device number: behind a PCI Express link only
     * device 0 answers, but a PCI bridge's slot (SHPC) puts its card at
     * the slot's own device number. */
    retry = 0;
    for (dev = 0; dev < 32; dev++) {
        if (!xhciTbtRead(io, XHCI_TBT_BDF(secondary, dev, 0),
                         XHCI_TBT_CFG_ID, &v)) {
            return XHCI_TBT_FN_UNREAD;
        }
        vendor = v & 0xFFFFUL;
        if (vendor == 0x0001UL) {
            retry = 1;
        } else if (vendor != 0xFFFFUL && vendor != 0UL) {
            return XHCI_TBT_FN_PRESENT;
        }
    }
    return retry ? XHCI_TBT_FN_NOT_READY : XHCI_TBT_FN_ABSENT;
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
        port->ShpcSeen = 1;
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

/* ---- chain ports (record 34 sections 3.1 and 3.9) --------------------- */

static ULONG xhciTbtChainFail(PXHCI_TBT_CHAIN_SET out, ULONG why, ULONG bdf)
{
    out->Incomplete = 1;
    out->Why = why;
    out->FailBdf = bdf;
    return XHCI_TBT_CHAIN_INCOMPLETE;
}

/*
 * One function the walk found present: a chain-port candidate is recorded;
 * any other bridge whose secondary bus is above its own and inside
 * [lo, hi] is pushed for the walk, unless it is at the last level.
 * Returns 0 when the walk is incomplete (out says why).
 */
static ULONG xhciTbtChainFn(PXHCI_TBT_CFG io, ULONG bdf, ULONG id,
                            ULONG header, ULONG member, ULONG level,
                            ULONG rot, ULONG lo, ULONG hi, PULONG stack,
                            PULONG levels, PULONG rots, PULONG top,
                            PULONG visited, PXHCI_TBT_CHAIN_SET out)
{
    ULONG buses;
    ULONG sec;
    ULONG sub;
    ULONG off;
    ULONG cap;
    ULONG caps;
    ULONG slotCap;
    ULONG slotCtl;
    PXHCI_TBT_CHAIN_PORT p;

    if (XHCI_TBT_HDR_LAYOUT(header >> 16) != XHCI_TBT_HDR_BRIDGE) {
        return 1;
    }
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_BUSES, &buses)) {
        xhciTbtChainFail(out, XHCI_TBT_CHAIN_WHY_READ, bdf);
        return 0;
    }
    sec = (buses >> 8) & 0xFFUL;
    sub = (buses >> 16) & 0xFFUL;
    cap = XhciTbtFindCapStatus(io, bdf, XHCI_TBT_CAP_PCIE, &off);
    if (cap == XHCI_TBT_CAP_FAILED || cap == XHCI_TBT_CAP_GONE) {
        xhciTbtChainFail(out, XHCI_TBT_CHAIN_WHY_CAP, bdf);
        return 0;
    }
    if (cap == XHCI_TBT_CAP_FOUND) {
        if (!xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_CAPS, &caps) ||
            !xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_SLOTCAP, &slotCap)) {
            xhciTbtChainFail(out, XHCI_TBT_CHAIN_WHY_READ, bdf);
            return 0;
        }
        caps = (caps >> 16) & 0xFFFFUL;
        if (XHCI_TBT_PCIE_TYPE(caps) == XHCI_TBT_PCIE_TYPE_DOWN &&
            XhciTbtPcieHotPlug(caps, slotCap)) {
            if (out->Count >= XHCI_TBT_CHAIN_MAX) {
                out->Over++;
                return 1;
            }
            p = &out->Port[out->Count];
            p->Bdf = bdf;
            p->Id = id;
            p->Secondary = sec;
            p->Subordinate = sub;
            p->Member = member;
            p->Depth = level + 1;
            p->Unpowered = 0;
            p->Rot = (rot + XHCI_TBT_BDF_DEV(bdf)) & 3UL;
            if (slotCap & XHCI_TBT_SLOTCAP_PCP) {
                if (!xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_SLOTSTA,
                                 &slotCtl)) {
                    xhciTbtChainFail(out, XHCI_TBT_CHAIN_WHY_READ, bdf);
                    return 0;
                }
                p->Unpowered = (slotCtl & XHCI_TBT_SLOTCTL_PCC_OFF) ? 1UL
                                                                     : 0UL;
            }
            out->Count++;
            /* One level of chain ports: nothing below a candidate. */
            return 1;
        }
    }
    if (sec <= XHCI_TBT_BDF_BUS(bdf) || sec < lo || sec > hi || sub < sec ||
        sub > hi || level + 1 >= XHCI_TBT_CHAIN_LEVELS ||
        ((visited[sec >> 5] >> (sec & 31UL)) & 1UL)) {
        /* Unnumbered, outside the member's range, too deep, or walked. */
        return 1;
    }
    if (*top >= XHCI_TBT_CHAIN_STACK) {
        xhciTbtChainFail(out, XHCI_TBT_CHAIN_WHY_STACK, bdf);
        return 0;
    }
    visited[sec >> 5] |= 1UL << (sec & 31UL);
    stack[*top] = sec;
    levels[*top] = level + 1;
    rots[*top] = (rot + XHCI_TBT_BDF_DEV(bdf)) & 3UL;
    (*top)++;
    return 1;
}

ULONG XhciTbtChainPorts(PXHCI_TBT_CFG io, ULONG memberBdf, ULONG member,
                        PXHCI_TBT_CHAIN_SET out)
{
    ULONG stack[XHCI_TBT_CHAIN_STACK];
    ULONG levels[XHCI_TBT_CHAIN_STACK];
    ULONG rots[XHCI_TBT_CHAIN_STACK];
    ULONG visited[8];
    ULONG top;
    ULONG bus;
    ULONG level;
    ULONG rot;
    ULONG buses;
    ULONG lo;
    ULONG hi;
    ULONG dev;
    ULONG fn;
    ULONG fns;
    ULONG bdf;
    ULONG id;
    ULONG header;
    ULONG vendor;
    ULONG i;

    if (out == NULL) {
        return XHCI_TBT_CHAIN_BAD_PARAM;
    }
    for (i = 0; i < sizeof(*out) / sizeof(ULONG); i++) {
        ((PULONG)out)[i] = 0;
    }
    out->FailBdf = XHCI_TBT_NO_BDF;
    if (io == NULL || io->Read == NULL || memberBdf > 0xFFFFUL) {
        return XHCI_TBT_CHAIN_BAD_PARAM;
    }
    if (!xhciTbtRead(io, memberBdf, XHCI_TBT_CFG_BUSES, &buses) ||
        buses == XHCI_TBT_ALL_ONES) {
        return xhciTbtChainFail(out, XHCI_TBT_CHAIN_WHY_MEMBER, memberBdf);
    }
    lo = (buses >> 8) & 0xFFUL;
    hi = (buses >> 16) & 0xFFUL;
    if (lo <= XHCI_TBT_BDF_BUS(memberBdf) || hi < lo) {
        return xhciTbtChainFail(out, XHCI_TBT_CHAIN_WHY_MEMBER, memberBdf);
    }
    xhciTbtZero(visited, 8);
    visited[lo >> 5] |= 1UL << (lo & 31UL);
    stack[0] = lo;
    levels[0] = 0;
    rots[0] = 0;
    top = 1;
    while (top != 0) {
        top--;
        bus = stack[top];
        level = levels[top];
        rot = rots[top];
        for (dev = 0; dev < 32; dev++) {
            fns = 1;
            for (fn = 0; fn < fns; fn++) {
                bdf = XHCI_TBT_BDF(bus, dev, fn);
                if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_ID, &id)) {
                    return xhciTbtChainFail(out, XHCI_TBT_CHAIN_WHY_READ,
                                            bdf);
                }
                vendor = id & 0xFFFFUL;
                if (vendor == 0xFFFFUL || vendor == 0UL) {
                    continue;
                }
                if (vendor == 0x0001UL) {
                    return xhciTbtChainFail(out, XHCI_TBT_CHAIN_WHY_RETRY,
                                            bdf);
                }
                if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_HEADER, &header) ||
                    header == XHCI_TBT_ALL_ONES) {
                    return xhciTbtChainFail(out, XHCI_TBT_CHAIN_WHY_READ,
                                            bdf);
                }
                if (fn == 0 && ((header >> 16) & XHCI_TBT_HDR_MF) != 0) {
                    fns = 8;
                }
                if (!xhciTbtChainFn(io, bdf, id, header, member, level, rot,
                                    lo, hi, stack, levels, rots, &top,
                                    visited, out)) {
                    return XHCI_TBT_CHAIN_INCOMPLETE;
                }
            }
        }
    }
    return XHCI_TBT_CHAIN_COMPLETE;
}

ULONG XhciTbtChainOwe(PXHCI_TBT_CHAIN_OWED owed, ULONG count, ULONG key,
                      ULONG serial, ULONG missing, ULONG nowMs)
{
    ULONG i;
    ULONG slot;

    if (owed == NULL) {
        return count;
    }
    slot = count;
    for (i = 0; i < count; i++) {
        if (owed[i].Used && owed[i].Key == key) {
            if (owed[i].Serial != serial) {
                owed[i].Serial = serial;
                owed[i].Since = nowMs;
                owed[i].DoneCount = 0;
            }
            owed[i].Missing = missing;
            return i;
        }
        if (!owed[i].Used && slot == count) {
            slot = i;
        }
    }
    if (slot == count) {
        return count;
    }
    owed[slot].Used = 1;
    owed[slot].Key = key;
    owed[slot].Serial = serial;
    owed[slot].Since = nowMs;
    owed[slot].Missing = missing;
    owed[slot].DoneCount = 0;
    return slot;
}

ULONG XhciTbtChainDone(const XHCI_TBT_CHAIN_OWED *e, ULONG bdf)
{
    ULONG i;

    if (e == NULL) {
        return 0;
    }
    for (i = 0; i < e->DoneCount; i++) {
        if (e->Done[i] == bdf) {
            return 1;
        }
    }
    return 0;
}

VOID XhciTbtChainMark(PXHCI_TBT_CHAIN_OWED e, ULONG bdf)
{
    if (e == NULL || XhciTbtChainDone(e, bdf) ||
        e->DoneCount >= sizeof(e->Done) / sizeof(e->Done[0])) {
        return;
    }
    e->Done[e->DoneCount++] = bdf;
}

ULONG XhciTbtChainOwedExpired(const XHCI_TBT_CHAIN_OWED *e, ULONG nowMs)
{
    if (e == NULL || !e->Used) {
        return 0;
    }
    return (nowMs - e->Since >= XHCI_TBT_CHAIN_OWED_MS) ? 1UL : 0UL;
}

ULONG XhciTbtChainPortTest(PXHCI_TBT_CFG io, ULONG bdf, ULONG memberBdf,
                           PULONG secondary)
{
    ULONG buses;
    ULONG lo;
    ULONG hi;
    ULONG id;
    ULONG header;
    ULONG sec;
    ULONG sub;
    ULONG bus;
    ULONG cap;
    ULONG off;
    ULONG caps;
    ULONG slotCap;

    if (secondary != NULL) {
        *secondary = 0;
    }
    if (io == NULL || io->Read == NULL || secondary == NULL ||
        bdf > 0xFFFFUL || memberBdf > 0xFFFFUL) {
        return XHCI_TBT_CPT_FAILED;
    }
    if (!xhciTbtRead(io, memberBdf, XHCI_TBT_CFG_BUSES, &buses) ||
        buses == XHCI_TBT_ALL_ONES) {
        return XHCI_TBT_CPT_MEMBER;
    }
    lo = (buses >> 8) & 0xFFUL;
    hi = (buses >> 16) & 0xFFUL;
    if (lo <= XHCI_TBT_BDF_BUS(memberBdf) || hi < lo) {
        return XHCI_TBT_CPT_MEMBER;
    }
    bus = XHCI_TBT_BDF_BUS(bdf);
    if (bus < lo || bus > hi) {
        return XHCI_TBT_CPT_OUTSIDE;
    }
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_ID, &id) ||
        (id & 0xFFFFUL) == 0xFFFFUL || (id & 0xFFFFUL) == 0x0001UL ||
        !xhciTbtRead(io, bdf, XHCI_TBT_CFG_HEADER, &header) ||
        header == XHCI_TBT_ALL_ONES) {
        return XHCI_TBT_CPT_FAILED;
    }
    if ((id & 0xFFFFUL) == 0UL ||
        XHCI_TBT_HDR_LAYOUT(header >> 16) != XHCI_TBT_HDR_BRIDGE) {
        return XHCI_TBT_CPT_OUTSIDE;
    }
    if (!xhciTbtRead(io, bdf, XHCI_TBT_CFG_BUSES, &buses) ||
        buses == XHCI_TBT_ALL_ONES) {
        return XHCI_TBT_CPT_FAILED;
    }
    sec = (buses >> 8) & 0xFFUL;
    sub = (buses >> 16) & 0xFFUL;
    if (sec <= bus || sec <= lo || sub < sec || sub > hi) {
        return XHCI_TBT_CPT_UNNUMBERED;
    }
    cap = XhciTbtFindCapStatus(io, bdf, XHCI_TBT_CAP_PCIE, &off);
    if (cap == XHCI_TBT_CAP_FAILED || cap == XHCI_TBT_CAP_GONE) {
        return XHCI_TBT_CPT_FAILED;
    }
    if (cap != XHCI_TBT_CAP_FOUND) {
        return XHCI_TBT_CPT_NOT;
    }
    if (!xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_CAPS, &caps) ||
        !xhciTbtRead(io, bdf, off + XHCI_TBT_PCIE_SLOTCAP, &slotCap) ||
        caps == XHCI_TBT_ALL_ONES || slotCap == XHCI_TBT_ALL_ONES) {
        /* A function that stopped answering reads all ones through an
         * access that succeeds. */
        return XHCI_TBT_CPT_FAILED;
    }
    caps = (caps >> 16) & 0xFFFFUL;
    if (XHCI_TBT_PCIE_TYPE(caps) != XHCI_TBT_PCIE_TYPE_DOWN ||
        !XhciTbtPcieHotPlug(caps, slotCap)) {
        return XHCI_TBT_CPT_NOT;
    }
    *secondary = sec;
    return XHCI_TBT_CPT_YES;
}
