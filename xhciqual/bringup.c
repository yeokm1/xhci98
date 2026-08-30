/*
 * bringup.c - Tier C active tests: BIOS handoff (C1), halt+reset (C2),
 * DMA round-trip via No-Op command (C3), port connect/reset (C6), Intel
 * XUSB2PR switchover (C7), and state restoration on exit.
 *
 * Register sequences follow docs/usb-xhci-info/xhci-programming.md ("Initialization
 * Sequence") and bit positions follow docs/usb-xhci-info/xhci-data-structures.md.
 */

#include <i86.h>
#include <conio.h>
#include <stdio.h>
#include <string.h>
#include "qual.h"

/* ------------------------------------------------------------------ */
/* timing                                                             */
/* ------------------------------------------------------------------ */

void msleep(unsigned ms)
{
    delay(ms);
}

u32 ticks_now(void)
{
    return *(volatile u32 *)0x46CUL;    /* BIOS tick counter, 18.2 Hz */
}

u32 ticks_to_ms(u32 dt)
{
    return dt * 55;
}

/* ------------------------------------------------------------------ */
/* DMA memory                                                         */
/* ------------------------------------------------------------------ */

static int dma_crosses(u32 a, u32 size, u32 boundary)
{
    if (boundary == 0)
        return 0;
    return ((a ^ (a + size - 1)) & ~(boundary - 1)) != 0;
}

/*
 * Allocate conventional memory, aligned, guaranteed not to cross a
 * physical boundary of the given size (spec Table 6-1: rings must not
 * cross 64 KB, DCBAA/contexts/scratchpad array must not cross PAGESIZE).
 * Requires size <= boundary (0 = no boundary rule).
 *
 * Conventional memory is scarce (a controller with a large scratchpad
 * already needs a few hundred KB), so try the tight request first and only
 * pay a whole boundary of slack when the block DOS handed back really does
 * straddle one. Every block is tracked for release - see dos_free_all().
 */
void *dma_alloc(u32 size, u32 align, u32 boundary, u32 *phys)
{
    u32 raw_phys, a;
    u8 *raw;

    raw = (u8 *)dos_alloc(size + align, &raw_phys);
    if (raw != 0) {
        a = (raw_phys + align - 1) & ~(align - 1);
        if (!dma_crosses(a, size, boundary)) {
            *phys = a;
            memset((void *)a, 0, size);
            return (void *)a;
        }
        dos_free_last();
    }

    raw = (u8 *)dos_alloc(size + align + boundary, &raw_phys);
    if (raw == 0)
        return 0;
    a = (raw_phys + align - 1) & ~(align - 1);
    if (dma_crosses(a, size, boundary))
        a = (a + boundary - 1) & ~(boundary - 1);
    *phys = a;
    memset((void *)a, 0, size);
    return (void *)a;
}

/* ------------------------------------------------------------------ */
/* PORTSC helpers (safe-write pattern from docs/usb-xhci-info/xhci-programming.md)  */
/* ------------------------------------------------------------------ */

u32 portsc_read(CTRL *c, int port)
{
    return RD32(c->op + XOP_PORTSC(port));
}

void portsc_write(CTRL *c, int port, u32 setbits)
{
    u32 v = portsc_read(c, port);
    v &= ~(PSC_PED | PSC_PR | PSC_LWS | PSC_CHANGE_BITS);
    v |= setbits;
    WR32(c->op + XOP_PORTSC(port), v);
}

void portsc_clear_changes(CTRL *c, int port, u32 changebits)
{
    portsc_write(c, port, changebits & PSC_CHANGE_BITS);
}

/* ------------------------------------------------------------------ */
/* event ring consumer                                                */
/* ------------------------------------------------------------------ */

/*
 * ERDP publishing follows the rule in docs/usb-xhci-info/xhci-data-structures.md (ISR/DPC
 * rules): EHB is RW1C, so an intermediate write during a drain carries 0 in
 * bit 3 and *leaves* Event Handler Busy set, and only the write made after
 * the ring reads empty carries EHB = 1 to release it. Every caller loops
 * until this returns 0, so the releasing write always happens.
 */
static void evt_publish(CTRL *c, u32 ehb)
{
    WR32(c->rt + XRT_ERDP, (c->evt_phys + (u32)c->evt_deq * 16) | ehb);
    WR32(c->rt + XRT_ERDP + 4, 0);
}

int evt_next(CTRL *c, TRB *out)
{
    TRB *t = &c->evt[c->evt_deq];

    if (((t->ctl & TRB_C) != 0) != (c->evt_ccs != 0)) {
        evt_publish(c, ERDP_EHB);       /* ring empty: release EHB */
        return 0;
    }
    *out = *t;
    c->evt_deq++;
    if (c->evt_deq == EVT_TRBS) {
        c->evt_deq = 0;
        c->evt_ccs ^= 1;
    }
    evt_publish(c, 0);
    if (TRB_GET_TYPE(out->ctl) == TRB_T_PORT_EVT)
        c->port_events++;
    return 1;
}

