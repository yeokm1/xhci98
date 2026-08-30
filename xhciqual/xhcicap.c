/*
 * xhcicap.c - BAR0 mapping and Tier B capability introspection.
 *
 * DPMI 0x0800 maps the (high) BAR0 physical range into linear space; under
 * the extender's zero-based flat model the returned linear address is directly
 * usable as a pointer. DPMI 0x0100 allocates conventional memory, which is
 * identity-mapped under a clean boot, so linear == physical for DMA buffers
 * (see docs/contributing/design/01-hardware-qualification-tool.md section 4).
 */

#include <i86.h>
#include <string.h>
#include "qual.h"

#define BAR_MAP_SIZE 0x10000UL   /* 64 KB covers cap/op/runtime/doorbells */

void *dpmi_map_phys(u32 phys, u32 size)
{
    union REGS r;

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0800;
    r.w.bx = (u16)(phys >> 16);
    r.w.cx = (u16)(phys & 0xFFFF);
    r.w.si = (u16)(size >> 16);
    r.w.di = (u16)(size & 0xFFFF);
    int386(0x31, &r, &r);
    if (r.w.cflag)
        return 0;
    return (void *)(((u32)r.w.bx << 16) | r.w.cx);
}

/* DPMI 0x0801. Best effort: extenders may not implement it, and a refusal
 * only leaks linear address space for the rest of the run. */
void dpmi_unmap_phys(void *linear)
{
    union REGS r;

    if (linear == 0)
        return;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0801;
    r.w.bx = (u16)((u32)linear >> 16);
    r.w.cx = (u16)((u32)linear & 0xFFFF);
    int386(0x31, &r, &r);
}

/*
 * Conventional-memory blocks are tracked so they can be given back: one
 * controller's rings, scratchpad pages and C8 buffers can run to several
 * hundred KB, and without a release the next controller in the same run
 * would fail to allocate and be reported as a DMA failure.
 */
#define MAX_DOS_BLOCKS 24
static u16 dos_sel[MAX_DOS_BLOCKS];
static int dos_nblocks;
static int dos_nprotected;

static void dos_free_sel(u16 sel)
{
    union REGS r;

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0101;
    r.w.dx = sel;
    int386(0x31, &r, &r);
}

void *dos_alloc(u32 bytes, u32 *phys)
{
    union REGS r;

    if (dos_nblocks >= MAX_DOS_BLOCKS)
        return 0;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0100;
    r.w.bx = (u16)((bytes + 15) >> 4);
    int386(0x31, &r, &r);
    if (r.w.cflag)
        return 0;
    dos_sel[dos_nblocks++] = r.w.dx;
    *phys = (u32)r.w.ax << 4;
    return (void *)*phys;   /* flat model: linear == physical below 1 MB */
}

void dos_free_last(void)
{
    if (dos_nblocks > dos_nprotected)
        dos_free_sel(dos_sel[--dos_nblocks]);
}

void dos_free_all(void)
{
    while (dos_nblocks > dos_nprotected)
        dos_free_sel(dos_sel[--dos_nblocks]);
}

void dos_protect_all(void)
{
    dos_nprotected = dos_nblocks;
}

