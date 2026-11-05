/*
 * pfw_line.c - XHCIPFW.VXD's interrupt-line pass (roadmap-usb4.md task
 * 46.11, design record 32 section 12.10): Interrupt Line FFh on every host
 * interface (NHI) xhcicm.inf names and on its own downstream port, chosen
 * by topology, at Sys_Critical_Init after the window step. Pure, as
 * pfw_scan.c (whose rules it keeps: no file-scope static, no data, no
 * initialised local array); in a file of its own so that XHCIPFW.SYS,
 * which links pfw_scan.c and has no such pass, links none of it. The
 * caller supplies PfwCfgRead and PfwCfgWrite8; test\test_pfw.c links it.
 */

#include "pfw_scan.h"

/* ---- the interrupt-line pass (record 32 section 12.10, task 46.11) ---- */

PFW_U32 PfwSwitchSetting(PFW_U32 openRc, PFW_U32 queryRc, PFW_U32 type,
                         PFW_U32 length, PFW_U32 raw)
{
    if (openRc != 0) {
        return PFW_SW_FAILED;
    }
    if (queryRc == PFW_RC_NOT_FOUND) {
        return PFW_SW_ON;           /* missing: the default */
    }
    if (queryRc != 0 || type != PFW_TYPE_DWORD || length != 4UL) {
        return PFW_SW_FAILED;       /* never overrides a 0 set */
    }
    return raw == 0 ? PFW_SW_OFF : PFW_SW_ON;   /* another value refused */
}

int PfwIsNhi(PFW_U32 id, PFW_U32 classReg)
{
    PFW_U32 i;

    if ((classReg >> 8) == PFW_CLASS_USB4_NHI) {
        return 1;
    }
    for (i = 0; i < PfwNhiIdCount; i++) {
        if (PfwNhiIds[i] == id) {
            return 1;
        }
    }
    return 0;
}

/* 1 when function `devfn` of bus `bus` exists. */
int PfwPresent(PFW_U32 bus, PFW_U32 devfn)
{
    return (PfwCfgRead(bus, devfn, PFW_CFG_ID) & 0xFFFFUL) != 0xFFFFUL;
}

/* 1 when bus `bus` holds no function but the NHI at `nhi`: every other
 * device absent, and the NHI's device's other functions absent too. */
int PfwAlone(PFW_U32 bus, PFW_U32 nhi)
{
    PFW_U32 dev;
    PFW_U32 fn;

    for (dev = 0; dev < 32; dev++) {
        if (dev == (nhi >> 3)) {
            continue;
        }
        if (PfwPresent(bus, dev << 3)) {
            return 0;
        }
    }
    for (fn = 0; fn < 8; fn++) {
        if (fn != (nhi & 7UL) && PfwPresent(bus, (nhi & 0xF8UL) | fn)) {
            return 0;
        }
    }
    return 1;
}

/* Step 1's flat scan: every NHI (step 2) kept, up to PFW_MAX_NHI. */
void PfwFindNhi(PFW_RESULT *r)
{
    PFW_U32 bus;
    PFW_U32 dev;
    PFW_U32 fn;
    PFW_U32 nfn;
    PFW_U32 id;
    PFW_U32 hdr;
    PFW_U32 df;

    for (bus = 0; bus < 256; bus++) {
        for (dev = 0; dev < 32; dev++) {
            hdr = PfwCfgRead(bus, dev << 3, PFW_CFG_HDR);
            if (!PfwPresent(bus, dev << 3)) {
                continue;
            }
            nfn = (hdr & 0x00800000UL) ? 8UL : 1UL;
            for (fn = 0; fn < nfn; fn++) {
                df = (dev << 3) | fn;
                id = PfwCfgRead(bus, df, PFW_CFG_ID);
                if ((id & 0xFFFFUL) == 0xFFFFUL ||
                    ((PfwCfgRead(bus, df, PFW_CFG_HDR) >> 16) & 0x7FUL) != 0 ||
                    !PfwIsNhi(id, PfwCfgRead(bus, df, PFW_CFG_CLASS)) ||
                    ((PfwCfgRead(bus, df, PFW_CFG_LINE) >> 8) & 0xFFUL) == 0) {
                    continue;       /* not an NHI, or Interrupt Pin 0 */
                }
                if (r->NNhi >= PFW_MAX_NHI) {
                    r->Result |= PFW_RES_FULL;
                    continue;
                }
                r->NhiBus[r->NNhi] = bus;
                r->NhiDevFn[r->NNhi] = df;
                r->NNhi++;
            }
        }
    }
}

/* Step 3: the one type 1 function whose secondary bus is `sec`, in
 * *port; 0 when none or more than one claims it. */