/*
 * Drain every event currently pending on the ring, advancing ERDP (with EHB)
 * for each so nothing strands. Bounded by the ring size so a runaway event
 * source cannot spin forever. This is the C6 helper: no command is
 * outstanding while it runs, so discarding command-completion events here is
 * safe - cmd_wait() owns the ring whenever a command is in flight.
 */
int evt_drain(CTRL *c, u32 *cmd_seen, u32 *port_seen)
{
    TRB e;
    int n, guard;
    u32 nc, np;

    n = 0;
    nc = 0;
    np = 0;
    if (c->evt == 0)                    /* rings never allocated (C3 skipped) */
        goto done;
    for (guard = 0; guard < EVT_TRBS * 2 && evt_next(c, &e); guard++) {
        switch (TRB_GET_TYPE(e.ctl)) {
        case TRB_T_CMD_DONE: nc++; break;
        case TRB_T_PORT_EVT: np++; break;
        default:             break;
        }
        n++;
    }
done:
    if (cmd_seen != 0)
        *cmd_seen = nc;
    if (port_seen != 0)
        *port_seen = np;
    return n;
}

/* ------------------------------------------------------------------ */
/* C1 - BIOS -> OS handoff                                            */
/* ------------------------------------------------------------------ */

int qual_handoff(CTRL *c)
{
    u32 dw0;
    int waited;

    if (c->legsup_off == 0) {
        strcpy(c->handoff_note, "no USBLEGSUP cap - nothing to hand off");
        c->v_handoff = V_PASS;
        return 1;
    }

    dw0 = RD32(c->base + c->legsup_off);
    if ((dw0 & LEGSUP_BIOS_OWNED) == 0 && (dw0 & LEGSUP_OS_OWNED) != 0) {
        strcpy(c->handoff_note, "already OS-owned");
    }

    WR32(c->base + c->legsup_off, dw0 | LEGSUP_OS_OWNED);
    for (waited = 0; waited < 1000; waited += 10) {
        dw0 = RD32(c->base + c->legsup_off);
        if ((dw0 & LEGSUP_BIOS_OWNED) == 0)
            break;
        msleep(10);
    }

    /* disable firmware SMI regardless: clear enables 15:0, ack RW1C 31:29,
     * preserve RsvdP (D2) */
    c->legctl_orig = RD32(c->base + c->legsup_off + 4);
    WR32(c->base + c->legsup_off + 4,
         (c->legctl_orig & LEGCTL_RSVDP) | 0xE0000000UL);

    if (dw0 & LEGSUP_BIOS_OWNED) {
        sprintf(c->handoff_note,
                "BIOS-Owned did not clear in 1 s (dw0=%08lX)", dw0);
        c->v_handoff = V_WARN;   /* some firmware ignores the semaphore */
        return 0;
    }
    if (c->handoff_note[0] == 0)
        sprintf(c->handoff_note, "BIOS released in <= %d ms", waited);
    c->v_handoff = V_PASS;
    return 1;
}

/* ------------------------------------------------------------------ */
/* C2 - halt + reset                                                  */
/* ------------------------------------------------------------------ */

int qual_reset(CTRL *c)
{
    u32 v, ms;

    /* halt: clear R/S, wait HCH (spec allows 16 ms) */
    v = RD32(c->op + XOP_USBCMD);
    WR32(c->op + XOP_USBCMD, v & ~CMD_RUN);
    for (ms = 0; ms < 100; ms++) {
        if (RD32(c->op + XOP_USBSTS) & STS_HCH)
            break;
        msleep(1);
    }
    if ((RD32(c->op + XOP_USBSTS) & STS_HCH) == 0) {
        c->v_reset = V_FAIL;
        return 0;
    }

    /* reset: set HCRST, wait for HCRST and CNR to clear. Read-modify-write, so
     * the controller's RsvdP bits survive the write (repo audit D2) - a literal
     * here zeroes twenty of them on the way past. */
    v = RD32(c->op + XOP_USBCMD);
    WR32(c->op + XOP_USBCMD, (v & USBCMD_RSVDP) | CMD_HCRST);
    for (ms = 0; ms < 1000; ms++) {
        if ((RD32(c->op + XOP_USBCMD) & CMD_HCRST) == 0 &&
            (RD32(c->op + XOP_USBSTS) & STS_CNR) == 0)
            break;
        msleep(1);
    }
    c->reset_ms = ms;
    if (ms >= 1000) {
        c->v_reset = V_FAIL;
        return 0;
    }

    /* capability registers must survive the reset */
    v = RD32(c->base + XCAP_CAPLENGTH);
    if ((v & 0xFF) != c->caplength || (u16)(v >> 16) != c->hciversion) {
        c->v_reset = V_FAIL;
        return 0;
    }
    c->v_reset = V_PASS;
    return 1;
}

/* ------------------------------------------------------------------ */
/* C3 - rings + No-Op command round trip (proves bus-master DMA)      */
/* ------------------------------------------------------------------ */

