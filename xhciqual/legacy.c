/*
 * legacy.c - EHCI and OHCI hardware qualification.
 *
 * These controllers use different schedule formats, but the qualification
 * contract remains ownership, reset, DMA, legacy INTx, and root-port reset.
 */

#include <conio.h>
#include <stdio.h>
#include <string.h>
#include "qual.h"

#define LEGACY_MAP_SIZE 0x1000UL

#define ECAP_CAPLENGTH       0x00
#define ECAP_HCIVERSION      0x02
#define ECAP_HCSPARAMS       0x04
#define ECAP_HCCPARAMS       0x08
#define EOP_USBCMD           0x00
#define EOP_USBSTS           0x04
#define EOP_USBINTR          0x08
#define EOP_CTRLDSSEGMENT    0x10
#define EOP_ASYNCLISTADDR    0x18
#define EOP_CONFIGFLAG       0x40
#define EOP_PORTSC(n)        (0x44 + 4 * ((n) - 1))
#define ECMD_RUN             0x00000001UL
#define ECMD_RESET           0x00000002UL
#define ECMD_ASE             0x00000020UL
#define ECMD_IAAD            0x00000040UL
#define ESTS_IAA             0x00000020UL
#define ESTS_HCH             0x00001000UL
#define ESTS_ASS             0x00008000UL
#define EPORT_CCS            0x00000001UL
#define EPORT_CSC            0x00000002UL
#define EPORT_PED            0x00000004UL
#define EPORT_PEC            0x00000008UL
#define EPORT_OCC            0x00000020UL
#define EPORT_PR             0x00000100UL
#define EPORT_PP             0x00001000UL
#define EPORT_OWNER          0x00002000UL
#define EPORT_CHANGE         (EPORT_CSC | EPORT_PEC | EPORT_OCC)
#define EQTD_ACTIVE          0x00000080UL

#define OREG_REVISION        0x00
#define OREG_CONTROL         0x04
#define OREG_COMMAND_STATUS  0x08
#define OREG_INTR_STATUS     0x0C
#define OREG_INTR_ENABLE     0x10
#define OREG_INTR_DISABLE    0x14
#define OREG_HCCA            0x18
#define OREG_FM_INTERVAL     0x34
#define OREG_PERIODIC_START  0x40
#define OREG_LS_THRESHOLD    0x44
#define OREG_RH_DESC_A       0x48
#define OREG_RH_STATUS       0x50
#define OREG_RH_PORT(n)      (0x54 + 4 * ((n) - 1))
#define OCTL_IR              0x00000100UL
#define OCTL_HCFS_MASK       0x000000C0UL
#define OCTL_HCFS_OPERATIONAL 0x00000080UL
#define OCMD_HCR             0x00000001UL
#define OCMD_OCR             0x00000008UL
#define OINT_SF              0x00000004UL
#define OINT_MIE             0x80000000UL
#define OPORT_CCS            0x00000001UL
#define OPORT_PES            0x00000002UL
#define OPORT_PRS            0x00000010UL
#define OPORT_PPS            0x00000100UL
#define OPORT_LSDA           0x00000200UL
#define OPORT_CHANGE         0x001F0000UL
#define OPORT_PRSC           0x00100000UL

static int pci_irq_usable(LEGACY_CTRL *c)
{
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
    /* The PIC cascade, refused with its own sentence - see the same check in
     * `qual_irq` for why `irq_install`'s generic refusal is not good enough
     * (repo audit D6). */
    if (c->pci.iline == 2) {
        strcpy(c->irq_note,
               "Interrupt Line 2 is the PIC cascade, not a usable IRQ");
        c->v_irq = V_FAIL;
        return 0;
    }
    if (pci_read16(c->pci.bus, c->pci.dev, c->pci.fn, 0x04) &
        PCI_CMD_INTX_OFF) {
        strcpy(c->irq_note, "PCI Interrupt Disable bit stuck set");
        c->v_irq = V_FAIL;
        return 0;
    }
    return 1;
}

static int pci_intx_status_supported(LEGACY_CTRL *c)
{
    u16 cmd;
    u16 probe;

    if (c->pci.has_pcie)
        return 1;   /* PCIe uses the PCI 2.3 command/status header */
    cmd = pci_read16(c->pci.bus, c->pci.dev, c->pci.fn, 0x04);
    pci_write16(c->pci.bus, c->pci.dev, c->pci.fn, 0x04,
                cmd | PCI_CMD_INTX_OFF);
    probe = pci_read16(c->pci.bus, c->pci.dev, c->pci.fn, 0x04);
    pci_write16(c->pci.bus, c->pci.dev, c->pci.fn, 0x04, cmd);
    return (probe & PCI_CMD_INTX_OFF) ? 1 : 0;
}

