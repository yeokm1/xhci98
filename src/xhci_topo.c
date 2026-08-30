/*
 * xhci_topo.c - the hub topology graph, roadmap task 7b-A.1.
 *
 * See src/xhci_topo.h for what this reconstructs, why snooping is required at
 * all, and where every wire constant was measured. This file is the state
 * machine, and three rules hold throughout:
 *
 *   **It decides nothing itself.** Everything here answers a question; what to
 *   do about the answer is the caller's. Task 7b-A.2 asks `XhciTopoHubMark`
 *   what a hub's own Slot Context should say and issues the command;
 *   task 7b-A.3 asks for a downstream device's position, through
 *   `XhciTopoClaimChild` and `XhciTopoAttachChild` in `src/xhci_slot.c`.
 *   Nothing below returns a status a transfer or a register write depends on.
 *   *(7b-A.3 was written here as a task that "will ask" until the post-Phase 13 review rounds; it
 *   has been landed and consuming this graph since Phase 7b.)*
 *
 *   **It never holds a pointer usbport owns.** The reply fold takes bytes the
 *   caller has already read; the caller is the only thing that ever touches a
 *   mapped transfer buffer, and it does so inside the callback that owns it.
 *
 *   **It refuses rather than guesses.** A parent nothing identified, a
 *   descriptor that is not self-consistent, a path deeper than the Route String
 *   can express - each is counted and dropped. A wrongly-addressed device
 *   corrupts the schedule; an unaddressed one does not (design doc 02
 *   section 3).
 *
 * C89 only. No MMIO, no DDK, no lock: the caller holds the controller lock
 * across every call.
 */

#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_topo.h"

/* ------------------------------------------------------------------ */
/* Table                                                               */
/* ------------------------------------------------------------------ */

/*
 * The one walk. `XhciTopoFind` is this function with the const the public
 * signature promises - two spellings of "which node is this" is how they stop
 * agreeing, which batch 7a-B paid for in the transfer queue.
 *
 * IRQL: any.
 */
static XHCI_TOPO_NODE *xhciTopoNodeFor(PXHCI_TOPOLOGY topo, ULONG address)
{
    ULONG i;

    if (topo == NULL || address == 0) {
        return NULL;
    }

    for (i = 0; i < XHCI_TOPO_NODES; i++) {
        if ((topo->Node[i].Flags & XHCI_TOPO_F_USED) != 0 &&
            topo->Node[i].Address == address) {
            return &topo->Node[i];
        }
    }
    return NULL;
}

/*
 * Clear one node. Written as a field-by-field store rather than a memset
 * because this file has no DDK and the pure core does not pull in string.h -
 * the same reason src/xhci_ring.c zeroes a TRB the long way.
 *
 * IRQL: any.
 */
static VOID xhciTopoNodeClear(XHCI_TOPO_NODE *node)
{
    node->Flags = 0;
    node->Address = 0;
    node->Speed = XHCI_SPEED_UNKNOWN;
    node->Tier = 0;
    node->Route = 0;
    node->RootPort = 0;
    node->Generation = 0;
    node->ParentAddress = 0;
    node->ParentPort = 0;
    node->PortCount = 0;
    node->Characteristics = 0;
    node->DescriptorType = 0;
    node->Connected = 0;
    node->Disconnects = 0;
}

/* IRQL: any. */
VOID XhciTopoReset(PXHCI_TOPOLOGY topo)
{
    ULONG i;

    if (topo == NULL) {
        return;
    }

    for (i = 0; i < XHCI_TOPO_NODES; i++) {
        xhciTopoNodeClear(&topo->Node[i]);
    }

    topo->Count = 0;
    topo->Dropped = 0;
    topo->Pending = 0;
    topo->PendingHub = 0;
    topo->PendingPort = 0;

    topo->Promotions = 0;
    topo->Resets = 0;
    topo->ResetsUnknownHub = 0;
    topo->ResetsOverwritten = 0;
    topo->Claims = 0;
    topo->ClaimsUnarmed = 0;
    topo->ClaimsUnusable = 0;
    topo->ClaimsTooDeep = 0;
    topo->PendingDropped = 0;
    topo->ResetsSuppressed = 0;
    topo->Migrations = 0;
    topo->MigrationsStale = 0;
    topo->PowerSweeps = 0;
    topo->Descriptors = 0;
    topo->DescriptorsBad = 0;
    topo->DescriptorsNoPorts = 0;
    topo->PortStatuses = 0;
    topo->PortStatusesWide = 0;
    topo->Disconnects = 0;
    topo->Reconnects = 0;
    topo->AltSettings = 0;
    topo->AltForgotten = 0;
    topo->Prunes = 0;
    topo->MaxTier = 0;
}

/*
 * Find or create the node for `address`.
 *
 * A created node has a position of nothing at all - `RootPort` 0 - because the
 * only caller that creates one is the promotion path, which learns "this device
 * is a hub" from traffic that says nothing about where it sits. The attach
 * entry points are what give it a position, and `XhciTopoChildOf` refuses a
 * parent whose `RootPort` is still 0 rather than deriving a route from it.
 *
 * IRQL: any.
 */