/* 0 = out of conventional memory (a tool limit, not a controller fault) */
static int xhci_alloc_rings(CTRL *c)
{
    int i;

    c->dcbaa = (u32 *)dma_alloc(2048, 64, 4096, &c->dcbaa_phys);
    c->erst  = (u32 *)dma_alloc(16, 64, 4096, &c->erst_phys);
    c->cmd   = (TRB *)dma_alloc(CMD_TRBS * 16UL, 64, 0x10000UL,
                                &c->cmd_phys);
    c->evt   = (TRB *)dma_alloc(EVT_TRBS * 16UL, 64, 0x10000UL,
                                &c->evt_phys);
    if (!c->dcbaa || !c->erst || !c->cmd || !c->evt)
        return 0;

    if (c->spbufs > 0) {
        u32 pages_phys;
        u8 *pages;

        if (c->spbufs > MAX_SCRATCHPAD_PAGES)
            return 0;   /* would not fit conventional memory */
        c->sparray = (u32 *)dma_alloc((u32)c->spbufs * 8, 64, 4096,
                                      &c->sparray_phys);
        pages = (u8 *)dma_alloc((u32)c->spbufs * 4096UL, 4096, 0,
                                &pages_phys);
        if (!c->sparray || !pages)
            return 0;
        for (i = 0; i < c->spbufs; i++) {
            c->sparray[i * 2]     = pages_phys + (u32)i * 4096UL;
            c->sparray[i * 2 + 1] = 0;
        }
        c->dcbaa[0] = c->sparray_phys;
        c->dcbaa[1] = 0;
    }

    /* command ring: last TRB is a Link back to the start, TC set */
    c->cmd[CMD_TRBS - 1].p0  = c->cmd_phys;
    c->cmd[CMD_TRBS - 1].p1  = 0;
    c->cmd[CMD_TRBS - 1].ctl = TRB_TYPE(TRB_T_LINK) | TRB_LINK_TC;
    c->cmd_enq = 0;
    c->cmd_pcs = 1;

    /* single-entry ERST */
    c->erst[0] = c->evt_phys;
    c->erst[1] = 0;
    c->erst[2] = EVT_TRBS;
    c->erst[3] = 0;
    c->evt_deq = 0;
    c->evt_ccs = 1;
    return 1;
}

static void xhci_program_and_run(CTRL *c)
{
    u32 ms;

    /* Every composed write here is a read-modify-write over the register's
     * RsvdP mask (repo audit D2). DNCTRL is the one exception and it is not one:
     * 0 is the value software owns in every defined bit of it, and the RsvdP
     * half is preserved by the read. */
    WR32(c->op + XOP_CONFIG,
         (RD32(c->op + XOP_CONFIG) & CONFIG_RSVDP) | (u32)c->maxslots);
    WR32(c->op + XOP_DNCTRL, RD32(c->op + XOP_DNCTRL) & 0xFFFF0000UL);
    WR32(c->op + XOP_DCBAAP, c->dcbaa_phys);
    WR32(c->op + XOP_DCBAAP + 4, 0);
    WR32(c->op + XOP_CRCR,
         (RD32(c->op + XOP_CRCR) & CRCR_RSVDP) | c->cmd_phys | CRCR_RCS);
    WR32(c->op + XOP_CRCR + 4, 0);

    /* event ring: ERSTSZ and ERDP before ERSTBA - ERSTBA latches (4.9.4) */
    WR32(c->rt + XRT_ERSTSZ, (RD32(c->rt + XRT_ERSTSZ) & ERSTSZ_RSVDP) | 1);
    WR32(c->rt + XRT_ERDP, c->evt_phys);
    WR32(c->rt + XRT_ERDP + 4, 0);
    WR32(c->rt + XRT_ERSTBA,
         (RD32(c->rt + XRT_ERSTBA) & ERSTBA_RSVDP) | c->erst_phys);
    WR32(c->rt + XRT_ERSTBA + 4, 0);

    WR32(c->op + XOP_USBCMD, RD32(c->op + XOP_USBCMD) | CMD_RUN);
    for (ms = 0; ms < 100; ms++) {
        if ((RD32(c->op + XOP_USBSTS) & STS_HCH) == 0)
            break;
        msleep(1);
    }
}

/* enqueue a command TRB (ctl without cycle bit), ring DB[0]; returns the
 * TRB physical address for matching its Command Completion Event */
u32 cmd_submit(CTRL *c, u32 p0, u32 p1, u32 ctl)
{
    TRB *t = &c->cmd[c->cmd_enq];
    u32 phys = c->cmd_phys + (u32)c->cmd_enq * 16;

    t->p0 = p0;
    t->p1 = p1;
    t->st = 0;
    t->ctl = ctl | (c->cmd_pcs ? TRB_C : 0);

    c->cmd_enq++;
    if (c->cmd_enq == CMD_TRBS - 1) {   /* hand the Link TRB to the HC */
        TRB *l = &c->cmd[CMD_TRBS - 1];
        l->ctl = TRB_TYPE(TRB_T_LINK) | TRB_LINK_TC |
                 (c->cmd_pcs ? TRB_C : 0);
        c->cmd_pcs ^= 1;
        c->cmd_enq = 0;
    }

    WR32(c->db, 0);                     /* DB[0] = 0: command doorbell */
    return phys;
}

