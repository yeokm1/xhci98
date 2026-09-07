/*
 * irq.c - DPMI protected-mode vector and 8259 lifecycle for C4.
 *
 * The handler is a deliberately one-shot assembly path. The target line
 * remains masked until the controller's own source is pending, so DOS/32A's
 * 16-bit interrupt-reflection thunk is never used for the test interrupt.
 */

#include <dos.h>
#include <conio.h>
#include "qual.h"

#define PIC1_DATA 0x21
#define PIC2_DATA 0xA1
#define ELCR1     0x4D0
#define ELCR2     0x4D1

extern void __interrupt irq_pm_entry(void);
extern void irq_pm_entry_end(void);

IRQ_ISR_STATE irq_isr_state;

static u16 old_selector;
static u32 old_offset;
static u32 code_linear;
static u32 code_size;
static u8 saved_mask1;
static u8 saved_mask2;
static int g_vec = -1;
static int installed;
static int code_locked;
static int data_locked;
static const char *last_error_step;
static u16 last_error_code;

static void irq_record_error(const char *step, u16 code)
{
    if (last_error_step == 0) {
        last_error_step = step;
        last_error_code = code;
    }
}

static int dpmi_get_pm_vector(int vec, u16 *selector, u32 *offset)
{
    union REGS r;

    r.x.eax = 0x0204;
    r.x.ebx = (u32)vec;
    int386(0x31, &r, &r);
    if (r.x.cflag) {
        irq_record_error("get protected-mode vector", (u16)r.x.eax);
        return 0;
    }
    *selector = (u16)r.x.ecx;
    *offset = r.x.edx;
    return 1;
}

static int dpmi_set_pm_vector(int vec, u16 selector, u32 offset,
                              const char *step)
{
    union REGS r;

    r.x.eax = 0x0205;
    r.x.ebx = (u32)vec;
    r.x.ecx = (u32)selector;
    r.x.edx = offset;
    int386(0x31, &r, &r);
    if (r.x.cflag) {
        irq_record_error(step, (u16)r.x.eax);
        return 0;
    }
    return 1;
}

static int dpmi_region(u16 service, u32 linear, u32 size,
                       const char *step)
{
    union REGS r;

    r.x.eax = service;
    r.x.ebx = linear >> 16;
    r.x.ecx = linear & 0xFFFFUL;
    r.x.esi = size >> 16;
    r.x.edi = size & 0xFFFFUL;
    int386(0x31, &r, &r);
    if (r.x.cflag) {
        irq_record_error(step, (u16)r.x.eax);
        return 0;
    }
    return 1;
}

/*
 * **Mask one line at the 8259 with no vector hooked, and put it back.**
 *
 * The C4 path above installs a handler and keeps the line masked until its own
 * source is pending. The OHCI C3 path wants neither: it enables a controller
 * interrupt only to watch the status bit, and installs no handler at all. That
 * left the line as firmware had it - and if firmware left it unmasked, the
 * frame interrupt asserts every millisecond into whatever vector the BIOS put
 * there, reached through DOS/32A's 16-bit reflection thunk, which is the path
 * design record 01 section 6 records as faulting. The 2026-09-07 audit's I3.
 *
 * Separate from `irq_mask_current` because that one reads the line out of the
 * installed ISR's state, and here there is no ISR. One line at a time, with
 * one saved mask byte, because that is all any caller needs.
 */
static u8 line_saved_mask;
static int line_saved_which = -1;   /* -1 none, 0 = PIC1, 1 = PIC2 */

int irq_line_mask(unsigned line)
{
    if (line_saved_which >= 0)
        return 0;               /* one at a time; a second is a caller bug */
    if (line > 15)
        return 0;
    _disable();
    if (line < 8) {
        line_saved_which = 0;
        line_saved_mask = (u8)inp(PIC1_DATA);
        outp(PIC1_DATA, line_saved_mask | (u8)(1 << line));
    } else {
        line_saved_which = 1;
        line_saved_mask = (u8)inp(PIC2_DATA);
        outp(PIC2_DATA, line_saved_mask | (u8)(1 << (line - 8)));
    }
    _enable();
    return 1;
}

void irq_line_restore(void)
{
    if (line_saved_which < 0)
        return;
    _disable();
    if (line_saved_which == 0)
        outp(PIC1_DATA, line_saved_mask);
    else
        outp(PIC2_DATA, line_saved_mask);
    _enable();
    line_saved_which = -1;
}

static void irq_mask_current(void)
{
    int irq;

    irq = (int)irq_isr_state.irq;
    _disable();
    if (irq < 8)
        outp(PIC1_DATA, inp(PIC1_DATA) | (1 << irq));
    else
        outp(PIC2_DATA, inp(PIC2_DATA) | (1 << (irq - 8)));
    _enable();
}