int legacy_map_and_read(LEGACY_CTRL *c)
{
    u32 dw;

    c->mmio_ok = 0;
    if (c->pci.bar_hi != 0 || c->pci.bar_phys == 0 ||
        (c->pci.bar_lo & 1) != 0)
        return 0;
    c->base = (volatile u8 *)dpmi_map_phys(c->pci.bar_phys,
                                           LEGACY_MAP_SIZE);
    if (c->base == 0)
        return 0;

    if (c->pci.hctype == HC_EHCI) {
        dw = RD32(c->base + ECAP_CAPLENGTH);
        if (dw == 0xFFFFFFFFUL || (dw & 0xFF) < 0x10)
            goto unmap;
        c->hciversion = (u16)(dw >> 16);
        if (c->hciversion < 0x0095)
            goto unmap;
        c->op = c->base + (u8)(dw & 0xFF);
        c->cap_a = RD32(c->base + ECAP_HCSPARAMS);
        c->cap_b = RD32(c->base + ECAP_HCCPARAMS);
        c->maxports = (int)(c->cap_a & 0xF);
        c->ppc = (c->cap_a & 0x10) ? 1 : 0;
        c->legsup_off = (c->cap_b >> 8) & 0xFF;
    } else {
        dw = RD32(c->base + OREG_REVISION);
        /* accept any OHCI 1.x revision (0x10 typical, 0x11 exists) */
        if (dw == 0xFFFFFFFFUL || (dw & 0xF0) != 0x10)
            goto unmap;
        c->hciversion = (u16)(dw & 0xFF);
        c->op = c->base;
        c->cap_a = RD32(c->base + OREG_RH_DESC_A);
        c->cap_b = RD32(c->base + 0x4C);
        c->maxports = (int)(c->cap_a & 0xFF);
        c->ppc = (c->cap_a & 0x200) ? 0 : 1;
    }
    if (c->maxports > MAX_PORTS)
        c->maxports = MAX_PORTS;
    c->mmio_ok = 1;
    return 1;

unmap:
    /* the window is dead to us - give the linear range back */
    dpmi_unmap_phys((void *)c->base);
    c->base = 0;
    return 0;
}

static int ehci_handoff(LEGACY_CTRL *c)
{
    u8 off;
    u32 v;
    int waited;

    off = (u8)c->legsup_off;
    if (off == 0 || off > 0xF8 || pci_read8(c->pci.bus, c->pci.dev,
                                             c->pci.fn, off) != 1) {
        strcpy(c->handoff_note, "no EHCI legacy cap - nothing to hand off");
        c->v_handoff = V_PASS;
        c->legsup_off = 0;
        return 1;
    }
    c->legsup_orig = pci_read32(c->pci.bus, c->pci.dev, c->pci.fn, off);
    c->legctl_orig = pci_read32(c->pci.bus, c->pci.dev, c->pci.fn,
                                (u8)(off + 4));
    pci_write32(c->pci.bus, c->pci.dev, c->pci.fn, off,
                c->legsup_orig | 0x01000000UL);
    v = c->legsup_orig;
    for (waited = 0; waited < 1000; waited += 10) {
        v = pci_read32(c->pci.bus, c->pci.dev, c->pci.fn, off);
        if ((v & 0x00010000UL) == 0)
            break;
        msleep(10);
    }
    pci_write32(c->pci.bus, c->pci.dev, c->pci.fn, (u8)(off + 4),
                0xFFFF0000UL);
    if (v & 0x00010000UL) {
        sprintf(c->handoff_note,
                "BIOS-Owned did not clear in 1 s (LEGSUP=%08lX)", v);
        c->v_handoff = V_WARN;
        return 0;
    }
    sprintf(c->handoff_note, "BIOS released in <= %d ms", waited);
    c->v_handoff = V_PASS;
    return 1;
}

static int ohci_handoff(LEGACY_CTRL *c)
{
    u32 v;
    int waited;

    v = RD32(c->base + OREG_CONTROL);
    if ((v & OCTL_IR) == 0) {
        strcpy(c->handoff_note, "InterruptRouting already OS-owned");
        c->v_handoff = V_PASS;
        return 1;
    }
    WR32(c->base + OREG_COMMAND_STATUS, OCMD_OCR);
    for (waited = 0; waited < 1000; waited += 10) {
        v = RD32(c->base + OREG_CONTROL);
        if ((v & OCTL_IR) == 0)
            break;
        msleep(10);
    }
    if (v & OCTL_IR) {
        strcpy(c->handoff_note, "SMI ownership did not clear in 1 s");
        c->v_handoff = V_WARN;
        return 0;
    }
    sprintf(c->handoff_note, "firmware released in <= %d ms", waited);
    c->v_handoff = V_PASS;
    return 1;
}

