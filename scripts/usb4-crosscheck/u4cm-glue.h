/*
 * u4cm-glue.h - the plain-C interface between the usb4-emu cross-check
 * harness and xhci98's TbtU4 core (see u4cm-glue.c). C89, no DDK types.
 *
 * Written for xhci98, 2026-10-15. SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef U4CM_GLUE_H
#define U4CM_GLUE_H

typedef struct XC_OPS {
    unsigned long (*rd)(unsigned long off);
    void (*wr)(unsigned long off, unsigned long v);
    void (*advance_us)(unsigned long us);
    unsigned long (*clock_us)(void);
    void (*log)(const char *label, unsigned long value);
} XC_OPS;

#define XC_STATE            1
#define XC_WHY              2
#define XC_BONDED           3
#define XC_PCIE_UP          4
#define XC_PCIE_PRESERVED   5
#define XC_USB3_UP          6
#define XC_TMU_OK           7
#define XC_PCIE_DOWN        8
#define XC_HOST_QUIRKS      9
#define XC_INERT            10
#define XC_REQUESTS         11
#define XC_RETRIES          12
#define XC_TIMEOUTS         13
#define XC_LD_CALLS         14
#define XC_LD_CLOSED        15
#define XC_HOTPLUGS         16
#define XC_ACKS             17
#define XC_HOST_ADP_PRESENT 18
#define XC_DEV_ADP_PRESENT  19      /* `port` is (port << 8) | adapter */
#define XC_ROP_ISSUED       20
#define XC_ROP_FAILED       21
#define XC_ROP_TIMEOUTS     22
#define XC_DPR              23
#define XC_CR_TIMEOUTS      24
#define XC_USB3_BUILT       25
#define XC_USB3_FAILED      26
#define XC_PCIE_BUILT       27
#define XC_CONNECTS         28
#define XC_DROM_CRC         29
#define XC_TMU_ENABLED      30
#define XC_ENUMERATED       31

long xc_start(const XC_OPS *ops, void *arena, unsigned long phys,
              unsigned long bytes, int usb3);
/* As xc_start, with XhciCmPreOsCleanup's value. */
long xc_start_clean(const XC_OPS *ops, void *arena, unsigned long phys,
                    unsigned long bytes, int usb3, unsigned long preOsCleanup);
/* The device router's DROM vendor and model names on `port` ("" none). */
const char *xc_vendor_name(unsigned long port);
const char *xc_model_name(unsigned long port);
void xc_poll(void);
void xc_stop(void);
unsigned long xc_get(int what, unsigned long port);
int xc_state_up(void);
int xc_state_empty(void);
unsigned long xc_quirk_usb3(void);

#endif /* U4CM_GLUE_H */
