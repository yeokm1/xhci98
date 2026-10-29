/*
 * pfw_scan.c - the pure half of XHCIPFW.VXD (roadmap-usb4.md task 44.49,
 * design record 32 section 4.1); pfw_scan.h says what it is and why it is
 * written so. Runs once, from the VxD's Sys_Critical_Init with interrupts
 * disabled, or from the host suite; nothing here waits or allocates.
 *
 * No 64-bit arithmetic (AGENTS.md): a 64-bit address is a pair, the upper
 * dword compared first.
 */

#include "pfw_scan.h"

/* 1 when (ahi:alo) is above (bhi:blo). */
int PfwAbove(PFW_U32 ahi, PFW_U32 alo, PFW_U32 bhi, PFW_U32 blo)
{
    if (ahi != bhi) {
        return ahi > bhi;
    }
    return alo > blo;
}

/* The window's base and limit, low halves: address bits 31:20 from the
 * two address fields, the limit's 19:0 taken as ones. */
PFW_U32 PfwBaseLo(PFW_U32 r24)
{
    return (r24 & 0x0000FFF0UL) << 16;
}

PFW_U32 PfwLimitLo(PFW_U32 r24)
{
    return (r24 & 0xFFF00000UL) | 0x000FFFFFUL;
}

int PfwClassify(PFW_U32 r24, PFW_U32 r28, PFW_U32 r2c)
{
    if ((r24 & 0xFUL) != 1UL) {
        return PFW_CLASS_LEAVE;     /* a 32-bit window, or none */
    }
    if (r28 != 0) {
        return PFW_CLASS_ABOVE;
    }
    if (r2c != 0) {
        return PFW_CLASS_ACROSS;
    }
    return PFW_CLASS_LEAVE;
}

/* Record 32 section 4.1 step 3's invariant: closed, or wholly at or above
 * 4 GB (a nonzero upper base puts the base, and so every forwarded
 * address, there). */
int PfwSafe(PFW_U32 r24, PFW_U32 r28, PFW_U32 r2c)
{
    if (r28 != 0) {
        return 1;
    }
    return PfwAbove(r28, PfwBaseLo(r24), r2c, PfwLimitLo(r24));
}

int PfwMatch(PFW_U32 off, PFW_U32 got, PFW_U32 want)
{
    if (off == PFW_CFG_PF) {
        /* The type nibbles are read-only (1h for a 64-bit window). */
        return (got & PFW_PF_ADDR_MASK) == (want & PFW_PF_ADDR_MASK);
    }
    return got == want;
}

int PfwStateSafe(PFW_U32 bus, PFW_U32 devfn)
{
    return PfwSafe(PfwCfgRead(bus, devfn, PFW_CFG_PF),
                   PfwCfgRead(bus, devfn, PFW_CFG_PFBU),
                   PfwCfgRead(bus, devfn, PFW_CFG_PFLU));
}

/*
 * The close (record 32 section 4.1 step 3): 28h = FFFFFFFFh, 2Ch = 0,
 * 24h = 0000FFF0h, 28h = 0, each write read back and the state checked;
 * on a write not taken or a state not safe, that write and every earlier
 * one undone in reverse order, each read back. Returns the action;
 * after[] the three registers as they read at the end.
 */
int PfwClose(PFW_U32 bus, PFW_U32 devfn, PFW_U32 *after)
{
    PFW_U32 off[4];
    PFW_U32 val[4];
    PFW_U32 prior[4];
    PFW_U32 got;
    int k;
    int j;
    int action;

    /* Assigned one by one: an initialised local array would be copied
     * from this object's data, the reference LINK 5.12 mislinks. */
    off[0] = PFW_CFG_PFBU;
    val[0] = 0xFFFFFFFFUL;
    prior[0] = PfwCfgRead(bus, devfn, PFW_CFG_PFBU);
    off[1] = PFW_CFG_PFLU;
    val[1] = 0;
    prior[1] = PfwCfgRead(bus, devfn, PFW_CFG_PFLU);
    off[2] = PFW_CFG_PF;
    val[2] = PFW_PF_CLOSED;
    prior[2] = PfwCfgRead(bus, devfn, PFW_CFG_PF);
    off[3] = PFW_CFG_PFBU;
    val[3] = 0;
    prior[3] = 0xFFFFFFFFUL;

    action = PFW_ACT_CLOSED;
    for (k = 0; k < 4; k++) {
        PfwCfgWrite(bus, devfn, off[k], val[k]);
        got = PfwCfgRead(bus, devfn, off[k]);
        if (PfwMatch(off[k], got, val[k]) && PfwStateSafe(bus, devfn)) {
            continue;
        }
        action = PFW_ACT_REFUSED;
        for (j = k; j >= 0; j--) {
            PfwCfgWrite(bus, devfn, off[j], prior[j]);
            got = PfwCfgRead(bus, devfn, off[j]);
            if (!PfwMatch(off[j], got, prior[j])) {
                action = PFW_ACT_PARKED;
                break;
            }
        }
        break;
    }
    after[0] = PfwCfgRead(bus, devfn, PFW_CFG_PF);
    after[1] = PfwCfgRead(bus, devfn, PFW_CFG_PFBU);
    after[2] = PfwCfgRead(bus, devfn, PFW_CFG_PFLU);
    return action;
}