static int ehci_reset(LEGACY_CTRL *c)
{
    u32 v;
    u32 ms;

    v = RD32(c->op + EOP_USBCMD);
    WR32(c->op + EOP_USBCMD, v & ~ECMD_RUN);
    for (ms = 0; ms < 100; ms++) {
        if (RD32(c->op + EOP_USBSTS) & ESTS_HCH)
            break;
        msleep(1);
    }
    if ((RD32(c->op + EOP_USBSTS) & ESTS_HCH) == 0) {
        c->v_reset = V_FAIL;
        return 0;
    }
    WR32(c->op + EOP_USBCMD, ECMD_RESET);
    for (ms = 0; ms < 1000; ms++) {
        if ((RD32(c->op + EOP_USBCMD) & ECMD_RESET) == 0)
            break;
        msleep(1);
    }
    c->reset_ms = ms;
    c->v_reset = (ms < 1000) ? V_PASS : V_FAIL;
    return c->v_reset == V_PASS;
}

static int ohci_reset(LEGACY_CTRL *c)
{
    u32 v;
    u32 ms;

    WR32(c->base + OREG_INTR_DISABLE, 0xFFFFFFFFUL);
    v = RD32(c->base + OREG_CONTROL);
    WR32(c->base + OREG_CONTROL, v & ~OCTL_HCFS_MASK);
    msleep(10);
    WR32(c->base + OREG_COMMAND_STATUS, OCMD_HCR);
    for (ms = 0; ms < 100; ms++) {
        if ((RD32(c->base + OREG_COMMAND_STATUS) & OCMD_HCR) == 0)
            break;
        msleep(1);
    }
    c->reset_ms = ms;
    c->v_reset = (ms < 100) ? V_PASS : V_FAIL;
    return c->v_reset == V_PASS;
}

static int ehci_dma(LEGACY_CTRL *c)
{
    volatile u32 *qh;
    u32 cmd;
    u32 ms;
    int touched;

    c->dma_a = dma_alloc(96, 32, 4096, &c->dma_a_phys);
    if (!c->dma_a) {
        strcpy(c->dma_note, "could not allocate asynchronous QH");
        c->v_dma = V_FAIL;
        return 0;
    }
    qh = (volatile u32 *)c->dma_a;
    qh[0] = c->dma_a_phys | 0x2;
    qh[1] = 127UL | (2UL << 12) | (1UL << 14) | (1UL << 15) |
            (64UL << 16) | (15UL << 28);
    qh[2] = 1UL << 30;
    qh[4] = 1;
    qh[5] = 1;
    qh[6] = 0x40UL;

    WR32(c->op + EOP_CTRLDSSEGMENT, 0);
    WR32(c->op + EOP_ASYNCLISTADDR, c->dma_a_phys);
    WR32(c->op + EOP_USBSTS, 0x3FUL);
    cmd = RD32(c->op + EOP_USBCMD);
    WR32(c->op + EOP_USBCMD, cmd | ECMD_RUN | ECMD_ASE);
    touched = 0;
    for (ms = 0; ms < 2000; ms++) {
        if (RD32(c->op + EOP_USBSTS) & ESTS_ASS)
            touched = 1;
        if (touched)
            break;
        msleep(1);
    }
    c->dma_ms = ms;
    if (!touched) {
        strcpy(c->dma_note, "asynchronous schedule never became active");
        c->v_dma = V_FAIL;
        return 0;
    }
    WR32(c->op + EOP_USBSTS, ESTS_IAA);
    cmd = RD32(c->op + EOP_USBCMD);
    WR32(c->op + EOP_USBCMD, cmd | ECMD_IAAD);
    touched = 0;
    for (ms = 0; ms < 1000; ms++) {
        if (RD32(c->op + EOP_USBSTS) & ESTS_IAA) {
            touched = 1;
            break;
        }
        msleep(1);
    }
    c->dma_ms += ms;
    if (!touched) {
        strcpy(c->dma_note, "halted QH never reached async advance");
        c->v_dma = V_FAIL;
        return 0;
    }
    WR32(c->op + EOP_USBSTS, ESTS_IAA);
    sprintf(c->dma_note, "halted QH DMA-read/IAA proof in %lu ms",
            c->dma_ms);
    c->v_dma = V_PASS;
    return 1;
}

static int ohci_dma(LEGACY_CTRL *c)
{
    volatile u16 *frame;
    u16 first;
    u32 control, interval;
    u32 ms;

    c->dma_a = dma_alloc(256, 256, 4096, &c->dma_a_phys);
    if (!c->dma_a) {
        strcpy(c->dma_note, "could not allocate HCCA");
        c->v_dma = V_FAIL;
        return 0;
    }
    frame = (volatile u16 *)((u8 *)c->dma_a + 0x80);
    first = *frame;
    WR32(c->base + OREG_HCCA, c->dma_a_phys);
    interval = RD32(c->base + OREG_FM_INTERVAL);
    interval = 0x27782EDFUL |
               ((interval ^ 0x80000000UL) & 0x80000000UL);
    WR32(c->base + OREG_FM_INTERVAL, interval);
    WR32(c->base + OREG_PERIODIC_START, 0x00002A2FUL);
    WR32(c->base + OREG_LS_THRESHOLD, 0x00000628UL);
    control = RD32(c->base + OREG_CONTROL) & ~OCTL_HCFS_MASK;
    WR32(c->base + OREG_CONTROL, control | OCTL_HCFS_OPERATIONAL);
    for (ms = 0; ms < 100; ms++) {
        if (*frame != first)
            break;
        msleep(1);
    }
    c->dma_ms = ms;
    if (*frame == first) {
        strcpy(c->dma_note, "HCCA frame number never changed");
        c->v_dma = V_FAIL;
        return 0;
    }
    sprintf(c->dma_note, "HCCA frame writeback %u -> %u in %lu ms",
            first, *frame, ms);
    c->v_dma = V_PASS;
    return 1;
}