/*
 * Wait for the Command Completion Event matching cmd_trb_phys. Each pass
 * drains the ring to empty before returning, even after the match is found:
 * that final empty read is what releases EHB, and leaving EHB set would
 * stop the interrupter asserting IP for the next test (C4).
 */
int cmd_wait(CTRL *c, u32 cmd_trb_phys, unsigned timeout_ms,
             u32 *elapsed, u8 *code, u8 *slotid)
{
    unsigned ms;
    TRB e;
    int found = 0;

    for (ms = 0; ms < timeout_ms; ms++) {
        while (evt_next(c, &e)) {
            if (TRB_GET_TYPE(e.ctl) == TRB_T_CMD_DONE &&
                e.p0 == cmd_trb_phys) {
                found = 1;
                if (elapsed != 0)
                    *elapsed = ms;
                if (code != 0)
                    *code = (u8)TRB_GET_CODE(e.st);
                if (slotid != 0)
                    *slotid = (u8)TRB_GET_SLOT(e.ctl);
            }
        }
        if (found)
            return 1;
        msleep(1);
    }
    return 0;
}

int qual_dma(CTRL *c)
{
    u32 trb_phys, ms;
    u8 code;

    /* The allocator carves 4 KB pages for the scratchpad and page-bounded
     * structures (docs/contributing/implementation-invariants.md, "MMIO Sanity"). */
    if ((c->pagesize & 1) == 0) {
        sprintf(c->dma_note, "PAGESIZE %08lX has no 4 KB support; C3 not run",
                c->pagesize);
        c->v_dma = V_SKIP;
        return 0;
    }
    /* Distinguish "the tool ran out of conventional memory" from "the
     * controller cannot do DMA" - reporting the former as a DMA failure
     * disqualifies working hardware. */
    if (!xhci_alloc_rings(c)) {
        sprintf(c->dma_note,
                "out of conventional memory (%d scratchpad page(s) needed); "
                "C3 not run", c->spbufs);
        c->v_dma = V_SKIP;
        return 0;
    }
    xhci_program_and_run(c);
    if (RD32(c->op + XOP_USBSTS) & STS_HCH) {
        strcpy(c->dma_note, "controller refused to leave the halted state");
        c->v_dma = V_FAIL;
        return 0;
    }

    trb_phys = cmd_submit(c, 0, 0, TRB_TYPE(TRB_T_NOOP_CMD));
    if (!cmd_wait(c, trb_phys, 1000, &ms, &code, 0)) {
        strcpy(c->dma_note, "No-Op command never completed (no event)");
        c->v_dma = V_FAIL;
        return 0;
    }
    c->noop_ms = ms;
    if (code == CC_SUCCESS) {
        sprintf(c->dma_note, "No-Op completion in %lu ms", ms);
        c->v_dma = V_PASS;
    } else {
        sprintf(c->dma_note, "No-Op completed in %lu ms with code %u",
                ms, code);
        c->v_dma = V_WARN;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* C4/C5 - interrupt delivery + poll differential                     */
/* ------------------------------------------------------------------ */

/*
 * Shut the xHCI interrupt source down and remove the protected-mode vector.
 * Called the instant the C4 test finishes - pass OR fail - so every later
 * gate runs with no live ISR. On the E460 the crash was a DOS/32A
 * interrupt-reflection fault taken when C6's first port reset delivered an
 * interrupt through a handler that C4 had left installed (docs/contributing/lessons.md). After
 * this returns the controller cannot assert INTx and the vector is restored.
 */
static int xhci_irq_source_off(CTRL *c)
{
    int ok;

    WR32(c->op + XOP_USBCMD, RD32(c->op + XOP_USBCMD) & ~CMD_INTE);
    /* EINT before IMAN.IP - the reverse order can lose an interrupt
     * (docs/usb-xhci-info/xhci-data-structures.md, section 3). Both are RW1C. */
    WR32(c->op + XOP_USBSTS, STS_EINT);
    /* IE off, ack IP - and RsvdP preserved, like every other IMAN write in this
     * tool. This one was missed by the first pass at repo audit D2 and found by
     * the review round after it. */
    WR32(c->rt + XRT_IMAN, (RD32(c->rt + XRT_IMAN) & IMAN_RSVDP) | IMAN_IP);
    ok = irq_uninstall();
    c->irq_isr_count = irq_count;
    c->irq_foreign_count = irq_foreign;
    if (!ok) {
        /* Don't clobber a more specific C4 failure note already recorded by
         * the caller; only describe the teardown fault when C4 had passed. */
        if (c->v_irq != V_FAIL)
            sprintf(c->irq_note, "DPMI %s failed (AX=%04X)",
                    irq_error_step() ? irq_error_step() : "IRQ teardown",
                    irq_error_code());
        c->v_irq = V_FAIL;
    }
    return ok;
}

int qual_irq(CTRL *c)
{
    u32 trb_phys, ms, before;
    int got_evt, pending;

    if (c->pci.ipin == 0) {
        strcpy(c->irq_note, "Interrupt Pin = 0: MSI-only, no INTx");
        c->v_irq = V_FAIL;
        return 0;
    }
    if (c->pci.iline == 0 || c->pci.iline > 15) {
        sprintf(c->irq_note, "Interrupt Line %u unusable (not 1-15)",
                c->pci.iline);
        c->v_irq = V_FAIL;
        return 0;
    }
    /* IRQ 2 is the PIC cascade and is refused HERE, with its own sentence (repo
     * audit D6). `irq_install` refuses it too, but through the same path as
     * every DPMI failure - so the note read "DPMI validate IRQ handler state
     * failed (AX=0000)" for a case in which no DPMI call was made and nothing
     * failed. A reader chasing that spent the time on an extender that was
     * working perfectly. */
    if (c->pci.iline == 2) {
        strcpy(c->irq_note,
               "Interrupt Line 2 is the PIC cascade, not a usable IRQ");
        c->v_irq = V_FAIL;
        return 0;
    }
    if (!irq_install(&c->pci, HC_XHCI, c->op, c->rt)) {
        sprintf(c->irq_note, "DPMI %s failed (AX=%04X)",
                irq_error_step() ? irq_error_step() : "IRQ install",
                irq_error_code());
        c->v_irq = V_FAIL;
        return 0;
    }

    /* The line stays masked until this test's level source is proven live. */
    WR32(c->op + XOP_USBSTS, STS_EINT);
    WR32(c->rt + XRT_IMAN,
         (RD32(c->rt + XRT_IMAN) & IMAN_RSVDP) | IMAN_IP | IMAN_IE);
    WR32(c->op + XOP_USBCMD, RD32(c->op + XOP_USBCMD) | CMD_INTE);

    before = irq_count;
    trb_phys = cmd_submit(c, 0, 0, TRB_TYPE(TRB_T_NOOP_CMD));
    pending = 0;
    for (ms = 0; ms < 1000; ms++) {
        if ((RD32(c->op + XOP_USBSTS) & STS_EINT) != 0 &&
            (RD32(c->rt + XRT_IMAN) & IMAN_IP) != 0) {
            pending = 1;
            break;
        }
        msleep(1);
    }
    if (!pending) {
        got_evt = 0;
        for (ms = 0; ms < 1000 && !got_evt; ms++) {
            TRB e;

            while (evt_next(c, &e)) {
                if (TRB_GET_TYPE(e.ctl) == TRB_T_CMD_DONE &&
                    e.p0 == trb_phys)
                    got_evt = 1;
            }
            if (!got_evt)
                msleep(1);
        }
        strcpy(c->irq_note, got_evt ?
               "No xHCI IP while PIC masked; event completed by polling" :
               "No xHCI IP or No-Op event while PIC masked");
        c->v_irq = V_FAIL;
        xhci_irq_source_off(c);
        return 0;
    }
    if (!irq_unmask()) {
        sprintf(c->irq_note, "%s failed (AX=%04X)",
                irq_error_step() ? irq_error_step() : "IRQ unmask",
                irq_error_code());
        c->v_irq = V_FAIL;
        xhci_irq_source_off(c);
        return 0;
    }

    for (ms = 0; ms < 1000; ms++) {
        if (irq_count != before)
            break;
        msleep(1);
    }

    got_evt = 0;
    for (ms = 0; ms < 1000 && !got_evt; ms++) {
        {
            TRB e;

            while (evt_next(c, &e)) {
                if (TRB_GET_TYPE(e.ctl) == TRB_T_CMD_DONE &&
                    e.p0 == trb_phys)
                    got_evt = 1;
            }
        }
        if (!got_evt)
            msleep(1);
    }

    if (irq_count != before && got_evt && irq_foreign == 0) {
        sprintf(c->irq_note, "one-shot ISR fired on IRQ %u (%s per ELCR)",
                c->pci.iline,
                irq_elcr_level(c->pci.iline) ? "level" : "edge");
        c->v_irq = V_PASS;
        if (!xhci_irq_source_off(c))
            return 0;
        return 1;
    }
    if (irq_foreign != 0) {
        sprintf(c->irq_note,
                "IRQ %u entered without owned xHCI status; line masked",
                c->pci.iline);
    } else if (irq_count != before) {
        strcpy(c->irq_note, "ISR fired but matching No-Op event was absent");
    } else {
        sprintf(c->irq_note,
                "xHCI IP pending but ISR never fired on IRQ %u",
                c->pci.iline);
    }
    c->v_irq = V_FAIL;
    xhci_irq_source_off(c);
    return 0;
}

/* ------------------------------------------------------------------ */
/* C6 - port power, connect detection, port reset, speed              */
/* ------------------------------------------------------------------ */

const char *speed_name(u32 psiv)
{
    switch (psiv) {
    case 1:  return "Full-Speed (12 Mb/s)";
    case 2:  return "Low-Speed (1.5 Mb/s)";
    case 3:  return "High-Speed (480 Mb/s)";
    case 4:  return "SuperSpeed (out of scope!)";
    default: return "unknown speed ID";
    }
}

static int default_speed_class(u32 psiv)
{
    switch (psiv) {
    case 1:  return USB_SPEED_FULL;
    case 2:  return USB_SPEED_LOW;
    case 3:  return USB_SPEED_HIGH;
    case 4:  return USB_SPEED_SUPER;
    default: return USB_SPEED_UNKNOWN;
    }
}

static const char *speed_class_name(int speed_class)
{
    switch (speed_class) {
    case USB_SPEED_LOW:   return "Low-Speed (1.5 Mb/s)";
    case USB_SPEED_FULL:  return "Full-Speed (12 Mb/s)";
    case USB_SPEED_HIGH:  return "High-Speed (480 Mb/s)";
    case USB_SPEED_SUPER: return "SuperSpeed (out of scope!)";
    default:              return "unknown speed";
    }
}

static const PROTOCAP *protocol_for_port(CTRL *c, int port)
{
    int i;

    for (i = 0; i < c->nproto; i++) {
        const PROTOCAP *pr;

        pr = &c->proto[i];
        if (port >= pr->portoff && port < pr->portoff + pr->portcnt)
            return pr;
    }
    return 0;
}

/* Protocol Speed ID dword (spec 7.2.2.1.2): PSIV 3:0, PSIE 5:4
 * (0=b/s 1=Kb/s 2=Mb/s 3=Gb/s), PSIM 31:16. Kb/s keeps Low-Speed's
 * 1.5 Mb/s exact without floating point. */
static u32 psi_kbits(u32 psi)
{
    u32 mant = (psi >> 16) & 0xFFFF;

    switch ((psi >> 4) & 0x3) {
    case 0:  return mant / 1000UL;
    case 1:  return mant;
    case 2:  return mant * 1000UL;
    default: return (mant > 4000UL) ? 0 : mant * 1000000UL;
    }
}

static int speed_class_kbits(u32 kb)
{
    if (kb == 1500UL)
        return USB_SPEED_LOW;
    if (kb == 12000UL)
        return USB_SPEED_FULL;
    if (kb == 480000UL)
        return USB_SPEED_HIGH;
    if (kb >= 5000000UL)
        return USB_SPEED_SUPER;
    return USB_SPEED_UNKNOWN;
}

int speed_class_port(CTRL *c, int port, u32 psiv)
{
    const PROTOCAP *pr;
    int k;

    pr = protocol_for_port(c, port);
    if (pr == 0)
        return USB_SPEED_UNKNOWN;
    if (pr->psic == 0)
        return default_speed_class(psiv);
    for (k = 0; k < (int)pr->npsi; k++) {
        if ((pr->psi[k] & 0xF) == psiv)
            return speed_class_kbits(psi_kbits(pr->psi[k]));
    }
    return USB_SPEED_UNKNOWN;
}

/*
 * A controller that advertises PSIC > 0 defines its own speed IDs, so the
 * default 1=FS/2=LS/3=HS/4=SS mapping does not apply (spec 7.2). Name the
 * speed from the advertised bit rate, and never reinterpret an absent entry
 * as a default ID.
 */
const char *speed_name_port(CTRL *c, int port, u32 psiv)
{
    static char buf[80];
    const PROTOCAP *pr;
    int k;

    pr = protocol_for_port(c, port);
    if (pr == 0) {
        sprintf(buf, "PSIV %lu, no protocol capability for port", psiv);
        return buf;
    }
    if (pr->psic == 0)
        return speed_name(psiv);
    for (k = 0; k < (int)pr->npsi; k++) {
        int speed_class;
        u32 kb;

        if ((pr->psi[k] & 0xF) != psiv)
            continue;
        kb = psi_kbits(pr->psi[k]);
        speed_class = speed_class_kbits(kb);
        if (speed_class != USB_SPEED_UNKNOWN &&
            speed_class == default_speed_class(psiv))
            return speed_class_name(speed_class);
        if (speed_class != USB_SPEED_UNKNOWN) {
            sprintf(buf, "%s; PSIV %lu", speed_class_name(speed_class), psiv);
            return buf;
        }
        sprintf(buf, "PSIV %lu, %lu Kb/s per protocol cap", psiv, kb);
        return buf;
    }
    sprintf(buf, "PSIV %lu not advertised by protocol cap", psiv);
    return buf;
}

int port_is_usb2(CTRL *c, int port)
{
    return c->portclass[port] == PC_USB2_ONLY ||
           c->portclass[port] == PC_USB2_PAIRED;
}

int qual_ports(CTRL *c, int wait_secs)
{
    int port, connected, resets_ok;
    u32 v, ms;

    /* power up managed USB2 ports only; USB3 ports stay unpowered */
    if (c->ppc) {
        for (port = 1; port <= c->maxports; port++) {
            if (port_is_usb2(c, port))
                portsc_write(c, port, PSC_PP);
        }
        msleep(50);   /* >= 20 ms before touching a freshly powered port */
    }
    evt_drain(c, 0, 0);   /* consume the power-on Port Status Change events */

    connected = 0;
    for (port = 1; port <= c->maxports; port++) {
        if (port_is_usb2(c, port) && (portsc_read(c, port) & PSC_CCS))
            connected++;
    }
    if (!connected && wait_secs > 0) {
        qprintf("  C6: no device present. Plug a USB2 device now "
                "(waiting %d s, ESC to skip)...\n", wait_secs);
        while (wait_secs > 0 && !connected) {
            msleep(1000);
            wait_secs--;
            evt_drain(c, 0, 0);   /* keep the ring drained while we wait */
            if (kbhit()) {
                getch();
                break;
            }
            for (port = 1; port <= c->maxports; port++) {
                if (port_is_usb2(c, port) &&
                    (portsc_read(c, port) & PSC_CCS))
                    connected++;
            }
        }
    }

    if (!connected) {
        strcpy(c->port_note, "no USB2 connect observed (no device?)");
        c->v_port = V_SKIP;
        return 0;
    }

    resets_ok = 0;
    for (port = 1; port <= c->maxports; port++) {
        if (!port_is_usb2(c, port))
            continue;
        v = portsc_read(c, port);
        if ((v & PSC_CCS) == 0)
            continue;

        qprintf("  C6: port %d connect (PORTSC=%08lX), resetting...\n",
                port, v);
        report_flush();
        evt_drain(c, 0, 0);             /* clear the connect event first */
        portsc_clear_changes(c, port, PSC_CSC);
        portsc_write(c, port, PSC_PR);
        for (ms = 0; ms < 500; ms++) {
            evt_drain(c, 0, 0);         /* drain reset/status events as they post */
            if (portsc_read(c, port) & PSC_PRC)
                break;
            msleep(1);
        }
        v = portsc_read(c, port);
        if ((v & PSC_PRC) && (v & PSC_PED)) {
            qprintf("  C6: port %d reset ok in %lu ms, %s\n",
                    port, ms, speed_name_port(c, port, PSC_SPEED(v)));
            resets_ok++;
        } else {
            qprintf("  C6: port %d reset FAILED (PORTSC=%08lX)\n", port, v);
        }
        report_flush();
        portsc_clear_changes(c, port, PSC_PRC | PSC_PEC | PSC_CSC);
        evt_drain(c, 0, 0);             /* and the post-reset change events */
    }

    sprintf(c->port_note, "%d connect(s), %d reset(s) ok, %lu port event(s)",
            connected, resets_ok, c->port_events);
    c->v_port = resets_ok ? V_PASS : V_FAIL;
    return resets_ok > 0;
}

/* ------------------------------------------------------------------ */
/* C7 - Intel 7/8-series EHCI-to-xHCI port switchover                 */
/* ------------------------------------------------------------------ */

void qual_intel_ports(CTRL *c, int allow_write)
{
    PCIINFO *p = &c->pci;
    u32 orig;

    if (c->quirk == 0 || (c->quirk->flags & QF_XUSB2PR) == 0) {
        /*
         * Silence here means "not attempted", which must not read as "routing
         * is fine". It is correct for every controller that has no mux - but
         * an Intel xHCI with no quirk row at all could be a PCH generation
         * this table has not learned, so say so for that case only.
         */
        if (c->quirk == 0 && p->vid == 0x8086)
            qprintf("  C7: not attempted - Intel xHCI %04X:%04X is not in the "
                    "quirk table, so whether it has the USB2 mux is unknown. "
                    "Record this ID.\n", p->vid, p->did);
        return;
    }

    p->xusb2pr    = pci_read32(p->bus, p->dev, p->fn, 0xD0);
    p->xusb2prm   = pci_read32(p->bus, p->dev, p->fn, 0xD4);
    p->usb3_pssen = pci_read32(p->bus, p->dev, p->fn, 0xD8);
    p->usb3prm    = pci_read32(p->bus, p->dev, p->fn, 0xDC);

    /* The verdict is pure PCIINFO -> text and lives in mmiodiag.c, where the
     * host runner can reach branches no QEMU case can. A write is warranted
     * only for NOT_ROUTED: routing an UNDETERMINED mask would be writing a
     * value derived from words that just failed to mean anything. */
    if (report_xusb2pr(p) != XUSB2PR_NOT_ROUTED)
        return;

    if (!allow_write) {
        qprintf("  C7: USB2 ports NOT routed to xHCI - a perfect init "
                "will see no connects.\n"
                "      Re-run with --set-intel-ports to claim them "
                "(USB3_PSSEN left unchanged).\n");
        return;
    }
    /* The routing change is deliberately left in place - it is what makes
     * the USB2 ports visible to xHCI - but keep the value found for the
     * report so the machine's as-shipped state stays on record.
     *
     * The read back is re-classified rather than merely printed. A config
     * write that did not take, or a read back of all ones, must not be
     * reported as "left routed to xHCI": that claim is the reason a later
     * C6 SKIP would be read as a port problem instead of a routing one. The
     * same rule the read path above follows - the verdict comes from the
     * words, never from the fact that a write was issued.
     */
    orig = p->xusb2pr;
    pci_write32(p->bus, p->dev, p->fn, 0xD0, p->xusb2prm);
    p->xusb2pr = pci_read32(p->bus, p->dev, p->fn, 0xD0);
    qprintf("  C7: wrote XUSB2PR=XUSB2PRM (was %08lX). Confirming by read "
            "back:\n", orig);

    /*
     * Three outcomes, not two. "The write did not take" is itself a claim
     * about readable words, so it may only be made when the read back is
     * usable; when it is not, whether the write took is unknown, and telling
     * the reader to blame routing for the C6 result below would be the same
     * unsupported attribution in the opposite direction.
     */
    switch (report_xusb2pr(p)) {
    case XUSB2PR_ROUTED:
        qprintf("  C7: routing confirmed by read back and left in place. "
                "Re-scanning ports.\n");
        break;
    case XUSB2PR_NOT_ROUTED:
        qprintf("  C7: the read back still shows switchable USB2 ports on "
                "EHCI, so the write did not take. Ports left on EHCI cannot "
                "report a connect below - that is the routing, not the "
                "port.\n");
        break;
    default:
        qprintf("  C7: the read back is not usable evidence, so whether the "
                "write took is unknown.\n"
                "      Do NOT attribute the C6 result below to routing or to "
                "the ports - and note the cause of the unusable read back is "
                "undetermined too. Record the words and re-run.\n");
        break;
    }
    msleep(100);
}

/* ------------------------------------------------------------------ */
/* teardown - leave the controller the way the BIOS expects it        */
/* ------------------------------------------------------------------ */

void qual_cleanup(CTRL *c)
{
    u32 v, ms;
    int halted, reset_ok, bme_off, release_ok;

    if (!c->mmio_ok)
        return;

    halted = 0;
    reset_ok = 0;

    /* silence the controller before giving the IRQ vector back */
    v = RD32(c->op + XOP_USBCMD);
    WR32(c->op + XOP_USBCMD, v & ~(CMD_RUN | CMD_INTE));
    WR32(c->op + XOP_USBSTS, STS_EINT); /* EINT before IP - see above */
    /* IE off, ack pending - and RsvdP preserved (repo audit D2). */
    WR32(c->rt + XRT_IMAN, (RD32(c->rt + XRT_IMAN) & IMAN_RSVDP) | IMAN_IP);
    irq_uninstall();
    for (ms = 0; ms < 100; ms++) {
        if (RD32(c->op + XOP_USBSTS) & STS_HCH) {
            halted = 1;
            break;
        }
        msleep(1);
    }
    if (halted) {
        v = RD32(c->op + XOP_USBCMD);
        WR32(c->op + XOP_USBCMD, (v & USBCMD_RSVDP) | CMD_HCRST);
        for (ms = 0; ms < 1000; ms++) {
            if ((RD32(c->op + XOP_USBCMD) & CMD_HCRST) == 0 &&
                (RD32(c->op + XOP_USBSTS) & STS_CNR) == 0) {
                reset_ok = 1;
                break;
            }
            msleep(1);
        }
    }

    if (c->legsup_off != 0) {
        /* restore the firmware's enables without re-acking RW1C 31:29 */
        WR32(c->base + c->legsup_off + 4,
             c->legctl_orig & (LEGCTL_RSVDP | LEGCTL_ENABLES));
        v = RD32(c->base + c->legsup_off);
        WR32(c->base + c->legsup_off, v & ~LEGSUP_OS_OWNED);
    }

    /*
     * Never return live DMA memory to DOS until either reset completed or
     * PCI bus mastering is confirmed off. If neither proof is available,
     * retain these selectors so a later controller cannot release/reuse them.
     *
     * All BAR MMIO must precede the PCI configuration write below. It restores
     * the PCI Command register as found (minus BME), which on a machine whose
     * firmware left Memory Space Enable clear switches BAR0 decode off. The
     * mapping and c->mmio_ok deliberately stay valid - report_controller()
     * needs mmio_ok to print Tier B/C - so this call is the MMIO cutoff:
     * report from cached facts only after it. If the write clears MSE, a later
     * BAR read may return all ones; that is invalid data, not proof that the
     * controller died.
     */
    bme_off = pci_disable_bus_master(&c->pci);
    release_ok = reset_ok || bme_off;
    if (release_ok) {
        dos_free_all();
        c->dcbaa = 0;
        c->erst = 0;
        c->sparray = 0;
        c->cmd = 0;
        c->evt = 0;
    } else {
        dos_protect_all();
    }

    if (reset_ok) {
        pci_write16(c->pci.bus, c->pci.dev, c->pci.fn, 0x04,
                    c->pci.cmd_orig);
        return;
    }

    c->cleanup_failed = 1;
    if (!halted && bme_off)
        strcpy(c->cleanup_note,
               "controller did not halt; BME disabled before DMA release");
    else if (!halted)
        strcpy(c->cleanup_note,
               "controller did not halt; BME stuck, DMA memory retained");
    else if (bme_off)
        strcpy(c->cleanup_note,
               "cleanup reset timed out; BME disabled before DMA release");
    else
        strcpy(c->cleanup_note,
               "cleanup reset timed out; BME stuck, DMA memory retained");
    qprintf("  Cleanup: FAIL  %s\n", c->cleanup_note);
    report_flush();
}
