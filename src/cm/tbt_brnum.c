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
    ULONG Flags;            /* XHCI_TBT_BRNUM_F_*                        */
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
static ULONG brBus(BRNUM_CTX *c, ULONG bus, ULONG depth, PULONG found)
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
    ULONG below;

    r = c->R;
    *found = 0;
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
            (*found)++;
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
                rc = brBus(c, secondary, depth + 1, &below);
                if (rc != 0) {
                    return rc;
                }
                if (below == 0 &&
                    (c->Flags & XHCI_TBT_BRNUM_F_WAIT_EMPTY) != 0) {
                    /* Nothing behind a bridge just numbered: a link below
                     * it may still be training. Undone and tried again,
                     * so its subordinate is not narrowed past a device
                     * that answers a moment later. */
                    r->Where = bdf;
                    return XHCI_TBT_BRNUM_NOT_READY;
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

/* ---- the memory windows (XHCI_TBT_BRNUM_F_WINDOWS) ---------------------- */

#define BRWIN_GRAIN     0x00100000UL    /* a bridge window's granularity   */
#define BRWIN_MAX_BAR   0x40000000UL    /* the largest BAR given a window  */
#define BRWIN_CMD_MEM   0x0002UL        /* Command: Memory Space Enable    */
#define BRWIN_CMD_BM    0x0004UL        /* Command: Bus Master Enable      */
#define BRWIN_IO_CLOSED 0x000000F0UL    /* I/O base F0h, limit 00h; the
                                           Secondary Status half 0 (RW1C) */
#define BRWIN_PF_CLOSED 0x0000FFF0UL    /* prefetchable base FFF0h, limit 0 */
#define BRWIN_MEM_MASK  0xFFF0FFF0UL    /* a window register's address bits */

/* The journal slot of the bridge at `bdf`, or XHCI_TBT_BRNUM_MAX_BRIDGES. */
static ULONG brSlot(BRNUM_CTX *c, ULONG bdf)
{
    ULONG k;

    for (k = 0; k < c->Count; k++) {
        if (c->R->Bdf[k] == bdf) {
            return k;
        }
    }
    return XHCI_TBT_BRNUM_MAX_BRIDGES;
}

/* *sum += add, refused (0) where it would pass 4 GiB. */
static ULONG brAdd(PULONG sum, ULONG add)
{
    if (*sum + add < *sum) {
        return 0;
    }
    *sum += add;
    return 1;
}

/* `v` rounded up to a multiple of the power of two `a`; 0 where it wraps. */
static ULONG brRound(ULONG v, ULONG a)
{
    if (v + (a - 1UL) < v) {
        return 0;
    }
    return (v + (a - 1UL)) & ~(a - 1UL);
}

/*
 * One memory BAR at `off` of the function at `bdf` sized: all-ones
 * written, read back, and the value read first written back and read back
 * - the last two on every path once the first write is made. *size is 0
 * for an unimplemented BAR. 0, or a refusal.
 */
static ULONG brBarSize(BRNUM_CTX *c, ULONG bdf, ULONG off, ULONG was,
                       PULONG size)
{
    ULONG mask;
    ULONG back;
    ULONG got;
    ULONG rc;

    *size = 0;
    c->Io->Writes++;
    c->R->Writes++;
    got = c->Io->Write(c->Io->Context, bdf, off, 4, 0xFFFFFFFFUL) &&
          brRead(c->Io, bdf, off, &mask);
    c->Io->Writes++;
    c->R->Writes++;
    if (!c->Io->Write(c->Io->Context, bdf, off, 4, was) ||
        !brRead(c->Io, bdf, off, &back) || back != was) {
        c->R->UndoFailed++;
        c->R->WinWhere = bdf;
        return XHCI_TBT_BRWIN_ACCESS;
    }
    if (!got) {
        c->R->WinWhere = bdf;
        return XHCI_TBT_BRWIN_ACCESS;
    }
    rc = 0;
    mask &= 0xFFFFFFF0UL;
    if (mask != 0) {
        *size = (~mask) + 1UL;
        if (*size > BRWIN_MAX_BAR) {
            c->R->WinWhere = bdf;
            rc = XHCI_TBT_BRWIN_NO_ROOM;
        }
    } else if (((was >> 1) & 3UL) == 2UL) {
        /* A 64-bit BAR whose low half sizes nothing: 4 GiB or more, or
         * unimplemented; either way not given a window. */
        c->R->WinWhere = bdf;
        rc = XHCI_TBT_BRWIN_NO_ROOM;
    }
    return rc;
}

/*
 * The memory BARs at offsets 10h up to `last` of the function at `bdf`,
 * added to *need with the largest in *align. I/O BARs are not counted (no
 * I/O window is opened). 0, or a refusal.
 */
static ULONG brBars(BRNUM_CTX *c, ULONG bdf, ULONG last, PULONG need,
                    PULONG align)
{
    ULONG off;
    ULONG was;
    ULONG size;
    ULONG cmd;
    ULONG rc;

    if (!brRead(c->Io, bdf, XHCI_TBT_CFG_COMMAND, &cmd)) {
        c->R->WinWhere = bdf;
        return XHCI_TBT_BRWIN_ACCESS;
    }
    if ((cmd & BRWIN_CMD_MEM) != 0) {
        /* Decoding already: someone configured it; sizing would move it. */
        c->R->WinWhere = bdf;
        return XHCI_TBT_BRWIN_ACTIVE;
    }
    for (off = 0x10UL; off <= last; off += 4) {
        if (!brGo(c)) {
            c->R->WinWhere = bdf;
            return XHCI_TBT_BRWIN_STALE;
        }
        if (!brRead(c->Io, bdf, off, &was)) {
            c->R->WinWhere = bdf;
            return XHCI_TBT_BRWIN_ACCESS;
        }
        if ((was & 1UL) != 0) {
            continue;
        }
        rc = brBarSize(c, bdf, off, was, &size);
        if (rc != 0) {
            return rc;
        }
        if (size != 0) {
            if (!brAdd(need, size)) {
                c->R->WinWhere = bdf;
                return XHCI_TBT_BRWIN_NO_ROOM;
            }
            if (size > *align) {
                *align = size;
            }
        }
        if (((was >> 1) & 3UL) == 2UL) {
            /* A 64-bit BAR: its upper half is the next dword, left 0. */
            off += 4;
        }
    }
    return 0;
}

/*
 * The memory the functions of `bus` need, in *need, with the largest
 * alignment any of it takes in *align: their BARs, and for each
 * journalled bridge the window its subtree needs (Need[] and Align[]: the
 * subtree rounded up to its alignment, at least 1 MiB, with one more
 * alignment unit where that is above 1 MiB, so the items fit whatever
 * order they are placed in). 0, or a refusal.
 */
static ULONG brNeed(BRNUM_CTX *c, ULONG bus, ULONG depth, PULONG need,
                    PULONG align)
{
    ULONG dev;
    ULONG fn;
    ULONG bdf;
    ULONG id;
    ULONG layout;
    ULONG multi;
    ULONG first;
    ULONG probe;
    ULONG k;
    ULONG sub;
    ULONG subAlign;
    ULONG size;
    ULONG rc;

    *need = 0;
    *align = 0;
    if (depth > XHCI_TBT_BRNUM_MAX_DEPTH) {
        return XHCI_TBT_BRWIN_ACCESS;
    }
    for (dev = 0; dev < 32UL; dev++) {
        first = 0;
        for (fn = 0; fn < 8UL; fn++) {
            bdf = XHCI_TBT_BDF(bus, dev, fn);
            layout = 0;
            multi = 0;
            probe = brProbe(c->Io, bdf, &id, &layout, &multi);
            if (probe == 2 || probe == 3) {
                c->R->WinWhere = bdf;
                return XHCI_TBT_BRWIN_ACCESS;
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
            if (layout == 0) {
                rc = brBars(c, bdf, 0x24UL, need, align);
                if (rc != 0) {
                    return rc;
                }
            } else if (layout == XHCI_TBT_HDR_BRIDGE) {
                k = brSlot(c, bdf);
                if (k == XHCI_TBT_BRNUM_MAX_BRIDGES) {
                    /* Not numbered by this call: not ours to window. */
                    c->R->WinWhere = bdf;
                    return XHCI_TBT_BRWIN_ACCESS;
                }
                rc = brBars(c, bdf, 0x14UL, need, align);
                if (rc != 0) {
                    return rc;
                }
                rc = brNeed(c, (c->R->Now[k] >> 8) & 0xFFUL, depth + 1, &sub,
                            &subAlign);
                if (rc != 0) {
                    return rc;
                }
                if (sub == 0) {
                    size = 0;
                    subAlign = 0;
                } else {
                    if (subAlign < BRWIN_GRAIN) {
                        subAlign = BRWIN_GRAIN;
                    }
                    size = brRound(sub, subAlign);
                    if (size == 0 ||
                        (subAlign > BRWIN_GRAIN && !brAdd(&size, subAlign))) {
                        c->R->WinWhere = bdf;
                        return XHCI_TBT_BRWIN_NO_ROOM;
                    }
                }
                c->R->Need[k] = size;
                c->R->Align[k] = subAlign;
                if (!brAdd(need, size)) {
                    c->R->WinWhere = bdf;
                    return XHCI_TBT_BRWIN_NO_ROOM;
                }
                if (subAlign > *align) {
                    *align = subAlign;
                }
            }
            if (fn == 0 && !first) {
                break;
            }
        }
    }
    return 0;
}

/* One register of journalled bridge k written; read back and compared
 * under `compare` (0: not compared, for a window a bridge may not
 * implement). */
static ULONG brWinWrite(BRNUM_CTX *c, ULONG k, ULONG offset, ULONG value,
                        ULONG compare)
{
    ULONG back;

    if (!brSame(c->Io, c->R->Bdf[k], c->R->Id[k])) {
        return 0;
    }
    c->Io->Writes++;
    c->R->Writes++;
    if (!c->Io->Write(c->Io->Context, c->R->Bdf[k], offset, 4, value)) {
        return 0;
    }
    if (compare == 0) {
        return 1;
    }
    if (!brRead(c->Io, c->R->Bdf[k], offset, &back)) {
        return 0;
    }
    return ((back & compare) == (value & compare)) ? 1UL : 0UL;
}

static ULONG brPlace(BRNUM_CTX *c, ULONG bus, ULONG top, ULONG floor,
                     ULONG depth);

/*
 * The journalled bridges of `bus` whose alignment is `only` (0: those with
 * nothing to decode, closed) given their windows, from *top (one past the
 * highest address) downward but never below `floor`, each aligned to its
 * Align[], and each subtree inside its bridge's window from that window's
 * top. *top is left below the last window placed. 0, or a refusal.
 */
static ULONG brPlaceAt(BRNUM_CTX *c, ULONG bus, PULONG ptop, ULONG floor,
                       ULONG depth, ULONG only)
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
    ULONG k;
    ULONG base;
    ULONG mem;
    ULONG rc;
    ULONG top;

    r = c->R;
    top = *ptop;
    if (depth > XHCI_TBT_BRNUM_MAX_DEPTH) {
        return XHCI_TBT_BRWIN_ACCESS;
    }
    for (dev = 0; dev < 32UL; dev++) {
        first = 0;
        for (fn = 0; fn < 8UL; fn++) {
            bdf = XHCI_TBT_BDF(bus, dev, fn);
            layout = 0;
            multi = 0;
            probe = brProbe(c->Io, bdf, &id, &layout, &multi);
            if (probe == 2 || probe == 3) {
                r->WinWhere = bdf;
                return XHCI_TBT_BRWIN_ACCESS;
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
                k = brSlot(c, bdf);
                if (k == XHCI_TBT_BRNUM_MAX_BRIDGES) {
                    r->WinWhere = bdf;
                    return XHCI_TBT_BRWIN_ACCESS;
                }
                if ((r->Need[k] == 0 ? 0UL : r->Align[k]) != only) {
                    if (fn == 0 && !first) {
                        break;
                    }
                    continue;
                }
                if (!brGo(c)) {
                    r->WinWhere = bdf;
                    return XHCI_TBT_BRWIN_STALE;
                }
                if (r->Need[k] == 0) {
                    /* Nothing behind it decodes memory: closed. */
                    base = 0;
                    mem = 0x0000FFF0UL;
                } else {
                    if (top - floor < r->Need[k]) {
                        r->WinWhere = bdf;
                        return XHCI_TBT_BRWIN_NO_ROOM;
                    }
                    base = (top - r->Need[k]) & ~(r->Align[k] - 1UL);
                    if (base < floor) {
                        r->WinWhere = bdf;
                        return XHCI_TBT_BRWIN_NO_ROOM;
                    }
                    mem = ((((base + r->Need[k] - 1UL) >> 16) & 0xFFF0UL)
                           << 16) |
                          ((base >> 16) & 0xFFF0UL);
                    top = base;
                    *ptop = top;
                }
                r->WinDone[k] = 1;
                if (!brWinWrite(c, k, XHCI_TBT_CFG_IOWIN, BRWIN_IO_CLOSED, 0) ||
                    !brWinWrite(c, k, XHCI_TBT_CFG_PFWIN, BRWIN_PF_CLOSED, 0) ||
                    !brWinWrite(c, k, XHCI_TBT_CFG_MEMWIN, mem,
                                BRWIN_MEM_MASK)) {
                    r->WinWhere = bdf;
                    return XHCI_TBT_BRWIN_ACCESS;
                }
                r->Win[k] = mem;
                /* Below a closed window everything is closed too (each
                 * Need there is 0), so none is left open at reset's 0. */
                rc = brPlace(c, (r->Now[k] >> 8) & 0xFFUL,
                             base + r->Need[k], base, depth + 1);
                if (rc != 0) {
                    return rc;
                }
                if (!brGo(c)) {
                    r->WinWhere = bdf;
                    return XHCI_TBT_BRWIN_STALE;
                }
                if (!brWinWrite(c, k, XHCI_TBT_CFG_COMMAND,
                                (r->WasCmd[k] & 0xFFFFUL) | BRWIN_CMD_MEM |
                                    BRWIN_CMD_BM,
                                BRWIN_CMD_MEM | BRWIN_CMD_BM)) {
                    r->WinWhere = bdf;
                    return XHCI_TBT_BRWIN_ACCESS;
                }
            }
            if (fn == 0 && !first) {
                break;
            }
        }
    }
    return 0;
}

/*
 * The journalled bridges of `bus` given their windows from `top` downward
 * but never below `floor`: the largest alignment first, so that windows
 * whose sizes are multiples of their alignments pack with no gap, then
 * the closed ones. 0, or a refusal.
 */
static ULONG brPlace(BRNUM_CTX *c, ULONG bus, ULONG top, ULONG floor,
                     ULONG depth)
{
    ULONG k;
    ULONG most;
    ULONG a;
    ULONG rc;

    most = 0;
    for (k = 0; k < c->Count; k++) {
        if ((c->R->Now[k] & 0xFFUL) == bus && c->R->Need[k] != 0 &&
            c->R->Align[k] > most) {
            most = c->R->Align[k];
        }
    }
    for (a = most; a >= BRWIN_GRAIN; a >>= 1) {
        rc = brPlaceAt(c, bus, &top, floor, depth, a);
        if (rc != 0) {
            return rc;
        }
    }
    return brPlaceAt(c, bus, &top, floor, depth, 0);
}

/* One register written back, read back under `compare` (0: not read). */
static ULONG brPut(BRNUM_CTX *c, ULONG bdf, ULONG offset, ULONG value,
                   ULONG compare)
{
    ULONG back;

    c->Io->Writes++;
    c->R->Writes++;
    if (!c->Io->Write(c->Io->Context, bdf, offset, 4, value)) {
        return 0;
    }
    if (compare == 0) {
        return 1;
    }
    return (brRead(c->Io, bdf, offset, &back) &&
            (back & compare) == (value & compare)) ? 1UL : 0UL;
}

/* Every bridge's window registers and command written back as read,
 * deepest first, each only while it still reads as the one journalled;
 * each register attempted whatever the others did. */
static VOID brWinUndo(BRNUM_CTX *c)
{
    PXHCI_TBT_BRNUM r;
    ULONG k;
    ULONG bdf;
    ULONG ok;

    r = c->R;
    for (k = c->Count; k > 0; k--) {
        if (!r->WinDone[k - 1]) {
            continue;
        }
        bdf = r->Bdf[k - 1];
        if (!brSame(c->Io, bdf, r->Id[k - 1])) {
            r->UndoFailed++;
            continue;
        }
        ok = brPut(c, bdf, XHCI_TBT_CFG_COMMAND,
                   r->WasCmd[k - 1] & 0xFFFFUL, BRWIN_CMD_MEM | BRWIN_CMD_BM);
        ok &= brPut(c, bdf, XHCI_TBT_CFG_MEMWIN, r->WasMem[k - 1],
                    BRWIN_MEM_MASK);
        ok &= brPut(c, bdf, XHCI_TBT_CFG_PFWIN, r->WasPf[k - 1],
                    BRWIN_MEM_MASK);
        ok &= brPut(c, bdf, XHCI_TBT_CFG_IOWIN, r->WasIo[k - 1] & 0xFFFFUL,
                    0x0000F0F0UL);
        if (!ok) {
            r->UndoFailed++;
            continue;
        }
        r->WinDone[k - 1] = 0;
        r->Win[k - 1] = 0;
        r->Undone++;
    }
}

/*
 * The windows of the chain just numbered: sized from the BARs behind each
 * bridge, placed from the top of the port's memory window downward,
 * prefetchable and I/O windows closed, memory and bus master enabled.
 * Sets r->WinOutcome; on a refusal every window written is put back.
 */
static VOID brWindows(BRNUM_CTX *c, ULONG port, ULONG secondary)
{
    PXHCI_TBT_BRNUM r;
    ULONG pw;
    ULONG pbase;
    ULONG plimit;
    ULONG need;
    ULONG align;
    ULONG k;
    ULONG rc;

    r = c->R;
    for (k = 0; k < c->Count; k++) {
        if (!brRead(c->Io, r->Bdf[k], XHCI_TBT_CFG_COMMAND, &r->WasCmd[k]) ||
            !brRead(c->Io, r->Bdf[k], XHCI_TBT_CFG_IOWIN, &r->WasIo[k]) ||
            !brRead(c->Io, r->Bdf[k], XHCI_TBT_CFG_MEMWIN, &r->WasMem[k]) ||
            !brRead(c->Io, r->Bdf[k], XHCI_TBT_CFG_PFWIN, &r->WasPf[k])) {
            r->WinWhere = r->Bdf[k];
            r->WinOutcome = XHCI_TBT_BRWIN_ACCESS;
            return;
        }
    }
    if (!brRead(c->Io, port, XHCI_TBT_CFG_MEMWIN, &pw)) {
        r->WinWhere = port;
        r->WinOutcome = XHCI_TBT_BRWIN_ACCESS;
        return;
    }
    r->PortMem = pw;
    pbase = (pw & 0xFFF0UL) << 16;
    plimit = (((pw >> 16) & 0xFFF0UL) << 16) | 0x000FFFFFUL;
    if (pbase > plimit || plimit == 0xFFFFFFFFUL) {
        r->WinWhere = port;
        r->WinOutcome = XHCI_TBT_BRWIN_PORT_CLOSED;
        return;
    }
    rc = brNeed(c, secondary, 0, &need, &align);
    r->WinNeed = need;
    /* One more unit of an alignment above 1 MiB: the gap aligned windows
     * may leave below them is not counted in the sum, and the BARs beside
     * the chain need the rest. */
    if (rc == 0 && ((align > BRWIN_GRAIN && !brAdd(&need, align)) ||
                    need > plimit - pbase + 1UL)) {
        r->WinWhere = port;
        rc = XHCI_TBT_BRWIN_NO_ROOM;
    }
    if (rc == 0) {
        rc = brPlace(c, secondary, plimit + 1UL, pbase, 0);
    }
    if (rc != 0) {
        brWinUndo(c);
        r->WinOutcome = rc;
        return;
    }
    r->WinOutcome = XHCI_TBT_BRWIN_DONE;
}

/* ---- the interrupt lines (XHCI_TBT_BRNUM_F_IRQ) ------------------------ */

#define XHCI_TBT_CFG_INTR   0x3CUL  /* Interrupt Line 7:0, Pin 15:8; Bridge
                                       Control 31:16 on a type 1 header   */

/*
 * The Interrupt Line of each function of `bus` (and, depth first, behind
 * each journalled bridge) whose interrupt reaches the port's primary side
 * on the port's own pin, set to the line the port itself has; `rot` is
 * the swizzle a function on `bus` takes on its way up to the port (0 on
 * the port's secondary bus: a device d with pin p arrives as pin
 * (p - 1 + d) mod 4, which is the port's own INTA-relative pin). A line
 * already set (neither 0 nor FFh) is left alone, and so is a function
 * whose interrupt arrives on another of the port's pins: no line the port
 * reads is known to be routed there.
 */
static ULONG brLines(BRNUM_CTX *c, ULONG bus, ULONG rot, ULONG depth,
                     ULONG portPin, ULONG line)
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
    ULONG v;
    ULONG pin;
    ULONG back;
    ULONG k;
    ULONG rc;

    r = c->R;
    if (depth > XHCI_TBT_BRNUM_MAX_DEPTH) {
        return 0;
    }
    for (dev = 0; dev < 32UL; dev++) {
        first = 0;
        for (fn = 0; fn < 8UL; fn++) {
            bdf = XHCI_TBT_BDF(bus, dev, fn);
            layout = 0;
            multi = 0;
            probe = brProbe(c->Io, bdf, &id, &layout, &multi);
            if (probe != 1) {
                if (fn == 0 || probe != 0) {
                    break;
                }
                continue;
            }
            if (fn == 0) {
                first = multi;
            }
            k = XHCI_TBT_BRNUM_MAX_BRIDGES;
            if (layout == XHCI_TBT_HDR_BRIDGE) {
                k = brSlot(c, bdf);
            }
            if (!brGo(c)) {
                return XHCI_TBT_BRIRQ_STALE;
            }
            if ((layout == 0 || k != XHCI_TBT_BRNUM_MAX_BRIDGES) &&
                (layout == 0 || layout == XHCI_TBT_HDR_BRIDGE) &&
                brRead(c->Io, bdf, XHCI_TBT_CFG_INTR, &v)) {
                pin = (v >> 8) & 0xFFUL;
                if (pin >= 1 && pin <= 4) {
                    r->IrqFunctions++;
                    if (((pin - 1UL + dev + rot) & 3UL) != portPin - 1UL) {
                        r->IrqOther++;
                    } else if ((v & 0xFFUL) != 0 && (v & 0xFFUL) != 0xFFUL) {
                        r->IrqKept++;
                    } else if (r->IrqJournal >= XHCI_TBT_BRNUM_MAX_LINES) {
                        r->IrqFailed++;
                    } else {
                        /* Journalled before it is issued, for the undo. */
                        r->IrqBdf[r->IrqJournal] = bdf;
                        r->IrqWas[r->IrqJournal] = v;
                        r->IrqJournal++;
                        c->Io->Writes++;
                        r->Writes++;
                        if (c->Io->Write(c->Io->Context, bdf, XHCI_TBT_CFG_INTR,
                                         4, (v & 0xFFFFFF00UL) | line) &&
                            brRead(c->Io, bdf, XHCI_TBT_CFG_INTR, &back) &&
                            (back & 0xFFUL) == line) {
                            r->IrqSet++;
                        } else {
                            r->IrqFailed++;
                        }
                    }
                }
            }
            if (k != XHCI_TBT_BRNUM_MAX_BRIDGES) {
                rc = brLines(c, (r->Now[k] >> 8) & 0xFFUL, (dev + rot) & 3UL,
                             depth + 1, portPin, line);
                if (rc != 0) {
                    return rc;
                }
            }
            if (fn == 0 && !first) {
                break;
            }
        }
    }
    return 0;
}