static int ehci_irq(LEGACY_CTRL *c)
{
    u32 before;
    u32 ms;
    int polled;
    int teardown_ok;
    int setup_ok;

    if (!pci_irq_usable(c))
        return 0;
    if (!irq_install(&c->pci, HC_EHCI, c->op, 0)) {
        sprintf(c->irq_note, "DPMI %s failed (AX=%04X)",
                irq_error_step() ? irq_error_step() : "IRQ install",
                irq_error_code());
        c->v_irq = V_FAIL;
        return 0;
    }
    WR32(c->op + EOP_USBSTS, ESTS_IAA);
    WR32(c->op + EOP_USBINTR, ESTS_IAA);
    before = irq_count;
    polled = 0;
    setup_ok = 1;
    WR32(c->op + EOP_USBCMD,
         RD32(c->op + EOP_USBCMD) | ECMD_IAAD);
    for (ms = 0; ms < 1000; ms++) {
        if (RD32(c->op + EOP_USBSTS) & ESTS_IAA) {
            polled = 1;
            break;
        }
        msleep(1);
    }
    if (polled && !irq_unmask()) {
        sprintf(c->irq_note, "%s failed (AX=%04X)",
                irq_error_step() ? irq_error_step() : "IRQ unmask",
                irq_error_code());
        c->v_irq = V_FAIL;
        setup_ok = 0;
    } else if (polled) {
        for (ms = 0; ms < 1000; ms++) {
            if (irq_count != before)
                break;
            msleep(1);
        }
    }
    WR32(c->op + EOP_USBINTR, 0);
    WR32(c->op + EOP_USBSTS, ESTS_IAA);
    teardown_ok = irq_uninstall();
    c->irq_isr_count = irq_count;
    c->irq_foreign_count = irq_foreign;
    if (!teardown_ok) {
        /* Keep an earlier unmask-failure note; only describe the teardown
         * fault when the ISR test itself had not already failed. */
        if (c->v_irq != V_FAIL)
            sprintf(c->irq_note, "DPMI %s failed (AX=%04X)",
                    irq_error_step() ? irq_error_step() : "IRQ teardown",
                    irq_error_code());
        c->v_irq = V_FAIL;
        return 0;
    }
    if (!setup_ok)
        return 0;
    if (irq_count != before && c->irq_foreign_count == 0) {
        sprintf(c->irq_note, "one-shot ISR fired on IRQ %u (%s per ELCR)",
                c->pci.iline,
                irq_elcr_level(c->pci.iline) ? "level" : "edge");
        c->v_irq = V_PASS;
        return 1;
    }
    if (c->irq_foreign_count != 0) {
        sprintf(c->irq_note,
                "IRQ %u entered without owned EHCI status; line masked",
                c->pci.iline);
    } else {
        strcpy(c->irq_note, polled ?
               "IAA pending while PIC masked but ISR never fired" :
               "IAA did not become pending while PIC masked");
    }
    c->v_irq = V_FAIL;
    return 0;
}

static int ohci_irq(LEGACY_CTRL *c)
{
    u32 ms;
    int polled;
    int intx;
    int pci23;

    if (!pci_irq_usable(c))
        return 0;
    pci23 = pci_intx_status_supported(c);
    WR32(c->base + OREG_INTR_STATUS, OINT_SF);
    polled = 0;
    intx = 0;
    WR32(c->base + OREG_INTR_ENABLE, OINT_MIE | OINT_SF);
    for (ms = 0; ms < 1000; ms++) {
        if (RD32(c->base + OREG_INTR_STATUS) & OINT_SF)
            polled = 1;
        if (pci_read16(c->pci.bus, c->pci.dev, c->pci.fn, 0x06) & 8)
            intx = 1;
        if (polled && intx)
            break;
        msleep(1);
    }
    WR32(c->base + OREG_INTR_DISABLE, OINT_MIE | OINT_SF);
    WR32(c->base + OREG_INTR_STATUS, OINT_SF);
    if (polled && intx) {
        sprintf(c->irq_note,
                "SOF and PCI INTx asserted on IRQ %u; ISR hook skipped",
                c->pci.iline);
        c->v_irq = V_WARN;
        return 1;
    }
    if (polled) {
        if (pci23) {
            strcpy(c->irq_note,
                   "SOF set but PCI INTx did not assert (PCI 2.3)");
            c->v_irq = V_FAIL;
            return 0;
        }
        /* The paired PCI 2.3 Interrupt Disable bit did not read back, so
         * Status bit 3 cannot be used to judge this older interface. */
        sprintf(c->irq_note,
                "SOF asserted on IRQ %u; PCI 2.3 INTx status unsupported",
                c->pci.iline);
        c->v_irq = V_WARN;
        return 1;
    }
    strcpy(c->irq_note, "no polled SOF interrupt status");
    c->v_irq = V_FAIL;
    return 0;
}