int PfwPortOf(PFW_U32 sec, PFW_U32 *portBus, PFW_U32 *portDevFn)
{
    PFW_U32 bus;
    PFW_U32 dev;
    PFW_U32 fn;
    PFW_U32 nfn;
    PFW_U32 hdr;
    PFW_U32 df;
    PFW_U32 found;

    found = 0;
    for (bus = 0; bus < 256; bus++) {
        for (dev = 0; dev < 32; dev++) {
            if (!PfwPresent(bus, dev << 3)) {
                continue;
            }
            hdr = PfwCfgRead(bus, dev << 3, PFW_CFG_HDR);
            nfn = (hdr & 0x00800000UL) ? 8UL : 1UL;
            for (fn = 0; fn < nfn; fn++) {
                df = (dev << 3) | fn;
                if (fn != 0) {
                    if (!PfwPresent(bus, df)) {
                        continue;
                    }
                    hdr = PfwCfgRead(bus, df, PFW_CFG_HDR);
                }
                if (((hdr >> 16) & 0x7FUL) != 1UL ||
                    ((PfwCfgRead(bus, df, PFW_CFG_BUS) >> 8) & 0xFFUL) !=
                        sec) {
                    continue;
                }
                found++;
                *portBus = bus;
                *portDevFn = df;
            }
        }
    }
    return found == 1UL;
}

/* Steps 4 and 5 for one selected function; its record (step 6). */
void PfwLineOne(PFW_RESULT *r, PFW_U32 bus, PFW_U32 devfn, PFW_U32 role)
{
    PFW_U32 before;
    PFW_U32 after;
    PFW_U32 hdr;
    PFW_LINE_REC *x;
    int outcome;

    before = PfwCfgRead(bus, devfn, PFW_CFG_LINE);
    if (((before >> 8) & 0xFFUL) == 0) {
        return;                     /* Interrupt Pin 0: not selected */
    }
    hdr = (PfwCfgRead(bus, devfn, PFW_CFG_HDR) >> 16) & 0x7FUL;
    after = before;
    if ((PfwCfgRead(bus, devfn, PFW_CFG_CMD) & 3UL) == 0) {
        outcome = PFW_LN_NODECODE;
    } else if (r->Switch.Setting != PFW_SW_ON) {
        outcome = PFW_LN_LEFT;
    } else if ((before & 0xFFUL) == 0xFFUL) {
        outcome = PFW_LN_ALREADY;
    } else {
        PfwCfgWrite8(bus, devfn, PFW_CFG_LINE, 0xFFUL);
        after = PfwCfgRead(bus, devfn, PFW_CFG_LINE);
        if (after == ((before & 0xFFFFFF00UL) | 0xFFUL)) {
            outcome = PFW_LN_WRITTEN;
            r->Result |= PFW_RES_LINE;
        } else {
            PfwCfgWrite8(bus, devfn, PFW_CFG_LINE, before & 0xFFUL);
            after = PfwCfgRead(bus, devfn, PFW_CFG_LINE);
            outcome = (after & 0xFFUL) == (before & 0xFFUL) ? PFW_LN_REFUSED
                                                            : PFW_LN_STUCK;
            r->Result |= PFW_RES_LINE_BAD;
        }
    }
    if (r->NLines >= PFW_MAX_LINES) {
        r->Result |= PFW_RES_FULL;
        return;
    }
    x = &r->Lines[r->NLines];
    x->Bus = (PFW_U8)bus;
    x->DevFn = (PFW_U8)devfn;
    x->Role = (PFW_U8)role;
    x->Outcome = (PFW_U8)outcome;
    x->Before = (PFW_U8)(before & 0xFFUL);
    x->After = (PFW_U8)(after & 0xFFUL);
    x->Hdr = (PFW_U8)hdr;
    x->Zero = 0;
    r->NLines++;
}

void PfwLines(PFW_RESULT *r)
{
    PFW_U32 i;
    PFW_U32 pb;
    PFW_U32 pdf;

    if (r->Switch.Setting == PFW_SW_OFF) {
        r->Result |= PFW_RES_LINE_OFF;
    } else if (r->Switch.Setting != PFW_SW_ON) {
        r->Result |= PFW_RES_LINE_NOREAD;
    }
    PfwFindNhi(r);
    for (i = 0; i < r->NNhi; i++) {
        PfwLineOne(r, r->NhiBus[i], r->NhiDevFn[i], PFW_ROLE_NHI);
        if (r->NhiBus[i] == 0 || !PfwAlone(r->NhiBus[i], r->NhiDevFn[i]) ||
            !PfwPortOf(r->NhiBus[i], &pb, &pdf)) {
            continue;               /* the root bus, or no port of its own */
        }
        PfwLineOne(r, pb, pdf, PFW_ROLE_PORT);
    }
}