static XHCI_TOPO_NODE *xhciTopoNodeAdd(PXHCI_TOPOLOGY topo, ULONG address)
{
    XHCI_TOPO_NODE *node;
    ULONG i;

    if (address == 0) {
        /* Every lookup rejects address 0, so a node built with one would occupy
         * a slot nothing could ever find, reuse or prune. */
        return NULL;
    }

    node = xhciTopoNodeFor(topo, address);
    if (node != NULL) {
        return node;
    }

    for (i = 0; i < XHCI_TOPO_NODES; i++) {
        if ((topo->Node[i].Flags & XHCI_TOPO_F_USED) == 0) {
            node = &topo->Node[i];
            xhciTopoNodeClear(node);
            node->Flags = XHCI_TOPO_F_USED;
            node->Address = address;
            topo->Count++;
            return node;
        }
    }

    topo->Dropped++;
    return NULL;
}

/*
 * Take one node out, and forget any pending claim that named it.
 *
 * The second half is not tidiness. A claim is (hub address, port); if the hub
 * leaves between the reset and the address-0 open that would spend it, the pair
 * would be handed out against a node that no longer exists, or - worse - a node
 * a later device has been given the same address as. Dropping it makes the next
 * open unarmed, which is a refusal rather than a wrong route.
 *
 * IRQL: any.
 */
static VOID xhciTopoNodeRemove(PXHCI_TOPOLOGY topo, XHCI_TOPO_NODE *node)
{
    ULONG address;

    address = node->Address;

    xhciTopoNodeClear(node);
    if (topo->Count != 0) {
        topo->Count--;
    }
    topo->Prunes++;

    if (topo->Pending && topo->PendingHub == address) {
        topo->Pending = 0;
        topo->PendingHub = 0;
        topo->PendingPort = 0;
    }
}

/* IRQL: any. */
const XHCI_TOPO_NODE *XhciTopoFind(const XHCI_TOPOLOGY *topo, ULONG address)
{
    return xhciTopoNodeFor((PXHCI_TOPOLOGY)topo, address);
}

/* ------------------------------------------------------------------ */
/* Position arithmetic                                                 */
/* ------------------------------------------------------------------ */

/* IRQL: any. */
ULONG XhciTopoChildOf(const XHCI_TOPOLOGY *topo,
                      ULONG hubAddress,
                      ULONG hubPort,
                      PXHCI_TOPO_CHILD out)
{
    const XHCI_TOPO_NODE *node;
    ULONG port;

    if (topo == NULL || out == NULL || hubPort == 0) {
        return 0;
    }

    node = XhciTopoFind(topo, hubAddress);
    if (node == NULL) {
        return 0;
    }
    /*
     * A hub whose own position was never learned cannot give a child one. This
     * is reachable: the promotion path creates a node the moment hub-class
     * traffic identifies a device, which is before anything says where it is.
     */
    if (node->RootPort == 0) {
        return 0;
    }

    out->HubAddress = hubAddress;
    out->HubPort = hubPort;
    out->RootPort = node->RootPort;
    out->Tier = node->Tier + 1;

    /*
     * Footnote 106's clamp, not a mask: port 16 masked to 4 bits is 0, which is
     * the encoding for "no further tier" and would silently address the hub
     * itself.
     */
    port = hubPort;
    if (port > XHCI_TOPO_ROUTE_MAX_PORT) {
        port = XHCI_TOPO_ROUTE_MAX_PORT;
    }

    if (out->Tier > XHCI_TOPO_MAX_TIER) {
        /*
         * Past the five nibbles the Route String holds. The route is left as
         * the parent's rather than shifted off the end, and TooDeep is what the
         * caller must read - a truncated route names a *different, real* device
         * (task 7b-A.3: "Reject paths deeper than xHCI's five-tier Route String
         * instead of truncating them").
         */
        out->Route = node->Route;
        out->TooDeep = 1;
        return 1;
    }

    out->Route = node->Route | (port << (4U * node->Tier));
    out->TooDeep = 0;
    return 1;
}

/* IRQL: any. */
ULONG XhciTopoThinkTime(const XHCI_TOPO_NODE *node)
{
    if (node == NULL || (node->Flags & XHCI_TOPO_F_DESCRIPTOR) == 0) {
        return 0;
    }
    return (node->Characteristics >> XHCI_TOPO_CHAR_TTT_SHIFT) &
           XHCI_TOPO_CHAR_TTT_MASK;
}

/* IRQL: any. */
ULONG XhciTopoHubMark(const XHCI_TOPO_NODE *node,
                      ULONG speedClass,
                      PXHCI_TOPO_HUBMARK out)
{
    if (out == NULL) {
        return 0;
    }
    out->Hub = 0;
    out->NumberOfPorts = 0;
    out->MultiTt = 0;
    out->TtThinkTime = 0;

    if (node == NULL || (node->Flags & XHCI_TOPO_F_HUB) == 0) {
        return 0;
    }
    /*
     * **The descriptor, not the port-power high-water mark.** The mark carries
     * `Number of Ports`, and the sweep's bound is a lower bound the header of
     * this file is explicit about; programming it would tell the xHC a hub is
     * narrower than it is on any bus where usbhub has not yet powered every
     * port. Task 7b-A.2's own wording is "at the point the required descriptor
     * facts become available", and this is that point.
     */
    if ((node->Flags & XHCI_TOPO_F_DESCRIPTOR) == 0) {
        return 0;
    }
    /*
     * A hub with no ports, or more than the field holds. `bNbrPorts` is one
     * byte so the upper test is unreachable through the fold - it is here
     * because this structure is also reachable from a caller that did not come
     * through it, and because the Slot Context builder would refuse the value
     * anyway and a refusal one layer down is a marking that never happens with
     * nothing saying why.
     */
    if (node->PortCount == 0 || node->PortCount > 0xFFUL) {
        return 0;
    }

    out->Hub = 1;
    out->NumberOfPorts = node->PortCount;
    if (speedClass == XHCI_SPEED_HIGH) {
        out->TtThinkTime = XhciTopoThinkTime(node);
        out->MultiTt = ((node->Flags & XHCI_TOPO_F_MTT) != 0) ? 1UL : 0UL;
    }
    return 1;
}