int xhci_map(CTRL *c)
{
    u32 dw, dboff_raw, rtsoff_raw, doorbell_bytes;

    c->mmio_ok = 0;
    /* Bit 0 of a BAR selects the space: `1` is I/O, not memory. `bar_phys`
     * masks the low nibble off either way, so an I/O BAR of 0x0000E001 would be
     * mapped as if 0xE000 were a physical memory address - which is real memory
     * on this machine and belongs to somebody else. The legacy path has always
     * refused it; this one did not (repo audit D6). No shipping xHCI puts BAR0
     * in I/O space - 5.2.1 requires a memory BAR - so this is a refusal for a
     * controller that is misdescribing itself, and a refusal is the right answer
     * to that. */
    if (c->pci.bar_hi != 0 || c->pci.bar_phys == 0 ||
        (c->pci.bar_lo & 1) != 0)
        return 0;                       /* above 4 GB, unassigned, or I/O space */

    c->base = (volatile u8 *)dpmi_map_phys(c->pci.bar_phys, BAR_MAP_SIZE);
    if (c->base == 0)
        return 0;

    /* sanity before any side-effecting access (init step 0) */
    dw = RD32(c->base + XCAP_CAPLENGTH);
    if (dw == 0xFFFFFFFFUL)
        goto unmap;                     /* not decoding (MSE off? D3?) */
    c->caplength  = (u8)(dw & 0xFF);
    c->hciversion = (u16)(dw >> 16);
    if (c->caplength == 0 || c->hciversion < 0x0090)
        goto unmap;

    c->hcs1 = RD32(c->base + XCAP_HCSPARAMS1);
    c->hcs2 = RD32(c->base + XCAP_HCSPARAMS2);
    c->hcs3 = RD32(c->base + XCAP_HCSPARAMS3);
    c->hcc1 = RD32(c->base + XCAP_HCCPARAMS1);
    dboff_raw = RD32(c->base + XCAP_DBOFF);
    rtsoff_raw = RD32(c->base + XCAP_RTSOFF);
    if (c->hcs1 == 0xFFFFFFFFUL || c->hcs2 == 0xFFFFFFFFUL ||
        c->hcs3 == 0xFFFFFFFFUL || c->hcc1 == 0xFFFFFFFFUL ||
        dboff_raw == 0xFFFFFFFFUL || rtsoff_raw == 0xFFFFFFFFUL)
        goto unmap;
    c->dboff  = dboff_raw & 0xFFFFFFFCUL;
    c->rtsoff = rtsoff_raw & 0xFFFFFFE0UL;
    doorbell_bytes = ((c->hcs1 & 0xFF) + 1) * 4;

    /* Everything this tool touches must live inside the mapped window:
     * operational block (PORTSC for MAX_PORTS ports), runtime, doorbells. */
    if ((u32)c->caplength + XOP_PORTSC(MAX_PORTS) + 0x10UL > BAR_MAP_SIZE ||
        c->rtsoff == 0 || c->rtsoff > BAR_MAP_SIZE - 0x40UL ||
        c->dboff == 0 || c->dboff > BAR_MAP_SIZE - doorbell_bytes)
        goto unmap;

    c->op = c->base + c->caplength;
    c->rt = c->base + c->rtsoff;
    c->db = c->base + c->dboff;
    c->mmio_ok = 1;
    return 1;

unmap:
    dpmi_unmap_phys((void *)c->base);
    c->base = 0;
    return 0;
}

int xhci_read_caps(CTRL *c)
{
    u32 off, dw;
    int guard, sp_hi, sp_lo;

    if (!c->mmio_ok)
        return 0;

    c->maxslots = (int)(c->hcs1 & 0xFF);
    c->maxintrs = (int)((c->hcs1 >> 8) & 0x7FF);
    c->maxports = (int)((c->hcs1 >> 24) & 0xFF);
    /* portclass[] and every port loop are sized MAX_PORTS; the register
     * field allows 255. Clamp loudly rather than run off the array. */
    if (c->maxports > MAX_PORTS) {
        qprintf("  NOTE: controller reports %d ports; only the first %d "
                "are examined\n", c->maxports, MAX_PORTS);
        c->maxports = MAX_PORTS;
    }

    /* Max Scratchpad Buffers: bits 25:21 are the HIGH 5 bits (B4) */
    sp_hi = (int)((c->hcs2 >> 21) & 0x1F);
    sp_lo = (int)((c->hcs2 >> 27) & 0x1F);
    c->spbufs = (sp_hi << 5) | sp_lo;

    c->ac64 = (int)(c->hcc1 & 1);
    c->csz  = (c->hcc1 & 0x4) ? 64 : 32;
    c->ppc  = (c->hcc1 & 0x8) ? 1 : 0;

    /*
     * HCCPARAMS2, and the gate is reach rather than version.
     *
     * CAPLENGTH is the controller's own statement of where its capability block
     * ends: one that stops at 1Ch has the operational registers there, so there
     * is no HCCPARAMS2 to read and a read would report USBCMD as capability
     * bits. All-ones is refused for the reason every other read here refuses it
     * - it is what an undecoding window answers, and FSC is a 1 in it, which is
     * the direction of this decision that loses data.
     *
     * It is deliberately NOT gated on HCIVERSION >= 1.10. Appendix H.1 lists the
     * capabilities "that were optional for xHCI 1.0 implementations [and] are
     * now required in xHCI 1.1 implementations" and H.1.6 (p.593) is FSC, so a
     * 1.0 controller answers this bit honestly and may answer 1. A version gate
     * would print 0 for hardware that had just said otherwise - which is the
     * inference this print exists to replace.
     */
    c->hcc2_ok = 0;
    c->hcc2 = 0;
    c->u3c = 0;
    c->cmc = 0;
    c->fsc = 0;
    if ((u32)c->caplength >= XCAP_HCCPARAMS2_END) {
        u32 hcc2 = RD32(c->base + XCAP_HCCPARAMS2);

        if (hcc2 != 0xFFFFFFFFUL) {
            c->hcc2 = hcc2;
            c->hcc2_ok = 1;
            /* Bits 0-2 only: those are what
             * docs/usb-xhci-info/xhci-data-structures.md transcribes, and this
             * project takes bit positions from there and not from memory. The
             * raw word is printed beside them so an undecoded bit is still on
             * the sheet. */
            c->u3c = (int)(hcc2 & 0x1UL);
            c->cmc = (int)((hcc2 >> 1) & 0x1UL);
            c->fsc = (int)((hcc2 >> 2) & 0x1UL);
        }
    }

    c->pagesize = RD32(c->op + XOP_PAGESIZE);

    /* extended capability walk (B6, B7) */
    c->legsup_off = 0;
    c->nproto = 0;
    off = ((c->hcc1 >> 16) & 0xFFFF) << 2;
    for (guard = 0; off != 0 && off < BAR_MAP_SIZE - 0x40UL && guard < 64;
         guard++) {
        u32 next;

        dw = RD32(c->base + off);
        switch (dw & 0xFF) {
        case XECP_ID_LEGSUP:
            if (c->legsup_off == 0)
                c->legsup_off = off;
            break;
        case XECP_ID_PROTO:
            if (c->nproto < MAX_PROTO) {
                PROTOCAP *pr = &c->proto[c->nproto];
                u32 dw2 = RD32(c->base + off + 0x08);
                int k;

                pr->major   = (u8)(dw >> 24);
                pr->minor   = (u8)(dw >> 16);
                pr->portoff = (u8)(dw2 & 0xFF);
                pr->portcnt = (u8)((dw2 >> 8) & 0xFF);
                pr->psic    = (u8)((dw2 >> 28) & 0xF);
                pr->slottype = (u8)(RD32(c->base + off + 0x0C) & 0x1F);
                /* PSIC > 0 means this cap redefines the speed IDs; the
                 * default 1=FS/2=LS/3=HS/4=SS mapping does not apply
                 * unless the table says so (spec 7.2, 7.2.2.1.2). */
                pr->npsi = 0;
                for (k = 0; k < (int)pr->psic && k < MAX_PSI; k++) {
                    if (off + 0x10UL + (u32)k * 4 >= BAR_MAP_SIZE)
                        break;
                    pr->psi[k] = RD32(c->base + off + 0x10 + k * 4);
                    pr->npsi++;
                }
                c->nproto++;
            }
            break;
        case 10:
            c->saw_debug_cap = 1;
            break;
        default:
            break;
        }
        next = (dw >> 8) & 0xFF;
        if (next == 0)
            break;
        off += next << 2;
    }
    /* A walk that ran off the mapped window (or past the 64-entry guard)
     * may have missed USBLEGSUP, and "no USBLEGSUP" then passes C1 with no
     * handoff performed. Say so rather than end silently. */
    if (off >= BAR_MAP_SIZE - 0x40UL || guard >= 64) {
        qprintf("  NOTE: extended capability list not walked to its end "
                "(next at %08lX); a USBLEGSUP beyond it was not seen\n",
                (unsigned long)off);
    }

    xhci_classify_ports(c);
    return 1;
}

