/*
 * tbt_brnum.c - bus numbers for the bridges a tunnel brought up below a
 * tunnel port (tbt_brnum.h; design record 21 section 6.1.1).
 *
 * IRQL: the accessors' and the go callback's. No lock, no DDK call;
 * host-tested by test\test_tbt_brnum.c.
 */

#include "tbt_brnum.h"

#define BRNUM_KEEP_MASK 0xFF000000UL    /* the secondary latency timer */
#define BRNUM_BUS_MASK  0x00FFFFFFUL
#define BRNUM_CRS_ID    0x0001UL        /* a vendor id read while the
                                           function answers Configuration
                                           Request Retry Status */

typedef struct _BRNUM_CTX {
    PXHCI_TBT_CFG Io;
    PXHCI_TBT_BRNUM R;
    XHCI_TBT_BRNUM_GO Go;
    PVOID GoContext;
    ULONG Next;             /* the next bus to give                      */
    ULONG Limit;            /* the port's subordinate bus                */
    ULONG Count;            /* bridges journalled in R                   */
} BRNUM_CTX;

static ULONG brRead(PXHCI_TBT_CFG io, ULONG bdf, ULONG offset, PULONG value)
{
    io->Reads++;
    return io->Read(io->Context, bdf, offset, value);
}

static ULONG brGo(BRNUM_CTX *c)
{
    return c->Go == NULL ? 1UL : c->Go(c->GoContext);
}

/*
 * The function at `bdf`: 0 for none, 1 for one (its id in *id, its header
 * layout with 7Fh masked in *layout, whether it is multi-function in
 * *multi), 2 for a failed read, 3 for one not ready (Configuration Request
 * Retry Status).
 */
static ULONG brProbe(PXHCI_TBT_CFG io, ULONG bdf, PULONG id, PULONG layout,
                     PULONG multi)
{
    ULONG hdr;

    if (!brRead(io, bdf, XHCI_TBT_CFG_ID, id)) {
        return 2;
    }
    if ((*id & 0xFFFFUL) == BRNUM_CRS_ID) {
        return 3;
    }
    if ((*id & 0xFFFFUL) == 0xFFFFUL || (*id & 0xFFFFUL) == 0) {
        return 0;
    }
    if (!brRead(io, bdf, XHCI_TBT_CFG_HEADER, &hdr) ||
        hdr == 0xFFFFFFFFUL) {
        return 2;
    }
    *layout = XHCI_TBT_HDR_LAYOUT((hdr >> 16) & 0xFFUL);
    *multi = ((hdr >> 16) & XHCI_TBT_HDR_MF) != 0 ? 1UL : 0UL;
    return 1;
}

/* The bridge at `bdf` is still the one journalled (identity, a type 1
 * header): 1 when it is. */
static ULONG brSame(PXHCI_TBT_CFG io, ULONG bdf, ULONG id)
{
    ULONG now;
    ULONG layout;
    ULONG multi;

    layout = 0;
    multi = 0;
    if (brProbe(io, bdf, &now, &layout, &multi) != 1) {
        return 0;
    }
    return (now == id && layout == XHCI_TBT_HDR_BRIDGE) ? 1UL : 0UL;
}

/* Journalled slot k's register written with `value`, the bridge checked
 * first and the value read back after: 1 when it holds. */
static ULONG brWrite(BRNUM_CTX *c, ULONG k, ULONG value)
{
    PXHCI_TBT_BRNUM r;
    ULONG back;

    r = c->R;
    if (!brSame(c->Io, r->Bdf[k], r->Id[k])) {
        return 0;
    }
    /* Journalled before it is issued: a write that lands but does not
     * read back is still undone. */
    r->Now[k] = value;
    c->Io->Writes++;
    r->Writes++;
    if (!c->Io->Write(c->Io->Context, r->Bdf[k], XHCI_TBT_CFG_BUSES, 4,
                      value)) {
        return 0;
    }
    if (!brRead(c->Io, r->Bdf[k], XHCI_TBT_CFG_BUSES, &back)) {
        return 0;
    }
    return ((back & BRNUM_BUS_MASK) == (value & BRNUM_BUS_MASK)) ? 1UL
                                                                  : 0UL;
}

/*
 * The survey of the port's secondary bus, before any write: every bridge
 * on it must read secondary and subordinate 0. Returns NONE, NUMBERED,
 * ACCESS, NOT_READY or DONE (meaning "go on"), with r->Where at a
 * refusal.
 */