static u32 ehci_port_read(LEGACY_CTRL *c, int port)
{
    return RD32(c->op + EOP_PORTSC(port));
}

static void ehci_port_write(LEGACY_CTRL *c, int port, u32 setbits)
{
    u32 v;

    v = ehci_port_read(c, port);
    v &= ~(EPORT_PED | EPORT_PR | EPORT_CHANGE);
    WR32(c->op + EOP_PORTSC(port), v | setbits);
}

static int ehci_ports(LEGACY_CTRL *c, int wait_secs)
{
    int port, connected, resets_ok;
    u32 v, ms;

    if (c->ppc) {
        for (port = 1; port <= c->maxports; port++)
            ehci_port_write(c, port, EPORT_PP);
        msleep(50);
    }
    WR32(c->op + EOP_CONFIGFLAG, 1);
    connected = 0;
    for (port = 1; port <= c->maxports; port++) {
        v = ehci_port_read(c, port);
        if ((v & EPORT_CCS) && (v & EPORT_OWNER) == 0)
            connected++;
    }
    if (!connected && wait_secs > 0) {
        qprintf("  C6: no EHCI device present. Plug a High-Speed device "
                "now (waiting %d s, ESC to skip)...\n", wait_secs);
        while (wait_secs > 0 && !connected) {
            msleep(1000);
            wait_secs--;
            if (kbhit()) {
                getch();
                break;
            }
            for (port = 1; port <= c->maxports; port++) {
                v = ehci_port_read(c, port);
                if ((v & EPORT_CCS) && (v & EPORT_OWNER) == 0)
                    connected++;
            }
        }
    }
    if (!connected) {
        strcpy(c->port_note, "no EHCI-owned connect observed");
        c->v_port = V_SKIP;
        return 0;
    }
    resets_ok = 0;
    for (port = 1; port <= c->maxports; port++) {
        v = ehci_port_read(c, port);
        if ((v & EPORT_CCS) == 0 || (v & EPORT_OWNER))
            continue;
        ehci_port_write(c, port, EPORT_PR);
        msleep(50);
        ehci_port_write(c, port, 0);
        for (ms = 0; ms < 100; ms++) {
            v = ehci_port_read(c, port);
            if ((v & EPORT_PR) == 0)
                break;
            msleep(1);
        }
        v = ehci_port_read(c, port);
        if (v & EPORT_PED) {
            qprintf("  C6: EHCI port %d reset ok, High-Speed\n", port);
            resets_ok++;
        } else {
            qprintf("  C6: EHCI port %d did not enable (FS/LS companion "
                    "device or reset failure, PORTSC=%08lX)\n", port, v);
        }
    }
    sprintf(c->port_note, "%d connect(s), %d High-Speed reset(s) ok",
            connected, resets_ok);
    c->v_port = resets_ok ? V_PASS : V_WARN;
    return resets_ok > 0;
}