/* Every line written put back as read, last first; each only on a
 * function that still reads with the pin it had. */
static VOID brLinesUndo(BRNUM_CTX *c)
{
    PXHCI_TBT_BRNUM r;
    ULONG k;
    ULONG v;

    r = c->R;
    for (k = r->IrqJournal; k > 0; k--) {
        if (!brRead(c->Io, r->IrqBdf[k - 1], XHCI_TBT_CFG_INTR, &v) ||
            (v & 0xFF00UL) != (r->IrqWas[k - 1] & 0xFF00UL)) {
            r->UndoFailed++;
            continue;
        }
        if (!brPut(c, r->IrqBdf[k - 1], XHCI_TBT_CFG_INTR,
                   (v & 0xFFFFFF00UL) | (r->IrqWas[k - 1] & 0xFFUL), 0xFFUL)) {
            r->UndoFailed++;
            continue;
        }
        r->Undone++;
    }
    r->IrqJournal = 0;
}

/* The interrupt lines of the chain just set up: the port's own Interrupt
 * Line and Pin read, and handed down (brLines). */
static VOID brIrq(BRNUM_CTX *c, ULONG port, ULONG secondary)
{
    PXHCI_TBT_BRNUM r;
    ULONG v;
    ULONG pin;
    ULONG line;

    r = c->R;
    if (!brRead(c->Io, port, XHCI_TBT_CFG_INTR, &v)) {
        r->IrqOutcome = XHCI_TBT_BRIRQ_ACCESS;
        return;
    }
    line = v & 0xFFUL;
    pin = (v >> 8) & 0xFFUL;
    r->IrqLine = line;
    r->IrqPin = pin;
    if (pin < 1 || pin > 4 || line == 0 || line >= 16) {
        r->IrqOutcome = XHCI_TBT_BRIRQ_PORT_NONE;
        return;
    }
    if (brLines(c, secondary, 0, 0, pin, line) != 0) {
        brLinesUndo(c);
        r->IrqOutcome = XHCI_TBT_BRIRQ_STALE;
        return;
    }
    r->IrqOutcome = r->IrqFailed != 0 ? XHCI_TBT_BRIRQ_ACCESS
                                      : XHCI_TBT_BRIRQ_DONE;
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

ULONG XhciTbtNumberBridges(PXHCI_TBT_CFG io, ULONG port, ULONG flags,
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
    r->WinOutcome = XHCI_TBT_BRWIN_NONE;
    r->IrqOutcome = XHCI_TBT_BRIRQ_NONE;
    r->IrqLine = 0;
    r->IrqPin = 0;
    r->IrqFunctions = 0;
    r->IrqSet = 0;
    r->IrqKept = 0;
    r->IrqOther = 0;
    r->IrqFailed = 0;
    r->IrqJournal = 0;
    r->WinWhere = XHCI_TBT_NO_BDF;
    r->PortMem = 0;
    r->WinNeed = 0;
    for (k = 0; k < XHCI_TBT_BRNUM_MAX_BRIDGES; k++) {
        r->Bdf[k] = XHCI_TBT_NO_BDF;
        r->Id[k] = 0;
        r->Was[k] = 0;
        r->Now[k] = 0;
        r->Need[k] = 0;
        r->Align[k] = 0;
        r->Win[k] = 0;
        r->WinDone[k] = 0;
        r->WasCmd[k] = 0;
        r->WasIo[k] = 0;
        r->WasMem[k] = 0;
        r->WasPf[k] = 0;
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
    c.Flags = flags;
    rc = brBus(&c, secondary, 0, &k);
    if (rc == 0) {
        r->Numbered = c.Count;
        r->Last = c.Count != 0 ? c.Next - 1 : 0;
        r->Outcome = XHCI_TBT_BRNUM_DONE;
        if ((flags & XHCI_TBT_BRNUM_F_WINDOWS) != 0 && c.Count != 0) {
            brWindows(&c, port, secondary);
            if (r->WinOutcome == XHCI_TBT_BRWIN_STALE) {
                /* Superseded: the bus numbers go too, so the newer request
                 * finds the chain in reset state and sets it all up. */
                brUndo(&c);
                r->Numbered = 0;
                r->Last = 0;
                r->Outcome = XHCI_TBT_BRNUM_STALE;
                return r->Outcome;
            }
        }
        if ((flags & XHCI_TBT_BRNUM_F_IRQ) != 0 && c.Count != 0 &&
            ((flags & XHCI_TBT_BRNUM_F_WINDOWS) == 0 ||
             r->WinOutcome == XHCI_TBT_BRWIN_DONE)) {
            brIrq(&c, port, secondary);
            if (r->IrqOutcome == XHCI_TBT_BRIRQ_STALE) {
                /* Superseded: windows and bus numbers go too. */
                brWinUndo(&c);
                brUndo(&c);
                r->Numbered = 0;
                r->Last = 0;
                r->Outcome = XHCI_TBT_BRNUM_STALE;
                return r->Outcome;
            }
        }
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