/*
 * Companion-port pairing convention (docs/usb-xhci-info/xhci-programming.md, "Port
 * Topology Classification"): with one USB2 range of P ports and one USB3
 * range of Q ports (P >= Q), the last Q USB2 ports pair with the USB3
 * ports in order; the first P-Q USB2 ports are USB2-only connectors.
 */
void xhci_classify_ports(CTRL *c)
{
    int i, k;
    const PROTOCAP *u2 = 0, *u3 = 0;

    memset(c->portclass, PC_NONE, sizeof(c->portclass));
    c->usb2_ports = 0;

    for (i = 0; i < c->nproto; i++) {
        const PROTOCAP *pr = &c->proto[i];
        int cls = (pr->major == 3) ? PC_USB3_ORPHAN : PC_USB2_ONLY;

        for (k = 0; k < pr->portcnt; k++) {
            int port = pr->portoff + k;
            if (port >= 1 && port <= MAX_PORTS)
                c->portclass[port] = (u8)cls;
        }
        if (pr->major == 2 && u2 == 0)
            u2 = pr;
        if (pr->major == 3 && u3 == 0)
            u3 = pr;
    }

    if (u2 != 0 && u3 != 0 && u2->portcnt >= u3->portcnt) {
        for (k = 0; k < u3->portcnt; k++) {
            int p2 = u2->portoff + u2->portcnt - u3->portcnt + k;
            int p3 = u3->portoff + k;
            if (p2 >= 1 && p2 <= MAX_PORTS)
                c->portclass[p2] = PC_USB2_PAIRED;
            if (p3 >= 1 && p3 <= MAX_PORTS)
                c->portclass[p3] = PC_USB3_PAIRED;
        }
    }

    for (i = 1; i <= MAX_PORTS; i++) {
        if (c->portclass[i] == PC_USB2_ONLY ||
            c->portclass[i] == PC_USB2_PAIRED)
            c->usb2_ports++;
    }
}