static int ohci_ports(LEGACY_CTRL *c, int wait_secs)
{
    int port, connected, resets_ok;
    u32 v, ms, delay_ms;

    if ((c->cap_a & 0x200) == 0) {
        if (c->cap_a & 0x100) {
            for (port = 1; port <= c->maxports; port++)
                WR32(c->base + OREG_RH_PORT(port), OPORT_PPS);
        } else {
            WR32(c->base + OREG_RH_STATUS, 0x00010000UL);
        }
        delay_ms = ((c->cap_a >> 24) & 0xFF) * 2;
        if (delay_ms < 20)
            delay_ms = 20;
        if (delay_ms > 500)
            delay_ms = 500;
        msleep((unsigned)delay_ms);
    }
    connected = 0;
    for (port = 1; port <= c->maxports; port++) {
        if (RD32(c->base + OREG_RH_PORT(port)) & OPORT_CCS)
            connected++;
    }
    if (!connected && wait_secs > 0) {
        qprintf("  C6: no OHCI device present. Plug a Full-/Low-Speed "
                "device now (waiting %d s, ESC to skip)...\n", wait_secs);
        while (wait_secs > 0 && !connected) {
            msleep(1000);
            wait_secs--;
            if (kbhit()) {
                getch();
                break;
            }
            for (port = 1; port <= c->maxports; port++) {
                if (RD32(c->base + OREG_RH_PORT(port)) & OPORT_CCS)
                    connected++;
            }
        }
    }
    if (!connected) {
        strcpy(c->port_note, "no OHCI connect observed");
        c->v_port = V_SKIP;
        return 0;
    }
    resets_ok = 0;
    for (port = 1; port <= c->maxports; port++) {
        v = RD32(c->base + OREG_RH_PORT(port));
        if ((v & OPORT_CCS) == 0)
            continue;
        WR32(c->base + OREG_RH_PORT(port), OPORT_PRS);
        for (ms = 0; ms < 500; ms++) {
            v = RD32(c->base + OREG_RH_PORT(port));
            if (v & OPORT_PRSC)
                break;
            msleep(1);
        }
        v = RD32(c->base + OREG_RH_PORT(port));
        if ((v & OPORT_PRSC) && (v & OPORT_PES)) {
            qprintf("  C6: OHCI port %d reset ok, %s-Speed\n", port,
                    (v & OPORT_LSDA) ? "Low" : "Full");
            resets_ok++;
        } else {
            qprintf("  C6: OHCI port %d reset FAILED (status=%08lX)\n",
                    port, v);
        }
        WR32(c->base + OREG_RH_PORT(port), v & OPORT_CHANGE);
    }
    sprintf(c->port_note, "%d connect(s), %d reset(s) ok",
            connected, resets_ok);
    c->v_port = resets_ok ? V_PASS : V_FAIL;
    return resets_ok > 0;
}

void legacy_run(LEGACY_CTRL *c, int wait_secs)
{
    int reset_ok, dma_ok;

    qprintf("  C1: requesting %s firmware handoff...\n",
            hc_name(c->pci.hctype));
    if (c->pci.hctype == HC_EHCI)
        ehci_handoff(c);
    else
        ohci_handoff(c);
    qprintf("  C1: %s - %s\n", verdict_name(c->v_handoff),
            c->handoff_note);
    qprintf("  C2: resetting %s controller...\n",
            hc_name(c->pci.hctype));
    reset_ok = (c->pci.hctype == HC_EHCI) ?
               ehci_reset(c) : ohci_reset(c);
    if (!reset_ok) {
        qprintf("  C2 failed - skipping C3-C6\n");
        return;
    }
    qprintf("  C2: PASS in %lu ms\n", c->reset_ms);
    qprintf("  C3: starting %s DMA proof...\n",
            hc_name(c->pci.hctype));
    dma_ok = (c->pci.hctype == HC_EHCI) ? ehci_dma(c) : ohci_dma(c);
    if (!dma_ok) {
        qprintf("  C3 failed - skipping C4/C6\n");
        return;
    }
    qprintf("  C3: %s - %s\n", verdict_name(c->v_dma), c->dma_note);
    qprintf("  C4: testing legacy IRQ delivery...\n");
    if (c->pci.hctype == HC_EHCI) {
        if (c->poll_only) {
            /* --poll-only forbids installing a protected-mode CPU ISR */
            c->v_irq = V_SKIP;
            strcpy(c->irq_note, "poll-only run: no ISR installed; CPU/PIC "
                   "delivery not tested");
        } else {
            ehci_irq(c);
        }
        qprintf("  C4: %s - %s\n", verdict_name(c->v_irq), c->irq_note);
        if (c->poll_only || c->v_irq == V_PASS) {
            qprintf("  C6: checking EHCI root ports...\n");
            ehci_ports(c, wait_secs);
        } else {
            c->v_port = V_SKIP;
            strcpy(c->port_note, "C4 failed; use --poll-only before running "
                   "C6");
            qprintf("  C6: SKIP - %s\n", c->port_note);
        }
    } else {
        ohci_irq(c);
        qprintf("  C4: %s - %s\n", verdict_name(c->v_irq), c->irq_note);
        qprintf("  C6: checking OHCI root ports...\n");
        ohci_ports(c, wait_secs);
    }
}