static ULONG brSurvey(PXHCI_TBT_CFG io, ULONG bus, PXHCI_TBT_BRNUM r)
{
    ULONG dev;
    ULONG fn;
    ULONG bdf;
    ULONG id;
    ULONG layout;
    ULONG multi;
    ULONG first;
    ULONG probe;
    ULONG buses;
    ULONG found;

    found = 0;
    for (dev = 0; dev < 32UL; dev++) {
        first = 0;
        for (fn = 0; fn < 8UL; fn++) {
            bdf = XHCI_TBT_BDF(bus, dev, fn);
            layout = 0;
            multi = 0;
            probe = brProbe(io, bdf, &id, &layout, &multi);
            if (probe == 2 || probe == 3) {
                r->Where = bdf;
                return probe == 2 ? XHCI_TBT_BRNUM_ACCESS
                                  : XHCI_TBT_BRNUM_NOT_READY;
            }
            if (probe == 0) {
                if (fn == 0) {
                    break;
                }
                continue;
            }
            r->Functions++;
            if (fn == 0) {
                first = multi;
            }
            if (layout == XHCI_TBT_HDR_BRIDGE) {
                found++;
                if (!brRead(io, bdf, XHCI_TBT_CFG_BUSES, &buses) ||
                    buses == 0xFFFFFFFFUL) {
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_ACCESS;
                }
                if ((buses & 0x00FFFF00UL) != 0) {
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_NUMBERED;
                }
            }
            if (fn == 0 && !first) {
                break;
            }
        }
    }
    return found != 0 ? XHCI_TBT_BRNUM_DONE : XHCI_TBT_BRNUM_NONE;
}

/*
 * Number every bridge on `bus` (at `depth` below the port) and, depth
 * first, the subtree behind each. 0 when the bus is done, else the
 * refusal's outcome with r->Where set.
 */
static ULONG brBus(BRNUM_CTX *c, ULONG bus, ULONG depth)
{
    PXHCI_TBT_BRNUM r;
    ULONG dev;
    ULONG fn;
    ULONG bdf;
    ULONG id;
    ULONG layout;
    ULONG multi;
    ULONG first;
    ULONG probe;
    ULONG buses;
    ULONG secondary;
    ULONG slot;
    ULONG rc;

    r = c->R;
    for (dev = 0; dev < 32UL; dev++) {
        first = 0;
        for (fn = 0; fn < 8UL; fn++) {
            bdf = XHCI_TBT_BDF(bus, dev, fn);
            layout = 0;
            multi = 0;
            probe = brProbe(c->Io, bdf, &id, &layout, &multi);
            if (probe == 2 || probe == 3) {
                r->Where = bdf;
                return probe == 2 ? XHCI_TBT_BRNUM_ACCESS
                                  : XHCI_TBT_BRNUM_NOT_READY;
            }
            if (probe == 0) {
                if (fn == 0) {
                    break;
                }
                continue;
            }
            if (fn == 0) {
                first = multi;
            }
            if (layout == XHCI_TBT_HDR_BRIDGE) {
                r->Bridges++;
                if (depth >= XHCI_TBT_BRNUM_MAX_DEPTH) {
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_TOO_DEEP;
                }
                if (c->Count >= XHCI_TBT_BRNUM_MAX_BRIDGES) {
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_TOO_MANY;
                }
                if (!brRead(c->Io, bdf, XHCI_TBT_CFG_BUSES, &buses) ||
                    buses == 0xFFFFFFFFUL) {
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_ACCESS;
                }
                if ((buses & 0x00FFFF00UL) != 0) {
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_INCONSISTENT;
                }
                if (c->Next > c->Limit) {
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_NO_ROOM;
                }
                if (!brGo(c)) {
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_STALE;
                }
                secondary = c->Next;
                c->Next++;
                slot = c->Count;
                r->Bdf[slot] = bdf;
                r->Id[slot] = id;
                r->Was[slot] = buses;
                r->Now[slot] = buses;
                c->Count++;
                /* The subordinate held at the port's limit while the
                 * subtree is walked, so configuration cycles reach every
                 * bus that may be given below it; narrowed after. */
                if (!brWrite(c, slot, (buses & BRNUM_KEEP_MASK) |
                                          (c->Limit << 16) |
                                          (secondary << 8) | bus)) {
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_ACCESS;
                }
                rc = brBus(c, secondary, depth + 1);
                if (rc != 0) {
                    return rc;
                }
                if (!brGo(c)) {
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_STALE;
                }
                if (!brWrite(c, slot, (buses & BRNUM_KEEP_MASK) |
                                          ((c->Next - 1) << 16) |
                                          (secondary << 8) | bus)) {
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_ACCESS;
                }
            }
            if (fn == 0 && !first) {
                break;
            }
        }
    }
    return 0;
}