/* A function's BARs (record 32 section 4.1 step 4): each 64-bit memory
 * BAR whose address lies inside a window to be closed is counted once,
 * read and never sized. */
void PfwBars(PFW_RESULT *r, PFW_U32 bus, PFW_U32 devfn, PFW_U32 type)
{
    PFW_U32 off;
    PFW_U32 end;
    PFW_U32 lo;
    PFW_U32 hi;
    PFW_U32 i;
    PFW_SUBJ *s;

    if (type == 0) {
        end = 0x28UL;
    } else if (type == 1) {
        end = 0x18UL;
    } else {
        return;
    }
    for (off = 0x10UL; off < end; off += 4) {
        lo = PfwCfgRead(bus, devfn, off);
        if ((lo & 1UL) != 0 || (lo & 6UL) != 4UL) {
            continue;       /* I/O, or a 32-bit memory BAR (below 4 GB) */
        }
        if (off + 4 >= end) {
            break;
        }
        hi = PfwCfgRead(bus, devfn, off + 4);
        lo &= 0xFFFFFFF0UL;
        if (hi != 0) {
            for (i = 0; i < r->NSubj; i++) {
                s = &r->Subj[i];
                if (s->Across || bus < s->Sec || bus > s->Sub) {
                    continue;
                }
                if (PfwAbove(s->Reg[1], PfwBaseLo(s->Reg[0]), hi, lo) ||
                    PfwAbove(hi, lo, s->Reg[2], PfwLimitLo(s->Reg[0]))) {
                    continue;
                }
                r->BarsInside++;
                if (r->NBars < PFW_MAX_BARS) {
                    r->Bars[r->NBars].Bus = (PFW_U8)bus;
                    r->Bars[r->NBars].DevFn = (PFW_U8)devfn;
                    r->Bars[r->NBars].Offset = (PFW_U8)off;
                    r->Bars[r->NBars].Zero = 0;
                    r->Bars[r->NBars].Upper = hi;
                    r->NBars++;
                } else {
                    r->Result |= PFW_RES_FULL;
                }
                break;
            }
        }
        off += 4;           /* the upper dword was this BAR's */
    }
}

/* An xHCI controller (class 0C0330h, a type 0 header) with a 64-bit
 * memory BAR whose upper half is not 0 is logged, nothing written to it
 * (the owner, 2026-10-27; record 32 section 4.4). */
void PfwXhci(PFW_RESULT *r, PFW_U32 bus, PFW_U32 devfn)
{
    PFW_U32 off;
    PFW_U32 lo;
    PFW_U32 hi;
    PFW_XHCI_REC *x;

    if ((PfwCfgRead(bus, devfn, PFW_CFG_CLASS) >> 8) != PFW_CLASS_XHCI) {
        return;
    }
    for (off = 0x10UL; off + 4 < 0x28UL; off += 4) {
        lo = PfwCfgRead(bus, devfn, off);
        if ((lo & 1UL) != 0 || (lo & 6UL) != 4UL) {
            continue;
        }
        hi = PfwCfgRead(bus, devfn, off + 4);
        if (hi != 0) {
            r->XhciHigh++;
            r->Result |= PFW_RES_XHCI_HIGH;
            if (r->NXhci < PFW_MAX_XHCI) {
                x = &r->Xhci[r->NXhci];
                x->Bus = (PFW_U8)bus;
                x->DevFn = (PFW_U8)devfn;
                x->Offset = (PFW_U8)off;
                x->Zero = 0;
                x->Id = PfwCfgRead(bus, devfn, PFW_CFG_ID);
                x->Hi = hi;
                x->Lo = lo & 0xFFFFFFF0UL;
                r->NXhci++;
            } else {
                r->Result |= PFW_RES_FULL;
            }
        }
        off += 4;
    }
}

/* One function found by the scan: pass 1 keeps the bridges with a window
 * above or across 4 GB, pass 2 counts the BARs inside them. */