/* IRQL: any. */
ULONG XhciTopoTtFor(const XHCI_TOPOLOGY *topo,
                    ULONG hubAddress,
                    ULONG hubPort,
                    PXHCI_TOPO_TT out)
{
    const XHCI_TOPO_NODE *node;
    ULONG port;
    ULONG steps;

    if (out == NULL) {
        return 0;
    }
    out->HubAddress = 0;
    out->HubPort = 0;
    out->MultiTt = 0;

    if (topo == NULL || hubAddress == 0 || hubPort == 0) {
        return 0;
    }

    /*
     * Upward from the immediate parent, carrying the port the path leaves each
     * hub by. That port is the answer's second half and is why the walk cannot
     * be written as "find an HS node": the TT's port is the port on the **HS
     * hub**, which is the port of whichever node hangs directly off it - not
     * the port the device itself is plugged into, unless the two are the same
     * hub. This is the same walk `USBPORT_GetTt` performs (batch 7a-0), which
     * is what makes usbport's pair a cross-check on this rather than a second
     * opinion.
     */
    node = XhciTopoFind(topo, hubAddress);
    port = hubPort;
    for (steps = 0; steps < XHCI_TOPO_NODES && node != NULL; steps++) {
        if (node->Speed == XHCI_SPEED_HIGH) {
            out->HubAddress = node->Address;
            out->HubPort = port;
            out->MultiTt = ((node->Flags & XHCI_TOPO_F_MTT) != 0) ? 1UL : 0UL;
            return 1;
        }
        if (node->ParentAddress == 0) {
            /* A hub on a root port, and not High-Speed: the whole path runs at
             * that hub's speed and no transaction translator exists on it. */
            break;
        }
        port = node->ParentPort;
        node = XhciTopoFind(topo, node->ParentAddress);
    }
    return 0;
}

/* IRQL: any. */
static VOID xhciTopoNotePosition(PXHCI_TOPOLOGY topo, XHCI_TOPO_NODE *node)
{
    if (node->Tier > topo->MaxTier) {
        topo->MaxTier = node->Tier;
    }
    if (node->Tier > XHCI_TOPO_MAX_TIER) {
        node->Flags |= XHCI_TOPO_F_TOO_DEEP;
    } else {
        node->Flags &= ~XHCI_TOPO_F_TOO_DEEP;
    }
}

/* ------------------------------------------------------------------ */
/* Attaching                                                           */
/* ------------------------------------------------------------------ */

/* IRQL: any. */
ULONG XhciTopoAttachRoot(PXHCI_TOPOLOGY topo,
                         ULONG address,
                         ULONG rootPort,
                         ULONG generation,
                         ULONG speed)
{
    XHCI_TOPO_NODE *node;

    if (topo == NULL || address == 0 || rootPort == 0) {
        return 0;
    }

    node = xhciTopoNodeAdd(topo, address);
    if (node == NULL) {
        return 0;
    }

    node->Tier = 0;
    node->Route = 0;
    node->RootPort = rootPort;
    node->Generation = generation;
    node->ParentAddress = 0;
    node->ParentPort = rootPort;
    node->Speed = speed;
    xhciTopoNotePosition(topo, node);
    return 1;
}

/*
 * **One node per (hub, port) is an assumption here, not an enforced
 * invariant** - written down after an audit went looking for the
 * check and did not find it, so the next reader is not sent on the same walk.
 * This function does not ask whether some other node already claims
 * (`at->HubAddress`, `at->HubPort`), and `xhciTopoPruneChildAt` removes only
 * the **first** node it finds at a position. So two nodes at one position, if
 * they could ever exist, would leave a stale one behind on the disconnect that
 * was supposed to clear it.
 *
 * **No sequence that produces two was constructed**, and the residue is bounded
 * either way: the table is XHCI_TOPO_NODES entries, an add that finds it full
 * refuses and is counted in `Dropped`, and a node reachable from nothing is
 * taken by `xhciTopoSweepOrphans`. That is why this is a note rather than a
 * fix - a prune loop or a displace-on-attach is cheap, but neither is worth
 * writing against a hazard nobody has shown is reachable, and a wrong one would
 * unhook a node that is still plugged in.
 *
 * IRQL: any.
 */