/* Every journalled bridge written back as read, deepest first: a parent
 * written back first would cut the configuration cycles to its children
 * off. Journalled parent before child, so the reverse order is right. A
 * bridge that no longer reads as the one journalled is not written. */
static VOID brUndo(BRNUM_CTX *c)
{
    PXHCI_TBT_BRNUM r;
    ULONG k;
    ULONG back;

    r = c->R;
    for (k = c->Count; k > 0; k--) {
        if (r->Now[k - 1] == r->Was[k - 1]) {
            continue;
        }
        if (!brSame(c->Io, r->Bdf[k - 1], r->Id[k - 1])) {
            r->UndoFailed++;
            continue;
        }
        r->Writes++;
        c->Io->Writes++;
        if (!c->Io->Write(c->Io->Context, r->Bdf[k - 1], XHCI_TBT_CFG_BUSES,
                          4, r->Was[k - 1]) ||
            !brRead(c->Io, r->Bdf[k - 1], XHCI_TBT_CFG_BUSES, &back) ||
            (back & BRNUM_BUS_MASK) != (r->Was[k - 1] & BRNUM_BUS_MASK)) {
            r->UndoFailed++;
            continue;
        }
        r->Now[k - 1] = r->Was[k - 1];
        r->Undone++;
    }
}

ULONG XhciTbtNumberBridges(PXHCI_TBT_CFG io, ULONG port,
                           XHCI_TBT_BRNUM_GO go, PVOID goContext,
                           PXHCI_TBT_BRNUM r)
{
    BRNUM_CTX c;
    ULONG buses;
    ULONG own;
    ULONG secondary;
    ULONG subordinate;
    ULONG rc;
    ULONG k;

    r->Outcome = XHCI_TBT_BRNUM_NONE;
    r->PortBuses = 0;
    r->Functions = 0;
    r->Bridges = 0;
    r->Numbered = 0;
    r->Last = 0;
    r->Where = XHCI_TBT_NO_BDF;
    r->Writes = 0;
    r->Undone = 0;
    r->UndoFailed = 0;
    for (k = 0; k < XHCI_TBT_BRNUM_MAX_BRIDGES; k++) {
        r->Bdf[k] = XHCI_TBT_NO_BDF;
        r->Id[k] = 0;
        r->Was[k] = 0;
        r->Now[k] = 0;
    }
    if (io->Write == NULL) {
        r->Outcome = XHCI_TBT_BRNUM_NO_WRITER;
        return r->Outcome;
    }
    own = XHCI_TBT_BDF_BUS(port);
    if (!brRead(io, port, XHCI_TBT_CFG_BUSES, &buses) ||
        buses == 0xFFFFFFFFUL) {
        r->Where = port;
        r->Outcome = XHCI_TBT_BRNUM_PORT;
        return r->Outcome;
    }
    r->PortBuses = buses;
    secondary = (buses >> 8) & 0xFFUL;
    subordinate = (buses >> 16) & 0xFFUL;
    if (secondary <= own || subordinate < secondary) {
        r->Where = port;
        r->Outcome = XHCI_TBT_BRNUM_PORT;
        return r->Outcome;
    }
    rc = brSurvey(io, secondary, r);
    if (rc != XHCI_TBT_BRNUM_DONE) {
        r->Outcome = rc;
        return r->Outcome;
    }
    c.Io = io;
    c.R = r;
    c.Go = go;
    c.GoContext = goContext;
    c.Next = secondary + 1;
    c.Limit = subordinate;
    c.Count = 0;
    rc = brBus(&c, secondary, 0);
    if (rc == 0) {
        r->Numbered = c.Count;
        r->Last = c.Count != 0 ? c.Next - 1 : 0;
        r->Outcome = XHCI_TBT_BRNUM_DONE;
        return r->Outcome;
    }
    /* Undone whatever the refusal, a stale operation's too: each write
     * back is made only to a bridge still reading as the one journalled,
     * and puts back the value read from it, which for these bridges is
     * the reset state a replacement of the same identity comes up in. */
    brUndo(&c);
    r->Outcome = rc;
    return r->Outcome;
}