void PfwFunction(PFW_RESULT *r, PFW_U32 pass, PFW_U32 bus, PFW_U32 devfn,
                 PFW_U32 type)
{
    PFW_U32 b;
    PFW_U32 r24;
    PFW_U32 r28;
    PFW_U32 r2c;
    int c;
    PFW_SUBJ *s;

    if (pass == 2) {
        PfwBars(r, bus, devfn, type);
        return;
    }
    r->Functions++;
    if (type == 0) {
        PfwXhci(r, bus, devfn);
        return;
    }
    if (type != 1) {
        return;             /* a CardBus bridge or another header: left */
    }
    r24 = PfwCfgRead(bus, devfn, PFW_CFG_PF);
    r28 = PfwCfgRead(bus, devfn, PFW_CFG_PFBU);
    r2c = PfwCfgRead(bus, devfn, PFW_CFG_PFLU);
    c = PfwClassify(r24, r28, r2c);
    if (c == PFW_CLASS_LEAVE) {
        return;
    }
    if (r->NSubj >= PFW_MAX_SUBJ) {
        r->Result |= PFW_RES_FULL;
        return;
    }
    b = PfwCfgRead(bus, devfn, PFW_CFG_BUS);
    s = &r->Subj[r->NSubj];
    s->Bus = bus;
    s->DevFn = devfn;
    s->Sec = (b >> 8) & 0xFFUL;
    s->Sub = (b >> 16) & 0xFFUL;
    s->Reg[0] = r24;
    s->Reg[1] = r28;
    s->Reg[2] = r2c;
    s->Across = (c == PFW_CLASS_ACROSS) ? 1UL : 0UL;
    r->NSubj++;
}

/* Every bus 0 to 255, device 0 to 31, functions 1 to 7 only where
 * function 0 says the device has them (record 32 section 4.1 step 1). */
void PfwScan(PFW_RESULT *r, PFW_U32 pass)
{
    PFW_U32 bus;
    PFW_U32 dev;
    PFW_U32 fn;
    PFW_U32 nfn;
    PFW_U32 id;
    PFW_U32 hdr;

    for (bus = 0; bus < 256; bus++) {
        for (dev = 0; dev < 32; dev++) {
            id = PfwCfgRead(bus, dev << 3, PFW_CFG_ID);
            if ((id & 0xFFFFUL) == 0xFFFFUL) {
                continue;
            }
            hdr = PfwCfgRead(bus, dev << 3, PFW_CFG_HDR);
            nfn = (hdr & 0x00800000UL) ? 8UL : 1UL;
            for (fn = 0; fn < nfn; fn++) {
                if (fn != 0) {
                    id = PfwCfgRead(bus, (dev << 3) | fn, PFW_CFG_ID);
                    if ((id & 0xFFFFUL) == 0xFFFFUL) {
                        continue;
                    }
                    hdr = PfwCfgRead(bus, (dev << 3) | fn, PFW_CFG_HDR);
                }
                PfwFunction(r, pass, bus, (dev << 3) | fn,
                            (hdr >> 16) & 0x7FUL);
            }
        }
    }
}

void PfwRecord(PFW_RESULT *r, PFW_SUBJ *s, int action, PFW_U32 *after)
{
    PFW_BRIDGE_REC *b;
    int i;

    if (r->NBridges >= PFW_MAX_BRIDGES) {
        r->Result |= PFW_RES_FULL;
        return;
    }
    b = &r->Bridges[r->NBridges];
    b->Bus = (PFW_U8)s->Bus;
    b->DevFn = (PFW_U8)s->DevFn;
    b->Action = (PFW_U8)action;
    b->Zero = 0;
    for (i = 0; i < 3; i++) {
        b->Before[i] = s->Reg[i];
        b->After[i] = after[i];
    }
    r->NBridges++;
}

void PfwRun(PFW_RESULT *r)
{
    PFW_U32 i;
    PFW_U32 after[3];
    PFW_SUBJ *s;
    int action;

    PfwScan(r, 1);
    if (r->NSubj != 0) {
        PfwScan(r, 2);
    }
    for (i = 0; i < r->NSubj; i++) {
        s = &r->Subj[i];
        after[0] = PfwCfgRead(s->Bus, s->DevFn, PFW_CFG_PF);
        after[1] = PfwCfgRead(s->Bus, s->DevFn, PFW_CFG_PFBU);
        after[2] = PfwCfgRead(s->Bus, s->DevFn, PFW_CFG_PFLU);
        if (after[0] != s->Reg[0] || after[1] != s->Reg[1] ||
            after[2] != s->Reg[2]) {
            r->Result |= PFW_RES_CHANGED;   /* left: not what was read */
            continue;
        }
        if (s->Across) {
            r->Across++;
            r->Result |= PFW_RES_ACROSS;
            PfwRecord(r, s, PFW_ACT_ACROSS, after);
            continue;
        }
        action = PfwClose(s->Bus, s->DevFn, after);
        if (action == PFW_ACT_CLOSED) {
            r->Closed++;
        } else if (action == PFW_ACT_REFUSED) {
            r->Refused++;
            r->Result |= PFW_RES_REFUSED;
        } else {
            r->Parked++;
            r->Result |= PFW_RES_PARKED;
        }
        PfwRecord(r, s, action, after);
    }
    r->Result |= PFW_RES_DONE;
}