void legacy_cleanup(LEGACY_CTRL *c)
{
    u32 v, ms;
    u8 off;
    int halted, reset_ok, bme_off, release_ok;

    if (!c->mmio_ok)
        return;
    halted = 0;
    reset_ok = 0;
    if (c->pci.hctype == HC_EHCI) {
        WR32(c->op + EOP_USBINTR, 0);
        v = RD32(c->op + EOP_USBCMD);
        WR32(c->op + EOP_USBCMD, v & ~(ECMD_RUN | ECMD_ASE));
        irq_uninstall();
        for (ms = 0; ms < 100; ms++) {
            if (RD32(c->op + EOP_USBSTS) & ESTS_HCH) {
                halted = 1;
                break;
            }
            msleep(1);
        }
        if (halted) {
            WR32(c->op + EOP_USBCMD, ECMD_RESET);
            for (ms = 0; ms < 1000; ms++) {
                if ((RD32(c->op + EOP_USBCMD) & ECMD_RESET) == 0) {
                    reset_ok = 1;
                    break;
                }
                msleep(1);
            }
        }
        if (c->legsup_off != 0) {
            off = (u8)c->legsup_off;
            pci_write32(c->pci.bus, c->pci.dev, c->pci.fn,
                        (u8)(off + 4), c->legctl_orig);
            v = pci_read32(c->pci.bus, c->pci.dev, c->pci.fn, off);
            pci_write32(c->pci.bus, c->pci.dev, c->pci.fn, off,
                        v & ~0x01000000UL);
        }
    } else {
        WR32(c->base + OREG_INTR_DISABLE, 0xFFFFFFFFUL);
        irq_uninstall();
        v = RD32(c->base + OREG_CONTROL);
        WR32(c->base + OREG_CONTROL, v & ~OCTL_HCFS_MASK);
        halted = 1;
        msleep(10);
        WR32(c->base + OREG_COMMAND_STATUS, OCMD_HCR);
        for (ms = 0; ms < 100; ms++) {
            if ((RD32(c->base + OREG_COMMAND_STATUS) & OCMD_HCR) == 0) {
                reset_ok = 1;
                break;
            }
            msleep(1);
        }
    }

    /* Same DMA-ownership rule as qual_cleanup(), and the same consequence:
     * this PCI configuration write restores the Command register as found
     * (minus BME), which can switch BAR0 decode off. It is the MMIO cutoff;
     * c->mmio_ok stays true for report_legacy_controller(), which prints
     * cached facts only. */
    bme_off = pci_disable_bus_master(&c->pci);
    release_ok = reset_ok || bme_off;
    if (release_ok) {
        dos_free_all();
        c->dma_a = 0;
        c->dma_b = 0;
        c->dma_c = 0;
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

void report_legacy_controller(LEGACY_CTRL *c, int active_ran)
{
    PCIINFO *p;

    p = &c->pci;
    qprintf("\n==== %s Controller %02X:%02X.%X - %04X:%04X rev %02X ====\n",
            hc_name(p->hctype), p->bus, p->dev, p->fn,
            p->vid, p->did, p->rev);
    qprintf("  PCI: cmd=%04X (MSE=%d BME=%d INTxDis=%d) pin=INT%c# "
            "line=IRQ %u\n", p->cmd_orig,
            (p->cmd_orig & PCI_CMD_MSE) ? 1 : 0,
            (p->cmd_orig & PCI_CMD_BME) ? 1 : 0,
            (p->cmd_orig & PCI_CMD_INTX_OFF) ? 1 : 0,
            (p->ipin >= 1 && p->ipin <= 4) ? ('A' + p->ipin - 1) : '?',
            p->iline);
    qprintf("  PCI subsys: %04X:%04X\n", p->subsys_vid, p->subsys_did);
    qprintf("  PCI caps: PM=%d MSI=%d(en=%d) MSI-X=%d PCIe=%d\n",
            p->has_pm, p->has_msi, p->msi_enabled, p->has_msix,
            p->has_pcie);
    report_pci_status(p);
    report_pci_pm(p);
    qprintf("  BAR0: %08lX%s (raw lo=%08lX hi=%08lX)\n",
            p->bar_phys, p->bar_is64 ? " 64-bit" : " 32-bit",
            p->bar_lo, p->bar_hi);
    if (!c->mmio_ok) {
        qprintf("  MMIO: NOT ACCESSIBLE - Tier B/C skipped\n");
        report_mmio_dead(p);
        return;
    }
    if (p->hctype == HC_EHCI) {
        qprintf("  HCIVERSION %X.%02X  ports=%d PPC=%d "
                "HCSPARAMS=%08lX HCCPARAMS=%08lX\n",
                c->hciversion >> 8, c->hciversion & 0xFF,
                c->maxports, c->ppc, c->cap_a, c->cap_b);
        qprintf("  EHCI legacy cap: %s\n",
                c->legsup_off ? "present" : "absent");
    } else {
        qprintf("  HcRevision %X.%X  ports=%d power-switching=%d "
                "RhDescriptorA=%08lX RhDescriptorB=%08lX\n",
                c->hciversion >> 4, c->hciversion & 0xF,
                c->maxports, c->ppc, c->cap_a, c->cap_b);
    }
    if (active_ran) {
        qprintf("  --- Tier C results ---\n");
        qprintf("  C1 handoff:  %-4s  %s\n",
                verdict_name(c->v_handoff), c->handoff_note);
        qprintf("  C2 reset:    %-4s  reset in %lu ms\n",
                verdict_name(c->v_reset), c->reset_ms);
        qprintf("  C3 DMA:      %-4s  %s\n",
                verdict_name(c->v_dma), c->dma_note);
        qprintf("  C4 IRQ:      %-4s  %s\n",
                verdict_name(c->v_irq), c->irq_note);
        if (c->irq_foreign_count != 0)
            qprintf("               (%lu foreign interrupt(s) on this "
                    "vector - line masked for safety)\n",
                    c->irq_foreign_count);
        qprintf("  C6 ports:    %-4s  %s\n",
                verdict_name(c->v_port), c->port_note);
        qprintf("  C8 devices:  SKIP  xHCI-only identification path\n");
        if (c->cleanup_failed)
            qprintf("  Cleanup:     FAIL  %s\n", c->cleanup_note);
    }
    qprintf("  FACT type=%s id=%04X:%04X rev=%02X bar=%08lX irq=%u "
            "pin=%u hciver=%04X ports=%d ppc=%d\n",
            hc_name(p->hctype), p->vid, p->did, p->rev, p->bar_phys,
            p->iline, p->ipin, c->hciversion, c->maxports, c->ppc);
}

int legacy_final_verdict(LEGACY_CTRL *c, int active_requested)
{
    int qualified, warned, disqualified, tool_limited;

    qualified = 1;
    warned = 0;
    disqualified = 0;
    tool_limited = 0;
    qprintf("\n---- Verdict for %s %04X:%04X ----\n",
            hc_name(c->pci.hctype), c->pci.vid, c->pci.did);
    if (c->pci.ipin == 0) {
        qprintf("  DISQUALIFIED: Interrupt Pin = 0 - no legacy INTx\n");
        qualified = 0;
        disqualified = 1;
    }
    if (!c->mmio_ok) {
        qualified = 0;
        if (report_mmio_unavailable(&c->pci, active_requested))
            disqualified = 1;
        else
            tool_limited = 1;
        if (!active_requested)
            qprintf("  Probe-only run: active tests NOT run.\n");
        qprintf("  ==> NOT QUALIFIED for cross-target Win98/Win2000 use\n");
        if (tool_limited && !disqualified)
            qprintf("      No controller fault was inferred from the "
                    "temporary state above.\n");
        return 0;
    }
    if (c->mmio_ok && c->maxports == 0) {
        qprintf("  DISQUALIFIED: controller reports no root ports\n");
        qualified = 0;
    }
    if (!active_requested) {
        qprintf("  Probe-only run: %s so far. Active tests NOT run.\n",
                qualified ? "no disqualifiers" : "disqualified");
        return 0;
    }
    if (c->poll_only) {
        qprintf("  Poll-only run: no interrupt handler was installed.\n");
        if (c->v_reset != V_PASS) {
            qprintf("  CONTROLLER FAILURE: halt/reset (C2) failed\n");
            qualified = 0;
        }
        if (c->v_dma != V_PASS) {
            qprintf("  CONTROLLER FAILURE: DMA proof (C3) failed\n");
            qualified = 0;
        }
        if (c->cleanup_failed) {
            qprintf("  CONTROLLER FAILURE: cleanup: %s\n",
                    c->cleanup_note);
            qualified = 0;
        }
        if (!qualified) {
            qprintf("  ==> NOT QUALIFIED - a disqualifier above was hit "
                    "(C4 interrupt delivery was not tested)\n");
        } else {
            qprintf("  ==> PROVISIONAL - C2/C3%s completed with no ISR and "
                    "no fault.\n"
                    "      C4 interrupt delivery NOT tested; re-run without "
                    "--poll-only to earn a verdict.\n",
                    c->v_port == V_PASS ? "/C6 port reset" : "");
        }
        return 0;
    }
    if (c->v_handoff == V_WARN)
        warned = 1;
    if (c->v_reset != V_PASS) {
        qprintf("  DISQUALIFIED: halt/reset (C2) failed\n");
        qualified = 0;
    }
    if (c->v_dma != V_PASS) {
        qprintf("  DISQUALIFIED: DMA proof (C3) failed\n");
        qualified = 0;
    }
    if (c->v_irq == V_FAIL) {
        qprintf("  DISQUALIFIED: legacy IRQ delivery (C4) failed for "
                "Win98/PIC-HAL use\n");
        qprintf("      Win2000 APIC-HAL routing remains inconclusive.\n");
        qualified = 0;
    } else if (c->v_irq != V_PASS)
        warned = 1;
    if (c->cleanup_failed) {
        qprintf("  DISQUALIFIED: cleanup could not quiesce the controller: "
                "%s\n", c->cleanup_note);
        qualified = 0;
    }
    if (c->v_port != V_PASS)
        warned = 1;
    if (qualified) {
        /* No APIC-HAL caveat here - see the note on the same branch in
         * report.c. The FAIL branch's conditional sibling above stays. */
        qprintf("  ==> CONTROLLER %s for cross-target %s development%s\n",
                warned ? "QUALIFIED (with warnings)" : "QUALIFIED",
                hc_name(c->pci.hctype),
                warned ? " - review WARN/SKIP lines above" : "");
    } else {
        qprintf("  ==> NOT QUALIFIED for cross-target Win98/Win2000 %s "
                "use\n", hc_name(c->pci.hctype));
    }
    return qualified;
}