static void irq_restore_masks(void)
{
    _disable();
    outp(PIC1_DATA, saved_mask1);
    outp(PIC2_DATA, saved_mask2);
    _enable();
}

static void irq_release_locks(void)
{
    if (data_locked) {
        if (dpmi_region(0x0601, (u32)&irq_isr_state,
                        (u32)sizeof(irq_isr_state), "unlock ISR data"))
            data_locked = 0;
    }
    if (code_locked) {
        if (dpmi_region(0x0601, code_linear, code_size,
                        "unlock ISR code"))
            code_locked = 0;
    }
}

int irq_install(PCIINFO *p, int hctype, volatile u8 *base,
                volatile u8 *aux)
{
    int irq;
    void __far *handler;

    last_error_step = 0;
    last_error_code = 0;
    irq = p->iline;
    if (installed || irq < 1 || irq > 15 || irq == 2) {
        irq_record_error("validate IRQ handler state", 0);
        return 0;
    }

    irq_isr_state.op = (u32)base;
    irq_isr_state.rt = (u32)aux;
    irq_isr_state.hctype = (u32)hctype;
    irq_isr_state.irq = (u32)irq;
    irq_isr_state.count = 0;
    irq_isr_state.foreign_count = 0;
    g_vec = (irq < 8) ? (8 + irq) : (0x70 + irq - 8);

    _disable();
    saved_mask1 = (u8)inp(PIC1_DATA);
    saved_mask2 = (u8)inp(PIC2_DATA);
    if (irq < 8)
        outp(PIC1_DATA, saved_mask1 | (1 << irq));
    else
        outp(PIC2_DATA, saved_mask2 | (1 << (irq - 8)));
    _enable();

    if (!dpmi_get_pm_vector(g_vec, &old_selector, &old_offset))
        goto cleanup;

    code_linear = (u32)irq_pm_entry;
    code_size = (u32)irq_pm_entry_end - code_linear;
    if (code_size == 0 || code_size > 4096UL) {
        irq_record_error("validate ISR code range", 0);
        goto cleanup;
    }
    if (!dpmi_region(0x0600, code_linear, code_size, "lock ISR code"))
        goto cleanup;
    code_locked = 1;
    if (!dpmi_region(0x0600, (u32)&irq_isr_state,
                     (u32)sizeof(irq_isr_state), "lock ISR data"))
        goto cleanup;
    data_locked = 1;

    handler = (void __far *)irq_pm_entry;
    if (!dpmi_set_pm_vector(g_vec, (u16)FP_SEG(handler), FP_OFF(handler),
                            "install protected-mode vector"))
        goto cleanup;

    installed = 1;
    return 1;

cleanup:
    irq_release_locks();
    irq_restore_masks();
    g_vec = -1;
    return 0;
}

int irq_unmask(void)
{
    int irq;

    if (!installed) {
        irq_record_error("unmask uninstalled IRQ", 0);
        return 0;
    }
    irq = (int)irq_isr_state.irq;
    _disable();
    if (irq < 8) {
        outp(PIC1_DATA, inp(PIC1_DATA) & ~(1 << irq));
    } else {
        outp(PIC2_DATA, inp(PIC2_DATA) & ~(1 << (irq - 8)));
        outp(PIC1_DATA, inp(PIC1_DATA) & ~(1 << 2));
    }
    _enable();
    return 1;
}

int irq_uninstall(void)
{
    int ok;

    if (!installed)
        return 1;

    last_error_step = 0;
    last_error_code = 0;
    irq_mask_current();
    /* If the vector cannot be given back, our handler is still the one the
     * CPU will dispatch to. Bail out with everything intact - locks held so
     * the handler and its state stay resident, `installed` set so a later
     * qual_cleanup() retries, and this line left masked. Releasing the locks
     * or restoring the masks here would arm an interrupt into pageable
     * memory. */
    if (!dpmi_set_pm_vector(g_vec, old_selector, old_offset,
                            "restore protected-mode vector"))
        return 0;

    installed = 0;
    ok = 1;
    irq_release_locks();
    if (code_locked || data_locked)
        ok = 0;
    irq_restore_masks();

    g_vec = -1;
    irq_isr_state.op = 0;
    irq_isr_state.rt = 0;
    irq_isr_state.hctype = 0;
    irq_isr_state.irq = 0;
    return ok;
}

const char *irq_error_step(void)
{
    return last_error_step;
}

u16 irq_error_code(void)
{
    return last_error_code;
}

int irq_elcr_level(int irq)
{
    u16 elcr;

    elcr = (u16)(inp(ELCR1) | (inp(ELCR2) << 8));
    return (elcr >> irq) & 1;
}