ULONG XhciTopoAttachChild(PXHCI_TOPOLOGY topo,
                          ULONG address,
                          const XHCI_TOPO_CHILD *at,
                          ULONG speed)
{
    XHCI_TOPO_NODE *node;
    const XHCI_TOPO_NODE *parent;

    if (topo == NULL || at == NULL || address == 0 || at->TooDeep != 0) {
        return 0;
    }
    if (at->HubAddress == address) {
        /* A hub cannot be plugged into itself, and an address the graph is
         * reusing for a device that replaced its own parent would otherwise
         * build a node whose route walks a cycle. */
        return 0;
    }

    parent = XhciTopoFind(topo, at->HubAddress);
    if (parent == NULL) {
        return 0;
    }

    node = xhciTopoNodeAdd(topo, address);
    if (node == NULL) {
        return 0;
    }

    node->Tier = at->Tier;
    node->Route = at->Route;
    node->RootPort = at->RootPort;
    node->Generation = parent->Generation;
    node->ParentAddress = at->HubAddress;
    node->ParentPort = at->HubPort;
    node->Speed = speed;
    xhciTopoNotePosition(topo, node);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Pruning                                                             */
/* ------------------------------------------------------------------ */

/*
 * Remove `address` and everything below it.
 *
 * The walk is repeated until a pass removes nothing rather than done in one
 * sweep, because the table is unordered: a grandchild may sit at a lower index
 * than its parent, and a single pass would leave it behind holding a
 * ParentAddress nobody answers to. Bounded by XHCI_TOPO_NODES passes, which is
 * the depth of the deepest possible chain in a table of that size.
 *
 * IRQL: any.
 */
/* Remove every node whose ParentAddress no longer answers to anything - a
 * route through a hub that is not in the graph names nothing. Split from the
 * prune because a migration orphans children the same way (their ParentAddress
 * keeps naming the old key). Called after whatever made parents disappear. */
static VOID xhciTopoSweepOrphans(PXHCI_TOPOLOGY topo)
{
    ULONG pass;
    ULONG i;
    XHCI_TOPO_NODE *node;

    for (pass = 0; pass < XHCI_TOPO_NODES; pass++) {
        ULONG removed;

        removed = 0;
        for (i = 0; i < XHCI_TOPO_NODES; i++) {
            node = &topo->Node[i];
            if ((node->Flags & XHCI_TOPO_F_USED) == 0) {
                continue;
            }
            if (node->ParentAddress == 0) {
                continue;
            }
            if (xhciTopoNodeFor(topo, node->ParentAddress) != NULL) {
                continue;
            }
            /* Its parent is gone, so its route is a path through a hub that no
             * longer exists. */
            xhciTopoNodeRemove(topo, node);
            removed++;
        }
        if (removed == 0) {
            break;
        }
    }
}

static VOID xhciTopoPruneFrom(PXHCI_TOPOLOGY topo, ULONG address)
{
    XHCI_TOPO_NODE *node;

    node = xhciTopoNodeFor(topo, address);
    if (node == NULL) {
        return;
    }
    xhciTopoNodeRemove(topo, node);
    xhciTopoSweepOrphans(topo);
}

/*
 * Remove whichever node hangs off (`hubAddress`, `hubPort`), and its subtree.
 *
 * The graph holds hubs only, so most disconnects find nothing here and that is
 * the ordinary case rather than a miss - a keyboard being unplugged from a hub
 * port has a device record and never had a node. What this exists for is the
 * hub-behind-a-hub: leaving its node would let a later device claim a parent
 * whose own parent has stopped reporting it.
 *
 * **It removes the FIRST match and returns**, which is correct exactly as far
 * as one-node-per-position holds - and that is an assumption rather than an
 * enforced invariant. See the note at `XhciTopoAttachChild`.
 *
 * IRQL: any.
 */
static VOID xhciTopoPruneChildAt(PXHCI_TOPOLOGY topo,
                                 ULONG hubAddress,
                                 ULONG hubPort)
{
    ULONG i;

    for (i = 0; i < XHCI_TOPO_NODES; i++) {
        XHCI_TOPO_NODE *node;

        node = &topo->Node[i];
        if ((node->Flags & XHCI_TOPO_F_USED) == 0) {
            continue;
        }
        if (node->ParentAddress != hubAddress || node->ParentPort != hubPort) {
            continue;
        }
        xhciTopoPruneFrom(topo, node->Address);
        return;
    }
}

/* IRQL: any. */
VOID XhciTopoDetach(PXHCI_TOPOLOGY topo, ULONG address)
{
    if (topo == NULL || address == 0) {
        return;
    }
    xhciTopoPruneFrom(topo, address);
}

/*
 * See xhci_topo.h for the two obligations. This replaced
 * `XhciTopoRootPortChanged`, which no driver path ever called (Phase 7 review,
 * finding A4): its by-generation discriminator did not survive contact with
 * the shadow's semantics - a port's generation advances on every claimed
 * operation, not per device tenancy, so a live hub's node stops matching the
 * current generation on the first re-enumeration reset. The keyed detach plus
 * this migration close the leak it was written for deterministically.
 *
 * IRQL: any.
 */
VOID XhciTopoMigrate(PXHCI_TOPOLOGY topo, ULONG oldAddress, ULONG newAddress)
{
    XHCI_TOPO_NODE *node;

    if (topo == NULL || newAddress == 0 || oldAddress == newAddress) {
        return;
    }

    /*
     * Whatever sits under the address being assigned is stale: usbport's
     * addresses are unique among live devices and the SET_ADDRESS interception
     * refuses an address another *record* holds, so a node here can only
     * describe a device that left without its detach running. Pruned with its
     * subtree before it can hand the new device - or any child claimed through
     * it - the departed hub's position (finding A5).
     */
    if (xhciTopoNodeFor(topo, newAddress) != NULL) {
        topo->MigrationsStale++;
        xhciTopoPruneFrom(topo, newAddress);
    }

    node = xhciTopoNodeFor(topo, oldAddress);
    if (node == NULL) {
        return;
    }
    node->Address = newAddress;
    topo->Migrations++;
    /* A claim naming the old key follows the hub it names. Defensive: a hub
     * mid-re-address should have no claim armed, but a stale pair handed out
     * under the old key would name a node that no longer answers to it. */
    if (topo->Pending && topo->PendingHub == oldAddress) {
        topo->PendingHub = newAddress;
    }
    /*
     * The migrated hub's children are not carried across: their ParentAddress
     * names the old key, and the re-enumeration that produces an address
     * change destroys and rebuilds everything below the hub anyway. The sweep
     * removes them exactly as it does below a pruned node.
     */
    xhciTopoSweepOrphans(topo);
}

/* ------------------------------------------------------------------ */
/* Claims                                                              */
/* ------------------------------------------------------------------ */

/* IRQL: any. */
ULONG XhciTopoClaimChild(PXHCI_TOPOLOGY topo, PXHCI_TOPO_CHILD out)
{
    ULONG hub;
    ULONG port;

    if (topo == NULL || out == NULL) {
        return 0;
    }
    if (!topo->Pending) {
        topo->ClaimsUnarmed++;
        return 0;
    }

    hub = topo->PendingHub;
    port = topo->PendingPort;

    /* Spent whatever happens below: a claim that survived its own failure would
     * be offered to the next open as well, which is the hijack shape task
     * 7b-A.0 bounded one tier up. */
    topo->Pending = 0;
    topo->PendingHub = 0;
    topo->PendingPort = 0;

    if (!XhciTopoChildOf(topo, hub, port, out)) {
        /*
         * Armed, spent, and unanswerable: the node was pruned between the
         * reset and this open, or its position was never learned. Its own
         * counter (Phase 7 review, B9) because `ClaimsUnarmed`'s declared
         * meaning is "an address-0 open with no pending parent" - the
         * ordinary root-port answer - and folding a consumed claim into it
         * made a channel failure indistinguishable from business as usual.
         */
        topo->ClaimsUnusable++;
        return 0;
    }

    topo->Claims++;
    if (out->TooDeep) {
        topo->ClaimsTooDeep++;
    }
    return 1;
}

/* IRQL: any. */
VOID XhciTopoDropPending(PXHCI_TOPOLOGY topo)
{
    if (topo == NULL || !topo->Pending) {
        return;
    }
    topo->Pending = 0;
    topo->PendingHub = 0;
    topo->PendingPort = 0;
    topo->PendingDropped++;
}

/* IRQL: any. See xhci_topo.h - the judgement that this arm was usbhub's second
 * reset of a bracket lives in the caller, which holds the device records. */
VOID XhciTopoSuppressClaim(PXHCI_TOPOLOGY topo)
{
    if (topo == NULL || !topo->Pending) {
        return;
    }
    topo->Pending = 0;
    topo->PendingHub = 0;
    topo->PendingPort = 0;
    topo->ResetsSuppressed++;
}

/* ------------------------------------------------------------------ */
/* Snooping                                                            */
/* ------------------------------------------------------------------ */

/*
 * A device that hub-class port traffic was addressed to is a hub.
 *
 * That is positive evidence and does not need the descriptor: only a hub has
 * ports, only `usbhub.sys` sends these requests, and usbport answers the *root*
 * hub's own class traffic through the RH_ callbacks so it never reaches here
 * (design doc 02 section 1's box, confirmed at runtime by batch 7b-V0's
 * hub-recipient split). The descriptor then supplies the numbers.
 *
 * IRQL: any.
 */
static XHCI_TOPO_NODE *xhciTopoPromote(PXHCI_TOPOLOGY topo, ULONG address)
{
    XHCI_TOPO_NODE *node;

    node = xhciTopoNodeAdd(topo, address);
    if (node == NULL) {
        return NULL;
    }

    if ((node->Flags & XHCI_TOPO_F_HUB) == 0) {
        node->Flags |= XHCI_TOPO_F_HUB;
        topo->Promotions++;
    }

    return node;
}

/* IRQL: any. */
static VOID xhciTopoObservePortRequest(PXHCI_TOPOLOGY topo,
                                       ULONG address,
                                       const XHCI_SETUP_PACKET *setup,
                                       PXHCI_TOPO_SNOOP snoop)
{
    XHCI_TOPO_NODE *node;
    ULONG selector;
    ULONG port;

    node = xhciTopoPromote(topo, address);
    port = (ULONG)setup->wIndex;
    selector = (ULONG)setup->wValue;

    /*
     * **Only the three requests whose `wIndex` is a port number** (audit finding
     * A7). The recipient bits alone do not say that: a hub-class request to
     * recipient Other also covers `CLEAR_TT_BUFFER` (bRequest 8), whose `wIndex`
     * encodes an endpoint number, device address, endpoint type and direction,
     * and `RESET_TT` / `GET_TT_STATE` / `STOP_TT` beside it. usbhub sends
     * `CLEAR_TT_BUFFER` to a real multi-TT hub whenever a full- or low-speed
     * transaction below it fails, and its encoded `wIndex` reads as a port
     * number in the thousands - which inflated this high-water mark until a hub
     * descriptor overwrote it.
     */
    if (setup->bRequest != XHCI_TOPO_REQ_SET_FEATURE &&
        setup->bRequest != XHCI_TOPO_REQ_CLEAR_FEATURE &&
        setup->bRequest != XHCI_TOPO_REQ_GET_STATUS) {
        return;
    }

    if (node != NULL && port != 0 && port > node->PortCount &&
        (node->Flags & XHCI_TOPO_F_DESCRIPTOR) == 0) {
        /*
         * The port-power sweep's high-water mark, which is a *lower bound* on
         * bNbrPorts and is kept only until a descriptor supplies the real one.
         * usbhub powers every port at hub start, so on the measured bus the two
         * agree - which is a reason to check the bound against the descriptor
         * on a target, not a reason to trust it.
         */
        node->PortCount = port;
    }

    if (setup->bRequest != XHCI_TOPO_REQ_SET_FEATURE) {
        return;
    }

    if (selector == XHCI_TOPO_SEL_PORT_POWER) {
        topo->PowerSweeps++;
        return;
    }
    if (selector != XHCI_TOPO_SEL_PORT_RESET) {
        return;
    }

    topo->Resets++;
    if (node == NULL || port == 0) {
        topo->ResetsUnknownHub++;
        return;
    }

    /*
     * Design doc 02 section 2 step 2: hub X resetting port Y means the next new
     * device enumerates at (X, Y). A second reset arriving before the first was
     * claimed is recorded rather than merged - "usbhub resets one port and
     * addresses one device at a time" is an assumption this graph would
     * otherwise be built on silently, and batch 7b-A.1.1 is what a mis-read
     * reset sequence costs.
     *
     * The newer reset wins. A stale claim is the one that cannot be right: the
     * device the older reset was for either enumerated already, in which case
     * the claim was spent, or it did not, in which case nothing will spend it.
     *
     * **OPEN, raised: this arms on the request, not on its success**,
     * and a SET_FEATURE(PORT_RESET) the hub stalls therefore arms a claim for a
     * port no device will appear on - and spends the root-port entitlement at
     * `xhciDevTopoSnoopSubmit` on the way past. It is *deliberately* not moved
     * to the completion the way the SET_INTERFACE beside it was, and the reason
     * is that the two flags are not the same kind of thing: MTT is a statement
     * about what the hub **is**, which a refusal falsifies outright, while this
     * is a prediction about what usbhub will do **next**, which the machinery
     * around it is already built to correct - the newer reset wins here, a root
     * port reset drops the claim and re-arms the entitlement outright
     * (`src/xhci_slot.c`, "it supersedes any pending hub claim"), and an open
     * that spends neither is counted as a device this driver cannot place.
     * Deferring it would also risk arming **late**: usbhub opens address 0
     * after the reset, and a claim that has not arrived by then breaks an
     * enumeration that works today, which is a worse failure than a stale claim
     * and is not observable on a host. What is left uncorrected is a
     * `ResetsOverwritten` that can count a claim which was never valid. Settling
     * it needs a guest with a hub that stalls a port reset; nobody has run one.
     */
    if (topo->Pending) {
        topo->ResetsOverwritten++;
    }
    topo->Pending = 1;
    topo->PendingHub = address;
    topo->PendingPort = port;
    if (snoop != NULL) {
        snoop->Armed = 1;
        /* The armed pair's port, so the caller can ask whether the position it
         * names is mid-enumeration (finding A6) without re-parsing the setup
         * packet. Reply stays NONE, so nothing ever folds against this. */
        snoop->Port = port;
    }
}

/* IRQL: any. */
VOID XhciTopoObserveSetup(PXHCI_TOPOLOGY topo,
                          ULONG address,
                          const XHCI_SETUP_PACKET *setup,
                          PXHCI_TOPO_SNOOP snoop)
{
    if (snoop != NULL) {
        snoop->Reply = XHCI_TOPO_REPLY_NONE;
        snoop->Address = address;
        snoop->Port = 0;
        snoop->Armed = 0;
    }
    if (topo == NULL || setup == NULL || address == 0) {
        return;
    }

    switch (setup->bmRequestType) {
    case XHCI_TOPO_RT_HUB_DESC:
        /*
         * **Matched on type and request only.** Both shipping hub drivers ask
         * with `wValue = 0x0000` rather than the `0x2900` the hub-class
         * specification prescribes (batch 7b-V0, measured twice per target),
         * so a graph keyed on the descriptor type in the *request* would find
         * no hub anywhere. The type is read from the reply.
         */
        if (setup->bRequest != XHCI_TOPO_REQ_GET_DESCRIPTOR) {
            return;
        }
        if (xhciTopoPromote(topo, address) == NULL) {
            return;
        }
        if (snoop != NULL) {
            snoop->Reply = XHCI_TOPO_REPLY_HUB_DESC;
        }
        return;

    case XHCI_TOPO_RT_PORT_OUT:
        /* SET_FEATURE and CLEAR_FEATURE on a downstream port. Only the first
         * carries topology; the second is still evidence of a hub, which is
         * why the promotion is above the request test rather than below it. */
        xhciTopoObservePortRequest(topo, address, setup, snoop);
        return;

    case XHCI_TOPO_RT_PORT_IN:
        if (setup->bRequest != XHCI_TOPO_REQ_GET_STATUS ||
            setup->wIndex == 0) {
            return;
        }
        if (xhciTopoPromote(topo, address) == NULL) {
            return;
        }
        if (snoop != NULL) {
            snoop->Reply = XHCI_TOPO_REPLY_PORT_STATUS;
            snoop->Port = (ULONG)setup->wIndex;
        }
        return;

    case XHCI_TOPO_RT_SET_INTERFACE:
        /*
         * **Seen here, applied nowhere here** - `XhciTopoApplySetInterface` is
         * the site, and it runs off the completion.
         *
         * The spec sentence this bit comes from is a statement about what the
         * hub *is*: xHCI 4.5.2, "MTT = '1' if the Multi-TT Interface of the hub
         * **has been enabled** with a Set Interface request". A request that
         * stalled enabled nothing, so applying it on placement would program a
         * Slot Context claiming a multi-TT hub the device refused to become -
         * and `XHCI_TOPO_F_ALT_SEEN` beside it would say the hub had answered
         * when it had not.
         *
         * That is the rule `xhciDevDescApply` already states for the *same
         * packet*, which `src/xhci_desc.c` reads for the alternate setting's
         * endpoint declarations: "a request the device refused did not change
         * what the device is running". Both readings of one SET_INTERFACE now
         * wait for the same success. *(Until a later review this case applied MTT
         * at placement; the descriptor half had been moved to the completion by
         * its own first review round and this half was not.)*
         */
        return;

    default:
        return;
    }
}

/*
 * Forget which alternate setting a device is running, because it has just been
 * reset and is therefore running alternate 0 of everything.
 *
 * **`XHCI_TOPO_F_ALT_SEEN` clears too, and that is the point of a separate
 * function** rather than `XhciTopoApplySetInterface(topo, address, 0)`: nothing
 * has been *asked* of the device that came back, so "single-TT" and "never
 * asked" have to read as never asked. Selecting alternate 0 explicitly is a
 * different fact and keeps its own flag.
 *
 * The rest of the node is deliberately left alone - it is a *position*, and a
 * re-enumeration on the same port is the same position. Hub-ness and the hub
 * descriptor's fields are re-established by the traffic that follows.
 *
 * IRQL: any.
 */
VOID XhciTopoForgetAlternate(PXHCI_TOPOLOGY topo, ULONG address)
{
    XHCI_TOPO_NODE *node;

    if (topo == NULL || address == 0) {
        return;
    }
    node = xhciTopoNodeFor(topo, address);
    if (node == NULL) {
        return;
    }
    if ((node->Flags & (XHCI_TOPO_F_MTT | XHCI_TOPO_F_ALT_SEEN)) != 0) {
        topo->AltForgotten++;
    }
    node->Flags &= ~(XHCI_TOPO_F_MTT | XHCI_TOPO_F_ALT_SEEN);
}

/*
 * Apply a SET_INTERFACE that **completed successfully**, which is the only kind
 * that says anything about the hub.
 *
 * Multi-TT, and **only on a device already known to be a hub**: every device
 * with an alternate setting sends this request, and it means MTT for a hub,
 * whose alternate setting 1 *is* the multi-TT interface. A device that is not
 * in the graph, or is in it but was never seen taking hub-class port traffic,
 * is left alone - this promotes nothing.
 *
 * IRQL: any.
 */
VOID XhciTopoApplySetInterface(PXHCI_TOPOLOGY topo,
                               ULONG address,
                               ULONG alternate)
{
    XHCI_TOPO_NODE *node;

    if (topo == NULL || address == 0) {
        return;
    }
    node = xhciTopoNodeFor(topo, address);
    if (node == NULL || (node->Flags & XHCI_TOPO_F_HUB) == 0) {
        return;
    }

    topo->AltSettings++;
    node->Flags |= XHCI_TOPO_F_ALT_SEEN;
    if (alternate != 0) {
        node->Flags |= XHCI_TOPO_F_MTT;
    } else {
        node->Flags &= ~XHCI_TOPO_F_MTT;
    }
}

/* ------------------------------------------------------------------ */
/* Replies                                                             */
/* ------------------------------------------------------------------ */

/* IRQL: any. */
static ULONG xhciTopoFoldHubDescriptor(PXHCI_TOPOLOGY topo,
                                       XHCI_TOPO_NODE *node,
                                       const UCHAR *data,
                                       ULONG length)
{
    ULONG declared;

    topo->Descriptors++;

    /*
     * **Self-consistency, not the type byte.** The hub descriptor's type value
     * is not in the era-appropriate DDK header this driver builds against and
     * no run has read it back, so gating on it would let one wrong constant
     * refuse every hub on both targets silently. What is checked is what can be
     * checked from the bytes themselves: a declared length that covers the
     * fields being read and does not claim more than arrived. The type is
     * recorded, and the match reported, so the run measures the constant.
     */
    if (length < XHCI_TOPO_HUBD_MIN) {
        topo->DescriptorsBad++;
        return 0;
    }
    declared = (ULONG)data[XHCI_TOPO_HUBD_LENGTH];
    if (declared < XHCI_TOPO_HUBD_MIN || declared > length) {
        topo->DescriptorsBad++;
        return 0;
    }

    node->DescriptorType = (ULONG)data[XHCI_TOPO_HUBD_TYPE];
    if (node->DescriptorType == XHCI_TOPO_DESC_TYPE_HUB) {
        node->Flags |= XHCI_TOPO_F_DESC_TYPE_OK;
    } else {
        node->Flags &= ~XHCI_TOPO_F_DESC_TYPE_OK;
    }

    node->PortCount = (ULONG)data[XHCI_TOPO_HUBD_PORTS];
    node->Characteristics = (ULONG)data[XHCI_TOPO_HUBD_CHAR_LO] |
                            ((ULONG)data[XHCI_TOPO_HUBD_CHAR_HI] << 8);
    node->Flags |= XHCI_TOPO_F_DESCRIPTOR;
    if (node->PortCount == 0) {
        /*
         * Self-consistent and unusable: `XhciTopoHubMark` will refuse to mark a
         * hub with no ports, so without a counter here the slot would silently
         * stay unmarked with `Descriptors` reporting a clean fold. Recorded, not
         * refused - the descriptor's other fields are still what arrived.
         */
        topo->DescriptorsNoPorts++;
    }
    return 1;
}

/* IRQL: any. */
static ULONG xhciTopoFoldPortStatus(PXHCI_TOPOLOGY topo,
                                    XHCI_TOPO_NODE *node,
                                    ULONG port,
                                    const UCHAR *data,
                                    ULONG length,
                                    PXHCI_TOPO_GONE gone)
{
    ULONG status;
    ULONG change;
    ULONG mask;
    ULONG connected;
    ULONG hubAddress;

    topo->PortStatuses++;

    if (length < XHCI_TOPO_PORTSTAT_MIN || port == 0) {
        return 0;
    }
    if (port > XHCI_TOPO_CONNECT_PORTS) {
        /*
         * Counted rather than clamped: folding port 33 onto bit 0 would report
         * a disconnect on port 1 that never happened, and a hub this wide is a
         * reading worth having on its own.
         */
        topo->PortStatusesWide++;
        return 0;
    }

    status = (ULONG)data[0] | ((ULONG)data[1] << 8);
    /* `wPortChange`, when the reply carried it - bytes 2-3 are inside the
     * 8-byte copy window, but a short reply may end before them, in which case
     * the change reading is simply lost rather than invented. */
    change = (length >= 4) ? ((ULONG)data[2] | ((ULONG)data[3] << 8)) : 0UL;
    connected = (status & XHCI_HUB_PORT_CONNECTION) ? 1UL : 0UL;
    mask = 1UL << (port - 1);

    if (connected) {
        /*
         * **Connected-to-connected with the connect-change bit set is a
         * departure too** (Phase 7 review, B10). A disconnect and reconnect
         * between usbhub polls never shows this graph an empty port; the
         * change bit is the only evidence a device left, and without acting
         * on it the old child record would be re-entered for a different
         * physical device - old slot, old endpoints, wrong device. The root
         * tier tears down on the change bit for exactly this reason. The
         * connect mask stays set: the port is occupied, by whatever comes
         * next. A repeated fold before usbhub clears the bit reports the
         * departure again, which is benign - the record and node are already
         * gone, so the second teardown finds nothing - and counted evidence
         * beats a silent reuse.
         */
        if ((node->Connected & mask) != 0 &&
            (change & XHCI_HUB_C_PORT_CONNECTION) != 0) {
            node->Disconnects++;
            topo->Disconnects++;
            topo->Reconnects++;
            hubAddress = node->Address;
            if (gone != NULL) {
                gone->Disconnected = 1;
                gone->HubAddress = hubAddress;
                gone->HubPort = port;
            }
            xhciTopoPruneChildAt(topo, hubAddress, port);
            return 1;
        }
        node->Connected |= mask;
        return 1;
    }

    if ((node->Connected & mask) == 0) {
        /* Already known empty - the ordinary answer for every unpopulated port
         * of every hub, and not a disconnect. */
        return 0;
    }

    node->Connected &= ~mask;
    node->Disconnects++;
    topo->Disconnects++;

    /*
     * **The one event that says a device behind a hub has gone**, and the
     * caller has to be told rather than left to notice. A root-port device
     * leaves through the root-hub callbacks - a connect change, a disable, a
     * disown - and none of those exists one tier down: usbhub owns that port and
     * this driver only ever sees the traffic. A record left standing here holds
     * a Slot ID nothing will give back and a usbport address that will refuse
     * the next device given it, which is the defect task 6-B.5's port-disable
     * teardown was written for, one tier lower.
     *
     * The node below - if the departing device was itself a hub - goes here,
     * because that is bookkeeping this file owns; the device *record* is the
     * caller's, which is why the pair is reported rather than acted on. The
     * address is read before the prune: `xhciTopoPruneFrom` clears the node it
     * removes, and `node` is the *parent* here so it survives, but the child
     * search must run against the graph as it was.
     */
    hubAddress = node->Address;
    if (gone != NULL) {
        gone->Disconnected = 1;
        gone->HubAddress = hubAddress;
        gone->HubPort = port;
    }
    xhciTopoPruneChildAt(topo, hubAddress, port);
    return 1;
}

/* IRQL: any. */
ULONG XhciTopoObserveReply(PXHCI_TOPOLOGY topo,
                           const XHCI_TOPO_SNOOP *snoop,
                           const UCHAR *data,
                           ULONG length,
                           PXHCI_TOPO_GONE gone)
{
    XHCI_TOPO_NODE *node;

    if (gone != NULL) {
        gone->Disconnected = 0;
        gone->HubAddress = 0;
        gone->HubPort = 0;
    }
    if (topo == NULL || snoop == NULL || data == NULL) {
        return 0;
    }
    if (snoop->Reply == XHCI_TOPO_REPLY_NONE) {
        return 0;
    }

    /*
     * No node, no fold. The snoop that armed this created one, so a miss means
     * the device was pruned between the request and its reply - which is
     * exactly when inventing a node would put a device in the tree with no
     * position and no parent.
     */
    node = xhciTopoNodeFor(topo, snoop->Address);
    if (node == NULL) {
        return 0;
    }

    if (snoop->Reply == XHCI_TOPO_REPLY_HUB_DESC) {
        return xhciTopoFoldHubDescriptor(topo, node, data, length);
    }
    if (snoop->Reply == XHCI_TOPO_REPLY_PORT_STATUS) {
        return xhciTopoFoldPortStatus(topo, node, snoop->Port, data, length,
                                      gone);
    }
    return 0;
}
